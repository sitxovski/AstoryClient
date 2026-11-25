# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_model_materials.py — привязка текстур к моделям (статик + скелет).
# Interchange создаёт авто-Material (ОБЫЧНЫЙ, не MaterialInstanceConstant) на слот меша → реперентить нельзя.
# Решение: на каждый слот (имя слота = имя текстуры) создаём MIC (мастер M_AsdaObject + Tex) В ПАПКЕ САМОЙ МОДЕЛИ
# (<model>/Materials/MI_<tex>) и НАЗНАЧАЕМ в слот. Дедуп в пределах модели. Ничего не удаляем.
# Запуск: run_headless.bat ...\asda_fix_model_materials.py   (или в редакторе).
import unreal, re, os, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U
import asda_material_policy as POL   # ★ выбор мастера по эталону (tSkin/tRigid=Opaque vs tNoSkin=Masked)

MODELS_ROOT = _U.G_MODELS
TEX_ROOT    = _U.G_TEXTURES
# ★ Охват: ASDA_ONLY_MODELS="имя1,имя2" — обрабатывать ТОЛЬКО эти модели (папка <Models>/<кат>/<имя>).
#   Нужен для точечного ремонта после переимпорта: полный прогон перевязывает материалы ПО ИМЕНИ и
#   откатил бы фиксы, сделанные ПО ДАННЫМ (лут asda_fix_items_loot_tex, NPC asda_fix_npc_materials).
#   Формат элемента: "<корень>/<модель>" (напр. "Mob/mon_s_0_kcat01-a_00" — статик, НЕ MobSkel) либо
#   голое имя модели (совпадёт в любом корне — использовать с осторожностью).
ONLY = set(s.strip() for s in os.environ.get("ASDA_ONLY_MODELS", "").split(",") if s.strip())


def in_scope(model_dir_pkg):
    """Попадает ли папка модели в охват ASDA_ONLY_MODELS."""
    if not ONLY: return True
    p = model_dir_pkg.split("/")
    return p[-1] in ONLY or "/".join(p[-2:]) in ONLY
MASTER      = POL.MASTER_MASKED   # ★ Lit-мастер (курс «современно» 2026-07-23): пересборка НЕ должна откатывать MIC на unlit
                                  #   ★ 2026-07-31: для семейства ch1_* берётся POL.MASTER_CHAR (непрозрачный) — см. asda_material_policy.py

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

# ★ ВОЛОСЫ: строка материала внутри .jbm причёсок 011..025 указывает на несуществующее имя
#   ch1_{m|f}_00_hair011_00_00_0 (3 цифры) — в архиве текстура ch1_{m|f}_00_hair0011_00_00_0 (4 цифры).
#   Оригинал эту строку не читает: имя строится формулой эталона (entity_char.c:11535,
#   CCharacter_LoadHairModel@0x9E2700): sprintf("ch1_%c_00_hair00%d_00_0%d_0.TEX", style+1, color)
#   — «hair00» + десятичное (style+1) без ведущих нулей. Воспроизводим её как fallback.

hair_slot_to_tex = MI.hair_slot_to_tex   # ★ формула имени текстуры причёски (эталон CCharacter_LoadHairModel@0x9E2700)

def find_tex(name, tex, kind=None):
    """Текстура для слота/MIC (пресет «blend_hair» библиотеки = прежняя копия этого файла).
    ★ 15.09.2026: kind — тип модели (MI.kind_for_package папки модели): при дублях имени копия из папки своего типа,
    как в оригинале (`Reports/dedup_audit/tex_dup_rule.md`). Внешний вызов без kind (asda_restore_char_models) —
    копия с наименьшим рангом."""
    return MI.find_tex(name, tex, "blend_hair", kind=kind)

def model_dir(pkg):
    # папка модели = до /StaticMeshes/ или /SkeletalMeshes/ ; иначе папка самого ассета
    for mk in ("/StaticMeshes/", "/SkeletalMeshes/"):
        if mk in pkg: return pkg.split(mk)[0]
    return pkg.rsplit("/", 1)[0]

