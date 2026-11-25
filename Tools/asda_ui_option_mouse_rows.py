# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_option_mouse_rows.py — ★★★ [ПОРТ] 19.10.2026 СТРОКИ «ЧУВСТВИТЕЛЬНОСТЬ МЫШИ» И «ЧУВСТВИТЕЛЬНОСТЬ ПОВОРОТА КАМЕРЫ»
#   ОКНА НАСТРОЕК 69500 (`WBP_SYSTEM_Option_NEW`).
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
#   Запрос владельца 19.10.2026: «в окно настроек добавить чувствительность мыши (ползунок, как в игре, штатно) + отдельно
#   чувствительность поворота камеры»; уточнение: мышь = скорость КУРСОРА, камера = поворот при зажатой ПКМ. В оригинале
#   пунктов нет — новый [ПОРТ]. Хранилище — поля профиля `UAsdaGameUserSettings::CursorSpeedPercent` / `CameraTurnPercent`.
#   Разметка — в АССЕТЕ (закон `no-runtime-widget-fixes`), как у строк «Язык» и «Цифры урона»: КЛОНЫ существующих узлов
#   этого же окна (помощники `asda_ui_option_extra_rows.py`), ничего не подбирается.
#
#   ГДЕ. Вкладка «Настройки игры» (70150), под-страница 70350 «Разное» (`SYSTEM_Op_INT_3_grp`). Строк с подписью у неё
#   десять (`Box_Check_grp_24…33`, Y 8…235; две последние раскрывает код по циклу эталона k<10), одиннадцатая
#   `Box_Check_grp_34` (Y 261, флажок 70382 без обработчика — дефект Д4) свёрнута навсегда. Новые строки идут тем же шагом
#   страницы (26 px) под последней строкой с подписью: I2 на месте свёрнутой 34-й, I3 — следом.
#   ⚠ Если Д4 когда-нибудь раскроют (`Box_Check_grp_34` станет видимой) — она ляжет под I2: строки тогда сдвинуть ниже.
#
#   ШАБЛОН. Полоса «Яркость» страницы «Экран» (`NoImage_grp_11`: подпись 70057 + четыре плана `SideBar_Scr_242_0…3` скина
#   полосы 70058) — та же полоса, что громкость и дальность прорисовки. У страниц «Настроек игры» контрол стоит СЛЕВА,
#   подпись — справа (флажки `Box_Check_grp_*`, строка «Цифры урона»), поэтому клон раскладывается так же:
#     • планы полосы сдвигаются на −223 (левый край левой стрелки = 0); ★ v2 (19.10.2026, владелец «в два раза»): жёлоб 110 → 220,
#       правая стрелка +110 — полоса занимает 0…246 (v1 из alpha-1.0.9: 0…136 — шаг перекладывает её в v2);
#     • подпись и её зона попадания — на X 252, ширина 230 (строка до 490, внутри страницы 0…498; v1: X 142, ширина 268);
#     • корень — L как у строк страницы (8), высота 19 шаблона.
#   Узлы (тег / база номеров): I2 «Чувствительность мыши» — 9600, I3 «Чувствительность поворота камеры» — 9610:
#     `SYSTEM_OX_<тег>_grp` (корень) · `SYSTEM_OX_<тег>_TXT` (подпись) · `NoImage_<n>_HIT` · `SideBar_Scr_<n+2>_0…3` (полоса).
#   Порядок детей страницы: строки встают ПЕРЕД `WheelWin_Nomal_337_HIT` (служебная зона колеса остаётся последней).
#   Код окна (`UAsdaSystemOptionSubsystem`, строки I2/I3 `SysOptExtra::kRows`) ставит подписи loc 990361/990362, значения —
#   поля профиля по «ОК/Применить».
#
# ═══ ПРОВЕРКА ═══════════════════════════════════════════════════════════════════════════════════════
#   Дамп окна ДО и ПОСЛЕ: прежние узлы совпадают байт в байт, новые — ровно ожидаемые. Повторный прогон — 0 правок.
#   Строка уже есть — шаг ТОЛЬКО СВЕРЯЕТ (родитель, порядок, положение, состав детей) и ничего не меняет; расхождение —
#   RuntimeError (ассет не пишется). Перед записью — резервная копия .uasset (`backup_file`, набор `mouse_rows`, md5).
#   Сухой прогон: ASDA_OPTMOUSE_DRYRUN=1. Маркер: «СТРОКИ МЫШИ 69500: ГОТОВО».
#   ★ Стоит в `asda_ui_post_import.STEPS` ПОСЛЕ `asda_ui_option_dmgtext_row` (тот же класс шагов окна 69500).
# Запуск (редактор закрыт, -game не запущен, окон не показывать):
#   python Tools\run_headless_safe.py Tools\asda_ui_option_mouse_rows.py --marker "СТРОКИ МЫШИ 69500: ГОТОВО"
#          --expect-asset /Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW
import os
import re
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_option_extra_rows as X   # общие помощники строк окна 69500 (без побочных эффектов при импорте)
import asda_headless_tool as HT         # единая резервная копия .uasset (`backup_file`)

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
DRYRUN_ENV = "ASDA_OPTMOUSE_DRYRUN"   # ключ сухого прогона (его включает и `asda_ui_post_import --dry-run`)
BACKUP_SET = "mouse_rows_v2"          # набор бэкапа: @@ASTORY_DEV_ROOT@@\_backup\<дата>_mouse_rows_v2\<путь> (★ v2: копия ассета ДО перекладки)

