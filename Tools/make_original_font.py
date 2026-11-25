# Copyright (c) 2026 @sitxovski (Discord)
# ★★★ ВНИМАНИЕ: ЭТОТ СКРИПТ НЕ ПРИМЕНЯТЬ. Он оставлен как ЗАПИСЬ ОТВЕРГНУТОЙ ВЕРСИИ.
#
# ЧТО ЗДЕСЬ БЫЛО НАПИСАНО НЕВЕРНО (исправлено 2026-09-24). Шапка утверждала, что оригинальный шрифт
#   игры — это `juidata/font/Base/arial.ttf`, внутри которого лежит Tahoma. Файл ДЕЙСТВИТЕЛЬНО
#   содержит Tahoma (name-таблица: запись 1 = "Tahoma", запись 5 = "Version 3.15"), НО АТЛАС ИГРЫ
#   СОБРАН НЕ ИМ.
# ЧЕМ СОБРАН НА САМОМ ДЕЛЕ. Оригинал вообще не растеризует TTF в рантайме: `LoadEngineFont`
#   (ui_core.c:106592-106660) грузит ГОТОВЫЙ растровый атлас `<клиент>\JUIData\font\DATA0..199.TEX`
#   плюс метрики `FONT0.BIN`. Сам атлас печётся `fontBuildGlyphAtlasForLang`
#   (font_text.c:6515-6600) вызовом `CreateFontA(..., cHeight 14, вес `boldFlag ? 700 : 400`,
#   "Arial", charset 204)`, причём `boldFlag` инициализируется НУЛЁМ (ui_core.c:106690) ⇒ вес 400.
# ЧЕМ ДОКАЗАНО. Перебор 8 гарнитур × 10 высот × 2 весов через `CreateFontA` +
#   `GetTextExtentPointA` против живого `Donors\AsdaChroniclesInterlude_MINE\JUIData\font\FONT0.BIN`
#   (7136 б = 4096 + 32-байтовые записи): Arial / cHeight 14 / вес 400 совпадает в 222 глифах
#   из 223 (Σ|Δ| = 1; расходятся 0x98, не определённый в cp1251, и 0xB7). Tahoma даёт Σ|Δ| в разы
#   хуже, Arial Bold — 131 глиф из 223.
# ⇒ В ассетах стоит и должен стоять `C:/Windows/Fonts/arial.ttf` (UFontFace `AsdaUIFont`,
#   гарнитура `Regular` композитного `AsdaUIFontRT`). Запуск этого скрипта ПОДМЕНИТ его на Tahoma
#   и разъедет ширины подписей заново. Следующая волна: НЕ ИДТИ ПО ЭТОМУ СЛЕДУ.
#
#   Запуск (только если владелец сознательно хочет Tahoma):
#   UnrealEditor-Cmd ... -ExecutePythonScript=make_original_font.py
import unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

SRC = _os.path.join(_P.JUI_FONT_DIR, "Base", "arial.ttf")
DIR = "/Game/AsdaAssets/Fonts"
FACE = DIR + "/AsdaUIFont"        # UFontFace (заменяем содержимое на Tahoma TTF)
RT   = DIR + "/AsdaUIFontRT"      # UFont (runtime), Typeface → FACE


# 2) AsdaUIFontRT (UFont, cache=Runtime) ссылается на AsdaUIFont-ассет по ССЫЛКЕ (FFontData(face)).
#    Content ассета заменён на TTF Tahoma → RT рендерит Tahoma автоматически, пересоздавать RT НЕ нужно.
#    (Python-API не даёт unreal.TypefaceEntry; пересборка RT — только C++ GetOrCreateAsdaUIFont@AsdaUmgBuilder.)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # 1) Импорт TTF → UFontFace AsdaUIFont (replace)
    task = unreal.AssetImportTask()
    task.filename = SRC
    task.destination_path = DIR
    task.destination_name = "AsdaUIFont"
    task.replace_existing = True
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    face = unreal.load_asset(FACE)
    unreal.log(f"[font] AsdaUIFont импортирован из arial.ttf(Tahoma): {'OK' if face else 'FAIL'}")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
