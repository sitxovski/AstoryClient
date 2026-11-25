// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «ЧИСЛО СТРЕЛ НА НАДЕТОМ КОЛЧАНЕ» ПО ПУТЯМ ИГРОКА (08.11.2026, пачка 18, баг 6 (б)). [ОРИГИНАЛ]
//
// ЖАЛОБА. На клиенте alpha-6.9.9 у надетого колчана баллисты (ballista's Quiver, 300 стрел) в слоте 10 над сумкой нет числа.
// ЭТАЛОН. `uiEquipPanelRefreshSlots@0xC6AB30` и `itemUI_DrawEquipSlotIcon@0xC6B1B0`: слот 10 — `sprintf("%d", запись+9)`, белым,
//   при любом пути перерисовки (вход в мир 1027 → `game_main.c:2807`, приём 5001 `inventoryApplySlotItem` case 3, обмен
//   `slotItemDrop` case 3). Порт — `AsdaEquipCellText::Build` из `UAsdaInventorySubsystem::UpdateEquipVisual`.
//
// ЧТО ПРОВЕРЯЕТ (штатные приёмники, без прямой записи в ячейки там, где у сервера есть пакет):
//   1. вход в мир с надетым колчаном — 11 записей надетого тем же делегатом, что пакет 1027 (`UAsdaNetSubsystem::OnEquipFull`),
//      по очереди для колчана лука 20562, арбалета 20565 и баллисты 20568 (по 300 стрел); окно открыто ПОСЛЕ и ДО прихода;
//   2. переоткрытие сумки;
//   3. выстрел — пакет 5030 код 1 (количество 299) штатным приёмом;
//   4. снятие — пакет 5001 (3:10 → 2:5), надпись слота пустая, число переехало в ячейку сумки;
//   5. надевание из сумки — 5001 (2:5 → 3:10);
//   6. замена колчана другим — 5001 обмен местами (колчан арбалета 20565 ×300 из 2:6 на 3:10);
//   7. смена карты — сброс сессии «смена карты» тем же `UAsdaSessionResetSubsystem::Broadcast`, что при травеле;
//   8. настоящий травел UE дважды (`OpenLevel`, как `asda.SelfTest.IconsAfterTravel`): окно сумки пересоздаётся, надпись — из модели.
//   На каждом шаге — надпись `EQUIP_SET10` живого окна (текст и видимость по цепочке родителей) и кадр.
//
// Консоль: `asda.SelfTest.QuiverCount [tag=after] [quit]`. Только офлайн. Кадры: `Reports/batch18/item_count/<tag>_quiver_*.png`.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaArrowAmmoSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Components/CanvasPanelSlot.h"
#include "Internationalization/Regex.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuiverTest, Log, All);

namespace AsdaQuiverTestLocal
{
	static constexpr int32 kSlot = 10;        // слот стрел (`a3 == 10` эталона)
	static constexpr int32 kBow = 20562;      // «Колчан» (лук), FlagWord176 300
	static constexpr int32 kXbow = 20565;     // «Колчан для арбалета», 300
	static constexpr int32 kBallista = 20568; // «Колчан для баллисты» (ballista's Quiver), 300
	static constexpr int32 kBagA = 5;         // ячейка расходников для снятия/надевания
	static constexpr int32 kBagB = 6;         // ячейка расходников запасного колчана (обмен)

	// Запись 57 байт: код, контейнер, слот, количество.
	static TArray<uint8> Rec(int32 Code, int32 Cont, int32 Slot, int32 Amount)
	{
		TArray<uint8> R; R.SetNumZeroed(FAsdaItemInstance::kRecordSize);
		AsdaBytes::WrI32(R.GetData() + 0, Code);
		R[4] = (uint8)Cont;
		AsdaBytes::WrI16(R.GetData() + 5, Slot);
		AsdaBytes::WrI32(R.GetData() + 9, Amount);
		return R;
	}

