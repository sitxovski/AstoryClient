# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# eac_build_manifest.py — манифест входов EAC-сборки и выбор режима упаковки (auto) для Tools/eac_package.py.
#   16.10.2026, agent-eac-diaglog (решение владельца: «скрипт сам определяет, что изменилось, и сам выбирает тип»).
#
# МАНИФЕСТ (<BUILD_EAC>\<сборка>\build_manifest.json) — по категориям, по каждому файлу [размер, mtime_ns, sha1]:
#   code       — Source/**, Plugins/*/Source/** (там же *.Build.cs, *.Target.cs, Source/ThirdParty/**);
#   content    — Content/**, Plugins/*/Content/**, AsdaReborn.uproject, Plugins/*/*.uplugin (состав модулей/плагинов
#                меняет кук, поэтому — уровень content);
#   config_pak — Config/**/*.ini (кроме DefaultCrypto.ini — он в meta как id набора ключей) и прочие файлы проекта,
#                которые стейджинг кладёт в pak не из Content (по Manifest_UFSFiles_Win64.txt базовой сборки; файлы,
#                которые порождает сам кук — AssetRegistry.bin, *.upluginmanifest, — пропускаются);
#   nonufs     — файлы вне pak, которые кладёт скрипт: тулкит EAC (zip), эталон DLL EOS из SDK, сам eac_package.py
#                (раскладка, README, Settings.json-шаблон);
#   meta       — версия движка (Build.version), id набора ключей (DefaultCrypto.ini), формат упаковки PACKAGE_FORMAT,
#                sha1 секции [/Script/UnrealEd.ProjectPackagingSettings] DefaultGame.ini, sha1 команды UAT.
#   Кэш хешей — <BUILD_EAC>\eac_hash_cache.json: путь → (размер, mtime_ns, sha1); файл перехешируется только при смене
#   размера или mtime.
#
# РЕШЕНИЕ (decide) — от лёгкого к тяжёлому, с причинами:
#   none     — ничего не изменилось (сборка не нужна; --force → code);
#   code     — только code/nonufs: UBT Shipping exe + копия базовой + подмена exe/DLL + integrity;
#   skipcook — есть config_pak, нет content: UAT -build -skipcook -stage -pak (прежние ключи, кук из Saved/Cooked);
#   iterate  — есть content: UAT -build -cook -iterate -stage -pak (прежние ключи, инкрементальный кук);
#   full     — изменился meta или --rotate-keys (eac_package; только major-версия): новые ключи gen_build_crypto + полный кук.
#   Подъём на ступень: нет базы/манифеста → full; ключи базы ≠ текущим → full; Saved/Cooked не от базы → skipcook→iterate,
#   iterate→full.
# При импорте ничего не делает (закон tools-no-import-side-effects). Самотест: python Tools/eac_build_manifest.py --selftest
import datetime
import fnmatch
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

MANIFEST_NAME = "build_manifest.json"     # имя манифеста в папке сборки
LATEST_NAME = "latest.json"               # указатель на последнюю успешную сборку в корне BUILD_EAC
HASH_CACHE_NAME = "eac_hash_cache.json"   # кэш sha1 в корне BUILD_EAC
MANIFEST_VERSION = 1                      # версия формата манифеста
# ★ Формат упаковки (раскладка архива, состав проверок, флаги UAT полного режима). Поднимать ТОЛЬКО при смене того, что
#   кладётся в архив иначе, чем в прошлых сборках, — тогда auto выберет full. Базовая сборка 2026-10-16 — формат 1.
PACKAGE_FORMAT = 1
ENGINE_BUILD_VERSION = os.path.join(os.environ.get("ASDA_UE_DIR") or r"D:\Games\UE_5.8",
                                    "Engine", "Build", "Build.version")   # версия движка (Changelist); корень — ASDA_UE_DIR

CATEGORIES = ("code", "content", "config_pak", "nonufs")     # файловые категории (meta — отдельно)
MODE_ORDER = ("none", "code", "skipcook", "iterate", "full")  # режимы от лёгкого к тяжёлому
CATEGORY_MODE = {"code": "code", "nonufs": "code", "config_pak": "skipcook", "content": "iterate"}  # минимум режима
# Файлы Content/, которые порождает кук (в Manifest_UFSFiles есть, исходника в Content/ нет и не было)
COOK_ONLY_CONTENT = ("ShaderArchive-*", "ShaderTypeInfo-*", "*.ushaderbytecode", "*.stinfo", "*.upipelinecache")
# Файлы, которые порождает кук/стейдж (в Manifest_UFSFiles есть, а в исходниках проекта — нет)
GENERATED_UFS = ("AssetRegistry.bin", "*.upluginmanifest", "*.ushaderbytecode", "*.uasset", "*.uexp", "*.ubulk",
                 "*.umap", "*.ufont", "*.uptnl", "*.utxt", "*.utxtmeta")