def main():
    master = unreal.load_asset(MASTER)
    charmaster = unreal.load_asset(POL.MASTER_CHAR)
    if not master:
        unreal.log_warning("Нет мастера %s" % MASTER); return
    if not charmaster:
        unreal.log_warning("Нет мастера %s (собрать: asda_build_materials_lit.py char)" % POL.MASTER_CHAR); return
    tex = tex_index()
    unreal.log("Текстур в индексе: %d%s" % (len(tex), ("  | ОХВАТ: %d моделей" % len(ONLY)) if ONLY else ""))
    cache = {}
    _stat = {}      # счётчики по мастерам (для отчёта: сколько слотов ушло на прозрачный мастер)

    def get_mic(micdir, slotname, meshname):
        tp = find_tex(slotname, tex, MI.kind_for_package(micdir))   # ★ 15.09: тип модели по папке модели
        if not tp: return None
        texname = tp.rsplit("/", 1)[-1]
        # ★★★ ИМЯ MIC НЕСЁТ КЛАСС ОТРИСОВКИ. Дедуп идёт по ТЕКСТУРЕ, а полупрозрачная подсетка носит
        #   ту же текстуру, что и непрозрачный корпус (`itm1_n_00_neck009_00`: 3 слота из 4 помечены).
        #   Без разделения ключа один MIC достался бы обоим классам и последний прогон затирал бы
        #   родителя — ровно та грабля, из-за которой правки материалов «молча откатывались».
        blend = POL.is_blend_slot(slotname)
        micname = "MI_%s%s" % (texname, POL.BLEND_SUFFIX if blend else "")
        micpath = "%s/%s" % (micdir, micname)
        if micpath in cache: return cache[micpath]
        mic = unreal.load_asset(micpath) if EAL.does_asset_exist(micpath) else \
              ATOOLS.create_asset(micname, micdir, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
        # ★ мастер: помеченная подсетка → прозрачный; ch1_* (tSkin/tRigid) → непрозрачный; иначе masked
        mpath = POL.master_for(meshname, slotname)
        if mpath == POL.MASTER_BLEND:
            bm = unreal.load_asset(POL.MASTER_BLEND)
            if not bm:
                unreal.log_warning("НЕТ мастера %s (собрать: asda_build_blend_material.py) — слот %s "
                                   "останется на %s" % (POL.MASTER_BLEND, slotname, POL.MASTER_MASKED))
                bm = master
            MEL.set_material_instance_parent(mic, bm)
            _stat["blend"] = _stat.get("blend", 0) + 1
        else:
            MEL.set_material_instance_parent(mic, charmaster if POL.is_char_family(meshname) else master)
        MEL.set_material_instance_texture_parameter_value(mic, "Tex", unreal.load_asset(tp))
        MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
        mic.modify(); EAL.save_loaded_asset(mic, False)   # ★ set_material_instance_parent не метит dirty
        cache[micpath] = mic
        return mic

    n_mesh = n_slot = n_wired = n_miss = 0
    for ad in AR.get_assets_by_path(unreal.Name(MODELS_ROOT), recursive=True):
        cls = str(ad.asset_class_path.asset_name)
        if cls not in ("StaticMesh", "SkeletalMesh"): continue
        pkg = str(ad.package_name)
        mdir0 = model_dir(pkg)
        if not in_scope(mdir0): continue                             # ★ охват
        if _LG.login_package_blocked(mdir0, "asda_fix_model_materials.py"): continue   # ★ 15.09: сцена логина
        if MI.data_pinned_dir(mdir0):                                     # ★ 15.09: текстура по данным (MI.DATA_PINNED_TEX)
            unreal.log_warning("[DATA-PIN] asda_fix_model_materials.py: пропуск %s — текстура по данным "
                               "(asda_fix_mob_materials.py), перевязка по имени слота её сменила бы" % mdir0)
            continue
        m = unreal.load_asset(pkg)
        mdir = mdir0 + "/Materials"
        changed = False
        if cls == "StaticMesh":
            # ВАЖНО: правка массива static_materials НЕ персистится — используем set_material(i, mic).
            mats = m.get_editor_property("static_materials")
            for i, slot in enumerate(mats):
                n_slot += 1
                mic = get_mic(mdir, str(slot.get_editor_property("material_slot_name")), str(ad.asset_name))
                if mic: m.set_material(i, mic); changed = True; n_wired += 1
                else: n_miss += 1
        else:
            mats = m.get_editor_property("materials")
            for slot in mats:
                n_slot += 1
                mic = get_mic(mdir, str(slot.get_editor_property("material_slot_name")), str(ad.asset_name))
                if mic: slot.set_editor_property("material_interface", mic); changed = True; n_wired += 1
                else: n_miss += 1
            if changed: m.set_editor_property("materials", mats)
        if changed: EAL.save_loaded_asset(m)
        n_mesh += 1
        if n_mesh % 200 == 0:
            unreal.log("  ...мешей %d, привязано слотов %d" % (n_mesh, n_wired))
    unreal.log("ГОТОВО: мешей=%d, слотов=%d, привязано=%d, без текстуры=%d, на прозрачном мастере=%d"
               % (n_mesh, n_slot, n_wired, n_miss, _stat.get("blend", 0)))


if __name__ == "__main__":   # ★ правило tools-no-import-side-effects
    main()
