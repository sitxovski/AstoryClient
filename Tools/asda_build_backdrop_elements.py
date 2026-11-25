# Copyright (c) 2026 @sitxovski (Discord)
# UE-билдер декомпозиции login-фона: читает RawAssets/backdrop/* → 491 уникальный StaticMesh +
# 697 редактируемых акторов под якорем «LoginBackdrop» (папка Outliner). Правило [[ue-editor-friendly]].
#
# ОСИ: эмпирически сверяем с оригиналом — сэмплим bbox старого скелетного backdrop (correct) и
#   выводим game→UE так, чтобы декомпозиция легла 1:1. Старый скелетный backdrop оставляем СКРЫТЫМ
#   фолбэком (LoginBackdrop_OLD) — если разложение визуально не сойдётся, вернём одним флагом.
import os, json, math
import unreal
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_material_policy as POL   # ★ выбор мастера (masked / прозрачный) — единая политика

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "RawAssets", "backdrop")
LEVEL = "/Game/Map/Login"
# Меши, которым в текущем прогоне уже назначен материал слота (MIC первого элемента); сбрасывается в main()
_SLOT_DONE = set()
DEST = "/Game/AsdaAssets/Models/Backdrop"
TEX_SRC = os.path.join(HERE, "..", "RawAssets", "Textures", "Data", "Character", "Texture")
TEX_DEST = "/Game/AsdaAssets/Textures/Backdrop"
MAT_MASTER = "/Game/Materials/M_AsdaObjectLit"   # ★ Lit-мастер (курс «современно» 2026-07-23): билдер
#   на каждом прогоне ПЕРЕ-привязывает MIC к MAT_MASTER (set_material_instance_parent) — с unlit-мастером
#   тут пересборка молча откатывала задник с Lit (грабля: «реагирует на свет только шкатулка»).
# ★★★ МАСШТАБ СЦЕНЫ (12.09.2026, «масштабы огромные»). БЫЛО 100.0 — «×100-сцена»: старый задник и
#   казна импортировались через glTF, а glTF-импорт UE трактует единицы файла как МЕТРЫ и умножает
#   на 100 (метры→см). Под этот артефакт подгонялась вся сцена, и логин оказался единственной
#   локацией порта в ×100: у всех 24 игровых карт `asda_build_map_level.py SCALE = 1.0` (игровая
#   единица = 1 см UE). Замер 12.09: медианный габарит элемента логина 18 736 юн против 179.7 юн
#   у alpen — ровно ×104. Поэтому ВСЕ длины-умолчания UE (радиус затухания точечного/конусного
#   света 1000 юн, дальность каскадов теней 20 000 юн, контактные тени, дальности отсечения)
#   на логине не действовали. СТАЛО 1.0 — как у карт.
#   Откат: ASDA_LOGIN_SCALE=100 + пересборка (этот скрипт, asda_export_backdrop_anim.py) +
#   GLoginScale=100.f в AsdaLoginGameMode.cpp + масштаб казны в уровне.
SCALE = float(os.environ.get("ASDA_LOGIN_SCALE", "1.0"))

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# ★ ТОЧНАЯ конверсия (эмпирически из сверки glb↔_OLD): экспортёр негатит Z (glb=(x,y,-z)), UE-glTF-импорт
#   переставляет (gx,gy,gz)→(gx,gz,gy)×100. Итог jbm→UE-локаль _OLD: (x, -z, y)×100. Совпадает с _OLD 1:1
#   (origin (0,-99255,82085), extent (394003,231288,176534)). Прошлый (-z,x,y) давал ДРУГУЮ перестановку = перекос.
def to_ue(x, y, z): return unreal.Vector(x * SCALE, -z * SCALE, y * SCALE)

# ★ Жёсткий поворот всего фона на BACKDROP_ANGLE° вокруг оси BACKDROP_AXIS (запрос: «по горизонту» =
#   горизонтальная ось UE-X = roll; камера смотрит вдоль +UE-Y, горизонт = экранное лево-право = UE-X).
#   Запекаем в относительные трансформы элементов (не в якорь — его перебивает секвенция перелёта).
#   Неверная ось → сменить "X"/"Y"/"Z"; не та сторона → знак BACKDROP_ANGLE.
BACKDROP_AXIS = "X"     # X=roll(по горизонту, экран лево-право) | Y=pitch(в глубину) | Z=yaw(вертикаль)
BACKDROP_ANGLE = 0.0    # ★ БЕЗ поворота (откат к исходной декомпозиции, которую юзер одобрил)
def rot_axis(v):       # поворот ВЕКТОРА вокруг выбранной оси
    if BACKDROP_AXIS == "X": return unreal.Vector(v.x, v.y * _c - v.z * _s, v.y * _s + v.z * _c)
    if BACKDROP_AXIS == "Y": return unreal.Vector(v.x * _c + v.z * _s, v.y, -v.x * _s + v.z * _c)
    return unreal.Vector(v.x * _c - v.y * _s, v.x * _s + v.y * _c, v.z)
