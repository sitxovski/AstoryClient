# Copyright (c) 2026 @sitxovski (Discord)
# digging_export_clips.py — КОПАНИЕ (10.10.2026): экспорт .glb (вне UE) клипов копания персонажа обоих полов.
#
# ИСТОЧНИК ИСТИНЫ (ничего не выдумано): `Char_LoadMotionAnim` case 0x1F (entity_char.c:21282-21345, эталон):
#   трек 17 (+1088) = "Motion\ch4_98_Shoveling_{m|f}.jbb"     — состояние 40 (лопата, `CCharacter_UpdateActionState` case 7);
#   трек 20 (+1280) = "Motion\ch4_98_Shoveling_{m|f}_01.jbb"  — состояние 43 (авто-лопата, case 10).
#   Имя зависит ТОЛЬКО от пола (`pThis[1079] == 1` — мужской). Файлы — Data/Character/Motion/CharMotion.baz клиента MINE
#   (у всех четырёх длина 6.000 с, 30 к/с, 52 кости у м / 56 у ж; проверено 10.10.2026).
#
# → мини-glb RawAssets/Models/Skeletal/Characters/digging/ch1_{g}_cremmy_preview.glb с клипами anim_233 (40) и anim_234 (43).
#   Скелет/бинд — общая точка `fishing_export_assets.export_char_gender` (1:1 export_char_emotions.export_gender: ref-клип
#   cre_cremmy-a_00_0 + hair-кости частей → трек-имена = существующий ch1_{g}_cremmy_preview_Skeleton). Первым идёт
#   "__ref_ignore" (импорт его выбрасывает).
# ★ Индексы 233/234 — сразу за рыбалкой (225..232); свободны у обоих полов (Content + Source/Tools, 10.10.2026) и внесены
#   в таблицу чужих номеров конвейера навыков `asda_skillclip_guard.FOREIGN` — перезаписать их импортом навыков нельзя.
# Импорт в UE: `python Tools/run_headless_safe.py Tools/digging_import_char_clips.py`.
#
# Запуск (вне UE): python Tools\digging_export_clips.py
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import importlib.util
import json
import os
import sys
import zipfile

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))   # каталог инструментов проекта

# Индекс ассета клипа по состоянию копания (эталонное `+47551`)
DIG_IDX = {40: 233, 43: 234}
# Файл клипа по состоянию (суффикс после пола) и трек эталона
DIG_CLIP = {40: ("", 17), 43: ("_01", 20)}
# Смысл клипа (для журнала и манифеста)
DIG_MEANING = {40: "лопата (case 7: 4 удара, конец = конец клипа)",
               43: "авто-лопата (case 10: 7 ударов, перемотка на порог [1] после 4-го)"}
# Кости клипов копания вне скелета превью — НЕ скиннуты ни одной частью тела, трек при импорте отбрасывается:
#   `test_dummy` (родитель — кисть `8`) и его дочерняя `Box01` — вспомогательные узлы аниматора, есть ТОЛЬКО в четырёх клипах
#   копания (скан 242 клипов ch1_m/ch4_m/ch4_98 CharMotion.baz, 10.10.2026); инструмент эталон вешает на `8_a1`, не на них;
#   `4_a_end` — концевая кость косы женского набора (та же картина у рыбалки и эмоций, `fishing_export_assets.KNOWN_NUBS`).
DIG_NUBS = {"4_a_end", "test_dummy", "box01"}


def _load_fishing():
    """Модуль общей точки экспорта клипов персонажа (fishing_export_assets) — без побочных эффектов."""
    spec = importlib.util.spec_from_file_location("fishing_export_assets", os.path.join(HERE, "fishing_export_assets.py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules["fishing_export_assets"] = mod
    spec.loader.exec_module(mod)
    return mod


def specs_for(gender):
    """[(файл .jbb, anim_N, запись манифеста)] клипов копания пола gender ('m'/'f')."""
    out = []
    for st in (40, 43):
        suf, track = DIG_CLIP[st]
        out.append(("ch4_98_shoveling_%s%s.jbb" % (gender, suf), DIG_IDX[st],
                    dict(state=st, track=track, meaning=DIG_MEANING[st])))
    return out


def main():
    """Экспорт обоих полов; манифест — RawAssets/.../digging/digging_clips_manifest.json. Возвращает True при полном успехе."""
    FE = _load_fishing()
    ECC = FE.ECC
    out_dir = os.path.join(ECC.OUT_DIR, "digging")   # рядом с fishing/, emotions/, skillclips/
    zmodel = zipfile.ZipFile(ECC.MODEL_ARC)
    zmot = zipfile.ZipFile(ECC.MOTION_ARC)
    zai = FE._zi(zmot)
    man, allok = {}, True
    for g in ("m", "f"):
        ok, info = FE.export_char_gender(g, zmodel, zmot, zai, specs=specs_for(g), out_dir=out_dir, tag="digging",
                                         known_nubs=DIG_NUBS)
        man[g] = info
        allok = allok and ok
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "digging_clips_manifest.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(man, f, ensure_ascii=False, indent=1)
    print("\nманифест ->", path)
    print("\n=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")
    return allok


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
