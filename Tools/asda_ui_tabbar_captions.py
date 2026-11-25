# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_tabbar_captions.py — ★★★ ВТОРЫЕ (и далее) ПОДПИСИ ПЕРЕКЛЮЧАТЕЛЕЙ-ВКЛАДОК В АССЕТЕ.
#
# ПРОБЛЕМА (класс дефектов импортёра S4/С4 из аудита `audit_runtime_vs_assets_SUMMARY.md`).
#   У контрола-переключателя со скином `TabBar_*` / `RadioButton` импортёр создаёт N КНОПОК
#   (`{idc}_OPT0`, `{idc}_OPT1`, …), но подпись — ОДНУ (`{idc}_Text`), да ещё во всю ширину
#   контрола, то есть поверх ВСЕХ вкладок. Затронуто **52 контрола в 31 окне**.
#   Следствие: игрок видит одну надпись и не понимает, что рядом есть вторая вкладка. Ровно так
#   у склада была не видна категория «Снаряжение» — вторую подпись пришлось создавать кодом.
#
# ЧТО ДЕЛАЕТ ШАГ (идемпотентно):
#   для каждого перечисленного переключателя кладёт В АССЕТ по подписи НА КАЖДУЮ кнопку —
#   `{idc}_Text` прижимается к первой кнопке, `{idc}_Text2`, `_Text3`… создаются для остальных.
#   Геометрия берётся у САМИХ КНОПОК (`{idc}_OPT<i>`), цвет — из данных `.JUI`,
#   ★ ВИДИМОСТЬ — ТОЖЕ ИЗ ДАННЫХ `.JUI` (см. `caption_visibility` ниже; правка 02.09.2026,
#   до неё шаг жёстко зажигал подписи и у СКРЫТЫХ полос — отсюда «Соулмейт» поверх «Друга»).
#
# ★ ПОЧЕМУ В АССЕТЕ, А НЕ КОДОМ: требование владельца (2026-08-27) — оформление живёт в виджете.
#   В коде остаётся только СОСТОЯНИЕ «какая вкладка активна» (её подсветка), которое в ассет
#   не уносится в принципе.
#
# ═══ КАК СОБРАН СПИСОК `TABBARS` (2026-08-27, охват расширен с 1 строки до 42) ═══
#   1. ДАННЫЕ. Оффлайн-скан всех четырёх `.JUI` (`gamemain/commonwin/temp/login_jui.ini`) тем же
#      парсером, что и импортёр (`asda_jui_parse.py` + `JuiSkin.ini`): контрол попадает в список,
#      если скин имеет тип `RadioButton`, у контрола НЕПУСТОЙ `ID`, размер > 0 и ВИДИМЫХ ПЛАНОВ
#      БОЛЬШЕ ОДНОГО (план = вкладка, импортёр делает по кнопке на план — `asda_jui_generic_umg.py`
#      :489-501). Найдено 52 контрола в 32 сборках окон (31 окно + вторая сборка `Job_Guide_Win`
#      из класса `temp`) — цифра сошлась с аудитом.
#   2. ПРОБА ПО ФАКТУ (не по данным): в каждом ассете `WBP_<окно>` найдены реальные узлы
#      `{idc}_OPT0..N` — совпало для всех 52 без исключения.
#   3. ★ ВТОРОЙ МЕТОД (доказательство отрицания «больше переключателей нет»): независимый скан
#      ВСЕХ 259 ассетов `/Game/AsdaAssets/UI` на узлы `*_OPT<i>`. Групп 55: 50 наших (52 контрола
#      минус два ДУБЛЯ idc, см. ниже) + 4 контрола с ОДНОЙ кнопкой (`CHAT_NEW_Radio_B`,
#      `CHAT_NEW_SERV_SYSbottn`, `CHAT_TAB_WIN_B_button`, `Tutorial_MENU_BOX1_1_T1` — один план,
#      дефекта S4 у них нет) + `SHELL_EQUIP_OPT1/OPT2` окна 6300 — это НЕ переключатель, а
#      совпадение имён: в `.JUI` так названы сами контролы (`gamemain_jui.ini:130272`), и нумерация
#      у них с 1, а не с 0. Пропусков нет.
#   4. ЧИСЛО КНОПОК = число видимых планов скина. У ДВУХ контролов в разметке ДВА одноимённых
#      узла с разными скинами (`CHARAINFO_STMENU` 3 и 2 плана; `EXCHANGER_RADIOBTN2` 7 и 5) —
#      в `WidgetTree` имя уникально, поэтому в ассете лежит ОДИН набор кнопок, самый широкий;
#      в таблице стоит МАКСИМУМ (3 и 7), и проба это подтвердила.
#   5. ЦВЕТ. По закону `ui-text-color-exs` приоритет у `EXS TextColor`, но у переключателей
#      блока `EXS` НЕТ НИ У ОДНОГО (проверено по всем 4 ini), поэтому цвет всюду из
#      `CaptionInfo[2]` и записан как ARGB. Сверено со вторым источником — локализованными
#      `*_loc.ini`: значения совпадают.
#
# ★ ЧЕГО В ТАБЛИЦЕ НЕТ И ПОЧЕМУ — 8 переключателей с ПУСТЫМ `CaptionText` в данных
#   (`AD_PageBtn`, `CARAMAKE_SEXBUT`, `CHARAINFO_3BUT`, `CLAN_TYPE_BTN`, `CmtyClan_SubMenuBtn`,
#   `Find_SMate_GenderSel_Btn`, `Skill_Reset_SelBtn`, `Smelting2_ModeSelBtn`). У них импортёр не
#   создал ни одной подписи — и это НЕ дефект: подписи у них нет и в оригинале (проверено дважды —
#   в `*_jui.ini` и в локализованных `*_loc.ini`, `CaptionText = ""` в обоих), надписи вкладок
#   нарисованы прямо в текстуре плана (пол ♂/♀, номера страниц рекламы, вертикальные иконки).
#   Придумывать им подписи запрещено законом «не выдумывать» — решение за владельцем.
#
# ⚠ ЧИТАЕМОСТЬ. У 29 из 42 строк яркость скина вкладки и яркость текста из данных отличаются
#   меньше чем на 0.30 (замер: средняя яркость UV-региона кадра 0 в атласе × Diffuse плана против
#   яркости цвета подписи) — текст сливается со светлой вкладкой. Это ДЕФЕКТ ОРИГИНАЛА, и правится
#   он ТОЛЬКО по решению владельца: в таблице стоит цвет ИЗ ДАННЫХ. Единственное отступление —
#   склад (`WAREKEEP_2BUT`), где владелец уже потребовал тёмно-серый `ff4c4c4c` вместо `ff808080`.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_tabbar_captions.py
#   Сухой прогон: set ASDA_TABCAP_DRYRUN=1
import os
import re
import sys
import traceback
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_jui_parse as JP          # ★ общая точка: разбор `JuiSkin.ini` (своего парсера не заводим)
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

