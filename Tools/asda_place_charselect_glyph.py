# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_charselect_glyph.py — размещает ЭФФЕКТ-ГЛИФ ПУСТОГО слота char-select как AAsdaEffectActor.
#
# ГРАУНД-ТРУС (charselect_render.md §4.1 + реверс):
#   Пустой слот (нет перса) показывает eft_creation0{2,3,4}-a_00.jeb вместо тела (slotType 0/1/2).
#   Загрузка: charCreate_LoadSlotEffects@0xF07700 ветка else → CharCreate_LoadCreationEffect@0xF05310.
#   Рендер: channelSelectRender3D@0xF07BE0 ветка else (slot==null, L531423):
#     objAnimRotateUpdate(&self[780*slotType+3075100], dt)  → holder-Y-spin (угол+=speed*dt, поле +82)
#     setTransformMatrixAndPos(...)  → мир-матрица = узел CH_00N шкатулки (Game_GetChannelEntryName)
#   Каждый глиф = 7 эмиттеров (подтверждено парсером asda_jeb_parse):
#     4× 0xC  CB00-03  single-quad биллборды (кольца/диски глифа, tex eft_creation0N-a/b/c/d.jpg)
#     2× 0xD  P00/P01  track-частицы (creation02=HP hpptn, 03=RE reptn, 04=MP mpptn potion-текстуры)
#     1× 0x12 M00      МЕШ-эмиттер cre_cresowell01-a_00_0.jem (кристалл-столб soul-well, 812 треуг)
#   header.rotY=0 → holder-spin из .jeb = 0. «Крутёж» задаётся вручную GLYPH_YAW_SPEED (см. отчёт агента).
#
# 3 актора (по слоту): label CharSelectGlyph_slot{0,1,2}, СКРЫТЫ стартово (bHiddenInGame).
# C++ (GameMode) показывает актор i, ЕСЛИ CharList[i].IsEmpty() (слот пуст), привязка к кости CH_00{i+1}.
#
# Запуск (headless):  run_headless.bat asda_place_charselect_glyph.py
import os, sys, importlib.util as _iu, unreal
import os as _fxg_os, sys as _fxg_sys
_fxg_sys.path.insert(0, _fxg_os.path.dirname(_fxg_os.path.abspath(__file__)))
import asda_fx_common as _FXC_GATE   # ★ Ф16: гейт записи эмиттеров (импорт без побочных эффектов)
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C

TOOLS = os.path.dirname(os.path.abspath(__file__))
def _load(mod, fn):
    sp=_iu.spec_from_file_location(mod, os.path.join(TOOLS, fn)); m=_iu.module_from_spec(sp); sp.loader.exec_module(m); return m

# по слоту → .jeb-глиф (charselect_render.md §4.1: Source[slot%3])
GLYPHS = {0: "eft_creation02-a_00", 1: "eft_creation03-a_00", 2: "eft_creation04-a_00"}
LEVEL    = "/Game/Map/Login"
TEX_SRC  = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/effects/tex_misc"                # PNG-текстуры эффектов (извлечены extract_effect_textures)
TEX_DEST = "/Game/AsdaAssets/Textures/Effects"                   # каталог текстур эффектов (catalog-all-resources)
MAT_DEST = "/Game/AsdaAssets/Textures/Effects/Mat"
MAT_ADD  = "/Game/Effects/M_AsdaEffectAdd"
MAT_ALPHA= "/Game/Effects/M_AsdaEffectAlpha"
CASKET_MESH_NAME = "cre_creation01"                              # актор шкатулки — по имени SkeletalMesh
# «Крутёж» глифа: header rotY=0 (holder-spin в данных нет). Скорость задаём вручную по вердикту реверса.
# 0 = без ручного спина (глиф статичен + движение частиц). Меняй тут, если реверс подтвердит спин.
GLYPH_YAW_SPEED = float(os.environ.get("ASDA_GLYPH_YAW", "0"))   # град/сек вокруг вертикальной оси
# ★★★ МАСШТАБ СЦЕНЫ ЛОГИНА (12.09.2026): сколько юнитов UE в ОДНОЙ игровой единице.
#   Сцена переведена из ×100 в ×1 (GLoginScale в AsdaLoginGameMode.cpp, ASDA_LOGIN_SCALE
#   у asda_build_backdrop_elements/asda_export_backdrop_anim/asda_rescale_login_scene).
#   Число ОБЯЗАНО совпадать с ними — иначе глиф выйдет в 100 раз больше или меньше сцены.
LOGIN_SCALE = float(os.environ.get("ASDA_LOGIN_SCALE", "1.0"))   # было жёстко 100 (зашито в «85»)
GLYPH_ART_SCALE = 0.85            # арт-поправка −15 % к натуральной величине (в ИГРОВЫХ единицах)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
import re

