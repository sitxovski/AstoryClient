# Copyright (c) 2026 @sitxovski (Discord)
# dump_nameplate_brushes.py — дамп ФАКТИЧЕСКИХ брюшей всех контролов WBP_NAMEPLATE_MAN1
#   (поиск контрола-виновника «коричневый квадратик + оранжевая дуга» на плашке RINA).
#   Для каждого Image: texture, UVRegion, DrawAs, Margin, Tint, ImageSize, slot, visibility.
import unreal

def L(m): unreal.log_warning("[NPDUMP] " + str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for asset in ("WBP_NAMEPLATE_MAN1", "WBP_NAMEPLATE_MAN", "WBP_NAMEPLATE_MON"):
        bp = unreal.load_object(None, "/Game/AsdaAssets/UI/GameMain/%s.%s" % (asset, asset))
        if not bp:
            L("НЕТ ассета %s" % asset); continue
        canvas = unreal.AsdaUmgBuilder.get_root_canvas(bp)   # widget_tree не экспонирован — берём канвас через билдер
        allw = []
        def collect(w):
            if not w: return
            allw.append(w)
            try:
                for i in range(w.get_children_count()):
                    collect(w.get_child_at(i))
            except Exception:
                pass
        collect(canvas)
        L("=== %s: контролов %d ===" % (asset, len(allw)))
        for w in allw:
            nm = w.get_name()
            cls = w.get_class().get_name()
            vis = str(w.get_editor_property("visibility"))
            pos = ""
            try:
                o = w.slot.get_editor_property("offsets")
                pos = "(%.1f,%.1f %.0fx%.0f)" % (o.left, o.top, o.right, o.bottom)
            except Exception:
                pass
            if isinstance(w, unreal.Image):
                b = w.get_editor_property("brush")
                def P(obj, *names):
                    for n in names:
                        try: return obj.get_editor_property(n)
                        except Exception: pass
                    return None
                tex = P(b, "resource_object")
                uv = P(b, "uv_region", "UVRegion")
                mg = P(b, "margin")
                draw = str(P(b, "draw_as"))
                tc = P(b, "tint_color")
                tint = P(tc, "specified_color") if tc else None
                isz = P(b, "image_size")
                if tint is None: tint = unreal.LinearColor(9, 9, 9, 9)
                if isz is None: isz = unreal.Vector2D(-1, -1)
                if mg is None: mg = unreal.Margin(-1, -1, -1, -1)
                uvs = str(uv)
                L("%-36s IMG vis=%-22s slot=%s draw=%s tex=%s size=%s margin=%s tint=%s uv=%s" % (
                    nm, vis, pos, draw, tex.get_name() if tex else "None", str(isz), str(mg), str(tint), uvs))
            else:
                L("%-36s %s vis=%s slot=%s" % (nm, cls, vis, pos))
    L("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
