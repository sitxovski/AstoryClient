# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# gen_build_crypto.py — ★ КЛЮЧИ СБОРКИ КЛИЕНТА: новый AES-256 ключ шифрования pak/IoStore и пара RSA подписи
#   НА КАЖДУЮ СБОРКУ (решение владельца 11.10.2026, «Дополнение 11.10» в Reports/anticheat/00_PLAN.md;
#   мера 5 плана античита; волна В2, 12.10.2026).
#
# ЧТО ПИШЕТ (оба файла перезаписываются при каждом запуске, до записи — копия в @@ASTORY_DEV_ROOT@@\_backup\<дата>_pakcrypto\…
#   плюс снимок с меткой времени — чтобы ключ ЛЮБОЙ прошлой сборки можно было найти):
#   1. Config/DefaultCrypto.ini — в формате, который читают UBT (EncryptionAndSigning.cs:323–400) и UAT/UnrealPak
#      (CopyBuildToStagingDirectory.cs:4720 → Metadata/Crypto.json → -cryptokeys/-sign/-encryptindex):
#      секция [/Script/CryptoKeys.CryptoKeysSettings], ключи base64; RSA-числа — base64 LITTLE-ENDIAN байтов
#      (так их пишет плагин CryptoKeys: CryptoKeysOpenSSL.cpp:40–49 BN_bn2bin + Algo::Reverse; читает движок через
#      BN_lebin2bn — PlatformCryptoUtilsOpenSSL.cpp:18–28). Экспонента 65537, модуль 4096 бит (как у плагина,
#      CryptoKeysHelpers.h:26).
#   2. Source/AsdaCore/Private/Crypto/AsdaPakKey.generated.h — ключ AES, разложенный на 4 XOR-доли; каждая доля
#      хранится ПЕРЕСТАВЛЕННОЙ (случайная перестановка 32 позиций) и замаскированной потоком xorshift64 от своего
#      seed. В исполняемом файле ключа в открытом виде НЕТ: его восстанавливает AsdaPakKey::RestoreEncryptionKey
#      (Source/AsdaCore/Private/Crypto/AsdaPakKeyProvider.cpp) только в памяти, когда движок просит ключ через
#      FCoreDelegates::GetPakEncryptionKeyDelegate. Открытый ключ подписи (экспонента+модуль) лежит в заголовке
#      как есть — он открытый. Отпечаток (первые 8 байт SHA-1 ключа) — для самопроверки восстановления.
#
# ПОЧЕМУ ключ не встраивается движком: UBT (TargetRules.cs:3434–3459) превращает EncryptionKey из DefaultCrypto.ini
#   в макрос IMPLEMENT_ENCRYPTION_KEY_REGISTRATION()=UE_REGISTER_ENCRYPTION_KEY(0x..,…) для всех модулей проекта
#   (UEBuildModuleCPP.cs:2831), а раскрывается он в IMPLEMENT_PRIMARY_GAME_MODULE (ModuleManager.h:1037–1133) —
#   32 байта ключа в открытом виде в .exe. В Source/AsdaReborn/Private/AsdaReborn.cpp оба макроса сняты (#undef),
#   и делегаты движка привязывает наш регистрант. Сам DefaultCrypto.ini в сборку не попадает в полезном виде:
#   ключи EncryptionKey/Signing* вырезаются из стейджинга (Engine/Config/BaseGame.ini:145–152 IniKeyDenylist).
#
# ЗАПУСК (обычный python, unreal НЕ нужен; закон tools-no-import-side-effects — при импорте ничего не делает):
#   python Tools/gen_build_crypto.py                 — новые ключи, оба файла (с бэкапом)
#   python Tools/gen_build_crypto.py --check         — состояние: файлы, совпадение ini↔заголовок, ключ в exe/стейдже
#   python Tools/gen_build_crypto.py --build-id X    — свой идентификатор набора (по умолчанию ГГГГММДД-ЧЧММСС-xxxxxx)
#   python Tools/gen_build_crypto.py --rsa-bits 2048 — короче ключ подписи (по умолчанию 4096, как у плагина)
#   python Tools/gen_build_crypto.py --no-encrypt-ini — не шифровать .ini в паке (по умолчанию шифруются)
#   python Tools/gen_build_crypto.py --print-uat     — показать команду упаковки
#
# ПОРЯДОК СБОРКИ РЕЛИЗА: редактор ЗАКРЫТ → этот скрипт → RunUAT BuildCookRun … -build (UBT пересоберёт модули
#   проекта: Definitions.*.h меняется вместе с ключом) → архив. Ключ прошлой сборки — в снимке бэкапа.
import argparse
import base64
import datetime
import hashlib
import os
import secrets
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(HERE)                                   # @@ASTORY_DEV_ROOT@@\AstoryClient
CRYPTO_INI = os.path.join(PROJECT_DIR, "Config", "DefaultCrypto.ini")
GEN_HEADER = os.path.join(PROJECT_DIR, "Source", "AsdaCore", "Private", "Crypto", "AsdaPakKey.generated.h")
BACKUP_SET = "pakcrypto"
INI_SECTION = "/Script/CryptoKeys.CryptoKeysSettings"
N_SHARES = 4
KEY_LEN = 32
MASK64 = (1 << 64) - 1

