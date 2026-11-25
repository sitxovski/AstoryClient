# Copyright (c) 2026 @sitxovski (Discord)
# asda_assign_materials.py — НАДЁЖНАЯ привязка материалов к мешам (статик + скелет).
# Interchange создаёт авто-Material (обычный) на слот → asda_fix (только MIC) их не трогал → серые.
# Здесь: для КАЖДОГО слота меша (имя слота = имя текстуры) создаём/переиспользуем ОДИН MIC
# (мастер M_AsdaObject + Tex) и НАЗНАЧАЕМ в слот. Дедуп по текстуре в /Game/AsdaAssets/Materials/Models.
# Запуск: run_headless.bat ...\asda_assign_materials.py   (или в редакторе).
#   Точечный прогон (без чистки MIC-каталога):  ...\asda_assign_materials.py -only hair
#   -only <подстрока>  — обрабатывать только меши, чьё имя содержит подстроку (регистр не важен).
import unreal, re, sys, os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U
import asda_material_policy as POL   # ★ мастер по эталону (tSkin/tRigid=Opaque, tNoSkin=Masked)

MODELS_ROOT = _U.G_MODELS
TEX_ROOT    = _U.G_TEXTURES
MASTER      = POL.MASTER_MASKED   # ★ Lit-мастер (курс «современно» 2026-07-23): пересборка НЕ должна откатывать MIC на unlit
# ★ D5 (волна 2 п. 7, 14.09.2026, сверка с Content): G_MATERIALS_MODELS — папка есть, ассетов 0 (помечено в реестре путей); путь не менялся.
MIC_DIR     = _U.G_MATERIALS_MODELS
CLEAN_MIC_DIR = True   # снести старые MI_* (мусор прошлых сессий) и перегенерировать

# ── фильтр из аргументов командной строки (UE прокидывает хвост после имени скрипта) ──
ONLY = ""
_argv = list(sys.argv)
for _i, _a in enumerate(_argv):
    if _a in ("-only", "--only") and _i + 1 < len(_argv):
        ONLY = _argv[_i + 1].lower()
if ONLY:
    CLEAN_MIC_DIR = False   # точечный прогон не сносит чужие MIC

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()

# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI
import asda_login_guard as _LG   # ★ 15.09.2026: модели сцены логина (Models/Char/cre_creback-a_00, cre_creation01*) — только ASDA_ALLOW_LOGIN_WRITE=1

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)

# ★ ВОЛОСЫ. Строка материала внутри .jbm причёсок 011..025 БИТАЯ: она указывает на
#   ch1_{m|f}_00_hair011_00_00_0 (3 цифры), а в Texture.btz текстура называется
#   ch1_{m|f}_00_hair0011_00_00_0 (4 цифры). Движок эту строку не использует вообще —
#   имя текстуры он СТРОИТ (эталон entity_char.c:11535, CCharacter_LoadHairModel@0x9E2700):
#       sprintf("ch1_%c_00_hair00%d_00_0%d_0.TEX", hairStyle + 1, hairColor)
#   т.е. "hair00" + ДЕСЯТИЧНОЕ число стиля без ведущих нулей (1→hair001, 11→hair0011, 38→hair0038).
#   Здесь воспроизводим ту же формулу как fallback резолва имени слота.

hair_slot_to_tex = MI.hair_slot_to_tex   # ★ формула имени текстуры причёски (эталон CCharacter_LoadHairModel@0x9E2700)

def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «plain_hair» библиотеки = прежняя копия этого файла)."""
    return MI.find_tex(name, tex, "plain_hair")

def main():
    master = unreal.load_asset(MASTER)
    charmaster = unreal.load_asset(POL.MASTER_CHAR)
    if not master:
        unreal.log_warning("Нет мастера %s" % MASTER); return
    if CLEAN_MIC_DIR and EAL.does_directory_exist(MIC_DIR):
        unreal.log("Чистка старых MIC: %s" % MIC_DIR); EAL.delete_directory(MIC_DIR)
    tex = tex_index()
    unreal.log("Текстур в индексе: %d" % len(tex))
    cache = {}

    def get_mic(slotname, meshname=""):
        tp = find_tex(slotname, tex)
        if not tp: return None
        texname = tp.rsplit("/", 1)[-1]
        # ★ 2026-07-31: у семейства ch1_* (персонаж/экип/оружие) мастер НЕПРОЗРАЧНЫЙ (эталон PShade),
        #   поэтому и MIC отдельный — иначе одна и та же текстура делила бы MIC с объектом карты.
        ischar = POL.is_char_family(meshname)
        if (texname, ischar) in cache: return cache[(texname, ischar)]
        micpath = "%s/%s%s" % (MIC_DIR, "MIC_" if ischar else "MI_", texname)
        mic = unreal.load_asset(micpath) if EAL.does_asset_exist(micpath) else \
              ATOOLS.create_asset(micpath.rsplit("/",1)[-1], MIC_DIR, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mic, charmaster if ischar else master)
        MEL.set_material_instance_texture_parameter_value(mic, "Tex", unreal.load_asset(tp))
        MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
        mic.modify(); EAL.save_loaded_asset(mic, False)   # ★ смена parent НЕ метит dirty
        cache[(texname, ischar)] = mic
        return mic

    n_mesh = n_slot = n_wired = n_miss = 0
    for ad in AR.get_assets_by_path(unreal.Name(MODELS_ROOT), recursive=True):
        cls = str(ad.asset_class_path.asset_name)
        if cls not in ("StaticMesh", "SkeletalMesh"): continue
        if ONLY and ONLY not in str(ad.asset_name).lower(): continue
        if _LG.login_package_blocked(str(ad.package_name), "asda_assign_materials.py"): continue   # ★ 15.09: сцена логина
        if MI.data_pinned_dir(str(ad.package_name)):                    # ★ 15.09: текстура по данным (MI.DATA_PINNED_TEX)
            unreal.log_warning("[DATA-PIN] asda_assign_materials.py: пропуск %s — текстура по данным" % ad.package_name); continue
        prop = "static_materials" if cls == "StaticMesh" else "materials"
        m = unreal.load_asset(str(ad.package_name))
        mats = m.get_editor_property(prop)
        changed = False
        for slot in mats:
            n_slot += 1
            name = str(slot.get_editor_property("material_slot_name"))
            mic = get_mic(name, str(ad.asset_name))
            if mic:
                slot.set_editor_property("material_interface", mic); changed = True; n_wired += 1
            else:
                n_miss += 1
        if changed:
            m.set_editor_property(prop, mats); EAL.save_loaded_asset(m)
        n_mesh += 1
        if n_mesh % 200 == 0:
            unreal.log("  ...мешей %d, привязано слотов %d" % (n_mesh, n_wired))
    unreal.log("ГОТОВО: мешей=%d, слотов=%d, привязано=%d, без текстуры=%d, уник.MIC=%d"
               % (n_mesh, n_slot, n_wired, n_miss, len(cache)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
