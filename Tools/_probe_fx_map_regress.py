# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_fx_map_regress.py — РЕГРЕСС-СНИМОК ЭФФЕКТОВ КАРТЫ живым путём игрока (-game).
#   Печатает `asda.Fx.SectionDump` (мировой центроид/радиус/число вершин секции каждого эмиттера всех
#   эффектов карты). Разбор лога, сравнение с эталоном и вердикт — снаружи UE, в `asda_fx_map_regress.py`
#   (он же запускает эту пробу; напрямую её запускать не нужно). Ничего не меняет в ассетах и не сохраняет.
#
# ★★★ ПОЧЕМУ ПРОБА БЫЛА СЛЕПА (разбор 27.09.2026, волна 0 графики, Reports/owner_0927/gfx_w0.md).
#   До 27.09 проба звала SectionDump через 3 с после старта с того места, где стоит камера игрока.
#   Все эффекты дальше `CullDistance` (10 000 юн) от камеры рантайм ЧИСТИТ (AsdaEffectActor.cpp, дистанц-кулл
#   в SimulateStep), а вне конуса камеры гейт значимости (`asda.Vis.Effects`) их ЗАМОРАЖИВАЕТ, не построив
#   ни разу. Отсюда «секции нет» у всех 175 эмиттеров в `Reports/fx_skill/regr_map_alpen.log` —
#   проба мерила пустоту и молча рапортовала «снято». Лечение (только в пробе, игра не меняется):
#     • `asda.Vis.Effects 0` — гейт значимости выключен на время пробы;
#     • `cull_distance = 1e9` у каждого AAsdaEffectActor мира — дистанц-кулл не срабатывает;
#     • отсчёт по КАДРАМ, а не по секундам, при фиксированном шаге времени (`-UseFixedTimeStep -FPS=60`,
#       ставит обёртка): фаза анимации эффекта в момент снимка одинакова от прогона к прогону.
#
# КОНТРОЛЬНАЯ ПОРЧА (закон [[pixel-metric-needs-control-pairs]]): ASDA_FXMAPREG_CONTROL=1 —
#   в ЖИВОМ мире -game (не в ассете, сохранения нет) у цели (ASDA_FXMAPREG_CONTROL_FX, иначе фонарь alpen
#   `eff_alpenredlight_a_00`, иначе первый актор с кольцом/квадом) всем узлам колец/квадов rotY += 90°. Прогон обязан дать «РЕГРЕСС» по этому актору; если «ЧИСТО» — проба снова слепа.
#
# Переменные окружения:
#   ASDA_FXMAPREG_FILTER  — фильтр SectionDump (часть имени), по умолчанию все эффекты карты;
#   ASDA_FXMAPREG_FRAME   — кадр снимка (умолчание 180 = 3 с при 60 кадр/с);
#   ASDA_FXMAPREG_CONTROL — 1 = контрольная порча (см. выше).
import os


