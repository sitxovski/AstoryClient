# Copyright (c) 2026 @sitxovski (Discord)
# Headless-импорт ТОЛЬКО сидячих клипов ch1_n_sit_00NN_{m|f}_{000,001,002} на СУЩЕСТВУЮЩИЙ
# скелет ch1_{m|f}_cremmy_preview_Skeleton. Меши/скелет/anim_00..35 НЕ трогаются (переимпорт скелета запрещён).
# Механика (паттерн import_char_cremmy_move35.py): Interchange-pipeline override:
#   common_skeletal_meshes_and_animations_properties.skeleton = существующий + import_only_animations=True.
# Импорт во ВРЕМЕННУЮ папку → перенос всех AnimSequence "…sit_NNNN_VVV" в SkeletalMeshes/ →
# служебный "__ref_ignore" и tmp-папка удаляются.
# Имена ассетов: ch1_{m|f}_cremmy_previewsit_<NNNN>_<VVV>  (VVV: 000=idle сидя → анимслот 0,
#   001=шаг → слот 1, 002=бег → слот 5; см. mount_system_re.md §2.2 п.5).
# Запуск: run_headless.bat import_char_sit.py [m|f]
import os, sys, re, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "sit"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # ★ волна 2 п. 7: Tools в пути — для asda_ue_paths
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_char_clips as _CC   # ★ дедуп №2 В1 Т11: общий импорт клипов персонажа (Interchange «только анимации»)
DEST_BASE = _U.G_MODELS_CHARACTERS + "/ch1_%s_cremmy_preview/SkeletalMeshes"
SKEL = {g: _U.skel_mesh(_U.G_MODELS_CHARACTERS, _U.char_preview_name(g == "m"), "_Skeleton") for g in ("m", "f")}
TMP = "/Game/Tmp_Sit"
NAME_RE = re.compile(r"sit_\d{4}_\d{3}$", re.I)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
AR = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[import_sit] " + str(m))


def import_gender(g):
    glb = os.path.join(RAW, "ch1_%s_cremmy_preview.glb" % g)
    if not os.path.exists(glb):
        log("НЕТ glb: %s — сначала: python export_char_sit.py" % glb); return False
    skel = unreal.load_asset(SKEL[g])
    if not skel:
        log("!! скелет не найден: %s" % SKEL[g]); return False
    tmp = TMP + "_" + g
    _CC.import_anims_to_tmp(glb, skel, tmp)   # ★ дедуп №2 Т11: Interchange «только анимации» во временную папку (общий)

    # собрать все AnimSequence "…sit_NNNN_VVV" во временной папке
    found = []
    for ad in AR.get_assets_by_path(unreal.Name(tmp), recursive=True):
        if str(ad.asset_class_path.asset_name) == "AnimSequence" and NAME_RE.search(str(ad.asset_name)):
            found.append((str(ad.asset_name), str(ad.package_name)))
    if not found:
        log("!! [%s] сидячие клипы не найдены после импорта" % g); return False
    log("[%s] импортировано во tmp: %d клипов" % (g, len(found)))

    dest = DEST_BASE % g
    ok = bad = 0
    for name, src in sorted(found):
        dst = "%s/%s" % (dest, name)
        if EAL.does_asset_exist(dst):
            EAL.delete_asset(dst)       # перезапуск — заменить только НАШИ ассеты
        if EAL.rename_asset(src, dst):
            EAL.save_asset(dst, only_if_is_dirty=False); ok += 1
        else:
            log("  !! rename %s -> %s не удался" % (src, dst)); bad += 1
    EAL.delete_directory(tmp)           # выбрасывает "__ref_ignore" и остатки

    # верификация: длины > 0 и правильный скелет
    zero = wrong = 0
    for name, _ in found:
        a = unreal.load_asset("%s/%s" % (dest, name))
        if not a: wrong += 1; continue
        if float(a.get_editor_property("sequence_length")) <= 0: zero += 1
        sk = a.get_editor_property("skeleton")
        if not sk or sk.get_name() != os.path.basename(SKEL[g]): wrong += 1
    log("[%s] ГОТОВО: перенесено=%d, ошибок=%d, нулевой длины=%d, чужой скелет/нет=%d → %s"
        % (g, ok, bad, zero, wrong, dest))
    return bad == 0 and zero == 0 and wrong == 0


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
