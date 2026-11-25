// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «ЦИФРЫ В НАДЕТЫХ ЯЧЕЙКАХ» ПУТЁМ ИГРОКА (31.10.2026, баг 10 владельца). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Эталон `itemUI_DrawEquipSlotIcon@0xC6B1B0` (item_inventory.c:22815-22882) печатает в контрол надетого слота
//   «+N%» заточки (гейт запись+24, значение i16 запись+27) и в слоте 10 — количество стрел (запись+9). Тест надевает
//   (запись контейнера 3 штатной точкой `ApplyExternalRecord`) оружие, кирасу и кольцо с заточкой, оружие без заточки и колчан
//   с количеством, открывает сумку, читает ЖИВЫЕ узлы `EQUIP_SET{n}` окна `WBP_INVEN_WIN` и снимает кадр. Затем:
//   смена количества стрел (как после выстрела), смена процента заточки (как перенос улучшения), снятие вещи, ключ = 0.
//
// Консоль: `asda.SelfTest.EquipCellText [tag=after] [legacy] [delay=N] [quit]`. Только офлайн (-AsdaNoNet).
//   `legacy` — ключ `asda.Inven.EquipCellText 0` (прежний порт): кадр «до», ждём пустые надписи.
//   Кадры: `Reports/equip_cell_text/<tag>_<шаг>.png`. Строки — «САМОТЕСТ-НАДЕТОЕ», итог — «САМОТЕСТ-НАДЕТОЕ ИТОГ».
//   Надетое (контейнер 3) и ключи возвращаются в конце.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaArrowAmmoSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "UI/AsdaNotice.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEquipTextTest, Log, All);

namespace AsdaEquipTextTestLocal
{
	// Надетая вещь набора: слот контейнера 3, тип предмета (== слот), количество, процент заточки (0 — без гейта).
	struct FWear { int32 Slot; int32 Amount; int32 Percent; const TCHAR* Why; };
	static const FWear kWear[] = {
		{ 9,  0,   86, TEXT("оружие +86%") },
		{ 1,  0,   24, TEXT("кираса +24%") },
		{ 5,  0,   7,  TEXT("кольцо +7%") },
		{ 3,  0,   0,  TEXT("ботинки без заточки — текста нет") },
		{ 10, 250, 0,  TEXT("колчан ×250") },
	};

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		bool bLegacy = false;
		float Delay = 0.f;
		FString Tag = TEXT("after");
		FString Dir;
		int32 Ok = 0, Fail = 0;
		TArray<TFunction<float()>> Steps;   // шаг возвращает паузу до следующего, с; < 0 — снимок (ждать кадра)
		int32 Index = 0;
		double NextTime = 0.0;
		bool bAwaitShot = false;
		int32 FramesAfterShot = 0;
		TArray<TArray<uint8>> Saved3;          // надетое до теста (сырые записи; пусто — слот был пуст)
		TUniquePtr<FAsdaCVarScope> CVars;      // ключи — вернуть
		TMap<int32, int32> CodeBySlot;         // слот → код предмета этого типа из item_list
		TArray<TArray<uint8>> Saved2;          // расходники до теста (ячейка запасной пачки)
		int32 SpareCell = -1;                  // ячейка расходников с запасной пачкой стрел (5030 @7)

