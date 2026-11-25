# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_text_style926.py — ★★★ ОБВОДКА ТЕКСТА ПО ДАННЫМ ОРИГИНАЛА (поле 926), а не по скину.
#
# ПРОБЛЕМА. Импортёр (`asda_jui_generic_umg.py`) ставит `OutlineSize = 1` каждому тексту, чей СКИН
#   имеет `TextOutLine = "1"` — это **7059 контролов из 7862**. Прочтение НЕВЕРНОЕ:
#     • поле **925** (`TextOutLine`, `cfg_WriteTextOutLine@0x12E4D90`) в `cControl_RenderLabel`
#       (`ui_core.c:110741-110746`) выбирает ФЛАГИ ВЫВОДА `0x2010` (перенос по словам) против
#       `0x100` (не обрезать) — к обводке отношения не имеет;
#     • обводку даёт поле **926** = `styleFlags`: `0x800000` обводка (`font_text.c:6952`),
#       `0x200000` псевдо-жирный (`:6832`), `0x400000` тень (`:6887`).
#   По оригиналу обводка положена **747 контролам**, а не 7059 ⇒ лишняя обводка на ~6300 контролах.
#   Это и есть источник трёх кругов жалоб владельца (магазин снаряжения, склад ×2, окно 900):
#   у ТЁМНОГО текста контур 1 px съедает букву.
#
# ЧТО ДЕЛАЕТ ЭТОТ ШАГ (идемпотентно):
#   идёт по всем `WBP_*` интерфейса и приводит `OutlineSize` каждого `TextBlock` к данным оригинала:
#     • есть в таблице с `outline=1` → 1;
#     • есть в таблице с `outline=0` → 0;
#     • В ТАБЛИЦЕ НЕТ → 0 (оригинал этому контролу стиль не ставит).
#   ★ Строки с `conditional=1` (90 шт.) НЕ ТРОГАЕМ вовсе: там оригинал включает стиль ВЕТКОЙ КОДА
#     (иконки бафов, каналы, облачки, строки наград) — это динамика, ею управляет рантайм.
#
# ★ ЖИРНЫЙ (`0x200000`) и ТЕНЬ (`0x400000`) включены 2026-08-27 по решению владельца.
#   Ветки рендера взаимоисключающие (`if / else if / else if`), поле ПЕРЕЗАПИСЫВАЕТСЯ ⇒
#   у контрола ровно ОДИН стиль, комбинировать нельзя.
#   ★ ПОРЯДОК ВЕТОК ЭТАЛОНА (поправка ревизии 24.09.2026): ЖИРНЫЙ `0x200000` (`font_text.c:6832`)
#     проверяется ПЕРВЫМ, ТЕНЬ `0x400000` (`:6887`) второй, ОБВОДКА `0x800000` (`:6952`) третьей.
#     Если у контрола вдруг окажутся оба бита — оригинал покажет ЖИРНЫЙ. В нашей таблице пересечений
#     нет (строк `bold=1 и shadow=1` — 0), но порядок в коде ниже держим эталонный.
#
#   ТЕНЬ. Эталон (`font_text.c:6887`) рисует глиф ВТОРОЙ раз со сдвигом `penX+1 / penY+1` цветом
#     `shadowColor` (`+0x256A44`). В UMG это `ShadowOffset (1,1)` + `ShadowColorAndOpacity`.
#     ⚠ ДОПУЩЕНИЕ: писателя `shadowColor` в доступном срезе декомпила НЕТ (искали по имени и по
#     оффсету), поэтому берём ЧЁРНЫЙ непрозрачный — тот же цвет, что у обводки (поле общее для
#     тени и обводки, `structs_ru.h:574`). Сменить: `ASDA_STYLE926_SHADOW_RGBA=r,g,b,a`.
#
#   ЖИРНЫЙ (★★★ переделан 2026-09-24, уточнён по ревизии в тот же день). Эталон (`:6832-6886`)
#     рисует глиф ДВАЖДЫ: `penX += 1.0` → квад → `penX -= 1.0` → квад, то есть второй квад встаёт
#     В ИСХОДНУЮ ТОЧКУ ⇒ позиции ровно **{x+1, x}**, а НЕ `x-1 / x+1`. Продвижение пера не меняется
#     ⇒ строка у оригинала ВСЕГДА имеет ширину Regular. Это ПСЕВДО-ЖИРНЫЙ, а НЕ жирная гарнитура:
#     атлас оригинала собран
#     `CreateFontA(..., вес 400, "Arial", cHeight 14)` — доказано побайтовым подбором по живому
#     `JUIData/font/FONT0.BIN` (Arial/14/400 — 222 глифа из 223, Σ|Δ| = 1; Arial Bold — 131 из 223),
#     `boldFlag` инициализируется нулём (ui_core.c:106690).
#     ⇒ Начертание `Bold` НЕ СТАВИТСЯ НИКОМУ: этот шаг приводит гарнитуру ВСЕХ текстов к `Regular`
#     (наш Bold был на 6.3 % шире оригинала и резал хвосты у 34 подписей).
#     Псевдо-жирный делаем ВТОРЫМ ПРОХОДОМ ПО ГОРИЗОНТАЛИ: `ShadowOffset = (1, 0)`,
#     `ShadowColorAndOpacity = цвет заливки самого контрола`. Тень Slate рисуется под основным
#     проходом со сдвигом ⇒ глиф оказывается ровно в `x+1` и `x` — **ТОЧНО ТО ЖЕ, ЧТО У ЭТАЛОНА**
#     (тем же цветом, без изменения продвижения). Это 1:1, а не приближение; долга «свой UTextBlock
#     с отрисовкой ±1» НЕ СУЩЕСТВУЕТ — он был заведён по ошибке чтения и снят поправкой в реестре.
#     ДОКАЗАТЕЛЬСТВО ОТ ПРОТИВНОГО: в ветке ОБВОДКИ (`0x800000`, `font_text.c:6952+`), где симметрия
#       действительно нужна, автор СОХРАНЯЕТ `savedPenX/savedPenY` и рисует `penX-1`, `savedPenX+1`,
#       `savedPenY-1`, `savedPenY+1` и только потом основной квад — КРЕСТ из 5 квадов цветом
#       `shadowColor`. То есть писать симметрично он умел и в ветке жирного сознательно не стал.
#     ⇒ ВАРИАНТ `OutlineSize = 1` для ЖИРНОГО ОТВЕРГНУТ ДВАЖДЫ. Во-первых по смыслу: обводка — это
#       ОТДЕЛЬНЫЙ стиль оригинала `0x800000` (крест ±1 по X и Y цветом `shadowColor`), а вовсе не
#       жирный, и подменять один стиль другим нельзя. Во-вторых по виду (замер): обводка FreeType
#       растит глиф во ВСЕ стороны, включая вертикаль, и на кегле 8 (10.67 px) заливает внутренние
#       просветы букв — они превращаются в кляксы.
#     ⚠ Цвет второго прохода берётся из заливки НА МОМЕНТ ПРОГОНА. Если рантайм меняет цвет текста
#       (`SetColorAndOpacity`), псевдо-жирный останется прежнего цвета — у 75 контролов таблицы это
#       заголовки с фиксированным цветом, но правило записано здесь, чтобы не искать заново.
#

