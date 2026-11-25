# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_items_loot_tex.py — доимпорт недостающих item-текстур + починка MIC дроп-моделей (Items).
# Причина серых моделей лута (юзер: «D ranked material без текстуры»): 44 .TEX клиента в формате DXT2
#   (premultiplied DXT3) не конвертировались Pillow и не были импортированы; MIC img-моделей остались
#   без Tex (fixer glTF-материалов текстуры сознательно не трогал). Пайплайн:
#   1) PNG уже извлечены (DXT2→DXT3+unpremultiply) в RawAssets/Textures/Data/Item/Texture;
#   2) импорт недостающих в /Game/AsdaAssets/Textures/Items/Texture (legacy factory);
#   3) MIC в Models/Items → текстура по ДАННЫМ item_img_table (см. asda_item_drop_tex.py).
#
# ★ 2026-08-01 (баг «Знак волка красный»): пункт 3 РАНЬШЕ подбирал текстуру ПО ИМЕНИ MIC, а имя MIC
#   пришло из строки материала внутри .jbm, которую оригинал НЕ ЧИТАЕТ ВООБЩЕ
#   (CModel_LoadGeometry@0x119CA00 обнуляет слот текстуры каждого сабмеша). У 49 из 243 дроп-моделей
#   эта строка указывает на ЧУЖОЙ файл или содержит корейский мусор: у itm1_n_98_othr224_00
#   («Знак волка», itemId 22647, img 2096) там записано «itm1_n_98_othr222_00.png» — КРАСНАЯ текстура.
#   Теперь дефолт MIC берётся из img-таблицы (самая частая TexMale для этой модели), а рантайм
#   (AsdaLootActor) в любом случае красит МОДЕЛЬ ТЕКСТУРОЙ КОНКРЕТНОГО ПРЕДМЕТА — как в эталоне.
# Запуск: run_headless.bat asda_fix_items_loot_tex.py     (аргумент -dry — только отчёт)
import os, re, sys, unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_item_drop_tex as POLICY

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
RAW = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Textures", "Data", "Item", "Texture")
TEX_PKG = "/Game/AsdaAssets/Textures/Items/Texture"
MODELS = "/Game/AsdaAssets/Models/Items"

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

DRY = "-dry" in sys.argv


def log(m): unreal.log_warning("[itemtex] %s" % m)


def import_missing():
    # legacy-фактори (Interchange-DDS выключать не нужно — тут PNG).
    tasks = []
    n_have = n_new = 0
    for f in sorted(os.listdir(RAW)):
        if not f.lower().endswith(".png"):
            continue
        base = os.path.splitext(f)[0]
        if EAL.does_asset_exist("%s/%s" % (TEX_PKG, base)):
            n_have += 1
            continue
        t = unreal.AssetImportTask()
        t.filename = os.path.join(RAW, f)
        t.destination_path = TEX_PKG
        t.automated = True
        t.save = True
        t.replace_existing = False
        tasks.append(t)
        n_new += 1
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    log("текстуры: уже было %d, импортировано %d" % (n_have, n_new))


def _current_tex_name(mi):
    """Собственный (не унаследованный) оверрайд параметра Tex у инстанса → имя ассета либо None.

    ВАЖНО: get_material_instance_texture_parameter_value возвращает УНАСЛЕДОВАННЫЙ дефолт мастера
    (не-None даже без оверрайда) → читаем СОБСТВЕННЫЕ texture_parameter_values.
    """
    try:
        for tp in (mi.get_editor_property("texture_parameter_values") or []):
            nm = tp.get_editor_property("parameter_info").get_editor_property("name")
            val = tp.get_editor_property("parameter_value")
            if str(nm) == "Tex" and val:
                return str(val.get_name())
    except Exception:
        pass
    return None


def _name_fallbacks(base):
    """Фолбэки под артефакты Interchange/jbm: «-N» дубликаты; хвостовой «_»; порядковый суффикс
    ПОСЛЕ базового _NN (arrw001_00 + «1..5» → arrw001_001); опечатка jbm «item1_» вместо «itm1_»."""
    cands = [base]
    if "-" in base:
        cands.append(base.rsplit("-", 1)[0])
    if base.endswith("_"):
        cands.append(base.rstrip("_"))
    m = re.match(r"^(.*_\d\d)\d+$", base)
    if m:
        cands.append(m.group(1))
    cands += [c.replace("item1_", "itm1_") for c in list(cands) if c.startswith("item1_")]
    return cands


def _mesh_name_of(mi_package):
    """/Game/AsdaAssets/Models/Items/<МЕШ>/Materials/<MIC> → «<МЕШ>»."""
    parts = str(mi_package).split("/")
    try:
        return parts[parts.index("Items") + 1]
    except (ValueError, IndexError):
        return ""


def fix_mics():
    # Дефолт берётся ИЗ ДАННЫХ (item_img_table), а не из имени MIC/меша — см. шапку файла.
    mesh_tex = POLICY.build_mesh_default_tex()
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ok = fixed = repointed = miss = 0
    for ad in ar.get_assets_by_path(MODELS, recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        mi = EAL.load_asset(str(ad.package_name))
        if not mi:
            continue
        mesh = _mesh_name_of(ad.package_name)
        # Порядок кандидатов: (1) текстура из img-таблицы для этой модели, (2) одноимённая мешу,
        # (3) исторические фолбэки по имени MIC (только если данных по модели нет).
        cands = []
        if mesh.lower() in mesh_tex:
            cands.append(mesh_tex[mesh.lower()])
        if mesh:
            cands += _name_fallbacks(mesh)
        cands += _name_fallbacks(str(ad.asset_name))

        tex = want = None
        for c in cands:
            p = "%s/%s" % (TEX_PKG, c)
            if EAL.does_asset_exist(p):
                want, tex = c, EAL.load_asset(p)
                break
        if not tex:
            miss += 1
            log("  нет текстуры-кандидата для меша %s (MIC %s)" % (mesh, ad.asset_name))
            continue

        cur = _current_tex_name(mi)
        if cur and cur.lower() == want.lower():
            ok += 1
            continue
        if DRY:
            log("  %s: %s → %s" % (mesh, cur or "<нет>", want))
        else:
            MEL.set_material_instance_texture_parameter_value(mi, "Tex", tex)
            mi.modify()
            EAL.save_loaded_asset(mi, False)
        if cur:
            repointed += 1
        else:
            fixed += 1
    log("MIC Items: верных %d, доназначено %d, ПЕРЕНАЗНАЧЕНО с чужой %d, без кандидата %d%s"
        % (ok, fixed, repointed, miss, " (DRY)" if DRY else ""))


if __name__ == "__main__":
    import_missing()
    fix_mics()
    log("ГОТОВО")
