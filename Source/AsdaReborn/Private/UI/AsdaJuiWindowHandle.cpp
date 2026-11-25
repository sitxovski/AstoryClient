// Copyright (c) 2026 @sitxovski (Discord)
// Реализация каркасов окон JUI — см. заголовок `UI/AsdaJuiWindowHandle.h`.
//   Каждое тело — дословный перенос копии, указанной в комментарии; порядок вызовов не менялся.
#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"

// ═════════════ FAsdaJuiWindowHandle ═════════════

// Перенос `UAsdaRepairSubsystem::EnsureWidget` и восьми одинаковых копий (отчёт волны, §1).
EAsdaJuiEnsure FAsdaJuiWindowHandle::Ensure(UGameInstance* GI, TObjectPtr<UUserWidget>& Widget, bool bReady,
	TFunctionRef<void(UAsdaWindowSubsystem& WM)> DropStale,
	TFunctionRef<void(UAsdaWindowSubsystem& WM)> Setup,
	TFunctionRef<void()> OnLoadFailed,
	TFunctionRef<void(UAsdaWindowSubsystem& WM)> AfterHide) const
{
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return EAsdaJuiEnsure::NoWindowManager; }
	if (WM->IsWindowLive(Widget) && bReady) { return EAsdaJuiEnsure::AlreadyLive; }
	// Смена мира (травел/выход из PIE) — старый виджет принадлежит мёртвому миру: снести и пересоздать.
	if (Widget) { DropStale(*WM); }

	Widget = WM->OpenWindowByPath(WindowId, WbpPath, ZOrder, ESlateVisibility::SelfHitTestInvisible);
	if (!Widget)
	{
		OnLoadFailed();
		return EAsdaJuiEnsure::LoadFailed;
	}
	Setup(*WM);
	WM->SetWindowVisible(WindowId, false);
	AfterHide(*WM);
	return EAsdaJuiEnsure::Created;
}

// Короткая форма: шага после скрытия нет.
EAsdaJuiEnsure FAsdaJuiWindowHandle::Ensure(UGameInstance* GI, TObjectPtr<UUserWidget>& Widget, bool bReady,
	TFunctionRef<void(UAsdaWindowSubsystem& WM)> DropStale,
	TFunctionRef<void(UAsdaWindowSubsystem& WM)> Setup,
	TFunctionRef<void()> OnLoadFailed) const
{
	return Ensure(GI, Widget, bReady, DropStale, Setup, OnLoadFailed, [](UAsdaWindowSubsystem&) {});
}

// ★ Дедуп №2, В1-B (U22, 11.10.2026): «открыть при первом показе» — перенос копий `UAsdaSoulmateSubsystem::EnsurePopWindow` /
//   `OpenSearchWindow` / `OpenIntroWindow` / `OpenLinkWindow` / `ShowProposalWindow`, `UAsdaOtherCharaSubsystem::EnsureWidget`,
//   `UAsdaSystemOptionSubsystem::EnsureMenuWidget` / `EnsureOptionWidget` (порядок шагов — в объявлении).
EAsdaJuiEnsure FAsdaJuiWindowHandle::EnsureLazy(UAsdaWindowSubsystem& WM, TObjectPtr<UUserWidget>& Widget, bool bReady,
	ESlateVisibility RootVisibility,
	TFunctionRef<void()> BeforeOpen,
	TFunctionRef<void()> OnLoadFailed,
	TFunctionRef<void(UAsdaWindowSubsystem& WM)> Setup,
	FSimpleDelegate Closer) const
{
	if (WM.IsWindowLive(Widget) && bReady) { return EAsdaJuiEnsure::AlreadyLive; }
	BeforeOpen();
	Widget = WM.OpenWindowByPath(WindowId, WbpPath, ZOrder, RootVisibility);
	if (!Widget)
	{
		OnLoadFailed();
		return EAsdaJuiEnsure::LoadFailed;
	}
	Setup(WM);
	if (Closer.IsBound()) { WM.RegisterWindowCloser(WindowId, MoveTemp(Closer)); }
	return EAsdaJuiEnsure::Created;
}

