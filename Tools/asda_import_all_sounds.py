# Copyright (c) 2026 @sitxovski (Discord)
# Импорт ВСЕХ звуков игры: Data/Sound/*.wav → USoundWave в /Game/AsdaAssets/Sound.
# Таблица имён = Data/Sound/SFT.BIN (эталон login_charselect.c: id 45..54 = S_e_creation01..10 —
# звуки анимации шкатулки char-select, играются 2D по кадрам). Здесь тянем ВСЮ папку, чтобы порт
# имел все SFX сразу (каталогизация — catalog-all-resources).
#   .mp3 (BGM, 19 шт) UE5 нативно не импортит — их берёт asda_import_bgm.py (ffmpeg → wav →
#   /Game/AsdaAssets/Sounds/Music, канон музыки). Здесь только SFX.
# ★ ДУБЛИРУЕТ asda_import_sounds.py (тот же канон + музыка) — оставлен как «только SFX, батчами».
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

SRC = _P.SOUND_DIR
DEST = "/Game/AsdaAssets/Sound"
BATCH = 200                                          # файлов на батч (память/прогресс)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    allf = sorted(os.path.join(SRC, f) for f in os.listdir(SRC))
    wavs = [f for f in allf if f.lower().endswith(".wav")]
    mp3s = [f for f in allf if f.lower().endswith(".mp3")]
    unreal.log("Найдено wav=%d mp3=%d в %s" % (len(wavs), len(mp3s), SRC))
    if not EAL.does_directory_exist(DEST):
        EAL.make_directory(DEST)
    total = 0
    for bi in range(0, len(wavs), BATCH):
        chunk = wavs[bi:bi + BATCH]
        d = unreal.AutomatedAssetImportData()
        d.set_editor_property("destination_path", DEST)
        d.set_editor_property("filenames", chunk)
        d.set_editor_property("replace_existing", True)
        imported = ATOOLS.import_assets_automated(d)
        saved = 0
        for a in imported:
            try:
                EAL.save_loaded_asset(a); saved += 1
            except Exception as e:
                unreal.log_warning("save fail: %s" % e)
        total += saved
        unreal.log("  батч %d..%d: импортировано/сохранено %d (из %d)" % (bi, bi + len(chunk), saved, len(chunk)))
    unreal.log("=== ЗВУКИ: всего импортировано/сохранено %d из %d wav ===" % (total, len(wavs)))
    if mp3s:
        unreal.log_warning("mp3 (BGM) НЕ импортированы (%d) — UE5 не берёт mp3 нативно: %s" %
                           (len(mp3s), ", ".join(os.path.basename(m) for m in mp3s)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
