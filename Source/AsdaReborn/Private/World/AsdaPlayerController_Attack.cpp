// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerController_Attack.cpp — ★ R2 10.11.2026 (разрез AsdaPlayerController.cpp, дословный перенос): атака: отложенный 4201, AttackTick, подбор лута, атака игрока.
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
// ★ CurrentMapId — развилка CampWar (mapId 19) перед окном 570   ★ R3: комментарий снятого включения "World/AsdaLoadManagerSubsystem.h" (файл им не пользовался) — сохранён
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
// cmd 37 «INTERFACE MAP» — карта мира (окно 1710)   ★ R3: комментарий снятого включения "UI/AsdaChatWindowSubsystem.h" (файл им не пользовался) — сохранён
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
// ★ 27.09.2026: иконка «сесть/встать» на хотбаре (`ToggleSitStand`)   ★ R3: комментарий снятого включения "UI/AsdaHotbarSubsystem.h" (файл им не пользовался) — сохранён
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
// ★ окно разговора с NPC (JUI 570) — этапы Э1/Э2   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
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
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#endif
#include "AsdaPlayerControllerInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerController*.cpp (LogAsdaPC, чистые хелперы)
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя (клик → цель → бег → дальность → заявка → замах → выход)
#include "World/AsdaSkillCastLock.h"  // ★ 05.11.2026: «свой навык занят» — гейты клика/клавиш цели, отложенное действие после навыка
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaPlayerControllerInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPlayerController*.cpp

#if ASDA_COMBAT_TRACE
namespace AsdaCtPc {
	// Ник персонажа с плашки (как `AsdaCtPcWho` в AsdaPlayerCharacter.cpp); плашки/имени нет — имя актора.
	FString PlayerNick(const AAsdaPlayerCharacter* C)
	{
		if (!C) { return TEXT("?"); }
		return (C->Nameplate && !C->Nameplate->DisplayName.IsEmpty()) ? C->Nameplate->DisplayName : C->GetName();
	}
}
#endif // ASDA_COMBAT_TRACE

#if ASDA_COMBAT_TRACE
static FAsdaCtOnce GAsdaCtApproachOnce;   // «подход к rid» (AttackTick)
#endif // ASDA_COMBAT_TRACE

// ★ 29.10.2026 (баг Charge) [ОРИГИНАЛ]: подход автоатаки к цели вне дальности ждёт, пока свой навык занят (25/24/14 или клип навыка).
//   0 — прежний порт (подход шёл посреди заявки и показа навыка: Charge превращался в бег к цели). Разбор — в `AttackTick`.
static ASDA_DEV_CVAR(int32, CVarAsdaApproachWaitsSkill,
	TEXT("asda.Combat.ApproachWaitsSkill"), 1,
	TEXT("1 = как оригинал: подход автоатаки не идёт, пока свой навык занят (заявка/показ/накопление); 0 = прежний порт."),
	ECVF_Default);
static bool ApproachWaitsSkillOn() { return CVarAsdaApproachWaitsSkill.GetValueOnGameThread() != 0; }

// ── ★ БОЕВОЙ ЦИКЛ (порт FSM=5 + CCharacter_ResolveBasicAttack@0xA47DC0): пока цель-моб выбрана и жива —
//   вне дальности: ПОДХОД 4006 с tgtRenderId (эталон Combat_SendNormalAttack@0x9F8CA0; сервер по достижении
//   дальности начинает бой 4027); в дальности: удар 4026 с периодом (врем., до реверса скорости клипа 6). ──
// ★★★ 15.10.2026 (волна mobpos_swing, `Reports/anticheat/wave_PORT_mobpos_swing.md` §1): КООРДИНАТЫ В 4026/4028 —
//   ПОЗИЦИЯ СВОЕГО ПЕРСОНАЖА. Порт с июля слал туда позицию ЦЕЛИ (видимую позицию моба): имя параметра `targetX` у
//   `netSabzil_SendActionRaw@0x9FA180` — миснейм IDA. Эталон: `Player_UpdateAutoActionState@0xA249A0` case 5 передаёт
//   `CObject_GetSubField47649(локальный игрок)` (узел кривой движения своего персонажа), ветки замаха `skill_buff.c`
//   (баллиста :1365-1369 и др.) — то же для 4028; перехват оригинала (netcap 29.07) — X/Z 4026/4028 побайтно равны
//   позиции предшествующего 4008. Сервер эти числа сейчас не читает (GameServer `Send_CharNormalMonsterAttack@0x4C49F0` берёт
//   из тела только индекс моба; дальность меряет NpcServer по своим клеткам) — правка ради точности протокола: «принятый»
//   первый 4026 живого лога 15.10 объясняется не координатами, а веткой NpcServer «вне мгновенной дальности» (8202 → 4027
//   «первый урон» без вычета HP). Ключ отката `asda.Combat.AttackPosSelf 0` (прежнее: позиция цели).
static ASDA_DEV_CVAR(int32, CVarAsdaAttackPosSelf,
	TEXT("asda.Combat.AttackPosSelf"), 1,
	TEXT("1 (по умолчанию) = X/Z в 4026/4028 — позиция своего персонажа (эталон, перехват оригинала); 0 = прежний порт: позиция цели."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarAsdaRangeOutServerCap,
	TEXT("asda.Combat.RangeOutServerCap"), 1,
	TEXT("1 (по умолчанию) = удержание боя (гистерезис дальности) не шире допуска NpcServer к 4028: дальность оружия + 2 клетки; ")
	TEXT("0 = прежний порт: 1.6 × дальности (дальнобойное слало 4028, которые сервер отвергал «вне дальности»)."),
	ECVF_Default);

bool AAsdaPlayerController::RangeOutServerCapEnabled()
{
	return CVarAsdaRangeOutServerCap.GetValueOnGameThread() != 0;
}

// ★★★ 25.10.2026 [ИСПРАВЛЕНИЕ] БАГ ИГРОКОВ «ФАНТОМНЫЙ УРОН» — ПЕРЕЗАПУСК СЕРИИ НЕ ОБГОНЯЕТ СЕРВЕРНЫЕ ЧАСЫ (отчёт
//   `Reports/phantom_damage/REPORT.md`). [ОРИГИНАЛ] NpcServer начинает серию 4026/4201 с обнуления момента прошлого удара
//   (`sub_45FCD0` @0x45FE0C / `sub_460710` @0x46085F: `mov [игрок+0FE90h], 0`) и тут же принимает удар без проверки скорости:
//   «атака → микроход → снова клик по цели» давал по 2 применённых удара (ближний бой) на каждый перезапуск, быстрее
//   скорости атаки (журнал игрока 20.10: 4 перезапуска за 2.4 с, HP 1400 → 902 → 452 → 269 → 0). Серверная правка
//   (AddonNPC N19) перестаёт обнулять часы; честный клиент обязан ждать их сам, иначе ранний перезапуск сервер молча отбросит
//   как ускорение. Поэтому 4026 (вход в бой с мобом) и 4201 (PvP) уходят не раньше `UAsdaAttackRhythmSubsystem::EarliestNext4028S`
//   — того же срока, что и 4028; пока ждём — боевая стойка лицом к цели. Ключ: 1 — гейт стоит; 0 — заявка сразу (как оригинал).
static ASDA_DEV_CVAR(int32, CVarAsdaRestartSeriesGate,
	TEXT("asda.Combat.RestartSeriesGate"), 1,
	TEXT("1 (по умолчанию) = 4026/4201 (начало серии) уходят не раньше серверных часов атаки, как 4028 («фантомный урон»: ")
	TEXT("микроход и повторный клик не ускоряют удары); 0 = как оригинал — заявка сразу."),
	ECVF_Default);

// ★ Ревизия М-4: свой таймер отложенной 4201 (локальный игрок один; таймер живёт в менеджере таймеров мира и умирает с ним).
static FTimerHandle GAsdaDeferred4201Timer;
// Снять отложенную 4201 (самотест; штатно её снимают проверки в самой лямбде).
void AsdaCancelDeferred4201(AAsdaPlayerController* PC) { if (PC) { PC->GetWorldTimerManager().ClearTimer(GAsdaDeferred4201Timer); } }
// Ждёт ли отложенная 4201 (самотест).
bool AsdaIsDeferred4201Pending(AAsdaPlayerController* PC) { return PC && PC->GetWorldTimerManager().IsTimerActive(GAsdaDeferred4201Timer); }

// Сколько секунд ещё ждать начала серии (0 — можно сейчас; гейт выключен или часов нет — 0).
static double AsdaRestartSeriesWaitS(UGameInstance* GI)
{
	if (CVarAsdaRestartSeriesGate.GetValueOnGameThread() == 0 || !GI) { return 0.0; }
	const UAsdaAttackRhythmSubsystem* Rh = GI->GetSubsystem<UAsdaAttackRhythmSubsystem>();
	return Rh ? Rh->SecondsUntilNext4028() : 0.0;
}

// ★ 05.11.2026 (`World/AsdaSkillCastLock`): точка хода лучом по земле .TER для клика, отложенного на время навыка (расчёт —
//   закрытый `AsdaClickRayTerrainPoint` этого файла, ключ `asda.Move.ClickRayTerrain`).
bool AsdaSkillCastLock::TerrainPointUnderScreen(AAsdaPlayerController* PC, const FVector2D& Screen, FVector& OutUe)
{
	const AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!Ch || CVarAsdaClickRayTerrain.GetValueOnGameThread() == 0) { return false; }
	return AsdaClickRayTerrainPoint(PC, Ch->WalkGrid, Screen, OutUe);
}

