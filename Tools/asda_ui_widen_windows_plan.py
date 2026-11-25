# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_widen_windows_plan.py — ГЕНЕРАТОР таблицы шага `asda_ui_widen_windows` (07.10.2026, отчёт
#   `Reports/owner_1007/widen_windows.md`). Без редактора: читает полный структурный дамп окон (JSON, снят коммандлетом
#   с собранных ассетов) и ПРАВИЛА расширения окон ниже, пишет закрытую таблицу «узел → слот было/стало»
#   `Tools/data/ui_widen_windows.json`. Сам шаг таблицу только ПРИМЕНЯЕТ (и сверяет) — он не вычисляет геометрию.
#
#   ПРАВИЛО ВЛАДЕЛЬЦА 07.10.2026 (память widen-windows-whole-keep-symmetry): русский текст не помещается → окно
#   расширяется ЦЕЛИКОМ и СИММЕТРИЧНО (левая часть −Δ/2, правая +Δ/2 — окно в корне привязано к центру), всё,
#   что тянется на всю ширину (рамка-девятисрез, шапка, фоны, линии, строки списка), растёт на Δ (растягиваются
#   только СРЕДНИЕ куски девятисреза — углы и края своего размера), квадратные элементы (ячейки, иконки,
#   кнопки) НЕ растягиваются — смещаются. Иерархия JUI расплющена: каждый зависимый узел двигается ЯВНО.
#
# Запуск:  python Tools\asda_ui_widen_windows_plan.py <дамп.json> [--only WBP_a,WBP_b] [--alt-dump <дамп ассетов.json>] [--write]
#   без --write — только печать плана (что и как сдвигается) и проверки.
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "data", "ui_widen_windows.json")

