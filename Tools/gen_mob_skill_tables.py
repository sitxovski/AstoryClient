# Copyright (c) 2026 @sitxovski (Discord)
# gen_mob_skill_tables.py — КЛИЕНТСКИЕ ТАБЛИЦЫ НАВЫКОВ МОБОВ → C++ (`AsdaMobSkillTables.inl`). [ПОРТ]
#
# ЗАЧЕМ (30.10.2026, система навыков мобов, отчёт `Reports/mob_skills/FLOW.md`). Автомат навыка моба оригинала
#   (пакеты 8011/8012) читает две таблицы клиента, которых в порту нет:
#     Data/Mob/MonsterSkillInfo.msif     — 101 б на запись; загрузчик npc_mob_pet.c:8255 (`self[2] = size / 0x65`),
#                                          читатель `sub_A526D0` (миснейм CMobManager_GetMobInfo): `a2 && a2 < count`
#                                          ⇒ навык 0 не играется никогда; байт +43 — «анимация» (клиент берёт `& 1`:
#                                          слот атаки 21 − (b & 1), звук 14 − (b & 1); entity_char.c:55856, :56287, :56329).
#     Data/Mob/MonsterSkillCondition.mst — 386 б на запись; загрузчик npc_mob_pet.c:8231 (`self[1] = size / 0x182`),
#                                          читатель `sub_F80690`: `a2 && a2 < count` ⇒ запись = mst[ent[117]] ПО ПОРЯДКУ, где
#                                          ent[117] = `mobTableFindIndexById@0xF707D0` = индекс записи Mob list = MobId − 1
#                                          (id записей Mob list и mst = индекс + 1 у всех; MobId 1 ⇒ индекс 0 ⇒ записи нет); dword +6 — добавка к таймауту фаз
#                                          (`gameSpawnTimedSlotEntry@0xF8C100`: entry+388 = (float)(mst+6 + 5000)).
#     Data/Mob/Mob list.BIN              — 0x110 б на запись (шапка 0x14, счётчик u32 @4), запись = ent[117] (MobId − 1). Слоты анимации
#                                          навыка 18/19/20/21 (файлы `_11/_12/_13/_14`, `Effect_BuildJbbSuffixByType@0xF717C0`)
#                                          загрузчик моба `mobEffect_LoadArchiveStep@0xF81B90` грузит ТОЛЬКО при условии:
#                                          18: cnt>6 || F0>0 || F2>0;  19: cnt>7 || F1>0 || F2>0;  20: cnt>8 || F2>0;
#                                          21: cnt>9 || F3>0  (cnt = i32 +0x9C, F[k] = float +0xAC+4k). Пустой слот = «клип
#                                          кончился сразу» (разбор `Reports/mob_skills/anim_slots.md` §1.4–1.5).
#   Третья таблица (Monster_SkillEffect.BIN) в порту уже есть — DT_MonsterSkillEffect (asda_build_monster_skilleffect_dt.py).
#   Источник — клиент-эталон Interlude (asda_paths.CLIENT_ORIG), md5 пишется в шапку .inl.
#
# ЗАПУСК: python Tools/gen_mob_skill_tables.py [--src <папка Data/Mob>] [--out <путь .inl>] [--check]
#   --check только сверить существующий .inl с источником (код 0 — совпало, 1 — нет)
#
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): работа — только в main() под __main__.
import argparse
import hashlib
import io
import os
import struct
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.join(TOOLS_DIR, "..", "Source", "AsdaReborn", "Private", "World", "AsdaMobSkillTables.inl")
# ★ 30.10.2026 второй выход: вариант модели «меш +0x04 + клипы базы +0x24» по типу моба.
CLIPBASE_OUT = os.path.join(TOOLS_DIR, "..", "Source", "AsdaReborn", "Private", "World", "AsdaMobClipBase.inl")
MSIF_REC = 101            # 0x65
MST_REC = 386             # 0x182
MOBLIST_HDR, MOBLIST_REC = 0x14, 0x110


