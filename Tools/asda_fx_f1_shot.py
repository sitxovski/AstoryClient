# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_f1_shot.py — КАДРЫ ВОЛНЫ Ф1 путём игрока (-game, не PIE, не редакторский вьюпорт).
#   Снимает пары «до/после» и ряд множителей ×2/×5/×10 по прототипным эффектам карты, а также
#   (если удаётся поднять своего персонажа) заклинание навыка, попадание и свечение оружия.
#
# ЧТО ИМЕННО МЕНЯЕТСЯ МЕЖДУ КАДРАМИ. Только скаляр `EmissiveBoost` мастеров эффектов, и только на
#   ДИНАМИЧЕСКИХ экземплярах (MID), созданных на компонентах эффектов ЭТОГО прогона. Ассеты (MIC, BP,
#   уровни) НЕ трогаются — раскат по ассетам делает отдельный генератор asda_apply_fx_boost.py.
#   Значения берутся из ТОЙ ЖЕ таблицы Tools/asda_fx_boost.json, что и генератор, — кадр показывает
#   ровно то, что даст раскат.
#
# ★ ФАЗА ЭФФЕКТА ЗАМОРАЖИВАЕТСЯ (TimeScale = 0) после прогрева: иначе «до» и «после» отличались бы
#   не только яркостью, но и случайным положением частиц, и глазами их было бы не сравнить.
#   Отдельно снимается серия фаз (ASDA_FXF1_PHASES) с живым временем — для эффектов, которые
#   читаются только в движении.
#
# Переменные окружения:
#   ASDA_FXF1_KEY      alpen | nightvalley (обязательно)
#   ASDA_FXF1_OUT      папка кадров (дефолт @@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/frames)
#   ASDA_FXF1_TAG      суффикс имён (дефолт пусто; для прогона с включённым затуханием — "soft")
#   ASDA_FXF1_ROW      ряд множителей помимо before/after (дефолт "2,5,10"; пусто = только before/after)
#   ASDA_FXF1_HERO     1 = поднять своего персонажа и снять навык/удар/свечение оружия (дефолт 1)
#   ASDA_FXF1_PHASES   сколько кадров фаз снять на эффект с живым временем (дефолт 3; 0 = не снимать)
#   ASDA_FXF1_RES      разрешение кадра (дефолт 2560x1440)
# Запуск:
#   python Tools/set0_run_game.py Tools/asda_fx_f1_shot.py --map /Game/Map/alpen/alpen \
#          --res 2560x1440 --env ASDA_FXF1_KEY=alpen --marker "FX-F1 ЗАВЕРШЕНО"
import glob
import json
import math
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_fx_boost as FXB   # таблица «семейство → буст»; импорт без побочных действий
import asda_map_registry as _MREG   # ★ дедуп №2 В1 Т14: уровень и id карты — из реестра карт (только чтение)

KEY = os.environ.get("ASDA_FXF1_KEY", "alpen")
OUTD = os.environ.get("ASDA_FXF1_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/frames")
TAG = os.environ.get("ASDA_FXF1_TAG", "")
ROW = [float(x) for x in os.environ.get("ASDA_FXF1_ROW", "2,5,10").split(",") if x.strip()]
HERO = os.environ.get("ASDA_FXF1_HERO", "1") == "1"
PHASES = int(os.environ.get("ASDA_FXF1_PHASES", "3") or 0)
RES = os.environ.get("ASDA_FXF1_RES", "2560x1440")
# ★ ASDA_FXF1_CAMS=<путь к fx_f1_<карта>.json> — взять камеры ГОТОВЫМИ из прежнего прогона вместо
#   собственного расчёта. Нужно для строгой пары кадров: расчёт камеры опирается на ФАКТИЧЕСКИЕ
#   габариты эффекта, а они у живой анимации кадр к кадру гуляют на единицы процентов — без этого
#   «до» и «после» из РАЗНЫХ запусков сняты с чуть разных точек.
CAMS_IN = os.environ.get("ASDA_FXF1_CAMS", "")
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
# ★ 09.09.2026 (волна калибровки bloom): добавлена сцена ЛОГИНА — там кристалл (`cre_creation01-a_00`)
#   и кольца создания (`eft_creation0N-a_00`) получили множитель ×3, и владелец смотрит именно на них.
#   У логина нет ни .mapraw, ни сетки проходимости, ни героя — камера берётся ШТАТНАЯ (см. LOGIN_CAM).
_LVL_ID = {k: _MREG.level_and_id(k) for k in ("alpen", "nightvalley")}   # (уровень, id) из реестра карт
MAPS = {"alpen": _LVL_ID["alpen"][0], "nightvalley": _LVL_ID["nightvalley"][0],
        "login": FXB.LOGIN_LEVEL}
MAPID = {"alpen": _LVL_ID["alpen"][1], "nightvalley": _LVL_ID["nightvalley"][1], "login": -1}   # id карт = DT_MapTable
# Сетка проходимости: у КАЖДОЙ карты ассет зовётся одинаково — DA_WalkGrid (имя без суффикса карты).
GRID = {"alpen": "/Game/Map/alpen/DA_WalkGrid.DA_WalkGrid",
        "nightvalley": "/Game/Map/nightvalley/DA_WalkGrid.DA_WalkGrid",
        "login": ""}                        # у логина сетки нет — высоты не нужны, камера штатная
# Тайл спавна героя (проходимая клетка рядом с прототипными эффектами карты).
HERO_TILE = {"alpen": (155, 320), "nightvalley": (188, 465)}
# Калибровка bloom: (метка, интенсивность, порог, масштаб размера, интенсивность бликов).
#   Первая строка — то, что стоит на картах сейчас. None = «не переопределять, оставить движковое».
#   ★ 09.09.2026: добавлены 4-е и 5-е поля. Размер (BloomSizeScale, движковый 4.0) — это РАДИУС
#     ореола; интенсивность бликов (LensFlareIntensity, движковая 1.0) нужна, чтобы показать,
#     перебивают ли блики ореол (у бликов СВОЙ порог, движковый 8.0, и своя интенсивность —
#     наша правка яркости bloom их не касалась ни разу).
BLOOM_SETS = [("v0_current", 0.35, -1.0, None, None),
              ("v1_soft", 0.50, 1.0, None, None),
              ("v2_engine", 0.675, 1.0, None, None),
              ("v3_clean", 0.675, 1.5, None, None),
              ("v4_halo", 1.00, 1.5, None, None),
              ("v5_wide", 1.00, 1.5, 6.0, None)]
