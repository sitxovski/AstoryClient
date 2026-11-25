# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_startup_ad_dt.py — DT_StartupAd: настройки СТАРТОВОГО окна новостей `AD_Win` (5000).
#
# ИСТОЧНИК: оригинальный `system/AD.bin` эталонного клиента.
#
# ★ ФОРМАТ (реверс `StartupAd_LoadConfig@0xF01BB0` / `StartupAd_SaveConfig@0xF01DC0`, проверен
#   байтами файла): это НЕ таблица `Bin.bbz` и вообще не бинарная запись, а ПЛОСКИЙ ТЕКСТ —
#   пять десятичных чисел, каждое своей строкой:
#       [0] признак показа   0 = окно не показывать вовсе
#       [1] число картинок   листалка `start_ad_img00`…`start_ad_img0<N-1>`
#       [2] год   \
#       [3] месяц  |  дата последнего нажатия «не показывать сегодня»
#       [4] день  /
#   Чтение: `fopen(...,"rt")` + 5 × (`fgets(buf,256)` + `sscanf(buf,"%d")`).
#   Запись: `fopen(...,"wt")` + 5 × `sprintf("%d\n")` + `fwrite` — из-за ТЕКСТОВОГО режима каждый
#   `\n` на Windows становится `\r\n`, поэтому эталонный файл занимает ровно 20 байт:
#       30 0D 0A 35 0D 0A 32 30 30 38 0D 0A 31 30 0D 0A 33 31 0D 0A   = "0\r\n5\r\n2008\r\n10\r\n31\r\n"
#   То есть эталонное содержимое = (0, 5, 2008, 10, 31).
#
# ★ ПОЧЕМУ НЕ В `Tools/decode_item_db.py`. Тот реестр описывает таблицы `Bin.bbz` моделью
#   «header 20 б + count × stride + пофилдовые смещения»; текстовый конфиг из пяти строк в неё
#   не ложится ни одним полем (`stride`/`cols`/`md5` бессмысленны). Формат задокументирован здесь
#   и в шапке `Public/UI/AsdaStartupAdSubsystem.h`; в реестр внесена ссылка-заметка.
#
# ★ ЗАЧЕМ ТАБЛИЦА. Рантайм не имеет права лезть в каталог оригинальной игры (у игрока его нет),
#   а `.bin` в `Content/` кукер в пак не пакует — то же правило, что у `DT_ServerList`.
#   ВАЖНО: `DT_StartupAd` — это ШТАТНОЕ (заводское) состояние. Живое состояние игрок пишет сам:
#   подсистема кладёт `Saved/AD.bin` в ТОМ ЖЕ текстовом формате, и он имеет приоритет над таблицей.
#
# RowStruct = FAsdaStartupAdRow (Source/AsdaReborn/Public/UI/AsdaStartupAdSubsystem.h) — ТРЕБУЕТ сборки C++.
# Выход: RawAssets/Data/AD.bin (копия источника) + RawAssets/Data/StartupAd.csv (ВНЕ Content!)
#        + /Game/AsdaAssets/Data/DT_StartupAd
# Запуск: Tools/run_headless.bat Tools\asda_build_startup_ad_dt.py
#   ⚠ Без редактора (просто `python asda_build_startup_ad_dt.py`) скрипт сделает только копию
#     источника и CSV, а DataTable не создаст — и честно об этом скажет.
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402
import shutil
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

try:
    import unreal
except ImportError:                     # запуск обычным python — только CSV, без ассета
    unreal = None

# Кандидаты источника (первый существующий). Каталог оригинала — только тут, на этапе импорта.
SRC_CANDIDATES = [
    os.path.join(_P.SYSTEM, "AD.bin"),
    os.path.join(_P.SYSTEM_ORIG, "AD.bin"),
]
RAW_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data")
RAW_BIN = os.path.join(RAW_DIR, "AD.bin")
CSV = os.path.join(RAW_DIR, "StartupAd.csv")
OUT_PKG, OUT_NAME = "/Game/AsdaAssets/Data", "DT_StartupAd"
ROW_STRUCT = "/Script/AsdaReborn.AsdaStartupAdRow"