EAL = unreal.EditorAssetLibrary
FONT_PX = 8            # кегль UI импортёра
EPS = 0.01             # допуск сравнения геометрии (чтобы повторный прогон давал 0 правок)

# (ассет, idc переключателя, число кнопок, ARGB цвета подписи)
#   Комментарий у строки — скин контрола; «★сливается» = кандидат на отступление по читаемости.
TABBARS = [
    ("/Game/AsdaAssets/UI/Player_UI/Character/WBP_CHARAINFO_WIN", "CHARAINFO_STMENU", 3, 0xFF4C4C4C),   # TabBar_W72_3Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Character/WBP_SKILL_WIN", "SKILL_CHARA_BTN1", 3, 0xFF4C4C4C),   # Skill_W124_3Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Character/WBP_SKILL_WIN", "SKILL_CHARA_BTN2", 4, 0xFFE2E2C7),   # Skill_W91_4Btn
    ("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Title_Win", "Title_ClassSelectBtn", 3, 0xFF4C4C4C),   # TabBar_W108_3Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_INVEN_Repair_Win", "INVEN_Repair_Radiobtn", 2, 0xFF4C4C4C),   # TabBar_W108_2Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ITEM_making_WIN", "ITEM_making_Radiobtn", 5, 0xFF4C4C4C),   # TabBar_W64_5Btn_2  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Smelting2_Win", "Smelting2_Alchemy_Btn", 5, 0xFF4C4C4C),   # TabBar_W64_5Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Smelting2_Win", "Smelting2_Assemble_Btn", 5, 0xFF4C4C4C),   # TabBar_W64_5Btn_2  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Smelting2_Win", "Smelting2_Process_Btn", 7, 0xFF4C4C4C),   # TabBar_W46_7Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Fishing/WBP_FISH_BOOK_WIN", "FISH_BOOK_RADIO_btn", 3, 0xFF4C4C4C),   # TabBar_W108_3Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_NEW_WIN", "INVEN_NEW_box3_radio", 2, 0xFF4C4C4C),   # TabBar_W108_2Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_INVEN_WIN", "INVEN_CHOICEBUT", 2, 0xFF4C4C4C),   # TabBar_W108_2Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_WARE_WIN", "WAREKEEP_2BUT", 2, 0xFF4C4C4C),   # TabBar_W108_2Btn  ★ОТСТУПЛЕНИЕ владельца (в данных ff808080)
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_HONOR_itemmall_Win", "HONOR_item_Radio", 4, 0xFFB5B5AB),   # BoxBar_W54W90_63Btn_2
    # ★★★ ПРЕМИУМ-МОЛЛ 30002: у скина ВСЕ ЧЕТЫРЕ плана `PlaneAlwaysInvisible = "1"` — вкладка не
    #   рисует ничего, индикация выбора живёт ТОЛЬКО в цвете подписи (кадр 0 FFC8C8C8, кадр 1
    #   FFFFFFFF, кадр 2 FF94FF28). Кнопки `_OPT0…3` импортёр не делает (нет ВИДИМЫХ планов) —
    #   их кладёт шаг `asda_ui_premium_menu_tabs.py`, он в STEPS стоит ВЫШЕ этого.
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_Menu", "PremiumMall_Menu", 4, "SKIN:PremiumMall_Menu"),   # PremiumMall_Menu (4 сегмента, планы невидимы)
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_INDEX", 3, 0xFF4C4C4C),   # TabBar_W108_3Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_MENU", 10, 0xFFB5B5AB),   # BoxBar_W54W90_73Btn
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_MENU_NEW1", 8, 0xFFB5B5AB),   # BoxBar_PremiumMall_1
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_MENU_NEW1_2", 2, 0xFF4C4C4C),   # BoxBar_PremiumMall_1_2  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_MENU_NEW1_3", 2, 0xFF4C4C4C),   # BoxBar_PremiumMall_1_2  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_MENU_NEW2", 5, 0xFFAEDFF4),   # BoxBar_PremiumMall_2
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_MENU_NEW3", 3, 0xFFAEDFF4),   # BoxBar_PremiumMall_3
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_MENU_NEW4", 8, 0xFFB5B5AB),   # BoxBar_PremiumMall_4
    ("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_Subjugate_WIN", "Subjugate_RADIO", 5, 0xFF4C4C4C),   # TabBar_W108_5Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN", "CMTY_CLAN_3BTN", 3, 0xFFD8D8D0),   # BoxBar_W69_3Btn  ★сливается на ТЁМНОМ → ffd8d8d0, ОСВЕТЛЕНО (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN", "COMMUNITY_MENU", 4, 0xFF4C4C4C),   # TabBar_Cmt_4Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN", "COMMUNITY_MENU_2", 5, 0xFF646464),   # TabBar_Cmt_4Btn_2
    ("/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW", "SYSTEM_Option_NEW_Radio_btn", 3, 0xFFB5B5AB),   # BoxBar_COROR_3BTN
    ("/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_WIN", "SYSTEM_3BUT", 3, 0xFF4C4C4C),   # TabBar_W77_3Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_PREV_WIN", "BOOSTER_PREV_PAGE", 2, 0xFF555555),   # BoosterPrev_Index_2Btn
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN", "BOOSTER_SHOP_MENU", 7, 0xFF4C4C4C),   # BoxBar_W54W90_34Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN", "EXCHANGER_RADIOBTN1", 8, "SKIN:TabBar_W72_8Btn"),   # ★★★ ЦВЕТ ИЗ СКИНА (2026-08-27, жалоба владельца «текста нету»)
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN", "EXCHANGER_RADIOBTN2", 7, "SKIN:TabBar_W72_7Btn"),   # ★★★ ЦВЕТ ИЗ СКИНА
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN", "EXCHANGER_RADIOBTN3", 9, "SKIN:TabBar_W72_8Btn_2"),   # ★★★ ЦВЕТ ИЗ СКИНА
    # ★★★ 13260: у скина `TabBar_W109_2Btn` НЕТ отдельной «выбранной» картинки — кадр 2 указывает
    #   почти в тот же светлый прямоугольник, что и кадр 0 (UV сдвинут на +0.004/+0.016), а
    #   `CaptionColor` кадра 2 при этом FFFFFFFF. Замер атласа `ne_wheeltaget2.png` 256×256:
    #   кадр 0 = пиксели (145,188)-(254,210), кадр 2 = (146,193)-(256,215) — ОБА сплошные белые.
    #   Итог в игре: активная вкладка = белый прямоугольник БЕЗ НАДПИСИ (жалоба владельца, скрин 1).
    #   Активной подписи задаём тёмный `ff4c4c4c` — тот же, что владелец уже утвердил для
    #   сливающихся вкладок остальных окон.
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN", "EXCHMY_CateSelBtn", 2, "SKIN:TabBar_W109_2Btn|ACTIVE=FF4C4C4C"),
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_SHELLITEM_STORE2_WIN", "SHELLITEM_STORE2_MENU", 9, 0xFFB5B5AB),   # BoxBar_W54W90_63Btn
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_StreetStall_Open_Win", "Stall_Open_InvenRadioBtn", 2, 0xFF4C4C4C),   # TabBar_W108_2Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Tutorial/WBP_Job_Guide_Win", "Job_Guide_SelectBtn", 3, 0xFF4C4C4C),   # TabBar_W108_3Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Tutorial/WBP_Tutorial_MENU_WIN", "Tutorial_MENU_Rtn", 3, 0xFFB5B5AB),   # BoxBar_COROR_3BTN
    ("/Game/AsdaAssets/UI/Player_UI/Unused/Chat/WBP_CHAT_BACKWIN", "CHAT_CHATTINGBUT", 6, 0xFFE1CFA9),   # TalkBar_Btn
    ("/Game/AsdaAssets/UI/Player_UI/Unused/Premium/WBP_Free_post_box_WIN", "Free_post_box_Radio", 2, 0xFF4C4C4C),   # TabBar_W108_2Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Player_UI/Unused/Premium/WBP_Free_post_box_go_WIN", "Free_post_box_go_radio", 2, 0xFF4C4C4C),   # TabBar_W108_2Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
    ("/Game/AsdaAssets/UI/Temp/WBP_Job_Guide_Win_Temp", "Job_Guide_SelectBtn", 3, 0xFF4C4C4C),   # TabBar_W108_3Btn  ★сливается → ff4c4c4c (решение владельца 2026-08-27)
]


