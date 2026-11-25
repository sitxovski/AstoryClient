// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerController_Move.cpp — ★ 23.10.2026 часть AAsdaPlayerController, вынесенная из AsdaPlayerController.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaPlayerController.plan.json).
//   Хвост PvP (принятие, стоп, тик, точка отправки), ход к точке, тик маршрута, сесть/встать, правая кнопка, колесо.
//   #include — те же, что у AsdaPlayerController.cpp, в том же порядке; общее — AsdaPlayerControllerInternal.h.
#include "World/AsdaPlayerController.h"
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
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaOsWindow.h"             // ★ R5-fix6: физический PrintScreen — только при настоящем окне ОС
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

// ★ 01.11.2026: ключ `asda.Skill.ApproachOverAttack` (разбор — AsdaSkillUseSubsystem.cpp) читается аксессором.
extern bool AsdaSkillApproachOverAttackOn();
#include "UI/AsdaSystemOptionSubsystem.h"   // ★ ESC-меню 69400 и окно настроек 69500 (клавиша ESC, захват клавиш)
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
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#endif
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя (клик → цель → бег → дальность → заявка → замах → выход)
#include "World/AsdaAttackExit.h"         // ★ 01.11.2026: общий отправитель 4024/400 (выход из атаки)
#include "AsdaPlayerControllerInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerController*.cpp (LogAsdaPC, чистые хелперы)
#include "World/AsdaSkillCastLock.h"        // ★ 05.11.2026: «свой навык занят» — приказ автопути во время навыка
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущему — только ход и предметы (тело — AsdaPlayerCharacter_Move.cpp).
bool AsdaSwimActionLocked(const class AAsdaPlayerCharacter* Ch);


// ★ Ревизия В4 МЕЛК-6 (рыбалка, 09.10.2026) [ПОРТ]: клик по миру, сделанный во время рыбалки. Эталон гейт `sub_92A968(g_pMap)`
//   (g:12566 / g:13266) стоит ДО снятия `g_WorldInputFlags` — бит клика живёт, и клик исполняется в первом кадре после конца
//   рыбалки (по текущему положению курсора, `Point` кадра). Порт по умолчанию такой клик отбрасывает (как у гейта ремесла).
// ★ 22.10.2026 (ревизия WASD М-2 / ревизия стана п. 7): отпускание ПКМ, которое синтезирует сброс клавиш, меню игрока не открывает.
static ASDA_DEV_CVAR(int32, CVarAsdaInputFlushRmbGuard,
	TEXT("asda.Input.FlushRmbGuard"), 1,
	TEXT("Сброс зажатых клавиш (всплыло модальное окно, окно игры потеряло фокус) при зажатой ПКМ: 1 = её «отпускание» не открывает ")
	TEXT("меню игрока под курсором; 0 = прежнее поведение."),
	ECVF_Default);

void AAsdaPlayerController::OnPvpAccepted(int32 TargetUserIdx)
{
	ASDA_CTRACE(EAsdaCT::Pvp, TEXT("4202 код 1: атака по userIdx=%d принята сервером (заявка была по %d, PvP-атака %s) — подход/замах"),
		TargetUserIdx, PvpTargetUserIdx, bPvpAttack ? TEXT("идёт") : TEXT("уже снята — ответ проигнорирован"));
	if (!bPvpAttack) { return; }
	if (TargetUserIdx != PvpTargetUserIdx)
	{
		UE_LOG(LogAsdaPC, Warning, TEXT("[PvP] 4202 код 1 по цели %d, а заявка была по %d — берём цель сервера"), TargetUserIdx, PvpTargetUserIdx);
		PvpTargetUserIdx = TargetUserIdx;
	}
	bPvpAccepted = true;
	NextAttackTickTime = 0.0;
	UE_LOG(LogAsdaPC, Log, TEXT("[PvP] 4202 код 1: атака по %d принята — подход/замах"), TargetUserIdx);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 31.10.2026 (баг 9 владельца «в дуэли из луков/арбалетов/баллист не работает обычная атака») [ОРИГИНАЛ]: ОТВЕТ НА 4201 —
//   НЕ ТОЛЬКО 4202. Порт начинал подход/замах ТОЛЬКО по 4202 код 1 (`OnPvpAccepted`), а без него через `CombatAckTimeout` снимал атаку
//   («нет ответа 4202»). Но сервер отвечает на 4201 двумя путями (NpcServer `Process_AttackToPlayer` = `sub_460710`,
//   NpcServer.exe.c:63697-63790):
//   • цель ВНЕ прямоугольника «дальность оружия атакующего ±1 тайл» (`+30864…30876`, `sub_41A3C0` :18120) → 8223 → GameServer
//     `sub_555150` (GameServer.c:131052) → 4202 код 1 → клиент подходит и бьёт (этот путь у порта был);
//   • цель ВНУТРИ → удар обрабатывается сразу (`sub_45FF80(…, a4 = 1)` :63404, первый удар серии) → 8225 → GameServer
//     `sub_555800` (GameServer.c:131254) → сразу **4204 код 0** со значением удара; 4202 не приходит вовсе.
//   У лука/арбалета/баллисты дальность 15–16 тайлов — стрелок почти всегда кликает соперника «в дальности», поэтому выстрела не было
//   никогда; у ближнего боя (1–3 тайла) то же случалось при клике вплотную. Эталон клиента: свой 4204 код 0 при состоянии ожидания
//   ответа 22 (и 15 подход, 0 покой, 33–47 эмоции, 51, 60–67) → `CharFx_AutoSabzil_State(…, 5)` — сразу замах, значения пакета —
//   в очередь (network.c:41447-41548). [ПОРТ] Гейт эталона `field_C & 6` (:41455 — флаги персонажа, которых порт не ведёт) НЕ
//   переносится: подменять его глобальным битом «4006 в пути» нельзя — при 4006 без ответа 4007 первый удар выбрасывался бы и баг
//   возвращался (ревизия бага 9, как и у 4202: `OnPvpAccepted` этого гейта тоже не держит). Свой персонаж в навыке/касте (24/25/14)
//   в списке состояний эталона НЕТ (:41459-41488) — там действие не меняется, значения только в очередь: порт ставит «атака принята»
//   и стойку, а замах возобновит `PvpAttackTick`, когда навык освободится.
//   Здесь — «принять атаку по первому удару»: тот же набор, что у входа в дальность в `PvpAttackTick` (стойка, замах), без 4008
//   (стоп ушёл перед 4201 и уходит перед каждым 4203). Ключ `asda.Combat.PvpHitAccepts`: 1 (по умолчанию) — так; 0 — прежнее.
//   Возврат: 0 — не этот случай (атака уже принята/не наша цель/ключ 0); 1 — атака принята этим 4204.
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
static ASDA_DEV_CVAR(int32, CVarAsdaPvpHitAccepts,
	TEXT("asda.Combat.PvpHitAccepts"), 1,
	TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = свой 4204 код 0 в ожидании ответа на 4201 (сервер обработал первый удар сразу — цель в дальности ")
	TEXT("оружия ±1 тайл, 4202 не будет) начинает PvP-атаку: замах сразу, значения в очередь (network.c:41447-41548). 0 = прежнее: атака ")
	TEXT("только по 4202, иначе снимается через 2,5 с."),
	ECVF_Default);

int32 AsdaPvpAcceptByFirstHit(AAsdaPlayerController* PC, int32 TargetUserIdx)
{
	if (!PC || CVarAsdaPvpHitAccepts.GetValueOnGameThread() == 0) { return 0; }
	if (!PC->bPvpAttack || PC->bPvpAccepted || PC->PvpTargetUserIdx != TargetUserIdx) { return 0; }
	UGameInstance* GI = PC->GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn());              // свой персонаж
	AAsdaPlayerCharacter* Tgt = Cast<AAsdaPlayerCharacter>(PC->TargetActor.Get());     // цель атаки
	if (!Net || !Ch || !Tgt) { return 0; }
	float RangeU = PC->AttackRange; int32 WeaponSub = -1;
	AsdaPvpSelfWeapon(GI, Net, PC->AttackRange, RangeU, WeaponSub);
	PC->bPvpAccepted = true;
	PC->NextAttackTickTime = 0.0;
	// Свой в навыке/касте (эталон 24/25/14 — не в списке :41459-41488): действие не меняется — замах не начинаем, стойка «взведена»;
	//   `PvpAttackTick` (ветка стойки) вернёт замах, когда навык освободится.
	{
		const UAsdaSkillUseSubsystem* SU = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
		const int32 CastState = SU ? SU->GetSelfCharState() : 0;
		const bool bCastBusy = SU && SU->GetActiveSkill() >= 0 && (CastState == 25 || CastState == 24 || CastState == 14);
		if (Ch->IsSkillAnimActive() || bCastBusy)
		{
			PC->bInCombatStance = true;
			Ch->AttackFaceTarget = Tgt;
			ASDA_CTRACE(EAsdaCT::Pvp, TEXT("свой 4204 код 0 по userIdx=%d без 4202: атака принята, но идёт навык — замах вернёт тик после навыка"), TargetUserIdx);
			UE_LOG(LogAsdaPC, Log, TEXT("[PvP] 4204 код 0 по %d без 4202: атака принята во время навыка — замах после навыка"), TargetUserIdx);
			return 1;
		}
	}
	// [ОРИГИНАЛ] состояние 5: ход снят, замах с кадра 0, боевая стойка (тот же набор, что у входа в дальность в `PvpAttackTick`).
	PC->CancelRoute();
	Ch->StopMoving();
	PC->bInCombatStance = true;
	const FVector2D Cur = Ch->GetGamePosXZ();
	Ch->AttackFaceTarget = Tgt;
	Ch->FaceGameDir(Tgt->GetGamePosXZ() - Cur);
	const float Len = Ch->StartAttackSwing(WeaponSub, /*HoldSec=*/0.f);
	Ch->SetCombatStance(true);
	PC->CombatStanceUntil = FPlatformTime::Seconds() + PC->CombatStanceHold;
	ASDA_CTRACE(EAsdaCT::Pvp, TEXT("свой 4204 код 0 по userIdx=%d без 4202: сервер обработал первый удар сразу (цель в дальности ±1 тайл) — атака принята, замах (клип %d, %.2f с, дист %.0f)"),
		TargetUserIdx, AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), Len, FVector::Dist2D(Ch->GetActorLocation(), Tgt->GetActorLocation()));
	UE_LOG(LogAsdaPC, Log, TEXT("[PvP] 4204 код 0 по %d без 4202: атака принята по первому удару — замах (клип %d, длина %.2f с)"),
		TargetUserIdx, AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), Len);
	return 1;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 01.11.2026 (ревизия 4024/400) [ОРИГИНАЛ]: ВЫХОД СВОЕГО ИЗ ДЕЙСТВИЯ АТАКИ 5 → 0 = 4024 КОД 400.
