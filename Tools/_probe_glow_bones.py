# Copyright (c) 2026 @sitxovski (Discord)
# _probe_glow_bones.py — есть ли в скелете персонажа кости, на которые вешается свечение/оружие.
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

WANT = ("8_a1", "_8_a1", "_7_a1", "2_a1", "_2_a1")
CAND = [
    "/Game/AsdaAssets/Models/Skeletal/Char/ch1_m/SkeletalMeshes/ch1_m",
    "/Game/AsdaAssets/Models/Skeletal/Char/ch1_f/SkeletalMeshes/ch1_f",
    "/Game/AsdaAssets/Models/Skeletal/Char/ch4_m/SkeletalMeshes/ch4_m",
]


def log(m): unreal.log_warning("[bones] %s" % m)


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = ar.get_assets_by_path("/Game/AsdaAssets/Models/Skeletal/Char", recursive=True)
    skels = [a for a in assets if str(a.asset_class_path.asset_name) == "SkeletalMesh"]
    log("скелетных мешей персонажа найдено: %d" % len(skels))
    shown = 0
    for a in skels:
        sm = a.get_asset()
        if not sm: continue
        sk = sm.skeleton
        if not sk: continue
        names = [str(b) for b in sk.get_editor_property("bone_tree")] if False else None
        # надёжнее — через SkeletalMesh bone names
        bn = [str(n) for n in unreal.SkeletalMeshComponent  # placeholder
              ] if False else None
        comp = unreal.SkeletalMeshComponent()
        comp.set_skeletal_mesh_asset(sm)
        allb = [str(x) for x in comp.get_all_socket_names()]
        # кости через bone index API
        bones = []
        i = 0
        while True:
            nm = comp.get_bone_name(i)
            if not nm or str(nm) == "None": break
            bones.append(str(nm)); i += 1
            if i > 400: break
        hit = {w: (w in bones) for w in WANT}
        if shown < 4:
            log("%s: костей=%d | %s" % (a.asset_name, len(bones), hit))
            near = [b for b in bones if "a1" in b.lower()]
            log("   кости с 'a1': %s" % (", ".join(near[:20]) if near else "НЕТ"))
            shown += 1


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