PAGE = "SYSTEM_Op_INT_3_grp"              # под-страница 70350 «Разное»
PAGE_LAST_ROW = "Box_Check_grp_33"        # последняя строка страницы с подписью (loc 30480)
PAGE_PREV_ROW = "Box_Check_grp_32"        # предпоследняя — по ней берётся шаг строк страницы (26 px)
PAGE_TAIL = "WheelWin_Nomal_337_HIT"      # служебная зона колеса — остаётся последним ребёнком страницы
TPL_ROOT = "NoImage_grp_11"               # строка «Яркость» страницы «Экран» — шаблон полосы
BAR_DX = -223.0                           # сдвиг планов полосы: левая стрелка шаблона (L223) → 0
# ★ 19.10.2026 (v2, владелец: «расширить ползунки в два раза»): жёлоб 110 → 220 px (+110), правая стрелка сдвигается на те же
#   +110, полоса занимает 0…246; подпись — правее неё, до 490 (внутри страницы 0…498). Раскладка v1 (жёлоб 110, подпись L142
#   W268) — выложенная в alpha-1.0.9; найденную v1 шаг ПЕРЕКЛАДЫВАЕТ в v2 (меняются только смещения своих 14 узлов).
TRACK_GROW = 110.0                        # прибавка к ширине жёлоба (план 0) и к X правой стрелки (план 2)
LABEL_L = 252.0                           # подпись и её зона попадания — правее полосы (полоса 0…246)
LABEL_W = 230.0                           # ширина подписи: до 482 строки (8 + 482 = 490 внутри страницы)
V1 = {"grow": 0.0, "label_l": 142.0, "label_w": 268.0}   # раскладка v1 (alpha-1.0.9) — только для узнавания и перекладки
V2 = {"grow": TRACK_GROW, "label_l": LABEL_L, "label_w": LABEL_W}
# (тег, база номеров планов, подпись в ассете — только для редактора; в игре подпись ставит код из loc)
ROWS = [
    ("I2", 9600, "Чувствительность мыши"),
    ("I3", 9610, "Чувствительность поворота камеры"),
]


def child_map(tag, n):
    """Явная таблица «имя ребёнка шаблона → имя клона» (как `X.TPL["slider"]`)."""
    m = {"SYSTEM_Op_SYSTEM_SYS22_TXT01": "SYSTEM_OX_%s_TXT" % tag, "NoImage_241_HIT": "NoImage_%d_HIT" % n}
    m.update({"SideBar_Scr_242_%d" % i: "SideBar_Scr_%d_%d" % (n + 2, i) for i in range(4)})
    return m


def root_name(tag):
    """Имя корня строки."""
    return "SYSTEM_OX_%s_grp" % tag


def expected_nodes():
    """Имена всех узлов обеих строк (для сторожей `asda_ui_post_import.HANDMADE` и отчёта)."""
    out = []
    for tag, n, _ in ROWS:
        out += [root_name(tag)] + sorted(child_map(tag, n).values())
    return out


