# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_map_zone_data.py — ЗОНОВЫЕ ДАННЫЕ КАРТ (BGM «город»/«поле» + текстуры названия локации)
#   в реестр карт `Reports/map_table.csv` (→ DT_MapTable, строка FAsdaMapRow).
#
# ИСТОЧНИКИ (1:1, без выдумок):
#  1. BGM — ЗАГОЛОВОК `<map>.TER` (magic 0x20050802). Заголовок = 0x2F6C байт, лежит в файле с офсета 12
#     и грузится в CMap как есть (эталон: getRecord32ByIndex@0x965FF0 читает CMap+12068/+12100 =
#     hdr+0x2F24/+0x2F44 = ФАЙЛ 0x2F30/0x2F50, по 32 байта ASCIIZ):
#       ФАЙЛ 0x2F30 (32б) — BGM СЛОТ 0 = ГОРОДСКОЙ  (играется при бите 0x80 атрибута тайла)
#       ФАЙЛ 0x2F50 (32б) — BGM СЛОТ 1 = ПОЛЕВОЙ
#       ФАЙЛ 0x2F70 (u32) — флаг «слот 0 заполнен» (эталонный g_bMapAmbientSound0)
#       ФАЙЛ 0x2F74 (u32) — флаг «слот 1 заполнен» (g_bMapAmbientSound1)
#     Эталон game_main.c:3391-3418 регистрирует непустые слоты в музыкальном устройстве
#     (sub_926601(dev, idx, name)) и играет CSoundStream_PlayById(dev, 0|1) по биту 0x80.
#  2. Текстуры названия локации — СВИТЧ Map_SetNameTextureByID@0xDA3700 (world_map.c:7825), ключ = mapId:
#     a2==0 → «городской» вариант (…_town), a2!=0 → обычный. Для карт без городского варианта свитч
#     возвращает ОДНУ И ТУ ЖЕ строку → в CSV городская колонка пуста (рантайм падает на обычную).
#     Файлы лежат в Data/NPC/Image (эталон TextureCache_GetOrReload("Data/NPC/Image", name)).
#
# ★ 14.09.2026 (реестр карт, волна 2): CSV больше НЕ правится этим скриптом — он генерируется из единого источника
#   `Tools/data/asda_map_registry.json`. Скрипт обновляет в реестре ТОЛЬКО поля bgm_town/bgm_field (из .TER + решения
#   владельца ниже) и перегенерирует файлы реестра (`Tools/asda_map_registry.py`). Текстуры названий — данные реестра.
# Запуск: python asda_export_map_zone_data.py   (обычный python, unreal НЕ нужен)
import os, csv, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P
import asda_ter_height as _TER   # ★ единый разбор .TER (были свои офсеты заголовка и своё чтение)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass

CSV_PATH = os.path.join(_P.RE_REPORTS, "map_table.csv")
TER_DIR  = _TER.TER_DIR

HDR_AT      = _TER.HDR_AT       # заголовок .TER начинается с офсета 12 (после magic/blobSize/tableSize)
OFF_BGM0    = _TER.OFF_BGM0     # внутри заголовка: городской трек
OFF_BGM1    = _TER.OFF_BGM1     # внутри заголовка: полевой трек
OFF_HAS0    = _TER.OFF_HAS0     # флаг наличия слота 0
OFF_HAS1    = _TER.OFF_HAS1     # флаг наличия слота 1

# Map_SetNameTextureByID@0xDA3700 — mapId → (обычная, городская). Пустая городская = городского варианта нет.
#   ★ С 14.09.2026 это КОПИЯ полей name_tex/name_tex_town реестра карт (генерируется), источник — JSON реестра.
# ── ★ ГЕНЕРИРУЕТСЯ Tools/asda_map_registry.py из Tools/data/asda_map_registry.json: NAME_TEX — НЕ ПРАВИТЬ РУКАМИ ──
NAME_TEX = {
    0:  ("name_silalis.tex",              "name_silalis_town.TEX"),
    1:  ("name_rein.TEX",                 "name_rein_town.TEX"),
    2:  ("name_conquest.TEX",             ""),
    3:  ("name_alphen.TEX",               "name_alphen_town.TEX"),
    4:  ("name_night_valley.TEX",         ""),
    5:  ("name_forgotten_canyon.TEX",     ""),
    6:  ("name_sunny_coast.TEX",          ""),
    7:  ("name_flammio.TEX",              "name_flammio_town.TEX"),
    8:  ("name_palace_of_queen.TEX",      ""),
    9:  ("name_castle_of_chess.TEX",      ""),
    10: ("name_disto_palace_of_queen.TEX",""),
    11: ("name_disto_castle_of_chess.TEX",""),
    12: ("name_Mountain Flamion.TEX",     ""),
    13: ("name_flabis.TEX",               ""),
    14: ("name_stagnant_desert.TEX",      ""),
    15: ("name_DragonLaiR.TEX",           ""),
    16: ("name_firewayforest.TEX",        ""),
    17: ("name_CastleOfChaos.TEX",        ""),
    18: ("name_Inferion.TEX",             ""),
    19: ("name_battlefield.TEX",          ""),
    20: ("name_eventfield.TEX",           ""),
    21: ("name_fieldofhonor.TEX",         ""),
    22: ("name_quizstage.TEX",            ""),
    23: ("name_mine.TEX",                 ""),
    100:("name_alphen.TEX",               ""),
}
# ── конец генерируемого блока NAME_TEX ──

NEW_COLS = ["BgmTown", "BgmField", "NameTex", "NameTexTown"]

