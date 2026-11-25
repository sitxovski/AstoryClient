// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerController_Pick.cpp — ★ R2 10.11.2026 (разрез AsdaPlayerController.cpp, дословный перенос): выбор под курсором: классификация, лучи (рельеф/порталы), цель, подсветка, курсор, наведение.
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

// ★★★ КОЛЬЦО ПОД ВЫБРАННОЙ ЦЕЛЬЮ — ВКЛЮЧЕНО ПО УМОЛЧАНИЮ, И ЭТО ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА.
//   ЧТО БЫЛО: в нашей сборке (`AsdaStory.exe`, 13 344 768 б) под выбранной целью не рисуется
//     НИЧЕГО — ни кольца, ни декали, ни `.jeb`, ни звука (доказано двумя методами,
//     `Reports/npc_visual_fix.md` §2.1). Белая заливка модели, которую владелец видел
//     20.08, была НАШЕЙ выдумкой и убрана — `Mode` подсветки модели остаётся `None`.
//   ЧТО ВЗЯТО: пара эффектов клиента-близнеца (`friendlyselect.jeb` — зелёное,
//     `unfriendlyselect.jeb` — красное, узел `SowelFloor`, тип 0x0B «квад в плоскости земли»)
//     из `NEW\WorldOfDreams\Data\Effect\Model\EffectModel.bnz`. В нашем архиве их нет.
//   ПОЧЕМУ: прямое решение владельца 2026-08-21 — он играл в ту сборку и хочет её обратную связь
//     на выбор цели. Разбор и мотивировка — `Reports/npc_target_ring.md`.
//   ОТКАТ К 1:1 НАШЕЙ СБОРКЕ: `asda.Target.Ring 0`.
static ASDA_DEV_CVAR(int32, CVarAsdaTargetRing,
	TEXT("asda.Target.Ring"), 1,
	TEXT("Кольцо под выбранной целью (зелёное — мирные, красное — враждебные). ")
	TEXT("1 = включено (по умолчанию, ОТСТУПЛЕНИЕ от эталона — взято у клиента-близнеца), ")
	TEXT("0 = как в нашей сборке (под целью не рисуется ничего)."),
	ECVF_Default);

// ★ [ПОРТ] 12.10.2026 (жалоба владельца «рыбалка предлагается по двойному клику на СУШЕ у берега»; разбор — шапка
//   `AsdaFishZoneSubsystem.h`, отчёт `owner_1012/fishzone/fishzone_fix.md`). У ЭТАЛОНА это штатно: коробка зоны 4r × 2 м × 4r на
//   высоте плоскости воды, террейн в пикинге не участвует, луч идёт сквозь землю — двойной клик по берегу в ±2r тайлов от спота
//   даёт тип 5 → 109 → (игрок в r тайлов) диалог 81. Ключ 1 — отступление: тип 5 засчитывается, только если точка попадания
//   `PickForClick` лежит на воде (`UAsdaFishZoneSubsystem::IsWaterHit`); промах луча (небо) при ключе 1 тоже не вода.
//   ★ Решение владельца 12.10.2026 (скрин: предложение рыбачить с суши — «косяк»): умолчание 1 — дефект оригинала исправляем
//   (закон fix-original-defects); 0 возвращает поведение оригинала.
static ASDA_DEV_CVAR(int32, CVarAsdaFishZonePickWaterOnly,
	TEXT("asda.Fish.ZonePickWaterOnly"), 1,
	TEXT("0 = как оригинал: двойной клик в коробке зоны (4r x 2 м x 4r на высоте воды) предлагает рыбалку и с суши; ")
	TEXT("1 = [ПОРТ] только если точка клика лежит на воде (меш SM_water либо не выше поверхности воды .WAT)."),
	ECVF_Default);

// Классификация актора под курсором в вид сущности эталона.
static AAsdaPlayerController::EAsdaPickKind ClassifyActor(const AActor* A, const AActor* SelfPawn)
{
	using EK = AAsdaPlayerController::EAsdaPickKind;
	if (!A) { return EK::None; }
	// ★★ КЛИК ПО СЕБЕ ВЫБИРАЕТ СЕБЯ ЦЕЛЬЮ (просьба владельца 2026-08-25). Раньше свой павн
	//   классифицировался как «ничто» и целью не становился вовсе. Это мешало применять на себя
	//   умения, которые накладываются на ДРУЖЕСТВЕННУЮ цель: выбрать себя было нечем.
	//   Атака при этом невозможна по построению: боевой цикл взводится только веткой монстра
	//   (`EAsdaPickKind::Mob`), а ветка игрока умеет лишь выделить и запросить сведения о цели.
	//   Отношение к себе — «игрок», поэтому кольцо и панель показываются дружественными.
	if (A == SelfPawn)                    { return EK::Player; }
	if (A->IsA(AAsdaMobActor::StaticClass()))          { return EK::Mob; }
	if (A->IsA(AAsdaNpcActor::StaticClass()))          { return EK::Npc; }
	if (A->IsA(AAsdaPlayerCharacter::StaticClass()))   { return EK::Player; }
	if (A->IsA(AAsdaLootActor::StaticClass()))         { return EK::Loot; }   // предмет на земле (курсор «рука»)
	return EK::Ground;                                                  // геометрия карты/террейн
}