def log(m):
    unreal.log("[вкладки] %s" % m)


# ═══ ★★★ ВИДИМОСТЬ ПОЛОСЫ — ИЗ РАЗМЕТКИ JUI, А НЕ ИЗ АССЕТА (правка 02.09.2026) ═════════════
#   ДЕФЕКТ, КОТОРЫЙ ЛЕЧИТСЯ (жалоба владельца 02.09.2026, окно 6400 «Сообщества»):
#   шаг ЖЁСТКО ставил каждой подписи `HIT_TEST_INVISIBLE`, не глядя на `Visible` самого контрола.
#   Импортёр (`asda_jui_generic_umg.py:808` `eff_hidden`) честно гасит `COLLAPSED` все виджеты
#   контрола с `Visible = "0"` — планы и кнопки, — но ПОСЛЕ него приходил этот шаг и ЗАЖИГАЛ
#   подпись обратно. У скрытой полосы `COMMUNITY_MENU_2` (13631) подпись `_Text` несёт ЗАПЕЧЁННЫЙ
#   импортёром `CaptionText` («Соулмейт») и лежит в ТОМ ЖЕ прямоугольнике, что подпись рабочей
#   полосы `COMMUNITY_MENU` (6401) — байт в байт, доказано дампом собранного окна
#   `Reports/headless_comm6400_dump.log`. Игрок видел «Соулмейт» поверх «Друг».
#
#   ★ ИСТОЧНИК ПРАВДЫ — РАЗМЕТКА `.JUI`, а не ассет: ассет уже испорчен прежними прогонами шага,
#     и «взять как есть» означало бы закрепить порчу.
#
#   ★★★ ПРАВИЛО (почему именно такое — каждое условие закрывает свой живой случай):
#     подписи полосы получают `COLLAPSED`, если
#       (1) ВСЕ объявления контрола с этим `ID` в разметке имеют `Visible = "0"`, И
#       (2) НИ У ОДНОГО из этих объявлений НЕТ детей.
#     Иначе — прежний `HIT_TEST_INVISIBLE`.
#
#     Условие (1) — про ДУБЛИ ИМЁН. `CHARAINFO_STMENU` объявлен ДВАЖДЫ (`gamemain_jui.ini`,
#       обход 161 `Visible=0` 3 плана и 162 `Visible=1` 2 плана). В `WidgetTree` имя уникально,
#       поэтому первому достаётся `CHARAINFO_STMENU_Text`, второму `..._Text_1` — и ОБА живые:
#       `AsdaCharaInfoSubsystem.cpp:481-482` держит их как `StatTab3Text` / `StatTab2Text`.
#       Погасить `_Text` значило бы убить подпись трёхвкладочного селектора характеристик.
#     Условие (2) — про СОБСТВЕННУЮ ГРУППУ. У контрола С ДЕТЬМИ импортёр делает контейнер
#       `{idc}_grp` и гасит ЕГО ЦЕЛИКОМ (`asda_jui_generic_umg.py:823-838`), а подписи лежат
#       ВНУТРИ. Порт показывает такую полосу, включая контейнер: `BOOSTER_SHOP_MENU` (15545) —
#       `AsdaBoosterShopSubsystem.cpp:760,779` берёт ровно `BOOSTER_SHOP_MENU_grp`. Поштучно
#       погашенная подпись после показа группы обратно НЕ вернулась бы.
#     Контрол, скрытый только ПРЕДКОМ (`CMTY_CLAN_3BTN` под `COMMUNITY_CLAN_WIN`,
#       `EXCHMY_CateSelBtn` под `EXCH_MYITEM`, `Job_Guide_SelectBtn` под `Job_Guide_TxtWin`),
#       сам объявлен ВИДИМЫМ: его гасит групповой контейнер предка, и подпись обязана следовать
#       за группой. Такие не трогаем — иначе клан-вкладки и вкладки «мои предметы» остались бы
#       без надписей.
#
#   ★ ПОРТ ЛЕЧЕНИЕ ПЕРЕЖИВАЕТ: подсистемы, которые эти полосы ПОКАЗЫВАЮТ, ставят видимость
#     ПОУЗЛОВО, включая `_Text*` — склад `AsdaWarehouseSubsystem.cpp:1184,1189`, премиум-витрина
#     `AsdaPremiumShopSubsystem.cpp:986-993`, аукцион `AsdaExchangeSubsystem.cpp:791` через
#     `CollectRadioSet` (в набор входят `_OPT<i>` и `_Text<n>`), книга навыков
#     `AsdaSkillWindowSubsystem.cpp:1175-1179` (12006 гасится всегда).
#
#   ⚠ ПЕРЕСЕЧЕНИЙ С `asda_ui_panel_own_visibility.py` НЕТ: тот шаг узлы `_Text<n>` и `_OPT<i>`
#     не трогает вовсе и полосы вкладок аукциона перечислены у него в исключениях.
import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
JUI_INIS = (
    os.path.join(_P.DECODED_JUI, "gamemain_jui.ini"),
    os.path.join(_P.DECODED_JUI, "commonwin_jui.ini"),
    os.path.join(_P.DECODED_JUI, "login_jui.ini"),
    os.path.join(_P.DECODED_JUI, "temp_jui.ini"),
)
_JUI_VIS = None      # кэш разбора разметки: idc → {"decls", "all_hidden", "any_kids"}


