# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_target_ring_height.py — ЗАМЕР: где на самом деле стоит точка привязки кольца выбора.
#
# ЗАЧЕМ. Кольцо крепится режимом `EAsdaFxAttachMode::CharPos`, то есть каждый кадр встаёт в
#   `Host->GetActorLocation() + (0,0,подъём)`. Владелец говорит, что кольцо должно лежать «в
#   нижней части ног NPC». Вопрос, который нельзя решить рассуждением: совпадает ли
#   `GetActorLocation()` с ПОДОШВОЙ у NPC (корень = `USkeletalMeshComponent`), у моба (то же) и
#   у игрока (корень = пустой `USceneComponent`). Если начало координат меша не в ступнях,
#   подъём отсчитывается не от земли, и у высоких/низких моделей кольцо «плавает».
#
# ЧТО ДЕЛАЕТ. Открывает уровень карты, обходит запечённые `AsdaNpcActor`/`AsdaMobActor` и печатает
#   по каждому: позицию актора, низ/верх ВИДИМОЙ геометрии в мире и разницу «низ − Z актора».
#
# ★ МЕРЯЕТ ТЕМ ЖЕ ПРАВИЛОМ, ЧТО И КОД (`UAsdaWorldSubsystem::TargetSoleZ`): только ВИДИМЫЕ
#   меш-компоненты (скелетал + статик). Габариты АКТОРА целиком тут не годятся — в них входят
#   невидимые примитивы (капсула пикинга, виджет плашки имени, скрытая квестовая метка с
#   `bUsingAbsoluteRotation`), и они дают ложную просадку низа. Замер обязан спрашивать то же,
#   что и рантайм, иначе «до/после» мерит разные величины ([[false-negatives-method]]).
#
# ★ ОФЛАЙН ЗДЕСЬ ЗАКОНЕН: замеряются габариты МЕША и трансформ АКТОРА, а не геометрия эффекта
#   (её строит тик — для неё живая проверка, `asda_target_ring_live_check.py`).
#
# Запуск: run_headless.bat Tools\_probe_target_ring_height.py   (ASDA_MAP=alpen по умолчанию)
#   ASDA_RING_PROBE_LIMIT=<N> — сколько акторов каждого класса печатать (0 = все).
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os

MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
LEVEL = "/Game/Map/%s/%s" % (MAP_NAME, MAP_NAME)
LIMIT = int(os.environ.get("ASDA_RING_PROBE_LIMIT", "12"))


def log(m):
    import unreal
    unreal.log_warning("[замер-кольца] %s" % m)


def visible_mesh_span(a, unreal):
    """Низ и верх ВИДИМОЙ меш-геометрии актора в мире; (None, None) — видимых мешей нет."""
    lo, hi = None, None
    for c in a.get_components_by_class(unreal.MeshComponent):
        # ★ ВИДИМОСТЬ — через свойство `visible`: метода `is_visible()` в Python-обёртке нет,
        #   и молчаливый `except` из-за него отбрасывал ВСЕ меши («видимых мешей НЕТ» у 39 NPC,
        #   которые заведомо видны) — ложное «нет» на ровном месте ([[false-negatives-method]]).
        try:
            if not c.get_editor_property("visible"):
                continue
            # ★ Габариты — `SystemLibrary.get_component_bounds` (UFUNCTION поверх
            #   `Component->Bounds`). Свойства `bounds` в Python-обёртке НЕТ: это открытое поле
            #   C++, а не UPROPERTY, и обращение к нему валило разбор в `except`.
            o, e, _r = unreal.SystemLibrary.get_component_bounds(c)
        except Exception as ex:
            log("  %s: компонент %s пропущен: %s" % (a.get_actor_label(), c.get_name(), ex))
            continue
        if e.z <= 1.0:
            continue                       # вырожденные габариты в счёт не идут
        lo = (o.z - e.z) if lo is None else min(lo, o.z - e.z)
        hi = (o.z + e.z) if hi is None else max(hi, o.z + e.z)
    return lo, hi


def main():
    import unreal

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    log("уровень %s загружен" % LEVEL)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors = eas.get_all_level_actors()

    for cls_name in ("AsdaNpcActor", "AsdaMobActor"):
        picked = [a for a in all_actors if a.get_class().get_name() == cls_name]
        log("── %s: в уровне %d ──" % (cls_name, len(picked)))
        worst = 0.0
        best = None
        shown = 0
        for a in picked:
            loc = a.get_actor_location()
            lo, hi = visible_mesh_span(a, unreal)
            if lo is None:
                log("%-24s Z=%8.1f  видимых мешей НЕТ" % (a.get_actor_label(), loc.z))
                continue
            drop = lo - loc.z
            worst = max(worst, -drop)
            best = drop if best is None else max(best, drop)
            if LIMIT == 0 or shown < LIMIT:
                log("%-24s Z=%8.1f  низ=%8.1f верх=%8.1f  рост=%6.1f  НИЗ−Z=%+7.1f  поз=(%.0f,%.0f)"
                    % (a.get_actor_label(), loc.z, lo, hi, hi - lo, drop, loc.x, loc.y))
                shown += 1
        if picked:
            log("── %s: максимальная просадка низа под корень %.1f, минимальная %.1f ──"
                % (cls_name, worst, -best if best is not None else 0.0))


try:
    import unreal as _u  # noqa: F401
    _DIRECT = (__name__ == "__main__" or __name__ == "builtins")
except Exception:
    _DIRECT = False

if _DIRECT:
    main()
