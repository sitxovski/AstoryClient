# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_update_format.py — ★ ОБЩИЙ КОД ФОРМАТА ОБНОВЛЕНИЙ v1 (17.10.2026, система обновлений, волна 2).
#
# Спецификация — @@ASTORY_DEV_ROOT@@\Reports\updater\FORMAT_v1.md (её же реализует C++-лаунчер, волна 4).
# Здесь: версии, конверты подписи Ed25519, списки кусков .plst, нарезка (.ucas по пакетам IoStore, прочее — контентная),
# проверка путей, хранилище закрытых ключей (DPAPI + резервная копия под пароль — по образцу Tools/asda_logseal.py).
# Пользователи: Tools/asda_release.py (выпуск), Tools/asda_update_client.py (эталонный клиент и тесты).
# При импорте ничего не делает (закон tools-no-import-side-effects).

import base64
import hashlib
import json
import os
import re
import struct

import numpy as np

# ── константы формата ─────────────────────────────────────────────────────────────────────────────────────────
# Префикс подписываемого сообщения (разделение доменов: + вид + "\0" + payload)
SIG_PREFIX = b"ASDA-UPD-v1\0"
# Допустимые виды конвертов
ENVELOPE_KINDS = ("keyring", "channel", "version")
# Магия и версия списка кусков
PLST_MAGIC, PLST_VERSION = b"ASPL", 1
# Запись куска в .plst: sha256, размер, хранимый размер, кодек, 3 б пусто, № пачки, смещение
PLST_ENTRY = struct.Struct("<32sIIB3xIQ")
# Кодеки хранения куска
CODEC_RAW, CODEC_ZSTD = 0, 1
# Нарезка .ucas: целевой предел куска (по границам блоков внутри чанка IoStore)
IOSTORE_PIECE_MAX = 1 << 20
# Контентная нарезка: минимум, цель (маска gear-хэша), максимум куска; меньше минимума файл — одним куском
CDC_MIN, CDC_AVG_BITS, CDC_MAX = 64 << 10, 18, 1 << 20
# Окно gear-хэша, байт (h = Σ G[b(i-k)] << k по k < окна, mod 2^64)
CDC_WINDOW = 32
# Предел длины пути в манифесте
PATH_MAX = 260
# Предел размера куска (исходного и хранимого): куски режутся ≤ 1 МиБ, клиент отвергает больше (ревизия В2, M16)
PIECE_MAX = 4 << 20
# Срок «свежести» подписанных указателей: просрочка — только предупреждение (решение владельца 16.10: не блокировать)
EXPIRES_DAYS = 90
# Служебная папка клиента и суффикс временных файлов (запрещены в путях манифеста)
STATE_DIR_NAME, TMP_SUFFIX = ".asdaupd", ".asdaupd-tmp"
# Белый список символов сегмента пути (ревизия В2, M1; все 174 файла сборки ему удовлетворяют)
SEGMENT_RE = re.compile(r"^[A-Za-z0-9._+()\- ]+$")
# Имена устройств Windows (сравнение после обрезки хвостовых пробелов/точек и расширения)
DEVICE_NAMES = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"} | {"COM%d" % i for i in range(10)} | {"LPT%d" % i for i in range(10)}
# Строгий SHA-256 hex (ревизия В2, M2: хэши из манифеста идут в пути)
HEX64_RE = re.compile(r"^[0-9a-f]{64}$")
# Регулярка версии: канал-мажор.минор.патч-месяц.год (решение владельца 16.10: alpha-1.0.0-10.2026)
VERSION_RE = re.compile(r"^([a-z]+)-(\d+)\.(\d+)\.(\d+)-(\d{1,2})\.(\d{4})$")


