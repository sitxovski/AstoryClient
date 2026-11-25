# Copyright (c) 2026 @sitxovski (Discord)
# Точечный импорт login-фона cre_creback-a_00.glb → SkeletalMesh+AnimSequence (заменяет неверный статик).
# Запуск: run_headless.bat import_creback.py  (или UnrealEditor-Cmd ... -ExecutePythonScript=...)
import unreal, importlib.util, os

HERE = os.path.dirname(os.path.abspath(__file__))
GLB  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "cre_creback-a_00.glb"))
DEST = "/Game/AsdaAssets/Models/Char"
SUB  = DEST + "/cre_creback-a_00"

def load_importer():
    spec = importlib.util.spec_from_file_location("imp", os.path.join(HERE, "asda_import_skinned_models.py"))
    m = importlib.util.module_from_spec(spec)
    import sys; sys.modules["imp"] = m
    spec.loader.exec_module(m)
    return m

def log(m): unreal.log("[import_creback] " + str(m))

EAL = unreal.EditorAssetLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, отчёт Reports/dedup_audit/tex_rebind_2026-09-15.md): шаг 1 сносит ВСЮ папку
    #   Models/Char/cre_creback-a_00 (в ней клип cre_creback-a_00_Anim — его грузит AsdaLoginGameMode.cpp, GBackdropAnimPath),
    #   шаги 2–4 переимпортируют и перевязывают её MIC. Только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1.
    import sys as _sys
    _sys.path.insert(0, HERE)
    from asda_login_guard import forbid_login_write
    forbid_login_write("import_creback.py", "Models/Char/cre_creback-a_00 (удаление папки, переимпорт, MIC)")
    imp = load_importer()
    # 1) снести старый (неверный статик) cre_creback
    if EAL.does_directory_exist(SUB):
        EAL.delete_directory(SUB)
        log("удалён старый статик " + SUB)
    # 2) импорт .glb (Interchange glTF → SkeletalMesh+Skeleton+AnimSequence)
    log("импорт " + GLB)
    imp.import_batch([GLB], DEST)
    # 3) материалы (текстуры по имени) + отключить Nanite (скелетке не нужен)
    tex = imp.tex_index()
    master = unreal.load_asset(imp.MASTER)
    mats = imp.wire_materials(SUB, tex, master)
    imp.disable_nanite(SUB)
    log("материалов привязано: %d" % mats)
    # 4) сохранить
    EAL.save_directory(SUB, only_if_is_dirty=False, recursive=True)
    # 5) отчёт: что импортировалось
    for ad in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(unreal.Name(SUB), recursive=True):
        log("  ассет: %s (%s)" % (ad.asset_name, ad.asset_class_path.asset_name))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
