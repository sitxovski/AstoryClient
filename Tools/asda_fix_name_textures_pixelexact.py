# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_name_textures_pixelexact.py — привести текстуры названий локаций (name_*) к ПИКСЕЛЬ-В-ПИКСЕЛЬ выводу.
#
# ЭТАЛОН (npcwin_DrawMapNpcImage@0xDA3330): картинка выводится 1:1 в ПИКСЕЛЯХ экрана —
#   CControl_DrawTextureRect(ctrl, name, 0, 0, texW, texH); vtbl+20 → SetSize(texW, texH);
#   x = (int)(g_ScreenW*0.5 − w/2), y = (int)(g_ScreenH*0.25 − h/2)   ← целочисленно, БЕЗ масштабирования.
# Значит в UE от текстуры требуется: без сжатия (артефакты DXT вокруг букв = «изуродована»),
#   без мип-мап (иначе на 1:1 берётся мип и получается мыло), фильтр Nearest (без ресемплинга),
#   группа UI, sRGB, без стриминга (иначе первый кадр — низкий мип).
#
# Печатает свойства ДО и ПОСЛЕ по каждой текстуре. Запуск:
#   Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_fix_name_textures_pixelexact.py
import unreal

EAL = unreal.EditorAssetLibrary
DIRS = ["/Game/AsdaAssets/Textures/NPC/Image", "/Game/AsdaAssets/Textures/NPC/Portrait"]

WANT = {
    # UserInterface2D (RGBA) — БЕЗ сжатия: у 512×64 с резким текстом BC-артефакты очень заметны.
    "compression_settings": unreal.TextureCompressionSettings.TC_EDITOR_ICON,
    "mip_gen_settings":     unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
    "filter":               unreal.TextureFilter.TF_NEAREST,
    "lod_group":            unreal.TextureGroup.TEXTUREGROUP_UI,
    "srgb":                 True,
    "never_stream":         True,
}


def val(x):
    try:
        return x.name
    except Exception:
        return str(x)


changed = 0
total = 0


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global changed, total
    for d in DIRS:
        if not EAL.does_directory_exist(d):
            continue
        for path in sorted(EAL.list_assets(d, recursive=False, include_folder=False)):
            name = path.split(".")[-1] if "." in path else path.split("/")[-1]
            if not name.startswith("name_"):
                continue
            t = EAL.load_asset(path)
            if not isinstance(t, unreal.Texture2D):
                continue
            total += 1
            before = {k: val(t.get_editor_property(k)) for k in WANT}
            diff = {}
            for k, v in WANT.items():
                if t.get_editor_property(k) != v:
                    t.set_editor_property(k, v)
                    diff[k] = (before[k], val(v))
            size = "%dx%d" % (t.blueprint_get_size_x(), t.blueprint_get_size_y())
            if diff:
                EAL.save_loaded_asset(t)
                changed += 1
                unreal.log("%s (%s) ИСПРАВЛЕНО: %s" % (name, size, "; ".join(
                    "%s %s→%s" % (k, a, b) for k, (a, b) in diff.items())))
            else:
                unreal.log("%s (%s) уже верно: %s" % (name, size, ", ".join(
                    "%s=%s" % (k, before[k]) for k in WANT)))
    unreal.log("=== ГОТОВО: текстур названий %d, исправлено %d ===" % (total, changed))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
