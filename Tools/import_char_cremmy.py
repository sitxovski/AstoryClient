# Copyright (c) 2026 @sitxovski (Discord)
# Импорт превью-персонажа char-select ch1_{m|f}_cremmy_preview.glb (SkeletalMesh на скелете cre_cremmy +
# 5 AnimSequence: anim_00=idle ... anim_04) + текстуры 5 частей + материалы. Headless-safe.
# САМОДОСТАТОЧЕН: не импортирует asda_import_skinned_models как модуль (у того main() на уровне файла —
# он бы прогнал весь батч скиннутых моделей). Хелперы продублированы локально.
# Параметризован по ПОЛУ (m + f).
# Запуск:
#   run_headless.bat import_char_cremmy.py        # оба пола
#   run_headless.bat import_char_cremmy.py m      # только мужской
import unreal, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # ★ волна 2 п. 7: Tools в пути — для asda_ue_paths
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters"))
TEXSRC = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Textures", "Data", "Character", "Texture"))
DEST = _U.G_MODELS_CHARACTERS
TEXDEST = _U.G_TEX_CHAR_TEXTURE
TEX_ROOT = _U.G_TEXTURES
MASTER   = _U.M_OBJECT_LIT  # ★ ПЕРСОНАЖИ = LIT (unlit-мастер давал пересвет лица/тела vs Lit-экип; см. face_overbright_fix.md)

def parts_for(g):
    return ["ch1_%s_00_chst001_00_00_0" % g, "ch1_%s_00_pant001_00_00_0" % g,
            "ch1_%s_00_shoe001_00_00_0" % g, "ch1_%s_00_face001_00_00_0" % g,
            "ch1_%s_00_hair001_00_00_0" % g]

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[import_char_cremmy] " + str(m))

import os as _mi_os, sys as _mi_sys
_mi_sys.path.insert(0, _mi_os.path.dirname(_mi_os.path.abspath(__file__)))
# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)

def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «plain» библиотеки = прежняя копия этого файла)."""
    return MI.find_tex(name, tex, "plain")

def import_batch(glb_paths, dest):
    """Импорт пачкой — asda_model_import_ue.import_batch."""
    return MI.import_batch(glb_paths, dest)

def wire_materials(dest, tex, master):
    """Все MIC под dest → master + Tex (+Ambient). Мастер ОДИН на все слоты (MASTER).
    ★ master_for НЕ ПОДКЛЮЧЁН — решение координатора волны 2 (14.09.2026): подключение сменит родителя существующих MIC при следующем прогоне (слоты ch1_* ушли бы с M_AsdaObjectLit на M_AsdaCharLit), а владелец правит ассеты руками — не терять. Отчёт `Reports/dedup_audit/wave2_import.md` §3.3, §8."""
    return MI.wire_materials(dest, tex_for=lambda n: find_tex(n, tex), master=master)["n_all"]

def disable_nanite(dest):
    """Снять Nanite со Static/Skeletal под dest — asda_model_import_ue.disable_nanite."""
    MI.disable_nanite(dest)

def import_gender(g, tex_cache):
    OUT_NAME = "ch1_%s_cremmy_preview" % g
    GLB = os.path.join(RAW, OUT_NAME + ".glb")
    SUB = DEST + "/" + OUT_NAME
    PARTS = parts_for(g)
    if not os.path.exists(GLB):
        log("НЕТ glb: %s — пропуск" % GLB); return

    # 1) текстуры частей (png)
    texfiles = [os.path.join(TEXSRC, p + ".png") for p in PARTS]
    texfiles = [f for f in texfiles if os.path.exists(f)]
    if texfiles:
        MI.import_batch(texfiles, TEXDEST)   # ★ дедуп №2 Т17: общий импорт пачкой (AutomatedAssetImportData)
    log("[%s] текстур импортировано: %d" % (g, len(texfiles)))

    # 2) снести старый + импорт glb
    if EAL.does_directory_exist(SUB):
        EAL.delete_directory(SUB)
    import_batch([GLB], DEST)

    # 3) материалы + nanite off
    tex = tex_index()
    master = unreal.load_asset(MASTER)
    mats = wire_materials(SUB, tex, master)
    disable_nanite(SUB)
    log("[%s] материалов привязано: %d" % (g, mats))

    # 3a) ★ СЛУЖЕБНАЯ РЕФЕРЕНС-АНИМАЦИЯ. Экспортёр кладёт первым клип `__ref_ignore`
    #     (`cre_cremmy-a_00_0`): `build_fixed` берёт скелет вывода из clips[0], а скелет превью
    #     един для обоих полов — иначе женский набор `cre_crewmy` (54 кости, корень `G`) породил бы
    #     ВТОРОЙ скелет и все ранее импортированные женские клипы осиротели бы. В ассетах эта
    #     анимация не нужна — сносим сразу после импорта, чтобы не болталась в папке.
    killed = 0
    for ad in AR.get_assets_by_path(unreal.Name(SUB), recursive=True):
        if str(ad.asset_class_path.asset_name) == "AnimSequence" and "__ref_ignore" in str(ad.asset_name):
            EAL.delete_asset(str(ad.package_name)); killed += 1
    log("[%s] служебных клипов __ref_ignore удалено: %d" % (g, killed))

    EAL.save_directory(SUB, only_if_is_dirty=False, recursive=True)
    for ad in AR.get_assets_by_path(unreal.Name(SUB), recursive=True):
        log("  ассет: %s (%s)" % (ad.asset_name, ad.asset_class_path.asset_name))

def main():
    genders = ["m", "f"]
    argv = [a for a in sys.argv if a.lower() in ("m", "f")]
    if argv: genders = [a.lower() for a in argv]
    for g in genders:
        import_gender(g, None)
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
