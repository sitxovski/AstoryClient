// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMinimapSubsystem_Draw.cpp — ★ R3-c 12.11.2026 (разрез AsdaMinimapSubsystem.cpp, дословный перенос): Tick, флаг города, метки, конус камеры, обновление карт, цели квестов, наведение, подписи.
//   Общее для частей — в AsdaMinimapSubsystem_Private.h; шапка и реверс — в AsdaMinimapSubsystem.cpp.
#include "UI/AsdaMinimapSubsystem.h"



#include "AsdaMinimapSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMinimapSubsystem*.cpp
#include "AsdaPerf.h"                         // ★ R4-b 14.11.2026: области замера времени игрового потока
#include "Internationalization/Internationalization.h"   // ★ R4-b 14.11.2026: культура чисел подписей (кэш `LblCulture`)

// ★ R4-b 14.11.2026 [ПОРТ] (R4, доля b, B1): подписи мини-карты 1702/1703 (X, Y, имя локации) каждый кадр искали три узла обходом дерева окна,
//   собирали два числа и имя из таблицы `Map_info` и писали все три подписи. Теперь узлы — кэш по виджету, числа и имя — только при
//   смене (тайла, карты, языка, культуры; пустое имя пересчитывается каждый кадр, как прежде). Текст подписей тот же.
// ★ R4-b закрытие (15.11.2026): ключ `asda.Perf.MinimapLabels` — старый путь и ключ удалены — только новое (решение владельца).

// ★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (по просьбе юзера): сокращать слово «канал» до «кан.» в подписи
//    переключателя каналов мини-карты (контрол 1717). 0 = как в оригинале (полное слово из нотиса 15200).
// ★ 30.10.2026 [ОРИГИНАЛ] справка кнопок мини-карты 1704/1706/1707/1708/1719 (`TickHelpTips`). 0 = прежний порт (без справки).
// ★ [ОРИГИНАЛ] 06.11.2026: курсор 18 «идти» над полем мини-карты (`MiniMap_Render@0xB4FCC0`, world_map.c:2553-2572 и 2781-2800).
//   0 — прежнее: над мини-картой курсор мира (порт этой ветки эталона не имел, курсор менялся только на карте мира).
static ASDA_DEV_CVAR(int32, CVarAsdaMinimapHoverCursor,
	TEXT("asda.Minimap.HoverCursor"), 1,
	TEXT("1 — над полем мини-карты курсор «идти» (как оригинал); 0 — прежнее (курсор мира)."),
	ECVF_Default);

// ★ Решение владельца 09.10.2026 (п. 3): метки мини-карты — тайл → точка БЕЗ +½ тайла, как у эталона во ВСЕХ
//   списках режимов 2/3 (`uiSlotIndexToScreenPos@0xB4DBC0` / `sub_B4DB50`: x = base − 259|133 + t·k, y = t·k + 36):
//   отряд `minimap_DrawPartyMarkers` (world_map.c:3142), соулмейт `minimapDrawTargetMarker` (:3428), квест-NPC
//   `uiTextRenderGlyphQuads` (ui_core.c:5488, world/100 → (int) → t·k), рыбалка `minimapDrawMarkerList` (:3279),
//   точка автобега `minimapDrawGroundTargetMarkerQuad` (:2657-2675, tile/2 без половины). Откат: 1 = прежний порт (+½).
static ASDA_DEV_CVAR(int32, CVarMinimapLegacyHalfTile,
	TEXT("asda.Minimap.LegacyHalfTile"),
	0,
	TEXT("Метки мини-карты (отряд/клан/соулмейт/квесты/точка автобега/рыбалка): 0 = как эталон, тайл·шаг без половины; "
	     "1 = прежний порт, центр тайла (100·t + 50) — сдвиг на полклетки вправо-вниз."),
	ECVF_Default);

static void ShiftSlot(UWidget* W, float DX, float DY)
{
	// ★ Схему анкора НЕ меняем (закон jui-align-anchors). ★ Дедуп №2, В2-B (U15): тело — общая точка
	//   `AsdaUi::ShiftCanvasSlot` (то же правило POINT/SPAN, что было здесь), поведение то же.
	AsdaUi::ShiftCanvasSlot(W, FVector2D(DX, DY));
}

