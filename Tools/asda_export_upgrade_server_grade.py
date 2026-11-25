# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_upgrade_server_grade.py — ВЫГРУЗКА СЕРВЕРНОГО ГРЕЙДА/ТИПА СНАРЯЖЕНИЯ ДЛЯ ФИЛЬТРА СВИТКОВ ОКНА УЛУЧШЕНИЯ (04.11.2026).
#
# ЗАЧЕМ. Окно 20000 (`uiCombineRefreshSlots@0xD28A80`, ui_core.c:48549-48580) показывает свиток, если грейд свитка
#   (`UpgradeTable.ReqTargetGrade`) равен 5 или КЛИЕНТСКОМУ грейду цели `pricetable.dw04` (`Table2Record_0[4]`), а класс
#   свитка подходит КЛИЕНТСКОМУ типу цели (`itemType`). Сервер (`CharShellItemUpgrade_0@0x4FEE50`, GameServer.c:98674…)
#   сверяет грейд свитка с СВОИМ грейдом цели — SowelCombine.txt, слово 46 (+150 записи 172 б; ветка «(2)» → 6505 код 3,
#   у игрока «Недостаточно купонов» 50007), кроме видов 74/84/91, а класс — по СВОЕМУ типу Item_Table.txt (слово 16, ветка
#   «(22)» → код 7). Клиентский и серверный грейд расходятся у 11 146 из 47 479 вещей (сток-сервер Donors/GameServer_stock —
#   так же: ДЕФЕКТ ОРИГИНАЛА): окно предлагает свитки, которые сервер отвергнет, и прячет годные. Серверного грейда 4 нет
#   ни у одной вещи — свитки ранга S (виды 79/89/96/101) сервер не принимает никогда.
#   Авторитет — сервер: расходящиеся коды кладутся ДАННЫМИ в `Config/DefaultAsdaUpgradeGrade.ini` (секция
#   `[Upgrade.ServerGrade]`, строка `+Item=<код>,<серверный грейд>,<серверный тип>`); читает `UAsdaItemUpgradeSubsystem`
#   (ключ `asda.Upgrade.ServerGradeFilter`). В набор входят только коды, которые КЛИЕНТ считает снаряжением и у которых
#   расходится грейд или класс (оружие/доспех/прочее); остальным клиентские поля и так равны серверным.
#
# ЗАПУСК (обычный python, unreal не нужен; сервер только читается — файлы забрать по scp, шару не трогать):
#   python Tools/asda_export_upgrade_server_grade.py <SowelCombine.txt> <Item_Table.txt>           — перезаписать ini (бэкап .bak_<время>)
#   python Tools/asda_export_upgrade_server_grade.py <SowelCombine.txt> <Item_Table.txt> --check   — сверка; код 1 при расхождении
#   ★ После правки серверных SowelCombine/Item_Table — прогнать с --check и при расхождении перевыгрузить.
import csv
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_server_txt as _ST  # noqa: E402  чтение .txt так, как читает сервер (atol, поток слов)

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
INI_PATH = os.path.join(ROOT, "Config", "DefaultAsdaUpgradeGrade.ini")            # ini порта
ITEM_CSV = os.path.join(ROOT, "RawAssets", "Data", "ItemDB", "item_list.csv")      # клиентская item_list
PRICE_CSV = os.path.join(ROOT, "RawAssets", "Data", "ItemDB", "pricetable.csv")    # клиентская pricetable (dw04 = грейд)
SECTION = "Upgrade.ServerGrade"   # секцию и ключ читает `UAsdaItemUpgradeSubsystem` (ServerGradeOf)
KEY = "Item"
SOWEL_WORDS = 57                  # слов на запись SowelCombine.txt (GameServer.c:65181-65250)
SOWEL_GRADE = 46                  # слово грейда (+150)
ITEM_TABLE_WORDS = 42             # слов на запись Item_Table.txt (GameServer.c:68193-68309)
ITEM_TABLE_TYPE = 16              # слово типа (+26)
GEAR_TYPES = {0, 1, 2, 3, 4, 8, 9}


def klass(t):
    """Класс заточки по типу: 1 — доспех (0…4, 8), 0 — оружие (9), −1 — не снаряжение (как ветка «(22)» сервера)."""
    return 1 if t in (0, 1, 2, 3, 4, 8) else 0 if t == 9 else -1


def server_tables(sowel_path, item_table_path):
    """code → серверный грейд (SowelCombine слово 46) и code → серверный тип (Item_Table слово 16) — как читает сервер."""
    _d, recs, _ts = _ST.read_counted_records(sowel_path, SOWEL_WORDS)
    grade = {_ST.crt_atol(r[0]): _ST.as_i16(_ST.crt_atol(r[SOWEL_GRADE])) for r in recs}
    _d, recs, _ts = _ST.read_counted_records(item_table_path, ITEM_TABLE_WORDS)
    typ = {_ST.crt_atol(r[0]): _ST.as_u8(_ST.crt_atol(r[ITEM_TABLE_TYPE])) for r in recs}
    return grade, typ


