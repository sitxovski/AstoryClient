# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gen_npc_interact_boxes.py — ★★★ [ОРИГИНАЛ] 22.10.2026 КОРОБА ВЗАИМОДЕЙСТВИЯ СТАТИЧНЫХ NPC (доски, гробницы, камни…).
#
# ЗАЧЕМ (жалоба владельца 22.10.2026): у досок объявлений и больших объектов-NPC кольцо выделения и зона клика стоят не под моделью,
#   а в начале координат модели, и зона «дошёл» — квадрат как у обычного NPC; диалог не открывается.
# ЭТАЛОН (`Reports/npc_pick_zone_re_2026-10-22.md`): у классов сущности 1/3/5 (и 4) хитбокс выбора мышью `+189` — короб по
#   ДВУМ служебным костям скелета `<модель>_0.jbb`: `b_bone01` и `b_bone02` — берётся ПЕРВЫЙ ключ каждой кости КАК ЕСТЬ
#   (`*(bone+36)+4`, entity_char.c:59550-59561), точки поворачиваются на угол NPC и сдвигаются в его позицию, min/max по осям;
#   при высоте `npc_list +104` ≠ −1 верх поднимается на неё. Зона «дошёл» `+183` = этот короб +200 по трём осям.
#   В импортированных скелетах порта эти кости выброшены (`prune_unskinned` — они без весов), поэтому координаты берём из
#   исходного архива оригинала `Data\NPC\Motion\NpcMotion.baz`.
#
# ЧТО ДЕЛАЕТ: для каждого `<модель>_0.jbb` архива с обеими костями пишет min/max короба в координатах МОДЕЛИ оригинала (ось Y —
#   вверх, единицы игры) в `Source/AsdaReborn/Private/Npc/AsdaNpcInteractBoxes.inl` (таблица для `AAsdaNpcActor`) и сводку в
#   `RawAssets/Data/NPC/npc_interact_boxes.json`. --calib <модель,…> — дополнительно печатает мировые позиции ВСЕХ костей кадра 0
#   (для калибровки осей «модель оригинала → компонент меша UE» по костям, которые в импорте остались).
# Запуск: python Tools\asda_gen_npc_interact_boxes.py [--calib mdel_walld-a_00]
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import json
import os
import struct
import sys
import zipfile


def _paths():
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    import asda_paths
    return asda_paths


def first_key_raw(b, want):
    """Сырые позиции ПЕРВОГО ключа костей `want` из .jbb (как читает эталон, без пересчёта иерархии): {имя: (x,y,z)}."""
    U = lambda o: struct.unpack_from("<I", b, o)[0]
    if U(0) != 0x20050728:
        return {}
    nb = U(0x24)
    blob = 0x44
    o = blob + 4 * nb
    heads = []
    for i in range(nb):
        bh = o + i * 0x94
        name = b[bh:bh + 0x20].split(b"\0")[0].decode("latin1", "ignore")
        heads.append((name, U(bh + 0x20)))
    kf = o + nb * 0x94
    out = {}
    for name, kc in heads:
        if name in want and kc > 0:
            out[name] = struct.unpack_from("<3f", b, kf + 4)   # ключ: t, pos[3], quat[4], scale[3] — pos с +4
        kf += kc * 0x2C
    return out


