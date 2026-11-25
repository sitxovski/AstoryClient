# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_step_dryrun.py — ★ ОБЩИЙ СУХОЙ РЕЖИМ ШАГОВ ПОСТ-ИМПОРТА, у которых своего не было (07.10.2026).
#
# ЗАЧЕМ. Ревизия `Reports/owner_1007/label_band_fixes_REVIEW.md` (СРЕДН-1): 9 шагов цепочки
#   `asda_ui_post_import.STEPS` не умели «посчитать, ничего не записав», и сухой прогон цепочки их
#   пропускал — поэтому неидемпотентность (шаг мини-карты гасил белую рамку карты мира) жила незамеченной.
#
# КАК РАБОТАЕТ. Логика шага НЕ меняется ни в чём: шаг правит загруженный ассет в памяти так же, как
#   всегда, а помощник делает ровно две вещи:
#     1) `save(...)` — в сухом режиме НЕ вызывает `save_loaded_asset` (запись на диск подавлена);
#     2) `WidgetDelta` — снимок дерева окна (`AsdaUmgBuilder.DumpWidgetTree` + `_extra_sig`: выравнивание, шрифт,
#        обводка, перенос, фокус, прозрачность, кисти — с 07.10.2026) до и после шага; итог
#        печатается строкой «[ШАГ-ДЕЛЬТА] <шаг>: изменено узлов N …» — по ней идемпотентность видна
#        штатно (второй прогон обязан дать 0).
#   ⚠ В сухом режиме правки шага остаются В ПАМЯТИ процесса (как и в «ревизорском» прогоне с
#     подавлением записи): следующий шаг той же цепочки видит их. На диск не уходит ничего.
#
# ★ 07.10.2026 (ревизия `Reports/owner_1007/poststeps_align_fix_REVIEW.md`, СРЕДН-1): снимок узла берёт
#   структуры (кисти, стили кнопок/галок/полей ввода, шрифт, трансформ) ЦЕЛИКОМ через `export_text()` — с
#   размером кисти (`ImageSize`, тип `DeprecateSlateVector2D` в UE 5.8), UV (`UVRegion`) и всем прочим, плюс
#   `clipping`/подсказку/трансформ у всех узлов и `checked_state` у галок. Адреса памяти `0x…` и путь
#   собственного пакета окна из строк вырезаются (`_norm`), поэтому снимки РАЗНЫХ объектов (бэкап против
#   текущего ассета, два процесса) сравнимы: различия = только реальные правки. Прежний снимок не видел размер и
#   UV кистей (`uv_region` у Python-`SlateBrush` нет — исключение глоталось), а адрес в строке давал 86/72 ложных
#   различия вместо 2/1 при сверке с бэкапом.
#   `save_if_changed` — запись окна, ТОЛЬКО если снимок шага или сам шаг насчитали правки (МЕЛК-3 той же
#   ревизии: шаги карт перезаписывали окна при нуле правок — шум даты/md5 в Perforce).
#
# ВКЛЮЧЕНИЕ сухого режима (любое из):
#   • ключ `--dry-run` в argv процесса (`-ExecutePythonScript="<шаг>.py --dry-run"`);
#   • `ASDA_POSTSTEPS_DRYRUN=1` — общий ключ всей цепочки (его ставит `asda_ui_post_import --dry-run`);
#   • собственный ключ шага `ASDA_<ШАГ>_DRYRUN=1` (имя — константа `DRYRUN_ENV` в модуле шага).
#
# ★ ЗАКОН tools-no-import-side-effects: модуль при импорте ничего не делает; `unreal` берётся лениво,
#   чтобы его можно было импортировать и вне редактора (проверки синтаксиса, офлайн-тесты).
import os
import re
import sys

# Общий ключ сухого прогона всей цепочки пост-импорта.
GLOBAL_ENV = "ASDA_POSTSTEPS_DRYRUN"
# Ключ командной строки сухого режима.
ARG = "--dry-run"
# Маркер строки итога шага в логе (по нему итог ищет человек и раннер).
MARK = "[ШАГ-ДЕЛЬТА]"
# Сколько строк различий печатать на шаг (остальное — числом).
MAX_LINES = 60


def _truthy(v):
    """Значение переменной окружения как «включено»: 1/true/yes/on."""
    return str(v or "").strip().lower() in ("1", "true", "yes", "on")


