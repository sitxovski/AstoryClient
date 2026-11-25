# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_lang_select_window.py — ★★★ [ПОРТ] 09.10.2026 ОКНО ПЕРВОГО ВЫБОРА ЯЗЫКА: НОВЫЙ АССЕТ `WBP_LANG_SELECT`.
#   ★ 11.10.2026 — РАЗМЕТКА V3 (выбор владельца «однозначно», `Reports/owner_1011/lang_window/variants.md`).
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
#   Решение владельца 09.10.2026 (мультиязычность): при первом запуске — модальное окно из трёх кнопок
#   «English», «Русский», «العربية» (неактивна). В оригинале такого окна нет. Разметка — АССЕТ (закон
#   `no-runtime-widget-fixes`), код (`UAsdaLangSelectSubsystem`) ставит только подписи из loc 990350…990353 и обработчики.
#   Шаг 1 (создание, 09.10): основа — нейтральный вариант окна выбора логина `WBP_MESSAGE2_WIN_NEUTRAL`:
#     • `/Game/AsdaAssets/UI/CommonWin/WBP_LANG_SELECT` — ДУБЛИКАТ ассета (исходник не меняется);
#     • третья кнопка `MESSAGE2_BUT02_BASE_grp` — клон группы второй кнопки (`MESSAGE2_BUT01_BASE_grp`, 6 узлов);
#     • значки-кружки кнопок (`*_ICON_*`) свёрнуты, подпись — на всю кнопку по центру;
#     • третья кнопка выключена (`is_enabled = False`), подпись серая; подписи в ассете — только для редактора.
#   Шаг 2 (разметка V3, 11.10): прежде кнопки стояли рядом на нижней кромке рамки, третья накрывала значок-печать угла.
#     • рамка — кисти рамки окна настроек `WBP_SYSTEM_WIN` (`SYSTEM_WIN_0_0…8`, атлас n_01, углы 20, без печати),
#       надетые на прежние узлы `MESSAGE2_WIN_0_0…8`; окно 280×172 по центру базы 1024×768 (L372 T298);
#     • бурая полоса заголовка того же окна — клоны `System_LBase_6_0` (торец 48) + `System_Base_3_0` (середина) +
#       `System_RBase_4_0` (торец 86), высота 37, отступ 2 px от кромки (как `System_Base_grp`), значок
#       `System_Icon_8_0` (ne_skillbar2, 35×35) слева: узлы `LANG_BandL/C/R`, `LANG_BandIcon`;
#     • вопрос `MESSAGE2_TXT00` — подписью на полосе (одной строкой, по центру, без переноса);
#     • кнопки 180×28 СТОЛБЦОМ по центру, шаг 36 (сверху вниз English / Русский / العربية); тянется только середина
#       скина ne_skillbar1, торцы 4 px свои.
#   Имена узлов, которые ищет код (`MESSAGE2_TXT00`, `MESSAGE2_BUT00..02(_Text)`), не меняются.
#
# ═══ ПРОВЕРКА ═══════════════════════════════════════════════════════════════════════════════════════
#   Идемпотентно: разметка V3 уже стоит (есть `LANG_BandL`) — раскладка пересчитывается в памяти и сравнивается с
#   ассетом по дампу: совпало — 0 правок, ничего не сохраняется; расходится — это ручная правка, СТОП без записи.
#   Ассет есть, но без третьей кнопки (чужой/ручной) — СТОП. После правки — сверка дампов «до/после»: изменились ровно
#   ожидаемые узлы, новые — ровно ожидаемые; иначе исключение без сохранения. Перед записью — резервная копия .uasset.
#   Сухой прогон: ASDA_LANGWIN_DRYRUN=1 (ничего не пишет, даже копию). Маркер: «ОКНО ЯЗЫКА: ГОТОВО».
# Запуск (редактор закрыт, -game не запущен, окон не показывать; под замком UE):
#   python Tools\run_headless_safe.py Tools\asda_ui_lang_select_window.py --marker "ОКНО ЯЗЫКА: ГОТОВО"
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_option_extra_rows as X   # общие помощники клонирования узлов (без побочных эффектов при импорте)

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

