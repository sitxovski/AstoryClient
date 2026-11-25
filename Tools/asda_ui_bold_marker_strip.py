# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_bold_marker_strip.py — ★★ МУСОРНЫЙ ХВОСТ ПЕРЕВОДА «(жирный)» В ТЕКСТАХ ОКОН (07.10.2026, отчёт
#   `Reports/owner_1007/audit_fixes2.md`, п. 2). [ИСПРАВЛЕНИЕ ДАННЫХ ПЕРЕВОДА] — правка АССЕТОВ, кода не касается.
#
#   ОТКУДА ХВОСТ. В `.JUI` у дизайнера в CaptionText стоит пометка «(볼드)» / «(볼드체)» / «(볼체)» / «_볼드체» (= «жирным»).
#   Словарь импортёра `Tools/kor_ru_ui.json` переводил её как ТЕКСТ — «… (жирный)», «…_жирный», «…; жирный» (93 ключа,
#   85 разных строк). Импортёр (`asda_jui_generic_umg.fix_text`) и шаг `asda_ui_missing_text_nodes` кладут перевод в
#   TextBlock как есть, поэтому хвост виден везде, где код окна не ставит свой текст (Inven_Strength, окна CAMPWAR и др.).
#   ИСТОЧНИК ИСПРАВЛЕН 07.10.2026: значения 93 ключей словаря без пометки (новый импорт даёт чистый текст).
#   ЭТОТ ШАГ переносит ту же правку в уже собранные ассеты БЕЗ перегенерации UI (закон no-ui-regeneration).
#
#   ПРАВИЛО (закон owner-asset-edits-never-lose): у TextBlock меняется ТОЛЬКО свойство `text`, и только если строка
#   В ТОЧНОСТИ равна прежнему значению словаря («было» из `Tools/data/ui_bold_marker_pairs.json`) — это вид импортёра.
#   Уже чистая строка («стало») — «готово» (идемпотентно). Иная строка с пометкой — «осмотреть», узел НЕ трогается
#   (возможна ручная правка владельца). Снимок всего дерева окна до/после: у прочих узлов без изменений, у правленого
#   изменилась только строка — иначе исключение ДО сохранения.
#   Ассеты не перечислены вручную: шаг находит их сам — побайтовый поиск UTF-16 пометки в .uasset под /Game/AsdaAssets/UI.
#   Окна сцены логина — под `asda_login_guard` (без ASDA_ALLOW_LOGIN_WRITE=1 пропуск со строкой в журнале).
#
# Запуск (редактор и игра ЗАКРЫТЫ; коммандлет):
#   python Tools\run_headless_safe.py Tools\asda_ui_bold_marker_strip.py --commandlet-rhi --marker "ХВОСТ-ЖИРНЫЙ: ГОТОВО"
#   ASDA_BOLDSTRIP_DRYRUN=1 — сухой прогон. Вне редактора: `python asda_ui_bold_marker_strip.py --plan` (список ассетов).
import json
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

# Бэкап до записи (★ Т3, 11.10.2026): `asda_headless_tool.backup_file` через `T._ensure_backup`, набор
#   `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_bold_marker_strip\<путь от @@ASTORY_DEV_ROOT@@>` (была прошитая папка `2026-10-07_audit_fixes2`).
#   Повторный запуск после записи больше не падает: копия есть — это исходник до первой правки, она не перезаписывается.
BACKUP_SET = "bold_marker_strip"
# ASDA_BOLDSTRIP_BACKUP_ROOT — готовая папка набора целиком (переопределение, как прежде); не задана — None
BACKUP_DIR_OVERRIDE = os.environ.get("ASDA_BOLDSTRIP_BACKUP_ROOT") or None
PAIRS_JSON = os.path.join(HERE, "data", "ui_bold_marker_pairs.json")   # «было → стало» по словарю (93 ключа, 85 строк)
UI_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content\AsdaAssets\UI"    # где искать ассеты окон
MARKER = u"жирный"                                              # пометка в UTF-16 (так FText лежит в .uasset)


def log(m):
    """Строка журнала шага (в редакторе — в лог UE, вне — в stdout)."""
    if unreal:
        unreal.log("[хвост-жирный] %s" % m)
    else:
        print("[хвост-жирный] %s" % m)


def pairs():
    """Словарь «было → стало» из `data/ui_bold_marker_pairs.json`."""
    with open(PAIRS_JSON, encoding="utf-8") as f:
        return {a: b for a, b in json.load(f)["pairs"]}


