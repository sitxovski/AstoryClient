# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_option_crouch_row.py — ★★★ [ПОРТ] 25.10.2026 СТРОКА «ПРИСЕД» (НАЖАТИЕ / ЗАЖАТИЕ) ОКНА НАСТРОЕК 69500 (`WBP_SYSTEM_Option_NEW`).
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
#   Запрос владельца 25.10.2026: «режим ходьбы в присяде — сделай переключаемым в настройках (Нажатие, зажатие)»; поручением
#   разрешена ТОЧЕЧНАЯ правка ассета окна настроек. Хранилище — поле профиля `UAsdaGameUserSettings::CrouchMode` (0 «Нажатие» —
#   одиночное нажатие Ctrl переключает присед, 1 «Зажатие» — присед, пока Ctrl удерживается). У оригинала и у донора
#   AsdaFableClassic клавиши приседа нет — пункт новый [ПОРТ]. Разметка — в АССЕТЕ (закон `no-runtime-widget-fixes`), тем же
#   способом, что строка «Цифры урона» (`asda_ui_option_dmgtext_row.py`, помощники `asda_ui_option_extra_rows.py`): КЛОН
#   существующего узла этого же окна, ничего не подбирается.
#
#   ГДЕ. Вкладка «Настройки игры» (70150), под-страница 70350 «Разное» (`SYSTEM_Op_INT_3_grp`) — там же, где управление мышью и
#   камерой (строки I2/I3, `asda_ui_option_mouse_rows.py`). Строка I4 встаёт под I3 тем же шагом страницы (26 px:
#   `Box_Check_grp_33` − `Box_Check_grp_32`). Подходящего свободного контрола в окне нет: единственная свёрнутая строка «Разного»
#   `Box_Check_grp_34` (флажок 70382 без обработчика, дефект Д4) — флажок, а не выбор из двух пунктов, и место её занято I2.
#   Высоты страницы хватает (I4 — T313…335, страница 0…404, раскрытый список из двух пунктов — внутри) — окно не расширяется.
#
#   ШАБЛОН. Строка «Свечение» страницы 3D (70119 + 70121, `Box_White1Stroke_Ot15_grp`) — собственный приём оригинала для списка
#   на странице флажков: рамка и список слева, подпись справа. Её же клонирует строка «Цифры урона». Сдвиг по X — как у
#   оригинала к последнему флажку 3D: dx = left(шаблон) − left(`Box_Check_grp_8`) = +3 ⇒ L = left(`Box_Check_grp_33`) + 3 = 11.
#   По Y — шаг страницы под I3: T = top(`SYSTEM_OX_I3_grp`) + 26 = 313 (числа берутся из ассета при прогоне).
#   Узлы (n = база номеров планов 9620, тег I4; 9620…9629 свободны: мышь/камера — 9600…9612):
#     `SYSTEM_OX_I4_grp` (корень) · `Box_White1Stroke_Ot15_9621_0…7` (рамка) · `Bk_ComboBox_9622_0/_1` (тело/стрелка) ·
#     `SYSTEM_OX_I4_CMB` (список) · `Bk_ComboBox_9622_txt` (подпись выбранного пункта) · `SYSTEM_OX_I4_TXT` (подпись строки).
#   Порядок детей страницы: строка встаёт ПЕРЕД `WheelWin_Nomal_337_HIT` (служебная зона колеса остаётся последней) — после I2/I3,
#   поэтому раскрытый список рисуется поверх соседей.
#   Код окна (`UAsdaSystemOptionSubsystem`, строка I4 таблицы `SysOptExtra::kRows`) ставит подпись loc 990364 и пункты
#   990365/990366, значение — `CrouchMode` профиля (по «ОК/Применить», как соседние строки).
#
# ═══ ПРОВЕРКА ═══════════════════════════════════════════════════════════════════════════════════════
#   Дамп окна ДО и ПОСЛЕ: прежние узлы совпадают байт в байт, новые — ровно ожидаемые. Повторный прогон — 0 правок.
#   Строка уже есть — шаг ТОЛЬКО СВЕРЯЕТ (родитель, порядок, положение, состав детей) и ничего не меняет даже в памяти;
#   расхождение — RuntimeError (возможна ручная правка; ассет не пишется). Перед записью — резервная копия .uasset
#   (`asda_headless_tool.backup_file`, набор `crouch_option`, сверка md5).
#   Сухой прогон: ASDA_OPTCROUCH_DRYRUN=1. Маркер: «СТРОКА ПРИСЕДА 69500: ГОТОВО».
#   ★ Стоит в `asda_ui_post_import.STEPS` ПОСЛЕ `asda_ui_option_mouse_rows` (ставится под его I3).
# Запуск (редактор закрыт, -game не запущен, окон не показывать):
#   python Tools\run_headless_safe.py Tools\asda_ui_option_crouch_row.py --marker "СТРОКА ПРИСЕДА 69500: ГОТОВО"
#          --expect-asset /Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW
import os
import sys
import traceback

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_option_extra_rows as X   # общие помощники и шаблоны строк окна 69500 (без побочных эффектов при импорте)
import asda_headless_tool as HT         # единая резервная копия .uasset (`backup_file`)

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
DRYRUN_ENV = "ASDA_OPTCROUCH_DRYRUN"   # ключ сухого прогона (его включает и `asda_ui_post_import --dry-run`)
BACKUP_SET = "crouch_option"           # набор бэкапа: @@ASTORY_DEV_ROOT@@\_backup\<дата>_crouch_option\<путь>