FVector2D AAsdaPlayerController::AttackPacketPos(const FVector2D& SelfGame, const FVector2D& TargetGame)
{
	return (CVarAsdaAttackPosSelf.GetValueOnGameThread() != 0) ? SelfGame : TargetGame;
}

void AAsdaPlayerController::AttackTick()
{
	if (!bAutoAttack)
	{
		// ★ 17.10.2026 (Ф4): PvP-атаку погасили снаружи (клик по земле, смена цели, движение) — свои флаги и очередь серии снять.
		if (bPvpAttack)
		{
			ASDA_CTRACE(EAsdaCT::Pvp, TEXT("PvP-атака по userIdx=%d снята снаружи (клик по земле/смена цели/движение) — флаги и очередь серии сброшены"), PvpTargetUserIdx);
#if ASDA_COMBAT_TRACE
			// Снятие цели её же смертью (ClearTarget до смерти) — тот же случай, что в SetTarget: пока держится боевая стойка
			//   после ударов, цикл открыт до ExitCombatStance (смерть/награда впереди); стойки нет — закрыть сразу.
			if (CombatStanceUntil <= 0.0) { ASDA_CTRACE_END(TEXT("PvP-атака снята снаружи")); }
#endif
			bPvpAttack = false; bPvpAccepted = false; PvpTargetUserIdx = -1;
			UAsdaNetSubsystem* NetP = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (UAsdaWorldSubsystem* WSp = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; WSp && NetP)
			{
				WSp->ResetPvpSeries((int32)NetP->SelfUserNum, TEXT("PvP-атака снята"));
			}
		}
		return;
	}
	// ★★★ GM-«СКРЫТЬСЯ»: БОЙ ЗАПРЕЩЁН. Это ЕДИНСТВЕННАЯ точка, где начинается замах
	//   (`StartAttackSwing` ниже по телу) и откуда уходят 4026/4028 — поэтому гейт стоит здесь,
	//   а не только у отправителей: сервер под скрытием боевые пакеты молча выбрасывает
	//   (`Send_CharNormalMonsterAttack`/`Next`, `Recv_User_AttackToPlayer`), и клиент, отыгрывая
	//   анимацию, показывал бы удары, которых на сервере не было — ровно тот рассинхрон, на
	//   который жаловался владелец. Гасим и автобой, чтобы цикл не крутился вхолостую.
	//   МОЛЧА: нотиса у этой команды в эталоне нет ни одного (см. `World/AsdaPossessGate.h`).
	//   Ключ отката: `asda.Gm.HideAttackGate 0`. Движение, чат, эмоции и предметы не затрагиваются.
	if (AsdaGmHide::BlockCombatAction(GetGameInstance()))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("тик боя: GM-«скрыться» — автоатака снята, 4026/4028 не уходят"));
		ASDA_CTRACE_END(TEXT("GM-«скрыться»: бой запрещён"));
		AsdaLeaveAttackAction(this, TEXT("GM-«скрыться»"));   // ★ 01.11.2026: 5 → 0 — 4024/400 до снятия флагов и до хода (эталон `CCharacter_SetAction` case 0)
		bAutoAttack = false;
		bCombatStarted = false;
		return;
	}
	// ★ 28.10.2026 [ПОРТ] (решение владельца): вошёл в глубокую воду посреди боя — автобой снимается (4026/4028/4201 не уходят).
	if (AsdaSwimActionLocked(GetChar()))
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[плавание] тик боя: персонаж плывёт — автоатака снята"));
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("тик боя: персонаж плывёт (asda.Swim.ActionGate) — автоатака снята, 4026/4028 не уходят"));
		ASDA_CTRACE_END(TEXT("плавание: бой запрещён"));
		AsdaLeaveAttackAction(this, TEXT("плавание"));   // ★ 01.11.2026: 5 → 0 — 4024/400 до снятия флагов и до хода (эталон `CCharacter_SetAction` case 0)
		bAutoAttack = false;
		bCombatStarted = false;
		return;
	}
	// ★ Мёртв → боевой цикл глохнет (эталон: state 6 обнуляет bActionBusy и targetRenderId).
	if (IsSelfDead())
	{
		ASDA_CTRACE(EAsdaCT::Exit, TEXT("тик боя: свой персонаж мёртв — автоатака снята (эталон state 6)"));
		ASDA_CTRACE_END(TEXT("смерть своего персонажа"));
		bAutoAttack = false; bCombatStarted = false; return;
	}
	// ★ 17.10.2026 (Ф4): цель — персонаж (4201/4203) — свой тик.
	if (bPvpAttack) { PvpAttackTick(); return; }
	AAsdaMobActor* M = Cast<AAsdaMobActor>(TargetActor.Get());
	AAsdaPlayerCharacter* Ch = GetChar();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// ★ ЦЕЛЬ ПОМЕЧЕНА ТРУПОМ, НО ЕЩЁ НЕ УМЕРЛА (порт флага 0x20 эталона, см. AAsdaMobActor::bServerDead):
	//   сервер уже засчитал смерть, поэтому НОВЫХ замахов не начинаем (эталон Player_UpdateAutoActionState
	//   @0xA249A0: цель с флагом 0x20 не атакуется). Но и НИЧЕГО НЕ ГАСИМ: идущий замах обязан доиграть
	//   до кадра удара — в оригинале состояние 5 живёт, пока HP цели не станет 0, а это происходит именно
	//   на кадре удара. Ранний выход отсюда и съедал анимацию при убийстве с одного удара (юзер 2026-07-31).
	if (M && M->bServerDead && !M->IsDead())
	{
		ASDA_CTRACE_V(EAsdaCT::Range, TEXT("тик боя: цель rid=%d обречена сервером, но жива — новых замахов нет, идущий доигрывает"), M->NetRenderId);
		return;
	}
	if (!M || M->IsDead() || !Ch || !Net || !Net->bInGameConnected)
	{
		ASDA_CTRACE(EAsdaCT::Exit, TEXT("тик боя: цель выбыла (%s) — замах %s, боевая стойка на %.1f с%s"),
			!M ? TEXT("цели-моба нет/деспавн") : (M->IsDead() ? TEXT("цель мертва") : (!Ch ? TEXT("нет персонажа") : TEXT("нет сети/не в мире"))),
			(Ch && Ch->IsAttackAnimActive()) ? TEXT("оборван на кадре удара") : TEXT("уже доигран"),
			Ch ? CombatStanceHold : 0.f, Ch ? TEXT(" (state 0 подтип 20)") : TEXT(" — персонажа нет, стойки нет"));
#if ASDA_COMBAT_TRACE
		// Смерть цели: труп/награда впереди — цикл закроет выход из стойки. Иначе (деспавн, нет сети/персонажа) — закрыть сразу.
		if (!(M && M->IsDead() && Ch)) { ASDA_CTRACE_END(TEXT("цель потеряна (деспавн/нет сети/нет персонажа)")); }
#endif
		// ★ ЭТАЛОН (уточнение юзера 2026-07-29): после смерти цели персонаж НЕ выходит из боя сразу —
		//   он ПРЕКРАЩАЕТ атаковать и остаётся в БОЕВОЙ СТОЙКЕ (state 0 + субрежим 20, клип idle01),
		//   пока сервер не сбросит боевое состояние (пакет 4205) или не истечёт кулдаун стойки.
		if (bAutoAttack && Ch)
		{
			// Цель реально умерла → эталон CCharacter_SetActionState case 0xA гасит замах у всех, кто
			//   в неё целился: CChar_SetState(entity, t, 0, 0, 20) = боевой покой. Это происходит РОВНО
			//   сейчас — на кадре удара, когда цель и умирает, а не по приходу пакета.
			AsdaLeaveAttackAction(this, TEXT("цель выбыла"));   // ★ 01.11.2026: 5 → 0 — 4024/400 до снятия флагов и до хода (эталон `CCharacter_SetAction` case 0)
			UE_LOG(LogAsdaPC, Log, TEXT("[бой] цель выбыла: замах %s → боевая стойка (эталон state 0 подтип 20)"),
				Ch->IsAttackAnimActive() ? TEXT("оборван на кадре удара") : TEXT("уже доигран"));
			Ch->StopAttackAnim();
			Ch->SetCombatStance(true);
			CombatStanceUntil = FPlatformTime::Seconds() + CombatStanceHold;
		}
		bAutoAttack = false; bCombatStarted = false; bInCombatStance = false;
		return;
	}
	const double Now = FPlatformTime::Seconds();
	// ★ ДАЛЬНОСТЬ АТАКИ 1:1 (эталон equip case 18: игрок+0x2EA10 = (item@0x8C × 100)²; чек:
	//   range² + targetRadius² >= distSq). Дальность — из item-записи НАДЕТОГО оружия (все типы:
	//   меч/двуруч/копьё/лук/посох различаются полем таблицы). Кулаки/нет записи → AttackRange (дефолт).
	float RangeU = AttackRange;
	int32 WeaponSub = -1;
	if (Net->SelfWeaponItemId > 0)
	{
		if (UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>())
		{
			if (const FAsdaItemRecord* R = IT->GetItem(Net->SelfWeaponItemId))
			{
				if (R->AttackRangeTiles > 0) { RangeU = R->AttackRangeTiles * 100.f; }
				WeaponSub = R->ItemSubType;
			}
		}
	}
	const FVector ML = M->GetActorLocation();
	// Радиус цели (эталон прибавляет targetRadius² цели) — берём радиус капсулы-хитбокса моба.
	float TargetR = 0.f;
	if (M->PickHitbox)
	{
		// ★★★ ПО МОДУЛЮ. Живой лог 14:42 показал `tgtR=-113` — радиус капсулы вышел ОТРИЦАТЕЛЬНЫМ:
		//   `GetScaledCapsuleRadius` умножает на масштаб компонента, а у моделей монстров он бывает
		//   с отрицательной осью (зеркалирование при импорте). Отрицательный радиус ВЫЧИТАЛСЯ из
		//   дальности удара, и порог входа в бой оказывался на волосок ниже фактического расстояния:
		//   в том же логе `dist=1488` против `rangeIn=1487` — не хватало ОДНОГО юнита, отсюда и
		//   «то атакует, то не атакует». Радиус — величина неотрицательная по смыслу.
		TargetR = FMath::Abs(M->PickHitbox->GetScaledCapsuleRadius());
	}
	const float Dist = FVector::Dist2D(Ch->GetActorLocation(), ML);
	const FVector2D DstG = AsdaCoord::UeToGame2D(ML);   // ★ Аудит №3 (B1): UE→game
	const float StopDist = FMath::Max(60.f, RangeU + TargetR - 20.f);   // встать на дальности удара, НЕ в мобе
	// ★ Порог ВХОДА в бой ≥ стоп-точки: живой лог 19:25 (зонтик rid=20) — 18с подход-цикла с cur==dst,
	//   вход так и не наступал (Dist на стоп-точке оказывался > RangeIn) → «подбегает и дёргается».
	//   Мы сами встаём на StopDist — значит и бой обязан начинаться с этой дистанции.
	//   ★★★ И ЕЩЁ ОДНО СЛАГАЕМОЕ — ПОРОГ «ДОШЁЛ» У МАРШРУТА (живой лог 14:42, жалоба «то атакует,
	//   то нет; добежать не мог»). Подход ведёт не в моба, а в точку на границе дальности, и
	//   считает её достигнутой с точностью `kRouteArriveDist` (45). Если остаток пути до этой
	//   точки МЕНЬШЕ порога, маршрут завершается в тот же миг — в логе это выглядело как пара
	//   «4006 move → через 15 мс 4008 stop», повторявшаяся каждые 0.4 с. Персонаж не сдвигался,
	//   расстояние до моба не менялось, а вход в бой не наступал, потому что считался от прежнего
	//   `RangeIn`. Замкнутый круг: не идём и не бьём, а сервер видит вечное «в движении».
	//   Лечение: порог входа обязан покрывать и стоп-точку, и допуск прибытия маршрута.
	const float RangeIn  = FMath::Max(RangeU + TargetR, StopDist + kRouteArriveDist + 10.f);
	// ★ ГИСТЕРЕЗИС (живой лог 12:59: бродящий моб на границе радиуса вызывал чередование подход↔удар
	//   каждые 0.4 с; сервер видел нас «в движении» и НАВСЕГДА переставал отвечать на замахи —
	//   «персонаж херачит, HP не падает»). В бою держимся до заметно большего расстояния.
	float RangeOut = RangeIn * ChaseHysteresis;
	// ★★★ 15.10.2026 (волна mobpos_swing, `wave_PORT_mobpos_swing.md` §1.4): ГИСТЕРЕЗИС НЕ ШИРЕ ДОПУСКА СЕРВЕРА К 4028.
	//   NpcServer принимает продолжение (`Process_AttackedToMonster sub_45F3A0`, проверка `sub_42C4F0`) только если клетка
	//   моба внутри квадрата «клетка игрока ± (дальность оружия + 3 + радиус моба .ai+148)» — по Чебышёву, по СВОИМ клеткам.
	//   Порт держал бой до 1.6 × дальности (баллиста 16 клеток → ~26), то есть слал 4028, которые сервер отвергает кодом 1
	//   («You cannot attack due to the distance»), вместо того чтобы подойти. Эталон гистерезиса не знает вовсе (проверка
	//   дальности в тике замаха, `skill_buff.c:762-793`). Верх удержания — дальность оружия + 2 клетки + радиус капсулы цели
	//   (запас на округление клетки; радиус моба сервера .ai+148 клиенту неизвестен — берём капсулу); евклидова мера ≥
	//   чебышёвской, так что сервер такой удар примет. Пол — порог входа + 100 ед. (см. ниже). Ключ отката
	//   `asda.Combat.RangeOutServerCap 0`.
	if (RangeOutServerCapEnabled())
	{
		const int32 SrvTiles = (RangeU > 0.f) ? FMath::Max(1, FMath::RoundToInt(RangeU / 100.f)) : 1;
		// ★ Ревизия 15.10 (СРЕДН-1): радиус цели — ПОЛНЫЙ (у сервера допуск тоже растёт с радиусом моба .ai+148; прежний срез до
		//   100 ед. у крупных целей опускал удержание до самого порога входа), и пол гистерезиса — порог входа + 100 ед.:
		//   иначе у босса с капсулой ≥ 265 ед. удержание совпадало с входом и возвращалось «подход ↔ удар каждые 0.4 с»
		//   (живой лог 12:59, ради которого гистерезис и вводили). Пол всегда ниже серверного допуска: RangeIn ≈ дальность +
		//   радиус + 35, предел = дальность + 200 + радиус.
		const float ServerOutU = (float)(SrvTiles + 2) * 100.f + TargetR;
		//   Пол не выше прежнего 1.6×: у малых целей поведение то же, что до правки.
		RangeOut = FMath::Max(FMath::Min(RangeOut, RangeIn + 100.f), FMath::Min(RangeOut, ServerOutU));
	}
	LastCombatRangeOut = RangeOut;          // ★ отдаём ритму от анимации, чтобы не считать дважды
	LastCombatRangeRid = M->NetRenderId;
	const bool bInRange  = bInCombatStance ? (Dist <= RangeOut) : (Dist <= RangeIn);
	ASDA_CTRACE_V(EAsdaCT::Range, TEXT("тик боя rid=%d: дист %.0f, вход %.0f, удержание %.0f, %s → %s"), M->NetRenderId, Dist, RangeIn, RangeOut,
		bInCombatStance ? TEXT("в стойке (мера — удержание)") : TEXT("вне стойки (мера — вход)"), bInRange ? TEXT("в дальности") : TEXT("вне дальности"));
	// ★★★ 01.11.2026 [ОРИГИНАЛ] (`asda.Skill.ApproachOverAttack`, разбор у ключа в AsdaSkillUseSubsystem.cpp): нажатый навык вне своей
	//   дальности ставит эталонное состояние 102 ВМЕСТО атаки 5 — тик атаки не бьёт и сам не подходит, к цели ведёт подход навыка
	//   (`UAsdaSkillUseSubsystem::TickSkillApproach`). Подход снимается сам: прибыли (заявка 5256 → дальше гейт «навык занят» ниже),
	//   сменилась/пропала цель или 20 с таймаута — тогда бой продолжается обычным путём.
	if (AsdaSkillApproachOverAttackOn())
	{
		const UAsdaSkillUseSubsystem* SUa = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		if (SUa && SUa->HasSkillApproach())
		{
			ASDA_CTRACE_V(EAsdaCT::State, TEXT("тик боя rid=%d: идёт подход ради навыка (состояние 102) — замах и подход автоатаки ждут, дист %.0f"),
				M->NetRenderId, Dist);
			NextAttackTickTime = 0.0;
			return;
		}
	}
	// ★★★ 29.10.2026 (баг Charge: «после навыка персонаж побежал к цели вместо рывка») [ОРИГИНАЛ]: пока свой навык занят — заявка 25,
	//   показ 24, накопление 14 или ещё играет его клип — НЕ подходим. Эталон гонит тик атаки (и подход — состояние 5 / отложенное
	//   действие) только вне состояний навыка; живой журнал Player15 25.10 05:11:49: 5256 Charge → через 150 мс порт слал 4006 к цели
	//   (подход автоатаки вне дальности), и рывок превращался в бег. Ворота ниже (`bInRange`) этого случая не покрывали. Тот же сторож
	//   «занят дольше 6 с». Ключ отката `asda.Combat.ApproachWaitsSkill 0`.
	if (!bInRange && ApproachWaitsSkillOn())
	{
		const UAsdaSkillUseSubsystem* SU = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		const int32 CastState = SU ? SU->GetSelfCharState() : 0;
		const bool bCastBusy = SU && SU->GetActiveSkill() >= 0 && (CastState == 25 || CastState == 24 || CastState == 14);
		if (Ch->IsSkillAnimActive() || bCastBusy)
		{
			if (CastBusySinceS <= 0.0)
			{
				ASDA_CTRACE(EAsdaCT::State, TEXT("тик боя rid=%d: подход ждёт — свой навык занят (навык %d, состояние %d, клип навыка %s)"),
					M->NetRenderId, SU ? SU->GetActiveSkill() : -1, CastState, Ch->IsSkillAnimActive() ? TEXT("играет") : TEXT("нет"));
				CastBusySinceS = Now;
			}
			if (Now - CastBusySinceS < kCastBusyWatchdogS) { return; }
		}
		else if (CastBusySinceS > 0.0)
		{
			ASDA_CTRACE(EAsdaCT::State, TEXT("тик боя rid=%d: навык освободился через %.2f с — подход может продолжаться"), M->NetRenderId, Now - CastBusySinceS);
			CastBusySinceS = 0.0; bCastBusyWatchdogLogged = false;
		}
	}
	// Ожидание ответа сервера НЕ должно блокировать ПОДХОД (иначе «стоим и не догоняем»).
	if (bInRange && Now < NextAttackTickTime) { return; }
	if (bInRange)
	{
		// ★★★ 15.10.2026 (ритм атаки, дефект порта Д-3): пока СВОЙ НАВЫК занят (заявка 25 / показ 24 / накопление 14 или
		//   ещё играет его клип), замах не начинается и 4026/4028 не уходят — эталон гонит тик атаки только в состоянии 5,
		//   а каждая заявка навыка ставит 25 (skill_buff.c:2237 и ещё семь мест). Раньше цикл замаха продолжал жить, его
		//   конец клипа перебивал клип навыка и слал 4028 посреди навыка — раньше серверной `Skill.lst`. Проверяем каждый
		//   кадр (`UAsdaAttackRhythmSubsystem::NoteSkillStart` обнуляет `NextAttackTickTime`).
		//   ★ Ревизия НИЗК-2: состояние навыка учитывается только при активном навыке (`ActiveSkill ≥ 0`) — у навыка соулмейта
		//   5231 и у 14 после 5270 подсистема навыков оставляет 25/14 без сторожа, и замах стоял бы вечно. Плюс свой сторож:
		//   «навык занят» дольше 6 с (самый длинный Skill.lst — 4000 мс, плюс клип) — не верим и продолжаем бой.
		if (UAsdaAttackRhythmSubsystem::IsEnabled() && RhythmFromAnimEnabled())
		{
			const UAsdaSkillUseSubsystem* SU = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
			const int32 CastState = SU ? SU->GetSelfCharState() : 0;
			const bool bCastBusy = SU && SU->GetActiveSkill() >= 0 && (CastState == 25 || CastState == 24 || CastState == 14);
			if (Ch->IsSkillAnimActive() || bCastBusy)
			{
				if (CastBusySinceS <= 0.0)
				{
					ASDA_CTRACE(EAsdaCT::State, TEXT("тик боя rid=%d: замах ждёт — свой навык занят (навык %d, состояние %d, клип навыка %s)"),
						M->NetRenderId, SU ? SU->GetActiveSkill() : -1, CastState, Ch->IsSkillAnimActive() ? TEXT("играет") : TEXT("нет"));
					CastBusySinceS = Now;
				}
				if (Now - CastBusySinceS < kCastBusyWatchdogS) { NextAttackTickTime = 0.0; return; }
				if (!bCastBusyWatchdogLogged)
				{
					ASDA_CTRACE(EAsdaCT::State, TEXT("тик боя rid=%d: навык «занят» дольше %.0f с (состояние %d) — сторож: бой продолжается"),
						M->NetRenderId, kCastBusyWatchdogS, CastState);
					bCastBusyWatchdogLogged = true;
					UE_LOG(LogAsdaPC, Warning, TEXT("[ритм] навык «занят» дольше %.0f с (состояние %d, клип %s) — сторож: бой продолжается"),
						kCastBusyWatchdogS, CastState, Ch->IsSkillAnimActive() ? TEXT("играет") : TEXT("нет"));
				}
			}
			else
			{
				if (CastBusySinceS > 0.0)
				{
					ASDA_CTRACE(EAsdaCT::State, TEXT("тик боя rid=%d: навык освободился через %.2f с — замах может продолжаться"), M->NetRenderId, Now - CastBusySinceS);
				}
				CastBusySinceS = 0.0; bCastBusyWatchdogLogged = false;
			}
		}
		// ★ ВХОД В БОЙ: СНАЧАЛА ОСТАНОВИТЬСЯ (локально + пакет 4008 ПРИНУДИТЕЛЬНО), затем удар.
		//   Стоп нужен ПЕРЕД КАЖДЫМ возвратом из подхода: сервер тихо отбрасывает атаку «на бегу»
		//   (гейт движения) — именно так бой и «залипал» после первой же догонялки.
		const FVector2D Cur = Ch->GetGamePosXZ();
		// ★★★ 27.09.2026 (эталонная схема хода): вход в бой начинается с остановки 4008, а эталонный тик своего
		//   персонажа, пока подход-4006 ждёт ответа (бит 2), стоп НЕ шлёт — доигрывает шаг
		//   (`Player_ExecuteSkillOnTarget@0x9F16D0`: `(g_nGameFlags & 2) ? ApplyMoveState : Net_SendActionPacket`).
		//   Иначе опоздавший грант подхода увёл бы уже остановленного персонажа после 4008.
		if (!bInCombatStance && IsEtalonSpeedSync() && Net->IsMoveRequestInFlight())
		{
			ASDA_CTRACE_V(EAsdaCT::Range, TEXT("тик боя rid=%d: в дальности, но подход-4006 ждёт 4007 (бит 2) — доигрываем шаг, стоп позже"), M->NetRenderId);
			return;
		}
		if (!bInCombatStance)
		{
			ASDA_CTRACE(EAsdaCT::Range, TEXT("ВОШЛИ в дальность rid=%d '%s': дист %.0f ≤ вход %.0f (удержание %.0f, стоп-точка %.0f, оружие %.0f юн, радиус цели %.0f, подтип %d)"),
				M->NetRenderId, *M->MobName, Dist, RangeIn, RangeOut, StopDist, RangeU, TargetR, WeaponSub);
			ASDA_CTRACE(EAsdaCT::Move, TEXT("остановка перед боем: маршрут снят, локальный бег погашен в (%.0f,%.0f), стоп 4008"), Cur.X, Cur.Y);
			CancelRoute();                                 // в бою маршрут не ведём (стоим и бьём)
			Ch->NetSnap(Cur);                            // погасить локальный бег (клип → idle)
			// ★ Гейт `bActionPending` — эталонный, обхода нет (T2): подход к цели идёт пакетами 4006,
			//   значит флаг взведён и стоп уйдёт; если мы стояли на месте, серверный флаг движения
			//   `CUser+51` не взводился и стоп не нужен (`Net_SendActionPacket@0x9F9C00`).
			Net->SendStop4008(Cur.X, Cur.Y);
			bInCombatStance = true;
		}
		if (!bCombatStarted)
		{
			// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Combat.RestartSeriesGate`, разбор у ключа): серия не начинается раньше серверных часов —
			//   стойка лицом к цели, тик вернётся к сроку (подход тиком не гейтится: ожидание стоит только в дальности).
			if (const double WaitRestart = AsdaRestartSeriesWaitS(GetGameInstance()); WaitRestart > 0.0)
			{
				ASDA_CTRACE(EAsdaCT::Send, TEXT("заявка 4026 rid=%d ЖДЁТ серверных часов ещё %.0f мс (перезапуск серии раньше прошлого удара сервер не примет; asda.Combat.RestartSeriesGate)"),
					M->NetRenderId, WaitRestart * 1000.0);
				UE_LOG(LogAsdaPC, Log, TEXT("[бой] 4026 по rid=%d ждёт серверных часов %.0f мс (перезапуск серии)"), M->NetRenderId, WaitRestart * 1000.0);
				Ch->StopMoving();
				Ch->FaceGameDir(DstG - Cur);
				Ch->AttackFaceTarget = M;
				Ch->SetCombatStance(true);
				CombatStanceUntil = Now + CombatStanceHold;
				NextAttackTickTime = Now + WaitRestart;
				return;
			}
			// ★★★ ВХОД В БОЙ ОБНУЛЯЕТ СВОЮ ОЧЕРЕДЬ РЕЗУЛЬТАТОВ — 1:1 эталон: сразу после отправки
			//   заявки `netSabzil_SendActionRaw` (`network.c:5219-5223`) сбрасывает счётчики очереди
			//   у локального игрока. Без этого хвост прошлого боя уезжает в новый и сдвигает всю
			//   серию цифр относительно ударов.
			if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
			{
				WS->ClearOwnDamageQueuePublic(TEXT("вход в бой, заявка 4026"));
			}
			{
				const FVector2D PktPos = AttackPacketPos(Cur, DstG);   // ★ mobpos_swing: своя позиция (эталон, netcap)
				ASDA_CTRACE(EAsdaCT::Send, TEXT("заявка 4026 (начало серии) rid=%d '%s' HP %d/%d: позиция в пакете (%.0f,%.0f) [%s], дист %.0f; своя очередь результатов очищена"),
					M->NetRenderId, *M->MobName, M->CurHp, M->MaxHp, PktPos.X, PktPos.Y,
					CVarAsdaAttackPosSelf.GetValueOnGameThread() != 0 ? TEXT("своя") : TEXT("цели"), Dist);
				Net->SendAttackMob4026(M->NetRenderId, PktPos.X, PktPos.Y);
			}
			bCombatStarted = true;
			// ★ 15.10.2026: новая серия у сервера — первый 4028 не раньше dur(шаг 0) от этой 4026. Идущий цикл замаха
			//   (смена цели без «взвода») пересчитывает темп текущего отрезка под новые часы.
			if (Ch->IsAttackLoopAlive()) { Ch->BeginSendSegment(TEXT("4026 новой серии")); }
			// ★ 17.10.2026 (ревизия, п.5): окно «наш замах в полёте» взводится СРАЗУ по отправке 4026 (оценка из текущего
			//   замаха) и уточняется после старта замаха — флаг в поле контроллера: тик ниже может уйти в ранний выход
			//   ожидания серверных часов, и замах тогда стартует в следующем тике (локальный флаг терялся).
			bArmImpactOnSwingStart = true;
			if (UAsdaWorldSubsystem* WS26 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
			{
				WS26->ArmOurImpactWindowFromSend(Ch, M, WeaponSub, TEXT("заявка 4026"));
			}
		}
		else if (!RhythmFromAnimEnabled())
		{
			// ★ Ревизия НИЗК-5: отладочная пара «ритм от ответа сервера» + серверные часы — пока часы не позволяют, не слать и
			//   не ждать ответа на неотправленный удар (иначе 2.5 с простоя `CombatAckTimeout`); тик вернётся к дедлайну.
			const UAsdaAttackRhythmSubsystem* RhL = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr;
			const double WaitL = RhL ? RhL->SecondsUntilNext4028() : 0.0;
			if (WaitL > 0.0)
			{
				ASDA_CTRACE(EAsdaCT::Send, TEXT("ритм «по ответу»: 4028 rid=%d ждёт серверных часов ещё %.0f мс"), M->NetRenderId, WaitL * 1000.0);
				NextAttackTickTime = Now + WaitL; return;
			}
			const FVector2D PktPos = AttackPacketPos(Cur, DstG);   // ★ mobpos_swing: своя позиция (эталон, netcap)
			ASDA_CTRACE(EAsdaCT::Send, TEXT("ритм «по ответу» (asda.Combat.RhythmFromAnim 0): удар 4028 rid=%d из тика боя, позиция (%.0f,%.0f), дист %.0f"),
				M->NetRenderId, PktPos.X, PktPos.Y, Dist);
			Net->SendAttackNext4028(M->NetRenderId, PktPos.X, PktPos.Y);
		}
		// ★ При ритме от анимации следующий удар шлёт `OnSwingCycleCompleted` — по завершении
		//   прохода клипа, как в оригинале. Здесь второй раз слать нельзя: получилось бы два удара
		//   за проход и та самая «дёрганость», от которой уходим.
		// ★ В БОЮ СТОИМ НА МЕСТЕ: гасим остаточное локальное движение (иначе персонаж «дрейфует» за
		//   бродящим мобом, сервер видит движение и перестаёт отвечать на замахи — юзер 2026-07-29).
		Ch->StopMoving();
		// Разворот к цели (эталон: атакующий смотрит на цель). Непрерывный доворот за бродящей целью —
		//   1:1 objAI_FaceTargetByUserIdx@0xA23350 (зовётся и для локального, entity_char.c:7688) — в
		//   Tick павна по AttackFaceTarget.
		Ch->FaceGameDir(DstG - Cur);
		Ch->AttackFaceTarget = M;
		// ★ РИТМ ЭТАЛОНА: следующий замах шлётся при ВХОДЕ в атак-состояние, т.е. ПО ОТВЕТУ сервера
		//   (4029/4027 с очередью ударов). Живой лог: ответы идут раз в ~4 с (в очереди 4 удара) —
		//   поэтому долбить по длине клипа нельзя («бьёт слишком часто»). Ждём ack; таймаут — страховка.
		// ★★★ 15.10.2026 (ритм атаки): замах ВОЗОБНОВЛЯЕТСЯ (после навыка, после погони) только когда до серверного
		//   «можно» осталось не больше первого отрезка клипа на скорости 0.8 — иначе первый удар нового замаха пришёл бы
		//   раньше часов, и анимация показала бы взмах без урона. Пока ждём — боевая стойка, тик каждый кадр.
		if (bCombatStarted && UAsdaAttackRhythmSubsystem::IsEnabled() && RhythmFromAnimEnabled() && !Ch->IsAttackLoopAlive())
		{
			const UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr;
			const double WaitS = Rh ? Rh->SecondsUntilNext4028() : 0.0;
			const float FirstSegS = Ch->FirstSendSegmentClipSec(WeaponSub);
			if (FirstSegS > 0.f && WaitS > (double)FirstSegS / 0.8)
			{
				ASDA_CTRACE_V(EAsdaCT::Swing, TEXT("возобновление замаха rid=%d ждёт часов: до «можно» %.0f мс > первый отрезок %.0f мс / 0.8 — стойка"),
					M->NetRenderId, WaitS * 1000.0, FirstSegS * 1000.0);
				Ch->SetCombatStance(true);
				CombatStanceUntil = Now + CombatStanceHold;
				NextAttackTickTime = 0.0;
				return;
			}
		}
		bWaitingCombatAck = true;
		NextAttackTickTime = Now + CombatAckTimeout;
		// ★ ЛОКАЛЬНЫЙ ЗАМАХ — эталонная модель «взвод один раз → цикл» (юзер 2026-08-03: было
		//   «взвод-атака-взвод»: каждый 4028 звал PlayAttackClip и перезапускал клип с 0 кадра).
		//   Эталон: state 5 у своего ставится ПРЯМОЙ записью без сброса клипа (input.c:1647,
		//   Player_UpdateAutoActionState entity_char.c:24048); клип стартует с 0 один раз, дальше на
		//   каждом завершении перематывается на WindowTime (Player_ProcessAttackTick LABEL_155).
		//   Удержание бесконечное (эталон 52121==100, таймаута нет) — до StopAttackAnim
		//   (смерть/метка-труп цели, отмена, наша смерть). Возврат >0 только на первом входе.
		const float ClipLen = Ch->StartAttackSwing(WeaponSub, /*HoldSec=*/0.f);
#if ASDA_COMBAT_TRACE
		if (ClipLen > 0.f)
		{
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("замах ВЗВОД rid=%d '%s' HP %d/%d: клип %d, длина %.2f с, дист %.0f (удержание %.0f); страховка ответа %.1f с"),
				M->NetRenderId, *M->MobName, M->CurHp, M->MaxHp, AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), ClipLen, Dist, RangeOut, CombatAckTimeout);
		}
		else
		{
			ASDA_CTRACE_V(EAsdaCT::Swing, TEXT("замах rid=%d: цикл продолжается (клип %d не перезапущен), HP цели %d/%d"),
				M->NetRenderId, AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), M->CurHp, M->MaxHp);
		}
