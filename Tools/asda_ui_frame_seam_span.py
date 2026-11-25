# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_frame_seam_span.py — ★★ ЩЕЛЬ НА СТЫКЕ РЯДОВ РАМКИ ОКНА, РАСТЯНУТОЙ ПО ВЫСОТЕ ЭКРАНА (08.11.2026, пачка 19, баг 4).
#   [ИСПРАВЛЕНИЕ] — правка АССЕТОВ, кода не касается (закон no-runtime-widget-fixes). Дополнение шага `asda_ui_frame_seam_overlap`.
#
#   ДЕФЕКТ (снимок владельца `Reports/batch19/screens/premium_gap.png`: премиум-магазин 6000, горизонтальная линия 1 px
#   правее вкладок, сквозь неё мир). Рамка окна на всю высоту экрана (`.JUI` `AlignYSize = "32 0 99"`) — 9 картинок, ряды
#   которой импортёр разбил ДОЛЯМИ экрана: верхний ряд до анкера 0.02604 (20/768), средний от него до 0.96396, нижний до 0.99.
#   Граница рядов падает на дробный пиксель (3840×2160: 0.02604 × 2160 = 56.25), Slate округляет каждый кусок сам — между
#   рядами остаётся строка без картинки. Самотест `asda.Shop.PremiumGapSelfTest` + `Tools/asda_premium_gap_scan.py`: при
#   3840×2160 просвет в строке 55 во всех вкладках; при 1024…2560 шов прячется под шапкой окна.
#   Шаг 24.10 (`asda_ui_frame_seam_overlap`) правил только куски с ТОЧЕЧНЫМ анкером; эти 6 рамок он отправил в «пропуск: не
#   точечный слот» (`Reports/frame_seam_2026-10-24/audit_all_frames.tsv`).
#
#   ПРАВКА (тот же приём, что у шага 24.10): средние куски уходят ПОД соседей на 1 единицу — средний ряд (3, 4, 5) на 1 вверх и
#   на 1 вниз, средняя колонка (1, 4, 7) на 1 влево и вправо (растянутая ось — оба поля −1, точечная — позиция −1 и размер +2);
#   ZOrder −1 у краёв середины, −2 у тела. Обе ориентации: рамки на высоту экрана (окна магазинов) и на ширину (окно обучения).
#   Углы не меняются. Соседи рисуются поверх, при целом стыке кадр не меняется.
#   БЕЗОПАСНОСТЬ: кусок трогается, только если он в точности в виде импортёра (анкер X точечный, Y растянут, поля Y = 0,
#   ZOrder 0, alignment 0); уже исправленный — «готово» (идемпотентно); иной вид любого куска — рамка «осмотреть», не трогается.
#   Бэкап каждого ассета до записи (набор `frame_seam_span`).
#
# Запуск (редактор и игра ЗАКРЫТЫ; коммандлет):
#   python Tools\run_headless_safe.py Tools\asda_ui_frame_seam_span.py --marker "ШОВ-РЯДОВ: ГОТОВО"
#   ASDA_FRAMESPAN_DRYRUN=1 — сухой прогон.
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

DRYRUN_ENV = "ASDA_FRAMESPAN_DRYRUN"   # ключ сухого прогона (таблица DRY_ENV раннера пост-импорта)
BACKUP_SET = "frame_seam_span"         # набор бэкапа
EPS = 0.05

# ЗАКРЫТЫЙ СПИСОК: рамки с вердиктом «не точечный слот» аудита 24.10 (окно → префикс кусков рамки, куски `<префикс>_0…8`).
FRAMES = [
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN", "SHELLITEM_STORE_WIN_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_SHOP_WIN", "BOOSTER_SHOP_WIN_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_HAIRSHOP_WIN", "HAIRSHOP_WIN_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_PackageShop_Win", "PackageShop_Win_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_SHELLITEM_STORE2_WIN", "SHELLITEM_STORE2_WIN_0"),
    ("/Game/AsdaAssets/UI/Player_UI/Tutorial/WBP_Tutorial_WIN", "Box_White1Stroke_BlackFill128Ot_1"),
]

# Кусок → (сдвиг по X: колонка середины, сдвиг по Y: ряд середины, новый ZOrder).
ROLE = {1: (True, False, -1), 7: (True, False, -1), 3: (False, True, -1), 5: (False, True, -1), 4: (True, True, -2)}


def log(m):
    """Строка журнала шага."""
    if unreal:
        unreal.log("[шов-рядов] %s" % m)
    else:
        print("[шов-рядов] %s" % m)


