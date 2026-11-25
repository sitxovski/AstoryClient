# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_models.py — импорт моделей (.glb из RawAssets/Models) в проект UE.
# Предварительно: python asda_build_models.py (.jbm → .glb, каталогизировано Char/Mob/Items).
# Запуск: в редакторе Tools -> Execute Python Script. .glb импортируется UE нативно (glTF/Interchange).
# ФАЗА 1: статик-геометрия (без скелета). → StaticMesh в /Game/AsdaAssets/Models/<кат>/.
import os, re, unreal
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_login_guard as _LG        # ★ 15.09.2026: модели сцены логина не переимпортировать без ASDA_ALLOW_LOGIN_WRITE=1
import asda_model_import_ue as MI     # ★ 15.09.2026: MI.DATA_PINNED_TEX — модели с текстурой по данным не переимпортировать

RAW_ROOT  = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Models"
DEST_BASE = "/Game/AsdaAssets/Models"
PURGE_FIRST = False                      # НЕ сносить всю Models (там скелетные!); replace_existing перезапишет
# ★★★ НЕ ИМПОРТИРОВАТЬ КАК СТАТИК. Кроме `Skeletal/` и тестов сюда входят папки, которые
#   `asda_build_skinned_models.py` (OUT_OVERRIDE) кладёт РЯДОМ с Models, а не внутрь Skeletal:
#   `Gachya`, `Vehicle`, `ClanFlag`; плюс выходы петов и Soul Guard. Все они — СКЕЛЕТАЛКИ со
#   своими импортёрами (`asda_import_gachya_models.py`, `asda_import_vehicle_models.py`,
#   `asda_import_clanflag_model.py`, `asda_import_pet_models.py`, `asda_import_soulguard_models.py`),
#   и статик-копии этих моделей проекту не нужны. Обнаружено 2026-08-18: прогон импортёра завёл
#   лишний `/Game/AsdaAssets/Models/Gachya` (435 ассетов-двойников автоматов гачи) — папка удалена,
#   а источник закрыт здесь.
SKIP_DIRS = ("Skeletal", "_AnimTest", "Gachya", "Vehicle", "ClanFlag", "Pet", "SoulGuard",
             "_prev_normals_2026-09-05")   # ★ 2026-09-05: бэкап старых glb (asda_fix_glb_normals.py) — НЕ категория
INCREMENTAL = True                       # пропускать уже импортированные
# ★ ТОЧЕЧНЫЙ ПЕРЕИМПОРТ: ASDA_MODEL_NAMES="^md_gachya_[a-e]_00$" — регексп по имени модели
#   (тот же ключ, что у сборщика `asda_build_models.py`, чтобы пара «пересобрать→переимпортировать»
#   задавалась одной переменной). Пока фильтр задан, INCREMENTAL выключается: раз модель названа
#   явно, её надо перезалить, даже если ассет уже есть — иначе исправленный `.glb` молча не доедет.
NAME_PAT = os.environ.get("ASDA_MODEL_NAMES", "").strip()
NAME_RE = re.compile(NAME_PAT, re.I) if NAME_PAT else None
if NAME_RE:
    INCREMENTAL = False

atools = unreal.AssetToolsHelpers.get_asset_tools()
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

def import_dir(abs_dir, files):
    rel = os.path.relpath(abs_dir, RAW_ROOT).replace("\\", "/")
    dest = DEST_BASE if rel == "." else "%s/%s" % (DEST_BASE, rel)
    if INCREMENTAL:   # пропустить уже импортированные (по имени ассета в dest)
        files = [f for f in files if not EAL.does_asset_exist("%s/%s" % (dest, os.path.splitext(f)[0]))]
    # ★ 15.09.2026: сцена логина (Models/Char/cre_creback-a_00, cre_creation01*) и модели с текстурой по данным — пропуск
    files = [f for f in files if not _LG.login_package_blocked("%s/%s" % (dest, os.path.splitext(f)[0]), "asda_import_models.py")]
    _pin = [f for f in files if MI.data_pinned_dir("%s/%s" % (dest, os.path.splitext(f)[0]))]
    if _pin:
        unreal.log_warning("[DATA-PIN] asda_import_models.py: пропуск %s — текстура по данным (MI.DATA_PINNED_TEX)" % _pin)
        files = [f for f in files if f not in _pin]
    if not files: return 0
    data = unreal.AutomatedAssetImportData()
    data.set_editor_property("destination_path", dest)
    data.set_editor_property("filenames", [os.path.join(abs_dir, f) for f in files])
    data.set_editor_property("replace_existing", True)
    data.set_editor_property("skip_read_only", True)
    imported = atools.import_assets_automated(data)
    n_off = disable_nanite_on(imported)          # ★ Сет B: Nanite off до сохранения
    if n_off: unreal.log("  Nanite выключен у %d импортированных мешей (%s)" % (n_off, dest))
    # ★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): dest — подпапка Models
    #   (Char, Backdrop и др.), где лежат модели сцены логина. Из ИМПОРТА они отсеяны выше
    #   (login_package_blocked), но сохранение папки целиком пересохраняло их всё равно.
    _LG.save_dir_login_safe(dest, "asda_import_models.py")
    return len(imported)

def main():
    if PURGE_FIRST and EAL.does_directory_exist(DEST_BASE):
        # ★ 15.09.2026: снос Models целиком уносит задник и шкатулку логина — только с ASDA_ALLOW_LOGIN_WRITE=1
        _LG.forbid_login_write("asda_import_models.py", target="Models целиком (PURGE_FIRST; в т.ч. Models/Backdrop, Char/cre_creback-a_00, cre_creation01*)")
        unreal.log("Чистка старых моделей: %s" % DEST_BASE); EAL.delete_directory(DEST_BASE)
    groups = []
    for root, dirs, files in os.walk(RAW_ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]   # не заходить в Skeletal/_AnimTest
        glb = [f for f in files if f.lower().endswith(".glb")]
        if NAME_RE:
            glb = [f for f in glb if NAME_RE.match(os.path.splitext(f)[0])]
        if glb: groups.append((root, glb))
    if NAME_RE:
        unreal.log("asda_import_models: ФИЛЬТР ИМЁН %s, инкремент выключен" % NAME_PAT)
    unreal.log("asda_import_models: папок=%d (статик; Skeletal/_AnimTest исключены)" % len(groups))
    total = 0
    for i, (root, glb) in enumerate(groups):
        n = import_dir(root, glb); total += n
        if n: unreal.log("[%d/%d] %s : +%d" % (i+1, len(groups), os.path.relpath(root, RAW_ROOT), n))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("asda_import_models: импортировано всего=%d → %s" % (total, DEST_BASE))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
