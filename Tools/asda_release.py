# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_release.py — ★ ВЫПУСК ОБНОВЛЕНИЙ КЛИЕНТА ОДНОЙ КОМАНДОЙ (17.10.2026, система обновлений, волна 2).
#
# Формат — @@ASTORY_DEV_ROOT@@\Reports\updater\FORMAT_v1.md; общий код — Tools/asda_update_format.py.
# Хранилище по умолчанию — Releases/ (решение владельца 16.10: все выпуски локально, в Perforce;
# на сервере — только текущая и прошлая версия, см. publish-set).
#
# КОМАНДЫ:
#   python Tools/asda_release.py keys init                   — корневой ключ + ключ выпуска (Ed25519) → DPAPI учётки,
#                                                              открытые части и keyring.json (подпись корнем)
#   python Tools/asda_release.py keys export-backup [--out F] [--offline-root]
#                                                            — ★ ВЛАДЕЛЕЦ: копия закрытых ключей под пароль (по умолчанию
#                                                              patches/keys/backup/…keybackup); --offline-root — после
#                                                              проверки копии удалить корневой ключ из DPAPI (рекомендуется:
#                                                              корень нужен только для ротации — тогда import-backup)
#   python Tools/asda_release.py keys import-backup F        — восстановить ключи на другой учётке/ПК
#   python Tools/asda_release.py keys rotate-release [--revoke-old] — новый ключ выпуска (keyring seq+1); канал и
#                                                              version.json тут же переподписываются новым ключом
#   python Tools/asda_release.py keys status                 — какие ключи есть и доступны
#   python Tools/asda_release.py build --version alpha-1.0.0-10.2026 [--game-root …] [--min-version …]
#                                                            — новая версия из сборки (по умолчанию BUILD_EAC/latest.json)
#   python Tools/asda_release.py verify [--deep]             — подписи, хэши, полнота хранилища (--deep: каждый кусок;
#                                                              после build запускается автоматически, --no-verify — нет)
#   python Tools/asda_release.py resign                      — продлить срок свежести канала/version (та же версия и seq)
#   python Tools/asda_release.py publish-set [--keep 2]      — список файлов для сервера В ПОРЯДКЕ ВЫКЛАДКИ (FORMAT §7):
#                                                              пачки → объекты → манифесты → root.pub → keyring → channel
#                                                              → version
#   python Tools/asda_release.py status                      — текущая версия канала и история
#   python Tools/asda_release.py upload [--dry-run] [--no-prune] [--keep 2]
#                                                            — ★ волна 3: выкладка publish-set на @@ASTORY_UPDATE_HOST@@
#                                                              по SFTP (пользователь asdaupd, chroot, ключ
#                                                              %USERPROFILE%\.ssh\asdaupd_upload): недостающие данные
#                                                              (имя.tmp → rename) → root.pub → keyring → channel →
#                                                              version (каждый атомарно) → сверка по HTTPS → удаление
#                                                              файлов, не нужных текущей и прошлой версии ни одного канала.
#                                                              До загрузки: seq канала на сервере не выше локального,
#                                                              свободное место, блокировки локальная и на сервере
#   общие: --store DIR (по умолчанию patches/), --channel alpha
#
# БЕЗОПАСНОСТЬ: закрытые ключи — только DPAPI текущей учётки (keys/private/*.dpapi) и копия под пароль
#   (keys/backup/); на сервер не уходят никогда (publish-set их не включает). Скрипт ключей не печатает.
# При импорте ничего не делает (закон tools-no-import-side-effects).

import argparse
import datetime
import getpass
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asda_update_format as F  # noqa: E402

# Корень проекта UE
PROJECT_DIR = os.path.dirname(HERE)
# Хранилище выпусков по умолчанию (в Perforce — дело владельца)
DEFAULT_STORE = os.path.join(os.path.dirname(PROJECT_DIR), "Releases")   # ★ 17.10: Releases (бывший AsdaReborn\patches)
# Указатель последней сборки eac_package.py
LATEST_BUILD = os.path.join(os.path.dirname(PROJECT_DIR), "Build", "EAC", "latest.json")
# Канал по умолчанию (решение владельца 16.10: один канал alpha)
DEFAULT_CHANNEL = "alpha"
# Канал игры (только у него есть version.json — версия для проверки в игре) и канал самого лаунчера (волна 4)
GAME_CHANNEL, LAUNCHER_CHANNEL = "alpha", "launcher"
# Папка выпуска лаунчера по умолчанию (<проект>/Launcher/dist, собирает make_dist.bat)
LAUNCHER_DIST = os.path.join(PROJECT_DIR, "Launcher", "dist")
# Предел размера одной пачки
PACK_MAX = 256 << 20
# Уровень zstd для кусков не-.ucas
ZSTD_LEVEL = 12
# Предел файла для контентной нарезки (читается в память целиком); .ucas режется по TOC потоково
CDC_FILE_MAX = 2 << 30
# Срок свежести ключевого кольца при ротации, дней
KEYRING_EXPIRES_DAYS = 365
# Сервер обновлений (волна 3): SSH-хост выкладки, SFTP-пользователь (chroot), корень сайта внутри chroot, адрес.
#   Значения — из _setup\configure (заглушки @@ASTORY_UPDATE_SSH_*@@); пока не заменены, выкладка отказывает с подсказкой.
UPLOAD_HOST = "@@ASTORY_UPDATE_SSH_HOST@@"
UPLOAD_USER = "@@ASTORY_UPDATE_SSH_USER@@"
UPLOAD_ROOT = "@@ASTORY_UPDATE_SSH_ROOT@@"
UPLOAD_URL = "https://@@ASTORY_UPDATE_HOST@@"
# Ключ выкладки (только SFTP в свою папку; подлинность обновлений даёт подпись Ed25519, не этот ключ)
UPLOAD_KEY = os.path.join(os.path.expanduser("~"), ".ssh", "asdaupd_upload")
# Каталоги данных на сервере, которыми управляет выкладка (прочее не трогается)
DATA_DIRS = ("packs", "objects", "manifests")
# Закреплённый ключ хоста сервера выкладки: файл НЕ хранится в git (.gitignore), создаётся один раз через
#   ssh-keyscan с проверкой отпечатка — см. DEPLOY\README.md
KNOWN_HOSTS = os.path.join(HERE, "data", "updater_known_hosts")
# Запас свободного места на общем диске сервера сверх объёма выкладки
UPLOAD_FREE_RESERVE = 3 << 30
# Имя канала: только строчные латинские буквы (подставляется в пути сервера)
CHANNEL_RE = re.compile(r"^[a-z]+$")


def log(msg):
    """Строка журнала с временем."""
    print("[%s] %s" % (datetime.datetime.now().strftime("%H:%M:%S"), msg), flush=True)


class ReleaseError(Exception):
    """Отказ выпуска (код 1)."""