# ★ ASDA_FXF1_BLOOM_SETS="метка:интенсивность:порог[:размер[:блики]],…" — ряд вариантов из окружения
#   (числа подбираются под кадр и не должны требовать правки файла).
_BS = os.environ.get("ASDA_FXF1_BLOOM_SETS", "").strip()
if _BS:
    def _f(x):
        x = x.strip()
        return None if x in ("", "-", "def") else float(x)
    BLOOM_SETS = []
    for _row in _BS.split(","):
        _p = (_row.strip().split(":") + ["", "", "", ""])[:5]
        BLOOM_SETS.append((_p[0], float(_p[1]), float(_p[2]), _f(_p[3]), _f(_p[4])))
BLOOM = os.environ.get("ASDA_FXF1_BLOOM", "1") == "1"
BLOOM_CAMS = int(os.environ.get("ASDA_FXF1_BLOOM_CAMS", "2"))
# ★ ASDA_FXF1_ONLY=bloom — снять ТОЛЬКО ряд калибровки bloom: ряды множителей, фазы, кадры героя
#   и замер цены пропускаются. Нужно потому, что множители яркости уже РАСКАТАНЫ ПО АССЕТАМ
#   (материалы `*_fxbNNN`), и ряд «до/после» повторно снимать незачем.
ONLY = os.environ.get("ASDA_FXF1_ONLY", "").strip().lower()
# ★ ASDA_FXF1_MATDUMP="фильтр1|фильтр2" — перед съёмкой напечатать `asda.Fx.MatDump <фильтр>`:
#   доказательство, что на секции в кадре стоит материал `*_fxbNNN` с нужным EmissiveBoost.
MATDUMP = [x for x in os.environ.get("ASDA_FXF1_MATDUMP", "").split("|") if x.strip()]
# Штатная камера сцены логина: кадр снимается ТАМ, КУДА СМОТРИТ ИГРОК (BugItGo не даётся).
LOGIN_CAM = {"label": "login_scene", "jeb": "", "doc": "штатный вид сцены логина, как его видит игрок",
             "keep_view": True, "loc": [0, 0, 0], "pitch": 0.0, "yaw": 0.0}
# Эффекты сцены логина, на которые наводимся отдельно (кольца создания персонажа, множитель ×3).
#   ★ BugItGo на логине не работает: игрового пешехода там нет, вид даёт КАМЕРА-АКТОР сцены —
#     поэтому наводка делается переносом самого актора камеры (только на время прогона).
#   ★ Ищутся НЕ по имени .jeb: в режиме -game Python не читает UPROPERTY `EffectName` (см. jeb_of),
#     а переписи имён у логина нет. Поэтому якорь берётся по имени МАТЕРИАЛА на секции — оно
#     доступно всегда и однозначно называет эффект (`MI_eft_creation01-…_fxb300`).
LOGIN_FX = [("creation01", "login_ring01", 700.0, "кольцо создания 1 (×3)"),
            ("creation03", "login_ring03", 700.0, "кольцо создания 3 (×3)")]
SETTLE = 90        # кадров прогрева после смены камеры (TSR + подкачка мипов)
WARMUP = 210       # кадров на выход эффектов на установившийся режим до заморозки фазы

# Прототипные эффекты карты: .jeb → (метка кадра, дистанция камеры в юнитах, подпись).
#   Метка попадает в имя файла: fx_<метка>_<вариант>.png.
PROTO_CAMS = {
    "alpen": [
        ("eff_noeaplhentower00_a_00", "alpen_tower_fire", 1500, "огонь башни (семейство map_fire ×4)"),
        ("eff_alpenredlight_a_00", "alpen_redlight", 900, "красный столб света (map_radiance ×2)"),
        ("eff_mdel_soulidol-a_00", "alpen_soulidol", 1100, "идол душ, 8 эмиттеров (map_radiance ×2)"),
        ("eff_mdel_alpen_smoke_00", "alpen_smoke", 700, "дым из трубы (map_smoke ×1 — намеренно без буста)"),
        ("eff_rainbow_a_00", "alpen_rainbow", 1600, "радуга дирижабля (map_radiance ×2)"),
    ],
    "nightvalley": [
        ("mdel_roadlamp02-a_00", "nv_roadlamp", 500, "уличный фонарь (map_fire ×4)"),
        ("eff_mdel_steamgate-a_00", "nv_steamgate", 900, "паровые врата/портал (map_portal ×3)"),
        ("mdel_worker09-a_00", "nv_worker_smoke", 600, "дым рабочего (map_smoke ×1)"),
        ("mdel_banditbul-a_00", "nv_banditbul", 700, "свечение лагеря бандитов (map_radiance ×2; текстура cmchar01-i = столб света)"),
        ("eff_mdel_alpenrefine-a_00", "nv_refine", 900, "пар плавильни (map_smoke ×1)"),
    ],
}
# Действия «на персонаже»: (метка, консольная команда, задержка кадров до кадра, подпись).
#   ★ Проверено прогоном 09.09: навык 707 офлайн НИЧЕГО не спавнит — его эффект «рождается у цели»,
#     а цели без сервера нет (лог: «ветка удара без цели не выполняется»). Навык 905 «Огненный шар»
#     спавнит каст-эффект И снаряд у самого персонажа — он и взят.
#   ★ Свечения ОРУЖИЯ офлайн не снять: у офлайн-павна подтип оружия 99 («без оружия»), а модель
#     оружия ставится приходом 5001 от сервера. Семейство weapon_glow остаётся предложением таблицы.
HERO_FX = [
    ("hero_spell", "asda.Skill.PlayFx 905", 20, "каст навыка 905 «Огненный шар» (char_skill_class ×3)"),
    ("hero_aura", "asda.Buff.PmFx 1", 40, "аура премиум-баффа на персонаже (char_common / char_item_buff)"),
    ("hero_luck", "asda.Buff.Boost 1", 40, "эффект удачи гачи на персонаже (char_common ×2.5)"),
]


