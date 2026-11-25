# Copyright (c) 2026 @sitxovski (Discord)
# Пустой WBP_ResPicker (root CanvasPanel) — конкретный UUserWidget-класс для рантайм-наполнения C++ пикером.
#   (CreateWidget из базового UUserWidget запрещён UE → нужен concrete WBP.) Запуск: run_headless.bat make_res_picker_wbp.py
import unreal
B = unreal.AsdaUmgBuilder
PKG = "/Game/AsdaAssets/UI/Dev"
full = PKG + "/WBP_ResPicker"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if not unreal.EditorAssetLibrary.does_directory_exist(PKG):
        unreal.EditorAssetLibrary.make_directory(PKG)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        unreal.EditorAssetLibrary.delete_asset(full)
    bp = B.create_widget_bp(PKG, "WBP_ResPicker")   # создаёт WBP с root CanvasPanel "RootCanvas"
    if bp:
        B.compile_and_save(bp)
        unreal.EditorAssetLibrary.save_loaded_asset(bp)
        unreal.log("[res_picker] WBP_ResPicker создан")
    else:
        unreal.log_warning("[res_picker] не создан")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
