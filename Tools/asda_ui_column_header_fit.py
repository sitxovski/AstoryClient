# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_column_header_fit.py — ★★ ЗАГОЛОВКИ СТОЛБЦОВ СПИСКОВ: РУССКИЙ ТЕКСТ ШИРЕ КОЛОНКИ (07.10.2026, отчёт
#   `Reports/owner_1007/audit_fixes2.md`, п. 1). [ИСПРАВЛЕНИЕ ДЕФЕКТА РАЗМЕТКИ ПОД RU] — правка АССЕТОВ, кода не
#   касается (закон no-runtime-widget-fixes). Тот же приём, что Д-1/Д-2 (`asda_ui_icon_caption_fit`).
#
#   ЗАМЕР путём игрока (`-game`, alpen, `asda.UI.OpenByPath` + `asda.UI.SetText` строкой кода + `asda.UI.DumpTextMetrics`,
#   лог `Reports/owner_1007/fix2_logs/hdr_measure_before.log`): 46 заголовков столбцов в 7 окнах со списками,
#   шире колонки 3:
#     • 8200 `Find_SMate_List_D6` — loc 110809 «Сравнение» 56 px (кегль 8) / 47 px (кегль 7) в колонке 36 px; справа
#       его закрывает фон соседней колонки D7 → на экране «Сравнени». Кегль 7 не входит ⇒ кегль 7 + поле расширено
#       ВЛЕВО в пустую часть заголовка колонки D5 «Вступление» (362 px, текст D5 занимает 60 px в центре): правый край
#       поля — у правой границы колонки D6 (−1 px), ширина под строку кегля 7. Разметка оригинала (строка заголовков)
#       не покидается, чужой текст не перекрывается; колонку D7 текст больше не задевает.
#       ★ 07.10.2026 ЗАМЕНЕНО: окно 8200 расширено целиком (шаг `asda_ui_widen_windows`), случай из таблицы снят.
#     • COMMUNITY_WIN `CmtyClan_Sub3_Txt02` и `CmtyClan_Sub5_T2` — loc 17163 «Уровень» 43 px в поле 40 ⇒ кегль 7 (36 px).
#   Кегль 7: верх строки ниже на CAPTION_TOP_SHIFT_7 (строка 13 → 10 px), центр строки на месте — как в Д-1.
#   Кегль и поле взяты ЗАМЕРОМ и записаны в таблицу — шаг не измеряет (в коммандлете нет шрифтового сервиса Slate).
#
#   БЕЗОПАСНОСТЬ (закон owner-asset-edits-never-lose): узел трогается, только если он в ТОЧНОСТИ в виде импортёра
#   (слот = «было» из таблицы, кегль 8, центр, интервал 0, точечные анкоры). Уже исправленный — «готово»
#   (идемпотентно). Иной вид — «осмотреть», узел НЕ трогается, прогон останавливается. Снимок дерева до/после: у прочих
#   узлов без изменений, у правленого изменились только слот и кегль. Ручной запуск пишет только при бэкапе того же
#   размера (закон backup-before-edit).
#
# Запуск (редактор и игра ЗАКРЫТЫ; коммандлет):
#   python Tools\run_headless_safe.py Tools\asda_ui_column_header_fit.py --commandlet-rhi --marker "ЗАГОЛОВКИ-СТОЛБЦОВ: ГОТОВО"
#   ASDA_COLHDR_DRYRUN=1 — сухой прогон.
import os
import sys
import traceback

try:
    import unreal
except ImportError:
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

# ★ Т3 (11.10.2026): набор бэкапа до записи — `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_column_header_fit\<путь от @@ASTORY_DEV_ROOT@@>`
#   (`T._ensure_backup` → `asda_headless_tool.backup_file`; была прошитая папка `2026-10-07_audit_fixes2`)
BACKUP_SET = "column_header_fit"
FONT_PX_IMPORTER = 8         # кегль импортёра (`FONT_PX` в asda_jui_generic_umg.py)
CAPTION_TOP_SHIFT_7 = 1.5    # кегль 7: строка 13 → 10 px, верх вниз на половину разницы — центр строки на месте
EPS = 0.05                   # допуск сравнения чисел слота

