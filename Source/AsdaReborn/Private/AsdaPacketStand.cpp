// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ СТЕНД ПОБАЙТОВОЙ СВЕРКИ ТЕЛ ИСХОДЯЩИХ ПАКЕТОВ (волна 3 плана дублей, п.10: `FAsdaPacketWriter`).
//   Отчёт: `Reports/dedup_audit/wave3_packet_writer.md`.
//
// ★ ЗАЧЕМ. Сервер СТАРЫЙ (закон `old-server-protocol-compat`): перевод отправителя на `FAsdaPacketWriter`
//   допустим, только если тело пакета совпадает с прежним кодом БАЙТ В БАЙТ. Стенд зовёт НАСТОЯЩИЕ
//   `UAsdaNetSubsystem::Send*` (не копии) на детерминированном наборе входов и пишет в файл, что каждый
//   из них отдал конверту: опкод, вид конверта, флаг пада, длину и hex тела.
//   Порядок доказательства: прогон на коде ДО миграции (эталонный файл) → миграция → прогон на коде
//   ПОСЛЕ → побайтовое сравнение файлов. Входы одинаковы, потому что сам стенд между прогонами не меняется.
//
// ★ КАК ПЕРЕХВАТ НЕ МЕНЯЕТ ИГРУ. `UAsdaNetSubsystem::SendCaptureForStand` в игре пуст; стенд ставит его на
//   СВОЙ объект подсистемы (не на живую), и `SendSecured`/`SendMd5Signed`/`SendMenuPacket` отдают payload
//   в перехватчик — в сеть ничего не уходит, счётчик секрета не расходуется. Перехват стоит ПОСЛЕ проверок,
//   влияющих на тело (длина/указатель), с реальным падом провода; сокет и гейты (состояние живой сессии)
//   при перехватчике пропускаются. 6124 читает ячейки контейнера 2 из `StandContainer2CodesForStand`
//   (у объекта стенда нет инвентаря). Ключи консоли стенд меняет С ИСХОДНЫМ приоритетом и возвращает
//   значение+приоритет (строки PACKET_STAND_CVAR в логе).
//
// ★ ВХОДЫ. Для каждого отправителя — Cases случаев от генератора xorshift64 с зерном = FNV-1a(имя):
//   пулы граничных значений (0, ±1, 127/128, 255/256, 32767/32768, 65535/65536, INT32_MIN/MAX…) вперемешку
//   со случайными; строки пустые/короткие/ровно на границах полей (19, 20, 64, 65, 127, 128, 199, 200, 499,
//   500, 501…), кириллица с Ё/ё и « » – — … №, апострофы, непредставимые в cp1251 символы; записи
//   предметов короче/ровно/длиннее 16 и 57 б; состояние подсистемы (свой номер, charId, канал, аккаунт,
//   профессия/класс/уровень, кэши приятелей/зова/соулмейта, троттлинг 6098, блок 6616) и ключи
//   `asda.PartyFind.LegacyBossJobByte`, `asda.Warehouse.RawPageNo`, `asda.Soul.AllowEnter` — тоже случайно.
//
// Консоль: `asda.Net.PacketStand <файл> [случаев=400] [проходов=1]`. Проход 2+ пишет `<файл>.passN` —
//   для проверки, что вывод детерминирован (неинициализированный байт тела дал бы расхождение проходов).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                 // AsdaBytes::WrI32 — ключ записи приятеля в состоянии случая
#include "Login/AsdaLoginData.h"       // FAsdaLoginData::EncodeCp1251 — имя в очереди приглашений
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "UObject/Package.h"

// Друг `UAsdaNetSubsystem` (объявлен в заголовке): стенду нужны закрытые поля состояния и перехватчик.
struct FAsdaPacketStand
{
	// Точка входа консольной команды.
	static void Run(const TArray<FString>& Args);
};

