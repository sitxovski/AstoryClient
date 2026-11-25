// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ СТЕНД ПОБАЙТОВОЙ СВЕРКИ РАЗБОРА ВХОДЯЩИХ ПАКЕТОВ (волна В0 аудита дублей №2, 11.10.2026).
//   План: `Reports/dedup_audit2/00_PLAN.md` (В0), `02_cpp_world_net_data.md` §4 (N6–N8). Отчёт: `wave0.md`.
//
// ★ ЗАЧЕМ. Механические замены разбора (N6 `AsdaBytes::*` вместо своих лямбд, N7 общий разбор IP, N8
//   `NetBodyToArray`/`NetNeedLen` вместо ручных копий тела и проверок длины) допустимы, только если штатный
//   `UAsdaNetSubsystem::HandlePacket` отдаёт ТО ЖЕ САМОЕ на тех же байтах. Стенд — пара к `asda.Net.PacketStand`
//   (исходящие): прогон на коде ДО правки (эталонный файл) → правка → прогон ПОСЛЕ → побайтовое сравнение файлов.
//
// ★ КАК УСТРОЕН. На каждый случай — СВОЙ свежий объект `UAsdaNetSubsystem` (внешний — собственный `UGameInstance`
//   стенда, без подсистем и мира): живая подсистема игры не трогается, состояние одного случая не протекает в
//   следующий. Тело подаётся в штатный `HandlePacket` (не копия разбора). В файл пишется всё, что разбор отдал:
//     D — каждый вызов любого из 208 делегатов подсистемы с аргументами (структуры — по полям);
//     S — ответные отправки (перехват `SendCaptureForStand`, как у PacketStand: в сеть ничего не уходит);
//     F — изменившиеся поля состояния (адрес game-сервера, карта, тайлы, «свой», виталы…);
//     L — строки лога игрового потока за время разбора (категория + текст; категории LogOutputDevice/LogStats/
//         LogWindows отсекаются — там обработанные ensure со стеком, ensure срабатывает один раз на место);
//     CRASH — исключение процессора внутри разбора (SEH, только Windows): случай помечается, стенд идёт дальше.
//
// ★ ВХОДЫ. Опкоды — все метки `case` из `HandlePacket` (верхний уровень и вложенные ≥ 1000, на 11.10.2026 — 305).
//   На каждый — Cases случаев от xorshift64 с зерном FNV-1a(опкод): длины тела из пула границ (0…1500, в т. ч.
//   57/82/686), заполнение — случайное / нули / 0xFF / малые числа / ASCII-текст «IP с пробелами» (для N7: поле
//   IP в любом смещении получает адрес с хвостом пробелов) / смесь; состояние «своего» (номер, известен ли,
//   charId, фаза сессии, канал, уровень) — тоже из генератора. Новый опкод в конце списка не сдвигает прежние случаи.
//
// Консоль: `asda.Net.RecvStand <файл> [случаев=100] [проходов=1] [опкод]`. Проход 2+ пишет `<файл>.passN`
//   (проверка детерминизма). Необязательный опкод — прогон одного опкода (отладка).
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include <type_traits>

// Друг `UAsdaNetSubsystem` (объявлен в заголовке): стенду нужны `HandlePacket`, закрытые поля и делегаты.
struct FAsdaRecvStand
{
	// Точка входа консольной команды.
	static void Run(const TArray<FString>& Args);
	// Подать пакет в штатный разбор под защитой SEH; false — исключение процессора внутри разбора.
	static bool SafeFeed(UAsdaNetSubsystem* N, const Asda::ParsedPacket* P);
	// Вызов разбора без защиты (отдельная функция: в функции с __try не должно быть объектов с деструкторами).
	static void Feed(UAsdaNetSubsystem* N, const Asda::ParsedPacket* P) { N->HandlePacket(*P); }
};

#if PLATFORM_WINDOWS
// Windows: исключение процессора внутри разбора ловится, случай помечается CRASH.
bool FAsdaRecvStand::SafeFeed(UAsdaNetSubsystem* N, const Asda::ParsedPacket* P)
{
	__try { Feed(N, P); return true; }
	__except (1 /* EXCEPTION_EXECUTE_HANDLER */) { return false; }
}
#else
// Прочие платформы: без защиты.
bool FAsdaRecvStand::SafeFeed(UAsdaNetSubsystem* N, const Asda::ParsedPacket* P) { Feed(N, P); return true; }
#endif

namespace AsdaRecvStandLocal
{
	// Детерминированный генератор (xorshift64) — тот же, что у PacketStand.
	struct FRng
	{
		uint64 S;
		explicit FRng(uint64 Seed) : S(Seed ? Seed : 0x9E3779B97F4A7C15ull) {}
		uint32 Next() { S ^= S << 13; S ^= S >> 7; S ^= S << 17; return (uint32)(S >> 32); }
		int32 Range(int32 N) { return N > 0 ? (int32)(Next() % (uint32)N) : 0; }
		bool Coin() { return (Next() & 1u) != 0; }
	};

