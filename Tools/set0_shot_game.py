# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# set0_shot_game.py — СЕТ 0 рендера: контрольные кадры «до» ПУТЁМ ИГРОКА (-game, не PIE, не
#   editor-viewport; закон ui-check-game-path-geometry). Ничего в проекте не меняет: камера —
#   штатный BugItGo (телепорт DefaultPawn), павн — штатный офлайн-крюк Asda.World.FakeSelfSpawn.
#
# Доказанные грабли -game/Python (5.8), учтённые здесь:
#   * в Python НЕТ спавна акторов (World.spawn_actor / GameplayStatics.begin_deferred_actor_spawn_from_class
#     отсутствуют) → камера = BugItGo x y z pitch yaw roll (DefaultPawn, FOV 90 менеджера камеры);
#   * у террейна порта НЕТ коллизии (движение по DA_WalkGrid) → LineTrace не бьёт землю; высоты берём
#     из UAsdaWalkGrid.GetGroundY (тот же источник, что у персонажа/мобов);
#   * тень от дерева трассой не доказать (нет коллизии) → геометрия: луч от моба К СОЛНЦУ проходит
#     сквозь цилиндр кроны ISM-инстанса дерева (меш с «tree» в имени);
#   * офлайн-павн гейтится LoadManager.CurrentMapId>=0 (в прямом -game он −1) → пробуем выставить
#     id карты из map_table.csv через set_editor_property; не вышло — кадры без персонажа (в отчёт).
#
# За ОДИН запуск на одной карте (env ASDA_SET0_KEY = alpen|nightvalley|flamion|chess):
#   мир → r.SetRes 1920x1080 → дамп рантайм-акторов → камеры карты (или из ASDA_SET0_CAMS — так
#   снимается «после» точь-в-точь) → павн → на каждой камере: прогрев → HighResShot 1920x1080 →
#   ProfileGPU (в лог) → средний кадр (мс) → Shot showui с stat unit/stat gpu; alpen кам.1 — Э4
#   (два соседних кадра ShowFlag.VisualizeSSAO и два `vis ScreenSpaceAO UV0`) → set0_<key>_cams.json.
# Запуск: python Tools/set0_run_game.py Tools/set0_shot_game.py --map /Game/Map/alpen/alpen --env ASDA_SET0_KEY=alpen
import os, glob, json, math, shutil, re
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_map_registry as _MREG   # ★ дедуп №2 В1 Т14: (уровень, id) карты — из реестра карт (только чтение)

OUTD = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05"
SSDIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
KEY = os.environ.get("ASDA_SET0_KEY", "alpen")
TAG = os.environ.get("ASDA_SET0_TAG", "before")
CAMS_IN = os.environ.get("ASDA_SET0_CAMS", "")
NOPAWN = os.environ.get("ASDA_SET0_NOPAWN", "") == "1"
# ★ дедуп №2 В1 Т14 (11.10.2026): (уровень, id) — из реестра карт по имени клиентской папки (`asda_map_registry.level_and_id`);
#   ключи-псевдонимы съёмки прежние.
MAPS = {"alpen": _MREG.level_and_id("alpen"), "nightvalley": _MREG.level_and_id("nightvalley"),
        "flamion": _MREG.level_and_id("Mountain Flamion"),
        "chess": _MREG.level_and_id("castle_of_chess"),
        # ★ 06.09 (indoor_fog): 4 закрытые карты без купола; id — DT_MapTable (Reports/map_table.csv: 10 = t_palace, 11 = t_chess)
        "chaos": _MREG.level_and_id("castle_of_chaos"),
        "queen": _MREG.level_and_id("palace_of_queen"),
        "tqueen": _MREG.level_and_id("t_palace_of_queen"),
        "tchess": _MREG.level_and_id("t_castle_of_chess"),
        # ★ Сет E-1 (06.09): mine — маленькая карта эксперимента Э10 (Distance Fields); id 23 — DT_MapTable (Reports/map_table.csv)
        "mine": _MREG.level_and_id("mine"),
        # ★ Сет E-4 (07.09.2026): L_LightTest — копия alpen в /Game/Temp (Tools/asda_build_light_test.py) с 12 локальными светами
        #   AAsdaLocalLightActor; id 3 = alpen (WalkGrid/спавн героя — alpen). Камеры — только из ASDA_SET0_CAMS (set0_lighttest_cams.json).
        "lighttest": ("/Game/Temp/L_LightTest", 3)}
# ★ 06.09 (indoor_fog): точка входа игрока = STARTPOS серверного портала на карту (GameServer/PortalIP_Info/*_PortalConfig.Ini):
#   chaos ← FirewayForest (105,226); queen ← Rein (129,282); tqueen ← Conquester (135,281) и tchess ← SunnyCoast (63,463) —
#   входы турнирных карт по Docs/Port/systems/ВКЛЮЧЕНИЕ_ТУРНИРНЫХ_КАРТ.md §4 (в исходных ini их нет).
INDOOR_ENTRY = {"chaos": ((105, 226), "k7"), "queen": ((129, 282), "k8"), "tqueen": ((135, 281), "k9"), "tchess": ((63, 463), "k10")}
MAP, MAPID = MAPS[KEY]
GRID_PATH = MAP.rsplit("/", 1)[0] + "/DA_WalkGrid.DA_WalkGrid"
if KEY == "lighttest":   # ★ E-4: сетка высот alpen (у копии в /Game/Temp своей нет)
    GRID_PATH = "/Game/Map/alpen/DA_WalkGrid.DA_WalkGrid"
SETTLE = 240        # кадров прогрева после переключения камеры (TSR + подкачка мипов)
GRID_STEP = 500.0   # шаг сетки высот для поиска склона (flamion)
CVARS = ["r.GTAO.TemporalFilter", "r.ContactShadows", "r.Shadow.Virtual.Enable", "r.Tonemapper.Sharpen",
         "r.ReflectionMethod", "r.ScreenPercentage", "r.AmbientOcclusion.Method", "r.AmbientOcclusionLevels",
         "r.AmbientOcclusionStaticFraction", "r.DefaultFeature.AmbientOcclusion", "r.CapsuleShadows",
         "r.DynamicGlobalIlluminationMethod", "r.GenerateMeshDistanceFields", "r.AllowStaticLighting",
         "sg.ShadowQuality", "sg.PostProcessQuality", "sg.AntiAliasingQuality", "r.AntiAliasingMethod",
         # ★ Доводка сета B (05.09): капсулы героя и очередь VSM — значения в лог каждого прогона
         "r.CapsuleShadowFadeAngleFromVertical", "r.CapsuleMaxIndirectOcclusionDistance", "asda.Shadow.Capsule",
         "asda.Shadow.CapsuleMinVisibility", "r.Shadow.Virtual.MaxPhysicalPages", "r.Shadow.Virtual.ResolutionLodBiasDirectional",
         "r.Shadow.Virtual.NonNanite.IncludeInCoarsePages", "r.Shadow.Virtual.Clipmap.FirstCoarseLevel", "r.Shadow.Virtual.Clipmap.LastCoarseLevel",
         # ★ Сет E-0 (06.09): уровни качества — все sg.* группы и ключи матрицы уровней (setE_plan_addendum §1) со SetBy в лог каждого прогона:
         #   доказательство «Low ≈ High» (пришпилено ini) до E-0b и «Low реально Low» (SetBy Scalability) после.
         "sg.ResolutionQuality", "sg.ViewDistanceQuality", "sg.TextureQuality", "sg.EffectsQuality", "sg.FoliageQuality",
         "sg.GlobalIlluminationQuality", "sg.ReflectionQuality", "sg.ShadingQuality",
         # ★ Сет E-2 (06.09): DFAO — ключи и SetBy в лог каждого прогона (гейт «Low без DF», DFAO на Medium+)
         "r.DistanceFieldAO", "r.AOQuality", "r.AOGlobalDistanceField", "r.AOMaxViewDistance", "r.SkylightIntensityMultiplier",
         "r.DistanceFieldShadowing", "r.AOGlobalDistanceFieldClipmapUpdatesPerFrame",
         "r.ShadowQuality", "r.Shadow.CSM.MaxCascades", "r.Shadow.MaxCSMResolution", "r.Shadow.DistanceScale", "r.Shadow.CSM.TransitionScale",
         "r.Shadow.RadiusThreshold", "r.LightFunctionQuality", "r.DistanceFieldShadowing", "r.DistanceFieldAO", "r.SkylightIntensityMultiplier",
         "r.Lumen.DiffuseIndirect.Allow", "r.Lumen.Reflections.Allow", "r.SSR.Quality", "r.VolumetricFog", "r.LightMaxDrawDistanceScale",
         "r.MaxAnisotropy", "r.ViewDistanceScale", "r.MaterialQualityLevel", "r.TSR.History.ScreenPercentage", "foliage.DensityScale",
         "r.AmbientOcclusionMaxQuality", "r.BloomQuality", "r.LensFlareQuality", "r.MotionBlurQuality",
         # ★ Сет E-1 (06.09): Distance Fields — ключи проекта/уровней со SetBy (DF выкл на Low: r.DistanceFieldShadowing/AOQuality 0)
         "r.DistanceFields.MaxPerMeshResolution", "r.DistanceFields.DefaultVoxelDensity", "r.AOQuality", "r.AOGlobalDistanceField",
         "r.Lumen.TraceMeshSDFs", "r.DistanceFields.SupportEvenIfHardwareRayTracingSupported", "r.MeshCardRepresentation",
         # ★ Сет E-3 (07.09.2026, Lumen): ключи GI/отражений уровня со SetBy в лог каждого прогона (гейт «Low/Medium без Lumen», Epic = Lumen SW)
         "r.Lumen.FinalGatherMethod", "r.Lumen.ScreenProbeGather.DownsampleFactor", "r.Lumen.ScreenProbeGather.RadianceCache.NumProbesToTraceBudget",
         "r.Lumen.ScreenProbeGather.TracingOctahedronResolution", "r.Lumen.ScreenProbeGather.ShortRangeAO", "r.Lumen.DiffuseIndirect.SSAO",
         "r.LumenScene.SurfaceCache.AtlasSize", "r.LumenScene.SurfaceCache.CardTexelDensityScale", "r.LumenScene.SurfaceCache.MeshCardsMinSize",
         "r.LumenScene.SurfaceCache.MeshCardsMergeInstances", "r.Lumen.TraceMeshSDFs.Allow", "r.Lumen.TraceDistanceScale", "r.Lumen.HeightFogOnGI",
         "r.Lumen.HeightFog", "r.Lumen.Reflections.DownsampleFactor", "r.Lumen.Reflections.MaxRoughnessToTrace", "r.Lumen.HardwareRayTracing",
         "r.Lumen.Supported", "r.LumenScene.GlobalSDF.Resolution", "r.LumenScene.GlobalSDF.ClipmapExtent", "r.Lumen.ScreenProbeGather.TwoSidedFoliageBackfaceDiffuse"]