//   `CCharacter_SetAction@0x9EE720` case 0: прошлое действие 5 ⇒ `Send_CharStatePos(свой, курс, 400)` (entity_char.c:16545-16556);
//   переход в 0 с любым подтипом (и боевая стойка 20 — смерть цели, отказ, PvP-стоп). НЕ шлётся из 22 (ждём ответа на 4201),
//   15 (подход), 24/25/14 (навык), 6 (смерть). Сервер: `CharEMotionSync` (GameServer.c:124795) пишет 400 в состояние CUser и
//   рассылает соседям 4025 — их клиенты ставят нас в покой (замах снят, network.c:39645). ★ Сервер принимает 4024 только когда
//   игрок НЕ «в движении» (`sub_523ED0`, GameServer.c:124817) — поэтому 400 уходит синхронно в точке выхода, ДО 4006 хода
//   (эталон case 0x10: `SetAction(0)` → 400, затем 4006). Отправитель — общий `AsdaAttackExit::SendAttackExit400` (ключ
//   `asda.Combat.AttackExit400`), здесь — только правило «было ли действие 5» и вызовы из точек выхода порта.
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
bool AsdaSelfInAttackAction(const AAsdaPlayerController* PC)
{
	if (!PC || !(PC->bAutoAttack || PC->bPvpAttack)) { return false; }
	if (PC->bPvpAttack && !PC->bPvpAccepted) { return false; }                    // ожидание ответа на 4201 (22) — не действие 5
	const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn());   // свой персонаж
	if (!Ch || Ch->IsIncapacitated() || Ch->IsSkillAnimActive()) { return false; }   // смерть (6) / навык (24) — не действие 5
	return PC->bInCombatStance || Ch->IsAttackLoopAlive();                            // замах на дальности (подход 15 и ожидание 22 — нет)
}

// Переход из атаки сразу в НОВУЮ атаку (отложенное действие эталона: прямая запись 5 + `SetAction(22)`, entity_char.c:24029-24075,
//   без прохода через 0) — 400 не шлётся: `AsdaStopPvpAttackForNewAttack` гасит его на время своего `StopPvpAttack`.
static bool GAsdaExit400Suppressed = false;

void AsdaStopPvpAttackForNewAttack(AAsdaPlayerController* PC, const TCHAR* Why)
{
	if (!PC) { return; }
	TGuardValue<bool> Guard(GAsdaExit400Suppressed, true);
	PC->StopPvpAttack(Why);
}

bool AsdaLeaveAttackAction(AAsdaPlayerController* PC, const TCHAR* Why)
{
	if (GAsdaExit400Suppressed || !AsdaSelfInAttackAction(PC)) { return false; }
	ASDA_CTRACE(EAsdaCT::Exit, TEXT("выход своего из действия атаки 5 → 0 (%s): 4024 код 400"), Why ? Why : TEXT("?"));
	return AsdaAttackExit::SendAttackExit400(PC->GetGameInstance(), Why);
}

void AAsdaPlayerController::StopPvpAttack(const TCHAR* Why)
{
	AsdaLeaveAttackAction(this, Why);   // ★ 01.11.2026: 5 → 0 (подтип 20) — 4024/400 до снятия флагов
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UE_LOG(LogAsdaPC, Log, TEXT("[PvP] атака по %d окончена (%s) — боевая стойка"), PvpTargetUserIdx, Why ? Why : TEXT("?"));
	ASDA_CTRACE(EAsdaCT::Exit, TEXT("PvP-атака по userIdx=%d окончена (%s) — замах остановлен, боевая стойка, серия сброшена"),
		PvpTargetUserIdx, Why ? Why : TEXT("?"));
	ASDA_CTRACE_END(Why ? Why : TEXT("PvP-атака окончена"));
	bAutoAttack = false; bCombatStarted = false; bInCombatStance = false;
	bPvpAttack = false; bPvpAccepted = false; PvpTargetUserIdx = -1;
	GetWorldTimerManager().ClearTimer(DeferredAttackSendTimer);
	if (AAsdaPlayerCharacter* Ch = GetChar())
	{
		Ch->StopAttackAnim();
		Ch->SetCombatStance(true);   // [ОРИГИНАЛ] `CharFx_AutoSabzil_State(…, 0, 0, 20)` / `CChar_SetState(…, 0, 0, 20)`
		CombatStanceUntil = FPlatformTime::Seconds() + CombatStanceHold;
	}
	if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; WS && Net)
	{
		WS->ResetPvpSeries((int32)Net->SelfUserNum, Why);   // покой сбрасывает очередь (entity_char.c:19301–19305)
	}
}

void AAsdaPlayerController::PvpAttackTick()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = GetChar();
	AAsdaPlayerCharacter* Tgt = Cast<AAsdaPlayerCharacter>(TargetActor.Get());
	if (!Net || !WS || !Ch || !Net->bInGameConnected) { StopPvpAttack(TEXT("нет сети/персонажа")); return; }
	if (!Tgt || WS->FindUserIdxByActor(Tgt) != PvpTargetUserIdx) { StopPvpAttack(TEXT("цель выбыла")); return; }
	if (Tgt->IsIncapacitated()) { StopPvpAttack(TEXT("цель мертва")); return; }   // эталон: смерть цели — состояние 5 → 0 (подтип 20)
	if (!Net->IsSelfPvpOn()) { StopPvpAttack(TEXT("PvP-режим выключен")); return; }
	const double Now = FPlatformTime::Seconds();
	if (!bPvpAccepted)
	{
		// Ждём 4202. Эталон без ответа держит ожидание (состояние 22); [ПОРТ] страж — `CombatAckTimeout` (эталонные 2500 мс).
		if (Now - PvpSentAtS > (double)CombatAckTimeout) { StopPvpAttack(TEXT("нет ответа 4202")); }
		return;
	}
	// ★★★ 01.11.2026 [ОРИГИНАЛ] (`asda.Skill.ApproachOverAttack`, ревизия п.3): нажатый вне дальности навык ставит 102 вместо атаки 5 и в
	//   PvP — эталонное `Player_UpdateAutoActionState` case 0x66 ведёт подход к `pTargetChar` одинаково для моба и игрока
	//   (entity_char.c:24852-24872). Пока подход идёт: PvP-замах, 4203 и собственный ход PvP-атаки стоят (замах снят при старте подхода).
	if (AsdaSkillApproachOverAttackOn())
	{
		const UAsdaSkillUseSubsystem* SUa = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
		if (SUa && SUa->HasSkillApproach())
		{
			ASDA_CTRACE_V(EAsdaCT::Pvp, TEXT("PvP-тик: идёт подход ради навыка (102) — замах, 4203 и подход атаки ждут"));
			return;
		}
	}
	float RangeU = AttackRange; int32 WeaponSub = -1;
	AsdaPvpSelfWeapon(GI, Net, AttackRange, RangeU, WeaponSub);
	const float TargetR = Tgt->PickHitbox ? FMath::Abs(Tgt->PickHitbox->GetScaledCapsuleRadius()) : 0.f;
	const float Dist = FVector::Dist2D(Ch->GetActorLocation(), Tgt->GetActorLocation());
	const FVector2D Cur = Ch->GetGamePosXZ();
	const FVector2D DstG = Tgt->GetGamePosXZ();
	const float StopDist = FMath::Max(60.f, RangeU + TargetR - 20.f);                     // встать на дальности удара
	const float RangeIn = FMath::Max(RangeU + TargetR, StopDist + kRouteArriveDist + 10.f); // порог входа (как у моба)
	Ch->AttackFaceTarget = Tgt;   // покадровый доворот к цели (эталон `objAI_FaceTargetByUserIdx`)
	if (bInCombatStance)
	{
		// В стойке: замах идёт; его снял навык — вернуть, когда навык освободится (как у моба).
		if (!Ch->IsAttackLoopAlive())
		{
			const UAsdaSkillUseSubsystem* SU = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
			const int32 CastState = SU ? SU->GetSelfCharState() : 0;
			const bool bCastBusy = SU && SU->GetActiveSkill() >= 0 && (CastState == 25 || CastState == 24 || CastState == 14);
			if (Ch->IsSkillAnimActive() || bCastBusy) { return; }
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("PvP: навык освободился — замах по userIdx=%d возобновлён (клип %d)"),
				PvpTargetUserIdx, AAsdaPlayerCharacter::AttackClipIdx(WeaponSub));
			Ch->StartAttackSwing(WeaponSub, /*HoldSec=*/0.f);
			Ch->SetCombatStance(true);
			CombatStanceUntil = Now + CombatStanceHold;
		}
		Ch->StopMoving();
		return;
	}
	if (Dist <= RangeIn)
	{
		// [ОРИГИНАЛ] 4202 в дистанции: стоп хода и состояние атаки (network.c:41143–41172); у порта — стоп, стойка и «взвод».
		if (IsEtalonSpeedSync() && Net->IsMoveRequestInFlight()) { return; }   // грант подхода ещё в пути — доиграть шаг
		CancelRoute();
		Ch->NetSnap(Cur);
		Net->SendStop4008(Cur.X, Cur.Y);
		bInCombatStance = true;
		Ch->FaceGameDir(DstG - Cur);
		const float Len = Ch->StartAttackSwing(WeaponSub, /*HoldSec=*/0.f);
		Ch->SetCombatStance(true);
		CombatStanceUntil = Now + CombatStanceHold;
		UE_LOG(LogAsdaPC, Log, TEXT("[PvP] замах по %d: клип=%d длина=%.2f с (дистанция %.0f ≤ %.0f)"), PvpTargetUserIdx,
			AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), Len, Dist, RangeIn);
		ASDA_CTRACE(EAsdaCT::Range, TEXT("PvP: ВОШЛИ в дальность userIdx=%d: дист %.0f ≤ вход %.0f — стоп 4008 в (%.0f,%.0f)"), PvpTargetUserIdx, Dist, RangeIn, Cur.X, Cur.Y);
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("PvP: замах ВЗВОД по userIdx=%d: клип %d, длина %.2f с"), PvpTargetUserIdx, AAsdaPlayerCharacter::AttackClipIdx(WeaponSub), Len);
		return;
	}
	// Вне дистанции — подход (эталон: состояние 15, 4006 к цели). [ПОРТ] 4006 без номера цели: в поле tgt 4006 у порта — rid
	//   сущности, а номер персонажа сервер в нём не ждёт (сам подход — тот же маршрут, что у моба).
	if (Now < NextApproachTime) { return; }
	if (IsEtalonSpeedSync() && Net->IsMoveRequestInFlight()) { return; }
	FVector2D Dir = DstG - Cur;
	const float D = Dir.Size();
	FVector2D Stop = DstG;
	if (D > KINDA_SMALL_NUMBER) { Dir /= D; Stop = DstG - Dir * FMath::Min(StopDist, D); }
	if (FVector2D::Distance(Cur, Stop) > FMath::Max(kMoveMinSendDist, kRouteArriveDist))
	{
		ASDA_CTRACE(EAsdaCT::Move, TEXT("PvP: подход к userIdx=%d: дист %.0f > вход %.0f — бег к стоп-точке (%.0f,%.0f)"), PvpTargetUserIdx, Dist, RangeIn, Stop.X, Stop.Y);
		StartRoutedMove(Stop, -1, /*bApproach=*/true);
	}
	NextApproachTime = Now + 0.4;
}

