# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_multitex.py — ПРОВЕРКА пер-меш текстур у статик-моделей (после asda_reimport_multitex).
# Для каждой многотекстурной модели из RawAssets/Models/{Char,Mob} сверяет:
#   (а) число материальных слотов StaticMesh == числу РАЗНЫХ имён материалов в .glb;
#   (б) у каждого слота назначен материал с НЕПУСТЫМ параметром Tex, и текстура ЭТАЛОННАЯ:
#       • Mob  — сверка с ЭТАЛОНОМ (<map>.MON/.MLK/.MBB, Tools/asda_mob_texmap.py): у мобов имя слота
#         это АВТОРСКОЕ имя материала 3ds Max («ragepig_hammer»), а .TEX назначает таблица типа моба,
#         поэтому прежнее правило «текстура называется как слот» здесь НЕВЕРНО (исправлено 2026-08-02);
#       • Char — как раньше: имя текстуры == имени слота (допускается UE-дедуп цифр/суффикса _NN);
#   (в) слот не остался на дефолтном/сером материале.
# Охват: ASDA_ONLY_MODELS (см. asda_reimport_multitex.py).
# Запуск: run_headless.bat ...\asda_verify_multitex.py
import os, re, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_reimport_multitex as MT   # scan()/slot_names() — без побочных действий при импорте
import asda_mob_texmap as _T          # эталонный резолв текстур мобов

MEL = unreal.MaterialEditingLibrary


def base(n):
    n = os.path.splitext(str(n))[0].lower()
    n = re.sub(r"\d+$", "", n)          # UE-дедуп: a_008 → a_00 → a_
    return n


def main():
    only = set(s.strip() for s in os.environ.get("ASDA_ONLY_MODELS", "").split(",") if s.strip())
    todo = MT.scan(only)
    # ЭТАЛОН мобов: модель → {matIdx: .TEX} доминирующего типа + сопоставление слот→matIdx.
    import collections
    _R, _JB = _T.resolve(), _T.jbm_index()
    def mob_expected(name, slots):
        jb = _JB.get(name.lower()); mobs = _R["by_model_lc"].get(name.lower())
        if not jb or not mobs:
            return None
        cnt = collections.Counter(tuple(sorted(d.items())) for d in mobs.values())
        top = max(cnt.values())
        base_set = next(mobs[k] for k in sorted(mobs) if cnt[tuple(sorted(mobs[k].items()))] == top)
        stems = [os.path.splitext(t)[0] for t in jb["matTable"]]
        order = []
        for k in jb["matIdx"]:
            if k not in order: order.append(k)
        res, taken = [None] * len(slots), set()
        for i, sn in enumerate(slots):
            if sn in stems and stems.index(sn) not in taken:
                res[i] = stems.index(sn); taken.add(res[i])
        rest = [k for k in order if k not in taken]
        for i in range(len(slots)):
            if res[i] is None and rest: res[i] = rest.pop(0)
        return [_T.tex_stem(base_set.get(k, "")) if k is not None else "" for k in res]
    unreal.log("[verify_multitex] моделей к проверке: %d" % len(todo))
    bad_slots = bad_tex = no_mat = ok = skipped = 0
    for cat, name, mats in todo:
        pkg = "%s/%s/%s/StaticMeshes/%s" % (MT.DEST_BASE, cat, name, name)
        if not unreal.EditorAssetLibrary.does_asset_exist(pkg):
            skipped += 1
            continue
        m = unreal.load_asset(pkg)
        slots = m.get_editor_property("static_materials")
        want = mob_expected(name, [str(x.get_editor_property("material_slot_name")) for x in slots]) if cat == "Mob" else None
        if len(slots) < len(mats):
            bad_slots += 1
            unreal.log_warning("  ★СЛОТОВ МАЛО %s/%s: %d < %d %s" % (cat, name, len(slots), len(mats), mats))
            continue
        for _si, s in enumerate(slots):
            sn = str(s.get_editor_property("material_slot_name"))
            mi = s.get_editor_property("material_interface")
            if not mi:
                no_mat += 1
                unreal.log_warning("  ★БЕЗ МАТЕРИАЛА %s/%s слот=%s" % (cat, name, sn)); continue
            t = None
            try: t = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
            except Exception: pass
            if t is None:
                no_mat += 1
                unreal.log_warning("  ★БЕЗ ТЕКСТУРЫ %s/%s слот=%s мат=%s" % (cat, name, sn, mi.get_name())); continue
            if want is not None:                       # МОБЫ: сверка с эталоном (не с именем слота)
                exp = want[_si] if _si < len(want) else ""
                if exp and t.get_name().lower() != exp.lower():
                    bad_tex += 1
                    unreal.log_warning("  ★НЕ ЭТАЛОН %s/%s слот=%s → %s (ожидалось %s)"
                                       % (cat, name, sn, t.get_name(), exp))
            elif base(t.get_name()) != base(sn):
                bad_tex += 1
                unreal.log_warning("  ★ЧУЖАЯ ТЕКСТУРА %s/%s слот=%s → %s" % (cat, name, sn, t.get_name()))
        ok += 1
    unreal.log("[verify_multitex] ИТОГ: проверено=%d, нет статик-меша=%d | недобор слотов=%d, "
               "слотов без материала/текстуры=%d, чужая текстура=%d" % (ok, skipped, bad_slots, no_mat, bad_tex))
    unreal.log("=== MULTITEX VERIFY DONE ===")


if __name__ == "__main__":
    main()
