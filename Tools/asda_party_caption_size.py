# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Кегль подписей панели отряда 2200: «Выгнать» (2262) и «Выход» (2260) — на 1 пункт мельче,
#   и заголовок «Отряд» (`Party_Txt`) — на те же ~8 % (он КАРТИНКА, кегль к нему неприменим).
#
# ★ ЭТО ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (просьба владельца 2026-08-29). Оформление правится В АССЕТЕ,
#   как и положено; в коде ничего не зашито.
# ★ ОТКАТ: запустить с ASDA_PARTY_CAPTION=revert — вернёт записанные ниже исходные значения.
#   Исходные значения зафиксированы в отчёте `Reports/party_invite_2250.md` §12.
#
# ЗАПУСК: Tools\run_asset_party_caption.bat
import os
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTY_WIN.WBP_PARTY_WIN"
TEXTS = ["PARTY_TOOLBUT00_Text", "PARTY_EXITBUT_Text"]
IMAGE = "Party_Txt"          # префикс плана-картинки «Отряд»
DELTA = -1                   # на сколько пунктов уменьшить подписи
IMG_SCALE = 0.92             # ~те же 8 % для картинки-заголовка
REVERT = os.environ.get("ASDA_PARTY_CAPTION", "").lower() == "revert"


def log(m):
    unreal.log("[кегль-отряд] " + str(m))


def collect(w, acc):
    if not w:
        return
    acc.append(w)
    try:
        n = w.get_children_count()
    except Exception:
        n = 0
    for i in range(n):
        collect(w.get_child_at(i), acc)


def main():
    bp = unreal.load_object(None, ASSET)
    if not bp:
        log("АССЕТ НЕ ЗАГРУЗИЛСЯ: " + ASSET)
        return
    root = None
    try:
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    except Exception:
        pass
    if not root:
        wt = bp.get_editor_property("widget_tree")
        root = wt.get_editor_property("root_widget") if wt else None
    if not root:
        log("КОРЕНЬ НЕ ПОЛУЧЕН")
        return

    acc = []
    collect(root, acc)
    changed = 0

    for w in sorted(acc, key=lambda x: 0 if x.get_name() in TEXTS else 1):
        nm = w.get_name()
        if nm in TEXTS:
            f = w.get_editor_property("font")
            old = f.size
            new = old - DELTA if REVERT else old + DELTA
            if new == old:
                continue
            f.size = new
            w.set_editor_property("font", f)
            log("%-24s кегль %s -> %s" % (nm, old, new))
            changed += 1
        elif nm.startswith(IMAGE + "_"):
            slot = w.slot
            # ⚠ У UCanvasPanelSlot в UE5 нет свойства "offsets" — оно внутри `layout_data`
            #   (FAnchorData: anchors/offsets/alignment). Прямое обращение даёт
            #   «Failed to find property 'offsets'» (обожглись 2026-08-29).
            ld = slot.get_editor_property("layout_data")
            o = ld.offsets
            k = (1.0 / IMG_SCALE) if REVERT else IMG_SCALE
            nw, nh = o.right * k, o.bottom * k
            # центр сохраняем: сдвигаем левый/верхний край на половину убыли
            o.left += (o.right - nw) * 0.5
            o.top += (o.bottom - nh) * 0.5
            o.right, o.bottom = nw, nh
            ld.offsets = o
            slot.set_editor_property("layout_data", ld)
            log("%-24s КАРТИНКА (кегль неприменим): размер -> %.1fx%.1f" % (nm, nw, nh))
            changed += 1

    if changed:
        unreal.EditorAssetLibrary.save_loaded_asset(bp)   # ★ CompileAndSave НЕ сохраняет
        log("сохранено, изменено узлов: %d (режим=%s)" % (changed, "ОТКАТ" if REVERT else "правка"))
    else:
        log("менять нечего")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
