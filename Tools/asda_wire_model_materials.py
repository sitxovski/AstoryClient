# Copyright (c) 2026 @sitxovski (Discord)
# asda_wire_model_materials.py — привязка текстур к импортированным моделям (StaticMesh).
# .glb-материалы имеют только ИМЯ (= имя текстуры .jbm), без текстуры → UE ставит серый дефолт.
# Скрипт: имя слота меша → ищет Texture2D с тем же именем → создаёт MaterialInstanceConstant мастера
# M_AsdaObject (Tex=текстура) → назначает на слот. Запуск: в редакторе Tools -> Execute Python Script.
# Предварительно: текстуры персонажей/мобов импортированы (asda_extract_archive_textures.py + re-run import).
import unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_login_guard as _LG        # ★ 15.09.2026: модели сцены логина пропускаются без ASDA_ALLOW_LOGIN_WRITE=1
import asda_model_import_ue as MI     # ★ 15.09.2026: MI.DATA_PINNED_TEX — модели с текстурой по данным не перевязывать

MODELS_ROOT = "/Game/AsdaAssets/Models"
TEX_ROOT    = "/Game/AsdaAssets/Textures"
MASTER      = "/Game/Materials/M_AsdaObject"
# ★ D5 (волна 2 п. 7, 14.09.2026, сверка с Content): папки /Game/AsdaAssets/Models/_Materials нет (раскладка до asda_assign_materials); путь не менялся.
MIC_DIR     = "/Game/AsdaAssets/Models/_Materials"

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

def build_tex_index():
    idx = {}
    for ad in AR.get_assets_by_path(unreal.Name(TEX_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) == "Texture2D":
            idx[str(ad.asset_name).lower()] = str(ad.package_name)
    unreal.log("Текстур в индексе: %d" % len(idx))
    return idx

def main():
    master = unreal.load_asset(MASTER)
    if not master:
        unreal.log_warning("Нет мастер-материала %s" % MASTER); return
    tex_idx = build_tex_index()
    mic_cache = {}

    def get_mic(texname):
        key = texname.lower()
        if key in mic_cache: return mic_cache[key]
        pkg = tex_idx.get(key)
        if not pkg:
            mic_cache[key] = None; return None
        tex = unreal.load_asset(pkg)
        micpath = "%s/MI_%s" % (MIC_DIR, texname)
        mic = unreal.load_asset(micpath) if EAL.does_asset_exist(micpath) else \
              ATOOLS.create_asset("MI_%s" % texname, MIC_DIR, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mic, master)
        MEL.set_material_instance_texture_parameter_value(mic, "Tex", tex)
        EAL.save_loaded_asset(mic)
        mic_cache[key] = mic
        return mic

    meshes = [p for p in EAL.list_assets(MODELS_ROOT, recursive=True)
              if not p.startswith(MIC_DIR)]
    n_mesh = n_slot = n_wired = 0
    for p in meshes:
        sm = unreal.load_asset(p)
        if not isinstance(sm, unreal.StaticMesh): continue
        if _LG.login_package_blocked(p, "asda_wire_model_materials.py"): continue   # ★ 15.09: сцена логина
        if MI.data_pinned_dir(p):                                                   # ★ 15.09: текстура по данным
            unreal.log_warning("[DATA-PIN] asda_wire_model_materials.py: пропуск %s — текстура по данным" % p); continue
        n_mesh += 1
        mats = sm.get_editor_property("static_materials")
        changed = False
        for s, slot in enumerate(mats):
            n_slot += 1
            name = str(slot.get_editor_property("material_slot_name"))
            mic = get_mic(name)
            if mic:
                slot.set_editor_property("material_interface", mic)
                changed = True; n_wired += 1
        if changed:
            sm.set_editor_property("static_materials", mats)
            EAL.save_loaded_asset(sm)
    unreal.log("Готово: мешей=%d, слотов=%d, привязано=%d" % (n_mesh, n_slot, n_wired))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
