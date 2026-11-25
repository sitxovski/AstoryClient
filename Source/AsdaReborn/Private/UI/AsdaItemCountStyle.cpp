// Copyright (c) 2026 @sitxovski (Discord)
// Обводка числа количества в ячейке предмета (код-путь). Разбор и решение владельца — в `UI/AsdaItemCountStyle.h`.
#include "UI/AsdaItemCountStyle.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR — дев-ключ; в Shipping константа с умолчанием
#include "HAL/IConsoleManager.h"
#include "Components/TextBlock.h"
#include "UObject/UnrealType.h"

// [ОРИГИНАЛ] 26.10.2026: эталон ставит числу сумки и хотбара обводку (поле 926 = 0x800000, item_inventory.c:35253, ui_core.c:65971);
//   порт её не рисовал (ошибка чтения). Владелец: чёрная 1 px.
static ASDA_DEV_CVAR(int32, CVarItemCountOutline,
	TEXT("asda.UI.ItemCountOutline"),
	1,
	TEXT("Обводка числа количества в ячейках сумки и панели быстрого доступа: N > 0 — чёрная обводка N px (умолчание 1, "
	     "как эталон, решение владельца 26.10.2026); 0 — как было в порте (без обводки). Действует при следующей раскладке окна. "
	     "Обводку окон, где шрифт числа задан ассетом, ставит шаг Tools/asda_ui_item_count_outline.py."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] 08.11.2026 (пачка 18, баг 6): решение владельца — числа количества на иконках сумки на 10% крупнее.
static ASDA_DEV_CVAR(int32, CVarItemCountFontScale,
	TEXT("asda.UI.ItemCountFontScale"),
	110,
	TEXT("Кегль числа количества в ячейках сумки (и надетых слотов окна сумки), в процентах от базы: 110 — на 10 процентов крупнее (решение "
	     "владельца 08.11.2026), 100 — прежний кегль. Действует при следующей раскладке окна. Окна, где кегль задан ассетом, "
	     "правит шаг Tools/asda_ui_item_count_font.py."),
	ECVF_Default);

float AsdaItemCount::FontSize(float BaseSize)
{
	const int32 Pct = FMath::Clamp(CVarItemCountFontScale.GetValueOnGameThread(), 50, 200);
	return FMath::Max(1.f, BaseSize * (float)Pct / 100.f);
}

int32 AsdaItemCount::OutlinePx(int32 LegacyPx)
{
	const int32 Px = CVarItemCountOutline.GetValueOnGameThread();
	return Px > 0 ? FMath::Min(Px, 4) : FMath::Max(0, LegacyPx);
}

FLinearColor AsdaItemCount::OutlineColor()
{
	return FLinearColor(0.f, 0.f, 0.f, 1.f);
}

void AsdaItemCount::ApplyOutline(FSlateFontInfo& Font, int32 LegacyPx, const FLinearColor& LegacyColor)
{
	const bool bFix = CVarItemCountOutline.GetValueOnGameThread() > 0;
	Font.OutlineSettings.OutlineSize = OutlinePx(LegacyPx);
	Font.OutlineSettings.OutlineColor = bFix ? OutlineColor() : LegacyColor;
	Font.OutlineSettings.bApplyOutlineToDropShadows = false;
}

void AsdaItemCount::CompensateGlyph(UTextBlock* Tb, int32 Px)
{
	if (!Tb) { return; }
	// Выравнивание — защищённое свойство `UTextLayoutWidget::Justification` (геттера нет): читаем отражением.
	int32 J = (int32)ETextJustify::Center;
	if (const FProperty* P = UTextLayoutWidget::StaticClass()->FindPropertyByName(TEXT("Justification")))
	{
		if (const uint8* V = P->ContainerPtrToValuePtr<uint8>(Tb)) { J = *V; }
	}
	float Dx = 0.f;
	if (Px > 0)
	{
		if (J == (int32)ETextJustify::Left || J == (int32)ETextJustify::InvariantLeft) { Dx = -(float)Px; }
		else if (J == (int32)ETextJustify::Right || J == (int32)ETextJustify::InvariantRight) { Dx = (float)Px; }
	}
	Tb->SetRenderTranslation(FVector2D(Dx, Px > 0 ? -(float)Px : 0.f));
}
