# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_tutorial_win_anchor.py — ★★ ПОЛОСА ОБУЧЕНИЯ 69200 НА ШИРОКОМ ЭКРАНЕ (16.11.2026, снимок владельца alpha-8.0.0, англ.).
#   [ОРИГИНАЛ] — правка АССЕТА `WBP_Tutorial_WIN`, кода не касается (закон no-runtime-widget-fixes).
#
#   ДЕФЕКТ. Окно `Tutorial_WIN` (.JUI gamemain_jui.ini:333941) растянуто на ширину экрана: корень AlignXPos="1 1 -180",
#   AlignXSize="2 0 0" (левый край +1, правый край к правому краю родителя), рамки/фон/строка `Map_Empty` — тоже «2 …».
#   В оригинале (`CControl::CalcLeftTop@0x12DFBB0` / `CalcRightBottom@0x12DDED0`, разбор RE/docs/_archive/72_login_screen.md §4)
#   режим 2 = ПИКСЕЛЬНЫЙ отступ от правого края, а режим 1/5 = пиксельный от левого: углы рамки всегда 1–3 px, правый
#   болт `Wood_Bolt` (AlignXPos="2 -11") всегда в 11 px от правого конца строки. Импортёр перевёл такие куски в ДОЛИ
#   экрана 1024 (анкер 0.001…0.004 и т. п.), а детей `Map_Empty` оставил точечными от левого края: на 1024×768 кадр
#   верный, а шире — углы рамки раздуваются пропорционально (3440: угол 10 px вместо 3), правый болт стоит на x≈1009
#   посреди экрана, полоса `Map_Empty_3_0` обрывается на 1015 px.
#
#   ПРАВКА. Каждый слот корня с дробным анкером X (0 < a < 1) переводится в пиксели при 1024 (round(a·1024) + поле) и
#   привязывается к БЛИЖНЕМУ краю: кусок в левой половине — точечный анкер 0, в правой — точечный анкер 1, через
#   середину — растянутый 0…1 с полями от краёв. На 1024×768 кадр не меняется ни на пиксель; Y не трогается.
#   `Map_Empty_grp` становится растянутым, поэтому его дети: `Map_Empty_3_0` — растянут по ширине группы
#   (.JUI AlignXSize="2 -8"), `Wood_Bolt_5_0` — точечный анкер 1, x = −11 (.JUI AlignXPos="2 -11").
#   Сдвиги шва (шаг asda_ui_frame_seam_span: поля −1, ZOrder) сохраняются — они входят в «поле».
#   БЕЗОПАСНОСТЬ: идемпотентно (уже в целевом виде — «готово»); дети Map_Empty трогаются только в виде импортёра;
#   бэкап ассета до записи (набор `tutorial_win_anchor`). Структурный дамп ДО/ПОСЛЕ — в ASDA_TUTANCHOR_DUMP (папка).
#
# Запуск (редактор и игра ЗАКРЫТЫ):
#   python Tools\run_headless_safe.py Tools\asda_ui_tutorial_win_anchor.py --marker "ПОЛОСА-ОБУЧЕНИЯ: ГОТОВО"
#   ASDA_TUTANCHOR_DRYRUN=1 — сухой прогон.
import os
import sys
import traceback

try:
    import unreal
except ImportError:
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

DRYRUN_ENV = "ASDA_TUTANCHOR_DRYRUN"   # ключ сухого прогона (таблица DRY_ENV раннера пост-импорта)
DUMP_ENV = "ASDA_TUTANCHOR_DUMP"       # папка для дампа дерева ДО/ПОСЛЕ (не задана — без дампа)
BACKUP_SET = "tutorial_win_anchor"     # набор бэкапа
ASSET = "/Game/AsdaAssets/UI/Player_UI/Tutorial/WBP_Tutorial_WIN"
BASE_W = 1024.0                        # ширина корня .JUI, от которой импортёр считал доли
EPS = 0.05
GRP = "Map_Empty_grp"                  # строка-шапка полосы: становится растянутой
GRP_FILL = "Map_Empty_3_0"             # её картинка: на всю ширину группы
GRP_BOLT_R = "Wood_Bolt_5_0"           # правый болт: от правого края группы
BOLT_RIGHT_X = -11.0                   # .JUI AlignXPos="2 -11"


def log(m):
    """Строка журнала шага."""
    if unreal:
        unreal.log("[полоса-обучения] %s" % m)
    else:
        print("[полоса-обучения] %s" % m)


def _is_dry():
    return bool(os.environ.get(DRYRUN_ENV)) or bool(os.environ.get("ASDA_POSTSTEPS_DRYRUN"))


def _lay(w):
    """(слот, layout_data, (ax0, ay0, ax1, ay1), (l, t, r, b))."""
    s = w.get_editor_property("slot")
    lay = s.get_editor_property("layout_data")
    a = lay.get_editor_property("anchors")
    mn, mx = a.get_editor_property("minimum"), a.get_editor_property("maximum")
    o = lay.get_editor_property("offsets")
    return s, lay, (mn.x, mn.y, mx.x, mx.y), (o.left, o.top, o.right, o.bottom)


def _frac(v):
    return EPS / 100.0 < v < 1.0 - EPS / 100.0


def _plan_root(anc, off):
    """Новые (ax0, ax1, l, r) для слота корня с дробным анкером X; None — слот не наш."""
    ax0, _, ax1, _ = anc
    l, _, r, _ = off
    if not (_frac(ax0) or _frac(ax1)):
        return None
    x0 = round(ax0 * BASE_W) + l
    if abs(ax0 - ax1) > 1e-6:          # растянут: правый край = a1·W − r
        x1 = round(ax1 * BASE_W) - r
    else:                              # точечный: r = ширина
        x1 = x0 + r
    mid = BASE_W / 2.0
    if x1 <= mid:
        return (0.0, 0.0, x0, x1 - x0)
    if x0 >= mid:
        return (1.0, 1.0, x0 - BASE_W, x1 - x0)
    return (0.0, 1.0, x0, BASE_W - x1)


