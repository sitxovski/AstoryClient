# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_pak_verify.py — ★ ПРОВЕРКА ШИФРОВАНИЯ И ПОДПИСИ КАЖДОГО КОНТЕЙНЕРА СБОРКИ (16.10.2026, система обновлений, волна 1).
#
# ЗАЧЕМ. После разбивки контента на чанки (Source/AsdaReborn/Public/Resources/AsdaAssetManager.h) вместо одного
#   AsdaReborn-Windows.ucas в сборке десятки контейнеров pakchunk<N>-Windows.*. Требование владельца: доказать, что
#   шифрование работает на КАЖДОМ, а не «флаг где-то стоит». Разбор формата — Reports/anticheat/wave_B2_pak_crypto.md §1.
#
# ЧТО ПРОВЕРЯЕТ (любой провал — код 1):
#   .utoc (заголовок — Engine/Source/Runtime/Core/Internal/IO/IoStore.h FIoStoreTocHeader; флаги — IoDispatcher.h
#          EIoContainerFlags):
#     1. флаги Encrypted|Signed|Indexed у каждого игрового контейнера (global.utoc — только Signed: движок его не шифрует);
#     2. GUID ключа = 0 (основной ключ из делегата AsdaPakKeyProvider);
#     3. выборка СЖАТЫХ блоков .ucas (метод ≠ 0), двусторонний тест: в сырых байтах заголовка Oodle почти нет (случайный
#        уровень ~0,09 % — замер ревизии на 290 412 блоках; без шифра было бы ~100 %, провал — от 50 %), после AES-256-ECB
#        ключом из Config/DefaultCrypto.ini он есть (≥ 98 %). Заголовок блока
#        Oodle: байт0 = 0x8C/0xCC, байт1 = номер компрессора ≤ 0x0F (у нас 0x06 Kraken). Энтропию НЕ используем: сжатые
#        данные почти случайны и без шифра. Несжатые блоки (метод 0 — движок хранит так несжимаемое) структуры не имеют —
#        только считаются;
#     4. SHA-1 каждого выбранного блока на диске совпадает с таблицей хэшей TOC (подпись покрывает именно шифроблоки).
#   .pak (подвал FPakInfo, magic 0x5A6F12E1): bEncryptedIndex = 1 и рядом лежит .sig.
#   Без ключа (--no-key) — только пункты 1, 2, 4 и .pak.
#
# ЗАПУСК (обычный python; при импорте ничего не делает — закон tools-no-import-side-effects):
#   python Tools/asda_pak_verify.py <корень игры | папка Paks>            — проверка с ключом из Config/DefaultCrypto.ini
#   python Tools/asda_pak_verify.py <…> --no-key                          — без ключа
#   python Tools/asda_pak_verify.py <…> --samples 64                      — блоков на контейнер (по умолчанию 32)
#   python Tools/asda_pak_verify.py <…> --json out.json                   — итог в файл (для сравнения сборок)
#   python Tools/asda_pak_verify.py --selftest                            — самотест разбора на синтетике
# Ключ скрипт НЕ печатает и никуда не пишет.

import argparse
import base64
import hashlib
import json
import math
import os
import random
import re
import struct
import sys

# Корень проекта (папка над Tools)
HERE = os.path.dirname(os.path.abspath(__file__))
# Файл ключей сборки (генерирует Tools/gen_build_crypto.py; в раздачу не попадает)
CRYPTO_INI = os.path.join(os.path.dirname(HERE), "Config", "DefaultCrypto.ini")

# Магия заголовка TOC IoStore
TOC_MAGIC = b"-==--==--==--==-"
# Магия подвала .pak
PAK_MAGIC = 0x5A6F12E1
# Флаги EIoContainerFlags
F_COMPRESSED, F_ENCRYPTED, F_SIGNED, F_INDEXED = 1, 2, 4, 8
# Первые байты блока Oodle (LZ-заголовок: 0x8C — обычный, 0xCC — с флагом сброса)
OODLE_FIRST = (0x8C, 0xCC)
# Доля сжатых блоков, у которых ключ обязан вскрыть заголовок Oodle; и предел случайных «заголовков» в сырых байтах
# ★ ревизия 16.10: порог 2 % давал ложные провалы на выборке 32–64 блока (один случайный «заголовок» = 3 %) — теперь 50 %
DECRYPT_MIN_SHARE, RAW_MAX_SHARE = 0.98, 0.5