# ── пути хранилища ────────────────────────────────────────────────────────────────────────────────────────────
class Store:
    """Раскладка хранилища (FORMAT_v1 §2)."""

    def __init__(self, root):
        self.root = os.path.abspath(root)

    def p(self, *parts):
        """Абсолютный путь внутри хранилища."""
        return os.path.join(self.root, *parts)

    priv = property(lambda s: s.p("keys", "private"))
    root_pub_path = property(lambda s: s.p("keys", "root.pub"))
    keyring_path = property(lambda s: s.p("keys", "keyring.json"))

    def channel_path(self, ch):
        """Путь конверта канала."""
        return self.p("channels", "%s.json" % ch)

    def manifest_rel(self, sha):
        """Относительный путь манифеста."""
        return "manifests/%s.json.zst" % sha

    def object_rel(self, sha):
        """Относительный путь списка кусков."""
        return "objects/%s/%s.plst" % (sha[:2], sha)

    def pack_rel(self, sha):
        """Относительный путь пачки."""
        return "packs/%s.blob" % sha


def write_atomic(path, data):
    """Запись файла через временный + os.replace (читатель видит либо старый, либо новый)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def write_once(path, data):
    """Адресуемый хэшем объект: если есть — содержимое обязано совпасть; иначе атомарная запись."""
    if os.path.isfile(path):
        if open(path, "rb").read() != data:
            raise ReleaseError("объект %s уже есть и отличается — хранилище повреждено" % path)
        return False
    write_atomic(path, data)
    return True


# ── ключи ─────────────────────────────────────────────────────────────────────────────────────────────────────
def load_root_pub(st):
    """Открытый корневой ключ хранилища (32 б)."""
    if not os.path.isfile(st.root_pub_path):
        raise ReleaseError("нет %s — сначала keys init" % st.root_pub_path)
    pub = open(st.root_pub_path, "rb").read()
    if len(pub) != 32:
        raise ReleaseError("root.pub не 32 байта")
    return pub


def load_keyring(st):
    """Нагрузка keyring.json, проверенная корневым ключом."""
    root = load_root_pub(st)
    env = json.load(open(st.keyring_path, encoding="utf-8"))
    return F.envelope_open("keyring", env, {F.key_id(root): root})


def keys_init(st):
    """Корневой ключ + ключ выпуска, keyring seq 1."""
    import secrets
    if os.path.exists(st.root_pub_path) or os.path.exists(st.keyring_path):
        raise ReleaseError("ключи уже созданы (%s) — повторная инициализация запрещена" % st.p("keys"))
    root_seed, rel_seed = secrets.token_bytes(32), secrets.token_bytes(32)
    rp = F.store_save(st.priv, "root", root_seed)
    lp = F.store_save(st.priv, "release", rel_seed)
    write_atomic(st.root_pub_path, F.ed_public_bytes(root_seed))
    rel_pub = F.ed_public_bytes(rel_seed)
    ring = {"format": "asda-keyring/1", "seq": 1, "created": now_iso(), "expires": F.expires_iso(KEYRING_EXPIRES_DAYS),
            "release_keys": [{"id": F.key_id(rel_pub), "pub": rel_pub.hex(), "revoked": False}]}
    write_atomic(st.keyring_path, json.dumps(F.envelope_sign("keyring", ring, root_seed), indent=1).encode("utf-8"))
    log("корневой ключ %s → %s" % (F.key_id(F.ed_public_bytes(root_seed)), rp))
    log("ключ выпуска %s → %s" % (F.key_id(rel_pub), lp))
    log("открытый корневой ключ (вшить в лаунчер): %s" % F.ed_public_bytes(root_seed).hex())
    log("★ СЛЕДУЮЩИЙ ШАГ (владелец): python Tools/asda_release.py keys export-backup — копия под пароль")


def keys_status(st):
    """Список ключей и их доступность через DPAPI."""
    root = load_root_pub(st)
    rid = F.key_id(root)
    ring = load_keyring(st)
    log("корневой ключ %s: закрытый %s" % (rid, "доступен" if safe_load(st, "root", rid) else "НЕДОСТУПЕН"))
    log("keyring seq %d" % ring["seq"])
    for k in ring["release_keys"]:
        log("  ключ выпуска %s %s: закрытый %s" % (k["id"], "ОТОЗВАН" if k.get("revoked") else "действует",
                                                  "доступен" if safe_load(st, "release", k["id"]) else "НЕДОСТУПЕН"))
    bdir = st.p("keys", "backup")
    backups = sorted(os.listdir(bdir)) if os.path.isdir(bdir) else []
    log("резервных копий: %d %s" % (len(backups), "" if backups else "— ★ сделать: keys export-backup"))


def safe_load(st, role, kid):
    """Зерно ключа или None (DPAPI другой учётки и т.п.)."""
    try:
        return F.store_load(st.priv, role, kid)
    except Exception:
        return None


def all_private(st):
    """[(роль, зерно)] всех ключей хранилища, доступных этой учётке."""
    out = []
    if os.path.isdir(st.priv):
        for fn in sorted(os.listdir(st.priv)):
            parts = fn[:-6].split("-", 1) if fn.endswith(".dpapi") else []
            if len(parts) == 2 and parts[0] in ("root", "release"):
                seed = safe_load(st, parts[0], parts[1])           # ревизия В2, M9: посторонний блоб не роняет
                if seed is not None:
                    out.append((parts[0], seed))
    return out


def keys_export_backup(st, out, password=None, offline_root=False):
    """Копия всех закрытых ключей под пароль владельца; копия сразу проверяется расшифровкой.
    offline_root — затем удалить корневой ключ из DPAPI (ревизия В2, V9: корень «офлайн», нужен только для ротации)."""
    recs = all_private(st)
    if not recs:
        raise ReleaseError("закрытых ключей нет")
    if password is None:
        password = getpass.getpass("Пароль резервной копии (не короче 14 символов): ")
        if len(password) < 14 or getpass.getpass("Повторите: ") != password:
            raise ReleaseError("пароль короткий или не совпал")
    out = out or st.p("keys", "backup", "updater-keys-%s.keybackup" % datetime.date.today().isoformat())
    os.makedirs(os.path.dirname(out), exist_ok=True)
    text = F.backup_encrypt(recs, password)
    back = F.backup_decrypt(text, password)
    if sorted(back) != sorted(recs):
        raise ReleaseError("проверка копии не прошла — файл не записан")
    with open(out, "x", encoding="utf-8") as f:
        f.write(text)
    log("резервная копия %d ключей → %s (проверена; без пароля бесполезна; пароль хранить отдельно)" % (len(recs), out))
    if offline_root:
        for fn in os.listdir(st.priv):
            if fn.startswith("root-") and fn.endswith(".dpapi"):
                os.remove(os.path.join(st.priv, fn))
                log("корневой ключ удалён из DPAPI (%s) — для ротации: keys import-backup %s" % (fn, out))


def keys_import_backup(st, path, password=None):
    """Восстановление ключей из копии в DPAPI текущей учётки."""
    password = password if password is not None else getpass.getpass("Пароль резервной копии: ")
    for role, seed in F.backup_decrypt(open(path, encoding="utf-8").read(), password):
        kid = F.key_id(F.ed_public_bytes(seed))
        if safe_load(st, role, kid):
            log("уже есть: %s %s" % (role, kid))
        elif os.path.exists(os.path.join(st.priv, "%s-%s.dpapi" % (role, kid))):
            raise ReleaseError("%s-%s.dpapi есть, но не читается этой учёткой — удалите его вручную и повторите" % (role, kid))
        else:
            log("восстановлен %s %s → %s" % (role, kid, F.store_save(st.priv, role, seed)))


def keys_rotate_release(st, revoke_old):
    """Новый ключ выпуска, keyring seq+1 (подпись корнем)."""
    import secrets
    root = load_root_pub(st)
    root_seed = safe_load(st, "root", F.key_id(root))
    if root_seed is None:
        raise ReleaseError("закрытый корневой ключ недоступен (import-backup?)")
    ring = load_keyring(st)
    seed = secrets.token_bytes(32)
    F.store_save(st.priv, "release", seed)
    pub = F.ed_public_bytes(seed)
    for k in ring["release_keys"]:
        if revoke_old:
            k["revoked"] = True
    ring["release_keys"].append({"id": F.key_id(pub), "pub": pub.hex(), "revoked": False})
    ring["seq"] += 1
    ring["created"] = now_iso()
    ring["expires"] = F.expires_iso(KEYRING_EXPIRES_DAYS)
    # ревизия В2, K1: текущие канал и version прочитать ДО смены keyring (их подпись — прежним ключом) и тут же
    # переподписать новым — иначе после --revoke-old ни выпуск, ни клиенты не примут канал
    resign = {}
    for ch in os.listdir(st.p("channels")) if os.path.isdir(st.p("channels")) else []:
        if ch.endswith(".json"):
            resign[ch[:-5]] = open_channel(st, ch[:-5])
    ver = open_version(st) if os.path.isfile(st.p("version.json")) else None
    write_atomic(st.keyring_path, json.dumps(F.envelope_sign("keyring", ring, root_seed), indent=1).encode("utf-8"))
    for ch, chan in resign.items():
        if chan:
            chan["expires"] = F.expires_iso()
            write_atomic(st.channel_path(ch), json.dumps(F.envelope_sign("channel", chan, seed), indent=1).encode("utf-8"))
    if ver:
        ver["expires"] = F.expires_iso()
        write_atomic(st.p("version.json"), json.dumps(F.envelope_sign("version", ver, seed), indent=1).encode("utf-8"))
    log("новый ключ выпуска %s, keyring seq %d%s; переподписано каналов %d%s"
        % (F.key_id(pub), ring["seq"], ", прежние ОТОЗВАНЫ" if revoke_old else "", len([c for c in resign.values() if c]),
           ", version.json" if ver else ""))
    log("★ выложить на сервер: keyring.json, channels/*.json, version.json (publish-set)")


def release_signer(st):
    """(зерно, keyring) — последний неотозванный ключ выпуска, закрытая часть которого доступна."""
    ring = load_keyring(st)
    for k in reversed(ring["release_keys"]):
        if not k.get("revoked"):
            seed = safe_load(st, "release", k["id"])
            if seed is not None:
                return seed, ring
    raise ReleaseError("нет доступного неотозванного ключа выпуска (keys status / import-backup)")


def now_iso():
    """Время UTC ISO без долей секунды."""
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat()


# ── каналы и манифесты ────────────────────────────────────────────────────────────────────────────────────────
def open_channel(st, ch, strict=False):
    """Нагрузка канала (проверенная) или None, если канала ещё нет. Своё хранилище читается ВСЕМИ ключами keyring,
    включая отозванные (ревизия В2, K1); strict=True — как клиент (только неотозванные) — для verify."""
    path = st.channel_path(ch)
    if not os.path.isfile(path):
        return None
    ring = load_keyring(st)
    return F.envelope_open("channel", json.load(open(path, encoding="utf-8")), F.keyring_trusted(ring, include_revoked=not strict))


def open_version(st, strict=False):
    """Нагрузка version.json (проверенная)."""
    ring = load_keyring(st)
    return F.envelope_open("version", json.load(open(st.p("version.json"), encoding="utf-8")),
                           F.keyring_trusted(ring, include_revoked=not strict))


def read_manifest(st, sha):
    """Манифест по хэшу (хэш файла проверяется)."""
    data = open(st.p(*st.manifest_rel(sha).split("/")), "rb").read()
    if F.sha256_hex(data) != sha:
        raise ReleaseError("манифест %s: хэш не сходится" % sha)
    return json.loads(F.safe_zstd(data, limit=512 << 20).decode("utf-8"))


def read_plst(st, sha):
    """Список кусков по хэшу (хэш проверяется)."""
    data = open(st.p(*st.object_rel(sha).split("/")), "rb").read()
    if F.sha256_hex(data) != sha:
        raise ReleaseError("plst %s: хэш не сходится" % sha)
    return F.plst_unpack(data)


# ── запись пачек ──────────────────────────────────────────────────────────────────────────────────────────────
class PackWriter:
    """Новые куски → временные пачки до PACK_MAX; при закрытии пачка переименовывается в <sha256>.blob."""

    def __init__(self, st):
        self.st = st
        self.tmpdir = st.p("packs", ".building")
        self.lock = acquire_lock(st)                      # ревизия В2 M11 / В3 В4: build и upload не одновременно
        shutil.rmtree(self.tmpdir, ignore_errors=True)    # остатки упавшего выпуска
        os.makedirs(self.tmpdir)
        self.packs = []        # [временный путь]; после close — sha
        self.cur = None        # открытый файл текущей пачки
        self.cur_size = 0
        self.added = 0         # байт записано

    def add(self, stored):
        """Записать хранимые байты куска → (№ временной пачки, смещение)."""
        if self.cur is None or self.cur_size + len(stored) > PACK_MAX:
            self._finish_current()
            path = os.path.join(self.tmpdir, "pack%04d.tmp" % len(self.packs))
            self.cur = open(path, "wb")
            self.packs.append(path)
            self.cur_size = 0
        off = self.cur_size
        self.cur.write(stored)
        self.cur_size += len(stored)
        self.added += len(stored)
        return len(self.packs) - 1, off

    def _finish_current(self):
        """Закрыть текущий временный файл пачки."""
        if self.cur is not None:
            self.cur.flush()
            os.fsync(self.cur.fileno())
            self.cur.close()
            self.cur = None

    def close(self):
        """Финализировать: хэш каждой пачки, переименование. → [sha] по номерам временных пачек."""
        self._finish_current()
        out = []
        for path in self.packs:
            sha = F.sha256_file(path)
            dst = self.st.p(*self.st.pack_rel(sha).split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.exists(dst):
                if F.sha256_file(dst) != sha:                 # ревизия В2, M11: испорченная прежняя пачка
                    raise ReleaseError("пачка %s уже есть и испорчена — хранилище повреждено" % dst)
                os.remove(path)
            else:
                os.replace(path, dst)
            out.append(sha)
        self.release()
        return out

    def release(self):
        """Снять блокировку и убрать временную папку (и при отказе выпуска)."""
        if self.cur is not None:
            self.cur.close()
            self.cur = None
        shutil.rmtree(self.tmpdir, ignore_errors=True)
        if self.lock is not None:
            release_lock(self.st, self.lock)
            self.lock = None


# ── индекс уже хранимых кусков ────────────────────────────────────────────────────────────────────────────────
def build_piece_index(st):
    """{sha куска: (pack_sha, offset, stored, codec, size)} по всем .plst хранилища (только те, чьи пачки на месте)."""
    idx = {}
    odir = st.p("objects")
    if not os.path.isdir(odir):
        return idx
    present = set(fn[:-5] for fn in os.listdir(st.p("packs"))) if os.path.isdir(st.p("packs")) else set()
    for sub in os.listdir(odir):
        if not os.path.isdir(os.path.join(odir, sub)):
            continue
        for fn in os.listdir(os.path.join(odir, sub)):
            if not fn.endswith(".plst") or not F.HEX64_RE.match(fn[:-5]):
                continue
            _, pieces = read_plst(st, fn[:-5])
            for h, size, stored, codec, pack, off in pieces:
                if pack in present:
                    idx.setdefault(h, (pack, off, stored, codec, size))
    return idx


# ── выпуск ────────────────────────────────────────────────────────────────────────────────────────────────────
def resolve_game_root(arg, ch=GAME_CHANNEL):
    """Корень выпуска: аргумент или BUILD_EAC/latest.json (игра) / AstoryClient/Launcher/dist (лаунчер)."""
    if ch == LAUNCHER_CHANNEL:
        root = os.path.abspath(arg or LAUNCHER_DIST)
        if not os.path.isfile(os.path.join(root, "AsdaLauncher.exe")):
            raise ReleaseError("%s — нет AsdaLauncher.exe (сначала AstoryClient/Launcher/make_dist.bat)" % root)
        return root
    if arg:
        root = arg
    else:
        if not os.path.isfile(LATEST_BUILD):
            raise ReleaseError("нет %s — укажи --game-root" % LATEST_BUILD)
        root = json.load(open(LATEST_BUILD, encoding="utf-8"))["game_root"]
    root = os.path.abspath(root)
    if not os.path.isfile(os.path.join(root, "AsdaReborn", "Binaries", "Win64", "AsdaReborn-Win64-Shipping.exe")):
        raise ReleaseError("%s — не корень игры (нет AsdaReborn-Win64-Shipping.exe)" % root)
    return root


def list_game_files(root):
    """Отсортированные относительные пути (/) всех файлов корня игры, с проверкой безопасности и регистра."""
    out, seen = [], {}
    for r, dirs, files in os.walk(root):
        dirs.sort()
        for fn in sorted(files):
            rel = os.path.relpath(os.path.join(r, fn), root).replace("\\", "/")
            F.check_rel_path(rel)
            low = rel.lower()
            if low in seen:
                raise ReleaseError("пути различаются только регистром: %s / %s" % (seen[low], rel))
            seen[low] = rel
            out.append(rel)
    return sorted(out)


def cut_file(root, rel):
    """(вид нарезки, [смещения начала кусков]) для файла."""
    path = os.path.join(root, *rel.split("/"))
    size = os.path.getsize(path)
    if rel.lower().endswith(".ucas"):
        if not os.path.isfile(path[:-5] + ".utoc"):
            raise ReleaseError("%s без .utoc — сборка неполная" % rel)  # ревизия В2, M12
        return "iostore", F.iostore_cuts(path[:-5] + ".utoc", size)
    if size <= F.CDC_MIN:
        return "whole", [0]
    if size > CDC_FILE_MAX:
        raise ReleaseError("%s: %d байт — больше предела контентной нарезки в памяти" % (rel, size))
    with open(path, "rb") as f:
        return "cdc", F.cdc_cuts(f.read())


def build_release(st, ch, version, game_root, min_version):
    """Новая версия канала из сборки. → нагрузка channel."""
    vch, _, _ = F.parse_version(version)
    if vch != ch:
        raise ReleaseError("версия %s не из канала %s" % (version, ch))
    min_version = min_version or version
    if F.parse_version(min_version)[0] != ch or F.version_key(min_version) > F.version_key(version):
        raise ReleaseError("min-version %s: другой канал или выше выпускаемой" % min_version)
    seed, ring = release_signer(st)
    prev = open_channel(st, ch)
    if prev:
        if F.version_key(version) <= F.version_key(prev["version"]):
            raise ReleaseError("версия %s не выше текущей %s — версии только растут" % (version, prev["version"]))
        if F.version_key(min_version) < F.version_key(prev["min_version"]):
            raise ReleaseError("min-version %s ниже прежней %s" % (min_version, prev["min_version"]))
    game_root = resolve_game_root(game_root, ch)
    log("выпуск %s (канал %s) из %s" % (version, ch, game_root))
    files = list_game_files(game_root)
    idx = build_piece_index(st)
    log("в хранилище уже %d кусков; файлов в сборке %d" % (len(idx), len(files)))
    pw = PackWriter(st)
    try:
        return _build_body(st, ch, version, min_version, seed, prev, game_root, files, idx, pw)
    finally:
        pw.release()


def next_seq(st, ch, prev):
    """seq нового выпуска = максимум по каналу, version.json и журналу + 1; расхождение — отказ (ревизия В2, V7:
    откат channels/*.json из P4 не должен выдать повторный seq)."""
    cands = {"канал": prev["seq"] if prev else 0}
    if os.path.isfile(st.p("version.json")):
        v = open_version(st)
        if v.get("channel") == ch:
            cands["version.json"] = v["seq"]
    if os.path.isfile(st.p("releases.log")):
        for line in open(st.p("releases.log"), encoding="utf-8").read().splitlines():
            f = line.split("\t")
            if len(f) > 2 and F.VERSION_RE.match(f[1]) and F.parse_version(f[1])[0] == ch:
                cands["журнал"] = max(cands.get("журнал", 0), int(f[2].split("=", 1)[1]))
    if len(set(cands.values())) > 1:
        raise ReleaseError("seq расходится между источниками %s — хранилище откатано? разберитесь до выпуска" % cands)
    return max(cands.values()) + 1


def _build_body(st, ch, version, min_version, seed, prev, game_root, files, idx, pw):
    """Тело выпуска (под блокировкой пачек)."""
    import zstandard
    seq = next_seq(st, ch, prev)
    cz = zstandard.ZstdCompressor(level=ZSTD_LEVEL)
    pending = []           # [(rel, size, sha, cut, entries)] — entries ссылаются на пачки ('old', sha) или ('new', №)
    new_bytes = reused = 0
    for rel in files:
        path = os.path.join(game_root, *rel.split("/"))
        size = os.path.getsize(path)
        cut, cuts = cut_file(game_root, rel)
        bounds = cuts + [size]
        entries = []
        fsha = hashlib.sha256()
        with open(path, "rb") as f:
            for i in range(len(cuts)):
                a, b = bounds[i], bounds[i + 1]
                if b <= a:
                    continue
                f.seek(a)
                data = f.read(b - a)
                if len(data) != b - a:
                    raise ReleaseError("%s: файл изменился во время выпуска" % rel)
                fsha.update(data)
                h = hashlib.sha256(data).digest()
                known = idx.get(h.hex())
                if known:
                    pack, off, stored, codec, psz = known
                    entries.append((h, b - a, stored, codec, pack, off))
                    reused += b - a
                    continue
                if cut == "iostore":
                    stored_b, codec = data, F.CODEC_RAW
                else:
                    z = cz.compress(data)
                    stored_b, codec = (z, F.CODEC_ZSTD) if len(z) < len(data) else (data, F.CODEC_RAW)
                pi, off = pw.add(stored_b)
                entries.append((h, b - a, len(stored_b), codec, ("new", pi), off))
                idx[h.hex()] = (("new", pi), off, len(stored_b), codec, b - a)
                new_bytes += b - a
        pending.append((rel, size, fsha.hexdigest(), cut, entries))
    new_packs = pw.close()
    log("новых данных %.1f МБ (хранимых %.1f МБ в %d пачк.), переиспользовано %.1f МБ"
        % (new_bytes / 1e6, pw.added / 1e6, len(new_packs), reused / 1e6))
    mfiles = []
    for rel, size, fsha, cut, entries in pending:
        packs, pieces = [], []
        for h, sz, stored, codec, pack, off in entries:
            psha = new_packs[pack[1]] if isinstance(pack, tuple) else pack
            if psha not in packs:
                packs.append(psha)
            pieces.append((h, sz, stored, codec, packs.index(psha), off))
        blob = F.plst_pack(pieces, [bytes.fromhex(x) for x in packs])
        osha = F.sha256_hex(blob)
        write_once(st.p(*st.object_rel(osha).split("/")), blob)
        mfiles.append({"path": rel, "size": size, "sha256": fsha, "cut": cut, "pieces": osha, "count": len(pieces)})
    manifest = {"format": "asda-manifest/1", "channel": ch, "version": version, "seq": seq, "created": now_iso(),
                "total_size": sum(x["size"] for x in mfiles), "files": mfiles}
    mblob = zstandard.ZstdCompressor(level=19).compress(F.canon_json(manifest))
    msha = F.sha256_hex(mblob)
    write_once(st.p(*st.manifest_rel(msha).split("/")), mblob)
    exp = F.expires_iso()
    chan = {"format": "asda-channel/1", "channel": ch, "version": version, "seq": seq, "created": manifest["created"],
            "expires": exp, "min_version": min_version, "manifest": {"sha256": msha, "size": len(mblob)},
            "previous": ({"version": prev["version"], "seq": prev["seq"], "manifest": prev["manifest"]} if prev else None)}
    verobj = {"format": "asda-version/1", "channel": ch, "version": version, "seq": seq, "min_version": min_version,
              "expires": exp}
    # журнал — ДО канала (ревизия В2, M15): при сбое между ними next_seq увидит расхождение, а не потеряет выпуск
    with open(st.p("releases.log"), "a", encoding="utf-8") as f:
        f.write("%s\t%s\tseq=%d\tmanifest=%s\tfiles=%d\tsize=%d\tnew=%d\tbuild=%s\n"
                % (manifest["created"], version, seq, msha, len(mfiles), manifest["total_size"], new_bytes, game_root))
        f.flush()
        os.fsync(f.fileno())
    if ch == GAME_CHANNEL:                               # version.json — только версия игры (волна 5)
        write_atomic(st.p("version.json"), json.dumps(F.envelope_sign("version", verobj, seed), indent=1).encode("utf-8"))
    write_atomic(st.channel_path(ch), json.dumps(F.envelope_sign("channel", chan, seed), indent=1).encode("utf-8"))
    log("ГОТОВО: %s seq %d, манифест %s, файлов %d, %.1f МБ" % (version, seq, msha[:16], len(mfiles),
                                                             manifest["total_size"] / 1e6))
    return chan


# ── проверка и набор для сервера ──────────────────────────────────────────────────────────────────────────────
def versions_to_keep(st, ch, keep):
    """[манифест-sha] текущей и предыдущих версий (keep штук) по цепочке previous."""
    chan = open_channel(st, ch)
    if chan is None:
        raise ReleaseError("канала %s нет" % ch)
    out = [chan["manifest"]["sha256"]]
    prev = chan.get("previous")
    if prev and keep > 1:
        out.append(prev["manifest"]["sha256"])
    if keep > 2:
        # дальше по журналу выпусков (previous хранит только одну ступень)
        lines = [l.split("\t") for l in open(st.p("releases.log"), encoding="utf-8").read().splitlines() if l]
        for l in reversed(lines):
            if len(l) < 4 or not F.VERSION_RE.match(l[1]) or F.parse_version(l[1])[0] != ch:
                continue                                 # ревизия В2, M15: только свой канал
            m = l[3].split("=", 1)[1]
            if m not in out and len(out) < keep:
                out.append(m)
    return chan, out


def verify_store(st, ch, deep=False, keep=2):
    """Проверка: keyring, канал, манифесты, .plst, наличие и (deep) содержимое кусков. → число ошибок."""
    errs = 0
    chan, mans = versions_to_keep(st, ch, keep)
    log("канал %s: %s seq %d, проверяю %d версий" % (ch, chan["version"], chan["seq"], len(mans)))
    open_channel(st, ch, strict=True)                    # как клиент: подпись неотозванным ключом
    if F.is_expired(chan.get("expires")) or F.is_expired(load_keyring(st).get("expires")):
        log("! срок свежести канала/keyring истёк — resign (канал) или rotate-release (keyring)")
    if ch == GAME_CHANNEL:
        vt = open_version(st, strict=True)
        if vt["version"] != chan["version"] or vt["seq"] != chan["seq"]:
            log("ОШИБКА version.json (%s) не совпадает с каналом" % vt["version"])
            errs += 1
    checked = set()
    for msha in mans:
        m = read_manifest(st, msha)
        for fe in m["files"]:
            F.check_rel_path(fe["path"])
            if fe["pieces"] in checked:
                continue
            checked.add(fe["pieces"])
            packs, pieces = read_plst(st, fe["pieces"])
            if sum(p[1] for p in pieces) != fe["size"] or len(pieces) != fe["count"]:
                log("ОШИБКА %s: куски не покрывают файл" % fe["path"])
                errs += 1
            for pk in packs:
                if not os.path.isfile(st.p(*st.pack_rel(pk).split("/"))):
                    log("ОШИБКА %s: нет пачки %s" % (fe["path"], pk))
                    errs += 1
            if deep:
                fh = hashlib.sha256()
                handles = {}
                for h, size, stored, codec, pk, off in pieces:
                    f = handles.get(pk) or handles.setdefault(pk, open(st.p(*st.pack_rel(pk).split("/")), "rb"))
                    f.seek(off)
                    raw = f.read(stored)
                    try:
                        data = F.safe_zstd(raw, expected_size=size) if codec == F.CODEC_ZSTD else raw
                    except ValueError:
                        data = b""
                    if len(data) != size or F.sha256_hex(data) != h:
                        log("ОШИБКА %s: кусок %s испорчен" % (fe["path"], h[:16]))
                        errs += 1
                        break
                    fh.update(data)
                else:
                    if fh.hexdigest() != fe["sha256"]:
                        log("ОШИБКА %s: собранный файл не совпал с хэшем манифеста" % fe["path"])
                        errs += 1
                for f in handles.values():
                    f.close()
    log("ИТОГ проверки: %s" % ("ОК" if errs == 0 else "ОШИБОК %d" % errs))
    return errs


def publish_set(st, ch, keep=2):
    """Относительные пути файлов для сервера (без закрытых ключей и журналов) В ПОРЯДКЕ ВЫКЛАДКИ (FORMAT_v1 §8):
    пачки → объекты → манифесты → root.pub → keyring → channel → version (ревизия В3, М5: version.json — после канала,
    иначе при обрыве игра потребует версию, которую лаунчер ещё не видит)."""
    chan, mans = versions_to_keep(st, ch, keep)
    packs_l, objs, mfs = [], [], []
    for msha in mans:
        mfs.append(st.manifest_rel(msha))
        m = read_manifest(st, msha)
        for fe in m["files"]:
            objs.append(st.object_rel(fe["pieces"]))
            packs, _ = read_plst(st, fe["pieces"])
            packs_l += [st.pack_rel(p) for p in packs]
    out = packs_l + objs + mfs + ["keys/root.pub", "keys/keyring.json", "channels/%s.json" % ch] + \
        (["version.json"] if ch == GAME_CHANNEL else [])
    seen, res = set(), []
    for x in out:
        if x not in seen:
            seen.add(x)
            res.append(x)
    return res


def keep_set_all_channels(st, keep=2):
    """Данные, нужные ВСЕМ каналам хранилища (ревизия В3, М7: prune одного канала не удаляет данные другого)."""
    out = set()
    for fn in os.listdir(st.p("channels")) if os.path.isdir(st.p("channels")) else []:
        if fn.endswith(".json") and CHANNEL_RE.match(fn[:-5]):
            out.update(x for x in publish_set(st, fn[:-5], keep) if x.split("/")[0] in DATA_DIRS)
    return out


def resign(st, ch):
    """Продлить срок свежести канала и version.json той же версии и seq (клиент принимает: тот же манифест)."""
    seed, _ = release_signer(st)
    chan = open_channel(st, ch)
    if chan is None:
        raise ReleaseError("канала %s нет" % ch)
    chan["expires"] = F.expires_iso()
    write_atomic(st.channel_path(ch), json.dumps(F.envelope_sign("channel", chan, seed), indent=1).encode("utf-8"))
    ver = open_version(st) if os.path.isfile(st.p("version.json")) else {}
    if ver.get("channel") == ch:
        ver["expires"] = chan["expires"]
        write_atomic(st.p("version.json"), json.dumps(F.envelope_sign("version", ver, seed), indent=1).encode("utf-8"))
    log("канал %s (%s seq %d) переподписан до %s — выложить channel и version.json" % (ch, chan["version"], chan["seq"],
                                                                                     chan["expires"]))


# ── выкладка на сервер (волна 3) ──────────────────────────────────────────────────────────────────────────────
class Sftp:
    """Пакетный SFTP (OpenSSH): каждый вызов — один сеанс с файлом команд; «-» перед командой = ошибку не считать.
    Ключ хоста закреплён (Tools/data/updater_known_hosts — локальный, не в git; StrictHostKeyChecking=yes — ревизия В3, М8)."""

    def __init__(self, host, user, key, root):
        self.exe = shutil.which("sftp")
        if not self.exe:
            raise ReleaseError("не найден sftp (OpenSSH)")
        for name, val in (("хост", host), ("пользователь", user), ("корень", root)):
            if not val or val.startswith("@@"):
                raise ReleaseError("сервер выкладки не настроен (%s = %r) — запустите _setup\\configure.bat "
                                   "(ASTORY_UPDATE_SSH_HOST/USER/ROOT), см. DEPLOY\\README.md" % (name, val))
        if not os.path.isfile(key):
            raise ReleaseError("нет ключа выкладки %s" % key)
        if not os.path.isfile(KNOWN_HOSTS):
            raise ReleaseError("нет закреплённого ключа хоста %s — создайте его: ssh-keyscan -t ed25519 <хост> > файл, "
                               "сверьте отпечаток (ssh-keygen -lf файл) с отпечатком сервера; см. DEPLOY\\README.md"
                               % KNOWN_HOSTS)
        self.host, self.user, self.key, self.root = host, user, key, root
        self.tmpdir = tempfile.mkdtemp(prefix="asdaupd_sftp_")
        self.cfg = os.path.join(self.tmpdir, "empty_ssh_config")    # свой пустой конфиг: ~/.ssh/config не влияет
        open(self.cfg, "w").close()

    def run(self, cmds, check=True):
        """Выполнить команды. → вывод (stdout+stderr)."""
        batch = os.path.join(self.tmpdir, "batch.txt")
        with open(batch, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(cmds) + "\n")
        r = subprocess.run([self.exe, "-F", self.cfg, "-i", self.key, "-o", "BatchMode=yes", "-o", "ConnectTimeout=20",
                            "-o", "StrictHostKeyChecking=yes", "-o", "UserKnownHostsFile=%s" % KNOWN_HOSTS,
                            "-o", "ServerAliveInterval=30", "-b", batch, "%s@%s" % (self.user, self.host)],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        if check and r.returncode != 0:
            raise ReleaseError("sftp: код %d: %s" % (r.returncode, (r.stderr or r.stdout).strip()[-400:]))
        return r.stdout + r.stderr

    def listing(self):
        """({отн. путь: размер} обычных файлов, [отн. пути НЕ обычных файлов — симлинки и т.п.]) в управляемых каталогах."""
        R_ = self.root
        out = self.run(["-ls -ln %s/packs" % R_, "-ls -ln %s/manifests" % R_, "-ls -ln %s/objects/*/*" % R_,
                        "-ls -ln %s/channels" % R_, "-ls -ln %s/keys" % R_, "-ls -ln %s/version.json*" % R_], check=False)
        files, odd = {}, []
        for line in out.splitlines():
            f = line.split()
            if len(f) >= 9 and f[-1].startswith(R_ + "/") and f[0][:1] in "-lpsbcd":
                rel = f[-1][len(R_) + 1:]
                if f[0].startswith("-"):
                    files[rel] = int(f[4])
                elif not f[0].startswith("d"):
                    odd.append(rel)
        return files, odd

    def free_bytes(self):
        """Свободно на томе корня выкладки (sftp df → statvfs), байт; None, если сервер не ответил."""
        out = self.run(["df /"], check=False)          # корень chroot — тот же том, что и корень выкладки
        for line in out.splitlines():
            f = line.split()
            if len(f) >= 5 and f[0].isdigit() and f[2].isdigit():
                return int(f[2]) * 1024                  # Size Used Avail (КиБ)
        return None

    def close(self):
        """Убрать временные файлы."""
        shutil.rmtree(self.tmpdir, ignore_errors=True)


