# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_option_lang_row.py — ★★★ [ПОРТ] 09.10.2026 СТРОКА «ЯЗЫК» ОКНА НАСТРОЕК 69500 (`WBP_SYSTEM_Option_NEW`).
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
#   Решение владельца 09.10.2026 (мультиязычность): смена языка — в меню настроек игры, применение после перезапуска с
#   сообщением игроку. Разметка — в АССЕТЕ (закон `no-runtime-widget-fixes`), тем же способом, что строки фазы 2
#   (`asda_ui_option_extra_rows.py`, его помощники используются отсюда): на странице «Экран» под примечанием
#   «Графический режим сменится после перезапуска игры.» (S6, Y 180) добавляются
#     • S7 — список «Язык» (Y 205, база номеров планов 9160) — клон строки «Разрешение» `NoImage_grp_12`;
#     • S8 — примечание «Язык сменится после перезапуска игры.» (Y 230) — клон подписи, как S6.
#   Порядок детей группы «Экран»: новые узлы встают ПЕРЕД S6 — у канваса порядок детей = порядок рисования, и раскрытые
#   вниз списки строк выше (S5 «Графический режим») должны ложиться поверх новых строк.
#   Код окна (`UAsdaSystemOptionSubsystem`, строки S7/S8 таблицы `SysOptExtra::kRows`) ставит подписи из loc 990354/990355
#   и пункты списка (loc 990351/990352).
#
# ═══ ПРОВЕРКА ═══════════════════════════════════════════════════════════════════════════════════════
#   Дамп окна ДО и ПОСЛЕ: прежние узлы совпадают байт в байт, новые — ровно ожидаемые. Повторный прогон — 0 правок.
#   Сухой прогон: ASDA_OPTLANG_DRYRUN=1. Маркер: «СТРОКА ЯЗЫКА 69500: ГОТОВО».
#   ★ Стоит в `asda_ui_post_import.STEPS` ПОСЛЕ `asda_ui_option_extra_rows` (клонирует его узлы-соседи, ставится перед S6).
# Запуск (редактор закрыт, -game не запущен, окон не показывать):
#   python Tools\run_headless_safe.py Tools\asda_ui_option_lang_row.py --marker "СТРОКА ЯЗЫКА 69500: ГОТОВО"
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_option_extra_rows as X   # общие помощники и шаблоны строк окна 69500 (без побочных эффектов при импорте)

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
DRYRUN_ENV = "ASDA_OPTLANG_DRYRUN"   # ключ сухого прогона (его включает и `asda_ui_post_import --dry-run`)

# (тег, вид, группа-родитель, Y строки, база номеров планов, подпись в ассете) — формат строк `asda_ui_option_extra_rows`.
ROWS = [   # порядок рисования: снизу вверх
    ("S8", "note",  X.SCREEN, 230, 0,    "Язык сменится после перезапуска игры."),
    ("S7", "combo", X.SCREEN, 205, 9160, "Язык"),
]


def log(m):
    """Строка журнала шага."""
    unreal.log("[строка языка 69500] %s" % m)


def main():
    """Добавить строки S7/S8 (идемпотентно) и сохранить окно после сверки дампов."""
    dry = bool(os.environ.get(DRYRUN_ENV))
    bp = EAL.load_asset(X.ASSET)
    if not bp:
        raise RuntimeError("не загрузился %s" % X.ASSET)
    before = X.dump_map(bp)
    st = X.Step(bp, dry)
    if not st.exists("SYSTEM_OX_S6_TXT"):
        raise RuntimeError("нет примечания S6 — сначала шаг asda_ui_option_extra_rows")
    scr = st.get(X.SCREEN)
    for row in ROWS:
        st.add_row(row, scr)
        st.move_before(scr, "SYSTEM_OX_%s_%s" % (row[0], "TXT" if row[1] == "note" else "grp"), "SYSTEM_OX_S6_TXT")
    if dry or not st.new:
        log("СТРОКА ЯЗЫКА 69500: ГОТОВО%s — новых узлов %d" % ("  [СУХОЙ ПРОГОН]" if dry else "", len(st.new)))
        return
    after = X.dump_map(bp)
    bad = ["пропал %s" % n for n in before if n not in after]
    bad += ["изменён %s" % n for n in before if n in after and before[n] != after[n]]
    new = sorted(set(after) - set(before))
    if new != sorted(st.new):
        bad.append("новые узлы не совпали: %s / %s" % (new, sorted(st.new)))
    if bad:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: %s" % "; ".join(bad))
    for n in new:
        log("  + %s" % after[n])
    B.compile_and_save(bp)
    unreal.log("ASDA_WRITES_ASSET: %s" % X.ASSET)
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("не сохранён %s" % X.ASSET)
    log("СТРОКА ЯЗЫКА 69500: ГОТОВО — создано узлов %d" % len(new))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[строка языка 69500] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
