# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_charselect_effect.py — эффект ШКАТУЛКИ char-select `eft_creation01-a_00.jeb` (свечение ФОРДЖИНГА
# создания персонажа) как AAsdaEffectActor в /Game/Map/Login.
#
# ГРАУНД-ТРУС (SPEC_C_mesh_ring.md §1-2 + сырые байты .jeb/.jem, дамп 2026-07-27):
#   3 эмиттера типа 0x12 (.jem меш-эмиттеры, CEmitter_Mesh_Update keyframe + CEffectMesh_Render):
#     [0] CRTW_M00  eft_creation01-a_00.jem  126в  голубые ЛУЧИ вокруг силуэта шкатулки (чёрный центр = невидим при additive)
#     [1] CRTW_M01  eft_creation01-a_01.jem  126в  второй слой лучей (ключ красит в серый 0.5 — тусклее)
#     [2] CRTW_M02  eft_creation01-a_02.jem   36в  подсветка цветка-медальона на фронте шкатулки
#   Каналы (fixed-сиды + 1 ключ 0xA0=40float, раскладка как sub-node 0xB/0xC: [0]=time [13]=alpha [19..21]=RGB):
#     сид color=(0,0,0) ЧЁРНЫЙ (невидим), alpha=1; ключ t=0.5с → SNAP color=(1,1,1)/(0.5,..)/(1,..);
#     life=3.0с, endMode=0 (стоп). Дельты нулевые ⇒ ступенька: 0..0.5с темно → вспышка → 3.0с конец.
#     Порт: ступенька узлами NodeAt (t=0 сид → t=key−0.01 сид → t=key snap → t=life snap), period=life, end=0.
#   Мировая матрица = ШКАТУЛКА (не слот); рендер только в форджинге (глоу-фаза TickCasketSequence).
#
# ★ ОСИ ВЕРТОВ: .jem хранит Max Z-up; asda_jem_parse отдаёт КАРТ-конвенцию UE=(My,Mx,Mz) (для карт ✓).
#   ЛОГИН-сцена запечена конверсией game→UE (x,−z,y) ⇒ для неё .jem-верты = СЫРЫЕ Max-оси (Mx,My,Mz):
#   Max→game (x,z,−y), game→UE_login (x,−z,y) ⇒ UE_login=(Mx,My,Mz). Берём v_map=(My,Mx,Mz) → (v[1],v[0],v[2]).
#   Иначе лучи/медальон повёрнуты на 90° вокруг вертикали («криво», скрин юзера 2026-07-27).
#
# Масштаб актора НЕ задаём: GameMode::StartPlay гарантирует МИРОВОЙ масштаб = GLoginScale
#   (★ 12.09.2026: сцена логина переведена из ×100 в ×1, GLoginScale = 1; геометрия .jem задана
#    в ИГРОВЫХ единицах, поэтому мировой масштаб эффекта равен масштабу сцены).
# Актор скрыт стартово — глоу-фаза показывает/прячет. Метка "CharSelectEffect_eft_creation01".
import os, importlib.util as _iu, unreal
import os as _fxg_os, sys as _fxg_sys
_fxg_sys.path.insert(0, _fxg_os.path.dirname(_fxg_os.path.abspath(__file__)))
import asda_fx_common as _FXC_GATE   # ★ Ф16: гейт записи эмиттеров (импорт без побочных эффектов)

TOOLS = os.path.dirname(os.path.abspath(__file__))
def _load(mod, fn):
    sp=_iu.spec_from_file_location(mod, os.path.join(TOOLS, fn)); m=_iu.module_from_spec(sp); sp.loader.exec_module(m); return m

EFFECT   = "eft_creation01-a_00"
LEVEL    = "/Game/Map/Login"
TEX_SRC  = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/effects/charselect"      # PNG-текстуры (извлечены asda_extract_effect_textures)
TEX_DEST = "/Game/AsdaAssets/Textures/Effects"           # каталог текстур эффектов (правило catalog-all-resources)
MAT_DEST = "/Game/AsdaAssets/Textures/Effects/Mat"
MAT_ADD  = "/Game/Effects/M_AsdaEffectAdd"
MAT_ALPHA= "/Game/Effects/M_AsdaEffectAlpha"
CASKET_MESH_NAME = "cre_creation01"                       # актор шкатулки — по имени SkeletalMesh-ассета

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary

def ensure_texture(stem):
    """Текстура TEX_DEST/stem (импорт из TEX_SRC/stem.png при отсутствии) — общий
    asda_model_import_ue.ensure_png_texture (дедуп №2 В1 Т17: была точная копия с asda_place_charselect_glyph)."""
    import asda_model_import_ue as _MIU
    return _MIU.ensure_png_texture(TEX_DEST, TEX_SRC, stem)

def get_material(stem, additive):
    ensure_texture(stem)
    nm="MI_%s_%s"%(stem,"add" if additive else "alpha"); mp="%s/%s"%(MAT_DEST,nm)
    master=EAL.load_asset(MAT_ADD if additive else MAT_ALPHA)
    if EAL.does_asset_exist(mp): mic=EAL.load_asset(mp)
    else: mic=ATOOLS.create_asset(nm,MAT_DEST,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mic,master)
    tp="%s/%s"%(TEX_DEST,stem)
    if EAL.does_asset_exist(tp):
        MEL.set_material_instance_texture_parameter_value(mic,"SpriteTexture",EAL.load_asset(tp))
    EAL.save_loaded_asset(mic)
    return mic

def _mesh_node(t, alpha, rgb):
    # Узел канальной ветки mesh12 (AsdaEffectActor: ConeAxis=scale3, RingRot=rot, RingPosOfs=posLocal,
    # RingBasePos=offsetWorld). У creation01 rot/scale/pos статичны → только время/цвет/альфа.
    n=unreal.AsdaFxNode()
    n.set_editor_property("time",float(t))
    n.set_editor_property("alpha",float(max(0.0,min(1.0,alpha))))
    n.set_editor_property("color",unreal.LinearColor(rgb[0],rgb[1],rgb[2],1.0))
    n.set_editor_property("cone_axis",unreal.Vector(1.0,1.0,1.0))      # scale3
    n.set_editor_property("ring_rot",unreal.Vector(0.0,0.0,0.0))
    n.set_editor_property("ring_pos_ofs",unreal.Vector(0.0,0.0,0.0))
    n.set_editor_property("ring_base_pos",unreal.Vector(0.0,0.0,0.0))
    return n

def build_mesh_emitter(em, additive, raw):
    meshes=JM.load_jem(em.get("jem") or em["name"]) or []
    if not meshes:
        unreal.log_warning("Нет .jem для %s"%em["name"]); return None
    m=meshes[0]
    fx=unreal.AsdaFxEmitter()
    fx.set_editor_property("name",em["name"]); fx.set_editor_property("jeb_type",em["type"])
    fx.set_editor_property("mesh_emitter",True)
    # ★ карт-конвенция парсера (My,Mx,Mz) → логин-конвенция = сырые Max-оси (Mx,My,Mz): swap X↔Y
    fx.set_editor_property("mesh_verts",[unreal.Vector(v[1],v[0],v[2]) for v in m["verts"]])
    fx.set_editor_property("mesh_normals",[unreal.Vector(v[1],v[0],v[2]) for v in m["normals"]])
    fx.set_editor_property("mesh_uv0",[unreal.Vector2D(u,v) for u,v in m["uvs"]])
    fx.set_editor_property("material",get_material(m["tex"] or em["name"], additive))
    # ── keyframe-ЛЕРП (эталон animComputeTrackDeltas@0x11F6660: дельты сегментов вычисляются при вставке
    #    delta=(cur−prev)/(t_cur−t_prev); лоадер добавляет START-узел (сиды, t=0) и END-узел (END-сиды,
    #    t=life)). Итог для creation01: ЧЁРНЫЙ(0) → лерп-РАСЦВЕТАНИЕ → белый/серый(0.5с) →
    #    лерп-ЗАТУХАНИЕ → ЧЁРНЫЙ(3.0с) (colorEnd fixed=(0,0,0)). НИКАКИХ ступенек и стробов. ──
    p=em["params"]
    life=max(p.get("life",[3.0])[0], 0.1)
    seed_rgb=p.get("colorStart",[0.0,0.0,0.0])
    end_rgb=p.get("colorEnd",[0.0,0.0,0.0])
    a_tri=p.get("alpha",[1.0,1.0,0.0])
    seed_a, end_a = a_tri[0], a_tri[1]
    keys=[]
    for n40 in (raw or []):
        if len(n40)>=22:
            keys.append({"t":n40[0], "rgb":[n40[19],n40[20],n40[21]], "a":n40[13]})
    keys.sort(key=lambda k:k["t"])
    nodes=[_mesh_node(0.0, seed_a, seed_rgb)]                       # START = сиды
    for k in keys:
        nodes.append(_mesh_node(max(0.01,min(k["t"],life-0.01)), k["a"], k["rgb"]))
    nodes.append(_mesh_node(life, end_a, end_rgb))                  # END = END-сиды (затухание в чёрный)
    fx.set_editor_property("nodes",nodes)
    fx.set_editor_property("period",life)                   # life=3.0 → GlowDurationSec фазы = 3.0с
    fx.set_editor_property("end_mode",0)                    # эталон endMode=0 (стоп; фаза скрывает актор)
    fx.set_editor_property("start_delay",float(em.get("startDelay",0.0)))
    return fx