# ── ПРАВИЛА ── (регулярное выражение ПОЛНОГО имени узла, операция, значение)
#   move dx       — L += dx
#   grow dw       — W += dw
#   set (L,T,W,H) — слот целиком (+ необязательные "font"/"clip" в WINDOWS[...]["text"])
#   fn  f         — слот = f([L,T,W,H]) (раскладка по таблице, напр. перестройка сетки столбцов)
#   Необязательный 4-й элемент правила — регулярное выражение ИМЕНИ РОДИТЕЛЯ (для неуникальных имён импортёра).
#   Правила складываются (move и grow на одном узле допустимы). Узел, не попавший ни под одно правило, не меняется.
_ROWS = r"(0[1-7])"
WINDOWS = {
    # ═══ 8200 «Поиск соулмейта» (Find_Soulmate_Win) ═══ Δ = 28: столбец D6 «Сравнение» (loc 110809, 56 px кеглем 8)
    #   36 → 64 px. Столбец «влечения» — сердечки 32×32 (квадраты) — не тянутся, центрируются (+14). Всё правее
    #   столбца (D7 «Знак зодиака», ячейки созвездия) и правая часть окна — +28 относительно левого края.
    "/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_Find_Soulmate_Win": {
        "window": "Find_Soulmate_Win (8200)", "delta": 28,
        "rules": [
            # рамка окна — девятисрез из 9 картинок (углы 16×16)
            (r"Find_Soulmate_Win_0_[036]", "move", -14),
            (r"Find_Soulmate_Win_0_[147]", "move", -14), (r"Find_Soulmate_Win_0_[147]", "grow", 28),
            (r"Find_Soulmate_Win_0_[258]", "move", 14),
            (r"Find_SMate_CloseBtn", "move", 14),                                   # крестик у правого края
            (r"Find_SMate_Line1_2_0", "move", -14), (r"Find_SMate_Line1_2_0", "grow", 28),
            (r"Find_SMate_Move", "move", -14), (r"Find_SMate_Move", "grow", 28),   # заголовок + ручка
            (r"Find_SMate_Move_144_DRAG", "move", -14), (r"Find_SMate_Move_144_DRAG", "grow", 28),
            # строка фильтра: подложка на всю ширину, кнопка «Поиск» у правого края
            (r"Find_SMate_GenderSel_Base_grp", "move", -14), (r"Find_SMate_GenderSel_Base_grp", "grow", 28),
            (r"Find_SMate_GenderSel_Base_145_0", "grow", 28),
            (r"Find_SMate_FindBtn", "move", 28), (r"Find_SMate_FindBtn_Text", "move", 28),
            # панель списка (девятисрез, углы 11)
            (r"Find_SMate_LisWin_grp", "move", -14), (r"Find_SMate_LisWin_grp", "grow", 28),
            (r"Find_SMate_LisWin_3_[147]", "grow", 28), (r"Find_SMate_LisWin_3_[258]", "move", 28),
            (r"Find_SMate_ListBase_grp", "grow", 28),
            # строки 01..07: подложка/рамка выбора/линия во всю ширину, группа колонок, колонка «влечения»
            (r"Find_SMate_ListBase%s_grp" % _ROWS, "grow", 28),
            (r"Find_SMate_ListBase%s_\d+_0" % _ROWS, "grow", 28),
            (r"Find_SMate_ListSel%s_\d+_0" % _ROWS, "grow", 28),
            (r"Box_WhiteOt30_18_0", "grow", 28),
            (r"Find_SMate_ListSex%s_grp" % _ROWS, "grow", 28),
            (r"Find_SMate_ListName%s_grp" % _ROWS, "grow", 28),
            (r"Black_grp(_(2|4|6|8|10|12))?", "grow", 28),                          # колонка «влечения» строк 1..7
            (r"Black_(11|25|38|51|64|77|90)_0", "grow", 28),                        # её чёрная подложка
            (r"Find_SMate_ListInt%s_MARK[123]_\d+_0" % _ROWS, "move", 14),          # сердечки 32×32 — по центру
            (r"Black_grp_(1|3|5|7|9|11|13)", "move", 28),                           # колонка созвездия строк 1..7
            # рамка выбора строки (девятисрез 3 px)
            (r"Find_SMate_ListSel_97_[16]", "grow", 28), (r"Find_SMate_ListSel_97_[247]", "move", 28),
            # строка заголовков столбцов D1..D8
            (r"Box_WhiteOt20_grp", "grow", 28), (r"Box_WhiteOt20_98_0", "grow", 28),
            (r"Box_BlueOt20_110_0", "grow", 28),
            (r"Find_SMate_List_D1_grp", "grow", 28), (r"Find_SMate_List_D3_grp", "grow", 28),
            (r"Find_SMate_List_D6_grp", "grow", 28), (r"Find_SMate_List_D6_104_0", "grow", 28),
            (r"Find_SMate_List_D6", "set", (0.0, 10.5, 64.0, 35.0)),               # как соседний D5: центр, кегль 8
            (r"HEART_[123]_\d+_0", "move", 14),
            (r"Find_SMate_List_D7_grp", "move", 28),
            # пейджер и сноска — по центру панели
            (r"Find_SMate_Slash_grp", "move", 14),
            (r"Find_SMate_LisWin_TXT00", "move", 14), (r"NoImage_118_HIT", "move", 14),
            # блок «Обо мне» (девятисрез), текст «о себе» сохраняет правый отступ
            (r"Find_SMate_MyIntro_grp", "move", -14), (r"Find_SMate_MyIntro_grp", "grow", 28),
            (r"Find_SMate_MyIntro_140_[147]", "grow", 28), (r"Find_SMate_MyIntro_140_[258]", "move", 28),
            (r"Find_SMate_MyIntroTxt", "grow", 28),
            # кнопки внизу (Find_SMate_BtnBase_grp) — по центру окна, остаются на месте
        ],
        # Узлы, которые до расширения могли быть в ДВУХ видах: импортёра и после прежнего шага (кегль 7, поле влево —
        #   `asda_ui_column_header_fit`, случай снят из его таблицы 07.10). (L,T,W,H), кегль.
        "alt_before": {"Find_SMate_List_D6": [((0.0, 10.5, 36.0, 35.0), 8.0, "INHERIT")]},
        "text": {"Find_SMate_List_D6": {"font": 8.0}},
    },
    # ═══ 56450 «Доклад об охоте» (Hunt report_Win) ═══ Δ = 40: заголовок loc 110210 «доклад о результате охоты на
    #   монстров» 205 px кеглем 8 при ширине окна 226 уходил под крестик. Поле заголовка — симметрично относительно
    #   центра окна до крестика (−2 px): 214 px. Сетка ячеек 32×32, плашки результата и кнопки — по центру (+20).
    "/Game/AsdaAssets/UI/Player_UI/Character/WBP_Hunt_report_Win": {
        "window": "Hunt report_Win (56450)", "delta": 40,
        "rules": [
            (r"Hunt_report_Win_0_[036]", "move", -20),
            (r"Hunt_report_Win_0_[147]", "move", -20), (r"Hunt_report_Win_0_[147]", "grow", 40),
            (r"Hunt_report_Win_0_[258]", "move", 20),
            # шапка (три куска: левый 53 с заклёпкой, средний тянется, правый 53)
            (r"WinSm_CBar_grp", "move", -20), (r"WinSm_CBar_grp", "grow", 40),
            (r"WinSm_CBar_1_0", "grow", 40), (r"WinSm_RBar_4_0", "move", 40),
            # заголовок + ручка + крестик
            (r"Hunt_report_Text_Move_grp", "move", -20), (r"Hunt_report_Text_Move_grp", "grow", 40),
            (r"Hunt_report_Text_Move_94_DRAG", "grow", 40),
            (r"Hunt_report_X", "move", 40),
            (r"Hunt_report_Text_Move", "set", (25.0, 5.0, 214.0, 24.0)),
            # каменная плашка результата (края 16 своего размера, середина тянется; содержимое по центру)
            (r"NoImage_grp", "move", -20), (r"NoImage_grp", "grow", 40),
            (r"Ston_CBase_grp", "grow", 40), (r"Ston_CBase_6_0", "grow", 40), (r"Ston_RBase_8_0", "move", 40),
            (r"Box_Black_Ot75_grp", "move", 20), (r"Box_Black_Ot75_grp_1", "move", 20), (r"StrengStonL_grp", "move", 20),
            # строка результата (текст по центру)
            (r"Hunt_report_Text1", "move", -20), (r"Hunt_report_Text1", "grow", 40),
            (r"Hunt_report_Text1_96_HIT", "move", -20), (r"Hunt_report_Text1_96_HIT", "grow", 40),
            # панель ячеек (девятисрез, углы 11); сетка 6×5 ячеек 32×32 и рамка выбора — по центру
            (r"Win_Tab_grp", "move", -20), (r"Win_Tab_grp", "grow", 40),
            (r"Win_Tab_20_[147]", "grow", 40), (r"Win_Tab_20_[258]", "move", 40),
            (r"NoImage_grp_1", "move", 20), (r"NoImage_icon_grp", "move", 20), (r"Hunt_report_Select_83_0", "move", 20),
            # кнопки «Отнести знаки» / «отмена» (Btn_CBase_grp, Btn_CBase_grp_1) — по центру окна, на месте
        ],
        # Заголовок: вид импортёра (поле во всю шапку, без клипа) или после `asda_ui_icon_caption_fit` Д-2 (поле до
        #   крестика 199, клип) — случай снят из его таблицы TITLE_CLIPS 07.10.
        "alt_before": {"Hunt_report_Text_Move": [((0.0, 5.0, 224.0, 24.0), 8.0, "INHERIT")]},
        "text": {"Hunt_report_Text_Move": {"clip": "CLIP_TO_BOUNDS"}},
    },

    # ══════════════════ П.2: ПОДПИСИ КНОПОК (замер путём игрока, текст — из кода) ══════════════════
    # Подпись кнопки со значком в виде прежнего шага `asda_ui_icon_caption_fit` (правее значка +1 px, до правого края
    #   −3 px) — теперь КЕГЛЕМ 8: кнопка шире ровно настолько, чтобы самая длинная строка кода входила с запасом ≥ 2 px.
    #   Пары/тройки кнопок остаются СИММЕТРИЧНЫМИ (обе шире одинаково, центр пары на месте).

    # ═══ 1520 TWOBUT_MSGBOX ═══ самая длинная подпись BUT00 «Дом для питомца» 88 px → поле 92, обе кнопки +14 наружу
    "/Game/AsdaAssets/UI/Player_UI/Common/WBP_TWOBUT_MSGBOX": {
        "window": "TWOBUT_MSGBOX (1520)", "delta": 0,
        "rules": [
            (r"Twobut_But00_Base_grp", "move", -14), (r"Twobut_But00_Base_grp", "grow", 14),
            (r"Twobut_But00_Base_5_0", "grow", 14), (r"Twobut_But00_RBase_7_0", "move", 14),
            (r"TWOBUT_BUT00", "grow", 14),
            (r"TWOBUT_BUT00_Text", "set", (22.0, 7.5, 92.0, 23.0)),
            (r"Twobut_But01_Base_grp", "grow", 14),
            (r"Twobut_But01_Base_10_0", "grow", 14), (r"Twobut_But01_RBase_12_0", "move", 14),
            (r"TWOBUT_BUT01", "grow", 14), (r"TWOBUT_BUT01_Text", "grow", 14),
        ],
        "alt_before": {"TWOBUT_BUT00_Text": [((6.0, 7.5, 97.0, 23.0), 8.0, "INHERIT")]},
        "text": {"TWOBUT_BUT00_Text": {"font": 8.0}},
    },
    # ═══ 79300 Town_as_WIN ═══ Δ = 40: «Установить ставку» 96 px → поле 100, обе кнопки +22 наружу (центр пары на
    #   месте); окно и панель +40 симметрично, чтобы отступы кнопок от рамки панели остались как в оригинале (38/31 →
    #   36/29), а не сжались до 16/9. Строки ставок и ползунок — на месте (по центру), текст справа от значка шире.
    "/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_Town_as_WIN": {
        "window": "Town_as_WIN (79300)", "delta": 40,
        "rules": [
            (r"Town_as_WIN_0_[036]", "move", -20),
            (r"Town_as_WIN_0_[147]", "move", -20), (r"Town_as_WIN_0_[147]", "grow", 40),
            (r"Town_as_WIN_0_[258]", "move", 20),
            (r"WinSm_CBar_grp", "move", -20), (r"WinSm_CBar_grp", "grow", 40),
            (r"WinSm_CBar_1_0", "grow", 40), (r"WinSm_RBar_4_0", "move", 40),
            (r"Town_as_movetext_grp", "move", -20), (r"Town_as_movetext_grp", "grow", 40),
            (r"Town_as_movetext|Town_as_movetext_16_HIT", "grow", 40), (r"Town_as_x", "move", 40),
            (r"Win_Tab_grp", "move", -20), (r"Win_Tab_grp", "grow", 40),
            (r"Win_Tab_5_[147]", "grow", 40), (r"Win_Tab_5_[258]", "move", 40),
            (r"Town_as_text_[24]_grp", "move", 20),
            (r"Town_as_mark_grp", "move", -20),
            (r"Town_as_text_1|Town_as_text_1_20_HIT", "move", -20), (r"Town_as_text_1|Town_as_text_1_20_HIT", "grow", 40),
            (r"Btn_CBase_grp", "move", -22), (r"Btn_CBase_grp", "grow", 22),
            (r"Btn_CBase_11_0", "grow", 22), (r"Btn_RBase_13_0", "move", 22),
            (r"Town_as_okbtn", "grow", 22),
            (r"Town_as_okbtn_Text", "set", (22.0, 7.5, 100.0, 23.0)),
            (r"Btn_CBase_grp_1", "grow", 22),
            (r"Btn_CBase_21_0", "grow", 22), (r"Btn_RBase_23_0", "move", 22),
            (r"Town_as_nobtn", "grow", 22), (r"Town_as_nobtn_Text", "grow", 22),
        ],
        "alt_before": {"Town_as_okbtn_Text": [((6.0, 7.5, 97.0, 23.0), 8.0, "INHERIT")]},
        "text": {"Town_as_okbtn_Text": {"font": 8.0}},
    },
    # ═══ CLAN_MAKING_WIN ═══ Δ = 16: «Создание клана» 81 px → поле 86, обе кнопки +8 наружу; окно +16 симметрично —
    #   отступы кнопок от рамки и от уголка-значка справа (15/17 px) как в оригинале. Поля ввода/стоимости — по центру.
    "/Game/AsdaAssets/UI/Player_UI/Social/WBP_CLAN_MAKING_WIN": {
        "window": "CLAN_MAKING_WIN", "delta": 16,
        "rules": [
            (r"CLAN_MAKING_WIN_0_[036]", "move", -8),
            (r"CLAN_MAKING_WIN_0_[147]", "move", -8), (r"CLAN_MAKING_WIN_0_[147]", "grow", 16),
            (r"CLAN_MAKING_WIN_0_[258]", "move", 8),
            (r"CLAN_MAKING_WIN_0_DRAG", "move", -8), (r"CLAN_MAKING_WIN_0_DRAG", "grow", 16),
            (r"CLAN_MAKING_Line_1_0", "move", -8), (r"CLAN_MAKING_Line_1_0", "grow", 16),
            (r"CLAN_MAKE_BTN_Base_grp", "move", -8), (r"CLAN_MAKE_BTN_Base_grp", "grow", 8),
            (r"CLAN_MAKE_BTN_Base_3_0", "grow", 8), (r"CLAN_MAKE_BTN_RBase_5_0", "move", 8),
            (r"CLAN_MAKE_BTN", "grow", 8),
            (r"CLAN_MAKE_BTN_Text", "set", (22.0, 7.5, 86.0, 23.0)),
            (r"CLAN_CANCEL_BTN_Base_grp", "grow", 8),
            (r"CLAN_CANCEL_BTN_Base_8_0", "grow", 8), (r"CLAN_CANCEL_BTN_RBase_10_0", "move", 8),
            (r"CLAN_CANCEL_BTN", "grow", 8), (r"CLAN_CANCEL_BTN_Text", "grow", 8),
        ],
        "alt_before": {"CLAN_MAKE_BTN_Text": [((3.0, 7.5, 100.0, 23.0), 8.0, "INHERIT")]},
        "text": {"CLAN_MAKE_BTN_Text": {"font": 8.0}},
    },
    # ═══ 8400 SMate_ReqAnsr_Win ═══ Δ = 24: «Написать шёпотом» 98 px → кнопка шёпота +24 (поле 102); «Разрешить» и
    #   «Отклонить» +12 (зазоры между кнопками и 17 px до уголка-значка справа — как в оригинале), окно +24 симметрично.
    "/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SMate_ReqAnsr_Win": {
        "window": "SMate_ReqAnsr_Win (8400)", "delta": 24,
        "rules": [
            (r"SMate_ReqAnsr_Win_0_[036]", "move", -12),
            (r"SMate_ReqAnsr_Win_0_[147]", "move", -12), (r"SMate_ReqAnsr_Win_0_[147]", "grow", 24),
            (r"SMate_ReqAnsr_Win_0_[258]", "move", 12),
            (r"SMate_ReqAnsr_Win_0_DRAG", "move", -12), (r"SMate_ReqAnsr_Win_0_DRAG", "grow", 24),
            (r"SMate_ReqAnsr_Img1_1_0", "move", -12),
            (r"SMate_ReqAnsr_Title", "move", -12), (r"SMate_ReqAnsr_Title", "grow", 24),
            (r"SMate_ReqAnsr_Name_grp", "move", -12), (r"SMate_ReqAnsr_Name_grp", "grow", 24),
            (r"SMate_ReqAnsr_Name", "grow", 24), (r"SMate_ReqAnsr_Intro", "grow", 24),
            (r"SMate_ReqAnsr_WhisBtn_Base_grp", "move", -12), (r"SMate_ReqAnsr_WhisBtn_Base_grp", "grow", 24),
            (r"SMate_ReqAnsr_WhisBtn_Base_12_0", "grow", 24), (r"SMate_ReqAnsr_WhisBtn_RBase_14_0", "move", 24),
            (r"SMate_ReqAnsr_WhisBtn", "grow", 24),
            (r"SMate_ReqAnsr_WhisBtn_Text", "set", (22.0, 7.5, 102.0, 23.0)),
            (r"SMate_ReqAnsr_OKBtn_Base_grp", "move", 12), (r"SMate_ReqAnsr_CanBtn_Base_grp", "move", 12),
        ],
        "alt_before": {"SMate_ReqAnsr_WhisBtn_Text": [((6.0, 7.5, 97.0, 23.0), 8.0, "INHERIT")]},
        "text": {"SMate_ReqAnsr_WhisBtn_Text": {"font": 8.0}},
    },
    # ═══ 6400 COMMUNITY_WIN ═══ окно большое (2016 узлов, 6 вкладок) и место рядом с кнопками ЕСТЬ — расширяются
    #   только поля кнопок/столбцов внутри своих панелей, симметрия панелей сохраняется:
    #   • «Сообщение»/«Разорвать Дружбу» (loc 12170/8045, 94 px в 89) — обе кнопки шапок блоков 89 → 100 влево
    #     (правый край у края полосы, слева пустое поле полосы);
    #   • «Передача полномочий главы клана» (loc 17203, 180 px в 137) — кнопка +50, по центру панели;
    #   • «Выйти из клана» (78 px) — кнопка +4 влево, кегль 8; «Пригласить в отряд» (100 px) — +6, пара по центру;
    #   • столбец «Уровень» (loc 17163, 43 px в 40) в «Присвоении рангов» и «Передаче полномочий» — граница столбцов
    #     «Имя»/«Уровень» на 6 px влево (имя 152 → 146, самые длинные имена ≤ 120 px), «Очки» не меняются; кегль 8.
    "/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN": {
        "window": "COMMUNITY_WIN (6400)", "delta": 0,
        "rules": [
            (r"Soul_(MessageBtn|CutoffBtn)(_Text)?", "move", -11), (r"Soul_(MessageBtn|CutoffBtn)(_Text)?", "grow", 11),
            (r"CmtyClan_Sub5_ConBtn_grp", "move", -25), (r"CmtyClan_Sub5_ConBtn_grp", "grow", 50),
            (r"CmtyClan_Sub5_ConBtn(_Text)?", "grow", 50),
            (r"BtnBase_447_[16]", "grow", 50), (r"BtnBase_447_[247]", "move", 50),
            (r"Cmty_ClanInfo_BtnBase_grp", "move", -4), (r"Cmty_ClanInfo_BtnBase_grp", "grow", 4),
            (r"Cmty_ClanInfo_BtnBase_209_0", "grow", 4), (r"Cmty_ClanInfo_RBtnBase_211_0", "move", 4),
            (r"CMTY_CLAN_SEC_BTN", "grow", 4),
            (r"CMTY_CLAN_SEC_BTN_Text", "set", (22.0, 7.5, 82.0, 23.0)),
            (r"Cmty_Who_WhisBtnBase_grp", "move", -3),
            (r"Cmty_Who_FaddBtnBase_grp", "move", -3), (r"Cmty_Who_FaddBtnBase_grp", "grow", 6),
            (r"Cmty_Who_FaddBtnBase_648_0", "grow", 6), (r"Cmty_Who_FaddBtnRBase_650_0", "move", 6),
            (r"Cmty_Who_FaddBtn", "grow", 6),
            (r"Cmty_Who_FaddBtn_Text", "set", (22.0, 7.5, 104.0, 23.0)),
            # столбцы «Имя | Уровень | Очки»: заголовки
            (r"CmtyClan_Sub3_Txt01_347_0|CmtyClan_Sub3_Txt01|CmtyClan_Sub5_T1_412_0|CmtyClan_Sub5_T1", "grow", -6),
            (r"CmtyClan_Sub3_Txt02_grp|CmtyClan_Sub5_T2_grp", "move", -6),
            (r"CmtyClan_Sub3_Txt02_grp|CmtyClan_Sub5_T2_grp", "grow", 6),
            (r"CmtyClan_Sub3_Txt02_348_0|CmtyClan_Sub5_T2_413_0", "grow", 6),
            (r"CmtyClan_Sub3_Txt02|CmtyClan_Sub5_T2", "set", (0.0, 2.5, 46.0, 17.0)),
            (r"CmtyClan_Sub3_Txt03_349_0|CmtyClan_Sub3_Txt03|CmtyClan_Sub5_T3_414_0|CmtyClan_Sub5_T3", "move", 6),
            # … и строки
            (r"CmtyClan_Sub[35]_Name\d_\d+_0|CmtyClan_Sub[35]_Name\d", "grow", -6),
            (r"CmtyClan_Sub[35]_Lv\d_grp", "move", -6), (r"CmtyClan_Sub[35]_Lv\d_grp", "grow", 6),
            (r"CmtyClan_Sub[35]_Lv\d_\d+_0|CmtyClan_Sub[35]_Lv\d", "grow", 6),
            (r"CmtyClan_Sub[35]_Points\d_\d+_0|CmtyClan_Sub[35]_Points\d", "move", 6),
        ],
        "alt_before": {
            "CMTY_CLAN_SEC_BTN_Text": [((9.0, 7.5, 94.0, 23.0), 8.0, "INHERIT")],
            "Cmty_Who_FaddBtn_Text": [((3.0, 7.5, 120.0, 23.0), 8.0, "INHERIT")],
            "CmtyClan_Sub3_Txt02": [((0.0, 2.5, 40.0, 17.0), 8.0, "INHERIT")],
            "CmtyClan_Sub5_T2": [((0.0, 2.5, 40.0, 17.0), 8.0, "INHERIT")],
        },
        "text": {n: {"font": 8.0} for n in ("CMTY_CLAN_SEC_BTN_Text", "Cmty_Who_FaddBtn_Text",
                                             "CmtyClan_Sub3_Txt02", "CmtyClan_Sub5_T2")},
    },
    # ═══ 8500 Find_Party_WIn «Поиск отряда» ═══ Δ = 124 (решение владельца 07.10.2026 «вариант А»: окно шире
    #   ЦЕЛИКОМ, имена карт кеглем 8, плитки «как было 148 или сколько нужно»; отчёт
    #   `Reports/owner_1007/label_band_and_8500.md`). ★ ЗАМЕР ПУТЁМ ИГРОКА (`asda.UI.DumpTextMetrics`, кегль 8):
    #   «Проклятый Шахматный Замок» 155 px, «Проклятый Дворец Королевы» 149 px — в плитку 148 НЕ входят (оценка
    #   ревизии «~143 px», из которой взялось число 68, неверна; при +68 кадр `P_after_1.png` — обе строки обрезаны).
    #   Поэтому плитка 159 (155 + по 2 px) — минимум «сколько нужно», кнопка «По всему региону» (loc 11904, 92 px) —
    #   97 (как у прежнего решения): сетка 97 + 5 × (159 + 1) = 897 = 773 + 124. Окно 783 → 907 px (на 1024 входит).
    #   Левая часть −62, правая +62 (окно в корне привязано к центру). Растут: рамка-девятисрез (средние куски), линия
    #   под шапкой, шапка с ручкой, обе панели списков (+62 каждая: девятисрез, строки, курсор, рамка выбора, шапка
    #   панели, линия), сетка карт, полоса кнопок низа. Квадраты не тянутся: крестик/свернуть (+62), кнопки
    #   «Найти»/«Правка» к правому краю своей панели (+62), пейджеры по центру панели (+31), пары нижних кнопок под
    #   своими панелями (левая −62, правая +62). Плитки карт — полосы (не квадраты): 148 → 159.
    #   +62 в строках делится поровну между ПЕРВЫМ и ВТОРЫМ столбцом обеих панелей (имя / профессия, лидер / цель
    #   отряда; +31 и +31) — структура панелей одинакова, длинные цели вида «Искажённый дворец королевы» входят.
    #   Попутно (ревизия `widen_windows_REVIEW.md` М-4): заголовки панелей шире на 8 (ромбы-украшения раздвинуты на 4 в
    #   каждую сторону — «Список желающих вступить в отряд» 184 px больше не касается ромбов), подпись «Профессия »
    #   50 → 64 (62 px).
    "/Game/AsdaAssets/UI/Player_UI/Social/WBP_Find_Party_WIn": {
        "window": "Find_Party_WIn (8500)", "delta": 124,
        "rules": [
            # рамка окна (девятисрез, углы 16) и всё, что во всю ширину окна
            (r"Find_Party_WIn_0_[036]", "move", -62),
            (r"Find_Party_WIn_0_[147]", "move", -62), (r"Find_Party_WIn_0_[147]", "grow", 124),
            (r"Find_Party_WIn_0_[258]", "move", 62),
            (r"Find_Party_CloseBtn_Line_1_0", "move", -62), (r"Find_Party_CloseBtn_Line_1_0", "grow", 124),
            (r"Find_Party_Move(_188_DRAG)?", "move", -62), (r"Find_Party_Move(_188_DRAG)?", "grow", 124),
            (r"Find_Party_(MiniBtn|CloseBtn)", "move", 62),                      # квадраты 13×13 у правого края
            # панели списков: левая растёт влево, правая вправо (зазор между ними прежний)
            (r"Find_Party_MemberBox_grp", "move", -62), (r"Find_Party_MemberBox_grp", "grow", 62),
            (r"Find_Party_PartyBox_grp", "grow", 62),
            (r"Find_Party_MemberBox_2_[147]|Find_Party_PartyBox_84_[147]", "grow", 62),
            (r"Find_Party_MemberBox_2_[258]|Find_Party_PartyBox_84_[258]", "move", 62),
            (r"Find_Party_[MP]ListBox_grp", "grow", 62),
            (r"Find_Party_MSel_64_[16]|Find_Party_PSel_146_[16]", "grow", 62),   # рамка выбора строки (3 px)
            (r"Find_Party_MSel_64_[247]|Find_Party_PSel_146_[247]", "move", 62),
            # строки 00…09: подложка, курсор, группа столбцов; первый и второй столбцы +31, правее — смещение
            (r"Find_Party_[MP]List\d\d_grp", "grow", 62),
            (r"Find_Party_[MP]List\d\d_\d+_0|Find_Party_[MP]Cur\d\d_\d+_0", "grow", 62),
            (r"Find_Party_[MP]Char\d\d_grp", "grow", 62),
            (r"Find_Party_[MP]Char\d\d(_\d+_0)?", "grow", 31),
            (r"Find_Party_MJob\d\d_grp|Find_Party_PDes\d\d_grp", "move", 31),
            (r"Find_Party_MJob\d\d_grp|Find_Party_PDes\d\d_grp", "grow", 31),
            (r"Find_Party_MJob\d\d(_\d+_0)?|Find_Party_PDes\d\d(_\d+_0)?", "grow", 31),
            (r"Find_Party_MLv\d\d_grp|Find_Party_PNum\d\d_grp", "move", 31),
            # пейджеры — по центру панели
            (r"Find_Party_[MP]Slash_grp", "move", 31),
            # шапки панелей: плашка/линия во всю ширину, кнопка «Найти» у правого края, заголовок по центру (+8)
            (r"Find_Party_[MP]Title_Box_grp", "grow", 62),
            (r"Find_Party_MTitle_Box_72_0|Find_Party_PTitle_Box_154_0", "grow", 62),
            (r"Box_BlackOt40_(77|159)_0", "grow", 62),
            (r"Find_Party_[MP]FindBtn(_Text)?", "move", 62),
            (r"Find_Party_[MP]Title_grp", "move", 27), (r"Find_Party_[MP]Title_grp", "grow", 8),
            (r"Find_Party_[MP]Title", "grow", 8),
            (r"____75_0|Title_Bullet1_Ot30_157_0", "move", 8),                      # правый ромб
            (r"Find_Party_MJobSel_Txt", "set", (2.0, 1.0, 64.0, 16.0)),            # «Профессия » 62 px
            (r"Find_Party_EditBtn(_Text)?", "move", 62),                            # «Правка» у правого края панели
            # полоса кнопок низа: пары под своими панелями
            (r"Find_Party_BtnBase_grp", "move", -62), (r"Find_Party_BtnBase_grp", "grow", 124),
            (r"Find_Party_(ClosBtnBase|MiniBase)_grp", "move", 124),
            # сетка карт: группа во всю ширину, кнопка «По всему региону» 28 → 97, пять столбцов 148 → 159 (шаг 160)
            (r"Find_Party_MapAll_grp", "move", -62), (r"Find_Party_MapAll_grp", "grow", 124),
            (r"Find_Party_MapAll(_Text)?", "fn", lambda s: [s[0], s[1], 97.0, s[3]]),
            (r"Find_Party_Map[NDE]\d\d(_Text|_grp)?", "fn",
             lambda s: [{29.0: 98.0, 178.0: 258.0, 327.0: 418.0, 476.0: 578.0, 625.0: 738.0}[s[0]], s[1], 159.0, s[3]],
             r"Find_Party_MapAll_grp"),
            # содержимое под-канвасов слотов (галка, подпись, заглушки `XXX*` пустых слотов): L = 0, ширина 159
            (r"Find_Party_Map[NDE]\d\d(_Text)?|XXX(_\d+)?(_\d+_0)?", "fn", lambda s: [s[0], s[1], 159.0, s[3]],
             r"Find_Party_Map[NDE]\d\d_grp"),
            # рамка выбора карты (девятисрез 3 px вокруг столбца 0: уголок на 1 px левее/выше плитки)
            (r"Find_Party_MapSel_237_[035]", "fn", lambda s: [97.0, s[1], s[2], s[3]]),
            (r"Find_Party_MapSel_237_[16]", "fn", lambda s: [100.0, s[1], 155.0, s[3]]),
            (r"Find_Party_MapSel_237_[247]", "fn", lambda s: [255.0, s[1], s[2], s[3]]),
        ],
        # ★ UCheckBox (тип CHECK_BOX) рисует картинку состояния размером КИСТИ, а не слота: у всех 20 кнопок карт
        #   кисти 159×17 (вид импортёра — 148×17, прежнего решения — 134×17; оба «было», см. `cb_img_alt`).
        "cb_img": {"before": [148.0, 17.0], "after": [159.0, 17.0]},
        "cb_img_alt": [[134.0, 17.0]],
        # Окно уже было правлено прежним решением (сетка 134): его вид каждого узла — допустимое «было» (`--alt-dump`).
        "alt_from_dump": True,
    },
    # ═══ 20200 Item_Initializ_Win ═══ (ярлык 20320) ярлык «Вернуть в исходное состояние» (loc 50034, 158 px) над панелью целей:
    #   плашка-ярлык 108 → 166 px вправо по полосе панели (221 px), подпись по центру ярлыка.
    "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Item_Initializ_Win": {
        "window": "Item_Initializ_Win (20200)", "delta": 0,
        "rules": [
            (r"Item_Initializ_TargetTitle_132_0|Item_Initializ_TargetTitle_Text", "grow", 58),
        ],
    },
    # ═══ 74000 «Книга рыбака» (FISH_BOOK_WIN) ═══ Δ = 200 (810 → 1010; в 1024×768 по 7 px до краёв экрана) — fishfix 09.10,
    #   ревизия `Reports/owner_1008/fishing_REVIEW.md` МЕЛК-4. Имена томов на вкладках (замер Slate тем же шрифтом:
    #   «Рыбалка на Альпеоне, том N» 145 px, «Руководство начинающего рыбака» 177 px) во вкладку 108 не входили — эталон резал
    #   «13 знаков + ...» (рассчитано на БАЙТЫ корейского, `asda.FishBook.Trunc13`), и тома 1/2/3 были неразличимы.
    #   • Вкладки 108 → 158 (все 4; шаг 159 у 0…2, 4-я = 3-я + её ширина, как `uiListAppendClonedCell`): «…, том N» входит
    #     целиком с полями по 6 px (концы скина 10/9 px — текст не ложится на скругление); длиннее — по ширине поля + «...»
    #     (код, `textTruncateWithEllipsis@0xBC6E60` эталона; многоточие Slate у центрированного текста не работает —
    #     `FSlateTextLayout::OnPaint`: Center → NoOverflow). Скин вкладки (`ne_wheeltaget2` покой/наведение, `ne_wheeltaget1`
    #     выбрана) тянется девятисрезом BOX по ОТДЕЛЬНОЙ текстуре-вырезке 108×22 (как `TabMd_XBarBtn`: BOX по атласу Slate
    #     считает от ширины всей текстуры).
    #   • Левая часть (+200): сетка 5×6 ячеек (квадраты) НЕ тянется — по центру своей части (+100); «Удалить книгу» под сеткой
    #     остаётся на месте в корне (эталон 415 от корня окна + сдвиг сетки 100 = −100 + 100). Стрелки ленты — за 4-й вкладкой.
    #   • Правая карточка — целиком на +200 (подписи наград 116 входят целиком без «13 знаков»: «"Циклон" (3 дня)» 82 px).
    #   • Рамка, шапка окна, ручка, верхняя строка «Общий рейтинг», коричневая подложка, контейнер 74010 — целиком, симметрично.
    #   Узлы, которые создаёт шаг клонов (`asda_ui_fishbook_clones`, он идёт ПОСЛЕ этого шага): ячейки CELL00…29 и 4-я вкладка —
    #   «необязательные» (на свежем импорте их ещё нет; клоны лягут от уже сдвинутого шаблона / 3-й вкладки).
    "/Game/AsdaAssets/UI/Player_UI/Fishing/WBP_FISH_BOOK_WIN": {
        "window": "FISH_BOOK_WIN (74000)", "delta": 200,
        "rules": [
            # рамка окна — девятисрез из 9 картинок (углы 20×20)
            (r"FISH_BOOK_WIN_0_[036]", "move", -100),
            (r"FISH_BOOK_WIN_0_[147]", "move", -100), (r"FISH_BOOK_WIN_0_[147]", "grow", 200),
            (r"FISH_BOOK_WIN_0_[258]", "move", 100),
            # шапка (левый кусок с заголовком-картинкой, средний тянется, правый с крестиком) + заголовок/ручка
            (r"WinMd_CBar_grp", "move", -100), (r"WinMd_CBar_grp", "grow", 200),
            (r"WinMd_CBar_1_0", "grow", 200), (r"WinMd_RBar_grp", "move", 200),
            (r"FISH_BOOK_MOVE_TEXT(_109_HIT)?", "move", -100), (r"FISH_BOOK_MOVE_TEXT(_109_HIT)?", "grow", 200),
            # верхняя строка «Общий рейтинг / Виды пойманной рыбы» (тексты прижаты влево — едут с группой) и подложка
            (r"Box_WhiteOt20_grp_4", "move", -100), (r"Box_WhiteOt20_grp_4", "grow", 200),
            (r"Black_102_0|Box_WhiteOt20_101_0", "grow", 200),
            (r"Box_Brown_8_0", "move", -100), (r"Box_Brown_8_0", "grow", 200),
            # контейнер 74010 (девятисрез углы 11) — всё содержимое окна
            (r"FISH_BOOK_GBOX_1_grp", "move", -100), (r"FISH_BOOK_GBOX_1_grp", "grow", 200),
            (r"FISH_BOOK_GBOX_1_9_[147]", "grow", 200), (r"FISH_BOOK_GBOX_1_9_[258]", "move", 200),
            # вкладки томов (план, кнопка, подпись): 108 → 158, шаг 159; 4-я — клон шага клонов (необязательна)
            (r"FISH_BOOK_RADIO_btn_100_0|FISH_BOOK_RADIO_btn_OPT0|FISH_BOOK_RADIO_btn_Text", "move", -100),
            (r"FISH_BOOK_RADIO_btn_100_1|FISH_BOOK_RADIO_btn_OPT1|FISH_BOOK_RADIO_btn_Text2", "move", -50),
            (r"FISH_BOOK_RADIO_btn_100_3|FISH_BOOK_RADIO_btn_OPT3|FISH_BOOK_RADIO_btn_Text4", "move", 50),
            (r"FISH_BOOK_RADIO_btn_100_[0-3]|FISH_BOOK_RADIO_btn_OPT[0-3]|FISH_BOOK_RADIO_btn_Text[2-4]?", "grow", 50),
            # стрелки ленты вкладок — за 4-й вкладкой (зазор 25 px, как у импортёра)
            (r"FISH_BOOK_H_LEFT|FISH_BOOK_H_RIGHT", "move", 100),
            # сетка 5×6 (шаблон и 30 клонов — квадраты, не тянутся) и мёртвая разметка 74031…74036 — по центру левой части
            (r"FISH_BOOK_00_grp|FISH_BOOK_CELL\d\d_grp|FISH_BOOK_CHAT_1_grp", "move", 100),
            # «Удалить книгу» 74270 — под сеткой: в корне на месте (−100 окна + 100 сетки); узел в таблице ради «было» импортёра
            (r"FISH_BOOK_DEL_grp", "move", 0),
            # правая карточка (шапка-ярлык, итоги, рамка картинки, картинка, сведения, награды) — целиком за левой частью
            (r"FISH_BOOK_FF1_grp|FISH_BOOK_GG_1_grp|FISH_BOOK_CBACK_92_[0-8]|FISH_BOOK_BIG_BOX_[123]_grp", "move", 200),
        ],
        # Узлы шага клонов: на свежем импорте их ещё нет — «нет узла» не ошибка (клоны лягут от сдвинутого шаблона/3-й вкладки).
        "optional": [r"FISH_BOOK_CELL\d\d_grp", r"FISH_BOOK_RADIO_btn_100_3|FISH_BOOK_RADIO_btn_OPT3|FISH_BOOK_RADIO_btn_Text4"],
        # «Удалить книгу»: на свежем импорте — позиция импортёра (Origin .JUI 109,117 от корня окна), шаг клонов её ещё не ставил.
        "alt_before": {"FISH_BOOK_DEL_grp": [((-296.0, -261.0, 116.0, 38.0), None, None)]},
        # Скин вкладки — девятисрез по вырезке (план — покой; кнопка — наведение/выбрана).
        "img_box": {r"FISH_BOOK_RADIO_btn_100_[0-3]": "_TAB_IDLE_BOX"},
        "btn_states_box": {r"FISH_BOOK_RADIO_btn_OPT[0-3]": {"hovered": "_TAB_IDLE_BOX", "pressed": "_TAB_SEL_BOX"}},
    },

    # ══════════════════ МУЛЬТИЯЗЫЧНОСТЬ, П.5 ПАРТИИ ВЛАДЕЛЬЦА 09.10.2026: ОБРЕЗАННЫЕ АНГЛИЙСКИЕ ПОДПИСИ ══════════════════
    #   Источник — таблица переполнений EN этапа 8 (`Reports/owner_1009/multilang_stage8.md` §2,
    #   `multilang_verify/scan_en_overflow.tsv`). Поле подписи должно вместить И EN, И RU: самая длинная строка обоих языков
    #   + запас ≥ 2 px, кегль 8, метрика arial.ttf кегль×4/3 px (как `owner_1007/widen_windows.md` §2). Строки — те, что
    #   ставит КОД эталона (id GameText; RU — `RawAssets/Text/GameText.csv`, EN — `RawAssets/Text/L10N/en/DT_Loc_GameText_en.csv`),
    #   а где код не ставит — текст ассета. Окна, кроме книги рыбака, портом пока не открываются (своей подсистемы нет) —
    #   правка разметки ассета на будущее и для `asda.Lang.ScanScreen`.

    # ═══ 1545 THREEBUT_MSGBOX ═══ Δ = 162. Подписи кнопок — код эталона (`ui_core.c:51675`, вид 9 — в оригинале мёртвый,
    #   никто его не ставит): BUT00 6861 «Купить GXP» 61 / «Purchase Campus Credit» 123, BUT01 6862 «Купить очки обаяния »
    #   109 / «Purchase charisma» 95, BUT02 2378 «Отмена» 40 / «Cancel» 34. Поле подписи — во всю кнопку (значка нет), 72 → 126
    #   (123 + 3). Тройка кнопок СИММЕТРИЧНА: все три +54 (шаг 75 → 129, нахлёст 3 px как в .JUI), средняя на центре.
    #   Ряд 228 → 390 ⇒ окно 280 → 442 (поля до рамки 25/27 px, как в оригинале). Значок-предупреждение — в угол (−81).
    #   ⚠ Текст сообщения THREEBUT_TEXT (и его HIT) НЕ трогается: его слот ведёт шаг `asda_ui_label_band` (идёт ПОСЛЕ этого,
    #   сверяет точный вид) — поле 200 px остаётся по центру окна (центр 645 при центре окна 640, как в оригинале).
    "/Game/AsdaAssets/UI/Player_UI/Common/WBP_THREEBUT_MSGBOX": {
        "window": "THREEBUT_MSGBOX (1545)", "delta": 162,
        "rules": [
            # рамка окна — девятисрез из 9 картинок (углы 26)
            (r"THREEBUT_MSGBOX_0_[036]", "move", -81),
            (r"THREEBUT_MSGBOX_0_[147]", "move", -81), (r"THREEBUT_MSGBOX_0_[147]", "grow", 162),
            (r"THREEBUT_MSGBOX_0_[258]", "move", 81),
            (r"THREEBUT_WARNING_1_0", "move", -81),                                   # значок 38×38 — квадрат, в угол
            # полоса-таймер (скрыта; рамка 1 px + заливка): во всю ширину окна, заливка — в той же доле (2/3), что у .JUI
            (r"ThreeBut_GaugeBase_grp", "move", -81), (r"ThreeBut_GaugeBase_grp", "grow", 162),
            (r"ThreeBut_GaugeBase_3_[16]", "grow", 162), (r"ThreeBut_GaugeBase_3_[247]", "move", 162),
            (r"THREEBUT_GAUGE_4_0", "grow", 162),
            (r"THREEBUT_GAUGE_4_1", "fn", lambda s: [s[0], s[1], s[2] * 418.0 / 256.0, s[3]]),
            # три кнопки: каждая +54 (середина-кусок тянется, правый край смещается), группы раздвинуты симметрично
            (r"ThreeBut_But00_Base_grp", "move", -81), (r"ThreeBut_But01_Base_grp", "move", -27),
            (r"ThreeBut_But02_Base_grp", "move", 27),
            (r"ThreeBut_But0[012]_Base_grp", "grow", 54),
            (r"ThreeBut_But0[012]_Base_\d+_0", "grow", 54), (r"ThreeBut_But0[012]_RBase_\d+_0", "move", 54),
            (r"THREEBUT_BUT0[012](_Text)?", "grow", 54),
        ],
    },
    # ═══ 48099 CAMPWAR_JOIN_LOGIN ═══ Δ = 54. Ярлык 48030 CAMPWAR_JOIN_LOGIN_T1 — код эталона 10038 (`combat_pvp.c:4677`)
    #   «Осталось времени до начала» 151 / «Remaining waiting time» 110: поле 100 → 153, плашка-ярлык 130 → 183 (поля 16/14
    #   как в .JUI). Строка «ярлык + время» (зазор 9) сохраняет отступы от рамки 50/71 (−26 / +27 от прежнего), окно
    #   +54 симметрично; шапка с ручкой и крестиком, текст сообщения 48020 — во всю ширину; кнопки по центру — на месте.
    "/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_JOIN_LOGIN": {
        "window": "CAMPWAR_JOIN_LOGIN (48099)", "delta": 54,
        "rules": [
            (r"CAMPWAR_JOIN_LOGIN_0_[036]", "move", -27),
            (r"CAMPWAR_JOIN_LOGIN_0_[147]", "move", -27), (r"CAMPWAR_JOIN_LOGIN_0_[147]", "grow", 54),
            (r"CAMPWAR_JOIN_LOGIN_0_[258]", "move", 27),
            # шапка (три куска) + заголовок/ручка + крестик
            (r"WinSm_CBar_grp", "move", -27), (r"WinSm_CBar_grp", "grow", 54),
            (r"WinSm_CBar_15_0", "grow", 54), (r"WinSm_RBar_18_0", "move", 54),
            (r"CAMPWAR_JOIN_LOGIN_MOK_T_grp", "move", -27), (r"CAMPWAR_JOIN_LOGIN_MOK_T_grp", "grow", 54),
            (r"CAMPWAR_JOIN_LOGIN_MOK_T(_19_DRAG)?", "grow", 54), (r"CAMPWAR_JOIN_LOGIN_X", "move", 54),
            # текст сообщения (48020) — во всю ширину
            (r"CAMPWAR_JOIN_LOGIN_T_M(_1_HIT)?", "move", -27), (r"CAMPWAR_JOIN_LOGIN_T_M(_1_HIT)?", "grow", 54),
            # ярлык 48030 и время 48040
            (r"CAMPWAR_JOIN_LOGIN_T1_0_grp", "move", -26), (r"CAMPWAR_JOIN_LOGIN_T1_0_grp", "grow", 53),
            (r"CAMPWAR_JOIN_LOGIN_T1_0_13_0", "grow", 53), (r"CAMPWAR_JOIN_LOGIN_T1(_14_HIT)?", "grow", 53),
            (r"CAMPWAR_JOIN_LOGIN_TIME_T(_12_HIT)?", "move", 27),
        ],
    },
    # ═══ 47000 CAMPWAR_Join ═══ окно НЕ расширяется: место есть. 47061 CAMPWAR_Join_0_M1_T5 — код 111302 (`combat_pvp.c:4518`)
    #   «Для уровней» 64 / «Required Entry Level» 102: поле 100 → 106 симметрично (−3/+6, центр на месте; до значка-пули
    #   справа 11 px). Соседний в том же столбце 47050 T3 (10024 «Начало» / «Start time») — так же, столбец одинаковый.
    "/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_Join": {
        "window": "CAMPWAR_Join (47000)", "delta": 0,
        "rules": [
            (r"CAMPWAR_Join_0_M1_T[35]", "move", -3), (r"CAMPWAR_Join_0_M1_T[35]", "grow", 6),
        ],
    },
    # ═══ 15200 ITEM_SWAP_WIN ═══ окно НЕ расширяется: столбец подписей в блоках «до»/«после» (192 px) 60 → 90, строки
    #   (подпись + значение 60) раздвинуты на 15 влево — отступы от краёв блока 20/22 (были 35/37), значения прижаты как
    #   были. Код эталона (`shop_trade.c:10628-10641`): 15271 UPGRD1_T 13215 «Этап улучшения » 88 / «Upgrade Stage» 73,
    #   15272/15274 CAP*_T 13201 «Вместимость» 69 / «Accepting rate» 70; 15270 LEVEL1_T 13214 «Уровень»; ассет 15248 UPGRD2
    #   «Уровень заточки» 86 (самая длинная RU) → поле 90. Блоки «до» и «после» одинаковы (симметрия), выпадающие списки
    #   строк «после» смещены вместе со значениями.
    "/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_ITEM_SWAP_WIN": {
        "window": "ITEM_SWAP_WIN (15200)", "delta": 0,
        "rules": [
            (r"ITEM_SWAP_(LEVEL1|UPGRD1|CAP1|DUR1|CAP2|DUR2)_T_grp", "move", -15),
            (r"ITEM_SWAP_(LEVEL1|UPGRD1|CAP1|DUR1|CAP2|DUR2)_T_grp", "grow", 30),
            (r"ITEM_SWAP_(LEVEL1|UPGRD1|CAP1|DUR1|CAP2|DUR2)_T", "grow", 30),
            (r"ITEM_SWAP_(LEVEL1|UPGRD1|CAP1|DUR1|CAP2|DUR2)(_\d+_HIT)?", "move", 30),
            (r"ITEM_SWAP_(LEVEL2|UPGRD2)_grp", "move", -15), (r"ITEM_SWAP_(LEVEL2|UPGRD2)_grp", "grow", 30),
            (r"ITEM_SWAP_(LEVEL2|UPGRD2)(_\d+_HIT)?", "grow", 30),
            (r"ITEM_SWAP_(LV|UP)_COMBO_Base_grp", "move", 30),
        ],
    },
    # ═══ 50600 Item_Union_Win ═══ Δ = 70. 50621 Item_Union_2_Text2 — код 109802 (`ui_core.c:91155`) «Основные компоненты»
    #   118 / «Main materials» 70: шапка блока основного компонента 64 → 120 (+56). В том же ряду шапка 50623 Text4 109803
    #   «Дополнительные компоненты» 151 / «Sub materials» 66 в поле 140 → 154 (+14). Ячейки 64×64 и 32×32 и плашки чисел
    #   под ними не тянутся — по центру своих блоков. Блок очков +70. Панели 2/3, нижняя подсказка, шапка окна — во всю
    #   ширину; результат, столб, кнопки — по центру окна, на месте.
    "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Item_Union_Win": {
        "window": "Item_Union_Win (50600)", "delta": 70,
        "rules": [
            (r"Item_Union_Win_0_[036]", "move", -35),
            (r"Item_Union_Win_0_[147]", "move", -35), (r"Item_Union_Win_0_[147]", "grow", 70),
            (r"Item_Union_Win_0_[258]", "move", 35),
            (r"WinSm_CBar_grp", "move", -35), (r"WinSm_CBar_grp", "grow", 70),
            (r"WinSm_CBar_78_0", "grow", 70), (r"WinSm_RBar_81_0", "move", 70),
            (r"Item_Union_Top_Text(_82_DRAG)?", "move", -35), (r"Item_Union_Top_Text(_82_DRAG)?", "grow", 70),
            (r"Item_Union_X", "move", 35),
            # панель 2 «Компоненты для сборки» (девятисрез углы 11) и её шапка
            (r"Item_Union_2_grp", "move", -35), (r"Item_Union_2_grp", "grow", 70),
            (r"Item_Union_2_93_[147]", "grow", 70), (r"Item_Union_2_93_[258]", "move", 70),
            (r"Box_WhiteOt20_grp_3|Box_WhiteOt20_94_0", "grow", 70), (r"Item_Union_2_Text1(_95_HIT)?", "move", 35),
            # блок основного компонента (рамка 1 px): шапка +56, ячейка 64 и плашка числа — по центру (+28)
            (r"Box_White1Stroke_Ot15_grp", "grow", 56),
            (r"Box_White1Stroke_Ot15_96_[16]", "grow", 56), (r"Box_White1Stroke_Ot15_96_[247]", "move", 56),
            (r"Box_Brown_grp|Box_Brown_99_0|Item_Union_2_Text2(_100_HIT)?", "grow", 56),
            (r"Box_WhiteOt20_grp_4|Box_GreyRed_grp(_1)?", "move", 28),
            # блок дополнительных компонентов: за основным (+56), шапка +14, 4 ячейки и плашки — по центру (+7)
            (r"Box_White1Stroke_Ot15_grp_1", "move", 56), (r"Box_White1Stroke_Ot15_grp_1", "grow", 14),
            (r"Box_White1Stroke_Ot15_105_[16]", "grow", 14), (r"Box_White1Stroke_Ot15_105_[247]", "move", 14),
            (r"Box_Brown_grp_1|Box_Brown_106_0|Item_Union_2_Text4(_107_HIT)?", "grow", 14),
            (r"NoImage_grp_2|Box_GreyRed_grp_[2-5]", "move", 7),
            # блок очков — за ними (+70)
            (r"Box_White1Stroke_Ot15_grp_2", "move", 70),
            # панель 3 «Рюкзак»: во всю ширину, шапка-подпись и сетка 10×3 ячеек — по центру
            (r"Item_Union_3_grp", "move", -35), (r"Item_Union_3_grp", "grow", 70),
            (r"Item_Union_3_1_[147]", "grow", 70), (r"Item_Union_3_1_[258]", "move", 70),
            (r"Box_WhiteOt20_grp|Box_WhiteOt20_65_0", "grow", 70), (r"Item_Union_3_Text1(_66_HIT)?", "move", 35),
            (r"NoImage_grp(_1)?", "move", 35),
            # нижняя подсказка (рамка 1 px) — во всю ширину
            (r"Box_White1Stroke_Ot15_grp_3", "move", -35), (r"Box_White1Stroke_Ot15_grp_3", "grow", 70),
            (r"Box_White1Stroke_Ot15_131_[16]", "grow", 70), (r"Box_White1Stroke_Ot15_131_[247]", "move", 70),
            (r"Item_Union_3_Tx(_132_HIT)?", "grow", 70),
        ],
    },
    # ═══ 50500 Strength_Part_Win «Разделение улучшений» ═══ Δ = 228. 50522/50524 Strength_Part_2_Text_3/5 — код эталона
    #   108521/108522 (`social.c:1325-1330`) «В наличии пустой камень улучшения оружия:» 233 / «…доспехов:» 244 и «Owned
    #   weapon empty Upgrade stone» 184 / «Owned armor …» 175 в поле 150: поле → 246 (+96). Соседний столбец того же ряда
    #   50523/50525 (подпись ассета «Количество» 60 px в 44, центром вылезала на соседа) — 44 → 62 (+18): ряд +114.
    #   Пара панелей 1/2 (по 212, рядом) СИММЕТРИЧНА — обе +114: панель 1 растёт влево, панель 2 вправо, окно 450 → 678
    #   (+228, на 1024 входит). В панели 1 +114 делится поровну между столбцами «Уровень/Сила улучшения» и значений
    #   (+57/+57: 108503 «Сила улучшения» 89 px входит в 137), шапки панелей 108501/108504 (188/199 px) входят в 294.
    #   Панели 3/4 — во всю ширину; в панели 3 подпись 108505 «Работа с камнем улучшения завершена» (206 px) +114, значок
    #   над ней по центру (+57), таблица справа +114 и сдвинута за ней (столбцы +57/+57); в панели 4 полоса «Рюкзак», текст
    #   108509 во всю ширину, сетки ячеек — по центру. Ячейки/иконки не тянутся. Стрелки, значки между панелями и кнопки —
    #   по центру окна, на месте. (Первая запись 09.10 шла с Δ = 192 без столбца «Количество» — путём игрока RU-строка
    #   «…доспехов:» сошлась с «Количество»; ассет возвращён из бэкапа до записи, таблица пересобрана.)
    "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Strength_Part_Win": {
        "window": "Strength_Part_Win (50500)", "delta": 228,
        "rules": [
            (r"Strength_Part_Win_0_[036]", "move", -114),
            (r"Strength_Part_Win_0_[147]", "move", -114), (r"Strength_Part_Win_0_[147]", "grow", 228),
            (r"Strength_Part_Win_0_[258]", "move", 114),
            (r"WinSm_CBar_grp", "move", -114), (r"WinSm_CBar_grp", "grow", 228),
            (r"WinSm_CBar_79_0", "grow", 228), (r"WinSm_RBar_82_0", "move", 228),
            (r"Strength_Part_Top_Text", "move", -114), (r"Strength_Part_Top_Text", "grow", 228),
            (r"Strength_Part_0_X", "move", 114),
            # ── панель 1 (слева): растёт влево; рамка-девятисрез углы 11
            (r"Strength_Part_1_0_grp", "move", -114), (r"Strength_Part_1_0_grp", "grow", 114),
            (r"Strength_Part_1_0_96_[147]", "grow", 114), (r"Strength_Part_1_0_96_[258]", "move", 114),
            (r"Box_WhiteOt20_97_0|Box_WhiteOt20_grp_1|Box_WhiteOt20_98_0", "grow", 114),
            (r"Strength_Part_1_Text_1(_109_HIT)?", "grow", 114),
            (r"Slot_Base_grp", "move", 57),                                            # ячейка 32 — по центру
            (r"Strength_Part_1_Text_2_grp|Strength_Part_1_Text_2(_104_HIT)?", "grow", 114),
            (r"Strength_Part_1_Text_3_grp|Strength_Part_1_Text_[35](_10[56]_HIT)?", "grow", 57),
            (r"Strength_Part_1_Text_4_grp", "move", 57),
            (r"Strength_Part_1_Text_4_grp|Strength_Part_1_Text_[46](_10[78]_HIT)?", "grow", 57),
            (r"Black_grp|Black_99_0", "grow", 114),
            (r"Black_10[01]_0", "grow", 57), (r"Black_10[23]_0", "move", 57), (r"Black_10[23]_0", "grow", 57),
            # ── панель 2 (справа): растёт вправо; столбец строк 50522/50524 +96, столбец «Количество» за ним +18
            (r"Strength_Part_2_0_grp", "grow", 114),
            (r"Strength_Part_2_0_112_[147]", "grow", 114), (r"Strength_Part_2_0_112_[258]", "move", 114),
            (r"Box_WhiteOt20_113_0|Box_WhiteOt20_grp_2|Box_WhiteOt20_114_0", "grow", 114),
            (r"Strength_Part_2_Text_1(_125_HIT)?", "grow", 114),
            (r"Slot_Base_grp_1", "grow", 57),
            (r"Strength_Part_2_Mark1(_127_0)?|Slot_Base_126_0", "move", 57),          # ячейка 32 — по центру
            (r"Strength_Part_2_Text_2_grp", "grow", 114),
            (r"Strength_Part_2_Text_2(_120_HIT)?", "move", 57),                          # «11/12» — по центру
            (r"Strength_Part_2_Text_3_grp|Strength_Part_2_Text_[35](_12[12]_HIT)?", "grow", 96),
            (r"Strength_Part_2_Text_4_grp", "move", 96),
            (r"Strength_Part_2_Text_4_grp|Strength_Part_2_Text_[46](_12[34]_HIT)?", "grow", 18),
            (r"Black_grp_1|Black_115_0", "grow", 114), (r"Black_11[67]_0", "grow", 96),
            (r"Black_11[89]_0", "move", 96), (r"Black_11[89]_0", "grow", 18),
            # ── панель 3 (результат): во всю ширину
            (r"Strength_Part_3_0_grp", "move", -114), (r"Strength_Part_3_0_grp", "grow", 228),
            (r"Strength_Part_3_0_131_[147]", "grow", 228), (r"Strength_Part_3_0_131_[258]", "move", 228),
            (r"Strength_Part_3_Mark(_143_0)?", "move", 57),
            (r"Box_WhiteOt20_grp_3|Box_WhiteOt20_132_0|Strength_Part_3_Text_1(_133_HIT)?", "grow", 114),
            (r"Box_WhiteOt20_grp_4", "move", 114),
            (r"Box_WhiteOt20_grp_4|Box_WhiteOt20_134_0|Black_grp_2", "grow", 114),
            (r"Black_13[56]_0", "grow", 57), (r"Black_13[78]_0", "move", 57), (r"Black_13[78]_0", "grow", 57),
            (r"Strength_Part_3_Text_3_grp|Strength_Part_3_Text_[35](_1(39|40)_HIT)?", "grow", 57),
            (r"Strength_Part_3_Text_4_grp", "move", 57),
            (r"Strength_Part_3_Text_4_grp|Strength_Part_3_Text_[46](_14[12]_HIT)?", "grow", 57),
            # ── панель 4 (рюкзак): во всю ширину, сетки ячеек — по центру
            (r"Strength_Part_4_0_grp", "move", -114), (r"Strength_Part_4_0_grp", "grow", 228),
            (r"Strength_Part_4_0_1_[147]", "grow", 228), (r"Strength_Part_4_0_1_[258]", "move", 228),
            (r"Box_WhiteOt20_grp|Box_WhiteOt20_66_0", "grow", 228), (r"Strength_Part_4_Text2(_67_HIT)?", "move", 114),
            (r"Strength_Part_4_Text1(_65_HIT)?", "grow", 228),
            (r"Strength_Part_4_BOX_H_grp|Strength_Part_4_0_Box_B_grp", "move", 114),
        ],
    },
}


