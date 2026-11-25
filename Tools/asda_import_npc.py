# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_npc.py — ПЕРЕЗАПИСЬ моделей NPC в проекте исправленными .glb (двух-позиционный bind).
# Источник: RawAssets/Models/Skeletal/NPC/*.glb  (сборка: asda_build_npc_fixed.py).
# Назначение: /Game/AsdaAssets/Models/NPC  (папка ПОЛНОСТЬЮ пересоздаётся).
# Запуск:
#   headless (RDP):  Tools\run_headless.bat Tools\asda_import_npc.py
#   в редакторе:     Tools -> Execute Python Script -> asda_import_npc.py  (НЕ в PIE!)
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U
import asda_material_policy as POL   # ★ выбор мастера по имени слота (masked / прозрачный)

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
RAW_DIR  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Models", "Skeletal", "NPC")
DEST     = _U.G_MODELS_NPC
TEX_ROOT = _U.G_TEXTURES
MASTER   = _U.M_OBJECT_LIT   # ★ Lit-мастер (курс «современно» 2026-07-23): пересборка НЕ должна откатывать MIC на unlit
CHUNK    = 8                       # маленький чанк = меньше GPU-всплеск (RDP: иначе TDR/«device removed»)

EAL    = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL    = unreal.MaterialEditingLibrary
AR     = unreal.AssetRegistryHelpers.get_asset_registry()

# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)

def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «blend» библиотеки = прежняя копия этого файла).
    ★ 15.09.2026: kind="npc" — при дублях имени копия из Textures/NPC/Texture, как в оригинале
    (NpcModel.bnz ↔ Data\\NPC\\Texture\\Texture.btz; `Reports/dedup_audit/tex_dup_rule.md`)."""
    return MI.find_tex(name, tex, "blend", kind="npc")

def import_batch(glb_paths):
    """Импорт пачкой в DEST — asda_model_import_ue.import_batch."""
    return MI.import_batch(glb_paths, DEST)

def wire_materials(tex, master):
    """★ 2026-08-18: мастер выбирается ПО ИМЕНИ MIC, а не «один на всех».

    Раньше здесь всем без разбора ставился `M_AsdaObjectLit` — и каждый переимпорт МОЛЧА откатывал
    прозрачность декора карт (`mdel_*`/`natu_*` лежат в NpcModel.bnz). Теперь маршрут вычисляется
    из имени слота, который экспортёр пометил суффиксом `_blend` по данным `.jbm`
    (рендер-флаг `mr+0x004 & 0x1|0x2` И техника tNoSkin) ⇒ результат воспроизводится.
    Реализация — asda_model_import_ue.wire_materials (мастер по политике, запасной — master).
    """
    st = MI.wire_materials(DEST, tex_for=lambda n: find_tex(n, tex), pick_master=MI.policy_master_loader(master))
    unreal.log(MI.per_master_line(st["per_master"]))
    return st["n_all"]

def disable_nanite():
    """Снять Nanite со Static/Skeletal под DEST — asda_model_import_ue.disable_nanite."""
    MI.disable_nanite(DEST)

def main():
    try:
        if unreal.EditorLevelLibrary.get_editor_world().get_name().startswith("PIE"):
            unreal.log_warning("PIE запущен — останови игру перед импортом."); return
    except Exception: pass

    if not os.path.isdir(RAW_DIR):
        unreal.log_error("нет папки с .glb: %s (сначала asda_build_npc_fixed.py)" % RAW_DIR); return
    glbs = sorted(f for f in os.listdir(RAW_DIR) if f.lower().endswith(".glb"))
    if not glbs:
        unreal.log_error("в %s нет .glb" % RAW_DIR); return

    # ЧИСТАЯ перезапись: сносим папку назначения целиком
    if EAL.does_directory_exist(DEST):
        unreal.log("WIPE %s" % DEST); EAL.delete_directory(DEST)

    master = unreal.load_asset(MASTER)
    tex = tex_index()
    unreal.log("К импорту NPC: %d моделей | текстур в индексе: %d" % (len(glbs), len(tex)))

    todo = [os.path.join(RAW_DIR, f) for f in glbs]
    for i in range(0, len(todo), CHUNK):
        import_batch(todo[i:i+CHUNK])
        unreal.log("  импортировано %d/%d" % (min(i+CHUNK, len(todo)), len(todo)))
        EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)   # сохранять по чанкам (TDR-устойчиво)
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass

    mats = wire_materials(tex, master)
    disable_nanite()
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    unreal.log("ГОТОВО: модели=%d, материалов привязано=%d → %s" % (len(glbs), mats, DEST))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
