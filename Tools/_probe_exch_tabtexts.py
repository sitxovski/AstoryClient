# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ Есть ли в таблице текста нотисы подписей вкладок аукциона (12520…12534, 98200/98201) и что
#   в них написано. Источник — `DT_GameText` (та же таблица, из которой читает `AsdaUi::LocRaw`).
import unreal

DT = "/Game/AsdaAssets/Text/DT_GameText.DT_GameText"
GROUPS = [
    ("13408 EXCHANGER_RADIOBTN1 — режим 5, 8 вкладок (uiOptionListboxInit, noticeIds[0..7])",
     list(range(12520, 12528))),
    ("13409 EXCHANGER_RADIOBTN2 — режим 4, 7 вкладок (noticeId 12528 + 7 шагов)",
     list(range(12528, 12535))),
    ("13260 EXCHMY_CateSelBtn — 2 пункта (CItemMediation_BuildUI)", [98200, 98201]),
    ("★ девятый элемент массива, циклом НЕ берётся (мёртвый)", [12775]),
]


def log(m):
    unreal.log("[тексты-вкладок] %s" % m)


def main():
    dt = unreal.EditorAssetLibrary.load_asset(DT)
    if not dt:
        log("НЕТ таблицы %s" % DT)
        return
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)]
    log("строк в DT_GameText: %d" % len(names))
    # Колонка текста как строки — порядок совпадает с порядком имён строк.
    col = None
    for field in ("text", "Text", "name"):
        try:
            col = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, unreal.Name(field))
            if col:
                log("колонка текста: %s" % field)
                break
        except Exception:                                    # noqa: BLE001
            col = None
    nameset = set(names)
    log("образец имён строк: %s" % ", ".join(names[:8]))
    log("★ проверка НАЛИЧИЯ строк (второй метод, без колонки): %s"
        % ", ".join("%d=%s" % (i, "ЕСТЬ" if str(i) in nameset else "нет")
                    for i in (12520, 12527, 12528, 12534, 12775, 98200, 98201, 12555)))
    table = dict(zip(names, col)) if col else {}
    for title, ids in GROUPS:
        log("--- %s ---" % title)
        for i in ids:
            key = str(i)
            log("   %d = %r" % (i, table.get(key, "СТРОКИ НЕТ")))


if __name__ == "__main__":
    main()
