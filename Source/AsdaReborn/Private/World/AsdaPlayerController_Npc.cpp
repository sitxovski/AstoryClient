// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerController_Npc.cpp — ★ 23.10.2026 часть AAsdaPlayerController, вынесенная из AsdaPlayerController.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaPlayerController.plan.json).
//   Зона разговора с NPC (подход, клик, тик), останов движения при смене карты, лагерь войны, выход из боевой стойки, цикл замаха.
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
// ★ Ф4: эталонные цвета нотисов PvP-клика (ColWhite/ColGold)   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
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
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#endif
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя (клик → цель → бег → дальность → заявка → замах → выход)
#include "AsdaPlayerControllerInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaPlayerController*.cpp (LogAsdaPC, чистые хелперы)
#include "UI/AsdaCampWarBattleSubsystem.h"   // ★ 30.10.2026: боевая фаза «Битвы армий» — 6723 с пометкой слота точки


// ── ★ СИСТЕМА ТАРГЕТА (порт targeting_system_re.md) ──────────────────────────────────────────

// Дистанция остановки у NPC (эталон: диалог открывается, когда игрок в зоне NPC; вплотную не подбегаем).
static constexpr float kNpcStandDist = 150.f;

// ══════════════════════════════════════════════════════════════════════════════════════════════
//  ★ РАЗГОВОР С NPC (порт `Reports/npc_interaction_re.md` §2 и §2.4, этап Э1)
// ══════════════════════════════════════════════════════════════════════════════════════════════

// ЗОНА РАЗГОВОРА = AABB СУЩНОСТИ NPC, а НЕ радиус.
//   Эталон `worldEntityInitByType@0xF98B30` строит сущности AABB с полу-размером (50, H/2, 50) и
//   центром (posX, posY + H/2, posZ), где H = `npc_list.BIN` +0x68 (heightParam; −1 ⇒ 150), и
//   `bboxContainsPoint(npc + 183, x, y, z)` спрашивает попадание игрока ровно в этот ящик.
//   По горизонтали это КВАДРАТ ±50 юнитов — то есть проверка Чебышева, а не евклидов радиус.
static constexpr float kNpcZoneHalfXY = 50.f;      // эталонный полу-размер AABB по горизонтали
static constexpr float kNpcZoneDefaultH = 150.f;   // эталонный дефолт при heightParam == −1

// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D9 («точки подхода нет — бежим в центр NPC»).
//   ЧТО В ЭТАЛОНЕ: клик по NPC шлёт `CGame_HandleGroundClickAttack(bag, npc[84], npc[85], npc[86], 1)`
//     — обычный click-to-move РОВНО В ЦЕНТР NPC, — а окно открывается по входу в AABB ±50.
//     Персонаж при этом ВЪЕЗЖАЕТ В МОДЕЛЬ собеседника и «тыкается» в неё, пока не попадёт в ящик;
//     ни отступа, ни разворота лицом, ни сообщения «слишком далеко» при промахе нет (§10 реверса).
//
//   ★ ЧЕСТНАЯ ОЦЕНКА «А НАДО ЛИ ЧИНИТЬ» (задание требует обосновать решение).
//     Технически 1:1 в порту ВОСПРОИЗВОДИМО: ни капсула NPC (`AAsdaNpcActor::PickHitbox`), ни
//     капсула персонажа НЕ блокируют движение — обе `QueryOnly` и отвечают только каналу
//     `ECC_Visibility` (нужны для пикинга), а само движение идёт `SetActorLocation` по сетке
//     проходимости `UAsdaWalkGrid`, где моделей NPC нет вовсе. Порог прибытия маршрута
//     `kRouteArriveDist` = 45 < 50, то есть эталонный ящик ±50 был бы достижим.
//   ⇒ Это выбор, а не вынужденная мера. ЧИНИМ по трём причинам:
//     1) отчёт реверса классифицирует поведение как ДЕФЕКТ (D9), а закон `fix-original-defects`
//        велит такие дефекты исправлять с пометкой;
//     2) в порту модели NPC и персонажа — полноценные меши современного рендера, и «персонаж
//        внутри собеседника» читается куда грубее, чем в клиенте 2008 года;
//     3) в порту это поведение УЖЕ было (константа `kNpcStandDist` = 150 стояла в ветке клика до
//        этого захода) — возврат к центру был бы регрессом того, что владелец уже видел.
//   ЧТО ДЕЛАЕМ (редакция 2026-08-20, после живого лога владельца — см. `npc_interaction_fix.md` §1):
//     отступ даёт НЕ цель движения, а ПРАВИЛО ОСТАНОВКИ. Цель движения — ЦЕНТР NPC, дословно как в
//     эталоне; ящик расширен на `kNpcZonePad`, и тик прибытия гасит бег в тот момент, когда игрок
//     ПЕРЕСЁК границу расширенного ящика. Игрок встаёт примерно в `kNpcStandDist` перед
//     собеседником — тот же видимый результат, но без второго порога.
//
//   ★★★ ПОЧЕМУ НЕ «ЦЕЛИТЬСЯ В ТОЧКУ ПЕРЕД NPC», КАК БЫЛО ДО ЭТОЙ ПРАВКИ.
//     Прежняя редакция ставила ЦЕЛЬ МАРШРУТА на `kNpcStandDist` от центра — то есть РОВНО НА
//     ГРАНИЦУ расширенного ящика. А маршрут в порту считается пройденным, когда игрок в
//     `kRouteArriveDist` = 45 от последней точки (`RouteTick`). Итог: остановка в 105…195 от
//     центра, по метрике Чебышева — до 195 при половине ящика 150 ⇒ «дошёл» не засчитывался
//     НИКОГДА, окно не открывалось, а повторный клик даже не сдвигал персонажа (маршрут сразу
//     считался пройденным). Это и видел владелец. Живой лог и арифметика — в отчёте, §1.1-1.2.
//     Эталонная схема этого порока лишена конструктивно: цель = центр, а порог 45 заведомо меньше
//     половины ящика, поэтому попадание в ящик гарантировано самим фактом прихода в цель.
//     ⇒ Возвращаем эталонную схему и оставляем от D9 только расширение ящика.
//   ИНВАРИАНТ, который теперь держится: цель движения лежит ВНУТРИ ящика с запасом
//     `kNpcZoneHalfXY + kNpcZonePad`, что заведомо больше `kRouteArriveDist`, — проверяется
//     `static_assert` ниже, чтобы правка констант не воскресила дефект молча.
//   ОБРАТИМОСТЬ: `kNpcZonePad = 0` возвращает эталонное поведение в точности (персонаж въедет
//     в модель и встанет в ±50 от центра).
static constexpr float kNpcZonePad = kNpcStandDist - kNpcZoneHalfXY;   // = 100
static_assert(kNpcZoneHalfXY + kNpcZonePad > AAsdaPlayerController::kRouteArriveDist,
	"Зона разговора обязана быть больше порога прибытия маршрута: иначе игрок останавливается "
	"снаружи зоны и окно 570 не откроется никогда (дефект порта, живой лог 2026-08-20).");