def client_tables():
    """code → (клиентский тип, клиентский грейд dw04) для снаряжения клиента."""
    csv.field_size_limit(10 ** 9)
    with open(PRICE_CSV, newline="", encoding="utf-8-sig") as f:
        dw04 = {int(r["ItemCode"]): int(r["dw04"]) for r in csv.DictReader(f)}
    out = {}
    with open(ITEM_CSV, newline="", encoding="utf-8-sig") as f:
        for r in csv.DictReader(f):
            c, t = int(r["ItemCode"]), int(r["ItemType"])
            if t in GEAR_TYPES:
                out[c] = (t, dw04.get(c, -99))
    return out


def build_rows(sowel_path, item_table_path):
    """Строки (код, серверный грейд, серверный тип) там, где грейд или класс клиента расходится с сервером."""
    grade, typ = server_tables(sowel_path, item_table_path)
    rows = []
    for c, (ct, cg) in sorted(client_tables().items()):
        if c not in grade or c not in typ:
            continue   # серверной записи нет — сервер такую вещь не точит; окно и так полагается на Д-18
        sg, st = grade[c], typ[c]
        if sg != cg or klass(st) != klass(ct):
            rows.append((c, sg, st))
    return rows


def build_ini(rows, sources):
    """Текст ini (CRLF): шапка-комментарий + секция."""
    lines = [
        "; Config/DefaultAsdaUpgradeGrade.ini — СЕРВЕРНЫЙ ГРЕЙД/ТИП СНАРЯЖЕНИЯ ДЛЯ ФИЛЬТРА СВИТКОВ ОКНА УЛУЧШЕНИЯ",
        ";   (генерируется Tools/asda_export_upgrade_server_grade.py; руками не править).",
        ";",
        "; [Upgrade.ServerGrade] +Item=<код>,<грейд сервера SowelCombine слово 46>,<тип сервера Item_Table слово 16> — только коды,",
        ";   у которых клиентский грейд pricetable.dw04 или класс клиентского типа расходится с сервером. Читатель —",
        ";   `UAsdaItemUpgradeSubsystem::IsScrollCompatible` (ключ `asda.Upgrade.ServerGradeFilter`, 0 = как оригинал).",
        "; Источник: %s (%d строк), выгрузка %s." % (", ".join(sources), len(rows), time.strftime("%d.%m.%Y %H:%M")),
        "",
        "[%s]" % SECTION,
    ]
    lines += ["+%s=%d,%d,%d" % (KEY, c, g, t) for c, g, t in rows]
    return "\r\n".join(lines) + "\r\n"


def read_ini_rows(path):
    """Строки из текущего ini (пусто — файла или секции нет)."""
    if not os.path.exists(path):
        return []
    rows, in_sec = [], False
    with open(path, "r", encoding="utf-8-sig") as f:
        for raw in f:
            s = raw.strip()
            if s.startswith("["):
                in_sec = (s == "[%s]" % SECTION)
                continue
            if in_sec and s.startswith("+%s=" % KEY):
                rows.append(tuple(int(x) for x in s.split("=", 1)[1].split(",")))
    return sorted(set(rows))


def main(argv):
    if len(argv) < 3:
        print("использование: asda_export_upgrade_server_grade.py <SowelCombine.txt> <Item_Table.txt> [--check]")
        return 2
    rows = build_rows(argv[1], argv[2])
    have = read_ini_rows(INI_PATH)
    print("снаряжение клиента с серверным грейдом/классом не как у клиента: %d; в ini: %d" % (len(rows), len(have)))
    if "--check" in argv[3:]:
        extra, miss = sorted(set(have) - set(rows)), sorted(set(rows) - set(have))
        if extra or miss:
            print("РАСХОЖДЕНИЕ: лишние в ini %d %s; нет в ini %d %s" % (len(extra), extra[:10], len(miss), miss[:10]))
            return 1
        print("совпадает")
        return 0
    if os.path.exists(INI_PATH):
        bak = INI_PATH + ".bak_" + time.strftime("%Y%m%d_%H%M%S")
        with open(INI_PATH, "rb") as a, open(bak, "wb") as b:
            b.write(a.read())
        print("бэкап: %s" % bak)
    with open(INI_PATH, "wb") as f:
        f.write(b"\xef\xbb\xbf" + build_ini(rows, [os.path.basename(argv[1]), os.path.basename(argv[2])]).encode("utf-8"))
    print("записано: %s" % os.path.normpath(INI_PATH))
    return 0


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    sys.exit(main(sys.argv))
