// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMinimapSubsystem — реализация. Разбор эталона и все формулы — в шапке AsdaMinimapSubsystem.h.
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaLocalGrid.h"   // ★ R5-fix: сетка проходимости локального игрока (одна копия на миникарту и зоны)



#include "AsdaMinimapSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMinimapSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaMinimap);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// Откат отступления: вернуть эталонное «иконки занятости не показываются».
static ASDA_DEV_CVAR(int32, CVarHideOccupationBox,
	TEXT("asda.War.HideOccupationBox"),
	0,
	TEXT("Панель занятости городов под мини-картой: 0 = работает по эталону (цветная буква видна, "
	     "если город держит фракция игрока); 1 = панель погашена целиком."),
	ECVF_Default);
// ★★★ 29.10.2026 [ОРИГИНАЛ] баг игроков «Moving does not remove the auto move icon from the map». Метку цели автобега
//   (`busDestPos`) эталон рисует `minimapDrawGroundTargetMarkerQuad@0xB551E0` (world_map.c:2936) на ОБЕИХ картах (мини — режим 1,
//   большая — режим 2, world_map.c:2675), только пока флаг `g_GroundTargetMarkerActive` поднят И своя клетка ≠ клетке цели
//   (`g_nPlayerTileX/Y` — миснейм: это клетка ЦЕЛИ). «Снять» = цель := своя клетка, флаг 0, узлы маршрута долой:
//     · отпускание клавиш хода (math_geom.c:684-690); · 6147 «стоп на месте» (network.c:11519-11524);
//     · каждый кадр в состояниях +47551 из набора LABEL_111 — 5/24/22/15/25/14/27/7/8/9/102/105/16/6 и разговор с NPC
//       (`g_bInventoryLocked`) (game_main.c:1668-1719): смерть, атака, навык, каст, сидение…;
//   клик по земле ставит цель в клетку клика (input.c:678/738). У порта флаг `bDestActive` снимал только сброс смены карты и
//   автопуть — после WASD/смерти/атаки метка висела, на большой карте — и после прибытия (проверки клетки там не было).
//   [ПОРТ] нажатие клавиш хода у порта СТИРАЕТ маршрут сразу (эталон его приостанавливает до отпускания) — метку снимаем там же.
//   1 = как оригинал; 0 = прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarDestMarkClear,
	TEXT("asda.Map.DestMarkClear"), 1,
	TEXT("1 (по умолчанию) — метка цели автобега на мини-карте и карте мира снимается ручным ходом (клавиши), смертью, атакой,\n")
	TEXT("навыком, сидением, разговором с NPC, остановкой оператора 6147; клик по земле ставит её в клетку клика; на большой карте\n")
	TEXT("она не рисуется в клетке цели (как у оригинала). 0 — прежний порт: метку снимали только смена карты и автопуть."),
	ECVF_Default);
// Значение `asda.Map.DestMarkClear` (читают контроллер и оператор 6147 — объявление локально у вызывающего).
bool AsdaDestMarkClearOn() { return CVarDestMarkClear.GetValueOnGameThread() != 0; }
// Снять метку цели автобега (эталон: цель := своя клетка, `g_GroundTargetMarkerActive = 0`). Межкартовую заявку не трогает —
//   её снятие при ручном ходе — у `CancelTravelIfManual`, при атаке — решение ключа `asda.Move.AutoPathExitsCombat`.
void AsdaDestMarkClear(UGameInstance* GI, const TCHAR* Why)
{
	if (!AsdaDestMarkClearOn() || !GI) { return; }
	UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>();
	if (!Mm || !Mm->IsDestinationActive()) { return; }
	Mm->SetDestinationTile(0, 0, false);
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] метка цели автобега снята: %s"), Why ? Why : TEXT(""));
}
// Клик по земле: цель автобега = клетка клика (game-координаты, input.c:678 — `g_nPlayerTileX/Y = клик / 100`).
void AsdaDestMarkSetGame(UGameInstance* GI, const FVector2D& Game)
{
	if (!AsdaDestMarkClearOn() || !GI) { return; }
	if (UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>())
	{
		Mm->SetDestinationTile(FMath::FloorToInt(Game.X / 100.f), FMath::FloorToInt(Game.Y / 100.f), true);
	}
}

