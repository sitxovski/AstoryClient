# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_forge_textures.py — качество текстур свечения форджинга (eft_creation01-a_00/01/02):
#   авторские 128×128; DXT-сжатие + мип-даунсемпл на дистанции (~80000 UE) давали «ступеньки»/мыло.
#   Фикс: БЕЗ сжатия (UserInterface2D = RGBA8) + NoMipmaps (всегда полные 128px) + NeverStream.
import unreal
EAL = unreal.EditorAssetLibrary


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for stem in ("eft_creation01-a_00", "eft_creation01-a_01", "eft_creation01-a_02"):
        p = "/Game/AsdaAssets/Textures/Effects/%s" % stem
        t = EAL.load_asset(p)
        if not t:
            unreal.log_warning("нет %s" % p); continue
        t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)  # RGBA8 без сжатия
        t.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        t.set_editor_property("never_stream", True)
        ok = EAL.save_loaded_asset(t)
        unreal.log("[FORGE-TEX] %s: RGBA8+NoMips save=%s" % (stem, ok))
    unreal.log("=== FORGE TEX DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