def _set_x(w, ax0, ax1, l, r):
    s, lay, anc, off = _lay(w)
    a = lay.get_editor_property("anchors")
    a.set_editor_property("minimum", unreal.Vector2D(ax0, anc[1]))
    a.set_editor_property("maximum", unreal.Vector2D(ax1, anc[3]))
    lay.set_editor_property("anchors", a)
    lay.set_editor_property("offsets", unreal.Margin(l, off[1], r, off[3]))
    s.set_editor_property("layout_data", lay)


def _dump(B, bp, name):
    d = os.environ.get(DUMP_ENV)
    if not d:
        return
    os.makedirs(d, exist_ok=True)
    body = B.dump_widget_tree(bp)
    rows = sorted(" ".join(x.split()) for x in body.split("\n") if x.strip())
    p = os.path.join(d, name)
    with open(p, "w", encoding="utf-8") as f:
        f.write("\n".join(rows) + "\n")
    log("дамп дерева → %s (%d строк)" % (p, len(rows)))


def main():
    """Точка входа."""
    dry = _is_dry()
    B = unreal.AsdaUmgBuilder
    EAL = unreal.EditorAssetLibrary
    bp = EAL.load_asset(ASSET)
    if not bp:
        raise RuntimeError("нет ассета %s" % ASSET)
    _dump(B, bp, "WBP_Tutorial_WIN_before.txt")
    root = B.get_root_canvas(bp)
    plan, grp = [], None
    for i in range(root.get_children_count()):
        w = root.get_child_at(i)
        if not w:
            continue
        if w.get_name() == GRP:
            grp = w
        _, _, anc, off = _lay(w)
        new = _plan_root(anc, off)
        if new:
            plan.append((w, anc, off, new))
    # Дети строки-шапки: только в виде импортёра (точечный анкер 0) или уже в целевом.
    kids = []
    if grp is None:
        raise RuntimeError("нет узла %s — осмотреть" % GRP)
    g_anc_new = next((p[3] for p in plan if p[0] == grp), None)
    _, _, g_anc, g_off = _lay(grp)
    if g_anc_new is not None:
        g_w = BASE_W - g_anc_new[2] - g_anc_new[3]          # ширина группы при 1024 (растянута 0…1)
    elif abs(g_anc[0]) < 1e-6 and abs(g_anc[2] - 1.0) < 1e-6:
        g_w = BASE_W - g_off[0] - g_off[2]                   # группа уже исправлена
    else:
        raise RuntimeError("%s: неожиданные анкеры %s — осмотреть" % (GRP, g_anc))
    for j in range(grp.get_children_count()):
        c = grp.get_child_at(j)
        if not c or c.get_name() not in (GRP_FILL, GRP_BOLT_R):
            continue
        _, _, anc, off = _lay(c)
        if c.get_name() == GRP_FILL:
            target = (0.0, 1.0, 0.0, 0.0)
            if abs(anc[0]) < 1e-6 and abs(anc[2] - 1.0) < 1e-6 and abs(off[0]) < EPS and abs(off[2]) < EPS:
                log("%s: готово" % GRP_FILL)
                continue
            if not (abs(anc[0]) < 1e-6 and abs(anc[2]) < 1e-6 and abs(off[0]) < EPS and abs(off[2] - g_w) <= 1.0 + EPS):
                raise RuntimeError("%s: вид не импортёра (анкеры %s, поля %s, ширина группы %.1f) — осмотреть"
                                   % (GRP_FILL, anc, off, g_w))
        else:
            target = (1.0, 1.0, BOLT_RIGHT_X, off[2])
            if abs(anc[0] - 1.0) < 1e-6 and abs(off[0] - BOLT_RIGHT_X) < EPS:
                log("%s: готово" % GRP_BOLT_R)
                continue
            if not (abs(anc[0]) < 1e-6 and abs(anc[2]) < 1e-6 and abs((off[0] - g_w) - BOLT_RIGHT_X) <= 1.0 + EPS):
                raise RuntimeError("%s: вид не импортёра (анкеры %s, поля %s, ширина группы %.1f) — осмотреть"
                                   % (GRP_BOLT_R, anc, off, g_w))
        kids.append((c, anc, off, target))
    for w, anc, off, new in plan + kids:
        log("%s: анкер X %.4f…%.4f поля L%.1f R%.1f → анкер X %.0f…%.0f поля L%.1f R%.1f%s"
            % (w.get_name(), anc[0], anc[2], off[0], off[2], new[0], new[1], new[2], new[3], "  [СУХОЙ]" if dry else ""))
    if not plan and not kids:
        log("готово: все слоты уже в пиксельном виде")
        log("ПОЛОСА-ОБУЧЕНИЯ: ГОТОВО")
        return
    if not dry:
        import asda_ui_text_block_align as T
        T._ensure_backup(ASSET, set_name=BACKUP_SET)
        for w, _a, _o, new in plan + kids:
            _set_x(w, *new)
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % ASSET)
        _dump(B, bp, "WBP_Tutorial_WIN_after.txt")
    log("слотов корня %d, детей шапки %d%s" % (len(plan), len(kids), "  [СУХОЙ ПРОГОН]" if dry else ""))
    log("ПОЛОСА-ОБУЧЕНИЯ: ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[полоса-обучения] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
