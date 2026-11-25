# Copyright (c) 2026 @sitxovski (Discord)
# asda_map_registry.py — РЕЕСТР КАРТ ПОРТА: единый источник `Tools/data/asda_map_registry.json` → генерация и сверка.
#
# ЗАЧЕМ (аудит дублей 14.09.2026, E15/D5/S1). Список карт жил в пяти местах (map_table.csv, Map_info.csv,
#   asda_maps.py, таблицы в C++, NAME_TEX, +MapsToCook), приведение имени карты к имени ассета — в шести копиях C++.
#   Сверки между ними не было, и новая карта (FlammioPlain, слоты сервера P30) требовала правки в 5–7 местах наугад.
#
# ЧТО ГЕНЕРИРУЕТСЯ из JSON (руками не править; правка = JSON + запуск этого скрипта):
#   1. @@ASTORY_DEV_ROOT@@\Reports\map_table.csv              → DT_MapTable (строка FAsdaMapRow)
#   2. Tools/asda_maps.py                                 — списки карт для инструментов (API прежний)
#   3. RawAssets/Text/World/Map_info.csv                  → DT_Map_info (русские имена карт, id = mapId)
#   4. Source/AsdaReborn/Public/World/AsdaMapRegistry.h   — C++: id, континенты, области карты мира, кроп городской
#                                                           карты, кнопки поиска группы, граф переходов, имя ассета,
#                                                           ★ 19.09.2026: «туман-альфа» .SCE (sce_fog) по имени уровня
#   5. Tools/asda_export_map_zone_data.py                 — блок NAME_TEX (между маркерами)
#   6. Config/DefaultGame.ini                             — блок +MapsToCook (между маркерами)
#                                                           ★ 16.10.2026: и блок правил чанков карт +ChunkRules
#                                                           (чанк = 100 + id карты; секция [/Script/AsdaReborn.AsdaAssetManager],
#                                                           система обновлений — Reports/updater/00_OPTIONS.md)
#   CSV пометку «генерируется» нести не могут (лишняя строка станет строкой DataTable) — их держит --check.
#
# ЗАПУСК (обычный python, unreal НЕ нужен):
#   python Tools/asda_map_registry.py              — перегенерировать; пишет только изменившиеся файлы, с бэкапом
#   python Tools/asda_map_registry.py --diff       — показать разницу, ничего не писать
#   python Tools/asda_map_registry.py --check      — сверка (ничего не пишет); код 1 при ОШИБКАХ, предупреждения — 0
#   python Tools/asda_map_registry.py --fill-sce-fog — ★ 19.09.2026: поля sce_fog карт из клиентских .SCE → JSON
#                                                    (затем обычный запуск — перегенерировать заголовок)
#       --no-server   не ходить на живой сервер (нет сети)
#       --no-client   не читать клиент (Map_info.BIN, папки map\)
#       --dump        сверить ещё и с реестром клиента g_aMapEntityTable из живого дампа @@ASTORY_DEV_ROOT@@\RE\dumps\asda.dmp (долго)
#
# ── ★ КАК ДОБАВИТЬ КАРТУ (пример: FlammioPlain) ──────────────────────────────────────────────────────────────
#   0. Номер карты = следующий свободный id (сейчас 24). Id идут подряд без дыр — от них зависят массивы C++.
#      Потолок сервера — 30 карт (P30: OpenMapInfo.ini и Total_PortalConfig.Ini не больше 30 записей).
#   1. Клиент: папка `map\<folder>` с `.TER` в CLIENT_MINE; уровень экспортирован в `Content/Map/<asset>/<asset>.umap`,
#      где asset = asda_maps.map_asset_name(folder). Имена — только [A-Za-z0-9_ ] (иначе C++ и Python разойдутся).
#   2. JSON, массив "maps" — новый объект (порядок полей как у соседей):
#        id, dir / display (имя папки из g_aMapEntityTable клиента; если записи нет — имя папки), folder,
#        has_level (уровень уже есть), loc_name (русское имя для Map_info; null — строки нет),
#        kind (town / field / special / tournament / campwar / event — правило в check_registry),
#        continent (0 Альпеон, 1 Инферно), item_restrict / campwar_pk (байты +68/+69 записи клиента),
#        bgm_town / bgm_field (из .TER — заполнит `python Tools/asda_export_map_zone_data.py`),
#        name_tex / name_tex_town (файл Data/NPC/Image, как в свитче Map_SetNameTextureByID),
#        town_crop (null, если у карты нет городской большой карты), indoor, mob_svr, npc (yes/none/empty),
#        sky_color (null для закрытых карт; иначе добавить имя в "sky_order"),
#        sce_fog ([начало, конец, у камеры, в конце] из map\<folder>\<folder>.SCE — заполнит
#        `python Tools/asda_map_registry.py --fill-sce-fog`; null без папки), cpp_const (null),
#        party_find (null — кнопки в окне поиска группы нет), server {…} — как станет на живом сервере.
#   3. По необходимости: строки "world_map_areas" (континент, map_id, town, text_id подписи), "portal_edges"
#      (после появления <Имя>_PortalConfig.Ini на сервере; прямоугольник = POS … POS+RANGE).
#   4. `python Tools/asda_map_registry.py`, затем `--check` (ошибок быть не должно).
#   5. DT_MapTable: `Tools\run_headless.bat Tools\asda_setup_zone_assets.py` — пересоздаёт таблицу из CSV, запускать
#      ТОЛЬКО если map_table.csv поменялся; DT_Map_info — импорт текстовых таблиц; C++ — полная сборка (заголовок).
#   6. Сервер (делает владелец, GameServer перезапустить): строка MapInfo.txt, секция MAP_<id> и MapCount в
#      OpenMapInfo.ini, запись и count в Total_PortalConfig.Ini, матрица MapWarpCost.txt N×N, <Имя>_PortalConfig.Ini,
#      ConfigSet/ThreadInfo.ini [MAX_INIT_THREAD] COUNT ≥ числа открытых карт. `--check` покажет, что не сошлось.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): main() только под __main__.
import argparse
import csv
import datetime
import difflib
import io
import json
import os
import re
import shutil
import struct
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(TOOLS_DIR)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)
import asda_paths as _P   # noqa: E402  (пути проекта; модуль без побочных эффектов)
import asda_registry_io as _RIO   # noqa: E402  ★ дедуп №2 В1 Т16: общий ввод-вывод генераторов реестров
import asda_server_txt as _ST     # noqa: E402  ★ дедуп №2 В2 Т7: путь к живому серверу

# ── Пути ──────────────────────────────────────────────────────────────────────────────────────────────────────
REGISTRY_JSON = os.path.join(TOOLS_DIR, "data", "asda_map_registry.json")     # ★ ЕДИНЫЙ ИСТОЧНИК
OUT_MAP_TABLE = os.path.join(_P.RE_REPORTS, "map_table.csv")                   # → DT_MapTable
OUT_ASDA_MAPS = os.path.join(TOOLS_DIR, "asda_maps.py")
OUT_MAP_INFO = os.path.join(PROJECT_DIR, "RawAssets", "Text", "World", "Map_info.csv")
OUT_CPP = os.path.join(PROJECT_DIR, "Source", "AsdaReborn", "Public", "World", "AsdaMapRegistry.h")
OUT_ZONE_TOOL = os.path.join(TOOLS_DIR, "asda_export_map_zone_data.py")
OUT_INI = os.path.join(PROJECT_DIR, "Config", "DefaultGame.ini")
CONTENT_MAP = os.path.join(PROJECT_DIR, "Content", "Map")
# ★ Т3 (11.10.2026, решение владельца): единый корень резервных копий @@ASTORY_DEV_ROOT@@\_backup (был AstoryClient\_backup;
#   старые наборы там не трогаются). Путь внутри набора — от @@ASTORY_DEV_ROOT@@ (asda_headless_tool.backup_rel_path).
BACKUP_ROOT = _P.ROOT + os.sep + "_backup"
# Живой сервер — ТОЛЬКО ЧТЕНИЕ. Путь со скобками; переопределяется переменной окружения.
SERVER_V2 = _ST.LIVE_V2   # ★ дедуп №2 В2 Т7: один путь к живому серверу на все инструменты (переменная ASDA_SERVER_V2 — та же)
CLIENT_MAP_DIR = os.path.join(_P.CLIENT_MINE, "map")
CLIENT_MAP_INFO_BIN = os.path.join(_P.DATA, "Map_info.BIN")

