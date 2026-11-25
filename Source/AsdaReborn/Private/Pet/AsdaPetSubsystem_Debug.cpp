// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaPetSubsystem.cpp, дословный перенос): дампы, разбор тела, перехват отправок, тестовые питомцы, RunSelfTest, дев-команды.
//   Общее для частей — в AsdaPetSubsystem_Private.h; шапка и реверс — в AsdaPetSubsystem.cpp.
#include "Pet/AsdaPetSubsystem.h"
#include "Misc/ScopeExit.h"   // ★ R4-c (15.11.2026, ревизия): возврат подписчиков самотеста на любом выходе
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "Login/AsdaLoginData.h"     // Decode/EncodeCp1251 — единственный кодек cp1251 порта
#include "Net/NetOpcodes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "World/AsdaWorldSubsystem.h"         // LocalPlayerActor — свой персонаж
#include "HAL/IConsoleManager.h"
#include "Engine/DataTable.h"
#include "UI/AsdaNotice.h"            // ★ волна 8: общая точка нотиса AsdaPetUi::NoticeMsgBox               // ★ волна 8: обход `petexptable` в AsdaPetUi::ExpRow
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaPetSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPetSubsystem*.cpp

// ═════════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════
FString UAsdaPetSubsystem::DumpState() const
{
	FString S = TEXT("\n═══════════ ПИТОМЦЫ: состояние ═══════════\n");
	S += FString::Printf(TEXT("  сеть: %s, свой charId=%d\n"),
		bNetBound ? TEXT("привязана") : TEXT("НЕ привязана"), SelfCharId());
	S += FString::Printf(TEXT("  ячеек доступно=%d, занято=%d, в списке записей=%d\n"),
		PetPossessionCount, GetOccupiedCount(), Slots.Num());
	S += FString::Printf(TEXT("  призван=%d, выбран=%d, запрос 6100 в полёте=%s\n"),
		ActiveUniqueId, SelectedUniqueId, CallLatch.IsArmed() ? TEXT("ДА") : TEXT("нет"));
	S += FString::Printf(TEXT("  6120 (мир): тип=%d имя=[%s]\n"), SelfPeerPetType, *SelfPeerPetName);
	S += FString::Printf(TEXT("  коды последних ответов: 6101=%d 6103=%d 6109=%d 6111=%d 6113=%d 6119=%d 6122=%d 6149=%d\n"),
		LastCallResult, LastIncubateResult, LastFeedResult, LastReleaseResult,
		LastUnionResult, LastRenameResult, LastSlotUpResult, LastLimitBreakResult);
	// ★ 27.09 (СРЕДН-1): uid, удалённые 6111 после последнего 6112 и ещё не «погашенные» 6113.
	S += FString::Printf(TEXT("  удалено 6111 во время юниона (ждут 6113): %d\n"), UnionReleasedUids.Num());
	S += FString::Printf(TEXT("  отложенных отправок в очереди: %d\n"), Pending.Num());

	S += TEXT("  ── список ──\n");
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FAsdaPetSlot& P = Slots[i];
		const FAsdaPetInfoRow* Inf = GetPetInfo(P.PetIndex);
		S += FString::Printf(
			TEXT("   %2d. uid=%-8d порода=%-3d [%s] ур.%d/%d опыт=%d/%d сытость=%d%s\n"),
			i, P.UniqueId, P.PetIndex, *P.Name, P.Level, P.MaxLevel,
			P.Exp, GetExpThreshold(P.PetIndex, P.Level), P.Fullness,
			P.bIsActive ? TEXT("  ★ПРИЗВАН") : TEXT(""));
		S += FString::Printf(TEXT("        бонусы: %d=%d, %d=%d, %d=%d%s\n"),
			P.Abilities[0].Type, P.Abilities[0].Value,
			P.Abilities[1].Type, P.Abilities[1].Value,
			P.Abilities[2].Type, P.Abilities[2].Value,
			Inf ? TEXT("") : TEXT("   ⚠ породы нет в серверной таблице"));
	}

	S += TEXT("  ── счётчики пакетов ──\n");
	TArray<int32> Ops;
	RxByOpcode.GetKeys(Ops);
	Ops.Sort();
	for (int32 Op : Ops) { S += FString::Printf(TEXT("   принято %d: %d\n"), Op, RxByOpcode[Op]); }
	TArray<int32> TxOps;
	TxByOpcode.GetKeys(TxOps);
	TxOps.Sort();
	for (int32 Op : TxOps) { S += FString::Printf(TEXT("   отправлено %d: %d\n"), Op, TxByOpcode[Op]); }
	return S;
}

FString UAsdaPetSubsystem::DumpTables() const
{
	FString S = TEXT("\n═══════════ ПИТОМЦЫ: таблицы ═══════════\n");
	S += FString::Printf(TEXT("  породы (сервер PetInfo_Table)      : %d\n"), PetInfo.Num());
	S += FString::Printf(TEXT("  уровни (сервер PetExp_Table)       : %d\n"), PetExp.Num());
	S += FString::Printf(TEXT("  шансы инкубации                    : %d\n"), IncubateRate.Num());
	S += FString::Printf(TEXT("  рецепты эволюции                   : %d\n"), UnionRecipe.Num());
	S += FString::Printf(TEXT("  пороги лимит-брейка                : %d\n"), LimitBreak.Num());
	S += FString::Printf(TEXT("  карты развития (клиентский PEV)    : %d\n"), EvoStage.Num());

	// ★ Сверка с КЛИЕНТСКИМИ таблицами (закон `client-server-table-desync`: авторитет — сервер).
	const UDataTable* CliPet = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/ItemDB/DT_pettable.DT_pettable"));
	const UDataTable* CliExp = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/ItemDB/DT_petexptable.DT_petexptable"));
	const int32 CliPetN = CliPet ? CliPet->GetRowMap().Num() : -1;
	const int32 CliExpN = CliExp ? CliExp->GetRowMap().Num() : -1;
	S += TEXT("  ── сверка клиент ↔ сервер ──\n");
	S += FString::Printf(TEXT("   породы: клиент %d, сервер %d → %s\n"), CliPetN, PetInfo.Num(),
		(CliPetN == PetInfo.Num()) ? TEXT("совпадают")
		: TEXT("РАСХОЖДЕНИЕ: лишние породы клиента сервер отвергает (PetIndex < PetInfoCount), в UI не показывать"));
	S += FString::Printf(TEXT("   уровни: клиент %d, сервер %d → %s\n"), CliExpN, PetExp.Num(),
		(CliExpN == PetExp.Num()) ? TEXT("совпадают")
		: TEXT("РАСХОЖДЕНИЕ: брать серверную (строка 11 запирающая)"));

	S += TEXT("  ── пороги лимит-брейка (дефект ПД4: загрузчик сервера читает 4 колонки из 5) ──\n");
	S += FString::Printf(TEXT("   режим ключа asda.Pet.LimitBreakFixTable = %d (%s)\n"),
		CVarPetLimitBreakFixTable.GetValueOnGameThread(),
		CVarPetLimitBreakFixTable.GetValueOnGameThread() ? TEXT("как в файле") : TEXT("как в памяти сервера, 1:1"));
	TArray<int32> Lv;
	LimitBreak.GetKeys(Lv);
	Lv.Sort();
	for (int32 L : Lv)
	{
		const FAsdaPetLimitBreakRow& R = LimitBreak[L];
		int32 T1 = 0, T2 = 0, T3 = 0;
		GetLimitBreakThresholds(L, T1, T2, T3);
		S += FString::Printf(
			TEXT("   MaxLevel=%2d (файл Level=%2d) файл: %6d %6d %6d %6d | память сервера: %6d %6d %6d %6d | действуют: %6d %6d %6d\n"),
			L, R.Level, R.Thr1, R.Thr2, R.Thr3, R.Thr4, R.ServerD0, R.ServerD1, R.ServerD2, R.ServerD3, T1, T2, T3);
	}

	S += TEXT("  ── рецепты эволюции (направление ИСПРАВЛЕНО: c1 = результат) ──\n");
	TArray<int32> Ui;
	UnionRecipe.GetKeys(Ui);
	Ui.Sort();
	for (int32 U : Ui)
	{
		const FAsdaPetUnionRecipeRow& R = UnionRecipe[U];
		S += FString::Printf(TEXT("   рецепт %d: РЕЗУЛЬТАТ порода %d ур.%d ← %d(ур.%d) + %d(ур.%d) + %d(ур.%d)\n"),
			R.UnionIndex, R.ResultPetIndex, R.ResultPetLevel,
			R.ReqPetIndex[0], R.ReqLevel[0], R.ReqPetIndex[1], R.ReqLevel[1], R.ReqPetIndex[2], R.ReqLevel[2]);
	}

	S += TEXT("  ── карты развития (стадия гасится по PetType == -1) ──\n");
	TArray<int32> Ev;
	EvoStage.GetKeys(Ev);
	Ev.Sort();
	for (int32 E : Ev)
	{
		const FAsdaPetEvoStageRow& R = EvoStage[E];
		S += FString::Printf(TEXT("   карта %d:"), R.EvoIndex);
		for (int32 k = 0; k < 7; ++k)
		{
			S += (R.PetType[k] < 0)
				? FString(TEXT("  [—]"))
				: FString::Printf(TEXT("  [порода %d ур.%d рец.%d]"), R.PetType[k], R.Level[k], R.StuffIdx[k]);
		}
		S += TEXT("\n");
	}
	return S;
}

