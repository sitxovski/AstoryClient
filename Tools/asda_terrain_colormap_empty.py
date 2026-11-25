# Copyright (c) 2026 @sitxovski (Discord)
# asda_terrain_colormap_empty.py — снять поплиточный Colormap террейна на выбранных картах (решение владельца 28.10.2026).
#
# ★ ЗАЧЕМ. Colormap патча (color_<off>, из TerrainColor.TEX оригинала) несёт запечённые «свечения» — пятна света под
#   фонарями/свечами. В порте свет живой (точечные светильники владельца), запечка стала лишней. Владелец: на
#   Palace of Queen, Castle of Chess (и их t_), Castle of Chaos заменить Colormap всех MIT_patch_* на прозрачную текстуру.
# ★ ЧТО ПРОИСХОДИТ В МАТЕРИАЛЕ. M_AsdaTerrainLit: BaseColor = lerp(сплат Detail0/Detail1, Colormap.RGB, Colormap.A) · тинт
#   (Tools/asda_fix_terrainlit_vertexcolor.py, узел g2). Colormap с альфой 0 = наложение выключено, остаются detail-тайлы;
#   RGB прозрачной текстуры ни на что не влияет.
# ★ ЧТО МЕНЯЕТСЯ. Только параметр Colormap в MaterialInstanceConstant /Game/Map/<карта>/Materials/MIT_patch_*. Уровни (.umap),
#   прочие параметры MIT, мастер и сами color_-текстуры не трогаются. Текстура-пустышка импортируется один раз.
#
# env: ASDA_MAPS (через запятую), ASDA_PNG (исходный PNG), ASDA_APPLY=1 (запись; иначе сухой прогон),
#      ASDA_BACKUP (корень бэкапа, обязателен при записи), ASDA_REPORT (json).
# Маркер конца: «ЗАВЕРШЕНО asda_terrain_colormap_empty».
import os, sys, json, shutil
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
CONTENT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content"
TEX_DIR = "/Game/AsdaAssets/Textures/Util"
TEX_NAME = "T_TerrainColormapEmpty"
MASTER = "/Game/Materials/M_AsdaTerrainLit"
MAPS = [m for m in os.environ.get("ASDA_MAPS", "").split(",") if m]
PNG = os.environ.get("ASDA_PNG", "")
APPLY = os.environ.get("ASDA_APPLY", "0") == "1"
BACKUP = os.environ.get("ASDA_BACKUP", "")
REPORT = os.environ.get("ASDA_REPORT", "")
# настройки текстуры, переносимые с существующего color_-тайла (чтобы сэмплер мастера принял её так же)
TEX_PROPS = ("srgb", "compression_settings", "lod_group", "address_x", "address_y", "mip_gen_settings", "filter")


def log(m):
    unreal.log("[colormap] " + str(m))


def _pkg_file(path):
    """/Game/... → путь .uasset на диске."""
    return os.path.join(CONTENT, path[len("/Game/"):] + ".uasset")


def _backup(path):
    src = _pkg_file(path)
    dst = os.path.join(BACKUP, "AstoryClient/Content", path[len("/Game/"):] + ".uasset")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if not os.path.exists(dst):
        shutil.copy2(src, dst)
    return os.path.getsize(src) == os.path.getsize(dst)


def empty_texture(template):
    """Пустышка T_TerrainColormapEmpty: импорт PNG владельца (один раз) + настройки как у color_-тайла."""
    path = TEX_DIR + "/" + TEX_NAME
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path), False
    if not APPLY:
        return None, True
    t = unreal.AssetImportTask()
    t.filename = PNG
    t.destination_path = TEX_DIR
    t.destination_name = TEX_NAME
    t.automated = True
    t.replace_existing = False
    t.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = EAL.load_asset(path)
    if tex is None:
        raise RuntimeError("импорт %s не удался" % PNG)
    if template is not None:
        for p in TEX_PROPS:
            try:
                tex.set_editor_property(p, template.get_editor_property(p))
            except Exception as e:
                log("свойство %s не перенесено: %s" % (p, e))
    EAL.save_loaded_asset(tex)
    unreal.log("ASDA_WRITES_ASSET: " + path)
    return tex, True


def main():
    if APPLY and not BACKUP:
        log("ASDA_APPLY=1 без ASDA_BACKUP — отказ"); return
    rep = {"maps": {}, "texture": None}
    plan = []
    template = None
    for m in MAPS:
        a = _MAPS.map_asset_name(m)
        mdir = "/Game/Map/%s/Materials" % a
        r = {"mit": 0, "to_change": 0, "already": 0, "no_colormap": 0, "foreign_parent": [], "colormaps": []}
        for p in sorted(EAL.list_assets(mdir, recursive=False, include_folder=False)):
            name = p.split("/")[-1].split(".")[0]
            if not name.startswith("MIT_patch_"):
                continue
            mic = EAL.load_asset(p)
            if not isinstance(mic, unreal.MaterialInstanceConstant):
                continue
            r["mit"] += 1
            par = mic.get_editor_property("parent")
            if par is None or par.get_path_name().split(".")[0] != MASTER:
                r["foreign_parent"].append([name, par.get_path_name() if par else None])
            cm = MEL.get_material_instance_texture_parameter_value(mic, "Colormap")
            if cm is None:
                r["no_colormap"] += 1; continue
            if cm.get_name() == TEX_NAME:
                r["already"] += 1; continue
            if template is None:
                template = cm
            r["colormaps"].append([name, cm.get_path_name()])
            r["to_change"] += 1
            plan.append((m, p.split(".")[0], mic))
        rep["maps"][m] = r
        log("%s: MIT %d, сменить %d, уже пусто %d, без Colormap %d, чужой родитель %s"
            % (m, r["mit"], r["to_change"], r["already"], r["no_colormap"], r["foreign_parent"]))
    tex, created = empty_texture(template)
    rep["texture"] = {"path": TEX_DIR + "/" + TEX_NAME, "created": created,
                      "props": {p: str(tex.get_editor_property(p)) for p in TEX_PROPS} if tex else None,
                      "template": template.get_path_name() if template else None}
    log("текстура-пустышка: %s" % rep["texture"])
    if APPLY and tex is not None:
        ok = 0
        for m, path, mic in plan:
            if not _backup(path):
                log("бэкап %s не совпал по размеру — пропуск" % path); continue
            MEL.set_material_instance_texture_parameter_value(mic, "Colormap", tex)
            if MEL.get_material_instance_texture_parameter_value(mic, "Colormap") != tex:
                log("%s: параметр не встал" % path); continue
            EAL.save_loaded_asset(mic)
            ok += 1
        rep["changed"] = ok
        log("записано MIT: %d из %d" % (ok, len(plan)))
    if REPORT:
        with open(REPORT, "w", encoding="utf-8") as f:
            json.dump(rep, f, ensure_ascii=False, indent=1)
    unreal.log("ЗАВЕРШЕНО asda_terrain_colormap_empty (%s)" % ("ЗАПИСЬ" if APPLY else "сухой прогон"))


# ★ ЗАКОН [[tools-no-import-side-effects]]
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