# Маркеры генерируемых блоков внутри файлов, которые пишутся не целиком.
ZONE_BEGIN = "# ── ★ ГЕНЕРИРУЕТСЯ Tools/asda_map_registry.py из Tools/data/asda_map_registry.json: NAME_TEX — НЕ ПРАВИТЬ РУКАМИ ──"
ZONE_END = "# ── конец генерируемого блока NAME_TEX ──"
INI_BEGIN = "; ★ ГЕНЕРИРУЕТСЯ Tools/asda_map_registry.py из Tools/data/asda_map_registry.json: блок +MapsToCook — НЕ ПРАВИТЬ РУКАМИ"
INI_END = "; ★ конец генерируемого блока +MapsToCook"
# ★ 16.10.2026: блок правил чанков карт в секции [/Script/AsdaReborn.AsdaAssetManager] (номер чанка = имя файла у игрока)
CHUNK_BEGIN = "; ★ ГЕНЕРИРУЕТСЯ Tools/asda_map_registry.py: чанки карт (100 + id карты) — НЕ ПРАВИТЬ РУКАМИ"
CHUNK_END = "; ★ конец генерируемого блока чанков карт"
# Смещение номеров чанков карт (0–99 заняты общим контентом, 99 — карта без правила)
MAP_CHUNK_BASE = 100

MAP_TABLE_COLUMNS = ["Name", "MapId", "DirName", "DisplayName", "Level", "ItemRestrict", "CampwarPkFlag",
                     "BgmTown", "BgmField", "NameTex", "NameTexTown"]
KINDS = ("town", "field", "special", "tournament", "campwar", "event")
SAFE_NAME = re.compile(r"^[A-Za-z0-9_ ]+$")   # имена, для которых правило C++ (пробел → «_») == правилу Python


# ══ Загрузка / сохранение источника ══════════════════════════════════════════════════════════════════════════
def load_registry(path=REGISTRY_JSON):
    """Прочитать JSON реестра (общий `asda_registry_io.load_json`)."""
    return _RIO.load_json(path)


def mob_bake_off(reg=None):
    """★ 11.10.2026 (решение владельца по nightvalley). Карты, на которые мобов НЕ запекать и манифест мобов НЕ строить:
    {папка карты: причина} из ключа "mob_bake_off" реестра. Флаг mob_svr при этом остаётся фактом «у сервера есть .svr»
    (сервер своих мобов по 4013 пришлёт всё равно — это поведение сервера, клиентом не отменяется).
    reg=None — прочитать реестр с диска. Читают asda_place_mobs.py и asda_build_mob_manifest.py."""
    if reg is None:
        reg = load_registry()
    return dict(reg.get("mob_bake_off") or {})


def _j(v):
    """Компактная JSON-запись значения (кириллица как есть)."""
    return json.dumps(v, ensure_ascii=False)


def dump_registry(reg):
    """Стабильный текст JSON: карта — блок «поле на строку», строки прочих таблиц — по одной на строку."""
    out = ["{"]
    keys = list(reg.keys())
    for ki, k in enumerate(keys):
        v = reg[k]
        tail = "," if ki < len(keys) - 1 else ""
        if k == "maps":
            out.append('  "maps": [')
            for i, m in enumerate(v):
                out.append("    {")
                fk = list(m.keys())
                for fi, f in enumerate(fk):
                    out.append("      %s: %s%s" % (_j(f), _j(m[f]), "," if fi < len(fk) - 1 else ""))
                out.append("    }" + ("," if i < len(v) - 1 else ""))
            out.append("  ]" + tail)
        elif isinstance(v, list) and v and all(isinstance(x, (dict, str)) for x in v) and k != "sky_order":
            out.append("  %s: [" % _j(k))
            for i, item in enumerate(v):
                out.append("    " + _j(item) + ("," if i < len(v) - 1 else ""))
            out.append("  ]" + tail)
        else:
            out.append("  %s: %s%s" % (_j(k), _j(v), tail))
    out.append("}")
    return ("\n".join(out) + "\n").encode("utf-8")


def save_registry(reg, path=REGISTRY_JSON):
    """Записать JSON реестра (с бэкапом прежней версии)."""
    _write_if_changed(path, dump_registry(reg))


def update_map_fields(updates, path=REGISTRY_JSON):
    """Точечно обновить поля карт: {mapId: {поле: значение}}. Возвращает число изменённых полей; JSON пишется,
    только если что-то поменялось. Используют экспортёры данных (BGM из .TER, байты +68/+69 из дампа)."""
    reg = load_registry(path)
    by_id = {m["id"]: m for m in reg["maps"]}
    changed = 0
    for mid, fields in updates.items():
        m = by_id.get(int(mid))
        if m is None:
            print("  !! карты id=%s нет в реестре — пропуск" % mid)
            continue
        for f, val in fields.items():
            if f not in m:
                raise KeyError("поле '%s' не из схемы реестра (карта %s)" % (f, mid))
            if m[f] != val:
                print("  реестр: карта %d  %s: %r → %r" % (m["id"], f, m[f], val))
                m[f] = val
                changed += 1
    if changed:
        save_registry(reg, path)
    return changed


# ══ ★ 19.09.2026 (свечение, glow_fix5): «туман-альфа» карты из .SCE ═══════════════════════════════════════════
#   Эталон: `CMap_LoadExTileInfo@0x11D3370` читает map\<имя>\<имя>.SCE (путь — world_map.c:926-935) целиком
#   `fread(0x1F8)` в CMap+0x2F80 (world_map.c:18770). Поля по смещению ФАЙЛА: +0x08 цвет тумана, +0x0C начало,
#   +0x10 конец, +0x14 «у камеры», +0x18 «в конце» (dword 3041…3045 объекта; умолчания `effectEmitterInitParams
#   @0xF18570`: 1000 / 5000 / 1.0 / 1.0). `renderUploadFogLightConstants@0x976900` → `CRenderer_SetFogAndLightConstants
#   @0x1124D30` кладёт в c90 (FogInfo EmptyProject.fx) (начало², у камеры, 1/(конец²−начало²), в конце − у камеры);
#   вершинный шейдер .fx-пути даёт fog = saturate(у камеры + (в конце − у камеры)·max(0, d² − начало²)/(конец² − начало²)),
#   а PShade пишет его в АЛЬФУ (EmptyProject.fx:828) — от него зависит вес буфера свечения (UAsdaVideoSettingsSubsystem).
SCE_FOG_OFFSET = 0x0C   # смещение «начала тумана» в .SCE (после магии 4 байта и двух полей)


def read_sce_fog(folder):
    """[начало, конец, у камеры, в конце] из CLIENT_MAP_DIR\\<folder>\\<folder>.SCE (None — файла нет/короткий).
    Числа float32 файла, округлены до 6 значащих цифр (0.3 вместо 0.30000001192...)."""
    p = os.path.join(CLIENT_MAP_DIR, folder, folder + ".SCE")
    raw = _read_bytes(p)
    if raw is None or len(raw) < SCE_FOG_OFFSET + 16:
        return None
    return [float("%.6g" % v) for v in struct.unpack_from("<4f", raw, SCE_FOG_OFFSET)]


def fill_sce_fog(path=REGISTRY_JSON):
    """Поле sce_fog всех карт из клиентских .SCE (новое поле добавляется после sky_color). Возвращает число изменений."""
    reg = load_registry(path)
    changed = 0
    for m in reg["maps"]:
        val = read_sce_fog(m["folder"]) if m.get("folder") else None
        if "sce_fog" not in m:
            items = list(m.items())
            at = [k for k, _v in items].index("sky_color") + 1 if "sky_color" in m else len(items)
            items.insert(at, ("sce_fog", val))
            m.clear()
            m.update(items)
            print("  реестр: карта %d  sce_fog: (нет поля) → %r" % (m["id"], val))
            changed += 1
        elif m["sce_fog"] != val:
            print("  реестр: карта %d  sce_fog: %r → %r" % (m["id"], m["sce_fog"], val))
            m["sce_fog"] = val
            changed += 1
    if changed:
        save_registry(reg, path)
    return changed


# ══ Общие правила имён ═══════════════════════════════════════════════════════════════════════════════════════
def asset_name(name):
    """Имя карты → имя ассета/уровня UE (тот же закон, что asda_maps.map_asset_name)."""
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def cpp_asset_name(name):
    """Правило C++ AsdaMapRegistry::ToAssetName для имён из реестра (пробел → «_»)."""
    return name.replace(" ", "_")


