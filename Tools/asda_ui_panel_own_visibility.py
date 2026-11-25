# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_panel_own_visibility.py — ★★★ ВЕРНУТЬ УЗЛУ ЕГО СОБСТВЕННУЮ ВИДИМОСТЬ ИЗ ДАННЫХ.
#
# ПРОБЛЕМА (класс дефектов импортёра **S6**, найден 2026-08-27 по жалобе владельца на аукцион:
#   «когда нажимаю „мои предметы“ … на заднем фоне ПУСТОЕ окно открылось»).
#   Если у контрола-контейнера в `.JUI` стоит `Visible = "0"`, импортёр помечает `COLLAPSED`
#   НЕ ТОЛЬКО сам контейнер, но и ВСЁ ЕГО ПОДДЕРЕВО — собственный флаг `Visible = "1"` детей
#   при этом теряется НАВСЕГДА. В оригинале так не бывает: видимость контрола проверяется
#   ПОЦЕПОЧЕЧНО (`CControl_IsVisibleChain`), поэтому у ребёнка свой флаг живёт независимо, и
#   стоит показать родителя — дети появляются.
#   Следствие в порте: код показывает панель, а внутри неё по-прежнему всё погашено ⇒ пустое окно.
#
# ГДЕ ЭТО ОБНАРУЖЕНО. Окно АУКЦИОНА `EXCHANGE_WIN` (13000): панель «мои лоты» `EXCH_MYITEM` 13200
#   объявлена `Visible = "0"` (её показывает приёмник 9908), и все её 95 внутренних узлов приехали
#   в ассет `COLLAPSED`, хотя в данных у каждого `Visible = "1"`. Замер: живой дамп дерева виджета
#   (`Tools/_probe_exchange_tree.py`) против дерева `.JUI` — 95 расхождений, обратных нет.
#
# ЧТО ДЕЛАЕТ ШАГ (идемпотентно). Для перечисленных ОКОН сверяет КАЖДЫЙ узел ассета с его узлом в
#   `.JUI` и ставит видимость по СОБСТВЕННОМУ флагу узла:
#     • `Visible = 0`      → `COLLAPSED`;
#     • `Visible = 1`      → `SELF_HIT_TEST_INVISIBLE` для контейнеров и планов-картинок,
#                            `VISIBLE` для интерактива (кнопка/чекбокс/поле ввода/список),
#                            `HIT_TEST_INVISIBLE` для подписей-`TextBlock`
#                            (закон `ui-button-hover-click-rule`: подпись не должна съедать клик).
#   ★ Сам КОРЕНЬ панели не трогаем вовсе: «какая панель открыта» — это СОСТОЯНИЕ, им управляет код
#     подсистемы, и переписывать его в ассете нельзя.
#   ★ Узлы, которых нет в данных (созданы шагами пост-импорта: `_OPT<i>`, `_Text<n>`, `_ICO<nn>`,
#     клик-планы `*_HIT`), НЕ ТРОГАЕМ — у них своя логика и свои шаги.
#
# ОХВАТ НАМЕРЕННО УЗКИЙ. В таблице `WINDOWS` только те окна, где дефект ДОКАЗАН пробой. Прогонять
#   его по всем 251 окну без разрешения владельца нельзя (закон `no-ui-regeneration-without-permission`):
#   это перекрасило бы видимость тысяч узлов разом.
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_panel_own_visibility.py
#   Сухой прогон: set ASDA_PANELVIS_DRYRUN=1
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_jui_parse as JP

EAL = unreal.EditorAssetLibrary
import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402
JUI_GAMEMAIN = os.path.join(_P.DECODED_JUI, "gamemain_jui.ini")

# (ассет, ini окна, IDNum окна, [IDNum поддеревьев, которые лечим], [idc-корни, которые НЕ трогать])
#   ★ Корни панелей исключены: их видимость — состояние, им правит код подсистемы.
WINDOWS = [
    ("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN", JUI_GAMEMAIN, 13000,
     [13005, 13200, 13400],
     ["EXCH_REGISTRATION", "EXCH_MYITEM", "EXCHANGER_WIN",
      # ★ Наборы вкладок 13408/13409/13500 и фильтр самоцветов 13501/13503 — тоже СОСТОЯНИЕ
      #   (какой набор категорий показан), их переключает подсистема аукциона.
      "EXCHANGER_RADIOBTN1", "EXCHANGER_RADIOBTN2", "EXCHANGER_RADIOBTN3",
      "EXCHANGER_TXT4", "EXCHANGER_SEARCH4",
      # SEL-рамки выделения включаются только на время выбора.
      "EXCHANGE_SELECTED1", "EXCH_ITEMSELECTED", "EXCHANGER_SELECTED",
      # Анимированная шестерня заголовка живёт по своему циклу.
      "Exchange_AniGaer"]),
]

# Классы, у которых видимость `VISIBLE` (ловят мышь сами).
INTERACTIVE = ("Button", "CheckBox", "EditableText", "EditableTextBox", "ComboBoxString",
               "Slider", "ScrollBox", "ListView")


