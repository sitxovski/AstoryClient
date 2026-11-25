# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_quest_reward_table.py — `Data\NPC\RewardTable.BIN` → **DT_QuestRewardTable**.
#   Последний крупный неимпортированный источник данных системы наград за квесты.
#
# ══════════════════════════════════ ФОРМАТ ФАЙЛА ════════════════════════════════════════════════
# Загрузчик `questLoadRewardTableBin@0xC66190` (`quest.c:4007`), проверено дизассемблером бинаря:
#     fopen("./Data/NPC/RewardTable.BIN","rb")
#     fread(hdr, 0x14, 1)                      — общая шапка `.BIN`
#     strncmp(off_13E52F4, hdr+8, 4)           — тег; в файле "RTE\0"
#     count = hdr+4                            — 669 (было 636 до переноса Флэммио) (mov ecx,[ebp-0x24] при буфере [ebp-0x28])
#     new[]( 0x30 * count );  fread(arr, 0x30, count)
#     arr → CQuestMgr+0x544C, count → CQuestMgr+0x559C
#   КОНТРОЛЬ РАЗМЕРОМ: 20 + 48×669 = 32 132 = размер файла.
#
# ═════════════════════════ ЗАПИСЬ 48 БАЙТ — ЧЕМ ПОДТВЕРЖДЕНО ПОЛЕ ═══════════════════════════════
# Потребитель `questBuildRewardSet@0xC663A0` (`quest.c:4059`) перебирает ВСЕ записи и берёт те,
# у которых `+4 == questId` и `+8 == stage`. Роль остальных полей задаётся НЕ типом данных, а тем,
# в какой СЛОТ окна наград поле уходит: массив типов восьми слотов лежит по `CQuestMgr+0x557C`,
# и рисовалка `CQuest_RefreshRewardItemGrid@0xC677B0` (тождественно `CQuest_PopulateRewardWindow
# @0xC66A20`) читает по типу слота РАЗНОЕ поле. Слот — это 36 байт `+12…+47` записи, скопированные
# как есть: `qmemcpy(0x545C + 36*slot, запись+12, 0x24)`.
#
#   ★ Смещения чтения сняты ДИЗАССЕМБЛЕРОМ ИЗ БИНАРЯ (`imul eax,eax,0x24` + `mov edx,[ecx+eax+K]`),
#     а не из декомпила: типизация `aRewardSlotItems` в эталоне противоречива — по ней раскладка
#     не сходится (даёт невыровненные и выходящие за 48 байт адреса).
#
#   смещение | слот и тип                     | что читает рисовалка          | вывод
#   ---------+--------------------------------+-------------------------------+-------------------
#   +0       | —                              | —                             | номер записи 1…669
#   +4       | ключ поиска                    | —                             | QuestId
#   +8       | ключ поиска                    | —                             | Stage
#   +12      | слот 0, тип 0 → [0x545C+36i]   | "%d %s" с именем предмета     | ДЕНЬГИ
#            |                                | 20551 = «Золото»              |
#   +16      | слот 1, тип 1 → [0x5460+36i]   | "%d %s" с нотисом 6010,       | ОПЫТ
#            |                                | иконка "exp2"; нотис 6010 =    |
#            |                                | «Опыт» (Text/GameText.csv)    |
#   +20/+24  | слот 2, тип 2 → [0x5464/0x5468]| код + количество предмета     | предмет 1
#   +28/+32  | слот 3, тип 2 → [0x546C/0x5470]| код + количество предмета     | предмет 2
#   +36      | —                              | —                             | ФЛАГ «на выбор»
#   +40/+44  | слоты 4…7, тип 2               | код + количество предмета     | награда НА ВЫБОР
#            |   → [0x5478+36i]/[0x547C+36i]  |                               |
#
#   Ветка `if (a2 < 20000 || a2 > 30000)`: диапазон 20000…30000 в ЭТУ таблицу не идёт — там
#   `mapObjFindEntryById(g_pQuestCraftWnd, questId)`, окно ремесла.
#   Флаг `+36 != 0` дополнительно копирует блок `+12…+47` в очередной слот 4…7 (счётчик `v7`
#   растёт на КАЖДОЙ подходящей записи) и переключает пороги слотов 0…3 с `>= 0` на `> 0`.
#
# ═════════════════════════ ЧИСЛА ИЗ САМИХ ДАННЫХ (подтверждают чтение кода) ═════════════════════
#   +12  61 различное значение, ВСЕ круглые: 0, 1, 10, 50, 100, 200 … 92000, 120000  → деньги
#   +16  73 различных круглых: 0, 10, 20, 30 … 80000, 150000                          → опыт
#   +20 / +28 / +40  вместе 111 различных кодов; ВСЕ 111 находятся в таблицах предметов
#        порта — item_list (item), sowell_list (самоцвет), special_itemlist (спец-предмет), —
#        ровно в той цепочке, которую опрашивает рисовалка (`GetItemRecord` → `GetSowelRecord`
#        → `GetTable2Record`). Ненайденных нет.
#   +24 / +32 / +44  −1…20 — количества, не коды.
#   +36  только 0/1; 1 у 76 записей. У ВСЕХ 76 поле +40 заполнено, а +20 и +28 ровно −1 —
#        значит слоты 4…7 у них могут показать ТОЛЬКО +40/+44. Пара (QuestId 27, Stage 1) —
#        записи 134 и 135 с кодами 7732 и 7733: это и есть «выбери одну награду».
#
# ═════════════════════════ КЛЮЧ СТРОКИ ═════════════════════════════════════════════════════════
#   RecordIndex (`+0`): у 55 пар (QuestId, Stage) записей БОЛЬШЕ ОДНОЙ (эталон не делает break,
#   он складывает их в слоты 4…7), поэтому пара ключом быть не может. `+0` идёт сплошным рядом
#   1…669 и все значения различны.
#
# ⚠ Таблица НЕ подключается ни к попапу наград 540, ни к окну разговора: её потребитель в эталоне —
#   окно журнала квестов и приём пакета 5044, а этой системы в порту ещё нет. Здесь только ДАННЫЕ.
#
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_import_quest_reward_table.py
#   без UE:  python asda_import_quest_reward_table.py --csv-only
import json
import os
import struct
import sys as _sys
import os as _os

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