#endif
		// ★ 17.10.2026: заявка 4026 ушла и замах начат — окно «наш замах в полёте» до оценки попадания из замаха: серверные
		//   HP/смерть цели (4015/4017), обогнавшие ответ 4029, ждут выстрела (разбор у `UAsdaWorldSubsystem::ArmOurImpactWindow`).
		if (bArmImpactOnSwingStart)
		{
			bArmImpactOnSwingStart = false;
			if (UAsdaWorldSubsystem* WSa = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
			{
				WSa->ArmOurImpactWindowFromSend(Ch, M, WeaponSub, TEXT("заявка 4026, старт замаха"));
			}
		}
		// ★ БОЕВАЯ СТОЙКА взводится ЗАМАХОМ, а не кликом (эталон CChar_SetState(char,0,0,20) на возврате
		//   в покой ПОСЛЕ боевого действия; там же fAutoActionStartTime = timeGetTime()). Кулдаун
		//   ПЕРЕВЗВОДИТСЯ каждым замахом — пока бьём, он не истекает.
		Ch->SetCombatStance(true);
		CombatStanceUntil = Now + CombatStanceHold;
		// Рантайм-доказательство модели: «взвод» печатается ровно один раз на бой, дальше — «цикл».
		if (ClipLen > 0.f)
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[бой] атака: клип=%d цель=%d hp=%d/%d длина=%.2fс (ВЗВОД, первый замах боя)"),
				AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), M->NetRenderId, M->CurHp, M->MaxHp, ClipLen);
		}
		else
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[бой] атака: клип=%d цель=%d hp=%d/%d (цикл продолжается, клип НЕ перезапущен)"),
				AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), M->NetRenderId, M->CurHp, M->MaxHp);
		}
	}
	else
	{
		// Вышли из боевой стойки — при возврате понадобится новый стоп (иначе удары «на бегу» глохнут).
		if (bInCombatStance)
		{
			ASDA_CTRACE(EAsdaCT::Range, TEXT("ВЫШЛИ из дальности rid=%d '%s': дист %.0f > удержание %.0f — замах снят, догон"),
				M->NetRenderId, *M->MobName, Dist, RangeOut);
		}
		bInCombatStance = false;
		// ★ Эталон: подход к убежавшей цели = ВЫХОД из state 5 в состояние подхода (CharFx_AutoSabzil_State
		//   (22,0,0) в Player_UpdateAutoActionState). Цикл атаки обязан кончиться здесь, иначе с бесконечным
		//   удержанием персонаж бежал бы в атак-клипе. Возврат в дальность = новый вход («взвод» снова —
		//   как в эталоне, где повторный вход в state 5 идёт через сброс клипа).
		Ch->StopAttackAnim();
		if (Now < NextApproachTime) { return; }   // троттл подхода (пакеты), но не боевого гейта
		// ★ 27.09.2026 (эталонная схема хода): перезапрос подхода эталон гейтит битом 2 — `(g_nGameFlags & 2) == 0 &&
		//   состояние == 100` (`Player_ExecuteSkillOnTarget`, game.c:68757): пока прошлый 4006 без ответа, новый не шлёт.
		if (IsEtalonSpeedSync() && Net->IsMoveRequestInFlight())
		{
			ASDA_CTRACE_V(EAsdaCT::Move, TEXT("подход rid=%d: прошлый 4006 ещё без ответа (бит 2) — перезапрос не шлём"), M->NetRenderId);
			return;
		}
		// ★ ДИАГНОСТИКА подход-цикла (баг «дёргается вместо атаки»): раз в подход печатаем параметры
		//   дальности — если вход в бой не наступает, из лога видно, какое слагаемое врёт.
		UE_LOG(LogAsdaPC, Log, TEXT("[бой-подход] rid=%d dist=%.0f rangeIn=%.0f rangeOut=%.0f stop=%.0f rangeU=%.0f tgtR=%.0f weapSub=%d"),
			M->NetRenderId, Dist, RangeIn, RangeOut, StopDist, RangeU, TargetR, WeaponSub);
		// ★ Подход: цель движения — ТОЧКА НА ГРАНИЦЕ ДАЛЬНОСТИ (не центр моба, иначе перс забегает
		//   внутрь); move-пакет с renderId цели @25 (эталон Combat_SendNormalAttack).
		// Цель ушла — ждать ответ сервера смысла нет: возобновляем бой сразу по прибытии.
		bWaitingCombatAck = false;
		NextAttackTickTime = 0.0;
		const FVector2D Cur = Ch->GetGamePosXZ();
		FVector2D Dir = DstG - Cur;
		const float D = Dir.Size();
		FVector2D Stop = DstG;
		if (D > KINDA_SMALL_NUMBER) { Dir /= D; Stop = DstG - Dir * FMath::Min(StopDist, D); }
		// ★ АНТИЧИТ-ГАРД (подтверждение юзера: кик 19:26 = античит): НЕ слать 4006 с нулевым
		//   перемещением — прежний подход-цикл долбил cur==dst каждые 0.4с и кормил Hack Position-счётчик.
		// ★ Подход идёт МАРШРУТОМ (обход препятствий): при свободной прямой это ровно один отрезок,
		//   как раньше, а за скалой/стеной — обход вместо упора в геометрию.
		// ★ Не начинать движение, которое завершится в тот же кадр: маршрут считает точку
		//   достигнутой с допуском `kRouteArriveDist`, и приказ короче этого допуска даёт лишь
		//   пару «поехали — приехали» в сеть (см. разбор порога входа выше).
		const float ToStop = FVector2D::Distance(Cur, Stop);
		if (ToStop > FMath::Max(kMoveMinSendDist, kRouteArriveDist))
		{
			// Повтор каждые 0.4 с: первая строка по цели в цикле — уровень 1, повторы — уровень 2.
			ASDA_CTRACE_IF(AsdaCtOnceFirst(GAsdaCtApproachOnce, M->NetRenderId), EAsdaCT::Move,
				TEXT("подход к rid=%d '%s': дист %.0f > вход %.0f — бег от (%.0f,%.0f) к стоп-точке (%.0f,%.0f), до неё %.0f (стоп-дист %.0f, цель в (%.0f,%.0f))"),
				M->NetRenderId, *M->MobName, Dist, RangeIn, Cur.X, Cur.Y, Stop.X, Stop.Y, ToStop, StopDist, DstG.X, DstG.Y);
#if ASDA_COMBAT_TRACE
			AsdaCtOnceMark(GAsdaCtApproachOnce, M->NetRenderId);
#endif
			// ★ 27.09.2026 (К1): это ПОДХОД — при эталонной схеме отрезок в своей клетке уходит без 4006 (`bRouteApproach`).
			StartRoutedMove(Stop, M->NetRenderId, /*bApproach=*/true);
		}
		else
		{
			UE_LOG(LogAsdaPC, Verbose,
				TEXT("[бой-подход] до стоп-точки %.0f — короче допуска прибытия, приказ не шлём"), ToStop);
			ASDA_CTRACE_V(EAsdaCT::Move, TEXT("подход к rid=%d: до стоп-точки %.0f — короче допуска прибытия %.0f, приказ не шлём (дист %.0f, вход %.0f)"),
				M->NetRenderId, ToStop, FMath::Max(kMoveMinSendDist, kRouteArriveDist), Dist, RangeIn);
		}
		NextApproachTime = Now + 0.4;   // рефреш подхода (цель может брести)
	}
}

