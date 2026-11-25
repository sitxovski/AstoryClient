# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_effects.py — размещение эффектов карты как AAsdaEffectActor (C++ симуляция 1:1, модель
# верифицирована live.dmp). Источник: export/maps/<map>/effects.json. На размещение — ОДИН актор со всеми
# эмиттерами .jeb (секция на эмиттер). Python строит track-узлы: из trackNodes .jeb (58f, тип 0xD/0x1B)
# или дефолт 2 узла birth/death из fixed (как рантайм при count=0). Оси/векторы game→UE: (x,y,z)→(-z,x,y).
# Материал: MIC (M_AsdaEffectAdd/Alpha + текстура эффекта из /Game/Map/<map>/EffectTex).
# ПОКРЫТИЕ v1: modern (0xD/3/0x1B) частицы + 0xB single-quad. LEGACY (1/0xC/0xE/0x18) — ПРОПУСК до
# реверса маппинга их 40dw-подузлов (rate=node[49] недоступен из fixed) — не гадаем.
import os, json, re, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
# ★★★ Ф16 (К-1, 20.09.2026): гейт `asda.Fx.Glow.EmittersWriter` — общая точка сборщиков эффектов.
#   Импорт безопасен: asda_fx_common при импорте НИЧЕГО не делает ([[tools-no-import-side-effects]]),
#   а обратной зависимости (asda_fx_common → asda_place_effects) нет — цикла не возникает.
import asda_fx_common as _FXC

MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
RAW_DIR  = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
MAT_ADD  = "/Game/Effects/M_AsdaEffectAdd"
MAT_ALPHA= "/Game/Effects/M_AsdaEffectAlpha"
# ★ D3D 6:4 (INVSRCALPHA:INVSRCCOLOR, «тёмный дым» flammio-a_09/10, worker02-a_00/01): Final =
#   srcC×(1−srcA) + dst×(1−srcC). Точная декомпозиция в UE = ДВЕ секции: Modulate (dst×(1−Tex×VC.rgb))
#   + Additive-добавка (Tex×VC.rgb×(1−VC.a)) → Material + MaterialB эмиттера (см. AsdaEffectActor).
MAT_MOD   = "/Game/Effects/M_AsdaEffectMod"
MAT_MODADD= "/Game/Effects/M_AsdaEffectModAdd"
MASTERS = {"add": MAT_ADD, "alpha": MAT_ALPHA, "mod": MAT_MOD, "modadd": MAT_MODADD}
_asset = _MAPS.map_asset_name  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
DEST_LEVEL = _U.level_path(MAP_NAME)
FX_TEX   = "/Game/Map/%s/EffectTex" % _asset(MAP_NAME)
FX_MAT   = "/Game/Map/%s/EffectMat" % _asset(MAP_NAME)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ML  = unreal.MathLibrary
import math
C  = [0,0,-1, 1,0,0, 0,1,0]
CT = [0,1,0, 0,0,1, -1,0,0]
matmul = _C.matmul3   # 3×3 row-major (общая реализация asda_coords; дедуп №2 Т19)
to_ue_v   = _C.to_ue_v   # ★ единая конверсия осей (asda_coords)
from_ue_v = _C.from_ue_v      # UE→game (обратно to_ue_v)

def place_transform(pos, m):
    AT=[m[0],m[4],m[8], m[1],m[5],m[9], m[2],m[6],m[10]]
    R=matmul(matmul(C,AT),CT)
    c0=(R[0],R[3],R[6]); c1=(R[1],R[4],R[7]); c2=(R[2],R[5],R[8])
    n0=math.sqrt(sum(x*x for x in c0)) or 1.0
    n1=math.sqrt(sum(x*x for x in c1)) or 1.0
    n2=math.sqrt(sum(x*x for x in c2)) or 1.0
    fwd=unreal.Vector(*[x/n0 for x in c0]); rgt=unreal.Vector(*[x/n1 for x in c1]); up=unreal.Vector(*[x/n2 for x in c2])
    rot=ML.make_rotation_from_axes(fwd,rgt,up)
    return unreal.Transform(to_ue_v(pos), rot, unreal.Vector(n0,n1,n2))

def compute_reflect_normal(parent, meshbone, matrix):
    # Нормаль хирального отражения в GAME-локале эффекта = local-Z ДЕКОРИРУЕМОЙ меш-кости (импортированная UE-ориентация,
    # уже включает зеркало скелета), перенесённая в кадр эффекта. meshbone=None/нет сокета → дефолт [0,0,1] (negate game-Z,
    # как у радуги). CONFIRMED: для лампы (objgroupNN) нормаль≈X → negate-X; замер уровня 174→25 юнитов до лампы.
    if parent is None:
        return [0.0, 0.0, 1.0]
    comp = parent.skeletal_mesh_component
    if not meshbone or not comp.does_socket_exist(unreal.Name(meshbone)):
        return [0.0, 0.0, 1.0]
    tb = comp.get_socket_transform(unreal.Name(meshbone), unreal.RelativeTransformSpace.RTS_WORLD)
    z_world = ML.get_up_vector(tb.rotation.rotator())               # local-Z меш-кости в мире (UE)
    exf = place_transform([0.0,0.0,0.0], matrix)                    # ориентация актора эффекта (selfMat)
    n_ue = ML.inverse_transform_direction(exf, z_world)            # в локаль эффекта (UE-оси)
    n = from_ue_v(n_ue)                                             # → game-оси
    L = (n[0]*n[0]+n[1]*n[1]+n[2]*n[2]) ** 0.5 or 1.0
    return [n[0]/L, n[1]/L, n[2]/L]

