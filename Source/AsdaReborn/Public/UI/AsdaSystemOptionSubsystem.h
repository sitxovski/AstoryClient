// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSystemOptionSubsystem — ДВА ОКНА ГРУППЫ «System» одного эталонного класса:
//   • ESC-меню            `SYSTEM_MenuWIN_NEW`  (JUI IDNum **69400**, `gamemain_jui.ini:325500`);
//   • окно настроек       `SYSTEM_Option_NEW`   (JUI IDNum **69500**, `gamemain_jui.ini:325886`).
//
// ═══════════════════ ПОЧЕМУ ОДНА ПОДСИСТЕМА НА ДВА ОКНА ════════════════════════════════════════
//   Потому что в оригинале это ОДИН объект. `uiNoticeDialogInitControls@0xED3080` (ui_core.c:88077)
//   первой же строкой кладёт в `this[0]` контрол **69400**, а в `this[1]` — контрол **69500**:
//       *(_DWORD *)this->filler      = CWindowMgr_FindControlByID(g_pWindowMgr, 69400);
//       *(_DWORD *)&this->filler[4]  = CWindowMgr_FindControlByID(g_pWindowMgr, 69500);
//   Оба окна обслуживает одна оконная процедура `uiOptionsWindow_WndProc@0xED4840` — диспетчер
//   верхнего уровня (ui_core.c:34844) отдаёт ей ВЕСЬ диапазон id `0x10F18…0x113C7` (69400…70599).
//   Разводить их по двум подсистемам значило бы разорвать общее состояние (захват клавиши, буфер
//   настроек, откат по «Отмена») на два владельца — ровно тот рассинхрон, от которого предостерегает
//   закон `check-existing-before-creating`.
//
// ═══════════════════ ТРИ ФУНКЦИИ-ОБЁРТКИ, КОТОРЫЕ НАДО ЗНАТЬ ═══════════════════════════════════
//   Всё управление видимостью обоих окон в эталоне идёт через тонкие обёртки над `this[0]`/`this[1]`
//   (снято ВТОРЫМ методом — прямой декомпил `AsdaStory_ida.c`, закон `false-negatives-method`):
//       `sub_93C2AD` → `sub_ED5760` :  if (this[0]) CControl_SetVisible(this[0], 0);   // СКРЫТЬ 69400
//       `sub_934C33` → `sub_ED5730` :  if (this[0]) CControl_SetVisible(this[0], 1);   // ПОКАЗАТЬ 69400
//       `sub_925247` → `sub_E8FAE0` :  return *(uint8*)(this[0] + 292);                // 69400 ВИДНО?
//       `keybindPageShowAndRefresh@0xED5790` : CControl_SetVisible(this[1], 1)         // ПОКАЗАТЬ 69500
//                                              + сброс флага «нужен рестарт» (+1228)
//                                              + qmemcpy(+1158 ← +1104, 0x36) = СНИМОК настроек для «Отмены».
//
// ═══════════════════ ★★★ ПОПРАВКА К РАЗБОРУ: КНОПКА 73406 ОТКРЫВАЕТ 69400, А НЕ 69500 ══════════
//   В `Docs/UI/System/SYSTEM_MenuWIN_NEW.md` и в таблице кнопок панели меню было записано, что
//   кнопка главного меню **73406** (`NEW_SYSTEMMAIN_SYSINFOBUT`) открывает окно настроек 69500.
//   Это НЕВЕРНО. Оба диспетчера кнопки — `uiHotkey_DispatchCommand@0xE8F670` (ui_core.c:79495) и
//   `uiMainMenu_OnCommand@0xE8F880` (ui_core.c:79553) — на `case 73406` уходят в
//   `uiPanelToggleOrClearTabs@0xE8FA70` (ui_core.c:79578):
//       if (sub_925247(g_pOptionsWnd) == 1) { sub_93C2AD(g_pOptionsWnd); … }   // видно → СКРЫТЬ
//       else                                { sub_934C33(g_pOptionsWnd);   }   // нет   → ПОКАЗАТЬ
//   а обе обёртки работают с `this[0]` = **69400**. Окно настроек 69500 открывается ТОЛЬКО кнопкой
//   `SYSTEM_MENU_btn3` (69403) внутри ESC-меню. Таблица в `AsdaSystemMenuSubsystem.cpp` исправлена.
//
// ═══════════════════ ESC-МЕНЮ 69400: ЧТО ДЕЛАЮТ КНОПКИ ═════════════════════════════════════════
//   Все команды идут через ОДИН диспетчер `optWndDispatchCommand@0xED4C50` (ui_core.c:88608),
//   сравнение по idnum контрола в hex:
//     0x10F19 **69401** `SYSTEM_MENU_btn1` (скин `GAME_X_btn`)        — ВЫЙТИ ИЗ ИГРЫ:
//         `CItem_BuildTooltipText(g_pItemTooltipUI, 3, -1, nullptr, 1)` + `sub_93C2AD(this)`.
//     0x10F1A **69402** `SYSTEM_MENU_btn2` (скин `GAME_restart_btn`)  — К ВЫБОРУ ПЕРСОНАЖА:
//         `CItem_BuildTooltipText(g_pItemTooltipUI, 4, -1, nullptr, 1)` + `sub_93C2AD(this)`.
//     0x10F1B **69403** `SYSTEM_MENU_btn3` (скин `GAMEOP_btn`)        — НАСТРОЙКИ:
//         `sub_93C2AD(this)` + `keybindPageShowAndRefresh(this)`.
//     0x10F1C **69404** `SYSTEM_MENU_btn4` (скин `GAMEOUT_btn`)       — ПРОСТО ЗАКРЫТЬ (общая ветка
//     0x10F2B **69419** `SYSTEM_MENU_Xbtn` (скин `CloseBtn_18`)         с крестиком): `sub_93C2AD(this)`.
//   Сетевых опкодов ESC-меню НЕ шлёт. `WM_CLOSE` окна (`uiOptionsWindow_WndProc` case 0x15, a3==69400)
//   тоже просто прячет окно, а `WM_CLOSE` ВСЕГО ПРИЛОЖЕНИЯ при `g_GameState == 1` (game_main.c:6169)
//   вызывает ту же модалку типа 3, что и btn1.
//
//   ★ Что именно делают «да» в модалках (ui_core.c:57077 и :57203, ветка кнопки 1523 msgbox'а):
//     тип 3 → `gameReturnToCharSelectTimer@0xED2D90` (game_main.c:12108): сохранение списка сундука,
//             отправка раскладок хотбара, `g_GameState = 0` ⇒ главный цикл (`game_main.c:1899`)
//             возвращает 0 и ПРИЛОЖЕНИЕ ЗАКРЫВАЕТСЯ. Это ВЫХОД ИЗ ИГРЫ.
//     тип 4 → `gameLeaveToCharSelect@0xED2E10` (game_main.c:12125): тот же сейв + полный сброс
//             панелей/пати/кампвара/инвентаря, `g_bReloadGameMainJUI = 1`, `g_GameState = 14`
//             ⇒ ВОЗВРАТ К ВЫБОРУ ПЕРСОНАЖА.
//   Тексты модалок (`CItem_BuildTooltipText@0xD6E790`, item_inventory.c:51593/51618 — имя ВРЁТ, это
//   построитель системного msgbox, а не тултип предмета):
//     тип 3 → текст loc **2100** «Выйти из игры?», кнопки loc 2288 / 2289;
//             ★ ГЕЙТ: если `objStateAllowsRender(g_InventoryBag.pLocalPlayerObj)` (мисномер: «персонаж
//             в боевом/занятом состоянии»), то вместо модалки — ОДНОКНОПОЧНЫЙ нотис loc **108106**
//             «Во время боя выход из игры невозможен.» и выход;
//     тип 4 → текст loc **2102** «Перезапустить игру?», кнопки loc 2288 / 2289 (гейта боя НЕТ).
//
// ═══════════════════ ОКНО НАСТРОЕК 69500: ТРИ ВКЛАДКИ ══════════════════════════════════════════
//   ★★★ ПОПРАВКА К РАЗБОРУ `SYSTEM_Option_NEW.md`: порядок вкладок там записан наоборот.
//   `uiTabWindowSelectTab@0xED5910` (ui_core.c:88934) прячет три корневых контейнера и показывает один:
//       case 0 → `uiTabWindowShowPage(this, 0)`   → filler[24]  = **70150** `SYSTEM_Op_INT`     — ИНТЕРФЕЙС
//       case 1 → `uiTabWindowShowPage3(this, 0)`  → pThis[51]   = **70000** `SYSTEM_Op_SYSTEM`  — СИСТЕМА
//       case 2 → `uiTabWindowSelectPage(this, 0)` → filler[352] = **69510** `SYSTEM_Op_HOTKEYS` — КЛАВИШИ
//   Подтверждено и локализацией: пункты переключателя 69502 — loc 30000 «Настройки игры»,
//   30001 «Настройки системы», 30002 «Горячие клавиши» (`uiNoticeDialogInitControls`, ui_core.c:88084).
//
//   ВКЛАДКА 0 «ИНТЕРФЕЙС» (70150). Список под-страниц **70156** (loc 30400/30401/30402), три панели:
//     70170 `SYSTEM_Op_INT_1` — 7 строк, подписи loc 30421…30427, чекбоксы 70172,70174,…,70184
//                               (`uiOptions_ApplyControlValue` LABEL_5, `(a2-70172)>>1` → filler[1105..1111]);
//     70250 `SYSTEM_Op_INT_2` — 3 подписи loc 30451…30453, чекбоксы 70262,70264,…,70272
//                               (`(a2-70262)>>1` → filler[1112,1113,1114,1121,1122,1123]);
//     70350 `SYSTEM_Op_INT_3` — 10 подписей loc 30471…30480, чекбоксы 70362,70364,…,70380
//                               (`(a2-70362)>>1` → filler[1115..1120,1145..1148]).
//   ВКЛАДКА 1 «СИСТЕМА» (70000). Список под-страниц **70006** (loc 30501/30502/30503), три панели эталона
//   (★ фаза 2: + «Качество» и «Эффекты», наши строки 990300/990301 — см. раздел ФАЗА 2 ниже):
//     70010 звук   — подписи loc 30521/30522 (70011/70014), чекбоксы 70012/70015, полосы 70013/70016
//                    (`uiScrollbarSetRange(…, 0, 256)`); значения принимает `uiOptionsWindow_WndProc`
//                    (0x1117D громкость музыки → `CSoundMgr::SetVolume`, 0x11180 → `SetReverb`),
//                    а сами галочки — `uiOptions_ApplyControlValue` (0x1117C → filler[1128] = «a3 == 0»,
//                    0x1117F → filler[1129] = «a3 == 0») ⇒ ★ В БУФЕРЕ ХРАНИТСЯ ИНВЕРСИЯ = «ЗАГЛУШЕНО»;
//     70050 экран  — подписи loc 30541/30542/30543 (70051/70054/70057), комбо разрешения 70053,
//                    чекбокс полноэкранного 70055 (0x111A7 → filler[1132]), полоса гаммы 70058
//                    (`uiScrollbarSetRange(…, 1, 100)`, значение → `D3D_SetGammaBrightnessRamp`);
//     70100 3D     — 9 подписей loc 30561…30569 (70101,70103,…,70117) и 9 чекбоксов 70102,70104,…,70118
//                    (`(a2-70102)>>1` → filler[1136..1144]); подпись loc 30570 (70119) и комбо 70121
//                    с пятью пунктами loc 12190…12194 («Нет/Персонаж/Монстр/Персонаж+монстр/Все»).
//                    ★ У ИНДЕКСА 1 особая ветка: он включается, ТОЛЬКО если `sub_924879(g_pRenderer) & 2`
//                    (возможности устройства), и тогда же взводит бит `g_dwSystemOptionFlags |= 0x40`.
//   ВКЛАДКА 2 «КЛАВИШИ» (69510). Список под-страниц **69517** (loc 30011…30014), четыре панели:
//     69648 INTER (действия 1…47), 69650 PARTY (48…65), 69750 CHAT (66…73), 69850 FEEL (74…84).
//
// ═══════════════════ ★★★ КОНТРОЛ ПОЛЯ КЛАВИШИ → КОД ДЕЙСТВИЯ ═══════════════════════════════════
//   Таблица живёт в функции с совершенно посторонним именем `fontMapCodepointToGlyphIndex@0xED6160`
//   (font_text.c:477) — очередной мисномер (закон `false-negatives-method`: искать по СМЫСЛУ, а не
//   по имени). Она переводит idnum поля привязки в код действия 1…84:
//       69530…69534 → 1…5      69571…69582 → 6…17     69584…69595 → 18…29    69621…69638 → 30…47
//       69671…69673 → 48…50    69711…69725 → 51…65    69771…69778 → 66…73    69891…69901 → 74…84
//   Итого 84 поля. Действие **85** в окне не выведено вовсе, а действие **66** отсекается первой же
//   строкой `SkillTab_Select@0xED5E20` (`if (a2 != 66)`) ⇒ ПЕРЕНАЗНАЧИТЬ ЕГО НЕЛЬЗЯ (дефект/замысел
//   оригинала — воспроизводим как есть, ключ `asda.SysOpt.LegacyNoRebind66`).
//   ★ В ассете порта эти 84 поля лежат ПОДРЯД и в том же порядке — контейнеры `PICK_BACK_grp`,
//     `PICK_BACK_grp_1` … `PICK_BACK_grp_83` (проверено headless-дампом дерева WBP, 802 виджета),
//     поэтому индекс контейнера + 1 == код действия. Подписи строк и текст клавиши лежат внутри:
//     `<...>_TXTnn` — название действия, `PICK_BACK_<jui>_txt` — текущая клавиша.
//
// ═══════════════════ ЗАХВАТ КЛАВИШИ ════════════════════════════════════════════════════════════
//   `SkillTab_Select@0xED5E20` (skill_buff.c:37630) — снова мисномер, это «начать захват клавиши»:
//     • повторный клик по уже выбранной строке = отмена (`uiWindowResetSelection@0xED60E0`);
//     • иначе: запомнить код действия (`this[275]`), переставить оверлей **69910** `SYSTEM_CLICK`
//       на строку (`CControl_SetField74_75(this[89], X, Y - 19*scroll)`), показать его,
//       `sub_92E7FC(g_pKeybindMgr, action, 1)` = включить режим захвата, нотис **11602**
//       «Выбери клавишу.» в чат красным (−65536, TTL 10000) и флаг `this+1096 = 1`.
//   Приём клавиши — `Input_OnKeyDown_Dispatch@0xE3C7D0` (input.c:3905): VK 27 (ESC) и 229 в захват
//   не попадают ВООБЩЕ; клавиша нормализуется `Keybind_NormalizeVK`, ищется в таблице 74 имён —
//   нет в таблице ⇒ нотис **11603** «Эту клавишу назначить нельзя. Выбери другую.»; иначе
//   `Keybind_SetBind` + `keybindWndRefreshAll` + нотис **11604** (клавиша была свободна) либо
//   **11605** (клавиша была занята другим действием и снята с него).
//   Кнопки страницы: **69651** `SYSTEM_default` (loc 11600 «По умолчанию») → `keybind_loadDefaults(mgr, 1)`
//   + перерисовка; **69652** `SYSTEM_cancel` (loc 11601) → только в режиме захвата: снять привязку
//   выбранного действия (`Keybind_SetBind(mgr, this[275], 0, 0)`) + перерисовка.
//
// ═══════════════════ ПРИМЕНИТЬ / ОК / ОТМЕНА ═══════════════════════════════════════════════════
//   `optWndDispatchCommand@0xED4C50`:
//     0x11329 **70441** `SYSTEM_Option_APP` (loc 2377) — ПРИМЕНИТЬ, окно НЕ закрывается:
//         `keybindSaveShortcutIni` → `hotkeyBar_RebuildAndRefresh` → `optApplySettingsFromConfig`
//         → `keybindSaveShortcutIni` (второй раз — так в оригинале).
//     0x1132A **70442** `SYSTEM_Option_OK` (loc 2288) — ОК:
//         `keybindSaveShortcutIni` → `hotkeyBar_RebuildAndRefresh` → `optionsWnd_ApplyAndSave(…, 1)`.
//     70443 `SYSTEM_Option_NO` (loc 2289) и 70599 `SYSTEM_Option_NEW_XX` — ОТМЕНА/КРЕСТИК:
//         `optionsWnd_ApplyAndSave(…, 0)`.
//   `optionsWnd_ApplyAndSave@0xED5800` (game_main.c:12156):
//       CControl_SetVisible(this[1], 0);                       // спрятать 69500
//       if (this[1096] == 1) return uiWindowResetSelection();  // ★ ИДЁТ ЗАХВАТ КЛАВИШИ — ТОЛЬКО
//                                                              //   отменить его, окно НЕ закрывать
//       if (apply) { optApplySettingsFromConfig(); keybindSaveShortcutIni(); Config_WriteSettings("system\\config.txt"); }
//       else       { sub_92F0DF(mgr); qmemcpy(+1104 ← +1158, 0x36); optionsWnd_applySettingsToControls(); }
//   ⇒ «Отмена» ВОССТАНАВЛИВАЕТ снимок буфера, снятый при ОТКРЫТИИ окна (см. `keybindPageShowAndRefresh`).
//
// ═══════════════════ СЕТЬ: ОПЦИИ ШЛЮТ ПАКЕТ 4070 ══════════════════════════════════════════════
//   `optApplySettingsFromConfig@0xED1F10` в конце зовёт `uiOptionsSendSettingsPacket2@0xED69E0`
//   (ui_core.c:89291): тело 19 байт, конверт `[0..15] MD5(payload) + [16..34] payload`, опкод **4070**,
//   длина 35 — это ровно порт-конверт `UAsdaNetSubsystem::SendMd5Signed`. Раскладка тела:
//       [0..4] = 1, [5] = filler[1110]  (INT_1 №5 «Услышанный шепот»),
//       [6] = filler[1104]  ★ БАЙТ, КОТОРОГО НИКТО НЕ ПИШЕТ (см. «дефекты» ниже),
//       [7] = filler[1105], [8] = filler[1111], [9] = filler[1106], [10] = filler[1107],
//       [11] = filler[1108], [12] = filler[1109], [13] = filler[1112], [14] = filler[1113],
//       [15] = filler[1116], [16..18] = 0.
//   Обратная сторона: сервер присылает те же флаги в пакете входа в мир, их раскладывает
//   `optionsApplyServerFlags@0xED1B40` (game_main.c:11915, вызов network.c:34302).
//
// ═══════════════════ ДЕФЕКТЫ ОРИГИНАЛА (закон `fix-original-defects`) ══════════════════════════
//   Д1 `uiPanelToggleOrClearTabs` РАЗРЕШАЕТ кнопки трея главного меню при ЗАКРЫТИИ ESC-меню
//      (`uiPanelClearAllTabFlags`), но при ОТКРЫТИИ их никто не запрещает — асимметрия.
//      Воспроизводим как есть; ключ `asda.SysOpt.LegacyMenuAsym 1` (по умолчанию 1 = как оригинал).
//   Д2 Действие **66** нельзя переназначить: `SkillTab_Select` отсекает его первой строкой, хотя оно
//      попадает в диапазон полей 66…73 и поле на странице «Чат» есть. `asda.SysOpt.LegacyNoRebind66`.
//   Д3 `uiOptions_ApplyControlValue` принимает ШЕСТЬ значений вкладки «Интерфейс-2» (70262…70272),
//      а `uiDialogInitControls` заводит подписи только ТРЁМ (loc 30451…30453). У чекбоксов
//      70268/70270/70272 подписи нет ⇒ игроку они бессмысленны. В ассете порта чекбоксов 8
//      (CHK00…CHK07): два последних (70274/70276) обработчика не имеют вовсе. Порт: показываем
//      ровно те строки, которым эталон даёт подпись; остальные гасим.
//   Д4 То же во «Интерфейс-3»: эталон заводит 10, в ассете 11 (CHK10 = 70382 без обработчика).
//   Д5 В теле пакета 4070 байт [6] читается из `filler[1104]`, в который НИ ОДНА ветка
//      `uiOptions_ApplyControlValue` не пишет ⇒ на провод уходит то, что осталось в памяти.
//      Порт шлёт там 0 (детерминированно); повторить «как повезёт» невозможно в принципе, поэтому
//      ключа отката нет, а расхождение задокументировано здесь.
//
// ═══════════════════ ЧЕГО В ЭТОМ ОКНЕ НЕТ ══════════════════════════════════════════════════════
//   • ЦИФРОВОГО ВВОДА НЕТ: окно `NUMPAD_WIN` (1410) к настройкам не подключается ни разу — в полном
//     перечне вызовов `uiOpenContextPopupWithHandlers@0xDA55A0` (`Docs/Port/systems/NUMPAD_FLOW.md` §3)
//     точек из `ui_core.c:88…89xxx` нет. Закон `check-numpad-on-new-system` соблюдён проверкой.
//   • Своей разметки мы не рисуем: оба WBP уже есть в `Content/AsdaAssets/UI/Player_UI/System/`,
//     перегенерация НЕ запускалась (законы `no-ui-regeneration-without-permission`,
//     `no-runtime-widget-fixes`). Единственное, что подсистема двигает, — оверлей `SYSTEM_CLICK`
//     и содержимое прокручиваемой страницы клавиш: ровно это двигает и оригинал
//     (`CControl_SetField74_75`, `uiCtrl_SetChildScrollbarPos`).
//
// ═══════════════════ ★★★ ФАЗА 2 (19.09.2026): ЧТО ДОБАВЛЕНО СВЕРХ ОРИГИНАЛА И ПОЧЕМУ ═══════════════
//   По решению владельца (план `opt_plan/PLAN.md`, решения №1–№5) окно получило настройки UE, вынесенные из
//   dev-окна Ctrl+N. У «Настроек системы» ПЯТЬ под-страниц: Звук / Экран / 3D-графика / ★Качество / ★Эффекты.
//   • Разметка новых строк — В АССЕТЕ (клоны строк этого же окна, шаг `Tools/asda_ui_option_extra_rows.py`);
//     код ставит подписи (loc) и состояние. Таблица строк — `SysOptExtra` в .cpp.
//   • «Полный экран» (70055) заменён списком «Режим экрана» (Полный экран / Без рамки / Окно).
//   • Хранилище всех новых строк — профиль `UAsdaGameUserSettings`; в игру и на диск уходит ТОЛЬКО
//     сменённое в окне (сравнение буфера со снимком при открытии) — калибровка владельца не сбивается.
//   • «Свечение» 70121 — один источник с dev-окном (`GlowLevel` профиля), пункты по эталону «на чём светится».
//   • Подписи клавиш — эталонная loc; исправления перевода — наши строки 990200+ (`SysOptLocFix`).
//   • [ИСПРАВЛЕНИЕ ОРИГИНАЛА] поле «Стойка на руках» (действие 85) на странице «Эмоции».
//   • ★ 18.10.2026 [ПОРТ] (явное разрешение владельца): строка «Цифры урона» (I1) на странице «Отображение имён» вкладки
//     «Настройки игры» — клон строки «Свечение» 3D (шаг `Tools/asda_ui_option_dmgtext_row.py`), поле профиля `DamageTextMode`.
//   • ★ 25.10.2026 [ПОРТ] (запрос владельца): строка «Присед» (I4, «Нажатие»/«Зажатие») на странице «Разное» — тот же клон
//     (шаг `Tools/asda_ui_option_crouch_row.py`), поле профиля `CrouchMode`.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"      // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Styling/SlateBrush.h"      // FSlateBrush — кадры покоя/выбора ячеек списка под-страниц
#include "AsdaSystemOptionSubsystem.generated.h"