# ── ★ ОСОЗНАННЫЕ ОТСТУПЛЕНИЯ ОТ .TER — РЕШЕНИЕ ВЛАДЕЛЬЦА (2026-08-02) ────────────────────────────
#   В оригинале слот с флагом наличия 0 = ТИШИНА в этой зоне (UAsdaZoneSubsystem::ResolveBgm вернёт
#   NAME_None → StopMusic). Владелец потребовал, чтобы на Inferion и в пустыне музыка звучала
#   И В ГОРОДЕ, И В ПОЛЕ — одна и та же. Правка делается ДАННЫМИ (эта таблица → CSV → DT_MapTable),
#   а НЕ кодом: строка DT_MapTable редактируется в редакторе UE, поведение подсистемы не меняется.
#   Имя берём РОВНО то же, что в заполненном слоте той же карты, — тогда ассет один, дублей нет.
#   КЛЮЧ: mapId → {"BgmTown"|"BgmField": имя ассета}. Применяется ТОЛЬКО когда слот пуст по .TER.
OWNER_BGM_OVERRIDES = {
    # Inferion: .TER слот 0 = S_BGM_Inferion (флаг 1), слот 1 = S_BGM_AlpeonField, но ФЛАГ 1 = 0 → тишина.
    18: {"BgmField": "S_BGM_Inferion"},
    # StagnantDesert: .TER слот 0 = S_BGM_NEW_Field (флаг 1), слот 1 = S_BGM_Alpeondgn_NightValley, ФЛАГ = 0.
    #   S_BGM_NEW_Field — ассет с пустынным треком (решение владельца 2026-08-01).
    14: {"BgmField": "S_BGM_NEW_Field"},
}


# 'S_BGM_AlpeonTown.mp3' → 'S_BGM_AlpeonTown'; 'name_alphen.TEX' → 'name_alphen' — общая реализация реестра карт
# (`asda_map_registry.strip_ext`; дедуп №2 В1 Т16: была дословная копия).
from asda_map_registry import strip_ext   # noqa: E402


def find_ter(dir_name, level_path):
    """Ищем <map>.TER: имя папки из CSV / из пути уровня (подчёркивания → пробелы), регистронезависимо."""
    if not os.path.isdir(TER_DIR):
        return None
    cands = []
    if dir_name: cands += [dir_name, dir_name.replace("_", " ")]
    if level_path:
        seg = level_path.strip("/").split("/")
        if len(seg) >= 3: cands += [seg[2], seg[2].replace("_", " ")]
    folders = {f.lower(): f for f in os.listdir(TER_DIR) if os.path.isdir(os.path.join(TER_DIR, f))}
    for c in cands:
        real = folders.get(c.lower())
        if not real: continue
        d = os.path.join(TER_DIR, real)
        for f in os.listdir(d):
            if f.lower().endswith(".ter"):
                return os.path.join(d, f)
    return None


def read_bgm(ter):
    """(town, field) из заголовка .TER; пусто, если флаг наличия слота = 0.

    Читается ТОЛЬКО шапка (HDR_AT + HDR_SIZE байт), как и прежде — тело в 10 МБ здесь не нужно.
    """
    with open(ter, "rb") as fh:
        b = fh.read(HDR_AT + _TER.HDR_SIZE)
    if len(b) < HDR_AT + _TER.HDR_SIZE:
        return "", ""
    try:
        t = _TER.TerFile(ter, data=b)           # разбор шапки поверх уже прочитанных байт
    except ValueError:
        return "", ""                           # не .TER — прежнее поведение «пусто»
    town, field, has0, has1 = t.bgm_slots()
    return (strip_ext(town) if has0 else "",
            strip_ext(field) if has1 else "")


def main():
    """BGM из заголовков .TER (+ решения владельца) → поля bgm_town/bgm_field реестра карт, затем перегенерация
    файлов реестра (map_table.csv → DT_MapTable и др.). Пути поиска .TER — прежние: DirName и путь уровня."""
    import asda_map_registry as _REG    # локально: реестр нужен только при запуске, не при импорте модуля
    reg = _REG.load_registry()
    print("%-4s %-18s %-30s %-30s %-24s %s" % ("id", "dir", "BGM город", "BGM поле", "название", "название(город)"))
    updates = {}
    for m in _REG.maps_by_id(reg):
        mid = m["id"]
        ter = find_ter(m["dir"], _REG.level_path(m))
        town = field = ""
        if ter:
            town, field = read_bgm(ter)
        else:
            print("  !! .TER не найден для mapId=%s dir=%s" % (mid, m["dir"]))
        # ★ Отступления владельца — ТОЛЬКО в пустой (флаг наличия = 0) слот, см. OWNER_BGM_OVERRIDES.
        ovr = OWNER_BGM_OVERRIDES.get(mid, {})
        if not town and ovr.get("BgmTown"):
            town = ovr["BgmTown"];  print("  ** override BgmTown  mapId=%d → %s" % (mid, town))
        if not field and ovr.get("BgmField"):
            field = ovr["BgmField"]; print("  ** override BgmField mapId=%d → %s" % (mid, field))
        updates[mid] = {"bgm_town": town, "bgm_field": field}
        nt = strip_ext(m["name_tex"]) if m["name_tex"] else ""
        ntt = strip_ext(m["name_tex_town"]) if m["name_tex_town"] else ""
        print("%-4s %-18s %-30s %-30s %-24s %s" % (mid, m["dir"], town or "—", field or "—", nt or "—", ntt or "—"))

    changed = _REG.update_map_fields(updates)
    print("\nРЕЕСТР: изменено полей BGM — %d (%d карт)" % (changed, len(updates)))
    _REG.generate()
    return 0


if __name__ == "__main__":
    sys.exit(main())