OUT_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "NPC")   # тот же каталог, что у asda_import_npc_tables
PKG = "/Game/AsdaAssets/Data/NPC"

DT_NAME = "DT_QuestRewardTable"
JSON_NAME = "QuestRewardTable.json"
ROW_STRUCT = "/Script/AsdaReborn.AsdaQuestRewardTableRow"

SRC = os.path.join(_P.DATA, r"NPC\RewardTable.BIN")

HDR = 0x14      # общая шапка `.BIN`: +0 версия, +4 count, +8 полей, +0xC тег, +0x10 ноль
TAG = "RTE"     # off_13E52F4, сверяется strncmp(...,4)
STRIDE = 0x30   # размер записи из `new[](0x30*count)` и `fread(...,0x30,count)`

# Колонки записи: (смещение, имя поля UPROPERTY). Порядок = порядок в файле.
COLUMNS = [
    (0,  "RecordIndex"),
    (4,  "QuestId"),
    (8,  "Stage"),
    (12, "Money"),
    (16, "Exp"),
    (20, "ItemCode1"),
    (24, "ItemCount1"),
    (28, "ItemCode2"),
    (32, "ItemCount2"),
    (36, "ChoiceFlag"),
    (40, "ChoiceItemCode"),
    (44, "ChoiceItemCount"),
]

I = lambda b, o: struct.unpack_from("<i", b, o)[0]


def log(msg):
    try:
        import unreal
        unreal.log_warning("[quest-reward] %s" % msg)
    except Exception:
        print("[quest-reward] %s" % msg)


