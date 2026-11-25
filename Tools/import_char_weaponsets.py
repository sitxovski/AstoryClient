# Copyright (c) 2026 @sitxovski (Discord)
# Headless-импорт мировых клипов НАБОРОВ ОРУЖИЯ (anim_40..85) на СУЩЕСТВУЮЩИЙ скелет
# ch1_{m|f}_cremmy_preview_Skeleton. Меши/скелет/anim_00..35 НЕ трогаются (переимпорт скелета запрещён).
# Источник: RawAssets/Models/Skeletal/Characters/wsets/ch1_{m|f}_cremmy_preview.glb
#   (export_char_weaponsets.py — набор _01.._09 из ch4_{g}_{SS}_{idle01..back01}).
# Механика 1:1 с import_char_cremmy_move35.py: Interchange-pipeline override
#   common_skeletal_meshes_and_animations_properties.skeleton = существующий + import_only_animations=True,
#   импорт во ВРЕМЕННУЮ папку → перенос каждого "…anim_NN" на место ch1_{g}_cremmy_previewanim_NN.
# Запуск: run_headless.bat import_char_weaponsets.py [m|f]
import os, sys, re, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "wsets"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # ★ волна 2 п. 7: Tools в пути — для asda_ue_paths
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_char_clips as _CC   # ★ дедуп №2 В1 Т11: общий импорт клипов персонажа (Interchange «только анимации»)
DEST_BASE = _U.G_MODELS_CHARACTERS + "/ch1_%s_cremmy_preview/SkeletalMeshes"
SKEL = {g: _U.skel_mesh(_U.G_MODELS_CHARACTERS, _U.char_preview_name(g == "m"), "_Skeleton") for g in ("m", "f")}
TMP = "/Game/Tmp_WSets"
EXPECT = list(range(40, 86))   # anim_40..85

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
AR = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[import_wsets] " + str(m))

def import_gender(g):
    """Импорт клипов пола g на существующий скелет — общая реализация `asda_char_clips.import_gender_clips`
    (дедуп №2 В1 Т11). Константы модуля читаются при вызове: копание (`digging_import_char_clips`) подменяет
    RAW/TMP/EXPECT/log у этого модуля."""
    return _CC.import_gender_clips(g, RAW, SKEL[g], TMP, DEST_BASE, EXPECT, log)


def main():
    genders = ["m", "f"]
    argv = [a for a in sys.argv if a.lower() in ("m", "f")]
    if argv: genders = [a.lower() for a in argv]
    ok = True
    for g in genders:
        ok = import_gender(g) and ok
    log("ИТОГ: %s" % ("ВСЕ OK" if ok else "ЕСТЬ ПРОБЛЕМЫ"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
