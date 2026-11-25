// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «СБРОС УЛУЧШЕНИЙ У КУЗНЕЦА ПРИМЕНЯЕТСЯ К ПРЕДМЕТУ» (25.10.2026): `asda.SelfTest.ItemReset [quit]`.
//   Жалоба игроков: полный сброс (до 0) оружия/брони у кузнеца пишет «успешно», а предмет остаётся улучшенным до перезапуска.
//   Причина: приёмник ответа 6507 (`UAsdaItemResetSubsystem::OnResetResult`) показывал нотис и ВЫБРАСЫВАЛ данные ветки
//   `case 1` эталона `uiInventoryOnRecvSlotUpdate@0xD31430` (запись цели @7, материалы @64, золото @3, вес @1).
//   Исправление — `ApplyResultToInventory` под ключом `asda.ItemReset.ApplyResult` (1 по умолчанию).
//   Реальный путь (закон diagnostic-must-use-real-path), без сети: тело 6507 подаётся в ШТАТНЫЙ `HandlePacket`
//   (`UAsdaNetSubsystem::DebugFeedPacket`) → делегат → очередь подсистемы → её Tick → инвентарь/окно/подсказка.
//   Разделы:
//     A. полный сброс (режим 0) предмета в СУМКЕ, тело с паддингом сервера (164 б): ячейка — ступень 0, попытки 0, процент 0;
//        подсказка без «+N%»; инфо-панель окна 20200 — «+0»; золото и вес из пакета; разрыва нет;
//     B. восстановление попыток (режим 1): запись цели (+25 = ступень) и два материала — стопка уменьшилась, вторая исчезла;
//     C. предмет НА ПЕРСОНАЖЕ (запись с контейнером 3): надетая ячейка — ступень 0, ячейка сумки с тем же номером не тронута;
//     D. ключ `asda.ItemReset.ApplyResult 0` — прежнее поведение (ступень не меняется);
//     E. отказ (код 5) — ничего не меняется.
//   Запускать на карте мира (`--map world` пакета), только офлайн (-AsdaNoNet). Итог: «СБРОС-УЛУЧШ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "UI/AsdaWindowSubsystem.h"   // ★ R4-b (15.11.2026): снять окна сообщений за собой
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaDisconnectPopupSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemResetSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaItemResetTest, Log, All);

