# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_mob_texture_dt.py — ЭТАЛОННАЯ таблица текстур мобов ПО ТИПУ + материалы-варианты.
#
# ★ ПРАВИЛО (реверс, полностью описано в Tools/asda_mob_texmap.py и Source/.../Data/AsdaMobTextureRow.h):
#   текстура сабмеша моба берётся НЕ из matTable модели, а по MobId:
#     <map>.MON → индекс типа → <map>.MLK (пары (A,B) на сабмеш) → <map>.MBB (имя .TEX группы A).
#   Эталон: modelBuildMeshFromCache@0x11A45E0 (`qmemcpy(subset+8, a8[2] + 32*A, 0x20)`),
#           mobEffect_LoadArchiveStep@0xF81B90 состояние 0x15, CMap_LoadNLK@0x1165760.
#
# ЧТО ДЕЛАЕТ:
#   1) на КАЖДУЮ эталонную текстуру мобов создаёт/обновляет MIC /Game/AsdaAssets/Models/MobTex/MI_<tex>
#      (родитель M_AsdaObjectLit, параметр Tex = текстура) — общий пул вариантов;
#   2) для каждой модели строит соответствие СЛОТ МЕША → matIdx (по имени слота = имя материала
#      из matTable, фолбэк — порядок первого появления matIdx среди сабмешей);
#   3) заполняет DataTable /Game/AsdaAssets/Data/Mob/DT_MobTextures (RowStruct FAsdaMobTextureRow,
#      RowName = MobId-1) — материал на каждый слот для КАЖДОГО типа моба.
#
# ★★★ 01.11.2026 (mob_fx_audit) ПОСЛЕ ЭТОГО СКРИПТА — ПОВТОРИТЬ ДОВОДКУ ЭФФЕКТОВ: таблица здесь строится на M_AsdaObjectLit
#   без слотов `_blend`/`_msk`/`_asdafx`, а рабочая DT_MobTextures несёт класс-корректные MI (`__char`, `__mobfx`) и эти слоты.
#   Порядок: Reports/mob_fx_audit/scripts/ mfx_ue_dump.py → mfx_plan.py → mfx_apply_ue.py (повтор безопасен).
#   Иначе вернутся жёлтые непрозрачные «лучи» Чепухона (тип 806) и masked-дыры у CHAR-подсеток мобов.
#
# Запуск (headless):  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=".../asda_build_mob_texture_dt.py"
# Охват: ASDA_ONLY_MODELS="Mob/имя,MobSkel/имя" — ограничить набор моделей (как в asda_fix_mob_materials).
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402
import asda_ue_paths as _U
import asda_mob_texmap as _T

EAL, AR = unreal.EditorAssetLibrary, unreal.AssetRegistryHelpers.get_asset_registry()
MEL, ATOOLS = unreal.MaterialEditingLibrary, unreal.AssetToolsHelpers.get_asset_tools()

MASTER    = _U.M_OBJECT_LIT
TEX_ROOT  = _U.G_TEXTURES
MIC_DIR   = _U.G_MODELS + "/MobTex"                 # общий пул MI_<текстура>
DT_PKG, DT_NAME = _U.G_ASSETS + "/Data/Mob", "DT_MobTextures"
ROOTS     = ((_U.G_MODELS_MOB, "StaticMeshes"), (_U.G_MODELS_MOBSKEL, "SkeletalMeshes"))
ONLY = set(s.strip() for s in os.environ.get("ASDA_ONLY_MODELS", "").split(",") if s.strip())


def log(m): unreal.log_warning("[mobtex] %s" % m)