// Высота AABB этого NPC из данных (`npc_list.BIN` +0x68 через DT_NpcQuestNpc).
//   ⚠ `AAsdaNpcActor::ResolvePlateHeight` сюда не годится: для статичных типов 1/3/5 он отдаёт верх модели
//   (или 0 при ключе asda.Npc.StaticPlateOnModel 0) — у плашки имени своя логика, а разговаривают и с ними.
static float NpcAabbHeight(const AAsdaNpcActor* Npc)
{
	if (!Npc || Npc->NpcId <= 0) { return kNpcZoneDefaultH; }
	if (const FAsdaNpcQuestNpcRow* R = AsdaDt::RowByInt<FAsdaNpcQuestNpcRow>(
			TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc"), Npc->NpcId, TEXT("NpcTalkZone")))
	{
		return (R->PlateHeight == -1) ? kNpcZoneDefaultH : static_cast<float>(R->PlateHeight);
	}
	return kNpcZoneDefaultH;
}

// Порт `bboxContainsPoint(npc + 183, playerX, playerY, playerZ)` + расширение D9.
bool AAsdaPlayerController::IsInsideNpcTalkZone(const AAsdaNpcActor* Npc) const
{
	const AAsdaPlayerCharacter* Ch = GetChar();
	return Ch && IsPointInNpcTalkZone(Npc, Ch->GetActorLocation());
}

bool AAsdaPlayerController::IsPointInNpcTalkZone(const AAsdaNpcActor* Npc, const FVector& UePoint) const
{
	if (!Npc) { return false; }
	// ★★★ 22.10.2026 [ОРИГИНАЛ] статичные объекты (доски, гробницы, камни — классы 1/3/4/5): зона «дошёл» — короб по костям
	//   модели +200 (класс 4 — короб меша +200 по X/Z), `AAsdaNpcActor::GetTalkBoxWorld`. Квадрат ±150 вокруг начала координат,
	//   как у говорящего NPC, у больших объектов лежал внутри модели или сбоку от неё — до него не доходили, окно не открывалось.
	{
		FBox Talk;
		if (Npc->GetTalkBoxWorld(Talk)) { return Talk.IsInsideOrOn(UePoint); }
	}
	const FVector NpcLoc = Npc->GetActorLocation();
	const FVector MyLoc = UePoint;
	const float Half = kNpcZoneHalfXY + kNpcZonePad;                 // ★ FIX-ORIG D9
	// Горизонталь: КВАДРАТ (метрика Чебышева), как в эталонном AABB. Своп осей игра↔UE тут не важен —
	//   ящик квадратный, |dx| и |dy| просто меняются местами.
	if (FMath::Abs(MyLoc.X - NpcLoc.X) > Half) { return false; }
	if (FMath::Abs(MyLoc.Y - NpcLoc.Y) > Half) { return false; }
	// Вертикаль: эталонный диапазон [posY, posY + H] от подошв NPC, расширенный на ту же величину.
	//   ⚠ У ОБОИХ акторов порта начало координат — ПОДОШВЫ: и NPC, и персонаж сажаются на террейн
	//   (`AAsdaPlayerCharacter::TerrainZ` → `SetActorLocation(..., TerrainZ)`), а капсулы висят
	//   смещёнными вверх на свою полу-высоту. Поэтому Z сравниваются напрямую, без поправок.
	const float H = NpcAabbHeight(Npc);
	return MyLoc.Z >= NpcLoc.Z - kNpcZonePad && MyLoc.Z <= NpcLoc.Z + H + kNpcZonePad;
}

// Клик по NPC (одиночный и двойной — одна ветка, см. `SetupInputComponent`).
void AAsdaPlayerController::StartNpcTalk(AAsdaNpcActor* Npc)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	if (!Ch || !Npc) { return; }
	// Эталон до всего прочего проверяет `sub_92F9D1(&g_InventoryBag)` («мир активен») и
	//   `!g_bInventoryLocked`. ★ Поправка 25.09.2026 (волна В3 ремесла): `g_bInventoryLocked` —
	//   НЕ «мы мертвы», а `dword_2C0F2EC` = `g_pNpcWnd`+0x400C, поле [4099] «ИДЁТ РАЗГОВОР С NPC» (ставит
	//   `regionWindow_OpenAndPathMove@0xD99290`, снимает `UI_resetAllWindowsAndLeave_5045`; в порту это
	//   `UAsdaNpcDialogSubsystem::IsDialogOpen()`, разбор — `Reports/craft/wave0-2.md` §1 В2). Ниже стоит
	//   проверка смерти (`IsSelfDead`) — поведение этой волной НЕ менялось, исправлена только подпись.
	if (IsSelfDead()) { ClearPendingNpcTalk(TEXT("персонаж мёртв")); return; }

	UAsdaNpcDialogSubsystem* Dlg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	if (!Dlg) { return; }

	if (IsInsideNpcTalkZone(Npc))
	{
		// Эталон: `if (getFlag_47551(localPlayer)) Net_SendActionPacket(...)` — стоп 4008 уходит
		//   ТОЛЬКО если персонаж действительно движется.
		if (IsMovingNow()) { StopMoveAndNotify(TEXT("разговор с NPC")); }
		ClearPendingNpcTalk(TEXT("уже в зоне разговора"));
		OpenNpcTalkOrCampWar(Npc);   // ★ единая точка: на карте осады вместо окна уходит 6723
		return;
	}

	// ★ 22.10.2026 (ревизия стана, ВАЖНО-1): под обездвиживанием подбегания нет и отложенный разговор НЕ взводится (иначе висел бы
	//   после стана). Плашка 1019 — канал клика по миру эталона. Разговор изнутри зоны (выше) — без хода, не гейтится.
	if (IsMoveInputLockedByState())
	{
		ClearPendingNpcTalk(TEXT("обездвижен"));
		AsdaPlankCannotMove(GetGameInstance());
		UE_LOG(LogAsdaPC, Log, TEXT("[NPC] обездвижен — подбегание к npcId=%d не начато, плашка 1019"), Npc->NetRenderId);
		return;
	}
	// Далеко → подбегание. ЭТАЛОН 1:1 (`ui_core.c:1174`): обычный click-to-move В ЦЕНТР NPC
	//   (`CGame_HandleGroundClickAttack(bag, npc[84], npc[85], npc[86], 1)`). Отступ D9 даёт не эта
	//   строка, а `NpcTalkTick`: он гасит бег на границе расширенного ящика (см. `kNpcZonePad`).
	const FVector NpcLoc = Npc->GetActorLocation();
	const FVector MyLoc = Ch->GetActorLocation();
	const FVector2D DstUe(NpcLoc.X, NpcLoc.Y);
	// ⚠ ПОРЯДОК ВАЖЕН: `RequestMoveTo` гасит ВСЕ отложенные намерения (в том числе наше),
	//   поэтому pending взводится ПОСЛЕ приказа движения — ровно как у отложенного подбора лута.
	RequestMoveTo(AsdaCoord::UeToGame2D(DstUe));
	bNpcTalkPending = true;
	PendingNpcRenderId = Npc->NetRenderId;
	PendingNpcActor = Npc;
	UE_LOG(LogAsdaPC, Log,
		TEXT("[NPC] подбегание к npcId=%d (renderId=%d): цель = ЦЕНТР NPC (%.0f,%.0f), дистанция %.0f, ")
		TEXT("зона ±%.0f (Чебышев), порог маршрута %.0f"),
		Npc->NpcId, Npc->NetRenderId, DstUe.X, DstUe.Y,
		FVector2D::Distance(FVector2D(MyLoc.X, MyLoc.Y), DstUe),
		kNpcZoneHalfXY + kNpcZonePad, kRouteArriveDist);
}

// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D5.
//   В эталоне отложенная цель `localPlayer[1].id5C` сбрасывается РОВНО В ОДНОМ месте —
//   `dialogOnConfirmClose@0xDA3BB0` при `a2 == 1`. Ни смерть, ни отмена движения, ни уход NPC из
//   видимости её не гасят, поэтому окно открывается «само» позже и невпопад: игрок кликнул NPC,
//   отвлёкся на бой или телепорт — и диалог всплывает, как только он случайно задел зону.
//   Здесь намерение снимается во всех перечисленных случаях; каждый вызов пишет ПРИЧИНУ в лог.
void AAsdaPlayerController::ClearPendingNpcTalk(const TCHAR* Why)
{
	if (!bNpcTalkPending) { return; }
	bNpcTalkPending = false;
	PendingNpcRenderId = -1;
	PendingNpcActor = nullptr;
	UE_LOG(LogAsdaPC, Log, TEXT("[NPC] отложенный разговор снят: %s"), Why);
}

// ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (требование владельца 2026-08-21).
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ: окно 570 закрывается `UI_resetAllWindowsAndLeave_5045@0xD995C0`, и все
//   его вызовы в срезе эталона просмотрены — это команды сценария 30001…31000
//   (`game_main.c:9942…10302`), посадка/эмоция и смерть (`entity_char.c:16621, 16900`) и выход из
//   мира (`charLeaveWorldCleanup@0xF8F710`, `entity_char.c:58478`). ДВИЖЕНИЯ среди них НЕТ:
//   в оригинале можно убежать от собеседника, а окно останется висеть, и цель тоже. Владелец
//   требует другого поведения — оно и реализовано здесь, ОДНОЙ точкой (закон «чинить в общей
//   точке, а не тремя копиями в трёх обработчиках»).
//
//   ПОЧЕМУ ЭТА ТОЧКА ОБЩАЯ. Все источники движения игрока сходятся ровно в двух местах:
//     · `StartRoutedMove` — клик по земле (`OnLeftClick` → `RequestMoveTo`), клик по мини-карте
//       (`MoveToGamePoint` → `RequestMoveTo`), подбор лута, подбегание к NPC, подход ради навыка
//       (`UAsdaSkillUseSubsystem` зовёт `StartRoutedMove` напрямую) и погоня за целью в бою;
//     · клавиатурная ветка `bMovingNow` в `PlayerTick` (WASD/стрелки).
//   Телепорт/варп идут не движением, а сменой карты (`UAsdaLoadManagerSubsystem`), где окно и так
//   закрывается вместе со сценой; отдельного «автобега» в порту нет.
//
//   ВЫДЕЛЕНИЕ снимается ТОЛЬКО с собеседника: если игрок кликнул по ДРУГОЙ цели (а клик по цели
//   тоже запускает движение), свежий выбор гасить нельзя.
// ★ Отменить путешествие к цели задания, если движение начал сам игрок.
//   Ход самого путешествия помечается `BeginTravelStep()` — такой приказ отмены не вызывает.
// ★ Остановка движения на время смены карты или канала. Пакет остановки при этом НЕ отправляется:
//   соединение уже новое, игрок на нём не опознан, и любой игровой пакет обрывает связь.
void AAsdaPlayerController::StopMovementForMapChange()
{
	CancelRoute();
	bMovingToPoint = false;
	ClearPendingNpcTalk(TEXT("смена карты"));
	CancelPendingSkillApproach(TEXT("смена карты"));
	if (AAsdaPlayerCharacter* Ch = GetChar())
	{
		Ch->SetAnimState(EAsdaPlayerState::Idle);
	}
	UE_LOG(LogAsdaPC, Log, TEXT("[переход] движение остановлено, маршрут сброшен"));
	ASDA_CTRACE(EAsdaCT::Exit, TEXT("смена карты: движение остановлено, маршрут сброшен (автоатака=%s)"), bAutoAttack ? TEXT("да") : TEXT("нет"));
	ASDA_CTRACE_END(TEXT("смена карты (портал)"));
}

void AAsdaPlayerController::CancelTravelIfManual(const TCHAR* Why)
{
	if (bTravelStepIssued)
	{
		bTravelStepIssued = false;      // это шаг самого путешествия — пропускаем
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaMinimapSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
	if (!Mini || !Mini->IsTravelRequested()) { return; }   // тот же флаг, метод уже был
	Mini->CancelTravelRequest();
	UE_LOG(LogAsdaPC, Log, TEXT("[автопуть] отменён: игрок сам взялся за управление (%s)"), Why);
}

void AAsdaPlayerController::NotifyPlayerStartedMoving(const TCHAR* Why)
{
	// Отложенное «подбежать и заговорить» снимается всегда — это прежнее поведение D5.
	ClearPendingNpcTalk(Why);

	// ★ И ПУТЕШЕСТВИЕ К ЦЕЛИ ЗАДАНИЯ тоже: игрок сам взялся за управление — значит передумал.
	//   Сюда сходятся клик по земле, клик по карте, подбор лута, подбегание к собеседнику.
	//   Шаги самого путешествия исключены флажком: подсистема карты помечает свой приказ
	//   `BeginTravelStep()` прямо перед вызовом, иначе автопуть отменял бы сам себя на первом шаге.
	CancelTravelIfManual(Why);

	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// ★★★ ОКНА NPC ЗАКРЫВАЮТСЯ ВМЕСТЕ С ДИАЛОГОМ. Эталон делает это одной функцией
	//   `UI_resetAllWindowsAndLeave_5045@0xD995C0`, которую зовут переходы состояния игрока
	//   («пошёл» — `ida.c:278273`), и она гасит ВСЕ окна NPC подряд, включая магазин бустеров
	//   (`boosterShopCloseWindow`). Правило оригинала — не дистанция до NPC, а сам факт движения.
	//   ⚠ Вызов стоит ДО проверки «диалог открыт»: магазин NPC живёт и после закрытия диалога
	//   (владелец: «отошёл от Пре-пре 19 — магазин бустеров не закрылся»).
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->CloseNpcWindowsOnPlayerMove(Why);
	}

	UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
	if (!Dlg || !Dlg->IsDialogOpen()) { return; }

	AAsdaNpcActor* Talker = Dlg->GetCurrentNpcActor();
	const int32 NpcId = Dlg->GetCurrentNpcId();

	// Закрытие — штатным закрывателем подсистемы: он сам ходит в `UAsdaWindowSubsystem`
	//   (`SetWindowVisible` + `RestoreAfterSceneExit`), чистит кэши страницы, списки выдачи/приёма,
	//   попапы 540/2406/599 и гасит анимацию собеседника. Повторный вход в `Close()` защищён
	//   собственным `if (!bDialogOpen) return;`, поэтому вызов идемпотентен.
	Dlg->Close();

	if (Talker && TargetActor.Get() == Talker)
	{
		ClearTarget();
	}
	UE_LOG(LogAsdaPC, Log, TEXT("[NPC-диалог] закрыт движением игрока (%s), npcId=%d, выделение %s"),
		Why, NpcId, (Talker && TargetActor.Get() == nullptr) ? TEXT("снято") : TEXT("оставлено"));
}

// Порт срабатывания из тика локального игрока (`Player_ExecuteSkillOnTarget@0x9F16D0`,
//   entity_char.c:18298/18639/18903): «есть отложенная цель И игрок в её AABB» → 4008 + окно.
void AAsdaPlayerController::NpcTalkTick()
{
	if (!bNpcTalkPending) { return; }
	// ★ D5: цель исчезла (деспавн/травел) либо мы умерли — намерение снимаем сами.
	AAsdaNpcActor* Npc = PendingNpcActor.Get();
	if (!Npc) { ClearPendingNpcTalk(TEXT("NPC исчез")); return; }
	if (IsSelfDead()) { ClearPendingNpcTalk(TEXT("персонаж мёртв")); return; }
	if (!IsInsideNpcTalkZone(Npc))
	{
		// ★ D5: путь кончился, а в зону не попали (упёрлись, сбили маршрут) — не висим вечно.
		if (!HasRoute() && !IsMovingNow())
		{
			// ★ ДИАГНОСТИКА (правка 2026-08-20): раньше здесь была голая причина без чисел, и по
			//   живому логу владельца нельзя было понять, НА СКОЛЬКО не дошли. Теперь печатаются
			//   обе координаты и обе метрики — этого хватило бы, чтобы поймать дефект сразу.
			const AAsdaPlayerCharacter* C = GetChar();
			const FVector NL = Npc->GetActorLocation();
			const FVector PL = C ? C->GetActorLocation() : FVector::ZeroVector;
			UE_LOG(LogAsdaPC, Warning,
				TEXT("[NPC] не дошли до npcId=%d: игрок UE(%.0f,%.0f,%.0f) NPC UE(%.0f,%.0f,%.0f) ")
				TEXT("Чебышев=%.0f евклид=%.0f при зоне ±%.0f, высота зоны 0…%.0f"),
				Npc->NpcId, PL.X, PL.Y, PL.Z, NL.X, NL.Y, NL.Z,
				FMath::Max(FMath::Abs(PL.X - NL.X), FMath::Abs(PL.Y - NL.Y)),
				FVector2D::Distance(FVector2D(PL.X, PL.Y), FVector2D(NL.X, NL.Y)),
				kNpcZoneHalfXY + kNpcZonePad, NpcAabbHeight(Npc));
			ClearPendingNpcTalk(TEXT("движение закончилось вне зоны разговора"));
		}
		return;
	}
	// Дошли: сначала стоп (эталон шлёт `Net_SendActionPacket` = 4008), потом окно.
	//   ★ Стоп происходит НА ГРАНИЦЕ расширенного ящика — это и есть отступ D9 «встать перед
	//     собеседником» (цель маршрута при этом эталонная, центр NPC).
	StopMoveAndNotify(TEXT("подошли к NPC"));
	AAsdaNpcActor* Target = Npc;
	ClearPendingNpcTalk(TEXT("подошли — открываем окно"));
	OpenNpcTalkOrCampWar(Target);
}

// ★★★ ЕДИНАЯ ТОЧКА «мы у NPC — что дальше»: развилка эталона перед открытием окна разговора.
//   Зовётся ИЗ ДВУХ мест: `StartNpcTalk` (кликнули, уже стоя в зоне) и `NpcTalkTick` (подбежали).
//   Раньше открытие окна было продублировано в обоих — при добавлении развилки CampWar копия
//   немедленно разъехалась бы (закон «одна точка на факт»).
void AAsdaPlayerController::OpenNpcTalkOrCampWar(AAsdaNpcActor* Target)
{
	if (!Target) { return; }

	// ★★★ РАЗВИЛКА ЭТАЛОНА ПЕРЕД ОТКРЫТИЕМ ОКНА (строка 55 реестра отложенного, закрыта 2026-08-20).
	//   `input.c:331`, `ui_core.c:1119`, `entity_char.c:18937` — все три точки клика по NPC устроены
	//   одинаково и ВНУТРИ проверки «игрок внутри ящика NPC» (`bboxContainsPoint`), то есть ровно
	//   здесь, а не в момент клика:
	//       if (getField3036(&g_pLocalPlayer) == 19) { v = sub_93CD7A(npc); netSendCampWarJoin(g_pCampWarClan, v); }
	//       else if (npc[182] == 5) NPC_SpecialAction_Type5(...);      // доска рейдов — уже есть в OpenForNpcId
	//       else { uiPanelSelectTab(npc,1,0); NPC_OpenWindow(...); }   // обычный разговор
	//   На карте CampWar (mapId 19) окно разговора НЕ открывается вообще — вместо него уходит
	//   пакет 6723 «вступить в осаду». Раньше порт этой развилки не знал и открывал окно.
	//   ★ 30.10.2026: побочный эффект эталона (пометка `слот+0x1C = 1` в списке точек CampWar `g_pCampWarClan+0x242C`,
	//     страйд 64, поиск `findSlotIndexById@0xCA76F0`) теперь у боевой фазы — `UAsdaCampWarBattleSubsystem::RequestCapture`.
	//     ★ ДЕФЕКТ ОРИГИНАЛА (на будущее, когда список появится): `findSlotIndexById` возвращает −1
	//     при промахе, и эталон результат НЕ проверяет — пишет байт по `this + 0x2408`. Нужен
	//     guard `slot >= 0` с пометкой исправления (закон fix-original-defects).
	const int32 MapId = GetCurrentMapIdForCampWar();
	if (MapId == kCampWarMapId)
	{
		UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (Net && WS)
		{
			// Фракция приходит пакетом 6713; −2 = пакет ещё не приходил, −1 = фракции нет.
			//   Эталон шлёт содержимое поля КАК ЕСТЬ, поэтому и мы не подставляем ничего своего —
			//   но пишем в лог, если значение ещё не получено: иначе такой пакет молча уйдёт мимо.
			if (WS->GetSelfCampId() < 0)
			{
				UE_LOG(LogAsdaPC, Warning,
					TEXT("[CampWar] вступление в осаду: своя фракция ещё не получена (SelfCampId=%d, пакет 6713 не приходил) — шлём как есть, как эталон"),
					WS->GetSelfCampId());
			}
			// ★ 30.10.2026 (боевая фаза): `netSendCampWarjoin` целиком — армия +0xB7C и пометка слота точки `slot+0x1C = 1`.
			UAsdaCampWarBattleSubsystem* CwB = GetGameInstance()->GetSubsystem<UAsdaCampWarBattleSubsystem>();
			if (CwB && UAsdaCampWarBattleSubsystem::IsBattleOn()) { CwB->RequestCapture(Target); }
			else { Net->SendCampWarJoin6723(Net->SelectedCharId, WS->GetSelfCampId(), Target->NetRenderId); }
		}
		else
		{
			UE_LOG(LogAsdaPC, Warning, TEXT("[CampWar] вступление в осаду: нет сетевой/мировой подсистемы — пакет 6723 не отправлен"));
		}
		return;   // окно разговора на карте осады НЕ открывается (ветка эталона `if`, без `else`)
	}

	if (UAsdaNpcDialogSubsystem* Dlg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
	{
		Dlg->OpenForNpc(Target);
	}
}

// Текущая карта для развилки CampWar. Эталон читает `getField3036(&g_pLocalPlayer)` — номер карты
//   у локального игрока; в порту единственный держатель этого числа — менеджер загрузки
//   (`UAsdaLoadManagerSubsystem::CurrentMapId`, он же ставится при травеле и при 4067).
//   Отдельного поля у контроллера НЕ заводим (закон «одна точка на факт»).
int32 AAsdaPlayerController::GetCurrentMapIdForCampWar() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
		{
			return LM->CurrentMapId;
		}
	}
	return -1;
}

