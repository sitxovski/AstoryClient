// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ХОД КЛАВИШАМИ ПО СХЕМЕ ОРИГИНАЛА (27.09.2026, решение владельца). Ключ `asda.Move.KeyboardEtalon`.
//
// Разбор оригинала — `Reports/owner_0927/wasd_original.md`, реализация и проверки — `wasd_impl.md`.
// Эталон: `updatePlayerMovement@0x95CDF0` (`AsdaStory_game.c:8682-9020`, `RE/etalon/math_geom.c:371`), вызов из главного
//   цикла `game_main.c:1506` (`AsdaStory_game.c:9558`), вход клика `CGame_HandleGroundClickAttack@0x996480` (game.c:31618),
//   «можно ли идти» `Party_CanInviteCheck@0x95C870` (миснейм, game.c:8536), окна `uiCanOpenWindowGate@0xECA560`,
//   сидение `charToggleSitStand@0xC54A70`. Сервер — `CharMove@0x544EB0` / `CharStop@0x545C70` (`GameServer.c:124434/124657`).
//
// ГЛАВНОЕ ОТЛИЧИЕ ОТ ПРЕЖНЕЙ СХЕМЫ ПОРТА (ключ 0): локального хода НЕТ. Клавиша шлёт 4006 на точку pos + dir·800
//   тем же входом, что клик по земле, и персонаж трогается только по гранту 4007 (`UAsdaWorldSubsystem::
//   ApplySelfMoveGrantEtalon` → `NetMoveFromActor`). Повтор: 10 мс стоя с W/S, 250 мс при изменении вектора, 1000 мс
//   иначе. Отпускание — покой сразу, 4006 cur→cur, 4008. Мёртвой зоны и мгновенного старта порта при ключе 1 нет
//   (решение владельца: это плата за 1:1).
//
// ЧИСЛЕННАЯ ПРОВЕРКА ВЕКТОРА (открытое место отчёта, закрыто 27.09): `updatePlayerMovement` берёт матрицу вида
//   (`GetTransform(D3DTS_VIEW)`) и тут же ОБРАЩАЕТ её — `call 0x9238CA` по адресу 0x95CFF9 (thunk → `[0x15B5FD4]` =
//   слот 19 таблицы D3DX = `D3DXMatrixInverse`, аргументы `(&view, NULL, &view)`); декомпилятор показывал это как
//   `callIndirectViaGlobalPtr()` без аргументов. После обращения строки матрицы — оси камеры в мире:
//   (m[0],m[2]) = правая ось (единичная, горизонтальная при нулевом крене), (m[8],m[10]) = −взгляд.
//   Камера — `cameraSetPosEulerOrient@0x1127FB0`: R = Z(крен)·X(наклон)·Y(курс) (`mathBuildRotationMatrixXYZ@0x11D2660`,
//   порядок умножений снят с байт-кода; X/Y опознаны по месту `fchs` в реализациях D3DX). Прогон формулы (numpy):
//   W-вектор = взгляд по горизонтали × cos(наклон), угол к горизонтальному взгляду 0.0000° при любом курсе;
//   правая ось единичная и ⟂ взгляду. То есть вектор НЕ повёрнут (гипотеза отчёта про поворот снята), а
//   укорочен: точка хода = pos + 800·cos(наклон) по взгляду (при наклоне −20° — 752 ед., −45° — 566, −70° — 274);
//   по диагонали со стрейфом длина √(cos²+1). Порт переносит ДЛИНУ, не только угол.
#include "World/AsdaPlayerController.h"
#include "World/AsdaAttackExit.h"   // ★ 01.11.2026: 4024/400 — выход своего из действия атаки (AsdaLeaveAttackAction)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCombatTrace.h"            // журнал боя (редактор/Development)
#include "World/AsdaCoord.h"                    // перевод UE ↔ игра, тайл 100
#include "World/AsdaKeybindConfig.h"            // раскладка: свободны ли стрелки (LookupCmd)
#include "AsdaNetSubsystem.h"                   // 4006/4008/4024, бит 2
#include "Npc/AsdaMobActor.h"                   // цель атаки для поля +25
#include "UI/AsdaChatWindowSubsystem.h"         // фокус ввода чата (`sub_91F216(g_pChatWnd)`)
#include "UI/AsdaEffectsSubsystem.h"            // список состояний: оковы, запись 547
#include "UI/AsdaMinimapSubsystem.h"            // ★ 08.11.2026: снятие автопути на фронте удержания действием UI
#include "UI/AsdaNotice.h"                      // строки чата/плашки по loc-id
#include "UI/AsdaWarehouseSubsystem.h"          // свиток возврата склада (+0x5734)
#include "UI/AsdaPremiumPostSubsystem.h"        // элитный почтовый ящик 69000 (`sub_9384DC(g_pInventoryBagWindow)`)
#include "UI/AsdaPremiumGachaSubsystem.h"       // сцена премиум-гачи (`sub_922D5D`)
#include "UI/AsdaPremiumShopSubsystem.h"        // элитный магазин (поле 2075 `g_pShopWnd`)
#include "UI/AsdaHairShopSubsystem.h"           // парикмахерская (`CObject_GetField166(g_pHairShopWnd)`)
#include "UI/AsdaBoosterPanelSubsystem.h"       // окно бустера 10050 (`CObject_GetField254(g_pInventoryWnd)`)
#include "UI/AsdaSkillUseSubsystem.h"           // отмена отложенного подхода ради навыка
#include "UI/AsdaCastBarSubsystem.h"            // состояния 13/14/24/27 (полоса каста)
#include "UI/AsdaExchangeSubsystem.h"           // аукцион 13000 (`CItemMediation+0x4C0`)
#include "UI/AsdaClanSubsystem.h"               // окно создания клана 14400
#include "UI/AsdaWarShopSubsystem.h"            // военный магазин 67200 (нотис 110611)
#include "UI/AsdaBoosterShopSubsystem.h"        // магазин бустеров 15300 (нотис 99003)
#include "UI/AsdaTradeStateSubsystem.h"         // ★ ПД-2 (29.09.2026): окно обмена 2008 открыто (`CExchange+0x2C`, нотис 9568)
#include "UI/AsdaSelfStoreSubsystem.h"          // ★ Л5 торговли: фокус в поле чата лавки 23403 (гейт окон h)
#include "UI/AsdaWindowSubsystem.h"             // видимость окон 9100 / 8300
#include "GameFramework/SpringArmComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "World/AsdaOsWindow.h"                 // ★ R5-fix6: физическая мышь — только при настоящем окне ОС
#include "HAL/IConsoleManager.h"
#include "World/AsdaSkillCastLock.h"   // ★ 05.11.2026: фаза своего навыка (25/24/14/13/27) для состояния действия
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaKbMove, Log, All);

// ★ 29.10.2026 (ключ asda.Map.DestMarkClear, разбор — в AsdaMinimapSubsystem.cpp): снять метку цели автобега на картах.
void AsdaDestMarkClear(UGameInstance* GI, const TCHAR* Why);

// ★ Ключ схемы. 1 = оригинал (умолчание, решение владельца 27.09), 0 = прежняя схема порта дословно (откат).
static ASDA_DEV_CVAR(int32, CVarAsdaMoveKeyboardEtalon,
	TEXT("asda.Move.KeyboardEtalon"), 1,
	TEXT("Ход клавишами (WASD): 1 = как в оригинале — персонаж трогается только по разрешению сервера (4007), запрос ")
	TEXT("хода уходит на точку в 8 м по взгляду камеры (повтор 10/250/1000 мс), отпускание — стоп через сервер, окна и ")
	TEXT("состояния оригинала запрещают ход; 0 = прежняя схема порта (идти сразу, мёртвая зона поворота)."),
	ECVF_Default);

// ★ [ДЕФЕКТ ОРИГИНАЛА ИСПРАВЛЕН] Отказной нотис `Party_CanInviteCheck` оригинал пишет в чат КАЖДЫЙ КАДР, пока клавиша
//   держится (`CChat_AddTokenizedLines` без защиты от повтора, `game.c:8546-8604`): лента за секунду забивается десятками
//   одинаковых строк. 1 = одна строка на удержание (пока ход запрещён тем же отказом), 0 = каждый кадр, как оригинал.
static ASDA_DEV_CVAR(int32, CVarAsdaMoveKeyboardNoticeOnce,
	TEXT("asda.Move.KeyboardNoticeOnce"), 1,
	TEXT("Отказ хода клавишами (открыто окно, склад, бустер…): 1 = одна строка в чат на удержание клавиши; ")
	TEXT("0 = строка каждый кадр, как в оригинале."),
	ECVF_Default);

// ★ Канал отказов своего 4007 (обработчик `Recv_EntityMove`, читает `UAsdaWorldSubsystem::OnNetEntityMove`): 1 = плашка 221
//   без журнала и звука, как оригинал; 0 = прежний маршрут порта (системный чат + error.wav). Ревизия WASD, МЕЛК-2.
static ASDA_DEV_CVAR(int32, CVarAsdaMoveRefusePlank,
	TEXT("asda.Move.RefusePlank"), 1,
	TEXT("Отказ сервера в ходе (4007: «недоступная область», «слишком далеко», …): 1 = строка в плашке над чатом без ")
	TEXT("звука, как в оригинале; 0 = строка в системном чате со звуком ошибки (прежнее поведение порта)."),
	ECVF_Default);