def strip_ext(s):
    """'name_alphen.TEX' → 'name_alphen'."""
    i = s.rfind(".")
    return s[:i] if i > 0 else s


def maps_by_id(reg):
    """Карты по возрастанию id."""
    return sorted(reg["maps"], key=lambda m: m["id"])


def level_path(m):
    """Путь уровня карты для DT_MapTable.Level (пусто, если уровня нет)."""
    if not m.get("has_level") or not m.get("folder"):
        return ""
    a = asset_name(m["folder"])
    return "/Game/Map/%s/%s.%s" % (a, a, a)


def tool_maps(reg):
    """Карты, у которых есть клиентская папка (это и есть ALL_MAPS), в порядке sorted() по имени."""
    return sorted([m for m in reg["maps"] if m.get("folder")], key=lambda m: m["folder"])


def level_and_id(folder, reg=None):
    """(путь пакета уровня `/Game/Map/<a>/<a>`, id карты) по имени клиентской папки карты — для съёмочных и
    диагностических скриптов вместо прошитых таблиц «карта → (уровень, id)» (дедуп №2 В1 Т14, 11.10.2026).
    Нет такой папки в реестре — KeyError. Реестр не меняется (только чтение)."""
    reg = reg if reg is not None else load_registry()
    for m in reg["maps"]:
        if m.get("folder") == folder:
            a = asset_name(m["folder"])
            return "/Game/Map/%s/%s" % (a, a), m["id"]
    raise KeyError("карты с папкой %r нет в реестре" % (folder,))


def _ru_and(names):
    """'a', 'b', 'c' → 'a, b и c'."""
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " и " + names[-1]


# ══ Генераторы ═══════════════════════════════════════════════════════════════════════════════════════════════
def render_map_table_csv(reg):
    """Reports/map_table.csv — источник DT_MapTable (LF, без BOM — как писали прежние экспортёры)."""
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(MAP_TABLE_COLUMNS)
    for m in maps_by_id(reg):
        w.writerow([m["id"], m["id"], m["dir"], m["display"], level_path(m), m["item_restrict"], m["campwar_pk"],
                    m["bgm_town"], m["bgm_field"], strip_ext(m["name_tex"]) if m["name_tex"] else "",
                    strip_ext(m["name_tex_town"]) if m["name_tex_town"] else ""])
    return buf.getvalue().encode("utf-8")


def render_map_info_csv(reg):
    """RawAssets/Text/World/Map_info.csv — русские имена карт (UTF-8 с BOM, CRLF; формат extract_text_tables)."""
    buf = io.StringIO()
    w = csv.writer(buf)                      # lineterminator по умолчанию — CRLF
    w.writerow(["Name", "id", "name"])
    for m in maps_by_id(reg):
        if m.get("loc_name") is not None:
            w.writerow([m["id"], m["id"], m["loc_name"]])
    return ("\ufeff" + buf.getvalue()).encode("utf-8")


def _all_maps_lines(names, max_items=6, max_width=91):
    """Строки списка ALL_MAPS: жадно, не больше 6 имён и 91 символа в строке (так был свёрстан исходный файл)."""
    lines, cur = [], []
    for n in names:
        cand = cur + [n]
        text = "    " + " ".join('"%s",' % x for x in cand)
        if cur and (len(cand) > max_items or len(text) > max_width):
            lines.append("    " + " ".join('"%s",' % x for x in cur))
            cur = [n]
        else:
            cur = cand
    if cur:
        lines.append("    " + " ".join('"%s",' % x for x in cur))
    return "\n".join(lines)


def render_asda_maps_py(reg):
    """Tools/asda_maps.py по шаблону: текст и API прежние, данные — из реестра."""
    tm = tool_maps(reg)
    names = [m["folder"] for m in tm]
    by_folder = {m["folder"]: m for m in tm}
    indoor = [m["folder"] for m in tm if m["indoor"]]
    outdoor_n = len(names) - len(indoor)
    mob_excl = [m["folder"] for m in tm if not m["mob_svr"]]
    npc_none = [m["folder"] for m in tm if m["npc"] == "none"]
    npc_empty = [m["folder"] for m in tm if m["npc"] == "empty"]
    npc_yes_n = len([m for m in tm if m["npc"] == "yes"])
    non_maps = reg["non_maps"]
    sky = reg["sky_order"]
    sky_lines = []
    for i in range(0, len(sky), 2):
        pair = sky[i:i + 2]
        sky_lines.append(" " + ", ".join('"%s": (%s)' % (k, ", ".join(repr(float(c)) for c in by_folder[k]["sky_color"]))
                                         for k in pair) + ",")
    mob_tuple = "(" + ", ".join('"%s"' % x for x in mob_excl) + ("," if len(mob_excl) == 1 else "") + ")"
    subs = {
        "@@N_ALL@@": str(len(names)),
        "@@ALL_MAPS@@": _all_maps_lines(names),
        "@@NON_MAPS@@": "\n".join("    " + ("%s:" % _j(k)).ljust(10) + _j(v) + "," for k, v in non_maps.items()),
        "@@INDOOR@@": "[" + ", ".join('"%s"' % x for x in indoor) + "]",
        "@@N_OUTDOOR@@": str(outdoor_n),
        "@@N_MOB@@": str(len(names) - len(mob_excl)),
        "@@MOB_EXCL_TEXT@@": _ru_and(mob_excl) if mob_excl else "—",
        "@@MOB_EXCL_TUPLE@@": mob_tuple,
        "@@N_NPC@@": str(npc_yes_n),
        "@@NPC_NONE_TEXT@@": ", ".join(npc_none),
        "@@NPC_EMPTY_TEXT@@": ", ".join(npc_empty),
        "@@NPC_WITHOUT@@": "[" + ", ".join('"%s"' % x for x in npc_none + npc_empty) + "]",
        "@@SKY@@": "\n".join(sky_lines),
    }
    text = ASDA_MAPS_TEMPLATE
    for k, v in subs.items():
        text = text.replace(k, v)
    if "@@" in text:
        raise RuntimeError("в шаблоне asda_maps.py остались неподставленные метки")
    return text.encode("utf-8")


def render_zone_tool(reg, current):
    """Tools/asda_export_map_zone_data.py: заменить блок NAME_TEX между маркерами."""
    text = current.decode("utf-8")
    nl = "\r\n" if "\r\n" in text else "\n"
    b, e = text.find(ZONE_BEGIN), text.find(ZONE_END)
    if b < 0 or e < 0 or e < b:
        raise RuntimeError("в %s нет маркеров блока NAME_TEX" % OUT_ZONE_TOOL)
    items = [(m["id"], m["name_tex"], m["name_tex_town"]) for m in maps_by_id(reg) if m["name_tex"]]
    items += [(int(k), v[0], v[1]) for k, v in sorted(reg["name_tex_extra"].items(), key=lambda kv: int(kv[0]))]
    body = ["NAME_TEX = {"]
    for mid, nt, ntt in items:
        body.append("    " + ("%d:" % mid).ljust(4) + "(" + ('"%s",' % nt).ljust(33) + '"%s"),' % ntt)
    body.append("}")
    block = ZONE_BEGIN + nl + nl.join(body) + nl
    return (text[:b] + block + text[e:]).encode("utf-8")


