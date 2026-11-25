# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_mob_namecolor_dt.py — DT_MobNameColors: ЦВЕТ ИМЕНИ МОБА ПО РАЗНИЦЕ УРОВНЕЙ.
#
# ★ ЭТАЛОН (реверс 2026-08-03). Одна лесенка в трёх местах клиента:
#     game_main.c:4544 (hover-плашка), game_main.c:4763 (плашка выбранной цели),
#     ui_core.c:75770 (hudShowMobTargetInfo — окно инфо о цели).
#   levelDiff = уровень ИГРОКА − уровень МОБА (уровень моба: Mob list.BIN +0x98 → сущность +0x1DC).
#   Ветка — по байту агрессии (Mob list.BIN +0xFC → сущность +6979):
#     1/4/5 → лесенка; 2 → 0xFFE24ACB; 3 → 0xFFC3A868; иное → цвет не трогается.
#   Лесенка (ARGB эталона, sRGB):
#     ≥7 → 0xFF808080 · 2..6 → 0xFF00FF00 · −1..1 → 0xFFFFFFFF · −5..−2 → 0xFFFFA200 · ≤−6 → 0xFFFF0000
#   ⚠ В ОКНЕ ИНФО О ЦЕЛИ верхняя ступень — 0xFFAAAAAA (−5592406), а не 0xFF808080. Над-головной
#     плашки это не касается (там −8355712), поэтому в таблице плашки лежит 0x808080.
#   ⚠ Красится ТОЛЬКО ТЕКСТ ИМЕНИ; полоса HP и фон плашки в эталоне не красятся.
#   ⚠ Цвета хранятся в sRGB (как в клиенте); перевод в linear делает рантайм — закон
#     [[ui-srgb-gamma-rule]] (FLinearColor(FColor)).
#
# Заполняет /Game/AsdaAssets/Data/Mob/DT_MobNameColors (RowStruct FAsdaMobNameColorRow).
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_build_mob_namecolor_dt.py
import unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

OUT_PKG, OUT_NAME = "/Game/AsdaAssets/Data/Mob", "DT_MobNameColors"

# (RowName, AggressionOverride, MinDiff, MaxDiff, R, G, B, эталонная ARGB-константа, подпись)
ROWS = [
    ("Diff_Ge7",   -1,      7,  32767, 0x80, 0x80, 0x80, -8355712,
     "diff>=7 — серый (моб не даёт опыта)"),
    ("Diff_2_6",   -1,      2,      6, 0x00, 0xFF, 0x00, -16711936,
     "diff 2..6 — зелёный (моб слабее)"),
    ("Diff_M1_P1", -1,     -1,      1, 0xFF, 0xFF, 0xFF, -1,
     "diff -1..1 — белый (в т.ч. РАВЕНСТВО уровней)"),
    ("Diff_M5_M2", -1,     -5,     -2, 0xFF, 0xA2, 0x00, -24064,
     "diff -5..-2 — оранжевый (моб сильнее)"),
    ("Diff_LeM6",  -1, -32768,     -6, 0xFF, 0x00, 0x00, -65536,
     "diff<=-6 — красный (моб намного сильнее)"),
    ("Aggr_2",      2, -32768,  32767, 0xE2, 0x4A, 0xCB, -1946933,
     "агрессия 2 — фиксированный розово-сиреневый (разница уровней не участвует)"),
    ("Aggr_3",      3, -32768,  32767, 0xC3, 0xA8, 0x68, -3954584,
     "агрессия 3 — фиксированный песочный (боссы/именные; разница уровней не участвует)"),
]


def log(m):
    unreal.log_warning("[mobcolor] %s" % m)


def main():
    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    if not dt:
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", unreal.load_object(None, "/Script/AsdaReborn.AsdaMobNameColorRow"))
        dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(OUT_NAME, OUT_PKG, unreal.DataTable, factory)
    if not dt:
        log("DT не создан")
        return
    head = "Row,AggressionOverride,MinDiff,MaxDiff,NameColor,EtalonArgb,Note"
    lines = []
    for nm, ov, lo, hi, r, g, b, argb, note in ROWS:
        # FColor в CSV: "(R=..,G=..,B=..,A=..)"
        col = '"(R=%d,G=%d,B=%d,A=255)"' % (r, g, b)
        lines.append('%s,%d,%d,%d,%s,%d,"%s"' % (nm, ov, lo, hi, col, argb, note))
    ok = _DTC.fill_and_save(unreal, dt, head + "\n" + "\n".join(lines), log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_MobNameColors: строк %d, заполнение=%s" % (len(ROWS), ok))
    for nm, ov, lo, hi, r, g, b, argb, note in ROWS:
        log("  %-12s aggrOv=%-3d diff[%6d..%6d] #%02X%02X%02X (%d) — %s" % (nm, ov, lo, hi, r, g, b, argb, note))


if __name__ == "__main__":   # ★ без побочных эффектов при импорте (правило проекта)
    main()