class APlayerController;
class UUserWidget;
struct FAsdaKeyBind;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UPanelWidget;
class UAsdaButtonRelay;
class UAsdaJuiComboBox;
class UAsdaKeybindConfig;

// ── Одна строка страницы «Горячие клавиши»: контейнер `PICK_BACK_grp*` + подпись действия + текст клавиши.
USTRUCT()
struct FAsdaOptHotkeyRow
{
	GENERATED_BODY()

	// Код действия раскладки (1…84) — `fontMapCodepointToGlyphIndex@0xED6160`.
	UPROPERTY(Transient) int32 ActionId = 0;
	// Под-страница клавиш: 0 INTER (69648), 1 PARTY (69650), 2 CHAT (69750), 3 FEEL (69850).
	UPROPERTY(Transient) int32 Page = 0;
	// Контейнер строки (`PICK_BACK_grp…`) — он же зона клика (хит-тест по его прямоугольнику).
	UPROPERTY(Transient) TObjectPtr<UWidget> RowGroup = nullptr;
	// Текст текущей привязки (`PICK_BACK_<jui>_txt`) — сюда пишется «SHIFT+F1» / «NONE».
	UPROPERTY(Transient) TObjectPtr<UTextBlock> KeyText = nullptr;
	// Подпись действия (`<контейнер страницы>_TXTnn`) — эталон ставит её из loc-таблицы.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText = nullptr;
};

