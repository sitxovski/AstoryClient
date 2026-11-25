# Copyright (c) 2026 @sitxovski (Discord)
# _probe_button_caption_states.py — дамп СОСТОЯНИЙ ПОДПИСИ у кнопок окна (JUI 570 и др.).
#   Для каждой кнопки: заданы ли foreground'ы стиля (цвет подписи по состояниям) и какие,
#   плюс NormalPadding/PressedPadding (смещение подписи `CaptionPoint`) и наличие соседа
#   `<Кнопка>_Text`. Проверка правки «hover & click у реплик игрока» (окно разговора с NPC).
#   Обоснование механизма — `Reports/npc_dialog_button_states.md`.
import unreal

PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Quest/WBP_NPC_LINE_NPC.WBP_NPC_LINE_NPC",
]


def L(m):
    unreal.log("[BTNCAP] " + str(m))


def _hex(c):
    """FLinearColor (линейный) -> sRGB-hex AARRGGBB, чтобы сверять со строкой скина."""
    def s(v):
        v = max(0.0, min(1.0, v))
        v = v * 12.92 if v <= 0.0031308 else 1.055 * (v ** (1.0 / 2.4)) - 0.055
        return int(round(v * 255.0))
    return "%02X%02X%02X%02X" % (int(round(max(0.0, min(1.0, c.a)) * 255.0)), s(c.r), s(c.g), s(c.b))


def _fg(sc):
    """FSlateColor -> (задан ли явный цвет, hex)."""
    try:
        rule = sc.get_editor_property("color_use_rule")
    except Exception:
        rule = None
    spec = sc.get_editor_property("specified_color")
    is_spec = ("USE_COLOR_SPECIFIED" in str(rule))   # enum печатается как «<SlateColorStylingMode.USE_COLOR_SPECIFIED: 0>»
    return is_spec, _hex(spec), str(rule)


def main():
    for p in PATHS:
        L("=" * 96)
        L("АССЕТ: " + p)
        bp = unreal.load_object(None, p)
        if not bp:
            L("  НЕ НАЙДЕН")
            continue
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
        found = [0]

        def walk(w):
            if not w:
                return
            if isinstance(w, unreal.Button):
                found[0] += 1
                st = w.get_editor_property("widget_style")
                n_ok, n_hex, n_rule = _fg(st.get_editor_property("normal_foreground"))
                h_ok, h_hex, _ = _fg(st.get_editor_property("hovered_foreground"))
                p_ok, p_hex, _ = _fg(st.get_editor_property("pressed_foreground"))
                np_ = st.get_editor_property("normal_padding")
                pp_ = st.get_editor_property("pressed_padding")
                # ★ Состояния КАРТИНКИ: у стрелок (Arrow_Back/Next) кадры отличаются ТОЛЬКО тинтом
                #   `Diffuse`, поэтому сравнение тинтов кистей — достаточная проверка «состояния есть».
                #   (UV-регион кисти в Python не экспонирован — см. DescribeCheckBoxStyle в C++.)
                def _tint(br):
                    try:
                        return _hex(br.get_editor_property("tint_color").specified_color)
                    except Exception:
                        return "??"
                bn = _tint(st.get_editor_property("normal"))
                bh = _tint(st.get_editor_property("hovered"))
                bp_ = _tint(st.get_editor_property("pressed"))
                foc = w.get_editor_property("is_focusable")
                L("  %-22s подпись:%s N=%s H=%s P=%s pad0=(%.0f,%.0f) pad2=(%.0f,%.0f) | картинка-тинт N=%s H=%s P=%s | фокус=%s"
                  % (w.get_name(), "ЕСТЬ" if (n_ok and h_ok and p_ok) else "нет ",
                     n_hex, h_hex, p_hex, np_.left, np_.top, pp_.left, pp_.top,
                     bn, bh, bp_, "ДА (кольцо!)" if foc else "нет"))
            for i in range(w.get_children_count() if hasattr(w, "get_children_count") else 0):
                walk(w.get_child_at(i))

        walk(root)
        # Соседи-подписи: рантайм ищет `<Кнопка>_Text` — проверяем, что они есть и не ловят мышь.
        names = []

        def walk2(w):
            if not w:
                return
            names.append(w.get_name())
            for i in range(w.get_children_count() if hasattr(w, "get_children_count") else 0):
                walk2(w.get_child_at(i))

        walk2(root)

        def walk3(w):
            if not w:
                return
            if isinstance(w, unreal.Button):
                cap = w.get_name() + "_Text"
                L("    подпись %-26s %s" % (cap, "ЕСТЬ" if cap in names else "— нет (caption пустой)"))
            for i in range(w.get_children_count() if hasattr(w, "get_children_count") else 0):
                walk3(w.get_child_at(i))

        walk3(root)
        L("  кнопок в окне: %d" % found[0])
    L("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
