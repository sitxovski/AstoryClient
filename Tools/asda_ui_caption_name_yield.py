# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_caption_name_yield.py — ★★★ ПУСТОЙ СЛОТ ПОДПИСИ ОТДАЁТ ИМЯ НАСТОЯЩЕМУ КОНТРОЛУ (25.09.2026).
#   Отчёт: `Reports/gear_upgrade/optwin_fix.md`. Общее правило — `asda_jui_parse.caption_slot_yields`
#   (им же с 25.09 пользуется импортёр `asda_jui_generic_umg.py`, так что после перегенерации шаг найдёт 0 правок).
#
# ═══ ДЕФЕКТ ═════════════════════════════════════════════════════════════════════════════════════
#   Окно 50300 `ItemOptionValue_WIN`, первая строка списка панели 0. Контрол `ItemOptionValue_2_b1`
#   (скин `Box_WhiteOt20`, `IDNum = -1`, `CaptionText = ""`, `CaptionInfo = "0 0 ffe9d6bc 5"`) получил от
#   импортёра ПУСТОЙ узел подписи `ItemOptionValue_2_b1_Text` (правило «настроенный слот», 31.08). Но это
#   же имя — `ID` его ребёнка, Text-контрола 50343 (строка 1 списка). Родитель обходится первым, поэтому
#   живая строка уехала в `ItemOptionValue_2_b1_Text_1` и осталась с текстом ассета «Макс. HP +1», а
#   подсистема писала значение в пустышку, которая лежит ПОД чёрным планом строки (`…_Text_92_0`).
#   Путь игрока (`-game`, `asda.UI.TreeGeom`): пустышка (470,208.5)-(762,227.5) рисуется ДО плана
#   (471,207)-(761,224), живая `_Text_1` (479,209.5) — поверх. Лог `optwin_game_before.log`.
#   У панели 1 (`ItemOptionValue_3_b1`, `CaptionInfo … 0`) слота нет — там всё верно.
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (точечно, идемпотентно) ═════════════════════════════════════════════════════
#   Для каждого случая правила (по 4 ini сейчас ровно один): в ассете окна
#     • узел `{idc}_Text` — пустышка (TextBlock с ПУСТЫМ текстом, в группе контрола) → удаляется;
#     • узел `{idc}_Text_1` (TextBlock той же группы) → переименовывается в `{idc}_Text`.
#   Больше ничего не меняется: сверка снимка до/после (класс, родитель, слот, видимость, непрозрачность,
#   кисть, текст, цвет) обязана показать «потеряно узлов: 0, изменено узлов: 0» — иначе исключение ДО
#   сохранения. Узел, который не похож на заготовку импортёра (у пустышки есть текст, другой класс, другая
#   группа), НЕ трогается — строка «осмотреть» (возможна ручная правка владельца).
#   Удаление/переименование — штатные операции UMG-редактора (`UAsdaUmgBuilder::RemoveWidgetByName` /
#   `RenameWidgetByName`), GUID-таблица ассета остаётся согласованной.
#
# Запуск (редактор ЗАКРЫТ):
#   python Tools\run_headless_safe.py Tools\asda_ui_caption_name_yield.py --marker "ПОДПИСЬ-ИМЯ: ГОТОВО"
#   ASDA_CAPYIELD_DRYRUN=1 — сухой прогон (ничего не пишет). Вне редактора: `python asda_ui_caption_name_yield.py --plan`.
#   Ручной запуск пишет ассет только после бэкапа `asda_headless_tool.backup_file` (закон backup-before-edit; ★ Т3 11.10.2026).
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
import asda_jui_parse as J  # noqa: E402
import asda_headless_tool as HT  # noqa: E402  ★ Т3 (11.10.2026): единая резервная копия `backup_file`

# Бэкап ДО записи (★ Т3, 11.10.2026): набор `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_caption_name_yield\<путь от @@ASTORY_DEV_ROOT@@>`
#   (была прошитая папка `2026-09-25_optwin`).
BACKUP_SET = "caption_name_yield"
AREA_ROOT = r"@@ASTORY_DEV_ROOT@@"
CONTENT_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"
INI_DIR = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui"
CLASSES = ("gamemain", "commonwin", "temp", "login")   # порядок = приоритет имени, как у импортёра


