# Copyright (c) 2026 @sitxovski (Discord)
# asda_refresh_mesh_ddc_key.py — СБРОС ОТРАВЛЕННЫХ ЗАПИСЕЙ DDC у StaticMesh'ей.
#
# ★★★ ЗАЧЕМ. Снять `bDoFastBuild` мало. «Быстрая» ветка FStaticMeshRenderData::Cache не считает
#   RenderData->Bounds, НО результат всё равно кладётся в DDC: блок `GetCache().Put(...)`
#   (StaticMesh.cpp:4448-4457) стоит ПОСЛЕ if/else и общий для обеих веток. Ключ
#   (BuildStaticMeshDerivedDataKeySuffix, стр. 3727) флаг `bDoFastBuild` не учитывает, поэтому меш с
#   уже снятым флагом попадает в ТУ ЖЕ запись и снова поднимает мусорные габариты — что и наблюдалось
#   на куке после первого этапа лечения (679 мешей с NaN при полностью снятых флагах).
#
#   Лечение: перекоммитить mesh-description LOD0 с bUseHashAsGuid=True. В ключ входит
#   `GetMeshDescriptionBulkData()->GetUnversionedIdString()` (стр. 3825); коммит меняет его на ХЕШ
#   СОДЕРЖИМОГО ⇒ ключ другой (отравленная запись не находится) и при этом стабильный: повторный
#   прогон ключ уже не двигает. Геометрия не меняется — записывается то же самое описание.
#
# Запуск: headless. ASDA_ROOTS="/Game/Map||/Game/AsdaAssets/Models/Backdrop", ASDA_DRYRUN=1.
import os
import sys as _sys
import unreal

_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_model_import_ue as _MIU   # noqa: E402  ★ дедуп №2 Т19: общий отбор StaticMesh по пакетам


# ★ 15.09.2026. AssetData StaticMesh из списка пакетов — общая реализация asda_model_import_ue (дедуп №2 Т19).
_loose_static_meshes = _MIU.static_meshes_in_packages


def _refresh_loose(pkgs, dry):
    """★ 15.09.2026. Поштучный вариант UAsdaMeshMaintenance::RefreshDerivedDataKeysInPaths (AsdaMeshMaintenance.cpp) для пакетов,
    лежащих прямо в папке с пакетами логина: попутное снятие bDoFastBuild (clear_fast_build_static_mesh без пересборки —
    те же Modify + сброс флага), перекоммит описания (refresh_derived_data_key) и сохранение. Строка итога — в формате C++."""
    mm = unreal.AsdaMeshMaintenance
    seen = refreshed = saved = errors = flags = 0
    for ad in _loose_static_meshes(pkgs):
        seen += 1
        mesh = ad.get_asset()
        if mesh is None:
            errors += 1
            continue
        if dry:
            continue
        if mm.is_fast_build_static_mesh(mesh):
            mm.clear_fast_build_static_mesh(mesh, False)
            flags += 1
        if not mm.refresh_derived_data_key(mesh):
            errors += 1
            continue
        refreshed += 1
        if unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
            saved += 1
        else:
            errors += 1
            unreal.log_error("[ddckey] не сохранён пакет %s" % ad.package_name)
    return ("просмотрено %d, ключ обновлён %d, попутно снят флаг %d, сохранено %d, ошибок %d"
            % (seen, refreshed, flags, saved, errors))


def main():
    roots = os.environ.get("ASDA_ROOTS", "/Game/Map||/Game/AsdaAssets/Models/Backdrop")
    roots = [r.strip() for r in roots.split("||") if r.strip()]
    # ★ 15.09.2026: C++ обходит корень целиком, пакет не пропустить — без ASDA_ALLOW_LOGIN_WRITE=1 корни сцены логина убираются.
    #   ★ Доработка 15.09: корень, СОДЕРЖАЩИЙ пакеты логина, больше не пропускается целиком — login_expand_roots разворачивает его
    #   в чистые подпапки (их обходит C++) и пакеты, лежащие прямо в «смешанной» папке (их обрабатывает _refresh_loose поштучно).
    import sys as _sys_lg   # ★ 15.09.2026: защита сцены логина (asda_login_guard: Models/Backdrop, Char/cre_creback-a_00, cre_creation01* и др.)
    _sys_lg.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_login_guard as _LG
    roots, loose = _LG.login_expand_roots(roots, "asda_refresh_mesh_ddc_key.py")
    if not roots and not loose:
        unreal.log_warning("[ddckey] корней не осталось (сцена логина) — ничего не делаю")
        return
    dry = os.environ.get("ASDA_DRYRUN", "0") == "1"
    unreal.log("[ddckey] корни: %s, dry_run=%s" % (roots, dry))
    if roots:
        report = unreal.AsdaMeshMaintenance.refresh_derived_data_keys_in_paths(roots, dry)
        unreal.log("[ddckey] ИТОГ: %s" % report)
    if loose:
        unreal.log("[ddckey] ИТОГ поштучно (%d пакетов из папок с пакетами логина): %s" % (len(loose), _refresh_loose(loose, dry)))


if __name__ == "__main__":
    main()
