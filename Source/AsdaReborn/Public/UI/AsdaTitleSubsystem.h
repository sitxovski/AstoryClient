// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaTitleSubsystem — ★★★ МЕНЮ ЗВАНИЙ (Fame/Title): окно `Title_Win` (JUI IDNum **18700**)
//   и значок-уведомление `Title_Notice_BG` (**50000**/50001/50002) на HUD.
//
//   Полный разбор с адресами, побайтовыми раскладками, таблицей нотисов и дефектами —
//   `Reports/title_system.md`. Здесь — карта того, что реализовано, и почему именно так.
//
// ═════════ ★ ТЕРМИНОЛОГИЯ И МИСНЕЙМЫ ЭТАЛОНА ══════════════════════════════════════════════════
//   Класс окна — **CUITitle**, глобальный экземпляр `g_pCharTitleMgr`. IDA назвала почти ВСЕ его
//   методы `questLog*`/`uiQuickbar*`/`char*StateFlag*` — это МИСНЕЙМЫ авто-именования: ни к
//   квест-логу, ни к хотбару, ни к «флагам состояния» они отношения не имеют. Снято здесь:
//     `uiTitleWnd_Init@0xE97A60`            → CUITitle::Init (сброс флага и разбор контролов)
//     `CUITitle_InitControls@0xE96430`      → разбор ВСЕХ контролов по idnum (имена — из
//                                             диагностических `MessageBoxA` самого эталона)
//     `questLogWnd_Open@0xE98380`           → ОТКРЫТЬ ОКНО      → `OpenTitleWindow()`
//     `uiTitleHide@0xE985C0`                → закрыть           → `CloseTitleWindow()`
//     `questLog_OnUiEvent@0xE97B00`         → диспетчер событий окна
//     `questLogWnd_HandleMessage@0xE98160`  → клики кнопок 18712/18727/18728/18737/18738
//     `QuestLog_PopulateCategoryList@0xE98C00` → наполнение 12 строк списка
//     `ItemTooltip_RefreshStats@0xE99360`   → инфо-панель выбранного звания (6 бонусов)
//     `questItem_ValidateSelection@0xE99C30` → проверка перед удалением
//     `uiSetSelectedTargetNameText@0xE98A50` → кнопка «Применить» (выбор в слот приставки/окончания)
//     `uiCollectionRefreshInfo@0xE98640`    → шапка: ранг / всего очков / % собрано
//     `uiQuickbarSelectSlot@0xE99EE0`       → клик по строке списка
//     `uiQuickbarUpdateBlink@0xE9A090`      → мигание маркеров «новое»/«потеряно» и значка HUD
//     `uiTitle_TickCountdown@0xE9A550`      → обратный отсчёт кулдауна (таймер 0x11, 1000 мс)
//     `questLog_SortCategoryByColumn@0xE9CC80` → сортировка по клику в заголовок колонки
//     `charTestStateFlagA@0xE9C240`         → бит «звание ПОЛУЧЕНО» (маска 1)
//     `charHasStateFlagByUserIdx@0xE9C310`  → бит «звание НАЙДЕНО» (маска 2)
//     `charSetStateFlagByUserIdx@0xE9C3E0`  → поставить/снять бит маски 1 (+ пометка «новое»/«потеряно»)
//     `questLog_toggleTrackBit@0xE9C620`    → поставить/снять бит маски 2
//     `netHandler_OnGuildTitleResult@0xE9A850` → ★ НЕ гильдия: «секрет звания открыт» (6085)
//     `uiTitle_OnJobChangeResult@0xE9A9F0`  → ★ НЕ смена профессии: «звание получено/потеряно» (6065)
//     `netOnCharEquipUpdate@0xE9A6F0`       → ★ НЕ экипировка: маска «получено» (6066)
//     `netHandler_OnCharData32@0xE9A7D0`    → маска «найдено» (6086)
//     `charApplyStateFlagDataA@0xE9BFE0`    → блоки статов званий (батч 2255)
//   Внутриигровой (и серверный) термин системы — **Fame**: опкоды `OP_*_FAME`, серверные
//   `AddFameBit` / `CheckFameBit` / `ChangeActiveFame` / `DeleteFameInfo`.
//
// ═════════ ТОЧКА ВХОДА ═════════════════════════════════════════════════════════════════════════
//   ★ ПУБЛИЧНЫЙ МЕТОД ОТКРЫТИЯ — **`UAsdaTitleSubsystem::OpenTitleWindow()`** (Blueprint-callable).
//   Эталонные вызывающие `questLogWnd_Open(&g_pCharTitleMgr)`:
//     • пункт «Меню званий» доп. меню «Прочее» — `uiOpenWindowDispatch@0xECD570` case 0
//       (подсказка пункта — нотис 20505 «Открывается меню получения/восстановления званий»);
//     • горячая клавиша, команда 38 (`input.c:4303`);
//     • кнопка 905 окна характеристик 900 (`case 0x389`, `ui_core.c:74403`);
//     • кнопка 2552 окна чата (`chat.c:2150`);
//     • клик по значку-уведомлению 50000, когда он мигает (`quest.c:8441`).
//   Открытие — ТОГГЛ: если окно уже видно, `OpenTitleWindow()` его закрывает (эталон:
//   `if (*(_BYTE*)(win+292) == 1) return uiTitleHide(this)`).
//
// ═════════ ГЕЙТЫ ОТКРЫТИЯ (1:1 `questLogWnd_Open`) ═════════════════════════════════════════════
//   Окно НЕ открывается, если открыт магазин NPC (`g_pShopWnd`), окно предмета (`g_pItemMainWnd`)
//   или диалог количества (`g_pItemQtyMakeDlg`). Порядок проверок сохранён.
//
// ═════════ ДЕРЕВО КОНТРОЛОВ (все idnum — из `CUITitle_InitControls`, имена из его MessageBoxA) ══
//   18700 `Title_Win` 744×526 @(32,111) — корень, Visible="0"
//     18701 `Title_Win_Gear` · 18702 `Title_Win_AniGear` — шестерёнки (hover по крестику)
//     18703 `Title_Win_CloseBtn` — крестик
//   ШАПКА СЛАВЫ: 18704 `Title_Ranking` (нотис 2401 «Ранг: [sd1]») · 18705 `Title_TotalPoint`
//     (2402 «Всего: [ss1]») · 18706 `Title_TotalGathRate` (2403 «Всего собрано %:» + "%.2f%%")
//     дельты 188480/188510/188540 и стрелки 188490/188500/188520/188530/188550/188560
//   АКТИВНЫЕ ЗВАНИЯ: 18708 `Title_1stTitle_Name` (приставка) · 18710 `Title_2ndTitle_Name`
//     (окончание) · 18711 `Title_Title_CharName` · 18712 `Title_ReleaseBtn` (2404)
//   ФИЛЬТР: 18713 `Title_ClassSelectBtn` — 3 сегмента: 2405 «Обычные» · 2406 «Соревнование» ·
//     2407 «Секретные»; 18714 `Title_ClassGathRate` (2408 «Собрано %:» по текущей вкладке)
//   ЗАГОЛОВКИ КОЛОНОК (клик = сортировка): 18729 `Title_TNum` (2409) · 18730 `Title_TCategory`
//     (2410) · 18731 `Title_TTitleName` (2411) · 18732 `Title_TTitlePosition` (2412) ·
//     18733 `Title_TGrade` (2413) · 18734 `Title_TPoint` (2414) · 188350 `Title_TState` (2434)
//   СПИСОК: 12 строк, шаг idnum 8 (и 10 у State/Find/Lose/NoticeBar):
//     `Title_ListBox0N` 18739+8N · `_Sel` 18740+8N · Num 18741+8N · Cate 18742+8N ·
//     Name 18743+8N · Position 18744+8N · Grade 18745+8N · Point 18746+8N ·
//     State 188360+10N · Find 60001+10N · Lose 60002+10N · NoticeBar 60003+10N
//     18736 `Title_ListSelected` (зелёная рамка выбора) · 18735 `Title_Wheel` (полоса, 12 позиций)
//   ИНФО-ПАНЕЛЬ: 18715 (2419) · 18716 имя · 18717 грейд (2420) · 18718 очки (2421) ·
//     18724 место (2422) · 18840…18845 шесть бонусов · 18723 условие получения ·
//     188570/188590 тип-эпизод · 188580 случайный бонус
//   КУЛДАУН: 18725 (2417 «Времени на изменения:») · 18726 (2418 «[sd1] мин. [sd2] сек.» / 2431)
//   КНОПКИ: 18737 `Title_List_AppBtn` (2415 «Применить») · 18738 `Title_List_DelBtn`
//     (2416 «Очистить») · 18727 `Title_BtnOk` (2423 «ОК») · 18728 `Title_BtnCancel` (2424 «Отмена»)
//   ЗНАЧОК HUD: 50000 `Title_Notice_BG` · 50001 `Title_Notice_Lose` · 50002 `Title_Notice_Find`
//   18846 `Title_ListBase` и 18847 `Title_List_Win` — контейнеры (эталон снимает им флаги 166/668)
//
// ═════════ ДАННЫЕ (★ ТОЛЬКО ИЗ ТАБЛИЦ, НИКАКОГО ХАРДКОДА) ═════════════════════════════════════
//   `DT_title_list`   (`title_list.bin`, 240 б, **280 записей, id 0…279**) — определения званий.
//     Поля, снятые по коду окна: `SortNum`(+4) · `ClassReq`(+12) · `Name`(+16) · `ColorIndex`(+80,
//     ГРЕЙД) · `Group`(+84, вкладка) · `Position`(+88, приставка/окончание) ·
//     `CategoryLocIndex`(+92) · `Point`(+96) · `Desc`(+100, условие получения) ·
//     `OptionTypeId0…5`(+176) · `Enabled`(+200) · `IsPercent0…5`(+204).
//   `DT_title_option` (36 б, 109 записей) — словарь имён статов для шести бонусов.
//   `DT_title_rank`   (68 б, 106 записей) — значки ранга на плашке (уже используется плашкой).
//   `DT_title_effect` (260 б) — визуальные эффекты; окном не используется.
//   ЗНАЧЕНИЯ бонусов приходят от СЕРВЕРА (пакеты 2255 и 6065), в таблице их нет — ровно как в
//   оригинале: таблица даёт ТИП бонуса, сервер — число.
//   ⚠ В ЭТОЙ сборке данных стат-бонусов у званий НЕТ ВООБЩЕ: `OptionTypeId0…5` равны 0 у ВСЕХ
//   280 клиентских записей, слот-баффы (кол. 12…41) равны 0 у ВСЕХ 251 серверной, а
//   `title_option[0]` несёт пустое имя. Оригинал в этом случае гасит все шесть строк
//   (`if (statTypeRecord) { if (тройка != −1 && *(DWORD*)statTypeRecord) … else очистить }`),
//   то есть панель шести бонусов у него ВСЕГДА пуста. Порт ведёт себя так же.
//
//   ★ СИММЕТРИЯ С СЕРВЕРОМ (`\\@@ASTORY_SERVER_HOSTNAME@@\…\v2\GameServer\ConfigSet\FameInfo.txt`, 251 запись):
//     • типы шести бонусов совпадают у ВСЕХ 251 общих записей — **0 расхождений**;
//     • очки: **42 расхождения из 251** (у 26 сервер даёт 0, у 16 другое ненулевое число);
//     • id 251…279 (**29 званий**) серверной строки НЕ имеют вовсе.
//     Это ДЕФЕКТ ДАННЫХ, чинится синхронизацией таблиц, а не кодом; числа и списки — в отчёте.
//     `CompetitionFameInfo.txt` (63 записи) грузит НЕ GameServer, а NpcServer — это отдельное
//     пространство id, к `title_list` оно не относится.
//
// ═════════ СЕТЬ ═══════════════════════════════════════════════════════════════════════════════
//   C→S 6067 `OP_CHANGE_ACTIVE_FAME`  (34 б: конверт `SendSecured` 24 + payload 10) — надеть.
//   C→S 6069 `OP_DELETE_FAME_INFO`    (32 б: 24 + 8)                                — удалить.
//   S→C 6065 звание получено/потеряно · 6066 маска «получено» · 6068 активные слоты+ранг ·
//       6070 результат удаления · 6085 «секрет открыт» · 6086 маска «найдено» ·
//       2255 батч блоков статов. Раскладки — у объявлений делегатов в `AsdaNetSubsystem.h`.
//   ★ Конверт у обеих отправок — `SendSecured` (8 б секрет + 16 б MD5), `bPad = true`:
//     эталонный `CNetwork_Send(..., OP, 1, 1, 1, 0, 0)` передаёт a6 = 1.
//   ★ `userNum` (i16) и `charId` (i32) в теле РАЗНЫЕ — закон `charid-vs-usernum`.
//
// ═════════ НОТИСЫ: событие → id строки → канал ════════════════════════════════════════════════
//   | событие                                            | id     | канал                        |
//   | «ОК» в кулдауне                                    | 2425   | ONEBUT_MSGBOX (модалка)      |
//   | «Применить» на неполученном звании                 | 2426   | ONEBUT_MSGBOX  ★ см. дефект D4|
//   | «Очистить» — подтверждение удаления                | 2427   | TWOBUT_MSGBOX (да/нет)       |
//   | «Очистить» на звании группы «Соревнование»         | 2428   | ONEBUT_MSGBOX                |
//   | «Очистить» на неполученном звании                  | 2429   | ONEBUT_MSGBOX                |
//   | «Очистить» на надетом сейчас звании                | 2430   | ONEBUT_MSGBOX                |
//   | подсказка кнопки 905 окна 900                      | 2432   | тултип (чужое окно)          |
//   | пустое звание в строке имени                       | 2433   | подпись плашки (чужое окно)  |
//   | hover «Применить» / «Очистить» / «ОК» / «Отмена»   | 20400…20403 (заголовки 20404…20407) | плашка-тултип |
//   | состояние строки: не получено / получено / найдено | 20409 / 20408 / 20410 | текст колонки |
//   | значок HUD: новое звание                           | 20500  | плашка-тултип (заголовок 20504)|
//   | значок HUD: открыт секрет звания                   | 20501  | плашка-тултип                |
//   | значок HUD: звание потеряно при обновлении         | 20502  | плашка-тултип                |
//   | значок HUD: система званий обновлена               | 20503  | плашка-тултип                |
//   Свои строки (id > 990000) НЕ добавлялись: всё, что показывает окно, уже есть в `msg*.dat`.
//
// ═════════ НУМПАД 1410 ════════════════════════════════════════════════════════════════════════
//   ★ ОКНО ВВОДА КОЛИЧЕСТВА В ЭТОЙ ФИЧЕ НЕ УЧАСТВУЕТ (сверено с `Docs/Port/systems/NUMPAD_FLOW.md`).
//   Доказано двумя способами: (1) перебором ВСЕХ функций класса 0xE962A0…0xE9CC80 — вызова
//   `uiOpenContextPopupWithHandlers@0xDA55A0` нет; (2) полным списком вызывающих этой функции по
//   мастер-декомпилю — ни одна не лежит в диапазоне класса. Количество здесь вводить нечего:
//   звание — единичная сущность, «применить»/«удалить» работают по выбранной строке.
//
// ═════════ ДЕФЕКТЫ ОРИГИНАЛА (все с ключом отката `asda.Title.*`) ═════════════════════════════
//   D1. ★ ДЕЛЬТЫ ШАПКИ — МЁРТВЫЙ КОД. `StatPanel_RefreshDiffs@0xE9C7E0` ЗАШИВАЕТ шесть констант
//       (10, 9999999, 3, 10, 9999999, 3), считает по ним три разности (всегда 0), форматирует
//       строки… и печатает в контролы 188480/188510/188540 **пустую строку** вместо них.
//       Итог: три метки дельты всегда пусты, стрелки всегда скрыты. Воспроизведено 1:1;
//       `asda.Title.ShowRankDelta 1` печатает то, что эталон СЧИТАЛ (даст «-»: разности нулевые).
//   D2. ★ СНЯТ 27.09.2026 — ДЕФЕКТА НЕТ, БЫЛ МИСНЕЙМ. `uiSetSelectedTargetNameText@0xE98A50`
//       (кнопка «Применить») и `questLog_SortCategoryByColumn` берут маску через
//       `Target_GetSelectedUserIdx(LocalPlayer)`, а этот геттер (thunk 0x9254E5 →
//       `Char_GetHoveredUserIdx@0x966490`, `*(u16*)(this+190188)`) возвращает номер ПЕРЕДАННОГО
//       персонажа — с локальным игроком это СВОЙ userNum. [ОРИГИНАЛ] эталон проверяет свои звания,
//       порт делает то же (`IsOwned`). Ключ `asda.Title.OwnerMaskFromTarget` удалён: откатывать
//       нечего (ревизия `Reports/owner_0927/charid_senders_REVIEW.md` МЕЛК-2).
//   D3. ★ ЗВАНИЯ С id ≥ 256 БЫЛИ НЕДОСТУПНЫ — ★ ЗАКРЫТО 2026-08-29 РАСШИРЕНИЕМ МАСКИ ДО 512 БИТ.
//       Как было в оригинале: маска — ровно 256 бит и у клиента, и у сервера. Клиент —
//       `CCharacter[1].worldMatrix[6]` («получено») и `worldMatrix[14]` («найдено»), по 0x20 б
//       (`netOnCharEquipUpdate@0xE9A6F0` копирует `0x20`, `netHandler_OnCharData32@0xE9A7D0` —
//       тоже `0x20`); индексация `(1LL << (id % 64)) & *(_QWORD*)&worldMatrix[2*(id/64)+6]`
//       (`charTestStateFlagA@0xE9C240`, `charHasStateFlagByUserIdx@0xE9C310`) — для id ≥ 256
//       читает ЗА границей маски (id 256…279 попадали в соседнюю маску «найдено»).
//       Сервер — `CUser+0x17DDC` («получено») и `CUser+0x17E00` («найдено»), по 4 qword;
//       гейт `(unsigned)(id/64) < 4` в `CheckFameBit@0x54A890`, `AddFameBit@0x54A980`,
//       `SubFameBit@0x54AD00`; загрузчик `FameInfo.txt` (`sub_4A18F0@0x4A18F0`) режет
//       `count > 256` → «FameInfo count over». В таблице `title_list` таких званий **24**
//       (id 256…279).
//       ★ КАК СТАЛО: маска 512 бит (64 байта). Формат пакетов САМООПИСАТЕЛЕН ПО ДЛИНЕ ТЕЛА —
//       расширенные 32 байта дописаны В ХВОСТ, старая часть не сдвинута:
//         6066 «получено»: 42 б (старый сервер) либо 74 б (новый) — хвост [42..73] = биты 256…511;
//         6086 «найдено» : 38 б (старый сервер) либо 70 б (новый) — хвост [38..69] = биты 256…511.
//       Старый клиент читает фиксированные 42/38 байт и хвост игнорирует; новый клиент по длине
//       тела понимает, какой формат пришёл. C→S менять не пришлось: в 6067/6069 titleId уже i16.
//       Откат к оригинальным 256 битам — `asda.Title.Mask512 0` (хвост игнорируется, бит вне
//       0…255 = «не получено» + запись в лог, как было до расширения).
//   D4. Кнопка «Применить» на НЕПОЛУЧЕННОМ звании в эталоне не делает НИЧЕГО и молчит, хотя
//       строка 2426 «Чтобы носить звание, нужно его заслужить» существует и не используется
//       нигде. Исправлено: показываем 2426. Откат — `asda.Title.SilentApplyDeny 1`.
//   D5. Текст 2425 обещает «1 раз в 10 минут», а кулдаун и клиента (`this+207 = 60000`), и
//       сервера (`+0x17DD8`, порог 60 с) — ОДНА минута. Расхождение в ТАБЛИЦЕ ТЕКСТА, код 1:1.
//   D6. Колонка «Категория» берёт нотис `1460 + CategoryLocIndex`, но в таблице встречаются
//       значения 6 и 7 (**15 званий**), а строки заканчиваются на 1465 — 1466/1467 это уже
//       «Преф.»/«Оконч.». Строку не выдумываем: при выходе за 1460…1465 колонка пуста + лог.
//       Откат к эталонному «чужому» тексту — `asda.Title.RawCategoryLoc 1`.
//   D7. `uiCollectionRefreshInfo` считает «Всего собрано %» как «биты 0…255 / g_nDataRecIdMax»,
//       где знаменатель = МАКСИМАЛЬНЫЙ id (279), а не число записей. Воспроизведено 1:1.
//   D8. ★★★ СНЯТИЕ ЗВАНИЯ ВЖИВУЮ («ультимативное снятие») — эталон его НЕ УМЕЕТ.
//       ★ КАК В ОРИГИНАЛЕ (доказано по декомпилу, два места):
//         (а) ЗАЩЁЛКА. 6066 разбирается В ДВА ПРИЁМА. Диспетчер зовёт
//             `netOnCharEquipUpdate(&g_pCharTitleMgr, &pkt[9], 0)` (`network.c:495`) — эта ветка
//             ТОЛЬКО КОПИРУЕТ 0x2A байт тела в буфер `this+977` и больше НИЧЕГО не делает.
//             Применение — вторая ветка (`a3 == 1`), и зовут её из
//             `sub_E9A6B0@0xE9A6B0` (`network.c:60876`): `if (*(DWORD*)(this+979) != -1)
//             netOnCharEquipUpdate(this, this+977, 1)`, после чего `this+979 = -1` (буфер гасится
//             как «съеденный»). То есть до дренажа маска в персонаже СТАРАЯ — ровно грабля
//             `no-state-latch-before-shown`.
//         (б) НЕТ ДИФФЕРА. Ветка применения делает `memcpy(&char[1].worldMatrix[6], тело+10, 0x20)` —
//             тупая перезапись. Эталон НЕ СРАВНИВАЕТ новую маску со старой, поэтому при
//             серверном снятии он: не зажигает маркер «потеряно», не гасит НАДЕТОЕ звание и
//             ничего не говорит игроку. Единственный путь утраты, который он замечает, — 6065
//             с флагом «потеряно» (+8), и тот лишь ставит бит-маркер.
//       ★ КАК В ПОРТУ (требование владельца: «звание обязано уйти, даже если окно не открыто»):
//         1) ЗАЩЁЛКИ НЕТ. Двухфазность не портировалась вовсе: `AsdaNetSubsystem` отдаёт тело
//            делегатом сразу, подсистема кладёт его в очередь и разбирает в СВОЁМ `Tick`
//            (`FTickableGameObject`, тикает всегда — окно к этому отношения не имеет).
//         2) ДИФФЕР. `HandleMaskOwned` снимает снимок маски ДО разбора и сравнивает: биты 1→0 —
//            это утраты. Дальше — общая точка `ApplyTitleLoss`, одна на 6066 и 6065.
//         3) СНЯТИЕ НАДЕТОГО. Если утраченный id стоял в `WornPrefix`/`WornSuffix`, слот
//            обнуляется локально и проталкивается в СВОЮ плашку над головой тем же путём, что и
//            приход 6068 (`UAsdaWorldSubsystem::ApplyCharTitleSlots`). У ЧУЖИХ персонажей это
//            делает сам сервер рассылкой 6068 — порт её принимает и применяет с 2026-08-хх
//            (`AsdaWorldSubsystem::OnCharTitle`), править там нечего. Откат — `asda.Title.UnwearOnLoss 0`.
//         4) НОТИС. В оригинале строки, НАЗЫВАЮЩЕЙ утраченное звание, НЕТ (единственная
//            про утрату — 20502 «Звание потеряно после обновления системы званий.», без слотов).
//            ★★★ РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-30: заведена НАША строка **990110**
//            «Ты утратил звание „[ss1]“.» (диапазон порта > 990000) с подстановкой имени
//            звания в СТРОКОВЫЙ слот `[ss1]` (закон `loc-string-placeholders`). Имя берётся из
//            `title_list` по id, который несут ОБА пути утраты: 6070 (поле titleId) и диффер 6066
//            (номер бита). Подстановка — общей точкой `FormatById` через `PostLocFmt`, своей
//            склейки строк нет. Кавычки-лапки „“ — НЕ U+0022, поэтому поле CSV не требует
//            экранирования и цепочка импорта его не рвёт.
//            ★ ОРИГИНАЛЬНАЯ 20502 НЕ ТРОНУТА и служит ЗАПАСНЫМ вариантом: её печатаем,
//            если имени звания в таблице нет — пустое место недопустимо (`no-scope-cutting`).
//            Канал — СИСТЕМНЫЙ ЧАТ, категория `Progress` (журнал 2570 + буфер B), по правилу
//            `audit-notices-on-new-system`. Ключ `asda.Title.LossNotice`: 0 молча · 1 наша строка
//            с именем (по умолчанию) · 2 только оригинальная 20502.
//       ★ КОНТРАКТ ПАКЕТОВ ПРИ СНЯТИИ (что мы принимаем от серверного патча):
//            6066 обновлённая маска «получено» (бит снят) — ОБЯЗАТЕЛЬНА, на ней держится всё;
//            6068 новые слоты активного звания — желательна (тогда п.3 просто подтверждается);
//            6065 flag=1 — необязательна, но если придёт, дублем не сработает (id уже утрачен).
//          Порядок значения не имеет: обе точки идемпотентны.
//       ★ ПРОВЕРКА ПУТЁМ ИГРОКА — `asda.Title.RevokeTest <id>` (см. регистрацию команд).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaScrollBarNomal.h"   // FAsdaScrollBarNomal — полоса 18735
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Styling/SlateTypes.h"      // FTextBlockStyle / FButtonStyle — кадры вкладок
#include "Framework/Text/TextLayout.h" // ETextJustify — снятое из ассета выравнивание
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaTitleSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UCanvasPanel;
class UAsdaButtonRelay;
class IConsoleObject;

