# Copyright (c) 2026 @sitxovski (Discord)
# asda_clear_fastbuild_flag.py — СНЯТИЕ `UStaticMesh::bDoFastBuild` у уже импортированных мешей.
#
# ★★★ ЗАЧЕМ. Пока флаг взведён, FStaticMeshRenderData::Cache (StaticMesh.cpp:4321) на КАЖДОЙ сборке
#   рендер-данных идёт «быстрой» веткой мимо DDC и мимо присваивания RenderData->Bounds
#   (поле не инициализируется в конструкторе) ⇒ в кук уходит мусор, у части мешей — NaN
#   («FStaticMeshRenderData::Serialize found NaN in Bounds of ...»).
#
# ★★ ПОЧЕМУ НЕ PYTHON. Свойство `bDoFastBuild` не выставлено скриптингу
#   («Property 'bDoFastBuild' ... is protected and cannot be read»), а обходной путь через
#   PostEditChangeProperty(NaniteSettings) не работает: Python пишет свойство в режиме
#   EPropertyAccessChangeNotifyMode::Default («уведомлять, только если значение реально изменилось»),
#   а структура, полученная из get_editor_property, правится ПО МЕСТУ — к моменту записи новое
#   значение равно старому, уведомления нет, флаг остаётся. Поэтому работу делает C++-утилита
#   UAsdaMeshMaintenance (модуль AsdaImporters, editor-only).
#
# Запуск: headless.
#   ASDA_ROOTS="/Game/Map||/Game/AsdaAssets/Models/Backdrop" — корни обхода (по умолчанию эти).
#   ASDA_DRYRUN=1 — только посчитать, ничего не менять.
#   ASDA_REBUILD=1 — дополнительно пересобрать каждый меш прямо сейчас (обычно не нужно:
#                    со снятым флагом обычная ветка Cache соберёт габариты сама при куке).
import os
import sys as _sys
import unreal

_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_model_import_ue as _MIU   # noqa: E402  ★ дедуп №2 Т19: общий отбор StaticMesh по пакетам


# ★ 15.09.2026. AssetData StaticMesh из списка пакетов — общая реализация asda_model_import_ue (дедуп №2 Т19).
_loose_static_meshes = _MIU.static_meshes_in_packages


def _clear_loose(pkgs, dry, rebuild):
    """★ 15.09.2026. Поштучный вариант UAsdaMeshMaintenance::ClearFastBuildInPaths (AsdaMeshMaintenance.cpp) для пакетов,
    лежащих прямо в папке с пакетами логина: тот же отбор (флаг взведён), то же снятие (clear_fast_build_static_mesh)
    и сохранение пакета. Возвращает строку итога в формате C++."""
    mm = unreal.AsdaMeshMaintenance
    seen = was = cleared = saved = errors = 0
    for ad in _loose_static_meshes(pkgs):
        seen += 1
        mesh = ad.get_asset()
        if mesh is None:
            errors += 1
            continue
        if not mm.is_fast_build_static_mesh(mesh):
            continue
        was += 1
        if dry:
            continue
        if not mm.clear_fast_build_static_mesh(mesh, rebuild):
            errors += 1
            continue
        cleared += 1
        if unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
            saved += 1
        else:
            errors += 1
            unreal.log_error("[fastbuild] не сохранён пакет %s" % ad.package_name)
    return "просмотрено %d, было взведено %d, снято %d, сохранено %d, ошибок %d" % (seen, was, cleared, saved, errors)


def main():
    roots = os.environ.get("ASDA_ROOTS", "/Game/Map||/Game/AsdaAssets/Models/Backdrop")
    roots = [r.strip() for r in roots.split("||") if r.strip()]
    # ★ 15.09.2026: C++ обходит корень целиком, пакет не пропустить — без ASDA_ALLOW_LOGIN_WRITE=1 корни сцены логина убираются.
    #   ★ Доработка 15.09: корень, СОДЕРЖАЩИЙ пакеты логина, больше не пропускается целиком — login_expand_roots разворачивает его
    #   в чистые подпапки (их обходит C++) и пакеты, лежащие прямо в «смешанной» папке (их обрабатывает _clear_loose поштучно).
    import sys as _sys_lg   # ★ 15.09.2026: защита сцены логина (asda_login_guard: Models/Backdrop, Char/cre_creback-a_00, cre_creation01* и др.)
    _sys_lg.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_login_guard as _LG
    roots, loose = _LG.login_expand_roots(roots, "asda_clear_fastbuild_flag.py")
    if not roots and not loose:
        unreal.log_warning("[fastbuild] корней не осталось (сцена логина) — ничего не делаю")
        return
    dry = os.environ.get("ASDA_DRYRUN", "0") == "1"
    rebuild = os.environ.get("ASDA_REBUILD", "0") == "1"
    unreal.log("[fastbuild] корни: %s, dry_run=%s, rebuild=%s" % (roots, dry, rebuild))
    if roots:
        report = unreal.AsdaMeshMaintenance.clear_fast_build_in_paths(roots, dry, rebuild)
        unreal.log("[fastbuild] ИТОГ: %s" % report)
    if loose:
        unreal.log("[fastbuild] ИТОГ поштучно (%d пакетов из папок с пакетами логина): %s" % (len(loose), _clear_loose(loose, dry, rebuild)))


if __name__ == "__main__":
    main()