def entropy(buf):
    """Энтропия Шеннона, бит на байт."""
    if not buf:
        return 0.0
    cnt = [0] * 256
    for b in buf:
        cnt[b] += 1
    n = float(len(buf))
    return -sum(c / n * math.log2(c / n) for c in cnt if c)


def is_oodle(buf):
    """Похоже на заголовок блока Oodle: байт0 0x8C/0xCC, байт1 — номер компрессора (≤ 0x0F)."""
    return len(buf) >= 2 and buf[0] in OODLE_FIRST and (buf[1] & 0x7F) <= 0x0F


def load_key(path=CRYPTO_INI):
    """AES-ключ из DefaultCrypto.ini (base64, 32 байта) или None."""
    if not os.path.isfile(path):
        return None
    m = re.search(r"^EncryptionKey=(\S+)", open(path, encoding="utf-8-sig").read(), re.M)
    if not m:
        return None
    key = base64.b64decode(m.group(1))
    return key if len(key) == 32 else None


def aes_ecb_decrypt(key, data):
    """AES-256-ECB, как FAES::DecryptData движка (длина кратна 16)."""
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    d = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    return d.update(data) + d.finalize()


def parse_toc(data):
    """Разбор .utoc: заголовок, блоки (смещение, сжатый/исходный размер, метод), хэши блоков. Словарь."""
    if data[:16] != TOC_MAGIC:
        raise ValueError("нет магии TOC")
    u32 = lambda o: struct.unpack_from("<I", data, o)[0]
    hsz, n, nblk, blk_esz = u32(20), u32(24), u32(28), u32(32)
    ncm, cml = u32(36), u32(40)
    flags = data[80]
    guid = data[64:80]
    nseeds, nnoph = u32(84), u32(96)
    if blk_esz != 12:
        raise ValueError("размер записи блока %d != 12" % blk_esz)
    off = hsz + 12 * n + 10 * n + 4 * nseeds + 4 * nnoph
    blocks = []
    for i in range(nblk):
        e = data[off + 12 * i: off + 12 * i + 12]
        o = int.from_bytes(e[0:5], "little")
        csz = int.from_bytes(e[5:8], "little")
        usz = int.from_bytes(e[8:11], "little")
        blocks.append((o, csz, usz, e[11]))
    off += 12 * nblk
    methods = [data[off + i * cml: off + (i + 1) * cml].split(b"\0")[0].decode("ascii", "replace") for i in range(ncm)]
    off += ncm * cml
    hashes = None
    if flags & F_SIGNED:
        hs = struct.unpack_from("<i", data, off)[0]
        off += 4 + 2 * hs
        hashes = [data[off + 20 * i: off + 20 * i + 20] for i in range(nblk)]
    return {"flags": flags, "guid_zero": guid == b"\0" * 16, "entries": n, "blocks": blocks,
            "methods": methods, "hashes": hashes}


def flags_text(f):
    """Флаги контейнера словами."""
    names = [(F_COMPRESSED, "Compressed"), (F_ENCRYPTED, "Encrypted"), (F_SIGNED, "Signed"), (F_INDEXED, "Indexed")]
    return "|".join(n for b, n in names if f & b) or "None"