// ── Один чекбокс-настройка: idnum эталона, виджет в WBP, подпись.
USTRUCT()
struct FAsdaOptCheck
{
	GENERATED_BODY()

	UPROPERTY(Transient) int32 IdNum = 0;          // idnum контрола в .JUI (по нему эталон диспатчит)
	UPROPERTY(Transient) FName WidgetName;         // имя `UCheckBox` в WBP
	UPROPERTY(Transient) FName LabelName;          // имя подписи `UTextBlock` рядом
	UPROPERTY(Transient) int32 LocId = -1;         // строка подписи; −1 = эталон подписи не ставит
	UPROPERTY(Transient) TObjectPtr<UCheckBox> Box = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;
};

// ── Горизонтальная полоса-ползунок JUI (скин `SideBar_Scr`): 4 плана — жёлоб, две стрелки, бегунок.
//    Эталон: `uiScrollbarSetRange(ctrl, min, max)` + `uiScrollbarSetPos(ctrl, value)`.
USTRUCT()
struct FAsdaOptSlider
{
	GENERATED_BODY()

	UPROPERTY(Transient) int32 IdNum = 0;
	UPROPERTY(Transient) TObjectPtr<UImage> Track = nullptr;    // план 0 — жёлоб
	UPROPERTY(Transient) TObjectPtr<UImage> BtnLeft = nullptr;  // план 1 — стрелка влево
	UPROPERTY(Transient) TObjectPtr<UImage> BtnRight = nullptr; // план 2 — стрелка вправо
	UPROPERTY(Transient) TObjectPtr<UImage> Thumb = nullptr;    // план 3 — бегунок
	UPROPERTY(Transient) int32 Min = 0;
	UPROPERTY(Transient) int32 Max = 100;
	UPROPERTY(Transient) int32 Value = 0;
	// ★ Фаза 2: вкладка и под-страница, на которой полоса видна (опрашивается мышью только там).
	UPROPERTY(Transient) int32 Tab = 1;
	UPROPERTY(Transient) int32 Page = 0;
	// Прямоугольник жёлоба, снятый ОДИН раз при разборе контролов (в координатах родительского канваса).
	UPROPERTY(Transient) FVector2D TrackPos = FVector2D::ZeroVector;
	UPROPERTY(Transient) FVector2D TrackSize = FVector2D::ZeroVector;
	UPROPERTY(Transient) FVector2D ThumbSize = FVector2D::ZeroVector;
	UPROPERTY(Transient) bool bDragging = false;
};

