# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_option_dmgtext_row.py — ★★★ [ПОРТ] 18.10.2026 СТРОКА «ЦИФРЫ УРОНА» ОКНА НАСТРОЕК 69500 (`WBP_SYSTEM_Option_NEW`).
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
#   Явное разрешение владельца 18.10.2026: пункт «Режим показа цифр урона» (0 — только касающиеся меня, 1 — все, 2 — не
#   показывать) в окне настроек. Хранилище — поле профиля `UAsdaGameUserSettings::DamageTextMode` (синхронизация боя Ф5,
#   ТЗ `Reports/combat_sync_2026-10-17/05_TZ_PVP_FIELD10112.md` §Ф5). В оригинале пункта нет — новый [ПОРТ].
#   Разметка — в АССЕТЕ (закон `no-runtime-widget-fixes`), тем же способом, что строка «Язык» (`asda_ui_option_lang_row.py`,
#   помощники `asda_ui_option_extra_rows.py`): КЛОН существующего узла этого же окна, ничего не подбирается.
#
#   ГДЕ. Вкладка «Настройки игры» (70150), под-страница 70250 «Отображение имён» (`SYSTEM_Op_INT_2_grp`) — там же, где
#   «Показывать ОЗ монстра / моё имя и ОЗ / имена других игроков» (loc 30451…30453): цифры урона — того же рода надпись над
#   головой. Строк с подписью на странице три (Box_Check_grp_16/17/18, Y 8/33/58), строки 70268…70276 без подписи свёрнуты
#   (дефект Д3 оригинала, см. шапку `AsdaSystemOptionSubsystem.h`).
#   ★ 18.10.2026 ⚠ НАЛОЖЕНИЕ ПРИ РАСКРЫТИИ Д3. Строка I1 (L11 T86, 416×22) лежит по геометрии ПОВЕРХ свёрнутой строки
#   оригинала `Box_Check_grp_19` (L8 T83, 410×20, `Collapsed`; флажок 70268 `SYSTEM_Op_INT_2_CHK03`). Пока Д3 не
#   раскрыт, её не видно и места она не занимает. Если Д3 раскроют (строки 70268…70276 станут видимыми) — строки
#   наложатся: I1 тогда переносить ниже последней раскрытой строки (сменить PAGE_LAST_ROW и переставить корень).
#
#   ШАБЛОН. Строка «Свечение» страницы 3D (70119 + 70121, `Box_White1Stroke_Ot15_grp`): это СОБСТВЕННЫЙ приём оригинала
#   для списка на странице флажков — рамка и список слева, подпись справа, в той же колонке, что галочки соседей. Строка
#   ставится к последней строке страницы так же, как оригинал поставил её к последнему флажку 3D (`Box_Check_grp_8`):
#     dx = left(шаблон) − left(Box_Check_grp_8)  = +3,   dy = top(шаблон) − top(Box_Check_grp_8) = +28
#   ⇒ на «Отображении имён» строка встаёт на L = 8 + 3 = 11, T = 58 + 28 = 86 (числа берутся из ассета при прогоне).
#   Узлы (n = база номеров планов 9500, тег I1):
#     `SYSTEM_OX_I1_grp` (корень) · `Box_White1Stroke_Ot15_9501_0…7` (рамка) · `Bk_ComboBox_9502_0/_1` (тело/стрелка) ·
#     `SYSTEM_OX_I1_CMB` (список) · `Bk_ComboBox_9502_txt` (подпись выбранного пункта) · `SYSTEM_OX_I1_TXT` (подпись строки).
#   Порядок детей страницы: строка встаёт ПЕРЕД `WheelWin_Nomal_313_HIT` (служебная зона колеса остаётся последней).
#   Код окна (`UAsdaSystemOptionSubsystem`, строка I1 таблицы `SysOptExtra::kRows`) ставит подпись loc 990357 и пункты
#   990358…990360, значение — `DamageTextMode` профиля (по «ОК/Применить», как соседние строки).
#
# ═══ ПРОВЕРКА ═══════════════════════════════════════════════════════════════════════════════════════
#   Дамп окна ДО и ПОСЛЕ: прежние узлы совпадают байт в байт, новые — ровно ожидаемые. Повторный прогон — 0 правок.
#   Перед записью — резервная копия .uasset (`asda_headless_tool.backup_file`, набор `damage_text_option`, сверка md5).
#   ★ 18.10.2026 (ревизия шага): строка УЖЕ ЕСТЬ — шаг ТОЛЬКО СВЕРЯЕТ и ничего не меняет даже в памяти: корень —
#   ребёнок страницы и стоит перед `WheelWin_Nomal_313_HIT`, положение то же, дети корня = ровно `expected_nodes()`
#   (без недостающих и без лишних). Любое расхождение — RuntimeError (шаг падает, ассет не пишется), а не «ГОТОВО».
#   Перестановка порядка детей делается только при СОЗДАНИИ строки (там же ассет и сохраняется).
#   Сухой прогон: ASDA_OPTDMG_DRYRUN=1. Маркер: «СТРОКА ЦИФР УРОНА 69500: ГОТОВО».
#   ★ Стоит в `asda_ui_post_import.STEPS` ПОСЛЕ `asda_ui_option_lang_row` (тот же класс шагов окна 69500).
# Запуск (редактор закрыт, -game не запущен, окон не показывать):
#   python Tools\run_headless_safe.py Tools\asda_ui_option_dmgtext_row.py --marker "СТРОКА ЦИФР УРОНА 69500: ГОТОВО"
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
DRYRUN_ENV = "ASDA_OPTDMG_DRYRUN"   # ключ сухого прогона (его включает и `asda_ui_post_import --dry-run`)
BACKUP_SET = "damage_text_option"   # набор бэкапа: @@ASTORY_DEV_ROOT@@\_backup\<дата>_damage_text_option\<путь>

