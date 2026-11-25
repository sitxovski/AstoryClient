# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _verify_ring_offset_fix.py — ПРОВЕРКА ДАННЫХ после фикса смещения колец 0xE (2026-09-10).
#
#   Читает УРОВЕНЬ (акторы MapEffect_*) и сверяет у КАЖДОГО кольцевого эмиттера (0xE):
#     ring_base_pos == (posX, posY, −posZ)   ← pos-канал с негацией z, как ждёт карт-ветка рантайма
#     ring_pos_ofs  == basePos               ← сырой мир
#   Эталон берётся из export/maps/<карта>/effects.json (данные ОРИГИНАЛА, не из порта) —
#   проверка НЕ циклическая (закон: оракул на своей же математике ничего не доказывает).
#   Уровень НЕ СОХРАНЯЕТСЯ — скрипт только читает.
#
#   Запуск: ASDA_MAP=<карта> run_headless_safe.py Tools/_verify_ring_offset_fix.py --marker "ПРОВЕРКА ЗАВЕРШЕНА"
import os, json, unreal

MAP  = os.environ.get("ASDA_MAP", "alpen")
RAW  = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS
LEVEL = "/Game/Map/%s/%s" % (_MAPS.map_asset_name(MAP), _MAPS.map_asset_name(MAP))


def main():
    data = json.load(open(os.path.join(RAW, MAP, "effects.json"), encoding="utf-8"))
    jeb = data.get("jeb", {})
    les = unreal.LevelEditorSubsystem()
    les.load_level(LEVEL)
    eas = unreal.EditorActorSubsystem()
    # ожидаемые значения по имени .jeb и имени эмиттера
    exp = {}
    for nm, jb in jeb.items():
        for e in jb["emitters"]:
            if e["type"] != 0xE or e.get("mesh"):
                continue
            p = e["params"]
            tn = [n for n in (p.get("trackNodes") or []) if len(n) >= 45]
            if tn:      # у трека значения берутся из узлов — сверяем ПОСЛЕДНИЙ узел файла
                n46 = tn[-1]
                pos = [n46[7], n46[9], n46[11]]; base = [n46[1], n46[3], n46[5]]
            else:
                pos = list(p.get("posStart", [0, 0, 0])); base = list(p.get("basePosStart", [0, 0, 0]))
            exp[(nm, e["name"])] = ([pos[0], pos[1], -pos[2]], base)

    ok = bad = miss = 0
    for a in eas.get_all_level_actors():
        if not a.get_actor_label().startswith("MapEffect"):
            continue
        if not a.get_class().get_name().startswith("AsdaEffectActor"):
            continue
        nm = a.get_editor_property("effect_name")
        for em in a.get_editor_property("emitters"):
            if int(em.get_editor_property("jeb_type")) != 0xE:
                continue
            key = (nm, str(em.get_editor_property("name")))
            if key not in exp:
                miss += 1; continue
            want_bp, want_po = exp[key]
            nodes = em.get_editor_property("nodes")
            if not nodes:
                miss += 1; continue
            nd = nodes[-1]
            bp = nd.get_editor_property("ring_base_pos"); po = nd.get_editor_property("ring_pos_ofs")
            d1 = max(abs(bp.x - want_bp[0]), abs(bp.y - want_bp[1]), abs(bp.z - want_bp[2]))
            d2 = max(abs(po.x - want_po[0]), abs(po.y - want_po[1]), abs(po.z - want_po[2]))
            if d1 < 1e-3 and d2 < 1e-3:
                ok += 1
            else:
                bad += 1
                unreal.log_warning("[кольца] РАСХОЖДЕНИЕ %s / %s: ring_base_pos=%s ждали %s | ring_pos_ofs=%s ждали %s"
                                   % (nm, key[1], bp, want_bp, po, want_po))
    unreal.log_warning("[кольца] %s: сверено колец=%d, СОВПАЛО=%d, расхождений=%d, без эталона=%d"
                       % (MAP, ok + bad, ok, bad, miss))
    unreal.log_warning("[кольца] ПРОВЕРКА ЗАВЕРШЕНА")


if __name__ == "__main__":
    main()