# ══════════════════ ПРАВКИ РЕВИЗИИ 07.10.2026 (`Reports/owner_1007/widen_fixes.md`) ══════════════════
#   Окна из WINDOWS уже расширены, и генерировать их по дампу ПОВТОРНО нельзя («было» стало бы «стало»). Поэтому
#   доправки ревизии заданы ЯВНО — узел: было/стало (как пишет `build`) — и вливаются в таблицу узел за узлом
#   (`--extra-only` пишет только их, без дампа). Каждое «было» снято полным структурным дампом 07.10
#   (`widenfix_logs/dump_before.json`).
_CLAN = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_CLAN_MAKING_WIN"
_MSG2 = ("/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN", "/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN_NEUTRAL")
_COMM = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN"


def _n(cls, parent, before, after, **kw):
    """Запись узла таблицы (тот же вид, что у `build`)."""
    e = {"before": [float(x) for x in before], "after": [float(x) for x in after], "class": cls, "parent": parent}
    e.update(kw)
    return e


# Кнопка-ярлык `TabMd_XBarBtn` (89×19, косые концы по 9 px — пиксельный разбор N_01.tga UV 0.176…0.350 × 0.963…1.000):
#   кисти состояний → ОТДЕЛЬНАЯ текстура-вырезка 89×19 (девятисрез BOX по атласу Slate считает от ширины всей текстуры —
#   см. шаг), поля BOX в долях кисти по X, тянется только середина. Вырезка: N_01.png x 90..178, y 493..511.
# ★ fishfix 09.10 (книга рыбака 74000, МЕЛК-4): скин вкладки томов `TabBar_W108_3Btn` 108×22 со скруглениями (концы 10/9 px) —
#   покой/наведение `ne_wheeltaget2` UV 0.574…0.996 × 0.738…0.824 (пиксели x 147..254, y 189..210), выбрана `ne_wheeltaget1`
#   UV 0.578…1.000 × 0.754…0.840 (x 148..255, y 193..214). Вырезки — `RawAssets/Textures/JUI_Slices/FishBookTab_*.png`.
#   Поля BOX — по пиксельному разбору вырезок: столбцы 10…98 одинаковы (сглаживание левого скругления заходит в x 9),
#   концы своего размера 10 и 9 px.
_TAB_IDLE_BOX = {"texture": "/Game/AsdaAssets/Textures/JUI_Slices/FishBookTab_Idle",
                 "source_png": "RawAssets/Textures/JUI_Slices/FishBookTab_Idle.png",
                 "src_texture": "/Game/AsdaAssets/Textures/JUI/ne_wheeltaget2", "src_uv": [0.574, 0.738, 0.996, 0.824],
                 "margin": [10.0 / 108.0, 0.0, 9.0 / 108.0, 0.0], "image_size": [108.0, 22.0]}
