# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_partyfind_live_check.py — ЖИВАЯ (PIE) проверка ОКНА «ПОИСК ОТРЯДА» (`Find_Party_WIn` 8500).
#   Разбор эталона, таблица нотисов и раскладки пакетов — `Reports/party_find_8500.md`.
#
# ★★★ ЗАЧЕМ ЖИВАЯ, А НЕ ОФЛАЙН-ДАМП (закон false-negatives-method). Окно строится в РАНТАЙМЕ:
#   подписи перетираются строками таблицы текста, комбобоксы наполняются кодом, рамки выбора
#   ездят по живой геометрии, а сам пункт меню приходит из `DT_ExtraMenu`. Офлайн-дамп ассета
#   ничего этого не видит.
#
# ★★★ ЖМЁМ САМИ КНОПКИ (закон «диагностика обязана жать саму кнопку»):
#   `asda.ExtraMenu.MenuButton` → НАСТОЯЩАЯ кнопка трея 73409;
#   `asda.ExtraMenu.Click 2`    → тот же колбэк, что у строки доп-меню (запись «Поиск отряда»);
#   `asda.PartyFind.Click <id>` → `UButton::OnClicked.Broadcast()` живой кнопки окна 8500.
#   Состояние читаем дампами, которые смотрят на ВИДЖЕТЫ (видимость, текст, число узлов).
#
# ЧТО ПРОВЕРЯЕТСЯ (каждый пункт оставляет строку-доказательство в логе):
#   1. в `DT_ExtraMenu` 12 записей, строка №2 — «Поиск отряда», команда 7  — `asda.ExtraMenu.Dump`
#   2. клик по строке №2 ОТКРЫВАЕТ окно 8500                              — `asda.PartyFind.Dump`
#   3. окно разобрано: кнопки, 16 карт, 10+10 строк, 6 комбобоксов        — там же
#   4. кнопка «Обновить» 8654 шлёт 3414 + 3422                            — журнал сети
#   5. клик по карте 8664 ставит бит маски и пишет нотис в чат            — журнал
#   6. клик по карте 8688 «Логово Дракона» РАБОТАЕТ (в оригинале — нет)   — журнал
#   7. выбор строки списка двигает рамку (8 планов)                       — `asda.PartyFind.Row`
#   8. «Изменить» 8651 без лидерства — честный отказ в журнал             — журнал
#   9. «Отмена» 8653 закрывает окно и шлёт 3412 + 3420                    — `asda.PartyFind.Dump`
#  10. повторный клик по строке меню открывает окно снова (тоггл)         — `asda.PartyFind.Dump`
#  11. кадры окна
#
# ЗАПУСК:  Tools\run_headless_partyfind.bat
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/partyfind_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 40000