SRC = "/Game/AsdaAssets/UI/Login/WBP_MESSAGE2_WIN_NEUTRAL"
# ★ Папка CommonWin, НЕ UI/Login: все пакеты /UI/Login/ защищены сторожем сцены логина (LOGIN-GUARD — отказ сохранения
#   в автоматической сессии, закон owner-login-light-protected), а окно языка — новый общий ассет.
DST = "/Game/AsdaAssets/UI/CommonWin/WBP_LANG_SELECT"
SYS_WIN = "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_WIN"   # образец V3: рамка n_01 + полоса заголовка (только чтение)
DRYRUN_ENV = "ASDA_LANGWIN_DRYRUN"   # ключ сухого прогона (его включает и `asda_ui_post_import --dry-run`)
BACKUP_SET = "langwin_v3"            # набор резервной копии .uasset перед записью (@@ASTORY_DEV_ROOT@@\_backup\<дата>_<набор>)
FRAME_L = 377.0          # левая кромка рамки исходника (узлы MESSAGE2_WIN_0_0/3/6) — раскладка шага 1
FRAME_W = 270.0          # ширина рамки исходника (26 + 218 + 26)
BTN_W = 78.0             # ширина группы кнопки шага 1 (как у первой кнопки исходника)
BTN_H = 28.0             # высота группы кнопки (скин ne_skillbar1)
CX, CY = 512.0, 384.0    # центр окна V3 (база 1024×768)
V3_W, V3_H = 280.0, 172.0   # размер окна V3
V3_BTN_W = 180.0         # ширина кнопки столбца V3
V3_STEP = 36.0           # шаг столбца кнопок V3
GREY = unreal.LinearColor(0.35, 0.35, 0.35, 1.0)   # подпись неактивной кнопки
# ★ 12.10.2026 (владелец: «арабский сейчас недоступен — закрась его тёмно-серым»): скин неактивной третьей кнопки
#   (три картинки ne_skillbar1: середина и торцы, и фон самой кнопки — она поверх них) затемняется тинтом, подпись остаётся серой. Шаг идемпотентен (V3g).
DARK = unreal.LinearColor(0.28, 0.28, 0.28, 1.0)
BUT02_SKIN = ("MESSAGE2_BUT02_BASE_15_0", "MESSAGE2_BUT02_LBASE_16_0", "MESSAGE2_BUT02_RBASE_17_0", "MESSAGE2_BUT02")
# Имена клонов третьей кнопки (детей группы второй кнопки → третьей).
BUT02_MAP = {
    "MESSAGE2_BUT01_BASE_8_0": "MESSAGE2_BUT02_BASE_15_0",
    "MESSAGE2_BUT01_LBASE_9_0": "MESSAGE2_BUT02_LBASE_16_0",
    "MESSAGE2_BUT01_RBASE_10_0": "MESSAGE2_BUT02_RBASE_17_0",
    "MESSAGE2_BUT01": "MESSAGE2_BUT02",
    "MESSAGE2_BUT01_Text": "MESSAGE2_BUT02_Text",
    "MESSAGE2_BUT01_ICON_12_0": "MESSAGE2_BUT02_ICON_18_0",
}
LABELS = {"MESSAGE2_BUT00_Text": "English", "MESSAGE2_BUT01_Text": "Русский", "MESSAGE2_BUT02_Text": "العربية",
          "MESSAGE2_TXT00": "Choose your language / Выберите язык"}
# Узлы полосы заголовка V3: имя клона → узел окна настроек.
V3_BAND = [("LANG_BandL", "System_LBase_6_0"), ("LANG_BandC", "System_Base_3_0"),
           ("LANG_BandR", "System_RBase_4_0"), ("LANG_BandIcon", "System_Icon_8_0")]


def log(m):
    """Строка журнала шага."""
    unreal.log("[окно языка] %s" % m)


# ── Общие помощники раскладки (ими же пользуется `asda_ui_lang_select_variants.py`) ───────────────────────────────────

def frame9(st, left, top, w, h, lw=None, rw=None, th=None, bh=None):
    """Рамка 9 частей MESSAGE2_WIN_0_0..8 на прямоугольник (left, top, w, h); углы — своего размера (не тянутся).
    Размеры углов по умолчанию — текущие (рамка MESSAGE2: 26/26 по X, 38 сверху, 26 снизу)."""
    o0, o2, o6 = X.offsets(st.get("MESSAGE2_WIN_0_0")), X.offsets(st.get("MESSAGE2_WIN_0_2")), X.offsets(st.get("MESSAGE2_WIN_0_6"))
    lw = o0.right if lw is None else lw
    rw = o2.right if rw is None else rw
    th = o0.bottom if th is None else th
    bh = o6.bottom if bh is None else bh
    xs = [(left, lw), (left + lw, w - lw - rw), (left + w - rw, rw)]
    ys = [(top, th), (top + th, h - th - bh), (top + h - bh, bh)]
    for r in range(3):
        for c in range(3):
            X.set_offsets(st.get("MESSAGE2_WIN_0_%d" % (r * 3 + c)), xs[c][0], ys[r][0], xs[c][1], ys[r][1])


