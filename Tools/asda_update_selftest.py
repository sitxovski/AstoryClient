# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_update_selftest.py — ★ СКВОЗНОЙ САМОТЕСТ ОБНОВЛЕНИЙ v1 (17.10.2026, система обновлений, волна 2; ревизия review_wave2.md).
#
# Во ВРЕМЕННОМ хранилище с ВРЕМЕННЫМИ ключами (DPAPI текущей учётки; боевое patches/ не трогается):
#   1. выпуск v1 из сборки A, verify --deep;  2. установка с нуля → каждый файл = сборка A;
#   3. выпуск v2 из сборки B → обновление (сколько скачано) → каждый файл = сборка B; повторное обновление = 0 байт;
#   4. «Проверить файлы»: порча байта в самом большом файле + удалённый файл → восстановление;
#   5. атаки: откат канала, чужой ключ, изменённая нагрузка, чужой корень, порча манифеста/.plst/куска, опасные пути,
#      повтор пути регистром, тот же seq с другим манифестом, junction в папке игры, zstd-бомба;
#   6. надёжность: испорченный state.json, обрыв посреди обновления (признак pending), откат seq в хранилище;
#   7. ключи: копия под пароль, корень офлайн, ротация с отзывом → выпуск → обновление, вшитый минимум keyring;
#   8. порядок publish-set (channel последним), без закрытых ключей.
# --unit (без сборок): синтетика из нескольких МБ + переход «файл → папка» между версиями.
# --client <exe> (волна 4): все сценарии гоняются через C++-клиент (AsdaLauncherCli.exe) вместо эталона на Python —
#   те же проверки, те же ключевые слова отказов (тексты ошибок C++ совпадают с эталоном).
# --server (волна 3): синтетическое хранилище с временными ключами выкладывается командой upload в /www/_selftest на
#   @@ASTORY_UPDATE_HOST@@ (на сервере должен быть временно открыт префикс /_selftest/), эталонный клиент работает по
#   HTTPS (Range, 206/Content-Range): установка, обновление, восстановление, повторная выкладка = 0 загрузок, удаление
#   ненужного после третьей версии; в конце /www/_selftest на сервере очищается.
#
# ЗАПУСК:
#   python Tools/asda_update_selftest.py --unit
#   python Tools/asda_update_selftest.py --server
#   python Tools/asda_update_selftest.py --build-a <…\Windows> --build-b <…\Windows> [--work <папка>]
# Рабочая папка создаётся с маркером .asda_selftest; существующую папку без маркера самотест НЕ удаляет.
# При импорте ничего не делает.

import argparse
import base64
import json
import os
import random
import secrets
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asda_release as R  # noqa: E402
import asda_update_client as C  # noqa: E402
import asda_update_format as F  # noqa: E402

# Маркер рабочей папки самотеста (ревизия В2, V11: не удалять чужие папки)
WORK_MARKER = ".asda_selftest"
# Результаты проверок
RESULTS = []


class ExeClient:
    """Клиент обновлений через AsdaLauncherCli.exe (тот же интерфейс, что у модуля asda_update_client)."""

    def __init__(self, exe):
        self.exe = exe

    def _run(self, args):
        import subprocess
        r = subprocess.run([self.exe] + args, capture_output=True)
        out = r.stdout.decode("utf-8", "replace")
        return r.returncode, out

    def update(self, source, game_dir, root_pub, ch="alpha", repair=False, log=print, min_keyring_seq=0, min_seq=0):
        """Как asda_update_client.update; отказ → C.UpdateError с текстом C++."""
        src = source if isinstance(source, str) else source.base.rstrip("/")
        rc, out = self._run(["verify" if repair else "update", "--source", src, "--dir", game_dir, "--root-pub",
                             root_pub.hex(), "--channel", ch, "--min-keyring-seq", str(min_keyring_seq),
                             "--min-seq", str(min_seq)])
        for line in out.splitlines():
            if line.startswith("LOG "):
                log(line[4:])
            if line.startswith("STATS "):
                return json.loads(line[6:])
        msg = [l[len("ОТКАЗ: "):] for l in out.splitlines() if l.startswith("ОТКАЗ: ")]
        raise C.UpdateError(msg[0] if msg else "клиент завершился с кодом %d: %s" % (rc, out[-300:]))

    def is_ready(self, game_dir):
        """Готова ли установка (status)."""
        rc, out = self._run(["status", "--dir", game_dir])
        return rc == 0 and out.strip() == "ready"