def check(name, blob):
    """Проверка «тег + count + шаг» арифметикой РАЗМЕРА ФАЙЛА, а не на глаз."""
    got = blob[0x0C:0x0F].decode("ascii", "replace")
    count = I(blob, 4)
    need = HDR + STRIDE * count
    if got != TAG or need != len(blob):
        raise ValueError("%s: тег «%s» (ждали «%s»), count %d, шаг %d ⇒ ожидалось %d б, в файле %d"
                         % (name, got, TAG, count, STRIDE, need, len(blob)))
    log("%s: тег «%s», count %d, %d + %d×%d = %d = размер файла — OK"
        % (name, got, count, HDR, STRIDE, count, need))
    return HDR, count


def read_rewards(path=SRC):
    """Разбор `RewardTable.BIN` в список словарей — по одному на 48-байтовую запись."""
    b = open(path, "rb").read()
    base, n = check(os.path.basename(path), b)
    rows = []
    # ★ 01.11.2026 (item_dedup, этап 2): награды-дубли -> основной код (общий модуль asda_item_dedup, карта
    #   Tools/data/item_dedup_map.json). Здесь, а не в main(): сверка DT (`verify_dt(…, read_rewards())`) читает тем же
    #   путём. ASDA_NO_DEDUP_MAP=1 — коды как в RewardTable.BIN.
    import asda_item_dedup as _DD
    dmap = _DD.active_map()
    for i in range(n):
        o = base + STRIDE * i
        r = {name: I(b, o + off) for off, name in COLUMNS}
        _DD.remap_keys(r, ("ItemCode1", "ItemCode2", "ChoiceItemCode"), dmap)
        rows.append(r)
    return rows


# ── заливка ─────────────────────────────────────────────────────────────────────────────────────
def json_rows(rows):
    # * DEDUP D10: sborka JSON - obshchaya tochka `asda_dt_common.json_rows`.
    #   Klyuch stroki - sploshnoy nomer zapisi 1..669 (pole `RecordIndex`).
    from asda_dt_common import json_rows as _json_rows
    return _json_rows(rows, lambda r: str(r["RecordIndex"]))


def fill_dt(name, row_struct_path, json_text):
    # * DEDUP D10: sam shag "RowStruct -> DataTable -> zalivka -> sohranenie" na port ODIN
    #   (`asda_dt_common.fill_dt`). Zdes ostayotsya tolko svoyo: paket, logger, podskazka pro
    #   zagolovok C++ i sam obyekt tablicy.
    import unreal
    from asda_dt_common import fill_dt as _fill
    dt = _fill(unreal, PKG, name, row_struct_path, json_text, log,
               fmt="json", missing_hint="Public/Npc/AsdaNpcDialogTypes.h")
    return dt


def verify_dt(name, src_rows):
    """Сверка ПОЛЕ-В-ПОЛЕ: таблица читается ИЗ АССЕТА, источник разбирается ЗАНОВО с диска."""
    import unreal
    full = "%s/%s" % (PKG, name)
    dt = unreal.EditorAssetLibrary.load_asset(full)
    if not dt:
        log("!! сверка: ассета %s нет" % full)
        return 1
    got = {str(r.get("Name")): r
           for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))}
    bad = 0
    log("сверка %s: строк в ассете %d, записей в .BIN %d" % (name, len(got), len(src_rows)))
    if len(got) != len(src_rows):
        log("!! строк в ассете %d, а записей в файле %d" % (len(got), len(src_rows)))
        bad += 1
    for want in src_rows:
        key = str(want["RecordIndex"])
        have = got.get(key)
        if have is None:
            bad += 1
            if bad <= 15:
                log("!! нет строки %s" % key)
            continue
        for _off, col in COLUMNS:
            if have.get(col) != want[col]:
                bad += 1
                if bad <= 15:
                    log("!! строка %s поле %s: в ассете %r, в файле %r"
                        % (key, col, have.get(col), want[col]))
    return bad


