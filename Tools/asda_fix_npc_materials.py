# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_npc_materials.py — ПОЛНЫЙ фикс материалов NPC:
#   1) переимпорт МОДЕЛЬНЫХ атласов (a/b/c) вместо ошибочных UI-портретов (было: NPC\Image вместо NPC\Texture);
#   2) переимпорт моделей с ПЕР-МЕШ материалами (matIdx@0x1f4 → a/b/c, раньше всё валилось на tex0);
#   3) перепривязка материалов (M_AsdaObject + правильная текстура по имени).
# Предварительно: asda_extract_archive_textures.py (атласы в RawAssets) + asda_build_npc_fixed.py (glb).
# Запуск: Tools\run_headless.bat Tools\asda_fix_npc_materials.py
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U
import asda_material_policy as POL   # ★ выбор мастера по имени слота (masked / прозрачный)

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
RAW_TEX  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "Data", "NPC", "Texture")   # модельные атласы (png)
RAW_GLB  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Models", "Skeletal", "NPC")
TEX_DEST = _U.G_TEX_NPC_TEX
MODELS   = _U.G_MODELS_NPC
TEX_ROOT = _U.G_TEXTURES
MASTER   = _U.M_OBJECT_LIT   # ★ Lit-мастер (курс «современно» 2026-07-23): пересборка НЕ должна откатывать MIC на unlit
CHUNK    = 8

EAL    = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL    = unreal.MaterialEditingLibrary
AR     = unreal.AssetRegistryHelpers.get_asset_registry()

def import_to(dest, files, replace=True):
    """Импорт пачкой (пустой список — ничего) — общий asda_model_import_ue.import_batch (дедуп №2 Т17: была копия)."""
    if not files: return []
    return MI.import_batch(files, dest, replace)

# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «noportrait_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("noportrait_last", root=TEX_ROOT)

def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «class» библиотеки = прежняя копия этого файла).
    ★ 15.09.2026: kind="npc" — при дублях имени копия из Textures/NPC/Texture, как в оригинале
    (NpcModel.bnz ↔ Data\\NPC\\Texture\\Texture.btz; `Reports/dedup_audit/tex_dup_rule.md`)."""
    return MI.find_tex(name, tex, "class", kind="npc")

def wire_materials(tex, master):
    """★ 2026-08-18: мастер выбирается ПО ИМЕНИ MIC, а не «один на всех».

    В NpcModel.bnz лежит и ДЕКОР КАРТ (`mdel_*`/`natu_*`): 10 моделей / 109 подсеток с рендер-флагом
    блендинга при технике tNoSkin (`Reports/jbm_blend_flag_study.md` §5). Экспортёр помечает их
    слоты суффиксом `_blend`, здесь метка превращается в прозрачный мастер. Без этого каждый прогон
    молча возвращал листву/дымку на masked-мастер с порогом 0.5.
    Реализация — asda_model_import_ue.wire_materials (мастер по политике, запасной — master).
    """
    st = MI.wire_materials(MODELS, tex_for=lambda n: find_tex(n, tex), pick_master=MI.policy_master_loader(master))
    unreal.log(MI.per_master_line(st["per_master"]))
    return st["n_tex"], st["n_miss"]

def disable_nanite():
    """Снять Nanite со Static/Skeletal под MODELS — asda_model_import_ue.disable_nanite."""
    MI.disable_nanite(MODELS)

def main():
    try:
        if unreal.EditorLevelLibrary.get_editor_world().get_name().startswith("PIE"):
            unreal.log_warning("PIE запущен — останови игру."); return
    except Exception: pass

    # 1) ТЕКСТУРЫ: чистим старые (портрет-производные) и заливаем модельные атласы
    if EAL.does_directory_exist(TEX_DEST):
        unreal.log("WIPE %s (старые портрет-текстуры)" % TEX_DEST); EAL.delete_directory(TEX_DEST)
    pngs = [os.path.join(RAW_TEX, f) for f in sorted(os.listdir(RAW_TEX)) if f.lower().endswith(".png")] if os.path.isdir(RAW_TEX) else []
    unreal.log("Импорт атласов NPC: %d" % len(pngs))
    for i in range(0, len(pngs), 64):
        import_to(TEX_DEST, pngs[i:i+64])
        EAL.save_directory(TEX_DEST, only_if_is_dirty=True, recursive=True)

    # 2) МОДЕЛИ: вайп и переимпорт (новые пер-меш слоты материалов a/b/c)
    if EAL.does_directory_exist(MODELS):
        unreal.log("WIPE %s" % MODELS); EAL.delete_directory(MODELS)
    glbs = [os.path.join(RAW_GLB, f) for f in sorted(os.listdir(RAW_GLB)) if f.lower().endswith(".glb")]
    unreal.log("Импорт моделей NPC: %d" % len(glbs))
    for i in range(0, len(glbs), CHUNK):
        import_to(MODELS, glbs[i:i+CHUNK])
        unreal.log("  модели %d/%d" % (min(i+CHUNK, len(glbs)), len(glbs)))
        EAL.save_directory(MODELS, only_if_is_dirty=True, recursive=True)
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass

    # 3) МАТЕРИАЛЫ
    master = unreal.load_asset(MASTER); tex = tex_index()
    unreal.log("Текстур в индексе (без портретов): %d" % len(tex))
    n, miss = wire_materials(tex, master)
    # ★★★ МАСТЕР ПО ТЕХНИКЕ ПОДСЕТКИ (2026-08-18, `Reports/blend_rollout_npc_mobs.md`).
    #   wire_materials выше вешает ВСЕМ MIC один masked-мастер — это верно только для подсеток
    #   техники tNoSkin. У tSkin/tRigid альфа текстуры в прозрачность не идёт (`PShade()` подставляет
    #   1 вместо Tex.w), она маскирует блик, и masked вырезал бы геометрию. Поэтому сразу после
    #   переимпорта переподвязываем слоты по классу, посчитанному из `meshType` самой модели.
    #   Вызов ЗДЕСЬ, а не отдельным ручным прогоном, — чтобы переимпорт воспроизводил результат.
    import asda_npc_mob_masters as _MM
    _MM.apply(only_roots={"NPC"})
    disable_nanite()
    EAL.save_directory(MODELS, only_if_is_dirty=True, recursive=True)
    EAL.save_directory(TEX_DEST, only_if_is_dirty=True, recursive=True)
    unreal.log("ГОТОВО: атласов=%d, моделей=%d, материалов с текстурой=%d, без текстуры=%d" % (len(pngs), len(glbs), n, miss))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
