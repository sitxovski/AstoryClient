// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerController_Command.cpp — ★ R2 10.11.2026 (разрез AsdaPlayerController.cpp, дословный перенос): маршрутное движение и ExecuteCommand.
//   Общее для частей — в AsdaPlayerControllerInternal.h; шапка и реверс — в AsdaPlayerController.cpp.
#include "World/AsdaPlayerController.h"
#include "World/AsdaOrbitCursor.h"   // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "World/AsdaAttackExit.h"   // ★ 01.11.2026: 4024/400 — выход своего из действия атаки (AsdaLeaveAttackAction)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: клик по миру при открытой доске объявлений — только закрыть (input.c:177)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.10.2026: GetCameraTurnScale — чувствительность поворота камеры
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaWindowSubsystem.h" // ★ единый реестр зон интерфейса: блок мирового клика под ЛЮБЫМ окном

#include "World/AsdaPlayerCharacter.h"
#include "GameFramework/PlayerInput.h"   // ★ 09.10.2026 (п. 4): `FKeyState::EventCounts` — второе нажатие двойного клика ЛКМ
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaTargetHighlightComponent.h"   // ★ подсветка выбранной цели (эталон g_pTargetHighlightPosA/B)
#include "World/AsdaWorldSubsystem.h"
#include "AsdaSelf.h"                       // ★ О3: свой userNum для записи пика `g_nPickedUserIdx`
#include "World/AsdaLoadManagerSubsystem.h"   // ★ CurrentMapId — развилка CampWar (mapId 19) перед окном 570
#include "World/AsdaMapRegistry.h"           // ★ 04.11.2026: зоны порталов (kPortalGraph) — клик по графике портала
#include "World/AsdaPortalGfx.h"             // ★ 04.11.2026: меши набора портала (домики, ворота, арки) — клик по модели портала
#include "Components/StaticMeshComponent.h"  // меш попадания клика (экземпляры ISM карты)
#include "Engine/StaticMesh.h"               // имя меша попадания
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaNpcBodyRay.h"   // ★ 08.11.2026: спорный щелчок по NPC — по телу (asda.Npc.ClickByBody)
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaKeybindProfile.h"       // ★ Editor-Friendly раскладка + путь к system/Shortcutkey.ini
#include "World/AsdaAdminSubsystem.h"
#include "World/AsdaVehicleRideSubsystem.h"   // пасхалка «покататься» (пункт админ-меню)       // админ-режим (GM): хоткеи + свободная камера
#include "World/AsdaLootActor.h"            // наземный лут (клик-подбор, loot_system_re.md)
#include "World/AsdaCoord.h"                // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPathfinder.h"           // ★ поиск пути по сетке проходимости (обход препятствий)
#include "World/AsdaWalkGrid.h"
#include "Items/AsdaItemTableSubsystem.h"   // дальность/тип оружия из item-записи
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"   // ★ cmd 34 INTERFACE QUEST — журнал заданий (окно 700)
#include "UI/AsdaChatWindowSubsystem.h"        // cmd 37 «INTERFACE MAP» — карта мира (окно 1710)
#include "UI/AsdaDevMenuSubsystem.h"        // dev-меню Ctrl+N: пока открыто — игровые хоткеи подавлены
#include "UI/AsdaPremiumGachaSubsystem.h"   // ★ гейт `sub_922D5D@0xB6F370`: сцена премиум-гачи открыта
#include "UI/AsdaMessageSubsystem.h"        // ★★★ ЕДИНЫЙ маршрутизатор сообщений (нотисы клика/скриншота)
#include "UI/AsdaSoulGuardSubsystem.h"      // хоткеи 45/46/47 «SOUL GUARDIAN SKILL Lv1..3» (окно 56000)
#include "UI/AsdaTitleSubsystem.h"          // ★ cmd 38 «INTERFACE TITLE» — окно званий (JUI 18700)
#include "UI/AsdaPetCageSubsystem.h"        // ★ cmd 43 «INTERFACE PETBAG» — окно питомца (JUI 43000)
#include "UI/AsdaItemUpgradeSubsystem.h"    // ★ cmd 42 — тумблер окна улучшения снаряжения (JUI 20000)
#include "UI/AsdaCraftWindowSubsystem.h"      // ★ cmd 41 — окно создания снаряжения 76000; гейт клика по миру
#include "UI/AsdaDisassemblySubsystem.h"       // ★ cmd 40 — окно разборки снаряжения 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaTradeStateSubsystem.h"       // ★ 29.09.2026: гейт клика по земле 9568 — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "UI/AsdaWarehouseSubsystem.h"   // ★ 30.09.2026 (ревизия Л4/Л5, СРЕДН-1): свиток возврата склада (+0x5734) — гейт 10305
#include "UI/AsdaSelfStoreSubsystem.h"         // ★ 30.09.2026 (волна Л5 торговли): фокус в поле чата лавки 23403 глушит горячие клавиши
#include "UI/AsdaNotice.h"                    // нотис 9568 в чат (фасад нотисов)
#include "UI/AsdaCraftSubsystem.h"            // ★ предикат «окно ремесла открыто» (гейт клика по миру, input.c:144-157)
#include "UI/AsdaPlayerMenuSubsystem.h"    // ★ контекстное меню по чужому игроку (окно 2000), ПКМ-отпускание
#include "UI/AsdaSkillUseSubsystem.h"       // ★ отмена отложенного «подбежать и применить навык»
#include "UI/AsdaSystemOptionSubsystem.h"   // ★ ESC-меню 69400 и окно настроек 69500 (клавиша ESC, захват клавиш)
#include "World/AsdaTargetHotkeys.h"        // ★ 25.10.2026: клавиши 59 «атаковать цель» / 60 «ближайший враг» (эталонные тела)
#include "UI/AsdaPartySubsystem.h"          // ★ 25.10.2026: клавиши 62 (Shift+Tab) / 64 (следующая общая цель)
#include "UI/AsdaHotbarSubsystem.h"         // ★ 27.09.2026: иконка «сесть/встать» на хотбаре (`ToggleSitStand`)
#include "UI/AsdaFishWindowSubsystem.h"     // ★ 08.10.2026 (рыбалка В2): фильтр клавиш мини-игры `Input_FishingKeyFilter`
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В2): гейт «идёт рыбалка» (fishState ≠ 0)
#include "World/AsdaFishZoneSubsystem.h"    // ★ 08.10.2026 (рыбалка В4): зоны в мире — тип попадания 5 двойного клика
#include "World/AsdaPossessGate.h"          // ★ гейт боевых действий под GM-«скрыться» (AsdaGmHide)
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (гейт 4028, темп замаха)
#include "UI/AsdaPremiumShopSubsystem.h"     // ★ 17.10.2026 (Ф4): гейт PvP-клика «элитный магазин открыт» (helper_getField2075)
#include "UI/AsdaCraftMakeWindowSubsystem.h" // ★ Ф4: гейт PvP-клика «окно количества изготовления» (g_pItemQtyMakeDlg)
#include "UI/AsdaHairShopSubsystem.h"        // ★ Ф4: гейт PvP-клика «сессия парикмахерской» (CObject_GetField166)
#include "UI/AsdaBoosterPanelSubsystem.h"    // ★ Ф4: гейт PvP-клика «барабан бустера» (CObject_GetField254)
#include "UI/AsdaEffectsSubsystem.h"         // ★ Ф4: гейт PvP-клика «обездвижен» (нотис 1161)
#include "Resources/AsdaSoundSubsystem.h"    // ★ Ф4: звук 214 «нет оружия»
#include "UI/AsdaChatSubsystem.h"            // ★ Ф4: эталонные цвета нотисов PvP-клика (ColWhite/ColGold)
#include "Npc/AsdaNpcDialogSubsystem.h"     // ★ окно разговора с NPC (JUI 570) — этапы Э1/Э2
#include "Quest/AsdaQuestTypes.h"           // FAsdaNpcQuestNpcRow — высота AABB NPC (npc_list +0x68)
#include "AsdaDataTableCache.h"             // AsdaDt::RowByInt — та же точка, что у плашки/меток
#include "Components/CapsuleComponent.h"    // радиус цели (хитбокс моба)
#include "Components/SkeletalMeshComponent.h" // фильтр по имени модели в asda.Target.RingNearest
#include "Engine/SkeletalMesh.h"
#include "Components/InputComponent.h"  // ★ UInputComponent: сборка без unity (-SingleFile) падала без него
#include "Engine/Engine.h"              // ★ GEngine: то же
#include "AsdaNetSubsystem.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"   // ★ modal_lock2: препроцессор мыши под модалкой (FAsdaModalMouseRelay)
#include "InputKeyEventArgs.h"                       // ★ modal_lock3: оси MouseX/MouseY препроцессора в UPlayerInput
#include "Misc/App.h"                                 // ★ modal_lock3: FApp::GetDeltaTime
#include "Widgets/SViewport.h"                       // ★ modal_lock2: событие — над игровым вьюпортом
#include "HAL/IConsoleManager.h"            // ★ ключи отката: asda.Target.Ring, asda.Npc.OpenOnSingleClick
#include "EngineUtils.h"                    // TActorIterator — живая проверка asda.Target.RingNearest
#include "AsdaEffectActor.h"                // фактика кольца в asda.Target.RingNearest (имя .jeb = цвет)
#include "AsdaFxNames.h"                    // ★ дедуп №2 В1-C (F5/F6): тег кольца цели, имя BP метки клика
#include "Resources/AsdaAssetPaths.h"       // ★ дедуп №2 В1-C (F5): путь класса эффект-BP по имени (FxClassPathFromBp)
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformApplicationMisc.h"
#if PLATFORM_WINDOWS
// ★ PrintScreen (VK 44, cmd 44 «SCREEN SHOT») — единственная биндабельная клавиша оригинала,
//   которой в `EKeys` UE нет вовсе, поэтому её приходится опрашивать платформенно.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#endif
#include "AsdaPlayerControllerInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerController*.cpp (LogAsdaPC, чистые хелперы)
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя (клик → цель → бег → дальность → заявка → замах → выход)
#include "World/AsdaSkillCastLock.h"  // ★ 05.11.2026: «свой навык занят» — гейты клика/клавиш цели, отложенное действие после навыка
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaPlayerControllerInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPlayerController*.cpp