void AAsdaPlayerController::PvpOnSendPoint()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Ch = GetChar();
	AAsdaPlayerCharacter* Tgt = Cast<AAsdaPlayerCharacter>(TargetActor.Get());
	if (!bPvpAttack || !bPvpAccepted || !Net || !Ch)
	{
		ASDA_CTRACE(EAsdaCT::Pvp, TEXT("PvP точка отправки: 4203 не шлём (атака=%s, 4202 принят=%s, сеть/персонаж=%s)"),
			bPvpAttack ? TEXT("да") : TEXT("нет"), bPvpAccepted ? TEXT("да") : TEXT("нет"), (Net && Ch) ? TEXT("есть") : TEXT("нет"));
		return;
	}
	if (!Tgt || Tgt->IsIncapacitated()) { StopPvpAttack(TEXT("точка отправки: цели нет")); return; }
	float RangeU = AttackRange; int32 WeaponSub = -1;
	AsdaPvpSelfWeapon(GI, Net, AttackRange, RangeU, WeaponSub);
	// [ОРИГИНАЛ] ближний бой (`equippedWeaponType` ≤ 2, 3, 5) перед 4203 сверяет дистанцию: `dist² > дальность² + 90000 + r²` →
	//   покой (entity_char.c:27245–27258, LABEL_124). У дальнобойного (8–11) проверки нет — дистанцию решает сервер (4204 код 1).
	if (WeaponSub >= 0 && WeaponSub <= 5)
	{
		const float TargetR = Tgt->PickHitbox ? FMath::Abs(Tgt->PickHitbox->GetScaledCapsuleRadius()) : 0.f;
		const float D2 = FVector::DistSquared2D(Ch->GetActorLocation(), Tgt->GetActorLocation());
		if (D2 > RangeU * RangeU + 90000.f + TargetR * TargetR) { StopPvpAttack(TEXT("цель вне досягаемости на точке отправки (эталон LABEL_124)")); return; }
	}
	// Серверные часы (как у 4028): точка пришла раньше — повтор таймером к моменту «можно».
	if (UAsdaAttackRhythmSubsystem::IsEnabled())
	{
		const UAsdaAttackRhythmSubsystem* Rh = GI->GetSubsystem<UAsdaAttackRhythmSubsystem>();
		const double NowS = FPlatformTime::Seconds();
		const double Earliest = Rh ? Rh->EarliestNext4028S() : 0.0;
		if (Earliest > NowS + 0.002)
		{
			ASDA_CTRACE(EAsdaCT::Pvp, TEXT("PvP точка отправки раньше серверных часов на %.1f мс — 4203 по userIdx=%d таймером"),
				(Earliest - NowS) * 1000.0, PvpTargetUserIdx);
			GetWorldTimerManager().SetTimer(DeferredAttackSendTimer, this, &AAsdaPlayerController::OnSwingCycleCompleted,
				FMath::Max(0.001f, (float)(Earliest - NowS)), false);
			return;
		}
		GetWorldTimerManager().ClearTimer(DeferredAttackSendTimer);
	}
	const FVector2D Cur = Ch->GetGamePosXZ();
	ASDA_CTRACE(EAsdaCT::Send, TEXT("PvP удар 4203 по userIdx=%d: позиция (%.0f,%.0f), дист %.0f, оружие подтип %d"),
		PvpTargetUserIdx, Cur.X, Cur.Y, FVector::Dist2D(Ch->GetActorLocation(), Tgt->GetActorLocation()), WeaponSub);
	Ch->FaceGameDir(Tgt->GetGamePosXZ() - Cur);
	if (!Net->SendAttackedPlayer4203(PvpTargetUserIdx, Cur.X, Cur.Y)) { StopPvpAttack(TEXT("4203 не ушёл (гейт эталона)")); return; }
#if ASDA_COMBAT_TRACE
	AsdaCombatTrace::NoteEngagedUser(PvpTargetUserIdx);   // журнал боя: игрок, которого мы бьём, — «наш» ещё 30 с
#endif
}

bool AAsdaPlayerController::IsAttackSendDeferred() const
{
	return GetWorld() && GetWorldTimerManager().IsTimerActive(DeferredAttackSendTimer);
}