// ── БУФЕР НАСТРОЕК = эталонные байты `COptionsWnd + 1104…1148` и слова `+562…+577`.
//    Именно он копируется целиком при открытии окна (`qmemcpy(+1158 ← +1104, 0x36)`) и
//    восстанавливается по «Отмене».
USTRUCT()
struct FAsdaOptState
{
	GENERATED_BODY()

	// Вкладка «Интерфейс», панель 1 — приём заявок (filler[1105…1111], loc 30421…30427).
	UPROPERTY(Transient) uint8 Int1[7] = { 1, 1, 1, 1, 1, 1, 1 };
	// Панель 2 — показ имён/ОЗ (filler[1112],[1113],[1114],[1121],[1122],[1123]).
	UPROPERTY(Transient) uint8 Int2[6] = { 1, 1, 1, 1, 1, 1 };
	// Панель 3 — прочее (filler[1115…1120],[1145…1148]; loc 30471…30480).
	UPROPERTY(Transient) uint8 Int3[10] = { 1, 1, 1, 1, 1, 0, 0, 1, 1, 1 };
	// Звук. ★★★ ПОПРАВКА 19.09.2026: эталонный байт `filler[1128] = (a3 == 0)` — это «музыка ВКЛЮЧЕНА»
	//   (он уходит в `CSoundStream_SetEnabled@0xFA0AC0`), а a3 = 1 приходит, когда чекбокс-динамик
	//   ОТМЕЧАЮТ (`checkBox_OnToggle`: 67073) — и отмеченный кадр скина `SpeakOnOff` рисует ПЕРЕЧЁРКНУТЫЙ
	//   динамик. Порт держит обратную величину «заглушено»: MusicMuted = 1 ⇔ байт эталона 0 ⇔ галочка стоит.
	UPROPERTY(Transient) uint8 MusicMuted = 0;     // !filler[1128]; 1 = чекбокс 70012 отмечен, музыка выключена
	UPROPERTY(Transient) uint8 SfxMuted = 0;       // !filler[1129]; 1 = чекбокс 70015 отмечен, эффекты выключены
	// ★★★ УМОЛЧАНИЯ ГРОМКОСТИ — ИЗ ЭТАЛОНА, А НЕ ИЗ ГОЛОВЫ (ревизия 2026-09-17, С1).
	//   `configInitDefaults@0x9509E0` (`RE/etalon/game_main.c:869-870`) ставит `*(this+17) = 128`
	//   и `*(this+18) = 128` — ровно посередине полосы 0…256. Соседние поля той же функции
	//   сходятся с нашими (`*(this+16) = 50` — гамма, `*(this+8) = 768`, `*(this+9) = 32`),
	//   значит нумерация слотов опознана верно. Здесь раньше стояло 180/180 — выдуманное значение.
	UPROPERTY(Transient) int32 MusicVolume = 128;  // +562, диапазон полосы 0…256
	UPROPERTY(Transient) int32 SfxVolume = 128;    // +563, диапазон полосы 0…256
	// Экран.
	// ★★★ 19.09.2026 (фаза 2, решение владельца №2): вместо галочки «Полный экран» (filler[1132],
	//   `g_bOptWindowed = (filler[1132] == 0)`) — ОДИН список «Полный экран / Без рамки / Окно»:
	//   0 = EWindowMode::Fullscreen, 1 = WindowedFullscreen, 2 = Windowed (нумерация `ApplyResolution`).
	UPROPERTY(Transient) int32 WindowMode = 1;
	UPROPERTY(Transient) int32 Gamma = 50;         // +565, диапазон полосы 1…100
	UPROPERTY(Transient) int32 ScreenW = 1024;     // +575
	UPROPERTY(Transient) int32 ScreenH = 768;      // +576
	UPROPERTY(Transient) int32 ScreenBpp = 32;     // +577
	// 3D: filler[1136…1144], подписи loc 30561…30569.
	UPROPERTY(Transient) uint8 D3[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	// Комбо «Свечение» 70121 (+567), пункты loc 12190…12194 «Нет/Персонаж/Монстр/Персонаж+монстр/Все».
	//   ★ Фаза 2: хранится в поле `GlowLevel` профиля — ОДНОМ источнике с dev-окном Ctrl+N.
	UPROPERTY(Transient) int32 GlowLevel = 4;

	// ══════ ★ ФАЗА 2 (19.09.2026): НАСТРОЙКИ, ВЫНЕСЕННЫЕ ИЗ Ctrl+N (строки «Экрана», страницы «Качество»
	//   и «Эффекты»). Снимок при открытии и сравнение со снимком по «ОК» — ровно как у 3D-флагов: в игру и
	//   в профиль уходит ТОЛЬКО то, что игрок сменил в этом окне (калибровка владельца не сбивается). ══════
	UPROPERTY(Transient) int32 RenderScale = 100;  // масштаб рендера, % (штатный ResolutionQuality профиля)
	UPROPERTY(Transient) int32 FpsLimit = 0;       // лимит кадров, 0 = без ограничения
	UPROPERTY(Transient) uint8 VSync = 0;          // вертикальная синхронизация
	UPROPERTY(Transient) uint8 RhiDx12 = 1;        // графический режим: 1 DirectX 12, 0 DirectX 11 (после перезапуска)
	// ★ [ПОРТ] 09.10.2026 (мультиязычность): язык игры, значение `EAsdaLang` (0 RU, 1 EN). Пишется в профиль по «ОК»,
	//   действует после перезапуска (`FAsdaLang`, шапка `Public/Text/AsdaLang.h`).
	UPROPERTY(Transient) uint8 Lang = 0;
	// ★ [ПОРТ] 18.10.2026 (явное разрешение владельца): режим всплывающих цифр урона — поле профиля
	//   `UAsdaGameUserSettings::DamageTextMode` (0 только касающиеся меня, 1 все, 2 не показывать; разбор у поля). Строка «Цифры
	//   урона» на странице «Отображение имён»; в профиль — по «ОК/Применить», действует сразу (без перезапуска).
	UPROPERTY(Transient) int32 DamageTextMode = 1;
	// ★ [ПОРТ] 19.10.2026 (запрос владельца): чувствительность мыши (скорость курсора) и поворота камеры, % 10…360 — поля профиля
	//   `CursorSpeedPercent` / `CameraTurnPercent`; строки I2/I3 на странице «Разное»; в профиль — по «ОК/Применить».
	UPROPERTY(Transient) int32 CursorSpeed = 100;
	UPROPERTY(Transient) int32 CameraTurn = 100;
	// ★ [ПОРТ] 25.10.2026 (запрос владельца): режим Ctrl ходьбы вприсядку — поле профиля `UAsdaGameUserSettings::CrouchMode`
	//   (0 «Нажатие», 1 «Зажатие»); строка I4 «Присед» на странице «Разное»; в профиль — по «ОК/Применить», действует сразу.
	UPROPERTY(Transient) int32 CrouchMode = 0;
	// Группы качества в порядке ключей `SysOptExtra::Q1_Tex…Q8_GI`: текстуры, тени, эффекты, постобработка,
	//   растительность, детализация вдали, отражения, глобальное освещение (уровни sg 0…4). ★ 12.10.2026: строк
	//   растительности [4], детализации [5] и ГИ [7] в окне нет — их уровни ставит только пресет (и они участвуют в
	//   решении «Своё» наравне с видимыми).
	UPROPERTY(Transient) int32 Groups[8] = { 3, 3, 3, 3, 3, 3, 3, 3 };
	// Пункт «Общее качество», выбранный игроком в ЭТОМ окне (−1 — не выбирал). Пресет ставит и скрытые
	//   группы (шейдинг, рельеф, качество сглаживания), поэтому по «ОК» он применяется целиком.
	UPROPERTY(Transient) int32 PresetPick = -1;
	UPROPERTY(Transient) int32 DrawDistance = 100; // дальность прорисовки, % (asda.Cull.Scale × 100)
	UPROPERTY(Transient) int32 AAMethod = 1;       // r.AntiAliasingMethod
	UPROPERTY(Transient) int32 SharpenX100 = 120;  // r.Tonemapper.Sharpen × 100
	// ★ 12.10.2026: Aniso/AO/LensFlare — строк в окне больше нет (сокращение 23 → 16); читаются при открытии только для сводки
	//   `asda.SysOpt.Dump`, в игру и профиль окном НЕ пишутся (поле профиля `LensFlare` и его миграция остаются как были).
	UPROPERTY(Transient) int32 Aniso = 8;          // r.MaxAnisotropy (только чтение)
	UPROPERTY(Transient) uint8 AO = 1;             // затенение окружения (только чтение)
	UPROPERTY(Transient) uint8 MotionBlur = 0;     // размытие в движении
	UPROPERTY(Transient) uint8 LensFlare = 0;      // блики объектива (только чтение)
	UPROPERTY(Transient) uint8 Clouds = 1;         // объёмные облака

	bool operator==(const FAsdaOptState& O) const;
	bool operator!=(const FAsdaOptState& O) const { return !(*this == O); }
};

// ── ★ Фаза 2: выпадающий список одной из новых строк (клон строки «Разрешение» 70051–70053 в ассете).
USTRUCT()
struct FAsdaOptExtraCombo
{
	GENERATED_BODY()

