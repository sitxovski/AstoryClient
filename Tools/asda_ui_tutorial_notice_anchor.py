# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_tutorial_notice_anchor.py — ★★ ЗНАЧОК «?» ОБУЧЕНИЯ 69800 ВЫСОКО НАД ПАНЕЛЬЮ НА ВЫСОКОМ ЭКРАНЕ (17.11.2026, снимок
#   владельца alpha-8.1.0). [ДЕФЕКТ ОРИГИНАЛА — ИСПРАВЛЕН] — правка АССЕТА `WBP_Tutorial_Notice_WIN`, кода не касается
#   (закон no-runtime-widget-fixes).
#
#   ОРИГИНАЛ. `.JUI` gamemain_jui.ini:357581 `Tutorial_Notice_WIN` 69800: Size 42×42, AlignXPos="2 -504 670" (x = W − 504),
#   AlignYPos="4 -504 670" — режим 4 = ВЕРХ: y = 670 от ВЕРХА экрана (`CControl::CalcLeftTop@0x12DFBB0`, разбор
#   RE/docs/_archive/72_login_screen.md §4.4). Код окно не двигает (`uiSelectFirstValidTab@0xB79370` только показывает,
#   клик 69801 — `tutorialPanelShowAndRefresh` + скрыть). Панель, у которой значок стоит, привязана к НИЗУ: кнопки меню
#   `SYSTEMMAIN_NEW_WIN` 73400 AlignYPos="8 -480 -94" (y = H − 94, кнопки H − 81), квикслоты `QUICK_NEW_WIN` 73000 "8 … -60".
#   На 1024×768 значок 670…712 = H − 98…H − 56 — вплотную к ряду меню; выше 768 он остаётся на 670, а панель уезжает вниз
#   (1440: разрыв ≈ 670 px — снимок владельца). Во всём gamemain это единственное корневое окно с режимом «верх» и отступом
#   в нижней полосе экрана (скан 17.11: ещё только `Mine_Time_WIN` 76200, y = 500 у правого края — к панели не относится).
#
#   ПРАВКА. Каждый слот корня (все куски значка — точечный анкер Y = 0) переводится на анкер Y = 1 с позицией y − 768:
#   значок держит расстояние до низа экрана, как панель. На 1024×768 кадр не меняется ни на пиксель; X не трогается.
#   БЕЗОПАСНОСТЬ: трогаются только точечные слоты с анкером Y = 0 и top в нижней полосе (≥ 600); уже переведённые
#   (анкер Y = 1) — «готово» (идемпотентно); иной вид — «осмотреть», ассет не пишется. Бэкап до записи (набор
#   `tutorial_notice_anchor`).
#
# Запуск (редактор и игра ЗАКРЫТЫ):
#   python Tools\run_headless_safe.py Tools\asda_ui_tutorial_notice_anchor.py --marker "ЗНАЧОК-ОБУЧЕНИЯ: ГОТОВО"
#   ASDA_TUTNOTICE_DRYRUN=1 — сухой прогон.
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

DRYRUN_ENV = "ASDA_TUTNOTICE_DRYRUN"   # ключ сухого прогона (таблица DRY_ENV раннера пост-импорта)
BACKUP_SET = "tutorial_notice_anchor"  # набор бэкапа
ASSET = "/Game/AsdaAssets/UI/Player_UI/Tutorial/WBP_Tutorial_Notice_WIN"
BASE_H = 768.0                         # высота корня .JUI
MIN_TOP = 600.0                        # нижняя полоса экрана: только такие слоты — наш случай
EPS = 1e-4


def log(m):
    """Строка журнала шага."""
    if unreal:
        unreal.log("[значок-обучения] %s" % m)
    else:
        print("[значок-обучения] %s" % m)


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


def main():
    """Точка входа."""
    dry = _is_dry()
    B = unreal.AsdaUmgBuilder
    EAL = unreal.EditorAssetLibrary
    bp = EAL.load_asset(ASSET)
    if not bp:
        raise RuntimeError("нет ассета %s" % ASSET)
    root = B.get_root_canvas(bp)
    plan, done, bad = [], 0, []
    for i in range(root.get_children_count()):
        w = root.get_child_at(i)
        if not w:
            continue
        _, _, anc, off = _lay(w)
        point_y = abs(anc[1] - anc[3]) < EPS
        if point_y and abs(anc[1] - 1.0) < EPS and off[1] < 0:
            done += 1
            continue
        if point_y and abs(anc[1]) < EPS and off[1] >= MIN_TOP:
            plan.append((w, anc, off))
            continue
        bad.append("%s анкеры %s поля %s" % (w.get_name(), anc, off))
    if bad:
        for b in bad:
            log("вид не ожидаемый: %s — осмотреть" % b)
        raise RuntimeError("слотов «осмотреть»: %d" % len(bad))
    for w, anc, off in plan:
        log("%s: анкер Y %.0f top %.1f → анкер Y 1 top %.1f%s"
            % (w.get_name(), anc[1], off[1], off[1] - BASE_H, "  [СУХОЙ]" if dry else ""))
    if not plan:
        log("готово: все %d слотов уже от низа экрана" % done)
        log("ЗНАЧОК-ОБУЧЕНИЯ: ГОТОВО")
        return
    if not dry:
        import asda_ui_text_block_align as T
        T._ensure_backup(ASSET, set_name=BACKUP_SET)
        for w, anc, off in plan:
            s, lay, _a, o = _lay(w)
            a = lay.get_editor_property("anchors")
            a.set_editor_property("minimum", unreal.Vector2D(anc[0], 1.0))
            a.set_editor_property("maximum", unreal.Vector2D(anc[2], 1.0))
            lay.set_editor_property("anchors", a)
            lay.set_editor_property("offsets", unreal.Margin(o[0], o[1] - BASE_H, o[2], o[3]))
            s.set_editor_property("layout_data", lay)
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % ASSET)
    log("слотов переведено %d, уже готово %d%s" % (len(plan), done, "  [СУХОЙ ПРОГОН]" if dry else ""))
    log("ЗНАЧОК-ОБУЧЕНИЯ: ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[значок-обучения] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