def render_ini(reg, current):
    """Config/DefaultGame.ini: заменить блок +MapsToCook (между маркерами; при первом запуске — обернуть маркерами
    существующие подряд идущие строки +MapsToCook). BOM и окончания строк файла сохраняются."""
    bom = current.startswith(b"\xef\xbb\xbf")
    text = current[3:].decode("utf-8") if bom else current.decode("utf-8")
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)
    cook = list(reg["cook_extra_levels"])
    cook += ["/Game/Map/%s/%s" % (asset_name(m["folder"]), asset_name(m["folder"]))
             for m in sorted([m for m in reg["maps"] if m.get("has_level")], key=lambda m: asset_name(m["folder"]).lower())]
    block = [INI_BEGIN] + ['+MapsToCook=(FilePath="%s")' % p for p in cook] + [INI_END]
    if INI_BEGIN in lines and INI_END in lines:
        b, e = lines.index(INI_BEGIN), lines.index(INI_END)
        new = lines[:b] + block + lines[e + 1:]
    else:
        idx = [i for i, ln in enumerate(lines) if ln.startswith("+MapsToCook=")]
        if not idx or idx[-1] - idx[0] + 1 != len(idx):
            raise RuntimeError("в DefaultGame.ini строки +MapsToCook не идут подряд — оберни их маркерами вручную")
        new = lines[:idx[0]] + block + lines[idx[-1] + 1:]
    # ★ 16.10.2026: правила чанков карт — папка /Game/Map/<ассет>/ → чанк 100 + id (только карты с уровнем)
    chunk = [CHUNK_BEGIN] + ['+ChunkRules=(PathPrefix="/Game/Map/%s/",ChunkId=%d)' % (asset_name(m["folder"]), MAP_CHUNK_BASE + m["id"])
                             for m in maps_by_id(reg) if m.get("has_level") and m.get("folder")] + [CHUNK_END]
    if CHUNK_BEGIN not in new or CHUNK_END not in new:
        raise RuntimeError("в DefaultGame.ini нет маркеров блока чанков карт (секция [/Script/AsdaReborn.AsdaAssetManager])")
    b, e = new.index(CHUNK_BEGIN), new.index(CHUNK_END)
    new = new[:b] + chunk + new[e + 1:]
    out = nl.join(new).encode("utf-8")
    return (b"\xef\xbb\xbf" + out) if bom else out


def _cf(x):
    """float → литерал C++ (256 → 256.f, 2.5 → 2.5f)."""
    s = repr(float(x))
    if s.endswith(".0"):
        s = s[:-2]
    return s + (".f" if "." not in s else "f")


def render_cpp_header(reg):
    """Source/AsdaReborn/Public/World/AsdaMapRegistry.h (UTF-8 с BOM, CRLF — как исходники проекта)."""
    maps = maps_by_id(reg)
    n = len(maps)
    by_id = {m["id"]: m for m in maps}
    srv_name = lambda mid: (by_id[mid]["server"] or {}).get("mapinfo_name") or by_id[mid]["dir"]
    L = []
    a = L.append
    a("// AsdaMapRegistry.h — РЕЕСТР КАРТ для C++: id карт, континенты карты мира, области карты мира, кроп городской")
    a("//   большой карты, кнопки поиска группы, граф межкартовых переходов, приведение имени оригинала к имени ассета UE.")
    a("//")
    a("// ★ ГЕНЕРИРУЕТСЯ Tools/asda_map_registry.py из Tools/data/asda_map_registry.json — НЕ ПРАВИТЬ РУКАМИ.")
    a("//   Правка: источник (JSON) → `python Tools/asda_map_registry.py`; сверка со всеми слоями — `--check`.")
    a("//   Строки карт (DirName, уровень, BGM, текстуры названий) — в DT_MapTable (FAsdaMapRow), из того же источника.")
    a("#pragma once")
    a("")
    a('#include "CoreMinimal.h"')
    a('#include "Misc/Paths.h"')
    a("")
    a("namespace AsdaMapRegistry")
    a("{")
    a("\t// Число карт в реестре; id идут подряд 0…kMapCount-1.")
    a("\tinline constexpr int32 kMapCount = %d;" % n)
    a("")
    a("\t// ── id карт, на которые ссылается код ──")
    for m in maps:
        if m.get("cpp_const"):
            a("\tinline constexpr int32 %s = %d;   // %s (kind=%s, campwar/PK=%d)" % (m["cpp_const"], m["id"], m["dir"], m["kind"], m["campwar_pk"]))
    a("")
    a("\t// Имя оригинала (папка карты, текстура названия, звук) → имя ассета UE: пробел в имени пакета недопустим → «_».")
    a("\t//   Python-двойник — asda_maps.map_asset_name (он заменяет любой не [A-Za-z0-9_]); для имён из реестра правила")
    a("\t//   совпадают — это проверяет --check.")
    a("\tinline FString ToAssetName(const FString& Name)")
    a("\t{")
    a('\t\treturn Name.Replace(TEXT(" "), TEXT("_"));')
    a("\t}")
    a("")
    a("\t// Путь сетки проходимости/высот карты (DA_WalkGrid) по имени папки карты (DT_MapTable.DirName). Символы,")
    a("\t//   запрещённые FPaths::MakeValidFileName (/?:&\\\\*\"<>|%#@^), отбрасываются — как в прежних двух копиях пути.")
    a("\tinline FString WalkGridObjectPath(const FString& DirName)")
    a("\t{")
    a('\t\treturn FString::Printf(TEXT("/Game/Map/%s/DA_WalkGrid.DA_WalkGrid"), *ToAssetName(FPaths::MakeValidFileName(DirName)));')
    a("\t}")
    a("")
    a("\t// Континент карты мира (комбобокс 1721): 0 «Альпеон», 1 «Инферно». Индекс — id карты.")
    a("\tinline constexpr int8 kContinentById[kMapCount] = { %s };" % ", ".join(str(m["continent"]) for m in maps))
    a("\tinline int32 ContinentOf(int32 MapId)")
    a("\t{")
    a("\t\tif (MapId >= 0 && MapId < kMapCount) { return kContinentById[MapId]; }")
    a("\t\t// Вне реестра — правило эталона worldMapDlg_SelectCurrentMap@0xD66A20: континент 1, если id >= 12 или == 7.")
    a("\t\treturn (MapId >= 12 || MapId == 7) ? 1 : 0;")
    a("\t}")
    a("")
    a("\t// Кроп городской большой карты (hudIconSetUvBySlot@0xB54450, isSpecialSlotIndex): начало и масштаб в пикселях.")
    a("\tstruct FTownCrop { int32 MapId; float OriginX; float OriginY; float Scale; };")
    a("\tinline constexpr FTownCrop kTownCrops[] = {")
    crops = [m for m in maps if m.get("town_crop")]
    if not crops:
        raise RuntimeError("пустой список town_crop — C++ не допускает массив нулевой длины")
    for m in crops:
        ox, oy, sc = m["town_crop"]
        a("\t\t{ %d, %s, %s, %s },   // %s" % (m["id"], _cf(ox), _cf(oy), _cf(sc), m["dir"]))
    a("\t};")
    a("\tinline const FTownCrop* FindTownCrop(int32 MapId)")
    a("\t{")
    a("\t\tfor (const FTownCrop& C : kTownCrops) { if (C.MapId == MapId) { return &C; } }")
    a("\t\treturn nullptr;")
    a("\t}")
    a("")
    fogs = [m for m in maps if m.get("sce_fog") and m.get("folder")]
    if not fogs:
        raise RuntimeError("пустой список sce_fog — C++ не допускает массив нулевой длины (запусти --fill-sce-fog)")
    a("\t// ★ 19.09.2026: «туман-альфа» карты из map\\<папка>\\<папка>.SCE (эталон `CMap_LoadExTileInfo@0x11D3370`, поля")
    a("\t//   +0x0C…+0x18 → c90/FogInfo EmptyProject.fx): fog = saturate(Near + (Far − Near)·max(0, d² − Start²)/(End² − Start²)),")
    a("\t//   d — расстояние камера–точка (игровые юниты = юниты UE). Ключ — имя ассета уровня (/Game/Map/<Asset>/<Asset>).")
    a("\tstruct FSceFog { int32 MapId; const TCHAR* Asset; float Start; float End; float Near; float Far; };")
    a("\tinline constexpr FSceFog kSceFogs[] = {")
    for m in fogs:
        s, e, nr, fr = m["sce_fog"]
        a('\t\t{ %2d, TEXT("%s"), %s, %s, %s, %s },' % (m["id"], asset_name(m["folder"]), _cf(s), _cf(e), _cf(nr), _cf(fr)))
    a("\t};")
    a("\t// Запись «туман-альфы» по имени ассета уровня (без регистра); nullptr — карты нет в реестре или .SCE не было.")
    a("\tinline const FSceFog* FindSceFogByAsset(const FString& AssetName)")
    a("\t{")
    a("\t\tfor (const FSceFog& F : kSceFogs) { if (AssetName.Equals(F.Asset, ESearchCase::IgnoreCase)) { return &F; } }")
    a("\t\treturn nullptr;")
    a("\t}")
    a("")
    a("\t// Области карты мира в порядке ключей комбобокса «Область» (uiInitSlotIndexMaps@0xD66320;")
    a("\t//   подписи helpWndFillNoticeList@0xD66880: string-ID в DT_GameText).")
    a("\tstruct FWorldMapAreaDef { int32 Continent; int32 MapId; bool bTown; int32 TextId; };")
    a("\tinline constexpr FWorldMapAreaDef kWorldMapAreas[] = {")
    for r in reg["world_map_areas"]:
        a("\t\t{ %d, %2d, %-5s, %d },   // %s" % (r["continent"], r["map_id"], "true" if r["town"] else "false", r["text_id"], r["label"]))
    a("\t};")
    a("")
    pf = sorted([m for m in maps if m.get("party_find")], key=lambda m: m["party_find"]["button_idnum"])
    if not pf:
        raise RuntimeError("пустой party_find")
    a("\t// Поиск группы: кнопки карт-фильтров (IDNum контрола → UI-индекс 1…N, он же смещение подписи от 11801).")
    a("\t//   Проволочный код карты в пакетах = id карты (доказано подписями 11802…11817 ↔ remapIndexForward@0xDBA900).")
    a("\tstruct FPartyFindButton { int32 IdNum; int32 UiIndex; const TCHAR* Idc; };")
    a("\tinline constexpr FPartyFindButton kPartyFindButtons[] = {")
    for m in pf:
        p = m["party_find"]
        a('\t\t{ %d, %2d, TEXT("%s") },   // %s' % (p["button_idnum"], p["ui_index"], p["button_idc"], m["dir"]))
    a("\t};")
    max_ui = max(m["party_find"]["ui_index"] for m in pf)
    ui_to_map = [-1] * (max_ui + 1)
    for m in pf:
        ui_to_map[m["party_find"]["ui_index"]] = m["id"]
    max_wire = max(m["id"] for m in pf)
    map_to_ui = [-1] * (max_wire + 2)
    map_to_ui[0] = 0                           # проволочный −1 («все карты») → UI-индекс 0
    for m in pf:
        map_to_ui[m["id"] + 1] = m["party_find"]["ui_index"]
    a("\t// UI-индекс 0…%d → id карты (−1 = «все»). Порт remapIndexForward@0xDBA900." % max_ui)
    a("\tinline constexpr int32 kPartyFindUiToMap[] = { %s };" % ", ".join(str(x) for x in ui_to_map))
    a("\t// [id карты + 1] (−1…%d) → UI-индекс. Порт remapIndexReverse@0xDBAA50." % max_wire)
    a("\tinline constexpr int32 kPartyFindMapToUi[] = { %s };" % ", ".join(str(x) for x in map_to_ui))
    a("")
    # ★ 27.09.2026: открытость карт на сервере — для маршрутов трекера/автопути (закрытые карты из маршрутов исключаются).
    a("\t// ★ Открыта ли карта на СЕРВЕРЕ: зеркало OpenMapInfo.ini (MAP_<id> / Use) из maps[].server.open_use.")
    a("\t//   ОБНОВЛЯТЬ ПРИ ОТКРЫТИИ/ЗАКРЫТИИ КАРТЫ на сервере (правка JSON → генерация; `--check` сверяет с живым).")
    a("\t//   Клиенту взять это неоткуда: напольный портал сервера (sub_52C4C0) молча игнорирует зону, ведущую на карту")
    a("\t//   с Use=0, пакета об этом нет; у оригинала граф трекера (pathGraph_BuildNavMesh) закрытость не знает.")
    a("\t//   Нет записи в OpenMapInfo (open_use = null) — 0: сервер такую карту не открывает.")
    opens = [(1 if (m.get("server") or {}).get("open_use") == 1 else 0) for m in maps]
    a("\tinline constexpr int8 kServerMapOpen[kMapCount] = { %s };" % ", ".join(str(x) for x in opens))
    a("\tinline bool IsMapOpenOnServer(int32 MapId)")
    a("\t{")
    a("\t\t// Вне реестра — закрыта: сервер отвергает номер карты вне своей таблицы (dword_5F5EE8 <= id).")
    a("\t\treturn MapId >= 0 && MapId < kMapCount && kServerMapOpen[MapId] != 0;")
    a("\t}")
    a("")
    a("\t// Граф межкартовых переходов для трекера квестов: прямоугольники зон из серверных")
    a("\t//   GameServer\\PortalIP_Info\\<Имя>_PortalConfig.Ini ([POSX, POSX+RANGEX] × [POSZ, POSZ+RANGEZ], в клетках).")
    a("\tstruct FPortalEdge { int32 From; int32 To; int32 X0; int32 Z0; int32 X1; int32 Z1; };")
    a("\tinline constexpr FPortalEdge kPortalGraph[] = {")
    last_file = None
    for e in reg["portal_edges"]:
        if e["file"] != last_file:
            a("\t\t// %s" % e["file"])
            last_file = e["file"]
        a("\t\t{ %2d, %2d, %3d,%3d, %3d,%3d },   // → %s%s" % (e["from"], e["to"], e["x0"], e["z0"], e["x1"], e["z1"], srv_name(e["to"]),
                                                          "" if opens[e["to"]] else "   [закрыта на сервере — в маршрут не идёт]"))
    a("\t};")
    a("}")
    a("")
    return ("\ufeff" + "\r\n".join(L)).encode("utf-8")


