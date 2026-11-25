// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPlayerMenuSubsystem — КОНТЕКСТНОЕ МЕНЮ ПО ЧУЖОМУ ПЕРСОНАЖУ (ПКМ по игроку).
//   JUI-окно `TRANTOOL_WIN` **IDNum 2000** (класс-владелец `CExchange`, поле +0x2710
//   `pTradeReqWin`). Полный разбор с адресами — `Reports/player_context_menu.md`.
//   Ни одно число здесь не выдумано: у каждого есть ссылка на `RE/etalon/*.c`,
//   `Data/Decoded/jui/gamemain_jui.ini` или `RE/ida/export/AsdaStory_ida.c`.
//
// ═══════════════════ 1. КАК МЕНЮ ОТКРЫВАЕТСЯ (эталон, 1:1) ═════════════════════════════════════
//   Всё живёт в ЕДИНОМ обработчике ввода `GameInput_DispatchEvent@0xC6DCA0` (game_main.c:9206),
//   который регистрируется у менеджера окон (`sub_92EF27(g_pWindowMgr, GameInput_DispatchEvent)`,
//   ui_core.c:54114). Коды событий доказаны телами очередей (ui_core.c:113561-113670):
//       msg[0]=3 — КНОПКА НАЖАТА  (`CWindowMgr_QueueLButton`,   a2: 0=ЛКМ 0x201, 1=ПКМ 0x204)
//       msg[0]=4 — двойной клик    (`CWindowMgr_QueueLButtonUp`, 0x203)
//       msg[0]=5 — движение мыши   (`CWindowMgr_QueueMouseMove`, 0x200)
//       msg[0]=6 — КНОПКА ОТПУЩЕНА (`CWindowMgr_QueueRButton`,   a2: 0=ЛКМ 0x202, 1=ПКМ 0x205)
//       msg[0]=7 — колесо          (`CWindowMgr_QueueMouseWheel`, 0x20A)
//
//   ★★★ ЧТО ОЗНАЧАЮТ БАЙТЫ СОСТОЯНИЯ (`CObject_GetStateByteAt(obj, i)` = `*(obj + i + 304)`).
//   Писатель — `input_ProcessMoveCommand@0x12F0290` (AsdaStory_ida.c:1216898-1216993), насос
//   очереди ввода. ИНДЕКС = КНОПКА, ЗНАЧЕНИЕ = ФАЗА:
//       индекс 0 = ЛЕВАЯ, 1 = ПРАВАЯ, 2 = СРЕДНЯЯ
//       значение 1 = нажата (msg 3) · 2 = двойной клик (msg 4) · 0 = отпущена (msg 6) ·
//                −1 = «событие не про эту кнопку»
//   Пример из кода: msg 3 при a2==1 пишет `[304]=-1, [305]=1, [306]=-1`; msg 6 при a2==1 —
//   `[304]=-1, [305]=0, [306]=-1`.
//   Перекрёстная проверка: `CControl_OnLButtonDown` требует (1, ≠1, ≠1) = левая нажата,
//   `CControl_OnRButtonDblClick` — ([305]==1, [304]≠1, [306]≠1), `CControl_OnLButtonClick` —
//   ([304]==0, [305]≠0, [306]≠0) = левая отпущена. Всё сходится.
//
//   ⇒ ОБЕ ВЕТКИ МЕНЮ — ПРАВАЯ КНОПКА, И ВЫДЕЛЕНИЕ ЛКМ ОРИГИНАЛУ НЕ НУЖНО:
//       case 3 (`GetStateByteAt(ev,1) == 1`)  = ПКМ НАЖАТА
//       case 6 (`GetStateByteAt(ev,1) == 0`)  = ПКМ ОТПУЩЕНА
//     `sub_C7B550` сверяет цель отпускания с целью НАЖАТИЯ ТОЙ ЖЕ ПКМ — это проверка целостности
//     клика, а не «цель должна быть выбрана». Цель берётся ХИТ-ТЕСТОМ ПОД КУРСОРОМ
//     (`Input_PickEntityAltClick`), выделение (`g_nTargetUserIdx`, кольцо) НЕ трогается.
//     Под курсором никого нет → `case 3` пишет −1, `case 6` зовёт `uiDialog_ClearAndHide` (гаснет).
//
//   case 3 (ПКМ НАЖАТА — `CObject_GetStateByteAt(ev,1)==1`), game_main.c:9375-9392:
//       if (Input_PickEntityAltClick(&g_InventoryBag, x, y) == 1) {
//           if (!dword_2903C20) {
//               pickedCharObj = CharManager_FindByUserIdx(g_CharManager, g_nPickedUserIdx);
//               if (pickedCharObj) set_o9C0(g_pExchangeWnd, pickedCharObj->userIdx);   // +0x2700
//           }
//       } else set_o9C0(g_pExchangeWnd, -1);
//
//   case 6 (ПКМ ОТПУЩЕНА — `GetStateByteAt(ev,0) && !GetStateByteAt(ev,1)`, т.е. левая ≠ «отпущена»
//           (у ПКМ-события там −1) И правая == «отпущена»), :9398-9436:
//       if (Input_PickEntityAltClick(&g_InventoryBag, x, y) == 1) {
//           if (!dword_2903C20) {
//               p = CharManager_FindByUserIdx(g_CharManager, g_nPickedUserIdx);
//               if (sub_C7B550(g_pExchangeWnd, p->userIdx))              // ★ ГЕЙТ «ТОТ ЖЕ ИГРОК»
//                 if (!p->effectSlots[112].field_40)                     // ★ ГЕЙТ «НЕ Я САМ»
//                 {
//                   set_pos(g_pExchangeWnd, x, y);                       // +0x2698/+0x269C
//                   if (g_nExchangeTargetUserIdx != localPlayer->userIdx) // ★ ГЕЙТ «обмен не занят»
//                   {
//                     if (!g_bExchangeTimerActive) set_o9C2(g_pExchangeWnd, p->userNum);  // +0x2708
//                     cPopup_ShowClampedToScreen(g_pExchangeWnd, p);     // ПОКАЗ
//                   }
//                 }
//           }
//       } else uiDialog_ClearAndHide(g_pExchangeWnd);                    // промах — меню гаснет
//
//   ★★★ ГЕЙТ «ТОТ ЖЕ ИГРОК» — `sub_C7B550@0xC7B550` (shop_trade.c:22332), ЦЕЛИКОМ:
//         return a2 == *(this + 2496) && a2 != -1 && *(this + 2496) != -1;   // 2496*4 = 0x2700
//       Поле 0x2700 пишется в `case 3`, то есть на НАЖАТИИ ПРАВОЙ. ⇒ смысл гейта: «нажатие и
//       отпускание ПКМ пришлись на ОДНОГО И ТОГО ЖЕ игрока». Предварительное выделение ЛКМ
//       НЕ ТРЕБУЕТСЯ. Промах ПКМ по земле пишет −1 и меню не откроется (а открытое — погаснет).
//       У нас эквивалент — `LastLeftPickUserNum`, который пишет
//       `AAsdaPlayerController::OnRightPressed` (ключ отладки asda.PlayerMenu.RequireSamePress).
//
//   ★★★ ГЕЙТ «НЕ Я САМ» — `effectSlots[112].field_40`. Доказательство, что это признак ЛОКАЛЬНОГО
//       игрока, а не «мёртв»/«в лавке»: тот же флаг проверяет ветка приглашения в отряд
//       (`exchangeUiOnCommand` case 0x7D1, shop_trade.c:6559-6565) и при TRUE печатает нотис
//       **1093 «Приглашать себя нельзя»**. Другого смысла у строки нет.
//
//   ПОКАЗ — `cPopup_ShowClampedToScreen@0xC75690` (ui_core.c:35093):
//       if (this->g_bPartyInviteFlag)  { нотис 5042 в чат, цвет −75279, канал 10000; НЕ показывать }
//       else {
//         this[13] = 1;                                      // +0x34 = g_bExchangeWndOpen
//         CControl_SetText(this[2512], this + 1224);          // 2007 := имя цели (+0x4C8)
//         x = this[2494]; y = this[2495];                     // точка ПКМ
//         GetSize(this[2500], &w, &h);
//         if (screenW < x + w) x -= (x + w) − screenW;        // ★ КЛАМП В ЭКРАН ЕСТЬ В ОРИГИНАЛЕ
//         if (screenH < y + h) y -= (y + h) − screenH;
//         CControl_SetField74_75(this[2500], x, y);           // ★ РОВНО В ТОЧКУ КУРСОРА, без сдвига
//         CControl_SetVisible(this[2500], 1);
//       }
//   ⚠ Регистрации «как верхнее окно» (`CClass_MapInsert`) тут НЕТ. У нас она нужна (закон
//     ui-window-z-layers): `PushTopmost`, иначе меню окажется под уже открытыми окнами.
//
//   ЗАКРЫТИЕ — `uiDialog_ClearAndHide@0xC75850`:
//       this->clearFlag = 0; CControl_SetText(this->pQtyText, ""); SetVisible(окно, 0);
//   Зовётся из: промаха ПКМ (выше), КАЖДОГО пункта меню, и из «игрок начал двигаться»
//   (input.c:869 и ui_core.c:1260 — `if (g_bExchangeWndOpen == 1) uiDialog_ClearAndHide(...)`).
//   ⇒ у нас — штатный `UAsdaWindowSubsystem::RegisterNpcWindowCloser` (копий не заводим).
//
// ═══════════════════ 2. СОСТАВ ОКНА (gamemain_jui.ini:7220-7588) ═══════════════════════════════
//   Окно `TRANTOOL_WIN` 2000, скин `Popup_SmWin`, Origin «345 145», Size «158 276», Visible=0.
//   Подписи ставит `CExchange_BuildUI@0xC77720` (shop_trade.c:7258-7540) — колонка «loc».
//
//   idnum  idc                  y    loc    подпись (RU, GameText.csv)     что делает
//   ─────  ───────────────────  ───  ─────  ────────────────────────────  ──────────────────────────
//   2007   TRANTOOL_IDTEXT        8   —      имя цели                      заголовок (ставит показ)
//   2001   TRANTOOL_BUT01        27   6800  «Пригласить в отряд»          локальный диалог 2250
//   2003   TRANTOOL_BUT03        46   6802  «Вступить в клан»             C→S 4108
//   2053   TRANTOOL_BUTDISABLE   36   6801  «Предложить Дружбу»           ★ МЁРТВАЯ (см. §4)
//   2055   TRANTOOL_BUT09        65  12819  «Добавить приятеля»           C→S 4150
//   2002   TRANTOOL_BUT02        84   6801  «Предложить Дружбу»           C→S 5200 (соулмейт)
//   2060   TRANTOOL_BUT10        84   —     (из .JUI «소울메이트»)          ★ МЁРТВАЯ (см. §4)
//   2004   TRANTOOL_BUT04       107   6803  «Обмен предметами»            C→S 5282, режим 0
//   2005   TRANTOOL_BUT05       126   6804  «Обмен личными вещами»        C→S 5282, режим 1
//   2006   TRANTOOL_BUT06       149   5061  «Информация о персонаже»      C→S 5478
//   2054   TRANTOOL_BUT08       168   6921  «Шепот»                       чат: «/W имя », без пакета
//   2062   TRANTOOL_BUT11       187   6958  «Следовать»                   ЛОКАЛЬНО, без пакета
//   2063   TRANTOOL_BUT12       206   6959  «Просмотреть блог»            C→S 6082 ★ МЁРТВАЯ (§4)
//   2070   TRANTOOL_BUT13       225   6963  «Вызвать на поединок»         C→S 4302
//   2052   TRANTOOL_BUT07   низ−28   5035  «Закрыть»                      только скрыть
//   —      TRANTOOL_WIN_MOVE      0   —      158×25                        ручка EVS{EVT 6 → 20003}
//
// ═══════════════════ 3. ЖИВЫЕ ПУНКТЫ: СЕТЬ (adресa + побайтовая раскладка) ═════════════════════
//   Общая шапка ВСЕХ пакетов ниже: [0..7] анти-реплей секрет, [8..23] MD5(payload), [24..] payload.
//   У ВСЕХ вызовов `CNetwork_Send(..., len, OP, 1, 1, 1, 0, 0)` шестой аргумент **a6 = 1**
//   ⇒ паддинг ПРИМЕНЯЕТСЯ (закон о `sub_5218A0`; у нас это `bPad=true`, умолчание `SendPacket`).
//
//   2003 клан   `partySendInviteAuth@0xBC96F0` (social.c:3415), len **26**, OP **4108**
//               payload(2) = u16 userNum цели (`CCharacter+190188` — сессионный индекс, i16!).
//               ГЕЙТ: `localPlayer->gap_8BE[3]` ≠ 0 (★ поправка 27.09.2026: это НЕ звание, а SoulState —
//               «душа»/одержимость, 6152 @12) → снять верхнее окно-сообщение + нотис **12435**, пакет НЕ шлётся
//               (`UAsdaClanSubsystem::RefuseIfSoulmateLocked`, `friend_gate_12435.md`).
//               S→C **4109**: byte0 = код (0,2..8 → нотисы 12413/12415/12416/12417/12419/12420/
//               12421/12168; 1 = успех ОБОИМ: [3..4] userNum ПРИГЛАШЁННОГО, [5..6] clanId, [7..] имя клана →
//               нотис 20051 с ИМЕНЕМ персонажа @3, затем 4138 и, при видимой вкладке клана, 4122+4124 —
//               ★ 27.09.2026, network.c:12039-12066).
//
//   2055 друзья `netSendGuarded_op4150@0xCE7680`, len **26**, OP **4150**
//               payload(2) = u16 userNum цели.
//               ГЕЙТ: цель уже в списке друзей → нотис **12812**, пакет НЕ шлётся.
//               S→C **4151**: byte0 = код (0→12808, 2→12811, 3→12812, 4→12813, 5→20082;
//               1 = успех: запись 31 б с +1, имя с +12, нотис **20000** со слотом-именем).
//
//   2002 соулм. `coupleSendProposalReq@0xE658E0` (ui_core.c:72763), len **169**, OP **5200**
//               ⚠ У ЭТОГО пакета шапка ДРУГАЯ: [0..15] MD5(payload,153), payload с [16].
//               payload(153): @0 u8 режим (`CSoulmateWnd` +1900: 1, а при открытом 9100 — 2) · @1 char[20] имя цели ·
//               @21 u16 = u8 (localPlayer+1082) уровень · @23 u8 (+1079) пол/раса ·
//               @24 u8 (+1080) класс · @25 char[128] текст «о себе» (`CFindSoulmateWin+516`, дефект D15).
//               ГЕЙТЫ `soulmateWnd[472]` и `+0xC[132]` у эталона МЕРТВЫ (волна 1-А, R21). S→C **5201**.
//               ★ Волна 1-А: пункт зовёт общую точку `UAsdaSoulmateSubsystem::SendProposal5200`.
//
//   2004/2005   `CExchange_SendRequest@0xC7BE30` / `CExchange_SendAcceptRequest@0xC7C6E0`,
//   обмен       len **27**, OP **5282**. ЕДИНСТВЕННОЕ различие двух функций — байт режима:
//               payload(3) = u16 userNum цели (+0x4C6) | u8 режим (**0** обычные / **1** личные).
//               ГЕЙТЫ (одинаковые у обеих, shop_trade.c:6641-6712): рюкзак заперт → 5043 ·
//               лавка → 5033 · инвентарь → 5032 · парикмахерская → 13016 · работа с предметом →
//               13104 · склад → 10307 · окно изготовления/кол-ва → молча · `g_bPartyInviteFlag`
//               → 5042.  Плюс внутри отправителя: цель == я → молча; заявка в полёте → **5054**.
//               ПОСЛЕ отправки: нотис **20090** со слотом-именем, `requestActive=1`,
//               `requestStartTime=now`.  ТАЙМАУТ — `CExchange_SendCancelTimeout@0xC7B5F0`:
//               через **33 000 мс** снимает флаг и шлёт OP **5286** (len 30, payload = i32 −2 +
//               u16 userNum).  S→C **5283** (заявка партнёру), **5287** (обмен закрыт).
//
//   2006 инфо   `antiCheat_sendItemFlagDigest@0xDA9000`, len **31**, OP **5478**
//               payload(7) = u16 userNum @0 | i32 charId @2 | u8 (цель+1076) @6.
//               ★ ЗДЕСЬ ОБА ИДЕНТИФИКАТОРА СРАЗУ — перепутать нельзя (закон charid-vs-usernum).
//               ГЕЙТ: `otherCharaWnd[40]` — запрос уже в полёте → нотис **9419**.  S→C **5479**.
//
//   2070 дуэль  `netSendDuelRequest@0xDE4340` (network.c:27150), len **58**, OP **4302**
//               payload(34): @0 u16 userNum МОЙ · @2 i32 charId МОЙ · @6 char[20] имя МОЁ ·
//               @26 u16 userNum ЦЕЛИ · @28 i32 charId ЦЕЛИ · @32 i16 = **−1** (хвост-заглушка).
//
//   2063 блог   `netSendReqStr_op6082@0xFBF9B0`, len **44**, OP **6082**
//               payload(20) = char[20] имя цели.  S→C 6083 (логин витрины item-mall).
//
//   2054 шёпот  ПАКЕТА НЕТ. `Chat_BeginWhisperTo@0xBB0DB0` (chat.c:4888) целиком:
//                 if (strlen(name) > 0) { edit(2609) := "/W <имя> "; chatParseSlashCommand();
//                                         SetFocus(2609); }
//               Сам 5088 уйдёт позже, когда игрок допишет текст и нажмёт Enter.
//
//   2062 следов. ПАКЕТА НЕТ — чистый клиентский автобег. `uiPreparePartyTradeRequest@0xFBE840`
//               (ui_core.c:99410) взводит состояние: [+240]=1, [+300]=1, [61]=0 (режим «за
//               игроком»), [63]=userNum цели, [64]=charId цели. Нотис **20311** со слотом-именем.
//               ГЕЙТЫ: открыто окно продажи → **107022** · открыт склад → **10305** ·
//               цель не найдена → **20308**. Плюс в самом пункте меню (shop_trade.c:6460-6469):
//               `g_bWorldTickActive && !g_pPartyTargetObj && !sub_922D5D(g_pGachaScene)`.
//               ТИК — `followTargetMoveTick@0xFBECA0` (math_geom.c:2290):
//                 карта типа 6 → отмена; дистанция ≥ 6.0 «единиц/100» → бежим; > 10.0 и прошло
//                 3000 мс (0xBB8) → новый приказ движения к позиции цели; < 6.0 → стоп в точке
//                 «цель − 150 юнитов».  ОТМЕНА — `followTargetCancelReset@0xFBEBD0`: нотис
//                 **20310**, поля в −1. Зовётся при ЛЮБОМ ручном действии (input.c:190/304/389/572).
//
//   2001 отряд  ПАКЕТА НЕТ на этом шаге. `exchangeUiOnCommand` case 0x7D1 (shop_trade.c:6553-6620)
//               гоняет ЛОКАЛЬНЫЕ проверки и открывает диалог `PARTYTAKE_WIN` **2250**
//               (`partyUi_ShowInviteRequest@0xB99630`, нотис **20020** со слотом-именем;
//               2-й аргумент −1 = «я не в отряде», иначе правило дележа лута).
//               Отказы: цель — я сам → **1093** · уже в моём отряде → **1047** ·
//               `sub_925A3F` → **1056** · `sub_924E28` → **1045** · я не глава → **1089**.
//               Пакет приглашения уходит уже ИЗ окна 2250 (`Party_Send_InviteByName`).
//
// ═══════════════════ 4. МЁРТВЫЕ ПУНКТЫ — С ДОКАЗАТЕЛЬСТВОМ ═════════════════════════════════════
//   2060 `TRANTOOL_BUT10` — в .JUI Visible="0" И `CWindowMgr_FindControlByID(2060)` в эталоне
//        НЕ ВЫЗЫВАЕТСЯ НИ РАЗУ (grep «2060» по всем 30 файлам эталона: попаданий-контролов 0).
//        Обработчика msgId 2060 в `exchangeUiOnCommand` тоже нет. ⇒ мертва в ДАННЫХ и в КОДЕ.
//   2053 `TRANTOOL_BUTDISABLE` — контрол ищется и ему ставится подпись (loc 6801), но
//        `CControl_SetVisible(pBtnDisable, 1)` не вызывается НИГДЕ (в отличие от 2001-2007, где
//        SetVisible(1) стоит явно), а в .JUI Visible="0". Обработчика msgId 2053 нет.
//        ⇒ невидима всегда. Это «серая копия» пункта соулмейта — задел, который не включили.
//   2063 `TRANTOOL_BUT12` — обработчик ЕСТЬ (msgId 2063 → 6082), но `CExchange_BuildUI` явно
//        делает `CControl_SetVisible(pListMenu2, 0)` (shop_trade.c:7345) и больше её никто не
//        показывает. ⇒ ветка кода живая, кнопка недостижима. Мы её НЕ оживляем (закон
//        no-invented-ui), но отправитель 6082 реализован и доступен консольной командой.
//   ★ 2070 `TRANTOOL_BUT13` — НАОБОРОТ: в .JUI Visible="0", но `BuildUI` делает
//        `CControl_SetVisible(pListMenu3, 1)` (shop_trade.c:7351). ⇒ ЖИВАЯ. Верить .JUI нельзя.
//
// ═══════════════════ 5. НУМПАД 1410 ═══════════════════════════════════════════════════════════
//   НЕ УЧАСТВУЕТ. Доказательство: `uiOpenContextPopupWithHandlers(g_pNumPadDialog, …)` в
//   shop_trade.c встречается только в `exchangeWndOnAddItem`/`exchangeWndOnMoveItem` (строки
//   6783/6827/6871/7036/7078) — это перекладывание СТОПКИ ПРЕДМЕТОВ В ЛОТОК УЖЕ ОТКРЫТОГО окна
//   обмена 2008 (режим 3 по `NUMPAD_FLOW.md`), а не меню 2000. В `exchangeUiOnCommand` (весь
//   разбор msgId меню) вызова нумпада НЕТ ни в одной ветке.
//
// ═══════════════════ 6. ЧТО ПОРТИРОВАНО ЗДЕСЬ, А ЧТО ЖДЁТ СИСТЕМ ══════════════════════════════
//   ПОЛНОСТЬЮ: открытие/закрытие/позиция/гейты меню, подписи, шёпот (2054), следование (2062),
//     закрытие (2052) и ОТПРАВКА всех шести пакетов (4108/4150/5200/5282/5478/4302 + 6082/5286).
//   ЖДЁТ СИСТЕМ (записано в `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`): окно `PARTYTAKE_WIN` 2250
//     (пункт 2001), окно обмена `TRAN_WIN` 2008 (2004/2005), окно `OTHERCHARA_DATA` 4200 (2006),
//     окно соулмейта 8200/6500 (текст заявки для 5200).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaMsgCategory.h"   // ★ R1: EAsdaMsgCategory — категория события (без класса подсистемы сообщений)
#include "AsdaSessionReset.h"          // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaPlayerMenuSubsystem.generated.h"

