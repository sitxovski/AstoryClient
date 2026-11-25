# Copyright (c) 2026 @sitxovski (Discord)
# Проба: есть ли в реестре меты .JUI записи окон хвастовства (28000 / 28100) и их ручки
#   перетаскивания (EVS{EVT=6, RET=0x20003}). Без них окна не будут таскаться.
import unreal

CAND = [
    "/Game/AsdaAssets/UI/DA_JuiWindowMeta",
    "/Game/AsdaAssets/UI/Data/DA_JuiWindowMeta",
    "/Game/AsdaAssets/Data/DA_JuiWindowMeta",
]

asset = None


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global asset
    for p in CAND:
        if unreal.EditorAssetLibrary.does_asset_exist(p):
            asset = unreal.EditorAssetLibrary.load_asset(p)
            print("МЕТА:", p)
            break
    if not asset:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/AsdaReborn", "AsdaJuiWindowMetaAsset")):
            print("НАЙДЕН реестр меты:", a.package_name)
            asset = unreal.EditorAssetLibrary.load_asset(str(a.package_name))
            break
    if not asset:
        print("РЕЕСТР МЕТЫ НЕ НАЙДЕН")
    else:
        wins = asset.get_editor_property("windows")
        print("окон в реестре:", len(wins))
        for w in wins:
            wid = str(w.get_editor_property("window_id"))
            if wid in ("Open_Item_Win", "Opend_Item_Win"):
                dh = w.get_editor_property("drag_handles")
                nh = w.get_editor_property("no_hit_widgets")
                print("=== %s idnum=%s size=%s ручек=%d NoSelect=%d" % (
                    wid, w.get_editor_property("id_num"), w.get_editor_property("base_size"), len(dh), len(nh)))
                for h in dh:
                    print("    ручка=%s цель=%s(%s) корень=%s окноЦели=%s виджетыЦели=%s" % (
                        h.get_editor_property("handle_widget"),
                        h.get_editor_property("target_control"), h.get_editor_property("target_id_num"),
                        h.get_editor_property("target_is_window_root"),
                        h.get_editor_property("target_window_id"),
                        [str(x) for x in h.get_editor_property("target_widgets")]))
                print("    NoSelect-виджеты:", [str(x) for x in nh])
    for p in ["/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_Open_Item_Win",
              "/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_Opend_Item_Win"]:
        cls = unreal.EditorAssetLibrary.load_blueprint_class(p)
        print("КЛАСС %s: %s" % (p, "OK" if cls else "НЕ ЗАГРУЗИЛСЯ"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
