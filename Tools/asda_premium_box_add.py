# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_premium_box_add.py — ОДНА КОМАНДА на «предмет ×N в премиум-магазин» коробкой (26.10.2026, первый набор — песок 20931
×100/×1000/×10000, решение владельца). Рецепт целиком — `Docs/Port/systems/PREMIUM_ITEM_ADD_FLOW.md`.

ПОЧЕМУ КОРОБКОЙ (реверс GameServer.c, проверено двумя путями):
  • Премиум-почта выдаёт предмет штучно: `Recv_PremiumItemToInven_Req` зовёт `sub_5340B0` с a4 = 0 — у стопочного предмета
    количество 0 или конфликт ячейки (памятка admin-give-items-gold). Поэтому стопку продаём КОРОБКОЙ: item_table
    ток.16 = 11 / ток.17 = 200, ток.39 = строка `PackageItem_Table.txt` (5 пар «код, кол-во»).
  • Вскрытие `Recv_OpenPackageItem@0x55A820` (GameServer.c:133374): количество пары — __int16 (+8 строки пакета, ≤ 32767);
    у стопочного предмета (item_table +7 = 4, +35 = 1, +36 = 1, +26 ≠ 10; ток.3/23/24/16) `sub_55B7F0` кладёт +9 = кол-во,
    +14 (вес стопки, i16) = вес единицы × кол-во; если стопка этого кода уже есть (`sub_4D6CE0` CheckEqualItem), клиент обязан
    прислать её ячейку (иначе код 7) — сервер ДОКЛАДЫВАЕТ в неё, отдельной стопки не бывает. БД `UPBundleItemUse`
    (a11_01_migrate.sql:4178): `Weight = Weight + @Weight1` в SMALLINT ⇒ стопка ≤ 32767 по весу; переполнение = ошибка SQL,
    откат всей транзакции (a11: XACT_ABORT + TRY), коробка не тратится.
  • Покупка: `CharChargeShopBuy@0x511DA0` (GameServer.c:104581) — код из item_table обязан стоять в `SellItemTable.txt`
    (заголовок «макс_код  число»: код ≥ макс+1 читается мимо массива/отказ загрузчика GameServer.c:68424), BuyCash = item_table
    ток.26 (+41). Шлюз gp7 (server.js resolvePremiumItem) для кода item_table карты не требует: цена = ток.26 той же таблицы.
  • Клиент: цена карточки = item_list.Price (+164), имя/подсказка = item_list / item_tooltip (+ EN-оверлеи), витрина 6000 =
    `PremiumShop.BIN` + хвост-ведомости (Category/Sub1/Sub2/NewFlag), иконка = строка item_img_table.

★ 02.11.2026 (баг «Зелье повышения уровня» 558 из премиум-магазина исчезает без повышения, Reports/levelup_potion):
  КОРОБКА ФУНКЦИОНАЛЬНОГО ПРЕДМЕТА (item < 20551, код Function_Table) под ПРЕЖНЕЙ позицией витрины. Витрина продаёт сам
  функциональный код (`catalog`, строки витрины не добавляются, SellItemTable не меняется), шлюз gp7 отдаёт коробку по
  карте `premium_item_map.json` (`catalog → {deliveryItemCode: коробка, price}`; прежнее значение ключа обязано равняться
  `map_was` — иначе отказ). Иконка коробки = иконка спецпредмета (special_itemlist IconPath/IconCell, IconSmallFlag 0 —
  как у коробок asda_fable_mounts_boxes.py). В пакете — (код, 1). Карта входит в манифест выкладки
  (`gp7-server\premium_item_map.json`; шлюз перечитывает её сам по изменению файла).

ЕДИНСТВЕННЫЙ ИСТОЧНИК — спецификация `Tools/data/premium_box_sets.json` (коды, состав, имена RU/EN, описание, ЦЕНА, витрина).
  Цену владелец меняет там (поле price) → `build` → `client-apply` → `dt-apply` → выкладка сервера.

КОМАНДЫ (без UE, кроме dt-apply):
  python asda_premium_box_add.py fetch-server                    — снимок 3 GameServer (stack_layout) + gp7 по SSH в srv_in
  python asda_premium_box_add.py add --group G --item C --qty 100,1000 --name-ru "Набор X {qty}" --name-en "X Package ({qty})"
         --desc-ru "…" --desc-en "…" --price 150,1350 [--cat 5 --sub1 0 --sub2 0 --newflag 0] [--icon-from C]
         [--catalog 558 --map-was 76405:5999]                    — функциональный предмет под прежней позицией витрины
                                                                 — выделить свободные коды и дописать набор в спецификацию
  python asda_premium_box_add.py build                           — ведомости клиента (хвосты ItemDB, EN, витрина) + srv_out + стенд
  python asda_premium_box_add.py verify                          — стенд по srv_out (загрузчики, гейты, покупка, вскрытие)
  python asda_premium_box_add.py client-apply --backup DIR       — декод в боевые CSV (только +новые строки), EN-оверлеи, витрина
                                                                   JSON, задание точечной заливки DT (DIR/dt_point_jobs.json)
  python asda_premium_box_add.py dt-apply --backup DIR [--dry]   — точечная заливка 7 DT (сторож ручной правки)
  python asda_premium_box_add.py gw-stand                        — стенд шлюза gp7 (живой server.js из снимка + srv_out):
                                                                   цена по одной, отказ при чужой цене, корзина CRT8
  python asda_premium_box_add.py verify-live                     — перечитать стойку по SSH и прогнать стенд
  Выкладка сервера — `AstoryServer/Patches/AsdaPatch/tools/deploy_premium_boxes.py` (через restart_stack.py, координатор).
Шару \\@@ASTORY_SERVER_HOSTNAME@@ не трогает. Модуль без побочных эффектов при импорте.
"""
import argparse
import csv
import datetime
import hashlib
import io
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))                                  # AstoryClient\Tools
sys.path.insert(0, HERE)
import asda_paths as _P                                                            # noqa: E402

PORT = _P.PORT_DIR                                                                 # @@ASTORY_DEV_ROOT@@\port
UPROJ = os.path.join(PORT, "AstoryClient")                                           # проект UE
ITEMDB_LIVE = os.path.join(UPROJ, "RawAssets", "Data", "ItemDB")                  # боевые CSV ItemDB
TAIL_DIR = os.path.join(UPROJ, "RawAssets", "ItemDB", "_tail_append")             # ведомости хвоста decode_item_db
EN_LIVE = os.path.join(UPROJ, "RawAssets", "Text", "L10N", "en")                  # боевые EN-оверлеи (CSV DT)
LOC_EN = os.path.join(HERE, "data", "loc", "en")                                  # ведомости EN
NPC_DIR = os.path.join(UPROJ, "RawAssets", "Data", "NPC")                         # витрина 6000 (JSON)
SHOP_JSON = os.path.join(NPC_DIR, "PremiumShopBin.json")                          # витрина = архив + хвосты
SHOP_TAIL = os.path.join(NPC_DIR, "PremiumShopBin_tail_boxes.json")               # хвост витрины ЭТОГО генератора
SPEC = os.path.join(HERE, "data", "premium_box_sets.json")                        # спецификация наборов (источник правды)
DECODER = os.path.join(PORT, "AstoryClient", "Tools", "ItemDB", "decode_item_db.py")              # декодер ItemDB
SRV_TOOLS = os.path.join(PORT, "AstoryServer", "Patches", "AsdaPatch", "tools")         # серверные инструменты
SRV_DIR = os.path.join(SRV_TOOLS, "data_premium_boxes")                           # снимок/выход/манифест
SRV_IN = os.path.join(SRV_DIR, "srv_in")                                          # снимок стойки до правки
SRV_OUT = os.path.join(SRV_DIR, "srv_out")                                        # файлы для выкладки
MANIFEST = os.path.join(SRV_DIR, "manifest.json")                                 # путь → md5 базы / нового (для deploy)
SRC_PREFIX = "premium_box:"                                                       # метка строк генератора в ведомостях

ITEM_FIRST = 20551                    # g_ItemCodeFirst / sub_532A90
TPL_CODE = 76467                      # образец коробки (как у asda_fable_mounts_boxes.py): 11/200, CategoryFlag 3, проверенный путь
SRV_TABLES = ("item_table.txt", "SowelCombine.txt", "PackageItem_Table.txt", "SellItemTable.txt")
SSH_KEY = r"C:\Users\@@ASTORY_DEV_USER@@\.ssh\@@ASTORY_SSH_KEY_NAME@@"
SSH_HOST = "Administrator@@@ASTORY_SERVER_VPN_IP@@"
SRV_ROOT = "@@ASTORY_SERVER_ROOT_FWD@@/Working_ASDA/v2"
I16_MAX = 32767                        # кол-во пары пакета (__int16) и вес стопки (i16 / SMALLINT БД)

DT = {   # DT-ассет → (новый CSV, имя файла в бэкапе, ось «строк»: ключ строки DT)
    "item_list": "/Game/AsdaAssets/Data/ItemDB/DT_item_list",
    "item_img_table": "/Game/AsdaAssets/Data/ItemDB/DT_item_img_table",
    "packagetable": "/Game/AsdaAssets/Data/ItemDB/DT_packagetable",
    "item_tooltip": "/Game/AsdaAssets/Data/ItemDB/DT_item_tooltip",
    "en_item_list": "/Game/AsdaAssets/Text/L10N/en/DT_Loc_item_list_en",
    "en_item_tooltip": "/Game/AsdaAssets/Text/L10N/en/DT_Loc_item_tooltip_en",
    "shop": "/Game/AsdaAssets/Data/NPC/DT_PremiumShopBin",
}


def out(*a):
    """Печать, устойчивая к консоли cp1251."""
    s = " ".join(str(x) for x in a)
    try:
        print(s, flush=True)
    except UnicodeEncodeError:
        print(s.encode("cp1251", "replace").decode("cp1251"), flush=True)


def md5f(path):
    """md5 файла."""
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def load_csv(path):
    """CSV → список словарей (UTF-8, с BOM или без)."""
    csv.field_size_limit(1 << 30)
    with open(path, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def load_spec():
    """Спецификация наборов (пустая, если файла нет)."""
    if not os.path.isfile(SPEC):
        return {"_comment": "", "boxes": []}
    return json.load(open(SPEC, encoding="utf-8"))


def save_spec(doc):
    """Записать спецификацию (UTF-8, indent 1, LF)."""
    with open(SPEC, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
        f.write("\n")


def decoder():
    """Модуль decode_item_db (реестр колонок и rd())."""
    sys.path.insert(0, os.path.dirname(DECODER))
    import decode_item_db as D   # noqa: E402
    return D


def registry(D):
    """Реестр таблиц decode_item_db."""
    return D.REGISTRY


def stack_layout():
    """Единый состав стойки (3 GameServer) — AstoryServer/Patches/AsdaPatch/tools/stack_layout.py."""
    sys.path.insert(0, SRV_TOOLS)
    import stack_layout as SL   # noqa: E402
    return SL


# ── серверные таблицы ──────────────────────────────────────────────────────────────────────────────
def read_srv(path):
    """(счёт 1-й строки, строки тела без пустых) — item_table / SowelCombine / PackageItem_Table (CRLF)."""
    raw = open(path, "rb").read()
    lines = raw.split(b"\r\n")
    return int(lines[0].split()[0]), [ln for ln in lines[1:] if ln.strip()]


def write_srv(path, count, body):
    """Записать таблицу: счёт + строки, CRLF (как у стока)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(str(count).encode("ascii") + b"\r\n")
        for ln in body:
            f.write(ln + b"\r\n")


