# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _verify_fx_seed_nodes.py — ПРОВЕРКА (ничего не меняет) достройки сид-узлов трека во ВСЕХ BP-каналах.
#
# Для каждого `BP_Fx_*` (и `BP_ClickMarker`) сверяет число узлов трека каждого эмиттера с числом
# keyframe-ключей в исходном `.jeb`:
#   ожидание при включённом `kf_seed_nodes`: узлов = ключей + 2 (СТАРТ-сид t=0 и КОНЕЦ-сид t=lifetime);
#   «как было» (гейт выключен): у кольца узлов = ключей, у квада = ключей + 1.
# Это ВТОРОЙ метод к живому тику: радиус секции показывает только геометрию, а у части колец
# анимированы лишь альфа/цвет — их оживление видно только по числу узлов.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_jeb_parse as J

PKG = "/Game/AsdaAssets/Effects"


def log(m): unreal.log_warning("[проверка-сидов] %s" % m)


EAL = unreal.EditorAssetLibrary
stat = {"BP": 0, "ring_seeded": 0, "ring_old": 0, "quad_seeded": 0, "quad_old": 0,
        "ring_was1": 0, "no_jeb": 0, "odd": 0}
old_examples, odd_examples = [], []
_cache = {}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    assets = [a for a in EAL.list_assets(PKG, False, False)]
    for a in assets:
        name = a.split(".")[-1]
        if not (name.startswith("BP_Fx_") or name == "BP_ClickMarker"): continue
        obj = EAL.load_asset(a.split(".")[0])
        if obj is None or not hasattr(obj, "generated_class"): continue
        try: cdo = unreal.get_default_object(obj.generated_class())
        except Exception: continue
        try:
            eff = cdo.get_editor_property("effect_name")
            ems = cdo.get_editor_property("emitters")
        except Exception:
            continue
        if not eff: continue
        if eff not in _cache:
            r = J.parse_effect(eff)
            _cache[eff] = ([(e["type"], len(( e.get("params") or {}).get("trackNodes") or []),
                             bool((e.get("params") or {}).get("ring")),
                             bool((e.get("params") or {}).get("growQuad")))
                            for e in r["emitters"]] if r else None)
        kf = _cache[eff]
        if kf is None:
            stat["no_jeb"] += 1; continue
        stat["BP"] += 1
        for i, e in enumerate(ems):
            if i >= len(kf): break
            _t, nkf, is_ring, is_quad = kf[i]
            if nkf == 0 or not (is_ring or is_quad): continue
            n = len(e.get_editor_property("nodes"))
            if is_ring and nkf == 1: stat["ring_was1"] += 1
            key = "ring" if is_ring else "quad"
            if n == nkf + 2: stat[key + "_seeded"] += 1
            elif n == nkf + (0 if is_ring else 1):
                stat[key + "_old"] += 1
                if len(old_examples) < 10: old_examples.append("%s/%s %s ключей=%d узлов=%d" % (eff, e.get_editor_property("name"), key, nkf, n))
            else:
                stat["odd"] += 1
                if len(odd_examples) < 10: odd_examples.append("%s/%s %s ключей=%d узлов=%d" % (eff, e.get_editor_property("name"), key, nkf, n))
    log("BP с данными: %d | кольца: с сидами=%d, ПО-СТАРОМУ=%d (из них были 1-ключевые=%d) | "
        "квады: с сидами=%d, ПО-СТАРОМУ=%d | прочие расхождения=%d | без .jeb=%d"
        % (stat["BP"], stat["ring_seeded"], stat["ring_old"], stat["ring_was1"],
           stat["quad_seeded"], stat["quad_old"], stat["odd"], stat["no_jeb"]))
    for x in old_examples: log("  ПО-СТАРОМУ: %s" % x)
    for x in odd_examples: log("  РАСХОЖДЕНИЕ: %s" % x)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
