# Copyright (c) 2026 @sitxovski (Discord)
# import_login_music.py — импорт Login.wav (BGM логина, зациклённая) → /Game/AsdaAssets/Sounds/Music/Login.
# Эталон: CSoundStream_PlayMusic("Login.mp3", loop=1) в state 4/9 (логин/char-make сцена creback).
import unreal
SRC = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Sounds\Music\Login.wav"
DEST = "/Game/AsdaAssets/Sounds/Music"
FULL = DEST + "/Login"
def log(m): unreal.log("[login_music] " + str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): удаляет и переимпортирует
    #   /Game/AsdaAssets/Sounds/Music/Login — пакет сцены логина (берут только DA_LoginConfig и PlayMusic("Login") в
    #   AsdaLoginGameMode.cpp). Без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до удаления.
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    from asda_login_guard import forbid_login_write
    forbid_login_write("import_login_music.py", "Sounds/Music/Login (удаление, импорт Login.wav, looping, сохранение папки Music)")
    if unreal.EditorAssetLibrary.does_asset_exist(FULL):
        unreal.EditorAssetLibrary.delete_asset(FULL)
    data = unreal.AutomatedAssetImportData()
    data.set_editor_property("destination_path", DEST)
    data.set_editor_property("filenames", [SRC])
    data.set_editor_property("replace_existing", True)
    assets = unreal.AssetToolsHelpers.get_asset_tools().import_assets_automated(data) or []
    for a in assets:
        if isinstance(a, unreal.SoundWave):
            a.set_editor_property("looping", True)     # зациклённая музыка логина
            log("SoundWave Login: looping=True")
    unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=False, recursive=True)
    log("ГОТОВО -> " + FULL)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
