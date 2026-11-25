# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_chat_markup_visibility.py — ★★★ СТАТИЧЕСКАЯ ВИДИМОСТЬ УЗЛОВ ОКНА ЧАТА — В АССЕТЕ, А НЕ В КОДЕ.
#
# ЗАЧЕМ (план дублей `Reports/dedup_audit/00_PLAN.md` §3, разрешение владельца 15.09.2026
#   «Разрешаю, но только это окно»; законы `no-runtime-widget-fixes`, `no-ui-regeneration-without-permission`).
#   Подсистемы окна чата при КАЖДОМ построении окна гасили/показывали узлы ассета кодом:
#     • `UAsdaChatWindowSubsystem::EnsureWidgets` — таблица `ChatNodeState` (эталонная видимость после
#       `CChat_BuildTabUI`) + гашение родного плана вкладки в `BuildChatTabs`;
#     • `UAsdaChatSubsystem::EnsureWidgets` — показ заголовка «Система», рамки и стрелок журнала 2570.
#   Всё это — исходное состояние разметки, одинаковое при каждом построении, т.е. место ему — WBP.
#   Код после переноса видимость узлов ассета НЕ решает: свёрнутое в ассете остаётся свёрнутым; за кодом
#   осталась только политика хит-теста показанных узлов (Visible / HitTestInvisible), шрифт/цвет, живая
#   раскладка и то, что меняется по событиям (справка 2580, полоса кулдауна 2618, ввод, списки, меню).
#
# ЧТО ДЕЛАЕТ (идемпотентно). Для каждого узла из таблицы ставит видимость `want`, если она другая.
#   Значение для показанных узлов = ровно то, что ставил код при построении (ассет в редакторе = игра).
#   Сохраняет ТОЛЬКО изменённые пакеты окна чата, по одному (`save_loaded_asset`).
#   Нет узла в ассете — ошибка (исключение): это значит, что разметка окна поменялась и таблицу надо сверить.
#
# ПОЧЕМУ ШАГ ПОСТ-ИМПОРТА. `asda_jui_generic_umg.py` пересобирает окно из .JUI (видимость по атрибуту
#   `Visible`), и перенос бы стёрся. Шаг стоит в `asda_ui_post_import.STEPS` ПОСЛЕ `asda_ui_handmade`
#   (тот воссоздаёт RESIZE/SCROLL_UP/SCROLL_DOWN по снимку, где стрелки свёрнуты).
#
# Запуск (коммандлет, НЕ -ExecutePythonScript: EditorStartupMap = Login):
#   UnrealEditor-Cmd.exe AsdaReborn.uproject -run=pythonscript -script=<этот файл> -nullrhi -unattended -nosplash
#   Сухой прогон: set ASDA_CHATVIS_DRYRUN=1
#
# ★ УЗЕЛ КОНТРОЛА 2588 СПРАВКИ 2580 (15.09.2026, то же разрешение владельца — окно чата).
#   Эталон `CChat_BuildQueBtnUI` (chat.c:1238-1280) пишет loc 5400…5406 ПРЯМО в контролы 2581…2586 и 2588.
#   Узлы 2581…2586 (`CHAT_NEW_Que_btn_tooltip_TXT00…05`) создаёт шаг `asda_ui_missing_text_nodes` (класс
#   S1a: пустой ID и CaptionText, непустой EXS TextContent). У 2588 `TextContent = ""` (gamemain_jui.ini:93400),
#   под правило S1a он не попадает, и узла не было — код рисовал текст отдельным виджетом. Теперь узел
#   создаёт ЭТОТ шаг (`ensure_help_nodes`) ТЕМИ ЖЕ функциями, что S1a: имя — `asda_jui_parse.anchored_names`,
#   панель/origin — `container_of`, геометрия и цвет — `text_geometry`/`text_color` по Origin/Size/Align/EXS.
#   Существующий узел не трогается (идемпотентно).
import importlib.util
import os

import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

EAL = unreal.EditorAssetLibrary
V = unreal.SlateVisibility

WIN = "/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_NEW"
SYS = "/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_NEW_SYSTEM"

# Строки эталона — `RE/etalon/chat.c` (CChat_BuildTabUI @0xB9E490, chatInitColorPalette @0xB9E110,
#   set_o2AC/CChat_BuildServNoticeUI @0xB9EF20). Атрибуты `Visible` — `Data/Decoded/jui/gamemain_jui.ini`.
_HIDE = V.COLLAPSED
_HTI = V.HIT_TEST_INVISIBLE
_SHTI = V.SELF_HIT_TEST_INVISIBLE
_VIS = V.VISIBLE

