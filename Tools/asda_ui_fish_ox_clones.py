# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_fish_ox_clones.py — ★★ КЛОНЫ «ВЕРНО / НЕВЕРНО» НА 15 ЯЧЕЙКАХ МИНИ-ИГРЫ РЫБАЛКИ (окно FISH_WIN 71300).
#   Волна В2 рыбалки (08.10.2026), отчёт `Reports/owner_1008/fishing_impl_W2.md`,
#   реверс `Reports/owner_1008/fishing_parts/ui_fishwin.md` §2.1-§2.3.
#
# ═══ ЧТО ДЕЛАЕТ ЭТАЛОН ═══════════════════════════════════════════════════════════════════════════════
#   `fishingWindowInitControls@0xFC83E0` (game.c:605579-605628): шаблоны `SMALL_O` (71356, скин `o_osmall`) и
#   `SMALL_X` (71355, скин `x_xsmall`) — дети ячейки 71331 `FISH_FFimgae_m1`, Origin −15,−13, 60×60 — СНАЧАЛА
#   прячутся (`CControl_SetVisible(…, 0)`), затем для КАЖДОЙ из 15 ячеек 71331…71345 делается
#   `CControl_InstantiateChild(шаблон, 1, ячейка, 0)` — сначала O, потом X. `CControl_InstantiateChild@0x12F3AC0`
#   (game.c:989104): клон становится ребёнком ячейки, получает поле 74/75 (Origin) и размер шаблона, его видимость
#   (= скрыт, шаблон уже спрятан) и его планы. Итог: на каждой ячейке висят свои O и X поверх кадра стрелки.
#
# ═══ ЧТО СДЕЛАЛ ИМПОРТЁР ═════════════════════════════════════════════════════════════════════════════
#   `InstantiateChild` — код, а не данные `.JUI`, поэтому в `WBP_FISH_WIN` есть ТОЛЬКО шаблоны (`SMALL_X_<n>_0`,
#   `SMALL_O_<n>_0` в `FISH_FFimgae_m1_grp`), у ячеек 2…15 знаков нет вовсе (дамп волны В2: 174 узла).
#   Строить их кодом-геометрией запрещено (закон no-runtime-widget-fixes) — шаг кладёт клоны В АССЕТ.
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (идемпотентно: второй прогон = 0 правок) ═════════════════════════════════════════
#   Для i = 1…15 — два Image `FISH_SMALL_O_CLONE{ii}` и `FISH_SMALL_X_CLONE{ii}`:
#     • родитель — родитель кадра ячейки (у ячейки 1 это её группа `FISH_FFimgae_m1_grp`, у 2…15 —
#       `FISH_btn_00_grp`, где кадр `FISH_FFimgae_m{i}_<n>_0` лежит без группы);
#     • позиция = позиция кадра + (−15, −13), размер 60×60 — Origin/Size шаблона (`.JUI`);
#     • кисть — КОПИЯ кисти шаблона (`SMALL_O_*`/`SMALL_X_*`, атлас `ne_ap_roulette`), ничего не подбирается;
#     • видимость — Collapsed (клон рождается скрытым, как у эталона); показывает код окна по ответу;
#     • z-порядок — сразу ПОСЛЕ кадра ячейки и его подписи (клон — ребёнок ячейки, рисуется поверх неё), O, затем X.
#   ★ Ревизия В2 ВАЖН-3 (волна В4, 08.10.2026): узел есть и слот СОВПАДАЕТ с расчётным — «готово»; слот ОТЛИЧАЕТСЯ —
#   «осмотреть: возможна ручная правка владельца», узел НЕ трогается (закон owner-asset-edits-never-lose; прежде шаг
#   молча возвращал слот к расчёту). Пишутся ТОЛЬКО отсутствующие узлы.
#   Чужой вид узла (не Image / не тот родитель) — СТОП с ошибкой, без записи.
#   ★ Ревизия В2 ВАЖН-3: снимок ВСЕГО дерева до/после записи (`asda_ui_text_block_align._snapshot`): потеряно узлов 0,
#   изменено чужих 0, лишние — только созданные клоны, иначе исключение ДО сохранения; ручной запуск пишет ассет только
#   после бэкапа (`T._ensure_backup` → `asda_headless_tool.backup_file`, закон backup-before-edit) — как у соседних шагов STEPS.
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_ui_fish_ox_clones.py --marker "ЗАВЕРШЕНО: клоны O/X рыбалки"
#   Сухой прогон: ASDA_FISHOX_DRYRUN=1 (или общий ASDA_POSTSTEPS_DRYRUN=1 / --dry-run).
#   Соседнего `-game` быть не должно: сохранение ассета при нём молча не проходит.
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as DR   # noqa: E402  общий сухой режим шагов пост-импорта
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