def default_src():
    """Папка Data/Mob клиента-эталона (через asda_paths — единственный источник путей)."""
    sys.path.insert(0, TOOLS_DIR)
    import asda_paths  # noqa: E402
    return os.path.join(asda_paths.CLIENT_ORIG, "Data", "Mob")


def build(src):
    """Текст .inl из двух файлов клиента (исключение — размер не кратен записи)."""
    msif = open(os.path.join(src, "MonsterSkillInfo.msif"), "rb").read()
    mst = open(os.path.join(src, "MonsterSkillCondition.mst"), "rb").read()
    ml = open(os.path.join(src, "Mob list.BIN"), "rb").read()
    if len(msif) % MSIF_REC or len(mst) % MST_REC:
        raise ValueError("размеры: msif %d (не кратно %d), mst %d (не кратно %d)" % (len(msif), MSIF_REC, len(mst), MST_REC))
    n_msif = len(msif) // MSIF_REC
    n_mst = len(mst) // MST_REC
    anim = [msif[i * MSIF_REC + 43] for i in range(n_msif)]
    extra = [(i, struct.unpack_from("<i", mst, i * MST_REC + 6)[0]) for i in range(n_mst)]
    extra_nz = [(i, v) for (i, v) in extra if v != 0]
    n_ml = struct.unpack_from("<I", ml, 4)[0]
    if MOBLIST_HDR + n_ml * MOBLIST_REC > len(ml):
        raise ValueError("Mob list.BIN: счётчик %d не влезает в %d б" % (n_ml, len(ml)))
    masks = []
    for i in range(n_ml):
        base = MOBLIST_HDR + i * MOBLIST_REC
        cnt = struct.unpack_from("<i", ml, base + 0x9C)[0]
        f = struct.unpack_from("<4f", ml, base + 0xAC)
        m = (1 if (cnt > 6 or f[0] > 0 or f[2] > 0) else 0) | (2 if (cnt > 7 or f[1] > 0 or f[2] > 0) else 0)             | (4 if (cnt > 8 or f[2] > 0) else 0) | (8 if (cnt > 9 or f[3] > 0) else 0)
        if m:
            masks.append((i, m))
    o = io.StringIO()
    o.write("﻿// АВТОГЕН `Tools/gen_mob_skill_tables.py` — НЕ ПРАВИТЬ РУКАМИ. [ПОРТ]\n")
    o.write("// Источник: клиент Interlude Data/Mob/MonsterSkillInfo.msif md5 %s (%d б, %d записей), "
            "MonsterSkillCondition.mst md5 %s (%d б, %d записей).\n"
            % (hashlib.md5(msif).hexdigest(), len(msif), n_msif, hashlib.md5(mst).hexdigest(), len(mst), n_mst))
    o.write("// Число записей msif — граница `sub_A526D0` (навык годен при 0 < id < kAsdaMobSkillInfoCount).\n")
    o.write("static constexpr int32 kAsdaMobSkillInfoCount = %d;\n" % n_msif)
    o.write("// msif +43 «анимация» по индексу записи (клиент использует только младший бит).\n")
    o.write("static constexpr uint8 kAsdaMobSkillAnim[%d] = {\n" % n_msif)
    for i in range(0, n_msif, 16):
        o.write("\t" + ", ".join("%d" % b for b in anim[i:i + 16]) + ",   // %d..%d\n" % (i, min(i + 15, n_msif - 1)))
    o.write("};\n")
    o.write("// Число записей mst — граница `sub_F80690` (запись годна при 0 < ent[117] < kAsdaMobSkillCondCount; ent[117] = MobId − 1).\n")
    o.write("static constexpr int32 kAsdaMobSkillCondCount = %d;\n" % n_mst)
    o.write("// mst +6 (dword, мс) — добавка к таймауту фаз; только ненулевые (у остальных типов 0). {ent[117] = MobId − 1, мс}\n")
    o.write("static constexpr int32 kAsdaMobSkillCondExtra[%d][2] = {\n" % len(extra_nz))
    for (i, v) in extra_nz:
        o.write("\t{ %d, %d },\n" % (i, v))
    o.write("};\n")
    o.write("// Mob list.BIN md5 %s (%d записей): маска загруженных слотов навыка по записи (ent[117] = MobId − 1) — бит0 слот 18 (anim_11),\n"
            "//   бит1 19 (anim_12), бит2 20 (anim_13), бит3 21 (anim_14). Только ненулевые. {ent[117], маска}\n"
            % (hashlib.md5(ml).hexdigest(), n_ml))
    o.write("static constexpr int32 kAsdaMobSkillSlotMask[%d][2] = {\n" % len(masks))
    for (i, m) in masks:
        o.write("\t{ %d, %d },\n" % (i, m))
    o.write("};\n")
    return o.getvalue()