# (ассет, узел, видимость, причина)
TABLE = [
    # ── окно 2500: эталон ГАСИТ при построении ─────────────────────────────────────────────────────
    (WIN, "CHAT_NEW_Bar_TEXT",         _HIDE, "2535 легаси: SetVisible(0) chat.c:850 (JUI Visible=1)"),
    (WIN, "CHAT_NEW_Bar_TEXT_26_HIT",  _HIDE, "клик-план подписи 2535 — гаснет вместе с ней"),
    (WIN, "CHAT_NEW_Edit_27_0",        _HIDE, "2533 легаси: SetVisible(0) chat.c:822 (JUI Visible=1)"),
    (WIN, "CHAT_NEW_Edit",             _HIDE, "2533 легаси: SetVisible(0) chat.c:822 (JUI Visible=1)"),
    (WIN, "CHAT_NEW_HAN_31_0",         _HIDE, "2511 IME-кор: SetVisible(0) chat.c:813 (JUI Visible=1)"),
    (WIN, "CHAT_NEW_scr_9_0",          _HIDE, "2536 скроллбар ленты: SetVisible(0) chat.c:892 (JUI Visible=1)"),
    (WIN, "CHAT_NEW_scr_9_1",          _HIDE, "2536 скроллбар ленты: SetVisible(0) chat.c:892"),
    (WIN, "CHAT_NEW_scr_9_2",          _HIDE, "2536 скроллбар ленты: SetVisible(0) chat.c:892"),
    (WIN, "CHAT_NEW_scr_9_3",          _HIDE, "2536 скроллбар ленты: SetVisible(0) chat.c:892"),
] + [
    (WIN, "lCHAT_NEW_ine0_5_%d" % i,   _HIDE, "план рамки 2530 (JUI Visible=0 у группы) — код гасил планы по префиксу")
    for i in range(8)
] + [
    # ── окно 2500: родной план вкладки 2501 — ряд вкладок рисует код (BuildChatTabs), узлы ассета скрыты ──
    (WIN, "CHAT_NEW_Radio_B_4_0",      _HIDE, "2501: ряд вкладок строит BuildChatTabs, родной план не показывается"),
    (WIN, "CHAT_NEW_Radio_B_OPT0",     _HIDE, "2501: то же (кнопка родного плана)"),
    (WIN, "CHAT_NEW_Radio_B_Text",     _HIDE, "2501: то же (подпись-плейсхолдер родного плана)"),
    # ── окно 2500: эталон ПОКАЗЫВАЕТ при построении (в JUI Visible=0 → импортёр свернул) ──────────────
    (WIN, "CHAT_NEW_ENG_30_0",         _HTI,  "2512 IME-англ: SetVisible(1) chat.c:818 (JUI Visible=0)"),
    (WIN, "CHAT_NEW_UDbtn",            _VIS,  "2605 свернуть/развернуть: SetVisible(1) chat.c:926 (JUI Visible=0)"),
    (WIN, "CHAT_NEW_Hbtn",             _HTI,  "2607 замок автопрокрутки: SetVisible(1) chat.c:932; индикатор, клик разбирает TickPointer"),
    (WIN, "CHAT_NEW_SSbtn_29_0",       _HTI,  "2608 комбобокс канала: SetVisible(1) chat.c:717 (JUI Visible=0)"),
    (WIN, "CHAT_NEW_SSbtn_29_1",       _HTI,  "2608 стрелка комбобокса: SetVisible(1) chat.c:717"),
    (WIN, "CHAT_NEW_Edit2_34_0",       _HTI,  "2609 плита поля ввода: SetVisible(1) chat.c:827 (JUI Visible=0)"),
    (WIN, "CHAT_NEW_Edit2",            _VIS,  "2609 поле ввода: SetVisible(1) chat.c:827; BuildInputWidgets показывал при построении"),
    (WIN, "CHAT_NEW_Edit2_Text",       _SHTI, "подпись поля 2609 (узел шага пост-импорта) — показывалась проходом кода"),
    (WIN, "CHAT_NEW_Que_btn_toolti_X", _VIS,  "2589 крестик справки: JUI Visible=1 внутри скрытой 2580 (импортёр свернул поддерево, S6)"),
] + [
    # ★ Контролы окна справки 2580 (узлы шага `asda_ui_missing_text_nodes` + TXT06 этого шага). Видимы по
    #   эталону: в JUI у всех `Visible = "1"` внутри скрытой 2580 (импортёр свернул поддерево), эталон их
    #   не гасит, а в `CChat_BuildQueBtnUI` пишет в них loc 5400…5406 (явный показ только 2585, chat.c:1265).
    #   С 15.09 код пишет текст В ЭТИ узлы; параллельные `CHAT_NEW_Que_tip_*` удалены — наложения больше нет.
    (WIN, "CHAT_NEW_Que_btn_tooltip_TXT%02d" % i, _SHTI,
     "контрол %d справки 2580: JUI Visible=1, текст loc %d пишет код (CChat_BuildQueBtnUI)" % (cid, loc))
    for i, (cid, loc) in enumerate([(2581, 5400), (2583, 5402), (2582, 5401), (2586, 5405),
                                    (2584, 5403), (2585, 5404), (2588, 5406)])
] + [
    # ── журнал 2570 ────────────────────────────────────────────────────────────────────────────────
    (SYS, "CHAT_NEW_SERV_SYSbottn_3_0",  _HTI,  "заголовок «Система» 2574 — ★ отступление, решение владельца 03.08 (JUI Visible=0)"),
    (SYS, "CHAT_NEW_SERV_SYSbottn_OPT0", _HTI,  "заголовок 2574 — кнопка плана, мышь не ловит"),
    (SYS, "CHAT_NEW_SERV_SYSbottn_Text", _SHTI, "подпись заголовка 2574 (текст loc 12144 ставит код)"),
] + [
    (SYS, "CHAT_NEW_SERV_line0_4_%d" % i, _HTI,
     "рамка 2575 — ★ отступление, решение владельца 03.08 (эталон SetVisible(0) chat.c:1007); тинт остаётся в коде")
    for i in range(8)
] + [
    (SYS, "SCROLL_UP",   _VIS, "ручной узел владельца (прокрутка журнала) — код показывал при каждом построении"),
    (SYS, "SCROLL_DOWN", _VIS, "ручной узел владельца (прокрутка журнала) — код показывал при каждом построении"),
]