EAL = unreal.EditorAssetLibrary
ASSET = "/Game/AsdaAssets/UI/Player_UI/Fishing/WBP_FISH_WIN"   # окно FISH_WIN 71300
DRYRUN_ENV = "ASDA_FISHOX_DRYRUN"                              # собственный ключ сухого прогона шага
CELLS = 15                                                      # ячейки 71331…71345
CLONE_DX = -15.0                                                # Origin шаблона по X (`.JUI` SMALL_O/SMALL_X)
CLONE_DY = -13.0                                                # Origin шаблона по Y
CLONE_W = 60.0                                                  # Size шаблона
CLONE_H = 60.0
EPS = 0.01                                                      # допуск сравнения чисел слота
TAG = "asda_ui_fish_ox_clones"                                  # метка строк сухого режима
# ★ Ревизия В4 МЕЛК-7 (09.10.2026): папка бэкапа была СВОЯ на каждый запуск (`_backup/<дата>_fish_ox_clones_<время>`): прежняя
#   общая папка волны держала копию ассета ДО волны В2 (553 501 б против нынешних 630 225 б), и проверка размера в
#   `_ensure_backup` роняла любой ручной запуск.
# ★ Т3 (11.10.2026): обход больше не нужен — `_ensure_backup` идёт через `asda_headless_tool.backup_file`, у которой копия,
#   уже лежащая в наборе, — исходник до первой правки: не перезаписывается и не роняет запуск (строка «файл уже изменён после
#   бэкапа»). Набор — `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_fish_ox_clones\<путь от @@ASTORY_DEV_ROOT@@>` (вычисляется при записи,
#   не при импорте модуля).
BACKUP_SET = "fish_ox_clones"


def log(m):
    """Строка журнала шага."""
    unreal.log("[клоны O/X рыбалки] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def canvas_slot(w):
    """Слот канваса узла (None — не в канвасе)."""
    try:
        s = w.get_editor_property("slot")
    except Exception:                                            # noqa: BLE001
        return None
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def offsets(w):
    """(L, T, R, B) оффсетов слота (импортёр кладёт POINT-анкер: R/B = ширина/высота)."""
    s = canvas_slot(w)
    if s is None:
        return None
    m = s.get_editor_property("layout_data").get_editor_property("offsets")
    return (float(m.left), float(m.top), float(m.right), float(m.bottom))


def set_offsets(w, want):
    """Поставить оффсеты слота."""
    s = canvas_slot(w)
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(want[0], want[1], want[2], want[3]))
    s.set_editor_property("layout_data", lay)


def same(a, b):
    """Совпадают ли две четвёрки чисел."""
    return all(abs(x - y) < EPS for x, y in zip(a, b))


def find_plane(nodes, prefix):
    """План импортёра `<prefix><juiIdx>_0` (индекс меняется при переимпорте — ищем по префиксу и форме хвоста)."""
    best = None
    for n, w in nodes.items():
        if not n.startswith(prefix) or not n.endswith("_0") or not isinstance(w, unreal.Image):
            continue
        mid = n[len(prefix):-2]
        if not mid.isdigit():
            continue
        if best is None or len(n) < len(best[0]):
            best = (n, w)
    return best


def child_index(parent, w):
    """Индекс ребёнка в родителе (−1 — нет)."""
    for i in range(parent.get_children_count()):
        if parent.get_child_at(i) == w:
            return i
    return -1


