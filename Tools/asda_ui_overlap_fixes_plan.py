# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_overlap_fixes_plan.py — ГЕНЕРАТОР правок волны «НАЕЗДЫ ТЕКСТА» (10.10.2026, одобрение владельца; отчёт
#   `Reports/owner_1010/overlaps.md`). Наезд — текст вылезает за свою кнопку/поле, заходит под значок или флажок,
#   перекрывает другую надпись или соседнюю кнопку (RU или EN). Находки — скан `asda.Lang.ScanScreen` путём игрока
#   (`AsdaLangScanSelfTest.cpp`), каждая сверена глазами по снимку и с кодом окна (что ставит код при показе).
#
#   ПРАВИЛО ВЛАДЕЛЬЦА 07.10.2026 (память widen-windows-whole-keep-symmetry): если хватает места внутри окна — шире
#   только поле/кнопку; иначе окно расширяется ЦЕЛИКОМ и СИММЕТРИЧНО (рамка −Δ/2…+Δ/2), квадраты (ячейки, значки,
#   флажки) не растягиваются, а смещаются. Подпись кнопки со значком — правее значка (x 22), до края кнопки −3 (приём
#   `asda_ui_icon_caption_fit` / TWOBUT / Strength_Part): поле = самая длинная строка RU/EN + 2. Пара кнопок остаётся
#   симметричной (обе шире на одно d, центр пары на месте).
#
#   ФОРМА ДАННЫХ — та же, что у `asda_ui_widen_windows_plan.WINDOWS` (правила «регэксп узла → move/grow/set/fn»), и тот
#   же построитель `build`: «было» — текущий вид собранного ассета из полного структурного дампа, «стало» — по правилам.
#   Результат вливается УЗЕЛ ЗА УЗЛОМ в закрытую таблицу шага `Tools/data/ui_widen_windows.json` (шаг
#   `asda_ui_widen_windows` её применяет и сверяет, запись — по одному окну, ASDA_WIDEN_ONLY) и хранится отдельно в
#   `Tools/data/ui_overlap_fixes.json` — генератор `asda_ui_widen_windows_plan.py` вливает его обратно после своей записи,
#   поэтому перегенерация окон 07.10 эти правки не теряет. Узел, уже бывший в таблице, не перезаписывается (проблема).
#
# Запуск:  python Tools\asda_ui_overlap_fixes_plan.py <дамп.json> [--only WBP_a,WBP_b] [--write]
#   без --write — печать плана и проверок.
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
TABLE = os.path.join(HERE, "data", "ui_widen_windows.json")      # таблица шага (общая с расширением окон 07.10)
OWN = os.path.join(HERE, "data", "ui_overlap_fixes.json")         # правки этой волны отдельно (для повторного вливания)

_P = "/Game/AsdaAssets/UI/"   # корень окон


def _frame(win, d):
    """Рамка-девятисрез окна `<win>_0_0…8` и ручка `<win>_0_DRAG`: окно шире на 2d симметрично (левая колонка −d,
    средняя −d и +2d, правая +d)."""
    return [(r"%s_0_[036]" % win, "move", -d),
            (r"%s_0_[147]" % win, "move", -d), (r"%s_0_[147]" % win, "grow", 2 * d),
            (r"%s_0_[258]" % win, "move", d)]


def _btn(grp, mid, rb, btn, dx, d):
    """Кнопка JUI в своей группе (середина-кусок, правый край, UButton): группа смещается на dx и шире на d, середина
    тянется, правый край смещается на d."""
    out = [(grp, "grow", d), (mid, "grow", d), (rb, "move", d), (btn, "grow", d)]
    if dx:
        out.append((grp, "move", dx))
    return out


# ── ОКНА ── (регэксп ПОЛНОГО имени узла, операция, значение[, регэксп имени родителя])
def _rescaled_rules(asset, mapping, sets):
    """Правила окна прежней волны (`asda_ui_widen_windows_plan.WINDOWS`) с заменой величин сдвигов/роста по `mapping`
    (старое → новое) и слотов `set` по `sets` (регэксп узла → слот) — для ДОРАСШИРЕНИЯ того же окна тем же приёмом."""
    import asda_ui_widen_windows_plan as WP
    out = []
    for rule in WP.WINDOWS[asset]["rules"]:
        rx, op, v = rule[0], rule[1], rule[2]
        if op == "set":
            if rx not in sets:
                continue                      # слот уже задан прежней волной и в доправке не меняется
            v = sets[rx]
        elif op in ("move", "grow"):
            if v not in mapping:
                raise ValueError("%s: величина %r правила %r не в таблице замены" % (asset, v, rx))
            v = mapping[v]
        out.append((rx, op, v) + tuple(rule[3:]))
    return out


_SOUL = _P + "Player_UI/Soulmate/WBP_Find_Soulmate_Win"