def read_sell(path):
    """SellItemTable.txt → (макс_код, число из заголовка, список кодов по порядку)."""
    raw = open(path, "rb").read().split(b"\r\n")
    head = raw[0].split()
    return int(head[0]), int(head[1]), [int(x) for x in raw[1:] if x.strip()]


def write_sell(path, maxcode, codes):
    """SellItemTable.txt: «макс_код<TAB>число», затем код на строку (CRLF, как у стока)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(("%d\t%d\r\n" % (maxcode, len(codes))).encode("ascii"))
        for c in codes:
            f.write(("%d\r\n" % c).encode("ascii"))


def toks(line):
    """Токены строки серверной таблицы."""
    return line.decode("latin1").split()


def gs_dirs():
    """Папки GameServer стойки (stack_layout.GS_FOLDERS)."""
    return list(stack_layout().GS_FOLDERS)


# ── SSH ────────────────────────────────────────────────────────────────────────────────────────────
def _ssh(cmd, timeout=120):
    """Команда на стойке по SSH (только чтение)."""
    return subprocess.run(["ssh", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", SSH_HOST, cmd],
                          capture_output=True, text=True, timeout=timeout)


def _scp_get(rel, dst):
    """Скачать v2/<rel> в dst со сверкой md5 со стойкой."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    r = subprocess.run(["scp", "-q", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15",
                        "%s:%s/%s" % (SSH_HOST, SRV_ROOT, rel), dst], capture_output=True, text=True, timeout=600)
    if r.returncode:
        raise SystemExit("scp %s: %s" % (rel, r.stderr.strip()))
    rm = _ssh('powershell -NoProfile -Command "(Get-FileHash \'%s/%s\' -Algorithm MD5).Hash"' % (SRV_ROOT, rel)).stdout.strip().lower()
    if rm != md5f(dst):
        raise SystemExit("md5 снимка %s: локально %s, на стойке %s" % (rel, md5f(dst), rm))
    return rm


def srv_rels():
    """(путь от v2, локальный путь в снимке) — 4 таблицы × 3 GameServer + Function_Table + карта и шлюз gp7 (только чтение)."""
    rels = [(d + "/ItemTable_Info/" + t, os.path.join(d, t)) for d in gs_dirs() for t in SRV_TABLES]
    rels += [("GameServer/ItemTable_Info/Function_Table.txt", os.path.join("GameServer", "Function_Table.txt")),
             ("gp7-server/premium_item_map.json", os.path.join("gp7-server", "premium_item_map.json")),
             ("gp7-server/server.js", os.path.join("gp7-server", "server.js"))]
    return rels


def fetch_server(base=SRV_IN):
    """Снимок стойки по SSH (scp + сверка md5)."""
    for rel, loc in srv_rels():
        h = _scp_get(rel, os.path.join(base, loc))
        out("снимок %-55s %s" % (rel, h))


# ── выделение кодов ────────────────────────────────────────────────────────────────────────────────
def next_free(spec):
    """Следующие свободные (код предмета, строка пакета, ключ связи) — общие для клиента и сервера.
    Клиент: item_list.csv / packagetable.csv; сервер: снимок item_table / PackageItem_Table; плюс уже выделенные в спецификации.
    Расхождение клиента и сервера по хвосту — отказ (выделять вслепую нельзя)."""
    items = load_csv(os.path.join(ITEMDB_LIVE, "item_list.csv"))
    pkgs = load_csv(os.path.join(ITEMDB_LIVE, "packagetable.csv"))
    it_cnt, it_body = read_srv(os.path.join(SRV_IN, "GameServer", "item_table.txt"))
    pk_cnt, _ = read_srv(os.path.join(SRV_IN, "GameServer", "PackageItem_Table.txt"))
    cli_max = max(int(r["ItemCode"]) for r in items)
    srv_max = it_cnt + ITEM_FIRST - 1
    boxes = spec["boxes"]
    sp_max = max([b["code"] for b in boxes] or [0])
    if cli_max != srv_max and sp_max < max(cli_max, srv_max):
        raise SystemExit("ОТКАЗ: последний код клиента %d ≠ сервера %d — хвосты разошлись, выделять коды нельзя" % (cli_max, srv_max))
    code = max(cli_max, srv_max, sp_max) + 1
    if len(pkgs) != pk_cnt and max([b["package"] for b in boxes] or [-1]) < max(len(pkgs), pk_cnt) - 1:
        raise SystemExit("ОТКАЗ: пакетов у клиента %d ≠ у сервера %d" % (len(pkgs), pk_cnt))
    pkg = max(len(pkgs), pk_cnt, max([b["package"] + 1 for b in boxes] or [0]))
    link = max(max(int(toks(ln)[40]) for ln in it_body), max(int(r["LinkWordCode"]) for r in items),
               max([b["link"] for b in boxes] or [0])) + 1
    return code, pkg, link