def main():
    import unreal
    MAP = MAPS[KEY]
    R = {"key": KEY, "tag": TAG, "res": RES, "cams": [], "variants": [], "notes": [],
         "mids": 0, "untouched_mics": [], "conflicts": [], "shots": []}
    st = {"n": 0, "w": None, "q": [], "seen": set(), "grid": None, "mids": [], "dt": [], "names": {}, "midkeys": {}, "untouched": set()}

    def log(m):
        unreal.log("[fxF1] " + str(m))

    def note(m):
        R["notes"].append(str(m))
        log("NOTE: " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def V(x, y, z):
        return unreal.Vector(float(x), float(y), float(z))

    def dir_from(pitch, yaw):
        p, y = math.radians(pitch), math.radians(yaw)
        return (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p))

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    find_world = PH.make_find_world(MAP)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 11 стр.)

    def ground_z(x, y):
        """Высота земли по WalkGrid (у террейна порта коллизии нет — трассой не достать)."""
        g = st["grid"]
        if g is None:
            return None
        try:
            r = g.get_ground_y(float(y), float(-x))
            if isinstance(r, tuple):
                return float(r[1]) if r[0] else None
            return float(r)
        except Exception:
            return None

    # ── скриншоты ────────────────────────────────────────────────────────────────────────────
    new_pngs = PH.make_new_pngs(SSDIR, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    def take(dest, tries=300):
        """★ ИСПРАВЛЕНО 09.09.2026. Раньше кадр копировался ЧЕРЕЗ ФИКСИРОВАННЫЕ 45 кадров после
        `HighResShot`. Запись PNG 2560×1440 иногда не успевала — и копировался ЛИБО кадр прошлого
        варианта, либо файл, дописанный лишь наполовину (в прогоне 09.09 такие кадры видны по
        размеру: 3.4 МБ и 2.5 МБ против обычных 5.2-5.6 МБ, и по «яркость упала в 40 раз» в замере).
        Теперь ждём появления НОВОГО файла и его стабильного размера, а не считаем кадры."""
        new = new_pngs()
        if not new:
            if tries > 0:
                st["q"].insert(0, [1, lambda: take(dest, tries - 1)])
            else:
                note("нет нового PNG для %s" % os.path.basename(dest))
            return None
        src = new[-1]
        sz = os.path.getsize(src)
        if sz <= 0 or sz != st.get("lastsz_" + src, -1):
            st["lastsz_" + src] = sz
            st["seen"].discard(src)            # ещё пишется — проверим тот же файл на следующем кадре
            if tries > 0:
                st["q"].insert(0, [2, lambda: take(dest, tries - 1)])
                return None
        st["seen"].add(src)                    # размер стабилен — файл дописан, забираем
        shutil.copy(src, dest)
        R["shots"].append(os.path.basename(dest))
        log("СОХРАНЁН -> %s (%d б)" % (dest, os.path.getsize(dest)))
        return dest

    # ── планировщик по кадрам ────────────────────────────────────────────────────────────────
    after = PH.make_after_frames(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)

    run_queue = PH.make_frame_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 8 стр.)

    # ── эффекты мира: MID с бустом по таблице ────────────────────────────────────────────────
    def effect_actors():
        return unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaEffectActor)

    # Мастер материала → режим бленда. Надёжнее суффикса имени MIC: у динамического экземпляра
    #   (MID) имя своё, а мастер тот же.
    MASTER_MODE = {"M_AsdaEffectAdd": "add", "M_AsdaEffectAlpha": "alpha",
                   "M_AsdaEffectMod": "mod", "M_AsdaEffectModAdd": "modadd"}

    def blend_mode_of(mat):
        """Режим бленда материала эмиттера: по мастеру, с откатом на суффикс имени MIC."""
        try:
            base = mat.get_base_material()
            if base is not None:
                m = MASTER_MODE.get(base.get_name())
                if m:
                    return m
        except Exception:
            pass
        return FXB.mode_of_mic(mat.get_name())

    def load_name_map():
        """Соответствие «имя актора → .jeb», снятое в редакторе (asda_fx_f1_probe_maps.py).

        ★ Нужно потому, что в режиме -game чтение UPROPERTY `EffectName` через Python недоступно:
          «Failed to find property 'effect_name' … on 'AsdaEffectActor'» (в редакторе то же чтение
          работает). Имена размещённых акторов приходят из ассета уровня и совпадают в обоих режимах."""
        try:
            with open(r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/fx_map_actors.json", encoding="utf-8") as f:
                rows = json.load(f)
            m = {r["actor"]: r["jeb"] for r in rows if r["map"] == KEY}
            log("карта имён акторов: %d записей" % len(m))
            return m
        except Exception as ex:
            note("перепись эффектов карты не прочиталась (%s) — .jeb только по классу BP" % ex)
            return {}

    def jeb_of(a):
        """Имя .jeb эффект-актора: свойство → класс BP_Fx_<jeb>_C → перепись по имени актора."""
        try:
            v = str(a.get_editor_property("effect_name"))
            if v:
                return v
        except Exception:
            pass
        cn = a.get_class().get_name()
        if cn.startswith("BP_Fx_"):
            return cn[6:-2] if cn.endswith("_C") else cn[6:]
        return st["names"].get(a.get_name(), "")

    def build_mids():
        """Динамические экземпляры материалов для эмиттеров, ПОДОШЕДШИХ под правило таблицы.

        ★ Вызывается ЗАНОВО у каждой камеры: у эффекта дальше CullDistance (10000) секции PMC
          очищены (AsdaEffectActor.cpp:729-733), материалов у компонента ноль, и MID создать не из
          чего. Первый прогон 09.09 строил MID один раз у точки спавна героя и получил 13 штук на
          78 акторов — дальние эффекты между «до» и «после» не менялись вовсе.

        ★★★ ИСПРАВЛЕНО 09.09.2026 (диагностика effects_f1_diag_report.md). Реестр ключей
          `midkeys` запрещал ПОВТОРНОЕ создание MID, а актор его молча снимает: на возврате
          камеры секция пересобирается веткой Create, и там стоит `SetMaterial(i, E.Material)`
          (AsdaEffectActor.cpp:1606/931/963/1195) — то есть ИСХОДНЫЙ MIC. Объект MID при этом
          жив, `set_scalar_parameter_value` на нём отрабатывает, лог рапортует успех — а на
          секции уже другой материал, и на экране яркость прежняя. Доказано зондом
          `asda_fx_diag_boost.py --env ASDA_FXD_MODE=roundtrip`: после отъезда за CullDistance и
          возврата «наш MID стоит на 0, ЗАТЁРТ на 3, прочитано с компонента = 1.0».
          Поэтому теперь ключ не запрещает работу, а хранит сам MID: если на секции ВСЁ ЕЩЁ он —
          пропускаем, если его сняли — создаём заново из того, что там сейчас."""
        seen_untouched = set()
        added = 0
        regained = 0
        for a in effect_actors():
            jeb = jeb_of(a)
            pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
            if not pmc:
                continue
            for i in range(pmc.get_num_materials()):
                key = (a.get_name(), i)
                cur = pmc.get_material(i)
                if cur is None:
                    continue
                have = st["midkeys"].get(key)
                if have is not None and cur is have:
                    continue                      # наш MID на месте — ничего не делаем
                mode = blend_mode_of(cur)
                stg = FXB.settings(jeb, mode) if mode else None
                if stg is None or FXB.is_noop(stg):
                    seen_untouched.add("%s [%s] %s" % (jeb, mode, cur.get_name()))
                    continue
                try:
                    mid = pmc.create_dynamic_material_instance(i, cur, unreal.Name(""))
                except Exception as ex:
                    note("MID не создан для %s/%d: %s" % (jeb, i, ex))
                    continue
                if mid:
                    if have is not None:
                        regained += 1             # MID был снят актором и восстановлен
                    else:
                        added += 1
                    st["midkeys"][key] = mid
                    st["mids"] = [(m, s, j) for (m, s, j) in st["mids"] if m is not have]
                    st["mids"].append((mid, stg, jeb))
        R["mids"] = len(st["mids"])
        R["mids_regained"] = R.get("mids_regained", 0) + regained
        for u in seen_untouched:
            st["untouched"].add(u)
        R["untouched_mics"] = sorted(st["untouched"])
        if added or regained:
            log("MID +%d, восстановлено после снятия актором %d (всего %d); без правила / с множителем 1.0: %d"
                % (added, regained, len(st["mids"]), len(st["untouched"])))

    def apply_variant(kind, fixed=None):
        """kind: 'before' (всё 1.0), 'table' (значения таблицы), 'fixed' (одно значение на все, кроме 1.0-семейств)."""
        for mid, stg, _jeb in st["mids"]:
            if kind == "before":
                v = 1.0
            elif kind == "table":
                v = stg[0]
            else:
                v = float(fixed)
            try:
                mid.set_scalar_parameter_value(unreal.Name("EmissiveBoost"), float(v))
            except Exception as ex:
                note("не выставился EmissiveBoost: %s" % ex)
                return
        log("вариант применён: kind=%s значение=%s (MID %d)" % (kind, fixed, len(st["mids"])))

    def freeze(scale):
        """Останов/пуск времени симуляции эффектов.

        ★ ПОЧЕМУ slomo, а не TimeScale актора: в режиме -game Python-рефлексия UPROPERTY у
          AsdaEffectActor недоступна («Failed to find property …», см. jeb_of), и попытка выставить
          time_scale не меняла НИЧЕГО — первый прогон 09.09 снял «до/после» с разной фазой частиц.
          Дилатация мира умножает SimDt эффектов там же (AsdaEffectActor.cpp:782), а ноль движок
          не принимает — берём 0.001 (за весь ряд вариантов эффект «проживает» доли кадра)."""
        cc("slomo %g" % (1.0 if scale else 0.001))
        R["freeze_mode"] = "slomo"

    # ── камеры на эффекты ────────────────────────────────────────────────────────────────────
    def pick_actor(jeb):
        """Представитель эффекта: самый «жирный» по числу эмиттеров, из стоящих на проходимой земле."""
        best = None
        for a in effect_actors():
            if jeb_of(a).lower() != jeb.lower():
                continue
            loc = a.get_actor_location()
            g = ground_z(loc.x, loc.y)
            score = (1 if g is not None else 0, len(a.get_editor_property("emitters")))
            if best is None or score > best[0]:
                best = (score, a)
        return best[1] if best else None

    def pick_by_material(frag):
        """Эффект-актор, у которого хоть одна секция нарисована материалом с `frag` в имени.

        ★ Запасной поиск для сцены логина: там ни `EffectName` из Python, ни класса BP_Fx_*,
          ни переписи имён — а имя материала на секции читается всегда."""
        f = frag.lower()
        for a in effect_actors():
            pmc = a.get_component_by_class(unreal.ProceduralMeshComponent)
            if not pmc:
                continue
            for i in range(pmc.get_num_materials()):
                m = pmc.get_material(i)
                if m is not None and f in m.get_name().lower():
                    return a
        return None

    def build_cams():
        # центр карты в UE: игровой тайл (256,256) → (-25650, 25650); камеру ставим со стороны центра,
        #   чтобы за эффектом была карта, а не край мира
        cx, cy = -25650.0, 25650.0
        for jeb, label, dist, doc in PROTO_CAMS.get(KEY, []):
            a = pick_actor(jeb)
            if a is None:
                note("на карте нет эффекта %s — камера пропущена" % jeb)
                continue
            org, ext = a.get_actor_bounds(False)
            big = max(ext.x, ext.y, ext.z)
            tgt = org if big > 20.0 else a.get_actor_location()
            d = min(max(float(dist), big * 2.2), 2500.0)   # верхний предел: иначе радуга (габарит 2663) уводила камеру на 8 км
            vx, vy = cx - tgt.x, cy - tgt.y
            n = math.hypot(vx, vy) or 1.0
            camx, camy = tgt.x + vx / n * d, tgt.y + vy / n * d
            g = ground_z(camx, camy)
            camz = max(tgt.z + d * 0.18, (g + 170.0) if g is not None else tgt.z + d * 0.18)
            yaw = math.degrees(math.atan2(tgt.y - camy, tgt.x - camx))
            flat = math.hypot(tgt.x - camx, tgt.y - camy) or 1.0
            pitch = math.degrees(math.atan2(tgt.z - camz, flat))
            R["cams"].append({"label": label, "jeb": jeb, "doc": doc,
                              "loc": [round(camx, 1), round(camy, 1), round(camz, 1)],
                              "pitch": round(pitch, 2), "yaw": round(yaw, 2),
                              "target": [round(tgt.x, 1), round(tgt.y, 1), round(tgt.z, 1)],
                              "extent": [round(ext.x, 1), round(ext.y, 1), round(ext.z, 1)]})
            log("камера %s: %s pitch=%.1f yaw=%.1f цель=%s габарит=%.0f"
                % (label, R["cams"][-1]["loc"], pitch, yaw, R["cams"][-1]["target"], big))

    def refine_cam(c):
        """Пересчитать камеру по РЕАЛЬНЫМ габаритам эффекта.

        ★ Первый расчёт делается до прогрева, когда эффект ещё отсечён по дистанции и его секции
          пусты: `get_actor_bounds` даёт нулевой габарит, кадр получается «эффект у края экрана»
          (прогон 09.09: идол и красный столб оказались на границе кадра). После того как камера
          постояла рядом, габарит настоящий — наводимся по нему."""
        a = pick_actor(c["jeb"])
        if a is None:
            return
        org, ext = a.get_actor_bounds(False)
        big = max(ext.x, ext.y, ext.z)
        if big < 20.0:
            return                       # секций всё ещё нет — оставляем грубую наводку
        cx, cy = -25650.0, 25650.0       # центр карты: камеру ставим со стороны центра
        d = min(max(big * 2.6, 260.0), 2500.0)
        vx, vy = cx - org.x, cy - org.y
        n = math.hypot(vx, vy) or 1.0
        camx, camy = org.x + vx / n * d, org.y + vy / n * d
        g = ground_z(camx, camy)
        camz = max(org.z + d * 0.22, (g + 160.0) if g is not None else org.z + d * 0.22)
        flat = math.hypot(org.x - camx, org.y - camy) or 1.0
        c["loc"] = [round(camx, 1), round(camy, 1), round(camz, 1)]
        c["yaw"] = round(math.degrees(math.atan2(org.y - camy, org.x - camx)), 2)
        c["pitch"] = round(math.degrees(math.atan2(org.z - camz, flat)), 2)
        c["target"] = [round(org.x, 1), round(org.y, 1), round(org.z, 1)]
        c["extent"] = [round(ext.x, 1), round(ext.y, 1), round(ext.z, 1)]
        log("камера %s уточнена: %s pitch=%.1f yaw=%.1f габарит=%.0f дист=%.0f"
            % (c["label"], c["loc"], c["pitch"], c["yaw"], big, d))

    def set_view(c):
        # ★ 09.09.2026: у сцены логина камера ШТАТНАЯ (её ставит сам режиссёр логина) — телепорт
        #   DefaultPawn увёл бы кадр в никуда. Такая камера помечена keep_view.
        if c.get("keep_view"):
            return
        if c.get("move_cam"):
            a = st.get("camact")
            if a is None:
                return
            a.set_actor_location_and_rotation(V(*c["loc"]), unreal.Rotator(0.0, c["pitch"], c["yaw"]),
                                              False, False)
            return
        cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0" % (c["loc"][0], c["loc"][1], c["loc"][2], c["pitch"], c["yaw"]))

    def fname(label, variant):
        t = ("_" + TAG) if TAG else ""
        # ★ 09.09.2026: кадры калибровки ореола именуются `bloom_<карта>_<ракурс>_<вариант>.png` —
        #   владельцу они уходят отдельной пачкой, и карта должна читаться из имени файла.
        if variant.startswith("bloom_"):
            return os.path.join(OUTD, "bloom_%s_%s_%s%s.png" % (KEY, label, variant[6:], t))
        return os.path.join(OUTD, "fx_%s_%s%s.png" % (label, variant, t))

    def schedule_shot(c, variant, kind, val, settle=SETTLE):
        """Кадр одной камеры: наводка → прогрев → MID для ставших видимыми эффектов → буст → снимок."""
        after(1, lambda: set_view(c))
        after(settle, lambda: build_mids())
        after(1, lambda k=kind, v=val: apply_variant(k, v))
        # ★ ПОВТОР ПЕРЕД САМИМ СНИМКОМ: между наводкой и кадром актор мог пересобрать секцию
        #   (ветка Create → SetMaterial с исходным MIC) и снять наш MID. Проверяем ещё раз вплотную.
        after(4, lambda k=kind, v=val: (build_mids(), apply_variant(k, v)))
        after(2, lambda: (log("SHOT %s %s" % (c["label"], variant)), cc("HighResShot %s" % RES)))
        after(45, lambda: take(fname(c["label"], variant)))

    # ── фазы этапов ──────────────────────────────────────────────────────────────────────────
    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[fxF1] МИР НЕ НАЙДЕН для " + MAP)
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        os.makedirs(OUTD, exist_ok=True)
        st["seen"] = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))
        cc("r.SetRes %sw" % RES)
        cc("r.ProfileGPU.ShowUI 0")
        cc("t.MaxFPS 60")           # ровный шаг симуляции: фазы кадров сопоставимы между вариантами
        st["names"] = load_name_map()
        try:
            st["grid"] = unreal.load_object(None, GRID[KEY]) if GRID[KEY] else None
        except Exception as ex:
            st["grid"] = None
            note("WalkGrid не загрузился: %s" % ex)
        if st["grid"] is None and GRID[KEY]:
            # ★ load_object на несуществующем пути возвращает None БЕЗ исключения — молча остаться
            #   без высот нельзя: камера тогда ставится «на глаз» и может уйти под землю.
            note("WalkGrid %s не найден — высота камеры считается от цели, без опоры на землю" % GRID[KEY])
        elif st["grid"] is not None:
            log("WalkGrid: %s" % st["grid"].get_name())
        else:
            # ★ У сцены логина сетки нет ПО ЗАМЫСЛУ (GRID["login"] == ""). Раньше эта ветка звала
            #   get_name() у None и роняла phase_init — очередь шагов при этом молчала, и прогон
            #   уходил в «idle» без единой строки об ошибке.
            log("сетки проходимости у этой сцены нет — она и не нужна (камера штатная)")
        log("мир: %s, эффект-акторов: %d" % (st["w"].get_name(), len(effect_actors())))
        after(30, phase_hero)

    def phase_hero():
        """Поднять своего персонажа (нужен для навыка/удара/свечения оружия)."""
        if KEY == "login" or not HERO:      # ★ на сцене логина героя нет — там режиссёр своя
            after(1, phase_warm)
            return
        try:
            lm = None
            for o in unreal.ObjectIterator(unreal.AsdaLoadManagerSubsystem):
                lm = o
            if lm is not None:
                lm.set_editor_property("current_map_id", MAPID[KEY])
        except Exception as ex:
            note("CurrentMapId не выставить (%s) — персонажа не будет" % ex)
        tx, tz = HERO_TILE[KEY]
        cc("Asda.World.FakeSelfSpawn %d %d FxF1" % (tx, tz))
        after(60, phase_warm)

    def phase_warm():
        chars = []
        try:
            chars = unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaPlayerCharacter)
        except Exception:
            pass
        R["hero"] = bool(chars)
        if chars:
            L = chars[0].get_actor_location()
            R["hero_loc"] = [round(L.x, 1), round(L.y, 1), round(L.z, 1)]
            log("свой персонаж: %s @ %s" % (chars[0].get_name(), R["hero_loc"]))
        else:
            note("своего персонажа в мире нет — кадры навыка/удара/оружия не снимаются")
        after(WARMUP, phase_build)

    def load_cams_file():
        """Камеры из JSON прошлого прогона (ASDA_FXF1_CAMS). True, если взяты оттуда."""
        if not CAMS_IN or not os.path.isfile(CAMS_IN):
            return False
        try:
            src = json.load(open(CAMS_IN, encoding="utf-8"))
        except Exception as ex:
            note("камеры из %s не прочитались: %s" % (CAMS_IN, ex))
            return False
        R["cams"] = [dict(c) for c in src.get("cams", [])]
        R["cams_from"] = CAMS_IN
        log("камеры взяты готовыми из %s (%d шт.) — уточнение по габаритам ОТКЛЮЧЕНО" % (CAMS_IN, len(R["cams"])))
        return bool(R["cams"])

    def phase_build():
        # ★ 09.09.2026: у логина одна «камера» — штатный вид сцены, считать её не из чего.
        if KEY == "login":
            R["cams"] = [dict(LOGIN_CAM)]
            log("сцена логина: первый кадр — штатный вид (камеру не трогаем)")
            st["camact"] = None
            for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor):
                try:
                    if a.get_component_by_class(unreal.CameraComponent):
                        st["camact"] = a
                        break
                except Exception:
                    pass
            if st["camact"] is None:
                note("актора камеры на сцене логина нет — кадры колец не снимаются")
            else:
                log("актор камеры сцены логина: %s" % st["camact"].get_name())
                for jeb, label, dist, doc in LOGIN_FX:
                    a = pick_by_material(jeb)
                    if a is None:
                        note("на сцене логина нет эффекта %s" % jeb)
                        continue
                    org, ext = a.get_actor_bounds(False)
                    # ★ Дистанция берётся из ASDA_FXF1_LOGIN_DIST (юниты) или из габарита эффекта,
                    #   но с потолком: у колец создания габарит ~9400 юн, и «габарит × 2.4» уводило
                    #   камеру внутрь соседней декорации.
                    d = float(os.environ.get("ASDA_FXF1_LOGIN_DIST", "0") or 0)                         or min(max(float(dist), max(ext.x, ext.y, ext.z) * 1.2), 5000.0)
                    camx, camy, camz = org.x - d, org.y, org.z + d * 0.35
                    flat = math.hypot(org.x - camx, org.y - camy) or 1.0
                    R["cams"].append({"label": label, "jeb": jeb, "doc": doc, "move_cam": True,
                                      "loc": [round(camx, 1), round(camy, 1), round(camz, 1)],
                                      "pitch": round(math.degrees(math.atan2(org.z - camz, flat)), 2),
                                      "yaw": round(math.degrees(math.atan2(org.y - camy, org.x - camx)), 2),
                                      "target": [round(org.x, 1), round(org.y, 1), round(org.z, 1)]})
                    log("камера %s: %s цель=%s" % (label, R["cams"][-1]["loc"], R["cams"][-1]["target"]))
        elif not load_cams_file():
            build_cams()
        after(1, phase_prewarm)

    def phase_prewarm():
        """Проход по всем камерам с ЖИВЫМ временем: набить частицы и создать MID.

        ★ Зачем отдельный проход: эффект дальше CullDistance (10000) держит секции пустыми, и
          создать MID не из чего. Пока камера не постояла рядом, эффект «не существует»."""
        for c in R["cams"]:
            after(1, lambda c_=c: set_view(c_))
            after(SETTLE, lambda c_=c: (None if (R.get("cams_from") or c_.get("keep_view"))
                                        else (refine_cam(c_), set_view(c_))))
            after(40, lambda: build_mids())
        after(5, lambda: freeze(0.0))     # дальше фаза заморожена: между вариантами меняется ТОЛЬКО яркость
        after(20, phase_map_shots)

    def phase_map_shots():
        """Кадры по эффектам карты: before → after(таблица) → ряд множителей."""
        # ★ 09.09.2026: ASDA_FXF1_ONLY=bloom — множители уже в АССЕТАХ (`*_fxbNNN`), ряд «до/после»
        #   пересниматься не должен; идём сразу к калибровке ореола.
        if ONLY == "bloom":
            after(5, phase_bloom)
            return
        variants = [("before", "before", None), ("after", "table", None)]
        variants += [("x%g" % v, "fixed", v) for v in ROW]
        R["variants"] = [v[0] for v in variants]
        for vname, kind, val in variants:
            for c in R["cams"]:
                schedule_shot(c, vname, kind, val)
        after(5, phase_phase_series)

    def phase_phase_series():
        """Серия фаз с ЖИВЫМ временем — для эффектов, которые читаются только в движении."""
        if PHASES <= 0 or not R["cams"]:
            after(1, phase_hero_shots)
            return
        after(1, lambda: freeze(1.0))
        for c in R["cams"][:2]:           # двух представителей достаточно (дым и огонь/портал)
            for vname, kind, val in (("before", "before", None), ("after", "table", None)):
                after(1, lambda cc_=c: set_view(cc_))
                after(SETTLE, lambda: build_mids())
                after(1, lambda k=kind, v=val: apply_variant(k, v))
                for p in range(PHASES):
                    after(14, lambda c_=c, v_=vname, p_=p: (log("SHOT фаза %s %s %d" % (c_["label"], v_, p_ + 1)),
                                                            cc("HighResShot %s" % RES)))
                    after(45, lambda c_=c, v_=vname, p_=p: take(fname(c_["label"], "%s_ph%d" % (v_, p_ + 1))))
        after(1, lambda: freeze(0.0))
        after(5, phase_hero_shots)

    def phase_hero_shots():
        """Кадры эффектов НА ПЕРСОНАЖЕ: заклинание, попадание, свечение оружия."""
        if not R.get("hero"):
            after(5, phase_cost)
            return
        chars = unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaPlayerCharacter)
        L = chars[0].get_actor_location()
        # камера «лицом к герою»: сзади-сбоку, чуть выше пояса
        dist, yaw = 260.0, 35.0          # ближе: на 420 юн эффект персонажа занимал ~1/12 кадра
        d = dir_from(-10.0, yaw)
        cam = {"label": "hero", "loc": [L.x - d[0] * dist, L.y - d[1] * dist, L.z + 110.0],
               "pitch": -10.0, "yaw": yaw}
        after(1, lambda: freeze(1.0))     # эффекты персонажа живут своим временем
        for label, cmd, delay, doc in HERO_FX:
            for vname, kind, val in (("before", "before", None), ("after", "table", None)):
                after(1, lambda k=kind, v=val: apply_variant(k, v))
                after(1, lambda: set_view(cam))
                after(SETTLE, lambda c_=cmd: cc(c_))
                after(5, lambda: build_mids())     # эффект родился только что — ему нужен свой MID
                after(1, lambda k=kind, v=val: apply_variant(k, v))
                after(delay, lambda l_=label, v_=vname: (log("SHOT %s %s" % (l_, v_)), cc("HighResShot %s" % RES)))
                after(45, lambda l_=label, v_=vname: take(fname(l_, v_)))
                if PHASES > 1:
                    for p in range(1, PHASES):
                        after(14, lambda: cc("HighResShot %s" % RES))
                        after(45, lambda l_=label, v_=vname, p_=p: take(fname(l_, "%s_ph%d" % (v_, p_ + 1))))
        after(5, phase_cost)

    def phase_cost():
        """Цена кадра: ProfileGPU и stat unit на первой камере при before и after."""
        if not R["cams"]:
            after(5, phase_bloom)
            return
        c = R["cams"][0]
        after(1, lambda: freeze(0.0))
        for vname, kind in (("before", "before"), ("after", "table")):
            after(1, lambda k=kind: apply_variant(k, None))
            after(1, lambda: set_view(c))
            after(SETTLE, lambda v=vname: (log("PROFILE %s %s" % (c["label"], v)), cc("ProfileGPU")))
            after(30, lambda v=vname: R.setdefault("frame_ms", {}).__setitem__(
                v, round(1000.0 * sum(st["dt"][-90:]) / max(1, len(st["dt"][-90:])), 2)))
            after(1, lambda: (cc("stat unit"), cc("stat gpu")))
            after(45, lambda: cc("Shot showui"))
            after(25, lambda v=vname: take(os.path.join(OUTD, "fx_%s_stats_%s%s.png"
                                                        % (KEY, v, ("_" + TAG) if TAG else ""))))
            after(1, lambda: cc("stat none"))
        after(5, phase_bloom)

    # ★ 09.09.2026. Полный список ручек ореола и бликов — печатается ДО правок, чтобы в отчёте
    #   стояли ФАКТИЧЕСКИЕ значения волюма, а не предположения о движковых умолчаниях.
    PP_KNOBS = ["bloom_method", "bloom_intensity", "bloom_threshold", "bloom_size_scale",
                "bloom_1_size", "bloom_2_size", "bloom_3_size", "bloom_4_size", "bloom_5_size", "bloom_6_size",
                "bloom_convolution_size", "bloom_convolution_buffer_scale",
                "lens_flare_intensity", "lens_flare_threshold", "lens_flare_bokeh_size",
                "auto_exposure_method", "auto_exposure_bias",
                "auto_exposure_min_brightness", "auto_exposure_max_brightness"]

    def pp_carriers():
        """Куда класть пост-настройки: (имя, объект-владелец, имя свойства с FPostProcessSettings).

        ★ 09.09.2026. У КАРТ носитель один — безграничный PP-волюм `PP_<карта>`. У СЦЕНЫ ЛОГИНА
          PP-волюма НЕТ ВОВСЕ (прогон 09.09: «волюмов 0»), поэтому там пост-обработка — чистые
          умолчания движка, и переопределять её надо на камерах сцены. Запасной путь ищет
          компоненты камер и менеджер камеры игрока; вес блендинга поднимается до 1.0, иначе
          настройки камеры в кадр не попадут."""
        out = []
        for v in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.PostProcessVolume):
            out.append((v.get_actor_label(), v, "settings"))
        if out:
            return out
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor):
            comp = None
            try:
                comp = a.get_component_by_class(unreal.CameraComponent)
            except Exception:
                comp = None
            if comp is None:
                continue
            try:
                comp.set_editor_property("post_process_blend_weight", 1.0)
            except Exception as ex:
                note("вес пост-обработки камеры %s не выставился: %s" % (a.get_name(), ex))
            out.append(("камера " + a.get_name(), comp, "post_process_settings"))
        return out

    def pp_dump(tag):
        """Снимок ручек пост-обработки со ВСЕХ носителей пост-настроек мира (что стоит, что движковое)."""
        rows = []
        for label, holder, prop in pp_carriers():
            ps = holder.get_editor_property(prop)
            d = {"актор": label}
            for k in PP_KNOBS:
                try:
                    d[k] = str(ps.get_editor_property(k))
                except Exception:
                    d[k] = "нет свойства"
                try:                       # флаг override_<ключ>: True = ставили мы, False = движковое
                    d[k + " (override)"] = bool(ps.get_editor_property("override_" + k))
                except Exception:
                    pass
            rows.append(d)
            log("PP[%s] %s: %s" % (tag, d["актор"],
                                   " ".join("%s=%s%s" % (k, d[k], "" if not d.get(k + " (override)") else "*")
                                            for k in PP_KNOBS)))
        R.setdefault("pp", {})[tag] = rows
        log("PP[%s]: носителей %d (звёздочка = переопределено, без неё — движковое умолчание)" % (tag, len(rows)))

    def set_bloom(intensity, threshold, size_scale=None, flare=None):
        """Ручки ореола на ВСЕХ PostProcess-волюмах мира (только этот прогон, ассеты не трогаются).

        size_scale — BloomSizeScale (радиус ореола, движковый 4.0); flare — LensFlareIntensity
        (сила бликов, движковая 1.0). None = не переопределять."""
        n = 0
        for label, holder, prop in pp_carriers():
            try:
                ps = holder.get_editor_property(prop)
                ps.set_editor_property("override_bloom_intensity", True)
                ps.set_editor_property("bloom_intensity", float(intensity))
                ps.set_editor_property("override_bloom_threshold", True)
                ps.set_editor_property("bloom_threshold", float(threshold))
                # ★ None = ВЕРНУТЬ движковое: снимаем переопределение, иначе значение прошлого
                #   варианта осталось бы жить в волюме и следующий кадр был бы снят не тем, что в метке.
                ps.set_editor_property("override_bloom_size_scale", size_scale is not None)
                if size_scale is not None:
                    ps.set_editor_property("bloom_size_scale", float(size_scale))
                ps.set_editor_property("override_lens_flare_intensity", flare is not None)
                if flare is not None:
                    ps.set_editor_property("lens_flare_intensity", float(flare))
                holder.set_editor_property(prop, ps)
                n += 1
            except Exception as ex:
                note("bloom не выставился (%s): %s" % (label, ex))
                return
        log("bloom: интенсивность=%.3f порог=%.2f размер=%s блики=%s на %d носителях"
            % (intensity, threshold, size_scale, flare, n))

    def phase_bloom():
        """Ряд связок «интенсивность + порог» при бусте из таблицы — для выбора владельцем.

        Порог в UE — в ПОСТ-экспозиционной светимости: −1 = без отсечки (bloom берёт всю картинку),
        порог 1.0 = блумит только то, что ярче единицы. При фиксированной экспозиции карт (1.0)
        освещённая поверхность держится ниже 1, а забустенный эффект уходит за 1 — отсечка и делит
        «светятся эффекты» и «светится вся сцена»."""
        if not BLOOM or not R["cams"]:
            after(5, finish)
            return
        after(1, lambda: pp_dump("до правок"))
        # ★ Доказательство, что множитель яркости ДОЕХАЛ ДО КАДРА: печать материала каждой секции
        #   (ждём имя `*_fxbNNN` и EmissiveBoost > 1) на камере, где эффект точно виден.
        for f in MATDUMP:
            after(2, lambda f_=f: cc("asda.Fx.MatDump %s" % f_))
        after(1, lambda: apply_variant("table", None))
        for name, inten, thr, ssc, flr in BLOOM_SETS:
            after(1, lambda i=inten, t=thr, s_=ssc, f_=flr: set_bloom(i, t, s_, f_))
            for c in R["cams"][:BLOOM_CAMS]:
                after(1, lambda c_=c: set_view(c_))
                after(SETTLE, lambda: build_mids())
                after(1, lambda: apply_variant("table", None))
                after(6, lambda c_=c, n_=name: (log("SHOT bloom %s %s" % (c_["label"], n_)),
                                                cc("HighResShot %s" % RES)))
                after(45, lambda c_=c, n_=name: take(fname(c_["label"], "bloom_" + n_)))
        after(2, lambda: pp_dump("последний вариант"))
        for f in MATDUMP:
            after(2, lambda f_=f: cc("asda.Fx.MatDump %s" % f_))
        after(5, finish)

    def finish():
        out = os.path.join(OUTD, "fx_f1_%s%s.json" % (KEY, ("_" + TAG) if TAG else ""))
        with open(out, "w", encoding="utf-8") as f:
            json.dump(R, f, ensure_ascii=False, indent=1)
        log("записано %s" % out)
        log("FX-F1 ЗАВЕРШЕНО")
        cc("quit")

    def tick(dt):
        st["n"] += 1
        if st["n"] == 1:
            phase_init()
            return
        if st["w"] is None:
            return
        try:
            st["dt"].append(unreal.GameplayStatics.get_world_delta_seconds(st["w"]))
        except Exception:
            pass
        try:
            run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[fxF1] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
