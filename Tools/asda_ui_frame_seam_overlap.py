# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_frame_seam_overlap.py — ★★ ЩЕЛЬ НА СТЫКЕ КУСКОВ СОСТАВНЫХ РАМОК ОКОН (24.10.2026). [ИСПРАВЛЕНИЕ] — правка
#   АССЕТОВ, кода не касается (закон no-runtime-widget-fixes).
#
#   ДЕФЕКТ (снимок владельца: окно сообщества 6400, вкладка «Друг», клиент alpha-2.0.0). Рамка окна и прочие составные
#   рамки — 9 (3×3) или 3 отдельных картинки встык. При масштабе интерфейса ≠ 1.0 Slate округляет ПОЗИЦИИ кусков, а размеры
#   оставляет дробными: середина кончается на x.9, соседний кусок начинается с x+1.0 — пиксель без кусков, сквозь окно виден
#   мир (вертикальная/горизонтальная светлая линия). Воспроизведено путём игрока (`-game`,
#   `-ini:Engine:[/Script/Engine.UserInterfaceSettings]:ApplicationScale=1.1797`): живая геометрия `COMMUNITY_WIN_0_4`
#   282.0…740.9, `COMMUNITY_WIN_0_5` с 742.0, столбец 741 — фон мира. При масштабе 1.0 щели нет.
#
#   ПРАВКА (решение владельца 24.10: «проверь все окна с такой рамкой и поправь щель тем же способом»): средние куски уходят
#   ПОД соседей на 1 единицу (растяжение однородного среза) с ZOrder −1 (края середины) / −2 (тело); углы не меняются.
#   Какие стороны каких рамок — ЗАКРЫТЫЙ план `Tools/data/ui_frame_seam_overlap.json`, построенный планировщиком
#   `Tools/asda_ui_frame_seam_plan.py` по слепку `Tools/asda_ui_dump_tree_order.py`: каждая сторона проверена офлайн-
#   симуляцией кадра при 1.0 (пиксели не меняются), неоднородный срез и полупрозрачный край соседа — пропуск. Аудит всех
#   составных рамок с причинами пропуска — `Reports/frame_seam_2026-10-24/audit_all_frames.tsv`.
#
#   БЕЗОПАСНОСТЬ (закон owner-asset-edits-never-lose): кусок трогается, только если он В ТОЧНОСТИ в виде импортёра (слот и
#   ZOrder = «было» плана, точечный анкор, alignment 0, без авторазмера). Уже исправленный — «готово» (идемпотентно). Иной
#   вид любого куска — вся рамка «осмотреть» и НЕ трогается (прочие рамки окна правятся). Снимок дерева до/после: у прочих
#   узлов без изменений. Ручной запуск пишет только после бэкапа того же размера (закон backup-before-edit).
#
# Запуск (редактор и игра ЗАКРЫТЫ; коммандлет):
#   python Tools\run_headless_safe.py Tools\asda_ui_frame_seam_overlap.py --marker "ШОВ-РАМКИ: ГОТОВО"
#   ASDA_FRAMESEAM_DRYRUN=1 — сухой прогон; ASDA_FRAMESEAM_BACKUP_DIR=<папка набора> — куда класть бэкап;
#   ASDA_FRAMESEAM_ONLY=<WBP_a,WBP_b> — только эти окна.
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

DRYRUN_ENV = "ASDA_FRAMESEAM_DRYRUN"          # ключ сухого прогона шага (таблица DRY_ENV раннера пост-импорта)
BACKUP_SET = "frame_seam_overlap"              # имя набора бэкапа по умолчанию
BACKUP_DIR_ENV = "ASDA_FRAMESEAM_BACKUP_DIR"   # готовая папка набора (переопределение)
ONLY_ENV = "ASDA_FRAMESEAM_ONLY"               # ограничить окнами (имена WBP через запятую)
PLAN_FILE = os.path.join(HERE, "data", "ui_frame_seam_overlap.json")   # закрытый план (планировщик asda_ui_frame_seam_plan)
EPS = 0.05                                     # допуск сравнения чисел слота
# ★ 24.10.2026, слово владельца: «Разрешаю поправить щель в 4 окнах сцены логина (только ассеты окон, Login.umap не трогать)».
#   Точечный список: из пакетов сцены логина шаг пишет ТОЛЬКО эти окна и только при ASDA_ALLOW_LOGIN_WRITE=1 процесса (его
#   требует C++-сторож сохранения AsdaLoginSaveGuard; уровень Login он не открывает — для него ASDA_ALLOW_LOGIN_MAP_WRITE, не
#   задавать). Прочие пакеты сцены логина шаг пропускает и при этом ключе.
LOGIN_APPROVED = {"WBP_2nd_ID_Warnning_Win", "WBP_ANI_WIN", "WBP_CharName_Change_Win", "WBP_MESSAGE_WIN"}