def cmd_add(a):
    """Дописать набор в спецификацию (коды выделяются здесь один раз и дальше не меняются)."""
    spec = load_spec()
    if any(b["group"] == a.group for b in spec["boxes"]):
        raise SystemExit("ОТКАЗ: группа %s уже есть в %s — правь её поля там (цена, имена), коды не перевыделяются" % (a.group, SPEC))
    qtys = [int(x) for x in a.qty.split(",")]
    prices = [int(x) for x in a.price.split(",")]
    if len(prices) != len(qtys):
        raise SystemExit("ОТКАЗ: цен %d, количеств %d" % (len(prices), len(qtys)))
    if a.catalog and (len(qtys) != 1 or qtys[0] != 1 or a.item >= ITEM_FIRST or not a.map_was):
        raise SystemExit("ОТКАЗ: --catalog — только функциональный предмет ×1 и с --map-was <коробка>:<цена>")
    code, pkg, link = next_free(spec)
    for k, (q, p) in enumerate(zip(qtys, prices)):
        box = {
            "group": a.group, "code": code + k, "package": pkg + k, "link": link + k,
            "item": a.item, "qty": q, "price": p, "icon_from": a.icon_from or a.item,
            "name_ru": a.name_ru.format(qty=q), "name_en": a.name_en.format(qty=q),
            "desc_ru": a.desc_ru.format(qty=q), "desc_en": a.desc_en.format(qty=q),
            "shop": {"Category": a.cat, "Sub1": a.sub1, "Sub2": a.sub2, "NewFlag": a.newflag},
            "why": a.why or ("%s решение владельца: %s в премиум-магазин" % (datetime.date.today().strftime("%d.%m.%Y"), a.group)),
        }
        if a.catalog:   # прежняя позиция витрины: строки витрины нет, шлюз ведёт catalog → коробка
            was_code, was_price = (int(x) for x in a.map_was.split(":"))
            box.update({"shop": None, "catalog": a.catalog,
                        "map_was": {"deliveryItemCode": was_code, "price": was_price}})
        spec["boxes"].append(box)
        out("  +", code + k, "пакет", pkg + k, "ключ", link + k, "—", a.name_ru.format(qty=q), "/", a.name_en.format(qty=q), "цена", p)
    # Метка выкладки (суффикс бэкапов стойки .bak_<tag>) — по последнему добавленному набору.
    spec["tag"] = "%s_%s" % (datetime.date.today().strftime("%Y%m%d"), a.group)
    spec["_comment"] = ("Премиум-коробки «предмет ×N» (Tools/asda_premium_box_add.py; рецепт Docs/Port/systems/"
                        "PREMIUM_ITEM_ADD_FLOW.md). ЕДИНСТВЕННЫЙ источник: коды (не менять после выкладки), состав, имена "
                        "RU/EN, описание, ЦЕНА GXP (price → item_list.Price клиента и item_table ток.26 сервера), витрина 6000.")
    save_spec(spec)
    out("спецификация:", SPEC)


# ── построение ─────────────────────────────────────────────────────────────────────────────────────
def box_pairs(b):
    """Пары пакета: `split` — нестопочный предмет, по штуке в позиции (как коробка 76460: 404 ×1, 404 ×1), иначе одна пара ×qty."""
    return [(b["item"], 1)] * b["qty"] if b.get("split") else [(b["item"], b["qty"])]


def is_func(b):
    """В коробке — функциональный предмет (код < 20551: special_itemlist клиента / Function_Table сервера)."""
    return b["item"] < ITEM_FIRST


def map_out_path(base=None):
    """Карта шлюза для выкладки (srv_out), если её строил build; иначе снимок стойки."""
    p = os.path.join(base or SRV_OUT, "gp7-server", "premium_item_map.json")
    return p if os.path.isfile(p) else os.path.join(SRV_IN, "gp7-server", "premium_item_map.json")


def put_str(b, off, n, s):
    """C-строка cp1251 в поле n байт (с нулём)."""
    raw = s.encode("cp1251")
    if len(raw) >= n:
        raise SystemExit("строка «%s» длиннее поля %d" % (s, n))
    b[off:off + n] = raw + b"\0" * (n - len(raw))


def ours(r, key="_source"):
    """Строка ведомости — этого генератора."""
    return str(r.get(key) or "").startswith(SRC_PREFIX)


def write_ledger_csv(path, rows, fields):
    """Ведомость хвоста: UTF-8 без BOM, перевод строки как у DictWriter по умолчанию (как писали прежние генераторы)."""
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="raise")
        w.writeheader()
        w.writerows(rows)


def tail_fields(rows, base):
    """Поля ведомости: прежние + `_source` в конце (метка строк генератора; декодер её не читает)."""
    f = list(rows[0].keys()) if rows else list(base)
    return f + (["_source"] if "_source" not in f else [])


def build_client(spec, D):
    """Ведомости клиента: хвосты item_list / item_img_table / packagetable / item_tooltip, EN item_list / item_tooltip,
    хвост витрины. Повторный прогон идемпотентен: свои строки (метка premium_box:) пересобираются, чужие не трогаются."""
    reg = registry(D)
    cols_it = reg["item_list.bin"]["cols"]
    cols_img = reg["item_img_table.bin"]["cols"]
    cols_pkg = reg["packagetable.bin"]["cols"]
    boxes = sorted(spec["boxes"], key=lambda b: b["code"])
    codes = {b["code"] for b in boxes}
    live_it = load_csv(os.path.join(ITEMDB_LIVE, "item_list.csv"))
    live_img = load_csv(os.path.join(ITEMDB_LIVE, "item_img_table.csv"))
    live_pkg = load_csv(os.path.join(ITEMDB_LIVE, "packagetable.csv"))
    live_tip = load_csv(os.path.join(ITEMDB_LIVE, "item_tooltip.csv"))
    special = {int(r["Code"]): r for r in load_csv(os.path.join(ITEMDB_LIVE, "special_itemlist.csv"))}
    by_code = {int(r["ItemCode"]): r for r in live_it}
    tpl = by_code[TPL_CODE]
    if tpl["ItemType"] != "11" or tpl["ItemSubType"] != "200":
        raise SystemExit("образец коробки %d сменился" % TPL_CODE)

    # ★ 29.10.2026 (donor_wod): строки наборов — НА МЕСТЕ, а не «хвостом». После наборов 79719…79721 идут предметы донора
    #   WorldOfDreams (79722…, asda_wod_records.py), и новые наборы встают уже после них — сегментами. item_list плотный
    #   (строка = код − 20551), packagetable позиционная (строка = Index), item_tooltip ключевая: свой код — его прежняя строка,
    #   новый — в конец. Новые коды/пакеты обязаны продолжать таблицу (их выделяет next_free).
    if any(int(r["_row"]) != int(r["ItemCode"]) - ITEM_FIRST for r in live_it):
        raise SystemExit("ОТКАЗ: item_list не плотный (строка ≠ код − %d)" % ITEM_FIRST)
    if any(int(r["_row"]) != int(r["Index"]) for r in live_pkg):
        raise SystemExit("ОТКАЗ: packagetable — строка ≠ Index")
    have_codes = {int(r["ItemCode"]) for r in live_it}
    tip_row = {int(r["ItemCode"]): int(r["_row"]) for r in live_tip}
    new_codes = [b["code"] for b in boxes if b["code"] not in have_codes]
    if new_codes != list(range(len(live_it) + ITEM_FIRST, len(live_it) + ITEM_FIRST + len(new_codes))):
        raise SystemExit("ОТКАЗ: новые коды %s не продолжают item_list (последний %d)" % (new_codes[:5], len(live_it) + ITEM_FIRST - 1))
    new_pk = [b["package"] for b in boxes if b["package"] >= len(live_pkg)]
    if new_pk != list(range(len(live_pkg), len(live_pkg) + len(new_pk))):
        raise SystemExit("ОТКАЗ: новые пакеты %s не продолжают packagetable (%d строк)" % (new_pk[:5], len(live_pkg)))
    new_tip = iter(range(len(live_tip), len(live_tip) + len(boxes)))

    L_it, L_img, L_pkg, L_tip, en_name, en_tip, shop = [], [], [], [], [], [], []
    for k, b in enumerate(boxes):
        row_i = b["code"] - ITEM_FIRST
        tip_i = tip_row[b["code"]] if b["code"] in tip_row else next(new_tip)
        src = SRC_PREFIX + b["group"]
        item = special.get(b["item"]) if is_func(b) else by_code.get(b["item"])
        if not item:
            raise SystemExit("предмета %d нет в %s" % (b["item"], "special_itemlist" if is_func(b) else "item_list"))
        # item_list: запись из образца
        rb = bytearray(bytes.fromhex(tpl["DataHex"]))
        struct.pack_into("<i", rb, 0, b["code"])
        put_str(rb, 4, 64, b["name_ru"])
        struct.pack_into("<i", rb, 164, b["price"])
        struct.pack_into("<i", rb, 216, b["package"])
        struct.pack_into("<h", rb, 220, b["link"])
        struct.pack_into("<i", rb, 240, row_i)
        row = {"_row": row_i}
        for (off, typ, nm) in cols_it:
            row[nm] = D.rd(rb, off, typ)
        row["DataHex"] = bytes(rb).hex()
        row["_source"] = src
        L_it.append(row)
        if is_func(b):
            # item_img_table функционального предмета: строка образца-коробки, атлас/ячейка спецпредмета в обоих
            #   слотах, IconSmallFlag 0 → сетка 8×64, как у special-иконки (тот же приём, что asda_fable_mounts_boxes.py)
            if b["icon_from"] != b["item"] or not item["IconPath"]:
                raise SystemExit("иконка коробки %d: только иконка самого спецпредмета %d" % (b["code"], b["item"]))
            g = bytearray(bytes.fromhex(live_img[int(tpl["ImgIndex"])]["DataHex"]))
            if struct.unpack_from("<i", g, 0)[0] != TPL_CODE:
                raise SystemExit("img-строка образца %d: +0 не код" % TPL_CODE)
            for off_a, off_c in ((132, 164), (172, 204)):
                put_str(g, off_a, 32, item["IconPath"])
                struct.pack_into("<i", g, off_c, int(item["IconCell"]))
            struct.pack_into("<i", g, 168, 0)
        else:
            # item_img_table: иконка = строка img предмета-источника (+0 = код)
            icon = by_code[b["icon_from"]]
            g = bytearray(bytes.fromhex(live_img[int(icon["ImgIndex"])]["DataHex"]))
            if struct.unpack_from("<i", g, 0)[0] != b["icon_from"]:
                raise SystemExit("img-строка %s: +0 не код %d" % (icon["ImgIndex"], b["icon_from"]))
        struct.pack_into("<i", g, 0, b["code"])
        grow = {"_row": row_i}
        for (off, typ, nm) in cols_img:
            grow[nm] = D.rd(g, off, typ)
        grow["DataHex"] = bytes(g).hex()
        grow["_source"] = src
        L_img.append(grow)
        # packagetable: одна пара (предмет, кол-во), прочие −1/0
        prow = {"_row": b["package"], "Index": b["package"]}
        pp = box_pairs(b)
        for j in range(5):
            prow["Item%d" % j], prow["Qty%d" % j] = pp[j] if j < len(pp) else (-1, 0)
        prow.update({"Item5": 0, "Qty5": 0, "Reserved52": 0, "_source": src})
        if [c[2] for c in cols_pkg] != [x for x in prow if x not in ("_row", "_source")]:
            raise SystemExit("колонки packagetable сменились")
        L_pkg.append(prow)
        # item_tooltip
        L_tip.append({"_row": tip_i, "ItemCode": b["code"], "Description": b["desc_ru"], "_orig": "", "_source": src})
        why = "%s (%s)" % (b["why"], os.path.basename(__file__))
        en_name.append({"key": "%d.ItemName" % row_i, "value": b["name_en"], "was": None, "why": why, "source": src})
        en_tip.append({"key": "%d.Description" % tip_i, "value": b["desc_en"], "was": None, "why": why, "source": src})
        s = b["shop"]
        if s:   # коробка под прежней позицией витрины (catalog) строки витрины не получает
            shop.append({"ItemId": b["code"], "Category": s["Category"], "Sub1": s["Sub1"], "Sub2": s["Sub2"], "Field16": 0,
                         "Field20": 0, "NewFlag": s["NewFlag"], "Note": "0", "Field57": 0, "Name": "0", "_source": src})

    # хвосты ItemDB: чужие строки (без метки) + наши НА СВОИХ местах; строки ведомости обязаны идти подряд без дыр
    for fn, new in (("item_list.csv", L_it), ("item_img_table.csv", L_img), ("packagetable.csv", L_pkg), ("item_tooltip.csv", L_tip)):
        p = os.path.join(TAIL_DIR, fn)
        led = load_csv(p)
        keep = [r for r in led if not ours(r)]
        rows = sorted(keep + new, key=lambda r: int(r["_row"]))
        nums = [int(r["_row"]) for r in rows]
        if nums and nums != list(range(nums[0], nums[0] + len(nums))):
            raise SystemExit("ОТКАЗ: ведомость %s после замены своих строк — дыра/повтор строк (%s…)" % (fn, nums[:3]))
        fields = tail_fields(led, new[0].keys() if new else [])
        for r in keep:
            r.setdefault("_source", "")
        write_ledger_csv(p, rows, fields)
    for fn, new in (("item_list.json", en_name), ("item_tooltip.json", en_tip)):
        p = os.path.join(LOC_EN, fn)
        raw = open(p, "rb").read()
        doc = json.loads(raw.decode("utf-8"))
        keep = [r for r in doc["rows"] if not ours(r, "source")]
        clash = {r["key"] for r in keep} & {r["key"] for r in new}
        if clash:
            raise SystemExit("ключи EN уже заняты: %s" % sorted(clash)[:5])
        # ★ 29.10.2026: свои ключи — на прежнем месте (после них идут ключи предметов WorldOfDreams), новые — в конец
        fresh = {r["key"]: r for r in new}
        doc["rows"] = [fresh.pop(r["key"], r) if ours(r, "source") else r for r in doc["rows"]
                       if not ours(r, "source") or r["key"] in fresh] + [r for r in new if r["key"] in fresh]
        nl = "\r\n" if b"\r\n" in raw[:4096] else "\n"
        with open(p, "w", encoding="utf-8", newline=nl) as f:
            f.write(json.dumps(doc, ensure_ascii=False, indent=2) + "\n")
    with open(SHOP_TAIL, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"_comment": "Хвост витрины 6000 для премиум-коробок (строит Tools/asda_premium_box_add.py по "
                               "Tools/data/premium_box_sets.json; дописывает asda_import_premium_shop_bin.py после "
                               "PremiumShopBin_tail.json). Не править руками.", "rows": shop}, f, ensure_ascii=False, indent=1)
        f.write("\n")
    out("клиент: item_list/img/packagetable/item_tooltip +%d, EN +%d/+%d, витрина +%d (строки item %s, пакеты %s)"
        % (len(L_it), len(en_name), len(en_tip), len(shop), [r["_row"] for r in L_it][:6], [r["_row"] for r in L_pkg][:6]))