def log(m):
    unreal.log("[видимость-панелей] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def subtree(node, acc):
    acc.append(node)
    for c in node.children:
        subtree(c, acc)
    return acc


def base_idc(name):
    """Базовый `ID` контрола по имени узла ассета: срезаем хвост плана/группы/подписи/кнопки."""
    s = name[:-4] if name.endswith("_grp") else name
    parts = s.split("_")
    # план `<idc>_<juiIdx>_<plane>`
    if len(parts) >= 3 and parts[-1].isdigit() and parts[-2].isdigit():
        return "_".join(parts[:-2]), "plane"
    if len(parts) >= 2:
        for pref in ("OPT", "ICO", "Text", "HIT", "DRAG"):
            if parts[-1].startswith(pref):
                return "_".join(parts[:-1]), "made"
    return s, "self"


def want_visibility(cls, kind):
    """Видимость для узла с собственным `Visible = 1`."""
    if kind == "plane":
        return unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE
    if cls in INTERACTIVE:
        return unreal.SlateVisibility.VISIBLE
    if cls == "TextBlock":
        # ★ Подпись не ловит мышь — иначе съедает клик по кнопке под ней.
        return unreal.SlateVisibility.HIT_TEST_INVISIBLE
    return unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE


def own_flags(path, ini, win_id, panels):
    """`ID` контрола → собственный `Visible` из `.JUI` — только для узлов перечисленных панелей окна.
    None — окна в данных нет (вынесено 07.10.2026 из `process`, общее с `step_visibility_nodes`)."""
    roots = JP.parse_jui(ini)
    win = JP.find_by_idnum(roots, win_id)
    if not win:
        log("%s: в данных нет окна %d" % (path, win_id))
        return None
    own = {}
    for pid in panels:
        p = JP.find_by_idnum([win], pid)
        if not p:
            log("%s: в данных нет панели %d" % (path, pid))
            continue
        for n in subtree(p, []):
            if n.idc:
                own[n.idc] = n.visible
    return own


def step_visibility_nodes():
    """★ 07.10.2026 (ревизия `Reports/owner_1007/label_band_fixes_REVIEW.md`, МЕЛК-1): узлы, видимость которых
    ВЕДЁТ этот шаг. Читает `asda_ui_sync_own_visibility` — их он не раскрывает и не гасит.
    Возврат — [(путь окна, узел, причина)]: узлы панелей `WINDOWS`, чей контрол в `.JUI` `Visible=0`
    (шаг держит их `COLLAPSED`). Только чтение ассетов."""
    out = []
    for w in WINDOWS:
        path, ini, win_id, panels, skip = w[0], w[1], w[2], w[3], set(w[4])
        if not EAL.does_asset_exist(path):
            continue
        own = own_flags(path, ini, win_id, panels)
        if not own:
            continue
        bp = EAL.load_asset(path)
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp) if bp else None
        if not root:
            continue
        for x in walk(root):
            nm = x.get_name()
            idc, _kind = base_idc(nm)
            if idc in skip or idc not in own or own[idc] == 1:
                continue
            out.append((path, nm, "собственный флаг JUI Visible=0 контрола %s" % idc))
    return out


def process(path, ini, win_id, panels, skip_idc, dry):
    if not EAL.does_asset_exist(path):
        log("НЕТ АССЕТА %s" % path)
        return (0, 0)
    # Собственный `Visible` по `ID` — только для узлов внутри перечисленных панелей.
    own = own_flags(path, ini, win_id, panels)
    if own is None:
        return (0, 0)

    bp = EAL.load_asset(path)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    if not root:
        log("%s: нет корневого канваса" % path)
        return (0, 0)

    fixed = seen = 0
    for w in walk(root):
        nm = w.get_name()
        idc, kind = base_idc(nm)
        if idc in skip_idc or idc not in own:
            continue
        seen += 1
        cls = w.get_class().get_name()
        cur = w.get_editor_property("visibility")
        if own[idc] != 1:
            want = unreal.SlateVisibility.COLLAPSED
            if cur == want:
                continue
        else:
            want = want_visibility(cls, kind)
            hidden = cur in (unreal.SlateVisibility.COLLAPSED, unreal.SlateVisibility.HIDDEN)
            # ★ МИНИМАЛЬНЫЙ ДИФФ: трогаем только то, что РЕАЛЬНО меняет поведение.
            #   У ЛИСТОВОГО узла `SELF_HIT_TEST_INVISIBLE` и `HIT_TEST_INVISIBLE` неразличимы (детей нет),
            #   поэтому такую пару считаем совпавшей — иначе шаг перекрашивал бы сотни узлов впустую.
            if not hidden:
                if cur == want:
                    continue
                pair = (unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE,
                        unreal.SlateVisibility.HIT_TEST_INVISIBLE)
                if cur in pair and want in pair and not isinstance(w, unreal.PanelWidget):
                    continue
                # Интерактив обязан быть VISIBLE, контейнер — не HIT_TEST_INVISIBLE: это правим.
        fixed += 1
        if not dry:
            w.set_editor_property("visibility", want)
    if fixed and not dry:
        unreal.AsdaUmgBuilder.compile_and_save(bp)
        # ⚠ `compile_and_save` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % path)
    log("%s: узлов панелей %d, поправлено %d%s" % (path, seen, fixed, " (СУХОЙ ПРОГОН)" if dry else ""))
    return (seen, fixed)


def main():
    dry = bool(os.environ.get("ASDA_PANELVIS_DRYRUN"))
    t_seen = t_fix = 0
    for path, ini, win_id, panels, skip, in_ in [(w[0], w[1], w[2], w[3], set(w[4]), None) for w in WINDOWS]:
        s, f = process(path, ini, win_id, panels, skip, dry)
        t_seen += s
        t_fix += f
    log("ИТОГО: узлов %d, поправлено %d%s" % (t_seen, t_fix, " (СУХОЙ ПРОГОН)" if dry else ""))


# ★ ЗАКОН tools-no-import-side-effects: голого вызова main() на импорте быть не должно.
if __name__ == "__main__":
    main()
