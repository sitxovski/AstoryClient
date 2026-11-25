# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""_probe_step_sounds.py — почему не слышно шагов: смотрим сами звуковые ассеты.

Проверяет четыре звука шагов: существуют ли, какой длительности, не пустые ли, какая громкость
и класс звука. Молчание может быть не в коде, а в самом ассете — например нулевая длительность
после импорта или громкость 0.

Запуск: headless через Tools/run_headless_safe.py.
"""
import unreal


def log(m):
    unreal.log("[звук шагов] " + str(m))


def main():
    EAL = unreal.EditorAssetLibrary
    log("═══ звуки шагов ═══")
    for n in ("S_c_run0", "S_c_run1", "S_c_run2", "S_c_run3"):
        path = "/Game/AsdaAssets/Sound/%s.%s" % (n, n)
        if not EAL.does_asset_exist(path):
            log("  %-10s АССЕТА НЕТ" % n)
            continue
        a = EAL.load_asset(path)
        if not a:
            log("  %-10s не загрузился" % n)
            continue
        info = ["класс=%s" % a.get_class().get_name()]
        for prop, label in (("duration", "длительность"), ("volume", "громкость"),
                            ("pitch", "высота"), ("num_channels", "каналов"),
                            ("sample_rate", "частота")):
            try:
                info.append("%s=%s" % (label, a.get_editor_property(prop)))
            except Exception:
                pass
        try:
            sc = a.get_editor_property("sound_class_object")
            info.append("класс звука=%s" % (sc.get_name() if sc else "нет"))
        except Exception:
            pass
        log("  %-10s %s" % (n, ", ".join(info)))

    # Для сравнения — звук, который в игре точно слышно.
    for n in ("s_e_riding_vehicle",):
        path = "/Game/AsdaAssets/Sound/%s.%s" % (n, n)
        if EAL.does_asset_exist(path):
            a = EAL.load_asset(path)
            try:
                log("  (для сравнения) %-20s длительность=%s громкость=%s"
                    % (n, a.get_editor_property("duration"), a.get_editor_property("volume")))
            except Exception:
                log("  (для сравнения) %s — свойства не читаются" % n)

    log("ЗАВЕРШЕНО")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