// ★★ 22.10.2026 — БАГ-РЕПОРТ ИГРОКОВ «WASD неотзывчив / телепорт / персонаж идёт вперёд после смены W на A/D»
//   (разбор: `Reports/wasd_2026-10-22/REPORT.md`; журналы игроков Player4 19.10 19:03:15, Player38 19.10 19:32:02/19:32:14,
//   Player17 21.10 21:09). Два дефекта ОРИГИНАЛА в `updatePlayerMovement@0x95CDF0` (math_geom.c:630-645), оба воспроизведены
//   самотестом `asda.Move.SelfTestKeyboard` (KB21–KB24, пара «ключ 0 = как оригинал»):
//
//   [ИСПРАВЛЕНИЕ] 1 — `asda.Move.KbDirPending`. Эталон сравнивает вектор с вектором ПРОШЛОГО КАДРА (`g_vMovePrevTarget` пишется
//     на каждом изменении, math_geom.c:630-636), поэтому интервал 250 мс живёт ровно один кадр — кадр смены. Если смена пришлась
//     в первые 250 мс после прошлого 4006 (или на бит 2 «запрос в пути», попытка при котором съедает таймер, :645), со следующего
//     кадра снова 1000 мс: новое направление уходит только через ~1 с, всё это время персонаж бежит по старому гранту.
//     Исправление: 250 мс, пока текущий вектор отличается от вектора последнего УШЕДШЕГО 4006; попытка при бите 2 таймер не
//     съедает (уходит в первый кадр после ответа сервера). Частота 4006 не выше эталонной (≤ 4/с, бит 2 — не больше одного в пути).
//
//   [ИСПРАВЛЕНИЕ] 2 — `asda.Move.KbIntentStop`. Ветка отпускания эталона (:675-701) срабатывает, только когда НЕ нажато ничего.
//     W/S отпущены, а A/D (без ПКМ — это разворот камеры) держится → «нажата любая», вектор 0: стопа нет, персонаж добегает
//     старый грант (до 800·cos(наклон) ед.), а через 1 с 4006 «в свою точку» даёт грант назад — рывок/снап («грант опоздал»).
//     То же при A+D и при W+S (эталон сбрасывает таймер каждый кадр — 4006 не уходит вовсе, персонаж добегает грант).
//     Исправление: «намерения хода нет» → покой сразу + 4008 в своей точке (как сервер принимает отпускание), опоздавший грант
//     прежнего направления не исполняется (`IsKeyboardEtalonMoveActive`); прочее (разворот камеры, 4006 «в свою точку» раз в
//     1000 мс) — как у эталона.
static ASDA_DEV_CVAR(int32, CVarAsdaMoveKbDirPending,
	TEXT("asda.Move.KbDirPending"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 22.10] Смена направления клавишами: 1 = новое направление уходит серверу не позже 250 мс (и сразу после ")
	TEXT("ответа, если запрос был в пути); 0 = как оригинал (смена в первые 250 мс после запроса ждёт до 1 с)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarAsdaMoveKbIntentStop,
	TEXT("asda.Move.KbIntentStop"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 22.10] W/S отпущены при зажатой A/D (без ПКМ), A+D, W+S: 1 = персонаж останавливается сразу (4008); ")
	TEXT("0 = как оригинал (добегает прежний запрос хода до 8 м, затем откат)."),
	ECVF_Default);

namespace AsdaKbEtalon
{
	// Эталонная база кадра `flt_15B5B18` = 31.25 мс: разворот A/D = 0.05 рад на базу.
	static constexpr double kFrameBaseMs = 31.25;
	// Точка хода — 800 единиц по вектору направления (game.c:8971 `Vector3_Scale(dir, 800.0)`).
	static constexpr float kStepUnits = 800.f;
	// Цвет строк отказа/автобега оригинала: −11514 = 0xFFFFD306 (ARGB), золотой.
	static const FColor kNoticeColor(0xFF, 0xD3, 0x06, 0xFF);
	// Нотисы (loc по обработчику: `Party_CanInviteCheck` и `updatePlayerMovement`, не по семье).
	static constexpr int32 kLocTradeInvite   = 9568;    // `g_bPartyInviteFlag` = окно обмена 2008 открыто (`CExchange+0x2C`)
	static constexpr int32 kLocShopOpen      = 12917;   // `g_pPartyTargetObj` (элитный магазин) / `g_pPackageStampWindow`
	static constexpr int32 kLocTooltipWnd    = 110611;  // `sub_93C7EE(g_pItemTooltipWindow)`
	static constexpr int32 kLocModalNotice   = 99003;   // `g_bModalNoticeActive`
	static constexpr int32 kLocHairShop      = 13010;   // `CObject_GetField166(g_pHairShopWnd)`
	static constexpr int32 kLocBooster       = 13100;   // `CObject_GetField254(g_pInventoryWnd)` / `g_pExtractShopWin`
	static constexpr int32 kLocWarehouse     = 10305;   // `CObject_GetField5581(g_pWarehouseWnd)` (свиток возврата)
	static constexpr int32 kLocAutoRunOn     = 110507;  // автобег включён
	static constexpr int32 kLocAutoRunOff    = 110508;  // автобег выключен
	// Состояние действия эталона «идёт» (`+47551 == 1`).
	static constexpr int32 kStateMove = 1;

	// Задержка автоповтора клавиатуры Windows (мс): `SPI_GETKEYBOARDDELAY` 0..3 → 250..1000. Эталон получает повторные
	//   WM_KEYDOWN с этим шагом и снова взводит флаг движения (например, после сброса флагов ЛКМ).
	static double RepeatDelayMs()
	{
#if PLATFORM_WINDOWS
		static double Cached = -1.0;
		if (Cached < 0.0)
		{
			int Delay = 1;
			if (!::SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &Delay, 0)) { Delay = 1; }
			Cached = 250.0 * (FMath::Clamp(Delay, 0, 3) + 1);
		}
		return Cached;
#else
		return 500.0;
#endif
	}
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★ 22.10.2026 — ЖЁСТКАЯ ОСТАНОВКА СВОЕГО ПЕРСОНАЖА ПОД «СТАНОМ» (слово владельца: «когда накладывается любой эффект типа
//   стан — он тоже должен намертво тормозиться»). Ключ `asda.Move.StunHardStop` (1; 0 — прежнее дословно).
//
//  ПОЛНЫЙ ПОТОК ОРИГИНАЛА (реверс 22.10):
//   • Сервер, наложение состояния `sub_484250@0x484250` (GameServer.c:52780-52794): запись состояния в `CUser+0x552E+24·id`, затем
//     обработчик `gap_C[4·id+156]` (таблица GameServer.c:47430-47443). Для id 0/1/2/3 (FREEZE_1..4: лёд, камень, паутина, оглушение),
//     8 (SLEEP), 9 (FREEZE_5: тайная паутина), 11 (SELF_ICE), 12 (GUARDIAN_STUN), 13 (GUARDIAN_FREEZE) обработчик, если игрок идёт
//     (`sub_523ED0`), снимает его со стека хода `sub_523EA0` → `Return_UserMoveStack@0x542520` — МОЛЧА, без пакета клиенту
//     (GameServer.c:54352-54604). Страх 4 (`ProcessState_SKILL_STATE_FEAR`) — сервер сам ведёт игрока (грант «Паника»).
//     Новый 4006 под состояниями 0,1,2,3,4,8,9,11,13 (`sub_484D20@0x484D20`, GameServer.c:52910) — result 6 (`CharMove`, :124511).
//     4008 под состоянием сервер принимает (в `CharStop` гейта нет; P57 о нём только пишет журнал).
//   • Клиент, наложение (`cChar_restartSkillEffectByType@0xA2AD60` / `netApplySkillHitResultPacket`, network.c:44999/45238): запись
//     в список состояний + обработчик эффекта `this+52304+id` (`charSkillFxHandlerTableInit@0x9AB290`: freez101/201/301, shocku01,
//     sleep01, freeze301 — только .jeb и звук). СВОЕГО ПЕРСОНАЖА ОРИГИНАЛ НЕ ОСТАНАВЛИВАЕТ: главный цикл не зовёт тик клавиш
//     (`game_main.c:1506`, `listHasAnyOfSlotTypes` 0,1,2,3,14,9,4,8) — ни нового 4006, ни ветки отпускания; клик по земле —
//     плашка 1019 (`Input_WorldClickHandler`, input.c:545); идущий грант персонаж ДОБЕГАЕТ, в конце — 4008 в точке прибытия.
//     Сервер же стоит в точке наложения: клиент впереди больше 1000 мс пути → «CharStop Revision Pos» → 4009, снап назад; ход
//     маршрута/автопути под станом шлёт 4006 → result 6 (плашка 1019, без остановки — `Recv_EntityMove` case 6).
//  [ИСПРАВЛЕНИЕ] Порт: на фронте наложения — остановка на месте (модель и логика), маршрут/подход/отложенный разговор/подбор сняты,
//   4008 в своей точке (сервер стоит в точке наложения — 4008 ставит его в точку клиента, `CharStop` LABEL_35; после снятия первый
//   4006 из этой точки совпадает точно, снапа нет). Пока состояние держится — ни клавиш, ни клика (плашка 1019, как эталон), ни
//   отрезков маршрута (`SendRouteLeg`), грант 4007 не исполняется (`UAsdaWorldSubsystem::OnNetEntityMove`), автопуть ждёт
//   (`UAsdaMinimapSubsystem::TickMapTravel`). После снятия зажатая W идёт сразу (сброс схемы клавиш → фронт нажатия).
//   Набор остановки = набор сервера, снимающий со стека хода (0,1,2,3,8,9,11,12,13) + абсолютное оглушение 14 (клиентский гейт
//   эталона); страх 4 — нет (ход ведёт сервер).
static ASDA_DEV_CVAR(int32, CVarAsdaMoveStunHardStop,
	TEXT("asda.Move.StunHardStop"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 22.10] Оглушение/заморозка/сон/паутина/окаменение на своём персонаже: 1 = остановка на месте сразу (4008), ")
	TEXT("ход клавишами, кликом и автопутём ждёт снятия, опоздавший грант не исполняется; 0 = как оригинал (персонаж добегает ")
	TEXT("прежний запрос хода, после снятия возможен откат)."),
	ECVF_Default);

namespace AsdaStunStop
{
	// Состояния остановки (id, type 0): набор сервера `ProcessState_*` со снятием со стека хода + 14 (разбор — выше).
	static const int32 kStopIds[] = { 0, 1, 2, 3, 8, 9, 11, 12, 13, 14 };
	// Эталонный гейт клика/клавиш `listHasAnyOfSlotTypes@0xA38970` (skill_buff.c:7928): 0,1,2,3,14,9,4,8.
	static const int32 kInputIds[] = { 0, 1, 2, 3, 14, 9, 4, 8 };
	// Первое найденное состояние набора у своего персонажа, −1 — нет.
	static int32 FirstOf(const UGameInstance* GI, const int32* Ids, int32 N)
	{
		const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		if (!Fx) { return -1; }
		const int32 Self = Fx->SelfUserIdx();
		for (int32 i = 0; i < N; ++i) { if (Fx->FindByTypeSub(Self, Ids[i], 0)) { return Ids[i]; } }
		return -1;
	}
}

// ★ 31.10.2026: свой персонаж доигрывает жест поражения дуэли (определение — AsdaPlayerCharacter_Move.cpp, ключ `asda.Duel.LoserLock`).
bool AsdaDuelLoserLocked(const class AAsdaPlayerCharacter* Ch);
// ★ 08.11.2026 [ИСПРАВЛЕНИЕ]: ход держится запросом смены канала или переходом (определение и разбор — AsdaMinimapSubsystem.cpp,
//   ключ `asda.Minimap.ChannelMoveStop`).
bool AsdaSelfMoveHeldForChannel(const class UGameInstance* GI);
// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 19): ход держится действием UI с ожиданием — крафт, заточка, разборка… (World/AsdaUiActionMoveHold.cpp,
//   ключ `asda.Move.UiActionStop`); второе — имя идущего действия для журнала (nullptr — никакого).
bool AsdaSelfMoveHeldForUiAction(const class UGameInstance* GI);
const TCHAR* AsdaSelfUiActionInProgress(const class UGameInstance* GI);

bool AAsdaPlayerController::IsStunHardStopped() const
{
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ]: жест поражения дуэли (51) держит персонажа как обездвиживание — та же точка остановки хода (клавиши,
	//   клик, автопуть, опоздавший грант, подбегание к NPC/луту). Разбор — у `AsdaDuelLoserLocked`.
	if (AsdaDuelLoserLocked(GetChar())) { return true; }
	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ]: смена канала запрошена / идёт переход — та же точка остановки хода (разбор — у `AsdaSelfMoveHeldForChannel`).
	if (AsdaSelfMoveHeldForChannel(GetGameInstance())) { return true; }
	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ]: идёт действие UI с ожиданием (создание снаряжения и др.) — та же точка (разбор — AsdaUiActionMoveHold.cpp).
	if (AsdaSelfMoveHeldForUiAction(GetGameInstance())) { return true; }
	if (CVarAsdaMoveStunHardStop.GetValueOnGameThread() == 0) { return false; }
	return AsdaStunStop::FirstOf(GetGameInstance(), AsdaStunStop::kStopIds, UE_ARRAY_COUNT(AsdaStunStop::kStopIds)) >= 0;
}

bool AAsdaPlayerController::IsMoveInputLockedByState() const
{
	// ★ Правка ревизии стана (мелочь 4): эталонный набор `listHasAnyOfSlotTypes` — [ОРИГИНАЛ], ключом исправления НЕ выключается;
	//   состояния остановки 11/12/13 — только под `asda.Move.StunHardStop` [ИСПРАВЛЕНИЕ] (`IsStunHardStopped`).
	if (AsdaStunStop::FirstOf(GetGameInstance(), AsdaStunStop::kInputIds, UE_ARRAY_COUNT(AsdaStunStop::kInputIds)) >= 0) { return true; }
	return IsStunHardStopped();
}

bool AAsdaPlayerController::StunHardStopTick(AAsdaPlayerCharacter* Ch)
{
	const bool bNow = Ch && IsStunHardStopped();
	if (!bNow)
	{
		if (bStunHardStopped)
		{
			bStunHardStopped = false;
			UE_LOG(LogAsdaKbMove, Log, TEXT("[стан] состояние снято — ход разрешён [ИСПРАВЛЕНИЕ 22.10]"));
		}
		return false;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!bStunHardStopped)
	{
		bStunHardStopped = true;
		++StunHardStopCount;
		const int32 Id = AsdaStunStop::FirstOf(GetGameInstance(), AsdaStunStop::kStopIds, UE_ARRAY_COUNT(AsdaStunStop::kStopIds));
		const bool bWasWalking = Ch->IsMoving();
		const bool bInFlight = Net && Net->IsMoveRequestInFlight();
		CancelRoute();
		bMovingToPoint = false;
		// ★ 08.11.2026 [ИСПРАВЛЕНИЕ]: фронт удержания действием UI — автопуть снимается (как эталонный `followTargetCancelReset` входа
		//   в ремесло/гачу): по концу крафта персонаж не уходит сам. Стан и переход заявку держат (поход продолжится).
		if (AsdaSelfMoveHeldForUiAction(GetGameInstance()))
		{
			if (UAsdaMinimapSubsystem* Mini = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr)
			{
				if (Mini->IsTravelRequested()) { Mini->CancelTravelRequest(); }
			}
		}
		CancelPendingSkillApproach(TEXT("стан"));
		if (bNpcTalkPending) { ClearPendingNpcTalk(TEXT("стан")); }
		bLootPending = false;
		// После снятия зажатая клавиша — снова фронт нажатия (ход сразу). ★ Правка ревизии стана (мелочь 3): автобег переживает стан,
		//   как у эталона (главный цикл просто не зовёт `updatePlayerMovement`, `g_bAutoRunActive` никто не трогает, game_main.c:1506).
		if (IsKeyboardEtalon())
		{
			const bool bAr = bAutoRunActive, bArPrev = bKbAutoRunPrev, bArNotice = bKbAutoRunNotice;
			const double ArHold = KbAutoRunHoldStartMs;
			KbResetState();
			bAutoRunActive = bAr; bKbAutoRunPrev = bArPrev; bKbAutoRunNotice = bArNotice; KbAutoRunHoldStartMs = ArHold;
		}
		bLocalStartPending = false;   // ★ локальный старт: стоп 4008 ставит сервер в точку клиента — возвращать некуда
		const FVector2D Pos = Ch->GetGamePosXZ();
		bool b4008 = false;
		if ((bWasWalking || bInFlight) && Net && Net->bInGameConnected) { b4008 = Net->SendStop4008((float)Pos.X, (float)Pos.Y); }
		Ch->StopMove();
		// Причина остановки для журнала: состояние стана, удержание сменой канала/переходом, действием UI или жест поражения дуэли.
		FString Reason;
		if (Id >= 0) { Reason = FString::Printf(TEXT("состояние %d"), Id); }
		else if (AsdaSelfMoveHeldForChannel(GetGameInstance())) { Reason = TEXT("удержание смены канала/перехода (08.11)"); }
		else if (AsdaSelfMoveHeldForUiAction(GetGameInstance()))
		{
			Reason = FString::Printf(TEXT("удержание действием UI «%s» (08.11)"), AsdaSelfUiActionInProgress(GetGameInstance()));
		}
		else { Reason = TEXT("жест поражения дуэли"); }
		UE_LOG(LogAsdaKbMove, Log, TEXT("[стан] %s наложено: остановка в (%.0f,%.0f), шёл %d, запрос в пути %d, 4008 %s [ИСПРАВЛЕНИЕ 22.10]"),
			*Reason,
			Pos.X, Pos.Y, bWasWalking ? 1 : 0, bInFlight ? 1 : 0, b4008 ? TEXT("ушёл") : TEXT("не нужен"));
	}
	else if (Ch->IsMoving())
	{
		// Под станом ход не начинается ничем — если что-то всё же тронуло персонажа (ход «в точку» из постороннего пути), гасим.
		UE_LOG(LogAsdaKbMove, Log, TEXT("[стан] персонаж тронулся под станом — остановлен"));
		Ch->StopMove();
	}
	bMoveFwd = bMoveBack = bMoveLeft = bMoveRight = false;
	bWasMoving = false;   // прежняя схема (ключ 0 клавиш): её «шёл» не должен дать второй стоп после снятия
	return true;
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★ 22.10.2026 — ЛОКАЛЬНЫЙ СТАРТ (решение владельца 22.10 «Делать локальный старт»). Ключ `asda.Move.LocalStart` (1; 0 — прежнее
//   дословно: персонаж трогается только по гранту 4007, как оригинал).
//
//  ЧТО ПРОВЕРЯЕТ СЕРВЕР (`CharMove@0x544EB0`, GameServer.c:124435-124654) — и почему локальный старт его не нарушает:
//   • Сервер СТОИТ (`sub_523ED0` ложно): cur 4006 обязан ТОЧНО совпасть с точкой сервера (float, :124599) — иначе result 5
//     «CharMove Hack Position». Локальный старт шлёт 4006 из точки стоянки ДО первого шага (тот же кадр, cur = точка, в которой
//     сервер нас держит: её поставили 4008/4009/грант), поэтому совпадение то же, что у оригинала.
//   • Сервер ИДЁТ: сравнивает «пройдено сервером» и «пройдено клиентом» от начала отрезка (`sub_523310` = расстояние / скорость,
//     мс; :124549-124579): клиент впереди больше 1000 мс → result 5 «CharReMove». Клиент трогается в момент отправки t0, сервер —
//     в момент приёма t0 + ½RTT; к любой следующей отправке t клиент прошёл (t − t0)·v, сервер к её приёму — те же (t − t0)·v.
//     Опережения нет, если скорость старта = скорость сервера: берётся `LastServerSpeed` (скорость последнего своего гранта/6071;
//     до первого гранта и после смены персонажа — 0.35; под замедлением сервер медленнее — клиент впереди, но ход без гранта ограничен потолком 0.6 с, запас сервера 1000 мс). Грант затем подгоняет скорость
//     к серверному времени (`ApplySelfMoveGrantEtalon`, ход «в точку» от текущей позиции — без рывка).
//   • P57 (на живом `Mode=0`, журнал): бюджет опережения по накопленному пути отрезка (3000 мс + 25 %) и `MaxStepLen` — пакеты те же,
//     что у оригинала, скорость та же: бюджет не растёт.
//   • Отказ (result 0/2/3/4/6/7/8/9): сервер остался в точке отправки → ПЛАВНЫЙ возврат туда (`NetSnap` — тело и камера догоняют
//     хвостом `asda.Anim.SnapSmoothSec`), следующая попытка — без локального старта, пока сервер не даст грант (иначе отказ
//     повторялся бы раз в RTT «шаг вперёд — назад»). Если после старта уже ушёл 4008 (`bActionPending` снят) — сервер принял точку
//     клиента (`CharStop` LABEL_35), возврата нет. result 5 — снап в точку сервера (как прежде).
//   • P77 (выложен): на молчаливой остановке сервер шлёт 4009 в свою точку — свой 4009 снапит (хвостом), следующий 4006 из неё
//     совпадает точно. Стан (`asda.Move.StunHardStop`) — локальный старт под ним не начинается, стоп шлёт 4008.
//  Подход к цели боя/навыка (`bRouteApproach`, цель маршрута) — по-прежнему по гранту (тайминг замаха сверен с эталоном).
static ASDA_DEV_CVAR(int32, CVarAsdaMoveLocalStart,
	TEXT("asda.Move.LocalStart"), 1,
	TEXT("Свой ход клавишами и кликом: 1 = персонаж трогается сразу (со скоростью сервера), ответ сервера только подгоняет ход; ")
	TEXT("отказ — плавный возврат. 0 = как оригинал: персонаж трогается только по разрешению сервера (4007)."),
	ECVF_Default);

bool AAsdaPlayerController::IsLocalStart()
{
	return CVarAsdaMoveLocalStart.GetValueOnGameThread() != 0;
}

// ★ Ревизия локального старта (22.10.2026): ВАЖНО-1…5 и мелочи — пометки по месту.
//   Скорость старта — `LastServerSpeed` (скорость своего последнего гранта/6071; сбрасывается на смене персонажа). Под дебаффом
//   замедления сервер медленнее 0.35 — тогда клиент впереди, но ход «без гранта» ограничен потолком `kLocalStartCapSec` (ВАЖНО-4),
//   а запас «идёт» у сервера — 1000 мс.
static constexpr double kLocalStartCapSec = 0.6;   // ВАЖНО-4: дольше без гранта персонаж не идёт (потом стоит до ответа)

bool AAsdaPlayerController::IsStunHardStopKey()
{
	return CVarAsdaMoveStunHardStop.GetValueOnGameThread() != 0;
}

void AAsdaPlayerController::LocalStartAfter4006(AAsdaPlayerCharacter* Ch, const FVector2D& Cur, const FVector2D& Dst, const TCHAR* Src)
{
	// ВАЖНО-5: каждый ушедший 4006 начинает новый ответ — прежний признак ожидания недействителен, ставится заново только при старте.
	bLocalStartPending = false; bLocalStartCapped = false;
	if (!Ch || !IsLocalStart() || !IsEtalonSpeedSync()) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->bInGameConnected) { return; }
	if (IsStunHardStopped() || IsSelfDead()) { return; }
	if (FVector2D::Distance(Cur, Dst) < 1.0) { return; }   // 4006 «в свою точку» — хода нет
	if (bSuppressLocalStartOnce) { return; }   // 23.10.2026: клик в зону рыбалки — ждём гранта, как оригинал (см. OnLeftClick)
	// Мелочь: точка в своей клетке 100×100 — сервер ответит 0 («та же клетка», GameServer.c:124532); отказ предсказуем — не трогаемся.
	if (AsdaCoord::GameToTile(Dst) == AsdaCoord::GameToTile(Cur)) { return; }
	if (bLocalStartSuppressed)
	{
		UE_LOG(LogAsdaKbMove, Verbose, TEXT("[ход] локальный старт (%s) пропущен: прошлый запрос сервер отклонил — ждём гранта"), Src);
		return;
	}
	const float V = (Net->LastServerSpeed > 0.0001f && Net->LastServerSpeed < 5.f) ? Net->LastServerSpeed : 0.35f;
	// ВАЖНО-3: шёл ли прежний грант — при отказе сервер его продолжает, и клиент должен его добежать, а не вернуться в точку отправки.
	bLocalStartOnRun = Ch->IsMovingToPoint();
	LocalPrevDst = Ch->GetMoveDstGame();
	LocalPrevSpeed = Ch->GetMoveSpeed();
	bLocalStartPending = true;
	LocalStartOrigin = Cur;
	LocalStartAtS = FPlatformTime::Seconds();
	++LocalStartCount;
	Ch->NetMoveFromActor(Dst, V);
	UE_LOG(LogAsdaKbMove, Verbose, TEXT("[ход] локальный старт (%s): (%.0f,%.0f)→(%.0f,%.0f) скорость %.4f, на бегу %d"), Src, Cur.X, Cur.Y, Dst.X, Dst.Y, V, bLocalStartOnRun ? 1 : 0);
}

void AAsdaPlayerController::OnLocalStartAnswered(int32 Result)
{
	bLastGrantWasLocal = bLocalStartPending && Result == 1;
	bLocalStartPending = false; bLocalStartCapped = false;
	if (Result == 1) { bLocalStartSuppressed = false; }
}

bool AAsdaPlayerController::OnLocalStartRefused(int32 Result)
{
	if (!bLocalStartPending) { return false; }
	bLocalStartPending = false; bLocalStartCapped = false;
	if (!IsLocalStart()) { return false; }
	bLocalStartSuppressed = true;
	AAsdaPlayerCharacter* Ch = GetChar();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Ch || !Net) { return false; }
	if (!Net->bActionPending)
	{
		UE_LOG(LogAsdaKbMove, Log, TEXT("[ход] отказ %d после локального старта: 4008 уже ушёл — сервер в точке клиента, возврата нет"), Result);
		return false;
	}
	++LocalStartReturnCount;
	if (bLocalStartOnRun && LocalPrevSpeed > 0.f)
	{
		// ВАЖНО-3: сервер продолжает прежний отрезок (отказ его не трогает) — клиент добегает прежний грант от текущей позиции.
		Ch->NetMoveFromActor(LocalPrevDst, LocalPrevSpeed);
		UE_LOG(LogAsdaKbMove, Log, TEXT("[ход] отказ сервера %d на бегу: продолжаем прежний грант к (%.0f,%.0f), без возврата"), Result, LocalPrevDst.X, LocalPrevDst.Y);
		return true;
	}
	const FVector2D Was = Ch->GetGamePosXZ();
	Ch->NetSnap(LocalStartOrigin);   // сервер стоял в точке отправки; тело и камера догоняют плавно (хвост поправки)
	UE_LOG(LogAsdaKbMove, Log, TEXT("[ход] отказ сервера %d после локального старта: плавный возврат (%.0f,%.0f) → (%.0f,%.0f); следующая попытка — по гранту"),
		Result, Was.X, Was.Y, LocalStartOrigin.X, LocalStartOrigin.Y);
	return true;
}

// ВАЖНО-4: исходящий задержан (Wi-Fi, > 1 с) — без гранта персонаж не уходит дальше потолка: клиент не обгоняет сервер на 1000 мс
//   (result 5 / 4009 с откатом). Остановка локальная, без 4008: грант (`ApplySelfMoveGrantEtalon`) поведёт дальше от этой точки.
void AAsdaPlayerController::LocalStartTick(AAsdaPlayerCharacter* Ch)
{
	if (!bLocalStartPending || bLocalStartCapped || !Ch) { return; }
	if (FPlatformTime::Seconds() - LocalStartAtS < kLocalStartCapSec) { return; }
	bLocalStartCapped = true;
	++LocalStartCapCount;
	// ★ 23.10.2026 (KB52c, исходящий 1.2 с): НА БЕГУ потолок не останавливает — прежний грант ещё действует, сервер идёт именно по нему
	//   (новый 4006 до него не дошёл), и оригинал его добегал бы. Остановка на бегу давала «шаг 0.6 с — стоп — рывок» на каждом
	//   цикле и копила отставание ~1.4 с от сервера: 4008 при отпускании попадал за допуск CharStop (1000 мс) → 4009 с переносом.
	//   Прежний отрезок — путь самого сервера, обгона нет. С места (прежнего гранта нет) — как прежде: стоим до ответа.
	const FVector2D PosNow = Ch->GetGamePosXZ();
	const FVector2D ToPrev = LocalPrevDst - PosNow;
	const FVector2D ToCur = Ch->GetMoveDstGame() - PosNow;
	// Прежняя цель должна лежать впереди по текущему ходу (угол < 90°) — иначе добегание развернуло бы персонажа назад. Персонаж ещё
	//   бежит и 4008 с тех пор не уходил (`bActionPending`): после отпускания сервер стоит в точке стопа — добегать нечего.
	const UAsdaNetSubsystem* NetCap = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (bLocalStartOnRun && LocalPrevSpeed > 0.f && Ch->IsMoving() && NetCap && NetCap->bActionPending
		&& ToPrev.Size() > 1.0 && FVector2D::DotProduct(ToPrev, ToCur) > 0.0)
	{
		Ch->NetMoveFromActor(LocalPrevDst, LocalPrevSpeed);
		UE_LOG(LogAsdaKbMove, Log, TEXT("[ход] локальный старт на бегу: ответа нет %.1f с — добегаем прежний грант к (%.0f,%.0f)"),
			kLocalStartCapSec, LocalPrevDst.X, LocalPrevDst.Y);
		return;
	}
	if (Ch->IsMoving()) { Ch->StopMove(); }
	UE_LOG(LogAsdaKbMove, Log, TEXT("[ход] локальный старт: ответа нет %.1f с — персонаж ждёт гранта на месте"), kLocalStartCapSec);
}

// ВАЖНО-2: 4009 — ответ на 4008, ушедший ДО нового 4006 (стоп без 4006 — `KbIntentStop`, тап, затем снова W быстрее RTT). Сервер на
//   этот 4008 встал в точку клиента — ту самую, из которой начат локальный ход; снап «назад» и затем грант «снова вперёд» не нужны.
bool AAsdaPlayerController::ShouldSkipStopSnap(const FVector2D& StopPos) const
{
	return bLocalStartPending && IsLocalStart() && FVector2D::Distance(StopPos, LocalStartOrigin) < 1.0;
}

bool AAsdaPlayerController::IsKeyboardEtalon()
{
	return CVarAsdaMoveKeyboardEtalon.GetValueOnGameThread() != 0;
}

// ─────────────────────────────── «Сырая» мышь ───────────────────────────────
// Эталон опрашивает `GetAsyncKeyState(1/2)` — физическое состояние кнопки, окна интерфейса его не заслоняют. [ПОРТ]
//   Только пока окно игры на переднем плане (иначе клик в другом окне Windows гасил бы бег свёрнутой игры). Подмена
//   самотеста (`KbDebugLmb/Rmb`) — единственный способ проверить ветку без физической мыши.
bool AAsdaPlayerController::KbRawMouseDown(int32 Button) const
{
	const int32 Dbg = (Button == 1) ? KbDebugLmb : KbDebugRmb;
	if (Dbg >= 0) { return Dbg != 0; }
	const FKey K = (Button == 1) ? EKeys::LeftMouseButton : EKeys::RightMouseButton;
	if (IsInputKeyDown(K)) { return true; }
#if PLATFORM_WINDOWS
	// ★ R5-fix6 (13.11.2026): без настоящего окна ОС (`-RenderOffscreen`) физическая мышь не читается — окно Null-приложения всегда
	//   «на переднем плане», и скрытая игра гасила бы ход от щелчков по другим окнам. Разбор — World/AsdaOsWindow.h.
	if (!AsdaGameHasOsWindow()) { return false; }
	FViewport* Vp = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->Viewport : nullptr;
	if (!Vp || !Vp->IsForegroundWindow()) { return false; }
	return (::GetAsyncKeyState(Button == 1 ? VK_LBUTTON : VK_RBUTTON) & 0x8000) != 0;
#else
	return false;
#endif
}

void AAsdaPlayerController::KbResetState()
{
	bKbWorldTickActive = true;
	for (int32 i = 0; i < 4; ++i) { bKbFlag[i] = false; bKbPrevLevel[i] = false; KbKeyDownAtMs[i] = 0.0; }
	KbRepeatKey = -1;
	bKbFwdLatch = bKbBackLatch = false;
	KbTimeoutMs = 10;
	KbLastRepeatMs = 0.0;
	KbPrevDir = FVector3f::ZeroVector;
	bKbPrevDirInit = false;
	KbSentDir = FVector3f::ZeroVector;   // ★ 22.10 [ИСПРАВЛЕНИЕ]
	bKbSentDirValid = false;
	bKbIntentStopped = false;
	bKbAutoRunPrev = false;
	bAutoRunActive = false;
	bKbAutoRunNotice = false;
	KbAutoRunHoldStartMs = 0.0;
	if (AAsdaPlayerCharacter* Ch = GetChar())
	{
		KbSeenMoveEndSerial = Ch->GetPointMoveEndSerial();
		Ch->SetEtalonBackpedal(false);
	}
}

// ─────────────────────────────── Состояние действия ───────────────────────────────
// Эталонное `+47551` своего персонажа, собранное из признаков порта. [ПОРТ] единого поля состояния у порта нет:
//   6 — смерть (`IsSelfDead`, оба источника); 7/8/9 — фазы сидения (`GetSitPhase`, номера эталона; «сидит» без фазы = 8);
//   5 — замах/бой в стойке (`bAutoAttack` и стойка боя или клип атаки: эталон `recvEntitySetState5@0xF3AAD0` ставит 5 по
//   ответу на удар и держит его, пока бьёт); 13/14/24/27 — полоса каста (`UAsdaCastBarSubsystem::GetState`), иначе
//   24 — применение навыка (`IsSkillAnimActive`, конец клипа = конец 24); «занятое» состояние персонажа — эмоция 33…46 (пока идёт
//   клип), лопата 40/43, рыбалка 60…67, 68 «добыча» (★ В3 S7, 12.10.2026: единое поле `GetBusyActionCode`; прежде три признака,
//   эмоция всегда 33, а 68 не выводилось — его дописывали два вызывающих в `UAsdaInventorySubsystem`); 1 — идёт; иначе 0. Состояния 15 («в дальности — начать атаку», мгновенный переход к 5)
//   и 25 порт не выводит — реестр отложенного.
int32 AAsdaPlayerController::KbEtalonActionState() const
{
	const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(GetPawn());
	if (IsSelfDead()) { return 6; }
	if (!Ch) { return 0; }
	const int32 Sit = Ch->GetEtalonSitState();   // 7/8/9 или 0 — общая точка персонажа (27.09.2026)
	if (Sit != 0) { return Sit; }
	// ★ 05.11.2026 [ОРИГИНАЛ] (`asda.Skill.CastLockMove`, `World/AsdaSkillCastLock`): свой навык — 25 (заявка в пути), 14 (каст-ожидание),
	//   13/27, 24 (клип). Прежде 25 не выводилось вовсе, а 14/24 — только по полосе каста: клавиши хода в 25 давали ход посреди
	//   заявки. Навык поверх удержания автоатаки — тоже 24 (у эталона одно поле +47551; удержание — +52121 = 100).
	if (const int32 SkPh = AsdaSkillCastLock::PhaseForActionState(GetGameInstance())) { return SkPh; }
	if (bAutoAttack && (bInCombatStance || Ch->IsAttackAnimActive())) { return 5; }
	// Полоса каста порта ведёт эталонное состояние 13/14/24/27 (`UAsdaCastBarSubsystem`, гейт видимости `ui_core.c:89767`).
	if (const UAsdaCastBarSubsystem* Cb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr)
	{
		const int32 Cs = Cb->GetState();
		if (Cb->IsActive() && (Cs == 13 || Cs == 14 || Cs == 24 || Cs == 27)) { return Cs; }
	}
	if (Ch->IsSkillAnimActive()) { return 24; }
	// ★ В5: рыбалка 60…67; ★ 09.10.2026 (п. 5): лопата 40/43; эмоция; 68. ★ В3 S7: одно поле персонажа (эталонное +47551).
	if (const int32 Busy = Ch->GetBusyActionCode()) { return Busy; }
	if (Ch->IsMoving()) { return AsdaKbEtalon::kStateMove; }
	return 0;
}

// ─────────────────────────────── Гейты ───────────────────────────────
// Главный цикл (`game_main.c:1506`, game.c:9558): `+190204 != 5 && != 24 && !listHasAnyOfSlotTypes` — иначе
//   `updatePlayerMovement` не вызывается. `listHasAnyOfSlotTypes@0xA38970`: записи списка состояний с id 0,1,2,3,14,9,4,8 и
//   type 0 (`CList_FindByTypeSub`). [ПОРТ] `HasControlState` порта проверяет другой набор ({0,1,3,14,8,4}, без 2 и 9) —
//   поэтому здесь свои восемь поисков по той же паре (id, type).
bool AAsdaPlayerController::KbMainLoopGateBlocked() const
{
	const int32 St = KbEtalonActionState();
	if (St == 5 || St == 24) { return true; }
	const UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return false; }
	static const int32 kShackles[] = { 0, 1, 2, 3, 14, 9, 4, 8 };
	const int32 Self = Fx->SelfUserIdx();
	for (const int32 Id : kShackles) { if (Fx->FindByTypeSub(Self, Id, 0)) { return true; } }
	return false;
}