// Перенос тела `UAsdaRepairSubsystem::RegisterZone` (и копий: Constellation, EquipShop, ItemOption, …).
void FAsdaJuiWindowHandle::RegisterFrameZone(UAsdaWindowSubsystem& WM, FName ZoneId, UObject* Owner,
	const TObjectPtr<UUserWidget>& Widget, const bool& bOpen, const TCHAR* PlaneFmt, int32 PlaneCount)
{
	WM.RegisterUiZone(ZoneId, FAsdaUiZoneRect::CreateWeakLambda(Owner,
		[&Widget, &bOpen, PlaneFmt, PlaneCount](FVector2D& Mn, FVector2D& Mx) -> bool
		{
			// Корпус окна — планы рамки (закон «rect только по узлам, которые окно и образуют»).
			if (!bOpen || !Widget) { return false; }
			return AsdaUi::FramePlanesAbsRect(Widget, PlaneFmt, PlaneCount, Mn, Mx);
		}));
}

// ═════════════ FAsdaPopupMenu ═════════════

// Перенос `AccumRect` (static) из `AsdaPlayerMenuSubsystem.cpp` ≡ `AsdaShowOffSubsystem.cpp`.
bool FAsdaPopupMenu::AccumRect(const UWidget* W, FVector2D& Mn, FVector2D& Mx, bool& bAny)
{
	if (!W || W->GetVisibility() == ESlateVisibility::Collapsed) { return bAny; }
	FVector2D P, Q;
	if (!AsdaUi::WidgetAbsRect(W, P, Q)) { return bAny; }
	Mn = FVector2D(FMath::Min(Mn.X, P.X), FMath::Min(Mn.Y, P.Y));
	Mx = FVector2D(FMath::Max(Mx.X, Q.X), FMath::Max(Mx.Y, Q.Y));
	bAny = true;
	return bAny;
}

// Перенос `UAsdaPlayerMenuSubsystem::GetMenuTopLeft` ≡ `UAsdaShowOffSubsystem::GetMenuTopLeft`.
bool FAsdaPopupMenu::GetTopLeft(const UUserWidget* Menu, FVector2D& Out)
{
	UPanelWidget* Root = Menu ? Cast<UPanelWidget>(Menu->GetRootWidget()) : nullptr;
	if (!Root) { return false; }
	bool bAny = false;
	FVector2D Mn(FLT_MAX, FLT_MAX);
	for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
	{
		if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Root->GetChildAt(i)))
		{
			const FMargin O = CS->GetOffsets();
			Mn = FVector2D(FMath::Min(Mn.X, O.Left), FMath::Min(Mn.Y, O.Top));
			bAny = true;
		}
	}
	if (!bAny) { return false; }
	Out = Mn;
	return true;
}

// Перенос `UAsdaPlayerMenuSubsystem::PlaceMenuAt`. У копии ShowOff нет `if (!Root) return;` — корень тот же,
//   что уже проверил `GetTopLeft` (иначе он вернул бы false), поэтому проверка недостижима и поведения не меняет.
void FAsdaPopupMenu::PlaceAt(UUserWidget* Menu, const FVector2D& TopLeft)
{
	FVector2D Cur;
	if (!GetTopLeft(Menu, Cur)) { return; }
	const FVector2D Delta = TopLeft - Cur;
	if (Delta.IsNearlyZero()) { return; }
	UPanelWidget* Root = Cast<UPanelWidget>(Menu->GetRootWidget());
	if (!Root) { return; }
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): сдвиг детей корня SPAN-безопасный (`AsdaUi::ShiftRootChildren`); было `Left/Top += d`,
	//   растянутый ребёнок поехал бы шириной. У меню 2000/28000, окон питомцев и подсказки 43300 SPAN-детей нет (перепись якорей) —
	//   на текущих ассетах результат прежний.
	AsdaUi::ShiftRootChildren(Root, Delta);
}

// Перенос общей части `GetMenuRect` меню 2000 (`bFallbackToWidgetRect = true`) и 28000 (`false`).
bool FAsdaPopupMenu::BodyRect(const UUserWidget* Menu, TArrayView<const TCHAR*> BodyNodes,
	bool bFallbackToWidgetRect, FVector2D& OutMin, FVector2D& OutMax)
{
	bool bAny = false;
	FVector2D Mn(FLT_MAX, FLT_MAX), Mx(-FLT_MAX, -FLT_MAX);
	for (const TCHAR* N : BodyNodes) { AccumRect(AsdaUi::FindNodeExact(Menu, N), Mn, Mx, bAny); }
	if (!bAny)
	{
		return bFallbackToWidgetRect ? AsdaUi::WidgetAbsRect(Menu, OutMin, OutMax) : false;
	}
	OutMin = Mn; OutMax = Mx;
	return true;
}

