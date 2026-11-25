# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_partyfind_map_captions.py — ★★★ ЧИТАЕМЫЕ ПОДПИСИ ПЛИТКИ КАРТ ОКНА «ПОИСК ОТРЯДА» (8500).
#
#   ЖАЛОБА ВЛАДЕЛЬЦА (2026-08-30, по кадру из игры): «список локаций пиздец тёмный».
#
#   ЭТО ДЕФЕКТ ОРИГИНАЛА, А НЕ НАША ОШИБКА — доказано данными:
#     • подложка плитки = тинт `Diffuse` состояния SS0 скина (`RE/docs/JuiSkin.ini`):
#         `MapName_Normal` (строка 64687) = FF423406 — почти чёрный оливковый;
#         `MapName_InDun`  (строка 38820) = FF3A1105 — почти чёрный коричневый;
#         `MapName_Elite`  (строка 38756) = FF0F2B09 — почти чёрный зелёный;
#     • цвет ПОДПИСИ берётся не из скина, а из поля `CaptionInfo` самого контрола
#       (`RE/docs/gamemain_jui.ini`, срез — `Reports/_partyfind_jui.tsv`, строки 212…237):
#         обычные карты  ff6d6341, подземелья ff69453d, элитные ff3f5e35.
#     Относительная яркость (WCAG): подпись 0.123 / 0.075 / 0.093 против подложки
#     0.036 / 0.013 / 0.018 ⇒ КОНТРАСТ 2.01 / 1.98 / 2.09 при норме 4.5 для мелкого текста.
#     То есть в оригинале подписи карт физически не читаются.
#
#   ЧИНИМ ПО ПРАВИЛУ `fix-original-defects` И НИЧЕГО НЕ ВЫДУМЫВАЕМ (`never-invent-only-original`):
#   красим подписи в цвет, который ТОТ ЖЕ САМЫЙ скин уже задаёт ЭТОЙ ЖЕ кнопке для состояния
#   наведения SS1 (`CaptionColor`): FFE4C67C / FFF08D68 / FFAFE9A9. Контраст становится
#   7.35 / 6.88 / 10.96. Ни одного цвета «из головы» здесь нет.
#
#   ПОЧЕМУ НЕ ОСВЕТЛЯЕМ ПОДЛОЖКУ. Считали: подъём тинта до, скажем, #6E5A0B роняет контраст со
#   светлой подписью до 4.06, а с исходной тёмной — почти не поднимает. Тёмная плитка при этом
#   фирменный вид оригинала. Меняем ровно то, что мешает читать, и ничего сверх.
#
#   ★★★ ВТОРОЙ ДЕФЕКТ ТОЙ ЖЕ ПЛИТКИ (жалоба владельца 2026-08-30, вторая волна):
#   «Проклятый Дворец Королевы» ДЛИННЕЕ плитки и затекает в соседний столбец.
#   Плитка 148x17, подпись — `TextBlock` 148x17, шрифт `AsdaUIFontRT` Bold 8, перенос выключен,
#   обрезка `Inherit` ⇒ текст, не влезший в 148 px, рисуется ЗА пределами своего слота и наезжает
#   на соседнюю плитку. Русские имена карт длиннее корейских: 11809 «Проклятый Дворец Королевы»
#   и 11811 «Проклятый Шахматный Замок» — по 25 знаков.
#   ⚠ Переносом НЕ лечим: `ui-autowrap-canvas-gotcha` — авто-перенос в канвасе даёт дёрганье,
#     да и высота плитки 17 px, вторая строка туда не встанет.
#   Лечим ДВУМЯ независимыми мерами, обе — уровня ассета и идемпотентные:
#     1) `Clipping = ClipToBounds` всем 16 подписям — ЖЁСТКАЯ гарантия, что подпись никогда не
#        вылезет за свою плитку, чем бы её потом ни заполнили;
#     2) размер шрифта 8 -> 7 всем 16 (единообразно, а не двум длинным — иначе строка станет
#        разнокалиберной), чтобы самые длинные имена ВЛЕЗАЛИ, а не обрезались.
#   Откат — тот же ключ `asda.PartyFind.LegacyMapCaptions 1` (возвращает и цвет, и размер 8,
#   и обрезку `Inherit`).
#
#   ★ ОТКАТ — в рантайме: `asda.PartyFind.LegacyMapCaptions 1` возвращает эталонные тёмные цвета
#     (`Private/UI/AsdaPartyFindSubsystem.cpp`, ветка в `SetupControls`).
#
#   ЗАПУСК: автоматически из `asda_ui_post_import.py` (шаг в `STEPS`), либо вручную:
#     Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_partyfind_map_captions.py
#   ⚠ Шаг ОБЯЗАН быть в `STEPS`: перегенерация окна из `.JUI` вернёт тёмные цвета из данных.
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`). Также `--dry-run` /
#   ASDA_POSTSTEPS_DRYRUN=1: логика та же, запись ассетов подавлена, итог — «[ШАГ-ДЕЛЬТА] … изменено узлов N».
DRYRUN_ENV = "ASDA_PARTYFIND_DRYRUN"

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

# Окно «Поиск отряда» (JUI 8500).
WBP = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_Find_Party_WIn"

# Категория плитки → (цвет SS1 того же скина, цвет в данных .JUI, имена подписей).
#   ⚠ Пустые слоты разметки `MapN07/N08/N09` и `MapD04` (`IDNum = 4294967295`) сюда НЕ входят:
#   у них нет ни номера, ни подписи — импортёр дал им лишь заглушку `XXX_*`.
GROUPS = [
    ("обычные карты", "MapName_Normal", "FFE4C67C", "ff6d6341", [
        "Find_Party_MapN00_Text", "Find_Party_MapN01_Text", "Find_Party_MapN02_Text",
        "Find_Party_MapN03_Text", "Find_Party_MapN04_Text", "Find_Party_MapN05_Text",
        "Find_Party_MapN06_Text",
    ]),
    ("подземелья", "MapName_InDun", "FFF08D68", "ff69453d", [
        "Find_Party_MapD00_Text", "Find_Party_MapD01_Text", "Find_Party_MapD02_Text",
        "Find_Party_MapD03_Text",
    ]),
    ("элитные", "MapName_Elite", "FFAFE9A9", "ff3f5e35", [
        "Find_Party_MapE00_Text", "Find_Party_MapE01_Text", "Find_Party_MapE02_Text",
        "Find_Party_MapE03_Text", "Find_Party_MapE04_Text",
    ]),
]

# ── ПОСАДКА ПОДПИСИ В ПЛИТКУ (второй дефект) ──
#   Размер в данных/ассете — 8; 7 подобран так, чтобы самое длинное имя (25 знаков,
#   11809/11811) влезало в 148 px. Проверено кадром, не на глаз.
# ★★★ 07.10.2026 — КЕГЛЬ ВОЗВРАЩЁН К 8 (решение владельца, вариант А: окно 8500 шире на 68 целиком, плитки снова 148 —
#   шаг `asda_ui_widen_windows`, отчёт `Reports/owner_1007/label_band_and_8500.md`). Самое длинное имя кеглем 8 ≈ 143 px
#   входит в плитку 148; обрезка ClipToBounds остаётся гарантией. Кегль 7 был вынужденной мерой при сетке 134/148.
FIT_FONT_SIZE = 8
LEGACY_FONT_SIZE = 8


def _srgb_to_linear(c):
    """sRGB [0..1] -> LINEAR [0..1] — закон `ui-srgb-gamma-rule` (UE трактует цвет как линейный)."""
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _hex_to_linear(h):
    """AARRGGBB (sRGB) -> unreal.LinearColor. Альфа НЕ гамма-корректируется."""
    a = int(h[0:2], 16) / 255.0
    r = _srgb_to_linear(int(h[2:4], 16) / 255.0)
    g = _srgb_to_linear(int(h[4:6], 16) / 255.0)
    b = _srgb_to_linear(int(h[6:8], 16) / 255.0)
    return unreal.LinearColor(r, g, b, a)


def _find_text(bp, name):
    """TextBlock по имени. ⚠ `WidgetBlueprint.widget_tree` из Python не заэкспорчен — обход идёт
    от корневого канваса, ровно как в `asda_ui_text_color_tweaks._find_text`."""
    root = B.get_root_canvas(bp)
    found = [None]

    def walk(w):
        if found[0] is not None or w is None:
            return
        if w.get_name() == name and isinstance(w, unreal.TextBlock):
            found[0] = w
            return
        try:
            kids = w.get_all_children()
        except Exception:                                        # noqa: BLE001
            return
        for c in kids:
            walk(c)

    walk(root)
    return found[0]


def main():
    """★ ИДЕМПОТЕНТЕН. `apply_jui_text_colors` возвращает число ИЗМЕНЁННЫХ узлов, и на втором
    прогоне это ноль — падать по нему нельзя. Проверяем НАЛИЧИЕ узла и ИТОГОВЫЙ цвет."""
    if not EAL.does_asset_exist(WBP):
        unreal.log_error("[карты-поиска-отряда] нет ассета %s" % WBP)
        raise RuntimeError("asset %s not found" % WBP)
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_ui_partyfind_map_captions", dry)  # учёт правок шага по снимкам дерева
    bp = EAL.load_asset(WBP)
    delta.take(bp)

    total, changed = 0, 0
    for human, skin, hexcol, jui, nodes in GROUPS:
        want = _hex_to_linear(hexcol)
        names = unreal.Array(str)
        cols = unreal.Array(unreal.LinearColor)
        for n in nodes:
            if _find_text(bp, n) is None:
                unreal.log_error("[карты-поиска-отряда] узел НЕ НАЙДЕН: %s" % n)
                raise RuntimeError("text node %s missing in %s" % (n, WBP))
            names.append(n)
            cols.append(want)
        # bOnlyIfPlaceholder = False: красим безусловно — значение из .JUI не плейсхолдер.
        changed += B.apply_jui_text_colors(bp, names, cols, False, unreal.LinearColor(0, 0, 0, 0))
        total += len(nodes)
        unreal.log("[карты-поиска-отряда] %s (%s): %d подписей → %s (в данных %s, контраст с "
                   "подложкой скина был ~2.0)" % (human, skin, len(nodes), hexcol, jui))

    # ── ПОСАДКА В ПЛИТКУ: обрезка + размер шрифта (второй дефект, см. шапку) ──
    #   Обе операции ИДЕМПОТЕНТНЫ: правим, только если значение отличается от нужного.
    fitted = 0
    for _h, _s, _c, _j, nodes in GROUPS:
        for n in nodes:
            node = _find_text(bp, n)
            # 1) Жёсткая обрезка по своей плитке — подпись физически не может налезть на соседнюю.
            if str(node.get_editor_property("clipping")) != str(unreal.WidgetClipping.CLIP_TO_BOUNDS):
                node.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
                changed += 1
            # 2) Размер шрифта: FIT_FONT_SIZE (с 07.10 — 8, как у импортёра; плитки 148 при окне +68).
            f = node.get_editor_property("font")
            if abs(float(f.size) - float(FIT_FONT_SIZE)) > 1e-3:
                f.size = FIT_FONT_SIZE
                node.set_editor_property("font", f)
                changed += 1
            fitted += 1
    unreal.log("[карты-поиска-отряда] посадка в плитку: %d подписей — обрезка ClipToBounds, "
               "шрифт %d (данные) -> %d (перенос НЕ включаем, закон ui-autowrap-canvas-gotcha)"
               % (fitted, LEGACY_FONT_SIZE, FIT_FONT_SIZE))

    if changed > 0:
        # ⚠ `compile_and_save` вопреки имени НЕ пишет пакет (AsdaUmgBuilder.cpp:610-615) —
        #   запись за Python, иначе правка живёт только в памяти прогона.
        B.compile_and_save(bp)
        if not SD.save(bp, dry, "asda_ui_partyfind_map_captions"):
            raise RuntimeError("не сохранён %s" % WBP)
    delta.done(bp)

    # Сверка ПОСЛЕ: цвет обязан стать заданным (иначе правка молча не доехала).
    for _human, _skin, hexcol, _jui, nodes in GROUPS:
        want = _hex_to_linear(hexcol)
        for n in nodes:
            cur = _find_text(bp, n).get_editor_property("color_and_opacity").specified_color
            if (abs(cur.r - want.r) > 2e-3 or abs(cur.g - want.g) > 2e-3
                    or abs(cur.b - want.b) > 2e-3):
                raise RuntimeError("цвет подписи %s НЕ применён" % n)
            node = _find_text(bp, n)
            if str(node.get_editor_property("clipping")) != str(unreal.WidgetClipping.CLIP_TO_BOUNDS):
                raise RuntimeError("обрезка подписи %s НЕ применена" % n)
            if abs(float(node.get_editor_property("font").size) - float(FIT_FONT_SIZE)) > 1e-3:
                raise RuntimeError("размер шрифта подписи %s НЕ применён" % n)
    unreal.log("=== ПОДПИСИ КАРТ 8500: подтверждено %d/%d (правок в ассете %d)%s ==="
               % (total, total, changed, " (СУХОЙ)" if dry else ""))
    delta.finish()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    main()
