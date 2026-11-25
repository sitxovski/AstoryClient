# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_store_lotprice_codes.py — ВЫГРУЗКА КОДОВ «ЦЕНА ЛОТА» СЕРВЕРА ДЛЯ ЛИЧНОЙ ЛАВКИ (волна Л4/Л5 торговли, 30.09.2026).
#
# ЗАЧЕМ. Защита порта `asda.Trade.StoreBuyGuard` (личная лавка, покупка 6309) считает стоимость позиции ПО ПРАВИЛУ СЕРВЕРА
#   `CharSelfStoreBuyItem` (GameServer.c:100863-100886): у предмета `item_list` со штучным количеством сервер берёт ЦЕНУ ЛОТА,
#   если в СВОЕЙ `Item_Table.txt` у записи столбец +7 (EnchantOrSocket) == 4 И столбец +26 (ItemType) == 10; иначе — кол-во × цена.
#   Клиентская таблица `item_list` с серверной расходится (ревизия Л3, МЕЛК-1: 69682…69685 у клиента ItemType 11, у сервера 10).
#   Авторитет — сервер (закон `client-server-table-desync`), поэтому набор кодов правила берётся из СЕРВЕРНОЙ таблицы и кладётся
#   ДАННЫМИ в `Config/DefaultAsdaTrade.ini` (секция `[SelfStore.ServerLotPriceRule]`), а не зашивается в C++.
#
# ЗАПУСК (обычный python, unreal не нужен; сервер НЕ запускается, живой только читается):
#   python Tools/asda_export_store_lotprice_codes.py <путь к Item_Table.txt>           — перезаписать секцию (с бэкапом .bak_<время>)
#   python Tools/asda_export_store_lotprice_codes.py <путь к Item_Table.txt> --check   — сверка, ничего не пишет; код 1 при расхождении
#   ★ `--check` — пункт чек-листа правки серверных таблиц: `Docs/Port/systems/ITEM_DB_REFERENCE.md` §3 (ревизия Л4/Л5, МЕЛК-4).
#
# ФОРМАТ `Item_Table.txt` (cp1251): 1-я строка — число записей; далее по строке на запись, поля через TAB; поле 0 — код,
#   поле 3 — байт +7 (EnchantOrSocket), поле 16 — байт +26 (ItemType). Сверено 29.08/30.09 на живой копии: 58 878 записей, 35 кодов.
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_server_txt as _ST  # noqa: E402  ★ дедуп №2 В2 Т7: путь к живому серверу и чтение .txt как у сервера

# Путь к ini порта (относительно этого файла: Tools/.. = корень проекта).
INI_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Config", "DefaultAsdaTrade.ini")
# Имя секции и ключа — их же читает `UAsdaSelfStoreSubsystem::IsServerLotPriceCode`.
SECTION = "SelfStore.ServerLotPriceRule"
KEY = "Code"


# Слов на запись Item_Table.txt у загрузчика сервера (GameServer.c:68193-68309): 10 полей + 2×3 + 26 = 42.
ITEM_TABLE_WORDS = 42


def read_rule_codes(item_table_path):
    """Коды записей серверной Item_Table.txt с полем 3 == 4 и полем 16 == 10 (правило «цена лота» сервера).

    ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т7: таблица читается ТАК, КАК ЕЁ ЧИТАЕТ СЕРВЕР (`GameServer.c:68170-68312`):
      `fscanf("%d")` — число записей, затем ровно столько записей по 42 слова ПОТОКОМ (`fscanf("%s")`), каждое —
      `atol`; слово 0 → код (+0 DWORD, коды обязаны идти подряд), слово 3 → +7 BYTE, слово 16 → +26 BYTE.
      Было: СРАВНЕНИЕ СТРОК `fields[3] == "4"` по строкам и табам — «04», «4.0», « 4» сервер видит как 4, скрипт нет;
      строки короче 17 полей и с нецифровым кодом молча пропускались. На живой таблице 11.10.2026 набор тот же."""
    declared, recs, ts = _ST.read_counted_records(item_table_path, ITEM_TABLE_WORDS)
    codes, prev = [], None
    for r in recs:
        code = _ST.crt_atol(r[0])
        if prev is not None and prev + 1 != code:
            raise SystemExit("Item_Table.txt: код %d после %d — сервер остановит загрузку «Sync Error»" % (code, prev))
        prev = code
        if _ST.as_u8(_ST.crt_atol(r[3])) == 4 and _ST.as_u8(_ST.crt_atol(r[16])) == 10:
            codes.append(code)
    lossy = [w for r in recs for w in r if w and not _ST.is_full_int(w)]
    if lossy:
        print("⚠ Item_Table.txt: atol сервера усёк %d слов: %s" % (len(lossy), lossy[:10]))
    if ts.short:
        print("⚠ Item_Table.txt: слов меньше, чем обещает заголовок %d" % declared)
    if ts.rest():
        print("⚠ Item_Table.txt: после %d записей ещё %d слов — сервер их не читает" % (declared, len(ts.rest())))
    return sorted(set(codes))


def build_ini(codes, source):
    """Текст ini (CRLF при записи): шапка-комментарий + секция с кодами."""
    lines = [
        "; Config/DefaultAsdaTrade.ini — ДАННЫЕ ТОРГОВЛИ ПОРТА (генерируется Tools/asda_export_store_lotprice_codes.py; руками не править).",
        ";",
        "; [SelfStore.ServerLotPriceRule] — коды предметов, у которых СЕРВЕР при покупке в личной лавке (6309) берёт ЦЕНУ ЛОТА,",
        ";   а не кол-во × цена: `CharSelfStoreBuyItem` (GameServer.c:100863-100886), запись серверной Item_Table.txt +7 == 4 И +26 == 10.",
        ";   Читатель — `UAsdaSelfStoreSubsystem::IsServerLotPriceCode` (защита [ПОРТ] `asda.Trade.StoreBuyGuard`). Нет секции — правило",
        ";   по клиентской таблице item_list (EnchantOrSocket 4 и ItemType 10).",
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
        print(__doc__ or "использование: asda_export_store_lotprice_codes.py <Item_Table.txt> [--check]")
        return 2
    src = argv[1]
    check = "--check" in argv[2:]
    codes = read_rule_codes(src)
    have = read_ini_codes(INI_PATH)
    print("серверная таблица: %d кодов правила; в ini: %d" % (len(codes), len(have)))
    if check:
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


if __name__ == "__main__":
    sys.exit(main(sys.argv))
