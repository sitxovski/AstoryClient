# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_df_build_settings.py — ★ Сет E-1 (06.09.2026): Mesh Distance Fields у ГОТОВЫХ StaticMesh карт (headless-редактор, -NullRHI).
#   Настройки DF (`FMeshBuildSettings::DistanceFieldResolutionScale` / `bGenerateDistanceFieldAsIfTwoSided`) и состояние DF
#   из Python не читаются (SourceModels — приватный UPROPERTY; DistanceFieldData — рендер-данные) ⇒ всё через C++
#   UAsdaMeshMaintenance (Source/AsdaImporters/.../AsdaMeshMaintenance.h): get_distance_field_info / set_distance_field_build_settings /
#   wait_for_distance_field_builds. Правила те же, что у билдера уровней (asda_build_map_level.df_settings_for):
#   SM_terrain → scale ASDA_DF_TERRAIN_SCALE (2.0: 259 вокселей ≈ 198 юн/воксель на 51 200 юн; при 1.0 — 126 ≈ 406; замер Э10), листва
#   (asda_foliage) → «как двусторонний», прочие — 1.0/false (не трогаются).
# Режимы (ASDA_DF_MODE):
#   e10     — эксперимент Э10 на одной карте (ASDA_MAPS=mine): per-mesh bGenerateMeshDistanceField=True (проектный ключ НЕ нужен:
#             StaticMesh.cpp:4564 `CVar!=0 || Owner->bGenerateMeshDistanceField`). Флаг пишется C++ БЕЗ PostEditChangeProperty
#             (set_editor_property → Build на каждом меше: сеанс 06.09 завис на террейне в FinishCompilation 30 мин — прогон убит по
#             простою) и пакеты сохраняются; сама сборка DF — ОТДЕЛЬНЫМ сеансом режима info (загрузка → PostLoad → Cache → фоновая
#             Embree-сборка → wait с пульсом в лог) — ровно тот путь, каким DF строится и при проектном ключе. Для чистоты после Э10
#             ассеты возвращаются из бэкапа драйвером (при проектном ключе флаг игнорируется — мусор в P4).
#   dry     — только посчитать, что бы изменилось (ничего не пишется).
#   apply   — записать настройки DF по правилам + сохранить пакеты (бэкап .uasset в ASDA_DF_BACKUP до записи; read-only P4 —
#             снять + путь в _readonly_list.txt). Сборка DF здесь НЕ запускается (bRebuild=false): ключ DDC меша меняется, рендер-данные
#             пересоберутся при следующей загрузке/прогреве DDC. ASDA_DF_VERIFY1=1 — первый листвяной меш пересобрать сразу (bRebuild=true)
#             и сверить габариты рендер-данных до/после (правило map-ue-export-pipeline: пересборка не должна портить bounds).
#   info    — загрузить меши карт и снять состояние DF (после включения проектного ключа загрузка сама строит недостающие DF —
#             wait) → JSON со счётчиками valid/пусто/байты; ASDA_DF_LIMIT=N — только первые N мешей карты.
# ★ Сет E-2 (06.09.2026): ASDA_DF_PATHS="/Game/AsdaAssets/Models/Char||/Game/AsdaAssets/Models/Items||…" — вместо карт обходятся StaticMesh
#   под этими путями (реестр ассетов, рекурсивно; одна «карта» на путь, имя = последний сегмент). Правило для них — класс «nodf»:
#   DistanceFieldResolutionScale = 0.0 (DF не строится: MeshDistanceFieldUtilities.cpp:602 `if (scale > 0)`; поле пустое, IsValid()=false,
#   примитив вне DF-сцены). Это ЕДИНСТВЕННЫЙ per-mesh выключатель при проектном r.GenerateMeshDistanceFields=1 — флаг
#   bGenerateMeshDistanceField при нём игнорируется (StaticMesh.cpp:4564). Зачем: review_setE1.md В-1 — ≈3.5 ГБ из 7.16 ГБ DF были
#   ch1_*/itm*/mon_* (оружие/предметы/мобы-статики, все 126³) — DFAO/DF-тени от них не нужны. В apply-JSON теперь пишутся bounds
#   до/после (М-3 ревизии). Режим info по путям = загрузка → пересборка рендер-данных с новым ключом DDC → проверка valid=false.
# Окружение: ASDA_MAPS="alpen||mine" (asda_maps.select_maps; пусто = ВСЕ карты), ASDA_DF_OUT=<json>, ASDA_DF_BACKUP=<dir>,
#   ASDA_DF_TERRAIN_SCALE=2.0, ASDA_DF_WAIT=<сек, умолч. 7200>.
# Запуск: python Tools/run_headless_safe.py Tools/asda_df_build_settings.py --marker DF-SETTINGS-FINISHED --timeout 14400 --idle 3600
import json
import os
import shutil
import stat
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_foliage as _FOL
import asda_login_guard as _LG   # ★ 15.09.2026: меши сцены логина — только с ASDA_ALLOW_LOGIN_WRITE=1