// ★ 23.10.2026: занятость не выше низа панели Друга 6500, пока та на экране (раскладка .JUI: 6500 — y 200…300, занятость — 300).
static ASDA_DEV_CVAR(int32, CVarOccupationBelowSoulPop,
	TEXT("asda.War.OccupationBelowSoulPop"), 1,
	TEXT("1 (по умолчанию) = при видимой панели Друга 6500 панель занятости 79900 встаёт под неё (как в .JUI).\n")
	TEXT("0 = прежнее: занятость прижата к низу мини-карты и может лечь поверх значков эффектов Друга."),
	ECVF_Default);
// ★ 11.11.2026 (А2 red_triage) [ИСПРАВЛЕНИЕ] ссылки панели занятости 79900 после смены карты. OpenLevel сносит окна, а
//   подсистема живёт: `ResetSessionState` сбрасывал только флаг `bOccupationBaseCached`, массивы `OccupationNodes` /
//   `OccupationBasePos` оставались на виджетах СНЕСЁННОГО окна до следующей удачной раскладки (ранние выходы
//   `LayoutOccupationBox` их не трогают) — детектор `asda.SelfTest.IconsAfterTravel` видел 6 мёртвых ссылок (памятка
//   window-pointers-stale-after-travel: сбрасывать все указатели в месте пересоздания окна). 1 — массивы сбрасываются при
//   сбросе сессии и перед пересозданием окна 79900; 0 — прежнее поведение.
ASDA_DEV_CVAR(int32, CVarOccupationResetNodes,
	TEXT("asda.War.OccupationResetNodes"), 1,
	TEXT("1 (по умолчанию) = при смене карты и пересоздании панели занятости 79900 сбрасывать её узлы и базовые позиции.\n")
	TEXT("0 = прежнее: сбрасывался только флаг кэша, массивы держали виджеты снесённого окна."),
	ECVF_Default);

static const TCHAR* kMarkDir    = AsdaAssetPaths::kMapTexPrefix;     // ★ волна 2 п. 7: корни — реестр путей

// Имена .TEX маркеров из CMiniMap_Init@0xB4BDC0 (порядок = EAsdaMapMark).
static const TCHAR* kMarkTexName[] = {
	TEXT("miniMyPos"), TEXT("miniMyDir"), TEXT("busDestPos"), TEXT("miniPartyPos"),
	TEXT("miniGulidPos"), TEXT("miniCouplePos"), TEXT("miniQuest"), TEXT("miniQuest2"),
	TEXT("FishingArea"),
	TEXT("miniFriendPos"),   // ★ [ПОРТ] 31.10.2026 EAsdaMapMark::QuestTarget — неиспользуемый маркер оригинала `./Map/miniFriendPos.TEX`
};