	UPROPERTY(Transient) int32 Key = 0;                              // ключ строки (`SysOptExtra::EKey`)
	UPROPERTY(Transient) int32 Tab = 1;                              // вкладка и под-страница, где строка видна
	UPROPERTY(Transient) int32 Page = 0;
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> Combo = nullptr;
	UPROPERTY(Transient) TArray<int32> Values;                       // пункт списка → значение поля буфера
};

UCLASS()
class ASDAREBORN_API UAsdaSystemOptionSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable: оба окна и весь буфер настроек принадлежат СЕССИИ ──
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// `gameLeaveToCharSelect@0xED2E10`: сейв хотбара, 1028, карта входа. ★ 09.11.2026 открыт для смены имени
	//   (`UAsdaCharRenameSubsystem`, 6141 код 1 — у оригинала там `g_GameState = 0`).
	void DoReturnToCharSelect();

	// Порт `objStateAllowsRender(g_InventoryBag.pLocalPlayerObj)` — «персонаж занят/в бою».
	// ★ Волна В2 ремесла (25.09.2026): ПУБЛИЧНО — тот же предикат (миснейм IDA: «состояние персонажа
	//   запрещает») второй строкой проверяет гейт окна ремесла `tileInteractGateAndOpen@0xFDD1A0`
	//   (ui_core.c:100522, молча). Второй копии не заводим.
	bool IsLocalPlayerBusy() const;

	// ★ 18.10.2026 (проверка обновлений): штатный выход из игры БЕЗ вопроса — тело ветки «Да» окна «Выйти из игры?»
	//   (`DoExitGame`: раскладки хотбара, выход к серверу, закрытие). Зовёт окно «Доступно обновление»
	//   (`UAsdaUpdateCheckSubsystem`), у которого единственная кнопка — «Закрыть игру». Гейт боя — у вопроса, не здесь.
	void ExitGameNow() { DoExitGame(); }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSystemOptionSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═══════════ КОНСТАНТЫ ЭТАЛОНА (не менять) ═══════════
	static constexpr int32 kMenuIdNum   = 69400;   // SYSTEM_MenuWIN_NEW
	static constexpr int32 kOptionIdNum = 69500;   // SYSTEM_Option_NEW
	static constexpr int32 kMenuZ       = 620;     // ESC-меню: выше HUD и окон, ниже модалок msgbox
	static constexpr int32 kOptionZ     = 610;     // окно настроек: под ESC-меню, над обычными окнами

	// ═══════════ ESC-МЕНЮ 69400 ═══════════
	// Порт `uiPanelToggleOrClearTabs@0xE8FA70`: видно → скрыть (+ разрешить кнопки трея), нет → показать.
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void ToggleMenu();

	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void ShowMenu();

	// Порт `sub_93C2AD` (= `sub_ED5760`): `CControl_SetVisible(this[0], 0)`.
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void HideMenu();

	// ★ 10.10.2026 [ОРИГИНАЛ] Роспись ESC окна 69400 (`RegisterEscCloser`): эталон `uiOptionsWindow_WndProc@0xED4840`,
	//   событие 0x15, `a3 == 69400` → `sub_93C2AD` = ТОЛЬКО `CControl_SetVisible(69400, 0)` — без `uiPanelClearAllTabFlags`
	//   (его зовёт лишь тоггл `uiPanelToggleOrClearTabs`). Идемпотентно: скрытое меню не трогает.
	void EscHideMenu();

	UFUNCTION(BlueprintPure, Category="Asda|SysOpt")
	bool IsMenuShown() const;

	// ═══════════ ОКНО НАСТРОЕК 69500 ═══════════
	// Порт `keybindPageShowAndRefresh@0xED5790`: показать окно, снять снимок настроек для «Отмены»,
	//   сбросить флаг «нужен перезапуск» и перерисовать ВСЕ страницы клавиш.
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void ShowOptions();

	// Порт `optionsWnd_ApplyAndSave@0xED5800`. bApply: true = ОК, false = Отмена (откат снимка).
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void CloseOptions(bool bApply);

	// ★ 10.10.2026 [ОРИГИНАЛ] Роспись ESC окна 69500 (`RegisterEscCloser`): эталон `uiOptionsWindow_WndProc@0xED4840`,
	//   событие 0x15, `a3 == 69500` → `optionsWnd_ApplyAndSave(this, 0)` = ветка «Отмена» (`CloseOptions(false)`).
	void EscCloseOptions();

	UFUNCTION(BlueprintPure, Category="Asda|SysOpt")
	bool IsOptionsShown() const;

	// Вкладка 0 «Интерфейс» / 1 «Система» / 2 «Клавиши» (`uiTabWindowSelectTab@0xED5910`).
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void SelectTab(int32 Tab);

	UFUNCTION(BlueprintPure, Category="Asda|SysOpt")
	int32 GetSelectedTab() const { return CurrentTab; }

	// Под-страница активной вкладки (`uiTabWindowShowPage` / `ShowPage3` / `SelectPage`).
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void SelectSubPage(int32 Tab, int32 Page);

	// Порт `optWndDispatchCommand` 0x11329: применить, окно оставить открытым.
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void ApplyNow();

	// ★ 12.10.2026: самотест окна `asda.SysOpt.SelfTest [quit]` — состав строк фазы 2 после сокращения 23 → 16 (снятых нет),
	//   ползунок «Дальность прорисовки» → буфер → «ОК» → `asda.Cull.Scale` и профиль, откат. Тело — в конце .cpp, только
	//   под ASDA_DEV_CONSOLE (в Shipping пустое). Разбор — шапка функции.
	void RunSelfTest(const TArray<FString>& Args);
	// ★ 18.10.2026: самотест строки «Цифры урона» `asda.SysOpt.SelfTestDmgText [quit] [shot=<папка>]` — путь игрока (клики Slate),
	//   живая геометрия строки и соседей, 0→1→2 с «Применить», «Отмена», «ОК», ini; кадры окна. Тело — в конце .cpp (ASDA_DEV_CONSOLE).
	void RunDmgTextSelfTest(const TArray<FString>& Args);
	// ★ 19.10.2026: самотест строк мыши/камеры `asda.SysOpt.SelfTestMouse [quit] [shot=<папка>]` — путь игрока (клики Slate), живая
	//   геометрия строк I2/I3 и соседей, стрелки → буфер, «Применить» → профиль и ini, «Отмена», таблица уровней Windows. Тело — в конце .cpp.
	void RunMouseSelfTest(const TArray<FString>& Args);
	// ★ 25.10.2026: самотест строки «Присед» `asda.SysOpt.SelfTestCrouch [quit] [shot=<папка>]` — путь игрока (клики Slate), живая
	//   геометрия строки I4 и соседей на «Разном», «Нажатие»↔«Зажатие» с «Применить», «Отмена», повторное открытие, «ОК», ini,
	//   режим персонажа сразу (`GetCrouchMode`). Тело — в конце .cpp (ASDA_DEV_CONSOLE).
	void RunCrouchModeSelfTest(const TArray<FString>& Args);

	// ── Клавиши ──
	// Порт `SkillTab_Select@0xED5E20`: начать/отменить захват клавиши для действия `ActionId`.
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void BeginKeyCapture(int32 ActionId);

	// Порт `uiWindowResetSelection@0xED60E0`: выйти из режима захвата, спрятать оверлей 69910.
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void CancelKeyCapture();

	UFUNCTION(BlueprintPure, Category="Asda|SysOpt")
	bool IsCapturingKey() const { return bCapturing; }

	// ★ 25.10.2026 [ОРИГИНАЛ] (ключ `asda.Keys.CaptureSwallow`): клавиша, нажатая в режиме захвата, — не игровая команда.
	//   Эталон в захвате не ставит флаги хода (`Input_OnKeyUp_Dispatch@0xE3C730`: `!captureMode`) и не диспетчит команду
	//   (`Input_OnKeyDown_Dispatch@0xE3C7D0`, ветка captureMode), а сам захват идёт по ОТПУСКАНИЮ. Порт ловит клавишу по
	//   НАЖАТИЮ, поэтому её отпускание приходило уже после захвата и исполняло только что назначенную команду (Пробел на
	//   подбор — прыжок на нажатии и подбор на отпускании). Здесь — VK, которые были зажаты в момент захвата, держатся
	//   «съеденными» до отпускания. `IsVkHeldFromCapture` — проверка без снятия (опрос прыжка/расширений),
	//   `ConsumeCaptureHeldKey` — проверка `PollKeys` со снятием на отпускании. При ключе 0 обе — false.
	bool IsVkHeldFromCapture(int32 Vk) const;
	bool ConsumeCaptureHeldKey(int32 Vk, bool bDownNow);
	// Идёт захват клавиши при показанном окне и ключ включён — игровой ввод клавиатуры стоит целиком.
	bool IsGameInputCaptured() const;