# ★★★ ОКНА СЦЕНЫ ЛОГИНА — РЕШЕНИЕ ВЛАДЕЛЬЦА ОТ 24.09.2026: НЕ ПРАВИТЬ.
#   Сторож `asda_login_guard` пропускает 26 окон `/Game/AsdaAssets/UI/Login/` (273 текстовых узла),
#   и они ОСТАЮТСЯ на `Bold`-гарнитуре. Это НЕ долг и НЕ «ждёт разрешения»: владелец постановил
#   оставить как есть, разрешения на `ASDA_ALLOW_LOGIN_WRITE` не будет, сторож не отключается,
#   расхождение с оригиналом по этим окнам принято сознательно.
#   ⇒ Строка `[LOGIN-GUARD] … окон сцены логина пропущено: 26` в конце прогона — ОЖИДАЕМА и верна,
#   это не сбой шага. Не предлагать перевод, не просить разрешения. Поимённый список окон и
#   последствия — `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`, поправка «… ОКНА ЛОГИНА, РЕШЕНИЕ ВЛАДЕЛЬЦА».
#
# Источник данных: `Tools/data/text_style926.tsv` (1386 строк) — реверс всех 252 вызовов двух
#   сеттеров: `CClass_SetField926@0x97D790` (226) и `set_o14B@0xB88950` через тонк
#   `sub_937235@0x937235` (26, поле 331 у листбокса; найден только сканом по HEX `0x800000`).
#   Отчёт: `Reports/text_style926_table.md`.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_text_style926.py
#   Сухой прогон (ничего не сохраняет, только отчёт):  set ASDA_STYLE926_DRYRUN=1
#   Только указанные окна (★ 09.10.2026):              set ASDA_STYLE926_ONLY=WBP_FISH_BOOK_WIN
import io
import os
import re
import traceback
import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402
# ★ 26.10.2026: перечень узлов-чисел количества (их обводку ведёт отдельный шаг, см. `main`)
import asda_ui_item_count_outline as _ITEMCOUNT   # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TSV = os.path.join(HERE, "data", "text_style926.tsv")
UI_ROOT = "/Game/AsdaAssets/UI"
EAL = unreal.EditorAssetLibrary