def log(m):
    """Строка журнала шага."""
    unreal.log("[строки мыши 69500] %s" % m)


def _child_names(panel):
    """Имена детей панели по порядку рисования."""
    return [panel.get_child_at(i).get_name() for i in range(panel.get_children_count())]


def want_child_offsets(tpl, cmap, lay=None):
    """Ожидаемые смещения детей клона: (имя клона → (L, T, W, H)) по раскладке «полоса слева, подпись справа».
    lay — раскладка (`V1`/`V2`, по умолчанию действующая V2): прибавка жёлоба и место подписи."""
    lay = lay or V2
    out = {}
    for i in range(tpl.get_children_count()):
        k = tpl.get_child_at(i)
        o = X.offsets(k)
        nm = k.get_name()
        name = cmap[nm]
        if nm.startswith("SideBar_Scr_"):
            plane = int(nm.rsplit("_", 1)[1])   # 0 жёлоб, 1 «−», 2 «+», 3 бегунок
            l, w = o.left + BAR_DX, o.right
            if plane == 0:
                w += lay["grow"]
            elif plane == 2:
                l += lay["grow"]
            out[name] = (l, o.top, w, o.bottom)
        else:
            out[name] = (lay["label_l"], o.top, lay["label_w"], o.bottom)
    return out


def _offsets_match(st, want_kids, root, root_w):
    """Совпадают ли смещения детей строки и ширина корня с раскладкой."""
    if abs(X.offsets(root).right - root_w) > 0.01:
        return False
    for k, (l, t, w, h) in want_kids.items():
        ok = X.offsets(st.get(k))
        if max(abs(ok.left - l), abs(ok.top - t), abs(ok.right - w), abs(ok.bottom - h)) > 0.01:
            return False
    return True


def check_existing_row(st, page, tag, n, want_l, want_t, want_kids):
    """Строка уже есть — сверить её целиком, НИЧЕГО не меняя. Расхождение — RuntimeError (возможна ручная правка)."""
    name = root_name(tag)
    page_kids = _child_names(page)
    if name not in page_kids:
        raise RuntimeError("строка %s уже есть, но не ребёнок %s — возможна ручная правка; не трогаю" % (name, PAGE))
    if page_kids.index(name) > page_kids.index(PAGE_TAIL):
        raise RuntimeError("строка %s стоит ПОСЛЕ %s — порядок нарушен, возможна ручная правка; не трогаю" % (name, PAGE_TAIL))
    o = X.offsets(st.get(name))
    if abs(o.left - want_l) > 0.01 or abs(o.top - want_t) > 0.01:
        raise RuntimeError("строка %s стоит в (%.1f, %.1f), а не в (%.1f, %.1f) — возможна ручная правка; не трогаю"
                           % (name, o.left, o.top, want_l, want_t))
    have = _child_names(st.get(name))
    want = sorted(child_map(tag, n).values())
    if sorted(have) != want:
        raise RuntimeError("строка %s: состав детей не тот (есть %s, ждали %s) — не трогаю" % (name, sorted(have), want))
    # Смещения детей сверяет `main`: раскладка v2 — правок 0, v1 — перекладка в v2, иная — RuntimeError.