def main(csv_only=False):
    os.makedirs(OUT_DIR, exist_ok=True)
    rows = read_rewards()
    text = json_rows(rows)
    with open(os.path.join(OUT_DIR, JSON_NAME), "w", encoding="utf-8", newline="") as f:
        f.write(text)
    log("%s: %d строк → %s" % (DT_NAME, len(rows), os.path.join(OUT_DIR, JSON_NAME)))

    # ── контрольные значения (взяты из разбора файла ВРУЧНУЮ, а не из этого же кода) ──
    bad = 0
    r1 = rows[0]
    ref1 = dict(RecordIndex=1, QuestId=0, Stage=0, Money=0, Exp=10, ItemCode1=-1, ItemCount1=0,
                ItemCode2=-1, ItemCount2=0, ChoiceFlag=0, ChoiceItemCode=-1, ChoiceItemCount=0)
    if any(r1[k] != v for k, v in ref1.items()):
        log("!! контроль записи 1 НЕ СОШЁЛСЯ: %s (ждали %s)" % (r1, ref1))
        bad += 1
    else:
        log("контроль записи 1: квест 0 шаг 0, денег 0, опыта 10, предметов нет — OK")

    r3 = rows[2]
    ref3 = dict(RecordIndex=3, QuestId=2, Stage=1, Money=400, Exp=150, ItemCode1=41782,
                ItemCount1=1, ItemCode2=73360, ItemCount2=1, ChoiceFlag=0)
    if any(r3[k] != v for k, v in ref3.items()):
        log("!! контроль записи 3 НЕ СОШЁЛСЯ: %s (ждали %s)" % (r3, ref3))
        bad += 1
    else:
        log("контроль записи 3: квест 2 шаг 1, 400 денег, 150 опыта, предметы 41782×1 и 73360×1 — OK")

    # Награда НА ВЫБОР: пара (27, 1) даёт ДВЕ записи с флагом 1 и разными кодами в +40.
    ch = [r for r in rows if r["QuestId"] == 27 and r["Stage"] == 1 and r["ChoiceFlag"]]
    codes = sorted(r["ChoiceItemCode"] for r in ch)
    if len(ch) != 2 or codes != [7732, 7733] or any(r["ItemCode1"] != -1 or r["ItemCode2"] != -1 for r in ch):
        log("!! контроль «награда на выбор» (27,1) НЕ СОШЁЛСЯ: %s" % ch)
        bad += 1
    else:
        log("контроль «награда на выбор» (квест 27, шаг 1): 2 записи, коды %s, "
            "обычные слоты пусты (−1) — OK" % codes)

    nflag = sum(1 for r in rows if r["ChoiceFlag"])
    nflag_ok = sum(1 for r in rows if r["ChoiceFlag"] and r["ChoiceItemCode"] > 0)
    if nflag != nflag_ok:
        log("!! записей с ChoiceFlag=1 %d, но с кодом в ChoiceItemCode только %d" % (nflag, nflag_ok))
        bad += 1
    else:
        log("контроль флага: у ВСЕХ %d записей с ChoiceFlag=1 заполнен ChoiceItemCode — OK" % nflag)

    if csv_only:
        log("СВЕРКА ИСТОЧНИКА: расхождений %d" % bad)
        return
    try:
        import unreal  # noqa: F401
    except Exception:
        log("UE недоступен — записан только JSON в %s" % OUT_DIR)
        log("СВЕРКА ИСТОЧНИКА: расхождений %d" % bad)
        return

    if fill_dt(DT_NAME, ROW_STRUCT, text) is None:
        log("СВЕРКА: расхождений %d (заливка не выполнена)" % (bad + 1))
        return
    bad += verify_dt(DT_NAME, read_rewards())
    log("СВЕРКА: расхождений %d" % bad)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main(csv_only=("--csv-only" in _sys.argv))
