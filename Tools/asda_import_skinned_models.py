# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_skinned_models.py — импорт скиннутых .glb (SkeletalMesh+Skeleton+AnimSequence) в проект + материалы.
# Запуск: в РЕДАКТОРЕ (НЕ в PIE!) Tools -> Execute Python Script.
# Назначение: /Game/AsdaAssets/Models/<DEST>/<model>/  (DEST = существующие папки проекта: Mob/Char/NPC).
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U
import asda_material_policy as _POL   # ★ выбор мастера по имени слота (masked / прозрачный / char)

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
RAW_ROOT = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Models", "Skeletal")
GROUP_MAP = [("Mobs", "Mob"), ("NPC", "NPC"), ("Characters", "Char")]  # (подпапка RawAssets, папка проекта)
DELETE_WRONG = ["Mobs", "Characters"]      # ошибочно созданные ранее папки
WIPE_DEST = ["NPC"]                        # ПОЛНОСТЬЮ снести эти папки-назначения перед импортом (чистый импорт).
                                           #   ставь сюда сломанные/смешанные; для Mob/Char НЕ ставь — там статика.
DEST_ROOT = _U.G_MODELS
TEX_ROOT  = _U.G_TEXTURES
MASTER    = _U.M_OBJECT_LIT   # ★ Lit-мастер (курс «современно» 2026-07-23): пересборка НЕ должна откатывать MIC на unlit
INCREMENTAL = True
CHUNK = 8                                  # маленький чанк = меньше GPU-всплеск (RDP: иначе TDR/потеря D3D)

EAL  = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL  = unreal.MaterialEditingLibrary
AR   = unreal.AssetRegistryHelpers.get_asset_registry()

# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI
import asda_login_guard as _LG   # ★ 15.09.2026: модели сцены логина (Models/Char/cre_creback-a_00, cre_creation01*) — только ASDA_ALLOW_LOGIN_WRITE=1

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)

def find_tex(name, tex, kind=None):
    """Текстура для слота/MIC (пресет «blend» библиотеки = прежняя копия этого файла).
    ★ 15.09.2026: kind — тип модели (MI.kind_for_package папки Mob/NPC/Char): при дублях имени копия из папки
    своего типа, как в оригинале (`Reports/dedup_audit/tex_dup_rule.md`)."""
    return MI.find_tex(name, tex, "blend", kind=kind)

def import_batch(glb_paths, dest):
    """Импорт пачкой — asda_model_import_ue.import_batch."""
    return MI.import_batch(glb_paths, dest)

def disable_nanite(dest):
    """Снять Nanite со Static/Skeletal под dest — asda_model_import_ue.disable_nanite."""
    MI.disable_nanite(dest)

def wire_materials(dest, tex, master):
    """★ 2026-08-18: мастер выбирается ПО ИМЕНИ MIC (`asda_material_policy.master_for`), а не «один
    на всех». Через эту функцию идут задник логина (`cre_creback-a_00`) и шкатулка
    (`cre_creation01-a_00`) — у них есть подсетки с рендер-флагом блендинга при технике tNoSkin
    (`Reports/jbm_blend_flag_study.md` §5). Прежний «один мастер» молча откатывал прозрачность
    на КАЖДОМ переимпорте.
    Реализация — asda_model_import_ue.wire_materials (мастер по политике, запасной — master).
    """
    # ★ 15.09.2026: MIC сцены логина (Models/Char/cre_creback-a_00, cre_creation01*) MI.wire_materials пропускает без
    #   ASDA_ALLOW_LOGIN_WRITE=1; у моделей из MI.DATA_PINNED_TEX (текстура по данным) текущая Tex не меняется.
    kind = MI.kind_for_package(dest)   # ★ 15.09: тип модели по папке назначения (Mob/NPC/Char) — копия текстуры своего типа
    st = MI.wire_materials(dest, tex_for=lambda n: find_tex(n, tex, kind), pick_master=MI.policy_master_loader(master))
    if st["per_master"]:
        unreal.log(MI.per_master_line(st["per_master"]))
    return st["n_all"]

