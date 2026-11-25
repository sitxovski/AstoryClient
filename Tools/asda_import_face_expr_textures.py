# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_face_expr_textures.py — 07.11.2026, баг «эмоции и лицо». Импорт 44 сдвинутых копий атласа лица.
#   Эталон: `meshSetPartBlendFactorsByMode@0x9E1DA0` сдвигает UV лица: режим 1 (+0.5, 0) — глаза закрыты, режим 2 (0, +0.5) — злое.
#   Сдвиг UV в порте заменён сменой текстуры: UV лица лежат в левой верхней четверти, копия `_e1` / `_e2` сдвинута на 256 точек,
#   и нужная четверть оказывается на месте. Так не меняется общий мастер M_AsdaCharLit, и смена работает на любом мастере слота
#   («душа», появление). Настройки копируются с исходной текстуры лица.
# PNG готовит сдвиг из RawAssets/Textures/Data/Character/Texture в RawAssets/Textures/Data/Character/FaceExpr.
# Запуск (скрыто): python Tools/run_headless_safe.py Tools/asda_import_face_expr_textures.py --marker FACE-EXPR-FINISHED
import os
import unreal

DEST = "/Game/AsdaAssets/Textures/Characters/Texture"   # папка текстур персонажа
PROPS = ("compression_settings", "srgb", "lod_group", "mip_gen_settings", "never_stream", "filter", "address_x", "address_y")   # что копируем


def log(msg):
    """Строка журнала с меткой скрипта."""
    unreal.log_warning("[face-expr] %s" % msg)


def main():
    """Импортирует копии и переносит на них настройки исходной текстуры."""
    src_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "RawAssets/Textures/Data/Character/FaceExpr")
    ok = bad = 0
    for sex in "mf":
        for idx in range(11):
            base = "ch1_%s_00_face001_%02d_00_0" % (sex, idx)
            src = unreal.EditorAssetLibrary.load_asset("%s/%s" % (DEST, base))
            if not src:
                log("нет исходной текстуры %s" % base); bad += 1
                continue
            for suf in ("_e1", "_e2"):
                t = unreal.AssetImportTask()
                t.filename = os.path.join(src_dir, base + suf + ".png")
                t.destination_path = DEST
                t.destination_name = base + suf
                t.automated = True
                t.replace_existing = True
                t.save = False
                unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
                new = unreal.EditorAssetLibrary.load_asset("%s/%s" % (DEST, base + suf))
                if not new:
                    log("не импортировалась %s%s" % (base, suf)); bad += 1
                    continue
                for p in PROPS:
                    new.set_editor_property(p, src.get_editor_property(p))
                unreal.EditorAssetLibrary.save_loaded_asset(new, False)
                ok += 1
    log("ИТОГО: импорт %d, ошибок %d" % (ok, bad))
    log("FACE-EXPR-FINISHED")


# Закон tools-no-import-side-effects: при импорте модуля main() не запускается.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
