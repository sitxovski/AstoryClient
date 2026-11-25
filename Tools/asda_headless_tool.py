# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_headless_tool.py — ★ КАРКАС HEADLESS-ИНСТРУМЕНТА (волна 3 п.15 плана дублей, 15.09.2026):
#   защита света владельца в Login.umap + резервная копия файла до правки + маркер успеха для run_headless_safe.
#
# ★★★ Защита логина НЕ ослабляется: forbid_login_write() здесь — тонкая обёртка над
#   `asda_login_guard.forbid_login_write` (единственная реализация правила), имя инструмента берётся из __file__.
#   Место вызова в инструменте остаётся тем же — первой строкой main() ДО загрузки уровня, и имя функции
#   в инструменте то же (`forbid_login_write(`), чтобы поиск мест защиты по тексту их находил.
#
# Подключение (UE при запуске файла НЕ добавляет его папку в sys.path — поэтому две строки перед импортом):
#     import sys as _sys, os as _os
#     _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
#     from asda_headless_tool import forbid_login_write
#     forbid_login_write(__file__)
#
# Закон tools-no-import-side-effects: при импорте модуль ничего не делает.
import datetime
import hashlib
import os
import shutil

import asda_login_guard as _LG

# Корень проекта порта (справочно; резервные копии от него больше НЕ считаются — Т3, 11.10.2026)
PROJECT_ROOT = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient"
# ★ Т3 (11.10.2026, решение владельца): ЕДИНЫЙ корень резервных копий; AstoryClient\_backup для новых копий не используется
BACKUP_ROOT = r"@@ASTORY_DEV_ROOT@@\_backup"
# Корень, от которого считается путь файла внутри набора копий (как у всех наборов в @@ASTORY_DEV_ROOT@@\_backup)
AREA_ROOT = r"@@ASTORY_DEV_ROOT@@"
# Подпапка набора для файлов вне @@ASTORY_DEV_ROOT@@: `_outside\<диск>\<путь>` — путь однозначен
OUTSIDE_DIR = "_outside"
# Имя переменной окружения явного разрешения владельца на запись в Login.umap (из asda_login_guard)
ALLOW_ENV = _LG.ALLOW_ENV


def forbid_login_write(tool_file):
    """Остановить инструмент, если запись в Login.umap не разрешена владельцем явно (SystemExit(3)).
    tool_file — __file__ инструмента: в сообщение идёт имя файла, как в прежних вызовах с литералом."""
    _LG.forbid_login_write(os.path.basename(tool_file))


def login_write_allowed():
    """True, если владелец явно разрешил запись в Login.umap (для обходчиков всех уровней: логин — пропустить)."""
    return _LG.login_write_allowed()


def md5_file(path):
    """md5 содержимого файла (hex, чтение блоками по 1 МБ) — для сверки копии в режиме verify="md5".
    Одна реализация (дедуп №2 В1 Т19, 11.10.2026): ею же пользуются asda_apply_fx_light_defaults.md5 и asda_bloom_rollout.md5."""
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


_md5 = md5_file   # прежнее внутреннее имя (зовут _verify_copy/_copy_checked и проверки снимков)


def backup_rel_path(path, area_root=AREA_ROOT):
    r"""Путь файла внутри набора копий: относительно @@ASTORY_DEV_ROOT@@; файл вне его — `_outside\<диск>\<путь>`
    (однозначно: два файла с одним именем из разных мест не сольются в одну копию, как было с basename)."""
    ap = os.path.abspath(path)
    try:
        rel = os.path.relpath(ap, os.path.abspath(area_root))
    except ValueError:                        # другой диск — relpath невозможен
        rel = ".."
    if rel.startswith(".."):
        drive, tail = os.path.splitdrive(ap)
        rel = os.path.join(OUTSIDE_DIR, drive.replace(":", "").strip("\\/") or "_", tail.lstrip("\\/"))
    return rel


def _verify_copy(src, dst, verify):
    """Сверка свежей копии с исходником: размер всегда, md5 — при verify="md5". Возвращает текст расхождения или ''."""
    if os.path.getsize(dst) != os.path.getsize(src):
        return "размер %d ≠ %d" % (os.path.getsize(dst), os.path.getsize(src))
    if verify == "md5" and _md5(dst) != _md5(src):
        return "md5 копии ≠ md5 исходника"
    return ""