def main():
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_place_charselect_effect.py'
    r=JEB.parse_effect(EFFECT)
    if not r: unreal.log_warning("Нет .jeb %s"%EFFECT); return
    additive=r.get("blendAdditive",True)
    ems=[]
    for em in r["emitters"]:
        if not em["mesh"]:
            unreal.log_warning("эмиттер %s не mesh (тип 0x%X) — пропуск"%(em["name"],em["type"])); continue
        fx=build_mesh_emitter(em, additive, em["params"].get("trackNodes"))
        if fx: ems.append(fx)
    if not ems: unreal.log_warning("Нет эмиттеров"); return

    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not EAL.does_asset_exist(LEVEL): unreal.log_warning("Нет уровня %s"%LEVEL); return
    les.load_level(LEVEL)

    # снести старый
    for a in list(eas.get_all_level_actors()):
        if a.get_actor_label().startswith("CharSelectEffect"):
            try: eas.destroy_actor(a)
            except Exception: pass

    # найти актор шкатулки по имени SkeletalMesh-ассета
    casket=None
    for a in eas.get_all_level_actors():
        if a.get_class().get_name()!="SkeletalMeshActor": continue
        comp=a.skeletal_mesh_component; sm=comp.get_skeletal_mesh_asset() if comp else None
        if sm and CASKET_MESH_NAME in sm.get_name().lower():
            casket=a; break
    if not casket:
        unreal.log_warning("Актор шкатулки cre_creation01 не найден — эффект в origin")

    base_xf = casket.get_actor_transform() if casket else unreal.Transform()
    act=eas.spawn_actor_from_class(unreal.AsdaEffectActor, base_xf.translation)
    act.set_actor_transform(base_xf, False, False)
    act.set_actor_label("CharSelectEffect_%s"%EFFECT)
    act.set_editor_property("effect_name",EFFECT)
    # ★★★ Ф16 (К-1, 20.09.2026): запись массива эмиттеров — под явным гейтом
    #   `asda.Fx.Glow.EmittersWriter` (обоснование — asda_fx_common.emitters_writer_gate):
    #   признак «пишет скрипт» не должен зависеть от формы события правки.
    with _FXC_GATE.emitters_writer_gate():
        act.set_editor_property("emitters",ems)
    act.set_editor_property("cull_distance",1.0e9)          # запас; ★ 12.09: в ×100-сцене дефолт-кулл 10000 прятал эффект ВСЕГДА, в ×1 он уже не мешал бы
    # стартово СКРЫТ — глоу-фаза форджинга (TickCasketSequence) показывает и прячет
    act.set_actor_hidden_in_game(True)

    # attach к корню шкатулки (мир-матрица = шкатулка, как эталон). ★ 13.09.2026: шкатулка при перелёте СТОИТ
    #   (летит камера); в откатной схеме «сцена летит» эффект едет вместе со шкатулкой — привязка верна для обеих.
    if casket:
        comp=casket.skeletal_mesh_component
        root_bone = comp.get_bone_name(0) if comp else unreal.Name("None")
        act.attach_to_component(comp, root_bone,
            unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
            unreal.AttachmentRule.KEEP_WORLD, False)

    les.save_current_level()
    unreal.log("=== CHAR-SELECT EFFECT %s: эмиттеров=%d (каналы-ступени, логин-оси), attach=%s (СКРЫТ) ==="%(
        EFFECT, len(ems), "casket" if casket else "origin"))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global JEB, JM
    JEB = _load("jeb","asda_jeb_parse.py")
    JM  = _load("jem","asda_jem_parse.py")
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
