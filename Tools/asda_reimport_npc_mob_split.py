# Copyright (c) 2026 @sitxovski (Discord)
# asda_reimport_npc_mob_split.py — ТОЧЕЧНЫЙ ПЕРЕИМПОРТ моделей NPC/мобов, у которых после правки
#   политики мастеров ИЗМЕНИЛИСЬ ИМЕНА МАТЕРИАЛОВ (то есть появились новые слоты меша).
#
# ★ ЗАЧЕМ ОТДЕЛЬНЫЙ СКРИПТ, А НЕ ШТАТНЫЕ ИМПОРТЁРЫ. `asda_fix_npc_materials.py` и
#   `asda_import_mob_skel.py` СНОСЯТ каталог назначения целиком (WIPE) и заливают всё дерево заново.
#   Здесь нужно переимпортировать 94 модели из 364, поэтому импортируем ТОЧЕЧНО с
#   `replace_existing=True` в те же пакеты: ссылки размещённых акторов и DataTable не рвутся.
#
# ★ ЧТО МЕНЯЕТСЯ В .glb. `asda_mesh_render_class.material_names()` даёт подсеткам РАЗНЫХ техник,
#   делящим одну текстуру, РАЗНЫЕ имена материалов (`_msk` — техника tNoSkin рядом с tSkin/tRigid,
#   `_blend` — tNoSkin с рендер-битом `mr+0x004 & 0x1|0x2`). UE заводит им отдельные слоты, и только
#   после этого им можно дать разные мастера. Перечень моделей вычисляется, а не хранится списком:
#   `MC.needs_reexport(recs)` сравнивает новые имена со старыми (голыми).
#
# ПОРЯДОК: сборка .glb (asda_build_npc_fixed / asda_build_mob_fixed / ASDA_MODEL_CATS=Mob
#   asda_build_models) → ЭТОТ скрипт → он сам довяжет текстуры и мастера.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_reimport_npc_mob_split.py
#   ASDA_ONLY_ROOTS=NPC,MobSkel,Mob — ограничить корни;  ASDA_LIST_ONLY=1 — только показать список.
import os
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zipfile

import unreal
import asda_paths as _P
import asda_ue_paths as _U
import asda_jbm as JBM
import asda_mesh_render_class as MC
import asda_npc_mob_masters as MM

CHUNK = 8            # как в штатных импортёрах: маленький чанк — меньше всплеск GPU/памяти

# (ключ, корень /Game, папка .glb, архив .jbm источника)
ROOTS = (
    ("NPC",     _U.G_MODELS_NPC,     os.path.join(_U.RAW_MODELS, "Skeletal", "NPC"), _P.NPC_MODEL_BNZ),
    ("MobSkel", _U.G_MODELS_MOBSKEL, os.path.join(_U.RAW_MODELS, "Skeletal", "Mob"), _P.MOB_MODEL_BNZ),
    ("Mob",     _U.G_MODELS_MOB,     os.path.join(_U.RAW_MODELS, "Mob"),
     os.path.join(_P.DATA_ORIG, "Mob", "Model", "MobModel.bnz")),
)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m): unreal.log_warning("[переимпорт NPC/мобов] %s" % m)


def models_to_reimport(archive, glb_dir):
    """Модели, у которых имена материалов изменились ⇒ нужен переимпорт (и .glb уже пересобран)."""
    have = {os.path.splitext(f)[0].lower(): os.path.join(glb_dir, f)
            for f in os.listdir(glb_dir) if f.lower().endswith(".glb")} if os.path.isdir(glb_dir) else {}
    out = []
    with zipfile.ZipFile(archive) as z:
        for n in sorted(x for x in z.namelist() if x.lower().endswith(".jbm")):
            model = os.path.splitext(os.path.basename(n))[0]
            path = have.get(model.lower())
            if not path:
                continue
            hdr = JBM.parse_header(z.read(n))
            if hdr and MC.needs_reexport(hdr["recs"]):
                out.append((model, path))
    return out


# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def import_batch(paths, dest):
    """Импорт пачкой (replace_existing=True) — asda_model_import_ue.import_batch."""
    return MI.import_batch(paths, dest)


def tex_index():
    """Индекс Texture2D (пресет «noportrait_first» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("noportrait_first")


def find_tex(name, tex, kind=None):
    """Текстура для слота/MIC (пресет «class_notail» библиотеки = прежняя копия этого файла).
    ★ 15.09.2026: kind — тип модели (MI.kind_for_package корня NPC/MobSkel/Mob): при дублях имени копия из папки
    своего типа, как в оригинале (`Reports/dedup_audit/tex_dup_rule.md`)."""
    return MI.find_tex(name, tex, "class_notail", kind=kind)


def wire_new_mics(root, models, tex):
    """Текстура для MIC переимпортированных моделей (у новых слотов её нет вовсе)."""
    n = miss = 0
    for model in models:
        for ad in AR.get_assets_by_path(unreal.Name("%s/%s" % (root, model)), recursive=True):
            if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            mic = unreal.load_asset(str(ad.package_name))
            cur = None
            try:
                cur = MEL.get_material_instance_texture_parameter_value(mic, "Tex")
            except Exception:
                pass
            if cur:
                continue                      # уже стоит (мобам её ставит asda_fix_mob_materials)
            tp = find_tex(str(ad.asset_name), tex, MI.kind_for_package(root))   # ★ 15.09: тип модели по корню
            if MI.data_pinned_dir(str(ad.package_name)):   # ★ 15.09: текстура по данным (MI.DATA_PINNED_TEX) — пустую Tex заполнить ею
                tp = MI.data_pinned_tex(str(ad.package_name), None)
            if tp:
                MEL.set_material_instance_texture_parameter_value(mic, "Tex", unreal.load_asset(tp))
                mic.modify(); EAL.save_loaded_asset(mic, False); n += 1
            else:
                miss += 1
                log("   без текстуры: %s/%s" % (model, ad.asset_name))
    return n, miss


def disable_nanite(root, models):
    """Nanite у проекта выключен (низкополигональный контент 2008) — снимаем и с новых мешей."""
    for model in models:
        MI.disable_nanite("%s/%s" % (root, model))


def main():
    only = set(s.strip() for s in os.environ.get("ASDA_ONLY_ROOTS", "").split(",") if s.strip())
    list_only = os.environ.get("ASDA_LIST_ONLY", "") not in ("", "0")
    tex = tex_index()
    log("текстур в индексе: %d" % len(tex))
    touched = {}
    for key, root, glb_dir, archive in ROOTS:
        if only and key not in only:
            continue
        todo = models_to_reimport(archive, glb_dir)
        log("%s: к переимпорту %d моделей" % (key, len(todo)))
        for m, _p in todo:
            unreal.log("   %s" % m)
        if list_only or not todo:
            continue
        paths = [p for _m, p in todo]
        for i in range(0, len(paths), CHUNK):
            import_batch(paths[i:i + CHUNK], root)
            log("  %s: импортировано %d/%d" % (key, min(i + CHUNK, len(paths)), len(paths)))
            EAL.save_directory(root, only_if_is_dirty=True, recursive=True)
            try:
                unreal.SystemLibrary.collect_garbage()
            except Exception:
                pass
        names = [m for m, _p in todo]
        touched[key] = names
        n, miss = wire_new_mics(root, names, tex)
        disable_nanite(root, names)
        log("  %s: текстур доназначено %d, без текстуры %d" % (key, n, miss))
        EAL.save_directory(root, only_if_is_dirty=True, recursive=True)

    if touched and not list_only:
        # мастера по технике — сразу, тем же прогоном (иначе новые слоты остались бы на masked)
        MM.apply(only_roots=set(touched.keys()))
        for key, root, _g, _a in ROOTS:
            if key in touched:
                EAL.save_directory(root, only_if_is_dirty=True, recursive=True)
    unreal.log("=== NPC/MOB SPLIT REIMPORT DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
