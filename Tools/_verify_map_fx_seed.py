# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _verify_map_fx_seed.py — ПРОВЕРКА (ничего не меняет) подэтапа «карты» этапа 3 раскатки сид-узлов.
#
# Загружает уровень каждой карты, обходит акторы `MapEffect*` и считает по их эмиттерам:
#   кольца/квады с keyframe-ключами и число узлов трека у каждого.
# ОЖИДАНИЕ: у кольца с N ключами в файле должно стать N+2 узла (СТАРТ-сид + ключи + КОНЕЦ-сид),
#   у квада — N+2 (СТАРТ ветка клала и раньше, КОНЕЦ вернулся). «Было» считается из самого `.jeb`
#   (старый сборщик: кольцо = ровно ключи файла, квад = 1 сид + ключи) — это второй, независимый
#   от уровня источник.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal

# ★★★ ПОПРАВКА 2026-08-14 (`Reports/skill_fx_scale.md` §3): КОНЕЦ-узел трека ОПРОВЕРГНУТ —
#   ветку `animSampleCurveAtTime@0x11FCEC0` при `a2[27]==0` не зовёт никто, а
#   `effectUpdateKeyframeTrack@0x11F7B10` без узла `курсор+1` делает `return 0` (канал ЗАМИРАЕТ).
#   ⇒ ожидание = ключи + ОДИН служебный СТАРТ-узел. `ASDA_FX_KF_END_SEED=1` возвращает прежнее «+2».
EXP_EXTRA = 2 if os.environ.get("ASDA_FX_KF_END_SEED", "") == "1" else 1
import asda_jeb_parse as J
import asda_maps as _MAPS

EAL = unreal.EditorAssetLibrary
_asset = _MAPS.map_asset_name
DEFAULT = ("alpen;flammio;palace_of_queen;t_palace_of_queen;castle_of_chess;t_castle_of_chess;"
           "castle_of_chaos;inferion;firewayforest;StagnantDesert;flabis;quizstage;rein;silalis;"
           "alpen_dungeon;dragonlair;Mountain Flamion;nightvalley")
MAPS = [m.strip() for m in os.environ.get("ASDA_PATCH_MAPS", DEFAULT).split(";") if m.strip()]


def log(m): unreal.log_warning("[проверка-карт] %s" % m)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
_kf_cache = {}


def kf_of(effname):
    """Число ключей файла по эмиттерам (в порядке эмиттеров): [(тип, ключей)]."""
    if effname not in _kf_cache:
        r = J.parse_effect(effname)
        out = []
        if r:
            for e in r["emitters"]:
                p = e.get("params") or {}
                out.append((e["type"], len(p.get("trackNodes") or []),
                            bool(p.get("ring")), bool(p.get("growQuad"))))
        _kf_cache[effname] = out
    return _kf_cache[effname]


tot = {"ring_ok": 0, "ring_bad": 0, "quad_ok": 0, "quad_bad": 0, "ring_was1": 0, "actors": 0}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global e, m
    for mp in MAPS:
        lvl = "/Game/Map/%s/%s" % (_asset(mp), _asset(mp))
        if not EAL.does_asset_exist(lvl):
            log("нет уровня: %s" % mp); continue
        les.load_level(lvl)
        m = {"ring_ok": 0, "ring_bad": 0, "quad_ok": 0, "quad_bad": 0, "ring_was1": 0, "actors": 0}
        bad = []
        for a in eas.get_all_level_actors():
            if not a.get_actor_label().startswith("MapEffect"): continue
            try: ems = a.get_editor_property("emitters")
            except Exception: continue
            eff = a.get_editor_property("effect_name")
            kf = kf_of(eff)
            m["actors"] += 1
            for i, e in enumerate(ems):
                if i >= len(kf): break
                _t, nkf, is_ring, is_quad = kf[i]
                if nkf == 0: continue
                n = len(e.get_editor_property("nodes"))
                if is_ring:
                    if nkf == 1: m["ring_was1"] += 1
                    if n == nkf + EXP_EXTRA: m["ring_ok"] += 1
                    else:
                        m["ring_bad"] += 1
                        bad.append("%s/%s кольцо ключей=%d узлов=%d" % (eff, e.get_editor_property("name"), nkf, n))
                elif is_quad:
                    if n == nkf + EXP_EXTRA: m["quad_ok"] += 1
                    else:
                        m["quad_bad"] += 1
                        bad.append("%s/%s квад ключей=%d узлов=%d" % (eff, e.get_editor_property("name"), nkf, n))
        log("%-20s акторов=%4d | кольца с ключами: сид ОК=%3d, не ОК=%3d (из них было 1-ключевых=%3d) "
            "| квады с ключами: сид ОК=%4d, не ОК=%3d"
            % (mp, m["actors"], m["ring_ok"], m["ring_bad"], m["ring_was1"], m["quad_ok"], m["quad_bad"]))
        for b in bad[:5]: log("     РАСХОЖДЕНИЕ: %s" % b)
        for k in tot: tot[k] += m[k]
    log("ИТОГО: акторов=%d | кольца: ОК=%d, расхождений=%d (было статичных 1-ключевых=%d) | квады: ОК=%d, расхождений=%d"
        % (tot["actors"], tot["ring_ok"], tot["ring_bad"], tot["ring_was1"], tot["quad_ok"], tot["quad_bad"]))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
