# Copyright (c) 2026 @sitxovski (Discord)
# asda_dump_mesh_textures.py — РАЗВЕДКА для превью «нормаль из альбедо»: какой меш какую текстуру носит.
#   Проходит по каталогам мешей, у каждого берёт материал слота 0 и читает параметр `Tex`
#   (мастера M_AsdaObjectLit / M_AsdaCharLit кладут альбедо именно в него, см. asda_build_materials_lit.py).
#   Пишет TSV: тип | меш | материал | текстура | размер.
#   Ничего не создаёт и не меняет — только чтение. Запуск: run_headless.bat asda_dump_mesh_textures.py
import os

# ★ Голый main(): при импорте модуль НЕ делает ничего (закон tools-no-import-side-effects).
DIRS = [
    ("объект",  "/Game/Map/alpen/Models"),
    ("объект",  "/Game/Map/alpen_dungeon/Models"),
    ("персонаж", "/Game/AsdaAssets/Models/Char"),
    ("моб",     "/Game/AsdaAssets/Models/Mob"),
    ("моб",     "/Game/AsdaAssets/Models/MobSkel"),
    ("npc",     "/Game/AsdaAssets/Models/NPC"),
]
OUT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/_out/mesh_textures.tsv"
PER_DIR = int(os.environ.get("ASDA_PER_DIR", "120"))   # сколько ассетов смотреть в каждом каталоге


def tex_of_material(unreal, mat):
    """Путь текстуры-альбедо материала: параметр `Tex` у MIC, иначе первая текстура графа."""
    if mat is None:
        return ""
    mel = unreal.MaterialEditingLibrary
    if isinstance(mat, unreal.MaterialInstanceConstant):
        try:
            t = mel.get_material_instance_texture_parameter_value(mat, "Tex")
            if t:
                return t.get_path_name()
        except Exception:
            pass
    try:
        used = mat.get_used_textures()
        if used:
            return used[0].get_path_name()
    except Exception:
        pass
    return ""


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    rows = []
    for kind, d in DIRS:
        if not eal.does_directory_exist(d):
            unreal.log_warning("нет каталога %s" % d)
            continue
        assets = eal.list_assets(d, recursive=True, include_folder=False)
        seen = 0
        for a in assets:
            if seen >= PER_DIR:
                break
            obj = eal.load_asset(a)
            if not isinstance(obj, (unreal.StaticMesh, unreal.SkeletalMesh)):
                continue
            seen += 1
            # материал слота 0
            mat = None
            try:
                if isinstance(obj, unreal.StaticMesh):
                    mats = obj.get_editor_property("static_materials")
                    if mats:
                        mat = mats[0].get_editor_property("material_interface")
                else:
                    mats = obj.get_editor_property("materials")
                    if mats:
                        mat = mats[0].get_editor_property("material_interface")
            except Exception:
                pass
            tex_path = tex_of_material(unreal, mat)
            size = ""
            if tex_path:
                t = eal.load_asset(tex_path)
                if t:
                    try:
                        size = "%dx%d" % (t.blueprint_get_size_x(), t.blueprint_get_size_y())
                    except Exception:
                        size = ""
            rows.append("\t".join([
                kind, a,
                mat.get_path_name() if mat else "",
                tex_path, size,
            ]))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("kind\tmesh\tmaterial\ttexture\tsize\n")
        f.write("\n".join(rows))
    unreal.log("=== dump_mesh_textures: %d строк -> %s ===" % (len(rows), OUT))


if __name__ == "__main__":
    main()
