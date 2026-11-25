# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_frame_frac_rows.py — ★★ РЯДЫ РАМКИ ОКНА НА ВСЮ ВЫСОТУ ЭКРАНА ДОЛЯМИ 768 (17.11.2026, продолжение правки полосы обучения 69200).
#   [ОРИГИНАЛ] — правка АССЕТОВ, кода не касается (закон no-runtime-widget-fixes).
#
#   ДЕФЕКТ. Пять магазинов (6000 SHELLITEM_STORE_WIN, 6800 SHELLITEM_STORE2_WIN, 15000 HAIRSHOP_WIN, 15300 BOOSTER_SHOP_WIN,
#   26000 PackageShop_Win) — окно на 99 % высоты экрана (.JUI AlignYSize="32 0 99", верно), рамка — скин `Win_Mg`, тип
#   `ResizeWindow` с углами 20×20 px (RE/docs/JuiSkin.ini). Импортёр разбил ряды рамки ДОЛЯМИ экрана 768: верхний ряд до
#   анкера 20/768, средний до 0.99 − 20/768, нижний до 0.99. На 1024×768 верно, а выше экрана углы и верх/низ рамки
#   растягиваются пропорционально (2160: 56 px вместо 20) — картинка угла плывёт.
#
#   ПРАВКА. Каждая граница ряда переводится в «край окна + пиксели» (база 768): ближний из двух краёв рамки (0 и 0.99) +
#   round((a − край)·768). Верхний ряд — точечный анкер 0, высота 20; нижний — точечный анкер 0.99, y = −20; средний —
#   растянут 0…0.99 с полями 20. Сдвиги шва (шаг asda_ui_frame_seam_span: поля −1, ZOrder) сохраняются. 1024×768 не
#   меняется. Ось X не трогается (у этих рамок точечный анкер 1). Панели покупки внутри окон стоят на процентах по .JUI
#   (AlignYPos режим 32) — это оригинал, шаг их не касается.
#   БЕЗОПАСНОСТЬ: рамка трогается, только если 9 кусков `<префикс>_0…8`, нижний край рамки 0 и верхний ≥ 0.9; идемпотентно;
#   бэкап каждого ассета до записи (набор `frame_frac_rows`).
#
# Запуск (редактор и игра ЗАКРЫТЫ):
#   python Tools\run_headless_safe.py Tools\asda_ui_frame_frac_rows.py --marker "РЯДЫ-РАМКИ: ГОТОВО"
#   ASDA_FRAMEROWS_DRYRUN=1 — сухой прогон.
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

DRYRUN_ENV = "ASDA_FRAMEROWS_DRYRUN"   # ключ сухого прогона (таблица DRY_ENV раннера пост-импорта)
BACKUP_SET = "frame_frac_rows"         # набор бэкапа
BASE_H = 768.0                         # высота корня .JUI, от которой импортёр считал доли
EPS = 1e-4

# Окно → префикс кусков рамки (`<префикс>_0…8`).
FRAMES = [
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_WIN_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN", "BOOSTER_SHOP_WIN_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_HAIRSHOP_WIN", "HAIRSHOP_WIN_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_PackageShop_Win", "PackageShop_Win_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_SHELLITEM_STORE2_WIN", "SHELLITEM_STORE2_WIN_0"),
]


def log(m):
    """Строка журнала шага."""
    if unreal:
        unreal.log("[ряды-рамки] %s" % m)
    else:
        print("[ряды-рамки] %s" % m)


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


def _edge(a, lo, hi):
    """Анкер → (край рамки, пиксели от него при 768)."""
    base = lo if abs(a - lo) <= abs(a - hi) else hi
    return base, float(round((a - base) * BASE_H))


def _plan(anc, off, lo, hi):
    """Новые (ay0, ay1, top, bottom) для куска; None — уже в пиксельном виде."""
    _, ay0, _, ay1 = anc
    _, t, _, b = off
    if all(min(abs(v - lo), abs(v - hi)) < EPS for v in (ay0, ay1)):
        return None
    b0, p0 = _edge(ay0, lo, hi)
    if abs(ay0 - ay1) > EPS:                 # растянут: низ = a1·H − b
        b1, p1 = _edge(ay1, lo, hi)
        y1 = p1 - b
    else:                                    # точечный: b = высота
        b1, y1 = b0, p0 + t + b
    y0 = p0 + t
    if abs(b0 - b1) < EPS:
        return (b0, b0, y0, y1 - y0)
    return (b0, b1, y0, -y1)


def main():
    """Точка входа."""
    dry = _is_dry()
    B = unreal.AsdaUmgBuilder
    EAL = unreal.EditorAssetLibrary
    import asda_ui_text_block_align as T
    fixed = done = inspect = 0
    for path, pref in FRAMES:
        name = path.rsplit("/", 1)[-1]
        bp = EAL.load_asset(path)
        if not bp:
            log("НЕТ АССЕТА %s — осмотреть" % path)
            inspect += 1
            continue
        root = B.get_root_canvas(bp)
        pieces = {}
        for i in range(root.get_children_count()):
            w = root.get_child_at(i)
            if w and w.get_name().startswith(pref + "_"):
                tail = w.get_name()[len(pref) + 1:]
                if tail.isdigit():
                    pieces[int(tail)] = w
        if sorted(pieces) != list(range(9)):
            log("%s: кусков %s — осмотреть" % (name, sorted(pieces)))
            inspect += 1
            continue
        ys = [v for w in pieces.values() for v in (_lay(w)[2][1], _lay(w)[2][3])]
        lo, hi = min(ys), max(ys)
        if abs(lo) > EPS or hi < 0.9:
            log("%s: края рамки %.4f…%.4f — не тот вид, осмотреть" % (name, lo, hi))
            inspect += 1
            continue
        plan = []
        for k in range(9):
            _, _, anc, off = _lay(pieces[k])
            new = _plan(anc, off, lo, hi)
            if new:
                plan.append((k, anc, off, new))
        if not plan:
            log("%s: готово" % name)
            done += 1
            continue
        for k, anc, off, new in plan:
            log("%s / %s_%d: анкер Y %.5f…%.5f поля T%.1f B%.1f → анкер Y %.2f…%.2f поля T%.1f B%.1f%s"
                % (name, pref, k, anc[1], anc[3], off[1], off[3], new[0], new[1], new[2], new[3], "  [СУХОЙ]" if dry else ""))
        if dry:
            fixed += 1
            continue
        T._ensure_backup(path, set_name=BACKUP_SET)
        for k, anc, off, new in plan:
            s, lay, _a, o = _lay(pieces[k])
            a = lay.get_editor_property("anchors")
            a.set_editor_property("minimum", unreal.Vector2D(anc[0], new[0]))
            a.set_editor_property("maximum", unreal.Vector2D(anc[2], new[1]))
            lay.set_editor_property("anchors", a)
            lay.set_editor_property("offsets", unreal.Margin(o[0], new[2], o[2], new[3]))
            s.set_editor_property("layout_data", lay)
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % path)
        fixed += 1
    log("исправлено рамок %d, уже готово %d, осмотреть %d%s" % (fixed, done, inspect, "  [СУХОЙ ПРОГОН]" if dry else ""))
    if inspect:
        raise RuntimeError("есть рамки «осмотреть»: %d" % inspect)
    log("РЯДЫ-РАМКИ: ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[ряды-рамки] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