namespace AsdaPacketStandLocal
{
	// Детерминированный генератор (xorshift64): одинаковые входы в обоих прогонах.
	struct FRng
	{
		uint64 S;
		explicit FRng(uint64 Seed) : S(Seed ? Seed : 0x9E3779B97F4A7C15ull) {}
		uint32 Next() { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32)(S >> 32); }
		int32 Range(int32 N) { return N > 0 ? (int32)(Next() % (uint32)N) : 0; }
		bool Coin() { return (Next() & 1u) != 0; }
	};

	// FNV-1a от имени отправителя — зерно генератора.
	static uint64 Hash(const TCHAR* Name)
	{
		uint64 H = 1469598103934665603ull;
		for (const TCHAR* C = Name; *C; ++C) { H ^= (uint64)(uint16)*C; H *= 1099511628211ull; }
		return H;
	}

	// int32: граничные значения либо случайное (каждое четвёртое).
	static int32 GenI32(FRng& R)
	{
		static const int32 Pool[] = {
			0, 1, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 19, 20, 41, 64, 100, 127, 128, 200, 254, 255, 256,
			1200, 1201, 9999, 10000, 10001, 32767, 32768, 65535, 65536, -2, -127, -128, -129, -255, -256,
			-32767, -32768, -32769, -65536, 0x12345678, (int32)0x87654321, (int32)0x7FFFFFFF, (int32)0x80000000,
			0x000125AC, 0x00FF00FF };
		if (R.Range(4) == 0) { return (int32)R.Next(); }
		return Pool[R.Range(UE_ARRAY_COUNT(Pool))];
	}

	// uint8: граничные значения либо случайное.
	static uint8 GenU8(FRng& R)
	{
		static const uint8 Pool[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 127, 128, 200, 254, 255 };
		if (R.Range(4) == 0) { return (uint8)R.Next(); }
		return Pool[R.Range(UE_ARRAY_COUNT(Pool))];
	}

	// uint32 маска.
	static uint32 GenMask(FRng& R)
	{
		static const uint32 Pool[] = { 0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x12345678u, 0x0000FFFFu };
		if (R.Range(3) == 0) { return R.Next(); }
		return Pool[R.Range(UE_ARRAY_COUNT(Pool))];
	}

	// Строка ровно из Len символов (кириллица, латиница, апостроф, пробел, «, №).
	static FString MakeLen(int32 Len, int32 Shift)
	{
		static const TCHAR* Pat = TEXT("Жa'1ё Z«Я№b—");
		const int32 P = FCString::Strlen(Pat);
		FString S;
		S.Reserve(Len);
		for (int32 i = 0; i < Len; ++i) { S.AppendChar(Pat[(i + Shift) % P]); }
		return S;
	}

	// Строка: пустые, короткие, кириллица, спецсимволы cp1251, апострофы, непредставимые, длины на границах полей.
	static FString GenStr(FRng& R)
	{
		static const TCHAR* Fixed[] = {
			TEXT(""), TEXT("a"), TEXT("Rimy"), TEXT("sitxovski"), TEXT("GM_sitxovski"), TEXT("Имя"),
			TEXT("АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя"),
			TEXT("O'Neil's 'x'"), TEXT("'"), TEXT("«Цитата» – тире — многоточие… №5"),
			TEXT("漢字€ÿ \U0001F600"), TEXT("   пробелы  "), TEXT("a\tb\nc"), TEXT("0123456789") };
		static const int32 Lens[] = { 15, 16, 17, 18, 19, 20, 21, 23, 24, 25, 63, 64, 65, 66, 126, 127, 128, 129,
			171, 172, 178, 179, 180, 198, 199, 200, 201, 498, 499, 500, 501, 549, 550, 551, 700 };
		if (R.Coin()) { return Fixed[R.Range(UE_ARRAY_COUNT(Fixed))]; }
		return MakeLen(Lens[R.Range(UE_ARRAY_COUNT(Lens))], R.Range(12));
	}

	// Малый пул имён — чтобы 4153 находил приглашение в очереди (совпадение имени).
	static FString GenName(FRng& R)
	{
		static const TCHAR* Names[] = { TEXT("Rimy"), TEXT("sitxovski"), TEXT("Имя"), TEXT("O'Neil") };
		return R.Coin() ? FString(Names[R.Range(UE_ARRAY_COUNT(Names))]) : GenStr(R);
	}

	// Байтовая запись длины из пула граничных (записи предмета 57, ссылки 11, строки аукциона 45/98…).
	static TArray<uint8> GenBytes(FRng& R)
	{
		static const int32 Lens[] = { 0, 1, 10, 11, 14, 15, 16, 17, 22, 40, 41, 42, 44, 45, 46, 54, 55, 56, 57, 58,
			97, 98, 99, 130 };
		TArray<uint8> A;
		A.SetNum(Lens[R.Range(UE_ARRAY_COUNT(Lens))]);
		const int32 Mode = R.Range(3);
		for (int32 i = 0; i < A.Num(); ++i)
		{
			A[i] = (Mode == 0) ? (uint8)R.Next() : (Mode == 1 ? (uint8)(i * 7 + 3) : (uint8)0xFF);
		}
		return A;
	}

	// Массив int32 длины из пула (слоты ремонта 41, материалы 10, покупка 7, цели 20…).
	static TArray<int32> GenInts(FRng& R)
	{
		static const int32 Lens[] = { 0, 1, 2, 5, 6, 7, 8, 9, 10, 11, 19, 20, 21, 40, 41, 42, 60 };
		TArray<int32> A;
		A.SetNum(Lens[R.Range(UE_ARRAY_COUNT(Lens))]);
		for (int32& V : A) { V = GenI32(R); }
		return A;
	}

	// Набор строк аукциона/корзины (0..12 записей).
	static TArray<TArray<uint8>> GenRows(FRng& R)
	{
		TArray<TArray<uint8>> Rows;
		const int32 N = R.Range(13);
		for (int32 i = 0; i < N; ++i) { Rows.Add(GenBytes(R)); }
		return Rows;
	}

	// Ключ консоли — выставить значение на время случая С ТЕКУЩИМ приоритетом ключа (SetBy не поднимается:
	//   прежняя форма писала `ECVF_SetByConsole`, и после стенда ключ оставался «выставленным из консоли»,
	//   так что последующая запись ini/кода с меньшим приоритетом молча отвергалась движком).
	static void SetCVar(const TCHAR* Name, int32 V)
	{
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			C->Set(V, (EConsoleVariableFlags)((uint32)C->GetFlags() & (uint32)ECVF_SetByMask));
		}
	}

	// Исходное состояние ключа консоли: значение и приоритет (SetBy) — для восстановления после прогона.
	struct FSavedCVar
	{
		const TCHAR* Name = nullptr;
		bool bFound = false;
		int32 Value = 0;
		EConsoleVariableFlags SetBy = ECVF_SetByConstructor;
	};

	// Снять исходное состояние ключа.
	static FSavedCVar SaveCVar(const TCHAR* Name)
	{
		FSavedCVar S;
		S.Name = Name;
		if (const IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			S.bFound = true;
			S.Value = C->GetInt();
			S.SetBy = (EConsoleVariableFlags)((uint32)C->GetFlags() & (uint32)ECVF_SetByMask);
		}
		return S;
	}

	// Вернуть значение с исходным приоритетом и проверить, что и значение, и приоритет совпали с исходными.
	static void RestoreCVar(const FSavedCVar& S)
	{
		IConsoleVariable* C = S.bFound ? IConsoleManager::Get().FindConsoleVariable(S.Name) : nullptr;
		if (!C) { return; }
		C->Set(S.Value, S.SetBy);
		const EConsoleVariableFlags Now = (EConsoleVariableFlags)((uint32)C->GetFlags() & (uint32)ECVF_SetByMask);
		const bool bSame = (C->GetInt() == S.Value) && (Now == S.SetBy);
		UE_LOG(LogTemp, Display, TEXT("PACKET_STAND_CVAR %s: значение=%d SetBy=%s (исходно %d, %s) — %s"),
			S.Name, C->GetInt(), GetConsoleVariableSetByName(Now), S.Value, GetConsoleVariableSetByName(S.SetBy),
			bSame ? TEXT("восстановлен") : TEXT("НЕ ВОССТАНОВЛЕН"));
	}

	// Одна отправка, пойманная перехватчиком.
	struct FCap
	{
		uint16 Opcode = 0;
		TCHAR Envelope = 0;   // 'S' = SendSecured, '5' = SendMd5Signed, 'M' = SendMenuPacket
		bool bPad = false;
		TArray<uint8> Body;
	};

	// Отправитель под стендом: имя и вызов со случайными входами.
	struct FCase
	{
		const TCHAR* Name;
		TFunction<bool(UAsdaNetSubsystem*, FRng&)> Call;
	};
}

