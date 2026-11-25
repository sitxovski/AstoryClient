# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★★★ СИСТЕМНОЕ ЛЕЧЕНИЕ «ФИОЛЕТОВОГО ТЕКСТА» В УЖЕ ПОРТИРОВАННЫХ ОКНАХ.
#
# ПРИЧИНА (реверс, не догадка). У контрола JUI ДВА разных цвета:
#   • `CaptionInfo = "offX offY ARGB alignMask"` — цвет ПОДПИСИ-caption. Рисуется ТОЛЬКО когда
#     непустой `CaptionText`. У 5374 узлов там дизайнерский маркер `ff400040` (фиолетовый).
#   • блок `EXS … TextColor = "AARRGGBB" … EXE` — НАСТОЯЩИЙ цвет текста контрола. Эталон:
#     `CTextControl_LoadProps @0x12E4E20` (`RE/etalon/font_text.c:9717-9744`):
#         TextPos → this+4/this+612, TextContent → CControl_SetText,
#         TextColor → this+3692, TextAlign → this+3696.
# Импортёр `asda_jui_generic_umg.py` красил Text-контролы цветом из `CaptionInfo` — отсюда
# фиолетовые заголовки окон (ловилось уже ПЯТЫЙ раз: окно смерти, справка чата, подпись кулдауна,
# заголовки 2590 и 2700). Импортёр ИСПРАВЛЕН; этот скрипт чинит АССЕТЫ, уже лежащие в проекте,
# чтобы не гонять полную регенерацию 251 окна.
#
# ЧТО ДЕЛАЕТ: для каждого топ-окна JUI собирает пары «имя виджета → EXS TextColor» (только
# Text-контролы, у которых импортёр создаёт именованный `UTextBlock`) и зовёт
# `UAsdaUmgBuilder::ApplyJuiTextColors` для соответствующего `WBP_*`.
#
# ЗАПУСК:  Tools\run_headless.bat Tools\asda_ui_fix_text_colors.py
#   ASDA_FIXCOL_ALL=1 — красить ВСЕ расхождения (по умолчанию — только плейсхолдер `ff400040`).
#   ASDA_FIXCOL_WIN=<имя> — одно окно (для проверки).
import unreal, importlib.util, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
INI_DIR = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui"
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"
CLASSES = {"gamemain": "GameMain", "commonwin": "CommonWin", "temp": "Temp", "login": "Login"}

spec = importlib.util.spec_from_file_location("Jc", os.path.join(HERE, "asda_jui_parse.py"))
J = importlib.util.module_from_spec(spec); sys.modules["Jc"] = J; spec.loader.exec_module(J)

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

ONLY_PH = os.environ.get("ASDA_FIXCOL_ALL", "") == ""
ONLY_WIN = os.environ.get("ASDA_FIXCOL_WIN", "")


import re
def san(s):
    """Санитайз имени — 1:1 с генератором (`asda_jui_generic_umg.san`)."""
    return re.sub(r"[^A-Za-z0-9_]", "_", (s or "").strip()) or "win"


def skin_types():
    """имя скина (lower) → Type (нужен, чтобы отобрать контролы Type=Text)."""
    m, cur = {}, None
    import io, re
    def val(line):
        mm = re.match(r'\s*[\w]+\s*=\s*"(.*)"\s*$', line)
        return mm.group(1) if mm else line.split("=", 1)[-1].strip()
    for line in io.open(SKIN_INI, "r", encoding="cp1251", errors="replace"):
        s = line.strip()
        if s.startswith("Name ="):
            cur = val(line).lower()
        elif s.startswith("Type =") and cur:
            m[cur] = val(line)
    return m




def collect(node, out, conflicts):
    """Пары «имя виджета → EXS TextColor» для всех Text-контролов поддерева."""
    st = SK.get((node.name or "").lower(), "")
    if node.idc and st == "Text" and node.txt_color:
        key = san(node.idc)
        if key in out and out[key] != node.txt_color:
            conflicts.append(key)
        else:
            out[key] = node.txt_color
    for c in node.children:
        collect(c, out, conflicts)


def layout_map():
    m = {}
    for a in EAL.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False):
        p = a.split(".")[0]
        m[p.rsplit("/", 1)[-1]] = p
    return m


# ★★★ ОКНА, ОБЪЯВЛЕННЫЕ В ДВУХ КЛАССАХ .JUI, разведены по разным ассетам суффиксом класса
#   (`WBP_COMMONWIN_MSGBOX_CommonWin`, `WBP_Job_Guide_Win_Temp` — см. `asda_jui_generic_umg._wsuffix`).
#   Без такого же правила здесь красилась бы только главная сборка, а вторая осталась бы с
#   фиолетовым плейсхолдером.
_PRIORITY = ["gamemain", "commonwin", "temp", "login"]
_COLLIDE = {}


def _ini_of(cls):
    p = os.path.join(INI_DIR, "%s_loc.ini" % cls)
    return p if os.path.isfile(p) else os.path.join(INI_DIR, "%s_jui.ini" % cls)


