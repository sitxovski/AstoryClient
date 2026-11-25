# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_logseal.py — ★ ЗАПЕЧАТАННЫЕ ЖУРНАЛЫ РИТЕЙЛ-КЛИЕНТА (решение владельца 16.10.2026): ключи, заголовок клиента, расшифровка.
#
# СХЕМА (клиентская часть — Source/AsdaNet/Private/LogSeal/AsdaLogSeal.cpp, формат — там же и ниже):
#   В клиенте только ОТКРЫТЫЙ ключ P-256 получателя (наш). На каждый сегмент журнала клиент берёт эфемерную пару ECDH P-256,
#   общий секрет с нашим открытым ключом → HKDF-SHA256 → AES-256-GCM. Это ровно рукопожатие AsdaSecure (Session::ClientHello +
#   ClientFinish), где «ServerHello» собран из нашего ДОЛГОВРЕМЕННОГО открытого ключа и случайного nonce, а под-сервер = 0x4C
#   ('L', журнал; разводит ключи журнала с ключами трафика 1–4). Кадры — Session::Seal: [u32 clen][u64 seq][ct][tag16],
#   AAD = clen||seq, IV = c2sIv XOR be64(seq). Закрытый ключ — только у нас (DPAPI, PRIV_STORE), в исходники и сборку не попадает.
#
# ФОРМАТ ФАЙЛА (.alog — журнал, .acrash — пакет падения): последовательность сегментов.
#   Сегмент: заголовок 128 б + кадры AsdaSecure.
#     0   8  "ASLGSEG1"
#     8   1  версия формата (1)
#     9   1  вид: 1 журнал EAC, 2 запасной маркер EAC (raw), 3 журнал движка, 4 пакет падения, 5 перенос старых файлов, 127 самотест
#     10  2  0
#     12  8  keyId = SHA-256(X||Y открытого ключа получателя)[0:8]
#     20  84 ClientHello AsdaSecure: A5 01 4C 00 | ePub клиента (64) | nonce клиента (16)
#     104 16 nonce «сервера» (случайный, от клиента)
#     120 8  0
#   Запись внутри кадра: 1 байт типа + данные. 1 — сведения о сегменте (UTF-8 «ключ=значение»), 2 — текст журнала (UTF-8),
#   3 — текст журнала, сжатый zlib (★ 31.10: u32 длина UTF-8 + поток zlib),
#   0x10 — начало файла (u16 длина имени, имя UTF-8, u64 размер), 0x11 — кусок данных файла, 0x12 — конец файла.
#   Новый сегмент отличается от кадра по первым 4 байтам: "ASLG" как u32 LE (0x474C5341) больше любого допустимого clen.
#   seq в сегменте строго 1, 2, 3… — пропуск/перестановка/подмена видны. Обрезанный хвост (падение при записи) — сообщение
#   «хвост обрезан», всё до него читается.
#
# ДОЛИ ОТКРЫТОГО КЛЮЧА в заголовке клиента — как у ключа pak (gen_build_crypto.py): 4 XOR-доли, каждая переставлена и
#   замаскирована xorshift64 от своего seed. Открытый ключ не секрет; доли нужны, чтобы его нельзя было найти grep-ом по exe и
#   тихо подменить своим. Маски меняются на КАЖДУЮ релизную сборку (gen_build_crypto.generate зовёт remask()).
#
# ЗАПУСК (обычный python + пакет cryptography; при импорте ничего не делает — закон tools-no-import-side-effects):
#   python Tools/asda_logseal.py keygen               — новая пара (если текущей нет; --force — новая при наличии), затем header
#   python Tools/asda_logseal.py header               — перемаскировать заголовок клиента текущим открытым ключом (с бэкапом)
#   python Tools/asda_logseal.py check [--exe P …]    — заголовок ↔ ключи; открытого ключа в открытом виде в exe нет
#   python Tools/asda_logseal.py decrypt <файл|папка> [-o папка] [--key файл]
#                                                     — .alog → .txt рядом (или в -o); .acrash → папка <имя>.d с файлами
#   python Tools/asda_logseal.py protect-key [--delete-plain] — открытые файлы ключей → хранилище DPAPI (★ 16.10)
#   python Tools/asda_logseal.py export-backup <файл> — резервная копия ключей под пароль владельца (★ 16.10)
#   python Tools/asda_logseal.py import-backup <файл> — восстановление ключей из копии в DPAPI (★ 16.10)
#   python Tools/asda_logseal.py selftest             — самопроверка разбора на синтетическом файле (Python ↔ Python)
#   python Tools/asda_logseal.py gen-test-key <папка> — тестовая пара для самотеста клиента (asda.SelfTest.LogSeal)
#   python Tools/asda_logseal.py verify-selftest <папка> — расшифровать и сверить всё, что записал asda.SelfTest.LogSeal
#   python Tools/asda_logseal.py join <файлы|папки> -o <папка> [--key файл]
#                                                     — ★ 31.10: части журнала сессии (<сессия>_partN.alog у игрока, случайные
#                                                       имена на сервере) → <сессия>.joined.txt по порядку частей, пропуски названы
#   python Tools/asda_logseal.py verify-rotation <папка> — ★ 31.10: сверить вывод asda.SelfTest.LogSealRotate (ротация частей)
# ★ 31.10.2026 РОТАЦИЯ: журнал сессии игры у предела части (≤ 4 МиБ — предел приёмника) продолжается файлом <сессия>_partN.alog;
#   каждая часть — самостоятельный сегмент вида 3 со сведениями session=<имя части>, session_id=<ключ сессии>, part=<N>;
#   строки — записью 2 (Text) или 3 (TextZ: u32 длина UTF-8 + поток zlib). Клиент хранит первую и последние части (всего 6).
import argparse
import datetime
import hashlib
import hmac
import io
import os
import re
import secrets
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(HERE)                                                     # @@ASTORY_DEV_ROOT@@\AstoryClient
ROOT_DIR = os.path.dirname(PROJECT_DIR)                                                 # @@ASTORY_DEV_ROOT@@
# ★ 16.10.2026: ОТКРЫТЫЕ части ключа — в проекте (Perforce), чтобы сборка шла на любом ПК без закрытого ключа:
#   Tools/data/logseal/current.txt (keyId текущей пары) и logseal_<keyId>.pub (X||Y, 64 б). Закрытый ключ нужен ТОЛЬКО
#   для расшифровки (decrypt) и живёт вне проекта — в хранилище DPAPI (PRIV_STORE ниже); на другом ПК — import-backup.
PUB_DIR = os.path.join(HERE, "data", "logseal")
CURRENT_FILE = os.path.join(PUB_DIR, "current.txt")                                     # keyId текущей пары (hex)
# Прежний каталог (до 16.10): там лежали открытые файлы закрытых ключей ASDAKEY1 — читается только protect-key.
KEYS_DIR = os.path.join(ROOT_DIR, "AstoryServer", "Patches", "AsdaSecure", "out", "keys", "logseal")   # ★ 17.10: AstoryServer\Patches
# ★ 16.10.2026 (сессия «Шифрование логов ритейл клиента»): закрытые ключи — ТОЛЬКО под Windows DPAPI (учётная запись
#   владельца, доп. энтропия = метка + keyId) в хранилище вне проекта/Perforce; папка закрыта ACL только на пользователя.
#   В KEYS_DIR остаются лишь открытые части (.pub, current.txt). Резервная копия — export-backup (пароль владельца,
#   scrypt N=2^19 r=8 + AES-256-GCM). Открытый файл ключа (старый формат ASDAKEY1) читается только для переноса
#   (protect-key) и для тестовых ключей самотеста.
PRIV_STORE = os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~"), "AsdaReborn", "LogSealKeys")
PROJECT_KEY_BACKUP = os.path.join(ROOT_DIR, "_keys", "LogSeal")  # копия DPAPI-файлов (не открытый ключ)
DPAPI_ENTROPY = b"AsdaReborn/LogSeal/v1/"                                                # + keyId (8 б)
BACKUP_FORMAT = "AsdaLogSealKeyBackup/v1"
GEN_HEADER = os.path.join(PROJECT_DIR, "Source", "AsdaNet", "Private", "LogSeal", "AsdaLogSealKey.generated.h")
BACKUP_SET = "logseal"

SEG_MAGIC = b"ASLGSEG1"          # метка сегмента
SEG_HDR_LEN = 128                # длина заголовка сегмента
FORMAT_VERSION = 1               # версия формата файла
LOG_SUBSERVER = 0x4C             # под-сервер AsdaSecure для журналов ('L')
SECURE_VERSION = 1               # AsdaSecure::kVersion
CLIENT_MAGIC = 0xA5              # AsdaSecure::kClientMagic
SERVER_MAGIC = 0x5A              # AsdaSecure::kServerMagic
PUB_LEN = 64                     # X||Y P-256
TAG_LEN = 16                     # тег GCM
FRAME_HDR = 12                   # u32 clen + u64 seq
MAX_PLAIN = 64 * 1024            # AsdaSecure::kMaxPlain
KEY_FILE_TAG = b"ASDAKEY1"       # формат файла ключа AsdaSecure (тег + d(32) + xy(64))
KINDS = {1: "журнал EAC", 2: "маркер EAC (raw)", 3: "журнал движка", 4: "пакет падения", 5: "перенос старых файлов",
         127: "самотест"}
REC_INFO, REC_TEXT, REC_FBEGIN, REC_FDATA, REC_FEND = 1, 2, 0x10, 0x11, 0x12
# ★ 31.10.2026 (ротация журнала сессии): запись 3 — строки журнала, сжатые zlib: u32 длина UTF-8 (LE) + поток zlib.
REC_TEXTZ = 3
TEXTZ_MAX = 1 << 20              # предел заявленной длины распакованной записи TextZ (клиент пишет ≤ 48 КиБ)
# ★ 31.10.2026: части одной сессии игры (клиент: <ГГГГММДД-ЧЧММСС>_<pid>[_<n>][_part<N>].alog; на сервере имя случайное —
#   часть и сессию называют сведения сегмента session_id= и part=).
PART_NAME_RE = re.compile(r"^(?P<key>\d{8}-\d{6}_\d+(?:_\d+)?)(?:_part(?P<part>\d{1,4}))?\.alog$", re.I)
# ★ 19.10.2026 (ревизия безопасности REVIEW_security.md, Б1): файлы пакета извлекаются только из сегментов этих видов
#   (4 пакет падения, 5 перенос старых файлов, 127 самотест клиента); в журналах игры/EAC записи файлов игнорируются.
FILE_KINDS = (4, 5, 127)
# Имена устройств Windows: как компонент пути (с любым расширением) открывают устройство, а не файл
WIN_DEVICES = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"} | {"COM%d" % i for i in range(10)} | \
    {"LPT%d" % i for i in range(10)} | {"COM\u00b9", "COM\u00b2", "COM\u00b3", "LPT\u00b9", "LPT\u00b2", "LPT\u00b3"}
