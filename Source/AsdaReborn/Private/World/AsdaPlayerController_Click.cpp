// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerController_Click.cpp — ★ R2 10.11.2026 (разрез AsdaPlayerController.cpp, дословный перенос): левый клик: NPC/моб/земля, OnLeftClick, GroundClickMove.
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
// ★ Журнал боя: имя вида попадания клика/наведения.
static const TCHAR* AsdaCtPickName(AAsdaPlayerController::EAsdaPickKind K)
{
	switch (K)
	{
	case AAsdaPlayerController::EAsdaPickKind::Ground: return TEXT("земля/геометрия");
	case AAsdaPlayerController::EAsdaPickKind::Mob:    return TEXT("моб");
	case AAsdaPlayerController::EAsdaPickKind::Player: return TEXT("игрок");
	case AAsdaPlayerController::EAsdaPickKind::Npc:    return TEXT("NPC");
	case AAsdaPlayerController::EAsdaPickKind::Loot:   return TEXT("лут");
	default:                                           return TEXT("ничего (луч мимо)");
	}
}
#endif // ASDA_COMBAT_TRACE

// ★ Ревизия В4 ВАЖН-1 (рыбалка, 09.10.2026): бит 2, взведённый 4006 ОДИНОЧНОГО обработчика этого же нажатия, не глушит
//   двойной клик. Эталон: второе нажатие двойного — WM_LBUTTONDBLCLK ВМЕСТО WM_LBUTTONDOWN (`MainWndProc` g:23838-23847), бит 1
//   не ставится, `Input_WorldClickHandler` второй раз не исполняется и второго 4006 нет; `world_OnLeftClickPick` (0x9675B6) и
//   `Picking_TestRayPickWorldTarget` (0x992D72, g:30224) видят бит 2 только от 4006 ПЕРВОГО нажатия. UE шлёт второе нажатие как
//   `IE_Pressed` + `IE_DoubleClick` в одном кадре (PlayerInput.cpp:461-465), и `IE_Pressed` успевал отправить свой 4006.
// ★ В45fix2 (ревизия В4/В5, МЕЛК-2, 09.10.2026) — что здесь ОТСТУПЛЕНИЕ, а что эталон:
//   ⚠ (МЕЛК-1 ревизии six_items, 09.10.2026) Этот разбор — ТОЛЬКО при `asda.Input.DblClickLegacySingle 1`: с 09.10.2026 по
//   умолчанию второго одиночного нет (п. 4 владельца, `OnLeftPressedInput` / `IsLeftPressPartOfDouble`), лишнего 4006 второго
//   нажатия нет, и описанное ниже как «устранить нельзя» устранено (ветки «земля»/«ничего»/«лут» у двойного отсечены).
//   [ПОРТ, при ключе 1] ЛИШНИЙ 4006 ВТОРОГО НАЖАТИЯ. Одиночное событие второго нажатия (`IE_Pressed`) исполняет `Input_WorldClickHandler`
//   ещё раз и на проходимой земле шлёт второй 4006: сервер видит 4006 → 4007 → 4006 → 109 (между последним 4006 и 109 ответа
//   4007 нет), у эталона — 4006 → 4007 → 109. Это давнее общее отступление двойного клика порта (обе привязки ЛКМ ведут в
//   `OnLeftClick`, см. разбор в `SetupInputComponent`), и ключ ниже его НЕ устраняет. Устранить 1:1 здесь нельзя: у эталона
//   WM_LBUTTONDBLCLK идёт только в `world_OnLeftClickPick` (g:12566-12700: ветки моба 1, NPC 3 и зоны 5, хода по земле НЕТ),
//   а `OnLeftClick(bDouble)` порта ведёт полный разбор (земля/лут/моб/NPC) — подавление `IE_Pressed` второго нажатия
//   перестраивает двойной клик всего проекта (бой, лут, NPC по решению владельца 21.08); записано в PORT_PENDING
//   («Рыбалка, ревизия В4/В5», п.2).
//   Ключ `asda.Fish.ZoneDblClickOwnMove` касается только ПОСЛЕДСТВИЯ этого 4006: 1 — двойной того же нажатия не глохнет на
//   бите 2 своего лишнего 4006 и доходит до типа 5 (итог = эталон: 109 по зоне); 0 — прежний порт (двойной глохнет, зона на
//   проходимом берегу двойным кликом не выбирается).
static ASDA_DEV_CVAR(int32, CVarAsdaFishDblOwnMove,
	TEXT("asda.Fish.ZoneDblClickOwnMove"), 1,
	TEXT("Действует ТОЛЬКО при asda.Input.DblClickLegacySingle 1 (по умолчанию с 09.10.2026 второго одиночного разбора и его ")
	TEXT("лишнего 4006 нет — как у оригинала). При ключе 1: 1 = двойной клик не глохнет на «запросе хода в пути», если этот ")
	TEXT("запрос отправило одиночное событие ТОГО ЖЕ нажатия (итог как у эталона — 109 по зоне; [ПОРТ] сам лишний 4006 второго ")
	TEXT("нажатия в этом режиме остаётся); 0 = прежний порт (зона на проходимом берегу двойным кликом не выбиралась)."),
	ECVF_Default);

// ★★ Решение владельца 09.10.2026 (п. 4): ЛИШНИЙ 4006 ВТОРОГО НАЖАТИЯ ДВОЙНОГО КЛИКА УБРАН ВО ВСЁМ ПРОЕКТЕ.
//   Эталон: второе нажатие двойного — WM_LBUTTONDBLCLK ВМЕСТО WM_LBUTTONDOWN (`MainWndProc` g:23838-23847): бит 1 не ставится,
//   `Input_WorldClickHandler@0x968890` (ход по земле, подбор, бой, NPC) второй раз НЕ исполняется; работает только
//   `world_OnLeftClickPick@0x966980` (g:12566-12700) — ветки моба (1), NPC (3) и зоны рыбалки (5); хода по земле и подбора в
//   ней НЕТ. Порт: `OnLeftPressedInput` пропускает одиночный разбор, если у ЛКМ в этом кадре есть IE_DoubleClick, а
//   `OnLeftClick(bDouble = true)` не исполняет ветки «земля» и «лут». Итог: сервер видит 4006 → 4007 → (109 / ничего), как у
//   оригинала. Ключ ниже — откат: 1 = прежний порт (одиночный разбор второго нажатия + полный разбор двойного; лишний 4006).
static ASDA_DEV_CVAR(int32, CVarAsdaDblClickLegacySingle,
	TEXT("asda.Input.DblClickLegacySingle"), 0,
	TEXT("0 = как эталон: второе нажатие двойного клика ЛКМ одиночный разбор не исполняет, двойной не ходит по земле и не ")
	TEXT("подбирает лут (лишнего 4006 нет); 1 = прежний порт (второе нажатие = одиночный + двойной, лишний 4006)."),
	ECVF_Default);

// ★★ КЛИК ПО ГРАФИКЕ ПОРТАЛА → ХОД В ЗОНУ ПОРТАЛА (04.11.2026, баг владельца «клик по кольцу/свечению не ведёт к порталу»).
//   [ИСПРАВЛЕНИЕ] У эталона клик по кольцу проходит лучом на землю ЗА кольцом (`mapObjectsCollideRayBvh`, см. выше); за кольцом
//   часто домик-портал, обрыв или край карты — клетка непроходима, и эталон отвечает «Недоступная область» (1013): игроку
//   приходится кликать в землю под кольцом; а где логическая земля .TER выше видимой (Fireway → Inferion), луч по земле
//   останавливается ПЕРЕД кольцом и ведёт не к порталу. Здесь: если луч мыши до дальней из точек «земля .TER» / «видимое
//   попадание по мешам» прошёл над проходимой клеткой зоны портала текущей карты (серверный прямоугольник `kPortalGraph`,
//   высота над землёй не больше столба зоны AsdaPortalGfx::ColumnHeight — 900 или верх модели портала), а точка хода вне этой зоны —
//   ход идёт в клетку зоны (сервер на ней шлёт 5055). Клики, чей луч над зоной не проходит, не меняются (почти все). Клик в
//   землю сразу за зоной тоже ведёт в зону — туда путь и так лежит через неё. Ключ `asda.Move.ClickPortalZone` (1).
static TAutoConsoleVariable<int32> CVarAsdaClickPortalZone(
	TEXT("asda.Move.ClickPortalZone"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Клик по графике портала (луч прошёл над зоной портала, а земля за ним непроходима) ведёт в зону портала. "
	     "0 — как эталон: «Недоступная область»."),
	ECVF_Default);

