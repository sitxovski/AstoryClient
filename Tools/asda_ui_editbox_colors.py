# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_editbox_colors.py — ★★★ ЦВЕТ ТЕКСТА ПОЛЕЙ ВВОДА ИЗ ДАННЫХ (`EXS TextColor`).
#
# ПРОБЛЕМА (класс С2 аудита `Reports/audit_runtime_style.md`). Импортёр UI отдавал
#   `UAsdaUmgBuilder::AddEditableText` жёсткий БЕЛЫЙ цвет, поэтому цвет из данных терялся у ВСЕХ
#   полей ввода. Правило импортёра исправлено (`asda_jui_generic_umg.py`, ветка `stype == "EditBox"`),
#   но ПЕРЕГЕНЕРАЦИЯ UI ЗАПРЕЩЕНА, значит уже собранные окна надо привести этим шагом.
#
# ИСТОЧНИК — ТОЛЬКО ДАННЫЕ. `EXS TextColor` (эталон `CTextControl_LoadProps @0x12E4E20`,
#   `font_text.c:9717-9744`, поле this+3692) — это НАСТОЯЩИЙ цвет текста контрола, в отличие от
#   `CaptionInfo[2]` (цвет подписи-caption; у 5374 узлов там дизайнерский маркер `ff400040` —
#   закон [[ui-text-color-exs]]). Перевод sRGB→linear делает тот же `asda_jui_parse._hex_argb_to_rgba`,
#   что и импортёр, — ни одного подобранного значения.
#
# СВЕРКА ПО ДАННЫМ (все 4 ini, парсер импортёра): контролов со скином типа `EditBox` — 66
#   (62 `gamemain` + 4 `login`), `EXS TextColor` есть у ВСЕХ 66. Значения разные: `FFC6C6C6`
#   у большинства, `FFFFFFFF` у полей цены лотка, `FF9DFF9D` у имени питомца, `FFFF1740` у крика,
#   `FF000000` у «о себе», `FFDCDCDC` у чата лотка.
#
# ⚠ ПОЛЕ ЧАТА 2609 (`CHAT_NEW_Edit2`) — ИСКЛЮЧЕНИЕ САМОГО ОРИГИНАЛА, а не наш костыль: эталон
#   `chat.c:826-833` ПОВЕРХ данных ставит ему `0xCCFFFFFF` (там же рядом `ctrlEditSetTextLength`).
#   Порт делает то же в `UAsdaChatWindowSubsystem::BuildInputWidgets`. Шаг кладёт в ассет ДАННЫЕ
#   (`FFC6C6C6`), рантайм оригинала их перекрывает — слои те же, что в игре.
#
# ⚠ ComboBox НЕ ТРОГАЕМ. Скан всех 4 ini: контролов со скином типа `ComboBox` — 59, и `EXS TextColor`
#   нет НИ У ОДНОГО. Брать вместо него `CaptionInfo[2]` было бы выдумкой; цвет строк раскрывающегося
#   списка оригинал берёт из скина списка (`DarkGrey_List CaptionColor = FFE9D6BC`), и порт уже так
#   и делает в общей точке `UAsdaJuiComboBox` (`AsdaJuiComboBox.cpp:107`).
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_editbox_colors.py
#   Сухой прогон (ничего не сохраняет):  set ASDA_EDITCOL_DRYRUN=1
import importlib.util
import os
import re
import sys
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
INI_DIR = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui"
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"
UI_ROOT = "/Game/AsdaAssets/UI"
CLASSES = ("gamemain", "commonwin", "temp", "login")
EPS = 1.0 / 512.0          # допуск сравнения цвета (полбайта) — шаг не должен «дёргать» ассет впустую

EAL = unreal.EditorAssetLibrary

_spec = importlib.util.spec_from_file_location("J", os.path.join(HERE, "asda_jui_parse.py"))
J = importlib.util.module_from_spec(_spec)
sys.modules["J"] = J
_spec.loader.exec_module(J)


def log(m):
    unreal.log("[цвет-поля] %s" % m)


def san(s):
    """Тот же санитайзер имени виджета, что у импортёра (`asda_jui_generic_umg.san`)."""
    return re.sub(r"[^A-Za-z0-9_]", "_", (s or "").strip()) or "win"


def _login_blocked(path):
    """★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 14): True — окно `path` принадлежит сцене логина (asda_login_guard: UI/Login, кроме
    LOGIN_SHARED_EXCEPTIONS), а ASDA_ALLOW_LOGIN_WRITE != 1: окно пропустить, НЕ загружая. Без строки на каждое окно (quiet):
    итог [LOGIN-GUARD] печатает вызывающий. Ленивый импорт — при импорте модуля ничего не делает."""
    import sys as _sys_lg
    _here = os.path.dirname(os.path.abspath(__file__))   # Tools — для asda_login_guard
    if _here not in _sys_lg.path:
        _sys_lg.path.insert(0, _here)
    from asda_login_guard import package_blocked_for   # ★ дедуп №2 Т18: имя инструмента — из __file__
    return package_blocked_for(__file__, path, quiet=True)


