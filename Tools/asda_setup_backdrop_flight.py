# Copyright (c) 2026 @sitxovski (Discord)
# ★★★ 13.09.2026: ПО УМОЛЧАНИЮ ЛЕТИТ КАМЕРА, а не фон (решение владельца; см. asda_build_login_flight_sequence.py).
#   В режиме camera LoginBackdrop_Flight НИКТО не поссессит — он стоит в конечной позе перелёта (тождество),
#   и этот тул ставит его в трансформ якоря (тоже тождество), то есть схему не ломает. Поссессит его только
#   откатная секвенция LS_LoginFlight_SceneMove (ASDA_LOGIN_FLIGHT_MODE=scene). Описание ниже — историческое.
# Схема «фон летит + ручная правка держится»:
#   LoginBackdrop_Flight  (ПОССЕССИТ секвенция перелёта — двигает весь фон, как раньше)
#     └ LoginBackdrop_Anchor  (РУЧНОЙ корень дизайнера; секвенция НЕ трогает → правки держатся)
#         └ 697 BackdropElem_*
# Flight создаётся в текущем мировом трансформе якоря (key0, -1,1,1); якорь аттачится KEEP_WORLD
# (его relative становится identity → визуально ничего не меняется, но появляется свободный knob).
import unreal
LEVEL = "/Game/Map/Login"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_setup_backdrop_flight.py'
    les.load_level(LEVEL)
    anchor = next((a for a in eas.get_all_level_actors() if a.get_actor_label()=="LoginBackdrop_Anchor"), None)
    if not anchor:
        unreal.log_warning("LoginBackdrop_Anchor не найден"); raise SystemExit
    flight = next((a for a in eas.get_all_level_actors() if a.get_actor_label()=="LoginBackdrop_Flight"), None)
    axf = anchor.get_actor_transform()
    if not flight:
        flight = eas.spawn_actor_from_class(unreal.StaticMeshActor, axf.translation)
        flight.set_actor_transform(axf, False, False)
        flight.set_actor_label("LoginBackdrop_Flight")
        flight.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(None)
        try: flight.set_folder_path("LoginBackdrop")
        except Exception: pass
        unreal.log("создан LoginBackdrop_Flight в %s scale=%s" % (axf.translation, axf.scale3d))
    else:
        flight.set_actor_transform(axf, False, False)
        unreal.log("LoginBackdrop_Flight уже есть — трансформ обновлён")
    # аттач якоря к Flight с сохранением мира (relative → identity)
    anchor.attach_to_actor(flight, "", unreal.AttachmentRule.KEEP_WORLD,
                           unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
    les.save_current_level()
    unreal.log("=== ГОТОВО: Flight→Anchor→элементы. Секвенция поссессит Flight, Anchor правится вручную ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
