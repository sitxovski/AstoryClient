// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPartySubsystem — ПРИГЛАШЕНИЕ В ОТРЯД: окна `PARTYTAKE_WIN` (JUI **2250**) и
//   `PARTYBOX_WIN` (JUI **2245**). Класс-владелец в эталоне — `CParty` (`g_pPartyWnd`),
//   привязка контролов — `social.c:1983-2050`. Полный разбор со ссылками `файл:строка`/`@0x…`
//   лежит в `Reports/party_invite_2250.md`. Ни одно число здесь не выдумано.
//
// ═══════════════════ 1. СОСТАВ ОКОН ПО ДАННЫМ (`Data/Decoded/jui/gamemain_jui.ini`) ══════════════════
//
//   `PARTYTAKE_WIN` **2250** — «пригласить в отряд», скин `Popup_Win`, Size 240×145,
//   Visible="0", AlwaysTop="1", якорь «16 50 50 / 32 50 50» (центр экрана). ★ РУЧКИ ПЕРЕТАСКИВАНИЯ
//   У ОКНА НЕТ: блока `EVS{EVT 6 → RET 20003}` в его записи .JUI НЕТ (у соседа 2245 — есть).
//     idnum  idc                     скин                     что это
//     2251   PARTYTAKE_TXT           NoImage                  текст запроса (loc 20020)
//     2254   PARTYTAKE_CHOICE        Bk_ComboBox              список правил дележа (loc 12153/12154)
//     2255   PARTYTAKE_DIS           NoImage                  текст текущего правила (loc 1167/1168)
//     2252   PARTYTAKE_YES           WkBtn_W72                «ОК»     — подпись loc 2288
//     2253   PARTYTAKE_NO            RdBtn_W71                «Отмена» — подпись loc 2378
//     —      PartyTake_YesBase/…     Btn_CBase/L/R, иконки    подложки кнопок (IDNum 4294967295)
//     —      PartyTake_ChoiceBase    Box_White1Stroke_Ot15    рамка списка (IDNum 2000000002)
//
//   `PARTYBOX_WIN` **2245** — «тебя приглашают», тот же скин и размер, ★ ЕСТЬ ручка
//   `EVS{EVT="6" RET="20003" CtrlID="2245"}` ⇒ окно перетаскиваемое (закон `ui-window-drag-hittest`).
//     2246   PARTYBOX_TXT            NoImage                  текст запроса (loc 20021/20022)
//     2247   PARTYBOX_TIME           White_Bar (ProgressBar)   шкала-таймер; ES0 подложка, ES1 заливка
//     2248   PARTYBOX_YES            WkBtn_W72                «ОК»     — подпись loc 2288
//     2249   PARTYBOX_NO             RdBtn_W71                «Отмена» — подпись loc 2378
//
//   Соответствие «контрол → поле `CParty`» доказано порядком привязки (`social.c:1983-2050`):
//     +0x0124 m_pCreateWin(2250) · +0x0128 m_pCreateText(2251) · +0x012C m_pCreateCombo(2254) ·
//     +0x0130 m_pCreateItemGetText(2255) · +0x0134 m_pCreateOkBtn(2252) · +0x0138 m_pCreateCancelBtn(2253) ·
//     +0x013C m_pJoinWin(2245) · +0x0140 m_pJoinText(2246) · +0x0144 m_pJoinTimer(2247) ·
//     +0x0148 m_pJoinOkBtn(2248) · +0x014C m_pJoinCancelBtn(2249).
//   Индексы `v8[73…76]` в `partyUi_ShowInviteRequest` — это ровно 0x124/4=73 … 0x130/4=76.
//
// ═══════════════════ 2. ПОЛНЫЙ ПОТОК ═══════════════════════════════════════════════════════════
//
//   ПКМ по игроку → пункт **2001** меню `TRANTOOL_WIN` → `exchangeUiOnCommand` case **0x7D1**
//   (`shop_trade.c:6549-6605`) → ЛОКАЛЬНЫЕ гейты → `partyUi_ShowInviteRequest@0xB99630`
//   (`social.c:2751`) открывает **2250**. ПАКЕТА НА ЭТОМ ШАГЕ НЕТ.
//
//   Кнопка **2252** «ОК» (`social.c:2295-2343`):
//     • я НЕ в отряде → `Party_Send_NewPartyReq(userNum, правило)` — опкод **5090**;
//     • я в отряде и ГЛАВА → `Party_Send_JoinReq(userNum)` — опкод **5094**;
//     • я в отряде и НЕ глава → нотис **1089**, пакет не уходит.
//   Кнопка **2253** «Отмена» → снять флаг «моя заявка в полёте» и спрятать окно.
//
//   У ПРИГЛАШЁННОГО приходит **5091** (ответ на 5090) или **5095** (ответ на 5094).
//   ★★★ ОКНО 2245 ПРИ ЭТОМ НЕ ОТКРЫВАЕТСЯ САМО. `Recv_PartyMember@0xDF3480` (`network.c:27285`)
//   кладёт заявку в слот УВЕДОМЛЕНИЙ ХОТБАРА (`statusBarAddIcon`, тип **0** для 5091 и **8** для
//   5095), играет звук **215** и печатает нотис **5057**. Окно 2245 показывает
//   `partyShowInviteRequest@0xB99970` — и зовёт его только `uiNoticeButtonDispatch@0xDF4F10`
//   (`ui_core.c:69392/69426`), то есть КЛИК ПО ИКОНКЕ УВЕДОМЛЕНИЯ.
//
//   Кнопки 2245 (`social.c:2241-2294`): **2248** «ОК» → 5092 (accept=1) либо 5096 (accept=1);
//   **2249** «Отмена» → те же опкоды с accept=0. Ветку выбирает пара флагов состояния
//   (см. §4). Тик `partyInviteTimeoutTick@0xB9B070` (`social.c:3108`) двигает шкалу 2247 и по
//   **30 000 мс** (0x7530) шлёт автоотказ — точно тот же пакет с accept=0.
//
//   Ответ на решение приглашённого: **5093** (новый отряд) или **5097** (вступление).
//
// ═══════════════════ 3. СЕТЬ: ПОБАЙТОВАЯ РАСКЛАДКА ════════════════════════════════════════════
//
//   ★★★ ШАПКА У ВСЕЙ ЭТОЙ СЕМЬИ — **БЕЗ 8-БАЙТНОГО АНТИ-РЕПЛЕЙ СЕКРЕТА**: тело =
//   `[0..15] MD5(payload)` + `[16..] payload`. Доказательство ДВУСТОРОННЕЕ:
//     • клиент `Party_Send_NewPartyReq@0xF38780` (`social.c:9315`): `memcpy(buf, md5, 16);
//       memcpy(buf+16, payload, 3); len = 19`;
//     • сервер `sub_523F00@0x523F00` (`GameServer.c:110821`) при `a4 == 0` считает MD5 от
//       `pkt+25` и сравнивает с `pkt+9` ⇒ смещение payload − смещение MD5 = **16**.
//       Ветка `a4 == 1` (MD5 @+17, payload @+33) — это как раз конверт `SendSecured` с секретом,
//       и party-обработчики её НЕ используют.
//   У всех пяти отправителей `CNetwork_Send(…, len, OP, 1, 1, 1, 0, 0)` ⇒ **a6 = 1, ПАДДИНГ ЕСТЬ**
//   (закон о `sub_5218A0`: без него сервер молча отбивает пакет по длине).
//
//   ★★★ `charId` (i32) ≠ `userNum` (i16). Во ВСЕХ четырёх исходящих пакетах отряда идёт
//   ТОЛЬКО **userNum (i16)**. Доказано сервером: `sub_44F4C0` кладёт прочитанное поле в
//   `sub_5324A0(userMng, v8[0])` — это выборка `CUser` по userNum. charId сервер берёт из своей
//   же `CUser` и присылает обратно в теле 5091/5095/5097.
//
//   C→S **5090** `OP_PARTY_SEND_NEW_PARTY_REQ`, len **19** (16 + 3):
//       @0 u16 userNum цели · @2 u8 правило дележа (0 = «кто успел», 1 = «автораспределение»)
//   C→S **5092** `OP_RESPONSE_USER_NEW_PARTY_CREATE`, len **19** (16 + 3):
//       @0 u8 код ответа · @1 i16 partyIndex (он же reqId из тела 5091)
//       коды: 1 = принять, 0 = отказ/таймаут, 5 = «занят другим запросом», 6 = «уже приглашён»
//   C→S **5094** `OP_PARTY_SEND_JOIN_REQ`, len **18** (16 + 2):
//       @0 u16 userNum цели
//   C→S **5096** `OP_PARTY_SEND_INVITE_ACCEPT`, len **19** (16 + 3):
//       @0 u8 код ответа · @1 i16 partyIndex   (коды 1/0, плюс 6 и 7 — «занят»)
//
//   S→C **5091** (misname в enum: `OP_MAP_DECO_RELEASE_ALL_RESOURCES`), тело **30**:
//       @0  u8    код: 0 = ЗАЯВКА, 1…0xB = событие/отказ (таблица нотисов §5)
//       @1  i16   userNum приглашающего      ⎫ сервер: `Recv_RequestUser_NewPartyCreate@0x44F7C0`
//       @3  i32   charId приглашающего       ⎬ (`GameServer.c:31873`) собирает тело именно так:
//       @7  char[20] имя приглашающего       ⎭ v5@0, v6@1, v7@3, v8[21]@7, v9@28
//       @27 u8    правило дележа отряда
//       @28 i16   reqId (partyIndex)
//       ★ Клиент читает ровно эти же смещения: `gap_474[67]` имя, `[87]` правило, `[88]` i16
//         при базе `gap_474+60` (`ui_core.c:69392-69396`) ⇒ +7 / +27 / +28. Совпало побайтово.
//   S→C **5095** `OP_PARTY_MEMBER_5095`, тело **30** — РАСКЛАДКА ТА ЖЕ (сервер
//       `recvRequestUserInviteParty@0x4500C0` собирает её тем же кодом).
//   S→C **5093** `OP_GM_NEWPARTY_RESPONSEUSER_RES`, тело **15**
//       (`recvResponseUserNewPartyCreate@0x44F9D0`, `GameServer.c:32009-32014`):
//       @0 u8 код · @1 i16 userNum приглашающего · @3 i32 charId приглашающего ·
//       @7 i16 userNum приглашённого · @9 i32 charId приглашённого · @13 i16 partyIndex
//   S→C **5097** `OP_GM_INVITEPARTY_RESPONSE_RES`, тело **29**
//       (`Recv_GM_INVITEPARTY_RESPONSE_RES@0xF39710`, `network.c:38973`):
//       @0 u8 код · @1 i16 userNum · @3 i32 charId · @7 char[20] имя · @27 i16 partyIndex
//   S→C **5099** `OP_GM_PARTY_LEAVEUSER_RES`: @0 i32 charId ушедшего, @6 char[20] имя.
//   S→C **5101** `OP_GM_PARTY_BANISHUSER_RES`: @0 i32 charId изгнанного, @4 char[20] имя.
//   S→C **5102** `OP_GM_PARTY_PARTYMASTER_CHANGE`: @0 i32 charId нового главы.
//   S→C **5103** `OP_GM_PARTY_DESTROY`: @0 i16 partyIndex распущенного отряда.
//
//   ⚠ Ловушка «пустой слот = −1, при этом +4 = 0» (`helper_initInvalidIds@0x973D30`) к этим
//     пакетам НЕ ОТНОСИТСЯ: массивов идентификаторов в них нет. Проверено телами сборщиков —
//     ни один из них не заполняет тело значением −1, кроме `*((_WORD*)party+862) = -1` ПОСЛЕ
//     отправки (это поле окна, а не поле пакета).
//
// ═══════════════════ 4. ГЕЙТЫ И ФЛАГИ СОСТОЯНИЯ ═══════════════════════════════════════════════
//
//   Четыре байта в `CharManager` (тунки `sub_92xxxx` → геттеры/сеттеры `social.c:3099-3168`):
//     +2365664 `get/set_o2418E0` — МОЯ заявка в полёте  (таймаут **32 000 мс**, 0x7D00)
//     +2365665 `get/set_o2418E1` — ВХОДЯЩАЯ заявка висит (таймаут **30 000 мс**, 0x7530)
//     +2365666 `get/set_o2418E2` — входящая заявка типа «новый отряд» (пришла по 5091)
//     +2365667 `get/set_o2418E3` — входящая заявка типа «вступление»  (пришла по 5095)
//   Ветку кнопок 2248/2249 выбирают ИМЕННО +2365666 / +2365667 (`social.c:2241-2294`).
//
//   Гейты пункта 2001 в порядке эталона (`shop_trade.c:6549-6605`):
//     1. `g_nPartyUiMode != 0`                        → молча закрыть меню   ★ см. §6, дефект D1
//     2. цель не найдена в менеджере                  → молча закрыть меню
//     3. цель — я сам (`effectSlots[112].field_40`)   → нотис **1093**
//     4. цель уже в моём отряде (`Party_FindMemberByUserIdx`) → нотис **1047**
//     5. `get_o2418E0` (моя заявка в полёте)          → нотис **1056**
//     6. `get_o2418E1` (входящая заявка висит)        → нотис **1045**
//     7. я в отряде и НЕ глава                        → нотис **1089**
//   ⚠ Гейтов «отряд полон», «разные карты», «в бою» у КЛИЕНТА нет вовсе — их считает сервер и
//     возвращает кодом в теле 5091/5095 (§5). Своих гейтов не выдумываем (закон о ложном гейте
//     «Вступить в клан», обжигались 2026-08-28).
//
// ═══════════════════ 5. НОТИСЫ: «событие → id строки → канал» ═════════════════════════════════
//   Канал у всех — `EAsdaMsgCategory::Social` (12 → системный журнал), кроме помеченных.
//   Строки со слотом `[ss1]` заполняются через `UAsdaTextSubsystem::FormatById`; строки,
//   помеченные «+имя», в эталоне склеиваются `sprintf("%s%s", имя, текст)` — слота там НЕТ,
//   имя приписывается ПЕРЕД строкой (воспроизводим склейку, а не подстановку).
//
//     событие                                     loc     где в эталоне
//     ──────────────────────────────────────────  ─────   ─────────────────────────────────────
//     открыт диалог 2250 «Пригласить [ss1]?»      20020   partyUi_ShowInviteRequest
//     правило дележа «кто успел»                   1167   то же, в 2255
//     правило дележа «автораспределение»           1168   то же, в 2255
//     пункты списка 2254                     12153/12154  CParty_BindControls
//     подписи кнопок 2252/2248 и 2253/2249     2288/2378  то же
//     2245 «[ss1] приглашает… (кто успел)»        20021   partyShowInviteRequest, a4 == 0
//     2245 «[ss1] приглашает… (авто)»             20022   то же, a4 != 0
//     цель — я сам                                 1093   гейт 3
//     цель/я уже в отряде                          1047   гейт 4, и коды 8/4 ответов
//     моя заявка уже в полёте                      1056   гейт 5
//     входящая заявка висит                        1045   гейт 6, и код 6 в 5091
//     я не глава отряда                            1089   гейт 7 и ветка 2252
//     пришла заявка (звук 215 + иконка)            5057   Recv_GM_*_REQUESTUSER_RES
//     5091 код 1  «Память переполнена»             1040   Party_DispatchEventNotice
//     5091 код 2/0xA «уже в отряде»       +имя     1041   то же
//     5091 код 3  «уже вне игры»          +имя     1042   то же
//     5091 код 4  «нельзя нескольких»              1043   то же
//     5091 код 5  «уже приглашаешь»                1044   то же
//     5091 код 7  «получено приглашение»           1046   то же
//     5091 код 9  «приглашение отклонено» +имя     1091   то же
//     5091 код 0xB «другая армия»                  1112   то же, ★ КРАСНЫЙ (−65536), канал 10000
//     5095 коды 1…8                     1052…1059   Notice_ShowResultByCode
//     5095 код 0xA                        +имя     1094   то же
//     5093 код 0/1/2/3                  1048…1051   Recv_GM_NEWPARTY_RESPONSEUSER_RES
//     5093 код 5/6                       1110/1111  то же
//     5097 код 0 «отказ»                  +имя     1084   Recv_GM_INVITEPARTY_RESPONSE_RES
//     5097 код 1 «принял»                 +имя     1085   то же
//     5099 ушёл я / другой                1086/1060 Recv_GM_PARTY_LEAVEUSER_RES
//     5101 изгнан я / другой              1064/1065 Recv_GM_PARTY_BANISHUSER_RES
//     5102 глава — я / другой             1061/1062 Recv_GM_PARTY_PARTYMASTER_CHANGE
//     5103 отряд распущен                  1063     Recv_GM_PARTY_DESTROY
//
// ═══════════════════ 6. ДЕФЕКТЫ ОРИГИНАЛА (каждый с ключом отката) ════════════════════════════
//
//   ★★★ D1 — КНОПКА «ПРИГЛАСИТЬ В ОТРЯД» В КОНТЕКСТНОМ МЕНЮ НЕ РАБОТАЕТ (окно открывалось
//     только с эмоций/горячей клавиши). ⚠ Формулировка уточнена 2026-08-29 после перепроверки.
//     Первая строка ветки 0x7D1 — `if (g_nPartyUiMode) goto LABEL_31;` (`shop_trade.c:6549`).
//     Это развилка ИСТОЧНИКА ЦЕЛИ, и у неё ДВА входа:
//       • ЭМОЦИИ/ГОРЯЧАЯ КЛАВИША → `UI_DispatchTargetContextMenu@0xC532D0`
//         (`ui_core.c:32108-32116`): `if (!g_nInputControlState) { g_nPartyUiMode = 0;
//         g_nPartyInviteTargetUid = g_nTargetUserIdx; exchangeUiOnCommand(…, 0x7D1, 0); }`
//         ⇒ режим обнуляется САМИМ вызывающим, окно открывается. НАСТОЯЩИЙ гейт этого входа —
//         `!g_nInputControlState` («ввод не занят»). Коды слотов: `uiDispatchTargetMenuByRange`
//         раскладывает 100…105 → категория 1 (это `IMOTION_PARTY00…05`), <100 → 0, 200…299 → 2.
//       • КНОПКА ОКНА 2000 → `exchangeWnd_OnMessage@0xC6F400`, сообщение **0x10401** →
//         `exchangeUiOnCommand(this, IDNum контрола, …)`. Режим НЕ трогается. Открыть меню можно
//         ТОЛЬКО правым кликом, а `MainWndProc@0x9841A0` на WM_RBUTTONDOWN (`case 0x204`,
//         `game_main.c:5830`) безусловно пишет `g_nPartyUiMode = 11` ⇒ к моменту клика по пункту
//         режим всегда 11, и ветка молча гаснет (`LABEL_31` → `uiDialog_ClearAndHide`).
//     ДОКАЗАНО ДВУМЯ МЕТОДАМИ:
//       (1) ПО ПОТОКУ. У `UI_DispatchTargetContextMenu@0xC532D0` во ВСЁМ декомпиле ровно ПЯТЬ
//           вызывающих: `EmoteWnd_WndProc@0xC55780`, `hotkeyDispatchAction@0xE3CD90`,
//           `input_HotkeyDispatchWithContextMenu@0xE3DAF0`, `uiDispatchTargetMenuByRange@0xC556C0`,
//           `uiOpenWindowDispatch@0xECD570` — все эмоции/клавиши, ни один не принадлежит окну 2000.
//           У `exchangeUiOnCommand@0xC6F7A0` вызывающих ДВА: тот же 0xC532D0 (литерал 0x7D1) и
//           `exchangeWnd_OnMessage` (msgId из сообщения) — второй и есть путь кнопки.
//       (2) СПЛОШНОЙ БАЙТОВЫЙ СКАН образа по адресу глобала **0x02902F64** — ровно ПЯТЬ ссылок:
//           0x9851CF `mov …,0Bh` (внутри `MainWndProc`) · 0xB980DB `cmp …,0` · 0xB980FB `mov …,0Bh`
//           (обе — кнопка 2252) · 0xC53DAD `mov …,0` (внутри 0xC532D0) · 0xC6F8BF `cmp …,0` (гейт).
//           Единственный обнуляющий сайт лежит в функции эмоций/клавиш.
//     ФИКС: порт делает то же, что рабочий вход, — заходит в ветку с нулевым режимом.
//     Откат: `asda.Party.FixContextMenuInvite 0`.
//     ⚠ Гейт `!g_nInputControlState` относится к входу «эмоции/клавиша», а не к кнопке меню;
//       воспроизводить его на пути кнопки было бы выдуманным гейтом. Он войдёт вместе с
//       лотком эмоций отряда (реестр отложенного, П4).
//
//   ★★★ D2 — ЦЕЛЬ РЕЖИМА 11 НИКОГДА НЕ ИНИЦИАЛИЗИРУЕТСЯ. ✅ ЗАКРЫТ КАК ПОГЛОЩЁННЫЙ D1.
//     Поле `*((_WORD*)party + 862)` (party+1724) читает кнопка 2252 в ветке
//     `g_nPartyUiMode != 0` (`social.c:2302/2314`), но во ВСЁМ декомпиле у него ТРИ
//     записи, и все три — значением −1 (`social.c:2303`, `:2315` после отправки и сброс
//     окна в `AsdaStory_ida.c:429476`). Реального userNum туда не кладёт никто.
//     ★ ОТДЕЛЬНОГО ИСПРАВЛЕНИЯ И КЛЮЧА ОТКАТА У D2 НЕТ, и это не пропуск: порт не
//     воспроизводит режим вообще, а цель кладёт в `InviteTargetUserNum` при ОТКРЫТИИ
//     окна (`OpenInviteWindow`) — то есть ровно то, чего оригиналу и не хватало.
//     Кнопка 2252 читает это же поле, поэтому «цель = −1» у нас невозможна
//     по построению. Ветка эталона, которую мы воспроизводим, — `else` (цель из
//     `g_nPartyInviteTargetUid`); общий для обеих веток сброс цели в −1 после отправки
//     сохранён. Взведение режима в 11 намеренно не повторяем: второго источника цели
//     у нас нет, а в оригинале именно эта строка и делала поле окна вечно невалидным.
//     Откат D2 = откат D1 (`asda.Party.FixContextMenuInvite 0`): без открытия окна из
//     меню поле цели вообще не заполняется, как и в оригинале.
//
//   ★★ D3 — ЛИДЕР НЕ ВИДИТ ПРАВИЛО ДЕЛЕЖА. В ветке «я в отряде и глава»
//     (`social.c:2765-2778`) текст правила (loc 1167/1168) КЛАДЁТСЯ в 2255 и следующей же
//     строкой `CControl_SetVisible(v8[76], 0)` ГАСИТСЯ, а список 2254 тоже скрыт. Окно остаётся
//     голым «Пригласить X?».
//     ★ ПРОВЕРЕНО ПО ПОТОКУ (2026-08-29): у `v8[76]` = `m_pCreateItemGetText` (2255) во
//     ВСЁМ декомпиле ЧЕТЫРЕ обращения — привязка (`social.c:1997`), `SetText` (`:2773`)
//     и ДВА `SetVisible(…, 0)` (`:2778` ветка лидера, `:2786` ветка создания).
//     `SetVisible(…, 1)` нет НИ ОДНОГО ⇒ контрол не показывается никогда, ни на одном пути.
//     ФИКС: 2255 показываем только для чтения (ветка лидера).
//     Откат: `asda.Party.ShowLootRuleForLeader 0`.
//
//   ★★ D4 — МЁРТВОЕ ПОЛЕ (НЕ ДЕФЕКТ; переклассифицировано 2026-08-29).
//     `byte_2BE0540` НИКОГДА НЕ ЗАПИСЫВАЕТСЯ. Он идёт третьим
//     аргументом `partyUi_ShowInviteRequest` («правило моего отряда»). Сплошной байтовый скан
//     образа даёт 4 ссылки — 0xC6FACC, 0xC9EF94, 0xE736A6, 0xEBF3ED — и ВСЕ ЧЕТЫРЕ суть
//     `movzx eax, byte ptr [0x2BE0540]` (`0F B6 05`), то есть ЧТЕНИЯ. Записи нет ни одной,
//     и адрес нигде не берётся (`lea`/`push` его в образе не встречается — тот же скан).
//     ★ ВТОРОЙ МЕТОД (2026-08-29): скан ВСЕХ dword-литералов в окне 0x2BE0140…0x2BE0640
//     по секциям .text/.rdata/.data даёт ссылки только на 0x2BE0540 и ВЫШЕ (…544/54C/
//     550/554/558); ни одного ссылающегося адреса НИЖЕ 0x2BE0540 в килобайтовом окне нет
//     ⇒ записать этот байт как «база соседнего глобала + смещение» неоткуда.
//     ⇒ у лидера всегда показывается «кто успел», каким бы правилом отряд ни создавался.
//     ФИКС: порт запоминает правило, с которым отряд был создан/в который вступил (`LootRule`).
//     Откат: `asda.Party.TrackLootRule 0` (вернёт вечный 0, как в оригинале).
//
//   ★ D5 — 2245 НЕ ПОКАЗЫВАЕТСЯ САМО. ★ ПРОВЕРЕНО ПО ПОТОКУ (2026-08-29): окно 2245
//     показывает единственная функция `partyShowInviteRequest@0xB99970`, и во ВСЁМ
//     декомпиле её зовёт ровно один вызывающий — `uiNoticeButtonDispatch@0xDF4F10`
//     (4 сайта: ветки 0 и 8, по две на правило дележа). Ни приёмник пакета, ни таймер
//     её не зовут. Заявка попадает только в иконку хотбара; если все семь
//     слотов уведомлений заняты, приглашение видно НЕ БУДЕТ, а таймер 30 с всё равно тикает и
//     через полминуты пришлёт автоотказ. Воспроизводим 1:1 (иконка + звук + нотис 5057), но
//     добавлен ключ `asda.Party.AutoOpenJoinBox 1` — сразу показывать окно, минуя иконку.
//     По умолчанию **0** (как в оригинале).
//
//   ДЕФЕКТЫ СЕРВЕРА (патчи заведены в `AstoryServer/Docs/`, живой сервер только на чтение):
//     S1 `sub_44F4C0` (5090) не проверяет, что цель ≠ отправитель — самоприглашение блокирует
//        только клиент (нотис 1093).
//     S2 `sub_44F4C0`/`sub_44FDE0` не проверяют, что отправитель не в отряде / является главой —
//        обе проверки живут только в клиенте (1047/1089).
//     S3 `recvResponseUserNewPartyCreate` (`GameServer.c:32027-32031`): в ветке зоны типа 4 имя
//        для второго `sub_467AD0` берётся `sub_532570(userMng, partyBody[0])` — тот же
//        приглашающий, хотя нужен `partyBody[3]` (приглашённый). Копипаста.
//     S4 `sub_44FDE0` (5094) шлёт `*(WORD*)(pUser+436)` как partyIndex, не убедившись, что
//        отправитель вообще в отряде.
//     S5 ★ `sub_523F00@0x523F00` (`GameServer.c:110838-110846`) СЧИТАЕТ MD5 и вызывает `memcmp`,
//        но РЕЗУЛЬТАТ НЕ ПРОВЕРЯЕТ — функция безусловно `return 1`. ⇒ подпись тела не проверяется
//        ни у одного пакета, идущего через неё (вся семья отряда в том числе).
//
// ═══════════════════ 7. НУМПАД 1410 — НЕ УЧАСТВУЕТ ═══════════════════════════════════════════
//   Доказательство (закон `check-numpad-on-new-system`, сверено с `Docs/Port/systems/NUMPAD_FLOW.md`):
//   в `RE/etalon/social.c` — файле, где живёт ВЕСЬ класс `CParty` вместе с обработчиками
//   2245…2263 — вхождений `g_pNumPadDialog` и `uiOpenContextPopupWithHandlers` **ноль**
//   (`grep -c` = 0). Количества в этой цепочке не выбираются вообще.
//
// ═══════════════════ 8. ЧТО ЗДЕСЬ ЕСТЬ, А ЧТО ЖДЁТ СИСТЕМ ════════════════════════════════════
//   ЕСТЬ: оба окна, все гейты, оба исходящих запроса (5090/5094), оба ответа (5092/5096),
//     приём 5091/5093/5095/5097/5099/5101/5102/5103, таймауты 30 с и 32 с, шкала 2247,
//     флаги состояния, нотисы по таблице §5, подвязка к пункту 2001 меню игрока;
//     панель `PARTY_WIN` 2200 (5107/5108/5109, кнопки 2260/2262 → 5098/5100, меню строки 8100);
//     ★ 06.10.2026 (волна 1-Б): окно роспуска `PARTYDISPERSION` 2256 (§9) и позиции соотрядников
//     на мини-карте — приём 5104 + байты 5109 @19/@21/@23 (§10).
//   ЖДЁТ (записано в `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`): чат отряда 5105/5106
//     и «второй лоток эмоций» (`IMOTION_PARTY00…05`) — второй путь к этому же окну 2250.
//
// ═══════════════════ 9. ★ ОКНО РОСПУСКА `PARTYDISPERSION` 2256 (волна 1-Б, 06.10.2026) ════════
//   Отчёт: `Reports/owner_1006/wave1B_party_roulette.md` §1.
//   СОСТАВ (`gamemain_jui.ini:42512-42760`, дамп ассета `Reports/_party_win_dump.txt:562`):
//     2256 окно 240×145 по центру экрана, ★ ЕСТЬ ручка `EVS{EVT 6 → RET 20003}` (перетаскивается);
//     2257 `PARTYDISPERSION_MESSAGE` (200×65) — текст; 2258 `PARTYDISPERSION_OK` «ОК» (loc 2288);
//     2259 `PARTYDISPERSION_CANCEL` «Отмена» (loc 2378). Привязка — `social.c:2048-2069`
//     (`CParty+0x150…+0x15C`), подписи кнопок ставятся при привязке.
//   ПОКАЗ — функция `0xB99C30` (thunk `0x92AE36`). ★ IDA её НЕ определила (в `functions.tsv` есть
//     только соседи `sub_B99AA0`/`sub_B99CF0`), поэтому прошлые аудиты (13_party_clan.md) её не
//     видели и писали «писателя видимости 1 нет». Тело (дизасм `RE/disasm_at.py B99C30`):
//       if (Party_IsMeBoss(g_CharManager) == 1) {                 // call j_Party_IsMeBoss
//         sprintf(buf, "%s", loc 1170);                           // push 0x492
//         CControl_SetText(m_pDisbandText /*+0x154*/, buf);
//         CControl_SetVisible(m_pDisbandWin /*+0x150*/, 1); }
//     loc 1170 = «Ты действительно хочешь разделить отряд?».
//   ★★★ ВЫЗЫВАЮЩИХ У НЕЁ НЕТ (доказано ТРЕМЯ методами, `wave1B_party_roulette.md` §1.2):
//     (1) декомпил: функции нет, ссылок на 2256 — только привязка; (2) сплошной скан образа
//     (E8/E9 rel32 + абсолютные dword) по 0xB99C30 и её thunk 0x92AE36 — ссылка ОДНА (сам thunk),
//     положительный контроль на том же прогоне — `j_partyShowInviteRequest` даёт свои 4 вызова;
//     (3) другие сборки клиента: Interlude ×3 (тот же адрес, ссылок 0), Arabic/XYT/WoS
//     (функция @0x5D6A50, ссылок 0). ⇒ В ОРИГИНАЛЕ ОКНО 2256 НЕДОСТИЖИМО (заготовка).
//   КНОПКИ (`CParty_OnControl`, `social.c:2351-2361`) — живые, обе шлют **5098**:
//     2258 «ОК»     → `if (Party_IsInParty) Party_Send_InviteByName(g_nPartyInviteTargetIdx, 1, моё имя)`;
//     2259 «Отмена» → то же с режимом **0**; обе затем прячут окно (`sub_B99CF0`).
//   СМЫСЛ РЕЖИМА — СЕРВЕР ОТРЯДОВ (RelayServer, «Chat Manager» :15801, живой
//     `RelayServer.exe` @0x408930 `CPartyManager::PartyLeave`, лог-строка
//     «IsDestroy %d, LeaveUserIndex %d, PartyIndex %d, NickName %s»): байт @2 пакета 5098 = IsDestroy.
//     Ушёл НЕ глава → режим игнорируется, рядовой выход (3212 → 5099). Ушёл ГЛАВА (слот 0):
//     IsDestroy == 1 → роспуск всего отряда (3216 → 5103 «Отряд распался»); IsDestroy == 0 → глава
//     уходит, главой становится следующий (3215 → 5102). Участников < 3 → роспуск в любом случае.
//   ⇒ Окно — развилка «глава выходит»: «ОК» = распустить отряд, «Отмена» = просто выйти (главенство
//     перейдёт). Именно так его и подключаем: [ОРИГИНАЛ] заготовка оживлена, вход — кнопка 2260
//     «Выход» у ГЛАВЫ (гейт `Party_IsMeBoss` взят из самой функции показа). Рядовой жмёт 2260 —
//     5098 режим 0 сразу, как в оригинале. Ключ отката `asda.Party.FixDisbandConfirm 0` (у главы
//     2260 снова шлёт режим 0 без окна, окно 2256 не показывается ничем — как в оригинале).
//   ⚠ «Отмена» = ВЫХОД (режим 0), а не «передумал» — это поведение обработчика оригинала 1:1.
//   НУМПАД: нет (`social.c` — 0 вхождений `g_pNumPadDialog`). НОТИСОВ у окна нет: ни показ, ни
//     кнопки строк не печатают; ответы — штатные 5099 (1086/1060), 5102 (1061/1062), 5103 (1063).
//
// ═══════════════════ 10. ★ ПОЗИЦИИ СООТРЯДНИКОВ НА МИНИ-КАРТЕ (волна 1-Б) ════════════════════
//   S→C **5104** `Recv_GM_PARTY_POSITION@0xF3A1F0` (`network.c`, диспетчер `game.c:15475` case
//     0x13F0), тело 12 б (сервер `sub_451A90@GameServer.c:32884`): @0 i16 partyIndex · @2 i32 charId ·
//     @6 i16 userNum (клиент не читает) · @8 i16 тайл X · @10 i16 тайл Z. Клиент:
//     `if (partyIdx == Party_GetMyPartyIdx) party_SetMemberFieldsById(charId, X, Z)` → запись
//     участника +64/+68. Шлёт сервер при движении участника ТОЛЬКО соотрядникам своего сервера карты.
//   5109 (`Recv_GM_PARTY_UPDATE_PARAM@0xB9ACA0`) кладёт ТЕ ЖЕ поля: @19 → +64, @21 → +68,
//     @6 → +96, @23 → +276 (id карты) и +272 = (id карты == моя карта).
//   ОТРИСОВКА — `minimap_DrawPartyMarkers@0xB55A00` (`world_map.c:3080`): только в отряде; текстура
//     `miniPartyPos` (CMap+512), квад 16×16 по центру точки; пропуск себя (`member+4 == charId`);
//     фильтр карты: мини-карта (режимы 2/3) — `+276 == g_nCurMapId`, большая (режим 1) —
//     `+276 == показанная карта`. Порт кладёт отметки в ОБЩИЙ список мини-карты
//     (`UAsdaMinimapSubsystem::SetMarks(EAsdaMapMark::Party, …)`), рендер там уже был полный.
//   ★ ИСПРАВЛЕНИЕ [ОРИГИНАЛ]: конструктор записи (`sub_9CC8A0`) ставит +64/+68/+276 = 0, и до первого
//     5109/5104 оригинал рисует соотрядника в тайле (0,0) карты 0. Порт до прихода координат и карты
//     отметку НЕ рисует (ключ `asda.Party.MapMarkNeedPos 0` — как оригинал).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaBarSmoother.h"        // ★ ОБЩАЯ точка сглаживания полос HP/MP (asda.UI.BarSmoothSec)
#include "UI/AsdaMsgCategory.h"   // ★ R1: EAsdaMsgCategory — категория события (без класса подсистемы сообщений)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaHelpTopics.h"         // FAsdaHelpTipHover — справка кнопок и иконок состояний панели 2200
#include "AsdaPartySubsystem.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UWidget;   // ★ R1: раньше объявление приходило из UI/AsdaUiHelpers.h
class UUserWidget;
class UAsdaButtonRelay;
class UAsdaJuiComboBox;