def log(m):
    import unreal
    unreal.log("[поиск-отряда-живое] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    # ★★★ ОКНО PIE ПОБОЛЬШЕ: окно 8500 в базе 1024×768 имеет размер 783×478, а вьюпорт PIE по
    #   умолчанию мал — нижние строки списков физически выходят за границу, Slate их отсекает,
    #   и прогон даёт ЛОЖНОЕ «строка не рисуется» (закон false-negatives-method).
    try:
        cls = unreal.find_object(None, "/Script/UnrealEd.LevelEditorPlaySettings")
        ps = unreal.get_default_object(cls)
        ps.set_editor_property("new_window_width", 1600)
        ps.set_editor_property("new_window_height", 1000)
        ps.set_editor_property("center_new_window", True)
        ps.set_editor_property("play_in_editor_start_mode",
                               unreal.PlayInEditorStartMode.NEW_EDITOR_WINDOW)
        log("размер окна PIE выставлен 1600x1000, режим — отдельное окно")
    except Exception as e:
        log("не удалось выставить размер окна PIE: %s" % e)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    log("уровень %s загружен" % LEVEL)
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

    # Масштаб игрового UMG-слоя. Класс `UserInterfaceSettings` в модуль `unreal` не экспонирован —
    #   берём CDO по пути объекта; имя свойства пробуем оба (у неэкспонированных классов
    #   «змеиного» псевдонима нет).
    def ui_scale(v):
        try:
            o = unreal.load_object(None, "/Script/Engine.Default__UserInterfaceSettings")
            for nm in ("ApplicationScale", "application_scale"):
                try:
                    o.set_editor_property(nm, float(v))
                    log("масштаб UMG выставлен %.2f (свойство %s)" % (v, nm))
                    return
                except Exception:
                    continue
            log("⚠ свойство масштаба UMG не найдено — кадр целиком не снять")
        except Exception as e:
            log("⚠ UserInterfaceSettings недоступны: %s" % e)

    PLAN = [
        # ★ Без `?game=/Script/AsdaReborn.AsdaWorldGameMode` подсистемы мира не поднимаются
        #   и прогон даёт ложное «ничего нет».
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),

        (lambda: mark("1. ДОП-МЕНЮ: в таблице должна быть 12-я запись «Поиск отряда» (команда 7)"), 2),
        (lambda: cmd("asda.ExtraMenu.MenuButton"), SETTLE),
        (lambda: cmd("asda.ExtraMenu.Dump"), 30),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("2. КЛИК ПО СТРОКЕ №2 ДОП-МЕНЮ — должно открыться окно 8500"), 2),
        (lambda: cmd("asda.ExtraMenu.Click 2"), SETTLE),
        (lambda: cmd("asda.PartyFind.Dump"), 40),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("3. КНОПКА «Обновить» 8654 — ждём 3414 + 3422 в журнале сети"), 2),
        (lambda: cmd("asda.PartyFind.Click 8654"), 40),

        (lambda: mark("4. КАРТА-ФИЛЬТР 8664 (первая обычная) — бит маски + нотис в чат"), 2),
        (lambda: cmd("asda.PartyFind.Click 8664"), 40),
        (lambda: cmd("asda.PartyFind.Dump"), 20),

        (lambda: mark("5. КАРТА 8688 «Логово Дракона» — в ОРИГИНАЛЕ не работала (дефект Д2)"), 2),
        (lambda: cmd("asda.PartyFind.Click 8688"), 40),
        (lambda: cmd("asda.PartyFind.Dump"), 20),
        (lambda: mark("5а. тот же клик при ЭТАЛОННОМ поведении — должен быть отброшен"), 2),
        (lambda: cmd("asda.PartyFind.LegacyMapRange 1"), 5),
        (lambda: cmd("asda.PartyFind.Click 8688"), 30),
        (lambda: cmd("asda.PartyFind.LegacyMapRange 0"), 5),

        (lambda: mark("6. ВЫБОР СТРОКИ списка — рамка из 8 планов обязана переехать"), 2),
        (lambda: cmd("asda.PartyFind.Row 0 3"), 30),
        (lambda: cmd("asda.PartyFind.Row 1 5"), 30),
        (lambda: cmd("asda.PartyFind.Dump"), 20),
        (lambda: cmd("shot showui"), 60),

        (lambda: mark("7. «Изменить» 8651 без лидерства — ждём честную строку в журнал"), 2),
        (lambda: cmd("asda.PartyFind.Click 8651"), 30),
        (lambda: mark("8. «Шепот» 8655 при выбранной строке"), 2),
        (lambda: cmd("asda.PartyFind.Click 8655"), 30),

        (lambda: mark("9. ЛИСТАНИЕ: 8573 вперёд, 8576 в начало"), 2),
        (lambda: cmd("asda.PartyFind.Click 8573"), 30),
        (lambda: cmd("asda.PartyFind.Click 8576"), 30),

        (lambda: mark("10. «Отмена» 8653 — окно закрыто, 3412 + 3420 ушли"), 2),
        (lambda: cmd("asda.PartyFind.Click 8653"), SETTLE),
        (lambda: cmd("asda.PartyFind.Dump"), 30),

        (lambda: mark("11. ПОВТОРНЫЙ клик по строке доп-меню — окно открывается снова"), 2),
        (lambda: cmd("asda.ExtraMenu.Click 2"), SETTLE),
        (lambda: cmd("asda.PartyFind.Dump"), 30),
        (lambda: mark("12. ТОГГЛ: ещё один клик по строке — окно сворачивается (нотис 11906)"), 2),
        (lambda: cmd("asda.ExtraMenu.Click 2"), SETTLE),
        (lambda: cmd("asda.PartyFind.Dump"), 30),
        (lambda: cmd("shot showui"), 60),

        # ★★★ КАДР ЦЕЛИКОМ. Игровой вьюпорт PIE редактора — ПАНЕЛЬ окна редактора, и ключи
        #   `-ResX/-ResY` его не меняют (проверено ранее заходом мультиквеста). Окно 8500 имеет
        #   размер 783×478 базовых юнитов и в панель ~700×230 не влезает — верх (фильтр карт) и
        #   низ (кнопки) обрезаются. Единственный работающий рычаг — `ApplicationScale` слоя UMG:
        #   он меняет размер КОРНЕВОЙ КАНВЫ в единицах Slate, то есть «разрешение» с точки зрения
        #   раскладки. Ставим 0.45, снимаем кадр целиком, возвращаем 1.0.
        (lambda: mark("13. КАДР ЦЕЛИКОМ: масштаб UMG 0.45, чтобы окно 783×478 влезло в панель PIE"), 2),
        (lambda: ui_scale(0.45), 60),
        (lambda: cmd("asda.ExtraMenu.Click 2"), SETTLE),
        (lambda: cmd("shot showui"), 90),
        (lambda: ui_scale(1.0), 30),
    ]

    def finish():
        try:
            src = SS_DIR
            if os.path.isdir(os.path.join(SS_DIR, "WindowsEditor")):
                src = os.path.join(SS_DIR, "WindowsEditor")
            for f in os.listdir(src):
                if f.lower().endswith(".png"):
                    shutil.copy2(os.path.join(src, f), os.path.join(OUT_DIR, f))
                    log("кадр сохранён: %s" % f)
        except Exception:
            pass
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        if st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        log("ЗАВЕРШЕНО")
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            fn, wait = PLAN[st["step"]]
            fn()
            st["next_at"] = st["tick"] + wait
        except Exception:
            import traceback
            unreal.log_error("[поиск-отряда-живое] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
