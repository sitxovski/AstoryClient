# Copyright (c) 2026 @sitxovski (Discord)
# asda_restyle_ui_font.py — МАССОВАЯ правка шрифта во ВСЕХ существующих UI-виджетах.
#
# ★ НЕ ПЕРЕГЕНЕРАЦИЯ: окна содержат много РУЧНЫХ правок (выравнивания, фиксы рамок, логика) — пересоздание
#   их уничтожит. Здесь мы открываем каждый WBP, проходим дерево виджетов и меняем ТОЛЬКО параметры шрифта
#   у текстовых контролов (размер + начертание), остальное не трогаем.
#
# ЗАМЕР ОРИГИНАЛА (RE/reports/entity/font_and_name_colors_re.md): пиксельный атлас FONT0.BIN+DATA0.TEX,
#   глиф 14 px, CAP-HEIGHT 8 px, шаг строки 12, масштабирования нет, ВТОРОГО КЕГЛЯ В ИГРЕ НЕТ
#   (чат/окна/плашки — один размер). Наш Arial при Size 13 давал cap 12.4 px = 1.55× крупнее → нужен 8.
#
# ★★★ НАЧЕРТАНИЕ — REGULAR (исправлено 2026-09-24, прежнее «Bold по требованию» было ОШИБКОЙ).
#   Доказано побайтово на живом клиенте `Donors\AsdaChroniclesInterlude_MINE\JUIDataont\FONT0.BIN`
#   (7136 б = 4096 + 32-байтовые записи): атлас собран `CreateFontA(..., cHeight 14, вес 400, "Arial",
#   charset 204)` — подбор по 8 гарнитурам × 10 высотам × 2 весам даёт совпадение Arial/14/400 в
#   222 глифах из 223 (Σ|Δ| = 1), Arial Bold — только 131 из 223.
#   Код: `UIFont_InitByLanguage` (ui_core.c:106523-106556) при языке 25 → cHeight 12/"Arial"/charset 204;
#        `fontBuildGlyphAtlasForLang` (font_text.c:6515-6600) → `CreateFontA(..., boldFlag ? 700 : 400, ...)`,
#        а `boldFlag` инициализируется НУЛЁМ (ui_core.c:106690) ⇒ вес 400 = Regular.
#   «ЖИРНЫЙ» у оригинала — НЕ гарнитура: тег `|FB` (font_text.c:7971-7977) ставит `styleFlags |= 0x200000`,
#   а обработчик (font_text.c:6832-6886) рисует ТОТ ЖЕ квад ДВАЖДЫ: `penX += 1.0` → квад → `penX -= 1.0`
#   → квад, то есть в `x+1` и ОБРАТНО в `x` (★ поправка 24.09.2026: НЕ «penX+1 и penX-1» — второй квад
#   возвращается в исходную точку). Продвижение пера НЕ меняется ⇒ ширина строки у оригинала ВСЕГДА
#   Regular-овская. Стиль по умолчанию 0x100000 = обычная одиночная отрисовка (ui_core.c:106702).
#   Наш приём (`ShadowOffset (1,0)` цветом заливки) даёт РОВНО ЭТИ ЖЕ позиции — 1:1, не приближение;
#   сравнительный рендер: `RE/reports/font_regular_2026-09-24/font_pseudobold_variants.png`, сет 1 = сет 5.
#   Цена дефекта: наш Bold = 106.3 % ширины оригинала (медиана 105.4), Arial Regular = 97.9 % — по корпусу
#   945 русских подписей. По снимку `RawAssets/Data/ui_snapshot_after.json` в слот не влезал 61 контрол,
#   при Regular не влезают 27 (их слот мал и в оригинале — см. `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`).
#   Псевдо-жирный там, где оригинал ставит `|FB`, расставляет `asda_ui_text_style926.py` (колонка `bold`).
#
# ★ ОБВОДКА ЗДЕСЬ БОЛЬШЕ НЕ СТАВИТСЯ (OUTLINE_PX = None). Владелец обводки — шаг
#   `asda_ui_text_style926.py`: он приводит `OutlineSize` к ДАННЫМ ОРИГИНАЛА (поле 926, таблица
#   `Tools/data/text_style926.tsv` — обводка положена 747 контролам, а не всем 7862). Прежнее
#   безусловное `apply_text_outline(bp, 1, чёрный)` в этом шаге ЗАТИРАЛО бы результат 926-го
#   (обводка вернулась бы на ~6300 лишних контролов — три круга жалоб владельца, см. шапку того файла).
#   Вернуть обводку разово можно аргументом `outline=1`.
#
# ★★★ ПРАВИЛО НА БУДУЩЕЕ: ГАРНИТУРУ ЗАДАВАТЬ ЯВНО ВСЕГДА, ИНАЧЕ ДЕФЕКТ ВЕРНЁТСЯ САМ.
#   ОТКУДА БРАЛСЯ `Bold`. В ассетах гарнитуры НЕ БЫЛО ВООБЩЕ — все 6648 переходов этой волны
#   это «свойства нет» → `Regular`. `Bold` приходил из CDO движка:
#   `Engine/Source/Runtime/UMG/Private/Components/TextBlock.cpp:36`
#   `Font = FSlateFontInfo(RobotoFontObj.Object, 24, FName("Bold"))`. Тегированная сериализация
#   пишет только ОТЛИЧИЯ от архетипа, поэтому наше прежнее явное `"Bold"` СОВПАДАЛО с дефолтом
#   CDO и в пакет не попадало — и при загрузке бралось из CDO.
#   ⇒ ЛЮБОЙ НОВЫЙ `TextBlock` БЕЗ ЯВНО ЗАДАННОЙ ГАРНИТУРЫ СНОВА БУДЕТ `Bold`. Любой будущий
#   импорт, пост-шаг или ручное добавление подписи в редакторе вернёт дефект молча.
#   Теперь `Regular` ОТЛИЧАЕТСЯ от дефолта CDO и пишется в пакет — но только там, где мы его
#   поставили. Новые узлы обязаны получать гарнитуру явно (`AsdaUmgBuilder::AddText` ставит её
#   сам; прямое `ConstructWidget<UTextBlock>` — ответственность вызывающего).
#   Этим же объясняются и 26 окон сцены логина: у них 299 узлов без гарнитуры.
#
# ★★★ ОКНА СЦЕНЫ ЛОГИНА — РЕШЕНИЕ ВЛАДЕЛЬЦА ОТ 24.09.2026: НЕ ПРАВИТЬ.
#   Сторож `asda_login_guard` пропускает 26 окон `/Game/AsdaAssets/UI/Login/` (273 текстовых узла),
#   и они ОСТАЮТСЯ на `Bold`-гарнитуре. Это НЕ долг и НЕ «ждёт разрешения»: владелец постановил
#   оставить как есть, разрешения на `ASDA_ALLOW_LOGIN_WRITE` не будет, сторож не отключается,
#   расхождение с оригиналом по этим окнам принято сознательно.
#   ⇒ Строка `[LOGIN-GUARD] … окон сцены логина пропущено: 26` в конце прогона — ОЖИДАЕМА и верна,
#   это не сбой шага. Не предлагать перевод, не просить разрешения. Поимённый список окон и
#   последствия — `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`, поправка «… ОКНА ЛОГИНА, РЕШЕНИЕ ВЛАДЕЛЬЦА».

