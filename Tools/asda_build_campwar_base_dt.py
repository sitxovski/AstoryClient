# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_campwar_base_dt.py — DT_CampWarBase ← Data/CampWar_BaseTable.BIN («Битва армий», 30.10.2026).
#
# ИСТОЧНИК (вещественное доказательство, скопирован из дистрибутива Donors\AsdaChroniclesInterlude\Data):
#   RawAssets/Data/CampWar_BaseTable.BIN — 2576 б, сигнатура "BGT", заголовок 20 б, 3 записи по 852 б (MD5-16 + тело 836 б).
#
# РАСКЛАДКА (загрузчик `CMap_LoadCampWarBaseTable@0xC9F670`, разбор `Reports/faction_select/REG_SPEC.md` §6):
#   заголовок: +0 float 100.0, +4 u32 число записей, +8 0, +0x0C "BGT\0", +0x10 0;
#   тело 836 б, шифр: каждый dword −43, каждый символ строки −1 до NUL:
#     @0..@24 7×dword; @28 12×char[32]; @412 12×char[32] (★ эталон берёт длину раскодирования у ПАРНОЙ строки @28 — дефект,
#     на этих данных результат одинаков); @796 dword; @800 / @812 / @824 по 3 dword.
#   MD5 записей здесь НЕ проверяется (соль `g_NetMd5Salt` в скрипт не перенесена) — тело берётся как есть.
#   Порт читает из таблицы `@16` (знаменатель «N/макс» окна 47000 и «загруженности» строк списка битв); прочие поля
#   переносятся ВСЕ (закон no-scope-cutting).
#
# RowStruct = FAsdaCampWarBaseRow (Source/AsdaReborn/Public/UI/AsdaCampWarSubsystem.h) — ТРЕБУЕТ сборки C++ до запуска.
# Выход: RawAssets/Data/CampWarBase.json (ВНЕ Content!) + /Game/AsdaAssets/Data/DT_CampWarBase (RowName = номер записи).
# Запуск (в редакторе):  python Tools/run_headless_safe.py Tools/asda_build_campwar_base_dt.py --marker ЗАВЕРШЕНО
# Запуск (только JSON):  python Tools/asda_build_campwar_base_dt.py
# ★ ПРАВИЛО tools-no-import-side-effects: НИКАКИХ действий при импорте модуля — только через main().
import json
import os
import struct
import sys as _sys_dtc

_sys_dtc.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_dt_common as _DTC  # noqa: E402  общий шаг «заливка → сохранение» DataTable

try:
    import unreal
except ImportError:      # запуск обычным python — штатный режим «только JSON»
    unreal = None

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "RawAssets", "Data", "CampWar_BaseTable.BIN")
JSON_OUT = os.path.join(ROOT, "RawAssets", "Data", "CampWarBase.json")
OUT_PKG = "/Game/AsdaAssets/Data"
OUT_NAME = "DT_CampWarBase"
ROW_STRUCT = "/Script/AsdaReborn.AsdaCampWarBaseRow"
HDR = 20          # заголовок 20 б
BODY = 836        # тело записи
REC = 16 + BODY   # MD5-16 + тело
SIG = b"BGT"      # сигнатура по смещению +0x0C


def log(m):
    if unreal:
        unreal.log_warning("[campwar-base-dt] %s" % m)
        return
    enc = (getattr(_sys_dtc.stdout, "encoding", None) or "utf-8")
    line = "[campwar-base-dt] %s" % m
    print(line.encode(enc, errors="replace").decode(enc, errors="replace"))


def dw(b, off):
    """dword со сдвигом −43, как в загрузчике."""
    return struct.unpack_from("<i", b, off)[0] - 43


def dec_str(b, off, length_from):
    """char[32]: −1 к каждому символу на длину строки `length_from` (у @412 — длина парной @28, как эталон)."""
    s = bytearray(b[off:off + 32])
    src = b[length_from:length_from + 32]
    n = src.index(0) if 0 in src else 32
    for i in range(n):
        s[i] = (s[i] - 1) & 0xFF
    end = s.index(0) if 0 in s else 32
    return bytes(s[:end]).decode("cp1251", "replace")


def parse(path):
    """Разбор файла → список строк DataTable в порядке файла."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HDR or data[12:15] != SIG:
        log("файл не тот (сигнатура %r) — разбирать нечего" % data[12:15])
        return []
    count = struct.unpack_from("<I", data, 4)[0]
    if HDR + count * REC != len(data):
        log("размер %d ≠ ожидаемому %d (записей %d)" % (len(data), HDR + count * REC, count))
    rows = []
    for i in range(count):
        b = data[HDR + i * REC + 16: HDR + (i + 1) * REC]
        if len(b) < BODY:
            break
        rows.append({
            "Name": str(i),
            "Index": dw(b, 0), "Field04": dw(b, 4), "Field08": dw(b, 8), "Field0C": dw(b, 12),
            "Field10": dw(b, 16), "Field14": dw(b, 20), "Field18": dw(b, 24),
            "Times1": [dec_str(b, 28 + 32 * j, 28 + 32 * j) for j in range(12)],
            "Times2": [dec_str(b, 412 + 32 * j, 28 + 32 * j) for j in range(12)],
            "Field31C": dw(b, 796),
            "Field320": [dw(b, 800 + 4 * k) for k in range(3)],
            "Field32C": [dw(b, 812 + 4 * k) for k in range(3)],
            "Field338": [dw(b, 824 + 4 * k) for k in range(3)],
        })
    return rows


def main():
    rows = parse(BIN)
    if not rows:
        log("записей нет — JSON и DT не тронуты")
        print("ЗАВЕРШЕНО")
        return
    for r in rows:
        log("строка %s: @0..@24 = %d %d %d %d %d %d %d, @796 = %d, @800 %s, @812 %s, @824 %s, времена %s / %s" % (
            r["Name"], r["Index"], r["Field04"], r["Field08"], r["Field0C"], r["Field10"], r["Field14"], r["Field18"],
            r["Field31C"], r["Field320"], r["Field32C"], r["Field338"], r["Times1"][:2], r["Times2"][:2]))
    text = json.dumps(rows, ensure_ascii=False, indent=1)
    with open(JSON_OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    log("JSON записан: %s (%d строк)" % (JSON_OUT, len(rows)))
    if not unreal:
        log("редактора нет — шаг «JSON → DataTable» пропущен (штатный режим «только JSON»)")
        print("ЗАВЕРШЕНО")
        return
    dt = _DTC.fill_dt(unreal, OUT_PKG, OUT_NAME, ROW_STRUCT, text, log, fmt="json",
                      missing_hint="Public/UI/AsdaCampWarSubsystem.h")
    log("%s: %s" % (OUT_NAME, "готово" if dt else "НЕ СОЗДАН"))
    print("ЗАВЕРШЕНО")


if __name__ == "__main__":
    main()
