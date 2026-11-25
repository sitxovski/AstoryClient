# Copyright (c) 2026 @sitxovski (Discord)
# duel_flag_export_models.py — ★ 31.10.2026 ФЛАГИ ДУЭЛИ: экспорт .glb (вне UE) скиннованных моделей флагов с их клипами. [ОРИГИНАЛ]
#
# ИСТОЧНИК ИСТИНЫ (ничего не выдумано, `Reports/duel/DUEL_FLOW.md` §3):
#   объекты-флаги `uiClanFlagGetOrCreate@0xFAF910` (ui_core.c:99138) → `CClan_DrawClanFlagByIdx` (social.c:9705) /
#   `mapDecoModelLoadStage@0xFB29F0` (world_map.c:10120): таблица моделей по ТИПУ объекта — `unk_3519D3C + 1028·тип`
#   (+0 модель .jbm, +256 клип стадии загрузки, +512/+768 клипы 1/2, +1024 число мешей); снята с ДВУХ дампов памяти
#   (`RE/dumps/asda.dmp`, `live.dmp` — совпадают):
#     тип 5 (флаг за спиной участника дуэли, 4305 `uiClanFlagGetOrCreate(…, charId+20000, 5)`) — pvp_flag_back.jbm + Battle_Flag_00.jbb;
#     тип 6 (флаг в центре арены, 4305 `clanFlagSpawnAt` → тип 6)                              — pvp_flag_land.jbm + pvp_flag_land.jbb.
#   Файлы — CharModel.bnz / CharMotion.baz клиента MINE (= донор-эталон AsdaChroniclesInterlude по составу).
#   Тики объектов (`UpdateList_TypedSub@0xFB03C0`): тип 5 → `CharAnim_UpdateFrame@…` (клип 0 по кругу, крепление к кости "2"
#   персонажа), тип 6 → `partAnimAdvanceBlend` (клип 0 по кругу на месте).
# → RawAssets/Models/DuelFlag/<модель>.glb: 1 SkeletalMesh + клип anim_00. Сборка — единый вход скина `asda_skin_build.build_skinned_glb`
#   (fixed-путь, как у флага клана small_flag).
# Импорт в UE: python Tools/run_headless_safe.py Tools/duel_flag_import_models.py --marker "[duelflag] ИТОГ"
# Запуск (вне UE): python Tools\duel_flag_export_models.py
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os
import sys
import zipfile

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))   # каталог инструментов
sys.path.insert(0, HERE)
OUT_DIR = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "DuelFlag"))   # выход .glb
# (модель .jbm, клип .jbb) по типу объекта-флага эталона
MODELS = {5: ("pvp_flag_back", "battle_flag_00"), 6: ("pvp_flag_land", "pvp_flag_land")}


def _zi(z):
    """Индекс архива: имя в нижнем регистре → настоящее имя."""
    return {n.lower(): n for n in z.namelist()}


def main():
    """Собрать оба .glb. Возвращает True при полном успехе."""
    import asda_paths as P
    import asda_skin_build as SB
    zmodel = zipfile.ZipFile(P.DATA + r"\Character\Model\CharModel.bnz")
    zmot = zipfile.ZipFile(P.DATA + r"\Character\Motion\CharMotion.baz")
    zm, za = _zi(zmodel), _zi(zmot)
    os.makedirs(OUT_DIR, exist_ok=True)
    ok = True
    for typ, (model, clip) in sorted(MODELS.items()):
        jm, jb = zm.get(model + ".jbm"), za.get(clip + ".jbb")
        if not jm or not jb:
            print("FAIL тип %d: нет %s.jbm или %s.jbb в архивах" % (typ, model, clip)); ok = False; continue
        jbb = SB.parse_jbb(zmot.read(jb))
        if not jbb:
            print("FAIL тип %d: %s не разобран" % (typ, jb)); ok = False; continue
        out = os.path.join(OUT_DIR, model + ".glb")
        rep = SB.build_skinned_glb(model, zmodel.read(jm), [("anim_00", jbb)], out, mode=SB.MODE_FIXED)
        print("тип %d: %s + %s → %s | кости %s→%s, bind_err=%s, %.3f с, %d б" % (typ, jm, jb, out, rep["bones_before"], rep["bones_after"],
              rep["bind_err"], float(jbb.get("duration", -1)), os.path.getsize(out)))
    print("=== ИТОГ:", "ВСЕ OK" if ok else "ЕСТЬ ПРОБЛЕМЫ", "===")
    return ok


if __name__ == "__main__":
    main()
