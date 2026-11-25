# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_mob_textures.py — АУДИТ текстур мобов против эталона (.MON/.MLK/.MBB, см. asda_mob_texmap.py).
#   1) покрытие: у каждого слота каждой модели мобов есть материал и НЕПУСТАЯ текстура;
#   2) DT_MobTextures: строка есть у каждого эталонного типа, материалы грузятся, Tex == эталонной .TEX;
#   3) меш-ассет: текстура слота == набору ДОМИНИРУЮЩЕГО типа (то, что кладёт asda_fix_mob_materials);
#   4) DT_MobNames.Model (по нему рантайм выбирает МЕШ) сверяется с Mob list.BIN +0x04 — гард
#      ApplyMobTextures молча пропустит оверрайд, если меш чужой.
# Запуск (headless). Итог → Reports/mob_texture_audit.txt
import os, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_ue_paths as _U
import asda_mob_texmap as _T

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
OUT = os.path.join(_P.RE_REPORTS, "mob_texture_audit.txt")
ROOTS = ((_U.G_MODELS_MOB, "StaticMeshes", "static_materials"),
         (_U.G_MODELS_MOBSKEL, "SkeletalMeshes", "materials"))
DT_TEX   = _U.G_ASSETS + "/Data/Mob/DT_MobTextures"
DT_NAMES = _U.G_ASSETS + "/Data/Mob/DT_MobNames"

lines = []
def out(m):
    lines.append(m); unreal.log_warning("[verify_mobtex] %s" % m)


def tex_of(mi):
    if not mi or not isinstance(mi, unreal.MaterialInstanceConstant):
        return None
    try:
        return MEL.get_material_instance_texture_parameter_value(mi, "Tex")
    except Exception:
        return None


def dominant_set(mobs):
    cnt = collections.Counter(tuple(sorted(d.items())) for d in mobs.values())
    top = max(cnt.values())
    for mid in sorted(mobs):
        if cnt[tuple(sorted(mobs[mid].items()))] == top:
            return mobs[mid], mid
    return None, None


def slot_to_matidx(slots, jb):
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
                res[i] = mi
                taken.add(mi)
    rest = [mi for mi in order if mi not in taken]
    for i in range(len(slots)):
        if res[i] is None and rest:
            res[i] = rest.pop(0)
    return res


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global d, i, jb, m, mi, mid, mobs, slots
    global t
    R  = _T.resolve()
    JB = _T.jbm_index()
    ML = _T.mob_list()
    out("эталон: типов %d, моделей %d, аномалий %d, конфликтов %d"
        % (len(R["by_mob"]), len(R["by_model"]), len(R["anomalies"]), len(R["conflicts"])))
    # ── 1/3. покрытие слотов и совпадение с доминирующим набором
    noslot, nomat, notex, mismatch, okslot = 0, [], [], [], 0
    for root, sub, prop in ROOTS:
        kind = root.rsplit("/", 1)[-1]
        for d in EAL.list_assets(root, recursive=False, include_folder=True):
            model = d.rstrip("/").rsplit("/", 1)[-1]
            mesh = unreal.load_asset("%s/%s/%s/%s" % (root, model, sub, model))
            if not mesh:
                continue
            mats = mesh.get_editor_property(prop)
            if not mats:
                noslot += 1
                continue
            slots = [str(m.get_editor_property("material_slot_name")) for m in mats]
            jb = JB.get(model.lower())
            mobs = R["by_model_lc"].get(model.lower())
            mapping = slot_to_matidx(slots, jb) if jb else [None] * len(slots)
            base, base_id = dominant_set(mobs) if mobs else (None, None)
            for i, m in enumerate(mats):
                mi = m.get_editor_property("material_interface")
                if not mi:
                    nomat.append("%s/%s слот %s" % (kind, model, slots[i])); continue
                t = tex_of(mi)
                if not t or t.get_name() == "DefaultTexture":
                    notex.append("%s/%s слот %s" % (kind, model, slots[i])); continue
                okslot += 1
                if base and mapping[i] is not None:
                    want = _T.tex_stem(base.get(mapping[i], ""))
                    if want and want.lower() != t.get_name().lower():
                        mismatch.append("%s/%s слот %s: %s ≠ эталон %s (тип %s)"
                                        % (kind, model, slots[i], t.get_name(), want, base_id))
    out("СЛОТЫ мешей: с текстурой %d | без материала %d | без текстуры %d | мешей без слотов %d"
        % (okslot, len(nomat), len(notex), noslot))
    out("СЛОТЫ ≠ доминирующему эталонному набору: %d" % len(mismatch))
    for x in nomat + notex + mismatch:
        out("   " + x)
    # ── 2. DT_MobTextures
    dt = unreal.load_asset(DT_TEX)
    if not dt:
        out("!! DT_MobTextures не найдена")
    else:
        names = set(str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
        miss_row = [mid for mid in R["by_mob"] if str(mid - 1) not in names]
        out("DT_MobTextures: строк %d | эталонных типов без строки: %d" % (len(names), len(miss_row)))
        if miss_row:
            out("   без строки: " + ", ".join(str(x) for x in sorted(miss_row)[:30]))
        # содержимое: материалы грузятся и несут ЭТАЛОННУЮ текстуру (читаем DT через JSON-экспорт)
        import json as _json
        data = _json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))
        byrow = {str(r.get("Name")): r for r in data}
        bad, checked = [], 0
        for mid, rec in sorted(R["by_mob"].items()):
            r = byrow.get(str(mid - 1))
            if not r:
                continue
            if (r.get("Model") or "").lower() != rec["model"].lower():
                bad.append("тип %d: Model=%s ≠ %s" % (mid, r.get("Model"), rec["model"]))
            for s_, mref, tname in zip(r.get("Slots", []), r.get("Materials", []), r.get("TexNames", [])):
                checked += 1
                mi = unreal.load_asset(str(mref).split(".")[0])
                t = tex_of(mi)
                if not t or t.get_name().lower() != str(tname).lower():
                    bad.append("тип %d слот %s: MIC=%s Tex=%s ≠ %s"
                               % (mid, s_, mi.get_name() if mi else "НЕТ", t.get_name() if t else "НЕТ", tname))
        out("DT_MobTextures: проверено пар (слот,материал) %d | несоответствий %d" % (checked, len(bad)))
        for x in bad[:30]:
            out("   " + x)
    # ── 4. DT_MobNames.Model vs Mob list.BIN +0x04 (по нему рантайм грузит МЕШ)
    dtn = unreal.load_asset(DT_NAMES)
    if not dtn:
        out("!! DT_MobNames не найдена")
    else:
        import json as _json2
        dn = {str(r.get("Name")): r for r in _json2.loads(
            unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dtn))}
        wrong = []
        for mid, rec in sorted(ML.items()):
            r = dn.get(str(mid - 1))
            if not r:
                continue
            m = (r.get("Model") or "")
            if m and m.lower() != rec["model"].lower():
                wrong.append("тип %d: DT_MobNames.Model=%s, Mob list +0x04=%s (+0x24=%s)"
                             % (mid, m, rec["model"], rec["motion"]))
        out("DT_MobNames.Model ≠ Mob list.BIN +0x04 (эталон CMap_LoadMOB@0xF71A50): %d типов" % len(wrong))
        for x in wrong[:40]:
            out("   " + x)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    unreal.log("[verify_mobtex] отчёт → %s" % OUT)
    unreal.log("=== MOB TEXTURE AUDIT DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