def check_container(utoc, key, samples, rng):
    """Проверка одного .utoc/.ucas. (ok, строки отчёта, сводка)."""
    name = os.path.basename(utoc)
    is_global = name.lower().startswith("global")
    lines, ok = [], True
    toc = parse_toc(open(utoc, "rb").read())
    f = toc["flags"]
    need = F_SIGNED if is_global else (F_ENCRYPTED | F_SIGNED | F_INDEXED)
    if f & need != need:
        ok = False
        lines.append("  ОШИБКА флаги %s, нужно %s" % (flags_text(f), flags_text(need)))
    if not toc["guid_zero"]:
        ok = False
        lines.append("  ОШИБКА GUID ключа не нулевой (контейнер ждёт вторичный ключ)")
    ucas = utoc[:-5] + ".ucas"
    summary = {"name": name, "flags": flags_text(f), "blocks": len(toc["blocks"]), "entries": toc["entries"],
               "ucas_size": os.path.getsize(ucas) if os.path.isfile(ucas) else None}
    blocks = toc["blocks"]
    pick = rng.sample(range(len(blocks)), min(samples, len(blocks))) if blocks else []
    comp, raw_hdr, raw_only, dec_hits, hash_bad, checked = 0, 0, 0, 0, 0, 0
    if pick and os.path.isfile(ucas):
        with open(ucas, "rb") as fh:
            for i in sorted(pick):
                o, csz, usz, meth = blocks[i]
                disk = (csz + 15) & ~15  # движок ВСЕГДА выравнивает блок до 16 (IoStoreWriter.cpp EncryptAndSign), хэш — по выровненному
                fh.seek(o)
                buf = fh.read(disk)
                checked += 1
                if toc["hashes"] is not None and hashlib.sha1(buf).digest() != toc["hashes"][i]:
                    hash_bad += 1
                if not (f & F_ENCRYPTED) or meth == 0:
                    raw_only += meth == 0
                    continue
                comp += 1
                raw_hdr += is_oodle(buf)
                if key:
                    dec_hits += is_oodle(aes_ecb_decrypt(key, buf[:16]))
    if hash_bad:
        ok = False
        lines.append("  ОШИБКА SHA-1 блоков не совпали с таблицей TOC: %d из %d" % (hash_bad, checked))
    if f & F_ENCRYPTED and checked:
        summary["compressed_sampled"], summary["stored_sampled"] = comp, raw_only
        summary["raw_oodle"] = "%d/%d" % (raw_hdr, comp)
        if comp == 0:
            ok = False
            lines.append("  ОШИБКА в выборке нет сжатых блоков — нечем доказать шифрование (увеличь --samples)")
        elif raw_hdr > RAW_MAX_SHARE * comp:
            ok = False
            lines.append("  ОШИБКА заголовок Oodle виден БЕЗ ключа в %d из %d блоков — данные не зашифрованы" % (raw_hdr, comp))
        if key is not None and comp:
            summary["decrypt_struct"] = "%d/%d" % (dec_hits, comp)
            if dec_hits < DECRYPT_MIN_SHARE * comp:
                ok = False
                lines.append("  ОШИБКА ключ сборки не вскрывает блоки: Oodle в %d из %d" % (dec_hits, comp))
    lines.insert(0, "%s %s  блоков %d  выборка %d  %s" % ("ОК    " if ok else "ПРОВАЛ", name, len(blocks), checked,
                                                         flags_text(f)) +
                 ("  без ключа Oodle %s" % summary["raw_oodle"] if "raw_oodle" in summary else "") +
                 ("  с ключом %s" % summary["decrypt_struct"] if "decrypt_struct" in summary else "") +
                 ("  несжатых %d" % summary["stored_sampled"] if summary.get("stored_sampled") else ""))
    return ok, lines, summary


def check_pak(pak):
    """Проверка .pak: индекс зашифрован, рядом .sig. (ok, строки, сводка)."""
    name = os.path.basename(pak)
    size = os.path.getsize(pak)
    with open(pak, "rb") as fh:
        fh.seek(max(0, size - 512))
        tail = fh.read()
    pos = tail.rfind(struct.pack("<I", PAK_MAGIC))
    lines, ok = [], True
    if pos < 1:
        return False, ["ПРОВАЛ %s: нет подвала FPakInfo" % name], {"name": name}
    enc_index = tail[pos - 1]
    sig = os.path.isfile(pak[:-4] + ".sig")
    if enc_index != 1:
        ok = False
        lines.append("  ОШИБКА индекс не зашифрован (bEncryptedIndex=%d)" % enc_index)
    if not sig:
        ok = False
        lines.append("  ОШИБКА нет .sig рядом")
    lines.insert(0, "%s %s  %d байт  индекс зашифрован=%s  .sig=%s" % ("ОК    " if ok else "ПРОВАЛ", name, size,
                                                                     enc_index == 1, sig))
    return ok, lines, {"name": name, "size": size, "encrypted_index": enc_index == 1, "sig": sig}


