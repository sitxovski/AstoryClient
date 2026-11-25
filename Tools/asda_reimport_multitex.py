# Copyright (c) 2026 @sitxovski (Discord)
# asda_reimport_multitex.py — ПЕРЕИМПОРТ статик-моделей с НЕСКОЛЬКИМИ текстурами (пер-меш matIdx@0x1f4).
#
# ЗАЧЕМ. До 2026-08-01 копия парсера в asda_jbm_to_gltf.py ставила ВСЕМ мешам модели текстуру
#   первого материала (tex0), игнорируя пер-меш matIdx@0x1f4 (закон npc-texture-source). В .glb
#   все материалы получали ОДНО имя, UE схлопывал их в ОДИН слот → часть мешей красилась чужой
#   текстурой (клас. пример: древко флага `template.TEX` красилось полотнищем `flag.TEX`).
#   Парсер сведён в asda_jbm.py и починен, но ассеты в Content остались от старого экспорта.
#
# ЧТО ДЕЛАЕТ. Находит в RawAssets/Models/{Char,Mob} все .glb, у которых РАЗНЫХ имён материалов > 1,
#   и переимпортирует РОВНО их в /Game/AsdaAssets/Models/<кат>/<модель>/ (replace_existing).
#   Печатает число материальных слотов ДО и ПОСЛЕ. Ничего не удаляет.
#   Предварительно: переэкспорт .glb (asda_build_models.py или точечный прогон asda_jbm_to_gltf).
#   ПОСЛЕ: прогнать фиксеры материалов с тем же охватом (переменная ASDA_ONLY_MODELS):
#       asda_fix_model_materials.py → asda_clean_materials.py → asda_fix_mob_materials.py
#
# Охват можно сузить (пилот): ASDA_ONLY_MODELS="big_flag,mon_s_0_kduck01-a_00"
# Сухой прогон (только отчёт, без импорта): ASDA_MULTITEX_DRYRUN=1
# Запуск: run_headless.bat ...\asda_reimport_multitex.py   (или в редакторе).
#
# ★ Без побочных действий при импорте модуля (правило tools-no-import-side-effects).
import os, json, struct
import unreal
import sys as _sys_lg   # ★ 15.09.2026: защита сцены логина (asda_login_guard: Models/Backdrop, Char/cre_creback-a_00, cre_creation01* и др.)
_sys_lg.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_login_guard as _LG

RAW_ROOT  = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Models"
DEST_BASE = "/Game/AsdaAssets/Models"
CATS      = ("Char", "Mob")          # Items многотекстурных не содержит (проверено: 0 из 243)

EAL = unreal.EditorAssetLibrary

# ★ Сет B (05.09.2026, ревизия КРИТ-1): Interchange при импорте glTF ставит `NaniteSettings.bEnabled=true` (дефолт
#   bBuildNanite, InterchangeGenericMeshPipeline.h:134). Пока r.Nanite.ProjectEnabled=False это было безвредно, с VSM
#   (ProjectEnabled=True) меш пошёл бы Nanite-путём. Гасим СРАЗУ после импорта — на тех объектах, что вернул импорт
#   (тот же приём, что в asda_import_skinned_models/asda_import_gachya_models.disable_nanite). Полный проход по проекту —
#   Tools/asda_disable_nanite.py.
def disable_nanite_on(objs):
    """Выключить Nanite у импортированных StaticMesh/SkeletalMesh; вернуть число выключенных."""
    off = 0
    for m in objs or []:
        if not isinstance(m, (unreal.StaticMesh, unreal.SkeletalMesh)): continue
        try:
            ns = m.get_editor_property("nanite_settings")
            if ns and ns.get_editor_property("enabled"):
                ns.set_editor_property("enabled", False); m.set_editor_property("nanite_settings", ns); off += 1
        except Exception as e:
            unreal.log_warning("nanite off fail %s: %s" % (m.get_name(), str(e)[:60]))
    return off
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def glb_material_names(path):
    """Имена материалов из JSON-чанка .glb (без внешних библиотек)."""
    with open(path, "rb") as f:
        head = f.read(12)
        if len(head) < 12 or head[:4] != b"glTF":
            return []
        clen, ctype = struct.unpack("<II", f.read(8))
        if ctype != 0x4E4F534A:      # 'JSON'
            return []
        doc = json.loads(f.read(clen).decode("utf-8", "replace"))
    return [m.get("name", "") for m in doc.get("materials", [])]


