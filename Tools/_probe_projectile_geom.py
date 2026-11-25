# Copyright (c) 2026 @sitxovski (Discord)
# _probe_projectile_geom.py — ЧИСЛЕННАЯ проверка геометрии снаряда БЕЗ игры.
#   Спавним AAsdaProjectileActor в редакторском мире, зовём Launch(from→to) и меряем:
#     • длину меша в МИРЕ (должна быть ≈73.7 юнита — как в .jbm, а не 7510 без масштаба);
#     • совпадение продольной оси меша с направлением полёта (косинус ≈ 1);
#     • стартовую точку и расчётную длительность полёта (эталон dist/2500 uu/с).
#   Визуал шлейфа (billboard-квады/кольца) headless НЕ проверяет — только asda.VerifyProjectile в игре.
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal, math

def log(m): unreal.log_warning("[снаряд-геом] %s" % m)

W = unreal.EditorLevelLibrary.get_editor_world()


CASES = [
    ((0.0, 0.0, 150.0), (1000.0,    0.0, 150.0)),   # строго по +X
    ((0.0, 0.0, 150.0), (   0.0, 800.0, 150.0)),    # строго по +Y
    ((0.0, 0.0, 150.0), ( 600.0, 600.0, 400.0)),    # наискось вверх
]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    A = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.AsdaProjectileActor, unreal.Vector(0, 0, 0))
    if not A:
        log("актор не заспавнен"); raise SystemExit
    mesh = unreal.EditorAssetLibrary.load_asset(
        "/Game/AsdaAssets/Models/Char/ch1_n_00_arrs001_00_00_1/StaticMeshes/ch1_n_00_arrs001_00_00_1")
    comp = A.get_editor_property("arrow_mesh")
    comp.set_static_mesh(mesh)
    for (f, t) in CASES:
        A.launch(unreal.Vector(*f), unreal.Vector(*t), 2.5)
        org, ext = A.get_actor_bounds(False)
        d = unreal.Vector(t[0] - f[0], t[1] - f[1], t[2] - f[2])
        dist = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
        dirv = (d.x / dist, d.y / dist, d.z / dist)
        fwd = A.get_actor_forward_vector()
        cos = fwd.x * dirv[0] + fwd.y * dirv[1] + fwd.z * dirv[2]
        # длина меша в мире = проекция габарита на направление полёта (полу-габарит ×2)
        proj = abs(ext.x * dirv[0]) + abs(ext.y * dirv[1]) + abs(ext.z * dirv[2])
        log("%s → %s: дист=%.0f, полёт=%.3fс (эталон %.3f), cos(вперёд,dir)=%.4f, длина меша в мире≈%.1f юн, старт=%s" %
            (f, t, dist, A.get_editor_property("flight_time_s"), dist / 2500.0, cos, proj * 2.0,
             A.get_editor_property("start_pos")))
    unreal.EditorLevelLibrary.destroy_actor(A)
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