# Пустое значение в ini = None (ключ отсутствует); флаги — строки True/False как пишет UCLASS config.
DEFAULT_FLAGS = {
    "bEnablePakSigning": True,       # подпись .pak (.sig) и IoStore (TOC + SHA-1 каждого блока)
    "bEncryptPakIniFiles": True,     # .ini в паке (адреса серверов и т.п.)
    "bEncryptPakIndex": True,        # индекс .pak (-encryptindex): список файлов не виден UnrealPak без ключа
    "bEncryptUAssetFiles": True,     # .uasset → в IoStore контейнер с такими файлами шифруется ЦЕЛИКОМ
    "bEncryptAllAssetFiles": False,  # .uexp/.ubulk отдельно — при IoStore уже покрыто флагом выше
}


# ── вспомогательное ────────────────────────────────────────────────────────────────────────────────────────────
def _le_bytes(n):
    """Целое → little-endian байты минимальной длины (как BigNumToArray плагина: BN_num_bytes, без дополнения)."""
    return n.to_bytes((n.bit_length() + 7) // 8, "little")


def _b64(b):
    return base64.b64encode(b).decode("ascii")


def _xorshift64_mask(seed, n=KEY_LEN):
    """Поток маски: xorshift64 от seed, по одному байту из слова на позицию (та же формула в AsdaPakKeyProvider.cpp)."""
    x = seed & MASK64
    out = bytearray(n)
    for i in range(n):
        x ^= (x << 13) & MASK64
        x ^= x >> 7
        x ^= (x << 17) & MASK64
        out[i] = (x >> ((i % 8) * 8)) & 0xFF
    return bytes(out)


def _fingerprint(key):
    return hashlib.sha1(key).digest()[:8]


def split_key(key, rng=secrets):
    """Ключ → N_SHARES долей: key = XOR всех долей; каждая доля хранится как (seed, perm, part):
    part[i] = share[perm[i]] ^ mask(seed)[i]. Возвращает список кортежей (seed, perm, part)."""
    assert len(key) == KEY_LEN
    shares = [bytes(rng.token_bytes(KEY_LEN)) for _ in range(N_SHARES - 1)]
    acc = bytearray(key)
    for s in shares:
        for i in range(KEY_LEN):
            acc[i] ^= s[i]
    shares.append(bytes(acc))                       # последняя доля замыкает XOR до ключа
    parts = []
    for s in shares:
        seed = int.from_bytes(rng.token_bytes(8), "little") | 1   # ненулевой seed (xorshift от 0 = нули)
        perm = list(range(KEY_LEN))
        # перестановка Фишера–Йетса на secrets
        for i in range(KEY_LEN - 1, 0, -1):
            j = rng.randbelow(i + 1)
            perm[i], perm[j] = perm[j], perm[i]
        mask = _xorshift64_mask(seed)
        part = bytes(s[perm[i]] ^ mask[i] for i in range(KEY_LEN))
        parts.append((seed, perm, part))
    return parts


def restore_key(parts):
    """Обратная операция (эмуляция AsdaPakKey::RestoreEncryptionKey) — для --check и самопроверки генератора."""
    key = bytearray(KEY_LEN)
    for seed, perm, part in parts:
        mask = _xorshift64_mask(seed)
        for i in range(KEY_LEN):
            key[perm[i]] ^= part[i] ^ mask[i]
    return bytes(key)


def gen_rsa(bits):
    """Пара RSA через пакет cryptography (есть в окружении); возвращает (e, d, n)."""
    from cryptography.hazmat.primitives.asymmetric import rsa
    priv = rsa.generate_private_key(public_exponent=65537, key_size=bits)
    nums = priv.private_numbers()
    return nums.public_numbers.e, nums.d, nums.public_numbers.n


def make_build_id():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + secrets.token_hex(3)


# ── текст файлов ───────────────────────────────────────────────────────────────────────────────────────────────
def render_ini(build_id, aes_key, e, d, n, flags):
    lines = [
        "; ★ ГЕНЕРИРУЕТСЯ Tools/gen_build_crypto.py — НЕ ПРАВИТЬ РУКАМИ; ключи статические, новые — только при major-версии.",
        "; AsdaPakKeyBuildId=%s" % build_id,
        "; Ключи отсюда читают UBT (макросы сняты в AsdaReborn.cpp — в exe ключ не попадает) и UAT/UnrealPak",
        "; (Crypto.json → шифрование и подпись pak/IoStore). В стейджинг ключи не идут: IniKeyDenylist движка.",
        "",
        "[%s]" % INI_SECTION,
        "EncryptionKey=%s" % _b64(aes_key),
        "SigningPublicExponent=%s" % _b64(_le_bytes(e)),
        "SigningModulus=%s" % _b64(_le_bytes(n)),
        "SigningPrivateExponent=%s" % _b64(_le_bytes(d)),
    ]
    for k in ("bEnablePakSigning", "bEncryptPakIniFiles", "bEncryptPakIndex", "bEncryptUAssetFiles",
              "bEncryptAllAssetFiles"):
        lines.append("%s=%s" % (k, "True" if flags[k] else "False"))
    return "\n".join(lines) + "\n"


def _c_array(name, data, per_line=16):
    rows = []
    for off in range(0, len(data), per_line):
        rows.append("\t" + ", ".join("0x%02X" % b for b in data[off:off + per_line]) + ",")
    return "static const uint8 %s[%d] =\n{\n%s\n};\n" % (name, len(data), "\n".join(rows))


def render_header(build_id, parts, fingerprint, e, n, flags):
    out = []
    out.append("// AsdaPakKey.generated.h — ★ ГЕНЕРИРУЕТСЯ Tools/gen_build_crypto.py, НЕ ПРАВИТЬ РУКАМИ.")
    out.append("//   Ключ шифрования pak/IoStore этой сборки в виде %d замаскированных долей + открытый ключ подписи." % N_SHARES)
    out.append("//   Восстановление — только в памяти: AsdaPakKey::RestoreEncryptionKey (AsdaPakKeyProvider.cpp).")
    out.append("//   Набор ключей: %s (та же метка в Config/DefaultCrypto.ini)." % build_id)
    out.append("#pragma once")
    out.append("")
    out.append("// Признак наличия ключей (провайдер без этого заголовка собирается, но делегаты не привязывает).")
    out.append("#define ASDA_PAKKEY_PRESENT 1")
    out.append("// Идентификатор набора ключей — для лога запуска и сверки с DefaultCrypto.ini.")
    out.append('#define ASDA_PAKKEY_BUILD_ID TEXT("%s")' % build_id)
    out.append("// Число XOR-долей ключа.")
    out.append("#define ASDA_PAKKEY_SHARES %d" % N_SHARES)
    out.append("// Включена ли подпись (bEnablePakSigning) — иначе открытый ключ движку не отдаём.")
    out.append("#define ASDA_PAKKEY_SIGNING %d" % (1 if flags["bEnablePakSigning"] else 0))
    out.append("")
    out.append("// Seed потока маски xorshift64 каждой доли.")
    out.append("static const uint64 ASDA_PAKKEY_SEED[ASDA_PAKKEY_SHARES] =\n{\n%s\n};\n" %
               ",\n".join("\t0x%016XULL" % s for s, _, _ in parts))
    out.append("// Перестановка позиций каждой доли: ключ[perm[i]] ^= part[i] ^ mask[i].")
    out.append("static const uint8 ASDA_PAKKEY_PERM[ASDA_PAKKEY_SHARES][32] =\n{\n%s\n};\n" %
               ",\n".join("\t{ " + ", ".join("%d" % p for p in perm) + " }" for _, perm, _ in parts))
    out.append("// Замаскированные и переставленные доли.")
    out.append("static const uint8 ASDA_PAKKEY_PART[ASDA_PAKKEY_SHARES][32] =\n{\n%s\n};\n" %
               ",\n".join("\t{ " + ", ".join("0x%02X" % b for b in part) + " }" for _, _, part in parts))
    out.append("// Отпечаток ключа: первые 8 байт SHA-1 — самопроверка восстановления (по нему ключ не подобрать).")
    out.append("static const uint8 ASDA_PAKKEY_FINGERPRINT[8] = { %s };\n" % ", ".join("0x%02X" % b for b in fingerprint))
    out.append("// Открытый ключ подписи (little-endian, как ожидает FRSA::CreateKey / BN_lebin2bn).")
    out.append(_c_array("ASDA_PAKSIGN_EXPONENT", _le_bytes(e)))
    out.append(_c_array("ASDA_PAKSIGN_MODULUS", _le_bytes(n)))
    return "\n".join(out)


# ── чтение существующих файлов (для --check) ────────────────────────────────────────────────────────────────────
def read_ini(path=CRYPTO_INI):
    """Разбор DefaultCrypto.ini → dict (ключи секции + 'build_id' из комментария). None — файла нет."""
    if not os.path.isfile(path):
        return None
    d = {}
    section = None
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("; AsdaPakKeyBuildId="):
                d["build_id"] = line.split("=", 1)[1]
            elif line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
            elif "=" in line and not line.startswith(";") and section == INI_SECTION:
                k, v = line.split("=", 1)
                d[k.strip()] = v.strip()
    return d


def read_header(path=GEN_HEADER):
    """Разбор сгенерированного заголовка → dict(build_id, parts, fingerprint, exponent, modulus). None — нет файла."""
    import re
    if not os.path.isfile(path):
        return None
    text = open(path, "r", encoding="utf-8").read()
    m = re.search(r'ASDA_PAKKEY_BUILD_ID TEXT\("([^"]+)"\)', text)
    build_id = m.group(1) if m else None

    def block(name):
        # До первого `};` — конец блока и у многострочных массивов (внутренние строки кончаются `},`), и у однострочных.
        mm = re.search(name + r"[^=]*=\s*\{(.*?)\};", text, re.S)
        return mm.group(1) if mm else ""

    seeds = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{16})ULL", block("ASDA_PAKKEY_SEED"))]
    perms = [[int(v) for v in row.split(",")] for row in re.findall(r"\{([^}]*)\}", block("ASDA_PAKKEY_PERM"))]
    parts = [bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", row))
             for row in re.findall(r"\{([^}]*)\}", block("ASDA_PAKKEY_PART"))]
    fp = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", block("ASDA_PAKKEY_FINGERPRINT")))
    exp = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", block("ASDA_PAKSIGN_EXPONENT")))
    mod = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", block("ASDA_PAKSIGN_MODULUS")))
    if not (len(seeds) == len(perms) == len(parts) == N_SHARES):
        return {"build_id": build_id, "error": "неполный заголовок: seeds=%d perms=%d parts=%d" % (len(seeds), len(perms), len(parts))}
    return {"build_id": build_id, "parts": list(zip(seeds, perms, parts)), "fingerprint": fp,
            "exponent": exp, "modulus": mod}


