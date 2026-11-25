# Copyright (c) 2026 @sitxovski (Discord)
# fishing_import_char_clips.py — РЫБАЛКА, волна В5а: headless-импорт клипов рыбалки персонажа
# (anim_225..232) на СУЩЕСТВУЮЩИЙ скелет ch1_{m|f}_cremmy_preview_Skeleton.
# Меши/скелет/прочие anim_NN НЕ трогаются (переимпорт скелета запрещён).
# Источник: RawAssets/Models/Skeletal/Characters/fishing/ch1_{m|f}_cremmy_preview.glb (fishing_export_assets.py char).
# Соответствие: anim_(225 + action − 60), action 60..67 = Fs_ch_{g}_ani_{start_00, idle_00, end_00, idle_01,
#   idle_02, end_01, end_02, end_00} (эталон Char_LoadMotionAnim@0x9B9890, game.c:45125, треки 27..34).
# Механика 1:1 с import_char_emotions.py (Interchange: skeleton = существующий + import_only_animations,
#   импорт во ВРЕМЕННУЮ папку → перенос каждого "…anim_NN" на место ch1_{g}_cremmy_previewanim_NN).
# Запуск: python Tools/run_headless_safe.py Tools/fishing_import_char_clips.py  (или из fishing_import_all.py)
import os, sys, re, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "fishing"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # ★ волна 2 п. 7: Tools в пути — для asda_ue_paths
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_char_clips as _CC   # ★ дедуп №2 В1 Т11: общий импорт клипов персонажа (Interchange «только анимации»)
DEST_BASE = _U.G_MODELS_CHARACTERS + "/ch1_%s_cremmy_preview/SkeletalMeshes"
SKEL = {g: _U.skel_mesh(_U.G_MODELS_CHARACTERS, _U.char_preview_name(g == "m"), "_Skeleton") for g in ("m", "f")}
TMP = "/Game/Tmp_FishingClips"   # временная папка импорта (удаляется)
EXPECT = list(range(225, 233))   # anim_225..232 — 8 клипов рыбалки (action 60..67)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
AR = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[fishing_char] " + str(m))

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
