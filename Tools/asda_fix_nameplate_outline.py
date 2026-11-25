# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_nameplate_outline.py — ТОЧЕЧНЫЙ возврат обводки на существующие над-головные плашки.
# НЕ перегенерация (генератор перезаписал бы ручные правки) — только параметры шрифта текст-контролов:
#   размер 8 + Regular + обводка 1px чёрная (эталон TextFont_BuildGlyphQuad@0x12C5B80: крест ±1px, 0xFF000000).
#   ★ 2026-09-24: начертание Regular, а не Bold — жирной гарнитуры в оригинале нет нигде,
#   атлас FONT0.BIN собран весом 400 (см. шапку `asda_restyle_ui_font.py`).
# Обводка «слетела» при перегенерации плашек 2026-07-29 (фикс цветов полос): её накатывал отдельный
#   asda_restyle_ui_font.py по всем окнам, а новый WBP создаётся без неё.
# Запуск: run_headless.bat asda_fix_nameplate_outline.py
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

B = unreal.AsdaUmgBuilder
PLATES = [
    "/Game/AsdaAssets/UI/GameMain/WBP_NAMEPLATE_MON",
    "/Game/AsdaAssets/UI/GameMain/WBP_NAMEPLATE_MAN",
    "/Game/AsdaAssets/UI/GameMain/WBP_NAMEPLATE_MAN1",
]


def log(m): unreal.log_warning("[plate-outline] %s" % m)


def main():
    for path in PLATES:
        bp = unreal.load_asset(path)
        if not bp:
            log("НЕТ ассета: %s" % path); continue
        n  = B.restyle_text_fonts(bp, 8, unreal.Name("Regular"))
        n += B.apply_text_outline(bp, 1, unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
        B.compile_and_save(bp)
        saved = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        log("%s: контролов изменено %d, сохранён=%s" % (path.split("/")[-1], n, saved))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