#if ASDA_COMBAT_TRACE
static FAsdaCtOnce GAsdaCtRouteOnce;      // «маршрут подхода» (StartRoutedMove)
static FAsdaCtOnce GAsdaCtLegOnce;        // «отрезок подхода» (SendRouteLeg)
#endif // ASDA_COMBAT_TRACE

// ── ★ ПОСТРОЕНИЕ МАРШРУТА (порт mapClick_BeginPathMove @0xFBC9A0) ────────────────────────────────
//   1) прямая видимость до цели → один отрезок (эталон game_main.c:1637 проверяет LOS ДО запуска поиска);
//   2) иначе поиск по сетке проходимости (AsdaPath::FindPathWorld) и движение по узлам.
void AAsdaPlayerController::StartRoutedMove(const FVector2D& DstGame, int32 TargetRenderId, bool bApproach)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch || IsSelfDead())
	{
		ASDA_CTRACE(EAsdaCT::Move, TEXT("маршрут к (%.0f,%.0f) не начат: %s"), DstGame.X, DstGame.Y, Ch ? TEXT("свой персонаж мёртв") : TEXT("нет персонажа"));
		return;
	}
	// ★ 22.10.2026 (ревизия стана, ВАЖНО-1 и мелочь 2): под обездвиживанием маршрут не начинается вовсе — ДО «игрок начал двигаться»
	//   (иначе подход боя/навыка раз в 0,4 с закрывал окна NPC, снимал автопуть и копил строки журнала). Молча: плашку показывают
	//   входы игрока (клик, лут, NPC, карта).
	//   ★ Мелочь ревизии локального старта: эталон маршрут под состоянием не гейтит (следование шлёт 4006 → result 6) — гейт шире
	//   эталона, поэтому только под ключом `asda.Move.StunHardStop` [ИСПРАВЛЕНИЕ]; ключ 0 — прежнее дословно.
	if (IsStunHardStopKey() && IsMoveInputLockedByState())
	{
		UE_LOG(LogAsdaPC, Verbose, TEXT("[путь] маршрут к (%.0f,%.0f) не начат: персонаж обездвижен"), DstGame.X, DstGame.Y);
		return;
	}
	const FVector2D Cur = Ch->GetGamePosXZ();

	// ★ ОБЩАЯ ТОЧКА «игрок начал двигаться»: сюда сходятся клик по земле, мини-карта, лут,
	//   подбегание к NPC, подход ради навыка и погоня в бою (см. `NotifyPlayerStartedMoving`).
	NotifyPlayerStartedMoving(TEXT("новый приказ движения"));

	CancelRoute();
	RouteFinalDst = DstGame;
	RouteRenderId = TargetRenderId;
	bRouteApproach = bApproach;             // ★ 27.09.2026 (К1): признак подхода — для гейта клетки в `SendRouteLeg`
	RouteReplanTime = FPlatformTime::Seconds() + 1.0;

	FAsdaPathStats Stats;
	TArray<FVector2D> Pts;
	const UAsdaWalkGrid* Grid = Ch->WalkGrid;
	if (!AsdaPath::FindPathWorld(Grid, Cur, DstGame, Pts, &Stats) || Pts.Num() == 0)
	{
		// Маршрута нет (цель в замкнутой области) — эталон в этом случае просто не идёт.
		UE_LOG(LogAsdaPC, Log, TEXT("[путь] маршрут не построен: (%.0f,%.0f)→(%.0f,%.0f) развёрнуто=%d %.2f мс"),
			Cur.X, Cur.Y, DstGame.X, DstGame.Y, Stats.Expanded, Stats.MilliSec);
		ASDA_CTRACE(EAsdaCT::Move, TEXT("маршрут НЕ построен (%s, rid цели %d): (%.0f,%.0f)→(%.0f,%.0f) — цель в замкнутой области, не идём"),
			bApproach ? TEXT("подход к цели") : TEXT("ход"), TargetRenderId, Cur.X, Cur.Y, DstGame.X, DstGame.Y);
		return;
	}
	Route = MoveTemp(Pts);
	RouteIdx = 0;