def build_server(spec):
    """srv_out: 4 таблицы × 3 GameServer из снимка srv_in + манифест для deploy_premium_boxes.py."""
    SL = stack_layout()
    boxes = sorted(spec["boxes"], key=lambda b: b["code"])
    codes = {b["code"] for b in boxes}
    pkgs = {b["package"] for b in boxes}
    for t in ("item_table.txt", "SowelCombine.txt", "PackageItem_Table.txt"):
        for d in gs_dirs()[1:]:
            if md5f(os.path.join(SRV_IN, "GameServer", t)) != md5f(os.path.join(SRV_IN, d, t)):
                raise SystemExit("ОТКАЗ: %s/%s отличается от GameServer — правка вслепую запрещена" % (d, t))
    for d in gs_dirs():
        tw = SL.twin_of(d)
        if tw != d and md5f(os.path.join(SRV_IN, tw, "SellItemTable.txt")) != md5f(os.path.join(SRV_IN, d, "SellItemTable.txt")):
            raise SystemExit("ОТКАЗ: %s/SellItemTable.txt ≠ поля %s" % (d, tw))
    it_cnt, it_body = read_srv(os.path.join(SRV_IN, "GameServer", "item_table.txt"))
    sc_cnt, sc_body = read_srv(os.path.join(SRV_IN, "GameServer", "SowelCombine.txt"))
    pk_cnt, pk_body = read_srv(os.path.join(SRV_IN, "GameServer", "PackageItem_Table.txt"))
    # ★ 29.10.2026 (donor_wod): свои строки заменяются НА МЕСТЕ, новые дописываются (наборы — сегментами вокруг блока
    #   WorldOfDreams); итог обязан идти подряд — проверка после сборки (ниже и в verify).
    if len(it_body) != len(sc_body) or [int(toks(ln)[0]) for ln in it_body] != list(range(ITEM_FIRST, ITEM_FIRST + len(it_body))):
        raise SystemExit("ОТКАЗ: item_table/SowelCombine снимка — не подряд")
    if [int(toks(ln)[0]) for ln in pk_body] != list(range(len(pk_body))):
        raise SystemExit("ОТКАЗ: PackageItem_Table снимка — не подряд")
    it_tpl = toks(next(ln for ln in it_body if int(toks(ln)[0]) == TPL_CODE))
    sc_tpl = toks(next(ln for ln in sc_body if int(toks(ln)[0]) == TPL_CODE))
    if len(it_tpl) != 42 or len(sc_tpl) != 57 or it_tpl[16] != "11" or it_tpl[17] != "200":
        raise SystemExit("серверный образец %d сменился" % TPL_CODE)
    it_new, sc_new, pk_new = [], [], []
    for b in boxes:
        t = list(it_tpl)
        t[0], t[26], t[39], t[40] = str(b["code"]), str(b["price"]), str(b["package"]), str(b["link"])
        it_new.append("\t".join(t).encode("ascii"))
        s = list(sc_tpl)
        s[0] = str(b["code"])
        sc_new.append("\t".join(s).encode("ascii"))
        pp = box_pairs(b) + [(-1, 0)] * (5 - len(box_pairs(b)))
        pk_new.append(("%d\t" % b["package"] + "\t".join("%d\t%d" % x for x in pp)).encode("ascii"))
    def splice(body, new, first):
        """Свои строки (ключ = 1-й токен) — на место прежних, остальные новые — в конец; итог подряд с first."""
        own = {int(toks(ln)[0]): ln for ln in new}
        res = [own.pop(int(toks(ln)[0]), ln) for ln in body]
        res += [own[k] for k in sorted(own)]
        if [int(toks(ln)[0]) for ln in res] != list(range(first, first + len(res))):
            raise SystemExit("ОТКАЗ: после вставки наборов таблица не подряд (новый код/пакет не продолжает таблицу?)")
        return res
    it_body, sc_body, pk_body = splice(it_body, it_new, ITEM_FIRST), splice(sc_body, sc_new, ITEM_FIRST), splice(pk_body, pk_new, 0)
    it_new = sc_new = pk_new = []
    manifest = {"tag": spec.get("tag") or datetime.date.today().strftime("%Y%m%d"), "files": {}}
    for d in gs_dirs():
        n = len(it_body) + len(it_new)
        write_srv(os.path.join(SRV_OUT, d, "item_table.txt"), n, it_body + it_new)
        write_srv(os.path.join(SRV_OUT, d, "SowelCombine.txt"), n, sc_body + sc_new)
        write_srv(os.path.join(SRV_OUT, d, "PackageItem_Table.txt"), len(pk_body) + len(pk_new), pk_body + pk_new)
        # В белый список — только коробки со строкой витрины (их покупают по коду коробки); коробку под прежней
        #   позицией (catalog) покупают по функциональному коду, сама она в SellItemTable не нужна.
        sold = [b["code"] for b in boxes if b.get("shop")]
        mx, _, sell = read_sell(os.path.join(SRV_IN, d, "SellItemTable.txt"))
        # ★ 07.11.2026: стойка сохраняется как есть, дописываются только новые коды: прежний отбор выбрасывал коробки подарка 79935–79939 (`catalog`), которые живут в белом списке.
        sell = sell + [c for c in sold if c not in sell]
        write_sell(os.path.join(SRV_OUT, d, "SellItemTable.txt"), max([mx] + sold), sell)
        for t in SRV_TABLES:
            a, o = os.path.join(SRV_IN, d, t), os.path.join(SRV_OUT, d, t)
            if md5f(a) != md5f(o):
                manifest["files"][d + "\\ItemTable_Info\\" + t] = {"local": os.path.relpath(o, SRV_TOOLS).replace("\\", "/"),
                                                                   "base": md5f(a), "new": md5f(o)}
    # Карта шлюза: catalog → коробка (только для коробок под прежней позицией витрины). Прежнее значение ключа обязано
    #   равняться map_was (или уже нашей записи при повторном прогоне) — иначе карту правил кто-то ещё, вслепую нельзя.
    mp_in = os.path.join(SRV_IN, "gp7-server", "premium_item_map.json")
    mp_out = os.path.join(SRV_OUT, "gp7-server", "premium_item_map.json")
    cat = [b for b in boxes if b.get("catalog")]
    if os.path.isfile(mp_out):
        os.remove(mp_out)
    if cat:
        mraw = open(mp_in, "rb").read()
        m = json.loads(mraw.decode("utf-8-sig"))
        for b in cat:
            want = {"deliveryItemCode": b["code"], "price": b["price"]}
            old = m.get(str(b["catalog"]))
            if old not in (b["map_was"], want):
                raise SystemExit("ОТКАЗ: карта шлюза %d = %r, ждали %r (map_was) или %r" % (b["catalog"], old, b["map_was"], want))
            m[str(b["catalog"])] = want
        os.makedirs(os.path.dirname(mp_out), exist_ok=True)
        nl = "\r\n" if b"\r\n" in mraw else "\n"
        with open(mp_out, "w", encoding="utf-8", newline=nl) as f:     # формат — как у прежних генераторов карты
            json.dump(m, f, indent=2)
            f.write("\n")
        if md5f(mp_out) != md5f(mp_in):
            manifest["files"]["gp7-server\\premium_item_map.json"] = {
                "local": os.path.relpath(mp_out, SRV_TOOLS).replace("\\", "/"), "base": md5f(mp_in), "new": md5f(mp_out)}
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
        f.write("\n")
    out("сервер: item_table/SowelCombine %d → %d, PackageItem %d → %d, SellItemTable +%d, карта шлюза %d; файлов к выкладке %d "
        "(манифест %s)" % (it_cnt, len(it_body) + len(it_new), pk_cnt, len(pk_body) + len(pk_new),
                           sum(1 for b in boxes if b.get("shop")), len(cat), len(manifest["files"]), MANIFEST))