TAG = "I4"                                # тег строки (вкладка «Настройки игры», строка 4)
N = 9620                                  # база номеров планов строки
PAGE = "SYSTEM_Op_INT_3_grp"              # под-страница 70350 «Разное»
PAGE_ABOVE = "SYSTEM_OX_I3_grp"           # строка над новой («Чувствительность поворота камеры»)
PAGE_LAST_ROW = "Box_Check_grp_33"        # последняя строка-флажок страницы (по ней — X и шаг)
PAGE_PREV_ROW = "Box_Check_grp_32"        # предпоследняя — шаг строк страницы (26 px)
PAGE_TAIL = "WheelWin_Nomal_337_HIT"      # служебная зона колеса — остаётся последним ребёнком страницы
TPL_ROOT = "Box_White1Stroke_Ot15_grp"    # строка «Свечение» 70119/70121 страницы 3D — шаблон
TPL_PREV_ROW = "Box_Check_grp_8"          # последний флажок 3D — к нему оригинал поставил шаблон (сдвиг по X)
LABEL = "Присед"                          # подпись в ассете (только для редактора; в игре её ставит код из loc 990364)
ROOT_NAME = "SYSTEM_OX_%s_grp" % TAG


def child_map():
    """Явная таблица «имя ребёнка шаблона → имя клона» (как у строки «Цифры урона»)."""
    m = {"Box_White1Stroke_Ot15_267_%d" % i: "Box_White1Stroke_Ot15_%d_%d" % (N + 1, i) for i in range(8)}
    m.update({
        "Bk_ComboBox_268_0": "Bk_ComboBox_%d_0" % (N + 2),
        "Bk_ComboBox_268_1": "Bk_ComboBox_%d_1" % (N + 2),
        "SYSTEM_O3_SYSTEM_SYS33_CMB00": "SYSTEM_OX_%s_CMB" % TAG,
        "Bk_ComboBox_268_txt": "Bk_ComboBox_%d_txt" % (N + 2),
        "SYSTEM_O3_SYSTEM_SYS33_TXT09": "SYSTEM_OX_%s_TXT" % TAG,
    })
    return m


def expected_nodes():
    """Имена всех узлов строки (для сторожей `asda_ui_post_import.HANDMADE` и отчёта)."""
    return [ROOT_NAME] + sorted(child_map().values())


def log(m):
    """Строка журнала шага."""
    unreal.log("[строка приседа 69500] %s" % m)


def _child_names(panel):
    """Имена детей панели по порядку рисования."""
    return [panel.get_child_at(i).get_name() for i in range(panel.get_children_count())]


def check_existing_row(st, page, want_l, want_t):
    """Строка уже есть — сверить её целиком, НИЧЕГО не меняя (ни на диске, ни в памяти).

    Сверяется: (1) корень — ребёнок страницы `PAGE`, стоит после `PAGE_ABOVE` и раньше `PAGE_TAIL`; (2) положение корня —
    расчётное; (3) дети корня — ровно имена `expected_nodes()` без корня. Расхождение — RuntimeError со списком (возможна
    ручная правка или недоделанный прежний прогон; сами не чиним). Возвращает число детей."""
    page_kids = _child_names(page)
    if ROOT_NAME not in page_kids:
        raise RuntimeError("строка %s уже есть, но не ребёнок %s — возможна ручная правка; не трогаю" % (ROOT_NAME, PAGE))
    i_row, i_tail, i_above = page_kids.index(ROOT_NAME), page_kids.index(PAGE_TAIL), page_kids.index(PAGE_ABOVE)
    if not (i_above < i_row < i_tail):
        raise RuntimeError("строка %s уже есть, но порядок детей не тот (%s %d, строка %d, %s %d) — возможна ручная правка; не трогаю"
                           % (ROOT_NAME, PAGE_ABOVE, i_above, i_row, PAGE_TAIL, i_tail))
    o = X.offsets(st.get(ROOT_NAME))
    if abs(o.left - want_l) > 0.01 or abs(o.top - want_t) > 0.01:
        raise RuntimeError("строка %s уже есть, но стоит в (%.1f, %.1f), а не в (%.1f, %.1f) — возможна ручная правка; не трогаю"
                           % (ROOT_NAME, o.left, o.top, want_l, want_t))
    want = [n for n in expected_nodes() if n != ROOT_NAME]
    have = _child_names(st.get(ROOT_NAME))
    missing = sorted(set(want) - set(have))
    extra = sorted(set(have) - set(want))
    if missing or extra:
        raise RuntimeError("строка %s уже есть, но состав детей не тот: недостаёт %s, лишние %s — возможна ручная правка "
                           "или недоделанный прежний прогон; не трогаю" % (ROOT_NAME, missing or "нет", extra or "нет"))
    return len(have)