// ── Жизненный цикл ───────────────────────────────────────────────────────────────────────────────
void UAsdaMinimapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Текстуры маркеров — те же файлы ./Map/*.TEX, что грузит CMiniMap_Init.
	// ★ Аудит №3 (A8/B8): загрузка через менеджер текстур (кэш по имени + refCount, закон
	//   `resource-subsystems`). `LoadTextureCached` сам дописывает `.<имя>` к пакетному пути, поэтому
	//   строка пути осталась прежней.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	for (int32 i = 0; i < UE_ARRAY_COUNT(kMarkTexName); ++i)
	{
		const FString P = FString(kMarkDir) + kMarkTexName[i];
		if (UTexture2D* T = TexMgr ? TexMgr->LoadTextureCached(P) : nullptr)
		{
			MarkTex.Add((uint8)i, T);
		}
		else
		{
			UE_LOG(LogAsdaMinimap, Warning, TEXT("[миникарта] нет текстуры маркера %s"), *P);
		}
	}
	BuildDefaultAreas();
}

// ── ★ КАРТА МИРА: таблица областей (uiInitSlotIndexMaps@0xD66320 + helpWndFillNoticeList@0xD66880) ─
//   Континент 0 — 10 строк (подписи 96110..96119), континент 1 — 9 строк (96210..96218).
//   Пары (mapId, городской флаг) взяты из sub_9336CB-вызовов уiInitSlotIndexMaps ровно в порядке ключей.
void UAsdaMinimapSubsystem::BuildDefaultAreas()
{
	// ★ 14.09.2026 (реестр карт, волна 2): таблица — `AsdaMapRegistry::kWorldMapAreas` (генерируемый заголовок из
	//   `Tools/data/asda_map_registry.json`); значения, порядок и подписи прежние.
	WorldMapAreas.Reset();
	for (const AsdaMapRegistry::FWorldMapAreaDef& D : AsdaMapRegistry::kWorldMapAreas)
	{
		FAsdaWorldMapArea R; R.Continent = D.Continent; R.MapId = D.MapId; R.bTown = D.bTown; R.TextId = D.TextId;
		WorldMapAreas.Add(R);
	}
}

void UAsdaMinimapSubsystem::SetWorldMapAreas(const TArray<FAsdaWorldMapArea>& In)
{
	WorldMapAreas = In;
	if (bWorldMapBuilt) { FillAreaCombo(SelContinent); }
}

void UAsdaMinimapSubsystem::Deinitialize()
{
	MinimapWidget = nullptr; MinIconWidget = nullptr; BigMapWidget = nullptr;
	MapView = nullptr; MapImage = nullptr; MarkLayer = nullptr;
	BigMapView = nullptr; BigMapImage = nullptr; BigMarkLayer = nullptr;
	MarkPool.Reset(); BigMarkPool.Reset();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaMinimapSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-B; тот же дефект, что у чата — `wave2_A.md` п.0): ОКНА КАРТ ЗАКРЫВАЮТСЯ В РЕЕСТРЕ.
	//   Кэши ниже (`MarkPool`, `BigMarkPool`, `bWorldMapBuilt`) рассчитаны на НОВОЕ дерево («старые UImage принадлежат мёртвому
	//   слою»), но при сбросе без смены мира (разрыв связи) окна 1700/1710 оставались живыми в реестре: пул маркеров и подписи
	//   карты мира строились второй раз в том же дереве, а прежние узлы оставались в слоях — MINIMAP_WIN 135 → 141 → 147,
	//   MAXMAP_WIN 131 → 143 → 155 узлов после двух сбросов (`asda.UI.TreeCensus`, `w2B_logs`). Стало: окна мини-карты, её
	//   значка, панели занятости и карты мира снимаются с реестра (тихо) — `IsWindowLive` на ближайшем тике/открытии ложно,
	//   окна создаются НОВЫМИ экземплярами, как после травела. Ключа отката нет.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindowSilently(FName(TEXT("MINIMAP_WIN")));
		WM->CloseWindowSilently(FName(TEXT("MINIMAPMIN_WIN")));
		WM->CloseWindowSilently(FName(TEXT("OCCUPATION_BOX")));
		WM->CloseWindowSilently(FName(TEXT("MAXMAP_WIN")));
	}
	// ★ Метки и кэш раскладки посчитаны для КАРТЫ — снимаем и при смене карты.
	//   ★★★ 18.10.2026 (автопуть квеста не продолжался после портала): ЗАЯВКУ путешествия (цель — карта+клетка) при смене
	//   карты НЕ снимаем — ради неё переход и был (`StopTravelAndMovementForTransfer`: «поход обязан продолжиться сам»).
	//   Снимается только при полном сбросе (разрыв/выход/новый вход). Шаг карты (`TravelStepMapId`) сбрасываем: тик
	//   пересчитает его на новой карте. Регрессия правки К-3 от 02.09 (таблица fix_disconnect_reset.md, «путешествие — +карта»).
	ReleaseMiniHoverCursor();   // окно снято — наведения больше нет
	ResetMinimapWidgetCaches();
	ResetBigMapWidgetCaches();
	Marks.Reset();
	MapId = -1;
	LastTileX = INT32_MIN; LastTileZ = INT32_MIN;
	DestTileX = 0; DestTileZ = 0; bDestActive = false;
	if (AsdaIsFullSessionReset(Reason))
	{
		bTravelActive = false;
		TravelMapId = -1;
		TravelTile = FIntPoint::ZeroValue;
	}
	TravelStepMapId = -1;
	TravelStuckCount = 0;
	bLayoutCached = false;
	ShownChannelCount = -1;
	ShownShortWord = -1;
	ShownSvrChText.Empty();
	bCampWarShown = false;
	ShownCampWarMapId = -2;
	bOccupationBaseCached = false;
	if (CVarOccupationResetNodes.GetValueOnGameThread() != 0)   // ★ А2: ссылки снесённого окна 79900 (см. ключ)
	{
		OccupationNodes.Reset();
		OccupationBasePos.Reset();
	}
	if (AsdaIsFullSessionReset(Reason))
	{
		bBigMapVisible = false;
		bExpanded = false;
		bZoom = false;
		bWorldMapBuilt = false;
	}
}

// ── Доступ к игроку/сетке ────────────────────────────────────────────────────────────────────────
const UAsdaWalkGrid* UAsdaMinimapSubsystem::GetGrid(AAsdaPlayerCharacter*& OutPlayer) const
{
	// ★ R5-fix (13.11.2026, чистка): тело дословно повторяло `UAsdaZoneSubsystem::GetPlayerGrid` — общая `AsdaLocalGrid::Get`.
	return AsdaLocalGrid::Get(GetGameInstance(), OutPlayer);
}

// Эталон uiMinimapBuildQuads: v11[201] = v11[202] = 256.0 / (TileCountX * 100).
//   ОБЕ оси делятся на ШИРИНУ карты — так в оригинале (v11[199] используется и для Y).
float UAsdaMinimapSubsystem::WorldToTexelScale(const UAsdaWalkGrid* Grid) const
{
	const float WorldW = (Grid && Grid->Width > 0) ? (float)Grid->Width * 100.f : 51200.f;
	return kTexSpace / WorldW;
}

// Эталон: scroll = clamp(texel − View/2, 0, 256 − View)  (uiClampUVRectTo01 + world_map.c:2650-2656).
FVector2D UAsdaMinimapSubsystem::ScrollTexels(const FVector2D& CenterTexel) const
{
	const float View = ViewTexels();
	const float MaxS = kTexSpace - View;
	return FVector2D(
		FMath::Clamp(CenterTexel.X - View * 0.5f, 0.f, FMath::Max(MaxS, 0.f)),
		FMath::Clamp(CenterTexel.Y - View * 0.5f, 0.f, FMath::Max(MaxS, 0.f)));
}

// hudIconSetUvBySlot@0xB54450 — кроп городской большой карты. Только карты 0/3/7 (isSpecialSlotIndex).
bool UAsdaMinimapSubsystem::TownCrop(int32 InMapId, FVector2D& OutOrigin, float& OutScale)
{
	// ★ 14.09.2026 (реестр карт, волна 2): карты и значения — `AsdaMapRegistry::kTownCrops` (0/3/7, прежние).
	if (const AsdaMapRegistry::FTownCrop* C = AsdaMapRegistry::FindTownCrop(InMapId))
	{
		OutOrigin = FVector2D(C->OriginX, C->OriginY); OutScale = C->Scale; return true;
	}
	OutOrigin = FVector2D::ZeroVector; OutScale = 1.f; return false;
}

// ★★★ СБРОС КЭШЕЙ ОКНА 1702 ПРИ ЕГО ПЕРЕСОЗДАНИИ (регрессия «пропал список каналов», 2026-08-18).
//
// ЧТО СЛУЧИЛОСЬ. Сцена премиум-гачи выполняла эталонный ШАГ 5 `uiCloseAllWindows(g_pUIManager)`
//   через `UAsdaWindowSubsystem::CloseAll()` (с 2026-08-19 — через `CloseAllPlayerWindows()`,
//   который дерево не сносит; сносящий `CloseAll()` остался у входа в мир `RevealWorld`),
//   который снимает с вьюпорта и с реестра ВСЕ окна, включая 1702. Гвард `IsWindowLive` ниже это
//   честно видит и заводит НОВОЕ дерево виджетов. Но кэши «на что уже отрисовано» — поля
//   ПОДСИСТЕМЫ, а не виджета, и пересоздание они переживали:
//     * `ShownChannel/ShownChannelCount/ShownShortWord` → `UpdateChannelUI` выходил по раннему
//       `return`, и НОВЫЙ виджет не получал ни подписи 1717 `MMap_SvrCh` («Альпеон - 1 кан.»),
//       ни текста/цвета/видимости строк 17503..17517, ни подгонки высоты панели 17501.
//       В .JUI у этих контролов подписи нет (её ставит игра в рантайме) — отсюда ПУСТАЯ плашка
//       и пустой список каналов на экране;
//     * `bCampWarShown/ShownCampWarMapId` → так же молча гасил восстановление кнопки 17520;
//     * `MarkPool`/`BigMarkPool` держали `UImage`, созданные в СТАРОМ слое маркеров: `AcquireMark`
//       переиспользовал их, и маркеры (стрелка игрока, мобы, отряд) рисовались в отсоединённое
//       дерево, то есть в никуда.
//   Имя карты 1703 при этом оставалось на месте — его пишет `UpdateLabels`, а его зовут КАЖДЫЙ
//   тик; именно этот контраст и указал на кэш.
//
// ⚠ Значения-«состояния карты» (`MapId`, `bTownMode`, `bExpanded`, `bZoom`, `bVisible`) здесь НЕ
//   трогаем: они описывают мир и режим окна, а не отрисованность конкретного дерева, и сбрасывать
//   их значило бы схлопывать развёрнутую мини-карту при каждом `CloseAll`.
void UAsdaMinimapSubsystem::ResetMinimapWidgetCaches()
{
	// Раскладка: BasePos кэширует offsets контролов СТАРОГО дерева.
	bLayoutCached = false;
	HelpTip.Hide(GetGameInstance(), EAsdaTipOwner::Minimap);   // ★ 30.10.2026: справка принадлежала кнопкам старого дерева
	BasePos.Reset();
	// Каналы (17500/1717/17501/17503+i) — тот самый кэш, из-за которого список оставался пустым.
	ShownChannel = -1; ShownChannelCount = -1; ShownShortWord = -1; ShownSvrChText.Reset();
	bChannelPanelOpen = false;    // JUI Visible=0 у 17501 — новое дерево приходит со свёрнутой панелью
	HoverChannel = -1;
	// Кнопка 17520 «выйти из боя».
	bCampWarShown = false; ShownCampWarMapId = -2;
	// Пул квадов маркеров: старые `UImage` принадлежат мёртвому слою `MMap_Markers`.
	MarkPool.Reset();
}

// То же для окна 1710 (карта мира). Здесь дополнительно снимается `bWorldMapBuilt`: комбобоксы
//   континента/области и легенда строятся ОДИН раз на дерево (`BuildWorldMapUI`), и без сброса
//   заново открытое окно осталось бы с пустыми списками.
void UAsdaMinimapSubsystem::ResetBigMapWidgetCaches()
{
	bWorldMapBuilt = false;
	BigMarkPool.Reset();
}

// ── Виджеты ──────────────────────────────────────────────────────────────────────────────────────
// ══════════ ПАНЕЛЬ ЗАНЯТОСТИ ГОРОДОВ (79900…79903) — эталон `uniconWar_UpdateFactionStateUI` ═════
// Правило ОДНО и оно ровно эталонное (ui_core.c:3919-3924): цветная буква города ВИДНА тогда и
//   только тогда, когда фракция-хозяин города совпадает с фракцией ИГРОКА. Иначе — скрыта, и
//   остаётся серый вариант, нарисованный на общей подложке `NEW_OCC_BACK`.
//   Ничего не красим: цветные варианты уже готовы отдельными планами в `.JUI`.
void UAsdaMinimapSubsystem::RefreshOccupationBox()
{
	if (!OccupationWidget) { return; }
	const bool bHide = CVarHideOccupationBox.GetValueOnGameThread() != 0;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem*   Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS  = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	// Фракция игрока: эталон читает `CClass_GetField142C(&g_InventoryBag)[1].buffFxSlots[116]`,
	//   в порту это `SelfCampId` (−2 = пакет 6713 ещё не приходил, −1 = фракции нет).
	const int32 Self = WS ? WS->GetSelfCampId() : -1;
	int32 Shown = 0;
	for (int32 i = 0; i < OccupationIcons.Num(); ++i)
	{
		UImage* Img = OccupationIcons[i];
		if (!Img) { continue; }
		// Без пакета 6743 `GetCityOwnerFaction` отдаёт 3 («не занят») ⇒ условие ложно ⇒ всё серое.
		const int32 Owner = Net ? Net->GetCityOwnerFaction(i) : 3;
		const bool bMine = !bHide && Self >= 0 && Owner == Self;
		AsdaUi::SetShownDecor(Img, bMine);
		if (bMine) { ++Shown; }
	}
	UE_LOG(LogAsdaMinimap, Log,
		TEXT("[занятость] фракция игрока %d; хозяева: Альпия=%d Силарис=%d Пламения=%d; ")
		TEXT("цветных букв %d (6743: %s)"),
		Self,
		Net ? Net->GetCityOwnerFaction(0) : 3, Net ? Net->GetCityOwnerFaction(1) : 3,
		Net ? Net->GetCityOwnerFaction(2) : 3, Shown,
		(Net && Net->HasCityOwnerInfo()) ? TEXT("получен") : TEXT("НЕ приходил"));
}

// Прижать панель к НИЗУ мини-карты. В `.JUI` она прибита к правому верху со смещением (−84, 300) —
//   это низ АВТОРСКОЙ мини-карты в базе 1024×768. В порту мини-карта своя, поэтому считаем сдвиг по
//   её ЖИВОЙ геометрии и двигаем ВСЕ узлы панели ОДНИМ общим смещением внутри её собственного
//   канваса (закон о двух системах координат: окно в единицах вьюпорта вторично не двигаем).
void UAsdaMinimapSubsystem::LayoutOccupationBox()
{
	if (!OccupationWidget || !OccupationWidget->WidgetTree) { return; }
	// Опорный узел мини-карты — окно карты `MMap_View`: его низ и есть низ мини-карты.
	if (!MapView) { return; }
	const UCanvasPanelSlot* VS = AsdaUi::SlotOf(MapView);
	if (!VS) { return; }

	// Авторские позиции узлов панели снимаем ОДИН раз — до всякого сдвига.
	if (!bOccupationBaseCached)
	{
		OccupationNodes.Reset();
		OccupationBasePos.Reset();
		TArray<UWidget*> All;
		OccupationWidget->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
			{
				OccupationNodes.Add(W);
				OccupationBasePos.Add(FVector2D(CS->GetOffsets().Left, CS->GetOffsets().Top));
			}
		}
		bOccupationBaseCached = true;
	}

	// Куда хотим: левый-верх панели = (правый край мини-карты − ширина панели, низ ОКНА + зазор).
	//   Ширина панели — подложка `NEW_OCC_BACK` (80 px по `.JUI`), зазор — «несколько пунктов».
	const FVector2D MapPos  = AsdaUi::CanvasRootPos(MapView);
	const FMargin   MapOff  = VS->GetOffsets();
	const float PanelW = 80.f;
	const float Gap    = 4.f;

	// ★★★ НИЗ БЕРЁМ У ВСЕГО ОКНА, А НЕ У `MMap_View` (замечание владельца 2026-08-26).
	//   Под самой картой у окна мини-карты есть ещё контролы — полоса с координатами и кнопками.
	//   Привязка к низу `MMap_View` ставила панель ПОВЕРХ них. Поэтому идём по узлам окна и берём
	//   САМУЮ НИЖНЮЮ границу среди видимых, а карту оставляем лишь опорой по горизонтали.
	float BottomY = MapPos.Y + MapOff.Bottom;
	if (MinimapWidget && MinimapWidget->WidgetTree)
	{
		if (UPanelWidget* Root = Cast<UPanelWidget>(MinimapWidget->GetRootWidget()))
		{
			for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
			{
				UWidget* Ch = Root->GetChildAt(i);
				if (!Ch || Ch->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
				const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Ch);
				if (!CS) { continue; }
				const FAnchors A = CS->GetAnchors();
				if (!A.Minimum.Equals(A.Maximum)) { continue; }        // растянутый фон — не мерило
				const FMargin O = CS->GetOffsets();
				if (O.Bottom <= 0.f) { continue; }                     // авторазмер — высоты не знаем
				BottomY = FMath::Max(BottomY, O.Top + O.Bottom);
			}
		}
	}
	FVector2D Want(MapPos.X + MapOff.Right - PanelW, BottomY + Gap);

	// ★★★ 23.10.2026 [ОРИГИНАЛ] (панель Друга 6500, жалоба «SM bar»): в `.JUI` между мини-картой и занятостью стоит панель
	//   Друга `SOULMATE_POP_WIN` (y 200…300: шапка 200…264, два ряда значков эффектов Друга 267…300), а занятость — ровно
	//   под ней (−84, 300). Прижатие к низу мини-карты клало занятость (z выше) ПОВЕРХ значков эффектов Друга. Пока
	//   панель Друга на экране — занятость не выше её низа (низ = максимум Top+Bottom корневых узлов окна 6500, кроме
	//   канвы эффектов яблока `Soulmate_AppleBase_grp` 256×256 — она не занимает места). Ключ `asda.War.OccupationBelowSoulPop 0` — прежнее.
	if (CVarOccupationBelowSoulPop.GetValueOnGameThread() != 0)
	{
		UGameInstance* GIs = GetGameInstance();
		const UAsdaSoulmateSubsystem* Sm = GIs ? GIs->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UUserWidget* PopW = (Sm && Sm->IsPopCardShown()) ? Sm->GetPopWindow() : nullptr;
		UPanelWidget* PopRoot = PopW ? Cast<UPanelWidget>(PopW->GetRootWidget()) : nullptr;
		if (PopRoot)
		{
			float PopBottom = -1.f;
			for (int32 i = 0; i < PopRoot->GetChildrenCount(); ++i)
			{
				UWidget* Ch = PopRoot->GetChildAt(i);
				if (!Ch || Ch->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
				if (Ch->GetName().StartsWith(TEXT("Soulmate_AppleBase"))) { continue; }
				const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Ch);
				if (!CS) { continue; }
				const FMargin O = CS->GetOffsets();
				if (O.Bottom > 0.f) { PopBottom = FMath::Max(PopBottom, O.Top + O.Bottom); }
			}
			if (PopBottom > Want.Y) { Want.Y = PopBottom; }
		}
	}

	// Смещение считаем по ПЕРВОМУ узлу (подложке): его авторская позиция — база всей панели.
	if (OccupationNodes.Num() == 0 || OccupationBasePos.Num() != OccupationNodes.Num()) { return; }
	// Базой берём САМЫЙ ЛЕВЫЙ-ВЕРХНИЙ авторский узел, чтобы взаимная раскладка не поехала.
	FVector2D Base = OccupationBasePos[0];
	for (const FVector2D& P : OccupationBasePos)
	{
		Base.X = FMath::Min(Base.X, P.X);
		Base.Y = FMath::Min(Base.Y, P.Y);
	}
	const FVector2D Delta = Want - Base;
	for (int32 i = 0; i < OccupationNodes.Num(); ++i)
	{
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(OccupationNodes[i]))
		{
			// ★ Анкор у всех узлов панели авторский (правый верх) — его НЕ трогаем, двигаем только
			//   смещение внутри анкора: так панель остаётся приклеенной к тому же краю экрана.
			CS->SetPosition(OccupationBasePos[i] + Delta);
		}
	}
	UE_LOG(LogAsdaMinimap, Log,
		TEXT("[занятость] панель под окном мини-карты: низ карты %.0f, низ ОКНА %.0f, панель в (%.0f, %.0f), узлов %d"),
		MapPos.Y + MapOff.Bottom, BottomY, Want.X, Want.Y, OccupationNodes.Num());
}