def jui_control_flags():
    """`idc` → сводка по ВСЕМ его объявлениям в четырёх `.JUI`.

    `decls` — сколько раз контрол объявлен, `all_hidden` — у всех ли `Visible = "0"`,
    `any_kids` — есть ли хоть у одного дети (значит, импортёр сделал ему `{idc}_grp`).
    Разбор ленивый и кэшируется: `gamemain_jui.ini` — 10 МБ, читать его на каждый контрол нельзя.
    """
    global _JUI_VIS
    if _JUI_VIS is not None:
        return _JUI_VIS
    want = set(idc for _p, idc, _n, _c in TABBARS)
    acc = {}

    def visit(node):
        if node.idc in want:
            rec = acc.setdefault(node.idc, {"decls": 0, "all_hidden": True, "any_kids": False})
            rec["decls"] += 1
            if node.visible != 0:
                rec["all_hidden"] = False
            if node.children:
                rec["any_kids"] = True
        for ch in node.children:
            visit(ch)

    for path in JUI_INIS:
        try:
            for root in JP.parse_jui(path):
                visit(root)
        except Exception as e:
            # Разметка — источник правды; без неё решение принимать нельзя.
            raise RuntimeError("не разобран %s: %s" % (path, e))
    _JUI_VIS = acc
    return acc