def main():
    """Добавить строку I4 (идемпотентно), сверить дампы «до/после», снять бэкап и сохранить ТОЛЬКО этот пакет."""
    dry = bool(os.environ.get(DRYRUN_ENV))
    bp = EAL.load_asset(X.ASSET)
    if not bp:
        raise RuntimeError("не загрузился %s" % X.ASSET)
    before = X.dump_map(bp)
    st = X.Step(bp, dry)
    for need in (PAGE, PAGE_ABOVE, PAGE_LAST_ROW, PAGE_PREV_ROW, PAGE_TAIL, TPL_ROOT, TPL_PREV_ROW):
        if not st.exists(need):
            raise RuntimeError("в окне нет узла %s — состав окна не тот, что ждёт шаг (сначала asda_ui_option_mouse_rows); не трогаю" % need)
    for n in child_map().values():
        if st.exists(n) and not st.exists(ROOT_NAME):
            raise RuntimeError("узел %s уже есть в окне, а строки %s нет — имена заняты; не трогаю" % (n, ROOT_NAME))
    page = st.get(PAGE)
    tpl = st.get(TPL_ROOT)
    o_tpl, o_prev3d = X.offsets(tpl), X.offsets(st.get(TPL_PREV_ROW))
    o_last, o_prev, o_above = X.offsets(st.get(PAGE_LAST_ROW)), X.offsets(st.get(PAGE_PREV_ROW)), X.offsets(st.get(PAGE_ABOVE))
    step_y = o_last.top - o_prev.top                     # шаг строк страницы (26 px)
    want_l = o_last.left + (o_tpl.left - o_prev3d.left)  # как у оригинала: шаблон к последнему флажку 3D
    want_t = o_above.top + step_y                        # под строкой I3 тем же шагом
    dx, dy = want_l - o_tpl.left, want_t - o_tpl.top
    root = st.clone_tree(tpl, page, ROOT_NAME, child_map(), dx, dy, X.VIS_ROW)
    if root is not None:
        st.nodes["SYSTEM_OX_%s_TXT" % TAG].set_text(LABEL)
        st.nodes["Bk_ComboBox_%d_txt" % (N + 2)].set_text("")   # рыба импортёра «ABC123» — пункт ставит код
        st.move_before(page, ROOT_NAME, PAGE_TAIL)
    elif st.exists(ROOT_NAME):
        n_kids = check_existing_row(st, page, want_l, want_t)
        log("строка %s уже есть: сверена — детей %d/%d, между %s и %s, положение верное; правок 0"
            % (ROOT_NAME, n_kids, len(expected_nodes()) - 1, PAGE_ABOVE, PAGE_TAIL))
    log("строка %s: L%.1f T%.1f (шаблон %s L%.1f T%.1f, сдвиг %+.1f/%+.1f, шаг страницы %.1f, над ней %s T%.1f)"
        % (ROOT_NAME, want_l, want_t, TPL_ROOT, o_tpl.left, o_tpl.top, dx, dy, step_y, PAGE_ABOVE, o_above.top))
    if dry or not st.new:
        log("СТРОКА ПРИСЕДА 69500: ГОТОВО%s — новых узлов %d" % ("  [СУХОЙ ПРОГОН]" if dry else "", len(st.new)))
        return
    after = X.dump_map(bp)
    bad = ["пропал %s" % n for n in before if n not in after]
    bad += ["изменён %s" % n for n in before if n in after and before[n] != after[n]]
    new = sorted(set(after) - set(before))
    if new != sorted(st.new) or new != sorted(expected_nodes()):
        bad.append("новые узлы не совпали: %s / %s" % (new, sorted(st.new)))
    if bad:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: %s" % "; ".join(bad))
    for n in new:
        log("  + %s" % after[n])
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
    log("СТРОКА ПРИСЕДА 69500: ГОТОВО — создано узлов %d" % len(new))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[строка приседа 69500] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
