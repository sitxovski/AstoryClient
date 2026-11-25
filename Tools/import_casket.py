# Copyright (c) 2026 @sitxovski (Discord)
# Импорт шкатулки cre_creation01-a_00.glb (SkeletalMesh+6 клипов) + её текстуры в проект.
# Запуск: run_headless.bat import_casket.py
import unreal, importlib.util, os

HERE = os.path.dirname(os.path.abspath(__file__))
GLB  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "cre_creation01-a_00.glb"))
TEXSRC = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Textures", "Data", "Character", "Texture"))
DEST = "/Game/AsdaAssets/Models/Char"
SUB  = DEST + "/cre_creation01-a_00"
TEXDEST = "/Game/AsdaAssets/Textures/Characters/Texture"

def load_importer():
    spec = importlib.util.spec_from_file_location("imp", os.path.join(HERE, "asda_import_skinned_models.py"))
    m = importlib.util.module_from_spec(spec); import sys; sys.modules["imp"] = m
    spec.loader.exec_module(m); return m

def log(m): unreal.log("[import_casket] " + str(m))
EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, отчёт Reports/dedup_audit/tex_rebind_2026-09-15.md): перезаливает текстуры
    #   шкатулки, сносит ВСЮ папку Models/Char/cre_creation01-a_00 (клипы открытия/поворота — AsdaLoginGameMode.cpp,
    #   GCasketOpenAnimPath) и перевязывает её MIC. Только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1.
    import sys as _sys
    _sys.path.insert(0, HERE)
    from asda_login_guard import forbid_login_write
    forbid_login_write("import_casket.py", "Models/Char/cre_creation01-a_00 (удаление папки, переимпорт, MIC) и текстуры шкатулки")
    imp = load_importer()
    # 1) текстуры шкатулки (если ещё не в проекте)
    texfiles = [os.path.join(TEXSRC, "cre_creation01-a_0%d.png" % i) for i in range(4)]
    texfiles = [f for f in texfiles if os.path.exists(f)]
    import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (путь Tools вставлен выше)
    _MIU.import_batch(texfiles, TEXDEST)
    log("текстур импортировано: %d" % len(texfiles))
    # 2) снести старый + импорт glb
    if EAL.does_directory_exist(SUB):
        EAL.delete_directory(SUB)
    imp.import_batch([GLB], DEST)
    # 3) материалы + nanite off
    tex = imp.tex_index()
    master = unreal.load_asset(imp.MASTER)
    mats = imp.wire_materials(SUB, tex, master)
    imp.disable_nanite(SUB)
    log("материалов привязано: %d" % mats)
    EAL.save_directory(SUB, only_if_is_dirty=False, recursive=True)
    for ad in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(unreal.Name(SUB), recursive=True):
        log("  ассет: %s (%s)" % (ad.asset_name, ad.asset_class_path.asset_name))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
