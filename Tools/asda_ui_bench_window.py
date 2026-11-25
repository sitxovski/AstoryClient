# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_bench_window.py — ★★★ [ПОРТ] 12.10.2026 ОКНО ЗАМЕРА ГРАФИКИ (автоподбор настроек замером кадра): НОВЫЙ АССЕТ
#   `/Game/AsdaAssets/UI/CommonWin/WBP_BENCH_WIN`.
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════════════════
#   Решение владельца 12.10.2026 (вариант В2, `Reports/owner_1012/autodetect_options.md`): при первом запуске (и по
#   кнопке «Автоподбор» окна настроек) — модальное окно, где виден ход замера: эталон, загрузка сцены, проходы с
#   числами, итог; применение только по «ОК», «Пропустить» — оставить прежнее. В оригинале такого окна нет.
#   Разметка — АССЕТ (закон no-runtime-widget-fixes); код (`UAsdaBenchSubsystem`) ставит подписи из loc 990969…990982
#   и обработчики, разметку не создаёт.
#   Основа — окно первого выбора языка `WBP_LANG_SELECT` в разметке V3 (рамка и бурая полоса заголовка окна настроек,
#   три кнопки 180×28 на скине ne_skillbar1): ДУБЛИКАТ ассета (исходник не меняется), затем:
#     • рамка 480×330 по центру базы 1024×768 (L272 T219), полоса заголовка 37 с отступом 2, значок 35×35;
#     • заголовок `MESSAGE2_TXT00` — на полосе одной строкой по центру;
#     • `BENCH_LOG` — клон текста заголовка: многострочный журнал хода (перенос, выравнивание влево), под полосой;
#     • `BENCH_STATUS` — клон текста заголовка: строка состояния над кнопками (влево);
#     • кнопки `MESSAGE2_BUT00` («ОК») и `MESSAGE2_BUT01` («Пропустить») 180×28 в ряд у нижней кромки;
#     • третья кнопка `MESSAGE2_BUT02_BASE_grp` свёрнута (узлы остаются — код её не ищет).
#   Имена узлов, которые ищет код: `MESSAGE2_TXT00`, `BENCH_LOG`, `BENCH_STATUS`, `MESSAGE2_BUT00/01` (+ `_Text`).
#
# ═══ ПРОВЕРКА ═══════════════════════════════════════════════════════════════════════════════════════════════════
#   Идемпотентно: ассет есть и в нём `BENCH_LOG` — раскладка пересчитывается в памяти и сравнивается с ассетом по
#   дампу: совпало — 0 правок, ничего не сохраняется; расходится — ручная правка, СТОП без записи. Ассет есть без
#   `BENCH_LOG` (чужой) — СТОП. После создания — сверка дампов «до/после»: изменились ровно ожидаемые узлы, новые —
#   ровно ожидаемые; иначе исключение без сохранения. Перед записью — резервная копия .uasset.
#   Сухой прогон: ASDA_BENCHWIN_DRYRUN=1 (ничего не пишет). Маркер: «ОКНО ЗАМЕРА: ГОТОВО».
# Запуск (редактор закрыт, -game не запущен, окон не показывать; под замком UE):
#   python Tools\run_headless_safe.py Tools\asda_ui_bench_window.py --marker "ОКНО ЗАМЕРА: ГОТОВО"
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_option_extra_rows as X        # общие помощники клонирования узлов (без побочных эффектов при импорте)
import asda_ui_lang_select_window as LW      # frame9 / button / text_box / clone_from / index_after (без побочных эффектов)

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

SRC = "/Game/AsdaAssets/UI/CommonWin/WBP_LANG_SELECT"     # образец: окно выбора языка V3 (только чтение)
DST = "/Game/AsdaAssets/UI/CommonWin/WBP_BENCH_WIN"
DRYRUN_ENV = "ASDA_BENCHWIN_DRYRUN"
BACKUP_SET = "benchwin"
CX, CY = 512.0, 384.0        # центр базы 1024×768
W, H = 480.0, 330.0          # окно
BTN_W, BTN_H = 180.0, 28.0   # кнопки (скин ne_skillbar1, как в окне языка)
BTN_GAP = 20.0
PAD = 16.0
LABELS = {"MESSAGE2_TXT00": "Проверка производительности", "MESSAGE2_BUT00_Text": "ОК", "MESSAGE2_BUT01_Text": "Пропустить",
          "BENCH_LOG": "…", "BENCH_STATUS": ""}
NEW_NODES = ["BENCH_LOG", "BENCH_STATUS"]


def log(m):
    unreal.log("[окно замера] %s" % m)


