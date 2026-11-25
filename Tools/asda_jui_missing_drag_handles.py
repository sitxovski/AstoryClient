# Copyright (c) 2026 @sitxovski (Discord)
# asda_jui_missing_drag_handles.py — ★★★ ВОССТАНОВЛЕНИЕ ПОТЕРЯННЫХ РУЧЕК ПЕРЕТАСКИВАНИЯ.
#
# ЗАЧЕМ. Перетаскивание окна в оригинале объявлено В ДАННЫХ: контрол-«ручка» несёт блок
#   `EVS { EVT="6"; RET="20003"; CtrlID="<IDNum цели>" } EVE`
# (`Reports/ui_window_drag_and_hittest.md`, часть A). Импортёр `asda_jui_generic_umg.py`
# честно переносит ровно то, что есть в `.JUI`: 143 записи EVS, 125 окон с ручкой, 99 без.
#
# ★ У ЧАСТИ ОКОН ЭТОТ БЛОК В ДАННЫХ ПОТЕРЯН — при том, что сам контрол-ручка на месте, называется
#   как у всех остальных (`*_move` / `*_movetext`), лежит там же (полоса заголовка над корпусом,
#   `Origin = "0 -22"`, высота 24) и в родном семействе окон соседи ручку имеют. Это тот же класс
#   дефекта данных, что уже починен импортёром у ШЕСТИ ручек со сломанным `CtrlID`
#   (`bOriginalBugFixed`, решение владельца 2026-08-06: «цель = родитель ручки»), только здесь
#   потерян весь блок целиком, и импортёру чинить нечего — он такого узла даже не видит.
#
# ЧТО ИМЕННО ВОССТАНАВЛИВАЕТСЯ (таблица `MISSING` ниже) — по одной строке на окно, каждая с
#   ДОКАЗАТЕЛЬСТВОМ, а не «на глаз». Общий признак у всех: (1) в `gamemain_jui.ini` у контрола
#   имя `*_move`/`*_movetext`, (2) rect = полоса заголовка окна, (3) `NoSelect = "0"`,
#   (4) EVS-блока нет НИ ОДНОГО во всём поддереве окна, (5) у окон-близнецов той же подсистемы
#   ручка есть.
#
# КАК РАБОТАЕТ. Скрипт НЕ пересобирает WBP (это стёрло бы пост-импортные правки) и ничего не
#   выдумывает про имена: виджет-ручку он ИЩЕТ В ЖИВОМ АССЕТЕ по префиксу `<idc>_` среди узлов,
#   чьё имя кончается на `_HIT` (невидимая хит-зона, которую импортёр ставит контролам со скином
#   без текстур) или `_DRAG` (та же зона, если импортёр уже знал про ручку). Найденное имя
#   пишется в запись окна в `DA_JuiWindowMeta`. Шаг ИДЕМПОТЕНТЕН: если ручка с таким виджетом уже
#   есть, запись не дублируется.
#
# ЗАПУСК: автоматически из `asda_ui_post_import.py` (шаг в `STEPS`), либо вручную:
#   Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_jui_missing_drag_handles.py
#
# ★★★ ДОПОЛНЕНИЕ 2026-08-26. У таблицы появился ВТОРОЙ класс записей — `added_by_port=True`:
#   ручка, которой в оригинале НЕ БЫЛО ВООБЩЕ (EVS нет ни одного во всём поддереве окна И у
#   окон-близнецов той же подсистемы его тоже нет). Это НАША ДОБАВКА по прямому решению владельца,
#   а не восстановление. В мете она помечается флагом `bAddedByPort`, в рантайме её целиком
#   выключает ключ отката `asda.UI.AddedDragHandles 0` (см. `UAsdaWindowSubsystem`).
#   Записи с `added_by_port=False` — прежний класс «потерянных» ручек (`bOriginalBugFixed`).
#
# Сухой прогон: `--dry-run` / ASDA_DRAGHANDLES_DRYRUN=1 / ASDA_POSTSTEPS_DRYRUN=1 (`asda_ui_step_dryrun`, 07.10.2026):
#   логика та же, запись DA_JuiWindowMeta подавлена; итог — «[ШАГ-ДЕЛЬТА] … прочих правок N» (N = добавленных ручек).
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)