# ── стенд ──────────────────────────────────────────────────────────────────────────────────────────
def verify(base=SRV_OUT, label="srv_out", client=True):
    """Повтор того, что сделают GameServer, шлюз gp7 и клиент со srv_out:
      загрузчики (счёт/подряд/42 и 57 токенов; SellItemTable: код ≤ макс, макс ≤ 100000, число = строкам), стартовые гейты
      (item_table == SowelCombine; +63 коробки ≤ счёта пакетов), покупка CharChargeShopBuy (код в белом списке, BuyCash = ток.26
      = цене спецификации), шлюз (не функциональный, без карты, выдаваемый, цена = ток.26), вскрытие Recv_OpenPackageItem
      (11/200, пакет[+63].index == +63, пара (предмет, кол-во), стопка: кол-во ≤ 32767 и вес стопки ≤ 32767), прежние строки
      байт в байт; клиент — цена/пакет/витрина в ведомостях = спецификации."""
    spec = load_spec()
    boxes = sorted(spec["boxes"], key=lambda b: b["code"])
    errs = []
    fn_cnt, ft_body = read_srv(os.path.join(SRV_IN, "GameServer", "Function_Table.txt"))
    fcodes = {int(toks(ln)[0]) for ln in ft_body if toks(ln) and toks(ln)[0].isdigit()}
    fprice = {int(toks(ln)[0]): int(toks(ln)[20]) for ln in ft_body if len(toks(ln)) > 20 and toks(ln)[0].isdigit()}
    m = json.loads(open(os.path.join(SRV_IN, "gp7-server", "premium_item_map.json"), "rb").read().decode("utf-8-sig"))
    m_out = json.loads(open(map_out_path(base), "rb").read().decode("utf-8-sig"))
    SL = stack_layout()
    for d in gs_dirs():
        it_cnt, it = read_srv(os.path.join(base, d, "item_table.txt"))
        sc_cnt, sc = read_srv(os.path.join(base, d, "SowelCombine.txt"))
        pk_cnt, pk = read_srv(os.path.join(base, d, "PackageItem_Table.txt"))
        for nm, cnt, body, ntok in (("item_table", it_cnt, it, 42), ("SowelCombine", sc_cnt, sc, 57)):
            if cnt != len(body):
                errs.append("%s/%s: счёт %d ≠ строк %d" % (d, nm, cnt, len(body)))
            for i, ln in enumerate(body):
                t = toks(ln)
                if int(t[0]) != ITEM_FIRST + i or len(t) != ntok:
                    errs.append("%s/%s: строка %d код %s токенов %d" % (d, nm, i, t[0], len(t)))
                    break
        if it_cnt != sc_cnt:
            errs.append("%s: item_table %d ≠ SowelCombine %d (MessageBox на старте)" % (d, it_cnt, sc_cnt))
        pkg = {}
        for i, ln in enumerate(pk):
            t = [int(x) for x in toks(ln)]
            if t[0] != i:
                errs.append("%s/PackageItem: строка %d индекс %d" % (d, i, t[0]))
                break
            pkg[i] = t
        if pk_cnt != len(pk):
            errs.append("%s/PackageItem: счёт %d ≠ строк %d" % (d, pk_cnt, len(pk)))
        rows = {int(toks(ln)[0]): toks(ln) for ln in it}
        for code, t in rows.items():
            if t[17] in ("200", "201") and int(t[39]) > pk_cnt:
                errs.append("%s: коробка %d пакет %s > %d" % (d, code, t[39], pk_cnt))
        mx, scount, sell = read_sell(os.path.join(base, d, "SellItemTable.txt"))
        if scount != len(sell) or mx > 100000 or any(c < 0 or c > mx for c in sell):
            errs.append("%s/SellItemTable: заголовок %d/%d, строк %d, вне диапазона %s"
                        % (d, mx, scount, len(sell), [c for c in sell if c < 0 or c > mx][:5]))
        sellset = set(sell)
        maxcode = it_cnt + ITEM_FIRST - 1
        for b in boxes:
            c = b["code"]
            t = rows.get(c)
            if not t:
                errs.append("%s: нет строки %d" % (d, c)); continue
            if b.get("catalog"):
                # CharChargeShopBuy, ветка sub_532B10 (GameServer.c:104757): функциональный код витрины обязан стоять в
                #   SellItemTable (dword_5F5E9C[код] == код, иначе 5071 код 14); BuyCash = Function_Table +33 = цене карты
                k = b["catalog"]
                if (k not in sellset or k > mx) and b.get("sell_after"):
                    # позицию заносит в белый список другая выкладка того же рестарта (идёт ПОСЛЕ этой)
                    out("  предупреждение: %s: витрина %d нет в SellItemTable — занесёт %s" % (d, k, b["sell_after"]))
                elif k not in sellset or k > mx:
                    errs.append("%s: витрина %d нет в SellItemTable (5071 код 14)" % (d, k))
                if k not in fcodes or fprice.get(k) != b["price"]:
                    errs.append("%s: витрина %d — не функциональный код или цена Function_Table %s ≠ %d"
                                % (d, k, fprice.get(k), b["price"]))
                # ★ 07.11.2026: коробка под позицией стоит в SellItemTable законно: подарок шлёт её код (06.11 «подарок = покупка себе»).
            elif c not in sellset or c > mx:
                errs.append("%s: %d нет в SellItemTable (CharChargeShopBuy: код не продаётся)" % (d, c))
            if t[16] != "11" or t[17] != "200" or int(t[26]) != b["price"] or int(t[39]) != b["package"]:
                errs.append("%s: %d тип %s/%s цена %s пакет %s (ждали 11/200, %d, %d)" % (d, c, t[16], t[17], t[26], t[39],
                                                                                       b["price"], b["package"]))
            p = int(t[39])
            if p not in pkg or pkg[p][0] != p:
                errs.append("%s: %d пакет %d не проходит проверки 8/9 Recv_OpenPackageItem" % (d, c, p)); continue
            pairs = [(pkg[p][1 + 2 * j], pkg[p][2 + 2 * j]) for j in range(5)]
            want = box_pairs(b)
            if pairs[:len(want)] != want or any(x > 0 for x, _ in pairs[len(want):]):
                errs.append("%s: пакет %d = %r, ждали %r" % (d, p, pairs, want))
            if is_func(b):
                # sub_55B7F0, ветка sub_532B10: функциональный код 1…count(Function_Table), выдаётся штучно
                if b["item"] not in fcodes or not 1 <= b["item"] <= fn_cnt:
                    errs.append("%s: функционального %d нет в Function_Table (sub_532B10)" % (d, b["item"]))
                if b["qty"] != 1:
                    errs.append("%s: функциональный %d — только ×1" % (d, b["item"]))
                ti = None
            else:
                ti = rows.get(b["item"])
                if not ti:
                    errs.append("%s: предмета %d нет в item_table (sub_532A90)" % (d, b["item"])); continue
            stack = ti is not None and ti[3] == "4" and ti[23] == "1" and ti[24] == "1" and ti[16] != "10"
            weight = int(ti[2]) if ti else 0
            if ti is None:
                pass
            elif stack:
                if b["qty"] > I16_MAX or b["qty"] * weight > I16_MAX:
                    errs.append("%s: %d × %d (вес %d) не влезает в одну стопку i16/SMALLINT" % (d, b["item"], b["qty"], weight))
                elif d == gs_dirs()[0] and b["qty"] * weight > 5000:
                    # Не ошибка: вскрытие вес персонажа не проверяет, SetCurrentWeightValue (sub_43F880) прижимает вес к
                    #   максимуму (+416; у персонажей стенда 15000/32000) — дальше отказ покупок (5071 код 3) и подбора.
                    #   ★ Решение владельца 26.10.2026: потолок набора — вес 5000 (песок ×10000 заменён на ×5000).
                    out("  предупреждение: %d даёт стопку весом %d (вес единицы %d) — у части персонажей это перегруз"
                        % (c, b["qty"] * weight, weight))
            elif b["qty"] != 1 and not (b.get("split") and b["qty"] <= 5):
                errs.append("%s: %d не стопочный — количество %d пакетом не выдаётся" % (d, b["item"], b["qty"]))
            # шлюз gp7: resolvePremiumItem
            if c <= 558 or c in fcodes:
                errs.append("gp7: %d — функциональный код" % c)
            if str(c) in m and m[str(c)] != {"deliveryItemCode": c, "price": b["price"]}:
                errs.append("gp7: в карте %d → %r (цена карты победит ток.26)" % (c, m[str(c)]))
            if b.get("catalog"):
                want = {"deliveryItemCode": c, "price": b["price"]}
                if m_out.get(str(b["catalog"])) != want:
                    errs.append("gp7: карта %d → %r, ждали %r" % (b["catalog"], m_out.get(str(b["catalog"])), want))
                # прочие ключи карты (не позиции коробок спецификации) не тронуты
                mine = {str(x["catalog"]) for x in boxes if x.get("catalog")}
                if {k: v for k, v in m_out.items() if k not in mine} != {k: v for k, v in m.items() if k not in mine}:
                    errs.append("gp7: в карте изменены чужие ключи")
            if not (ITEM_FIRST <= c <= maxcode):
                errs.append("gp7/GS: %d вне sub_532A90 (≤ %d) — undeliverable" % (c, maxcode))
        a_dir = SL.twin_of(d)
        for t in SRV_TABLES[:3]:
            a = open(os.path.join(SRV_IN, a_dir, t), "rb").read().split(b"\r\n", 1)[1]
            o = open(os.path.join(base, d, t), "rb").read().split(b"\r\n", 1)[1]
            own = {str(b["code"]).encode() for b in boxes} if t != "PackageItem_Table.txt" else {str(b["package"]).encode() for b in boxes}
            a_lines = [ln for ln in a.split(b"\r\n") if ln.strip() and ln.split()[0] not in own]
            o_lines = [ln for ln in o.split(b"\r\n") if ln.strip() and ln.split()[0] not in own]   # ★ 29.10: свои — на месте
            if o_lines != a_lines:
                errs.append("%s/%s: прежние строки изменены" % (d, t))
        _, _, sell_in = read_sell(os.path.join(SRV_IN, d, "SellItemTable.txt"))
        if [x for x in sell if x not in {b["code"] for b in boxes}] != [x for x in sell_in if x not in {b["code"] for b in boxes}]:
            errs.append("%s/SellItemTable: прежние коды изменены" % d)
        out("%-9s %-17s item_table %d, пакетов %d, SellItemTable %d (макс %d); наборов %d"
            % (label, d, it_cnt, pk_cnt, len(sell), mx, len(boxes)))
    if client:
        tail_it = {int(r["ItemCode"]): r for r in load_csv(os.path.join(TAIL_DIR, "item_list.csv")) if ours(r)}
        tail_pk = {int(r["Index"]): r for r in load_csv(os.path.join(TAIL_DIR, "packagetable.csv")) if ours(r)}
        shop = {r["ItemId"]: r for r in json.load(open(SHOP_TAIL, encoding="utf-8"))["rows"]} if os.path.isfile(SHOP_TAIL) else {}
        for b in boxes:
            r = tail_it.get(b["code"])
            pr = tail_pk.get(b["package"])
            if not r or int(r["Price"]) != b["price"] or int(r["DurationOrEnchant"]) != b["package"] or r["ItemName"] != b["name_ru"]:
                errs.append("клиент: item_list %d ≠ спецификации" % b["code"])
            if not pr or [(int(pr["Item%d" % j]), int(pr["Qty%d" % j])) for j in range(len(box_pairs(b)))] != box_pairs(b):
                errs.append("клиент: packagetable %d ≠ спецификации" % b["package"])
            s = shop.get(b["code"])
            if b["shop"] and (not s or any(s[k] != b["shop"][k] for k in b["shop"])):
                errs.append("клиент: витрина %d ≠ спецификации" % b["code"])
            if not b["shop"] and s:
                errs.append("клиент: коробка %d под позицией %d попала в витрину" % (b["code"], b["catalog"]))
    out("стенд %s: ошибок %d" % (label, len(errs)))
    for x in errs[:30]:
        out("  !", x)
    return not errs


