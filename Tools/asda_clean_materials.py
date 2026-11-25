# Copyright (c) 2026 @sitxovski (Discord)
# asda_clean_materials.py — чистка материалов в папках моделей.
# В <model>/Materials/ соседствуют наши MIC (префикс MI_) и серые авто-материалы Interchange (без префикса).
# Правило:
#   1) удалить безпрефиксный 'Z', ЕСЛИ в той же папке есть 'MI_Z' (это серый авто-дубль, меш уже на MI_Z);
#   2) переименовать 'MI_Z' -> 'Z' (hard-ссылки мешей следуют за переименованием объекта);
#   3) починить/удалить редиректоры (чтобы не осталось MI_-призраков), сохранить.
# НЕ трогаем безпрефиксные без MI_-копии (напр. мультимеш-сиблинги _001.._006 — отдельный случай).
# Запуск: run_headless.bat ...\asda_clean_materials.py
import os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U
import asda_login_guard as _LG   # ★ 15.09.2026: материалы сцены логина (Models/Char/cre_creback-a_00, cre_creation01*) — только ASDA_ALLOW_LOGIN_WRITE=1
import unreal

MODELS_ROOT = _U.G_MODELS
# ★ Охват: ASDA_ONLY_MODELS="Char/имя1,Mob/имя2" — чистить ТОЛЬКО папки этих моделей
#   (<корень>/<модель>/Materials). Голое имя совпадёт в любом корне.
ONLY = set(s.strip() for s in os.environ.get("ASDA_ONLY_MODELS", "").split(",") if s.strip())


def _scope(folder_pkg):
    """folder_pkg = …/Models/<корень>/<модель>/Materials"""
    if not ONLY: return True
    p = folder_pkg.split("/")
    return len(p) >= 3 and p[-1] == "Materials" and (p[-2] in ONLY or "/".join(p[-3:-1]) in ONLY)
EAL = unreal.EditorAssetLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()

def main():
    # 1) собрать материалы по папкам
    by_folder = {}
    n_login = 0   # ★ 15.09: пропущено материалов сцены логина
    for ad in AR.get_assets_by_path(unreal.Name(MODELS_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) in ("Material", "MaterialInstanceConstant"):
            pkg = str(ad.package_name)
            folder, _, name = pkg.rpartition("/")
            if not _scope(folder): continue            # ★ охват
            if _LG.login_package_blocked(folder, "asda_clean_materials.py", quiet=True):   # ★ 15.09: сцена логина
                n_login += 1; continue
            by_folder.setdefault(folder, {})[name] = pkg
    unreal.log("Папок с материалами: %d" % len(by_folder))
    if n_login:
        unreal.log_warning("[LOGIN-GUARD] asda_clean_materials.py: пропущено материалов сцены логина: %d (ASDA_ALLOW_LOGIN_WRITE=1)" % n_login)

    deleted = renamed = collide = 0
    for folder, mats in by_folder.items():
        # (1) удалить безпрефиксный Z, если есть MI_Z.
        #     ★ Сравнение БЕЗ УЧЁТА РЕГИСТРА: имя MIC берётся от ассета текстуры (mon_s_0_greengnom01-a_00),
        #     а имя авто-материала — от строки материала в .jbm (mon_s_0_GreenGnom01-a_00). Регистр
        #     расходится, точное сравнение их не сводило → 38 «коллизий» и MI_-хвосты в папках.
        low = dict((n.lower(), n) for n in mats)
        for name in list(mats):
            if name.startswith("MI_"): continue
            src = low.get(("MI_" + name).lower())
            if src and src in mats:
                if EAL.delete_asset(mats[name]):
                    deleted += 1; del mats[name]
        # (2) переименовать MI_Z -> Z
        for name in list(mats):
            if name.startswith("MI_"):
                tgt = name[3:]
                tgtpkg = "%s/%s" % (folder, tgt)
                if EAL.does_asset_exist(tgtpkg):
                    collide += 1   # цель занята (безпрефиксный без MI_-копии не удалялся) — пропуск
                    continue
                if EAL.rename_asset(mats[name], tgtpkg):
                    renamed += 1

    # (3) починить редиректоры (обновит ссылки + удалит MI_-призраки)
    redirs = [unreal.load_asset(str(ad.package_name))
              for ad in AR.get_assets_by_path(unreal.Name(MODELS_ROOT), recursive=True)
              if str(ad.asset_class_path.asset_name) == "ObjectRedirector"
              and _scope(str(ad.package_name).rsplit("/", 1)[0])]
    if redirs:
        try: ATOOLS.fixup_referencers(redirs)
        except Exception as e: unreal.log_warning("fixup_referencers: %s" % str(e)[:60])
    # ★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): папку
    #   /Game/AsdaAssets/Models ЦЕЛИКОМ не сохраняем — под ней задник сцены логина (Models/Backdrop),
    #   шкатулка cre_creation01* и cre_creback-a_00. БЫЛО: из ПРАВКИ они отсеяны выше (login_package_blocked),
    #   а save_directory по всей папке пересохранял их всё равно — мимо защиты.
    _LG.save_dir_login_safe(MODELS_ROOT, "asda_clean_materials.py")
    unreal.log("ГОТОВО: удалено авто=%d, переименовано MI_->без префикса=%d, коллизий(пропущено)=%d, редиректоров=%d"
               % (deleted, renamed, collide, len(redirs)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