def _read_bytes(path):
    """Байты файла или None (общий `asda_registry_io.read_bytes`)."""
    return _RIO.read_bytes(path)


def rendered_outputs(reg):
    """[(путь, ожидаемые байты)] для всех генерируемых файлов."""
    zone_cur = _read_bytes(OUT_ZONE_TOOL)
    ini_cur = _read_bytes(OUT_INI)
    return [
        (OUT_MAP_TABLE, render_map_table_csv(reg)),
        (OUT_ASDA_MAPS, render_asda_maps_py(reg)),
        (OUT_MAP_INFO, render_map_info_csv(reg)),
        (OUT_CPP, render_cpp_header(reg)),
        (OUT_ZONE_TOOL, render_zone_tool(reg, zone_cur)),
        (OUT_INI, render_ini(reg, ini_cur)),
    ]


def _backup_path(path):
    """Путь бэкапа: @@ASTORY_DEV_ROOT_ESC@@\\_backup\\<дата>_map_registry\\<путь относительно @@ASTORY_DEV_ROOT_ESC@@> (★ Т3, 11.10.2026: был
    AstoryClient\\_backup\\…\\<путь от проекта | _root\\путь от корня>). Правило «копия дня + промежуточная со
    временем» — в _write_if_changed."""
    import asda_headless_tool as HT   # лениво: модулю реестра не нужен каркас при импорте
    day = datetime.date.today().isoformat()
    return os.path.join(BACKUP_ROOT, day + "_map_registry", HT.backup_rel_path(path, _P.ROOT))


def _write_if_changed(path, data):
    """Записать файл, только если содержимое другое; прежнюю версию — в бэкап (один раз за день, со сверкой размера);
    атомарно. Тело — общий `asda_registry_io.write_if_changed` (набор `_map_registry`, временный файл `.tmp_map_registry`)."""
    return _RIO.write_if_changed(path, data, _backup_path, ".tmp_map_registry")


def generate(write=True, show_diff=False, reg=None):
    """Сгенерировать все файлы. Возвращает список путей, которые изменились (или изменились бы).
    Тело — общий `asda_registry_io.generate` (сверка реестра → отрисовка → запись с бэкапом)."""
    reg = reg or load_registry()
    return _RIO.generate(reg, check_registry, rendered_outputs, _write_if_changed, write=write, show_diff=show_diff)