// ── ★ ПОДБОР ЛУТА (порт netHandleTargetObjectClick@0x9968F0 + state 103): радиус подбора =
//   ОДНА ЯЧЕЙКА террейна (тайл 100 юнитов). В ячейке — 5012 сразу; далеко — 4006-подход к центру
//   тайла предмета + отложенная отправка по прибытии (LootTick). Повторный клик в пути — игнор. ──
void AAsdaPlayerController::StartLootPickup(const FIntPoint& LootTile)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Ch || !Net || !Net->bInGameConnected) { return; }
	// ★ Мёртв → подбор запрещён (эталон item_inventory.c:22254: !isMovementActionState).
	//   Сервер и так отказал бы (IsCheck_CharFlag → 6001 причина 1 / 5013 result=0) — не шумим в сеть.
	if (IsSelfDead()) { bLootPending = false; return; }
	const FVector2D CurG = Ch->GetGamePosXZ();
	const FIntPoint MyTile = AsdaCoord::GameToTile(CurG);   // ★ Аудит №3 (B2)
	if (MyTile == LootTile)
	{
		bLootPending = false;
		Net->SendItemPick5012(MyTile, LootTile);
		return;
	}
	// ★ 22.10.2026 (ревизия стана, ВАЖНО-1): под обездвиживанием подбор с подходом не начинается и намерение НЕ защёлкивается —
	//   иначе `bLootPending` переживал стан, маршрут молча снимался, и повторный клик по тому же луту ничего не делал. Плашка 1019
	//   (эталон: клик по миру под состоянием — `Input_WorldClickHandler`, input.c:545). Подбор со своей клетки (выше) — без хода, не гейтится.
	if (IsMoveInputLockedByState())
	{
		if (bLootPending && PendingLootTile == LootTile) { bLootPending = false; }
		AsdaPlankCannotMove(GetGameInstance());
		UE_LOG(LogAsdaPC, Log, TEXT("[лут] обездвижен — подход к (%d,%d) не начат, плашка 1019"), LootTile.X, LootTile.Y);
		return;
	}
	if (bLootPending && PendingLootTile == LootTile) { return; }   // уже идём к этому дропу (эталон)
	// ★★★ 26.10.2026 (navattack2) [ИСПРАВЛЕНИЕ] клик по луту ВНЕ своей клетки посреди атаки — выход из атаки сразу (та же точка, что у
	//   автопути: `AsdaExitCombatForAutoPath`, ключ `asda.Move.AutoPathExitsCombat`). Без этого подход шёл в клипе атаки, конец прохода
	//   слал 4028 по прежнему мобу, а за удержанием дальности тик боя уводил персонажа обратно к мобу (тот же дефект, что у навигации).
	//   [ОРИГИНАЛ] `netHandleTargetObjectClick@0x9968F0` (network.c:4084-4126): своя клетка — 5012 сразу, состояние не трогается (у нас —
	//   ветка выше, атаку не снимает); чужая клетка в состоянии атаки 5 — отложенное действие 103 (`sub_925EA4 → sub_996CE0`:
	//   +52121 = 103, +56642 = id предмета, +52124…52129 своя точка и точка предмета). Оно исполняется в точке отправки удара вместо 4028
	//   (`Player_UpdateAutoActionState@0xA249A0` case 0x67, entity_char.c): цель атаки (+0x32DFC) = −1, подбираемый = +56642; предмета
	//   нет — звук 214 и нотис 1010; далеко — `CharFx_AutoSabzil_State(0,0,10)` и 4006 с целью −1 в точку предмета (подбор по прибытии);
	//   рядом — 4008 (если шёл) и 5012. То есть оригинал тоже выходит из атаки и идёт за лутом, но на точке удара; мы — сразу, как у
	//   клика по земле порта. Вне атаки эталон шлёт 4006 сразу (+208472 = id предмета) — у нас прежний путь ниже.
	AsdaExitCombatForAutoPath(this, TEXT("клик по луту"));
	PendingLootTile = LootTile;
	bLootPending = true;
	// Подход: цель = центр тайла предмета (обычный move; сервер ведёт как простое движение).
	RequestMoveTo(AsdaCoord::TileCenterGame(LootTile));   // ★ Аудит №3 (B2): центр тайла
}

