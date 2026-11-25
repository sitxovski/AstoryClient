# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_widen_windows.py — ★★★ РАСШИРЕНИЕ ОКОН ЦЕЛИКОМ ПОД РУССКИЙ ТЕКСТ (07.10.2026, решение владельца, память
#   widen-windows-whole-keep-symmetry; отчёт `Reports/owner_1007/widen_windows.md`). [ОТСТУПЛЕНИЕ ПОРТА ПОД RU] —
#   правка АССЕТОВ, кода не касается (закон no-runtime-widget-fixes).
#
#   Игра сделана под 1024×768, окна оригинала узкие, русский текст шире корейского. Владелец: «лучше изменить макет …
#   можно расширить, если нужно» — окно расширяется ЦЕЛИКОМ и СИММЕТРИЧНО (рамка, шапка, фоны, строки, столбцы),
#   квадратные элементы (ячейки, иконки, кнопки) НЕ растягиваются, а смещаются. Это предпочтительнее кегля 7 и
#   сокращений: где прежние шаги ставили кегль 7 / обрезку ради тех же мест, здесь возвращается кегль 8.
#
#   ДАННЫЕ: закрытая таблица `Tools/data/ui_widen_windows.json` «узел → слот было/стало (+кегль/клип)». Её строит
#   генератор `Tools/asda_ui_widen_windows_plan.py` по ПОЛНОМУ структурному дампу собранного окна и правилам окна
#   (иерархия JUI расплющена — каждый зависимый узел перечислен явно, закон jui-flattened-hierarchy-geometry).
#
#   БЕЗОПАСНОСТЬ (закон owner-asset-edits-never-lose): узел трогается, только если его слот (и кегль/клип у текста)
#   в ТОЧНОСТИ равен «было» таблицы (или одному из допустимых «было» — вид импортёра до прежнего шага). Уже «стало» —
#   готово (идемпотентно). Иное — «осмотреть», окно НЕ пишется, прогон падает. Снимок всего дерева до/после: узлов не
#   потеряно, чужие узлы не изменились, у правленых изменились только смещения слота (и кегль/клип, где заявлено).
#   Ручной запуск пишет только при бэкапе того же размера (закон backup-before-edit). Окна логина — под защитой логина.
#
# Запуск (редактор и игра ЗАКРЫТЫ; коммандлет):
#   python Tools\run_headless_safe.py Tools\asda_ui_widen_windows.py --commandlet-rhi --marker "РАСШИРЕНИЕ-ОКОН: ГОТОВО"
#   ASDA_WIDEN_DRYRUN=1 — сухой прогон; ASDA_WIDEN_ONLY=<подстрока ассета> — только эти окна.
import json
import os
import re   # кисти кнопок: правка export_text (п.6 правок 07.10)
import sys
import traceback

try:
    import unreal
except ImportError:
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

DATA = os.path.join(HERE, "data", "ui_widen_windows.json")
# Бэкап до записи (★ Т3, 11.10.2026): набор `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_widen\<путь от @@ASTORY_DEV_ROOT@@>`
#   (`T._ensure_backup` → `asda_headless_tool.backup_file`; была прошитая папка `2026-10-07_widen`). Первый бэкап хранит
#   состояние до самой первой правки и не перезаписывается; повторная запись того же ассета больше не падает.
BACKUP_SET = "widen"
# ASDA_WIDEN_BACKUP — готовая папка набора целиком (переопределение, как прежде); не задана — None
BACKUP_DIR_OVERRIDE = os.environ.get("ASDA_WIDEN_BACKUP") or None
EPS = 0.05


def log(m):
    """Строка журнала шага."""
    if unreal:
        unreal.log("[расширение-окон] %s" % m)
    else:
        print("[расширение-окон] %s" % m)


def _tb():
    """Общие помощники `asda_ui_text_block_align` (обход, снимок, слоты, бэкап) — одна реализация на все шаги."""
    import asda_ui_text_block_align as T
    return T


def _near(a, b):
    """Покомпонентное равенство с допуском EPS."""
    return len(a) == len(b) and all(abs(float(x) - float(y)) <= EPS for x, y in zip(a, b))


def _clip_name(w):
    """Режим клипа строкой (`INHERIT`, `CLIP_TO_BOUNDS` …)."""
    return str(w.get_editor_property("clipping")).split(".")[-1].split(":")[0].strip().upper()