		void Expect(bool b, const FString& What, const FString& Det)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaEquipTextTest, Log, TEXT("САМОТЕСТ-НАДЕТОЕ %s: %s — %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *What, *Det);
		}
		template<class T> T* Sub() const
		{
			UWorld* Wd = W.Get();
			UGameInstance* GI = Wd ? Wd->GetGameInstance() : nullptr;
			return GI ? GI->GetSubsystem<T>() : nullptr;
		}
		void Exec(const FString& Cmd) const
		{
			UE_LOG(LogAsdaEquipTextTest, Log, TEXT("САМОТЕСТ-НАДЕТОЕ exec: %s"), *Cmd);
			if (GEngine && W.IsValid()) { GEngine->Exec(W.Get(), *Cmd); }
		}
		// Текст живого узла `EQUIP_SET{Slot}` открытой сумки; `bShown` — узел и все родители видимы.
		FString SlotText(int32 Slot, bool& bShown) const
		{
			bShown = false;
			const FString Want = FString::Printf(TEXT("EQUIP_SET%d"), Slot);
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (!UW || UW->GetWorld() != W.Get() || !UW->WidgetTree || !UW->IsInViewport()
					|| UW->GetClass()->GetName() != TEXT("WBP_INVEN_WIN_C")) { continue; }
				UTextBlock* Tb = Cast<UTextBlock>(UW->WidgetTree->FindWidget(FName(*Want)));
				if (!Tb) { continue; }
				bShown = true;
				for (const UWidget* P = Tb; P; P = P->GetParent()) { if (!P->IsVisible()) { bShown = false; break; } }
				if (!UW->IsVisible()) { bShown = false; }
				return Tb->GetText().ToString();
			}
			return FString();
		}
		// Сверить надпись слота с ожидаемой (в режиме legacy ждём пустую).
		void CheckSlot(const TCHAR* Stage, int32 Slot, const FString& WantAfter)
		{
			bool bShown = false;
			const FString Got = SlotText(Slot, bShown);
			const FString Want = bLegacy ? FString() : WantAfter;
			Expect(Got == Want && (Want.IsEmpty() || bShown),
				FString::Printf(TEXT("[%s] слот %d: надпись «%s»"), Stage, Slot, *Want),
				FString::Printf(TEXT("в окне «%s», видим=%d"), *Got, bShown ? 1 : 0));
		}
		// Шаг-снимок.
		void AddShot(const TCHAR* Name)
		{
			const FString N = Name;
			Steps.Add([this, N]() -> float
			{
				const FString File = Dir / FString::Printf(TEXT("%s_%s.png"), *Tag, *N);
				FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				UE_LOG(LogAsdaEquipTextTest, Log, TEXT("САМОТЕСТ-НАДЕТОЕ кадр → %s"), *File);
				return -1.f;
			});
		}
		int32 Stops0 = 0;   // снятий атаки до шага «стрел нет»
		uint32 CapGen = 0;                     // поколение перехвата отправок (0 — не взведён)
		TArray<FIntPoint> Sent;                // перехваченные отправки: (опкод, actionType для 4024 иначе −1)
		bool bSwingBefore = false;             // цикл клипа атаки шёл перед подачей 5030 код 2
		// Подать 5013 «подобрал» с готовой записью (очередь ИНВЕНТАРЯ) — для проверки порядка очередей с 5030.
		void Feed5013(const TArray<uint8>& Rec57) const
		{
			TArray<uint8> B; B.SetNumZeroed(62);
			B[0] = 1; AsdaBytes::WrI16(B.GetData() + 1, 0);
			FMemory::Memcpy(B.GetData() + 3, Rec57.GetData(), FAsdaItemInstance::kRecordSize);
			AsdaBytes::WrI16(B.GetData() + 60, 777);   // вес подбора: 5030 следом обязан его перекрыть своим (321)
			if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5013, B); }
		}
		// Подать 5030 штатным приёмом (диспетчер → очередь) и разобрать очередь сразу.
		void Feed5030(int32 Code, int32 Amount, int32 Weight, int32 BagSlot) const
		{
			TArray<uint8> B; B.SetNumZeroed(UAsdaArrowAmmoSubsystem::kBodySize);
			B[0] = (uint8)Code; AsdaBytes::WrI32(B.GetData() + 1, Amount); AsdaBytes::WrI16(B.GetData() + 5, Weight);
			AsdaBytes::WrI16(B.GetData() + 7, BagSlot); B[11] = 2;
			if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5030, B); }
			if (UAsdaArrowAmmoSubsystem* A = Sub<UAsdaArrowAmmoSubsystem>()) { A->PumpPending(); }
		}
		// Надеть запись: код по типу слота, количество +9, гейт +24 и процент +27 (i16).
		bool Wear(int32 Slot, int32 Amount, int32 Percent)
		{
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>();
			const int32* Code = CodeBySlot.Find(Slot);
			if (!Inv || !Code) { return false; }
			TArray<uint8> R; R.SetNumZeroed(FAsdaItemInstance::kRecordSize);
			AsdaBytes::WrI32(R.GetData() + 0, *Code);
			R[4] = 3;
			AsdaBytes::WrI16(R.GetData() + 5, Slot);
			AsdaBytes::WrI32(R.GetData() + 9, Amount);
			R[24] = Percent != 0 ? 3 : 0;
			AsdaBytes::WrI16(R.GetData() + 27, Percent);
			return Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		}
	};

	static void Finish(TSharedRef<FCtx> C)
	{
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>())
		{
			for (int32 i = 0; i < C->Saved3.Num(); ++i)
			{
				if (C->Saved3[i].Num() >= FAsdaItemInstance::kRecordSize) { Inv->ApplyExternalRecord(C->Saved3[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(3, i); }
			}
			for (int32 i = 0; i < C->Saved2.Num(); ++i)
			{
				if (C->Saved2[i].Num() >= FAsdaItemInstance::kRecordSize) { Inv->ApplyExternalRecord(C->Saved2[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(2, i); }
			}
			Inv->CloseWindow();
		}
		if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(C->Sub<UAsdaNetSubsystem>(), C->CapGen); C->CapGen = 0; }
		C->CVars.Reset();
		AsdaSelfTestKit::Finish(C->W.Get(), TEXT("asda.SelfTest.EquipCellText"), C->bQuit, LogAsdaEquipTextTest, TEXT("САМОТЕСТ-НАДЕТОЕ"),
			C->Ok, C->Fail, *FString::Printf(TEXT(" (режим %s)"), C->bLegacy ? TEXT("legacy") : TEXT("эталон")));
	}

	static void Build(TSharedRef<FCtx> C)
	{
		FCtx* P = &C.Get();
		// ── 0. свой персонаж офлайн ──
		P->Steps.Add([P]() -> float
		{
			if (AsdaSelfTestKit::NeedSelfSpawn(P->W.Get()))
			{
				P->Exec(TEXT("Asda.Load.CurrentMap 3"));
				P->Exec(TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			return 3.0f;
		});
		// ── 1. ключ, коды по типам слотов, надеть набор, открыть сумку ──
		P->Steps.Add([P]() -> float
		{
			UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			UAsdaItemTableSubsystem* IT = P->Sub<UAsdaItemTableSubsystem>();
			if (!Inv || !IT) { P->Expect(false, TEXT("подсистемы сумки/таблиц"), TEXT("нет")); return 0.f; }
			P->CVars = MakeUnique<FAsdaCVarScope>(P->W.Get(), TEXT("asda.SelfTest.EquipCellText"));
			P->CVars->Set(TEXT("asda.Inven.EquipCellText"), P->bLegacy ? 0 : 1);
			P->CVars->Set(TEXT("asda.Inven.EnchantTextGradeColor"), 0);   // эталонный цвет 0xFFFFF200 — сверяем 1:1
			P->CVars->Set(TEXT("asda.Inven.Arrow5030"), P->bLegacy ? 0 : 1);   // приём 5030: legacy — прежний порт (пакет игнорируется)
			for (int32 Code = 1; Code < 200000; ++Code)
			{
				const FAsdaItemRecord* Rec = IT->GetItem(Code);
				if (!Rec || IT->IsSpecialItemCode(Code) || !IT->GetItemIcon(Code, true).IsValid()) { continue; }   // эталон без пути иконки надпись гасит
				for (const FWear& Wr : kWear)
				{
					if (Rec->ItemType == Wr.Slot && !P->CodeBySlot.Contains(Wr.Slot)) { P->CodeBySlot.Add(Wr.Slot, Code); }
				}
				if (P->CodeBySlot.Num() == (int32)UE_ARRAY_COUNT(kWear)) { break; }
			}
			P->Saved3.Reset();
			for (int32 i = 0; i < Inv->GetContainerSize(3); ++i)
			{
				const FAsdaItemInstance* I = Inv->GetCell(3, i);
				P->Saved3.Add((I && !I->IsEmpty()) ? I->Raw : TArray<uint8>());
				Inv->ClearCell(3, i);
			}
			FString Det;
			for (const FWear& Wr : kWear)
			{
				const bool b = P->Wear(Wr.Slot, Wr.Amount, Wr.Percent);
				Det += FString::Printf(TEXT("%d:%d%s "), Wr.Slot, P->CodeBySlot.FindRef(Wr.Slot), b ? TEXT("") : TEXT("(ОТКАЗ)"));
			}
			P->Expect(P->CodeBySlot.Num() == (int32)UE_ARRAY_COUNT(kWear), TEXT("набор надетого собран (слот:код)"), Det);
			Inv->OpenWindow();
			Inv->SetTab(1);
			return 1.5f;
		});
		P->AddShot(TEXT("1_worn"));
		P->Steps.Add([P]() -> float
		{
			P->CheckSlot(TEXT("worn"), 9, TEXT("+86%"));
			P->CheckSlot(TEXT("worn"), 1, TEXT("+24%"));
			P->CheckSlot(TEXT("worn"), 5, TEXT("+7%"));
			P->CheckSlot(TEXT("worn"), 3, TEXT(""));
			P->CheckSlot(TEXT("worn"), 10, TEXT("250"));
			// Выстрел: сервер отдаёт новое количество слота 10 (эталон `uiStatLabelUpdateCount@0xC6BF80`) — перерисовка надписи.
			P->Wear(10, 249, 0);
			// Перенос/разделение улучшения меняет процент оружия.
			P->Wear(9, 0, 41);
			return 0.5f;
		});
		P->AddShot(TEXT("2_shot_transfer"));
		P->Steps.Add([P]() -> float
		{
			P->CheckSlot(TEXT("shot"), 10, TEXT("249"));
			P->CheckSlot(TEXT("transfer"), 9, TEXT("+41%"));
			// Снять кольцо (эталон `slotItemDrop` → `itemUI_DrawEquipSlotIcon(…, 0, слот)` — текст ""). Штатный путь приёмника:
			//   запись с количеством −1 ⇒ `ClearCell` + `RefreshEquip` (голый `ClearCell` окно не перерисовывает — это делают приёмники).
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>())
			{
				TArray<uint8> R; R.SetNumZeroed(FAsdaItemInstance::kRecordSize);
				AsdaBytes::WrI32(R.GetData() + 0, P->CodeBySlot.FindRef(5)); R[4] = 3; AsdaBytes::WrI16(R.GetData() + 5, 5);
				AsdaBytes::WrI32(R.GetData() + 9, -1);
				Inv->ApplyExternalRecord(R);
			}
			return 0.5f;
		});
		P->Steps.Add([P]() -> float
		{
			P->CheckSlot(TEXT("unequip"), 5, TEXT(""));
			// Повторное открытие окна: надписи восстанавливаются из модели (`RefreshEquip` при открытии).
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); Inv->OpenWindow(); Inv->SetTab(0); }
			return 1.0f;
		});
		P->AddShot(TEXT("3_reopen_tab0"));
		P->Steps.Add([P]() -> float
		{
			P->CheckSlot(TEXT("reopen"), 9, TEXT("+41%"));
			P->CheckSlot(TEXT("reopen"), 1, TEXT("+24%"));
			P->CheckSlot(TEXT("reopen"), 10, TEXT("249"));
			// ── 5030: запасная пачка того же колчана ×50 в последней ячейке расходников ──
			UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			if (!Inv) { return 0.f; }
			P->Saved2.Reset();
			for (int32 i = 0; i < Inv->GetContainerSize(2); ++i)
			{
				const FAsdaItemInstance* I = Inv->GetCell(2, i);
				P->Saved2.Add((I && !I->IsEmpty()) ? I->Raw : TArray<uint8>());
			}
			P->SpareCell = Inv->GetContainerSize(2) - 1;
			Inv->ClearCell(2, P->SpareCell);   // запасная пачка придёт пакетом 5013 в одном кадре с 5030 (порядок очередей)
			Inv->SetTab(0);
			// Выстрел: 5030 код 1, количество 248 — штатным приёмом (диспетчер → очередь → разбор).
			P->Feed5030(1, 248, 0, 0);
			return 0.3f;
		});
		P->Steps.Add([P]() -> float
		{
			UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			const int32 Amt = Inv ? Inv->GetCellAmount(3, 10) : -1;
			P->Expect(P->bLegacy ? Amt == 249 : Amt == 248, TEXT("[5030 код 1] количество слота 10 после выстрела"),
				FString::Printf(TEXT("%d (ждём %d)"), Amt, P->bLegacy ? 249 : 248));
			P->CheckSlot(TEXT("5030 выстрел"), 10, TEXT("248"));
			// Пачка кончилась: код 1, количество 0, вес 321, ячейка запасной пачки.
			if (UAsdaArrowAmmoSubsystem* A = P->Sub<UAsdaArrowAmmoSubsystem>()) { A->ClearShownNotices(); }
			if (UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>()) { Net->SelfWeightCur = -1; }
			// ★ Ревизия 01.11 (гонка очередей): 5013 кладёт запасную пачку в очередь ИНВЕНТАРЯ, 5030 приходит следом в ТОМ ЖЕ кадре
			//   (очередь инвентаря до 5030 не разбиралась) — разбор 5030 обязан увидеть пачку.
			TArray<uint8> Spare; Spare.SetNumZeroed(FAsdaItemInstance::kRecordSize);
			AsdaBytes::WrI32(Spare.GetData() + 0, P->CodeBySlot.FindRef(10)); Spare[4] = 2;
			AsdaBytes::WrI16(Spare.GetData() + 5, P->SpareCell); AsdaBytes::WrI32(Spare.GetData() + 9, 50);
			P->Feed5013(Spare);
			const FAsdaItemInstance* Pre = P->Sub<UAsdaInventorySubsystem>() ? P->Sub<UAsdaInventorySubsystem>()->GetCell(2, P->SpareCell) : nullptr;
			P->Expect(!Pre || Pre->IsEmpty(), TEXT("[порядок очередей] 5013 ещё в очереди инвентаря в момент прихода 5030"), TEXT(""));
			P->Feed5030(1, 0, 321, P->SpareCell);
			return 0.3f;
		});
		P->AddShot(TEXT("4_refill"));
		P->Steps.Add([P]() -> float
		{
			UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			UAsdaArrowAmmoSubsystem* A = P->Sub<UAsdaArrowAmmoSubsystem>();
			UAsdaNetSubsystem* Net = P->Sub<UAsdaNetSubsystem>();
			if (!Inv || !A || !Net) { P->Expect(false, TEXT("подсистемы 5030"), TEXT("нет")); return 0.f; }
			if (!P->bLegacy)
			{
				const TArray<int32>& N = A->GetShownNotices();
				P->Expect(Inv->GetCellAmount(3, 10) == 50 && Inv->GetCellItemCode(3, 10) == P->CodeBySlot.FindRef(10)
					&& Inv->GetCellItemCode(2, P->SpareCell) == 0 && Net->SelfWeightCur == 321
					&& N.Num() == 2 && N[0] == 20135 && N[1] == 20134,
					TEXT("[5030 подтяжка] слот 10 = запасная пачка ×50, ячейка расходников пуста, вес 321, чат 20135 → 20134"),
					FString::Printf(TEXT("слот10 %d×%d, расх[%d]=%d, вес %d, нотисов %d"), Inv->GetCellItemCode(3, 10), Inv->GetCellAmount(3, 10),
						P->SpareCell, Inv->GetCellItemCode(2, P->SpareCell), Net->SelfWeightCur, N.Num()));
				P->CheckSlot(TEXT("5030 подтяжка"), 10, TEXT("50"));
			}
			else
			{
				P->Expect(Inv->GetCellAmount(3, 10) == 249 && Inv->GetCellItemCode(2, P->SpareCell) != 0 && Net->SelfWeightCur == 777,
					TEXT("[5030 legacy] пакет проигнорирован: слот 10 не тронут, пачка 5013 лежит в расходниках, вес — от 5013 (777)"),
					FString::Printf(TEXT("слот10 ×%d, вес %d"), Inv->GetCellAmount(3, 10), Net->SelfWeightCur));
			}
			// Стрел нет совсем: идёт атака из лука (действие 5: авто-атака, стойка, бесконечный цикл клипа) — код 2. Всё в ОДНОМ
			//   кадре: иначе боевой тик без цели сам гасит флаги и проверка ничего бы не доказала. Отправки — под перехватом.
			A->ClearShownNotices();
			P->Stops0 = A->GetAttackStopCount();
			P->Sent.Reset();
			P->CapGen = AsdaSelfTestKit::ArmCapture(Net, [P](uint16 Op, const uint8* Pl, int32 Len, bool, TCHAR)
			{
				P->Sent.Add(FIntPoint(Op, (Op == 4024 && Len >= 20) ? AsdaBytes::RdI16(Pl + 18) : -1));
			}, 0, TEXT("asda.SelfTest.EquipCellText"));
			AAsdaPlayerController* PC = P->W.IsValid() ? Cast<AAsdaPlayerController>(P->W->GetFirstPlayerController()) : nullptr;
			AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			if (PC && Ch)
			{
				PC->bAutoAttack = true; PC->bInCombatStance = true; PC->bCombatStarted = true;
				Ch->StartAttackSwing(/*лук*/ 10, /*HoldSec — бесконечно, как свой*/ 0.f);
				P->bSwingBefore = Ch->IsAttackAnimActive();
			}
			P->Feed5030(2, 0, 0, 0);
			if (PC && Ch)
			{
				int32 N400 = 0;
				for (const FIntPoint& S : P->Sent) { if (S.X == 4024 && S.Y == 400) { ++N400; } }
				if (!P->bLegacy)
				{
					P->Expect(P->bSwingBefore && !Ch->IsAttackAnimActive() && !PC->bAutoAttack && !PC->bInCombatStance && !PC->bCombatStarted
						&& !PC->bWaitingCombatAck && N400 == 1,
						TEXT("[5030 код 2] цикл клипа атаки остановлен, флаги боя и стойка сняты, ушёл 4024/400 ровно один раз"),
						FString::Printf(TEXT("цикл до %d после %d, авто-атака %d, стойка %d, серия %d, 4024/400 ×%d (перехвачено %d)"),
							P->bSwingBefore ? 1 : 0, Ch->IsAttackAnimActive() ? 1 : 0, PC->bAutoAttack ? 1 : 0, PC->bInCombatStance ? 1 : 0,
							PC->bCombatStarted ? 1 : 0, N400, P->Sent.Num()));
				}
				else
				{
					P->Expect(Ch->IsAttackAnimActive() && N400 == 0, TEXT("[5030 legacy] код 2 проигнорирован: цикл атаки идёт, 4024/400 нет"),
						FString::Printf(TEXT("цикл %d, 4024/400 ×%d"), Ch->IsAttackAnimActive() ? 1 : 0, N400));
					Ch->StopAttackAnim();
					PC->bAutoAttack = false; PC->bInCombatStance = false; PC->bCombatStarted = false;
				}
			}
			return 1.0f;
		});
		P->AddShot(TEXT("5_no_arrows"));
		P->Steps.Add([P]() -> float
		{
			UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			UAsdaArrowAmmoSubsystem* A = P->Sub<UAsdaArrowAmmoSubsystem>();
			AAsdaPlayerController* PC = P->W.IsValid() ? Cast<AAsdaPlayerController>(P->W->GetFirstPlayerController()) : nullptr;
			if (!Inv || !A || !PC) { P->Expect(false, TEXT("подсистемы/контроллер 5030"), TEXT("нет")); return 0.f; }
			const TArray<int32>& N = A->GetShownNotices();
			if (!P->bLegacy)
			{
				P->Expect(Inv->GetCellItemCode(3, 10) == 0 && !PC->bAutoAttack && A->GetAttackStopCount() == P->Stops0 + 1
					&& N.Num() == 2 && N[0] == 20135 && N[1] == 2021,
					TEXT("[5030 код 2] слот 10 снят, авто-атака снята, чат 20135 → 2021"),
					FString::Printf(TEXT("слот10 %d, авто-атака %d, снятий %d, нотисов %d"), Inv->GetCellItemCode(3, 10),
						PC->bAutoAttack ? 1 : 0, A->GetAttackStopCount() - P->Stops0, N.Num()));
				P->CheckSlot(TEXT("5030 стрел нет"), 10, TEXT(""));
				UE_LOG(LogAsdaEquipTextTest, Log, TEXT("САМОТЕСТ-НАДЕТОЕ тексты: 2021 «%s», 20134 «%s»"),
					*AsdaNotice::Loc(P->W->GetGameInstance(), 2021), *AsdaNotice::LocSlot(P->W->GetGameInstance(), 20134, TEXT("X")));
			}
			else
			{
				// Флаг авто-атаки без цели гасит сам `AttackTick` за время паузы — смотрим счётчик снятий этой подсистемой.
				P->Expect(Inv->GetCellItemCode(3, 10) != 0 && A->GetAttackStopCount() == P->Stops0 && N.Num() == 0,
					TEXT("[5030 legacy] код 2 проигнорирован: слот 10 цел, атаку подсистема не снимала, нотисов нет"),
					FString::Printf(TEXT("слот10 %d, снятий %d, нотисов %d"), Inv->GetCellItemCode(3, 10), A->GetAttackStopCount() - P->Stops0, N.Num()));
			}
			// За секунду после «стрел нет» замах не уходит: ни 4028 (обычный), ни 4203 (PvP), цикл клипа не возобновился.
			int32 NSwing = 0;
			for (const FIntPoint& S : P->Sent) { if (S.X == 4028 || S.X == 4026 || S.X == 4203) { ++NSwing; } }
			const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
			P->Expect(NSwing == 0 && Ch && !Ch->IsAttackAnimActive(), TEXT("[5030 код 2] через 1 с: 4026/4028/4203 не уходили, клип атаки не идёт"),
				FString::Printf(TEXT("замахов %d, клип %d"), NSwing, (Ch && Ch->IsAttackAnimActive()) ? 1 : 0));
			if (P->CapGen) { AsdaSelfTestKit::DisarmCapture(P->Sub<UAsdaNetSubsystem>(), P->CapGen); P->CapGen = 0; }
			PC->bAutoAttack = false;
			return 0.2f;
		});
	}

	static FAutoConsoleCommandWithWorldAndArgs GCmdEquipCellText(
		TEXT("asda.SelfTest.EquipCellText"),
		TEXT("Цифры в надетых ячейках сумки путём игрока: «+N%» заточки оружия/брони/кольца и количество стрел слота 10; "
		     "смена количества, перенос заточки, снятие, повторное открытие; кадры в Reports/equip_cell_text. "
		     "`tag=after` `legacy` `delay=N` `quit`. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			static const uint16 kGuardOps[] = { 5000 };
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.SelfTest.EquipCellText"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->W = W;
			C->bQuit = KitArgs.bQuit;
			C->Delay = KitArgs.Delay;
			C->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/equip_cell_text"));
			for (const FString& A : KitArgs.Pos)
			{
				if (A.StartsWith(TEXT("tag="))) { C->Tag = A.Mid(4); }
				else if (A.Equals(TEXT("legacy"), ESearchCase::IgnoreCase)) { C->bLegacy = true; C->Tag = TEXT("legacy"); }
			}
			Build(C);
			C->NextTime = FPlatformTime::Seconds() + FMath::Max(0.f, C->Delay);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) -> bool
			{
				if (!C->W.IsValid()) { return false; }
				if (C->bAwaitShot)
				{
					if (FScreenshotRequest::IsScreenshotRequested()) { return true; }
					if (C->FramesAfterShot++ < 2) { return true; }
					C->bAwaitShot = false;
				}
				if (FPlatformTime::Seconds() < C->NextTime) { return true; }
				if (!C->Steps.IsValidIndex(C->Index))
				{
					Finish(C);
					return false;
				}
				const float Wait = C->Steps[C->Index++]();
				if (Wait < 0.f) { C->bAwaitShot = true; C->FramesAfterShot = 0; C->NextTime = 0.0; }
				else { C->NextTime = FPlatformTime::Seconds() + Wait; }
				return true;
			}));
		}));
}

#endif // !UE_BUILD_SHIPPING