def _copy_checked(src, dst, verify):
    """Копия через временный `<dst>.part` + сверка + переименование: недописанная копия не может остаться под именем
    настоящей (иначе следующий запуск принял бы её за «исходник до первой правки»)."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    part = dst + ".part"
    shutil.copy2(src, part)
    why = _verify_copy(src, part, verify)
    if why:
        try:
            os.remove(part)
        except OSError:
            pass
        raise RuntimeError("резервная копия %s не прошла сверку: %s — запись запрещена" % (dst, why))
    os.replace(part, dst)


def backup_file(path, set_name, date=None, root=BACKUP_ROOT, verify="size", reuse=True, set_dir=None, log=None):
    r"""★ ЕДИНАЯ резервная копия файла ДО правки (закон backup-before-edit; Т3 волны В0, 11.10.2026).

    Куда: `<root>\<дата>_<set_name>\<путь относительно @@ASTORY_DEV_ROOT@@>` (файл вне @@ASTORY_DEV_ROOT@@ — `_outside\<диск>\<путь>`).
      root     — корень наборов, по умолчанию единый `@@ASTORY_DEV_ROOT@@\_backup` (решение владельца; `AstoryClient\_backup`
                 для новых копий не используется). ⚠ До 11.10 параметр значил КОРЕНЬ ПРОЕКТА (копия шла в `<root>/_backup/…`);
                 вызывающих с этим параметром не было.
      date     — дата набора `ГГГГ-ММ-ДД`; None — день запуска.
      set_dir  — готовая папка набора целиком (переопределение из переменной окружения шага); тогда root/date/set_name
                 в путь не входят.
      verify   — "size": сверка размера новой копии; "md5": ещё и md5 содержимого.
      reuse    — True (по умолчанию): копия уже есть — это исходник ДО ПЕРВОЙ правки, она НЕ перезаписывается и
                 исключения нет; False: старая копия тоже не трогается, но рядом снимается ещё одна —
                 `<копия>.<ГГГГММДД_ЧЧММСС>.bak` (снимок состояния перед этим запуском).
      log      — функция строки журнала (None — print).
    Правило повторного запуска: копия есть → проверка, что она непустая (пустая при непустом файле — исключение:
      такой бэкап не годится); файл отличается от копии (размер/время, при verify="md5" — md5) — строка
      «файл уже изменён после бэкапа, исходная копия сохранена: <путь>», запись НЕ запрещается.
      ★ 11.10.2026 (ревизия wave0_REVIEW.md, С3): при отличии ТЕКУЩЕЕ состояние снимается ВСЕГДА (и при reuse=True) —
      `<копия>.<ГГГГММДД_ЧЧММСС>.bak`. Сценарий: шаг утром → владелец правит файл днём → шаг вечером перезаписывает;
      без снимка правка владельца не попадала ни в один бэкап (закон owner-asset-edits-never-lose). Исходная копия не трогается.
    Возврат: путь копии; если снят снимок (отличие или reuse=False) — путь снимка; None — исходного файла нет."""
    if verify not in ("size", "md5"):
        raise ValueError("backup_file: verify=%r — допустимо 'size' или 'md5'" % (verify,))
    say = log or print
    if not os.path.isfile(path):
        return None
    if set_dir:
        base = set_dir
    else:
        day = date or datetime.date.today().strftime("%Y-%m-%d")
        base = os.path.join(root, "%s_%s" % (day, set_name))
    dst = os.path.join(base, backup_rel_path(path))
    if not os.path.isfile(dst):
        _copy_checked(path, dst, verify)
        say("бэкап создан: %s (%d б, сверка %s)" % (dst, os.path.getsize(dst), verify))
        return dst
    # Копия уже есть — это состояние ДО ПЕРВОЙ правки набора: не перезаписываем.
    if os.path.getsize(dst) == 0 and os.path.getsize(path) > 0:
        raise RuntimeError("резервная копия %s пустая, а файл %d б — бэкап негоден, запись запрещена"
                           % (dst, os.path.getsize(path)))
    if verify == "md5":
        differs = _md5(dst) != _md5(path)
    else:
        differs = (os.path.getsize(dst) != os.path.getsize(path)
                   or abs(os.path.getmtime(dst) - os.path.getmtime(path)) > 2.0)
    if differs:
        say("файл уже изменён после бэкапа, исходная копия сохранена: %s" % dst)
    else:
        say("бэкап на месте: %s (%d б)" % (dst, os.path.getsize(dst)))
    if reuse and not differs:       # ★ С3: отличие → снимок текущего состояния обязателен (ниже)
        return dst
    if differs:   # ★ С3: то же отличающееся состояние уже снято прошлым запуском — второй снимок не плодим
        import glob as _glob
        prev = sorted(_glob.glob(glob_escape(dst) + ".*.bak"), key=os.path.getmtime)
        if prev and os.path.getsize(prev[-1]) == os.path.getsize(path) and _md5(prev[-1]) == _md5(path):
            say("снимок этого состояния уже есть: %s" % prev[-1])
            return prev[-1]
    snap = dst + datetime.datetime.now().strftime(".%Y%m%d_%H%M%S.bak")
    n = 1
    while os.path.exists(snap):                # два запуска в одну секунду — не затираем снимок
        snap = dst + datetime.datetime.now().strftime(".%Y%m%d_%H%M%S") + "_%d.bak" % n
        n += 1
    _copy_checked(path, snap, verify)
    say("снимок перед запуском: %s (%d б, сверка %s)" % (snap, os.path.getsize(snap), verify))
    return snap


def glob_escape(path):
    """Путь для glob с экранированными [ ] * ? (в путях ассетов бывают квадратные скобки)."""
    import glob as _glob
    return _glob.escape(path)


def marker(text):
    """Маркер успеха/этапа в лог UE (для `run_headless_safe.py --marker`); вне UE — в stdout.
    ASCII-маркер надёжнее: кириллица в аргументах командной строки бывает испорчена."""
    try:
        import unreal
        unreal.log(text)
    except Exception:
        print(text)


def run(main, tool_file, marker_ok, writes_login=False):
    """Запуск тела инструмента по каркасу: [защита логина] → main() → маркер успеха.
    Маркер печатается ТОЛЬКО если main() дошла до конца без исключения — вердикт run_headless_safe по маркеру
    тогда честный. writes_login=True — инструмент загружает/сохраняет /Game/Map/Login."""
    if writes_login:
        forbid_login_write(tool_file)
    result = main()
    marker(marker_ok)
    return result