def _state(w):
    """(смещения L,T,W,H; кегль или None; клип или None) узла."""
    T = _tb()
    s = T._slot_sig(w)
    if s is None:
        return None
    font = clip = None
    if isinstance(w, unreal.TextBlock):
        font = float(w.get_editor_property("font").get_editor_property("size"))
        clip = _clip_name(w)
    return list(s[0]), font, clip, s


def _matches(st, slot, font, clip):
    """Состояние узла равно заданному (кегль/клип сравниваются, только если заданы)."""
    off, f, c, _s = st
    if not _near(off, slot):
        return False
    if font is not None and f is not None and abs(f - float(font)) > EPS:
        return False
    if clip is not None and c is not None and c != clip:
        return False
    return True


# Кисти стиля UCheckBox. Тип CHECK_BOX рисует картинку состояния РАЗМЕРОМ КИСТИ (`image_size`), а не размером слота:
#   сузили слот — без правки кистей картинка вылезает за слот (проверено путём игрока 07.10, сетка карт 8500).
_CB_BRUSHES = ("unchecked_image", "unchecked_hovered_image", "unchecked_pressed_image", "checked_image",
               "checked_hovered_image", "checked_pressed_image", "undetermined_image")


def _vec2(v):
    """Пара чисел из `DeprecateSlateVector2D` (UE 5.8: атрибутов .x/.y нет, `str()` пустой — читаем `export_text`)."""
    import re
    m = re.search(r"X=([-\d.]+),Y=([-\d.]+)", v.export_text())
    return (float(m.group(1)), float(m.group(2))) if m else (float("nan"), float("nan"))


def _cb_sizes(w):
    """Размеры (w, h) кистей состояний UCheckBox (`_CB_BRUSHES`)."""
    st = w.get_editor_property("widget_style")
    return [_vec2(st.get_editor_property(p).get_editor_property("image_size")) for p in _CB_BRUSHES]


# ★ 07.10.2026 (п.6 правок ревизии, `widen_fixes.md`): кисти стиля UButton. Фигурная кнопка-ярлык (скин `TabMd_XBarBtn`,
#   шестиугольник с косыми концами по 9 px) рисуется кистью IMAGE по UV-региону атласа N_01 — при ширине кнопки больше
#   родной (89 → 100) концы растягиваются вместе с серединой и косина искажается на 12 %.
#   ⚠ ДЕВЯТИСРЕЗ BOX ПО АТЛАСУ НЕ РАБОТАЕТ: Slate считает поля в ПИКСЕЛЯХ как «поле × ширина ТЕКСТУРЫ» (512), а в UV —
#     как «поле × ширина UV-региона» (`FSlateElementBatcher::AddBoxElement`, ElementBatcher.cpp:857-860) — концы выходят
#     по 52 px (проверено путём игрока 07.10). Поэтому и импортёр режет девятисрезы на отдельные планы.
#   РЕШЕНИЕ: кисти состояний переводятся на ОТДЕЛЬНУЮ текстуру-вырезку скина 89×19 (`RawAssets/Textures/JUI_Slices`,
#   пиксели N_01 x 90..178, y 493..511 — ровно те, что брал UV-регион 0.176…0.350 × 0.963…1.000) с BOX-полями по X:
#   концы 9 px своего размера, тянется середина. Тинт состояний (E6/FF/80) и размер кисти не меняются.
_BTN_BRUSHES = ("normal", "hovered", "pressed", "disabled")
_RAW_ROOT = os.path.normpath(os.path.join(HERE, ".."))   # корень проекта (там RawAssets)


def _brush_text(w, p):
    """export_text кисти состояния `p` UButton."""
    return w.get_editor_property("widget_style").get_editor_property(p).export_text()


