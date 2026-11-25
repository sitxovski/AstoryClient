# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_title_fx_live_check.py — ЖИВАЯ (PIE) проверка ДВУХ ЭФФЕКТОВ ЗВАНИЙ на персонаже и
#   БАННЕРА «ЗВАНИЕ ПОЛУЧЕНО». Разбор эталона — `Reports/title_system.md` §13.
#
# ★★★ ЗАЧЕМ ЖИВАЯ, А НЕ ОФЛАЙН-ПРОГОН (закон false-negatives-method). Эффект звания — это
#   `AAsdaEffectActor`, и садится он НА ПЕРСОНАЖА (`ResolveHost(userIdx)`). Без павна прогон
#   печатает «персонажа нет в мире» и даёт ложное «не работает»: ровно это показал первый заход
#   `-game /Game/Map/Login` (пакет 4003 офлайн не приходит, павна нет). Здесь павн поднимается
#   ТЕМ ЖЕ путём, что и в игре, — офлайн-крюком `Asda.World.FakeSelfSpawn`, который зовёт
#   `OnSelfSpawn()`, то есть точку прихода 4003. Своей ветки спавна нет.
#
# ★★★ ЖМЁМ ШТАТНЫЕ ОБРАБОТЧИКИ (закон diagnostic-must-use-real-path): `asda.Title.Sim secret|grant`
#   собирает НАСТОЯЩЕЕ тело пакета 6085/6065 и гонит его через `HandleFound`/`HandleGranted`.
#   Никаких «тестовых спавнеров эффекта» не заводится.
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт оставляет строку-доказательство в логе):
#   1. павн своего персонажа поднят (иначе проверка бессмысленна и это видно сразу);
#   2. 6085 «секрет открыт» → в мире появляется `AAsdaEffectActor` с `EffectName == ex_mark_003`,
#      он ПРИВЯЗАН к павну (`FollowActor` = павн, `bFollowCharWorld` = attach-6) и стоит в его точке;
#   3. эффект РАЗОВЫЙ — спустя его срок жизни в мире его больше нет;
#   4. 6065 «звание получено» → эффект `0018` тем же порядком + БАННЕР типа 2 в логе HUD;
#   5. `asda.Title.CharFx 0` — эффект НЕ спавнится (ключ отката работает);
#   6. значок HUD после обоих пакетов: `активен=1 иконка=1` (50002 «найдено»), подсказка 1 и 0.
#
# ЗАПУСК:  Tools\run_headless_title_fx_live.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/title_fx_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 600     # переезд уровня: стриминг + BeginPlay мира
SPAWN_SETTLE = 200      # павну нужно несколько кадров (меш/скин/скелет)
SETTLE = 60             # эффекту нужен тик, чтобы построить геометрию
LATE_MARK = 400         # «сильно позже»: доказать, что разовый эффект ДОГОРЕЛ
MAX_TICKS = 60000


