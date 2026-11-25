# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_jui_importer_sim.py — ★ ОФЛАЙН-ПРОГОН ИМПОРТЁРА JUI → UMG БЕЗ РЕДАКТОРА (07.10.2026, задача «невидимая полоса
#   под подписью», отчёт `Reports/owner_1007/label_band_and_8500.md`).
#
#   ЗАЧЕМ. Шаги пост-импорта правят ассет, только если узел в ТОЧНОСТИ в «виде импортёра». Узнать этот вид у тысяч
#   узлов вручную нельзя (имена, группы-контейнеры, анкоры корня — всё считает сам импортёр). Здесь импортёр
#   `asda_jui_generic_umg.py` выполняется КАК ЕСТЬ, но вместо модуля `unreal` подставлена запись вызовов: каждый
#   `AsdaUmgBuilder.add_*` создаёт запись «виджет: имя, класс, родитель, слот», анкоры корня ставит штатный
#   `_apply_anchor`. Ни одного ассета не создаётся и не читается (кроме списка файлов `Content/AsdaAssets/UI` для
#   выбора папки — так же, как `_layout_pkg` импортёра). Так получается ТОЧНЫЙ вид импортёра для всех окон 4 классов.
#
#   ★ Перегенерацией UI это НЕ является: в редактор ничего не пишется, это чтение данных .JUI тем же кодом.
#
#   Применение:
#     • генератор таблицы шага `asda_ui_label_band.py` (`--plan`): два прогона — с прежним правилом подписи
#       (`ASDA_LABEL_BAND_LEGACY=1`) и с новым — дают закрытый список узлов «было → стало»;
#     • проверка «прогон от вида импортёра сходится к итогу шага» без перегенерации.
#
# Запуск:  python Tools\asda_jui_importer_sim.py <выход.json> [--legacy]
#   Выход: {путь ассета: {"window": id, "root_abs": [l,t,w,h], "rows": [{"n","c","p","slot":[off,anch,align,auto],
#           "tb": {"font","wrap","text"} | null}]}}
import importlib.util
import json
import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
CONTENT_DIR = os.path.normpath(os.path.join(HERE, "..", "Content"))


# ══════════════ запись вызовов вместо `unreal` ══════════════
class _Any(object):
    """Объект-заглушка: принимает любые свойства/вызовы (структуры меты, кадры, enum-значения)."""
    def __init__(self, *a, **k):
        self.__dict__["_p"] = {}

    def set_editor_property(self, k, v):
        self._p[k] = v

    def get_editor_property(self, k):
        return self._p.get(k, _Any())

    def __getattr__(self, k):
        if k.startswith("__"):
            raise AttributeError(k)
        return _Any()

    def __call__(self, *a, **k):
        return _Any()

    def __iter__(self):
        return iter(())

    def __bool__(self):
        return True


class Vector2D(object):
    """Пара чисел (как `unreal.Vector2D`)."""
    def __init__(self, x=0.0, y=0.0):
        self.x, self.y = float(x), float(y)


class Box2D(object):
    """UV-регион (содержимое симуляции не нужно)."""
    def __init__(self, a=None, b=None):
        self.min, self.max = a, b


class LinearColor(object):
    """Цвет RGBA."""
    def __init__(self, r=0.0, g=0.0, b=0.0, a=1.0):
        self.r, self.g, self.b, self.a = r, g, b, a


class Margin(object):
    """Смещения слота канваса: left/top/right/bottom."""
    def __init__(self, left=0.0, top=0.0, right=0.0, bottom=0.0):
        self.left, self.top, self.right, self.bottom = float(left), float(top), float(right), float(bottom)


class Anchors(object):
    """Анкоры слота (minimum/maximum)."""
    def __init__(self, mn=None, mx=None):
        self.minimum = mn or Vector2D()
        self.maximum = mx or Vector2D()


