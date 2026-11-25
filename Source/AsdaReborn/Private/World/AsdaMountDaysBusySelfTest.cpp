// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ТРАНСПОРТ: СРОК И БОЙ» (08.11.2026, пачка 16, баги 1 и 3): `asda.SelfTest.MountDaysBusy [out=<папка>] [quit]`.
//   Баг 1 «двойной щелчок по транспорту не показывает, сколько дней осталось» — ключ `asda.Mount.DaysNotice`
//   (`UAsdaInventorySubsystem::ApplyFunctionResult`, эталон `netRecvItemBuffApply@0xF23C60` case 14: loc 20230 / 13227, плашка).
//   Баг 3 «садится на транспорт во время боя» — ключ `asda.Mount.BusyGate` (`AsdaPremiumUseBusyBlocked`, эталон
//   `objStateAllowsRender` + `inventoryItemIsBoosterCategory` в обоих входах; состояние 7/8/9/40/43 в `itemUseDispatch@0xA105A0`).
//   Офлайн, путём игрока: двойной щелчок из окна сумки (`UseFromBagWindow`), слот панели быстрого доступа (`ActivateSlot`),
//   исходящие — перехват, ответ 5451 — штатный разбор `DebugFeedPacket` и очередь инвентаря.
//   Ветки:
//     A) в бою (автоатака) двойной щелчок по транспорту — 5450 нет, плашка 1157 (ключ `asda.Mount.BusyNotice`, решение владельца
//        08.11 [ИСПРАВЛЕНИЕ]); A3 — повтор: плашка снова; B3 — верхом в бою «слезть»: 5455 нет, плашка 1157; B4 — ключ 0: молча;
//     B) в бою слот панели с транспортом — 5450 нет;
//     C) в бою зелье категории «бустер» (подтипы 42-45/47/52/53) — 5450 ушёл (эталон пропускает);
//     D) сидит (состояние 7/8), не в бою — транспорт: 5450 нет;
//     E) покой — транспорт: 5450 ушёл;
//     F) ключ `asda.Mount.BusyGate` 0, в бою — 5450 ушёл (воспроизведение бага);
//     G) 5451 успех транспорта, 100 ч — плашка 20230 с «5»; 10 ч — плашка 13227; −1 ч — нотиса нет; ключ DaysNotice 0 — нотиса нет.
//   Итог: «ТРАНСПОРТ-СРОК-БОЙ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — отложенный старт   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"   // «в бою» = IsLocalPlayerBusy
#include "UI/AsdaItemRecordWriter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMountDaysTest, Log, All);