WINDOWS = {
    # ═══ INSDUNGEON_ENTRY_WIN (вход в инстанс-подземелье) ═══ подписи из ассета (кодом не ставятся): «Присоединиться» RU 89 /
    #   «Join» 21, «Отмена» 41 / «Cancel» 35. В .JUI подпись — по центру всей кнопки 78, значок 16×16 с x 5 ⇒ RU заходит
    #   под значок и за края кнопки, «Отмена» — под значок на 3 px. Подписи — правее значка (x 22): поле 88 ⇒ кнопка
    #   78 → 113 (d = 35), вторая — та же d (симметрия пары, центр пары 0 на месте). Отступы кнопок от рамки (40 px) —
    #   как в оригинале ⇒ окно +70 симметрично (240 → 310); текст и полоса прогресса — по центру, на месте.
    _P + "Player_UI/Dungeon/WBP_INSDUNGEON_ENTRY_WIN": {
        "window": "INSDUNGEON_ENTRY_WIN", "delta": 70,
        "rules": _frame("INSDUNGEON_ENTRY_WIN", 35) + [
            (r"INSDUNGEON_ENTRY_WIN_0_DRAG", "move", -35), (r"INSDUNGEON_ENTRY_WIN_0_DRAG", "grow", 70),
        ] + _btn(r"INSDUNGEON_OKBTN_BG_grp", r"INSDUNGEON_OKBTN_BG_3_0", r"INSDUNGEON_OKBTN_RBG_5_0", r"INSDUNGEON_OKBTN", -35, 35)
          + _btn(r"INSDUNGEON_NOBTN_BG_grp", r"INSDUNGEON_NOBTN_BG_8_0", r"INSDUNGEON_NOBTN_RBG_10_0", r"INSDUNGEON_NOBTN", 0, 35) + [
            (r"INSDUNGEON_OKBTN_Text", "set", (22.0, 7.5, 88.0, 23.0)),
            (r"INSDUNGEON_NOBTN_Text", "set", (22.0, 7.5, 87.0, 23.0)),
        ],
    },
    # ═══ CAMPWAR_In_List (ожидание списка битвы) ═══ «Выйти из очереди» RU 92 / «Cancel List» 55 (из ассета) — по центру
    #   кнопки 106 с начала под значком. Правее значка: поле 95 ⇒ кнопка 106 → 120 (d = 14), вторая — так же (центр пары
    #   0.5 на месте). Место внутри окна есть: кнопки −121…122 остаются в ширине панели списка (−128…128) — окно не растёт.
    _P + "Player_UI/CampWar/WBP_CAMPWAR_In_List": {
        "window": "CAMPWAR_In_List", "delta": 0,
        "rules": _btn(r"CAMPWAR_In_List_Ok_grp", r"CAMPWAR_In_List_Ok_31_0", r"Btn_RBase_33_0", r"CAMPWAR_In_List_Ok_B", -14, 14)
          + _btn(r"CAMPWAR_In_List_No_grp", r"CAMPWAR_In_List_No_36_0", r"Btn_RBase_38_0", r"CAMPWAR_In_List_No_B", 0, 14) + [
            (r"CAMPWAR_In_List_Ok_B_Text", "set", (22.0, 7.5, 95.0, 23.0)),
            (r"CAMPWAR_In_List_No_B_Text", "grow", 14),
        ],
    },
    # ═══ Premium_Pick_select (розыгрыш премиум-товара) ═══ подпись ставит код: 110663 «Играть» 36 / «Prize drawing» 67
    #   (`AsdaPremiumGachaSubsystem.cpp:770`); в EN по центру поля 6…103 начало строки касается значка (x 21). Подпись —
    #   правее значка, поле 81 (кнопка 106 не меняется).
    _P + "Player_UI/Premium/WBP_Premium_Pick_select": {
        "window": "Premium_Pick_select", "delta": 0,
        "rules": [(r"Premium_Pick_select_ok_Text", "set", (22.0, 7.5, 81.0, 23.0))],
    },
    # ═══ Subjugate_WIN (задания на истребление) ═══ «Взять задание» / «Receive Quest» 74 (из ассета) — по центру поля 6…103,
    #   «В» под значком. Правее значка, поле 81 — кнопка не меняется.
    _P + "Player_UI/Quest/WBP_Subjugate_WIN": {
        "window": "Subjugate_WIN", "delta": 0,
        "rules": [(r"Subjugate_OKbtn_Text", "set", (22.0, 7.5, 81.0, 23.0))],
    },
    # ═══ CLAN_DELETE_BOX ═══ «Подтвердить» 67 (из ассета) по центру поля 3…103 — «П» под значком. Правее значка, поле 81.
    _P + "Player_UI/Social/WBP_CLAN_DELETE_BOX": {
        "window": "CLAN_DELETE_BOX", "delta": 0,
        "rules": [(r"CLAN_DEL_OKBTN_Text", "set", (22.0, 7.5, 81.0, 23.0))],
    },
    # ═══ CLAN_INCREASE_BOX ═══ 1) «Подтвердить» — как в CLAN_DELETE_BOX. 2) Вопрос 14460 — код ставит 12448
    #   (`AsdaClanSubsystem.cpp:646`): RU 275 / EN 348 при поле 260 ⇒ ДВЕ строки (28 px) от верха −1 до 27, вторая строка
    #   ложится на строку «Взносы» (верх 22). Между линией-разделителем (−10) и строкой взносов (22) — 31 px: поле поднято
    #   на 6 (верх −7), две строки кончаются на 21. Одна строка (короткий текст) — та же позиция под линией.
    #   3) ★ 10.10.2026 (ревизия волны, СРЕДН-2, `owner_1010/overlaps_fixes.md`): строки «сейчас»/«станет» ставит код
    #   (`AsdaClanSubsystem.cpp` SetLoc 12444/12445): EN «■ Currently available clan member openings» 220 и «■ Desired
    #   increase in clan membership openings» 244 px (замер `asda.Lang.ScanScreen live` в игре) при поле 180 — ложились на
    #   числа «30»/«40» и выходили за рамку; RU 12444/12445 — до 150. Места внутри нет ⇒ окно +90 СИММЕТРИЧНО (300 → 390,
    #   растяжение от середины cx 0, a 45): рамка, ручка, полосы и линия тянутся; поля подписей 180 → 270 (≥ 244 + 2 + зазор
    #   до чисел); столбец чисел (и «Взносы»/«Золото» со значками) — на +45 вправо, буллеты и подписи нижнего блока — на
    #   −45 влево, отступы содержимого от рамки (47 / 53) — прежние. Пара кнопок и вопрос 14460 остаются по центру на месте
    #   (`own`); подпись «Золото» (EN «Available gold» 73 в поле 70) — поле +12.
    _P + "Player_UI/Social/WBP_CLAN_INCREASE_BOX": {
        "window": "CLAN_INCREASE_BOX", "delta": 90,
        "stretch": {"a": 45, "cx": 0,
                    "own": [r"CLAN_INC_OKBTN_Base_grp", r"CLAN_INC_CABTN_Base_grp", r"CLAN_INC_TEXT"]},
        "rules": [(r"CLAN_INC_OKBTN_Text", "set", (22.0, 7.5, 81.0, 23.0)),
                  (r"CLAN_INC_TEXT", "set", (-127.0, -7.0, 260.0, 30.0)),
                  (r"CLAN_INC_MONEY", "grow", 12)],
    },
    # ═══ 400 INVEN_WIN (сумка) ═══ ★ 10.10.2026 (СРЕДН-3, замер строк кода в игре `asda.Lang.ScanScreen live`): подписи
    #   трёх кнопок ставит код (`AsdaInventorySubsystem.cpp:807-809`): «Улучшен. снаряжения » 117 / «Gear Upgrade» 71,
    #   «Создание предмета» 102 / «Item Crafting» 63, «Демонтаж снаряжения » 119 / «Gear Disassembly» 92 px по центру поля
    #   6…143 — в RU начало строки под значком (x 3…19). Правее значка, поле 121 (≥ 119 + 2), кнопка 146 не меняется.
    _P + "Player_UI/Inventory/WBP_INVEN_WIN": {
        "window": "INVEN_WIN (400)", "delta": 0,
        "rules": [(r"Inven_(ItemUpgrade|Smelting|Disassembly)_Btn_Text", "set", (22.0, 7.5, 121.0, 23.0))],
    },
    # ═══ 50300 ItemOptionValue_WIN (смена опции) ═══ ★ 10.10.2026 (СРЕДН-3, замер строк кода в игре): подпись кнопки
    #   «Изменить» 51 / EN «Change option» 71 px по центру поля 6…103 — в EN начало строки на значке (x 5…21). Правее
    #   значка, поле 81 (кнопка 100 не меняется) — тот же приём, что Subjugate_WIN / CLAN_DELETE_BOX.
    _P + "Player_UI/Craft/WBP_ItemOptionValue_WIN": {
        "window": "ItemOptionValue_WIN (50300)", "delta": 0,
        "rules": [(r"ItemOptionValue_OK_B_Text", "set", (22.0, 7.5, 81.0, 23.0))],
    },
    # ═══ CLAN_JION (запрос в клан) ═══ «Запросить приглашение» RU 125 / «Request Invitation» 88 (из ассета) в кнопке 106 —
    #   за обоими краями кнопки, под значком и на кнопке «Отмена». Правее значка: поле 127 ⇒ кнопка 106 → 152 (d = 46),
    #   «Отмена» — та же d (пара симметрична, центр −1 на месте). Отступ кнопок от рамки (41) — как в оригинале ⇒ окно
    #   +92 симметрично (300 → 392): рамка, ручка, полоса-подложка шапки; текст и поле ввода — по центру, на месте.
    _P + "Player_UI/Social/WBP_CLAN_JION": {
        "window": "CLAN_JION", "delta": 92,
        "rules": _frame("CLAN_JION", 46) + [
            (r"CLAN_JION_0_DRAG", "move", -46), (r"CLAN_JION_0_DRAG", "grow", 92),
            (r"CLAN_JION_Base_1_0", "move", -46), (r"CLAN_JION_Base_1_0", "grow", 92),
        ] + _btn(r"CLAN_JOIN_OKBTN_Base_grp", r"CLAN_JOIN_OKBTN_Base_4_0", r"CLAN_JOIN_OKBTN_RBase_6_0", r"CLAN_JOIN_OKBTN", -46, 46)
          + _btn(r"CLAN_JOIN_CANCELBTN_Base_grp", r"CLAN_JOIN_CANCELBTN_Base_9_0", r"CLAN_JOIN_CANCELBTN_RBase_11_0",
                 r"CLAN_JOIN_CANCELBTN", 0, 46) + [
            (r"CLAN_JOIN_OKBTN_Text", "set", (22.0, 7.5, 127.0, 23.0)),
            (r"CLAN_JOIN_CANCELBTN_Text", "grow", 46),
        ],
    },
    # ═══ SHOUTING_WIN («Крикнуть на весь мир») ═══ «Крикнуть» 47 (из ассета) по центру поля 6…75 — «К» под значком.
    #   Правее значка, поле 53 (кнопка 78 не меняется; «Отмена» уже правее значка).
    _P + "Player_UI/Social/WBP_SHOUTING_WIN": {
        "window": "SHOUTING_WIN", "delta": 0,
        "rules": [(r"Shouting_OKBtn_Text", "set", (22.0, 7.5, 53.0, 23.0))],
    },
    # ═══ 20200 Item_Initializ_Win (полный сброс заточки) ═══ строки ставит код (`AsdaItemResetSubsystem.cpp`):
    #   • кнопка «Сбросить» (из ассета, 42 px справа от лотка 5 ячеек) — RU 50 / «Reset» 30 при поле 36 — за края кнопки;
    #   • подпись 50032 «Количество улучшений» 120 (EN 75) в поле 100 — заходит на рамку значения «20%»;
    #   • подсказка (98504 «Сбрасывать улучшения…» RU 518 / EN 403) — ТРИ строки (42 px) с верха 242 при поле 211 — третья
    #     строка ложится на сетку ячеек (верх 275).
    #   Места внутри нет (лоток + кнопка = ширина панели) ⇒ окно +32 симметрично (226 → 258) растяжением: рамки, шапка,
    #   панели, строки шире; сетки ячеек, лоток (у левого края своей панели) и кнопки — квадраты, не тянутся. Кнопка
    #   «Сбросить» 42 → 58 влево (правый край у края панели, как было), подпись 52; подписи строк 100 → 132 (значения справа
    #   от них смещены растяжением на +16); подсказка шире (243) и поднята к верху своей полосы (229…271 — три строки
    #   над сеткой). Узлы вкладки «Предмет для сброса» (уже в таблице 09.10) — в координатах своей панели, как были.
    _P + "Player_UI/Craft/WBP_Item_Initializ_Win": {
        "window": "Item_Initializ_Win (20200)", "delta": 32,
        "stretch": {"a": 16, "cx": 428, "own": [r"Item_Initializ_TargetTitle_132_0", r"Item_Initializ_TargetTitle_Text"]},
        "rules": [
            (r"Item_Initializ_BtnBase_grp", "move", -16), (r"Item_Initializ_BtnBase_grp", "grow", 16),
            (r"Item_Initializ_BtnBase_151_0", "grow", 16), (r"Btn_RBase_153_0", "move", 16),
            (r"Item_Initializ_Btn", "grow", 16), (r"Item_Initializ_Btn_Text", "grow", 16),
            (r"Item_Initializ_(Grade|Power|Chance)", "grow", 32),
            (r"Item_Initializ_(Grade_22|Power_26|Chance_30)_HIT", "grow", 32),
            (r"Item_Initializ_GuideTxt", "set", (5.0, 2.0, 243.0, 45.0)),
        ],
    },
    # ═══ 1300 NPCSHOP_WIN (магазин NPC) ═══ кнопка справа от лотка 5 ячеек — 37 px (поле 31): код ставит 13250 «Продать» RU 44
    #   / «Sell» 18 и 13249 «Купить» 36 / «Buy» 20 (`AsdaNpcShopSubsystem.cpp:432-433`) — RU за края кнопки. Места внутри нет
    #   (лоток + кнопка = ширина панели) ⇒ окно +32 симметрично растяжением (dz 24: сетка 6×6 и лоток — по-прежнему колонка
    #   в колонку, на месте); кнопка 37 → 53 вправо (поле 47), правый отступ до рамки панели — прежние 7 px. Левая шапка
    #   (уголок + плашка заголовка) — жёсткая, как картинка.
    _P + "Player_UI/Trade/WBP_NPCSHOP_WIN": {
        "window": "NPCSHOP_WIN (1300)", "delta": 32,
        "stretch": {"a": 16, "cx": 703, "dz": 24, "rigid": [r"NpcShop_LBase_grp"],
                    # пустая полоса-подпись блока сумм: её слот ведёт шаг `asda_ui_label_band` (идёт ПОСЛЕ этого шага и
                    # сверяет точный вид 228 × …) — растяжение её не трогает
                    "own": [r"NpcShop_Win03"]},
        "rules": [
            (r"NpcShop_LBase_grp", "move", -16),   # жёсткая левая шапка — у левого края рамки, как была
            (r"NpcShop_SellBut_Base_grp", "grow", 16), (r"NpcShop_SellBut_Base_129_0", "grow", 16),
            (r"NpcShop_SellBut_RBase_131_0", "move", 16),
            (r"NPCSHOP_(SELL|BUY)BUT", "grow", 16), (r"NPCSHOP_(SELL|BUY)BUT_Text", "grow", 16),
        ],
    },
    # ═══ EventDungeon_Win (вход в событийное подземелье) ═══ столбец состояния 53 px: «Переполнено» 69 (из ассета) — за
    #   край строки и рамки. Место внутри строки есть: граница столбцов на 20 влево (имя 184 → 164; имя — до 14 знаков,
    #   ≈ 100 px), состояние 53 → 73. Поле имени — с обрезкой по краю (образец «Название подземелья до 14 символов» 197 px
    #   длиннее предела в 14 знаков и иначе ложится на состояние). Окно не растёт.
    _P + "Player_UI/Dungeon/WBP_EventDungeon_Win": {
        "window": "EventDungeon_Win", "delta": 0,
        "rules": [
            (r"EDG_Name\d\d_\d+_0", "grow", -20), (r"EDG_Name\d\d", "grow", -20),
            (r"Edg_Status\d\d_\d+_0", "move", -20), (r"Edg_Status\d\d_\d+_0", "grow", 20),
            (r"Edg_Status\d\d", "move", -20), (r"Edg_Status\d\d", "grow", 20),
        ],
        "text": {"EDG_Name%02d" % _i: {"clip": "CLIP_TO_BOUNDS"} for _i in range(10)},
    },
    # ═══ 43700 Pet_Evolution_Win (развитие питомца) ═══ заголовки трёх колонок справа ставит код: 109964 «Основной питомец»
    #   RU 98 / «Main Pet» 43, «Материал 1/2» 60 / «Material 1/2» 48 (`AsdaPetEvolutionSubsystem.cpp:578-582`) — плашка
    #   64 (поле 60) в колонке 116: RU-заголовок за обе стороны плашки. Место в колонке есть: плашка 64 → 104 по центру
    #   колонки (x 28 → 8), поле 100 — у ВСЕХ трёх колонок одинаково (симметрия ряда). Окно не растёт.
    _P + "Player_UI/Pet/WBP_Pet_Evolution_Win": {
        "window": "Pet_Evolution_Win (43700)", "delta": 0,
        "rules": [
            (r"b[123]_grp", "move", -20), (r"b[123]_grp", "grow", 40),
            (r"b1_29_0|b2_37_0|b3_43_0|b[123]_Text", "grow", 40),
            (r"Pet_Evolution_R_Text[234]_(30|38|44)_0", "grow", 40),
            (r"Pet_Evolution_R_Text[234]", "grow", 40),
        ],
    },
    # ═══ 16000 EXTRACT_WIN (перенос опций) ═══ подпись флажка «использовать свиток» ставит код: 100201 «Использовать
    #   выбранный предмет» RU 180 / «Use the selected item» 106 (`AsdaExtractShopSubsystem.cpp:521`) — поле 125 справа от
    #   флажка, RU выходит за панель свитков и рамку окна. Панель свитков = вся ширина нижнего блока (места справа нет),
    #   по высоте внутри панели — 34 px ⇒ подпись в ДВЕ строки: перенос по ширине 125 (`wrap_text_at`, не AutoWrap —
    #   в канвасе он не получает ширину), поле 125 × 28 с верха флажка. EN — одна строка.
    _P + "Player_UI/Trade/WBP_EXTRACT_WIN": {
        "window": "EXTRACT_WIN (16000)", "delta": 0,
        "rules": [(r"EXTRACT_ScrollText", "set", (18.0, 0.0, 125.0, 28.0))],
        "wrap": {"EXTRACT_ScrollText": 125.0},
    },
    # ═══ 4000 WARE_WIN (склад) ═══ кнопка справа от лотка 5 ячеек — 37 px (поле 31): код ставит 13243 «Забрать» RU 43 /
    #   «Get» 19 и 13242 «Положить» 51 / «Keep» 26 (`AsdaWarehouseSubsystem.cpp:774`) — в игре RU обрезано до «абрат».
    #   Тот же случай, что NPCSHOP_WIN: окно +44 симметрично растяжением (dz 24 — сетка хранилища и лоток на месте),
    #   кнопка 37 → 59 влево (поле 53 ≥ 51 + 2; правый отступ до рамки панели и зазор 3 px до лотка — прежние). Окна-сумки WAREBAG1…8 не трогаются: их позицию
    #   ставит код от угла окна, к ней прибавлена прибавка ширины окна (`GetWareWinOrigin`, 10.10.2026). Левая шапка
    #   (значок + плашка-надпись «Хранилище» картинкой) — жёсткая, как в NPCSHOP_WIN. Пустая полоса-подпись Ware_Win3 —
    #   за шагом `asda_ui_label_band` (как NpcShop_Win03).
    _P + "Player_UI/Inventory/WBP_WARE_WIN": {
        "window": "WARE_WIN (4000)", "delta": 44,
        "stretch": {"a": 22, "cx": 153, "dz": 24, "skip": [r"WAREBAG\d_WIN_grp"], "rigid": [r"Ware_RBase_grp_1"],
                    "own": [r"Ware_Win3"]},
        "rules": [
            (r"Ware_RBase_grp_1", "move", -22),    # жёсткая левая шапка — у левого края рамки, как была
            (r"WareKeep_ButBase_grp", "move", -22), (r"WareKeep_ButBase_grp", "grow", 22),
            (r"WareKeep_ButBase_319_0", "grow", 22), (r"WareKeep_ButRBase_321_0", "move", 22),
            (r"WARE(TAKE|KEEP)_BUT", "grow", 22), (r"WARE(TAKE|KEEP)_BUT_Text", "grow", 22),
        ],
    },
    # ═══ NPC_LINE_NPC (диалог NPC) ═══ кнопки «Назад»/«Вперёд» в разметке стоят наискосок и показываются вместе (шаги
    #   квеста, `AsdaNpcDialogSubsystem.cpp:1373-1380`); подпись «назад» — по центру поля 100 при кнопке 6…84: EN
    #   «Previous» 44 заходит на кнопку «Next» (3 px). Поле подписи — по центру СВОЕЙ кнопки (0…90, середина 45).
    _P + "Player_UI/Quest/WBP_NPC_LINE_NPC": {
        "window": "NPC_LINE_NPC", "delta": 0,
        "rules": [(r"NPC_PRE_BUT_TXT00", "set", (0.0, 9.5, 90.0, 15.0))],
    },
    # ═══ 8200 Find_Soulmate_Win (поиск Друга) — ДОРАСШИРЕНИЕ ═══ 07.10 столбец D6 расширен 36 → 64 под «Сравнение» (56);
    #   09.10 ведомость строк (`gametext_overrides.json`) вернула id 110809 и 110811 смысл EN «Matching» — «Совместимость»
    #   83 px, и в D6 (поле 64, клип) заголовок обрезается до «Совместимост». Тот же приём 07.10 ещё на Δ = 22: D6 64 → 86
    #   (поле 86 ≥ 83 + 2), всё правее столбца и правая часть окна +22, окно +22 симметрично (±11), сердечки — по центру
    #   столбца (+11). Записи узлов 07.10 перестраиваются: «было» — вид импортёра, прежнее «стало» — допустимое «было».
    _SOUL: {
        "window": "Find_Soulmate_Win (8200)", "delta": 22, "rewiden": True,
        "rules": _rescaled_rules(_SOUL, {-14: -11, 14: 11, 28: 22}, {r"Find_SMate_List_D6": (0.0, 10.5, 86.0, 35.0)}),
    },
    # ═══ 67200 HONOR_itemmall_Win (военный магазин) ═══ ★ 10.10.2026 (ревизия `owner_1010/overlaps_fixes_REVIEW.md` МЕЛК-1/2,
    #   отчёт `overlaps_fixes2.md`). Замер `asda.Lang.ScanScreen live` по всем 4 категориям × 3 страницам в игре:
    #   1) имя товара (код: `MakeLabelLike` по плашке 67211+10i, 212 × 22) — RU до 216 px («Перчатки/Баллиста справедливого
    #      львиного сердца»), EN до 212 («Courageous Lion Heart Two-handed Sword»): конец имени упирается в имя соседней
    #      карточки («сердцаПоножи»). Оригинал (`TextFont_DrawText@0x12C8F00`, font_text.c:8087-8236) не режет строку
    #      многоточием, а ПЕРЕНОСИТ по словам в ширину контрола; плашка — в одну строку (22 px), вторая строка легла бы
    #      на кнопку «Купить». Решение владельца 07.10 (память widen-windows-whole-keep-symmetry): шире окно целиком;
    #   2) звание (67215+10i, вправо, поле 104) — до 128 px «[Staff sergeant] или выше»: левый край в 1–2 px от значка товара.
    #   Каждая карточка шире на D = 12 (216 → 228): плашка имени, рамка и рамка выбора тянутся; кнопка «Купить» и столбцы
    #   звания/цены (с их значками) — к правому краю карточки (+12); значок товара 64×64 и знак армии — квадраты, на месте
    #   (у левого края). Имя: поле 224 ≥ 216 + 8; звание: зазор до значка 2 → 14. Столбцы карточек: x 5 / 221 / 437 →
    #   5 / 233 / 461 (зазоры между карточками прежние). Панель карточек и полосы шапки +36, окно +36 (содержимое симметрично, рост влево — см. ниже):
    #   рамка 668 → 704, заголовок (левая часть у левого края, правая с крестиком — у правого), полоса вкладок шире (кнопки
    #   категорий у левого края, как были), строка «Монеты славы» — у правого края, листалка страниц — по центру. Подписи карточек код ставит по прямоугольникам хит-зон (`AsdaWarShopSubsystem.cpp` SetupControls)
    #   — к координатам не привязан.
    _P + "Player_UI/Premium/WBP_HONOR_itemmall_Win": {
        # ★★ 10.10.2026, вторая доправка (`owner_1010/esc_ranks.md`): названия званий стали РУССКИМИ (ведомость
        #   `data/warrank_ru_overrides.json`), самое длинное «[Рядовой 1-го класса] или выше» — 164 px (замер
        #   `asda.Lang.ScanScreen live`, все 4 категории × 3 страницы; EN «[Staff sergeant] or above» короче). Строка звания
        #   выравнена вправо к значку звания (правый край столбца — rel 210 карточки при D = 12), значок товара 64×64 — rel 4…68.
        #   Чтобы строка 164 px встала без выхода за поле и с зазором 4 px до значка товара: правый край ≥ 68 + 4 + 164 = 236
        #   ⇒ карточка шире ещё на 26: D = 12 → 38 (216 → 254). Поле звания — строка ассета карточки 0
        #   `HONOR_itemmall_Win_TXT00` (в `NoImage_grp`, x −4, 104 × 15): влево на 62 (x −66, ширина 166 ≥ 164 + 2; левый край
        #   rel 70 — в 2 px от значка товара, сама строка с зазором 4). Подписи карточек 1…11 код строит в оформлении этой
        #   строки (`AsdaWarShopSubsystem.cpp` SetupControls, `ColumnLabel`: тот же сдвиг/прибавка от хит-зоны) — правка
        #   только в ассете. Окно: 3 столбца ⇒ Δ = 3·38 = 114 (рамка 668 → 782), растёт
        #   ВЛЕВО (правый край на месте, см. ниже); при 1024×768 левый край 1022 − 782 = 240 — окно целиком на экране.
        "window": "HONOR_itemmall_Win (67200)", "delta": 114,
        # ★ Окно привязано к ПРАВОМУ краю (анкор узлов x = 1, правый край рамки = правый край окна): при ±Δ/2 правая часть
        #   уходила за край экрана 1024 (проверено снимком путём игрока). Поэтому окно растёт ВЛЕВО на Δ (весь сдвиг
        #   «симметрично от середины» плюс −Δ/2): правый край на месте, внутри окна всё симметрично — левые части −Δ,
        #   правые на месте, середина (листалка страниц) −Δ/2.
        "rules": [(r"HONOR_itemmall_Win_0_[036]", "move", -114),
                  (r"HONOR_itemmall_Win_0_[147]", "move", -114), (r"HONOR_itemmall_Win_0_[147]", "grow", 114)] + [
            # окно: панели шире влево (левые края −Δ), строка монет у правого края на месте, листалка по центру (−Δ/2)
            (r"Win_Tab_grp|Box_Brown_grp|WinMd_CBar_grp|HONOR_item_Radio_grp", "move", -114),
            (r"Win_Tab_grp|Box_Brown_grp|WinMd_CBar_grp|HONOR_item_Radio_grp", "grow", 114),
            (r"Store_Slash_grp", "move", -57),
            # шапка: полоса тянется, правая часть с крестиком и (скрытые) флажки — к правому краю
            (r"WinMd_CBar_1_0", "grow", 114), (r"WinMd_RBar_grp", "move", 114),
            (r"Box_Brown_8_0", "grow", 114), (r"BigBox_Check_grp(_1)?", "move", 114),
            # полоса вкладок: подложка правой половины тянется, кнопки категорий на месте
            (r"Box_BarRadioBtn_Bg_247_0", "grow", 114),
            # панель карточек (девятисрез 11 px)
            (r"Win_Tab_13_[147]", "grow", 114), (r"Win_Tab_13_[258]", "move", 114),
            # карточки: столбцы 1 и 2 сдвигаются на D и 2D, все карточки шире на D
            (r"Box_White1Stroke_Ot15_grp(_\d+)?", "grow", 38),
            (r"Box_White1Stroke_Ot15_grp_(1|4|7|10|13|16|19)", "move", 38),
            (r"Box_White1Stroke_Ot15_grp_(2|5|8|11|14|17|20)", "move", 76),
            # внутри карточки: рамка и рамка выбора (по 8 кусков), плашка имени (и её скрытый образец) тянутся
            (r"Box_White(1Stroke_Ot15|3Stroke)_\d+_[16]", "grow", 38, r"Box_White1Stroke_Ot15_grp(_\d+)?"),
            (r"Box_White(1Stroke_Ot15|3Stroke)_\d+_[247]", "move", 38, r"Box_White1Stroke_Ot15_grp(_\d+)?"),
            (r"Box_WhiteOt20_\d+_(0|txt)", "grow", 38, r"Box_White1Stroke_Ot15_grp(_\d+)?"),
            # кнопка «Купить» и столбцы звания/цены со значками — к правому краю карточки
            (r"BKBtn_W122_grp(_\d+)?|NoImage_grp(_\d+)?", "move", 38, r"Box_White1Stroke_Ot15_grp(_\d+)?"),
            # поле звания (образец всех карточек) — шире влево до значка товара
            (r"HONOR_itemmall_Win_TXT00", "move", -62, r"NoImage_grp"),
            (r"HONOR_itemmall_Win_TXT00", "grow", 62, r"NoImage_grp"),
            # ★ 10.10.2026, третья доправка (`owner_1010/barber_minors.md`, М-5 ревизии `esc_ranks_REVIEW.md`): EN «Available
            #   Honor Coins» (67335 `HONOR_item_T1`, loc строки монет) — 108 px в поле 100. Строка монет стоит у правого края
            #   окна: [значок 0…15][подпись 16…116][число 116…232]; слева до листалки страниц (−436…−344) свободно ~100 px.
            #   Внутри окна места хватает ⇒ окно не шире, шире только поле (правило владельца 07.10): группа строки −10 влево и
            #   +10 шире, подпись и её хит-зона +10 (100 → 110 ≥ 108 + 2), блок числа +10 внутри группы — его правый край
            #   и значок «E» остаются на месте у правого края окна; значок-пуля уходит влево вместе с группой.
            (r"HONOR_item_T1_grp", "move", -10),
            (r"HONOR_item_T1_grp", "grow", 10),
            (r"HONOR_item_T1|HONOR_item_T1_255_HIT", "grow", 10, r"HONOR_item_T1_grp"),
            (r"HONOR_item_T2_grp", "move", 10, r"HONOR_item_T1_grp"),
        ],
    },
}


