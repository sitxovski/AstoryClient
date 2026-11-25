# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_upgrade_nongear_codes.py — ВЫГРУЗКА КОДОВ «СЕРВЕР НЕ СЧИТАЕТ СНАРЯЖЕНИЕМ» ДЛЯ ОКНА УЛУЧШЕНИЯ (30.10.2026).
#
# ЗАЧЕМ. Сервер точит только вещи, чей тип в СВОЕЙ `Item_Table.txt` (поле 16, байт +26) — 0…4, 8, 9 (`Recv_User_ShellItemUpgrade(22)`,
#   GameServer.c:98868-98905; иначе код 7 → «Предмет нельзя улучшить»). Клиентская `item_list` с серверной расходится: у части кодов
#   клиент показывает снаряжение, а сервер — заготовку/коробку/удочку (баг 30.10: пустые заготовки «-», удочки 69630/69631, тыквенные
#   головы 73346–73349). Окно улучшения (`UAsdaItemUpgradeSubsystem`, ключ `asda.Upgrade.GearOnlyGrid`) не должно их предлагать.
#   Авторитет — сервер (закон `client-server-table-desync`), поэтому набор берётся из СЕРВЕРНОЙ таблицы и кладётся ДАННЫМИ
#   в `Config/DefaultAsdaUpgrade.ini` (секция `[Upgrade.ServerNonGear]`), а не зашивается в C++.
#   В набор входят только коды, которые КЛИЕНТ считает снаряжением: остальное окно и так отсекает по клиентскому типу.
#
# ЗАПУСК (обычный python, unreal не нужен; сервер только читается):
#   python Tools/asda_export_upgrade_nongear_codes.py <путь к Item_Table.txt>           — перезаписать ini (с бэкапом .bak_<время>)
#   python Tools/asda_export_upgrade_nongear_codes.py <путь к Item_Table.txt> --check   — сверка, ничего не пишет; код 1 при расхождении
#   ★ После правки серверной item_table (серверные типы предметов) — прогнать с --check и при расхождении перевыгрузить.
import csv
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_server_txt as _ST  # noqa: E402  чтение .txt так, как читает сервер (atol, поток слов)

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
INI_PATH = os.path.join(ROOT, "Config", "DefaultAsdaUpgrade.ini")          # ini порта (Base → Default → платформа)
CLIENT_CSV = os.path.join(ROOT, "RawAssets", "Data", "ItemDB", "item_list.csv")   # клиентская item_list порта
SECTION = "Upgrade.ServerNonGear"   # секцию и ключ читает `UAsdaItemUpgradeSubsystem` (IsServerNonGearCode)
KEY = "Code"
ITEM_TABLE_WORDS = 42               # слов на запись Item_Table.txt (GameServer.c:68193-68309)
GEAR_TYPES = {0, 1, 2, 3, 4, 8, 9}  # маска заточки сервера: шлем, доспех, штаны, ботинки, перчатки, щит, оружие


def server_types(item_table_path):
    """code → серверный тип (поле 16) — так, как таблицу читает сервер."""
    _declared, recs, _ts = _ST.read_counted_records(item_table_path, ITEM_TABLE_WORDS)
    return {_ST.crt_atol(r[0]): _ST.as_u8(_ST.crt_atol(r[16])) for r in recs}


def client_gear_codes():
    """Коды, которые клиентская item_list считает снаряжением (ItemType 0…4, 8, 9)."""
    csv.field_size_limit(10 ** 9)
    with open(CLIENT_CSV, newline="", encoding="utf-8") as f:
        return {int(r["ItemCode"]) for r in csv.DictReader(f) if int(r["ItemType"]) in GEAR_TYPES}


def build_ini(codes, source):
    """Текст ini (CRLF): шапка-комментарий + секция с кодами."""
    lines = [
        "; Config/DefaultAsdaUpgrade.ini — ДАННЫЕ ОКНА УЛУЧШЕНИЯ (генерируется Tools/asda_export_upgrade_nongear_codes.py; руками не править).",
        ";",
        "; [Upgrade.ServerNonGear] — коды, которые КЛИЕНТ показывает снаряжением, а серверная Item_Table.txt — нет (поле 16 не 0…4/8/9):",
        ";   сервер их не точит (`Recv_User_ShellItemUpgrade(22)`, код 7). Читатель — `UAsdaItemUpgradeSubsystem` (ключ",
        ";   `asda.Upgrade.GearOnlyGrid`): такие вещи в сетку окна улучшения не попадают.",
        "; Источник: %s (%d кодов), выгрузка %s." % (source, len(codes), time.strftime("%d.%m.%Y %H:%M")),
        "",
        "[%s]" % SECTION,
    ]
    lines += ["+%s=%d" % (KEY, c) for c in codes]
    return "\r\n".join(lines) + "\r\n"


def read_ini_codes(path):
    """Коды из текущего ini (пусто — файла или секции нет)."""
    if not os.path.exists(path):
        return []
    codes, in_sec = [], False
    with open(path, "r", encoding="utf-8-sig") as f:
        for raw in f:
            s = raw.strip()
            if s.startswith("["):
                in_sec = (s == "[%s]" % SECTION)
                continue
            if in_sec and s.startswith("+%s=" % KEY):
                codes.append(int(s.split("=", 1)[1]))
    return sorted(set(codes))


def main(argv):
    if len(argv) < 2:
        print("использование: asda_export_upgrade_nongear_codes.py <Item_Table.txt> [--check]")
        return 2
    src = argv[1]
    st = server_types(src)
    codes = sorted(c for c in client_gear_codes() if c in st and st[c] not in GEAR_TYPES)
    have = read_ini_codes(INI_PATH)
    print("клиент — снаряжение, сервер — нет: %d кодов; в ini: %d" % (len(codes), len(have)))
    if "--check" in argv[2:]:
        extra, miss = sorted(set(have) - set(codes)), sorted(set(codes) - set(have))
        if extra or miss:
            print("РАСХОЖДЕНИЕ: лишние в ini %s; нет в ini %s" % (extra, miss))
            return 1
        print("совпадает")
        return 0
    if os.path.exists(INI_PATH):
        bak = INI_PATH + ".bak_" + time.strftime("%Y%m%d_%H%M%S")
        with open(INI_PATH, "rb") as a, open(bak, "wb") as b:
            b.write(a.read())
        print("бэкап: %s" % bak)
    with open(INI_PATH, "wb") as f:
        f.write(b"\xef\xbb\xbf" + build_ini(codes, os.path.basename(src)).encode("utf-8"))
    print("записано: %s" % os.path.normpath(INI_PATH))
    return 0


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    sys.exit(main(sys.argv))