/** ★★★ УЗЕЛ ФОРМАТИРОВАННОГО ТЕКСТА. Всё, что окно печатает, приходит С inline-тегами
 *  (`"<FC=%08x>%s</F>"` — так их строит сам эталон), поэтому показывать это можно ТОЛЬКО через
 *  общую точку `UAsdaRichText::ApplyFormatted`: она подменяет `UTextBlock` на `URichTextBlock`
 *  в том же слоте и ВОЗВРАЩАЕТ новый узел — кэш обязан переприсваиваться, иначе осиротеет.
 *  Стиль (кегль/цвет/выравнивание/перенос) снимается ИЗ АССЕТА до первой подмены: после неё
 *  исходные свойства уже не прочитать, а rich возьмёт дефолт `DT_AsdaRichStyle`. */
struct FAsdaTitleRichSlot
{
	UWidget* Node = nullptr;
	FTextBlockStyle Style;
	ETextJustify::Type Justify = ETextJustify::Left;
	bool  bWrap  = false;     // из ассета (`bWrap = высота слота >= 28`, закон ui-autowrap-canvas-gotcha)
	float WrapAt = 0.f;       // ширина переноса — ФИКСИРОВАННАЯ, по слоту из ассета
	bool  bCaptured = false;
};

/** Одно звание из `DT_title_list` в готовом для окна виде (кэш, строится один раз). */
USTRUCT()
struct FAsdaTitleDef
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() int32 SortNum = 0;            // колонка «№», формат "%ld"
	UPROPERTY() int32 ClassReq = 0;           // 0 любой · 1 class<=4 · 2 class 12..15 · 3 class 23..26
	UPROPERTY() FString Name;                 // имя для показа (без служебной «*», `AsdaLangRows::TitleDisplayName`)
	UPROPERTY() FString RawName;              // имя как в таблице: ключ сортировки эталона считает первые 10 байт вместе со «*»
	UPROPERTY() int32 Grade = 0;              // цвет + нотис 1450+Grade
	UPROPERTY() int32 Group = 0;              // вкладка 0/1/2
	UPROPERTY() int32 Position = 0;           // 0 приставка · 1 окончание
	UPROPERTY() int32 CategoryLoc = 0;        // нотис 1460+CategoryLoc
	UPROPERTY() int32 Point = 0;
	UPROPERTY() FString Desc;                 // условие получения (контрол 18723)
	UPROPERTY() int32 Enabled = 0;            // 0 ⇒ в список не попадает
	UPROPERTY() TArray<int32> OptionTypeId;   // 6 типов бонусов → `DT_title_option`
	UPROPERTY() TArray<int32> IsPercent;      // 6 флагов «значение в процентах»
};