FString UAsdaPetSubsystem::DecodeBody(int32 Opcode, const TArray<uint8>& B) const
{
	FString S = FString::Printf(TEXT("  опкод %d, тело %d б: "), Opcode, B.Num());
	auto I32 = [&B](int32 O) { return (B.Num() >= O + 4) ? AsdaBytes::RdI32(&B[O]) : 0; };
	auto I16 = [&B](int32 O) { return (B.Num() >= O + 2) ? AsdaBytes::RdI16(&B[O]) : 0; };
	auto U16 = [&B](int32 O) { return (B.Num() >= O + 2) ? (int32)AsdaBytes::RdU16(&B[O]) : 0; };
	auto U8  = [&B](int32 O) { return (B.Num() >= O + 1) ? (int32)B[O] : 0; };

	switch (Opcode)
	{
		case OP_PET_CALL_REQ:
			S += FString::Printf(TEXT("C→S призыв: charId=%d pet=%d"), I32(0), I32(4)); break;
		case OP_PET_INCUBATE_REQ:
			S += FString::Printf(TEXT("C→S инкубатор: charId=%d катализатор=(%d,%d) яйцо=(%d,%d) бустер=(%d,%d)"),
				I32(0), U8(4), I16(5), U8(7), I16(8), U8(10), I16(11)); break;
		case OP_PET_FEED_REQ:
			S += FString::Printf(TEXT("C→S кормление: charId=%d pet=%d предмет=(%d,%d)"),
				I32(0), I32(4), U8(8), I16(9)); break;
		case OP_PET_RELEASE_REQ:
			S += FString::Printf(TEXT("C→S выброс: charId=%d pet=%d"), I32(0), I32(4)); break;
		case OP_PET_UNION_REQ:
			S += FString::Printf(TEXT("C→S эволюция: charId=%d рецепт=%d материалы=%d/%d/%d"),
				I32(0), I16(4), I32(6), I32(10), I32(14)); break;
		case OP_PET_NAME_CHANGE_REQ:
			S += FString::Printf(TEXT("C→S переименование: charId=%d pet=%d имя=[%s]"),
				I32(0), I32(4), (B.Num() >= 24) ? *FAsdaLoginData::DecodeCp1251(B.GetData() + 8, 16) : TEXT("?")); break;
		case OP_PET_SLOT_UP_REQ:
			S += FString::Printf(TEXT("C→S +ячейки: charId=%d предмет=(%d,%d)"), I32(0), U8(4), I16(5)); break;
		case OP_PET_LIMITBREAK_REQ:
			S += FString::Printf(TEXT("C→S лимит-брейк: pet=%d зелье=(%d,%d) защита=%d (★ charId в теле нет)"),
				I32(0), U8(6), I16(4), I16(7)); break;

		case OP_PET_CALL_RESULT:
			S += FString::Printf(TEXT("S→C призыв: charId=%d код=%d pet=%d"), I32(0), U8(4), I32(5)); break;
		case OP_PET_INCUBATE_RESULT:
			S += FString::Printf(TEXT("S→C инкубатор: charId=%d код=%d предметы=(%d,%d)(%d,%d)(%d,%d) вес=%d"),
				I32(0), U8(4), U8(5), I16(6), U8(8), I16(9), U8(11), I16(12), I32(14)); break;
		case OP_PET_CONDITION_UPDATE:
			S += FString::Printf(TEXT("S→C сытость: userNum=%d charId=%d pet=%d сытость=%d бонусы=%d:%d %d:%d %d:%d"),
				U16(0), I32(2), I32(6), U8(10),
				U16(11), I32(13), U16(17), I32(19), U16(23), I32(25)); break;
		case OP_PET_EXP_UPDATE:
			S += FString::Printf(TEXT("S→C опыт: userNum=%d charId=%d pet=%d опыт=%d повышение=%d уровень=%d"),
				U16(0), I32(2), I32(6), U16(10), U8(12), U16(13)); break;
		case OP_PET_LIST:
		{
			const bool bFits = (B.Num() >= 5) && (((B.Num() - 5) % 46) == 0);
			S += FString::Printf(TEXT("S→C список: charId=%d флаг=%d записей=%d (формула 5+46·N: %s)"),
				I32(0), U8(4), bFits ? (B.Num() - 5) / 46 : -1, bFits ? TEXT("сходится") : TEXT("НЕ СХОДИТСЯ"));
			if (bFits)
			{
				for (int32 i = 0; i < (B.Num() - 5) / 46; ++i)
				{
					const int32 O = 5 + i * 46;
					S += FString::Printf(TEXT("\n      запись %d: uid=%d порода=%d имя=[%s] сытость=%d ур.%d/%d опыт=%d призван=%d"),
						i, I32(O), I16(O + 4), *FAsdaLoginData::DecodeCp1251(B.GetData() + O + 6, 16),
						U8(O + 22), U8(O + 23), U8(O + 24), U16(O + 25), U8(O + 45));
				}
			}
			break;
		}
		case OP_PET_HUNGER_RECALL:
			S += FString::Printf(TEXT("S→C голод: charId=%d pet=%d"), I32(0), I32(4)); break;
		case OP_PET_FEED_RESULT:
			// ★ Волна 6: печатаем и запись предмета Item(57) с +8 — её применяет окно 43000.
			S += FString::Printf(TEXT("S→C кормление: charId=%d pet=%d код=%d сытость=%d вес=%d корм=(код %d, сумка %d, ячейка %d, штук %d) бонусы=%d:%d %d:%d %d:%d"),
				I32(0), I32(4), U8(65), U8(66), I32(67),
				I32(8), U8(12), I16(13), I32(17),
				U16(71), I32(73), U16(77), I32(79), U16(83), I32(85)); break;
		case OP_PET_RELEASE_RESULT:
			S += FString::Printf(TEXT("S→C выброс: charId=%d pet=%d код=%d"), I32(0), I32(4), U8(8)); break;
		case OP_PET_UNION_RESULT:
			// ★ +50/+52/+54 — МЛАДШИЕ 16 БИТ uid съеденных материалов (GameServer.c:36090 vs :36100),
			//   а не породы: прежняя подпись вводила в заблуждение (ревизия 2026-09-21).
			S += FString::Printf(TEXT("S→C эволюция: charId=%d код=%d новый pet=%d порода=%d ур.%d; съедены uid(16 бит) %d/%d/%d"),
				I32(0), U8(4), I32(5), I16(9), U8(28), U16(50), U16(52), U16(54)); break;
		case OP_PET_NAME_CHANGE_RESULT:
			S += FString::Printf(TEXT("S→C переименование: charId=%d pet=%d имя=[%s] код=%d"),
				I32(0), I32(4), (B.Num() >= 25) ? *FAsdaLoginData::DecodeCp1251(B.GetData() + 8, 16) : TEXT("?"), U8(24)); break;
		case OP_PEER_ACTIVE_PET:
			S += FString::Printf(TEXT("S→C питомец в мире: charId=%d тип=%d имя=[%s]"),
				I32(0), U16(4) == 0xFFFF ? -1 : U16(4),
				(B.Num() >= 22) ? *FAsdaLoginData::DecodeCp1251(B.GetData() + 6, 16) : TEXT("?")); break;
		case OP_PET_SLOT_UP_RESULT:
			S += FString::Printf(TEXT("S→C +ячейки: charId=%d код=%d ячеек=%d предмет=(%d,%d) вес=%d"),
				I32(0), U8(4), U8(5), U8(6), I16(7), I32(9)); break;
		case OP_PET_SLOT_COUNT:
			S += FString::Printf(TEXT("S→C ячеек при входе: charId=%d ячеек=%d"), I32(0), U8(4)); break;
		case OP_PET_LIMITBREAK_RESULT:
			// ★ байт 0 — petUid, НЕ charId (GameServer.c:36840 ← 36774; npc_mob_pet.c:7731).
			S += FString::Printf(TEXT("S→C лимит-брейк: pet=%d порода=%d ур.%d предел=%d опыт=%d исход=%d зелье=(%d,%d) вес=%d"),
				I32(0), I16(4), U8(23), U8(24), U16(25), U8(48), U8(47), I16(45), I32(49)); break;
		default:
			S += TEXT("не пет-опкод"); break;
	}
	return S + TEXT("\n");
}

// ★★★ ВОЛНА 3, КРИТ-1: перехват исходящих пет-пакетов на живом прогоне.
//   Обоснование целиком — у объявления в `.h`. Здесь только механика: та же, что у
//   `RunSelfTest` (подмена `SelectedCharId` + `SendCaptureForStand`), но живущая между кадрами.
bool UAsdaPetSubsystem::BeginSendCapture(
	TFunction<void(int32, const TArray<uint8>&, bool, TCHAR)> Sink, int32 TestCharId)
{
	UAsdaNetSubsystem* N = Net();
	if (!N)
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] перехват исходящих: сетевой подсистемы нет"));
		return false;
	}
	// ★ ВОЛНА 8 (пункт 9 ревизии): отказ теперь ВИДЕН вызывающему — см. объявление.
	if (bSendCaptureArmed)
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] перехват исходящих УЖЕ взведён — повторный Begin игнорирую"));
		return false;
	}
	// ★ Волна 15 (МЕЛК-2 ревизии волны 14): перехватчик сети ОДИН на клиент. Если его уже держит другой
	//   отладочный инструмент (узкий перехват 6500 самотеста окна 78200, он снимается сам через 5 с), не
	//   перетираем его — иначе тот по таймауту снял бы наш, а наш «весь трафик» остался бы узким.
	if (N->SendCaptureForStand)
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] перехват исходящих НЕ взведён: перехватчик сети занят другим инструментом (узкий опкод %u)"),
			(uint32)N->SendCaptureOnlyOpcode);
		return false;
	}
	bSendCaptureArmed = true;
	SavedCharIdForCapture = N->SelectedCharId;
	if (TestCharId > 0) { N->SelectedCharId = TestCharId; }
	// Весь трафик (`OnlyOpcode = 0`), поколение запоминаем — `EndSendCapture` снимет только свой перехватчик.
	SendCaptureGen = N->ArmSendCapture([Sink](uint16 Op, const uint8* Payload, int32 Len, bool bPad, TCHAR Env)
	{
		TArray<uint8> Body;
		if (Payload && Len > 0) { Body.Append(Payload, Len); }
		if (Sink) { Sink((int32)Op, Body, bPad, Env); }
	}, /*OnlyOpcode*/ 0);
	UE_LOG(LogAsdaPet, Warning,
		TEXT("[питомцы] ★ ПЕРЕХВАТ ИСХОДЯЩИХ ВЗВЕДЁН: в сеть НЕ УХОДИТ НИЧЕГО, charId %d → %d. ")
		TEXT("Это инструмент прогона; снять — EndSendCapture."),
		SavedCharIdForCapture, N->SelectedCharId);
	return true;
}