def log(m):
    import unreal
    unreal.log("[звания-эффекты] " + str(m))


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
    try:
        ps = unreal.get_default_object(unreal.LevelEditorPlaySettings)
        ps.set_editor_property("new_window_width", 1600)
        ps.set_editor_property("new_window_height", 1000)
        ps.set_editor_property("center_new_window", True)
    except Exception as e:
        log("не удалось задать размер PIE-окна: %s" % e)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    def mark(t):
        log("═══════════ %s ═══════════" % t)

    def self_pawn():
        """Павн своего персонажа — тот самый `LocalPlayerActor` подсистемы мира."""
        w = world()
        if not w:
            return None
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaPlayerCharacter):
            return a
        return None

    def fx_actors(name=None):
        """Живые эффект-акторы мира; при `name` — только с таким `EffectName`."""
        w = world()
        if not w:
            return []
        out = []
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaEffectActor):
            try:
                en = str(a.get_editor_property("effect_name"))
            except Exception:
                en = "?"
            if name is None or en == name:
                out.append((a, en))
        return out

    def report_pawn():
        p = self_pawn()
        if not p:
            log("★ ПАВН НЕ СОЗДАН — дальше проверка бессмысленна (см. шапку файла)")
            return
        loc = p.get_actor_location()
        log("павн своего персонажа: %s, позиция (%.1f, %.1f, %.1f)"
            % (p.get_name(), loc.x, loc.y, loc.z))

    def report_fx(name, expect):
        """Напечатать ФАКТ: есть ли эффект `name`, к чему привязан, где стоит."""
        p = self_pawn()
        found = fx_actors(name)
        if not found:
            log("эффект «%s»: В МИРЕ НЕТ%s" % (name, "  ← ОЖИДАЛСЯ" if expect else "  (и не ожидался)"))
            return
        for a, en in found:
            loc = a.get_actor_location()
            try:
                fa = a.get_editor_property("follow_actor")
                # ★ Имя свойства в Python — БЕЗ венгерского «b»: UPROPERTY `bFollowCharWorld`
                #   отдаётся как `follow_char_world` (первый заход читал `b_follow_char_world`
                #   и падал с «Failed to find property»).
                cw = bool(a.get_editor_property("follow_char_world"))
                ems = a.get_editor_property("emitters")
                ne = len(ems) if ems is not None else -1
            except Exception as e:
                fa, cw, ne = None, False, -1
                log("  чтение свойств не удалось: %s" % e)
            same = (p is not None and fa is not None and fa.get_name() == p.get_name())
            d = -1.0
            if p is not None:
                pl = p.get_actor_location()
                d = ((loc.x - pl.x) ** 2 + (loc.y - pl.y) ** 2 + (loc.z - pl.z) ** 2) ** 0.5
            log("эффект «%s»: ЕСТЬ | эмиттеров %d | носитель=%s (это павн: %s) | attach-6 (bFollowCharWorld)=%s "
                "| позиция (%.1f, %.1f, %.1f), расстояние до павна %.1f%s"
                % (en, ne, fa.get_name() if fa else "НЕТ", "ДА" if same else "НЕТ",
                   "ДА" if cw else "НЕТ", loc.x, loc.y, loc.z, d,
                   "" if expect else "  ← НЕ ОЖИДАЛСЯ"))

    PLAN = [
        # ★ Без `?game=/Script/AsdaReborn.AsdaWorldGameMode` подсистемы мира не поднимаются.
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),

        (lambda: mark("1. ПАВН СВОЕГО ПЕРСОНАЖА (офлайн-крюк = точка прихода 4003)"), 2),
        # ★ БЕЗ ЭТОЙ СТРОКИ ПАВНА НЕ БУДЕТ. `OnSelfSpawn` пускает спавн только при
        #   `LM->CurrentMapId >= 0`; офлайн менеджер загрузки поля не заполняет (сервера нет), и
        #   спавн вечно «отложен до PostLoadMap». Диагностический крюк ставит ТО ЖЕ поле.
        #   3 = alpen (`DT_MapTable`, `g_aMapDirNames`) — тот уровень, что открыт выше.
        (lambda: cmd("Asda.Load.CurrentMap 3"), 10),
        (lambda: cmd("Asda.World.FakeSelfSpawn 256 256 TitleFxProbe"), SPAWN_SETTLE),
        (lambda: report_pawn(), 20),

        (lambda: mark("2. ПАКЕТ 6085 «секрет звания открыт» → ex_mark_003 (восклицательный знак)"), 2),
        (lambda: cmd("asda.Title.Sim secret 65"), SETTLE),
        (lambda: report_fx("ex_mark_003", True), 10),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("shot showui"), 60),

        # ★ Срок жизни актора = САМЫЙ ДОЛГИЙ эмиттер файла (`NaturalLifeSpan`), у ex_mark_003 это
        #   2.78 с (2.12 с жизнь меша + стартовая задержка). Поэтому сперва фиксируем «ещё жив»,
        #   и только ПОСЛЕ выдержки — «догорел». Иначе снимок берётся слишком рано и harness
        #   сам себе печатает ложное «НЕ ОЖИДАЛСЯ».
        (lambda: mark("3. РАЗОВЫЙ: пока не вышел срок жизни (2.78 с) — эффект в мире ЕСТЬ"), 2),
        (lambda: report_fx("ex_mark_003", True), LATE_MARK),
        (lambda: mark("3а. ПОСЛЕ срока жизни эффекта в мире быть НЕ должно"), 2),
        (lambda: report_fx("ex_mark_003", False), 20),

        (lambda: mark("4. ПАКЕТ 6065 «звание получено» → 0018 (шапка) + БАННЕР типа 2"), 2),
        (lambda: cmd("asda.Title.Sim grant 66"), SETTLE),
        (lambda: report_fx("0018", True), 10),
        (lambda: cmd("asda.Title.State"), 20),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("5. КЛЮЧ ОТКАТА asda.Title.CharFx 0 — эффекта быть НЕ должно"), 2),
        (lambda: cmd("asda.Title.CharFx 0"), 10),
        (lambda: cmd("asda.Title.Sim secret 67"), SETTLE),
        (lambda: report_fx("ex_mark_003", False), 20),
        (lambda: cmd("asda.Title.CharFx 1"), 10),

        # ★★★ 2026-08-30, замечание владельца «иконка ничего не делает».
        (lambda: mark("6. ЗНАЧОК HUD: ФОН ОБЯЗАН ЛОВИТЬ МЫШЬ (видимость Visible + ненулевой прямоугольник)"), 2),
        (lambda: cmd("asda.Title.Sim secret 65"), SETTLE),
        # ★ Живая геометрия готова только со следующего кадра — дампим ПОСЛЕ выдержки, иначе
        #   прямоугольник будет 0x0 и проверка «подтвердит» пустыми числами.
        (lambda: cmd("asda.Title.Dump"), 60),

        (lambda: mark("7. КЛИК ПО ЗНАЧКУ: окно открывается, значок гаснет, маркер строки ОСТАЁТСЯ"), 2),
        (lambda: cmd("asda.Title.Click notice"), SETTLE),
        (lambda: cmd("asda.Title.State"), 30),
        (lambda: cmd("asda.Title.List"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("8. ТОЛЬКО МАСКА 6066 (без опкода) — эффекта и значка быть НЕ должно"), 2),
        (lambda: cmd("asda.Title.Open"), SETTLE),          # закрыть окно тогглом
        (lambda: cmd("asda.Title.Sim owned 40"), SETTLE),
        (lambda: report_fx("0018", False), 10),
        (lambda: report_fx("ex_mark_003", False), 10),
        (lambda: cmd("asda.Title.State"), 30),

        (lambda: mark("9. ТОЛЬКО МАСКА 6086 (без опкода) — эффекта и значка быть НЕ должно"), 2),
        (lambda: cmd("asda.Title.Sim found 40"), SETTLE),
        (lambda: report_fx("ex_mark_003", False), 10),
        (lambda: cmd("asda.Title.State"), 30),

        (lambda: mark("10. ПРОВЕРКА ЗАКОНЧЕНА"), 2),
    ]

    def collect_shots():
        if not os.path.isdir(SS_DIR):
            return
        for root, _dirs, files in os.walk(SS_DIR):
            for f in files:
                if f.lower().endswith(".png"):
                    shutil.copy2(os.path.join(root, f), os.path.join(OUT_DIR, f))

    def tick(_dt):
        st["tick"] += 1
        if st["busy"]:
            return
        if st["tick"] < st["next_at"]:
            return
        st["step"] += 1
        if st["step"] >= len(PLAN):
            st["busy"] = True
            collect_shots()
            log("кадры: %s" % OUT_DIR)
            unreal.unregister_slate_post_tick_callback(st["handle"])
            les.editor_request_end_play()
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        fn, wait = PLAN[st["step"]]
        try:
            fn()
        except Exception as e:
            log("шаг %d упал: %s" % (st["step"], e))
        st["next_at"] = st["tick"] + wait
        if st["tick"] > MAX_TICKS:
            st["busy"] = True
            unreal.unregister_slate_post_tick_callback(st["handle"])
            les.editor_request_end_play()

    st["handle"] = unreal.register_slate_post_tick_callback(tick)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