// ★★★ ЛУЧ ПОД КУРСОРОМ, СКВОЗЬ НЕОТРИСОВАННЫХ ПЕРСОНАЖЕЙ (27.09.2026, ревизия gm_hide_npc_2 СРЕДН-2).
//   [ОРИГИНАЛ] все три пикера персонажей эталона берут в перебор ТОЛЬКО отрисовываемых:
//     • `pickEntityType32ByRayObb@0x993C30` (клик, render_core.c:2878) — `if (*(DWORD*)(j + 28))`;
//     • `Input_PickEntityHover@0x993DD0` (наведение, input.c:937)       — `if (*((DWORD*)i + 7))`;
//     +28 = `RenderCtx_GetEnable(entity + 8)` (`*(this + 5)`, render_core.c:3061/754).
//   Флаг снимает `CharManager_UpdateAll@0x9D56C0` (entity_char.c:8544-8553) у персонажа с битом
//   GM-скрытия (`(sub_93B218(ch) & 1) != 0`), если сам игрок не скрыт. Такого персонажа в переборе
//   НЕТ ВОВСЕ: он не выбирается целью и не заслоняет того, кто стоит за ним; земля под ним тоже
//   берётся (`collide_rayVsMapObjects` персонажей не знает).
//   [ПОРТ] персонаж — актор с коллизией, а `SetActorHiddenInGame` коллизию не трогает (так и задумано,
//   см. `UAsdaWorldSubsystem::SetCharHiddenByGm`), поэтому обычный `GetHitResultUnderCursor` упирался
//   в капсулу невидимки: её можно было выделить кликом, навести курсор (плашка!) и ею закрыть моба.
//   Аналог флага отрисовки у нас — `IsHidden()` актора персонажа: попадание в такой актор
//   отбрасываем и повторяем луч без него. Своя полупрозрачность под hide (`asda.Gm.HideTranslucent 1`)
//   актор НЕ прячет — свой персонаж остаётся кликабельным, как и видимым.
//   Круг ограничен персонажами: скрытые мобы/NPC у порта и так без коллизии (AsdaMobActor.cpp:290,
//   AsdaNpcActor.cpp:667/669), страж души и питомец — без коллизии вовсе; прочие скрытые акторы карты
//   (невидимые коллизии-заглушки) должны по-прежнему ловить клик по земле.
// ★★★ 08.11.2026 [ИСПРАВЛЕНИЕ] СПОРНЫЙ ЩЕЛЧОК ПО NPC — ПО ТЕЛУ (пачка 19 баг 1: щелчок по Бумибуми, NPC 131 в Альпии, выбирал
//   PrePre-v19, NPC 146 — робот на пружине в 200 ед. от неё; и наоборот).
//   ОРИГИНАЛ: `Input_WorldClickHandler` (input.c:1021-1058) берёт NPC с БЛИЖАЙШИМ попаданием луча в короб +189 (класс 0: 100 × h × 100
//     у ног, entity_char.c:58726-58745), тело модели в выборе не участвует. Короб шире тела: где короб ближнего NPC закрывает на
//     экране тело дальнего, щелчок по дальнему достаётся ближнему (у оригинала так же — дефект оригинала).
//   ПОРТ ДО ПРАВКИ: то же правило (первое попадание луча в капсулу клика R 50). Замер `asda.SelfTest.NpcClickPick` (оракул —
//     треугольники скинованных моделей) нашёл, где капсула одного NPC забирает пиксели тела другого (отчёт
//     `Reports/batch19/npc_click/`).
//   ПРАВИЛО: если луч задевает ТЕЛО NPC (коробки костей модели в текущей позе, `AsdaNpcBodyRay::RayBody`) ближе первого препятствия
//     не-NPC (карта, моб, игрок, лут), выбирается NPC с ближайшим телом — даже если ближе стоит капсула клика другого NPC, а
//     попадание вне своей капсулы (руки, широкая шляпа). Ни одно тело не задето — прежнее попадание (ближайшая
//     капсула или земля). Зовётся из общего луча клика и наведения (ниже): действует и на щелчок, и на курсор/плашку.
//     Ключ `asda.Npc.ClickByBody` 1; 0 — как оригинал (ближайшая капсула).
static TAutoConsoleVariable<int32> CVarAsdaNpcClickByBody(
	TEXT("asda.Npc.ClickByBody"), 1,
	TEXT("[FIX] A click/hover ray that touches an NPC body (per-bone boxes of the model in the current pose) before any non-NPC obstacle ")
	TEXT("picks the NPC with the nearest body, even past another NPC's click capsule; no body touched - the previous hit. 0 - as the original: the nearest capsule."),
	ECVF_Default);

static void AsdaClickResolveNpcOverlap(const APlayerController* PC, const FVector2D& ScreenPos, FCollisionQueryParams Q, FHitResult& InOutHit)
{
	if (CVarAsdaNpcClickByBody.GetValueOnGameThread() == 0 || !PC || !PC->GetWorld()) { return; }
	FVector O, D;
	if (!PC->DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, O, D)) { return; }
	// Препятствие луча без NPC: карта, мобы, игроки, лут (их капсулы закрывают NPC за ними, как и раньше).
	TArray<AAsdaNpcActor*, TInlineAllocator<16>> Npcs;
	for (TActorIterator<AAsdaNpcActor> It(PC->GetWorld()); It; ++It)
	{
		AAsdaNpcActor* N = *It;
		if (!N || N->IsHidden() || !N->GetActorEnableCollision() || !N->Mesh) { continue; }
		// Грубый отсев: сфера габаритов меша далеко от луча.
		const FBoxSphereBounds& B = N->Mesh->Bounds;
		const FVector C = B.Origin;
		const double Along = FVector::DotProduct(C - O, D);
		if (Along < 0.0 || FVector::DistSquared(O + D * Along, C) > FMath::Square(B.SphereRadius)) { continue; }
		Npcs.Add(N);
		Q.AddIgnoredActor(N);
	}
	if (Npcs.Num() == 0) { return; }
	double Wall = TNumericLimits<double>::Max();
	FHitResult W;
	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		if (!PC->GetHitResultAtScreenPosition(ScreenPos, ECC_Visibility, Q, W)) { break; }
		const AActor* A = W.GetActor();
		if (A && A->IsA<AAsdaPlayerCharacter>() && A->IsHidden()) { Q.AddIgnoredActor(A); continue; }   // как в основном луче
		if (const AAsdaNpcActor* N = Cast<AAsdaNpcActor>(A)) { Q.AddIgnoredActor(N); continue; }        // NPC вне отсева — не препятствие
		Wall = W.Distance;
		break;
	}
	AAsdaNpcActor* Best = nullptr;
	double BestT = Wall;
	for (AAsdaNpcActor* N : Npcs)
	{
		double T = 0.0;
		if (AsdaNpcBodyRay::RayBody(N->Mesh, O, D, T) && T < BestT) { Best = N; BestT = T; }
	}
	if (!Best || Best == InOutHit.GetActor()) { return; }
	const AActor* Was = InOutHit.GetActor();
	UE_LOG(LogAsdaPC, Verbose, TEXT("[клик] тело NPC %d ближе всех на луче (%.0f) — вместо %s (щелчок по телу)"),
		Best->NpcId, BestT, Was ? *Was->GetName() : TEXT("ничего"));
	FHitResult H;
	H.bBlockingHit = true;
	H.Distance = (float)BestT;
	H.Location = H.ImpactPoint = O + D * BestT;
	H.TraceStart = O;
	H.TraceEnd = O + D * BestT;
	H.ImpactNormal = H.Normal = -D;
	H.HitObjectHandle = FActorInstanceHandle(Best);
	H.Component = Best->PickHitbox.Get();
	InOutHit = H;
}