// ★ Выход из боевой стойки: по пакету сервера 4205 (netTradeCancelResetCharState) или по кулдауну.
void AAsdaPlayerController::ExitCombatStance()
{
	ASDA_CTRACE(EAsdaCT::State, TEXT("боевая стойка снята (%s, автоатака=%s) → мирный покой"),
		(CombatStanceUntil > 0.0 && FPlatformTime::Seconds() >= CombatStanceUntil) ? TEXT("истёк кулдаун стойки") : TEXT("движение/клик по земле/сервер"),
		bAutoAttack ? TEXT("да") : TEXT("нет"));
	// Бой не идёт — выход из стойки и есть конец цикла (после смерти цели цикл держался ради трупа/награды).
	if (!bAutoAttack) { ASDA_CTRACE_END(TEXT("выход из боевой стойки")); }
	CombatStanceUntil = 0.0;
	if (AAsdaPlayerCharacter* Ch = GetChar()) { Ch->SetCombatStance(false); }
}

// ★ Ответ сервера на замах получен (4029/4027): серия ударов раздаётся мир-подсистемой по кадрам,
//   следующий замах шлём по её окончании (эталонный ритм — не чаще, чем сервер отвечает).
// ★★★ РИТМ ОТ АНИМАЦИИ. Ключ отката: 0 = прежний ритм по ответу сервера.
static ASDA_DEV_CVAR(int32, CVarAsdaRhythmFromAnim,
	TEXT("asda.Combat.RhythmFromAnim"), 1,
	TEXT("1 = следующий удар шлётся по завершении прохода клипа (как в оригинале); 0 = по ответу сервера (бой с мобом). ")
	TEXT("PvP (4203) от ключа не зависит: ритма «по ответу» у PvP нет — удар всегда в точке отправки замаха."),
	ECVF_Default);