def main():
    P = _paths()
    bnz = os.path.join(P.DATA, "NPC", "Motion", "NpcMotion.baz")   # клипы .jbb — в архиве движений
    calib = []
    if "--calib" in sys.argv:
        calib = sys.argv[sys.argv.index("--calib") + 1].split(",")
    rows = {}
    with zipfile.ZipFile(bnz) as z:
        for n in sorted(z.namelist()):
            low = n.lower()
            if not low.endswith("_0.jbb"):
                continue
            model = os.path.basename(n)[:-len("_0.jbb")]
            b = z.read(n)
            k = first_key_raw(b, ("b_bone01", "b_bone02"))
            if "b_bone01" in k and "b_bone02" in k:
                a, c = k["b_bone01"], k["b_bone02"]
                rows[model.lower()] = {"b_bone01": a, "b_bone02": c,
                                       "min": [min(a[i], c[i]) for i in range(3)], "max": [max(a[i], c[i]) for i in range(3)]}
            if model in calib:
                sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
                import asda_skinned_to_gltf as S
                j = S.parse_jbb(b)
                print("КАЛИБРОВКА %s:" % model)
                for bn in j["bones"]:
                    if bn["keyCount"]:
                        print("  %-24s %s" % (bn["name"], tuple(round(float(x), 2) for x in bn["pos"][0])))
    # ★ Полуразмеры геометрии модели `.jbm` (короб `+106` эталона: `modelObjectPtr+332`, model_anim.c:34948) — нужны классу 4, у
    #   которого зона «дошёл» = короб меша +200 по X и Z (entity_char.c:60335-60342). [ИСПРАВЛЕНИЕ ОРИГИНАЛА] у моделей из
    #   нескольких подмешей эталон сравнивает Y и Z с X (model_anim.c:34929-34939) — здесь min/max честно по своей оси.
    import importlib.util
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location("T", os.path.join(here, "asda_test_export_one.py"))
    T = importlib.util.module_from_spec(spec)
    sys.modules["T"] = T
    spec.loader.exec_module(T)
    with zipfile.ZipFile(os.path.join(P.DATA, "NPC", "Model", "NpcModel.bnz")) as zm:
        jbms = {os.path.basename(n).lower()[:-4]: n for n in zm.namelist() if n.lower().endswith(".jbm")}
        for m, r in rows.items():
            r["mesh_half"] = [0.0, 0.0, 0.0]
            if m not in jbms:
                continue
            jbm = T.parse_jbm_ext(zm.read(jbms[m]))
            lo, hi = [1e18] * 3, [-1e18] * 3
            for me in jbm["meshes"]:
                for v in me["pos0"]:
                    for i in range(3):
                        lo[i] = min(lo[i], float(v[i]))
                        hi[i] = max(hi[i], float(v[i]))
            if hi[0] >= lo[0]:
                r["mesh_half"] = [(hi[i] - lo[i]) / 2.0 for i in range(3)]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    js = os.path.join(root, "RawAssets", "Data", "NPC", "npc_interact_boxes.json")
    os.makedirs(os.path.dirname(js), exist_ok=True)
    with open(js, "w", encoding="utf-8") as f:
        json.dump({"source": bnz, "rule": "entity_char.c:59550-59615 (b_bone01/b_bone02, первый ключ)", "boxes": rows}, f, indent=1)
    inl = os.path.join(root, "Source", "AsdaReborn", "Private", "Npc", "AsdaNpcInteractBoxes.inl")
    lines = ["// АВТОГЕНЕРАЦИЯ: Tools/asda_gen_npc_interact_boxes.py — НЕ ПРАВИТЬ РУКАМИ.",
             "// Короба взаимодействия статичных NPC по костям b_bone01/b_bone02 (первый ключ) из Data\\NPC\\Motion\\NpcMotion.baz.",
             "// Координаты МОДЕЛИ оригинала (ось Y — вверх, единицы игры): { имя модели (нижний регистр), b_bone01 x, y, z, b_bone02 x, y, z,",
             "//   полуразмеры геометрии .jbm hx, hy, hz }.",
             "// Эталон: entity_char.c:59550-59615 (`Entity_StreamLoadAndInitFSM`, классы 1/3/5), разбор Reports/npc_pick_zone_re_2026-10-22.md."]
    for m, r in sorted(rows.items()):
        lines.append('\t{ TEXT("%s"), %.3ff, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff },'
                     % (m, *r["b_bone01"], *r["b_bone02"], *r["mesh_half"]))   # сырые точки: эталон поворачивает именно их
    with open(inl, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("моделей с обеими костями: %d → %s, %s" % (len(rows), js, inl))
    for m, r in sorted(rows.items()):
        print("  %-28s min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f) меш-полу=(%.0f,%.0f,%.0f)" % (m, *r["min"], *r["max"], *r["mesh_half"]))


if __name__ == "__main__":
    main()