	struct FCtx
	{
		TWeakObjectPtr<UGameInstance> GI;   // переживает травел (мир — нет)
		bool bQuit = false;
		FString Tag = TEXT("after");
		FString Dir;
		int32 Ok = 0, Fail = 0;
		TArray<TFunction<float()>> Steps;
		int32 Index = 0;
		double NextTime = 0.0;
		bool bAwaitShot = false;
		int32 FramesAfterShot = 0;
		TArray<TArray<uint8>> Saved2, Saved3, Saved5;   // сумка расходников, надетое и склад до теста
		int32 EntryIdx = 0;                     // какой колчан идёт по пути «вход в мир»

		UWorld* World() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template<class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Expect(bool b, const FString& What, const FString& Det = FString())
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaQuiverTest, Log, TEXT("САМОТЕСТ-КОЛЧАН %s: %s — %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *What, *Det);
		}
		void Exec(const FString& Cmd) const
		{
			UE_LOG(LogAsdaQuiverTest, Log, TEXT("САМОТЕСТ-КОЛЧАН exec: %s"), *Cmd);
			if (GEngine && World()) { GEngine->Exec(World(), *Cmd); }
		}
		// Надпись слота 10 живого окна сумки и её видимость по цепочке.
		FString SlotText(bool& bShown) const
		{
			bShown = false;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (!UW || UW->GetWorld() != World() || !UW->WidgetTree || !UW->IsInViewport()) { continue; }
				if (UW->GetClass()->GetName() != TEXT("WBP_INVEN_WIN_C")) { continue; }
				UTextBlock* Tb = Cast<UTextBlock>(UW->WidgetTree->FindWidget(TEXT("EQUIP_SET10")));
				if (!Tb) { continue; }
				bShown = UW->IsVisible();
				for (const UWidget* P = Tb; P && bShown; P = P->GetParent()) { bShown = P->IsVisible(); }
				return Tb->GetText().ToString();
			}
			return FString(TEXT("<нет окна>"));
		}
		void Check(const TCHAR* Stage, const FString& Want)
		{
			bool bShown = false;
			const FString Got = SlotText(bShown);
			const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>();
			Expect(Got == Want && (Want.IsEmpty() || bShown),
				FString::Printf(TEXT("[%s] слот 10: надпись «%s»"), Stage, *Want),
				FString::Printf(TEXT("в окне «%s», видим=%d; модель: код %d ×%d"), *Got, bShown ? 1 : 0,
					Inv ? Inv->GetCellItemCode(3, kSlot) : -1, Inv ? Inv->GetCellAmount(3, kSlot) : -1));
		}
		void AddShot(const TCHAR* Name)
		{
			const FString N = Name;
			Steps.Add([this, N]() -> float
			{
				const FString File = Dir / FString::Printf(TEXT("%s_quiver_%s.png"), *Tag, *N);
				FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				UE_LOG(LogAsdaQuiverTest, Log, TEXT("САМОТЕСТ-КОЛЧАН кадр → %s"), *File);
				return -1.f;
			});
		}
		// Вход в мир: 11 записей надетого тем же делегатом, что пакет 1027 (слот 10 — колчан).
		void FeedEquipFull(int32 Code, int32 Amount) const
		{
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>();
			if (!Net) { return; }
			TArray<uint8> All; All.SetNumZeroed(FAsdaItemInstance::kRecordSize * 11);
			for (int32 S = 0; S < 11; ++S) { AsdaBytes::WrI16(All.GetData() + FAsdaItemInstance::kRecordSize * S + 5, S); All[FAsdaItemInstance::kRecordSize * S + 4] = 3; }
			const TArray<uint8> R = Rec(Code, 3, kSlot, Amount);
			FMemory::Memcpy(All.GetData() + FAsdaItemInstance::kRecordSize * kSlot, R.GetData(), R.Num());
			Net->OnEquipFull.Broadcast(All);
		}
		// ── Часть (а): кегль числа +10% и цифра внутри своей ячейки (живая геометрия) ──
		// План иконки ячейки: узел импортёра `{Idc}_{N}_{M}` того же родителя.
		static UWidget* IconPlane(UUserWidget* Root, const UTextBlock* Tb)
		{
			UWidget* Found = nullptr;
			const FString Idc = Tb->GetName();
			Root->WidgetTree->ForEachWidget([&](UWidget* W)
			{
				if (Found || !W || W == Tb || W->GetParent() != Tb->GetParent()) { return; }
				FString I; int32 J = 0, Pl = 0;
				if (AsdaUi::ParsePlaneName(W->GetName(), I, J, Pl) && I == Idc) { Found = W; }
			});
			return Found;
		}
		// Все узлы-числа окна `Wbp` (regex): кегль = WantSize; у видимых непустых цифра (по желаемому размеру текста и выравниванию
		//   по центру) внутри ячейки по горизонтали с полем ≥ 1 px и низом не ниже низа ячейки (база UI).
		void CheckFit(const TCHAR* Stage, const TCHAR* Wbp, const TCHAR* Rx, float WantSize)
		{
			int32 Found = 0, SizeOk = 0, Shown = 0, Fit = 0;
			FString Bad;
			const FRegexPattern Pat(Rx);
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (!UW || UW->GetWorld() != World() || !UW->WidgetTree || !UW->IsInViewport() || !UW->IsVisible()) { continue; }
				if (UW->GetClass()->GetName() != FString(Wbp) + TEXT("_C")) { continue; }
				UW->WidgetTree->ForEachWidget([&](UWidget* Wg)
				{
					UTextBlock* Tb = Cast<UTextBlock>(Wg);
					if (!Tb) { return; }
					FRegexMatcher M(Pat, Tb->GetName());
					if (!M.FindNext()) { return; }
					++Found;
					if (FMath::IsNearlyEqual(Tb->GetFont().Size, WantSize, 0.05f)) { ++SizeOk; }
					if (Tb->GetText().IsEmpty()) { return; }
					for (const UWidget* Pw = Tb; Pw; Pw = Pw->GetParent()) { if (!Pw->IsVisible()) { return; } }
					UWidget* Icon = IconPlane(UW, Tb);
					FVector2D BMn, BMx, CMn, CMx;
					if (!Icon || !AsdaUi::WidgetAbsRect(Tb, BMn, BMx) || !AsdaUi::WidgetAbsRect(Icon, CMn, CMx)) { return; }
					const UCanvasPanelSlot* IS = Cast<UCanvasPanelSlot>(Icon->Slot);
					const float Sc = (IS && IS->GetOffsets().Right > 0.f) ? (CMx.X - CMn.X) / IS->GetOffsets().Right : 1.f;
					const FVector2D D = Tb->GetDesiredSize() * Sc;
					const double Gx0 = BMn.X + ((BMx.X - BMn.X) - D.X) * 0.5;
					// Заливка цифры: обводка 1 px по краям желаемого размера, строка — с запасом под нижние выносные: низ
					//   заливки ≈ верх бокса + 0,8 высоты строки (цифры выносных не имеют).
					const double Out = Tb->GetFont().OutlineSettings.OutlineSize * Sc;
					const double L = Gx0 + Out, R = Gx0 + D.X - Out, Bot = BMn.Y + D.Y * 0.8;
					++Shown;
					const bool bOk = L >= CMn.X + 1.0 * Sc && R <= CMx.X - 1.0 * Sc && Bot <= CMx.Y + 0.5 * Sc;
					if (bOk) { ++Fit; }
					else { Bad += FString::Printf(TEXT("%s «%s» (л %.1f п %.1f низ %.1f) "), *Tb->GetName(), *Tb->GetText().ToString(), (L - CMn.X) / Sc, (CMx.X - R) / Sc, (CMx.Y - Bot) / Sc); }
					UE_LOG(LogAsdaQuiverTest, Log, TEXT("САМОТЕСТ-КОЛЧАН узел [%s] %s/%s «%s» кегль %.2f ячейка (%.1f,%.1f)-(%.1f,%.1f) заливка (%.1f..%.1f, низ %.1f) поля: лево %.1f право %.1f низ %.1f (база)"),
						Stage, Wbp, *Tb->GetName(), *Tb->GetText().ToString(), Tb->GetFont().Size, CMn.X, CMn.Y, CMx.X, CMx.Y, L, R, Bot,
						(L - CMn.X) / Sc, (CMx.X - R) / Sc, (CMx.Y - Bot) / Sc);
				});
			}
			Expect(Found > 0 && SizeOk == Found, FString::Printf(TEXT("[%s] %s: кегль числа %.2f у всех узлов"), Stage, Wbp, WantSize),
				FString::Printf(TEXT("узлов %d, с кеглем %d"), Found, SizeOk));
			Expect(Shown > 0 && Fit == Shown, FString::Printf(TEXT("[%s] %s: каждая видимая цифра внутри ячейки, не на рамке"), Stage, Wbp),
				FString::Printf(TEXT("видимых %d, внутри %d; не так: %s"), Shown, Fit, Bad.IsEmpty() ? TEXT("—") : *Bad));
		}
		// Травел UE: `OpenLevel` на мир (движок сносит виджеты мира, `PostLoadMap` рассылает «смену карты»).
		void Travel() const
		{
			if (UWorld* Wd = World()) { UGameplayStatics::OpenLevel(Wd, FName(TEXT("/Game/Map/alpen/alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
		}
		// Свой персонаж в новом мире (офлайн его ставит тест; в игре — вход сервера).
		void Respawn() const { Exec(TEXT("Asda.Load.CurrentMap 3")); Exec(TEXT("Asda.World.FakeSelfSpawn 256 256")); }
		// Пакет 5001 штатным приёмом: результат 1, вид (1 обмен / 2 переезд), источник и приёмник.
		void Feed5001(int32 SubKind, int32 SrcType, int32 SrcSlot, int32 DstType, int32 DstSlot) const
		{
			TArray<uint8> B; B.SetNumZeroed(24);
			B[0] = 1; B[1] = (uint8)SubKind;
			AsdaBytes::WrI16(B.GetData() + 2, SrcSlot); B[6] = (uint8)SrcType;
			AsdaBytes::WrI16(B.GetData() + 13, DstSlot); B[17] = (uint8)DstType;
			if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5001, B); }
		}
		// Пакет 5030 (выстрел) штатным приёмом и разбор очереди.
		void Feed5030(int32 Code, int32 Amount) const
		{
			TArray<uint8> B; B.SetNumZeroed(UAsdaArrowAmmoSubsystem::kBodySize);
			B[0] = (uint8)Code; AsdaBytes::WrI32(B.GetData() + 1, Amount); B[11] = 2;
			if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5030, B); }
			if (UAsdaArrowAmmoSubsystem* A = Sub<UAsdaArrowAmmoSubsystem>()) { A->PumpPending(); }
		}
	};

	static void Finish(TSharedRef<FCtx> C)
	{
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>())
		{
			auto Restore = [Inv](const TArray<TArray<uint8>>& Saved, int32 Cont)
			{
				for (int32 i = 0; i < Saved.Num(); ++i)
				{
					if (Saved[i].Num() >= FAsdaItemInstance::kRecordSize) { Inv->ApplyExternalRecord(Saved[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
					else { Inv->ClearCell(Cont, i); }
				}
			};
			Restore(C->Saved2, 2);
			Restore(C->Saved3, 3);
			Restore(C->Saved5, 5);
			Inv->CloseWindow();
		}
		AsdaSelfTestKit::Finish(C->World(), TEXT("asda.SelfTest.QuiverCount"), C->bQuit, LogAsdaQuiverTest, TEXT("САМОТЕСТ-КОЛЧАН"),
			C->Ok, C->Fail, *FString::Printf(TEXT(" (%s)"), *C->Tag));
	}

	static void Build(TSharedRef<FCtx> C)
	{
		FCtx* P = &C.Get();
		P->Steps.Add([P]() -> float
		{
			if (AsdaSelfTestKit::NeedSelfSpawn(P->World()))
			{
				P->Exec(TEXT("Asda.Load.CurrentMap 3"));
				P->Exec(TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			if (!Inv) { P->Expect(false, TEXT("подсистема сумки"), TEXT("нет")); return 0.f; }
			for (int32 Cont : { 2, 3 })
			{
				TArray<TArray<uint8>>& S = (Cont == 2) ? P->Saved2 : P->Saved3;
				S.Reset();
				for (int32 i = 0; i < Inv->GetContainerSize(Cont); ++i)
				{
					const FAsdaItemInstance* I = Inv->GetCell(Cont, i);
					S.Add((I && !I->IsEmpty()) ? I->Raw : TArray<uint8>());
					Inv->ClearCell(Cont, i);
				}
			}
			// Часть (а): стопки 1…9999 в сумке расходников и на складе.
			const int32 kFit[][2] = { { 20929, 9999 }, { 20930, 999 }, { 20956, 99 }, { 20571, 2 }, { kBallista, 300 }, { 20927, 9999 } };
			for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kFit); ++i)
			{
				Inv->ApplyExternalRecord(Rec(kFit[i][0], 2, i, kFit[i][1]), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
			P->Saved5.Reset();
			for (int32 i = 0; i < Inv->GetContainerSize(5); ++i)
			{
				const FAsdaItemInstance* I = Inv->GetCell(5, i);
				P->Saved5.Add((I && !I->IsEmpty()) ? I->Raw : TArray<uint8>());
				Inv->ClearCell(5, i);
			}
			for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(kFit); ++i)
			{
				Inv->ApplyExternalRecord(Rec(kFit[i][0], 5, i, kFit[i][1]), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
			Inv->OpenWindow(); Inv->SetTab(0);
			return 2.0f;
		});
		P->AddShot(TEXT("a_bag"));
		P->Steps.Add([P]() -> float
		{
			P->CheckFit(TEXT("сумка"), TEXT("WBP_INVEN_WIN"), TEXT("^INVEN_SET\\d+$"), 7.7f);
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); }
			P->Exec(TEXT("asda.Warehouse.Open 0"));
			return 1.5f;
		});
		P->AddShot(TEXT("a_ware"));
		P->Steps.Add([P]() -> float
		{
			P->CheckFit(TEXT("склад"), TEXT("WBP_WARE_WIN"), TEXT("^(WAREKEEP_TRAY\\d\\d|WAREBAG\\d_TRAY\\d\\d)$"), 8.8f);
			P->Exec(TEXT("asda.Warehouse.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			P->Exec(TEXT("asda.Shop.Open 6 1"));
			return 1.5f;
		});
		P->AddShot(TEXT("a_npcshop"));
		P->Steps.Add([P]() -> float
		{
			P->CheckFit(TEXT("магазин NPC"), TEXT("WBP_NPCSHOP_WIN"), TEXT("^(NPCSHOP_TRAY\\d\\d|NPCSHOP_BTRAY\\d\\d)$"), 8.8f);
			P->Exec(TEXT("asda.Shop.Close"));
			if (UAsdaWindowSubsystem* WM = P->Sub<UAsdaWindowSubsystem>()) { WM->CloseAll(); }
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>())
			{
				for (int32 i = 0; i < Inv->GetContainerSize(2); ++i) { Inv->ClearCell(2, i); }
			}
			return 0.5f;
		});
		// ── 1. Вход в мир: три вида колчанов; окно закрыто при приходе 1027 (как на входе) и открыто ПОСЛЕ ──
		const int32 kEntry[] = { kBow, kXbow, kBallista };
		const TCHAR* kEntryName[] = { TEXT("bow"), TEXT("xbow"), TEXT("ballista") };
		for (int32 k = 0; k < 3; ++k)
		{
			const int32 Code = kEntry[k];
			P->Steps.Add([P, Code]() -> float
			{
				if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); }
				P->FeedEquipFull(Code, 300);
				return 0.5f;
			});
			P->Steps.Add([P]() -> float
			{
				if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->OpenWindow(); Inv->SetTab(0); }
				return 1.0f;
			});
			P->AddShot(*FString::Printf(TEXT("1_entry_%s"), kEntryName[k]));
			P->Steps.Add([P, Code, k]() -> float
			{
				const UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
				P->Expect(Inv && Inv->GetCellItemCode(3, kSlot) == Code, FString::Printf(TEXT("[вход %d] 1027 надел колчан %d в слот 10"), k, Code));
				P->Check(*FString::Printf(TEXT("вход %d, окно открыто после"), k), TEXT("300"));
				return 0.2f;
			});
		}
		// ── 1б. Вход при ОТКРЫТОМ окне (1027 пришёл, когда сумка уже показана) ──
		P->Steps.Add([P]() -> float { P->FeedEquipFull(kBallista, 300); return 0.5f; });
		P->Steps.Add([P]() -> float { P->Check(TEXT("вход при открытом окне"), TEXT("300")); return 0.1f; });
		// ── 2. Переоткрытие сумки ──
		P->Steps.Add([P]() -> float
		{
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->CloseWindow(); Inv->OpenWindow(); Inv->SetTab(1); }
			return 1.0f;
		});
		P->Steps.Add([P]() -> float { P->Check(TEXT("переоткрытие, вкладка «Снаряжение»"), TEXT("300")); P->Feed5030(1, 299); return 0.5f; });
		// ── 3. Выстрел 5030 ──
		P->AddShot(TEXT("3_shot"));
		P->Steps.Add([P]() -> float
		{
			P->Check(TEXT("выстрел 5030"), TEXT("299"));
			P->Feed5001(2, 3, kSlot, 2, kBagA);   // ── 4. снятие в сумку ──
			return 0.6f;
		});
		P->Steps.Add([P]() -> float
		{
			const UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			P->Check(TEXT("снятие 5001 3:10 → 2:5"), TEXT(""));
			P->Expect(Inv && Inv->GetCellItemCode(2, kBagA) == kBallista && Inv->GetCellAmount(2, kBagA) == 299,
				TEXT("[снятие] колчан ×299 лёг в ячейку расходников 5"),
				Inv ? FString::Printf(TEXT("%d ×%d"), Inv->GetCellItemCode(2, kBagA), Inv->GetCellAmount(2, kBagA)) : FString());
			P->Feed5001(2, 2, kBagA, 3, kSlot);   // ── 5. надевание из сумки ──
			return 0.6f;
		});
		P->AddShot(TEXT("5_reequip"));
		P->Steps.Add([P]() -> float
		{
			P->Check(TEXT("надевание из сумки 5001 2:5 → 3:10"), TEXT("299"));
			// ── 6. замена: колчан арбалета ×300 в ячейке 6 → обмен местами со слотом 10 ──
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>())
			{
				Inv->ApplyExternalRecord(Rec(kXbow, 2, kBagB, 300), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
			P->Feed5001(1, 2, kBagB, 3, kSlot);
			return 0.6f;
		});
		P->AddShot(TEXT("6_swap"));
		P->Steps.Add([P]() -> float
		{
			const UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>();
			P->Check(TEXT("замена колчана 5001 обмен 2:6 ↔ 3:10"), TEXT("300"));
			P->Expect(Inv && Inv->GetCellItemCode(2, kBagB) == kBallista && Inv->GetCellAmount(2, kBagB) == 299,
				TEXT("[замена] прежний колчан баллисты ×299 ушёл в ячейку 6"));
			// ── 7. смена карты/канала: сброс сессии «смена карты» ──
			if (UAsdaSessionResetSubsystem* R = P->Sub<UAsdaSessionResetSubsystem>()) { R->Broadcast(EAsdaSessionResetReason::MapChanged); }
			return 0.5f;
		});
		P->Steps.Add([P]() -> float
		{
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->OpenWindow(); Inv->SetTab(0); }
			return 1.0f;
		});
		P->Steps.Add([P]() -> float
		{
			P->Check(TEXT("после сброса «смена карты»"), TEXT("300"));
			P->Travel();   // ── 8. настоящий травел (тот же приём, что `asda.SelfTest.IconsAfterTravel`) ──
			return 6.0f;
		});
		P->Steps.Add([P]() -> float { P->Respawn(); return 2.0f; });
		P->Steps.Add([P]() -> float
		{
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->OpenWindow(); Inv->SetTab(0); }
			return 1.5f;
		});
		P->AddShot(TEXT("8_travel"));
		P->Steps.Add([P]() -> float
		{
			P->Check(TEXT("после травела на другую карту"), TEXT("300"));
			P->Feed5030(1, 298);
			return 0.5f;
		});
		P->Steps.Add([P]() -> float
		{
			P->Check(TEXT("выстрел после травела"), TEXT("298"));
			P->Travel();
			return 6.0f;
		});
		P->Steps.Add([P]() -> float { P->Respawn(); return 2.0f; });
		P->Steps.Add([P]() -> float
		{
			if (UAsdaInventorySubsystem* Inv = P->Sub<UAsdaInventorySubsystem>()) { Inv->OpenWindow(); Inv->SetTab(1); }
			return 1.5f;
		});
		P->AddShot(TEXT("8_travel2"));
		P->Steps.Add([P]() -> float { P->Check(TEXT("второй травел"), TEXT("298")); return 0.2f; });
	}

	static FAutoConsoleCommandWithWorldAndArgs GCmdQuiver(
		TEXT("asda.SelfTest.QuiverCount"),
		TEXT("Число стрел на надетом колчане (слот 10) по путям игрока: вход 1027 (лук/арбалет/баллиста), переоткрытие, выстрел 5030, "
		     "снятие/надевание/замена 5001, сброс «смена карты», травел туда и обратно. `tag=after` `quit`. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.SelfTest.QuiverCount"), KitArgs)) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->GI = W->GetGameInstance();
			C->bQuit = KitArgs.bQuit;
			C->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch18/item_count"));
			for (const FString& A : KitArgs.Pos) { if (A.StartsWith(TEXT("tag="))) { C->Tag = A.Mid(4); } }
			Build(C);
			C->NextTime = FPlatformTime::Seconds() + FMath::Max(0.f, KitArgs.Delay);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) -> bool
			{
				if (!C->GI.IsValid()) { return false; }
				if (C->bAwaitShot)
				{
					if (FScreenshotRequest::IsScreenshotRequested()) { return true; }
					if (C->FramesAfterShot++ < 2) { return true; }
					C->bAwaitShot = false;
				}
				if (FPlatformTime::Seconds() < C->NextTime) { return true; }
				if (!C->World()) { return true; }   // травел: ждём новый мир
				if (!C->Steps.IsValidIndex(C->Index)) { Finish(C); return false; }
				const float Wait = C->Steps[C->Index++]();
				if (Wait < 0.f) { C->bAwaitShot = true; C->FramesAfterShot = 0; C->NextTime = 0.0; }
				else { C->NextTime = FPlatformTime::Seconds() + Wait; }
				return true;
			}));
		}));
}

#endif // !UE_BUILD_SHIPPING