def _is_dry():
    return bool(os.environ.get(DRYRUN_ENV)) or bool(os.environ.get("ASDA_POSTSTEPS_DRYRUN"))


def _state(w):
    """(анкеры, поля, alignment, z) куска."""
    s = w.get_editor_property("slot")
    lay = s.get_editor_property("layout_data")
    a = lay.get_editor_property("anchors")
    mn, mx = a.get_editor_property("minimum"), a.get_editor_property("maximum")
    o = lay.get_editor_property("offsets")
    al = lay.get_editor_property("alignment")
    return (mn.x, mn.y, mx.x, mx.y), (o.left, o.top, o.right, o.bottom), (al.x, al.y), int(s.get_editor_property("z_order"))


def _want(k, off, anc):
    """Поля «стало» из полей «было» по роли куска и виду оси (точечная: позиция −1, размер +2; растянутая: оба поля −1)."""
    l, t, r, b = off
    dx, dy, _ = ROLE[k]
    span_x = abs(anc[0] - anc[2]) > 1e-6
    span_y = abs(anc[1] - anc[3]) > 1e-6
    if dx:
        l, r = (l - 1.0, r - 1.0) if span_x else (l - 1.0, r + 2.0)
    if dy:
        t, b = (t - 1.0, b - 1.0) if span_y else (t - 1.0, b + 2.0)
    return (l, t, r, b)


def _eq(a, b):
    return all(abs(float(x) - float(y)) <= EPS for x, y in zip(a, b))


def main():
    """Точка входа."""
    dry = _is_dry()
    B = unreal.AsdaUmgBuilder
    EAL = unreal.EditorAssetLibrary
    import asda_ui_text_block_align as T
    fixed = done = inspect = 0
    for path, pref in FRAMES:
        bp = EAL.load_asset(path)
        if not bp:
            log("НЕТ АССЕТА %s" % path)
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
            log("%s / %s: кусков %s — осмотреть" % (path, pref, sorted(pieces)))
            inspect += 1
            continue
        plan, ok, already = [], True, 0
        for k in sorted(ROLE):
            anc, off, al, z = _state(pieces[k])
            z_new = ROLE[k][2]
            # ★ 16.11.2026: уже исправленный кусок — «готово» ДО проверки вида. Шаг asda_ui_tutorial_win_anchor переводит
            #   рамку полосы обучения 69200 в пиксельные анкеры (углы и края середины — точечные), и без этой ветки
            #   повторный прогон считал бы её «осмотреть».
            if z == z_new and _eq(al, (0.0, 0.0)):
                already += 1
                continue
            # Рамка растянута долями экрана хотя бы по одной оси (иначе это случай шага 24.10, не этот).
            any_span = abs(anc[0] - anc[2]) > 1e-6 or abs(anc[1] - anc[3]) > 1e-6
            if not any_span or not _eq(al, (0.0, 0.0)):
                log("%s / %s_%d: вид не импортёра (анкеры %s, alignment %s) — осмотреть" % (path, pref, k, anc, al))
                ok = False
                break
            if z != 0:
                log("%s / %s_%d: «было» не совпало (поля %s, z %d) — осмотреть" % (path, pref, k, off, z))
                ok = False
                break
            plan.append((k, off, _want(k, off, anc), z, z_new))
        if not ok:
            inspect += 1
            continue
        if not plan:
            log("%s / %s: готово (все %d кусков уже исправлены)" % (path.rsplit("/", 1)[-1], pref, already))
            done += 1
            continue
        for k, off, new, z, z_new in plan:
            log("%s / %s_%d: поля %s → %s, z %d → %d%s" % (path.rsplit("/", 1)[-1], pref, k, tuple(round(v, 2) for v in off),
                                                       tuple(round(v, 2) for v in new), z, z_new, "  [СУХОЙ]" if dry else ""))
        if dry:
            fixed += 1
            continue
        T._ensure_backup(path, set_name=BACKUP_SET)
        for k, off, new, z, z_new in plan:
            s = pieces[k].get_editor_property("slot")
            lay = s.get_editor_property("layout_data")
            lay.set_editor_property("offsets", unreal.Margin(new[0], new[1], new[2], new[3]))
            s.set_editor_property("layout_data", lay)
            s.set_editor_property("z_order", z_new)
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % path)
        fixed += 1
    log("исправлено рамок %d, уже готово %d, осмотреть %d%s" % (fixed, done, inspect, "  [СУХОЙ ПРОГОН]" if dry else ""))
    log("ШОВ-РЯДОВ: ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[шов-рядов] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
