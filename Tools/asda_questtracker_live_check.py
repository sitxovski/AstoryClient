# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_questtracker_live_check.py — ЖИВАЯ (PIE) проверка ЭКРАННОГО ТРЕКЕРА ЗАДАНИЙ (окно 1800).
#
# ★★★ ЗАЧЕМ ИМЕННО ЖИВАЯ (закон `false-negatives-method`). Офлайн-дамп `.uasset` показывает
#   разметку и НЕ показывает главного: открывается ли окно, встают ли записи по формуле
#   `15*строк + 5*i + 18`, считается ли дистанция, поворачивается ли стрелка компаса.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт даёт строку-доказательство в логе):
#   1. журнал наполняется (эмуляция пакета 5047)          — `Asda.QuestLog.FakeJournal 12`
#   2. ★ ЛИМИТ 12: 13-я попытка обязана получить отказ    — `Asda.Quest.Tracker limittest 3`
#      (по 3 строки условий в записи — САМЫЙ ВЫСОКИЙ список, на нём проверяется раскладка)
#   3. ★ раскладка ЧИСЛАМИ: верх/высота окна, полоса между мини-картой и хотбаром, масштаб,
#      позиция КАЖДОЙ из 12 групп (в т.ч. клонов 7…12)    — `Asda.Quest.Tracker`
#   4. постановка на отслеживание (путь галки 743)        — `Asda.Quest.Tracker track <ячейка>`
#   5. цель, дистанция В МЕТРАХ, угол компаса             — `Asda.Quest.Tracker` (дамп ЧИСЛАМИ)
#   6. окно реально рисуется                             — снимки в `Reports/questtracker_live`
#   7. счётчик «есть/надо» двигается пакетом 5052         — `Asda.QuestLog.FakeProgress`
#   8. снятие записи перестраивает список без дырок       — `Asda.Quest.Tracker untrack <ячейка>`
#   9. ★ СОХРАНЕНИЕ СПИСКА между сессиями: список пишется в ini, `clear` (это делает выход из
#      игры) стирает записи, `restore` поднимает их обратно — `Asda.QuestLog.Tracked`
#  10. ★ ЗАДАНИЕ УШЛО ИЗ ЖУРНАЛА (сдано/отменено) — запись обязана исчезнуть сама:
#      журнал переливается меньшим числом ячеек        — `Asda.QuestLog.FakeJournal 2`
#  11. ★ ПОЗИЦИЯ ОКНА между сессиями: перед запуском в ini кладётся сдвиг окна 1800, и окно
#      обязано открыться сдвинутым — `asda.UI.DumpWindowPos` + верх окна в дампе трекера
#
# ЗАПУСК: Tools\run_headless_questtracker.bat
import glob
import io
import os
import re
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/questtracker_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
INI = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Config/WindowsEditor/GameUserSettings.ini"
LEVEL = "/Game/Map/alpen/alpen"

# ★ Сдвиг, который кладётся в ini ДО запуска: имитация «игрок перетащил окно в прошлой сессии».
#   Формат и ключ — общего механизма раскладки окон (`[AsdaWindowLayout] Pos.<окно>.<цель>`),
#   своего хранилища у трекера нет.
DRAG_DY = -120.0

# ★★★ Е2 («список отслеживаемых не сохраняется между сессиями»). Ключ ini состоит из ИМЕНИ
#   ПЕРСОНАЖА, а имя в игре появляется ПОЗЖЕ журнала — на этой гонке дефект и держался.
#   В headless сервера нет, поэтому имя кладём тем же тестовым крюком, что и павна
#   (`Asda.World.FakeSelfSpawn <x> <y> <имя>` — пишет в ТО ЖЕ поле `SelfCharName`).
CHAR_NAME = "Sitxovski"
# Номер задания, который кладётся в ini ДО запуска редактора: это и есть «прошлая сессия».
#   25 = «Осмотр собирателя живых камней» (эпизод 2026) — он же поднимается FakeJournal 4 0 2026.
SEED_TRACKED = "25"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 30000


