# Copyright (c) 2026 @sitxovski (Discord)
# Диагностика: какие текстура/UV/цвет реально стоят у контролов полос в собранных плашках.
import unreal

def log(m): unreal.log_warning("[probe] %s" % m)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for asset in ("WBP_NAMEPLATE_MON", "WBP_NAMEPLATE_MAN1"):
        path = "/Game/AsdaAssets/UI/GameMain/%s" % asset
        bp = unreal.EditorAssetLibrary.load_asset(path)
        if not bp:
            log("%s: НЕ загружен" % asset); continue
        wt = bp.get_editor_property("widget_tree") if hasattr(bp, "get_editor_property") else None
        widgets = []
        try:
            widgets = unreal.AsdaUmgBuilder.list_widgets(bp)
        except Exception:
            pass
        log("%s: виджетов=%s" % (asset, len(widgets) if widgets else "?"))
        for w in (widgets or []):
            try:
                nm = str(w.get_name())
            except Exception:
                continue
            if "GAUGE" not in nm.upper() and "HP" not in nm.upper():
                continue
            if isinstance(w, unreal.Image):
                b = w.get_editor_property("brush")
                res = b.get_editor_property("resource_object")
                tint = b.get_editor_property("tint_color")
                sc = tint.get_editor_property("specified_color") if tint else None
                log("  %-26s tex=%-22s tint=(%.3f %.3f %.3f %.3f)" % (
                    nm, (res.get_name() if res else "None"),
                    sc.r if sc else -1, sc.g if sc else -1, sc.b if sc else -1, sc.a if sc else -1))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
