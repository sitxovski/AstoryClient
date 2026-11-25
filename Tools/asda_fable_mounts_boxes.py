# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_fable_mounts_boxes.py — ПРЕМИУМ-КОРОБКИ для 79 маунтов донора AsdaFableClassic (функциональные коды 559…637),
чтобы их можно было КУПИТЬ в премиум-магазине 6000 (баг 24.10.2026: «купил — ничего не случилось, дальше
"Сервер не отвечает"»).

ПОЧЕМУ КОРОБКИ (реверс 24.10.2026, GameServer.c + дизасм GameServer.exe.asm, оба метода сошлись):
  • Ответ шлюза GP7 на покупку (подтип 1) GameServer принимает ТОЛЬКО с кодом обычного предмета:
    `sub_4F6650` (@0x4F69DC…0x4F6AC4): `sub_532A90(code)` = `code >= 20551 && code <= dword_5F5EAC`
    (`dword_5F5EAC = count(item_table.txt) + 20550`), затем `tCash = item_table[code].+41` и
    `SendNewPremiumPacket` → DB 2601 → `UPCashitemBuy` → ItemPostbox. Функциональный код (≤ 637) →
    «GP7's Response ItemCode is Invalid», клиенту НЕ отвечает ничего (дефект оригинала: ни 5071, ни отказа).
  • Оригинал продаёт функциональные предметы через КОРОБКУ: предмет item_table `+26 = 11, +27 = 200`
    (`Recv_OpenPackageItem@0x55A820`, GameServer.c:133455), `+63` = индекс строки `PackageItem_Table.txt`
    (5 пар «код, кол-во»), внутри — функциональный код (выдаёт `sub_55B7F0`, ветка `sub_532B10` = 1…count
    Function_Table). Сайт сопоставляет «код витрины → код коробки» картой `premium_item_map.json` шлюза:
    например 490 → 76406 (пакет 4573 = 2×490), 30 → 76424 (пакет 4591 = 30), 157 → 76467 (пакет 4634).
  • Поэтому витрина (`PremiumShopBin`) ПРОДАЁТ маунт по функциональному коду (как оригинальные 30/157/490),
    а шлюз отдаёт коду коробки: 559…637 → 79429…79507.

★ 24.10.2026, решение владельца «Да, все 211»: тем же способом заведены коробки и для 211 ОРИГИНАЛЬНЫХ
  функциональных предметов витрины, у которых не было записи в карте шлюза (готовой коробки «предмет ×1» нет ни у
  одного — проверено по PackageItem_Table). Итого 290 коробок 79429…79718, пакеты 5228…5517, ключ связи 651…940:
  сначала 79 маунтов (их коды не сдвигаются), затем 211 оригинальных по возрастанию кода. Цена коробки и записи
  карты — серверная Function_Table.txt (21-я колонка; её присылает клиент, у всех 290 совпадает с special_itemlist).
  Ниже числа «+79» читать как «+290» (сводка — server/boxes/boxes.csv).

★ 26.10.2026: строки ПОСЛЕ диапазона 79429…79718 (премиум-коробки «предмет ×N» `asda_premium_box_add.py`, метка
  `_source = premium_box:…` в ведомостях; коды с 79719) этот генератор сохраняет как есть — и в ведомостях, и на сервере.

ЧТО СТРОИТ (повторный прогон идемпотентен; номера строк — от АРХИВНОЙ части таблиц):
  Клиент (ведомости хвоста `AstoryClient/RawAssets/ItemDB/_tail_append/`, их дописывает
  `AstoryClient/Tools/ItemDB/decode_item_db.py` ключом `tail_ledger` реестра):
    • item_list.csv       +79 коробок 79429…79507 (образец — премиум-коробка маунта 76467 «Бустер метлы»:
                          ItemType 11 / SubType 200, ClassRestriction 1, CategoryFlag 3 / SubCategory 1);
                          имя = имя маунта; Price = 500; DurationOrEnchant = индекс пакета; LinkWordCode — свой
                          у каждой (как у премиум-коробок 76347…76470, у которых ключ уникален);
    • item_img_table.csv  +79 строк: иконка = иконка маунта (атлас/ячейка special_itemlist, сетка 8×64 — та же
                          формула `AsdaItemIcon::FromImg` при IconSmallFlag 0, что у `FromSpecialIcon`);
    • packagetable.csv    +79 пакетов 5228…5306: (код маунта, 1), прочие позиции −1/0;
    • item_tooltip.csv    +79 описаний = имени (у оригинальных коробок описание = имя: «Бустер метлы»);
    • EN: Tools/data/loc/en/item_list.json, item_tooltip.json — ключи новых строк (was = null, source = fable_shop).
  Сервер (файлы `Reports/fable_mounts/server/boxes/`; запись на стойку — ключ --apply-server):
    • GameServer/ и Indun_GameServer/ItemTable_Info/item_table.txt      счёт 58878 → 58957, +79 строк (образец 76467:
                          ток.26 (+41 цена) = 500, ток.39 (+63 пакет), ток.40 (+65 ключ связи));
    • …/SowelCombine.txt  счёт 58878 → 58957, +79 строк — образец строки 76467 (обязано совпадать с item_table,
                          иначе MessageBox «item_table.txt != SowelCombine.txt», GameServer.c:75269);
    • …/PackageItem_Table.txt счёт 5228 → 5307, +79 строк (индекс строки == её номер — проверка 9 у
                          `Recv_OpenPackageItem`; `+63 ≤ счёта` — проверка старта GameServer.c:75252);
    • gp7-server/premium_item_map.json  +79 ключей «559…637 → коробка, цена 500».

