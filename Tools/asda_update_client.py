# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_update_client.py — ★ ЭТАЛОННЫЙ КЛИЕНТ ОБНОВЛЕНИЙ v1 (17.10.2026, система обновлений, волна 2; ревизия review_wave2.md).
#
# Делает ровно то, что будет делать C++-лаунчер (волна 4), по FORMAT_v1.md §6–§8: проверка подписей (корневой ключ и
# минимальные seq ВШИВАЮТСЯ в лаунчер, с сервера не берутся), защита от отката, сборка файлов из кусков (свои старые +
# скачанные диапазонами), проверка каждого куска и файла SHA-256, атомарная замена, удаление исчезнувших файлов,
# признак незавершённого обновления, «Проверить файлы».
# Нужен: (1) как исполняемая спецификация для C++, (2) для сквозных тестов выпуска (asda_update_selftest.py).
#
# ЗАПУСК:
#   python Tools/asda_update_client.py update --source <папка хранилища | https://…> --dir <папка игры> --root-pub <hex|файл>
#   python Tools/asda_update_client.py verify --source … --dir … --root-pub …   — пересчитать все файлы и восстановить
#   общие: --channel alpha, --min-keyring-seq N, --min-seq N (то, что вшито в лаунчер)
# При импорте ничего не делает.

import argparse
import json
import os
import stat
import sys
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asda_update_format as F  # noqa: E402

# Папка состояния клиента внутри папки игры
STATE_DIR = F.STATE_DIR_NAME
# Склейка диапазонов: допустимый зазор между кусками одной пачки и предел длины диапазона
RANGE_GAP, RANGE_MAX = 64 << 10, 16 << 20
# Пределы размеров служебных файлов (защита от подсунутых гигантов): конверт, манифест (сжатый/распакованный), .plst
MAX_ENVELOPE, MAX_MANIFEST, MAX_MANIFEST_RAW, MAX_PLST = 1 << 20, 64 << 20, 512 << 20, 64 << 20
# Признак точки повторного разбора (junction/symlink) в атрибутах файла Windows
FILE_ATTRIBUTE_REPARSE_POINT = 0x400


class UpdateError(Exception):
    """Отказ обновления (подпись, откат, порча, путь)."""


# ── источники ─────────────────────────────────────────────────────────────────────────────────────────────────
class LocalSource:
    """Хранилище — локальная папка (тесты)."""

    def __init__(self, root):
        self.root = root
        self.downloaded = 0

    def get(self, rel, limit):
        """Файл целиком (не больше limit байт)."""
        path = os.path.join(self.root, *rel.split("/"))
        if os.path.getsize(path) > limit:
            raise UpdateError("%s больше предела %d" % (rel, limit))
        data = open(path, "rb").read()
        self.downloaded += len(data)
        return data

    def get_range(self, rel, start, length):
        """Диапазон байт файла."""
        with open(os.path.join(self.root, *rel.split("/")), "rb") as f:
            f.seek(start)
            data = f.read(length)
        if len(data) != length:
            raise UpdateError("%s: короткий диапазон" % rel)
        self.downloaded += length
        return data


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    """Переадресации запрещены (ревизия В2, M3: https → http)."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise UpdateError("сервер переадресует (%d) — запрещено" % code)


class HttpSource:
    """Хранилище по HTTPS (Range-запросы). http:// — только для 127.0.0.1/localhost (тесты)."""

    def __init__(self, base):
        u = urllib.parse.urlsplit(base)
        if not (u.scheme == "https" or (u.scheme == "http" and u.hostname in ("127.0.0.1", "localhost"))):
            raise UpdateError("источник должен быть https://")
        self.base = base.rstrip("/") + "/"
        self.downloaded = 0
        self.opener = urllib.request.build_opener(_NoRedirect)

    def _open(self, rel, headers):
        """Запрос с таймаутом."""
        req = urllib.request.Request(self.base + rel, headers=dict(headers, **{"User-Agent": "AsdaUpdater/1"}))
        try:
            return self.opener.open(req, timeout=30)
        except urllib.error.HTTPError as e:
            raise UpdateError("%s: HTTP %d" % (rel, e.code))
        except urllib.error.URLError as e:
            raise UpdateError("%s: сеть: %s" % (rel, e.reason))

    def get(self, rel, limit):
        """Файл целиком (не больше limit)."""
        with self._open(rel, {}) as r:
            data = r.read(limit + 1)
        if len(data) > limit:
            raise UpdateError("%s больше предела %d" % (rel, limit))
        self.downloaded += len(data)
        return data

    def get_range(self, rel, start, length):
        """Диапазон байт: ответ обязан быть 206 с Content-Range ровно на запрошенное (ревизия В2, M3)."""
        end = start + length - 1
        with self._open(rel, {"Range": "bytes=%d-%d" % (start, end)}) as r:
            cr = r.headers.get("Content-Range", "")
            if r.status != 206 or not cr.startswith("bytes %d-%d/" % (start, end)):
                raise UpdateError("%s: сервер не выдал диапазон (код %d, Content-Range «%s»)" % (rel, r.status, cr))
            data = r.read(length + 1)
        if len(data) != length:
            raise UpdateError("%s: диапазон неверной длины" % rel)
        self.downloaded += length
        return data


