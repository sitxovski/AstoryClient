# Copyright (c) 2026 @sitxovski (Discord)
# duel_flag_import_models.py — ★ 31.10.2026 ФЛАГИ ДУЭЛИ: импорт СКЕЛЕТАЛЬНЫХ моделей флагов (тип 5 — за спиной участника,
#   тип 6 — в центре арены) с клипом anim_00. [ОРИГИНАЛ]
# Источник: RawAssets/Models/DuelFlag/{pvp_flag_back,pvp_flag_land}.glb (`duel_flag_export_models.py`, разбор там же).
# Назначение: /Game/AsdaAssets/Models/Skeletal/DuelFlag/<модель>/... (новая папка; статики Models/Char/pvp_flag_* не трогаются).
# Материалы: MIC модели → мастер `M_AsdaObjectLit` (сам мастер НЕ меняется) + текстура по имени материала glb
#   (`Pvp_flag_back` → pvp_flag_back.tex, `Pvp_flag_land` → pvp_flag_land.tex; эталон `meshLoad_BindSubsetTexturesWithState` —
#   у типов 5/6 текстура из .jbm, подмены нет). Nanite снят (скелетал). Механика — та же библиотека, что у флага клана
#   (`asda_import_clanflag_model.py`, `asda_model_import_ue`).
# Повтор: существующая папка модели не переимпортируется без DUELFLAG_REIMPORT=1 (ассет могли править руками).
# Запуск: MSYS_NO_PATHCONV=1 python Tools/run_headless_safe.py Tools/duel_flag_import_models.py --marker "[duelflag] ИТОГ"
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))   # каталог инструментов
sys.path.insert(0, HERE)
import asda_ue_paths as _U          # noqa: E402  корни проекта
import asda_model_import_ue as MI   # noqa: E402  импорт пачкой, MIC, Nanite

RAW = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "DuelFlag"))   # .glb экспорта
DEST = _U.G_MODELS_SKELETAL + "/DuelFlag"                                              # корень ассетов
MODELS = ["pvp_flag_back", "pvp_flag_land"]                                          # тип 5, тип 6
REIMPORT = os.environ.get("DUELFLAG_REIMPORT") == "1"
EAL = unreal.EditorAssetLibrary


def log(m):
    """Журнал импорта флагов дуэли."""
    unreal.log("[duelflag] " + str(m))


def main():
    """Импорт обеих моделей, подвязка MIC, снятие Nanite. Возвращает True при полном успехе."""
    master = unreal.load_asset(_U.M_OBJECT_LIT)
    tex = MI.tex_index("all_last", root=_U.G_TEXTURES)
    ok = True
    for model in MODELS:
        glb = os.path.join(RAW, model + ".glb")
        root = "%s/%s" % (DEST, model)
        if not os.path.isfile(glb):
            log("НЕТ %s — сначала python Tools/duel_flag_export_models.py" % glb); ok = False; continue
        if EAL.does_directory_exist(root) and not REIMPORT:
            log("%s: папка уже есть — пропуск (перезапись только с DUELFLAG_REIMPORT=1)" % root); continue
        MI.import_batch([glb], DEST)
        EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)

        def tex_for(name, _m=model):
            """Текстура MIC: имя материала glb = имя текстуры модели."""
            return tex.get(_m.lower())
        st = MI.wire_materials(root, tex_for=tex_for, master=master, on_miss=lambda n: log("  !! MIC %s: текстура не найдена" % n))
        MI.disable_nanite(root)
        EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
        sk = EAL.does_asset_exist("%s/SkeletalMeshes/%s" % (root, model))
        # единственный клип .glb Interchange называет «<модель>_Anim» (при нескольких — «<модель>anim_NN»)
        an = EAL.does_asset_exist("%s/SkeletalMeshes/%s_Anim" % (root, model)) or EAL.does_asset_exist("%s/SkeletalMeshes/%sanim_00" % (root, model))
        log("%s: скелетал=%s клип=%s MIC=%d (с текстурой %d)" % (model, sk, an, st["n_all"], st["n_tex"]))
        ok = ok and sk and an and st["n_tex"] >= 1
    log("ИТОГ: %s" % ("ВСЕ OK" if ok else "ЕСТЬ ПРОБЛЕМЫ"))
    return ok


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