// ── Тик ──────────────────────────────────────────────────────────────────────────────────────────
void UAsdaMinimapSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Minimap");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	if (!LM || LM->CurrentMapId < 0) { return; }

	AAsdaPlayerCharacter* Player = nullptr;
	const UAsdaWalkGrid* Grid = GetGrid(Player);
	if (!Player || !Grid) { HelpTip.Hide(GI, EAsdaTipOwner::Minimap); ReleaseMiniHoverCursor(); return; }          // ждём павна и сетки карты (как ZoneSubsystem)

	// ★ Межкартовый автопуть ведётся ДО проверки виджетов: в эталоне он живёт в главном тике
	//   игры, а не в отрисовке мини-карты, и обязан работать при закрытом окне карты.
	TickMapTravel();

	if (!EnsureWidgets()) { HelpTip.Hide(GI, EAsdaTipOwner::Minimap); ReleaseMiniHoverCursor(); return; }

	if (LM->CurrentMapId != MapId)
	{
		MapId = LM->CurrentMapId;
		LastTileX = LastTileZ = INT32_MIN;
		LoadMapTextures();
		UpdateLabels();
		UpdateCampWarButton();      // 17520 показывается только на campwar-картах (запись карты +69)
		UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] карта=%d (%s) сетка=%dx%d тайлов"),
			MapId, *LM->GetMapRow(MapId)->DirName, Grid->Width, Grid->Height);
	}

	QuestBlinkT += DeltaTime;
	UpdateTownFlag();
	UpdateMap();
	UpdateLabels();
	TickHelpTips();   // ★ 30.10.2026: справка кнопок 1704…1719 (событие 8/0xA эталона)
	UpdateCampWarButton();   // ★ 30.10.2026: 17520 следит за HUD боя (`uiDlgSetDetailVisible`) — дёшево, кэш по состоянию
	if (bBigMapVisible)
	{
		if (!bWindowPlaced) { CenterBigMapWindow(); }   // вьюпорт мог быть не готов на момент сборки окна
		UpdateBigMap();
		UpdateBigMapHover();    // тайл под курсором в 1733/1734 + курсор 18/20
		// перетаскивание окна 1710 ведёт ОБЩАЯ система (UAsdaWindowSubsystem::TickDrag) по данным .JUI
	}
	UpdateMiniMapHoverCursor();   // после карты мира: её наведение главнее, и уход с неё не должен сбить курсор мини-карты
}

// Бит 0x80 тайла → sub_93539F (entity_char.c:20986-20997). Пересчёт только при СМЕНЕ клетки.
void UAsdaMinimapSubsystem::UpdateTownFlag()
{
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!P || !G) { return; }
	const FVector2D Pos = P->GetGamePosXZ();
	const int32 Tx = FMath::FloorToInt(Pos.X / 100.f);
	const int32 Tz = FMath::FloorToInt(Pos.Y / 100.f);
	if (Tx == LastTileX && Tz == LastTileZ) { return; }
	LastTileX = Tx; LastTileZ = Tz;

	const bool bTown = G->IsTilePeaceZone(Tx, Tz);
	if (bTown == bTownMode) { return; }
	bTownMode = bTown;
	// worldMapDlg_SelectCurrentMap → MiniMap_OpenBigMap: смена зоны ПЕРЕЗАГРУЖАЕТ текстуру большой карты.
	LoadMapTextures();
	FVector2D O; float S;
	UE_LOG(LogAsdaMinimap, Log,
		TEXT("[миникарта] зона=%s карта=%d городской вариант большой карты=%s"),
		bTownMode ? TEXT("город(0x80)") : TEXT("поле"), MapId,
		(bTownMode && TownCrop(MapId, O, S)) ? TEXT("есть") : TEXT("нет"));
}

// ── Пул квадов маркеров ──────────────────────────────────────────────────────────────────────────
UImage* UAsdaMinimapSubsystem::AcquireMark(bool bBig, int32 Index)
{
	TArray<TObjectPtr<UImage>>& Pool = bBig ? BigMarkPool : MarkPool;
	UCanvasPanel* Layer = bBig ? BigMarkLayer.Get() : MarkLayer.Get();
	if (!Layer) { return nullptr; }
	while (Pool.Num() <= Index)
	{
		UImage* Img = NewObject<UImage>(Layer);
		Img->SetVisibility(ESlateVisibility::Collapsed);
		UCanvasPanelSlot* S = Layer->AddChildToCanvas(Img);
		if (S) { S->SetAlignment(FVector2D(0.5f, 0.5f)); S->SetAutoSize(false); }
		Pool.Add(Img);
	}
	return Pool[Index];
}