def make_source(s):
    """Источник по строке: URL или папка."""
    return HttpSource(s) if "://" in s else LocalSource(s)


# ── безопасная работа с путями внутри папки игры ──────────────────────────────────────────────────────────────
def _is_reparse(st):
    """Точка повторного разбора (junction/symlink) по результату lstat."""
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, "st_file_attributes", 0) & FILE_ATTRIBUTE_REPARSE_POINT)


def safe_path(game_dir, rel):
    """Абсолютный путь файла манифеста внутри папки игры. Отвергает точки повторного разбора (junction/symlink) на
    любом существующем компоненте и выход за корень (ревизия В2, V10). C++: FILE_FLAG_OPEN_REPARSE_POINT + сверка
    GetFinalPathNameByHandle с корнем."""
    F.check_rel_path(rel)
    root = os.path.realpath(game_dir)
    cur = root
    for seg in rel.split("/"):
        cur = os.path.join(cur, seg)
        try:
            st = os.lstat(cur)
        except FileNotFoundError:
            break
        if _is_reparse(st):
            raise UpdateError("путь %s проходит через junction/symlink — отказ" % rel)
    full = os.path.join(root, *rel.split("/"))
    if os.path.commonpath([root, os.path.realpath(full)]) != root:
        raise UpdateError("путь %s выходит за папку игры" % rel)
    return full


# ── состояние клиента ─────────────────────────────────────────────────────────────────────────────────────────
class State:
    """<папка игры>/.asdaupd: state.json (+ .bak), манифест (сжатый, как с сервера) и .plst текущей версии, кэш хэшей.
    C++: дополнительно дублировать seq_max/keyring_seq в HKCU (переживает удаление папки)."""

    def __init__(self, game_dir, log):
        self.dir = os.path.join(game_dir, STATE_DIR)
        os.makedirs(os.path.join(self.dir, "objects"), exist_ok=True)
        self.state_path = os.path.join(self.dir, "state.json")
        self.cache_path = os.path.join(self.dir, "hashcache.json")
        self.s = self._load(self.state_path, log, "состояние")
        self.cache = self._load(self.cache_path, None, "кэш")

    @staticmethod
    def _load(path, log, what):
        """JSON из файла или его .bak; оба испорчены → пусто (ревизия В2, V4: не вечный отказ)."""
        for p in (path, path + ".bak"):
            try:
                obj = json.load(open(p, encoding="utf-8"))
                if isinstance(obj, dict):
                    return obj
            except (OSError, ValueError):
                continue
        if log and (os.path.exists(path) or os.path.exists(path + ".bak")):
            log("! %s клиента повреждено — начинаю с чистого (минимальные seq из лаунчера остаются)" % what)
        return {}

    def save(self):
        """Запись state.json и кэша: временный → fsync → replace; прежний — в .bak."""
        for path, obj in ((self.state_path, self.s), (self.cache_path, self.cache)):
            tmp = path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(obj, f)
                f.flush()
                os.fsync(f.fileno())
            if os.path.isfile(path):
                os.replace(path, path + ".bak")
            os.replace(tmp, path)

    def manifest(self):
        """Манифест установленной версии (хэш файла = state["manifest"], ревизия В2, M7) или None."""
        p = os.path.join(self.dir, "manifest.json.zst")
        sha = self.s.get("manifest")
        if not sha or not os.path.isfile(p):
            return None
        blob = open(p, "rb").read()
        if F.sha256_hex(blob) != sha:
            return None
        return json.loads(F.safe_zstd(blob, limit=MAX_MANIFEST_RAW).decode("utf-8"))

    def plst(self, sha):
        """Сохранённый .plst (проверка хэша) или None."""
        p = os.path.join(self.dir, "objects", sha + ".plst")
        if os.path.isfile(p):
            data = open(p, "rb").read()
            if F.sha256_hex(data) == sha:
                return data
        return None