static bool AsdaTraceSkippingUnrenderedChars(const APlayerController* PC, const FVector2D& ScreenPos, FHitResult& OutHit)
{
	// Параметры как у `GetHitResultUnderCursor(ECC_Visibility, bTraceComplex = true)` движка (своё имя статистики).
	FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaClickableTrace), /*bTraceComplex*/true);
	// Потолок повторов: одна невидимка = один повтор; 8 стоящих в одной точке — с запасом.
	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		if (!PC->GetHitResultAtScreenPosition(ScreenPos, ECC_Visibility, Q, OutHit)) { OutHit = FHitResult(); return false; }
		const AActor* A = OutHit.GetActor();
		const bool bUnrenderedChar = A && A->IsA<AAsdaPlayerCharacter>() && A->IsHidden();
		if (!bUnrenderedChar) { AsdaClickResolveNpcOverlap(PC, ScreenPos, Q, OutHit); return true; }   // ★ 08.11.2026: спорный щелчок по NPC
		Q.AddIgnoredActor(A);   // эталон: такого персонажа в переборе нет — луч идёт дальше
	}
	OutHit = FHitResult();
	return false;
}

bool AAsdaPlayerController::TraceScreenSkippingUnrenderedChars(const FVector2D& ScreenPos, FHitResult& OutHit) const
{
	return AsdaTraceSkippingUnrenderedChars(this, ScreenPos, OutHit);
}

// ★★★ ТОЧКА ХОДА ПО КЛИКУ — ЛУЧ ТОЛЬКО ПО ЗЕМЛЕ .TER (04.11.2026, баг владельца «клик по порталу не ведёт к нему»). [ОРИГИНАЛ]
//   Эталон `Input_WorldClickHandler@0x968890` (input.c:575-577): `pick_BuildRaySegment` → `mapObjectsCollideRayBvh@0x1139910`
//   (world_map.c:14458) → `collision_rayBvhTraverseNearest@0x11394F0` — обход КВАДРОДЕРЕВА .TER (узел 92 б, 4 потомка, лист =
//   два треугольника тайла, `collSweepMeshRecurse2`); ближайшее пересечение — точка хода. Объектов карты (дома, кольца порталов,
//   эффекты) в этом обходе нет: клик по кольцу/свечению/домику-порталу у оригинала проходит СКВОЗЬ них на землю за ними.
//   [ПОРТ] раньше брал первое попадание луча по мешам (кольцо, стена домика, дерево) и шёл в проекцию этой точки — клик по порталу
//   уводил в тайл под стеной домика (карман, ныне закрытый → «Недоступная область») или не туда.
//   Здесь: луч мыши в игровых координатах (высота игры = Z UE) шагами по 25 ед. до первого погружения под землю сетки
//   `UAsdaWalkGrid::GetGroundY` (та же интерполяция, что `terrainInterpHeightAtXY`), затем 16 делений пополам. Ключ
//   `asda.Move.ClickRayTerrain` (1); 0 — прежняя точка попадания по мешам. Вне сетки/в небо — false (вызывающий берёт прежнюю).
TAutoConsoleVariable<int32> CVarAsdaClickRayTerrain(
	TEXT("asda.Move.ClickRayTerrain"), 1,
	TEXT("[ОРИГИНАЛ] Точка хода по клику — пересечение луча с землёй .TER (сквозь дома, кольца порталов, эффекты), как "
	     "mapObjectsCollideRayBvh эталона. 0 — прежнее: проекция первого попадания по мешам."),
	ECVF_Default);

// Наведение: эталон Input_PickEntityHover@0x993DD0 — из 4 списков побеждает БЛИЖАЙШАЯ сущность,
//   но геометрия ближе сущности ОТМЕНЯЕТ наведение (окклюзия collide_rayVsMapObjects).
AAsdaPlayerController::FAsdaPick AAsdaPlayerController::PickHover() const
{
	FAsdaPick R;
	FHitResult Hit;
	// Трейс по всем видимым: ближайшее попадание = либо сущность, либо геометрия (тогда наведения нет).
	//   ★ Скрытые (неотрисованные) персонажи луч не останавливают — `AsdaTraceSkippingUnrenderedChars`.
	float MX = 0.f, MY = 0.f;
	if (!GetMousePosition(MX, MY) || !AsdaTraceSkippingUnrenderedChars(this, FVector2D(MX, MY), Hit))
	{
		return R;
	}
	AActor* A = Hit.GetActor();
	R.Point = Hit.ImpactPoint;
	const EAsdaPickKind K = ClassifyActor(A, GetPawn());
	if (K == EAsdaPickKind::Mob || K == EAsdaPickKind::Npc || K == EAsdaPickKind::Player || K == EAsdaPickKind::Loot)
	{
		R.Kind = K; R.Actor = A;
	}
	else
	{
		R.Kind = EAsdaPickKind::Ground;   // геометрия ближе → наведения на сущность нет (эталон)
	}
	return R;
}