void AAsdaPlayerController::OnCombatAckReceived(float SeriesLen)
{
	ASDA_CTRACE(EAsdaCT::Recv, TEXT("ответ на замах (4029/4027) в контроллер: серия %.2f с — %s"), SeriesLen,
		bAutoAttack ? *FString::Printf(TEXT("ожидание снято, следующий тик боя через %.2f с"), FMath::Max(0.2f, SeriesLen)) : TEXT("боя нет, проигнорирован"));
	if (!bAutoAttack) { return; }
	bWaitingCombatAck = false;
	NextAttackTickTime = FPlatformTime::Seconds() + FMath::Max(0.2f, SeriesLen);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 26.10.2026 (баг владельца «навигация журнала во время атаки», волна navattack): АВТОПУТЬ ВЫВОДИТ ИЗ АТАКИ. [ИСПРАВЛЕНИЕ]
//   СИМПТОМ. Игрок бьёт моба и жмёт кнопку маршрута журнала 705…708 («=> Моб — Карта, x, y»): персонаж идёт к новой цели, но
//   остаётся в клипе атаки и продолжает бить прежнего моба (4028 по нему уходят, цикл замаха жив, `SetAnimState(Move)` не трогает
//   клип при живом замахе). Отойдя за удержание дальности, тик боя разворачивал персонажа обратно — подход 4006 с rid моба.
//   ПРИЧИНА. Все приказы автопути (`MoveToGamePoint` ← журнал своей карты, клик по мини-/большой карте, шаг межкартового
//   автопути, следование) шли в `RequestMoveTo` мимо выхода из боя. Выход из боя порт делал только в ветке клика по земле
//   (`OnLeftClick`, case Ground) и в ходе клавишами; флаги автоатаки оставались взведены.
//   ЭТАЛОН (как устроено у оригинала, все ветки):
//   • приказ автопути = `mapClick_BeginPathMove@0xFBC9A0` (своя карта: `miniMapClick_TravelTo@0xC692D0`, world_map.c:4942) или
//     заявка `beginMoveToTile@0xFBE140` (чужая карта); шаги маршрута исполняет главный тик (game_main.c:1592-1662, 1762-1800)
//     вызовом `CGame_HandleGroundClickAttack@0x996480` — ТЕМ ЖЕ, что клик по земле;
//   • `CGame_HandleGroundClickAttack` в состоянии атаки 5/24 хода не шлёт, а ставит отложенное действие 16:
//     `sub_92D316 → sub_996880` (+52121 = 16, +52124…52129 = своя точка и точка клика), game_main.c:6693-6702;
//   • отложенное действие 16 исполняется в точке отправки удара (`Skill_SpawnHitFeedback`, skill_buff.c:762 → LABEL_152 →
//     `Player_UpdateAutoActionState@0xA249A0` case 0x10, entity_char.c:24085-24112) ВМЕСТО 4028: `CharFx_AutoSabzil_State(0,0,0)`
//     — мирный покой (замах снят), `targetRenderId` (+0x32DFC) = −1 — снята цель АТАКИ (выделение остаётся), 4006 подтип 2 из
//     своей точки в точку клика с целью −1, `+52135 = 0`, затем чистка узлов маршрута, метки земли и заявки автопути (`sub_FBE200`
//     = `g_bMapTransferActive = 0`); 4008 нет;
//   • и ещё: КАЖДЫЙ кадр, пока своё состояние 5/24/22/15/25/14/27/7/8/9/102/105/16/6 (game_main.c:1668-1720, LABEL_111), главный
//     тик чистит узлы маршрута и снимает заявку автопути. Итог у оригинала: кнопка маршрута посреди атаки (своя карта) — удар
//     доигрывается до точки отправки, атака снята, персонаж идёт к ПЕРВОМУ узлу и встаёт (маршрут потерян); чужая карта — заявка
//     снята в тот же кадр, атака продолжается, похода нет.
//   ИСПРАВЛЕНИЕ (требование владельца: «сбрасывать состояние атаки, обычное движение — бег, на маунте полёт»): приказ автопути
//   ИГРОКА выводит из атаки сразу, тем же набором, что клик по земле порта (`OnLeftClick`, case Ground), и маршрут не теряется:
//   флаги автоатаки/серии/стойки/ожидания ответа сняты, поставленный 4028 снят, серия брошена (`NoteSeriesAbandoned` — её
//   неприменённые сервером значения не показываются, правка 25.10 «фантомный урон»), замах остановлен, мирный покой; цель остаётся
//   выбранной (у эталона снимается только цель атаки). Ход идёт штатно: 4006 с целью −1, клип бега (крылья — полёт, клип 35;
//   в седле — сидячий клип хода и клип хода транспорта).
//   [ОРИГИНАЛ] Шаг межкартового автопути и приказ следования, выданные ПОСЛЕ того, как игрок сам начал атаку, атаку не ломают:
//   шаг автопути снимает заявку (LABEL_111), следование ждёт конца атаки (в эталоне его заявка снимается тем же LABEL_111 каждый
//   кадр, а само следование живёт). Ключ `asda.Move.AutoPathExitsCombat`: 1 (по умолчанию) — так; 0 — прежнее поведение порта.
//   ★ 26.10.2026 (navattack2): та же точка — у клика по луту в чужой клетке (`StartLootPickup`, разбор эталона действия 103 там).
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
static ASDA_DEV_CVAR(int32, CVarAsdaAutoPathExitsCombat,
	TEXT("asda.Move.AutoPathExitsCombat"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = приказ автопути от игрока (кнопки маршрута журнала 705-708, клик по мини-/большой карте, ")
	TEXT("межкартовый автопуть, «Следовать», клик по луту в чужой клетке) выводит из атаки как клик по земле: замах снят, мирный покой, серия брошена, 4028 по ")
	TEXT("прежней цели нет, бег (крылья — полёт, седло — ход транспорта) по маршруту; шаг автопути при атаке, начатой игроком позже, ")
	TEXT("снимает заявку, следование ждёт конца атаки (эталон LABEL_111). 0 = прежнее поведение порта."),
	ECVF_Default);

// Ключ `asda.Move.AutoPathExitsCombat` для других .cpp (подсистема карты, меню игрока) — аксессор, имя ключа в exe вне журнала не
//   попадает (правило Shipping-проверки дев-строк).
bool AsdaAutoPathExitsCombatOn()
{
	return CVarAsdaAutoPathExitsCombat.GetValueOnGameThread() != 0;
}

// Выход из атаки по приказу автопути игрока (разбор — в шапке блока). Возврат: был ли бой/замах/стойка, которые сняты.
//   Набор — тот же, что у клика по земле (`OnLeftClick`, case Ground), плюс отметка брошенной серии и снятие поставленного 4028
//   (4006 автопути может уйти не сразу: при бите 2 отрезок откладывается до ответа, а серия и удар обязаны кончиться сейчас).
bool AsdaExitCombatForAutoPath(AAsdaPlayerController* PC, const TCHAR* Why)
{
	if (!PC || !AsdaAutoPathExitsCombatOn()) { return false; }
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn());   // свой персонаж
	const bool bLoop = Ch && (Ch->IsAttackLoopAlive() || Ch->IsAttackAnimActive());   // замах ещё играет
	const bool bStance = Ch && Ch->IsCombatStance();                                     // боевая стойка (idle01)
	if (!PC->bAutoAttack && !PC->bCombatStarted && !PC->bPvpAttack && !PC->bInCombatStance && !bLoop && !bStance) { return false; }
	UGameInstance* GI = PC->GetGameInstance();
	const AActor* Tgt = PC->TargetActor.Get();
	const AAsdaMobActor* TgtMob = Cast<AAsdaMobActor>(Tgt);
	ASDA_CTRACE(EAsdaCT::Exit, TEXT("автопуть (%s): выход из боя — автоатака была=%s, серия=%s, PvP=%s, замах %s; стойка снята, цель %s остаётся выбранной"),
		Why ? Why : TEXT("?"), PC->bAutoAttack ? TEXT("да") : TEXT("нет"), PC->bCombatStarted ? TEXT("да") : TEXT("нет"),
		PC->bPvpAttack ? TEXT("да") : TEXT("нет"), bLoop ? TEXT("остановлен") : TEXT("не играл"),
		TgtMob ? *FString::Printf(TEXT("rid=%d"), TgtMob->NetRenderId) : (Tgt ? *Tgt->GetName() : TEXT("(нет)")));
	if (PC->bAutoAttack || PC->bCombatStarted) { ASDA_CTRACE_END(TEXT("автопуть")); }
	// ★ Серия брошена: следующий удар после хода — новый 4026; её неприменённые сервером значения не показываются (25.10).
	if (PC->bCombatStarted)
	{
		if (UAsdaAttackRhythmSubsystem* RhA = GI ? GI->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr)
		{
			RhA->NoteSeriesAbandoned(TEXT("автопуть"));
		}
	}
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->CancelPending4028(TEXT("автопуть: выход из боя"));   // удар к дедлайну по прежней цели не уйдёт
	}
	AsdaLeaveAttackAction(PC, Why);   // ★ 01.11.2026: 5 → 0 — 4024/400 до снятия флагов и до хода (эталон `CCharacter_SetAction` case 0)
	PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
	PC->bArmImpactOnSwingStart = false;   // заявка прежнего боя окно «замах в полёте» не взводит
	if (Ch) { Ch->StopAttackAnim(); Ch->AttackFaceTarget = nullptr; }   // замах снят, доворот к прежней цели снят
	PC->ExitCombatStance();               // мирный покой (эталон `CharFx_AutoSabzil_State(…, 0, 0, 0)`, case 0x10)
	UE_LOG(LogAsdaPC, Log, TEXT("[автопуть] %s: выход из боя — замах снят, мирный покой, серия брошена (asda.Move.AutoPathExitsCombat)"),
		Why ? Why : TEXT("?"));
	return true;
}

// ★ 30.09.2026 (волна Л5 торговли): клик по мини-/большой карте, шаг межкартового автопути, следование, путь журнала — всё это
//   `mapClick_BeginPathMove@0xFBC9A0` эталона (world_map.c:10534-10540): первым делом `!sub_934E86(g_pSellWindow) &&
//   !sub_929DC4(g_bSomeFeatureEnabled)`, иначе МОЛЧА ничего; ★ ревизия Л4/Л5 (СРЕДН-1): третье условие — `!CObject_GetField5581(
//   g_pWarehouseWnd)` (world_map.c:10541, свиток возврата склада) — тоже молча.
void AAsdaPlayerController::MoveToGamePoint(const FVector2D& DstGame)
{
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GetGameInstance()))
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[путь] приказ по карте (%.0f,%.0f) отклонён молча: открыта личная лавка 22000/23000 (mapClick_BeginPathMove)"),
			DstGame.X, DstGame.Y);
		return;
	}
	if (const UAsdaWarehouseSubsystem* Ware = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr)
	{
		if (Ware->IsRecallActive())
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[путь] приказ по карте (%.0f,%.0f) отклонён молча: свиток возврата склада активен (mapClick_BeginPathMove)"),
				DstGame.X, DstGame.Y);
			return;
		}
	}
	// ★ 05.11.2026 [ОРИГИНАЛ] (`AsdaSkillCastLock`, разбор в его заголовке): приказ во время своего навыка — в 24 после клипа (шаг
	//   автопути = `CGame_HandleGroundClickAttack` → отложенное действие 16), в 25/14/13/27 — отброшен (заявка снимается, LABEL_111).
	if (AsdaSkillCastLock::GateMapMove(this, DstGame)) { return; }
	// ★ 22.10.2026 (ревизия стана, ВАЖНО-1): приказ по карте под обездвиживанием — плашка 1019, без хода. Шаги автопути сюда не
	//   доходят под станом (`UAsdaMinimapSubsystem::TickMapTravel` ждёт); при прочих состояниях эталонного набора (страх) — без плашки.
	if (IsStunHardStopKey() && IsMoveInputLockedByState())   // шире эталона (mapClick_BeginPathMove гейта нет) — только под ключом
	{
		if (!bTravelStepIssued) { AsdaPlankCannotMove(GetGameInstance()); }
		UE_LOG(LogAsdaPC, Log, TEXT("[путь] приказ по карте (%.0f,%.0f) отклонён: персонаж обездвижен"), DstGame.X, DstGame.Y);
		bTravelStepIssued = false;
		return;
	}
	// ★★★ 26.10.2026 (navattack, разбор у `AsdaExitCombatForAutoPath`): приказ автопути и атака.
	if (AsdaAutoPathExitsCombatOn())
	{
		if (bTravelStepIssued && bAutoAttack)
		{
			// [ОРИГИНАЛ] шаг межкартового автопути, а игрок тем временем сам начал атаку: главный тик эталона в состоянии атаки
			//   снимает заявку автопути (game_main.c:1707-1719, LABEL_111) — атака не ломается, похода дальше нет.
			bTravelStepIssued = false;
			if (UAsdaMinimapSubsystem* Mini = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr)
			{
				if (Mini->IsTravelRequested()) { Mini->SetDestinationTile(0, 0, false); Mini->CancelTravelRequest(); }
			}
			UE_LOG(LogAsdaPC, Log, TEXT("[автопуть] шаг к (%.0f,%.0f) не выдан: игрок атакует — заявка снята (эталон LABEL_111, game_main.c:1707)"),
				DstGame.X, DstGame.Y);
			ASDA_CTRACE(EAsdaCT::Move, TEXT("шаг автопути к (%.0f,%.0f) не выдан: идёт атака — заявка автопути снята (эталон LABEL_111)"), DstGame.X, DstGame.Y);
			return;
		}
		if (!bTravelStepIssued) { AsdaExitCombatForAutoPath(this, TEXT("приказ по карте/журналу")); }   // [ИСПРАВЛЕНИЕ]
	}
	RequestMoveTo(DstGame);
}