def log(m):
    """Строка журнала шага (в редакторе — в лог UE, вне — в stdout)."""
    if unreal:
        unreal.log("[шов-рамки] %s" % m)
    else:
        print("[шов-рамки] %s" % m)


def load_plan():
    """План: путь ассета → [рамка {frame, pieces{имя: {old, z_old, new, z_new, …}}}]."""
    data = json.load(open(PLAN_FILE, encoding="utf-8"))
    by_asset = {}
    for f in data["frames"]:
        by_asset.setdefault(f["path"], []).append(f)
    return by_asset


def _tb():
    """Общие помощники шага `asda_ui_text_block_align` (снимок, слоты, бэкап) — одна реализация на все шаги."""
    import asda_ui_text_block_align as T
    return T


def _near(a, b):
    """Покомпонентное равенство чисел с допуском EPS."""
    return len(a) == len(b) and all(abs(float(x) - float(y)) <= EPS for x, y in zip(a, b))


def _is_dry():
    """Сухой прогон: свой ключ или общий ключ цепочки пост-импорта."""
    try:
        import asda_ui_step_dryrun as D
        return D.is_dry(DRYRUN_ENV)
    except ImportError:
        return bool(os.environ.get(DRYRUN_ENV))


def _piece_state(name, spec, nodes, asset=None):
    """Кусок: ('fixed'|'ok'|'inspect', текст). `asset` — путь окна (для кусков, расширенных шагом `asda_ui_inventory_x2`)."""
    T = _tb()
    old, z_old, want, z_want = spec["old"], spec["z_old"], spec["new"], spec["z_new"]
    w = nodes.get(name)
    if not isinstance(w, unreal.Image):
        return "inspect", "%s: нет Image" % name
    s = T._slot_sig(w)
    if s is None or s[3]:
        return "inspect", "%s: слот не канвасный или авторазмер" % name
    offs, anch, align, _auto, z = s
    if not (_near(anch[0:2], anch[2:4]) and _near(align, (0.0, 0.0))):
        return "inspect", "%s: анкоры %s / alignment %s — не вид импортёра" % (name, anch, align)
    unchanged = _near(old, want) and z_old == z_want
    if _near(offs, want) and z == z_want:
        return "ok", ("%s: без изменений по плану" % name) if unchanged else ("%s: уже исправлено" % name)
    # ★ 29.10.2026 (инвентарь ×2): рамки окна сумки шаг `asda_ui_inventory_x2` (идёт ПОСЛЕ этого) расширяет на +198 поверх
    #   правки шва — «стало» плана шва = его «было». Повторный прогон цепочки без переимпорта видит расширенный кусок; это
    #   «готово», а не «осмотреть» (ZOrder шва сохранён). Решение — закрытая таблица того шага (`frame_seam_done`).
    if z == z_want and asset and "/Inventory/WBP_INVEN_WIN" in asset:
        import asda_ui_inventory_x2 as INVX2
        if INVX2.frame_seam_done(asset, name, offs, want):
            return "ok", "%s: уже исправлено и расширено шагом asda_ui_inventory_x2" % name
    if not _near(offs, old) or z != z_old:
        return "inspect", "%s: слот %s / z %d не совпадают с видом импортёра %s / %d — НЕ трогаю" % (name, offs, z, tuple(old), z_old)
    return "fixed", "%s: слот %s z %d → %s z %d" % (name, tuple(old), z_old, tuple(want), z_want)


