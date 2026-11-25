# Copyright (c) 2026 @sitxovski (Discord)
# Проверка: сколько ячеек-иконок и подписей количества реально лежит в группах сумок 1…8
#   окна склада `WBP_WARE_WIN`. Нужна после `asda_ware_bag58_cell_names.py`, который достраивает
#   подписи сумкам 5…8 (в `.JUI` их ячейки объявлены с пустым idc и приехали безымянными).
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_ware_bag_labels.py
import traceback
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Inventory/WBP_WARE_WIN"


def log(msg):
    unreal.log("[проверка-сумок] %s" % msg)


def find_panel(root, name):
    stack = [root]
    while stack:
        w = stack.pop()
        if not w:
            continue
        try:
            if w.get_name() == name:
                return w
        except Exception:
            pass
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                stack.append(w.get_child_at(i))
    return None


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    if not bp:
        log("НЕ НАЙДЕН ассет")
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    if not root:
        log("нет корневого канваса")
        return

    for n in range(1, 9):
        grp = find_panel(root, "WAREBAG%d_TRAYIcon_grp" % n)
        if not isinstance(grp, unreal.PanelWidget):
            log("сумка %d: группы нет" % n)
            continue
        icons = labels = 0
        sample_pos = ""
        for i in range(grp.get_children_count()):
            ch = grp.get_child_at(i)
            if not ch:
                continue
            nm = ch.get_name()
            if isinstance(ch, unreal.TextBlock) and nm.startswith("WAREBAG%d_TRAY" % n) \
               and nm[-2:].isdigit():
                labels += 1
                if not sample_pos:
                    try:
                        off = ch.get_editor_property("slot").get_editor_property(
                            "layout_data").get_editor_property("offsets")
                        sample_pos = "первая подпись off(L%.0f T%.0f R%.0f B%.0f)" % (
                            off.left, off.top, off.right, off.bottom)
                    except Exception:
                        pass
            elif isinstance(ch, unreal.Image):
                icons += 1
        log("сумка %d: иконок %d, подписей %d  %s" % (n, icons, labels, sample_pos))


try:
    main()
except Exception:
    unreal.log_error("[проверка-сумок] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