class _Layout(object):
    """`layout_data` слота (offsets/anchors/alignment) — для `asda_ui_text_block_align._set_slot`."""
    def __init__(self, s):
        self._s = s

    def get_editor_property(self, k):
        return {"offsets": self._s.off, "anchors": self._s.anch, "alignment": self._s.align}[k]

    def set_editor_property(self, k, v):
        if k == "offsets":
            self._s.off = v
        elif k == "anchors":
            self._s.anch = v
        elif k == "alignment":
            self._s.align = v


class CanvasPanelSlot(object):
    """Слот канваса: как `ApplyResponsiveSlot` — анкоры 0, alignment 0, offsets = (pos, size)."""
    def __init__(self, pos, size):
        self.off = Margin(pos.x, pos.y, size.x, size.y)
        self.anch = Anchors(Vector2D(0, 0), Vector2D(0, 0))
        self.align = Vector2D(0, 0)
        self.auto = False

    def get_offsets(self):
        return self.off

    def set_offsets(self, m):
        self.off = m

    def set_anchors(self, a):
        self.anch = a

    def get_alignment(self):
        return self.align

    def set_alignment(self, v):
        self.align = v

    def get_editor_property(self, k):
        if k == "layout_data":
            return _Layout(self)
        if k == "auto_size":
            return self.auto
        return _Any()

    def set_editor_property(self, k, v):
        if k == "auto_size":
            self.auto = bool(v)


class Widget(object):
    """Виджет симуляции: имя, класс, родитель, слот, свойства."""
    CLS = "Widget"

    def __init__(self, name):
        self.name = name
        self.parent = None
        self.slot = None
        self.props = {}
        self.children = []

    def get_name(self):
        return self.name

    def get_parent(self):
        return self.parent

    def get_class(self):
        c = self.CLS
        return types.SimpleNamespace(get_name=lambda: c)

    def set_editor_property(self, k, v):
        self.props[k] = v

    def get_editor_property(self, k):
        if k == "slot":
            return self.slot
        return self.props.get(k, _Any())

    def get_children_count(self):
        return len(self.children)

    def get_child_at(self, i):
        return self.children[i]


class PanelWidget(Widget):
    CLS = "PanelWidget"


class CanvasPanel(PanelWidget):
    CLS = "CanvasPanel"


class TextBlock(Widget):
    CLS = "TextBlock"


class Image(Widget):
    CLS = "Image"


class Button(Widget):
    CLS = "Button"


class CheckBox(Widget):
    CLS = "CheckBox"


class EditableText(Widget):
    CLS = "EditableText"


class ComboBoxString(Widget):
    CLS = "ComboBoxString"


class AsdaAnimatedPlane(Widget):
    CLS = "AsdaAnimatedPlane"


class _BP(object):
    """Виджет-блюпринт симуляции: корень `RootCanvas`."""
    def __init__(self, path):
        self.path = path
        self.root = CanvasPanel("RootCanvas")


_CREATED = {}     # путь ассета → _BP (последняя сборка выигрывает — как у импортёра)


def _add(cls, parent, name, pos, size):
    """Создать виджет класса `cls` в `parent` со слотом `ApplyResponsiveSlot`."""
    w = cls(name)
    w.parent = parent
    w.slot = CanvasPanelSlot(pos, size)
    parent.children.append(w)
    return w