_TAB_SEL_BOX = {"texture": "/Game/AsdaAssets/Textures/JUI_Slices/FishBookTab_Sel",
                "source_png": "RawAssets/Textures/JUI_Slices/FishBookTab_Sel.png",
                "src_texture": "/Game/AsdaAssets/Textures/JUI/ne_wheeltaget1", "src_uv": [0.578, 0.754, 1.0, 0.84],
                "margin": [10.0 / 108.0, 0.0, 9.0 / 108.0, 0.0], "image_size": [108.0, 22.0]}
_BOX_SPECS = {"_TAB_IDLE_BOX": _TAB_IDLE_BOX, "_TAB_SEL_BOX": _TAB_SEL_BOX}   # имя → вырезка (ссылки из WINDOWS[...])

_XBAR_BOX = {"texture": "/Game/AsdaAssets/Textures/JUI_Slices/TabMd_XBarBtn",
             "source_png": "RawAssets/Textures/JUI_Slices/TabMd_XBarBtn.png",
             "src_texture": "/Game/AsdaAssets/Textures/JUI/n_01", "src_uv": [0.176, 0.963, 0.35, 1.0],
             "margin": [9.0 / 89.0, 0.0, 9.0 / 89.0, 0.0], "image_size": [89.0, 19.0]}