# Активный клиент: модуль-эталон или ExeClient (--client)
CL = C


def check(name, ok, detail=""):
    """Записать результат проверки и напечатать."""
    RESULTS.append((name, bool(ok)))
    print("  [%s] %s %s" % ("ОК" if ok else "ПРОВАЛ", name, detail), flush=True)


def expect_fail(name, fn, needle=""):
    """Проверка, что fn бросает UpdateError/ValueError/ReleaseError (с подстрокой needle)."""
    try:
        fn()
    except (C.UpdateError, ValueError, R.ReleaseError) as e:
        check(name, needle in str(e), "→ отказ: %s" % e)
        return
    check(name, False, "→ НЕ отказал")


def quiet(*_a):
    """Пустой журнал клиента."""


def same_tree(game_dir, build_root):
    """Каждый файл сборки есть в папке игры с тем же SHA-256; лишних (кроме .asdaupd) нет. → (ok, текст)."""
    want = R.list_game_files(build_root)
    have = []
    for r, dirs, files in os.walk(game_dir):
        dirs[:] = [d for d in dirs if d != C.STATE_DIR]
        have += [os.path.relpath(os.path.join(r, f), game_dir).replace("\\", "/") for f in files]
    if sorted(have) != want:
        return False, "состав различается: лишние %s, нет %s" % (sorted(set(have) - set(want))[:3], sorted(set(want) - set(have))[:3])
    for rel in want:
        if F.sha256_file(os.path.join(game_dir, *rel.split("/"))) != F.sha256_file(os.path.join(build_root, *rel.split("/"))):
            return False, "отличается %s" % rel
    return True, "%d файлов совпали" % len(want)


def make_fake_build(root, variant):
    """Синтетическое дерево «игры»; variant 2 — часть файлов изменена/удалена/добавлена, файл conflict/item → папка."""
    rnd = random.Random(7)

    def w(rel, data):
        p = os.path.join(root, *rel.split("/"))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(data)
    big = rnd.randbytes(3 << 20)
    w("AsdaReborn/Binaries/Win64/AsdaReborn-Win64-Shipping.exe", big if variant == 1 else big[:1 << 20] + b"PATCH" + big[(1 << 20) + 5:])
    w("AsdaReborn/Content/Paks/data.bin", b"AsdaReborn config line\n" * 40000)
    w("small.txt", b"hello" if variant == 1 else b"hello v2")
    if variant == 1:
        w("old_only.dat", b"x" * 1000)
        w("conflict/item", b"i am a file")
    else:
        w("new_only.dat", b"y" * 2000)
        w("conflict/item/inner.dat", b"now i am a folder")


def flip_byte(path, offset):
    """Инвертировать байт файла. → исходный байт."""
    with open(path, "r+b") as f:
        f.seek(offset)
        b = f.read(1)
        f.seek(offset)
        f.write(bytes([b[0] ^ 0xFF]))
    return b


def put_byte(path, offset, b):
    """Вернуть байт."""
    with open(path, "r+b") as f:
        f.seek(offset)
        f.write(b)


def signed_channel(st, payload, seed):
    """Записать канал с данной нагрузкой и подписью."""
    open(st.channel_path("alpha"), "wb").write(json.dumps(F.envelope_sign("channel", payload, seed)).encode())


def put_manifest(st, man):
    """Положить манифест в хранилище. → (sha, size)."""
    import zstandard
    blob = zstandard.ZstdCompressor().compress(F.canon_json(man))
    sha = F.sha256_hex(blob)
    open(st.p("manifests", sha + ".json.zst"), "wb").write(blob)
    return sha, len(blob)