# ══ Сверка ═══════════════════════════════════════════════════════════════════════════════════════════════════
def check_registry(reg):
    """Внутренняя целостность источника. → (ошибки, предупреждения, сведения)."""
    E, W, I = [], [], []
    maps = maps_by_id(reg)
    ids = [m["id"] for m in maps]
    if ids != list(range(len(ids))):
        E.append("id карт должны идти подряд 0…N-1, сейчас: %s" % ids)
    if len(maps) > reg.get("server_max_maps", 30):
        W.append("карт в реестре %d > слотов сервера %d (P30)" % (len(maps), reg.get("server_max_maps", 30)))
    by_id = {m["id"]: m for m in maps}
    seen_folder, seen_asset, seen_const = {}, {}, {}
    for m in maps:
        tag = "карта %d (%s)" % (m["id"], m["dir"])
        if m["kind"] not in KINDS:
            E.append("%s: kind=%r не из %s" % (tag, m["kind"], KINDS))
        names = [m["dir"]] + ([m["folder"]] if m.get("folder") else [])
        names += [strip_ext(x) for x in (m["name_tex"], m["name_tex_town"]) if x]
        names += [x for x in (m["bgm_town"], m["bgm_field"]) if x]
        for nm in names:
            if not SAFE_NAME.match(nm) or cpp_asset_name(nm) != asset_name(nm):
                E.append("%s: имя %r вне [A-Za-z0-9_ ] — C++ ToAssetName и Python map_asset_name разойдутся" % (tag, nm))
        if m.get("folder"):
            f = m["folder"]
            if f in seen_folder:
                E.append("%s: папка %r уже у карты %d" % (tag, f, seen_folder[f]))
            seen_folder[f] = m["id"]
            al = asset_name(f).lower()
            if al in seen_asset:
                E.append("%s: имя ассета %r совпадает (без регистра) с картой %d" % (tag, al, seen_asset[al]))
            seen_asset[al] = m["id"]
        else:
            for fld in ("has_level", "indoor", "mob_svr"):
                if m.get(fld):
                    E.append("%s: %s=true без клиентской папки folder" % (tag, fld))
            if m.get("sky_color"):
                E.append("%s: sky_color без клиентской папки" % tag)
        if m.get("folder") and asset_name(m["folder"]).lower() != cpp_asset_name(m["dir"]).lower():
            I.append("%s: DirName %r ≠ папке уровня %r (путь /Game/Map/<DirName>/DA_WalkGrid спасает только "
                     "нечувствительность к регистру)" % (tag, m["dir"], asset_name(m["folder"])))
        if m["continent"] not in (0, 1):
            E.append("%s: continent=%r" % (tag, m["continent"]))
        rule = 1 if (m["id"] >= 12 or m["id"] == 7) else 0
        if m["continent"] != rule:
            I.append("%s: континент %d отличается от правила эталона (id>=12 или 7 → 1)" % (tag, m["continent"]))
        if m.get("cpp_const"):
            if m["cpp_const"] in seen_const:
                E.append("%s: cpp_const %s повторяется" % (tag, m["cpp_const"]))
            seen_const[m["cpp_const"]] = m["id"]
        # тип карты — согласованность с данными
        srv = m.get("server") or {}
        if (m["kind"] == "campwar") != (m["campwar_pk"] == 1):
            E.append("%s: kind=%s, а campwar_pk=%d" % (tag, m["kind"], m["campwar_pk"]))
        if (m["kind"] == "tournament") != bool(m.get("folder") and m["folder"].startswith("t_")):
            E.append("%s: kind=%s не согласован с префиксом t_ папки" % (tag, m["kind"]))
        if srv.get("mapinfo_town") is not None and (m["kind"] == "town") != (srv["mapinfo_town"] == 1):
            E.append("%s: kind=%s, а MapInfo.txt столбец «город»=%s" % (tag, m["kind"], srv["mapinfo_town"]))
        if srv.get("mapinfo_type") is not None and m["kind"] not in ("tournament", "campwar") and \
                (m["kind"] == "special") != (srv["mapinfo_type"] == 3):
            E.append("%s: kind=%s, а MapInfo.txt столбец «тип»=%s" % (tag, m["kind"], srv["mapinfo_type"]))
        if m.get("town_crop") and m["kind"] != "town":
            W.append("%s: town_crop у карты не-города" % tag)
        # ★ 19.09.2026: «туман-альфа» — 4 числа, конец дальше начала (иначе деление на ноль в формуле .fx)
        sf = m.get("sce_fog")
        if "sce_fog" not in m:
            E.append("%s: нет поля sce_fog (запусти --fill-sce-fog)" % tag)
        elif sf is not None:
            if not (isinstance(sf, list) and len(sf) == 4 and all(isinstance(x, (int, float)) for x in sf)):
                E.append("%s: sce_fog=%r — нужно [начало, конец, у камеры, в конце]" % (tag, sf))
            elif sf[1] <= sf[0]:
                E.append("%s: sce_fog конец %r ≤ начала %r" % (tag, sf[1], sf[0]))
            if not m.get("folder"):
                E.append("%s: sce_fog без клиентской папки" % tag)
        elif m.get("folder"):
            W.append("%s: у карты с папкой sce_fog=null — свечение возьмёт вес без тумана" % tag)
        if m["npc"] not in ("yes", "none", "empty"):
            E.append("%s: npc=%r" % (tag, m["npc"]))
        if m.get("loc_name") is None:
            I.append("%s: нет русского имени в Map_info (рантайм берёт DisplayName/DirName)" % tag)
    # ★ 11.10.2026: ключи mob_bake_off — папки карт, у которых mob_svr=true (иначе запрет бессмыслен/опечатка).
    _folders = {m["folder"]: m for m in maps if m.get("folder")}
    for f, why in (reg.get("mob_bake_off") or {}).items():
        if f not in _folders:
            E.append("mob_bake_off: %r — нет карты с такой папкой" % f)
        elif not _folders[f]["mob_svr"]:
            W.append("mob_bake_off: у %r mob_svr=false — запрет запекания ничего не меняет" % f)
        if not (isinstance(why, str) and why.strip()):
            E.append("mob_bake_off: у %r нет причины" % f)
    sky = reg["sky_order"]
    sky_maps = sorted(m["folder"] for m in maps if m.get("sky_color"))
    if sorted(sky) != sky_maps or len(set(sky)) != len(sky):
        E.append("sky_order %s ≠ карты с sky_color %s" % (sky, sky_maps))
    for r in reg["world_map_areas"]:
        m = by_id.get(r["map_id"])
        if m is None:
            E.append("область карты мира %s: нет карты %d" % (r["label"], r["map_id"]))
        elif m["continent"] != r["continent"]:
            E.append("область %s: континент %d ≠ континенту карты %d (%d)" % (r["label"], r["continent"], m["id"], m["continent"]))
    tids = [r["text_id"] for r in reg["world_map_areas"]]
    if len(set(tids)) != len(tids):
        E.append("повторяются text_id областей карты мира")
    pf = [m for m in maps if m.get("party_find")]
    for key in ("ui_index", "button_idnum", "button_idc"):
        vals = [m["party_find"][key] for m in pf]
        if len(set(vals)) != len(vals):
            E.append("party_find.%s повторяется" % key)
    if sorted(m["party_find"]["ui_index"] for m in pf) != list(range(1, len(pf) + 1)):
        E.append("party_find.ui_index должны быть 1…%d" % len(pf))
    for e in reg["portal_edges"]:
        if e["from"] not in by_id or e["to"] not in by_id:
            E.append("ребро перехода %s: нет карты %d или %d" % (e["file"], e["from"], e["to"]))
        if e["x0"] > e["x1"] or e["z0"] > e["z1"]:
            E.append("ребро перехода %s %d→%d: перевёрнутый прямоугольник" % (e["file"], e["from"], e["to"]))
    return E, W, I


def check_generated(reg):
    """Источник ↔ сгенерированные файлы."""
    E = []
    for path, data in rendered_outputs(reg):
        cur = _read_bytes(path)
        if cur != data:
            E.append("файл не совпадает с реестром (правлен руками или не перегенерирован): %s" % path)
    return E


def check_levels(reg):
    """Источник ↔ уровни Content/Map."""
    E, W, I = [], [], []
    known = set()
    for m in maps_by_id(reg):
        if not m.get("folder"):
            continue
        a = asset_name(m["folder"])
        p = os.path.join(CONTENT_MAP, a, a + ".umap")
        known.add(("%s/%s" % (a, a)).lower())
        if m["has_level"] and not os.path.isfile(p):
            E.append("карта %d: has_level=true, но нет %s" % (m["id"], p))
        if not m["has_level"] and os.path.isfile(p):
            W.append("карта %d: уровень %s есть, а has_level=false (не попадёт в DT_MapTable/MapsToCook)" % (m["id"], p))
    extra = {x.replace("/Game/Map/", "").lower() for x in reg["cook_extra_levels"]}
    for root, _d, files in os.walk(CONTENT_MAP):
        for f in files:
            if f.lower().endswith(".umap"):
                rel = os.path.relpath(os.path.join(root, f), CONTENT_MAP).replace("\\", "/")[:-5].lower()
                if rel not in known and rel not in extra:
                    I.append("уровень вне реестра: /Game/Map/%s" % rel)
    return E, W, I