# ── РАСТЯЖЕНИЕ ОКНА ЦЕЛИКОМ («stretch») ──
#   Для окон, где строке не хватает места внутри, окно шире на 2a СИММЕТРИЧНО относительно своей середины cx. Иерархия JUI
#   расплющена лишь частично (группы `_grp` с детьми в своих координатах), поэтому правило задаётся в АБСОЛЮТНЫХ координатах
#   и переводится обратно в координаты родителя для каждого узла:
#     • узел, перекрывающий середину (левый край < cx − dz и правый > cx + dz) и НЕ жёсткий, растягивается: L − a, R + a
#       (рамки-девятисрезы, полосы, панели, строки, поля текста по центру);
#     • прочие узлы смещаются как целое: середина левее cx − dz — на −a, правее cx + dz — на +a, в мёртвой зоне — на месте
#       (dz по умолчанию 8 px: сетки и кнопки «по центру» окна остаются на месте);
#     • ЖЁСТКИЕ (квадраты не тянутся — закон владельца): кнопки/флажки и группы кнопок (подгруппа с UButton/UCheckBox
#       шириной ≤ 160), группы-сетки (≥ 4 детей-квадратов), квадратные картинки (|W−H| ≤ 4, W ≤ 64) и группы-ячейки/рамки
#       (|W−H| ≤ 4, W ≤ 96), плюс узлы `rigid` окна. Жёсткий узел смещается целиком, его поддерево не трогается;
#     • `skip` — узлы (с поддеревом), которые окно не трогает (скрытые чужие страницы и т. п.), `own` — узлы, которые
#       задаются только явными правилами окна (без явного правила остаются в координатах родителя, как были).
#   Правила, сгенерированные растяжением, идут ПЕРВЫМИ; явные правила окна (set/move/grow) — после них (set заменяет,
#   move/grow добавляются к растянутому).
import re as _re


