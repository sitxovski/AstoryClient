# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Починка материалов БАЗОВОГО тела превью char-select.
# Симптом: у персонажа не видно рук. По коду сборки (AsdaCharAssembly.cpp) руки несёт базовое
# тело (chst001), перчатки — только оверлей. Меши и текстуры целы, значит проверяем слоты
# материалов базового превью-меша: пустой слот = невидимая секция.
import unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
AR = unreal.AssetRegistryHelpers.get_asset_registry()
MASTER = _U.M_OBJECT_LIT  # ★ ПЕРСОНАЖИ = LIT (unlit-мастер давал пересвет лица/тела vs Lit-экип; см. face_overbright_fix.md)
TEX_DIRS = (_U.G_TEX_CHAR_TEXTURE,
            _U.G_TEX_CHARACTERS)

def log(m): unreal.log("[fix-body] " + str(m))

# 1) находим превью-меши тела
cands = []

tex_index = {}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for a in AR.get_assets_by_path("/Game/AsdaAssets/Models", recursive=True):
        n = str(a.asset_name).lower()
        if "cremmy" in n and "preview" in n and "skeleton" not in n and "anim" not in n:
            cands.append(str(a.package_name))
    log("найдено превью-мешей: %d" % len(cands))
    for c in sorted(cands): log("   " + c)
    for d in TEX_DIRS:
        if EAL.does_directory_exist(d):
            for p in EAL.list_assets(d, recursive=True, include_folder=False):
                tex_index[p.split("/")[-1].split(".")[0].lower()] = p
    master = EAL.load_asset(MASTER)
    fixed = 0
    for path in sorted(cands):
        sk = EAL.load_asset(path)
        if not sk:
            log("%s: не загрузился" % path); continue
        try:
            mats = sk.get_editor_property("materials")
        except Exception as e:
            log("%s: слоты прочитать не удалось: %s" % (path, e)); continue
        log("%s: слотов = %d" % (path.split('/')[-1], len(mats)))
        for i, sm in enumerate(mats):
            mi = sm.material_interface
            slot = str(sm.material_slot_name)
            cur_tex = None
            if mi is not None:
                try:
                    cur_tex = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
                except Exception:
                    pass
            log("   слот %d '%s': материал=%s Tex=%s" % (
                i, slot,
                mi.get_path_name().split('/')[-1] if mi else "ПУСТО",
                cur_tex.get_path_name().split('/')[-1] if cur_tex else "ПУСТО"))
            if mi is not None and cur_tex is not None:
                continue                          # слот в порядке
            # ищем текстуру по имени слота
            tex = tex_index.get(slot.lower())
            if tex is None:
                for key, p in tex_index.items():
                    if slot.lower() and slot.lower() in key:
                        tex = p; break
            if tex is None:
                log("      текстуры для слота не нашлось — пропуск"); continue
            texobj = EAL.load_asset(tex)
            mic_dir = path.rsplit("/", 1)[0]
            mic_name = "MI_%s" % slot
            mic_path = "%s/%s" % (mic_dir, mic_name)
            mic = EAL.load_asset(mic_path)
            if mic is None:
                mic = ATOOLS.create_asset(mic_name, mic_dir, unreal.MaterialInstanceConstant,
                                          unreal.MaterialInstanceConstantFactoryNew())
            MEL.set_material_instance_parent(mic, master)
            MEL.set_material_instance_texture_parameter_value(mic, "Tex", texobj)
            try:
                MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
            except Exception:
                pass
            sk.set_material(i, mic)
            EAL.save_asset(mic.get_path_name())
            EAL.save_asset(path)
            fixed += 1
            log("      ИСПРАВЛЕНО: слот %d -> %s (Tex=%s)" % (i, mic_name, tex.split('/')[-1]))
    log("исправлено слотов: %d" % fixed)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