# ── версии ────────────────────────────────────────────────────────────────────────────────────────────────────
def parse_version(v):
    """«alpha-1.0.0-10.2026» → (канал, (мажор, минор, патч), (год, месяц)); ValueError при неверном формате."""
    m = VERSION_RE.match(v or "")
    if not m:
        raise ValueError("версия «%s» не в формате канал-X.Y.Z-ММ.ГГГГ" % v)
    month = int(m.group(5))
    if not 1 <= month <= 12:
        raise ValueError("месяц версии %d вне 1..12" % month)
    return m.group(1), (int(m.group(2)), int(m.group(3)), int(m.group(4))), (int(m.group(6)), month)


def version_key(v):
    """Ключ сравнения версий внутри канала: семвер (месяц.год — справочный, порядок не задаёт)."""
    return parse_version(v)[1]


# ── хэши и JSON ───────────────────────────────────────────────────────────────────────────────────────────────
def sha256_hex(b):
    """SHA-256 байтов, hex."""
    return hashlib.sha256(b).hexdigest()


def sha256_file(path, bufsize=16 << 20):
    """SHA-256 файла, hex."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for c in iter(lambda: f.read(bufsize), b""):
            h.update(c)
    return h.hexdigest()


def check_hex64(h, what):
    """Строгий SHA-256 hex; ValueError иначе."""
    if not isinstance(h, str) or not HEX64_RE.match(h):
        raise ValueError("%s: не SHA-256 hex" % what)
    return h


def safe_zstd(raw, expected_size=None, limit=None):
    """Распаковка zstd с защитой от бомб (ревизия В2, V2): размер из заголовка кадра обязан быть известен и равен
    expected_size (или ≤ limit) ДО распаковки; max_output_size в python-zstandard не действует при заявленном размере."""
    import zstandard
    try:
        cs = zstandard.get_frame_parameters(raw).content_size
    except zstandard.ZstdError:
        raise ValueError("zstd: повреждённый заголовок кадра")
    if cs in (zstandard.CONTENTSIZE_UNKNOWN, zstandard.CONTENTSIZE_ERROR):
        raise ValueError("zstd: размер в кадре не указан")
    if (expected_size is not None and cs != expected_size) or (limit is not None and cs > limit):
        raise ValueError("zstd: заявлено %d байт, ожидалось %s" % (cs, expected_size if expected_size is not None else "≤ %d" % limit))
    try:
        out = zstandard.ZstdDecompressor().decompress(raw, max_output_size=cs)
    except zstandard.ZstdError as e:
        raise ValueError("zstd: %s" % e)
    if len(out) != cs:
        raise ValueError("zstd: распаковано %d вместо %d" % (len(out), cs))
    return out


def expires_iso(days=EXPIRES_DAYS):
    """Срок свежести указателя: сейчас + days (UTC ISO)."""
    import datetime
    t = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0) + datetime.timedelta(days=days)
    return t.isoformat()


def is_expired(iso):
    """Просрочен ли срок (нет поля — не просрочен; неразборчивое — просрочен)."""
    import datetime
    if not iso:
        return False
    try:
        return datetime.datetime.fromisoformat(iso) < datetime.datetime.now(datetime.timezone.utc)
    except (ValueError, TypeError):
        return True


def canon_json(obj):
    """Каноничный JSON (ключи по алфавиту, без пробелов, UTF-8) — для манифестов и полезной нагрузки конвертов."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


# ── ключи Ed25519 ─────────────────────────────────────────────────────────────────────────────────────────────
def key_id(pub):
    """id ключа: первые 8 байт SHA-256 открытого ключа, hex."""
    return hashlib.sha256(pub).hexdigest()[:16]


def ed_private(seed):
    """Закрытый ключ Ed25519 из 32-байтного зерна."""
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    return Ed25519PrivateKey.from_private_bytes(seed)


def ed_public_bytes(seed):
    """Открытый ключ (32 б) по зерну."""
    from cryptography.hazmat.primitives import serialization as s
    return ed_private(seed).public_key().public_bytes(s.Encoding.Raw, s.PublicFormat.Raw)


def ed_verify(pub, sig, msg):
    """Проверка подписи Ed25519: True/False."""
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    try:
        Ed25519PublicKey.from_public_bytes(pub).verify(sig, msg)
        return True
    except (InvalidSignature, ValueError):
        return False