UCLASS()
class ASDAREBORN_API UAsdaTitleSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaTitleSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Константы окна ──
	static constexpr int32 kIdWin       = 18700;
	static constexpr int32 kRows        = 12;    // видимых строк списка
	static constexpr int32 kOptionSlots = 6;     // бонусов у звания
	// ★ D3 ЗАКРЫТ: маска расширена до 512 бит. `kMaskBitsLegacy` — сколько было в оригинале
	//   (и сколько шлёт НЕ обновлённый сервер); `kMaskBits` — сколько мы храним.
	static constexpr int32 kMaskBitsLegacy = 256;
	static constexpr int32 kMaskBits       = 512;
	static constexpr int32 kMaskBytesLegacy = kMaskBitsLegacy / 8;  // 32 — хвост старого пакета
	static constexpr int32 kMaskBytes       = kMaskBits / 8;        // 64 — наше хранилище
	// Длины тела: старый формат → новый (старая часть НЕ сдвинута, 32 б дописаны в хвост).
	static constexpr int32 kBody6066Legacy = 42;  static constexpr int32 kBody6066Ext = 74;
	static constexpr int32 kBody6086Legacy = 38;  static constexpr int32 kBody6086Ext = 70;
	static constexpr int32 kCooldownMs  = 60000; // `this+207 = 60000` (нотис 2425 врёт про 10 мин)
	static constexpr int32 kWinZ        = 745;   // семья «окна персонажа»

	// ═══ ★ ПУБЛИЧНАЯ ТОЧКА ВХОДА ═══
	/** Порт `questLogWnd_Open@0xE98380`. ТОГГЛ: открытое окно закрывает.
	 *  false — гейт не пустил (открыт магазин/окно предмета/диалог количества) либо окна нет. */
	UFUNCTION(BlueprintCallable, Category="Asda|Звания")
	bool OpenTitleWindow();

	/** Порт `uiTitleHide@0xE985C0`. */
	UFUNCTION(BlueprintCallable, Category="Asda|Звания")
	void CloseTitleWindow();

	UFUNCTION(BlueprintPure, Category="Asda|Звания")
	bool IsTitleWindowOpen() const { return bOpen; }

	// ═══ ★ ЗНАЧОК HUD 50000 И «ЗАКРЫТЬ ВСЁ» / СНИМОК ОКОН (волна В5 ремесла, ревизия В4 С-2) ═══
	//   Поле эталона `CUITitle+880` = `bNoticeActive` (его же читает мигание `uiQuickbarUpdateBlink`, ui_core.c:82198).
	/** Предикат слота 8 снимка `sub_923F91` = `sub_D97230@0xD97230`: `*(this+8)+292` — контрол 50000 (`Title_Notice_BG`)
	 *  ВИДИМ (флаг видимости JUI). У порта фон показывает мигание ровно при взведённом `+880`. */
	bool IsNoticeIconShown() const;
	/** Закрыватель «закрыть всё» `sub_924419` = `set_o370@0xD61C00` (`[880] = 0`, ui_core.c:54393), зовётся из
	 *  `uiCloseAllWindows` (:54376). Эталон гасит только поле — контролы прячет ближайшее мигание; порт прячет сразу. */
	void HideNoticeIconForCloseAll();
	/** Восстановление слота 8 `sub_934373` = `set_o370_7470@0xD97470` (`[880] = 1`, ui_core.c:61025): значок снова
	 *  мигает с прежними видом и подсказкой (`+223`/`+222` эталон не трогает). */
	void ShowNoticeIconFromSnapshot();

	/** Обе НАДЕТЫЕ строки звания (пришли пакетом 6068) с inline-тегом цвета грейда — ровно то, что
	 *  эталон кладёт в `targetChar[1].gap_1CC` и `buffFxSlots[8]`. Их читает окно характеристик 900
	 *  (`uiRowSetTextOrLocalizedNotice@0xB95BB0`) и плашка над головой. Пустой слот = пустая строка. */
	UFUNCTION(BlueprintCallable, Category="Asda|Звания")
	void GetWornTitleTexts(FString& OutPrefix, FString& OutSuffix) const;

	/** Надетые id (−1 = слот пуст) — для диагностики и потребителей, которым нужен код, а не текст. */
	UFUNCTION(BlueprintPure, Category="Asda|Звания")
	int32 GetWornPrefixId() const { return WornPrefix; }
	UFUNCTION(BlueprintPure, Category="Asda|Звания")
	int32 GetWornSuffixId() const { return WornSuffix; }

	// ═══ ДЕЙСТВИЯ (ветки `questLogWnd_HandleMessage`) ═══
	void OnApplyClicked();     // 18737 «Применить» — положить звание в свой слот
	void OnDeleteClicked();    // 18738 «Очистить»  — проверка и подтверждение удаления
	void OnOkClicked();        // 18727 «ОК»        — отправить 6067
	void OnCancelClicked();    // 18728 «Отмена»    — закрыть без применения
	void OnReleaseClicked();   // 18712 «Убрать указанное звание» — обнулить оба слота
	void SelectTab(int32 Tab); // 18713, вкладки 0…2
	void SelectRow(int32 Row); // клик по строке 0…11
	void SortByColumn(int32 ColumnIdNum);   // 18729…18734, 188350

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // маски, очки, слоты, выбор, вкладка, сортировка, кулдаун
	FString DumpList() const;       // что реально стоит в 12 строках (ЧИТАЕТСЯ С ВИДЖЕТОВ)
	// ★★★ ВКЛАДКИ ПУТЁМ ИГРОКА: фактическая ЭКРАННАЯ геометрия после отрисовки, а не слот из
	//   ассета. Живой PIE-дамп по вкладкам уже врал (закон `ui-check-game-path-geometry`),
	//   поэтому проверка вида вкладок снимается только этим и только в запуске `-game`.
	void DumpTabsOnScreen(const TCHAR* Tag) const;