MODE = os.environ.get("ASDA_DF_MODE", "dry").lower()
OUT = os.environ.get("ASDA_DF_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/setE1_df_%s.json" % MODE)
# ★ Т3 (11.10.2026): ASDA_DF_BACKUP=<папка> — явная папка бэкапа в ПРЕЖНЕЙ раскладке (путь от корня проекта: <папка>\Content\…;
#   её так читают драйверы сетов E1/E2 в Reports/render_sets_2026-09-05). Не задана (None) — единый корень:
#   asda_headless_tool.backup_file, набор @@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_df_settings\<путь от @@ASTORY_DEV_ROOT@@>
#   (был прошитый AstoryClient\_backup\2026-09-06_setE1).
BACKUP = os.environ.get("ASDA_DF_BACKUP") or None
BACKUP_SET = "df_settings"   # имя набора в @@ASTORY_DEV_ROOT@@\_backup при незаданном ASDA_DF_BACKUP
TERRAIN_SCALE = float(os.environ.get("ASDA_DF_TERRAIN_SCALE", "2.0"))
VERIFY1 = os.environ.get("ASDA_DF_VERIFY1", "") == "1"
LIMIT = int(os.environ.get("ASDA_DF_LIMIT", "0") or 0)
WAIT_SEC = float(os.environ.get("ASDA_DF_WAIT", "7200"))
# ★ Сет E-2: пути (не карты) — StaticMesh под ними получают scale 0.0 (класс nodf)
PATHS = [x.strip() for x in os.environ.get("ASDA_DF_PATHS", "").split("||") if x.strip()]
CONTENT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content"
def backup_dir():
    """Папка набора бэкапа этого запуска: ASDA_DF_BACKUP или @@ASTORY_DEV_ROOT_ESC@@\\_backup\\<дата>_df_settings (★ Т3; дата — при вызове,
    не при импорте)."""
    if BACKUP:
        return BACKUP
    import asda_headless_tool as HT
    return os.path.join(HT.BACKUP_ROOT, "%s_%s" % (time.strftime("%Y-%m-%d"), BACKUP_SET))


def readonly_list():
    """Список снятых read-only P4 (для reconcile) — `_readonly_list.txt` в папке набора бэкапа (как прежний READONLY_LIST)."""
    return os.path.join(backup_dir(), "_readonly_list.txt")


def rule_for(asset_name, nodf=False):
    """(scale, two_sided, класс) по имени ассета — единые правила с билдером уровней. ★ E-2: nodf=True → (0.0, False, "nodf")."""
    n = str(asset_name)
    if nodf:
        return 0.0, False, "nodf"
    if n.lower() == "sm_terrain":
        return TERRAIN_SCALE, False, "terrain"
    if _FOL.is_foliage_asset(n):
        return 1.0, True, "foliage"
    return 1.0, False, "other"


def asset_file(package_name):
    """Путь .uasset пакета /Game/... на диске."""
    return CONTENT + package_name[len("/Game"):] + ".uasset"