def main():
    try:
        if unreal.EditorLevelLibrary.get_editor_world().get_name().startswith("PIE"):
            unreal.log_warning("PIE запущен — останови игру перед импортом."); return
    except Exception: pass

    for w in DELETE_WRONG:
        d = "%s/%s" % (DEST_ROOT, w)
        if EAL.does_directory_exist(d):
            unreal.log("Удаляю ошибочную папку %s" % d); EAL.delete_directory(d)

    master = unreal.load_asset(MASTER)
    tex = tex_index()
    unreal.log("Текстур в индексе: %d" % len(tex))
    total = mats = skip = 0
    for sub, destname in GROUP_MAP:
        src = os.path.join(RAW_ROOT, sub)
        if not os.path.isdir(src): continue
        dest_import = "%s/%s" % (DEST_ROOT, destname)   # РОДИТЕЛЬ; Interchange кладёт в Models/<dest>/<model>/...
        # чистая папка (для сломанных/смешанных групп)
        if destname in WIPE_DEST and EAL.does_directory_exist(dest_import):
            unreal.log("WIPE %s (чистый импорт)" % dest_import); EAL.delete_directory(dest_import)
        glbs = sorted(f for f in os.listdir(src) if f.lower().endswith(".glb"))
        # инкрементально
        todo = []
        for fn in glbs:
            mdir = "%s/%s" % (dest_import, os.path.splitext(fn)[0])
            if _LG.login_package_blocked(mdir, "asda_import_skinned_models.py"):
                continue   # ★ сцена логина (задник/шкатулка): ведут import_creback.py / import_casket.py
            if INCREMENTAL and EAL.does_directory_exist(mdir) and EAL.list_assets(mdir): skip += 1
            else: todo.append(os.path.join(src, fn))
        unreal.log("[%s -> %s] к импорту: %d (пропуск=%d)" % (sub, destname, len(todo), len(glbs)-len(todo)))
        for i in range(0, len(todo), CHUNK):
            chunk = todo[i:i+CHUNK]
            import_batch(chunk, dest_import)
            total += len(chunk)
            unreal.log("  [%s] импортировано %d/%d" % (destname, min(i+CHUNK, len(todo)), len(todo)))
            # ★ раздел 22 (16.09.2026): dest_import группы Characters — /Game/AsdaAssets/Models/Char, где лежат
            #   пакеты сцены логина (из импорта они отсеяны выше, но папка сохранялась целиком).
            _LG.save_dir_login_safe(dest_import, "asda_import_skinned_models.py")   # по чанкам, как было (TDR-устойчиво)
            try: unreal.SystemLibrary.collect_garbage()
            except Exception: pass
        if todo:
            mats += wire_materials(dest_import, tex, master)
            # ★★★ МАСТЕР ПО ТЕХНИКЕ ПОДСЕТКИ для групп NPC/мобов (2026-08-18,
            #   `Reports/blend_rollout_npc_mobs.md`). `master_for()` выше решает по ИМЕНИ, а у NPC и
            #   мобов класс меняется от подсетки к подсетке ВНУТРИ одной модели: tSkin/tRigid — альфа
            #   маскирует блик (нужен непрозрачный мастер), tNoSkin — альфа режет геометрию (masked),
            #   tNoSkin с рендер-битом — блендинг. Класс берётся из `meshType` самой модели `.jbm`.
            if destname in ("NPC", "Mob"):
                import asda_npc_mob_masters as _MM
                _MM.apply(only_roots={"NPC"} if destname == "NPC" else {"Mob", "MobSkel"})
            disable_nanite(dest_import)
            _LG.save_dir_login_safe(dest_import, "asda_import_skinned_models.py")   # ★ раздел 22: не папкой целиком
    unreal.log("ГОТОВО: импортировано=%d, пропущено=%d, материалов=%d" % (total, skip, mats))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