TAG = "I1"                                # тег строки (вкладка «Интерфейс», строка 1)
N = 9500                                  # база номеров планов строки (свободна: фаза 2 — 9110…9330, «Язык» 9160, «Эмоции» 9400)
PAGE = "SYSTEM_Op_INT_2_grp"              # под-страница 70250 «Отображение имён»
PAGE_LAST_ROW = "Box_Check_grp_18"        # последняя строка страницы с подписью (loc 30453)
PAGE_TAIL = "WheelWin_Nomal_313_HIT"      # служебная зона колеса — остаётся последним ребёнком страницы
TPL_ROOT = "Box_White1Stroke_Ot15_grp"    # строка «Свечение» 70119/70121 страницы 3D — шаблон
TPL_PREV_ROW = "Box_Check_grp_8"          # последний флажок 3D — к нему оригинал поставил шаблон
LABEL = "Цифры урона"                     # подпись в ассете (только для редактора; в игре её ставит код из loc 990357)
ROOT_NAME = "SYSTEM_OX_%s_grp" % TAG


def child_map():
    """Явная таблица «имя ребёнка шаблона → имя клона» (как `X.TPL`)."""
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
    unreal.log("[строка цифр урона 69500] %s" % m)


def _child_names(panel):
    """Имена детей панели по порядку рисования."""
    return [panel.get_child_at(i).get_name() for i in range(panel.get_children_count())]