def step_visibility_nodes():
    """★ 07.10.2026 (ревизия `Reports/owner_1007/label_band_fixes_REVIEW.md`, МЕЛК-1): узлы, видимость которых
    ВЕДЁТ этот шаг. Читает `asda_ui_sync_own_visibility` — их он не раскрывает и не гасит.
    Возврат — [(путь окна, узел, причина)] = строки `TABLE` (без загрузки ассетов)."""
    return [(row[0], row[1], row[3]) for row in TABLE]


def log(m):
    """Метка шага в логе."""
    unreal.log("[чат-видимость] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


# (IDNum контрола, ожидаемое имя узла) — контролы справки 2580, которым узел создаёт ЭТОТ шаг.
HELP_NODES = [(2588, "CHAT_NEW_Que_btn_tooltip_TXT06")]
# Имена узлов S1a тех же контролов (создаёт `asda_ui_missing_text_nodes`) — сторож, что правило имён,
#   расширенное на 2588, не сдвинуло нумерацию соседей (порядок документа JUI, не IDNum).
HELP_S1A_EXPECT = {2581: "CHAT_NEW_Que_btn_tooltip_TXT00", 2583: "CHAT_NEW_Que_btn_tooltip_TXT01",
                   2582: "CHAT_NEW_Que_btn_tooltip_TXT02", 2586: "CHAT_NEW_Que_btn_tooltip_TXT03",
                   2584: "CHAT_NEW_Que_btn_tooltip_TXT04", 2585: "CHAT_NEW_Que_btn_tooltip_TXT05"}
HELP_PANEL = "CHAT_NEW_Que_btn_tooltip_grp"


def _load_misstxt():
    """Модуль шага S1a — его функции панели/геометрии/цвета (модуль без побочных эффектов при импорте)."""
    spec = importlib.util.spec_from_file_location("M_chatvis", os.path.join(HERE, "asda_ui_missing_text_nodes.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _near(off, pos, size):
    """Слот (offsets POINT: L,T = позиция, R,B = размер) совпадает с расчётом до 0.6 px."""
    return all(abs(a - b) < 0.6 for a, b in zip((off.left, off.top, off.right, off.bottom),
                                                (pos.x, pos.y, size.x, size.y)))


# ★ ЧИСЛО СТРОК ТЕКСТА для вертикальной центровки (EXS TextAlign 0x4). Формула импортёра `text_geometry`
#   допускает ОДНУ строку 14 px: y = top + (h − 14)/2. У 2588 `TextContent` пуст, а код пишет loc 5406
#   «Чтобы ответить на шепот другого игрока, нажми 'R' и введи сообщение» — при кегле 8 и переносе 256 это
#   ДВЕ строки: замер -game `asda.ChatWin.DumpTip` 15.09 — текст 249×28 (отчёт chat_markup_to_asset.md,
#   «Справка 2580»). По формуле на 1 строку текст вставал на Y 275 (центр 289 против 282 у контрола
#   Y 267 H 30 — на 7 px ниже эталонной центровки); на 2 строки — Y 268, центр 282.
#
# ★★★ СТОРОЖ ДРЕЙФА (15.09.2026, хвост ревизии). Число строк — ЗАМЕР, а не расчёт: в UE Python нет замера
#   текста шрифтом (проба коммандлетом 15.09: в `unreal` из «measure» только GeometryScript-структура,
#   у SlateBlueprintLibrary/KismetTextLibrary в коммандлете классов нет, у AsdaUmgBuilder замера нет), а
#   повторять перенос Slate (разрыв строк ICU + метрики FreeType) по таблице глифов — выдумывание.
#   Поэтому рядом с числом хранится ОТПЕЧАТОК ВХОДОВ замера. Шаг при каждом прогоне собирает те же входы из
#   живых источников и при ЛЮБОМ расхождении ПАДАЕТ «перемерить» — старый Y молча не пишется:
#     text     — сырой SourceString строки loc из DT_GameText (рантайм: GetTextById → FAsdaText::Loc);
#     loc/bold — строка таблицы `Tips` в BuildQueTipTexts (AsdaChatWindowSubsystem.cpp) для этого контрола;
#     font     — FontObject|действующая гарнитура. Рантайм копирует ВЕСЬ `TextStyle.Font` Default-строки
#                DT_AsdaRichStyle (`TipFont = Row->TextStyle.Font`), затем ставит `Size = kFontPx` и при bold
#                `TypefaceFontName = "Bold"` — значит гарнитура = Bold при bold, иначе TypefaceFontName строки;
#     font_metrics — прочие поля того же `Font`, которые движок учитывает при раскладке (15.09, ревизия):
#                LetterSpacing и bForceMonospaced/MonospacedWidth (SlateTextShaper.cpp:436-487 — ширина глифов),
#                OutlineSettings.OutlineSize (SlateTextRun.cpp:36/42 — базовая линия и высота строки).
#                НЕ входят: Size (перезаписан kFontPx), SkewAmount/FontMaterial/цвет и прочее обводки — в
#                замере (FontMeasure/SlateTextShaper/SlateTextRun) не участвуют; CompositeFont/FontFallback — не
#                UPROPERTY, из таблицы не приходят;
#     font_faces — пакеты FontFace, которыми рисуется ЭТА строка: запись действующей гарнитуры DefaultTypeface
#                + все FontFace FallbackTypeface/SubTypefaces (живой `composite_font.export_text()` шрифта);
#                у 2588 (не bold) это AsdaUIFont, `AsdaUIFontBold` сюда не попадает — нет ложной тревоги;
#     font_md5 — md5 пакета шрифта + пакетов font_faces (замена гарнитуры/метрик при том же пути);
#     font_px  — kFontPx из AsdaChatWindowSubsystem.h (кегль, который ставит код);
#     wrap_w   — ширина слота из `text_geometry` (= SetWrapTextAt в коде).
#   Перемер: -game, открыть справку, `asda.ChatWin.DumpTip` → высота текста узла / 14 = lines; затем
#   обновить lines, «замер» и отпечаток (текущие значения печатает сообщение о падении).
#   ★ Падение сторожа блокирует ТОЛЬКО запись слота/создание узла этого контрола: таблица видимости TABLE
#   применяется независимо, а шаг в конце всё равно завершается ошибкой «ПЕРЕМЕРИТЬ» (HelpMeasureDrift).
HELP_TEXT_MEASURE = {
    2588: {
        "lines": 2,
        "замер": "-game asda.ChatWin.DumpTip 15.09.2026: CHAT_NEW_Que_btn_tooltip_TXT06 текст 249x28, перенос 256 "
                 "(Reports/dedup_audit/chat_help_game_D1.log)",
        "fp": {
            "loc": 5406,
            "bold": False,
            "text": "Чтобы ответить на шепот другого игрока, нажми 'R' и введи сообщение",
            "font": "/Game/AsdaAssets/Fonts/AsdaUIFontRT.AsdaUIFontRT|Regular",
            "font_metrics": "LetterSpacing=0|OutlineSize=0|bForceMonospaced=False|MonospacedWidth=1.000000",
            "font_faces": "/Game/AsdaAssets/Fonts/AsdaUIFont",
            # AsdaUIFontRT + AsdaUIFont (15.09.2026; файлы шрифта от 20.07 — те же, что при замере DumpTip.
            #   Прежний 35b34b45… включал и AsdaUIFontBold; оба значения пересчитаны вне редактора тем же алгоритмом).
            "font_md5": "9e22a796a2bea77589b4e0b508552f01",
            "font_px": 8.0,
            "wrap_w": 256.0,
        },
    },
}
HELP_TEXT_LINE_PX = 14   # GEN_TEXT_LINE_PX импортёра / шага S1a (высота строки кегля 8, замер DumpTip)

PROJ = os.path.normpath(os.path.join(HERE, ".."))
CHATWIN_CPP = os.path.join(PROJ, "Source", "AsdaReborn", "Private", "UI", "AsdaChatWindowSubsystem.cpp")
CHATWIN_H = os.path.join(PROJ, "Source", "AsdaReborn", "Public", "UI", "AsdaChatWindowSubsystem.h")
GAMETEXT_DT = "/Game/AsdaAssets/Text/DT_GameText"
RICHSTYLE_DT = "/Game/AsdaAssets/UI/DT_AsdaRichStyle"


def read_loc_raw(loc):
    """Сырой SourceString строки loc из DT_GameText (тот же ассет, что читает рантайм)."""
    lib = unreal.DataTableFunctionLibrary
    dt = EAL.load_asset(GAMETEXT_DT)
    if not dt:
        raise RuntimeError("нет %s" % GAMETEXT_DT)
    rows = [str(n) for n in lib.get_data_table_row_names(dt)]
    if str(loc) not in rows:
        raise RuntimeError("в %s нет строки %d" % (GAMETEXT_DT, loc))
    return lib.get_data_table_column_as_string(dt, "SourceString")[rows.index(str(loc))]


class HelpMeasureDrift(RuntimeError):
    """Сторож HELP_TEXT_MEASURE: входы замера числа строк изменились — «ПЕРЕМЕРИТЬ»."""


def _ue_fields(s):
    """Разбор текста экспорта UE-структуры «(K=V,K=(…),…)» верхнего уровня → {K: V как строка}."""
    s = s.strip()
    if not (s.startswith("(") and s.endswith(")")):
        raise RuntimeError("не структура UE: %r" % s[:120])
    return dict(p.partition("=")[::2] for p in _ue_items(s) if p)


def _ue_items(s):
    """Элементы верхнего уровня «(a,b,(c,d))» → ['a', 'b', '(c,d)'] с учётом скобок и кавычек; «» → []."""
    s = s.strip()
    if not s:
        return []
    body, out, depth, quote, start = s[1:-1], [], 0, False, 0
    for i, ch in enumerate(body):
        if ch == '"':
            quote = not quote
        elif not quote:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                out.append(body[start:i])
                start = i + 1
    out.append(body[start:])
    return [x for x in out if x]


def _obj_path(v):
    """Путь объекта из «"/Script/Engine.Font'/Game/…/X.X'"»; None/пусто — None."""
    import re
    m = re.search(r"'([^']+)'", v or "")
    return m.group(1) if m else None


def read_style_textstyle():
    """Текст экспорта `TextStyle` Default-строки DT_AsdaRichStyle (источник TipFont кода; тест подменяет копией)."""
    lib = unreal.DataTableFunctionLibrary
    dt = EAL.load_asset(RICHSTYLE_DT)
    if not dt:
        raise RuntimeError("нет %s" % RICHSTYLE_DT)
    rows = [str(n) for n in lib.get_data_table_row_names(dt)]
    if "Default" not in rows:
        raise RuntimeError("в %s нет строки Default" % RICHSTYLE_DT)
    return lib.get_data_table_column_as_string(dt, "TextStyle")[rows.index("Default")]


def read_composite_font(font_path):
    """Текст экспорта `CompositeFont` живого UFont (гарнитуры → FontFace)."""
    f = EAL.load_asset(font_path)
    if not f:
        raise RuntimeError("нет шрифта %s" % font_path)
    return f.get_editor_property("composite_font").export_text()


def style_font_inputs(bold):
    """(font, font_metrics, font_faces[список пакетов]) — то, что рантайм берёт из `TextStyle.Font` для строки.

    Рантайм (`BuildQueTipTexts`): `F = TipFont (= TextStyle.Font); F.Size = kFontPx; if bold: F.TypefaceFontName = "Bold"`.
    """
    font = _ue_fields(_ue_fields(read_style_textstyle()).get("Font", ""))
    path = _obj_path(font.get("FontObject"))
    if not path:
        raise RuntimeError("Default-строка %s: FontObject пуст (%r)" % (RICHSTYLE_DT, font.get("FontObject")))
    face = "Bold" if bold else font.get("TypefaceFontName", "").strip('"')
    outline = _ue_fields(font.get("OutlineSettings", "()"))
    metrics = "|".join("%s=%s" % (k, v) for k, v in (
        ("LetterSpacing", font.get("LetterSpacing")), ("OutlineSize", outline.get("OutlineSize")),
        ("bForceMonospaced", font.get("bForceMonospaced")), ("MonospacedWidth", font.get("MonospacedWidth"))))
    cf = _ue_fields(read_composite_font(path))
    entries = [_ue_fields(e) for e in _ue_items(_ue_fields(cf.get("DefaultTypeface", "()")).get("Fonts", ""))]
    if face in ("", "None"):
        face_entry = entries[0] if entries else None   # движок: «None will use the first entry» (SlateFontInfo.h)
    else:
        face_entry = next((e for e in entries if e.get("Name", "").strip('"') == face), None)
    if face_entry is None:
        raise RuntimeError("шрифт %s: нет гарнитуры %r среди %r"
                           % (path, face, [e.get("Name") for e in entries]))
    face = face_entry.get("Name", "").strip('"')
    used = [face_entry]
    fb = _ue_fields(_ue_fields(cf.get("FallbackTypeface", "()")).get("Typeface", "()"))
    used += [_ue_fields(e) for e in _ue_items(fb.get("Fonts", ""))]
    for sub in _ue_items(cf.get("SubTypefaces", "")):
        tf = _ue_fields(_ue_fields(sub).get("Typeface", "()"))
        used += [_ue_fields(e) for e in _ue_items(tf.get("Fonts", ""))]
    faces = []
    for e in used:
        p = _obj_path(_ue_fields(e.get("Font", "()")).get("FontFaceAsset"))
        if not p:
            raise RuntimeError("шрифт %s: у записи %r нет FontFaceAsset" % (path, e.get("Name")))
        faces.append(p.split(".")[0])
    return "%s|%s" % (path, face), metrics, sorted(set(faces))


def font_md5(font_path, faces):
    """md5 файлов пакета шрифта и пакетов FontFace, которыми рисуется строка (faces), по порядку имён пакетов."""
    import hashlib
    pkg = font_path.split(".")[0]
    content = os.path.normpath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()))
    h = hashlib.md5()
    for p in sorted({pkg} | set(faces)):
        files = [os.path.join(content, p[len("/Game/"):].replace("/", os.sep) + ext) for ext in (".uasset", ".uexp", ".ubulk")]
        files = [f for f in files if os.path.isfile(f)]
        if not files:
            raise RuntimeError("пакет шрифта %s: нет файлов в %s" % (p, content))
        for f in files:
            h.update(p.encode("utf-8") + os.path.basename(f).encode("utf-8"))
            with open(f, "rb") as fh:
                h.update(hashlib.md5(fh.read()).digest())
    return h.hexdigest()


def _cpp_tip_row(cid):
    """(loc, bold) строки таблицы `Tips` в BuildQueTipTexts для контрола cid — из исходника кода."""
    import re
    src = open(CHATWIN_CPP, encoding="utf-8-sig").read()
    m = re.findall(r"\{\s*%d\s*,\s*(\d+)\s*,\s*TEXT\(\"([^\"]+)\"\)\s*,\s*(true|false)\s*," % cid, src)
    if len(m) != 1:
        raise RuntimeError("строка контрола %d в таблице Tips %s: найдено %d, ожидалась 1" % (cid, CHATWIN_CPP, len(m)))
    return int(m[0][0]), m[0][2] == "true"


def _cpp_font_px():
    """kFontPx подсистемы окна чата (кегль, который ставит код справке)."""
    import re
    m = re.findall(r"static\s+constexpr\s+float\s+kFontPx\s*=\s*([0-9.]+)f", open(CHATWIN_H, encoding="utf-8-sig").read())
    if len(m) != 1:
        raise RuntimeError("kFontPx в %s: найдено %d, ожидалось 1" % (CHATWIN_H, len(m)))
    return float(m[0])


def help_measure_inputs(cid, size):
    """Текущий отпечаток входов замера числа строк контрола cid (из живых источников)."""
    loc, bold = _cpp_tip_row(cid)
    font, metrics, faces = style_font_inputs(bold)
    return {"loc": loc, "bold": bold, "text": read_loc_raw(loc), "font": font, "font_metrics": metrics,
            "font_faces": ",".join(faces), "font_md5": font_md5(font.split("|")[0], faces),
            "font_px": _cpp_font_px(), "wrap_w": round(float(size.x), 1)}


def check_help_measure(cid, cur):
    """Сторож: отпечаток входов обязан совпасть с сохранённым рядом с числом строк, иначе — падение «перемерить»."""
    ref = HELP_TEXT_MEASURE[cid]["fp"]
    diff = sorted(k for k in set(ref) | set(cur) if ref.get(k) != cur.get(k))
    if diff:
        raise HelpMeasureDrift(
            "ПЕРЕМЕРИТЬ число строк контрола %d: входы замера (%s) изменились — %s. Сохранённый Y не пишу. "
            "Замер: -game, открыть справку, asda.ChatWin.DumpTip; затем обновить HELP_TEXT_MEASURE[%d] "
            "(lines, «замер», fp). Текущий отпечаток: %r"
            % (cid, HELP_TEXT_MEASURE[cid]["замер"],
               "; ".join("%s: было %r, стало %r" % (k, ref.get(k), cur.get(k)) for k in diff), cid, cur))
    log("контрол %d: отпечаток входов замера совпал (%s) — строк %d"
        % (cid, ", ".join(sorted(ref)), HELP_TEXT_MEASURE[cid]["lines"]))


def _vcenter_lines(node, pos, size, cid, J=None):
    """Позиция из `text_geometry`, пересчитанная на замеренное число строк (маска 0x4, HELP_TEXT_MEASURE + сторож).
    `J` — модуль `asda_jui_parse` вызывающего (None — взять из модуля шага S1a)."""
    if cid not in HELP_TEXT_MEASURE:
        return pos
    check_help_measure(cid, help_measure_inputs(cid, size))   # до любой записи слота
    lines = HELP_TEXT_MEASURE[cid]["lines"]
    # ★ 07.10.2026 (ревизия `poststeps_align_fix_REVIEW.md`, МЕЛК-2): маска — ОБЩАЯ ТОЧКА импортёра `J.label_mask`.
    m = (J if J is not None else _load_misstxt().J).label_mask(node)
    if lines == 1 or not (m & 0x4):
        return pos
    h = node.abs[3]
    return unreal.Vector2D(pos.x, pos.y - max((h - HELP_TEXT_LINE_PX) / 2.0, 0.0)
                           + max((h - lines * HELP_TEXT_LINE_PX) / 2.0, 0.0))


def ensure_help_nodes(dry):
    """Создать недостающие узлы HELP_NODES в WBP_CHAT_NEW. Возвращает список имён созданных (в сухом — планируемых)."""
    M = _load_misstxt()
    J = M.J
    roots = J.parse_jui(M.ini_of("gamemain"))
    root = next((r for r in roots if r.idnum == 2500), None)
    if root is None:
        raise RuntimeError("в gamemain нет окна 2500")
    J.compute_anchors(root)
    ids = {i for i, _n in HELP_NODES}

    def pick(n):
        # Правило S1a (`asda_jui_parse.missing_text_nodes`) + наши контролы с пустым TextContent.
        cap = (n.caption or "").strip()
        txt = (n.txt_content or "").strip() if n.txt_content else ""
        return "TXT" if ((not n.idc) and (not cap) and (txt or n.idnum in ids)) else None
    names = {n.idnum: nm for n, _p, nm, _k in J.anchored_names(root, pick)}
    for cid, nm in list(HELP_S1A_EXPECT.items()) + HELP_NODES:
        if names.get(cid) != nm:
            raise RuntimeError("имя узла контрола %d: по правилу %r, ожидалось %r" % (cid, names.get(cid), nm))

    bp = EAL.load_asset(WIN)
    canvas = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    order, parent_of, _hidden = M.index_nodes(root)
    wid, par = M.widget_index(canvas)
    made = []
    for cid, nm in HELP_NODES:
        node = J.find_by_idnum([root], cid)
        panel, ox, oy = M.container_of(node, parent_of, order, wid, par, canvas)
        if panel is None or panel.get_name() != HELP_PANEL:
            raise RuntimeError("контрол %d: панель %s, ожидалась %s"
                               % (cid, panel.get_name() if panel else None, HELP_PANEL))
        # Второй метод: тот же origin панели обязан дать слоты уже существующих узлов S1a (сверка по числам).
        for sid, snm in HELP_S1A_EXPECT.items():
            spos, ssize, _h, _w = M.text_geometry(J.find_by_idnum([root], sid), ox, oy, root)   # ★ 07.10: + рамка окна (`J.label_slot`)
            off = M.slot_offsets(wid[snm]) if snm in wid else None
            if off is None or not _near(off, spos, ssize):
                raise RuntimeError("сверка origin панели: %s слот %s против расчёта (%.1f,%.1f %.1fx%.1f)"
                                   % (snm, off and (off.left, off.top, off.right, off.bottom),
                                      spos.x, spos.y, ssize.x, ssize.y))
        pos, size, halign, wrap = M.text_geometry(node, ox, oy, root)   # ★ 07.10: + рамка окна (`J.label_slot`)
        pos = _vcenter_lines(node, pos, size, cid, J)
        txt = M.fix_text(node.txt_content or "")
        if nm in wid:
            # Узел уже есть (его создал ЭТОТ шаг): сверяем слот с расчётом; разошёлся — правим только слот.
            off = M.slot_offsets(wid[nm])
            if off is not None and _near(off, pos, size):
                log("узел %s (контрол %d) уже есть, слот совпадает — не трогаю" % (nm, cid))
                continue
            log("узел %s (контрол %d): слот %s -> (%.1f,%.1f %.1fx%.1f)%s"
                % (nm, cid, off and (off.left, off.top, off.right, off.bottom), pos.x, pos.y, size.x, size.y,
                   " [СУХОЙ]" if dry else ""))
            made.append(nm)
            if not dry:
                wid[nm].slot.set_offsets(unreal.Margin(pos.x, pos.y, size.x, size.y))
            continue
        log("%s контрол %d -> %s в %s: (%.1f,%.1f %.1fx%.1f) выравн=%d перенос=%d цвет=%s текст=%r%s"
            % ("план:" if dry else "СОЗДАЮ", cid, nm, HELP_PANEL, pos.x, pos.y, size.x, size.y, halign,
               int(wrap), node.txt_color_hex, txt, " [СУХОЙ]" if dry else ""))
        made.append(nm)
        if dry:
            continue
        tb = unreal.AsdaUmgBuilder.add_text(bp, panel, nm, pos, size, txt, M.FONT_PX, M.text_color(node),
                                            halign, wrap, True)
        if not tb:
            raise RuntimeError("AddText вернул None для %s" % nm)
        tb.set_editor_property("visibility", _SHTI)   # подпись мышь не ловит (как у узлов S1a)
    if made and not dry:
        unreal.AsdaUmgBuilder.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % WIN)
    return made