// Высота «столба» зоны над землёй — `AsdaPortalGfx::ColumnHeight`: 900 (кольцо + свечение) или верх самой высокой модели набора
//   портала у зоны (ворота, арки, домики — до ≈ 2900).
// ★★ КЛИК ПО МОДЕЛИ ПОРТАЛА → ХОД В ЗОНУ (04.11.2026, баг владельца «клик по модели портала не ведёт к порталу»; самотест
//   `asda.SelfTest.PortalClicks` на всех 19 картах). [ИСПРАВЛЕНИЕ]
//   Луч по земле (эталон) за домиком/воротами/аркой портала упирается в непроходимый край карты или стену (1013) либо уводит мимо
//   зоны; проход луча над зоной (`AsdaClickRayPortalZone`) ловит только кольца у земли — перекладина арки и крыша домика выше.
//   Здесь: луч мыши попал в меш набора портала (`AsdaPortalGfx::kEntries`, из `.mapraw`), точка попадания не дальше
//   kPortalGfxReach от серверной зоны текущей карты — ход в проходимую клетку этой зоны, ближайшую к персонажу. Ключ
//   `asda.Move.ClickPortalGfx` (1); 0 — прежнее (луч по земле и проход над зоной).
static TAutoConsoleVariable<int32> CVarAsdaClickPortalGfx(
	TEXT("asda.Move.ClickPortalGfx"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Клик по модели портала (домик, ворота, арка, кольцо — меши AsdaPortalGfx) ведёт в зону этого портала. "
	     "0 — прежнее: точка хода лучом по земле."),
	ECVF_Default);

static constexpr double kPortalGfxReach = 800.0;   // попадание дальше этого от зоны (ед., 8 клеток) — не «клик по порталу»
static constexpr double kPortalEdgeReach = 300.0;  // непроходимая точка клика не дальше этого от зоны (ед., 3 клетки) — ход в зону

// Зона портала текущей карты для клика по мешу набора портала: true и клетка зоны (проходимая, ближайшая к персонажу).
static bool AsdaClickPortalGfxZone(const UAsdaWalkGrid* Grid, int32 MapId, const UPrimitiveComponent* Comp, const FVector& HitUe,
	const FVector2D& SelfGame, FIntPoint& OutTile, FIntPoint& OutZoneMin, FIntPoint& OutZoneMax)
{
	const UStaticMeshComponent* Smc = Cast<UStaticMeshComponent>(Comp);
	const UStaticMesh* Sm = Smc ? Smc->GetStaticMesh() : nullptr;
	if (!Grid || !Sm || MapId < 0 || !AsdaPortalGfx::IsPortalMesh(MapId, Sm->GetName())) { return false; }
	return AsdaNearestPortalZoneTile(Grid, MapId, AsdaCoord::UeToGame2D(HitUe), kPortalGfxReach, SelfGame, OutTile, OutZoneMin, OutZoneMax);
}

static bool AsdaClickRayPortalZone(const APlayerController* PC, const UAsdaWalkGrid* Grid, int32 MapId, const FVector2D& ScreenPos,
	double MaxLen, FVector2D& OutGame)
{
	if (!PC || !Grid || Grid->Width <= 0 || MapId < 0) { return false; }
	FVector OriginUe, DirUe;
	if (!PC->DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, OriginUe, DirUe)) { return false; }
	const FVector O = AsdaCoord::UeToGame3D(OriginUe);
	const FVector D = AsdaCoord::UeToGame3D(DirUe).GetSafeNormal();
	for (double T = 0.0; T <= MaxLen; T += 25.0)
	{
		const FVector P = O + D * T;
		const FIntPoint Tile = AsdaCoord::GameToTile(FVector2D(P.X, P.Z));
		for (const AsdaMapRegistry::FPortalEdge& E : AsdaMapRegistry::kPortalGraph)
		{
			// Столб — зона и клетка вокруг неё (свечение портала стоит в зоне или у её края).
			if (E.From != MapId || Tile.X < E.X0 - 1 || Tile.X > E.X1 + 1 || Tile.Y < E.Z0 - 1 || Tile.Y > E.Z1 + 1) { continue; }
			float Gy = 0.f;
			if (!Grid->GetGroundY((float)P.X, (float)P.Z, Gy) || P.Y - Gy > AsdaPortalGfx::ColumnHeight(MapId, E.X0, E.Z0, E.X1, E.Z1)) { continue; }
			// Клетка хода — проходимая клетка зоны, ближайшая к клетке луча.
			double BestD = TNumericLimits<double>::Max();
			bool bAny = false;
			for (int32 Z = E.Z0; Z <= E.Z1; ++Z)
			{
				for (int32 X = E.X0; X <= E.X1; ++X)
				{
					if (!Grid->IsTileWalkable(X, Z)) { continue; }
					const double Dd = FMath::Square(double(X - Tile.X)) + FMath::Square(double(Z - Tile.Y));
					if (Dd < BestD) { BestD = Dd; OutGame = AsdaCoord::TileCenterGame(X, Z); bAny = true; }
				}
			}
			if (bAny) { return true; }
		}
	}
	return false;
}

// ★ ЛЕВЫЙ КЛИК — полная диспетчеризация по эталону (targeting_system_re.md §3):
//   моб → выбор целью + стоп(4008) + атака(4026); игрок → выбор + запрос HP/MP цели (6538);
//   NPC → выбор + (диалог локально, опкода нет); земля → движение (4006, троттл 150 мс).
//   Клик по пустому месту цель НЕ снимает (эталон) — снимают ESC/смерть/деспавн.
// ★★★ ЕДИНАЯ ТОЧКА ПРАВИЛА «когда клик по NPC открывает окно» (см. шапку в заголовке).
//   Эталон открыл бы окно ЛЮБЫМ кликом; отступление владельца оставляет одиночному клику только
//   выделение цели. Ключ отката `asda.Npc.OpenOnSingleClick 1` возвращает эталонное поведение.
bool AAsdaPlayerController::ShouldOpenNpcTalkOnClick(bool bDouble) const
{
	return bDouble || CVarAsdaNpcOpenOnSingleClick.GetValueOnGameThread() != 0;
}

// ★ Ревизия В4 ВАЖН-1 (рыбалка, 09.10.2026): одиночное событие ЛКМ. Если ИМЕННО оно взвело бит 2 («запрос хода в пути») своим
//   4006 — запомнить кадр: `IE_DoubleClick` того же нажатия придёт в этом же кадре (см. гейт бита 2 в `OnLeftClick`).
// ★ 09.10.2026 (п. 4 владельца): второе нажатие двойного? `UPlayerInput::ProcessInputStack` до вызова делегатов переносит события
//   кадра в `EventCounts` (PlayerInput.cpp:1335-1336); IE_DoubleClick (из WM_LBUTTONDBLCLK Slate или из двух IE_Pressed ближе
//   `DoubleClickTime`, PlayerInput.cpp:446-450 / 461-465) кладёт в кадр и IE_Pressed, и IE_DoubleClick. Нажатий не больше,
//   чем двойных, — значит отдельного «первого» нажатия в этом кадре нет.
bool AAsdaPlayerController::IsLeftPressPartOfDouble() const
{
	const FKeyState* KS = PlayerInput ? PlayerInput->GetKeyState(EKeys::LeftMouseButton) : nullptr;
	if (!KS) { return false; }
	const int32 NDbl = KS->EventCounts[IE_DoubleClick].Num();
	return NDbl > 0 && KS->EventCounts[IE_Pressed].Num() <= NDbl;
}

void AAsdaPlayerController::OnLeftPressedInput()
{
	if (CVarAsdaDblClickLegacySingle.GetValueOnGameThread() == 0 && IsLeftPressPartOfDouble())
	{
		UE_LOG(LogAsdaPC, Verbose, TEXT("[клик] второе нажатие двойного — одиночный разбор пропущен (WM_LBUTTONDBLCLK эталона, без 4006)"));
		return;
	}
	OnLeftClickSingle();
}

void AAsdaPlayerController::OnLeftClickSingle()
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bFlightBefore = Net && Net->IsMoveRequestInFlight();
	OnLeftClick(/*bDouble*/false);
	if (Net && !bFlightBefore && Net->IsMoveRequestInFlight()) { SinglePressMoveFrame = GFrameCounter; }
}

// ★ 15.10.2026: «второй клик» по уже выбранному мобу = боевой режим (тело прежней ветки `OnLeftClick`, case Mob).
void AAsdaPlayerController::EngageMobFromClick(AActor* Mob)
{
	if (!Mob) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = GetChar();
	// ★★★ 15.10.2026 (ритм атаки, дефект порта Д-2 `wave_N16_attack_tolerance.md` §5): ПОВТОРНЫЙ КЛИК ПО ТЕКУЩЕЙ ЦЕЛИ
	//   ПОСРЕДИ БОЯ НЕ НАЧИНАЕТ НОВУЮ СЕРИЮ. Раньше он снимал `bCombatStarted`, и `AttackTick` слал 4026 заново: сервер
	//   (`sub_45FCD0`) обнуляет шаг и прошлый удар и принимает 4026 всегда, а следующий 4028 уходил через остаток прохода
	//   клипа — раньше длительности (~¼ случаев даже у мечей). Эталон: `Player_UpdateAutoActionState@0xA249A0` case 5
	//   (entity_char.c:24031-24034) — состояние уже 5 и цель та же → только `52121 = 100`, ничего не перезапускается.
	//   Откат вместе со всеми часами: `asda.Combat.ServerRhythm 0`.
	//   ★ Ревизия НИЗК-3: ровно как эталон — только пока идёт замах (состояние 5 = цикл замаха жив). В погоне и после
	//   навыка (у эталона состояния 22/24) клик даёт новую 4026, как у оригинала (entity_char.c:24035-24081).
	if (UAsdaAttackRhythmSubsystem::IsEnabled() && bAutoAttack && bCombatStarted && TargetActor.Get() == Mob
		&& Ch && Ch->IsAttackLoopAlive())
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[бой] повторный клик по цели посреди боя — серия идёт, новой 4026 нет (эталон: case 5, та же цель)"));
		ASDA_CTRACE(EAsdaCT::Input, TEXT("повторный клик по цели %s посреди серии — цикл замаха жив, новой 4026 нет (эталон case 5)"),
			*AsdaCtActorDesc(GetGameInstance(), Mob));
		return;
	}
	ASDA_CTRACE(EAsdaCT::Input, TEXT("второй клик по выбранной цели %s — намерение атаки (дистанция %.0f юн)"),
		*AsdaCtActorDesc(GetGameInstance(), Mob), Ch ? FVector::Dist2D(Ch->GetActorLocation(), Mob->GetActorLocation()) : -1.0);
