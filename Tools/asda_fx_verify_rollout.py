# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_verify_rollout.py — ПРОВЕРКА РАСКАТА волны Ф1: доехал ли множитель яркости до секции.
#   Ассеты НЕ трогает, MID НЕ создаёт — читает то, что стоит на UProceduralMeshComponent сейчас.
#
# ПОЧЕМУ ИМЕННО ТАК. `AAsdaEffectActor` при пересборке секции ставит на неё материал ИЗ ДАННЫХ
#   ЭМИТТЕРА (`SetMaterial`, AsdaEffectActor.cpp:1606 и 931/963/1195/1622), и делает это всякий раз,
#   когда число вершин секции разошлось с закоммиченным — в частности ПОСЛЕ дистанц-кулла
#   (секции чистятся дальше CullDistance, .cpp:729-733, и создаются заново на возврате).
#   Именно на этом сгорела волна Ф1: рантайм-MID снимался с секции, а лог рапортовал успех.
#   Поэтому проверка обязана быть «отъехал за отсечку — вернулся — прочитал С КОМПОНЕНТА».
#   Если яркость лежит в АССЕТЕ (MIC-варианте, на который перенацелен эмиттер), она цикл переживает.
#
# Переменные окружения:
#   ASDA_FXV_KEY   alpen | nightvalley        — карта (камеры берутся из JSON волны Ф1)
#   ASDA_FXV_CAMS  метки камер через запятую  — пусто = все камеры карты
#   ASDA_FXV_OUT   путь JSON с результатом
# Запуск:
#   python Tools/set0_run_game.py Tools/asda_fx_verify_rollout.py --map /Game/Map/alpen/alpen \
#          --env ASDA_FXV_KEY=alpen --marker "FX-VERIFY ЗАВЕРШЕНО"
import json
import os
import sys as _sys

_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ue_paths as _UP   # ★ дедуп №2 В1 Т14: путь уровня карты — общий level_path (без прошитых литералов)

KEY = os.environ.get("ASDA_FXV_KEY", "alpen")
WANT = [x.strip() for x in os.environ.get("ASDA_FXV_CAMS", "").split(",") if x.strip()]
OUT = os.environ.get("ASDA_FXV_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/fx_verify_%s.json" % KEY)
CAMS_IN = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/frames/fx_f1_%s.json" % KEY
MAPS = {k: _UP.level_path(k) for k in ("alpen", "nightvalley")}   # карта → путь уровня /Game/Map/<a>/<a>
WARMUP = 210          # кадров на прогрев после наводки камеры (как в оснастке Ф1)
AWAY = 30000.0        # отъезд заведомо больше CullDistance (10000)
SETTLE = 150          # кадров на отъезде и на возврате