// Верхний гейт `updatePlayerMovement` (game.c:8730), по порядку эталона:
//   `sub_934E86(g_pSellWindow)` — подготовка личной лавки 22000 (`CSellWindow+0x300`) — ★ Л5 торговли: общая точка `IsSellWindowOpen`;
//   `sub_929DC4(g_bSomeFeatureEnabled)` = `CShopDlg+0x168` — витрина лавки 23000 открыта — ★ Л5: `IsShopDlgOpen` (реестр «Ход клавишами» п.1);
//   `sub_922D5D(g_pGachaScene)` — сцена премиум-гачи (у порта её гейт стоит раньше в `PlayerTick` и здесь повторён);
//   `sub_9384DC(g_pInventoryBagWindow) == 1` — элитный почтовый ящик 69000;
//   `LocalPlayer[1].gap_A42[15039] == 1` — «идёт добыча» (состояние 68, пакеты 6753/6756; НЕ смерть, как считалось в
//     комментарии гачи) — НЕТ В ПОРТЕ (реестр);
//   `CObject_GetField5581(g_pWarehouseWnd)` — свиток возврата склада (`IsRecallActive`, у кладовщика склад не блокирует).
bool AAsdaPlayerController::KbTopGateBlocked() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI)) { return true; }   // ★ Л5: `sub_934E86(g_pSellWindow) || sub_929DC4(…)`
	if (const UAsdaPremiumGachaSubsystem* Pg = GI->GetSubsystem<UAsdaPremiumGachaSubsystem>()) { if (Pg->IsSceneOpen()) { return true; } }
	if (const UAsdaPremiumPostSubsystem* Pp = GI->GetSubsystem<UAsdaPremiumPostSubsystem>()) { if (Pp->IsWindowVisible()) { return true; } }
	if (const UAsdaWarehouseSubsystem* Wh = GI->GetSubsystem<UAsdaWarehouseSubsystem>()) { if (Wh->IsRecallActive()) { return true; } }
	return false;
}