namespace AsdaMountDaysTest
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.MountDaysBusy");   // имя команды
	const TCHAR* const kPrefix = TEXT("ТРАНСПОРТ-СРОК-БОЙ");         // префикс строк
	constexpr int32 kSelfFallback = 4324;   // свой номер, если не известен
	constexpr int32 kMount = 522;           // транспорт (SubType 15)
	constexpr int32 kMountCell = 8;         // ячейка сумки транспорта
	constexpr int32 kPotionCell = 9;        // ячейка сумки зелья
	constexpr int32 kHotbarSlot = 11;       // слот панели быстрого доступа

	// Ловит плашки (`LogAsdaMsg`, приёмник WARMESSAGE221 — верх по центру экрана).
	struct FTap : public FOutputDevice
	{
		TArray<FString> Planks;   // тексты плашек
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Cat) override
		{
			if (Cat != FName(TEXT("LogAsdaMsg"))) { return; }
			const FString L(V);
			if (!L.Contains(TEXT("WARMESSAGE221"))) { return; }   // приёмник плашки (эталон `CControl_SetTooltipText(g_pChatWnd…)`)
			int32 A = INDEX_NONE, B = INDEX_NONE;
			if (L.FindChar(TEXT('«'), A) && L.FindLastChar(TEXT('»'), B) && B > A) { Planks.Add(L.Mid(A + 1, B - A - 1)); }
			else { Planks.Add(L); }
		}
	};

	struct FStep { FString Name; float TimeoutSec = 10.f; TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check; };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		FTap Tap;
		bool bWasInGame = false;
		int32 Self = -1, Potion = 0, N5450 = 0, N5455 = 0, Last5450Code = 0, Mark = 0, Mark2 = 0, PlankMark = 0;
		FString Want1157;   // текст loc 1157 на языке сессии
		FAsdaHotbarSlot SavedSlot;
		FString OutDir;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaInventorySubsystem* Inv() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaInventorySubsystem>() : nullptr; }
		UAsdaHotbarSubsystem* Hb() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			const TCHAR* Nm = Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?");
			if (bOk) { UE_LOG(LogAsdaMountDaysTest, Log, TEXT("%s ok   %s: %s"), kPrefix, Nm, *What); }
			else     { UE_LOG(LogAsdaMountDaysTest, Error, TEXT("%s FAIL %s: %s"), kPrefix, Nm, *What); }
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		// «В бою»: автоатака по цели — то, что `IsLocalPlayerBusy` (порт `objStateAllowsRender`) читает.
		//   Ставить ПРЯМО перед действием: тик контроллера снимает автоатаку без цели.
		void Busy(bool b)
		{
			if (AAsdaPlayerController* P = PC()) { P->bAutoAttack = b; P->bCombatStarted = b; if (!b) { P->bInCombatStance = false; } }
		}
		bool IsBusy() const
		{
			UGameInstance* G = GI(); const UAsdaSystemOptionSubsystem* So = G ? G->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr;
			return So && So->IsLocalPlayerBusy();
		}
		void Put(int32 Code, int32 Cell)
		{
			if (UAsdaInventorySubsystem* I = Inv())
			{
				I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, 1, Cell, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
		}
		void Clear(int32 Cell)
		{
			if (UAsdaInventorySubsystem* I = Inv())
			{
				I->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, Cell), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
		}
		// Плашек с текстом loc 1157 после отметки PlankMark.
		int32 Planks1157() const
		{
			int32 N = 0;
			for (int32 i = PlankMark; i < Tap.Planks.Num(); ++i) { if (!Want1157.IsEmpty() && Tap.Planks[i] == Want1157) { ++N; } }
			return N;
		}
		// 5453 (10 б): свой userIdx, код транспорта, длительность −1 — штатный разбор «сел»; 5454 (6 б) — «слез».
		void Mount()
		{
			TArray<uint8> B; B.SetNumZeroed(10);
			AsdaBytes::WrI16(&B[0], (int16)Self); AsdaBytes::WrI32(&B[2], kMount); AsdaBytes::WrI32(&B[6], -1);
			Feed(5453, B);
		}
		void Dismount()
		{
			TArray<uint8> B; B.SetNumZeroed(6);
			AsdaBytes::WrI16(&B[0], (int16)Self); AsdaBytes::WrI32(&B[2], kMount);
			Feed(5454, B);
		}
		void DoubleClick(int32 Cell) { if (UAsdaInventorySubsystem* I = Inv()) { I->UseFromBagWindow(1, Cell); } }
		// 5451 (68 б): итог 1, userNum@1, запись 57 б @3, часы @62.
		void Feed5451(int32 Hours)
		{
			TArray<uint8> B; B.SetNumZeroed(68);
			B[0] = 1; AsdaBytes::WrI16(&B[1], (int16)Self);
			const TArray<uint8> Rec = FAsdaItemRecordWriter::Cell(kMount, 1, kMountCell, 1);
			FMemory::Memcpy(&B[3], Rec.GetData(), FMath::Min(57, Rec.Num()));
			AsdaBytes::WrI32(&B[62], Hours);
			Feed(5451, B);
			if (UAsdaInventorySubsystem* I = Inv()) { I->DebugPumpPendingPackets(); }
		}
		void Shot(const TCHAR* Tag)
		{
			if (OutDir.IsEmpty()) { return; }
			FScreenshotRequest::RequestScreenshot(OutDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		}
	};

	// Премиум-зелье категории «бустер» эталона: подтип 42-45/47/52/53, ветка `Flag102 == 1` (иначе 5450 не уходит и в покое).
	static int32 FindBoosterPotion(const UAsdaItemTableSubsystem* IT)
	{
		if (!IT) { return 0; }
		for (int32 Code = 1; Code < 2000; ++Code)
		{
			FAsdaSpecialItemlistRow Sp;
			if (!IT->GetSpecialItem(Code, Sp)) { continue; }
			const int32 St = Sp.SubType;
			const bool bCat = St == 42 || St == 43 || St == 44 || St == 45 || St == 47 || St == 52 || St == 53;
			if (bCat && AsdaBytes::HexByteAt(Sp.DataHex, 101) != 2 && AsdaBytes::HexByteAt(Sp.DataHex, 102) == 1) { return Code; }
		}
		return 0;
	}
}