void UAsdaPetSubsystem::EndSendCapture()
{
	if (!bSendCaptureArmed) { return; }
	bSendCaptureArmed = false;
	if (UAsdaNetSubsystem* N = Net())
	{
		// ★ Волна 15 (МЕЛК-2): снимаем только СВОЙ перехватчик (по поколению); charId возвращаем в любом случае —
		//   его подменяли мы.
		const bool bMine = N->DisarmSendCapture(SendCaptureGen);
		N->SelectedCharId = SavedCharIdForCapture;
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] перехват исходящих снят%s, charId возвращён в %d"),
			bMine ? TEXT("") : TEXT(" (перехватчик уже сменён другим — чужой не трогаю)"), N->SelectedCharId);
	}
	SendCaptureGen = 0;
	SavedCharIdForCapture = 0;
}

void UAsdaPetSubsystem::InjectTestPets(int32 Count, int32 FirstBreed)
{
	// ★ Закон `diagnostic-must-use-real-path`: подстановка идёт НЕ записью в поля, а синтезом
	//   настоящего тела 6106 и прогоном его через боевой приёмник. Иначе проверка подтвердила бы
	//   работоспособность кода, которого не выполняли.
	Count = FMath::Clamp(Count, 0, kMaxSlots);
	TArray<uint8> Body;
	Body.SetNumZeroed(5 + 46 * Count);
	AsdaBytes::WrI32(Body.GetData() + 0, SelfCharId());
	Body[4] = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 O = 5 + i * 46;
		FAsdaPetSlot S;
		S.UniqueId = 1000 + i;
		// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026: ПОРОДА 31 БОЛЬШЕ НЕ НЕДОСТИЖИМА.
		//   Было `i % PetInfo.Num()` при потолке `kMaxSlots` = 30 ⇒ стендом нельзя было получить
		//   породы 30 и 31 ВООБЩЕ, а у 31 своя эталонная дистанция (`npc_mob_pet.c:5628-5633`),
		//   то есть именно её и надо было проверять. Теперь первая порода задаётся аргументом:
		//   `asda.Pet.Test 10 22` даёт породы 22…31.
		S.PetIndex = (FirstBreed + i) % FMath::Max(1, PetInfo.Num());
		S.Name = FString::Printf(TEXT("Тест%d"), i);
		S.Fullness = 100 - i;
		S.Level = 1 + (i % 9);
		S.MaxLevel = FMath::Max(S.Level, 3);
		// ★★★ ВОЛНА 8 (пункт 8 сквозной ревизии): ОПЫТ ПО СЕРВЕРНОМУ ИНВАРИАНТУ,
		//   А НЕ НЕЗАВИСИМО ОТ УРОВНЯ.
		//   БЫЛО: `S.Exp = 10 * i` при `S.Level = 1 + (i % 9)` — два независимых числа. Такой
		//   питомец НЕВОЗМОЖЕН: сервер при левел-апе КЛАМПИТ опыт к порогу и поднимает
		//   уровень (`if (exp >= v26) { exp = v26; … }`, GameServer.c:35306-35314), то есть на уровне L
		//   опыт ВСЕГДА лежит в [порог(L−1), порог(L)]. Стенд же порождал питомца i=1:
		//   Level 2, Exp 10 при порогах 333/733 — и полоса опыта честно показывала −80.75 %.
		//   Этим стендом пользовались волны 2/4/5/6, и все их числа сняты на таких данных.
		//   ★★★ САМУ ФОРМУЛУ ПРОЦЕНТА НЕ ТРОГАЕМ: она ВЕРНА и дословна эталону
		//   (AsdaStory_game.c:411572, без клампа). Чинится ИСТОЧНИК ДАННЫХ, а не счёт.
		//   Пороги берём у готовой общей точки `GetExpThreshold` (та самая формула сервера
		//   `PetExpTable[Level + 1].Exp * ExpRateX10 / 10`, GameServer.c:35304) — второй копии
		//   счёта не заводим (закон `check-existing-before-creating`).
		//   Раскладка внутри диапазона шагает 0/25/50/75/100 %, чтобы полосы в окнах
		//   были разной длины и проверялись оба края.
		//   ★ Таблиц нет (голый стенд без DT) ⇒ оба порога нулевые ⇒ Exp = 0. Это тоже
		//   законное состояние (свежий питомец), а не отрицательный процент.
		{
			const int32 ExpLo = GetExpThreshold(S.PetIndex, S.Level - 1);   // низ текущего уровня
			const int32 ExpHi = GetExpThreshold(S.PetIndex, S.Level);       // потолок текущего уровня
			const int32 Lo = FMath::Max(0, FMath::Min(ExpLo, ExpHi));
			const int32 Hi = FMath::Max(Lo, FMath::Max(ExpLo, ExpHi));
			const int32 Step = i % 5;                                       // 0…4 ⇒ 0/25/50/75/100 %
			// Запись поля опыта в теле 6106 — 16-битная (`WrI16(P + 21, In.Exp)`), как и у сервера
			//   (`*(_WORD*)(… + 25)`, GameServer.c:35305). Зажимаем, чтобы стенд не породил
			//   числа, которое не влезает в провод — то же «невозможные данные».
			S.Exp = FMath::Clamp(Lo + ((Hi - Lo) * Step) / 4, 0, 32767);
		}
		for (int32 j = 0; j < 3; ++j) { S.Abilities[j].Type = j + 1; S.Abilities[j].Value = 10 * (j + 1) + i; }
		AsdaBytes::WrI32(Body.GetData() + O, S.UniqueId);
		WritePetRecord(Body, O + 4, S);
		Body[O + 45] = (i == 0) ? 1 : 0;   // первый — призванный
	}
	OnPetPacket(OP_PET_LIST, Body);
	// ★ ВОЛНА 8: в лог идёт ПРОВЕРКА ИНВАРИАНТА по УЖЕ ПРИНЯТЫМ записям,
	//   а не по тому, что мы собирались записать: тело прошло боевой приёмник, и если
	//   тот что-то исказил, стенд обязан это показать (закон `diagnostic-must-use-real-path`).
	int32 Impossible = 0;
	for (const FAsdaPetSlot& S : Slots)
	{
		const int32 Lo = GetExpThreshold(S.PetIndex, S.Level - 1);
		const int32 Hi = GetExpThreshold(S.PetIndex, S.Level);
		if (Hi > Lo && (S.Exp < Lo || S.Exp > Hi)) { ++Impossible; }
	}
	UE_LOG(LogAsdaPet, Log,
		TEXT("[питомцы] тестовая подстановка: %d записей через настоящий приёмник 6106; ")
		TEXT("записей вне серверного инварианта опыта [порог(L−1)…порог(L)]: %d"),
		Count, Impossible);
	if (Impossible > 0)
	{
		UE_LOG(LogAsdaPet, Error,
			TEXT("[питомцы] СТЕНД ПОРОДИЛ %d НЕВОЗМОЖНЫХ ЗАПИСЕЙ: полоса опыта покажет ")
			TEXT("процент вне 0…100, и любое число, снятое на этих данных, недостоверно"), Impossible);
	}
}

