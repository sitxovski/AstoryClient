# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_effect_mob_tables.py — ЗОНД-СВЕРКА трёх таблиц (чистый python, БЕЗ unreal).
# Проверяет три вещи, каждую независимо:
#   1) число записей в CSV == count из заголовка .bin, и размер файла == HDR + count*stride;
#   2) КАЖДОЕ типизированное поле, перечитанное из CSV, побайтно ре-сериализуется обратно в запись —
#      восстановленная запись сравнивается с оригинальными байтами .bin (не «сам с собой»:
#      сборка идёт из строк CSV, а не из промежуточных python-объектов);
#   3) DataHex каждой строки == сырые байты записи; конкатенация всех DataHex + заголовок == весь файл.
# Дополнительно проверяет раскладку на всех 5 клиентах, что лежат на диске (контент версионный —
# сверяется ФОРМАТ: магия, число колонок, размер == HDR + count*stride).
# ★ ПРАВИЛО tools-no-import-side-effects: работа только через main().
import csv
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import hashlib
import io
import os
import struct
import sys

import asda_effect_mob_tables as T  # noqa: E402

OTHER_CLIENTS = [
    os.path.join(_P.ROOT, "Donors", "ArabicASDAStory", "data"),
    _P.DATA_ORIG,
    _P.DATA,
    os.path.join(_P.ROOT, "Donors", "Fantazium", "data"),
]


def read_csv(text):
    return list(csv.DictReader(io.StringIO(text)))


def pad(s, n):
    b = s.encode("latin1")
    assert len(b) < n, "строка не влезает в поле %d: %r" % (n, s)
    return b + b"\0" * (n - len(b))


def check(name, ok, detail=""):
    print("  [%s] %s%s" % ("OK " if ok else "FAIL", name, (" — " + detail) if detail else ""))
    return ok


def verify_effect_list():
    print("== effect list.bin (EFF)")
    blob = open(T.BIN_EFFECT_LIST, "rb").read()
    count, columns = T.read_header(blob, b"EFF")
    rows = T.parse_effect_list()
    back = read_csv(T.effect_list_csv(rows))
    ok = True
    ok &= check("записей", len(back) == count, "%d/%d" % (len(back), count))
    ok &= check("размер файла", len(blob) == T.HDR + count * 36, "%d байт" % len(blob))
    # ре-сборка КОЛОНОК из CSV и сравнение с файлом.
    # Имя сравнивается ДО \0: хвост 32-байтного поля хранит мусор ранее записанных имён
    # (напр. "eft_bow\0ctmon02" — остаток "eft_selectmon02"); это не данные, но DataHex их держит.
    bad_names = garbage = 0
    for i, r in enumerate(back):
        fld = blob[T.HDR + 32 * i:T.HDR + 32 * (i + 1)]
        if pad(r["EffectName"], 32).split(b"\0")[0] != fld.split(b"\0")[0]:
            bad_names += 1
        garbage += sum(1 for b in fld[len(r["EffectName"]) + 1:] if b)
    flags = b"".join(struct.pack("<i", int(r["Flag"])) for r in back)
    ok &= check("колонка Name[32]x%d" % count, bad_names == 0,
                "расхождений %d; хвостового мусора после \\0: %d б (сохранён в DataHex)"
                % (bad_names, garbage))
    ok &= check("колонка Flag[i32]x%d" % count, flags == blob[T.HDR + 32 * count:],
                "расхождений 0" if flags == blob[T.HDR + 32 * count:] else "есть расхождения")
    # DataHex 1:1
    hx_ok = all(bytes.fromhex(r["DataHex"]) ==
                blob[T.HDR + 32 * i:T.HDR + 32 * (i + 1)] + blob[T.HDR + 32 * count + 4 * i:T.HDR + 32 * count + 4 * (i + 1)]
                for i, r in enumerate(back))
    ok &= check("DataHex 1:1", hx_ok)
    return ok


def verify_call_mob():
    print("== CallMobTable.BIN (CMT)")
    blob = open(T.BIN_CALL_MOB, "rb").read()
    count, columns = T.read_header(blob, b"CMT")
    rows = T.parse_call_mob()
    back = read_csv(T.call_mob_csv(rows))
    ok = True
    ok &= check("записей", len(back) == count, "%d/%d" % (len(back), count))
    ok &= check("размер файла", len(blob) == T.HDR + T.CALL_MOB_REC * count, "%d байт" % len(blob))
    bad_fields = bad_hex = 0
    for i, r in enumerate(back):
        rec = struct.pack("<8i", *[int(r[f]) for f in T.CALL_MOB_FIELDS])
        orig = blob[T.HDR + T.CALL_MOB_REC * i:T.HDR + T.CALL_MOB_REC * (i + 1)]
        if rec != orig:
            bad_fields += 1
        if bytes.fromhex(r["DataHex"]) != orig:
            bad_hex += 1
    ok &= check("поля→байты == .bin", bad_fields == 0, "расхождений %d" % bad_fields)
    ok &= check("DataHex 1:1", bad_hex == 0, "расхождений %d" % bad_hex)
    return ok


