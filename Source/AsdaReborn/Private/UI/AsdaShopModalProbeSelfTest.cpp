// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 18, БАГ 2 (08.11.2026): щелчки под открытым окном покупки магазина.
//
// ЧТО ПРОВЕРЯЕТ (путь игрока `-game -AsdaNoNet -RenderOffscreen`). Для каждого окна покупки:
//   · СЕТКА: экран проходится сеткой 8 px; для точки берётся лист пути Slate (`LocateWindowUnderMouse`) — туда уйдёт нажатие,
//     если его не съест гейт верхнего окна (`FindLowerWindowUnderTopAt`) — и считаются «утечки»: нажатие доходит до кнопки,
//     флажка или поля ВНЕ окна покупки там, где оригинал его не отдаёт. Модальное у оригинала окно (`helper_setupFields`,
//     ui_core.c:56514-56612 — 1559 и окна сообщений; `ItemTooltip_PopulateWindow` item_inventory.c:21454 — 6296; нумпад) —
//     утечка в любой точке экрана. Немодальная панель внутри окна (15515 бустер-магазина, ui_core.c:9762 — только `SetVisible`)
//     — утечка только в её прямоугольнике (тело панели над карточками).
//   · НАСТОЯЩИЙ ЩЕЛЧОК (`SimulateClickAt`) по иконке товара под / рядом с окном покупки: выбор в магазине меняться не должен
//     (вторая ветка — покадровый опрос, `AsdaUi::IsCursorOverWidget`).
//   Сценарии: М1 премиум 6000 + 1559 · М2 премиум 6000 + панель 6296 (выбор варианта) · Б бустер 15300 + панель 15515 ·
//   С снаряжение 6800 + подтверждение · Н магазин NPC 1300 + нумпад количества.
//   Кадры — `Reports/batch18/shops/<язык>_<кадр>.png`.
//
// Консоль: `asda.Shop.ModalProbeSelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-МОДАЛ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaShopDblClick.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Layout/WidgetPath.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaModalProbe, Log, All);

namespace AsdaModalProbeLocal
{
	static const TCHAR* kWho = TEXT("asda.Shop.ModalProbeSelfTest");

	static UWidget* FindLive(UWorld* W, const FString& Name)
	{
		for (TObjectIterator<UWidget> It; It; ++It)
		{
			UWidget* X = *It;
			if (X && X->GetWorld() == W && X->GetParent() && X->GetName() == Name) { return X; }
		}
		return nullptr;
	}
	static UWidget* FindLivePrefix(UWorld* W, const FString& Prefix)
	{
		UWidget* Best = nullptr;
		for (TObjectIterator<UWidget> It; It; ++It)
		{
			UWidget* X = *It;
			if (X && X->GetWorld() == W && X->GetParent() && X->GetName().StartsWith(Prefix) && (!Best || X->GetName() < Best->GetName())) { Best = X; }
		}
		return Best;
	}
	using AsdaSelfTestInput::CenterOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Кнопочный тип Slate: нажатие по нему что-то делает.
	static bool IsClickableType(const FString& T)
	{
		return T == TEXT("SButton") || T == TEXT("SCheckBox") || T.Contains(TEXT("EditableText")) || T == TEXT("SComboButton")
			|| T == TEXT("SSpinBox") || T == TEXT("SSlider");
	}
}

struct FAsdaModalProbe
{
	int32 Ok = 0;
	int32 Fail = 0;
	TArray<TFunction<float(UWorld*)>> Steps;
	FString Lang;
	FString ShotDir;
	int32 NpcId = 0;       // NPC с товаром, который спрашивает количество
	int32 NpcCell = -1;    // ячейка такого товара
	int32 NpcCell2 = -1;   // вторая ячейка витрины — щелчок мимо нумпада
	int32 GoldWas = 0;

	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaModalProbe, Log, TEXT("САМОТЕСТ-МОДАЛ %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	void Shot(const TCHAR* Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaModalProbe, Log, TEXT("САМОТЕСТ-МОДАЛ кадр: %s"), *File);
	}
	template <class T> static T* Sub(UWorld* W) { UGameInstance* G = W ? W->GetGameInstance() : nullptr; return G ? G->GetSubsystem<T>() : nullptr; }

