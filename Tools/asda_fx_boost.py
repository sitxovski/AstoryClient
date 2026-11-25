# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_boost.py — ЕДИНАЯ точка правды по таблице «семейство эффектов → яркость эмиссии» (волна Ф1).
#   Импортируемый модуль БЕЗ побочных действий при импорте (закон tools-no-import-side-effects):
#   читает Tools/asda_fx_boost.json и отвечает на один вопрос — «какой множитель у этого .jeb
#   в этом режиме бленда и по какому правилу».
#   Пользователи: asda_apply_fx_boost.py (правка MIC в ассетах) и asda_fx_f1_shot.py (съёмка кадров).
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
TABLE_PATH = os.path.join(HERE, "asda_fx_boost.json")
# Режимы бленда = суффикс имени MIC, который ставит asda_place_effects.get_fx_material.
MODES = ("add", "alpha", "mod", "modadd")

_cache = {}

# ★ ОХВАТ КАРТ раската/отката буста (дедуп №2 В1 Т14, 11.10.2026): был одинаковый словарь из 21 карты в
#   asda_apply_fx_boost.py и asda_revert_fx_boost.py. Источник — реестр карт (asda_maps.ALL_MAPS, 22 карты) минус
#   карты без эффект-акторов (проверено по .umap 09.09.2026), плюс сцена логина (лежит НЕ в подпапке).
FX_MAPS_WITHOUT_ACTORS = ("battlefield", "mine")   # эффект-акторов нет — уровни не грузим
LOGIN_LEVEL = "/Game/Map/Login"                    # уровень сцены логина (ключ «Login»)


def effect_maps():
    """{ключ = имя ассета карты: путь пакета уровня} в порядке имени без учёта регистра (как был прежний словарь),
    «Login» — последним. Гейт сцены логина (снять «Login» без разрешения) остаётся у вызывающего."""
    import asda_maps as _M          # генерируемый реестр карт (без побочных эффектов при импорте)
    import asda_ue_paths as _U      # путь уровня /Game/Map/<a>/<a>
    out = {}
    for m in sorted(_M.ALL_MAPS, key=str.lower):
        if m in FX_MAPS_WITHOUT_ACTORS:
            continue
        out[_M.map_asset_name(m)] = _U.level_path(m)
    out["Login"] = LOGIN_LEVEL
    return out


def load(path=None):
    """Разобранная таблица (кэшируется по пути). Ошибки схемы — исключение, а не тихий дефолт."""
    p = path or TABLE_PATH
    if p in _cache:
        return _cache[p]
    with open(p, encoding="utf-8") as f:
        t = json.load(f)
    if t.get("_schema") != 1:
        raise RuntimeError("asda_fx_boost.json: неизвестная схема %r" % t.get("_schema"))
    if t.get("fallback", {}).get("action") != "skip":
        # ★ Единственное допустимое поведение для «не найдено»: не трогать. Любое другое значение —
        #   это молчаливый раскат на всё подряд, чего волна Ф1 делать не должна.
        raise RuntimeError("asda_fx_boost.json: fallback.action обязан быть 'skip'")
    for fam in t["families"]:
        fam["_rx"] = re.compile(fam["jeb"], re.IGNORECASE)
        for m in fam["modes"]:
            if m not in MODES:
                raise RuntimeError("семейство %s: неизвестный режим бленда %r" % (fam["id"], m))
    _cache[p] = t
    return t


def jeb_key(name):
    """Нормализованное имя .jeb: нижний регистр, без расширения и без префикса BP_Fx_.

    ★ На диске имена эффектов лежат в НИЖНЕМ регистре, а в таблицах сервера встречаются с
      заглавными («Sb_md_Eff_Oh_Sk_01_00») — сравнение всегда по нижнему.
    ★ ДЕФИС ПРИВОДИТСЯ К ПОДЧЁРКИВАНИЮ: одно и то же имя приходит в двух написаниях — из поля
      EffectName актора «eff_mdel_steamgate-a_00» и из имени ассета BP_Fx_eff_mdel_steamgate_a_00
      (генератор BP заменяет дефис, `asda_fx_common.bp_name`). Без приведения одно и то же
      семейство совпадало бы по одному источнику и промахивалось по другому."""
    s = str(name or "").strip()
    if s.lower().startswith("bp_fx_"):
        s = s[6:]
    if s.lower().endswith(".jeb"):
        s = s[:-4]
    return s.lower().replace("-", "_")


def match(jeb, mode, table=None):
    """Первое подходящее правило для (.jeb, режим бленда) или None.

    Правила проверяются в порядке файла — порядок значим (щит `modulate_never` и «дым» стоят
    выше общих семейств)."""
    t = table or load()
    key = jeb_key(jeb)
    for fam in t["families"]:
        if mode in fam["modes"] and fam["_rx"].search(key):
            return fam
    return None


def settings(jeb, mode, table=None):
    """(множитель, мягкое_врезание, дистанция, id_правила) — или None, если правило не нашлось.

    None означает «эффект вне таблицы» и обязан приводить к ПОЛНОМУ пропуску (fallback = skip)."""
    t = table or load()
    fam = match(jeb, mode, t)
    if fam is None:
        return None
    d = t["defaults"]
    return (float(fam.get("boost", d["boost"])),
            bool(fam.get("soft_fade", d["soft_fade"])),
            float(fam.get("soft_fade_distance", d["soft_fade_distance"])),
            fam["id"])


def is_noop(st):
    """Правка ничего не меняет? (буст ровно 1.0 и затухание выключено) — такие MIC не трогаем."""
    return st is not None and abs(st[0] - 1.0) < 1e-6 and not st[1]


def mode_of_mic(mic_name):
    """Режим бленда по имени MIC (`MI_<текстура>_<режим>`), или None, если суффикс чужой.

    ★ Порядок проверки важен: 'modadd' заканчивается на 'add', поэтому длинный суффикс первым."""
    n = str(mic_name or "").lower()
    # ★ 10.10.2026 (`fx_blend_fix.md`): аддитивные половины пары dst = 3 по src (M_AsdaEffectModAdd{A,Sq,Inv}) несут
    #   EmissiveBoost и для таблицы — тот же режим «modadd». `_modaddc` и `_opaque` параметра нет — None (пропуск).
    for m in ("modadda", "modaddsq", "modaddinv"):
        if n.endswith("_" + m):
            return "modadd"
    for m in ("modadd", "alpha", "mod", "add"):
        if n.endswith("_" + m):
            return m
    return None
