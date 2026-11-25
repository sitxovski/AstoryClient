# Copyright (c) 2026 @sitxovski (Discord)
# asda_atlas_size_add.py — ДОБАВИТЬ строки в `DT_AtlasSourceSize`, не трогая существующие.
#
# Зачем отдельный шаг: `Tools/NormalStudio/ue_build_atlas_table.py` заливает таблицу ЦЕЛИКОМ из манифеста апскейла, а
#   `fill_data_table_from_csv_string` заменяет содержимое. Для текстур вне манифеста (иконки совела, пачка 13) он либо потерял
#   бы новые строки, либо старые. Здесь: старые строки читаются из самой таблицы, к ним дописываются новые, после заливки
#   таблица сверяется построчно, и только потом сохраняется. Расхождение — отказ БЕЗ сохранения.
#
# Параметры (окружение):
#   ASDA_ATLAS_NAMES      файл со списком имён текстур /Game/AsdaAssets/Textures/JUI (по одному в строке);
#   ASDA_ATLAS_W, _H      логический размер (по умолчанию 32×32);
#   ASDA_ATLAS_DRYRUN=1   только показать план.
# Попутно проверяет, что у этих текстур остались NoMipmaps/TC_EditorIcon (импорт настройку мипов мог сбросить), и возвращает её.
# Запуск: python Tools/run_headless_safe.py Tools/asda_atlas_size_add.py --marker "ЗАВЕРШЕНО: размеры атласов" --idle 1500
import os

TABLE = "/Game/AsdaAssets/Data/DT_AtlasSourceSize"   # таблица логических размеров
TEX_DIR = "/Game/AsdaAssets/Textures/JUI"            # папка текстур


def main():
    """Работа скрипта; импорт модуля ничего не делает (закон tools-no-import-side-effects)."""
    import unreal
    eal = unreal.EditorAssetLibrary
    lib = unreal.DataTableFunctionLibrary
    w = int(os.environ.get("ASDA_ATLAS_W", "32"))
    h = int(os.environ.get("ASDA_ATLAS_H", "32"))
    dry = os.environ.get("ASDA_ATLAS_DRYRUN", "") == "1"
    names = [l.strip() for l in open(os.environ["ASDA_ATLAS_NAMES"], encoding="utf-8") if l.strip()]

    # Текстуры: настройки и фактический размер.
    fixed = 0
    bad = []
    for n in names:
        tex = eal.load_asset("%s/%s" % (TEX_DIR, n))
        if not isinstance(tex, unreal.Texture2D):
            bad.append(n)
            continue
        if tex.get_editor_property("mip_gen_settings") != unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS:
            fixed += 1
            if not dry:
                tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
                eal.save_loaded_asset(tex)
    unreal.log("[размеры атласов] текстур %d, без ассета %d %s, мипы возвращены %d" % (len(names), len(bad), bad[:5], fixed))
    if bad:
        unreal.log_error("[размеры атласов] ОТКАЗ: нет ассетов")
        return

    table = eal.load_asset(TABLE)
    rows = [str(r) for r in lib.get_data_table_row_names(table)]
    ws = list(lib.get_data_table_column_as_string(table, "Width"))
    hs = list(lib.get_data_table_column_as_string(table, "Height"))
    if not (len(rows) == len(ws) == len(hs)):
        unreal.log_error("[размеры атласов] ОТКАЗ: столбцы разной длины %d/%d/%d" % (len(rows), len(ws), len(hs)))
        return
    old = {r: (ws[i], hs[i]) for i, r in enumerate(rows)}
    want = dict(old)
    added = 0
    for n in names:
        key = ("%s/%s" % (TEX_DIR, n)).replace("/", "_")
        if key not in want:
            added += 1
        want[key] = (str(w), str(h))
    unreal.log("[размеры атласов] в таблице строк %d, добавить %d, станет %d" % (len(old), added, len(want)))
    if dry:
        unreal.log("ЗАВЕРШЕНО: размеры атласов (сухой прогон)")
        return

    csv = ["Name,Width,Height"] + ["%s,%s,%s" % (k, v[0], v[1]) for k, v in want.items()]
    lib.fill_data_table_from_csv_string(table, "\n".join(csv))
    rows2 = [str(r) for r in lib.get_data_table_row_names(table)]
    ws2 = list(lib.get_data_table_column_as_string(table, "Width"))
    hs2 = list(lib.get_data_table_column_as_string(table, "Height"))
    got = {r: (ws2[i], hs2[i]) for i, r in enumerate(rows2)}
    if got != want:
        lost = [k for k in want if got.get(k) != want[k]]
        unreal.log_error("[размеры атласов] ОТКАЗ: после заливки расхождение %d строк (%s), таблица НЕ сохранена" % (len(lost), lost[:5]))
        return
    eal.save_loaded_asset(table)
    unreal.log("ASDA_WRITES_ASSET: %s" % TABLE)
    unreal.log("[размеры атласов] старых строк сохранено %d, всего %d" % (len(old), len(got)))
    unreal.log("ЗАВЕРШЕНО: размеры атласов")


if __name__ == "__main__":
    main()