Запуск (без UE):
  python asda_fable_mounts_boxes.py                  — построить ведомости и серверные файлы (из снимка srv_in/)
  python asda_fable_mounts_boxes.py --fetch-server   — снять снимок сервера по SSH в srv_in/ (перед построением)
  python asda_fable_mounts_boxes.py --apply-server   — стенд, бэкап на сервере *.bak_20261024_fableshop[_<md5 до>] + запись + сверка md5
  python asda_fable_mounts_boxes.py --verify-live    — перечитать стойку по SSH и прогнать стенд (все функц. коды витрины)
Шару \\@@ASTORY_SERVER_HOSTNAME@@ не трогает (только SSH/scp).
"""
import csv
import hashlib
import json
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asda_paths as _P

PORT = _P.PORT_DIR
ITEMDB_LIVE = os.path.join(PORT, "AstoryClient", "RawAssets", "Data", "ItemDB")
TAIL_DIR = os.path.join(PORT, "AstoryClient", "RawAssets", "ItemDB", "_tail_append")
LOC_EN = os.path.join(HERE, "data", "loc", "en")
SRV_DIR = os.path.join(PORT, "Reports", "fable_mounts", "server", "boxes")
SRV_IN = os.path.join(SRV_DIR, "srv_in")        # снимок стойки ДО правки (--fetch-server)
SRV_OUT = os.path.join(SRV_DIR, "srv_out")      # построенные файлы для записи

MOUNT_FIRST, MOUNT_LAST = 559, 637        # функциональные коды маунтов донора (asda_fable_mounts_data.py)
ITEM_FIRST = 20551                        # g_ItemCodeFirst / sub_532A90
PRICE = 500                               # GXP, решение владельца 23.10.2026 (= цена витрины и карты шлюза)
TPL_CODE = 76467                          # образец: премиум-коробка маунта «Бустер метлы» (пакет 4634)
OUR_FIRST = 79429                         # первая коробка этого генератора (после хвоста клиента 13.08, g_ItemCodeLast 79428)
SOURCE = "fable_shop"                     # метка строк этого генератора (EN-ведомости, item_tooltip)

SSH_KEY = r"C:\Users\@@ASTORY_DEV_USER@@\.ssh\@@ASTORY_SSH_KEY_NAME@@"
SSH_HOST = "Administrator@@@ASTORY_SERVER_VPN_IP@@"
SRV_ROOT = "@@ASTORY_SERVER_ROOT_FWD@@/Working_ASDA/v2"
SRV_TABLES = ("item_table.txt", "SowelCombine.txt", "PackageItem_Table.txt")
sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "AstoryServer", "Patches", "AsdaPatch", "tools")))
import stack_layout   # noqa: E402  ★ 26.10.2026: единый состав стойки (канал 2: Ch2_*) — Reports/channel2/REPORT.md §3.1
SRV_GS_DIRS = tuple(stack_layout.GS_FOLDERS)   # было ("GameServer", "Indun_GameServer"); 26.10: + Ch2_GameServer
SRV_MAP = "gp7-server/premium_item_map.json"
SRV_BAK = ".bak_20261024_fableshop"


# ── разбор/сборка серверных таблиц ─────────────────────────────────────────────────────────────────
def read_srv(path):
    """(счёт из 1-й строки, список строк-байтов тела без пустых, признак CRLF в конце)."""
    raw = open(path, "rb").read()
    lines = raw.split(b"\r\n")
    count = int(lines[0].strip())
    body = [ln for ln in lines[1:] if ln.strip()]
    return count, body


def write_srv(path, count, body):
    with open(path, "wb") as f:
        f.write(str(count).encode("ascii") + b"\r\n")
        for ln in body:
            f.write(ln + b"\r\n")


def toks(line):
    return line.decode("ascii").split()


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


# ── SSH ────────────────────────────────────────────────────────────────────────────────────────────
def _ssh(cmd, timeout=120):
    return subprocess.run(["ssh", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", SSH_HOST, cmd],
                          capture_output=True, text=True, timeout=timeout)


def _scp(src, dst, timeout=600):
    r = subprocess.run(["scp", "-q", "-i", SSH_KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", src, dst],
                       capture_output=True, text=True, timeout=timeout)
    if r.returncode:
        raise SystemExit("scp %s -> %s: %s" % (src, dst, r.stderr.strip()))


def _remote_md5(rel):
    r = _ssh('powershell -NoProfile -Command "(Get-FileHash \'%s/%s\' -Algorithm MD5).Hash"' % (SRV_ROOT, rel))
    return r.stdout.strip().lower()


def srv_files():
    out = [(d + "/ItemTable_Info/" + t, os.path.join(d, t)) for d in SRV_GS_DIRS for t in SRV_TABLES]
    out.append((SRV_MAP, os.path.join("gp7-server", "premium_item_map.json")))
    return out


def fetch_server():
    # Function_Table.txt — только для стенда (граница sub_532B10), не правится.
    for rel, loc in srv_files() + [("GameServer/ItemTable_Info/Function_Table.txt",
                                    os.path.join("GameServer", "Function_Table.txt"))]:
        p = os.path.join(SRV_IN, loc)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        _scp("%s:%s/%s" % (SSH_HOST, SRV_ROOT, rel), p)
        rm = _remote_md5(rel)
        assert rm == md5(p), "md5 снимка %s: локально %s, на сервере %s" % (rel, md5(p), rm)
        print("снимок %-52s %s" % (rel, rm))


def apply_server():
    """Бэкап рядом (*.bak_20261024_fableshop, только если его ещё нет), запись, сверка md5. Перед записью
    проверяет, что файл на сервере НЕ менялся со снимка (иначе отказ — пересобрать от свежего снимка)."""
    plan = []
    for rel, loc in srv_files():
        src, out = os.path.join(SRV_IN, loc), os.path.join(SRV_OUT, loc)
        rm = _remote_md5(rel)
        if rm == md5(out):
            print("уже записан  %s" % rel)
            continue
        if rm != md5(src):
            raise SystemExit("ОТКАЗ: %s на сервере изменился со снимка (%s ≠ %s) — --fetch-server и пересборка"
                             % (rel, rm, md5(src)))
        plan.append((rel, out))
    for rel, out in plan:
        win = (SRV_ROOT + "/" + rel).replace("/", "\\")
        # Бэкап именуется по состоянию «до»: первый заход — `*.bak_20261024_fableshop` (исходное до 24.10), каждый
        #   следующий — `*.bak_20261024_fableshop_<md5[:8] снимка>`; прежние бэкапы не перезаписываются никогда.
        src_md5 = md5(os.path.join(SRV_IN, dict(srv_files())[rel]))
        r0 = _ssh('powershell -NoProfile -Command "if(Test-Path \'%s%s\'){(Get-FileHash \'%s%s\' -Algorithm MD5).Hash}"'
                  % (win, SRV_BAK, win, SRV_BAK))
        first = r0.stdout.strip().lower()
        bak = SRV_BAK if (not first or first == src_md5) else "%s_%s" % (SRV_BAK, src_md5[:8])
        r = _ssh('powershell -NoProfile -Command "if(-not (Test-Path \'%s%s\')){Copy-Item \'%s\' \'%s%s\'}; '
                 '(Get-FileHash \'%s%s\' -Algorithm MD5).Hash"' % (win, bak, win, win, bak, win, bak))
        assert r.stdout.strip().lower() == src_md5, "бэкап %s%s не совпал со снимком" % (rel, bak)
        _scp(out, "%s:%s/%s.new_fableshop" % (SSH_HOST, SRV_ROOT, rel))
        r = _ssh('powershell -NoProfile -Command "Move-Item -Force \'%s.new_fableshop\' \'%s\'; '
                 '(Get-FileHash \'%s\' -Algorithm MD5).Hash"' % (win, win, win))
        got = r.stdout.strip().lower()
        assert got == md5(out), "после записи %s md5 %s ≠ %s" % (rel, got, md5(out))
        print("записан     %-52s %s (бэкап %s)" % (rel, got, bak))


# ── построение ─────────────────────────────────────────────────────────────────────────────────────
def put_str(b, off, n, s):
    raw = s.encode("cp1251")
    assert len(raw) < n, "строка «%s» длиннее поля %d" % (s, n)
    b[off:off + n] = raw + b"\0" * (n - len(raw))


def registry(D):
    for name in dir(D):
        v = getattr(D, name)
        if isinstance(v, dict) and isinstance(v.get("item_list.bin"), dict) and "stride" in v["item_list.bin"]:
            return v
    raise SystemExit("реестр decode_item_db не найден")


def load_csv(name, keep=None):
    rows = list(csv.DictReader(open(os.path.join(ITEMDB_LIVE, name), encoding="utf-8-sig")))
    return [r for r in rows if keep is None or keep(r)]


def build():
    sys.path.insert(0, os.path.join(PORT, "AstoryClient", "Tools", "ItemDB"))
    import decode_item_db as D
    csv.field_size_limit(1 << 30)
    reg = registry(D)
    for k in ("item_list.bin", "item_img_table.bin", "packagetable.bin", "item_tooltip.bin"):
        assert reg[k].get("tail_ledger"), "в реестре decode_item_db у %s нет tail_ledger" % k

    # Архивная часть клиентских таблиц: коды < первой коробки (повторный прогон не сдвигает _row).
    items = load_csv("item_list.csv", lambda r: int(r["ItemCode"]) <= 79428)
    imgs = load_csv("item_img_table.csv", lambda r: int(r["_row"]) < len(items))
    pkgs = load_csv("packagetable.csv", lambda r: int(r["_row"]) < 5228)
    tips_all = load_csv("item_tooltip.csv")
    assert len(items) == 58878 and int(items[-1]["ItemCode"]) == 79428, "item_list сменился: %d" % len(items)
    assert len(imgs) == 58878 and len(pkgs) == 5228, (len(imgs), len(pkgs))
    box_first = int(items[-1]["ItemCode"]) + 1                 # 79429
    pkg_first = len(pkgs)                                     # 5228
    tpl = next(r for r in items if int(r["ItemCode"]) == TPL_CODE)
    assert tpl["ItemType"] == "11" and tpl["ItemSubType"] == "200", "образец коробки сменился"
    tpl_img = imgs[int(tpl["_row"])]

    spec_all = {int(r["Code"]): r for r in load_csv("special_itemlist.csv")}
    # EN-имя коробки — та же строка, что EN-клиент показывает у самого функционального предмета:
    #   DT_Loc_special_itemlist_en (ключ «_row.Name»), поверх — ведомость special_itemlist.json.
    en_spec = {}
    en_dt = os.path.join(PORT, "AstoryClient", "RawAssets", "Text", "L10N", "en", "DT_Loc_special_itemlist_en.csv")
    for r in csv.DictReader(open(en_dt, encoding="utf-8-sig")):
        en_spec[r["Name"]] = r["Text"]
    for r in json.load(open(os.path.join(LOC_EN, "special_itemlist.json"), encoding="utf-8"))["rows"]:
        en_spec[r["key"]] = r["value"]
    # Цена — серверная Function_Table.txt (21-я колонка): её же берёт шлюз и её же присылает клиент (покупки 493/497
    #   23.10 прошли сверку цены шлюза со status 1).
    ft_price = {}
    for ln in read_srv(os.path.join(SRV_IN, "GameServer", "Function_Table.txt"))[1]:
        t = ln.decode("latin1").split()
        if len(t) > 20 and t[0].isdigit():
            ft_price[int(t[0])] = int(t[20])
    # Состав: (1) маунты донора 559…637 — первыми (их коробки 79429…79507 уже на стойке и не сдвигаются);
    #   (2) решение владельца 24.10 «Да, все 211»: оригинальные функциональные предметы витрины (код < 559) без записи
    #   в карте шлюза. Готовой коробки «предмет ×1» нет ни у одного из них (проверено по PackageItem_Table 24.10) —
    #   иначе в карту пошла бы она. Записи карты, ведущие на НАШИ коробки (повторный прогон), считаются «нет записи».
    map_in = json.loads(open(os.path.join(SRV_IN, "gp7-server", "premium_item_map.json"), "rb").read().decode("utf-8-sig"))
    shop = json.load(open(os.path.join(PORT, "AstoryClient", "RawAssets", "Data", "NPC", "PremiumShopBin.json"),
                          encoding="utf-8-sig"))
    ours_from = int(items[-1]["ItemCode"]) + 1
    orig = sorted({int(r["ItemId"]) for r in shop if 0 < int(r["ItemId"]) < MOUNT_FIRST
                   and (str(r["ItemId"]) not in map_in
                        or map_in[str(r["ItemId"])]["deliveryItemCode"] >= ours_from)})
    ENTRIES = [(c, "маунт донора AsdaFableClassic") for c in range(MOUNT_FIRST, MOUNT_LAST + 1)]
    ENTRIES += [(c, "оригинальный предмет витрины без коробки (решение владельца 24.10)") for c in orig]
    for c, _ in ENTRIES:
        assert c in spec_all and c in ft_price, "код %d нет в special_itemlist/Function_Table" % c
        assert spec_all[c]["IconPath"], "у %d нет иконки" % c
    assert all(ft_price[c] == PRICE for c in range(MOUNT_FIRST, MOUNT_LAST + 1)), "цена маунтов в Function_Table ≠ 500"

    # Снимок сервера: item_table / SowelCombine / PackageItem_Table (GameServer; Indun обязан совпадать).
    it_cnt, it_body = read_srv(os.path.join(SRV_IN, "GameServer", "item_table.txt"))
    sc_cnt, sc_body = read_srv(os.path.join(SRV_IN, "GameServer", "SowelCombine.txt"))
    pk_cnt, pk_body = read_srv(os.path.join(SRV_IN, "GameServer", "PackageItem_Table.txt"))
    for t in SRV_TABLES:
        for d in SRV_GS_DIRS[1:]:   # 26.10: Indun_GameServer и Ch2_GameServer обязаны совпадать с GameServer
            assert md5(os.path.join(SRV_IN, "GameServer", t)) == md5(os.path.join(SRV_IN, d, t)), \
                "%s/%s отличается от GameServer — правка вслепую запрещена" % (d, t)
    # Строки этого генератора (если уже записаны) отбрасываем — сборка всегда от архивной части.
    # ★ 26.10.2026: строки ПОСЛЕ диапазона этого генератора (премиум-коробки Tools/asda_premium_box_add.py, коды с
    #   79719) — чужие: сохраняются как есть и дописываются за своими (иначе пересборка стёрла бы их со стойки).
    nb = len(ENTRIES)
    it_foreign = [ln for ln in it_body if int(toks(ln)[0]) >= box_first + nb]
    sc_foreign = [ln for ln in sc_body if int(toks(ln)[0]) >= box_first + nb]
    pk_foreign = [ln for ln in pk_body if int(toks(ln)[0]) >= pkg_first + nb]
    it_body = [ln for ln in it_body if int(toks(ln)[0]) < box_first]
    sc_body = [ln for ln in sc_body if int(toks(ln)[0]) < box_first]
    pk_body = [ln for ln in pk_body if int(toks(ln)[0]) < pkg_first]
    assert len(it_body) == len(sc_body) == box_first - ITEM_FIRST, (len(it_body), len(sc_body))
    assert len(pk_body) == pkg_first
    it_tpl = toks(next(ln for ln in it_body if int(toks(ln)[0]) == TPL_CODE))
    sc_tpl = toks(next(ln for ln in sc_body if int(toks(ln)[0]) == TPL_CODE))
    assert len(it_tpl) == 42 and len(sc_tpl) == 57
    assert it_tpl[16] == "11" and it_tpl[17] == "200", "серверный образец не коробка"
    # Ключ связи (+65 сервера = LinkWordCode @220 клиента): первый свободный выше максимума обеих сторон.
    link0 = max(max(int(toks(ln)[40]) for ln in it_body), max(int(r["LinkWordCode"]) for r in items)) + 1

    cols_it = reg["item_list.bin"]["cols"]
    cols_img = reg["item_img_table.bin"]["cols"]
    cols_tip = reg["item_tooltip.bin"]["cols"]
    cols_pkg = reg["packagetable.bin"]["cols"]
    L_it, L_img, L_pkg, L_tip, en_name, en_tip, it_new, sc_new, pk_new, gp7 = ([] for _ in range(10))
    tips_keep = [r for r in csv.DictReader(open(os.path.join(TAIL_DIR, "item_tooltip.csv"), encoding="utf-8"))
                 if r.get("_source") != SOURCE]
    # ★ 26.10.2026: строки asda_premium_box_add.py (метка «premium_box:») лежат ПОСЛЕ наших — сохраняются за ними.
    tips_foreign = [r for r in tips_keep if str(r.get("_source") or "").startswith("premium_box:")]
    tips_keep = [r for r in tips_keep if r not in tips_foreign]
    nbox = len(ENTRIES)
    fcodes = {r["ItemCode"] for r in tips_foreign}
    tip_row0 = len(tips_all) - sum(1 for r in tips_all if box_first <= int(r["ItemCode"]) < box_first + nbox)         - sum(1 for r in tips_all if r["ItemCode"] in fcodes)
    assert tip_row0 == int(tips_keep[-1]["_row"]) + 1, "хвост item_tooltip сдвинулся: %d" % tip_row0

    price_diff = []
    for k, (mcode, why) in enumerate(ENTRIES):
        code, pidx, link, row_i = box_first + k, pkg_first + k, link0 + k, len(items) + k
        sp = spec_all[mcode]
        price = ft_price[mcode]
        if int(sp["Price"]) != price:
            price_diff.append((mcode, int(sp["Price"]), price))
        name_ru = sp["Name"]
        name_en = en_spec.get("%s.Name" % sp["_row"])
        # item_list: запись 248 б из образца
        b = bytearray(bytes.fromhex(tpl["DataHex"]))
        struct.pack_into("<i", b, 0, code)
        put_str(b, 4, 64, name_ru)
        struct.pack_into("<i", b, 164, price)
        struct.pack_into("<i", b, 216, pidx)
        struct.pack_into("<h", b, 220, link)
        struct.pack_into("<i", b, 240, row_i)          # плейсхолдер imgIndex = номер строки (как у всех записей)
        row = {"_row": row_i}
        for (off, typ, nm) in cols_it:
            row[nm] = D.rd(b, off, typ)
        row["DataHex"] = bytes(b).hex()
        L_it.append(row)
        # item_img_table: иконка маунта
        g = bytearray(bytes.fromhex(tpl_img["DataHex"]))
        assert struct.unpack_from("<i", g, 0)[0] == TPL_CODE, "img-запись образца: +0 не код предмета"
        struct.pack_into("<i", g, 0, code)             # +0 img-записи = код предмета (как у всех строк)
        for off_a, off_c in ((132, 164), (172, 204)):
            put_str(g, off_a, 32, sp["IconPath"])
            struct.pack_into("<i", g, off_c, int(sp["IconCell"]))
        struct.pack_into("<i", g, 168, 0)              # IconSmallFlag 0 → сетка 8×64, как у special-иконки
        grow = {"_row": row_i}
        for (off, typ, nm) in cols_img:
            grow[nm] = D.rd(g, off, typ)
        grow["DataHex"] = bytes(g).hex()
        L_img.append(grow)
        # packagetable
        prow = {"_row": pidx, "Index": pidx, "Item0": mcode, "Qty0": 1}
        for j in range(1, 5):
            prow["Item%d" % j], prow["Qty%d" % j] = -1, 0
        prow.update({"Item5": 0, "Qty5": 0, "Reserved52": 0})
        assert [c[2] for c in cols_pkg] == [k2 for k2 in prow if k2 != "_row"], "колонки packagetable сменились"
        L_pkg.append(prow)
        # item_tooltip: описание = имя (как у оригинальных коробок)
        L_tip.append({"_row": tip_row0 + k, "ItemCode": code, "Description": name_ru, "_orig": "",
                      "_source": SOURCE})
        if name_en:   # нет EN у самого предмета — нет и у коробки (EN-клиент покажет RU, как у предмета)
            en_name.append({"key": "%d.ItemName" % row_i, "value": name_en, "was": None,
                            "why": "24.10.2026 премиум-коробка предмета %d: %s (Reports/fable_mounts)" % (mcode, why),
                            "source": SOURCE})
            en_tip.append({"key": "%d.Description" % (tip_row0 + k), "value": name_en, "was": None,
                           "why": "24.10.2026 премиум-коробка предмета %d" % mcode, "source": SOURCE})
        # сервер
        t = list(it_tpl)
        t[0], t[26], t[39], t[40] = str(code), str(price), str(pidx), str(link)
        it_new.append("\t".join(t).encode("ascii"))
        s = list(sc_tpl)
        s[0] = str(code)
        sc_new.append("\t".join(s).encode("ascii"))
        pk_new.append(("%d\t%d\t1\t-1\t0\t-1\t0\t-1\t0\t-1\t0" % (pidx, mcode)).encode("ascii"))
        gp7.append((mcode, code, price, name_en or ""))

    # ── клиентские ведомости ──
    def wcsv(name, rows, fields):
        # ★ 26.10.2026: строки asda_premium_box_add.py (колонка _source «premium_box:…») — после наших, без изменений.
        p = os.path.join(TAIL_DIR, name)
        foreign = [r for r in csv.DictReader(open(p, encoding="utf-8"))
                   if str(r.get("_source") or "").startswith("premium_box:")] if os.path.isfile(p) else []
        if name == "item_tooltip.csv":
            foreign = []                  # у подсказок чужие строки уже в rows (tips_foreign)
        if foreign:
            assert int(foreign[0]["_row"]) == int(rows[-1]["_row"]) + 1, "чужой хвост %s не продолжает наш" % name
            fields = fields + ["_source"] if "_source" not in fields else fields
            rows = [dict(r, _source=r.get("_source", "")) for r in rows] + foreign
        with open(p, "w", encoding="utf-8", newline="") as f:
            w = csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            w.writerows(rows)
    wcsv("item_list.csv", L_it, ["_row"] + [c[2] for c in cols_it] + ["DataHex"])
    wcsv("item_img_table.csv", L_img, ["_row"] + [c[2] for c in cols_img] + ["DataHex"])
    wcsv("packagetable.csv", L_pkg, ["_row"] + [c[2] for c in cols_pkg])
    assert [c[2] for c in cols_tip] == ["ItemCode", "Description"]
    wcsv("item_tooltip.csv", tips_keep + L_tip + tips_foreign, ["_row", "ItemCode", "Description", "_orig", "_source"])
    for fn, new in (("item_list.json", en_name), ("item_tooltip.json", en_tip)):
        p = os.path.join(LOC_EN, fn)
        raw = open(p, "rb").read()
        doc = json.loads(raw.decode("utf-8"))
        keep = [r for r in doc["rows"] if r.get("source") != SOURCE]
        clash = {r["key"] for r in keep} & {r["key"] for r in new}
        assert not clash, "ключи EN уже заняты: %s" % sorted(clash)[:5]
        doc["rows"] = keep + new
        nl = "\r\n" if b"\r\n" in raw[:4096] else "\n"
        with open(p, "w", encoding="utf-8", newline=nl) as f:     # перевод строк — как был до правки
            json.dump(doc, f, ensure_ascii=False, indent=2)
            f.write("\n")

    # ── серверные файлы ──
    n_items = len(it_body) + len(it_new) + len(it_foreign)    # ★ 26.10.2026: + чужие строки после нашего диапазона
    for d in SRV_GS_DIRS:
        os.makedirs(os.path.join(SRV_OUT, d), exist_ok=True)
        write_srv(os.path.join(SRV_OUT, d, "item_table.txt"), n_items, it_body + it_new + it_foreign)
        write_srv(os.path.join(SRV_OUT, d, "SowelCombine.txt"), n_items, sc_body + sc_new + sc_foreign)
        write_srv(os.path.join(SRV_OUT, d, "PackageItem_Table.txt"), len(pk_body) + len(pk_new) + len(pk_foreign),
                  pk_body + pk_new + pk_foreign)
    mp = os.path.join(SRV_IN, "gp7-server", "premium_item_map.json")
    mraw = open(mp, "rb").read()
    m = json.loads(mraw.decode("utf-8-sig"))
    for mcode, code, price, _ in gp7:
        old = m.get(str(mcode))
        assert old is None or old == {"deliveryItemCode": code, "price": price}, "ключ %d в карте занят: %r" % (mcode, old)
        m[str(mcode)] = {"deliveryItemCode": code, "price": price}
    os.makedirs(os.path.join(SRV_OUT, "gp7-server"), exist_ok=True)
    nl = "\r\n" if b"\r\n" in mraw else "\n"
    with open(os.path.join(SRV_OUT, "gp7-server", "premium_item_map.json"), "w", encoding="utf-8", newline=nl) as f:
        json.dump(m, f, indent=2)
        f.write("\n")
    with open(os.path.join(SRV_DIR, "boxes.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["item_code", "box_code", "package", "link", "price", "client_price", "name_ru", "name_en", "atlas",
                    "cell", "kind"])
        for k, (mcode, code, price, name_en) in enumerate(gp7):
            sp = spec_all[mcode]
            w.writerow([mcode, code, pkg_first + k, link0 + k, price, sp["Price"], sp["Name"], name_en,
                        sp["IconPath"], sp["IconCell"], ENTRIES[k][1]])
    print("коробки %d…%d, пакеты %d…%d, ключ связи %d…%d; item_table/SowelCombine %d → %d, PackageItem %d → %d"
          % (box_first, box_first + len(gp7) - 1, pkg_first, pkg_first + len(gp7) - 1, link0, link0 + len(gp7) - 1,
             it_cnt, n_items, pk_cnt, len(pk_body) + len(pk_new)))
    print("состав: маунтов %d + оригинальных %d = %d новых коробок; только записей в карте (готовая коробка): 0"
          % (MOUNT_LAST - MOUNT_FIRST + 1, len(orig), len(ENTRIES)))
    print("цена клиента (special_itemlist) ≠ серверной Function_Table: %d %s" % (len(price_diff), price_diff[:12]))
    print("ведомости: item_list/img/packagetable/item_tooltip; EN item_list/item_tooltip +%d/+%d; карта шлюза +%d"
          % (len(en_name), len(en_tip), len(gp7)))

# ── стенд: повтор проверок GameServer над построенными файлами ─────────────────────────────────────
def verify(base=SRV_OUT, label="srv_out"):
    """Повторяет то, что сделает GameServer со srv_out: загрузчики (счёт/подряд/42 и 57 токенов), стартовые
    гейты (item_table == SowelCombine; +63 коробки ≤ счёта пакетов) и путь покупки для КАЖДОГО ключа карты шлюза
    — для ВСЕХ функциональных кодов витрины: sub_532A90(код коробки) → +26 11 / +27 200 → пакет[+63].index == +63;
    у коробок этого генератора ещё содержимое = (предмет, 1) в границе sub_532B10 и tCash (+41) = цена карты =
    цена Function_Table. Плюс: прежние строки всех таблиц — байт в байт как в снимке."""
    errs = []
    fn_cnt, ft_body = read_srv(os.path.join(SRV_IN, "GameServer", "Function_Table.txt"))   # счёт = dword_5F5EA4 (sub_532B10)
    ft_price = {}
    for ln in ft_body:
        t = ln.decode("latin1").split()
        if len(t) > 20 and t[0].isdigit():
            ft_price[int(t[0])] = int(t[20])
    # Все функциональные коды витрины 6000 (PremiumShopBin.json = архив + хвост): каждый обязан покупаться.
    shop = json.load(open(os.path.join(PORT, "AstoryClient", "RawAssets", "Data", "NPC", "PremiumShopBin.json"),
                          encoding="utf-8-sig"))
    shop_codes = sorted({int(r["ItemId"]) for r in shop if 0 < int(r["ItemId"]) <= fn_cnt})
    m = json.load(open(os.path.join(base, "gp7-server", "premium_item_map.json"), encoding="utf-8-sig"))
    for d in SRV_GS_DIRS:
        it_cnt, it = read_srv(os.path.join(base, d, "item_table.txt"))
        sc_cnt, sc = read_srv(os.path.join(base, d, "SowelCombine.txt"))
        pk_cnt, pk = read_srv(os.path.join(base, d, "PackageItem_Table.txt"))
        for nm, cnt, body, ntok in (("item_table", it_cnt, it, 42), ("SowelCombine", sc_cnt, sc, 57)):
            if cnt != len(body):
                errs.append("%s/%s: счёт %d ≠ строк %d" % (d, nm, cnt, len(body)))
            for i, ln in enumerate(body):
                t = toks(ln)
                if int(t[0]) != ITEM_FIRST + i:
                    errs.append("%s/%s: строка %d код %s (Sync Error)" % (d, nm, i, t[0])); break
                if len(t) != ntok:
                    errs.append("%s/%s: код %s токенов %d" % (d, nm, t[0], len(t))); break
        if it_cnt != sc_cnt:
            errs.append("%s: item_table %d != SowelCombine %d (MessageBox на старте)" % (d, it_cnt, sc_cnt))
        if pk_cnt != len(pk):
            errs.append("%s/PackageItem: счёт %d ≠ строк %d" % (d, pk_cnt, len(pk)))
        pkg = {}
        for i, ln in enumerate(pk):
            t = [int(x) for x in toks(ln)]
            pkg[i] = t
        maxcode = it_cnt + ITEM_FIRST - 1                     # dword_5F5EAC
        rows = {int(toks(ln)[0]): toks(ln) for ln in it}
        for code, t in rows.items():                          # стартовый гейт GameServer.c:75250
            if t[17] in ("200", "201") and int(t[39]) > pk_cnt:
                errs.append("%s: коробка %d пакет %s > %d (MessageBox на старте)" % (d, code, t[39], pk_cnt))
        ok = ours = 0
        for mcode in shop_codes:
            e = m.get(str(mcode))
            if not e:
                errs.append("карта шлюза: нет ключа %d (функц. предмет витрины не купить)" % mcode); continue
            c = e["deliveryItemCode"]
            if not (ITEM_FIRST <= c <= maxcode):
                errs.append("%s: %d → %d не проходит sub_532A90 (≤ %d)" % (d, mcode, c, maxcode)); continue
            if c < OUR_FIRST:      # запись сайта (81 прежняя): GameServer требует только sub_532A90 (у 9/17/20 там 11/66)
                ok += 1
                continue
            t = rows[c]
            p = int(t[39])
            if t[16] != "11" or t[17] not in ("200", "201"):
                errs.append("%s: %d не коробка (%s/%s)" % (d, c, t[16], t[17])); continue
            if p < 0 or p >= pk_cnt or pkg[p][0] != p:
                errs.append("%s: %d пакет %d не проходит проверки 8/9 Recv_OpenPackageItem" % (d, c, p)); continue
            items = [(pkg[p][1 + 2 * j], pkg[p][2 + 2 * j]) for j in range(5)]
            ours += 1
            if not (1 <= mcode <= fn_cnt):
                errs.append("%s: маунт %d вне Function_Table (1…%d) — sub_55B7F0 вернёт 0" % (d, mcode, fn_cnt)); continue
            if items[0] != (mcode, 1) or any(x > 0 for x, _ in items[1:]):
                errs.append("%s: пакет %d = %r, ждали (%d, 1)" % (d, p, items, mcode)); continue
            if int(t[26]) != e["price"] or e["price"] != ft_price.get(mcode):
                errs.append("%s: цена %d: +41 коробки %s, карта %d, Function_Table %s" % (d, mcode, t[26], e["price"],
                                                                                      ft_price.get(mcode))); continue
            ok += 1
        print("%-9s %-17s item_table/SowelCombine %d, пакетов %d, maxcode %d; функц. кодов витрины %d, путь покупки OK %d "
              "(из них наших коробок %d)" % (label, d, it_cnt, pk_cnt, maxcode, len(shop_codes), ok, ours))
        # прежние строки — байт в байт
        for t in SRV_TABLES:
            a = open(os.path.join(SRV_IN, stack_layout.twin_of(d), t), "rb").read()   # 26.10: у копии Ch2 — снимок её поля
            b = open(os.path.join(base, d, t), "rb").read()
            ha, hb = a.split(b"\r\n", 1)[1], b.split(b"\r\n", 1)[1]
            if not hb.startswith(ha):
                errs.append("%s/%s: прежние строки изменены" % (d, t))
    a = json.load(open(os.path.join(SRV_IN, "gp7-server", "premium_item_map.json"), encoding="utf-8-sig"))
    if any(m.get(k) != v for k, v in a.items()):
        errs.append("карта шлюза: прежние ключи изменены")
    print("стенд: ошибок %d" % len(errs))
    for x in errs[:20]:
        print("  !", x)
    return not errs


def main(argv):
    if "--fetch-server" in argv:
        fetch_server()
    if "--apply-server" in argv:
        if not verify():
            raise SystemExit("стенд не прошёл — запись отменена")
        apply_server()
        return
    if "--verify-live" in argv:      # перечитать стойку по SSH во временный снимок и прогнать стенд
        live = os.path.join(SRV_DIR, "srv_live")
        for rel, loc in srv_files():
            p = os.path.join(live, loc)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            _scp("%s:%s/%s" % (SSH_HOST, SRV_ROOT, rel), p)
        raise SystemExit(0 if verify(live, "srv_live") else 2)
    build()
    if not verify():
        raise SystemExit(2)


if __name__ == "__main__":
    main(sys.argv[1:])