	// FNV-1a от номера опкода — зерно генератора случаев этого опкода.
	static uint64 Hash(uint32 Op)
	{
		uint64 H = 1469598103934665603ull;
		for (int32 i = 0; i < 4; ++i) { H ^= (uint64)((Op >> (8 * i)) & 0xFFu); H *= 1099511628211ull; }
		return H;
	}

	// Опкоды под стендом — метки `case` из `UAsdaNetSubsystem::HandlePacket` (≥ 1000). Новые — только в конец.
	static const int32 kOps[] = {
		1004, 1006, 1007, 1011, 1013, 1015, 1017, 1019, 1021, 1023, 1024, 1025, 1026, 1027, 2247, 2255,
		3113, 3411, 3413, 3415, 3417, 3419, 3421, 3423, 3425, 4003, 4005, 4007, 4009, 4011, 4013, 4015,
		4017, 4019, 4021, 4023, 4025, 4027, 4029, 4036, 4038, 4045, 4046, 4048, 4049, 4051, 4052, 4053,
		4055, 4056, 4058, 4059, 4060, 4062, 4064, 4065, 4067, 4101, 4102, 4103, 4105, 4107, 4109, 4110,
		4117, 4121, 4123, 4125, 4126, 4131, 4132, 4133, 4135, 4137, 4151, 4152, 4155, 4157, 4158, 4160,
		4205, 4206, 4301, 4302, 4999, 5001, 5003, 5005, 5007, 5009, 5011, 5013, 5015, 5017, 5019, 5021,
		5023, 5025, 5029, 5032, 5034, 5036, 5042, 5044, 5047, 5052, 5055, 5059, 5071, 5085, 5087, 5089,
		5091, 5093, 5095, 5097, 5099, 5101, 5102, 5103, 5104, 5106, 5107, 5108, 5109, 5110, 5111, 5201,
		5203, 5205, 5207, 5220, 5226, 5228, 5229, 5230, 5232, 5233, 5236, 5237, 5238, 5251, 5252, 5254,
		5257, 5268, 5270, 5271, 5272, 5273, 5283, 5285, 5287, 5289, 5292, 5294, 5295, 5306, 5307, 5417,
		5418, 5426, 5430, 5434, 5451, 5452, 5453, 5454, 5456, 5457, 5459, 5462, 5464, 5465, 5467, 5469,
		5471, 5472, 5473, 5479, 5481, 5482, 5484, 5485, 6001, 6004, 6006, 6013, 6015, 6039, 6041, 6049,
		6051, 6056, 6057, 6060, 6065, 6066, 6068, 6070, 6071, 6078, 6080, 6083, 6085, 6086, 6093, 6097,
		6099, 6101, 6103, 6104, 6105, 6106, 6107, 6109, 6111, 6113, 6119, 6120, 6122, 6123, 6125, 6126,
		6130, 6131, 6133, 6135, 6136, 6149, 6151, 6152, 6153, 6154, 6155, 6156, 6157, 6159, 6160, 6161,
		6163, 6164, 6166, 6168, 6170, 6172, 6173, 6174, 6177, 6179, 6181, 6183, 6201, 6203, 6301, 6303,
		6304, 6306, 6308, 6310, 6311, 6312, 6314, 6315, 6501, 6503, 6505, 6507, 6512, 6514, 6519, 6523,
		6539, 6542, 6554, 6561, 6570, 6572, 6576, 6578, 6580, 6582, 6585, 6587, 6597, 6599, 6601, 6603,
		6605, 6713, 6719, 6722, 6743, 6745, 7006, 8008, 9902, 9904, 9906, 9908, 9910, 9913, 9914, 11005,
		11007,
		4202, 4204,   // ★ 17.10.2026 (Ф4): ответы PvP-атаки
		8011, 8012, 8013,   // ★ 30.10.2026: навыки мобов (UAsdaMobSkillSubsystem)
		6701, 6703,         // ★ 30.10.2026: выбор армии (UAsdaFactionSelectSubsystem)
		6552,               // ★ 31.10.2026: перенос улучшения камнем (UAsdaUpgradeStoneSubsystem)
		6550,               // ★ 31.10.2026: разделение улучшения, окно 50500 (UAsdaUpgradeSplitSubsystem)
		5073, 5075, 5077, 5078,   // ★ 01.11.2026: P101_CashGift — ответы подарка (UAsdaCashGiftSubsystem)
		4207,               // ★ 04.11.2026: отказ портала в бою (нотис 108000)
	};