# ── запись с бэкапом ──────────────────────────────────────────────────────────────────────────────────────────
def _backup(path, no_backup):
    if no_backup or not os.path.isfile(path):
        return None
    sys.path.insert(0, HERE)
    from asda_headless_tool import backup_file   # единая копия до правки (закон backup-before-edit)
    # reuse=False: кроме исходной копии — снимок с меткой времени при КАЖДОМ запуске (ключ прошлой сборки).
    return backup_file(path, BACKUP_SET, reuse=False)


def _write(path, text, no_backup):
    bak = _backup(path, no_backup)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("записан: %s (%d б)%s" % (path, os.path.getsize(path), ("; бэкап: %s" % bak) if bak else ""))


def generate(build_id=None, rsa_bits=4096, encrypt_ini=True, no_backup=False):
    build_id = build_id or make_build_id()
    flags = dict(DEFAULT_FLAGS)
    flags["bEncryptPakIniFiles"] = bool(encrypt_ini)
    aes_key = secrets.token_bytes(KEY_LEN)
    e, d, n = gen_rsa(rsa_bits)
    parts = split_key(aes_key)
    if restore_key(parts) != aes_key:
        raise RuntimeError("самопроверка долей не сошлась — генератор неисправен")
    fp = _fingerprint(aes_key)
    _write(CRYPTO_INI, render_ini(build_id, aes_key, e, d, n, flags), no_backup)
    _write(GEN_HEADER, render_header(build_id, parts, fp, e, n, flags), no_backup)
    print("набор ключей %s: AES-256 новый, RSA-%d новый, отпечаток %s" % (build_id, rsa_bits, fp.hex()))
    # ★ 16.10.2026 (журналы ритейл-клиента — только запечатанными): новые маски долей ОТКРЫТОГО ключа журналов на каждую
    #   релизную сборку (сама пара не меняется — закрытый ключ у нас). Нет пары (публичный клон: ключ журналов в git
    #   не хранится) — только предупреждение: редактор и Development собираются без ключа, Shipping остановится на #error.
    sys.path.insert(0, HERE)
    import asda_logseal
    if asda_logseal.current_public() is None:
        print("ключ журналов: пары нет (%s) — перед первой Shipping-сборкой выполните "
              "`python Tools/asda_logseal.py keygen` (см. Prerequisites/README.md)" % asda_logseal.CURRENT_FILE)
    else:
        print("ключ журналов: заголовок перемаскирован, keyId %s" % asda_logseal.remask(quiet=True))
    return build_id