# ★★★ ОКНА, КОТОРЫХ НЕТ В `.JUI` И У КОТОРЫХ СВОЙ ИСТОЧНИК СТИЛЯ.
#   Плашки имён (`NAMEPLATE_*`) в данных `.JUI` ОТСУТСТВУЮТ вовсе — их собирает наш генератор
#   `Tools/asda_build_nameplates.py`, и он же ставит им обводку (`apply_text_outline(bp, 1, чёрный)`).
#   Основание — эталон `TextFont_BuildGlyphQuad@0x12C5B80`: у имени/гильдии/титула над головой
#   обводка крестом ±1 px цветом `0xFF000000`.
#   ⚠ ПОЧЕМУ ЭТО ЗДЕСЬ: таблица поля 926 их не покрывает (в `.JUI` их нет), поэтому первый прогон
#   этого шага СНЯЛ с них обводку — имена мобов, NPC и игроков стали без контура (жалоба владельца
#   2026-08-27). Обводка у плашек уже терялась однажды (2026-07-29), поэтому фиксируем правилом,
#   а не разовой правкой: этим окнам обводка ставится ВСЕМ текстам.
ALWAYS_OUTLINED_WINDOWS = (
    "NAMEPLATE_MON", "NAMEPLATE_MAN", "NAMEPLATE_MAN1",
)

# ★★★ КЛОН НАСЛЕДУЕТ СТИЛЬ ШАБЛОНА (09.10.2026, ревизия мультиязычности СРЕДН-1,
#   `Reports/owner_1009/multilang_REVIEW.md` п. 6). Клоны, которые КОД эталона делает из шаблона
#   (`CControl_InstantiateChild`), в `.JUI` отсутствуют; их кладёт в ассет отдельный шаг пост-импорта под СВОИМИ
#   именами. Таблица поля 926 знает только idc шаблона, поэтому раньше клон попадал в ветку «в таблице нет → 0»,
#   и любой повторный прогон цепочки снимал обводку у 90 текстов книги рыбака (`ОБВОДКА: +0 / −90`).
#   Эталон ставит стиль КАЖДОМУ клону: `ui_core.c:100104` (74021 x30), `:100124` (74025 x30), `:100133` (74026 x30).
#   Строка: (окно в виде `san`, regex имени узла-клона, idc шаблона в таблице). Узел, совпавший с regex, ищется в
#   таблице под idc шаблона — и в исправленном (`want`), и в буквальном (`legacy`) сопоставлении.
#   Аудит прочих шагов-клонировщиков 09.10.2026: `asda_ui_fish_ox_clones` кладёт Image (текста нет); клоны плашек
#   `TARGETMARK_*` создаёт рантайм (в ассете их нет) — другим окнам строка здесь не нужна.
#   Новый шаг, который клонирует TextBlock с шаблоном из `text_style926.tsv`, ОБЯЗАН дописать сюда строку.
CLONE_TEMPLATES = (
    ("FISH_BOOK_WIN", re.compile(r"^FISH_BOOK_CELL\d\d_NAME$"), "FISH_BOOK_BAR00_TEXT00"),   # клон 74021 — имя рыбы
    ("FISH_BOOK_WIN", re.compile(r"^FISH_BOOK_CELL\d\d_NO$"), "FISH_BOOK_no00"),             # клон 74025 — номер «%03d»
    ("FISH_BOOK_WIN", re.compile(r"^FISH_BOOK_CELL\d\d_CNT$"), "FISH_BOOK_NO_K00"),          # клон 74026 — счёт «%d/%d»
    # ★ 29.10.2026 (инвентарь ×2, шаг `asda_ui_inventory_x2`): числа стопки ячеек сумки 30…59 — клоны `INVEN_SET0`
    #   (сетка 12×5, 60 ячеек); эталон ставит стиль всем ячейкам сетки одним циклом (item_inventory.c:35253).
    ("INVEN_WIN", re.compile(r"^INVEN_SET(3\d|4\d|5\d)$"), "INVEN_SET0"),
)


