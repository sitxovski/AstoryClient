# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_exchange_live_check.py — ★ ЖИВАЯ (PIE) проверка окна АУКЦИОНА `EXCHANGE_WIN` (13000).
#
#   Офлайн-дамп ассета показывает только разметку. Реально ли поднялась подсистема, связались ли
#   контролы, какая панель показана, где легли клик-зоны и что написано на вкладках — видно только
#   в запущенном мире (закон `false-negatives-method`).
#   Прогон БЕЗ СЕРВЕРА: сетки будут пустыми (данных инвентаря нет), но связывание, видимость
#   панелей, подписи вкладок и клик-зоны проверяются полностью.
#
# ЗАПУСК: UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=".../asda_exchange_live_check.py"
#   -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput -abslog=<лог>
import unreal

LEVEL = "/Game/Map/alpen/alpen"
WARMUP = 300
STEP_GAP = 120
MAX_TICKS = 40000

# (команда, комментарий)
SCRIPT = [
    ("asda.Exchange.Open 5", "режим 5 — поиск, набор RADIOBTN1 (8 вкладок)"),
    ("asda.Exchange.Widgets", ""),
    ("asda.Exchange.Cat 5 3", "★ переключить вкладку 13408 на индекс 3 (проверка подсветки)"),
    ("asda.Exchange.Widgets", ""),
    ("asda.Exchange.FakeLot 1 999999", "★ подать синтетический лот в выдачу (проверка иконки строки)"),
    ("asda.Exchange.Widgets", ""),
    ("asda.Exchange.Open 0", "закрыть"),
    ("asda.Exchange.Open 4", "режим 4 — поиск, набор RADIOBTN2 (7 вкладок)"),
    ("asda.Exchange.Widgets", "★ РЕЖИМ 4, активна вкладка 0 «Кольцо» — жалоба «у первой нет надписи»"),
    ("asda.Exchange.Cat 4 6", "★ вкладка «Прочее» (индекс 6)"),
    ("asda.Exchange.Widgets", "★ снимок СРАЗУ после переключения"),
    ("asda.Exchange.Widgets", "★ снимок ЧЕРЕЗ ШАГ — значения обязаны совпасть (нет промежуточного кадра)"),
    ("asda.Exchange.Cat 4 0", "★ вернуть вкладку 0"),
    ("asda.Exchange.Widgets", ""),
    ("asda.Exchange.Open 5", "★★ СМЕНА РЕЖИМА ПРИ ОТКРЫТОМ ОКНЕ 4→5 — обязана пройти БЕЗ нотиса 12555"),
    ("asda.Exchange.Widgets", ""),
    ("asda.Exchange.Open 0", "закрыть"),
    ("asda.Exchange.Open 1", "режим 1 — выставить лот из ИНВЕНТАРЯ (контейнер 2)"),
    ("asda.Exchange.Widgets", ""),
    ("asda.Exchange.Open 3", "★ режим 3 «мои лоты» — панели обязаны погаснуть"),
    ("asda.Exchange.Widgets", ""),
    ("asda.Exchange.Open 0", "закрыть"),
]


def log(m):
    unreal.log("[аукцион-живой] %s" % m)


def main():
    for m in ("UMG", "UMGEditor"):
        try:
            unreal.load_module(m)
        except Exception as e:                               # noqa: BLE001
            unreal.log_warning("[аукцион-живой] load_module(%s): %s" % (m, e))

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None}

    def tick(_dt):
        st["tick"] += 1
        if st["tick"] < st["next_at"]:
            if st["tick"] > MAX_TICKS:
                unreal.unregister_slate_post_tick_callback(st["handle"])
                les.editor_request_end_play()
            return
        w = ues.get_game_world()
        if not w:
            st["next_at"] = st["tick"] + 60
            return
        st["step"] += 1
        if st["step"] >= len(SCRIPT):
            unreal.unregister_slate_post_tick_callback(st["handle"])
            log("ГОТОВО")
            les.editor_request_end_play()
            return
        cmd, note = SCRIPT[st["step"]]
        if note:
            log("── %s ──" % note)
        unreal.SystemLibrary.execute_console_command(w, cmd)
        st["next_at"] = st["tick"] + STEP_GAP

    st["handle"] = unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__":
    main()
