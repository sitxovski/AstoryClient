# Copyright (c) 2026 @sitxovski (Discord)
# asda_fable_mounts_import_ue.py — headless UE: импорт ТОЛЬКО 28 новых маунтов донора AsdaFableClassic
# (itm1_n_96_vehicle_0053…0080) из RawAssets/Models/Vehicle в /Game/AsdaAssets/Models/Skeletal/Vehicle/<модель>.
# Шаги те же, что у asda_import_vehicle_models.py (его функции и переиспользуются), но материалы и Nanite
# настраиваются ТОЛЬКО у новых моделей: штатный main() проходит по всем 80 .glb и пересохранил бы MIC прежних
# 52 моделей (закон «правки владельца в ассетах не терять»). Уже импортированная модель пропускается.
# Предусловие: текстуры <модель>_t импортированы (asda_fable_mounts_textures.py + asda_import_textures.py).
# Запуск: python run_headless_safe.py asda_fable_mounts_import_ue.py --marker "ЗАВЕРШЕНО: fable-маунты"
import os, re, sys, importlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

NEW_MODEL = re.compile(r"^itm1_n_96_vehicle_00(5[3-9]|[67]\d|80)$", re.I)   # 28 новых моделей донора


def main():
    import unreal
    V = importlib.import_module("asda_import_vehicle_models")   # импорт модуля без побочных эффектов
    eal = unreal.EditorAssetLibrary
    models = sorted(os.path.splitext(f)[0] for f in os.listdir(V.RAW)
                    if f.lower().endswith(".glb") and NEW_MODEL.match(os.path.splitext(f)[0]))
    todo = [m for m in models if not (eal.does_directory_exist("%s/%s" % (V.DEST, m)) and eal.list_assets("%s/%s" % (V.DEST, m)))]
    V.log("fable: моделей %d, к импорту %d" % (len(models), len(todo)))
    for i in range(0, len(todo), V.CHUNK):
        V.import_batch([os.path.join(V.RAW, m + ".glb") for m in todo[i:i + V.CHUNK]], V.DEST)
        eal.save_directory(V.DEST, only_if_is_dirty=True, recursive=True)
        unreal.SystemLibrary.collect_garbage()
    master = unreal.load_asset(V.MASTER)
    tex = V.tex_index()
    mats, miss = 0, []
    for m in todo:
        if not eal.does_directory_exist("%s/%s" % (V.DEST, m)):
            miss.append(m)
            continue
        mats += V.wire_model_materials(m, tex, master)
        V.disable_nanite(m)
        eal.save_directory("%s/%s" % (V.DEST, m), only_if_is_dirty=False, recursive=True)
    V.log("fable: импортировано %d, материалов %d, не появилось %d %s" % (len(todo) - len(miss), mats, len(miss), miss))
    unreal.log("ЗАВЕРШЕНО: fable-маунты")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
