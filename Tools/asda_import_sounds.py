# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_sounds.py — импорт ВСЕГО звука игры в КАНОНИЧЕСКУЮ раскладку проекта.
#
# КАНОН (утверждён 2026-08-02, отчёт Reports/sound_layout_cleanup.md):
#   SFX     → /Game/AsdaAssets/Sound         (1462 .wav клиента 1:1; дефолт ResolveSound(Name,"Sound"))
#   МУЗЫКА  → /Game/AsdaAssets/Sounds/Music  (PlayMusic → ResolveSound(Track,"Sounds/Music"))
# Раскладка повторяет ОРИГИНАЛ: там один каталог Data/Sound, но ДВА устройства FMOD —
#   sfxDevice (сэмплы .wav, id-таблица SFT.BIN) и musicDevice (стрим .mp3, CSoundStream, LOOP_NORMAL).
#
# ★ 2026-08-02: раньше скрипт клал SFX в /Game/AsdaAssets/Sounds/SFX — параллельный каталог, на который
#   НИЧТО не ссылалось (1462 дубля, 102 МБ). Каталог удалён; здесь назначение исправлено на канон.
#   Заодно снят PURGE_FIRST c delete_directory("/Game/AsdaAssets/Sounds") — он снёс бы и музыку, и UI.
# ★ Кода на уровне модуля нет (правило tools-no-import-side-effects).
# Запуск: run_headless.bat asda_import_sounds.py
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as P
import asda_ue_paths as U

GAME_SFX  = P.SOUND_DIR                              # <клиент>/Data/Sound — loose .wav (SFX/голоса)
RAW_MUSIC = os.path.join(U.RAW_SOUNDS, "Music")      # RawAssets/Sounds/Music — BGM .wav (из mp3, ffmpeg)


def import_group(src_dir, dest, looping=False):
    """Импорт всех .wav каталога в dest. Уже существующие пропускаются (инкрементально)."""
    EAL = unreal.EditorAssetLibrary
    ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
    if not os.path.isdir(src_dir):
        unreal.log_warning("[sound] нет каталога %s" % src_dir)
        return 0
    files = [os.path.join(src_dir, f) for f in sorted(os.listdir(src_dir)) if f.lower().endswith(".wav")]
    files = [f for f in files
             if not EAL.does_asset_exist("%s/%s" % (dest, os.path.splitext(os.path.basename(f))[0]))]
    if not files:
        return 0
    if not EAL.does_directory_exist(dest):
        EAL.make_directory(dest)
    n = 0
    for bi in range(0, len(files), 200):                      # батчами — память/прогресс
        d = unreal.AutomatedAssetImportData()
        d.set_editor_property("destination_path", dest)
        d.set_editor_property("filenames", files[bi:bi + 200])
        d.set_editor_property("replace_existing", True)
        for a in ATOOLS.import_assets_automated(d):
            if looping:
                # музыка зациклена — эталон FSOUND_Stream_Open(..., 0x02 = FSOUND_LOOP_NORMAL)
                try: a.set_editor_property("looping", True)
                except Exception as e: unreal.log_warning("[sound] looping %s: %s" % (a.get_name(), e))
            try:
                EAL.save_loaded_asset(a); n += 1
            except Exception as e:
                unreal.log_warning("[sound] save fail: %s" % e)
    return n


def main():
    nm = import_group(RAW_MUSIC, U.G_SOUNDS_MUSIC, looping=True)
    ns = import_group(GAME_SFX, U.G_SOUND)
    unreal.log("asda_import_sounds: музыка +%d → %s, SFX +%d → %s"
               % (nm, U.G_SOUNDS_MUSIC, ns, U.G_SOUND))


if __name__ == "__main__":
    main()