// Движение к точке = МАРШРУТ с обходом препятствий (см. StartRoutedMove).
//   ★ Любой НОВЫЙ приказ движения от игрока отменяет отложенный «подбежать и применить навык»
//     (эталон: клик по земле сбрасывает состояние 102 — `Player_UpdateAutoActionState` case 0x64
//     чистит список чек-пойнтов и pending-слот).
void AAsdaPlayerController::RequestMoveTo(const FVector2D& DstGame)
{
	CancelPendingSkillApproach(TEXT("новый приказ движения"));
	// ★ D5: новый приказ движения замещает отложенное «подбежать и заговорить». Подбегание к NPC
	//   само зовёт `RequestMoveTo`, поэтому его pending взводится ПОСЛЕ этого вызова.
	//   Сам сброс живёт в `StartRoutedMove` (общая точка «игрок начал двигаться»), чтобы он был
	//   ОДИН и на путь через `RequestMoveTo`, и на прямые вызовы `StartRoutedMove` из подсистемы
	//   навыков и боевого подхода.
	StartRoutedMove(DstGame, /*TargetRenderId=*/-1);
}

// Отмена отложенного «подхода ради навыка» — вынесена сюда, чтобы контроллер не тянул
//   подсистему навыков в каждом месте вызова.
void AAsdaPlayerController::CancelPendingSkillApproach(const TCHAR* Why)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaSkillUseSubsystem* SU = GI->GetSubsystem<UAsdaSkillUseSubsystem>())
		{
			SU->CancelSkillApproach(Why);
		}
	}
}

// ★ 27.09.2026 — отказ своего хода при эталонной схеме (обоснование — в объявлении).
void AAsdaPlayerController::OnSelfMoveRefused(int32 Result)
{
	if (!IsEtalonSpeedSync() || !HasRoute()) { return; }
	UE_LOG(LogAsdaPC, Log, TEXT("[путь] сервер отказал в ходе (4007 result=%d) — маршрут закрыт"), Result);
	ASDA_CTRACE(EAsdaCT::Refuse, TEXT("сервер отказал в ходе: 4007 result=%d (%s, rid цели %d) — маршрут закрыт"), Result,
		bRouteApproach ? TEXT("подход") : TEXT("ход"), RouteRenderId);
	StopMoveAndNotify(TEXT("сервер отказал в ходе"));
}

// ★★★ 04.11.2026 [ИСПРАВЛЕНИЕ] (баг владельца «часть NPC не взаимодействует», Силарис / Солнечный берег).
//   Перестройка маршрута ниже (`RouteTick`: персонаж упёрся в непроходимую клетку или отрезок перекрыт) шла через
//   `StartRoutedMove`, а тот начинается с `NotifyPlayerStartedMoving` → `ClearPendingNpcTalk`. Поэтому при подбегании к NPC,
//   путь к которому огибает прилавки, стены и заборы, первая же перестройка молча снимала «подбежать и заговорить»: персонаж
//   добегал, но окно не открывалось (самотест `asda.SelfTest.NpcTalkAll`). У эталона отложенная цель снимается только
//   закрытием диалога (D5, `dialogOnConfirmClose@0xDA3BB0`), а повторный `BeginPathMove` её не трогает — перестройка не
//   новый приказ игрока. Ключ: 1 — разговор переживает перестройку пути; 0 — прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarAsdaNpcTalkKeepOnReplan,
	TEXT("asda.Npc.TalkKeepOnReplan"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = перестройка пути при подбегании к NPC не снимает отложенный разговор (эталон: снимает ")
	TEXT("только закрытие диалога, D5); 0 = прежнее поведение порта (первая перестройка снимала разговор — окно не открывалось)."),
	ECVF_Default);

// ── ★ ПРОДВИЖЕНИЕ ПО МАРШРУТУ (порт тика game_main.c:1592-1644) ─────────────────────────────────
//   Дошли до текущего узла → следующий узел + новый ход. Раз в 1 с проверяем LOS до текущего узла
//   и при перекрытии перестраиваем маршрут от текущей позиции (эталон: повторный BeginPathMove).
void AAsdaPlayerController::RouteTick()
{
	if (!HasRoute()) { return; }
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch || IsSelfDead()) { CancelRoute(); return; }
	const FVector2D Cur = Ch->GetGamePosXZ();
	const FVector2D Wp = Route[RouteIdx];

	// ★★★ 27.09.2026 (эталонная схема): пока запрос хода в пути (бит 2), тик своего персонажа эталона НЕ шлёт
	//   стоп 4008 и не выдаёт новых приказов — доигрывает текущий шаг (`Player_ExecuteSkillOnTarget@0x9F16D0`,
	//   ветка `(g_nGameFlags & 2) != 0 → CCharacter_ApplyMoveState`, иначе `Net_SendActionPacket`). Поэтому здесь
	//   ни прибытия, ни пересчёта: ответ придёт за время пути туда-обратно, сторож снимет бит через 10 с.
	if (IsEtalonSpeedSync())
	{
		UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (Net && Net->IsMoveRequestInFlight()) { return; }
		// Отложенный отрезок (см. `bRouteLegUnsent`) — ответ пришёл, отправляем.
		if (bRouteLegUnsent)
		{
			RouteReplanTime = FPlatformTime::Seconds() + 1.0;
			SendRouteLeg(Cur, Wp);
			return;
		}
	}

	if (FVector2D::Distance(Cur, Wp) <= kRouteArriveDist)
	{
		++RouteIdx;
		if (RouteIdx >= Route.Num())
		{
			// ★★★ МАРШРУТ ПРОЙДЕН → ОБЯЗАТЕЛЬНЫЙ 4008 (правка 2026-08-10).
			//   Раньше здесь стоял голый CancelRoute() — только локальный сброс. Сервер при этом
			//   оставался в состоянии «игрок движется»: `MoveSyncProcess@0x5427D0` по естественному
			//   концу отрезка флаг `CUser+51` НЕ снимает (GameServer.c:123458-123483 — при
			//   `duration <= elapsed` и пустой очередной точке ветка пустая), а снимает его ТОЛЬКО
			//   `CharStop@0x545C70` (пакет 4008). Отсюда и «Сначала нужно остановиться» на всех навыках.
#if ASDA_COMBAT_TRACE
			if (bRouteApproach || RouteRenderId >= 0)
			{
				ASDA_CTRACE(EAsdaCT::Move, TEXT("подход (rid цели %d): маршрут пройден в (%.0f,%.0f) — стоп"), RouteRenderId, Cur.X, Cur.Y);
			}
			else
			{
				ASDA_CTRACE_V(EAsdaCT::Move, TEXT("ход: маршрут пройден в (%.0f,%.0f) — стоп"), Cur.X, Cur.Y);
			}
#endif
			StopMoveAndNotify(TEXT("маршрут пройден"));
			return;
		}
		SendRouteLeg(Cur, Route[RouteIdx]);
		return;
	}

	// Персонаж встал (упёрся в непроходимый тайл в PlayerCharacter::Tick), а узел не достигнут —
	//   значит отрезок перекрыт: перестраиваем маршрут от текущей позиции.
	const double Now = FPlatformTime::Seconds();
	if (Now < RouteReplanTime) { return; }
	RouteReplanTime = Now + 1.0;   // эталонный троттл 1000 мс
	if (!Ch->IsMoving() || !AsdaPath::IsLineWalkable(Ch->WalkGrid, Cur / 100.f, Wp / 100.f))
	{
		const FVector2D Dst = RouteFinalDst;
		const int32 Rid = RouteRenderId;
		const bool bApproach = bRouteApproach;   // ★ 27.09.2026 (К1): пересчёт подхода остаётся подходом
		UE_LOG(LogAsdaPC, Log, TEXT("[путь] пересчёт маршрута от (%.0f,%.0f) к (%.0f,%.0f)"), Cur.X, Cur.Y, Dst.X, Dst.Y);
		ASDA_CTRACE(EAsdaCT::Move, TEXT("перестройка пути (%s, rid цели %d): от (%.0f,%.0f) к (%.0f,%.0f) — %s"),
			bApproach ? TEXT("подход") : TEXT("ход"), Rid, Cur.X, Cur.Y, Dst.X, Dst.Y,
			!Ch->IsMoving() ? TEXT("персонаж встал, узел не достигнут") : TEXT("отрезок перекрыт"));
		// ★ 18.10.2026: перестройка пути — не «игрок взялся за управление». Без флажка `CancelTravelIfManual` снимал заявку
		//   автопути посреди карты (флажок шага к этому моменту уже израсходован первым приказом).
		{
			UGameInstance* GIt = GetGameInstance();
			UAsdaMinimapSubsystem* Mini = GIt ? GIt->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
			if (Mini && Mini->IsTravelRequested()) { BeginTravelStep(); }
		}
		// ★ 04.11.2026: отложенный разговор с NPC переживает перестройку (см. `CVarAsdaNpcTalkKeepOnReplan`).
		const bool bKeepTalk = bNpcTalkPending && CVarAsdaNpcTalkKeepOnReplan.GetValueOnGameThread() != 0;
		const int32 TalkRid = PendingNpcRenderId;
		const TWeakObjectPtr<AAsdaNpcActor> TalkNpc = PendingNpcActor;
		StartRoutedMove(Dst, Rid, bApproach);
		if (bKeepTalk && TalkNpc.IsValid() && HasRoute())
		{
			bNpcTalkPending = true;
			PendingNpcRenderId = TalkRid;
			PendingNpcActor = TalkNpc.Get();
			UE_LOG(LogAsdaPC, Log, TEXT("[NPC] перестройка пути: разговор с npcId=%d сохранён"), TalkNpc->NpcId);
		}
	}
}