def caption_visibility(idc):
    """Целевая видимость подписей полосы `idc` (правило — в блоке выше)."""
    rec = jui_control_flags().get(idc)
    if rec and rec["all_hidden"] and not rec["any_kids"]:
        return unreal.SlateVisibility.COLLAPSED
    # Подпись видимой полосы мышь не ловит — иначе вкладка под ней теряет hover/press/click
    #   (закон `ui-button-hover-click-rule`).
    return unreal.SlateVisibility.HIT_TEST_INVISIBLE


# ═══ ★★★ ЦВЕТ ПОДПИСИ ВКЛАДКИ — ИЗ СКИНА, ПОКАДРОВО ═════════════════════════
#   Закон `jui-caption-states`: у скина-переключателя `CaptionColor` задан У КАЖДОГО КАДРА
#   (`JuiSkin.ini`), а рендер подписи (`ui_core.c:109129-109151`) берёт цвет АКТИВНОГО кадра.
#   Кадры: 0 покой, 1 наведение, 2/3 нажатие = ВЫБРАНО. У всех четырёх скинов аукциона
#   это `FF808080` / `FFAAAAAA` / `FFFFFFFF` — то есть сам оригинал рисовал СВЕТЛЫЙ текст, т.е. скин ТЁМНЫЙ.
#   Прежний замер брал цвет из `CaptionInfo[2]` КОНТРОЛЯ и ошибся — отсюда тёмный текст
#   на тёмной вкладке (жалоба владельца 2026-08-27 «текста нету либо он того же цвета»).
_SKINS = None


def step_visibility_nodes():
    """★ 07.10.2026 (ревизия `Reports/owner_1007/label_band_fixes_REVIEW.md`, МЕЛК-1): узлы, видимость которых
    ВЕДЁТ этот шаг. Читает `asda_ui_sync_own_visibility` — их он не раскрывает и не гасит.
    Возврат — [(путь окна, узел, причина)]: подписи `{idc}_Text`, `{idc}_Text2…N` каждой полосы `TABBARS`
    (их видимость шаг ставит по `caption_visibility`). Ассеты не грузятся — только разбор `.JUI`."""
    out = []
    for path, idc, nbtn, _argb in TABBARS:
        vis = caption_visibility(idc)
        for i in range(nbtn):
            name = (idc + "_Text") if i == 0 else ("%s_Text%d" % (idc, i + 1))
            out.append((path, name, "подпись вкладки, видимость по разметке JUI (%s)" % vis))
    return out


def skin_frame_caption_colors(skin_name):
    """(покой, наведение, выбрано) в ARGB из `JuiSkin.ini`; None, если скин их не задаёт."""
    global _SKINS
    if _SKINS is None:
        _SKINS = JP.parse_skins_full()
    sk = _SKINS.get(skin_name.lower())
    if not sk or not sk["planes"]:
        return None
    frames = sk["planes"][0]["frames"]

    def pick(i, fallback):
        fr = frames.get(i)
        c = fr["capcol"] if fr else None
        if not c:
            return fallback
        try:
            return int(c, 16)
        except ValueError:
            return fallback
    idle = pick(0, None)
    if idle is None:
        return None
    return (idle, pick(1, idle), pick(2, idle))