namespace AsdaItemResetSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.ItemReset");   // имя команды (итог, отказ)
	static const TCHAR* kTag = TEXT("СБРОС-УЛУЧШ");                // префикс строк журнала и итога

	// Предметы из живых журналов игроков (ответы 6507 от 19.10): оружие/броня, проходящие фильтр списка окна 20200.
	constexpr int32 kItemA     = 70871;   // полный сброс (Player51, слот 10)
	constexpr int32 kItemB     = 62277;   // восстановление попыток (Player5, слот 1)
	constexpr int32 kMatCode   = 461;     // стопка материала (любой стековый код — приёмник тип не проверяет)
	constexpr int32 kGoldCode  = 20551;   // запись денег в контейнере 2
	// Ячейки, которые самотест занимает (содержимое сохраняется и возвращается в конце).
	constexpr int32 kBagA = 25, kBagB = 26, kBagMat1 = 27, kBagMat2 = 28, kSpendGold = 59, kEquipSlot = 0;

	// Сохранённая ячейка (контейнер, номер, сырые 57 байт; пусто ⇒ ячейка была пуста).
	struct FSaved { int32 Cont = 0; int32 Cell = 0; TArray<uint8> Raw; };

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры
		TWeakObjectPtr<UWorld> World;       // мир прогона
		bool bQuit = false;                 // выйти после итога
		int32 Ok = 0, Fail = 0;             // счёт проверок
		int32 Step = 0;                     // шаг машины состояний
		double StepAt = 0.0;                // время входа в шаг
		TArray<FSaved> Saved;               // занятые ячейки — вернуть в конце
		int32 PrevApply = 1;                // значение ключа до прогона
		bool bModalWas = false;             // ★ R4-b (15.11.2026): модалка была открыта до прогона (тогда окна сообщений не трогать)
	};
	static TSharedPtr<FRun> GRun;

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaItemResetTest, Log, TEXT("%s ok   %s"), kTag, *What); }
		else { ++R.Fail; UE_LOG(LogAsdaItemResetTest, Error, TEXT("%s FAIL %s"), kTag, *What); }
	}

	static UAsdaInventorySubsystem* Inv(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr; }
	static UAsdaNetSubsystem* Net(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
	static UAsdaItemResetSubsystem* Reset(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaItemResetSubsystem>() : nullptr; }

	// Ключ отката подсистемы (только дев-сборка — этот файл в Shipping не собирается).
	static IConsoleVariable* ApplyKey() { return IConsoleManager::Get().FindConsoleVariable(TEXT("asda.ItemReset.ApplyResult")); }

	// Запомнить ячейку до правки (один раз на ячейку).
	static void Save(FRun& R, int32 Cont, int32 Cell)
	{
		for (const FSaved& S : R.Saved) { if (S.Cont == Cont && S.Cell == Cell) { return; } }
		FSaved S; S.Cont = Cont; S.Cell = Cell;
		if (const FAsdaItemInstance* It = Inv(R) ? Inv(R)->GetCell(Cont, Cell) : nullptr)
		{
			if (!It->IsEmpty()) { S.Raw = It->Raw; }
		}
		R.Saved.Add(S);
	}

	// Положить предмет с полями заточки: +24 ступень, +25 исп., +26 макс., +27 процент; Amount — количество (+9).
	static bool Put(FRun& R, int32 Cont, int32 Cell, int32 Code, int32 Amount, int32 Lvl, int32 Used, int32 Max, int32 Pct)
	{
		UAsdaInventorySubsystem* I = Inv(R);
		if (!I) { return false; }
		Save(R, Cont, Cell);
		if (!I->DebugPutItem(Cont, Cell, Code, Amount)) { return false; }
		const FAsdaItemInstance* It = I->GetCell(Cont, Cell);
		if (!It || It->Raw.Num() < FAsdaItemInstance::kRecordSize) { return false; }
		TArray<uint8> Rec = It->Raw;
		Rec[4] = (uint8)Cont;
		AsdaBytes::WrI16(Rec.GetData() + 5, Cell);
		AsdaBytes::WrI32(Rec.GetData() + 9, Amount);
		AsdaBytes::WrI16(Rec.GetData() + 14, FMath::Max(1, Amount));
		Rec[24] = (uint8)Lvl; Rec[25] = (uint8)Used; Rec[26] = (uint8)Max;
		AsdaBytes::WrI16(Rec.GetData() + 27, Pct);
		return I->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}

	// Тело 6507 (154 б + паддинг Pad): код, вес, золото, запись цели (копия ячейки с новыми полями), материалы.
	struct FMat { int32 Cont; int32 Slot; int32 Count; int32 Weight; };
	static TArray<uint8> Body6507(const FRun& R, int32 Code, int32 Weight, int32 Gold, int32 TCont, int32 TCell,
		int32 Lvl, int32 Used, int32 Pct, const TArray<FMat>& Mats, int32 Pad)
	{
		TArray<uint8> B; B.SetNumZeroed(154 + Pad);
		B[0] = (uint8)Code;
		AsdaBytes::WrI16(B.GetData() + 1, Weight);
		AsdaBytes::WrI32(B.GetData() + 3, Gold);
		const FAsdaItemInstance* It = Inv(R) ? Inv(R)->GetCell(TCont, TCell) : nullptr;
		if (It && It->Raw.Num() >= FAsdaItemInstance::kRecordSize)
		{
			FMemory::Memcpy(B.GetData() + 7, It->Raw.GetData(), FAsdaItemInstance::kRecordSize);
		}
		uint8* Rec = B.GetData() + 7;
		Rec[4] = (uint8)TCont;
		AsdaBytes::WrI16(Rec + 5, TCell);
		Rec[24] = (uint8)Lvl; Rec[25] = (uint8)Used;
		AsdaBytes::WrI16(Rec + 27, Pct);
		// Пустые записи материалов — как у сервера (`sub_40A9F0`): контейнер 0, слот −1.
		for (int32 I = 0; I < 10; ++I)
		{
			uint8* M = B.GetData() + 64 + 9 * I;
			M[0] = 0; AsdaBytes::WrI16(M + 1, -1); AsdaBytes::WrI32(M + 3, 0); AsdaBytes::WrI16(M + 7, 0);
		}
		for (int32 I = 0; I < Mats.Num() && I < 10; ++I)
		{
			uint8* M = B.GetData() + 64 + 9 * I;
			M[0] = (uint8)Mats[I].Cont; AsdaBytes::WrI16(M + 1, Mats[I].Slot);
			AsdaBytes::WrI32(M + 3, Mats[I].Count); AsdaBytes::WrI16(M + 7, Mats[I].Weight);
		}
		return B;
	}

	// Поля ячейки для проверок.
	static int32 CellLvl(const FRun& R, int32 C, int32 Cell) { const FAsdaItemInstance* It = Inv(R) ? Inv(R)->GetCell(C, Cell) : nullptr; return (It && It->Raw.Num() > 24) ? It->Raw[24] : -1; }
	static int32 CellUsed(const FRun& R, int32 C, int32 Cell) { const FAsdaItemInstance* It = Inv(R) ? Inv(R)->GetCell(C, Cell) : nullptr; return (It && It->Raw.Num() > 25) ? It->Raw[25] : -1; }
	static int32 CellPct(const FRun& R, int32 C, int32 Cell) { const FAsdaItemInstance* It = Inv(R) ? Inv(R)->GetCell(C, Cell) : nullptr; return (It && It->Raw.Num() > 28) ? AsdaBytes::RdI16(It->Raw.GetData() + 27) : -1; }
	static int32 CellCode(const FRun& R, int32 C, int32 Cell) { const FAsdaItemInstance* It = Inv(R) ? Inv(R)->GetCell(C, Cell) : nullptr; return It ? It->ItemCode : -1; }

	// Заголовок подсказки ячейки (тот же построитель, что у наведения: `BuildTooltip`).
	static FString TipTitle(const FRun& R, int32 C, int32 Cell)
	{
		const FAsdaItemInstance* It = Inv(R) ? Inv(R)->GetCell(C, Cell) : nullptr;
		const UAsdaItemTooltipSubsystem* Tip = R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		return (It && Tip) ? Tip->BuildTooltip(*It).Title : FString(TEXT("<нет>"));
	}

	// Номер позиции окна 20200 для ячейки сумки (фильтр списка — как `uiInventoryCraftListRefresh`).
	static int32 ListIndexOf(const FRun& R, int32 BagCell)
	{
		const UAsdaInventorySubsystem* I = Inv(R);
		const UAsdaItemTableSubsystem* IT = R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!I || !IT) { return -1; }
		int32 Vis = 0;
		for (int32 C = 0; C < FMath::Min(I->GetContainerSize(1), UAsdaItemResetSubsystem::kListMax); ++C)   // ★ 29.10.2026 (инвентарь ×2): список — вся сумка
		{
			const FAsdaItemInstance* It = I->GetCell(1, C);
			if (!It || It->IsEmpty()) { continue; }
			const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
			if (!Rec || Rec->ItemSubType == 66 || Rec->ItemSubType == 200 || Rec->ItemSubType == 201) { continue; }
			if (C == BagCell) { return Vis; }
			++Vis;
		}
		return -1;
	}

	// «Разрыва нет»: попап обрыва не показан, мир тот же, приёмник не взвёл отключение.
	static void ExpectNoDisconnect(FRun& R, const TCHAR* Sec)
	{
		const UAsdaDisconnectPopupSubsystem* Pop = R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaDisconnectPopupSubsystem>() : nullptr;
		const UWorld* W = R.GI.IsValid() ? R.GI->GetWorld() : nullptr;
		Expect(R, (!Pop || !Pop->IsOpen()) && W && W == R.World.Get() && !AsdaWorldKind::IsLoginScene(W),
			FString::Printf(TEXT("%s: разрыва нет — попап обрыва не показан, мир прежний"), Sec));
	}

	// Вернуть ячейки и ключ, закрыть окно.
	static void Restore(FRun& R)
	{
		UAsdaInventorySubsystem* I = Inv(R);
		for (int32 K = R.Saved.Num() - 1; K >= 0; --K)
		{
			const FSaved& S = R.Saved[K];
			if (!I) { break; }
			if (S.Raw.Num() >= FAsdaItemInstance::kRecordSize) { I->ApplyExternalRecord(S.Raw, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
			else { I->ClearCell(S.Cont, S.Cell); }
		}
		if (IConsoleVariable* K = ApplyKey()) { K->Set(R.PrevApply, ECVF_SetByConsole); }
		if (UAsdaItemResetSubsystem* Rs = Reset(R)) { Rs->CloseWindow(); }
		// ★ R4-b (15.11.2026): ответ 6507 оставлял ONEBUT_MSGBOX — сосед по пачке (`asda.SelfTest.NpcPlace`) упирался в открытую модалку.
		if (!R.bModalWas && R.GI.IsValid()) { if (UAsdaWindowSubsystem* WM = R.GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(kCmd); } }
	}

	static bool Finish(FRun& R)
	{
		Restore(R);
		AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaItemResetTest, kTag, R.Ok, R.Fail);
		GRun.Reset();
		return false;
	}

	// Шаги: нечётные — «подать и ждать», чётные — «проверить». Пауза между ними — 0,4 с (Tick подсистемы успеет).
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		const double Now = FPlatformTime::Seconds();
		if (Now - R.StepAt < 0.4) { return true; }
		UAsdaNetSubsystem* N = Net(R);
		UAsdaInventorySubsystem* I = Inv(R);
		UAsdaItemResetSubsystem* Rs = Reset(R);
		if (!N || !I || !Rs || !R.World.IsValid()) { Expect(R, false, TEXT("подсистемы/мир пропали посреди прогона")); return Finish(R); }
		R.StepAt = Now;
		switch (R.Step++)
		{
		case 0:   // A: окно режима 0, предмет +7 (исп. 7/8, 35 %) в сумке, выбрать его в списке
		{
			const bool bPut = Put(R, 1, kBagA, kItemA, 0, 7, 7, 8, 35) && Put(R, 2, kSpendGold, kGoldCode, 1000, 0, 0, 0, 0);
			Expect(R, bPut && CellLvl(R, 1, kBagA) == 7, TEXT("A0: в сумке предмет +7 (исп. 7 из 8, 35 %)"));
			Expect(R, TipTitle(R, 1, kBagA).Contains(TEXT("+35%")), FString::Printf(TEXT("A0: подсказка до сброса с «+35%%» — «%s»"), *TipTitle(R, 1, kBagA)));
			Rs->OpenWindow(UAsdaItemResetSubsystem::kModeFullReset);
			return true;
		}
		case 1:
		{
			const int32 Li = ListIndexOf(R, kBagA);
			Expect(R, Rs->IsOpen() && Li >= 0, FString::Printf(TEXT("A1: окно 20200 открыто, предмет в списке на позиции %d"), Li));
			if (Li >= 0) { Rs->OnListCellClicked(Li, false); }
			Expect(R, Rs->DebugInfoPanel().StartsWith(TEXT("ур=+7→+0")), FString::Printf(TEXT("A1: инфо-панель до сброса — %s"), *Rs->DebugInfoPanel()));
			// Тело с паддингом сервера (164 б, как в журнале 19.10) — приёмник обязан взять первые 154.
			N->DebugFeedPacket(6507, Body6507(R, 1, 4113, 49653747, 1, kBagA, 0, 0, 0, {}, 10));
			return true;
		}
		case 2:
		{
			Expect(R, CellCode(R, 1, kBagA) == kItemA && CellLvl(R, 1, kBagA) == 0 && CellUsed(R, 1, kBagA) == 0 && CellPct(R, 1, kBagA) == 0,
				FString::Printf(TEXT("A2: ячейка сумки после 6507 — ступень %d, исп. %d, процент %d (ждём 0/0/0)"), CellLvl(R, 1, kBagA), CellUsed(R, 1, kBagA), CellPct(R, 1, kBagA)));
			const FString T = TipTitle(R, 1, kBagA);
			Expect(R, !T.Contains(TEXT("%")) && !T.IsEmpty(), FString::Printf(TEXT("A2: подсказка без «+N%%» — «%s»"), *T));
			Expect(R, Rs->DebugInfoPanel().StartsWith(TEXT("ур=+0→+0 исп=0→0")), FString::Printf(TEXT("A2: инфо-панель окна — %s"), *Rs->DebugInfoPanel()));
			Expect(R, I->GetGold() == 49653747, FString::Printf(TEXT("A2: золото из пакета — %d"), I->GetGold()));
			Expect(R, N->SelfWeightCur == 4113, FString::Printf(TEXT("A2: вес из пакета — %d"), N->SelfWeightCur));
			ExpectNoDisconnect(R, TEXT("A2"));
			// B: режим 1 — предмет +6 (исп. 7 из 7, 48 %), материалы: стопка 12 и стопка 1.
			const bool bPut = Put(R, 1, kBagB, kItemB, 0, 6, 7, 7, 48) && Put(R, 1, kBagMat1, kMatCode, 12, 0, 0, 0, 0)
				&& Put(R, 1, kBagMat2, kMatCode, 1, 0, 0, 0, 0);
			Expect(R, bPut, TEXT("B0: цель +6 (7/7) и две стопки материала положены"));
			N->DebugFeedPacket(6507, Body6507(R, 1, 5296, 49100024, 1, kBagB, 6, 6, 48,
				{ { 1, kBagMat1, 11, 11 }, { 1, kBagMat2, 0, 0 } }, 16));
			return true;
		}
		case 3:
		{
			Expect(R, CellLvl(R, 1, kBagB) == 6 && CellUsed(R, 1, kBagB) == 6,
				FString::Printf(TEXT("B1: цель — ступень %d, исп. %d (ждём 6/6: попытка возвращена)"), CellLvl(R, 1, kBagB), CellUsed(R, 1, kBagB)));
			const FAsdaItemInstance* M1 = I->GetCell(1, kBagMat1);
			Expect(R, M1 && M1->ItemCode == kMatCode && M1->Amount == 11 && M1->StackWeight == 11,
				FString::Printf(TEXT("B1: стопка материала 12 → %d (вес %d)"), M1 ? M1->Amount : -1, M1 ? M1->StackWeight : -1));
			Expect(R, CellCode(R, 1, kBagMat2) <= 0, TEXT("B1: израсходованная стопка (количество 0) убрана из ячейки"));
			ExpectNoDisconnect(R, TEXT("B1"));
			// C: предмет НА ПЕРСОНАЖЕ (контейнер 3) +9; в сумке в ячейке с тем же номером — свой предмет.
			const bool bPut = Put(R, 3, kEquipSlot, kItemA, 0, 9, 9, 9, 60) && Put(R, 1, kEquipSlot, kItemB, 0, 4, 4, 7, 20);
			Expect(R, bPut && CellLvl(R, 3, kEquipSlot) == 9, TEXT("C0: надет предмет +9, в сумке в ячейке 0 — свой +4"));
			N->DebugFeedPacket(6507, Body6507(R, 1, 4200, 49000000, 3, kEquipSlot, 0, 0, 0, {}, 0));
			return true;
		}
		case 4:
		{
			Expect(R, CellLvl(R, 3, kEquipSlot) == 0 && CellPct(R, 3, kEquipSlot) == 0,
				FString::Printf(TEXT("C1: надетая ячейка — ступень %d, процент %d (ждём 0/0)"), CellLvl(R, 3, kEquipSlot), CellPct(R, 3, kEquipSlot)));
			const FString T = TipTitle(R, 3, kEquipSlot);
			Expect(R, !T.Contains(TEXT("%")), FString::Printf(TEXT("C1: подсказка надетого без «+N%%» — «%s»"), *T));
			Expect(R, CellCode(R, 1, kEquipSlot) == kItemB && CellLvl(R, 1, kEquipSlot) == 4, TEXT("C1: ячейка сумки с тем же номером не тронута (+4)"));
			ExpectNoDisconnect(R, TEXT("C1"));
			// D: ключ 0 — прежнее поведение.
			if (IConsoleVariable* K = ApplyKey()) { K->Set(0, ECVF_SetByConsole); }
			Put(R, 1, kBagA, kItemA, 0, 5, 5, 8, 25);
			N->DebugFeedPacket(6507, Body6507(R, 1, 4113, 1, 1, kBagA, 0, 0, 0, {}, 0));
			return true;
		}
		case 5:
		{
			Expect(R, CellLvl(R, 1, kBagA) == 5, FString::Printf(TEXT("D1: ключ 0 — ступень осталась %d (прежнее поведение)"), CellLvl(R, 1, kBagA)));
			if (IConsoleVariable* K = ApplyKey()) { K->Set(1, ECVF_SetByConsole); }
			// E: отказ код 5 — у отказа сервер кладёт в @7 только код предмета; ничего не меняется.
			TArray<uint8> B; B.SetNumZeroed(154); B[0] = 5; AsdaBytes::WrI32(B.GetData() + 7, kItemA);
			N->DebugFeedPacket(6507, B);
			return true;
		}
		case 6:
		{
			Expect(R, CellLvl(R, 1, kBagA) == 5 && CellCode(R, 1, kBagA) == kItemA, TEXT("E1: отказ (код 5) — предмет не тронут"));
			ExpectNoDisconnect(R, TEXT("E1"));
			return Finish(R);
		}
		default:
			return Finish(R);
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaWorldKind::IsLoginScene(W)) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("запускать в мире, не на сцене входа")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		if (!IT || !IT->GetItem(kItemA) || !IT->GetItem(kItemB))
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет предметов 70871/62277 в таблице"));
			return;
		}
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.World = W; R.bQuit = Args.bQuit; R.StepAt = 0.0;
		if (const UAsdaWindowSubsystem* WM0 = GI->GetSubsystem<UAsdaWindowSubsystem>()) { R.bModalWas = WM0->IsAnyModalOpen(); }   // ★ R4-b (15.11.2026)
		if (IConsoleVariable* K = ApplyKey()) { R.PrevApply = K->GetInt(); K->Set(1, ECVF_SetByConsole); }
		Expect(R, ApplyKey() != nullptr, TEXT("0: ключ asda.ItemReset.ApplyResult зарегистрирован"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestItemReset(
	TEXT("asda.SelfTest.ItemReset"),
	TEXT("Самотест: ответ 6507 сброса/восстановления улучшений (окно 20200) применяется к предмету в сумке и на персонаже — "
	     "ступень 0 в ячейке, подсказке и инфо-панели, золото/вес/свитки из пакета, разрыва нет; ключ 0 — прежнее поведение. "
	     "Запускать в мире, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaItemResetSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
