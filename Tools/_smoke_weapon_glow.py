# Copyright (c) 2026 @sitxovski (Discord)
# _smoke_weapon_glow.py — СМОК свечения заточенного оружия/щита: для КАЖДОГО надетого предмета
# item-БД (оружие itemType 9, щит itemType 8) × уровни заточки 5/6/7 строим имя .jeb ТЕМ ЖЕ правилом,
# что и C++ (UAsdaWorldSubsystem::BuildGlowJebName), и проверяем, что BP_Fx существует и НЕ пустой.
# Запуск: Tools/run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_smoke_weapon_glow.py
import sys, os, struct, collections
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal

DATA_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\Character"
OUT_PKG = "/Game/AsdaAssets/Effects"
WEAPON_BASE = {0: "ohs", 1: "ohs", 2: "spr", 3: "ths", 5: "stf", 8: "bow", 9: "xbow", 10: "bow", 11: "bali"}
GRADE_CODE = {0: "co", 1: "uc", 2: "ra", 3: "he", 4: "le"}
# Кость крепления (эталон): оружие "8_a1", луки (subType 8/10) "_8_a1", щит "_7_a1".
def bone_of(is_shield, sub): return "_7_a1" if is_shield else ("_8_a1" if sub in (8, 10) else "8_a1")


def log(m): unreal.log_warning("[смок-заточка] %s" % m)


def name_of(base, isub, grade, enh):
    if enh < 5 or not base: return ""
    if not (1 <= isub <= 12): return ""
    g = GRADE_CODE.get(grade)
    if g is None: return ""
    return "%s_%s_%s_ma_%s" % (base, chr(96 + isub), g, "01" if enh == 5 else ("02" if enh == 6 else "03"))


def main():
    il = open(os.path.join(DATA_DIR, "item_list.dec.bin"), "rb").read()
    ig = open(os.path.join(DATA_DIR, "item_img_table.bin"), "rb").read()
    n = struct.unpack_from("<i", il, 4)[0]
    first = struct.unpack_from("<i", il, 20)[0]
    # уникальные (имя эффекта, кость) + пример itemId/grade для строки-доказательства
    combos = {}
    no_name = collections.Counter()
    for i in range(n):
        o, io = 20 + 248 * i, 20 + 300 * i
        if o + 248 > len(il) or io + 300 > len(ig): break
        itype = struct.unpack_from("<i", il, o + 0x80)[0]
        if itype not in (8, 9): continue
        code = struct.unpack_from("<i", il, o)[0]
        sub = struct.unpack_from("<i", il, o + 0x84)[0]
        grade = struct.unpack_from("<i", il, o + 0xEC)[0]
        isub = struct.unpack_from("<i", ig, io + 212)[0]
        is_shield = (itype == 8)
        base = "shd" if is_shield else WEAPON_BASE.get(sub)
        for enh in (5, 6, 7):
            nm = name_of(base, isub, grade, enh)
            if not nm:
                no_name["itemType=%d subType=%d imgSub=%d grade=%d" % (itype, sub, isub, grade)] += 1
                continue
            combos.setdefault((nm, bone_of(is_shield, sub)), (code, enh, is_shield))
    log("уникальных сочетаний (эффект, кость) из item-БД: %d" % len(combos))

    ok = miss = empty = 0
    shown = 0
    for (nm, bone), (code, enh, is_shield) in sorted(combos.items()):
        bp = "%s/BP_Fx_%s" % (OUT_PKG, nm.replace("-", "_"))
        if not unreal.EditorAssetLibrary.does_asset_exist(bp):
            miss += 1
            log("НЕТ BP: %s (itemId=%d, заточка=%d)" % (bp, code, enh)); continue
        a = unreal.EditorAssetLibrary.load_asset(bp)
        cdo = unreal.get_default_object(a.generated_class())
        ems = cdo.get_editor_property("emitters") or []
        if len(ems) == 0:
            empty += 1
            log("ПУСТОЙ BP: %s" % bp); continue
        ok += 1
        if shown < 12:
            shown += 1
            log("[заточка] itemId=%d grade=%d → эффект=%s (%s, кость %s, эмиттеров %d)"
                % (code, enh, nm, "щит" if is_shield else "оружие", bone, len(ems)))
    log("ИТОГ: BP найден и не пуст %d/%d; отсутствует %d; пустых %d"
        % (ok, len(combos), miss, empty))
    if no_name:
        log("комбинаций БЕЗ эффекта (нет буквы imgSubType / нет подтипа): %d разновидностей, напр.: %s"
            % (len(no_name), "; ".join(list(no_name)[:4])))


if __name__ == "__main__":
    main()