#if ASDA_COMBAT_TRACE
	// Подход к цели/боевой — событие цикла; обычный ход — частый уровень.
	if (bApproach || TargetRenderId >= 0)
	{
		// Подход перестраивается каждые 0.4 с: первая строка по цели в цикле — уровень 1, повторы — уровень 2.
		ASDA_CTRACE_IF(AsdaCtOnceFirst(GAsdaCtRouteOnce, TargetRenderId), EAsdaCT::Move,
			TEXT("маршрут подхода (rid цели %d): (%.0f,%.0f)→(%.0f,%.0f), узлов %d, первый узел (%.0f,%.0f)%s"),
			TargetRenderId, Cur.X, Cur.Y, DstGame.X, DstGame.Y, Route.Num(), Route[0].X, Route[0].Y,
			Stats.Expanded <= 0 ? TEXT(" [прямая видимость]") : (Stats.bReachedGoal ? TEXT(" [поиск пути]") : TEXT(" [цель недостижима — к ближайшей точке]")));
		AsdaCtOnceMark(GAsdaCtRouteOnce, TargetRenderId);
	}
	else
	{
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("маршрут хода: (%.0f,%.0f)→(%.0f,%.0f), узлов %d"), Cur.X, Cur.Y, DstGame.X, DstGame.Y, Route.Num());
	}
#endif
	if (Stats.Expanded > 0)
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[путь] (%.0f,%.0f)→(%.0f,%.0f): узлов %d (сырых тайлов %d, поворотов %d), "
			"стоимость %.1f, развёрнуто %d, %.2f мс%s%s"),
			Cur.X, Cur.Y, DstGame.X, DstGame.Y, Stats.Waypoints, Stats.RawLen, Stats.TurnPoints,
			Stats.CostG, Stats.Expanded, Stats.MilliSec,
			Stats.bReachedGoal ? TEXT("") : TEXT(" [ЦЕЛЬ НЕДОСТИЖИМА — идём к ближайшей точке]"),
			Stats.bGoalMoved ? TEXT(" [цель была в стене — сдвинута]") : TEXT(""));
	}
	SendRouteLeg(Cur, Route[0]);
}

// Отправка одного отрезка маршрута: пакет 4006 + локальное движение.
void AAsdaPlayerController::SendRouteLeg(const FVector2D& Cur, const FVector2D& Wp)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch) { return; }
	// ★★ ПОСЛЕДНИЙ РУБЕЖ: мёртвым НЕ шлём 4006 и НЕ запускаем локальное предсказание движения.
	//   Сервер всё равно режет ход (6001 причина 1), а клиент до этой правки успевал уехать локально —
	//   отсюда «персонаж бегает, хотя мёртв» из живого теста.
	if (IsSelfDead()) { CancelRoute(); return; }
	// ★ 22.10.2026 [ИСПРАВЛЕНИЕ] под «станом» (`asda.Move.StunHardStop`) отрезков нет: ни 4006 (сервер ответил бы result 6), ни
	//   локального шага. Маршрут снимается; автопуть ждёт снятия сам (`UAsdaMinimapSubsystem::TickMapTravel`).
	if (IsStunHardStopped()) { UE_LOG(LogAsdaPC, Verbose, TEXT("[стан] отрезок маршрута не отправлен: персонаж обездвижен")); CancelRoute(); return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bEtalon = IsEtalonSpeedSync();
	bool bSent = false;
	// ★★★ 27.09.2026 (исправление К1 ревизии хода 4007) — ПОДХОД К ЦЕЛИ ВНУТРИ СВОЕЙ КЛЕТКИ 100×100 ИДЁТ БЕЗ 4006.
	//   [ОРИГИНАЛ] Сервер `CharMove@0x544EB0` отклоняет ход в свою клетку: `if (тайл(pUser) == тайл(dst)) { result 0 }`
	//   (GameServer.c:124534; тайл = (int)(коорд/100), пишется `CharStop` :124767 и ходом :54527). Эталонный подход
	//   (`Player_ExecuteSkillOnTarget@0x9F16D0`) поэтому шлёт 4006 ТОЛЬКО при `terrainCellIndexFromXZChecked(цель) !=
	//   self+0x2E878` (game.c:68302, 68553, 68590, 68764, 68827, 68854, 69041, 69064; клетка = координата / 100,
	//   `mapGrid_WorldPosToCellIndex@0x1139B00`), а внутри клетки персонаж доходит шагом, уже идущим к цели.
	//   Без этого гейта при ключе 1 грант 0 закрывал маршрут, подход повторялся через 0.4 с с той же геометрией —
	//   персонаж стоял и не бил (ревизия К1).
	//   [ПОРТ] 1) Клетку «своего» берём по ТЕКУЩЕЙ позиции, а не как эталон — по клетке точки последнего гранта
	//   (`self+0x2E878`, писатели только `charMoveToWorldPos` 0xA1E56F и `charMoveStepToCell` 0xA1EA12): после стопа
	//   посреди отрезка (вход в бой) поле эталона указывает на клетку старой цели, а сервер сравнивает с клеткой
	//   своей точки стопа — текущая позиция предсказывает отказ сервера точно. 2) Сравниваем клетку ТОЧКИ ОТРЕЗКА
	//   (у порта подход идёт в стоп-точку на границе дальности, у эталона — в саму цель): гейт стоит на том, что
	//   реально уходит в 4006. 3) Шаг внутри клетки исполняется локально, и незакрытый ход помечается
	//   (`bActionPending`), чтобы стоп перед ударом/навыком (4008) сообщил серверу новую точку — `CharStop@0x545C70`
	//   принимает её при расхождении ≤ 1000 (GameServer.c, ветка LABEL_35); при ключе 0 то же самое делал
	//   отклонённый сервером 4006.
	//   Клик по земле и узлы обычного маршрута гейтом НЕ охвачены: эталонный `CGame_HandleGroundClickAttack` шлёт
	//   4006 без проверки клетки, и в своей клетке персонаж по отказу 0 не идёт — так и у нас.
	if (bEtalon && bRouteApproach && Net && Net->bInGameConnected
		&& AsdaCoord::GameToTile(Wp) == AsdaCoord::GameToTile(Cur))
	{
		bRouteLegUnsent = false;
		Net->bActionPending = true;   // [ПОРТ] незакрытый ход: 4008 на входе в бой/перед навыком сообщит точку
		UE_LOG(LogAsdaPC, Log, TEXT("[путь] подход в своей клетке (%d,%d): (%.0f,%.0f)→(%.0f,%.0f) — без 4006, шагом (эталон)"),
			AsdaCoord::GameToTile(Cur).X, AsdaCoord::GameToTile(Cur).Y, Cur.X, Cur.Y, Wp.X, Wp.Y);
		ASDA_CTRACE(EAsdaCT::Move, TEXT("подход (rid %d) в своей клетке: (%.0f,%.0f)→(%.0f,%.0f) — без 4006, локальный шаг, ход помечен незакрытым"),
			RouteRenderId, Cur.X, Cur.Y, Wp.X, Wp.Y);
		Ch->NetMove(Cur, Wp, Net->LastServerSpeed);
		return;
	}
	// ★ АНТИЧИТ-ГАРД: ход без реального перемещения не шлём (сервер считает шаги
	//   ROUND(sqrt(dx²+dz²)/speed) и на «пустых» ходах копит Hack Position-счётчик).
	if (Net && Net->bInGameConnected && FVector2D::Distance(Cur, Wp) > kMoveMinSendDist)
	{
		// ★ 27.09.2026 (эталонная схема): пока предыдущий 4006 ждёт ответа (бит 2), эталонный клик отказан
		//   (`CGame_HandleGroundClickAttack@0x996480`: `if ((flags & 4) || (flags & 2)) return 0;`). Отрезок не
		//   теряем — он уйдёт из `RouteTick`, как только ответ придёт (см. `bRouteLegUnsent`).
		if (bEtalon && Net->IsMoveRequestInFlight())
		{
			bRouteLegUnsent = true;
			UE_LOG(LogAsdaPC, Verbose, TEXT("[путь] отрезок к (%.0f,%.0f) отложен: прошлый запрос хода ещё без ответа"), Wp.X, Wp.Y);
			ASDA_CTRACE_V(EAsdaCT::Move, TEXT("отрезок к (%.0f,%.0f) отложен: прошлый 4006 ещё без ответа (rid цели %d)"), Wp.X, Wp.Y, RouteRenderId);
			return;
		}
		if (RouteRenderId >= 0) { bSent = Net->SendMove4006(Cur.X, Cur.Y, Wp.X, Wp.Y, RouteRenderId); }
		else                    { bSent = Net->SendMove4006(Cur.X, Cur.Y, Wp.X, Wp.Y); }
	}
#if ASDA_COMBAT_TRACE
	if (bRouteApproach || RouteRenderId >= 0)
	{
		// Отрезок уходит с каждым перезапросом подхода (0.4 с): первая строка по цели в цикле — уровень 1, повторы — уровень 2.
		ASDA_CTRACE_IF(AsdaCtOnceFirst(GAsdaCtLegOnce, RouteRenderId), EAsdaCT::Move,
			TEXT("отрезок подхода (rid цели %d, узел %d/%d): (%.0f,%.0f)→(%.0f,%.0f), %.0f юн — 4006 %s%s"),
			RouteRenderId, RouteIdx + 1, Route.Num(), Cur.X, Cur.Y, Wp.X, Wp.Y, FVector2D::Distance(Cur, Wp),
			bSent ? TEXT("ушёл") : TEXT("не ушёл (офлайн/короткий/гейт)"),
			(bEtalon && bSent) ? TEXT(", шаг начнёт грант 4007") : TEXT(", шаг локально сразу"));
		AsdaCtOnceMark(GAsdaCtLegOnce, RouteRenderId);
	}
	else
	{
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("отрезок хода: (%.0f,%.0f)→(%.0f,%.0f) — 4006 %s"), Cur.X, Cur.Y, Wp.X, Wp.Y, bSent ? TEXT("ушёл") : TEXT("не ушёл"));
	}