// ★★★ ПОЛНАЯ ОСТАНОВКА + СИНХРОНИЗАЦИЯ С СЕРВЕРОМ (обоснование — в объявлении в .h).
//   Эталон делает ровно эту пару: гасит локальный бег и шлёт `Net_SendActionPacket@0x9F9C00`
//   (4008, тип 0, cur == dst == текущая позиция, motionStatus = 0).
void AAsdaPlayerController::StopMoveAndNotify(const TCHAR* Why)
{
	CancelRoute();
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch) { return; }
	const FVector2D Cur = Ch->GetGamePosXZ();
	Ch->StopMove();          // погасить локальное предсказание (клип → idle); позицию НЕ дёргаем
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->bInGameConnected) { return; }
	// ★ Гейт `bActionPending` (эталон `Net_SendActionPacket@0x9F9C00`): 4008 уходит ровно тогда, когда
	//   есть НЕЗАКРЫТЫЙ ход — то есть после любого отправленного 4006. Ходьба по маршруту и ходьба
	//   клавишами шлют 4006 всегда, поэтому стоп в конце маршрута/по отпусканию клавиш уходит.
	const bool bSent = Net->SendStop4008(Cur.X, Cur.Y);
	UE_LOG(LogAsdaPC, Log, TEXT("[движение] стоп (%s): позиция (%.0f,%.0f), 4008 %s"),
		Why, Cur.X, Cur.Y, bSent ? TEXT("отправлен") : TEXT("не нужен — незакрытого хода нет"));
	// В бою (открыт цикл/автоатака) — событие цикла; обычная остановка хода — частый уровень.
	ASDA_CTRACE_IF(AsdaCombatTrace::CurrentCycle() > 0 || bAutoAttack, EAsdaCT::Move, TEXT("остановка (%s): позиция (%.0f,%.0f), 4008 %s"), Why, Cur.X, Cur.Y,
		bSent ? TEXT("отправлен") : TEXT("не нужен — незакрытого хода нет"));
}

// ★★★ 27.09.2026 — «сесть/встать» по эталону (обоснование — у `ToggleSitStand` в .h; pkt4024_facing.md §6).
static ASDA_DEV_CVAR(int32, CVarAsdaSitToggleEtalon,
	TEXT("asda.Char.SitToggleEtalon"), 1,
	TEXT("«Сесть/встать» (кнопка окна действий, клавиша, клик по земле сидя, клавиши хода сидя): 1 = как в оригинале — ")
	TEXT("на ходу сначала «стоп» серверу, занятый персонаж не садится, иконка кнопки меняется на «встать»/«сесть»; ")
	TEXT("0 = прежний порт (переключение позы без этих проверок)."),
	ECVF_Default);

bool AAsdaPlayerController::IsSitToggleEtalon()
{
	return CVarAsdaSitToggleEtalon.GetValueOnGameThread() != 0;
}

void AAsdaPlayerController::ToggleSitStand(const TCHAR* Why)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch) { return; }
	UGameInstance* GI = GetGameInstance();
	// 1. `sub_92B95D` → `get_oB9C0@0xC54CC0` = `*(this+47552)` (флаги персонажа `effectSlots[112].field_C`) == 1 → выход.
	//    [ПОРТ] Флагов персонажа в порту нет — реестр «Биты занятости» (PORT_PENDING_ON_SYSTEMS.md).
	// 2. `CObject_GetField47551` (миснейм `Net_GetCurrentMapType`) == 6 — мёртв → выход (ни пакетов, ни иконок).
	const int32 St = KbEtalonActionState();
	if (St == 6)
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[сесть/встать] (%s): мёртв — ничего (эталон)"), Why);
		return;
	}
	// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущий не садится — МОЛЧА, ни 4008, ни 4024. Ключ `asda.Swim.ActionGate`.
	if (St != 8 && AsdaSwimActionLocked(Ch))
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[сесть/встать] (%s): персонаж плывёт — не садится"), Why);
		return;
	}
	// 3. `getFlag_47551` = `sub_9664C0` (`state == 1`, «идёт») → `Net_SendActionPacket` = 4008 в точке персонажа.
	//    Общая точка стопа порта: 4008 (гейт `bActionPending` внутри отправителя) + локальная остановка и сброс
	//    маршрута — у эталона ход гасит смена состояния на 7 ниже. [ПОРТ] Если дальше сработает гейт «занят»,
	//    эталон оставляет персонажа идти локально при уже остановленном сервере; порт гасит и локальный ход.
	if (IsMovingNow()) { StopMoveAndNotify(TEXT("сесть/встать: стоп перед позой (эталон charToggleSitStand)")); }
	if (St == 8)
	{
		// 4. Сидит → `CharFx_AutoSabzil_State(9)` (встаёт) + 4024 код 109; тело +5/+9 — курс взгляда.
		ApplySitPose4024(/*bSit*/ false, Why);
	}
	else if (!(St == 7 || St == 9))
	{
		// 5. `objStateAllowsRender@0xA2A2F0` («персонаж занят») → МОЛЧА выход, иконки не трогаются.
		//    Общая точка порта — `UAsdaSystemOptionSubsystem::IsLocalPlayerBusy` (полный набор номеров — реестр С7).
		const UAsdaSystemOptionSubsystem* SO = GI ? GI->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr;
		if (SO && SO->IsLocalPlayerBusy())
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[сесть/встать] (%s): персонаж занят — молча (эталон objStateAllowsRender)"), Why);
			return;
		}
		// 6. `CharFx_AutoSabzil_State(7)` (садится) + 4024 код 107.
		ApplySitPose4024(/*bSit*/ true, Why);
	}
	else
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[сесть/встать] (%s): состояние %d (садится/встаёт) — поза не меняется (эталон)"), Why, St);
	}
	// 7. `UI_SetSitStandButtonIcon` (окно действий, группа «функции») + `CHotbar_UpdateAnimSlots` (страница хотбара):
	//    состояние 7/8 → `act_standup`, иначе `act_updown`.
	RefreshSitStandIcons(/*bCharaInfoIcon*/ true);
}

// ★ Общее тело позы (объявление в .h): анимация `CharFx_AutoSabzil_State(7|9)` + 4024 с курсом взгляда (+5/+9).
void AAsdaPlayerController::ApplySitPose4024(bool bSit, const TCHAR* Why)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// сидел → хвост «встаёт» (фаза 9), затем покой; стоял → «садится» (7) → «сидит» (8).
	Ch->SetAnimState(bSit ? EAsdaPlayerState::Sit : EAsdaPlayerState::Idle);
	const int32 Code = bSit ? 107 : 109;
	if (Net) { const FVector2D F = Ch->GetFacingDirGame(); Net->SendCharStatePos4024(Code, (float)F.X, (float)F.Y); }
	UE_LOG(LogAsdaPC, Log, TEXT("[сесть/встать] (%s): %s — 4024 код %d"), Why, bSit ? TEXT("сесть") : TEXT("встать"), Code);
}

// ★ Иконки «сесть/встать» по текущему состоянию персонажа (объявление в .h).
void AAsdaPlayerController::RefreshSitStandIcons(bool bCharaInfoIcon)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch) { return; }
	UGameInstance* GI = GetGameInstance();
	const int32 IconState = Ch->GetEtalonSitState();
	if (bCharaInfoIcon)
	{
		if (UAsdaCharaInfoSubsystem* CI = GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr) { CI->SetSitStandButtonIcon(IconState); }
	}
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->UpdateSitStandIcons(); }
}