bool AAsdaPlayerController::RhythmFromAnimEnabled()
{
	return CVarAsdaRhythmFromAnim.GetValueOnGameThread() != 0;
}

// ★★★ ПРОХОД КЛИПА ЗАВЕРШЁН — ШЛЁМ СЛЕДУЮЩИЙ УДАР (1:1 оригинал, `skill_buff.c:762-793`).
//   Там отправка стоит внутри тика замаха и обставлена ровно двумя условиями: цель ещё не мертва
//   (`Entity_GetActionState != 10`) и она в пределах дальности — иначе персонаж уходит в боевую
//   стойку (`CharFx_AutoSabzil_State(…, 20)`), а не бьёт в пустоту.
void AAsdaPlayerController::OnSwingCycleCompleted()
{
	// ★★★ 01.11.2026 [ОРИГИНАЛ] (`asda.Skill.ApproachOverAttack`): идёт подход ради навыка (эталонное 102 вместо атаки 5) — конец
	//   прохода замаха не шлёт ни 4028, ни 4203: удар «на бегу» сервер отбрасывает, а у эталона тика замаха в 102 нет.
	if (AsdaSkillApproachOverAttackOn())
	{
		const UAsdaSkillUseSubsystem* SUa = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		if (SUa && SUa->HasSkillApproach())
		{
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("конец прохода: идёт подход ради навыка (102) — заявки атаки нет"));
			return;
		}
	}
	// ★ 18.10.2026 (ревизия Ф4, мелочь 2): точка отправки PvP-замаха — 4203 — РАНЬШЕ гейта ключа `RhythmFromAnim`: ключ откатывает ритм
	//   боя с мобом к «по ответу сервера» (тик боя шлёт 4028 после ответа), а у PvP такого пути нет — при ключе 0 4203 не уходил вовсе.
	//   [ОРИГИНАЛ] 4203 шлёт тик замаха (entity_char.c:27265/27280/27555), т.е. всегда от анимации. Без ключа серверных часов точки
	//   отправки внутри прохода не ставятся — удар уходит на конце прохода (`OnAttackClipEnd`, `bEndIsSend`).
	if (bPvpAttack && bAutoAttack && bCombatStarted && !IsSelfDead())
	{
		ASDA_CTRACE(EAsdaCT::Pvp, TEXT("конец прохода/точка отправки замаха по userIdx=%d → решение PvP (4203)"), PvpTargetUserIdx);
		PvpOnSendPoint(); return;   // ★ 17.10.2026 (Ф4)
	}
	if (!RhythmFromAnimEnabled())
	{
		ASDA_CTRACE_V(EAsdaCT::Swing, TEXT("конец прохода: ритм «по ответу» (asda.Combat.RhythmFromAnim 0) — удар шлёт тик боя"));
		return;
	}
	if (!bAutoAttack || !bCombatStarted)
	{
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("конец прохода: боя нет (автоатака=%s, серия=%s) — заявки нет"),
			bAutoAttack ? TEXT("да") : TEXT("нет"), bCombatStarted ? TEXT("да") : TEXT("нет"));
		return;
	}
	if (IsSelfDead())
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("конец прохода: свой персонаж мёртв — заявки нет"));
		return;
	}
	AAsdaMobActor* M = Cast<AAsdaMobActor>(TargetActor.Get());
	AAsdaPlayerCharacter* Ch = GetChar();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!M || !Ch || !Net || !Net->bInGameConnected)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("конец прохода: %s — заявки нет"),
			!M ? TEXT("цель не моб/нет цели") : (!Ch ? TEXT("нет персонажа") : TEXT("нет сети/не в мире")));
		return;
	}
	// Цель уже засчитана мёртвой — новых заявок не шлём (эталонная проверка состояния 10).
	if (M->IsDead() || M->bServerDead)
	{
		ASDA_CTRACE(EAsdaCT::Swing, TEXT("конец прохода: цель rid=%d '%s' %s — новой заявки нет (эталон: состояние 10)"),
			M->NetRenderId, *M->MobName, M->IsDead() ? TEXT("мертва") : TEXT("обречена сервером (HP 0 в ответе)"));
		return;
	}

	const FVector2D Cur  = Ch->GetGamePosXZ();
	const FVector2D DstG = AsdaCoord::UeToGame2D(M->GetActorLocation());   // ★ Аудит №3 (B1): UE→game
	const float Dist = FVector::Dist2D(Ch->GetActorLocation(), M->GetActorLocation());
	// Дальность берём посчитанную боевым тиком (он идёт постоянно и держит её свежей).
	//   Нет её для этой цели — не гадаем: пропускаем такт, тик посчитает и следующий проход уйдёт.
	if (LastCombatRangeRid != M->NetRenderId || LastCombatRangeOut <= 0.f)
	{
		ASDA_CTRACE(EAsdaCT::Range, TEXT("конец прохода rid=%d: дальность для этой цели ещё не посчитана (есть для rid=%d, %.0f) — такт пропущен"),
			M->NetRenderId, LastCombatRangeRid, LastCombatRangeOut);
		return;
	}
	if (Dist > LastCombatRangeOut)
	{
		ASDA_CTRACE(EAsdaCT::Range, TEXT("конец прохода rid=%d: цель ушла, дист %.0f > удержание %.0f — не бьём, подход сделает тик боя"),
			M->NetRenderId, Dist, LastCombatRangeOut);
		// Цель ушла — как в оригинале: не бьём, переходим в боевую стойку, подход сделает AttackTick.
		bInCombatStance = false;
		return;
	}
	// ★★★ 15.10.2026 СЕРВЕРНЫЕ ЧАСЫ: 4028 не раньше длительности, которую сервер посчитал при приёме прошлого удара
	//   (Combo.lst/Skill.lst, баффы — `UAsdaAttackRhythmSubsystem`). Темп замаха подогнан так, что точка отправки приходит
	//   ровно к этому моменту; остаток в доли кадра добирается отложенной отправкой. Цикл замаха снят (навык) — не шлём.
	// ★★★ Ревизия СРЕДН-2: удар уходит к АБСОЛЮТНОМУ дедлайну (`SendAttackNext4028At`, поток точной отправки), а не «в
	//   ближайшем кадре»: точка замаха приходит за кадр до дедлайна (`BeginSendSegment`), остаток добирает поток. Так
	//   интервал не зависит от FPS. Точка пришла намного раньше (рывок, ускорение клипа на пределе) — повтор таймером за
	//   кадр до дедлайна.
	double DeadlineS = 0.0;
	if (UAsdaAttackRhythmSubsystem::IsEnabled())
	{
		if (!Ch->IsAttackLoopAlive())
		{
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("конец прохода rid=%d: цикл замаха снят (навык/стоп) — 4028 не шлём, отложенный снят"), M->NetRenderId);
			GetWorldTimerManager().ClearTimer(DeferredAttackSendTimer); return;
		}
		if (Net->HasPending4028())
		{
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("конец прохода rid=%d: удар 4028 уже поставлен к дедлайну — повтора нет"), M->NetRenderId);
			return;   // удар уже поставлен к дедлайну
		}
		const UAsdaAttackRhythmSubsystem* Rh = GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>();
		const double NowS = FPlatformTime::Seconds();
		const double Earliest = Rh ? Rh->EarliestNext4028S() : 0.0;
		if (Earliest > NowS + kTimedSendAheadS)
		{
			const double FrameS = FMath::Clamp((double)FApp::GetDeltaTime(), 0.004, 0.1);
			GetWorldTimerManager().SetTimer(DeferredAttackSendTimer, this, &AAsdaPlayerController::OnSwingCycleCompleted,
				FMath::Max(0.001f, (float)(Earliest - NowS - FrameS)), false);
			UE_LOG(LogAsdaPC, Verbose, TEXT("[ритм] точка отправки раньше часов на %.1f мс — повтор за кадр до дедлайна"), (Earliest - NowS) * 1000.0);
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("точка отправки rid=%d раньше серверных часов на %.1f мс — повтор таймером через %.1f мс (за кадр до дедлайна)"),
				M->NetRenderId, (Earliest - NowS) * 1000.0, FMath::Max(0.001, Earliest - NowS - FrameS) * 1000.0);
			return;
		}
		GetWorldTimerManager().ClearTimer(DeferredAttackSendTimer);
		DeadlineS = (Earliest > NowS) ? Earliest : 0.0;
	}
	Ch->FaceGameDir(DstG - Cur);
	Ch->AttackFaceTarget = M;
	const FVector2D PktPos = AttackPacketPos(Cur, DstG);   // ★ mobpos_swing: своя позиция (эталон, netcap)
	ASDA_CTRACE(EAsdaCT::Send, TEXT("удар 4028 rid=%d '%s' HP %d/%d: %s, позиция (%.0f,%.0f), дист %.0f ≤ удержание %.0f"),
		M->NetRenderId, *M->MobName, M->CurHp, M->MaxHp,
		*(DeadlineS > 0.0 ? FString::Printf(TEXT("к дедлайну через %.1f мс (поток точной отправки)"), (DeadlineS - FPlatformTime::Seconds()) * 1000.0)
		                  : FString(TEXT("сразу"))),
		PktPos.X, PktPos.Y, Dist, LastCombatRangeOut);
	if (DeadlineS > 0.0) { Net->SendAttackNext4028At(M->NetRenderId, PktPos.X, PktPos.Y, DeadlineS); }
	else { Net->SendAttackNext4028(M->NetRenderId, PktPos.X, PktPos.Y); }
	// ★ 17.10.2026: удар 4028 ушёл — окно «наш замах в полёте» до оценки попадания (см. то же у 4026 в `AttackTick`).
	//   Подтип оружия — из записи надетого, тем же путём, что у боевого тика и у ответа сервера.
	if (UAsdaWorldSubsystem* WSa = GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
	{
		int32 WSub = -1;   // подтип надетого оружия (−1 — нет записи)
		if (const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>())
		{
			if (const FAsdaItemRecord* R = (Net->SelfWeaponItemId > 0) ? IT->GetItem(Net->SelfWeaponItemId) : nullptr) { WSub = R->ItemSubType; }
		}
		WSa->ArmOurImpactWindowFromSend(Ch, M, WSub, TEXT("удар 4028"));
	}
	// Ответ сервера больше не задаёт ритм — только приносит очередь результатов. Гейт ожидания
	//   снимаем, а страховочный таймер отодвигаем, чтобы `AttackTick` не послал второй удар.
	bWaitingCombatAck = false;
	NextAttackTickTime = FPlatformTime::Seconds() + (double)FMath::Max(0.2f, AttackPeriod);
}
