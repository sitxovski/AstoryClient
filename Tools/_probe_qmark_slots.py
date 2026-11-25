# Copyright (c) 2026 @sitxovski (Discord)
# _probe_qmark_slots.py — слоты материалов у скелетал-мешей квестовых меток + статистика компиляции мастера.
import unreal
EAL = unreal.EditorAssetLibrary
MLIB = unreal.MaterialEditingLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for i in range(3):
        p = "/Game/AsdaAssets/Models/NPC/eff_0_npc_99_exclam-a_%02d/SkeletalMeshes/eff_0_npc_99_exclam-a_%02d" % (i, i)
        sm = EAL.load_asset(p)
        if not sm:
            unreal.log("[slots] НЕТ %s" % p); continue
        mats = sm.get_editor_property("materials")
        for j, m in enumerate(mats):
            mi = m.get_editor_property("material_interface")
            unreal.log("[slots] %s слот%d = %s (класс %s)" % (p.split('/')[-1], j,
                       mi.get_name() if mi else "None", mi.get_class().get_name() if mi else "-"))
    m = EAL.load_asset("/Game/Materials/M_AsdaMeshGlowTint")
    try:
        st = MLIB.get_statistics(m)
        unreal.log("[slots] статистика мастера: %s" % st)
    except Exception as e:
        unreal.log("[slots] get_statistics недоступна: %s" % e)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
