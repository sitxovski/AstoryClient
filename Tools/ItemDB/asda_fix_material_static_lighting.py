# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_material_static_lighting.py
#
# ПРОБЛЕМА, которую он лечит: в УПАКОВАННОЙ сборке часть объектов рисуется серым дефолтным
# материалом, а в редакторе всё нормально. В логе игры это видно дословно:
#     LogMaterial: Warning: Material <путь> missing usage flag StaticLighting!
#                  Default Material will be used in game.
#
# ПРИЧИНА. Если на карте ЗАПЕЧЁНО освещение (рядом с картой лежит <Карта>_BuiltData.uasset),
# движок требует от каждого материала на статической геометрии флаг "использовать со статическим
# освещением". В РЕДАКТОРЕ этот флаг ставится автоматически при первом использовании — и работает,
# поэтому проблема не видна. Но ассет при этом НЕ пересохраняется: флаг живёт только в памяти
# редактора. В кук уходит материал БЕЗ флага, и в игре движок подставляет дефолтный серый.
#
# ЛЕЧЕНИЕ: выставить флаг у базовых материалов явно и сохранить их на диск.
# ⚠ Флаг живёт у БАЗОВОГО материала (UMaterial). У инстансов (UMaterialInstance) своего флага нет —
#   они наследуют родительский, поэтому идём вверх по цепочке родителей.
#
# Режимы (переменная окружения ASDA_MODE):
#   fix   — (по умолчанию) выставить недостающие флаги и сохранить;
#   audit — ТОЛЬКО проверить и напечатать список, ничего не менять. Гонять ПЕРЕД УПАКОВКОЙ.
#
# Запуск (headless):
#   UnrealEditor-Cmd.exe <проект> -ExecutePythonScript="@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/ItemDB/asda_fix_material_static_lighting.py"
#     -unattended -nopause -nosplash -nosound
import os

# Где искать карты проекта.
MAP_ROOTS = ["/Game/Map"]


def base_material(unreal, mat):
    """Подняться от инстанса к базовому материалу (флаг использования живёт только там)."""
    seen = 0
    while mat is not None and not isinstance(mat, unreal.Material):
        if not isinstance(mat, unreal.MaterialInstance):
            return None
        mat = mat.get_editor_property("parent")
        seen += 1
        if seen > 16:          # страховка от кольца в цепочке родителей
            return None
    return mat


def maps_with_baked_lighting(unreal):
    """Карты, рядом с которыми лежит <Карта>_BuiltData — только у них требуется флаг."""
    eal = unreal.EditorAssetLibrary
    out = []
    for root in MAP_ROOTS:
        if not eal.does_directory_exist(root):
            continue
        for a in eal.list_assets(root, recursive=True, include_folder=False):
            pkg = a.split(".")[0]
            if pkg.endswith("_BuiltData"):
                continue
            if eal.does_asset_exist(pkg + "_BuiltData"):
                out.append(pkg)
    return out


def collect_static_materials(unreal, map_pkg):
    """Материалы ВСЕЙ геометрии карты, у которой запечён свет.

    * Фильтр по мобильности СНЯТ намеренно. Первый заход отбирал только компоненты с
      мобильностью STATIC и не нашёл ни одного, хотя движок в упакованной сборке ругался на
      тринадцать материалов именно этой карты: мобильность у актёров карты не STATIC, а флаг
      всё равно требуется. Раз у карты есть запечённый свет, флаг нужен всей её геометрии."""
    unreal.EditorLoadingAndSavingUtils.load_map(map_pkg)
    mats = {}
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    ncomp = 0
    for act in actors:
        for comp in act.get_components_by_class(unreal.MeshComponent):
            ncomp += 1
            for i in range(comp.get_num_materials()):
                m = comp.get_material(i)
                if m is None:
                    continue
                b = base_material(unreal, m)
                if b is None:
                    continue
                path = b.get_path_name().split(".")[0]
                # * Только НАШИ материалы: движковые (/Engine/EditorMaterials/...) в игру не идут,
                #   и записать их всё равно нельзя.
                if not path.startswith("/Game/"):
                    continue
                mats[path] = b
    unreal.log("    actors %d, mesh components %d, base materials %d" % (len(actors), ncomp, len(mats)))
    return mats


def main():
    import unreal
    mode = os.environ.get("ASDA_MODE", "fix").lower()
    eal = unreal.EditorAssetLibrary

    baked = maps_with_baked_lighting(unreal)
    unreal.log("=== static lighting usage flag: mode=%s, maps with baked lighting: %d ===" % (mode, len(baked)))
    for m in baked:
        unreal.log("    %s" % m)
    if not baked:
        unreal.log("no baked maps - nothing to do")
        return

    missing_total = []
    fixed_total = 0
    for map_pkg in baked:
        mats = collect_static_materials(unreal, map_pkg)
        missing = [p for p, b in mats.items()
                   if not b.get_editor_property("used_with_static_lighting")]
        unreal.log("%s: static materials %d, missing flag %d" % (map_pkg, len(mats), len(missing)))
        missing_total.extend(missing)
        if mode == "audit":
            for p in sorted(missing):
                unreal.log_warning("    MISSING StaticLighting: %s" % p)
            continue
        for p in sorted(missing):
            b = mats[p]
            b.set_editor_property("used_with_static_lighting", True)
            if eal.save_loaded_asset(b):
                fixed_total += 1
                unreal.log("    fixed+saved: %s" % p)
            else:
                unreal.log_warning("    FAILED to save: %s" % p)

    if mode == "audit":
        if missing_total:
            unreal.log_warning("=== AUDIT FAILED: %d material(s) without StaticLighting flag ===" % len(missing_total))
            unreal.log_warning("    They will render as the grey default material in a packaged build.")
        else:
            unreal.log("=== AUDIT OK: every static-lit material carries the flag ===")
    else:
        unreal.log("=== done: fixed %d of %d ===" % (fixed_total, len(missing_total)))


if __name__ == "__main__":
    main()
