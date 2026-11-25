# Copyright (c) 2026 @sitxovski (Discord)
# _fix_rainbow_live_attach.py — live-attach радуг к анимации дирижаблей (2026-07-31).
# Канон (эталон 0x1215C10): N520 = ЖИВАЯ кость×группа — порт: follow-механика AAsdaEffectActor
# (bFollowPosition: SetActorLocation(socket); bFollowRotation: (SockNow·Bind⁻¹)·Authored — дельта живой
# кости поверх запечённой группы). Парентинг снимается (закон attach-архитектуры: НЕ парентинг).
# t=0 инвариант: relative loc = 0 (socketLoc==actorLoc, замер headless_airship_amp.log), SockNow==Bind.
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

TARGETS = ("MapEffect_eff_rainbow_a_00_01", "MapEffect_eff_rainbow_a_00_02",
           "MapEffect_eff_rainbow_a_00_03", "MapEffect_eff_rainbow2_a_00_01")


def log(m): unreal.log_warning("[liveAtt] %s" % m)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    les.load_level("/Game/Map/alpen/alpen")
    fixed = 0
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.AsdaEffectActor): continue
        lbl = a.get_actor_label()
        if lbl not in TARGETS: continue
        before = a.get_actor_transform()
        a.detach_from_actor(unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD,
                            unreal.DetachmentRule.KEEP_WORLD)
        a.set_editor_property("follow_position", True)
        a.set_editor_property("follow_rotation", True)
        after = a.get_actor_transform()
        d = (after.translation - before.translation).length()
        log("%s: detach+follow ON; сдвиг трансформа при детаче = %.4f юн" % (lbl, d))
        fixed += 1
    if fixed == 4:
        ok = les.save_current_level()
        log("4/4, save=%s" % ok)
    else:
        log("ОШИБКА: %d из 4 — НЕ сохраняю" % fixed)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