def is_dry(env_key=None):
    """True — шаг обязан работать в сухом режиме (см. «ВКЛЮЧЕНИЕ» в шапке)."""
    if ARG in sys.argv:
        return True
    if _truthy(os.environ.get(GLOBAL_ENV)):
        return True
    return bool(env_key) and _truthy(os.environ.get(env_key))


def _log(msg):
    """Лог: в UE — unreal.log, вне UE — print."""
    try:
        import unreal
        unreal.log(msg)
    except ImportError:
        print(msg)


def save(asset, dry, tag, only_if_dirty=None):
    """Записать ассет на диск, а в сухом режиме — только отметить, что запись была бы.

    `only_if_dirty` — второй аргумент `save_loaded_asset` (как его передавал шаг; None — не передавать).
    Возврат — как у `save_loaded_asset` (в сухом режиме True: «запись условно успешна»)."""
    import unreal
    name = asset.get_path_name().split(".")[0] if asset is not None else "?"
    if dry:
        _log("%s %s: [СУХОЙ] запись %s подавлена" % (MARK, tag, name))
        return True
    eal = unreal.EditorAssetLibrary
    if only_if_dirty is None:
        return eal.save_loaded_asset(asset)
    return eal.save_loaded_asset(asset, only_if_dirty)


def save_if_changed(bp, dry, tag, delta=None, own=0, only_if_dirty=None, compile_fn=None):
    """★ 07.10.2026 (МЕЛК-3 ревизии `poststeps_align_fix_REVIEW.md`): записать окно, ТОЛЬКО если есть правки.

    Правки = узлы, изменённые по снимку `delta` (`WidgetDelta.done(bp)` — снимок снимается здесь, повторно его
    вызывать не нужно), плюс `own` — правки, которые насчитал сам шаг (созданные узлы и т. п.). Ноль — окно не
    компилируется и не пишется (дата и md5 файла не меняются). `compile_fn(bp)` — компиляция перед записью
    (`AsdaUmgBuilder.compile_and_save`: вопреки имени только компилирует и метит пакет грязным).
    Возврат — число правок окна (снимок + `own`)."""
    n = delta.done(bp) if delta is not None else 0
    total = int(n) + int(own or 0)
    name = bp.get_path_name().split(".")[0] if bp is not None else "?"
    if total <= 0:
        _log("%s %s: правок 0 — окно %s не компилируется и не записывается" % (MARK, tag, name))
        return 0
    if compile_fn is not None:
        compile_fn(bp)
    if not save(bp, dry, tag, only_if_dirty):
        raise RuntimeError("%s: не сохранён %s" % (tag, name))
    return total


# Адрес памяти в строке структуры/объекта (`(0x00000000FFAD8EE0)`): у разных объектов и процессов разный.
_ADDR_RX = re.compile(r"0x[0-9A-Fa-f]{8,16}")
# Путь собственного пакета окна, у которого снимается снимок (подставляет `_tree_rows`; вне снимка — None).
_SELF_PKG = [None]


def _norm(s):
    """Строка снимка без адресов памяти и без пути собственного пакета окна → сравнима между объектами/процессами."""
    s = _ADDR_RX.sub("0x?", str(s))
    pkg = _SELF_PKG[0]
    if pkg:
        s = s.replace(pkg, "<ОКНО>")
    return s


def _export(v):
    """Структура UE целиком (`export_text()`) без адресов; нет `export_text` — None."""
    fn = getattr(v, "export_text", None)
    if fn is None:
        return None
    try:
        return _norm(fn())
    except Exception:                                            # noqa: BLE001
        return None


def _fmt(v):
    """Значение свойства → устойчивая строка снимка (без адресов памяти; дробные — 4 знака).
    ★ 07.10.2026: запасной путь для любых структур — `export_text()` целиком (так видны `DeprecateSlateVector2D`
    размера кисти, `Box2D` UV и всё, для чего нет своей ветки); строка без адресов (`_norm`)."""
    import unreal
    if v is None:
        return "-"
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, float):
        return "%.4f" % v
    if isinstance(v, int):
        return str(v)
    if isinstance(v, unreal.LinearColor):
        return "(%.4f,%.4f,%.4f,%.4f)" % (v.r, v.g, v.b, v.a)
    if isinstance(v, unreal.SlateColor):
        e = _export(v)
        if e is not None:
            return "sc" + e
        try:
            return "sc" + _fmt(v.get_editor_property("specified_color"))
        except Exception:                                        # noqa: BLE001
            return "sc?"
    if isinstance(v, unreal.Vector2D):
        return "(%.4f,%.4f)" % (v.x, v.y)
    if isinstance(v, unreal.Margin):
        return "m(%.4f,%.4f,%.4f,%.4f)" % (v.left, v.top, v.right, v.bottom)
    if isinstance(v, unreal.Object):
        return _norm(v.get_path_name())
    if isinstance(v, (str, unreal.Text, unreal.Name)):
        return _norm(str(v))
    e = _export(v)
    if e is not None:
        return e
    return _norm(str(v).split(".")[-1].split(":")[0].strip())