#if ASDA_COMBAT_TRACE
	if (!(Net && Net->bInGameConnected && Ch))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("бой не начат: сеть=%s, в мире=%s, персонаж=%s"), Net ? TEXT("есть") : TEXT("нет"),
			(Net && Net->bInGameConnected) ? TEXT("да") : TEXT("нет"), Ch ? TEXT("есть") : TEXT("нет"));
	}
	else if (!Cast<AAsdaMobActor>(Mob))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("бой не начат: цель '%s' — не моб"), *Mob->GetName());
	}
#endif
	// ★ Второй клик = БОЕВОЙ РЕЖИМ. ЭТАЛОН (CGame_HandleGroundClickAttack@0x996480): клик НЕ шлёт
	//   4026 — только Combat_SendNormalAttack = 4006 с tgtRenderId («иди и бей», сервер ведёт).
	//   4026 уходит ПО ПРИБЫТИИ в радиус (вход в FSM=5, Player_UpdateAutoActionState) — это делает
	//   AttackTick. Прежний немедленный 4026 издалека заставлял сервер бить, пока мы ещё бежим.
	if (Net && Net->bInGameConnected && Ch)
	{
		if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(Mob))
		{
			ClearPendingNpcTalk(TEXT("начат бой"));   // ★ D5
			// ★★★ GM-«скрыться»: боевой режим не взводим вовсе — иначе `AttackTick` каждый кадр
			//   заходил бы и выходил, а персонаж вставал бы в боевую стойку без боя.
			//   Цель при этом ОСТАЁТСЯ выбранной (выбор сервер под скрытием не гейтит).
			if (AsdaGmHide::BlockCombatAction(GetGameInstance()))
			{
				UE_LOG(LogAsdaPC, Log, TEXT("[GM-hide] бой по цели не начат: оператор скрыт (asda.Gm.HideAttackGate)"));
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("бой по rid=%d не начат: GM-«скрыться» (asda.Gm.HideAttackGate), цель остаётся выбранной"), M->NetRenderId);
				return;
			}
			// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущий не атакует — МОЛЧА, цель остаётся выбранной. Ключ `asda.Swim.ActionGate`.
			if (AsdaSwimActionLocked(GetChar()))
			{
				UE_LOG(LogAsdaPC, Log, TEXT("[плавание] бой по цели не начат: персонаж плывёт"));
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("бой по rid=%d не начат: персонаж плывёт (asda.Swim.ActionGate), цель остаётся выбранной"), M->NetRenderId);
				return;
			}
			if (bPvpAttack) { AsdaStopPvpAttackForNewAttack(this, TEXT("переход к бою с мобом")); }   // ★ 01.11: без 400 — сразу новая атака   // ★ 17.10.2026 (Ф4): PvP-серия кончается
			// ★ Ревизия 25.10 (фантомный урон, В-1): повторный клик посреди своей серии начнёт новую (4026) — прежняя брошена.
			if (bCombatStarted)
			{
				if (UAsdaAttackRhythmSubsystem* RhA = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr)
				{
					RhA->NoteSeriesAbandoned(TEXT("повторный клик по цели"));
				}
			}
			bAutoAttack = true;
			bCombatStarted = false;
			NextAttackTickTime = 0.0;   // AttackTick сработает сразу: далеко → подход, рядом → 4008+4026
#if ASDA_COMBAT_TRACE
			// Повторный клик по цели уже открытого цикла (погоня/после навыка — новая 4026) — тот же бой, цикл не перезапускаем.
			if (AsdaCombatTrace::CurrentCycle() > 0 && AsdaCombatTrace::CurrentCycleTarget() == M->NetRenderId)
			{
				ASDA_CTRACE(EAsdaCT::Target, TEXT("повторный клик по цели цикла rid=%d — бой продолжается, серия будет начата заново (новая 4026)"), M->NetRenderId);
			}
			else
			{
				ASDA_CTRACE_BEGIN(M->NetRenderId, *M->MobName, TEXT("второй клик по выбранному мобу — атака"));
			}
#endif
			ASDA_CTRACE(EAsdaCT::Target, TEXT("атака цели %s: автоатака ВКЛ, серия не начата — тик боя: далеко → подход 4006, рядом → 4008+4026; оружие itemId=%d, дистанция %.0f юн"),
				*AsdaCtActorDesc(GetGameInstance(), M), Net->SelfWeaponItemId, FVector::Dist2D(Ch->GetActorLocation(), M->GetActorLocation()));
			// ★ ФИКС «боевая стойка БЕЗ БОЯ» (живой тест 2026-08-01). Раньше здесь стояло
			//   SetCombatStance(true) + CombatStanceUntil = 0 (кулдаун выключен) — стойка включалась
			//   от одного КЛИКА и потом НИКОГДА не гасла, потому что пути выхода (клик по земле,
			//   ручное движение) сбрасывали только bAutoAttack. ЭТАЛОН включает субрежим 20 не по
			//   клику, а вызовом CChar_SetState(char, 0, 0, 20) при ВОЗВРАТЕ В ПОКОЙ ПОСЛЕ БОЕВОГО
			//   ДЕЙСТВИЯ (combat_pvp.c:8059/8341/8408 — удар/получение удара; effects_particles.c —
			//   скиллы). Клик = только выбор цели + намерение атаковать; стойку взводит AttackTick
			//   в момент реального замаха.
		}
	}
}

