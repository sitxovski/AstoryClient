# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_diag_boost.py — ДИАГНОСТИКА: доезжает ли EmissiveBoost до пикселей.
#   Ничего в ассетах не меняет. Работает путём ИГРОКА (-game), камеры берутся ГОТОВЫМИ из
#   fx_f1_<карта>.json волны Ф1 — чтобы кадры были сравнимы с уже снятыми.
#
# Что делает по шагам (каждый шаг печатает ЧИСЛА в лог, а не рассуждения):
#   1. ДАМП материала, которым РЕАЛЬНО нарисована каждая секция UProceduralMeshComponent целевого
#      эффекта: имя объекта, класс (MIC/MID), мастер, есть ли у мастера параметр EmissiveBoost,
#      и текущее значение параметра.
#   2. ПРОВЕРКА УДЕРЖАНИЯ: создаём MID, ставим значение, читаем ОБРАТНО с компонента сразу и через
#      N кадров — так видно, не затирает ли актор наш MID своим SetMaterial в тике.
#   3. КРИВАЯ ОТКЛИКА: ряд значений (дефолт 1/10/100/1000) с ПЕРЕПРОВЕРКОЙ значения перед каждым
#      кадром и снимком HighResShot.
#   4. НАСЫЩЕНИЕ: лестница экспозиции (auto_exposure_bias 0/-2/-4/-6) при бусте 1 — во сколько раз
#      надо прижать кадр, чтобы ядро эффекта погасло; это и есть его величина ДО тонмаппера.
#   5. КОНТРОЛЬ ПОСТ-ЭФФЕКТОВ: заведомо грубая правка bloom (интенсивность 3.0) — если и она не
#      меняет кадр, значит переопределения PP-волюмов вообще не доезжают, и выводы Ф1 по bloom
#      сделаны на пустом месте.
#
# Переменные окружения:
#   ASDA_FXD_KEY    alpen | nightvalley
#   ASDA_FXD_CAM    метка камеры из fx_f1_<карта>.json (дефолт: первая)
#   ASDA_FXD_ROW    ряд значений буста (дефолт "1,10,100,1000")
#   ASDA_FXD_OUT    папка кадров (дефолт @@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1_diag)
#   ASDA_FXD_RES    разрешение (дефолт 2560x1440)
# Запуск:
#   python Tools/set0_run_game.py Tools/asda_fx_diag_boost.py --map /Game/Map/alpen/alpen \
#          --res 2560x1440 --env ASDA_FXD_KEY=alpen --marker "FX-DIAG ЗАВЕРШЕНО"
import glob
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ue_paths as _UP   # ★ дедуп №2 В1 Т14: путь уровня карты — общий level_path (без прошитых литералов)

KEY = os.environ.get("ASDA_FXD_KEY", "alpen")
CAMLBL = os.environ.get("ASDA_FXD_CAM", "")
ROW = [float(x) for x in os.environ.get("ASDA_FXD_ROW", "1,10,100,1000").split(",") if x.strip()]
OUTD = os.environ.get("ASDA_FXD_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1_diag")
RES = os.environ.get("ASDA_FXD_RES", "2560x1440")
MODE = os.environ.get("ASDA_FXD_MODE", "curve")   # curve | roundtrip
CAMS_IN = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/frames/fx_f1_%s.json" % KEY
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
MAPS = {k: _UP.level_path(k) for k in ("alpen", "nightvalley")}   # карта → путь уровня /Game/Map/<a>/<a>
SETTLE = 90
WARMUP = 210
MASTER_MODE = {"M_AsdaEffectAdd": "add", "M_AsdaEffectAlpha": "alpha",
               "M_AsdaEffectMod": "mod", "M_AsdaEffectModAdd": "modadd"}


