// Copyright (c) 2026 @sitxovski (Discord)
// AsdaJuiHSlider — ★ ГОРИЗОНТАЛЬНАЯ ПОЛОСА-ПОЛЗУНОК JUI (контрол типа `ScrollBar`, `ScrollBarType = 0`) И РАСКЛАДКА 9-slice.
//   [ПОРТ] 01.11.2026 (сессия clan-window): заведено для редактора герба клана (страница 36100 окна 6400 — девять ползунков RGB
//   `SideBar_Red/Green/Blue`) и его всплывающей подсказки 14900 (`Notice_Tip`, `ResizeWindow` из девяти планов).
//
// ЧТО ДАЁТ ИМПОРТЁР. Для `ScrollBar` интерактивного виджета нет: четыре `UImage` `<idc>_<jui>_0…3` — `_0` жёлоб, `_1` стрелка «−»,
//   `_2` стрелка «+», `_3` бегунок (стоит в точке шаблона; двигает его рантайм — как и в оригинале, где `CScrollBar` сам кладёт
//   бегунок по `uiScrollbarSetPos`). Модель ввода и шаги — те же, что у полос окна системных настроек
//   (`UAsdaSystemOptionSubsystem::TickSlider`: стрелка ±6, щелчок по жёлобу ±50, перетаскивание бегунка, `CScrollBar` +155/+156).
//   ⚠ Копия той модели в окне настроек осталась своей (ниже — та же логика отдельной точкой); свести их — отдельная задача.
//
// 9-slice. Эталон `uiPanel9SliceLayout@0x13005D0`: после `SetSize(w, h)` девять кусков раскладываются заново — углы своего
//   размера, бордюры тянутся по одной оси, середина по обеим. В порте планы расплющены в девять `UImage` с абсолютными слотами,
//   поэтому раскладку делает `AsdaJuiNine::Layout`.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UUserWidget;
class UImage;
class UWidget;
struct FAsdaMouseFrame;

// Одна полоса-ползунок. Виджеты держатся слабо: их владелец — дерево окна (оно пересоздаётся при смене карты).
struct ASDAREBORN_API FAsdaJuiHSlider
{
	TWeakObjectPtr<UImage> Track;     // план 0 — жёлоб
	TWeakObjectPtr<UImage> BtnMinus;  // план 1 — стрелка «−»
	TWeakObjectPtr<UImage> BtnPlus;   // план 2 — стрелка «+»
	TWeakObjectPtr<UImage> Thumb;     // план 3 — бегунок
	int32 Min = 0;                    // `uiScrollbarSetRange` — нижняя граница
	int32 Max = 255;                  // верхняя граница
	int32 Value = 0;                  // `uiScrollbarSetPos` — текущее значение
	FVector2D TrackPos = FVector2D::ZeroVector;   // слот жёлоба (снят при привязке, координаты родительского канваса)
	FVector2D TrackSize = FVector2D::ZeroVector;  // размер жёлоба
	FVector2D ThumbSize = FVector2D::ZeroVector;  // размер бегунка
	bool bDragging = false;           // тянется ли бегунок

	static constexpr int32 kLineStep = 6;    // стрелка (CScrollBar +155, как у полос окна настроек)
	static constexpr int32 kPageStep = 50;   // щелчок по жёлобу (CScrollBar +156)

	// Найти четыре плана контрола `Idc` в окне (`<Idc>_<jui>_<plane>`) и снять геометрию. false — планов нет.
	bool Bind(UUserWidget* Host, const FString& Idc, int32 InMin, int32 InMax);
	bool IsBound() const { return Track.IsValid(); }
	// Поставить значение (с зажимом) и переложить бегунок. Возврат — изменилось ли значение.
	bool SetValue(int32 NewValue);
	// Кадр мыши. true — полоса забрала ввод (значение могло измениться — сравнить с прежним).
	bool Tick(const FAsdaMouseFrame& M);
	// Переложить бегунок по `Value` (`uiScrollbarLayoutThumb`).
	void LayoutThumb() const;
};

namespace AsdaJuiNine
{
	// Девять планов `<Idc>_<jui>_0…8` контрола `ResizeWindow` (построчно: 0,1,2 — верх; 3,4,5 — середина; 6,7,8 — низ).
	//   false — планов не девять.
	ASDAREBORN_API bool Collect(UUserWidget* Host, const FString& Idc, TArray<TWeakObjectPtr<UImage>>& Out);
	// Разложить девять планов в прямоугольник `Pos`/`Size` (координаты родительского канваса): углы — размера, снятого
	//   с исходной раскладки импортёра (`CornerTL`/`CornerBR`), бордюры и середина тянутся. Порт `uiPanel9SliceLayout`.
	ASDAREBORN_API void Layout(const TArray<TWeakObjectPtr<UImage>>& Planes, const FVector2D& CornerTL, const FVector2D& CornerBR,
		const FVector2D& Pos, const FVector2D& Size);
	// Размеры углов из ТЕКУЩЕЙ раскладки планов (план 0 — левый верхний, план 8 — правый нижний). false — планов нет.
	ASDAREBORN_API bool MeasureCorners(const TArray<TWeakObjectPtr<UImage>>& Planes, FVector2D& OutTL, FVector2D& OutBR);
}