# Управляющие символы и переключатели направления текста — в выводе показываются как \xNN / \uNNNN
_CTRL_RE = re.compile("[\x00-\x08\x0b-\x1f\x7f-\x9f\u200e\u200f\u202a-\u202e\u2066-\u2069]")
N_SHARES = 4
MASK64 = (1 << 64) - 1


# ── ключи ──────────────────────────────────────────────────────────────────────────────────────────────────────
def key_id(pub):
    """keyId открытого ключа: первые 8 байт SHA-256(X||Y)."""
    return hashlib.sha256(pub).digest()[:8]


def gen_keypair():
    """Новая пара P-256 → (d 32 б, xy 64 б)."""
    from cryptography.hazmat.primitives.asymmetric import ec
    priv = ec.generate_private_key(ec.SECP256R1())
    nums = priv.private_numbers()
    d = nums.private_value.to_bytes(32, "big")
    xy = nums.public_numbers.x.to_bytes(32, "big") + nums.public_numbers.y.to_bytes(32, "big")
    return d, xy


def save_key(path, d, xy):
    """Файл ключа в формате AsdaSecure (SavePrivateKeyFile): «ASDAKEY1» + d + xy = 104 б."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(KEY_FILE_TAG + d + xy)


def load_key(path):
    """Файл ключа → (d, xy). Исключение — не тот формат."""
    raw = open(path, "rb").read()
    if len(raw) != 104 or raw[:8] != KEY_FILE_TAG:
        raise ValueError("не файл ключа ASDAKEY1: %s" % path)
    return raw[8:40], raw[40:104]


def key_path(kid):
    return os.path.join(KEYS_DIR, "logseal_%s.key" % kid.hex())


def pub_path(kid):
    return os.path.join(PUB_DIR, "logseal_%s.pub" % kid.hex())


def current_public():
    """(xy, kid) текущей пары из проекта (без закрытого ключа) или None."""
    if not os.path.isfile(CURRENT_FILE):
        return None
    kid = bytes.fromhex(open(CURRENT_FILE, "r", encoding="ascii").read().strip())
    xy = open(pub_path(kid), "rb").read()
    if len(xy) != PUB_LEN or key_id(xy) != kid:
        raise ValueError("открытый ключ %s не совпадает с keyId из current.txt" % pub_path(kid))
    return xy, kid


def current_key():
    """(d, xy, kid) текущей пары — только где есть закрытый ключ (расшифровка). None — пары нет или ключа на этом ПК нет."""
    cur = current_public()
    if cur is None:
        return None
    xy, kid = cur
    got = find_private(kid)
    if got is None or got[1] != xy:
        return None
    return got[0], xy, kid


def find_private(kid, extra=None):
    """Закрытый ключ по keyId: явный файл (--key), затем хранилище DPAPI, затем старый открытый файл в KEYS_DIR
    (с предупреждением — его надо перенести: protect-key). None — нет."""
    if extra and os.path.isfile(extra):
        d, xy = load_key(extra)
        if key_id(xy) == kid:
            return d, xy
    got = store_load(kid)
    if got is not None:
        return got
    p = key_path(kid)
    if os.path.isfile(p):
        d, xy = load_key(p)
        if key_id(xy) == kid:
            sys.stderr.write("! закрытый ключ %s лежит ОТКРЫТЫМ файлом %s — перенесите: protect-key\n" % (kid.hex(), p))
            return d, xy
    return None


# ── хранилище закрытых ключей (DPAPI) ─────────────────────────────────────────────────────────────────────────────
def _dpapi(data, entropy, protect):
    """CryptProtectData/CryptUnprotectData (CurrentUser, без UI)."""
    import ctypes
    import ctypes.wintypes as wt

    class BLOB(ctypes.Structure):
        _fields_ = [("cbData", wt.DWORD), ("pbData", ctypes.POINTER(ctypes.c_char))]

    def mk(b):
        buf = ctypes.create_string_buffer(b, len(b))
        return BLOB(len(b), ctypes.cast(buf, ctypes.POINTER(ctypes.c_char))), buf
    src, _k1 = mk(data)
    ent, _k2 = mk(entropy)
    dst = BLOB()
    fn = ctypes.windll.crypt32.CryptProtectData if protect else ctypes.windll.crypt32.CryptUnprotectData
    desc = "AsdaReborn log seal key" if protect else None
    if not fn(ctypes.byref(src), desc, ctypes.byref(ent), None, None, 0x1, ctypes.byref(dst)):
        raise OSError("DPAPI: ошибка %d (ключ защищён другой учётной записью Windows?)" % ctypes.GetLastError())
    try:
        return ctypes.string_at(dst.pbData, dst.cbData)
    finally:
        ctypes.windll.kernel32.LocalFree(dst.pbData)


def _lock_acl(path):
    """Снять наследование и оставить полный доступ только текущему пользователю."""
    import subprocess
    user = os.environ.get("USERNAME", "")
    dom = os.environ.get("USERDOMAIN", "")
    who = "%s\\%s" % (dom, user) if dom else user
    r = subprocess.run(["icacls", path, "/inheritance:r", "/grant:r", "%s:(OI)(CI)F" % who], capture_output=True)
    if r.returncode != 0:
        raise OSError("icacls %s: код %d" % (path, r.returncode))


def store_path(kid):
    return os.path.join(PRIV_STORE, "logseal_%s.key.dpapi" % kid.hex())


def store_save(d, xy):
    """Закрытый ключ → хранилище DPAPI (файл не перезаписывается). Проверка чтением назад. Возврат: путь."""
    kid = key_id(xy)
    if not os.path.isdir(PRIV_STORE):
        os.makedirs(PRIV_STORE)
        _lock_acl(PRIV_STORE)
    path = store_path(kid)
    if os.path.isfile(path):
        if store_load(kid) != (d, xy):
            raise RuntimeError("в хранилище уже ДРУГОЙ ключ с keyId %s" % kid.hex())
        return path
    blob = _dpapi(KEY_FILE_TAG + d + xy, DPAPI_ENTROPY + kid, True)
    with open(path, "xb") as f:
        f.write(blob)
    if store_load(kid) != (d, xy):
        raise RuntimeError("хранилище: ключ не читается назад")
    return path


def _container_stores():
    """Хранилища ключей, оставшиеся в подменном AppData упакованного приложения (★ 19.10.2026). Процесс, запущенный
    из упакованного приложения (вкладка Code в Claude Desktop и т. п.), пишет в %APPDATA% не туда: Windows молча
    уводит запись в <Packages>/<пакет>/LocalCache/Roaming, и только процессы того же пакета видят файл на «обычном»
    месте. Так ключ (keygen 16.10 из сессии Claude) оказался невидим инструменту, запущенному
    владельцем двойным щелчком. Здесь — все такие папки на всех дисках (LOCALAPPDATA и <диск>:/WpSystem)."""
    import glob
    tail = os.path.join("LocalCache", "Roaming", "AsdaReborn", "LogSealKeys")
    pats = []
    la = os.environ.get("LOCALAPPDATA")
    if la:
        pats.append(os.path.join(la, "Packages", "*", tail))
    pats.append(os.path.join("?:" + os.sep, "WpSystem", "*", "AppData", "Local", "Packages", "*", tail))
    # ★ 19.10.2026 (владелец): копия того же файла DPAPI в папке проекта вне депо — @@ASTORY_DEV_ROOT_FWD@@/_keys/LogSeal
    pats.append(PROJECT_KEY_BACKUP)
    seen, out = set(), []
    for pat in pats:
        for d in glob.glob(pat):
            r = os.path.normcase(os.path.realpath(d))
            if r not in seen and r != os.path.normcase(os.path.realpath(PRIV_STORE)) and os.path.isdir(d):
                seen.add(r)
                out.append(d)
    return out


def _dpapi_read(path, kid):
    """(d, xy) из файла DPAPI с проверкой метки и keyId."""
    raw = _dpapi(open(path, "rb").read(), DPAPI_ENTROPY + kid, False)
    if len(raw) != 104 or raw[:8] != KEY_FILE_TAG or key_id(raw[40:104]) != kid:
        raise ValueError("хранилище: ключ %s повреждён" % kid.hex())
    return raw[8:40], raw[40:104]


def _adopt_from_container(kid):
    """Ключа нет в PRIV_STORE, но он есть в подменном AppData пакета → проверить (DPAPI этой же учётки) и перенести
    тот же защищённый файл в настоящее хранилище (папка под ACL пользователя). Открытым ключ нигде не появляется.
    Возврат: (d, xy) или None."""
    name = os.path.basename(store_path(kid))
    for d in _container_stores():
        src = os.path.join(d, name)
        if not os.path.isfile(src):
            continue
        try:
            key = _dpapi_read(src, kid)
        except (OSError, ValueError):
            continue
        try:
            if not os.path.isdir(PRIV_STORE):
                os.makedirs(PRIV_STORE)
                _lock_acl(PRIV_STORE)
            dst = store_path(kid)
            if not os.path.isfile(dst):
                with open(src, "rb") as f:
                    blob = f.read()
                with open(dst, "xb") as f:
                    f.write(blob)
            if _dpapi_read(dst, kid) == key:
                print("  [ключ] %s перенесён в хранилище %s (лежал в подменном AppData: %s)" % (kid.hex(), PRIV_STORE, d))
        except (OSError, ValueError) as e:
            print("  [ключ] %s взят из %s, перенести в %s не удалось: %s" % (kid.hex(), d, PRIV_STORE, e))
        return key
    return None


def store_load(kid):
    """(d, xy) из хранилища DPAPI или None (★ 19.10: при отсутствии — перенос из подменного AppData пакета)."""
    path = store_path(kid)
    if not os.path.isfile(path):
        return _adopt_from_container(kid)
    return _dpapi_read(path, kid)


def store_kids():
    """keyId всех ключей хранилища (и оставшихся в подменном AppData пакета — их подхватит store_load)."""
    kids = set()
    for d in [PRIV_STORE] + _container_stores():
        if os.path.isdir(d):
            kids.update(bytes.fromhex(n[8:24]) for n in os.listdir(d)
                        if n.startswith("logseal_") and n.endswith(".key.dpapi") and len(n) == 8 + 16 + 10)
    return sorted(kids)


def _shred(path):
    """Перезаписать файл случайными байтами, сбросить на диск, удалить."""
    n = os.path.getsize(path)
    with open(path, "r+b") as f:
        f.write(secrets.token_bytes(n))
        f.flush()
        os.fsync(f.fileno())
    os.remove(path)


def protect_key(delete_plain=False):
    """Перенести открытые файлы ключей KEYS_DIR в хранилище DPAPI; с --delete-plain — затереть открытые после проверки."""
    moved = 0
    if not os.path.isdir(KEYS_DIR):
        print("открытых ключей нет (%s)" % KEYS_DIR)
        return 0
    for n in sorted(os.listdir(KEYS_DIR)):
        if not (n.startswith("logseal_") and n.endswith(".key")):
            continue
        src = os.path.join(KEYS_DIR, n)
        d, xy = load_key(src)
        dst = store_save(d, xy)
        ok = store_load(key_id(xy)) == (d, xy)
        print("%s → %s : %s" % (n, dst, "ОК" if ok else "ОШИБКА"))
        if not ok:
            return 1
        moved += 1
        if delete_plain:
            _shred(src)
            print("  открытый файл затёрт и удалён: %s" % src)
    if moved and not delete_plain:
        print("Открытые файлы ОСТАВЛЕНЫ. Сначала export-backup (резервная копия под пароль), затем protect-key --delete-plain.")
    return 0


def export_backup(path):
    """Все ключи хранилища → один файл под пароль владельца (scrypt + AES-256-GCM)."""
    import base64
    import getpass
    import json
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives.kdf.scrypt import Scrypt
    kids = store_kids()
    if not kids:
        print("хранилище пусто: %s" % PRIV_STORE)
        return 1
    pw = getpass.getpass("Пароль резервной копии (не короче 14 символов): ")
    if len(pw) < 14 or getpass.getpass("Повторите: ") != pw:
        print("пароль короткий или не совпал")
        return 1
    plain = b""
    for k in kids:
        d, xy = store_load(k)
        plain += KEY_FILE_TAG + d + xy
    salt, nonce = secrets.token_bytes(32), secrets.token_bytes(12)
    hdr = {"format": BACKUP_FORMAT, "kdf": "scrypt", "n": 2 ** 19, "r": 8, "p": 1, "salt": salt.hex(),
           "nonce": nonce.hex(), "cipher": "AES-256-GCM", "kids": [k.hex() for k in kids]}
    key = Scrypt(salt=salt, length=32, n=hdr["n"], r=hdr["r"], p=hdr["p"]).derive(pw.encode("utf-8"))
    aad = json.dumps(hdr, sort_keys=True).encode("utf-8")
    hdr["ct"] = base64.b64encode(AESGCM(key).encrypt(nonce, plain, aad)).decode("ascii")
    with open(path, "x", encoding="utf-8") as f:
        json.dump(hdr, f, indent=1)
    print("резервная копия %d ключ(ей) %s → %s. Храните отдельно от пароля (внешний носитель)."
          % (len(kids), ", ".join(k.hex() for k in kids), path))
    return 0


def import_backup(path):
    """Резервная копия → хранилище DPAPI текущей учётной записи."""
    import base64
    import getpass
    import json
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives.kdf.scrypt import Scrypt
    hdr = json.load(open(path, encoding="utf-8"))
    ct = base64.b64decode(hdr.pop("ct"))
    if hdr.get("format") != BACKUP_FORMAT:
        print("не резервная копия ключей журналов")
        return 1
    key = Scrypt(salt=bytes.fromhex(hdr["salt"]), length=32, n=hdr["n"], r=hdr["r"], p=hdr["p"]).derive(
        getpass.getpass("Пароль резервной копии: ").encode("utf-8"))
    try:
        plain = AESGCM(key).decrypt(bytes.fromhex(hdr["nonce"]), ct, json.dumps(hdr, sort_keys=True).encode("utf-8"))
    except Exception:
        print("неверный пароль или копия повреждена")
        return 2
    for i in range(0, len(plain), 104):
        rec = plain[i:i + 104]
        print("восстановлен %s → %s" % (key_id(rec[40:104]).hex(), store_save(rec[8:40], rec[40:104])))
    return 0


# ── доли открытого ключа ───────────────────────────────────────────────────────────────────────────────────────
def _xorshift64_mask(seed, n):
    """Поток маски (та же формула, что у ключа pak и в AsdaLogSeal.cpp): xorshift64, байт слова по i % 8."""
    x = seed & MASK64
    out = bytearray(n)
    for i in range(n):
        x ^= (x << 13) & MASK64
        x ^= x >> 7
        x ^= (x << 17) & MASK64
        out[i] = (x >> ((i % 8) * 8)) & 0xFF
    return bytes(out)


def split_shares(data):
    """data → N_SHARES кортежей (seed, perm, part): data[perm[i]] ^= part[i] ^ mask(seed)[i] по всем долям."""
    n = len(data)
    shares = [secrets.token_bytes(n) for _ in range(N_SHARES - 1)]
    acc = bytearray(data)
    for s in shares:
        for i in range(n):
            acc[i] ^= s[i]
    shares.append(bytes(acc))
    parts = []
    for s in shares:
        seed = int.from_bytes(secrets.token_bytes(8), "little") | 1
        perm = list(range(n))
        for i in range(n - 1, 0, -1):
            j = secrets.randbelow(i + 1)
            perm[i], perm[j] = perm[j], perm[i]
        mask = _xorshift64_mask(seed, n)
        parts.append((seed, perm, bytes(s[perm[i]] ^ mask[i] for i in range(n))))
    return parts


def restore_shares(parts, n):
    out = bytearray(n)
    for seed, perm, part in parts:
        mask = _xorshift64_mask(seed, n)
        for i in range(n):
            out[perm[i]] ^= part[i] ^ mask[i]
    return bytes(out)


def render_header(xy, kid, parts, stamp):
    o = []
    o.append("// AsdaLogSealKey.generated.h — ★ ГЕНЕРИРУЕТСЯ Tools/asda_logseal.py, НЕ ПРАВИТЬ РУКАМИ.")
    o.append("//   ОТКРЫТЫЙ ключ P-256 получателя запечатанных журналов ритейл-клиента (закрытый — только у нас,")
    o.append("//   AstoryServer/Patches/AsdaSecure/out/keys/logseal). Ключ разложен на %d XOR-доли (переставлены и замаскированы xorshift64)," % N_SHARES)
    o.append("//   маски новые на каждую релизную сборку. Восстановление — AsdaLogSeal.cpp (RestorePublicKey), только в памяти.")
    o.append("//   Маски сгенерированы: %s." % stamp)
    o.append("#pragma once")
    o.append("")
    o.append("// Признак наличия ключа (в Shipping без него сборка запрещена — #error в AsdaLogSeal.cpp).")
    o.append("#define ASDA_LOGSEAL_PRESENT 1")
    o.append("// Число долей.")
    o.append("#define ASDA_LOGSEAL_SHARES %d" % N_SHARES)
    o.append("")
    o.append("// Seed потока маски каждой доли.")
    o.append("static const uint64 ASDA_LOGSEAL_SEED[ASDA_LOGSEAL_SHARES] =\n{\n%s\n};\n" %
             ",\n".join("\t0x%016XULL" % s for s, _, _ in parts))
    o.append("// Перестановка позиций каждой доли: ключ[perm[i]] ^= part[i] ^ mask[i].")
    o.append("static const uint8 ASDA_LOGSEAL_PERM[ASDA_LOGSEAL_SHARES][64] =\n{\n%s\n};\n" %
             ",\n".join("\t{ " + ", ".join("%d" % p for p in perm) + " }" for _, perm, _ in parts))
    o.append("// Замаскированные и переставленные доли.")
    o.append("static const uint8 ASDA_LOGSEAL_PART[ASDA_LOGSEAL_SHARES][64] =\n{\n%s\n};\n" %
             ",\n".join("\t{ " + ", ".join("0x%02X" % b for b in part) + " }" for _, _, part in parts))
    o.append("// keyId = SHA-256(X||Y)[0:8] — пишется в заголовок сегмента и сверяется после восстановления.")
    o.append("static const uint8 ASDA_LOGSEAL_KEYID[8] = { %s };\n" % ", ".join("0x%02X" % b for b in kid))
    return "\n".join(o)


def read_header(path=GEN_HEADER):
    """Сгенерированный заголовок → (xy, kid) восстановлением долей; None — файла нет."""
    import re
    if not os.path.isfile(path):
        return None
    text = open(path, "r", encoding="utf-8").read()

    def block(name):
        m = re.search(name + r"[^=]*=\s*\{(.*?)\};", text, re.S)
        return m.group(1) if m else ""
    seeds = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{16})ULL", block("ASDA_LOGSEAL_SEED"))]
    perms = [[int(v) for v in row.split(",")] for row in re.findall(r"\{([^}]*)\}", block("ASDA_LOGSEAL_PERM"))]
    parts = [bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", row))
             for row in re.findall(r"\{([^}]*)\}", block("ASDA_LOGSEAL_PART"))]
    kid = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", block("ASDA_LOGSEAL_KEYID")))
    if not (len(seeds) == len(perms) == len(parts) == N_SHARES):
        raise ValueError("неполный заголовок %s" % path)
    return restore_shares(list(zip(seeds, perms, parts)), PUB_LEN), kid


def _backup(path):
    if not os.path.isfile(path):
        return None
    sys.path.insert(0, HERE)
    try:
        from asda_headless_tool import backup_file   # единая копия до правки (закон backup-before-edit)
        return backup_file(path, BACKUP_SET, reuse=False)
    except OSError:
        # ★ 16.10: другой ПК без @@ASTORY_DEV_ROOT@@\_backup — копия в Saved проекта (вне Perforce), сборка не останавливается.
        import shutil
        dst = os.path.join(PROJECT_DIR, "Saved", "Backup", BACKUP_SET,
                           datetime.datetime.now().strftime("%Y%m%d-%H%M%S"), os.path.basename(path))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(path, dst)
        if os.path.getsize(dst) != os.path.getsize(path):
            raise RuntimeError("бэкап разошёлся по размеру: %s" % dst)
        return dst


def remask(quiet=False):
    """Перезаписать заголовок клиента новыми масками текущего открытого ключа. Возврат: keyId hex. Ошибка — нет ключа."""
    cur = current_public()                           # закрытый ключ НЕ нужен — работает на любом ПК
    if cur is None:
        raise RuntimeError("пары журналов нет (%s): сначала `python Tools/asda_logseal.py keygen`" % CURRENT_FILE)
    xy, kid = cur
    parts = split_shares(xy)
    if restore_shares(parts, PUB_LEN) != xy:
        raise RuntimeError("самопроверка долей не сошлась")
    text = render_header(xy, kid, parts, datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
    bak = _backup(GEN_HEADER)
    os.makedirs(os.path.dirname(GEN_HEADER), exist_ok=True)
    with open(GEN_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    if not quiet:
        print("записан: %s (keyId %s)%s" % (GEN_HEADER, kid.hex(), ("; бэкап: %s" % bak) if bak else ""))
    return kid.hex()


def keygen(force=False):
    if current_public() is not None and not force:
        print("пара уже есть (keyId %s) — новая не создаётся (--force — создать новую; старая остаётся для старых журналов)"
              % current_public()[1].hex())
        return remask()
    d, xy = gen_keypair()
    kid = key_id(xy)
    priv_path = store_save(d, xy)                    # закрытый — сразу под DPAPI, открытым файлом не пишется
    os.makedirs(PUB_DIR, exist_ok=True)
    with open(pub_path(kid), "wb") as f:
        f.write(xy)
    with open(CURRENT_FILE, "w", encoding="ascii") as f:
        f.write(kid.hex() + "\n")
    print("новая пара журналов: keyId %s; закрытый ключ (DPAPI): %s\n  ★ сделайте export-backup" % (kid.hex(), priv_path))
    return remask()


# ── расшифровка ────────────────────────────────────────────────────────────────────────────────────────────────
def _hkdf_c2s(raw, client_hello, server_hello):
    """HKDF как AsdaSecure::Session::DeriveKeys → (c2sKey, c2sIv) — ключ исходящего направления клиента."""
    salt = client_hello[4 + PUB_LEN:4 + PUB_LEN + 16] + server_hello[4 + PUB_LEN:4 + PUB_LEN + 16]
    info = bytes([SECURE_VERSION, client_hello[2], server_hello[2]]) + client_hello[4:4 + PUB_LEN] + \
        server_hello[4:4 + PUB_LEN] + b"AsdaSecure/v1\x00\x00\x00"
    prk = hmac.new(salt, raw, hashlib.sha256).digest()
    okm, t, c = b"", b"", 1
    while len(okm) < 88:
        t = hmac.new(prk, t + info + bytes([c]), hashlib.sha256).digest()
        okm += t
        c += 1
    return okm[0:32], okm[32:44]


def _ecdh_raw(d, peer_xy):
    from cryptography.hazmat.primitives.asymmetric import ec
    priv = ec.derive_private_key(int.from_bytes(d, "big"), ec.SECP256R1())
    pub = ec.EllipticCurvePublicNumbers(int.from_bytes(peer_xy[:32], "big"), int.from_bytes(peer_xy[32:], "big"),
                                        ec.SECP256R1()).public_key()
    return priv.exchange(ec.ECDH(), pub)


class Segment:
    """Разобранный сегмент: вид, keyId, записи [(тип, байты)], признаки ошибок."""
    def __init__(self, offset):
        self.offset = offset
        self.kind = 0
        self.kid = b""
        self.records = []
        self.error = None        # текст ошибки (сегмент дальше не читался)
        self.truncated = False   # хвост обрезан


def parse(data, keys):
    """data — байты файла; keys(kid) → (d, xy) или None. Возврат: список Segment."""
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    segs = []
    pos = 0
    n = len(data)
    while pos < n:
        if data[pos:pos + 8] != SEG_MAGIC:
            s = Segment(pos)
            s.error = "на смещении %d нет метки сегмента — мусор/порча, дальше файл не читается" % pos
            segs.append(s)
            break
        s = Segment(pos)
        segs.append(s)
        if n - pos < SEG_HDR_LEN:
            s.truncated = True
            s.error = "заголовок сегмента обрезан"
            break
        hdr = data[pos:pos + SEG_HDR_LEN]
        s.kind = hdr[9]
        s.kid = hdr[12:20]
        ch = hdr[20:104]
        snonce = hdr[104:120]
        pos += SEG_HDR_LEN
        key = keys(s.kid) if hdr[8] == FORMAT_VERSION else None
        # Кадры сегмента — до следующей метки или конца.
        aes = iv_base = None
        if hdr[8] != FORMAT_VERSION:
            s.error = "версия формата %d не поддерживается" % hdr[8]
        elif ch[0] != CLIENT_MAGIC or ch[1] != SECURE_VERSION or ch[2] != LOG_SUBSERVER:
            s.error = "ClientHello сегмента не журнальный"
        elif key is None:
            s.error = "нет закрытого ключа для keyId %s" % s.kid.hex()
        else:
            d, xy = key
            sh = bytes([SERVER_MAGIC, SECURE_VERSION, LOG_SUBSERVER, 0]) + xy + snonce
            k, iv_base = _hkdf_c2s(_ecdh_raw(d, ch[4:4 + PUB_LEN]), ch, sh)
            aes = AESGCM(k)
        expect = 1
        while pos < n:
            if data[pos:pos + 4] == SEG_MAGIC[:4]:
                break
            if n - pos < FRAME_HDR:
                s.truncated = True
                pos = n
                break
            clen, seq = struct.unpack_from("<IQ", data, pos)
            if clen < TAG_LEN or clen > MAX_PLAIN + TAG_LEN:
                if s.error is None:
                    s.error = "кадр на смещении %d: длина %d вне допустимого — порча" % (pos, clen)
                pos = n
                break
            if n - pos < FRAME_HDR + clen:
                s.truncated = True
                pos = n
                break
            if aes is not None and s.error is None:
                if seq != expect:
                    s.error = "кадр на смещении %d: seq %d вместо %d (пропуск/перестановка)" % (pos, seq, expect)
                else:
                    iv = bytearray(iv_base)
                    for i in range(8):
                        iv[11 - i] ^= (seq >> (8 * i)) & 0xFF
                    try:
                        plain = aes.decrypt(bytes(iv), data[pos + FRAME_HDR:pos + FRAME_HDR + clen], data[pos:pos + FRAME_HDR])
                        if plain:
                            s.records.append((plain[0], plain[1:]))
                        expect += 1
                    except Exception:
                        s.error = "кадр на смещении %d: тег GCM не сошёлся (подмена/порча)" % pos
            pos += FRAME_HDR + clen
    return segs


def _keys_fn(explicit=None):
    cache = {}

    def get(kid):
        if kid not in cache:
            cache[kid] = find_private(kid, explicit)
        return cache[kid]
    return get


def _esc(t):
    """Текст из журнала → без управляющих символов и переключателей направления (кроме \\n и \\t): они показываются
    как \\xNN/\\uNNNN — присланный журнал не может прятать строки, перекрашивать консоль или переставлять текст."""
    return _CTRL_RE.sub(lambda m: ("\\x%02x" % ord(m.group())) if ord(m.group()) < 0x100 else ("\\u%04x" % ord(m.group())), t)


def textz_decode(b):
    """Запись TextZ (★ 31.10.2026): u32 длина + zlib → (байты UTF-8, None) или (b"", причина). Распаковывается не больше
    заявленной длины (≤ TEXTZ_MAX) — присланный файл не раздует память."""
    import zlib
    if len(b) < 5:
        return b"", "короче заголовка"
    n = struct.unpack_from("<I", b, 0)[0]
    if n > TEXTZ_MAX:
        return b"", "заявлено %d б — больше предела" % n
    try:
        z = zlib.decompressobj()
        raw = z.decompress(b[4:], n + 1)
    except zlib.error as e:
        return b"", "поток zlib испорчен (%s)" % e
    if len(raw) != n or not z.eof:
        return b"", "распаковано %d б вместо %d" % (len(raw), n)
    return raw, None


def segment_info(seg):
    """Сведения сегмента (первая запись Info) → словарь «ключ: значение»."""
    for t, b in seg.records:
        if t == REC_INFO:
            out = {}
            for ln in b.decode("utf-8", "replace").splitlines():
                k, sep, v = ln.partition("=")
                if sep:
                    out[k.strip()] = v.strip()
            return out
    return {}


def render_text(segs, name):
    """Сегменты → читаемый текст (сведения сегментов, строки журнала, перечень файлов пакета)."""
    out = io.StringIO()
    for i, s in enumerate(segs):
        out.write("==== сегмент %d (смещение %d): %s, keyId %s ====\n" % (i + 1, s.offset, KINDS.get(s.kind, "вид %d" % s.kind), s.kid.hex()))
        for t, b in s.records:
            if t == REC_INFO:
                out.write("[сведения] " + _esc(b.decode("utf-8", "replace").rstrip("\n").replace("\n", "; ")) + "\n")
            elif t == REC_TEXT:
                out.write(_esc(b.decode("utf-8", "replace")))
            elif t == REC_TEXTZ:
                raw, err = textz_decode(b)
                out.write(_esc(raw.decode("utf-8", "replace")) if err is None else "[!] запись TextZ: %s\n" % err)
            elif t == REC_FBEGIN and len(b) >= 2 and len(b) >= 2 + struct.unpack_from("<H", b, 0)[0] + 8:
                ln = struct.unpack_from("<H", b, 0)[0]
                out.write("[файл] %s (%d б)%s\n" % (_esc(b[2:2 + ln].decode("utf-8", "replace")), struct.unpack_from("<Q", b, 2 + ln)[0],
                                                   "" if s.kind in FILE_KINDS else " — не извлекается: вид сегмента без файлов"))
        if s.truncated:
            out.write("[!] хвост сегмента обрезан (запись прервана) — всё до него прочитано\n")
        if s.error:
            out.write("[!] ОШИБКА: %s\n" % s.error)
    return out.getvalue()


def safe_member_path(out_dir, name):
    """Имя файла из пакета → путь СТРОГО внутри out_dir (★ 19.10.2026, ревизия Б1). Каждая часть имени чистится: без «:»
    (буква диска, альтернативный поток), «*?"<>|» и управляющих символов, без «.»/«..», хвостовых точек и пробелов; имена
    устройств (CON, NUL, COM1…, в том числе с расширением) получают префикс «_». Итоговый realpath сверяется с out_dir —
    иначе ValueError. Возврат: (полный путь, очищенное имя через «/»)."""
    parts = [p for p in name.replace("\\", "/").split("/") if p not in ("", ".", "..")]
    clean = []
    for p in parts:
        p = re.sub(r"[^\w .()\-]", "_", p, flags=re.UNICODE).rstrip(" .") or "_"
        if p.split(".")[0].upper() in WIN_DEVICES:
            p = "_" + p
        clean.append(p[:120])
    if not clean:
        clean = ["_"]
    root = os.path.realpath(out_dir)
    path = os.path.realpath(os.path.join(root, *clean))
    if not path.startswith(root + os.sep):
        raise ValueError("имя файла пакета вне папки расшифровки: %r" % name)
    return path, "/".join(clean)


def _open_new(path):
    """Открыть НОВЫЙ файл ('xb', без перезаписи); занято — «имя~2.расш», «имя~3.расш»…"""
    stem, ext = os.path.splitext(path)
    for i in range(1, 1000):
        cand = path if i == 1 else "%s~%d%s" % (stem, i, ext)
        try:
            return open(cand, "xb"), cand
        except FileExistsError:
            continue
    raise FileExistsError(path)


def extract_files(segs, out_dir):
    """Файлы из записей 0x10/0x11/0x12 сегментов видов FILE_KINDS → out_dir. Возврат: [(имя, размер, полный?)].
    Имена — через safe_member_path (за пределы out_dir не выйти), файлы только создаются (существующие не перезаписываются)."""
    done = []
    cur = None
    os.makedirs(out_dir, exist_ok=True)

    def close_cur(full_check):
        if cur:
            cur[0].close()
            done.append((cur[1], cur[3], full_check and cur[3] == cur[2]))

    for s in segs:
        if s.kind not in FILE_KINDS:
            continue
        for t, b in s.records:
            if t == REC_FBEGIN:
                close_cur(False)   # прежний файл без записи конца — неполный
                cur = None
                if len(b) < 2 or len(b) < 2 + struct.unpack_from("<H", b, 0)[0] + 8:
                    continue
                ln = struct.unpack_from("<H", b, 0)[0]
                name = b[2:2 + ln].decode("utf-8", "replace")
                size = struct.unpack_from("<Q", b, 2 + ln)[0]
                try:
                    path, safe = safe_member_path(out_dir, name)
                except ValueError as e:
                    print("  [!] " + _esc(str(e)))
                    continue
                os.makedirs(os.path.dirname(path), exist_ok=True)
                if not os.path.realpath(os.path.dirname(path)).startswith(os.path.realpath(out_dir)):
                    print("  [!] папка файла пакета вне папки расшифровки: %r" % _esc(name))
                    continue
                f, _real = _open_new(path)
                cur = [f, safe, size, 0]
            elif t == REC_FDATA and cur:
                cur[0].write(b)
                cur[3] += len(b)
            elif t == REC_FEND and cur:
                close_cur(True)
                cur = None
    close_cur(False)
    return done


def decrypt_path(path, out_dir=None, key=None):
    """Один файл: .txt со всем текстом; если есть файлы пакета — папка <имя>.d. Возврат: число ошибок."""
    data = open(path, "rb").read()
    segs = parse(data, _keys_fn(key))
    base = os.path.join(out_dir or os.path.dirname(os.path.abspath(path)), os.path.basename(path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    txt = render_text(segs, os.path.basename(path))
    with open(base + ".txt", "w", encoding="utf-8", newline="\n") as f:
        f.write(txt)
    files = []
    if any(t == REC_FBEGIN for s in segs for t, _ in s.records):
        files = extract_files(segs, base + ".d")
    errs = sum(1 for s in segs if s.error)
    print("%s: сегментов %d, ошибок %d, обрезанных %d → %s.txt%s" % (
        path, len(segs), errs, sum(1 for s in segs if s.truncated), base,
        ("; файлы: " + ", ".join("%s (%d б%s)" % (a, b, "" if c else ", НЕПОЛНЫЙ") for a, b, c in files)) if files else ""))
    return errs


# ── ★ 31.10.2026 склейка частей журнала сессии ───────────────────────────────────────────────────────────────
def session_part_of(segs, path):
    """Часть журнала сессии игры (вид 3) → (ключ сессии, номер части) или None. Ключ и номер — из сведений сегмента
    (session_id=, part=); у файлов до 31.10 их нет — тогда из session= (имя файла у игрока), часть 1."""
    for s in segs:
        if s.kind != 3:
            continue
        info = segment_info(s)
        sid, part = info.get("session_id", ""), info.get("part", "")
        if sid and part.isdigit():
            return sid, int(part)
        m = PART_NAME_RE.match(info.get("session", "") or os.path.basename(path))
        if m:
            return m.group("key"), int(m.group("part") or 1)
    return None


JOIN_CACHE = ".join_cache.json"   # кэш склейки в папке вывода: файл → (размер, время, логин, сессия, часть)


def _file_login(path):
    """Логин файла с сервера: account_claimed из соседнего .json приёмника (при account_status ok), иначе — имя папки логина
    (раскладка <логин>/<дата>/<файл>), иначе пусто. Логин заявлен клиентом — для группировки, не доказательство."""
    meta = os.path.splitext(path)[0] + ".json"
    if os.path.isfile(meta):
        try:
            import json
            with open(meta, encoding="utf-8") as f:
                j = json.load(f)
            if j.get("account_status") == "ok" and j.get("account_claimed"):
                return str(j["account_claimed"]).lower()
        except (OSError, ValueError):
            pass
    day_dir = os.path.dirname(os.path.abspath(path))
    if not re.match(r"^\d{4}-\d{2}-\d{2}$", os.path.basename(day_dir)):   # не раскладка <логин>/<дата> — логина нет
        return ""
    parent = os.path.basename(os.path.dirname(day_dir))
    return parent.lower() if re.match(r"^[A-Za-z0-9_.@-]{1,32}$", parent) and not parent.startswith("_") else ""


def join_paths(paths, out_dir, key=None, keys=None):
    """Части журналов сессий (файлы/папки .alog, в том числе со случайными серверными именами) → по файлу
    <out_dir>/<сессия>[__<логин>].joined.txt: части по порядку, каждая со своим заголовком; пропуски номеров названы
    (первая часть хранится всегда, средние клиент удаляет сверх 6 частей; хвостовые могут быть ещё не выгружены).
    Группа — (логин, session_id): одинаковый ключ сессии у разных учёток (другой ПК с тем же временем и pid) не смешивается;
    суффикс логина в имени — только если сессия встретилась у нескольких логинов. Повторно выгруженная часть берётся один
    раз (самая полная). Кэш <out_dir>/.join_cache.json: неизменившиеся файлы (размер+время) не расшифровываются заново,
    склейка пересобирается только для сессий с новыми/изменившимися частями. keys — функция ключей (по умолчанию — по keyId
    из хранилища или key). Возврат: (число пересобранных сессий, число ошибок разбора)."""
    import json
    files = []
    for p in paths:
        if os.path.isdir(p):
            for r, _, fs in os.walk(p):
                files += [os.path.join(r, f) for f in fs if f.lower().endswith(".alog")]
        elif p.lower().endswith(".alog"):
            files.append(p)
    keys = keys or _keys_fn(key)
    os.makedirs(out_dir, exist_ok=True)
    cache_path = os.path.join(out_dir, JOIN_CACHE)
    cache = {}
    if os.path.isfile(cache_path):
        try:
            with open(cache_path, encoding="utf-8") as f:
                cache = json.load(f)
        except (OSError, ValueError):
            cache = {}
    members = {}       # (логин, сессия) → {часть: [файлы]}
    dirty = set()      # группы с новыми/изменившимися файлами
    rendered = {}      # файл → (текст, ошибок, обрезан) для файлов, расшифрованных в этом проходе
    errs = 0
    new_cache = {}
    for f in sorted(set(os.path.abspath(x) for x in files)):
        st = os.stat(f)
        sig = [st.st_size, int(st.st_mtime)]
        c = cache.get(f)
        if c and c[:2] == sig:
            grp, part = (c[2], c[3]), c[4]
            new_cache[f] = c
            if c[3] is None:
                continue
        else:
            with open(f, "rb") as fh:
                segs = parse(fh.read(), keys)
            sp = session_part_of(segs, f)
            login = _file_login(f)
            new_cache[f] = sig + [login, sp[0] if sp else None, sp[1] if sp else 0]
            if sp is None:
                continue
            grp, part = (login, sp[0]), sp[1]
            bad = sum(1 for s in segs if s.error)
            errs += bad
            rendered[f] = (render_text(segs, os.path.basename(f)), bad, any(s.truncated for s in segs))
            dirty.add(grp)
        members.setdefault(grp, {}).setdefault(part, []).append(f)
    logins_of = {}
    for login, sid in members:
        logins_of.setdefault(sid, set()).add(login)

    def target_of(login, sid):
        name = re.sub(r"[^\w.\-]", "_", sid) + (("__" + re.sub(r"[^\w.@\-]", "_", login or "_unknown")) if len(logins_of[sid]) > 1 else "")
        return os.path.join(out_dir, name + ".joined.txt")
    dirty |= {g for g in members if not os.path.isfile(target_of(*g))}   # склейку удалили — пересобрать
    for grp in sorted(dirty):
        login, sid = grp
        parts = {}
        for n, fl in members[grp].items():
            best = None
            for f in fl:
                if f not in rendered:   # из кэша, а группа изменилась — расшифровать для текста
                    with open(f, "rb") as fh:
                        segs = parse(fh.read(), keys)
                    rendered[f] = (render_text(segs, os.path.basename(f)), sum(1 for s in segs if s.error), any(s.truncated for s in segs))
                if best is None or len(rendered[f][0]) > len(rendered[best][0]):
                    best = f
            parts[n] = (best,) + rendered[best]
        nums = sorted(parts)
        missing = [n for n in range(1, nums[-1] + 1) if n not in parts]
        out = io.StringIO()
        out.write("#### сессия %s%s: части %s (последняя известная — %d)%s ####\n" % (
            sid, (" (логин %s)" % login) if login else "", ", ".join(map(str, nums)), nums[-1],
            ("; НЕТ частей %s — удалены клиентом сверх предела хранения (6) или ещё не выгружены" % ", ".join(map(str, missing))) if missing else ""))
        prev = None
        for n in nums:
            f, text, bad, trunc = parts[n]
            if prev is not None and n != prev + 1:
                out.write("#### ПРОПУСК: частей %d–%d нет ####\n" % (prev + 1, n - 1))
            out.write("#### часть %d — %s%s%s ####\n" % (n, os.path.basename(f), ", ОШИБКИ РАЗБОРА" if bad else "",
                                                     ", хвост обрезан (сбой записи/падение — дальше следующая часть)" if trunc else ""))
            out.write(text)
            prev = n
        target = target_of(login, sid)
        with open(target, "w", encoding="utf-8", newline="\n") as fo:
            fo.write(out.getvalue())
        print("%s%s: частей %d (%s)%s → %s" % (sid, (" [%s]" % login) if login else "", len(nums), ", ".join(map(str, nums)),
                                             ("; нет " + ", ".join(map(str, missing))) if missing else "", target))
    with open(cache_path, "w", encoding="utf-8") as f:
        json.dump(new_cache, f)
    return len(dirty), errs


def _read_bytes(path):
    """Байты файла (файл закрывается сразу)."""
    with open(path, "rb") as f:
        return f.read()


def verify_rotation(d):
    """Расшифровать вывод asda.SelfTest.LogSealRotate в папке d и сверить: каждая часть расшифровывается сама по себе,
    строки ROT-LINE непрерывны внутри частей и на стыках, пропуск только там, где клиент удалил средние части, склейка
    даёт непрерывный журнал, хвост в пакете падения = конец последней части, после сбоя записи новая часть читается
    целиком, ключ 0 — прежняя обрезка. Итоги прогона клиента (число строк, частей, имена файлов) — <d>/rot_result.txt.
    Возврат: число провалов."""
    import shutil
    import tempfile
    ok = fail = 0

    def expect(c, what):
        nonlocal ok, fail
        if c:
            ok += 1
            print("ok   " + what)
        else:
            fail += 1
            print("FAIL " + what)
    # Ключ: тестовый получатель самотеста (<d>/test.key от gen-test-key) или — без него — ключ владельца по keyId.
    test_key = os.path.join(d, "test.key")
    key = test_key if os.path.isfile(test_key) else None
    keys = _keys_fn(key)
    # rot_result.txt: «<случай>.lines=…», «.parts=», «.dropped=», «.files=<имена через запятую>», «.crash=<пакет>».
    res = {}
    with io.open(os.path.join(d, "rot_result.txt"), encoding="utf-8") as f:
        for ln in f:
            k, _, v = ln.strip().partition("=")
            res[k] = v
    line_re = re.compile(r"ROT-LINE #(\d{7}) ")   # номер строки самотеста — по нему видны потери и порядок

    def numbers(text):
        return [int(x) for x in line_re.findall(text)]

    def consecutive(n):
        return n == list(range(n[0], n[0] + len(n))) if n else False

    for case in ("rot_on", "rot_fail", "rot_plain", "rot_off"):
        if case + ".files" not in res:
            expect(False, "%s: нет итогов прогона в rot_result.txt" % case)
            continue
        sess = os.path.join(d, case, "Sessions")
        names = [x for x in res.get(case + ".files", "").split(",") if x]
        lines = int(res.get(case + ".lines", "0"))
        parts = int(res.get(case + ".parts", "0"))
        # 1) каждая часть отдельно: один сегмент, без ошибок; обрезка допустима только у части, оборванной сбоем записи
        texts, part_nums, segs_all, all_ok = [], [], [], True
        for i, nm in enumerate(names):
            segs = parse(_read_bytes(os.path.join(sess, nm)), keys)
            sp = session_part_of(segs, nm)
            trunc_ok = case == "rot_fail" and i == 0
            all_ok &= len(segs) == 1 and not segs[0].error and (trunc_ok or not segs[0].truncated) and sp is not None
            part_nums.append(sp[1] if sp else -1)
            texts.append(render_text(segs, nm))
            segs_all.append(segs)
        expect(all_ok and names, "%s: каждая из %d частей расшифровывается сама по себе, без ошибок%s" % (
            case, len(names), " (первая — обрезана сбоем записи, всё до обрыва читается)" if case == "rot_fail" else " и обрезки"))
        nums = [numbers(t) for t in texts]
        expect(all(consecutive(n) for n in nums), "%s: строки ROT-LINE внутри каждой части идут подряд" % case)
        if case == "rot_off":
            # 2) ключ 0: один файл, после 3,5 МиБ строки Log отброшены (последней строки прогона нет)
            t = texts[0] if texts else ""
            expect(len(names) == 1 and "дальше пишутся только Warning/Error/Fatal" in t and nums and nums[0][-1] < lines - 1
                   and "ROT-LAST" not in t, "rot_off (ключ 0): один файл, пометка мягкого предела, хвост строк Log отброшен (последняя %s из %d)"
                   % (nums[0][-1] if nums and nums[0] else "?", lines))
            continue
        z = sum(1 for segs in segs_all for s in segs for t, _ in s.records if t == REC_TEXTZ)
        if case == "rot_on":
            # 3) сжатие и хранение: части 1 и 5 последних, на стыках сохранившихся частей строки без потерь
            expect(z > 0, "rot_on: строки записаны сжатыми записями TextZ (%d)" % z)
            expect(part_nums == [1] + list(range(parts - 4, parts + 1)), "rot_on: номера частей в сведениях %s = 1 и %d..%d" % (part_nums, parts - 4, parts))
            seams = all(nums[i + 1][0] == nums[i][-1] + 1 for i in range(1, len(nums) - 1))
            expect(seams, "rot_on: на стыках сохранившихся частей 2..6 строки продолжаются без потерь")
        elif case == "rot_fail":
            # 4) сбой записи: часть 1 до обрыва, затем часть 2 с пометкой о сбое; строки после восстановления подряд до конца
            expect(part_nums == list(range(1, parts + 1)), "rot_fail: номера частей %s = 1..%d (неудачная попытка части не оставила файла)" % (part_nums, parts))
            expect(len(texts) > 1 and "после СБОЯ ЗАПИСИ части" in texts[1], "rot_fail: вторая часть начинается пометкой о сбое записи первой")
            after = [x for n in nums[1:] for x in n]
            expect(nums[0] and after and after == list(range(after[0], lines)) and after[0] > nums[0][-1],
                   "rot_fail: после восстановления строки %d..%d подряд, до сбоя — 0..%d (потеряны только строки за время сбоя)"
                   % (after[0] if after else -1, lines - 1, nums[0][-1] if nums and nums[0] else -1))
        else:
            # 5) без сжатия: все части хранятся, все строки от 0 до конца
            expect(z == 0, "rot_plain: без сжатия записей TextZ нет")
            expect(part_nums == list(range(1, parts + 1)), "rot_plain: номера частей %s = 1..%d" % (part_nums, parts))
            seams = all(nums[i + 1][0] == nums[i][-1] + 1 for i in range(len(nums) - 1))
            expect(seams and nums[0][0] == 0 and nums[-1][-1] == lines - 1, "rot_plain: все %d строк от 0 до конца, на стыках без потерь" % lines)
        expect("ROT-LAST #%07d" % lines in texts[-1] and "журнал сессии закрыт" in texts[-1], "%s: последняя часть кончается строкой ROT-LAST и закрытием" % case)
        if case != "rot_fail":
            expect(all("продолжение: " in t for t in texts[:-1]) and all("продолжение сессии" in t for t in texts[1:]),
                   "%s: у частей пометки «закрыта — продолжение» и «продолжение сессии»" % case)
        # 6) склейка той же папки: одна сессия, части по порядку
        tmp = tempfile.mkdtemp(prefix="logseal_join_")
        n, e = join_paths([sess], tmp, key)
        jpath = os.path.join(tmp, "20261031-120000_4242.joined.txt")
        joined = io.open(jpath, encoding="utf-8").read() if os.path.isfile(jpath) else ""
        jn = numbers(joined)
        if case == "rot_on":
            gap_ok = "ПРОПУСК: частей 2–%d нет" % (parts - 5) in joined
            tail = jn[len(nums[0]):]
            expect(e == 0 and gap_ok and jn[:len(nums[0])] == nums[0] and tail and tail == list(range(tail[0], lines)),
                   "rot_on: склейка — часть 1, названный пропуск 2–%d, затем строки подряд до %d" % (parts - 5, lines - 1))
            n2, _e2 = join_paths([sess], tmp, key)
            expect(n2 == 0, "rot_on: повторная склейка без новых частей ничего не пересобирает (кэш)")
            # 7) пакет падения: session_tail.txt = непрерывные последние строки последней части, session= — её имя
            crash = os.path.join(d, case, res.get(case + ".crash", ""))
            segs = parse(_read_bytes(crash), keys) if res.get(case + ".crash") and os.path.isfile(crash) else []
            tdir = os.path.join(tmp, "crash.d")
            got = extract_files(segs, tdir) if segs else []
            tail_txt = ""
            if any(nm == "session_tail.txt" and full for nm, _sz, full in got):
                tail_txt = _read_bytes(os.path.join(tdir, "session_tail.txt")).decode("utf-8", "replace")
            info = segment_info(segs[0]) if segs else {}
            expect(segs and not segs[0].error and "ROT-LAST #%07d" % lines in tail_txt and 60000 <= len(tail_txt) <= 140000
                   and info.get("session") == names[-1] and "session_tail.txt" in info.get("content", ""),
                   "rot_on: пакет падения — session_tail.txt (%d символов) кончается строкой ROT-LAST, session=%s" % (len(tail_txt), info.get("session")))
            last_line = numbers(tail_txt)
            expect(last_line and last_line[-1] == lines - 1 and consecutive(last_line), "rot_on: хвост падения — непрерывные последние строки последней части")
        elif case == "rot_fail":
            expect(e == 0 and "ПРОПУСК" not in joined and "хвост обрезан" in joined and jn[-1] == lines - 1,
                   "rot_fail: склейка — части 1..%d без пропусков, обрыв первой назван, строки до конца" % parts)
        else:
            expect(n == 1 and e == 0 and "ПРОПУСК" not in joined and jn == list(range(lines)), "rot_plain: склейка — непрерывный журнал из %d строк" % lines)
        shutil.rmtree(tmp, ignore_errors=True)
    print("LOGSEAL-ROTATE-VERIFY ИТОГ: ok=%d fail=%d" % (ok, fail))
    return fail


# ── самопроверка разбора (эмуляция клиента на Python) ─────────────────────────────────────────────────────────
def _seal_segment_py(xy, kind, records, info=b"selftest=1\n"):
    """Сегмент так, как его пишет клиент (для selftest): эфемерный ключ, HKDF, кадры Session::Seal."""
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    e = ec.generate_private_key(ec.SECP256R1())
    en = e.public_key().public_numbers()
    epub = en.x.to_bytes(32, "big") + en.y.to_bytes(32, "big")
    cnonce, snonce = secrets.token_bytes(16), secrets.token_bytes(16)
    ch = bytes([CLIENT_MAGIC, SECURE_VERSION, LOG_SUBSERVER, 0]) + epub + cnonce
    sh = bytes([SERVER_MAGIC, SECURE_VERSION, LOG_SUBSERVER, 0]) + xy + snonce
    peer = ec.EllipticCurvePublicNumbers(int.from_bytes(xy[:32], "big"), int.from_bytes(xy[32:], "big"), ec.SECP256R1()).public_key()
    k, ivb = _hkdf_c2s(e.exchange(ec.ECDH(), peer), ch, sh)
    out = bytearray(SEG_MAGIC + bytes([FORMAT_VERSION, kind, 0, 0]) + key_id(xy) + ch + snonce + bytes(8))
    for seq, rec in enumerate([bytes([REC_INFO]) + info] + records, 1):
        iv = bytearray(ivb)
        for i in range(8):
            iv[11 - i] ^= (seq >> (8 * i)) & 0xFF
        hdr = struct.pack("<IQ", len(rec) + TAG_LEN, seq)
        out += hdr + AESGCM(k).encrypt(bytes(iv), rec, hdr)
    return bytes(out)


def selftest():
    ok = fail = 0

    def expect(c, what):
        nonlocal ok, fail
        if c:
            ok += 1
            print("ok   " + what)
        else:
            fail += 1
            print("FAIL " + what)
    d, xy = gen_keypair()
    keys = lambda kid: (d, xy) if kid == key_id(xy) else None
    a = _seal_segment_py(xy, 1, [bytes([REC_TEXT]) + "строка 1\n".encode(), bytes([REC_TEXT]) + b"line 2\n"])
    b = _seal_segment_py(xy, 1, [bytes([REC_TEXT]) + b"run 2\n"])
    segs = parse(a + b, keys)
    expect(len(segs) == 2 and not any(s.error for s in segs), "два сегмента подряд читаются без ошибок")
    expect(render_text(segs, "x").count("строка 1") == 1 and "run 2" in render_text(segs, "x"), "текст обоих сегментов на месте")
    segs = parse(a[:-5], keys)
    expect(segs[0].truncated and not segs[0].error and len(segs[0].records) == 2, "обрезанный хвост: предыдущие кадры читаются, флаг обрезки")
    bad = bytearray(a)
    bad[SEG_HDR_LEN + 20] ^= 1
    expect(parse(bytes(bad), keys)[0].error is not None, "порча шифротекста → ошибка тега")
    swapped = a[:SEG_HDR_LEN]
    frames = []
    p = SEG_HDR_LEN
    while p < len(a):
        clen = struct.unpack_from("<I", a, p)[0]
        frames.append(a[p:p + FRAME_HDR + clen])
        p += FRAME_HDR + clen
    expect("seq" in (parse(swapped + frames[0] + frames[2] + frames[1], keys)[0].error or ""), "перестановка кадров → ошибка seq")
    d2, xy2 = gen_keypair()
    expect("нет закрытого ключа" in (parse(_seal_segment_py(xy2, 1, []), keys)[0].error or ""), "чужой keyId → «нет закрытого ключа»")
    payload = secrets.token_bytes(150000)
    recs = [bytes([REC_FBEGIN]) + struct.pack("<H", 5) + b"a.bin" + struct.pack("<Q", len(payload))]
    for o in range(0, len(payload), 60000):
        recs.append(bytes([REC_FDATA]) + payload[o:o + 60000])
    recs.append(bytes([REC_FEND]))
    import tempfile
    tmp = tempfile.mkdtemp(prefix="logseal_")
    files = extract_files(parse(_seal_segment_py(xy, 4, recs), keys), tmp)
    expect(files == [("a.bin", len(payload), True)] and open(os.path.join(tmp, "a.bin"), "rb").read() == payload,
           "пакет файла 150 000 б (3 куска) извлекается байт-в-байт")
    # ★ 19.10.2026 (ревизия Б1): вредные имена файлов пакета не выходят за папку и ничего не перезаписывают
    evil = ["C:/x.bat", "D:x.bat", "a:b.txt", "CON", "nul.txt", "../../up.txt", "..\\..\\up2.txt", "sub/../../up3.txt",
            "\\\\srv\\share\\x", "/abs.txt", "ok/inner.txt", "a.txt."]
    recs = []
    for nm in evil:
        nb = nm.encode()
        recs += [bytes([REC_FBEGIN]) + struct.pack("<H", len(nb)) + nb + struct.pack("<Q", 3), bytes([REC_FDATA]) + b"bad",
                 bytes([REC_FEND])]
    out = os.path.join(tmp, "evil.d")
    before = set(os.listdir(tmp))
    got = extract_files(parse(_seal_segment_py(xy, 4, recs), keys), out)
    inside = []
    for r, _dirs, fs in os.walk(out):
        inside += [os.path.join(r, f) for f in fs]
    root = os.path.realpath(out) + os.sep
    expect(len(got) == len(evil) and len(inside) == len(evil) and all(os.path.realpath(x).startswith(root) for x in inside),
           "вредные имена (C:, D:, a:b, CON, NUL, .., UNC, абсолютный): %d файлов, все внутри папки" % len(inside))
    expect(set(os.listdir(tmp)) - before == {"evil.d"} and not os.path.exists(os.path.join(os.path.dirname(tmp), "up.txt")),
           "вне папки ничего не создано")
    expect(not any(os.path.basename(x).split(".")[0].upper() in WIN_DEVICES for x in inside) and
           not any(":" in os.path.basename(x) for x in inside), "нет имён устройств и «:» среди созданных")
    got2 = extract_files(parse(_seal_segment_py(xy, 4, recs[:3]), keys), out)
    expect(len(got2) == 1 and any("~2" in f for _r, _d, fs in os.walk(out) for f in fs),
           "повторное извлечение не перезаписывает (имя~2)")
    e1 = extract_files(parse(_seal_segment_py(xy, 3, recs[:3]), keys), os.path.join(tmp, "k3.d"))
    expect(e1 == [] and not os.listdir(os.path.join(tmp, "k3.d")), "в сегменте журнала (вид 3) файлы не извлекаются")
    txt = render_text(parse(_seal_segment_py(xy, 3, [bytes([REC_TEXT]) + b"a\x1b[2Jb\rc\xe2\x80\xaed\n"]), keys), "x")
    expect("\x1b" not in txt and "\r" not in txt and "\u202e" not in txt and "\\x1b" in txt and "\\u202e" in txt,
           "управляющие символы и RLO в тексте экранированы")
    # ★ 31.10.2026: запись TextZ и склейка частей со случайными (серверными) именами файлов.
    import zlib
    body = ("строка журнала %d\r\n" * 3) % (1, 2, 3)
    zb = body.encode()
    zrec = bytes([REC_TEXTZ]) + struct.pack("<I", len(zb)) + zlib.compress(zb)
    txt = render_text(parse(_seal_segment_py(xy, 3, [zrec]), keys), "z")
    expect(body.replace("\r", "\\x0d") in txt, "запись TextZ распаковывается в тот же текст")
    bomb = bytes([REC_TEXTZ]) + struct.pack("<I", 10) + zlib.compress(b"x" * 100000)
    lie = bytes([REC_TEXTZ]) + struct.pack("<I", TEXTZ_MAX + 1) + zlib.compress(b"x")
    txt = render_text(parse(_seal_segment_py(xy, 3, [bomb, lie, bytes([REC_TEXTZ]) + b"\x05\0\0\0garbage"]), keys), "z")
    expect(txt.count("[!] запись TextZ") == 3 and "xxxxxxxxxx" not in txt, "TextZ: длина не та / больше предела / порча — ошибка, без распаковки сверх заявленного")
    jdir = os.path.join(tmp, "join_in")
    os.makedirs(jdir)
    for nm, part, lines in (("a1b2c3.alog", 3, "C\n"), ("ffee.alog", 1, "A\n"), ("0099.alog", 3, "C\n"), ("zz.alog", 5, "E\n")):
        info = ("kind=3\nsession=20261031-101010_77_part%d.alog\nsession_id=20261031-101010_77\npart=%d\n" % (part, part)).encode()
        with open(os.path.join(jdir, nm), "wb") as f:
            f.write(_seal_segment_py(xy, 3, [bytes([REC_TEXT]) + lines.encode()], info))
    with open(os.path.join(jdir, "old.alog"), "wb") as f:   # файл до 31.10: без session_id/part — часть 1 по session=
        f.write(_seal_segment_py(xy, 3, [bytes([REC_TEXT]) + b"OLD\n"], b"kind=3\nsession=20261020-090000_5.alog\n"))
    n, e = join_paths([jdir], os.path.join(tmp, "join_out"), keys=keys)
    n_again, _e = join_paths([jdir], os.path.join(tmp, "join_out"), keys=keys)
    expect(n_again == 0, "склейка: повторный проход без новых файлов ничего не пересобирает (кэш по размеру и времени)")
    other = os.path.join(tmp, "join_in2", "otheruser", "2026-10-31")   # тот же ключ сессии у другой учётки (раскладка <логин>/<дата>)
    os.makedirs(other)
    with open(os.path.join(other, "x.alog"), "wb") as f:
        f.write(_seal_segment_py(xy, 3, [bytes([REC_TEXT]) + b"OTHER\n"], b"kind=3\nsession=20261031-101010_77.alog\nsession_id=20261031-101010_77\npart=1\n"))
    n3, _e = join_paths([jdir, os.path.join(tmp, "join_in2")], os.path.join(tmp, "join_out3"), keys=keys)
    outs = sorted(os.listdir(os.path.join(tmp, "join_out3")))
    expect("20261031-101010_77__otheruser.joined.txt" in outs and any(o.startswith("20261031-101010_77__") and "otheruser" not in o for o in outs),
           "склейка: одинаковый ключ сессии у разных логинов — разные файлы (%s)" % ", ".join(o for o in outs if o.endswith(".txt")))
    j = io.open(os.path.join(tmp, "join_out", "20261031-101010_77.joined.txt"), encoding="utf-8").read()
    expect(n == 2 and e == 0 and j.index("\nA\n") < j.index("\nC\n") < j.index("\nE\n") and j.count("\nC\n") == 1
           and "ПРОПУСК: частей 2–2" in j and "ПРОПУСК: частей 4–4" in j and "НЕТ частей 2, 4" in j,
           "склейка: части по номеру из сведений (имена файлов случайные), повтор части один раз, пропуски названы")
    expect(os.path.isfile(os.path.join(tmp, "join_out", "20261020-090000_5.joined.txt")), "склейка: журнал до 31.10 (без part) — сессия из одной части")
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    print("LOGSEAL-PY ИТОГ: ok=%d fail=%d" % (ok, fail))
    return fail


def verify_selftest(d):
    """Расшифровать вывод asda.SelfTest.LogSeal в папке d и сверить содержимое. Возврат: число провалов."""
    import glob
    import shutil
    import tempfile
    ok = fail = 0

    def expect(c, what):
        nonlocal ok, fail
        if c:
            ok += 1
            print("ok   " + what)
        else:
            fail += 1
            print("FAIL " + what)
    test_key = os.path.join(d, "test.key")
    keys = _keys_fn(test_key if os.path.isfile(test_key) else None)

    def segs_of(path):
        return parse(open(path, "rb").read(), keys) if os.path.isfile(path) else []

    def files_of(path):
        tmp = tempfile.mkdtemp(prefix="logseal_v_")
        segs = segs_of(path)
        got = extract_files(segs, tmp)
        data = {}
        for name, _size, _full in got:
            data[name] = open(os.path.join(tmp, *name.split("/")), "rb").read()
        shutil.rmtree(tmp, ignore_errors=True)
        return segs, got, data

    s = segs_of(os.path.join(d, "journal.alog"))
    t = render_text(s, "journal")
    expect(len(s) == 2 and not any(x.error or x.truncated for x in s), "journal.alog: 2 сегмента, ошибок и обрезки нет")
    expect("LOGSEAL-SELFTEST-MARKER-1 строка журнала" in t and "MARKER-2" in t and "MARKER-3" in t, "journal.alog: все три строки на месте (кириллица цела)")
    expect(all(x.records and x.records[0][0] == REC_INFO and b"kind=127" in x.records[0][1] for x in s), "journal.alog: первая запись сегмента — сведения kind=127")
    segs, got, data = files_of(os.path.join(d, "big.alog"))
    expect(got == [("big.bin", 150000, True)] and data.get("big.bin") == bytes(((i * 131 + 7) & 0xFF) for i in range(150000)),
           "big.alog: big.bin 150 000 б байт-в-байт")
    if os.path.isfile(test_key):
        t = render_text(segs_of(os.path.join(d, "override.alog")), "override")
        expect("LOGSEAL-SELFTEST-MARKER-4 тестовый получатель" in t and "ОШИБКА" not in t, "override.alog: расшифрован тестовым ключом")
    ac = glob.glob(os.path.join(d, "sweep", "Saved", "Crashes", "*.acrash"))
    lg = glob.glob(os.path.join(d, "sweep", "Saved", "Logs", "Legacy_*.alog"))
    expect(len(ac) == 1 and len(lg) == 1, "перенос: один .acrash и один Legacy_*.alog")
    if ac:
        _s, _g, data = files_of(ac[0])
        expect(data.get("Crashes/UECC-Windows-TEST_0000/CrashContext.runtime-xml") == b"LOGSEAL-SELFTEST-LEGACY-XML"
               and data.get("Crashes/UECC-Windows-TEST_0000/UEMinidump.dmp") == b"LOGSEAL-SELFTEST-LEGACY-DMP", "перенос падения: оба файла извлечены байт-в-байт")
    if lg:
        _s, _g, data = files_of(lg[0])
        raw = [v for k, v in data.items() if k.endswith("AsdaEac_raw.log")]
        expect(data.get("Logs/AsdaEac.log") == b"LOGSEAL-SELFTEST-LEGACY-LOG" and raw == [b"LOGSEAL-SELFTEST-LEGACY-RAW"],
               "перенос журналов: AsdaEac.log и AsdaEac_raw.log извлечены байт-в-байт")
    # ★ 19.10.2026 (ревизия безопасности, В4): файлы за джанкшеном и жёсткой ссылкой в пакеты переноса не попадают.
    victim = False
    for pkg in ac + lg:
        _s, _g, data = files_of(pkg)
        victim |= any(b"-VICTIM" in v for v in data.values()) or any("hardlink" in k or "JUNCTION" in k for k in data)
    expect(not victim, "перенос: содержимого за джанкшеном и жёсткой ссылкой в пакетах нет")
    segs, got, data = files_of(os.path.join(d, "crash_test.acrash"))
    summ = data.get("summary.txt", b"").decode("utf-8", "replace")
    dmp = data.get("minidump.dmp", b"")
    expect(not any(x.error or x.truncated for x in segs) and segs and segs[0].kind == 4, "crash_test.acrash: вид 4, без ошибок")
    expect("0xE0A5DA7E" in summ and "стек:" in summ and "#00" in summ, "сводка падения: код синтетического исключения и стек")
    expect(dmp[:4] == b"MDMP" and len(dmp) > 64 * 1024 and all(full for _n, _s2, full in got), "minidump.dmp: подпись MDMP, %d б, полный" % len(dmp))
    print("LOGSEAL-VERIFY ИТОГ: ok=%d fail=%d" % (ok, fail))
    return fail


def check(exes):
    errors = 0
    cur = current_public()
    print("открытый ключ  : %s" % PUB_DIR)
    stored = store_kids()
    print("хранилище DPAPI: %s (%s)" % (PRIV_STORE, ", ".join(k.hex() for k in stored) or "пусто"))
    if cur and cur[1] not in stored:
        print("  (закрытого ключа на этом ПК нет: сборка возможна, расшифровка — после import-backup)")
    plain = [n for n in (os.listdir(KEYS_DIR) if os.path.isdir(KEYS_DIR) else []) if n.endswith(".key")]
    if plain:
        print("! ОТКРЫТЫЕ файлы закрытых ключей: %s — protect-key" % ", ".join(plain))
        errors += 1
    print("текущая пара   : %s" % (cur[1].hex() if cur else "НЕТ"))
    hdr = read_header()
    print("заголовок      : %s" % (GEN_HEADER if hdr else "НЕТ"))
    if not cur or not hdr:
        return 1
    xy, kid = hdr
    same = xy == cur[0] and kid == cur[1] and key_id(xy) == kid
    print("доли → открытый ключ текущей пары, keyId сходится: %s" % ("ДА" if same else "НЕТ"))
    errors += 0 if same else 1
    for exe in exes or [os.path.join(PROJECT_DIR, "Binaries", "Win64", "AsdaReborn-Win64-Shipping.exe")]:
        if not os.path.isfile(exe):
            print("exe нет: %s" % exe)
            continue
        blob = open(exe, "rb").read()
        leak = xy in blob or xy[:32] in blob or xy[32:] in blob
        print("открытый ключ (X||Y, X, Y) в открытом виде в %s: %s" % (exe, "ЕСТЬ — ОШИБКА" if leak else "нет"))
        errors += 1 if leak else 0
    print("итог: %s" % ("ОК" if errors == 0 else "%d ошиб." % errors))
    return errors


def main(argv=None):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="Запечатанные журналы ритейл-клиента: ключи, заголовок, расшифровка")
    sub = ap.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen", help="новая пара (если нет) и заголовок клиента")
    k.add_argument("--force", action="store_true", help="новая пара, даже если текущая есть")
    sub.add_parser("header", help="перемаскировать заголовок клиента")
    c = sub.add_parser("check", help="сверить заголовок с ключом; ключа нет в exe")
    c.add_argument("--exe", action="append", help="exe для поиска ключа (можно несколько)")
    dd = sub.add_parser("decrypt", help="расшифровать .alog/.acrash (файл или папку)")
    dd.add_argument("path")
    dd.add_argument("-o", "--out", help="куда класть расшифровку (по умолчанию рядом)")
    dd.add_argument("--key", help="файл закрытого ключа (по умолчанию — по keyId из каталога ключей)")
    sub.add_parser("selftest", help="самопроверка разбора")
    v = sub.add_parser("verify-selftest", help="расшифровать и сверить вывод asda.SelfTest.LogSeal")
    v.add_argument("dir")
    vr = sub.add_parser("verify-rotation", help="расшифровать и сверить вывод asda.SelfTest.LogSealRotate (★ 31.10)")
    vr.add_argument("dir")
    jn = sub.add_parser("join", help="склеить части журналов сессий в <сессия>.joined.txt (★ 31.10)")
    jn.add_argument("paths", nargs="+", help="файлы .alog или папки с ними (имена любые — сессию и часть называют сведения)")
    jn.add_argument("-o", "--out", required=True, help="куда класть склейки")
    jn.add_argument("--key", help="файл закрытого ключа (по умолчанию — по keyId из каталога ключей)")
    pk = sub.add_parser("protect-key", help="перенести открытые файлы ключей в хранилище DPAPI")
    pk.add_argument("--delete-plain", action="store_true", help="после проверки затереть и удалить открытые файлы")
    eb = sub.add_parser("export-backup", help="резервная копия всех ключей под пароль (scrypt + AES-256-GCM)")
    eb.add_argument("path")
    ib = sub.add_parser("import-backup", help="восстановить ключи из резервной копии в хранилище DPAPI")
    ib.add_argument("path")
    g = sub.add_parser("gen-test-key", help="тестовая пара для asda.SelfTest.LogSeal → <папка>/test.key, test.pub.hex")
    g.add_argument("dir")
    a = ap.parse_args(argv)
    if a.cmd == "keygen":
        keygen(a.force)
        return 0
    if a.cmd == "header":
        remask()
        return 0
    if a.cmd == "check":
        return 1 if check(a.exe) else 0
    if a.cmd == "selftest":
        return 1 if selftest() else 0
    if a.cmd == "protect-key":
        return protect_key(a.delete_plain)
    if a.cmd == "export-backup":
        return export_backup(a.path)
    if a.cmd == "import-backup":
        return import_backup(a.path)
    if a.cmd == "verify-selftest":
        return 1 if verify_selftest(a.dir) else 0
    if a.cmd == "verify-rotation":
        return 1 if verify_rotation(a.dir) else 0
    if a.cmd == "join":
        _n, errs = join_paths(a.paths, a.out, a.key)
        return 1 if errs else 0
    if a.cmd == "gen-test-key":
        d, xy = gen_keypair()
        os.makedirs(a.dir, exist_ok=True)
        save_key(os.path.join(a.dir, "test.key"), d, xy)
        with open(os.path.join(a.dir, "test.pub.hex"), "w", encoding="ascii") as f:
            f.write(xy.hex())
        print("тестовая пара: %s (keyId %s)" % (os.path.join(a.dir, "test.key"), key_id(xy).hex()))
        return 0
    if a.cmd == "decrypt":
        paths = []
        if os.path.isdir(a.path):
            for r, _, fs in os.walk(a.path):
                paths += [os.path.join(r, f) for f in fs if f.lower().endswith((".alog", ".acrash"))]
        else:
            paths = [a.path]
        errs = sum(decrypt_path(p, a.out, a.key) for p in sorted(paths))
        return 1 if errs else 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
