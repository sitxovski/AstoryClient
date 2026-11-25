# Copyright (c) 2026 @sitxovski (Discord)
# Точечный патч LoginCamera в /Game/Map/Login: снять 4:3-констрейн → 16:9 fill (вертикальный FOV 30°).
#   НЕ сносит прочие актёры (бэкдроп 697 шт остаётся). Запуск: run_headless.bat patch_login_camera_169.py
import unreal, math

MAP = "/Game/Map/Login"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'patch_login_camera_169.py'
    les.load_level(MAP)
    FOV = math.degrees(2.0*math.atan(math.tan(math.radians(15.0))*16.0/9.0))  # hFOV для vFOV30 при 16:9 = 50.93°
    n = 0
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.CameraActor):
            cc = a.camera_component
            cc.set_editor_property("constrain_aspect_ratio", False)
            cc.set_editor_property("aspect_ratio", 16.0/9.0)
            cc.set_editor_property("field_of_view", FOV)
            n += 1
            unreal.log("[patch_cam] %s: constrain=False fov=%.2f" % (a.get_actor_label(), FOV))
    les.save_current_level()
    unreal.log("[patch_cam] патчено камер: %d, уровень сохранён" % n)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