def _stretch_rules(rows, spec):
    """Правила `set` растяжения окна (см. выше) по строкам дампа одного ассета."""
    st = spec["stretch"]
    a, cx, dz = float(st["a"]), float(st["cx"]), float(st.get("dz", 8.0))
    rigid_rx = [_re.compile(x) for x in st.get("rigid", [])]
    flex_rx = [_re.compile(x) for x in st.get("flex", [])]        # явно ГИБКИЕ (растягиваются, хоть и похожи на жёсткие)
    skip_rx = [_re.compile(x) for x in st.get("skip", [])]
    own_rx = [_re.compile(x) for x in st.get("own", [])]
    by = {r["n"]: r for r in rows}
    kids = {}
    for r in rows:
        kids.setdefault(r["p"], []).append(r)

    def slot(r):
        return r["slot"][0] if r["slot"] else None

    def sub_has_btn(n):
        st_ = [n]
        while st_:
            x = st_.pop()
            for c in kids.get(x, []):
                if c["c"] in ("Button", "CheckBox"):
                    return True
                st_.append(c["n"])
        return False

    def is_square(r):
        s = slot(r)
        if s is None or abs(s[2] - s[3]) > 4:
            return False
        return (r["c"] in ("Image", "AsdaAnimatedImage") and s[2] <= 64) or (r["c"] == "CanvasPanel" and s[2] <= 96)

    def is_rigid(r):
        n = r["n"]
        if any(x.fullmatch(n) for x in flex_rx):
            return False
        if any(x.fullmatch(n) for x in rigid_rx):
            return True
        if r["c"] in ("Button", "CheckBox") or is_square(r):
            return True
        if r["c"] == "CanvasPanel":
            s = slot(r)
            if s is not None and s[2] <= 160 and sub_has_btn(n):
                return True
            ch = kids.get(n, [])
            if sum(1 for c in ch if is_square(c)) >= 4:   # сетка ячеек
                return True
        return False

    def f(x):
        return x - a if x < cx - dz else (x + a if x > cx + dz else x)

    out = []

    def go(p, old_px, new_px):
        """Узлы родителя `p`: старый/новый абсолютный левый край родителя — old_px/new_px."""
        for r in kids.get(p, []):
            n, s = r["n"], slot(r)
            if s is None or any(x.fullmatch(n) for x in skip_rx) or any(x.fullmatch(n) for x in own_rx):
                continue                            # `own` — только явные правила окна, координаты в родителе как были
            L, W = old_px + s[0], s[2]
            R = L + W
            if is_rigid(r):
                c = (L + R) * 0.5
                d = f(c) - c
                nl, nr, deep = L + d, R + d, False
            elif L < cx - dz and R > cx + dz:
                nl, nr, deep = L - a, R + a, True
            else:
                c = (L + R) * 0.5
                d = f(c) - c
                nl, nr, deep = L + d, R + d, False   # смещение целиком: дети едут вместе с родителем
            rel = nl - new_px
            if abs(rel - s[0]) > 1e-6 or abs((nr - nl) - W) > 1e-6:
                out.append((_re.escape(n), "set", (round(rel, 3), s[1], round(nr - nl, 3), s[3]), _re.escape(p)))
            if deep:
                go(n, L, nl)

    go("RootCanvas", 0.0, 0.0)
    return out