def clone_template(win, name):
    """idc шаблона, если узел `name` окна `win` (имя пакета без `WBP_`, вид `san`) — клон из `CLONE_TEMPLATES`; иначе None."""
    for w, rx, tpl in CLONE_TEMPLATES:
        if w == win and rx.match(name):
            return tpl
    return None


def log(m):
    unreal.log("[обводка926] %s" % m)


def _login_blocked(path):
    """★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 14): True — окно `path` принадлежит сцене логина (asda_login_guard: UI/Login, кроме
    LOGIN_SHARED_EXCEPTIONS), а ASDA_ALLOW_LOGIN_WRITE != 1: окно пропустить, НЕ загружая. Без строки на каждое окно (quiet):
    итог [LOGIN-GUARD] печатает вызывающий. Ленивый импорт — при импорте модуля ничего не делает."""
    import sys as _sys_lg
    _here = os.path.dirname(os.path.abspath(__file__))   # Tools — для asda_login_guard
    if _here not in _sys_lg.path:
        _sys_lg.path.insert(0, _here)
    from asda_login_guard import package_blocked_for   # ★ дедуп №2 Т18: имя инструмента — из __file__
    return package_blocked_for(__file__, path, quiet=True)


def load_table():
    """(window_idc, idc) → (outline, bold, shadow); отдельно условные пары, которые не трогаем.

    Возвращает ТРИ штуки: `want` — ключи в виде имён АССЕТА (через `san`, исправленное
    сопоставление); `legacy` — ключи БУКВАЛЬНО из разметки (как было до 24.09.2026); `cond` —
    условные пары. Две таблицы нужны гейту `ASDA_STYLE926_SANMATCH_ALL` (см. `main`)."""
    want, legacy, cond = {}, {}, set()
    with io.open(TSV, encoding="utf-8") as f:
        head = f.readline().rstrip("\n").split("\t")
        col = {n: i for i, n in enumerate(head)}
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < len(head):
                continue
            idc = p[col["idc"]].strip()
            win = p[col["window_idc"]].strip()
            if not idc:
                continue
            key = (san(win), san(idc))   # ★ ключи таблицы — В ВИДЕ ИМЁН АССЕТА (см. `san`)
            if p[col["conditional"]].strip() == "1":
                cond.add(key)
                continue
            val = (1 if p[col["outline"]].strip() == "1" else 0,
                   1 if p[col["bold"]].strip() == "1" else 0,
                   1 if p[col["shadow"]].strip() == "1" else 0)
            want[key] = val
            legacy[(win, idc)] = val   # буквально, без `san` — прежнее поведение шага
    return want, legacy, cond


def san(s):
    """★ САНИТАЙЗЕР ИМЁН ИМПОРТЁРА (`asda_jui_generic_umg.py:110`) — ОБЯЗАН СОВПАДАТЬ С НИМ.
    Всё, что не `[A-Za-z0-9_]`, становится `_`. Поправка ревизии 24.09.2026: без этого шага
    таблица не находила 8 узлов из 75 строк `bold=1`, потому что в разметке у них ДЕФИС или
    ПРОБЕЛ (`CHARAINFO_CAMPWART1-S`, окно `NPC_LINE NPC`, `Hunt report_Text1`), а в ассете —
    подчёркивание. Стиль этим контролам просто не доезжал."""
    return re.sub(r"[^A-Za-z0-9_]", "_", (s or "").strip())


def node_idc_legacy(name):
    """ПРЕЖНЕЕ (буквальное) правило сопоставления — оставлено НАРОЧНО, см. `ASDA_STYLE926_SANMATCH_ALL`."""
    return name[:-5] if name.endswith("_Text") else name