def _btn_box_is(w, spec, which):
    """Все кисти состояний в виде `which`: 'before' — атлас `src_texture` с UV-регионом `src_uv`, IMAGE без полей;
    'after' — вырезка `texture` без UV-региона, BOX с полями `margin`. Размер кисти — `image_size` в обоих."""
    for p in _BTN_BRUSHES:
        t = _brush_text(w, p)
        m = re.search(r"ImageSize=\(X=([-\d.]+),Y=([-\d.]+)\)", t)
        if not m or not _near((float(m.group(1)), float(m.group(2))), spec["image_size"]):
            return False
        res = re.search(r"ResourceObject=\"[^']*'([^'.]+)\.", t)
        res = res.group(1) if res else ""
        draw = re.search(r"DrawAs=(\w+)", t).group(1)
        mg = re.search(r"Margin=\(Left=([-\d.]+),Top=([-\d.]+),Right=([-\d.]+),Bottom=([-\d.]+)\)", t)
        marg = tuple(float(x) for x in mg.groups()) if mg else (0.0, 0.0, 0.0, 0.0)
        uv = re.search(r"UVRegion=\(Min=\(X=([-\d.]+),Y=([-\d.]+)\),Max=\(X=([-\d.]+),Y=([-\d.]+)\),bIsValid=(\w+)\)", t)
        uv_valid = bool(uv) and uv.group(5) == "True"
        if which == "before":
            if res != spec["src_texture"] or draw != "Image" or not _near(marg, (0, 0, 0, 0)) or not uv_valid \
                    or not _near([float(x) for x in uv.groups()[:4]], spec["src_uv"]):
                return False
        else:
            if res != spec["texture"] or draw != "Box" or not _near(marg, spec["margin"]) or uv_valid:
                return False
    return True


# ★ fishfix 09.10 (книга рыбака 74000, ревизия `fishing_REVIEW.md` МЕЛК-4): тот же приём «вырезка + BOX» для ЛЮБОЙ кисти —
#   кисть картинки (`img_box`) и отдельные состояния кнопки (`btn_states_box`: только перечисленные состояния, прочие не
#   трогаются — у вкладок книги «обычное»/«недоступна» = NoDraw). Сверка «было/стало» — по export_text кисти.
def _brush_text_is(t, spec, which):
    """Кисть (export_text `t`) в виде `which`: 'before' — атлас `src_texture` + UV-регион `src_uv`, IMAGE без полей;
    'after' — вырезка `texture` без UV-региона, BOX с полями `margin`. Размер кисти — `image_size` в обоих."""
    m = re.search(r"ImageSize=\(X=([-\d.]+),Y=([-\d.]+)\)", t)
    if not m or not _near((float(m.group(1)), float(m.group(2))), spec["image_size"]):
        return False
    res = re.search(r"ResourceObject=\"[^']*'([^'.]+)\.", t)
    res = res.group(1) if res else ""
    draw = re.search(r"DrawAs=(\w+)", t).group(1)
    mg = re.search(r"Margin=\(Left=([-\d.]+),Top=([-\d.]+),Right=([-\d.]+),Bottom=([-\d.]+)\)", t)
    marg = tuple(float(x) for x in mg.groups()) if mg else (0.0, 0.0, 0.0, 0.0)
    uv = re.search(r"UVRegion=\(Min=\(X=([-\d.]+),Y=([-\d.]+)\),Max=\(X=([-\d.]+),Y=([-\d.]+)\),bIsValid=(\w+)\)", t)
    uv_valid = bool(uv) and uv.group(5) == "True"
    if which == "before":
        return res == spec["src_texture"] and draw == "Image" and _near(marg, (0, 0, 0, 0)) and uv_valid \
            and _near([float(x) for x in uv.groups()[:4]], spec["src_uv"])
    # Поля BOX сверяются с допуском 1e-3 (export_text печатает 6 знаков, EPS шага 0.05 для полей-долей велик).
    return res == spec["texture"] and draw == "Box" and not uv_valid \
        and all(abs(a - b) <= 1e-3 for a, b in zip(marg, spec["margin"]))


def _brush_to_box(t, tex, margin):
    """Кисть export_text `t` → та же кисть: вырезка `tex`, DrawAs=Box, поля `margin`, без UV-региона (тинт/размер те же)."""
    t = re.sub(r"DrawAs=\w+", "DrawAs=Box", t)
    t = re.sub(r"Margin=\([^)]*\)", "Margin=(Left=%f,Top=%f,Right=%f,Bottom=%f)" % tuple(margin), t)
    t = re.sub(r"ResourceObject=\"[^\"]*\"", "ResourceObject=\"%s\"" % tex.get_path_name(), t)
    t = re.sub(r"UVRegion=\(Min=\([^)]*\),Max=\([^)]*\),bIsValid=\w+\)",
               "UVRegion=(Min=(X=0.000000,Y=0.000000),Max=(X=0.000000,Y=0.000000),bIsValid=False)", t)
    br = unreal.SlateBrush()
    br.import_text(t)
    return br