def find_paks(root):
    """Папка Paks внутри корня игры (или сам путь, если это она)."""
    for cand in (root, os.path.join(root, "AsdaReborn", "Content", "Paks"),
                 os.path.join(root, "Windows", "AsdaReborn", "Content", "Paks")):
        if os.path.isdir(cand) and any(f.lower().endswith(".utoc") for f in os.listdir(cand)):
            return cand
    raise SystemExit("не найдена папка Paks с .utoc в %s" % root)


def run(root, use_key=True, samples=32, json_out=None, seed=20261016):
    """Проверка всех контейнеров папки. Код возврата 0/1."""
    paks = find_paks(root)
    key = load_key() if use_key else None
    if use_key and key is None:
        print("ОШИБКА: ключ не найден в %s (или запусти с --no-key)" % CRYPTO_INI)
        return 1
    rng = random.Random(seed)
    files = sorted(os.listdir(paks))
    all_ok, report = True, {"paks": paks, "containers": [], "pak_files": []}
    print("Папка: %s   ключ: %s" % (paks, "есть (не печатается)" if key else "не используется"))
    for fn in files:
        p = os.path.join(paks, fn)
        if fn.lower().endswith(".utoc"):
            ok, lines, s = check_container(p, key, samples, rng)
            report["containers"].append(dict(s, ok=ok))
        elif fn.lower().endswith(".pak"):
            ok, lines, s = check_pak(p)
            report["pak_files"].append(dict(s, ok=ok))
        else:
            continue
        all_ok &= ok
        print("\n".join(lines))
    print("ИТОГ: контейнеров %d, pak %d — %s" % (len(report["containers"]), len(report["pak_files"]),
                                                "ВСЁ ЗАЩИЩЕНО" if all_ok else "ЕСТЬ ПРОВАЛЫ"))
    if json_out:
        report["ok"] = all_ok
        json.dump(report, open(json_out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    return 0 if all_ok else 1


def selftest():
    """Синтетический TOC: разбор полей и блоков, энтропия, расшифровка известным ключом."""
    key = bytes(range(32))
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    plain = bytes([0x8C, 0x06]) + b"A" * 30
    enc = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    ct = enc.update(plain) + enc.finalize()
    assert aes_ecb_decrypt(key, ct) == plain and is_oodle(plain) and not is_oodle(bytes([0x8C, 0x40]))
    hdr = bytearray(144)
    hdr[:16] = TOC_MAGIC
    struct.pack_into("<IIIIIII", hdr, 20, 144, 1, 1, 12, 1, 32, 65536)
    hdr[80] = F_COMPRESSED | F_ENCRYPTED | F_SIGNED | F_INDEXED
    body = bytes(12) + bytes(10)
    blk = (0).to_bytes(5, "little") + (32).to_bytes(3, "little") + (65536).to_bytes(3, "little") + bytes([1])
    meth = b"Oodle".ljust(32, b"\0")
    sig = struct.pack("<i", 4) + b"S" * 8 + hashlib.sha1(ct).digest()
    toc = parse_toc(bytes(hdr) + body + blk + meth + sig)
    assert toc["flags"] == 15 and toc["guid_zero"] and toc["blocks"] == [(0, 32, 65536, 1)]
    assert toc["methods"] == ["Oodle"] and toc["hashes"][0] == hashlib.sha1(ct).digest()
    assert entropy(bytes(range(256)) * 4) == 8.0 and entropy(b"\0" * 64) == 0.0
    print("самотест: ВСЁ ВЕРНО")
    return 0


def main(argv=None):
    """Точка входа."""
    ap = argparse.ArgumentParser(description="Проверка шифрования/подписи контейнеров сборки (каждого pakchunk)")
    ap.add_argument("root", nargs="?", help="корень игры (…\\Windows) или папка Paks")
    ap.add_argument("--no-key", action="store_true", help="без ключа: флаги, хэши блоков, .pak")
    ap.add_argument("--samples", type=int, default=32, help="блоков на контейнер (по умолчанию 32)")
    ap.add_argument("--json", help="итог в JSON")
    ap.add_argument("--selftest", action="store_true", help="самотест разбора")
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.root:
        ap.error("нужен путь к сборке")
    return run(a.root, not a.no_key, a.samples, a.json)


if __name__ == "__main__":
    sys.exit(main())
