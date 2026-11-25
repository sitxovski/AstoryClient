# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""ЗАМЕР (ничего не меняет): цепочка множителей размера у эффектов навыка 707.

Сверяем ТРИ величины по каждому эмиттеру собранного BP:
  1) что лежит в `.jeb` (полу-экстент квада / радиус кольца, каналы СТАРТ→КОНЕЦ);
  2) что лежит в узлах собранного BP (`AAsdaEffectActor::Emitters[i].Nodes[]`);
  3) во что это превращается на экране при масштабе актора s (эффект удара: s = %размера цели/100).
Печатаем ПОЛНУЮ ширину квада (2·halfW·s) в юнитах и в ростах персонажа (170 юн).
"""
import os
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal
import asda_jeb_parse as J

SRC = r"@@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked\models_orig\Data\Effect\Model"
CHAR_H = 170.0                     # рост персонажа в мировых юнитах (плашка 150 + голова)
CASES = [
    ("eff_1_char_07_runfire01-a_00", 1.0, "КАСТ (выстрел) @follow:caster — эталон scaleAll НЕ зовёт"),
    ("eff_1_char_07_runfire02-a_00", 1.0, "ШЛЕЙФ СНАРЯДА @projectile — эталон scaleAll НЕ зовёт"),
    ("eff_1_char_07_runfirb03-a_00", 1.5, "УДАР фаза 0 @world:target — scaleAll(%цели/100), волк=150%"),
    ("eff_1_char_07_runfirb04-a_00", 1.5, "УДАР фаза 1 @world:target"),
    ("eff_1_char_07_runfirb05-a_00", 1.5, "УДАР фаза 2 @world:target"),
]


def log(m):
    unreal.log_warning("[707-scale] %s" % m)


def jeb_half(pr):
    """Максимальный полу-экстент эмиттера по данным `.jeb` (СТАРТ и КОНЕЦ канала)."""
    best = 0.0
    for k in ("scaleX", "scaleY", "sizeX", "sizeY", "topR", "botR", "height"):
        v = pr.get(k)
        if isinstance(v, (list, tuple)):
            for x in v[:2]:
                try:
                    best = max(best, abs(float(x or 0.0)))
                except Exception:
                    pass
    return best


def main():
    for eff, s, why in CASES:
        path = os.path.join(SRC, eff + ".jeb")
        jb = J.parse_jeb(open(path, "rb").read()) if os.path.isfile(path) else None
        by_name = {}
        if jb:
            for e in jb.get("emitters", []):
                by_name[str(e.get("name", "?"))] = e
        bp = unreal.EditorAssetLibrary.load_asset("/Game/AsdaAssets/Effects/BP_Fx_%s"
                                                  % eff.replace("-", "_"))
        if not bp:
            log("НЕТ BP для %s" % eff)
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        ems = cdo.get_editor_property("emitters")
        log("=" * 118)
        log("%s   s=%.2f   %s" % (eff, s, why))
        log("  %-16s %-5s %-4s %8s %8s %8s %9s %10s" %
            ("эмиттер", "тип", "bb", "jeb_half", "bp_half", "на экране", "×роста", "узлов"))
        worst = 0.0
        for i in range(len(ems)):
            em = ems[i]
            nm = str(em.get_editor_property("name"))
            t = int(em.get_editor_property("jeb_type"))
            try:
                bb = int(em.get_editor_property("billboard_mode"))
            except Exception:
                bb = -1
            nodes = em.get_editor_property("nodes")
            bph = 0.0
            for n in nodes:
                for p in ("size_x", "size_y", "ring_top_r", "ring_bot_r", "ring_h"):
                    try:
                        bph = max(bph, abs(float(n.get_editor_property(p))))
                    except Exception:
                        pass
            jh = jeb_half((by_name.get(nm, {}) or {}).get("params", {}) or {})
            scr = 2.0 * bph * s
            worst = max(worst, scr)
            log("  %-16s 0x%-3X %-4d %8.1f %8.1f %8.1f %9.2f %10d"
                % (nm, t, bb, jh, bph, scr, scr / CHAR_H, len(nodes)))
        log("  ИТОГ %s: самая крупная геометрия на экране = %.0f юн = %.2f роста персонажа"
            % (eff, worst, worst / CHAR_H))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