def ensure_texture(tstem):
    # текстура в /Game/Map/<map>/EffectTex; если нет — импорт из fxtex карты или общего tex_misc (jem-текстуры)
    tp="%s/%s"%(FX_TEX,tstem)
    if EAL.does_asset_exist(tp): return True
    # ★ фикс «белый квадрат импакта» (2026-07-29): текстура может быть экспортирована fxtex-ом ДРУГОЙ карты
    #   (eff_0_mon_99_battk01-a_00 — только silalis/fxtex; скрипт шёл alpen→tex_misc → MIC оставался с
    #   DefaultTexture → additive-квад). Фолбэк: fxtex ВСЕХ карт (после родной карты и tex_misc).
    import glob as _glob
    cands=[os.path.join(RAW_DIR,MAP_NAME,"fxtex",tstem+".png"),
           r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/effects/tex_misc/"+tstem+".png"]
    cands+=sorted(_glob.glob(os.path.join(RAW_DIR,"*","fxtex",tstem+".png")))
    for src in cands:
        if os.path.isfile(src):
            import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (AutomatedAssetImportData)
            for a in _MIU.import_batch([src], FX_TEX):
                try: EAL.save_loaded_asset(a)
                except Exception: pass
            if EAL.does_asset_exist(tp):
                _fx_tex_settings(tp)          # свежий импорт сразу без сжатия/мипов (аудит 2026-07-27)
                return True
            # импорт не удался — пробуем следующий кандидат (раньше был return False = обрыв поиска)
    return False

_matcache={}
def _fx_tex_settings(tp):
    # ★ аудит 2026-07-27: fx-текстуры мелкие — DXT+мипы+стриминг дают «ступени»/мыло → RGBA8+NoMips+NeverStream
    t = EAL.load_asset(tp)
    if t and isinstance(t, unreal.Texture2D):
        t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        t.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        t.set_editor_property("never_stream", True)
        EAL.save_loaded_asset(t)

def get_fx_material(tex, mode):
    # ДЕФИС СОХРАНЯЕМ (ассеты текстур 'smoke03-a_00' с дефисом; классика tree box) + lowercase (PNG в нижнем).
    # mode: "add"/"alpha"/"mod"/"modadd" (bool для обратной совместимости: True="add", False="alpha").
    if mode is True: mode="add"
    elif mode is False: mode="alpha"
    tstem=re.sub(r'[^A-Za-z0-9_-]','_',os.path.splitext(tex)[0]).lower() if tex else "none"
    ensure_texture(tstem)
    key=(tstem,mode)
    if key in _matcache: return _matcache[key]
    nm="MI_%s_%s"%(tstem,mode); mp="%s/%s"%(FX_MAT,nm)
    master=EAL.load_asset(MASTERS[mode])
    MEL=unreal.MaterialEditingLibrary
    if EAL.does_asset_exist(mp): mic=EAL.load_asset(mp)
    else: mic=ATOOLS.create_asset(nm,FX_MAT,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    # ВСЕГДА чинить parent+текстуру (само-ремонт: битый parent после пересоздания мастера / DefaultTexture)
    MEL.set_material_instance_parent(mic,master)
    tp="%s/%s"%(FX_TEX,tstem)
    tv=MEL.get_material_instance_texture_parameter_value(mic,"SpriteTexture")
    if EAL.does_asset_exist(tp) and (tv is None or tv.get_name()=="DefaultTexture"):
        MEL.set_material_instance_texture_parameter_value(mic,"SpriteTexture",EAL.load_asset(tp))
    EAL.save_loaded_asset(mic)
    _matcache[key]=mic
    return mic

def ring_node(t, alpha, rgb, scale, roll, rot, h, topR, botR, u, v, basePos):
    n=unreal.AsdaFxNode()
    n.set_editor_property("time",float(t)); n.set_editor_property("alpha",float(alpha))
    n.set_editor_property("color",unreal.LinearColor(rgb[0],rgb[1],rgb[2],1.0))
    n.set_editor_property("ring_scale",float(scale)); n.set_editor_property("ring_roll",float(roll))
    n.set_editor_property("ring_rot",unreal.Vector(rot[0],rot[1],rot[2]))
    n.set_editor_property("ring_h",float(h)); n.set_editor_property("ring_top_r",float(topR)); n.set_editor_property("ring_bot_r",float(botR))
    n.set_editor_property("ring_u",float(u)); n.set_editor_property("ring_v",float(v))
    n.set_editor_property("ring_base_pos",unreal.Vector(basePos[0],basePos[1],basePos[2]))
    return n

def mesh12_key_nodes(p, life, nzfn=None):
    """Серединные узлы трека МЕШ-эмиттера (0x11/0x12/0x1F) из файловых ключей.

    Общий код для `build_emitter` и точечного патчера `asda_patch_mesh12_keys.py`.
    Соответствие «слот ключа → канал узла» ЗАМЕРЕНО в живом оригинале
    (`Reports/fx_pipeline_ground_truth.md` §8): слот i лежит по смещению узла 4·i,
    [0]=время [1/3/5]=offsetWorld [7/9/11]=posLocal [13]=alpha [15/17/28]=scale XYZ
    [19..21]=RGB [30/32/34]=rot XYZ. Время клампится до life (эталонная вставка `0x11F7270`).
    Гейт `ASDA_FX_NO_MESH12_KEYS=1` → пусто (прежнее поведение: только два сид-узла).
    """
    if os.environ.get("ASDA_FX_NO_MESH12_KEYS", "") == "1":
        return []
    nz = nzfn or (lambda v: list(v))
    out = []
    for k in (p.get("trackNodes") or []):
        if len(k) < 35:
            continue
        kt = min(max(float(k[0]), 0.0), life)
        # ★ Правило краёв — ПО ДИЗАСМУ вставки ключа `0x11F7270` (ветка «ключ из файла», 0x11F747D):
        #   `if (key.time > duration) key.time = duration;` (кламп, 0x11F7485-0x11F749E), затем поиск
        #   ПЕРВОГО узла с `time >= key.time` и вставка ПЕРЕД ним (0x11F7517-0x11F7587).
        #   ⇒ ключ с временем ≥ life КЛАМПИТСЯ и ВСТАЁТ ПЕРЕД КОНЕЦ-сидом (а не выбрасывается):
        #     напр. `eff_2_char_99_freez101-a_00/IAS_M00` — ключ t=39.5 при life 15.0.
        #   ⇒ ключ с временем 0 ВЫБРАСЫВАЕТСЯ: `prev = elementAt(i-1)` при i=0 даёт NULL,
        #     и по `0x11F7546 je 0x11F75A1` вставка не выполняется.
        if kt <= 0.0:
            continue
        n = node(kt, 0, 1, 1, 0, 0, min(max(k[13], 0.0), 1.0),
                 [min(max(k[19], 0.0), 1.0), min(max(k[20], 0.0), 1.0), min(max(k[21], 0.0), 1.0)],
                 0, [0, 1, 0])
        n.set_editor_property("cone_axis", unreal.Vector(k[15] or 1.0, k[17] or 1.0, k[28] or 1.0))
        n.set_editor_property("ring_rot", unreal.Vector(k[30], k[32], k[34]))
        n.set_editor_property("ring_pos_ofs", unreal.Vector(*nz([k[7], k[9], k[11]])))
        n.set_editor_property("ring_base_pos", unreal.Vector(*nz([k[1], k[3], k[5]])))
        out.append(n)
    out.sort(key=lambda n: float(n.get_editor_property("time")))
    return out


def node(t, rate, sx, sy, spd, cone, alpha, rgb, grav, axis):
    n=unreal.AsdaFxNode()
    n.set_editor_property("time",float(t)); n.set_editor_property("rate",float(rate))
    n.set_editor_property("size_x",float(sx)); n.set_editor_property("size_y",float(sy))
    n.set_editor_property("speed",float(spd)); n.set_editor_property("cone_angle",float(cone))
    n.set_editor_property("alpha",float(alpha))
    n.set_editor_property("color",unreal.LinearColor(rgb[0],rgb[1],rgb[2],1.0))
    n.set_editor_property("gravity",float(grav)); n.set_editor_property("cone_axis",to_ue_v(axis))
    return n

def etalon_end_mode(em):
    """★★★ ЭТАЛОННЫЙ `end_mode` квада/кольца/меша — по ДВУМ РАЗНЫМ машинам обновления.

    Дизасм (`Reports/fx_scale_timing_audit.md` §4):
      * С КЛЮЧАМИ — `effectUpdateKeyframeTrack@0x11F7B10` (квады) и `fxTrack_updateKeyframe@0x11FD800`
        (кольца): ветка `spawnSpread==3` СНАПАЕТ значения текущего узла и **не сбрасывает** `lifeTimer`
        ⇒ трек стоит на последнем узле = ФРИЗ (порт: `end_mode 3`);
        ветка `==0` — снап базовых сидов + `lifeTimer = −1.0` = СТОП; `==1` — снап + `vtbl+1` = рестарт.
      * БЕЗ КЛЮЧЕЙ — `fxParticle_stepAnim@0x1218650` (квады) и `effectTransformTick@0x12158B0` (кольца):
        `case 3: lifeTimer = 0.0; fxParticleCommitPos(...)`, а `fxParticleCommitPos@0x1215830` зовёт
        `fxParticleSaveTransformState@0x120AAE0`, который ВОЗВРАЩАЕТ каналы к базовым сидам
        ⇒ это РЕСТАРТ с начала, а НЕ фриз (порт: `end_mode 1`).
    ⇒ значение 3 c диска означает РАЗНОЕ в зависимости от наличия keyframe-хвоста.
    Для колец роль `endMode` играет `loopMode` (диск +0x88): 0=стоп, 1=respawn, 2=пинг-понг
    (`fxParticleRandomizeVelBounds@0x1215670` — свап start↔END; в порте не реализован, ближайшее
    поведение — рестарт), 3=луп.
    """
    p = em.get("params") or {}
    has_kf = bool(p.get("trackNodes"))
    if p.get("ring"):
        d = int(p.get("loopMode", 3))
        if d == 0: return 0
        if d == 1: return 1
        if d == 2: return 1                       # пинг-понга в порте нет — рестарт ближе фриза
        return 3 if has_kf else 1                 # 3: фриз только у трека
    d = int(p.get("endMode", 0))
    if d == 0: return 0
    if d == 1: return 1
    if d == 3: return 3 if has_kf else 1
    return 1


def _kf_end_seed():
    """КОНЕЦ-узел трека (t=lifetime из «конечных» сидов) — ВОССТАНОВЛЕН 2026-08-15, по умолчанию ВКЛ.

    ★★★ ИСТОРИЯ. 14.08 (вечер) узел был объявлен «опровергнутым» и выключен: считалось, что его
    собирает только ветка `a2[27]==0` у `animSampleCurveAtTime@0x11FCEC0`, а она в бинаре
    недостижима (все 6 вызовов переходника 0x920D0A пишут в `a2[27]` единицу). Проверка была
    ЛОЖНО-ОТРИЦАТЕЛЬНОЙ — смотрели не ту функцию (см. память [[false-negatives-method]]).

    ЭТАЛОН: трек = `[СТАРТ-сид @ t=0] + [ключи файла] + [КОНЕЦ-сид @ t=lifetime]`.
    Доказательство (полностью — `Reports/weapon_fx_loop.md` §2.3):
      1. КОНЕЦ-узел строит `animComputeKeyframeDeltas@0x1202BE0` (`RE/etalon/model_anim.c:40265`),
         а не `animSampleCurveAtTime`: она переписывает ПЕРВЫЙ элемент списка временем 0 и
         НАЧАЛЬНЫМИ сидами (`velBase`/`posBase`/`colorStart`/`colorBaseG,B`/`lifeBase`), затем
         `sub_93A84F` (= «на последний», доказано соседними строками 40418-40434 по аргументам
         `effectInterpKeyframeDelta(prev,cur,next)`) переводит итератор на ПОСЛЕДНИЙ и переписывает
         его временем `billboardMode` (рандомизированный lifetime, строки 40356-40358) и
         КОНЕЧНЫМИ сидами (`velTarget`/`posTarget`/`colorEnd`/`colorEndG,B`/`lifeTarget`).
         Вызывающих у неё ДЕСЯТЬ, а не ноль.
      2. Порядок в лоадере виден целиком у текстового близнеца формата
         `CEffectElement_LoadType13@0x12809B0` (`effects_particles.c:110616`):
         разбор записи → `List_PushBack` → `animComputeKeyframeDeltas` → и ТОЛЬКО ПОТОМ
         `<ADD_KEYFRAME_COUNT>` + `animSampleCurveAtTime` на каждый ключ. Сиды существуют ДО ключей.
         `particleEmitterResetAndRecenter@0x12029C0` зовёт её же на КАЖДОМ рестарте.
      3. Сам `animSampleCurveAtTime` при `a2[27]==1` (единственный живой путь) КЛАМПИТ время ключа
         до lifetime и вставляет узел, ТРЕБУЯ предыдущий (`listAdvanceN(v4, i-1)`; `model_anim.c`
         40142-40143 и 40199-40201) — оба требования выполнимы только при уже готовых СТАРТ/КОНЕЦ.
      4. Принятый закон каналов «нет узлов → лерп start→END за life» физически реализуется ЭТИМ ЖЕ
         списком из двух сид-узлов; эмиттеры с ключами и без ходят через одну машину.
      5. Данные: в одном `.jeb` удара навыка 707 соседние эмиттеры без ключей честно росли
         `0 → 400`, а `sprite14` с ключом замирал на 100 при `scaleEnd = 800`. Асимметрия
         невозможна.
    Цена ошибки (что и увидел юзер): у эмиттера в режиме РЕСТАРТ трек замирал на последнем
    ФАЙЛОВОМ ключе (в канале заточки он стоит ровно на середине жизни у ~900 эмиттеров) и на
    рестарте ПРЫГАЛ к старту. Медианный разрыв стыка по альфе/размеру 0.41.

    Откат: `ASDA_FX_NO_KF_END_SEED=1` (+ пересборка канала). `ASDA_FX_KF_END_SEED=1` оставлен как
    явное «включить» для A/B. Читаем ЛЕНИВО (закон tools-no-import-side-effects).
    """
    if os.environ.get("ASDA_FX_KF_END_SEED", "") == "1":
        return True
    return os.environ.get("ASDA_FX_NO_KF_END_SEED", "") != "1"


def _load_jem_module():
    """Парсер .jem (`asda_jem_parse`), загружается по пути рядом с этим файлом один раз на процесс."""
    global _jem
    if "_jem" not in globals():
        import importlib.util as _iu
        sp=_iu.spec_from_file_location("jem", os.path.join(os.path.dirname(os.path.abspath(__file__)),"asda_jem_parse.py"))
        _jem=_iu.module_from_spec(sp); sp.loader.exec_module(_jem)
    return _jem


# ★★★ 30.10.2026 [ИСПРАВЛЕНИЕ]: ОДНОМЕШЕВЫЕ .jem, у которых матрица узла меша — не чистый масштаб (поворот, неравный
#   масштаб, сдвиг) или узел анимирован треками. Прежний путь брал из матрицы узла только элемент [0][0] (`scale`
#   в asda_jem_parse.parse_jem), а эталон рисует жёсткий узел в его матрице целиком: aabbDrawer_construct@0x129A3D0
#   кладёт bind-матрицу узла в матрицу отрисовки, objApplyAnimTransformToMatrix@0x129A5B0 двигает её треками,
#   CEffectMesh_DrawLeaf@0x129B5D0 → MultiplyTransform. Перепись EffectModel.bnz: 59 таких файлов; здесь 56 —
#   без трёх, что стоят на картах и приняты (кристаллы eff_alpenjewels_a_00/_b_00 на alpen, mdel_worker01-a_00 на
#   silalis): карты правятся только через AsdaMapObjects_ и с согласия владельца. Остальные 46 одномешевых .jem
#   (матрица = масштаб) дают ту же геометрию обоими путями и остаются на прежнем. Откат: ASDA_FX_JEM_SINGLE_FIRST=1.
JEM_SINGLE_NODE_FIX = frozenset((
    "201-01", "4skill_stone", "ch1_m_00_grov305_00_00_01", "ch1_m_00_grov305_00_00_02", "ch1_m_00_grov305_00_00_03",
    "cre_light01-a_00", "cre_light02-a_00", "eff_0_char_99_c-shovel01-a_00", "eff_0_othr_99_pmhpre01-a_00",
    "eff_1_char_02_amorde01-a_00", "eff_1_char_02_amorde02-a_00", "eff_1_char_02_amorde03-a_00",
    "eff_1_char_02_parlac01-a_00", "eff_1_char_02_rgeblwe01-a_00", "eff_1_char_02_rgeblwe01-b_00",
    "eff_1_char_03_trapth01-a_00", "eff_1_char_06_earthmb-a_02", "eff_1_char_06_firqukb01-b_00",
    "eff_1_char_06_stmtrpg01-b_00", "eff_1_char_99_cancel-a_00", "eff_1_char_99_cancel-a_01",
    "eff_1_char_99_grened01-a_00", "eff_1_char_99_icarrw03-a_00", "eff_1_char_99_scry01-a_00",
    "eff_1_char_99_scry01-a_01", "eff_1_char_99_tforeh01-a_00", "eff_1_char_99_tforeh01-a_01",
    "eff_1_char_99_tforeh01-a_02", "eff_2_char_99_bomsta01-a_00", "eff_2_char_99_bomsta01-a_01",
    "eff_2_char_99_defmine05-a_00", "eff_2_char_99_sleep01-a_00", "eff_2_char_99_sleep01-b_00", "eff_flammio-a_50",
    "eff_lucky_a_00", "fish_eff", "flower", "ice", "laurel wreath", "nonfish_eff", "ragepig_stone_ani_01",
    "roulette_item_001", "roulette_item_002", "shd_a_mesh", "shd_b_mesh", "shd_c_mesh", "shd_d_mesh", "shd_e_mesh",
    "shd_f_mesh", "shd_g_mesh", "shd_h_mesh", "shd_i_mesh", "shd_j_mesh", "shd_k_mesh", "shd_l_mesh", "stone_ani_01",
))


def jem_uses_model(meshes, em):
    """Рисовать ли .jem эмиттера как модель (`asda_jem_parse.model_mesh`), а не как `meshes[0]`."""
    if os.environ.get("ASDA_FX_JEM_FIRST_MESH", "") == "1":
        return False
    if len(meshes) > 1:
        return True
    return (os.environ.get("ASDA_FX_JEM_SINGLE_FIRST", "") != "1"
            and _load_jem_module()._base(em.get("jem") or em.get("name") or "") in JEM_SINGLE_NODE_FIX)


def jem_render_mesh(meshes, em):
    """Меш, который рисует меш-эмиттер: одномешевый .jem — сам меш, многомешевый — вся модель.

    ★★★ 30.10.2026 [ИСПРАВЛЕНИЕ]: раньше всегда `meshes[0]` — из 20 многомешевых .jem рисовался только первый меш, да
      ещё без матрицы своего узла (у мячей m_ball — один мяч из трёх, у eff_f_wing_big_00 — одно крыло из двух, камни
      firqukw03 стояли кучей в центре вместо кольца, комья лопаты не летели). Эталон рисует все узлы модели
      (CMesh_RenderNode@0x125C4C0), жёсткий узел — через MultiplyTransform(матрица узла) (CEffectMesh_DrawLeaf
      @0x129B5D0); разбор — в шапке `asda_jem_parse.model_mesh`. Откат: `ASDA_FX_JEM_FIRST_MESH=1` + пересборка канала.
    ★★★ 30.10.2026: тот же путь — у одномешевых .jem из `JEM_SINGLE_NODE_FIX` (матрица узла целиком, а не [0][0]).
    """
    if jem_uses_model(meshes, em):
        m = _load_jem_module().model_mesh(meshes)
        print("[эффекты] меш-эмиттер '%s': .jem '%s' — мешей %d → одна модель, вершин %d, костей %d"
              % (em.get("name"), em.get("jem") or em.get("name"), len(meshes), len(m["verts"]), len(m["palette"])))
        return m
    return meshes[0]


def apply_jem_geometry(fx, m, label):
    """Геометрия и скин .jem в эмиттер `fx` (общая точка build_emitter и точечной правки asda_fx_jem_model_patch)."""
    fx.set_editor_property("mesh_verts",[unreal.Vector(*v) for v in m["verts"]])
    fx.set_editor_property("mesh_normals",[unreal.Vector(*v) for v in m["normals"]])
    fx.set_editor_property("mesh_uv0",[unreal.Vector2D(u,v) for u,v in m["uvs"]])
    # ★ СКИН .jem (крылья МАШУТ и стоя): кости+треки A5/A6/A7 из .jem → матрицы по кадрам
    #   (skin_frames: inv(W(0))·W(f), stored-UE оси). Эталон — animTracks_evaluateAtTime(pMesh, animFrame),
    #   animFrame += 1 за кадр рендера. Меш без скелета (jewels-кристалл) → массивы пустые, ветка спит.
    try:
        frames,nf = _load_jem_module().skin_frames(m)
    except Exception as ex:
        unreal.log_warning("[fx] скин .jem не собран (%s): %s" % (label, ex)); frames,nf = [],0
    if frames and m.get("vbone") and len(m["vbone"])==len(m["verts"]):
        nb = len(m["palette"])
        xf = []
        for R,T in frames:
            mat = unreal.Matrix(unreal.Plane(R[0][0],R[0][1],R[0][2],0.0),
                                unreal.Plane(R[1][0],R[1][1],R[1][2],0.0),
                                unreal.Plane(R[2][0],R[2][1],R[2][2],0.0),
                                unreal.Plane(T[0],T[1],T[2],1.0))
            xf.append(mat.transform())
        fx.set_editor_property("skin_frames", xf)
        fx.set_editor_property("skin_bones", nb)
        fx.set_editor_property("mesh_vert_bone", [int(b) for b in m["vbone"]])
        unreal.log_warning("[fx] %s: скин .jem — кадров %d, костей %d, вершин %d" %
                           (label, nf, nb, len(m["verts"])))
    return frames, nf


def build_emitter(em, blend_add, reflect_n=None, authored_rot=None, pure_ring_attached=False,
                  ring_track_pos=False, kf_seed_nodes=False, etalon_end=False, holder_restart=False):
    # ★★★ holder_restart (только КАРТЫ): у эффекта карты есть ХОЛДЕР, который перезапускает его
    #   целиком, когда он «умер» (`effects_system_verification.md` §«type −1: … dead → restart
    #   (vtbl+4)»; для type 2 рестарт синхронизован с `rec+0x324`, `sceneComposite_UpdateGridsAndEffects@0x11DD150`)
    #   ⇒ на картах эмиттерный «СТОП» (endMode/loopMode 0) НЕ означает «погас навсегда»: он гаснет
    #   и холдер поднимает эффект заново. У нас холдера нет — актор живёт вечно, поэтому эталонный
    #   `0` надо отображать в `1` (рестарт), иначе фонари/волны/лампы карт погаснут НАВСЕГДА.
    #   Правится ТОЛЬКО «фриз вместо лупа» (3 без ключей → 1).
    # ★★★ etalon_end (2026-08-14, `Reports/fx_scale_timing_audit.md`): ветки growQuad и mesh12
    #   ставят `end_mode = 1` БЕЗУСЛОВНО («закон вечных карт»). Для каналов, где эффект одноразовый
    #   или замирает, это неверно — см. `etalon_end_mode()` выше. ФЛАГ, А НЕ ГЛОБАЛЬНАЯ ПРАВКА:
    #   на картах 30 квадов с `endMode=0` намеренно зациклены (принятые эффекты), их не трогаем.
    # ★★★ kf_seed_nodes (2026-08-14, ПЕРВОПРИЧИНА «"юбка" внизу статична», отчёт
    #   `Reports/item_fx_ring_motion.md`): в файле лежат только ПРОМЕЖУТОЧНЫЕ keyframe-ключи, а
    #   СТАРТ-узел (t=0) и КОНЕЦ-узел (t=lifetime) эталонный лоадер достраивает САМ из базовых
    #   «начальных»/«конечных» сидов записи. Доказательство целиком:
    #     `animSampleCurveAtTime@0x11FCEC0` — при `a2[27]==1` (так лоадер зовёт КАЖДЫЙ файловый ключ)
    #       только клампит время до lifetime и вставляет узел ПЕРЕД первым бо́льшим, ТРЕБУЯ наличия
    #       предыдущего узла ⇒ СТАРТ-узел в списке уже есть; при `a2[27]==0` функция сама собирает
    #       узел из конечных сидов (`t=lifetime`, `[40]=heightEnd(+0x11DC)`, `[42]=topREnd(+0x11E8)`,
    #       `[44]=botREnd(+0x11F4)`, `[13]=alphaEnd`, `[15]=scaleEnd`, `[19..21]=colorEnd`, …);
    #     `effectUpdateKeyframeTrack@0x11F7B10` берёт узел по индексу `курсор+1` при стартовом
    #       курсоре 0 ⇒ элемент 0 списка — служебный СТАРТ, а не первый ключ файла.
    #   Порт строил трек ТОЛЬКО из ключей файла. Замер по архиву: 1691 кольцо 0xE с ключами — у ВСЕХ
    #   первый ключ t>0 и последний t<life; из них 1083 имеют РОВНО ОДИН ключ ⇒ `NodeAt` отдаёт его
    #   на любом времени = кольцо полностью статично (та самая «юбка» pmbuff_a_00..a_05).
    #   Квадов с ключами 5716 — у них СТАРТ-сид ветка growQuad клала и раньше, терялся только КОНЕЦ.
    #   ★★★ ПОПРАВКА 2026-08-14 (`skill_fx_scale.md`), ОТМЕНЕНА 2026-08-15: КОНЕЦ-узел объявляли
    #   опровергнутым и отключали, ссылаясь на недостижимость ветки `a2[27]==0` у
    #   `animSampleCurveAtTime`. Недостижимость верна, ВЫВОД НЕВЕРЕН: КОНЕЦ-узел строит ДРУГАЯ
    #   функция — `animComputeKeyframeDeltas@0x1202BE0` (10 вызывающих), и лоадер зовёт её ДО
    #   чтения ключей файла. Узел ВОССТАНОВЛЕН, дефолт ВКЛ; разбор — в шапке `_kf_end_seed()`
    #   и `Reports/weapon_fx_loop.md`. Симптом отсутствия: рывок раз в период у всех
    #   эмиттеров в режиме РЕСТАРТ (жалоба юзера про «столбы света» баллисты).
    #   ФЛАГ, А НЕ ГЛОБАЛЬНАЯ ПРАВКА (закон «общий код эффектов — только под флагом, дефолт как было»):
    #   на картах 34 кольца и 95 квадов с ключами (alpen, flammio, inferion, palace_of_queen, оба
    #   castle_of_chess…) — принятые эффекты трогать нельзя. True передаёт только канал предметов
    #   (`asda_build_char_fx.ITEM_FX`).
    # ★ ring_track_pos (2026-07-31, ПЕРВОПРИЧИНА «свечение оружия бьёт ИЗ КИСТИ»): в ветке 0xE с
    #   ТРЕК-УЗЛАМИ офсет кольца передавался в ring_node ЖЁСТКОЙ КОНСТАНТОЙ [0,0,0] — поля узла
    #   [1/3/5]=basePos и [7/9/11]=pos не читались ВООБЩЕ (ветка БЕЗ узлов их читает правильно).
    #   Итог: любое кольцо/цилиндр 0xE с треком садится в НАЧАЛО КООРДИНАТ ХОЛДЕРА. Для свечения
    #   заточки холдер = кость кисти (эталон CCharacter_UpdateHeldItemEffect@0xA4C070:
    #   EffectWorld = boneLive("8_a1") × charWorld) ⇒ 4 светящихся цилиндра баллисты
    #   (bali_e_*_ma_03: ccyl1..4, height 50×scale 2 = 100 юн, radius 4×2 = 8) растут ПРЯМО ИЗ КИСТИ.
    #   Замер живого лога v4 это и показал: .jeb pos=(70,−5,19), а факт-центр (0.2,−50,0) при
    #   габарите (16,100,16) — размер верный, позиция потеряна.
    #   ФЛАГ, А НЕ ГЛОБАЛЬНАЯ ПРАВКА: потеря затрагивает 1263 кольца из 3255, из них 1199 — НЕ свечение
    #   (карт-эффекты eff_flammio/eff_worker01/eff_mdel_alpengod/camp_*, скилл-эффекты, мобы).
    #   Карты откалиброваны на текущем (потерянном) офсете — трогать их нельзя (закон: общий код
    #   эффектов только под флагом). True передаёт ТОЛЬКО asda_build_weapon_glow_fx.py.
    # ★ pure_ring_attached (2026-07-31, фонари alpen*light): эффект type-2 attached, ВСЕ эмиттеры 0xE.
    #   По эталону (дизасм 0x1215C10 + live-дамп: det(N520)=+1 у ВСЕХ фонарей) зеркала у attach-матрицы НЕТ,
    #   а канал rec+0x144 — это POS-канал (вращается R²·N48·N520). Хиральный рефлект и склейка bp=basePos+pos
    #   были артефактами эры транспонированных ротаций: у симметричных радуг рефлект геометрии невидим, у
    #   несимметричных конусов фонарей он разворачивал конус на 180° вокруг блика («светит наружу»).
    #   Новая схема (ТОЛЬКО чистые 0xE): ring_base_pos = negate-z(pos-канал) (C++ карт-путь читает pc=(x,y,−z)),
    #   ring_pos_ofs = basePos-канал (сырой мир), chiral_normal НЕ ставится (см. main()).
    #   Для радуг новая схема воспроизводит их ПРИНЯТЫЕ данные бит-в-бит: negate_z(5.5,0,1.75)=(5.5,0,−1.75)
    #   == refl_z старой схемы (проверено повершинно 0.08 юн против оригинала). Tower (есть 0xB) — старый путь.
    # ★ authored_rot (3x3 game, нормированные строки авторской матрицы placement'а): для type-2 attached
    #   частиц ось конуса вращается АВТОРСКОЙ матрицей эффекта (дизасм: vel=TransformCoord(coneDir, M+0x208);
    #   M=авторская ориентация, позиция-only attach). Выхлоп дирижабля: authored вращает up→горизонт-назад.
    #   Рантайм bAttachedMap ось НЕ вращает (мир-оси) → поворот бейкается здесь.
    def _end_mode():
        """Эталонный end_mode с поправкой на холдер карты (см. holder_restart)."""
        m = etalon_end_mode(em)
        return 1 if (holder_restart and m == 0) else m
    def _rot_axis(v3):
        if authored_rot is None: return list(v3)
        R=authored_rot
        out=[v3[0]*R[0][0]+v3[1]*R[1][0]+v3[2]*R[2][0],
             v3[0]*R[0][1]+v3[1]*R[1][1]+v3[2]*R[2][1],
             v3[0]*R[0][2]+v3[1]*R[1][2]+v3[2]*R[2][2]]   # row-vector × matrix (game row-major)
        L=(out[0]**2+out[1]**2+out[2]**2)**0.5 or 1.0
        return [out[0]/L,out[1]/L,out[2]/L]
    def _exhaust_axis(v3):
        # ★ ось конуса attached = ДИСКОВАЯ ось (thetaSpread @0xFC: выхлоп (0,0,-1), дым труб (0,1,0)) ×
        #   авторская матрица в мир (S5: ориентация authored статична; дирижабль ВИСИТ — курс не меняется,
        #   анимация Bone03 = только вертикальное покачивание ±400). БЕЗ негейт-гаданий.
        return _rot_axis(v3)
    # ★ reflect_n: хиральный фикс для type-2 attached. Меш дирижабля проходит per-bone negate game-Z в bone-local
    #   (asda_mapanim_to_gltf:190) + зеркальный импорт скелета (actor scale.X<0). Эффект (basePos/pos) хиральность НЕ
    #   наследует → садится ЗЕРКАЛЬНО относительно декорируемого меша. Фикс: отразить basePos-офсеты об ПЛОСКОСТЬ ⟂
    #   local-Z ДЕКОРИРУЕМОЙ меш-кости (в game-локале эффекта). reflect_n = эта нормаль (game-оси); None = без отражения.
    #   Для лампы (objgroupNN) normal≈X → negate-X; для радуги (нет декор-меша) normal=[0,0,1] → negate-Z. Оба CONFIRMED
    #   (замер уровня: фонарь 174→25 юнитов до лампы; rainbow2 совпал с эталоном [38.37,126.83,7.04]).
    def _nz(v3):
        if reflect_n is None: return list(v3)
        d = v3[0]*reflect_n[0] + v3[1]*reflect_n[1] + v3[2]*reflect_n[2]
        return [v3[i] - 2.0*d*reflect_n[i] for i in range(3)]
    p=em["params"]; t=em["type"]
    fx=unreal.AsdaFxEmitter()
    fx.set_editor_property("name",em["name"]); fx.set_editor_property("jeb_type",t)
    fx.set_editor_property("start_delay",float(em.get("startDelay",0.0) or 0.0))   # каскад (rec+0x20)
    fx.set_editor_property("material",get_fx_material(em.get("tex",""),blend_add))
    if blend_add=="mod":
        # D3D 6:4: вторая (additive) секция пары — рантайм рисует её в SectionIdx+NumEmitters
        fx.set_editor_property("material_b",get_fx_material(em.get("tex",""),"modadd"))
    if t in (0xD,3,0x1B,0x17,0x20):
        # 0x17 = grid-частицы «блёстки» крыльев (§D wings_soulmate_re): 0xD-подобные частицы + атлас-раскадровка UV.
        # ★ 0x20 ДОБАВЛЕН 2026-08-07 (jeb_emitters_0x14_0x19.md): партикл-эмиттер класса 0xE0C0,
        #   лоадер CEffect_LoadEmitterType32@0x117E190. Парсер (extract_type20) отдаёт ТЕ ЖЕ ключи,
        #   что и 0xD, — семантика частиц идентична, отличается только раскладка на диске.
        #   Всего 1 эмиттер в архиве (eff_1_char_02_drgflye02-a_00), на картах НЕ встречается.
        period=max(p.get("period",5.0),0.05)
        fx.set_editor_property("period",period)
        fx.set_editor_property("end_mode",p.get("endMode",3))
        fx.set_editor_property("atlas_cols",int(p.get("atlasCols",1) or 1))   # атлас-раскадровка (0x17/0x1B): 1×1 = полный UV
        fx.set_editor_property("atlas_rows",int(p.get("atlasRows",1) or 1))
        # ★ Спавн-объём (КРУГ 3 крыльев, live cdb + мода-5 @0x1213340): ТОЛЬКО 0x17 — у 0xD/0x1B поле
        #   не сверено с их дисковой раскладкой, дефолт mode=0 (как было) по закону «общий код под флагом».
        if t == 0x17:
            fx.set_editor_property("spawn_pos_mode",int(p.get("spawnPosMode",0) or 0))
            fx.set_editor_property("spawn_pos_radius",float(p.get("spawnPosRadius",10.0) or 10.0))
        fx.set_editor_property("life_base",max(p["partLife"][0],0.05)); fx.set_editor_property("life_var",p["partLife"][1])
        fx.set_editor_property("size_x_var",p["sizeX"][2]); fx.set_editor_property("size_y_var",p["sizeY"][2])
        fx.set_editor_property("speed_var",p["speed"][2]); fx.set_editor_property("alpha_var",p["alpha"][2])
        cv=p["colorVar"]; fx.set_editor_property("color_var",unreal.Vector(cv[0],cv[1],cv[2]))
        fx.set_editor_property("gravity_var",p["gravity"][2])
        fx.set_editor_property("pos_base",to_ue_v(p["posBase"])); fx.set_editor_property("pos_range",to_ue_v([abs(x) for x in p["posRange"]]))
        # ★★★ ДВИЖЕНИЕ САМОГО ЭМИТТЕРА (2026-08-14, `Reports/item_fx_audit.md` §2/§3).
        #   Эталон `CEmitter_InitReset@0x120F0E0` + `CEmitter_Update_v3@0x1211C20`: точка эмиссии
        #   маршем едет posBase(+rand·posRange) → posTarget за период, а тройки rotX/rotY/rotZ
        #   (старт→КОНЕЦ) поворачивают её матрицей R = RotX·RotY·RotZ. Порт до этой даты не читал
        #   НИ posTarget, НИ rot ⇒ у зелий/премиум-усилений/Камня души пропадали спирали и подъём
        #   искр (72 из 113 партикл-эмиттеров канала предметов «стояли на месте»).
        #   Гейт `emitter_motion` (дефолт False) — по закону «общий код эффектов только под флагом»:
        #   старые собранные BP не меняются, поведение включает только пересборка. Риск для КАРТ
        #   измерен: во всех 23 `export/maps/*/effects.json` 275 партикл-эмиттеров и НИ ОДНОГО с
        #   posTarget != posBase, rot-полей в кэше карт нет вовсе.
        _pt = p.get("posTarget")
        if _pt is not None:
            fx.set_editor_property("pos_target", to_ue_v(_pt))
            _rx = p.get("rotX") or [0.0,0.0,0.0]; _ry = p.get("rotY") or [0.0,0.0,0.0]
            _rz = p.get("rotZ") or [0.0,0.0,0.0]
            # game-градусы (x,y,z): [0]=старт, [1]=КОНЕЦ, [2]=разброс (во всём архиве всегда 0)
            fx.set_editor_property("emit_rot_start", unreal.Vector(_rx[0], _ry[0], _rz[0]))
            fx.set_editor_property("emit_rot_end",   unreal.Vector(_rx[1], _ry[1], _rz[1]))
            fx.set_editor_property("emit_rot_var",   unreal.Vector(_rx[2], _ry[2], _rz[2]))
            fx.set_editor_property("emitter_motion", True)
        fx.set_editor_property("spawn_mode",p.get("spawnMode",0))
        fx.set_editor_property("vel_base",to_ue_v(p["velBase"])); fx.set_editor_property("vel_range",to_ue_v([abs(x) for x in p["velRange"]]))
        fx.set_editor_property("billboard_mode",p.get("billboardMode",0))
        nodes=[]
        tn=p.get("trackNodes") or []
        full=[n for n in tn if len(n)>=56]
        if full:
            # 58f-узлы (0xD): [0]=time,[13]=alpha,[15]=sizeX,[17]=sizeY,[19..21]=RGB,[34]=speed,[36]=cone,[47]=grav,[49]=rate,[53..55]=axis
            for n58 in full:
                ax=n58[53:56]
                if abs(ax[0])+abs(ax[1])+abs(ax[2])<1e-4: ax=[0,1,0]
                nodes.append(node(n58[0], n58[49], n58[15], n58[17], n58[34], n58[36], n58[13], n58[19:22], n58[47], _exhaust_axis(ax)))
        if not nodes:
            # дефолт: 2 узла birth/death; ось выхлопа = авторская матрица в мир, затем НЕГЕЙТ (авторская-up ==
            # forward носителя, замер 3/3 → выхлоп бил ВПЕРЁД; −axis = НАЗАД, шлейф за дирижаблем)
            ax=_exhaust_axis(p.get("coneAxis",[0,1,0]))
            nodes=[node(0.0, p["spawnRate"][0], p["sizeX"][0], p["sizeY"][0], p["speed"][0], p["coneAngle"][0],
                        p["alpha"][0], p["colorBirth"], p["gravity"][0], ax),
                   node(period, p["spawnRate"][1], p["sizeX"][1], p["sizeY"][1], p["speed"][1], p["coneAngle"][1],
                        p["alpha"][1], p["colorDeath"], p["gravity"][1], ax)]
        fx.set_editor_property("nodes",nodes)
        return fx
    # ★★★ ТИП 0x1 ПЕРЕВЕДЁН СЮДА 2026-08-14 (`Reports/item_fx_audit.md` §7). 2026-08-08 его
    #   добавили в НИЖНЮЮ legacy-ветку `t in (1,0xB,0xC,0x18)` — ту самую, про которую в комментарии
    #   к 0x18 написано «не переносит ни pos-канал, ни scaleX/Y, ни rot ⇒ квад 20×20 в НАЧАЛЕ
    #   КООРДИНАТ». Парсер отдаёт для типа 1 ПОЛНЫЙ набор каналов (`extract_wave(data, ro)` — тот же
    #   вызов, что и для 0xB), а вердикт того же реверса гласит: «0x01 = квад 0xB БЕЗ keyframe-хвоста»
    #   (дизасм `fxParticleUpdate_ABB0@0x120ABB0`, draw = `0x120AEB0` = draw 0xB). Значит и ветка
    #   нужна квадовая. Симптом до правки — вспышка зелий (`HP_C_SW`/`MP_P_SW00`/`EL_P_SW00`:
    #   диск pos=(0,200,0), size 0→70 за 0.3 с) строилась как константный квад 20×20 у НОГ с
    #   периодом 4 с. Риск для карт измерен: во всех `export/maps/*/effects.json` 566 эмиттеров и
    #   НИ ОДНОГО типа 0x1.
    if t in (1,2,0xB,0xC,0x13,0x14,0x18,0x19) and p.get("growQuad"):
        # ★ 0x13 / 0x14 / 0x19 ДОБАВЛЕНЫ 2026-08-07 (отчёт Reports/jeb_emitters_0x14_0x19.md).
        #   ЭТО БЫЛА ОБЩАЯ ДЫРА ПАЙПЛАЙНА: парсер эти типы разбирал, а build_emitter их не знал и
        #   молча возвращал None ⇒ 262 (0x13) + 836 (0x14) + 950 (0x19) = 2048 эмиттеров выпадали
        #   из КАЖДОГО эффекта, где они есть (430 и 273 файла). Симптом — не «эффект не работает»,
        #   а «эффект нарисован не полностью» (у хранителя души — 15 эмиттеров в 7 эффектах).
        #   Раньше дыру затыкал только генератор свечения оружия, подменявший 0x13→0x18, 0x14/0x19→0xC
        #   у себя локально; здесь она закрыта в ОБЩЕМ коде, подмена больше не нужна.
        #   ПОЧЕМУ ветка 0xC подходит 1:1 (дизасм, сверено дважды):
        #     0x19 рисуется БУКВАЛЬНО функцией 0xC — spriteBuildTransformAndDraw@0x120CF10;
        #     0x14 — effectEmitterRenderBillboardQuad@0x123F3E0, копия той же функции без UV-скролла;
        #     0x13 — emitterBuildAndDrawBillboard@0x123E5B0 (квад-семейство 0xB/0x18).
        #   Геометрия 0xC/0x14/0x19: V=(∓halfW,±halfH,0) — квад в плоскости XY, нормаль = Z; roll-канал
        #   @0xDC уходит в RotZ = спин ВОКРУГ НОРМАЛИ СПРАЙТА (у наземного квада 0xB та же семантика
        #   даёт RotY — оси пер-типовые, но смысл один; рантайм уже разводит их по bBaseAlreadyUE).
        #   Хиральность/зеркаливание, масштаб и бленд наследуются от общей ветки без изменений:
        #   _nz() применяется к basePos/pos ровно как у 0xB/0xC, геометрия квада ×actorScale,
        #   размер частиц тут не участвует, бленд — пер-контейнер (mod → пара Modulate+Additive).
        #   РЕГРЕСС-БЕЗОПАСНО: скан всех 23 export/maps/*/effects.json — типы 0x13/0x14/0x19 на картах
        #   НЕ ВСТРЕЧАЮТСЯ НИ РАЗУ (карты используют только 0x03/0x0B/0x0C/0x0D/0x0E/0x12/0x18),
        #   поэтому флаг не нужен: принятые эффекты карт физически не могут измениться.
        # ★ тип 2 ДОБАВЛЕН 2026-08-03 (эффект появления cmchar01): SPEC_TAXONOMY класс B QUAD-ORIENTED,
        #   близнец 0xC (Draw spriteBuildTransformAndDraw@0x120CF10), диск = тип 1 + billboardMode@0x8C
        #   (SPEC_DISKMAP_ALL §1). SINGLE (без kf-хвоста) ⇒ ветка «без узлов» START→END. Раньше тип 2
        #   молча пропускался (return None) — финальная вспышка cmchar01 теряется. Тип 1 НЕ трогаем
        #   (закон «общий код эффектов только под флагом»: карты калиброваны без него).
        # ★ 0x18 ДОБАВЛЕН 2026-07-31 (баг «эффект оружия утоплен в модель»): 0x18 = 0xB + пара atlasCols/Rows
        #   (иерархия ctor'ов, weapon_glow_re.md §D), парсер отдаёт для него ПОЛНЫЙ набор каналов
        #   (extract_wave sh=8). Раньше 0x18 проваливался в нижнюю legacy-ветку, которая НЕ переносит
        #   ни pos-канал, ни scaleX/Y, ни rot ⇒ все полотна свечения схлопывались в квад 20×20 в НАЧАЛЕ
        #   КООРДИНАТ КОСТИ (= внутри рукояти/кисти) вместо клинка. Замер: ohs_a_co_ma_03 — на диске
        #   posStart=(2.2,0,90) scale=(32,80), в BP было pos=(0,0,0) size=(20,20).
        # ★ 0xB = keyframe-квад плоскости ЗЕМЛИ. ИСТИННАЯ раскладка (лоадер-трейс effect_loaders_pertype.md):
        #   каналы (start,END,var) лерпятся за life (2 сид-записи трека); БЕЗ эвристик — данные сами различают:
        #   s==E → константа (орбита flooer), s≠E → лерп (дрейф wave/pado). ДВА офсет-канала:
        #   pos (ring_pos_ofs) = орбитальный (канальные вращения + матрица узла); basePos (ring_base_pos) = сырой мир.
        #   rot-каналы уже в верном маппинге парсера: rotY1@0xDC→roll(RY), rotX@0xE8→RX, rotY@0xF4→RY, rotZ@0x100→RZ.
        # ★ ДЛИТЕЛЬНОСТЬ КВАДА КЛАМПИТСЯ [0.1, 10.0] — как у кольца: сброс квад-эмиттера
        #   `quadEmitterReset@0x12182D0` зовёт `fxParticleSpawnRandomize@0x1208F90`, а тот пишет
        #   `lifetime = clamp(lifeBase + randSym()·lifeVar, 0.1, 10.0)` (константы 1036831949=0.1f /
        #   1092616192=10.0f). Раньше здесь стоял только пол 0.05 ⇒ у записи с `life = 0` анимация
        #   и период рестарта шли ВДВОЕ быстрее эталона. Замер архива: квадов с `life < 0.1` — 56
        #   (все в `eff_1_char_02_cycutb01-a_11`), с `life > 10` — 0; на картах ни одного (риск 0).
        life=min(max(p.get("life",[1.0])[0],0.1),10.0)
        tn=p.get("trackNodes") or []
        def _wq(tt, sx, sy, a, rgb, roll, rot, u, v, bp, po):
            # ★ БЕЗ пола размера: эталон строит вершины квада прямо из полу-экстентов
            #   (`effectQuad_updateAndDrawBillboard@0x120AEB0`), пола/клампа у него нет. Порог теперь
            #   ОДИН и живёт в рантайме — `asda.Fx.MinQuadSize` (дефолт 0). Прежний `max(size,1.0)`
            #   здесь ЗАПЕКАЛ пол в ассет, из-за чего рантайм-порог нечем было опустить ниже 1.0.
            nd=node(tt,0,sx,sy,0,0,min(max(a,0.0),1.0),[min(max(c,0.0),1.0) for c in rgb],0,[0,1,0])
            nd.set_editor_property("ring_roll",float(roll))
            nd.set_editor_property("ring_rot",unreal.Vector(rot[0],rot[1],rot[2]))
            nd.set_editor_property("ring_u",float(u)); nd.set_editor_property("ring_v",float(v))
            nd.set_editor_property("ring_base_pos",unreal.Vector(bp[0],bp[1],bp[2]))
            nd.set_editor_property("ring_pos_ofs",unreal.Vector(po[0],po[1],po[2]))
            return nd
        cS=p.get("colorStart",[1,1,1]); cE=p.get("colorEnd",[0,0,0])
        if tn:
            # kf-track (pado): 40f-узлы [0]t,[1/3/5]basePos,[7/9/11]pos,[13]alpha,[15/17]halfW/H,[19-21]RGB,
            # [28]rotY1,[30]rotX,[32]rotY,[34]rotZ,[36/38]uv. Сид t=0 из base-полей, далее узлы.
            sx0=p.get("scaleX",[200,0])[0]; sy0=(p.get("scaleY") or [sx0])[0]; a0=(p.get("alpha") or [0])[0]
            nodes=[_wq(0.0, sx0, sy0, a0, cS, (p.get("rotY1") or [0])[0],
                       [(p.get("rotX") or [0])[0],(p.get("rotY") or [0])[0],(p.get("rotZ") or [0])[0]],
                       (p.get("uScroll") or [0])[0], (p.get("vScroll") or [0])[0],
                       _nz(p.get("basePosStart",[0,0,0])), _nz(p.get("posStart",[0,0,0])))]
            for n40 in tn:
                bp=_nz([n40[1], n40[3], n40[5]])                 # basePos-канал узла (сырой мир)
                po=_nz([n40[7], n40[9], n40[11]])                # pos-канал узла (орбитальный)
                nodes.append(_wq(min(n40[0], life), n40[15], n40[17], n40[13], list(n40[19:22]),
                                 n40[28], [n40[30],n40[32],n40[34]], n40[36], n40[38], bp, po))
            if kf_seed_nodes and _kf_end_seed():
                # ★★★ ВОССТАНОВЛЕНО 2026-08-15 (`Reports/weapon_fx_loop.md` §2.3) — ветка снова
                #   ВКЛЮЧЕНА по умолчанию. Отключение 14.08 опиралось на недостижимость ветки
                #   `a2[27]==0` у `animSampleCurveAtTime@0x11FCEC0` (это верно) и молча предполагало,
                #   что КОНЕЦ-узел больше строить некому (это НЕВЕРНО): его строит
                #   `animComputeKeyframeDeltas@0x1202BE0` — переписывает ПОСЛЕДНИЙ элемент списка
                #   временем `billboardMode` (рандомизированный lifetime) и КОНЕЧНЫМИ сидами записи
                #   (`velTarget`/`posTarget`/`colorEnd`/`colorEndG,B`/`lifeTarget`). У неё 10 вызовов,
                #   и лоадер `CEffectElement_LoadType13@0x12809B0` зовёт её ДО чтения ключей файла.
                #   Полный разбор и ещё три независимых подтверждения — в шапке `_kf_end_seed()`.
                #   Откат: `ASDA_FX_NO_KF_END_SEED=1` + пересборка канала.
                sXe=(p.get("scaleX") or [200,200,0]); sYe=(p.get("scaleY") or sXe)
                nodes.append(_wq(life, sXe[1], sYe[1], (p.get("alpha") or [1,0,0])[1], cE,
                                 (p.get("rotY1") or [0,0,0])[1],
                                 [(p.get("rotX") or [0,0,0])[1],(p.get("rotY") or [0,0,0])[1],
                                  (p.get("rotZ") or [0,0,0])[1]],
                                 (p.get("uScroll") or [0,0,0])[1], (p.get("vScroll") or [0,0,0])[1],
                                 _nz(p.get("basePosEnd",[0,0,0])), _nz(p.get("posEnd",[0,0,0]))))
            nodes.sort(key=lambda nd: nd.get_editor_property("time"))
        else:
            # без узлов (wave/flooer): 2 узла START→END из base-троек (лерп; s==E даёт константу естественно)
            sX=p.get("scaleX",[75,300,0]); sY=p.get("scaleY",sX)
            al=p.get("alpha",[1,0,0]); uS=p.get("uScroll",[0,0,0]); vS=p.get("vScroll",[0,0,0])
            r1=p.get("rotY1",[0,0,0]); rX=p.get("rotX",[0,0,0]); rY=p.get("rotY",[0,0,0]); rZ=p.get("rotZ",[0,0,0])
            bp0=_nz(p.get("basePosStart",[0,0,0])); bp1=_nz(p.get("basePosEnd",[0,0,0]))
            po0=_nz(p.get("posStart",[0,0,0]));     po1=_nz(p.get("posEnd",[0,0,0]))
            nodes=[_wq(0.0,  sX[0],sY[0],al[0],cS, r1[0],[rX[0],rY[0],rZ[0]],uS[0],vS[0],bp0,po0),
                   _wq(life, sX[1],sY[1],al[1],cE, r1[1],[rX[1],rY[1],rZ[1]],uS[1],vS[1],bp1,po1)]
        fx.set_editor_property("single_quad",True)
        fx.set_editor_property("period",life)
        fx.set_editor_property("end_mode", _end_mode() if etalon_end else 1)          # restart-пульс (карта вечная)
        fx.set_editor_property("billboard_mode",int(p.get("billboardMode",0)))        # 0xC: 1/3=сфер.биллборд, 2=lookAt
        # атлас-раскадровка (0x18/0x13: cols·rows>1; у 0xB/0xC на диске полей нет ⇒ 1×1 = полный UV)
        fx.set_editor_property("atlas_cols",int(p.get("atlasCols",1) or 1))
        fx.set_editor_property("atlas_rows",int(p.get("atlasRows",1) or 1))
        fx.set_editor_property("nodes",nodes)
        return fx
    if t in (1,0xB,0xC,0x18):
        # SINGLE-QUAD keyframe-биллборд (агент §10: 0xB=0xC=0x18 семантика идентична, НЕ эмиттеры).
        # ★ ТИП 1 ДОБАВЛЕН 2026-08-08 (jeb_emitters_0x14_0x19.md §9). Прежний вердикт «0x01 — партикл-
        #   спавнер, отправлять в квад-ветку = гадание» ОПРОВЕРГНУТ дизасмом: `fxParticleUpdate_ABB0`
        #   @0x120ABB0 разобран целиком — НИ ОДНОГО ЦИКЛА, слотов частиц нет, единственная ветвь
        #   `cmp [ebp-0xC],3` = switch по `endMode`; тело = линейный марш каналов 0xB/0xC/0x13 +
        #   uScroll/vScroll; draw = `0x120AEB0` = ТОТ ЖЕ draw, что у 0xB; `SINGLE[1]=0x174` = контент 0xB.
        #   ⇒ 0x01 = квад 0xB БЕЗ keyframe-хвоста (ровно как 0x04 = кольцо 0xE без хвоста, уже в ring-ветке).
        #   Это последние 20 эмиттеров до 100 % покрытия; на картах тип 1 не встречается (риск измерен).
        # 40f-подузлы: [0]=time,[13]=alpha,[15]=sizeX,[17]=sizeY,[19..21]=RGB (нечётные=delta, на диске мусор).
        period=max(p.get("period",4.0),0.05)
        fx.set_editor_property("end_mode",p.get("endMode",3))
        fx.set_editor_property("single_quad",True)
        rgb=p.get("quadColorFallback") or p.get("colorBase") or [1,1,1]
        alpha=min(max(p.get("quadAlphaFallback",p.get("alpha",[1,1])[0]),0.0),1.0)
        sx=max(p.get("sizeXcur",0.0),p.get("sizeBase",0.0),20.0); sy=p.get("sizeYcur",0.0) or sx
        # ★ 0xC keyframe-трек (CONFIRMED дизасм animAdvanceKeyframe@0x11FA8E0): subCount=1 → рантайм строит 3 узла
        #   START(t=0)+ключ(t=1,диск)+END(t=period), loop=RESTART форвард (spawnSpread@+0x88; endMode@0x164=мусор).
        #   alpha=КОНСТАНТА 1.0 (не мерцает); «мерцание яркости» = RGB-канал под additive: сиды colorBase (тёмные,
        #   края) → ключ (ярче) → назад. Края РАВНЫ ⇒ бесшовно. rot 0→фаза→0 (качание). posY (node[9]) уже в ring_base_pos.
        cbase=[min(max(c,0.0),1.0) for c in (p.get("colorBase") or [0,0,0])]   # тёмные сиды RGB (края мерцания)
        tn=p.get("trackNodes") or []
        if tn:
            n40=tn[0]
            kx=n40[15]; ky=n40[17]                        # ★ без пола (порог — asda.Fx.MinQuadSize)
            krgb=[min(max(c,0.0),1.0) for c in n40[19:22]]
            krot=unreal.Vector(n40[28],n40[30],n40[32])
            _kp=_nz([n40[1]+n40[7], n40[3]+n40[9], n40[5]+n40[11]]); kpos=unreal.Vector(_kp[0],_kp[1],_kp[2])
            def _mk(tt, crgb, rot):
                nd=node(tt,0,kx,ky,0,0,1.0,crgb,0,[0,1,0])                     # alpha=1 КОНСТАНТА; пульс — по RGB
                nd.set_editor_property("ring_rot",rot); nd.set_editor_property("ring_base_pos",kpos)
                return nd
            _z=unreal.Vector(0,0,0)
            nodes=[_mk(0.0,cbase,_z), _mk(1.0,krgb,krot), _mk(period,cbase,_z)]   # тёмный→ярче→тёмный (RESTART бесшовно)
        else:
            nodes=[node(0.0,0,sx,sy,0,0,alpha,rgb,0,[0,1,0]),
                   node(period,0,sx,sy,0,0,0.0,rgb,0,[0,1,0])]
        fx.set_editor_property("period",period)
        fx.set_editor_property("nodes",nodes)
        return fx
    if t in (0xE,4,0x15,0x1A) and p.get("ring"):
        # ★ 4 / 0x15 / 0x1A ДОБАВЛЕНЫ 2026-08-07 (jeb_emitters_0x14_0x19.md) — то же кольцо 0xE:
        #     тип 4    = 0xE без keyframe-хвоста (SINGLE 0x18C = контент 0xE);
        #     тип 0x15 = 0xE + atlasCols/Rows, без хвоста (0x18C+8 = 0x194);
        #     тип 0x1A = 0xE + atlasCols/Rows + хвост 0xB8 (0x18C+8+4 = 0x198 fixed).
        #   Порядок node-слотов в лоадерах sceneNodeLoadType4@0x1171EA0 / sceneNodeLoadType21@0x1179E80
        #   идентичен 0xE (включая ring-блок +0x11D4/11E0/11EC cylHeight/upCycle/downCycle), размеры
        #   записей сходятся точно. Итого 97 эмиттеров (84+12+1), на картах НЕ встречаются.
        # КОЛЬЦО-лента (SPEC §11): каналы start→END за lifetime (тройки (start,END,var)); loopMode→EndMode.
        life=min(max(p["life"][0],0.1),10.0)
        fx.set_editor_property("period",life)
        # ★ etalon_end: `loopMode 3` БЕЗ keyframe-хвоста = ЛУП (рестарт с базовых сидов,
        #   `effectTransformTick@0x12158B0` case 3 → `fxParticleCommitPos@0x1215830` →
        #   `fxParticleSaveTransformState@0x120AAE0`), а не фриз. Старое отображение (2→3, 3→3)
        #   оставлено дефолтом: на картах 12 таких колец (радуги/фонари/башня alpen) — принятые.
        fx.set_editor_property("end_mode", _end_mode() if etalon_end
                               else {0:0,1:1,2:3,3:3}.get(p.get("loopMode",3),3))
        fx.set_editor_property("ring",True)
        fx.set_editor_property("ring_u_tile",max(p.get("uTile",1.0),0.01))
        fx.set_editor_property("ring_va",p.get("vA",0.99) or 0.99)
        # ★ Атлас колец (только 0x15/0x1A; у 0xE и типа 4 полей на диске нет ⇒ 1×1 = полный UV).
        #   ★★★ ФЛИПБУК ОТРЕВЕРСЕН И ВКЛЮЧЁН 2026-08-08 (jeb_emitters_0x14_0x19.md §9, дизасм
        #   update-функций 0x1240250 (0x15) / 0x1247CE0 (0x1A) — единственных писателей UV):
        #     frameDur = life/(cols·rows); frame = lifeTimer/frameDur; row = frame/cols; col = frame%cols;
        #     u(i) = col/cols + (i/29)/cols;  v[низ]=(row+1)/rows;  v[верх]=row/rows   (i = сегмент 0..29)
        #   Ячейка атласа растягивается по U на всю окружность, по V — на всю высоту цилиндра;
        #   uTile/vA/uScroll оригиналом у этих типов НЕ применяются (флипбук перетирает UV целиком).
        #   Раскадровку выполняет ring-ветка AsdaEffectActor.cpp под гейтом cols·rows>1 ⇒ у 0xE и
        #   типа 4 (1×1) поведение бит-в-бит прежнее. Затрагивает 13 эмиттеров (0x15: 12, 0x1A: 1),
        #   ни один из них не встречается на картах.
        fx.set_editor_property("atlas_cols",int(p.get("atlasCols",1) or 1))
        fx.set_editor_property("atlas_rows",int(p.get("atlasRows",1) or 1))
        def tri_se(tr): return (tr[0],tr[1])                      # (start,END)
        # ★ Сид-узлы трека (kf_seed_nodes, см. шапку build_emitter): СТАРТ = «начальные» значения
        #   всех троек при t=0, КОНЕЦ = «конечные» при t=lifetime. Раскладка офсетов — ТА ЖЕ, что у
        #   узлов файла в этой ветке: ring_base_pos = negate-z(POS-канал), ring_pos_ofs = basePos
        #   (её ждёт эталонная ветка bEtalonMapPath рантайма); без ring_track_pos офсет узла = 0,
        #   и сид обязан вести себя так же, иначе трек «прыгнет» на первом же ключе.
        def _ring_seed(tt, k):
            _bp = ([p["posStart"][0], p["posStart"][1], -p["posStart"][2]] if k == 0
                   else [p["posEnd"][0], p["posEnd"][1], -p["posEnd"][2]]) if ring_track_pos else [0,0,0]
            nd = ring_node(tt, tri_se(p["alpha"])[k], p["colorStart"] if k == 0 else p["colorEnd"],
                           # ★ БЕЗ подстановки «0 → 1.0»: у эталона пола нет — вершина кольца
                           #   строится как `цилиндр · RotY(spin) · S(scale) · …` (`effectTrailRender@0x1215C10`),
                           #   т.е. `scale = 0` = кольцо РОЖДАЕТСЯ ИЗ НИЧЕГО и растёт к КОНЦУ-сиду.
                           #   Замер: таких колец в архиве 38 (scale 0→20/40/55/65), на картах 0 (риск 0).
                           tri_se(p["scale"])[k], tri_se(p["rollY"])[k],
                           [tri_se(p["rotX"])[k], tri_se(p["rotY"])[k], tri_se(p["rotZ"])[k]],
                           tri_se(p["height"])[k], tri_se(p["topR"])[k], tri_se(p["botR"])[k],
                           tri_se(p["uScroll"])[k], tri_se(p["vScroll"])[k], _bp)
            if ring_track_pos:
                _po = p["basePosStart"] if k == 0 else p["basePosEnd"]
                nd.set_editor_property("ring_pos_ofs", unreal.Vector(_po[0], _po[1], _po[2]))
            return nd
        nodes=[]
        tn=[n for n in (p.get("trackNodes") or []) if len(n)>=45]
        if tn and kf_seed_nodes:
            nodes.append(_ring_seed(0.0, 0))
        if tn:
            # 46f-узлы (§11.4, float-индексы): alpha[13], scale[15], RGB[19..21], rollY[28], rotX[30],
            # rotY[32], rotZ[34], u[36], v[38], height[40], topR[42], botR[44]
            for n46 in tn:
                # ★ ОФСЕТ УЗЛА. Раньше здесь стояла константа [0,0,0] — данные узла терялись (см. шапку
                #   build_emitter, ring_track_pos). Раскладка 46f-узла (значения на НЕЧЁТНЫХ индексах,
                #   чётные = дельты) сверена сырым дампом записи bali_e_co_ma_03:
                #     [1/3/5]  = basePos-канал  (+0x3FC, диск rec+0x144) — у ВСЕХ колец свечения 0;
                #     [7/9/11] = POS-канал      (+0x3E4, диск rec+0x168) — у ccyl1 = (70,−5,19),
                #                бит-в-бит равно posStart из base-полей.
                #   Атрибуция каналов — по КАНОНУ 0xE (Docs/Port/systems/EFFECTS_CONVERSION_MODEL.md, дизасм
                #   ring-Draw 0x1215C10): кросс-проверена радугой eff_rainbow_a_00 — её живой posChan
                #   (5.5, 0, +1.75) лежит на диске ровно по 0x168/0x16C/0x170.
                #   Карт-рантайм (эталонная ветка) ждёт: ring_base_pos = negate-z(POS-канал)
                #   (он сам снимает негацию), ring_pos_ofs = basePos (сырой мир). Та же схема, что
                #   у pure_ring_attached — иначе pos-канал не пройдёт через R²·N48·N520.
                npos  = _nz([n46[7], n46[9], n46[11]])
                nbase = _nz([n46[1], n46[3], n46[5]])
                nbp = [npos[0], npos[1], -npos[2]] if ring_track_pos else [0,0,0]
                nd = ring_node(min(n46[0], life) if kf_seed_nodes else n46[0],
                               min(max(n46[13],0.0),1.0), [min(max(c,0.0),1.0) for c in n46[19:22]],
                               n46[15], n46[28], [n46[30],n46[32],n46[34]],   # ★ без пола 1.0 (см. _ring_seed)
                               n46[40], n46[42], n46[44], n46[36], n46[38], nbp)
                if ring_track_pos:
                    nd.set_editor_property("ring_pos_ofs", unreal.Vector(nbase[0], nbase[1], nbase[2]))
                nodes.append(nd)
            if kf_seed_nodes and _kf_end_seed():
                # ★★★ ВОССТАНОВЛЕНО 2026-08-15 — доказательство в квадовой ветке выше и в шапке
                #   `_kf_end_seed()`. Для КОЛЕЦ это критично вдвойне: у 778 из 1152 эмиттеров канала
                #   заточки конечные сиды РАВНЫ начальным ⇒ КОНЕЦ-узел замыкает цикл БЕСШОВНО, а без
                #   него режим РЕСТАРТ (`loopMode = 1`) даёт видимый рывок раз в период — ровно то,
                #   что юзер увидел на «столбах света» баллисты (`bali_e_*_ma_03`, ccyl1..4:
                #   alpha 0.196 → ключ 0.588 @ t=life/2 → КОНЕЦ 0.196).
                nodes.append(_ring_seed(life, 1))              # КОНЕЦ-узел @ t=lifetime
            nodes.sort(key=lambda nd: nd.get_editor_property("time"))
        else:
            s0,s1=tri_se(p["scale"]); a0,a1=tri_se(p["alpha"]); r0,r1=tri_se(p["rollY"])
            x0,x1=tri_se(p["rotX"]); y0,y1=tri_se(p["rotY"]); z0,z1=tri_se(p["rotZ"])
            h0,h1=tri_se(p["height"]); t0,t1=tri_se(p["topR"]); b0,b1=tri_se(p["botR"])
            u0,u1=tri_se(p["uScroll"]); v0,v1=tri_se(p["vScroll"])
            if pure_ring_attached:
                # ★ ЭТАЛОН (см. шапку build_emitter): ring_base_pos = negate-z(POS-канал), ring_pos_ofs = BASEPOS (сырой мир)
                bp0=[p["posStart"][0], p["posStart"][1], -p["posStart"][2]]
                bp1=[p["posEnd"][0],   p["posEnd"][1],   -p["posEnd"][2]]
                po0=list(p["basePosStart"]); po1=list(p["basePosEnd"])
            else:
                # офсет кольца в BONE-LOCAL = POS + BASEPOS (формула MapViewer: MatrixTranslate(pos+basePos)·boneW)
                bp0=_nz([p["basePosStart"][k]+p["posStart"][k] for k in range(3)])
                bp1=_nz([p["basePosEnd"][k]+p["posEnd"][k] for k in range(3)])
                po0=po1=None
            # rot-каналы колец БЕЗ свопа (кольца 0xE чинились ранее и ПОДТВЕРЖДЕНЫ пользователем — НЕ ТРОГАТЬ;
            # своп Y↔Z применим ТОЛЬКО к квадам 0xB, где подтверждён визуалом flooer-спина)
            # ★ без пола 1.0 (см. _ring_seed): `scale = 0` у эталона = кольцо рождается из ничего
            nodes=[ring_node(0.0, a0, p["colorStart"], s0, r0, [x0,y0,z0], h0, t0, b0, u0, v0, bp0),
                   ring_node(life, a1, p["colorEnd"],  s1, r1, [x1,y1,z1], h1, t1, b1, u1, v1, bp1)]
            if po0 is not None:
                nodes[0].set_editor_property("ring_pos_ofs", unreal.Vector(*po0))
                nodes[1].set_editor_property("ring_pos_ofs", unreal.Vector(*po1))
        fx.set_editor_property("nodes",nodes)
        return fx
    if em.get("mesh"):
        # .jem модель-эмиттер (0x11/0x12/0x1F): статичный меш кристалла/модели
        _load_jem_module()
        meshes=_jem.load_jem(em.get("jem") or em["name"]) or []
        if not meshes:
            # ★★★ Закон jeb-emitter-routing-hole (2026-08-10): здесь был ВТОРОЙ НЕМОЙ `return None`
            #   в том же файле — меш-эмиттер (0x11/0x12/0x1F) без разобранного .jem исчезал молча,
            #   ровно как когда-то исчезали 0x13/0x14/0x19. Проверка по эффектам навыков: 78/78
            #   меш-эмиттеров свой .jem находят, но ветка обязана КРИЧАТЬ, а не глотать.
            print("[эффекты] ВНИМАНИЕ: меш-эмиттер '%s' (тип 0x%X) ПРОПУЩЕН — .jem '%s' не разобран"
                  % (em.get("name"), em.get("type", 0), em.get("jem") or em.get("name")))
            return None
        # ★★★ 30.10.2026: многомешевый .jem — вся модель (раньше `m=meshes[0]`), разбор — в шапке jem_render_mesh.
        m=jem_render_mesh(meshes, em)
        fx.set_editor_property("mesh_emitter",True)
        apply_jem_geometry(fx, m, em.get("jem") or em["name"])
        fx.set_editor_property("material",get_fx_material(m["tex"],blend_add))
        if blend_add=="mod":
            # ★ 10.10.2026 (`digging_hole.md`): парная ModAdd-секция меша — с текстурой МЕША (.jem). Раньше её ставила общая
            #   строка выше по имени текстуры записи эмиттера (`default.bmp` у всех мешей) ⇒ `MI_default_modadd`. Действует
            #   только при пересборке; уже собранные BP (19 меш-эмиттеров в контейнерах dst=3) не меняются.
            fx.set_editor_property("material_b",get_fx_material(m["tex"],"modadd"))
        if p.get("mesh12"):
            # ★ 0x12/0x11/0x1F меш-каналы (лоадер-трейс 0x1178530): W=Scale·RotX·RotY·RotZ·parent; трансляция +=
            #   offsetWorld(сырой мир) + parent; локальный pos вращается матрицей. 2 узла START→END за life.
            #   Узел: cone_axis=scale3, ring_rot=(rotX,rotY,rotZ), ring_pos_ofs=posLocal, ring_base_pos=offsetWorld.
            life=max(p.get("life",[10.0])[0],0.1)
            cS=p.get("colorStart",[1,1,1]); cE=p.get("colorEnd",[1,1,1]); al=p.get("alpha",[1,1,0])
            sx=p.get("scaleX",[1,1,0]); sy=p.get("scaleYm",sx); sz=p.get("scaleZ",sx)
            rX=p.get("rotX",[0,0,0]); rY=p.get("rotY",[0,0,0]); rZ=p.get("rotZ",[0,0,0])
            ow0=_nz(p.get("offsetWorldStart",[0,0,0])); ow1=_nz(p.get("offsetWorldEnd",[0,0,0]))
            pl0=_nz(p.get("posStart",[0,0,0])); pl1=_nz(p.get("posEnd",[0,0,0]))
            def _mq(tt,i):
                nd=node(tt,0,1,1,0,0,min(max(al[i],0.0),1.0),[min(max(c,0.0),1.0) for c in (cS if i==0 else cE)],0,[0,1,0])
                nd.set_editor_property("cone_axis",unreal.Vector(sx[i] or 1.0, sy[i] or 1.0, sz[i] or 1.0))   # scale3 БЕЗ to_ue-свопа
                nd.set_editor_property("ring_rot",unreal.Vector(rX[i],rY[i],rZ[i]))
                nd.set_editor_property("ring_pos_ofs",unreal.Vector(*(pl0 if i==0 else pl1)))
                nd.set_editor_property("ring_base_pos",unreal.Vector(*(ow0 if i==0 else ow1)))
                return nd
            fx.set_editor_property("period",life)
            fx.set_editor_property("end_mode", _end_mode() if etalon_end else 1)
            # ★★★ ФАЙЛОВЫЕ КЛЮЧИ МЕША (2026-08-15, ЗАМЕР ЖИВОГО ОРИГИНАЛА,
            #   `Reports/fx_pipeline_ground_truth.md` §8): ветка строила РОВНО два сид-узла и
            #   `trackNodes` не читала ВООБЩЕ — 200 эмиттеров (176 × 0x12 + 24 × 0x1F) теряли всю
            #   серединную анимацию. Замер: `effectAnimTickKeyframe@0x1239250` (0x12) и
            #   `CEmitter_Mesh_Update@0x123BA70` (0x1F) — та же машина, что у 0x0B: список трека
            #   `+0x724`, `next` узла `+0xA0`, марш дельтой узла `курсор+1` + снап на узле.
            #   В живом процессе у меша `SW02_M00` (dur 2.0) трек = [t=0 сид] + [t=1.0 ключ файла,
            #   RGB 0→0.498, rotY 0→50] + [t=2.0 КОНЕЦ-сид] — ровно как у квадов.
            #   Соответствие «слот ключа → канал узла» ЗАМЕРЕНО (слот i лежит по смещению узла 4·i):
            #     [0]=время [1/3/5]=offsetWorld [7/9/11]=posLocal [13]=alpha [15/17/28]=scale XYZ
            #     [19..21]=RGB [30/32/34]=rot XYZ.  Кросс-проверка на трёх эмиттерах:
            #     `eff_0_othr_99_pmhpre01-a_01/mesh` слот[3]=200=offsetWorld.Y, слот[32]=−90/−180/
            #     −270/−360=rotY (диск rotY END −360); `eff_1_char_02_fatale01-a_00/FSE00_M00`
            #     слоты[19..21] 0→1→0 = БЕЛАЯ ВСПЫШКА, которой у нас не было совсем;
            #     живые байты узла `SW02_M00` дают те же смещения 0x34/0x4C..0x54/0x80.
            #   Время ключа клампится до life (как у эталонной вставки, ср. `0x11F7270`).
            #   Гейт `ASDA_FX_NO_MESH12_KEYS=1` — вернуть прежние 2 узла.
            fx.set_editor_property("nodes",[_mq(0.0,0)]+mesh12_key_nodes(p,life,_nz)+[_mq(life,1)])
        return fx
    # ★★★ СИСТЕМНАЯ СТРАХОВКА (2026-08-07). До неё этот `return None` был НЕМЫМ: тип, не попавший
    #   ни в одну ветку выше, пропадал БЕЗ СТРОЧКИ В ЛОГ. Так дважды теряли эмиттеры — сперва 0x17,
    #   потом 0x13/0x14/0x19/0x04/0x15/0x1A/0x20 (2176 штук по проекту, покрытие 89.8% вместо 99.9%);
    #   эффект «выглядел нормально», просто недорисовывался. Дыру каждый раз находили случайно.
    #   Теперь любой немаршрутизированный тип кричит в лог — третьего раза не будет.
    #   Ожидаемо непокрытые на 2026-08-07: 0xE (trail, 30-сегментная геометрия — отдельная задача),
    #   0x01 (draw квадовый, но update = партикл-спавнер — отправлять «по аналогии» = гадание).
    _t = em.get("type") if isinstance(em, dict) else getattr(em, "type", None)
    print(f"[эффекты] ВНИМАНИЕ: эмиттер типа 0x{_t:X} не маршрутизирован — ПРОПУЩЕН "
          f"(build_emitter не имеет ветки для этого типа)" if isinstance(_t, int)
          else f"[эффекты] ВНИМАНИЕ: эмиттер неизвестного вида не маршрутизирован — ПРОПУЩЕН: {_t!r}")
    return None

def main():
    jpath=os.path.join(RAW_DIR,MAP_NAME,"effects.json")
    if not os.path.isfile(jpath):
        unreal.log_warning("Нет %s"%jpath); return
    data=json.load(open(jpath,encoding="utf-8"))
    mbpath=os.path.join(RAW_DIR,MAP_NAME,"effect_meshbones.json")   # маппинг attach-кость→декорируемая меш-кость (хиральность)
    meshbone_map=json.load(open(mbpath,encoding="utf-8")) if os.path.isfile(mbpath) else {}
    bbpath=os.path.join(RAW_DIR,MAP_NAME,"effect_bone_binds.json")  # bind-повороты костей (boneW = conv_bind·pmat, калибр. по радуге 4e-8)
    bone_binds=json.load(open(bbpath,encoding="utf-8")) if os.path.isfile(bbpath) else {}
    # ★★★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): ASDA_MAP со списком карт
    #   не сверяется (map_asset_name только чистит символы), поэтому ASDA_MAP=Login даёт
    #   FX_MAT=/Game/Map/Login/EffectMat и FX_TEX=/Game/Map/Login/EffectTex — папки под деревом сцены логина.
    #   Ниже идёт delete_directory(FX_MAT) (снос папки ЦЕЛИКОМ), пересоздание MIC, загрузка уровня и правка
    #   акторов. Без явного разрешения владельца останавливаемся ДО удаления.
    import asda_login_guard as _LG_map
    if _LG_map.is_login_owned_package(FX_MAT):
        _LG_map.forbid_login_write("asda_place_effects.py", "карта " + str(MAP_NAME) + " -> " + FX_MAT + " (сцена логина)")
    # ★★★ 16.09.2026 (ревизия savedir3, СРЕДН-2): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json).
    #   Этот инструмент создаёт и СНОСИТ целые деревья под /Game/Map/<ASSET>, поэтому опечатка в ASDA_MAP здесь
    #   дороже всего: delete_directory(FX_MAT) сносит папку ЦЕЛИКОМ, а MIC эффектов пересоздавались бы
    #   под /Game/Map/<опечатка>/EffectMat. Реестр — единый источник имён [[map-registry-single-source]].
    #   ★ 16.09.2026 (ревизия savedir7, СРЕДН-1): гейту отдаётся ПОСТРОЕННЫЙ путь, а не сырое имя из ASDA_MAP.
    #   Инструмент делает папку из сырого значения (map_asset_name: всё, кроме [A-Za-z0-9_], → «_»), поэтому
    #   «/Game/Map/alpen» в переменной давало папку «/Game/Map/_Game_Map_alpen» — а гейт, судивший имя после
    #   своей нормализации, такой вход пропускал. Судим ровно ту строку, в которую пойдёт запись.
    if _LG_map.map_name_blocked(FX_MAT, "asda_place_effects.py"):
        raise SystemExit(3)
    if EAL.does_directory_exist(FX_MAT):
        EAL.delete_directory(FX_MAT)          # чистая регенерация MIC (старые underscore-имена без текстур)
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not EAL.does_asset_exist(DEST_LEVEL):
        unreal.log_warning("Нет уровня %s"%DEST_LEVEL); return
    les.load_level(DEST_LEVEL)
    # ★★★ ПАТЧ-РЕЖИМ (`ASDA_MAP_PATCH_ONLY=1`, этап 3 раскатки сид-узлов): НЕ пересоздавать акторы,
    #   а только переписать у существующих массив `emitters`. Причина: обычный переимпорт УНИЧТОЖАЕТ
    #   все `MapEffect*` и спавнит заново — вместе с РУЧНЫМИ правками пользователя (башня alpen
    #   ставилась руками, см. память effects-conversion-model). В патч-режиме трансформы, привязки,
    #   хиральность и прочие настройки акторов остаются ровно как есть.
    patch_only = os.environ.get("ASDA_MAP_PATCH_ONLY", "") == "1"
    existing = {}
    if patch_only:
        for a in eas.get_all_level_actors():
            lbl = a.get_actor_label()
            if lbl.startswith("MapEffect") and a.get_class().get_name().startswith("AsdaEffectActor"):
                existing[lbl] = a
        unreal.log_warning("[эффекты] ПАТЧ-РЕЖИМ: найдено %d акторов MapEffect — правим только emitters" % len(existing))
    else:
        for a in list(eas.get_all_level_actors()):
            if a.get_actor_label().startswith("MapEffect"):
                try: eas.destroy_actor(a)
                except Exception: pass
    # скелетные акторы карты (для attach type-2: эффект следует за костью анимированного объекта)
    skel_actors=[a for a in eas.get_all_level_actors() if a.get_class().get_name()=="SkeletalMeshActor"]
    def find_skel(pmat):
        tgt=to_ue_v([pmat[12],pmat[13],pmat[14]])
        best=None; bd=1e18
        for a in skel_actors:
            d=(a.get_actor_location()-tgt).length()
            if d<bd: bd=d; best=a
        return (best,bd) if best and bd<1500 else (None,bd)

    placed=0; legacy=0; meshskip=0; nojeb=0; attached=0; _lbl_counts={}
    for pl in data["placements"]:
        jb=data["jeb"].get(pl["name"])
        if not jb: nojeb+=1; continue
        # ★ хиральная нормаль отражения basePos (только type-2 attached): local-Z декорируемой меш-кости в game-локале
        reflect_n=None
        # ★ чистый 0xE type-2 (фонари/радуги): эталонная схема БЕЗ хиральности (см. шапку build_emitter)
        pure_ring = pl.get("type")==2 and pl.get("attached") and \
            all(e["type"]==0xE and not e.get("mesh") for e in jb["emitters"])
        if pl.get("type")==2 and pl.get("attached") and not pure_ring:
            _par,_=find_skel(pl["pmat"])
            _mb=meshbone_map.get("clip_%s:%s"%(pl.get("clip"),pl.get("boneName")))
            reflect_n=compute_reflect_normal(_par,_mb,pl["matrix"])
        ems=[]
        for em in jb["emitters"]:
            _arot=None
            if pl.get("type")==2 and pl.get("attached"):
                _m=pl["matrix"]
                _arot=[]
                for _ri in range(3):                                  # нормированные строки авторской матрицы (game)
                    _row=[_m[_ri*4],_m[_ri*4+1],_m[_ri*4+2]]
                    _L=(_row[0]**2+_row[1]**2+_row[2]**2)**0.5 or 1.0
                    _arot.append([_c/_L for _c in _row])
            # blendMode из кэша (новый экспорт); фолбэк по blendDst (старый кэш): 1=add, 3=mod, иначе alpha
            _bd=jb.get("blendDst",1)
            _bm=jb.get("blendMode") or ("add" if _bd==1 else ("mod" if _bd==3 else "alpha"))
            # ★★★ kf_seed_nodes ДЛЯ КАРТ — этап 3 раскатки, ОТДЕЛЬНЫЙ подэтап
            #   (`Reports/item_fx_ring_motion.md` §«карты»). Гейт — переменная окружения
            #   `ASDA_MAP_KF_SEED=1`, ДЕФОЛТ ВЫКЛЮЧЕН: на картах 26 уникальных `.jeb` с keyframe-
            #   ключами (13 колец, ВСЕ 13 с единственным ключом, и 51 квад), и это ПРИНЯТЫЕ
            #   пользователем визуально эффекты (alpen, flammio, inferion, palace_of_queen,
            #   оба castle_of_chess). Откат подэтапа = перезапечь карты без этой переменной.
            # ★★★ ASDA_MAP_ETALON_END (подэтап «карты» аудита масштабов, `fx_scale_timing_audit.md` §10),
            #   ДЕФОЛТ ВЫКЛЮЧЕН. Правит РОВНО ОДИН класс: `loopMode/endMode = 3` БЕЗ keyframe-ключей
            #   у эталона = ЛУП (`effectTransformTick@0x12158B0` case 3: `lifeTimer = 0` +
            #   `fxParticleCommitPos@0x1215830` → `fxParticleSaveTransformState@0x120AAE0` — возврат
            #   к базовым сидам), а у нас это был ФРИЗ ⇒ прокрутка радуг/фонарей/башни alpen
            #   останавливалась через `life` секунд. `holder_restart=True` не даёт превратить
            #   эмиттерный СТОП в «погасло навсегда»: у эффекта карты есть холдер (dead → restart).
            # ★★★ [ПОРТ] ФИКС СМЕЩЕНИЯ КОЛЕЦ 0xE (2026-09-10). ПЕРВОПРИЧИНА: контракт pos-канала между
            #   печкой и рантаймом соблюдался ТОЛЬКО для «чистых» кольцевых эффектов (`pure_ring`,
            #   гейт 31.07 — ВСЕ эмиттеры .jeb должны быть 0xE). Карт-ветка рантайма
            #   (`bEtalonMapPath`, `AsdaEffectActor.cpp`) БЕЗУСЛОВНО читает `ring_base_pos` как
            #   pos-канал С НЕГИРОВАННЫМ z (`PC=(x,y,−z)`, снятие негации парсера), а легаси-ветка
            #   печки клала туда `basePos+pos` БЕЗ негации (и жёсткий [0,0,0] при keyframe-узлах).
            #   Итог: у эффекта с ХОТЯ БЫ ОДНИМ не-кольцевым эмиттером кольца уезжали (башня alpen —
            #   101 и 262 юн; вода замков — 608/500/224; соулидол — 250/200; неоалпеншоп — 60).
            #   Гейт — ПО ТИПУ ЭМИТТЕРА, а не по составу файла: семантика 0xE не может зависеть от
            #   того, лежит ли рядом в .jeb дым или квад. Откат: `ASDA_FX_NO_RING_OFFSET_FIX=1`.
            #   ★ Хиральный рефлект у 0xE отменён КАНОНОМ (det(N520)=+1) — снимаем его с печки и,
            #   когда нормаль пришла от РЕАЛЬНОГО сокета (не дефолтная (0,0,1)), гасим и в рантайме
            #   флагом `ring_no_chiral`. При дефолтной нормали рефлект тождественен негации z, и
            #   принятые rainbow/inferoshouse остаются БИТ-В-БИТ прежними (замер: Δ = 0.0 юн).
            _is_ring = (em["type"] == 0xE and not em.get("mesh"))
            _ring_fix = _is_ring and os.environ.get("ASDA_FX_NO_RING_OFFSET_FIX", "") != "1"
            fx=build_emitter(em, _bm, reflect_n=(None if _ring_fix else reflect_n), authored_rot=_arot,
                             pure_ring_attached=(pure_ring or _ring_fix),
                             ring_track_pos=_ring_fix,
                             kf_seed_nodes=(os.environ.get("ASDA_MAP_KF_SEED", "") == "1"),
                             etalon_end=(os.environ.get("ASDA_MAP_ETALON_END", "") == "1"),
                             holder_restart=True)
            #   Откат ОДНОЙ ЭТОЙ половины (оставить рефлект, чинить только контракт pos-канала):
            #   `ASDA_FX_RING_KEEP_CHIRAL=1` — им же снимается кадр-двойник для сверки владельцем.
            if (fx is not None and _ring_fix and reflect_n is not None
                    and os.environ.get("ASDA_FX_RING_KEEP_CHIRAL", "") != "1"
                    and max(abs(reflect_n[0]), abs(reflect_n[1]), abs(reflect_n[2] - 1.0)) > 1e-3):
                fx.set_editor_property("ring_no_chiral", True)   # нормаль от реального сокета — рефлект гасим
            # ★★★ [ПОРТ] ПИВОТ КВАДА (2026-09-10, «эмиттер вращается вокруг оси ЭФФЕКТА, а не своей»).
            #   Эталон `effectQuad_updateAndDrawBillboard@0x120AEB0`: pos-канала в ГЕОМЕТРИИ квада нет
            #   вовсе — он живёт только в ТРАНСЛЯЦИИ (M._41-43 = basePos + posChan·R·N48 + позиция узла),
            #   поэтому канальные вращения крутят квад ВОКРУГ ЕГО СОБСТВЕННОГО ЦЕНТРА. Карт-ветка порта
            #   прибавляла pos К УГЛАМ до вращений ⇒ вращение вокруг НАЧАЛА эффекта = орбита
            #   (`eff_mdel_obsdoor02-a_00` эм. «S-1»: живой центроид ходит по окружности r = 300).
            #   Гейт — ПО ТИПУ ЭМИТТЕРА (урок волны 0xE: семантика не может зависеть от соседей в .jeb).
            #   Откат: `ASDA_FX_NO_QUAD_PIVOT_FIX=1` + перепечка карт.
            if (fx is not None and em["type"] in (1, 2, 0xB, 0xC, 0x13, 0x14, 0x18, 0x19)
                    and not em.get("mesh")
                    and os.environ.get("ASDA_FX_NO_QUAD_PIVOT_FIX", "") != "1"):
                fx.set_editor_property("strict_quad_pivot", True)
            if fx is None:
                if em["mesh"]: meshskip+=1
                else: legacy+=1
                continue
            ems.append(fx)
        if not ems: continue
        if patch_only:
            _lbl_counts[pl["name"]] = _lbl_counts.get(pl["name"], 0) + 1
            _lbl = "MapEffect_%s_%02d" % (pl["name"], _lbl_counts[pl["name"]])
            _act = existing.get(_lbl)
            if _act is None:
                unreal.log_warning("[эффекты] ПАТЧ: актор '%s' не найден — пропущен" % _lbl)
                continue
            # ★★★ Ф16 (К-1): перепечка эмиттеров УЖЕ РАЗМЕЩЁННОГО актора — самый опасный случай:
            #   у него в `EmitterGlowSaved` лежат настройки владельца, а список эмиттеров сейчас
            #   заменяется целиком из .jeb. Явный гейт снимает зависимость от формы события правки
            #   (обоснование — asda_fx_common.emitters_writer_gate).
            with _FXC.emitters_writer_gate():
                _act.set_editor_property("emitters", ems)
            placed += 1
            continue
        # ★ type-2 attach к кости анимированного объекта (CONFIRMED дизасм+live.dmp @0x3183C880): selfMat (авторская
        #   matrix) = УЖЕ МИРОВАЯ поза эффекта (ориентация-дуга + scale~23). pmat в оригинале служит ТОЛЬКО для
        #   bone-local резолва (localMat=selfMat·pmatᵀ), НЕ для ориентации актора. Ставим актор в авторскую мировую
        #   позу (pos+matrix) — как обычный не-attached эффект — и ниже attach'им к кости с KEEP_WORLD (следование за
        #   анимацией дирижабля без наследования его yaw/scale). Прежняя ветка use_pmat(type2)+SNAP роняла радугу плашмя.
        #   type-1 (A-инстанс, статичный грид) — прежняя pmat-логика (агент type-1 не пересматривал).
        use_pmat = pl.get("type") == 1 and pl.get("attached")
        xf = place_transform([pl["pmat"][12],pl["pmat"][13],pl["pmat"][14]], pl["pmat"]) if use_pmat \
             else place_transform(pl["pos"], pl["matrix"])
        act=eas.spawn_actor_from_class(unreal.AsdaEffectActor, xf.translation)
        act.set_actor_transform(xf, False, False)
        _lbl_counts[pl["name"]]=_lbl_counts.get(pl["name"],0)+1
        act.set_actor_label("MapEffect_%s_%02d"%(pl["name"],_lbl_counts[pl["name"]]))   # уникальный суффикс инстанции
        # ★ Плотность дыма (визуальный арбитраж юзера 2026-07-27): честный альфа-марш 1→0 (после оживления
        #   VertexColor) делает шлейф слишком прозрачным против оригинала → ×2 c клампом на пикселе.
        #   Editor-friendly: значение на акторе, правится в редакторе; TODO — дореверс дефолт-узлов 0xD.
        if "smoke" in pl["name"].lower():
            act.set_editor_property("particle_alpha_scale", 2.0)
        # ★ Хиральная нормаль на актор (2026-07-28): зеркальный носитель type-2 требует отражения и
        #   ГЕОМЕТРИИ колец/квадов (рантайм), не только позиций (place-данные) — иначе конус/офсеты
        #   рассинхронизированы (tower «юбка ниже конуса»).
        if reflect_n is not None:
            act.set_editor_property("chiral_normal", unreal.Vector(reflect_n[0], reflect_n[1], reflect_n[2]))
        act.set_editor_property("effect_name",pl["name"])
        act.set_editor_property("time_sync_gate", float(pl.get("timeSync",0.0) or 0.0))   # №13 one-shot гейт
        hdr=jb.get("header") or {}
        dur=max(float(hdr.get("duration") or 0.0), 0.05)
        ry=hdr.get("rotY") or [0,0,0]
        import random as _r
        a0=ry[0]+(_r.random()-_r.random())*ry[2]
        act.set_editor_property("holder_yaw_start", a0)
        act.set_editor_property("holder_yaw_speed", (ry[1]-a0)/dur if abs(ry[1]-a0)>1e-4 else 0.0)   # °/с
        hp0=hdr.get("posStart") or [0,0,0]; hp1=hdr.get("posEnd") or [0,0,0]
        pv=[(hp1[k]-hp0[k])/dur for k in range(3)]
        if any(abs(x)>1e-4 for x in pv):
            act.set_editor_property("holder_pos_vel", to_ue_v(pv))                                    # дрейф в мире
        if pl.get("type")==2 and pl.get("attached"):
            parent,dist=find_skel(pl["pmat"])
            bname=pl.get("boneName") or ""
            if parent and bname:
                comp=parent.skeletal_mesh_component
                if comp.does_socket_exist(unreal.Name(bname)):
                    # ★ ПАРЕНТИНГ к кости (эффект = ребёнок в иерархии: едет за носителем И двигается руками,
                    #   без пер-тик снапа). Root: rotation+scale АБСОЛЮТНЫЕ (зеркало скелета scale.X<0 не корёжит;
                    #   следует только ПОЗИЦИЯ). ОРИЕНТАЦИЯ/МАСШТАБ = АВТОРСКИЕ (подтверждены: радуги/фонари);
                    #   позиция = кость (S5 SetPos). follow_actor/bone — маркер attached для рантайма частиц
                    #   (bAttachedMap; пер-тик follow по дефолту ВЫКЛ).
                    _sock=comp.get_socket_transform(unreal.Name(bname), unreal.RelativeTransformSpace.RTS_WORLD)
                    act.set_actor_location(_sock.translation, False, False)
                    act.set_editor_property("follow_actor", parent)
                    act.set_editor_property("follow_bone", unreal.Name(bname))
                    act.set_editor_property("follow_bind_rot", _sock.rotation.rotator())
                    act.set_editor_property("authored_rot", act.get_actor_transform().rotation.rotator())
                    if pure_ring:
                        # ★ КАНОН live-attach 0xE (2026-07-31): N520 = ЖИВАЯ кость×группа — БЕЗ парентинга
                        #   (закон attach-архитектуры). Рантайм Tick: bFollowPosition (позиция = сокет) +
                        #   bFollowRotation ((SockNow·Bind⁻¹)·Authored — живая дельта кости поверх группы).
                        #   t=0: SockNow==Bind, socketLoc==actorLoc → инвариант запечённой позы.
                        act.set_editor_property("follow_position", True)
                        act.set_editor_property("follow_rotation", True)
                    else:
                        _root=act.root_component
                        # loc+rot=relative (следует за translation И rotation анимированной кости — качание фонарей),
                        # scale=absolute (зеркало родителя scale.X<0 корёжит ТОЛЬКО масштаб, поворот — чистый кватернион)
                        _root.set_absolute(False, False, True)
                        act.attach_to_component(comp, unreal.Name(bname),
                            unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
                            unreal.AttachmentRule.KEEP_WORLD, False)
                    attached+=1
                else:
                    unreal.log_warning("attach %s: кость '%s' не найдена в %s"%(pl["name"],bname,parent.get_actor_label()))
        # emitters ПОСЛЕ attach: set_editor_property → RerunConstructionScripts → прогрев печёт вершины
        # уже в ФИНАЛЬНОМ (bone) трансформе (аудит КРИТ-2: раньше пеклось до attach → волны в башне)
        # ★★★ Ф16 (К-1): под явным гейтом «пишет скрипт» — актор здесь свежесозданный, но правило
        #   одно на все записи эмиттеров: признак не должен зависеть от того, целиком ли пишется
        #   массив (обоснование — asda_fx_common.emitters_writer_gate).
        with _FXC.emitters_writer_gate():
            act.set_editor_property("emitters",ems)
        placed+=1
    les.save_current_level()
    unreal.log("=== ЭФФЕКТЫ %s: акторов=%d (attach к костям=%d) | пропущено: legacy=%d mesh=%d без.jeb=%d ==="%(MAP_NAME,placed,attached,legacy,meshskip,nojeb))


# ★★★ НЕ ВОЗВРАЩАТЬ ГОЛЫЙ `main()` СЮДА (регрессия 2026-07-30, дважды подряд).
#   Модуль ИМПОРТИРУЮТ ради build_emitter/get_fx_material: asda_build_char_fx, asda_build_mount_fx,
#   asda_build_loot_fx, asda_build_click_marker, asda_build_combat_impacts. При голом `main()` КАЖДЫЙ такой
#   импорт МОЛЧА сносил все MapEffect_* карты `ASDA_MAP` (дефолт alpen), заново раскладывал их ТЕКУЩИМ —
#   недоделанным, правившимся под крылья/шлейф — кодом печки, удалял и пересобирал `Map/<карта>/EffectMat`
#   и делал `save_current_level()`. Так откалиброванные эффекты карт (радуги дирижаблей, фонари alpen)
#   переписывались по 6 раз за день без единого слова в логе того скрипта, который его импортировал.
#   Перед правкой ОБЩЕГО кода эффектов гонять `Tools/asda_fx_orient_check.py` (см. его шапку).
if __name__ == "__main__":
    main()
elif unreal is not None:
    unreal.log_warning("[fx] asda_place_effects импортирован как модуль — размещение НЕ выполняется "
                       "(для размещения запускать скрипт напрямую: run_headless.bat asda_place_effects.py)")