void FAsdaPacketStand::Run(const TArray<FString>& Args)
{
	using namespace AsdaPacketStandLocal;
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("asda.Net.PacketStand <файл> [случаев=400] [проходов=1]"));
		return;
	}
	const FString OutPath = Args[0];
	const int32 Cases  = (Args.Num() > 1) ? FMath::Max(1, FCString::Atoi(*Args[1])) : 400;
	const int32 Passes = (Args.Num() > 2) ? FMath::Max(1, FCString::Atoi(*Args[2])) : 1;

	// Собственный экземпляр подсистемы (Within = GameInstance ⇒ внешний объект обязан быть UGameInstance).
	UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
	UAsdaNetSubsystem* Net = NewObject<UAsdaNetSubsystem>(GI);
	GI->AddToRoot();
	Net->AddToRoot();

	TArray<FCap> Caps;
	Net->SendCaptureForStand = [&Caps](uint16 Op, const uint8* P, int32 Len, bool bPad, TCHAR Env)
	{
		FCap& C = Caps.AddDefaulted_GetRef();
		C.Opcode = Op; C.Envelope = Env; C.bPad = bPad;
		if (P && Len > 0) { C.Body.Append(P, Len); }
	};

	static const TCHAR* kCVarJob  = TEXT("asda.PartyFind.LegacyBossJobByte");
	static const TCHAR* kCVarPage = TEXT("asda.Warehouse.RawPageNo");
	static const TCHAR* kCVarSoul = TEXT("asda.Soul.AllowEnter");
	static const TCHAR* kCVarF2   = TEXT("asda.Warehouse.LegacyField2");   // 5008/5010: эталонная индексация поля +2
	const FSavedCVar SavedJob = SaveCVar(kCVarJob), SavedPage = SaveCVar(kCVarPage), SavedSoul = SaveCVar(kCVarSoul),
		SavedF2 = SaveCVar(kCVarF2);

	// Состояние подсистемы перед случаем — тоже из генератора.
	auto SetupState = [](UAsdaNetSubsystem* N, FRng& R)
	{
		N->SelfUserNum = (uint16)GenI32(R);
		N->bSelfUserNumKnown = R.Coin();
		N->SelectedCharId = GenI32(R);
		N->CurrentChannel = GenI32(R);
		N->UserIndex = (uint32)GenI32(R);
		N->SelfJob = GenI32(R);
		N->SelfClassId = GenI32(R);
		N->SelfLevel = GenU8(R);
		N->ExchangeNpcId = GenI32(R);
		N->KnownClanIndex = GenI32(R);
		N->SelfClanRank = GenU8(R);
		N->EnterMapId = GenI32(R);
		N->PortalChannelParam = GenU8(R);
		N->CampWarToken1 = GenU8(R);
		N->CampWarToken2 = GenU8(R);
		N->Unacked6616 = R.Range(4);
		N->bAnnounce6616Blocked = (R.Range(4) == 0);
		N->LastNpcExchangeSendTime = R.Coin() ? 0.0 : FPlatformTime::Seconds() - 1.0;
		// ★ 27.09.2026: суточный отказ 6059 — к конструктору эталона БЕЗ розыгрыша (порядок зерна прежних случаев не сдвигается).
		N->ResetEventNpcDayThrottle();
		N->PendingFriendInvites.Reset();
		for (int32 i = 0, Cnt = R.Range(4); i < Cnt; ++i)
		{
			TArray<uint8> Inv = GenBytes(R);
			if (R.Coin())
			{
				Inv.SetNumZeroed(22 + R.Range(20));
				FAsdaLoginData::EncodeCp1251(GenName(R), Inv.GetData(), 19);
				Inv[19] = R.Coin() ? 0 : Inv[19];
				Inv[20] = (uint8)R.Next(); Inv[21] = (uint8)R.Next();
			}
			N->PendingFriendInvites.Add(MoveTemp(Inv));
		}
		N->FriendEntries.Reset();
		for (int32 i = 0, Cnt = R.Range(4); i < Cnt; ++i)
		{
			TArray<uint8> E = GenBytes(R);
			if (R.Coin())
			{
				E.SetNum(31 + R.Range(30));
				for (uint8& B : E) { B = (uint8)R.Next(); }
				static const int32 Ids[] = { 0, 1, -1, 123456 };
				AsdaBytes::WrI32(E.GetData(), Ids[R.Range(4)]);
			}
			N->FriendEntries.Add(MoveTemp(E));
		}
		N->LastMateRecall = GenBytes(R);
		if (R.Coin()) { N->LastMateRecall.SetNum(42 + R.Range(3)); for (uint8& B : N->LastMateRecall) { B = (uint8)R.Next(); } }
		N->SoulmateSharedChars.Reset();
		N->SoulmateSharedChars.SetNum(3);
		for (FAsdaSoulmateSharedChar& S : N->SoulmateSharedChars)
		{
			static const uint8 Idx[] = { 0, 9, 10, 11, 12, 13 };
			static const int32 Own[] = { 0, -1, 1, 0x000125AC };
			S.Charidx = Idx[R.Range(6)];
			S.OwnerUserIndex = Own[R.Range(4)];
		}
		SetCVar(kCVarJob,  R.Coin() ? 1 : 0);
		SetCVar(kCVarPage, R.Coin() ? 1 : 0);
		SetCVar(kCVarSoul, R.Coin() ? 1 : 0);
	};

	using N_t = UAsdaNetSubsystem;
	const TArray<FCase> All = {
		// ── Конверт «меню игрока» (SendMenuPacket) ──
		{ TEXT("SendClanInvite4108"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendClanInvite4108(A); } },
		{ TEXT("SendFriendInvite4150"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendFriendInvite4150(A); } },
		{ TEXT("SendExchangeReq5282"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const bool B = R.Coin(); return N->SendExchangeReq5282(A, B); } },
		{ TEXT("SendExchangeCancel5286"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendExchangeCancel5286(A); } },
		{ TEXT("SendExchangeAnswer5284"), [](N_t* N, FRng& R) { const uint8 C = GenU8(R); const int32 A = GenI32(R); const bool B = R.Coin(); return N->SendExchangeAnswer5284(C, A, B); } },   // ★ 29.09.2026, волна О1
		{ TEXT("SendExchangeClose5286"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendExchangeClose5286(A); } },   // ★ 29.09.2026, волна О1
		{ TEXT("SendExchangeAddItem5288"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const int32 S = GenI32(R); const int32 T = GenI32(R); const int32 C = GenI32(R); const int32 Q = GenI32(R); return N->SendExchangeAddItem5288(A, S, T, C, Q); } },   // ★ 29.09.2026, волна О2
		{ TEXT("SendExchangeRemoveItem5290"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const int32 T = GenI32(R); const int32 C = GenI32(R); const int32 Q = GenI32(R); return N->SendExchangeRemoveItem5290(A, T, C, Q); } },   // ★ 29.09.2026, волна О2
		{ TEXT("SendExchangeFix5291"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint8 L = GenU8(R); int32 T[5], S[5], Q[5]; for (int32 k = 0; k < 5; ++k) { T[k] = GenI32(R); S[k] = GenI32(R); Q[k] = GenI32(R); } return N->SendExchangeFix5291(A, L, T, S, Q); } },   // ★ 30.09.2026, волна О3
		{ TEXT("SendExchangeComplete5293"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendExchangeComplete5293(A); } },   // ★ 30.09.2026, волна О3
		{ TEXT("SendTargetCharInfo5478"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendTargetCharInfo5478(A, B, C); } },
		{ TEXT("SendDuelReq4302"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const FString S = GenStr(R); const int32 C = GenI32(R), D = GenI32(R); return N->SendDuelReq4302(A, B, S, C, D); } },
		{ TEXT("SendGetUserId6082"), [](N_t* N, FRng& R) { const FString S = GenStr(R); return N->SendGetUserId6082(S); } },
		// ── Конверт без секрета (SendMd5Signed) ──
		{ TEXT("SendSoulmateResponse5202"), [](N_t* N, FRng& R) { const bool B = R.Coin(); const int32 A = GenI32(R), C = GenI32(R); const uint8 b7 = GenU8(R), b8 = GenU8(R), b9 = GenU8(R), Ch = GenU8(R); const FString S = GenStr(R); const uint8 Sk = GenU8(R); return N->SendSoulmateResponse5202(B, A, C, b7, b8, b9, Ch, S, Sk); } },
		{ TEXT("SendSoulmateBreakReq5204"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendSoulmateBreakReq5204(A); } },
		{ TEXT("SendSoulmateBreakResp5206"), [](N_t* N, FRng& R) { const bool B = R.Coin(); const int32 A = GenI32(R), C = GenI32(R); const uint8 b7 = GenU8(R), b8 = GenU8(R), Ch = GenU8(R); const FString S = GenStr(R); return N->SendSoulmateBreakResp5206(B, A, C, b7, b8, Ch, S); } },
		{ TEXT("SendSoulmateModes5221"), [](N_t* N, FRng& R) { const uint8 a = GenU8(R), b = GenU8(R), c = GenU8(R), d = GenU8(R); return N->SendSoulmateModes5221(a, b, c, d); } },
		{ TEXT("SendPartyNewReq5090"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint8 L = GenU8(R); return N->SendPartyNewReq5090(A, L); } },
		{ TEXT("SendPartyJoinReq5094"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendPartyJoinReq5094(A); } },
		{ TEXT("SendPartyNewAccept5092"), [](N_t* N, FRng& R) { const uint8 C = GenU8(R); const int32 A = GenI32(R); return N->SendPartyNewAccept5092(C, A); } },
		{ TEXT("SendPartyInviteAccept5096"), [](N_t* N, FRng& R) { const uint8 C = GenU8(R); const int32 A = GenI32(R); return N->SendPartyInviteAccept5096(C, A); } },
		{ TEXT("SendPartyLeave5098"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint8 M = GenU8(R); const FString S = GenStr(R); return N->SendPartyLeave5098(A, M, S); } },
		{ TEXT("SendPartyBanish5100"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const FString S = GenStr(R); return N->SendPartyBanish5100(A, B, S); } },
		// ── Конверт с секретом (SendSecured): логин-фаза ──
		{ TEXT("SendEnterWorldHandshake"), [](N_t* N, FRng& R) { return N->SendEnterWorldHandshake(); } },
		{ TEXT("SendCharListRequest"), [](N_t* N, FRng& R) { const uint16 A = (uint16)GenI32(R); return N->SendCharListRequest(A); } },
		{ TEXT("SendChannelSelect"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); return N->SendChannelSelect(A); } },
		{ TEXT("SendSelectChar"), [](N_t* N, FRng& R) { static const uint8 Sl[] = { 9, 10, 11, 12, 13, 0, 255 }; static const uint8 Md[] = { 0, 1, 2, 3, 255 }; const uint8 S = Sl[R.Range(7)], G = GenU8(R), C = GenU8(R), M = Md[R.Range(5)]; return N->SendSelectChar(S, G, C, M); } },
		// ── SendSecured: игровые ──
		{ TEXT("SendRareItemNotice6616"), [](N_t* N, FRng& R) { const FString S = GenStr(R); const int32 A = GenI32(R); return N->SendRareItemNotice6616(S, A); } },
		{ TEXT("SendSoulmateSearch5483"), [](N_t* N, FRng& R) { const uint8 P = GenU8(R), F = GenU8(R); return N->SendSoulmateSearch5483(P, F); } },
		{ TEXT("SendSoulmateNote6052"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const FString S = GenStr(R); return N->SendSoulmateNote6052(A, S); } },
		{ TEXT("SendSoulmateNoteConfirm6054"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendSoulmateNoteConfirm6054(A); } },
		{ TEXT("SendItemMove5000"), [](N_t* N, FRng& R) { const TArray<uint8> E = GenBytes(R); const int32 A = GenI32(R), B = GenI32(R); return N->SendItemMove5000(E, A, B); } },
		{ TEXT("SendSpendItemUse5028"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendSpendItemUse5028(A, B, C); } },
		{ TEXT("SendSoulJarRequest5419"), [](N_t* N, FRng& R) { uint8 B[11]; for (uint8& X : B) { X = GenU8(R); } return N->SendSoulJarRequest5419(B); } },
		{ TEXT("SendSoulJarResponse5422"), [](N_t* N, FRng& R) { const uint8 M = GenU8(R); const int32 A = GenI32(R); uint8 B[11]; for (uint8& X : B) { X = GenU8(R); } return N->SendSoulJarResponse5422(M, A, B); } },
		{ TEXT("SendSoulJarForwardExp5423"), [](N_t* N, FRng& R) { return N->SendSoulJarForwardExp5423(); } },
		{ TEXT("SendOpenPackageItem6096"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const uint8 C = GenU8(R), D = GenU8(R); const int32 E = GenI32(R); TArray<uint8> S = GenBytes(R); if (R.Range(4) != 0) { S.SetNum(15); for (uint8& X : S) { X = (uint8)R.Next(); } } const int32 Snd = R.Coin() ? N->SelectedCharId : A; return N->SendOpenPackageItem6096(Snd, B, C, D, E, S); } },
		{ TEXT("SendGachaItemUse5468"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint8 B = GenU8(R); const int32 C = GenI32(R); return N->SendGachaItemUse5468(A, B, C); } },
		{ TEXT("SendGmRatingRecalc6020"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); return N->SendGmRatingRecalc6020(A); } },
		{ TEXT("SendGmHide6005"), [](N_t* N, FRng& R) { const bool A = R.Coin(); return N->SendGmHide6005(A); } },
		{ TEXT("SendGmBlock6094"), [](N_t* N, FRng& R) { const int32 T = GenI32(R), H = GenI32(R); return N->SendGmBlock6094(T, H); } },   // ★ 12.10.2026, античит П6: тело {u16 свой, u16 цель, i32 часы}
		{ TEXT("SendGachaItemEffect5474"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); return N->SendGachaItemEffect5474(A); } },
		{ TEXT("SendSoulmateIntroduction6080"), [](N_t* N, FRng& R) { const FString S = GenStr(R); return N->SendSoulmateIntroduction6080(S); } },
		{ TEXT("SendPremiumBalanceQuery5466"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendPremiumBalanceQuery5466(A); } },
		{ TEXT("SendHairShopBuy5470"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint8 B = GenU8(R), C = GenU8(R); return N->SendHairShopBuy5470(A, B, C); } },
		{ TEXT("SendPremiumGachaRoll6577"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint8 B = GenU8(R); return N->SendPremiumGachaRoll6577(A, B); } },
		{ TEXT("SendPremiumGachaReward6581"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const uint8 C = GenU8(R); const int32 D = GenI32(R); return N->SendPremiumGachaReward6581(A, B, C, D); } },
		{ TEXT("SendPremiumPostListReq11004"), [](N_t* N, FRng& R) { return N->SendPremiumPostListReq11004(); } },
		{ TEXT("SendFunctionItemUse5450"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R), D = GenI32(R), E = GenI32(R); const uint8 F = GenU8(R); const int32 G = GenI32(R); const TArray<int32> T = GenInts(R); const FString S = GenStr(R); return N->SendFunctionItemUse5450(A, B, C, D, E, F, G, T, S); } },
		{ TEXT("SendPortalMove5477"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendPortalMove5477(A); } },
		{ TEXT("SendEventNpc6059"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendEventNpc6059(A, B, C); } },
		{ TEXT("SendNpcExchange6098"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendNpcExchange6098(A, B, C); } },
		{ TEXT("SendItemCraft6091"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendItemCraft6091(A, B, C); } },
		// ── Личная лавка, продавец (волна Л1 торговли, 29.09.2026): 6300 / 6302 / 6305 — конверт SendSecured ──
		{ TEXT("SendSelfStoreReady6300"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const bool P = R.Coin(); const FString S = GenName(R); const TArray<uint8> T = GenBytes(R); return N->SendSelfStoreReady6300(A, P, S, T); } },
		{ TEXT("SendSelfStoreOpen6302"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const TArray<int32> I = GenInts(R); const TArray<uint8> C = GenBytes(R); const TArray<int32> Sl = GenInts(R), Q = GenInts(R), Pr = GenInts(R); const FString S = GenStr(R); return N->SendSelfStoreOpen6302(A, I, C, Sl, Q, Pr, S); } },
		{ TEXT("SendSelfStoreClose6305"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); return N->SendSelfStoreClose6305(A, B); } },
		// ── Личная лавка, посетитель (волна Л2 торговли, 30.09.2026): 6307 / 6309 — конверт SendSecured ──
		{ TEXT("SendSelfStoreInfo6307"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendSelfStoreInfo6307(A, B, C); } },
		{ TEXT("SendSelfStoreBuy6309"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); const TArray<int32> I = GenInts(R), Q = GenInts(R), X = GenInts(R), P = GenInts(R); return N->SendSelfStoreBuy6309(A, B, C, I, Q, X, P); } },
		// ── Личная лавка, чат (волна Л4 торговли, 30.09.2026): 6313 — конверт SendSecured, тело 106 б ([ИСПРАВЛЕНИЕ] К-0) ──
		{ TEXT("SendSelfStoreChat6313"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const FString S = GenName(R); const FString T = GenStr(R); return N->SendSelfStoreChat6313(A, B, S, T); } },
		{ TEXT("SendClientTargetJump6146"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); int32 Six[6]; for (int32& V : Six) { V = GenI32(R); } return N->SendClientTargetJump6146(A, B, Six); } },
		{ TEXT("SendTitleApply6067"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); return N->SendTitleApply6067(A, B); } },
		{ TEXT("SendTitleDelete6069"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendTitleDelete6069(A); } },
		{ TEXT("SendPartyFindAddMember3410"), [](N_t* N, FRng& R) { const FString S = GenStr(R); return N->SendPartyFindAddMember3410(S); } },
		{ TEXT("SendPartyFindSubMember3412"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendPartyFindSubMember3412(A); } },
		{ TEXT("SendPartyFindGetMember3414"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const uint32 M1 = GenMask(R), M2 = GenMask(R); const int32 C = GenI32(R), D = GenI32(R), E = GenI32(R); return N->SendPartyFindGetMember3414(A, B, M1, M2, C, D, E); } },
		{ TEXT("SendPartyFindUpdateMember3416"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint32 M1 = GenMask(R), M2 = GenMask(R); return N->SendPartyFindUpdateMember3416(A, M1, M2); } },
		{ TEXT("SendPartyFindAddLeader3418"), [](N_t* N, FRng& R) { const FString S = GenStr(R); const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendPartyFindAddLeader3418(S, A, B, C); } },
		{ TEXT("SendPartyFindSubLeader3420"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendPartyFindSubLeader3420(A); } },
		{ TEXT("SendPartyFindGetLeader3422"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const uint32 M1 = GenMask(R), M2 = GenMask(R); const int32 C = GenI32(R), D = GenI32(R); return N->SendPartyFindGetLeader3422(A, B, M1, M2, C, D); } },
		{ TEXT("SendPartyFindUpdateLeader3424"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint32 M1 = GenMask(R), M2 = GenMask(R); const int32 C = GenI32(R), D = GenI32(R), E = GenI32(R); return N->SendPartyFindUpdateLeader3424(A, M1, M2, C, D, E); } },
		{ TEXT("SendCampWarReq6746"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendCampWarReq6746(A); } },
		{ TEXT("SendCampWarJoin6723"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendCampWarJoin6723(A, B, C); } },
		{ TEXT("SendCampWarSelfId6749"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); return N->SendCampWarSelfId6749(A, B); } },
		{ TEXT("SendQuestStepAccept5043"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); return N->SendQuestStepAccept5043(A, B); } },
		{ TEXT("SendQuestTurnIn5041"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R), D = GenI32(R); return N->SendQuestTurnIn5041(A, B, C, D); } },
		{ TEXT("SendQuestAbandon5006"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendQuestAbandon5006(A); } },
		{ TEXT("SendQuestSlotMove5000"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); return N->SendQuestSlotMove5000(A, B); } },
		{ TEXT("SendFunctionItemToggle5455"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendFunctionItemToggle5455(A); } },
		{ TEXT("SendItemInfoDisplay6129"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const uint8 C = GenU8(R); const int32 D = GenI32(R); return N->SendItemInfoDisplay6129(A, B, C, D); } },
		{ TEXT("SendItemDump5006"), [](N_t* N, FRng& R) { const TArray<uint8> E = GenBytes(R); const int32 A = GenI32(R); return N->SendItemDump5006(E, A); } },
		{ TEXT("SendItemDrop5002"), [](N_t* N, FRng& R) { const TArray<uint8> E = GenBytes(R); return N->SendItemDrop5002(E); } },
		{ TEXT("SendFishingToolUse6165"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint8 B = GenU8(R); const TArray<uint8> E1 = GenBytes(R); const TArray<uint8> E2 = GenBytes(R); return N->SendFishingToolUse6165(A, B, E1, E2); } },
		{ TEXT("SendSowelSocket5024"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); const int32 B = GenI32(R); const uint8 C = GenU8(R); const int32 D = GenI32(R), E = GenI32(R); const uint8 F = GenU8(R); const int32 G = GenI32(R); return N->SendSowelSocket5024(A, B, C, D, E, F, G); } },
		{ TEXT("SendCharItemBuy5020"), [](N_t* N, FRng& R) { const TArray<int32> A = GenInts(R); const TArray<int32> B = GenInts(R); return N->SendCharItemBuy5020(A, B); } },
		{ TEXT("SendCharItemSell5018"), [](N_t* N, FRng& R) { const TArray<uint8> E = GenBytes(R); return N->SendCharItemSell5018(E); } },
		{ TEXT("SendRepair6571"), [](N_t* N, FRng& R) { const TArray<uint8> A = GenBytes(R); const TArray<int32> B = GenInts(R); return N->SendRepair6571(A, B); } },
		{ TEXT("SendItemReset6506"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); const int32 B = GenI32(R); const TArray<int32> C = GenInts(R); return N->SendItemReset6506(A, B, C); } },
		// ★ 25.09.2026 (заточка, волна 7): отправитель 6500 высокой переплавки — ключ рецепта, цель, три ссылки.
		{ TEXT("SendUpperRefine6500"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const int32 B = GenI32(R); const uint8 C = GenU8(R); const TArray<uint8> D = GenBytes(R); const TArray<uint8> E = GenBytes(R); const TArray<uint8> F = GenBytes(R); return N->SendUpperRefine6500(A, B, C, D, E, F); } },
		// ★ 25.09.2026 (заточка, волна 2): отправитель 6504 — четыре пары {u8 контейнер, i16 слот}.
		{ TEXT("SendItemUpgrade6504"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); const int32 B = GenI32(R); const uint8 C = GenU8(R); const int32 D = GenI32(R); const uint8 E = GenU8(R); const int32 F = GenI32(R); const uint8 G = GenU8(R); const int32 H = GenI32(R); return N->SendItemUpgrade6504(A, B, C, D, E, F, G, H); } },
		{ TEXT("SendUpgradeExtraction6549"), [](N_t* N, FRng& R) { TArray<TArray<uint8>> S; const int32 K = (int32)((uint32)GenI32(R) % 10u); for (int32 i = 0; i < K; ++i) { TArray<uint8> Rec; Rec.SetNumZeroed(57); const int32 C = GenI32(R); FMemory::Memcpy(Rec.GetData(), &C, 4); Rec[4] = (uint8)GenI32(R); const int16 Sl = (int16)GenI32(R); FMemory::Memcpy(Rec.GetData() + 5, &Sl, 2); S.Add(MoveTemp(Rec)); } const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendUpgradeExtraction6549(S, A, B, C); } },
		{ TEXT("SendUpgradeBless6551"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R), D = GenI32(R), E = GenI32(R), F = GenI32(R); return N->SendUpgradeBless6551(A, B, C, D, E, F); } },
		{ TEXT("SendItemOptionTrans6553"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R), D = GenI32(R), E = GenI32(R), F = GenI32(R); return N->SendItemOptionTrans6553(A, B, C, D, E, F); } },
		{ TEXT("SendItemOptionExtraction5433"), [](N_t* N, FRng& R) { const TArray<uint8> A = GenBytes(R); const TArray<uint8> B = GenBytes(R); const TArray<uint8> C = GenBytes(R); return N->SendItemOptionExtraction5433(A, B, C); } },
		{ TEXT("SendConstellationSelect6586"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); return N->SendConstellationSelect6586(A); } },
		{ TEXT("SendClanCreate4104"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); const FString S = GenStr(R); return N->SendClanCreate4104(A, S); } },
		{ TEXT("SendClanDelete4106"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendClanDelete4106(A); } },
		{ TEXT("SendClanRosterRefresh4138"), [](N_t* N, FRng& R) { return N->SendClanRosterRefresh4138(); } },
		{ TEXT("SendWarShopBuy6162"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); return N->SendWarShopBuy6162(A, B); } },
		{ TEXT("SendSowelDetach5022"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); const int32 B = GenI32(R); const uint8 C = GenU8(R); return N->SendSowelDetach5022(A, B, C); } },
		{ TEXT("SendHolyItemUse5031"), [](N_t* N, FRng& R) { const uint8 A = GenU8(R); const int32 B = GenI32(R); const uint8 C = GenU8(R); const int32 D = GenI32(R); return N->SendHolyItemUse5031(A, B, C, D); } },
		{ TEXT("SendSkillLearn5253"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const uint8 B = GenU8(R), C = GenU8(R); return N->SendSkillLearn5253(A, B, C); } },
		{ TEXT("SendExchangeRegister9901"), [](N_t* N, FRng& R) { const TArray<TArray<uint8>> Rows = GenRows(R); return N->SendExchangeRegister9901(Rows); } },
		{ TEXT("SendExchangeSearch9903"), [](N_t* N, FRng& R) { uint8 C8[8]; for (uint8& V : C8) { V = GenU8(R); } const bool bNull = (R.Range(8) == 0); return N->SendExchangeSearch9903(bNull ? nullptr : C8); } },
		{ TEXT("SendExchangeBuy9905"), [](N_t* N, FRng& R) { const TArray<TArray<uint8>> Rows = GenRows(R); return N->SendExchangeBuy9905(Rows); } },
		{ TEXT("SendExchangeMyLots9907"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendExchangeMyLots9907(A); } },
		{ TEXT("SendExchangeDelete9909"), [](N_t* N, FRng& R) { const TArray<TArray<uint8>> Rows = GenRows(R); return N->SendExchangeDelete9909(Rows); } },
		{ TEXT("SendStoreHousePageReq4050"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendStoreHousePageReq4050(A); } },
		// ── SendSecured: сообщество, клан, зов друга, рулетка ──
		{ TEXT("SendFriendAnswer4153"), [](N_t* N, FRng& R) { const bool B = R.Coin(); const int32 A = GenI32(R); const FString S = GenName(R); return N->SendFriendAnswer4153(B, A, S); } },
		{ TEXT("SendFriendDelete4154"), [](N_t* N, FRng& R) { static const int32 Ids[] = { 0, 1, -1, 123456 }; const int32 A = R.Coin() ? Ids[R.Range(4)] : GenI32(R); return N->SendFriendDelete4154(A); } },
		{ TEXT("SendClanBaseInfoReq4122"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendClanBaseInfoReq4122(A); } },
		{ TEXT("SendClanMemberListReq4124"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendClanMemberListReq4124(A); } },
		{ TEXT("SendClanNoticeDelete6515"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendClanNoticeDelete6515(A, B, C); } },
		{ TEXT("SendClanLeaveReq4116"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); const FString S = GenStr(R); return N->SendClanLeaveReq4116(A, S); } },
		{ TEXT("SendClanNoticeReadReq6511"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R), C = GenI32(R); return N->SendClanNoticeReadReq6511(A, B, C); } },
		{ TEXT("SendClanNoticeWrite6513"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const FString S = GenStr(R); return N->SendClanNoticeWrite6513(A, B, S); } },
		{ TEXT("SendClanMemoWrite6518"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); const FString S = GenStr(R); return N->SendClanMemoWrite6518(A, B, S); } },
		{ TEXT("SendMateRecallAnswer5237"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); return N->SendMateRecallAnswer5237(A, B); } },
		{ TEXT("SendMateRecallPosition5427"), [](N_t* N, FRng& R) { const int32 A = GenI32(R), B = GenI32(R); return N->SendMateRecallPosition5427(A, B); } },
		{ TEXT("SendRouletteSpin6575"), [](N_t* N, FRng& R) { return N->SendRouletteSpin6575(); } },
		{ TEXT("SendRouletteClaim6579"), [](N_t* N, FRng& R) { const int32 A = GenI32(R); return N->SendRouletteClaim6579(A); } },
		// ── Хвост волны 3 п.10 (добавлены ДО их миграции; ставятся в конец списка, чтобы строки прежних
		//   100 отправителей не сдвинулись; своё зерно у каждого — по имени) ──
		// 6124: ячейки контейнера 2 подменяются через `StandContainer2CodesForStand` (у объекта стенда нет инвентаря).
		//   Длины 0…61 (эталон 60), часть ячеек = искомый код; MaxMatches — граничные (−1, 0, 6, 7, ≫6).
		{ TEXT("SendRaidMonsterCall6124"), [](N_t* N, FRng& R)
			{
				static const int32 Codes[] = { 30050, 30061, 0, -1, 20977 };
				static const int32 Lens[] = { 0, 1, 2, 5, 6, 7, 8, 13, 59, 60, 61 };
				static const int32 Max[] = { (int32)0x80000000, -1, 0, 1, 2, 3, 5, 6, 7, 8, 60, 1000 };
				const int32 Self = GenI32(R);
				const int32 Code = R.Coin() ? Codes[R.Range(UE_ARRAY_COUNT(Codes))] : GenI32(R);
				const int32 MaxM = R.Coin() ? Max[R.Range(UE_ARRAY_COUNT(Max))] : GenI32(R);
				const int32 Hit = R.Range(5);   // доля совпадений: 0 — нет, 4 — почти все
				N->StandContainer2CodesForStand.Reset();
				N->StandContainer2CodesForStand.SetNum(Lens[R.Range(UE_ARRAY_COUNT(Lens))]);
				for (int32& V : N->StandContainer2CodesForStand) { V = (R.Range(5) < Hit) ? Code : GenI32(R); }
				return N->SendRaidMonsterCall6124(Self, Code, MaxM);
			} },
		// 5008/5010: корзина 0…8 записей длиной 0…58 (порог сборщика 16, запись 57), код @0 — 0/−1/золото/обычный;
		//   ключ `asda.Warehouse.LegacyField2` — случайно (эталонная индексация поля +2 при дырках корзины).
		{ TEXT("SendItemKeeping5008"), [](N_t* N, FRng& R)
			{
				static const int32 Lens[] = { 0, 1, 15, 16, 17, 56, 57, 58 };
				static const int32 Codes[] = { 0, 1, -1, 20551, 12345, (int32)0x80000000 };
				TArray<TArray<uint8>> Basket;
				for (int32 i = 0, Cnt = R.Range(9); i < Cnt; ++i)
				{
					TArray<uint8>& E = Basket.AddDefaulted_GetRef();
					E.SetNum(Lens[R.Range(UE_ARRAY_COUNT(Lens))]);
					for (uint8& B : E) { B = (uint8)R.Next(); }
					if (E.Num() >= 4) { AsdaBytes::WrI32(E.GetData(), R.Coin() ? Codes[R.Range(UE_ARRAY_COUNT(Codes))] : GenI32(R)); }
				}
				SetCVar(TEXT("asda.Warehouse.LegacyField2"), R.Coin() ? 1 : 0);
				return N->SendItemKeeping5008(Basket);
			} },
		{ TEXT("SendItemRetake5010"), [](N_t* N, FRng& R)
			{
				static const int32 Lens[] = { 0, 1, 15, 16, 17, 56, 57, 58 };
				static const int32 Codes[] = { 0, 1, -1, 20551, 12345, (int32)0x80000000 };
				TArray<TArray<uint8>> Basket;
				for (int32 i = 0, Cnt = R.Range(9); i < Cnt; ++i)
				{
					TArray<uint8>& E = Basket.AddDefaulted_GetRef();
					E.SetNum(Lens[R.Range(UE_ARRAY_COUNT(Lens))]);
					for (uint8& B : E) { B = (uint8)R.Next(); }
					if (E.Num() >= 4) { AsdaBytes::WrI32(E.GetData(), R.Coin() ? Codes[R.Range(UE_ARRAY_COUNT(Codes))] : GenI32(R)); }
				}
				SetCVar(TEXT("asda.Warehouse.LegacyField2"), R.Coin() ? 1 : 0);
				return N->SendItemRetake5010(Basket);
			} },
		// 11006: запись 10 б; Count @8 — граница кламп-отказа (7/8) и случайные; изредка nullptr.
		{ TEXT("SendPremiumPostToInven11006"), [](N_t* N, FRng& R)
			{
				static const uint8 Cnt[] = { 0, 1, 2, 6, 7, 8, 9, 127, 128, 255 };
				uint8 Rec[10];
				for (uint8& B : Rec) { B = (uint8)R.Next(); }
				if (R.Coin()) { Rec[8] = Cnt[R.Range(UE_ARRAY_COUNT(Cnt))]; }
				const bool bNull = (R.Range(10) == 0);
				return N->SendPremiumPostToInven11006(bNull ? nullptr : Rec);
			} },
	};

	int64 TotalCaps = 0;
	int32 TotalCases = 0;
	for (int32 Pass = 0; Pass < Passes; ++Pass)
	{
		FString Out;
		Out.Reserve(16 * 1024 * 1024);
		TotalCaps = 0;
		TotalCases = 0;
		for (const FCase& C : All)
		{
			FRng R(Hash(C.Name));
			for (int32 k = 0; k < Cases; ++k)
			{
				SetupState(Net, R);
				Caps.Reset();
				const bool bRet = C.Call(Net, R);
				++TotalCases;
				Out += FString::Printf(TEXT("%s\t%d\tret=%d\tn=%d"), C.Name, k, bRet ? 1 : 0, Caps.Num());
				for (const FCap& Cp : Caps)
				{
					++TotalCaps;
					Out += FString::Printf(TEXT("\top=%u env=%c pad=%d len=%d "), (uint32)Cp.Opcode, Cp.Envelope,
						Cp.bPad ? 1 : 0, Cp.Body.Num());
					static const TCHAR* kHex = TEXT("0123456789ABCDEF");
					for (const uint8 Byte : Cp.Body) { Out.AppendChar(kHex[Byte >> 4]); Out.AppendChar(kHex[Byte & 0xF]); }
				}
				Out += TEXT("\n");
			}
		}
		const FString Path = (Pass == 0) ? OutPath : FString::Printf(TEXT("%s.pass%d"), *OutPath, Pass + 1);
		const bool bSaved = FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogTemp, Display, TEXT("PACKET_STAND_PASS %d: отправителей=%d случаев=%d отправок=%lld файл=%s (%s)"),
			Pass + 1, All.Num(), TotalCases, TotalCaps, *Path, bSaved ? TEXT("записан") : TEXT("ОШИБКА ЗАПИСИ"));
	}

	RestoreCVar(SavedJob);
	RestoreCVar(SavedPage);
	RestoreCVar(SavedSoul);
	RestoreCVar(SavedF2);
	Net->StandContainer2CodesForStand.Reset();
	Net->SendCaptureForStand = nullptr;
	Net->RemoveFromRoot();
	GI->RemoveFromRoot();
	Net->MarkAsGarbage();
	GI->MarkAsGarbage();
	UE_LOG(LogTemp, Display, TEXT("PACKET_STAND_DONE отправителей=%d случаев=%d"), All.Num(), TotalCases);
}

// Консольная команда стенда (имя уникально: `asda.Net.PacketStand`).
static FAutoConsoleCommand GAsdaPacketStandCmd(
	TEXT("asda.Net.PacketStand"),
	TEXT("Стенд побайтовой сверки тел исходящих пакетов: asda.Net.PacketStand <файл> [случаев=400] [проходов=1]"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FAsdaPacketStand::Run));

#endif // !UE_BUILD_SHIPPING