def rot_rotator():     # соответствующий Rotator (pitch=UE-Y, yaw=UE-Z, roll=UE-X)
    if BACKDROP_AXIS == "X": return unreal.Rotator(0.0, 0.0, BACKDROP_ANGLE)
    if BACKDROP_AXIS == "Y": return unreal.Rotator(BACKDROP_ANGLE, 0.0, 0.0)
    return unreal.Rotator(0.0, BACKDROP_ANGLE, 0.0)

def unpack(mid):
    m = MESHES[mid]
    return m["pos"], m["uv"], m["idx"]     # plain lists

_matcache = {}
def get_material(texbase, blend=False):
    """MIC по текстуре элемента. ★ КЛЮЧ КЭША/ИМЯ MIC включает класс отрисовки.

    Экспортёр помечает элемент полем `blend` (рендер-флаг `mr+0x004 & 0x1|0x2` И техника tNoSkin,
    `Reports/jbm_blend_flag_study.md` §5): у задника это 667 подсеток из 697. Одна и та же
    текстура встречается и у прозрачных, и у непрозрачных элементов, поэтому MIC должно быть ДВА —
    иначе последний прогон затирал бы родителя предыдущему (та же грабля, что «билдер
    пере-привязывает MIC к MAT_MASTER на каждом прогоне»).
    """
    key = texbase + (POL.BLEND_SUFFIX if blend else "")
    if key in _matcache: return _matcache[key]
    tp = "%s/%s" % (TEX_DEST, texbase)
    if not EAL.does_asset_exist(tp):
        src = os.path.join(TEX_SRC, texbase + ".png")
        if os.path.isfile(src):
            import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (AutomatedAssetImportData)
            for a in _MIU.import_batch([src], TEX_DEST):
                try: EAL.save_loaded_asset(a)
                except Exception: pass
    nm = "MI_%s" % key
    mp = "%s/%s" % (DEST, nm)
    mic = EAL.load_asset(mp) if EAL.does_asset_exist(mp) else \
          ATOOLS.create_asset(nm, DEST, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    # ★★★ Мастер спрашиваем У ПОЛИТИКИ (`POL.blend_master()`), а не подставляем MASTER_BLEND: при
    #   выключенной раскатке (POL.BLEND_ROLLOUT_ENABLED=False, решение владельца 2026-08-18 —
    #   «прозрачность пока только в gacha») она вернёт masked, и пересборка задника НЕ вернёт
    #   прозрачность обратно. Поле `blend` элемента и ОТДЕЛЬНЫЙ MIC `MI_<tex>_blend` сохраняются:
    #   чтобы вернуть раскатку, достаточно переключить один флаг в asda_material_policy.py.
    mpath = POL.blend_master() if blend else MAT_MASTER
    master = EAL.load_asset(mpath)
    if not master:
        unreal.log_warning("НЕТ мастера %s — элемент уйдёт на %s (собрать: asda_build_blend_material.py)"
                           % (mpath, MAT_MASTER))
        master = EAL.load_asset(MAT_MASTER)
    if master: MEL.set_material_instance_parent(mic, master)
    if EAL.does_asset_exist(tp):
        MEL.set_material_instance_texture_parameter_value(mic, "Tex", EAL.load_asset(tp))
        MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
    EAL.save_loaded_asset(mic)
    _matcache[key] = mic
    return mic

# ---- сборка уникальных StaticMesh ----
_smcache = {}
def build_sm(mid):
    if mid in _smcache: return _smcache[mid]
    path = "%s/SM_%s" % (DEST, mid)
    pos, uv, idx = unpack(mid)
    # ★ Геометрия зависит от to_ue → пересобираем ДЕСКРИПТОР на СУЩЕСТВУЮЩЕМ ассете (не delete+create:
    #   удаление не успевает в реестре → create_asset=None). build_from_static_mesh_descriptions сбрасывает bounds.
    sm = EAL.load_asset(path) if EAL.does_asset_exist(path) else ATOOLS.create_asset("SM_%s" % mid, DEST, unreal.StaticMesh, None)
    if not sm: return None
    desc = sm.create_static_mesh_description()
    g = desc.create_polygon_group(); desc.set_polygon_group_material_slot_name(g, "Mat0")
    used = sorted(set(int(x) for x in idx))
    remap = {old: k for k, old in enumerate(used)}
    vids = []
    for old in used:
        vid = desc.create_vertex()
        desc.set_vertex_position(vid, to_ue(float(pos[old*3]), float(pos[old*3+1]), float(pos[old*3+2])))
        vids.append(vid)
    ntri = 0
    for t in range(0, len(idx) - 2, 3):
        a, b, c = int(idx[t]), int(idx[t+1]), int(idx[t+2])
        if a == b or b == c or a == c: continue
        tri = []
        for cc in (a, b, c):
            vi = desc.create_vertex_instance(vids[remap[cc]])
            desc.set_vertex_instance_uv(vi, unreal.Vector2D(float(uv[cc*2]), float(uv[cc*2+1])), 0)
            tri.append(vi)
        desc.create_triangle(g, tri); ntri += 1
    if ntri == 0: return None
    # ★★★ fast_build=False ОБЯЗАТЕЛЬНО (было `[desc]` = fast_build=True). fast_build взводит и
    # сериализует в ассет UStaticMesh::bDoFastBuild, после чего FStaticMeshRenderData::Cache всегда
    # идёт «быстрой» веткой (StaticMesh.cpp:4321), которая НЕ заполняет FStaticMeshRenderData::Bounds
    # (поле без инициализации, StaticMeshResources.h:795) → в кук уходит мусор памяти; у части мешей
    # он оказывается NaN и кук падает («found NaN in Bounds», ensure FQuat(FMatrix)).
    sm.build_from_static_mesh_descriptions([desc], False, False)
    # ★ НОРМАЛИ: дескриптор их не несёт → NdotL мёртв → Lit-свет/тени на элементах НЕ работали
    #   («не подвержен правкам света»). recompute_normals в build settings — UE считает сглаженные сам.
    smes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    bs = smes.get_lod_build_settings(sm, 0)
    bs.set_editor_property("recompute_normals", True)
    bs.set_editor_property("recompute_tangents", True)
    smes.set_lod_build_settings(sm, 0, bs)   # применение перебилживает LOD0 с нормалями
    # ★★★ СЛОТ МАТЕРИАЛА (14.09.2026, тени задника). build_from_static_mesh_descriptions НЕ заводит StaticMaterials
    #   по имени группы полигонов → у меша 0 слотов → UStaticMeshComponent::GetNumMaterials()=0 → пустая
    #   MaterialRelevance прокси → элемент НЕ отбрасывает тени (ShadowSetup.cpp: if (bOpaque && bShadowRelevance)),
    #   хотя в основном проходе виден (секция берёт override). Разбор: Reports/backdrop_shadows.
    #   Слот «Mat0» = имя группы полигонов выше; материал слота ставится при размещении (первый MIC элемента).
    if not list(sm.get_editor_property("static_materials")):
        sm.set_editor_property("static_materials", [unreal.StaticMaterial(material_interface=None, material_slot_name="Mat0")])
    EAL.save_loaded_asset(sm)          # ★ БЫЛ ПРОПУЩЕН → ассеты не персистились (mesh=НЕТ после релоада)
    _smcache[mid] = sm
    return sm


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_build_backdrop_elements.py'
    global _SLOT_DONE
    _SLOT_DONE = set()   # новый прогон — материал слота назначается заново
    global MESHES, _c, _s, a, c, mic, old, sm
    les.load_level(LEVEL)
    _c = math.cos(math.radians(BACKDROP_ANGLE)); _s = math.sin(math.radians(BACKDROP_ANGLE))
    # ---- загрузка данных экспортёра ----
    with open(os.path.join(DATA, "backdrop_elements.json"), "r", encoding="utf-8") as f:
        j = json.load(f)
    elements = j["elements"]
    MESHES = j["meshes"]
    unreal.log("элементов=%d уник.мешей=%d" % (len(elements), len(j["meshIds"])))
    # ---- материалы по текстуре ----
    for d in (TEX_DEST,):
        if not EAL.does_directory_exist(d): EAL.make_directory(d)
    if not EAL.does_directory_exist(DEST): EAL.make_directory(DEST)
    # ---- якорь = трансформ старого скелетного backdrop ----
    old = None
    for a in eas.get_all_level_actors():
        if a.get_actor_label().startswith("LoginBackdrop") and a.get_class().get_name() == "SkeletalMeshActor":
            old = a; break
    if not old:
        unreal.log_warning("Старый LoginBackdrop не найден — якорь в origin")
    base_xf = old.get_actor_transform() if old else unreal.Transform()
    # снести прошлые элементы
    for a in list(eas.get_all_level_actors()):
        if a.get_actor_label().startswith("BackdropElem") or a.get_actor_label() == "LoginBackdrop_Anchor":
            try: eas.destroy_actor(a)
            except Exception: pass
    anchor = eas.spawn_actor_from_class(unreal.StaticMeshActor, base_xf.translation)
    anchor.set_actor_transform(base_xf, False, False)
    anchor.set_actor_label("LoginBackdrop_Anchor")
    anchor.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(None)
    try: anchor.set_folder_path("LoginBackdrop")
    except Exception: pass
    # ---- разместить 697 акторов ----
    n = 0
    for e in elements:
        sm = build_sm(e["meshId"])
        if not sm: continue
        act = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,0))
        smc = act.get_component_by_class(unreal.StaticMeshComponent)
        smc.set_static_mesh(sm)
        # ★ Порядок сортировки прозрачных: у Masked-мастера (M_AsdaObjectBlendLit — Masked +
        #   DitherOpacityMask) элементы рисуются в НЕПРОЗРАЧНОМ проходе с записью глубины, поэтому
        #   667 задниковых карточек НЕ требуют translucency-сортировки и не «просвечивают сквозь себя».
        mic = get_material(e["tex"], bool(e.get("blend")))
        if mic: smc.set_material(0, mic)
        # ★ Материал слота меша (для превью ассета; внешний вид элемента задаёт override выше) — MIC ПЕРВОГО
        #   элемента, использующего меш, в КАЖДОМ прогоне (не только при пустом слоте: иначе после смены MIC
        #   в слоте остался бы старый, а DF/карточки Lumen смотрят на класс материала слота). Слот обязателен
        #   для теней — см. build_sm. Ревизия 14.09.2026.
        _sl = list(sm.get_editor_property("static_materials"))
        _key = sm.get_path_name()
        if mic and _sl and _key not in _SLOT_DONE:
            _SLOT_DONE.add(_key)
            if _sl[0].material_interface != mic:
                _sl[0].material_interface = mic
                sm.set_editor_property("static_materials", _sl)
                EAL.save_loaded_asset(sm)
        act.attach_to_actor(anchor, "", unreal.AttachmentRule.KEEP_RELATIVE, unreal.AttachmentRule.KEEP_RELATIVE, unreal.AttachmentRule.KEEP_RELATIVE, False)
        c = e["center"]
        # позиция + ориентация повёрнуты на BACKDROP_ANGLE вокруг BACKDROP_AXIS (жёсткий поворот всего фона)
        rel_loc = rot_axis(to_ue(c[0], c[1], c[2]))
        act.set_actor_relative_transform(unreal.Transform(rel_loc, rot_rotator(), unreal.Vector(1,1,1)), False, False)
        act.set_actor_label("BackdropElem_%s" % e["name"])
        # ★ АНИМАЦИЯ ЗАДНИКА (asda_build_backdrop_anim.py + AAsdaBackdropAnimator): дирижёр находит
        #   элементы ПО ТЕГАМ (метка актора в упакованной игре недоступна) и двигает их трансформ,
        #   что требует Movable. Ставим здесь же, иначе пересборка декомпозиции молча гасила бы
        #   качание растительности до повторного прогона asda_build_backdrop_anim.py.
        act.set_editor_property("tags", [unreal.Name("AsdaBackdropElem"), unreal.Name(e["name"])])
        smc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        try: act.set_folder_path("LoginBackdrop")
        except Exception: pass
        n += 1
        if n % 100 == 0: unreal.log("  размещено %d/%d" % (n, len(elements)))
    # старый скелетный backdrop → СКРЫТ (фолбэк), переименован
    if old:
        old.set_actor_label("LoginBackdrop_OLD")
        old.set_actor_hidden_in_game(True)
        try: old.set_folder_path("LoginBackdrop")
        except Exception: pass
    # ★ Восстановить связь Flight → Anchor (anchor — ручной корень). Anchor пересоздаётся каждый билд, поэтому
    #   реаттачим здесь, иначе откатная секвенция «сцена летит» перестанет двигать фон (Flight без ребёнка).
    # ★★★ 13.09.2026: по умолчанию летит КАМЕРА, фон стоит в КОНЕЧНОЙ позе перелёта = тождество. base_xf здесь —
    #   тождество (старого LoginBackdrop нет, «якорь в origin»), то есть пересборка схему не ломает. Если base_xf
    #   когда-нибудь станет не тождеством — после пересборки прогнать asda_build_login_flight_sequence.py.
    flight = next((a for a in eas.get_all_level_actors() if a.get_actor_label()=="LoginBackdrop_Flight"), None)
    if flight:
        flight.set_actor_transform(base_xf, False, False)
        anchor.attach_to_actor(flight, "", unreal.AttachmentRule.KEEP_WORLD,
                               unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
        unreal.log("Anchor реаттачен под LoginBackdrop_Flight (фон летит через Flight)")
    les.save_current_level()
    unreal.log("=== BACKDROP декомпозирован: %d элементов под LoginBackdrop_Anchor, %d уник.мешей, старый скрыт ===" % (n, len(_smcache)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
