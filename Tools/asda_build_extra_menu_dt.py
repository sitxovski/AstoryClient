# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_extra_menu_dt.py — DT_ExtraMenu: клиентская таблица окна «Прочее» (ETC_NEW_WIN 74600).
#
# Источник (скопирован из дистрибутива клиента владельца Donors\AsdaChroniclesInterlude_MINE\Data):
#   RawAssets/Data/Extra_menu_table.BIN — сигнатура "EMT", хедер 20 б, запись 116 б (0x74).
# Полная раскладка и чем она подтверждена — в шапке Public/Data/AsdaExtraMenuRow.h.
#
# RowStruct = FAsdaExtraMenuRow — ТРЕБУЕТ сборки C++ до запуска скрипта.
# Выход: RawAssets/Data/ExtraMenu.csv (ВНЕ Content!) и /Game/AsdaAssets/Data/DT_ExtraMenu
# Запуск: python Tools/run_headless_safe.py Tools/asda_build_extra_menu_dt.py --idle 200
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os as _os
import struct
import sys as _sys

import unreal

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_bintable as _BT     # noqa: E402  ★ дедуп №2 В1 Т5: общий разбор C-строк
import asda_paths as _P         # noqa: E402  ★ дедуп №2 В1 Т20: корень диска — asda_paths (ASDA_ROOT)
import asda_ue_paths as _U      # noqa: E402  ★ дедуп №2 В1 Т20: каталоги проекта и /Game — реестр путей

BIN = _os.path.join(_U.DATA_SRC, "Extra_menu_table.BIN")
# Таблица текста игры — из неё берётся ПОДПИСЬ восстановленного пункта (закон «данные из таблиц»).
GAMETEXT = _os.path.join(_U.RAW_TEXT, "GameText.csv")
# CSV кладём ВНЕ Content: любой .csv внутри Content редактор авто-импортирует и заводит двойника.
CSV = _os.path.join(_U.DATA_SRC, "ExtraMenu.csv")
OUT_PKG = _U.G_DATA
OUT_NAME = "DT_ExtraMenu"
ROW_STRUCT = "/Script/AsdaReborn.AsdaExtraMenuRow"
HDR = 20
REC = 116


def log(m):
    unreal.log_warning("[extra-menu-dt] %s" % m)


def cstr(b, o, n):
    """Строка char[n] с нулевым терминатором. Текст RU-сборки — cp1251 (общий `asda_bintable.cstr`)."""
    return _BT.cstr(b, o, n, "cp1251", "replace")


def csv_cell(s):
    """Экранирование для CSV DataTable: кавычки удваиваются."""
    return '"%s"' % s.replace('"', '""')


# ═══════════ ★★★ ВОССТАНОВЛЕНИЕ ВЫРЕЗАННОЙ ЗАПИСИ «ПОИСК ОТРЯДА» ═══════════════════════════════
# Что было в оригинале: в RU-сборке владельца файл `Extra_menu_table.BIN` содержит 11 записей
#   (1296 б = 20 + 11×116), а в корейской/глобальной сборках — 14 (1649 б = 20 + 14×116 + 5).
#   Три недостающие записи соответствуют трём МЁРТВЫМ строкам разметки `ETC_NEW_WIN`:
#   `ETC_NEW_Partyfind`, `ETC_NEW_Select_Premium`, `ETC_NEW_PremiumShop`.
# Чем доказано, что у вырезанной записи «Поиск отряда» были именно эти поля
#   (`Reports/extra_menu_partyfind.md` §1.2, §1.3, §2):
#   • `Idx = 2`  — подсказки доп-меню идут строго по порядку строк: 110925, 110926, **дырка**,
#     110928…110936. Единственная дырка ряда приходится на строку №2 (индекс с нуля);
#   • `DescLocId = 110927` — эта строка ЖИВА в `GameText.csv`: «Чтобы собрать отряд в выбранном
#     регионе, открой окно 'Поиск отряда'.»;
#   • `IconX/IconY = (2, 0)` — в атласе `Tools/NormalStudio/backup_originals/JUI/extra_menu.png`
#     (256×256, сетка 32×32) заполнены ровно 14 ячеек, а RU-таблица использует 11; ячейка (2,0) —
#     группа из четырёх персонажей, то есть именно «поиск отряда». Ячейки (4,0) и (5,0) —
#     премиальные пункты, это две другие мёртвые строки;
#   • `Cmd = 7` — в разборе команды `uiOpenWindowDispatch@0xECD570` таблица переходов `0xECD6B0`
#     имеет 12 входов (0…0xB), из них ровно ОДИН — код 7 — ведёт в метку `default`. Другого
#     свободного кода нет, а `ja` отсекает всё, что ≥ 12.
#   • `Name` — берётся ИЗ ДАННЫХ: текст 11800 «Поиск отряда» (он же заголовок окна 8500 и хинт
#     кнопки чата 12195). Корейский оригинал разметки — `파티찾기~` (`gamemain_jui.ini:351047`).
# ★ САМ ФАЙЛ `.BIN` НЕ ПРАВИМ — он вещественное доказательство. Запись подмешивается здесь,
#   при сборке `DT_ExtraMenu`. КЛЮЧ ОТКАТА: `RESTORE_PARTYFIND = False` возвращает ровно
#   ту таблицу, что лежит в файле (11 записей, пункта нет).
RESTORE_PARTYFIND = True
PARTYFIND_AT = 2          # позиция строки в меню (Idx исходной записи)
PARTYFIND_CMD = 7
PARTYFIND_ICON = (2, 0)
PARTYFIND_DESC = 110932   # ★ 09.10.2026: id ОРИГИНАЛА (ORIG «Open the “Find Party” window…»); прежнее 110927 — ряд Фантазиума

