# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _patch_map_fx_seed.py — ЭТАП 3 раскатки сид-узлов трека, ПОДЭТАП «КАРТЫ».
#
# Гоняет `asda_place_effects.py` по перечисленным картам в ПАТЧ-РЕЖИМЕ
# (`ASDA_MAP_PATCH_ONLY=1`): существующие акторы `MapEffect*` НЕ пересоздаются, у них переписывается
# ТОЛЬКО массив `emitters`. Так ручные правки пользователя (башня alpen ставилась руками —
# память `effects-conversion-model`) остаются на месте: трансформы, привязки к костям, хиральная
# нормаль, `particle_alpha_scale` и метки не трогаются.
#
# ⚠ НЕ используем `asda_place_effects_all.py`: он удаляет `EffectTex`/`EffectMat` каждой карты
#   (пере-извлечение текстур и пересоздание MIC) — для точечного патча это лишнее разрушение.
#
# Карты задаются `ASDA_PATCH_MAPS="карта1;карта2"`, по умолчанию — 18 карт, у которых в `.jeb`
# есть keyframe-ключи у колец/квадов (замер `Reports/item_fx_ring_motion.md`, этап 3).
import os
import sys

import unreal

DEFAULT_MAPS = ("alpen;flammio;palace_of_queen;t_palace_of_queen;castle_of_chess;t_castle_of_chess;"
                "castle_of_chaos;inferion;firewayforest;StagnantDesert;flabis;quizstage;rein;silalis;"
                "alpen_dungeon;dragonlair;Mountain Flamion;nightvalley")


import asda_maps as _MAPS
EAL = unreal.EditorAssetLibrary
_asset = _MAPS.map_asset_name


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    TOOLS = os.path.dirname(os.path.abspath(__file__))
    MAPS = [m.strip() for m in os.environ.get("ASDA_PATCH_MAPS", DEFAULT_MAPS).split(";") if m.strip()]
    os.environ["ASDA_MAP_PATCH_ONLY"] = "1"
    os.environ.setdefault("ASDA_MAP_KF_SEED", "1")
    for mp in MAPS:
        lvl = "/Game/Map/%s/%s" % (_asset(mp), _asset(mp))
        if not EAL.does_asset_exist(lvl):
            unreal.log_warning("[патч-карт] пропуск (нет уровня): %s" % mp)
            continue
        unreal.log_warning("=== [патч-карт] %s ===" % mp)
        os.environ["ASDA_MAP"] = mp
        try:
            pf = os.path.join(TOOLS, "asda_place_effects.py")
            exec(compile(open(pf, encoding="utf-8").read(), pf, "exec"),
                 {"__name__": "__main__", "__file__": pf})
        except Exception as e:
            unreal.log_error("[патч-карт] %s ОШИБКА: %s" % (mp, e))
        try:
            unreal.SystemLibrary.collect_garbage()
        except Exception:
            pass
    unreal.log_warning("[патч-карт] ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
