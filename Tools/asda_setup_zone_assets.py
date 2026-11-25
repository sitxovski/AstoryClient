# Copyright (c) 2026 @sitxovski (Discord)
# asda_setup_zone_assets.py — ассеты системы «город / не город»:
#   1) DT_MapTable заново из `Reports/map_table.csv` (там появились BgmTown/BgmField/NameTex/NameTexTown,
#      см. Tools/asda_export_map_zone_data.py — данные из заголовка .TER и свитча Map_SetNameTextureByID@0xDA3700);
#   2) ЗАЦИКЛИВАНИЕ BGM: оригинал открывает музыкальный стрим с FSOUND_LOOP_NORMAL
#      (CSound_OpenStream@0xFA00D0 → FSOUND_Stream_Open(path, 0x80052, …), 0x02 = LOOP_NORMAL),
#      поэтому все S_BGM_* обязаны быть looping;
#   3) UI-настройки текстур названий локаций (name_*): POINT-фильтр без мипов, группа UI —
#      как весь UI оригинала (D3DTEXF_POINT, WindowMgr_RenderControls@0x97D8F0).
# Запуск: run_headless.bat asda_setup_zone_assets.py
import unreal

EAL = unreal.EditorAssetLibrary
CSV = r"@@ASTORY_DEV_ROOT@@\Reports\map_table.csv"
DATA_DIR = "/Game/AsdaAssets/Data"
MUSIC_DIRS = ["/Game/AsdaAssets/Sounds/Music"]   # канон музыки (Sound/BGM удалён 2026-08-02 как дубль)
NAMETEX_DIRS = ["/Game/AsdaAssets/Textures/NPC/Image", "/Game/AsdaAssets/Textures/NPC/Portrait"]


def log(m): unreal.log("[zone-assets] " + str(m))


# ── 1) DT_MapTable ────────────────────────────────────────────────────────────────────────────────
def rebuild_map_table():
    row_struct = unreal.load_object(None, "/Script/AsdaReborn.AsdaMapRow")
    if not row_struct:
        unreal.log_warning("[zone-assets] FAsdaMapRow не найден (собери C++)"); return
    if not EAL.does_directory_exist(DATA_DIR):
        EAL.make_directory(DATA_DIR)
    if EAL.does_asset_exist(DATA_DIR + "/DT_MapTable"):
        EAL.delete_asset(DATA_DIR + "/DT_MapTable")
    factory = unreal.CSVImportFactory()
    factory.automated_import_settings.import_row_struct = row_struct
    task = unreal.AssetImportTask()
    task.filename = CSV
    task.destination_path = DATA_DIR
    task.destination_name = "DT_MapTable"
    task.factory = factory
    task.replace_existing = True
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    dt = EAL.load_asset(DATA_DIR + "/DT_MapTable")
    if dt:
        EAL.save_asset(DATA_DIR + "/DT_MapTable")
        rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
        log("DT_MapTable импортирован, строк=%d" % len(rows))
    else:
        unreal.log_warning("[zone-assets] DT_MapTable НЕ создан")


# ── 2) Зацикливание BGM ───────────────────────────────────────────────────────────────────────────
def set_bgm_looping():
    n = 0
    # ★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): обход идёт по ПАПКЕ
    #   /Game/AsdaAssets/Sounds/Music целиком, а в ней лежит пакет сцены логина Music/Login. Сегодня его
    #   отводит фильтр имени (S_BGM*), но надеяться на имя нельзя — проверяем принадлежность перед записью.
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))
    from asda_login_guard import login_package_blocked as _lg_blocked
    for d in MUSIC_DIRS:
        if not EAL.does_directory_exist(d):
            continue
        for path in EAL.list_assets(d, recursive=True, include_folder=False):
            name = path.split("/")[-1].split(".")[0]
            if not name.upper().startswith("S_BGM"):
                continue
            if _lg_blocked(path, "asda_setup_zone_assets.py"):   # ★ раздел 22: пакет сцены логина — ДО правки
                continue
            a = EAL.load_asset(path)
            if not a:
                continue
            ok = False
            for prop in ("looping", "b_looping"):
                try:
                    a.set_editor_property(prop, True); ok = True; break
                except Exception:
                    pass
            if ok:
                EAL.save_loaded_asset(a); n += 1
            else:
                unreal.log_warning("[zone-assets] не удалось выставить looping: %s" % path)
    log("зациклено BGM: %d" % n)


# ── 3) UI-настройки текстур названий локаций (+ импорт недостающих) ──────────────────────────────
RAW_NAMETEX = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures\Data\NPC\Image"


def import_missing_name_textures():
    """Импортируем name_*.png из RawAssets (их кладёт Tools/asda_extract_name_textures.py)."""
    import os
    dest = NAMETEX_DIRS[0]
    if not os.path.isdir(RAW_NAMETEX):
        unreal.log_warning("[zone-assets] нет %s — запусти asda_extract_name_textures.py" % RAW_NAMETEX); return
    if not EAL.does_directory_exist(dest):
        EAL.make_directory(dest)
    files = []
    for f in os.listdir(RAW_NAMETEX):
        if not f.lower().startswith("name_") or not f.lower().endswith(".png"):
            continue
        asset = os.path.splitext(f)[0].replace(" ", "_")
        if EAL.does_asset_exist("%s/%s" % (dest, asset)):
            continue
        files.append(os.path.join(RAW_NAMETEX, f))
    if not files:
        log("текстуры названий: импортировать нечего"); return
    import os as _os, sys as _sys
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (AutomatedAssetImportData)
    got = _MIU.import_batch(files, dest)
    for a in got:
        EAL.save_loaded_asset(a)
    log("текстур названий импортировано: %d из %d" % (len(got), len(files)))


def fix_name_textures():
    n = 0
    for d in NAMETEX_DIRS:
        if not EAL.does_directory_exist(d):
            continue
        for path in EAL.list_assets(d, recursive=False, include_folder=False):
            name = path.split("/")[-1].split(".")[0]
            if not name.lower().startswith("name_"):
                continue
            t = EAL.load_asset(path)
            if not t:
                continue
            try:
                t.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
                t.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
                t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
                t.set_editor_property("never_stream", True)
                # UserInterface2D (RGBA), БЕЗ сжатия: на 512×64 с резкими буквами BC-артефакты видны
                #   глазом (баннер «изуродован»). Эталон выводит картинку 1:1 в пикселях, без потерь.
                t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
                EAL.save_loaded_asset(t); n += 1
            except Exception as e:
                unreal.log_warning("[zone-assets] %s: %s" % (path, e))
    log("текстур названий локаций настроено: %d" % n)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    rebuild_map_table()
    set_bgm_looping()
    import_missing_name_textures()
    fix_name_textures()
    log("=== ГОТОВО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