FIELDS = ["Enabled", "ImageCount", "SuppressYear", "SuppressMonth", "SuppressDay"]


def log(m):
    if unreal:
        unreal.log_warning("[startupad-dt] %s" % m)
    else:
        # ★ Консоль Windows бывает в cp1251/cp866 — там нет ни «→», ни «★». Печатаем «как получится»
        #   (незаменимое → «?»), иначе скрипт падал бы на собственном сообщении.
        try:
            print("[startupad-dt] %s" % m)
        except UnicodeEncodeError:
            enc = (_sys.stdout.encoding or "ascii")
            print(("[startupad-dt] %s" % m).encode(enc, "replace").decode(enc))


def read_ad_bin(path):
    """Разбор `AD.bin` 1:1 с эталоном: пять строк, из каждой `sscanf("%d")`.

    Возвращает список из ПЯТИ int. Недостающие строки остаются нулями — ровно как в оригинале,
    где `sscanf` при неудаче не трогает целевую переменную, а та обнулена конструктором окна.
    """
    with open(path, "rb") as f:
        raw = f.read()
    vals = [0, 0, 0, 0, 0]
    lines = raw.decode("latin1").replace("\r\n", "\n").replace("\r", "\n").split("\n")
    for i in range(5):
        if i >= len(lines):
            break
        s = lines[i].strip()
        if not s:
            continue
        # `sscanf("%d")`: необязательный знак + цифры, мусор после числа игнорируется.
        j, neg = 0, False
        if j < len(s) and s[j] in "+-":
            neg = (s[j] == "-")
            j += 1
        d = ""
        # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т7: только ASCII-цифры, как у CRT (str.isdigit() принимал «²», «³», «¹» из latin-1,
        #   и int() затем падал). На текущем AD.bin итог тот же.
        while j < len(s) and "0" <= s[j] <= "9":
            d += s[j]
            j += 1
        if d:
            vals[i] = -int(d) if neg else int(d)
    return vals


def main():
    src = next((p for p in SRC_CANDIDATES if os.path.isfile(p)), None)
    if not src:
        log("!! AD.bin не найден ни по одному пути: %s" % SRC_CANDIDATES)
        return
    vals = read_ad_bin(src)
    log("источник %s (%d б) → %s" % (src, os.path.getsize(src),
                                     ", ".join("%s=%d" % (n, v) for n, v in zip(FIELDS, vals))))
    if vals[0] == 0:
        log("признак показа = 0 — в эталонном клиенте стартовое окно новостей НЕ показывается; "
            "переносим значение как есть (закон never-invent-only-original)")

    os.makedirs(RAW_DIR, exist_ok=True)
    shutil.copyfile(src, RAW_BIN)       # копия источника рядом с CSV — чтобы разбор был перепроверяем

    csv = "_row,%s\n1,%s\n" % (",".join(FIELDS), ",".join(str(v) for v in vals))
    with open(CSV, "w", encoding="utf-8", newline="") as f:
        f.write(csv)
    log("CSV записан: %s" % CSV)

    if not unreal:
        log("редактор не подключён (обычный python) — DataTable не создан. "
            "Повторить через Tools/run_headless.bat после сборки C++.")
        return

    rs = unreal.load_object(None, ROW_STRUCT)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (FAsdaStartupAdRow в "
            "Public/UI/AsdaStartupAdSubsystem.h ещё не скомпилирован). DT не создан." % ROW_STRUCT)
        return

    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, OUT_NAME, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable не создан")
        return
    problems = _DTC.fill_and_save(unreal, dt, csv, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_StartupAd: строк=%d, заполнение=%s" % (len(dt.get_row_names()), problems))


if __name__ == "__main__":
    main()