EXTRA_NODES = {
    # С-1: «Название клана:» (loc 12217, 84 px кеглем 8) в поле 70 заезжало под поле ввода на реальном пути
    #   `asda.Clan.OpenCreate`. Блок «подпись + поле ввода» шире на 16 и сдвинут влево на 8 — центр блока (−16)
    #   и отступы от рамки (38 слева / 70 справа, как в оригинале до расширения окна) сохраняются. «Затраты на
    #   создание:» (111 px) — поле 70 → 112 (группа взноса 177, сумма прижата вправо, наезда нет).
    _CLAN: {
        "CLAN_NAME_grp": _n("CanvasPanel", "RootCanvas", (-112, -35, 192, 22), (-120, -35, 208, 22)),
        "CLAN_NAME": _n("TextBlock", "CLAN_NAME_grp", (0, 4, 70, 20), (0, 4, 86, 20),
                        font_before=8.0, clip_before="INHERIT"),
        "CLAN_NAME_13_HIT": _n("Image", "CLAN_NAME_grp", (0, 1, 70, 20), (0, 1, 86, 20)),
        "CLAN_NAME_INPUT_Base_grp": _n("CanvasPanel", "CLAN_NAME_grp", (72, 0, 120, 22), (88, 0, 120, 22)),
        "CLAN_FEE": _n("TextBlock", "CLAN_FEE_grp", (0, 0.5, 70, 15), (0, 0.5, 112, 15),
                       font_before=8.0, clip_before="INHERIT"),
    },
    # П.6: кнопки-ярлыки «Сообщение»/«Разорвать Дружбу» 89 → 100 (шаг 07.10, «было» — вид импортёра 116/89) — кисть
    #   IMAGE растягивала косые концы на 12 %. Слот тот же (100), кисти состояний → девятисрез BOX, концы 9 px своего размера.
    _COMM: {
        "Soul_MessageBtn": _n("Button", "SOUL_TXT00_B2_grp", (116, 0, 89, 19), (105, 0, 100, 19), btn_box=_XBAR_BOX),
        "Soul_CutoffBtn": _n("Button", "SOUL_TXT11_B2_grp", (116, 0, 89, 19), (105, 0, 100, 19), btn_box=_XBAR_BOX),
        # ★ 08.10.2026 (баг владельца, `Reports/owner_1008/ui_bugs.md` п.2): подпись «Разорвать Дружбу» (loc 8045) —
        #   кегль на 1 меньше (решение владельца). Кегль 7: верх строки +1.5 (строка на 3 px ниже, центр на месте —
        #   правило `CAPTION_TOP_SHIFT_7` шага `asda_ui_icon_caption_fit`). «Было» — вид импортёра и вид после
        #   расширения 07.10 (кегль 8, поле 100).
        "Soul_CutoffBtn_Text": _n("TextBlock", "SOUL_TXT11_B2_grp", (116, 3.5, 89, 19), (105, 5.0, 100, 19),
                                  font_before=8.0, clip_before="INHERIT", font_after=7.0, clip_after="INHERIT",
                                  alt_before=[{"slot": [105.0, 3.5, 100.0, 19.0], "font": 8.0, "clip": "INHERIT"}]),
    },
}
# ★ 08.10.2026 (баг владельца п.2, вкладка «Друг» окна 6400, блок «Делиться / Не делиться»): галка SOUL_CHECK0N
#   наезжала на подпись «Делиться». Причина — рантайм-сдвиг `asda.Soulmate.ShareLabelShiftX` (03.09) двигал на −12
#   И группу `SHARE_0N_grp` (в ней галка), И саму подпись внутри неё: подпись уехала на −24, галка на −12 → наезд
#   (подпись 136, галка 129…147). Правка В АССЕТЕ (закон no-runtime-widget-fixes; рантайм-сдвиг снят): подпись
#   остаётся там, где её видит владелец (x 136 в строке), квадрат галки 18×19 НЕ растягивается — группа смещается
#   влево на 24 (141 → 117): галка 117…135, зазор 1 px до подписи — как в `.JUI` (галка Origin «−19 −7» от подписи).
#   Все три строки одинаково (симметрия).
for _i, _row in enumerate(("SOUL_TXT07_grp", "SOUL_TXT08_grp", "SOUL_TXT09_grp")):
    EXTRA_NODES[_COMM]["SHARE_0%d_grp" % _i] = _n("CanvasPanel", _row, (141, 0, 69, 22), (117, 0, 69, 22))
