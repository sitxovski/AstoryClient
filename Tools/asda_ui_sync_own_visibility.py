# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_sync_own_visibility.py — ★★★ ОДНОРАЗОВАЯ МИГРАЦИЯ УЖЕ СОБРАННЫХ ОКОН:
#   снять с узлов УНАСЛЕДОВАННЫЙ `Collapsed`, который прошил им прежний импортёр.
#
# ЗАЧЕМ. До 17.09.2026 `asda_jui_generic_umg.build_tree` наследовал скрытость ВНИЗ по дереву:
#   если у контрола в `.JUI` стоял `Visible = "0"`, весь его подвал уезжал в ассет `Collapsed`,
#   и собственный `Visible = "1"` каждого потомка терялся НАВСЕГДА. В оригинале так не бывает:
#   клиент держит у контрола ЕГО СОБСТВЕННЫЙ бит, а при отрисовке проверяет ЦЕПОЧКУ предков
#   (`CControl_IsVisibleChain`) — скрытый предок прячет поддерево ВРЕМЕННО. Импортёр починен
#   (см. комментарий в `build_tree`), но УЖЕ СОБРАННЫЕ ассеты чинить нечем: данные в них
#   разрушены. Этот скрипт и есть лечение — по 4811 узлам в 87 окнах.
#
# ★★★ ПОЧЕМУ НЕ ПРОСТО «ПЕРЕИМПОРТ». Закон [[owner-asset-edits-never-lose]]: «только точечная
#   запись по списку, проверка ручной правки, бэкап; БЕЗ МАССОВЫХ ПЕРЕИМПОРТОВ». Переимпорт
#   СНОСИТ ассет и собирает заново — всё, что владелец правил в редакторе, живёт ровно до
#   следующего такого прогона. Дефект же состоит РОВНО В ОДНОМ СВОЙСТВЕ (`visibility`) РОВНО
#   у перечисленных узлов, поэтому лечится точечной записью этого свойства и ничего больше.
#
# ОТКУДА БЕРЁТСЯ ЭТАЛОННОЕ ЗНАЧЕНИЕ. Из окна, СОБРАННОГО ПОЧИНЕННЫМ ИМПОРТЁРОМ В ПЕСОЧНИЦУ
#   (`ASDA_JUI_PKG_OVERRIDE=/Game/_JuiDiff/New`, боевые ассеты при этом не трогаются вовсе).
#   Это честнее, чем повторять правило импортёра «на словах»: сверяем с тем, что импортёр
#   ДЕЙСТВИТЕЛЬНО СОБИРАЕТ, узел в узел, по имени виджета.
#
# ★★★ ЧТО СКРИПТ ДЕЛАЕТ И ЧЕГО НЕ ДЕЛАЕТ НИКОГДА (правила написаны так, чтобы потерять ручную
#   правку было НЕЧЕМ):
#     • ТОЛЬКО РАСКРЫВАЕТ. Пишем лишь тем виджетам, у кого в БОЕВОМ `Collapsed`, а в ЭТАЛОНЕ —
#       не `Collapsed`. Обратный случай (в боевом видно, в эталоне скрыто) НЕ ТРОГАЕМ: это либо
#       правка владельца, либо шаг пост-импорта, и гасить такое вслепую — ровно та потеря,
#       от которой стоит закон. Он попадает в отчёт «ОСМОТРЕТЬ».
#     • ТОЛЬКО ПРИ ПОЛНОМ СОВПАДЕНИИ ОСТАЛЬНОГО. Перед записью строка дампа виджета
#       (`AsdaUmgBuilder.DumpWidgetTree`: класс, родитель, слот, цвет, текст, браш, UV, тинт)
#       сверяется с эталонной ПОМИМО поля `вид=`. Отличается хоть что-то — узел ПРОПУСКАЕТСЯ
#       и уходит в отчёт: значит его кто-то правил руками, и трогать его нельзя.
#       Исключение — два «мягких» поля (`z=` и `цвет(`, см. `_SOFT_FIELDS`): ими распоряжаются
#       ИМЕНОВАННЫЕ шаги пост-импорта, поэтому их расхождение личность узла не опровергает.
#       Такие узлы раскрываются, но перечисляются отдельно (`раскрыто_с_оговоркой`), а сами
#       значения мягких полей остаются БОЕВЫМИ — мы их не переписываем.
#     • ТОЛЬКО СВОЙСТВО `visibility`. Ни удалений, ни создания узлов, ни слотов, ни цветов.
#     • Узлы, которых в эталоне НЕТ (их дорисовывают шаги пост-импорта — подписи вкладок
#       `*_Text2/3` и т.п.), не трогаются и считаются нормой.
#     • ★★★ 07.10.2026 — ИСКЛЮЧЕНИЯ ШАГОВ (ревизия `Reports/owner_1007/label_band_and_8500_REVIEW.md`,
#       СРЕДН-2). Узлы, чью видимость ведёт ИМЕНОВАННЫЙ шаг пост-импорта по строке эталона, синхронизатор
#       не трогает вовсе (отчёт «исключено_шагом»). 17.09 он сверил окно чата с `.JUI Visible=1` и раскрыл
#       20 узлов WBP_CHAT_NEW, которые шаг `asda_ui_chat_markup_visibility` 15.09 погасил по коду
#       оригинала (chat.c:813/822/850/892, разрешение владельца) — старое поле ввода, скроллбар и родная
#       кнопка вкладки снова легли поверх живых. Список НЕ копируется сюда: он читается из таблицы
#       самого шага (`TABLE`), поэтому новая строка шага сразу становится исключением.
#     • ★★★ 07.10.2026 (вторая ревизия, `label_band_fixes_REVIEW.md` МЕЛК-1) — ОБЩИЙ МЕХАНИЗМ. Исключения
#       берутся из ВСЕХ шагов цепочки `asda_ui_post_import.STEPS`: шаг, который ведёт видимость узлов импортёра
#       (гасит по эталону, показывает рамку, держит ручную правку), объявляет функцию `step_visibility_nodes()` →
#       [(путь окна, узел, причина)]; синхронизатор вызывает её у каждого шага, где она есть. Шаги, которые по
#       скану 07.10 такие узлы ведут (`_REQUIRED_DECLARERS`), обязаны её иметь — иначе синхронизатор падает
#       ДО записи. Узлы, которые шаги СОЗДАЮТ сами, в эталонной песочнице отсутствуют («нет_в_эталоне»)
#       и не трогаются и без объявления.
#     • ★★★ 07.10.2026 — СТОРОЖ ЛОГИНА. Окна папки `/UI/Login/` (и всё, что `asda_login_guard` числит за сценой
#       логина) синхронизатор не грузит и не пишет никогда: пара пропускается с отметкой «пропущено_логин».
#
# ЗАПУСК (headless, редактор ЗАКРЫТ):
#     set ASDA_VISSYNC_PAIRS=<боевой>=<эталон>[;<боевой>=<эталон>...]
#     set ASDA_VISSYNC_REPORT=<путь к .json отчёта>
#     set ASDA_VISSYNC_DRYRUN=1        — только посчитать, ничего не писать
#     python Tools\run_headless_safe.py Tools\asda_ui_sync_own_visibility.py
#       --marker "СИНХРОНИЗАЦИЯ ВИДИМОСТИ ЗАВЕРШЕНА"
import importlib.util
import json
import os
import re