def main():
    """Добавить строки I2/I3 (идемпотентно), сверить дампы «до/после», снять бэкап и сохранить ТОЛЬКО этот пакет."""
    dry = bool(os.environ.get(DRYRUN_ENV))
    bp = EAL.load_asset(X.ASSET)
    if not bp:
        raise RuntimeError("не загрузился %s" % X.ASSET)
    before = X.dump_map(bp)
    st = X.Step(bp, dry)
    for need in (PAGE, PAGE_LAST_ROW, PAGE_PREV_ROW, PAGE_TAIL, TPL_ROOT):
        if not st.exists(need):
            raise RuntimeError("в окне нет узла %s — состав окна не тот, что ждёт шаг; не трогаю" % need)
    page = st.get(PAGE)
    tpl = st.get(TPL_ROOT)
    o_tpl, o_last, o_prev = X.offsets(tpl), X.offsets(st.get(PAGE_LAST_ROW)), X.offsets(st.get(PAGE_PREV_ROW))
    step_y = o_last.top - o_prev.top   # шаг строк страницы (26 px)
    created = 0
    relaid = []   # ★ v2: узлы, переложенные из раскладки v1 (меняется только off(...))
    for i, (tag, n, label) in enumerate(ROWS):
        cmap = child_map(tag, n)
        want_l = o_last.left
        want_t = o_last.top + step_y * (i + 1)
        kids = want_child_offsets(tpl, cmap)
        root = st.clone_tree(tpl, page, root_name(tag), cmap, want_l - o_tpl.left, want_t - o_tpl.top, X.VIS_ROW)
        if root is not None:
            created += 1
            X.set_offsets(root, want_l, want_t, LABEL_L + LABEL_W, o_tpl.bottom)
            for k, (l, t, w, h) in kids.items():
                X.set_offsets(st.nodes[k], l, t, w, h)
            st.nodes["SYSTEM_OX_%s_TXT" % tag].set_text(label)
            st.move_before(page, root_name(tag), PAGE_TAIL)
            log("строка %s «%s»: L%.1f T%.1f (шаг страницы %.1f)" % (root_name(tag), label, want_l, want_t, step_y))
        elif st.exists(root_name(tag)):
            check_existing_row(st, page, tag, n, want_l, want_t, kids)
            root = st.get(root_name(tag))
            if _offsets_match(st, kids, root, LABEL_L + LABEL_W):
                log("строка %s уже есть: сверена — положение, порядок, состав и раскладка v2 верные; правок 0" % root_name(tag))
            elif _offsets_match(st, want_child_offsets(tpl, cmap, V1), root, V1["label_l"] + V1["label_w"]):
                # ★ v2: строка в раскладке v1 (alpha-1.0.9) — расширить полосу и сдвинуть подпись, только смещения своих узлов.
                log("%sстрока %s: раскладка v1 → v2 (жёлоб +%.0f, подпись L%.0f W%.0f)"
                    % ("[сухой] " if dry else "", root_name(tag), TRACK_GROW, LABEL_L, LABEL_W))
                relaid += [root_name(tag)] + list(kids)
                if not dry:
                    o = X.offsets(root)
                    X.set_offsets(root, o.left, o.top, LABEL_L + LABEL_W, o.bottom)
                    for k, (l, t, w, h) in kids.items():
                        X.set_offsets(st.get(k), l, t, w, h)
            else:
                raise RuntimeError("строка %s: смещения детей не совпадают ни с раскладкой v2, ни с v1 — возможна ручная правка; "
                                   "не трогаю" % root_name(tag))
    if dry or (not st.new and not relaid):
        log("СТРОКИ МЫШИ 69500: ГОТОВО%s — новых узлов %d, переложено v1→v2 %d" % ("  [СУХОЙ ПРОГОН]" if dry else "", len(st.new), len(relaid)))
        return
    after = X.dump_map(bp)
    off_rx = re.compile(r"off\([^)]*\)")
    bad = ["пропал %s" % n for n in before if n not in after]
    for n in before:
        if n not in after or before[n] == after[n]:
            continue
        if n in relaid and off_rx.sub("", before[n]) == off_rx.sub("", after[n]):
            continue   # ★ v2: перекладка — изменилось только смещение своего узла
        bad.append("изменён %s" % n)
    new = sorted(set(after) - set(before))
    if new != sorted(st.new) or (st.new and new != sorted(expected_nodes())):
        bad.append("новые узлы не совпали: %s / %s" % (new, sorted(st.new)))
    if bad:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: %s" % "; ".join(bad))
    for n in new:
        log("  + %s" % after[n])
    for n in relaid:
        log("  ~ %s" % after[n])
    asset_file = os.path.join(X.CONTENT_DIR, X.ASSET[len("/Game/"):].replace("/", os.sep) + ".uasset")
    bak = HT.backup_file(asset_file, BACKUP_SET, verify="md5", log=log)
    if bak is None:
        raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % asset_file)
    log("резервная копия: %s" % bak)
    # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset` ровно этот пакет.
    B.compile_and_save(bp)
    unreal.log("ASDA_WRITES_ASSET: %s" % X.ASSET)
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("не сохранён %s" % X.ASSET)
    log("СТРОКИ МЫШИ 69500: ГОТОВО — строк %d, создано узлов %d, переложено v1→v2 %d" % (created, len(new), len(relaid)))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[строки мыши 69500] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
