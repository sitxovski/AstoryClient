# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_textures_never_stream.py
#
# ПРОБЛЕМА: при открытии сумки (и любого окна) картинки предметов первую секунду мыльные, потом
# «прогружаются». То же самое было со стартовой заставкой.
#
# ПРИЧИНА: атласы интерфейса лежат в /Game/AsdaAssets/Textures/JUI и идут через обычную потоковую
# подкачку. Менеджер текстур зовёт SetForceMipLevelsToBeResident(...) — но это ПРОСЬБА подержать
# мипы, а не ожидание: пока подкачка догоняет, окно уже открыто и рисует мелкий мип.
#
# ЛЕЧЕНИЕ: снять потоковую подкачку у текстур интерфейса (never_stream = True). Такая текстура
# грузится целиком сразу вместе с ассетом и мылить не может в принципе.
# Почему это дёшево именно здесь: атласы интерфейса невелики, их немного одновременно на экране,
# и они живут весь сеанс — держать их целиком выгоднее, чем догружать поверх уже видимого окна.
#
# Режимы (переменная окружения ASDA_MODE):
#   fix   — (по умолчанию) снять подкачку и сохранить;
#   audit — только проверить и напечатать, сколько текстур ещё стримятся.
#
# Корни задаются через ASDA_ROOTS (через ';'), по умолчанию — папка интерфейса.
#
# Запуск (headless, редактор ЗАКРЫТ):
#   UnrealEditor-Cmd.exe <проект> -ExecutePythonScript="@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools/ItemDB/asda_ui_textures_never_stream.py"
#     -unattended -nopause -nosplash -nosound
import os

DEFAULT_ROOTS = "/Game/AsdaAssets/Textures/JUI"
BATCH = 200


def main():
    import unreal
    eal = unreal.EditorAssetLibrary
    mode = os.environ.get("ASDA_MODE", "fix").lower()
    roots = [r.strip() for r in os.environ.get("ASDA_ROOTS", DEFAULT_ROOTS).split(";") if r.strip()]

    paths = []
    for root in roots:
        if not eal.does_directory_exist(root):
            unreal.log_warning("no such directory: %s" % root)
            continue
        for a in eal.list_assets(root, recursive=True, include_folder=False):
            paths.append(a.split(".")[0])
    unreal.log("=== UI textures never_stream: mode=%s, roots=%s, assets=%d ===" % (mode, roots, len(paths)))

    streamed = []
    fixed = 0
    for i, pkg in enumerate(paths):
        tex = eal.load_asset(pkg)
        if not isinstance(tex, unreal.Texture2D):
            continue
        if tex.get_editor_property("never_stream"):
            continue
        streamed.append(pkg)
        if mode == "audit":
            continue
        tex.set_editor_property("never_stream", True)
        if eal.save_loaded_asset(tex):
            fixed += 1
        else:
            unreal.log_warning("  FAILED to save: %s" % pkg)
        if fixed and fixed % BATCH == 0:
            unreal.log("  saved %d ..." % fixed)

    if mode == "audit":
        if streamed:
            unreal.log_warning("=== AUDIT: %d UI texture(s) still streamed (they can look blurry on open) ===" % len(streamed))
            for p in streamed[:20]:
                unreal.log_warning("    %s" % p)
            if len(streamed) > 20:
                unreal.log_warning("    ...and %d more" % (len(streamed) - 20))
        else:
            unreal.log("=== AUDIT OK: no streamed UI textures ===")
    else:
        unreal.log("=== done: never_stream set on %d of %d ===" % (fixed, len(streamed)))


if __name__ == "__main__":
    main()