void AAsdaPlayerController::OnLeftClick(bool bDouble)
{
	// ★ Свободная камера админа: персонаж не управляется, клик по земле/цели игнорируется.
	if (UAsdaAdminSubsystem* Adm = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAdminSubsystem>() : nullptr)
	{
		if (Adm->IsFreeCamActive()) { return; }
	}
	// ★★★ КУРСОР НАД ЛЮБЫМ ОКНОМ ИНТЕРФЕЙСА → мировой клик игнорируем целиком (решение юзера
	//   2026-08-04: «клик в виджете должен работать ТОЛЬКО в виджете, а не сквозь него»).
	//   ПОЧЕМУ ЗДЕСЬ, А НЕ В UMG: ЛКМ приходит сюда напрямую через
	//   `InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, …)`, а виджеты окон — `UImage`;
	//   `SImage` не реализует `OnMouseButtonDown` и возвращает Unhandled, поэтому `Visible` делает
	//   картинку хит-таргетом для наведения, но событие НЕ поглощает и клик всё равно доходит сюда.
	//   ★ ЕДИНЫЙ РЕЕСТР ЗОН (2026-08-04): раньше здесь стоял персональный вызов
	//   UAsdaChatSubsystem::IsCursorOverWindow(). Окон становится много (журнал 2570, основной
	//   чат 2500, вкладки 2590, палитра 2700, меню 2670/80000, HUD) — «каждому своя ветка» это
	//   гарантированные будущие дыры (забыли ветку → клик проходит сквозь окно). Теперь подсистема
	//   сама регистрирует свою зону в UAsdaWindowSubsystem::RegisterUiZone, а здесь — ОДИН вопрос.
	//   ★★★ 2026-08-06: зона теперь заводится АВТОМАТИЧЕСКИ у КАЖДОГО окна реестра (по данным .JUI:
	//   прямоугольники видимых контролов минус `NoSelect="1"`), а гасятся ЛКМ, ПКМ и колесо.
	{
		FName Zone;
		// ★ 05.11.2026: повтор клика, отложенного на время навыка, — курсор уже не там, где был; гейт окон прошёл при нажатии.
		if (!AsdaSkillCastLock::IsReplayingClick() && IsCursorOverUi(Zone))
		{
			UE_LOG(LogTemp, Verbose, TEXT("[клик] курсор над окном «%s» — мировой клик игнорирован"), *Zone.ToString());
#if ASDA_COMBAT_TRACE
			// В бою — событие цикла; вне боя — только частый уровень (клики по окнам не засоряют журнал боя).
			if (AsdaCombatTrace::CurrentCycle() > 0 || bAutoAttack)
			{
				ASDA_CTRACE(EAsdaCT::Input, TEXT("ЛКМ %s: курсор над окном «%s» — мировой клик игнорирован"), bDouble ? TEXT("двойной") : TEXT("одиночный"), *Zone.ToString());
			}
			else
			{
				ASDA_CTRACE_V(EAsdaCT::Input, TEXT("ЛКМ %s: курсор над окном «%s» — мировой клик игнорирован"), bDouble ? TEXT("двойной") : TEXT("одиночный"), *Zone.ToString());
			}
#endif
			return;
		}
	}
	// ★★ ИДЁТ РЫБАЛКА → мировой клик не делает НИЧЕГО (волна В4 рыбалки, 08.10.2026). Эталон: обе точки входа ЛКМ
	//   выходят по `sub_92A968(g_pMap)` = fishState ≠ 0 — `Input_WorldClickHandler@0x968890` (game.c:13266, вызов 0x96890D:
	//   ни клик-мува, ни атаки, ни подбора) и `world_OnLeftClickPick@0x966980` (game.c:12566, 0x966A17: ни выбора цели/
	//   моба/NPC/предмета, ни зоны). Стоит ПЕРЕД гейтом окон ремесла — порядок условий эталона (game.c:13266-13268).
	//   [ПОРТ] (ревизия В4 МЕЛК-6, ключ `asda.Fish.LegacyDeferredClick`): эталон при этом НЕ гасит флаг клика
	//   (`g_WorldInputFlags` снимается ниже гейтов), и клик, сделанный во время рыбалки, исполняется в первом кадре после её
	//   конца; порт по умолчанию (0) клик отбрасывает — как у гейта ремесла ниже; 1 — запоминает и исполняет (`PlayerTick`).
	if (IsFishingNow())
	{
		if (CVarAsdaFishLegacyDeferredClick.GetValueOnGameThread() != 0)
		{
			(bDouble ? bFishDeferredDouble : bFishDeferredSingle) = true;
			UE_LOG(LogAsdaPC, Log, TEXT("[клик] идёт рыбалка — %s клик отложен до её конца (эталон: бит g_WorldInputFlags жив, asda.Fish.LegacyDeferredClick 1)"),
				bDouble ? TEXT("двойной") : TEXT("одиночный"));
			return;
		}
		UE_LOG(LogAsdaPC, Verbose, TEXT("[клик] идёт рыбалка (fishState ≠ 0) — мировой клик игнорирован (game.c:12566/13266)"));
		ASDA_CTRACE(EAsdaCT::Input, TEXT("ЛКМ %s: идёт рыбалка — мировой клик игнорирован"), bDouble ? TEXT("двойной") : TEXT("одиночный"));
		return;
	}
	// ★★ ОТКРЫТО ОКНО РЕМЕСЛА → мировой клик не делает НИЧЕГО (волна В2 ремесла, 25.09.2026). Эталон
	//   `Input_WorldClickHandler@0x968890` (input.c:144-157) и `world_OnLeftClickPick` (ui_core.c:961) выходят
	//   ПЕРВОЙ строкой, если видно окно списка 76000 (`tileInteract_isControlVisible(g_pItemMainWnd)`) или
	//   окно изготовления 77500 (`isBoundControlVisible(g_pItemQtyMakeDlg)`) — ни выбора цели, ни хода.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GetGameInstance()))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[клик] открыто окно ремесла 76000/77500 — мировой клик игнорирован (input.c:144-157)"));
		ASDA_CTRACE(EAsdaCT::Input, TEXT("ЛКМ %s: открыто окно ремесла 76000/77500 — мировой клик игнорирован"), bDouble ? TEXT("двойной") : TEXT("одиночный"));
		return;
	}
	// ★★★ 05.11.2026 СВОЙ НАВЫК ЗАНЯТ (25 заявка / 24 клип / 14, 13, 27 каст) — разбор и ключи в `World/AsdaSkillCastLock.h`.
	//   [ОРИГИНАЛ] `Input_WorldClickHandler@0x968890` (input.c:150): в 14/13/27 клик игнорируется целиком; земля в 24 — ход после
	//   клипа (отложенное действие 16, game_main.c:6693), в 25 — без хода. [ИСПРАВЛЕНИЕ] (владелец) клик по цели — после навыка.
	if (AsdaSkillCastLock::GateWorldClick(this, bDouble))
	{
		ASDA_CTRACE(EAsdaCT::Input, TEXT("ЛКМ %s: свой навык занят (состояние %d) — клик отложен до конца навыка или отброшен"),
			bDouble ? TEXT("двойной") : TEXT("одиночный"), AsdaSkillCastLock::SelfSkillPhase(GetGameInstance()));
		return;
	}
	// ★★ МЁРТВ → мировой клик не делает НИЧЕГО (эталон input.c:170: isMovementActionState@0x95DBE0
	//   уводит обработку ЛКМ в ветку сидя/встать и до Picking_TestRayPickWorldTarget дело не доходит —
	//   ни выбора цели, ни клик-мува, ни атаки, ни подбора). Камера и UI при этом работают.
	if (IsSelfDead())
	{
		ASDA_CTRACE(EAsdaCT::Input, TEXT("ЛКМ %s: свой персонаж мёртв — мировой клик игнорирован"), bDouble ? TEXT("двойной") : TEXT("одиночный"));
		return;
	}
	// ★ 27.09.2026 (pkt4024_facing.md §6): садится/сидит/встаёт (7/8/9) → мировой клик = `charToggleSitStand`
	//   (input.c:170-174: `isMovementActionState` → ветка сидения, ни выбора цели, ни хода). Гейт эталона
	//   `!sub_929DC4(g_bSomeFeatureEnabled)` (витрина личной лавки открыта) — ★ 30.09.2026 (волна Л5 торговли): общая точка
	//   `UAsdaTradeStateSubsystem::IsShopDlgOpen`; при витрине клик сидя НИЧЕГО не делает (ни «встать», ни выбора цели).
	if (IsSitToggleEtalon())
	{
		const int32 SitSt = KbEtalonActionState();
		if (SitSt == 7 || SitSt == 8 || SitSt == 9)
		{
			ASDA_CTRACE(EAsdaCT::Input, TEXT("ЛКМ: персонаж сидит/садится/встаёт (состояние %d) — клик = «сесть/встать», без выбора цели и хода"), SitSt);
			if (!UAsdaTradeStateSubsystem::IsShopDlgOpen(GetGameInstance())) { ToggleSitStand(TEXT("клик по миру")); }
			else { UE_LOG(LogAsdaPC, Log, TEXT("[клик] сидя при открытой витрине лавки 23000 — «встать» не выполняется (input.c:172)")); }
			return;
		}
	}
	// ★★ 28.10.2026 ОТКРЫТА ДОСКА ОБЪЯВЛЕНИЙ 75000 → мировой клик ТОЛЬКО закрывает её (эталон `Input_WorldClickHandler`,
	//   input.c:177-181: `if (sub_937640(g_pQuestCraftWnd)) { questUiReset(g_pQuestCraftWnd); return; }` — сразу после ветки
	//   «сидит/мёртв» и ДО выбора цели/хода: ни нового хода, ни цели, ни подбора). Клик по самому окну сюда не доходит (зона окна).
	if (UAsdaBoardQuestSubsystem::IsBoardOpen(GetGameInstance()))
	{
		if (UAsdaBoardQuestSubsystem* Board = GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>()) { Board->Close(); }
		UE_LOG(LogAsdaPC, Log, TEXT("[клик] открыта доска объявлений 75000 — клик по миру только закрыл её (input.c:177)"));
		return;
	}
	// ★★★ 27.09.2026 (эталонная схема хода): ЗАПРОС ХОДА В ПУТИ → мировой клик не делает НИЧЕГО. Эталон проверяет
	//   бит 2 в трёх точках этого пути — `world_OnLeftClickPick` (0x9675B6: `and eax,2 / jne → выход`),
	//   `Picking_TestRayPickWorldTarget` (0x992D72: бит 2 → «ничего не выбрано») и `CGame_HandleGroundClickAttack`
	//   (0x9964F2: → return 0). То есть до ответа 4007 на прошлый 4006 ни выбора цели, ни нового хода, ни подбора.
	//   Окно — время пути пакета туда-обратно (сторож снимает бит через 10 с, если ответ потерян).
	//   ★ Ревизия В4 ВАЖН-1 (рыбалка, 09.10.2026): исключение — ДВОЙНОЙ клик, чей бит 2 взвёл 4006 одиночного обработчика ЭТОГО
	//   ЖЕ нажатия (`SinglePressMoveFrame == GFrameCounter`): у эталона второе нажатие — WM_LBUTTONDBLCLK без нового 4006, и
	//   `world_OnLeftClickPick` видит бит 2 только от первого нажатия. Такой двойной проходит ТОЛЬКО к проверке типа 5 (зона
	//   рыбалки); если зоны под курсором нет — выходит, как прежде (прочие ветки двойного уже исполнило одиночное событие).
	//   Ключ `asda.Fish.ZoneDblClickOwnMove` (0 — прежний порт). [ПОРТ] сам 4006 одиночного события второго нажатия — лишний
	//   (у эталона его нет); ключ его не убирает, а лишь обходит его бит 2 — разбор у объявления ключа (В45fix2, МЕЛК-2).
	//   ⚠ (МЕЛК-1 ревизии six_items) Это исключение срабатывает только при `asda.Input.DblClickLegacySingle 1`: по умолчанию
	//   одиночного разбора второго нажатия нет и его 4006 нет; `SinglePressMoveFrame` этим кадром взводится лишь в крайнем
	//   случае «оба нажатия в одном кадре» (одиночный первого нажатия), тогда исключение по-прежнему ведёт двойной к типу 5.
	bool bDoubleZoneOnly = false;
	if (IsEtalonSpeedSync())
	{
		if (UAsdaNetSubsystem* GateNet = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			if (GateNet->IsMoveRequestInFlight())
			{
				const bool bOwnPress = bDouble && SinglePressMoveFrame == GFrameCounter
					&& CVarAsdaFishDblOwnMove.GetValueOnGameThread() != 0;
				if (!bOwnPress)
				{
					// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Input.ClickQueue`): клик не теряется — ждёт ответа 4007 и исполняется (вытеснение).
					//   Только одиночное нажатие: событие двойного — второе нажатие того же места (у эталона его ветки земли нет).
					if (!bDouble && AsdaSkillCastLock::QueueBlockedClick(this, bDouble)) { return; }
					UE_LOG(LogAsdaPC, Verbose, TEXT("[клик] запрос хода ещё без ответа (бит 2) — мировой клик игнорирован (эталон)"));
					ASDA_CTRACE(EAsdaCT::Input, TEXT("ЛКМ %s: прошлый 4006 ещё без ответа 4007 (бит 2) — мировой клик игнорирован (эталон)"),
						bDouble ? TEXT("двойной") : TEXT("одиночный"));
					return;
				}
				bDoubleZoneOnly = true;
				UE_LOG(LogAsdaPC, Verbose, TEXT("[клик] двойной: бит 2 взведён 4006 этого же нажатия — проверяется только тип 5 (WM_LBUTTONDBLCLK без 4006)"));
			}
		}
	}
	// ★ ЛКМ отменяет автобег и клавиатурное удержание (эталон: GetAsyncKeyState(1) гасит все move-флаги).
	bAutoRunActive = false; AutoRunHoldStart = 0.0;
	const FAsdaPick P = PickForClick();
	ASDA_CTRACE(EAsdaCT::Input, TEXT("ЛКМ %s: попало «%s» %s, точка UE (%.0f,%.0f,%.0f), до персонажа %.0f юн; текущая цель %s"),
		bDouble ? TEXT("двойной") : TEXT("одиночный"), AsdaCtPickName(P.Kind), *AsdaCtActorDesc(GetGameInstance(), P.Actor.Get()),
		P.Point.X, P.Point.Y, P.Point.Z, GetChar() ? FVector::Dist2D(GetChar()->GetActorLocation(), P.Point) : -1.0,
		*AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()));
	// ★★ ДВОЙНОЙ КЛИК ПО ЗОНЕ РЫБАЛКИ (волна В4, 08.10.2026). Эталон `world_OnLeftClickPick@0x966980` (game.c:12578-12586):
	//   `Picking_TestRayPickWorldTarget(…, 1)` → тип 5 → `sub_9202A1(g_pMap, зона)` (= `PickSpot`, только при fishState 0)
	//   → `g_nInputControlState == 5` → `inputHotkeyAction109` (= `RequestStart109`). Ничего больше эта ветка не делает
	//   (ни снятия следования, ни выбора цели). Одиночный клик (`Input_WorldClickHandler`, `a6 = 0`) зоны не видит.
	if (bDouble)
	{
		const int32 ZoneSpot = PickFishZoneForDoubleClick(P);
		if (ZoneSpot >= 0)
		{
			if (UAsdaFishingSubsystem* Fish = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFishingSubsystem>() : nullptr)
			{
				Fish->PickSpot(ZoneSpot);
				Fish->RequestStart109();
				UE_LOG(LogAsdaPC, Log, TEXT("[рыбалка] двойной клик по зоне %d → 109 (world_OnLeftClickPick, тип 5)"), ZoneSpot);
			}
			return;
		}
	}
	if (bDoubleZoneOnly) { return; }   // ★ ВАЖН-1: зоны нет — двойной при бите 2 своего нажатия дальше не идёт (как прежде)
	// ★ 09.10.2026 (п. 4 владельца): двойной = `world_OnLeftClickPick` эталона — веток «земля» и «лут» (ход, подбор = 4006) в нём
	//   нет. Их исполнило первое нажатие; второе нажатие пакет движения больше не шлёт. Откат — `asda.Input.DblClickLegacySingle 1`.
	if (bDouble && CVarAsdaDblClickLegacySingle.GetValueOnGameThread() == 0
		&& (P.Kind == EAsdaPickKind::Ground || P.Kind == EAsdaPickKind::None || P.Kind == EAsdaPickKind::Loot))
	{
		UE_LOG(LogAsdaPC, Verbose, TEXT("[клик] двойной по %s — без хода/подбора (у world_OnLeftClickPick этих веток нет)"),
			P.Kind == EAsdaPickKind::Loot ? TEXT("луту") : TEXT("земле"));
		ASDA_CTRACE(EAsdaCT::Input, TEXT("двойной по %s — без хода/подбора и без выхода из боя (исполнило первое нажатие)"),
			P.Kind == EAsdaPickKind::Loot ? TEXT("луту") : TEXT("земле"));
		// ★ 05.11.2026 (разбор бага «второй клик двойного отменяется»): оставлено по решению владельца 09.10 (п. 4). Двойной ОС/Slate
		//   признаёт только при втором нажатии в пределах ~4 px и `GetDoubleClickTime` — то есть в ТУ ЖЕ точку, куда персонаж уже идёт по
		//   первому нажатию; второй 4006 в ту же точку лишний. Теряемые нажатия (бит 2, троттл 150 мс) ловит очередь `asda.Input.ClickQueue`.
		return;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = GetChar();

	// ★ Любое РУЧНОЕ действие игрока снимает следование — эталон зовёт
	//   `followTargetCancelReset(&g_bMapTransferActive)` из четырёх точек ввода
	//   (input.c:190/304/389/572). ЛКМ по миру — одна из них.
	//   ⚠ ЗАПИСЬ ЦЕЛИ ДЛЯ КОНТЕКСТНОГО МЕНЮ ЗДЕСЬ БОЛЬШЕ НЕ ДЕЛАЕТСЯ: эталон пишет её на
	//   НАЖАТИИ ПРАВОЙ кнопки, а не левой (см. `OnRightPressed`).
	if (UAsdaPlayerMenuSubsystem* PMenu = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr)
	{
		if (PMenu->IsFollowing()) { PMenu->CancelFollow(TEXT("клик по миру")); }
	}

	switch (P.Kind)
	{
	case EAsdaPickKind::Mob:
	{
		AActor* Mob = P.Actor.Get();
		if (!Mob) { break; }
		// ★ ПЕРВЫЙ клик по сущности = ТОЛЬКО ВЫБОР; действие — со ВТОРОГО клика по уже выбранной цели.
		const bool bAlreadyTarget = (TargetActor.Get() == Mob);
		SetTarget(Mob);
		if (!bAlreadyTarget)
		{
			ASDA_CTRACE(EAsdaCT::Target, TEXT("первый клик по мобу %s — только выбор цели (атака — вторым кликом)"), *AsdaCtActorDesc(GetGameInstance(), Mob));
			break;                                         // первый клик — просто выделили
		}
		// ★ 15.10.2026: тело «второго клика» вынесено в `EngageMobFromClick` (его зовёт и самотест ритма атаки).
		EngageMobFromClick(Mob);
		break;
	}
	case EAsdaPickKind::Player:
	{
		AActor* Pl = P.Actor.Get();
		if (!Pl) { break; }
		const bool bAlreadyTargetPl = (TargetActor.Get() == Pl);   // ★ 17.10.2026 (Ф4): второй клик по выбранному игроку
		SetTarget(Pl);
		ASDA_CTRACE(EAsdaCT::Target, TEXT("%s клик по игроку %s — %s"), bAlreadyTargetPl ? TEXT("второй") : TEXT("первый"),
			*AsdaCtActorDesc(GetGameInstance(), Pl), bAlreadyTargetPl ? TEXT("проверка PvP-атаки 4201") : TEXT("только выбор цели"));
		// ★★★ 17.10.2026 (Ф4) [ОРИГИНАЛ]: второй клик по выбранному персонажу при включённом PvP-режиме — атака 4201
		//   (`Input_HandleTargetInteractClick@0xC55CC0`, ветка `g_nInputControlState == 0`, input.c:1547–1680). Режим выключен —
		//   эталон ничего не делает (`[160] == 1` — единственный вход ветки).
		if (bAlreadyTargetPl) { EngagePlayerFromClick(Pl); }
		// ★★★ ЗАПРОС HP/MP ЦЕЛИ (6538) ОТСЮДА УБРАН 2026-08-29 (дефект порта, два в одном).
		//   1) Он уходил ТОЛЬКО по ВТОРОМУ клику (`if (!bAlreadyTarget) break;`), а эталон шлёт его
		//      в момент САМОГО ВЫБОРА: `hud_setTarget@0xE7B470` (`ui_core.c:75922-75932`) сравнивает
		//      charId новой цели с кэшем и, если он изменился, шлёт 6538 сразу.
		//   2) Клик — не единственный путь выбора цели (клавиши отряда/поиск ближайшего), а запрос
		//      висел только на клике.
		//   Теперь единственная точка отправки — `UAsdaTargetFrameSubsystem::FillForPlayer`, наш
		//   дословный аналог `hud_setTarget`: он видит ЛЮБУЮ смену цели и держит кэш прежней
		//   (charId/userNum), который сервер требует в теле пакета. Атака 4201 — только в PvP-режиме.
		break;
	}
	case EAsdaPickKind::Npc:
	{
		AAsdaNpcActor* Npc = Cast<AAsdaNpcActor>(P.Actor.Get());
		if (!Npc) { break; }
		// ★★★ ОДИНОЧНЫЙ КЛИК = ТОЛЬКО ВЫДЕЛЕНИЕ, ОКНО — ПО ДВОЙНОМУ (решение владельца 2026-08-21).
		//   ЧТО В ЭТАЛОНЕ: обе точки входа ЛКМ (`Input_WorldClickHandler@0x968890` и
		//     `world_OnLeftClickPick@0x966980`) в ветке `g_nInputControlState == 3` ПОБАЙТОВО
		//     одинаковы ⇒ у оригинала окно открывается уже с ПЕРВОГО клика.
		//   ЧТО У НАС: одиночный только выбирает цель (и зажигает кольцо, §1 отчёта), а окно
		//     разговора и подбегание запускает двойной. Возврат к эталону —
		//     `asda.Npc.OpenOnSingleClick 1` (по умолчанию 0).
		//   ⚠ Условие «первый/второй клик» здесь БОЛЬШЕ НЕ ГОДИТСЯ: движок на двойном нажатии
		//     присылает `IE_Pressed` И `IE_DoubleClick` (PlayerInput.cpp:461-465), поэтому
		//     «повторный одиночный» сработал бы РАНЬШЕ двойного и открывал бы окно всё равно.
		//     Признак `bDouble` — единственный надёжный различитель.
		SetTarget(Npc);
		ASDA_CTRACE(EAsdaCT::Target, TEXT("клик по NPC %s (%s) — выбор цели, окно разговора: %s"), *AsdaCtActorDesc(GetGameInstance(), Npc),
			bDouble ? TEXT("двойной") : TEXT("одиночный"), ShouldOpenNpcTalkOnClick(bDouble) ? TEXT("открываем/подбегаем") : TEXT("нет"));
		if (!ShouldOpenNpcTalkOnClick(bDouble))
		{
			UE_LOG(LogAsdaPC, Verbose, TEXT("[NPC] одиночный клик по %s — только выделение "
				"(окно разговора открывает двойной; эталонное поведение: asda.Npc.OpenOnSingleClick 1)"),
				*Npc->GetName());
			break;
		}
		StartNpcTalk(Npc);
		break;
	}
	case EAsdaPickKind::Loot:
	{
		// ★ Лут подбирается С ПЕРВОГО клика (эталон netHandleTargetObjectClick — без «выбора целью»).
		if (const AAsdaLootActor* L = Cast<AAsdaLootActor>(P.Actor.Get()))
		{
			ASDA_CTRACE(EAsdaCT::Input, TEXT("клик по луту на клетке (%d,%d) — подбор (автоатака=%s)"), L->Tile.X, L->Tile.Y, bAutoAttack ? TEXT("да") : TEXT("нет"));
			StartLootPickup(L->Tile);
			// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `Input_WorldClickHandler` (input.c:293) после `netHandleTargetObjectClick` — байты 76/80.
			AsdaTutorial::Mark(this, EAsdaTutorialFlag::TargetObject);
			AsdaTutorial::Mark(this, EAsdaTutorialFlag::ItemPickup);
		}
		break;
	}
	case EAsdaPickKind::Ground:
	default:
	{
		// ★ 22.10.2026 [ОРИГИНАЛ] (ревизия стана, мелочь 1): гейт обездвиживания `Input_WorldClickHandler` (input.c:545) стоит ДО выхода
		//   из боя (тот идёт после гейтов, input.c:560+) — клик по земле под станом автоатаку не снимает.
		if (IsMoveInputLockedByState())
		{
			AsdaPlankCannotMove(GetGameInstance());
			UE_LOG(LogAsdaPC, Log, TEXT("[клик] свой персонаж обездвижен состоянием — клик по земле без хода и без выхода из боя, плашка 1019 (input.c:545)"));
			break;
		}
		// Движение по земле (троттл 150 мс — эталон). Клик по земле выводит из боевого режима
		//   (эталон: FSM уходит из state 5 в движение), цель при этом НЕ снимается.
		//   Отложенный подбор лута тоже отменяется (новое намерение движения замещает state 103).
		bLootPending = false;
		ASDA_CTRACE(EAsdaCT::Exit, TEXT("клик по земле: выход из боя (автоатака была=%s, серия=%s, PvP=%s) — замах остановлен, стойка снята, цель %s остаётся выбранной"),
			bAutoAttack ? TEXT("да") : TEXT("нет"), bCombatStarted ? TEXT("да") : TEXT("нет"), bPvpAttack ? TEXT("да") : TEXT("нет"),
			*AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()));
		ASDA_CTRACE_END(TEXT("клик по земле"));
		AsdaLeaveAttackAction(this, TEXT("клик по земле"));   // ★ 01.11.2026: 5 → 0 — 4024/400 до снятия флагов и до хода (эталон `CCharacter_SetAction` case 0)
		bAutoAttack = false; bCombatStarted = false; bInCombatStance = false;
		// ★★★ ДЕФЕКТ ПОРТА, живой отчёт владельца 2026-08-24: «начал атаку и вышел из неё — персонаж
		//   остался в боевой стойке и делает вид, что атакует». Здесь гасились только ФЛАГИ, а сам
		//   цикл клипа атаки (`AttackLoopClipIdx` с бесконечным удержанием) продолжал крутиться —
		//   клавиатурный выход из боя (ниже, `bMovingNow && bAutoAttack`) снимал его правильно, а
		//   клик по земле — нет. Заодно снимаем ожидание ответа сервера: без этого первый замах
		//   после возвращения в бой глох в гейте `bWaitingCombatAck` до истечения таймаута.
		bWaitingCombatAck = false;
		if (AAsdaPlayerCharacter* MoveCh = GetChar()) { MoveCh->StopAttackAnim(); }
		// ★ ДВИЖЕНИЕ СНИМАЕТ БОЕВУЮ СТОЙКУ. Эталон: тик управления игроком возвращает персонажа в покой
		//   вызовами CChar_SetState(char, 0, 0, **0**) (entity_char.c:18284…18933) — субрежим 0 = МИРНЫЙ
		//   покой (idle02 ×7 → фиджет idle03), в отличие от боевого субрежима 20 (idle01). Без этого
		//   стойка, включённая боем, оставалась висеть после ухода с поля боя.
		ExitCombatStance();
		// ★ 23.10.2026 [ИСПРАВЛЕНИЕ] (регресс локального старта, `asda.Fish.ZoneSelfTest` R2/R4a/W2): одиночное нажатие по точке,
		//   где двойной клик выбрал бы зону рыбалки (тип 5), идёт БЕЗ локального старта — персонаж ждёт гранта, как оригинал
		//   (у него ход начинается только по 4007, позже второго нажатия). С локальным стартом персонаж и камера трогались сразу,
		//   повторный пик второго нажатия (`PickFishZoneForDoubleClick`, та же экранная точка) смотрел уже сдвинутым лучом
		//   и зону не находил — двойной клик по воде вёл не к 109, а в «двойной по земле». Ключ `asda.Move.LocalStart` прежний.
		bSuppressLocalStartOnce = IsLocalStart() && PickFishZoneForDoubleClick(P, nullptr, true) >= 0;
		if (bSuppressLocalStartOnce)
		{
			UE_LOG(LogAsdaPC, Verbose, TEXT("[клик] точка в зоне рыбалки (тип 5 двойного клика) — ход без локального старта, ждём гранта"));
		}
		// ★ 04.11.2026 [ОРИГИНАЛ] (`asda.Move.ClickRayTerrain`): точка хода — луч по земле .TER сквозь объекты карты (кольца и
		//   домики порталов, дома, эффекты), как `mapObjectsCollideRayBvh` эталона. Нет земли под лучом — прежняя точка.
		FVector MovePoint = P.Point;
		{
			float CX = 0.f, CY = 0.f;
			FVector TerrUe;
			const AAsdaPlayerCharacter* TerrCh = GetChar();
			const bool bScreen = TerrCh && GetClickScreenPos(CX, CY);
			bool bTerrain = false;   // точка хода найдена лучом по земле .TER
			if (CVarAsdaClickRayTerrain.GetValueOnGameThread() != 0 && bScreen
				&& AsdaClickRayTerrainPoint(this, TerrCh->WalkGrid, FVector2D(CX, CY), TerrUe))
			{
				if (FVector::Dist2D(TerrUe, P.Point) > 50.f)
				{
					ASDA_CTRACE(EAsdaCT::Move, TEXT("клик: точка хода по земле .TER (%.0f,%.0f) вместо попадания по «%s» (%.0f,%.0f) — эталон mapObjectsCollideRayBvh"),
						TerrUe.X, TerrUe.Y, *AsdaCtActorDesc(GetGameInstance(), P.Actor.Get()), P.Point.X, P.Point.Y);
				}
				MovePoint = TerrUe;
				bTerrain = true;
			}
			// ★ 04.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Move.ClickPortalGfx`): луч попал в модель портала — ход в клетку его зоны (разбор у
			//   `AsdaClickPortalGfxZone`); уже в зоне — точка не меняется.
			bool bGfxDone = false;   // точка хода задана кликом по модели портала
			if (bScreen && TerrCh->WalkGrid && CVarAsdaClickPortalGfx.GetValueOnGameThread() != 0)
			{
				const UAsdaLoadManagerSubsystem* LMg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
				FIntPoint ZTile, ZMin, ZMax;
				if (LMg && AsdaClickPortalGfxZone(TerrCh->WalkGrid, LMg->CurrentMapId, P.Component.Get(), P.Point, TerrCh->GetGamePosXZ(), ZTile, ZMin, ZMax))
				{
					const FIntPoint MoveTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(MovePoint));
					// В зоне И проходима — точка не меняется; непроходимая клетка зоны (край, стена ворот) — ход в проходимую клетку зоны.
					const bool bInZone = MoveTile.X >= ZMin.X && MoveTile.X <= ZMax.X && MoveTile.Y >= ZMin.Y && MoveTile.Y <= ZMax.Y
						&& TerrCh->WalkGrid->IsTileWalkable(MoveTile.X, MoveTile.Y);
					if (!bInZone)
					{
						const FVector2D ZoneUe = AsdaCoord::GameToUe2D(AsdaCoord::TileCenterGame(ZTile));
						UE_LOG(LogAsdaPC, Log, TEXT("[клик] по модели портала «%s»: ход в клетку зоны (%d,%d) вместо (%d,%d) (asda.Move.ClickPortalGfx)"),
							*GetNameSafe(Cast<UStaticMeshComponent>(P.Component.Get()) ? Cast<UStaticMeshComponent>(P.Component.Get())->GetStaticMesh() : nullptr),
							ZTile.X, ZTile.Y, MoveTile.X, MoveTile.Y);
						MovePoint = FVector(ZoneUe.X, ZoneUe.Y, MovePoint.Z);
					}
					bGfxDone = true;
				}
			}
			// ★ 04.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Move.ClickPortalZone`): точка непроходима (не закрытая ClosedBits — та и так ведёт к
			//   ближайшей проходимой) — если луч прошёл над зоной портала, ход в зону (разбор у `AsdaClickRayPortalZone`).
			if (!bGfxDone && bScreen && TerrCh->WalkGrid && CVarAsdaClickPortalZone.GetValueOnGameThread() != 0)
			{
				const FVector2D MoveGame = AsdaCoord::UeToGame2D(MovePoint);
				const FIntPoint MoveTile = AsdaCoord::GameToTile(MoveGame);
				const bool bWalk = TerrCh->WalkGrid->IsWorldWalkable(MoveGame.X, MoveGame.Y)
					|| (AsdaWalkClosedTilesOn() && TerrCh->WalkGrid->IsTileClosed(MoveTile.X, MoveTile.Y));
				const UAsdaLoadManagerSubsystem* LM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
				FVector2D ZoneGame = FVector2D::ZeroVector;   // ★ R3-c чистка (C4701): начальное значение; читается только при bFound
				FVector CamLoc; FRotator CamRot; GetPlayerViewPoint(CamLoc, CamRot);
				// Дальность поиска — до ДАЛЬНЕЙ из двух точек: земли .TER и видимого попадания по мешам (кольцо). Логическая земля
				//   .TER местами выше видимой (портал Fireway → Inferion): луч по земле «упирается» перед кольцом, а игрок кликал по
				//   кольцу за ней — зона портала лежит между ними.
				const double MeshLen = (P.Kind != EAsdaPickKind::None) ? FVector::Dist(CamLoc, P.Point) : 0.0;
				const double MaxLen = bTerrain ? FMath::Max(FVector::Dist(CamLoc, MovePoint), MeshLen) + 50.0 : 80000.0;
				bool bFound = LM && AsdaClickRayPortalZone(this, TerrCh->WalkGrid, LM->CurrentMapId, FVector2D(CX, CY), MaxLen, ZoneGame);
				// Зона на луче, а точка хода — вне неё (непроходима или по эту сторону кольца): ход в клетку зоны.
				bool bMoveInZone = false;
				if (bFound && LM)
				{
					for (const AsdaMapRegistry::FPortalEdge& E : AsdaMapRegistry::kPortalGraph)
					{
						bMoveInZone |= E.From == LM->CurrentMapId && MoveTile.X >= E.X0 && MoveTile.X <= E.X1 && MoveTile.Y >= E.Z0 && MoveTile.Y <= E.Z1
							&& TerrCh->WalkGrid->IsTileWalkable(MoveTile.X, MoveTile.Y);   // непроходимая клетка зоны — не «уже в зоне»
					}
				}
				// Луч над зону не прошёл, а точка хода непроходима и не дальше kPortalEdgeReach от зоны (свечение портала у обрыва или края
				//   карты, земля у ворот) — ход в ближайшую к персонажу клетку этой зоны.
				if (!bFound && !bWalk && LM)
				{
					FIntPoint ZTile, ZMin, ZMax;
					if (AsdaNearestPortalZoneTile(TerrCh->WalkGrid, LM->CurrentMapId, MoveGame, kPortalEdgeReach, TerrCh->GetGamePosXZ(), ZTile, ZMin, ZMax))
					{
						ZoneGame = AsdaCoord::TileCenterGame(ZTile);
						bFound = true;
					}
				}
				if (bFound && !bMoveInZone)
				{
					UE_LOG(LogAsdaPC, Log, TEXT("[клик] по графике портала: точка хода (%d,%d)%s, луч прошёл над зоной портала — ход в клетку зоны (%.0f,%.0f) (asda.Move.ClickPortalZone)"),
						MoveTile.X, MoveTile.Y, bWalk ? TEXT(" перед кольцом") : TEXT(" непроходима"), ZoneGame.X, ZoneGame.Y);
					ASDA_CTRACE(EAsdaCT::Move, TEXT("клик по графике портала: ход в зону портала (%.0f,%.0f) вместо непроходимой (%d,%d)"), ZoneGame.X, ZoneGame.Y, MoveTile.X, MoveTile.Y);
					const FVector2D ZoneUe = AsdaCoord::GameToUe2D(ZoneGame);
					MovePoint = FVector(ZoneUe.X, ZoneUe.Y, MovePoint.Z);
				}
			}
		}
		GroundClickMove(MovePoint);   // ★ 27.09.2026: тело вынесено (самотест хода клавишами проходит тот же путь)
		bSuppressLocalStartOnce = false;   // только этот клик (4006 уходит синхронно внутри GroundClickMove)
		break;
	}
	}
}