import unreal

# Папка инструментов — откуда грузятся шаги цепочки пост-импорта (их объявления видимости).
HERE = os.path.dirname(os.path.abspath(__file__))
# Имя функции-объявления в модуле шага: () → [(путь окна, узел, причина)].
_DECL_FUNC = "step_visibility_nodes"
# ★ 07.10.2026: шаги, которые по скану цепочки ведут видимость УЗЛОВ ИМПОРТЁРА (не только своих) и потому
#   ОБЯЗАНЫ объявлять их (скан всех 35 шагов на `COLLAPSED`/`set_visibility`/`"visibility"`):
#     asda_fix_worldmap_ui — свёрнуты родные комбобоксы 1721/1723, показана рамка 1710;
#     asda_fix_minimap_ui — JUI Visible=0 (группа контрола / узлы листа), кнопки строк каналов;
#     asda_ui_handmade — ручные правки владельца со свёрнутой видимостью (снимок);
#     asda_ui_tabbar_captions — видимость подписей вкладок по разметке;
#     asda_ui_missing_interactive — план 0 под созданной кнопкой/чекбоксом/держателем;
#     asda_ui_panel_own_visibility — собственный Visible=0 узлов панелей аукциона;
#     asda_ui_chat_markup_visibility — 20 узлов чата, погашенных эталоном (таблица `TABLE`);
#     asda_ui_option_extra_rows — `NoImage_grp_10/11` окна настроек (`TOUCH_VIS`).
#   Остальные шаги со словом «visibility» ставят его только СВОИМ новым узлам (их в песочнице нет).
_REQUIRED_DECLARERS = (
    "asda_fix_worldmap_ui", "asda_fix_minimap_ui", "asda_ui_handmade", "asda_ui_tabbar_captions",
    "asda_ui_missing_interactive", "asda_ui_panel_own_visibility", "asda_ui_chat_markup_visibility",
    "asda_ui_option_extra_rows",
)
# Папка окон сцены логина — синхронизатор их не трогает никогда (сторож, см. заголовок).
_LOGIN_DIR = "/UI/Login/"

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