	// Тело пакета: длина из пула границ, заполнение по режиму.
	static TArray<uint8> GenBody(FRng& R)
	{
		static const int32 Lens[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 25,
			26, 27, 28, 29, 30, 32, 34, 36, 40, 44, 48, 50, 56, 57, 58, 60, 64, 66, 72, 80, 82, 90, 98, 100, 114, 128,
			150, 160, 200, 256, 300, 400, 512, 600, 686, 700, 1000, 1500 };
		TArray<uint8> B;
		B.SetNumUninitialized(Lens[R.Range(UE_ARRAY_COUNT(Lens))]);
		const int32 Mode = R.Range(6);
		static const char* kIp = "192.168.10.70   10.0.0.1  198.51.100.177 ";   // IP с хвостами пробелов (N7)
		const int32 IpLen = (int32)FCStringAnsi::Strlen(kIp);
		const int32 IpShift = R.Range(IpLen);
		for (int32 i = 0; i < B.Num(); ++i)
		{
			switch (Mode)
			{
			case 0:  B[i] = (uint8)R.Next(); break;                      // случайные байты
			case 1:  B[i] = 0; break;                                    // нули
			case 2:  B[i] = 0xFF; break;                                 // все единицы
			case 3:  B[i] = (uint8)R.Range(4); break;                    // малые числа 0…3 (счётчики, флаги)
			case 4:  B[i] = (uint8)kIp[(i + IpShift) % IpLen]; break;    // ASCII «IP с пробелами»
			default: B[i] = (R.Range(4) == 0) ? (uint8)R.Next() : (uint8)R.Range(16); break;   // смесь
			}
		}
		return B;
	}