def build_clipbase(src):
    """Текст `AsdaMobClipBase.inl` (30.10.2026): {MobId, "<меш>__<база>"} для записей Mob list, где база клипов +0x24 ≠ меш +0x04.
    Оригинал: меш — `sub_F83080` (+0x04), клипы — `Effect_BuildJbbSuffixByType@0xF717C0` (+0x24); MobId = индекс + 1.
    Варианты собирает `asda_build_mob_fixed.py --clipbase`, импортирует `asda_import_mob_clipbase.py`."""
    ml = open(os.path.join(src, "Mob list.BIN"), "rb").read()
    n = struct.unpack_from("<I", ml, 4)[0]

    def cs(o, k):
        t = ml[o:o + k].split(bytes([0]))[0].decode("cp1251", "ignore")
        return t[:-4] if t.lower().endswith((".jbm", ".jbb")) else t
    rows = []
    for i in range(n):
        o = MOBLIST_HDR + i * MOBLIST_REC
        mesh, base = cs(o + 0x04, 0x20), cs(o + 0x24, 0x20)
        if mesh and base and mesh.lower() != base.lower():
            rows.append((i + 1, "%s__%s" % (mesh.lower(), base.lower())))
    o = io.StringIO()
    o.write("﻿// АВТОГЕН `Tools/gen_mob_skill_tables.py` — НЕ ПРАВИТЬ РУКАМИ. [ОРИГИНАЛ]\n")
    o.write("// Источник: клиент Interlude Data/Mob/Mob list.BIN md5 %s (%d записей). Тип моба (MobId) → скелетный вариант\n"
            "//   «меш +0x04 + клипы базы +0x24» (`asda_build_mob_fixed.py --clipbase`); только записи, где база ≠ меш.\n"
            % (hashlib.md5(ml).hexdigest(), n))
    o.write("struct FAsdaMobClipBaseRow { int32 MobId; const TCHAR* Variant; };\n")
    o.write("static const FAsdaMobClipBaseRow kAsdaMobClipBase[%d] = {\n" % len(rows))
    for (mid, v) in rows:
        o.write('\t{ %d, TEXT("%s") },\n' % (mid, v))
    o.write("};\n")
    return o.getvalue()


def main():
    ap = argparse.ArgumentParser(description="Таблицы мобов: msif +43, mst +6, маска слотов → AsdaMobSkillTables.inl; "
                                             "вариант клипов базы +0x24 → AsdaMobClipBase.inl")
    ap.add_argument("--src", default=None, help="папка Data/Mob клиента (по умолчанию — эталон Interlude)")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true", help="только сверить оба .inl с источником")
    a = ap.parse_args()
    src = a.src or default_src()
    outs = [(a.out, build(src)), (CLIPBASE_OUT, build_clipbase(src))]
    if a.check:
        rc = 0
        for path, text in outs:
            cur = open(path, "r", encoding="utf-8-sig").read() if os.path.exists(path) else ""
            same = cur == text.lstrip("﻿")
            print("СОВПАДАЕТ" if same else "РАСХОДИТСЯ", path)
            rc |= 0 if same else 1
        return rc
    for path, text in outs:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("записано", os.path.abspath(path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