struct FAsdaMountDaysBusySelfTest { static void Run(const TArray<FString>& Args, UWorld* W); };

void FAsdaMountDaysBusySelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaMountDaysTest;
	const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 5450, 5455, 5067 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KA.bQuit; S->Gate.bQuit = KA.bQuit;
	for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { S->OutDir = A.Mid(4); } }
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};

	// ── подготовка ──
	Add(TEXT("0 подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* P = S->PC();
		if (!C || !P || !S->Inv() || !S->Hb()) { S->Expect(false, TEXT("нет своего персонажа/инвентаря/панели")); return; }
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* Pl, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin(); if (!St) { return; }
			if (Op == 5450) { ++St->N5450; }
			if (Op == 5455) { ++St->N5455; }
		}, (uint16)0, kCmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		const UAsdaItemTableSubsystem* IT = S->GI()->GetSubsystem<UAsdaItemTableSubsystem>();
		FAsdaSpecialItemlistRow Sp;
		S->Expect(IT && IT->GetSpecialItem(kMount, Sp) && Sp.SubType == 15, FString::Printf(TEXT("предмет %d — транспорт (подтип 15)"), kMount));
		S->Potion = FindBoosterPotion(IT);
		S->Expect(S->Potion > 0, FString::Printf(TEXT("зелье категории «бустер»: %d"), S->Potion));
		S->Put(kMount, kMountCell); S->Put(S->Potion, kPotionCell);
		S->SavedSlot = S->Hb()->GetSlot(kHotbarSlot);
		S->Hb()->SetSlotContent(kHotbarSlot, (uint8)EAsdaHotbarSlotType::Item, (uint8)EAsdaHotbarContainer::Bag, kMountCell, 1, kMount);
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.BusyGate"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.DaysNotice"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.BusyNotice"), 1);
		// Запрет «плывёт» (`asda.Swim.ActionGate`) — не предмет этого теста: в пакете после самотестов воды персонаж может стоять в воде.
		AsdaCVarSave::SetForTest(TEXT("asda.Swim.ActionGate"), 0);
		if (UAsdaTextSubsystem* Tx = S->GI()->GetSubsystem<UAsdaTextSubsystem>()) { S->Want1157 = Tx->FormatNotice(1157); }
		S->Expect(!S->Want1157.IsEmpty(), FString::Printf(TEXT("текст loc 1157: «%s»"), *S->Want1157));
		GLog->AddOutputDevice(&S->Tap);
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		const FAsdaHotbarSlot& Sl = S->Hb()->GetSlot(kHotbarSlot);
		S->Expect(Sl.Id == kMount, TEXT("транспорт лежит в слоте панели"));
	});

	// ── A. В бою двойной щелчок по транспорту ──
	Add(TEXT("A бой: двойной щелчок"), 3.f, [S]()
	{
		S->Busy(true); S->Mark = S->N5450; S->PlankMark = S->Tap.Planks.Num();
		S->Expect(S->IsBusy(), TEXT("A0 персонаж «в бою» (IsLocalPlayerBusy)"));
		S->DoubleClick(kMountCell);
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->Expect(S->N5450 == S->Mark, FString::Printf(TEXT("A1 5450 не ушёл (+%d), как эталон"), S->N5450 - S->Mark));
		S->Expect(S->Tap.Planks.Num() == S->PlankMark + 1 && S->Planks1157() == 1,
			FString::Printf(TEXT("A2 [ИСПРАВЛЕНИЕ] плашка 1157 «%s» показана один раз (плашек %d)"), *S->Want1157, S->Tap.Planks.Num() - S->PlankMark));
		S->Shot(TEXT("A_busy_dblclick"));
	});
	Add(TEXT("A3 бой: повторный щелчок"), 3.f, [S]()
	{
		S->Busy(true); S->Mark = S->N5450; S->PlankMark = S->Tap.Planks.Num();
		S->DoubleClick(kMountCell);
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->Expect(S->N5450 == S->Mark && S->Planks1157() == 1,
			FString::Printf(TEXT("A3 повтор: 5450 нет, плашка 1157 снова (%d) — как оригинал повторяет 1157 на каждый отказ"), S->Planks1157()));
	});

	// ── B. В бою слот панели ──
	Add(TEXT("B бой: слот панели"), 3.f, [S]()
	{
		S->Busy(true); S->Mark = S->N5450; S->PlankMark = S->Tap.Planks.Num();
		S->Expect(S->IsBusy(), TEXT("B0 персонаж «в бою» (IsLocalPlayerBusy)"));
		S->Hb()->ActivateSlot(kHotbarSlot);
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->Expect(S->N5450 == S->Mark, FString::Printf(TEXT("B1 5450 не ушёл (+%d), как эталон"), S->N5450 - S->Mark));
		S->Expect(S->Planks1157() == 1, FString::Printf(TEXT("B2 слот панели: плашка 1157 (%d)"), S->Planks1157()));
	});

	// ── B3. Верхом в бою: «слезть» отклонено, плашка 1157 ──
	Add(TEXT("B3 бой: слезть"), 3.f, [S]()
	{
		S->Mount();
	}, [S]() { return S->El() > 0.4; }, [S]()
	{
		S->Busy(true); S->Mark2 = S->N5455; S->PlankMark = S->Tap.Planks.Num();
		S->DoubleClick(kMountCell);
		S->Expect(S->N5455 == S->Mark2, FString::Printf(TEXT("B3a верхом в бою 5455 не ушёл (+%d), как эталон"), S->N5455 - S->Mark2));
		S->Expect(S->Planks1157() == 1, FString::Printf(TEXT("B3b «слезть» в бою: плашка 1157 (%d)"), S->Planks1157()));
		S->Busy(false);
		S->Dismount();
	});

	// ── B4. Ключ BusyNotice 0: отказ в бою молча (как оригинал) ──
	Add(TEXT("B4 ключ BusyNotice 0"), 3.f, [S]()
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.BusyNotice"), 0);
		S->Busy(true); S->Mark = S->N5450; S->PlankMark = S->Tap.Planks.Num();
		S->DoubleClick(kMountCell);
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->Expect(S->N5450 == S->Mark && S->Tap.Planks.Num() == S->PlankMark, TEXT("B4 ключ 0: 5450 нет, плашки нет (оригинал молчит)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.BusyNotice"), 1);
		S->Busy(false);
	});

	// ── C. В бою зелье категории «бустер» ──
	Add(TEXT("C бой: зелье"), 3.f, [S]()
	{
		S->Busy(true); S->Mark = S->N5450;
		S->Expect(S->IsBusy(), TEXT("C0 персонаж «в бою» (IsLocalPlayerBusy)"));
		S->DoubleClick(kPotionCell);
	}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->Expect(S->N5450 == S->Mark + 1, FString::Printf(TEXT("C1 зелье %d в бою применено: 5450 +%d (эталон пропускает подтипы 42-45/47/52/53)"), S->Potion, S->N5450 - S->Mark));
		S->Busy(false);
	});

	// ── D. Сидит, не в бою ──
	Add(TEXT("D сидит"), 6.f, [S]()
	{
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->SetAnimState(EAsdaPlayerState::Sit); }
	}, [S]()
	{
		const AAsdaPlayerCharacter* C = S->Ch();   // 3.4 с — вне троттла эталона после прошлых посадок
		return S->El() > 3.4 && C && C->GetEtalonSitState() != 0;
	}, [S]()
	{
		const AAsdaPlayerCharacter* Cs = S->Ch();
		const int32 Sit = Cs ? Cs->GetEtalonSitState() : -1;   // та же точка, из которой `KbEtalonActionState` берёт 7/8/9
		S->Expect(Sit == 7 || Sit == 8, FString::Printf(TEXT("D0 состояние действия %d (7/8)"), Sit));
		S->Mark = S->N5450; S->PlankMark = S->Tap.Planks.Num();
		S->DoubleClick(kMountCell);
		S->Expect(S->N5450 == S->Mark, FString::Printf(TEXT("D1 сидя транспорт не вызван: 5450 +%d (эталон: состояние 7/8/9)"), S->N5450 - S->Mark));
		S->Expect(S->Tap.Planks.Num() == S->PlankMark, TEXT("D2 сидя: плашки нет (у оригинала строки нет — молча)"));
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->SetAnimState(EAsdaPlayerState::Idle); }
	});

	// ── E. Покой: транспорт вызывается (троттл эталона 3 с после D) ──
	Add(TEXT("E покой"), 6.f, [S]() {}, [S]()
	{
		const AAsdaPlayerCharacter* C = S->Ch();
		return S->El() > 3.4 && C && C->GetEtalonSitState() == 0;
	}, [S]()
	{
		S->Mark = S->N5450;
		S->DoubleClick(kMountCell);
		S->Expect(S->N5450 == S->Mark + 1, FString::Printf(TEXT("E1 в покое транспорт вызван: 5450 +%d"), S->N5450 - S->Mark));
	});

	// ── F. Ключ 0: прежний порт, в бою транспорт вызывается (воспроизведение бага) ──
	Add(TEXT("F ключ 0"), 6.f, [S]() {}, [S]() { return S->El() > 3.4; }, [S]()
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.BusyGate"), 0);
		S->Busy(true); S->Mark = S->N5450;
		S->Expect(S->IsBusy(), TEXT("F0 персонаж «в бою» (IsLocalPlayerBusy)"));
		S->DoubleClick(kMountCell);
		S->Expect(S->N5450 == S->Mark + 1, FString::Printf(TEXT("F1 ключ 0: в бою 5450 ушёл (+%d) — так было до правки"), S->N5450 - S->Mark));
		S->Busy(false);
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.BusyGate"), 1);
	});

	// ── G. Нотис срока на 5451 ──
	Add(TEXT("G1 100 часов"), 3.f, [S]()
	{
		S->PlankMark = S->Tap.Planks.Num();
		S->Feed5451(100);
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		const int32 Got = S->Tap.Planks.Num() - S->PlankMark;
		const FString Txt = Got > 0 ? S->Tap.Planks.Last() : FString();
		S->Expect(Got == 1, FString::Printf(TEXT("G1a одна плашка (%d): «%s»"), Got, *Txt));
		S->Expect(Txt.Contains(TEXT("5")) && !Txt.Contains(TEXT("[sd1]")), TEXT("G1b в тексте 5 дней (100 ч = 4 сут 4 ч → 5), маркер подставлен"));
		UAsdaTextSubsystem* T = S->GI()->GetSubsystem<UAsdaTextSubsystem>();
		FString Want; if (T) { T->NoticeSetInt(0, 5); Want = T->FormatNotice(20230); }
		S->Expect(!Want.IsEmpty() && Txt == Want, FString::Printf(TEXT("G1c текст = loc 20230 «%s»"), *Want));
		S->Shot(TEXT("G1_days_100h"));
	});
	Add(TEXT("G2 10 часов"), 3.f, [S]()
	{
		S->PlankMark = S->Tap.Planks.Num();
		S->Feed5451(10);
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		const int32 Got = S->Tap.Planks.Num() - S->PlankMark;
		const FString Txt = Got > 0 ? S->Tap.Planks.Last() : FString();
		UAsdaTextSubsystem* T = S->GI()->GetSubsystem<UAsdaTextSubsystem>();
		const FString Want = T ? T->FormatNotice(13227) : FString();
		S->Expect(Got == 1 && !Want.IsEmpty() && Txt == Want, FString::Printf(TEXT("G2 плашка 13227 «%s» (получено %d: «%s»)"), *Want, Got, *Txt));
		S->Shot(TEXT("G2_today_10h"));
	});
	Add(TEXT("G3 бессрочный"), 3.f, [S]()
	{
		S->PlankMark = S->Tap.Planks.Num();
		S->Feed5451(-1);
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		S->Expect(S->Tap.Planks.Num() == S->PlankMark, TEXT("G3 −1 ч: нотиса нет (как эталон)"));
	});
	Add(TEXT("G4 ключ 0"), 3.f, [S]()
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.DaysNotice"), 0);
		S->PlankMark = S->Tap.Planks.Num();
		S->Feed5451(100);
	}, [S]() { return S->El() > 0.6; }, [S]()
	{
		S->Expect(S->Tap.Planks.Num() == S->PlankMark, TEXT("G4 ключ DaysNotice 0: нотиса нет (так было до правки)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Mount.DaysNotice"), 1);
	});

	// ── K. Кадр плашки 1157 (в начале прогона её перекрывает баннер названия зоны) ──
	Add(TEXT("K кадр 1157"), 4.f, [S]()
	{
		S->Busy(true); S->PlankMark = S->Tap.Planks.Num();
		S->DoubleClick(kMountCell);
	}, [S]() { return S->El() > 0.8; }, [S]()
	{
		S->Expect(S->Planks1157() == 1, FString::Printf(TEXT("K1 плашка 1157 для кадра (%d)"), S->Planks1157()));
		S->Shot(TEXT("K_busy_1157"));
		S->Busy(false);
	});
	Add(TEXT("K2 ожидание кадра"), 2.f, [S]() {}, [S]() { return S->El() > 0.6; }, [S]() {});

	// ── итог ──
	Add(TEXT("итог"), 3.f, [S]()
	{
		S->Busy(false);
		if (UAsdaHotbarSubsystem* H = S->Hb())
		{
			const FAsdaHotbarSlot& O = S->SavedSlot;
			H->SetSlotContent(kHotbarSlot, O.Type, O.SubType, O.Index, O.Count, O.Id);
		}
		S->Clear(kMountCell); S->Clear(kPotionCell);
	}, [S]() { return S->El() > 0.5; }, [S, Net]()
	{
		GLog->RemoveOutputDevice(&S->Tap);
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaMountDaysTest, kPrefix, S->Ok, S->Fail);
	});

	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H; TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get(); const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KA.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		if (!St->Steps.IsValidIndex(St->StepIdx)) { Wd->GetTimerManager().ClearTimer(St->Tick); AsdaSelfTestKit::Quit(Wd, St->bQuit); return; }
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaMountDaysTest, Log, TEXT("%s шаг %s"), kPrefix, *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KA.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMountDaysBusy(
	TEXT("asda.SelfTest.MountDaysBusy"),
	TEXT("Самотест транспорта: нотис срока 20230/13227 на 5451 (asda.Mount.DaysNotice) и запрет вызова в бою/сидя (asda.Mount.BusyGate). ")
	TEXT("Только офлайн. `out=<папка>` `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.SelfTest.MountDaysBusy"));
		FAsdaMountDaysBusySelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