def scan_collisions():
    _COLLIDE.clear()
    for cls in CLASSES:
        ini = _ini_of(cls)
        if not os.path.isfile(ini):
            continue
        for r in J.parse_jui(ini):
            _COLLIDE.setdefault(san(r.idc or r.name), []).append(cls)


def wsuffix(base, cls):
    owners = _COLLIDE.get(base)
    if not owners or len(owners) < 2:
        return ""
    main_cls = sorted(owners, key=lambda c: _PRIORITY.index(c) if c in _PRIORITY else 99)[0]
    return "" if cls == main_cls else "_" + CLASSES[cls]


def main():
    lm = layout_map()
    # ★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): класс login (и любое окно, чей ассет лежит в UI/Login,
    #   кроме общих LOGIN_SHARED_EXCEPTIONS) — пакеты сцены логина; без ASDA_ALLOW_LOGIN_WRITE=1 окно не загружается и не сохраняется.
    sys.path.insert(0, HERE)   # Tools — для asda_login_guard
    import asda_login_guard as _LG
    login_skipped = []
    scan_collisions()
    total_win = total_fix = total_skip = 0
    for cls, folder in CLASSES.items():
        ini = os.path.join(INI_DIR, "%s_loc.ini" % cls)
        if not os.path.isfile(ini):
            ini = os.path.join(INI_DIR, "%s_jui.ini" % cls)
        if not os.path.isfile(ini):
            unreal.log_warning("[цвет-текста] нет INI %s" % ini)
            continue
        roots = J.parse_jui(ini)
        unreal.log("[цвет-текста] === %s: топ-окон %d ===" % (cls, len(roots)))
        for r in roots:
            base = san(r.idc or r.name)
            wname = "WBP_" + base + wsuffix(base, cls)
            if ONLY_WIN and ONLY_WIN.lower() not in (wname.lower(), base.lower()):
                continue
            path = lm.get(wname)
            if not path:
                total_skip += 1
                continue
            if _LG.login_package_blocked(path, "asda_ui_fix_text_colors.py", quiet=True):   # ★ (раздел 16): окно сцены логина
                login_skipped.append(path)
                continue
            pairs, conflicts = {}, []
            collect(r, pairs, conflicts)
            if not pairs:
                continue
            bp = EAL.load_asset(path)
            if not bp:
                unreal.log_warning("[цвет-текста] не грузится %s" % path)
                continue
            names = list(pairs.keys())
            cols = [unreal.LinearColor(*pairs[n]) for n in names]
            n = 0
            if ONLY_PH:
                for ph in PLACEHOLDERS:
                    n += B.apply_jui_text_colors(bp, names, cols, True, ph)
            else:
                n = B.apply_jui_text_colors(bp, names, cols, False, PLACEHOLDERS[0])
            total_win += 1
            total_fix += n
            if n:
                EAL.save_loaded_asset(bp)
                unreal.log("[цвет-текста] %-44s исправлено %d из %d Text-контролов%s" % (
                    wname, n, len(names), (" (конфликт имён: %s)" % ",".join(conflicts)) if conflicts else ""))
    if login_skipped:   # ★ 15.09.2026 (раздел 16): итог пропуска одной строкой
        unreal.log_warning("[LOGIN-GUARD] [цвет-текста] окон сцены логина пропущено: %d (%s%s) — цвета не менялись (только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"
                           % (len(login_skipped), ", ".join(p.rsplit("/", 1)[-1] for p in login_skipped[:5]),
                              " …" if len(login_skipped) > 5 else ""))
    unreal.log("[цвет-текста] === ИТОГ: окон обработано %d, контролов перекрашено %d, окон без ассета %d ===" % (
        total_win, total_fix, total_skip))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global PLACEHOLDERS, SK
    # ★ Плейсхолдер `ff400040` встречается в ассетах В ДВУХ ВИДАХ:
    #   • «сырой» 0x40/255 = 0.2510 — так его записал импортёр ДО введения гамма-правила
    #     (`ui-srgb-gamma-rule`); именно этот вариант и лежит в текущих 251 окне (проверено дампом
    #     `asda_ui_dump_window.py`: у CHAT_TAB_WIN_TextMove цвет 0.2510/0/0.2510);
    #   • «линейный» 0.0513 — так его записал бы импортёр ПОСЛЕ введения гаммы.
    #   Проверяем оба, иначе часть окон осталась бы фиолетовой.
    PH_LIN = J._hex_argb_to_rgba("FF400040")
    PLACEHOLDERS = [
        unreal.LinearColor(PH_LIN[0], PH_LIN[1], PH_LIN[2], PH_LIN[3]),
        unreal.LinearColor(0x40 / 255.0, 0.0, 0x40 / 255.0, 1.0),
    ]
    SK = skin_types()
    main()


# ★★★ ЗАКОН tools-no-import-side-effects: раньше `main()` стоял голым на уровне модуля —
#   любой импорт этого файла молча перекрашивал бы все окна проекта.
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