# ── клиент: декод в боевой каталог, EN, витрина, задание DT ────────────────────────────────────────
def _run(argv, cwd=None, env=None, timeout=3600):
    """Запуск подпроцесса с выводом (utf-8)."""
    e = dict(os.environ, PYTHONIOENCODING="utf-8", **(env or {}))
    p = subprocess.run(argv, cwd=cwd, env=e, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def _backup(paths, bdir):
    """Бэкап файлов в bdir/<путь от @@ASTORY_DEV_ROOT_ESC@@> со сверкой размера; существующий бэкап не перезаписывается."""
    for src in paths:
        dst = os.path.join(bdir, os.path.relpath(src, os.path.dirname(PORT)))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst):
            continue                      # первый бэкап = состояние ДО волны; повторный прогон его не трогает
        shutil.copy2(src, dst)
        if os.path.getsize(dst) != os.path.getsize(src) or md5f(dst) != md5f(src):
            raise SystemExit("бэкап %s не сошёлся (размер/md5)" % dst)
    return lambda src: os.path.join(bdir, os.path.relpath(src, os.path.dirname(PORT)))


def _rows_by_key(path, key):
    """CSV → {ключ: строка-словарь}, порядок ключей."""
    rows = load_csv(path)
    return {r[key]: r for r in rows}, [r[key] for r in rows]