def apply_button_frame_colors(btn, cols):
    """Кладёт три кадровых цвета в `FButtonStyle` кнопки вкладки — туда же, куда их кладёт
    импортёр обычным кнопкам (`UAsdaUmgBuilder::ApplyButtonCaptionStates`). Оттуда их читает
    рантайм (`AsdaUi::ApplyTabCaptionState` с кнопками). Возвращает True, если что-то менялось."""
    if not btn or not isinstance(btn, unreal.Button):
        return False
    st = btn.get_editor_property("widget_style")
    want = [("normal_foreground", cols[0]), ("hovered_foreground", cols[1]),
            ("pressed_foreground", cols[2])]
    changed = False
    for prop, argb in want:
        col = argb_to_linear(argb)
        if not same_color(st.get_editor_property(prop), col):
            st.set_editor_property(prop, unreal.SlateColor(col))
            changed = True
    if changed:
        btn.set_editor_property("widget_style", st)
    return changed


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def argb_to_linear(argb):
    a = ((argb >> 24) & 0xFF) / 255.0
    r = srgb_to_linear((argb >> 16) & 0xFF)
    g = srgb_to_linear((argb >> 8) & 0xFF)
    b = srgb_to_linear(argb & 0xFF)
    return unreal.LinearColor(r, g, b, a)


canvas_slot = _UWC.canvas_slot_strict   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def slot_geo(w):
    """(anchors, alignment, offsets) слота-канваса или None."""
    s = canvas_slot(w)
    if s is None:
        return None
    lay = s.get_editor_property("layout_data")
    return (lay.get_editor_property("anchors"), lay.get_editor_property("alignment"),
            lay.get_editor_property("offsets"))


def same_margin(m, want):
    return (abs(m.left - want[0]) < EPS and abs(m.top - want[1]) < EPS
            and abs(m.right - want[2]) < EPS and abs(m.bottom - want[3]) < EPS)


def same_anchors(a, b):
    return (abs(a.minimum.x - b.minimum.x) < 1e-4 and abs(a.minimum.y - b.minimum.y) < 1e-4
            and abs(a.maximum.x - b.maximum.x) < 1e-4 and abs(a.maximum.y - b.maximum.y) < 1e-4)


def same_color(sc, col):
    """Сравнение SlateColor подписи с целевым LinearColor (по specified_color)."""
    try:
        c = sc.get_editor_property("specified_color")
    except Exception:
        return False
    return (abs(c.r - col.r) < 1e-3 and abs(c.g - col.g) < 1e-3
            and abs(c.b - col.b) < 1e-3 and abs(c.a - col.a) < 1e-3)



def planes_draw_top(nodes, idc):
    """★ Верхняя точка отрисовки ПЛАНОВ полосы `idc` — пара (максимальный ZOrder, позиция ребёнка).

    Порядок отрисовки в канвасе задаётся сначала `ZOrder`, а при равном `ZOrder` — позицией
    ребёнка в списке. Возвращаем обе величины, чтобы понять, перекрыта ли подпись плашкой.
    Планы импортёр называет `{idc}_<juiIdx>_<plane>`.
    """
    pat = re.compile(r"^" + re.escape(idc) + r"_(\d+)_(\d+)$")
    best = None
    for name, w in nodes.items():
        if not pat.match(name):
            continue
        s = canvas_slot(w)
        if s is None:
            continue
        cur = (int(s.get_editor_property("z_order")), child_index(w))
        best = cur if best is None else max(best, cur)
    return best


def child_index(w):
    """Позиция виджета среди детей его родителя (−1, если родителя нет)."""
    par = w.get_parent()
    if not par:
        return -1
    for i in range(par.get_children_count()):
        if par.get_child_at(i) == w:
            return i
    return -1


