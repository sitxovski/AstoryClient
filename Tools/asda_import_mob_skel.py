# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_mob_skel.py — импорт СКЕЛЕТАЛ-мобов (glb от asda_build_mob_fixed.py) в UE + материалы.
#   Скелетал → /Game/AsdaAssets/Models/MobSkel/<model>/ (СТАТИК-версии /Models/Mob НЕ трогаем — фолбэк
#   для 33 моделей без клипов: tree/stone-декор). Материалы: M_AsdaObject + текстура по имени
#   (атласы мобов уже в /Game/AsdaAssets/Textures/Mobs, tex_index общий по /Textures).
# Запуск: headless UnrealEditor-Cmd -ExecutePythonScript.
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
RAW_GLB  = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Models", "Skeletal", "Mob")
MODELS   = _U.G_MODELS_MOBSKEL
TEX_ROOT = _U.G_TEXTURES
MASTER   = _U.M_OBJECT_LIT   # ★ Lit-мастер (курс «современно» 2026-07-23): пересборка НЕ должна откатывать MIC на unlit
CHUNK    = 8

EAL    = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL    = unreal.MaterialEditingLibrary
AR     = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[import_mob_skel] " + str(m))

def import_to(dest, files):
    """Импорт пачкой с заменой — общий asda_model_import_ue.import_batch (дедуп №2 Т17: была копия)."""
    return MI.import_batch(files, dest)

# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «noportrait_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("noportrait_last", root=TEX_ROOT)

def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «plain_notail» библиотеки = прежняя копия этого файла).
    ★ 15.09.2026: kind="mob" — при дублях имени копия из Textures/Mobs/Texture, как в оригинале
    (MobModel.bnz ↔ Data\\Mob\\Texture\\Texture.btz; `Reports/dedup_audit/tex_dup_rule.md`)."""
    return MI.find_tex(name, tex, "plain_notail", kind="mob")

def wire_materials(tex, master):
    """Текстура + базовый мастер. ★ Мастер здесь ЧЕРНОВОЙ: окончательный выбирается по ТЕХНИКЕ
    подсетки в asda_npc_mob_masters.apply() сразу после (см. комментарий в main).
    Реализация — asda_model_import_ue.wire_materials (без Ambient, как было).
    ★ master_for НЕ ПОДКЛЮЧЁН — решение координатора волны 2 (14.09.2026): подключение сменит родителя существующих MIC при следующем прогоне (черновой мастер сменился бы до asda_npc_mob_masters.apply()), а владелец правит ассеты руками — не терять. Отчёт `Reports/dedup_audit/wave2_import.md` §3.3, §8."""
    st = MI.wire_materials(MODELS, tex_for=lambda n: find_tex(n, tex), master=master, ambient=False)
    return st["n_tex"], st["n_miss"]

def main():
    if EAL.does_directory_exist(MODELS):
        log("WIPE %s" % MODELS); EAL.delete_directory(MODELS)
    glbs = [os.path.join(RAW_GLB, f) for f in sorted(os.listdir(RAW_GLB)) if f.lower().endswith(".glb")]
    log("Импорт скелетал-мобов: %d" % len(glbs))
    for i in range(0, len(glbs), CHUNK):
        import_to(MODELS, glbs[i:i+CHUNK])
        log("  модели %d/%d" % (min(i+CHUNK, len(glbs)), len(glbs)))
        EAL.save_directory(MODELS, only_if_is_dirty=True, recursive=True)
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    master = unreal.load_asset(MASTER); tex = tex_index()
    n, miss = wire_materials(tex, master)
    # ★★★ МАСТЕР ПО ТЕХНИКЕ ПОДСЕТКИ (2026-08-18, `Reports/blend_rollout_npc_mobs.md`).
    #   Один masked-мастер на все слоты верен только для техники tNoSkin. У tSkin/tRigid альфа
    #   текстуры — маска блика (`PShade()` подставляет 1 вместо Tex.w), и masked вырезал бы
    #   геометрию; у tNoSkin с рендер-битом `mr+0x004 & 0x1|0x2` нужен, наоборот, прозрачный
    #   мастер. Класс каждого слота считается из `meshType` модели `.jbm` ⇒ переимпорт
    #   воспроизводит результат.
    import asda_npc_mob_masters as _MM
    _MM.apply(only_roots={"MobSkel"})
    EAL.save_directory(MODELS, only_if_is_dirty=True, recursive=True)
    log("ГОТОВО: моделей=%d, материалов с текстурой=%d, без=%d" % (len(glbs), n, miss))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