def _overflow_name(w):
    """Политика переполнения текста строкой (`CLIP`, `ELLIPSIS`)."""
    return str(w.get_editor_property("text_overflow_policy")).split(".")[-1].split(":")[0].strip().upper()


def _ensure_slice_texture(spec, dry):
    """Текстура-вырезка `spec['texture']`: есть — вернуть; нет — импортировать `spec['source_png']` с настройками атласа
    `src_texture` (UI-группа, sRGB, без мипов, фильтр как у атласа), адресация CLAMP (концы не подмешивают край)."""
    EAL = unreal.EditorAssetLibrary
    if EAL.does_asset_exist(spec["texture"]):
        return EAL.load_asset(spec["texture"])
    if dry:
        log("текстуры-вырезки %s нет — будет импортирована из %s" % (spec["texture"], spec["source_png"]))
        return None
    png = os.path.join(_RAW_ROOT, spec["source_png"].replace("/", os.sep))
    if not os.path.isfile(png):
        raise RuntimeError("нет исходника вырезки %s" % png)
    folder, name = spec["texture"].rsplit("/", 1)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", False)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = EAL.load_asset(spec["texture"])
    src = EAL.load_asset(spec["src_texture"])
    if not tex or not src:
        raise RuntimeError("импорт вырезки %s не удался" % spec["texture"])
    for prop in ("compression_settings", "srgb", "lod_group", "mip_gen_settings", "filter", "never_stream"):
        tex.set_editor_property(prop, src.get_editor_property(prop))
    tex.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    if not EAL.save_loaded_asset(tex, False):
        raise RuntimeError("не удалось сохранить %s" % spec["texture"])
    log("ASDA_WRITES_ASSET: %s" % spec["texture"])
    return tex