def https_get(url, limit=1 << 20):
    """GET по HTTPS напрямую (без системного прокси — ревизия В3, М6), с ошибкой ReleaseError."""
    import urllib.error
    import urllib.request
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(urllib.request.Request(url, headers={"User-Agent": "AsdaRelease/1", "Cache-Control": "no-cache"}),
                         timeout=30) as r:
            return r.read(limit + 1)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise ReleaseError("HTTPS %s: код %d" % (url, e.code))
    except (urllib.error.URLError, OSError) as e:
        raise ReleaseError("HTTPS %s: %s" % (url, e))


def remote_channel(st, ch, url):
    """Канал, который сейчас отдаёт сервер (проверенный всеми ключами keyring хранилища) или None."""
    body = https_get("%s/channels/%s.json" % (url, ch))
    if body is None:
        return None
    ring = load_keyring(st)
    try:
        return F.envelope_open("channel", json.loads(body), F.keyring_trusted(ring, include_revoked=True))
    except ValueError as e:
        raise ReleaseError("канал на сервере не проверяется ключами хранилища (%s) — чужой сервер или чужое хранилище" % e)


def upload(st, ch, keep=2, dry_run=False, prune=True, host=UPLOAD_HOST, user=UPLOAD_USER, key=UPLOAD_KEY,
           root=UPLOAD_ROOT, url=UPLOAD_URL):
    """Выложить publish-set (FORMAT_v1 §8). → число ошибок сверки."""
    if not CHANNEL_RE.match(ch):
        raise ReleaseError("имя канала «%s» — только a-z" % ch)          # ревизия В3, М9
    if re.search(r"[\[\]*?\"]", st.root):
        raise ReleaseError("путь хранилища содержит символы шаблонов sftp ([]*?\") — переименуйте")
    if verify_store(st, ch, deep=False, keep=keep):
        raise ReleaseError("хранилище не прошло verify — выкладка запрещена")
    lock = acquire_lock(st)                                                # ревизия В3, В4: не одновременно с build/upload
    sf = Sftp(host, user, key, root)
    remote_lock = None
    try:
        local = open_channel(st, ch)
        rem = remote_channel(st, ch, url)
        if rem and (rem["seq"] > local["seq"] or (rem["seq"] == local["seq"] and rem["manifest"] != local["manifest"])):
            raise ReleaseError("на сервере канал %s seq %d (%s), локально seq %d (%s) — хранилище устарело или разошлось; "
                               "выкладка откатила бы игроков" % (ch, rem["seq"], rem["version"], local["seq"], local["version"]))
        ps = publish_set(st, ch, keep)
        # снимок указателей на момент начала (ревизия В3, В4): выкладывается ровно то, что проверено выше
        snap = os.path.join(sf.tmpdir, "meta")
        meta = [x for x in ps if x.split("/")[0] not in DATA_DIRS]
        for x in meta:
            os.makedirs(os.path.dirname(os.path.join(snap, *x.split("/"))), exist_ok=True)
            shutil.copyfile(st.p(*x.split("/")), os.path.join(snap, *x.split("/")))
        data = [x for x in ps if x.split("/")[0] in DATA_DIRS]
        keep_all = keep_set_all_channels(st, keep)
        remote, odd = sf.listing()
        local_size = {x: os.path.getsize(st.p(*x.split("/"))) for x in data}
        todo = [x for x in data if remote.get(x) != local_size[x]]
        gone = [x for x in remote if (x.split("/")[0] in DATA_DIRS and x not in keep_all) or x.endswith(".tmp")]
        need = sum(local_size[x] for x in todo)
        log("выкладка %s: канал %s seq %d (на сервере %s); файлов в наборе %d, на сервере %d; загрузить данных %d (%.1f МБ), "
            "указателей %d; удалить %d" % (url, ch, local["seq"], ("seq %d" % rem["seq"]) if rem else "пусто", len(ps),
                                          len(remote), len(todo), need / 1e6, len(meta), len(gone) if prune else 0))
        if odd:
            raise ReleaseError("на сервере не обычные файлы (симлинки?) %s — разберитесь вручную" % odd[:5])
        free = sf.free_bytes()
        if free is None or free < need + UPLOAD_FREE_RESERVE:              # ревизия В3, В2: общий диск с соседями
            raise ReleaseError("на сервере свободно %s, нужно %.1f ГБ + запас %.0f ГБ" % (
                "?" if free is None else "%.1f ГБ" % (free / 1e9), need / 1e9, UPLOAD_FREE_RESERVE / 1e9))
        if dry_run:
            for x in todo[:20]:
                log("  + " + x)
            for x in gone[:20]:
                log("  - " + x)
            return 0
        # блокировка на сервере: mkdir атомарен; скрытое имя nginx не отдаёт
        sf.run(["-mkdir %s" % root])
        out = sf.run(["mkdir %s/.upload-lock" % root], check=False)
        if "failure" in out.lower() or "exists" in out.lower() or "denied" in out.lower():
            raise ReleaseError("на сервере идёт другая выкладка (%s/.upload-lock); если она упала — удалите каталог" % root)
        remote_lock = "%s/.upload-lock" % root
        dirs = sorted({"/".join(x.split("/")[:i]) for x in ps for i in range(1, len(x.split("/")))})
        sf.run(["-mkdir %s/%s" % (root, d) for d in dirs])

        def put_cmds(rel, src):
            """put во временное имя → права → атомарная замена."""
            rp = "%s/%s" % (root, rel)
            return ['put "%s" %s.tmp' % (src.replace("\\", "/"), rp), "chmod 644 %s.tmp" % rp, "rename %s.tmp %s" % (rp, rp)]
        big = [x for x in todo if local_size[x] > (64 << 20)]
        small = [x for x in todo if x not in big]
        done = 0
        for x in big:                                    # большие пачки — по одной на сеанс (виден ход)
            sf.run(put_cmds(x, st.p(*x.split("/"))))
            done += local_size[x]
            log("  загружено %s (%.0f МБ, всего %.0f из %.0f МБ)" % (x, local_size[x] / 1e6, done / 1e6, need / 1e6))
        for i in range(0, len(small), 200):
            cmds = []
            for x in small[i:i + 200]:
                cmds += put_cmds(x, st.p(*x.split("/")))
            sf.run(cmds)
            log("  загружено мелких файлов %d/%d" % (min(i + 200, len(small)), len(small)))
        remote, odd = sf.listing()                       # сверка данных до переключения указателей
        bad = [x for x in data if remote.get(x) != local_size[x]]
        if bad or odd:
            raise ReleaseError("после загрузки на сервере не сходятся %d файлов данных (%s) — указатели НЕ переключены"
                               % (len(bad) + len(odd), (bad + odd)[0]))
        for x in meta:                                   # root.pub → keyring → channel → version, каждый атомарно
            try:
                sf.run(put_cmds(x, os.path.join(snap, *x.split("/"))))
            except ReleaseError as e:
                raise ReleaseError("выкладка прервана на указателе %s (%s) — запустите upload повторно" % (x, e))
        log("  указатели выложены: %s" % ", ".join(meta))
        errs = 0
        for x in meta:                                   # сверка по HTTPS: сервер отдаёт ровно наш снимок
            if https_get("%s/%s" % (url, x)) != open(os.path.join(snap, *x.split("/")), "rb").read():
                log("ОШИБКА: %s по HTTPS отличается от выложенного" % x)
                errs += 1
        if prune and not errs:
            remote, _ = sf.listing()
            gone = [x for x in remote if (x.split("/")[0] in DATA_DIRS and x not in keep_all) or x.endswith(".tmp")]
            for i in range(0, len(gone), 300):
                sf.run(["-rm %s/%s" % (root, x) for x in gone[i:i + 300]])
            if gone:
                log("  удалено с сервера ненужных файлов: %d" % len(gone))
        log("ВЫКЛАДКА %s: %s" % ("ГОТОВА" if not errs else "С ОШИБКАМИ %d" % errs, url))
        return errs
    finally:
        if remote_lock:
            sf.run(["-rmdir %s" % remote_lock], check=False)
        sf.close()
        release_lock(st, lock)