def backup_and_writable(package_name):
    """Копия .uasset в бэкап (до первой записи) + снятие read-only P4 (путь — в список для reconcile). Возвращает True, если файл есть."""
    src = asset_file(package_name)
    if not os.path.isfile(src):
        return False
    if BACKUP:
        # прежняя раскладка явной папки (путь от корня проекта) — её читают драйверы сетов E
        dst = os.path.join(BACKUP, os.path.relpath(src, r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient"))
        if not os.path.isfile(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            if os.path.getsize(dst) <= 0:
                raise RuntimeError("пустой бэкап %s" % dst)
    else:
        import asda_headless_tool as HT   # ★ Т3: единая функция, первая копия набора не перезаписывается, сверка размера
        if HT.backup_file(src, BACKUP_SET, log=lambda _m: None) is None:
            raise RuntimeError("нет файла для бэкапа %s" % src)
    if not os.access(src, os.W_OK):
        os.chmod(src, stat.S_IWRITE | stat.S_IREAD)
        rl = readonly_list()
        os.makedirs(os.path.dirname(rl), exist_ok=True)
        with open(rl, "a", encoding="utf-8") as fh:
            fh.write(os.path.relpath(src, r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient").replace("\\", "/") + "\n")
    return True


def list_static_meshes(map_asset, paths=None):
    """[(package_name, asset_name)] StaticMesh под /Game/Map/<map>/Models и /Terrain (реестр ассетов, отсортировано).
    ★ E-2: paths=[...] — вместо карты обойти эти пути реестра (рекурсивно)."""
    import unreal
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    res = []
    n_login = 0   # ★ 15.09.2026: пропущено мешей сцены логина
    for path in (paths if paths else ["/Game/Map/%s/%s" % (map_asset, sub) for sub in ("Models", "Terrain")]):
        for ad in ar.get_assets_by_path(path, True):
            try:
                cls = str(ad.asset_class_path.asset_name)
            except Exception:
                cls = str(ad.asset_class)
            if cls != "StaticMesh":
                continue
            if _LG.login_package_blocked(str(ad.package_name), "asda_df_build_settings.py", quiet=True):   # ★ 15.09.2026: сцена логина
                n_login += 1
                continue
            res.append((str(ad.package_name), str(ad.asset_name)))
    if n_login:   # ★ 15.09.2026
        unreal.log_warning("[LOGIN-GUARD] asda_df_build_settings.py: пропущено мешей сцены логина: %d (ASDA_ALLOW_LOGIN_WRITE=1)" % n_login)
    res.sort()
    if LIMIT:
        res = res[:LIMIT]
    return res


def info_dict(mm, mesh):
    i = mm.get_distance_field_info(mesh)
    b = i.mip0_bricks
    bs = i.bounds_size
    return {"has_data": bool(i.has_data), "valid": bool(i.valid), "async": bool(i.async_building), "mostly_two_sided": bool(i.mostly_two_sided),
            "bricks": [int(b.x), int(b.y), int(b.z)], "voxels": [int(b.x) * 7, int(b.y) * 7, int(b.z) * 7], "num_bricks": int(i.mip0_num_bricks),
            "bytes": int(i.resource_bytes), "scale": round(float(i.resolution_scale), 4), "two_sided_setting": bool(i.two_sided_setting),
            "per_mesh_flag": bool(i.per_mesh_flag), "bounds": [round(float(bs.x), 1), round(float(bs.y), 1), round(float(bs.z), 1)]}


def run_map(map_name, res, path=None):
    """★ E-2: path=/Game/... — «карта» = путь реестра, все StaticMesh под ним получают класс nodf (scale 0.0)."""
    import unreal
    mm = unreal.AsdaMeshMaintenance
    mb = unreal.AsdaMeshBuilder
    nodf = path is not None
    asset = map_name if nodf else _MAPS.map_asset_name(map_name)
    items = list_static_meshes(asset, [path] if nodf else None)
    rec = {"map": map_name, "path": path, "assets": len(items), "meshes": {}, "counts": {}}
    res["maps"][map_name] = rec
    if not items:
        unreal.log_warning("DF %s: StaticMesh не найдены (%s)" % (map_name, path or "/Game/Map/%s/Models|Terrain" % asset))
        return
    unreal.log("DF %s: режим %s, мешей %d" % (map_name, MODE, len(items)))
    changed = saved = errors = 0
    by_class = {}
    t0 = time.time()
    loaded = []
    for pkg, name in items:
        scale, two, cls = rule_for(name, nodf)
        by_class[cls] = by_class.get(cls, 0) + 1
        if MODE == "dry":
            if cls != "other":
                changed += 1
            continue
        mesh = unreal.load_asset(pkg + "." + name)
        if mesh is None:
            errors += 1
            unreal.log_error("DF %s: не грузится %s" % (map_name, pkg))
            continue
        loaded.append(mesh)
        m = {"class": cls, "rule": [scale, two]}
        if MODE == "apply":
            if cls == "other":
                continue
            before = info_dict(mm, mesh)
            do_verify = VERIFY1 and cls == "foliage" and not res.get("verify1")
            if do_verify:
                b0 = mb.get_static_mesh_render_bounds(mesh)
            ch = mm.set_distance_field_build_settings(mesh, scale, two, do_verify)
            if do_verify:
                b1 = mb.get_static_mesh_render_bounds(mesh)
                res["verify1"] = {"mesh": pkg, "bounds_before": [str(x) for x in b0], "bounds_after": [str(x) for x in b1]}
                unreal.log("DF VERIFY1 %s: bounds до %s после %s" % (pkg, b0, b1))
            after = info_dict(mm, mesh)
            m.update({"changed": bool(ch), "before": [before["scale"], before["two_sided_setting"]], "after": [after["scale"], after["two_sided_setting"]],
                      "bounds": after["bounds"], "df_before": [before["valid"], before["bytes"], before["voxels"]]})   # ★ E-2 (М-3 ревизии): bounds и DF «до» в JSON
            if ch:
                changed += 1
                if not backup_and_writable(pkg):
                    errors += 1
                    unreal.log_error("DF %s: нет файла %s" % (map_name, pkg))
                elif unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
                    saved += 1
                else:
                    errors += 1
                    unreal.log_error("DF %s: не сохранён %s" % (map_name, pkg))
            rec["meshes"][name] = m
        elif MODE == "e10":
            if mm.set_generate_mesh_distance_field_flag(mesh, True):   # без PostEditChange/Build (см. шапку)
                backup_and_writable(pkg)
                changed += 1
                if unreal.EditorAssetLibrary.save_loaded_asset(mesh, False):
                    saved += 1
                else:
                    errors += 1
                    unreal.log_error("DF %s: не сохранён %s" % (map_name, pkg))
            m["per_mesh_flag"] = True
            rec["meshes"][name] = m
        elif MODE == "info":
            rec["meshes"][name] = m
    rec["t_load_set_s"] = round(time.time() - t0, 1)
    if MODE == "info":
        t1 = time.time()
        left = mm.wait_for_distance_field_builds(WAIT_SEC)
        rec["t_wait_df_s"] = round(time.time() - t1, 1)
        rec["outstanding_after_wait"] = int(left)
        unreal.log("DF %s: ожидание фоновых DF %.1f с, осталось %d" % (map_name, rec["t_wait_df_s"], left))
        tot_bytes = 0
        for mesh in loaded:
            d = info_dict(mm, mesh)
            rec["meshes"][mesh.get_name()].update(d)
            tot_bytes += d["bytes"]
        valid = sum(1 for v in rec["meshes"].values() if v.get("valid"))
        empty = sum(1 for v in rec["meshes"].values() if v.get("has_data") and not v.get("valid"))
        rec["counts"].update({"valid": valid, "empty": empty, "no_data": len(loaded) - valid - empty, "bytes": tot_bytes,
                              "two_sided": sum(1 for v in rec["meshes"].values() if v.get("mostly_two_sided"))})
    rec["counts"].update({"by_class": by_class, "changed": changed, "saved": saved, "errors": errors, "t_total_s": round(time.time() - t0, 1)})
    unreal.log("DF-MAP %s: %s" % (map_name, json.dumps(rec["counts"], ensure_ascii=False)))
    del loaded
    unreal.SystemLibrary.collect_garbage()


def _script_main():
    import unreal
    maps = _MAPS.select_maps()
    if MODE not in ("dry", "apply", "e10", "info"):
        unreal.log_error("ASDA_DF_MODE=%r неизвестен" % MODE)
    res = {"mode": MODE, "terrain_scale": TERRAIN_SCALE, "maps": {}, "started": time.strftime("%Y-%m-%d %H:%M:%S"), "paths": PATHS}
    # ★ Сет E-2: заданы пути — обходим их (класс nodf), карты не трогаем
    units = [(p.rstrip("/").rsplit("/", 1)[-1], p) for p in PATHS] if PATHS else [(m, None) for m in maps]
    for m, path in units:
        try:
            run_map(m, res, path)
        except Exception as e:
            import traceback
            unreal.log_error("DF %s: %s\n%s" % (m, e, traceback.format_exc()))
            res["maps"][m] = {"error": str(e)}
    res["finished"] = time.strftime("%Y-%m-%d %H:%M:%S")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(res, fh, ensure_ascii=False, indent=1)
    tot = {}
    for r in res["maps"].values():
        for k, v in (r.get("counts") or {}).items():
            if isinstance(v, (int, float)):
                tot[k] = tot.get(k, 0) + v
    unreal.log("DF-TOTAL %s карт %d: %s → %s" % (MODE, len(res["maps"]), json.dumps(tot, ensure_ascii=False), OUT))
    unreal.log("ЗАВЕРШЕНО (DF-SETTINGS-FINISHED)")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