class AsdaUmgBuilder(object):
    """Запись вызовов `UAsdaUmgBuilder` (сигнатуры — как у вызовов в импортёре)."""
    @staticmethod
    def create_widget_bp(pkg, name):
        bp = _BP("%s/%s" % (pkg, name))
        _CREATED[bp.path] = bp
        return bp

    @staticmethod
    def get_root_canvas(bp):
        return bp.root

    @staticmethod
    def add_canvas(bp, parent, name, pos, size):
        return _add(CanvasPanel, parent, name, pos, size)

    @staticmethod
    def add_text(bp, parent, name, pos, size, text, font, col, halign, wrap, *rest):
        w = _add(TextBlock, parent, name, pos, size)
        w.tb = {"font": float(font), "wrap": bool(wrap), "text": text, "halign": int(halign)}
        return w

    @staticmethod
    def add_image(bp, parent, name, pos, size, *rest):
        return _add(Image, parent, name, pos, size)

    @staticmethod
    def add_animated_plane(bp, parent, name, pos, size, *rest):
        return _add(AsdaAnimatedPlane, parent, name, pos, size)

    @staticmethod
    def add_hit_zone(bp, parent, name, pos, size):
        return _add(Image, parent, name, pos, size)

    @staticmethod
    def add_styled_button(bp, parent, name, pos, size, *rest):
        return _add(Button, parent, name, pos, size)

    @staticmethod
    def add_button(bp, parent, name, pos, size, *rest):
        return _add(Button, parent, name, pos, size)

    @staticmethod
    def add_option_button(bp, parent, name, pos, size, *rest):
        return _add(Button, parent, name, pos, size)

    @staticmethod
    def add_check_box(bp, parent, name, pos, size, *rest):
        return _add(CheckBox, parent, name, pos, size)

    @staticmethod
    def add_editable_text(bp, parent, name, pos, size, *rest):
        return _add(EditableText, parent, name, pos, size)

    @staticmethod
    def add_combo_box(bp, parent, name, pos, size, *rest):
        return _add(ComboBoxString, parent, name, pos, size)

    @staticmethod
    def apply_button_caption_states(*a):
        return True

    @staticmethod
    def compile_and_save(bp):
        return True


def _list_ui_assets(root, recursive=True, include_folder=False):
    """Список ассетов `/Game/AsdaAssets/UI` по файлам на диске (для `_layout_pkg` импортёра)."""
    base = os.path.join(CONTENT_DIR, "AsdaAssets", "UI")
    out = []
    for d, _s, fs in os.walk(base):
        for f in fs:
            if f.endswith(".uasset"):
                rel = os.path.relpath(os.path.join(d, f[:-7]), CONTENT_DIR).replace(os.sep, "/")
                out.append("/Game/%s.%s" % (rel, f[:-7]))
    return out


def _make_unreal():
    """Модуль-заглушка `unreal` для импортёра и `asda_ui_text_block_align`."""
    u = types.ModuleType("unreal")
    u.__dict__.update(dict(
        Vector2D=Vector2D, Box2D=Box2D, LinearColor=LinearColor, Margin=Margin, Anchors=Anchors,
        CanvasPanelSlot=CanvasPanelSlot, PanelWidget=PanelWidget, CanvasPanel=CanvasPanel, TextBlock=TextBlock,
        Image=Image, Button=Button, CheckBox=CheckBox, EditableText=EditableText, AsdaUmgBuilder=AsdaUmgBuilder,
        AsdaJuiFrame=_Any, AsdaJuiWindowMeta=_Any, AsdaJuiDragHandle=_Any, Name=str,
        Array=lambda *a: [], AutomatedAssetImportData=_Any,
        SlateVisibility=_Any(), WidgetClipping=_Any(), TextJustify=_Any(), TextureFilter=_Any(),
        TextureGroup=_Any(), TextureMipGenSettings=_Any(),
        load_asset=lambda *a, **k: None,
        log=lambda m: None, log_warning=lambda m: None, log_error=lambda m: sys.stderr.write("[sim] %s\n" % m),
    ))
    u.EditorAssetLibrary = types.SimpleNamespace(
        does_asset_exist=lambda p: False, delete_asset=lambda p: True, save_loaded_asset=lambda *a, **k: True,
        does_directory_exist=lambda p: True, make_directory=lambda p: True, load_asset=lambda p: None,
        list_assets=_list_ui_assets)
    u.AssetToolsHelpers = types.SimpleNamespace(get_asset_tools=lambda: _Any())
    return u


def _rows(bp):
    """Плоский список узлов дерева (обход в порядке детей)."""
    out = []

    def walk(w):
        s = w.slot
        slot = None
        if s is not None:
            slot = [[s.off.left, s.off.top, s.off.right, s.off.bottom],
                    [s.anch.minimum.x, s.anch.minimum.y, s.anch.maximum.x, s.anch.maximum.y],
                    [s.align.x, s.align.y], bool(s.auto)]
        out.append({"n": w.name, "c": w.CLS, "p": w.parent.name if w.parent else "-", "slot": slot,
                    "tb": getattr(w, "tb", None)})
        for c in w.children:
            walk(c)
    walk(bp.root)
    return out


