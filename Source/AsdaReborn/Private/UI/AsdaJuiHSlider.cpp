// Copyright (c) 2026 @sitxovski (Discord)
// AsdaJuiHSlider — горизонтальная полоса-ползунок JUI и раскладка 9-slice (разбор — в заголовке).
#include "UI/AsdaJuiHSlider.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R3: прямое включение (раньше приходило транзитивно)

#include "UI/AsdaUiHelpers.h"   // FAsdaMouseFrame, ParsePlaneName, SlotRect/SetSlotPos/SetSlotRect, WidgetAbsRect, IsCursorOverWidget
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"

// Планы контрола `Idc`: `<Idc>_<jui>_<plane>` (закон `jui-importer-widget-naming`), по номеру плана.
static void AsdaJuiCollectPlanes(UUserWidget* Host, const FString& Idc, TArray<UImage*>& Out)
{
	Out.Reset();
	if (!Host || !Host->WidgetTree) { return; }
	Host->WidgetTree->ForEachWidget([&Out, &Idc](UWidget* W)
	{
		UImage* Img = Cast<UImage>(W);
		if (!Img) { return; }
		FString NodeIdc; int32 Jui = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(Img->GetName(), NodeIdc, Jui, Plane) || NodeIdc != Idc || Plane < 0 || Plane > 15) { return; }
		if (Out.Num() <= Plane) { Out.SetNumZeroed(Plane + 1); }
		Out[Plane] = Img;
	});
}

bool FAsdaJuiHSlider::Bind(UUserWidget* Host, const FString& Idc, int32 InMin, int32 InMax)
{
	TArray<UImage*> P;
	AsdaJuiCollectPlanes(Host, Idc, P);
	*this = FAsdaJuiHSlider();
	Min = InMin; Max = InMax; Value = InMin;
	if (P.Num() < 4 || !P[0] || !P[3]) { return false; }
	Track = P[0]; BtnMinus = P[1]; BtnPlus = P[2]; Thumb = P[3];
	FVector2D Pos, Sz;
	if (AsdaUi::SlotRect(P[0], Pos, Sz)) { TrackPos = Pos; TrackSize = Sz; }
	if (AsdaUi::SlotRect(P[3], Pos, Sz)) { ThumbSize = Sz; }
	// Стрелки лежат ПОВЕРХ концов жёлоба (импортёр тянет `_0` на всю ширину контрола): ход бегунка — между стрелками.
	if (UImage* L = P[1]) { if (AsdaUi::SlotRect(L, Pos, Sz)) { TrackPos.X += Sz.X; TrackSize.X -= Sz.X; } }
	if (UImage* R = P[2]) { if (AsdaUi::SlotRect(R, Pos, Sz)) { TrackSize.X -= Sz.X; } }
	return true;
}

void FAsdaJuiHSlider::LayoutThumb() const
{
	UImage* T = Thumb.Get();
	if (!T || TrackSize.X <= 0.f) { return; }
	const float Span = FMath::Max(TrackSize.X - ThumbSize.X, 1.f);
	const float Frac = (Max > Min) ? (float)(Value - Min) / (float)(Max - Min) : 0.f;
	FVector2D P, Sz;
	if (!AsdaUi::SlotRect(T, P, Sz)) { return; }
	AsdaUi::SetSlotPos(T, FVector2D(TrackPos.X + Span * FMath::Clamp(Frac, 0.f, 1.f), P.Y));
}

bool FAsdaJuiHSlider::SetValue(int32 NewValue)
{
	const int32 V = FMath::Clamp(NewValue, Min, Max);
	const bool bChanged = (V != Value);
	Value = V;
	LayoutThumb();
	return bChanged;
}