def run(build_a, build_b, work, unit):
    """Весь сценарий. → код возврата."""
    if unit:
        build_a, build_b = os.path.join(work, "buildA"), os.path.join(work, "buildB")
        make_fake_build(build_a, 1)
        make_fake_build(build_b, 2)
        orig = R.resolve_game_root
        R.resolve_game_root = lambda arg, ch="alpha": os.path.abspath(arg)      # синтетика без настоящего exe
    st = R.Store(os.path.join(work, "store"))
    game = os.path.join(work, "game")
    g = lambda name: os.path.join(work, name)                        # noqa: E731 — свежая папка игры
    print("== ключи и выпуск v1")
    R.keys_init(st)
    root_pub = R.load_root_pub(st)
    t = time.time()
    R.build_release(st, "alpha", "alpha-1.0.0-10.2026", build_a, None)
    print("   выпуск v1: %.0f с" % (time.time() - t))
    check("verify --deep v1", R.verify_store(st, "alpha", deep=True, keep=1) == 0)
    chan_v1 = open(st.channel_path("alpha"), "rb").read()
    print("== установка с нуля")
    t = time.time()
    s1 = CL.update(st.root, game, root_pub, log=quiet)
    ok, txt = same_tree(game, build_a)
    check("установка = сборка A", ok and CL.is_ready(game), "%s; скачано %.1f МБ за %.0f с" % (txt, s1["downloaded"] / 1e6, time.time() - t))
    print("== выпуск v2 и обновление")
    expect_fail("выпуск той же версии отклонён", lambda: R.build_release(st, "alpha", "alpha-1.0.0-10.2026", build_b, None), "не выше")
    t = time.time()
    R.build_release(st, "alpha", "alpha-1.0.1-10.2026", build_b, None)
    print("   выпуск v2: %.0f с" % (time.time() - t))
    check("verify --deep v2+v1", R.verify_store(st, "alpha", deep=True, keep=2) == 0)
    chan_v2 = open(st.channel_path("alpha"), "rb").read()
    t = time.time()
    s2 = CL.update(st.root, game, root_pub, log=quiet)
    ok, txt = same_tree(game, build_b)
    check("обновление = сборка B" + (" (с переходом файл → папка)" if unit else ""), ok and CL.is_ready(game),
          "%s; скачано %.1f МБ (полезных %.1f, из своих кусков %.1f) за %.0f с"
          % (txt, s2["downloaded"] / 1e6, s2["net_payload"] / 1e6, s2["local"] / 1e6, time.time() - t))
    s3 = CL.update(st.root, game, root_pub, log=quiet)
    check("повторное обновление ничего не качает", s3["net_payload"] == 0 and s3["changed"] == 0,
          "служебных %.3f МБ" % (s3["downloaded"] / 1e6))
    print("== проверка файлов (порча + удаление)")
    man = json.loads(F.safe_zstd(open(os.path.join(game, C.STATE_DIR, "manifest.json.zst"), "rb").read(), limit=1 << 30))
    victim = max(man["files"], key=lambda f: f["size"])
    flip_byte(os.path.join(game, *victim["path"].split("/")), victim["size"] // 2)
    gone = min((f for f in man["files"] if f["size"] > 0), key=lambda f: f["size"])
    os.remove(os.path.join(game, *gone["path"].split("/")))
    s4 = CL.update(st.root, game, root_pub, repair=True, log=quiet)
    ok, txt = same_tree(game, build_b)
    check("восстановление", ok and s4["changed"] == 2, "%s; полезных скачано %.2f МБ, из своих кусков %.1f МБ"
          % (txt, s4["net_payload"] / 1e6, s4["local"] / 1e6))
    print("== атаки")
    payload = json.loads(base64.b64decode(json.loads(chan_v2)["payload"]))
    seed, _ = R.release_signer(st)
    open(st.channel_path("alpha"), "wb").write(chan_v1)
    expect_fail("откат канала на v1", lambda: CL.update(st.root, game, root_pub, log=quiet), "откат")
    signed_channel(st, payload, secrets.token_bytes(32))
    expect_fail("канал, подписанный чужим ключом", lambda: CL.update(st.root, game, root_pub, log=quiet), "нет действительной подписи")
    env = json.loads(chan_v2)
    env2 = dict(env, payload=base64.b64encode(base64.b64decode(env["payload"]).replace(b'"alpha"', b'"alphb"', 1)).decode())
    open(st.channel_path("alpha"), "wb").write(json.dumps(env2).encode())
    expect_fail("изменённая нагрузка канала", lambda: CL.update(st.root, game, root_pub, log=quiet), "нет действительной подписи")
    open(st.channel_path("alpha"), "wb").write(chan_v2)
    expect_fail("клиент с чужим корневым ключом", lambda: CL.update(st.root, game, F.ed_public_bytes(secrets.token_bytes(32)), log=quiet),
                "нет действительной подписи")
    msha = payload["manifest"]["sha256"]
    mp = st.p("manifests", msha + ".json.zst")
    mb = open(mp, "rb").read()
    open(mp, "wb").write(mb[:-1] + bytes([mb[-1] ^ 1]))
    expect_fail("испорченный манифест", lambda: CL.update(st.root, g("g2"), root_pub, log=quiet), "хэш не совпал")
    open(mp, "wb").write(mb)
    m2 = R.read_manifest(st, msha)
    fe = max(m2["files"], key=lambda f: f["size"])
    op = st.p(*st.object_rel(fe["pieces"]).split("/"))
    ob = open(op, "rb").read()
    open(op, "wb").write(ob[:20] + bytes([ob[20] ^ 1]) + ob[21:])
    expect_fail("испорченный .plst", lambda: CL.update(st.root, g("g3"), root_pub, log=quiet), "не совпал")
    open(op, "wb").write(ob)
    _, pieces = F.plst_unpack(ob)
    pk = st.p(*st.pack_rel(pieces[0][4]).split("/"))
    b0 = flip_byte(pk, pieces[0][5])
    expect_fail("испорченный кусок в пачке", lambda: CL.update(st.root, g("g4"), root_pub, log=quiet), "испорчен")
    check("обрыв посреди обновления → игра не готова (pending)", not CL.is_ready(g("g4")))
    put_byte(pk, pieces[0][5], b0)
    CL.update(st.root, g("g4"), root_pub, log=quiet)
    check("после успешного повтора игра готова", CL.is_ready(g("g4")))
    # подписанный (нашим ключом) злонамеренный манифест: каждый — в свежей папке, со своим seq
    for i, (bad, needle) in enumerate((("../evil.txt", "точечный"), ("C:/evil.txt", "абсолютный"), ("AsdaReborn/../../x", "точечный"),
                                       (m2["files"][0]["path"].upper(), "регистр"), ("ASDAUP~1/state.json", "недопустимые"))):
        sha, size = put_manifest(st, dict(m2, files=m2["files"] + [dict(m2["files"][0], path=bad)]))
        signed_channel(st, dict(payload, manifest={"sha256": sha, "size": size}), seed)
        expect_fail("путь «%s» отвергнут" % bad, lambda: CL.update(st.root, g("gp%d" % i), root_pub, log=quiet), needle)
        os.remove(st.p("manifests", sha + ".json.zst"))
    sha, size = put_manifest(st, dict(m2, files=m2["files"][1:]))
    signed_channel(st, dict(payload, manifest={"sha256": sha, "size": size}), seed)
    expect_fail("тот же seq с другим манифестом", lambda: CL.update(st.root, game, root_pub, log=quiet), "другим манифестом")
    os.remove(st.p("manifests", sha + ".json.zst"))
    open(st.channel_path("alpha"), "wb").write(chan_v2)
    bomb = __import__("zstandard").ZstdCompressor().compress(b"\0" * (64 << 20))
    expect_fail("zstd-бомба (заявлено 64 МиБ вместо 1000 б)", lambda: F.safe_zstd(bomb, expected_size=1000), "заявлено")
    # junction внутри папки игры
    content = os.path.join(game, "AsdaReborn", "Content")
    outside = g("outside_content")
    os.rename(content, outside)
    r = subprocess.run(["cmd", "/c", "mklink", "/J", content, outside], capture_output=True)
    if r.returncode == 0:
        expect_fail("запись через junction в папке игры", lambda: CL.update(st.root, game, root_pub, repair=True, log=quiet), "junction")
        os.rmdir(content)
    else:
        check("junction создан для теста", False, r.stderr.decode("cp866", "replace"))
    os.rename(outside, content)
    ok, txt = same_tree(game, build_b)
    check("после атак папка игры цела", ok, txt)
    print("== надёжность состояния и хранилища")
    sp = os.path.join(game, C.STATE_DIR, "state.json")
    open(sp, "w").write("")
    CL.update(st.root, game, root_pub, log=quiet)
    check("пустой state.json → восстановлен из .bak", CL.is_ready(game))
    keep_chan = open(st.channel_path("alpha"), "rb").read()
    open(st.channel_path("alpha"), "wb").write(chan_v1)
    expect_fail("откат channels/alpha.json в хранилище → выпуск отказывает", lambda: R.build_release(st, "alpha", "alpha-1.0.2-10.2026", build_b, None), "расходится")
    open(st.channel_path("alpha"), "wb").write(keep_chan)
    print("== ключи: копия, корень офлайн, ротация с отзывом")
    bk = os.path.join(work, "keys.keybackup")
    R.keys_export_backup(st, bk, password="тестовый-пароль-1234", offline_root=True)
    check("копия ключей расшифровывается своим паролем", len(F.backup_decrypt(open(bk, encoding="utf-8").read(), "тестовый-пароль-1234")) == 2)
    expect_fail("копия с чужим паролем", lambda: F.backup_decrypt(open(bk, encoding="utf-8").read(), "неверный-пароль-0000"), "неверный пароль")
    expect_fail("корень офлайн: ротация без него невозможна", lambda: R.keys_rotate_release(st, True), "корневой ключ недоступен")
    R.keys_import_backup(st, bk, password="тестовый-пароль-1234")
    keyring_v1 = open(st.keyring_path, "rb").read()
    R.keys_rotate_release(st, True)
    check("после ротации с отзывом verify проходит", R.verify_store(st, "alpha", deep=False, keep=2) == 0)
    s5 = CL.update(st.root, game, root_pub, log=quiet)
    check("клиент принял переподписанный канал (та же версия)", s5["changed"] == 0 and CL.is_ready(game))
    R.build_release(st, "alpha", "alpha-1.0.2-10.2026", build_a, None)
    CL.update(st.root, game, root_pub, log=quiet)
    ok, txt = same_tree(game, build_a)
    check("выпуск новым ключом → обновление", ok, txt)
    keyring_v2, chan_v3 = open(st.keyring_path, "rb").read(), open(st.channel_path("alpha"), "rb").read()
    open(st.keyring_path, "wb").write(keyring_v1)
    open(st.channel_path("alpha"), "wb").write(chan_v2)                # подписан ОТОЗВАННЫМ ключом
    expect_fail("старое кольцо при вшитом минимуме keyring seq 2", lambda: CL.update(st.root, g("g7"), root_pub, log=quiet, min_keyring_seq=2), "откат ключей")
    expect_fail("старое кольцо у клиента, видевшего новое", lambda: CL.update(st.root, game, root_pub, log=quiet), "откат ключей")
    open(st.keyring_path, "wb").write(keyring_v2)
    expect_fail("канал, подписанный отозванным ключом", lambda: CL.update(st.root, g("g8"), root_pub, log=quiet), "нет действительной подписи")
    open(st.channel_path("alpha"), "wb").write(chan_v3)
    print("== набор для сервера")
    ps = R.publish_set(st, "alpha", 2)
    check("publish-set без закрытых ключей и журналов", not any("private" in x or "backup" in x or x.endswith(".log") for x in ps),
          "%d файлов" % len(ps))
    check("publish-set: пачки первыми, затем channel и version последними (ревизия В3, М5)",
          ps[0].startswith("packs/") and ps[-2] == "channels/alpha.json" and ps[-1] == "version.json")
    if unit:
        R.resolve_game_root = orig
    bad = [n for n, ok in RESULTS if not ok]
    print("ИТОГ: проверок %d, провалов %d %s" % (len(RESULTS), len(bad), bad))
    return 1 if bad else 0


def run_server(work):
    """Выкладка синтетики на настоящий сервер и клиент по HTTPS. → код возврата."""
    root_remote, url = R.UPLOAD_ROOT + "/_selftest", R.UPLOAD_URL + "/_selftest"
    ba, bb = os.path.join(work, "buildA"), os.path.join(work, "buildB")
    make_fake_build(ba, 1)
    make_fake_build(bb, 2)
    orig = R.resolve_game_root
    R.resolve_game_root = lambda arg, ch="alpha": os.path.abspath(arg)
    st = R.Store(os.path.join(work, "store"))
    game = os.path.join(work, "game")
    up = lambda **kw: R.upload(st, "alpha", root=root_remote, url=url, **kw)    # noqa: E731
    try:
        print("== выпуск v1 и выкладка")
        R.keys_init(st)
        root_pub = R.load_root_pub(st)
        R.build_release(st, "alpha", "alpha-1.0.0-10.2026", ba, None)
        check("выкладка v1", up() == 0)
        chan_v1_local = open(st.channel_path("alpha"), "rb").read()
        ver_v1_local = open(st.p("version.json"), "rb").read()
        src = C.HttpSource(url)
        s1 = CL.update(src, game, root_pub, log=quiet)
        ok, txt = same_tree(game, ba)
        check("установка по HTTPS = сборка A", ok and CL.is_ready(game), "%s; скачано %.2f МБ" % (txt, s1["downloaded"] / 1e6))
        print("== повторная выкладка")
        sf = R.Sftp(R.UPLOAD_HOST, R.UPLOAD_USER, R.UPLOAD_KEY, root_remote)
        before = sf.listing()[0]
        check("повторная выкладка ничего не грузит", up() == 0 and sf.listing()[0] == before)
        print("== v2: обновление по HTTPS")
        R.build_release(st, "alpha", "alpha-1.0.1-10.2026", bb, None)
        check("выкладка v2", up() == 0)
        src = C.HttpSource(url)
        s2 = CL.update(src, game, root_pub, log=quiet)
        ok, txt = same_tree(game, bb)
        check("обновление по HTTPS = сборка B", ok, "%s; скачано %.2f МБ, полезных %.2f" % (txt, s2["downloaded"] / 1e6, s2["net_payload"] / 1e6))
        # устаревшее локальное хранилище (канал откатан) — выкладка обязана отказать, а не откатить игроков
        chan_now = open(st.channel_path("alpha"), "rb").read()
        log_now = open(st.p("releases.log"), "rb").read()
        ver_now = open(st.p("version.json"), "rb").read()
        open(st.channel_path("alpha"), "wb").write(chan_v1_local)            # копия хранилища до v2 (канал + version)
        open(st.p("version.json"), "wb").write(ver_v1_local)
        expect_fail("выкладка из устаревшего хранилища отклонена", lambda: up(), "устарело")
        open(st.channel_path("alpha"), "wb").write(chan_now)
        open(st.p("version.json"), "wb").write(ver_now)
        open(st.p("releases.log"), "wb").write(log_now)
        sf.run(["mkdir %s/.upload-lock" % root_remote])
        expect_fail("занятая блокировка на сервере", lambda: up(), "другая выкладка")
        sf.run(["rmdir %s/.upload-lock" % root_remote])
        man = json.loads(F.safe_zstd(open(os.path.join(game, C.STATE_DIR, "manifest.json.zst"), "rb").read(), limit=1 << 30))
        victim = max(man["files"], key=lambda f: f["size"])
        flip_byte(os.path.join(game, *victim["path"].split("/")), victim["size"] // 2)
        src = C.HttpSource(url)
        s3 = CL.update(src, game, root_pub, repair=True, log=quiet)
        ok, txt = same_tree(game, bb)
        check("восстановление по HTTPS (диапазоном)", ok, "скачано %.2f МБ" % (s3["downloaded"] / 1e6))
        print("== v3: удаление ненужного (на сервере только текущая и прошлая)")
        R.build_release(st, "alpha", "alpha-1.0.2-10.2026", ba, None)
        check("выкладка v3", up() == 0)
        want = set(R.publish_set(st, "alpha", 2))
        have = set(x for x in sf.listing()[0] if not x.endswith(".tmp"))
        check("на сервере ровно набор текущей и прошлой версии", have == want,
              "лишних %d, нет %d" % (len(have - want), len(want - have)))
        print("== заголовки и запреты")
        import urllib.request
        import urllib.error
        anyp = sorted(x for x in want if x.startswith("packs/"))[0]
        req = urllib.request.Request("%s/%s" % (url, anyp), method="HEAD", headers={"User-Agent": "t"})
        r = urllib.request.urlopen(req, timeout=20)
        check("пачка отдаётся (HEAD 200)", r.status == 200, r.headers.get("Content-Type", ""))
        try:
            urllib.request.urlopen(urllib.request.Request("%s/%s.tmp" % (url, anyp), headers={"User-Agent": "t"}), timeout=20)
            check(".tmp не отдаётся", False)
        except urllib.error.HTTPError as e:
            check(".tmp не отдаётся", e.code == 404, "HTTP %d" % e.code)
        expect_fail("http:// к боевому адресу запрещён клиентом", lambda: C.HttpSource("http://@@ASTORY_UPDATE_HOST@@"), "https")
    finally:
        R.resolve_game_root = orig
        try:                                             # очистка /www/_selftest на сервере
            sf = R.Sftp(R.UPLOAD_HOST, R.UPLOAD_USER, R.UPLOAD_KEY, root_remote)
            files, odd = sf.listing()
            cmds = ["-rm %s/%s" % (root_remote, x) for x in list(files) + odd]
            out = sf.run(["-ls -ln %s/objects" % root_remote], check=False)   # подкаталоги objects/<hh> (и опустевшие)
            subs = ["objects/" + l.split()[-1].rsplit("/", 1)[-1] for l in out.splitlines() if l.startswith("d")]
            cmds += ["-rmdir %s/%s" % (root_remote, d) for d in subs]
            cmds += ["-rmdir %s/%s" % (root_remote, d) for d in ("objects", "packs", "manifests", "channels", "keys")]
            cmds += ["-rmdir %s/.upload-lock" % root_remote, "-rmdir %s" % root_remote]
            sf.run(cmds, check=False)
            left = sf.run(["-ls -la %s" % root_remote], check=False)
            print("   /www/_selftest на сервере очищен (%d файлов)%s" % (len(files), "" if "not found" in left else " — ОСТАЛОСЬ: " + left[-200:]))
        except Exception as e:
            print("   ! очистка сервера не удалась: %s" % e)
    bad = [n for n, ok in RESULTS if not ok]
    print("ИТОГ: проверок %d, провалов %d %s" % (len(RESULTS), len(bad), bad))
    return 1 if bad else 0


def main(argv=None):
    """Точка входа."""
    ap = argparse.ArgumentParser(description="Сквозной самотест обновлений")
    ap.add_argument("--unit", action="store_true", help="синтетика, без настоящих сборок")
    ap.add_argument("--server", action="store_true", help="выкладка синтетики на @@ASTORY_UPDATE_HOST@@/_selftest + клиент по HTTPS")
    ap.add_argument("--build-a")
    ap.add_argument("--build-b")
    ap.add_argument("--work", help="рабочая папка (по умолчанию — новая во временной; существующую без маркера не трогаем)")
    ap.add_argument("--keep", action="store_true", help="не удалять рабочую папку")
    ap.add_argument("--client", help="C++-клиент AsdaLauncherCli.exe вместо эталона на Python")
    a = ap.parse_args(argv)
    global CL
    if a.client:
        if not os.path.isfile(a.client):
            ap.error("нет %s" % a.client)
        CL = ExeClient(os.path.abspath(a.client))
        print("клиент: %s" % CL.exe)
    if not a.unit and not a.server and not (a.build_a and a.build_b):
        ap.error("нужно --unit или --build-a и --build-b")
    if a.work:
        work = os.path.abspath(a.work)
        if os.path.exists(work):
            if not os.path.isfile(os.path.join(work, WORK_MARKER)):
                ap.error("%s существует и это не папка самотеста (нет %s) — не трогаю" % (work, WORK_MARKER))
            shutil.rmtree(work)
    else:
        work = tempfile.mkdtemp(prefix="asda_upd_selftest_",
                                dir=None if (a.unit or a.server) else os.path.dirname(os.path.dirname(HERE)))   # <ROOT>
    os.makedirs(work, exist_ok=True)
    open(os.path.join(work, WORK_MARKER), "w").close()
    try:
        return run_server(work) if a.server else run(a.build_a, a.build_b, work, a.unit)
    finally:
        if not a.keep and os.path.isfile(os.path.join(work, WORK_MARKER)):
            shutil.rmtree(work, ignore_errors=True)
        if a.client:                                     # счётчики отката тестового C++-клиента в HKCU
            subprocess.run(["reg", "delete", r"HKCU\Software\AStoryRemakeTest", "/f"], capture_output=True)


if __name__ == "__main__":
    sys.exit(main())