// Клик: эталон Picking_TestRayPickWorldTarget@0x992D60 — приоритет моб→игрок→лут→NPC→геометрия.
//   У мобов есть aim-assist (промах центральным лучом → 4 доп. луча ±10 px) — воспроизводим.
AAsdaPlayerController::FAsdaPick AAsdaPlayerController::PickForClick() const
{
	AAsdaPlayerController* Self = const_cast<AAsdaPlayerController*>(this);
	FAsdaPick Best;
	// ★ 05.11.2026 (`AsdaSkillCastLock`): повтор клика, отложенного на время навыка, — пикинг тот, что был в момент нажатия.
	{
		uint8 RpKind = 0; AActor* RpActor = nullptr; FVector RpPoint = FVector::ZeroVector; UPrimitiveComponent* RpComp = nullptr;
		if (AsdaSkillCastLock::GetReplayPick(RpKind, RpActor, RpPoint, RpComp))
		{
			Best.Kind = (EAsdaPickKind)RpKind; Best.Actor = RpActor; Best.Point = RpPoint; Best.Component = RpComp;
			return Best;
		}
	}
	FHitResult Hit;
	// ★ Скрытые (неотрисованные) персонажи луч не останавливают — `AsdaTraceSkippingUnrenderedChars`
	//   (центральный луч и все 4 луча aim-assist: скрытый перед мобом не должен «закрывать» моба).
	float MX = 0.f, MY = 0.f;
	const bool bHaveMouse = Self->GetClickScreenPos(MX, MY);   // ★ В4 рыбалки: мышь или подмена самотеста
	if (bHaveMouse && AsdaTraceSkippingUnrenderedChars(this, FVector2D(MX, MY), Hit))
	{
		Best.Kind  = ClassifyActor(Hit.GetActor(), GetPawn());
		Best.Actor = Hit.GetActor();
		Best.Point = Hit.ImpactPoint;
		Best.Component = Hit.GetComponent();   // ★ ревизия В4 ВАЖН-2 (рыбалка): тип 4 — по компоненту (экземпляры ISM карты)
	}
	// Aim-assist по мобам: если центральный луч не дал моба — пробуем 4 смещения ±10 px.
	if (Best.Kind != EAsdaPickKind::Mob)
	{
		if (bHaveMouse)
		{
			static const FVector2D kOffsets[4] = { {-10,0}, {10,0}, {0,-10}, {0,10} };
			for (const FVector2D& O : kOffsets)
			{
				FHitResult H2;
				if (AsdaTraceSkippingUnrenderedChars(this, FVector2D(MX + O.X, MY + O.Y), H2))
				{
					if (ClassifyActor(H2.GetActor(), GetPawn()) == EAsdaPickKind::Mob)
					{
						Best.Kind = EAsdaPickKind::Mob; Best.Actor = H2.GetActor(); Best.Point = H2.ImpactPoint;
						Best.Component = H2.GetComponent();
						ASDA_CTRACE(EAsdaCT::Input, TEXT("клик: центральный луч моба не дал — aim-assist смещением (%+.0f,%+.0f) px нашёл %s"),
							O.X, O.Y, *AsdaCtActorDesc(GetGameInstance(), H2.GetActor()));
						break;
					}
				}
			}
		}
	}
	return Best;
}

// ★ Волна В4 рыбалки (08.10.2026): экранная точка клика — мышь либо подмена самотеста.
bool AAsdaPlayerController::GetClickScreenPos(float& OutX, float& OutY) const
{
	// ★ 05.11.2026 (`AsdaSkillCastLock`): повтор отложенного клика после поворота камеры — экранной точки нет (точка — из пикинга).
	if (AsdaSkillCastLock::IsReplayNoScreen()) { return false; }
	if (DebugClickScreen.IsSet())
	{
		OutX = (float)DebugClickScreen->X; OutY = (float)DebugClickScreen->Y;
		return true;
	}
	return GetMousePosition(OutX, OutY);
}

// ★ Волна В4 рыбалки: тип попадания 5 `Picking_TestRayPickWorldTarget@0x992D60` (game.c:30370-30383) — см. объявление.
//   Луч — тот же, что у `PickForClick` (депроекция экранной точки; эталон `pick_BuildRaySegment@0x112D650`).
int32 AAsdaPlayerController::PickFishZoneForDoubleClick(const FAsdaPick& Best, float* OutZoneDist, bool bQuiet) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaFishZoneSubsystem* Zones = GI ? GI->GetSubsystem<UAsdaFishZoneSubsystem>() : nullptr;
	if (!Zones) { return -1; }
	float MX = 0.f, MY = 0.f;
	if (!GetClickScreenPos(MX, MY)) { return -1; }
	FVector Origin, Dir;
	if (!DeprojectScreenPositionToWorld(MX, MY, Origin, Dir)) { return -1; }
	float ZoneDist = 0.f;
	// `pickClosestFromList@0xFC6360`: ближайшая по лучу зона текущей карты.
	const int32 Spot = Zones->PickZoneByRay(Origin, Dir, ZoneDist);
	if (Spot < 0) { return -1; }
	if (OutZoneDist) { *OutZoneDist = ZoneDist; }
	// ★ [ПОРТ] 12.10.2026, ключ `asda.Fish.ZonePickWaterOnly` (разбор у объявления ключа): точка клика должна лежать на воде.
	//   Касается попаданий «земля» (террейн/вода, не объект карты) и промаха луча; сущности и объекты карты — соперники ниже,
	//   как у эталона.
	if (CVarAsdaFishZonePickWaterOnly.GetValueOnGameThread() != 0)
	{
		const bool bMapObj = Best.Kind == EAsdaPickKind::Ground && UAsdaFishZoneSubsystem::IsMapObjectHit(Best.Component.Get());
		const bool bLandOrSky = Best.Kind == EAsdaPickKind::None
			|| (Best.Kind == EAsdaPickKind::Ground && !bMapObj
				&& !UAsdaFishZoneSubsystem::IsWaterHit(GetWorld(), Best.Component.Get(), Best.Point));
		if (bLandOrSky)
		{
			if (bQuiet) { return -1; }
			UE_LOG(LogAsdaPC, Log, TEXT("[рыбалка] двойной клик: зона %d (%.0f) в луче, но точка клика (%.0f %.0f %.0f) не на воде — тип 5 не выбран (asda.Fish.ZonePickWaterOnly 1)"),
				Spot, ZoneDist, Best.Point.X, Best.Point.Y, Best.Point.Z);
			return -1;
		}
	}
	// Соперник — итог прочих типов. Сущность (типы 0…3) и объект карты (тип 4) соревнуются по дальности; террейн и вода
	//   эталоном не пикаются вовсе (их нет ни в одном из пяти списков `Picking_TestRayPickWorldTarget`).
	bool bRival = false;
	switch (Best.Kind)
	{
	case EAsdaPickKind::Mob: case EAsdaPickKind::Player: case EAsdaPickKind::Npc: case EAsdaPickKind::Loot:
		bRival = Best.Actor.IsValid();
		break;
	case EAsdaPickKind::Ground:
		// ★ Ревизия В4 ВАЖН-2 (09.10.2026): объект карты — по КОМПОНЕНТУ попадания (экземпляры ISM на акторе
		//   `AsdaMapObjects_<карта>`; прежняя проверка класса актора `AStaticMeshActor` их не видела — зона выбиралась сквозь
		//   дома, деревья и мосты, а у эталона `collide_rayVsMapObjects` ближе зоны её закрывает, g:30344-30356).
		bRival = UAsdaFishZoneSubsystem::IsMapObjectHit(Best.Component.Get());
		break;
	default:
		break;
	}
	if (bRival)
	{
		const float RivalDist = (float)FVector::Dist(Origin, Best.Point);
		// Зона проверяется последней: побеждает только при `bestDist > hitDist` (строго).
		if (!(RivalDist > ZoneDist))
		{
			if (bQuiet) { return -1; }
			UE_LOG(LogAsdaPC, Log, TEXT("[рыбалка] двойной клик: зона %d (%.0f) дальше/равна сопернику (%.0f) — тип 5 не выбран"),
				Spot, ZoneDist, RivalDist);
			return -1;
		}
	}
	return Spot;
}