def check_existing_row(st, page, want_l, want_t):
    """★ 18.10.2026: строка уже есть — сверить её целиком, НИЧЕГО не меняя (ни на диске, ни в памяти).

    Сверяется: (1) корень — ребёнок страницы `PAGE` и стоит раньше `PAGE_TAIL`; (2) положение корня — расчётное;
    (3) дети корня — ровно имена `expected_nodes()` без корня: нет недостающих, нет лишних. Расхождение — RuntimeError
    со списком (возможна ручная правка или недоделанный прежний прогон; сами не чиним). Возвращает число детей."""
    page_kids = _child_names(page)
    if ROOT_NAME not in page_kids:
        raise RuntimeError("строка %s уже есть, но не ребёнок %s — возможна ручная правка; не трогаю" % (ROOT_NAME, PAGE))
    if page_kids.index(ROOT_NAME) > page_kids.index(PAGE_TAIL):
        raise RuntimeError("строка %s уже есть, но стоит ПОСЛЕ %s (индексы %d > %d) — порядок нарушен, возможна ручная "
                           "правка; не трогаю" % (ROOT_NAME, PAGE_TAIL, page_kids.index(ROOT_NAME), page_kids.index(PAGE_TAIL)))
    root = st.get(ROOT_NAME)
    o = X.offsets(root)
    if abs(o.left - want_l) > 0.01 or abs(o.top - want_t) > 0.01:
        raise RuntimeError("строка %s уже есть, но стоит в (%.1f, %.1f), а не в (%.1f, %.1f) — возможна ручная правка; не трогаю"
                           % (ROOT_NAME, o.left, o.top, want_l, want_t))
    want = [n for n in expected_nodes() if n != ROOT_NAME]
    have = _child_names(root)
    missing = sorted(set(want) - set(have))
    extra = sorted(set(have) - set(want))
    if missing or extra:
        elsewhere = [n for n in missing if st.exists(n)]   # узел есть в окне, но не под корнем строки
        raise RuntimeError("строка %s уже есть, но состав детей не тот: недостаёт %s%s, лишние %s — возможна ручная правка "
                           "или недоделанный прежний прогон; не трогаю"
                           % (ROOT_NAME, missing or "нет", " (из них в другом месте окна: %s)" % elsewhere if elsewhere else "",
                              extra or "нет"))
    return len(have)


def main():
    """Добавить строку I1 (идемпотентно), сверить дампы «до/после», снять бэкап и сохранить ТОЛЬКО этот пакет."""
    dry = bool(os.environ.get(DRYRUN_ENV))
    bp = EAL.load_asset(X.ASSET)
    if not bp:
        raise RuntimeError("не загрузился %s" % X.ASSET)
    before = X.dump_map(bp)
    st = X.Step(bp, dry)
    for need in (PAGE, PAGE_LAST_ROW, PAGE_TAIL, TPL_ROOT, TPL_PREV_ROW):
        if not st.exists(need):
            raise RuntimeError("в окне нет узла %s — состав окна не тот, что ждёт шаг; не трогаю" % need)
    page = st.get(PAGE)
    tpl = st.get(TPL_ROOT)
    o_tpl, o_prev, o_last = X.offsets(tpl), X.offsets(st.get(TPL_PREV_ROW)), X.offsets(st.get(PAGE_LAST_ROW))
    # Положение — как у оригинала: шаблон относительно последнего флажка 3D ⇒ строка относительно последней строки страницы.
    want_l = o_last.left + (o_tpl.left - o_prev.left)
    want_t = o_last.top + (o_tpl.top - o_prev.top)
    dx, dy = want_l - o_tpl.left, want_t - o_tpl.top
    root = st.clone_tree(tpl, page, ROOT_NAME, child_map(), dx, dy, X.VIS_ROW)
    if root is not None:
        st.nodes["SYSTEM_OX_%s_TXT" % TAG].set_text(LABEL)
        st.nodes["Bk_ComboBox_%d_txt" % (N + 2)].set_text("")   # рыба импортёра «ABC123» — пункт ставит код
        # ★ 18.10.2026: порядок детей страницы — только при создании (ассет сохраняется ниже вместе со строкой).
        st.move_before(page, ROOT_NAME, PAGE_TAIL)
    elif st.exists(ROOT_NAME):
        # ★ 18.10.2026: строка уже есть — только сверка; перестановки в памяти без сохранения больше нет.
        n_kids = check_existing_row(st, page, want_l, want_t)
        log("строка %s уже есть: сверена — детей %d/%d, перед %s, положение верное; правок 0"
            % (ROOT_NAME, n_kids, len(expected_nodes()) - 1, PAGE_TAIL))
    log("строка %s: L%.1f T%.1f (шаблон %s L%.1f T%.1f, сдвиг %+.1f/%+.1f)" % (ROOT_NAME, want_l, want_t, TPL_ROOT, o_tpl.left, o_tpl.top, dx, dy))
    if dry or not st.new:
        log("СТРОКА ЦИФР УРОНА 69500: ГОТОВО%s — новых узлов %d" % ("  [СУХОЙ ПРОГОН]" if dry else "", len(st.new)))
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
    log("СТРОКА ЦИФР УРОНА 69500: ГОТОВО — создано узлов %d" % len(new))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        unreal.log_error("[строка цифр урона 69500] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