void UAsdaMinimapSubsystem::HideMarksFrom(bool bBig, int32 Index)
{
	TArray<TObjectPtr<UImage>>& Pool = bBig ? BigMarkPool : MarkPool;
	for (int32 i = Index; i < Pool.Num(); ++i)
	{
		if (Pool[i]) { Pool[i]->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

void UAsdaMinimapSubsystem::DrawMark(bool bBig, int32& Cursor, EAsdaMapMark Kind,
	const FVector2D& LocalPx, float SizePx, float AngleDeg)
{
	const TObjectPtr<UTexture2D>* T = MarkTex.Find((uint8)Kind);
	if (!T || !*T) { return; }
	UImage* Img = AcquireMark(bBig, Cursor);
	if (!Img) { return; }
	FSlateBrush B;
	B.SetResourceObject(T->Get());
	B.ImageSize = FVector2f(SizePx, SizePx);
	B.DrawAs = ESlateBrushDrawType::Image;
	Img->SetBrush(B);
	Img->SetRenderTransformAngle(AngleDeg);
	Img->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(Img))
	{
		S->SetOffsets(FMargin(LocalPx.X, LocalPx.Y, SizePx, SizePx));
	}
	++Cursor;
}

// ── Отрисовка мини-карты (MiniMap_RenderBigMap@0xB4FCC0) ─────────────────────────────────────────
void UAsdaMinimapSubsystem::UpdateMap()
{
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!P || !G || !MapImage || !MarkLayer) { return; }

	const float Scale = WorldToTexelScale(G);
	const FVector2D Pos = P->GetGamePosXZ();
	const FVector2D Center(Pos.X * Scale, Pos.Y * Scale);     // sub_93D5F9
	const FVector2D Scroll = ScrollTexels(Center);
	const float View = ViewTexels();
	const float MP   = MapPx();

	// Квад карты: UV-окно (scroll .. scroll+View)/256, растянутое на MP пикселей.
	FSlateBrush B;
	B.SetResourceObject(MiniMapTex.Get());
	B.ImageSize = FVector2f(MP, MP);
	B.DrawAs = ESlateBrushDrawType::Image;
	B.SetUVRegion(FBox2f(
		FVector2f(Scroll.X / kTexSpace,          Scroll.Y / kTexSpace),
		FVector2f((Scroll.X + View) / kTexSpace, (Scroll.Y + View) / kTexSpace)));
	MapImage->SetBrush(B);
	MapImage->SetVisibility(MiniMapTex ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	// Тексел → пиксель внутри окна карты (эталон: ровно ×2 при зуме, иначе ×1).
	const float K = MarkPxPerTexel();
	auto ToPx = [&](const FVector2D& Texel) { return (Texel - Scroll) * K; };
	auto Inside = [&](const FVector2D& Px) { return Px.X >= 0.f && Px.Y >= 0.f && Px.X < MP && Px.Y < MP; };
	// Тайл → тексел. ★ 09.10.2026 (п. 3 владельца): без +½ тайла, как эталон `uiSlotIndexToScreenPos` (t·k) во всех
	//   списках; прежний «центр тайла» — только по ключу отката `asda.Minimap.LegacyHalfTile 1`.
	const float HalfTile = (CVarMinimapLegacyHalfTile.GetValueOnGameThread() != 0) ? 50.f : 0.f;
	auto TileToTexel = [&](int32 Tx, int32 Ty)
	{
		return FVector2D((100.f * Tx + HalfTile) * Scale, (100.f * Ty + HalfTile) * Scale);
	};

	int32 Cursor = 0;

	// 1) Клан / группа / соулмейт / квесты — внешние списки (порядок как в оригинале).
	//   ★ Волна В4 рыбалки (08.10.2026): зоны рыбалки отсюда УБРАНЫ — эталон рисует их ПОСЛЕДНИМИ (`minimapDrawMarkerList`
	//   после `minimapDrawTargetMarker`, g:191754-191755 / 191988-191989), см. шаг 5 ниже.
	static const EAsdaMapMark kListKinds[] = {
		EAsdaMapMark::Party, EAsdaMapMark::Guild,
		EAsdaMapMark::Couple,  EAsdaMapMark::Quest, EAsdaMapMark::Quest2,
	};
	// Квест-точки мигают: 1000 мс видно / 200 мс скрыто (эталон dword206/dword207).
	const double BlinkPeriod = 1.2;
	const bool bQuestOn = FMath::Fmod(QuestBlinkT, BlinkPeriod) < 1.0;
	for (EAsdaMapMark Kind : kListKinds)
	{
		if ((Kind == EAsdaMapMark::Quest || Kind == EAsdaMapMark::Quest2) && !bQuestOn) { continue; }
		const TArray<FAsdaMapMark>* Arr = Marks.Find((uint8)Kind);
		if (!Arr) { continue; }
		for (const FAsdaMapMark& M : *Arr)
		{
			if (M.MapId >= 0 && M.MapId != MapId) { continue; }     // фильтр «только текущая карта»
			const FVector2D Px = ToPx(TileToTexel(M.TileX, M.TileY));
			if (!Inside(Px)) { continue; }
			DrawMark(false, Cursor, Kind, Px, 16.f, 0.f);
		}
	}

	// 2) Точка автобега (busDestPos) — только пока цель не достигнута (эталон: tile != tile игрока).
	if (bDestActive)
	{
		const int32 Tx = FMath::FloorToInt(Pos.X / 100.f);
		const int32 Tz = FMath::FloorToInt(Pos.Y / 100.f);
		if (Tx != DestTileX || Tz != DestTileZ)
		{
			const FVector2D Px = ToPx(TileToTexel(DestTileX, DestTileZ));
			if (Inside(Px)) { DrawMark(false, Cursor, EAsdaMapMark::Destination, Px, 16.f, 0.f); }
		}
	}

	// 3) Конус взгляда камеры (miniMyDir, 64×64) — под значком игрока, как в оригинале.
	//    Угол — через ЕДИНУЮ точку `AsdaCameraConeYaw` (эталон g_fCameraYaw), см. её шапку.
	const float CamYaw = AsdaCameraConeYaw(P);
	const FVector2D SelfPx = ToPx(Center);
	if (Inside(SelfPx))
	{
		DrawMark(false, Cursor, EAsdaMapMark::PlayerDir, SelfPx, 64.f, CamYaw);
		// 4) Сам игрок (miniMyPos, 16×16), поворот = КУРС ПЕРСОНАЖА.
		//    Эталон uiDrawMarkerQuad@0xB54DA0: uiQuad_rotateCorners(..., *(float*)(g_LocalPlayer+190592)) —
		//    поле курса персонажа (у конуса взгляда рядом — g_fCameraYaw). В порту курс держит МЕШ, а не
		//    актор: GetActorRotation().Yaw == 0 всегда, поэтому стрелка «всегда вверх» (баг живого теста).
		DrawMark(false, Cursor, EAsdaMapMark::Player, SelfPx, 16.f, P->GetFacingYawUE());
	}

	// 5) ★ Волна В4 рыбалки: зоны рыбалки FishingArea 16×16 — ПОСЛЕДНИМИ, поверх всего (`minimapDrawMarkerList@0xB56330`
	//    в режимах 2/3 с `g_nCurMapId`; фильтров/опций нет — видны всегда). Квад пула позже = выше по Z.
	//    ★ Ревизия В4 МЕЛК-1 (рыбалка, 09.10.2026): тайл → точка БЕЗ +½ тайла. Эталон в обоих режимах мини-карты считает
	//    `uiSlotIndexToScreenPos@0xB4DBC0` (режим 2: x = base − 259 + t·k, y = t·k + 36, g:190918) / `sub_B4DB50` (режим 3,
	//    `0x928677 jmp`: x = base − 133 + t·k) — тайл умножается на шаг без половины, т.е. (100·t)·масштаб.
	//    ★ 09.10.2026 (п. 3 владельца): общая `TileToTexel` прочих списков переведена на ту же формулу (ключ отката
	//    `asda.Minimap.LegacyHalfTile`); рыбалку ключ не трогает — у неё эталонная формула была и раньше.
	if (const TArray<FAsdaMapMark>* Fish = Marks.Find((uint8)EAsdaMapMark::Fishing))
	{
		for (const FAsdaMapMark& M : *Fish)
		{
			if (M.MapId >= 0 && M.MapId != MapId) { continue; }     // `*v17 == a3` (текущая карта)
			const FVector2D Px = ToPx(FVector2D(100.f * M.TileX * Scale, 100.f * M.TileY * Scale));   // uiSlotIndexToScreenPos: t·k
			if (!Inside(Px)) { continue; }
			DrawMark(false, Cursor, EAsdaMapMark::Fishing, Px, 16.f, 0.f);
		}
	}

	HideMarksFrom(false, Cursor);
}

// ── Отрисовка большой карты (CBigMap_RenderAndHandleClick@0xB521F0) ──────────────────────────────
void UAsdaMinimapSubsystem::UpdateBigMap()
{
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!P || !G || !BigMapImage || !BigMarkLayer) { return; }

	FSlateBrush B;
	B.SetResourceObject(BigMapTex.Get());
	B.ImageSize = FVector2f(kBigMapPx, kBigMapPx);
	B.DrawAs = ESlateBrushDrawType::Image;
	BigMapImage->SetBrush(B);

	// Тайл → пиксель 512-карты: px = tile * (512 / TileCountX); в городском режиме сперва кроп
	//   (uiMapPointWithZoom): pt = (pt − CropOrigin) * CropScale.
	const float TileToPx = BigMapTileToPx();
	FVector2D CropOrigin = FVector2D::ZeroVector; float CropScale = 1.f;   // TownCrop заполнит; инициализация — от C4701
	const bool bCrop = bDisplayTown && TownCrop(DisplayMapId, CropOrigin, CropScale);
	auto ToPx = [&](const FVector2D& T) { return (bCrop ? (T - CropOrigin) * CropScale : T) * TileToPx; };
	auto Inside = [](const FVector2D& Q) { return Q.X >= 0.f && Q.Y >= 0.f && Q.X < kBigMapPx && Q.Y < kBigMapPx; };

	// Своя ли карта показана. Эталон: сравнение g_nBigMapDisplayedMapId/g_nBigMapTargetMapId с g_nCurMapId
	//   (плюс campwar-байт записи карты +69, который снимает фильтр «только своя карта»).
	const bool bOwnMap = (DisplayMapId == MapId);

	const FVector2D Pos = P->GetGamePosXZ();
	const FVector2D SelfPx = ToPx(FVector2D(Pos.X / 100.f, Pos.Y / 100.f));

	int32 Cursor = 0;
	// Маркер игрока и конус взгляда — только когда в окне ЕГО карта (иначе позиция бессмысленна).
	if (bOwnMap && Inside(SelfPx))
	{
		// Конус взгляда — та же ЕДИНАЯ точка, что и на мини-карте (в эталоне обе карты читают
		//   одну глобаль g_fCameraYaw; двух копий формулы быть не должно).
		DrawMark(true, Cursor, EAsdaMapMark::PlayerDir, SelfPx, 64.f, AsdaCameraConeYaw(P));
		DrawMark(true, Cursor, EAsdaMapMark::Player, SelfPx, 16.f, P->GetFacingYawUE());
	}

	// Метка цели: точка автобега на своей карте ИЛИ точка запрошенного межкартового перехода
	//   (эталон: transferMark ставится либо по g_nMapTransferTile*, если показана карта-цель перехода,
	//    либо по g_nPlayerTile*, если показана своя карта). Текстура busDestPos, 32×32 на большой карте.
	if (bTravelActive && DisplayMapId == TravelMapId)
	{
		const FVector2D Q = ToPx(FVector2D((float)TravelTile.X, (float)TravelTile.Y));
		if (Inside(Q)) { DrawMark(true, Cursor, EAsdaMapMark::Destination, Q, 32.f, 0.f); }
	}
	// ★ 29.10.2026 [ОРИГИНАЛ] (ключ asda.Map.DestMarkClear): большая карта рисует метку той же функцией, что мини-карта
	//   (`minimapDrawGroundTargetMarkerQuad`, режим 2, world_map.c:2675) — в клетке цели её нет. Было: висела и после прибытия.
	else if (bOwnMap && bDestActive && !(AsdaDestMarkClearOn()
		&& FMath::FloorToInt(Pos.X / 100.f) == DestTileX && FMath::FloorToInt(Pos.Y / 100.f) == DestTileZ))
	{
		const FVector2D Q = ToPx(FVector2D((float)DestTileX, (float)DestTileZ));
		if (Inside(Q)) { DrawMark(true, Cursor, EAsdaMapMark::Destination, Q, 32.f, 0.f); }
	}

	// Внешние отметки (группа/клан/соулмейт/квесты/рыбалка) — фильтр по ПОКАЗАННОЙ карте.
	// Квест-точки МИГАЮТ и здесь: в эталоне и мини-карту, и большую карту рисует ОДНА функция
	//   uiTextRenderGlyphQuads@0xB537E0 (разные значения a2), а мигание 1000/200 мс живёт внутри неё.
	const bool bQuestOnBig = FMath::Fmod(QuestBlinkT, 1.2) < 1.0;
	for (const TPair<uint8, TArray<FAsdaMapMark>>& It : Marks)
	{
		const EAsdaMapMark BigKind = (EAsdaMapMark)It.Key;
		if (BigKind == EAsdaMapMark::Fishing) { continue; }   // ★ В4 рыбалки: рисуются последними, ниже
		if ((BigKind == EAsdaMapMark::Quest || BigKind == EAsdaMapMark::Quest2) && !bQuestOnBig) { continue; }
		for (const FAsdaMapMark& M : It.Value)
		{
			const int32 MarkMap = (M.MapId >= 0) ? M.MapId : MapId;
			if (MarkMap != DisplayMapId) { continue; }
			const FVector2D MP2 = ToPx(FVector2D((float)M.TileX, (float)M.TileY));
			if (!Inside(MP2)) { continue; }
			DrawMark(true, Cursor, (EAsdaMapMark)It.Key, MP2, 16.f, 0.f);
		}
	}
	// ★ Волна В4 рыбалки: зоны рыбалки — ПОСЛЕДНИМИ (`CBigMap_RenderAndHandleClick` g:192264-192265: после метки цели
	//   `minimapDrawMarkerList(…, 1, g_nBigMapDisplayedMapId, …)`), фильтр — по ПОКАЗАННОЙ карте.
	if (const TArray<FAsdaMapMark>* Fish = Marks.Find((uint8)EAsdaMapMark::Fishing))
	{
		for (const FAsdaMapMark& M : *Fish)
		{
			const int32 MarkMap = (M.MapId >= 0) ? M.MapId : MapId;
			if (MarkMap != DisplayMapId) { continue; }
			const FVector2D MP2 = ToPx(FVector2D((float)M.TileX, (float)M.TileY));
			if (!Inside(MP2)) { continue; }
			DrawMark(true, Cursor, EAsdaMapMark::Fishing, MP2, 16.f, 0.f);
		}
	}
	HideMarksFrom(true, Cursor);
}

// ── ★ Наведение на поле большой карты (CBigMap_RenderAndHandleClick, ветка cursorInMap) ──────────
//   Эталон: подписи 1733/1734 показывают ТАЙЛ ПОД КУРСОРОМ ("%03d"), курсор меняется на 18 «идти» /
//   20 «нельзя»; когда курсор ушёл с поля — курсор возвращается к обычному (Cursor_SetByIndex(0)).
// ★★★ [ПОРТ] 31.10.2026 ПОДСКАЗКА ЗНАЧКА ЦЕЛИ ЗАДАНИЯ на карте мира (значки строит `UAsdaQuestMarkerSubsystem::BuildQuestTargetMarks`).
//   Значок под курсором ищется в той же системе координат, в которой его рисует `UpdateBigMap` (тайл → пиксель поля
//   512×512 с кропом города), порог — половина значка 16×16. Показ — общая плашка от владельца `EAsdaTipOwner::Minimap`
//   (`UAsdaMiniStateSubsystem::ShowSharedTip`), смена значка под курсором — новая подсказка, уход — гашение (своя пара
//   ключей `TargetTip`, справку кнопок 1704…1719 не задевает).
int32 UAsdaMinimapSubsystem::FindQuestTargetAt(const FVector2D& LocalPx) const
{
	const TArray<FAsdaMapMark>* Arr = Marks.Find((uint8)EAsdaMapMark::QuestTarget);
	if (!Arr) { return -1; }
	const float TileToPx = BigMapTileToPx();
	FVector2D O = FVector2D::ZeroVector; float S = 1.f;
	const bool bCrop = bDisplayTown && TownCrop(DisplayMapId, O, S);
	int32 Best = -1; double BestD = 8.0 * 8.0 + 1.0;
	for (int32 i = 0; i < Arr->Num(); ++i)
	{
		const FAsdaMapMark& M = (*Arr)[i];
		if (M.MapId != DisplayMapId) { continue; }
		const FVector2D T((float)M.TileX, (float)M.TileY);
		const FVector2D Px = (bCrop ? (T - O) * S : T) * TileToPx;
		const double D = FVector2D::DistSquared(Px, LocalPx);
		if (D < BestD) { BestD = D; Best = i; }
	}
	return Best;
}

void UAsdaMinimapSubsystem::UpdateQuestTargetTip(bool bInMap, const FVector2D& LocalPx)
{
	UGameInstance* GI = GetGameInstance();
	const int32 Idx = bInMap ? FindQuestTargetAt(LocalPx) : -1;
	const TArray<FAsdaMapMark>* Arr = Marks.Find((uint8)EAsdaMapMark::QuestTarget);
	// Ключ — значок и показанная карта: при смене области тот же номер значка — уже другая подсказка.
	const int32 Key = (Idx >= 0 && Arr && Arr->IsValidIndex(Idx)) ? (DisplayMapId * 1000 + Idx) : -1;
	TargetTip.Update(GI, EAsdaTipOwner::Minimap, Key, [this, GI, Idx, Arr]() -> bool
	{
		UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
		if (!Mini || !Arr || !Arr->IsValidIndex(Idx)) { return false; }
		const FAsdaMapMark& M = (*Arr)[Idx];
		Mini->ShowSharedTip(M.TipTitle, M.TipBody, EAsdaTipOwner::Minimap);
		UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] подсказка цели: «%s» — %s"), *M.TipTitle, *M.TipBody);
		return true;
	});
}