def node_keys(name):
    """Имя виджета → КАНДИДАТЫ в idc, в порядке убывания точности.

    Импортёр даёт подписи имя `{idc}` либо `{idc}_Text`. ★ Поправка ревизии 24.09.2026: слепое
    срезание `_Text` ЛОМАЕТ контролы, у которых `_Text` есть в самом `idc`
    (`ItemOptionValue_Top_Text`, `Item_Union_Top_Text`, `Pet_Inven_Top_Text`,
    `Strength_Part_Top_Text`, `Hunt report_Text_Move`) — они превращались в `..._Top` и в таблице
    не находились. Поэтому пробуем СНАЧАЛА имя как есть, и только потом — без хвоста `_Text`."""
    out = [name]
    if name.endswith("_Text"):
        out.append(name[:-5])
    return out


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def _rgba_env(name, default):
    v = os.environ.get(name)
    if not v:
        return default
    try:
        parts = [float(x) for x in v.split(",")]
        return parts if len(parts) == 4 else default
    except Exception:                                        # noqa: BLE001
        return default


def main():
    dry = bool(os.environ.get("ASDA_STYLE926_DRYRUN"))
    san_all = bool(os.environ.get("ASDA_STYLE926_SANMATCH_ALL"))   # ★ см. гейт ниже
    sh_rgba = _rgba_env("ASDA_STYLE926_SHADOW_RGBA", [0.0, 0.0, 0.0, 1.0])
    want, legacy, cond = load_table()
    log("таблица: %d безусловных, %d условных (не трогаем)%s%s"
        % (len(want), len(cond), "  [СУХОЙ ПРОГОН]" if dry else "",
           "  [ИСПРАВЛЕННОЕ СОПОСТАВЛЕНИЕ НА ВСЕ СТИЛИ]" if san_all else ""))

    assets = [a for a in EAL.list_assets(UI_ROOT, True, False) if "/WBP_" in a]
    # ★ 09.10.2026 (СРЕДН-1): `ASDA_STYLE926_ONLY=WBP_A,WBP_B` — шаг только по этим окнам (настоящая запись по одному окну,
    #   закон owner-asset-edits-never-lose); пусто — все окна, как прежде. Неизвестное имя — стоп, а не «0 правок».
    only = [s.strip() for s in os.environ.get("ASDA_STYLE926_ONLY", "").split(",") if s.strip()]
    if only:
        assets = [a for a in assets if a.split(".")[0].rsplit("/", 1)[-1] in only]
        found = set(a.split(".")[0].rsplit("/", 1)[-1] for a in assets)
        if found != set(only):
            raise RuntimeError("ASDA_STYLE926_ONLY: нет окон %s" % sorted(set(only) - found))
        log("только окна: %s" % ", ".join(sorted(found)))
    B = unreal.AsdaUmgBuilder
    st = {"seen": 0, "skip": 0, "out_on": 0, "out_off": 0,
          "face_reg": 0, "pbold_on": 0, "sh_on": 0, "sh_off": 0,
          "clone": 0,    # ★ 09.10.2026: «clone» — текстов-клонов, сопоставленных по шаблону (CLONE_TEMPLATES)
          "itemcount": 0}   # ★ 26.10.2026: чисел количества, чью ОБВОДКУ ведёт шаг asda_ui_item_count_outline
    changed_assets = 0
    login_skipped = []   # ★ 15.09.2026 (раздел 14): окна сцены логина, пропущенные защитой

    for a in assets:
        path = a.split(".")[0]
        if _login_blocked(path):   # ★ 15.09.2026 (раздел 14): окно сцены логина — только с разрешением
            login_skipped.append(path.rsplit("/", 1)[-1])
            continue
        bp = EAL.load_asset(path)
        if not bp:
            continue
        root = B.get_root_canvas(bp)
        if not root:
            continue
        win = path.rsplit("/", 1)[-1]
        if win.startswith("WBP_"):
            win = win[4:]
        win_raw = win      # имя ПАКЕТА как есть — им сверяется ПРЕЖНЯЯ (буквальная) таблица
        win = san(win)     # ★ имя пакета уже санитизовано, но приводим к одному виду с ключами
        touched = 0
        for w in walk(root):
            if not isinstance(w, unreal.TextBlock):
                continue
            st["seen"] += 1
            # ★ 26.10.2026 (решение владельца): число количества в ячейках предметов — с чёрной обводкой 1 px. Её ставит шаг
            #   `asda_ui_item_count_outline` (идёт ПОСЛЕ этого), поэтому у таких узлов здесь пропускается ТОЛЬКО ветка
            #   «ОБВОДКА» (иначе «нет в таблице → 0» снимала бы её при каждом прогоне). Гарнитура и тень/псевдо-жирный
            #   оригинала ставятся как всем (поправка ревизии wave_1026 Н1: прежде пропускался узел целиком).
            is_cnt = _ITEMCOUNT.is_count_node(win_raw, w.get_name(), w)
            if is_cnt:
                st["itemcount"] += 1
            tpl = clone_template(win, w.get_name())   # ★ 09.10.2026 СРЕДН-1: клон ищется под idc своего шаблона
            if tpl:
                st["clone"] += 1
            cands = [(win, k) for k in ([tpl] if tpl else node_keys(w.get_name()))]
            if any(k in cond for k in cands):
                st["skip"] += 1
                continue
            # Нет в таблице ⇒ оригинал стиль этому контролу не ставит.
            key = next((k for k in cands if k in want), cands[0])
            outline, bold, shadow = want.get(key, (0, 0, 0))
            # ★★★ ГЕЙТ ИСПРАВЛЕННОГО СОПОСТАВЛЕНИЯ (24.09.2026, поправка ревизии).
            #   Починка `san`/`node_keys` открыла таблице 9 узлов, которых она раньше не находила.
            #   ЖИРНЫЙ по ним ставим сразу — это задача текущей волны (8 узлов; девятый, 2581
            #   `CHAT_NEW` без `idc`, живёт в рантайме `AsdaChatWindowSubsystem::BuildQueTipTexts`).
            #   ОБВОДКУ И ТЕНЬ — НЕТ, без слова владельца: та же починка включила бы обводку ещё
            #   32 контролам (31 счётчик `Hunt_report_Mark_*` окна `Hunt report_Win` + `SKILL_TEST_TEXT2`)
            #   и тень двум (`Package_Contents_Win`). Это ИЗМЕНЕНИЕ ВИДА двух окон, а лишняя обводка
            #   у тёмного текста — ровно то, на что владелец жаловался трижды (см. шапку). Данные
            #   оригинала за такую правку, поэтому она не выброшена, а внесена в реестр отложенного.
            #   Включить целиком: `ASDA_STYLE926_SANMATCH_ALL=1`.
            if not san_all:
                lval = legacy.get((win_raw, tpl or node_idc_legacy(w.get_name())), (0, 0, 0))   # клон — по шаблону
                outline, shadow = lval[0], lval[2]
            # ★ Плашки имён: обводка ВСЕМ текстам — ровно как ставит их
            #   собственный генератор `asda_build_nameplates.py`.
            #   ★ 2026-09-24: `bold` тут теперь 0 — жирной гарнитуры в оригинале нет НИГДЕ (см. шапку),
            #     а над-головным плашкам положена именно ОБВОДКА крестом ±1 px, а не псевдо-жирный.
            if win in ALWAYS_OUTLINED_WINDOWS:
                outline, bold = 1, 0
            try:
                f = w.get_editor_property("font")

                # ── ОБВОДКА ── (у чисел количества её ведёт шаг asda_ui_item_count_outline — см. `is_cnt`)
                os_ = f.get_editor_property("outline_settings")
                if not is_cnt and int(os_.get_editor_property("outline_size")) != outline:
                    if not dry:
                        os_.set_editor_property("outline_size", int(outline))
                        f.set_editor_property("outline_settings", os_)
                        w.set_editor_property("font", f)
                    st["out_on" if outline else "out_off"] += 1
                    touched += 1

                # ── ГАРНИТУРА: ВСЕГДА Regular (★ 2026-09-24, обоснование в шапке) ──
                #   Жирного начертания в оригинале нет ни у одного контрола; «жирный» там — второй
                #   проход того же глифа, он ставится ниже вместе с тенью.
                if str(f.get_editor_property("typeface_font_name")) != "Regular":
                    if not dry:
                        f.set_editor_property("typeface_font_name", unreal.Name("Regular"))
                        w.set_editor_property("font", f)
                    st["face_reg"] += 1
                    touched += 1

                # ── ВТОРОЙ ПРОХОД ГЛИФА: ТЕНЬ (0x400000) ИЛИ ПСЕВДО-ЖИРНЫЙ (0x200000) ──
                #   ★★★ ПРИЗНАК — АЛЬФА ЦВЕТА, А НЕ СДВИГ. Сдвиг `(1,1)` у `UTextBlock` — ДЕФОЛТ UE,
                #   он стоит у всех 7311 текстов, но `ShadowColorAndOpacity` при этом `A = 0` ⇒ второй
                #   проход НЕ РИСУЕТСЯ. Если считать признаком сдвиг, шаг «снимет тень» у 6839 текстов,
                #   которой там и не было, и не включит её ни одному из 537, кому она положена
                #   (поймано сухим прогоном до применения).
                #   Стили эталона ВЗАИМОИСКЛЮЧАЮЩИЕ (`if / else if / else if`, поле перезаписывается),
                #   и в таблице пересечений нет: строк `bold=1 и shadow=1` — 0, `bold=1 и outline=1` — 0.
                #     ЖИРНЫЙ(font_text.c:6832): квады в `penX+1` и обратно в `penX` тем же цветом
                #                               ⇒ `ShadowOffset (1,0)`, цвет = ЗАЛИВКА контрола. 1:1.
                #     ТЕНЬ  (font_text.c:6887): второй глиф в `penX+1 / penY+1` цветом `shadowColor`
                #                               ⇒ `ShadowOffset (1,1)`, цвет чёрный (см. шапку).
                col = w.get_editor_property("shadow_color_and_opacity")
                off = w.get_editor_property("shadow_offset")
                # ★ ПОРЯДОК КАК В ЭТАЛОНЕ: жирный (`:6832`) проверяется ПЕРВЫМ, тень (`:6887`) второй.
                if bold:
                    fill = w.get_editor_property("color_and_opacity").specified_color
                    want_off = (1.0, 0.0)
                    want_col = unreal.LinearColor(fill.r, fill.g, fill.b, fill.a)
                    ctr = "pbold_on"
                elif shadow:
                    want_off, want_col, ctr = (1.0, 1.0), unreal.LinearColor(*sh_rgba), "sh_on"
                else:
                    want_off, want_col, ctr = None, unreal.LinearColor(0.0, 0.0, 0.0, 0.0), "sh_off"
                same = (abs(col.a - want_col.a) < 0.01
                        and (want_col.a < 0.01 or (abs(col.r - want_col.r) < 0.004
                                                   and abs(col.g - want_col.g) < 0.004
                                                   and abs(col.b - want_col.b) < 0.004))
                        and (want_off is None
                             or (abs(off.x - want_off[0]) < 0.01 and abs(off.y - want_off[1]) < 0.01)))
                if not same:
                    if not dry:
                        if want_off is not None:
                            w.set_editor_property("shadow_offset", unreal.Vector2D(*want_off))
                        # Гасим ИМЕННО альфой — сдвиг оставляем дефолтным, как у всего проекта.
                        w.set_editor_property("shadow_color_and_opacity", want_col)
                    st[ctr] += 1
                    touched += 1
            except Exception as e:                           # noqa: BLE001
                unreal.log_warning("[обводка926] %s / %s: %s" % (win, w.get_name(), e))

        if touched:
            changed_assets += 1
            if not dry:
                # ★ `CompileAndSave` только КОМПИЛИРУЕТ (имя врёт) — сохранение делаем сами.
                B.compile_and_save(bp)
                if not EAL.save_loaded_asset(bp, False):
                    raise RuntimeError("не сохранён ассет %s" % path)

    log("окон %d, текстов просмотрено %d, изменено окон %d" % (len(assets), st["seen"], changed_assets))
    log("ОБВОДКА: +%d / −%d   ГАРНИТУРА→Regular: %d   ПСЕВДО-ЖИРНЫЙ: +%d   ТЕНЬ: +%d / −%d   условных пропущено %d   "
        "клонов по шаблону %d   чисел количества (обводку ведёт asda_ui_item_count_outline) %d"
        % (st["out_on"], st["out_off"], st["face_reg"], st["pbold_on"],
           st["sh_on"], st["sh_off"], st["skip"], st["clone"], st["itemcount"]))
    if login_skipped:   # ★ 15.09.2026 (раздел 14): итог пропуска окон сцены логина одной строкой
        unreal.log_warning("[LOGIN-GUARD] [обводка926] окон сцены логина пропущено: %d (%s) — правка только с явного разрешения "
                           "владельца и ASDA_ALLOW_LOGIN_WRITE=1" % (len(login_skipped), ", ".join(login_skipped)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[обводка926] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
