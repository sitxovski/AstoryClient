# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _fix_backdrop_mesh_slots.py — ИСПРАВЛЕНИЕ теней задника логина (решение владельца 14.09.2026).
#   Причина (замер 13.09, Reports/backdrop_shadows): у мешей SM_m* 0 слотов StaticMaterials →
#   UStaticMeshComponent::GetNumMaterials()=0 → пустая MaterialRelevance прокси → примитив не попадает в тени
#   (ShadowSetup.cpp: if (bOpaque && bShadowRelevance)).
#   Что делает: добавляет каждому SM_m* БЕЗ слотов один слот «Mat0» (имя = слоту группы полигонов сборщика),
#   материал слота = override-материал первого элемента, использующего меш (внешний вид не меняется: у всех
#   компонентов override остаётся). Геометрию НЕ пересобирает (без PostEditChange), сохраняет ТОЛЬКО пакеты мешей
#   явным списком. ★★★ Login.umap грузится только для чтения соответствия меш→материал и НЕ сохраняется.
#   Режимы: ASDA_FIX_MODE=dump (только замер) | apply (правка+сохранение). Выход — ASDA_FIX_OUT.
import os, json, hashlib

MODE = os.environ.get("ASDA_FIX_MODE", "dump")
OUT = os.environ.get("ASDA_FIX_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/backdrop_shadows/fix/mesh_state_%s.json" % MODE)
DEST = "/Game/AsdaAssets/Models/Backdrop"


def main():
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): скрипт открывает /Game/Map/Login — без ASDA_ALLOW_LOGIN_WRITE=1 стоп
    import sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from asda_login_guard import forbid_login_write
    forbid_login_write("_fix_backdrop_mesh_slots.py")
    import unreal
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    smes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    EAL = unreal.EditorAssetLibrary

    def log(m):
        unreal.log_warning("[fix-bdslots] %s" % m)

    def geo(sm):
        """Отпечаток геометрии: исходное описание (позиции, UV, треугольники) + рендер-данные + bounds."""
        d = {}
        try:
            md = sm.get_static_mesh_description(0)
            h = hashlib.md5()
            nv = md.get_vertex_count(); ni = md.get_vertex_instance_count(); nt = md.get_triangle_count()
            for v in range(nv):
                p = md.get_vertex_position(unreal.VertexID(v))
                h.update(("%.5f,%.5f,%.5f;" % (p.x, p.y, p.z)).encode())
            for i in range(ni):
                uv = md.get_vertex_instance_uv(unreal.VertexInstanceID(i), 0)
                h.update(("%.6f,%.6f;" % (uv.x, uv.y)).encode())
            for t in range(nt):
                vis = md.get_triangle_vertex_instances(unreal.TriangleID(t))
                h.update((",".join(str(x.id_value) for x in vis) + ";").encode())
            d.update({"desc_v": nv, "desc_vi": ni, "desc_tri": nt, "desc_md5": h.hexdigest(),
                      "desc_pg": md.get_polygon_group_count()})
        except Exception as ex:
            d["desc_err"] = str(ex)
        try:
            d["rd_verts"] = smes.get_number_verts(sm, 0)
            d["lods"] = smes.get_lod_count(sm)
            d["num_sections"] = sm.get_num_sections(0)
            d["uv_ch"] = smes.get_num_uv_channels(sm, 0)
            d["sec0_cast_shadow"] = smes.is_section_cast_shadow_enabled(sm, 0, 0)
        except Exception as ex:
            d["rd_err"] = str(ex)
        b = sm.get_bounds()
        d["bounds"] = [round(b.origin.x, 4), round(b.origin.y, 4), round(b.origin.z, 4),
                       round(b.box_extent.x, 4), round(b.box_extent.y, 4), round(b.box_extent.z, 4), round(b.sphere_radius, 4)]
        sl = list(sm.get_editor_property("static_materials"))
        d["slots"] = [[str(s.material_slot_name), s.material_interface.get_path_name() if s.material_interface else None] for s in sl]
        try:
            d["section0_material_index"] = sm.get_section_info_map().get(0, 0).material_index if hasattr(sm, "get_section_info_map") else None
        except Exception:
            pass
        return d

    assert les.load_level("/Game/Map/Login")     # только чтение: меш → override-материал элемента
    mesh2mat = {}; mesh2elems = {}
    for a in eas.get_all_level_actors():
        if not a.actor_has_tag("AsdaBackdropElem"):
            continue
        c = a.static_mesh_component
        sm = c.static_mesh
        if not sm:
            continue
        p = sm.get_path_name()
        mesh2elems[p] = mesh2elems.get(p, 0) + 1
        ov = list(c.get_editor_property("override_materials"))
        if p not in mesh2mat and ov and ov[0]:
            mesh2mat[p] = ov[0]
    R = {"mode": MODE, "meshes": {}, "unreferenced": [], "changed": [], "saved": [], "save_failed": []}
    paths = sorted(p for p in EAL.list_assets(DEST, recursive=False, include_folder=False)
                   if p.split("/")[-1].split(".")[0].startswith("SM_m"))
    log("SM_m* ассетов: %d, упомянуто элементами: %d" % (len(paths), len(mesh2elems)))
    for ap in paths:
        sm = EAL.load_asset(ap)
        if not sm or not isinstance(sm, unreal.StaticMesh):
            continue
        p = sm.get_path_name()
        rec = {"elems": mesh2elems.get(p, 0), "before": geo(sm)}
        if p not in mesh2elems:
            R["unreferenced"].append(p)
        if MODE == "apply" and not rec["before"]["slots"]:
            mat = mesh2mat.get(p)
            st = unreal.StaticMaterial(material_interface=mat, material_slot_name="Mat0")
            try:
                st.set_editor_property("imported_material_slot_name", "Mat0")   # editor-only, может быть недоступно
            except Exception:
                pass
            sm.set_editor_property("static_materials", [st], unreal.PropertyAccessChangeNotifyMode.NEVER)
            R["changed"].append(p)
            ok = EAL.save_loaded_asset(sm, only_if_is_dirty=False)   # ★ только этот пакет меша
            (R["saved"] if ok else R["save_failed"]).append(p)
            rec["after"] = geo(sm)
        R["meshes"][p] = rec
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=1)
    log("изменено %d, сохранено %d, ошибок сохранения %d, без ссылок %d" %
        (len(R["changed"]), len(R["saved"]), len(R["save_failed"]), len(R["unreferenced"])))
    log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