// `uiCanOpenWindowGate@0xECA560`: при любом из окон ниже клавиши движения «не нажаты» (флаги не гаснут; если окно
//   открылось посреди хода — срабатывает ветка отпускания, стоп). Соответствие контролам доказано записью
//   `FindControlByID(id)` в поле объекта (дизасм) и `gamemain_jui.ini`:
//   a) `CSoulmateWnd+0x330` = 9100 `SOULMATE_LINK_WIN` видно;
//   b) `CMsgBoxMgr+0x80` = 1552 `GIFT_MSGBOX` — ★ 06.10.2026 (волна 2-Г) окно перенесено (ввод имени свитков призыва/перемещения);
//   c) сообщество +0x574 = 14400 `CLAN_MAKING_WIN` (создание клана);
//   d) `CItemMediation+0x4C0` — аукцион 13000 открыт;
//   e) элитный магазин 6000 (поле 2075 `g_pShopWnd`);  f) сессия парикмахерской (+0x298);
//   g) `CFindSoulmateWin+0x1C8` = 8300 `My_Introduction_Win` («Обо мне»);
//   h) фокус в `Stall_Chat_Input` 23403 личной лавки — ★ Л5 торговли: `UAsdaSelfStoreSubsystem::IsChatInputFocused` (явно: у UE
//      буква, не обработанная полем, всплывает до вьюпорта — «покрыт фокусом ввода» было неверно, см. `IsChatInputFocused`);
//   i) окно заметки соулмейта 6605 `Soul_MsgWin` видно; фокус в полях клана 14021/14029;
//   j) фокус в `Pet_Cage_NameInput` 43071;  k) фокус в `Shouting_InputBox` 1583.
//   [ПОРТ] Гейты «фокус в поле ввода» (h-фокус, i-поля клана, j, k) у UE выполняются самим вводом: пока строка
//   ввода держит клавиатурный фокус, Slate отдаёт клавишу виджету и уровень клавиши у контроллера не взводится (а при
//   уходе фокуса ввод контроллера сбрасывается — то же «не нажата»). Окно 6605 публичного признака у подсистемы соулмейта
//   не имеет — в реестр отложенного вместе с лавкой (1552 — перенесено волной 2-Г).
bool AAsdaPlayerController::KbWindowGateAllows() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI) { return true; }
	bool bAllow = true;
	if (const UAsdaWindowSubsystem* Wm = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		if (Wm->IsWindowVisible(FName(TEXT("SOULMATE_LINK_WIN"))))   { bAllow = false; }   // a
		if (Wm->IsWindowVisible(FName(TEXT("GIFT_MSGBOX"))))         { bAllow = false; }   // b (★ волна 2-Г)
		if (Wm->IsWindowVisible(FName(TEXT("My_Introduction_Win")))) { bAllow = false; }   // g
	}
	if (const UAsdaClanSubsystem* Cl = GI->GetSubsystem<UAsdaClanSubsystem>())           { if (Cl->IsCreateOpen())   { bAllow = false; } }   // c
	if (const UAsdaExchangeSubsystem* Ex = GI->GetSubsystem<UAsdaExchangeSubsystem>())   { if (Ex->IsOpen())         { bAllow = false; } }   // d
	if (const UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { if (Ps->IsOpen())     { bAllow = false; } }   // e
	if (const UAsdaHairShopSubsystem* Hs = GI->GetSubsystem<UAsdaHairShopSubsystem>())   { if (Hs->IsSessionOpen())  { bAllow = false; } }   // f
	if (const UAsdaSelfStoreSubsystem* St = GI->GetSubsystem<UAsdaSelfStoreSubsystem>())  { if (St->IsChatInputFocused()) { bAllow = false; } }   // h
	return bAllow;
}