# ── проверка состояния ─────────────────────────────────────────────────────────────────────────────────────────
def _scan_file_for(path, needle):
    """Есть ли байтовая строка needle в файле (чтение кусками) — поиск ключа в exe/стейдже."""
    if not os.path.isfile(path):
        return None
    tail = b""
    with open(path, "rb") as f:
        while True:
            chunk = f.read(16 * 1024 * 1024)
            if not chunk:
                return False
            if needle in tail + chunk:
                return True
            tail = chunk[-(len(needle) - 1):]


def check(verbose=True):
    """Печатает состояние; возвращает число ОШИБОК (несовпадений)."""
    errors = 0
    ini = read_ini()
    hdr = read_header()
    print("DefaultCrypto.ini : %s" % (CRYPTO_INI if ini is not None else "НЕТ (%s)" % CRYPTO_INI))
    print("generated.h       : %s" % (GEN_HEADER if hdr is not None else "НЕТ (%s)" % GEN_HEADER))
    if ini is None or hdr is None:
        return 1
    if "error" in hdr:
        print("ОШИБКА: %s" % hdr["error"])
        return 1
    print("набор ini         : %s" % ini.get("build_id"))
    print("набор заголовка   : %s" % hdr.get("build_id"))
    if ini.get("build_id") != hdr.get("build_id"):
        print("ОШИБКА: идентификаторы набора различаются — перегенерировать")
        errors += 1
    for k in ("bEnablePakSigning", "bEncryptPakIniFiles", "bEncryptPakIndex", "bEncryptUAssetFiles",
              "bEncryptAllAssetFiles"):
        print("  %-22s = %s" % (k, ini.get(k)))
    aes_ini = base64.b64decode(ini.get("EncryptionKey", ""))
    aes_hdr = restore_key(hdr["parts"])
    same_key = aes_ini == aes_hdr and len(aes_ini) == KEY_LEN
    print("ключ AES ini == восстановленный из долей: %s (отпечаток %s%s)" % (
        "ДА" if same_key else "НЕТ", _fingerprint(aes_hdr).hex(),
        "" if _fingerprint(aes_hdr) == hdr["fingerprint"] else " != отпечатка в заголовке!"))
    if not same_key or _fingerprint(aes_hdr) != hdr["fingerprint"]:
        errors += 1
    exp_ini = base64.b64decode(ini.get("SigningPublicExponent", ""))
    mod_ini = base64.b64decode(ini.get("SigningModulus", ""))
    same_sign = exp_ini == hdr["exponent"] and mod_ini == hdr["modulus"]
    print("открытый ключ подписи ini == заголовок: %s (модуль %d бит, e=%d)" % (
        "ДА" if same_sign else "НЕТ", len(mod_ini) * 8, int.from_bytes(exp_ini, "little") if exp_ini else 0))
    if not same_sign:
        errors += 1
    # Где ключ НЕ должен лежать в открытом виде.
    for rel in ("Binaries/Win64/AsdaReborn.exe", "Binaries/Win64/AsdaReborn-Win64-Shipping.exe",
                "Binaries/Win64/UnrealEditor-AsdaReborn.dll"):
        p = os.path.join(PROJECT_DIR, rel)
        r = _scan_file_for(p, aes_ini) if aes_ini else None
        if r is None:
            continue
        print("ключ в открытом виде в %-45s: %s" % (rel, "ЕСТЬ — ОШИБКА" if r else "нет"))
        if r:
            errors += 1
    staged = os.path.join(PROJECT_DIR, "Saved", "StagedBuilds", "Windows", "AsdaReborn", "Config", "DefaultCrypto.ini")
    if os.path.isfile(staged):
        txt = open(staged, "r", encoding="utf-8", errors="replace").read()
        leak = ("EncryptionKey=" in txt and ini.get("EncryptionKey", "") in txt) or "SigningPrivateExponent=" in txt
        print("стейдж Config/DefaultCrypto.ini: %s" % ("СОДЕРЖИТ КЛЮЧИ — ОШИБКА" if leak else "ключи вырезаны (IniKeyDenylist)"))
        if leak:
            errors += 1
    # ★ 16.10.2026: ключ журналов — заголовок сходится с текущей парой, открытого ключа в открытом виде в exe нет.
    sys.path.insert(0, HERE)
    import asda_logseal
    print("--- ключ запечатанных журналов (asda_logseal check) ---")
    errors += asda_logseal.check([os.path.join(PROJECT_DIR, rel) for rel in ("Binaries/Win64/AsdaReborn-Win64-Shipping.exe",)
                                  if os.path.isfile(os.path.join(PROJECT_DIR, rel))])
    print("итог: %s" % ("ОК" if errors == 0 else "%d ошиб." % errors))
    return errors


