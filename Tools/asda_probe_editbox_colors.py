# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_probe_editbox_colors.py — ТОЛЬКО ЧТЕНИЕ. Независимая сверка шага `asda_ui_editbox_colors`.
# ★ ВТОРОЙ МЕТОД: идёт не «от таблицы к ассету», а ОБРАТНО — читает цвет из ассета, переводит
#   linear→sRGB→HEX ARGB и сравнивает со строкой `EXS TextColor` из `.JUI` как ТЕКСТ.
#   Так ошибка в самой функции перевода цвета шага не замаскирует себя.
import importlib.util
import os
import re
import sys

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
INI_DIR = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui"
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"
UI_ROOT = "/Game/AsdaAssets/UI"
EAL = unreal.EditorAssetLibrary

_spec = importlib.util.spec_from_file_location("J", os.path.join(HERE, "asda_jui_parse.py"))
J = importlib.util.module_from_spec(_spec)
sys.modules["J"] = J
_spec.loader.exec_module(J)


def san(s):
    return re.sub(r"[^A-Za-z0-9_]", "_", (s or "").strip()) or "win"


def lin_to_srgb(c):
    c = max(0.0, min(1.0, float(c)))
    return c * 12.92 if c <= 0.0031308 else 1.055 * (c ** (1.0 / 2.4)) - 0.055


def to_hex(col):
    a = int(round(max(0.0, min(1.0, col.a)) * 255.0))
    r = int(round(lin_to_srgb(col.r) * 255.0))
    g = int(round(lin_to_srgb(col.g) * 255.0))
    b = int(round(lin_to_srgb(col.b) * 255.0))
    return "%02X%02X%02X%02X" % (a, r, g, b)


def walk(root):
    stack, out = [root], []
    while stack:
        w = stack.pop()
        if not w:
            continue
        out.append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                stack.append(w.get_child_at(i))
    return out


def main():
    sk = {k.lower(): v for k, v in J.parse_skins_full(SKIN_INI).items()}
    want = {}
    for cls in ("gamemain", "commonwin", "temp", "login"):
        p = os.path.join(INI_DIR, cls + "_jui.ini")
        if not os.path.exists(p):
            continue
        for root in J.parse_jui(p):
            stack = [root]
            while stack:
                n = stack.pop()
                s = sk.get((n.name or "").lower())
                if s and s.get("type") == "EditBox" and n.idc and n.txt_color_hex:
                    want.setdefault(san(n.idc), n.txt_color_hex.upper())
                stack.extend(n.children)

    B = unreal.AsdaUmgBuilder
    good = bad = skipped = 0
    for a in [x for x in EAL.list_assets(UI_ROOT, True, False) if "/WBP_" in x]:
        path = a.split(".")[0]
        bp = EAL.load_asset(path)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            continue
        win = path.rsplit("/", 1)[-1]
        for w in walk(root):
            if not isinstance(w, unreal.EditableText):
                continue
            nm = str(w.get_name())
            exp = want.get(nm)
            if exp is None:
                skipped += 1
                unreal.log("[сверка-поля] ПРОПУСК (нет в данных) %s / %s" % (win, nm))
                continue
            st = w.get_editor_property("widget_style")
            got = to_hex(st.get_editor_property("color_and_opacity").get_editor_property("specified_color"))
            if got == exp:
                good += 1
            else:
                bad += 1
                unreal.log_error("[сверка-поля] РАСХОЖДЕНИЕ %s / %s: в ассете %s, в данных %s"
                                 % (win, nm, got, exp))
    unreal.log("[сверка-поля] ИТОГ: совпало %d, расхождений %d, пропущено (не из .JUI) %d"
               % (good, bad, skipped))


if __name__ == "__main__":
    main()