// `Party_CanInviteCheck@0x95C870` (миснейм: «можно ли идти», game.c:8536). Порядок и нотисы эталона; все отказные
//   строки — `CChat_AddTokenizedLines(g_pChatWnd, текст, −11514, 10000, 0)` = строка системного чата, золотая, 10 с:
//   1) `g_bPartyInviteFlag` (МИСНЕЙМ: `CExchange+0x2C` = ОКНО обмена 2008 открыто) → 9568 «При открытом окне обмена
//      двигаться нельзя». ★ ПД-2 (29.09.2026, `Reports/trade/trade_re_report.md` §2.3): порт читал здесь «заявка в
//      полёте» (`IsExchangeRequestActive`, до 33 с) — игрок с неотвеченной заявкой не мог ходить клавишами. У эталона на
//      стадии заявки (`+0x14`/`+0x38`) гейт молчит. Теперь — общая точка `UAsdaTradeStateSubsystem::IsExchangeWindowOpen`
//      (пока окна 2008 нет — false);
//   2) `g_pPartyTargetObj` (элитный магазин) → 12917 — при ходе клавишами недостижимо (раньше срабатывает гейт окон e);
//   3) `CItemTooltipWindow` (миснейм: военный магазин 67200 `HONOR_itemmall_Win`) → 110611;
//   4) `g_pPackageStampWindow` открыт → 12917 — писателя «= 1» в срезе нет (окно 26000 не показывается никогда): не
//      переносится, реестр;
//   5) `g_bModalNoticeActive` (миснейм: магазин бустеров 15300 открыт) → 99003;
//   6) парикмахерская → 13010 — недостижимо (гейт окон f);  7) окно бустера 10050 → 13100;
//   8) свиток склада → 10305 — недостижимо (верхний гейт);
//   9) машина извлечения занята (`g_pExtractShopWin+0x1064`) → 13100 — публичного признака у `UAsdaExtractShopSubsystem`
//      нет (поле `bBusy` закрыто) — реестр;
//   10) МОЛЧА: состояние 13/14/15/25/27 или запись 547 («Ловушка», навык-тогл) в списке состояний (type 0).
//       [ПОРТ] 15 и 25 у порта не выводятся (реестр), 13/14/27 — по полосе каста;
//   11) `sub_93924C` — заглушка `return 0` → «можно».
bool AAsdaPlayerController::KbCanMoveCheck()
{
	using namespace AsdaKbEtalon;
	const UGameInstance* GI = GetGameInstance();
	if (!GI) { return true; }
	int32 RefuseLoc = 0;
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI)) { RefuseLoc = kLocTradeInvite; }   // [ОРИГИНАЛ] `CExchange+0x2C` (ПД-2)
	if (!RefuseLoc) { if (const UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { if (Ps->IsOpen()) { RefuseLoc = kLocShopOpen; } } }
	if (!RefuseLoc) { if (const UAsdaWarShopSubsystem* Ws = GI->GetSubsystem<UAsdaWarShopSubsystem>()) { if (Ws->IsOpen()) { RefuseLoc = kLocTooltipWnd; } } }
	if (!RefuseLoc) { if (const UAsdaBoosterShopSubsystem* Bs = GI->GetSubsystem<UAsdaBoosterShopSubsystem>()) { if (Bs->IsOpen()) { RefuseLoc = kLocModalNotice; } } }
	if (!RefuseLoc) { if (const UAsdaHairShopSubsystem* Hs = GI->GetSubsystem<UAsdaHairShopSubsystem>()) { if (Hs->IsSessionOpen()) { RefuseLoc = kLocHairShop; } } }
	if (!RefuseLoc) { if (const UAsdaBoosterPanelSubsystem* Bp = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>()) { if (Bp->IsWindowOpen()) { RefuseLoc = kLocBooster; } } }
	if (!RefuseLoc) { if (const UAsdaWarehouseSubsystem* Wh = GI->GetSubsystem<UAsdaWarehouseSubsystem>()) { if (Wh->IsRecallActive()) { RefuseLoc = kLocWarehouse; } } }
	if (RefuseLoc)
	{
		const bool bOnce = CVarAsdaMoveKeyboardNoticeOnce.GetValueOnGameThread() != 0;
		if (!bOnce || KbRefuseShownLoc != RefuseLoc)
		{
			KbRefuseShownLoc = RefuseLoc;
			AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, AsdaNotice::LocRaw(GI, RefuseLoc), kNoticeColor, 10000.f);   // без звука, как у эталона
			UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] ход запрещён: нотис %d в чат (Party_CanInviteCheck)"), RefuseLoc);
		}
		return false;
	}
	KbRefuseShownLoc = 0;
	const int32 St = KbEtalonActionState();
	if (St == 13 || St == 14 || St == 15 || St == 25 || St == 27) { return false; }
	if (const UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>())
	{
		if (Fx->FindByTypeSub(Fx->SelfUserIdx(), 547, 0)) { return false; }
	}
	return true;
}