# ── конверты ──────────────────────────────────────────────────────────────────────────────────────────────────
def envelope_sign(kind, payload_obj, seed):
    """Конверт {payload, sigs} с одной подписью ключом-зерном seed."""
    assert kind in ENVELOPE_KINDS
    payload = canon_json(payload_obj)
    sig = ed_private(seed).sign(SIG_PREFIX + kind.encode("ascii") + b"\0" + payload)
    return {"payload": base64.b64encode(payload).decode("ascii"),
            "sigs": [{"key": key_id(ed_public_bytes(seed)), "sig": base64.b64encode(sig).decode("ascii")}]}


def envelope_open(kind, env, trusted):
    """Проверка конверта: trusted = {id: pub}. Подпись проверяется ДО разбора JSON. → объект нагрузки; ValueError."""
    if kind not in ENVELOPE_KINDS or not isinstance(env, dict):
        raise ValueError("конверт: неверный вид или не объект")
    try:
        payload = base64.b64decode(env["payload"], validate=True)
        sigs = env["sigs"]
    except Exception:
        raise ValueError("конверт %s: повреждён" % kind)
    msg = SIG_PREFIX + kind.encode("ascii") + b"\0" + payload
    for s in sigs if isinstance(sigs, list) else []:
        if not isinstance(s, dict) or not isinstance(s.get("key"), str) or not isinstance(s.get("sig"), str):
            continue                                    # ревизия В2, M4: мусорные типы — не исключение, а «нет подписи»
        pub = trusted.get(s["key"])
        if pub is None:
            continue
        try:
            sig = base64.b64decode(s["sig"], validate=True)
        except Exception:
            continue
        if len(sig) == 64 and ed_verify(pub, sig, msg):
            try:
                obj = json.loads(payload.decode("utf-8"))
            except (UnicodeDecodeError, ValueError):
                raise ValueError("конверт %s: нагрузка не JSON" % kind)
            if not isinstance(obj, dict) or obj.get("format") != "asda-%s/1" % kind:   # ревизия В2, M5: версия точно 1
                raise ValueError("конверт %s: чужой формат нагрузки" % kind)
            return obj
    raise ValueError("конверт %s: нет действительной подписи доверенным ключом" % kind)


def keyring_trusted(keyring_obj, include_revoked=False):
    """Из нагрузки keyring — {id: pub} ключей выпуска (по умолчанию только неотозванных).
    include_revoked=True — ТОЛЬКО для чтения своего хранилища инструментом выпуска (ревизия В2, K1), не для клиента."""
    out = {}
    keys = keyring_obj.get("release_keys")
    for k in keys if isinstance(keys, list) else []:
        try:
            pub = bytes.fromhex(k["pub"])
        except (KeyError, TypeError, ValueError):
            continue
        if len(pub) == 32 and (include_revoked or not k.get("revoked")) and key_id(pub) == k.get("id"):
            out[k["id"]] = pub
    return out


# ── пути ──────────────────────────────────────────────────────────────────────────────────────────────────────
def check_rel_path(p):
    """Безопасный относительный путь манифеста (FORMAT_v1 §4); ValueError при нарушении."""
    if not isinstance(p, str) or not p or len(p) > PATH_MAX:
        raise ValueError("путь: пустой или длиннее %d" % PATH_MAX)
    if p.startswith("/") or "\\" in p or ":" in p or "\0" in p:
        raise ValueError("путь «%s»: абсолютный, с \\, : или NUL" % p)
    segs = p.split("/")
    for seg in segs:
        if seg in ("", ".", "..") or seg.endswith((" ", ".")):
            raise ValueError("путь «%s»: пустой/точечный сегмент" % p)
        if not SEGMENT_RE.match(seg):
            raise ValueError("путь «%s»: недопустимые символы (белый список A-Za-z0-9._+()- и пробел)" % p)
        if re.search(r"~\d", seg):
            raise ValueError("путь «%s»: похоже на короткое имя 8.3" % p)
        if seg.split(".")[0].rstrip(" .").upper() in DEVICE_NAMES:
            raise ValueError("путь «%s»: зарезервированное имя Windows" % p)
    if segs[0].lower() == STATE_DIR_NAME or p.lower().endswith(TMP_SUFFIX):
        raise ValueError("путь «%s»: служебная папка или временный файл клиента" % p)
    return p


