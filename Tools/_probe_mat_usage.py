# Copyright (c) 2026 @sitxovski (Discord)
# _probe_mat_usage.py — флаги ИСПОЛЬЗОВАНИЯ материалов: рабочий M_AsdaObjectLit vs новый M_AsdaMeshGlowTint.
#   Материал без нужного usage-флага на скелетал-меше подменяется движком на DefaultMaterial (СЕРЫЙ).
import unreal
EAL = unreal.EditorAssetLibrary
FLAGS = ["used_with_skeletal_mesh", "used_with_static_lighting", "used_with_morph_targets",
         "used_with_particle_sprites", "used_with_instanced_static_meshes", "used_with_nanite",
         "used_with_spline_meshes", "used_with_geometry_cache", "used_with_water"]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for p in ["/Game/Materials/M_AsdaObjectLit", "/Game/Materials/M_AsdaObject", "/Game/Materials/M_AsdaMeshGlowTint"]:
        m = EAL.load_asset(p)
        if not m:
            unreal.log("[usage] НЕТ %s" % p); continue
        vals = []
        for f in FLAGS:
            try:
                vals.append("%s=%s" % (f, m.get_editor_property(f)))
            except Exception:
                pass
        unreal.log("[usage] %s: %s" % (p, " ".join(vals)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