def main(standalone=False):
    """Шаг пост-импорта. `standalone` — ручной запуск: бэкап ассета обязателен (ревизия В2 ВАЖН-3)."""
    import asda_ui_text_block_align as T   # noqa: E402  общие снимок дерева и бэкап (соседние шаги STEPS)
    dry = DR.is_dry(DRYRUN_ENV)
    B = unreal.AsdaUmgBuilder
    if not EAL.does_asset_exist(ASSET):
        raise RuntimeError("нет ассета %s" % ASSET)
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    if not root:
        raise RuntimeError("нет корневого канваса %s" % ASSET)
    nodes = {}
    for w in walk(root):
        nodes.setdefault(w.get_name(), w)

    # Шаблоны (дети ячейки 1): их кисть копируется клонам.
    tmpl = {}
    for kind in ("O", "X"):
        hit = find_plane(nodes, "SMALL_%s_" % kind)
        if hit is None:
            raise RuntimeError("нет шаблона SMALL_%s_<n>_0 в %s" % (kind, ASSET))
        tmpl[kind] = hit[1]
        log("шаблон %s = %s, слот %s" % (kind, hit[0], offsets(hit[1])))

    added = 0
    inspect = []                                                 # узлы с отличающимся слотом — «осмотреть», не трогаются
    # Решение по каждому клону — ДО записи (сухой проход), чтобы снимок «до» и бэкап предшествовали первой правке.
    todo = []                                                    # (i, kind, parent, want) — отсутствующие клоны
    for i in range(1, CELLS + 1):
        cell = find_plane(nodes, "FISH_FFimgae_m%d_" % i)
        if cell is None:
            raise RuntimeError("нет кадра ячейки FISH_FFimgae_m%d_<n>_0" % i)
        cell_w = cell[1]
        parent = cell_w.get_parent()
        cell_off = offsets(cell_w)
        if parent is None or cell_off is None:
            raise RuntimeError("у кадра %s нет родителя-канваса" % cell[0])
        want = (cell_off[0] + CLONE_DX, cell_off[1] + CLONE_DY, CLONE_W, CLONE_H)
        # Клон рисуется поверх ячейки и её подписи (`FISH_FFimgae_m{i}` — TextBlock того же родителя).
        anchor = child_index(parent, cell_w)
        cap = nodes.get("FISH_FFimgae_m%d" % i)
        if cap is not None and cap.get_parent() == parent:
            anchor = max(anchor, child_index(parent, cap))
        for kind in ("O", "X"):
            name = "FISH_SMALL_%s_CLONE%02d" % (kind, i)
            w = nodes.get(name)
            if w is not None:
                if not isinstance(w, unreal.Image) or w.get_parent() != parent:
                    raise RuntimeError("узел %s чужого вида (класс %s, родитель %s) — СТОП"
                                       % (name, w.get_class().get_name(),
                                          w.get_parent().get_name() if w.get_parent() else "-"))
                cur = offsets(w)
                if cur is None or not same(cur, want):
                    inspect.append("%s: слот %s ≠ расчёт %s — возможна ручная правка владельца, НЕ трогаю" % (name, cur, want))
                continue
            todo.append((i, kind, name, want))

    for m in inspect:
        log("осмотреть: %s" % m)
    if todo and not dry:
        if standalone:
            T._ensure_backup(ASSET, BACKUP_SET)
        before = T._snapshot(root)
        made = set()
        for i, kind, name, want in todo:
            cell_w = find_plane(nodes, "FISH_FFimgae_m%d_" % i)[1]
            parent = cell_w.get_parent()
            # Клон рисуется поверх ячейки, её подписи и уже стоящих клонов этой ячейки (O, затем X).
            anchor = child_index(parent, cell_w)
            for extra in ("FISH_FFimgae_m%d" % i, "FISH_SMALL_O_CLONE%02d" % i, "FISH_SMALL_X_CLONE%02d" % i):
                e = nodes.get(extra)
                if e is not None and e.get_parent() == parent:
                    anchor = max(anchor, child_index(parent, e))
            # AddImage(BP, Parent, Name, Pos, Size, Texture, UV, Tint, bBox, Margin) — кисть сразу заменяется копией шаблона.
            w = B.add_image(bp, parent, name, unreal.Vector2D(want[0], want[1]), unreal.Vector2D(want[2], want[3]),
                            None, unreal.Box2D(), unreal.LinearColor(1.0, 1.0, 1.0, 1.0), False, unreal.Margin())
            if not w:
                raise RuntimeError("AddImage не создал %s" % name)
            w.set_editor_property("brush", tmpl[kind].get_editor_property("brush"))
            w.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
            set_offsets(w, want)
            B.move_child_to_index(parent, w, anchor + 1)
            nodes[name] = w
            made.add(name)
            log("%s: создан в %s, слот %s" % (name, parent.get_name(), want))
        after = T._snapshot(B.get_root_canvas(bp))
        lost = [n for n in before if n not in after]
        extra = [n for n in after if n not in before and n not in made]
        foreign = [n for n in before if n in after and before[n] != after[n]]
        log("узлов до %d, после %d; создано %d; потеряно узлов: %d; лишних: %d; изменено чужих: %d"
            % (len(before), len(after), len(made), len(lost), len(extra), len(foreign)))
        if lost or extra or foreign:
            raise RuntimeError("сверка не прошла: потеряно %s, лишних %s, изменено чужих %s — НЕ сохраняю" % (lost, extra, foreign))
    added = len(todo)
    if added:
        if not dry:
            B.compile_and_save(bp)   # ⚠ только компилирует и метит пакет — пишет `save`
        if not DR.save(bp, dry, TAG, False):
            raise RuntimeError("не сохранён %s" % ASSET)
        if not dry:
            unreal.log("ASDA_WRITES_ASSET: %s" % ASSET)
    unreal.log("%s %s: создано %d, осмотреть %d%s" % (DR.MARK, TAG, added, len(inspect), " (СУХОЙ ПРОГОН)" if dry else ""))
    log("ЗАВЕРШЕНО: клоны O/X рыбалки (создано %d, осмотреть %d)" % (added, len(inspect)))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    try:
        main(standalone=True)
    except Exception:                                            # noqa: BLE001
        unreal.log_error("[клоны O/X рыбалки] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