	// Порт ветки 69651: загрузить хардкод-дефолты раскладки и перерисовать страницы.
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void LoadKeybindDefaults();

	// Порт ветки 0x11014 (69652): снять привязку с ВЫБРАННОГО действия (работает только в захвате).
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void ClearSelectedBind();

	// Перерисовать текст клавиш ВСЕХ четырёх страниц (`keybindWndRefreshAll@0xED6830`).
	UFUNCTION(BlueprintCallable, Category="Asda|SysOpt")
	void RefreshAllHotkeyLabels();

	// ── Ввод: зовётся из `AAsdaPlayerController::PlayerTick` (там уже поднят опрос клавиш) ──
	//   Обрабатывает ESC и приём клавиши в режиме захвата.
	//   ★ `bEscConsumed` (ревизия 2026-09-17, С6) — ESC в этом кадре уже забрала админ-надстройка
	//     (`UAsdaAdminSubsystem::PollHotkeys` вернула true: выход из свободной камеры или закрытие
	//     GM-меню). Тогда мы фронт клавиши всё равно ДОЧИТЫВАЕМ (латч обязан сдвинуться, иначе
	//     следующее нажатие будет пропущено), но действий по ESC не совершаем.
	void PollHotkeys(APlayerController* PC, bool bEscConsumed = false);

	// ══════════════ ГЕЙТ ТОГГЛА ESC-МЕНЮ 69400 (пункт С10 реестра, закрыт 17.09) ═════════════
	//
	// ЭТАЛОН. `inputHandleInventoryAndEscHotkeys@0xC6E710` (`RE/etalon/input.c:1732`) зовёт
	//   `uiPanelToggleOrClearTabs@0xE8FA70` ТОЛЬКО при четырёх ложных условиях сразу:
	//     !sub_9323E3(g_pUIManager) && !helper_getField1(g_pUIManager)
	//     && !sub_930E71(g_pWindowMgr) && !sub_931CA4(g_pWindowMgr)
	//   Имена ничего не значат — все четыре ТОНКИЕ ГЕТТЕРЫ, вывод сделан по их телам:
	//
	//   1) `sub_9323E3@0x9323E3` → `get_o11@0x9865A0` = `*((DWORD*)g_pUIManager + 17)` (+0x44).
	//      Пишет `setField_p17@0x92F292` → `set_o11@0x966700`, 40 мест. В 1 ставят ДВЕ группы:
	//        • принудительный системный экран — `DeathMenu_AddOption@0xD6D060` (меню смерти) и
	//          нотисы 2318/2370 в `CMsgBox_SetContent@0xD6E790`; обе тут же делают
	//          `uiPanel_ShowTab(g_pSystemMainWnd, 6)`;
	//        • ЗАЩИЩЁННЫЙ ЗАПРОС В ПОЛЁТЕ — `shopSendBuyConfirm@0xE138E0`, `shopBuySingleSendReq
	//          @0xE206B0`, `shopBuyMultiSendReq@0xE18EC0`, `netSendItemSecureRequest@0xE1D3B0`,
	//          `net_sendSecuredRequest_5031@0xD167A0` — все ставят 1 СРАЗУ ПОСЛЕ `CNetwork_Send`
	//          вместе со своим локальным «жду ответ».
	//      В 0 снимают приёмники ответа (`shopWindow_OnTradeResult@0xE147D0`, `OnSendItemUpgrade
	//      @0xD74180`, `sowelWndHandleSocketResult@0xD16070`), выбор в меню смерти
	//      (`DeathMenu_OnSelect@0xD6DE70`), закрытие окна настроек (`SystemMenu_OnUICommand@0xE93E80`,
	//      `uiPanelToggleOrClearTabs`) и вход в мир/на карту (`netHandler_EnterWorld@0xF43050`).
	//      ⇒ СМЫСЛ: «интерфейс в блокирующем состоянии». Подтверждение независимым путём:
	//      `Input_OnKeyDown_Dispatch@0xE3C7D0` зовут с белым списком `a4 = 44` именно при
	//      `sub_9323E3(...) == 1` (`MainWndProc@0x9841A0`), то есть из раскладки выживает ОДНО действие.
	//
	//   2) `helper_getField1@0x9354C1` → `get_o1_6580@0x986580` = `*((DWORD*)g_pUIManager + 1)` (+0x04).
	//      Пишет `sub_91F61C@0x91F61C` → `set_o1_5C90@0xC55C90`, 26 мест, и ВСЕ они — пары
	//      JUI-событий 6/7 у окон с ячейками (`hotbarOnUiEvent@0xDF28E0`, `uiControlMessageProc
	//      @0xC6C030`, `EmoteWnd_WndProc@0xC55780`, `consignDropMouseUp@0xD428F0`): 6 = нажатие ЛКМ
	//      на ячейке → 1, 7 = отпускание → 0. Те же функции на событии 8 (движение мыши) по этому
	//      флагу ГЛУШАТ ПОДСКАЗКИ. ⇒ СМЫСЛ: «предмет держат в руке» (перетаскивание).
	//
	//   3) `sub_930E71@0x930E71` → `get_o200@0x9864A0` = `*((BYTE*)g_pWindowMgr + 0x200)`.
	//      В 1 ставит `helper_setupFields@0x924FA9` → `sub_12AFBB0@0x12AFBB0` — та самая функция,
	//      которой показывают MSGBOX, окно покупки, окно подарка (`CMsgBox_SetContent`, все ветки);
	//      в 0 — `sub_92E450@0x92E450` → `sub_12B2900@0x12B2900` (он же обнуляет указатель на
	//      модальный контрол `+104`). ⇒ СМЫСЛ: «открыто модальное окно менеджера окон».
	//
	//   4) `sub_931CA4@0x931CA4` → `get_o208@0xC6E580` = `*((BYTE*)g_pWindowMgr + 0x208)`.
	//      Единственный, кто его взводит, — `uiContainerHandleKey@0x12AF390`: при VK 27 он ищет
	//      ВИДИМЫЙ контрол с флагом `get_o228@0x12B3DE0` (`CControl+0x228`), шлёт ему событие
	//      закрытия `CControl_OnEvent21` и ставит +0x208 = 1; если никого не закрыл — 0.
	//      ⇒ СМЫСЛ: «ЭТО САМОЕ нажатие ESC уже съедено окном» (окно закрылось — меню не открывать).
	//      ★ Флаг `CControl+0x228` ставят 53 функции инициализации окон через обёртку
	//        `helper_setByteField552@0x91F8CE` (62 вызова, всегда значением 1, снимающих вызовов нет):
	//        инвентарь `InventoryWindow_InitControls@0xCFC8F0`, книга навыков `CSkillWindow_InitControls
	//        @0xE50DB0`, окно персонажа `CCharaInfoWin_InitControls@0xE75E70`, миникарта `MiniMap_BuildUI
	//        @0xD63220`, msgbox `CMsgBoxMgr_InitControls@0xD6AD20`, лавки, склад, обмен, кланы, питомцы —
	//        и САМО ОКНО НАСТРОЕК `CSystemOptionUI_InitControls@0xE906E0` (`pWinSystem`).
	//        ⇒ В оригинале ESC СНАЧАЛА закрывает верхнее открытое окно и только потом, если закрывать
	//        было нечего, тоггает меню 69400. Порт воспроизводит из этого ровно два случая: окно
	//        настроек (ветка `IsOptionsShown() → CloseOptions(false)` ниже по `PollHotkeys`) и свои
	//        админ-окна (`bEscConsumed`). Общего прохода менеджера окон нет — пункт С10-б реестра.
	//      ⚠ УРОК (закон `false-negatives-method`): первый поиск по имени `set_o228@0xB61180` дал
	//        НОЛЬ вызовов в обоих срезах, и это было ЛОЖНОЕ «нет» — пишет поле не он сам, а тонкая
	//        обёртка с ничего не говорящим именем. Нашлась она встречным поиском от порта, а не от
	//        адреса. JUI-парсер `CWindowMgr_LoadJUI_ParseNode` +552 действительно не трогает: он
	//        разбирает Visible (+292), NoSelect (+295), AlwaysTop (+553), EFrameStop.
	//
	// ЧТО ИЗ ЭТОГО ЕСТЬ В ПОРТУ: (1) частично — меню смерти `UAsdaAfterDeathSubsystem`; (2) полностью —
	//   `FAsdaDragGhost::IsAnyActive`; (3) полностью — `UAsdaWindowSubsystem::IsAnyModalOpen`;
	//   (4) `bEscConsumed` (проверяется в `PollHotkeys` отдельно, ДО этого гейта). Чего нет —
	//   в `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`, пункты С10-а / С10-б / С10-в.
	//
	// `OutWhy` — причина по-русски для журнала. Ключ отката: `asda.SysOpt.EscGate 0`.
	bool IsEscMenuGateBlocked(FString& OutWhy) const;