class UButton;
class UTextBlock;
class UUserWidget;
class UAsdaButtonRelay;

UCLASS()
class ASDAREBORN_API UAsdaPlayerMenuSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ── UGameInstanceSubsystem ──
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable ──
	// ★★★ КРИТИЧНО ИМЕННО ЗДЕСЬ: `FollowUserNum` — это НОМЕР СЕССИИ (userNum), а не постоянный
	//   идентификатор персонажа. Сервер выдаёт номера заново и при перезаходе, и при входе на
	//   другую карту (мир и подземелья — разные процессы GameServer). Если следование пережило
	//   такое событие, `FollowTick` найдёт по старому номеру ЧУЖОГО игрока и молча побежит за ним.
	//   Поэтому следование гасится на ЛЮБОЙ причине сброса, включая смену карты.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── FTickableGameObject ──
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPlayerMenuSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual bool IsTickableInEditor() const override { return false; }

	// ═══ ВВОД (зовёт AAsdaPlayerController) ═══
	// ★★★ ПКМ-НАЖАТИЕ (а НЕ левое!): `set_o9C0(g_pExchangeWnd, userNum)` / `set_o9C0(..., -1)`
	//   при промахе. Доказательство, что это правая кнопка, — §1 ниже (таблица байтов состояния).
	//   UserNum = −1, если ПКМ не попал в игрока.
	void NotifyRightPress(int32 UserNum);
	// ПКМ-отпускание по актору игрока. Возвращает true, если меню открылось.
	//   UserNum = −1 (промах) ⇒ меню гасится, как `uiDialog_ClearAndHide` в эталоне.
	bool NotifyRightRelease(int32 UserNum, bool bIsSelf);
	// ★ 30.09.2026 (волна О3 торговли, ревизия О2 СРЕДН-2): эталонный `g_nPickedUserIdx@0x2903C24` (поле +0x22D4 объекта пика
	//   с базой 0x2901950, МИСНЕЙМ `g_InventoryBag`). Пишет ЕДИНСТВЕННАЯ функция `Input_PickEntityAltClick@0x996EE0`
	//   (input.c:1305-1307: `[2229] = Target_GetSelectedUserIdx(pick)`) — при УДАЧНОМ ПКМ-пике игрока: нажатие (`case 3`,
	//   game_main.c:9379), отпускание (`case 6`, :9405) и Alt+ПКМ (WM_RBUTTONDOWN, :5845). Своего персонажа пик тоже берёт
	//   (гейт «не я сам» стоит уже у меню). [ОРИГИНАЛ] При промахе, хоткее, команде меню и сбросе сессии поле НЕ меняется.
	//   Читают: меню (game_main.c:9384/9410) и фильтр типа 1 окна обмена (item_inventory.c:23745-23746).
	void NotePickedUser(int32 UserIdx);
	int32 GetPickedUserIdx() const { return PickedUserIdx; }   // userNum последнего удачного ПКМ-пика игрока (до первого пика — 0)

	// ═══ ОКНО ═══
	bool IsMenuOpen() const { return bMenuOpen; }
	void CloseMenu();
	// Имя цели, по которой открыто меню (эталонный `g_szSelectedTargetName`, CExchange+0x4C8).
	const FString& GetTargetName() const { return TargetName; }
	int32 GetTargetUserNum() const { return TargetUserNum; }

	// ═══ ДЕЙСТВИЯ ПУНКТОВ (публичны — их же дёргают консольные диагностики) ═══
	void ActionPartyInvite();     // 2001
	void ActionClanInvite();      // 2003
	void ActionFriendAdd();       // 2055
	void ActionSoulmate();        // 2002
	void ActionExchange(bool bPersonalItems);   // 2004 (false) / 2005 (true)
	void ActionCharInfo();        // 2006
	// ★ 25.10.2026: тело 2006 без меню — порт `antiCheat_sendItemFlagDigest@0xDA9000` (запрос в полёте → 9419, иначе 5478) +
	//   `otherCharaWnd_SetTargetUser@0xDA7640` (окно 4200 ждёт ответ). Зовут меню (2006) и функция 2/5 «ACTION VIEW OTHER INFO»
	//   (клавиша 61, `UI_DispatchTargetContextMenu`). Возврат — 5478 ушёл.
	bool RequestCharInfo(int32 UserNum, int32 CharId, const FString& Name, const TCHAR* Why);
	void ActionWhisper();         // 2054
	void ActionFollow();          // 2062
	void ActionBlog();            // 2063 (мёртвая кнопка, ветка кода живая)
	void ActionDuel();            // 2070

	// ═══ СЛЕДОВАНИЕ (2062) ═══
	// ★ ЕДИНАЯ ТОЧКА ВХОДА для ОБОИХ пунктов «Следовать»: 2062 в меню по игроку и 8101 в меню по
	//   строке отряда. В эталоне они тоже зовут один и тот же `uiPreparePartyTradeRequest`,
	//   различаясь только режимом, — поэтому копии логики заводить нельзя.
	//   `Mode`: 0 — из меню по игроку, 1 — из меню по строке отряда.
	void StartFollow(int32 UserNum, int32 CharId, const FString& Name, int32 Mode);
	bool IsFollowing() const { return bFollowActive; }
	// «Заявка обмена в полёте» — аналог `CExchange+0x14`/`+0x38` эталона (гейт 5054 в `CExchange_SendRequest`, таймаут 33 с).
	//   ★ ПД-2 (29.09.2026): это НЕ `g_bPartyInviteFlag`. Тот флаг = `CExchange+0x2C` = «ОКНО обмена 2008 открыто», его
	//   читатели (ход клавишами 9568, ремесло 5590 и ещё 18) спрашивают общую точку `UAsdaTradeStateSubsystem::IsExchangeWindowOpen`.
	//   ★ 29.09.2026 (волна О1): признак живёт в модели `CExchange` (`UAsdaExchangeP2PSubsystem`, поле +0x14) — здесь форвард.
	bool IsExchangeRequestActive() const;