/** Один участник отряда, известный порту (эталонная запись CharManager, список `this+591356`).
 *  Полный состав приходит пакетом 5107; до него копятся те, о ком сказали 5093/5097 (создание
 *  отряда и вступление). Параметры — 5109, состояния — 5108, координаты на карте — 5104/5109. */
USTRUCT(BlueprintType)
struct FAsdaPartyMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   UserNum = -1;  // i16, ключ CharManager
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   CharId  = 0;   // i32, ключ БД (запись +4)
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") FString Name;           // запись +12, 20 б
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") bool    bLeader = false;// запись +72 == 1
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   Hp = 0;         // запись +76
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   MaxHp = 1;      // запись +80
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   Mp = 0;         // запись +84
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   MaxMp = 1;      // запись +88
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   Level = 0;      // запись +94 (u8)
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   ClassCode = 0;  // запись +95 (u8)
	/** 28 слотов состояний из 5108 (запись +104, по 6 б): вид (1 = навык, 3 = соулмейт) и id. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") TArray<uint8> StateKind;
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") TArray<int32> StateId;
	/** Слот панели, в который участник попал последним расчётом (`запись +100`). −1 = не показан. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   SlotIndex = -1;
	/** ★ Тайл X на карте (`запись +64`): 5104 @8 / 5109 @19. Читает `minimap_DrawPartyMarkers`. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   TileX = 0;
	/** ★ Тайл Z на карте (`запись +68`): 5104 @10 / 5109 @21. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   TileZ = 0;
	/** ★ Пришли ли координаты (5104 или 5109). Эталон такого признака не держит (§10, исправление). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") bool    bPosKnown = false;
	/** ★ Id карты участника (`запись +276`): 5109 @23. −1 = ещё не приходил (эталон: 0). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   MapId = -1;
	/** ★ «Участник на моей карте» (`запись +272`): 5109 и смена карты (`objRefreshOwnershipFlags`). */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") bool    bOnMyMap = true;
	/** ★ Байт 5109 @6 (`запись +96`). Читателей в разобранном срезе нет — храним, не толкуем. */
	UPROPERTY(BlueprintReadOnly, Category = "Asda|Party") int32   Param96 = 0;
};

