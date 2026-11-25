# Copyright (c) 2026 @sitxovski (Discord)
# Импорт атласа цифр урона font.TEX (сконвертирован в PNG) → /Game/AsdaAssets/UI/T_AsdaCombatFont.
# Атлас 256×256: клетка 0.125; цифры v=0.75..1.0 (0..7 / 8,9+ПРОМАХ), слова УКЛОНЕНИЕ(0,0.625),
# НЕУЯЗВИМОСТЬ(0,0.5), ОПЫТ+(0,0.375) — реверс combatTextCharToGlyphUV@0x11632C0.
import os

import unreal

# ★ 18.10.2026: языковой вариант атласа. ASDA_COMBATFONT_LANG=en → английский атлас оригинала (Donors/AsdaGlobal
#   font.tex: EXP +, IMMUNE, DODGE, MISS; клетки и ширины слов те же — код рисует их по оригиналу) в /Game/L10N/en/…;
#   движок подменяет его в игре при культуре en (в PIE — AsdaLangUiAssets). Без ключа — русский атлас, как раньше.
LANG = os.environ.get("ASDA_COMBATFONT_LANG", "").strip().lower()
SRC = r"@@ASTORY_DEV_ROOT@@\Reports\font_tex_en.png" if LANG == "en" else r"@@ASTORY_DEV_ROOT@@\Reports\font_tex.png"
DEST = "/Game/L10N/en/AsdaAssets/UI" if LANG == "en" else "/Game/AsdaAssets/UI"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", SRC)
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", "T_AsdaCombatFont")
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    tex = unreal.load_asset(DEST + "/T_AsdaCombatFont")
    if tex:
        # UI-атлас: без мипов, без sRGB-сюрпризов (цвет = tint), группа UI.
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("never_stream", True)
        unreal.EditorAssetLibrary.save_asset(DEST + "/T_AsdaCombatFont")
        unreal.log("[combatfont] OK: " + DEST + "/T_AsdaCombatFont")
    else:
        unreal.log_error("[combatfont] import FAILED")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
