# Copyright (c) 2026 @sitxovski (Discord)
# Создать DataAsset-конфиг login-сцены /Game/AsdaAssets/Config/DA_LoginConfig (класс UAsdaLoginConfig).
# Editor-Friendly: дизайнер правит его в Content Browser. Значения = эталонные дефолты (заданы в
# конструкторе UAsdaLoginConfig), здесь только создаём инстанс, если его нет. Звук LoginMusic — привязка.
import unreal

PKG = "/Game/AsdaAssets/Config"
NAME = "DA_LoginConfig"
PATH = "%s/%s" % (PKG, NAME)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()


cfg_class = unreal.AsdaLoginConfig


# привязать секвенцию перелёта, если построена
FLIGHT = "/Game/AsdaAssets/Cinematics/LS_LoginFlight"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): пишет ТОЛЬКО /Game/AsdaAssets/Config/DA_LoginConfig —
    #   его грузит лишь AsdaLoginGameMode.cpp (LS_LoginFlight и музыку только читает).
    #   Запуск только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1 (иначе стоп, код 3).
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    from asda_login_guard import forbid_login_write
    forbid_login_write("asda_create_login_config.py", "Config/DA_LoginConfig (создание, привязка музыки и перелёта, сохранение)")
    if not EAL.does_directory_exist(PKG):
        EAL.make_directory(PKG)
    if EAL.does_asset_exist(PATH):
        asset = EAL.load_asset(PATH)
        unreal.log("Конфиг уже есть: %s" % PATH)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cfg_class)
        asset = ATOOLS.create_asset(NAME, PKG, cfg_class, factory)
        unreal.log("Создан конфиг: %s" % PATH)
    # привязать музыку логина, если импортирована
    music = None
    # Login.mp3 — музыкальный стрим оригинала → канон музыки /Game/AsdaAssets/Sounds/Music.
    for cand in ("/Game/AsdaAssets/Sounds/Music/Login.Login",):
        if EAL.does_asset_exist(cand.split(".")[0]):
            music = EAL.load_asset(cand.split(".")[0]); break
    if asset and music:
        asset.set_editor_property("login_music", music)
    if asset and EAL.does_asset_exist(FLIGHT):
        asset.set_editor_property("flight_sequence", EAL.load_asset(FLIGHT))
        unreal.log("FlightSequence привязан: %s" % FLIGHT)
    if asset:
        EAL.save_loaded_asset(asset)
        # диагностика: сколько кью в дефолте
        oc = asset.get_editor_property("open_sound_cues")
        tc = asset.get_editor_property("turn_sound_cues")
        unreal.log("=== DA_LoginConfig готов: open_cues=%d turn_cues=%d fps=%.1f music=%s ===" % (
            len(oc), len(tc), asset.get_editor_property("anim_frame_rate"), "есть" if music else "нет"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
