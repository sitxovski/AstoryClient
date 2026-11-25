# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_server_list_dt.py — DT_ServerList: активный login-сервер (группа/IP/порт/имя мира).
# Источник: оригинальный system/OPset.bin (эталон CServerList::Load @0x00981f10).
# ★ ФОРМАТ (verified байтами): ВЕСЬ файл кодирован +0x64 (декод: byte-0x64 по модулю 256);
#   после декода — блоки по 0x100 с C-строками:
#     +0x004 group   (напр. "ME")
#     +0x104 IP      (напр. "192.168.10.70")
#     +0x204 port    СТРОКОЙ (atoi), напр. "15001"
#     +0x304 name    (напр. "Arab_Closed")
#   Минимальный валидный размер — 0x308.
# ★ ЗАЧЕМ: рантайм не имеет права лезть в каталог оригинальной игры (у игрока его нет), а .bin в
#   Content/ кукер в пак не пакует. Разбираем ОДИН РАЗ здесь → ассет /Game/AsdaAssets/Data/DT_ServerList.
#   ★ 16.10.2026: IP из таблицы игра НЕ использует — хост всегда домен @@ASTORY_AUTH_HOST@@ (Login/AsdaServerAddress.h);
#   из таблицы берётся порт. IP оставлен в данных как есть (импорт оригинала 1:1).
# RowStruct = FAsdaServerListRow (Source/AsdaReborn/Public/Login/AsdaLoginData.h) — ТРЕБУЕТ сборки C++.
# Выход: RawAssets/Data/ServerList.csv (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_ServerList
# Запуск: Tools/run_headless.bat Tools\asda_build_server_list_dt.py
#   (путь к OPset.bin — переменная SRC ниже; это ЕДИНСТВЕННОЕ место, где он упоминается).
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

import unreal
import os as _os_dtc  # noqa: E402  ★ дедуп №2 В2 Т4: общий шаг «заливка → сохранение» DataTable
import sys as _sys_dtc  # noqa: E402
_sys_dtc.path.insert(0, _os_dtc.path.dirname(_os_dtc.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402

# Кандидаты источника (первый существующий). Каталог оригинала — только тут, на этапе импорта.
SRC_CANDIDATES = [
    _P.OPSET_BIN,
    _P.OPSET_BIN_ORIG,
]
CSV = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "ServerList.csv")
OUT_PKG, OUT_NAME = "/Game/AsdaAssets/Data", "DT_ServerList"
ROW_STRUCT = "/Script/AsdaReborn.AsdaServerListRow"


def log(m):
    unreal.log_warning("[serverlist-dt] %s" % m)


def read_entry(path):
    b = open(path, "rb").read()
    assert len(b) >= 0x308, "OPset.bin короче 0x308: %d" % len(b)
    d = bytes(((x - 0x64) & 0xFF) for x in b)   # декод −0x64 по всему файлу

    def s(off, maxlen=64):
        end = d.find(b"\0", off, off + maxlen)
        return d[off:end if end >= 0 else off + maxlen].decode("latin1")

    port = s(0x204)
    return {
        "Group": s(0x004),
        "IP": s(0x104),
        "Port": int(port) if port.strip().isdigit() else 0,
        "Name": s(0x304),
    }


def main():
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): пишет ТОЛЬКО /Game/AsdaAssets/Data/DT_ServerList (его грузит
    #   лишь AsdaLoginData.cpp) и промежуточный RawAssets/Data/ServerList.csv (в Tools/Source/Config больше не упоминается).
    #   Запуск только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1 (иначе стоп, код 3).
    from asda_login_guard import forbid_login_write
    forbid_login_write("asda_build_server_list_dt.py", "Data/DT_ServerList (разбор OPset.bin, ServerList.csv, заполнение и сохранение DT)")
    src = next((p for p in SRC_CANDIDATES if os.path.isfile(p)), None)
    if not src:
        log("!! OPset.bin не найден ни по одному пути: %s" % SRC_CANDIDATES)
        return
    e = read_entry(src)
    log("источник %s → group='%s' ip=%s port=%d name='%s'" % (src, e["Group"], e["IP"], e["Port"], e["Name"]))
    if e["Port"] <= 0:
        log("!! порт не распарсен — DT не создан")
        return

    csv = ("_row,Group,IP,Port,Name\n"
           '1,"%s","%s",%d,"%s"\n' % (e["Group"], e["IP"], e["Port"], e["Name"]))
    with open(CSV, "w", encoding="utf-8", newline="") as f:
        f.write(csv)

    rs = unreal.load_object(None, ROW_STRUCT)
    if not rs:
        log("!! RowStruct %s НЕ НАЙДЕН — нужна СБОРКА C++ (FAsdaServerListRow в "
            "Public/Login/AsdaLoginData.h ещё не скомпилирован). DT не создан." % ROW_STRUCT)
        return

    full = "%s/%s" % (OUT_PKG, OUT_NAME)
    dt = _DTC.open_or_create_dt(unreal, OUT_PKG, OUT_NAME, rs)  # дедуп №2 В2 Т4: общий «открыть-или-создать»
    if not dt:
        log("!! DataTable не создан")
        return
    problems = _DTC.fill_and_save(unreal, dt, csv, log, fmt="csv")  # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т4: провал заливки → DT не сохраняется; маркер ASDA_WRITES_ASSET
    log("DT_ServerList: строк=%d, заполнение=%s" % (len(dt.get_row_names()), problems))


if __name__ == "__main__":
    main()
