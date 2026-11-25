# Copyright (c) 2026 @sitxovski (Discord)
# Проверка пункта С6 аудита: «ассеты отстают от уже исправленного импортёра».
#   После перегенерации 2026-08-27 надо понять, ПРАВИЛЬНУЮ ли геометрию/стиль теперь кладёт
#   импортёр в WBP — и можно ли снимать рантайм-компенсации в коде.
#   ⚠ ТОЛЬКО ЧТЕНИЕ. Ничего не меняет и не сохраняет.
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_c6_compensations.py
import traceback
import unreal

# Узлы, вокруг которых стоят компенсации в коде.
TARGETS = [
    # (ассет, имя узла, чем интересен)
    ("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_MINISTATE_WIN", "MINISTATE_HPTEXT",
     "EXS TextAlign=5 (центр+центр) против CaptionInfo=0; ApplyJuiTextAlign"),
    ("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_MINISTATE_WIN", "MINISTATE_MPTEXT",
     "то же"),
    ("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_QUICK_NEW_WIN", "QUICK_NEW_TRAY00",
     "счётчик стака: TextAlign=9 (центр+низ); ApplyStackTextStyle"),
]


def log(m):
    unreal.log("[С6] %s" % m)


def find(bp, name):
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
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
    for path, name, why in TARGETS:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            log("НЕТ АССЕТА %s" % path)
            continue
        bp = unreal.EditorAssetLibrary.load_asset(path)
        node = find(bp, name)
        if not node:
            log("%s / %s — УЗЕЛ НЕ НАЙДЕН (%s)" % (path.rsplit("/", 1)[-1], name, why))
            continue
        info = []
        try:
            slot = node.get_editor_property("slot")
            off = slot.get_editor_property("layout_data").get_editor_property("offsets")
            info.append("off(L%.1f T%.1f R%.1f B%.1f)" % (off.left, off.top, off.right, off.bottom))
        except Exception as e:
            info.append("слот: %s" % e)
        if isinstance(node, unreal.TextBlock):
            try:
                f = node.get_editor_property("font")
                info.append("кегль=%d" % f.get_editor_property("size"))
                info.append("обводка=%d" % f.get_editor_property("outline_settings")
                            .get_editor_property("outline_size"))
            except Exception as e:
                info.append("шрифт: %s" % e)
            # Justification объявлено protected — читаем рефлексией через export_text.
            try:
                info.append("justify=%s" % node.get_editor_property("justification"))
            except Exception:
                info.append("justify=<protected>")
        log("%s / %s: %s   ← %s" % (path.rsplit("/", 1)[-1], name, "  ".join(info), why))


try:
    main()
except Exception:
    unreal.log_error("[С6] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