# ★★★ ПОСЛЕ ЭТОГО ШАГА ОБЯЗАТЕЛЬНО ПЕРЕЗАПУСТИТЬ ДВА ШАГА, КОТОРЫЕ ОН ЗАТИРАЕТ:
#     1) `asda_ui_text_style926.py` — обводка/тень/псевдо-жирный по данным поля 926;
#     2) `asda_ui_partyfind_map_captions.py` — у 16 подписей карт окна 8500 кегль НАМЕРЕННО 7,
#        а не 8 (`FIT_FONT_SIZE`: самое длинное русское имя карты не влезает в плитку 148 px даже
#        Regular-ом). Этот шаг гонит по ВСЕМ текстам один кегль и сбрасывает их в 8 — поймано
#        прогоном 24.09.2026, восстановлено перезапуском того шага. Оба шага идемпотентны.
#
# Запуск (headless): UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=asda_restyle_ui_font.py
#   Аргументы: [размер] [Bold|Regular] [outline=N]   (по умолчанию 8 Regular, обводку не трогаем)
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

FONT_PX  = 8
TYPEFACE = "Regular"   # ★ 2026-09-24: Regular, а не Bold — обоснование в шапке
# ★ ОБВОДКА (замер оригинала, font_and_name_colors_re.md раздел «Обводка текста»):
#   TextFont_BuildGlyphQuad@0x12C5B80 рисует глиф 5 проходами — 4 копии со смещением ±1 px по X и Y
#   (КРЕСТ, диагоналей нет) цветом shadowColor + основной проход. Размер РОВНО 1 px в экранных
#   координатах (не зависит от кегля), цвет жёстко 0xFF000000. Стиль включается битом 0x800000 поля 926.
#   Обводка стоит в 111 местах: плашки (имя/гильдия/титул), имя цели, тултип NPC, чат, 63 места общего UI.
OUTLINE_PX = None   # ★ None = обводку НЕ трогаем (её владелец — asda_ui_text_style926.py)
OUTLINE_COLOR = unreal.LinearColor(0.0, 0.0, 0.0, 1.0)