def _diff_only_added(old_path, new_path, key, want_new, own=()):
    """Новый CSV = прежний + ровно строки want_new (по ключу); прежние строки неизменны, кроме своих (own — строки
    генератора: при правке спецификации, например количества или цены, они меняются). Возврат список ошибок."""
    o, ok = _rows_by_key(old_path, key)
    n, nk = _rows_by_key(new_path, key)
    own = set(own)
    errs = []
    for k in ok:
        if k not in n:
            errs.append("%s: пропала строка %s" % (os.path.basename(new_path), k))
        elif n[k] != o[k] and k not in own:
            diff = [c for c in o[k] if o[k].get(c) != n[k].get(c)]
            errs.append("%s: строка %s изменилась %s" % (os.path.basename(new_path), k, diff[:5]))
    added = [k for k in nk if k not in o]
    if sorted(added) != sorted(want_new):
        errs.append("%s: добавлено %s, ждали %s" % (os.path.basename(new_path), added[:8], sorted(want_new)[:8]))
    return errs


def shop_csv(rows, path):
    """Строки витрины (JSON) → CSV формы экспорта DT_PremiumShopBin (ключ = _row)."""
    cols = ["ItemId", "Category", "Sub1", "Sub2", "Field16", "Field20", "NewFlag", "Note", "Field57", "DataHex", "CardQty", "GiftCode"]   # ★ 06.11.2026: штуки на карточке и код выдачи для подарка
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["---"] + cols)
        for r in rows:
            w.writerow([str(r["_row"])] + [r.get(c, 0) for c in cols])


