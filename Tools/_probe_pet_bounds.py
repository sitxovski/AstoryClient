# Copyright (c) 2026 @sitxovski (Discord)
# _probe_pet_bounds.py — контроль после фикса скиннинга петов: bounds/кости/масштаб реф-позы.
# «Взорванный» bind виден как гигантский bounds или кость с масштабом реф-позы в десятки раз.
import unreal
AR = unreal.AssetRegistryHelpers.get_asset_registry()
DEST = "/Game/AsdaAssets/Models/Skeletal/Pet"
def log(m): unreal.log_warning("[pet-bounds] %s" % m)
bad = 0; n = 0


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global bad, m, n
    for ad in AR.get_assets_by_path(unreal.Name(DEST), recursive=True):
        if str(ad.asset_class_path.asset_name) != "SkeletalMesh": continue
        m = unreal.load_asset(str(ad.package_name)); n += 1
        b = m.get_bounds(); ext = b.box_extent
        sk = m.get_editor_property("skeleton")
        nb = len(sk.get_editor_property("bone_tree")) if sk else -1
        mx = max(ext.x, ext.y, ext.z)
        flag = ""
        if mx > 20000.0: flag = "  <<< ПОДОЗРИТЕЛЬНО (bounds)"; bad += 1
        log("%-26s bounds=(%.0f,%.0f,%.0f) костей=%d%s" % (ad.asset_name, ext.x, ext.y, ext.z, nb, flag))
    log("ИТОГО мешей=%d, подозрительных=%d" % (n, bad))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