bool FAsdaJuiHSlider::Tick(const FAsdaMouseFrame& M)
{
	UImage* T = Track.Get();
	if (!T || !AsdaUi::IsShown(T)) { bDragging = false; return false; }
	if (M.bLmbDown)
	{
		if (BtnMinus.IsValid() && AsdaUi::IsCursorOverWidget(BtnMinus.Get())) { SetValue(Value - kLineStep); return true; }
		if (BtnPlus.IsValid() && AsdaUi::IsCursorOverWidget(BtnPlus.Get()))   { SetValue(Value + kLineStep); return true; }
		if (Thumb.IsValid() && AsdaUi::IsCursorOverWidget(Thumb.Get())) { bDragging = true; }
		else if (AsdaUi::IsCursorOverWidget(T))
		{
			FVector2D TMin, TMax;
			if (Thumb.IsValid() && AsdaUi::WidgetAbsRect(Thumb.Get(), TMin, TMax))
			{
				SetValue(Value + ((M.Cursor.X < TMin.X) ? -kPageStep : kPageStep));
				return true;
			}
		}
	}
	if (M.bLmbUp) { bDragging = false; }
	if (!bDragging) { return false; }
	FVector2D Mn, Mx, BMn, BMx;
	if (!AsdaUi::WidgetAbsRect(T, Mn, Mx)) { return false; }
	const float ThumbW = (Thumb.IsValid() && AsdaUi::WidgetAbsRect(Thumb.Get(), BMn, BMx)) ? (BMx.X - BMn.X) : 0.f;
	// Экранный ход бегунка — между стрелками (их края на экране), как ход по слоту в `LayoutThumb`.
	float Left = Mn.X, Right = Mx.X;
	FVector2D AMn, AMx;
	if (BtnMinus.IsValid() && AsdaUi::WidgetAbsRect(BtnMinus.Get(), AMn, AMx)) { Left = AMx.X; }
	if (BtnPlus.IsValid() && AsdaUi::WidgetAbsRect(BtnPlus.Get(), AMn, AMx))   { Right = AMn.X; }
	const float W = FMath::Max((Right - Left) - ThumbW, 1.f);
	const float Frac = FMath::Clamp((M.Cursor.X - Left - ThumbW * 0.5f) / W, 0.f, 1.f);
	SetValue(Min + FMath::RoundToInt(Frac * (float)(Max - Min)));
	return true;
}

bool AsdaJuiNine::Collect(UUserWidget* Host, const FString& Idc, TArray<TWeakObjectPtr<UImage>>& Out)
{
	TArray<UImage*> P;
	AsdaJuiCollectPlanes(Host, Idc, P);
	Out.Reset();
	if (P.Num() < 9) { return false; }
	for (int32 i = 0; i < 9; ++i) { if (!P[i]) { Out.Reset(); return false; } Out.Add(P[i]); }
	return true;
}

bool AsdaJuiNine::MeasureCorners(const TArray<TWeakObjectPtr<UImage>>& Planes, FVector2D& OutTL, FVector2D& OutBR)
{
	if (Planes.Num() < 9 || !Planes[0].IsValid() || !Planes[8].IsValid()) { return false; }
	FVector2D P, Sz;
	if (!AsdaUi::SlotRect(Planes[0].Get(), P, Sz)) { return false; }
	OutTL = Sz;
	if (!AsdaUi::SlotRect(Planes[8].Get(), P, Sz)) { return false; }
	OutBR = Sz;
	return true;
}

void AsdaJuiNine::Layout(const TArray<TWeakObjectPtr<UImage>>& Planes, const FVector2D& CornerTL, const FVector2D& CornerBR,
	const FVector2D& Pos, const FVector2D& Size)
{
	if (Planes.Num() < 9) { return; }
	const float X0 = Pos.X, X1 = Pos.X + CornerTL.X, X2 = Pos.X + FMath::Max(Size.X - CornerBR.X, CornerTL.X);
	const float Y0 = Pos.Y, Y1 = Pos.Y + CornerTL.Y, Y2 = Pos.Y + FMath::Max(Size.Y - CornerBR.Y, CornerTL.Y);
	const float Xs[3] = { X0, X1, X2 };
	const float Ws[3] = { CornerTL.X, FMath::Max(X2 - X1, 0.f), CornerBR.X };
	const float Ys[3] = { Y0, Y1, Y2 };
	const float Hs[3] = { CornerTL.Y, FMath::Max(Y2 - Y1, 0.f), CornerBR.Y };
	for (int32 R = 0; R < 3; ++R)
	{
		for (int32 C = 0; C < 3; ++C)
		{
			if (UImage* I = Planes[R * 3 + C].Get()) { AsdaUi::SetSlotRect(I, FVector2D(Xs[C], Ys[R]), FVector2D(Ws[C], Hs[R])); }
		}
	}
}