# П.4: «Отменить» MESSAGE2_BUT01 (окна папки Login — разрешение владельца 07.10 ТОЛЬКО на эту кнопку): кнопка +3
#   (71 → 74), подпись правее значка кеглем 8 (51 px в поле 52, было кегль 7 шага `asda_ui_icon_caption_fit`).
#   Пара кнопок остаётся по центру окна (512): обе группы −1.5, зазор 9 px прежний. Запись — только с
#   ASDA_ALLOW_LOGIN_WRITE=1 (шаг пропускает пакеты логина без разрешения).
for _a in _MSG2:
    EXTRA_NODES[_a] = {
        "MESSAGE2_BUT00_BASE_grp": _n("CanvasPanel", "RootCanvas", (430, 431, 78, 28), (428.5, 431, 78, 28)),
        "MESSAGE2_BUT01_BASE_grp": _n("CanvasPanel", "RootCanvas", (517, 431, 77, 28), (515.5, 431, 80, 28)),
        "MESSAGE2_BUT01": _n("Button", "MESSAGE2_BUT01_BASE_grp", (3, 3, 71, 23), (3, 3, 74, 23)),
        "MESSAGE2_BUT01_BASE_8_0": _n("Image", "MESSAGE2_BUT01_BASE_grp", (4, 0, 69, 28), (4, 0, 72, 28)),
        "MESSAGE2_BUT01_RBASE_10_0": _n("Image", "MESSAGE2_BUT01_BASE_grp", (73, 0, 4, 28), (76, 0, 4, 28)),
        # подпись: вид шага `asda_ui_icon_caption_fit` (кегль 7) или вид импортёра (во всю кнопку, кегль 8)
        "MESSAGE2_BUT01_Text": _n("TextBlock", "MESSAGE2_BUT01_BASE_grp", (22, 9, 49, 23), (22, 7.5, 52, 23),
                                  font_before=7.0, clip_before="INHERIT", font_after=8.0, clip_after="INHERIT",
                                  alt_before=[{"slot": [3.0, 7.5, 71.0, 23.0], "font": 8.0, "clip": "INHERIT"}]),
    }
