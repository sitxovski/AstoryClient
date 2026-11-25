# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_hair_materials.py — ПОЧИНКА МАТЕРИАЛОВ ПРИЧЁСОК (баг «часть волос без текстуры»).
#
# ПРИЧИНА (реверс, CONFIRMED):
#   Строка материала ВНУТРИ .jbm причёсок 011..025 указывает на несуществующий файл
#   ch1_{m|f}_00_hair011_00_00_0 (3 цифры), тогда как в Character\Texture\Texture.btz текстура называется
#   ch1_{m|f}_00_hair0011_00_00_0 (4 цифры). Импортёр брал имя из .jbm → текстура не находилась →
#   у 30 мешей (hair011..hair025 × 2 пола) материал остался с DefaultTexture (визуально «без текстуры»).
#   Движок ОРИГИНАЛА эту строку не читает вообще — он СТРОИТ имя текстуры формулой
#   (эталон RE/etalon/entity_char.c:11535, CCharacter_LoadHairModel@0x9E2700):
#       sprintf(Buffer, "ch1_%c_00_hair00%d_00_0%d_0.TEX", hairStyle + 1, hairColor);
#   т.е. «hair00» + ДЕСЯТИЧНОЕ (style+1) БЕЗ ведущих нулей: 1→hair001, 11→hair0011, 38→hair0038.
#
# ЧТО ДЕЛАЕТ: для КАЖДОГО слота КАЖДОГО меша волос в /Game/AsdaAssets/Models/Char
#   резолвит текстуру по формуле эталона и гарантирует, что в слоте стоит MaterialInstance
#   мастера M_AsdaObjectLit с этой текстурой в параметре Tex (цвет 0 — базовый; цвета 1/2
#   подменяются в рантайме UAsdaCharAssembly::ApplyHair).
#   Существующий авто-материал Interchange (имя = имя слота) НЕ удаляется — ему проставляется
#   правильный parent+Tex; если он не MaterialInstance, в <модель>/Materials создаётся MI_<tex>.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_fix_hair_materials.py
import unreal, re
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U

MODELS_ROOT = _U.G_MODELS_CHAR
TEX_ROOT    = _U.G_TEXTURES
MASTER      = _U.M_OBJECT_LIT

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()

# ch1_{m|f}_{NN}_hair{NNN}_{NN}_{NN}_{N}  (у 026..038 в конце ещё «_0» — обрабатывается отдельной веткой)
# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI



def hair_canon_tex(name):
    """Имя слота причёски → каноническое имя текстуры (формула эталона) или None.
    ★ Ревизия 14.09: lower() здесь, как в прежней копии — библиотечная функция ждёт уже нижний регистр."""
    return MI.hair_slot_to_tex(name.lower())


def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)


def resolve_tex(name, tex):
    """Точное имя → каноническое (формула эталона). Возврат: package path или None (пресет «hair_only»)."""
    return MI.find_tex(name, tex, "hair_only")


def model_dir(pkg):
    for mk in ("/StaticMeshes/", "/SkeletalMeshes/"):
        if mk in pkg:
            return pkg.split(mk)[0]
    return pkg.rsplit("/", 1)[0]


def main():
    master = unreal.load_asset(MASTER)
    if not master:
        unreal.log_warning("Нет мастера %s" % MASTER)
        return
    tex = tex_index()
    unreal.log("[hairmat] текстур в индексе: %d" % len(tex))

    n_mesh = n_slot = n_fixed = n_ok = n_miss = 0
    miss_names = []

    for ad in AR.get_assets_by_path(unreal.Name(MODELS_ROOT), recursive=True):
        cls = str(ad.asset_class_path.asset_name)
        if cls not in ("StaticMesh", "SkeletalMesh"):
            continue
        if "hair" not in str(ad.asset_name).lower():
            continue
        pkg = str(ad.package_name)
        mesh = unreal.load_asset(pkg)
        if not mesh:
            continue
        n_mesh += 1
        prop = "static_materials" if cls == "StaticMesh" else "materials"
        mats = mesh.get_editor_property(prop)
        changed = False
        for slot in mats:
            n_slot += 1
            sname = str(slot.get_editor_property("material_slot_name"))
            tp = resolve_tex(sname, tex)
            if not tp:
                n_miss += 1
                miss_names.append(sname)
                continue
            texobj = unreal.load_asset(tp)
            mi = slot.get_editor_property("material_interface")
            cur = None
            if mi:
                try:
                    cur = MEL.get_material_instance_texture_parameter_value(mi, unreal.Name("Tex"))
                except Exception:
                    cur = None
            if cur and cur.get_path_name() == texobj.get_path_name():
                n_ok += 1
                continue
            if isinstance(mi, unreal.MaterialInstanceConstant):
                # авто-материал Interchange (имя = имя слота): чиним на месте — parent + Tex.
                mi.set_editor_property("parent", master)
                MEL.set_material_instance_texture_parameter_value(mi, "Tex", texobj)
                MEL.set_material_instance_scalar_parameter_value(mi, "Ambient", 1.0)
                mi.modify()
                EAL.save_loaded_asset(mi)
                n_fixed += 1
            else:
                # не MIC (обычный Material) — реперентить нельзя: создаём MI_<tex> в папке модели.
                mdir = model_dir(pkg)
                texname = tp.rsplit("/", 1)[-1]
                micpath = "%s/Materials/MI_%s" % (mdir, texname)
                mic = unreal.load_asset(micpath) if EAL.does_asset_exist(micpath) else \
                    ATOOLS.create_asset("MI_%s" % texname, "%s/Materials" % mdir,
                                        unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
                MEL.set_material_instance_parent(mic, master)
                MEL.set_material_instance_texture_parameter_value(mic, "Tex", texobj)
                MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
                EAL.save_loaded_asset(mic)
                slot.set_editor_property("material_interface", mic)
                changed = True
                n_fixed += 1
        if changed:
            mesh.set_editor_property(prop, mats)
            EAL.save_loaded_asset(mesh)

    unreal.log("[hairmat] ГОТОВО: мешей=%d слотов=%d исправлено=%d уже_ок=%d без_текстуры=%d"
               % (n_mesh, n_slot, n_fixed, n_ok, n_miss))
    for nm in sorted(set(miss_names)):
        unreal.log_warning("[hairmat] текстура не найдена для слота: %s" % nm)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