// Перенос WM-блока `CloseMenu` меню 2000 ≡ 28000.
void FAsdaPopupMenu::HideAndPopTopmost(UGameInstance* GI, FName WindowId, FName ClaimId)
{
	if (GI)
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(WindowId, false);
			WM->PopTopmost(ClaimId);
		}
	}
}

// ═════════════ FAsdaGaugeFill ═════════════

// Перенос `UAsdaItemTooltipSubsystem::ClipBarFill` (≡ `UAsdaMiniStateSubsystem::ClipFill` ≡ `ClipBar` окна
//   характеристик после их проверки `FAsdaMiniBarFill::IsValid`).
void FAsdaGaugeFill::Clip(UImage* Fill, float BaseLeft, float FullW, float Frac)
{
	UCanvasPanelSlot* S = Fill ? AsdaUi::SlotOf(Fill) : nullptr;
	if (!S || FullW <= 0.f) { return; }
	FMargin O = S->GetOffsets();
	O.Left  = BaseLeft;
	O.Right = FullW * FMath::Clamp(Frac, 0.f, 1.f);
	S->SetOffsets(O);
}

// ═════════════ AsdaShopUi ═════════════

namespace AsdaShopUi
{
	// Перенос `UAsdaEquipShopSubsystem::MakeLabelLike` ≡ `UAsdaWarShopSubsystem::MakeLabelLike`.
	UTextBlock* MakeLabelLike(UUserWidget* Host, UWidget* Ref, const TCHAR* NewName,
		const UTextBlock* FontDonor, bool bRightAlign)
	{
		if (!Ref || !Host || !Host->WidgetTree) { return nullptr; }
		UPanelWidget* Parent = Ref->GetParent();
		if (!Parent) { return nullptr; }
		UTextBlock* Tb = Host->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(NewName));
		if (!Tb) { return nullptr; }
		Tb->SetAutoWrapText(false);                                  // закон `ui-autowrap-canvas-gotcha`
		Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // закон `ui-button-hover-click-rule`
		if (FontDonor) { Tb->SetFont(FontDonor->GetFont()); }
		if (bRightAlign) { Tb->SetJustification(ETextJustify::Right); }
		Parent->AddChild(Tb);
		if (const UCanvasPanelSlot* From = AsdaUi::SlotOf(Ref))
		{
			if (UCanvasPanelSlot* To = AsdaUi::SlotOf(Tb)) { AsdaUi::CopyCanvasGeometry(From, To); }
		}
		return Tb;
	}

	// Перенос `UAsdaHairShopSubsystem::RefreshPageLabels` ≡ `UAsdaPremiumShopSubsystem::RefreshPageLabels`.
	void SetPageLabels(UTextBlock* TxtPage, UTextBlock* TxtPages, int32 Page, int32 MaxPage)
	{
		if (TxtPage)  { TxtPage->SetText(FText::FromString(FString::Printf(TEXT("%d"), Page + 1))); }
		if (TxtPages) { TxtPages->SetText(FText::FromString(FString::Printf(TEXT("%d"), MaxPage + 1))); }
	}

	// Перенос `UAsdaPremiumShopSubsystem::TickTooltip` ≡ `UAsdaBoosterShopSubsystem::TickTooltip`.
	void TickCardTooltip(UGameInstance* GI, int32& TipIdx, int32 OverIdx, EAsdaTipOwner Owner,
		TFunctionRef<int32(int32)> ItemCodeAt)
	{
		UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		if (!Tip) { return; }
		if (OverIdx == TipIdx) { return; }
		TipIdx = OverIdx;
		if (OverIdx < 0) { Tip->HideTooltipFor(Owner); return; }
		Tip->ShowShopSlotTooltip(ItemCodeAt(OverIdx), 1, /*bSellMode*/false, Owner);
	}

	// Перенос `OnBalanceClicked` бустер-шопа ≡ премиум-магазина ≡ парикмахерской.
	void QueryPremiumBalance(UGameInstance* GI)
	{
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->SendPremiumBalanceQuery5466(Net->SelectedCharId);
		}
	}

	// Перенос общей части `ApplyPanelHeight` магазина снаряжения (6983) ≡ премиум-магазина (6083).
	bool ApplyShowcaseHeight(const TArray<TObjectPtr<UWidget>>& PanelPlanes,
		TFunctionRef<int32()> ByResFn, int32 VisibleCards, int32& InOutApplied, FShowcaseHeight& Out)
	{
		if (PanelPlanes.Num() != 9) { return false; }
		for (const TObjectPtr<UWidget>& W : PanelPlanes) { if (!W) { return false; } }

		Out.ByRes = ByResFn();
		Out.Shown = VisibleCards > 0 ? VisibleCards : Out.ByRes;
		Out.Rows  = FMath::Max(Out.ByRes, FMath::DivideAndRoundUp(Out.Shown, 3) * 3);
		Out.NewH  = 98 * (Out.Rows / 3) + 48;
		if (Out.NewH == InOutApplied) { return false; }

		UCanvasPanelSlot* S0 = AsdaUi::SlotOf(PanelPlanes[0].Get());   // верх-лево
		UCanvasPanelSlot* S3 = AsdaUi::SlotOf(PanelPlanes[3].Get());   // середина-лево
		UCanvasPanelSlot* S6 = AsdaUi::SlotOf(PanelPlanes[6].Get());   // низ-лево
		if (!S0 || !S3 || !S6) { return false; }

		const float TopH = S0->GetSize().Y;          // высота верхнего ряда 9-slice (из скина)
		const float BotH = S6->GetSize().Y;          // высота нижнего ряда
		Out.MidH = FMath::Max(1.f, (float)Out.NewH - TopH - BotH);
		const float Top0 = S0->GetPosition().Y;      // верх рамки (локаль группы) — не двигаем
		Out.BotY = Top0 + TopH + Out.MidH;

		// ★ 09.11.2026 (пачка 19, баг 4; `asda.UI.FrameKeepOverlap`): перекрытие кусков из ассета (шаг 24.10: середина на 1 выше и
		//   ниже соседних рядов) сохраняется — прежде средний ряд оставался сдвинутым вверх, а высоту получал встык, и до нижнего
		//   ряда не хватало 1 px (просвет витрины 6083/6983 на дробной позиции окна).
		const bool bKeep = AsdaUi::FrameKeepOverlapOn();
		const float MidY = Top0 + TopH;
		const float OldBotY = S6->GetPosition().Y;
		for (int32 Pl = 3; Pl <= 5; ++Pl)
		{
			if (UCanvasPanelSlot* Sl = AsdaUi::SlotOf(PanelPlanes[Pl].Get()))
			{
				FVector2D Ps = Sl->GetPosition();
				FVector2D Sz = Sl->GetSize();
				const float Up   = bKeep ? FMath::Max(0.f, MidY - Ps.Y) : 0.f;
				const float Down = bKeep ? FMath::Max(0.f, (Ps.Y + Sz.Y) - OldBotY) : 0.f;
				if (bKeep) { Ps.Y = MidY - Up; Sl->SetPosition(Ps); }
				Sz.Y = Out.MidH + Up + Down; Sl->SetSize(Sz);
			}
		}
		for (int32 Pl = 6; Pl <= 8; ++Pl)
		{
			if (UCanvasPanelSlot* Sl = AsdaUi::SlotOf(PanelPlanes[Pl].Get()))
			{
				FVector2D Ps = Sl->GetPosition(); Ps.Y = bKeep ? Out.BotY + (Ps.Y - OldBotY) : Out.BotY; Sl->SetPosition(Ps);
			}
		}
		InOutApplied = Out.NewH;
		return true;
	}

	// Перенос `ShowCardSelection` магазина снаряжения ≡ военного магазина (без проверки индекса — она в окне).
	void SetSelectionVisible(const TArray<TObjectPtr<UWidget>>& Parts, bool bVisible)
	{
		const ESlateVisibility V = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		for (const TObjectPtr<UWidget>& W : Parts) { if (W) { W->SetVisibility(V); } }
	}
}