EAL = unreal.EditorAssetLibrary

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`).
DRYRUN_ENV = "ASDA_DRAGHANDLES_DRYRUN"
META_PATH = "/Game/AsdaAssets/UI/DA_JuiWindowMeta"

# (window_id, WBP-путь, idc контрола-ручки, IDNum окна-цели, added_by_port, обоснование для лога)
MISSING = [
    (
        "Premium_Pick_select_2",
        "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_Pick_select_2",
        "Premium_Pick_select_2_move",
        68500,
        False,
        "окно выбора варианта приза 68500: контрол 68501 назван `_move`, стоит на `Origin 0 -22` "
        "размером 352x24 (полоса заголовка), NoSelect=0; EVS во всём поддереве окна НЕТ. "
        "У ВСЕХ соседей того же семейства премиум-гачи ручка в данных есть: "
        "`Premium_Pick_select`->68100, `Premium_Pick_Click_Move`->68200, "
        "`Premium_Pick_View_Move`->68300, `Premium_Pick_Click_move_RU`->79100. "
        "Замечание владельца 2026-08-18 №3 «Должно быть перемещаемым»",
    ),
    (
        "Primium_Item_post_WIN",
        "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Primium_Item_post_WIN",
        "Primium_Item_post_movetext",
        69000,
        False,
        "окно элитного почтового ящика 69000: контрол 69001 назван `_movetext`, `Origin 0 -22`, "
        "294x24, NoSelect=0; EVS во всём поддереве окна НЕТ. У бесплатного почтового ящика той же "
        "подсистемы ручки есть: `Free_post_box_textmove`->81000 и "
        "`Free_post_box_go_movetext`->82900. Отчёт `elite_mailbox_impl.md` ошибочно считал окно "
        "перетаскиваемым — в `DA_JuiWindowMeta` ручки у него не было",
    ),
    (
        "SHELLITEM_STORE2_WIN",
        "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_SHELLITEM_STORE2_WIN",
        "Shellitem_Store2_Win_Title",
        6800,
        False,
        "магазин снаряжения 6800: у контрола заголовка `Shellitem_Store2_Win_Title` "
        "(скин WinTitle_WeaponStore, Size \"126 21\", NoSelect=0) блока EVS нет, и во ВСЁМ "
        "поддереве окна (567 узлов, разбор `Data/Decoded/jui/gamemain_jui.ini`) EVS ровно НОЛЬ — "
        "окно не двигалось вовсе. ⚠ ЧЕСТНАЯ ОГОВОРКА: у окон-близнецов того же семейства "
        "витрин 668x760 (`SHELLITEM_STORE_WIN` 6000, `PackageShop_Win` 26000, "
        "`BOOSTER_SHOP_WIN` 15300, `HAIRSHOP_WIN` 15000) ручка есть только у ВЛОЖЕННЫХ "
        "поп-апов покупки (`SHELL_EQUIP_TITLE` 6301, `Package_Pur_Move` 26801, "
        "`BOOSTER_PUR_ITEM_NAME` 15538, `HAIR_PUR_TITLE` 15173), а у самих корней окон её "
        "тоже нет. Заведено решением владельца 2026-08-26 №5 «окно должно запоминать позицию»",
    ),
    (
        "SHELLITEM_STORE2_WIN",
        "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_SHELLITEM_STORE2_WIN",
        "Shellitem_Store2_Win_Base",
        6800,
        True,
        "магазин снаряжения 6800, ВТОРАЯ зона захвата: полоса заголовка `WinMd_CBar` 530x37 "
        "(x 406..936, y 2..39). Одна подпись 126x21 — слишком мелкая цель. Такой захват — "
        "ОРИГИНАЛЬНЫЙ приём: в `INVEN_WIN` 400 EVS{EVT=6,RET=0x20003,CtrlID=400} стоит именно "
        "на `WinMd_CBar` (`Inven_Base2`, 512x37). Кликабельных контролов в этой полосе нет: "
        "крестик 6801 лежит в RBar (x 1000..1018), иконка заголовка — в LBar (x 358..406), "
        "оба чекбокса 6803/6808 в `.JUI` Visible=0. Помечено как ДОБАВКА",
    ),
    (
        "HONOR_itemmall_Win",
        "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_HONOR_itemmall_Win",
        "WinMd_CBar",
        67200,
        True,
        "военный магазин 67200: ★ РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-26 №6 (дословно) «военный магазин "
        "хочу чтобы двигался». ⚠ ЭТО ДОБАВКА, А НЕ ВОССТАНОВЛЕНИЕ: EVS во всём поддереве окна "
        "НОЛЬ, контролов с непустым `ID` всего девять (HONOR_item_BTN1/BTN2/MARK_E/Radio/T1/T2/"
        "X/p1, HONOR_itemp2), заголовка-подписи нет вовсе. Зона захвата — полоса заголовка "
        "`WinMd_CBar` 530x37 (x 406..936, y 2..39), тот же приём, что у `Inven_Base2` в "
        "`INVEN_WIN`. Кликабельного в полосе нет: крестик `HONOR_item_X` 67349 лежит в RBar "
        "(x 1000..1018), оба `BigBox_Check` (67341 и безымянный) в `.JUI` Visible=0",
    ),
]


def _all_widgets(bp):
    """Имена всех виджетов дерева WBP.

    ⚠ `WidgetBlueprint.widget_tree` из Python НЕ доступен (свойство не заэкспорчено) — обход идёт
    ровно тем же приёмом, что и в `asda_ui_post_import._widget_names`: от корневого канваса
    через `AsdaUmgBuilder.get_root_canvas` и `get_all_children`.
    """
    out = set()

    def walk(w):
        try:
            kids = w.get_all_children()
        except Exception:
            return
        for c in kids:
            out.add(c.get_name())
            walk(c)

    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    if root is not None:
        out.add(root.get_name())
        walk(root)
    return out


def _find_handle_widget(names, idc):
    """Имя виджета-ручки в живом ассете.

    Порядок поиска (от самого «ручечного» к самому общему):
      1. `<idc>_<idx>_DRAG`  — зона захвата, которую импортёр ставит контролам с объявленной ручкой;
      2. `<idc>_<idx>_HIT`   — невидимая хит-зона (у контролов со скином без текстур, напр. `NoImage`);
      3. `<idc>`             — сам контрол, если импортёр создал именованный виджет;
      4. ★ `<idc>_<idx>_<план>` — ПЛАН-картинка контрола. Нужен для ручек, у которых скин
         ТЕКСТУРНЫЙ (полоса заголовка `WinMd_CBar`, подпись `WinTitle_*`): для таких контролов
         hit-зона не создаётся (`needs_hit = has_plane and not planes` в импортёре), а прямоугольник
         для захвата даёт сам план. Берём план с наименьшим номером — он покрывает весь rect
         контрола у однопланового скина, а у 9-slice это верх-лево (для полосы заголовка
         `WinMd_CBar` план ровно один).
      ⚠ Группа `<idc>_grp` НЕ подходит: её габариты — union поддерева, а не rect контрола.
    """
    pref = idc + "_"
    drag = sorted(n for n in names if n.startswith(pref) and n.endswith("_DRAG"))
    if drag:
        return drag[0]
    hit = sorted(n for n in names if n.startswith(pref) and n.endswith("_HIT"))
    if hit:
        return hit[0]
    if idc in names:
        return idc
    rx = re.compile(r"^%s_(\d+)_(\d+)$" % re.escape(idc))
    planes = []
    for n in names:
        m = rx.match(n)
        if m:
            planes.append((int(m.group(1)), int(m.group(2)), n))
    if planes:
        planes.sort()
        return planes[0][2]
    return None


def main():
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_jui_missing_drag_handles", dry) # учёт правок (ручки — «прочие правки»)
    asset = EAL.load_asset(META_PATH)
    if asset is None:
        unreal.log_error("[ручки] нет ассета %s — сначала прогони импортёр JUI" % META_PATH)
        raise RuntimeError("DA_JuiWindowMeta not found")

    windows = list(asset.get_editor_property("windows"))
    by_id = {}
    for i, m in enumerate(windows):
        by_id[str(m.get_editor_property("window_id"))] = i

    changed = 0
    for win_id, wbp_path, idc, target_idnum, added_by_port, why in MISSING:
        idx = by_id.get(win_id)
        if idx is None:
            unreal.log_error("[ручки] окна %s нет в DA_JuiWindowMeta — пропуск" % win_id)
            raise RuntimeError("window %s missing in meta" % win_id)
        bp = EAL.load_asset(wbp_path)
        if bp is None:
            unreal.log_error("[ручки] нет ассета %s" % wbp_path)
            raise RuntimeError("WBP %s not found" % wbp_path)
        names = _all_widgets(bp)
        hw = _find_handle_widget(names, idc)
        if hw is None:
            unreal.log_error("[ручки] в %s не найден виджет-ручка по префиксу %s_ "
                             "(есть ли контрол в .JUI?)" % (wbp_path, idc))
            raise RuntimeError("handle widget for %s not found" % idc)

        meta = windows[idx]
        handles = list(meta.get_editor_property("drag_handles"))
        if any(str(h.get_editor_property("handle_widget")) == hw for h in handles):
            unreal.log("[ручки] %s: ручка %s уже есть — пропуск (идемпотентность)" % (win_id, hw))
            continue

        dh = unreal.AsdaJuiDragHandle()
        dh.set_editor_property("handle_widget", unreal.Name(hw))
        dh.set_editor_property("target_id_num", int(target_idnum))
        dh.set_editor_property("target_control", unreal.Name(win_id))
        dh.set_editor_property("target_window_id", unreal.Name(win_id))
        dh.set_editor_property("target_is_window_root", True)
        dh.set_editor_property("target_widgets", unreal.Array(unreal.Name))
        # ★ Пометка «дефект оригинала починен» — ровно тем же полем, что у шести ручек со
        #   сломанным CtrlID. Так в дампе `asda.UI.DumpDrag` видно, что ручка не из .JUI.
        dh.set_editor_property("original_bug_fixed", not added_by_port)
        # ★ Ручка-ДОБАВКА: в оригинале её не было вовсе. Флаг читает `UAsdaWindowSubsystem`
        #   (ключ отката `asda.UI.AddedDragHandles 0`) и печатает `asda.UI.DumpDrag`.
        dh.set_editor_property("added_by_port", bool(added_by_port))

        arr = unreal.Array(unreal.AsdaJuiDragHandle)
        for h in handles:
            arr.append(h)
        arr.append(dh)
        meta.set_editor_property("drag_handles", arr)
        windows[idx] = meta
        changed += 1
        unreal.log("[ручки] %s: %s ручка %s -> цель %d (корень окна). Основание: %s"
                   % (win_id, "ДОБАВЛЕНА (нашей волей)" if added_by_port else "ВОССТАНОВЛЕНА",
                      hw, target_idnum, why))

    if changed:
        arr = unreal.Array(unreal.AsdaJuiWindowMeta)
        for m in windows:
            arr.append(m)
        asset.set_editor_property("windows", arr)
        SD.save(asset, dry, "asda_jui_missing_drag_handles")
    total = sum(len(m.get_editor_property("drag_handles")) for m in windows)
    unreal.log("=== ПОТЕРЯННЫЕ РУЧКИ: восстановлено %d, всего ручек в мете %d%s ==="
               % (changed, total, " (СУХОЙ)" if dry else ""))
    delta.extra = changed
    delta.finish()


if __name__ == "__main__":
    main()