def cmd_client_apply(a):
    """Боевые CSV ItemDB (+N строк в 4 таблицах), EN-оверлеи (+N ключей в 2), витрина JSON (+N строк), задание DT."""
    spec = load_spec()
    boxes = sorted(spec["boxes"], key=lambda b: b["code"])
    bdir = a.backup
    D = decoder()
    live = {t: os.path.join(ITEMDB_LIVE, t + ".csv") for t in ("item_list", "item_img_table", "packagetable", "item_tooltip")}
    live_json = [os.path.join(ITEMDB_LIVE, t + ".json") for t in live]
    en = {t: os.path.join(EN_LIVE, "DT_Loc_%s_en.csv" % t) for t in ("item_list", "item_tooltip")}
    bk = _backup(list(live.values()) + [p for p in live_json if os.path.isfile(p)] + list(en.values()) + [SHOP_JSON], bdir)
    # 1) пробный декод в отдельную папку + слой имён → сверка «только наши строки»
    tmp = os.path.join(bdir, "_decode_tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    only = "item_list.bin,item_img_table.bin,packagetable.bin,item_tooltip.bin"
    rc, log = _run([sys.executable, DECODER, "--out", tmp, "--only", only])
    open(os.path.join(bdir, "decode_tmp.log"), "w", encoding="utf-8").write(log)
    rc2, log2 = _run([sys.executable, os.path.join(HERE, "asda_apply_name_overlay.py"), "--itemdb", tmp,
                     "--tables", "item_list,item_tooltip"])   # в пробном каталоге только 4 таблицы
    open(os.path.join(bdir, "name_overlay_tmp.log"), "w", encoding="utf-8").write(log2)
    if rc or rc2:
        raise SystemExit("пробный декод/слой имён: коды %d/%d — журналы в %s" % (rc, rc2, bdir))
    # ключ сверки: _row у позиционных таблиц
    live_it = load_csv(live["item_list"])
    have = {int(r["ItemCode"]) for r in live_it}
    new_codes = [b for b in boxes if b["code"] not in have]
    row_of = {}
    tmp_it = {int(r["ItemCode"]): r["_row"] for r in load_csv(os.path.join(tmp, "item_list.csv"))}
    for b in boxes:
        row_of[b["code"]] = tmp_it.get(b["code"])
    tmp_tip = {int(r["ItemCode"]): r["_row"] for r in load_csv(os.path.join(tmp, "item_tooltip.csv"))}
    want = {"item_list": [row_of[b["code"]] for b in new_codes], "item_img_table": [row_of[b["code"]] for b in new_codes],
            "packagetable": [str(b["package"]) for b in new_codes], "item_tooltip": [tmp_tip.get(b["code"]) for b in new_codes]}
    # Все свои строки (новые и уже залитые — их меняет правка спецификации): ось allow_rows точечной заливки DT.
    own = {"item_list": [row_of[b["code"]] for b in boxes], "item_img_table": [row_of[b["code"]] for b in boxes],
           "packagetable": [str(b["package"]) for b in boxes], "item_tooltip": [tmp_tip.get(b["code"]) for b in boxes]}
    # ★ 02.11.2026: `extra_rows` спецификации — АРХИВНЫЕ строки, которые эта волна правит через ведомости (рецепт имён,
    #   _value_overlay, EN-ведомости): коробка 76405 после правки сервера выдаёт 558 — её имя/описание/состав в клиенте
    #   приведены к выдаче (Reports/levelup_potion). Они разрешены к изменению, как свои, и попадают в оси DT.
    extra = spec.get("extra_rows") or {}
    for t in own:
        own[t] = own[t] + [str(x) for x in extra.get(t, [])]
    errs = []
    for t, p in live.items():
        errs += _diff_only_added(p, os.path.join(tmp, t + ".csv"), "_row", want[t], own[t])
    if errs:
        for e in errs[:20]:
            out("  !", e)
        raise SystemExit("ОТКАЗ: пробный декод меняет не только наши строки — боевой каталог не тронут")
    out("пробный декод: +%d новых строк, меняются только свои строки — OK" % len(new_codes))
    # 2) боевой декод (--accept-diff: стадия + слой имён + сверка + бэкап декодера) и сверка с пробным
    if any(md5f(p) != md5f(os.path.join(tmp, t + ".csv")) for t, p in live.items()):
        rc, log = _run([sys.executable, DECODER, "--only", only, "--accept-diff"])
        open(os.path.join(bdir, "decode_live.log"), "w", encoding="utf-8").write(log)
        if rc:
            raise SystemExit("боевой декод: код %d — журнал %s" % (rc, os.path.join(bdir, "decode_live.log")))
    for t, p in live.items():
        if md5f(p) != md5f(os.path.join(tmp, t + ".csv")):
            raise SystemExit("ОТКАЗ: боевой %s ≠ пробному декоду (см. %s)" % (t, bdir))
    out("боевые CSV ItemDB = пробному декоду")
    # 3) EN-оверлеи: извлечение в отдельную папку → сверка → перенос двух CSV
    etmp = os.path.join(bdir, "_en_tmp")
    shutil.rmtree(etmp, ignore_errors=True)
    rc, log = _run([sys.executable, os.path.join(HERE, "extract_text_lang.py"), "--lang", "en", "--out", etmp,
                    "--only", "item_list,item_tooltip"], cwd=UPROJ)
    open(os.path.join(bdir, "extract_en.log"), "w", encoding="utf-8").write(log)
    if rc:
        raise SystemExit("extract_text_lang: код %d — журнал %s" % (rc, bdir))
    en_want = {"item_list": ["%s.ItemName" % row_of[b["code"]] for b in boxes] + list(extra.get("en_item_list", [])),
               "item_tooltip": ["%s.Description" % tmp_tip[b["code"]] for b in boxes] + list(extra.get("en_item_tooltip", []))}
    for t, p in en.items():
        cur = {r["Name"] for r in load_csv(p)}
        e = _diff_only_added(p, os.path.join(etmp, os.path.basename(p)), "Name", [k for k in en_want[t] if k not in cur], en_want[t])
        if e:
            for x in e[:10]:
                out("  !", x)
            raise SystemExit("ОТКАЗ: EN %s меняет не только наши ключи" % t)
        shutil.copy2(os.path.join(etmp, os.path.basename(p)), p)
    out("EN-оверлеи: +ключи только наши — перенесены")
    # 4) витрина: JSON из архива + хвостов
    old_shop = json.load(open(bk(SHOP_JSON), encoding="utf-8-sig"))
    rc, log = _run([sys.executable, os.path.join(HERE, "asda_import_premium_shop_bin.py"), "--json-only"])
    open(os.path.join(bdir, "shop_json.log"), "w", encoding="utf-8").write(log)
    new_shop = json.load(open(SHOP_JSON, encoding="utf-8-sig"))
    if rc or new_shop[:len(old_shop)] != old_shop \
            or [r["ItemId"] for r in new_shop[len(old_shop):]] != [b["code"] for b in boxes
                                                                   if b["shop"] and b["code"] not in {x["ItemId"] for x in old_shop}]:
        raise SystemExit("ОТКАЗ: витрина JSON меняет не только хвост (журнал %s)" % os.path.join(bdir, "shop_json.log"))
    shop_csv(old_shop, os.path.join(bdir, "PremiumShopBin_old.csv"))
    shop_csv(new_shop, os.path.join(bdir, "PremiumShopBin_new.csv"))
    out("витрина: %d → %d строк" % (len(old_shop), len(new_shop)))
    # 5) задание точечной заливки DT
    shop_rows = [str(r["_row"]) for r in new_shop if r["ItemId"] in {b["code"] for b in boxes}]
    jobs = [
        {"dt": DT["item_list"], "csv_new": live["item_list"], "csv_old": bk(live["item_list"]), "allow_rows": own["item_list"]},
        {"dt": DT["item_img_table"], "csv_new": live["item_img_table"], "csv_old": bk(live["item_img_table"]), "allow_rows": own["item_img_table"]},
        {"dt": DT["packagetable"], "csv_new": live["packagetable"], "csv_old": bk(live["packagetable"]), "allow_rows": own["packagetable"]},
        {"dt": DT["item_tooltip"], "csv_new": live["item_tooltip"], "csv_old": bk(live["item_tooltip"]), "allow_rows": own["item_tooltip"]},
        {"dt": DT["en_item_list"], "csv_new": en["item_list"], "csv_old": bk(en["item_list"]), "allow_rows": en_want["item_list"]},
        {"dt": DT["en_item_tooltip"], "csv_new": en["item_tooltip"], "csv_old": bk(en["item_tooltip"]), "allow_rows": en_want["item_tooltip"]},
        {"dt": DT["shop"], "csv_new": os.path.join(bdir, "PremiumShopBin_new.csv"), "csv_old": os.path.join(bdir, "PremiumShopBin_old.csv"),
         "allow_rows": shop_rows},
    ]
    for j in jobs:
        for k in ("csv_new", "csv_old"):
            j[k] = j[k].replace("\\", "/")
        j["allow_rows"] = [x for x in j["allow_rows"] if x is not None]
    with open(os.path.join(bdir, "dt_point_jobs.json"), "w", encoding="utf-8") as f:
        json.dump(jobs, f, ensure_ascii=False, indent=1)
    out("задание DT:", os.path.join(bdir, "dt_point_jobs.json"))


def cmd_dt_apply(a):
    """Точечная заливка DT (asda_ml_dt_point.py через run_headless_safe.py; сухой прогон — --dry)."""
    jobs = os.path.join(a.backup, "dt_point_jobs.json")
    # ★ 26.10.2026: запись — только ассетов, где сухой прогон нашёл изменения («изменений 0» не пересохраняем; так при правке
    #   одного поля спецификации, например количества, пишутся 2–3 DT, а не все 7). Нужен прежний dt_point_dry.log.
    dry_log = os.path.join(a.backup, "dt_point_dry.log")
    if not a.dry and os.path.isfile(dry_log):
        import re
        zero = set(re.findall(r"\[dt-point\] (/Game/\S+): изменений 0 ", open(dry_log, encoding="utf-8", errors="replace").read()))
        all_jobs = json.load(open(jobs, encoding="utf-8"))
        keep = [j for j in all_jobs if j["dt"] not in zero]
        if len(keep) != len(all_jobs):
            jobs = os.path.join(a.backup, "dt_point_jobs_write.json")
            with open(jobs, "w", encoding="utf-8") as f:
                json.dump(keep, f, ensure_ascii=False, indent=1)
            out("без изменений (не пишутся):", sorted(zero))
    # бэкап .uasset до записи
    if not a.dry:
        for j in json.load(open(jobs, encoding="utf-8")):
            p = os.path.join(UPROJ, "Content", j["dt"][len("/Game/"):].replace("/", os.sep) + ".uasset")
            _backup([p], a.backup)
    lg = os.path.join(a.backup, "dt_point_%s.log" % ("dry" if a.dry else "write"))
    env = {"ASDA_PT_JOBS": jobs}
    if a.dry:
        env["ASDA_PT_DRYRUN"] = "1"
    rc, log = _run([sys.executable, os.path.join(HERE, "run_headless_safe.py"), os.path.join(HERE, "asda_ml_dt_point.py"),
                    "--log", lg, "--marker", "ЗАВЕРШЕНО: точечные DT", "--timeout", "1800"], env=env)
    out(log[-3000:])
    out("код", rc, "журнал", lg)
    if rc:
        raise SystemExit(rc)


GW_STAND = os.path.join(PORT, "AstoryServer", "Patches", "Gp7Gateway", "stand")       # стенд шлюза (js, шаблон кадра, фальшивый sqlcmd)


def cmd_gw_stand(a):
    """Стенд шлюза gp7: живой server.js (снимок) + item_table из srv_out + фальшивый sqlcmd; кейсы premium_box_cases.js
    (покупка по одной по цене спецификации, отказ при чужой цене без списания, корзина CRT8 одной транзакцией)."""
    d = a.out or os.path.join(SRV_DIR, "gw_stand")
    shutil.rmtree(d, ignore_errors=True)
    gs = os.path.join(d, "gp7-server")
    tab = os.path.join(d, "GameServer", "ItemTable_Info")
    os.makedirs(gs)
    os.makedirs(tab)
    shutil.copy2(os.path.join(SRV_IN, "gp7-server", "server.js"), gs)
    shutil.copy2(map_out_path(), os.path.join(gs, "premium_item_map.json"))   # карта выкладки (catalog → коробка)
    shutil.copy2(os.path.join(SRV_OUT, "GameServer", "item_table.txt"), tab)
    shutil.copy2(os.path.join(SRV_IN, "GameServer", "Function_Table.txt"), tab)
    cfg = {"host": "127.0.0.1", "port": a.port, "logBytes": 256, "mode": "echo", "balanceSource": "json",
           "sql": {"command": os.path.join(GW_STAND, "fake_sqlcmd.exe"), "server": "none", "database": "none", "timeoutMs": 500},
           "balancesFile": "balances.json", "defaultPremiumBalance": 0, "defaultPremiumPresent": 0, "quietHeartbeat": True,
           "heartbeatHex": "FB0A0000005D001400FE", "testFixedBalance": 999999}
    with open(os.path.join(gs, "config.json"), "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=1)
    sql_log = os.path.join(d, "sql.log")
    rc, log = _run(["node", os.path.join(GW_STAND, "premium_box_cases.js"), gs, os.path.join(GW_STAND, "tpl_75702.hex"),
                    str(a.port), sql_log, SPEC], timeout=300)
    open(os.path.join(d, "stand.log"), "w", encoding="utf-8").write(log)
    out(log.strip())
    out("стенд шлюза: код %d, журнал %s" % (rc, os.path.join(d, "stand.log")))
    if rc:
        raise SystemExit(rc)


def cmd_verify_live(a):
    """Перечитать стойку по SSH во временный снимок и прогнать стенд (после выкладки)."""
    live = os.path.join(SRV_DIR, "srv_live")
    for rel, loc in srv_rels():
        if rel.endswith(tuple(SRV_TABLES)) or rel.endswith("premium_item_map.json"):
            _scp_get(rel, os.path.join(live, loc))
    raise SystemExit(0 if verify(live, "srv_live", client=False) else 2)


def main(argv=None):
    for st in (sys.stdout, sys.stderr):
        try:
            st.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fetch-server")
    p = sub.add_parser("add")
    p.add_argument("--group", required=True)
    p.add_argument("--item", type=int, required=True)
    p.add_argument("--qty", required=True)
    p.add_argument("--price", required=True)
    p.add_argument("--name-ru", required=True)
    p.add_argument("--name-en", required=True)
    p.add_argument("--desc-ru", required=True)
    p.add_argument("--desc-en", required=True)
    p.add_argument("--cat", type=int, default=5)
    p.add_argument("--sub1", type=int, default=0)
    p.add_argument("--sub2", type=int, default=0)
    p.add_argument("--newflag", type=int, default=0)
    p.add_argument("--icon-from", type=int, default=0)
    p.add_argument("--catalog", type=int, default=0)       # прежняя позиция витрины (функциональный код), шлюз → коробка
    p.add_argument("--map-was", default="")                # прежнее значение ключа карты «коробка:цена» (сторож)
    p.add_argument("--why", default="")
    sub.add_parser("build")
    sub.add_parser("verify")
    p = sub.add_parser("client-apply")
    p.add_argument("--backup", required=True)
    p = sub.add_parser("dt-apply")
    p.add_argument("--backup", required=True)
    p.add_argument("--dry", action="store_true")
    sub.add_parser("verify-live")
    p = sub.add_parser("gw-stand")
    p.add_argument("--out", default="")
    p.add_argument("--port", type=int, default=25041)
    a = ap.parse_args(argv)
    if a.cmd == "fetch-server":
        fetch_server()
    elif a.cmd == "add":
        cmd_add(a)
    elif a.cmd == "build":
        spec = load_spec()
        build_client(spec, decoder())
        build_server(spec)
        if not verify():
            raise SystemExit(2)
    elif a.cmd == "verify":
        raise SystemExit(0 if verify() else 2)
    elif a.cmd == "client-apply":
        os.makedirs(a.backup, exist_ok=True)
        cmd_client_apply(a)
    elif a.cmd == "dt-apply":
        cmd_dt_apply(a)
    elif a.cmd == "verify-live":
        cmd_verify_live(a)
    elif a.cmd == "gw-stand":
        cmd_gw_stand(a)


if __name__ == "__main__":
    main()