# ★ МУЛЬТИЯЗЫЧНОСТЬ, П.5 ПАРТИИ ВЛАДЕЛЬЦА 09.10.2026: книга рыбака 74000, панель 74270 «Удалить книгу». Подпись 74271 ставит
#   код (`AsdaFishBookSubsystem.cpp:546`, loc 111047 «Удалить книгу» 72 px / «Delete fishing guide» 94 px) — центром во всю
#   кнопку (поле 9…105), а значок-крестик 16×16 стоит у левого края (4…20): английская строка уходила под значок (снимок
#   `owner_1009/multilang_review_shots/en_fishbook_grid.png`). Приём шага `asda_ui_icon_caption_fit` (Д-1): подпись правее
#   значка на 1 px (21), поле 96 (94 + 2) — кнопка +12 (102 → 114), её середина-кусок тянется, правый край смещается; рамка
#   панели (девятисрез 2 px) +12. Панель растёт ВЛЕВО: её правый край у правого края сетки — на месте (эталон
#   `SetField74_75(74270, 415, 703)`; шаг `asda_ui_fishbook_clones` с 09.10 сверяет позицию по правому краю). RU-подпись
#   стоит там же, где была (центр 71 от панели в старых координатах). Окно ВНЕ дампа: книга уже расширена (fishfix 09.10),
#   «было» каждого узла снято полным структурным дампом 09.10 (`owner_1009/multilang_owner_batch/widen/dump_before.json`).
_FISHBOOK = "/Game/AsdaAssets/UI/Player_UI/Fishing/WBP_FISH_BOOK_WIN"
EXTRA_NODES[_FISHBOOK] = {
    # панель: «было» — вид после fishfix 09.10 (= эталонная точка) и вид импортёра (Origin .JUI 109,117 от корня окна)
    "FISH_BOOK_DEL_grp": _n("CanvasPanel", "RootCanvas", (10, 325, 116, 38), (-2, 325, 128, 38),
                            alt_before=[{"slot": [-296.0, -261.0, 116.0, 38.0], "font": None, "clip": None}]),
    "FISH_BOOK_DEL_203_1": _n("Image", "FISH_BOOK_DEL_grp", (2, 0, 112, 2), (2, 0, 124, 2)),
    "FISH_BOOK_DEL_203_4": _n("Image", "FISH_BOOK_DEL_grp", (2, 2, 112, 34), (2, 2, 124, 34)),
    "FISH_BOOK_DEL_203_7": _n("Image", "FISH_BOOK_DEL_grp", (2, 36, 112, 2), (2, 36, 124, 2)),
    "FISH_BOOK_DEL_203_2": _n("Image", "FISH_BOOK_DEL_grp", (114, 0, 2, 2), (126, 0, 2, 2)),
    "FISH_BOOK_DEL_203_5": _n("Image", "FISH_BOOK_DEL_grp", (114, 2, 2, 34), (126, 2, 2, 34)),
    "FISH_BOOK_DEL_203_8": _n("Image", "FISH_BOOK_DEL_grp", (114, 36, 2, 2), (126, 36, 2, 2)),
    "Btn_CBase_grp_2": _n("CanvasPanel", "FISH_BOOK_DEL_grp", (4, 5, 108, 28), (4, 5, 120, 28)),
    "Btn_CBase_204_0": _n("Image", "Btn_CBase_grp_2", (4, 0, 100, 28), (4, 0, 112, 28)),
    "Btn_RBase_206_0": _n("Image", "Btn_CBase_grp_2", (104, 0, 4, 28), (116, 0, 4, 28)),
    "FISH_BOOK_DEL_btn": _n("Button", "Btn_CBase_grp_2", (3, 3, 102, 23), (3, 3, 114, 23)),
    "FISH_BOOK_DEL_btn_Text": _n("TextBlock", "Btn_CBase_grp_2", (9, 7.5, 96, 23), (21, 7.5, 96, 23),
                                 font_before=8.0, clip_before="INHERIT"),
}
# ★ 09.10.2026 ПРАВКИ РЕВИЗИИ ПАРТИИ ВЛАДЕЛЬЦА (`Reports/owner_1009/multilang_owner_batch_REVIEW.md` СРЕДН-1/2,
#   отчёт `multilang_owner_fixes.md`). Окна уже расширены (Δ 228 / 70) — генерировать их по дампу повторно нельзя, поэтому
#   доправки заданы ЯВНО. «Было» каждого узла — вид импортёра (полный структурный дамп 09.10
#   `owner_1009/multilang_owner_batch/widen/dump_before.json`); где узел уже менялся расширением — его текущий вид из
#   `dump_after.json` в `alt_before`.
#   Кнопка со значком (приём `asda_ui_icon_caption_fit` Д-1, как TWOBUT/Town_as/CLAN_MAKING выше): подпись правее значка
#   16×16 (5…21) на 1 px — с x 22, правый край = край кнопки −3. Ширина подписи — самая длинная строка RU/EN кеглем 8
#   (замер `asda.Lang.ScanScreen` путём игрока: шрифт игры даёт ширину на 1 px больше, чем arial.ttf 10.667 px в PIL)
#   + запас ≥ 2 px. Кнопка-пара СИММЕТРИЧНА: обе кнопки шире на d (середина-кусок тянется, правый край смещается), правая
#   смещена на d, группа пары шире на 2d и сдвинута на −d — центр пары, зазор 6 px между кнопками и отступ значка — прежние.
_STRENGTH = "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Strength_Part_Win"
_UNION = "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Item_Union_Win"
_TB8 = dict(font_before=8.0, clip_before="INHERIT")   # кегль/клип подписей кнопок — как у импортёра, не меняются


def _btn_pair(grp, hit, left, right, d, ok_text_w):
    """Узлы пары кнопок «подтвердить (значок ✓) | отмена (значок ✕)» в группе `grp` (228×38, кнопки 106×28 с x 7/8 и
    118/119): каждая кнопка +d, правая +d вправо, группа −d и +2d. `left`/`right` — (группа кнопки, x группы, середина-кусок,
    правый край, Button, подпись). Подпись левой — правее значка, ширина `ok_text_w` (= 78 + d)."""
    g_parent, g_slot = grp[1], grp[2]
    out = {grp[0]: _n("CanvasPanel", g_parent, g_slot, (g_slot[0] - d, g_slot[1], g_slot[2] + 2 * d, g_slot[3])),
           hit: _n("Image", grp[0], (0, 0, 228, 38), (0, 0, 228 + 2 * d, 38))}
    for side, (bgrp, bx, mid, rb, btn, txt) in (("L", left), ("R", right)):
        nx = bx if side == "L" else bx + d
        out[bgrp] = _n("CanvasPanel", grp[0], (bx, 6, 106, 28), (nx, 6, 106 + d, 28))
        out[mid] = _n("Image", bgrp, (4, 0, 98, 28), (4, 0, 98 + d, 28))
        out[rb] = _n("Image", bgrp, (102, 0, 4, 28), (102 + d, 0, 4, 28))
        out[btn] = _n("Button", bgrp, (3, 3, 100, 23), (3, 3, 100 + d, 23))
        if side == "L":
            out[txt] = _n("TextBlock", bgrp, (6, 7.5, 97, 23), (22, 7.5, ok_text_w, 23), **_TB8)
        else:
            out[txt] = _n("TextBlock", bgrp, (9, 7.5, 94, 23), (9, 7.5, 94 + d, 23), **_TB8)
    return out


# СРЕДН-1. 50500 «Разделение улучшений»: подпись 50582 — код эталона 108507 (`social.c:1362`) «Разделение улучшений» 121 /
#   «Upgrade separation» 97 в поле 97 центром во всю кнопку — RU выходила за обе стороны кнопки, начало под значком; EN
#   «U» под значком. Поле 123 (121 + 2) с x 22 ⇒ d = 45 (кнопка 100 → 145). Пара 228 → 318, центр −424 как был
#   (окно −760…−82; смещение пары на 3 px от центра окна — как в .JUI до расширения).
EXTRA_NODES[_STRENGTH] = _btn_pair(("NoImage_grp", "RootCanvas", (-538, 617, 228, 38)), "NoImage_68_HIT",
                                   ("Btn_CBase_grp", 7, "Btn_CBase_69_0", "Btn_RBase_71_0", "Strength_Part_OK", "Strength_Part_OK_Text"),
                                   ("Btn_CBase_grp_1", 119, "Btn_CBase_74_0", "Btn_RBase_76_0", "Strength_Part_NO", "Strength_Part_NO_Text"),
                                   45, 123)
# СРЕДН-2. 50600 «Объединение элитных предметов»: подпись 50683 — код 109806 (`ui_core.c:91207`) «Объединение предметов» 129 /
#   «Combine Items» 74 — RU обрезалась с двух сторон, начало под значком. Поле 131 (129 + 2) с x 22 ⇒ d = 53 (кнопка
#   100 → 153). Пара 228 → 334 (−486…−152 в окне −530…−100), центр −319 как был. Пустая подпись Item_Union_on (поле 228)
#   не трогается: её слот ведёт шаг `asda_ui_label_band` (идёт после этого и сверяет точный вид).
EXTRA_NODES[_UNION] = _btn_pair(("Item_Union_on_grp", "RootCanvas", (-433, 286, 228, 38)), "Item_Union_on_67_HIT",
                                ("Item_Union_OK_B_grp", 8, "Item_Union_OK_B_68_0", "Btn_RBase_70_0", "Item_Union_Ok", "Item_Union_Ok_Text"),
                                ("Item_Union_NO_B_grp", 118, "Item_Union_NO_B_73_0", "Btn_RBase_75_0", "Item_Union_No", "Item_Union_No_Text"),
                                53, 131)
# СРЕДН-2 (текст). 50682 Item_Union_3_Tx — код 109808 (`ui_core.c:91203`), 3 строки в RU и EN при поле 400. Импортёр ставил
#   верх поля 18 — центр по вертикали «как для одной строки», дальше строки текли вниз: третья строка вылезала из рамки
#   подсказки (низ 50) на кнопки. Оригинал центрирует ВЕСЬ блок строк (`textFont_DrawRichText`: верх = середина rect −
#   шаг 14 × строк / 2, font_text.c:8471-8476 — разбор шага `asda_ui_text_block_align`): rect контрола = HIT (1…49, середина
#   25) ⇒ верх 25 − 21 = 4, высота 45 (до 49, внутри рамки). Окно не растёт. Ширина 400 — расширение 09.10 (alt «было»).
# ★ 10.10.2026 (волна «наезды текста», ревизия 09.10 МЕЛК-1, `Reports/owner_1010/overlaps.md`): верх 4 / высота 45 —
#   центр «под 3 строки» жёстко: при 2 строках блок вставал к верху, при 4 вылезал бы за рамку. Как в оригинале — блок
#   строк по центру rect при ЛЮБОМ числе строк: слот с выравниванием (0.5, 0.5) в середине HIT (x 5 + 200 = 205, y 25),
#   авторазмер, перенос по ширине 400 (`wrap_text_at`; авторазмер без ширины переноса текст не переносит). Прежний вид
#   09.10 (5, 4, 400, 45) — допустимое «было».
EXTRA_NODES[_UNION]["Item_Union_3_Tx"] = _n("TextBlock", "Box_White1Stroke_Ot15_grp_3", (5, 18, 330, 48), (205, 25, 400, 45),
                                            alt_before=[{"slot": [5.0, 18.0, 400.0, 48.0], "font": 8.0, "clip": "INHERIT"},
                                                        {"slot": [5.0, 4.0, 400.0, 45.0], "font": 8.0, "clip": "INHERIT"}],
                                            layout_after={"align": [0.5, 0.5], "auto": True},
                                            wrap_at_before=0.0, wrap_at_after=400.0,
                                            **_TB8)