def button(st, n, x, y, w):
    """Группа кнопки n (0…2): левая кромка x, верх y, ширина w; фон-середина тянется только по ширине, торцы — свои."""
    g = st.get("MESSAGE2_BUT%02d_BASE_grp" % n)
    X.set_offsets(g, x, y, w, BTN_H)
    for i in range(g.get_children_count()):
        k = g.get_child_at(i)
        nm = k.get_name()
        ko = X.offsets(k)
        if "_LBASE_" in nm:
            X.set_offsets(k, 0.0, ko.top, ko.right, ko.bottom)
        elif "_RBASE_" in nm:
            X.set_offsets(k, w - ko.right, ko.top, ko.right, ko.bottom)
        elif "_ICON_" in nm:
            continue
        elif "_BASE_" in nm:
            X.set_offsets(k, 4.0, ko.top, w - 8.0, ko.bottom)
        elif nm.endswith("_Text") or isinstance(k, unreal.Button):
            X.set_offsets(k, 3.0, ko.top, w - 6.0, ko.bottom)


def text_box(st, left, top, w, h):
    """Текст вопроса: прямоугольник и выравнивание по центру."""
    t = st.get("MESSAGE2_TXT00")
    X.set_offsets(t, left, top, w, h)
    t.set_editor_property("justification", unreal.TextJustify.CENTER)


def nodes_of(path):
    """Узлы окна-образца {имя: виджет} (только чтение, пакет не сохраняется)."""
    bp = EAL.load_asset(path)
    if bp is None:
        raise RuntimeError("нет образца %s" % path)
    return X.walk(B.get_root_canvas(bp))


def index_after(bp, name):
    """Индекс сразу после узла `name` среди детей корня."""
    root = B.get_root_canvas(bp)
    for i in range(root.get_children_count()):
        if root.get_child_at(i).get_name() == name:
            return i + 1
    raise RuntimeError("нет %s в корне" % name)


def clone_from(st, bp, src, new_name, index):
    """Клон узла окна-образца (класс и свойства, в т.ч. кисть атласа) в корень окна: слот как у рамки окна
    (POINT (0,0), выравнивание 0), порядок рисования — `index`. Прямоугольник ставит раскладка."""
    root = B.get_root_canvas(bp)
    w_ = X.make_like(bp, src, root, new_name)
    X.place_like(w_, st.get("MESSAGE2_WIN_0_0"))
    if not B.move_child_to_index(root, w_, index):
        raise RuntimeError("не переставлен %s" % new_name)
    st.nodes[new_name] = w_
    return w_


# ── Разметка V3 ─────────────────────────────────────────────────────────────────────────────────────────────────

def v3_clone(st, bp, sysn):
    """V3, состав: кисти рамки окна настроек на узлы MESSAGE2_WIN_0_*, клоны полосы заголовка и значка (после рамки)."""
    for k in range(9):
        st.get("MESSAGE2_WIN_0_%d" % k).set_editor_property("brush", sysn["SYSTEM_WIN_0_%d" % k].get_editor_property("brush"))
    i = index_after(bp, "MESSAGE2_WIN_0_8")
    for n, (new, src) in enumerate(V3_BAND):
        clone_from(st, bp, sysn[src], new, i + n)


def v3_layout(st):
    """V3, раскладка (идемпотентна — одни и те же числа при каждом вызове): рамка 280×172 по центру, полоса 37 с
    отступом 2, значок 35×35, вопрос на полосе одной строкой, кнопки 180×28 столбцом с шагом 36."""
    L, T = CX - V3_W / 2.0, CY - V3_H / 2.0
    frame9(st, L, T, V3_W, V3_H, 20.0, 20.0, 20.0, 20.0)
    bl, bt, bw = L + 2.0, T + 2.0, V3_W - 4.0
    X.set_offsets(st.get("LANG_BandL"), bl, bt, 48.0, 37.0)
    X.set_offsets(st.get("LANG_BandC"), bl + 48.0, bt, bw - 48.0 - 86.0, 37.0)
    X.set_offsets(st.get("LANG_BandR"), bl + bw - 86.0, bt, 86.0, 37.0)
    X.set_offsets(st.get("LANG_BandIcon"), bl + 1.0, bt + 1.0, 35.0, 35.0)
    st.get("MESSAGE2_TXT00").set_editor_property("auto_wrap_text", False)
    text_box(st, bl + 38.0, bt + 12.0, bw - 48.0, 18.0)
    y0 = bt + 37.0 + 16.0
    for n in range(3):
        button(st, n, L + (V3_W - V3_BTN_W) / 2.0, y0 + n * V3_STEP, V3_BTN_W)


