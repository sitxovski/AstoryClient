# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_clanflag_model.py — импорт СКЕЛЕТАЛЬНОЙ модели ФЛАГА КЛАНА (флажок на палочке,
#   который висит на транспорте у члена гильдии).
# Источник: RawAssets/Models/ClanFlag/small_flag.glb (1 SkeletalMesh + клипы anim_00/01/02).
# Назначение: /Game/AsdaAssets/Models/Skeletal/ClanFlag/small_flag/...
#
# Эталон (clan_flag_mount_re.md):
#   CBattleFlag_Construct@0xFAF3D0, тип 1 = small_flag.jbm + small_flag000/001/002.jbb;
#   mapDecoModelLoadStage@0xFB29F0 стадия 2 при типе 1:
#     mesh_BuildAndAssignParts(..., pTextureName="clan_flag_<charId>.tex", "template.TEX", lod)
#   ⇒ подсетка с текстурой "template.TEX" (ПОЛОТНИЩЕ) получает ИСПЕЧЁННУЮ ЭМБЛЕМУ (RT 128×64),
#     подсетка "flag.TEX" (ДРЕВКО) остаётся со своей текстурой.
#   Поэтому здесь: MIC «flag» → текстура flag; MIC «template» → текстура template (плейсхолдер,
#   в рантайме подменяется динамическим материалом с RT-эмблемой, см. AAsdaPlayerCharacter).
# Слоты анимации: anim_00/anim_01 = покой (эталон случайно чередует их), anim_02 = движение.
# Запуск: run_headless.bat asda_import_clanflag_model.py
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_ue_paths as _U

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "ClanFlag"))
DEST = _U.G_MODELS_CLANFLAG
TEX_ROOT = _U.G_TEXTURES
MASTER   = _U.M_OBJECT_LIT
MODEL    = "small_flag"
# имя материала в .glb (= базовое имя текстуры из .jbm, matIdx@0x1f4) -> имя текстуры проекта
MAT_TEX  = {"flag": "flag", "template": "template"}
# ★ CLANFLAG_REIMPORT=1 — полный переимпорт (удалить папку до импорта). Нужен после смены скин-пайплайна:
#   меняется состав костей (prune_unskinned), Interchange со старым Skeleton молча оставит старый ассет.
REIMPORT = os.environ.get("CLANFLAG_REIMPORT") == "1"

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[clanflag] " + str(m))


# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)


def import_batch(paths, dest):
    """Импорт пачкой (replace_existing=True) — asda_model_import_ue.import_batch."""
    return MI.import_batch(paths, dest)


def wire_materials(tex, master):
    """MIC модели → мастер + Tex по имени материала (полотнище/древко — РАЗНЫЕ текстуры).
    ★ master_for НЕ ПОДКЛЮЧЁН — решение координатора волны 2 (14.09.2026): подключение сменит родителя существующих MIC при следующем прогоне (у полотнища/древка сменился бы мастер), а владелец правит ассеты руками — не терять. Отчёт `Reports/dedup_audit/wave2_import.md` §3.3, §8."""
    def key_of(name):
        # имя MIC содержит имя материала glb ("flag" / "template"); берём самое длинное совпадение
        name = name.lower(); key = None
        for k in MAT_TEX:
            if k in name:
                key = k if (key is None or len(k) > len(key)) else key
        return key
    def tex_for(name):
        key = key_of(name)
        return tex.get(MAT_TEX.get(key, "").lower()) if key else None
    def on_miss(name):
        log("  !! MIC %s: не определена текстура (key=%s)" % (name.lower(), key_of(name)))
    st = MI.wire_materials("%s/%s" % (DEST, MODEL), tex_for=tex_for, master=master, on_miss=on_miss)
    return st["n_all"]


def disable_nanite():
    """Снять Nanite с мешей модели — asda_model_import_ue.disable_nanite."""
    MI.disable_nanite("%s/%s" % (DEST, MODEL))


def main():
    glb = os.path.join(RAW, MODEL + ".glb")
    if not os.path.isfile(glb):
        log("НЕТ %s — сначала: python asda_build_skinned_models.py clanflag" % glb); return
    if REIMPORT and EAL.does_directory_exist(DEST):
        log("ПЕРЕИМПОРТ: удаляю %s (старый Skeleton несовместим по составу костей)" % DEST)
        EAL.delete_directory(DEST)
    master = unreal.load_asset(MASTER)
    tex = tex_index()
    log("текстур в индексе: %d" % len(tex))

    import_batch([glb], DEST)
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    mats = wire_materials(tex, master)
    disable_nanite()
    EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)

    # отчёт
    kinds = {}
    for ad in AR.get_assets_by_path(unreal.Name("%s/%s" % (DEST, MODEL)), recursive=True):
        k = str(ad.asset_class_path.asset_name)
        kinds[k] = kinds.get(k, 0) + 1
    log("ГОТОВО: материалов настроено=%d, ассеты=%s" % (mats, kinds))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