	// Ловушка лога игрового потока на время одного разбора (синхронно: устройство разрешено на любом потоке).
	struct FLogTrap : public FOutputDevice
	{
		FString* Sink = nullptr;   // куда дописывать строки (nullptr — ловушка выключена)

		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verb, const FName& Category) override
		{
			if (!Sink || !V || !IsInGameThread()) { return; }
			if ((Verb & ELogVerbosity::VerbosityMask) > ELogVerbosity::Log) { return; }
			static const FName kOutDev(TEXT("LogOutputDevice"));
			static const FName kStats(TEXT("LogStats"));
			static const FName kWindows(TEXT("LogWindows"));
			if (Category == kOutDev || Category == kStats || Category == kWindows) { return; }
			FString Line(V);
			Line.ReplaceInline(TEXT("\r"), TEXT(" "));
			Line.ReplaceInline(TEXT("\n"), TEXT(" | "));
			*Sink += FString::Printf(TEXT("\n\tL %s: %s"), *Category.ToString(), *Line);
		}
	};

	// ── Печать аргументов делегатов ──
	// Строка — в кавычках.
	static void Fmt(FString& S, const FString& V) { S += TEXT("\""); S += V; S += TEXT("\""); }
	// Имя.
	static void Fmt(FString& S, const FName& V) { S += V.ToString(); }
	// Текст.
	static void Fmt(FString& S, const FText& V) { Fmt(S, V.ToString()); }
	// Точка тайла.
	static void Fmt(FString& S, const FIntPoint& V) { S += FString::Printf(TEXT("(%d,%d)"), V.X, V.Y); }
	// Цвет.
	static void Fmt(FString& S, const FColor& V) { S += FString::Printf(TEXT("#%02X%02X%02X%02X"), V.R, V.G, V.B, V.A); }
	// Вектор.
	static void Fmt(FString& S, const FVector& V) { S += FString::Printf(TEXT("(%.6g,%.6g,%.6g)"), V.X, V.Y, V.Z); }
	// Точка экрана/плоскости.
	static void Fmt(FString& S, const FVector2D& V) { S += FString::Printf(TEXT("(%.6g,%.6g)"), V.X, V.Y); }
	// Байты — длина и hex целиком.
	static void Fmt(FString& S, const TArray<uint8>& V)
	{
		static const TCHAR* kHex = TEXT("0123456789ABCDEF");
		S += FString::Printf(TEXT("[%d]"), V.Num());
		for (const uint8 B : V) { S.AppendChar(kHex[B >> 4]); S.AppendChar(kHex[B & 0xF]); }
	}
	// Признак USTRUCT (есть `StaticStruct()`) — печать по отражению.
	template<typename T, typename = void> struct THasStaticStruct : std::false_type {};
	template<typename T> struct THasStaticStruct<T, std::void_t<decltype(T::StaticStruct())>> : std::true_type {};
	// Общий шаблон (числа, перечисления, указатели, USTRUCT) — определён ниже.
	template<typename T> static void Fmt(FString& S, const T& V);
	// Массив — поэлементно (определён ниже, после печати структур: их перегрузки должны быть видны в теле).
	template<typename T> static void Fmt(FString& S, const TArray<T>& V);
	// Виталы (время мира `LastServerHpS` не печатается: недетерминировано).
	static void Fmt(FString& S, const UAsdaNetSubsystem::FAsdaVitals& V)
	{
		S += FString::Printf(TEXT("{hp=%d/%d mp=%d/%d hp2=%d mp2=%d src="), V.HpCur, V.HpMax, V.MpCur, V.MpMax, V.HpSecond, V.MpSecond);
		Fmt(S, V.LastHpSrc); S += TEXT("}");
	}
	// Строка чата.
	static void Fmt(FString& S, const UAsdaNetSubsystem::FAsdaChatLineIn& V)
	{
		S += FString::Printf(TEXT("{cat=%d op=%d "), V.Category, V.SrcOpcode); Fmt(S, V.Text); S += TEXT(" fb="); Fmt(S, V.Fallback);
		S += TEXT(" bg="); Fmt(S, V.RowBg); S += FString::Printf(TEXT(" rowbg=%d snd="), V.bRowBg ? 1 : 0); Fmt(S, V.SenderName);
		S += FString::Printf(TEXT(" uidx=%d auth=%d rank=%d bv=%d bt="), V.SenderUserIdx, V.bSenderAuthored ? 1 : 0, V.RankLevel, V.BalloonVariant);
		Fmt(S, V.BalloonText); S += TEXT(" bc="); Fmt(S, V.BalloonColor);
		S += FString::Printf(TEXT(" bf=%d cid=%d}"), V.BalloonFontFlag, V.SenderCharId);
	}
	// Попадание навыка.
	static void Fmt(FString& S, const UAsdaNetSubsystem::FAsdaSkillHitRec& V)
	{
		S += FString::Printf(TEXT("{tk=%d ti=%d sk=%d dmg=%d hp=%d af=%d ci=%d st=%d hf=%d f4102=%d ph=%d}"), V.TargetKind, V.TargetIdx,
			V.SkillId, V.Damage, V.HpAfter, V.ApplyFlag, V.CasterIdx, V.Stack, V.HitFlags, V.bFrom4102 ? 1 : 0, V.Phase);
	}
	// Запись каста навыка.
	static void Fmt(FString& S, const UAsdaNetSubsystem::FAsdaSkillCastRec& V)
	{
		S += FString::Printf(TEXT("{tk=%d ti=%d dmg=%d hf=%d hp=%d st=%d pop=%d}"), V.TargetKind, V.TargetIdx, V.Damage, V.HitFlags, V.HpAfter, V.StateId, V.PopupValue);
	}
	// Предмет на земле.
	static void Fmt(FString& S, const UAsdaNetSubsystem::FAsdaLootDrop& V)
	{
		S += FString::Printf(TEXT("{code=%d tile=(%d,%d) n=%d rid=%d}"), V.ItemCode, V.Tile.X, V.Tile.Y, V.Count, V.DropperRid);
	}
	// Разобранный 4015.
	static void Fmt(FString& S, const FAsdaCombatHit& V)
	{
		S += FString::Printf(TEXT("{t=%d a=%d k=%u cur=(%d,%d) dst=(%d,%d) sp=%.6g dmg=%d hp=%d hv=%u ht=%u mr=%.6g ar=%.6g mob=%d rsk=%d rdm=%d}"),
			V.TargetRid, V.AttackerUserIdx, (uint32)V.Kind, V.CurTile.X, V.CurTile.Y, V.DstTile.X, V.DstTile.Y, V.Speed, V.Damage, V.NewHp,
			(uint32)V.HitVariant, (uint32)V.HitType, V.MoveAnimRate, V.AtkAnimRate, V.MobType, V.ReflectSkillId, V.ReflectDamage);
	}
	// Хвост 4015 (моб: флаги, HP, тайл, баффы).
	static void Fmt(FString& S, const FAsdaCombatHitAux& V)
	{
		S += FString::Printf(TEXT("{mob=%d rid=%d fl=%08X hp=%d tile=(%d,%d) buffs="), V.MobType, V.Rid, V.Flags, V.Hp, V.Tile.X, V.Tile.Y);
		Fmt(S, V.BuffIds); S += TEXT(" lv="); Fmt(S, V.BuffLevels); S += TEXT("}");
	}
	// ★ Ф4: ответ PvP-атаки 4202/4204.
	static void Fmt(FString& S, const UAsdaNetSubsystem::FAsdaPvpAttackPkt& V)
	{
		S += FString::Printf(TEXT("{op=%d code=%d a=%d t=%d q=%d"), V.SrcOpcode, V.Code, V.AttackerUserIdx, V.TargetUserIdx, V.Dmg.Num());
		for (const int32 D : V.Dmg) { S += FString::Printf(TEXT(" %d"), D); }
		for (int32 i = 0; i < 4; ++i) { S += FString::Printf(TEXT(" [sk=%d v=%d]"), V.SkillIds[i], V.SkillDmg[i]); }
		S += TEXT("}");
	}
	// Побочный урон 4029/4027 (★ 18.10.2026, ревизия Ф4 мелочь 9: комментарий возвращён к своему объявлению).
	static void Fmt(FString& S, const FAsdaCombatSideDamage& V)
	{
		S += FString::Printf(TEXT("{op=%d a=%d t=%d"), V.Opcode, V.AttackerUserIdx, V.TargetRid);
		for (const FAsdaCombatSideHit& H : V.Steps)
		{
			S += FString::Printf(TEXT(" [raw=%d sk=%d att=%d v=%d ok=%d]"), H.Raw, H.SkillId, H.bOnAttacker ? 1 : 0, H.Value, H.bValid ? 1 : 0);
		}
		S += TEXT("}");
	}
	template<typename T> static void Fmt(FString& S, const TArray<T>& V)
	{
		S += FString::Printf(TEXT("[%d]{"), V.Num());
		for (int32 i = 0; i < V.Num(); ++i) { if (i) { S += TEXT(";"); } Fmt(S, V[i]); }
		S += TEXT("}");
	}
	template<typename T> static void Fmt(FString& S, const T& V)
	{
		if constexpr (std::is_same_v<T, bool>) { S += V ? TEXT("1") : TEXT("0"); }
		else if constexpr (std::is_enum_v<T>) { S += FString::Printf(TEXT("%lld"), (long long)V); }
		else if constexpr (std::is_integral_v<T>) { S += FString::Printf(TEXT("%lld"), (long long)V); }
		else if constexpr (std::is_floating_point_v<T>) { S += FString::Printf(TEXT("%.9g"), (double)V); }
		else if constexpr (std::is_pointer_v<T>) { S += V ? TEXT("ptr") : TEXT("null"); }
		else if constexpr (THasStaticStruct<T>::value)
		{
			FString Txt;
			T::StaticStruct()->ExportText(Txt, &V, nullptr, nullptr, PPF_None, nullptr);
			S += Txt;
		}
		else { static_assert(sizeof(T) == 0, "RecvStand: нет печати для типа аргумента делегата"); }
	}

	// Подписка-шпион на делегат: каждый вызов дописывает строку `D имя(аргументы)` в приёмник.
	template<typename... A, typename P>
	static void Spy(TMulticastDelegate<void(A...), P>& D, const TCHAR* Name, FString* Sink)
	{
		D.AddLambda([Name, Sink](A... Args)
		{
			FString S = FString::Printf(TEXT("\n\tD %s("), Name);
			int32 Idx = 0;
			(void)Idx;   // у делегата без аргументов счётчик не используется
			((S += (Idx++ ? TEXT(",") : TEXT("")), Fmt(S, Args)), ...);
			S += TEXT(")");
			*Sink += S;
		});
	}
}