// ─────────────────────────────── Вход клика для клавиш ───────────────────────────────
// Порт `CGame_HandleGroundClickAttack@0x996480` (game.c:31618) с `spawnEffect = 0` (метки клика у клавиш нет):
//   1) состояние 1/19 и поле +208484 == 19 → поле = 100, отказ — [ПОРТ] поля +208484 в порте нет (смысл в срезе не
//      выведен, писатели не разобраны; реестр отложенного), ветка не переносится;
//   2) бит 4 (переход) или бит 2 (4006 в пути) → молча. Бит 4 у порта — гейт перехода внутри `SendMove4006`;
//   3) состояние 6 или бит занятости +190208 & 1 → молча ([ПОРТ] бита занятости в порте нет — реестр «Биты занятости» №2);
//   4) состояния 5/24 → `sub_92D316(…, 16)`: с клавиатуры недостижимо (гейт главного цикла) — не переносится;
//      14/15/25 → молча; 5) иначе 4006 подтип 2, +25 = младшее слово `targetRenderId` (цель атаки), если такая есть.
//   Сброс «режима курсора предмета» (`pGame[4676]`, курсор «nomal») — ветка окна сумки, у клавиш не влияет на ход. [ПОРТ]
bool AAsdaPlayerController::KbHandleGroundClick(const FVector2D& DstGame)
{
	AAsdaPlayerCharacter* Ch = GetChar();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Ch || !Net || !Net->bInGameConnected) { return false; }
	if (Net->IsMoveRequestInFlight()) { return false; }                 // бит 2 — молча
	const int32 St = KbEtalonActionState();
	if (St == 6) { return false; }
	if (St == 5 || St == 24 || St == 14 || St == 15 || St == 25) { return false; }
	// +25: младшее слово `targetRenderId` = цель атаки своего персонажа, если такая сущность есть (эталон
	//   `Combat_SendNormalAttack@0x9F8CA0`, `Entity_FindByRenderId`); иначе −1.
	int32 Tgt = -1;
	if (bAutoAttack)
	{
		if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(TargetActor.Get())) { if (!M->IsDead()) { Tgt = M->NetRenderId; } }
	}
	const FVector2D Cur = Ch->GetGamePosXZ();
	return Net->SendMove4006((float)Cur.X, (float)Cur.Y, (float)DstGame.X, (float)DstGame.Y, Tgt);
}

// ─────────────────────────────── Отпускание ───────────────────────────────
// Эталон game.c:8989-9013 — первый кадр без нажатых клавиш после активной клавиатуры (`!g_bWorldTickActive`):
//   `CGame_HandleGroundClickAttack(своя позиция)` → 4006 cur→cur (если бит 2/4 чист, жив, не занят); клетка игрока;
//   очистка узлов маршрута и маркера земли; `g_bGroundClickMoveSent = 1`; 4008 в свою точку (гейт `g_bActionPending`);
//   `[228] = 1` (следующий свой 4009 ещё раз поставит покой и снапнет); `SetAction(0)` — покой СРАЗУ.
//   Сервер на 4006 cur→cur (`CharMove`, GameServer.c:124534-124640): клетка точки == клетке сервера → result 0; сервер
//   «идёт» → result 1 (cur — серверная точка, если она впереди; клиент впереди > 1000 мс пути → 5); стоит → cur обязан
//   совпасть точно (иначе 5), dst == cur → 2 (нотис 1013). Все ответы порт обрабатывает штатно (`OnNetEntityMove`):
//   0 — без хода, 1 — грант не нужен (клавиатура уже неактивна), 5 — снап, 2 — нотис 1013, как у оригинала.
//   [228]: у порта свой 4009 (`NetSnap`) ставит покой всегда — это надмножество ветки эталона с флагом.
void AAsdaPlayerController::KbRelease(AAsdaPlayerCharacter* Ch)
{
	if (!Ch) { return; }
	const FVector2D Pos = Ch->GetGamePosXZ();
	const bool b4006 = KbHandleGroundClick(Pos);
	if (b4006) { NoteMove4006WithoutLocalStart(); }   // ★ 22.10.2026: 4006 «в свою точку» — локального хода нет (ВАЖНО-5)
	CancelRoute();                          // `listClearNodes_0` / `sub_92F3B9` — узлы маршрута
	bMovingToPoint = false;                 // `g_GroundTargetMarkerActive = 0` — повтор метки земли
	AsdaDestMarkClear(GetGameInstance(), TEXT("клавиши хода отпущены (math_geom.c:689)"));   // ★ 29.10.2026 [ОРИГИНАЛ] — и метка на картах
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool b4008 = Net && Net->bInGameConnected && Net->SendStop4008((float)Pos.X, (float)Pos.Y);
	Ch->StopMove();                         // `CharFx_AutoSabzil_State(…, 0)` — локальный покой
	// ★ 22.10.2026: следующее нажатие начинает с чистого листа (вектор «ушедшего» 4006 и признак остановки без намерения).
	KbSentDir = FVector3f::ZeroVector; bKbSentDirValid = false; bKbIntentStopped = false;
	UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] отпущены: покой в (%.0f,%.0f), 4006 cur→cur %s, 4008 %s (эталон game.c:8989-9013)"),
		Pos.X, Pos.Y, b4006 ? TEXT("ушёл") : TEXT("не ушёл (бит 2/4, состояние)"), b4008 ? TEXT("ушёл") : TEXT("не нужен"));
}

// ★ 22.10.2026 [ИСПРАВЛЕНИЕ] — разбор у объявления и в шапке файла (`asda.Move.KbIntentStop`).
//   Отличие от отпускания: 4006 «в свою точку» здесь НЕ шлётся — он взвёл бы бит 2, и новое направление (игрок жмёт W снова)
//   ждало бы его ответа; серверу достаточно 4008 (`CharStop@0x545C70`, GameServer.c:124657: клиент не впереди сервера больше
//   чем на 1000 мс → позиция = точка клиента, 4009 в неё же; иначе 4009 в точку сервера — снап, как при отпускании). Гейт
//   `bActionPending` — внутри `SendStop4008`. Клавиатура остаётся активной: разворот камеры A/D и 4006 «в свою точку» раз в 1000 мс
//   — как у эталона; их гранты (и грант прежнего направления, если он ещё в пути) не исполняются, пока не уйдёт новый 4006 с
//   ненулевым вектором.
void AAsdaPlayerController::KbIntentStop(AAsdaPlayerCharacter* Ch, const TCHAR* Why, double NowMs)
{
	bKbIntentStopped = true;
	// ★ Ревизия В-1 (REVIEW.md): без этого таймер [ИСПРАВЛЕНИЯ 1] видел «вектор 0 ≠ ушедшего W» и тут же (≤ 250 мс) слал 4006
	//   «в свою точку» — бит 2 взводился, и новое нажатие W ждало его ответа (+RTT). Теперь стоп считается ушедшим нулевым
	//   вектором: 4006 «в свою точку» — раз в 1000 мс от стопа (каденция эталона), а нажатие W/S/стрейф даёт «вектор ≠ ушедшего»
	//   и уходит сразу (W/S стоя — 10 мс).
	KbSentDir = FVector3f::ZeroVector; bKbSentDirValid = true; KbLastRepeatMs = NowMs;
	if (!Ch) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bInFlight = Net && Net->IsMoveRequestInFlight();
	if (!Ch->IsMoving() && !bInFlight)
	{
		UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] намерения хода нет (%s): персонаж уже стоит — без пакетов [ИСПРАВЛЕНИЕ 22.10]"), Why);
		return;
	}
	const FVector2D Pos = Ch->GetGamePosXZ();
	CancelRoute();                          // как у отпускания: узлы маршрута
	bMovingToPoint = false;                 // повтор метки земли
	AsdaDestMarkClear(GetGameInstance(), TEXT("намерения хода клавишами нет"));   // ★ 29.10.2026: как у отпускания — метка на картах
	const bool b4008 = Net && Net->bInGameConnected && Net->SendStop4008((float)Pos.X, (float)Pos.Y);
	Ch->StopMove();
	UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] намерения хода нет (%s): покой в (%.0f,%.0f), 4008 %s, грант прежнего направления %s [ИСПРАВЛЕНИЕ 22.10]"),
		Why, Pos.X, Pos.Y, b4008 ? TEXT("ушёл") : TEXT("не нужен"), bInFlight ? TEXT("в пути — не исполнится") : TEXT("не ждём"));
}