def grey_arabic(st):
    """V3g (12.10.2026): тинт тёмно-серого на скин неактивной кнопки «العربية». Возврат — True, если что-то изменилось."""
    changed = False
    for n in BUT02_SKIN:
        w_ = st.get(n)
        # у картинок скина — color_and_opacity; у самой кнопки (она поверх картинок и рисует лицо своим стилем) — background_color
        prop = "background_color" if isinstance(w_, unreal.Button) else "color_and_opacity"
        c = w_.get_editor_property(prop)
        if abs(c.r - DARK.r) > 1e-3 or abs(c.g - DARK.g) > 1e-3 or abs(c.b - DARK.b) > 1e-3:
            w_.set_editor_property(prop, DARK)
            changed = True
    return changed


def v3_touched(st):
    """Узлы, которые вправе измениться при переводе на V3 (кроме новых)."""
    out = {"MESSAGE2_TXT00"} | {"MESSAGE2_WIN_0_%d" % k for k in range(9)}
    for n in range(3):
        g = st.get("MESSAGE2_BUT%02d_BASE_grp" % n)
        out.add(g.get_name())
        out |= {g.get_child_at(i).get_name() for i in range(g.get_children_count())}
    return out


# ── Шаг 1: создание ассета (как 09.10) ────────────────────────────────────────────────────────────────────────

def layout_group(st, n, x):
    """Шаг 1: группа кнопки n (0…2) шириной BTN_W с левой кромкой x; значок свёрнут. Возврат — изменённые узлы."""
    g = st.get("MESSAGE2_BUT%02d_BASE_grp" % n)
    o = X.offsets(g)
    X.set_offsets(g, x, o.top, BTN_W, o.bottom)
    kids = [g.get_child_at(i) for i in range(g.get_children_count())]
    for k in kids:
        nm = k.get_name()
        ko = X.offsets(k)
        if "_LBASE_" in nm:
            X.set_offsets(k, 0.0, ko.top, ko.right, ko.bottom)
        elif "_RBASE_" in nm:
            X.set_offsets(k, BTN_W - ko.right, ko.top, ko.right, ko.bottom)
        elif "_BASE_" in nm:
            X.set_offsets(k, 4.0, ko.top, BTN_W - 8.0, ko.bottom)
        elif "_ICON_" in nm:
            k.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
        elif nm.endswith("_Text"):
            X.set_offsets(k, 3.0, ko.top, BTN_W - 6.0, ko.bottom)
            k.set_editor_property("justification", unreal.TextJustify.CENTER)
        elif isinstance(k, unreal.Button):
            X.set_offsets(k, 3.0, ko.top, BTN_W - 6.0, ko.bottom)
    return [g.get_name()] + [k.get_name() for k in kids]


def create(bp):
    """Шаг 1 на свежем дубликате: третья кнопка, ряд по 78 px, подписи. Возврат — (изменённые, новые) имена."""
    before = X.dump_map(bp)
    st = X.Step(bp, False)
    st.clone_tree(st.get("MESSAGE2_BUT01_BASE_grp"), st.get("RootCanvas"), "MESSAGE2_BUT02_BASE_grp", BUT02_MAP, 0.0, 0.0)
    gap = (FRAME_W - 3 * BTN_W) / 4.0
    touched = []
    for n in range(3):
        touched += layout_group(st, n, FRAME_L + gap + n * (BTN_W + gap))
    for nm, txt in LABELS.items():
        st.get(nm).set_text(txt)
    st.get("MESSAGE2_BUT02").set_editor_property("is_enabled", False)
    st.get("MESSAGE2_BUT02_Text").set_editor_property("color_and_opacity", unreal.SlateColor(GREY))
    after = X.dump_map(bp)
    allowed = set(touched) | set(LABELS) | {"MESSAGE2_BUT02_BASE_grp"} | set(BUT02_MAP.values())
    bad = [n for n in before if n not in after] + [n for n in before if before[n] != after.get(n) and n not in allowed]
    new = sorted(set(after) - set(before))
    if new != sorted(["MESSAGE2_BUT02_BASE_grp"] + list(BUT02_MAP.values())):
        bad.append("новые узлы: %s" % new)
    if bad:
        raise RuntimeError("СВЕРКА ШАГА 1 НЕ ПРОШЛА — не сохраняю: %s" % bad)
    return len(new)