def check_client(reg):
    """Источник ↔ клиент: папки map\\<folder> с .TER, Map_info.BIN."""
    E, W, I = [], [], []
    if not os.path.isdir(CLIENT_MAP_DIR):
        W.append("нет клиентского каталога %s — сверка с клиентом пропущена" % CLIENT_MAP_DIR)
        return E, W, I
    for m in maps_by_id(reg):
        if not m.get("folder"):
            continue
        d = os.path.join(CLIENT_MAP_DIR, m["folder"])
        if not os.path.isdir(d) or not any(f.lower().endswith(".ter") for f in os.listdir(d)):
            W.append("карта %d: в клиенте нет %s\\*.TER" % (m["id"], d))
        # ★ 19.09.2026: sce_fog реестра ↔ .SCE клиента
        if "sce_fog" in m and read_sce_fog(m["folder"]) != m["sce_fog"]:
            W.append("карта %d: sce_fog %r ≠ %s.SCE клиента %r (--fill-sce-fog)" % (m["id"], m["sce_fog"], m["folder"], read_sce_fog(m["folder"])))
    for nm in reg["non_maps"]:
        if not os.path.isdir(os.path.join(CLIENT_MAP_DIR, nm)):
            I.append("не-карта %r из non_maps в клиенте отсутствует" % nm)
    raw = _read_bytes(CLIENT_MAP_INFO_BIN)
    if raw is None or len(raw) < 20:
        W.append("нет %s — сверка имён пропущена" % CLIENT_MAP_INFO_BIN)
        return E, W, I
    count = struct.unpack_from("<I", raw, 4)[0]
    stride = 392                                       # extract_text_tables.TABLES: Map_info.BIN, stride 392
    bin_names = {}
    for i in range(count):
        o = 0x14 + i * stride
        if o + stride > len(raw):
            break
        rec = raw[o:o + stride]
        bin_names[struct.unpack_from("<i", rec, 0)[0]] = rec[4:].split(b"\x00")[0].decode("cp1251", "replace")
    by_id = {m["id"]: m for m in maps_by_id(reg)}
    for mid in sorted(set(bin_names) | {m["id"] for m in by_id.values() if m.get("loc_name") is not None}):
        a = bin_names.get(mid)
        b = by_id[mid].get("loc_name") if mid in by_id else None
        if a != b:
            W.append("Map_info.BIN id=%d: %r ≠ реестр loc_name %r" % (mid, a, b))
    return E, W, I


def check_client_dump(reg):
    """Источник ↔ реестр карт клиента g_aMapEntityTable из живого дампа (через asda_export_map_flags)."""
    W = []
    import asda_export_map_flags as _FL
    if not os.path.isfile(_FL.DUMP_PATH):
        return ["нет дампа %s — сверка с g_aMapEntityTable пропущена" % _FL.DUMP_PATH]
    recs = {r[0]: r for r in _FL.read_records()}
    for m in maps_by_id(reg):
        r = recs.get(m["id"])
        if r is None:
            W.append("g_aMapEntityTable: у карты %d нет записи (в оригинале зона недоступна)" % m["id"])
            continue
        if m["dir"].lower().replace(" ", "_") != r[2].lower().replace(" ", "_"):
            W.append("g_aMapEntityTable карта %d: папка %r ≠ реестр dir %r" % (m["id"], r[2], m["dir"]))
        if (m["item_restrict"], m["campwar_pk"]) != (r[3], r[4]):
            W.append("g_aMapEntityTable карта %d: +68/+69 = %d/%d ≠ реестр %d/%d"
                     % (m["id"], r[3], r[4], m["item_restrict"], m["campwar_pk"]))
    return W


def _read_text(path, enc="cp1251"):
    """Текст файла сервера (cp1251)."""
    with open(path, "rb") as f:
        return f.read().decode(enc, "replace")


def _ini_sections(text):
    """INI сервера → {секция: {ключ в нижнем регистре: значение}}; комментарии `//` и `;` отрезаются."""
    secs, cur = {}, None
    for raw in text.splitlines():
        ln = raw.split("//")[0].strip()
        if not ln or ln.startswith(";"):
            continue
        mm = re.match(r"\[([^\]]+)\]", ln)
        if mm:
            cur = mm.group(1).strip()
            secs.setdefault(cur, {})
            continue
        if cur is not None and "=" in ln:
            k, v = ln.split("=", 1)
            secs[cur].setdefault(k.strip().lower(), v.strip().strip('"').strip())
    return secs


def read_server_portal_edges(gs):
    """Все зоны переходов живого сервера: [(файл, from, to, x0, z0, x1, z1)] в порядке имён файлов."""
    n, rows = read_server_mapinfo(gs)
    name_to_id = {f[1].strip().lower(): mid for mid, f in rows.items()}
    d = os.path.join(gs, "PortalIP_Info")
    out, unknown = [], []
    for fn in sorted(os.listdir(d)):
        if not fn.lower().endswith("_portalconfig.ini"):
            continue
        stem = fn[:-len("_PortalConfig.Ini")]
        if stem.lower() in ("total", "temp"):
            continue
        frm = name_to_id.get(stem.lower())
        secs = _ini_sections(_read_text(os.path.join(d, fn)))
        cnt = int((secs.get("PORTAL_COUNT") or {}).get("count", "0") or 0)
        if frm is None:
            unknown.append(fn)
            continue
        for i in range(cnt):
            s = secs.get("PORTAL_INFO_%d" % i)
            if not s:
                continue
            x, z = int(s["posx"]), int(s["posz"])
            out.append((stem, frm, int(s["index"]), x, z, x + int(s["rangex"]), z + int(s["rangez"])))
    return out, unknown


def read_server_mapinfo(gs):
    """MapData_Info/MapInfo.txt → (число из первой строки, {id: столбцы})."""
    lines = _read_text(os.path.join(gs, "MapData_Info", "MapInfo.txt")).splitlines()
    n = int(lines[0].split()[0])
    rows = {}
    for ln in lines[1:]:
        f = ln.split("\t")
        if len(f) > 7 and re.match(r"^-?\d+$", f[0].strip()):
            rows[int(f[0])] = f
    return n, rows