def log(m):
    """Строка журнала шага (в редакторе — в лог UE, вне — в stdout)."""
    if unreal:
        unreal.log("[подпись-имя] %s" % m)
    else:
        print("[подпись-имя] %s" % m)


def ini_of(cls):
    """Путь .ini класса — как у импортёра (приоритет локализованного `*_loc.ini`)."""
    p = os.path.join(INI_DIR, "%s_loc.ini" % cls)
    return p if os.path.isfile(p) else os.path.join(INI_DIR, "%s_jui.ini" % cls)


def plan():
    """Случаи правила по всем классам: список (класс, окно, имя окна для ассета, имя подписи)."""
    skins = J.parse_skins_full()
    stype = lambda n: (skins.get((n.name or "").lower()) or {}).get("type", "")   # noqa: E731
    owners = {}
    roots = {}
    for cls in CLASSES:
        roots[cls] = J.parse_jui(ini_of(cls))
        for r in roots[cls]:
            owners.setdefault(J._san(r.idc or r.name), []).append(cls)
    out = []
    for cls in CLASSES:
        for r in roots[cls]:
            J.compute_anchors(r)
            base = J._san(r.idc or r.name)
            # Суффикс неглавной сборки окна — как `_wsuffix` импортёра (главная = первая по приоритету).
            sfx = "" if len(owners[base]) < 2 or owners[base][0] == cls else "_" + {
                "gamemain": "GameMain", "commonwin": "CommonWin", "temp": "Temp", "login": "Login"}[cls]
            for n, nm in J.caption_slot_yields(r, stype):
                out.append((cls, base, "WBP_" + base + sfx, nm, J._san(n.idc) + "_grp"))
    return out


def print_plan():
    """Печать случаев правила без редактора."""
    for c in plan():
        # ★ Волна 15 (МЕЛК-1 ревизии optwin): только ASCII-стрелка — консоль cp1251 не знает «→».
        print("%s %s -> %s: подпись %s (группа %s)" % c)


def _asset_file(path):
    """Файл .uasset пакета /Game/…"""
    return os.path.join(CONTENT_DIR, path[len("/Game/"):].replace("/", os.sep) + ".uasset")


def _check_backup(path):
    """Бэкап ассета `/Game/…` ДО записи — `asda_headless_tool.backup_file` (★ Т3, 11.10.2026): копии нет — снимается и
    сверяется по размеру; есть — исходник до первой правки, не трогается (несовпадение — строка в журнал, не отказ).
    Отказ (исключение) — нет файла ассета, копия пустая или не прошла сверку. Возвращает путь копии."""
    f = _asset_file(path)
    bak = HT.backup_file(f, BACKUP_SET, log=log)
    if bak is None:
        raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % f)
    return bak


def _login_blocked(path):
    """True — окно сцены логина, запись закрыта сторожем (ASDA_ALLOW_LOGIN_WRITE не задан)."""
    from asda_login_guard import package_blocked_for   # ★ дедуп №2 Т18: имя инструмента — из __file__
    return package_blocked_for(__file__, path, quiet=True)