def _prop(w, name):
    """`get_editor_property` с защитой: у класса свойства нет → «-»."""
    try:
        return w.get_editor_property(name)
    except Exception:                                            # noqa: BLE001
        return None


def _font_sig(f):
    """Шрифт (`SlateFontInfo`) целиком: гарнитура, кегль, объект шрифта, материал, межбуквенный, наклон, обводка."""
    if f is None:
        return "-"
    return _fmt(f)


def _brush_sig(b):
    """Кисть (`SlateBrush`) целиком через `export_text()`: ресурс, режим рисования, поля, `ImageSize`, тинт,
    `UVRegion`, мозаика, отражение … (★ 07.10.2026: прежде размер и UV сюда не попадали — СРЕДН-1 ревизии)."""
    if b is None:
        return "-"
    return _fmt(b)


# Свойства текста, которые пишут шаги пост-импорта (шрифт/обводка — `_font_sig`; цвет и первые 24 символа —
#   уже в `DumpWidgetTree`). Список — по записям `set_editor_property` во всех шагах `STEPS` (скан 07.10.2026).
TEXT_PROPS = ("justification", "auto_wrap_text", "wrap_text_at", "shadow_offset", "shadow_color_and_opacity",
              "margin", "line_height_percentage", "text_overflow_policy", "min_desired_width",
              "color_and_opacity", "font_outline_material", "stroke_settings")
# Общие свойства любого узла (★ 07.10.2026: `clipping` пишут 6 шагов, в т. ч. `widen_windows` и `icon_caption_fit`).
COMMON_PROPS = ("render_opacity", "is_enabled", "clipping", "tool_tip_text", "render_transform",
                "render_transform_pivot", "flow_direction_preference", "cursor")
# Состояния кисти кнопки (`ButtonStyle`) — их перекрашивают `apply_button_skin` и подобные (стиль целиком — `стиль=`).
BUTTON_BRUSHES = ("normal", "hovered", "pressed", "disabled")
# Свойства кнопки/галки сверх стиля.
BUTTON_PROPS = ("is_focusable", "background_color", "color_and_opacity", "click_method", "touch_method",
                "press_method")
CHECK_PROPS = ("is_focusable", "checked_state", "click_method", "touch_method", "press_method")
# Свойства поля ввода сверх стиля (`widget_style` целиком).
EDIT_PROPS = ("is_read_only", "is_password", "hint_text", "select_all_text_when_focused", "clear_keyboard_focus_on_commit",
              "keyboard_type", "allow_context_menu", "minimum_desired_width")


def _cls(*names):
    """Кортеж классов `unreal.<имя>`, которые есть в этой сборке движка (нет класса — пропуск)."""
    import unreal
    return tuple(c for c in (getattr(unreal, n, None) for n in names) if c is not None)


def _props_sig(w, names):
    """«имя=значение» для свойств `names`, которые у узла есть (нет свойства — пропуск)."""
    out = []
    for n in names:
        v = _prop(w, n)
        if v is not None:
            out.append("%s=%s" % (n, _fmt(v)))
    return out


# Свойства слотов, отличных от `CanvasPanelSlot` (в UI на 08.10.2026 их нет: все 39 762 слота — канвасные).
SLOT_PROPS = ("padding", "horizontal_alignment", "vertical_alignment", "size")