# ── списки кусков .plst ───────────────────────────────────────────────────────────────────────────────────────
def plst_pack(pieces, packs):
    """pieces: [(sha256 bytes, size, stored, codec, pack_idx, offset)], packs: [sha256 bytes] → байты .plst."""
    out = [PLST_MAGIC, struct.pack("<III", PLST_VERSION, len(packs), len(pieces))]
    out += [p for p in packs]
    out += [PLST_ENTRY.pack(*e) for e in pieces]
    return b"".join(out)


def plst_unpack(data):
    """Байты .plst → (packs [hex], pieces [(hex, size, stored, codec, pack_hex, offset)]); ValueError при порче."""
    if len(data) < 16 or data[:4] != PLST_MAGIC:
        raise ValueError("plst: нет магии")
    ver, npk, n = struct.unpack_from("<III", data, 4)
    if ver != PLST_VERSION or len(data) != 16 + 32 * npk + PLST_ENTRY.size * n:
        raise ValueError("plst: версия или длина не сходятся")
    packs = [data[16 + 32 * i: 48 + 32 * i].hex() for i in range(npk)]
    base, pieces = 16 + 32 * npk, []
    for i in range(n):
        h, size, stored, codec, pi, off = PLST_ENTRY.unpack_from(data, base + PLST_ENTRY.size * i)
        if pi >= npk or codec not in (CODEC_RAW, CODEC_ZSTD) or not 0 < size <= PIECE_MAX \
                or not 0 < stored <= PIECE_MAX or (codec == CODEC_RAW and stored != size):
            raise ValueError("plst: кусок %d с неверными полями" % i)
        pieces.append((h.hex(), size, stored, codec, packs[pi], off))
    return packs, pieces


# ── нарезка ───────────────────────────────────────────────────────────────────────────────────────────────────
def iostore_cuts(utoc_path, ucas_size):
    """Точки реза .ucas по TOC (FORMAT_v1 §1): начало первого блока каждого чанка + границы блоков через ~1 МиБ.
    → отсортированный список смещений (включая 0, без конца файла). ValueError при неожиданном формате TOC.
    Ошибка точек реза влияет только на дедупликацию, не на корректность: куски всё равно покрывают файл целиком."""
    d = open(utoc_path, "rb").read()
    if d[:16] != b"-==--==--==--==-":
        raise ValueError("utoc: нет магии")
    u32 = lambda o: struct.unpack_from("<I", d, o)[0]
    hsz, n, nblk, esz = u32(20), u32(24), u32(28), u32(32)
    bs, nseeds, nnoph = u32(44), u32(84), u32(96)
    # ревизия В2, M13: как движок (IoStore.cpp ~1302): размер заголовка 144, версия ≥ PerfectHashWithOverflow (5) —
    # с неё поля seeds/без-хэша на этих смещениях; иначе нарезка по TOC неприменима
    if esz != 12 or bs == 0 or hsz != 144 or d[16] < 5:
        raise ValueError("utoc: неожиданный заголовок (версия %d, размер %d, запись блока %d)" % (d[16], hsz, esz))
    off_ol = hsz + 12 * n
    off_blk = off_ol + 10 * n + 4 * nseeds + 4 * nnoph
    blk_off = np.empty(nblk, dtype=np.uint64)
    blk_disk = np.empty(nblk, dtype=np.uint64)
    raw = np.frombuffer(d, dtype=np.uint8, count=12 * nblk, offset=off_blk).reshape(nblk, 12).astype(np.uint64)
    blk_off[:] = raw[:, 0] | raw[:, 1] << 8 | raw[:, 2] << 16 | raw[:, 3] << 24 | raw[:, 4] << 32
    csz = raw[:, 5] | raw[:, 6] << 8 | raw[:, 7] << 16
    blk_disk[:] = (csz + 15) & ~np.uint64(15)          # движок всегда выравнивает блок до 16 (IoStoreWriter EncryptAndSign)
    cuts = {0}
    for i in range(n):
        e = d[off_ol + 10 * i: off_ol + 10 * i + 10]
        o, l = int.from_bytes(e[0:5], "big"), int.from_bytes(e[5:10], "big")
        if l == 0:
            continue
        a, b = o // bs, (o + l - 1) // bs
        if b >= nblk:
            raise ValueError("utoc: чанк %d ссылается за пределы таблицы блоков" % i)
        acc = 0
        for k in range(a, b + 1):
            if k == a or acc + int(blk_disk[k]) > IOSTORE_PIECE_MAX:
                cuts.add(int(blk_off[k]))
                acc = 0
            acc += int(blk_disk[k])
    return sorted(c for c in cuts if c < ucas_size)