def main():
    """Работа скрипта (закон tools-no-import-side-effects: импорт ничего не делает)."""
    import unreal
    MAP = MAPS[KEY]
    R = {"карта": KEY, "камеры": []}
    st = {"n": 0, "w": None, "q": [], "cams": [], "i": 0, "cam": None, "target": [], "rows1": []}

    def log(m):
        unreal.log("[fxVERIFY] " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    after = PH.make_after_frames(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)

    run_queue = PH.make_frame_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 8 стр.)

    find_world = PH.make_find_world(MAP)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 11 стр.)

    def read_boost(mat):
        """EmissiveBoost у КОНКРЕТНОГО экземпляра материала, стоящего на секции.

        ★ Грабля волны Ф1: чтение отсутствующего имени молча отдаёт 0.0 — поэтому рядом всегда
          печатается мастер и факт наличия имени в списке параметров мастера (`параметр_есть`)."""
        try:
            if isinstance(mat, unreal.MaterialInstanceDynamic):
                return float(mat.get_scalar_parameter_value(unreal.Name("EmissiveBoost")))
            return float(unreal.MaterialEditingLibrary
                         .get_material_instance_scalar_parameter_value(mat, unreal.Name("EmissiveBoost")))
        except Exception:
            return None

    def has_param(mat):
        """Есть ли имя EmissiveBoost у МАСТЕРА этого материала (проверка по списку имён)."""
        try:
            b = mat.get_base_material()
            names = [str(n) for n in unreal.MaterialEditingLibrary.get_scalar_parameter_names(b)]
            return "EmissiveBoost" in names
        except Exception:
            return None

    def master_of(mat):
        """Имя мастер-материала экземпляра."""
        try:
            b = mat.get_base_material()
            return b.get_name() if b else "?"
        except Exception:
            return "?"

    def effect_actors():
        """Все эффект-акторы текущего мира."""
        return unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaEffectActor)

    def jeb_of(a):
        """Имя .jeb актора. ★ В режиме ИГРОКА (-game) `get_editor_property('effect_name')` бросает
        «Failed to find property» (в редакторе тот же вызов работает — проверено генератором
        раската), поэтому доступ к полю пробуется несколькими способами, а последний рубеж —
        имя класса/актора. Идентификация эффекта не должна ронять всю проверку."""
        for get in (lambda: a.get_editor_property("effect_name"),
                    lambda: a.get_editor_property("EffectName"),
                    lambda: getattr(a, "effect_name")):
            try:
                v = str(get())
                if v:
                    return v
            except Exception:
                pass
        try:
            return "class:" + a.get_class().get_name()
        except Exception:
            return a.get_name()

    def target_actors():
        """Эффект-акторы рядом с целью камеры (радиус 900 юн — как в диагностике)."""
        tx, ty, tz = st["cam"]["target"]
        out = []
        for a in effect_actors():
            L = a.get_actor_location()
            d = ((L.x - tx) ** 2 + (L.y - ty) ** 2 + (L.z - tz) ** 2) ** 0.5
            if d < 900.0:
                out.append((d, a))
        out.sort(key=lambda p: p[0])
        return [a for _d, a in out]

    def dump(tag):
        """Что РЕАЛЬНО стоит на секциях целевых акторов в этот момент."""
        rows = []
        for a in st["target"]:
            pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
            if not pmc:
                continue
            jeb = jeb_of(a)
            for i in range(pmc.get_num_materials()):
                m = pmc.get_material(i)
                if m is None:
                    rows.append({"актор": a.get_name(), "эффект": jeb, "секция": i, "материал": None})
                    log("[%s] %s сек=%d МАТЕРИАЛА НЕТ" % (tag, a.get_name(), i))
                    continue
                row = {"актор": a.get_name(), "эффект": jeb, "секция": i,
                       "материал": m.get_name(), "класс": type(m).__name__,
                       "мастер": master_of(m), "параметр_есть": has_param(m),
                       "EmissiveBoost": read_boost(m)}
                rows.append(row)
                log("[%s] %s (%s) сек=%d мат=%s (%s) мастер=%s парам=%s EmissiveBoost=%s"
                    % (tag, row["актор"], jeb, i, row["материал"], row["класс"],
                       row["мастер"], row["параметр_есть"], row["EmissiveBoost"]))
        return rows

    # ── фазы: по одной камере за раз ────────────────────────────────────────────────────────
    def phase_next():
        """Следующая камера списка либо завершение."""
        if st["i"] >= len(st["cams"]):
            return finish()
        st["cam"] = st["cams"][st["i"]]
        st["i"] += 1
        c = st["cam"]
        log("=== камера %s (.jeb «%s») ===" % (c["label"], c.get("jeb")))
        cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0"
           % (c["loc"][0], c["loc"][1], c["loc"][2], c["pitch"], c["yaw"]))
        after(WARMUP, phase_first)

    def phase_first():
        """Дамп на месте — до отъезда камеры."""
        st["target"] = target_actors()
        log("целевых акторов возле цели камеры: %d (%s)"
            % (len(st["target"]), ", ".join(a.get_name() for a in st["target"][:6])))
        st["rows1"] = dump("НА МЕСТЕ")
        after(2, phase_away)

    def phase_away():
        """Отъезд за дистанцию отсечки — секции эффектов чистятся."""
        c = st["cam"]
        cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0"
           % (c["loc"][0] + AWAY, c["loc"][1] + AWAY, c["loc"][2] + 3000.0, c["pitch"], c["yaw"]))
        after(SETTLE, phase_back)

    def phase_back():
        """Возврат — секции создаются заново, актор сам ставит на них материал эмиттера."""
        c = st["cam"]
        log("отъезд на %.0f юн выполнен (CullDistance 10000 заведомо превышен)" % AWAY)
        cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0"
           % (c["loc"][0], c["loc"][1], c["loc"][2], c["pitch"], c["yaw"]))
        after(SETTLE, phase_second)

    def phase_second():
        """Дамп после возврата — ЭТО и есть доказательство."""
        rows2 = dump("ПОСЛЕ ВОЗВРАТА")
        c = st["cam"]
        vals = sorted(set(str(r.get("EmissiveBoost")) for r in rows2 if r.get("материал")))
        mats = sorted(set(r["материал"] for r in rows2 if r.get("материал")))
        R["камеры"].append({"метка": c["label"], "jeb": c.get("jeb"),
                            "на_месте": st["rows1"], "после_возврата": rows2,
                            "значения_после_возврата": vals, "материалы_после_возврата": mats})
        log("ИТОГ камеры %s: EmissiveBoost после возврата = %s; материалы = %s"
            % (c["label"], vals, mats))
        after(2, phase_next)

    def finish():
        """Запись JSON и маркер успеха (успех определяется по логу, не по коду возврата)."""
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT, "w", encoding="utf-8") as f:
            json.dump(R, f, ensure_ascii=False, indent=1, default=str)
        log("JSON: %s" % OUT)
        log("FX-VERIFY ЗАВЕРШЕНО")
        cc("quit")

    def phase_init():
        """Мир, камеры, стартовые консольные ключи."""
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[fxVERIFY] МИР НЕ НАЙДЕН")
            return
        cc("t.MaxFPS 60")
        try:
            src = json.load(open(CAMS_IN, encoding="utf-8"))
        except Exception as ex:
            unreal.log_error("[fxVERIFY] камеры не прочитались %s: %s" % (CAMS_IN, ex))
            return
        cams = src.get("cams", [])
        st["cams"] = [c for c in cams if (not WANT or c["label"] in WANT)]
        log("камер к обходу: %d из %d" % (len(st["cams"]), len(cams)))
        log("эффект-акторов в мире: %d" % len(effect_actors()))
        after(30, phase_next)

    def tick(_dt):
        """Шаг очереди на пост-тике Slate (как в asda_fx_diag_boost.py)."""
        st["n"] += 1
        if st["n"] == 1:
            phase_init()
            return
        if st["w"] is None:
            return
        try:
            run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[fxVERIFY] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