# ── хеши с кэшем ───────────────────────────────────────────────────────────────────────────────────────────────
def sha1_file(path, chunk=8 * 1024 * 1024):
    """sha1 файла кусками."""
    h = hashlib.sha1()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


class HashCache:
    """Кэш sha1 по (путь, размер, mtime_ns): перехеширование только изменённых файлов."""

    def __init__(self, path=None):
        self.path = path          # файл кэша (None — только в памяти)
        self.data = {}            # путь (нижний регистр) → [размер, mtime_ns, sha1]
        self.hits = 0             # попаданий в кэш
        self.hashed = 0           # перехешировано файлов
        self.hashed_bytes = 0     # перехешировано байт
        if path and os.path.isfile(path):
            try:
                self.data = json.load(open(path, "r", encoding="utf-8"))
            except (OSError, ValueError):
                self.data = {}

    def get_many(self, items, workers=8):
        """items: [(абс. путь, размер, mtime_ns)] → {абс. путь: sha1}; промахи хешируются параллельно."""
        out, todo = {}, []
        for p, size, mt in items:
            c = self.data.get(p.lower())
            if c and c[0] == size and c[1] == mt:
                out[p] = c[2]
                self.hits += 1
            else:
                todo.append((p, size, mt))
        if todo:
            with ThreadPoolExecutor(max_workers=workers) as ex:
                for (p, size, mt), h in zip(todo, ex.map(lambda t: sha1_file(t[0]), todo)):
                    out[p] = h
                    self.data[p.lower()] = [size, mt, h]
                    self.hashed += 1
                    self.hashed_bytes += size
        return out

    def save(self):
        """Записать кэш (атомарно через .part)."""
        if not self.path:
            return
        tmp = self.path + ".part"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f, separators=(",", ":"))
        os.replace(tmp, self.path)


# ── сбор входов ────────────────────────────────────────────────────────────────────────────────────────────────
def _walk(root, sub):
    """Файлы под root/sub: (абс. путь, отн. путь от root с '/')."""
    base = os.path.join(root, sub)
    if not os.path.isdir(base):
        return
    for d, _, files in os.walk(base):
        for fn in files:
            p = os.path.join(d, fn)
            yield p, os.path.relpath(p, root).replace("\\", "/")