# Таблица gear-хэша: фиксированная (меняется — меняются все точки реза; при смене поднять версию формата)
_GEAR = np.frombuffer(hashlib.shake_256(b"ASDA-UPD-v1/gear").digest(256 * 8), dtype="<u8").copy()


def _cdc_candidates(data, slice_bytes=16 << 20):
    """Позиции-кандидаты реза (после байта i, где старшие биты gear-хэша окна = 0). Срезами — память O(среза).
    Хэш в позиции i зависит только от байтов i-31..i, поэтому срез берётся с перекрытием окна и результат тот же."""
    n = len(data)
    b_all = np.frombuffer(data, dtype=np.uint8)
    mask = np.uint64(((1 << CDC_AVG_BITS) - 1) << (64 - CDC_AVG_BITS))   # старшие биты — лучше перемешаны
    out = []
    for s in range(0, n, slice_bytes):
        lo = max(0, s - (CDC_WINDOW - 1))
        g = _GEAR[b_all[lo:min(n, s + slice_bytes)]]
        h = np.zeros(len(g), dtype=np.uint64)
        for k in range(CDC_WINDOW):                     # h[i] = Σ g[i-k] << k  (mod 2^64)
            h[k:] += g[:len(g) - k] << np.uint64(k)
        idx = np.flatnonzero((h & mask) == 0) + lo
        out.append(idx[idx >= s] + 1)                   # рез ПОСЛЕ байта-кандидата
    return np.concatenate(out) if out else np.empty(0, dtype=np.int64)


def cdc_cuts(data):
    """Контентная нарезка байтов data → список смещений начала кусков (с 0). Детерминирована (только от содержимого)."""
    n = len(data)
    if n <= CDC_MIN:              # ≤ 64 КиБ — один кусок (FORMAT_v1 §1)
        return [0]
    cand = _cdc_candidates(data)
    cuts, last, ci = [0], 0, 0
    while True:
        lo, hi = last + CDC_MIN, last + CDC_MAX
        if hi >= n:
            # хвост: если остаток > MAX — режем дальше по кандидатам/максимуму, иначе конец
            if n - last <= CDC_MAX:
                break
        ci = int(np.searchsorted(cand, lo, side="left"))
        nxt = int(cand[ci]) if ci < len(cand) and cand[ci] <= hi and cand[ci] < n else min(hi, n)
        if nxt >= n:
            break
        cuts.append(nxt)
        last = nxt
    return cuts


# ── хранилище закрытых ключей ─────────────────────────────────────────────────────────────────────────────────
# Энтропия DPAPI (привязка блоба к назначению) + id ключа
DPAPI_ENTROPY = b"AsdaReborn/Updater/v1/"
# Формат резервной копии под пароль
BACKUP_FORMAT = "AsdaUpdaterKeyBackup/v1"


def dpapi(data, entropy, protect):
    """CryptProtectData/CryptUnprotectData текущей учётной записи — реализация Tools/asda_logseal.py (не дублируем)."""
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_logseal
    return asda_logseal._dpapi(data, entropy, protect)


