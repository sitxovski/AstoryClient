# Copyright (c) 2026 @sitxovski (Discord)
# _probe_bali_sizes.py — размеры эмиттеров эффекта против размеров модели оружия.
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

FX = "/Game/AsdaAssets/Effects/BP_Fx_bali_e_ra_ma_03.BP_Fx_bali_e_ra_ma_03_C"


def log(m): unreal.log_warning("[sizes] %s" % m)


def main():
    cls = unreal.load_object(None, FX)
    if not cls:
        log("BP не найден"); return
    cdo = unreal.get_default_object(cls)
    ems = cdo.get_editor_property("emitters")
    log("эмиттеров: %d" % len(ems))
    for i in range(len(ems)):
        e = ems[i]
        if i == 0:
            log("ПОЛЯ ЭМИТТЕРА: %s" % ", ".join([p for p in dir(e) if not p.startswith("_")])[:900])
        def gp(o, n, d=None):
            try: return o.get_editor_property(n)
            except Exception: return d
        nm = gp(e, "name", "?"); typ = gp(e, "emitter_type", gp(e, "fx_type", "?"))
        single = gp(e, "b_single_quad", gp(e, "single_quad", "?"))
        bil = gp(e, "billboard_mode", gp(e, "billboard", "?"))
        ring = gp(e, "b_ring", gp(e, "ring", "?"))
        nodes = gp(e, "nodes", [])
        n0 = nodes[0] if nodes else None
        if i == 0 and n0:
            props = [pr for pr in dir(n0) if not pr.startswith("_")]
            log("ПОЛЯ УЗЛА: %s" % ", ".join(props[:80]))
        vals = {}
        if n0:
            for pn in ("size_x", "size_y", "half_w", "half_h", "size", "scale",
                       "ring_base_pos", "ring_pos_ofs", "pos_base", "cone_axis"):
                try: vals[pn] = str(n0.get_editor_property(pn))
                except Exception: pass
        log("%-8s тип=%s single=%s bil=%s ring=%s узлов=%d | %s"
            % (str(nm), str(typ), single, bil, ring, len(nodes),
               "; ".join("%s=%s" % kv for kv in vals.items())))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
