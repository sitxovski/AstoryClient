# Copyright (c) 2026 @sitxovski (Discord)
# Импорт BGM (музыка): mp3 из Data/Sound сконвертированы ffmpeg → wav (44.1кГц) и импортируются
# в КАНОНИЧЕСКИЙ каталог музыки /Game/AsdaAssets/Sounds/Music (UE5 не берёт mp3 нативно).
#
# КАНОН РАСКЛАДКИ ЗВУКА (утверждён 2026-08-02, отчёт Reports/sound_layout_cleanup.md):
#   /Game/AsdaAssets/Sound        — SFX (1462 wav клиента) + DT_SoundTable; дефолт ResolveSound(Name, "Sound").
#   /Game/AsdaAssets/Sounds/Music — МУЗЫКА (стрим): PlayMusic → ResolveSound(Track, "Sounds/Music")
#                                   (AsdaSoundSubsystem.cpp:172) — ЕДИНСТВЕННЫЙ путь музыки в рантайме.
#   /Game/AsdaAssets/Sounds/UI    — служебный клик генератора UMG (S_e_button09).
#   Разделение повторяет ОРИГИНАЛ: там один каталог Data/Sound, но два устройства FMOD —
#   sfxDevice (1462 .wav, id-таблица SFT.BIN) и musicDevice (19 .mp3, стрим CSoundStream).
#   Каталог /Game/AsdaAssets/Sound/BGM УДАЛЁН 2026-08-02 (был подмножеством Sounds/Music).
#
# ★ 2026-08-01: SRC указывал на scratchpad ЧУЖОЙ сессии (…\Temp\claude\c2bfb14c-…\bgm_wav) — такого
#   каталога давно нет, скрипт упал бы на listdir. Постоянное место конвертированных wav —
#   RawAssets/Sounds/Music (21 файл). Пути — из asda_ue_paths.py.
#   ★ 2026-08-02: добавлен S_BGM_Inferion.wav — трек карты Inferion (её .TER просит слот 0
#     «S_BGM_Inferion»). Источник — AsdaGlobal\Data\Sound\S_BGM_inferion.mp3; тот же файл лежит
#     во второй части (NEW\AsdaFableClassic, NEW\WorldOfDreams), все три байт-в-байт идентичны
#     (md5 7c26ab19d37a1f712190401e6efcef8a). Имя ассета — из .TER (S_BGM_Inferion), не из mp3.
#     S_BGM_NEW_Field.wav — копия stagnantdesert (решение владельца 2026-08-01, трека нет нигде).
#   ★ 2026-08-02 (2): добавлен S_BGM_flabisTown.wav — слот 0 «город» карты flabis (.TER просит
#     «S_BGM_flabisTown», флаг наличия=1; такого mp3 НЕТ ни в одной сборке). Решение владельца:
#     источник — NEW\AsdaFableClassic\Data\Sound\S_BGM_flabisfield.mp3.
#     ФАКТ (сверено побайтово): flabisfield и stagnantdesert второй части — ОДИН файл
#     (md5 24c7e0057b982d98f3719d75003643d9), а наш Donors\AsdaChroniclesInterlude_MINE\…\S_BGM_flabisfield.mp3
#     отличается ТОЛЬКО ID3-тегами (0 различий в аудио-теле) → аудио у flabis-поля, flabis-города
#     и пустыни ОДНО И ТО ЖЕ. После ffmpeg все четыре wav совпадают (md5 22e671f4b411c65b7d07196cf26a15d0).
#   Конвертация, если каталог пуст:
#     ffmpeg -y -i <клиент>/Data/Sound/<имя>.mp3 -ar 44100 RawAssets/Sounds/Music/<имя>.wav
#
# ★ Кода на уровне модуля больше нет (правило tools-no-import-side-effects): всё в main().
# Запуск: run_headless.bat asda_import_bgm.py

import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as P
import asda_ue_paths as U

SRC     = os.path.join(U.RAW_SOUNDS, "Music")   # RawAssets/Sounds/Music — конвертированные wav
DEST    = U.G_SOUNDS_MUSIC                      # /Game/AsdaAssets/Sounds/Music — канон музыки (читает PlayMusic)
MP3_SRC = P.SOUND_DIR                            # <клиент>/Data/Sound — исходные mp3 (для сверки состава)


def main():
    EAL = unreal.EditorAssetLibrary
    ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()

    if not os.path.isdir(SRC):
        unreal.log_error("[BGM] нет каталога с wav: %s (сконвертируй mp3 из %s, см. шапку)" % (SRC, MP3_SRC))
        return
    wavs = sorted(os.path.join(SRC, f) for f in os.listdir(SRC) if f.lower().endswith(".wav"))
    unreal.log("[BGM] wav к импорту: %d (из %s)" % (len(wavs), SRC))
    if not wavs:
        unreal.log_error("[BGM] в %s нет .wav — импортировать нечего" % SRC)
        return
    # ★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): Login.wav → Sounds/Music/Login — пакет сцены логина
    #   (берут только DA_LoginConfig и PlayMusic("Login") в AsdaLoginGameMode.cpp); replace_existing переписал бы его. Без
    #   ASDA_ALLOW_LOGIN_WRITE=1 такие wav не импортируются, остальные треки — как раньше. Итог — одной строкой [LOGIN-GUARD].
    import asda_login_guard as _LG   # Tools уже в sys.path (строка у импорта asda_paths)
    login_skipped = [w for w in wavs if _LG.login_package_blocked(
        "%s/%s" % (DEST, os.path.splitext(os.path.basename(w))[0]), "asda_import_bgm.py", quiet=True)]
    if login_skipped:
        wavs = [w for w in wavs if w not in login_skipped]
        unreal.log_warning("[LOGIN-GUARD] [BGM] треков сцены логина пропущено: %d (%s) — не импортированы (только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"
                           % (len(login_skipped), ", ".join(os.path.basename(w) for w in login_skipped)))
        if not wavs:
            return
    if not EAL.does_directory_exist(DEST):
        EAL.make_directory(DEST)

    d = unreal.AutomatedAssetImportData()
    d.set_editor_property("destination_path", DEST)
    d.set_editor_property("filenames", wavs)
    d.set_editor_property("replace_existing", True)
    saved = 0
    for a in ATOOLS.import_assets_automated(d):
        try:
            # Музыка зациклена — эталон FSOUND_Stream_OpenFile(..., 0x02 = FSOUND_LOOP_NORMAL);
            #   PlayMusic(bLoop) полагается на свойство ассета.
            a.set_editor_property("looping", True)
        except Exception as e:
            unreal.log_warning("[BGM] looping fail %s: %s" % (a.get_name(), e))
        try:
            EAL.save_loaded_asset(a); saved += 1
        except Exception as e:
            unreal.log_warning("[BGM] save fail: %s" % e)
    unreal.log("=== BGM: импортировано/сохранено %d из %d → %s ===" % (saved, len(wavs), DEST))


if __name__ == "__main__":
    main()