# ★★★ 09.10.2026 (мультиязычность, этап 0, решение владельца «RU по EN-оригиналу»): DescLocId — ИЗ ТАБЛИЦЫ ОРИГИНАЛА.
#   В RU-файле `Extra_menu_table.BIN` (из Фантазиума) подсказки ссылаются на 110925…110936, а в той же сборке EN-оригинала
#   (`Donors\AsdaChroniclesInterlude\Data\Extra_menu_table.BIN`, те же 11 записей, тот же Cmd) — на 110930…110941; по id 110925
#   у оригинала лежит текст бонуса Plus Package. Русские описания перенесены на id оригинала ведомостью
#   `Tools/data/gametext_overrides.json`. Сам RU-файл .BIN не правим (вещественное доказательство) — подсказка берётся
#   из записи оригинала с тем же Cmd. Откат: DESC_FROM_ORIG = False (и PARTYFIND_DESC = 110927).
DESC_FROM_ORIG = True
ORIG_BIN = _os.path.join(_P.DATA_ORIG, "Extra_menu_table.BIN")
PARTYFIND_NAME_LOC = 11800


def loc_text(loc_id):
    """Подпись пункта — ИЗ ТАБЛИЦЫ ТЕКСТА, а не хардкодом (закон «данные из таблиц»)."""
    import csv
    try:
        with open(GAMETEXT, "r", encoding="utf-8-sig", newline="") as f:
            for row in csv.reader(f):
                if row and row[0].strip() == str(loc_id):
                    return row[1]
    except OSError as e:
        log("не прочитать %s: %s" % (GAMETEXT, e))
    return ""