void AAsdaPlayerController::SetTarget(AActor* NewTarget)
{
	if (TargetActor.Get() == NewTarget) { return; }
	// ★ Журнал боя: смена/снятие цели и судьба боевого цикла (до сброса флагов — видно, шёл ли бой).
	ASDA_CTRACE(EAsdaCT::Target, TEXT("цель: %s → %s (автоатака=%s, серия начата=%s, в стойке боя=%s)"),
		*AsdaCtActorDesc(GetGameInstance(), TargetActor.Get()), *AsdaCtActorDesc(GetGameInstance(), NewTarget),
		bAutoAttack ? TEXT("да") : TEXT("нет"), bCombatStarted ? TEXT("да") : TEXT("нет"), bInCombatStance ? TEXT("да") : TEXT("нет"));
#if ASDA_COMBAT_TRACE
	if (AsdaCombatTrace::CurrentCycle() > 0)
	{
		const AActor* OldCt = TargetActor.Get();                          // прежняя цель (для решения о цикле)
		const AAsdaMobActor* OldMobCt = Cast<AAsdaMobActor>(OldCt);       // прежняя цель-моб
		int32 OldUidxCt = -1;                                             // userIdx прежней цели-игрока (-1 — не игрок/неизвестен)
		if (OldCt && OldCt->IsA<AAsdaPlayerCharacter>())
		{
			const UAsdaWorldSubsystem* WSct = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			OldUidxCt = WSct ? WSct->FindUserIdxByActor(OldCt) : -1;
		}
		// Прежняя цель — та, по которой открыт цикл (моб по rid, игрок по userIdx PvP-цикла).
		const bool bOldIsCycleTgt = (OldMobCt && OldMobCt->NetRenderId >= 0 && OldMobCt->NetRenderId == AsdaCombatTrace::CurrentCycleTarget())
			|| (OldUidxCt >= 0 && OldUidxCt == AsdaCombatTrace::CurrentCyclePlayer());
		// Бой ещё держится: автоатака, PvP-атака или боевая стойка с кулдауном.
		const bool bFightHeld = bAutoAttack || bPvpAttack || CombatStanceUntil > 0.0;
		if (NewTarget)
		{
			ASDA_CTRACE_END(TEXT("смена цели (выбрана другая сущность)"));
		}
		else if (bOldIsCycleTgt && bFightHeld)
		{
			// ★ Флаги смерти здесь НЕ смотрим: мир снимает цель (ClearTarget) ДО NetDeath, а ветка 4017 flags=0 сбрасывает
			//   bServerDead раньше — по ним цикл закрывался до смерти/трупа/награды. Цикл закроет выход из боевой стойки
			//   (кулдаун/движение/клик/4205) либо страховка «боя нет, стойки нет» в PlayerTick.
			ASDA_CTRACE(EAsdaCT::Exit, TEXT("цель цикла %s снята посреди боя (автоатака=%s, PvP=%s, стойка %s) — цикл остаётся открытым до выхода из боевой стойки"),
				*AsdaCtActorDesc(GetGameInstance(), OldCt), bAutoAttack ? TEXT("да") : TEXT("нет"), bPvpAttack ? TEXT("да") : TEXT("нет"),
				CombatStanceUntil > 0.0 ? *FString::Printf(TEXT("ещё %.1f с"), CombatStanceUntil - FPlatformTime::Seconds()) : TEXT("не взведена"));
		}
		else
		{
			ASDA_CTRACE_END(TEXT("снятие цели"));
		}
	}
#endif
	// ★ Смена/снятие цели гасит боевой цикл И ЗАМАХ — 1:1 эталон: CCharacter_SetActionState case 0xA
	//   (смерть цели) обходит все сущности и у каждой, чья цель = умерший, при state==5 делает
	//   CChar_SetState(entity, t, 0, 0, 20) = боевой покой. Защёлки/минимальной длительности у состояния
	//   атаки в оригинале НЕТ. Важно ДРУГОЕ: сюда нельзя приходить раньше, чем цель реально умрёт —
	//   а умирает она на КАДРЕ УДАРА (KillTargetAfter в UAsdaWorldSubsystem::OnCombatAnim), не по
	//   приходу пакета. Именно ранний вызов и съедал анимацию при киле с одного удара (юзер 2026-07-31).
	if (bAutoAttack)
	{
		if (AAsdaPlayerCharacter* C = GetChar())
		{
			// ★ СМЕНА цели на другого живого моба ≠ выход из боя: эталон (Player_UpdateAutoActionState,
			//   entity_char.c:24048) переписывает state=5 ПРЯМОЙ записью — клип атаки продолжает цикл без
			//   повторного «взвода». Даём циклу конечную отсрочку: клик-атака по новому мобу подхватит его
			//   через гейт StartAttackSwing; НЕ подхватит — штатно доиграет в стойку.
			AAsdaMobActor* NewMob = Cast<AAsdaMobActor>(NewTarget);
			if (NewMob && !NewMob->IsDead() && !NewMob->bServerDead)
			{
				ASDA_CTRACE(EAsdaCT::Swing, TEXT("смена цели на живого моба rid=%d — цикл замаха придержан на 2.0 с (подхватит клик-атака)"), NewMob->NetRenderId);
				C->HoldAttackLoop(2.0f);
			}
			else
			{
				ASDA_CTRACE(EAsdaCT::Swing, TEXT("снятие цели посреди боя — замах остановлен (StopAttackAnim, эталон SetState 0/0/20)"));
				C->StopAttackAnim();   // снятие цели/смерть → эталон CChar_SetState(0,0,20)
			}
		}
	}
	// ★ Ревизия 25.10 (фантомный урон, В-1) [ИСПРАВЛЕНИЕ]: смена/снятие цели посреди своей серии бросает её — 4028 по прежней
	//   цели больше не уйдёт, её неприменённые значения сервер не спишет (следующий удар — новый 4026/4201, а до него гейт часов
	//   держит паузу, и без отметки эти значения успели бы показаться и вычесться из полосы прежней цели).
	if (bCombatStarted && TargetActor.Get() != NewTarget)
	{
		if (UAsdaAttackRhythmSubsystem* RhA = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr)
		{
			RhA->NoteSeriesAbandoned(NewTarget ? TEXT("смена цели") : TEXT("снятие цели"));
		}
	}
	// ★ 01.11.2026: снятие цели / смена не на живого моба — у эталона 5 → 0 (подтип 20) ⇒ 4024/400; смена на живого моба — прямая
	//   запись состояния 5 (Player_UpdateAutoActionState, entity_char.c:24048), 400 нет.
	{
		const AAsdaMobActor* NewMob400 = Cast<AAsdaMobActor>(NewTarget);
		if (bAutoAttack && !(NewMob400 && !NewMob400->IsDead() && !NewMob400->bServerDead)) { AsdaLeaveAttackAction(this, NewTarget ? TEXT("смена цели") : TEXT("снятие цели")); }
	}
	bAutoAttack = false; bCombatStarted = false; bInCombatStance = false; bWaitingCombatAck = false;
	bArmImpactOnSwingStart = false;   // ★ ревизия 17.10, п.5: заявка прежнего боя окно новой цели не взводит
	// ★ Ревизия 17.10 (правка боя), п.6: снятие/смена цели прерывает замах по прежней — окно «наш замах в полёте»
	//   сокращается до попадания, назначенного ответом сервера, или закрывается. Прежняя цель запоминается здесь, окно
	//   режется в конце (после смены `TargetActor`). Снятие цели её же смертью (`KillTargetAfter`, конец окна, 4015/4017)
	//   ничего не ломает: окно там уже закрыто или кончается в этот же миг, а `NetDeath` повторно не срабатывает.
	const AAsdaMobActor* OldMobForWindow = Cast<AAsdaMobActor>(TargetActor.Get());   // прежняя цель-моб (окно — по ней)
	const bool bCutImpactWindow = OldMobForWindow && !OldMobForWindow->IsDead();      // живая прежняя цель — замах прерван
	const int32 OldRidForWindow = OldMobForWindow ? OldMobForWindow->NetRenderId : -1;  // окно режется, только если оно по ней
	// Снять панель с прежней цели (эталон: плашка цели видна только для ВЫБРАННОЙ).
	if (AActor* Old = TargetActor.Get())
	{
		if (UAsdaNameplateComponent* NP = Old->FindComponentByClass<UAsdaNameplateComponent>())
		{
			NP->bShowHp = false;
			// У МОБА имя тоже гаснет — остаётся только если сейчас на него наведён курсор.
			if (Old->IsA(AAsdaMobActor::StaticClass()) && Old != HoverActor.Get())
			{
				NP->bAlwaysShowName = false;
			}
			NP->RefreshNameplate();
		}
	}
	ApplyTargetHighlight(TargetActor.Get(), false);   // ★ снять подсветку с прежней цели
	TargetActor = NewTarget;
	if (NewTarget)
	{
		if (UAsdaNameplateComponent* NP = NewTarget->FindComponentByClass<UAsdaNameplateComponent>())
		{
			// Цель-моб: показать полосу HP (эталон окно 1600 с сегментами).
			if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(NewTarget)) { NP->SetHp(M->CurHp, M->MaxHp, 0); }
			// ★ Цель-ИГРОК: значения берём из ЕДИНОГО источника истины (4058 приходит и о чужих —
			//   эталон netHandleHpMpUpdate ищет сущность по userIdx@0 без гейта «свой»). Если по нему
			//   снапшота ещё не было, полоса останется прежней, а не «нулём с потолка».
			else if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
			{
				const int32 Uidx = WS->FindUserIdxByActor(NewTarget);
				if (Uidx >= 0) { WS->ApplyVitalsToNameplate(Uidx); }
			}
			NP->bShowHp = true;
			NP->bAlwaysShowName = true;
			NP->RefreshNameplate();
		}
		ApplyTargetHighlight(NewTarget, true);   // ★ подсветка модели цели (эталон: яркость ×3 в .z)
		UE_LOG(LogAsdaPC, Log, TEXT("Цель выбрана: %s"), *NewTarget->GetName());
	}
	// ★ Ревизия 17.10, п.6 (см. выше): после смены `TargetActor` — снятие цели изнутри окна сюда уже не вернётся.
	if (bCutImpactWindow)
	{
		if (UAsdaWorldSubsystem* WSc = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			WSc->CutOurImpactWindow(NewTarget ? TEXT("смена цели") : TEXT("снятие цели"), OldRidForWindow);
		}
	}
}

