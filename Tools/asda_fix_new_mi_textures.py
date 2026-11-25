# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_new_mi_textures.py — дозаполнить текстуру Tex в MIC новых мешей правки дедупа .mapraw (28.10.2026).
#
# ★ ДЕФЕКТ. asda_fix_map_patch_dedup.py собрал 189 новых SM; материал брался asda_build_map_level.get_material →
#   find_texture, а индекс текстур строился по реестру ассетов, который в headless-сессии ещё не был досканирован.
#   Для части имён find_texture вернул None → MIC создан с текстурой мастера по умолчанию (DefaultTexture):
#   «светлая плита» вместо эмблемы unicon03 на silalis, деревья natu_bigtree03 на silalis/firewayforest, egung на flammio.
#   Корень исправлен в find_texture (синхронный скан). Здесь — точечная починка уже созданных MIC.
# ★ ЧТО МЕНЯЕТСЯ. Только параметр Tex в MIC новых SM (список — apply_report.json правки), и только если он не равен
#   текстуре модели из .mapraw. Уровни (.umap) не трогаются вовсе.
# env: ASDA_APPLY=1 (запись; иначе сухой прогон), ASDA_BACKUP (обязателен при записи), ASDA_REPORT (json).
# Маркер конца: «ЗАВЕРШЕНО asda_fix_new_mi_textures».
import os, sys, json, shutil
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_build_map_level as BML
import asda_maps as _MAPS

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
CONTENT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content"
APPLY_REPORT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/map_children_transforms/apply_report.json"
RAW = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps"
APPLY = os.environ.get("ASDA_APPLY", "0") == "1"
BACKUP = os.environ.get("ASDA_BACKUP", "")
REPORT = os.environ.get("ASDA_REPORT", "")


def log(m):
    unreal.log("[mitex] " + str(m))


def find_tex_on_disk(asset, texbase):
    """Текстура по имени из .mapraw — поиском .uasset на диске (без реестра): папка карты, затем AsdaAssets;
    редиректор разыменовывается."""
    want = BML._san(texbase).lower()
    # папка карты может содержать лишь РЕДИРЕКТОРЫ на AsdaAssets (перенос 17.08), а destination_object из Python 5.8
    # недоступен — редиректоры пропускаются, цель берётся по одноимённому файлу второго корня
    for root in ("Map/%s/Texture" % asset, "AsdaAssets/Textures/Maps/%s/Texture" % asset):
        d = os.path.join(CONTENT, root)
        if not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            if f.lower() == want + ".uasset":
                t = EAL.load_asset("/Game/%s/%s" % (root, f[:-7]))
                if isinstance(t, unreal.Texture):
                    return t
    return None


def main():
    if APPLY and not BACKUP:
        log("ASDA_APPLY=1 без ASDA_BACKUP — отказ"); return
    rep = json.load(open(APPLY_REPORT, encoding="utf-8"))
    out = {"checked": 0, "ok": 0, "fix": [], "no_texture": [], "fixed": 0}
    for r in rep:
        m = r.get("map"); news = r.get("new_comp", [])
        if not news:
            continue
        asset = _MAPS.map_asset_name(m)
        models = {("SM_" + x["name"]): x for x in BML.read_mapraw(os.path.join(RAW, m + ".mapraw"))["models"]}
        seen_mi = set()
        for key in news:
            out["checked"] += 1
            smn = key.split("/")[-1]
            sm = EAL.load_asset(key)
            mdl = models.get(smn)
            mats = sm.get_editor_property("static_materials") if sm else []
            mi = mats[0].material_interface if mats else None
            if mdl is None or not isinstance(mi, unreal.MaterialInstanceConstant):
                out["no_texture"].append([m, smn, "нет модели в .mapraw или MIC"]); continue
            cur = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
            want = BML._san(mdl["tex"]).lower()
            if cur is not None and cur.get_name().lower() == want:
                out["ok"] += 1; continue
            mip = mi.get_path_name().split(".")[0]
            if mip in seen_mi:
                continue
            seen_mi.add(mip)
            tex = find_tex_on_disk(asset, mdl["tex"])
            if tex is None:
                out["no_texture"].append([m, smn, mdl["tex"]]); continue
            out["fix"].append([m, mip, cur.get_path_name() if cur else None, tex.get_path_name()])
            if APPLY:
                rel = mip[len("/Game/"):] + ".uasset"
                src = os.path.join(CONTENT, rel); dst = os.path.join(BACKUP, "AstoryClient/Content", rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                if not os.path.exists(dst):
                    shutil.copy2(src, dst)
                if os.path.getsize(src) != os.path.getsize(dst):
                    log("бэкап %s не совпал — пропуск" % mip); continue
                MEL.set_material_instance_texture_parameter_value(mi, "Tex", tex)
                if MEL.get_material_instance_texture_parameter_value(mi, "Tex") != tex:
                    log("%s: Tex не встал" % mip); continue
                EAL.save_loaded_asset(mi)
                unreal.log("ASDA_WRITES_ASSET: " + mip)
                out["fixed"] += 1
    log("проверено SM %d, верных %d, MIC к починке %d, без найденной текстуры %d, починено %d"
        % (out["checked"], out["ok"], len(out["fix"]), len(out["no_texture"]), out["fixed"]))
    for f in out["fix"]:
        log("   %s" % f)
    for f in out["no_texture"]:
        log("   НЕТ ТЕКСТУРЫ %s" % f)
    if REPORT:
        with open(REPORT, "w", encoding="utf-8") as fh:
            json.dump(out, fh, ensure_ascii=False, indent=1)
    unreal.log("ЗАВЕРШЕНО asda_fix_new_mi_textures (%s)" % ("ЗАПИСЬ" if APPLY else "сухой прогон"))


# ★ ЗАКОН [[tools-no-import-side-effects]]
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