def simulate(legacy=False):
    """Выполнить импортёр по 4 классам .JUI в симуляции. Возврат {путь ассета: {...}} (см. шапку)."""
    if legacy:
        os.environ["ASDA_LABEL_BAND_LEGACY"] = "1"
    else:
        os.environ.pop("ASDA_LABEL_BAND_LEGACY", None)
    os.environ.pop("ASDA_ONLY_WIN", None)
    os.environ.pop("ASDA_JUI_PKG_OVERRIDE", None)
    sys.modules["unreal"] = _make_unreal()
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    for m in ("asda_ui_text_block_align", "J"):
        sys.modules.pop(m, None)
    spec = importlib.util.spec_from_file_location("asda_jui_generic_umg_sim",
                                                  os.path.join(HERE, "asda_jui_generic_umg.py"))
    G = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(G)       # __spec__ задан → `_script_main` не запускается (закон импорта)
    G.ensure_texture = lambda base: _Any() if base else None
    G._login_blocked = lambda p: False      # симуляция ничего не пишет: окна логина считаются для отчёта
    G._TRANS = json.load(open(G.TRANS_JSON, encoding="utf-8")) if os.path.isfile(G.TRANS_JSON) else {}
    G._DBGF = None
    G.NO_M = Margin()
    G.SKINS = G.parse_skins_full(G.SKIN_INI)
    _CREATED.clear()
    G._scan_collisions()
    roots_abs = {}
    for cls in G.CLASSES:
        ini = G._ini_of(cls)
        if not os.path.isfile(ini):
            continue
        roots = G.J.parse_jui(ini)
        pkg = "/Game/AsdaAssets/UI/%s" % G.CLASSES[cls]
        byid, rootof, parentof = {}, {}, {}
        for r in roots:
            G._index_nodes(r, r, None, byid, rootof, parentof)
        for r in roots:
            items = [(r, None)]
            for vid, mut in G.WINDOW_VARIANTS.get((cls, G.san(r.idc or r.name)), []):
                items.append((mut(G.copy.deepcopy(r)), vid))
            for node, vid in items:
                before = set(_CREATED)
                try:
                    if vid:
                        vb, vr, vp = {}, {}, {}
                        G._index_nodes(node, node, None, vb, vr, vp)
                        for k, v in byid.items():
                            vb.setdefault(k, v)
                        for k, v in rootof.items():
                            vr.setdefault(k, v)
                        for k, v in parentof.items():
                            vp.setdefault(k, v)
                        G.build_window(pkg, node, vb, vr, vp, cls, variant_id=vid)
                    else:
                        G.build_window(pkg, node, byid, rootof, parentof, cls)
                except Exception as e:                       # noqa: BLE001
                    sys.stderr.write("[sim] окно %s: %s\n" % (getattr(node, "idc", "?"), e))
                for p in set(_CREATED) - before:
                    roots_abs[p] = (cls, vid or G.san(node.idc or node.name), [float(x) for x in node.abs])
                # пересборка того же пути (коллизия) — последняя выигрывает, как у импортёра
                for p in _CREATED:
                    if p not in roots_abs:
                        pass
    out = {}
    for p, bp in _CREATED.items():
        cls, wid, rabs = roots_abs.get(p, ("?", "?", None))
        out[p] = {"class": cls, "window": wid, "root_abs": rabs, "rows": _rows(bp)}
    return out


def main():
    """CLI: python asda_jui_importer_sim.py <выход.json> [--legacy]."""
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    res = simulate(legacy="--legacy" in sys.argv)
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False)
    print("окон %d, узлов %d" % (len(res), sum(len(v["rows"]) for v in res.values())))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля — ничего.
if __name__ == "__main__":
    main()