# ★ 14.10.2026 (античит, блок Б): `-nodebuginfo` обязателен. `IncludeDebugFiles=False` в DefaultGame.ini читает только меню
#   редактора «Package Project»; RunUAT из командной строки без ключа кладёт в архив AsdaReborn-Win64-Shipping.pdb
#   (≈300 МБ, все имена функций и переменных — подарок автору чита). Проверено: CopyBuildToStagingDirectory.Automation.cs:3043
#   (`if (!Params.NoDebugInfo) CopyManifestFilesToStageDir(... NonUFSDebugFiles ...)`), упаковка 14.10 без ключа — pdb в архиве.
UAT_CMD = (r'& "D:\Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun '
           r'-project="@@ASTORY_DEV_ROOT@@\AstoryClient\AsdaReborn.uproject" -noP4 -platform=Win64 '
           r'-clientconfig={cfg} -build -cook -stage -pak -archive -nodebuginfo -archivedirectory="{out}" -utf8output')


def main(argv=None):
    # Консоль Windows (cp1251) не знает часть символов — печатаем в UTF-8 с заменой, чтобы не падать на выводе.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="Ключи сборки: новый AES-256 + RSA подписи (запускать только при смене major-версии)")
    ap.add_argument("--check", action="store_true", help="только проверить состояние (ничего не писать)")
    ap.add_argument("--build-id", help="свой идентификатор набора ключей")
    ap.add_argument("--rsa-bits", type=int, default=4096, help="размер модуля RSA (по умолчанию 4096)")
    ap.add_argument("--no-encrypt-ini", action="store_true", help="не шифровать .ini в паке")
    ap.add_argument("--no-backup", action="store_true", help="без копии перед перезаписью (не для релиза)")
    ap.add_argument("--print-uat", action="store_true", help="напечатать команду упаковки и выйти")
    ap.add_argument("--uat-config", default="Shipping", help="конфигурация для --print-uat (Shipping/Development)")
    ap.add_argument("--uat-out", default=r"@@ASTORY_DEV_ROOT@@\Build\Dev", help="каталог архива для --print-uat")
    a = ap.parse_args(argv)
    if a.print_uat:
        print(UAT_CMD.format(cfg=a.uat_config, out=a.uat_out))
        return 0
    if a.check:
        return 1 if check() else 0
    generate(build_id=a.build_id, rsa_bits=a.rsa_bits, encrypt_ini=not a.no_encrypt_ini, no_backup=a.no_backup)
    return 1 if check() else 0


if __name__ == "__main__":
    sys.exit(main())