// ★ [ОРИГИНАЛ] Курсор над мини-картой. Эталон `MiniMap_Render@0xB4FCC0`: кадр начинается со сброса (`g_bMapCursorHover1/2 == 1` →
//   `Cursor_SetByIndex(0, 0)`), затем `PtInRect(поле карты)` и `g_nBigMapOpenMode == 1 && !окно пополнения` → `Cursor_SetByIndex(18, 0)`.
//   `g_nBigMapOpenMode` = 1 ставит только событие 9 контрола 1714 (`MiniMap_BigMapWindowProc`, world_map.c:7456), а каждое другое
//   сообщение окна сбрасывает его в −1: окно поверх карты и кнопки на ней событие забирают — курсор не меняется. Индекс 20
//   «нельзя» здесь эталон НЕ ставит (он только у карты мира, ui_core.c:5363). У порта «верхний контрол» — `IsHovered()` кнопки
//   поля `MMap_ClickArea` и верхнее окно по общему хит-тесту `FindWindowCatchingAt`: тело настоящего окна для Slate прозрачно
//   (самотест F: под сумкой `IsHovered()` поля остаётся истинным). Скрытый интерфейс (Alt+Z) и модальное окно мышь
//   полю не отдают. Чужой курсор интерфейса (самоцветы, камень улучшения, карта мира) не перебиваем: у эталона взведённый
//   курсор держит флаг `a3 = 1` в `Cursor_SetByIndex`, и индекс 18 с `a3 = 0` его не меняет.
void UAsdaMinimapSubsystem::UpdateMiniMapHoverCursor()
{
	UGameInstance* GI = GetGameInstance();
	const UWidget* Area = MinimapWidget ? MinimapWidget->GetWidgetFromName(TEXT("MMap_ClickArea")) : nullptr;
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bWant = CVarAsdaMinimapHoverCursor.GetValueOnGameThread() != 0
		&& Area && Area->IsHovered() && AsdaUi::IsCursorOverWidget(Area)
		&& UAsdaUiVisibilitySubsystem::IsUiShownFor(this) && !AsdaUi::IsModalDispatchBlocked(GI)
		&& WM && !WM->IsWindowVisible(FName(TEXT("CashCharge_Win")))      // `!sub_93B9B6(g_pCashChargeDialog)`
		&& FSlateApplication::IsInitialized()
		&& WM->FindWindowCatchingAt(FSlateApplication::Get().GetCursorPos()) == FName(TEXT("MINIMAP_WIN"));   // верхнее окно — сама карта
	if (!bWant) { ReleaseMiniHoverCursor(); return; }
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI ? GI->GetFirstLocalPlayerController() : nullptr);
	if (!PC || (PC->bUiCursorOverride && !bMiniCursorOverridden)) { return; }
	PC->bUiCursorOverride = true;    // мировой UpdateHover курсор больше не перебивает
	PC->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Move);
	bMiniCursorOverridden = true;
}

