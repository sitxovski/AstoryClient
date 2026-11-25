# Copyright (c) 2026 @sitxovski (Discord)
# campwar_export_clips.py — ★ 30.10.2026 «БИТВА АРМИЙ» (CampWar, карта 19): экспорт .glb (вне UE) поз захвата и поражения
# персонажа обоих полов. [ОРИГИНАЛ]
#
# ИСТОЧНИК ИСТИНЫ (ничего не выдумано, `Reports/faction_select/BATTLE_GAPS_RE.md` §2):
#   состояние 47 «захват точки» (ставит только 6724 sub 1, `CCampWar_OnObjectEffectPacket@0xCA5910`, combat_pvp.c:5585/5592):
#     `CChar_SetState@0xA13D50` case 47 (entity_char.c:19869) → слот 25 (+0x640) → трек 25 (`CCharacter_UpdateActionState` case 14,
#     entity_char.c:20490: по концу клипа кадр 0 — ЦИКЛ, состояние не меняется);
#     файл `Char_LoadMotionAnim` case 14 (entity_char.c:5447): "ch4" + "_m"/"_f" + "_00" + "_occupa" + "_00_0.jbb" (строки .rdata
#     0x13A5D88 / 0x13A5D7C, push 0x9B902B / 0x9BA640);
#   состояние 51 «проигравший PvP» (4304 `PvpDuel_ApplyMatchResult`, 4025 действие 121, спаун @102 = 121): слот 26 (+1664), трек 26,
#     `ch4_{m|f}_00_loser_00_0.jbb`; по концу клипа `SetState(0)` — ОДНОРАЗОВЫЙ.
#   Имя зависит ТОЛЬКО от пола (категория 1 — набор `_00`, от оружия/класса не зависит). Файлы — Data/Character/Motion/CharMotion.baz
#   клиента MINE (= эталону по размеру: м occupa 75 868, ж 81 932, м loser 291 468, ж 314 780; 30 к/с; 1.000 с / 4.267 с).
#
# → мини-glb RawAssets/Models/Skeletal/Characters/campwar/ch1_{g}_cremmy_preview.glb с клипами anim_98 (47) и anim_99 (51).
#   Скелет/бинд — общая точка `fishing_export_assets.export_char_gender` (1:1 export_char_emotions.export_gender: ref-клип
#   cre_cremmy-a_00_0 + hair-кости частей → трек-имена = существующий ch1_{g}_cremmy_preview_Skeleton). Первым идёт
#   "__ref_ignore" (импорт его выбрасывает). Существующие эмоции anim_86…97 НЕ переэкспортируются.
# ★ Индексы 98/99 — остаток блока 86…99 клипов набора `_00` (export_char_emotions.py:16); свободны у обоих полов (Content,
#   30.10.2026) и уже закрыты от конвейера навыков таблицей `asda_skillclip_guard.FOREIGN` (блок 0…99).
# Импорт в UE: `python Tools/run_headless_safe.py Tools/campwar_import_char_clips.py --marker "[campwar_char] ИТОГ"`.
#
# Запуск (вне UE): python Tools\campwar_export_clips.py
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

# Индекс ассета клипа по состоянию эталона (`+47551`)
CW_IDX = {47: 98, 51: 99}
# Файл клипа по состоянию (середина имени после пола) и трек эталона
CW_CLIP = {47: ("occupa", 25), 51: ("loser", 26)}
# Смысл клипа (для журнала и манифеста)
CW_MEANING = {47: "захват точки CampWar (case 14: цикл, по концу — кадр 0, состояние не меняется)",
              51: "проигравший PvP (по концу клипа — состояние 0, одноразовый)"}


def _load_fishing():
    """Модуль общей точки экспорта клипов персонажа (fishing_export_assets) — без побочных эффектов."""
    spec = importlib.util.spec_from_file_location("fishing_export_assets", os.path.join(HERE, "fishing_export_assets.py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules["fishing_export_assets"] = mod
    spec.loader.exec_module(mod)
    return mod


def specs_for(gender):
    """[(файл .jbb, anim_N, запись манифеста)] клипов CampWar пола gender ('m'/'f')."""
    out = []
    for st in (47, 51):
        mid, track = CW_CLIP[st]
        out.append(("ch4_%s_00_%s_00_0.jbb" % (gender, mid), CW_IDX[st], dict(state=st, track=track, meaning=CW_MEANING[st])))
    return out


def main():
    """Экспорт обоих полов; манифест — RawAssets/.../campwar/campwar_clips_manifest.json. Возвращает True при полном успехе."""
    FE = _load_fishing()
    ECC = FE.ECC
    out_dir = os.path.join(ECC.OUT_DIR, "campwar")   # рядом с fishing/, emotions/, digging/
    zmodel = zipfile.ZipFile(ECC.MODEL_ARC)
    zmot = zipfile.ZipFile(ECC.MOTION_ARC)
    zai = FE._zi(zmot)
    man, allok = {}, True
    for g in ("m", "f"):
        ok, info = FE.export_char_gender(g, zmodel, zmot, zai, specs=specs_for(g), out_dir=out_dir, tag="campwar")
        man[g] = info
        allok = allok and ok
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "campwar_clips_manifest.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(man, f, ensure_ascii=False, indent=1)
    print("\nманифест ->", path)
    print("\n=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")
    return allok


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