def main(standalone=False):
    """Применить план. `standalone` — ручной запуск: бэкап обязателен."""
    T = _tb()
    dry = _is_dry()
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    from asda_login_guard import login_package_blocked
    only = {x.strip() for x in (os.environ.get(ONLY_ENV) or "").split(",") if x.strip()}
    plan = load_plan()
    stats = dict(assets=0, saved=0, frames_fixed=0, frames_ok=0, pieces=0)
    inspect = []
    for asset in sorted(plan):
        frames = plan[asset]
        wname = asset.rsplit("/", 1)[-1]
        if only and wname not in only:
            continue
        if login_package_blocked(asset, "asda_ui_frame_seam_overlap.py"):
            log("%s: окно сцены логина — пропуск (защита логина)" % wname)
            continue
        from asda_login_guard import is_login_owned_package
        if is_login_owned_package(asset) and wname not in LOGIN_APPROVED:
            log("%s: пакет сцены логина вне разрешения владельца 24.10 — пропуск" % wname)
            continue
        stats["assets"] += 1
        bp = EAL.load_asset(asset)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            inspect.append("%s: не грузится / нет корневого канваса" % wname)
            continue
        nodes = {w.get_name(): w for w in T._walk(root)}
        todo = {}   # имя куска → spec
        for fr in frames:
            st = {n: _piece_state(n, sp, nodes, asset) for n, sp in fr["pieces"].items()}
            bad = [m for v, m in st.values() if v == "inspect"]
            if bad:
                inspect.append("%s/%s: %s" % (wname, fr["frame"], "; ".join(bad)))
                log("%s [осмотреть] %s: %s" % (wname, fr["frame"], "; ".join(bad)))
                continue
            fix = [n for n, (v, _m) in st.items() if v == "fixed"]
            if fix:
                stats["frames_fixed"] += 1
                for n in fix:
                    todo[n] = fr["pieces"][n]
                log("%s [править] %s: %s" % (wname, fr["frame"], "; ".join(st[n][1] for n in sorted(fix))))
            else:
                stats["frames_ok"] += 1
        if dry or not todo:
            continue
        if standalone:
            T._ensure_backup(asset, BACKUP_SET, set_dir=os.environ.get(BACKUP_DIR_ENV) or None)
        before = T._snapshot(root)
        for n, sp in todo.items():
            w = nodes[n]
            T._set_slot(w, sp["new"], (0.0, 0.0), False)
            T._slot(w).set_editor_property("z_order", sp["z_new"])
        after = T._snapshot(B.get_root_canvas(bp))
        lost = [n for n in before if n not in after]
        extra = [n for n in after if n not in before]
        foreign = [n for n in before if n in after and before[n] != after[n] and n not in todo]
        # У правленого куска меняется только слот (смещения и z); класс, родитель, видимость, кисть — прежние.
        broken = [n for n in todo if before[n][:2] != after[n][:2] or before[n][3:] != after[n][3:]
                  or before[n][2][1:4] != after[n][2][1:4]]
        log("%s: узлов до %d, после %d; правлено кусков %d; потеряно: %d; лишних: %d; изменено чужих: %d; испорчено: %d"
            % (wname, len(before), len(after), len(todo), len(lost), len(extra), len(foreign), len(broken)))
        if lost or extra or foreign or broken:
            raise RuntimeError("%s: сверка не прошла: потеряно %s, лишних %s, чужих %s, испорчено %s — НЕ сохраняю"
                               % (wname, lost, extra, foreign, broken))
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % asset)
        log("ASDA_WRITES_ASSET: %s" % asset)
        stats["saved"] += 1
        stats["pieces"] += len(todo)
    for s in inspect:
        log("осмотреть: %s" % s)
    log("окон %d: сохранено %d; рамок исправлено %d, уже в порядке %d; кусков %d; осмотреть %d%s"
        % (stats["assets"], stats["saved"], stats["frames_fixed"], stats["frames_ok"], stats["pieces"], len(inspect),
           " (СУХОЙ ПРОГОН)" if dry else ""))
    if inspect and not dry:
        raise RuntimeError("шов-рамки: %d случаев «осмотреть» — куски не в виде импортёра, см. лог" % len(inspect))
    log("ШОВ-РАМКИ: ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта) — ничего.
if __name__ == "__main__":
    if unreal is None:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        p = load_plan()
        print("окон %d, рамок %d" % (len(p), sum(len(v) for v in p.values())))
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[шов-рамки] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