void AAsdaPlayerController::ClearTarget()
{
	SetTarget(nullptr);
}

// ★ ПОДСВЕТКА ЦЕЛИ. Эталон (game_main.c:1449-1462 + render_core.c:40099): у КАЖДОГО узла меша цели
//   шейдерный множитель яркости NoSkinTexScroll.z = 3.0 (у остальных 1.0), в .fx это
//   «Change Brightness»: RGBColor += z*0.1 → дельта выделения +0.2 по всем каналам.
//   Мёртвая цель (Entity_GetActionState==10) и САМ игрок в оригинале не подсвечиваются.
//   Отношение цели (моб/NPC/игрок) на подсветку в ОРИГИНАЛЕ не влияет — здесь оно только выбирает
//   стиль в DataAsset (по умолчанию все стили одинаковые, белые).
void AAsdaPlayerController::ApplyTargetHighlight(AActor* Actor, bool bOn)
{
	if (!Actor) { return; }
	if (Actor == GetPawn()) { return; }                     // сам игрок — не подсвечиваем (эталон)

	EAsdaTargetRelation Rel = EAsdaTargetRelation::Neutral;
	int32 Rid = -1;
	const TCHAR* KindName = TEXT("объект");
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(Actor))
	{
		Rel = EAsdaTargetRelation::Hostile; Rid = M->NetRenderId; KindName = TEXT("моб");
	// ★★★ ИСПРАВЛЕНО 2026-08-25 (владелец: «как только персонаж начал взводить оружие — выделение
	//   и плашка пропали»). Здесь стояло `|| bServerDead`, а эта метка — «обречён» (бит 0x20
	//   оригинала) — ставится СРАЗУ по приходу боевого пакета с нулевым здоровьем, то есть за
	//   полторы секунды до кадра удара. Метка нужна боевой логике, чтобы НЕ начинать новый замах
	//   по уже засчитанной цели, но гасить по ней ВИЗУАЛ нельзя: в оригинале рамка и подсветка
	//   живут, пока цель не умрёт по-настоящему. Условие — только реальная смерть.
		if (bOn && M->IsDead()) { bOn = false; }   // мёртвую цель не подсвечиваем
	}
	else if (const AAsdaNpcActor* N = Cast<AAsdaNpcActor>(Actor))
	{
		Rel = EAsdaTargetRelation::Friendly; Rid = N->NetRenderId; KindName = TEXT("NPC");
	}
	else if (Actor->IsA(AAsdaPlayerCharacter::StaticClass()))
	{
		Rel = EAsdaTargetRelation::Player; KindName = TEXT("игрок");
	}

	// ★★★ КОЛЬЦО ПОД ЦЕЛЬЮ (отступление от эталона, дефолт ВКЛ — см. блок у `CVarAsdaTargetRing`).
	//   Ставится/снимается ЗДЕСЬ, в единственной точке смены выделения, — своей системы выбора цели
	//   у кольца нет. Снятие идёт БЕЗУСЛОВНО (без гейта cvar): выключив `asda.Target.Ring 0` на лету,
	//   владелец должен увидеть, как уже висящее кольцо гаснет при следующей смене цели.
	if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		const bool bRingOn = bOn && (CVarAsdaTargetRing.GetValueOnGameThread() != 0);
		WS->ApplyTargetRingToActor(Actor, bRingOn, Rel == EAsdaTargetRelation::Hostile);
		bTargetRingShown = bOn;   // кэш для `TargetHighlightTick` (гашение на смерти цели)
		ASDA_CTRACE(EAsdaCT::Target, TEXT("выделение %s: %s rid=%d '%s' — кольцо %s%s"), bOn ? TEXT("ВКЛ") : TEXT("СНЯТО"), KindName, Rid,
			*Actor->GetName(), bRingOn ? (Rel == EAsdaTargetRelation::Hostile ? TEXT("красное") : TEXT("зелёное")) : TEXT("нет"),
			(bOn || !Cast<AAsdaMobActor>(Actor) || !Cast<AAsdaMobActor>(Actor)->IsDead()) ? TEXT("") : TEXT(" (цель мертва — не подсвечивается)"));
	}

	UAsdaTargetHighlightComponent* HC = Actor->FindComponentByClass<UAsdaTargetHighlightComponent>();
	if (!HC)
	{
		if (!bOn) { return; }                               // снимать нечего
		HC = NewObject<UAsdaTargetHighlightComponent>(Actor, TEXT("AsdaTargetHighlight"));
		HC->RegisterComponent();
	}
	HC->SetHighlighted(bOn, Rel, HighlightSettings);
	if (bOn)
	{
		// ★ ВНИМАНИЕ: дефолт — «выключено», и это 1:1 оригинал: шейдер сборки юзера компоненту .z
		//   параметра NoSkinTexScroll НЕ читает, модель цели визуально не меняется.
		//   Включить улучшение на лету: `asda.Target.HL 1|2|3`, посмотреть фактику: `asda.Target.HLDump`.
		const TCHAR* ModeName = TEXT("выключено (как в оригинале)");
		switch (HC->EffectiveMode())
		{
		case EAsdaHighlightMode::Brightness: ModeName = TEXT("яркость (+0.2)");  break;
		case EAsdaHighlightMode::Outline:    ModeName = TEXT("обводка");         break;
		case EAsdaHighlightMode::Both:       ModeName = TEXT("яркость+обводка"); break;
		default: break;
		}
		// ★ Формулировка правлена 2026-08-20: прежняя строка «выделение включено: тип=выключено»
		//   читалась как противоречие и мешала разбирать живой лог владельца.
		// ★ Кольцо пишем в ту же строку: живой лог PIE — единственный способ владельцу проверить,
		//   что под целью встало ИМЕННО то кольцо (зелёное/красное), а не «что-то светится».
		const TCHAR* RingName = (CVarAsdaTargetRing.GetValueOnGameThread() == 0)
			? TEXT("выключено (asda.Target.Ring 0)")
			: (Rel == EAsdaTargetRelation::Hostile ? TEXT("красное (unfriendlyselect)")
			                                       : TEXT("зелёное (friendlyselect)"));
		UE_LOG(LogAsdaPC, Log, TEXT("[таргет] выбран %s rid=%d (%s), подсветка модели: %s, кольцо: %s"),
			KindName, Rid, *Actor->GetName(), ModeName, RingName);
	}
}

