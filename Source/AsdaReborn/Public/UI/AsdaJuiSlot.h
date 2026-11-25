// Copyright (c) 2026 @sitxovski (Discord)
// AsdaJuiSlot.h — ЕДИНАЯ схема слота UMG, совпадающая со схемой импортёра JUI.
//
// ★★★ ЗАКОН (память jui-align-anchors + регрессия живого теста 2026-08-03).
// Импортёр `Tools/asda_jui_generic_umg.py::_apply_anchor` переводит JUI-геометрию в
// «доля экрана + пиксельный оффсет»:
//     не-span ось → POINT-анкор в (fx|fy), offset = poz − f·база, размер в пикселях базы;
//     span   ось → анкор [poz/база .. (poz+size)/база], offsets 0.
// Все контролы чат-колонки (окна 2500 и 2570) в JUI: X = POINT fx 0.0 (левый край),
// Y = POINT fy 1.0 (нижний край) — прогон Tools/asda_jui_parse.py::compute_anchors.
//
// ЧТО БЫЛО, ЕСЛИ СХЕМЫ РАСХОДЯТСЯ. Наши собственные панели ставились SPAN-анкором
// (Y = 516/768 … 573/768). На базовой высоте 768 обе схемы дают один и тот же y, а дальше
// расходятся: 1080 → 102 px, 1440 → 220 px. Список «отрывался» от своей плиты и выглядел
// отдельным окном. Поэтому рантайм-слоты ОБЯЗАНЫ строиться этой функцией, а не FAnchors вручную.
#pragma once

#include "CoreMinimal.h"
#include "Components/CanvasPanelSlot.h"

namespace AsdaJui
{
	// База раскладки JUI (разрешение эталонного клиента).
	static constexpr float kBaseW = 1024.f;
	static constexpr float kBaseH = 768.f;
	// Доли экрана чат-колонки: левый край по X, нижний край по Y.
	static constexpr float kFxLeft   = 0.f;
	static constexpr float kFyBottom = 1.f;

	// Поставить слот по схеме импортёра: POINT-анкор (Fx,Fy), выравнивание 0/0,
	//   offsets = (X − Fx·1024, Y − Fy·768, Ширина, Высота) — X/Y/W/H в базовых пикселях.
	inline void SetPointSlot(UCanvasPanelSlot* CS, float Fx, float Fy, float X, float Y, float W, float H)
	{
		if (!CS) { return; }
		CS->SetAnchors(FAnchors(Fx, Fy, Fx, Fy));
		CS->SetAlignment(FVector2D(0.f, 0.f));
		CS->SetOffsets(FMargin(X - Fx * kBaseW, Y - Fy * kBaseH, W, H));
	}

	// Тот же слот для чат-колонки (левый-нижний угол) — самый частый случай.
	inline void SetChatSlot(UCanvasPanelSlot* CS, float X, float Y, float W, float H)
	{
		SetPointSlot(CS, kFxLeft, kFyBottom, X, Y, W, H);
	}
}