def main():
    import unreal
    st = {"n": 0, "w": None, "q": []}
    SNAP_FRAME = int(os.environ.get("ASDA_FXMAPREG_FRAME", "180"))
    CONTROL = os.environ.get("ASDA_FXMAPREG_CONTROL", "") == "1"

    def log(m):
        unreal.log("[fxmapreg] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def at_frame(n, fn):
        st["q"].append([int(n), fn])
        st["q"].sort(key=lambda x: x[0])

    def run_queue():
        while st["q"] and st["n"] >= st["q"][0][0]:
            st["q"].pop(0)[1]()

    # ★ В -game у python-обёрток НЕТ таблицы «python-имя → имя UE» для полей без BlueprintVisible
    #   (CullDistance, Emitters, bRing, Nodes, RingRot): `set_editor_property("cull_distance")` падает
    #   «Failed to find property» (проверено 27.09), а в редакторе то же имя работает. Имя UE находится
    #   напрямую (FindPropertyByName) в обоих режимах — поэтому сначала имя UE, затем python-имя.
    def gp(o, ue, py):
        try:
            return o.get_editor_property(ue)
        except Exception:
            return o.get_editor_property(py)

    def sp(o, ue, py, v):
        try:
            o.set_editor_property(ue, v)
        except Exception:
            o.set_editor_property(py, v)

    def effect_actors():
        return list(unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaEffectActor))

    def uncull():
        # Дистанц-кулл выключается у КАЖДОГО эффекта мира: иначе снимаются только эффекты рядом с камерой
        n = 0; fail = 0
        for a in effect_actors():
            try:
                sp(a, "CullDistance", "cull_distance", 1.0e9); n += 1
            except Exception as ex:
                fail += 1
                if fail <= 3:
                    log("cull_distance не выставлен у %s: %s" % (a.get_name(), ex))
        log("дистанц-кулл снят: акторов %d, отказов %d" % (n, fail))

    def corrupt():
        # Контрольная порча ЖИВОГО актора (не ассета). Цель: ASDA_FXMAPREG_CONTROL_FX (часть имени .jeb), иначе
        # фонарь alpen `eff_alpenredlight_a_00`, иначе ПЕРВЫЙ по имени объекта актор с кольцом 0xE или квадом 0xB —
        # так порча есть на любой карте. Всем узлам колец/квадов цели rotY += 90°.
        want = os.environ.get("ASDA_FXMAPREG_CONTROL_FX", "") or "eff_alpenredlight_a_00"
        acts = sorted(effect_actors(), key=lambda x: x.get_name())

        def rq(em):
            return gp(em, "bRing", "ring") or gp(em, "bSingleQuad", "single_quad")
        tgt = [x for x in acts if want in str(gp(x, "EffectName", "effect_name"))]
        if not tgt:
            tgt = [x for x in acts if any(rq(em) for em in gp(x, "Emitters", "emitters"))][:1]
        for a in tgt:
            out = []
            for em in gp(a, "Emitters", "emitters"):
                if rq(em):
                    nn = []
                    for nd in gp(em, "Nodes", "nodes"):
                        r = gp(nd, "RingRot", "ring_rot")
                        sp(nd, "RingRot", "ring_rot", unreal.Vector(r.x, r.y + 90.0, r.z)); nn.append(nd)
                    sp(em, "Nodes", "nodes", nn)
                out.append(em)
            sp(a, "Emitters", "emitters", out)
            log("КОНТРОЛЬ: %s (%s) — кольцам/квадам rotY += 90°" % (gp(a, "EffectName", "effect_name"), a.get_name()))
        log("КОНТРОЛЬ: испорчено акторов %d" % len(tgt))

    def geom_dump():
        # ★ Дополнение к SectionDump (27.09.2026): центроид и радиус НЕ видят поворот симметричной секции вокруг
        #   своего центра (квад 0xB, повёрнутый на месте, даёт те же числа). Поэтому по каждой секции PMC ещё
        #   печатаются ПЕРВАЯ ВЕРШИНА (порядок вершин детерминирован) и габариты AABB — в координатах компонента.
        n = 0
        for a in effect_actors():
            pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
            if pmc is None:
                continue
            ems = gp(a, "Emitters", "emitters")
            for i in range(len(ems)):
                try:
                    V = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)[0]
                except Exception:
                    continue
                if not V:
                    continue
                lo = [1e18] * 3; hi = [-1e18] * 3
                for v in V:
                    for j, x in enumerate((v.x, v.y, v.z)):
                        lo[j] = min(lo[j], x); hi[j] = max(hi[j], x)
                unreal.log("[fxmapreg-sec] %s %d n=%d v0=%.2f,%.2f,%.2f ext=%.2f,%.2f,%.2f" % (
                    a.get_name(), i, len(V), V[0].x, V[0].y, V[0].z, hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))
                n += 1
        log("геометрия секций напечатана: %d" % n)

    def snap():
        cc("asda.Fx.SectionDump %s" % os.environ.get("ASDA_FXMAPREG_FILTER", ""))
        geom_dump()   # тот же кадр, что и SectionDump

    def start():
        w = None
        for o in unreal.ObjectIterator(unreal.World):
            if o.get_name() not in ("Untitled", "None"):
                w = o
        st["w"] = w
        log("мир: %s, кадр снимка %d, контроль %s" % (w.get_name() if w else None, SNAP_FRAME, CONTROL))
        cc("t.MaxFPS 60")
        cc("asda.Vis.Effects 0")
        uncull()
        if CONTROL:
            corrupt()
        # повторно на случай акторов, появившихся после первого кадра (спавн по таймеру/стримингу)
        at_frame(30, uncull)
        at_frame(SNAP_FRAME, snap)
        at_frame(SNAP_FRAME + 10, done)

    def done():
        log("РЕГРЕСС-КАРТА ЗАВЕРШЕНО")
        at_frame(st["n"] + 60, lambda: cc("quit"))

    def tick(dt):
        st["n"] += 1
        if st["n"] == 1:
            start()
            return
        if st["w"] is None:
            return
        try:
            run_queue()
        except Exception as ex:
            log("ОШИБКА: %s" % ex)

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