// Каждый кадр: цель умерла/деспавнилась — гасим подсветку (эталон: actionState==10 снимает ключи A/B).
void AAsdaPlayerController::TargetHighlightTick()
{
	AActor* T = TargetActor.Get();
	if (!T) { return; }
	const AAsdaMobActor* M = Cast<AAsdaMobActor>(T);
	if (!M) { return; }
	// ★★★ ИСПРАВЛЕНО 2026-08-25 (владелец: «как только персонаж начал взводить оружие — выделение
	//   и плашка пропали»). Здесь стояло `|| bServerDead`, а эта метка — «обречён» (бит 0x20
	//   оригинала) — ставится СРАЗУ по приходу боевого пакета с нулевым здоровьем, то есть за
	//   полторы секунды до кадра удара. Метка нужна боевой логике, чтобы НЕ начинать новый замах
	//   по уже засчитанной цели, но гасить по ней ВИЗУАЛ нельзя: в оригинале рамка и подсветка
	//   живут, пока цель не умрёт по-настоящему. Условие — только реальная смерть.
	const bool bWant = !M->IsDead();
	// ★★★ КОЛЬЦО ГАСНЕТ НА СМЕРТИ ЦЕЛИ — в той же точке, что и подсветка модели (эталон гасит
	//   ключи A/B при actionState==10). Ветка выше выходила по `!HC`, а компонент подсветки при
	//   дефолтном режиме `None` может и не быть создан ⇒ кольцо тогда осталось бы висеть на трупе.
	//   Поэтому кольцо обрабатывается ДО проверки компонента и по своему кэшу состояния.
	if (bWant != bTargetRingShown)
	{
		ASDA_CTRACE(EAsdaCT::Target, TEXT("кольцо/подсветка цели rid=%d '%s' %s (тик выделения: %s)"), M->NetRenderId, *M->MobName,
			bWant ? TEXT("снова зажжены") : TEXT("погашены"), bWant ? TEXT("цель жива") : TEXT("цель умерла, IsDead"));
		bTargetRingShown = bWant;
		if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			WS->ApplyTargetRingToActor(T, bWant && CVarAsdaTargetRing.GetValueOnGameThread() != 0, /*bHostile*/true);
		}
	}
	UAsdaTargetHighlightComponent* HC = T->FindComponentByClass<UAsdaTargetHighlightComponent>();
	if (!HC) { return; }
	if (bWant != HC->IsHighlighted())
	{
		HC->SetHighlighted(bWant, EAsdaTargetRelation::Hostile, HighlightSettings);
	}
}