	// Текстовая сводка состояния — консоль `asda.SysOpt.Dump`.
	FString DebugState() const;

	// ★★★ 25.10.2026 [ОРИГИНАЛ] Порт `optionsApplyServerFlags@0xED1B40` (game_main.c:11915): блок опций персонажа из
	//   1027 (19 байт, раскладка = тело 4070) → буфер окна и поля профиля в памяти (на диск — только по «ОК», как
	//   эталонный `Config_WriteSettings`). Ставит «Интерфейс-1» №0…№6 (байты 7, 9, 10, 11, 12, 5, 8), «Интерфейс-2»
	//   №0/№1 (13/14) и «Показывать шлем» (15). Байт 6 (`filler[1104]`) эталон не принимает — не трогаем.
	//   Ключ `asda.SysOpt.ServerFlags 0` — прежнее поведение порта (окно знает только локальный профиль).
	void ApplyServerOptionFlags(const TArray<uint8>& Block19);
	// ★ 25.10.2026 Флаг вкладки «Интерфейс» буфера окна: группа 1/2/3 (`Int1`/`Int2`/`Int3`), номер строки. −1 — нет такого.
	//   Для самотестов и дампа.
	int32 GetInterfaceFlag(int32 Group, int32 Idx) const;
	// ★ 25.10.2026 Самотест: поставить флаг «Интерфейса» в буфер и провести ТО, что эталон делает по «ОК» для показа
	//   персонажа (`ApplyCharacterDisplayToGame` + пакет 4070), не трогая остальные группы окна.
	void DebugConfirmCharacterDisplay(int32 Group, int32 Idx, bool bOn);

	// ★★★ ГРАБЛЯ ПОДКЛЮЧЕНИЯ К ПАНЕЛИ МЕНЮ (шапка `AsdaSystemMenuSubsystem.h`): подключений ДВА и
	//   они РАЗНЫЕ — `OnMenuCommand` (сама команда кнопки) и `RegisterWindow` (только подсветка).
	//   Кнопка **73406** тогглит именно ESC-меню 69400 (`uiPanelToggleOrClearTabs@0xE8FA70`).
	UFUNCTION()
	void OnMenuCommand(int32 ButtonIdNum, int32 TargetWindowId, bool bOpen);

private:
	// ── Окна ──
	bool EnsureMenuWidget();
	bool EnsureOptionWidget();
	void SetupMenuControls();
	void SetupOptionControls();
	void ResetMenuWidget();
	void ResetOptionWidget();

	// ── ESC-меню ──
	void OnMenuButton(int32 Which);          // 0 = 69401, 1 = 69402, 2 = 69403, 3 = 69404/69419
	void AskExitGame();                      // msgClass 3
	void AskReturnToCharSelect();            // msgClass 4
	void DoExitGame();                       // `gameReturnToCharSelectTimer@0xED2D90`
	// (Порт `objStateAllowsRender` — `IsLocalPlayerBusy` — перенесён в публичную секцию, волна В2 ремесла.)

	// ── Настройки: страницы и контролы ──
	// ★★★ ДВЕ СТРОКИ «ИНТЕРФЕЙС-3», КОТОРЫЕ ЭТАЛОН ПОКАЗЫВАЕТ ЯВНО (контролы 70378/70380):
	//   `uiDialogInitControls` зовёт им `CControl_SetVisible(…, 1u)` вопреки нулю в данных `.JUI`.
	//   Это поведение ОРИГИНАЛА, а не заплатка импорта — обоснование у тела функции в .cpp.
	//   (Прежний временный мост `RevealImportedPanel` снят 17.09.2026 вместе с починкой импортёра.)
	void RevealEtalonShownRows();
	void BuildCheckTable();
	void ApplyTabVisibility();
	void ApplySubPageVisibility(int32 Tab);
	// ★★★ 19.09.2026: СПИСОК ПОД-СТРАНИЦ — ЭТО СПИСОК, А НЕ ВЫПАДАЮЩЕЕ МЕНЮ.
	//   Эталон: контролы 70156 / 70006 / 69517 (скин `TalkBar_system_D`, тип `RadioButton`) —
	//   `CListBox`, которому `uiDialogInitControls` / `uiTradeDialogInitControls` /
	//   `uiDlgInitControlsByID` клонируют ячейки (`uiListAppendClonedCell`, шаг = высота ячейки 25)
	//   и ставят подписи loc 30400… / 30501… / 30011…; выбор строки — `CControl_SetSelectedIndex`.
	//   Прежде порт принимал его за «свёрнутый список» с раскрытием по клику и показывал ОДНУ строку.
	//   Ячейки 1…N−1 теперь лежат в АССЕТЕ (шаг `Tools/asda_ui_option_list_cells.py`), здесь —
	//   только подписи из loc и подсветка выбранной ячейки (кадр 2 скина — состояние, а не разметка).
	void RefreshListCells();

	// ── Буфер настроек ──
	void LoadStateFromGame();                // порт `optionsWndLoadSettingsToControls@0xED1C50`
	void ApplyStateToControls();             // порт `optionsWnd_applySettingsToControls@0xED23E0`
	// Порт `optApplySettingsFromConfig@0xED1F10`.
	//   ★ `bUserConfirmed` = «игрок нажал ОК/Применить». Только в этом случае эталон шлёт серверу
	//     пакет 4070 и показывает предупреждение «нужен перезапуск»: ветка ОТКАТА
	//     (`optionsWnd_ApplyAndSave@0xED5800`, `RE/etalon/game_main.c:12181-12185`) ни того,
	//     ни другого не делает, а лишь восстанавливает буфер и накатывает его на контролы.
	void ApplyStateToGame(bool bUserConfirmed = true);
	// ★★★ 19.09.2026: персист — ШТАТНЫЙ профиль игрока `UAsdaGameUserSettings` (поля `UPROPERTY(Config)`
	//   + `SaveSettings`), а не строки `GConfig` в секции `[AsdaOptions]`. Эталон писал `system\config.txt`
	//   (`Config_WriteSettings`); владелец велел хранить «как правильно в UE».
	void SaveStateToConfig() const;          // буфер → поля профиля → `SaveSettings`
	void LoadStateFromConfig();              // поля профиля → буфер
	// Флаги вкладки «Интерфейс» (и облачка речи 3D №3) у своих живых владельцев: хотбар, окно
	//   хвастовства, облачка, «всегда бежать», подсказка квеста. Зовётся по ОК/Применить и при
	//   появлении нового игрового мира — иначе сохранённое не доезжало до игры до первого открытия окна.
	void ApplyInterfaceStateToGame();
	// ★ 25.10.2026 [ОРИГИНАЛ] Показ своего шлема по «ОК» — хвост `optApplySettingsFromConfig@0xED1F10`
	//   (game_main.c:12035-12040): `LocalPlayer+0x32F78 = filler[1116]; CCharacter_ApplyEquippedHelmet(LocalPlayer)`.
	//   Только по подтверждению: при появлении мира шлем задаёт сервер (4003 [8]), а не локальный профиль.
	void ApplyCharacterDisplayToGame();
	void SendSettingsPacket4070() const;     // порт `uiOptionsSendSettingsPacket2@0xED69E0`
	FDelegateHandle ServerOptionFlagsHandle; // ★ 25.10.2026: подписка на блок опций 1027 (`OnServerOptionFlags` сети)
	void OnCheckToggled(int32 IdNum, bool bOn);   // порт `uiOptions_ApplyControlValue@0xED4F80`

	// ── Клавиши ──
	void RefreshHotkeyPage(int32 Page);      // порт `keybindPageRefreshLabels@0xED6330`
	void MoveClickOverlayTo(const FAsdaOptHotkeyRow* Row);
	void SetHotkeyScroll(int32 Pos);
	const FAsdaOptHotkeyRow* FindRow(int32 ActionId) const;
	void AcceptCapturedKey(const FKey& Key, bool bShift, bool bCtrl, bool bAlt);

	// ── Полосы ──
	void SetSliderValue(FAsdaOptSlider& S, int32 NewValue);
	void LayoutSliderThumb(FAsdaOptSlider& S);
	bool TickSlider(FAsdaOptSlider& S, const FAsdaMouseFrame& M);
	FAsdaOptSlider* FindSlider(int32 IdNum);

	// ── Кадровые разборы ──
	void TickOptionInput(const FAsdaMouseFrame& M);