def apply_control(bp, root, idc, nbtn, col, dry, frame_cols=None, vis=None):
    """Приводит подписи ОДНОГО переключателя. Возвращает (добавлено, поправлено, пропущено).

    `frame_cols` — тройка ARGB кадровых цветов скина (покой/наведение/выбрано); если задана,
    шаг кладёт её в `FButtonStyle` кнопок `{idc}_OPT<i>` — там её читает рантайм.
    `vis` — целевая видимость подписей (`caption_visibility`, источник правды — разметка JUI)."""
    if vis is None:
        vis = caption_visibility(idc)
    nodes = {}
    for w in walk(root):
        nodes.setdefault(w.get_name(), w)     # ★ имя может встретиться дважды (общий узел обхода)
    added = fixed = skipped = 0

    btn0 = nodes.get("%s_OPT0" % idc)
    g0 = slot_geo(btn0)
    if g0 is None:
        log("  %s: нет кнопки _OPT0 или она не в канвасе — пропуск контрола" % idc)
        return (0, 0, nbtn)

    # ★ ВЕРТИКАЛЬНЫЙ СДВИГ ПОДПИСИ — НЕ КОНСТАНТА, а собственный сдвиг окна: разница
    #   «верх подписи − верх кнопки», как её посчитал импортёр из `CaptionInfo`/выравнивания
    #   (у окон он разный: 3.0 у склада и обмена, 3.5 у книги навыков). Так подписи 2…N встают
    #   ровно на ту же высоту, что и первая, и первая при повторном прогоне НЕ дёргается.
    base = nodes.get("%s_Text" % idc)
    gb = slot_geo(base)
    dy = (gb[2].top - g0[2].top) if gb else 3.0
    hh = gb[2].bottom if gb else g0[2].bottom
    just = base.get_editor_property("justification") if base else unreal.TextJustify.CENTER
    parent = (base.get_parent() if base else None) or btn0.get_parent() or root

    # ★★★ ПОДПИСЬ ОБЯЗАНА РИСОВАТЬСЯ ПОВЕРХ ПЛАШКИ СВОЕЙ ВКЛАДКИ (2026-08-28).
    #   При равном `ZOrder` порядок в канвасе решает позиция ребёнка, а импортёр кладёт
    #   `{idc}_Text` РАНЬШЕ планов — если контрол объявлен в `.JUI` дважды, нулевая подпись
    #   принадлежит ПЕРВОМУ объявлению и оказывается под планами ВТОРОГО. В окне аукциона
    #   подпись «Кольцо» (ребёнок 17) уходила под плашку кадра 2 (дети 35…41), и первая
    #   вкладка выглядела пустой; подписи 2…N (дети 76…81) лежат выше планов и видны.
    planes_top = planes_draw_top(nodes, idc)

    for i in range(nbtn):
        btn = nodes.get("%s_OPT%d" % (idc, i))
        g = slot_geo(btn)
        if g is None:
            log("  %s: нет кнопки _OPT%d (или не в канвасе) — пропуск" % (idc, i))
            skipped += 1
            continue
        b_anch, b_align, b_off = g
        want = (b_off.left, b_off.top + dy, b_off.right, hh)   # X, Y, ширина, высота
        name = (idc + "_Text") if i == 0 else ("%s_Text%d" % (idc, i + 1))
        tb = nodes.get(name)

        if tb is None:
            if dry:
                added += 1
                continue
            tb = unreal.AsdaUmgBuilder.add_text(
                bp, parent, name, unreal.Vector2D(want[0], want[1]), unreal.Vector2D(want[2], want[3]),
                "",              # ★ текст ставит РАНТАЙМ по id локализации — выдумывать нельзя
                FONT_PX, col, 1, False, True)
            if not tb:
                log("  %s: не удалось создать %s" % (idc, name))
                skipped += 1
                continue
            nodes[name] = tb
            tb.set_editor_property("justification", just)
            added += 1

        if dry:
            # в сухом прогоне у существующих узлов только СЧИТАЕМ расхождения
            gt = slot_geo(tb)
            sd = canvas_slot(tb)
            # ★ сухой прогон обязан видеть и подпись, уходящую под плашку
            z_bad = (sd is not None and planes_top is not None
                     and (int(sd.get_editor_property("z_order")), child_index(tb)) < planes_top)
            if gt is None or z_bad or not same_margin(gt[2], want) or not same_anchors(gt[0], b_anch) \
               or not same_color(tb.get_editor_property("color_and_opacity"), col) \
               or tb.get_editor_property("visibility") != vis:
                fixed += 1
                if tb.get_editor_property("visibility") != vis:
                    log("    [СУХОЙ] %s: видимость %s → %s"
                        % (name, tb.get_editor_property("visibility"), vis))
            continue

        changed = False
        s = canvas_slot(tb)
        if s is None:
            log("  %s: подпись %s не в канвасе — геометрию не трогаю" % (idc, name))
        else:
            lay = s.get_editor_property("layout_data")
            if not same_margin(lay.get_editor_property("offsets"), want) \
               or not same_anchors(lay.get_editor_property("anchors"), b_anch):
                # ★ Анкоры/выравнивание берём У КНОПКИ: у 30 окон из 42 они не (0,0) —
                #   `ApplyResponsiveSlot` привязал группу к правому краю/центру экрана, и новая
                #   подпись с анкором по умолчанию уехала бы от своей вкладки.
                lay.set_editor_property("anchors", b_anch)
                lay.set_editor_property("alignment", b_align)
                lay.set_editor_property("offsets", unreal.Margin(want[0], want[1], want[2], want[3]))
                s.set_editor_property("layout_data", lay)
                changed = True
            if s.get_editor_property("auto_size"):
                s.set_editor_property("auto_size", False)
                changed = True
            # ★ Подпись поднимаем ТОЛЬКО если она реально уходит под плашку (разбор выше).
            #   Подпись, которая и так рисуется выше планов, не трогаем — лишний ZOrder
            #   поднял бы её и над теми окнами, что ложатся поверх полосы вкладок.
            if planes_top is not None:
                mine = (int(s.get_editor_property("z_order")), child_index(tb))
                if mine < planes_top:
                    s.set_editor_property("z_order", planes_top[0] + 1)
                    changed = True
        if not same_color(tb.get_editor_property("color_and_opacity"), col):
            tb.set_editor_property("color_and_opacity", unreal.SlateColor(col))
            changed = True
        # ★★★ ВИДИМОСТЬ — ИЗ РАЗМЕТКИ (`caption_visibility`, разбор в блоке выше):
        #   у видимой полосы `HIT_TEST_INVISIBLE` (подпись не ловит мышь, закон
        #   `ui-button-hover-click-rule`), у скрытой — `COLLAPSED` (подпись-призрак не рисуется).
        if tb.get_editor_property("visibility") != vis:
            tb.set_editor_property("visibility", vis)
            changed = True
        # ★★★ КАДРОВЫЕ ЦВЕТА СКИНА → в стиль кнопки (читает `AsdaUi::ApplyTabCaptionState`).
        if frame_cols and apply_button_frame_colors(btn, frame_cols):
            changed = True
        if changed:
            fixed += 1
    return (added, fixed, skipped)


