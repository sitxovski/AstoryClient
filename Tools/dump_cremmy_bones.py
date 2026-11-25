# Copyright (c) 2026 @sitxovski (Discord)
import unreal

CANDIDATES = [
    "/Game/AsdaAssets/Models/Char/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview",
    "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview",
    "/Game/AsdaAssets/Models/Char/ch1_f_cremmy_preview/SkeletalMeshes/ch1_f_cremmy_preview",
]

def dump(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log_warning("MISSING: %s" % path)
        return
    mesh = unreal.load_asset(path)
    if mesh is None:
        unreal.log_warning("LOAD FAIL: %s" % path)
        return
    comp = unreal.SkeletalMeshComponent()
    try:
        comp.set_skeletal_mesh_asset(mesh)
    except Exception:
        comp.set_skeletal_mesh(mesh)
    n = comp.get_num_bones()
    unreal.log("=== BONES %s  count=%d ===" % (path, n))
    for i in range(n):
        bn = comp.get_bone_name(i)
        unreal.log("BONE[%03d] %s" % (i, bn))
    # highlight anchor candidates
    names = [str(comp.get_bone_name(i)) for i in range(n)]
    hits = [x for x in names if any(k in x.upper() for k in ["DUMMY","HAND","WRIST","GRIP","WEAP","_R_","R_HAND"])]
    unreal.log("=== ANCHOR CANDIDATES %s: %s ===" % (path, hits))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for p in CANDIDATES:
        dump(p)
    unreal.log("=== DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
