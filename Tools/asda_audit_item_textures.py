# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_audit_item_textures.py — ИНВЕНТАРИЗАЦИЯ текстур 3D-моделей предметов (только чтение).

Отвечает на вопрос «у каких предметов на модели окажется НЕ та текстура».
Считает две независимые вещи:

  A. РАНТАЙМ (то, что увидит игрок). Для каждого предмета item_list раскладывается эталонная
     пара (модель, текстура) — см. asda_item_drop_tex.py — и проверяется, что оба ассета
     импортированы. Рантайм AsdaLootActor красит модель именно этой текстурой.
  B. MIC-ДЕФОЛТ (то, что видно в редакторе и что подставится, если рантайм текстуру не нашёл).
     Для каждого MIC под Models/<категория> читается СОБСТВЕННЫЙ оверрайд параметра Tex
     и сравнивается с ожидаемой текстурой модели.

Запуск: run_headless.bat asda_audit_item_textures.py
Отчёт:  Reports/item_texture_audit_data.txt  (+ построчный лог)
"""
import os, sys, collections, io, unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_item_drop_tex as POLICY

TEX_PKG   = "/Game/AsdaAssets/Textures/Items/Texture"
MODELS    = "/Game/AsdaAssets/Models"
ITEM_CAT  = "Items"
import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
OUT       = os.path.join(_P.RE_REPORTS, "item_texture_audit_data.txt")

EAL = unreal.EditorAssetLibrary
LINES = []


def log(m):
    unreal.log_warning("[item-audit] %s" % m)
    LINES.append(m)


def _tex_exists(name):
    return bool(name) and EAL.does_asset_exist("%s/%s" % (TEX_PKG, name))


def _mesh_exists(cat, name):
    return bool(name) and EAL.does_asset_exist(
        "/Game/AsdaAssets/Models/%s/%s/StaticMeshes/%s" % (cat, name, name))


def _own_tex(mi):
    try:
        for tp in (mi.get_editor_property("texture_parameter_values") or []):
            nm = tp.get_editor_property("parameter_info").get_editor_property("name")
            val = tp.get_editor_property("parameter_value")
            if str(nm) == "Tex" and val:
                return str(val.get_name())
    except Exception:
        pass
    return None


def audit_runtime():
    items, img = POLICY.load_tables()
    stat = collections.Counter()
    bad = collections.Counter()
    mesh_cache, tex_cache = {}, {}

    for rec in items:
        model, tex, custom = POLICY.resolve_drop_visual(rec, img)
        cat = "экип/оружие (custom-tex)" if custom else "img-модель"
        if not model:
            stat[("предметы без модели (img-запись пуста)", "—")] += 1
            continue
        mk = model.lower()
        if mk not in mesh_cache:
            mesh_cache[mk] = _mesh_exists(ITEM_CAT, model)
        if not mesh_cache[mk]:
            stat[(cat, "меш НЕ импортирован")] += 1
            bad["MESH %s" % model] += 1
            continue
        if not tex:
            stat[(cat, "текстуры нет и в оригинале (textureMode>2 / пустое поле)")] += 1
            continue
        tk = tex.lower()
        if tk not in tex_cache:
            tex_cache[tk] = _tex_exists(tex)
        if tex_cache[tk]:
            stat[(cat, "OK")] += 1
        else:
            stat[(cat, "ТЕКСТУРА НЕ импортирована")] += 1
            bad["TEX %s (модель %s)" % (tex, model)] += 1
    return stat, bad


def audit_mics():
    mesh_tex = POLICY.build_mesh_default_tex()
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    per_cat = collections.defaultdict(collections.Counter)
    wrong = []
    for ad in ar.get_assets_by_path(MODELS, recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        parts = str(ad.package_name).split("/")
        try:
            cat = parts[parts.index("Models") + 1]
            mesh = parts[parts.index("Models") + 2]
        except (ValueError, IndexError):
            continue
        mi = EAL.load_asset(str(ad.package_name))
        if not mi:
            continue
        cur = _own_tex(mi)
        if cat != ITEM_CAT:
            per_cat[cat]["с текстурой" if cur else "БЕЗ текстуры"] += 1
            continue
        want = mesh_tex.get(mesh.lower()) or (mesh if _tex_exists(mesh) else None)
        if not want:
            per_cat[cat]["одноимённой текстуры нет и в оригинале"] += 1
        elif not cur:
            per_cat[cat]["БЕЗ текстуры"] += 1
            wrong.append("%s: <нет> вместо %s" % (mesh, want))
        elif cur.lower() != want.lower():
            per_cat[cat]["ЧУЖАЯ текстура"] += 1
            wrong.append("%s: %s вместо %s" % (mesh, cur, want))
        else:
            per_cat[cat]["OK"] += 1
    return per_cat, wrong


# ═══════════════════════════════════════════════════════════════════════════════
#  ДОБАВЛЕНО 2026-08-01 (круг «Сияющая руда»). Первый аудит проверял ТОЛЬКО «есть ли ассеты»
#  и потому был слеп к трём вещам: (C) взята ли ВЕРНАЯ img-запись, (D) привязан ли материал
#  внутри самого меша, (E) иконка в интерфейсе. Ниже — эти ветки.
# ═══════════════════════════════════════════════════════════════════════════════

def audit_img_binding():
    """C. Доказать, что ImgIndex → строка img-таблицы взята верно (по вшитому в запись itemCode)."""
    ok, mismatched, no_record = POLICY.check_img_binding()
    log("%8d  связка item→img подтверждена вшитым itemCode (DataHex+0)" % ok)
    log("%8d  ImgIndex указывает на ЧУЖУЮ запись" % len(mismatched))
    log("%8d  предметов без img-записи вообще" % len(no_record))
    for m in mismatched[:30]:
        log("   itemCode=%s '%s' ImgIndex=%s → запись принадлежит %s" % m)
    return len(mismatched)


def audit_mesh_material_slots():
    """D. Меш дроп-модели: слоты материалов, их параметр Tex и существование самой текстуры.

    Ловит случай «ассеты на месте, но материал к мешу не привязан» — модель тогда рисуется
    ДЕФОЛТНЫМ материалом движка (ровно «предмет без текстуры/материала»).
    """
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    stat = collections.Counter()
    bad = []
    for ad in ar.get_assets_by_path("%s/%s" % (MODELS, ITEM_CAT), recursive=True):
        if str(ad.asset_class_path.asset_name) != "StaticMesh":
            continue
        sm = EAL.load_asset(str(ad.package_name))
        if not sm:
            continue
        name = str(ad.asset_name)
        mats = sm.get_editor_property("static_materials")
        stat["мешей"] += 1
        if not mats:
            stat["БЕЗ слотов материала"] += 1
            bad.append("%s: слотов материала нет" % name)
            continue
        for i, sl in enumerate(mats):
            mi = sl.get_editor_property("material_interface")
            if not mi:
                stat["слот БЕЗ материала"] += 1
                bad.append("%s[%d]: материал не назначен" % (name, i))
                continue
            tex = _own_tex(mi) if isinstance(mi, unreal.MaterialInstanceConstant) else None
            if tex is None:
                stat["материал без параметра Tex"] += 1
                bad.append("%s[%d]: у материала %s нет параметра Tex" % (name, i, mi.get_name()))
            elif not EAL.does_asset_exist("%s/%s" % (TEX_PKG, tex)):
                stat["Tex указывает на несуществующую текстуру"] += 1
                bad.append("%s[%d]: Tex=%s — ассета нет" % (name, i, tex))
            else:
                stat["слот OK"] += 1
    log("[меши предметов] %s" % dict(stat))
    for b in bad[:40]:
        log("   " + b)
    return len(bad)


def audit_icons():
    """E. Иконка в интерфейсе: атлас существует И ячейка помещается в атлас.

    Раскладка эталона (UAsdaItemTableSubsystem::GetItemIcon): smallFlag=1 → 32px/16 колонок,
    иначе 64px/8 колонок; левый-верх ячейки = size*(cell%cols), size*(cell/cols).
    """
    items, img = POLICY.load_tables()
    stat = collections.Counter()
    bad = collections.Counter()
    size_cache = {}
    for rec in items:
        atlas, cell = POLICY.resolve_icon(rec, img)
        if not atlas or atlas in ("-1", "0"):
            stat["атласа нет в самих данных клиента"] += 1
            continue
        pkg = "/Game/AsdaAssets/Textures/JUI/%s" % POLICY.asset_name(atlas)
        if atlas not in size_cache:
            t = EAL.load_asset(pkg) if EAL.does_asset_exist(pkg) else None
            size_cache[atlas] = (t.blueprint_get_size_x(), t.blueprint_get_size_y()) if t else None
        wh = size_cache[atlas]
        if not wh:
            stat["АТЛАС не импортирован"] += 1
            bad["ATLAS %s" % atlas] += 1
            continue
        ir = img.get(rec["ImgIndex"])
        small = int.from_bytes(bytes.fromhex(ir["DataHex"])[168:172], "little") == 1
        size = 32 if small else 64
        cols = 16 if small else 8
        x, y = size * (cell % cols), size * (cell // cols)
        if cell < 0 or x + size > wh[0] or y + size > wh[1]:
            stat["ячейка ВНЕ атласа"] += 1
            bad["CELL %s#%d (атлас %dx%d)" % (atlas, cell, wh[0], wh[1])] += 1
        else:
            stat["OK"] += 1
    log("[иконки] %s" % dict(stat))
    for k, n in bad.most_common(30):
        log("%6d  %s" % (n, k))
    return sum(stat[k] for k in stat if k not in ("OK", "атласа нет в самих данных клиента"))


def main():
    log("=== A. РАНТАЙМ: пара (модель, текстура) на каждый предмет item_list ===")
    stat, bad = audit_runtime()
    for (cat, verdict), n in sorted(stat.items(), key=lambda kv: -kv[1]):
        log("%8d  %-26s %s" % (n, cat, verdict))
    if bad:
        log("--- отсутствующие ассеты (уникальные) ---")
        for k, n in bad.most_common(60):
            log("%6d  %s" % (n, k))

    log("")
    log("=== B. MIC-ДЕФОЛТЫ по категориям Models/* ===")
    per_cat, wrong = audit_mics()
    for cat in sorted(per_cat):
        log("%-10s %s" % (cat, dict(per_cat[cat])))
    if wrong:
        log("--- Items: MIC с неверным дефолтом ---")
        for w in wrong:
            log("   " + w)

    log("")
    log("=== C. СВЯЗКА item → img-запись (доказательство вшитым itemCode) ===")
    audit_img_binding()

    log("")
    log("=== D. МЕШИ предметов: слоты материалов и их текстуры ===")
    audit_mesh_material_slots()

    log("")
    log("=== E. ИКОНКИ предметов: атлас + ячейка ===")
    audit_icons()

    try:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with io.open(OUT, "w", encoding="utf-8") as f:
            f.write("\n".join(LINES) + "\n")
        unreal.log_warning("[item-audit] отчёт → %s" % OUT)
    except Exception as e:
        unreal.log_error("[item-audit] не записал отчёт: %s" % e)


if __name__ == "__main__":
    main()