def verify_monster_skill_effect():
    print("== Monster_SkillEffect.BIN (MSE)")
    blob = open(T.BIN_MONSTER_SKILL_FX, "rb").read()
    count, columns = T.read_header(blob, b"MSE")
    rows = T.parse_monster_skill_effect()
    back = read_csv(T.monster_skill_effect_csv(rows))
    ok = True
    ok &= check("записей", len(back) == count, "%d/%d" % (len(back), count))
    ok &= check("размер файла", len(blob) == T.HDR + T.MSE_REC * count, "%d байт" % len(blob))
    bad_fields = bad_hex = bad_zero = 0
    for i, r in enumerate(back):
        orig = blob[T.HDR + T.MSE_REC * i:T.HDR + T.MSE_REC * (i + 1)]
        # ре-сборка записи ТОЛЬКО из типизированных полей CSV; хвосты фиксированных полей
        # (мусор после \0) берём из оригинала — они не являются данными, но их держит DataHex.
        rec = bytearray(T.MSE_REC)
        struct.pack_into("<HH", rec, 0, int(r["EffectId"]), int(r["Reserved2"]))
        for k in range(T.MSE_SLOTS):
            rec[T.MSE_JEB + 64 * k] = 0
            j = pad(r["Jeb%d" % k], 64)
            rec[T.MSE_JEB + 64 * k:T.MSE_JEB + 64 * (k + 1)] = j
            rec[T.MSE_BONE + 64 * k:T.MSE_BONE + 64 * (k + 1)] = pad(r["Bone%d" % k], 64)
            rec[T.MSE_SLOT_TYPE + k] = int(r["SlotType%d" % k])
            rec[T.MSE_SLOT_PARAM + k] = int(r["SlotParam%d" % k])
        struct.pack_into("<ff", rec, T.MSE_RANGE_A, float(r["RangeA"]), float(r["RangeB"]))
        # сравниваем поле за полем (строковые — до \0, остальное — точно)
        same = (rec[0:4] == orig[0:4]
                and rec[T.MSE_SLOT_TYPE:T.MSE_SLOT_TYPE + 10] == orig[T.MSE_SLOT_TYPE:T.MSE_SLOT_TYPE + 10]
                and rec[T.MSE_RANGE_A:T.MSE_RANGE_A + 8] == orig[T.MSE_RANGE_A:T.MSE_RANGE_A + 8])
        for k in range(T.MSE_SLOTS):
            for base in (T.MSE_JEB, T.MSE_BONE):
                a = bytes(rec[base + 64 * k:base + 64 * (k + 1)]).split(b"\0")[0]
                b = orig[base + 64 * k:base + 64 * (k + 1)].split(b"\0")[0]
                same = same and a == b
        if not same:
            bad_fields += 1
        if bytes.fromhex(r["DataHex"]) != orig:
            bad_hex += 1
        if any(orig[o] for o in range(*T.MSE_ZERO_ZONE)):
            bad_zero += 1
    ok &= check("поля→байты == .bin", bad_fields == 0, "расхождений %d" % bad_fields)
    ok &= check("DataHex 1:1", bad_hex == 0, "расхождений %d" % bad_hex)
    ok &= check("зона +0x28E..0x2CF нулевая", bad_zero == 0, "записей с данными: %d" % bad_zero)
    return ok


def verify_whole_file_coverage():
    """Заголовок + все DataHex должны давать ровно исходный файл (ни одного потерянного байта)."""
    print("== покрытие файлов целиком (заголовок + DataHex)")
    ok = True
    for key in ("EffectList", "CallMob", "MonsterSkillEffect"):
        t = T.TABLES[key]
        blob = open(t["bin"], "rb").read()
        rows = t["parse"]()
        raw = [bytes.fromhex(r["DataHex"]) for r in rows]
        if key == "EffectList":
            # колоночное хранение: DataHex = имя(32) + флаг(4), файл = все имена, затем все флаги
            body = b"".join(x[:32] for x in raw) + b"".join(x[32:] for x in raw)
        else:
            body = b"".join(raw)
        ok &= check(key, blob[:T.HDR] + body == blob, "%d байт тела, побайтно == .bin" % len(body))
    return ok


def verify_clients_format():
    """Кросс-клиентская проверка РАСКЛАДКИ (не идентичности: контент у клиентов разных версий).
    У каждого клиента: магия на месте, число колонок то же, размер == HDR + count*stride."""
    print("== раскладка на всех клиентах (контент версионный — сверяем ФОРМАТ)")
    ok = True
    for key, rel, magic, stride in (("EffectList", r"Effect\effect list.bin", b"EFF", 36),
                                    ("CallMob", r"Mob\CallMobTable.BIN", b"CMT", T.CALL_MOB_REC),
                                    ("MonsterSkillEffect", r"Mob\Monster_SkillEffect.BIN", b"MSE", T.MSE_REC)):
        good, info = 0, []
        for root in [os.path.dirname(os.path.dirname(T.TABLES[key]["bin"]))] + OTHER_CLIENTS:
            p = os.path.join(root, rel)
            if not os.path.exists(p):
                continue
            blob = open(p, "rb").read()
            cnt, cols = T.read_header(blob, magic)
            fits = len(blob) == T.HDR + cnt * stride
            good += 1 if fits else 0
            info.append("%s:n=%d%s" % (os.path.basename(root.rstrip("\\")) or root, cnt, "" if fits else "!РАЗМЕР"))
            ok &= fits
        check(key, ok, "%d клиентов, все размеры сходятся; %s" % (good, ", ".join(info)))
    return ok


def main():
    all_ok = True
    for fn in (verify_effect_list, verify_call_mob, verify_monster_skill_effect,
               verify_whole_file_coverage, verify_clients_format):
        all_ok &= bool(fn())
    print("\nИТОГ: %s" % ("ВСЁ СОШЛОСЬ, расхождений 0" if all_ok else "ЕСТЬ РАСХОЖДЕНИЯ"))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
