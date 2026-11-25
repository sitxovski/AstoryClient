# Copyright (c) 2026 @sitxovski (Discord)
# gen_server_anim_tables.py — СЕРВЕРНЫЕ ТАБЛИЦЫ ДЛИТЕЛЬНОСТЕЙ АТАКИ → C++ (`AsdaServerAnimTables.inl`). [ПОРТ]
#
# ЗАЧЕМ (15.10.2026, ритм атаки, отчёт `Reports/anticheat/wave_PORT_attack_rhythm.md`). NpcServer принимает
#   обычный удар 4028, только если от прошлого принятого удара прошла серверная длительность. Её база — две таблицы
#   NpcServer, которых в данных клиента НЕТ (поиск по порту и `_re`: 0 файлов):
#     ./Ani/Combo.lst — 504 б: i16[2][18][7] (строка a = CUser+0x7888, тип оружия wt = ItemSubType, 6 шагов + число шагов);
#                       читатель `sub_401900@0x401900` = word_786BA8[126·a + 7·wt + шаг], число шагов word_786BB4[...].
#     ./Ani/Skill.lst — 2400 б: i32[600] (длительность навыка, мс); читатель SkillStart `sub_418670` (NpcServer.exe.c:17114:
#                       `word_786DA0[2 * skill]` = i32 по коду навыка). Массив сервера — 1200 dword (0x786DA0…0x788060),
#                       файл заполняет первые 600, хвост 600…1199 обнуляет `sub_44FF20`.
#   Клиент обязан считать длительность ТАК ЖЕ, как сервер, поэтому таблицы переносятся байт в байт, с md5 источника.
#
# ЗАПУСК: python Tools/gen_server_anim_tables.py [--src <папка Ani>] [--out <путь .inl>] [--check]
#   --src   по умолчанию — dev-сервер `//@@ASTORY_SERVER_HOSTNAME@@/h(@@ASTORY_SERVER_HOSTNAME@@_ns)/ASDA/Working_ASDA/v2/NpcServer/Ani` (ТОЛЬКО ЧТЕНИЕ)
#   --out   по умолчанию — Source/AsdaReborn/Private/World/AsdaServerAnimTables.inl
#   --check только сверить существующий .inl с источником (код 0 — совпало, 1 — нет)
#
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): работа — только в main() под __main__.
import argparse
import hashlib
import io
import os
import struct
import sys

DEFAULT_SRC = "//@@ASTORY_SERVER_HOSTNAME@@/h(@@ASTORY_SERVER_HOSTNAME@@_ns)/ASDA/Working_ASDA/v2/NpcServer/Ani"
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(TOOLS_DIR, "..", "Source", "AsdaReborn", "Private", "World", "AsdaServerAnimTables.inl")
COMBO_SIZE = 504          # i16[2][18][7]
SKILL_SIZE = 2400         # i32[600]


def build(src):
    """Текст .inl из двух файлов сервера (исключение — размер не тот)."""
    combo = open(os.path.join(src, "Combo.lst"), "rb").read()
    skill = open(os.path.join(src, "Skill.lst"), "rb").read()
    if len(combo) != COMBO_SIZE or len(skill) != SKILL_SIZE:
        raise ValueError("размеры: Combo.lst %d (ждём %d), Skill.lst %d (ждём %d)" % (len(combo), COMBO_SIZE, len(skill), SKILL_SIZE))
    cw = struct.unpack("<252h", combo)
    sv = struct.unpack("<600i", skill)
    out = io.StringIO()
    out.write("// АВТОГЕН `Tools/gen_server_anim_tables.py` — НЕ ПРАВИТЬ РУКАМИ. [ПОРТ]\n")
    out.write("// Источник: NpcServer ./Ani/Combo.lst md5 %s (%d б), ./Ani/Skill.lst md5 %s (%d б).\n"
              % (hashlib.md5(combo).hexdigest(), len(combo), hashlib.md5(skill).hexdigest(), len(skill)))
    out.write("// Combo.lst: i16[строка a][тип оружия wt][7] — шесть длительностей шага, мс, и число шагов (последний столбец).\n")
    out.write("static constexpr int16 kAsdaServerCombo[2][18][7] = {\n")
    for a in range(2):
        out.write("\t{\n")
        for wt in range(18):
            row = cw[126 * a + 7 * wt: 126 * a + 7 * wt + 7]
            out.write("\t\t{ %s },   // wt %d\n" % (", ".join("%d" % v for v in row), wt))
        out.write("\t},\n")
    out.write("};\n")
    nz = [(i, v) for i, v in enumerate(sv) if v != 0]
    out.write("// Skill.lst: ненулевые записи (код навыка → мс); прочие коды 0…1199 = 0 (хвост массива сервер обнуляет).\n")
    out.write("static constexpr int32 kAsdaServerSkillLockCount = %d;\n" % len(nz))
    out.write("static constexpr int32 kAsdaServerSkillLock[%d][2] = {\n" % len(nz))
    for i in range(0, len(nz), 6):
        out.write("\t" + " ".join("{ %d, %d }," % p for p in nz[i:i + 6]) + "\n")
    out.write("};\n")
    out.write("static constexpr const TCHAR* kAsdaServerAnimTablesMd5 = TEXT(\"Combo %s / Skill %s\");\n"
              % (hashlib.md5(combo).hexdigest()[:8], hashlib.md5(skill).hexdigest()[:8]))
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser(description="Combo.lst/Skill.lst NpcServer -> AsdaServerAnimTables.inl")
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    text = build(a.src)
    data = ("﻿" + text).encode("utf-8")          # UTF-8 с BOM, как остальные исходники порта
    if a.check:
        cur = open(a.out, "rb").read() if os.path.exists(a.out) else b""
        same = cur.replace(b"\r\n", b"\n") == data.replace(b"\r\n", b"\n")
        print("СОВПАДАЕТ" if same else "РАСХОЖДЕНИЕ", a.out)
        return 0 if same else 1
    with open(a.out, "wb") as f:
        f.write(data.replace(b"\n", b"\r\n"))
    print("записан", os.path.normpath(a.out), len(data), "б")
    return 0


if __name__ == "__main__":
    sys.exit(main())