def main():
    dry = bool(os.environ.get("ASDA_TABCAP_DRYRUN"))
    B = unreal.AsdaUmgBuilder
    t_add = t_fix = t_skip = t_win = 0
    # ★ Сводка по разметке печатается ДО работы: по ней видно, какие полосы будут погашены,
    #   и её же кладём в отчёт (доказательство «видимость взята из данных, а не подобрана»).
    flags = jui_control_flags()
    hid = [i for i in sorted(flags) if flags[i]["all_hidden"] and not flags[i]["any_kids"]]
    log("разметка JUI: контролов в таблице %d, найдено в данных %d, подписи гасим у %d — %s"
        % (len(TABBARS), len(flags), len(hid), ", ".join(hid)))
    for i in sorted(flags):
        f = flags[i]
        log("  %-30s объявлений %d, все скрыты %s, есть дети %s → подписи %s"
            % (i, f["decls"], "да" if f["all_hidden"] else "нет",
               "да" if f["any_kids"] else "нет", caption_visibility(i)))
    for path, idc, nbtn, argb in TABBARS:
        if not EAL.does_asset_exist(path):
            log("НЕТ АССЕТА %s" % path)
            t_skip += nbtn
            continue
        bp = EAL.load_asset(path)
        root = B.get_root_canvas(bp)
        if not root:
            log("%s: нет корневого канваса" % path)
            t_skip += nbtn
            continue
        # ★ Строка может задавать ЦВЕТ ИЗ СКИНА ("SKIN:<имя скина>") вместо ARGB из `CaptionInfo`.
        frame_cols = None
        if isinstance(argb, str) and argb.startswith("SKIN:"):
            # ★★★ ПЕРЕОПРЕДЕЛЕНИЕ ЦВЕТА АКТИВНОГО КАДРА: "SKIN:<скин>|ACTIVE=AARRGGBB".
            #   Нужно там, где у скина НЕТ отдельной «выбранной» картинки и кадр 2 остаётся
            #   СВЕТЛОЙ плашкой, а `CaptionColor` кадра 2 при этом БЕЛЫЙ — то есть белое по белому.
            #   Это дефект ДАННЫХ оригинала, и правится он по закону `fix-original-defects`.
            skin_spec = argb[5:]
            active_override = None
            if "|ACTIVE=" in skin_spec:
                skin_spec, act = skin_spec.split("|ACTIVE=", 1)
                active_override = int(act, 16)
            argb = "SKIN:" + skin_spec
            frame_cols = skin_frame_caption_colors(skin_spec)
            if frame_cols and active_override is not None:
                frame_cols = (frame_cols[0], frame_cols[1], active_override)
            if not frame_cols:
                log("%s / %s: у скина %s нет CaptionColor по кадрам — пропуск" % (path, idc, argb[5:]))
                t_skip += nbtn
                continue
            log("%s: цвета из скина %s — покой %08X, наведение %08X, выбрано %08X"
                % (idc, argb[5:], frame_cols[0], frame_cols[1], frame_cols[2]))
            cap_argb = frame_cols[0]
        else:
            cap_argb = argb
        added, fixed, skipped = apply_control(bp, root, idc, nbtn, argb_to_linear(cap_argb), dry, frame_cols)
        if (added or fixed) and not dry:
            # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
            B.compile_and_save(bp)
            if not EAL.save_loaded_asset(bp, False):
                raise RuntimeError("не сохранён %s" % path)
            t_win += 1
        t_add += added; t_fix += fixed; t_skip += skipped
        if added or fixed or skipped:
            log("%s / %s: добавлено %d, поправлено %d, пропущено %d%s"
                % (path.rsplit("/", 1)[-1], idc, added, fixed, skipped, "  [СУХОЙ]" if dry else ""))
    log("ГОТОВО%s: контролов %d, добавлено подписей %d, поправлено %d, пропущено %d, окон сохранено %d"
        % ("  [СУХОЙ ПРОГОН]" if dry else "", len(TABBARS), t_add, t_fix, t_skip, t_win))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[вкладки] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