def is_ready(game_dir):
    """Можно ли запускать игру: версия установлена и нет незавершённого обновления (ревизия В2, V8)."""
    st = State(game_dir, None)
    return bool(st.s.get("version")) and not st.s.get("pending")


def file_sha_cached(state, game_dir, rel, use_cache):
    """SHA-256 локального файла (кэш по размеру+mtime_ns) или None, если файла нет / это не файл."""
    path = safe_path(game_dir, rel)
    try:
        stt = os.stat(path)
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(stt.st_mode):
        return None
    c = state.cache.get(rel)
    if use_cache and c and c[0] == stt.st_size and c[1] == stt.st_mtime_ns:
        return c[2]
    sha = F.sha256_file(path)
    state.cache[rel] = [stt.st_size, stt.st_mtime_ns, sha]
    return sha


# ── доверенные метаданные ─────────────────────────────────────────────────────────────────────────────────────
def check_manifest_shape(man, ch, chan):
    """Поля манифеста и записей (типы, хэши, пути, повторы, файл-и-папка) — до любой работы с диском."""
    if not isinstance(man, dict) or man.get("format") != "asda-manifest/1":
        raise UpdateError("манифест: чужой формат")
    if man.get("version") != chan["version"] or man.get("seq") != chan["seq"] or man.get("channel") != ch:
        raise UpdateError("манифест не той версии")
    files = man.get("files")
    if not isinstance(files, list):
        raise UpdateError("манифест: нет списка файлов")
    seen = set()
    for fe in files:
        if not isinstance(fe, dict) or not isinstance(fe.get("size"), int) or fe["size"] < 0 \
                or not isinstance(fe.get("count"), int) or fe["count"] < 0:
            raise UpdateError("манифест: запись с неверными полями")
        try:
            F.check_rel_path(fe.get("path"))
            F.check_hex64(fe.get("sha256"), fe["path"])
            F.check_hex64(fe.get("pieces"), fe["path"])
        except ValueError as e:
            raise UpdateError(str(e))
        low = fe["path"].lower()
        if low in seen:
            raise UpdateError("манифест: повтор пути (регистр): %s" % fe["path"])
        seen.add(low)
    dirs = set()
    for p in seen:
        parts = p.split("/")
        dirs.update("/".join(parts[:i]) for i in range(1, len(parts)))
    both = seen & dirs
    if both:
        raise UpdateError("манифест: путь %s — и файл, и папка" % sorted(both)[0])