def check_server(reg):
    """Источник ↔ живой сервер (только чтение): MapInfo.txt, OpenMapInfo.ini, Total_PortalConfig.Ini,
    MapWarpCost.txt, ThreadInfo.ini, зоны переходов PortalIP_Info."""
    E, W, I = [], [], []
    gs = os.path.join(SERVER_V2, "GameServer")
    if not os.path.isdir(gs):
        W.append("живой сервер недоступен (%s) — сверка пропущена" % gs)
        return E, W, I
    maps = maps_by_id(reg)
    nmaps = len(maps)
    cap = reg.get("server_max_maps", 30)
    # MapInfo.txt
    n, rows = read_server_mapinfo(gs)
    if n != len(rows):
        W.append("MapInfo.txt: первая строка %d, а строк карт %d" % (n, len(rows)))
    if n != nmaps:
        W.append("MapInfo.txt: карт %d, в реестре %d" % (n, nmaps))
    for m in maps:
        s = m.get("server") or {}
        f = rows.get(m["id"])
        if f is None:
            W.append("MapInfo.txt: нет строки карты %d" % m["id"])
            continue
        got = (f[1].strip(), int(f[5]), int(f[6]))
        want = (s.get("mapinfo_name"), s.get("mapinfo_town"), s.get("mapinfo_type"))
        if got != want:
            W.append("MapInfo.txt карта %d: (имя, город, тип)=%s ≠ реестр %s" % (m["id"], got, want))
    # OpenMapInfo.ini
    secs = _ini_sections(_read_text(os.path.join(gs, "MapData_Info", "OpenMapInfo.ini")))
    mc = int((secs.get("MAP_INFO") or {}).get("mapcount", "0") or 0)
    open_secs = sorted(int(k[4:]) for k in secs if re.match(r"^MAP_\d+$", k))
    if mc != len(open_secs):
        W.append("OpenMapInfo.ini: MapCount=%d, а секций MAP_<n> %d" % (mc, len(open_secs)))
    if len(open_secs) > cap or mc > cap:
        W.append("OpenMapInfo.ini: записей %d / MapCount %d > %d (P30)" % (len(open_secs), mc, cap))
    if len(open_secs) != nmaps:
        W.append("OpenMapInfo.ini: секций %d, карт в реестре %d" % (len(open_secs), nmaps))
    for m in maps:
        s = m.get("server") or {}
        sec = secs.get("MAP_%d" % m["id"])
        got = (sec.get("name"), int(sec.get("use", "0") or 0)) if sec else (None, None)
        want = (s.get("open_name"), s.get("open_use"))
        if got != want:
            W.append("OpenMapInfo.ini MAP_%d: (Name, Use)=%s ≠ реестр %s" % (m["id"], got, want))
    # Total_PortalConfig.Ini
    tsec = _ini_sections(_read_text(os.path.join(gs, "PortalIP_Info", "Total_PortalConfig.Ini")))
    tc = int((tsec.get("PORTAL_COUNT") or {}).get("count", "0") or 0)
    if tc > cap:
        W.append("Total_PortalConfig.Ini: count=%d > %d (P30)" % (tc, cap))
    if tc != nmaps:
        W.append("Total_PortalConfig.Ini: count=%d, карт в реестре %d" % (tc, nmaps))
    tindex = {}
    for i in range(tc):
        s = tsec.get("PORTAL_INFO_%d" % i)
        if s and "index" in s:
            tindex[int(s["index"])] = int(s.get("use", "0") or 0)
    for m in maps:
        s = m.get("server") or {}
        if tindex.get(m["id"]) != s.get("total_portal_use"):
            W.append("Total_PortalConfig.Ini карта %d: Use=%s ≠ реестр %s" % (m["id"], tindex.get(m["id"]), s.get("total_portal_use")))
    # MapWarpCost.txt
    wl = [ln.split() for ln in _read_text(os.path.join(gs, "MapData_Info", "MapWarpCost.txt")).splitlines() if ln.strip()]
    wn = int(wl[0][0])
    bad = [i for i, r in enumerate(wl[1:]) if len(r) != wn]
    if len(wl) - 1 != wn or bad:
        W.append("MapWarpCost.txt: N=%d, строк %d, строк не длины N: %d" % (wn, len(wl) - 1, len(bad)))
    if wn != nmaps:
        W.append("MapWarpCost.txt: матрица %d×%d, карт в реестре %d" % (wn, wn, nmaps))
    # ThreadInfo.ini — потоков хватит на открытые карты (Use=1 и тип 0/1/3/6)
    tpath = os.path.join(gs, "ConfigSet", "ThreadInfo.ini")
    if os.path.isfile(tpath):
        th = int((_ini_sections(_read_text(tpath)).get("MAX_INIT_THREAD") or {}).get("count", "0") or 0)
        opened = [mid for mid in open_secs if int(secs["MAP_%d" % mid].get("use", "0") or 0) == 1
                  and mid in rows and int(rows[mid][6]) in (0, 1, 3, 6)]
        if th < len(opened):
            W.append("ThreadInfo.ini: COUNT=%d < открытых карт %d — сервер выйдет при старте" % (th, len(opened)))
        else:
            I.append("ThreadInfo.ini: COUNT=%d, открытых карт с потоком %d" % (th, len(opened)))
    # Зоны переходов
    live, unknown = read_server_portal_edges(gs)
    for fn in unknown:
        I.append("PortalIP_Info\\%s: имя не найдено в MapInfo.txt" % fn)
    live_set = {tuple(e[1:]) for e in live}
    reg_set = {(e["from"], e["to"], e["x0"], e["z0"], e["x1"], e["z1"]) for e in reg["portal_edges"]}
    for e in sorted(reg_set - live_set):
        W.append("граф переходов: ребро %s есть в реестре, но нет на сервере" % (e,))
    for e in live:
        if tuple(e[1:]) not in reg_set:
            W.append("граф переходов: зона %s %s есть на сервере, но нет в реестре" % (e[0], e[1:]))
    for m in maps:
        pf = (m.get("server") or {}).get("portal_file")
        if pf and not os.path.isfile(os.path.join(gs, "PortalIP_Info", pf)):
            W.append("карта %d: нет PortalIP_Info\\%s" % (m["id"], pf))
    return E, W, I


def run_check(server=True, client=True, dump=False):
    """Полная сверка. Код возврата: 1 при ошибках."""
    reg = load_registry()
    E, W, I = check_registry(reg)
    if not E:
        E += check_generated(reg)
    for fn in (check_levels,) + ((check_client,) if client else ()) + ((check_server,) if server else ()):
        e, w, i = fn(reg)
        E += e
        W += w
        I += i
    if dump:
        W += check_client_dump(reg)
    for title, arr in (("СВЕДЕНИЯ", I), ("ПРЕДУПРЕЖДЕНИЯ", W), ("ОШИБКИ", E)):
        print("── %s (%d) ──" % (title, len(arr)))
        for x in arr:
            print("  " + x)
    print("ИТОГ: карт %d, ошибок %d, предупреждений %d" % (len(reg["maps"]), len(E), len(W)))
    return 1 if E else 0


# ══ Шаблон Tools/asda_maps.py (метки @@…@@ подставляет render_asda_maps_py) ═══════════════════════════════════
ASDA_MAPS_TEMPLATE = r'''# asda_maps.py — ЕДИНЫЙ список карт порта и его подмножества.
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

# --------------------------------------------------------------------- канон: @@N_ALL@@ карты
ALL_MAPS = [
@@ALL_MAPS@@
]

# Не карты (в ALL_MAPS не входят, задокументированы, чтобы не «чинили» обратно).
NON_MAPS = {
@@NON_MAPS@@
}

# --------------------------------------------------------------------- подмножества
# Закрытые/инстансные карты: небо и тени облаков к ним не применяются.
INDOOR_MAPS = @@INDOOR@@
OUTDOOR_MAPS = [m for m in ALL_MAPS if m not in INDOOR_MAPS]      # @@N_OUTDOOR@@ — купол неба, тени облаков

# Карты со спавнами мобов (есть .svr в GameServer\MonsterData_Info). @@N_MOB@@ шт: без @@MOB_EXCL_TEXT@@.
MAPS_WITH_MOB_SVR = [m for m in ALL_MAPS if m not in @@MOB_EXCL_TUPLE@@]

# Карты с НЕПУСТЫМ списком NPC. @@N_NPC@@ шт.
#   нет .npc вовсе: @@NPC_NONE_TEXT@@
#   .npc есть, но 0 записей: @@NPC_EMPTY_TEXT@@
MAPS_WITHOUT_NPC = @@NPC_WITHOUT@@
MAPS_WITH_NPC = [m for m in ALL_MAPS if m not in MAPS_WITHOUT_NPC]


# --------------------------------------------------------------------- главный цвет неба карты
# ★ 2026-08-02: таблица жила ТРЕМЯ дословными копиями (asda_rollout_sky_full, asda_add_volumetric_clouds,
#   asda_percloud_mics). Сверено: все три побайтово одинаковы, набор ключей = OUTDOOR_MAPS (18 карт).
#   Значение = среднее RGB текстуры SKY_DOME_CLOUDE1 карты (посчитано офлайн). Порядок ключей
#   сохранён от копий — по нему шёл обход раскатки (list(SKY_COLORS.keys())).
SKY_COLORS = {
@@SKY@@
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
'''


def main(argv=None):
    """Точка входа командной строки."""
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description="Реестр карт: генерация и сверка")
    ap.add_argument("--check", action="store_true", help="сверка со всеми слоями, ничего не пишет")
    ap.add_argument("--diff", action="store_true", help="показать разницу, ничего не писать")
    ap.add_argument("--no-server", action="store_true", help="без живого сервера")
    ap.add_argument("--no-client", action="store_true", help="без клиента")
    ap.add_argument("--dump", action="store_true", help="сверка с g_aMapEntityTable из @@ASTORY_DEV_ROOT_ESC@@\\RE\\dumps\\asda.dmp")
    ap.add_argument("--fill-sce-fog", action="store_true", help="поля sce_fog из клиентских .SCE → JSON (без генерации)")
    a = ap.parse_args(argv)
    if a.fill_sce_fog:
        n = fill_sce_fog()
        print("ИТОГ: sce_fog изменено %d" % n)
        return 0
    if a.check:
        return run_check(server=not a.no_server, client=not a.no_client, dump=a.dump)
    changed = generate(write=not a.diff, show_diff=a.diff)
    print("ИТОГ: %s %d файл(ов)" % ("изменилось бы" if a.diff else "записано", len(changed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