FString UAsdaPetSubsystem::RunSelfTest()
{
	// Снимок состояния — самопроверка обязана вернуть всё как было.
	const TArray<FAsdaPetSlot> SaveSlots = Slots;
	const int32 SavePoss = PetPossessionCount;
	const int32 SaveActive = ActiveUniqueId;
	const int32 SaveSelected = SelectedUniqueId;
	// ★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (среднее 7): засов в снимке ЦЕЛИКОМ (признак, момент взвода, эхо, признак гонки) —
	//   иначе после прогона засов мог вернуться без таймера ⇒ ВЕЧНЫЙ засов (закон `no-state-latch-before-shown`).
	//   ★ S2 (В2-A): прежние поля `bCallRequestInFlight`/`CallInFlightLeft` — одна структура `FAsdaReplyLatch`.
	const FAsdaReplyLatch SaveCallLatch = CallLatch;
	const int32 SaveRx = RxSinceReset;
	// ★ 27.09 (СРЕДН-1): список «удалено 6111 во время юниона», диагностика 6113 и счётчики отправок
	//   тоже в снимке — сценарии P31 ниже их трогают, а живой юнион мог быть в полёте.
	const TArray<int32> SaveReleased = UnionReleasedUids;
	const int32 SaveUnionMissing = LastUnionMissing;
	const TMap<int32, int32> SaveTx = TxByOpcode;

	// ★★★ ЧУЖОЙ charId НА ВЕСЬ ПРОГОН. При `SelectedCharId == 0` гейт `IsForSelf` замкнут на
	//   «принимаю всё» (иначе офлайн-прогон не смог бы проверить ни одного приёмника), и подмена
	//   поля charId/petUid в теле осталась бы незамеченной — именно так прежняя самопроверка
	//   пропустила ошибку в приёме 6149. Здесь номер ставится явно и снимается в конце.
	UAsdaNetSubsystem* N = Net();
	const int32 SaveCharId = N ? N->SelectedCharId : 0;
	static constexpr int32 kTestCharId = 12345;
	static constexpr int32 kAlienCharId = 999777;
	if (N) { N->SelectedCharId = kTestCharId; }
	// ★ R4-c (15.11.2026) [чистота самотеста]: на время прогона подписчики `OnPetEvent` сняты. Окна питомцев (клетка, развитие, рост,
	//   инкубатор) кладут событие в очередь и обрабатывают его в своём тике — то есть уже ПОСЛЕ итога самотеста: по поддельным
	//   пакетам открывалось окно клетки 43000 и модальное сообщение, менялся вес персонажа, и следующий самотест пачки получал
	//   чужие окна (`asda.Fish.ZoneSelfTest` 74/4). Самотест проверяет приёмники самой подсистемы питомцев; делегат он не читает.
	FAsdaPetEventSignature SaveOnPetEvent = MoveTemp(OnPetEvent);
	OnPetEvent = FAsdaPetEventSignature();
	// ★ R4-c (15.11.2026, ревизия): возврат подписчиков — на любом выходе из функции (сейчас выход один, в конце).
	ON_SCOPE_EXIT { OnPetEvent = MoveTemp(SaveOnPetEvent); };

	FString S;
	S += TEXT("\n╔══════════════════════════════════════════════════════════════════════════════════╗\n");
	S += FString::Printf(TEXT("║ САМОПРОВЕРКА ПИТОМЦЕВ БЕЗ СЕРВЕРА. Метка компиляции: %s %s\n"),
		TEXT(__DATE__), TEXT(__TIME__));
	S += TEXT("╚══════════════════════════════════════════════════════════════════════════════════╝\n");
	S += DumpTables();

	// ── 1. C→S: прогон БОЕВЫХ отправителей через перехватчик стенда ──
	// ★★★ ПОЧЕМУ ИМЕННО ТАК (ревизия 2026-09-21). Прежняя редакция собирала тело ЗДЕСЬ и
	//   сверяла его длину сама с собой — это тавтология: проверка мутациями показала, что
	//   ошибка в боевом `SendPetCall6100` (сдвиг смещения поля) ею НЕ ловится. Теперь
	//   вызываются НАСТОЯЩИЕ `UAsdaNetSubsystem::SendPet*`, а тело снимается перехватчиком
	//   `SendCaptureForStand` — тем же механизмом, которым пользуется стенд побайтовой сверки
	//   (`AsdaPacketStand.cpp`). Перехватчик стоит ПОСЛЕ всех проверок, влияющих на тело, и
	//   короткого замыкания ради: в сокет при нём не уходит ничего.
	S += TEXT("\n── C→S: БОЕВЫЕ отправители через перехватчик (конверт «с секретом»: 8 + 16 + тело) ──\n");
	int32 Fails = 0;
	if (!N)
	{
		S += TEXT(" !! сетевой подсистемы нет — боевые отправители не проверены\n");
		++Fails;
	}
	else
	{
		// ★ НЕНУЛЕВОЙ charId обязателен: при `SelectedCharId == 0` гейт `IsForSelf` замкнут на
		//   «принимаю всё», и подмена поля charId/petUid осталась бы незамеченной — ровно так
		//   прежняя самопроверка пропустила ошибку в приёме 6149.
		auto SaveCapture = N->SendCaptureForStand;
		// ★ Волна 15 (МЕЛК-2): узость чужого перехватчика (6500 самотеста 78200) на время самопроверки снимаем —
		//   иначе пет-пакеты пошли бы мимо перехватчика боевым путём; после прогона возвращаем как было.
		const uint16 SaveOnlyOpcode = N->SendCaptureOnlyOpcode;
		N->SendCaptureOnlyOpcode = 0;
		const int32 Me = kTestCharId;

		struct FCap { int32 Op = 0; TArray<uint8> Body; bool bPad = false; TCHAR Env = 0; int32 Hits = 0; };
		FCap Cap;
		N->SendCaptureForStand = [&Cap](uint16 Op, const uint8* Payload, int32 Len, bool bPad, TCHAR Env)
		{
			Cap.Op = (int32)Op;
			Cap.Body.Reset();
			if (Payload && Len > 0) { Cap.Body.Append(Payload, Len); }
			Cap.bPad = bPad;
			Cap.Env = Env;
			++Cap.Hits;
		};

		// Ожидание по каждому запросу: длина тела, вид конверта и КОНТРОЛЬНЫЕ поля тела.
		//   Поля сверяются по смещениям, взятым с СЕРВЕРА, — то есть мимо кода отправителя.
		struct FTx { int32 Op; int32 Len; const TCHAR* Name; };
		static const FTx Tx[] = {
			{ OP_PET_CALL_REQ,         8, TEXT("6100 призыв/отзыв") },
			{ OP_PET_INCUBATE_REQ,    13, TEXT("6102 инкубатор") },
			{ OP_PET_FEED_REQ,        11, TEXT("6108 кормление") },
			{ OP_PET_RELEASE_REQ,      8, TEXT("6110 выброс") },
			{ OP_PET_UNION_REQ,       18, TEXT("6112 эволюция") },
			{ OP_PET_NAME_CHANGE_REQ, 24, TEXT("6118 переименование") },
			{ OP_PET_SLOT_UP_REQ,      7, TEXT("6121 +ячейки") },
			{ OP_PET_LIMITBREAK_REQ,   9, TEXT("6148 лимит-брейк") },
		};

		for (const FTx& T : Tx)
		{
			Cap = FCap();
			switch (T.Op)
			{
				case OP_PET_CALL_REQ:         N->SendPetCall6100(Me, 1000); break;
				case OP_PET_INCUBATE_REQ:     N->SendPetIncubate6102(Me, 1, 7, 2, 9, 1, -1); break;
				case OP_PET_FEED_REQ:         N->SendPetFeed6108(Me, 1000, 1, 3); break;
				case OP_PET_RELEASE_REQ:      N->SendPetRelease6110(Me, 1001); break;
				case OP_PET_UNION_REQ:        N->SendPetUnion6112(Me, 5, 1000, 1001, 1002); break;
				case OP_PET_NAME_CHANGE_REQ:  N->SendPetRename6118(Me, 1000, TEXT("Барсик")); break;
				case OP_PET_SLOT_UP_REQ:      N->SendPetSlotUp6121(Me, 1, 5); break;
				default:                      N->SendPetLimitBreak6148(1000, 1, 11, -1); break;
			}

			const TArray<uint8>& Bd = Cap.Body;
			// ★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (среднее 5): ТРЕТЬЯ КОПИЯ ЭТИХ ЛЯМБД СВЕДЕНА.
			//   Волна 8 отчиталась «3→1», а на деле свела только две (окна 43000 и 44200) —
			//   эта, в самопроверке модели, осталась жить и повторяла тело байт в байт.
			//   Теперь все три идут в общую точку `AsdaPetUi::StandI32/StandI16/StandU8`.
			auto I32 = [&Bd](int32 O) { return AsdaPetUi::StandI32(Bd, O); };
			auto I16 = [&Bd](int32 O) { return AsdaPetUi::StandI16(Bd, O); };
			auto U8b = [&Bd](int32 O) { return AsdaPetUi::StandU8 (Bd, O); };

			bool bFields = false;
			switch (T.Op)
			{
				case OP_PET_CALL_REQ:
					bFields = (I32(0) == Me && I32(4) == 1000); break;
				case OP_PET_INCUBATE_REQ:
					bFields = (I32(0) == Me && U8b(4) == 1 && I16(5) == 7
						&& U8b(7) == 2 && I16(8) == 9 && U8b(10) == 1 && I16(11) == -1); break;
				case OP_PET_FEED_REQ:
					bFields = (I32(0) == Me && I32(4) == 1000 && U8b(8) == 1 && I16(9) == 3); break;
				case OP_PET_RELEASE_REQ:
					bFields = (I32(0) == Me && I32(4) == 1001); break;
				case OP_PET_UNION_REQ:
					bFields = (I32(0) == Me && I16(4) == 5
						&& I32(6) == 1000 && I32(10) == 1001 && I32(14) == 1002); break;
				case OP_PET_NAME_CHANGE_REQ:
					bFields = (I32(0) == Me && I32(4) == 1000
						&& FAsdaLoginData::DecodeCp1251(Bd.GetData() + 8, 16) == TEXT("Барсик")); break;
				case OP_PET_SLOT_UP_REQ:
					bFields = (I32(0) == Me && U8b(4) == 1 && I16(5) == 5); break;
				default: // 6148 — ★ БЕЗ charId: тело начинается с petUid
					bFields = (I32(0) == 1000 && I16(4) == 11 && U8b(6) == 1 && I16(7) == -1); break;
			}

			const bool bOk = (Cap.Hits == 1) && (Cap.Op == T.Op) && (Bd.Num() == T.Len)
				&& (Cap.Env == TEXT('S')) && bFields;
			if (!bOk) { ++Fails; }
			S += FString::Printf(
				TEXT(" %-24s тело=%2d б (ожидалось %2d) конверт=%c(с секретом) кадр=%d б поля=%s → %s\n"),
				T.Name, Bd.Num(), T.Len, (Cap.Env ? Cap.Env : TEXT('?')), 10 + 24 + Bd.Num(),
				bFields ? TEXT("сходятся") : TEXT("РАСХОДЯТСЯ"), bOk ? TEXT("OK") : TEXT("ОШИБКА"));
			S += DecodeBody(T.Op, Bd);
		}

		// ── ★ КОНТРОЛЬНАЯ ПАРА: проверка обязана быть ЗРЯЧЕЙ ─────────────────────────────────
		//   Закон `pixel-metric-needs-control-pairs`: самопроверка, которая только «рисует
		//   контролы», слепа. Прежняя редакция этого блока сверяла длину тела саму с собой и
		//   действительно не ловила сдвиг поля в боевом сборщике (показано ревизией 2026-09-21).
		//   Здесь берётся ТО ЖЕ тело, что сняли с боевого `SendPetCall6100`, в нём поле сдвигается
		//   на один байт — и сверка ОБЯЗАНА это заметить. Если контроль «проходит», значит
		//   сверка ничего не проверяет, и весь блок C→S ничего не стоит.
		{
			Cap = FCap();
			N->SendPetCall6100(Me, 1000);
			TArray<uint8> Broken = Cap.Body;          // эталонное тело 6100 (8 б)
			bool bDetected = false;
			if (Broken.Num() == 8)
			{
				AsdaBytes::WrI32(Broken.GetData() + 3, 1000);   // тот же сдвиг, что ловила ревизия
				const bool bFieldsOnBroken = (AsdaBytes::RdI32(&Broken[0]) == Me)
					&& (AsdaBytes::RdI32(&Broken[4]) == 1000);
				bDetected = !bFieldsOnBroken;                   // сверка ДОЛЖНА сказать «расходятся»
			}
			if (!bDetected) { ++Fails; }
			S += FString::Printf(
				TEXT(" контроль: сдвиг поля 6100 на 1 байт      → %s\n"),
				bDetected ? TEXT("ЗАМЕЧЕН (сверка зрячая)") : TEXT("ПРОПУЩЕН — СВЕРКА СЛЕПА, блок C→S недействителен"));
		}

		// ── Гейты отправки: проверяем, что они ровно такие, как у оригинала ──
		Cap = FCap();
		Slots.Reset();
		ActiveUniqueId = -1;
		SelectedUniqueId = -1;
		{
			// 6108 «из сумки» без призванного → пакет НЕ уходит, взводится нотис 101105.
			const bool bSent = RequestFeed(1, 3, /*bFromWindowSlot*/false);
			const bool bNotice = ConsumeFeedNeedsActivePetNotice();
			const bool bOk = (!bSent && bNotice);
			if (!bOk) { ++Fails; }
			S += FString::Printf(TEXT(" гейт 6108 из сумки без призванного      → %s (нотис 101105 %s)\n"),
				bOk ? TEXT("OK, не отправлен") : TEXT("ОШИБКА"), bNotice ? TEXT("взведён") : TEXT("НЕТ"));
		}
		{
			// 6108 «кнопкой окна» без выбранного → молча не уходит, нотиса НЕТ.
			const bool bSent = RequestFeed(1, 3, /*bFromWindowSlot*/true);
			const bool bNotice = ConsumeFeedNeedsActivePetNotice();
			const bool bOk = (!bSent && !bNotice);
			if (!bOk) { ++Fails; }
			S += FString::Printf(TEXT(" гейт 6108 кнопкой окна без выбранного   → %s (нотиса нет: %s)\n"),
				bOk ? TEXT("OK, не отправлен") : TEXT("ОШИБКА"), bNotice ? TEXT("НЕТ, взведён") : TEXT("да"));
		}
		{
			// Повторный 6100, пока первый в полёте, — второй запрос отбрасывается.
			CallLatch.Disarm();
			const bool b1 = RequestCallPet(1000);
			const bool b2 = RequestCallPet(1000);
			const bool bOk = (b1 && !b2);
			if (!bOk) { ++Fails; }
			Pending.Reset();
			CallLatch.Disarm();
			S += FString::Printf(TEXT(" гейт 6100 «запрос уже в полёте»         → %s\n"),
				bOk ? TEXT("OK, второй отброшен") : TEXT("ОШИБКА"));
		}

		{
			// ★ 27.09 (СРЕДН-1): БОЕВАЯ отправка 6112 через `SendRequest` обязана очистить список
			//   «удалено 6111» — uid, выброшенный ДО юниона обычным 6110, не должен приниматься за
			//   материал этого юниона. Тело ловит перехватчик, в сокет не уходит ничего.
			UnionReleasedUids.Reset();
			UnionReleasedUids.Add(777);                      // «старый» выброс до юниона
			FAsdaPetPendingRequest R;
			R.Opcode = OP_PET_UNION_REQ;
			R.UnionIndex = 5;
			R.Materials = { 1000, 1001, 1002 };
			Cap = FCap();
			const bool bSent = SendRequest(R);
			const bool bOk = bSent && Cap.Hits == 1 && Cap.Op == OP_PET_UNION_REQ && UnionReleasedUids.Num() == 0;
			if (!bOk) { ++Fails; }
			S += FString::Printf(TEXT(" отправка 6112 чистит список «удалено 6111» → %s (осталось %d)\n"),
				bOk ? TEXT("OK") : TEXT("ОШИБКА"), UnionReleasedUids.Num());
			UnionReleasedUids.Reset();
		}

		N->SendCaptureForStand = SaveCapture;
		N->SendCaptureOnlyOpcode = SaveOnlyOpcode;
	}

	// ── 2. S→C: синтез тела, прогон через НАСТОЯЩИЙ приёмник, проверка эффекта ──
	S += TEXT("\n── S→C: синтез тела и прогон через боевые приёмники ──\n");
	Slots.Reset();
	ActiveUniqueId = -1;
	SelectedUniqueId = -1;
	PetPossessionCount = kDefaultPossession;

	auto Step = [&](const TCHAR* Name, int32 Op, TArray<uint8>&& Body, TFunctionRef<bool()> Check)
	{
		const int32 Len = Body.Num();
		S += DecodeBody(Op, Body);
		OnPetPacket(Op, Body);
		const bool bOk = Check();
		if (!bOk) { ++Fails; }
		S += FString::Printf(TEXT(" %-26s тело=%3d б → %s\n"), Name, Len, bOk ? TEXT("OK") : TEXT("ОШИБКА"));
	};

	const int32 Me = SelfCharId();   // = kTestCharId, выставлен в начале прогона

	// ★ ЧУЖОЙ ПАКЕТ ОБЯЗАН БЫТЬ ОТВЕРГНУТ. Без ненулевого charId эта проверка невозможна вовсе.
	{
		TArray<uint8> B; B.SetNumZeroed(5);
		AsdaBytes::WrI32(B.GetData(), kAlienCharId); B[4] = 30;
		const int32 Before = PetPossessionCount;
		Step(TEXT("6123 ЧУЖОЙ charId"), OP_PET_SLOT_COUNT, MoveTemp(B), [&] { return PetPossessionCount == Before; });
	}

	// 6123 — число ячеек
	{
		TArray<uint8> B; B.SetNumZeroed(5);
		AsdaBytes::WrI32(B.GetData(), Me); B[4] = 12;
		Step(TEXT("6123 ячейки"), OP_PET_SLOT_COUNT, MoveTemp(B), [&] { return PetPossessionCount == 12; });
	}
	// 6106 — список из трёх, первый призван
	{
		TArray<uint8> B; B.SetNumZeroed(5 + 46 * 3);
		AsdaBytes::WrI32(B.GetData(), Me); B[4] = 0;
		for (int32 i = 0; i < 3; ++i)
		{
			const int32 O = 5 + i * 46;
			FAsdaPetSlot P;
			P.UniqueId = 1000 + i;
			P.PetIndex = 13 + i;
			P.Name = FString::Printf(TEXT("Пет%d"), i);
			P.Fullness = 80 + i;
			P.Level = 3;
			P.MaxLevel = 5;
			P.Exp = 100 + i;
			for (int32 j = 0; j < 3; ++j) { P.Abilities[j].Type = j + 1; P.Abilities[j].Value = 1000 * (j + 1) + i; }
			AsdaBytes::WrI32(B.GetData() + O, P.UniqueId);
			WritePetRecord(B, O + 4, P);
			B[O + 45] = (i == 0) ? 1 : 0;
		}
		Step(TEXT("6106 список (3 записи)"), OP_PET_LIST, MoveTemp(B), [&]
		{
			const FAsdaPetSlot* P = FindByUniqueId(1002);
			return Slots.Num() == 3 && ActiveUniqueId == 1000 && P && P->PetIndex == 15
				&& P->Abilities[2].Value == 3002 && P->MaxLevel == 5;
		});
	}
	// 6106 — битая длина: пакет обязан быть отброшен целиком
	{
		TArray<uint8> B; B.SetNumZeroed(5 + 46 * 2 + 1);   // лишний байт
		AsdaBytes::WrI32(B.GetData(), Me);
		const int32 Before = Slots.Num();
		Step(TEXT("6106 битая длина"), OP_PET_LIST, MoveTemp(B), [&] { return Slots.Num() == Before; });
	}
	// 6101 — успех: тумблер призыва (1000 был призван → станет отозван)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI32(B.GetData(), Me); B[4] = 1; AsdaBytes::WrI32(B.GetData() + 5, 1000);
		CallLatch.bArmed = true;   // как прежде — только признак (эхо 1000 осталось от `RequestCallPet(1000)` выше)
		Step(TEXT("6101 отзыв (тумблер)"), OP_PET_CALL_RESULT, MoveTemp(B), [&]
		{
			return ActiveUniqueId == -1 && !CallLatch.IsArmed();
		});
	}
	// 6101 — успех: тот же питомец призывается обратно
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI32(B.GetData(), Me); B[4] = 1; AsdaBytes::WrI32(B.GetData() + 5, 1000);
		Step(TEXT("6101 призыв (тумблер)"), OP_PET_CALL_RESULT, MoveTemp(B), [&] { return ActiveUniqueId == 1000; });
	}
	// 6104 — сытость и бонусы
	{
		TArray<uint8> B; B.SetNumZeroed(29);
		AsdaBytes::WrI16(B.GetData(), 7); AsdaBytes::WrI32(B.GetData() + 2, Me);
		AsdaBytes::WrI32(B.GetData() + 6, 1001); B[10] = 55;
		for (int32 j = 0; j < 3; ++j)
		{
			AsdaBytes::WrI16(B.GetData() + 11 + 6 * j, 7 + j);
			AsdaBytes::WrI32(B.GetData() + 13 + 6 * j, 555 + j);
		}
		Step(TEXT("6104 сытость+бонусы"), OP_PET_CONDITION_UPDATE, MoveTemp(B), [&]
		{
			const FAsdaPetSlot* P = FindByUniqueId(1001);
			return P && P->Fullness == 55 && P->Abilities[2].Type == 9 && P->Abilities[2].Value == 557;
		});
	}
	// 6105 — опыт с повышением уровня
	{
		TArray<uint8> B; B.SetNumZeroed(15);
		AsdaBytes::WrI16(B.GetData(), 7); AsdaBytes::WrI32(B.GetData() + 2, Me);
		AsdaBytes::WrI32(B.GetData() + 6, 1001);
		AsdaBytes::WrI16(B.GetData() + 10, 4321); B[12] = 1; AsdaBytes::WrI16(B.GetData() + 13, 4);
		Step(TEXT("6105 опыт+уровень"), OP_PET_EXP_UPDATE, MoveTemp(B), [&]
		{
			const FAsdaPetSlot* P = FindByUniqueId(1001);
			return P && P->Exp == 4321 && P->Level == 4;
		});
	}
	// 6107 — голод отозвал питомца
	{
		TArray<uint8> B; B.SetNumZeroed(8);
		AsdaBytes::WrI32(B.GetData(), Me); AsdaBytes::WrI32(B.GetData() + 4, 1000);
		Step(TEXT("6107 голод"), OP_PET_HUNGER_RECALL, MoveTemp(B), [&]
		{
			const FAsdaPetSlot* P = FindByUniqueId(1000);
			return ActiveUniqueId == -1 && P && !P->bIsActive;
		});
	}
	// 6109 — кормление, успех
	{
		TArray<uint8> B; B.SetNumZeroed(89);
		AsdaBytes::WrI32(B.GetData(), Me); AsdaBytes::WrI32(B.GetData() + 4, 1001);
		B[65] = 1; B[66] = 100; AsdaBytes::WrI32(B.GetData() + 67, 4200);
		for (int32 j = 0; j < 3; ++j)
		{
			AsdaBytes::WrI16(B.GetData() + 71 + 6 * j, 20 + j);
			AsdaBytes::WrI32(B.GetData() + 73 + 6 * j, 900 + j);
		}
		Step(TEXT("6109 кормление"), OP_PET_FEED_RESULT, MoveTemp(B), [&]
		{
			const FAsdaPetSlot* P = FindByUniqueId(1001);
			return P && P->Fullness == 100 && P->Abilities[1].Value == 901;
		});
	}
	// 6103 — инкубатор (код запоминается, список придёт отдельно)
	{
		TArray<uint8> B; B.SetNumZeroed(18);
		AsdaBytes::WrI32(B.GetData(), Me); B[4] = 1;
		Step(TEXT("6103 инкубатор"), OP_PET_INCUBATE_RESULT, MoveTemp(B), [&] { return LastIncubateResult == 1; });
	}
	// 6119 — переименование
	{
		TArray<uint8> B; B.SetNumZeroed(25);
		AsdaBytes::WrI32(B.GetData(), Me); AsdaBytes::WrI32(B.GetData() + 4, 1002);
		FAsdaLoginData::EncodeCp1251(TEXT("Мурзик"), B.GetData() + 8, 16);
		B[24] = 1;
		Step(TEXT("6119 переименование"), OP_PET_NAME_CHANGE_RESULT, MoveTemp(B), [&]
		{
			const FAsdaPetSlot* P = FindByUniqueId(1002);
			return P && P->Name == TEXT("Мурзик") && SelectedUniqueId == 1002;
		});
	}
	// 6113 — эволюция
	{
		TArray<uint8> B; B.SetNumZeroed(56);
		AsdaBytes::WrI32(B.GetData(), Me); B[4] = 1; AsdaBytes::WrI32(B.GetData() + 5, 2000);
		FAsdaPetSlot New;
		New.PetIndex = 21; New.Name = TEXT("Эво"); New.Fullness = 100; New.Level = 4; New.MaxLevel = 6; New.Exp = 0;
		WritePetRecord(B, 9, New);
		// ★ Это МЛАДШИЕ 16 БИТ uid съеденных материалов (а не породы): 1000 и 1001 из списка,
		//   третий слот — −1 «материала нет» (так его кладёт ctor `sub_45B9D0`).
		AsdaBytes::WrI16(B.GetData() + 50, 1000);
		AsdaBytes::WrI16(B.GetData() + 52, 1001);
		AsdaBytes::WrI16(B.GetData() + 54, -1);
		// ★ КРИТ-2: три материала обязаны ИСЧЕЗНУТЬ из списка (эталон стирает их сам,
		//   npc_mob_pet.c:7677 → :5549; сервер список 6106 после юниона НЕ шлёт).
		const int32 BeforeNum = Slots.Num();
		Step(TEXT("6113 эволюция + чистка"), OP_PET_UNION_RESULT, MoveTemp(B), [&]
		{
			const FAsdaPetSlot* P = FindByUniqueId(2000);
			const bool bNew = P && P->PetIndex == 21 && P->Level == 4 && P->MaxLevel == 6 && !P->bIsActive;
			// были 1000 и 1001 (1002 уже удалён шагом 6111 ниже — он идёт позже, так что здесь оба живы),
			//   материалы 1000/1001 должны уйти, вместо них появиться 2000.
			const bool bEaten = (FindByUniqueId(1000) == nullptr) && (FindByUniqueId(1001) == nullptr);
			const bool bCount = (Slots.Num() == BeforeNum - 2 + 1);
			return bNew && bEaten && bCount;
		});
	}
	// ★ КРИТ-1: 6149 адресуется `petUid` ИЗ ТЕЛА (байт 0), а не charId и не «выбранным».
	//   Ставим ЗАВЕДОМО ДРУГОГО выбранного — если код снова начнёт применять к нему, шаг упадёт.
	{
		SetSelectedUniqueId(2000);
		TArray<uint8> B; B.SetNumZeroed(110);
		AsdaBytes::WrI32(B.GetData(), 1002);       // ← petUid, НЕ charId
		FAsdaPetSlot Upd;
		Upd.PetIndex = 14; Upd.Name = TEXT("Рост"); Upd.Fullness = 90; Upd.Level = 5; Upd.MaxLevel = 7; Upd.Exp = 50;
		WritePetRecord(B, 4, Upd);
		AsdaBytes::WrI16(B.GetData() + 45, 11);    // itemSlot
		B[47] = 1;                                 // itemBag
		B[48] = 2;                                 // исход
		AsdaBytes::WrI32(B.GetData() + 49, 4100);  // вес
		Step(TEXT("6149 лимит-брейк по uid"), OP_PET_LIMITBREAK_RESULT, MoveTemp(B), [&]
		{
			const FAsdaPetSlot* Target = FindByUniqueId(1002);
			const FAsdaPetSlot* Other  = FindByUniqueId(2000);
			return Target && Target->MaxLevel == 7 && Target->Level == 5 && Target->PetIndex == 14
				&& LastLimitBreakResult == 2
				&& Other && Other->MaxLevel == 6;   // выбранный НЕ тронут
		});
	}
	// 6122 — +ячейки
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		AsdaBytes::WrI32(B.GetData(), Me); B[4] = 1; B[5] = 18; B[6] = 1;
		AsdaBytes::WrI16(B.GetData() + 7, 4); AsdaBytes::WrI32(B.GetData() + 9, 4000);
		Step(TEXT("6122 +ячейки"), OP_PET_SLOT_UP_RESULT, MoveTemp(B), [&] { return PetPossessionCount == 18; });
	}
	// 6111 — выброс
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI32(B.GetData(), Me); AsdaBytes::WrI32(B.GetData() + 4, 1002); B[8] = 1;
		Step(TEXT("6111 выброс"), OP_PET_RELEASE_RESULT, MoveTemp(B), [&]
		{
			return FindByUniqueId(1002) == nullptr && SelectedUniqueId == -1;
		});
	}
	// ── ★★★ 27.09 (ревизия P31/P32, СРЕДН-1): ЮНИОН С СЕРВЕРНЫМ ПАТЧЕМ P31 И БЕЗ НЕГО ──
	//   P31 досылает 6111 на каждый съеденный материал ДО 6113. Проверяем боевыми приёмниками, что
	//   6113 не ищет уже удалённый материал повторно по младшим 16 битам: питомец-двойник
	//   (uid + 65536) обязан выжить при любом порядке в списке, Warning «не найден» — 0.
	//   Контроль: без 6111 (сервер без P31) 6113 удаляет материалы сам, как раньше.
	{
		static constexpr int32 kTwin = 1000 + 65536;   // младшие 16 бит = 1000 — двойник материала 1000
		// Синтез настоящего 6106 из заданных uid (никто не призван) — 6106 ДОПОЛНЯЕТ список, поэтому сперва Reset.
		auto MakeList = [&](const TArray<int32>& Uids)
		{
			Slots.Reset();
			ActiveUniqueId = -1;
			SelectedUniqueId = -1;
			UnionReleasedUids.Reset();
			TArray<uint8> B; B.SetNumZeroed(5 + 46 * Uids.Num());
			AsdaBytes::WrI32(B.GetData(), Me); B[4] = 0;
			for (int32 i = 0; i < Uids.Num(); ++i)
			{
				const int32 O = 5 + i * 46;
				FAsdaPetSlot P;
				P.UniqueId = Uids[i]; P.PetIndex = 13; P.Name = FString::Printf(TEXT("П%d"), i);
				P.Fullness = 90; P.Level = 5; P.MaxLevel = 5; P.Exp = 0;
				AsdaBytes::WrI32(B.GetData() + O, Uids[i]);
				WritePetRecord(B, O + 4, P);
				B[O + 45] = 0;                               // не призван
			}
			OnPetPacket(OP_PET_LIST, B);
		};
		// Тело 6111 «выброс успешен» (9 б).
		auto Make6111 = [&](int32 Uid)
		{
			TArray<uint8> B; B.SetNumZeroed(9);
			AsdaBytes::WrI32(B.GetData(), Me); AsdaBytes::WrI32(B.GetData() + 4, Uid); B[8] = 1;
			return B;
		};
		// Тело 6113 «юнион успешен» (56 б) с эхом младших 16 бит uid трёх материалов (−1 = «нет»).
		auto Make6113 = [&](int32 NewUid, int32 M0, int32 M1, int32 M2)
		{
			TArray<uint8> B; B.SetNumZeroed(56);
			AsdaBytes::WrI32(B.GetData(), Me); B[4] = 1; AsdaBytes::WrI32(B.GetData() + 5, NewUid);
			FAsdaPetSlot New;
			New.PetIndex = 21; New.Name = TEXT("Эво"); New.Fullness = 100; New.Level = 1; New.MaxLevel = 6; New.Exp = 0;
			WritePetRecord(B, 9, New);
			AsdaBytes::WrI16(B.GetData() + 50, (int16)(uint16)(M0 & 0xFFFF));
			AsdaBytes::WrI16(B.GetData() + 52, (int16)(uint16)(M1 & 0xFFFF));
			AsdaBytes::WrI16(B.GetData() + 54, (int16)(uint16)(M2 & 0xFFFF));
			return B;
		};

		// Сценарий P31-а: двойник ПЕРВЫМ в списке (без правки 6113 стирал двойника вслед за материалом).
		// Сценарий P31-б: двойник ПОСЛЕ материалов (без правки — «ухудшение»: двойник стирался тоже).
		for (int32 Order = 0; Order < 2; ++Order)
		{
			if (Order == 0) { MakeList({ kTwin, 1000, 1001, 1500 }); }
			else            { MakeList({ 1000, 1001, kTwin, 1500 }); }
			Step(Order == 0 ? TEXT("P31 6111(1000) двойн.1") : TEXT("P31 6111(1000) двойн.2"),
				OP_PET_RELEASE_RESULT, Make6111(1000), [&]
			{
				return FindByUniqueId(1000) == nullptr && FindByUniqueId(kTwin) != nullptr
					&& UnionReleasedUids.Num() == 1;
			});
			Step(TEXT("P31 6111(1001)"), OP_PET_RELEASE_RESULT, Make6111(1001), [&]
			{
				return FindByUniqueId(1001) == nullptr && UnionReleasedUids.Num() == 2;
			});
			Step(Order == 0 ? TEXT("P31 6113 двойник первым") : TEXT("P31 6113 двойник после"),
				OP_PET_UNION_RESULT, Make6113(3000, 1000, 1001, -1), [&]
			{
				// Выжили двойник и посторонний 1500, родился 3000; ложных Warning нет; список погашен.
				return FindByUniqueId(kTwin) != nullptr && FindByUniqueId(1500) != nullptr
					&& FindByUniqueId(3000) != nullptr && Slots.Num() == 3
					&& LastUnionMissing == 0 && UnionReleasedUids.Num() == 0;
			});
		}

		// Сценарий P31-в: три материала, у одного uid >= 32768 (эхо как i16 «отрицательное»), эхо в другом порядке.
		MakeList({ 40000, 1000, 1001, kTwin });
		Step(TEXT("P31 6111(40000)"), OP_PET_RELEASE_RESULT, Make6111(40000), [&] { return UnionReleasedUids.Num() == 1; });
		Step(TEXT("P31 6111(1000)"),  OP_PET_RELEASE_RESULT, Make6111(1000),  [&] { return UnionReleasedUids.Num() == 2; });
		Step(TEXT("P31 6111(1001)"),  OP_PET_RELEASE_RESULT, Make6111(1001),  [&] { return UnionReleasedUids.Num() == 3; });
		Step(TEXT("P31 6113 три мат.+двойник"), OP_PET_UNION_RESULT, Make6113(3001, 1001, 40000, 1000), [&]
		{
			return FindByUniqueId(kTwin) != nullptr && FindByUniqueId(3001) != nullptr && Slots.Num() == 2
				&& LastUnionMissing == 0 && UnionReleasedUids.Num() == 0;
		});

		// Сценарий «без P31»: 6113 без предшествующих 6111 — материалы удаляет сам 6113, как раньше.
		MakeList({ 1000, 1001, kTwin, 1500 });
		Step(TEXT("без P31: 6113 чистит сам"), OP_PET_UNION_RESULT, Make6113(3002, 1000, 1001, -1), [&]
		{
			return FindByUniqueId(1000) == nullptr && FindByUniqueId(1001) == nullptr
				&& FindByUniqueId(kTwin) != nullptr && FindByUniqueId(1500) != nullptr
				&& FindByUniqueId(3002) != nullptr && Slots.Num() == 3 && LastUnionMissing == 0;
		});

		// Отказ 6113 (код 3) гасит список и записей не трогает.
		MakeList({ 1000, 1001 });
		UnionReleasedUids.Add(1000 + 65536 * 2);            // «хвост» 6111, пришедший во время юниона
		{
			TArray<uint8> B = Make6113(0, 1000, 1001, -1);
			B[4] = 3;
			Step(TEXT("6113 отказ гасит список"), OP_PET_UNION_RESULT, MoveTemp(B), [&]
			{
				return Slots.Num() == 2 && UnionReleasedUids.Num() == 0 && LastUnionMissing == 0;
			});
		}

		// ★ КОНТРОЛЬНАЯ ПАРА (закон `pixel-metric-needs-control-pairs`): сценарии выше обязаны быть
		//   ЗРЯЧИМИ. Воспроизводим порт ДО правки — «забываем» список 6111 перед 6113. Тогда 6113 ищет
		//   материал 1000 в `Slots` и находит двойника (он первым): двойник ОБЯЗАН исчезнуть, а материал
		//   1001 дать 1 ложный Warning. Если этого не произошло — сценарии P31 ничего не доказывают.
		MakeList({ kTwin, 1000, 1001 });
		OnPetPacket(OP_PET_RELEASE_RESULT, Make6111(1000));
		OnPetPacket(OP_PET_RELEASE_RESULT, Make6111(1001));
		UnionReleasedUids.Reset();                          // ← имитация порта ДО правки
		OnPetPacket(OP_PET_UNION_RESULT, Make6113(3003, 1000, 1001, -1));
		{
			const bool bDetected = (FindByUniqueId(kTwin) == nullptr) && (LastUnionMissing == 1);
			if (!bDetected) { ++Fails; }
			S += FString::Printf(TEXT(" контроль: 6113 без учёта 6111            → %s\n"),
				bDetected ? TEXT("двойник стёрт, 1 ложный Warning (сверка зрячая)")
				          : TEXT("НЕ ВОСПРОИЗВЕДЕНО — сценарии P31 недействительны"));
		}
		Slots.Reset();
		UnionReleasedUids.Reset();
		LastUnionMissing = 0;
	}

	// 6120 — тот же кадр, что принимает сеть; проверяем только разбор полей
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI32(B.GetData(), Me); AsdaBytes::WrI16(B.GetData() + 4, 21);
		FAsdaLoginData::EncodeCp1251(TEXT("Эво"), B.GetData() + 6, 16);
		S += DecodeBody(OP_PEER_ACTIVE_PET, B);
		S += TEXT(" 6120 питомец в мире      тело= 22 б → принимается СЕТЬЮ (UAsdaNetSubsystem), дубля нет\n");
	}

	// ── 3. Хвостовой паддинг сервера не должен ломать разбор ──
	{
		TArray<uint8> B; B.SetNumZeroed(5 + 19);   // 6123 (5 б) + максимальный мусор 19 б
		AsdaBytes::WrI32(B.GetData(), Me); B[4] = 24;
		for (int32 i = 5; i < B.Num(); ++i) { B[i] = (uint8)(0xA0 + i); }
		Step(TEXT("6123 + паддинг 19 б"), OP_PET_SLOT_COUNT, MoveTemp(B), [&] { return PetPossessionCount == 24; });
	}

	// ── 4. Итоги ──
	S += FString::Printf(TEXT("\n── ИТОГ: проверок провалено %d ──\n"), Fails);
	S += DumpState();

	// Возврат состояния.
	Slots = SaveSlots;
	PetPossessionCount = SavePoss;
	ActiveUniqueId = SaveActive;
	SelectedUniqueId = SaveSelected;
	CallLatch = SaveCallLatch;   // ★ восстанавливается целиком (S2, В2-A)
	RxSinceReset = SaveRx;
	UnionReleasedUids = SaveReleased;    // ★ 27.09
	LastUnionMissing = SaveUnionMissing; // ★ 27.09
	TxByOpcode = SaveTx;                 // ★ 27.09: боевой SendRequest(6112) выше прибавил счётчик
	if (N) { N->SelectedCharId = SaveCharId; }
	S += TEXT("(состояние восстановлено из снимка, charId сети возвращён)\n");
	return S;
}

