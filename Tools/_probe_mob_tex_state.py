# Copyright (c) 2026 @sitxovski (Discord)
# _probe_mob_tex_state.py — СНИМОК текущего состояния материалов мобов в Content (для сверки с эталоном).
#   Для каждой модели /Game/AsdaAssets/Models/Mob/<m>/StaticMeshes/<m> и /MobSkel/<m>/SkeletalMeshes/<m>
#   печатает СЛОТЫ по порядку: имя слота, MIC, значение параметра Tex.
#   Вывод → Reports/_mob_tex_state.tsv  (TSV: kind, model, slotIdx, slotName, mic, tex)
import unreal, os

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\_mob_tex_state.tsv"

def tex_of(mi):
    if not mi or not isinstance(mi, unreal.MaterialInstanceConstant):
        return ""
    try:
        t = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
        return t.get_name() if t else ""
    except Exception:
        return ""

rows = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global mi
    for kind, root, sub in (("Mob", "/Game/AsdaAssets/Models/Mob", "StaticMeshes"),
                            ("MobSkel", "/Game/AsdaAssets/Models/MobSkel", "SkeletalMeshes")):
        if not EAL.does_directory_exist(root):
            continue
        for d in EAL.list_assets(root, recursive=False, include_folder=True):
            model = d.rstrip("/").rsplit("/", 1)[-1]
            path = "%s/%s/%s/%s" % (root, model, sub, model)
            a = unreal.load_asset(path)
            if not a:
                continue
            if kind == "Mob":
                mats = a.get_editor_property("static_materials")
                for i, m in enumerate(mats):
                    mi = m.get_editor_property("material_interface")
                    rows.append((kind, model, i, str(m.get_editor_property("material_slot_name")),
                                 mi.get_name() if mi else "", tex_of(mi)))
            else:
                mats = a.get_editor_property("materials")
                for i, m in enumerate(mats):
                    mi = m.get_editor_property("material_interface")
                    rows.append((kind, model, i, str(m.get_editor_property("material_slot_name")),
                                 mi.get_name() if mi else "", tex_of(mi)))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("kind\tmodel\tslot\tslotName\tmic\ttex\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")
    unreal.log("[probe_mob_tex] строк: %d → %s" % (len(rows), OUT))
    unreal.log("=== MOB TEX STATE DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