def store_save(priv_dir, role, seed):
    """Зерно ключа → <priv_dir>/<role>-<id>.dpapi (DPAPI, ACL только владельцу). → путь."""
    kid = key_id(ed_public_bytes(seed))
    os.makedirs(priv_dir, exist_ok=True)
    # ACL папки при КАЖДОМ сохранении (ревизия В2, M9: папка могла появиться без ACL — P4 sync, вручную): без
    # наследования, полный доступ только текущему пользователю; файлы наследуют (OI)(CI). Функция asda_logseal
    # рассчитана на ПАПКУ — к файлу её применять нельзя (файл остаётся без доступа).
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_logseal
    asda_logseal._lock_acl(priv_dir)
    path = os.path.join(priv_dir, "%s-%s.dpapi" % (role, kid))
    blob = dpapi(seed, DPAPI_ENTROPY + kid.encode("ascii"), True)
    with open(path, "xb") as f:
        f.write(blob)
    return path


def store_load(priv_dir, role, kid):
    """Зерно ключа роли role с id kid из DPAPI-хранилища; None, если нет."""
    path = os.path.join(priv_dir, "%s-%s.dpapi" % (role, kid))
    if not os.path.isfile(path):
        return None
    seed = dpapi(open(path, "rb").read(), DPAPI_ENTROPY + kid.encode("ascii"), False)
    if key_id(ed_public_bytes(seed)) != kid:
        raise ValueError("DPAPI-блоб %s не соответствует id" % path)
    return seed


def backup_encrypt(records, password):
    """records [(role, seed)] → JSON-текст резервной копии (scrypt N=2^19 r=8 + AES-256-GCM, как у asda_logseal)."""
    import secrets
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives.kdf.scrypt import Scrypt
    salt, nonce = secrets.token_bytes(32), secrets.token_bytes(12)
    hdr = {"format": BACKUP_FORMAT, "kdf": "scrypt", "n": 2 ** 19, "r": 8, "p": 1, "salt": salt.hex(),
           "nonce": nonce.hex(), "cipher": "AES-256-GCM",
           "keys": [{"role": r, "id": key_id(ed_public_bytes(s))} for r, s in records]}
    key = Scrypt(salt=salt, length=32, n=hdr["n"], r=hdr["r"], p=hdr["p"]).derive(password.encode("utf-8"))
    aad = json.dumps(hdr, sort_keys=True).encode("utf-8")
    plain = b"".join(s for _, s in records)
    hdr["ct"] = base64.b64encode(AESGCM(key).encrypt(nonce, plain, aad)).decode("ascii")
    return json.dumps(hdr, indent=1)


def backup_decrypt(text, password):
    """JSON резервной копии → [(role, seed)]; ValueError при неверном пароле/порче."""
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives.kdf.scrypt import Scrypt
    hdr = json.loads(text)
    ct = base64.b64decode(hdr.pop("ct"))
    if hdr.get("format") != BACKUP_FORMAT:
        raise ValueError("не резервная копия ключей обновлений")
    if (hdr.get("n"), hdr.get("r"), hdr.get("p")) != (2 ** 19, 8, 1):     # ревизия В2, M10: подложенные параметры KDF
        raise ValueError("копия: неожиданные параметры scrypt")
    key = Scrypt(salt=bytes.fromhex(hdr["salt"]), length=32, n=hdr["n"], r=hdr["r"], p=hdr["p"]).derive(
        password.encode("utf-8"))
    try:
        plain = AESGCM(key).decrypt(bytes.fromhex(hdr["nonce"]), ct, json.dumps(hdr, sort_keys=True).encode("utf-8"))
    except Exception:
        raise ValueError("неверный пароль или копия повреждена")
    out = []
    for i, k in enumerate(hdr["keys"]):
        seed = plain[32 * i: 32 * i + 32]
        if key_id(ed_public_bytes(seed)) != k["id"]:
            raise ValueError("копия: ключ %s не сходится" % k["id"])
        out.append((k["role"], seed))
    return out
