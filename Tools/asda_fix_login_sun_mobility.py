# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Login: Sun_Login STATIONARY -> MOVABLE (как на всех 23 игровых картах; статик-свет не билдится,
# запрет «Собрать свет»). Убирает unbuilt-preview поведение теней. Сохраняет карту.
import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_fix_login_sun_mobility.py'
    assert les.load_level("/Game/Map/Login"), "Login load failed"
    fixed = 0
    for a in eas.get_all_level_actors():
        if isinstance(a, (unreal.DirectionalLight, unreal.SkyLight)):
            comp = a.root_component
            if comp.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE:
                comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
                unreal.log("[fix_sun] %s -> MOVABLE" % a.get_actor_label()); fixed += 1
    if fixed:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log("[fix_sun] DONE fixed=%d" % fixed)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