to_ue_v = _C.to_ue_v   # ★ единая конверсия осей (asda_coords)

def ensure_texture(stem):
    """Текстура TEX_DEST/stem (импорт из TEX_SRC/stem.png при отсутствии) — общий
    asda_model_import_ue.ensure_png_texture (дедуп №2 В1 Т17: была точная копия с asda_place_charselect_effect)."""
    import asda_model_import_ue as _MIU
    return _MIU.ensure_png_texture(TEX_DEST, TEX_SRC, stem)

_matcache={}
def get_material(tex, additive):
    stem=re.sub(r'[^A-Za-z0-9_-]','_',os.path.splitext(tex)[0]).lower() if tex else "none"
    ensure_texture(stem)
    key=(stem,additive)
    if key in _matcache: return _matcache[key]
    nm="MI_%s_%s"%(stem,"add" if additive else "alpha"); mp="%s/%s"%(MAT_DEST,nm)
    master=EAL.load_asset(MAT_ADD if additive else MAT_ALPHA)
    if EAL.does_asset_exist(mp): mic=EAL.load_asset(mp)
    else: mic=ATOOLS.create_asset(nm,MAT_DEST,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mic,master)
    tp="%s/%s"%(TEX_DEST,stem)
    if EAL.does_asset_exist(tp):
        MEL.set_material_instance_texture_parameter_value(mic,"SpriteTexture",EAL.load_asset(tp))
    EAL.save_loaded_asset(mic)
    _matcache[key]=mic
    return mic

def node(t, rate, sx, sy, spd, cone, alpha, rgb, grav, axis):
    n=unreal.AsdaFxNode()
    n.set_editor_property("time",float(t)); n.set_editor_property("rate",float(rate))
    n.set_editor_property("size_x",float(sx)); n.set_editor_property("size_y",float(sy))
    n.set_editor_property("speed",float(spd)); n.set_editor_property("cone_angle",float(cone))
    n.set_editor_property("alpha",float(alpha))
    n.set_editor_property("color",unreal.LinearColor(rgb[0],rgb[1],rgb[2],1.0))
    n.set_editor_property("gravity",float(grav)); n.set_editor_property("cone_axis",to_ue_v(axis))
    return n