def ufs_project_entries(ufs_manifest_path, project_name="AsdaReborn"):
    """Из Manifest_UFSFiles_Win64.txt базы: {отн. путь проекта: mtime(UTC-строка)} — только файлы проекта."""
    out = {}
    if not ufs_manifest_path or not os.path.isfile(ufs_manifest_path):
        return out
    pref = project_name + "/"
    with open(ufs_manifest_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\r\n").split("\t")
            if parts and parts[0].startswith(pref):
                out[parts[0][len(pref):]] = parts[1] if len(parts) > 1 else ""
    return out


def classify_project_files(project_dir, extra_nonufs=(), ufs_entries=None):
    """Категория → {отн. путь: абс. путь}. extra_nonufs — абсолютные пути вне проекта (ключ = 'ext:<имя>')."""
    cats = {c: {} for c in CATEGORIES}
    for p, rel in _walk(project_dir, "Source"):
        cats["code"][rel] = p
    for p, rel in _walk(project_dir, "Content"):
        cats["content"][rel] = p
    plug = os.path.join(project_dir, "Plugins")
    if os.path.isdir(plug):
        for name in sorted(os.listdir(plug)):
            pd = os.path.join(plug, name)
            if not os.path.isdir(pd):
                continue
            for p, rel in _walk(project_dir, "Plugins/%s/Source" % name):
                cats["code"][rel] = p
            for p, rel in _walk(project_dir, "Plugins/%s/Content" % name):
                cats["content"][rel] = p
            for fn in os.listdir(pd):
                if fn.lower().endswith(".uplugin"):
                    cats["content"]["Plugins/%s/%s" % (name, fn)] = os.path.join(pd, fn)
    for fn in os.listdir(project_dir):
        if fn.lower().endswith(".uproject"):
            cats["content"][fn] = os.path.join(project_dir, fn)
    for p, rel in _walk(project_dir, "Config"):
        if rel.lower().endswith(".ini") and os.path.basename(rel).lower() != "defaultcrypto.ini":
            cats["config_pak"][rel] = p
    # Прочие файлы проекта, уходящие в pak не из Content/Config (RuntimeDependencies UFS и т. п.) — по манифесту базы.
    for rel in sorted(ufs_entries or {}):
        if rel.startswith(("Content/", "Config/", "Plugins/")) or rel.endswith(".uproject"):
            continue
        if any(fnmatch.fnmatch(os.path.basename(rel), g) for g in GENERATED_UFS):
            continue
        p = os.path.join(project_dir, rel.replace("/", os.sep))
        if os.path.isfile(p):
            cats["config_pak"][rel] = p
    for p in extra_nonufs:
        if os.path.isfile(p):
            cats["nonufs"]["ext:" + os.path.basename(p)] = p
    return cats


def snapshot(cats, cache, base=None):
    """Категории → {кат: {отн.: [размер, mtime_ns, sha1]}}. Совпадение (размер, mtime) с базой — sha1 берётся из базы."""
    out = {c: {} for c in cats}
    items = []
    stat_of = {}
    for c, files in cats.items():
        for rel, p in files.items():
            st = os.stat(p)
            stat_of[(c, rel)] = (p, st.st_size, st.st_mtime_ns)
            b = ((base or {}).get(c) or {}).get(rel)
            if b and b[0] == st.st_size and b[1] == st.st_mtime_ns and b[2]:
                out[c][rel] = [st.st_size, st.st_mtime_ns, b[2]]
            else:
                items.append((p, st.st_size, st.st_mtime_ns))
    hashes = cache.get_many(items)
    for (c, rel), (p, size, mt) in stat_of.items():
        if rel not in out[c]:
            out[c][rel] = [size, mt, hashes[p]]
    return out


# ── meta ───────────────────────────────────────────────────────────────────────────────────────────────────────
def ini_section_sha1(ini_path, section):
    """sha1 строк секции ini (без комментариев и пустых строк); '' — секции нет."""
    if not os.path.isfile(ini_path):
        return ""
    cur, lines = None, []
    with open(ini_path, "r", encoding="utf-8-sig", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if line.startswith("[") and line.endswith("]"):
                cur = line[1:-1]
                continue
            if cur == section and line and not line.startswith(";"):
                lines.append(line)
    return hashlib.sha1("\n".join(lines).encode("utf-8")).hexdigest() if lines else ""


def engine_version(path=ENGINE_BUILD_VERSION):
    """«5.8.2 CL 56702186» из Build.version; '?' — нет файла."""
    try:
        j = json.load(open(path, "r", encoding="utf-8"))
        return "%s.%s.%s CL %s" % (j["MajorVersion"], j["MinorVersion"], j["PatchVersion"], j["Changelist"])
    except (OSError, ValueError, KeyError):
        return "?"


def current_meta(project_dir, key_set_id, uat_cmd):
    """meta текущего состояния."""
    return {
        "engine": engine_version(),
        "key_set": key_set_id or "",
        "package_format": PACKAGE_FORMAT,
        "packaging_settings_sha1": ini_section_sha1(os.path.join(project_dir, "Config", "DefaultGame.ini"),
                                                    "/Script/UnrealEd.ProjectPackagingSettings"),
        "uat_cmd_sha1": hashlib.sha1(uat_cmd.encode("utf-8")).hexdigest(),
    }


# ── сравнение и решение ───────────────────────────────────────────────────────────────────────────────────────
def diff_category(base_files, cur_files, unknown=()):
    """Изменения одной категории: {'added': [...], 'removed': [...], 'modified': [...], 'unknown': [...]}."""
    base_files = base_files or {}
    unknown = set(unknown or ())
    added, removed, modified, unk = [], [], [], []
    for rel, v in cur_files.items():
        b = base_files.get(rel)
        if b is None:
            (unk if rel in unknown else added).append(rel)
        elif b[2] != v[2]:
            modified.append(rel)
    for rel in base_files:
        if rel not in cur_files:
            removed.append(rel)
    return {"added": sorted(added), "removed": sorted(removed), "modified": sorted(modified), "unknown": sorted(unk)}


def changed_count(d):
    """Число изменённых файлов в результате diff_category."""
    return sum(len(d[k]) for k in ("added", "removed", "modified", "unknown"))


def _up(mode):
    """Следующая (тяжелее) ступень."""
    i = MODE_ORDER.index(mode)
    return MODE_ORDER[min(i + 1, len(MODE_ORDER) - 1)]


def decide(base_manifest, cur_snapshot, cur_meta, base_keys_ok, cooked_ok, release=False, force=False, cooked_trusted=False):
    """Выбор режима. Возвращает (режим, [причины], {кат: diff}). Чистая функция (без диска).
    cooked_trusted (★ 10.11.2026) — Saved/Cooked не кук базы ЭТОЙ цепочки, но целый успешный кук другой цепочки
    (Dev ↔ Shipping) теми же ключами и с тем же meta: кук от конфигурации клиента не зависит (команда -run=Cook одна),
    поэтому iterate на нём допустим (кукер сам сверяет каждый пакет с исходником); skipcook — нет (поднимается до iterate)."""
    reasons = []
    diffs = {}
    if release:
        return "full", ["--rotate-keys: смена major-версии — новый набор ключей и полный кук"], diffs
    if not base_manifest:
        return "full", ["нет базовой сборки с манифестом — полный режим"], diffs
    bm = base_manifest.get("meta") or {}
    meta_changes = [k for k in sorted(set(bm) | set(cur_meta)) if bm.get(k) != cur_meta.get(k)]
    if meta_changes:
        return "full", ["meta изменился: %s" % ", ".join("%s «%s» → «%s»" % (k, bm.get(k), cur_meta.get(k))
                                                        for k in meta_changes)], diffs
    if not base_keys_ok:
        return "full", ["базовая сборка собрана НЕ текущим набором ключей — быстрый и skipcook-режимы запрещены"], diffs
    unknown = base_manifest.get("unknown") or {}
    mode = "none"
    for c in CATEGORIES:
        d = diff_category((base_manifest.get("categories") or {}).get(c), cur_snapshot.get(c) or {}, unknown.get(c))
        diffs[c] = d
        n = changed_count(d)
        if n:
            need = CATEGORY_MODE[c]
            reasons.append("%s: %d изм. (добавлено %d, удалено %d, изменено %d, неизвестно с базы %d) → не ниже «%s»" % (
                c, n, len(d["added"]), len(d["removed"]), len(d["modified"]), len(d["unknown"]), need))
            if MODE_ORDER.index(need) > MODE_ORDER.index(mode):
                mode = need
    if mode == "none":
        if force:
            reasons.append("изменений нет, но --force — пересборка exe быстрым режимом")
            mode = "code"
        else:
            reasons.append("изменений с базы нет — сборка не нужна (--force пересоберёт)")
            return "none", reasons, diffs
    if mode in ("skipcook", "iterate") and not cooked_ok:
        if cooked_trusted:
            # ★ 10.11.2026: раньше Dev- и Shipping-сборки сбрасывали друг другу кук в full (≈25 мин вместо ≈10)
            reasons.append("Saved/Cooked — целый кук другой цепочки (те же ключи и meta): %s → iterate, полный кук не нужен" % mode)
            mode = "iterate"
        else:
            new = _up(mode)
            reasons.append("Saved/Cooked не совпадает с куком базы — ступень выше: %s → %s" % (mode, new))
            mode = new
    return mode, reasons, diffs


# ── манифест: запись/чтение ───────────────────────────────────────────────────────────────────────────────────
def write_manifest(build_dir, data):
    """Записать build_manifest.json в папку сборки (атомарно)."""
    p = os.path.join(build_dir, MANIFEST_NAME)
    tmp = p + ".part"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, separators=(",", ":"))
    os.replace(tmp, p)
    return p


def read_manifest(build_dir):
    """build_manifest.json папки сборки или None."""
    p = os.path.join(build_dir, MANIFEST_NAME)
    if not os.path.isfile(p):
        return None
    try:
        return json.load(open(p, "r", encoding="utf-8"))
    except (OSError, ValueError):
        return None


def write_latest(build_root, build_dir, mode):
    """BUILD_EAC\\latest.json → последняя успешная сборка."""
    p = os.path.join(build_root, LATEST_NAME)
    data = {"build": os.path.basename(build_dir.rstrip("\\/")), "build_dir": build_dir,
            "game_root": os.path.join(build_dir, "Windows"), "manifest": os.path.join(build_dir, MANIFEST_NAME),
            "mode": mode, "written": datetime.datetime.now().isoformat(" ", "seconds")}
    with open(p + ".part", "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    os.replace(p + ".part", p)
    return p


def read_latest(build_root):
    """Папка последней сборки из latest.json или None."""
    p = os.path.join(build_root, LATEST_NAME)
    try:
        d = json.load(open(p, "r", encoding="utf-8"))
        return d.get("build_dir")
    except (OSError, ValueError):
        return None


def build_retro_manifest(cats, cache, t0_epoch, meta, ufs_entries, retro_note):
    """Ретроспективный манифест базы без собственного манифеста.

    Доказательство: файл с mtime ≤ t0 (начало сборки базы) с тех пор не менялся — его текущее содержимое и есть то, что
    видела база. Файлы с mtime > t0 — состояние на момент базы недоказуемо → «unknown» (считаются изменёнными).
    Удалённые после базы файлы content/config_pak видны по Manifest_UFSFiles базы (кук-выход → исходник): исходника
    нет — запись «был в базе» с sha1 '?' (diff покажет removed). Удаления в code/nonufs ретро не видит — в режиме code
    UBT всё равно собирает по текущему дереву."""
    t0_ns = int(t0_epoch * 1e9)
    cats_known = {c: {} for c in cats}
    unknown = {c: [] for c in cats}
    for c, files in cats.items():
        for rel, p in files.items():
            if os.stat(p).st_mtime_ns <= t0_ns:
                cats_known[c][rel] = p
            else:
                unknown[c].append(rel)
    snap = snapshot(cats_known, cache)
    # content: кук-выход базы → исходные пакеты; нет исходника сейчас — удалён после базы
    removed_content = 0
    content_src = set(cats.get("content", {}).keys())
    for rel in ufs_entries or {}:
        if not rel.startswith("Content/"):
            continue
        stem, ext = os.path.splitext(rel)
        base_name = os.path.basename(rel)
        if rel.startswith("Content/Paks/") or any(fnmatch.fnmatch(base_name, g) for g in COOK_ONLY_CONTENT):
            continue   # порождены куком/стейджем (шейдерные архивы, pak) — исходника нет и не было
        if ext.lower() in (".uexp", ".ubulk", ".uptnl", ".ufont"):
            cands = [stem + ".uasset", stem + ".umap"]   # спутники кукнутого пакета
        elif ext.lower() in (".uasset", ".umap"):
            cands = [stem + ".uasset", stem + ".umap"]
        else:
            cands = [rel]
        if not any(c in content_src for c in cands):
            snap["content"][rel] = [0, 0, "?"]   # был в базе, исходника нет → diff даст removed
            removed_content += 1
    for rel in ufs_entries or {}:
        if rel.startswith("Config/") and rel not in cats.get("config_pak", {}) \
                and os.path.basename(rel).lower() != "defaultcrypto.ini":
            snap["config_pak"][rel] = [0, 0, "?"]
    return {"version": MANIFEST_VERSION, "retro": True, "retro_note": retro_note, "meta": meta,
            "categories": snap, "unknown": unknown, "removed_since_base_content": removed_content,
            "created": datetime.datetime.now().isoformat(" ", "seconds")}


# ── самотест ───────────────────────────────────────────────────────────────────────────────────────────────────
def selftest():
    """5 сценариев на временном дереве проекта: none, code, skipcook, iterate, full (+ подъём ступени). 0 — всё верно."""
    root = tempfile.mkdtemp(prefix="asda_eac_manifest_selftest_")
    fails = 0
    try:
        def w(rel, text):
            p = os.path.join(root, rel.replace("/", os.sep))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8") as f:
                f.write(text)
        w("Source/Mod/A.cpp", "int a=1;")
        w("Source/Mod/Mod.Build.cs", "x")
        w("Content/Maps/M.umap", "map")
        w("Content/UI/W.uasset", "w")
        w("Config/DefaultGame.ini", "[/Script/UnrealEd.ProjectPackagingSettings]\nBuildConfiguration=Shipping\n")
        w("Config/DefaultCrypto.ini", "; AsdaPakKeyBuildId=K1\n")
        w("P.uproject", "{}")
        w("ext/toolkit.zip", "zip")
        ext = [os.path.join(root, "ext", "toolkit.zip")]
        cache = HashCache(None)
        meta = {"engine": "5.8.2 CL 1", "key_set": "K1", "package_format": 1,
                "packaging_settings_sha1": ini_section_sha1(os.path.join(root, "Config", "DefaultGame.ini"),
                                                            "/Script/UnrealEd.ProjectPackagingSettings"),
                "uat_cmd_sha1": "u"}
        base = {"meta": dict(meta), "categories": snapshot(classify_project_files(root, ext), cache), "unknown": {}}

        counter = [0]   # номер сценария (имя временной копии)

        def run(name, expect, mutate=None, meta2=None, keys_ok=True, cooked_ok=True, release=False, force=False, trusted=False):
            nonlocal fails
            counter[0] += 1
            src = root + "_s%d" % counter[0]   # копия дерева рядом (имя без спецсимволов сценария)
            shutil.copytree(root, src)
            try:
                if mutate:
                    mutate(src)
                bcopy = json.loads(json.dumps(base))       # временная копия манифеста базы
                cur = snapshot(classify_project_files(src, [os.path.join(src, "ext", "toolkit.zip")]), cache, None)
                mode, why, _ = decide(bcopy, cur, meta2 or meta, keys_ok, cooked_ok, release, force, trusted)
                ok = mode == expect
                fails += 0 if ok else 1
                print("  [%s] %-34s → %-8s (ожидали %s): %s" % ("ОК" if ok else "ОШИБКА", name, mode, expect, "; ".join(why)))
            finally:
                shutil.rmtree(src, ignore_errors=True)

        def edit(rel, text):
            return lambda r: open(os.path.join(r, rel.replace("/", os.sep)), "w", encoding="utf-8").write(text)

        def touch(rel):
            def f(r):
                p = os.path.join(r, rel.replace("/", os.sep))
                st = os.stat(p)
                os.utime(p, ns=(st.st_atime_ns, st.st_mtime_ns + 5 * 10 ** 9))   # mtime сдвинут, содержимое то же
            return f
        print("самотест выбора режима (временное дерево %s):" % root)
        run("1-ничего (только touch)", "none", touch("Source/Mod/A.cpp"))
        run("2-код", "code", edit("Source/Mod/A.cpp", "int a=2;"))
        run("2б-тулкит nonufs", "code", edit("ext/toolkit.zip", "zip2"))
        run("3-конфиг в pak", "skipcook", edit("Config/DefaultGame.ini",
                                               "[/Script/UnrealEd.ProjectPackagingSettings]\nBuildConfiguration=Shipping\n[X]\nA=1\n"))
        run("3б-конфиг, кук не от базы", "iterate", edit("Config/DefaultGame.ini",
                                                         "[/Script/UnrealEd.ProjectPackagingSettings]\nBuildConfiguration=Shipping\n[X]\nA=2\n"),
            cooked_ok=False)
        run("3в-конфиг, кук другой цепочки", "iterate", edit("Config/DefaultGame.ini",
                                                             "[/Script/UnrealEd.ProjectPackagingSettings]\nBuildConfiguration=Shipping\n[X]\nA=3\n"),
            cooked_ok=False, trusted=True)
        run("4в-ассет, кук другой цепочки", "iterate", edit("Content/UI/W.uasset", "w3"), cooked_ok=False, trusted=True)
        run("4г-ассет, кук чужой и не доверенный", "full", edit("Content/UI/W.uasset", "w4"), cooked_ok=False)
        run("2в-код, кук другой цепочки", "code", edit("Source/Mod/A.cpp", "int a=3;"), cooked_ok=False, trusted=True)
        run("4-ассет + код", "iterate", lambda r: (edit("Content/UI/W.uasset", "w2")(r), edit("Source/Mod/A.cpp", "b")(r)))
        run("4б-ассет удалён", "iterate", lambda r: os.remove(os.path.join(r, "Content", "UI", "W.uasset")))
        m2 = dict(meta)
        m2["key_set"] = "K2"
        run("5-meta: новые ключи", "full", meta2=m2)
        run("5б-ключи базы не те", "full", edit("Source/Mod/A.cpp", "c"), keys_ok=False)
        run("5в---release", "full", release=True)
        run("1б-ничего + --force", "code", force=True)
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("самотест: %s" % ("ВСЁ ВЕРНО" if fails == 0 else "%d ОШИБОК" % fails))
    return fails


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    if "--selftest" in sys.argv:
        sys.exit(1 if selftest() else 0)
    print("модуль eac_build_manifest: используется из eac_package.py; самотест — --selftest")