ROOTS = ["/Game/AsdaAssets/UI"]


def log(m): unreal.log_warning("[restyle] %s" % m)


# WidgetTree из Python недоступен (protected) → правку делает C++ билдер, у него есть доступ.
B = unreal.AsdaUmgBuilder


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = []
    for r in ROOTS:
        assets += ar.get_assets_by_path(r, recursive=True)
    wbps = [a for a in assets if str(a.asset_class_path.asset_name) == "WidgetBlueprint"]
    # ★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): окна UI/Login (кроме общих LOGIN_SHARED_EXCEPTIONS) —
    #   пакеты сцены логина; без ASDA_ALLOW_LOGIN_WRITE=1 не загружаются и не сохраняются. Итог — одной строкой [LOGIN-GUARD].
    import os as _os_lg
    sys.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    import asda_login_guard as _LG
    login_skipped = [str(a.package_name) for a in wbps
                     if _LG.login_package_blocked(str(a.package_name), "asda_restyle_ui_font.py", quiet=True)]
    if login_skipped:
        wbps = [a for a in wbps if str(a.package_name) not in login_skipped]
        unreal.log_warning("[LOGIN-GUARD] [restyle] окон сцены логина пропущено: %d (%s%s) — шрифт и обводка не менялись (только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"
                           % (len(login_skipped), ", ".join(p.rsplit("/", 1)[-1] for p in login_skipped[:5]),
                              " …" if len(login_skipped) > 5 else ""))
    log("виджет-блюпринтов найдено: %d (размер=%d, начертание=%s, обводка=%s)"
        % (len(wbps), FONT_PX, TYPEFACE,
           "не трогаем (владелец — asda_ui_text_style926.py)" if OUTLINE_PX is None else "%dpx чёрная" % OUTLINE_PX))

    total_assets = total_ctrls = 0
    for a in wbps:
        path = str(a.package_name)
        bp = unreal.load_asset(path)
        if not bp:
            continue
        n = B.restyle_text_fonts(bp, FONT_PX, unreal.Name(TYPEFACE))
        # ★ Обводку трогаем ТОЛЬКО по явному аргументу `outline=N` — иначе её ставит 926-й шаг по данным.
        if OUTLINE_PX is not None:
            n += B.apply_text_outline(bp, OUTLINE_PX, OUTLINE_COLOR)
        if n:
            unreal.EditorAssetLibrary.save_loaded_asset(bp)
            total_assets += 1; total_ctrls += n
            log("%-44s текстов изменено: %d" % (path.split("/")[-1], n))
    log("ИТОГО: окон изменено %d, текстовых контролов %d" % (total_assets, total_ctrls))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global FONT_PX, OUTLINE_PX, TYPEFACE, a
    for a in sys.argv[1:]:
        if a.isdigit(): FONT_PX = int(a)
        elif a in ("Bold", "Regular"): TYPEFACE = a
        elif a.startswith("outline="): OUTLINE_PX = int(a.split("=")[1])
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