def build_emitter(em, blend_add):
    # 1:1 с asda_place_effects.py::build_emitter (0xC single-quad, 0xD частицы, 0x12 mesh)
    p=em["params"]; t=em["type"]
    fx=unreal.AsdaFxEmitter()
    fx.set_editor_property("name",em["name"]); fx.set_editor_property("jeb_type",t)
    fx.set_editor_property("start_delay",float(em.get("startDelay",0.0) or 0.0))
    if not em.get("mesh"):                                       # у mesh-эмиттера tex=default.bmp — берём из .jem ниже
        fx.set_editor_property("material",get_material(em.get("tex",""),blend_add))
    if t in (0xD,3,0x1B):
        period=max(p.get("period",5.0),0.05)
        fx.set_editor_property("period",period); fx.set_editor_property("end_mode",p.get("endMode",3))
        fx.set_editor_property("life_base",max(p["partLife"][0],0.05)); fx.set_editor_property("life_var",p["partLife"][1])
        fx.set_editor_property("size_x_var",p["sizeX"][2]); fx.set_editor_property("size_y_var",p["sizeY"][2])
        fx.set_editor_property("speed_var",p["speed"][2]); fx.set_editor_property("alpha_var",p["alpha"][2])
        cv=p["colorVar"]; fx.set_editor_property("color_var",unreal.Vector(cv[0],cv[1],cv[2]))
        fx.set_editor_property("gravity_var",p["gravity"][2])
        fx.set_editor_property("pos_base",to_ue_v(p["posBase"])); fx.set_editor_property("pos_range",to_ue_v([abs(x) for x in p["posRange"]]))
        fx.set_editor_property("spawn_mode",p.get("spawnMode",0))
        fx.set_editor_property("vel_base",to_ue_v(p["velBase"])); fx.set_editor_property("vel_range",to_ue_v([abs(x) for x in p["velRange"]]))
        fx.set_editor_property("billboard_mode",p.get("billboardMode",0))
        nodes=[]; tn=p.get("trackNodes") or []
        full=[n for n in tn if len(n)>=56]
        for n58 in full:
            ax=n58[53:56]
            if abs(ax[0])+abs(ax[1])+abs(ax[2])<1e-4: ax=[0,1,0]
            nodes.append(node(n58[0], n58[49], n58[15], n58[17], n58[34], n58[36], n58[13], n58[19:22], n58[47], ax))
        if not nodes:
            ax=p.get("coneAxis",[0,1,0])
            nodes=[node(0.0, p["spawnRate"][0], p["sizeX"][0], p["sizeY"][0], p["speed"][0], p["coneAngle"][0],
                        p["alpha"][0], p["colorBirth"], p["gravity"][0], ax),
                   node(period, p["spawnRate"][1], p["sizeX"][1], p["sizeY"][1], p["speed"][1], p["coneAngle"][1],
                        p["alpha"][1], p["colorDeath"], p["gravity"][1], ax)]
        fx.set_editor_property("nodes",nodes)
        return fx
    if t in (0xB,0xC,0x18):
        period=max(p.get("period",4.0),0.05)
        fx.set_editor_property("end_mode",p.get("endMode",3)); fx.set_editor_property("single_quad",True)
        rgb=p.get("quadColorFallback") or p.get("colorBase") or [1,1,1]
        alpha=min(max(p.get("quadAlphaFallback",p.get("alpha",[1,1])[0]),0.0),1.0)
        sx=max(p.get("sizeXcur",0.0),p.get("sizeBase",0.0),20.0); sy=p.get("sizeYcur",0.0) or sx
        # ★ 0xC keyframe-трек (CONFIRMED дизасм animAdvanceKeyframe@0x11FA8E0 + CEmitter_SpawnParticle): loop-режим
        #   spawnSpread@+0x88=1 = RESTART форвард (НЕ ping-pong) → концы трека должны СОВПАДАТЬ для бесшовности.
        #   Alpha = КОНСТАНТА 1.0 (не мерцает). «Мерцание яркости» = RGB-канал под аддитивом (MODULATE2X): сиды
        #   colorBase=[0,0,0] тёмные (края, невидимо) → ключ(t=1) серый (ярче, видно) → назад к тёмному. Края равны
        #   (0=0) ⇒ плавно гаснет↔разгорается без скачка. rot 0→фаза→0 (качание, тоже равные края). posY=65 = сдвиг вверх.
        tn=p.get("trackNodes") or []
        cbase=[min(max(c,0.0),1.0) for c in (p.get("colorBase") or [0,0,0])]   # тёмные сиды RGB (края мерцания)
        if tn:
            n40=tn[0]
            kx=max(n40[15],1.0); ky=max(n40[17],1.0)
            krgb=[min(max(c,0.0),1.0) for c in n40[19:22]]                     # ключ RGB (ярче → пик разгорания)
            krot=unreal.Vector(n40[28],n40[30],n40[32])                        # фаза-наклон ключа (±2.5/7.5°)
            kpos=unreal.Vector(n40[1]+n40[7],n40[3]+n40[9],n40[5]+n40[11])     # basePos+pos (posY=65 → вверх)
            def mk(tt, crgb, rot):
                nd=node(tt,0,kx,ky,0,0,1.0,crgb,0,[0,1,0])                     # alpha=1 КОНСТАНТА; пульс — по RGB
                nd.set_editor_property("ring_rot",rot); nd.set_editor_property("ring_base_pos",kpos)
                return nd
            zero=unreal.Vector(0,0,0)
            nodes=[mk(0.0,cbase,zero), mk(1.0,krgb,krot), mk(period,cbase,zero)]   # тёмный→ярче→тёмный (бесшовный RESTART)
        else:
            nodes=[node(0.0,0,sx,sy,0,0,alpha,rgb,0,[0,1,0]), node(period,0,sx,sy,0,0,0.0,rgb,0,[0,1,0])]
        fx.set_editor_property("period",period); fx.set_editor_property("nodes",nodes)
        return fx
    if em.get("mesh"):
        meshes=JM.load_jem(em.get("jem") or em["name"]) or []
        if not meshes:
            unreal.log_warning("Нет .jem для %s"%em["name"]); return None
        m=meshes[0]
        fx.set_editor_property("mesh_emitter",True)
        fx.set_editor_property("mesh_verts",[unreal.Vector(*v) for v in m["verts"]])
        fx.set_editor_property("mesh_normals",[unreal.Vector(*v) for v in m["normals"]])
        fx.set_editor_property("mesh_uv0",[unreal.Vector2D(u,v) for u,v in m["uvs"]])
        fx.set_editor_property("material",get_material(m["tex"] or em["name"], blend_add))
        return fx
    return None