def _paths_ok(dump):
    """Пути окон WINDOWS есть в дампе (иначе — опечатка в пути)."""
    return [a for a in WINDOWS if a not in dump]


def build(dump, only=None):
    """План по дампу построителем `asda_ui_widen_windows_plan.build` (те же правила и проверки слотов)."""
    import asda_ui_widen_windows_plan as WP
    saved = WP.WINDOWS
    specs = {}
    for asset, spec in WINDOWS.items():
        s = dict(spec)
        if "stretch" in spec and isinstance(dump.get(asset), list):
            s["rules"] = _stretch_rules(dump[asset], spec) + list(spec.get("rules", []))
        specs[asset] = s
    try:
        WP.WINDOWS = specs
        plan, problems = WP.build(dump, only)
    finally:
        WP.WINDOWS = saved
    # ★ перенос по ширине (`wrap`: узел → px): «было» — из дампа (wrap_text_at, индекс 5 подписи текста)
    for asset, p in plan.items():
        for n, at in WINDOWS[asset].get("wrap", {}).items():
            ent = p["nodes"].get(n)
            row = next((r for r in dump[asset] if r["n"] == n), None)
            if ent is None or row is None or not row.get("tx"):
                problems.append("%s.%s: перенос задан, а узла нет в плане/дампе" % (asset, n))
                continue
            ent["wrap_at_before"], ent["wrap_at_after"] = float(row["tx"][5]), float(at)
    return plan, problems