	// ══ ★ ФАЗА 2: НОВЫЕ СТРОКИ ОКНА (разбор и таблица строк — в .cpp, пространство имён `SysOptExtra`) ══
	void SetupExtraControls();               // найти в ассете клоны строк и подключить их
	void RebuildExtraComboItems();           // состав списков (зависит от текущих значений: «Ультра», «Своё», 2x…)
	void SyncExtraCombos();                  // выбранные пункты ← буфер
	void OnExtraPicked(int32 Key, int32 Value);
	void OnAutoDetect();                     // кнопка «Автоподбор» страницы «Качество»
	void CloseAllCombos();                   // смена вкладки/страницы, закрытие окна — раскрытые списки гаснут
	bool IsAnyComboOpen() const;
	// Подписи окна С ИСПРАВЛЕНИЯМИ ПЕРЕВОДА: наша строка 990xxx, если она есть, иначе эталонная loc.
	FString LocUi(int32 EtalonLocId) const;
	// Текст клавиши для ОКНА (решение владельца №5): как на клавиатуре — «Space», «Enter», «Shift+F1»;
	//   не назначено — «—». В файле раскладки имена прежние (формат оригинала).
	static FString FormatBindForWindow(const FAsdaKeyBind& B);
	// Эталонная loc подписи строки клавиш по коду действия (`uiDlgInitControlsByID`, блоки 30040…30272).
	static int32 HotkeyActionLocId(int32 ActionId);

	UAsdaKeybindConfig* GetKeybinds() const;
	FString Loc(int32 Id) const;
	void ChatNotice(int32 LocId) const;

	// Плоский индекс пары «вкладка (0…2) × под-страница (0…4)» для массивов `OptSubPage` и др.
	//   ★ Фаза 2: у вкладки «Настройки системы» под-страниц стало ПЯТЬ («Звук / Экран / 3D-графика /
	//   Качество / Эффекты», решение владельца №4), поэтому шаг 5, а массивы — 3×5.
	static constexpr int32 kMaxPages = 5;
	static FORCEINLINE int32 PageSlot(int32 Tab, int32 Page) { return Tab * kMaxPages + Page; }
	// Число под-страниц вкладки: 3 «Настройки игры», 5 «Настройки системы», 4 «Горячие клавиши».
	static FORCEINLINE int32 PageCount(int32 Tab) { return (Tab == 1) ? 5 : (Tab == 2) ? 4 : 3; }

	// ── Виджеты ESC-меню ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> MenuWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> MenuBtnExit = nullptr;      // 69401
	UPROPERTY(Transient) TObjectPtr<UButton> MenuBtnRestart = nullptr;   // 69402
	UPROPERTY(Transient) TObjectPtr<UButton> MenuBtnOptions = nullptr;   // 69403
	UPROPERTY(Transient) TObjectPtr<UButton> MenuBtnQuit = nullptr;      // 69404
	UPROPERTY(Transient) TObjectPtr<UButton> MenuBtnClose = nullptr;     // 69419

	// ── Виджеты окна настроек ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> OptWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> OptTabBtn[3] = { nullptr, nullptr, nullptr };    // 69502
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OptTabText[3] = { nullptr, nullptr, nullptr };
	UPROPERTY(Transient) TObjectPtr<UWidget> OptTabRoot[3] = { nullptr, nullptr, nullptr };   // 70150 / 70000 / 69510
	// ⚠ UHT принимает у UPROPERTY только ОДНОМЕРНЫЕ фиксированные массивы, поэтому три группы
	//   «вкладка × под-страница» разложены в плоские массивы 3×5; индекс считает `PageSlot`.
	UPROPERTY(Transient) TObjectPtr<UWidget> OptSubPage[15];                                  // под-панели вкладок
	// Ячейки списков под-страниц (70156 / 70006 / 69517): план скина, кнопка выбора, подпись.
	UPROPERTY(Transient) TObjectPtr<UImage> OptListPlane[15];
	UPROPERTY(Transient) TObjectPtr<UButton> OptListBtn[15];
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OptListText[15];
	// Кисти плана ячейки: покой (кадр 0 — как лежит в ассете) и выбрано (кадр 2 — его несёт
	//   кнопка ячейки как `Pressed`, импортёр кладёт туда ровно кадр 2 скина `TalkBar_system_D`).
	FSlateBrush OptListBrushIdle[15];
	FSlateBrush OptListBrushSel[15];
	bool bOptListBrushValid[15] = {};
	// Зона колеса/обрезки страницы клавиш — контрол 69649 `SYSTEM_Op_HOTKEYS_Scroll` (484×346).
	//   Клик и колесо действуют ТОЛЬКО внутри неё: строки, уехавшие прокруткой под рамку,
	//   невидимы (обрезаны ассетом) и нажиматься не должны.
	UPROPERTY(Transient) TObjectPtr<UWidget> HotkeyViewport = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> OptBtnApply = nullptr;    // 70441
	UPROPERTY(Transient) TObjectPtr<UButton> OptBtnOk = nullptr;       // 70442
	UPROPERTY(Transient) TObjectPtr<UButton> OptBtnCancel = nullptr;   // 70443
	UPROPERTY(Transient) TObjectPtr<UButton> OptBtnClose = nullptr;    // 70599
	UPROPERTY(Transient) TObjectPtr<UButton> OptBtnDefault = nullptr;  // 69651
	UPROPERTY(Transient) TObjectPtr<UButton> OptBtnClearKey = nullptr; // 69652
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OptBtnApplyText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OptBtnOkText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OptBtnCancelText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OptBtnDefaultText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OptBtnClearKeyText = nullptr;
	// Оверлей «нажми клавишу» 69910 `SYSTEM_CLICK` — 8 планов рамки `Box_White3Stroke`.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ClickOverlay;
	// Исходные смещения планов оверлея (оверлей ПЕРЕставляется целиком, форма не меняется).
	TArray<FVector2D> ClickOverlayBase;
	// Прокручиваемое содержимое страниц клавиш — четыре контейнера под-страниц.
	UPROPERTY(Transient) TObjectPtr<UWidget> HotkeyPageHost[4];
	// Их исходные смещения (прокрутка считается ОТ ИСХОДНОГО, а не от текущего — грабля D19 склада).
	TArray<FVector2D> HotkeyPageBase;

	UPROPERTY(Transient) TArray<FAsdaOptHotkeyRow> HotkeyRows;
	UPROPERTY(Transient) TArray<FAsdaOptCheck> Checks;
	UPROPERTY(Transient) TArray<FAsdaOptSlider> Sliders;
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> ResolutionCombo = nullptr;   // 70053
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> FlareCombo = nullptr;        // 70121 «Свечение»
	// ★ Фаза 2: списки новых строк и кнопка «Автоподбор» (клон «По умолчанию» 69651).
	UPROPERTY(Transient) TArray<FAsdaOptExtraCombo> ExtraCombos;
	UPROPERTY(Transient) TObjectPtr<UButton> AutoDetectBtn = nullptr;
	// Уровень группы в момент открытия окна (для пункта «Ультра» у групп, где он уже стоит).
	int32 OpenGroupLevel[8] = { 3, 3, 3, 3, 3, 3, 3, 3 };
	// Все группы качества (включая скрытые в окне) при открытии были одним уровнем? −1 — вразнобой.
	int32 OpenSingleLevel = -1;
	// Разрешения, которыми заполнено комбо 70053 (индекс пункта → размер).
	TArray<FIntPoint> ResolutionList;

	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Состояние ──
	UPROPERTY(Transient) FAsdaOptState State;      // живой буфер (эталонные +1104…)
	UPROPERTY(Transient) FAsdaOptState Snapshot;   // снимок при открытии (эталонные +1158…)
	UPROPERTY(Transient) bool bStateLoaded = false;

	UPROPERTY(Transient) int32 CurrentTab = 0;
	UPROPERTY(Transient) int32 CurrentPage[3] = { 0, 0, 0 };
	// Предупреждение «Общее качество меняет все группы» показано в этом открытии окна.
	bool bPresetWarned = false;
	// Контроллер игрока, которому уже раздали флаги «Интерфейса» (новый мир = новый контроллер).
	TWeakObjectPtr<APlayerController> InterfaceAppliedPC;
	UPROPERTY(Transient) bool bCapturing = false;   // эталонный байт +1096
	UPROPERTY(Transient) int32 CaptureAction = -1;  // эталонное поле +275
	TSet<int32> CaptureHeldVk;                      // ★ 25.10.2026: VK, зажатые в момент захвата (съедаются до отпускания)
	UPROPERTY(Transient) int32 HotkeyScroll = 0;    // позиция прокрутки страницы клавиш (шаг 19 px)
	UPROPERTY(Transient) int32 HotkeyScrollMax = 0; // верх диапазона (`uiControlSetScrollRange`)
	// Эталонный байт +1228: «настройки требуют перезапуска» (смена оконного режима).
	UPROPERTY(Transient) bool bNeedRestartNotice = false;
	// ★ [ПОРТ] 09.10.2026: язык сменён в этом окне — после «ОК» вопрос «Перезапустить сейчас?» (loc 990356).
	UPROPERTY(Transient) bool bNeedLangRestart = false;

	// Кадр, на котором ESC уже обработан (одно нажатие — одно действие).
	uint64 EscHandledFrame = 0;
	TMap<FKey, bool> PrevKeyDown;

	// Консольные команды диагностики; снимаются в `Deinitialize`.
	TArray<IConsoleObject*> ConsoleCmds;

	// Кнопка 73406 панели меню уже подключена (подписка делается один раз, как у соседних окон).
	UPROPERTY(Transient) bool bMenuBound = false;

	FAsdaMouseFrame OptMouse;     // латч мыши окна настроек (латч принадлежит МЕСТУ опроса)
};
