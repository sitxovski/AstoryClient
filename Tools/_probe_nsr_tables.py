# Copyright (c) 2026 @sitxovski (Discord)
# _probe_nsr_tables.py — сверка ЗАЛИТЫХ DataTable против исходного `Nsr.bsz` (не против CSV/JSON).
#   Проверяется ровно то, чем пользуется подсистема диалога: ключи строк, подписи кнопок, коды
#   команд, текст страниц, подписи и состояния шагов, а также ModelName в DT_NpcQuestNpc.
# Запуск: Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_nsr_tables.py
import os as _os
import sys as _sys

_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import unreal
import asda_import_nsr_dialog as SRC


def L(m):
    unreal.log_warning("[nsr-check] %s" % m)


def main():
    files, headers, buttons, pages = SRC.read_all()
    dt_h = unreal.load_asset("/Game/AsdaAssets/Data/NPC/DT_NpcNsrHeader")
    dt_b = unreal.load_asset("/Game/AsdaAssets/Data/NPC/DT_NpcNsrButton")
    dt_p = unreal.load_asset("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage")
    dt_n = unreal.load_asset("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc")
    for tag, dt in (("DT_NpcNsrHeader", dt_h), ("DT_NpcNsrButton", dt_b),
                    ("DT_NpcNsrPage", dt_p), ("DT_NpcQuestNpc", dt_n)):
        if not dt:
            L("!! %s НЕ ЗАГРУЖЕН" % tag)
            return
        L("%s: строк=%d" % (tag, len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))))

    def names(dt):
        return set(str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))

    nh, nb, np_, nn = names(dt_h), names(dt_b), names(dt_p), names(dt_n)
    bad = 0
    for r in headers:
        if SRC.KEY_HEADER(r) not in nh:
            bad += 1
            L("!! нет строки шапки %s" % SRC.KEY_HEADER(r))
    for r in buttons:
        if SRC.KEY_BUTTON(r) not in nb:
            bad += 1
            L("!! нет строки кнопки %s" % SRC.KEY_BUTTON(r))
    for r in pages:
        if SRC.KEY_PAGE(r) not in np_:
            bad += 1
            L("!! нет строки страницы %s" % SRC.KEY_PAGE(r))
    L("ключи: шапки %d/%d, кнопки %d/%d, страницы %d/%d — промахов %d"
      % (len(nh), len(headers), len(nb), len(buttons), len(np_), len(pages), bad))

    # Содержимое: столбцы таблицы как строки (`get_data_table_column_as_string` — единственный
    #   доступный из Python экспорт значений; порядок совпадает с `get_data_table_row_names`).
    def col(dt, name):
        return [str(x) for x in unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, name)]

    def rows(dt):
        return [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]

    # ── кнопки: подпись и коды команд всех пяти под-записей ──
    rb = rows(dt_b)
    cap = dict(zip(rb, col(dt_b, "Caption")))
    subs = dict(zip(rb, col(dt_b, "Subs")))
    mism = 0
    for r in buttons:
        k = SRC.KEY_BUTTON(r)
        if cap.get(k) != r["Caption"]:
            mism += 1
            L("!! подпись %s: «%s» != «%s»" % (k, cap.get(k), r["Caption"]))
        blob = subs.get(k, "")
        for j, sb in enumerate(r["Subs"]):
            if sb["State"] == 0:
                continue
            need = "CmdCode=%d" % sb["CmdCode"]
            if need not in blob:
                mism += 1
                L("!! %s sub%d: в таблице нет %s" % (k, j, need))
            if "Param=%d" % sb["Param"] not in blob:
                mism += 1
                L("!! %s sub%d: в таблице нет Param=%d" % (k, j, sb["Param"]))
    L("кнопки: расхождений содержимого %d" % mism)

    # ── страницы: текст, число шагов, подписи шагов и состояния под-записей ──
    rp = rows(dt_p)
    txt = dict(zip(rp, col(dt_p, "Text")))
    stc = dict(zip(rp, col(dt_p, "StepCount")))
    stp = dict(zip(rp, col(dt_p, "Steps")))
    mism2 = 0
    shown = 0
    for r in pages:
        k = SRC.KEY_PAGE(r)
        if txt.get(k).replace("\t", "	").replace("\\", "") != r["Text"]:
            mism2 += 1
            if shown < 5:
                shown += 1
                L("!! текст %s разошёлся" % k)
        if stc.get(k) != str(r["StepCount"]):
            mism2 += 1
        # ★ `get_data_table_column_as_string` отдаёт значение в форме ImportText, где кавычка и
        #   апостроф ЭКРАНИРУЮТСЯ обратным слэшем. Сравниваем по тексту без экранирования, иначе
        #   61 подпись с апострофом («Victim's Voucher») даёт ЛОЖНОЕ расхождение.
        blob = stp.get(k, "").replace("\t", "	").replace("\\", "")
        for st in r["Steps"]:
            # ⚠ Подпись сверяем БЕЗ хвостовых пробельных символов: у `clark_5` в данных на конце
            #   стоит ТАБУЛЯЦИЯ, и экспортёр столбца отдаёт её как «	» — расхождение чисто
            #   диагностическое (в JSON, которым заливалась таблица, символ настоящий).
            capt_s = st["Caption"].strip()
            if capt_s and capt_s not in blob:
                mism2 += 1
                if shown < 8:
                    shown += 1
                    L("!! %s: подписи шага «%s» нет в таблице" % (k, st["Caption"]))
            for sb in st["Subs"]:
                if sb["State"] and ("State=%d" % sb["State"]) not in blob:
                    mism2 += 1
                    if shown < 12:
                        shown += 1
                        L("!! %s: состояния шага %d нет в таблице" % (k, sb["State"]))
    L("страницы: расхождений содержимого %d" % mism2)

    # ── ModelName в DT_NpcQuestNpc + наличие ассета портрета ──
    rn = rows(dt_n)
    mns = dict(zip(rn, col(dt_n, "ModelName")))
    ids = dict(zip(rn, col(dt_n, "NpcId")))
    miss = []
    empty = 0
    for k, mn in mns.items():
        if not mn:
            empty += 1
            continue
        if not unreal.EditorAssetLibrary.does_asset_exist("/Game/AsdaAssets/Textures/NPC/Portrait/%s" % mn):
            miss.append((ids.get(k), mn))
    L("ModelName: заполнено %d/%d; портрета-ассета нет у %d" % (len(mns) - empty, len(mns), len(miss)))
    L("примеры без портрета: %s" % (miss[:6],))

    # Контроль плана реверса.
    for key, capt, code in (("ian_0", "Sell Items", 30002), ("ian_1", "Buy Items", 30001),
                           ("ian_2", "Item Repair", 30069), ("rachel_0", "Store Items", 30011),
                           ("rachel_1", "Retrieve Items", 30012)):
        ok = (cap.get(key) == capt) and ("CmdCode=%d" % code in subs.get(key, ""))
        L("контроль %s: %s (подпись=«%s», код %d %s)"
          % (key, "OK" if ok else "ПРОВАЛ", cap.get(key), code,
             "найден" if ("CmdCode=%d" % code in subs.get(key, "")) else "НЕ найден"))
    L("ГОТОВО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