# Поле видимости в строке дампа — единственное, что нам позволено менять.
_VIS = re.compile(r"вид=(\S+)")

# ★ ПОЛЯ, КОТОРЫМИ ЗАКОННО РАСПОРЯЖАЮТСЯ ШАГИ ПОСТ-ИМПОРТА, а не импортёр. При сверке «узел тот же
#   самый?» их различие НЕ считается признаком ручной правки — но и НЕ ПЕРЕЗАПИСЫВАЕТСЯ: мы трогаем
#   только `visibility`, а значение поля остаётся боевым.
#     `z=`     — ZOrder. Шаг `asda_ui_missing_interactive` кладёт свои кнопки класса S2 ПОД планы
#                (`z = -1`), шаг `asda_ui_title_tab_cover_z` опускает маску вкладок званий.
#     `цвет(`  — цвет подписи. Шаги `asda_ui_text_color_tweaks`, `asda_ui_tabbar_captions`,
#                `asda_ui_partyfind_map_captions` ставят цвета, которых нет в данных `.JUI`.
#   Личность узла при этом остаётся доказанной: класс, родитель, слот, шрифт, текст, браш, UV и
#   тинт обязаны совпасть ТОЧНО, иначе узел пропускается.
_SOFT_FIELDS = (re.compile(r" z=-?\d+"), re.compile(r"цвет\([^)]*\)"))


def _ident(line):
    """Строка дампа без видимости и без «мягких» полей — то, по чему узлы обязаны совпасть."""
    out = _VIS.sub("вид=X", line)
    for rx in _SOFT_FIELDS:
        out = rx.sub("", out)
    return out

# Имя видимости из дампа → значение перечисления UE.
_VIS_ENUM = {
    "Visible": unreal.SlateVisibility.VISIBLE,
    "Collapsed": unreal.SlateVisibility.COLLAPSED,
    "Hidden": unreal.SlateVisibility.HIDDEN,
    "HitTestInvisible": unreal.SlateVisibility.HIT_TEST_INVISIBLE,
    "SelfHitTestInvisible": unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE,
}


def log(m):
    unreal.log("[видимость-синк] %s" % m)


def _norm(line):
    """Схлопнуть выравнивающие пробелы дампа — иначе сверка ловит форматирование, а не смысл."""
    return " ".join(line.split())


def _dump_map(bp):
    """`DumpWidgetTree` → {имя виджета: нормализованная строка описания}."""
    out = {}
    for raw in B.dump_widget_tree(bp).split("\n"):
        line = raw.strip()
        if not line or line.startswith("==="):
            continue
        parts = line.split(None, 1)
        if len(parts) == 2:
            out[parts[0]] = _norm(parts[1])
    return out


def _widgets(bp):
    """{имя виджета: объект} по всему дереву окна."""
    out = {}
    stack = [B.get_root_canvas(bp)]
    while stack:
        w = stack.pop()
        if w is None:
            continue
        out[w.get_name()] = w
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                stack.append(w.get_child_at(i))
    return out


def _pkg(path):
    """Путь пакета без суффикса объекта: «/Game/X/WBP_A.WBP_A» → «/Game/X/WBP_A»."""
    return path.strip().split(".")[0]