// ★ 09.10.2026 (автолопата): порт `itemPickupGroundItem@0xC54ED0` (item_inventory.c:22223-22283), разбор — у объявления.
//   Эталон: `if (!sub_937F2D(&g_InventoryBag, 1) && !isMovementActionState(LP))` → позиция своего (`Class_GetVec3_86`) →
//   `sub_928C3F(g_pBillboardScene, pos)` → при находке `byte_2C715E4 = byte_2C715E8 = 1` и
//   `SendItemPickPacket(свой тайл X, Z, тайл предмета X, Z)`. Хода к предмету НЕТ (в отличие от клика по луту).
bool AAsdaPlayerController::PickupGroundItemEtalon(const TCHAR* Why)
{
	AAsdaPlayerCharacter* Ch = GetChar();   // свой персонаж
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Ch || !Net || !WS) { return false; }
	// `isMovementActionState` (entity_char.c:53) — миснейм: состояния 7/8/9 (сидение) и 6 (смерть).
	const int32 St = KbEtalonActionState();   // эталонное `+47551` своего
	if (St == 6 || St == 7 || St == 8 || St == 9) { return false; }
	const AAsdaLootActor* L = WS->FindFirstLootWithin(Ch->GetActorLocation(), 300.f);   // `sub_F2F110`: d² < 90000
	if (!L) { return false; }
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `itemPickupGroundItem` (item_inventory.c:22268) — предмет найден → байты 76/80.
	AsdaTutorial::Mark(this, EAsdaTutorialFlag::TargetObject);
	AsdaTutorial::Mark(this, EAsdaTutorialFlag::ItemPickup);
	const FIntPoint MyTile = AsdaCoord::GameToTile(Ch->GetGamePosXZ());   // свой тайл (ratioIfFieldSet / helper_scaleIfNonzero)
	const bool bSent = Net->SendItemPick5012(MyTile, L->Tile);
	UE_LOG(LogAsdaPC, Log, TEXT("[подбор рядом] %s: предмет на тайле (%d,%d), свой тайл (%d,%d) — 5012 %s"),
		Why ? Why : TEXT("?"), L->Tile.X, L->Tile.Y, MyTile.X, MyTile.Y, bSent ? TEXT("ушёл") : TEXT("не ушёл"));
	return bSent;
}

