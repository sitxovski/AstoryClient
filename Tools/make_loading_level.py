# Copyright (c) 2026 @sitxovski (Discord)
# Выделенный УРОВЕНЬ загрузки L_Loading (пустой): GO→OpenLevel(L_Loading) выгружает login,
#   на нём субсистема показывает картинку+бар и async-грузит целевую карту → OpenLevel(target).
#   Запуск: run_headless.bat make_loading_level.py
import unreal

path = "/Game/Map/L_Loading"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log("[loading_level] L_Loading уже существует")
    else:
        ok = les.new_level(path)   # создаёт пустой уровень, делает текущим
        if ok:
            # ★★★ 16.09.2026 (обратный обход ревизии savedir12; поправлено в тот же день).
            #   Опасность тут не в имени — путь фиксированный, чужое не подставить. Опасность в том, что
            #   save_current_level() пишет ТЕКУЩИЙ уровень: если редактор по какой-то причине остался на
            #   стартовой карте (в headless-сессии это Login), запись уйдёт в сцену владельца.
            #   ПОЭТОМУ спрашиваем ФАКТИЧЕСКИЙ текущий уровень, а не константу — прежняя проверка стояла
            #   на `path` да ещё и внутри ветки «создание удалось», то есть закрывала несуществующий случай.
            import asda_login_guard as _LG
            _cur = ""
            try:
                _w = unreal.EditorLevelLibrary.get_editor_world()
                _cur = _w.get_path_name() if _w else ""
            except Exception:
                _cur = ""
            # ★★★ 16.09.2026 (КРИТ ревизии волны гейтов): FAIL-CLOSED. Прежнее `if _cur and …` было fail-open:
            #   при недоступном мире `_cur` пуст, проверка пропускалась, и запись уходила в ТЕКУЩИЙ уровень —
            #   в headless-сессии это Login. Не смогли узнать, где мы, — не пишем (правило то же, что у
            #   save_dir_login_safe: неизвестность трактуется в пользу защиты).
            if not _cur:
                unreal.log_error("[LOGIN-GUARD] make_loading_level.py ОСТАНОВЛЕН: не удалось определить текущий "
                                 "уровень редактора, а save_current_level() пишет именно его. В автоматической "
                                 "сессии текущим может быть Login со светом владельца.")
                raise SystemExit(3)
            if _LG.login_map_write_blocked(_cur, "make_loading_level.py"):
                raise SystemExit(3)
            les.save_current_level()
            unreal.EditorAssetLibrary.save_asset(path)
            unreal.log("[loading_level] L_Loading создан")
        else:
            unreal.log_warning("[loading_level] L_Loading НЕ создан (new_level=false)")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