namespace {
// ════════════════════════════ КОНСОЛЬНЫЕ КЛЮЧИ (проверка без сервера) ═════════════════════════
	UAsdaPetSubsystem* PetSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetDump(
	TEXT("asda.Pet.Dump"),
	TEXT("Состояние питомцев: список, ячейки, призванный, коды последних ответов, счётчики пакетов."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& /*Args*/, UWorld* W)
		{
			if (UAsdaPetSubsystem* S = PetSub(W)) { UE_LOG(LogAsdaPet, Log, TEXT("%s"), *S->DumpState()); }
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetTables(
	TEXT("asda.Pet.Tables"),
	TEXT("Таблицы питомцев: размеры, сверка клиент/сервер, пороги лимит-брейка (файл и память сервера), рецепты."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& /*Args*/, UWorld* W)
		{
			if (UAsdaPetSubsystem* S = PetSub(W)) { UE_LOG(LogAsdaPet, Log, TEXT("%s"), *S->DumpTables()); }
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetTest(
	TEXT("asda.Pet.Test"),
	TEXT("asda.Pet.Test <N> [первая порода] — подставить N тестовых питомцев СИНТЕЗОМ настоящего ")
	TEXT("пакета 6106 (0..30). Второй аргумент задаёт породу первого питомца, дальше по кругу: ")
	TEXT("без него стенд не мог выдать породы 30 и 31 вовсе (потолок слотов 30)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* W)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6106 (питомцы uid 1000+) — клик по ним отправил бы 6100/6108 с фальшивым uid.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.Test"))) { return; }
			if (UAsdaPetSubsystem* S = PetSub(W))
			{
				S->InjectTestPets(Args.Num() >= 1 ? FCString::Atoi(*Args[0]) : 3,
					Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 0);
				UE_LOG(LogAsdaPet, Log, TEXT("%s"), *S->DumpState());
			}
		}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 8 (среднее 3): СИНТЕЗ 6107 «ПИТОМЕЦ СПРЯТАЛСЯ ОТ ГОЛОДА».
//   ЗАЧЕМ. Окна 44200 и 43500 имеют ЭТАЛОННЫЕ гейты 109975 и 109974: «пока есть ПРИЗВАННЫЙ
//   питомец, окно не открывается» (ui_core.c:90614 и соседний). Сквозной прогон волны 8 их не
//   проходил, а объяснение в скрипте («`asda.Pet.EvoPets 0` синтезирует 6106, где никто не
//   призван») было ФАКТИЧЕСКИ НЕВЕРНЫМ: 6106 приходит пачкой и ДОПОЛНЯЕТ список — признак
//   призванного у уже лежащей записи не снимается (`OnPetList6106` ставит `ActiveUniqueId`
//   только для записей С признаком, а старые не трогает).
//   ПОЧЕМУ ИМЕННО 6107, А НЕ «ПОЧИСТИТЬ ПОЛЕ РУКАМИ». 6107 — ШТАТНОЕ событие оригинала: сытость
//   упала в ноль, и сервер отзывает питомца (`CPetData::OnHungerRecall@0xDC88A0`,
//   item_enhance.c:4545). Это ровно тот путь, которым «призванный» исчезает в живой игре.
//   Закон `diagnostic-must-use-real-path`: тело собирается настоящее и скармливается БОЕВОМУ
//   приёмнику `OnPetPacket`, а не пишется в поля.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetHungerRecall(
	TEXT("asda.Pet.HungerRecall"),
	TEXT("asda.Pet.HungerRecall [uid] — синтезировать НАСТОЯЩЕЕ тело 6107 (8 б) «питомец спрятался ")
	TEXT("от голода» и скормить его боевому приёмнику. Без аргумента — по текущему ПРИЗВАННОМУ. ")
	TEXT("Нужна прогонам окон 44200/43500: их эталонные гейты требуют, чтобы призванного НЕ БЫЛО."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* W)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6107 помечает питомца отозванным и открывает окна 44200/43500.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.HungerRecall"))) { return; }
			UAsdaPetSubsystem* S = PetSub(W);
			if (!S) { return; }
			UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
			UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			const int32 Uid = (Args.Num() >= 1) ? FCString::Atoi(*Args[0]) : S->GetActiveUniqueId();
			if (Uid < 0)
			{
				UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] asda.Pet.HungerRecall: призванного нет, синтезировать нечего"));
				return;
			}
			TArray<uint8> Body;
			Body.SetNumZeroed(8);
			AsdaBytes::WrI32(Body.GetData() + 0, N ? (int32)N->SelectedCharId : 0);   // +0 charId
			AsdaBytes::WrI32(Body.GetData() + 4, Uid);                                // +4 petUid
			S->OnPetPacket(OP_PET_HUNGER_RECALL, Body);
			UE_LOG(LogAsdaPet, Log,
				TEXT("[питомцы] синтез 6107: pet=%d отозван голодом; призван теперь %d"),
				Uid, S->GetActiveUniqueId());
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetDecode(
	TEXT("asda.Pet.Decode"),
	TEXT("asda.Pet.Decode <опкод> <hex тела> — разобрать тело пакета и напечатать поля. Состояние не меняется."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaPetSubsystem* S = PetSub(W);
			if (!S || Args.Num() < 2) { return; }
			const int32 Op = FCString::Atoi(*Args[0]);
			FString Hex = Args[1].Replace(TEXT(" "), TEXT(""));
			TArray<uint8> B;
			for (int32 i = 0; i + 1 < Hex.Len(); i += 2)
			{
				B.Add((uint8)FParse::HexNumber(*Hex.Mid(i, 2)));
			}
			UE_LOG(LogAsdaPet, Log, TEXT("%s"), *S->DecodeBody(Op, B));
		}));
#endif // ASDA_DEV_CONSOLE

// ★ 27.09 (ревизия P31/P32, СРЕДН-1): ПОДСТАНОВКА ПЕТ-ПАКЕТА S→C В БОЕВОЙ ПРИЁМНИК.
//   ЗАЧЕМ. Ручной прогон последовательностей, которых офлайн иначе не получить: «6111 × N → 6113»
//   от серверного патча P31, 6113 без 6111 (сервер без патча), отказы. Тело скармливается
//   НАСТОЯЩЕМУ `OnPetPacket` (закон `diagnostic-must-use-real-path`), а не пишется в поля.
//   Тело подставляется как есть: charId пишет сам вызывающий (у большинства пет-пакетов он в +0,
//   у 6104/6105 в +2, у 6149 его нет вовсе). Гейт `IsForSelf` работает как в бою.
//   ТОЛЬКО ОФЛАЙН: поддельный 6106/6113 меняет список питомцев, клик по фальшивой записи отправил
//   бы на живой сервер запрос с чужим uid.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetInject(
	TEXT("asda.Pet.Inject"),
	TEXT("asda.Pet.Inject <опкод> <hex тела> — ТОЛЬКО ОФЛАЙН: подставить пет-пакет S→C (тело без шапки) ")
	TEXT("в боевой приёмник и напечатать разбор и состояние. Пример P31: 6111 ×2, затем 6113."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* W)
		{
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.Inject"))) { return; }
			UAsdaPetSubsystem* S = PetSub(W);
			if (!S) { return; }
			if (Args.Num() < 2)
			{
				UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] asda.Pet.Inject: нужно <опкод> <hex тела>"));
				return;
			}
			const int32 Op = FCString::Atoi(*Args[0]);
			// Hex можно дать одним словом или несколькими аргументами через пробел.
			FString Hex;
			for (int32 a = 1; a < Args.Num(); ++a) { Hex += Args[a]; }
			Hex.ReplaceInline(TEXT(" "), TEXT(""));
			if ((Hex.Len() % 2) != 0)
			{
				UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] asda.Pet.Inject: нечётная длина hex (%d) — отказ"), Hex.Len());
				return;
			}
			TArray<uint8> B;
			for (int32 i = 0; i + 1 < Hex.Len(); i += 2)
			{
				B.Add((uint8)FParse::HexNumber(*Hex.Mid(i, 2)));
			}
			UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] подстановка %d (%d б):\n%s"), Op, B.Num(), *S->DecodeBody(Op, B));
			S->OnPetPacket(Op, B);
			UE_LOG(LogAsdaPet, Log, TEXT("%s"), *S->DumpState());
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetSelfTest(
	TEXT("asda.Pet.SelfTest"),
	TEXT("Полная самопроверка питомцев без сервера: тела всех C→S, прогон всех S→C через боевые приёмники, сверка таблиц."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* W)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: самопроверка ставит перехват ВСЕХ опкодов на живую сетевую подсистему и подменяет SelectedCharId — только офлайн.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Pet.SelfTest"), KitArgs)) { return; }
			UAsdaPetSubsystem* S = PetSub(W);
			if (!S) { AsdaSelfTestKit::Refused(W, TEXT("asda.Pet.SelfTest"), KitArgs.bQuit, TEXT("нет подсистемы питомцев")); return; }
			const FString Rep = S->RunSelfTest();
			UE_LOG(LogAsdaPet, Log, TEXT("%s"), *Rep);
			// ★ 11.10.2026 (В3 S1): единый итог и `quit` для пакетного прогона (прежде команда не выходила — пакет стоял до
			//   таймаута). Провалы — из строки отчёта «ИТОГ: проверок провалено N», ok — строки отчёта с вердиктом «OK».
			int32 Fails = -1, Oks = 0;
			TArray<FString> Lines;
			Rep.ParseIntoArrayLines(Lines);
			for (const FString& L : Lines)
			{
				if (L.Contains(TEXT("проверок провалено"))) { Fails = FCString::Atoi(*L.RightChop(L.Find(TEXT("провалено")) + 9)); }
				else if (L.EndsWith(TEXT("OK")) || L.Contains(TEXT("→ OK"))) { ++Oks; }
			}
			AsdaSelfTestKit::Finish(W, TEXT("asda.Pet.SelfTest"), KitArgs.bQuit, LogAsdaPet, TEXT("[питомцы] САМОПРОВЕРКА"), Oks,
				Fails < 0 ? 1 : Fails, Fails < 0 ? TEXT(" (строки итога в отчёте нет)") : TEXT(""));
		}));
#endif // ASDA_DEV_CONSOLE
