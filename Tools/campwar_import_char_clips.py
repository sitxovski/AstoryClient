# Copyright (c) 2026 @sitxovski (Discord)
# campwar_import_char_clips.py — ★ 30.10.2026 «БИТВА АРМИЙ»: headless-импорт клипов поз CampWar персонажа (anim_98 / anim_99)
# на СУЩЕСТВУЮЩИЙ скелет ch1_{m|f}_cremmy_preview_Skeleton. [ОРИГИНАЛ]
# Источник: RawAssets/Models/Skeletal/Characters/campwar/ch1_{m|f}_cremmy_preview.glb (`campwar_export_clips.py`).
# Соответствие (`Reports/faction_select/BATTLE_GAPS_RE.md` §2): anim_98 = состояние 47 (трек 25, ch4_{g}_00_occupa_00_0.jbb,
#   цикл), anim_99 = состояние 51 (трек 26, ch4_{g}_00_loser_00_0.jbb, одноразовый).
# Механика — общая точка `fishing_import_char_clips.import_gender` (Interchange: skeleton = существующий +
#   import_only_animations, импорт во ВРЕМЕННУЮ папку → перенос каждого «…anim_NN» на место ch1_{g}_cremmy_previewanim_NN);
#   этот скрипт подставляет ей только источник, временную папку и список номеров (образец — `digging_import_char_clips.py`).
# ★ ЗАЩИТА ЧУЖИХ КЛИПОВ: пишутся РОВНО номера 98/99 (блок 0…99 закрыт от конвейера навыков `asda_skillclip_guard.FOREIGN`);
#   эмоции anim_86…97, прочие anim_NN, меши и скелет не трогаются (переимпорт скелета запрещён). Если ассет 98/99 уже лежит
#   в Content, он ПЕРЕЗАПИСЫВАЕТСЯ только с ASDA_CW_REIMPORT=1 (его могли править в редакторе) — иначе отказ.
# Запуск: MSYS_NO_PATHCONV=1 python Tools/run_headless_safe.py Tools/campwar_import_char_clips.py --marker "[campwar_char] ИТОГ"
import importlib.util
import os
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))   # каталог инструментов
sys.path.insert(0, HERE)
RAW = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Characters", "campwar"))   # мини-glb
TMP = "/Game/Tmp_CampWarClips"   # временная папка импорта (удаляется общей точкой)
EXPECT = [98, 99]                # anim_98 (47 захват) / anim_99 (51 поражение)


def log(m):
    """Журнал импорта клипов CampWar."""
    unreal.log("[campwar_char] " + str(m))


def _fishing_import():
    """Модуль общей точки импорта (`fishing_import_char_clips`) — загружается без побочных эффектов (у importlib есть __spec__)."""
    spec = importlib.util.spec_from_file_location("fishing_import_char_clips", os.path.join(HERE, "fishing_import_char_clips.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    """Импорт обоих полов (или переданных в argv 'm'/'f'). Возвращает True при полном успехе."""
    FI = _fishing_import()
    FI.RAW, FI.TMP, FI.EXPECT, FI.log = RAW, TMP, list(EXPECT), log
    import asda_skillclip_guard as G
    allow = os.environ.get("ASDA_CW_REIMPORT", "") == "1"
    genders = [a.lower() for a in sys.argv if a.lower() in ("m", "f")] or ["m", "f"]
    ok = True
    for g in genders:
        exist = [n for n in EXPECT if os.path.exists(G.asset_file(g, n))]
        if exist and not allow:
            log("[%s] ОТКАЗ: ассеты %s уже лежат в Content — перезапись только с ASDA_CW_REIMPORT=1 (правки владельца)" % (g, exist))
            ok = False
            continue
        ok = FI.import_gender(g) and ok
    log("ИТОГ: %s" % ("ВСЕ OK" if ok else "ЕСТЬ ПРОБЛЕМЫ"))
    return ok


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает (`__spec__` выставляет любая машинерия
#   импорта и не выставляет прямое исполнение файла — python file.py / UE -ExecutePythonScript).
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