void AAsdaPlayerController::EngagePlayerFromClick(AActor* Pl)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = GetChar();
	AAsdaPlayerCharacter* Tgt = Cast<AAsdaPlayerCharacter>(Pl);
	if (!Net || !WS || !Ch || !Tgt || !Net->bInGameConnected)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик: нет сети/мира/персонажа/цели-персонажа — атаки нет"));
		return;
	}
	if (!Net->IsSelfPvpOn())
	{
		ASDA_CTRACE(EAsdaCT::Pvp, TEXT("PvP-клик по '%s': свой PvP-режим выключен — ветки атаки нет (эталон)"), *Tgt->GetName());
		return;   // [ОРИГИНАЛ] `localPlayer[1].buffFxSlots[160] == 1` — иначе ветки нет вовсе
	}
	// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущий PvP-атаку не начинает — МОЛЧА. Ключ `asda.Swim.ActionGate`.
	if (AsdaSwimActionLocked(Ch))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик по '%s': персонаж плывёт (asda.Swim.ActionGate) — атаки нет"), *Tgt->GetName());
		return;
	}
	// Гейты эталона по порядку (input.c:1556–1680). Цвета: 111204/110707 — −1 (белый), 1161/1004 — −11514 (золотой).
	const UAsdaSkillUseSubsystem* SU = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
	if (SU && SU->IsAutoShovelEquipped())
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик: надет авто-бур — нотис 111204, атаки нет"));
		WS->PostPvpNotice(111204, UAsdaChatSubsystem::ColWhite(), TEXT("клик PvP: авто-бур")); return;
	}
	{
		const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		const FAsdaItemRecord* R = (IT && Net->SelfWeaponItemId > 0) ? IT->GetItem(Net->SelfWeaponItemId) : nullptr;
		if (R && R->ItemType == 10 && R->ItemSubType == 1)   // `playerCheckEquippedItemType10` — удочка (тот же признак, что у навыков)
		{
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик: надета удочка (itemId=%d) — нотис 110707, атаки нет"), Net->SelfWeaponItemId);
			WS->PostPvpNotice(110707, UAsdaChatSubsystem::ColWhite(), TEXT("клик PvP: удочка"));
			return;
		}
	}
	const int32 TU = WS->FindUserIdxByActor(Tgt);   // userIdx цели (i16 в 4201) — НЕ charId
	if (TU < 0 || Net->IsSelfUserIdx(TU) || Tgt == Ch)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик: userIdx цели %d — %s, атаки нет"), TU, TU < 0 ? TEXT("неизвестен") : TEXT("это свой персонаж"));
		return;   // `localPlayer != targetChar`
	}
	if (const UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>(); Fx && Fx->HasControlState(Fx->SelfUserIdx()))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик по userIdx=%d: свой персонаж обездвижен — нотис 1161, атаки нет"), TU);
		WS->PostPvpNotice(1161, UAsdaChatSubsystem::ColGold(), TEXT("клик PvP: обездвижен"));
		return;
	}
	if (Tgt->IsIncapacitated())
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик по userIdx=%d: цель мертва (состояние 6) — молча"), TU);
		return;   // цель мертва (состояние 6) — молча
	}
	// Уже бьём эту цель (состояния 5/15/1 с той же целью) — молча, серия идёт.
	if (bPvpAttack && bAutoAttack && PvpTargetUserIdx == TU)
	{
		ASDA_CTRACE(EAsdaCT::Pvp, TEXT("PvP-клик по userIdx=%d: серия по этой цели уже идёт — новой 4201 нет"), TU);
		return;
	}
	// Открытые окна — молча (`helper_getField2075(g_pShopWnd)`, `tileInteract_isControlVisible(g_pItemMainWnd)`,
	//   `isBoundControlVisible(g_pItemQtyMakeDlg)`, `CObject_GetField166(g_pHairShopWnd)`, `CObject_GetField254(g_pInventoryWnd)`,
	//   `CObject_GetField5581(g_pWarehouseWnd)`).
	{
		const UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
		const UAsdaCraftWindowSubsystem* Cw = GI->GetSubsystem<UAsdaCraftWindowSubsystem>();
		const UAsdaCraftMakeWindowSubsystem* Cm = GI->GetSubsystem<UAsdaCraftMakeWindowSubsystem>();
		const UAsdaHairShopSubsystem* Hs = GI->GetSubsystem<UAsdaHairShopSubsystem>();
		const UAsdaBoosterPanelSubsystem* Bo = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>();
		const UAsdaWarehouseSubsystem* Wh = GI->GetSubsystem<UAsdaWarehouseSubsystem>();
		if ((Ps && Ps->IsOpen()) || (Cw && Cw->IsOpen()) || (Cm && Cm->IsOpen()) || (Hs && Hs->IsSessionOpen())
			|| (Bo && Bo->IsWindowOpen()) || (Wh && Wh->IsRecallActive()))
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[PvP] клик по игроку %d: открыто окно (магазин/ремесло/парикмахерская/бустер/свиток склада) — молча (эталон)"), TU);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик по userIdx=%d: открыто окно (магазин/ремесло/парикмахерская/бустер/свиток склада) — молча"), TU);
			return;
		}
	}
	float RangeU = AttackRange; int32 WeaponSub = -1;
	AsdaPvpSelfWeapon(GI, Net, AttackRange, RangeU, WeaponSub);
	if (Net->SelfWeaponItemId <= 0)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик по userIdx=%d: нет оружия — звук 214, нотис 1004"), TU);
		// [ОРИГИНАЛ] `equippedWeaponType == -1` → звук 214 и нотис 1004.
		if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound2DById(214); }
		WS->PostPvpNotice(1004, UAsdaChatSubsystem::ColGold(), TEXT("клик PvP: нет оружия"));
		return;
	}
	if (AsdaGmHide::BlockCombatAction(GI))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP-клик по userIdx=%d: GM-«скрыться» — бой запрещён (молча)"), TU);
		return;   // GM-«скрыться»: бой запрещён (молча, как у моба)
	}
	// [ПОРТ] Эталон в занятых состояниях (22/15/5/24/25/14/27/13, состояние 547) ставит атаку в отложенное действие
	//   (`sub_928AFA(…, 8)`); у порта очереди отложенных действий нет — заявка уходит сразу (сервер сам ответит отказом кода 4).
	ClearPendingNpcTalk(TEXT("начат PvP-бой"));
	if (bAutoAttack && !bPvpAttack)
	{
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("PvP-клик: шёл бой с мобом — его замах остановлен"));
		Ch->StopAttackAnim();   // шёл бой с мобом — его замах кончается
		// ★ Ревизия 25.10 (фантомный урон, В-1): серия боя с мобом брошена — 4028 по ней больше не уйдёт.
		if (UAsdaAttackRhythmSubsystem* RhA = GI->GetSubsystem<UAsdaAttackRhythmSubsystem>()) { RhA->NoteSeriesAbandoned(TEXT("PvP-клик посреди боя с мобом")); }
	}
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Combat.RestartSeriesGate`, разбор у ключа): 4201 начинает серию, как 4026 — не раньше
	//   серверных часов. Заявка откладывается на остаток (свой таймер `GAsdaDeferred4201Timer`, ревизия М-4: общий таймер точки
	//   отправки 4203/4028 её не перезапишет); к сроку клик повторяется, если игрок за это время не сдвинулся, цель та же и другой
	//   бой не начат. Повторный клик по цели идущей PvP-атаки ничего не перезапускает.
	if (const double WaitPvp = AsdaRestartSeriesWaitS(GI); WaitPvp > 0.0)
	{
		if (bPvpAttack && PvpTargetUserIdx == TU)
		{
			ASDA_CTRACE(EAsdaCT::Pvp, TEXT("PvP-клик по userIdx=%d: атака по нему уже идёт, серверные часы не истекли — перезапуска нет"), TU);
			return;
		}
		ASDA_CTRACE(EAsdaCT::Pvp, TEXT("PvP-клик по userIdx=%d: заявка 4201 ЖДЁТ серверных часов ещё %.0f мс (asda.Combat.RestartSeriesGate)"), TU, WaitPvp * 1000.0);
		UE_LOG(LogAsdaPC, Log, TEXT("[PvP] 4201 по %d ждёт серверных часов %.0f мс (перезапуск серии)"), TU, WaitPvp * 1000.0);
		if (bAutoAttack && !bPvpAttack) { bAutoAttack = false; bCombatStarted = false; bInCombatStance = false; }   // бой с мобом кончен кликом
		// ★ Ревизия М-4: PvP-атака по ПРЕЖНЕЙ жертве кончается сразу — флаги актуальны на всё ожидание, замах прежней не идёт.
		if (bPvpAttack) { AsdaStopPvpAttackForNewAttack(this, TEXT("смена цели PvP (4201 по новой ждёт серверных часов)")); }   // ★ 01.11: без 400
		// Ожидание — в боевой стойке лицом к новой цели (покадровый доворот за ней).
		Ch->FaceGameDir(Tgt->GetGamePosXZ() - Ch->GetGamePosXZ());
		Ch->AttackFaceTarget = Tgt;
		Ch->SetCombatStance(true);
		CombatStanceUntil = FPlatformTime::Seconds() + CombatStanceHold + WaitPvp;
		TWeakObjectPtr<AActor> WeakPl = Pl;                    // цель клика
		const FVector AtLoc = Ch->GetActorLocation();          // где стоял игрок при клике
		GetWorldTimerManager().SetTimer(GAsdaDeferred4201Timer, FTimerDelegate::CreateWeakLambda(this, [this, WeakPl, AtLoc, TU]()
		{
			AActor* P = WeakPl.Get();
			const AAsdaPlayerCharacter* C = GetChar();
			const bool bMoved = !C || FVector::Dist2D(C->GetActorLocation(), AtLoc) > 30.f || HasRoute() || IsMovingNow();
			if (!P || TargetActor.Get() != P || bMoved || (bAutoAttack && !bPvpAttack))
			{
				ASDA_CTRACE(EAsdaCT::Pvp, TEXT("отложенная заявка 4201 снята: %s"), !P ? TEXT("цели нет") : (TargetActor.Get() != P ? TEXT("цель сменилась")
					: (bMoved ? TEXT("игрок сдвинулся") : TEXT("начат бой с мобом"))));
				return;
			}
			if (bPvpAttack && PvpTargetUserIdx == TU) { return; }   // PvP-атака по этой цели уже идёт — перезапуск не нужен
			EngagePlayerFromClick(P);
		}), (float)WaitPvp + 0.001f, false);
		return;
	}
	const FVector2D Cur = Ch->GetGamePosXZ();
#if ASDA_COMBAT_TRACE
	ASDA_CTRACE_BEGIN_PVP(TU, *AsdaCtPc::PlayerNick(Tgt), TEXT("второй клик по выбранному игроку — PvP-атака"));
	AsdaCombatTrace::NoteEngagedUser(TU);   // журнал боя: строки по этому игроку — уровень 1 (30 с и после цикла)
#endif
	ASDA_CTRACE(EAsdaCT::Pvp, TEXT("PvP-атака userIdx=%d '%s': стоп 4008 в (%.0f,%.0f), сброс серии, заявка 4201 (оружие подтип %d, дальность %.0f, дист %.0f)"),
		TU, *AsdaCtPc::PlayerNick(Tgt), Cur.X, Cur.Y, WeaponSub, RangeU, FVector::Dist2D(Ch->GetActorLocation(), Tgt->GetActorLocation()));
	// [ОРИГИНАЛ] input.c:1641–1676: цель, состояние 5, `Net_SendActionPacket` (стоп 4008 при `g_bActionPending`), 4201.
	Net->SendStop4008(Cur.X, Cur.Y);
	WS->ResetPvpSeries((int32)Net->SelfUserNum, TEXT("заявка 4201"));   // `playerCharObject_ResetUiTickCounters` в `netSabzil_SendAction`
	if (!Net->SendAttackPlayer4201(TU, Cur.X, Cur.Y))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("PvP: 4201 по userIdx=%d не ушёл (гейт отправителя) — боевой покой"), TU);
		ASDA_CTRACE_END(TEXT("4201 не ушёл"));
		Ch->SetCombatStance(true);   // [ОРИГИНАЛ] `CharFx_AutoSabzil_State(…, 0, 0, 20)` — боевой покой
		CombatStanceUntil = FPlatformTime::Seconds() + CombatStanceHold;
		return;
	}
	bAutoAttack = true; bCombatStarted = true; bPvpAttack = true; bPvpAccepted = false; bInCombatStance = false;
	PvpTargetUserIdx = TU; PvpSentAtS = FPlatformTime::Seconds(); NextAttackTickTime = 0.0;
	Ch->FaceGameDir(Tgt->GetGamePosXZ() - Cur);
	Ch->AttackFaceTarget = Tgt;
	UE_LOG(LogAsdaPC, Log, TEXT("[PvP] атака игрока userIdx=%d: 4201 ушёл, ждём 4202 (оружие %d, дальность %.0f)"), TU, WeaponSub, RangeU);
}