def _state_of_after(e):
    """Вид узла «стало» записи как допустимое «было» (`alt_before`): слот, кегль, клип."""
    return {"slot": list(e["after"]), "font": e.get("font_after", e.get("font_before")),
            "clip": e.get("clip_after", e.get("clip_before"))}


def total_delta(asset, own_delta):
    """★ 10.10.2026 (ревизия, МЕЛК-4): поле `delta` таблицы шага — ИТОГОВАЯ прибавка ширины окна: прибавка волны 07.10
    (`asda_ui_widen_windows_plan.WINDOWS` / `EXTRA_WINDOWS`) + прибавка этой волны. Раньше `setdefault` оставлял в таблице
    шага первое попавшее значение (Item_Initializ 0, WARE 32, Find_Soulmate 28 против 32 / 44 / 22 в своей таблице).
    Поле только печатается шагом (`asda_ui_widen_windows.py`), на запись не влияет."""
    import asda_ui_widen_windows_plan as WP
    base = WP.WINDOWS.get(asset, {}).get("delta", 0)               # прибавка волны 07.10 (окно из её списка)
    if asset in WP.EXTRA_WINDOWS:
        base = WP.EXTRA_WINDOWS[asset][1]                          # окно из её добавочного списка
    return base + own_delta


def merge_into(table, plan, own_prev=None):
    """Влить план в таблицу шага УЗЕЛ ЗА УЗЛОМ. Тот же самый узел — без изменений. Узел, уже бывший в таблице с ДРУГИМИ
    значениями:
      • правка ЭТОЙ ЖЕ волны (узел есть в `own_prev`) или окно с флагом `rewiden` (доправка окна прежней волны) —
        запись ПЕРЕСТРАИВАЕТСЯ: «было» — исходное «было» таблицы (вид импортёра), «стало» — новое, а прежнее «стало»
        (то, что сейчас лежит в ассете) добавляется в допустимые «было» (`alt_before`) — идемпотентно и без «осмотреть»;
      • иначе — проблема (чужую правку не перезаписываем)."""
    problems = []
    own_prev = own_prev or {}
    for asset, p in plan.items():
        spec = table.setdefault(asset, {"window": p["window"], "delta": p["delta"], "nodes": {}})
        spec["delta"] = total_delta(asset, p["delta"])   # ★ МЕЛК-4: итоговая прибавка, не первое попавшее значение
        rewiden = bool(WINDOWS.get(asset, {}).get("rewiden"))
        mine = set(own_prev.get(asset, {}).get("nodes", {}))
        for n, e in p["nodes"].items():
            old = spec["nodes"].get(n)
            if old is None or old == e:
                spec["nodes"][n] = e
                continue
            if not (rewiden or n in mine):
                problems.append("%s.%s: узел уже в таблице с другими значениями — не перезаписываю" % (asset, n))
                continue
            new = dict(e)
            new["before"] = list(old["before"])
            for k in ("font_before", "clip_before", "wrap_at_before", "overflow_before"):
                if k in old:
                    new[k] = old[k]
            for k in ("font_after", "clip_after"):   # кегль/клип «стало» прежней записи (сверка «стало» без них сломается)
                if k in old and k not in e:
                    new[k] = old[k]
            alts = [dict(a) for a in old.get("alt_before", [])] + [dict(a) for a in e.get("alt_before", [])]
            if list(old["after"]) != list(new["after"]):
                alts.append(_state_of_after(old))
            if list(e["before"]) != list(new["before"]):
                alts.append({"slot": list(e["before"]), "font": e.get("font_before"), "clip": e.get("clip_before")})
            uniq = []
            for a in alts:
                if a not in uniq and list(a["slot"]) != list(new["before"]) and list(a["slot"]) != list(new["after"]):
                    uniq.append(a)
            if uniq:
                new["alt_before"] = uniq
            else:
                new.pop("alt_before", None)
            spec["nodes"][n] = new
    return problems