void FAsdaRecvStand::Run(const TArray<FString>& Args)
{
	using namespace AsdaRecvStandLocal;
	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Error, TEXT("asda.Net.RecvStand <файл> [случаев=100] [проходов=1] [опкод]"));
		return;
	}
	const FString OutPath = Args[0];
	const int32 Cases  = (Args.Num() > 1) ? FMath::Max(1, FCString::Atoi(*Args[1])) : 100;
	const int32 Passes = (Args.Num() > 2) ? FMath::Max(1, FCString::Atoi(*Args[2])) : 1;
	const int32 OnlyOp = (Args.Num() > 3) ? FCString::Atoi(*Args[3]) : 0;

	// Собственный внешний объект (Within = GameInstance ⇒ внешний обязан быть UGameInstance), без подсистем и мира.
	UGameInstance* GI = NewObject<UGameInstance>(GetTransientPackage());
	GI->AddToRoot();

	FLogTrap Trap;
	GLog->AddOutputDevice(&Trap);

	int32 TotalCases = 0, TotalCrash = 0;
	TArray<int32> CrashOps;
	for (int32 Pass = 0; Pass < Passes; ++Pass)
	{
		FString Out;
		Out.Reserve(64 * 1024 * 1024);
		TotalCases = 0; TotalCrash = 0; CrashOps.Reset();
		for (const int32 Op : kOps)
		{
			if (OnlyOp != 0 && Op != OnlyOp) { continue; }
			UE_LOG(LogTemp, Display, TEXT("RECV_STAND_OP %d"), Op);   // вне ловушки: последняя строка перед падением процесса
			FRng R(Hash((uint32)Op));
			for (int32 k = 0; k < Cases; ++k)
			{
				// Свежий объект подсистемы на случай: состояние не протекает между случаями.
				UAsdaNetSubsystem* N = NewObject<UAsdaNetSubsystem>(GI);
				N->AddToRoot();
				FString Sink;
				N->SendCaptureForStand = [&Sink](uint16 SOp, const uint8* P, int32 Len, bool bPad, TCHAR Env)
				{
					static const TCHAR* kHex = TEXT("0123456789ABCDEF");
					Sink += FString::Printf(TEXT("\n\tS op=%u env=%c pad=%d len=%d "), (uint32)SOp, Env, bPad ? 1 : 0, Len);
					for (int32 i = 0; P && i < Len; ++i) { Sink.AppendChar(kHex[P[i] >> 4]); Sink.AppendChar(kHex[P[i] & 0xF]); }
				};
				// Шпионы на все делегаты подсистемы (список — объявления `F… On…;` заголовка на 11.10.2026).
#define ASDA_RS_D(Name) Spy(N->Name, TEXT(#Name), &Sink);

		ASDA_RS_D(OnChannelListReady) ASDA_RS_D(OnChannelChanged) ASDA_RS_D(OnSoulmateSharedCharsUpdated) ASDA_RS_D(OnCharSelectRefused) ASDA_RS_D(OnCharCreateResult) ASDA_RS_D(OnCharDeleteResult)
		ASDA_RS_D(OnEnterWorld) ASDA_RS_D(OnGmHideResult) ASDA_RS_D(OnPartyFindAck) ASDA_RS_D(OnPartyFindPage) ASDA_RS_D(OnSelfEquipGrade) ASDA_RS_D(OnSelfSpawn)
		ASDA_RS_D(OnSelfSkillPoints) ASDA_RS_D(OnSelfExp) ASDA_RS_D(OnSelfLevelUp) ASDA_RS_D(OnRemoteLevel) ASDA_RS_D(OnServerNotice) ASDA_RS_D(OnChatLine)
		ASDA_RS_D(OnMobChatBubble) ASDA_RS_D(OnItemAcquired) ASDA_RS_D(OnWorldReady) ASDA_RS_D(OnCharDeath) ASDA_RS_D(OnSelfWarp) ASDA_RS_D(OnSelfVitals)
		ASDA_RS_D(OnVitalsChanged) ASDA_RS_D(OnCharStateUpdate) ASDA_RS_D(OnVitalsFx) ASDA_RS_D(OnVitalsStateFx) ASDA_RS_D(OnSelfStatsChanged) ASDA_RS_D(OnSelfWeight) ASDA_RS_D(OnPvpMode)
		ASDA_RS_D(OnPvpModeRefused) ASDA_RS_D(OnDuelPacket) ASDA_RS_D(OnWave2DPacket) ASDA_RS_D(OnCampWarSquadPacket)ASDA_RS_D(OnCharAction) ASDA_RS_D(OnMiningResult) ASDA_RS_D(OnCharRevived)
		ASDA_RS_D(OnResurrectOffer) ASDA_RS_D(OnActionBlocked) ASDA_RS_D(OnMapEntryFx) ASDA_RS_D(OnMapEntry4056) ASDA_RS_D(OnEnterWorldFail) ASDA_RS_D(OnCharTitle)
		ASDA_RS_D(OnCharRank) ASDA_RS_D(OnTitleGranted) ASDA_RS_D(OnTitleFound) ASDA_RS_D(OnTitleDeleted) ASDA_RS_D(OnTitleMaskOwned) ASDA_RS_D(OnTitleMaskFound)
		ASDA_RS_D(OnTitleStatBatch) ASDA_RS_D(OnClanInfo) ASDA_RS_D(OnClanFlag) ASDA_RS_D(OnClanReset) ASDA_RS_D(OnClanSnapshot) ASDA_RS_D(OnClanCreateResult)
		ASDA_RS_D(OnClanDisbandResult) ASDA_RS_D(OnPeerSoulStateByCharId) ASDA_RS_D(OnPeerSoulStateByUserIdx) ASDA_RS_D(OnCharPartUpdate) ASDA_RS_D(OnEnvBatch) ASDA_RS_D(OnEntityDespawn)
		ASDA_RS_D(OnQuestStateSync) ASDA_RS_D(OnCombatHit) ASDA_RS_D(OnCombatHitAux) ASDA_RS_D(OnCombatAnim) ASDA_RS_D(OnCombatSideDamage) ASDA_RS_D(OnSkillHit)
		ASDA_RS_D(OnTargetHp) ASDA_RS_D(OnCombatRefused) ASDA_RS_D(OnCombatStanceReset) ASDA_RS_D(OnPlayerMenuResult) ASDA_RS_D(OnPartyRequest) ASDA_RS_D(OnPartyResult)
		ASDA_RS_D(OnSoulApplePacket) ASDA_RS_D(OnSoulmatePartnerLeft) ASDA_RS_D(OnLootSpawn) ASDA_RS_D(OnLootRemove) ASDA_RS_D(OnLootPickupResult) ASDA_RS_D(OnLootPickHint)
		ASDA_RS_D(OnHotbarPage) ASDA_RS_D(OnInventoryBatch) ASDA_RS_D(OnInventoryBatchEnd) ASDA_RS_D(OnEquipFull) ASDA_RS_D(OnItemMoveResult) ASDA_RS_D(OnWarehouseResult)
		ASDA_RS_D(OnItemSlotUpdate) ASDA_RS_D(OnItemSlotRemoved) ASDA_RS_D(OnItemUseResult) ASDA_RS_D(OnItemDumpResult) ASDA_RS_D(OnPackageOpenResult) ASDA_RS_D(OnGachaResult)
		ASDA_RS_D(OnOtherPlayerBoost) ASDA_RS_D(OnSelfIntroChanged) ASDA_RS_D(OnPremiumShopResult) ASDA_RS_D(OnPremiumBalance) ASDA_RS_D(OnHairShopResult) ASDA_RS_D(OnHairChanged)
		ASDA_RS_D(OnPremiumGachaResult) ASDA_RS_D(OnPremiumGachaApply) ASDA_RS_D(OnPremiumPostList) ASDA_RS_D(OnPremiumPostApply) ASDA_RS_D(OnFunctionItemResult) ASDA_RS_D(OnFunctionItemExpired)
		ASDA_RS_D(OnBagExpand) ASDA_RS_D(OnItemRepaired) ASDA_RS_D(OnItemStatusNotice) ASDA_RS_D(OnItemShowOff) ASDA_RS_D(OnFishingToolResult) ASDA_RS_D(OnFishSkill6164)
		ASDA_RS_D(OnFishStart6168) ASDA_RS_D(OnFishGauge6170) ASDA_RS_D(OnFishResult6172) ASDA_RS_D(OnFishPeer6173) ASDA_RS_D(OnFishBookReward6183) ASDA_RS_D(OnFishBookPage6174)
		ASDA_RS_D(OnFishBookListEnd6175) ASDA_RS_D(OnFishBookRegist6177) ASDA_RS_D(OnFishBookDelete6179) ASDA_RS_D(OnFishBookUpdate6181) ASDA_RS_D(OnSlotDurability6570) ASDA_RS_D(OnSowelSocketResult)
		ASDA_RS_D(OnSowelDetachResult) ASDA_RS_D(OnHolyItemResult) ASDA_RS_D(OnCityOwnerInfo) ASDA_RS_D(OnShopBuyResult) ASDA_RS_D(OnShopSellResult) ASDA_RS_D(OnRepairResult)
		ASDA_RS_D(OnItemOptionTransResult) ASDA_RS_D(OnUpgradeBlessResult) ASDA_RS_D(OnUpgradeExtractionResult) ASDA_RS_D(OnExtractShopResult) ASDA_RS_D(OnConstellationResult) ASDA_RS_D(OnItemResetResult) ASDA_RS_D(OnItemUpgradeResult) ASDA_RS_D(OnHighRefineResult)
		ASDA_RS_D(OnDisassembleResult) ASDA_RS_D(OnCraftPacket) ASDA_RS_D(OnSelfStorePacket) ASDA_RS_D(OnHuntReportResult) ASDA_RS_D(OnDonationResult) ASDA_RS_D(OnWarShopBuyResult)
		ASDA_RS_D(OnSkillUpgradeResult) ASDA_RS_D(OnSkillListFull) ASDA_RS_D(OnSkillListUpdate) ASDA_RS_D(OnSkillListCompact) ASDA_RS_D(OnSkillTreeReset) ASDA_RS_D(OnSkillSlotsReset)
		ASDA_RS_D(OnSkillsRealigned) ASDA_RS_D(OnSkillCooldowns) ASDA_RS_D(OnItemCooldowns) ASDA_RS_D(OnSelfFaction) ASDA_RS_D(OnWingState) ASDA_RS_D(OnSoulmateState)
		ASDA_RS_D(OnMountState) ASDA_RS_D(OnEffectMsg) ASDA_RS_D(OnEventBonus) ASDA_RS_D(OnEventSet) ASDA_RS_D(OnSoulSlotState) ASDA_RS_D(OnSoulGuardIdx)
		ASDA_RS_D(OnSkillCast) ASDA_RS_D(OnSkillCastIdle) ASDA_RS_D(OnSkillCastState) ASDA_RS_D(OnSkillUseResult) ASDA_RS_D(OnSkillUseCastIdle) ASDA_RS_D(OnSkillCastRecs)
		ASDA_RS_D(OnSoulSkillCastRecs) ASDA_RS_D(OnQuestStepResult) ASDA_RS_D(OnQuestProgress) ASDA_RS_D(OnQuestTurnInResult) ASDA_RS_D(OnPeerPet) ASDA_RS_D(OnPetPacket)
		ASDA_RS_D(OnPeerFaction) ASDA_RS_D(OnSoulmatePeer) ASDA_RS_D(OnSoulmateBond) ASDA_RS_D(OnSoulmateBreak) ASDA_RS_D(OnSoulmateCard) ASDA_RS_D(OnSoulmateBreakRefused)
		ASDA_RS_D(OnSoulmateBreakRequest) ASDA_RS_D(OnSoulmateSearch) ASDA_RS_D(OnSoulmateSearchShow) ASDA_RS_D(OnGameDisconnected) ASDA_RS_D(OnLoginKick) ASDA_RS_D(OnEntityMove)
		ASDA_RS_D(OnEntityPosSync) ASDA_RS_D(OnEntitySpeed) ASDA_RS_D(OnLoginResult) ASDA_RS_D(OnCharListReady) ASDA_RS_D(OnCharListFailed) ASDA_RS_D(OnExchangePacket)
		ASDA_RS_D(OnFriendInvite) ASDA_RS_D(OnFriendDeleteResult) ASDA_RS_D(OnFriendList) ASDA_RS_D(OnFriendListRefresh) ASDA_RS_D(OnFriendEntryUpdate) ASDA_RS_D(OnClanMemberList)
		ASDA_RS_D(OnClanBaseInfo) ASDA_RS_D(OnClanBaseInfoPush) ASDA_RS_D(OnClanIncreaseResult) ASDA_RS_D(OnClanInviteRecv) ASDA_RS_D(OnClanMemberListEnd) ASDA_RS_D(OnClanLeaveResult)
		ASDA_RS_D(OnClanNotice) ASDA_RS_D(OnWhoResult) ASDA_RS_D(OnWhoNotice) ASDA_RS_D(OnSoulSkillState) ASDA_RS_D(OnSoulSkillApply) ASDA_RS_D(OnSoulSkillCooldowns)
		ASDA_RS_D(OnSoulSkillTray) ASDA_RS_D(OnMateRecall) ASDA_RS_D(OnRouletteResult) ASDA_RS_D(OnRoulettePrize)
		ASDA_RS_D(OnPvpAttackStart) ASDA_RS_D(OnPvpHit)   // ★ 17.10.2026 (Ф4)
#undef ASDA_RS_D
				// Состояние «своего» — из генератора.
				static const uint16 Nums[] = { 0, 1, 17, 255, 256, 0x7FFF, 0xFFFF };
				static const int32 Ids[] = { 0, 1, -1, 424242, 0x000125AC };
				static const EAsdaNetState States[] = { EAsdaNetState::Idle, EAsdaNetState::CharSelect, EAsdaNetState::EnteringWorld, EAsdaNetState::InWorld };
				N->SelfUserNum = Nums[R.Range(UE_ARRAY_COUNT(Nums))];
				N->bSelfUserNumKnown = R.Coin();
				N->SelectedCharId = Ids[R.Range(UE_ARRAY_COUNT(Ids))];
				N->State = States[R.Range(UE_ARRAY_COUNT(States))];
				N->CurrentChannel = R.Range(4);
				N->SelfLevel = (uint8)R.Range(100);

				// Поля состояния, изменения которых пишутся в файл (строка F).
				auto Snap = [](const UAsdaNetSubsystem* M)
				{
					FString F = FString::Printf(TEXT("gs=%s:%d map=%d tile=(%d,%d) ptile=(%d,%d) xfer=%d state=%d uidx=%u clu=%08X self=%u/%d cid=%d ch=%d lv=%u job=%d cls=%d dead=%d"),
						*M->GameServerIP, M->GameServerPort, M->EnterMapId, M->SelfTileX, M->SelfTileZ, M->PortalTileX, M->PortalTileZ,
						M->bTransferInProgress ? 1 : 0, (int32)M->State, M->UserIndex, M->CharListUserNo, (uint32)M->SelfUserNum,
						M->bSelfUserNumKnown ? 1 : 0, M->SelectedCharId, M->CurrentChannel, (uint32)M->SelfLevel, M->SelfJob, M->SelfClassId,
						M->bSelfDead ? 1 : 0);
					TArray<int32> Keys;
					M->VitalsByUserIdx.GetKeys(Keys);
					Keys.Sort();
					for (const int32 K : Keys)
					{
						const UAsdaNetSubsystem::FAsdaVitals& V = M->VitalsByUserIdx[K];
						F += FString::Printf(TEXT(" v%d=%d/%d,%d/%d,%d,%d"), K, V.HpCur, V.HpMax, V.MpCur, V.MpMax, V.HpSecond, V.MpSecond);
					}
					return F;
				};
				const FString Before = Snap(N);

				const TArray<uint8> Body = GenBody(R);
				Asda::ParsedPacket Pkt;
				Pkt.Opcode = (uint16)Op;
				Pkt.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
				Pkt.Size = (uint16)(Body.Num() + 10);

				Out += FString::Printf(TEXT("%d\t%d\tlen=%d\tself=%u/%d cid=%d st=%d"), Op, k, Body.Num(), (uint32)N->SelfUserNum,
					N->bSelfUserNumKnown ? 1 : 0, N->SelectedCharId, (int32)N->State);
				Trap.Sink = &Sink;
				const bool bOk = SafeFeed(N, &Pkt);
				Trap.Sink = nullptr;
				++TotalCases;
				if (!bOk) { ++TotalCrash; CrashOps.AddUnique(Op); Sink += TEXT("\n\tCRASH"); }
				const FString After = Snap(N);
				if (After != Before) { Sink += TEXT("\n\tF "); Sink += After; }
				Out += Sink;
				Out += TEXT("\n");

				N->SendCaptureForStand = nullptr;
				N->RemoveFromRoot();
				N->MarkAsGarbage();
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);   // свежие объекты случаев этого опкода — в сборку
		}
		const FString Path = (Pass == 0) ? OutPath : FString::Printf(TEXT("%s.pass%d"), *OutPath, Pass + 1);
		const bool bSaved = FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FString CrashList;
		for (const int32 C : CrashOps) { CrashList += FString::Printf(TEXT(" %d"), C); }
		UE_LOG(LogTemp, Display, TEXT("RECV_STAND_PASS %d: опкодов=%d случаев=%d исключений=%d (опкоды:%s) файл=%s (%s)"),
			Pass + 1, (OnlyOp != 0) ? 1 : (int32)UE_ARRAY_COUNT(kOps), TotalCases, TotalCrash, *CrashList, *Path,
			bSaved ? TEXT("записан") : TEXT("ОШИБКА ЗАПИСИ"));
	}

	GLog->RemoveOutputDevice(&Trap);
	GI->RemoveFromRoot();
	GI->MarkAsGarbage();
	UE_LOG(LogTemp, Display, TEXT("RECV_STAND_DONE случаев=%d исключений=%d"), TotalCases, TotalCrash);
}

// Консольная команда стенда (имя уникально: `asda.Net.RecvStand`).
static FAutoConsoleCommand GAsdaRecvStandCmd(
	TEXT("asda.Net.RecvStand"),
	TEXT("Стенд побайтовой сверки разбора входящих пакетов: asda.Net.RecvStand <файл> [случаев=100] [проходов=1] [опкод]"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FAsdaRecvStand::Run));

#endif // !UE_BUILD_SHIPPING