# (ассет, имя узла, слот импортёра (L, T, W, H), слот после правки (L, T, W, H), кегль после правки)
#   Слоты — в канвасе группы узла (анкоры точечные, offsets = L, T, W, H). «Было» сверено с дампом путём игрока 07.10.
#   Список ЗАКРЫТЫЙ: новые случаи добавлять только по замеру (отчёт audit_fixes2.md, п. 1).
HEADER_FITS = [
    # ★ 07.10.2026 (решение владельца — расширять окно целиком, а не кегль 7): случай 8200 «Сравнение» (было: кегль 7,
    #   поле 36 → 48 px влево) СНЯТ отсюда — окно 8200 расширено шагом `asda_ui_widen_windows` (столбец D6 64 px,
    #   кегль 8). Тот шаг принимает D6 и в виде импортёра, и в прежнем виде этого шага.
    # ★ 07.10.2026: клан «Уровень» (Sub3_Txt02 / Sub5_T2) тоже СНЯТ — шаг `asda_ui_widen_windows` сдвигает границу
    #   столбцов «Имя | Уровень» на 6 px (столбец 40 → 46), кегль 8. Таблица пуста; шаг оставлен в STEPS как пустой.
]


def log(m):
    """Строка журнала шага (в редакторе — в лог UE, вне — в stdout)."""
    if unreal:
        unreal.log("[заголовки-столбцов] %s" % m)
    else:
        print("[заголовки-столбцов] %s" % m)


def _tb():
    """Общие помощники шага `asda_ui_text_block_align` (снимок, слоты, бэкап) — одна реализация на все шаги."""
    import asda_ui_text_block_align as T
    return T


def _near(a, b):
    """Покомпонентное равенство чисел с допуском EPS."""
    return len(a) == len(b) and all(abs(float(x) - float(y)) <= EPS for x, y in zip(a, b))


def _handle(case, nodes, dry):
    """Один заголовок: ('fixed'|'ok'|'inspect', текст, имя узла). Пишет в виджет только при dry=False."""
    T = _tb()
    _asset, name, old, want, font_want = case
    tw = nodes.get(name)
    if not isinstance(tw, unreal.TextBlock):
        return "inspect", "%s: нет TextBlock" % name, name
    ts = T._slot_sig(tw)
    if ts is None or ts[3]:
        return "inspect", "%s: слот не канвасный или авторазмер" % name, name
    anch = ts[1]
    if not (_near(anch[0:2], anch[2:4]) and _near(ts[2], (0.0, 0.0))):
        return "inspect", "%s: анкоры %s / alignment %s — не точечные, не вид импортёра" % (name, anch, ts[2]), name
    font = tw.get_editor_property("font")
    size = float(font.get_editor_property("size"))
    ls = int(font.get_editor_property("letter_spacing"))
    just = str(tw.get_editor_property("justification")).split(".")[-1].split(":")[0].strip().upper()
    if just != "CENTER" or ls != 0:
        return "inspect", "%s: выравнивание %s / интервал %d — не вид импортёра" % (name, just, ls), name
    if _near(ts[0], want) and abs(size - font_want) <= EPS:
        return "ok", "%s: уже исправлено (слот %s, кегль %d)" % (name, want, font_want), name
    if not _near(ts[0], old) or abs(size - FONT_PX_IMPORTER) > EPS:
        return "inspect", "%s: слот %s / кегль %.1f не совпадают с видом импортёра %s / %d — НЕ трогаю" \
            % (name, ts[0], size, old, FONT_PX_IMPORTER), name
    if not dry:
        T._set_slot(tw, want, (0.0, 0.0), False)
        font.set_editor_property("size", font_want)
        tw.set_editor_property("font", font)
    return "fixed", "%s: слот %s → %s, кегль %d → %d" % (name, ts[0], want, FONT_PX_IMPORTER, font_want), name


