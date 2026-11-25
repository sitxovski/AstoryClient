# Copyright (c) 2026 @sitxovski (Discord)
# _probe_stow_space.py — ref-поза костей 2_a1/8_a1/_8_a1 + габариты мешей оружия.
# Нужно, чтобы численно проверить масштаб эталонных поправок «оружие за спиной» (client units x100).
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

def log(m): unreal.log_warning("[stow] %s" % m)

WANT = ("2_a1", "_2_a1", "8_a1", "_8_a1", "_7_a1", "M")

def dump_skel(path):
    sm = unreal.load_asset(path)
    if not sm:
        log("НЕТ ассета %s" % path); return
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(sm)
    bones = []
    i = 0
    while True:
        nm = comp.get_bone_name(i)
        if not nm or str(nm) == "None": break
        bones.append(str(nm)); i += 1
        if i > 400: break
    log("%s: костей=%d" % (path.split('/')[-1], len(bones)))
    b = sm.get_bounds()
    log("   bounds origin=%s extent=%s (сырые ед. скелета)" % (b.origin, b.box_extent))
    ref = sm.skeleton
    for w in WANT:
        if w not in bones:
            log("   кость %-6s: НЕТ" % w); continue
        idx = bones.index(w)
        # component-space ref-поза
        t = comp.get_bone_transform(idx, unreal.Transform())
        log("   кость %-6s idx=%3d loc=%s rot=%s" % (w, idx, t.translation, t.rotation.rotator()))


# габариты мешей оружия (сырые единицы StaticMesh)
ar = unreal.AssetRegistryHelpers.get_asset_registry()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global nm, sm
    for p in ("/Game/AsdaAssets/Models/Skeletal/Char/ch1_m/SkeletalMeshes/ch1_m",
              "/Game/AsdaAssets/Models/Skeletal/Char/ch4_m/SkeletalMeshes/ch4_m"):
        dump_skel(p)
    assets = ar.get_assets_by_path("/Game/AsdaAssets/Models/Char", recursive=True)
    n = 0
    for a in assets:
        if str(a.asset_class_path.asset_name) != "StaticMesh": continue
        nm = str(a.asset_name)
        if not nm.startswith("ch1_b_"): continue
        sm = a.get_asset()
        if not sm: continue
        bb = sm.get_bounding_box()
        ext = bb.max - bb.min
        log("оружие %-24s min=%s max=%s размер=(%.0f,%.0f,%.0f)" % (nm, bb.min, bb.max, ext.x, ext.y, ext.z))
        n += 1
        if n >= 12: break
    log("готово")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