def scan(only):
    """Список (кат, имя, [различные имена материалов]) для многотекстурных .glb."""
    out = []
    for cat in CATS:
        d = os.path.join(RAW_ROOT, cat)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.lower().endswith(".glb"):
                continue
            name = os.path.splitext(fn)[0]
            if only and not (name in only or ("%s/%s" % (cat, name)) in only):
                continue
            names = sorted(set(n for n in glb_material_names(os.path.join(d, fn)) if n))
            if len(names) > 1:
                out.append((cat, name, names))
    return out


def slot_names(cat, name):
    """Имена материальных слотов StaticMesh в проекте (пусто, если ассета нет)."""
    pkg = "%s/%s/%s/StaticMeshes/%s" % (DEST_BASE, cat, name, name)
    if not EAL.does_asset_exist(pkg):
        return None
    m = unreal.load_asset(pkg)
    if not m:
        return None
    return [str(s.get_editor_property("material_slot_name")) for s in m.get_editor_property("static_materials")]


def main():
    only = set(s.strip() for s in os.environ.get("ASDA_ONLY_MODELS", "").split(",") if s.strip())
    dry  = os.environ.get("ASDA_MULTITEX_DRYRUN", "") not in ("", "0")
    todo = scan(only)
    unreal.log("[multitex] многотекстурных .glb: %d%s%s" %
               (len(todo), (" (охват: %d имён)" % len(only)) if only else "", "  СУХОЙ ПРОГОН" if dry else ""))

    before = {}
    for cat, name, names in todo:
        before[(cat, name)] = slot_names(cat, name)

    imported = skipped = 0
    if not dry:
        # группируем по каталогу-назначению: один вызов импорта на категорию
        for cat in CATS:
            files = [os.path.join(RAW_ROOT, cat, n + ".glb") for c, n, _ in todo if c == cat]
            names = [n for c, n, _ in todo if c == cat]
            for path, name in zip(files, names):
                # ★ назначение = КАТЕГОРИЯ: Interchange сам разложит <модель>/StaticMeshes|Materials.
                #   Если указать сюда папку модели, получится вложенное <модель>/<модель>/…
                dest = "%s/%s" % (DEST_BASE, cat)
                if _LG.login_package_blocked("%s/%s" % (dest, name), "asda_reimport_multitex.py"):   # ★ 15.09.2026: сцена логина
                    continue
                if before[(cat, name)] is None:
                    skipped += 1      # в проекте нет статик-меша (модель живёт как скелетал) — не заводим новый
                    unreal.log("  ПРОПУСК %s/%s: статик-меша в проекте нет" % (cat, name))
                    continue
                data = unreal.AutomatedAssetImportData()
                data.set_editor_property("destination_path", dest)
                data.set_editor_property("filenames", [path])
                data.set_editor_property("replace_existing", True)
                data.set_editor_property("skip_read_only", True)
                res = ATOOLS.import_assets_automated(data)
                n_off = disable_nanite_on(res)          # ★ Сет B: Nanite off до сохранения
                if n_off: unreal.log("  Nanite выключен у %d мешей %s/%s" % (n_off, cat, name))
                imported += 1 if res else 0
                EAL.save_directory("%s/%s" % (dest, name), only_if_is_dirty=True, recursive=True)
                try: unreal.SystemLibrary.collect_garbage()
                except Exception: pass

    unreal.log("[multitex] переимпортировано=%d, пропущено(нет статика)=%d" % (imported, skipped))
    unreal.log("[multitex] --- слоты ДО -> ПОСЛЕ (нужно = числу различных текстур) ---")
    bad = 0
    for cat, name, names in todo:
        b = before[(cat, name)]
        if b is None:
            continue
        a = slot_names(cat, name) or []
        flag = "" if len(a) >= len(names) else "  ★ВСЁ ЕЩЁ МАЛО"
        if len(a) < len(names):
            bad += 1
        unreal.log("  %-6s %-28s текстур=%-3d слотов %d -> %d%s" % (cat, name, len(names), len(b), len(a), flag))
    unreal.log("[multitex] недобор слотов остался у %d моделей" % bad)
    # охват для последующих фиксеров: только модели, у которых статик-меш реально есть
    scope = ",".join("%s/%s" % (c, n) for c, n, _ in todo if before[(c, n)] is not None)
    sf = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_out", "multitex_scope.txt")
    try:
        os.makedirs(os.path.dirname(sf), exist_ok=True)
        open(sf, "w", encoding="utf-8").write(scope)
        unreal.log("[multitex] охват для фиксеров записан: %s" % sf)
    except Exception as e:
        unreal.log_warning("[multitex] не записал охват: %s" % str(e)[:80])
    unreal.log("=== MULTITEX REIMPORT DONE ===")


if __name__ == "__main__":
    main()