def build_actor_for_slot(eas, casket, slot, effect):
    r=JEB.parse_effect(effect)
    if not r: unreal.log_warning("Нет .jeb %s"%effect); return None
    add=r.get("blendAdditive",True)
    ems=[]
    for em in r["emitters"]:
        fx=build_emitter(em, add)
        if fx: ems.append(fx)
    if not ems: unreal.log_warning("Слот %d: нет эмиттеров"%slot); return None

    # базовый трансформ = кость CH_00{slot+1} шкатулки (если найдена)
    bone=unreal.Name("CH_00%d"%(slot+1))
    base=unreal.Transform()
    comp=casket.skeletal_mesh_component if casket else None
    if comp and comp.does_socket_exist(bone):
        base=comp.get_socket_transform(bone, unreal.RelativeTransformSpace.RTS_WORLD)
    elif casket:
        base=casket.get_actor_transform()

    act=eas.spawn_actor_from_class(unreal.AsdaEffectActor, base.translation)
    act.set_actor_transform(base, False, False)
    act.set_actor_label("CharSelectGlyph_slot%d"%slot)
    act.set_editor_property("effect_name",effect)
    hdr=r.get("header") or {}
    dur=max(float(hdr.get("duration") or 3.1),0.05)
    ry=hdr.get("rotY") or [0,0,0]
    # holder-spin из .jeb (rotY=0 для creation) + ручной GLYPH_YAW_SPEED (вердикт реверса)
    a0=ry[0]
    speed=((ry[1]-a0)/dur if abs(ry[1]-a0)>1e-4 else 0.0) + GLYPH_YAW_SPEED
    act.set_editor_property("holder_yaw_start", a0)
    act.set_editor_property("holder_yaw_speed", speed)
    # ★★★ Ф16 (К-1, 20.09.2026): запись массива эмиттеров — под явным гейтом
    #   `asda.Fx.Glow.EmittersWriter` (обоснование — asda_fx_common.emitters_writer_gate):
    #   признак «пишет скрипт» не должен зависеть от формы события правки.
    with _FXC_GATE.emitters_writer_gate():
        act.set_editor_property("emitters",ems)
    # ★ Editor-Friendly: арт-дефолты глифа выставляем ЗДЕСЬ как редактируемые level-значения на акторе
    #   (пер-глиф правится в редакторе). GameMode их НЕ форсит, только читает/показывает.
    act.set_editor_property("cull_distance", 1.0e9)             # глиф ~сотни ед. в тысячной сцене — не куллить
    act.set_editor_property("base_already_ue", True)            # база = кость шкатулки (уже UE-оси); b-префикс срезан UE-Python
    act.set_editor_property("spin_mesh_emitters", True)         # вращение кристалла (эталон objPitchStepBy6Deg)
    act.set_editor_property("effect_emitter_scale", 0.85)       # размер сияния/искр (0xC/0xD), −15%
    # ★★★ МАСШТАБ ГЛИФА — ЭТО МИРОВОЙ МАСШТАБ, А НЕ ОТНОСИТЕЛЬНЫЙ (грабля, 12.09.2026).
    #   Строка стоит ДО attach_to_component, то есть актор ещё НЕ пристёгнут: у непристёгнутого
    #   актора относительный масштаб РАВЕН мировому. Поэтому прежнее «85» было МИРОВЫМ масштабом
    #   ×100-сцены (85 = 100 юн/игр.ед. × 0.85 арт-поправки), а не относительным к шкатулке.
    #   В уровне у глифов rel_scale (−85, 85, 85): это результат KEEP_WORLD-привязки (мировые 85
    #   поделены на масштаб родителя (−1,1,1)), а вовсе не то, что задавал этот тул.
    #   ⚠ ЧЕМ ГРОЗИЛО: после перевода сцены в ×1 шкатулка имеет масштаб 0.01, и прежний прогон тула
    #   дал бы глиф с мировым масштабом 85 при сцене ×1 — в 100 раз больше нужного, char-select закрыт.
    #   ЛЕЧИМ ЧЕСТНО: мировой масштаб = арт-поправка × масштаб сцены. KEEP_WORLD ниже удержит его
    #   при ЛЮБОМ масштабе родителя, поэтому формула не зависит от того, чему равна шкатулка.
    glyph_world = GLYPH_ART_SCALE * LOGIN_SCALE          # ×1-сцена → 0.85; ×100-сцена → 85
    act.set_actor_scale3d(unreal.Vector(glyph_world, glyph_world, glyph_world))
    act.set_actor_hidden_in_game(True)                          # C++ показывает, если слот пуст
    # привязка к кости слота (глиф едет со шкатулкой)
    if comp and comp.does_socket_exist(bone):
        act.attach_to_component(comp, bone,
            unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD,
            unreal.AttachmentRule.KEEP_WORLD, False)
    # ★ КОНТРОЛЬ (иначе грабля выше вернётся молча): после привязки читаем ФАКТИЧЕСКИЙ мировой масштаб.
    ws = act.get_actor_scale3d()
    if abs(abs(ws.x) - glyph_world) > 1e-3 * max(glyph_world, 1.0) or abs(abs(ws.z) - glyph_world) > 1e-3 * max(glyph_world, 1.0):
        unreal.log_error("  слот %d: мировой масштаб глифа %s, ожидался %.4f по модулю — привязка съела масштаб"
                         % (slot, ws, glyph_world))
    unreal.log("  слот %d: глиф %s эмиттеров=%d yawSpeed=%.1f attach=%s мировой масштаб=(%.4f,%.4f,%.4f) (ожидали %.4f)"%(
        slot, effect, len(ems), speed, "CH_00%d"%(slot+1) if (comp and comp.does_socket_exist(bone)) else "origin",
        ws.x, ws.y, ws.z, glyph_world))
    return act

def main():
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_place_charselect_glyph.py'
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not EAL.does_asset_exist(LEVEL): unreal.log_warning("Нет уровня %s"%LEVEL); return
    les.load_level(LEVEL)
    # снести старые глифы
    for a in list(eas.get_all_level_actors()):
        if a.get_actor_label().startswith("CharSelectGlyph"):
            try: eas.destroy_actor(a)
            except Exception: pass
    # найти шкатулку
    casket=None
    for a in eas.get_all_level_actors():
        if a.get_class().get_name()!="SkeletalMeshActor": continue
        comp=a.skeletal_mesh_component; sm=comp.get_skeletal_mesh_asset() if comp else None
        if sm and CASKET_MESH_NAME in sm.get_name().lower(): casket=a; break
    if not casket:
        unreal.log_warning("Шкатулка cre_creation01 не найдена — глифы в origin (C++ довяжет к CH_00N)")
    n=0
    for slot,eff in GLYPHS.items():
        if build_actor_for_slot(eas, casket, slot, eff): n+=1
    les.save_current_level()
    unreal.log("=== CHAR-SELECT ГЛИФЫ: акторов=%d (СКРЫТЫ стартово; C++ показывает пустые слоты) ==="%n)


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