// Ход по клику в точку земли (хвост ветки `EAsdaPickKind::Ground` в `OnLeftClick`, после выхода из боя). Вынесен
//   27.09.2026 (ревизия WASD, МЕЛК-3): самотест `asda.Move.SelfTestKeyboard` зовёт ровно этот путь — пикинга курсором
//   в прогоне без экрана нет. `PointUe` — точка попадания луча под курсором.
void AAsdaPlayerController::GroundClickMove(const FVector& PointUe)
{
	const FVector P_Point = PointUe;
	// ★ 29.09.2026 (волна 0 торговли): [ОРИГИНАЛ] ветка клика по земле `Input_WorldClickHandler@0x968890` (input.c:493-498,
	//   game.c:13612) и `world_OnLeftClickPick@0x966980` (ui_core.c:1180-1184) ДО троттла 150 мс: окно обмена открыто
	//   (`g_bPartyInviteFlag` = `CExchange+0x2C`) → 9568 «При открытом окне обмена двигаться нельзя» в чат (−11514, 10 с),
	//   хода нет. Общая точка `UAsdaTradeStateSubsystem` (пока окна 2008 нет — false).
	//   [ПОРТ] Остальные гейты той же ветки (110611, 12917, 99003, 13010, 13100) в порту клика не перенесены (реестр отложенного,
	//   «Ход кликом»); 107012/107011 и 10305 — ниже (волна Л5), 1019 — ниже (22.10.2026).
	// ★ 22.10.2026 [ОРИГИНАЛ] `Input_WorldClickHandler@0x968890` (input.c:545): `listHasAnyOfSlotTypes` (0,1,2,3,14,9,4,8) → плашка 1019
	//   «персонаж не может двигаться в таком состоянии» (`CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)`), хода нет — всегда
	//   (эталонный гейт ключом исправления не выключается); под ключом `asda.Move.StunHardStop` ещё и состояния остановки 11/12/13
	//   [ИСПРАВЛЕНИЕ]. Ветка клика `OnLeftClick` проверяет то же раньше выхода из боя; здесь — для прямых вызовов (самотест).
	if (IsMoveInputLockedByState())
	{
		AsdaPlankCannotMove(GetGameInstance());
		UE_LOG(LogAsdaPC, Log, TEXT("[клик] свой персонаж обездвижен состоянием — клик по земле без хода, плашка 1019 (input.c:545)"));
		ASDA_CTRACE(EAsdaCT::Move, TEXT("ход по клику не начат: обездвижен (плашка 1019)"));
		return;
	}
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()))
	{
		AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, AsdaNotice::LocRaw(GetGameInstance(), 9568),
			AsdaNoticeColor::Gold, 10000.f);   // −11514 = 0xFFFFD306
		UE_LOG(LogAsdaPC, Log, TEXT("[клик] окно обмена 2008 открыто — клик по земле без хода, нотис 9568 (input.c:495)"));
		ASDA_CTRACE(EAsdaCT::Move, TEXT("ход по клику не начат: открыто окно обмена 2008 (нотис 9568)"));
		return;
	}
	// ★ 30.09.2026 (волна Л5 торговли): [ОРИГИНАЛ] та же ветка, input.c:533-544 (после 110611/12917/99003/13010/13100, которых в порту
	//   клика нет): подготовка лавки 22000 → 107012 «При открытом окне создания лавки двигаться невозможно.», витрина 23000 → 107011
	//   «При открытом окне покупок двигаться невозможно.» — строка чата (−11514, 10 с), хода нет (троттл 150 мс не трогается).
	if (UAsdaTradeStateSubsystem::IsSellWindowOpen(GetGameInstance()) || UAsdaTradeStateSubsystem::IsShopDlgOpen(GetGameInstance()))
	{
		const bool bPrep = UAsdaTradeStateSubsystem::IsSellWindowOpen(GetGameInstance());
		UAsdaTradeStateSubsystem::ChatStoreGate(GetGameInstance(), bPrep ? 107012 : 107011, TEXT("Input_WorldClickHandler"));
		UE_LOG(LogAsdaPC, Log, TEXT("[клик] открыта личная лавка (%s) — клик по земле без хода (input.c:%d)"),
			bPrep ? TEXT("подготовка 22000") : TEXT("витрина 23000"), bPrep ? 533 : 539);
		ASDA_CTRACE(EAsdaCT::Move, TEXT("ход по клику не начат: открыта личная лавка (%s)"), bPrep ? TEXT("подготовка 22000") : TEXT("витрина 23000"));
		return;
	}
	// ★ Ревизия Л4/Л5 (СРЕДН-1): [ОРИГИНАЛ] та же ветка, input.c:551-556 (после 1019 `listHasAnyOfSlotTypes` — плашка, в порту клика
	//   нет, реестр «Ход кликом»): свиток возврата склада активен → 10305 в чат (−11514, 10 с), хода нет.
	if (UAsdaWarehouseSubsystem* Ware = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr)
	{
		if (Ware->IsRecallActive())
		{
			Ware->NoticeRecallBlocked(10305);
			UE_LOG(LogAsdaPC, Log, TEXT("[клик] свиток возврата склада активен — клик по земле без хода, нотис 10305 (input.c:553)"));
			ASDA_CTRACE(EAsdaCT::Move, TEXT("ход по клику не начат: активен свиток возврата склада (нотис 10305)"));
			return;
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMoveSendTime < 0.150)
	{
		// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Input.ClickQueue`): троттл эталона сохранён, но точка не теряется — ход по окончании троттла.
		if (AsdaSkillCastLock::QueueGroundMove(this, P_Point)) { return; }
		ASDA_CTRACE(EAsdaCT::Move, TEXT("ход по клику не начат: троттл 150 мс (прошло %.0f мс)"), (Now - LastMoveSendTime) * 1000.0);
		return;
	}
	LastMoveSendTime = Now;
	// ★ 27.09.2026 (`asda.Move.KeyboardEtalon 1`): эталон `Input_WorldClickHandler@0x968890` (game.c:13678-13684) после
	//   троттла 150 мс ведёт ход ТОЛЬКО при `!g_bMoveFwd && !g_bMoveBack && !g_bMoveLeft && !g_bMoveRight` и
	//   `g_bWorldTickActive` (клавиатура неактивна). Клик идёт раньше тика клавиш, поэтому клик с зажатой клавишей хода
	//   не даёт; сама ЛКМ в тике гасит флаги, и клавиатура уходит в ветку отпускания (стоп).
	if (IsKeyboardEtalon() && (bKbFlag[0] || bKbFlag[1] || bKbFlag[2] || bKbFlag[3] || !bKbWorldTickActive))
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[клик] клавиатура хода активна — клик по земле без хода (эталон game.c:13684)"));
		ASDA_CTRACE(EAsdaCT::Move, TEXT("ход по клику не начат: активна клавиатура хода (эталон game.c:13684)"));
		return;
	}
	const FVector2D DstGame = AsdaCoord::UeToGame2D(P_Point);   // ★ Аудит №3 (B1): UE→game
	// ★★★ «НЕДОСТУПНАЯ ОБЛАСТЬ» — КЛИЕНТСКАЯ проверка, а не серверная (диагноз круга 7).
	//   ЭТАЛОН input.c:697/823: `if (GroundTarget_PickOnMesh_thunk(клик) && bMoveAllowed) { идти }
	//   else if (bMoveAllowed) { loc 1013 в журнал + в 221 + CSoundFlag_PlaySound2D("error.wav") }`.
	//   Пик по проходимой геометрии = наш WalkGrid: клик по непроходимой клетке движения НЕ даёт.
	//   Почему раньше сообщения не было НИКОГДА: порт держал только серверную ветку 4007 result=2,
	//   а сервер её не шлёт (в 312 живых пакетах 4007 только result 0/1/5) — канал был мёртв.
	//   Маршрут (решение юзера): категория MoveRefused → СИСТЕМНЫЙ ЧАТ + error.wav.
	if (const AAsdaPlayerCharacter* WalkCh = GetChar())
	{
		// ★ 04.11.2026 [ИСПРАВЛЕНИЕ]: тайл, закрытый ТОЛЬКО `ClosedBits` (по .TER он проходим — у эталона клик туда даёт ход),
		//   не отказ 1013, а ход к ближайшей проходимой клетке (поиск пути сдвигает цель, `FindPathTiles` радиус 12). Так клик
		//   сквозь кольцо Fireway Forest → Flabis на пол домика-портала ведёт в зону портала — у эталона ход шёл в домик через зону.
		const FIntPoint ClickTile = AsdaCoord::GameToTile(DstGame);
		const bool bClosedOnly = WalkCh->WalkGrid && AsdaWalkClosedTilesOn() && WalkCh->WalkGrid->IsTileClosed(ClickTile.X, ClickTile.Y);
		if (bClosedOnly && !WalkCh->WalkGrid->IsWorldWalkable(DstGame.X, DstGame.Y))
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[движение] клик в закрытую клетку (%d,%d) (ClosedBits) — ход к ближайшей проходимой"), ClickTile.X, ClickTile.Y);
			ASDA_CTRACE(EAsdaCT::Move, TEXT("клик в закрытую клетку (%d,%d) — ход к ближайшей проходимой (asda.Walk.ClosedTiles)"), ClickTile.X, ClickTile.Y);
		}
		else if (WalkCh->WalkGrid && !WalkCh->WalkGrid->IsWorldWalkable(DstGame.X, DstGame.Y))
		{
			if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				FAsdaMsgParams MP;
				MP.Category = EAsdaMsgCategory::MoveRefused;
				MP.LocId = 1013;                                   // «Недоступная область»
				// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → «[#1013]».
				Msg->Post(MP);
			}
			UE_LOG(LogAsdaPC, Log, TEXT("[движение] клик в непроходимую клетку (%.0f,%.0f) → нотис 1013"),
				DstGame.X, DstGame.Y);
			ASDA_CTRACE(EAsdaCT::Move, TEXT("ход по клику не начат: непроходимая клетка (%.0f,%.0f) → нотис 1013"), DstGame.X, DstGame.Y);
			return;                                                // движение НЕ начинаем (эталон)
		}
	}
	ASDA_CTRACE(EAsdaCT::Move, TEXT("ход по клику в точку game (%.0f,%.0f) — маршрут"), DstGame.X, DstGame.Y);
	RequestMoveTo(DstGame);
	// ★ 29.10.2026 [ОРИГИНАЛ] (asda.Map.DestMarkClear): клик по земле ставит цель автобега в клетку клика (input.c:678/738) —
	//   метка на мини-карте/карте мира. После `RequestMoveTo`: тот сам метку не трогает, а прежняя (клик по карте) заменяется.
	AsdaDestMarkSetGame(GetGameInstance(), DstGame);
	// Метка «иду сюда» — только по земле (эталон); проходимость проверяет сам павн (WalkGrid).
	SpawnClickMarker(P_Point);
	LastMoveDestUe  = P_Point;
	bMovingToPoint  = true;
}