#if !UE_BUILD_SHIPPING
	// Самотест `asda.Trade.SelfTestGates`: взвести/снять «заявку в полёте» без сети (контрольная пара ПД-2) — форвард в модель.
	void DebugSetExchangeRequestActive(bool bOn);
#endif
	// Отмена следования с нотисом 20310 (`followTargetCancelReset`). Зовёт контроллер при любом
	//   ручном действии игрока — ровно как эталон из input.c:190/304/389/572.
	void CancelFollow(const TCHAR* Why);

	// ★ ЗАКОН «диагностика жмёт САМУ кнопку»: нажать пункт его собственным `OnClicked`,
	//   а не звать обработчик напрямую — иначе проверка подтверждает несуществующее.
	void ClickByNodeName(const TCHAR* Node);

	// Полный структурный дамп ЖИВОГО окна (консоль asda.PlayerMenu.Dump).
	FString DumpMenu() const;

	// ★ 27.09.2026: последний нотис подсистемы — для самотеста 4109 код 1 (`asda.Clan.FriendGateSelfTest`).
	int32 GetNoticeSeq() const { return NoticeSeq; }                  // счётчик напечатанных нотисов
	int32 GetLastNoticeLoc() const { return LastNoticeLoc; }            // loc-id последнего нотиса
	const FString& GetLastNoticeSlot() const { return LastNoticeSlot; } // слот [ss1] последнего нотиса