// ★ Метка клика по земле (эталон: CEffectMgr_CreateEffect type=1 в точке клика + 20 по вертикали,
//   жизнь ~1.1 с, самоудаление; старая метка при новом клике не снимается).
void AAsdaPlayerController::SpawnClickMarker(const FVector& WorldPoint)
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	if (!ClickMarkerClass)
	{
		// ★ Дедуп №2 В1-C (F5): путь — общим правилом по имени BP (строка побайтно прежняя). Класс по-прежнему
		//   кэшируется (клик — частая операция) и без него метка не ставится и время метки не обновляется.
		ClickMarkerClass = LoadClass<AActor>(nullptr, *AsdaAssetPaths::FxClassPathFromBp(AsdaFxBp::kClickMarker));
		if (!ClickMarkerClass) { return; }
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Loc(WorldPoint.X, WorldPoint.Y, WorldPoint.Z + 20.f);   // +20 (эталон flt_1396DA8)
	if (AActor* Marker = W->SpawnActor<AActor>(ClickMarkerClass, Loc, FRotator::ZeroRotator, SP))
	{
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F2): срок — `NaturalLifeSpan` самой метки (эталон: `CEffectMgr_CreateEffect`
		//   type 1 без хэндла — эффект живёт до конца своих эмиттеров/контейнера). Было жёсткое 1,1 с («длительность JEB» —
		//   литерал, не данные); по снимку `asda.Dedup.W2C.Life` natural 1,6 с (контейнер 1,1 с endMode 0 + 0,5). Не эффект-актор — прежний срок как запас.
		if (AAsdaEffectActor* Fx = Cast<AAsdaEffectActor>(Marker)) { Fx->SetLifeSpan(Fx->NaturalLifeSpan()); }
		else                                                        { Marker->SetLifeSpan(1.1f); }
	}
	LastMarkerTime = FPlatformTime::Seconds();
}

// ★ Смена курсора (эталон Cursor_SetByIndex: один SetCursor на состояние; hotspot из заголовка .cur).
void AAsdaPlayerController::SetGameCursor(EAsdaCursor C)
{
	if (CurrentCursor == C) { return; }
	CurrentCursor = C;
	const TCHAR* File = TEXT("Slate/Cursors/BO");
	switch (C)
	{
	case EAsdaCursor::Battle: File = TEXT("Slate/Cursors/AO");     break;   // враг под курсором (красный шар)
	case EAsdaCursor::Talk:   File = TEXT("Slate/Cursors/TK");     break;   // NPC (золотая рука)
	case EAsdaCursor::Hand:   File = TEXT("Slate/Cursors/ID");     break;   // предмет на земле
	case EAsdaCursor::Move:   File = TEXT("Slate/Cursors/Move");   break;   // точка движения
	case EAsdaCursor::NoMove: File = TEXT("Slate/Cursors/noMove"); break;   // непроходимо
	case EAsdaCursor::Hammer: File = TEXT("Slate/Cursors/HM");     break;   // «святость» сработала
	case EAsdaCursor::Bottle: File = TEXT("Slate/Cursors/HO");     break;   // взведён «святой» предмет
	case EAsdaCursor::Pick:   File = TEXT("Slate/Cursors/P0");     break;   // взведён камень переноса улучшения (индекс 26)
	case EAsdaCursor::Normal:
	default:                  File = TEXT("Slate/Cursors/BO");     break;   // idle (синий шар)
	}
	// Меняем и Default, и Hand: движок сам выбирает тип по контексту, а в мире курсор должен быть один.
	UWidgetBlueprintLibrary::SetHardwareCursor(this, EMouseCursor::Default, FName(File), FVector2D::ZeroVector);
	UWidgetBlueprintLibrary::SetHardwareCursor(this, EMouseCursor::Hand,    FName(File), FVector2D::ZeroVector);
}

// Наведение: эталон показывает имя МОБА только при наведении/выборе (постоянного имени у мобов нет).
void AAsdaPlayerController::UpdateHover()
{
	const FAsdaPick P = PickHover();
	// ★ КУРСОР по тому, что под мышью (эталон: состояние наведения → индекс курсора).
	//   Если курсор занял интерфейс (мышь над полем большой карты) — мир его не трогает.
	if (bUiCursorOverride)
	{
		// пропускаем блок курсора, наведение на сущности считаем дальше как обычно
	}
	else
	switch (P.Kind)
	{
	case EAsdaPickKind::Mob:    SetGameCursor(EAsdaCursor::Battle); break;   // враг → «бой»
	case EAsdaPickKind::Npc:    SetGameCursor(EAsdaCursor::Talk);   break;   // NPC → «разговор»
	case EAsdaPickKind::Loot:   SetGameCursor(EAsdaCursor::Hand);   break;   // лут → «взять»
	case EAsdaPickKind::Player: SetGameCursor(EAsdaCursor::Talk);   break;   // чужой игрок → как NPC (взаимодействие)
	case EAsdaPickKind::Ground: SetGameCursor(EAsdaCursor::Normal); break;   // земля (nomove — по проходимости)
	default:                    SetGameCursor(EAsdaCursor::Normal); break;
	}
	AActor* NewHover = (P.Kind == EAsdaPickKind::Mob || P.Kind == EAsdaPickKind::Npc ||
	                    P.Kind == EAsdaPickKind::Player) ? P.Actor.Get() : nullptr;
	AActor* OldHover = HoverActor.Get();
	if (OldHover == NewHover) { return; }
	// Скрыть плашку у прежнего наведённого моба (если он не выбранная цель) — эталон: у мобов
	//   постоянной плашки нет, она живёт только на время наведения/выбора.
	if (OldHover && OldHover != TargetActor.Get() && OldHover->IsA(AAsdaMobActor::StaticClass()))
	{
		if (UAsdaNameplateComponent* NP = OldHover->FindComponentByClass<UAsdaNameplateComponent>())
		{
			NP->bAlwaysShowName = false;
			NP->RefreshNameplate();
		}
	}
	HoverActor = NewHover;
	if (NewHover && NewHover->IsA(AAsdaMobActor::StaticClass()))
	{
		if (UAsdaNameplateComponent* NP = NewHover->FindComponentByClass<UAsdaNameplateComponent>())
		{
			NP->bAlwaysShowName = true;
			NP->RefreshNameplate();
		}
	}
}