def find_assets():
    """Пакеты /Game/… окон, в .uasset которых есть пометка (UTF-16LE). Без unreal."""
    pat = MARKER.encode("utf-16-le")
    out = []
    for root, _dirs, files in os.walk(UI_DIR):
        for fn in files:
            if not fn.endswith(".uasset"):
                continue
            p = os.path.join(root, fn)
            with open(p, "rb") as f:
                if pat in f.read():
                    rel = os.path.relpath(p, r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content")[:-len(".uasset")]
                    out.append("/Game/" + rel.replace(os.sep, "/"))
    return sorted(out)


def _tb():
    """Общие помощники шага `asda_ui_text_block_align` (обход, снимок, бэкап) — одна реализация на все шаги."""
    import asda_ui_text_block_align as T
    return T


def main(standalone=False):
    """Снять пометку у TextBlock всех найденных окон. `standalone` — ручной запуск: бэкап обязателен."""
    T = _tb()
    dry = bool(os.environ.get("ASDA_BOLDSTRIP_DRYRUN"))
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    P = pairs()
    clean = set(P.values())
    from asda_login_guard import login_package_blocked
    assets = find_assets()
    fixed = ok = 0
    inspect, skipped = [], []
    for asset in assets:
        if login_package_blocked(asset, "asda_ui_bold_marker_strip.py"):
            skipped.append(asset)
            log("%s: окно сцены логина — пропуск (защита логина; нужно ASDA_ALLOW_LOGIN_WRITE=1 от владельца)" % asset)
            continue
        bp = EAL.load_asset(asset)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            raise RuntimeError("нет корневого канваса у %s" % asset)
        todo = []
        for w in T._walk(root):
            if not isinstance(w, unreal.TextBlock):
                if isinstance(w, unreal.EditableText) and MARKER in str(w.get_editor_property("hint_text")):
                    inspect.append("%s: %s — пометка в подсказке поля ввода (шаг не трогает)" % (asset, w.get_name()))
                continue
            s = str(w.get_editor_property("text"))
            if s in P:
                todo.append((w, s, P[s]))
            elif MARKER in s:
                inspect.append("%s: %s — строка «%s» не из словаря, НЕ трогаю" % (asset, w.get_name(), s))
            elif s in clean and s:
                ok += 1
        for w, s, n in todo:
            log("%s [%s] %s: «%s» → «%s»" % (asset.rsplit("/", 1)[-1], "сухо" if dry else "править", w.get_name(), s, n))
        if dry or not todo:
            if not todo:
                log("%s: правок нет (пометка только в служебных данных пакета или уже снята)" % asset)
            continue
        if standalone:
            T._ensure_backup(asset, BACKUP_SET, set_dir=BACKUP_DIR_OVERRIDE)
        before = T._snapshot(root)
        names = set()
        for w, s, n in todo:
            w.set_editor_property("text", unreal.Text(n))
            names.add(w.get_name())
        root2 = B.get_root_canvas(bp)
        after = T._snapshot(root2)
        lost = [k for k in before if k not in after]
        extra = [k for k in after if k not in before]
        foreign = [k for k in before if k in after and before[k] != after[k] and k not in names]
        # у правленого узла: всё, кроме строки (индекс 1 подписи текста), обязано совпасть
        broken = [k for k in names if before[k][:6] != after[k][:6]
                  or before[k][6][0] != after[k][6][0] or before[k][6][2:] != after[k][6][2:]]
        wrong = [w.get_name() for w, s, n in todo if after[w.get_name()][6][1] != n]
        log("%s: узлов до %d, после %d; правлено %d; потеряно узлов: %d; лишних: %d; изменено чужих: %d; "
            "испорчено у правленых: %d; строка не легла: %d" % (asset.rsplit("/", 1)[-1], len(before), len(after),
                                                                len(names), len(lost), len(extra), len(foreign),
                                                                len(broken), len(wrong)))
        if lost or extra or foreign or broken or wrong:
            raise RuntimeError("сверка не прошла: потеряно %s, лишних %s, чужих %s, испорчено %s, не легло %s — НЕ сохраняю"
                               % (lost, extra, foreign, broken, wrong))
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % asset)
        log("ASDA_WRITES_ASSET: %s" % asset)
        fixed += len(names)
    for s in inspect:
        log("осмотреть: %s" % s)
    log("ассетов с пометкой %d (пропущено окон логина %d): исправлено строк %d, строк, уже равных «стало» %d, осмотреть %d%s"
        % (len(assets), len(skipped), fixed, ok, len(inspect), " (СУХОЙ ПРОГОН)" if dry else ""))
    if inspect and not dry:
        raise RuntimeError("хвост-жирный: %d случаев «осмотреть» — строки не в виде импортёра, см. лог" % len(inspect))
    log("ХВОСТ-ЖИРНЫЙ: ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта) — ничего.
if __name__ == "__main__":
    if "--plan" in sys.argv or unreal is None:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        for a in find_assets():
            print(a)
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[хвост-жирный] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