# МЕЛК-3 (следствие). 48099 CAMPWAR_JOIN_LOGIN: вторая кнопка 48060 с 09.10 получает подпись по id эталона 10034
#   (`combat_pvp.c:4692`, план подписей) — «Выйти из очереди» 92 / «Cancel List» 55 вместо заглушки «Быстрее»; первая 48050
#   10039 «Войти в битву» 72 / «Enter the war» 66 уже в RU заходила под значок (скан наездов 09.10). Обе подписи — правее
#   значка, поле 94 (92 + 2) ⇒ d = 16 у ОБЕИХ кнопок (симметрия): кнопки 100 → 116, левая группа −16, правая на месте —
#   центр пары −2.5 и зазор 11 px как в .JUI. Кнопки — прямые дети корня (группы-пары нет).
_CJL = "/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_CAMPWAR_JOIN_LOGIN"
EXTRA_NODES[_CJL] = {}
for _g, _gx, _dx, _mid, _rb, _btn, _txt, _tx0 in (
        ("CAMPWAR_JOIN_LOGIN_OK_M_grp", -114, -16, "CAMPWAR_JOIN_LOGIN_OK_M_2_0", "Btn_RBase_4_0", "CAMPWAR_JOIN_LOGIN_P",
         "CAMPWAR_JOIN_LOGIN_P_Text", (6, 7.5, 97, 23)),
        ("CAMPWAR_JOIN_LOGIN_NO_M_grp", 3, 0, "CAMPWAR_JOIN_LOGIN_NO_M_7_0", "Btn_RBase_9_0", "CAMPWAR_JOIN_LOGIN_P_1",
         "CAMPWAR_JOIN_LOGIN_P_Text_1", (9, 7.5, 94, 23))):
    EXTRA_NODES[_CJL].update({
        _g: _n("CanvasPanel", "RootCanvas", (_gx, 32, 106, 28), (_gx + _dx, 32, 122, 28)),
        _mid: _n("Image", _g, (4, 0, 98, 28), (4, 0, 114, 28)),
        _rb: _n("Image", _g, (102, 0, 4, 28), (118, 0, 4, 28)),
        _btn: _n("Button", _g, (3, 3, 100, 23), (3, 3, 116, 23)),
        _txt: _n("TextBlock", _g, _tx0, (22, 7.5, 94, 23), **_TB8),
    })
EXTRA_WINDOWS = {_FISHBOOK: ("FISH_BOOK_WIN (74000)", 200),   # ★ 09.10 мультиязычность п.5 (панель «Удалить книгу»)
                 _CJL: ("CAMPWAR_JOIN_LOGIN (48099)", 54),         # ★ 09.10 ревизия МЕЛК-3 (пара кнопок под подписи по id)
                 _STRENGTH: ("Strength_Part_Win (50500)", 228),   # ★ 09.10 ревизия СРЕДН-1 (пара кнопок)
                 _UNION: ("Item_Union_Win (50600)", 70),           # ★ 09.10 ревизия СРЕДН-2 (пара кнопок, подсказка)
                 _CLAN: ("CLAN_MAKING_WIN", 16), _COMM: ("COMMUNITY_WIN (6400)", 0),
                 _MSG2[0]: ("MESSAGE2_WIN (логин)", 0), _MSG2[1]: ("MESSAGE2_WIN_NEUTRAL (логин)", 0)}


def _merge_overlap_fixes(table):
    """★ 10.10.2026 (волна «наезды текста», `Reports/owner_1010/overlaps.md`): правки генератора
    `asda_ui_overlap_fixes_plan.py` хранятся отдельно (`Tools/data/ui_overlap_fixes.json`, итоговые записи узлов) и
    вливаются ПОСЛЕ перезаписи окон этим генератором, чтобы перегенерация окна 07.10 их не стёрла."""
    import asda_ui_overlap_fixes_plan as OF
    OF.merge_own(table)


def merge_extra(table):
    """Влить EXTRA_NODES в таблицу узел за узлом (прочие узлы окна не трогаются)."""
    for asset, nodes in EXTRA_NODES.items():
        win, delta = EXTRA_WINDOWS[asset]
        spec = table.setdefault(asset, {"window": win, "delta": delta, "nodes": {}})
        spec["nodes"].update(nodes)
    return table


def _vis_short(v):
    m = re.search(r"(VISIBLE|COLLAPSED|HIDDEN|SELF_HIT_TEST_INVISIBLE|HIT_TEST_INVISIBLE)", v or "")
    return m.group(1) if m else v


def _clip(row):
    c = (row.get("x") or {}).get("clip", "")
    m = re.search(r"\.([A-Z_]+)", c)
    return m.group(1) if m else "INHERIT"


def build(dump, only=None, alt_dump=None):
    """План по дампу: {ассет: {"window", "delta", "nodes": {имя: {...}}}} + список проблем. `only` — подстроки имён
    ассетов (окна, уже расширенные в дампе, генерировать повторно НЕЛЬЗЯ: «было» стало бы «стало»). `dump` — вид
    ИМПОРТЁРА; `alt_dump` — дамп собранных ассетов для окон с `alt_from_dump` (их текущий вид — допустимое «было»)."""
    out, problems = {}, []
    alt_rows = None if alt_dump is None else {a: {r["n"]: r for r in rows} for a, rows in alt_dump.items()
                                                if isinstance(rows, list)}
    for asset, spec in WINDOWS.items():
        if only and not any(o in asset for o in only):
            continue
        rows = dump.get(asset)
        if not isinstance(rows, list):
            problems.append("%s: нет в дампе" % asset)
            continue
        by = {r["n"]: r for r in rows}
        used = set()
        nodes = {}
        for name, r in by.items():
            hit = [(i, rule[1], rule[2]) for i, rule in enumerate(spec["rules"])
                   if re.fullmatch(rule[0], name) and (len(rule) < 4 or re.fullmatch(rule[3], r["p"]))]
            if not hit:
                continue
            s = r["slot"]
            if not s:
                problems.append("%s.%s: нет канвас-слота" % (asset, name))
                continue
            off, anch, align, auto, _z = s
            if not (abs(anch[0] - anch[2]) < 1e-6 and abs(anch[1] - anch[3]) < 1e-6) or auto or list(align) != [0, 0]:
                problems.append("%s.%s: анкоры %s / alignment %s / авторазмер — не точечный слот" % (asset, name, anch, align))
                continue
            before = [float(x) for x in off]
            after = list(before)
            for i, op, v in hit:
                used.add(i)
                if op == "move":
                    after[0] += v
                elif op == "grow":
                    after[2] += v
                elif op == "set":
                    after = [float(x) for x in v]
                elif op == "fn":
                    after = [float(x) for x in v(list(after))]
            ent = {"before": before, "after": after}
            tx = r.get("tx")
            if tx and tx[0] == "TB":
                ent["font_before"] = float(tx[6])
                ent["clip_before"] = _clip(r)
            want = spec.get("text", {}).get(name)
            if want:
                ent["font_after"] = float(want.get("font", ent.get("font_before", 8.0)))
                ent["clip_after"] = want.get("clip", ent.get("clip_before", "INHERIT"))
            alts = spec.get("alt_before", {}).get(name)
            if alts:
                ent["alt_before"] = [{"slot": list(a[0]), "font": a[1], "clip": a[2]} for a in alts]
            # ★ 07.10.2026 (8500, вариант А): окно, уже правленное ПРЕЖНИМ решением, — текущий слот узла из второго дампа
            #   (`--alt-dump`, собранные ассеты) становится допустимым «было» (кегль/клип не сверяются — их ведут свои шаги).
            if spec.get("alt_from_dump") and alt_rows is not None:
                cur = alt_rows.get(asset, {}).get(name)
                cs = cur and cur.get("slot")
                if cs and [float(x) for x in cs[0]] not in (before, after):
                    ent.setdefault("alt_before", []).append({"slot": [float(x) for x in cs[0]], "font": None, "clip": None})
            # ★ fishfix 09.10: необязательный узел (его создаёт шаг, идущий позже), многоточие поля, вырезки-девятисрезы кистей.
            if any(re.fullmatch(o, name) for o in spec.get("optional", [])):
                ent["optional"] = True
            for rx_o, (ob, oa) in spec.get("overflow", {}).items():
                if re.fullmatch(rx_o, name) and r["c"] == "TextBlock":
                    ent["overflow_before"], ent["overflow_after"] = ob, oa
            for rx_i, key in spec.get("img_box", {}).items():
                if re.fullmatch(rx_i, name) and r["c"] == "Image":
                    ent["img_box"] = dict(_BOX_SPECS[key])
            for rx_b, states in spec.get("btn_states_box", {}).items():
                if re.fullmatch(rx_b, name) and r["c"] == "Button":
                    ent["btn_states_box"] = {st: dict(_BOX_SPECS[k]) for st, k in states.items()}
            if spec.get("cb_img") and r["c"] == "CheckBox":
                ent["cb_img"] = dict(spec["cb_img"])
                if spec.get("cb_img_alt"):              # ★ 07.10: прочие допустимые «было» размера кистей (8500)
                    ent["cb_img"]["alt_before"] = [list(z) for z in spec["cb_img_alt"]]
            ent["class"] = r["c"]
            ent["parent"] = r["p"]
            nodes[name] = ent
        for i, rule in enumerate(spec["rules"]):
            if i not in used:
                problems.append("%s: правило %r %s ни к чему не применилось" % (asset, rule[0], rule[1]))
        out[asset] = {"window": spec["window"], "delta": spec["delta"], "nodes": nodes}
    return out, problems


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    if "--extra-only" in sys.argv:
        # Только доправки ревизии 07.10 (без дампа): печать, а с --write — запись в таблицу.
        for asset, nodes in EXTRA_NODES.items():
            print("== %s: узлов %d" % (asset.rsplit("/", 1)[-1], len(nodes)))
            for n, e in sorted(nodes.items()):
                print("   %-30s %s → %s%s" % (n, e["before"], e["after"], " кисти BOX" if "btn_box" in e else ""))
        if "--write" in sys.argv:
            old = json.load(open(OUT, encoding="utf-8")) if os.path.isfile(OUT) else {}
            merge_extra(old)
            _merge_overlap_fixes(old)   # ★ 10.10.2026: правки волны «наезды текста» — поверх
            with open(OUT, "w", encoding="utf-8", newline="\n") as f:
                json.dump(old, f, ensure_ascii=False, indent=1, sort_keys=True)
            print("записано:", OUT)
        return
    dump = json.load(open(sys.argv[1], encoding="utf-8"))
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1].split(",")
    alt = None
    if "--alt-dump" in sys.argv:
        alt = json.load(open(sys.argv[sys.argv.index("--alt-dump") + 1], encoding="utf-8"))
    plan, problems = build(dump, only, alt)
    for asset, p in plan.items():
        print("== %s %s Δ=%d: узлов %d" % (asset.rsplit("/", 1)[-1], p["window"], p["delta"], len(p["nodes"])))
        for n, e in sorted(p["nodes"].items()):
            extra = ""
            if "font_after" in e:
                extra = " кегль %s→%s клип %s→%s" % (e["font_before"], e["font_after"], e["clip_before"], e["clip_after"])
            print("   %-36s %-12s %s → %s%s" % (n, e["class"][:12], e["before"], e["after"], extra))
    for p in problems:
        print("ПРОБЛЕМА:", p)
    if "--write" in sys.argv:
        if problems:
            raise SystemExit("есть проблемы — таблица НЕ записана")
        # Ранее записанные окна сохраняются: генератор дописывает/обновляет только окна из WINDOWS.
        old = json.load(open(OUT, encoding="utf-8")) if os.path.isfile(OUT) else {}
        old.update(plan)
        merge_extra(old)   # доправки ревизии 07.10 не теряются при перезаписи окна по дампу
        _merge_overlap_fixes(old)   # ★ 10.10.2026: правки волны «наезды текста» — поверх (их таблица `ui_overlap_fixes.json`)
        with open(OUT, "w", encoding="utf-8", newline="\n") as f:
            json.dump(old, f, ensure_ascii=False, indent=1, sort_keys=True)
        print("записано:", OUT)


if __name__ == "__main__":
    main()