def _verdict(name, ent, w):
    """('fixed'|'ok'|'inspect', текст) для одного узла таблицы: слот (+кегль/клип у текста) и, у UCheckBox с ключом
    `cb_img`, размер кистей состояний — каждая часть в виде «было» или «стало», иначе «осмотреть»."""
    if w is None:
        # ★ fishfix 09.10: узел создаёт шаг, идущий ПОСЛЕ этого (клоны), — на свежем импорте его ещё нет; это не ошибка.
        if ent.get("optional"):
            return "absent", "%s: узла пока нет (его создаст шаг клонов от уже сдвинутого шаблона)" % name
        return "inspect", "%s: узла нет" % name
    st = _state(w)
    if st is None:
        return "inspect", "%s: слот не канвасный" % name
    _off, _f, _c, sig = st
    anch, align, auto = sig[1], sig[2], sig[3]
    # ★ 10.10.2026 (волна «наезды текста»): `layout_after` {"align": [x, y], "auto": bool} — узел, которому шаг сам ставит
    #   выравнивание слота и авторазмер (блок строк по центру рамки по числу строк, как оригинал). Допустимы вид «было»
    #   (alignment 0,0, без авторазмера) и этот вид «стало»; любой другой — «осмотреть».
    la = ent.get("layout_after")
    lay_after = la is not None and _near(align, la["align"]) and bool(auto) == bool(la["auto"])
    lay_before = _near(align, (0.0, 0.0)) and not auto
    if not _near(anch[0:2], anch[2:4]) or not (lay_before or lay_after):
        return "inspect", "%s: анкоры %s / alignment %s / авторазмер — не точечный слот" % (name, anch, align)
    layout_state = "after" if (la is None or lay_after) else "before"
    fa = ent.get("font_after", ent.get("font_before"))
    ca = ent.get("clip_after", ent.get("clip_before"))
    befores = [(ent["before"], ent.get("font_before"), ent.get("clip_before"))]
    befores += [(a["slot"], a.get("font"), a.get("clip")) for a in ent.get("alt_before", [])]
    if _matches(st, ent["after"], fa, ca):
        slot_state = "after"
    elif any(_matches(st, *b) for b in befores):
        slot_state = "before"
    else:
        return "inspect", "%s: слот %s / кегль %s / клип %s не совпадают ни с «было» %s, ни со «стало» %s — НЕ трогаю"             % (name, st[0], st[1], st[2], [b[0] for b in befores], ent["after"])
    cb = ent.get("cb_img")
    cb_state = "after"
    if cb:
        if not isinstance(w, unreal.CheckBox):
            return "inspect", "%s: ожидался UCheckBox" % name
        sizes = _cb_sizes(w)
        if all(_near(z, cb["after"]) for z in sizes):
            cb_state = "after"
        elif all(_near(z, cb["before"]) for z in sizes)                 or any(all(_near(z, alt) for z in sizes) for alt in cb.get("alt_before", [])):
            # ★ 07.10.2026: `alt_before` — прочие допустимые «было» (8500: вид импортёра 148 и прежнего решения 134)
            cb_state = "before"
        else:
            return "inspect", "%s: кисти галки %s не «было» %s и не «стало» %s — НЕ трогаю"                 % (name, sizes, cb["before"], cb["after"])
    bb = ent.get("btn_box")
    bb_state = "after"
    if bb:
        if not isinstance(w, unreal.Button):
            return "inspect", "%s: ожидался UButton" % name
        if _btn_box_is(w, bb, "after"):
            bb_state = "after"
        elif _btn_box_is(w, bb, "before"):
            bb_state = "before"
        else:
            return "inspect", "%s: кисти кнопки не вид импортёра (атлас+UV, IMAGE) и не вырезка BOX — НЕ трогаю: %s" \
                % (name, _brush_text(w, "normal")[:400])
    # ★ fishfix 09.10: многоточие поля (`overflow_before/after`), кисть картинки (`img_box`), состояния кнопки (`btn_states_box`).
    of_state = ib_state = bs_state = wr_state = "after"
    # ★ 10.10.2026 (волна «наезды текста», `Reports/owner_1010/overlaps.md`): перенос по ширине `wrap_text_at`
    #   (`wrap_at_before/after`, px). Не AutoWrap — тот в канвасе не получает ширину (память ui-autowrap-canvas-gotcha).
    if "wrap_at_after" in ent:
        if not isinstance(w, unreal.TextBlock):
            return "inspect", "%s: ожидался UTextBlock" % name
        cur = float(w.get_editor_property("wrap_text_at"))
        if abs(cur - float(ent["wrap_at_after"])) <= EPS:
            wr_state = "after"
        elif abs(cur - float(ent.get("wrap_at_before", 0.0))) <= EPS:
            wr_state = "before"
        else:
            return "inspect", "%s: перенос %s не «было» %s и не «стало» %s — НЕ трогаю" % (
                name, cur, ent.get("wrap_at_before"), ent["wrap_at_after"])
    if "overflow_after" in ent:
        if not isinstance(w, unreal.TextBlock):
            return "inspect", "%s: ожидался UTextBlock" % name
        cur = _overflow_name(w)
        if cur == ent["overflow_after"]:
            of_state = "after"
        elif cur == ent.get("overflow_before"):
            of_state = "before"
        else:
            return "inspect", "%s: переполнение %s не «было» %s и не «стало» %s — НЕ трогаю" % (
                name, cur, ent.get("overflow_before"), ent["overflow_after"])
    ib = ent.get("img_box")
    if ib:
        if not isinstance(w, unreal.Image):
            return "inspect", "%s: ожидался UImage" % name
        t = w.get_editor_property("brush").export_text()
        if _brush_text_is(t, ib, "after"):
            ib_state = "after"
        elif _brush_text_is(t, ib, "before"):
            ib_state = "before"
        else:
            return "inspect", "%s: кисть не вид импортёра (атлас+UV, IMAGE) и не вырезка BOX — НЕ трогаю: %s" % (name, t[:400])
    bs = ent.get("btn_states_box")
    if bs:
        if not isinstance(w, unreal.Button):
            return "inspect", "%s: ожидался UButton" % name
        sts = []
        for p, sp in sorted(bs.items()):
            t = _brush_text(w, p)
            if _brush_text_is(t, sp, "after"):
                sts.append("after")
            elif _brush_text_is(t, sp, "before"):
                sts.append("before")
            else:
                return "inspect", "%s: кисть «%s» не вид импортёра и не вырезка BOX — НЕ трогаю: %s" % (name, p, t[:400])
        bs_state = "after" if all(x == "after" for x in sts) else "before"
    if slot_state == "after" and cb_state == "after" and bb_state == "after" \
            and of_state == "after" and ib_state == "after" and bs_state == "after" and wr_state == "after" \
            and layout_state == "after":
        return "ok", "%s: уже расширено %s" % (name, ent["after"])
    return "fixed", "%s: %s → %s%s%s%s" % (name, st[0], ent["after"],
                                         "" if fa is None else " кегль %s→%s клип %s→%s" % (st[1], fa, st[2], ca),
                                         "" if not cb else " кисти галки %s → %s" % (cb["before"], cb["after"]),
                                         "" if not bb else " кисти кнопки: атлас IMAGE → вырезка %s BOX поля %s" % (bb["texture"], bb["margin"])) \
        + ("" if of_state == "after" else " переполнение %s → %s" % (ent.get("overflow_before"), ent["overflow_after"])) \
        + ("" if wr_state == "after" else " перенос %s → %s" % (ent.get("wrap_at_before"), ent["wrap_at_after"])) \
        + ("" if layout_state == "after" else " выравнивание слота %s, авторазмер %s" % (la["align"], la["auto"])) \
        + ("" if ib_state == "after" else " кисть: атлас IMAGE → вырезка %s BOX" % ib["texture"]) \
        + ("" if bs_state == "after" else " кисти состояний %s: атлас IMAGE → вырезки BOX" % sorted(bs))