def main(standalone=False):
    """Применить таблицу HEADER_FITS. `standalone` — ручной запуск: бэкап обязателен."""
    T = _tb()
    dry = bool(os.environ.get("ASDA_COLHDR_DRYRUN"))
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    from asda_login_guard import login_package_blocked
    by_asset = {}
    for c in HEADER_FITS:
        by_asset.setdefault(c[0], []).append(c)
    fixed_total = ok_total = 0
    inspect = []
    for asset, lst in by_asset.items():
        if login_package_blocked(asset, "asda_ui_column_header_fit.py"):
            log("%s: окно сцены логина — пропуск (защита логина)" % asset)
            continue
        bp = EAL.load_asset(asset)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            raise RuntimeError("нет корневого канваса у %s" % asset)
        nodes = {w.get_name(): w for w in T._walk(root)}
        verdicts = [_handle(c, nodes, True) for c in lst]
        for v, msg, _n in verdicts:
            log("%s [%s] %s" % (asset.rsplit("/", 1)[-1], {"fixed": "править", "ok": "готово", "inspect": "осмотреть"}[v], msg))
        todo = [c for c, (v, _m, _n) in zip(lst, verdicts) if v == "fixed"]
        ok_total += sum(1 for v, _m, _n in verdicts if v == "ok")
        inspect += ["%s: %s" % (asset, m) for v, m, _n in verdicts if v == "inspect"]
        if dry or not todo:
            continue
        if standalone:
            T._ensure_backup(asset, BACKUP_SET)
        before = T._snapshot(root)
        names = set()
        for c in todo:
            _v, _msg, name = _handle(c, nodes, False)
            names.add(name)
        root2 = B.get_root_canvas(bp)
        after = T._snapshot(root2)
        lost = [n for n in before if n not in after]
        extra = [n for n in after if n not in before]
        foreign = [n for n in before if n in after and before[n] != after[n] and n not in names]

        def keep(sig):
            """Неизменяемая часть текста: всё, кроме кегля (индекс 6)."""
            return sig[:6] + sig[7:] if sig else sig
        broken = [n for n in names if before[n][:2] != after[n][:2] or before[n][3:6] != after[n][3:6]
                  or keep(before[n][6]) != keep(after[n][6])
                  or before[n][2][1:] != after[n][2][1:]]   # анкоры/alignment/авторазмер/z — без изменений
        log("%s: узлов до %d, после %d; правлено %d; потеряно узлов: %d; лишних: %d; изменено чужих: %d; "
            "испорчено у правленых: %d" % (asset.rsplit("/", 1)[-1], len(before), len(after), len(names),
                                          len(lost), len(extra), len(foreign), len(broken)))
        if lost or extra or foreign or broken:
            raise RuntimeError("сверка не прошла: потеряно %s, лишних %s, чужих %s, испорчено %s — НЕ сохраняю"
                               % (lost, extra, foreign, broken))
        for n in sorted(names):
            log("   %s: слот %s → %s; текст %s → %s" % (n, before[n][2], after[n][2], before[n][6], after[n][6]))
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % asset)
        log("ASDA_WRITES_ASSET: %s" % asset)
        fixed_total += len(names)
    for s in inspect:
        log("осмотреть: %s" % s)
    log("случаев %d: исправлено %d, уже исправлено %d, осмотреть %d%s"
        % (len(HEADER_FITS), fixed_total, ok_total, len(inspect), " (СУХОЙ ПРОГОН)" if dry else ""))
    if inspect and not dry:
        raise RuntimeError("заголовки-столбцов: %d случаев «осмотреть» — узлы не в виде импортёра, см. лог" % len(inspect))
    log("ЗАГОЛОВКИ-СТОЛБЦОВ: ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта) — ничего.
if __name__ == "__main__":
    if unreal is None:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        for c in HEADER_FITS:
            print("%s %s: %s → %s, кегль %d" % (c[0].rsplit("/", 1)[-1], c[1], c[2], c[3], c[4]))
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[заголовки-столбцов] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