def layout(st):
    """Раскладка окна (идемпотентна — одни и те же числа при каждом вызове)."""
    L, T = CX - W / 2.0, CY - H / 2.0
    LW.frame9(st, L, T, W, H, 20.0, 20.0, 20.0, 20.0)
    bl, bt, bw = L + 2.0, T + 2.0, W - 4.0
    X.set_offsets(st.get("LANG_BandL"), bl, bt, 48.0, 37.0)
    X.set_offsets(st.get("LANG_BandC"), bl + 48.0, bt, bw - 48.0 - 86.0, 37.0)
    X.set_offsets(st.get("LANG_BandR"), bl + bw - 86.0, bt, 86.0, 37.0)
    X.set_offsets(st.get("LANG_BandIcon"), bl + 1.0, bt + 1.0, 35.0, 35.0)
    t = st.get("MESSAGE2_TXT00")
    t.set_editor_property("auto_wrap_text", False)
    LW.text_box(st, bl + 38.0, bt + 12.0, bw - 48.0, 18.0)
    # Кнопки — ряд у нижней кромки.
    by = T + H - PAD - BTN_H
    x0 = L + (W - 2 * BTN_W - BTN_GAP) / 2.0
    LW.button(st, 0, x0, by, BTN_W)
    LW.button(st, 1, x0 + BTN_W + BTN_GAP, by, BTN_W)
    st.get("MESSAGE2_BUT02_BASE_grp").set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
    # Строка состояния над кнопками, журнал — между полосой и строкой состояния.
    sy = by - 10.0 - 18.0
    s = st.get("BENCH_STATUS")
    X.set_offsets(s, L + PAD, sy, W - 2 * PAD, 18.0)
    s.set_editor_property("justification", unreal.TextJustify.LEFT)
    s.set_editor_property("auto_wrap_text", False)
    ly = bt + 37.0 + 12.0
    g = st.get("BENCH_LOG")
    X.set_offsets(g, L + PAD, ly, W - 2 * PAD, sy - 8.0 - ly)
    g.set_editor_property("justification", unreal.TextJustify.LEFT)
    g.set_editor_property("auto_wrap_text", True)


def touched(st):
    """Узлы, которые вправе измениться при создании (кроме новых)."""
    out = {"MESSAGE2_TXT00", "LANG_BandL", "LANG_BandC", "LANG_BandR", "LANG_BandIcon", "MESSAGE2_BUT02_BASE_grp"}
    out |= {"MESSAGE2_WIN_0_%d" % k for k in range(9)}
    for n in range(2):
        gr = st.get("MESSAGE2_BUT%02d_BASE_grp" % n)
        out.add(gr.get_name())
        out |= {gr.get_child_at(i).get_name() for i in range(gr.get_children_count())}
    out |= set(LABELS)
    return out


def create(bp):
    """На свежем дубликате: два текстовых узла (клоны заголовка), раскладка, подписи. Возврат — число новых узлов."""
    before = X.dump_map(bp)
    st = X.Step(bp, False)
    i = LW.index_after(bp, "LANG_BandIcon")
    src = st.get("MESSAGE2_TXT00")
    for n, name in enumerate(NEW_NODES):
        LW.clone_from(st, bp, src, name, i + n)
    layout(st)
    for nm, txt in LABELS.items():
        st.get(nm).set_text(txt)
    after = X.dump_map(bp)
    allowed = touched(st)
    new = sorted(set(after) - set(before))
    bad = [n for n in before if n not in after] + [n for n in before if before[n] != after.get(n) and n not in allowed]
    if new != sorted(NEW_NODES):
        bad.append("новые узлы: %s" % new)
    if bad:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: %s" % bad)
    for n in sorted(allowed | set(new)):
        log("  %s" % after[n])
    return len(new)


def save(bp):
    disk = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()),
                        "AsdaAssets", "UI", "CommonWin", "WBP_BENCH_WIN.uasset")
    if os.path.isfile(disk):
        import asda_headless_tool as HT
        log("резервная копия: %s" % HT.backup_file(disk, BACKUP_SET))
    B.compile_and_save(bp)
    unreal.log("ASDA_WRITES_ASSET: %s" % DST)
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("не сохранён %s" % DST)


def main():
    dry = bool(os.environ.get(DRYRUN_ENV))
    if EAL.does_asset_exist(DST):
        bp = EAL.load_asset(DST)
        names = X.walk(B.get_root_canvas(bp))
        if "BENCH_LOG" not in names:
            raise RuntimeError("%s есть, но без BENCH_LOG — не наш ассет/ручная правка, СТОП" % DST)
        before = X.dump_map(bp)
        layout(X.Step(bp, False))
        after = X.dump_map(bp)   # один дамп (ревизия МЕЛК-15), а не по дампу на узел
        diff = [n for n in before if before[n] != after.get(n)]
        if diff:
            raise RuntimeError("разметка в ассете отличается от генератора (ручная правка?) — СТОП, не пишу: %s" % diff)
        log("ассет уже в нужной разметке — 0 правок")
        log("ОКНО ЗАМЕРА: ГОТОВО (0 правок)")
        return
    if not EAL.does_asset_exist(SRC):
        raise RuntimeError("нет образца %s — сначала шаг asda_ui_lang_select_window" % SRC)
    if dry:
        log("[сухой] будет создан %s из %s (+%d узла текста)" % (DST, SRC, len(NEW_NODES)))
        log("ОКНО ЗАМЕРА: ГОТОВО [СУХОЙ ПРОГОН]")
        return
    if not EAL.duplicate_asset(SRC, DST):
        raise RuntimeError("не продублирован %s → %s" % (SRC, DST))
    bp = EAL.load_asset(DST)
    n = create(bp)
    save(bp)
    log("ОКНО ЗАМЕРА: ГОТОВО — создан %s, новых узлов %d" % (DST, n))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[окно замера] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