def _apply(name, ent, w):
    """Записать «стало» в узел (слот: только смещения; у текста — кегль/клип, если заявлены; у галки — размер кистей)."""
    T = _tb()
    s = T._slot(w)
    lay = s.get_editor_property("layout_data")
    a = ent["after"]
    lay.set_editor_property("offsets", unreal.Margin(a[0], a[1], a[2], a[3]))
    la = ent.get("layout_after")   # ★ 10.10.2026: выравнивание слота и авторазмер (см. `_verdict`)
    if la is not None:
        lay.set_editor_property("alignment", unreal.Vector2D(float(la["align"][0]), float(la["align"][1])))
    s.set_editor_property("layout_data", lay)
    if la is not None:
        s.set_editor_property("auto_size", bool(la["auto"]))
    if isinstance(w, unreal.TextBlock):
        if "font_after" in ent:
            font = w.get_editor_property("font")
            if abs(float(font.get_editor_property("size")) - ent["font_after"]) > EPS:
                font.set_editor_property("size", ent["font_after"])
                w.set_editor_property("font", font)
        if "clip_after" in ent and _clip_name(w) != ent["clip_after"]:
            w.set_editor_property("clipping", getattr(unreal.WidgetClipping, ent["clip_after"]))
    cb = ent.get("cb_img")
    if cb and isinstance(w, unreal.CheckBox):
        style = w.get_editor_property("widget_style")
        for p in _CB_BRUSHES:
            br = style.get_editor_property(p)
            v = br.get_editor_property("image_size")
            v.import_text("(X=%f,Y=%f)" % (cb["after"][0], cb["after"][1]))
            br.set_editor_property("image_size", v)
            style.set_editor_property(p, br)
        w.set_editor_property("widget_style", style)
        got = _cb_sizes(w)
        if not all(_near(z, cb["after"]) for z in got):
            raise RuntimeError("%s: кисти галки после записи %s ≠ %s — НЕ сохраняю" % (name, got, cb["after"]))
    # ★ fishfix 09.10: многоточие поля
    # ★ 10.10.2026: перенос по ширине
    if "wrap_at_after" in ent and isinstance(w, unreal.TextBlock):
        w.set_editor_property("wrap_text_at", float(ent["wrap_at_after"]))
        if abs(float(w.get_editor_property("wrap_text_at")) - float(ent["wrap_at_after"])) > EPS:
            raise RuntimeError("%s: перенос после записи ≠ %s — НЕ сохраняю" % (name, ent["wrap_at_after"]))
    if "overflow_after" in ent and isinstance(w, unreal.TextBlock) and _overflow_name(w) != ent["overflow_after"]:
        w.set_editor_property("text_overflow_policy", getattr(unreal.TextOverflowPolicy, ent["overflow_after"]))
        if _overflow_name(w) != ent["overflow_after"]:
            raise RuntimeError("%s: переполнение после записи %s ≠ %s — НЕ сохраняю" % (name, _overflow_name(w), ent["overflow_after"]))
    # ★ fishfix 09.10: кисть картинки → вырезка BOX
    ib = ent.get("img_box")
    if ib and isinstance(w, unreal.Image) and not _brush_text_is(w.get_editor_property("brush").export_text(), ib, "after"):
        tex = _ensure_slice_texture(ib, False)
        w.set_editor_property("brush", _brush_to_box(w.get_editor_property("brush").export_text(), tex, ib["margin"]))
        if not _brush_text_is(w.get_editor_property("brush").export_text(), ib, "after"):
            raise RuntimeError("%s: кисть после записи не «вырезка BOX» — НЕ сохраняю" % name)
    # ★ fishfix 09.10: перечисленные состояния кнопки → вырезки BOX (прочие состояния не трогаются)
    bs = ent.get("btn_states_box")
    if bs and isinstance(w, unreal.Button):
        style = w.get_editor_property("widget_style")
        for p, sp in sorted(bs.items()):
            t = style.get_editor_property(p).export_text()
            if _brush_text_is(t, sp, "after"):
                continue
            style.set_editor_property(p, _brush_to_box(t, _ensure_slice_texture(sp, False), sp["margin"]))
        w.set_editor_property("widget_style", style)
        for p, sp in sorted(bs.items()):
            if not _brush_text_is(_brush_text(w, p), sp, "after"):
                raise RuntimeError("%s: кисть «%s» после записи не «вырезка BOX» — НЕ сохраняю" % (name, p))
    bb = ent.get("btn_box")
    if bb and isinstance(w, unreal.Button) and not _btn_box_is(w, bb, "after"):
        tex = _ensure_slice_texture(bb, False)
        style = w.get_editor_property("widget_style")
        m = bb["margin"]
        for p in _BTN_BRUSHES:
            # UVRegion закрыт для Python-свойств — кисть пересобирается через export_text/import_text.
            t = style.get_editor_property(p).export_text()
            t = re.sub(r"DrawAs=\w+", "DrawAs=Box", t)
            t = re.sub(r"Margin=\([^)]*\)", "Margin=(Left=%f,Top=%f,Right=%f,Bottom=%f)" % (m[0], m[1], m[2], m[3]), t)
            t = re.sub(r"ResourceObject=\"[^\"]*\"", "ResourceObject=\"%s\"" % tex.get_path_name(), t)
            t = re.sub(r"UVRegion=\(Min=\([^)]*\),Max=\([^)]*\),bIsValid=\w+\)",
                       "UVRegion=(Min=(X=0.000000,Y=0.000000),Max=(X=0.000000,Y=0.000000),bIsValid=False)", t)
            br = unreal.SlateBrush()
            br.import_text(t)
            style.set_editor_property(p, br)
        w.set_editor_property("widget_style", style)
        if not _btn_box_is(w, bb, "after"):
            raise RuntimeError("%s: кисти кнопки после записи не «вырезка BOX»: %s — НЕ сохраняю"
                               % (name, _brush_text(w, "normal")[:400]))