def apply_v3(bp):
    """Шаг 2: перевод на V3 со сверкой дампов «до/после». Возврат — число новых узлов."""
    before = X.dump_map(bp)
    st = X.Step(bp, False)
    v3_clone(st, bp, nodes_of(SYS_WIN))
    v3_layout(st)
    after = X.dump_map(bp)
    allowed = v3_touched(st)
    new = sorted(set(after) - set(before))
    bad = [n for n in before if n not in after] + [n for n in before if before[n] != after.get(n) and n not in allowed]
    if new != sorted(n for n, _ in V3_BAND):
        bad.append("новые узлы: %s" % new)
    if bad:
        raise RuntimeError("СВЕРКА V3 НЕ ПРОШЛА — не сохраняю: %s" % bad)
    for n in sorted(allowed | set(new)):
        log("  %s" % after[n])
    return len(new)


def save(bp):
    """Резервная копия .uasset (если файл есть), компиляция и запись."""
    disk = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()),
                        "AsdaAssets", "UI", "CommonWin", "WBP_LANG_SELECT.uasset")
    if os.path.isfile(disk):
        import asda_headless_tool as HT   # резервная копия до правки (закон backup-before-edit)
        log("резервная копия: %s" % HT.backup_file(disk, BACKUP_SET))
    B.compile_and_save(bp)
    unreal.log("ASDA_WRITES_ASSET: %s" % DST)
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("не сохранён %s" % DST)


def main():
    """Создать/проверить ассет окна выбора языка в разметке V3."""
    dry = bool(os.environ.get(DRYRUN_ENV))
    if EAL.does_asset_exist(DST):
        bp = EAL.load_asset(DST)
        names = X.walk(B.get_root_canvas(bp))
        if "MESSAGE2_BUT02" not in names:
            raise RuntimeError("%s есть, но без MESSAGE2_BUT02 — не наш ассет/ручная правка, СТОП" % DST)
        if "LANG_BandL" in names:
            # V3 уже стоит: пересчёт раскладки в памяти и сверка дампа; правку не сохраняем никогда.
            before = X.dump_map(bp)
            v3_layout(X.Step(bp, False))
            diff = [n for n in before if before[n] != X.dump_map(bp).get(n)]
            if diff:
                raise RuntimeError("разметка V3 в ассете отличается от генератора (ручная правка?) — СТОП, не пишу: %s" % diff)
            # ★ V3g (12.10.2026): арабская кнопка тёмно-серая. Уже так — 0 правок; иначе — ровно три узла скина и запись.
            before_g = X.dump_map(bp)
            if not grey_arabic(X.Step(bp, False)):
                log("ассет уже в разметке V3 (+V3g) — 0 правок")
                log("ОКНО ЯЗЫКА: ГОТОВО (0 правок)")
                return
            after_g = X.dump_map(bp)
            bad = [n for n in before_g if before_g[n] != after_g.get(n) and n not in BUT02_SKIN]
            if bad:
                raise RuntimeError("V3g: изменились лишние узлы — не пишу: %s" % bad)
            if dry:
                log("[сухой] будет затемнён скин кнопки «العربية» (3 узла)")
                log("ОКНО ЯЗЫКА: ГОТОВО [СУХОЙ ПРОГОН]")
                return
            save(bp)
            log("ОКНО ЯЗЫКА: ГОТОВО — V3g: скин кнопки «العربية» затемнён (3 узла)")
            return
        if dry:
            log("[сухой] %s будет переведён на разметку V3 (+%d узла полосы заголовка)" % (DST, len(V3_BAND)))
            log("ОКНО ЯЗЫКА: ГОТОВО [СУХОЙ ПРОГОН]")
            return
        n = apply_v3(bp)
        grey_arabic(X.Step(bp, False))   # V3g
        save(bp)
        log("ОКНО ЯЗЫКА: ГОТОВО — %s переведён на V3(+V3g), новых узлов %d" % (DST, n))
        return
    if dry:
        log("[сухой] будет создан %s из %s (+6 узлов третьей кнопки) и переведён на V3 (+%d узла)" % (DST, SRC, len(V3_BAND)))
        log("ОКНО ЯЗЫКА: ГОТОВО [СУХОЙ ПРОГОН]")
        return
    if not EAL.duplicate_asset(SRC, DST):
        raise RuntimeError("не продублирован %s → %s" % (SRC, DST))
    bp = EAL.load_asset(DST)
    n1 = create(bp)
    n2 = apply_v3(bp)
    grey_arabic(X.Step(bp, False))   # V3g
    save(bp)
    log("ОКНО ЯЗЫКА: ГОТОВО — создан %s, новых узлов %d" % (DST, n1 + n2))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[окно языка] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