// ★★★ Поза личной лавки 107/109 — порт `CharAction_SendStatePose@0xDD71C0` (entity_char.c:48616-48700; разбор в .h).
bool AAsdaPlayerController::SendStallStatePose(int32 PoseCode, const TCHAR* Why)
{
	if (PoseCode != 107 && PoseCode != 109)
	{
		UE_LOG(LogAsdaPC, Warning, TEXT("[поза лавки] (%s): код %d — у эталона только 107/109"), Why, PoseCode);
		return false;
	}
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch) { return false; }
	UGameInstance* GI = GetGameInstance();
	// 1. `sub_92B95D(LocalPlayer) == 1` (флаги `+47552`) → выход. [ПОРТ] Флагов персонажа в порту нет — как у `ToggleSitStand`.
	// 2. `Net_GetCurrentMapType` (МИСНЕЙМ: состояние действия) == 6 — мёртв → выход (ни пакетов, ни иконок).
	if (KbEtalonActionState() == 6)
	{
		UE_LOG(LogAsdaPC, Log, TEXT("[поза лавки] (%s): мёртв — поза %d не ставится (эталон)"), Why, PoseCode);
		return false;
	}
	// 3. `getFlag_47551` (идёт) → `Net_SendActionPacket` = 4008 в точке персонажа.
	if (IsMovingNow()) { StopMoveAndNotify(TEXT("поза лавки: стоп перед позой (эталон CharAction_SendStatePose)")); }
	if (PoseCode == 109)
	{
		// 4. `CharFx_AutoSabzil_State(9)` + 4024 код 109 — БЕЗ проверки «сидит» (в отличие от тумблера).
		ApplySitPose4024(/*bSit*/ false, Why);
	}
	else
	{
		// 5. `objStateAllowsRender == 1` («занят») → return ДО иконок.
		const UAsdaSystemOptionSubsystem* SO = GI ? GI->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr;
		if (SO && SO->IsLocalPlayerBusy())
		{
			UE_LOG(LogAsdaPC, Log, TEXT("[поза лавки] (%s): персонаж занят — поза 107 не ставится, молча (эталон objStateAllowsRender)"), Why);
			return false;
		}
		ApplySitPose4024(/*bSit*/ true, Why);
	}
	// 6. `CHotbar_UpdateAnimSlots(g_pQuickSlotBar, …)` — иконки хотбара. `UI_SetSitStandButtonIcon` эталон здесь НЕ зовёт.
	RefreshSitStandIcons(/*bCharaInfoIcon*/ false);
	return true;
}

// «Двигаемся ли сейчас» — порт эталонного `getFlag_47551(localPlayer)` (флаг движения персонажа),
//   которым эталон решает, нужен ли 4008 перед кастом/атакой.
bool AAsdaPlayerController::IsMovingNow() const
{
	if (HasRoute()) { return true; }
	const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(GetPawn());
	return Ch && Ch->IsMoving();
}

// ★★★ ЕДИНЫЕ ВОРОТА «курсор над интерфейсом» (обоснование — в объявлении).
bool AAsdaPlayerController::IsCursorOverUi(FName& OutZone) const
{
	OutZone = NAME_None;
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// ★ R5-fix8 (14.11.2026) [ШОВ САМОТЕСТОВ]: при подменённой точке мирового клика (`DebugClickScreen` — её ставят только
	//   самотесты и повтор отложенного клика, а повтор этот гейт не проходит) гейт берёт ЭТУ точку, а не курсор ОС. Раньше
	//   самотест подменял точку клика, а гейт смотрел настоящий курсор — мышь пользователя: стояла над окном игры — клики
	//   самотестов молча гасились, и итог зависел от положения мыши (`Fish.ZoneSelfTest` 09.10 — 14 ложных провалов;
	//   `Input.SelfTestClickQueue` 15/9; `Skill.SelfTestCastLock` 32 из 49). У игрока подмены нет — поведение игры прежнее.
	if (DebugClickScreen.IsSet() && WM)
	{
		const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
		const TSharedPtr<SWindow> Win = VP ? VP->GetWindow() : nullptr;
		const FVector2D Org = Win.IsValid() ? FVector2D(Win->GetWindowGeometryInScreen().GetAbsolutePosition()) : FVector2D::ZeroVector;
		return WM->IsScreenPointOverAnyUiWindow(Org + *DebugClickScreen, OutZone);
	}
	return WM && WM->IsCursorOverAnyUiWindow(OutZone);
}

// ПКМ (орбит-камера). Эталон ворот тут НЕ ставит, но юзер потребовал не пускать сквозь окна ни один
//   клик — если нажатие началось над интерфейсом, вращение камеры за этот зажим не стартует.
// ★ 30.09.2026 (волна О3 торговли, ревизия О2 СРЕДН-2): удачный ПКМ-пик игрока = запись `g_nPickedUserIdx`
//   (`Input_PickEntityAltClick@0x996EE0`, input.c:1305-1307). Свой персонаж — тоже игрок под лучом (гейт «не я сам» стоит
//   уже у меню, game_main.c:9412). Промах поле не трогает. Потребитель — `UAsdaPlayerMenuSubsystem::NotePickedUser`.
static void AsdaNotePickedPlayer(UGameInstance* GI, const AActor* Actor, const AActor* SelfPawn)
{
	UAsdaPlayerMenuSubsystem* PMenu = GI ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
	if (!PMenu || !Actor) { return; }
	int32 U = -1;
	if (Actor == SelfPawn) { U = AsdaSelf::GetUserIdxOr(GI, -1); }
	else if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>()) { U = WS->FindUserIdxByActor(Actor); }
	if (U >= 0) { PMenu->NotePickedUser(U); }
}