# ★ Доводка сета B (05.09): ASDA_SET0_SPAWN_TILE="tx,tz" — тайл спавна героя вместо тайла цели камер (герой рядом с мобами на alpen k1_a5,
#   чтобы герой и моб стояли в ОДНОМ кадре — review_setB_apply.md §4 шаг 1а); ASDA_SET0_EXEC_POST="cmd;cmd" — консольные команды
#   ПОСЛЕ спавна героя (напр. asda.Shadow.CapsuleDump — дамп капсул уже живого тела). Только рантайм прогона.
SPAWN_TILE = os.environ.get("ASDA_SET0_SPAWN_TILE", "")
EXEC_POST = [x.strip() for x in os.environ.get("ASDA_SET0_EXEC_POST", "").split(";") if x.strip()]
# ★ Сет C-1 (05.09): ASDA_SET0_HERO_CAM="dist,pitch,head_z[,yaw_off]" — доп. камера «лицо героя» ПОСЛЕ спавна (герой известен только тогда):
#   стоит перед лицом персонажа на dist юн по yaw актора+yaw_off (меш смотрит в actor_yaw+90), смотрит назад на char_loc+head_z; кадр set0_<key>_hero_<tag>.png.
#   Нужна для A/B roughness персонажей и SSS лица (M_AsdaSkinLit) — на штатных камерах сета 0 герой мелкий/далёкий.
#   ASDA_SET0_VIS="GBufferC A" — после каждого кадра ещё один `Shot` с `vis <буфер>` (экранный визуализатор; напр. GBufferC A =
#   материальный AO при r.AllowStaticLighting=False) → set0_<key>_<n>_<tag>_vis.png. Только рантайм прогона.
HERO_CAM = os.environ.get("ASDA_SET0_HERO_CAM", "")
VIS = os.environ.get("ASDA_SET0_VIS", "").strip()
# ★ Сет E-1 (06.09): ASDA_SET0_SHOWFLAGS="VisualizeMeshDistanceFields,VisualizeGlobalDistanceField" — после каждого кадра по одному
#   `Shot` на каждый ShowFlag.<имя> 1/0 (ShowFlagsValues.inl:353,357) → set0_<key>_<n>_<tag>_sf_<имя>.png. ASDA_SET0_DF_STATS=1 —
#   r.DistanceFields.LogAtlasStats 2 на кадре ProfileGPU (движок сам сбрасывает в 0 после дампа, DistanceFieldStreaming.cpp:1682-1686):
#   в лог уходят статистика атласа DF и список SDF-ассетов сцены. Только рантайм прогона.
SHOWFLAGS = [x.strip() for x in os.environ.get("ASDA_SET0_SHOWFLAGS", "").split(",") if x.strip()]
DF_STATS = os.environ.get("ASDA_SET0_DF_STATS", "") == "1"
# ★ Сет E-3 (07.09.2026, Lumen): элемент ASDA_SET0_SHOWFLAGS вида "VisualizeLumen:3" — перед ShowFlag.VisualizeLumen 1 ставится
#   r.Lumen.Visualize 3 (LumenVisualize.cpp: 1 обзор, 3 Lumen Scene — промахи лучей = НЕБО из кубмапа SkyLight, 4 отражения, 5 покрытие
#   surface cache, 12/13 прямой/непрямой свет карточек), после кадра ShowFlag 0 и r.Lumen.Visualize 0; кадр set0_<key>_<n>_<tag>_sf_VisualizeLumen3.png.
#   Так же "VisualizeLumen:card" → r.Lumen.Visualize.CardPlacement 1 (размещение карточек) → _sf_VisualizeLumenCard.png.
#   ASDA_SET0_FLICKER=N — после основного кадра ещё N подряд идущих `Shot` (соседние кадры, камера и облака неподвижны) →
#   set0_<key>_<n>_<tag>_fl<k>.png: мера мерцания Lumen (absdiff соседних кадров) — план §5.5 «мерцание: 3 кадра подряд».
try:
    FLICKER = int(os.environ.get("ASDA_SET0_FLICKER", "0") or 0)
except ValueError:
    FLICKER = 0
# ★ Сет C-2 (05.09): ASDA_SET0_HERO_CAM принимает НЕСКОЛЬКО камер героя: "d,p,z,yaw|метка;d,p,z,yaw|метка" (метка → set0_<key>_<метка>_<tag>.png;
#   без метки — hero). ASDA_SET0_CONTACT="метка:regex;метка:regex" — контактные камеры ближнего плана по MIC: среди всех StaticMesh/ISM-инстансов
#   мира берётся ближайший к цели ракурса объект, чей материал (имя MIC) или меш совпадает с regex; камера ставится на 1.8·радиус bounds
#   со стороны солнца (относительный рельеф виден по светотени), взгляд на центр объекта. Спец-regex "@ground" — крупный план земли у цели
#   (тайл террейна). Ревизия классов силы рельефа NormalStudio (setC2_report.md §4). Только рантайм прогона.
CONTACT = [x.strip() for x in os.environ.get("ASDA_SET0_CONTACT", "").split(";") if x.strip()]
# ★ Сет C-2 (05.09): ASDA_SET0_FREEZE_CLOUDS=1 — остановить тени облаков на время прогона: Light Function солнца (M_AsdaCloudShadowLF, панорама
#   Time×SpeedU) заменяется MID с SpeedU=SpeedV=0 ⇒ смещение маски 0 во всех прогонах. Иначе облачная тень ползёт (SpeedU 0.01) и кадры «до/после»
#   с разных запусков расходятся на absdiff ~25 (лист 92→117 люмы при неизменном материале) — сравнение рельефа тонет в облаках. Только рантайм.
FREEZE_CLOUDS = os.environ.get("ASDA_SET0_FREEZE_CLOUDS", "") == "1"
# ★ Сет D (05.09): ASDA_SET0_UNHIDE_MOBS=<радиус юн> — раскрывать hidden-мобов (ждут adoption 4013) у КАЖДОЙ камеры, не только у «mobshadow»
#   (кадры nightvalley с мобом для рима). ASDA_SET0_HERO_MID="Param=val;Param=val" — после спавна героя на ВСЕ его меш-компоненты ставится
#   рантайм-MID (create_dynamic_material_instance поверх текущего материала) со скалярами (A/B roughness по маске: RoughAlphaMix=1 против
#   дефолта 0 мастера, ассеты не трогаются). Только рантайм прогона.
try:
    UNHIDE_MOBS = float(os.environ.get("ASDA_SET0_UNHIDE_MOBS", "0") or 0)
except ValueError:
    UNHIDE_MOBS = 0.0
HERO_MID = [x.strip() for x in os.environ.get("ASDA_SET0_HERO_MID", "").split(";") if x.strip()]
# ★ Сет D: ASDA_SET0_MPC="/Game/Materials/MPC_AsdaToon:RimPower=8;RimWidth=0.1" — скаляры коллекции ЭТОГО мира на время прогона
#   (unreal.MaterialLibrary.set_scalar_parameter_value; поверх значений актора Rim_<map>, ассеты не трогаются) — A/B параметров рима.
MPC_SET = os.environ.get("ASDA_SET0_MPC", "").strip()
# ★ Сет C-2 fix UE-этап (05.09, review_setC2.md ВАЖНО-2): ASDA_SET0_NPC_FACE="regex@@count@@dist,pitch,head_off,yaw_off@@Param=val;Param=val"
#   — контактные камеры на ЛИЦО NPC живого мира (AAsdaNpcActor, скелетал `Mesh`): среди NPC, чей скелетал-меш/материалы совпадают
#   с regex (напр. `npc_[mf]_h_` = humanoid), берутся count ближайших к цели ракурса; камера — на dist юн перед лицом по
#   yaw актора + yaw_off (меш смотрит в actor_yaw+90, как у героя), взгляд на верх габаритов меша (MeshTopZ) + head_off (лицо ниже
#   макушки). Для каждого NPC ДВА кадра: как есть (`set0_<key>_npcface<i>_<mesh>_<tag>.png`) и с рантайм-MID на всех слотах меша со
#   скалярами из последнего поля (напр. `ClassStrength=0` → `…_<tag>_cs0.png`) — A/B класса силы рельефа без правки ассетов.
#   Idle-анимация NPC замораживается на его меше (GlobalAnimRateScale=0), чтобы поза A и B совпала. Только рантайм прогона.
NPC_FACE = os.environ.get("ASDA_SET0_NPC_FACE", "").strip()
# ★ Сет E-2 (06.09.2026, DFAO): A/B БЕЗ правки ассетов и без перезапуска мира — один прогон, несколько вариантов рантайм-состояния:
#   ASDA_SET0_BASE="действие;действие" — применить один раз после спавна героя, ДО всех камер (состояние всего прогона);
#   ASDA_SET0_VARIANTS="имя:действие;действие|имя2:…" — после базовых кадров для каждого варианта: применить действия и ПЕРЕСНЯТЬ те же
#     камеры → set0_<key>_<n>_<tag>_<имя>.png (+_stats, ShowFlag-кадры), маркер PROFILE «<камера>@<имя>» (свой ProfileGPU на вариант).
#     Варианты НЕ откатываются сами — следующий вариант должен явно вернуть, что менял (или строить на предыдущем).
#   Действия (вид=аргумент): cc=<консольная команда>; sky=<поле>=<знач> — на всех SkyLightComponent мира (occlusion_max_distance /
#     contrast / min_occlusion / cast_shadows; после записи прокси пересоздаётся тумблером видимости — OcclusionMaxDistance в прокси
#     попадает только при пересоздании, SkyLightComponent.cpp:252 vs 511-521); dfl=terrain|foliage=0|1 — SetAffectDistanceFieldLighting
#     у SMC террейна (меш SM_terrain) / ISM листвы (I_<модель>, asda_foliage) — «в DF-сцене или нет» (RendererScene.cpp:573-584);
#     tmid=<Параметр>=<знач> — MID на всех слотах SMC террейна (напр. TerrainAOAmount=0.4); ppv=<поле>=<знач> — FPostProcessSettings
#     всех PostProcessVolume (override_* ставится сам, напр. ambient_occlusion_intensity=0.2).
BASE_ACTIONS = [x.strip() for x in os.environ.get("ASDA_SET0_BASE", "").split(";") if x.strip()]
VARIANTS = []
for _v in [x.strip() for x in os.environ.get("ASDA_SET0_VARIANTS", "").split("|") if x.strip()]:
    _n, _, _acts = _v.partition(":")
    VARIANTS.append((_n.strip(), [a.strip() for a in _acts.split(";") if a.strip()]))