def main():
    with open(BIN, "rb") as f:
        data = f.read()
    sig = data[12:15]
    if sig != b"EMT":
        log("СИГНАТУРА НЕ 'EMT', а %r — файл не тот" % sig)
        print("ЗАВЕРШЕНО")
        return
    count_hdr = struct.unpack_from("<I", data, 4)[0]
    count_file = (len(data) - HDR) // REC
    if count_hdr != count_file:
        log("хедер обещает %d записей, а по размеру файла их %d — беру меньшее" % (count_hdr, count_file))
    count = min(count_hdr, count_file)
    if (len(data) - HDR) % REC:
        log("хвост %d б не кратен записи %d — файл повреждён" % ((len(data) - HDR) % REC, REC))

    # Читаем записи файла как есть.
    recs = []
    for i in range(count):
        o = HDR + REC * i
        recs.append({
            "name": cstr(data, o + 4, 64),
            "cmd": struct.unpack_from("<i", data, o + 68)[0],
            "atlas": cstr(data, o + 72, 32),
            "x": struct.unpack_from("<i", data, o + 104)[0],
            "y": struct.unpack_from("<i", data, o + 108)[0],
            "desc": struct.unpack_from("<i", data, o + 112)[0],
            "restored": False,
        })

    # ★ 09.10.2026: подсказки — по id оригинала (запись ORIG с тем же Cmd); нет записи — СТОП (не выдумывать).
    if DESC_FROM_ORIG:
        with open(ORIG_BIN, "rb") as f:
            od = f.read()
        ocount = min(struct.unpack_from("<I", od, 4)[0], (len(od) - HDR) // REC)
        odesc = {struct.unpack_from("<i", od, HDR + REC * i + 68)[0]: struct.unpack_from("<i", od, HDR + REC * i + 112)[0]
                 for i in range(ocount)}
        for r in recs:
            if r["cmd"] not in odesc:
                log("СТОП: в таблице оригинала нет записи cmd=%d — CSV/DT не пишутся" % r["cmd"])
                print("ЗАВЕРШЕНО")
                return
            r["desc"] = odesc[r["cmd"]]

    # ★ Возвращаем вырезанную запись «Поиск отряда» на её исходное место (обоснование — выше).
    if RESTORE_PARTYFIND:
        if any(r["cmd"] == PARTYFIND_CMD for r in recs):
            log("запись с cmd=%d уже есть в файле — восстановление не требуется" % PARTYFIND_CMD)
        else:
            name = loc_text(PARTYFIND_NAME_LOC)
            if not name:
                log("★ ТЕКСТ %d НЕ НАЙДЕН в GameText.csv — подпись пункта осталась бы пустой; "
                    "запись НЕ добавлена (закон «не выдумывать»)" % PARTYFIND_NAME_LOC)
            else:
                atlas = recs[0]["atlas"] if recs else "Extra_menu"
                recs.insert(min(PARTYFIND_AT, len(recs)), {
                    "name": name, "cmd": PARTYFIND_CMD, "atlas": atlas,
                    "x": PARTYFIND_ICON[0], "y": PARTYFIND_ICON[1], "desc": PARTYFIND_DESC,
                    "restored": True,
                })
                log("★ ВОССТАНОВЛЕНА запись «%s»: позиция %d, cmd=%d, иконка=(%d,%d), подсказка=%d"
                    % (name, PARTYFIND_AT, PARTYFIND_CMD, PARTYFIND_ICON[0], PARTYFIND_ICON[1],
                       PARTYFIND_DESC))

    lines = ["_row,Idx,Name,Cmd,IconAtlas,IconX,IconY,DescLocId,Restored"]
    for i, r in enumerate(recs):
        # ⚠ `Idx` — ПОРЯДКОВЫЙ НОМЕР записи; после вставки он перенумеровывается.
        # ★★★ КОЛОНКА `Restored` (наша, в файле её нет) помечает записи, которых в исходной
        #   `Extra_menu_table.BIN` не было. По ней подсистема считает «N-я строка ИСХОДНОЙ
        #   таблицы» и правильно разрешает СТАРЫЕ раскладки хотбара, где слот типа 5 ссылался
        #   на пункт порядковым номером. Новые раскладки хранят устойчивый `Cmd`.
        lines.append("%d,%d,%s,%d,%s,%d,%d,%d,%s" % (
            i, i, csv_cell(r["name"]), r["cmd"], csv_cell(r["atlas"]), r["x"], r["y"], r["desc"],
            "True" if r["restored"] else "False"))
        log("  %2d: cmd=%-3d иконка=(%d,%d) подсказка=%-6d «%s»%s" % (
            i, r["cmd"], r["x"], r["y"], r["desc"], r["name"],
            "   ← ВОССТАНОВЛЕНА" if r["restored"] else ""))

    with open(CSV, "w", encoding="utf-8-sig", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    log("CSV записан: %s (%d записей; в файле было %d)" % (CSV, len(recs), count))

    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    task = unreal.AssetImportTask()
    task.filename = CSV
    task.destination_path = OUT_PKG
    task.destination_name = OUT_NAME
    task.replace_existing = True
    task.automated = True
    task.save = True
    fac = unreal.CSVImportFactory()
    fac.automated_import_settings.import_row_struct = unreal.load_object(None, ROW_STRUCT)
    task.factory = fac
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    dt = unreal.load_asset(full)
    if not dt:
        log("DT НЕ СОЗДАН")
    else:
        names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
        log("DT_ExtraMenu: строк %d" % len(names))
        # ★ CompileAndSave НЕ сохраняет — сохраняем явно (закон из брифинга).
        unreal.EditorAssetLibrary.save_loaded_asset(dt)
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