void AAsdaPlayerController::OnRightPressed()
{
	FName Zone;
	// ★ 30.09.2026 (волна О3): Alt+ПКМ — WM_RBUTTONDOWN при `GetKeyState(VK_MENU) < 0` зовёт `Input_PickEntityAltClick`
	//   ДО ворот интерфейса (game_main.c:5843-5846), то есть пик пишется, даже если нажатие пришлось на окно.
	if (IsInputKeyDown(EKeys::LeftAlt) || IsInputKeyDown(EKeys::RightAlt))
	{
		const FAsdaPick AltPick = PickForClick();
		if (AltPick.Kind == EAsdaPickKind::Player && AltPick.Actor.IsValid()) { AsdaNotePickedPlayer(GetGameInstance(), AltPick.Actor.Get(), GetPawn()); }
	}
	if (IsCursorOverUi(Zone))
	{
		UE_LOG(LogAsdaPC, Verbose, TEXT("[ПКМ] курсор над окном «%s» — орбит-камера не стартует"), *Zone.ToString());
		return;
	}
	bRMBDown = true;
	RMBDragPixels = 0.f;   // ★ новый зажим — новый счёт «проехало ли мышью» (см. OnRightReleased)
	bNextRmbReleaseSynthetic = false;   // ★ 22.10.2026: новое нажатие игрока — прежний признак сброса не действует

	// ★★★ ЗАПОМНИТЬ ЦЕЛЬ ПОД КУРСОРОМ ДЛЯ КОНТЕКСТНОГО МЕНЮ (окно TRANTOOL_WIN 2000).
	//   ЭТАЛОН `GameInput_DispatchEvent@0xC6DCA0` `case 3` (game_main.c:9375-9392) делает это на
	//   НАЖАТИИ ПРАВОЙ КНОПКИ, а не левой. Доказательство — писатель байтов состояния кнопок
	//   `input_ProcessMoveCommand@0x12F0290` (AsdaStory_ida.c:1216898-1216993): индекс байта =
	//   КНОПКА (0 левая, 1 правая, 2 средняя), значение = ФАЗА (1 нажата, 2 двойной, 0 отпущена,
	//   −1 «не она»). `case 3` требует `GetStateByteAt(ev, 1) == 1` = ПРАВАЯ НАЖАТА;
	//   `case 6` требует `GetStateByteAt(ev, 1) == 0` = ПРАВАЯ ОТПУЩЕНА.
	//   ⇒ ВЫДЕЛЕНИЕ ЛКМ ОРИГИНАЛУ НЕ НУЖНО: цель берётся ХИТ-ТЕСТОМ ПОД КУРСОРОМ в момент ПКМ,
	//   а `sub_C7B550` лишь сверяет, что нажатие и отпускание пришлись на одного игрока.
	//   ⚠ Выделение (`SetTarget`, кольцо цели) при этом НЕ трогается — эталон его тут не меняет.
	if (UAsdaPlayerMenuSubsystem* PMenu = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr)
	{
		int32 PickedUserNum = -1;
		const FAsdaPick P = PickForClick();
		// `case 3`: `Input_PickEntityAltClick(...) == 1` — пик удачен → `g_nPickedUserIdx` (О3).
		if (P.Kind == EAsdaPickKind::Player && P.Actor.IsValid()) { AsdaNotePickedPlayer(GetGameInstance(), P.Actor.Get(), GetPawn()); }
		if (P.Kind == EAsdaPickKind::Player && P.Actor.IsValid() && P.Actor.Get() != GetPawn())
		{
			if (UAsdaWorldSubsystem* WS = GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
			{
				PickedUserNum = WS->FindUserIdxByActor(P.Actor.Get());
			}
		}
		PMenu->NotifyRightPress(PickedUserNum);
	}
}

// ★★★ ПКМ-ОТПУСКАНИЕ — ЕДИНСТВЕННАЯ ТОЧКА ОТКРЫТИЯ КОНТЕКСТНОГО МЕНЮ ПО ЧУЖОМУ ИГРОКУ.
//   Эталон `GameInput_DispatchEvent@0xC6DCA0` case 6 (game_main.c:9398-9436): меню 2000
//   открывается именно на ОТПУСКАНИИ правой (msg[0]=6 ставит `CWindowMgr_QueueRButton`,
//   ui_core.c:113631, вызываемая из `case 0x205u` = WM_RBUTTONUP, game_main.c:5839).
//   Разбор всех гейтов — шапка `Public/UI/AsdaPlayerMenuSubsystem.h` §1.
void AAsdaPlayerController::OnRightReleased()
{
	const bool bWasDown = bRMBDown;
	const float Dragged = RMBDragPixels;
	bRMBDown = false;
	RMBDragPixels = 0.f;
	// ★ 22.10.2026 [ПОРТ] (ревизия WASD М-2): отпускание, синтезированное `FlushPressedKeys` (переход модалки `asda.UI.ModalFlushKeys`,
	//   потеря фокуса окна), — не отпускание игрока: события ушли в `UPlayerInput` в кадре сброса и исполняются в ближайшем
	//   опросе ввода. Без гейта ПКМ, зажатая без сдвига над другим игроком, открывала бы меню игрока — прозрачный блокер модалки
	//   хит-тест `IsCursorOverUi` не видит. Орбита камеры снимается (выше), меню — нет.
	//   ★ Правка ревизии стана п. 7: одноразовый признак `bNextRmbReleaseSynthetic` (ставит `FlushPressedKeys`) вместо окна «2 кадра».
	const bool bSynthetic = bNextRmbReleaseSynthetic;
	bNextRmbReleaseSynthetic = false;
	if (bWasDown && bSynthetic && CVarAsdaInputFlushRmbGuard.GetValueOnGameThread() != 0)
	{
		++RmbFlushReleaseSuppressed;
		RMBDragPixels = Dragged;   // ★ 09.11.2026: поворот может вернуться (`FAsdaOrbitCursor::TakeRearm`) — счёт сдвига за зажим не теряем
		UE_LOG(LogAsdaPC, Log, TEXT("[ПКМ] отпускание от сброса клавиш — меню игрока не открываем"));
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaPlayerMenuSubsystem* PM = GI ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
	if (!PM) { return; }

	// ★ Курсор над интерфейсом — мировой реакции нет (общие ворота порта; эталон ПКМ не гасит,
	//   но у нас это прямое требование владельца, см. объявление IsCursorOverUi).
	FName Zone;
	if (!bWasDown || IsCursorOverUi(Zone)) { return; }

	// ★ 30.09.2026 (волна О3): `case 6` тоже начинается с `Input_PickEntityAltClick` (game_main.c:9405) — пик пишет
	//   `g_nPickedUserIdx` при любом зажиме, ДО нашей правки «после разворота камеры меню не открывать» ниже.
	{
		const FAsdaPick RelPick = PickForClick();
		if (RelPick.Kind == EAsdaPickKind::Player && RelPick.Actor.IsValid()) { AsdaNotePickedPlayer(GI, RelPick.Actor.Get(), GetPawn()); }
	}

	// ★ Наша правка дефекта: после ПКМ-разворота камеры меню не выскакивает.
	static const IConsoleVariable* CVarAfterDrag = ASDA_FIND_DEV_CVAR(TEXT("asda.PlayerMenu.OpenAfterDrag"));
	const bool bAllowAfterDrag = CVarAfterDrag && CVarAfterDrag->GetInt() != 0;
	if (!bAllowAfterDrag && Dragged > 6.f)
	{
		UE_LOG(LogAsdaPC, Verbose,
			TEXT("[ПКМ] меню по игроку не открыто: за зажим камера повернулась на %.0f px "
			     "(asda.PlayerMenu.OpenAfterDrag 1 вернёт поведение оригинала)"), Dragged);
		return;
	}

	// Пик под курсором ровно тем же способом, что и ЛКМ (`Input_PickEntityAltClick`).
	const FAsdaPick P = PickForClick();
	int32 UserNum = -1;
	bool bIsSelf = false;
	if (P.Kind == EAsdaPickKind::Player && P.Actor.IsValid())
	{
		bIsSelf = (P.Actor.Get() == GetPawn());
		if (UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			UserNum = WS->FindUserIdxByActor(P.Actor.Get());
		}
	}
	PM->NotifyRightRelease(UserNum, bIsSelf);
}

void AAsdaPlayerController::OnWheel(float AxisValue)
{
	if (FMath::IsNearlyZero(AxisValue)) { return; }
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `byte_2C715C7 = 1` сразу после `CWindowMgr_QueueMouseWheel` (game_main.c:5872) — до гейтов.
	AsdaTutorial::Mark(this, EAsdaTutorialFlag::MouseWheel);
	// ★★★ ПОРЯДОК КАК В ЭТАЛОНЕ (game_main.c:5864, `case 0x20Au`): колесо СНАЧАЛА уходит в
	//   интерфейс — `CWindowMgr_QueueMouseWheel(g_pWindowMgr, wParam)` и `…(g_pGameWindowMgr, …)`
	//   (запись типа 7, ui_core.c:113636) — и только ПОТОМ, если курсор НЕ над UI
	//   (`!CControl_HitTest(...)`), зовётся `Camera_MouseWheelZoom`. Раньше порт просто ГЛОТАЛ
	//   колесо над окном, и списки, у которых нет ползунка, листались только стрелками.
	const int32 Notches = (AxisValue > 0.f) ? 1 : -1;
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->HandleMouseWheel(Notches))
		{
			return;                                    // колесо съел список интерфейса
		}
	}
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch) { return; }
	// ★ Колесо над UI гасит и ОРИГИНАЛ — зум камеры не трогаем, даже если список его не взял.
	{
		FName Zone;
		if (IsCursorOverUi(Zone))
		{
			UE_LOG(LogAsdaPC, Verbose, TEXT("[колесо] курсор над окном «%s» — зум камеры игнорирован"), *Zone.ToString());
			return;
		}
	}
	// Эталон Camera_MouseWheelZoom: шаг ×0.25 текущей дистанции, кламп [170..1270].
	float Len = Ch->CameraArm->TargetArmLength;
	Len *= (AxisValue > 0.f) ? 0.75f : 1.25f;
	Ch->CameraArm->TargetArmLength = FMath::Clamp(Len, 170.f, 1270.f);
}

// Нажата ли клавиша бинда. Два уточнения против «просто IsInputKeyDown(VkToFKey(vk))»:
//   1) НУМПАД. Эталон `Keybind_NormalizeVK@0xE3CB60` сводит VK 96..105 (нумпадные 0..9) к 48..57,
//      то есть нумпадная «1» в оригинале жмёт «BAR SLOT 1» наравне с цифровым рядом. В UE это
//      РАЗНЫЕ FKey, поэтому цифровые бинды проверяем по обоим.
//   2) PRINT SCREEN (VK 44, «SCREEN SHOT»). В `EKeys` UE такой клавиши нет вообще, поэтому через
//      обычный ввод команда 44 не срабатывала НИКОГДА. Опрашиваем платформенно и только когда окно
//      игры активно (иначе скриншот ловился бы свёрнутым).
// ★ Опрос клавиши ПО КОДУ ДЕЙСТВИЯ — единая точка для подсистем (см. объявление).
bool AAsdaPlayerController::IsCmdKeyDown(int32 Cmd) const
{
	if (!Keys) { return false; }
	FAsdaKeyBind B;
	if (!Keys->GetBind(Cmd, B) || B.Vk == 0) { return false; }   // «NONE» = действие без клавиши
	if (!IsBindKeyDown(B.Vk)) { return false; }
	// ★ 25.10.2026 [ОРИГИНАЛ] (`asda.Keys.CaptureSwallow`): идёт захват клавиши окном настроек или клавиша зажата с момента захвата —
	//   не игровая (иначе Пробел, назначаемый на подбор, на нажатии ещё и прыгал бы).
	if (const UAsdaSystemOptionSubsystem* Opt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr)
	{
		if (Opt->IsGameInputCaptured() || Opt->IsVkHeldFromCapture(B.Vk)) { return false; }
	}
	if (B.Mod == EAsdaKeyMod::None) { return true; }
	const bool bShift = IsInputKeyDown(EKeys::LeftShift)   || IsInputKeyDown(EKeys::RightShift);
	const bool bCtrl  = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
	const bool bAlt   = IsInputKeyDown(EKeys::LeftAlt)     || IsInputKeyDown(EKeys::RightAlt);
	switch (B.Mod)
	{
	case EAsdaKeyMod::Shift: return bShift;
	case EAsdaKeyMod::Ctrl:  return bCtrl;
	case EAsdaKeyMod::Alt:   return bAlt;
	default:                 return true;
	}
}

bool AAsdaPlayerController::IsBindKeyDown(int32 Vk) const
{
#if PLATFORM_WINDOWS
	// ★ R5-fix6 (13.11.2026): без настоящего окна ОС (`-RenderOffscreen`) физический PrintScreen не читается: без окна клавиша
	//   считается не нажатой (у VK 44 нет клавиши событий ввода — `VkToFKey` даёт пустую). Разбор — World/AsdaOsWindow.h.
	if (Vk == 44 && AsdaGameHasOsWindow())
	{
		FViewport* Vp = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->Viewport : nullptr;
		if (!Vp || !Vp->IsForegroundWindow()) { return false; }
		return (::GetAsyncKeyState(0x2C /*VK_SNAPSHOT*/) & 0x8000) != 0;
	}
#endif
	const FKey K = UAsdaKeybindConfig::VkToFKey(Vk);
	if (K.IsValid() && IsInputKeyDown(K)) { return true; }
	const FKey NumTwin = UAsdaKeybindConfig::VkToNumpadFKey(Vk);
	return NumTwin.IsValid() && IsInputKeyDown(NumTwin);
}
