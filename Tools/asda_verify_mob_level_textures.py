# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_mob_level_textures.py — АУДИТ ЗАПЕЧЁННЫХ моб-акторов на всех картах:
#   а) меш актора == модели типа из Mob list.BIN **+0x04** (эталон CMap_LoadMOB@0xF71A50);
#   б) фактическая текстура каждого слота компонента == эталонной для ЭТОГО MobId
#      (<map>.MON/.MLK/.MBB, см. Tools/asda_mob_texmap.py).
# Запуск (headless). Итог → Reports/mob_level_texture_audit.txt
import os, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_mob_texmap as _T

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
OUT = os.path.join(_P.RE_REPORTS, "mob_level_texture_audit.txt")

lines = []
def out(m):
    lines.append(m); unreal.log_warning("[verify_lvl] %s" % m)


def tex_name(mi):
    if isinstance(mi, unreal.MaterialInstanceConstant):
        try:
            t = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
            return t.get_name() if t else None
        except Exception:
            return None
    return None


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    R  = _T.resolve()
    JB = _T.jbm_index()
    ML = _T.mob_list()
    tot = wrong_model = wrong_tex = no_etalon = 0
    badm, badt = collections.Counter(), collections.Counter()
    for mp in _MAPS.MAPS_WITH_MOB_SVR:
        asset = _MAPS.map_asset_name(mp)
        lvl = _U.level_path(mp)
        if not EAL.does_asset_exist(lvl):
            continue
        les.load_level(lvl)
        for a in eas.get_all_level_actors():
            if not isinstance(a, unreal.AsdaMobActor):
                continue
            tot += 1
            mid = a.get_editor_property("mob_id")
            rec = ML.get(mid)
            comp = a.mesh
            mesh = comp.get_editor_property("skeletal_mesh") if hasattr(comp, "get_editor_property") else None
            try:
                mesh = comp.get_skinned_asset()
            except Exception:
                pass
            if not mesh:
                comp = a.static_mesh_comp
                mesh = comp.get_editor_property("static_mesh")
            if not mesh:
                continue
            mname = mesh.get_name()
            if rec and rec["model"].lower() != mname.lower():
                wrong_model += 1; badm[(mid, mname, rec["model"])] += 1
            et = R["by_mob"].get(mid)
            if not et:
                no_etalon += 1; continue
            jb = JB.get(rec["model"].lower()) if rec else None
            if not jb:
                continue
            stems = [os.path.splitext(t)[0] for t in jb["matTable"]]
            order = []
            for k in jb["matIdx"]:
                if k not in order:
                    order.append(k)
            slots = [str(s) for s in comp.get_material_slot_names()]
            taken, mapping = set(), [None] * len(slots)
            for i, s in enumerate(slots):
                if s in stems and stems.index(s) not in taken:
                    mapping[i] = stems.index(s); taken.add(mapping[i])
            rest = [k for k in order if k not in taken]
            for i in range(len(slots)):
                if mapping[i] is None and rest:
                    mapping[i] = rest.pop(0)
            for i in range(len(slots)):
                want = _T.tex_stem(et["by_matidx"].get(mapping[i], "")) if mapping[i] is not None else ""
                got = tex_name(comp.get_material(i))
                if want and (not got or got.lower() != want.lower()):
                    wrong_tex += 1; badt[(mid, rec["model"], slots[i], got, want)] += 1
    out("запечённых моб-акторов: %d | НЕВЕРНАЯ модель: %d | НЕВЕРНАЯ текстура слота: %d | без эталона: %d"
        % (tot, wrong_model, wrong_tex, no_etalon))
    for k, v in sorted(badm.items())[:40]:
        out("   модель: тип %d меш=%s ≠ %s ×%d" % (k[0], k[1], k[2], v))
    for k, v in sorted(badt.items())[:60]:
        out("   текстура: тип %d %s слот %s: %s ≠ %s ×%d" % (k[0], k[1], k[2], k[3], k[4], v))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    unreal.log("=== MOB LEVEL TEXTURE AUDIT DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