def main():
    import unreal
    MAP = MAPS[KEY]
    R = {"key": KEY, "cam": CAMLBL, "row": ROW, "notes": [], "dump": [], "hold": [],
         "curve": [], "shots": [], "exposure": [], "bloom_ctl": []}
    st = {"n": 0, "w": None, "q": [], "seen": set(), "cam": None, "mids": [], "target": []}

    def log(m):
        unreal.log("[fxDIAG] " + str(m))

    def note(m):
        R["notes"].append(str(m))
        log("NOTE: " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    after = PH.make_after_frames(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)

    run_queue = PH.make_frame_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 8 стр.)

    find_world = PH.make_find_world(MAP)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 11 стр.)

    new_pngs = PH.make_new_pngs(SSDIR, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    def take(name):
        new = new_pngs()
        if not new:
            note("нет нового PNG для %s" % name)
            return
        dest = os.path.join(OUTD, name + ".png")
        shutil.copy(new[-1], dest)
        R["shots"].append(name)
        log("СОХРАНЁН -> %s (%d б)" % (dest, os.path.getsize(dest)))

    def shot(name):
        after(6, lambda: cc("HighResShot %s" % RES))
        after(45, lambda n=name: take(n))

    # ── материалы ────────────────────────────────────────────────────────────────────────────
    def effect_actors():
        return unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaEffectActor)

    def master_of(mat):
        try:
            b = mat.get_base_material()
            return b.get_name() if b else "?"
        except Exception:
            return "?"

    def has_param(mat):
        """Есть ли у МАСТЕРА скаляр EmissiveBoost. Проверяем по списку имён, а не по значению:
        get_*_scalar_parameter_value на отсутствующем имени молча отдаёт 0.0 (грабля волны Ф1)."""
        try:
            b = mat.get_base_material()
            names = [str(n) for n in unreal.MaterialEditingLibrary.get_scalar_parameter_names(b)]
            return "EmissiveBoost" in names, names
        except Exception as ex:
            return None, ["ошибка: %s" % ex]

    def read_boost(mat):
        """Значение EmissiveBoost у КОНКРЕТНОГО экземпляра материала (MID или MIC)."""
        try:
            if isinstance(mat, unreal.MaterialInstanceDynamic):
                return float(mat.get_scalar_parameter_value(unreal.Name("EmissiveBoost"))), "MID.get"
        except Exception as ex:
            return None, "MID.get ошибка: %s" % ex
        try:
            v = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
                mat, unreal.Name("EmissiveBoost"))
            return float(v), "MIC.get"
        except Exception as ex:
            return None, "MIC.get ошибка: %s" % ex

    def target_actors():
        """Эффект-акторы возле цели камеры (радиус 1.5×дистанции до цели)."""
        c = st["cam"]
        t = unreal.Vector(c["target"][0], c["target"][1], c["target"][2])
        out = []
        for a in effect_actors():
            L = a.get_actor_location()
            d = ((L.x - t.x) ** 2 + (L.y - t.y) ** 2 + (L.z - t.z) ** 2) ** 0.5
            if d < 900.0:
                out.append((d, a))
        out.sort(key=lambda p: p[0])
        return [a for _d, a in out]

    def dump(tag):
        """Полный дамп материалов на секциях целевых акторов. Возвращает список словарей."""
        rows = []
        for a in st["target"]:
            pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
            if not pmc:
                continue
            for i in range(pmc.get_num_materials()):
                m = pmc.get_material(i)
                if m is None:
                    rows.append({"актор": a.get_name(), "секция": i, "материал": None})
                    continue
                ok, names = has_param(m)
                val, how = read_boost(m)
                row = {"актор": a.get_name(), "секция": i,
                       "материал": m.get_name(), "класс": type(m).__name__,
                       "мастер": master_of(m), "режим": MASTER_MODE.get(master_of(m), "?"),
                       "параметр_есть": ok, "значение": val, "как": how}
                rows.append(row)
                log("[%s] %s сек=%d мат=%s (%s) мастер=%s парам=%s ЗНАЧЕНИЕ=%s (%s)"
                    % (tag, row["актор"], i, row["материал"], row["класс"], row["мастер"],
                       row["параметр_есть"], row["значение"], how))
        R["dump"].append({"тег": tag, "строки": rows})
        return rows

    def make_mids(value):
        """Создать MID на всех секциях целевых акторов и выставить значение. Возвращает счётчик."""
        st["mids"] = []
        n = 0
        for a in st["target"]:
            pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
            if not pmc:
                continue
            for i in range(pmc.get_num_materials()):
                cur = pmc.get_material(i)
                if cur is None:
                    continue
                try:
                    mid = pmc.create_dynamic_material_instance(i, cur, unreal.Name(""))
                except Exception as ex:
                    note("MID не создан %s/%d: %s" % (a.get_name(), i, ex))
                    continue
                if mid is None:
                    continue
                try:
                    mid.set_scalar_parameter_value(unreal.Name("EmissiveBoost"), float(value))
                except Exception as ex:
                    note("значение не выставилось: %s" % ex)
                st["mids"].append((a, pmc, i, mid))
                n += 1
        log("MID создано: %d, значение=%s" % (n, value))
        return n

    def assert_value(value, tag):
        """ПЕРЕСТАВИТЬ значение и ПРОЧИТАТЬ ОБРАТНО С КОМПОНЕНТА — доказательство, что на секции
        стоит именно наш экземпляр и в нём именно это число."""
        same = 0
        lost = 0
        vals = []
        for (a, pmc, i, mid) in st["mids"]:
            try:
                mid.set_scalar_parameter_value(unreal.Name("EmissiveBoost"), float(value))
            except Exception:
                pass
            cur = pmc.get_material(i)
            if cur is not mid:
                lost += 1
            else:
                same += 1
            v, _how = read_boost(cur) if cur is not None else (None, "")
            vals.append(v)
        uniq = sorted(set(v for v in vals if v is not None))
        rec = {"тег": tag, "значение": value, "секций": len(st["mids"]),
               "наш_MID_на_секции": same, "MID_затёрт": lost, "прочитано": uniq}
        R["hold"].append(rec)
        log("[%s] значение=%s: секций=%d, наш MID стоит на %d, ЗАТЁРТ на %d, прочитано с компонента=%s"
            % (tag, value, len(st["mids"]), same, lost, uniq))
        return rec

    # ── PP-волюмы ────────────────────────────────────────────────────────────────────────────
    def pp_set(**kw):
        n = 0
        for v in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.PostProcessVolume):
            try:
                ps = v.get_editor_property("settings")
                for k, val in kw.items():
                    ps.set_editor_property("override_" + k, True)
                    ps.set_editor_property(k, val)
                v.set_editor_property("settings", ps)
                n += 1
            except Exception as ex:
                note("PP не выставился (%s): %s" % (kw, ex))
                return 0
        log("PP: %s на %d волюмах" % (kw, n))
        return n

    # ── фазы ─────────────────────────────────────────────────────────────────────────────────
    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[fxDIAG] МИР НЕ НАЙДЕН")
            cc("quit")
            return
        os.makedirs(OUTD, exist_ok=True)
        st["seen"] = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))
        cc("r.SetRes %sw" % RES)
        cc("t.MaxFPS 60")
        try:
            src = json.load(open(CAMS_IN, encoding="utf-8"))
        except Exception as ex:
            unreal.log_error("[fxDIAG] камеры не прочитались %s: %s" % (CAMS_IN, ex))
            cc("quit")
            return
        cams = src.get("cams", [])
        st["cam"] = next((c for c in cams if c["label"] == CAMLBL), None) or (cams[0] if cams else None)
        if st["cam"] is None:
            unreal.log_error("[fxDIAG] в JSON нет камер")
            cc("quit")
            return
        R["cam"] = st["cam"]["label"]
        log("камера: %s (%s) цель=%s" % (st["cam"]["label"], st["cam"].get("jeb"), st["cam"]["target"]))
        log("эффект-акторов в мире: %d" % len(effect_actors()))
        after(30, phase_view)

    def phase_view():
        c = st["cam"]
        cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0"
           % (c["loc"][0], c["loc"][1], c["loc"][2], c["pitch"], c["yaw"]))
        after(WARMUP, phase_dump)

    def phase_dump():
        st["target"] = target_actors()
        log("целевых акторов возле цели камеры: %d (%s)"
            % (len(st["target"]), ", ".join(a.get_name() for a in st["target"][:6])))
        dump("ИСХОДНО")
        after(2, phase_hold)

    def phase_hold():
        cc("slomo 0.001")
        after(5, lambda: make_mids(1000.0))
        after(2, lambda: assert_value(1000.0, "сразу после создания"))
        after(2, lambda: dump("после MID=1000"))
        after(60, lambda: assert_value(1000.0, "через 60 кадров"))
        after(2, lambda: dump("через 60 кадров"))
        after(2, phase_roundtrip if MODE == "roundtrip" else phase_curve)

    def phase_roundtrip():
        """★ ПРОВЕРКА ОТЪЕЗДА КАМЕРЫ. Эффект дальше CullDistance чистит секции
        (AsdaEffectActor.cpp:729-733), на возврате секции пересоздаются, и в ветке Create актор
        сам ставит на секцию СВОЙ материал из данных эмиттера (`SetMaterial(i, E.Material)`,
        .cpp:1606). Если это так — наш MID со секции слетает, а значение в объекте MID остаётся:
        лог рапортует успех, на экране прежняя яркость."""
        c = st["cam"]
        after(2, lambda: cc("slomo 1.0"))
        shot("rt_%s_%s_1_на_месте_boost1000" % (KEY, c["label"]))
        # отъезд на 30 000 юн вдоль обратного направления камеры (заведомо больше CullDistance 10000)
        after(2, lambda: cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0"
                            % (c["loc"][0] + 30000.0, c["loc"][1] + 30000.0, c["loc"][2] + 3000.0,
                               c["pitch"], c["yaw"])))
        after(150, lambda: assert_value(1000.0, "в отъезде"))
        after(2, lambda: cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0"
                            % (c["loc"][0], c["loc"][1], c["loc"][2], c["pitch"], c["yaw"])))
        after(150, lambda: assert_value(1000.0, "ПОСЛЕ возврата"))
        after(2, lambda: dump("после возврата"))
        after(2, lambda: cc("slomo 0.001"))
        shot("rt_%s_%s_2_после_возврата_boost1000" % (KEY, c["label"]))
        # и то же, что делает build_mids волны Ф1: повторный вызов ЗАБЛОКИРОВАН собственным
        # реестром ключей — здесь показываем, что помогает только ПЕРЕСОЗДАНИЕ MID
        after(2, lambda: make_mids(1000.0))
        after(2, lambda: assert_value(1000.0, "после ПЕРЕСОЗДАНИЯ MID"))
        shot("rt_%s_%s_3_MID_пересоздан_boost1000" % (KEY, c["label"]))
        after(5, finish)

    def phase_curve():
        for v in ROW:
            after(2, lambda vv=v: assert_value(vv, "кривая"))
            after(SETTLE, lambda: None)
            shot("diag_%s_%s_boost%g" % (KEY, st["cam"]["label"], v))
            after(2, lambda vv=v: R["curve"].append({"буст": vv,
                                                     "кадр": "diag_%s_%s_boost%g" % (KEY, st["cam"]["label"], vv)}))
        after(2, phase_exposure)

    def phase_exposure():
        """Лестница экспозиции при бусте 1: во сколько раз надо прижать кадр, чтобы ядро погасло."""
        after(2, lambda: assert_value(1.0, "экспозиция"))
        for bias in (0.0, -2.0, -4.0, -6.0):
            after(2, lambda b=bias: pp_set(auto_exposure_bias=float(b)))
            after(SETTLE, lambda: None)
            shot("diag_%s_%s_b1_ev%+g" % (KEY, st["cam"]["label"], bias))
            after(2, lambda b=bias: R["exposure"].append({"буст": 1.0, "ev": b}))
        # то же при бусте 100 — если параметр доезжает, ядро должно пережить −6 EV
        after(2, lambda: assert_value(100.0, "экспозиция"))
        for bias in (0.0, -4.0, -6.0):
            after(2, lambda b=bias: pp_set(auto_exposure_bias=float(b)))
            after(SETTLE, lambda: None)
            shot("diag_%s_%s_b100_ev%+g" % (KEY, st["cam"]["label"], bias))
            after(2, lambda b=bias: R["exposure"].append({"буст": 100.0, "ev": b}))
        after(2, lambda: pp_set(auto_exposure_bias=0.0))
        after(2, phase_bloom_ctl)

    def phase_bloom_ctl():
        """Контроль: доезжают ли переопределения PP-волюмов до кадра вообще."""
        after(2, lambda: assert_value(1.0, "bloom-контроль"))
        for lbl, inten, thr in (("b035_tOff", 0.35, -1.0), ("b300_tOff", 3.0, -1.0),
                                ("b035_t50", 0.35, 5.0)):
            after(2, lambda i=inten, t=thr: pp_set(bloom_intensity=float(i), bloom_threshold=float(t)))
            after(SETTLE, lambda: None)
            shot("diag_%s_%s_bloom_%s" % (KEY, st["cam"]["label"], lbl))
            after(2, lambda l=lbl, i=inten, t=thr: R["bloom_ctl"].append({"метка": l, "интенсивность": i, "порог": t}))
        after(2, lambda: pp_set(bloom_intensity=0.35, bloom_threshold=-1.0))
        after(5, finish)

    def finish():
        out = os.path.join(OUTD, "fx_diag_%s_%s_%s.json" % (KEY, R["cam"], MODE))
        with open(out, "w", encoding="utf-8") as f:
            json.dump(R, f, ensure_ascii=False, indent=1, default=str)
        log("записано %s" % out)
        log("FX-DIAG ЗАВЕРШЕНО")
        cc("quit")

    def tick(dt):
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
            unreal.log_error("[fxDIAG] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