#endif
	bRouteLegUnsent = false;
	// ★★★ 27.09.2026 (эталонная схема, `asda.Move.EtalonSpeedSync 1`): после 4006 персонаж НЕ трогается —
	//   эталон `CGame_HandleGroundClickAttack` после `Combat_SendNormalAttack` локального хода не начинает, шаг
	//   запускает только грант 4007 (`Recv_EntityMove` case 1 → `charMoveToWorldPos`). Идущий шаг прошлого отрезка
	//   при этом НЕ гасится — он доигрывается до гранта (эталон так же: тик своего персонажа при взведённом бите 2
	//   продолжает `CCharacter_ApplyMoveState`). Если 4006 не ушёл (офлайн, короткий отрезок, сбой) — ответа не
	//   будет, и ход исполняется локально, как раньше.
	// ★ 22.10.2026: локальный старт (`asda.Move.LocalStart`, разбор — AsdaPlayerKeyboardEtalon.cpp): клик/маршрут трогается сразу,
	//   грант подгонит ход; подход к цели боя/навыка — по гранту, как прежде.
	if (bEtalon && bSent)
	{
		if (!bRouteApproach && RouteRenderId < 0) { LocalStartAfter4006(Ch, Cur, Wp, TEXT("клик/маршрут")); }
		else                                       { NoteMove4006WithoutLocalStart(); }   // ВАЖНО-5: подход боя — по гранту
		return;
	}
	// ★ Прежний порт: двигаемся ЛОКАЛЬНО сразу. Сервер держит через result=5 (анти-чит снап в OnNetEntityMove).
	Ch->NetMove(Cur, Wp, (Net && Net->bInGameConnected) ? Net->LastServerSpeed : 0.35f);
}