	bool Click(UWorld* W, const UWidget* X, const TCHAR* What)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		FVector2D C;
		if (!WM || !AsdaModalProbeLocal::CenterOf(X, C)) { Expect(false, FString::Printf(TEXT("щелчок: %s"), What), TEXT("нет живой геометрии")); return false; }
		WM->SimulateClickAt(C, 2, false);
		return true;
	}

	// СЕТКА. `Popup` — корень окна покупки (поддерево, которому оригинал отдаёт нажатие); `bModal` — модально ли оно у оригинала.
	void Sweep(UWorld* W, const UWidget* Popup, bool bModal, const FString& Name)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		if (!WM || !Popup || !FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport)
		{
			Expect(false, Name, TEXT("нет окна покупки / подсистемы окон"));
			return;
		}
		FSlateApplication& App = FSlateApplication::Get();
		const TSharedPtr<SWidget> PopupSlate = Popup->GetCachedWidget();
		FVector2D PMn, PMx;
		const bool bRect = AsdaUi::WidgetAbsRect(Popup, PMn, PMx);
		TSharedPtr<SViewport> VP = GEngine->GameViewport->GetGameViewportWidget();
		if (!VP.IsValid() || !PopupSlate.IsValid() || !bRect) { Expect(false, Name, TEXT("нет геометрии вьюпорта/окна покупки")); return; }
		const FGeometry& G = VP->GetCachedGeometry();
		const FVector2D VMn = G.GetAbsolutePosition();
		const FVector2D VMx = VMn + G.GetAbsoluteSize();
		int32 Points = 0, Leaks = 0, Eaten = 0;
		TMap<FString, int32> ByOwner;
		FString Example;
		for (float Y = VMn.Y + 4.f; Y < VMx.Y; Y += 8.f)
		{
			for (float X = VMn.X + 4.f; X < VMx.X; X += 8.f)
			{
				const FVector2D Pt(X, Y);
				const bool bInPopupRect = Pt.X >= PMn.X && Pt.X <= PMx.X && Pt.Y >= PMn.Y && Pt.Y <= PMx.Y;
				if (!bModal && !bInPopupRect) { continue; }   // немодальная панель: проверяем только её прямоугольник
				++Points;
				const FWidgetPath Path = App.LocateWindowUnderMouse(Pt, App.GetInteractiveTopLevelWindows());
				if (!Path.IsValid()) { continue; }
				// Нажатие получает ближайший к листу кнопочный виджет пути.
				TSharedPtr<SWidget> Target;
				bool bInPopup = false;
				for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
				{
					const TSharedRef<SWidget>& Sw = Path.Widgets[i].Widget;
					if (!Target.IsValid() && AsdaModalProbeLocal::IsClickableType(Sw->GetType().ToString())) { Target = Sw; }
					if (Sw == PopupSlate) { bInPopup = true; }
				}
				if (!Target.IsValid() || bInPopup) { continue; }
				FName Top, Lower;
				if (WM->FindLowerWindowUnderTopAt(Pt, Top, Lower)) { ++Eaten; continue; }
				// ★ Второе правило того же гейта (`asda.UI.PanelPress`): тело панели / модальный узел внутри одного окна — по листу пути.
				const TSharedPtr<SWidget> Leaf = Path.Widgets.Last().Widget;
				if (WM->IsPressCaughtByPanel(WM->DebugWindowOwningSlate(Leaf), Leaf, Pt)) { ++Eaten; continue; }
				++Leaks;
				const FString Owner = WM->DebugWindowOwningSlate(Target).ToString();
				ByOwner.FindOrAdd(Owner)++;
				if (Example.IsEmpty()) { Example = FString::Printf(TEXT("(%.0f,%.0f) %s окна «%s»"), Pt.X, Pt.Y, *Target->GetType().ToString(), *Owner); }
			}
		}
		FString Owners;
		for (const TPair<FString, int32>& Kv : ByOwner) { Owners += FString::Printf(TEXT(" %s:%d"), *Kv.Key, Kv.Value); }
		Expect(Leaks == 0, Name, FString::Printf(TEXT("точек %d, съедено гейтом %d, утечек %d%s%s%s"), Points, Eaten, Leaks,
			Owners.IsEmpty() ? TEXT("") : TEXT(" — по окнам:"), *Owners, Example.IsEmpty() ? TEXT("") : *(TEXT("; пример ") + Example)));
	}

	bool PickNpcQty(UGameInstance* GI)
	{
		UDataTable* DT = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcShopList.DT_NpcShopList"));
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!DT || !IT) { return false; }
		for (const TPair<FName, uint8*>& P : DT->GetRowMap())
		{
			const FAsdaNpcShopRow* Row = reinterpret_cast<const FAsdaNpcShopRow*>(P.Value);
			if (!Row) { continue; }
			int32 A = -1, B = -1;
			for (int32 i = 0; i < Row->ItemCodes.Num() && i < 36; ++i)
			{
				const FAsdaItemRecord* R = IT->GetItem(Row->ItemCodes[i]);
				if (!R || R->Price <= 0 || IT->IsSowelCode(Row->ItemCodes[i]) || R->ItemSubType == 32) { continue; }
				const bool bAsk = !(R->FlagWord176 == 0 && R->TradableFlag != 0) ? false : true;   // спросит количество
				if (bAsk && A < 0) { A = i; } else if (B < 0) { B = i; }
			}
			if (A >= 0 && B >= 0) { NpcId = Row->NpcId; NpcCell = A; NpcCell2 = B; return true; }
		}
		return false;
	}

	// Закрыть верхнюю модалку окна сообщений / подтверждения (без ответа).
	static void CloseTopModal(UWorld* W)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		if (!WM) { return; }
		for (int32 Guard = 0; Guard < 4; ++Guard)
		{
			UUserWidget* M = WM->GetTopModalWindow();
			if (!M) { break; }
			const FName Id = WM->DebugWindowOwningSlate(M->GetCachedWidget());
			if (Id.IsNone()) { break; }
			WM->CloseWindow(Id);
		}
		if (UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(W)) { if (Box->IsOpen()) { Box->Close(); } }
	}

	void Build()
	{
		using namespace AsdaModalProbeLocal;
		// ═══ М1. Премиум 6000 + модалка покупки 1559 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			if (P && P->IsOpen()) { P->CloseWindow(); }
			if (P) { P->OpenWindow(false); P->DebugSetGxp(1000000); }
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			// Карточка без вариантов: первая, у которой «Купить» открыла 1559, а не 6296.
			for (int32 c = 0; P && c < 9; ++c)
			{
				if (P->DebugCardCode(c) <= 0) { continue; }
				P->OnCardBuyClicked(c);
				UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(W);
				if (Box && Box->IsOpen()) { UE_LOG(LogAsdaModalProbe, Log, TEXT("САМОТЕСТ-МОДАЛ М1: 1559 открыта карточкой %d"), c); break; }
			}
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			UUserWidget* Box = WM ? WM->FindWindow(FName(TEXT("SHELLITEM_PURCHASE_BOX"))) : nullptr;
			Expect(Box && AsdaUi::IsShown(Box), TEXT("М1.0 модалка 1559 открыта"));
			Sweep(W, Box, /*bModal*/true, TEXT("М1.1 сетка: при 1559 нажатие не доходит до кнопок вне неё"));
			Shot(TEXT("m1_premium_1559"));
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			SelBefore = P ? P->DebugSelCard() : -2;
			Click(W, P ? P->DebugCardIcon(4) : nullptr, TEXT("иконка карточки 4 при 1559"));
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			Expect(P && P->DebugSelCard() == SelBefore && P->DebugSelCard() != 4, TEXT("М1.2 щелчок по карточке 4 при 1559 — выбор не меняется"),
				FString::Printf(TEXT("было %d, стало %d"), SelBefore, P ? P->DebugSelCard() : -2));
			CloseTopModal(W);
			return 0.5f;
		});
		// ═══ М2. Премиум 6000 + панель 6296 (выбор класса и уровня) ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			bool bFound = false;
			for (int32 Cat = 0; P && Cat < 8 && !bFound; ++Cat)
			{
				if (Cat == 1 || Cat == 4 || Cat == 7) { continue; }   // скрытые решением владельца
				P->SetCategory(Cat);
				for (int32 c = 0; c < 9 && !bFound; ++c)
				{
					if (P->DebugCardCode(c) <= 0) { continue; }
					P->OnCardBuyClicked(c);
					if (P->IsGiftPanelShown()) { bFound = true; UE_LOG(LogAsdaModalProbe, Log, TEXT("САМОТЕСТ-МОДАЛ М2: 6296 открыта, вкладка %d карточка %d"), Cat, c); break; }
					CloseTopModal(W);
				}
			}
			Expect(bFound, TEXT("М2.0 панель 6296 (варианты) открыта"));
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			UWidget* Panel = P ? P->DebugNode(TEXT("SHELLITEM_EQUIP_WIN_grp")) : nullptr;
			if (P && P->IsGiftPanelShown()) { Sweep(W, Panel, /*bModal*/true, TEXT("М2.1 сетка: при 6296 нажатие не доходит до кнопок вне неё")); }
			Shot(TEXT("m2_premium_6296"));
			SelBefore = P ? P->DebugSelCard() : -2;
			Click(W, P ? P->DebugCardIcon(5) : nullptr, TEXT("иконка карточки 5 при 6296"));
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaPremiumShopSubsystem* P = Sub<UAsdaPremiumShopSubsystem>(W);
			Expect(P && P->DebugSelCard() == SelBefore && P->IsGiftPanelShown(), TEXT("М2.2 щелчок по карточке 5 при 6296 — выбор не меняется, панель открыта"),
				FString::Printf(TEXT("было %d, стало %d, панель %d"), SelBefore, P ? P->DebugSelCard() : -2, (P && P->IsGiftPanelShown()) ? 1 : 0));
			CloseTopModal(W);
			if (P) { P->CloseWindow(); }
			return 0.5f;
		});

		// ═══ Б. Бустер 15300 + панель покупки 15515 ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W);
			if (B && B->IsOpen()) { B->CloseWindow(); }
			if (B) { B->OpenWindow(); }
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W);
			if (B) { B->OnCellBuyClicked(0); }
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W);
			UWidget* Panel = AsdaModalProbeLocal::FindLive(W, TEXT("BOOSTER_PUR_WIN_grp"));
			Expect(B && B->IsPurPanelShown() && Panel, TEXT("Б.0 панель покупки 15515 открыта"));
			Sweep(W, Panel, /*bModal*/false, TEXT("Б.1 сетка: нажатие по телу 15515 не доходит до кнопок под ней"));
			Shot(TEXT("b_booster_15515"));
			// Баг 7: панель 15515 рисуется выше полосы вкладок 15545 (пара «z слота, индекс ребёнка» у общего родителя больше).
			UWidget* Menu = AsdaModalProbeLocal::FindLive(W, TEXT("BOOSTER_SHOP_MENU_grp"));
			const UCanvasPanelSlot* PS = Panel ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr;
			const UCanvasPanelSlot* MS = Menu ? Cast<UCanvasPanelSlot>(Menu->Slot) : nullptr;
			const int32 PanIdx = (Panel && Panel->GetParent()) ? Panel->GetParent()->GetChildIndex(Panel) : -1;
			const int32 MenuIdx = (Menu && Menu->GetParent()) ? Menu->GetParent()->GetChildIndex(Menu) : -1;
			const bool bAbove = PS && MS && Panel->GetParent() == Menu->GetParent()
				&& (PS->GetZOrder() > MS->GetZOrder() || (PS->GetZOrder() == MS->GetZOrder() && PanIdx > MenuIdx));
			Expect(bAbove, TEXT("Б.2 панель покупки 15515 рисуется поверх полосы вкладок 15545 (баг 7)"),
				FString::Printf(TEXT("панель z=%d №%d, вкладки z=%d №%d"), PS ? PS->GetZOrder() : -99, PanIdx, MS ? MS->GetZOrder() : -99, MenuIdx));
			// Кадр на высоком экране: панель по центральному анкеру поднимается в строку вкладок (как на снимке владельца).
			if (GEngine) { GEngine->Exec(W, TEXT("r.SetRes 1600x1200w")); }
			return 1.2f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			Shot(TEXT("b_booster_tabs_tall"));
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			if (GEngine) { GEngine->Exec(W, TEXT("r.SetRes 1024x768w")); }
			return 1.0f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			if (UAsdaBoosterPanelSubsystem* Pv = Sub<UAsdaBoosterPanelSubsystem>(W)) { Pv->ClosePrevWindow(); }
			if (UAsdaBoosterShopSubsystem* B = Sub<UAsdaBoosterShopSubsystem>(W)) { B->CloseWindow(); }
			return 0.5f;
		});

		// ═══ С. Снаряжение 6800 + подтверждение покупки ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W);
			if (E && E->IsOpen()) { E->CloseWindow(); }
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W)) { GoldWas = Inv->GetGold(); Inv->SetGoldDisplay(100000000); }
			if (E) { E->OpenWindow(); }
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			if (UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W)) { E->OnCardBuyClicked(0); }
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			UUserWidget* M = WM ? WM->GetTopModalWindow() : nullptr;
			Expect(M != nullptr, TEXT("С.0 подтверждение покупки 6800 открыто (модалка)"));
			Sweep(W, M, /*bModal*/true, TEXT("С.1 сетка: при подтверждении нажатие не доходит до кнопок вне него"));
			Shot(TEXT("c_equip6800_confirm"));
			UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W);
			SelBefore = E ? E->DebugSelCard() : -2;
			Click(W, E ? E->DebugCardIcon(4) : nullptr, TEXT("иконка карточки 4 при подтверждении"));
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaEquipShopSubsystem* E = Sub<UAsdaEquipShopSubsystem>(W);
			Expect(E && E->DebugSelCard() == SelBefore && E->DebugSelCard() != 4, TEXT("С.2 щелчок по карточке 4 при подтверждении — выбор не меняется"),
				FString::Printf(TEXT("было %d, стало %d"), SelBefore, E ? E->DebugSelCard() : -2));
			CloseTopModal(W);
			if (E) { E->CloseWindow(); }
			return 0.5f;
		});

		// ═══ Н. Магазин NPC 1300 + нумпад количества ═══
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = Sub<UAsdaNpcShopSubsystem>(W);
			if (!S || !PickNpcQty(W->GetGameInstance())) { Expect(false, TEXT("Н.0 подготовка"), TEXT("нет NPC с товаром на количество")); return 0.1f; }
			S->OpenShop(5, NpcId);
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = Sub<UAsdaNpcShopSubsystem>(W);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			FVector2D C;
			if (S && WM && NpcCell >= 0 && AsdaModalProbeLocal::CenterOf(S->DebugGridIcon(NpcCell), C)) { AsdaSimDoubleClickAt(WM, C, false); }
			return 0.8f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			UUserWidget* Pad = WM ? WM->FindWindow(FName(TEXT("NUMPAD_WIN"))) : nullptr;
			Expect(WM && WM->IsNumPadOpen() && Pad, TEXT("Н.0 нумпад количества открыт"), FString::Printf(TEXT("NPC %d, ячейка %d"), NpcId, NpcCell));
			if (Pad) { Sweep(W, Pad, /*bModal*/true, TEXT("Н.1 сетка: при нумпаде нажатие не доходит до кнопок вне него")); }
			Shot(TEXT("n_npc1300_numpad"));
			UAsdaNpcShopSubsystem* S = Sub<UAsdaNpcShopSubsystem>(W);
			SelBefore = S ? S->DebugSelGridCell() : -2;
			Click(W, S ? S->DebugGridIcon(NpcCell2) : nullptr, TEXT("ячейка витрины при нумпаде"));
			return 0.6f;
		});
		Steps.Add([this](UWorld* W) -> float
		{
			UAsdaNpcShopSubsystem* S = Sub<UAsdaNpcShopSubsystem>(W);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			Expect(S && S->DebugSelGridCell() == SelBefore && WM && WM->IsNumPadOpen(), TEXT("Н.2 щелчок по витрине при нумпаде — выбор не меняется, нумпад открыт"),
				FString::Printf(TEXT("было %d, стало %d"), SelBefore, S ? S->DebugSelGridCell() : -2));
			if (WM && WM->IsNumPadOpen()) { WM->CloseNumPad(false); }
			if (S) { S->CloseShop(); }
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(W)) { Inv->SetGoldDisplay(GoldWas); }
			return 0.4f;
		});
	}
	int32 SelBefore = -2;
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaModalProbeSelfTest(
		TEXT("asda.Shop.ModalProbeSelfTest"),
		TEXT("asda.Shop.ModalProbeSelfTest [delay=N] [quit] — ОФЛАЙН-самотест пачки 18 (баг 2): нажатия под открытым окном покупки ")
		TEXT("магазинов 6000/15300/6800/1300 (сетка по живой геометрии и настоящие щелчки), кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaModalProbeLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (был отдельный цикл по аргументам с одной этой веткой)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaModalProbe> T = MakeShared<FAsdaModalProbe>();
				T->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
				T->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch18/shops")));
				T->Build();
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				TSharedRef<double> Next = MakeShared<double>(0.0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, Next, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					const double Now = FPlatformTime::Seconds();
					if (Now < *Next) { return true; }
					if (*Idx < T->Steps.Num()) { *Next = Now + T->Steps[(*Idx)++](W3); return true; }
					AsdaCVarSave::RestoreAll(Who);
					UE_LOG(LogAsdaModalProbe, Log, TEXT("САМОТЕСТ-МОДАЛ ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaModalProbe, Log, TEXT("САМОТЕСТ-МОДАЛ: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