def merge_own(table):
    """Повторно влить сохранённые правки волны (`ui_overlap_fixes.json`) — зовёт генератор расширения окон после записи."""
    if not os.path.isfile(OWN):
        return []
    with open(OWN, encoding="utf-8") as f:
        own = json.load(f)
    # Записи своей таблицы — ИТОГОВЫЕ (уже перестроенные при записи волны): кладутся поверх как есть.
    for asset, p in own.items():
        spec = table.setdefault(asset, {"window": p["window"], "delta": p["delta"], "nodes": {}})
        spec["delta"] = total_delta(asset, p["delta"])   # ★ МЕЛК-4: итоговая прибавка (07.10 + эта волна), идемпотентно
        spec["nodes"].update(p["nodes"])
    return []


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    dump = json.load(open(sys.argv[1], encoding="utf-8"))
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    problems = ["нет в дампе: %s" % a for a in _paths_ok(dump)]
    plan, pr = build(dump, only)
    problems += pr
    for asset, p in sorted(plan.items()):
        print("== %s %s Δ=%d: узлов %d" % (asset.rsplit("/", 1)[-1], p["window"], p["delta"], len(p["nodes"])))
        for n, e in sorted(p["nodes"].items()):
            print("   %-40s %-12s %s → %s" % (n, e["class"][:12], e["before"], e["after"]))
    if "--write" in sys.argv:
        table = json.load(open(TABLE, encoding="utf-8")) if os.path.isfile(TABLE) else {}
        own = json.load(open(OWN, encoding="utf-8")) if os.path.isfile(OWN) else {}
        problems += merge_into(table, plan, own)
        for p in problems:
            print("ПРОБЛЕМА:", p)
        if problems:
            raise SystemExit("есть проблемы — таблицы НЕ записаны")
        for asset, p in plan.items():          # своя таблица волны: ИТОГОВЫЕ записи узлов плана (после перестройки)
            own[asset] = {"window": p["window"], "delta": p["delta"],
                          "nodes": {n: table[asset]["nodes"][n] for n in p["nodes"]}}
        with open(TABLE, "w", encoding="utf-8", newline="\n") as f:
            json.dump(table, f, ensure_ascii=False, indent=1, sort_keys=True)
        with open(OWN, "w", encoding="utf-8", newline="\n") as f:
            json.dump(own, f, ensure_ascii=False, indent=1, sort_keys=True)
        print("записано:", TABLE, OWN)
    else:
        for p in problems:
            print("ПРОБЛЕМА:", p)


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля — ничего.
if __name__ == "__main__":
    main()
