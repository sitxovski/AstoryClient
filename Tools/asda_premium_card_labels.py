# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_premium_card_labels.py — подписи витрины 6000 (06.11.2026, решение владельца; Reports/premium_circle_bike).

1) Имена коробок: 76452/76453 носили имя «Сброс фракции аккаунта» (остаток старого клиентского имени кода 387; сервер и сток — камень
   подтипа 203, решение владельца 13.10), 76444–76447 сдвинуты на одну штуку от содержимого (пакеты 4611–4614 = ×2…×5).
2) Количество на карточке: ведомость `RawAssets/Data/NPC/PremiumShopBin_card_qty.json` {код: штук}, столбец CardQty DT_PremiumShopBin.

  python asda_premium_card_labels.py names [--dry]                  — рецепт имён, боевые CSV/JSON, EN-ведомости и EN-CSV (повтор безвреден)
  python asda_premium_card_labels.py qty --map M --items I --pkg P  — ведомость штук по снимку карты gp7, item_table и PackageItem_Table
Модуль без побочных эффектов при импорте.
"""
import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
ITEMDB = os.path.join(PROJ, "RawAssets", "Data", "ItemDB")
NPC = os.path.join(PROJ, "RawAssets", "Data", "NPC")
EN_CSV = os.path.join(PROJ, "RawAssets", "Text", "L10N", "en")
EN_LEDGER = os.path.join(HERE, "data", "loc", "en")
RECIPE = os.path.join(HERE, "_out", "itemdb_overlay", "item_name_overlay.csv")
QTY_JSON = os.path.join(NPC, "PremiumShopBin_card_qty.json")
SHOP_JSON = os.path.join(NPC, "PremiumShopBin.json")
WHY = "06.11.2026 решение владельца: имя коробки = содержимому (Reports/premium_circle_bike/REPORT_LABELS.md)"

# код: (_row, имя RU, имя EN)
NAMES = {
    76444: (55893, "Эликсир здоровья (100) x2", "HP Elixir(100)*2"),
    76445: (55894, "Эликсир здоровья (100) x3", "HP Elixir(100)*3"),
    76446: (55895, "Эликсир здоровья (100) x4", "HP Elixir(100)*4"),
    76447: (55896, "Эликсир здоровья (100) x5", "HP Elixir(100)*5"),
    76452: (55901, "Пустой камень улучшения (9+2)", "Empty Upgrade Stone(9+2)"),
    76453: (55902, "Пустой камень улучшения (9+2) x2", "Empty Upgrade Stone(9+2)*2"),
}


def read_rows(path):
    """CSV → строки-словари."""
    with open(path, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def cur_values(table, col):
    """Текущие значения ячейки col в боевой таблице по коду."""
    return {int(r["ItemCode"]): r[col] for r in read_rows(os.path.join(ITEMDB, table + ".csv")) if int(r["ItemCode"]) in NAMES}


def patch_recipe(cur_il, dry):
    """Рецепт: item_list — строки дописать, item_tooltip — new_value существующих строк заменить."""
    with open(RECIPE, encoding="utf-8-sig", newline="") as f:
        rows = list(csv.reader(f))
    head = rows[0]
    ix = {c: i for i, c in enumerate(head)}
    seen = set()
    n = 0
    for r in rows[1:]:
        if len(r) < len(head) or r[ix["table"]] not in ("item_list", "item_tooltip"):
            continue
        k = r[ix["key"]]
        if not k.isdigit() or int(k) not in NAMES or r[ix["column"]] not in ("ItemName", "Description"):
            continue
        seen.add((r[ix["table"]], int(k)))
        if r[ix["new_value"]] != NAMES[int(k)][1]:
            r[ix["new_value"]] = NAMES[int(k)][1]
            r[ix["note"]] = WHY
            n += 1
    for code, (_, ru, _en) in NAMES.items():
        if ("item_list", code) not in seen:
            row = [""] * len(head)
            row[ix["table"]], row[ix["key"]], row[ix["column"]] = "item_list", str(code), "ItemName"
            row[ix["old_value"]], row[ix["new_value"]] = cur_il[code], ru
            row[ix["source"]], row[ix["reason"]], row[ix["note"]] = "owner", "premium_labels", WHY
            rows.append(row)
            n += 1
    print("рецепт: правок %d" % n)
    if not dry and n:
        with open(RECIPE, "w", encoding="utf-8-sig", newline="") as f:
            csv.writer(f, lineterminator="\n").writerows(rows)


def apply_live(cur_il, cur_tip, dry):
    """Боевые CSV/JSON — штатным применителем по временному рецепту (его сторож: меняются только перечисленные ячейки)."""
    fd, tmp = tempfile.mkstemp(suffix=".csv", prefix="asda_labels_")
    os.close(fd)
    with open(RECIPE, encoding="utf-8-sig", newline="") as f:
        head = next(csv.reader(f))
    with open(tmp, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, head, lineterminator="\n")
        w.writeheader()
        for code, (_, ru, _en) in NAMES.items():
            for t, c, old in (("item_list", "ItemName", cur_il[code]), ("item_tooltip", "Description", cur_tip[code])):
                if old != ru:
                    w.writerow({"table": t, "key": code, "column": c, "old_value": old, "new_value": ru,
                                "source": "owner", "reason": "premium_labels", "note": WHY})
    cmd = [sys.executable, os.path.join(HERE, "asda_apply_name_overlay.py"), "--overlay", tmp, "--tables", "item_list,item_tooltip"]
    if dry:
        cmd.append("--dry-run")
    rc = subprocess.call(cmd)
    os.remove(tmp)
    if rc:
        raise SystemExit(rc)


def patch_en(dry):
    """EN: ведомость правок (её применяет extract_text_lang) и готовый CSV DT_Loc_*_en."""
    for t, col in (("item_list", "ItemName"), ("item_tooltip", "Description")):
        p = os.path.join(EN_CSV, "DT_Loc_%s_en.csv" % t)
        with open(p, encoding="utf-8-sig", newline="") as f:
            rows = list(csv.reader(f))
        want = {"%d.%s" % (v[0], col): v[2] for v in NAMES.values()}
        was = {}
        for r in rows[1:]:
            if r and r[0] in want and r[1] != want[r[0]]:
                was[r[0]] = r[1]
                r[1] = want[r[0]]
        lp = os.path.join(EN_LEDGER, t + ".json")
        with open(lp, encoding="utf-8") as f:
            led = json.load(f)
        have = {r["key"]: r for r in led["rows"]}
        for k, old in was.items():
            if k in have:
                have[k]["value"] = want[k]
                have[k]["why"] = WHY
            else:
                led["rows"].append({"key": k, "value": want[k], "was": old, "why": WHY, "source": "premium_labels"})
        print("EN %s: правок %d" % (t, len(was)))
        if not dry and was:
            with open(p, "w", encoding="utf-8-sig", newline="") as f:
                csv.writer(f, lineterminator="\n").writerows(rows)
            with open(lp, "w", encoding="utf-8") as f:
                json.dump(led, f, ensure_ascii=False, indent=1)


def cmd_names(a):
    """Имена коробок: рецепт, боевые таблицы, EN."""
    il, tip = cur_values("item_list", "ItemName"), cur_values("item_tooltip", "Description")
    patch_recipe(il, a.dry)
    apply_live(il, tip, a.dry)
    patch_en(a.dry)


def cmd_qty(a):
    """Ведомость штук: карточка функционального кода → коробка карты gp7 → сумма количества пакета."""
    with open(a.map, encoding="utf-8") as f:
        m = json.load(f)
    it = {}
    with open(a.items, encoding="cp1251", errors="replace") as f:
        for line in f:
            t = [x.strip() for x in line.rstrip("\r\n").split("\t")]
            if len(t) > 39 and t[0].isdigit():
                it[int(t[0])] = t
    pk = {}
    with open(a.pkg, encoding="cp1251") as f:
        for line in f.read().splitlines()[1:]:
            t = line.split("\t")
            pk[int(t[0])] = [(int(t[i]), int(t[i + 1])) for i in range(1, len(t) - 1, 2) if int(t[i]) > 0]
    with open(SHOP_JSON, encoding="utf-8") as f:
        shop = json.load(f)
    qty = {}
    gift = {}
    for r in shop:
        c = r["ItemId"]
        e = m.get(str(c))
        if r["Category"] != 3 and e and e["deliveryItemCode"] != c:
            gift[str(c)] = e["deliveryItemCode"]   # подарок = покупка себе: клиент шлёт в 5074 код выдачи (решение владельца 06.11)
        if r["Category"] == 3 or c > 637 or not e:
            continue
        t = it.get(e["deliveryItemCode"])
        if not t or t[16] != "11" or t[17] != "200":
            continue
        cont = pk.get(int(t[39]), [])
        if {x[0] for x in cont} == {c} and sum(x[1] for x in cont) > 1:
            qty[str(c)] = sum(x[1] for x in cont)
    print("карточек со штуками: %d, с кодом подарка: %d" % (len(qty), len(gift)))
    with open(QTY_JSON, "w", encoding="utf-8") as f:
        json.dump({"_comment": "Штук в коробке, которую выдаёт карточка витрины 6000 (карта gp7 + PackageItem_Table). "
                               "Столбцы CardQty и GiftCode (код выдачи для подарка) DT_PremiumShopBin; накладывает asda_import_premium_shop_bin.py. " + WHY, "qty": qty, "gift": gift},
                  f, ensure_ascii=False, indent=1)


def main(argv=None):
    """Разбор команд."""
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("names")
    p.add_argument("--dry", action="store_true")
    p = sub.add_parser("qty")
    p.add_argument("--map", required=True)
    p.add_argument("--items", required=True)
    p.add_argument("--pkg", required=True)
    a = ap.parse_args(argv)
    {"names": cmd_names, "qty": cmd_qty}[a.cmd](a)


if __name__ == "__main__":
    main()