def main():
    dry = bool(os.environ.get("ASDA_CHATVIS_DRYRUN"))
    # ★ 15.09 (ревизия): падение сторожа HELP_TEXT_MEASURE не должно блокировать таблицу видимости обоих окон.
    #   `ensure_help_nodes` проверяет сторож ДО записи слота/создания узла, поэтому при HelpMeasureDrift ничего
    #   из узлов справки не записано; таблица ниже применяется как обычно, а ошибка «ПЕРЕМЕРИТЬ» поднимается в
    #   конце. Прочие ошибки `ensure_help_nodes` (имена, панель, origin) по-прежнему останавливают шаг сразу.
    drift = None
    try:
        planned = ensure_help_nodes(dry)
    except HelpMeasureDrift as e:
        drift, planned = e, []
        unreal.log_error("[чат-видимость] СТОРОЖ: %s — запись узлов справки пропущена, таблица видимости применяется" % e)
    help_names = {nm for _cid, nm in HELP_NODES}
    by_asset = {}
    for path, node, want, why in TABLE:
        by_asset.setdefault(path, []).append((node, want, why))
    missing, total_fix = [], 0
    for path, rows in by_asset.items():
        if not EAL.does_asset_exist(path):
            raise RuntimeError("нет ассета %s" % path)
        bp = EAL.load_asset(path)
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        if not root:
            raise RuntimeError("%s: нет корневого канваса" % path)
        nodes = {w.get_name(): w for w in walk(root)}
        fixed = 0
        for node, want, why in rows:
            w = nodes.get(node)
            if w is None:
                if dry and node in planned:
                    log("%s :: %s  узла ещё нет — будет создан ensure_help_nodes [СУХОЙ]" % (path.rsplit("/", 1)[-1], node))
                    continue
                if drift is not None and node in help_names:
                    # Узел не создан потому, что сработал сторож (не «разметка поменялась»): причина — в drift.
                    log("%s :: %s  узла нет — НЕ создан: сработал сторож HELP_TEXT_MEASURE" % (path.rsplit("/", 1)[-1], node))
                    continue
                missing.append("%s::%s" % (path, node))
                continue
            cur = w.get_editor_property("visibility")
            if cur == want:
                continue
            fixed += 1
            log("%s :: %s  %s -> %s  (%s)%s" % (path.rsplit("/", 1)[-1], node, cur, want, why,
                                                 " [СУХОЙ]" if dry else ""))
            if not dry:
                w.set_editor_property("visibility", want)
        if fixed and not dry:
            unreal.AsdaUmgBuilder.compile_and_save(bp)
            # ⚠ `compile_and_save` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
            if not EAL.save_loaded_asset(bp, False):
                raise RuntimeError("не сохранён %s" % path)
        log("%s: узлов в таблице %d, изменено %d%s" % (path, len(rows), fixed, " (СУХОЙ ПРОГОН)" if dry else ""))
        total_fix += fixed
    log("ИТОГО изменено %d" % total_fix)
    if drift is not None:
        raise HelpMeasureDrift("%s Таблица видимости применена (изменено %d)%s."
                               % (drift, total_fix, "; ещё нет узлов в ассете: %s" % ", ".join(missing) if missing else ""))
    if missing:
        raise RuntimeError("нет узлов в ассете: %s" % ", ".join(missing))
    unreal.log("ЗАВЕРШЕНО: видимость окна чата")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