def main(standalone=False):
    """Починить имена по всем случаям правила (см. шапку). `standalone` — ручной запуск: запись только при бэкапе."""
    # Снимок дерева и обход — общие функции шага окна 78200 (без побочных эффектов при импорте).
    import asda_ui_smelting3_scroll as S3
    dry = bool(os.environ.get("ASDA_CAPYIELD_DRYRUN"))
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    index = {}
    for a in EAL.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False):
        p = a.split(".")[0]
        index.setdefault(p.rsplit("/", 1)[-1], []).append(p)
    fixed = seen = 0
    inspect = []
    for cls, base, wbp, cap_name, grp_name in plan():
        seen += 1
        paths = index.get(wbp) or []
        if len(paths) != 1:
            inspect.append("%s: ассет %s найден %d раз — пропуск" % (cap_name, wbp, len(paths)))
            continue
        path = paths[0]
        if _login_blocked(path):
            log("%s: окно логина, запись закрыта сторожем — пропуск" % path)
            continue
        bp = EAL.load_asset(path)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            raise RuntimeError("нет корневого канваса у %s" % path)
        nodes = {w.get_name(): w for w in S3._walk(root)}
        cap = nodes.get(cap_name)
        real = nodes.get(cap_name + "_1")
        if real is None:
            # Уже исправлено (или окно пересобрано исправленным импортёром): имя держит сам контрол.
            log("%s / %s: узла %s_1 нет — правок нет" % (wbp, cap_name, cap_name))
            continue
        # Узлы обязаны выглядеть ровно как заготовка импортёра — иначе не трогаем.
        why = []
        if not isinstance(cap, unreal.TextBlock):
            why.append("%s не TextBlock/отсутствует" % cap_name)
        elif str(cap.get_editor_property("text")) != "":
            why.append("у %s есть текст «%s»" % (cap_name, cap.get_editor_property("text")))
        if not isinstance(real, unreal.TextBlock):
            why.append("%s_1 не TextBlock" % cap_name)
        for w in (cap, real):
            par = w.get_parent() if w else None
            if not par or par.get_name() != grp_name:
                why.append("%s не в группе %s" % (w.get_name() if w else "?", grp_name))
        if why:
            inspect.append("%s: %s — НЕ трогаю (возможна правка владельца)" % (wbp, "; ".join(why)))
            continue
        log("%s: удалить пустую подпись %s, переименовать %s_1 → %s" % (wbp, cap_name, cap_name, cap_name))
        if dry:
            continue
        before = S3._snapshot(root)
        if not B.remove_widget_by_name(bp, cap_name, "TextBlock"):
            raise RuntimeError("%s: не удалён узел %s" % (wbp, cap_name))
        if not B.rename_widget_by_name(bp, cap_name + "_1", cap_name):
            raise RuntimeError("%s: не переименован %s_1" % (wbp, cap_name))
        after = S3._snapshot(B.get_root_canvas(bp))
        # Ожидание: всё как было, кроме пустышки (удалена) и живого узла (то же содержимое под именем подписи).
        want = dict(before)
        del want[cap_name]
        want[cap_name] = want.pop(cap_name + "_1")
        lost = [n for n in want if n not in after]
        changed = [n for n in want if n in after and after[n] != want[n]]
        extra = [n for n in after if n not in want]
        log("%s: узлов до %d, после %d; удалён пустой узел-подпись 1, переименован 1; "
            "потеряно узлов: %d; изменено узлов: %d; лишних: %d" % (wbp, len(before), len(after), len(lost), len(changed), len(extra)))
        if lost or changed or extra:
            raise RuntimeError("сверка не прошла: потеряно %s, изменено %s, лишних %s — НЕ сохраняю" % (lost, changed, extra))
        if standalone:
            _check_backup(path)
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % path)
        names2 = {w.get_name() for w in S3._walk(B.get_root_canvas(EAL.load_asset(path)))}
        if cap_name not in names2 or (cap_name + "_1") in names2:
            raise RuntimeError("%s: после сохранения имена не те" % path)
        fixed += 1
    for s in inspect:
        log("осмотреть: %s" % s)
    log("случаев правила %d, исправлено окон %d, осмотреть %d%s"
        % (seen, fixed, len(inspect), " (СУХОЙ ПРОГОН)" if dry else ""))
    log("ПОДПИСЬ-ИМЯ: ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта) — ничего.
if __name__ == "__main__":
    if "--plan" in sys.argv or unreal is None:
        # ★ Волна 15: вывод не падает на символах, которых нет в кодировке консоли (cp1251 и т. п.):
        #   кодировка прежняя, незнакомый символ заменяется «?». Логика плана не меняется.
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(errors="replace")
        print_plan()
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[подпись-имя] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