def main():
    import unreal
    R = {"key": KEY, "map": MAP, "map_id": MAPID, "tag": TAG, "cams": [], "pawn": None, "runtime_actors": {},
         "notes": [], "height_source": None}
    st = {"n": 0, "w": None, "dt": [], "q": [], "seen": set(), "grid": None, "trees": None, "dp": None}

    def log(m):
        unreal.log("[set0] " + str(m))

    def note(m):
        R["notes"].append(str(m))
        log("NOTE: " + str(m))

    def cc(c):
        unreal.SystemLibrary.execute_console_command(st["w"], c)
        log("cmd: " + c)

    def V(x, y, z):
        return unreal.Vector(float(x), float(y), float(z))

    def vec3(v):
        return [round(v.x, 2), round(v.y, 2), round(v.z, 2)]

    def dir_from(pitch, yaw):
        p = math.radians(pitch); y = math.radians(yaw)
        return (math.cos(p) * math.cos(y), math.cos(p) * math.sin(y), math.sin(p))

    def yaw_to(dx, dy):
        return math.degrees(math.atan2(dy, dx))

    # ---------- мир / высоты ----------
    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    find_world = PH.make_find_world(MAP)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 11 стр.)

    def load_grid():
        try:
            g = unreal.load_object(None, GRID_PATH)
            if g:
                r = g.get_ground_y(25600.0, 25600.0)   # Python 5.8 отдаёт голый float (bool-возврат опущен)
                y = r[1] if isinstance(r, tuple) else r
                log("WalkGrid %s: центр карты y=%.1f (raw=%r)" % (GRID_PATH, y, r))
                st["grid"] = g
                R["height_source"] = "UAsdaWalkGrid.GetGroundY(%s)" % GRID_PATH
                return
        except Exception as ex:
            log("WalkGrid недоступен: %s" % ex)
        R["height_source"] = "нет (WalkGrid не загрузился)"

    def ground_z(x, y):
        """Высота земли UE-z в точке UE (x,y): game X = UE.y, game Z = −UE.x, game Y = UE.z."""
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

    # ---------- деревья (ISM-инстансы) для геометрической тени ----------
    def collect_trees():
        """[(cx, cy, zbot, ztop, radius, mesh, actor)] — цилиндры крон всех инстансов мешей 'tree'."""
        w = st["w"]
        trees = []
        n_comp = 0
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
            try:
                comps = a.get_components_by_class(unreal.StaticMeshComponent)
            except Exception:
                continue
            for c in comps:
                try:
                    sm = c.static_mesh
                except Exception:
                    sm = None
                if not sm:
                    continue
                mn = sm.get_name().lower()
                if "tree" not in mn:
                    continue
                n_comp += 1
                b = sm.get_bounds()
                O = b.origin; E = b.box_extent
                if isinstance(c, unreal.InstancedStaticMeshComponent):
                    cnt = c.get_instance_count()
                    for i in range(cnt):
                        tr = c.get_instance_transform(i, True)   # 5.8: голый Transform (или (bool, Transform))
                        if isinstance(tr, tuple):
                            if not tr[0]:
                                continue
                            tr = tr[1]
                        L = tr.translation; S = tr.scale3d
                        trees.append((L.x + O.x * S.x, L.y + O.y * S.y, L.z + (O.z - E.z) * S.z, L.z + (O.z + E.z) * S.z,
                                      max(E.x * abs(S.x), E.y * abs(S.y)) * 0.7, mn, a.get_name()))
                else:
                    L = c.get_world_location(); S = c.get_world_scale()
                    trees.append((L.x + O.x * S.x, L.y + O.y * S.y, L.z + (O.z - E.z) * S.z, L.z + (O.z + E.z) * S.z,
                                  max(E.x * abs(S.x), E.y * abs(S.y)) * 0.7, mn, a.get_name()))
        st["trees"] = trees
        log("деревьев (инстансов мешей 'tree'): %d в %d компонентах" % (len(trees), n_comp))

    def shadow_blocker(M, sd):
        """Первое дерево, чью крону пересекает луч из M к солнцу sd. None — моб на солнце."""
        ox, oy, oz = M
        dx, dy, dz = sd
        dd = dx * dx + dy * dy
        best = None
        for (cx, cy, zb, zt, r, mn, an) in st["trees"]:
            if dd < 1e-6:
                t = 0.0
            else:
                t = ((cx - ox) * dx + (cy - oy) * dy) / dd
            if t <= 0.0 or t > 6000.0:
                continue
            px = ox + dx * t; py = oy + dy * t; pz = oz + dz * t
            if math.hypot(px - cx, py - cy) > r:
                continue
            if pz < zb + (zt - zb) * 0.35 or pz > zt:
                continue
            if best is None or t < best[0]:
                best = (t, mn, an, (cx, cy, zb, zt, r))
        return best

    # ---------- дамп рантайм-акторов ----------
    def sval(v):
        if isinstance(v, unreal.LinearColor):
            return [round(v.r, 5), round(v.g, 5), round(v.b, 5), round(v.a, 5)]
        if isinstance(v, unreal.Vector):
            return vec3(v)
        if isinstance(v, unreal.Rotator):
            return {"roll": round(v.roll, 4), "pitch": round(v.pitch, 4), "yaw": round(v.yaw, 4)}
        if isinstance(v, (int, float, bool, str)) or v is None:
            return v
        return str(v)

    def grab(obj, props):
        d = {}
        for p in props:
            try:
                d[p] = sval(obj.get_editor_property(p))
            except Exception:
                pass
        return d

    def dump_runtime_actors():
        w = st["w"]
        out = {"suns": [], "skylights": [], "fogs": [], "postprocess": [], "mobs": 0, "npcs": 0}
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.DirectionalLight):
            c = a.get_component_by_class(unreal.DirectionalLightComponent)
            d = {"name": a.get_name(), "rotation": sval(a.get_actor_rotation()), "forward": vec3(a.get_actor_forward_vector())}
            d.update(grab(c, ["intensity", "light_color", "light_source_angle", "contact_shadow_length",
                              "dynamic_shadow_distance_movable_light", "cast_shadows"]))
            out["suns"].append(d)
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.SkyLight):
            c = a.get_component_by_class(unreal.SkyLightComponent)
            d = {"name": a.get_name(), "location": vec3(a.get_actor_location())}
            d.update(grab(c, ["intensity", "light_color", "lower_hemisphere_color", "real_time_capture",
                              # ★ Сет E-3: точка/порог захвата неба (кубмап SkyLight = небо Lumen) — setE3_report.md §2
                              "sky_distance_threshold", "cubemap_resolution", "source_type", "lower_hemisphere_is_black", "cast_shadows"]))
            out["skylights"].append(d)
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ExponentialHeightFog):
            c = a.get_component_by_class(unreal.ExponentialHeightFogComponent)
            d = {"name": a.get_name()}
            d.update(grab(c, ["fog_density", "fog_height_falloff", "fog_inscattering_luminance", "start_distance",
                              "fog_max_opacity", "directional_inscattering_luminance",
                              # ★ Сет E-0 (06.09): объёмный туман актора (setE_plan_addendum §6 — «ПРОВЕРИТЬ дампом»)
                              "enable_volumetric_fog", "volumetric_fog_distance", "volumetric_fog_scattering_distribution"]))
            out["fogs"].append(d)
        # ★ Сет E-0 (06.09): локальные света мира (Point/Spot/Rect) — план §2: ожидание 0 на 22 картах, фиксируем числом.
        try:
            out["local_lights"] = {"point": len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PointLight)),
                                   "spot": len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.SpotLight)),
                                   "rect": len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.RectLight)),
                                   "local_comps": sum(1 for _ in unreal.ObjectIterator(unreal.LocalLightComponent)
                                                      if not _.get_outer().get_name().startswith("Default__"))}
        except Exception as ex:
            note("локальные света: %s" % ex)
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PostProcessVolume):
            d = {"name": a.get_name()}
            try:
                s = a.get_editor_property("settings")
                d.update(grab(s, ["auto_exposure_method", "auto_exposure_bias", "auto_exposure_min_brightness",
                                  "auto_exposure_max_brightness", "ambient_occlusion_intensity",
                                  "ambient_occlusion_radius", "ambient_occlusion_static_fraction"]))
            except Exception:
                pass
            out["postprocess"].append(d)
        try:
            out["mobs"] = len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaMobActor))
            out["npcs"] = len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaNpcActor))
        except Exception as ex:
            note("класс AsdaMobActor/AsdaNpcActor недоступен из Python: %s" % ex)
        R["runtime_actors"] = out
        log("рантайм-акторы: солнц=%d небо=%d туман=%d PP=%d мобов=%d NPC=%d" % (
            len(out["suns"]), len(out["skylights"]), len(out["fogs"]), len(out["postprocess"]), out["mobs"], out["npcs"]))

    def sun_to_dir():
        suns = R["runtime_actors"].get("suns", [])
        if not suns:
            return None
        f = suns[0]["forward"]
        return (-f[0], -f[1], -f[2])

    # ---------- камеры ----------
    def add_cam(name, loc, pitch, yaw, note_txt=""):
        R["cams"].append({"name": name, "loc": [round(loc[0], 1), round(loc[1], 1), round(loc[2], 1)],
                          "rot": {"roll": 0.0, "pitch": round(pitch, 3), "yaw": round(yaw, 3)},
                          "ground_z_under_cam": ground_z(loc[0], loc[1]), "note": note_txt})

    def place_from_target(T, pitch, yaw, dist, min_above=150.0):
        d = dir_from(pitch, yaw)
        loc = [T[0] - d[0] * dist, T[1] - d[1] * dist, T[2] - d[2] * dist]
        g = ground_z(loc[0], loc[1])
        if g is not None and loc[2] < g + min_above:
            loc[2] = g + min_above
        return loc

    def game_tile_to_ue(tx, tz):
        return (-(tz * 100.0 + 50.0), tx * 100.0 + 50.0)

    def ue_to_tile(x, y):
        return int(y // 100), int((-x) // 100)

    def ray_ground_hit(loc, d, maxd=20000.0, step=100.0):
        t = step
        while t <= maxd:
            x = loc[0] + d[0] * t; y = loc[1] + d[1] * t; z = loc[2] + d[2] * t
            g = ground_z(x, y)
            if g is None:
                return None
            if z <= g:
                return (x, y, g)
            t += step
        return None

    def build_cams_alpen():
        base_loc = (-39800.0, 14200.0, 3600.0); bp, by = -40.0, 45.0
        T = ray_ground_hit(base_loc, dir_from(bp, by))
        if T:
            log("цель базового ракурса (земля по лучу): %s" % (T,))
        else:
            d = dir_from(bp, by)
            T = (base_loc[0] + d[0] * 4500, base_loc[1] + d[1] * 4500, base_loc[2] + d[2] * 4500)
            note("базовый ракурс: земля по лучу не найдена, цель = 4500 юн по лучу")
        R["target"] = list(T)
        add_cam("k1_a1_0208", list(base_loc), bp, by, "ракурс 02.08 shadow_banding_fix.md (тот же, что shadow_alpen_final.png)")
        add_cam("k1_a2_yaw225", place_from_target(T, -25.0, 225.0, 4500.0), -25.0, 225.0, "та же поляна, с противоположной стороны")
        add_cam("k1_a3_low135", place_from_target(T, -10.0, 135.0, 2500.0, 200.0), -10.0, 135.0, "низкая камера, взгляд к горизонту")
        add_cam("k1_a4_yaw315", place_from_target(T, -30.0, 315.0, 3500.0), -30.0, 315.0, "четвёртая сторона поляны")
        sd = sun_to_dir()
        cand = []
        mobs = []
        try:
            mobs = unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaMobActor)
        except Exception:
            pass
        if sd and mobs and st["trees"]:
            for m in mobs:
                L = m.get_actor_location()
                b = shadow_blocker((L.x, L.y, L.z + 80.0), sd)
                if b:
                    cand.append((math.hypot(L.x - T[0], L.y - T[1]), m.get_name(), (L.x, L.y, L.z), b[1], b[2], b[0]))
            cand.sort()
            log("мобов в тени крон: %d из %d; ближайшие к поляне: %s" % (len(cand), len(mobs), [(round(c[0]), c[1], c[3]) for c in cand[:5]]))
        R["mob_shadow_candidates"] = [{"dist_to_glade": round(c[0]), "mob": c[1], "loc": [round(v, 1) for v in c[2]],
                                       "tree_mesh": c[3], "tree_actor": c[4], "ray_t": round(c[5], 1)} for c in cand[:10]]
        if cand:
            c = cand[0]
            ML = c[2]
            yaw = yaw_to(sd[0], sd[1]) + 180.0 + 35.0      # солнце за спиной камеры: моб и его тень в кадре
            loc = place_from_target((ML[0], ML[1], ML[2] + 60.0), -14.0, yaw, 750.0, 120.0)
            add_cam("k1_a5_mobshadow", loc, -14.0, yaw, "моб %s в тени кроны %s (%s), луч к солнцу перекрыт на %.0f юн" % (c[1], c[3], c[4], c[5]))
            R["mob_shadow_pick"] = {"mob": c[1], "loc": [round(v, 1) for v in ML]}
        else:
            note("моба в тени кроны не найдено (мобов=%d, деревьев=%s, солнце=%s) — кадр a5 пропущен" % (
                len(mobs), len(st["trees"] or []), sd))

    def build_cams_entry(tile, tag):
        x, y = game_tile_to_ue(*tile)
        g = ground_z(x, y)
        if g is None:
            note("вход %s: высоты нет, z=0" % (tile,)); g = 0.0
        E = (x, y, g)
        R["target"] = list(E)
        C = (-25600.0, 25600.0)
        yaw = yaw_to(C[0] - x, C[1] - y)
        d = dir_from(0.0, yaw)
        loc1 = [x - d[0] * 500.0, y - d[1] * 500.0, g + 220.0]
        g1 = ground_z(loc1[0], loc1[1])
        if g1 is not None and loc1[2] < g1 + 150.0:
            loc1[2] = g1 + 150.0
        add_cam("%s_c1_entry" % tag, loc1, -10.0, yaw, "точка входа (портал, тайл %s), из-за спины, к центру карты" % (tile,))
        loc2 = [x + d[0] * 300.0, y + d[1] * 300.0, g + 110.0]
        g2 = ground_z(loc2[0], loc2[1])
        if g2 is not None:
            loc2[2] = g2 + 110.0
        add_cam("%s_c2_lowhorizon" % tag, loc2, 1.0, yaw, "низкая камера (110 юн над землёй) к горизонту через долину")
        try:
            near = []
            for m in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaMobActor):
                L = m.get_actor_location()
                near.append((round(math.hypot(L.x - x, L.y - y)), m.get_name()))
            near.sort()
            R["nearest_mobs"] = near[:5]
            log("ближайшие мобы к входу: %s" % R["nearest_mobs"])
        except Exception:
            pass

    def build_cams_flamion():
        n = int(42000 / GRID_STEP) + 1
        xs = [-47000.0 + i * GRID_STEP for i in range(n)]
        ys = [5000.0 + j * GRID_STEP for j in range(n)]
        H = [[ground_z(x, y) for y in ys] for x in xs]
        rows = []
        for i in range(1, n - 1):
            for j in range(1, n - 1):
                h = H[i][j]
                if h is None or None in (H[i - 1][j], H[i + 1][j], H[i][j - 1], H[i][j + 1]):
                    continue
                gx = (H[i + 1][j] - H[i - 1][j]) / (2 * GRID_STEP)
                gy = (H[i][j + 1] - H[i][j - 1]) / (2 * GRID_STEP)
                slope = math.degrees(math.atan(math.hypot(gx, gy)))
                if slope < 15.0 or slope > 50.0:
                    continue
                ln = math.hypot(gx, gy) or 1.0
                dn = (-gx / ln, -gy / ln)
                drops = []
                for k in (2000.0, 4000.0, 6000.0):
                    hz = ground_z(xs[i] + dn[0] * k, ys[j] + dn[1] * k)
                    if hz is not None:
                        drops.append(h - hz)
                drop = max(drops) if drops else 0.0
                rows.append((drop + slope * 10.0, slope, drop, xs[i], ys[j], h, dn))
        rows.sort(reverse=True)
        R["slope_candidates"] = [{"score": round(r[0]), "slope_deg": round(r[1], 1), "drop": round(r[2]),
                                  "x": r[3], "y": r[4], "z": round(r[5], 1), "downhill": [round(r[6][0], 3), round(r[6][1], 3)]} for r in rows[:8]]
        log("кандидаты склона: %s" % R["slope_candidates"][:4])
        if not rows:
            note("склон не найден по сетке высот — камеры у входа (485,71)")
            build_cams_entry((485, 71), "k3")
            return
        _, slope, drop, x, y, z, dn = rows[0]
        R["target"] = [x, y, z]
        yaw = yaw_to(dn[0], dn[1])
        add_cam("k3_s1_downslope", [x - dn[0] * 200.0, y - dn[1] * 200.0, z + 260.0], -12.0, yaw,
                "склон %.0f°, перепад вниз %.0f юн; взгляд вниз по склону к долине" % (slope, drop))
        yaw2 = yaw + 90.0
        add_cam("k3_s2_side", place_from_target((x, y, z + 100.0), -28.0, yaw2, 2800.0, 200.0), -28.0, yaw2,
                "тот же склон сбоку-сверху (персонаж на крутом склоне)")

    def build_contact_cams():
        """Контактные камеры ближнего плана (ASDA_SET0_CONTACT) — по MIC/мешам живого мира, только чтение."""
        if not CONTACT:
            return
        T = R.get("target") or [0.0, 0.0, 0.0]
        sd = sun_to_dir()
        specs = []
        opts = {}
        for item in CONTACT:
            label, _, rx = item.partition(":")
            rx, *mods = rx.split("@@")            # "regex@@zfrac@@distmul": zfrac — доля высоты bounds для точки взгляда (0.5 = центр; ствол ~0.2), distmul — множитель дистанции (1.8)
            opts[label.strip()] = (float(mods[0]) if len(mods) > 0 and mods[0] else 0.5, float(mods[1]) if len(mods) > 1 and mods[1] else 1.8)
            specs.append((label.strip(), rx.strip()))
        found = {lb: None for lb, _ in specs}
        n_comp = 0
        for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.Actor):
            try:
                comps = a.get_components_by_class(unreal.StaticMeshComponent)
            except Exception:
                continue
            for c in comps:
                try:
                    sm = c.static_mesh
                except Exception:
                    sm = None
                if not sm:
                    continue
                n_comp += 1
                mats = []
                try:
                    for mi in range(c.get_num_materials()):
                        mat = c.get_material(mi)
                        if mat:
                            mats.append(mat.get_name())
                except Exception:
                    pass
                hay = (" ".join(mats) + " " + sm.get_name()).lower()
                hits = [(lb, rx) for lb, rx in specs if rx != "@ground" and re.search(rx, hay)]
                if not hits:
                    continue
                b = sm.get_bounds(); O = b.origin; rad = b.sphere_radius; EZ = b.box_extent.z
                inst = []
                if isinstance(c, unreal.InstancedStaticMeshComponent):
                    for i in range(c.get_instance_count()):
                        tr = c.get_instance_transform(i, True)
                        if isinstance(tr, tuple):
                            if not tr[0]:
                                continue
                            tr = tr[1]
                        L = tr.translation; S = tr.scale3d
                        inst.append(((L.x + O.x * S.x, L.y + O.y * S.y, L.z + O.z * S.z), rad * max(abs(S.x), abs(S.y), abs(S.z)), EZ * abs(S.z)))
                else:
                    L = c.get_world_location(); S = c.get_world_scale()
                    inst.append(((L.x + O.x * S.x, L.y + O.y * S.y, L.z + O.z * S.z), rad * max(abs(S.x), abs(S.y), abs(S.z)), EZ * abs(S.z)))
                for (cx, cy, cz), r, ez in inst:
                    dist = math.hypot(cx - T[0], cy - T[1])
                    for lb, rx in hits:
                        if found[lb] is None or dist < found[lb][0]:
                            found[lb] = (dist, (cx, cy, cz), r, mats[:3], sm.get_name(), a.get_name(), ez)
        log("контактные камеры: компонентов просмотрено %d" % n_comp)
        for lb, rx in specs:
            if rx == "@ground":
                yaw = 45.0 if not sd else yaw_to(sd[0], sd[1]) + 180.0
                d = dir_from(0.0, yaw)
                loc = [T[0] - d[0] * 140.0, T[1] - d[1] * 140.0, (ground_z(T[0] - d[0] * 140.0, T[1] - d[1] * 140.0) or T[2]) + 170.0]
                pitch = -math.degrees(math.atan2(loc[2] - T[2], 140.0))
                add_cam("contact_%s" % lb, loc, pitch, yaw, "крупный план земли у цели (террейн)")
                R["cams"][-1]["contact"] = {"label": lb, "regex": rx}
                continue
            f = found.get(lb)
            if not f:
                note("контакт %s (/%s/): подходящего объекта в мире нет" % (lb, rx))
                continue
            dist_t, (cx, cy, cz), r, mats, mesh, actor, ez = f
            zfrac, dmul = opts.get(lb, (0.5, 1.8))
            cz = cz - ez + 2.0 * ez * zfrac          # точка взгляда по высоте bounds (0 = низ, 0.5 = центр, 1 = верх)
            dist = max(150.0, min(1200.0, dmul * max(r, 40.0)))
            if sd and (abs(sd[0]) + abs(sd[1])) > 1e-3:
                hd = (sd[0], sd[1]); ln = math.hypot(*hd); hd = (hd[0] / ln, hd[1] / ln)      # со стороны солнца
            else:
                hd = (T[0] - cx, T[1] - cy); ln = math.hypot(*hd) or 1.0; hd = (hd[0] / ln, hd[1] / ln)
            loc = [cx + hd[0] * dist, cy + hd[1] * dist, cz + 0.35 * dist]
            g = ground_z(loc[0], loc[1])
            if g is not None and loc[2] < g + 80.0:
                loc[2] = g + 80.0
            pitch = -math.degrees(math.atan2(loc[2] - cz, dist))
            yaw = yaw_to(cx - loc[0], cy - loc[1])
            add_cam("contact_%s" % lb, loc, pitch, yaw, "контакт %s: %s (%s, %s) r=%.0f, до цели %.0f" % (lb, mats, mesh, actor, r, dist_t))
            R["cams"][-1]["contact"] = {"label": lb, "regex": rx, "materials": mats, "mesh": mesh, "actor": actor, "center": [round(cx, 1), round(cy, 1), round(cz, 1)], "radius": round(r, 1)}
            log("контакт %s: %s" % (lb, R["cams"][-1]["note"]))

    def build_cams():
        if CAMS_IN and os.path.isfile(CAMS_IN):
            src = json.load(open(CAMS_IN, encoding="utf-8"))
            only = os.environ.get("ASDA_SET0_ONLY", "")
            R["cams"] = [{k: c[k] for k in ("name", "loc", "rot", "note")} for c in src["cams"]
                         if not only or c["name"] in only.split(",")]
            R["cam_index_offset"] = [c["name"] for c in src["cams"]].index(R["cams"][0]["name"]) if R["cams"] else 0
            R["target"] = src.get("target")
            note("камеры взяты из %s (%d шт., фильтр=%r)" % (CAMS_IN, len(R["cams"]), only))
            build_contact_cams()          # ★ сет C-2: контактные камеры добавляются и к камерам «из файла»
            return
        if KEY == "alpen":
            build_cams_alpen()
        elif KEY == "nightvalley":
            build_cams_entry((188, 465), "k2")
            # вход лежит в каньоне у края карты (кадр чёрный) → третий ракурс из самой долины,
            # у NPC-возврата mdel_recall00 (npc_manifest: тайл 395,88), низко, к центру карты
            x, y = game_tile_to_ue(395, 88)
            g = ground_z(x, y) or 0.0
            yaw = yaw_to(-25600.0 - x, 25600.0 - y)
            d = dir_from(0.0, yaw)
            loc3 = [x - d[0] * 400.0, y - d[1] * 400.0, (ground_z(x - d[0] * 400.0, y - d[1] * 400.0) or g) + 160.0]
            add_cam("k2_c3_valley_npc", loc3, -6.0, yaw, "долина у NPC-возврата (тайл 395,88), низкая камера к центру карты")
        elif KEY == "chess":
            build_cams_entry((44, 459), "k6")
        elif KEY == "flamion":
            build_cams_flamion()
        elif KEY in INDOOR_ENTRY:         # ★ 06.09 (indoor_fog): закрытые карты — вход игрока, из-за спины + низкий горизонт
            build_cams_entry(*INDOOR_ENTRY[KEY])
        elif KEY == "mine":               # ★ Сет E-1 (06.09): входа в PortalIP_Info нет — ближайшая к центру проходимая клетка WalkGrid
            best = None
            for tx in range(8, 504, 8):
                for tz in range(8, 504, 8):
                    x, y = game_tile_to_ue(tx, tz)
                    if ground_z(x, y) is None:
                        continue
                    d2 = (tx - 256) ** 2 + (tz - 256) ** 2
                    if best is None or d2 < best[0]:
                        best = (d2, tx, tz)
            tile = (best[1], best[2]) if best else (256, 256)
            note("mine: проходимая клетка у центра карты = %s" % (tile,))
            build_cams_entry(tile, "k11")
        build_contact_cams()              # ★ сет C-2
        log("камер: %d" % len(R["cams"]))
        for c in R["cams"]:
            log("  %s loc=%s rot=%s — %s" % (c["name"], c["loc"], c["rot"], c["note"]))

    # ---------- павн ----------
    def spawn_self():
        T = R.get("target")
        if NOPAWN or not T:
            return
        st["dp"] = unreal.GameplayStatics.get_player_pawn(st["w"], 0)
        try:
            lm = None
            for o in unreal.ObjectIterator(unreal.AsdaLoadManagerSubsystem):   # 5.8: SubsystemBlueprintLibrary в Python нет
                lm = o
            if lm is None:
                raise RuntimeError("ObjectIterator не нашёл UAsdaLoadManagerSubsystem")
            log("LoadManager %s: current_map_id=%s" % (lm.get_name(), lm.get_editor_property("current_map_id")))
            lm.set_editor_property("current_map_id", MAPID)
            log("LoadManager: current_map_id выставлен = %s" % lm.get_editor_property("current_map_id"))
        except Exception as ex:
            note("CurrentMapId не выставить из Python (%s) — павн не появится" % ex)
        tx, tz = ue_to_tile(T[0], T[1])
        if SPAWN_TILE:
            tx, tz = [int(v) for v in SPAWN_TILE.split(",")]   # ★ доводка сета B: тайл спавна задан явно (герой рядом с мобами)
            note("тайл спавна героя из ASDA_SET0_SPAWN_TILE: %s" % ((tx, tz),))
        R["pawn"] = {"tile": [tx, tz], "spawned": False}
        cc("Asda.World.FakeSelfSpawn %d %d Set0" % (tx, tz))

    def after_spawn():
        w = st["w"]
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        p = unreal.GameplayStatics.get_player_pawn(w, 0)
        dp = st["dp"]
        log("павн после спавна: %s (был %s)" % (p.get_name() if p else None, dp.get_name() if dp else None))
        if BASE_ACTIONS:          # ★ Сет E-2: базовое состояние прогона — до всех камер
            apply_actions(BASE_ACTIONS, "base")
        chars = []
        try:
            chars = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaPlayerCharacter)
        except Exception:
            pass
        if chars:
            R["pawn"]["spawned"] = True
            R["pawn"]["char_loc"] = vec3(chars[0].get_actor_location())
            R["pawn"]["char_name"] = chars[0].get_name()
            log("свой персонаж в мире: %s @ %s" % (chars[0].get_name(), R["pawn"]["char_loc"]))
            # ★ Сет B (05.09): диагностика капсульной тени ПУТЁМ ИГРОКА — флаги скелетальных компонентов героя
            #   (cast_shadow / cast_capsule_indirect_shadow / MinVisibility / меш / ShadowPhysicsAsset). Только чтение.
            diag = []
            try:
                for comp in chars[0].get_components_by_class(unreal.SkeletalMeshComponent):
                    sk = comp.get_editor_property("skeletal_mesh_asset")
                    spa = sk.get_editor_property("shadow_physics_asset") if sk else None
                    d = {"comp": comp.get_name(), "visible": bool(comp.is_visible()),
                         "cast_shadow": bool(comp.get_editor_property("cast_shadow")),
                         "capsule_indirect": bool(comp.get_editor_property("cast_capsule_indirect_shadow")),
                         "capsule_direct": bool(comp.get_editor_property("cast_capsule_direct_shadow")),
                         "min_vis": float(comp.get_editor_property("capsule_indirect_shadow_min_visibility")),
                         "mesh": sk.get_path_name() if sk else None, "shadow_pa": spa.get_path_name() if spa else None,
                         "world_scale": vec3(comp.get_component_to_world().scale3d) if hasattr(comp, "get_component_to_world") else None}
                    diag.append(d)
                    log("капсулы героя: %s" % json.dumps(d, ensure_ascii=False))
            except Exception as ex:
                note("диагностика капсул героя не удалась: %s" % ex)
            R["pawn"]["capsule_diag"] = diag
        delayed = []
        for c in EXEC_POST:   # ★ доводка сета B: команды после спавна (asda.Shadow.CapsuleDump и т.п.)
            # ★ сет D: префикс "@N " — выполнить через N кадров, ПЕРЕД камерами героя (slomo 0.001 сразу после спавна замораживал столб
            #   эффекта появления поверх героя — кадры skinoff/sunA2 12:35 негодны); ставится в голову очереди в конце after_spawn
            if c.startswith("@"):
                n_str, _, cmd = c[1:].partition(" ")
                try:
                    delayed.append([int(n_str), cmd.strip()])
                except ValueError:
                    cc(c)
                continue
            cc(c)
        # ★ Сет D: рантайм-MID на мешах героя со скалярами из ASDA_SET0_HERO_MID (A/B roughness по маске без правки ассетов)
        if HERO_MID and chars:
            n_mid = 0
            try:
                kv = []
                for spec in HERO_MID:
                    k, _, v = spec.partition("=")
                    kv.append((k.strip(), float(v)))
                for comp in chars[0].get_components_by_class(unreal.MeshComponent):
                    for i in range(comp.get_num_materials()):
                        if comp.get_material(i) is None:
                            continue
                        mid = comp.create_dynamic_material_instance(i)
                        if mid is None:
                            continue
                        for k, v in kv:
                            mid.set_scalar_parameter_value(k, v)
                        n_mid += 1
                R["pawn"]["hero_mid"] = {"params": kv, "slots": n_mid}
                log("MID героя: %d слотов, параметры %s" % (n_mid, kv))
            except Exception as ex:
                note("MID героя не поставлен: %s" % ex)
        # ★ Сет C-1: камера «лицо героя» — по фактическому положению/повороту персонажа; ставится В НАЧАЛО очереди
        #   (after() дописывает в конец — после finish() кадр бы не снялся).
        if HERO_CAM and chars:
            n0 = len(st["q"])
            for spec in [x.strip() for x in HERO_CAM.split(";") if x.strip()]:     # ★ сет C-2: несколько камер героя "d,p,z,yaw|метка"
                try:
                    nums, _, label = spec.partition("|")
                    label = label.strip() or "hero"
                    vals = [float(v) for v in nums.split(",")]
                    dist, pitch, head_z = vals[:3]
                    yaw_off = vals[3] if len(vals) > 3 else 0.0   # ★ меш персонажа повёрнут относительно актора: лицо смотрит в actor_yaw+90 (кадр before_setC1 — профиль)
                    L = chars[0].get_actor_location(); fwd = chars[0].get_actor_rotation().yaw + yaw_off
                    fd = dir_from(0.0, fwd)
                    head = (L.x, L.y, L.z + head_z)
                    loc = [head[0] + fd[0] * dist, head[1] + fd[1] * dist, head[2] + math.tan(math.radians(abs(pitch))) * dist]   # смотрим вниз на голову под |pitch|
                    cam_yaw = (fwd + 180.0) % 360.0
                    c = {"name": "hero_%s" % label, "loc": [round(v, 1) for v in loc], "rot": {"roll": 0.0, "pitch": round(pitch, 3), "yaw": round(cam_yaw, 3)},
                         "ground_z_under_cam": ground_z(loc[0], loc[1]),
                         "note": "герой %s (%s): %.0f юн перед персонажем (yaw героя %.1f), взгляд на char_loc+%.0f" % (chars[0].get_name(), label, dist, fwd, head_z)}
                    R["cams"].append(c)
                    schedule_camera(len(R["cams"]) - 1, c, label="set0_%s_%s_%s" % (KEY, label, TAG))
                    log("камера героя добавлена: %s" % c)
                except Exception as ex:
                    note("камера героя не поставлена (%s): %s" % (spec, ex))
            new = st["q"][n0:]; del st["q"][n0:]; st["q"][0:0] = new      # в НАЧАЛО очереди (после finish() кадр бы не снялся)
        if delayed:   # ★ сет D: отложенные команды — раньше камер героя
            st["q"][0:0] = [[d, (lambda cmd=cmd: (log("cmd(@%d): %s" % (d, cmd)), cc(cmd)))] for d, cmd in delayed]
        if dp and p and p != dp:
            # персонажа заняли под управление → вернуть DefaultPawn, чтобы BugItGo двигал КАМЕРУ, а не героя
            try:
                pc.possess(dp)
                log("возвращено управление DefaultPawn (персонаж остаётся в мире)")
            except Exception as ex:
                note("вернуть DefaultPawn не удалось: %s — BugItGo будет двигать героя" % ex)

    # ---------- камера / экранные точки ----------
    def unhide_mobs_near(c, radius=2500.0):
        """Только для кадра «моб в тени»: акторы мобов офлайн стоят hidden (ждут adoption 4013) —
        раскрываем ТОЛЬКО мобов у камеры, только в рантайме этого прогона (ассеты не трогаются)."""
        if "mobshadow" not in c["name"] and UNHIDE_MOBS <= 0:
            return
        if UNHIDE_MOBS > 0:
            radius = max(radius, UNHIDE_MOBS)   # ★ сет D: у всех камер, радиус из окружения
        try:
            shown = []
            for m in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaMobActor):
                L = m.get_actor_location()
                if math.hypot(L.x - c["loc"][0], L.y - c["loc"][1]) <= radius:
                    m.set_actor_hidden_in_game(False)
                    shown.append((m.get_name(), vec3(L)))
            c["mobs_unhidden"] = shown
            log("раскрыто мобов у камеры %s: %s" % (c["name"], shown))
        except Exception as ex:
            note("раскрыть мобов не удалось: %s" % ex)

    def set_view(c):
        unhide_mobs_near(c)
        cc("BugItGo %.1f %.1f %.1f %.3f %.3f 0" % (c["loc"][0], c["loc"][1], c["loc"][2], c["rot"]["pitch"], c["rot"]["yaw"]))

    def record_view(c):
        try:
            pc = unreal.GameplayStatics.get_player_controller(st["w"], 0)
            cm = pc.player_camera_manager
            c["camera_actual"] = {"loc": vec3(cm.get_camera_location()), "rot": sval(cm.get_camera_rotation()),
                                  "fov": cm.get_fov_angle()}
            log("камера факт: %s" % c["camera_actual"])
        except Exception as ex:
            log("камера факт: %s" % ex)

    def screen_points(c):
        pc = unreal.GameplayStatics.get_player_controller(st["w"], 0)
        pts = {}
        try:
            feet = None
            if R.get("pawn") and R["pawn"].get("char_loc"):
                L = R["pawn"]["char_loc"]; feet = V(L[0], L[1], L[2] - 80.0)
            elif R.get("target"):
                T = R["target"]; feet = V(T[0], T[1], T[2])
            def proj(v):
                r = pc.project_world_location_to_screen(v, False)
                if r is None:
                    return None
                if isinstance(r, tuple):
                    return [round(r[1].x, 1), round(r[1].y, 1)] if r[0] else None
                return [round(r.x, 1), round(r.y, 1)]
            if feet:
                pts["feet"] = proj(feet)
            d = dir_from(0.0, c["rot"]["yaw"])
            far = V(c["loc"][0] + d[0] * 200000.0, c["loc"][1] + d[1] * 200000.0, c["loc"][2])
            pts["horizon"] = proj(far)
            vp = unreal.WidgetLayoutLibrary.get_viewport_size(st["w"])
            pts["viewport"] = [vp.x, vp.y]
        except Exception as ex:
            log("экранные точки: %s" % ex)
        return pts

    # ---------- скриншоты ----------
    new_pngs = PH.make_new_pngs(SSDIR, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    def take(dest):
        new = new_pngs()
        if not new:
            note("нет нового PNG для %s" % dest); return None
        shutil.copy(new[-1], dest); log("СОХРАНЁН -> %s (%d б)" % (dest, os.path.getsize(dest)))
        return dest

    def take_many(dests):
        new = new_pngs()
        if len(new) < len(dests):
            note("ожидалось %d PNG, получено %d для %s" % (len(dests), len(new), dests))
        for src, dst in zip(new[-len(dests):], dests):
            shutil.copy(src, dst); log("СОХРАНЁН -> %s" % dst)

    # ---------- планировщик ----------
    after = PH.make_after_frames(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 2 стр.)

    run_queue = PH.make_frame_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 8 стр.)

    def schedule_camera(i, c, label=None):
        i = i + R.get("cam_index_offset", 0)
        name = label or ("set0_%s_%d_%s" % (KEY, i + 1, TAG))
        after(1, lambda: set_view(c))
        after(SETTLE, lambda: (record_view(c), log("SHOT %s %s" % (KEY, c["name"])), cc("HighResShot 1920x1080")))
        after(40, lambda: c.__setitem__("png", take(os.path.join(OUTD, name + ".png"))))
        after(2, lambda: c.__setitem__("screen", screen_points(c)))
        after(2, lambda: (log("PROFILE %s %d %s" % (KEY, i + 1, c["name"])), cc("ProfileGPU")))
        if DF_STATS:   # ★ Сет E-1: статистика атласа DF + SDF-ассеты сцены (один кадр, движок сам обнуляет ключ)
            after(2, lambda: cc("r.DistanceFields.LogAtlasStats 2"))
        after(20, lambda: c.__setitem__("frame_ms_avg60", round(1000.0 * sum(st["dt"][-60:]) / max(1, len(st["dt"][-60:])), 2)))
        after(1, lambda: (cc("stat unit"), cc("stat gpu")))
        after(40, lambda: cc("Shot showui"))
        after(20, lambda: c.__setitem__("png_stats", take(os.path.join(OUTD, name + "_stats.png"))))
        after(1, lambda: cc("stat none"))
        if VIS:   # ★ Сет C-1: экранный визуализатор буфера (например материальный AO в GBufferC.A)
            after(1, lambda: cc("vis %s" % VIS))
            after(40, lambda: cc("Shot"))
            after(30, lambda: c.__setitem__("png_vis", take(os.path.join(OUTD, name + "_vis.png"))))
            after(1, lambda: cc("vis off"))
            after(10, lambda: None)
        if FLICKER:    # ★ Сет E-3: соседние кадры для меры мерцания (без смены камеры; облака заморожены ASDA_SET0_FREEZE_CLOUDS)
            for k in range(FLICKER):
                after(1, lambda: cc("Shot"))
            after(30, lambda: take_many([os.path.join(OUTD, name + "_fl%d.png" % k) for k in range(FLICKER)]))
        for sf in SHOWFLAGS:   # ★ Сет E-1: визуализаторы ShowFlag (DF мешей / глобальный SDF) — по кадру на флаг
            flag, _, mode = sf.partition(":")     # ★ Сет E-3: "VisualizeLumen:3" / "VisualizeLumen:card"
            sfx = flag + (("Card" if mode == "card" else mode) if mode else "")
            if mode == "card":
                after(1, lambda: cc("r.Lumen.Visualize.CardPlacement 1"))
            elif mode:
                after(1, lambda mode=mode: cc("r.Lumen.Visualize %s" % mode))
            after(1, lambda flag=flag: cc("ShowFlag.%s 1" % flag))
            after(60 if flag == "VisualizeLumen" else 40, lambda: cc("Shot"))
            after(30, lambda sfx=sfx: c.__setitem__("png_sf_" + sfx, take(os.path.join(OUTD, name + "_sf_%s.png" % sfx))))
            after(1, lambda flag=flag: cc("ShowFlag.%s 0" % flag))
            if mode == "card":
                after(1, lambda: cc("r.Lumen.Visualize.CardPlacement 0"))
            elif mode:
                after(1, lambda: cc("r.Lumen.Visualize 0"))
            after(10, lambda: None)
        if KEY == "alpen" and i == 0 and not label:
            after(1, lambda: cc("ShowFlag.VisualizeSSAO 1"))
            after(40, lambda: cc("Shot"))
            after(1, lambda: cc("Shot"))
            after(30, lambda: take_many([os.path.join(OUTD, "set0_alpen_1_%s_ao_ssao_f0.png" % TAG),
                                         os.path.join(OUTD, "set0_alpen_1_%s_ao_ssao_f1.png" % TAG)]))
            after(1, lambda: cc("ShowFlag.VisualizeSSAO 0"))
            after(10, lambda: cc("vis ScreenSpaceAO UV0"))
            after(40, lambda: cc("Shot"))
            after(1, lambda: cc("Shot"))
            after(30, lambda: take_many([os.path.join(OUTD, "set0_alpen_1_%s_ao_vis_f0.png" % TAG),
                                         os.path.join(OUTD, "set0_alpen_1_%s_ao_vis_f1.png" % TAG)]))
            after(1, lambda: cc("vis off"))
            after(10, lambda: None)

    def finish():
        R["frame_ms_overall_avg"] = round(1000.0 * sum(st["dt"][-600:]) / max(1, len(st["dt"][-600:])), 2)
        R["trees_count"] = len(st["trees"] or [])
        # ★ Сет A (05.09): кадры «после» пишут СВОЙ json (set0_<key>_cams_<tag>.json), чтобы не затирать
        #   камеры/экранные точки «до» (set0_<key>_cams.json — источник для ASDA_SET0_CAMS и set0_metrics.py).
        out = os.path.join(OUTD, "set0_%s_cams.json" % KEY if TAG == "before" else "set0_%s_cams_%s.json" % (KEY, TAG))
        with open(out, "w", encoding="utf-8") as f:
            json.dump(R, f, ensure_ascii=False, indent=1)
        log("записано %s" % out)
        log("ЗАВЕРШЕНО")
        cc("quit")

    def phase_init():
        st["w"] = find_world()
        if st["w"] is None:
            unreal.log_error("[set0] МИР НЕ НАЙДЕН для " + MAP)
            unreal.SystemLibrary.execute_console_command(None, "quit")
            return
        log("мир: %s" % st["w"].get_name())
        os.makedirs(OUTD, exist_ok=True)
        st["seen"] = set(glob.glob(os.path.join(SSDIR, "**", "*.png"), recursive=True))
        cc("r.SetRes 1920x1080w")
        cc("r.ProfileGPU.ShowUI 0")
        for cv in CVARS:
            cc(cv)
        # ★ Сет B (05.09): ASDA_SET0_EXEC="cmd1;cmd2" — доп. консольные команды перед съёмкой (A/B ключей без пересборки,
        #   напр. asda.Shadow.CapsuleMinVisibility 0.5). Только рантайм этого прогона, ассеты не трогаются.
        for c in [x.strip() for x in os.environ.get("ASDA_SET0_EXEC", "").split(";") if x.strip()]:
            cc(c)
        load_grid()
        if FREEZE_CLOUDS:
            freeze_clouds()
        if MPC_SET:
            after(30, apply_mpc)        # после BeginPlay акторов карты (Rim_<map> пишет свои значения на старте мира)
        after(90, phase_actors)

    def _is_terrain_smc(comp):
        try:
            m = comp.get_editor_property("static_mesh")
            return m is not None and m.get_name().lower() == "sm_terrain"
        except Exception:
            return False

    def _is_foliage_ism(comp):
        """Листва — по имени МЕША компонента (SM_<модель>, asda_foliage.is_foliage_asset): имя компонента в -game не I_<модель>
        (первый прогон E-2: «компонентов 0»)."""
        try:
            import sys as _sys
            _td = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools"     # __file__ в -ExecCmds=py не задан; каталог Tools в sys.path не попадает
            if _td not in _sys.path:
                _sys.path.insert(0, _td)
            import asda_foliage as _FOL
            m = comp.get_editor_property("static_mesh")
            if m is not None and _FOL.is_foliage_asset(m.get_name()):
                return True
            n = comp.get_name()
            return n.startswith("I_") and _FOL.is_foliage_model(n[2:])
        except Exception as ex:
            if not st.get("_fol_err"):
                st["_fol_err"] = True
                log("листва: признак не работает: %s" % ex)
            return False

    def _live_comps(cls):
        """Компоненты класса cls ЭТОГО мира (без CDO/архетипов)."""
        w = st["w"]
        for comp in unreal.ObjectIterator(cls):
            try:
                if comp.get_outer().get_name().startswith("Default__") or comp.get_world() != w:
                    continue
            except Exception:
                continue
            yield comp

    def apply_actions(actions, where="base"):
        """★ Сет E-2: действия A/B в рантайме (см. шапку ASDA_SET0_BASE/VARIANTS). Результат каждого — в лог и в R["actions"]."""
        w = st["w"]
        done = []
        for act in actions:
            kind, _, arg = act.partition("=")
            kind = kind.strip().lower(); arg = arg.strip()
            try:
                if kind == "cc":
                    cc(arg); done.append((act, "ok"))
                elif kind == "sky":
                    prop, _, val = arg.partition("=")
                    prop = prop.strip(); n = 0
                    v = (val.strip().lower() in ("1", "true", "on")) if prop == "cast_shadows" else float(val)
                    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.SkyLight):
                        c = a.get_component_by_class(unreal.SkyLightComponent)
                        c.set_editor_property(prop, v)
                        c.set_visibility(False); c.set_visibility(True)      # пересоздать прокси (OcclusionMaxDistance читается только там)
                        n += 1
                        done.append((act, "%s: %s=%s" % (a.get_name(), prop, c.get_editor_property(prop))))
                    if n == 0:
                        done.append((act, "SkyLight не найден"))
                elif kind == "dfl":
                    what, _, val = arg.partition("=")
                    what = what.strip(); on = val.strip() in ("1", "true", "on"); n = 0
                    if what == "foliage":
                        seen = 0; sample = []
                        for comp in _live_comps(unreal.InstancedStaticMeshComponent):
                            seen += 1
                            if len(sample) < 4:
                                try:
                                    m = comp.get_editor_property("static_mesh")
                                    sample.append((comp.get_name(), comp.get_class().get_name(), m.get_name() if m else None))
                                except Exception as ex:
                                    sample.append((comp.get_name(), "?", str(ex)))
                            if _is_foliage_ism(comp):
                                comp.set_affect_distance_field_lighting(on); n += 1
                        log("dfl=foliage: ISM в мире %d, образцы %s" % (seen, sample))
                    else:
                        for comp in _live_comps(unreal.StaticMeshComponent):
                            if _is_terrain_smc(comp) and not isinstance(comp, unreal.InstancedStaticMeshComponent):
                                comp.set_affect_distance_field_lighting(on); n += 1
                    done.append((act, "компонентов %d -> affect_distance_field_lighting=%s" % (n, on)))
                elif kind == "tmid":
                    k, _, val = arg.partition("="); n = 0
                    for comp in _live_comps(unreal.StaticMeshComponent):
                        if not _is_terrain_smc(comp):
                            continue
                        for i in range(comp.get_num_materials()):
                            if comp.get_material(i) is None:
                                continue
                            mid = comp.create_dynamic_material_instance(i)
                            if mid is not None:
                                mid.set_scalar_parameter_value(k.strip(), float(val)); n += 1
                    done.append((act, "MID террейна: слотов %d, %s=%s" % (n, k.strip(), val)))
                elif kind == "ppv":
                    prop, _, val = arg.partition("="); prop = prop.strip(); n = 0
                    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PostProcessVolume):
                        sset = a.get_editor_property("settings")
                        try:
                            sset.set_editor_property("override_" + prop, True)
                        except Exception:
                            pass
                        sset.set_editor_property(prop, float(val))
                        a.set_editor_property("settings", sset)
                        n += 1
                    done.append((act, "PPV %d: %s=%s" % (n, prop, val)))
                # ★ Сет E-3 (07.09.2026, Lumen/небо): skyp=<поле>=<знач> — ЛЮБОЕ поле всех SkyLightComponent (float/int/bool по виду значения),
                #   без пересоздания прокси; skyloc=x,y,z | cam | cam+dz — переставить актор(ы) SkyLight (точка захвата captured-scene =
                #   позиция компонента: внутри купола SkyDome, который следует за камерой); skycap — recapture_sky() у всех SkyLight (Movable
                #   captured-scene: новый захват на следующем тике мира, SkyLightComponent.cpp UpdateSkyCaptureContents);
                #   sun=intensity_mul=<k> — сила всех DirectionalLight ×k от АВТОРСКОЙ (кэш первого вызова; k=1 возвращает); sun=intensity=<abs>.
                elif kind == "skyp":
                    prop, _, val = arg.partition("="); prop = prop.strip(); vs = val.strip(); n = 0
                    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.SkyLight):
                        c = a.get_component_by_class(unreal.SkyLightComponent)
                        cur = c.get_editor_property(prop)
                        if isinstance(cur, bool):
                            v = vs.lower() in ("1", "true", "on")
                        elif isinstance(cur, int):
                            v = int(float(vs))
                        elif isinstance(cur, float):
                            v = float(vs)
                        else:
                            v = vs
                        c.set_editor_property(prop, v); n += 1
                        done.append((act, "%s: %s %s -> %s" % (a.get_name(), prop, cur, c.get_editor_property(prop))))
                    if n == 0:
                        done.append((act, "SkyLight не найден"))
                elif kind == "skyloc":
                    a0 = arg.strip().lower(); dz = 0.0
                    if a0.startswith("cam"):
                        rest = a0[3:].replace("+", ",").strip(",")
                        dz = float(rest) if rest else 0.0
                        cm = unreal.GameplayStatics.get_player_camera_manager(w, 0)
                        L = cm.get_camera_location(); loc = unreal.Vector(L.x, L.y, L.z + dz)
                    else:
                        x, y, z = [float(t) for t in a0.split(",")]; loc = unreal.Vector(x, y, z)
                    n = 0
                    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.SkyLight):
                        was = a.get_actor_location(); a.set_actor_location(loc, False, False); n += 1
                        done.append((act, "%s: %s -> %s" % (a.get_name(), vec3(was), vec3(a.get_actor_location()))))
                    if n == 0:
                        done.append((act, "SkyLight не найден"))
                elif kind == "skycap":
                    n = 0
                    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.SkyLight):
                        c = a.get_component_by_class(unreal.SkyLightComponent); c.recapture_sky(); n += 1
                    done.append((act, "recapture_sky у %d SkyLight" % n))
                elif kind == "sun":
                    prop, _, val = arg.partition("="); prop = prop.strip(); v = float(val); n = 0
                    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.DirectionalLight):
                        c = a.get_component_by_class(unreal.DirectionalLightComponent)
                        base = st.setdefault("sun_base", {}).setdefault(a.get_name(), float(c.get_editor_property("intensity")))
                        newv = base * v if prop == "intensity_mul" else v
                        c.set_intensity(newv); n += 1
                        done.append((act, "%s: intensity авторская %.4f -> %.4f" % (a.get_name(), base, c.get_editor_property("intensity"))))
                    if n == 0:
                        done.append((act, "DirectionalLight не найден"))
                else:
                    done.append((act, "неизвестный вид действия"))
            except Exception as ex:
                done.append((act, "ОШИБКА %s" % ex))
        for a, r in done:
            log("ACTION[%s] %s -> %s" % (where, a, r))
        R.setdefault("actions", {}).setdefault(where, []).extend(done)

    def apply_mpc():
        """★ Сет D: значения скаляров коллекции в экземпляре этого мира (диагностика/A/B рима)."""
        try:
            path, _, kvs = MPC_SET.partition(":")
            mpc = unreal.load_object(None, path)
            ml = getattr(unreal, "MaterialLibrary", None) or getattr(unreal, "KismetMaterialLibrary", None)
            done = []
            for kv in [x.strip() for x in kvs.split(";") if x.strip()]:
                k, _, v = kv.partition("=")
                ml.set_scalar_parameter_value(st["w"], mpc, k.strip(), float(v))
                done.append((k.strip(), float(v), round(float(ml.get_scalar_parameter_value(st["w"], mpc, k.strip())), 4)))
            R["mpc_set"] = done
            log("MPC %s: %s (имя, задано, прочитано)" % (path, done))
        except Exception as ex:
            note("MPC не выставлен (%s): %s" % (MPC_SET, ex))

    def freeze_clouds():
        """MID поверх light_function_material каждого DirectionalLight: SpeedU/SpeedV = 0 (маска облаков неподвижна и одинакова между прогонами)."""
        n = 0
        try:
            for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.DirectionalLight):
                c = a.get_component_by_class(unreal.DirectionalLightComponent)
                lf = c.get_editor_property("light_function_material")
                if not lf:
                    continue
                # ★ в -game Python 5.8 класса KismetMaterialLibrary НЕТ (первый прогон 05.09 упал на нём); рабочее имя — unreal.MaterialLibrary
                #   (как в asda_gacha_grey_probe.py). Запасной путь — снять Light Function вовсе (тени облаков выключены; одинаково во всех прогонах).
                ml = getattr(unreal, "MaterialLibrary", None) or getattr(unreal, "KismetMaterialLibrary", None)
                if ml is not None:
                    mid = ml.create_dynamic_material_instance(st["w"], lf)
                    mid.set_scalar_parameter_value("SpeedU", 0.0)
                    mid.set_scalar_parameter_value("SpeedV", 0.0)
                    c.set_light_function_material(mid)
                    log("тени облаков остановлены: %s ← MID(%s) SpeedU=SpeedV=0" % (a.get_name(), lf.get_name()))
                else:
                    c.set_light_function_material(None)
                    log("тени облаков СНЯТЫ (MaterialLibrary недоступна): %s light_function=None" % a.get_name())
                n += 1
        except Exception as ex:
            note("остановить тени облаков не удалось: %s" % ex)
        # ★ UE-этап C-2 fix (05.09): 3D-облака CloudField (AAsdaCloudFieldActor, WindSpeed 800 юн/с) отбрасывают тени и ползут между
        #   прогонами — пары кадров contact_wall/k1_a4 расходились на absdiff 17–23 при неизменных материалах (тень облака через весь кадр).
        #   Ветер в ноль на время прогона (только рантайм; положение облаков на старте мира детерминировано сидом актора).
        try:
            for a in unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaCloudFieldActor):
                try:
                    a.set_editor_property("wind_speed", 0.0)      # UPROPERTY без BlueprintReadWrite — в -game из Python недоступно
                    log("CloudField %s: WindSpeed=0 (3D-облака стоят)" % a.get_name())
                except Exception:
                    a.set_actor_hidden_in_game(True)             # запасной путь: облака (и их тени) скрыты — одинаково во всех прогонах с FREEZE
                    log("CloudField %s: WindSpeed недоступен → актор скрыт (3D-облаков и их теней нет)" % a.get_name())
                n += 1
        except Exception as ex:
            note("остановить CloudField не удалось: %s" % ex)
        R["clouds_frozen"] = n

    def mob_diag():
        """Почему моб не виден в кадре офлайн: скрыт ли актор/меш (в отчёт, а не гадать)."""
        try:
            mobs = unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaMobActor)
        except Exception:
            return
        out = []
        for m in mobs[:3]:
            d = {"name": m.get_name(), "loc": vec3(m.get_actor_location())}
            for p in ("hidden", "actor_hidden_in_game"):
                try:
                    d[p] = m.get_editor_property(p)
                except Exception:
                    pass
            try:
                d["was_recently_rendered"] = m.was_recently_rendered(1.0)
            except Exception:
                pass
            try:
                comps = m.get_components_by_class(unreal.MeshComponent)
                d["meshes"] = [(c.get_name(), c.is_visible(), c.get_editor_property("hidden_in_game")) for c in comps]
                for c in comps:
                    if isinstance(c, unreal.SkeletalMeshComponent):
                        sk = c.get_skinned_asset() if hasattr(c, "get_skinned_asset") else None
                        d["skel"] = sk.get_name() if sk else None
            except Exception as ex:
                d["meshes_err"] = str(ex)
            out.append(d)
        R["mob_diag"] = out
        log("диагностика мобов: %s" % out)

    def build_npc_face_cams():
        """★ Сет C-2 fix: контактные камеры на лицо NPC (ASDA_SET0_NPC_FACE) + A/B рантайм-MID. Ассеты не трогаются."""
        if not NPC_FACE:
            return
        try:
            parts = NPC_FACE.split("@@")
            rx = parts[0].strip() or "npc_[mf]_h_"
            count = int(parts[1]) if len(parts) > 1 and parts[1].strip() else 3
            nums = [float(v) for v in (parts[2].split(",") if len(parts) > 2 and parts[2].strip() else ["110", "-6", "-14", "90"])]
            dist, pitch, head_off = nums[:3]
            yaw_off = nums[3] if len(nums) > 3 else 90.0
            kv = []
            for spec in (parts[3].split(";") if len(parts) > 3 else ["ClassStrength=0"]):
                if "=" in spec:
                    k, _, v = spec.partition("=")
                    kv.append((k.strip(), float(v)))
            mid_tag = "".join("%s%g" % ("".join(ch for ch in k if ch.isupper()).lower() or k[:2].lower(), v) for k, v in kv) or "mid"
        except Exception as ex:
            note("ASDA_SET0_NPC_FACE не разобран (%s): %s" % (NPC_FACE, ex))
            return
        T = R.get("target") or [0.0, 0.0, 0.0]
        cands = []
        try:
            npcs = unreal.GameplayStatics.get_all_actors_of_class(st["w"], unreal.AsdaNpcActor)
        except Exception as ex:
            note("AsdaNpcActor недоступен из Python: %s" % ex)
            return
        for a in npcs:
            try:
                comp = a.get_editor_property("mesh")
                sk = comp.get_skinned_asset() if (comp and hasattr(comp, "get_skinned_asset")) else (comp.get_editor_property("skeletal_mesh_asset") if comp else None)
                if not sk:
                    continue
                mats = []
                for mi in range(comp.get_num_materials()):
                    m = comp.get_material(mi)
                    if m:
                        mats.append(m.get_name())
                hay = (sk.get_path_name() + " " + " ".join(mats)).lower()
                if not re.search(rx, hay):
                    continue
                L = a.get_actor_location()
                cands.append((math.hypot(L.x - T[0], L.y - T[1]), a, comp, sk, mats))
            except Exception:
                continue
        cands.sort(key=lambda t: t[0])
        log("лица NPC (/%s/): подходящих %d из %d, берём %d" % (rx, len(cands), len(npcs), min(count, len(cands))))
        R["npc_face"] = {"regex": rx, "mid_params": kv, "npcs": []}
        for idx, (d, a, comp, sk, mats) in enumerate(cands[:count], 1):
            try:
                L = a.get_actor_location()
                top = float(a.mesh_top_z()) if hasattr(a, "mesh_top_z") else 160.0
                head = (L.x, L.y, L.z + top + head_off)
                fwd = a.get_actor_rotation().yaw + yaw_off
                fd = dir_from(0.0, fwd)
                loc = [head[0] + fd[0] * dist, head[1] + fd[1] * dist, head[2] + math.tan(math.radians(abs(pitch))) * dist]
                cam_yaw = (fwd + 180.0) % 360.0
                mesh_name = sk.get_name()
                base = "set0_%s_npcface%d_%s_%s" % (KEY, idx, mesh_name, TAG)
                c = {"name": "npcface%d_%s" % (idx, mesh_name), "loc": [round(v, 1) for v in loc], "rot": {"roll": 0.0, "pitch": round(pitch, 3), "yaw": round(cam_yaw, 3)},
                     "ground_z_under_cam": ground_z(loc[0], loc[1]),
                     "note": "лицо NPC %s (%s, %s; NpcId %s): %.0f юн перед лицом (yaw актора %.1f+%.0f), MeshTopZ %.1f, до цели %.0f" % (
                         a.get_name(), mesh_name, mats[:3], a.get_editor_property("npc_id"), dist, a.get_actor_rotation().yaw, yaw_off, top, d),
                     "npc": {"actor": a.get_name(), "mesh": sk.get_path_name(), "materials": mats, "loc": vec3(L), "mesh_top_z": round(top, 1)}}
                R["cams"].append(c)
                ci = len(R["cams"]) - 1
                R["npc_face"]["npcs"].append(c["npc"])

                def freeze(comp=comp, a=a):
                    # ★ офлайн NPC скрыт (AsdaNpcActor::BeginPlay: !bNetSpawned → SetActorHiddenInGame(true)) — раскрываем ТОЛЬКО этого
                    #   NPC на время прогона (первая серия warm: 6 кадров без NPC в кадре). Анимация не запускается — поза A и B одна.
                    try:
                        a.set_actor_hidden_in_game(False)
                        comp.set_visibility(True, True)
                        comp.set_editor_property("global_anim_rate_scale", 0.0)
                        log("NPC %s: раскрыт (hidden_in_game=%s), анимация остановлена (GlobalAnimRateScale=0)" % (a.get_name(), a.get_editor_property("hidden")))
                    except Exception as ex:
                        note("NPC %s: раскрыть/остановить анимацию не удалось: %s" % (a.get_name(), ex))

                def apply_mid(comp=comp, a=a, c=c):
                    n = 0
                    try:
                        for i in range(comp.get_num_materials()):
                            if comp.get_material(i) is None:
                                continue
                            mid = comp.create_dynamic_material_instance(i)
                            if mid is None:
                                continue
                            for k, v in kv:
                                mid.set_scalar_parameter_value(k, v)
                            n += 1
                        c["mid_slots"] = n
                        log("NPC %s: MID на %d слотах, %s" % (a.get_name(), n, kv))
                    except Exception as ex:
                        note("NPC %s: MID не поставлен: %s" % (a.get_name(), ex))

                after(1, freeze)
                schedule_camera(ci, c, label=base)                       # A: как есть (ClassStrength класса)
                after(1, apply_mid)
                schedule_camera(ci, c, label=base + "_" + mid_tag)       # B: с MID (напр. ClassStrength=0)
                log("камера лица NPC: %s" % c["note"])
            except Exception as ex:
                note("камера лица NPC не поставлена: %s" % ex)

    def phase_actors():
        dump_runtime_actors()
        mob_diag()
        if KEY == "alpen":
            collect_trees()
        build_cams()
        spawn_self()
        after(240, after_spawn)
        for i, c in enumerate(R["cams"]):
            schedule_camera(i, c)
        build_npc_face_cams()             # ★ сет C-2 fix: лица NPC (A/B ClassStrength), планирует свои кадры сама
        # ★ Сет E-2: варианты рантайм-состояния — те же камеры ещё раз на каждый вариант (кадры с суффиксом _<имя>)
        for vname, acts in VARIANTS:
            after(1, lambda vname=vname, acts=acts: apply_actions(acts, vname))
            after(30, lambda: None)       # дать DF-сцене/прокси/клипмапам глобального SDF обновиться
            vc_list = []
            for i, c in enumerate(R["cams"]):
                vc = {k: c[k] for k in ("name", "loc", "rot", "note")}
                vc["name"] = "%s@%s" % (c["name"], vname)
                vc_list.append(vc)
                idx = i + R.get("cam_index_offset", 0) + 1
                schedule_camera(i, vc, label="set0_%s_%d_%s_%s" % (KEY, idx, TAG, vname))
            R.setdefault("variants", {})[vname] = {"actions": acts, "cams": vc_list}
        after(5, finish)

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
            unreal.log_error("[set0] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт: key=%s map=%s id=%d tag=%s" % (KEY, MAP, MAPID, TAG))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
