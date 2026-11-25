# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_foliage.py — ЕДИНЫЙ признак «модель карты = листва/карточка/крест» (сет B: setB_foliage_prefixes.json).
#   ★ Сет E-1 (06.09.2026): вынесен из asda_convert_map_lit.py (load_foliage_prefixes/is_foliage_model), потому что тот же
#   признак теперь нужен билдеру уровней (asda_build_map_level.py: bGenerateDistanceFieldAsIfTwoSided у листвы) и
#   проходу настроек DF по готовым мешам (asda_df_build_settings.py). Один источник правды — JSON сета B; map_lit
#   зовёт эти функции (обёртки с теми же именами оставлены там ради совместимости).
#   Правило: модель считается листвой, если её имя (без регистра) начинается с любого префикса из "prefixes" и НЕ
#   содержит ни одной подстроки из "exclude" (natu_ захватывал камни/утёсы/водопад — ревизия review_setB_prep.md ВАЖНО-2).
#   Имя ассета StaticMesh карт = "SM_<модель>" — префикс SM_ снимается (asset_model_name).
#   Без побочных эффектов при импорте (закон tools-no-import-side-effects); без зависимости от `unreal`.
import json
import os

FOLIAGE_JSON = os.environ.get("ASDA_FOLIAGE_JSON",
                              os.path.join(os.path.dirname(os.path.abspath(__file__)), "setB_foliage_prefixes.json"))
_CACHE = {}


def _warn(msg):
    try:
        import unreal
        unreal.log_warning(msg)
    except Exception:
        print("WARN:", msg)


def load_foliage_rules(path=None):
    """(prefixes, exclude) в нижнем регистре из JSON; нет/битый → ([], []) + предупреждение (листва не распознаётся)."""
    p = path or FOLIAGE_JSON
    if p in _CACHE:
        return _CACHE[p]
    pref, excl = [], []
    try:
        with open(p, "r", encoding="utf-8") as f:
            d = json.load(f)
        pref = [str(x).lower() for x in (d.get("prefixes") or []) if str(x).strip()]
        excl = [str(x).lower() for x in (d.get("exclude") or []) if str(x).strip()]
        if not pref:
            _warn("asda_foliage: %s без 'prefixes' — листва не распознаётся" % p)
    except Exception as e:
        _warn("asda_foliage: нет/битый %s (%s) — листва не распознаётся" % (p, e))
    _CACHE[p] = (pref, excl)
    return _CACHE[p]


def is_foliage_model(model, path=None):
    """Имя модели из .mapraw (без регистра): листва = префикс из JSON и ни одной подстроки exclude."""
    pref, excl = load_foliage_rules(path)
    m = str(model).lower()
    if not any(m.startswith(x) for x in pref):
        return False
    return not any(x in m for x in excl)


def asset_model_name(asset_name):
    """Имя модели по имени ассета StaticMesh карт: 'SM_tree_gru_012' → 'tree_gru_012' (без регистра не приводится)."""
    n = str(asset_name)
    return n[3:] if n[:3].upper() == "SM_" else n


def is_foliage_asset(asset_name, path=None):
    """Листва по имени ассета StaticMesh (SM_<модель>)."""
    return is_foliage_model(asset_model_name(asset_name), path)
