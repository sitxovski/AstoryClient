# Copyright (c) 2026 @sitxovski (Discord)
# _probe_projectile.py — headless-проверка ПОДСИСТЕМЫ СНАРЯДОВ (projectile_re.md).
#   Что можно проверить БЕЗ игры: наличие ассетов (2 меша стрелы + BP шлейфа), продольная ось меша
#   полёта (должна быть UE +X = «вперёд» актора) и высота кости «8_b1» в ref-позе мир-скелета
#   (точка выстрела не должна быть у ступней). Визуал шлейфа (billboard-ветка) headless НЕ видит —
#   финальная сверка только в игре командой `asda.VerifyProjectile`.
import sys, os
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

def log(m): unreal.log_warning("[снаряд-probe] %s" % m)

MESH_FLY  = "/Game/AsdaAssets/Models/Char/ch1_n_00_arrs001_00_00_1/StaticMeshes/ch1_n_00_arrs001_00_00_1"
MESH_NOCK = "/Game/AsdaAssets/Models/Char/ch1_n_00_arrs001_00_00_0/StaticMeshes/ch1_n_00_arrs001_00_00_0"
MAT_ARROW = "/Game/AsdaAssets/Models/Char/ch1_n_00_arrs001_00_00_0/Materials/ch1_n_00_arrs001_00_00_0"
BP_TRAIL  = "/Game/AsdaAssets/Effects/BP_Fx_eff_0_char_03_flyarr01_a_00"

EAL = unreal.EditorAssetLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global m
    for p in (MESH_FLY, MESH_NOCK, MAT_ARROW, BP_TRAIL):
        log("%-95s %s" % (p, "ЕСТЬ" if EAL.does_asset_exist(p) else "НЕТ"))
    m = EAL.load_asset(MESH_FLY)
    if m:
        b = m.get_bounds().box_extent
        log("меш полёта: полу-габарит UE=(%.1f, %.1f, %.1f) → продольная ось = %s" %
            (b.x, b.y, b.z, "X (верно: совпадает с «вперёд» актора)" if b.x > b.y and b.x > b.z else "НЕ X — нужен MeshAlignRot"))
    m0 = EAL.load_asset(MESH_NOCK)
    if m0:
        b = m0.get_bounds().box_extent
        log("меш тетивы:  полу-габарит UE=(%.1f, %.1f, %.1f)" % (b.x, b.y, b.z))
    bp = EAL.load_asset(BP_TRAIL)
    if bp:
        cdo = unreal.get_default_object(bp.generated_class())
        ems = cdo.get_editor_property("emitters")
        log("шлейф: эмиттеров %d, effect_name=%s" % (len(ems), cdo.get_editor_property("effect_name")))
        for e in ems:
            log("   эмиттер %-10s type=0x%X узлов=%d период=%.2f" %
                (e.get_editor_property("name"), e.get_editor_property("jeb_type"),
                 len(e.get_editor_property("nodes")), e.get_editor_property("period")))
    # Кость выстрела в ref-позе мир-скелета персонажа.
    for sk_path in ("/Game/AsdaAssets/Models/Char/cre_cremmy/SkeletalMeshes/cre_cremmy",
                    "/Game/AsdaAssets/Models/Char/cre_cremmy/cre_cremmy"):
        if not EAL.does_asset_exist(sk_path):
            continue
        sk = EAL.load_asset(sk_path)
        try:
            names = [str(n) for n in sk.get_editor_property("skeleton").get_editor_property("bone_tree")]
        except Exception:
            names = []
        log("скелет %s: костей %s, '8_b1' %s" % (sk_path, len(names) or "?",
            "ЕСТЬ" if "8_b1" in names else "(проверь в игре asda.VerifyProjectile)"))
        break
    log("ГОТОВО (визуал шлейфа headless не проверяется — только asda.VerifyProjectile в игре)")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