UCLASS()
class ASDAREBORN_API UAsdaPartySubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ── UGameInstanceSubsystem ──
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Состав отряда и ПОКАЗАННАЯ панель 2200 переживали перезаход: панель оставалась
	// на экране с участниками прошлой сессии. Плюс два таймера заявок.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── FTickableGameObject ──
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPartySubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual bool IsTickableInEditor() const override { return false; }

	// ═══ ВХОД ИЗ КОНТЕКСТНОГО МЕНЮ (пункт 2001) ═══
	/** Ветка `exchangeUiOnCommand` case 0x7D1 целиком: все гейты + показ окна 2250.
	 *  Возвращает true, если окно реально открылось. Любой отказ печатает нотис И пишет причину
	 *  в лог (молчаливых выходов быть не должно).
	 *  `bSkipPresenceGate` — ТОЛЬКО для консольной диагностики без живого сервера: пропускает
	 *  гейт 2 «цель есть в реестре соседей». Из игры всегда false. */
	//  ★ 25.10.2026 `bFromHotkey` — вход «эмоции/горячая клавиша» (`UI_DispatchTargetContextMenu` кат. 1/0): вызывающий сам
	//    пишет `g_nPartyUiMode = 0`, поэтому гейт 1 (дефект D1 кнопки меню, ключ `asda.Party.FixContextMenuInvite`) его не касается.
	bool OpenInviteWindow(int32 TargetUserNum, const FString& TargetName, bool bSkipPresenceGate = false, bool bFromHotkey = false);

	// ═══ ОКНО 2250 ═══
	bool IsInviteWindowOpen() const { return bCreateOpen; }
	void CloseInviteWindow();                     // ветка 2253 без снятия флага
	void OnCreateOk();                            // кнопка 2252
	void OnCreateCancel();                        // кнопка 2253
	/** Индекс правила дележа в списке 2254: 0 = «кто успел» (12153), 1 = «автораспределение» (12154). */
	int32 GetLootRuleChoice() const;
	void  SetLootRuleChoice(int32 Idx);

	// ═══ ОКНО 2245 ═══
	bool IsJoinBoxOpen() const { return bJoinOpen; }
	/** `partyShowInviteRequest@0xB99970`: показать окно по ВИСЯЩЕЙ входящей заявке.
	 *  Возвращает false, если заявки нет (эталон: `if (a4 == -1) return 0`). */
	bool ShowJoinBox();
	void OnJoinOk();                              // кнопка 2248
	void OnJoinCancel();                          // кнопка 2249
	/** ★ 22.10.2026: порт `CPartyInviteDlg_OnAccept@0xB9B360` (social.c:3182) — шаг 0 загрузки карты (game_main.c:3544):
	 *  висящая входящая заявка получает автоотказ кодом 0 (5092/5096), значок уведомления и окно 2245 снимаются. Зовётся,
	 *  пока соединение прежней карты ещё живо (`AsdaEntryWindows::ApplyMapLoadWindowCloses`). Нет заявки — ничего. */
	void DeclineIncomingOnMapLoad(const TCHAR* Why);
	// ★ 06.10.2026 (волна 2-В): ОКНО 2245 ОБЩЕЕ С ДУЭЛЬЮ. В эталоне `CDuelMgr` держит ТЕ ЖЕ контролы
	//   (`CPartyJoinWnd_InitControls@0xDE3310`: 2245/2246/2247/2248/2249), а `CParty_OnControl@0xB980A0`
	//   (ui_core 2248/2249) после веток отряда зовёт ветку дуэли `if (CDuelMgr::m_nTargetSessionId >= 0)
	//   { duelSendReqPkt4303(…, 1|0); duelUiPanelReset(); }`. Поэтому кнопки рассылаются второму владельцу.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaJoinBoxButton, bool /*bOk: 2248 = true, 2249 = false*/);
	FOnAsdaJoinBoxButton OnJoinBoxButton;          // ★ кнопки 2248/2249 — ПОСЛЕ веток отряда (порядок эталона)
	/** `duelUiPanelShow@0xDE4190`: текст 2246 и показ 2245 для второго владельца окна (дуэль). */
	bool ShowJoinBoxText(const FString& Text);
	/** `duelUiPanelReset@0xDE41E0`: `CControl_SetVisible(m_pDuelWnd, 0)` — окно общее, гаснет целиком. */
	void HideJoinBoxShared();
	/** Шкала 2247 для второго владельца (`CProgressBar_SetValue(m_pTimerBar, accum)`, диапазон 0…30000). */
	void SetJoinBoxTimerFracShared(float Frac) { SetJoinTimerFrac(Frac); }
	/** Текст 2246 сейчас (для самотеста дуэли). */
	FString GetJoinBoxText() const;

	// ═══ СОСТОЯНИЕ ОТРЯДА ═══
	bool  IsInParty()  const { return bInParty; }
	bool  IsPartyLeader() const { return bInParty && bIsLeader; }
	int32 GetPartyIndex() const { return PartyIndex; }
	/** Правило дележа МОЕГО отряда. Зеркало `byte_2BE0540`, которое оригинал не пишет (§6 D4). */
	int32 GetPartyLootRule() const;
	bool  IsMemberUserNum(int32 UserNum) const;
	// ★ 30.10.2026 (боевая фаза «Битвы армий»): член отряда по charId (`Party_FindMemberByUserIdx(charId)`, имя врёт).
	bool  IsMemberCharId(int32 CharId) const;
	// ★ 30.10.2026: защёлки CharManager для гейтов окна «Рейд» 49410 — `get_o2418E0` (моя заявка в полёте) и
	//   `get_o2418E1` (висит входящая заявка).
	bool  IsOwnRequestActive() const { return bMyRequestActive; }
	bool  IsIncomingPending() const { return bIncomingPending; }
	// ★ 30.10.2026: `partyUi_ShowInviteRequest@0xB99630` без гейтов контекстного меню (их делает вызывающий — окно «Рейд»
	//   49410 у «Битвы армий»); тот же показ окна 2250, что в конце `OpenInviteWindow`. true — окно открыто.
	bool  ShowInviteRequestFor(int32 TargetUserNum, const FString& TargetName);
	const TArray<FAsdaPartyMember>& GetMembers() const { return Members; }

	// ★ 06.10.2026 (портрет): 5102 разобран В ПОРЯДКЕ очереди пакетов (после 5107 того же кадра) —
	//   эталон `Party_SetMasterLocal@0x9CCC30` (social.c:419-455): bFound = новый глава есть в составе,
	//   bMe = это я. Слушает `UAsdaMiniStateSubsystem` (значок главы MINISTATE_Partyer 9).
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaPartyMasterChanged, int32 /*charId*/, bool /*bFound*/, bool /*bMe*/);
	FOnAsdaPartyMasterChanged OnPartyMasterChanged;

	// ═══ ОКНО СПИСКА УЧАСТНИКОВ `PARTY_WIN` 2200 (панель отряда в HUD) ═══
	//   Состав по данным (`gamemain_jui.ini:42772-50380`): шесть строк `PARTY_WIN00…05`
	//   (2201…2206), в каждой — иконки класса `PARTY_SWORD/BOW/STAFF{NN}` (11001…11018),
	//   уровень `PARTY_LEVEL{NN}` (11020…11025), полосы `PARTY_HP{NN}` (2211/2216/2221/2226/
	//   2231/2236) и `PARTY_MP{NN}` (2212/2217/…), имя `PARTY_NAME{NN}` (2210/2215/…),
	//   28 иконок состояний `PARTY_STATE_ICON{N}{ii}` (33100…33127, 33200…33227, …),
	//   подсветка выбора `Party_MemSelect{NN}` (2282…2287), зона клика `Party_MemClick{NN}`
	//   (2294…2299) и значок главы `PARTY_ICON{NN}` (2209/2214/…).
	//   Сверху — `PARTY_TOOLBUT00` (2262 «Выгнать», loc 12001) и `PARTY_EXITBUT` (2260 «Выход»,
	//   loc 12002), шестерёнка `Party_Gear` (2280) и свёрнутый заголовок `Party_WinH_grp`
	//   с кнопкой `PARTY_MINIBUT_H`.
	/** Показать/спрятать панель. Открывается сама, как только в отряде появился хоть кто-то. */
	void SetRosterVisible(bool bVisible);
	bool IsRosterVisible() const { return bRosterOpen; }
	/** Показан свёрнутый вид панели (`Party_WinH_grp`). */
	bool IsRosterCollapsed() const { return bRosterCollapsed; }
	/** Сколько раз панель восстановлена после сноса окна (`HealRosterIfLost`) — для дампа и самотеста. */
	int32 GetRosterHealCount() const { return CntRosterHeal; }
	/** Полная перерисовка строк — порт `PartyWnd_UpdateMemberList@0xB99EA0` (`social.c:2851`). */
	void RefreshRoster();
	/** Порт `partyUpdateStateIcons@0xB97930`: 28 иконок состояний на каждого соотрядца. */
	void RefreshStateIcons();
	/** Выбранный в панели участник (эталонный `g_nPartyMemberMenuIdx`). −1 = никто. */
	int32 GetSelectedMemberSlot() const { return SelectedSlot; }
	void  SelectMemberSlot(int32 Slot);
	/** Кнопка 2260 «Выход». Рядовой — 5098 режим 0 по своему имени (эталон `social.c:2362-2368`).
	 *  ★ Глава при `asda.Party.FixDisbandConfirm 1` — окно роспуска 2256 (§9), пакет шлёт уже оно. */
	void OnLeaveClicked();

	// ═══ ★ ОКНО РОСПУСКА `PARTYDISPERSION` 2256 (§9) ═══
	/** Порт функции показа `0xB99C30`: только у ГЛАВЫ (`Party_IsMeBoss`) — текст loc 1170 в 2257 и
	 *  окно на экран. Возвращает true, если окно реально показано (не главе — false, молча, как эталон). */
	bool ShowDisbandConfirm();
	/** Окно 2256 сейчас на экране. */
	bool IsDisbandOpen() const { return bDisbandOpen; }
	/** Кнопка 2258 «ОК» (`social.c:2351-2355`): в отряде — 5098 режим 1 (IsDestroy), окно прячется. */
	void OnDisbandOk();
	/** Кнопка 2259 «Отмена» (`social.c:2356-2361`): в отряде — 5098 режим 0 (просто выход), окно прячется. */
	void OnDisbandCancel();
	/** Кнопка 2262 «Выгнать»: гейты 1089 (не глава) и 1090 (некого) → 5100. */
	void OnBanishClicked();
	/** Свернуть/развернуть панель (пара `Party_WinH_grp` / `Party_WinN_grp`). */
	void ToggleRosterCollapsed();

	// ═══ КЛИК ПО СТРОКЕ УЧАСТНИКА ═══
	/** ЛКМ по строке: выбрать участника СВОЕЙ целью (панель цели + запрос 6538) и обвести строку. */
	void ClickRow(int32 Slot);
	/** ПКМ по строке: меню `Follow_Another_win` 8100 у курсора. */
	void OpenRowMenu(int32 Slot);
	/** Пункт 8102 «Указать цель» — эталонный msgId 8102 (`exchangeUiOnCommand`). */
	void OnRowMenuMarkTarget();
	/** Пункт 8101 «Следовать». */
	void OnRowMenuFollow();
	/** Пункт 8103 «Закрыть». */
	void CloseRowMenu();
	/** Строка, помеченная «общая цель» (эталонный `g_nPartySelIdx`). −1 = никто. */
	int32 GetSharedTargetSlot() const { return SharedTargetSlot; }

	// ═══ ★ 25.10.2026 [ОРИГИНАЛ] ГОРЯЧИЕ КЛАВИШИ И ЭМОЦИИ ОТРЯДА (баг игроков «клавиши действий не работают») ═══
	/** `UI_DispatchTargetContextMenu@0xC532D0` категория 1 (ui_core.c:32105-32157): 0 «пригласить» (`!g_nInputControlState` →
	 *  окно 2250 цели), 1 «выйти» (в отряде → 5098 режим 0 своим именем, БЕЗ окна роспуска), 2 «выгнать» (не глава → 1089;
	 *  выбран участник панели → 5100, выбранного нет в составе → 1090, не выбран — молча), 3 (не глава → 1089).
	 *  Клавиши 48/49/50 и эмоции 100…103. `TargetUserNum`/`TargetName` — выбранная цель-персонаж (−1 — цели-персонажа нет). */
	bool DispatchPartyMenu(int32 Idx, int32 TargetUserNum, const FString& TargetName, const TCHAR* Why);
	/** Категория 2, случай 6 (ui_core.c:32208-32285, клавиша 63 «ACTION TARGET SHARE»): взять цель помеченного соотрядника
	 *  (`g_nPartySelIdx`): не в отряде 20306 · я мёртв 20302 · верхом 20305 · не помечен 20304 · его нет рядом 20308 ·
	 *  он мёртв 20301 · у него нет цели-моба 20300; иначе его моб — моя цель. Пакета нет. */
	bool TakeSharedTarget(const TCHAR* Why);
	/** `hotkeyDispatchAction` case 64 (input.c:4383-4402, клавиша «ACTION TARGET SHARE NEXT»): пометка общей цели —
	 *  следующий участник (`++g_nPartySelIdx`, круг по `count − 1` строкам), нотис 20307 с его именем; пустая строка — снять. */
	bool ShareTargetNext(const TCHAR* Why);
	/** `input_HandleTargetSelectClick@0xE3E090` (клавиша 62 «ACTION SELETE MYSELF», Shift+Tab): вне отряда — цель = я;
	 *  в отряде — цель = следующий видимый участник по строкам 0…5 (`g_nDlgStateMachine`), после шестой — снова я. */
	bool SelectSelfOrNextMember(const TCHAR* Why);
	/** Шаг круга Shift+Tab (`g_nDlgStateMachine`: 0…5 — следующая строка, −1 — после шестой). Самотест. */
	int32 GetSelectCycleState() const { return SelectCycleState; }
	// ★ 26.10.2026 (ревизия wave_1025d): самотест — круг Shift+Tab и пометка общей цели с начала (повторный прогон в той же игре
	//   начинал круг со строки 1, пометку — с прошлой строки; эталон снимает `g_nPartySelIdx` в `CParty_BuildUI@0xB95FF0`).
	void DebugResetTargetCycles() { SelectCycleState = 0; SharedTargetSlot = -1; }

	// ★ ЗАКОН «диагностика жмёт САМУ кнопку»: нажать кнопку её собственным `OnClicked`.
	void ClickByNodeName(const TCHAR* Node);

	/** Полный структурный дамп обоих окон и всего состояния (asda.Party.Dump). */
	FString DumpAll() const;

	/** Состояние строк панели 2200: кто в каком слоте, что показано (asda.Party.DumpRoster). */
	FString DumpRoster() const;

	/** ★ ДИАГНОСТИКА: положить ГОТОВОЕ тело пакета в ту же очередь, которую наполняет
	 *  `UAsdaNetSubsystem` (asda.Party.Inject). Разбирается тем же кодом, что и живой пакет —
	 *  то есть проверяется НАСТОЯЩИЙ приёмник, а не его копия. Нужна, чтобы прогнать ветку
	 *  «пришла заявка → окно 2245 → ответ» без второго живого игрока на сервере. */
	void InjectPacket(int32 Opcode, const TArray<uint8>& Body) { PendingIn.Push(TPair<int32, TArray<uint8>>(Opcode, Body)); }

	/** ★ ДИАГНОСТИКА (asda.Party.FakeState): выставить состояние отряда напрямую. Нужна ровно
	 *  для одного: прогнать ветку «я глава отряда» окна 2250 без второго живого игрока —
	 *  честный путь туда (5093/5102) требует, чтобы сервер прислал МОЙ charId, а без сессии
	 *  `SelectedCharId` равен нулю. В игровой код НЕ зовётся. */
	void DebugSetPartyState(bool bIn, bool bLeader, int32 Index, int32 Rule);

