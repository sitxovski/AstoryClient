# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_callmob_dt.py — ПЕРЕЧИТЫВАНИЕ DT_CallMob С ДИСКА и сверка КАЖДОГО поля с сырыми
# байтами `Data/Mob/CallMobTable.BIN` и с серверным `MonsterData_Info/CallMobTable.txt`.
# Смысл: доказать, что ассет на диске совпал с оригиналом, а не поверить логу импортёра.
# Запуск: UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=<этот файл> -unattended -NullRHI
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуля НИЧЕГО не выполняется.
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_effect_mob_tables as T  # noqa: E402

DT_PATH = "/Game/AsdaAssets/Data/DT_CallMob"
import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
SRV_TXT = os.path.join(_P.MONSTER_DATA_INFO, "CallMobTable.txt")


def L(m):
    unreal.log_warning("[verify-callmob] %s" % m)


def snake(name):
    """UPROPERTY `NpcId` → python-имя `npc_id` (как их отдаёт рефлексия UE)."""
    out = ""
    for i, c in enumerate(name):
        if c.isupper() and i:
            out += "_"
        out += c.lower()
    return out


def read_server_txt(path):
    """Серверная копия той же таблицы: первое число — count, дальше по 10 чисел на запись."""
    if not os.path.isfile(path):
        return None
    toks = open(path, "r", encoding="latin1").read().split()
    n = int(toks[0])
    return [[int(x) for x in toks[1 + 10 * i:1 + 10 * (i + 1)]] for i in range(n)]


def main():
    raw = T.parse_call_mob()                     # сырьё .BIN, разобранное общим парсером
    srv = read_server_txt(SRV_TXT)
    cols = T.CALL_MOB_FIELDS + ["DataHex"]

    dt = unreal.EditorAssetLibrary.load_asset(DT_PATH)
    if not dt:
        L("!! ассет %s НЕ ЗАГРУЗИЛСЯ" % DT_PATH)
        return
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]
    L("ассет: %s" % DT_PATH)
    L("RowStruct: %s" % dt.get_editor_property("row_struct").get_name())
    L("строк в ассете: %d, записей в .BIN: %d" % (len(names), len(raw)))
    L("поля строки (%d): %s" % (len(cols), ", ".join(cols)))
    L("имена строк: %s" % ", ".join(names))

    # Значения ассета — поколоночно; порядок значений совпадает с порядком get_data_table_row_names.
    # ★ `GetDataTableColumnAsString` ждёт ИМЯ СВОЙСТВА КАК В C++ («NpcId»), а не python-имя
    #   («npc_id»): по python-имени он молча возвращает ПУСТОЙ список. Пробуем оба и проверяем длину.
    got = {}
    for f in cols:
        for nm in (f, snake(f)):
            v = [str(x) for x in
                 unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, nm)]
            if len(v) == len(names):
                got[f] = v
                break
        else:
            L("!! колонка %s из ассета не прочиталась" % f)
            got[f] = [""] * len(names)

    bad = 0
    for i in range(len(raw)):
        if str(i) not in names:
            L("!! строки '%d' в ассете нет" % i)
            bad += 1
            continue
        k = names.index(str(i))
        vals = {f: got[f][k] for f in cols}
        diff = [f for f in cols if vals[f] != str(raw[i][f])]
        L("строка %s: %s%s"
          % (i, ", ".join("%s=%s" % (f, vals[f]) for f in cols),
             "" if not diff else "   !! РАСХОЖДЕНИЕ %s (в .BIN %s)"
             % (diff, [raw[i][f] for f in diff])))
        bad += len(diff)

    # Сверка .BIN ↔ серверный .txt: первые 8 колонок должны совпадать запись в запись.
    if srv:
        srv_bad = 0
        for i, row in enumerate(srv):
            mine = [raw[i][f] for f in T.CALL_MOB_FIELDS]
            if mine != row[:8]:
                L("!! запись %d: .BIN %s != сервер %s" % (i, mine, row[:8]))
                srv_bad += 1
        L("сверка с серверным CallMobTable.txt: %d записей, расхождений %d" % (len(srv), srv_bad))
        bad += srv_bad
    else:
        L("серверный %s не найден — сверка только с .BIN" % SRV_TXT)

    L("ИТОГ: расхождений %d" % bad)


if __name__ == "__main__":
    main()