private:
	// ── виджет ──
	bool EnsureMenu();
	void SetupControls();
	void RegisterZones();
	void UnregisterZones();
	bool GetMenuRect(FVector2D& OutMin, FVector2D& OutMax) const;
	bool GetMenuTopLeft(FVector2D& Out) const;
	void PlaceMenuAt(const FVector2D& Pos);
	void OpenMenuAtCursor();

	// ── следование ──
	void FollowTick();

	// ── ответы сервера (4151/5283/5479/5201/6083) ──
	void OnMenuResult(int32 Opcode, const TArray<uint8>& Body);
	FDelegateHandle HMenuResult;
	bool bNetBound = false;

	// ── общее ──
	// Нотис через ЕДИНЫЙ маршрутизатор (категория → DT_MessageRoutes → системный журнал 2570).
	//   NameSlot заполняет маркер [ss1]; пустой — строка без аргументов.
	void Notice(int32 LocId, const FString& NameSlot = FString()) const;
	// ★ Ревизия С-1 (29.09.2026): нотис в чат ЦВЕТОМ обработчика эталона (5042 → −75279), след самотеста как у `Notice`.
	void NoticeColored(int32 LocId, FColor Color) const;
	// Категория нотиса (маршрут берётся из данных, не из хардкода в месте вызова).
	static EAsdaMsgCategory CategoryForNotice(int32 LocId);
	// Цель ещё рядом и жива? (актор мог деспавниться, пока меню открыто)
	class AActor* ResolveTargetActor() const;

	UPROPERTY(Transient) TObjectPtr<UUserWidget> MenuWidget = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IdText = nullptr;

	bool bMenuReady = false;
	bool bMenuOpen  = false;
	bool bZonesOn   = false;
	bool bCloserBound = false;

	// ★ Эталонное поле CExchange+0x2700: userNum, в который попал ПОСЛЕДНИЙ ЛКМ-даун. −1 = промах.
	int32 LastLeftPickUserNum = -1;
	// ★ `g_nPickedUserIdx` (+0x22D4 объекта пика): userNum последнего УДАЧНО подхваченного ПКМ игрока (см. `NotePickedUser`).
	// [ОРИГИНАЛ] начальное 0: поле статического объекта пика, других писателей в декомпиле нет (скан `[2229] =`) ⇒ обнуление
	//   статической памяти. До первого пика фильтр обмена смотрит игрока с userNum 0 — как эталон.
	int32 PickedUserIdx = 0;
	// Цель открытого меню: +0x4C6 (userNum, i16) и +0x4C8 (имя, 20 б).
	int32   TargetUserNum = -1;
	int32   TargetCharId  = 0;
	FString TargetName;

	// ★ 29.09.2026 (волна О1): защёлка «заявка обмена в полёте», её время и цель таймаута переехали в модель `CExchange`
	//   (`UAsdaExchangeP2PSubsystem`: +0x14, +0x38, +0x2704, +0x2708). Прежний комментарий «+0x2C = заявка» был ошибкой (ПД-2).

	// ★ Запрос «инфо о персонаже» (otherCharaWnd[40]) — защёлка с таймаутом.
	//   ★ Волна 1-А (06.10.2026): защёлка «соулмейт» снята — гейты 5200 эталона мертвы (`AsdaSoulmateSubsystem.h`, R21),
	//   а сам пункт 2002 теперь идёт через общую точку `UAsdaSoulmateSubsystem::SendProposal5200`.
	bool   bCharInfoRequestActive = false;     // запрос 5478 в полёте
	double CharInfoRequestStartTime = 0.0;     // время отправки 5478 (для таймаута)

	// ── следование (2062) ──
	bool    bFollowActive = false;
	int32   FollowUserNum = -1;
	int32   FollowCharId  = 0;
	FString FollowName;            // имя ведущего — слот [ss1] нотисов 20311/20310
	double  FollowLastOrderTime = 0.0;
	bool    bFollowMoving = false;

	// счётчики для диагностики
	int32 CntOpened = 0, CntSent = 0;
	// ★ 27.09.2026: след последнего нотиса (пишет `Notice`, константный метод — поэтому mutable).
	mutable int32   NoticeSeq = 0;        // сколько нотисов напечатано
	mutable int32   LastNoticeLoc = 0;    // loc-id последнего
	mutable FString LastNoticeSlot;       // слот имени последнего
};
