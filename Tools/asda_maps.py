# Copyright (c) 2026 @sitxovski (Discord)
# asda_maps.py — ЕДИНЫЙ список карт порта и его подмножества.
#
# ★ ГЕНЕРИРУЕТСЯ Tools/asda_map_registry.py из Tools/data/asda_map_registry.json — НЕ ПРАВИТЬ РУКАМИ.
#   Правка: источник (JSON) → `python Tools/asda_map_registry.py`; сверка со всеми слоями — `--check`.
#
# СВЕРЕНО С ДАННЫМИ (2026-08-01), а не «по большинству копий»:
#   каталоги @@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\map              — 24 шт
#   минус  "flamio"  — заглушка: в каталоге только TILE11.TEX, .mapraw = 114 байт, .TER нет
#   минус  "load"    — не карта: экраны загрузки (Start_logo.TEX, ad_img*.TEX)
#   = 22 карты, и это ровно тот список, который был у 8 из 13 копий. Все 22 имеют .TER.
#
# ПОДМНОЖЕСТВА НИЖЕ — НЕ ОШИБКИ КОПИЙ, а осознанные фильтры (проверено по данным):
#   OUTDOOR_MAPS      — небо/облака: без 4 закрытых/инстансных карт
#   MAPS_WITH_MOB_SVR — ровно карты, у которых есть GameServer\MonsterData_Info\*.svr
#   MAPS_WITH_NPC     — карты, где .npc даёт НЕПУСТОЙ список NPC (у inferion и t_castle_of_chess
#                       файл .npc есть, но записей 0 — потому их и не было в раскатке)
#
# ★ Модуль БЕЗ побочных эффектов при импорте (правило tools-no-import-side-effects).

import os
import re

# --------------------------------------------------------------------- канон: 22 карты
ALL_MAPS = [
    "Mountain Flamion", "StagnantDesert", "alpen", "alpen_dungeon", "battlefield",
    "castle_of_chaos", "castle_of_chess", "conquester", "dragonlair", "firewayforest",
    "flabis", "flammio", "inferion", "mine", "nightvalley", "palace_of_queen",
    "quizstage", "rein", "silalis", "sunnycoast", "t_castle_of_chess", "t_palace_of_queen",
]

# Не карты (в ALL_MAPS не входят, задокументированы, чтобы не «чинили» обратно).
NON_MAPS = {
    "flamio": "заглушка: только TILE11.TEX, .mapraw 114 байт, .TER нет",
    "load":   "экраны загрузки (Start_logo.TEX, ad_img*.TEX), не карта",
}

# --------------------------------------------------------------------- подмножества
# Закрытые/инстансные карты: небо и тени облаков к ним не применяются.
INDOOR_MAPS = ["castle_of_chaos", "palace_of_queen", "t_castle_of_chess", "t_palace_of_queen"]
OUTDOOR_MAPS = [m for m in ALL_MAPS if m not in INDOOR_MAPS]      # 18 — купол неба, тени облаков

# Карты со спавнами мобов (есть .svr в GameServer\MonsterData_Info). 21 шт: без mine.
MAPS_WITH_MOB_SVR = [m for m in ALL_MAPS if m not in ("mine",)]

# Карты с НЕПУСТЫМ списком NPC. 17 шт.
#   нет .npc вовсе: alpen_dungeon, flabis, mine
#   .npc есть, но 0 записей: inferion, t_castle_of_chess
MAPS_WITHOUT_NPC = ["alpen_dungeon", "flabis", "mine", "inferion", "t_castle_of_chess"]
MAPS_WITH_NPC = [m for m in ALL_MAPS if m not in MAPS_WITHOUT_NPC]


# --------------------------------------------------------------------- главный цвет неба карты
# ★ 2026-08-02: таблица жила ТРЕМЯ дословными копиями (asda_rollout_sky_full, asda_add_volumetric_clouds,
#   asda_percloud_mics). Сверено: все три побайтово одинаковы, набор ключей = OUTDOOR_MAPS (18 карт).
#   Значение = среднее RGB текстуры SKY_DOME_CLOUDE1 карты (посчитано офлайн). Порядок ключей
#   сохранён от копий — по нему шёл обход раскатки (list(SKY_COLORS.keys())).
SKY_COLORS = {
 "alpen": (0.475, 0.867, 0.971), "alpen_dungeon": (0.0, 0.199, 0.233),
 "battlefield": (0.759, 0.865, 0.989), "castle_of_chess": (0.475, 0.867, 0.971),
 "conquester": (0.928, 0.58, 0.0), "dragonlair": (0.49, 0.16, 0.072),
 "firewayforest": (0.74, 0.377, 0.092), "flabis": (0.651, 0.82, 1.0),
 "flammio": (0.74, 0.377, 0.092), "inferion": (0.274, 0.293, 0.149),
 "mine": (0.475, 0.867, 0.971), "Mountain Flamion": (0.49, 0.16, 0.072),
 "nightvalley": (0.0, 0.123, 0.163), "quizstage": (0.698, 0.773, 0.994),
 "rein": (0.843, 0.935, 0.98), "silalis": (0.759, 0.889, 0.989),
 "StagnantDesert": (0.651, 0.82, 1.0), "sunnycoast": (0.843, 0.935, 0.98),
}

# Порядок обхода небесных тулов = порядок ключей SKY_COLORS (НЕ порядок ALL_MAPS: он другой).
SKY_MAPS = list(SKY_COLORS.keys())


def select_maps(default=None, env="ASDA_MAPS"):
    """Список карт с учётом фильтра из окружения.

    ASDA_MAPS="alpen||rein" — порционная раскатка (одна UE-сессия на 18 карт = OOM).
    Историческое имя переменной у скай/облачных тулов — ASDA_SKY_MAPS; поддержаны обе.
    """
    maps = list(default if default is not None else ALL_MAPS)
    only = os.environ.get(env, "") or os.environ.get("ASDA_SKY_MAPS", "")
    if only:
        want = only.split("||")
        maps = [m for m in maps if m in want]
    return maps


def map_asset_name(map_name):
    """Имя карты, пригодное для имени ассета/уровня UE (пробелы → '_')."""
    return re.sub(r"[^A-Za-z0-9_]", "_", map_name)
