# Copyright (c) 2026 @sitxovski (Discord)
# Headless-импорт ТОЛЬКО нового клипа anim_35 (бег с крыльями, ch4_{m|f}_00_move_00) на СУЩЕСТВУЮЩИЙ
# скелет ch1_{m|f}_cremmy_preview_Skeleton. Меши/скелет/anim_00..28 НЕ трогаются (переимпорт скелета запрещён).
# Механика (паттерн import_equip_parts.py): Interchange-pipeline override:
#   common_skeletal_meshes_and_animations_properties.skeleton = существующий + import_only_animations=True.
# Импорт во ВРЕМЕННУЮ папку → перенос AnimSequence "…anim_35" на место ch1_{g}_cremmy_previewanim_35 →
# служебный "__ref_ignore" и tmp-папка удаляются.
# Запуск: run_headless.bat import_char_cremmy_move35.py [m|f]
import os, sys, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "move35"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # ★ волна 2 п. 7: Tools в пути — для asda_ue_paths
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_char_clips as _CC   # ★ дедуп №2 В1 Т11: общий импорт клипов персонажа (Interchange «только анимации»)
DEST_BASE = _U.G_MODELS_CHARACTERS + "/ch1_%s_cremmy_preview/SkeletalMeshes"
SKEL = {g: _U.skel_mesh(_U.G_MODELS_CHARACTERS, _U.char_preview_name(g == "m"), "_Skeleton") for g in ("m", "f")}
TMP = "/Game/Tmp_Move35"

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
AR = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[import_move35] " + str(m))

def import_gender(g):
    glb = os.path.join(RAW, "ch1_%s_cremmy_preview.glb" % g)
    if not os.path.exists(glb):
        log("НЕТ glb: %s — пропуск" % glb); return False
    skel = unreal.load_asset(SKEL[g])
    if not skel:
        log("!! скелет не найден: %s" % SKEL[g]); return False
    tmp = TMP + "_" + g
    _CC.import_anims_to_tmp(glb, skel, tmp)   # ★ дедуп №2 Т11: Interchange «только анимации» во временную папку (общий)

    # найти AnimSequence "…anim_35" во временной папке
    src = None
    for ad in AR.get_assets_by_path(unreal.Name(tmp), recursive=True):
        cn = str(ad.asset_class_path.asset_name); an = str(ad.asset_name)
        log("  tmp-ассет: %s (%s)" % (an, cn))
        if cn == "AnimSequence" and an.lower().endswith("anim_35"):
            src = str(ad.package_name)
    if not src:
        log("!! [%s] anim_35 не найден после импорта" % g); return False

    dst = (DEST_BASE % g) + "/ch1_%s_cremmy_previewanim_35" % g
    if EAL.does_asset_exist(dst):
        EAL.delete_asset(dst)   # перезапуск скрипта — заменить только НАШ ассет
    if not EAL.rename_asset(src, dst):
        log("!! [%s] rename %s -> %s не удался" % (g, src, dst)); return False
    EAL.save_asset(dst, only_if_is_dirty=False)
    EAL.delete_directory(tmp)   # выбрасывает "__ref_ignore" и остатки

    # верификация
    a = unreal.load_asset(dst)
    if not a:
        log("!! [%s] итоговый ассет не грузится: %s" % (g, dst)); return False
    ln = float(a.get_editor_property("sequence_length"))
    sk = a.get_editor_property("skeleton")
    ntr = -1
    try: ntr = a.get_data_model().get_num_bone_tracks()
    except Exception: pass
    log("[%s] OK: %s | len=%.3fс | скелет=%s | треков=%d" % (g, dst, ln, sk.get_name() if sk else "NONE", ntr))
    return ln > 0 and sk is not None and sk.get_name() == os.path.basename(SKEL[g])

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