def acquire_lock(st):
    """Общая блокировка хранилища для build и upload (packs/.building.lock). → дескриптор."""
    os.makedirs(st.p("packs"), exist_ok=True)
    try:
        return os.open(st.p("packs", ".building.lock"), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        raise ReleaseError("хранилище занято другим выпуском/выкладкой (packs/.building.lock); если процесс упал — удалите файл")


def release_lock(st, fd):
    """Снять общую блокировку."""
    os.close(fd)
    try:
        os.remove(st.p("packs", ".building.lock"))
    except FileNotFoundError:
        pass


def status(st, ch):
    """Текущая версия и журнал."""
    chan = open_channel(st, ch)
    log("канал %s: %s" % (ch, "%s seq %d (min %s)" % (chan["version"], chan["seq"], chan["min_version"]) if chan else "пусто"))
    if os.path.isfile(st.p("releases.log")):
        for l in open(st.p("releases.log"), encoding="utf-8").read().splitlines()[-10:]:
            log("  " + l)


def main(argv=None):
    """Точка входа."""
    ap = argparse.ArgumentParser(description="Выпуск обновлений клиента AsdaReborn (формат v1)")
    ap.add_argument("--store", default=DEFAULT_STORE, help="хранилище (по умолчанию Releases)")
    ap.add_argument("--channel", default=DEFAULT_CHANNEL)
    sub = ap.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keys")
    ks = k.add_subparsers(dest="kcmd", required=True)
    ks.add_parser("init")
    ks.add_parser("status")
    eb = ks.add_parser("export-backup")
    eb.add_argument("--out")
    eb.add_argument("--offline-root", action="store_true", help="после проверки копии удалить корневой ключ из DPAPI")
    ib = ks.add_parser("import-backup")
    ib.add_argument("file")
    rr = ks.add_parser("rotate-release")
    rr.add_argument("--revoke-old", action="store_true")
    b = sub.add_parser("build")
    b.add_argument("--version", required=True)
    b.add_argument("--game-root")
    b.add_argument("--min-version")
    b.add_argument("--no-verify", action="store_true", help="не запускать verify --deep после выпуска")
    v = sub.add_parser("verify")
    v.add_argument("--deep", action="store_true")
    v.add_argument("--keep", type=int, default=2)
    ps = sub.add_parser("publish-set")
    ps.add_argument("--keep", type=int, default=2)
    sub.add_parser("status")
    sub.add_parser("resign")
    up = sub.add_parser("upload")
    up.add_argument("--dry-run", action="store_true")
    up.add_argument("--no-prune", action="store_true", help="не удалять с сервера ненужные файлы")
    up.add_argument("--keep", type=int, default=2)
    a = ap.parse_args(argv)
    st = Store(a.store)
    try:
        if a.cmd == "keys":
            {"init": lambda: keys_init(st), "status": lambda: keys_status(st),
             "export-backup": lambda: keys_export_backup(st, a.out, offline_root=a.offline_root),
             "import-backup": lambda: keys_import_backup(st, a.file),
             "rotate-release": lambda: keys_rotate_release(st, a.revoke_old)}[a.kcmd]()
        elif a.cmd == "build":
            build_release(st, a.channel, a.version, a.game_root, a.min_version)
            if not a.no_verify and verify_store(st, a.channel, deep=True, keep=2):
                return 1                                 # ревизия В2, M11: выпуск проверяется сразу
        elif a.cmd == "verify":
            return 1 if verify_store(st, a.channel, a.deep, a.keep) else 0
        elif a.cmd == "publish-set":
            for x in publish_set(st, a.channel, a.keep):
                print(x)
        elif a.cmd == "status":
            status(st, a.channel)
        elif a.cmd == "resign":
            resign(st, a.channel)
        elif a.cmd == "upload":
            return 1 if upload(st, a.channel, a.keep, a.dry_run, not a.no_prune) else 0
        return 0
    except (ReleaseError, ValueError) as e:
        log("ОТКАЗ: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