def _slot_sig(w):
    """★ 08.10.2026 (ревизия `poststeps_tools_harden_REVIEW.md`, МЕЛК-4): слот узла с точностью 3 знака.
    `DumpWidgetTree` печатает смещения `%.1f` — сдвиг меньше 0.05 px снимок не видел, и `save_if_changed` молча не
    записал бы такую правку шага. Канвасный слот — смещения, анкоры, выравнивание, auto/z; прочие — `SLOT_PROPS`."""
    import unreal
    s = _prop(w, "slot")
    if s is None:
        return "слот=-"
    if isinstance(s, unreal.CanvasPanelSlot):
        ld = s.get_editor_property("layout_data")
        o, a, al = ld.offsets, ld.anchors, ld.alignment
        return ("слот=off(%.3f,%.3f,%.3f,%.3f) анкор(%.3f,%.3f,%.3f,%.3f) align(%.3f,%.3f) auto=%s z=%s"
                % (o.left, o.top, o.right, o.bottom, a.minimum.x, a.minimum.y, a.maximum.x, a.maximum.y,
                   al.x, al.y, _fmt(_prop(s, "auto_size")), _fmt(_prop(s, "z_order"))))
    return "слот=" + type(s).__name__ + "(" + " ".join(_props_sig(s, SLOT_PROPS)) + ")"


def _extra_sig(w):
    """★ 07.10.2026 (ревизии `poststeps_idempotent_REVIEW.md` СРЕДН-1 и `poststeps_align_fix_REVIEW.md` СРЕДН-1):
    свойства узла, которых НЕТ в `DumpWidgetTree`, но которые пишут шаги: общие (`COMMON_PROPS`: прозрачность,
    `is_enabled`, `clipping`, подсказка, трансформ), у текстов — шрифт целиком, выравнивание/перенос/тень/поля и
    полный текст; у кнопок и галок — стиль `widget_style` ЦЕЛИКОМ (все кисти с размером и UV, отступы, звуки),
    фокус, `checked_state`; у полей ввода — стиль целиком; у картинок — кисть целиком. Структуры — `export_text()`
    без адресов, поэтому строка сравнима между объектами."""
    import unreal
    parts = _props_sig(w, COMMON_PROPS)
    parts.append(_slot_sig(w))                                   # ★ 08.10.2026 (МЕЛК-4): слот с точностью 3 знака
    if isinstance(w, _cls("TextBlock", "EditableText", "EditableTextBox", "RichTextBlock", "MultiLineEditableText",
                          "MultiLineEditableTextBox")):
        parts.append("font=" + _font_sig(_prop(w, "font")))
        parts += _props_sig(w, TEXT_PROPS)
        t = _prop(w, "text")
        if t is not None:
            parts.append("текст=«%s»" % str(t))
    if isinstance(w, unreal.Button):
        parts += _props_sig(w, BUTTON_PROPS)
    if isinstance(w, unreal.CheckBox):
        parts += _props_sig(w, CHECK_PROPS)
    if isinstance(w, _cls("EditableText", "EditableTextBox", "MultiLineEditableText", "MultiLineEditableTextBox")):
        parts += _props_sig(w, EDIT_PROPS)
    if isinstance(w, _cls("Button", "CheckBox", "EditableText", "EditableTextBox", "MultiLineEditableText",
                          "MultiLineEditableTextBox", "ComboBoxString", "ProgressBar", "Slider", "ScrollBox",
                          "ScrollBar", "SpinBox")):
        st = _prop(w, "widget_style")
        if st is not None:
            parts.append("стиль=" + _fmt(st))
    if isinstance(w, unreal.Border):
        parts.append("кисть=" + _brush_sig(_prop(w, "background")))
        parts += _props_sig(w, ("brush_color", "content_color_and_opacity", "padding"))
    if isinstance(w, unreal.Image):
        parts.append("кисть=" + _brush_sig(_prop(w, "brush")))
        parts += _props_sig(w, ("color_and_opacity",))
    return " ".join(parts)


def _walk_widgets(w, cb):
    """Обход поддерева виджета (дети панелей и контент-виджетов)."""
    try:
        kids = w.get_all_children()
    except Exception:                                            # noqa: BLE001
        return
    for c in kids:
        cb(c)
        _walk_widgets(c, cb)


def _tree_rows(bp):
    """Дерево окна → {имя узла: «строка дампа #порядок-среди-братьев || доп. свойства»}.

    Порядковый номер у родителя нужен, чтобы перестановка детей (`move_child_to_index`) тоже
    считалась правкой: сама строка `DumpWidgetTree` порядка не содержит.
    Доп. свойства (`_extra_sig`) — то, чего нет в дампе, но что пишут шаги (выравнивание, шрифт, обводка …).
    ★ 07.10.2026: строки без адресов памяти и с путём собственного пакета, заменённым на «<ОКНО>» (`_norm`) — снимки
    двух объектов (бэкап, загруженный временной копией, против текущего ассета) сравнимы."""
    prev_pkg = _SELF_PKG[0]
    _SELF_PKG[0] = bp.get_outermost().get_path_name() if bp is not None else None
    try:
        return _tree_rows_impl(bp)
    finally:
        _SELF_PKG[0] = prev_pkg