void UAsdaMinimapSubsystem::ReleaseMiniHoverCursor()
{
	if (!bMiniCursorOverridden) { return; }
	bMiniCursorOverridden = false;
	if (bCursorOverridden) { return; }   // курсор уже держит карта мира — снимет сама
	UGameInstance* GI = GetGameInstance();
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI ? GI->GetFirstLocalPlayerController() : nullptr))
	{
		PC->bUiCursorOverride = false;
		PC->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Normal);
	}
}

void UAsdaMinimapSubsystem::UpdateBigMapHover()
{
	if (!BigMapWidget || !BigMapView) { return; }
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(
		GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr);

	FVector2D L;
	const bool bGot = ClickLocalPx(this, BigMapView, L);
	const bool bIn = bGot && L.X >= 0.f && L.Y >= 0.f && L.X < kBigMapPx && L.Y < kBigMapPx;
	UpdateQuestTargetTip(bIn, L);   // ★ [ПОРТ] 31.10.2026 подсказка значка цели задания
	if (!bIn)
	{
		if (bCursorOverridden && PC)
		{
			PC->bUiCursorOverride = false;
			PC->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Normal);
		}
		bCursorOverridden = false;
		bHoverInMap = false;
		return;
	}
	bHoverInMap = true;

	// Локальный пиксель → тайл (обратно к ToPx): tile = px / TileToPx, затем снять кроп города.
	const float TileToPx = BigMapTileToPx();
	FVector2D T = L / FMath::Max(TileToPx, KINDA_SMALL_NUMBER);
	FVector2D O; float S;
	if (bDisplayTown && TownCrop(DisplayMapId, O, S)) { T = T / S + O; }
	HoverTile = FIntPoint(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));

	// Эталон sprintf("%03d") — ведущие нули, ровно как в оригинале.
	if (UTextBlock* TX = Cast<UTextBlock>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_X"))))
	{
		TX->SetText(FText::FromString(FString::Printf(TEXT("%03d"), HoverTile.X)));
	}
	if (UTextBlock* TY = Cast<UTextBlock>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_Y"))))
	{
		TY->SetText(FText::FromString(FString::Printf(TEXT("%03d"), HoverTile.Y)));
	}

	if (PC)
	{
		// Проходимость проверяется только у СВОЕЙ карты (сетки чужой у клиента нет — как и в оригинале).
		const bool bBlocked = (DisplayMapId == MapId) && G && !G->IsTileWalkable(HoverTile.X, HoverTile.Y);
		PC->bUiCursorOverride = true;    // мировой UpdateHover курсор больше не перебивает
		PC->SetGameCursor(bBlocked ? AAsdaPlayerController::EAsdaCursor::NoMove
		                           : AAsdaPlayerController::EAsdaCursor::Move);
		bCursorOverridden = true;
	}
}

// ★★★ СОБСТВЕННОЕ ПЕРЕТАСКИВАНИЕ MAXMAP_WIN УДАЛЕНО (2026-08-06).
//   Было: `UpdateWindowDrag()` — ручной прямоугольник ручки 1713 `MAXMAP_MOVE` + сдвиг всех детей
//   корневой канвы. Теперь перетаскивание — ОДНА система на все окна порта: ручка объявлена
//   в данных .JUI (`EVS{EVT=6, RET=0x20003, CtrlID=1710}`), попадает в `DA_JuiWindowMeta` при
//   импорте и обрабатывается `UAsdaWindowSubsystem::TickDrag`. Дублировать её здесь нельзя:
//   два независимых обработчика двигали бы окно на удвоенную дельту.
//   `ShiftBigMapWindow` СОХРАНЁН — им пользуется однократное центрирование окна при сборке UI
//   (`CenterBigMapWindow`, эталон MiniMap_BuildUI@0xD63220).
// Сдвиг всего окна = сдвиг ВСЕХ детей его корневого канваса (в JUI окно — плоский набор контролов).
void UAsdaMinimapSubsystem::ShiftBigMapWindow(const FVector2D& Delta)
{
	if (!BigMapWidget) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(BigMapWidget->GetRootWidget());
	if (!Root) { return; }
	for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
	{
		ShiftSlot(Root->GetChildAt(i), Delta.X, Delta.Y);
	}
}

// ── Подписи (MiniMap_UpdatePlayerCoordText@0xD64900 + ChannelSelectWnd_UpdateChannelLabels) ──────
void UAsdaMinimapSubsystem::UpdateLabels()
{
	ASDA_PERF_SCOPE("Minimap.Labels");   // ★ R4-b 14.11.2026: область замера (AsdaPerf.h; в Shipping пусто)
	if (!MinimapWidget) { return; }
	AAsdaPlayerCharacter* P = nullptr;
	const UAsdaWalkGrid* G = GetGrid(P);
	if (!P) { return; }
	const FVector2D Pos = P->GetGamePosXZ();
	const int32 Tx = FMath::FloorToInt(Pos.X / 100.f);
	const int32 Tz = FMath::FloorToInt(Pos.Y / 100.f);
	// Подписи по кэшу (`UpdateLabelsCached`; ★ R4-b закрытие (15.11.2026): старый путь и ключ удалены — только новое (решение владельца)). Имя 1703 — `MapTitleText` (эталон `CMap_LoadMapForSlot@0xB4CA90`:
	//   отображаемое имя записи карты; у нас — таблица `Map_info`). Цвет, обводку и выравнивание несёт сам контрол.
	UpdateLabelsCached(Tx, Tz);
	UpdateChannelUI();
}

// ★ R4-b 14.11.2026: имя локации для 1703 — тело, прежде стоявшее в `UpdateLabels` (дословно; зовёт `UpdateLabelsCached`).
FString UAsdaMinimapSubsystem::MapTitleText() const
{
	UGameInstance* GI = GetGameInstance();
	FString Title;
	if (UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		Title = TS->GetText(TEXT("Map_info"), MapId, TEXT("name"));
	}
	if (Title.IsEmpty())
	{
		// Таблица не дала строки — берём отображаемое имя записи карты (DT_MapTable), затем имя папки.
		UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		if (const FAsdaMapRow* Row = LM ? LM->GetMapRow(MapId) : nullptr)
		{
			// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `MapTable`, ключ `<mapId>.DisplayName`.
			Title = Row->DisplayName.IsEmpty() ? Row->DirName
				: FAsdaLang::ResolveId(TEXT("MapTable"), Row->MapId, TEXT("DisplayName"), Row->DisplayName.ToString());
		}
	}
	return Title;
}

// ★ R4-b 14.11.2026 (B1): подписи по кэшу. Узлы — раз на виджет (окно пересоздано — найти заново). Число пишется при смене тайла или культуры
//   (формат `FText::AsNumber` зависит от культуры), имя — при смене карты или языка, а пустое — каждый кадр (как прежде: таблица
//   могла ещё не дать строку). Смена языка сбрасывает всё — подписи переписываются заново.
void UAsdaMinimapSubsystem::UpdateLabelsCached(int32 Tx, int32 Tz)
{
	if (LblOwner.Get() != MinimapWidget)
	{
		LblOwner = MinimapWidget;
		LblX = Cast<UTextBlock>(MinimapWidget->GetWidgetFromName(TEXT("MINIMAP_X")));
		LblY = Cast<UTextBlock>(MinimapWidget->GetWidgetFromName(TEXT("MINIMAP_Y")));
		LblName = Cast<UTextBlock>(MinimapWidget->GetWidgetFromName(TEXT("MINIMAP_NAME")));
		LblTx = LblTz = INT32_MIN; LblCulture = nullptr; bLblTitleSet = false; LblLang = -1;
	}
	const int32 Lang = (int32)FAsdaLang::Current();
	if (Lang != LblLang) { LblLang = Lang; LblTx = LblTz = INT32_MIN; LblCulture = nullptr; bLblTitleSet = false; }
	const void* Cul = &FInternationalization::Get().GetCurrentCulture().Get();
	if (Cul != LblCulture) { LblCulture = Cul; LblTx = LblTz = INT32_MIN; }
	if (Tx != LblTx) { if (UTextBlock* TX = LblX.Get()) { TX->SetText(FText::AsNumber(Tx)); } LblTx = Tx; }
	if (Tz != LblTz) { if (UTextBlock* TY = LblY.Get()) { TY->SetText(FText::AsNumber(Tz)); } LblTz = Tz; }
	if (UTextBlock* TN = LblName.Get())
	{
		if (!bLblTitleSet || LblTitle.IsEmpty() || LblMapId != MapId)
		{
			const FString Title = MapTitleText();
			if (!bLblTitleSet || !Title.Equals(LblTitle, ESearchCase::CaseSensitive)) { TN->SetText(FText::FromString(Title)); }
			LblTitle = Title; LblMapId = MapId; bLblTitleSet = true;
		}
	}
}

