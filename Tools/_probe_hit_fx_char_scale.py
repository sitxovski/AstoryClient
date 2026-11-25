# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""ЗАМЕР (ничего не меняет): множитель эффекта удара по виду цели — до/после переноса 2.0 и 1.5.

Два измерения:
  1) СТРУКТУРНО — максимальный полу-экстент эмиттеров BP импактов обычной атаки и эффектов удара
     навыка 707, и во что он превращается при масштабе актора s;
  2) ЖИВОЙ ТИК — актор реально спавнится в редакторе с масштабом s, тикается, и у него читаются
     ВЕРШИНЫ секций ProceduralMesh: габарит на экране = то, что видит игрок. Это доказывает, что
     множитель доезжает до геометрии, а не просто лежит в трансформе.
"""
import os
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal

CHAR_H = 170.0
# (BP, подпись, масштаб ДО, масштаб ПОСЛЕ)
CASES = [
    ("BP_ImpactBattk01", "обычная атака, импакт 01", 1.0, 2.0),
    ("BP_ImpactBattk02", "обычная атака, импакт 02", 1.0, 2.0),
    ("BP_ImpactBattk03", "обычная атака, импакт 03 (дальнобой)", 1.0, 2.0),
    ("BP_ImpactBattk04", "обычная атака, импакт 04", 1.0, 2.0),
    ("BP_ImpactMob", "моб бьёт игрока (эталон 1.0 — не трогаем)", 1.0, 1.0),
    ("BP_Fx_eff_1_char_07_runfirb04_a_00", "удар навыка 707 по ПЕРСОНАЖУ", 1.0, 2.0),
]


def log(m):
    unreal.log_warning("[удар-масштаб] %s" % m)


def struct_half(cdo):
    """Максимальный полу-экстент по узлам всех эмиттеров BP."""
    best = 0.0
    for em in cdo.get_editor_property("emitters"):
        for n in em.get_editor_property("nodes"):
            for k in ("size_x", "size_y", "ring_top_r", "ring_bot_r", "ring_h"):
                try:
                    best = max(best, abs(float(n.get_editor_property(k))))
                except Exception:
                    pass
    return best


def live_extent(cls, scale, eas):
    """Живой габарит: спавним актор с масштабом и меряем ВЕРШИНЫ секций PMC (мировые координаты)."""
    a = eas.spawn_actor_from_class(cls, unreal.Vector(0, 0, 0))
    if not a:
        return -1.0
    try:
        a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
        if not pmc:
            return -1.0
        lo = [1e18, 1e18, 1e18]
        hi = [-1e18, -1e18, -1e18]
        got = False
        for i in range(pmc.get_num_sections()):
            try:
                V = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)[0]
            except Exception:
                continue
            for v in (V or []):
                got = True
                for k, c in enumerate((v.x, v.y, v.z)):
                    lo[k] = min(lo[k], c)
                    hi[k] = max(hi[k], c)
        if not got:
            return -1.0
        return max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2])
    finally:
        eas.destroy_actor(a)


def main():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # Пустой уровень: без него у только что заспавненного актора секции PMC не строятся
    #   (тот же приём, что в `_probe_fx_runtime_verts.py`).
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level("/Game/Temp/_hitfx_probe")
    except Exception as e:
        log("новый уровень не создан: %s" % e)
    log("%-38s %10s %10s %10s %12s %12s" %
        ("BP", "полу-экст", "s(до)", "s(после)", "живой ДО", "живой ПОСЛЕ"))
    for bp_name, why, s_before, s_after in CASES:
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/%s" % bp_name)
        if not bp:
            log("%-38s НЕТ АССЕТА" % bp_name)
            continue
        cls = bp.generated_class()
        cdo = unreal.get_default_object(cls)
        half = struct_half(cdo)
        lb = live_extent(cls, s_before, eas)
        la = live_extent(cls, s_after, eas)
        log("%-38s %10.1f %10.2f %10.2f %12.1f %12.1f   %s"
            % (bp_name, half, s_before, s_after, lb, la, why))
        if lb > 0 and la > 0:
            log("    → на экране %.0f юн (%.2f роста) → %.0f юн (%.2f роста), отношение x%.2f"
                % (lb, lb / CHAR_H, la, la / CHAR_H, la / max(lb, 1e-6)))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