def log(m):
    import unreal
    unreal.log("[трекер-живой] " + str(m))


def seed_window_offset():
    """Положить в ini сохранённую позицию окна 1800 (как будто её оставила прошлая сессия).

    ★ ЗАПУСКАЕТСЯ ВНЕ РЕДАКТОРА (обычным python из .bat), и это принципиально: `GConfig`
    читает ini ОДИН раз при старте, а при выходе переписывает секцию целиком — правка файла из
    уже запущенного редактора не была бы ни прочитана, ни сохранена (проверено: ключ пропал).
    """
    key = "Pos.Quest_State_Win.Quest_State_Win"
    val = "0.000,%.3f" % DRAG_DY
    try:
        txt = io.open(INI, encoding="utf-8-sig").read() if os.path.isfile(INI) else ""
    except Exception:
        txt = ""
    if "[AsdaWindowLayout]" not in txt:
        txt = txt.rstrip() + "\n\n[AsdaWindowLayout]\n"
    if key in txt:
        txt = re.sub(re.escape(key) + r"=[^\r\n]*", key + "=" + val, txt)
    else:
        txt = txt.replace("[AsdaWindowLayout]", "[AsdaWindowLayout]\n" + key + "=" + val, 1)
    # ★★★ Е2: тем же способом кладём «список из прошлой сессии». Ключ — `Tracked.<Имя>`;
    #   имя обязано совпасть с тем, что в PIE отдаст тестовый крюк спавна, иначе восстанавливать
    #   будет нечего (а прежний код в этом месте писал `Tracked.Default` и терял список навсегда).
    tkey = "Tracked." + CHAR_NAME
    NL = chr(10)   # ★ перевод строки через chr(): в ini у нас Unix-переводы, как у GConfig
    if "[AsdaQuestTracker]" not in txt:
        txt = txt.rstrip() + NL + NL + "[AsdaQuestTracker]" + NL
    if tkey in txt:
        txt = re.sub(re.escape(tkey) + "=[^" + chr(92) + "r" + chr(92) + "n]*", tkey + "=" + SEED_TRACKED, txt)
    else:
        txt = txt.replace("[AsdaQuestTracker]", "[AsdaQuestTracker]" + NL + tkey + "=" + SEED_TRACKED, 1)
    io.open(INI, "w", encoding="utf-8-sig").write(txt)


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    log("уровень %s загружен" % LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False, "shots": []}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 3 стр.)

    def dump_ini_section():
        """Прочитать ФАЙЛ настроек с диска и напечатать секцию трекера.

        ★ Это отдельное доказательство от `Asda.QuestLog.Tracked show`: тот читает `GConfig`
        (память процесса), а владелец жалуется именно на то, что между сессиями ничего не
        остаётся — то есть на ДИСК. Читаем файл заново.
        """
        try:
            txt = io.open(INI, encoding="utf-8-sig").read()
        except Exception as e:
            log("НЕ удалось прочитать %s: %s" % (INI, e))
            return
        out, grab = [], False
        for line in txt.splitlines():
            if line.startswith("["):
                grab = (line.strip() == "[AsdaQuestTracker]")
                continue
            if grab and line.strip():
                out.append(line.strip())
        log("файл %s, секция [AsdaQuestTracker] НА ДИСКЕ: %s"
            % (INI, "; ".join(out) if out else "ПУСТО/НЕТ"))

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    PLAN = [
        # ★ У карт мира в порту нет своего GameMode: PIE поднимает GameModeBase, и
        #   AAsdaPlayerController не создаётся. Переезжаем в тот же уровень с ЯВНЫМ режимом.
        lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"),
        lambda: cmd("Asda.Quest.Tracker"),                  # пусто — записей 0, окна нет
        lambda: cmd("Asda.QuestLog.FakeJournal 12"),        # эмуляция 5047: 12 заданий

        # ── 1. ЛИМИТ 12 на самом высоком списке (3 строки условий в каждой записи) ──
        lambda: cmd("Asda.Quest.Tracker limittest 3"),
        lambda: cmd("Asda.Quest.Tracker"),                  # геометрия ЧИСЛАМИ + все 12 групп
        lambda: cmd("asda.UI.DumpWindowPos"),               # окно сохраняемое? что применено?
        lambda: cmd("asda.UI.DumpDrag"),                    # ручка окна 1800 и её цель
        lambda: take_shot("tracker_limit12"),
        lambda: harvest("tracker_limit12"),
        # то же, но по одной строке условий — обычный вид списка из 12 записей
        lambda: cmd("Asda.Quest.Tracker limittest 1"),
        lambda: cmd("Asda.Quest.Tracker"),
        lambda: take_shot("tracker_limit12_short"),
        lambda: harvest("tracker_limit12_short"),
        lambda: cmd("Asda.Quest.Tracker clear"),
    ]
    # ── 2. Постановка на отслеживание ЖИВЫМ путём (галка 743) по всем 12 ячейкам ──
    for slot in range(12):
        PLAN.append(lambda s=slot: cmd("Asda.Quest.Tracker track %d" % s))
    PLAN += [
        lambda: cmd("Asda.Quest.Tracker"),                  # ЧИСЛА: цель, дистанция, угол
        lambda: cmd("Asda.QuestLog.Tracked show"),          # что записалось в ini
        lambda: take_shot("tracker_open"),
        lambda: harvest("tracker_open"),
        # Прогресс цели пакетом 5052 — строка «есть/надо» обязана сдвинуться без открытия журнала.
        lambda: cmd("Asda.QuestLog.FakeProgress 0 21299 1"),
        lambda: cmd("Asda.Quest.Tracker"),
        lambda: take_shot("tracker_progress"),
        lambda: harvest("tracker_progress"),

        # ── 3. Снятие записи: список обязан перестроиться без дырок ──
        lambda: cmd("Asda.Quest.Tracker untrack 2"),
        lambda: cmd("Asda.Quest.Tracker"),
        lambda: take_shot("tracker_untrack"),
        lambda: harvest("tracker_untrack"),

        # ── 4. СОХРАНЕНИЕ СПИСКА: «вышел» (clear — ровно это делает выход) → «зашёл» (restore) ──
        lambda: cmd("Asda.QuestLog.Tracked save"),
        lambda: cmd("Asda.Quest.Tracker clear"),
        lambda: cmd("Asda.Quest.Tracker"),                  # записей 0 — окно скрыто
        lambda: cmd("Asda.QuestLog.Tracked restore"),
        lambda: cmd("Asda.Quest.Tracker"),                  # записи вернулись
        lambda: take_shot("tracker_restored"),
        lambda: harvest("tracker_restored"),

        # ── 5. ЗАДАНИЕ УШЛО ИЗ ЖУРНАЛА (сдано/отменено): журнал переливается двумя ячейками ──
        lambda: cmd("Asda.QuestLog.FakeJournal 2"),
        lambda: cmd("Asda.Quest.Tracker"),                  # остались только те, что ещё в журнале
        lambda: cmd("Asda.QuestLog.Tracked show"),
        lambda: take_shot("tracker_after_journal_shrink"),
        lambda: harvest("tracker_after_journal_shrink"),
        lambda: cmd("Asda.Quest.Tracker clear"),
        lambda: cmd("Asda.Quest.Tracker"),

        # ══════════════════════════════════════════════════════════════════════════════════════
        # ★★★ ТРИ ЗАЯВКИ ВЛАДЕЛЬЦА 2026-08-23 (`Reports/quest_tracker_fix.md`)
        # ══════════════════════════════════════════════════════════════════════════════════════
        # ── Д1: задание БЕЗ требований по предметам («Осмотр собирателя живых камней»,
        #        эпизод 2026 / квест 25, все 10 под-записей `.QST` = −1).
        #        Эталон: галка гаснет молча. Порт: берём, потому что цель с координатами есть.
        # ★ Номер текущей карты. В headless-PIE уровень открывается напрямую, менеджер загрузки не
        #   участвует, и `g_nCurMapId` остаётся −1 — тогда ветки «своя/чужая карта» не проверяются
        #   вовсе (и трекер, и автопуть молчат). Ставим 3 = «Альпия», это и есть открытый уровень.
        lambda: cmd("Asda.Load.CurrentMap 3"),
        # ★ Павн своего персонажа. Без него ни движение, ни автопуть, ни расстояние трекера
        #   проверить нечем: в headless-PIE сервера нет и пакет 4003 не приходит.
        #   Клетка (256,256) выбрана НЕ на глаз: проверена по DA_WalkGrid карты alpen —
#   `is_tile_walkable(256,256) = True` (а, например, (150,300) непроходима, и поиск пути
#   умирал на первом же узле — «развёрнуто=1»).
        # ★ Журнал наполняется РАНЬШЕ павна намеренно: именно в этом порядке приходят пакеты в
        #   игре (5047 в стартовом бурсте, 4003 — после загрузки уровня), и именно он вскрыл
        #   гонку ключа ini (заявка Е2). Восстановление списка обязано догнать имя само.
        lambda: cmd("Asda.QuestLog.FakeJournal 4 0 2026"),   # ячейка 0 = тот самый квест
        lambda: cmd("Asda.QuestLog.Tracked show"),           # Е2: ключа ещё нет — имени нет
        lambda: cmd("Asda.World.FakeSelfSpawn 256 256 %s" % CHAR_NAME),
        lambda: cmd("Asda.QuestLog.Tracked show"),           # Е2: ключ появился, список поднят САМ
        lambda: cmd("Asda.Quest.Tracker track 0"),           # ЖИВОЙ путь галки 743
        lambda: cmd("Asda.Quest.Tracker"),                   # запись обязана быть, строк 0, цель есть
        lambda: take_shot("tracker_noitem_quest"),
        lambda: harvest("tracker_noitem_quest"),
        # ключ отката к эталону: та же ячейка обязана получить ОТКАЗ
        lambda: cmd("Asda.Quest.Tracker clear"),
        lambda: cmd("Asda.Quest.TrackerNoItemQuests 0"),
        lambda: cmd("Asda.Quest.Tracker track 0"),
        lambda: cmd("Asda.Quest.Tracker"),                   # записей 0 — эталонный отказ
        lambda: cmd("Asda.Quest.TrackerNoItemQuests 1"),
        lambda: cmd("Asda.Quest.Tracker track 0"),

        # ── Д2: стрелка. Альфа печатается ЧИСЛАМИ в дампе выше; здесь проверяется КЛЮЧ ОТКАТА:
        #        при 0 обязана совпасть с эталонной, при 10 — быть на 25.5 больше.
        lambda: cmd("Asda.Quest.TrackerArrowAlphaBoost 0"),
        lambda: cmd("Asda.Quest.Tracker"),
        lambda: cmd("Asda.Quest.TrackerArrowAlphaBoost 10"),
        lambda: cmd("Asda.Quest.Tracker"),
        lambda: take_shot("tracker_arrow_alpha"),
        lambda: harvest("tracker_arrow_alpha"),

        # ── Д3: межкартовый автопуть. Мы на карте 3 (alpen). Заявка на карту 1 обязана повести
        #        персонажа к КЛЕТКЕ ПОРТАЛА 3→1 (462,346) из графа `kPortalGraph`, а не молчать.
        lambda: cmd("Asda.Map.Travel"),                      # состояние до
        lambda: cmd("Asda.Map.Travel 1 100 100"),            # цель на ЧУЖОЙ карте
        lambda: cmd("Asda.Map.Travel"),                      # шаг обязан быть посчитан
        lambda: cmd("Asda.Map.Travel cancel"),
        # путь, которого нет вовсе: у карты 19 в графе только ИСХОДЯЩЕЕ ребро — войти нельзя
        lambda: cmd("Asda.Map.Travel 19 100 100"),
        lambda: cmd("Asda.Map.Travel"),                      # заявка обязана сняться сама
        # ── и то же ЖИВЫМ путём — кнопками маршрута окна 700.
        #    У квеста 25 в QDT ДВЕ цели: NPC на карте 1 (чужая) и NPC на карте 3 (наша).
        lambda: cmd("Asda.QuestLog.Open 1"),
        lambda: cmd("Asda.QuestLog.Select 0"),
        lambda: cmd("Asda.QuestLog.Route"),                  # перечислить маршруты ячейки
        lambda: cmd("Asda.QuestLog.Route 0"),                # цель на ЧУЖОЙ карте → автопуть
        lambda: cmd("Asda.Map.Travel"),
        lambda: take_shot("route_crossmap"),
        lambda: harvest("route_crossmap"),
        lambda: cmd("Asda.QuestLog.Route 1"),                # цель на СВОЕЙ карте → идём ногами
        lambda: cmd("Asda.Map.Travel"),                      # заявки быть не должно
        lambda: cmd("Asda.QuestLog.Open 0"),

        # ══════════════════════════════════════════════════════════════════════════════════════
        # ★★★ ТРИ ЗАЯВКИ ВЛАДЕЛЬЦА 2026-08-23, ЗАХОД 2 (`Reports/quest_tracker_fix2.md`)
        # ══════════════════════════════════════════════════════════════════════════════════════

        # ── Е1: галка 743 НЕДОСТУПНА у задания, которое отслеживать нечем.
        #    Эпизод 2001 = книга `0_Qst_90` («Sowel Instruction»): все 10 под-записей `.QST` = −1
        #    И в `QDT` у квеста 0 нет ни одной строки с координатами — категория «ничего».
        lambda: cmd("Asda.Quest.Tracker clear"),
        lambda: cmd("Asda.QuestLog.FakeJournal 1 0 2001"),
        lambda: cmd("Asda.QuestLog.Open 1"),
        lambda: cmd("Asda.QuestLog.Select 0"),
        lambda: cmd("Asda.QuestLog.Dump"),                   # «галка 743: НЕДОСТУПНА» + сам виджет
        lambda: cmd("Asda.QuestLog.Track 1"),                # попытка поставить — обязан быть отказ
        lambda: cmd("Asda.Quest.Tracker"),                   # записей 0
        lambda: take_shot("track_checkbox_disabled"),
        lambda: harvest("track_checkbox_disabled"),
        # ключ отката к эталону: галка снова доступна («ставится и тут же гаснет»)
        lambda: cmd("Asda.Quest.TrackerLockUselessCheckbox 0"),
        lambda: cmd("Asda.QuestLog.Open 0"),
        lambda: cmd("Asda.QuestLog.Open 1"),                 # перерисовка окна
        lambda: cmd("Asda.QuestLog.Dump"),                   # чекбокс ДОСТУПЕН, прозрачность 1.00
        lambda: cmd("Asda.Quest.TrackerLockUselessCheckbox 1"),
        # …а у РАЗГОВОРНОГО задания С КООРДИНАТАМИ (квест 25) галка обязана быть доступна
        lambda: cmd("Asda.QuestLog.FakeJournal 4 0 2026"),
        lambda: cmd("Asda.QuestLog.Open 0"),
        lambda: cmd("Asda.QuestLog.Open 1"),
        lambda: cmd("Asda.QuestLog.Select 0"),
        lambda: cmd("Asda.QuestLog.Dump"),                   # «галка 743: ДОСТУПНА»
        lambda: cmd("Asda.QuestLog.Track 1"),
        lambda: cmd("Asda.Quest.Tracker"),                   # запись есть
        lambda: take_shot("track_checkbox_enabled"),
        lambda: harvest("track_checkbox_enabled"),

        # ── Е2: список отслеживаемых между сессиями. Показываем ВСЮ цепочку числами.
        lambda: cmd("Asda.QuestLog.Tracked show"),           # ключ, файл, что в файле, что в памяти
        lambda: dump_ini_section(),                          # ★ что РЕАЛЬНО лежит на диске
        lambda: cmd("Asda.Quest.Tracker clear"),             # «вышел из игры»
        lambda: cmd("Asda.Quest.Tracker"),                   # записей 0
        lambda: cmd("Asda.QuestLog.Tracked restore"),        # «зашёл снова»
        lambda: cmd("Asda.Quest.Tracker"),                   # записи вернулись
        lambda: cmd("Asda.QuestLog.Tracked show"),

        # ── Е3: кнопка 73404 панели быстрого доступа обязана ОТКРЫВАТЬ журнал.
        lambda: cmd("asda.SysMenu.Dump"),                    # геометрия/видимость ВСЕХ кнопок панели
        lambda: cmd("Asda.QuestLog.Open 0"),                 # журнал заведомо закрыт
        lambda: cmd("Asda.QuestLog.Dump"),
        lambda: cmd("asda.SysMenu.Click 73404"),             # ЖИВОЙ путь кнопки (события 67073)
        lambda: cmd("Asda.QuestLog.Dump"),                   # окно обязано быть ОТКРЫТО
        lambda: cmd("asda.SysMenu.Dump"),                    # кнопка обязана быть подсвечена
        lambda: take_shot("menu_button_quest_open"),
        lambda: harvest("menu_button_quest_open"),
        lambda: cmd("asda.SysMenu.Click 73404"),             # второй клик — закрыть
        lambda: cmd("Asda.QuestLog.Dump"),                   # окно обязано быть ЗАКРЫТО
        # ★ Охват не режем: остальные кнопки панели — тем же путём, каждая пишет, что сделала.
        lambda: cmd("asda.SysMenu.Click 73401"),
        lambda: cmd("asda.SysMenu.Click 73402"),
        lambda: cmd("asda.SysMenu.Click 73403"),
        lambda: cmd("asda.SysMenu.Click 73405"),
        lambda: cmd("asda.SysMenu.Click 73406"),
        lambda: cmd("asda.SysMenu.Click 73407"),
        lambda: cmd("asda.SysMenu.Click 73409"),
        lambda: cmd("asda.SysMenu.Click 73410"),
        lambda: cmd("asda.SysMenu.Dump"),
        lambda: take_shot("menu_all_buttons"),
        lambda: harvest("menu_all_buttons"),
    ]

    finish = PH.make_pie_finish(st, les, log, "=== готово, снимков запрошено %d ===", count_shots=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[трекер-живой] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            PLAN[st["step"]]()
            st["next_at"] = st["tick"] + (TRAVEL_SETTLE if st["step"] == 0 else SETTLE)
        except Exception:
            import traceback
            unreal.log_error("[трекер-живой] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


def _inside_unreal():
    try:
        import unreal  # noqa: F401
        return True
    except Exception:
        return False


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
#   ДВА РЕЖИМА ОДНОГО ФАЙЛА (второго инструмента не заводим):
#     · обычный python (из .bat, ДО старта редактора) — только кладёт в ini сохранённую позицию
#       окна 1800, чтобы следующий запуск проверил её восстановление;
#     · внутри редактора — сам живой прогон.
if __name__ == "__main__" or globals().get("__spec__") is None:
    if _inside_unreal():
        main()
    else:
        seed_window_offset()
        print("[трекер-живой] в ini положен сдвиг окна Quest_State_Win: 0,%.0f" % DRAG_DY)
        print("[трекер-живой] в ini положен список прошлой сессии: [AsdaQuestTracker] Tracked.%s=%s"
              % (CHAR_NAME, SEED_TRACKED))