// ─────────────────────────────── Покадровый тик ───────────────────────────────
void AAsdaPlayerController::KeyboardEtalonTick(float DeltaTime, AAsdaPlayerCharacter* Ch)
{
	using namespace AsdaKbEtalon;
	if (!Ch) { return; }
	const double NowMs = FPlatformTime::Seconds() * 1000.0;
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	// ── 0. Сообщения клавиатуры (оконная процедура эталона — ДО главного цикла, гейтами тика не закрыты) ──
	//   Уровень клавиш — тот же, что у прежней схемы (раскладка MOVE_W/S/A/D + свободные стрелки — решение порта 15.08).
	//   WM_KEYDOWN (фронт нажатия и автоповтор Windows) взводит флаг (`input_SetMoveFlagFromKey@0xE3CCE0`, W/S ещё и
	//   гасят автобег), WM_KEYUP снимает (`hotkeyDispatchAction` case 51-54).
	{
		auto ArrowFree = [this](int32 Vk) { return !Keys || Keys->LookupCmd(Vk, EAsdaKeyMod::None) == 0; };
		const bool Level[4] = {
			bMoveFwd   || (ArrowFree(38) && IsInputKeyDown(EKeys::Up)),
			bMoveBack  || (ArrowFree(40) && IsInputKeyDown(EKeys::Down)),
			bMoveLeft  || (ArrowFree(37) && IsInputKeyDown(EKeys::Left)),
			bMoveRight || (ArrowFree(39) && IsInputKeyDown(EKeys::Right)) };
		const double Rep = RepeatDelayMs();
		// ★ Ревизия WASD, МЕЛК-1: Windows повторяет WM_KEYDOWN ТОЛЬКО последней нажатой клавиши; нажатие другой обрывает
		//   повтор прежней навсегда (он не возвращается и после отпускания новой), отпускание повторяемой — тоже.
		//   Пример: W, коснулись A, ЛКМ → после отпускания ЛКМ W не взводится (бег не возобновляется).
		for (int32 i = 0; i < 4; ++i)
		{
			if (Level[i] && !bKbPrevLevel[i]) { KbRepeatKey = i; KbKeyDownAtMs[i] = NowMs; }
			else if (!Level[i] && bKbPrevLevel[i] && KbRepeatKey == i) { KbRepeatKey = -1; }
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const bool bDownEdge = Level[i] && !bKbPrevLevel[i];
			const bool bRepeat = Level[i] && bKbPrevLevel[i] && KbRepeatKey == i && (NowMs - KbKeyDownAtMs[i] >= Rep);
			if (bDownEdge || bRepeat)
			{
				bKbFlag[i] = true;
				if (i < 2) { bAutoRunActive = false; }          // case '3'/'4': `g_bAutoRunActive = 0`
				// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ]: свежее нажатие W или S — намерение хода новее отложенного очередью клика хода,
				//   старый клик не исполняется (разбор — `AsdaSkillCastLock::SupersedeQueuedMove`). A/D без ПКМ только крутят камеру — не трогают.
				if (i < 2 && bDownEdge) { AsdaSkillCastLock::SupersedeQueuedMove(TEXT("ход клавишами W/S")); }
			}
			else if (!Level[i] && bKbPrevLevel[i]) { bKbFlag[i] = false; }   // WM_KEYUP
			bKbPrevLevel[i] = Level[i];
			// ★ 10.11.2026 [ОРИГИНАЛ] обучение: гид 1950 каждый кадр читает `g_bMoveFwd/Back/Left/Right` → байты 40…43.
			if (bKbFlag[i]) { AsdaTutorial::Mark(this, (EAsdaTutorialFlag)((int32)EAsdaTutorialFlag::MoveFwd + i)); }
		}
	}

	// ── Конец хода «в точку» самим персонажем (дошёл / упёрся) при активной клавиатуре → 4008 ──
	//   [ОРИГИНАЛ] `Player_ExecuteSkillOnTarget@0x9F16D0`: прибытие без следующего узла (game.c:68720-68730) и выход
	//   позиции за навигационную сетку (`GroundTarget_PickOnMesh` ложно, 68738-68745) шлют 4008 + покой; пока бит 2
	//   взведён, тик доигрывает шаг и стопа не шлёт — поэтому ждём снятия бита. Если к этому моменту новый грант уже
	//   повёл персонажа, стоп не нужен (ход идёт). Условие ветки упора дочитано 27.09 по байт-коду: 0x9F40DC
	//   `fcomp [0.0]` / `test ah,5` / `jp` — ветка берётся при СКАЛЯРНОМ ПРОИЗВЕДЕНИИ (путь до точки клетки-цели ·
	//   направление шага) < 0, то есть персонаж ПРОШЁЛ клетку-цель луча `terrainRaycastWalk`; затем точка на сетке →
	//   шаг продолжается, вне сетки → 4008 + покой. У порта тот же итог даёт проверка проходимости клетки шага
	//   (`AAsdaPlayerCharacter::Tick`, `IsGameWalkable`), конец хода считает `GetPointMoveEndSerial`.
	{
		const int32 Serial = Ch->GetPointMoveEndSerial();
		if (Serial != KbSeenMoveEndSerial)
		{
			const bool bBit2 = Net && Net->IsMoveRequestInFlight();
			if (!bBit2)
			{
				KbSeenMoveEndSerial = Serial;
				if (!bKbWorldTickActive && !HasRoute() && !Ch->IsMoving() && Net && Net->bInGameConnected)
				{
					const FVector2D P = Ch->GetGamePosXZ();
					const bool bSent = Net->SendStop4008((float)P.X, (float)P.Y);
					UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] ход по гранту закончен (дошёл/упёрся) в (%.0f,%.0f): 4008 %s"),
						P.X, P.Y, bSent ? TEXT("ушёл") : TEXT("не нужен"));
				}
			}
		}
	}

	// ── 1. Гейт главного цикла (`game_main.c:1506`): состояние 5/24 или статус-оковы → функция не вызывается ──
	//   и верхний гейт самой функции (лавка, гача, почтовый ящик, склад…) — ни хода, ни ветки отпускания.
	//   Камера у эталона живёт отдельно (`cameraUpdate`) — стрелу применяем.
	if (KbMainLoopGateBlocked() || KbTopGateBlocked())
	{
		if (Ch->CameraArm) { Ch->CameraArm->SetRelativeRotation(FRotator(CamPitchDeg, CamYawDeg, 0.f)); }
		return;
	}

	// ── 2. Автобег (game.c:8732-8757) ──
	bool& bFwd = bKbFlag[0]; bool& bBack = bKbFlag[1]; bool& bLeft = bKbFlag[2]; bool& bRight = bKbFlag[3];
	if (bFwd && bOptionAlwaysRun)
	{
		if (NowMs - KbAutoRunHoldStartMs > 3000.0 && !bAutoRunActive) { bAutoRunActive = true; }
	}
	else { KbAutoRunHoldStartMs = NowMs; }
	if (bBack) { bAutoRunActive = false; }
	if (bAutoRunActive) { bKbAutoRunPrev = true; }
	else if (bKbAutoRunPrev) { bKbAutoRunPrev = false; bFwd = false; }
	const int32 StateAtStart = KbEtalonActionState();
	if (StateAtStart == 5 || StateAtStart == 24 || StateAtStart == 22 || StateAtStart == 25) { bAutoRunActive = false; }

	// ── 3. Кадровая дельта и оси камеры (обоснование — шапка файла) ──
	//   Эталон читает матрицу вида ПРОШЛОГО кадра (разворот A/D этого кадра попадёт в вектор следующего), поэтому оси
	//   берём до разворота ниже. Направление — в игровых осях, как точка хода.
	const double FrameDelta = (double)DeltaTime * 1000.0 / kFrameBaseMs;     // `g_fMoveFrameDelta`
	if (!bKbPrevDirInit) { bKbPrevDirInit = true; KbPrevDir = FVector3f::ZeroVector; }
	const FRotator YawOnly(0.f, CamYawDeg, 0.f);
	const float CosPitch = FMath::Cos(FMath::DegreesToRadians(CamPitchDeg));
	const FVector UeFwd = YawOnly.Vector() * CosPitch;                            // −(m[8],0,m[10]) = взгляд · cos(наклон)
	const FVector UeRight = FRotationMatrix(YawOnly).GetScaledAxis(EAxis::Y);   // (m[0],0,m[2]) — единичная
	const FVector2D GFwd = AsdaCoord::UeDirToGame(FVector2D(UeFwd.X, UeFwd.Y));
	const FVector2D GRight = AsdaCoord::UeDirToGame(FVector2D(UeRight.X, UeRight.Y));
	FVector3f Dir = FVector3f::ZeroVector;
	bool bAny = false;

	// ── 4. Разрешение ввода: фокус ввода чата + окна (`sub_91F216(g_pChatWnd) && uiCanOpenWindowGate()`) ──
	const UAsdaChatWindowSubsystem* ChatWnd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
	const bool bAllowed = !(ChatWnd && ChatWnd->IsInputActive()) && KbWindowGateAllows();
	if (bAutoRunActive) { bFwd = true; }
	// ── 5. ЛКМ гасит все четыре флага и автобег (`GetAsyncKeyState(1)`) ──
	{
		// ★ R5-fix6 (13.11.2026): запись в журнал — один раз на нажатие, когда ЛКМ сняла взведённые флаги хода или автобег (прежде
		//   это было молча, и «отпущенные сами» клавиши в журнале самотеста не объяснялись). Поведение то же.
		//   ★ Ревизия: строка пишется и когда клавишу зажали при уже зажатой ЛКМ (защёлка — «за это нажатие ЛКМ уже записано»).
		static bool bLmbLogged = false;   // за текущее нажатие ЛКМ строка уже записана (один свой контроллер)
		const bool bLmbDown = KbRawMouseDown(1);
		if (!bLmbDown) { bLmbLogged = false; }
		if (bLmbDown && !bLmbLogged && (bFwd || bBack || bLeft || bRight || bAutoRunActive))
		{
			bLmbLogged = true;
			UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] ЛКМ зажата — флаги хода W=%d S=%d A=%d D=%d и автобег=%d сняты (эталон GetAsyncKeyState(1)); источник: %s"),
				(int32)bFwd, (int32)bBack, (int32)bLeft, (int32)bRight, (int32)bAutoRunActive,
				KbDebugLmb >= 0 ? TEXT("подмена самотеста") : (IsInputKeyDown(EKeys::LeftMouseButton) ? TEXT("события ввода игры") : TEXT("физическая кнопка мыши")));
		}
		if (bLmbDown) { bFwd = bBack = bLeft = bRight = false; bAutoRunActive = false; }
	}
	bool bF = false, bB = false, bL = false, bR = false;
	// Нотис автобега (плашка над чатом, `CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)`).
	if (bKbAutoRunNotice != bAutoRunActive)
	{
		bKbAutoRunNotice = bAutoRunActive;
		UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] автобег %s — нотис %d"), bAutoRunActive ? TEXT("включён") : TEXT("выключен"),
			bAutoRunActive ? kLocAutoRunOn : kLocAutoRunOff);
		AsdaNotice::PlankLoc(GetGameInstance(), EAsdaMsgCategory::System, bAutoRunActive ? kLocAutoRunOn : kLocAutoRunOff,
			kNoticeColor, 10000.f, 51);
	}
	// ── 6. Нажатые клавиши, свежее нажатие W/S (таймер 10 мс) ──
	if (bFwd && bAllowed)
	{
		bF = true; bAny = true;
		if (!bKbFwdLatch) { KbTimeoutMs = 10; KbLastRepeatMs = NowMs; }
		bKbFwdLatch = true; bKbBackLatch = false;
	}
	else { bKbFwdLatch = false; }
	if (bBack && bAllowed)
	{
		bB = true; bAny = true;
		if (!bKbBackLatch) { KbTimeoutMs = 10; KbLastRepeatMs = NowMs; }
		bKbBackLatch = true; bKbFwdLatch = false;
	}
	else { bKbBackLatch = false; }
	if (bLeft && bAllowed)  { bL = true; bAny = true; }
	if (bRight && bAllowed) { bR = true; bAny = true; }
	// ── 7. W+S: побеждает нажатая позже; пока держатся обе — таймер сбрасывается КАЖДЫЙ кадр ──
	const bool bBothFB = bF && bB;   // ★ 22.10: обе нажаты — 4006 у эталона не уйдёт, пока держатся (для `KbIntentStop`)
	if (!bF && !bB) { MoveFBPriority = 0; }
	if (bF)
	{
		if (bB) { if (MoveFBPriority != 1) { KbTimeoutMs = 10; KbLastRepeatMs = NowMs; bB = false; } }
		else    { MoveFBPriority = 1; }
	}
	if (bB)
	{
		if (bF) { if (MoveFBPriority != 2) { KbTimeoutMs = 10; KbLastRepeatMs = NowMs; bF = false; } }
		else    { MoveFBPriority = 2; }
	}
	// ── 8. Вектор и «последнее направление» [128]; «пятка» [208] = S нажата ──
	Ch->SetEtalonBackpedal(bB);
	const int32 PrevDir128 = KbLastDir128;
	if (bF) { Dir = FVector3f((float)GFwd.X, 0.f, (float)GFwd.Y); KbLastDir128 = 1; }
	if (bB) { Dir = FVector3f((float)-GFwd.X, 0.f, (float)-GFwd.Y); KbLastDir128 = 2; }
	// ── 9. A/D: с ПКМ — стрейф, без — разворот камеры 0.05 рад на базу кадра (знак по ФЛАГУ S, не по «нажата») ──
	const bool bRmb = KbRawMouseDown(2);
	const float TurnDeg = (float)(FMath::RadiansToDegrees(0.05) * FrameDelta);
	if (bL && !bR)
	{
		if (bRmb) { Dir -= FVector3f((float)GRight.X, 0.f, (float)GRight.Y); }
		else      { CamYawDeg += (bBack ? +1.f : -1.f) * TurnDeg; }   // знак зеркален свопом осей игра→UE (`AsdaCoord`)
	}
	if (bR && !bL)
	{
		if (bRmb) { Dir += FVector3f((float)GRight.X, 0.f, (float)GRight.Y); }
		else      { CamYawDeg += (bBack ? -1.f : +1.f) * TurnDeg; }
	}
	if (Ch->CameraArm) { Ch->CameraArm->SetRelativeRotation(FRotator(CamPitchDeg, CamYawDeg, 0.f)); }

	if (!bAny) { KbRefuseShownLoc = 0; }   // «один раз за удержание» — новое нажатие снова покажет отказ
	// ── 10. Нажата любая ──
	if (bAny)
	{
		const int32 St = KbEtalonActionState();
		if (St == 6 || St == 7 || St == 8 || St == 9)
		{
			// `isMovementActionState` → `charToggleSitStand@0xC54A70`: 6 — ничего; занят (+47552 == 1) — ничего;
			//   «идёт» — 4008 (здесь недостижимо: состояние 7…9); 8 — встать: `SetAction(9)` + 4024 код 109 в своей точке;
			//   7/9 — ничего (`charActionStateIs7to9@0xA210C0` закрывает ветку «сесть»; открытое место отчёта закрыто).
			//   Иконку кнопки «сесть/встать» (`UI_SetSitStandButtonIcon` + `CHotbar_UpdateAnimSlots`) ведёт `ToggleSitStand`
			//   (27.09.2026; реестр «Ход клавишами», п.8 закрыт).
			// ★ 27.09.2026 (pkt4024_facing.md §6): вся функция эталона — единой точкой `ToggleSitStand` (иконки,
			//   гейты); при `asda.Char.SitToggleEtalon 0` — прежняя ветка ниже.
			if (IsSitToggleEtalon())
			{
				ToggleSitStand(TEXT("клавиши хода"));
			}
			else if (St == 8)
			{
				Ch->SetAnimState(EAsdaPlayerState::Idle);   // сидел → хвост «встаёт» (фаза 9), затем покой
				// ★ Ревизия WASD, МЕЛК-6 — аргументы 1:1: `Send_CharStatePos(Target_GetSelectedUserIdx(LP), v.x, v.y, v.z, 109)`.
				//   `Target_GetSelectedUserIdx@0x9254E5` → `Char_GetHoveredUserIdx@0x966490` = `*(WORD*)(this + 95094)` — это СВОЙ
				//   userNum (миснейм; то же слово — первый аргумент каждого 4008), у порта `SelfUserNum` внутри отправителя.
				//   Вектор — `effectSlots[112].gap_298+12` поля 5..7 (`sub_9F1250`) = НАПРАВЛЕНИЕ, куда смотрит персонаж: его
				//   пишут вместе с `charComputeFacingAngleFromDir` (game.c:78554), а приём 4025 (`Recv_CharSpawnFx`) кладёт +5/+9
				//   обратно в то же поле. То есть в теле — не позиция, а курс (x, z). Берём курс меша порта —
				//   общей точкой `GetFacingDirGame` (27.09.2026, та же у эмоций, сидения и танцпола; pkt4024_facing.md).
				if (Net && Net->bInGameConnected)
				{
					const FVector2D FaceGame = Ch->GetFacingDirGame();   // единичный курс (x, z)
					Net->SendCharStatePos4024(109, (float)FaceGame.X, (float)FaceGame.Y);
				}
				UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] сидит → встать (4024 код 109), хода в этот кадр нет (эталон)"));
			}
		}
		else if (KbCanMoveCheck())
		{
			if (bKbWorldTickActive)
			{
				// [ПОРТ] Общие точки порта «игрок взялся за управление» — как при ключе 0 (эталонный маршрут у клавиш
				//   приостановлен `g_bWorldTickActive` и стирается отпусканием; у порта маршрут снимается сразу).
				CancelRoute();
				CancelPendingSkillApproach(TEXT("ход клавишами"));
				NotifyPlayerStartedMoving(TEXT("ход клавишами"));
				CancelTravelIfManual(TEXT("клавиатура"));
				// ★ 29.10.2026 [ПОРТ] (asda.Map.DestMarkClear): маршрут у порта стёрт сразу — и метка цели на картах (у эталона — при отпускании).
				AsdaDestMarkClear(GetGameInstance(), TEXT("ход клавишами (маршрут снят)"));
				UE_LOG(LogAsdaKbMove, Log, TEXT("[клавиши] клавиатура активна (эталон): W=%d S=%d A=%d D=%d ПКМ=%d автобег=%d"),
					bF, bB, bL, bR, bRmb, bAutoRunActive);
			}
			bKbWorldTickActive = false;          // `g_bWorldTickActive = 0` (миснейм: «клавиатура активна»)
			// ★ 22.10.2026 [ИСПРАВЛЕНИЕ 2] «намерения хода нет» (вектор 0 — A/D без ПКМ, A+D; либо W+S) → покой сразу + 4008,
			//   один раз до нового ненулевого 4006. Ключ 0 — как оригинал (персонаж добегает прежний грант).
			const bool bNoIntent = (Dir == FVector3f::ZeroVector) || bBothFB;
			if (CVarAsdaMoveKbIntentStop.GetValueOnGameThread() != 0 && bNoIntent && !bKbIntentStopped)
			{
				KbIntentStop(Ch, bBothFB ? TEXT("W+S") : (bL && bR) ? TEXT("A+D") : TEXT("W/S отпущены, A/D без ПКМ — разворот камеры"), NowMs);
			}
			// ★ 22.10.2026 [ИСПРАВЛЕНИЕ 1] интервал 250 мс держится, пока вектор отличается от вектора УШЕДШЕГО 4006 (эталон —
			//   от вектора прошлого кадра: 250 мс живёт один кадр). `KbPrevDir` ведём как у эталона (читателей больше нет).
			const bool bDirPending = CVarAsdaMoveKbDirPending.GetValueOnGameThread() != 0;
			if (bDirPending)
			{
				if (Dir != KbPrevDir) { KbPrevDir = Dir; }
				KbTimeoutMs = (!bKbSentDirValid || Dir != KbSentDir) ? 250 : 1000;
			}
			else if (Dir != KbPrevDir) { KbPrevDir = Dir; KbTimeoutMs = 250; }   // `Vector3_NotEqual` — точное сравнение float
			else                  { KbTimeoutMs = 1000; }
			if (KbEtalonActionState() != kStateMove && (bF || bB)) { KbTimeoutMs = 10; }
			// ★ 22.10.2026 [ИСПРАВЛЕНИЕ 1] попытка при бите 2 (4006 в пути) таймер НЕ съедает: эталон ставил метку и отбрасывал
			//   попытку (`CGame_HandleGroundClickAttack` → 0), следующая — через полный интервал; у нас — в первый кадр после ответа.
			const bool bWaitBit2 = bDirPending && Net && Net->IsMoveRequestInFlight();
			if (!bWaitBit2 && NowMs - KbLastRepeatMs > (double)KbTimeoutMs)
			{
				KbLastRepeatMs = NowMs;
				const FVector2D Pos = Ch->GetGamePosXZ();
				FVector2D Tgt(Pos.X + (double)Dir.X * kStepUnits, Pos.Y + (double)Dir.Z * kStepUnits);
				// `terrainGetCellByPos`: X/Z < 0 → 100, > 51100 → 51100 (высота — террейн; проверки препятствий нет,
				//   `sub_93924C` — заглушка `return 0`).
				if (Tgt.X < 0.0) { Tgt.X = 100.0; }
				if (Tgt.X > 51100.0) { Tgt.X = 51100.0; }
				if (Tgt.Y < 0.0) { Tgt.Y = 100.0; }
				if (Tgt.Y > 51100.0) { Tgt.Y = 51100.0; }
				// Смена вперёд↔назад, пришедшаяся на кадр отправки, → 4008 в текущую точку (гейт `g_bActionPending`).
				if (KbLastDir128 != PrevDir128 && Net && Net->bInGameConnected) { Net->SendStop4008((float)Pos.X, (float)Pos.Y); }
				// ★ 01.11.2026: ход клавишами посреди атаки — у эталона отложенное действие 16: `SetAction(0)` (4024/400) и ПОТОМ 4006
				//   (entity_char.c:24085-24112); сервер 4024 «в движении» не принимает — 400 строго до хода.
				if (bAutoAttack || bPvpAttack) { AsdaLeaveAttackAction(this, TEXT("ход клавишами")); }
				const bool bSent = KbHandleGroundClick(Tgt);
				if (bSent)
				{
					// ★ 22.10.2026: вектор ушедшего 4006 (интервал [ИСПРАВЛЕНИЕ 1]); ненулевой ход снова «ждёт» свой грант.
					KbSentDir = Dir; bKbSentDirValid = true;
					if (!bNoIntent) { bKbIntentStopped = false; }
					// ★ 22.10.2026: локальный старт (`asda.Move.LocalStart`) — персонаж трогается сразу, грант подгонит.
					if (!bNoIntent) { LocalStartAfter4006(Ch, Pos, Tgt, TEXT("клавиши")); }
					else            { NoteMove4006WithoutLocalStart(); }   // 4006 «в свою точку» (ВАЖНО-5)
				}
				UE_LOG(LogAsdaKbMove, Verbose, TEXT("[клавиши] попытка хода к (%.0f,%.0f) интервал %d мс: 4006 %s"),
					Tgt.X, Tgt.Y, KbTimeoutMs, bSent ? TEXT("ушёл") : TEXT("не ушёл"));
				// Затем БЕЗУСЛОВНО (и при отброшенной попытке): цель атаки (младшее слово `targetRenderId`), отложенный
				//   подбор (`id58`), цель взаимодействия (`id5C`).
				if (bAutoAttack)
				{
					ASDA_CTRACE(EAsdaCT::Exit, TEXT("ход клавишами (схема оригинала) к (%.0f,%.0f): автоатака снята, замах остановлен, 4006 %s"),
						Tgt.X, Tgt.Y, bSent ? TEXT("ушёл") : TEXT("не ушёл"));
					ASDA_CTRACE_END(TEXT("ход клавишами"));
					bAutoAttack = false; bCombatStarted = false; bWaitingCombatAck = false; bInCombatStance = false;
					Ch->StopAttackAnim();
					ExitCombatStance();
				}
				bLootPending = false;
				if (bNpcTalkPending) { ClearPendingNpcTalk(TEXT("ход клавишами")); }
			}
		}
	}
	else if (!bKbWorldTickActive)
	{
		// ── 11. Ничего не нажато, первый такой кадр — отпускание ──
		KbRefuseShownLoc = 0;
		KbRelease(Ch);
		bKbWorldTickActive = true;
	}
}