private:
	// ── виджеты ──
	bool EnsureCreateWidget();
	bool EnsureJoinWidget();
	void SetupCreateControls();
	void SetupJoinControls();
	void ResetWidgets();
	void SetCreateVisible(bool bVisible);
	void SetJoinVisible(bool bVisible);
	/** Заливка шкалы 2247: доля 0…1 от прошедшего времени (эталон `SetRange(0, 30000.0f)`). */
	void SetJoinTimerFrac(float Frac);

	// ── сеть ──
	void OnPartyRequest(int32 Opcode, const TArray<uint8>& Body);   // 5091 / 5095
	void OnPartyResult(int32 Opcode, const TArray<uint8>& Body);    // 5093 / 5097 / 5099 / 5101 / 5102 / 5103
	void ClearIncoming(const TCHAR* Why);
	void SendJoinAnswer(bool bAccept, uint8 Code, const TCHAR* Why);

	// ── нотисы ──
	FString Loc(int32 LocId) const;
	/** Нотис по id. `NameSlot` подставляется в маркер `[ss1]`; `NamePrefix` ПРИКЛЕИВАЕТСЯ
	 *  спереди — так эталон делает `sprintf("%s%s", имя, текст)` (слота там нет). */
	void Notice(int32 LocId, const FString& NameSlot = FString(),
	            const FString& NamePrefix = FString(), bool bWarnColor = false) const;
	static EAsdaMsgCategory CategoryForNotice(int32 LocId);

	// ── ассеты и id ──
	static constexpr int32 kCreateZ = 736;   // 2250
	static constexpr int32 kJoinZ   = 737;   // 2245 — выше 2250: приглашение обязано быть видно
	// ★ Панель отряда — часть HUD, а не диалог: она должна лежать НИЖЕ любых окон, но выше HUD-фона.
	static constexpr int32 kRosterZ  = 120;  // 2200 `PARTY_WIN`
	static constexpr int32 kDisbandZ = 738;  // 2256 `PARTYDISPERSION` — подтверждение поверх панели
	static constexpr int32 kRowMenuZ = 8810; // 8100 — всплывашка поверх панели (полоса приоритетных)

	UPROPERTY(Transient) TObjectPtr<UUserWidget> CreateWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> JoinWidget   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> Combo   = nullptr;   // 2254
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> CreateText = nullptr;   // 2251
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CreateRule = nullptr;   // 2255
	UPROPERTY(Transient) TObjectPtr<UButton>    CreateOk   = nullptr;   // 2252
	UPROPERTY(Transient) TObjectPtr<UButton>    CreateNo   = nullptr;   // 2253
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CreateOkText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CreateNoText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget>    ChoiceGroup  = nullptr;  // PartyTake_ChoiceBase_grp

	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinText  = nullptr;    // 2246
	UPROPERTY(Transient) TObjectPtr<UButton>    JoinOk    = nullptr;    // 2248
	UPROPERTY(Transient) TObjectPtr<UButton>    JoinNo    = nullptr;    // 2249
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinOkText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinNoText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UImage>     JoinBarFill = nullptr;  // 2247, план ES1

	// ── ОКНО 2200: узлы одной строки участника ──
	//   ⚠ Имена планов у импортёра — `{idc}_{порядковый}_{план}`, и порядковый РАЗНЫЙ у каждой
	//     строки (`PARTY_HP00_31_0`, `PARTY_HP01_82_0`, …), поэтому ищем по ПРЕФИКСУ
	//     (`AsdaUi::FindNodeByPrefix`), а не по точному имени (закон `jui-importer-widget-naming`).
	struct FAsdaPartyRowUi
	{
		TObjectPtr<UWidget>    Root      = nullptr;   // `PARTY_WIN{NN}_grp`
		TObjectPtr<UTextBlock> Name      = nullptr;   // `PARTY_NAME{NN}`
		TObjectPtr<UTextBlock> Level     = nullptr;   // `PARTY_LEVEL{NN}`
		TObjectPtr<UImage>     HpFill    = nullptr;   // `PARTY_HP{NN}_*_1` (ES1 — заливка)
		TObjectPtr<UImage>     MpFill    = nullptr;   // `PARTY_MP{NN}_*_1`
		TObjectPtr<UImage>     Sword     = nullptr;   // `PARTY_SWORD{NN}_*_0`
		TObjectPtr<UImage>     Bow       = nullptr;
		TObjectPtr<UImage>     Staff     = nullptr;
		TObjectPtr<UImage>     LeaderIcon= nullptr;   // `PARTY_ICON{NN}_*_0` — ★ значок главы
		TObjectPtr<UImage>     Select      = nullptr; // `Party_MemSelect{NN}_*_0` — подсветка строки
		// ★ «Он выбран целью» — `Party_TargetShare{NN}` (2288…2293). Эталон показывает именно его
		//   (`partyUI_ShowSelectedSlot@0xB97F30` → `apTargetShare`, `CParty+0x3C`).
		TObjectPtr<UImage>     TargetShare = nullptr;
		TObjectPtr<UWidget>    StateRoot = nullptr;   // `PARTY_ICONBACK{NN}_grp`
		TArray<TObjectPtr<UImage>> StateIcons;        // 28 штук `PARTY_STATE_ICON{N}{ii}_*_0`
		// ★ 30.10.2026: что нарисовано в иконке `i` (упаковано подряд, как `aStateIconId` эталона) — для подсказки при
		//   наведении: id эффекта, вид его записи (1 — stateinfotip, 3 — soulstateinfotip) и номер исходной записи пакета.
		TArray<int32> StateTipId;
		TArray<uint8> StateTipKind;
		TArray<int32> StateTipRaw;
		float HpFullW = 0.f, MpFullW = 0.f;           // исходная ширина заливок из ассета
		FAsdaBarSmoother HpBar, MpBar;                // ★ ОБЩАЯ точка сглаживания полос
		int32 MemberIdx = INDEX_NONE;                 // какой участник сейчас в строке
	};

	UPROPERTY(Transient) TObjectPtr<UUserWidget> RosterWidget = nullptr;
	TArray<FAsdaPartyRowUi> Rows;
	UPROPERTY(Transient) TObjectPtr<UWidget>    RosterNormal   = nullptr;  // `Party_WinN_grp`
	UPROPERTY(Transient) TObjectPtr<UWidget>    RosterHeader   = nullptr;  // `Party_WinH_grp`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBanish      = nullptr;  // 2262
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnLeave       = nullptr;  // 2260
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnMini        = nullptr;  // `PARTY_MINIBUT_H` (свёрнутый вид)
	// ★ Крестик РАЗВЁРНУТОГО окна — это `PARTY_MINIBUT`, и импортёр сделал из него `UCheckBox`
	//   (скин `CloseBtn_18_Party`, у него два кадра: SS0 обычный и SS2 нажатый).
	UPROPERTY(Transient) TObjectPtr<class UCheckBox> BtnMiniCheck = nullptr;
	// ★ 22.10.2026: якорь реле кнопок САМОЙ панели — свой, обнуляется при каждой привязке (панель пересобирается после
	//   каждого сноса окна; общий `Relays` копил бы реле умерших кнопок — закон «массивы контролов в Setup — Reset()»).
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> RosterRelays;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnBanishText  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnLeaveText   = nullptr;

	bool  bRosterReady = false, bRosterOpen = false, bRosterCollapsed = false;
	int32 SelectedSlot = -1;                 // эталонный `g_nPartyMemberMenuIdx`
	int32 SharedTargetSlot = -1;             // эталонный `g_nPartySelIdx` (общая цель)
	int32 SelectCycleState = 0;              // ★ 25.10.2026: эталонный `g_nDlgStateMachine` — круг Shift+Tab по строкам
	int32 PendingRowSlot   = -1;             // эталонный `g_nPartySelIdxPending` (ЛКМ по строке)
	int32 MenuRowSlot      = -1;             // строка, по которой открыто меню 8100
	int32 TargetedUserNum  = -1;             // кого мы выбрали целью кликом по строке
	/** Обводка «этот участник — моя цель» / «помечен общей целью» (`Party_TargetShare{NN}`). */
	void ApplyTargetShareHighlight();
	/** Мышь по строкам панели: ЛКМ — цель, ПКМ — меню 8100. Зовётся из `Tick`. */
	void TickRosterMouse();
	// ★ 30.10.2026 [ОРИГИНАЛ] наведение на панель 2200 (`partyWnd_onCommand` событие 8/0xA): справка кнопок 2260/2262/2279,
	//   значков главы 2209+5k, имён 2210+5k (`uiShowHelpTooltipByControlId@0xB9B9C0`, темы 18…22) и подсказки иконок состояний
	//   участников 33100+100r+i (`uiHelpEntryLayoutAndShow[Alt]`); у 2279 — подмена шестерёнки 2280 на анимированную 2281.
	void TickRosterHover();
	void SetGearHover(bool bOn);              // шестерёнка 2280 ↔ анимированная 2281 (наведение на 2279)
	FAsdaHelpTipHover HelpTip;                // контрол под курсором и показанная подсказка (владелец `EAsdaTipOwner::PartyHud`)
	bool bGearHover = false;                  // анимированная шестерёнка показана наведением

	// ── меню строки `Follow_Another_win` 8100 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> RowMenuWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>     RowMenuMark   = nullptr;  // 8102 «Указать цель»
	UPROPERTY(Transient) TObjectPtr<UButton>     RowMenuFollow = nullptr;  // 8101 «Следовать»
	UPROPERTY(Transient) TObjectPtr<UButton>     RowMenuClose  = nullptr;  // 8103 «Закрыть»
	bool bRowMenuReady = false, bRowMenuOpen = false;
	/** Исходный левый-верхний угол детей меню 8100 (запечённая база .JUI Origin "519 266").
	 *  Запоминается ОДИН раз: иначе повторные открытия двигали бы меню накопительно. */
	FVector2D RowMenuBaseTopLeft = FVector2D::ZeroVector;
	bool      bRowMenuBaseTaken  = false;
	bool EnsureRowMenu();
	void SetupRowMenu();
	FAsdaMouseFrame RosterMouse;             // кадр мыши для строк панели
	bool  bRosterDirty = false;              // перерисовать на ближайшем тике

	// ── окно роспуска/подтверждения `PARTYDISPERSION` 2256 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> DisbandWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  DisbandText   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>     DisbandOk     = nullptr;  // 2258
	UPROPERTY(Transient) TObjectPtr<UButton>     DisbandCancel = nullptr;  // 2259
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  DisbandOkText     = nullptr;  // подпись 2258 (loc 2288)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  DisbandCancelText = nullptr;  // подпись 2259 (loc 2378)
	bool bDisbandReady = false, bDisbandOpen = false;
	/** Показать/спрятать окно 2256 (эталон `CControl_SetVisible(m_pDisbandWin, …)`). */
	void SetDisbandVisible(bool bVisible);
	/** Общая часть кнопок 2258/2259: 5098 с режимом `Mode` (если в отряде) и скрытие окна. */
	void SendDisbandChoice(uint8 Mode, const TCHAR* Why);

	// ── ★ отметки соотрядников на мини-карте (§10) ──
	/** Перестроить список `EAsdaMapMark::Party` мини-карты из `Members` (порт `minimap_DrawPartyMarkers`:
	 *  только в отряде, без себя, с картой и координатами). Зовётся из `Tick` по флагу `bMarksDirty`. */
	void PushMinimapMarks();
	bool  bMarksDirty = false;               // состав/координаты изменились — перестроить отметки
	int32 PushedMarkCount = -1;              // сколько отметок отдано мини-карте последним разом (лог/дамп)

	/** Снять авто-перенос у текста и зафиксировать перенос по ширине слота (см. .cpp). */
	static void FixTextWrapJitter(class UTextBlock* T, const TCHAR* Who);

	bool  EnsureRosterWidget();
	void  SetupRosterControls();
	// ★★★ 22.10.2026 (баг игроков «Team HUD occasionally disappears»). У ОРИГИНАЛА окно 2200 — постоянный контрол
	//   `CParty::m_pMemberWin`: его гасят ТОЛЬКО роспуск/выход (`partyDisbandAndClearMembers@0x9CC9F0` — 5099/5101/5103, 3226,
	//   выход в выбор персонажа game_main.c:3351) и 6200 (`uiWndHideAndKillFocus`, network.c:712 — ★ ПРИЁМНИКА 6200
	//   `OP_UI_WND_HIDE_AND_KILL_FOCUS` В ПОРТУ НЕТ: панель по 6200 не прячется, у порта `bRosterOpen` снимают только
	//   5099/5101/5103 и полный сброс сессии; 3226 инстанса — тоже без приёмника). Смена карты
	//   (game_main.c:3540-3544 — только автоотказ висящей заявки), 4055 (`netHandler_EnterWorld@0xF43050`) и 4056
	//   (`GameNet_OnLocalPlayerMapEntry@0xF43390`) окно не трогают; 5107 лишь перерисовывает его.
	//   У порта окно — виджет вьюпорта, и его сносят: травел UE (виджеты умирают с миром), `UAsdaWindowSubsystem::CloseAll`
	//   из `RevealWorld` (4003 каждого входа на карту, 4055/4056 своего), а пересоздавал панель только приход 5107. Сервер
	//   5107 шлёт лишь при смене состава/входе участника на карту (GameServer `sub_450E10@0x450E10`, ретрансляция 3217) —
	//   воскрешение на той же карте (4056) и гонка «5107 раньше 4003» оставляли отряд без панели до следующего 5107.
	//   [ИСПРАВЛЕНИЕ ПОРТА] пока панель должна быть видна (`bRosterOpen` — его снимают ровно пути эталона выше), а окна
	//   во вьюпорте СВОЕГО мира нет — пересобрать и показать с тем же составом и свёрнутостью. Не на экране загрузки и
	//   не на логине (там окно умрёт следующим травелом/`RevealWorld`). Откат: `asda.Party.RosterSurvivesWipe 0`.
	void  HealRosterIfLost();
	int32  CntRosterHeal = 0;            // сколько раз панель восстановлена
	double NextRosterHealTry = 0.0;      // после неудачной сборки окна — не чаще раза в секунду
	/** Каркас окна 2256 (`FAsdaJuiWindowHandle::Ensure`) — как у 2250/2245. */
	bool  EnsureDisbandWidget();
	/** Привязка контролов 2257/2258/2259 по idc (порт `social.c:2048-2069`) + подписи кнопок. */
	void  SetupDisbandControls();
	/** Заливка полосы: ширина плана ES1 = полная ширина × доля (как у полос прочности). */
	static void ClipBar(UImage* Fill, float FullW, float Frac);
	/** Порядок участников для панели 1:1 с эталоном: глава → слот 0, себя пропускаем. */
	void BuildSlotOrder(TArray<int32>& OutMemberIdxBySlot) const;

	bool bCreateReady = false, bJoinReady = false;
	bool bCreateOpen  = false, bJoinOpen  = false;

	// ── состояние заявок (зеркала четырёх байтов CharManager, §4) ──
	bool   bMyRequestActive = false;      // +2365664
	double MyRequestStart   = 0.0;        // charManagerMarkAndStampTime → party+1728
	bool   bIncomingPending = false;      // +2365665
	bool   bIncomingIsNew   = false;      // +2365666 (пришло по 5091)
	bool   bIncomingIsJoin  = false;      // +2365667 (пришло по 5095)
	double IncomingStart    = 0.0;        // g_dwPartyReqTimerStart

	int32   IncomingReqId    = -1;        // g_nPartyInviteReqId (partyIndex из тела)
	int32   IncomingUserNum  = -1;
	int32   IncomingCharId   = 0;
	FString IncomingName;
	int32   IncomingLootRule = 0;         // байт @27 тела

	// ── цель ОТКРЫТОГО окна 2250 ──
	int32   InviteTargetUserNum = -1;
	FString InviteTargetName;

	// ── состояние отряда ──
	bool  bInParty  = false;
	bool  bIsLeader = false;
	int32 PartyIndex = -1;
	int32 LootRule   = 0;                 // §6 D4: оригинал этого не помнит, мы помним
	UPROPERTY(Transient) TArray<FAsdaPartyMember> Members;

	// ── очередь пакетов (закон no-reentrant-net-callbacks: из колбэка только КЛАДЁМ) ──
	TAsdaNetQueue<TPair<int32, TArray<uint8>>> PendingIn;
	FDelegateHandle HReq, HRes;

	FAsdaMouseFrame Mouse;

	// счётчики для диагностики
	int32 CntOpened = 0, CntSentReq = 0, CntSentAns = 0, CntIncoming = 0, CntTimeout = 0;
};