void AAsdaPlayerController::ExecuteCommand(int32 Cmd)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	switch (Cmd)
	{
	// ★★★ ПАНЕЛЬ БЫСТРОГО ДОСТУПА — СТРАНИЦЫ (cmd 1..5).
	//   Эталон `hotkeyDispatchAction@0xE3CD90`, второй switch: `Hotbar_SetPage(g_pQuickSlotBar, 0..4)`.
	//   По ini оригинала это F1..F5 (а НЕ Shift+F1..F5 из хардкод-дефолтов).
	//   Страницы ровно пять: `hotbarSetCurrentPage@0xDEE9F0` клампит `a2 <= 4`; смена страницы
	//   на сервер ничего не шлёт и нигде не сохраняется.
	case 1: case 2: case 3: case 4: case 5:
		OnHotbarPage.Broadcast(Cmd - 1);
		UE_LOG(LogAsdaPC, Verbose, TEXT("[cmd%d] %s → страница хотбара %d"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd), Cmd - 1);
		return;
	// ★★★ ПАНЕЛЬ БЫСТРОГО ДОСТУПА — СЛОТЫ (cmd 6..17 основной ряд, 18..29 второй ряд «NEW BAR»).
	//   Эталон, первый switch: `Hotbar_ActivateSlotByIndex(g_pQuickSlotBar, Cmd-6)` — номер колонки
	//   для ОБОИХ диапазонов считается одинаково (6→0 … 29→23), а уже внутри
	//   `Hotbar_ActivateSlotByIndex@0xDEE650` колонка превращается в индекс слота:
	//   `slot = col + 13*page` для col<12 и `slot = col + 53` для col>=12.
	//   По ini оригинала: 1..9,0,UP,DOWN и SHIFT+1..SHIFT+9,LEFT,[,] (а не 1..0,-,= и F1..F12).
	case 6:  case 7:  case 8:  case 9:  case 10: case 11: case 12: case 13: case 14: case 15:
	case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25:
	case 26: case 27: case 28: case 29:
		// Защёлка `CHotbar+24480+4*col` (ставится при активации, снимается вторым switch'ом того же
		//   вызова) в порту не нужна: PollKeys стреляет РОВНО ОДИН раз — по фронту отпускания.
		ASDA_CTRACE(EAsdaCT::Input, TEXT("клавиша cmd%d «%s» → слот хотбара, колонка %d (навык/предмет решает хотбар); цель %s, автоатака=%s"),
			Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd), Cmd - 6, *AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()), bAutoAttack ? TEXT("да") : TEXT("нет"));
		OnHotbarSlot.Broadcast(Cmd - 6);
		UE_LOG(LogAsdaPC, Verbose, TEXT("[cmd%d] %s → слот хотбара, колонка %d"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd), Cmd - 6);
		return;
	// ★ ЧАТ (Shortcutkey.ini): 66 CHAT OPEN CLOSE · 67 CHAT NORMAL · 68 CHAT SHOUT ·
	//   69 CHAT ANSWER · 70 CHAT WHISPER · 71 CHAT PARTY · 72 CHAT CLAN · 73 CHAT SOULMATE.
	//   Номера состояний ввода — таблица эталона (`uiQuestInput_UpdatePromptByState`):
	//   NORMAL → 4 «[Общий]», SHOUT → 3 «[Глобальный чат]», WHISPER → 7, PARTY → 5,
	//   CLAN → 6, SOULMATE → 8; ANSWER — шёпот последнему отправителю.
	case 66: case 67: case 68: case 69: case 70: case 71: case 72: case 73:
		if (UAsdaChatWindowSubsystem* CW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
		{
			switch (Cmd)
			{
			case 66: CW->ToggleInput();            break;
			case 67: CW->OpenInput(4);             break;
			case 68: CW->OpenInput(3);             break;
			case 69: CW->AnswerLastWhisper();      break;
			case 70: CW->OpenInput(7);             break;
			case 71: CW->OpenInput(5);             break;
			case 72: CW->OpenInput(6);             break;
			case 73: CW->OpenInput(8);             break;
			}
			UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] команда чата → режим ввода %d"), Cmd, CW->GetInputState());
		}
		return;
	case 44:   // SCREEN SHOT (эталон: .\Screen Save\*.jpg; UE — высокого разрешения в Saved/Screenshots).
		//   ★ ЭТАЛОН render_core.c:1826-1838: по итогу сохранения строка БЕЛЫМ (−1) на 10 000 мс в
		//   СИСТЕМНЫЙ ЧАТ (CChat_AddTokenizedLines, track 0): успех → loc 1102 «Изображение сохранено.»,
		//   ошибка → loc 1103 «Невозможно сохранить изображение.». В порту сообщения не было вовсе.
		if (GEngine && GEngine->GameViewport)
		{
			FScreenshotRequest::RequestScreenshot(false);
			UE_LOG(LogAsdaPC, Log, TEXT("[cmd44] SCREEN SHOT"));
			if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				FAsdaMsgParams MP;
				MP.Category = EAsdaMsgCategory::Tip;
				MP.LocId = 1102;                       // «Изображение сохранено.»
				// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → «[#1102]».
				Msg->Post(MP);
			}
		}
		else if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			FAsdaMsgParams MP;
			MP.Category = EAsdaMsgCategory::Tip;
			MP.LocId = 1103;                           // «Невозможно сохранить изображение.»
			// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → «[#1103]».
			Msg->Post(MP);
		}
		return;
	// ★★★ 30/31 «INTERFACE CHARACTOER» / «INTERFACE EMOTION» — ОКНО ХАРАКТЕРИСТИК (900).
	//   Эталон `hotkeyDispatchAction@0xE3CD90` (input.c:4273/4276):
	//     case 30: uiWindowToggleTab(g_pCharaInfoWin, 0);   // вкладка «Статус»  — клавиша C
	//     case 31: uiWindowToggleTab(g_pCharaInfoWin, 1);   // вкладка «Эмоции»  — клавиша E
	//   ⚠ До 2026-08-23 обеих веток не было: команды падали в `default:` и уходили в `OnGameCommand`,
	//   где их никто не слушал — клавиши C и E не делали ничего (та же дыра, что была у журнала).
	case 30: case 31:
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaCharaInfoSubsystem* CI = GI->GetSubsystem<UAsdaCharaInfoSubsystem>())
			{
				const int32 Tab = (Cmd == 30) ? 0 : 1;
				CI->ToggleTab(Tab);
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] %s → окно характеристик, вкладка %d, теперь %s"),
					Cmd, (Cmd == 30) ? TEXT("INTERFACE CHARACTOER") : TEXT("INTERFACE EMOTION"),
					Tab, CI->IsWindowOpen() ? TEXT("открыто") : TEXT("закрыто"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd%d] окно характеристик: подсистема недоступна"), Cmd);
		OnGameCommand.Broadcast(Cmd);
		return;
	case 34:   // ★★★ INTERFACE QUEST (Shortcutkey.ini «INTERFACE QUEST: Q») — ЖУРНАЛ ЗАДАНИЙ, окно 700.
		//   Эталон `input.c:4287`: `case 34: if (g_bQuestGuideOpen) uiWnd_Close(g_pQuestMgr);
		//                                    else uiQuestGuide_Open(g_pQuestMgr);` — ТУМБЛЕР.
		//   ⚠ До 2026-08-22 этой ветки не было: команда падала в `default:` и уходила в
		//   `OnGameCommand`, где её никто не слушал — клавиша Q не делала ничего.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaQuestLogSubsystem* QL = GI->GetSubsystem<UAsdaQuestLogSubsystem>())
			{
				QL->Toggle();
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd34] INTERFACE QUEST → журнал заданий %s"),
					QL->IsOpen() ? TEXT("открыт") : TEXT("закрыт"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd34] INTERFACE QUEST: подсистема журнала недоступна"));
		OnGameCommand.Broadcast(Cmd);
		return;
	case 38:   // ★★★ INTERFACE TITLE (Shortcutkey.ini «INTERFACE TITLE: T») — ОКНО ЗВАНИЙ, JUI 18700.
		//   Эталон `hotkeyDispatchAction@0xE3CD90`, `input.c:4303`:
		//     `case 38: questLogWnd_Open(&g_pCharTitleMgr);`
		//   ⚠ Имя `questLogWnd_Open` — МИСНЕЙМ декомпила (закон [[false-negatives-method]]): это
		//   открытие МЕНЕДЖЕРА ЗВАНИЙ `g_pCharTitleMgr`, а не журнала заданий (тот — cmd 34).
		//   Метод-порт `UAsdaTitleSubsystem::OpenTitleWindow()` — ТУМБЛЕР (эталон: открытое окно
		//   гасит через `uiTitleHide`), поэтому повторное нажатие T закрывает окно; своей защёлки
		//   тут не нужно — `PollKeys` стреляет ровно раз по фронту отпускания.
		//   ⚠ До этой правки ветки не было: cmd 38 падала в `default:` → `OnGameCommand`, где
		//   подписчиков нет — клавиша T не делала ничего (та же дыра, что была у cmd 34/30/31).
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaTitleSubsystem* TS = GI->GetSubsystem<UAsdaTitleSubsystem>())
			{
				// Возврат false = отказ гейта эталона (открыт магазин NPC / окно предмета / нумпад).
				const bool bAccepted = TS->OpenTitleWindow();
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd38] INTERFACE TITLE → окно званий %s%s"),
					TS->IsTitleWindowOpen() ? TEXT("открыто") : TEXT("закрыто"),
					bAccepted ? TEXT("") : TEXT(" (отклонено гейтом эталона)"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd38] INTERFACE TITLE: подсистема званий недоступна"));
		OnGameCommand.Broadcast(Cmd);
		return;
	case 40:   // ★★★ INTERFACE DISASSEMBLY (Shortcutkey.ini «INTERFACE DISASSEMBLY:») — ОКНО РАЗБОРКИ 32000 (волна 2-А, 06.10.2026).
		//   Эталон `hotkeyDispatchAction@0xE3CD90`, `input.c:4308-4309`:
		//     `case 40: upgradeUiOnOpen((CDisjoint *)g_pRecvUiHandler3, result);`
		//   `upgradeUiOnOpen` — ТУМБЛЕР окна 32000 (имя IDA врёт); гейтов и звука у ветки нет.
		//   До этой правки cmd 40 падала в `default:` → `OnGameCommand` без подписчиков — клавиша не делала ничего.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaDisassemblySubsystem* Dis = GI->GetSubsystem<UAsdaDisassemblySubsystem>())
			{
				Dis->Toggle(TEXT("клавиша действия 40"));
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd40] окно разборки 32000 → %s"), Dis->IsOpen() ? TEXT("открыто") : TEXT("закрыто"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd40] подсистема окна разборки недоступна"));
		OnGameCommand.Broadcast(Cmd);
		return;
	case 41:   // ★★★ INTERFACE REFINE (Shortcutkey.ini:41 «INTERFACE REFINE: Z») — ОКНО СОЗДАНИЯ СНАРЯЖЕНИЯ 76000.
		//   Эталон `hotkeyDispatchAction@0xE3CD90`, `input.c:4311-4313`:
		//     `case 41: tileInteractGateAndOpen(g_pItemMainWnd, result);`
		//   Тумблер окна списка рецептов со всеми гейтами (волна В2 ремесла, 25.09.2026). Звука у ветки нет.
		//   До этой правки cmd 41 падала в `default:` → `OnGameCommand` без подписчиков — Z не делала ничего.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaCraftWindowSubsystem* Craft = GI->GetSubsystem<UAsdaCraftWindowSubsystem>())
			{
				const bool bDone = Craft->GateAndToggle(TEXT("клавиша действия 41 (Z)"));
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd41] окно создания снаряжения 76000 → %s%s"),
					Craft->IsOpen() ? TEXT("открыто") : TEXT("закрыто"), bDone ? TEXT("") : TEXT(" (отклонено гейтом)"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd41] подсистема окна ремесла недоступна"));
		OnGameCommand.Broadcast(Cmd);
		return;
	case 42:   // ★★★ ОКНО «УЛУЧШЕНИЕ СНАРЯЖЕНИЯ» (заточка), JUI 20000 — ревизия R-2 волн 2–4 (25.09.2026).
		//   Эталон `hotkeyDispatchAction@0xE3CD90`, `input.c:4314-4316`:
		//     `case 42: uiCombineWindow_Close((CInventoryWindow *)g_pInventoryWnd);`
		//   ⚠ `uiCombineWindow_Close@0xD28340` — МИСНЕЙМ: это ТУМБЛЕР окна 20000 (видимо ⇒ спрятать, иначе
		//   полный сброс и показ). Своих гейтов у ветки нет (гейтов открытия у окна нет вовсе).
		//   До этой правки cmd 42 падала в `default:` → `OnGameCommand` без подписчиков.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaItemUpgradeSubsystem* Upg = GI->GetSubsystem<UAsdaItemUpgradeSubsystem>())
			{
				Upg->ToggleWindow();
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd42] окно улучшения снаряжения 20000 → %s"),
					Upg->IsOpen() ? TEXT("открыто") : TEXT("закрыто"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd42] подсистема окна улучшения снаряжения недоступна"));
		OnGameCommand.Broadcast(Cmd);
		return;
	case 43:   // ★★★ INTERFACE PETBAG (Shortcutkey.ini «INTERFACE PETBAG:») — ОКНО ПИТОМЦА, JUI 43000.
		//   Эталон `hotkeyDispatchAction@0xE3CD90`, `input.c:4317-4319`:
		//     `case 43: CRefineWnd_Open((CRefineWnd *)g_bDeviceReloadPending);`
		//   ⚠ Оба имени — МИСНЕЙМЫ (закон `false-negatives-method`): `CRefineWnd_Open@0xDC3390` —
		//   это открытие окна ПИТОМЦА «Дом для питомца», а `g_bDeviceReloadPending` — глобал
		//   `g_PetData`. Метод-порт `UAsdaPetCageSubsystem::OpenCage()` — ТУМБЛЕР с четырьмя
		//   гейтами оригинала (открыто окно создания предмета / количества / 43500 / 44200);
		//   false = окно закрылось тумблером либо сработал гейт (нотиса у гейтов нет).
		//   ⚠ До этой правки ветки не было: cmd 43 падала в `default:` → `OnGameCommand`,
		//   где подписчиков нет — клавиша не делала ничего.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaPetCageSubsystem* PetWin = GI->GetSubsystem<UAsdaPetCageSubsystem>())
			{
				const bool bOpened = PetWin->OpenCage();
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd43] INTERFACE PETBAG → окно питомца 43000 %s"),
					bOpened ? TEXT("открыто") : TEXT("закрыто/отклонено гейтом"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd43] INTERFACE PETBAG: подсистема окна питомца недоступна"));
		OnGameCommand.Broadcast(Cmd);
		return;
	case 37:   // ★ INTERFACE MAP (Shortcutkey.ini «INTERFACE MAP: M») — КАРТА МИРА, окно 1710 MAXMAP_WIN.
		//   Эталон hotkeyDispatchAction@0xE3CD90 case 37 → inventory_ToggleControlBarWindow@0xD64A30.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaMinimapSubsystem* MM = GI->GetSubsystem<UAsdaMinimapSubsystem>())
			{
				MM->ToggleWorldMap();
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd37] INTERFACE MAP → карта мира %s"),
					MM->IsWorldMapOpen() ? TEXT("открыта") : TEXT("закрыта"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd37] INTERFACE MAP: подсистема карты недоступна"));
		OnGameCommand.Broadcast(Cmd);
		return;
	// ★★★ 45/46/47 «SOUL GUARDIAN SKILL Lv1/2/3» — три ряда окна Soul Boost (56000).
	//   Эталон input.c:4319-4328: `case 45/46/47 → hudQuickSlot_TryUseSkill(g_pSoundOrAnimCtx, 0/1/2)`.
	//   Дефолтные клавиши (keybindLoadDefaults@0xE3A9C0): Insert / Home / Page Up, без модификатора.
	case 45: case 46: case 47:
		if (UAsdaSoulGuardSubsystem* SG = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulGuardSubsystem>() : nullptr)
		{
			const int32 Row = Cmd - 45;
			const bool bSent = SG->TryUseSkill(Row);
			UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] SOUL GUARDIAN SKILL Lv%d → %s"),
				Cmd, Row + 1, bSent ? TEXT("6158 отправлен") : TEXT("отказ"));
			ASDA_CTRACE(EAsdaCT::Skill, TEXT("клавиша cmd%d: навык хранителя души Lv%d → %s; цель %s"), Cmd, Row + 1,
				bSent ? TEXT("6158 отправлен") : TEXT("отказ подсистемы"), *AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()));
			return;
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd%d] SOUL GUARDIAN: подсистема хранителя недоступна"), Cmd);
		return;
	// ★★★ 74…85 — ЭМОЦИИ ПО ГОРЯЧЕЙ КЛАВИШЕ (`Shortcutkey.ini`, эталон `hotkeyDispatchAction@0xE3CD90`,
	//   input.c:4427-4461): каждая команда зовёт `Emote_Execute(0, индекс)` того же лотка «обычных»,
	//   что и вкладка эмоций окна 900. Соответствие «команда → индекс» снято с эталона дословно.
	case 74: case 75: case 76: case 77: case 78: case 79:
	case 80: case 81: case 82: case 83: case 84: case 85:
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaCharaInfoSubsystem* CI = GI->GetSubsystem<UAsdaCharaInfoSubsystem>())
			{
				//                     74 BOW 75 THANK 76 ANGER 77 SAD 78 HAPPY 79 FOLLOW
				//                     80 DUEL 81 DANCE1 82 DANCE2 83 COURTSHIP 84 JUGGLING 85 UPPERARM
				static const int32 kEmoteIdx[12] = { 4, 3, 1, 2, 0, 5, 6, 7, 8, 11, 9, 10 };
				const int32 Idx = kEmoteIdx[Cmd - 74];
				const bool bOk = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(0, Idx));
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] %s → эмоция %d: %s"),
					Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd), Idx, bOk ? TEXT("проиграна") : TEXT("отклонена гейтом"));
				return;
			}
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd%d] эмоция: подсистема окна характеристик недоступна"), Cmd);
		return;
	case 65:   // ACTION PRIVATESHOP (по умолчанию Y) — личная лавка.
		// ★ 29.09.2026 (волна Л1 торговли): эталон `hotkeyDispatchAction@0xE3CD90` case 65 (input.c:4396-4397) зовёт
		//   `UI_DispatchTargetContextMenu(g_pLocalPlayer, …, 2, 7)` — тот же вход, что пункт 7 «Действий» окна эмоций
		//   (гейт «верхом» 2040, затем `if (!sub_934E86(g_pSellWindow)) netSendPlayerInfo6300`). Одна точка — `PlayEmotion`.
		if (UAsdaCharaInfoSubsystem* CI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr)
		{
			const bool bOk = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 7));
			UE_LOG(LogAsdaPC, Log, TEXT("[cmd65] ACTION PRIVATESHOP → функция 2/7 (личная лавка): %s"), bOk ? TEXT("6300 отправлен") : TEXT("отказ/молча"));
			return;
		}
		UE_LOG(LogAsdaPC, Warning, TEXT("[cmd65] ACTION PRIVATESHOP: подсистема окна характеристик недоступна"));
		return;
	// ★ 29.09.2026 (волна О1 торговли): 57 / 58 (по умолчанию N / B, `input_camera.md:216-217`) — эталон
	//   `hotkeyDispatchAction` (input.c:4355/4358) зовёт `UI_DispatchTargetContextMenu(2, 2)` / `(2, 3)` — тот же вход, что
	//   функции 202/203 окна действий (заявка обмена обычными / личными вещами выбранной цели).
	case 57:
	case 58:
		if (UAsdaCharaInfoSubsystem* CI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr)
		{
			const bool bOk = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, Cmd == 57 ? 2 : 3));
			UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] %s → функция %d (заявка обмена): %s"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd),
				Cmd == 57 ? 202 : 203, bOk ? TEXT("отправлена") : TEXT("не отправлена"));
		}
		return;
	case 55:   // ACTION SIT DOWN UP — сесть/встать (клип sitdown уже есть).
		// ★ R5-fix8 (14.11.2026): новое намерение игрока — отложенный очередью клик хода больше не исполняется (разбор — у ключа).
		AsdaSkillCastLock::SupersedeQueuedMove(TEXT("клавиша «сесть/встать»"), 2);
		// ★ 27.09.2026 (pkt4024_facing.md §6): эталон `hotkeyDispatchAction` case 55 (input.c:4352) зовёт
		//   `UI_DispatchTargetContextMenu(2, 0)` — тот же вход, что кнопка окна действий (гейт «верхом» 2040 в чат,
		//   затем `charToggleSitStand`). Ключ `asda.Char.SitToggleEtalon 0` — прежняя ветка ниже.
		if (IsSitToggleEtalon())
		{
			UAsdaCharaInfoSubsystem* CI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
			if (CI) { CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, 0)); }
			return;
		}
		if (Ch && Ch->AnimState != EAsdaPlayerState::Dead)
		{
			Ch->SetAnimState(Ch->AnimState == EAsdaPlayerState::Sit ? EAsdaPlayerState::Idle : EAsdaPlayerState::Sit);
		}
		return;
	// ★★★ 25.10.2026 [ОРИГИНАЛ] 56 «ACTION GET ITEM» / 59 «ACTION ATTACK» / 60 «ACTION TARGET NEAR_ENEMY» (баг игроков: клавиши молчали
	//   ни по умолчанию, ни после переназначения — команд не было в этом switch, `OnGameCommand` без подписчиков).
	//   Эталон: 56 и 59 — обёртка `input_HotkeyDispatchWithContextMenu@0xE3DAF0` (input.c:4471-4484): `if (a4 == 56) { if (g_GameState == 1)
	//   UI_DispatchTargetContextMenu(player, 2, 1); } else if (a4 == 59) UI_DispatchTargetContextMenu(player, 2, 4);` — у порта это
	//   `UAsdaCharaInfoSubsystem::PlayEmotion` (общий гейт «верхом» 2040, затем 2/1 `itemPickupGroundItem`, 2/4
	//   `Input_HandleTargetInteractClick`). 60 — `hotkeyDispatchAction` case 60 → `targetPick_updateSelection@0xE3E990`.
	//   `g_GameState == 1` — мы в мире: у порта `PollKeys` без своего персонажа не идёт. Ключ отката — `asda.Keys.TargetActions 0`.
	case 56:
	case 59:
		// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`AsdaSkillCastLock`, просьба владельца): клавиша атаки цели во время своего навыка — после навыка.
		if (AsdaSkillCastLock::GateTargetCommand(this, Cmd)) { return; }
		// ★ R5-fix8 (14.11.2026): новое намерение игрока — отложенный очередью клик хода больше не исполняется (разбор — у ключа).
		AsdaSkillCastLock::SupersedeQueuedMove(Cmd == 56 ? TEXT("клавиша «подобрать»") : TEXT("клавиша «атака»"), 2);
		if (AsdaTargetHotkeys::IsEnabled())
		{
			if (UAsdaCharaInfoSubsystem* CI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr)
			{
				const bool bOk = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(2, Cmd == 56 ? 1 : 4));
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] %s → функция 2/%d: %s"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd), Cmd == 56 ? 1 : 4,
					bOk ? TEXT("выполнено") : TEXT("отказ/молча"));
				ASDA_CTRACE(EAsdaCT::Input, TEXT("клавиша cmd%d «%s» → функция 2/%d (%s); цель %s"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd),
					Cmd == 56 ? 1 : 4, bOk ? TEXT("выполнено") : TEXT("отказ"), *AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()));
			}
			return;
		}
		OnGameCommand.Broadcast(Cmd);   // прежнее поведение (ключ 0)
		return;
	case 60:
		// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`AsdaSkillCastLock`): «ближайший враг» во время своего навыка цель не меняет — после навыка
		//   (у эталона `targetPick_updateSelection@0xE3E990` гейта нет).
		if (AsdaSkillCastLock::GateTargetCommand(this, Cmd)) { return; }
		if (AsdaTargetHotkeys::IsEnabled())
		{
			const bool bOk = AsdaTargetHotkeys::SelectNearestEnemy(this, TEXT("клавиша 60"));
			ASDA_CTRACE(EAsdaCT::Target, TEXT("клавиша cmd60 «ACTION TARGET NEAR_ENEMY» → ближайший враг: %s; цель %s"),
				bOk ? TEXT("выбран") : TEXT("нет в радиусе"), *AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()));
			return;
		}
		OnGameCommand.Broadcast(Cmd);   // прежнее поведение (ключ 0)
		return;
	// ★★★ 25.10.2026 [ОРИГИНАЛ] 48–50 «PARTY INVITE/LEAVE/EXILE», 61 «ACTION VIEW OTHER INFO», 63 «ACTION TARGET SHARE» — эталон
	//   `hotkeyDispatchAction@0xE3CD90` (input.c:4339-4347, 4362, 4368): `UI_DispatchTargetContextMenu(player, 1, 0/1/2)`, `(2, 5)`,
	//   `(2, 6)` — у порта `UAsdaCharaInfoSubsystem::PlayEmotion` (общий гейт «верхом» 2040, далее отряд / окно 4200 / общая цель).
	//   62 «ACTION SELETE MYSELF» — `input_HandleTargetSelectClick@0xE3E090`, 64 «ACTION TARGET SHARE NEXT» — case 64 самого диспетча
	//   (без гейта «верхом»): `UAsdaPartySubsystem`. Прежде все семь падали в `default:` без подписчиков. Ключ — `asda.Keys.PartyActions 0`.
	case 48: case 49: case 50: case 61: case 63:
		if (AsdaTargetHotkeys::IsPartyActionsEnabled())
		{
			if (UAsdaCharaInfoSubsystem* CI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr)
			{
				const int32 Group = (Cmd <= 50) ? 1 : 2;
				const int32 Idx = (Cmd <= 50) ? (Cmd - 48) : (Cmd == 61 ? 5 : 6);
				const bool bOk = CI->PlayEmotion(UAsdaCharaInfoSubsystem::MakeEmotionCode(Group, Idx));
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] %s → функция %d/%d: %s"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd), Group, Idx,
					bOk ? TEXT("выполнено") : TEXT("отказ/молча"));
			}
			return;
		}
		OnGameCommand.Broadcast(Cmd);   // прежнее поведение (ключ 0)
		return;
	case 62: case 64:
		// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`AsdaSkillCastLock`): выбор себя/члена отряда и общей цели во время своего навыка — после навыка.
		if (AsdaSkillCastLock::GateTargetCommand(this, Cmd)) { return; }
		if (AsdaTargetHotkeys::IsPartyActionsEnabled())
		{
			if (UAsdaPartySubsystem* Party = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPartySubsystem>() : nullptr)
			{
				const bool bOk = (Cmd == 62) ? Party->SelectSelfOrNextMember(TEXT("клавиша 62")) : Party->ShareTargetNext(TEXT("клавиша 64"));
				UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] %s → %s"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd), bOk ? TEXT("выполнено") : TEXT("ничего"));
			}
			return;
		}
		OnGameCommand.Broadcast(Cmd);   // прежнее поведение (ключ 0)
		return;
	default:
		// Цель-подсистема (окно/хотбар/скилл/чат/соц/эмоция) в порту ещё не реализована — транслируем команду.
		//   Будущая система подпишется на OnGameCommand и отреагирует; управление уже полностью распознано.
		UE_LOG(LogAsdaPC, Log, TEXT("[cmd%d] %s → OnGameCommand (цель ещё не портирована)"), Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd));
#if ASDA_COMBAT_TRACE
		// Боевые команды раскладки (ACTION ATTACK, ACTION TARGET …) контроллер сам не исполняет — фиксируем, куда ушло нажатие.
		if (UAsdaKeybindConfig::GetCmdName(Cmd).Contains(TEXT("ATTACK")) || UAsdaKeybindConfig::GetCmdName(Cmd).Contains(TEXT("TARGET")))
		{
			ASDA_CTRACE(EAsdaCT::Input, TEXT("клавиша cmd%d «%s» — в контроллере не исполняется, передана в OnGameCommand (цель %s)"),
				Cmd, *UAsdaKeybindConfig::GetCmdName(Cmd), *AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()));
		}
#endif
		OnGameCommand.Broadcast(Cmd);
		return;
	}
}