def load_table():
    """(окно, idc) → (rgba, hex). Только контролы со СКИНОМ типа `EditBox` и живым `EXS TextColor`."""
    skins = J.parse_skins_full(SKIN_INI)
    sk = {k.lower(): v for k, v in skins.items()}
    want = {}
    by_idc = {}
    for cls in CLASSES:
        p = os.path.join(INI_DIR, cls + "_jui.ini")
        if not os.path.exists(p):
            log("НЕТ файла %s — пропуск класса" % p)
            continue
        for root in J.parse_jui(p):
            top = san(root.idc or root.name)
            stack = [root]
            while stack:
                n = stack.pop()
                s = sk.get((n.name or "").lower())
                if s and s.get("type") == "EditBox" and n.idc and n.txt_color:
                    want[(top, san(n.idc))] = (n.txt_color, n.txt_color_hex)
                    by_idc.setdefault(san(n.idc), {})[n.txt_color_hex] = n.txt_color
                stack.extend(n.children)
    # idc, у которых цвет ОДИН и тот же во всех окнах, применимы и без совпадения имени окна
    uniq = {k: list(v.items())[0] for k, v in by_idc.items() if len(v) == 1}
    return want, uniq


def walk_widgets(root):
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


def _same(c, rgba):
    return (abs(c.r - rgba[0]) < EPS and abs(c.g - rgba[1]) < EPS
            and abs(c.b - rgba[2]) < EPS and abs(c.a - rgba[3]) < EPS)


def main():
    dry = bool(os.environ.get("ASDA_EDITCOL_DRYRUN"))
    want, uniq = load_table()
    log("таблица: %d полей ввода из данных, из них однозначных по idc %d%s"
        % (len(want), len(uniq), "  [СУХОЙ ПРОГОН]" if dry else ""))

    assets = [a for a in EAL.list_assets(UI_ROOT, True, False) if "/WBP_" in a]
    B = unreal.AsdaUmgBuilder
    seen = fixed = already = nodata = 0
    changed_assets = 0
    login_skipped = []   # ★ 15.09.2026 (раздел 14): окна сцены логина, пропущенные защитой
    misses = []

    for a in assets:
        path = a.split(".")[0]
        if _login_blocked(path):   # ★ 15.09.2026 (раздел 14): окно сцены логина — только с разрешением
            login_skipped.append(path.rsplit("/", 1)[-1])
            continue
        bp = EAL.load_asset(path)
        if not bp:
            continue
        root = B.get_root_canvas(bp)
        if not root:
            continue
        win = path.rsplit("/", 1)[-1]
        if win.startswith("WBP_"):
            win = win[4:]
        touched = 0
        for w in walk_widgets(root):
            if not isinstance(w, unreal.EditableText):
                continue
            seen += 1
            nm = str(w.get_name())
            ent = want.get((win, nm))
            if ent is None and nm in uniq:                # окно названо иначе — берём по idc
                hx, rgba = uniq[nm]
                ent = (rgba, hx)
            if ent is None:
                nodata += 1
                misses.append("%s/%s" % (win, nm))
                continue
            rgba, hx = ent
            try:
                st = w.get_editor_property("widget_style")
                cur = st.get_editor_property("color_and_opacity")
                col = cur.get_editor_property("specified_color")
                if _same(col, rgba):
                    already += 1
                    continue
                if not dry:
                    st.set_editor_property(
                        "color_and_opacity",
                        unreal.SlateColor(specified_color=unreal.LinearColor(*rgba),
                                          color_use_rule=unreal.SlateColorStylingMode.USE_COLOR_SPECIFIED))
                    w.set_editor_property("widget_style", st)
                fixed += 1
                touched += 1
                log("  %s / %s → %s" % (win, nm, hx))
            except Exception as e:                        # noqa: BLE001
                unreal.log_warning("[цвет-поля] %s / %s: %s" % (win, nm, e))

        if touched:
            changed_assets += 1
            if not dry:
                # ★ `CompileAndSave` только КОМПИЛИРУЕТ (имя врёт) — сохранение делаем сами.
                B.compile_and_save(bp)
                if not EAL.save_loaded_asset(bp, False):
                    raise RuntimeError("не сохранён ассет %s" % path)

    log("окон %d, полей ввода найдено %d: приведено %d, уже верных %d, без данных %d, окон изменено %d"
        % (len(assets), seen, fixed, already, nodata, changed_assets))
    if misses:
        log("без данных (в .JUI такого EditBox нет — наши собственные узлы): %s" % ", ".join(sorted(misses)))
    if login_skipped:   # ★ 15.09.2026 (раздел 14): итог пропуска окон сцены логина одной строкой
        unreal.log_warning("[LOGIN-GUARD] [цвет-поля] окон сцены логина пропущено: %d (%s) — правка только с явного разрешения "
                           "владельца и ASDA_ALLOW_LOGIN_WRITE=1" % (len(login_skipped), ", ".join(login_skipped)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:                                     # noqa: BLE001
        unreal.log_error("[цвет-поля] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
