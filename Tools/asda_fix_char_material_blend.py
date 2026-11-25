# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_char_material_blend.py — ПОЧИНКА «частично прозрачного» оружия/брони/причёсок.
#
# СИМПТОМ (живой тест 2026-07-31): у части оружия (напр. ch1_n_90_bbow209 с текстурой ..._00_00_1)
#   и брони на модели видны дыры/просветы.
# КОРЕНЬ: MIC семейства персонажа были инстансами MASKED-мастера M_AsdaObjectLit (OpacityMask = Tex.A,
#   clip 0.5). У 93 текстур оружия и 386 текстур брони есть зоны alpha=0 — они вырезались.
#   ЭТАЛОН (system/EmptyProject.fx, техники tSkin/tRigid → PShade) альфу в прозрачность НЕ заводит:
#       Output.RGBColor.w = (1 * (In.fog * 4.0f)) * (NoSkinTexScroll.w * 0.25f);
#   а использует её как МАСКУ БЛИКА/КУБМАПА (vSpecular*Tex.w, texCUBE*Tex.w*0.3).
#   У объектов карты (tNoSkin → PShadeNoSkin) альфа В прозрачности есть — их не трогаем.
#
# ЧТО ДЕЛАЕТ: у всех мешей семейства ch1_* (оружие/броня/причёски/лицо/тело — статик и скелетал)
#   переводит назначенные в слоты MaterialInstanceConstant с MASKED-мастера на M_AsdaCharLit (Opaque).
#   Текстурные/скалярные оверрайды инстанса сохраняются. Идемпотентно.
#
# Запуск: run_headless.bat asda_fix_char_material_blend.py            (починка + отчёт)
#         run_headless.bat asda_fix_char_material_blend.py -dry       (только инвентаризация)
import os, sys, json
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_material_policy as POL

ROOTS = ["/Game/AsdaAssets/Models/Char",
         "/Game/AsdaAssets/Models/Skeletal/EquipParts",
         "/Game/AsdaAssets/Models/Skeletal/Characters"]
import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
REPORT = os.path.join(_P.RE_REPORTS, "char_material_blend.json")
DRY = any(a.lower() in ("-dry", "--dry") for a in sys.argv)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m): unreal.log_warning("[charmat] %s" % m)


def iter_meshes():
    for root in ROOTS:
        if not EAL.does_directory_exist(root):
            continue
        for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
            cls = str(ad.asset_class_path.asset_name)
            if cls in ("StaticMesh", "SkeletalMesh"):
                yield cls, str(ad.asset_name), str(ad.package_name)


def slot_materials(cls, mesh):
    """[(index, MaterialInterface|None)] по слотам меша."""
    out = []
    if cls == "StaticMesh":
        for i, s in enumerate(mesh.get_editor_property("static_materials")):
            out.append((i, s.material_interface))
    else:
        for i, s in enumerate(mesh.get_editor_property("materials")):
            out.append((i, s.get_editor_property("material_interface")))
    return out


def main():
    charmaster = unreal.load_asset(POL.MASTER_CHAR)
    if not charmaster:
        log("НЕТ мастера %s — сначала: run_headless.bat asda_build_materials_lit.py char" % POL.MASTER_CHAR)
        return
    stats = {"мешей": 0, "слотов": 0, "уже_opaque": 0, "переведено": 0, "чужой_мастер": 0,
             "пусто": 0, "не_MIC": 0, "не_ch1": 0}
    changed_mics = set()
    details = []
    for cls, name, pkg in iter_meshes():
        if not POL.is_char_family(name):
            stats["не_ch1"] += 1
            continue
        mesh = unreal.load_asset(pkg)
        if not mesh:
            continue
        stats["мешей"] += 1
        for idx, mi in slot_materials(cls, mesh):
            stats["слотов"] += 1
            if not mi:
                stats["пусто"] += 1
                continue
            if not isinstance(mi, unreal.MaterialInstanceConstant):
                stats["не_MIC"] += 1
                details.append({"mesh": name, "slot": idx, "mat": mi.get_path_name(),
                                "проблема": "не MIC (%s)" % mi.get_class().get_name()})
                continue
            parent = mi.get_editor_property("parent")
            ppath = parent.get_path_name().split(".")[0] if parent else None
            if ppath == POL.MASTER_CHAR:
                stats["уже_opaque"] += 1
                continue
            if ppath not in (POL.MASTER_MASKED, "/Game/Materials/M_AsdaObject", None):
                stats["чужой_мастер"] += 1
                details.append({"mesh": name, "slot": idx, "mat": mi.get_path_name(),
                                "проблема": "чужой мастер %s" % ppath})
                continue
            if not DRY:
                MEL.set_material_instance_parent(mi, charmaster)
                # ★ ГРАБЛЯ: set_material_instance_parent НЕ помечает пакет dirty, а save_loaded_asset по
                #   умолчанию сохраняет ТОЛЬКО dirty → первый прогон «прошёл успешно», но на диск не лёг
                #   (контроль: в .uasset осталась строка M_AsdaObjectLit). Нужны modify() + only_if_is_dirty=False.
                mi.modify()
                if not EAL.save_loaded_asset(mi, False):
                    log("НЕ СОХРАНЁН: %s" % mi.get_path_name())
            changed_mics.add(mi.get_path_name())
            stats["переведено"] += 1
        if stats["мешей"] % 300 == 0:
            log("...мешей %d, переведено слотов %d" % (stats["мешей"], stats["переведено"]))
    log("ИТОГ%s: %s" % (" (DRY)" if DRY else "", stats))
    log("уникальных MIC переведено: %d" % len(changed_mics))
    try:
        os.makedirs(os.path.dirname(REPORT), exist_ok=True)
        with open(REPORT, "w") as f:
            json.dump({"dry": DRY, "stats": stats, "mics": sorted(changed_mics),
                       "details": details[:200]}, f, ensure_ascii=False, indent=1)
        log("отчёт: %s" % REPORT)
    except Exception as e:
        log("отчёт не записан: %s" % e)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
