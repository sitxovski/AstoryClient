# Copyright (c) 2026 @sitxovski (Discord)
# _import_new_field_bgm.py — импорт трека S_BGM_NEW_Field (копия stagnantdesert).
# Пустыня в заголовке своей карты просит трек `S_BGM_NEW_Field`, которого нет НИ В ОДНОЙ сборке клиента
# (проверены 5 клиентов) — решение юзера 2026-08-01: отдать пустыне её же `S_BGM_stagnantdesert`.
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

SRC = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Sounds\Music\S_BGM_NEW_Field.wav"
DST = "/Game/AsdaAssets/Sounds/Music"
NAME = "S_BGM_NEW_Field"


def log(m): unreal.log_warning("[bgm] %s" % m)


def main():
    pkg = "%s/%s" % (DST, NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(pkg):
        log("уже импортирован: %s" % pkg); return
    task = unreal.AssetImportTask()
    task.filename = SRC
    task.destination_path = DST
    task.destination_name = NAME
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if not unreal.EditorAssetLibrary.does_asset_exist(pkg):
        log("ОШИБКА: ассет не создан"); return
    snd = unreal.EditorAssetLibrary.load_asset(pkg)
    # BGM зациклен (эталон: FSOUND_Stream_Open ... 0x02 = LOOP_NORMAL)
    ref = unreal.EditorAssetLibrary.load_asset("%s/S_BGM_stagnantdesert" % DST)
    for prop in ("looping", "sound_class_object", "volume_multiplier"):
        try:
            if ref: snd.set_editor_property(prop, ref.get_editor_property(prop))
        except Exception:
            pass
    try: snd.set_editor_property("looping", True)
    except Exception: pass
    unreal.EditorAssetLibrary.save_asset(pkg)
    log("импортирован %s (looping=%s, длительность=%.1f c)" % (
        pkg, snd.get_editor_property("looping"), snd.get_editor_property("duration")))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