private:
	// ★ 25.10.2026: самотест `asda.SelfTest.InvRelog` (`Private/UI/AsdaInvRelogSelfTest.cpp`) — кладёт и читает `FlagNew`/`FlagLost`/
	//   `WornPrefix` (звания «учётки А» до перезахода); дружба с необъявленным в Shipping типом безвредна.
	friend struct FAsdaInvRelogSelfTest;
	// ── Жизненный цикл ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();
	void SetupNoticeWidget();

	// ── Данные ──
	void BuildDefs();
	const FAsdaTitleDef* FindDef(int32 TitleId) const;

	// ── Маски (★ D3: строго 0…255) ──
	/** Сколько бит маски ДЕЙСТВУЕТ сейчас: 512, либо 256 при `asda.Title.Mask512 0` (откат к 1:1). */
	static int32 EffectiveMaskBits();
	static bool  MaskGet(const uint8* Mask, int32 Bit);
	static void  MaskSet(uint8* Mask, int32 Bit, bool bOn);
	bool IsOwned(int32 TitleId) const;
	bool IsFound(int32 TitleId) const;

	// ── Список ──
	void RebuildOrder();      // порт `questLog_SortCategoryByColumn` (пересборка порядка показа)
	void RefreshList();       // порт `QuestLog_PopulateCategoryList`
	void RefreshHeader();     // порт `uiCollectionRefreshInfo` + `StatPanel_RefreshDiffs`
	void RefreshInfoPanel();  // порт `ItemTooltip_RefreshStats`
	void ClearInfoPanel();    // порт `questUiWnd_clearTextFields`
	void RefreshSelfTitles(); // порт `uiFillLocalPlayerInfo`
	void LayoutScrollBar();
	void RefreshTabState();      // кадры вкладок 18713 (общая точка `AsdaUi::ApplyTabState`)
	/** Снять стиль узла ИЗ АССЕТА (один раз) и показать в нём СЫРОЙ текст с тегами. */
	static void CaptureRich(FAsdaTitleRichSlot& S, UWidget* Node);
	static void SetRich(FAsdaTitleRichSlot& S, const FString& Raw);
	/** Текст узла независимо от того, `UTextBlock` он ещё или уже `URichTextBlock`. */
	static FString NodeText(const UWidget* W);
	/** Ширина/высота canvas-слота узла ИЗ АССЕТА (для фиксированного переноса). */
	static FVector2D SlotSize(const UWidget* W);
	/** Смещение узла в координатах КОРНЕВОЙ канвы: сумма canvas-слотов вверх по дереву. */
	static FVector2D OffsetInRoot(const UWidget* W);
	void UpdateBlink();       // порт `uiQuickbarUpdateBlink`

	// ── Ввод ──
	void TickMouse();
	int32 RowUnderCursor() const;
	int32 ColumnHeaderUnderCursor() const;

	// ── Нотисы ──
	FString Loc(int32 Id) const;
	FString LocTagged(int32 Id) const;   // ★ Дедуп №2, В1-B (U10a): было `LocRaw` — по смыслу это `AsdaNotice::LocTagged` (теги сохранены, маркеры [ss1]/[sd1] не тронуты)
	// EtalonNoticeId — noticeId эталонного `CItem_BuildTooltipText(ui, 0, id, …)` для очереди окон-сообщений:
	//   2425/2428/2429/2430 эталон передаёт своим id (quest.c:8499/8783/8805/8792), 2426 — −1.
	void NoticeMsgBox(int32 LocId, int32 EtalonNoticeId = -1) const;
	void ShowHoverTip(int32 TitleLocId, int32 BodyLocId);
	void HideHoverTip();

	// ── Сеть ──
	void HandleGranted(const TArray<uint8>& B);     // 6065
	/** Общая точка разбора тела маски: старая часть на своём месте, расширение — в хвосте.
	 *  Offset — смещение старой (256-битной) части в теле пакета; хвост идёт сразу за ней. */
	static int32 ParseMaskBody(const TArray<uint8>& B, int32 Offset, uint8* OutMask, bool& bOutExtended);
	void HandleMaskOwned(const TArray<uint8>& B);   // 6066
	void HandleDeleted(const TArray<uint8>& B);     // 6070
	void HandleFound(const TArray<uint8>& B);       // 6085
	void HandleMaskFound(const TArray<uint8>& B);   // 6086
	void HandleStatBatch(const TArray<uint8>& B);   // 2255
	void HandleSlots(int32 UserIdx, int32 T0, int32 T1, int32 Rank);   // 6068
	/** Порт `sub_E9A4B0@0xE9A4B0`: зажечь значок HUD. IconKind 0 = «потеряно», 1 = «найдено»;
	 *  TipKind 0…3 → нотисы 20500…20503. */
	void ShowNoticeIcon(int32 IconKind, int32 TipKind);

	/** ★★★ КЛИК И НАВЕДЕНИЕ НА ЗНАЧОК HUD 50000. Зовётся из `Tick` ВНЕ гейта `bOpen` — значок
	 *  кликают именно при закрытом окне. Порт `questLog_OnUiEvent` case 0x8 (подсказка 20504 +
	 *  20500…20503) и case 0xB (открыть окно тогглом + БЕЗУСЛОВНО погасить значок).
	 *  Маркеры строк «новое»/«потеряно» НЕ трогает: в эталоне их гасит только клик ПО СТРОКЕ. */
	void TickNoticeIcon();

	/** ДЕЙСТВИЕ клика по значку (открыть окно тогглом + погасить значок), отдельно от ПОПАДАНИЯ.
	 *  Зовут двое: `TickNoticeIcon` после проверки курсора и `asda.Title.Click notice`. */
	void OnNoticeIconClicked();

	/** ★★★ ДВА ЭФФЕКТА ЗВАНИЯ НА ПЕРСОНАЖЕ (2026-08-30). Общая точка для 6065 и 6085: оба
	 *  хендлера эталона спавнят эффект ОДИНАКОВО и оба — ДО гейта «свой игрок», поэтому эффект
	 *  видно и над чужим персонажем.
	 *    `bFound == true`  → 6085 «секрет звания открыт»: `ex_mark_003.jeb` — ВОСКЛИЦАТЕЛЬНЫЙ ЗНАК
	 *                        (меш `ex_mark.jem`), ~2.1 с (`netHandler_OnGuildTitleResult@0xE9A850`).
	 *    `bFound == false` → 6065 «звание получено»: `0018.jeb` — ШАПКА ВЫПУСКНИКА
	 *                        (меши `cap_01.jem`/`cap_02.jem`), 5.0 с (`uiTitle_OnJobChangeResult@0xE9A9F0`).
	 *  Привязка у обоих — attach-6 (полная мировая матрица персонажа). ЗВУКА В ОРИГИНАЛЕ НЕТ.
	 *  Откат: `asda.Title.CharFx 0`. */
	void SpawnTitleEffect(int32 UserIdx, bool bFound);

	// ── ★★★ УЛЬТИМАТИВНОЕ СНЯТИЕ ЗВАНИЯ (см. блок «D8» в шапке файла) ──
	/** ОБЩАЯ ТОЧКА утраты званий. Зовут ОБА пути: диффер маски 6066 и «потеряно» из 6065.
	 *  Работает при ЛЮБОМ состоянии окна: маркеры, значок HUD, снятие надетого, плашка, нотис.
	 *  `Src` — откуда пришла утрата (для лога: «6066» / «6065» / «тест»). */
	void ApplyTitleLoss(const TArray<int32>& LostIds, const TCHAR* Src);
	/** Утраченное звание было надето → снять слот локально и протолкнуть в плашку над головой.
	 *  Возвращает true, если хоть один слот действительно очистился. */
	bool UnwearLostTitles(const TArray<int32>& LostIds);
	/** Протолкнуть текущие `WornPrefix`/`WornSuffix`/`FameRank` в СВОЮ плашку над головой
	 *  (тем же путём, что и приход 6068 — общая точка `UAsdaWorldSubsystem::ApplyCharTitleSlots`). */
	void PushSelfTitlesToNameplate();
	/** Нотис «звание утрачено» в СИСТЕМНЫЙ ЧАТ (категория Progress). Строка — 20502 из таблицы.
	 *  ★ Имя утраченного звания в этой точке ДОСТУПНО (`FindDef(Id)->Name`) — 6070 несёт titleId,
	 *  диффер 6066 даёт id напрямую. Подставить его в текст = одна строка, как только владелец
	 *  утвердит свою строку с `[ss1]`; до решения ничего не выдумываем (`never-invent-only-original`). */
	void NoticeTitleLost(const TArray<int32>& LostIds);
	/** Собрать тело 6066 («получено») либо 6086 («найдено») из ТЕКУЩЕЙ маски — для теста
	 *  снятия без сервера. Раскладка та же, что собирает `asda.Title.Sim owned/found`. */
	TArray<uint8> BuildMaskBody(bool bOwned, bool bExtBody) const;

	/** ★ Тело маски (6066 или 6086) с ДОПОЛНИТЕЛЬНО взведённым битом `Bit` — для самотеста
	 *  «пришла ТОЛЬКО маска, опкода не было»: приёмник обязан увидеть переход 0→1, обновить
	 *  данные и НЕ зажечь ни значок, ни эффект. Собственную маску подсистемы не меняет. */
	TArray<uint8> BuildMaskBodyForTest(bool bOwned, int32 Bit);

	// ═══ ВИДЖЕТЫ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> NoticeWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> RootCanvas = nullptr;

	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose = nullptr;    // 18703
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnApply = nullptr;    // 18737
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnDelete = nullptr;   // 18738
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOk = nullptr;       // 18727
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel = nullptr;   // 18728
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnRelease = nullptr;  // 18712
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TabButtons;       // 18713 сегменты 0…2
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     TabPlanes;    // 18713 планы кадров 0…2
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TabCaptions;  // `_Text` / `_Text2` / `_Text3`
	// Кэши «как было в ассете» для `AsdaUi::ApplyTabState` (снимаются РОВНО ОДИН раз).
	TArray<FSlateBrush>  TabPlaneIdle;
	TArray<FButtonStyle> TabBtnIdle;
	TArray<FLinearColor> TabCaptionIdle;

	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtRanking = nullptr;      // 18704
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTotalPoint = nullptr;   // 18705
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTotalRate = nullptr;    // 18706
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtRankDelta = nullptr;    // 188480
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPointDelta = nullptr;   // 188510
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtRateDelta = nullptr;    // 188540
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> DeltaArrows;          // 6 стрелок
	FAsdaTitleRichSlot RsFirstTitle;    // 18708 — имя звания приходит с тегом цвета грейда
	FAsdaTitleRichSlot RsSecondTitle;   // 18710
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCharName = nullptr;     // 18711
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTabRate = nullptr;      // 18714
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLeftTimeCap = nullptr;  // 18725
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLeftTime = nullptr;     // 18726

	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfoCap = nullptr;      // 18715
	FAsdaTitleRichSlot RsInfoName;      // 18716
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfoGrade = nullptr;    // 18717
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfoPoint = nullptr;    // 18718
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfoLocation = nullptr; // 18724
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfoRandom = nullptr;   // 188580
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfoType = nullptr;     // 188570
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfoTypeName = nullptr; // 188590
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtInfoCondition = nullptr;// 18723
	TArray<FAsdaTitleRichSlot> RsInfoOption;   // 18840…18845 (значения красятся +/−)

	// Заголовки колонок: [0] №, [1] Категория, [2] Имя, [3] Позиц., [4] Ранг, [5] Очки, [6] Состояние
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ColumnCaptions;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    ColumnZones;

	/** Один ряд списка: контейнер (для попадания курсора) + 7 текстов + 4 картинки-маркера. */
	struct FRowWidgets
	{
		UWidget*    Group = nullptr;      // `Title_ListBoxNN_grp` — ЖИВАЯ геометрия ряда
		// № · Категория · Имя · Позиц. · Ранг · Очки · Состояние. ВСЕ семь — rich-слоты: четыре из
		//   них эталон печатает с тегом `<FC=..>`, а держать колонки разного типа значило бы
		//   заводить две ветки показа на один ряд.
		FAsdaTitleRichSlot Cells[7];
		UWidget*    Sel = nullptr;        // `Title_ListBox_SelNN_*` — подсветка наведения/выбора
		UWidget*    Find = nullptr;       // `Title_List_FindNN_*`   — маркер «новое»
		UWidget*    Lose = nullptr;       // `Title_List_LoseNN_*`   — маркер «потеряно»
		UWidget*    Bar = nullptr;        // `Title_NoticeBarNN_*` / `Title_NoticeBar0NN_*`
	};
	TArray<FRowWidgets> Rows;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RowPlanes;   // всё, что гасим при ResetWidget
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SelectedFrame;  // 18736 (8 планов рамки)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> FramePlanes;    // 9 планов окна 18700
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// Значок HUD 50000/50001/50002
	UPROPERTY(Transient) TObjectPtr<UWidget> NoticeBg = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget> NoticeLose = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget> NoticeFind = nullptr;

	UPROPERTY(Transient) FAsdaScrollBarNomal ScrollBar;   // 18735
	FVector2D ScrollBarOrigin = FVector2D::ZeroVector;   // куда её реально поставили (для дампа)
	float     ScrollBarHeight = 0.f;

	// ═══ СОСТОЯНИЕ ═══
	UPROPERTY(Transient) TArray<FAsdaTitleDef> Defs;      // по возрастанию Id
	TMap<int32, int32>   DefIndexById;
	TMap<int32, FString> OptionNames;                     // `DT_title_option`: id → имя стата
	int32 MaxTitleId = 0;                                 // `g_nDataRecIdMax` (знаменатель % — D7)

	uint8 MaskOwned[kMaskBytes] = {};                     // маска 1 «получено» (512 бит)
	uint8 MaskFound[kMaskBytes] = {};                     // маска 2 «найдено»  (512 бит)
	TMap<int32, TArray<int16>> TitleStats;                // titleId → 18 i16 (6 троек)
	TSet<int32> FlagNew;                                  // мигает маркер «новое» (+12 эталона)
	TSet<int32> FlagLost;                                 // мигает маркер «потеряно» (+13 эталона)

	// ★ Приходила ли уже хоть одна маска 6066. Нужна ТОЛЬКО для лога: первая маска ложится
	//   поверх нулей, поэтому «утрат» в ней быть не может и диффер её не путает.
	bool  bMaskOwnedSeen = false;
	// ★ 18.10.2026: первая 6066 после СМЕНЫ КАРТЫ — сравнивать с маской прошлой карты только младшие 256 бит (их короткое
	//   тело P21 несёт верно; старшие придут второй, полной 6066). Так настоящая утрата без 6070/6065 через портал не теряется.
	bool  bCompareLowAfterMapChange = false;
	// ★ Сколько раз нотис об утрате реально ушёл в чат. Нужен ТОЛЬКО проверке `RevokeTest`:
	//   идемпотентность доказывается тем, что при полном контракте (6070 + 6066 об одной утрате)
	//   счётчик прирастает на ОДИН, а не на два.
	int32 LossNoticeCount = 0;
	int32 FamePoints = 0;      // `this+204` — «Всего: [ss1]»
	int32 FameRank   = 0;      // `this+203` — «Ранг: [sd1]»
	int32 WornPrefix = -1;     // `this+844` — надетая приставка (пришла в 6068)
	int32 WornSuffix = -1;     // `this+846` — надетое окончание
	int32 SelPrefix  = -1;     // `this+840` — выбор игрока, уходит в 6067
	int32 SelSuffix  = -1;     // `this+842`
	int32 PendingDeleteId = -1;// `this+836` — что уйдёт в 6069 после подтверждения 2427

	int32 Tab = 0;             // `this+201` — вкладка 0…2
	int32 ScrollTop = 0;       // `this+208`
	int32 SelectedRow = -1;    // `this+202`
	int32 SortColumn = 188350; // последняя колонка сортировки
	bool  bSortDesc = false;   // `this+896`
	TArray<int32> Order;       // id званий текущей вкладки в порядке показа
	TArray<int32> RowTitleId;  // titleId, лежащий в каждой из 12 строк (−1 = пусто)
	// ★ Знаменатель «Собрано %» вкладки — эталонный `totalCount` из `QuestLog_PopulateCategoryList`:
	//   ВСЕ записи вкладки, прошедшие гейты Group/Enabled/ClassReq, включая НЕполученные.
	//   Считается в `RebuildOrder`, потому что `Order` содержит только ПОКАЗЫВАЕМЫЕ строки.
	int32 TabTotalCount = 0;   // эталон: `totalCount` (локальная переменная 0xE98C00)
	int32 TabOwnedCount = 0;   // эталон: `completedCount` (там же)
	float TabRatePercent = 0.f;// «Собрано %» по текущей вкладке (2408)
	float TotalRatePercent = 0.f;

	int32 CooldownMs = 0;      // `this+207`
	double LastCooldownTickS = 0.0;
	double LastBlinkS = 0.0;
	bool   bBlinkPhase = false;

	bool bOpen = false;
	bool bControlsReady = false;
	bool bNoticeActive = false;   // `this+880`
	int32 NoticeIconKind = 1;     // `this+223`
	int32 NoticeTipKind  = 0;     // `this+222`
	bool bTipShown = false;
	int32 HoverRow = -1;

	FAsdaMouseFrame Mouse;
	// Закон `no-reentrant-net-callbacks`: колбэки только КОПЯТ тела, разбор — в Tick.
	TArray<TArray<uint8>> QGranted, QMaskOwned, QDeleted, QFound, QMaskFound, QStatBatch;
	struct FSlotsMsg { int32 UserIdx = 0, T0 = -1, T1 = -1, Rank = 0; };
	TArray<FSlotsMsg> QSlots;
	FDelegateHandle HGranted, HMaskOwned, HDeleted, HFound, HMaskFound, HStatBatch, HSlots;
	TArray<IConsoleObject*> ConsoleCmds;
};