def _tree_rows_impl(bp):
    """Тело `_tree_rows` (путь собственного пакета уже выставлен в `_SELF_PKG`)."""
    import unreal
    rows = {}
    order = {}
    for raw in _norm(unreal.AsdaUmgBuilder.dump_widget_tree(bp)).split("\n"):
        line = raw.strip()
        if not line or line.startswith("==="):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        body = " ".join(parts[1].split())
        par = ""
        for tok in body.split():
            if tok.startswith("род="):
                par = tok[4:]
                break
        k = order.get(par, 0)
        order[par] = k + 1
        rows[parts[0]] = "%s #%d" % (body, k)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    if root is not None:
        def add_extra(w):
            """Дописать к строке узла его доп. свойства."""
            n = w.get_name()
            rows[n] = rows.get(n, "?") + " || " + _extra_sig(w)
        add_extra(root)
        _walk_widgets(root, add_extra)
    return rows


# Сколько знаков контекста оставлять вокруг различия в строке лога (строки снимка со стилями — килобайты).
DIFF_CONTEXT = 70


def _short_diff(a, b):
    """Пара строк снимка → их различающиеся середины с контекстом `DIFF_CONTEXT` (для лога; сравнение — по целым)."""
    if a is None or b is None:
        return a, b
    p = 0
    lim = min(len(a), len(b))
    while p < lim and a[p] == b[p]:
        p += 1
    q = 0
    while q < lim - p and a[len(a) - 1 - q] == b[len(b) - 1 - q]:
        q += 1
    lo = max(p - DIFF_CONTEXT, 0)

    def cut(s):
        """Середина строки `s` от общего начала до общего конца с контекстом."""
        hi = min(len(s) - q + DIFF_CONTEXT, len(s))
        return ("…" if lo else "") + s[lo:hi] + ("…" if hi < len(s) else "")
    return cut(a), cut(b)


class WidgetDelta(object):
    """Учёт правок шага по снимкам дерева окон (до → после)."""

    def __init__(self, tag, dry):
        """`tag` — имя шага в логе; `dry` — сухой ли прогон (только для подписи итога)."""
        self.tag = tag                 # имя шага в строках лога
        self.dry = dry                 # сухой прогон (подпись итога)
        self.before = {}               # путь окна → снимок до шага
        self.lines = []                # строки различий «окно | узел: до → после»
        self.changed = 0               # всего изменённых/новых/удалённых узлов
        self.windows = 0               # окон с правками
        self.extra = 0                 # правки вне деревьев окон (данные-ассеты), считает сам шаг

    def take(self, bp):
        """Снять снимок окна ДО правок (повторный вызов для того же окна игнорируется)."""
        if bp is None:
            return
        path = bp.get_path_name().split(".")[0]
        if path not in self.before:
            self.before[path] = _tree_rows(bp)

    def done(self, bp):
        """Сравнить окно ПОСЛЕ правок со снимком; вернуть число изменённых узлов окна."""
        if bp is None:
            return 0
        path = bp.get_path_name().split(".")[0]
        a = self.before.pop(path, None)
        if a is None:
            return 0
        b = _tree_rows(bp)
        n = 0
        for name in sorted(set(a) | set(b)):
            if a.get(name) != b.get(name):
                n += 1
                da, db = _short_diff(a.get(name), b.get(name))
                self.lines.append("%s | %s\n      до:    %s\n      после: %s"
                                  % (path.rsplit("/", 1)[-1], name, da, db))
        if n:
            self.windows += 1
            self.changed += n
        return n

    def finish(self):
        """Напечатать итог шага (строка с маркером `MARK`) и вернуть число правок."""
        total = self.changed + self.extra
        for ln in self.lines[:MAX_LINES]:
            _log("%s %s:   %s" % (MARK, self.tag, ln))
        if len(self.lines) > MAX_LINES:
            _log("%s %s:   … ещё %d" % (MARK, self.tag, len(self.lines) - MAX_LINES))
        _log("%s %s: изменено узлов %d в окнах %d, прочих правок %d%s"
             % (MARK, self.tag, self.changed, self.windows, self.extra,
                " (СУХОЙ ПРОГОН — на диск не записано ничего)" if self.dry else ""))
        return total