def fetch_trusted(src, root_pub, ch, state, min_keyring_seq, min_seq, log):
    """Keyring → channel → манифест, с проверками подписи, отката и хэшей. → (channel, manifest, mblob, keyring)."""
    try:
        ring = F.envelope_open("keyring", json.loads(src.get("keys/keyring.json", MAX_ENVELOPE)),
                               {F.key_id(root_pub): root_pub})
        kseq_floor = max(state.s.get("keyring_seq", 0), min_keyring_seq)
        if not isinstance(ring.get("seq"), int) or ring["seq"] < kseq_floor:
            raise UpdateError("keyring seq %s ниже допустимого %d — откат ключей" % (ring.get("seq"), kseq_floor))
        chan = F.envelope_open("channel", json.loads(src.get("channels/%s.json" % ch, MAX_ENVELOPE)),
                               F.keyring_trusted(ring))
    except ValueError as e:
        raise UpdateError(str(e))
    if chan.get("channel") != ch:
        raise UpdateError("конверт канала %s выдан за %s" % (chan.get("channel"), ch))
    if not isinstance(chan.get("seq"), int) or not isinstance(chan.get("manifest"), dict):
        raise UpdateError("канал: неверные поля")
    seq_max = state.s.get("seq_max") if isinstance(state.s.get("seq_max"), dict) else {}
    floor = max(seq_max.get(ch, 0), min_seq)
    if chan["seq"] < floor:
        raise UpdateError("сервер предлагает seq %d ниже уже виденного %d — откат версии, отказ" % (chan["seq"], floor))
    try:
        msha = F.check_hex64(chan["manifest"].get("sha256"), "манифест канала")
    except ValueError as e:
        raise UpdateError(str(e))
    seen_m = state.s.get("seen_manifest", {}) if isinstance(state.s.get("seen_manifest"), dict) else {}
    if chan["seq"] == seq_max.get(ch) and seen_m.get(ch) not in (None, msha):
        raise UpdateError("seq %d уже был с другим манифестом — подмена, отказ (ревизия В2, V7)" % chan["seq"])
    if F.is_expired(chan.get("expires")) or F.is_expired(ring.get("expires")):
        log("! предупреждение: подписанный указатель просрочен (сервер давно не обновлялся или заморожен)")
    # запомнить виденное сразу после проверки подписей (ревизия В2, M6) — по каналу
    seq_max[ch] = max(seq_max.get(ch, 0), chan["seq"])
    seen_m[ch] = msha
    state.s.update({"seq_max": seq_max, "seen_manifest": seen_m,
                    "keyring_seq": max(state.s.get("keyring_seq", 0), ring["seq"])})
    state.save()
    mblob = src.get("manifests/%s.json.zst" % msha, MAX_MANIFEST)
    if F.sha256_hex(mblob) != msha:
        raise UpdateError("манифест: хэш не совпал с подписанным")
    try:
        man = json.loads(F.safe_zstd(mblob, limit=MAX_MANIFEST_RAW).decode("utf-8"))
    except ValueError as e:
        raise UpdateError("манифест: %s" % e)
    check_manifest_shape(man, ch, chan)
    return chan, man, mblob, ring


# ── сборка файлов ─────────────────────────────────────────────────────────────────────────────────────────────
def local_piece_index(state, old_man):
    """{sha куска: [(rel, offset, size)]} по .plst старой версии (каждый кусок при копировании перехэшируется)."""
    idx = {}
    if not old_man:
        return idx
    for fe in old_man.get("files", []):
        data = state.plst(fe["pieces"])
        if data is None:
            continue
        _, pieces = F.plst_unpack(data)
        off = 0
        for h, size, *_ in pieces:
            idx.setdefault(h, []).append((fe["path"], off, size))
            off += size
    return idx


def assemble(src, state, game_dir, fe, plst_data, local_idx, stats):
    """Собрать файл во временный, проверить, заменить."""
    try:
        _, pieces = F.plst_unpack(plst_data)
    except ValueError as e:
        raise UpdateError("%s: %s" % (fe["path"], e))
    if sum(p[1] for p in pieces) != fe["size"] or len(pieces) != fe["count"]:
        raise UpdateError("%s: куски не покрывают файл" % fe["path"])
    dst = safe_path(game_dir, fe["path"])
    if os.path.isdir(dst):
        try:
            os.rmdir(dst)                                # пустая папка на месте нового файла (ревизия В2, V3)
        except OSError:
            raise UpdateError("%s: на месте файла непустая папка" % fe["path"])
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + F.TMP_SUFFIX
    handles = {}
    missing = []          # [(offset в файле, кусок)]
    try:
        with open(tmp, "wb") as out:
            out.truncate(fe["size"])
            off = 0
            for p in pieces:
                h, size = p[0], p[1]
                done = False
                for rel, loff, lsize in local_idx.get(h, []):
                    try:
                        lp = safe_path(game_dir, rel)
                        f = handles.get(lp) or handles.setdefault(lp, open(lp, "rb"))
                        f.seek(loff)
                        data = f.read(lsize)
                    except (OSError, UpdateError, ValueError):
                        continue
                    if len(data) == size and F.sha256_hex(data) == h:
                        out.seek(off)
                        out.write(data)
                        stats["local"] += size
                        done = True
                        break
                if not done:
                    missing.append((off, p))
                off += size
            for f in handles.values():
                f.close()
            handles.clear()
            by_pack = {}
            for foff, p in missing:
                by_pack.setdefault(p[4], []).append((p[5], foff, p))
            for pack, items in by_pack.items():
                items.sort()
                i = 0
                while i < len(items):
                    j, start, end = i, items[i][0], items[i][0] + items[i][2][2]
                    while j + 1 < len(items) and items[j + 1][0] - end <= RANGE_GAP and \
                            items[j + 1][0] + items[j + 1][2][2] - start <= RANGE_MAX:
                        j += 1
                        end = max(end, items[j][0] + items[j][2][2])
                    blob = src.get_range("packs/%s.blob" % pack, start, end - start)
                    for poff, foff, p in items[i:j + 1]:
                        h, size, stored, codec = p[0], p[1], p[2], p[3]
                        raw = blob[poff - start: poff - start + stored]
                        try:
                            data = F.safe_zstd(raw, expected_size=size) if codec == F.CODEC_ZSTD else raw
                        except ValueError as e:
                            raise UpdateError("%s: кусок %s испорчен: %s" % (fe["path"], h[:16], e))
                        if len(data) != size or F.sha256_hex(data) != h:
                            raise UpdateError("%s: скачанный кусок %s испорчен" % (fe["path"], h[:16]))
                        out.seek(foff)
                        out.write(data)
                        stats["net_payload"] += size
                    i = j + 1
            out.flush()
            os.fsync(out.fileno())
        if F.sha256_file(tmp) != fe["sha256"]:
            raise UpdateError("%s: собранный файл не совпал с манифестом" % fe["path"])
        os.replace(tmp, dst)
    finally:
        for f in handles.values():
            f.close()
        if os.path.exists(tmp):
            os.remove(tmp)
    stt = os.stat(dst)
    state.cache[fe["path"]] = [stt.st_size, stt.st_mtime_ns, fe["sha256"]]