def main(standalone=False):
    """Применить таблицу DATA. `standalone` — ручной запуск: бэкап обязателен."""
    T = _tb()
    dry = bool(os.environ.get("ASDA_WIDEN_DRYRUN"))
    only = os.environ.get("ASDA_WIDEN_ONLY", "")
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    from asda_login_guard import login_package_blocked
    with open(DATA, encoding="utf-8") as f:
        table = json.load(f)
    fixed_total = ok_total = 0
    inspect = []
    for asset in sorted(table):
        if only and only not in asset:
            continue
        spec = table[asset]
        if login_package_blocked(asset, "asda_ui_widen_windows.py"):
            log("%s: окно сцены логина — пропуск (защита логина)" % asset)
            continue
        bp = EAL.load_asset(asset)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            raise RuntimeError("нет корневого канваса у %s" % asset)
        nodes = {w.get_name(): w for w in T._walk(root)}
        verdicts = {n: _verdict(n, e, nodes.get(n)) for n, e in spec["nodes"].items()}
        short = asset.rsplit("/", 1)[-1]
        todo = sorted(n for n, (v, _m) in verdicts.items() if v == "fixed")
        okn = sum(1 for v, _m in verdicts.values() if v == "ok")
        bad = ["%s: %s" % (short, m) for v, m in verdicts.values() if v == "inspect"]
        absent = sum(1 for v, _m in verdicts.values() if v == "absent")   # ★ fishfix 09.10: необязательные узлы (клоны)
        log("%s %s Δ=%s: узлов %d — править %d, готово %d, осмотреть %d%s"
            % (short, spec["window"], spec["delta"], len(verdicts), len(todo), okn, len(bad),
               ", нет необязательных %d" % absent if absent else ""))
        for n in todo:
            log("   [править] %s" % verdicts[n][1])
        ok_total += okn
        if bad:
            inspect += bad
            continue                     # окно с чужим видом хоть одного узла не пишется целиком
        if dry:
            # ★ fishfix 09.10: сухой режим сообщает и о недостающих вырезках (импорт — только при записи).
            for n in todo:
                e = spec["nodes"][n]
                for sp in ([e["img_box"]] if e.get("img_box") else []) + list((e.get("btn_states_box") or {}).values()):
                    _ensure_slice_texture(sp, True)
            continue
        if not todo:
            continue
        if standalone:
            T._ensure_backup(asset, BACKUP_SET, set_dir=BACKUP_DIR_OVERRIDE)
        before = T._snapshot(root)
        for n in todo:
            _apply(n, spec["nodes"][n], nodes[n])
        after = T._snapshot(B.get_root_canvas(bp))
        lost = [n for n in before if n not in after]
        extra = [n for n in after if n not in before]
        changed = set(todo)
        foreign = [n for n in before if n in after and before[n] != after[n] and n not in changed]
        broken = []
        for n in changed:
            b, a = before[n], after[n]
            # неизменно: класс, родитель, анкоры/alignment/авторазмер/z, видимость, непрозрачность, кисть
            # ★ fishfix 09.10: у узла с `img_box` меняется и кисть (индекс 5 снимка) — её «стало» проверил `_apply`.
            brush_ok = b[5] == a[5] or bool(spec["nodes"][n].get("img_box"))
            # ★ 10.10.2026: у узла с `layout_after` меняются alignment и авторазмер (индексы 2, 3 подписи слота)
            lay_ok = b[2][1:] == a[2][1:] or (bool(spec["nodes"][n].get("layout_after"))
                                               and b[2][1] == a[2][1] and b[2][4] == a[2][4])
            if b[:2] != a[:2] or not lay_ok or b[3:5] != a[3:5] or not brush_ok:
                broken.append(n)
                continue
            if b[6] != a[6]:
                # у текста может меняться только кегль (индекс 6 подписи текста) и — ★ 10.10.2026 — перенос `wrap_text_at`
                #   (индекс 5), если заявлен `wrap_at_after`
                e = spec["nodes"][n]
                same5 = b[6] is not None and a[6] is not None and (b[6][5] == a[6][5] or "wrap_at_after" in e)
                same6 = b[6] is not None and a[6] is not None and (b[6][6] == a[6][6] or "font_after" in e)
                if b[6] is None or a[6] is None or b[6][:5] != a[6][:5] or b[6][7:] != a[6][7:] or not same5 or not same6:
                    broken.append(n)
        log("%s: узлов до %d, после %d; правлено %d; потеряно узлов: %d; лишних: %d; изменено чужих: %d; "
            "испорчено у правленых: %d" % (short, len(before), len(after), len(changed), len(lost), len(extra),
                                          len(foreign), len(broken)))
        if lost or extra or foreign or broken:
            raise RuntimeError("сверка не прошла: потеряно %s, лишних %s, чужих %s, испорчено %s — НЕ сохраняю"
                               % (lost, extra, foreign, broken))
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % asset)
        log("ASDA_WRITES_ASSET: %s" % asset)
        fixed_total += len(changed)
    for s in inspect:
        log("осмотреть: %s" % s)
    log("окон %d: исправлено узлов %d, уже расширено %d, осмотреть %d%s"
        % (len(table), fixed_total, ok_total, len(inspect), " (СУХОЙ ПРОГОН)" if dry else ""))
    if inspect and not dry:
        raise RuntimeError("расширение-окон: %d узлов «осмотреть» — вид не совпал с таблицей, см. лог" % len(inspect))
    log("РАСШИРЕНИЕ-ОКОН: ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта) — ничего.
if __name__ == "__main__":
    if unreal is None:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        with open(DATA, encoding="utf-8") as f:
            for a, s in sorted(json.load(f).items()):
                print("%s %s Δ=%s: узлов %d" % (a.rsplit("/", 1)[-1], s["window"], s["delta"], len(s["nodes"])))
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[расширение-окон] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