def _load_module(name):
    """Загрузить модуль `Tools/<name>.py` под отдельным именем (шаги без побочных эффектов при импорте)."""
    spec = importlib.util.spec_from_file_location("vissync_" + name, os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def chain_steps():
    """Имена шагов цепочки пост-импорта (`asda_ui_post_import.STEPS`) — единственный источник списка."""
    return [name for name, _d in _load_module("asda_ui_post_import").STEPS]


def step_exclusions():
    """{путь пакета: {имя узла: «шаг: причина»}} — узлы, видимость которых ведут шаги цепочки.

    Объявления читаются из самих шагов (`step_visibility_nodes()`); шаг из `_REQUIRED_DECLARERS` без
    объявления либо с пустым объявлением — ошибка (синхронизатор не пишет вслепую).
    """
    out = {}
    steps = chain_steps()
    missing = [n for n in _REQUIRED_DECLARERS if n not in steps]
    if missing:
        raise RuntimeError("шагов %s нет в цепочке STEPS — список _REQUIRED_DECLARERS устарел" % missing)
    for name in steps:
        mod = _load_module(name)
        fn = getattr(mod, _DECL_FUNC, None)
        if fn is None:
            if name in _REQUIRED_DECLARERS:
                raise RuntimeError("шаг %s ведёт видимость узлов импортёра, но не объявляет %s()" % (name, _DECL_FUNC))
            continue
        rows = list(fn())
        if not rows and name in _REQUIRED_DECLARERS:
            raise RuntimeError("объявление %s.%s() пустое — исключения не прочитаны" % (name, _DECL_FUNC))
        for path, node, why in rows:
            out.setdefault(_pkg(path), {})[node] = "%s: %s" % (name, why)
        log("исключения шага %s: %d узлов" % (name, len(rows)))
    return out


def is_login_window(path):
    """True — окно сцены логина (`/UI/Login/` или пакет, который `asda_login_guard` числит за логином)."""
    if _LOGIN_DIR in path:
        return True
    try:
        import sys
        if HERE not in sys.path:
            sys.path.insert(0, HERE)
        from asda_login_guard import is_login_owned_package
        return bool(is_login_owned_package(_pkg(path)))
    except Exception as e:                                       # noqa: BLE001
        # Сторож обязан срабатывать с запасом: не смогли спросить — считаем окно чужим только по папке.
        log("сторож логина: asda_login_guard недоступен (%s) — проверка только по папке %s" % (e, _LOGIN_DIR))
        return False


def sync_one(live_path, ref_path, dry, excl=None):
    """Синхронизировать ОДНО окно. `excl` — {узел: шаг}, видимость которых ведёт шаг (не трогать)."""
    excl = excl or {}
    rec = {"боевой": live_path, "эталон": ref_path, "раскрыто": [], "пропущено_различие": [], "исключено_шагом": [],
           "осмотреть_видно_в_боевом": [], "нет_в_эталоне": [], "нет_в_боевом": [],
           "ручные_отличия": [], "раскрыто_с_оговоркой": [], "ошибка": None, "пропущено_логин": False}
    # ★ 07.10.2026: СТОРОЖ ЛОГИНА — окно сцены логина не грузим и не пишем (см. заголовок).
    if is_login_window(live_path):
        rec["пропущено_логин"] = True
        unreal.log_warning("[LOGIN-GUARD] [видимость-синк] %s: окно сцены логина — пропущено, запись запрещена"
                           % live_path)
        return rec
    live_bp = EAL.load_asset(live_path)
    ref_bp = EAL.load_asset(ref_path)
    if live_bp is None or ref_bp is None:
        rec["ошибка"] = "не загрузился ассет (боевой=%s эталон=%s)" % (live_bp is not None, ref_bp is not None)
        unreal.log_error("[видимость-синк] %s: %s" % (live_path, rec["ошибка"]))
        return rec

    live_d, ref_d = _dump_map(live_bp), _dump_map(ref_bp)
    rec["нет_в_эталоне"] = sorted(set(live_d) - set(ref_d))
    rec["нет_в_боевом"] = sorted(set(ref_d) - set(live_d))
    objs = _widgets(live_bp)

    for name in sorted(set(live_d) & set(ref_d)):
        # ★ АУДИТ РУЧНЫХ ПРАВОК (закон [[owner-asset-edits-never-lose]]). Всё, чем боевой узел
        #   отличается от свежесобранного ПОМИМО видимости, — это правка владельца либо результат
        #   шага пост-импорта. Такие узлы мы НЕ ТРОГАЕМ (кроме снятия Collapsed), но обязаны
        #   назвать их поимённо: без этого списка нельзя утверждать, что правка уцелела.
        if _VIS.sub("вид=X", live_d[name]) != _VIS.sub("вид=X", ref_d[name]):
            rec["ручные_отличия"].append(
                "%s\n      боевой: %s\n      эталон: %s" % (name, live_d[name], ref_d[name]))
        ml, mr = _VIS.search(live_d[name]), _VIS.search(ref_d[name])
        if not ml or not mr:
            continue
        vl, vr = ml.group(1), mr.group(1)
        if vl == vr:
            continue
        if name in excl:
            # ★ 07.10: видимость узла ведёт шаг пост-импорта по эталону — не наше дело (см. заголовок).
            rec["исключено_шагом"].append("%s: боевой=%s эталон=%s — ведёт шаг %s" % (name, vl, vr, excl[name]))
            continue
        # ★ ТОЛЬКО РАСКРЫТИЕ. Обратное направление — не наше дело (см. заголовок).
        if vl != "Collapsed":
            rec["осмотреть_видно_в_боевом"].append("%s: боевой=%s эталон=%s" % (name, vl, vr))
            continue
        if vr == "Collapsed":
            continue
        # ★ ВСЁ ОСТАЛЬНОЕ ОБЯЗАНО СОВПАДАТЬ — иначе узел кто-то правил руками.
        if _ident(live_d[name]) != _ident(ref_d[name]):
            rec["пропущено_различие"].append(
                "%s\n      боевой: %s\n      эталон: %s" % (name, live_d[name], ref_d[name]))
            continue
        if _VIS.sub("вид=X", live_d[name]) != _VIS.sub("вид=X", ref_d[name]):
            # Личность узла доказана, разошлись только «мягкие» поля (ZOrder/цвет) — раскрываем,
            # но называем такой узел отдельно: значение мягкого поля остаётся боевым.
            rec["раскрыто_с_оговоркой"].append(
                "%s: Collapsed → %s; мягкие поля оставлены боевыми\n      боевой: %s\n      эталон: %s"
                % (name, vr, live_d[name], ref_d[name]))
        w = objs.get(name)
        if w is None or vr not in _VIS_ENUM:
            rec["пропущено_различие"].append("%s: виджет не найден либо неизвестная видимость %s" % (name, vr))
            continue
        if not dry:
            w.set_editor_property("visibility", _VIS_ENUM[vr])
        rec["раскрыто"].append("%s: Collapsed → %s" % (name, vr))

    if (rec["раскрыто"] or rec["раскрыто_с_оговоркой"]) and not dry:
        B.compile_and_save(live_bp)
        EAL.save_loaded_asset(live_bp)
    log("%s: раскрыто %d, пропущено по различию %d, исключено шагом %d, «осмотреть» %d, ручных отличий %d, "
        "нет в эталоне %d, нет в боевом %d%s"
        % (live_path, len(rec["раскрыто"]), len(rec["пропущено_различие"]), len(rec["исключено_шагом"]),
           len(rec["осмотреть_видно_в_боевом"]), len(rec["ручные_отличия"]),
           len(rec["нет_в_эталоне"]), len(rec["нет_в_боевом"]),
           "  (СУХОЙ ПРОГОН)" if dry else ""))
    return rec


def main():
    pairs = [p for p in os.environ.get("ASDA_VISSYNC_PAIRS", "").split(";") if p.strip()]
    if not pairs:
        raise RuntimeError("ASDA_VISSYNC_PAIRS пуст: нечего синхронизировать")
    dry = os.environ.get("ASDA_VISSYNC_DRYRUN") == "1"
    report = {"сухой_прогон": dry, "окна": []}
    excl = step_exclusions()
    log("исключения шагов: %d узлов в %d окнах" % (sum(len(v) for v in excl.values()), len(excl)))
    for p in pairs:
        live, _, ref = p.partition("=")
        report["окна"].append(sync_one(live.strip(), ref.strip(), dry, excl.get(_pkg(live), {})))

    tot = sum(len(r["раскрыто"]) for r in report["окна"])
    skip = sum(len(r["пропущено_различие"]) for r in report["окна"])
    look = sum(len(r["осмотреть_видно_в_боевом"]) for r in report["окна"])
    soft = sum(len(r["раскрыто_с_оговоркой"]) for r in report["окна"])
    hand = sum(len(r["ручные_отличия"]) for r in report["окна"])
    extra = sum(len(r["нет_в_эталоне"]) for r in report["окна"])
    exc = sum(len(r["исключено_шагом"]) for r in report["окна"])
    login = sum(1 for r in report["окна"] if r.get("пропущено_логин"))
    report["итог"] = {"исключено_шагом": exc, "окон": len(report["окна"]), "раскрыто_узлов": tot,
                      "пропущено_логин": login,
                      "пропущено_по_различию": skip, "осмотреть": look,
                      "ручных_отличий": hand, "узлов_только_в_боевом": extra,
                      "раскрыто_с_оговоркой": soft}
    out = os.environ.get("ASDA_VISSYNC_REPORT")
    if out:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "w", encoding="utf-8") as f:
            json.dump(report, f, ensure_ascii=False, indent=1, sort_keys=True)
        log("отчёт: %s" % out)
    log("ИТОГ: окон %d, раскрыто узлов %d, пропущено по различию %d, исключено шагом %d, «осмотреть» %d, "
        "окон логина пропущено %d" % (len(report["окна"]), tot, skip, exc, look, login))
    # ★ Маркер для run_headless_safe.py: код возврата редактора недостоверен
    #   ([[headless-exit-code-unreliable]]) — успех опознаётся по этой строке.
    unreal.log("СИНХРОНИЗАЦИЯ ВИДИМОСТИ ЗАВЕРШЕНА")


# ★★★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
