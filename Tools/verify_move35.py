# Copyright (c) 2026 @sitxovski (Discord)
# Верификация anim_35 (бег с крыльями): существует, длина >0, треков ~50, скелет верный.
import unreal
def log(m): unreal.log("[verify_move35] " + str(m))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for g in ("m", "f"):
        p = "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_%s_cremmy_preview/SkeletalMeshes/ch1_%s_cremmy_previewanim_35" % (g, g)
        a = unreal.load_asset(p)
        if not a:
            log("[%s] !! НЕТ ассета %s" % (g, p)); continue
        ln = float(a.get_editor_property("sequence_length"))
        sk = a.get_editor_property("skeleton")
        ntr = -1; names = []
        try:
            dm = a.get_data_model()
            log("[%s] data_model type=%s" % (g, type(dm).__name__))
            try: ntr = dm.get_num_bone_tracks()
            except Exception as e1:
                log("[%s] get_num_bone_tracks fail: %s" % (g, e1))
                try:
                    names = [str(x) for x in dm.get_bone_track_names()]
                    ntr = len(names)
                except Exception as e2: log("[%s] get_bone_track_names fail: %s" % (g, e2))
        except Exception as e:
            log("[%s] get_data_model fail: %s" % (g, e))
            log("[%s] dir: %s" % (g, [x for x in dir(a) if "model" in x.lower() or "track" in x.lower() or "curve" in x.lower()]))
        if ntr < 0:
            try:
                names = [str(x) for x in unreal.AnimationLibrary.get_animation_track_names(a)]
                ntr = len(names)
            except Exception as e: log("[%s] AnimationLibrary fail: %s" % (g, e))
        log("[%s] %s | len=%.3fс | скелет=%s | треков=%d%s" % (
            g, p, ln, sk.get_name() if sk else "NONE", ntr,
            (" | first5=%s" % names[:5]) if names else ""))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