def cleanup_tmp(game_dir):
    """Удалить *.asdaupd-tmp, оставшиеся после сбоя (ревизия В2, M8); в junction не заходить."""
    for r, dirs, files in os.walk(game_dir, followlinks=False):
        dirs[:] = [d for d in dirs if d != STATE_DIR and not _is_reparse(os.lstat(os.path.join(r, d)))]
        for fn in files:
            if fn.lower().endswith(F.TMP_SUFFIX):
                os.remove(os.path.join(r, fn))


def remove_empty_parents(game_dir, rel):
    """Удалить опустевшие папки удалённого файла вверх до корня игры."""
    root = os.path.realpath(game_dir)
    d = os.path.dirname(os.path.join(root, *rel.split("/")))
    while os.path.realpath(d) != root and os.path.commonpath([root, os.path.realpath(d)]) == root:
        try:
            os.rmdir(d)
        except OSError:
            break
        d = os.path.dirname(d)


def remove_file(state, game_dir, rel, stats):
    """Удалить файл прежней версии (через проверенный путь) и его кэш."""
    p = safe_path(game_dir, rel)
    if os.path.isfile(p):
        os.remove(p)
        stats["deleted"] += 1
    state.cache.pop(rel, None)


def update(source, game_dir, root_pub, ch="alpha", repair=False, log=print, min_keyring_seq=0, min_seq=0):
    """Обновить/восстановить папку игры до версии канала. → статистика."""
    src = make_source(source) if isinstance(source, str) else source
    os.makedirs(game_dir, exist_ok=True)
    state = State(game_dir, log)
    chan, man, mblob, ring = fetch_trusted(src, root_pub, ch, state, min_keyring_seq, min_seq, log)
    old_man = state.manifest()
    use_cache = not repair
    stats = {"files": len(man["files"]), "changed": 0, "local": 0, "net_payload": 0, "deleted": 0}
    cleanup_tmp(game_dir)
    # признак незавершённого обновления — до первого изменения на диске (ревизия В2, V8)
    state.s["pending"] = {"version": chan["version"], "seq": chan["seq"], "manifest": chan["manifest"]["sha256"]}
    state.save()
    local_idx = local_piece_index(state, old_man)
    new_paths = {fe["path"].lower() for fe in man["files"]}
    new_dirs = set()
    for p in new_paths:
        parts = p.split("/")
        new_dirs.update("/".join(parts[:i]) for i in range(1, len(parts)))
    gone = [fe["path"] for fe in (old_man or {}).get("files", []) if fe["path"].lower() not in new_paths]
    # 1) конфликтующие старые файлы — сразу (ревизия В2, V3): файл на месте будущей папки (файл → папка) и файлы
    #    внутри папки, на месте которой будет файл (папка → файл). Их куски уже в индексе, но копировать их можно
    #    только пока файлы есть — поэтому такие куски, если нужны, будут скачаны. Остальные исчезнувшие удаляются
    #    ПОСЛЕ сборки: их куски могут понадобиться новым файлам.
    for rel in gone:
        low = rel.lower()
        parts = low.split("/")
        if low in new_dirs or any("/".join(parts[:i]) in new_paths for i in range(1, len(parts))):
            remove_file(state, game_dir, rel, stats)
            remove_empty_parents(game_dir, rel)
    # 2) сборка/замена файлов
    for fe in man["files"]:
        if file_sha_cached(state, game_dir, fe["path"], use_cache) == fe["sha256"]:
            continue
        stats["changed"] += 1
        data = state.plst(fe["pieces"])
        if data is None:
            data = src.get("objects/%s/%s.plst" % (fe["pieces"][:2], fe["pieces"]), MAX_PLST)
            if F.sha256_hex(data) != fe["pieces"]:
                raise UpdateError("%s: список кусков не совпал с манифестом" % fe["path"])
        assemble(src, state, game_dir, fe, data, local_idx, stats)
        with open(os.path.join(state.dir, "objects", fe["pieces"] + ".plst"), "wb") as f:
            f.write(data)
        state.save()                                     # прогресс не теряется при обрыве
    # 3) остальные исчезнувшие файлы и опустевшие папки
    for rel in gone:
        remove_file(state, game_dir, rel, stats)
        remove_empty_parents(game_dir, rel)
    # 4) .plst текущей версии сохранить (индекс локальных кусков для следующего обновления), лишние удалить
    keep = {fe["pieces"] for fe in man["files"]}
    for fe in man["files"]:
        p = os.path.join(state.dir, "objects", fe["pieces"] + ".plst")
        if not os.path.isfile(p):
            data = src.get("objects/%s/%s.plst" % (fe["pieces"][:2], fe["pieces"]), MAX_PLST)
            if F.sha256_hex(data) != fe["pieces"]:
                raise UpdateError("список кусков %s испорчен" % fe["pieces"])
            open(p, "wb").write(data)
    for fn in os.listdir(os.path.join(state.dir, "objects")):
        if fn.endswith(".plst") and fn[:-5] not in keep:
            os.remove(os.path.join(state.dir, "objects", fn))
    mpath = os.path.join(state.dir, "manifest.json.zst")
    with open(mpath + ".tmp", "wb") as f:
        f.write(mblob)
        f.flush()
        os.fsync(f.fileno())
    os.replace(mpath + ".tmp", mpath)
    state.s.update({"channel": ch, "version": chan["version"], "seq": chan["seq"],
                    "manifest": chan["manifest"]["sha256"], "min_version": chan.get("min_version")})
    state.s.pop("pending", None)
    state.save()
    stats["downloaded"] = src.downloaded
    log("версия %s seq %d: изменено файлов %d, из локальных кусков %.1f МБ, скачано %.1f МБ (полезных %.1f), удалено %d"
        % (chan["version"], chan["seq"], stats["changed"], stats["local"] / 1e6, src.downloaded / 1e6,
           stats["net_payload"] / 1e6, stats["deleted"]))
    return stats


def read_root_pub(s):
    """Корневой ключ: hex-строка или путь к 32-байтному файлу."""
    b = open(s, "rb").read() if os.path.isfile(s) else bytes.fromhex(s)
    if len(b) != 32:
        raise UpdateError("корневой ключ — 32 байта")
    return b


def main(argv=None):
    """Точка входа."""
    ap = argparse.ArgumentParser(description="Эталонный клиент обновлений AsdaReborn (формат v1)")
    ap.add_argument("cmd", choices=("update", "verify"))
    ap.add_argument("--source", required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--root-pub", required=True)
    ap.add_argument("--channel", default="alpha")
    ap.add_argument("--min-keyring-seq", type=int, default=0, help="вшитый в лаунчер минимум keyring seq")
    ap.add_argument("--min-seq", type=int, default=0, help="вшитый в лаунчер минимум seq канала")
    a = ap.parse_args(argv)
    try:
        update(a.source, a.dir, read_root_pub(a.root_pub), a.channel, repair=(a.cmd == "verify"),
               min_keyring_seq=a.min_keyring_seq, min_seq=a.min_seq)
        return 0
    except (UpdateError, ValueError) as e:
        print("ОТКАЗ: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main())