def texture_index():
    """{имя_текстуры_lower: пакет} по всем Texture2D под /Textures (портреты исключены)."""
    idx = {}
    for ad in AR.get_assets_by_path(unreal.Name(TEX_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) != "Texture2D":
            continue
        p = str(ad.package_name)
        if "/Portrait/" in p:
            continue
        idx.setdefault(str(ad.asset_name).lower(), p)
    return idx


def folder_index():
    """{lower(имя_папки_модели): (kind → фактическое имя папки)} — регистр в Content и в .jbm разный."""
    idx = {}
    for root, _sub in ROOTS:
        kind = root.rsplit("/", 1)[-1]
        for d in EAL.list_assets(root, recursive=False, include_folder=True):
            nm = d.rstrip("/").rsplit("/", 1)[-1]
            idx.setdefault(nm.lower(), {})[kind] = nm
    return idx


FOLDERS = {}


def mesh_slots(model):
    """{'Mob'|'MobSkel': [имена слотов]} — фактические слоты ассетов модели в Content."""
    out = {}
    have = FOLDERS.get(model.lower(), {})
    for root, sub in ROOTS:
        kind = root.rsplit("/", 1)[-1]
        nm = have.get(kind)
        if not nm:
            continue
        a = unreal.load_asset("%s/%s/%s/%s" % (root, nm, sub, nm))
        if not a:
            continue
        prop = "static_materials" if kind == "Mob" else "materials"
        out[kind] = [str(m.get_editor_property("material_slot_name")) for m in a.get_editor_property(prop)]
    return out


def slot_to_matidx(slots, jb):
    """СЛОТ → matIdx. Сначала по ИМЕНИ (имя слота = имя материала matTable без расширения),
    остаток — позиционно в порядке ПЕРВОГО ПОЯВЛЕНИЯ matIdx среди сабмешей."""
    stems = [os.path.splitext(t)[0] for t in jb["matTable"]]
    order = []
    for mi in jb["matIdx"]:
        if mi not in order:
            order.append(mi)
    res, taken = [None] * len(slots), set()
    for i, s in enumerate(slots):
        if s in stems:
            mi = stems.index(s)
            if mi not in taken:
                res[i], _ = mi, taken.add(mi)
    rest = [mi for mi in order if mi not in taken]
    for i in range(len(slots)):
        if res[i] is None and rest:
            res[i] = rest.pop(0)
    return res


def ensure_mic(tex_stem, tex_pkg, master):
    """MI_<текстура> в общем пуле; создаёт при отсутствии, всегда доводит родителя и Tex."""
    name = "MI_" + tex_stem
    full = "%s/%s" % (MIC_DIR, name)
    mi = unreal.load_asset(full) if EAL.does_asset_exist(full) else None
    if not mi:
        mi = ATOOLS.create_asset(name, MIC_DIR, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
    if not mi:
        return None
    MEL.set_material_instance_parent(mi, master)
    tex = unreal.load_asset(tex_pkg)
    if tex:
        cur = None
        try:
            cur = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
        except Exception:
            pass
        if not cur or cur.get_name() != tex.get_name():
            MEL.set_material_instance_texture_parameter_value(mi, "Tex", tex)
    EAL.save_loaded_asset(mi)
    return full


def main():
    master = unreal.load_asset(MASTER)
    if not master:
        log("нет мастер-материала %s" % MASTER); return
    FOLDERS.update(folder_index())
    R = _T.resolve()
    JB = _T.jbm_index()
    TEX = texture_index()
    log("эталон: типов мобов %d, моделей %d, аномалий %d, конфликтов %d"
        % (len(R["by_mob"]), len(R["by_model"]), len(R["anomalies"]), len(R["conflicts"])))

    # ── 1. пул MIC на каждую эталонную текстуру
    need = sorted({t for m in R["by_mob"].values() for t in m["submesh"] if t})
    mic, miss_tex = {}, []
    for t in need:
        stem = _T.tex_stem(t)
        pkg = TEX.get(stem.lower())
        if not pkg:
            miss_tex.append(stem); continue
        p = ensure_mic(stem, pkg, master)
        if p:
            mic[stem.lower()] = p
    log("MIC-вариантов: %d из %d текстур (нет в Content: %d)" % (len(mic), len(need), len(miss_tex)))
    for s in miss_tex:
        log("   нет текстуры в Content: %s" % s)

    # ── 2. слоты моделей
    slotmap, no_asset = {}, []
    for model in sorted(R["by_model"]):
        if ONLY and not (model in ONLY or ("Mob/" + model) in ONLY or ("MobSkel/" + model) in ONLY):
            continue
        jb = JB.get(model.lower())
        if not jb:
            continue
        ms = mesh_slots(model)
        if not ms:
            no_asset.append(model); continue
        pairs = {}                                    # имя слота → matIdx (объединение статик+скелетал)
        for kind, slots in ms.items():
            for s, mi in zip(slots, slot_to_matidx(slots, jb)):
                if mi is not None:
                    pairs.setdefault(s, mi)
        slotmap[model.lower()] = pairs
    log("моделей со слотами: %d, без ассета в Content: %d" % (len(slotmap), len(no_asset)))

    # ── 3. строки DataTable
    rows, skipped = [], 0
    for mid, rec in sorted(R["by_mob"].items()):
        pairs = slotmap.get(rec["model"].lower())
        if not pairs:
            skipped += 1; continue
        slots, mats, texs = [], [], []
        for s, mi in sorted(pairs.items(), key=lambda kv: kv[0]):
            t = rec["by_matidx"].get(mi)
            p = mic.get(_T.tex_stem(t).lower()) if t else None
            if not p:
                continue
            slots.append(s); mats.append(p + "." + p.rsplit("/", 1)[-1]); texs.append(_T.tex_stem(t))
        if not slots:
            skipped += 1; continue
        rows.append(dict(Name=str(mid - 1), Model=rec["model"], Slots=slots, Materials=mats, TexNames=texs))
    log("строк DT: %d (пропущено типов без ассета/материала: %d)" % (len(rows), skipped))

    full = "%s/%s" % (DT_PKG, DT_NAME)
    dt = unreal.load_asset(full) if EAL.does_asset_exist(full) else None
    if not dt:
        f = unreal.DataTableFactory()
        f.set_editor_property("struct", unreal.load_object(None, "/Script/AsdaReborn.AsdaMobTextureRow"))
        dt = ATOOLS.create_asset(DT_NAME, DT_PKG, unreal.DataTable, f)
    if not dt:
        log("DT не создан"); return
    ok = _DTC.fill_and_save(unreal, dt, json.dumps(rows), log, fmt="json")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_MobTextures заполнена: %s (строк %d)" % (ok, len(rows)))

    # манифест для не-UE инструментов и отчётов
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "RawAssets", "Data", "mob_textures.json")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    json.dump(dict(rows=rows, anomalies=R["anomalies"], missing_textures=miss_tex,
                   models_without_asset=no_asset),
              open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    log("манифест: %s" % os.path.normpath(out))
    unreal.log("=== MOB TEXTURE DT DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
