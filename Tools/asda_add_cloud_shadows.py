# Copyright (c) 2026 @sitxovski (Discord)
# asda_add_cloud_shadows.py — ТЕНИ ОБЛАКОВ (просьба юзера 2026-07-28: «облака над картой, чтобы тени
#   отбрасывали»): Light Function на DirectionalLight карты — движущаяся маска из ЕЁ ЖЕ облачной
#   текстуры SKY_DOME_CLOUDE2 (тени соответствуют облакам купола). VolumetricCloud-путь удаляется
#   (глушился куполом, is_sky ломал сцену). Editor-Friendly: мастер M_AsdaCloudShadowLF + MIC per map
#   (параметры TileSize/SpeedU/SpeedV/Strength/Contrast — ДЕФОЛТЫ МАСТЕРА, с 2026-08-27 их
#   больше не перебивает актор), всё крутится в редакторе.
import os, re, unreal, struct
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_mat_graph as _MG   # ★ чистка графа до пустого + контроль после сборки (баг UE 5.8 DeleteAllMaterialExpressions)
import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина
import asda_model_import_ue as _MIU   # ★ дедуп №2 В1 Т13: общий поиск текстуры в <root>/Texture

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT  = unreal.AssetToolsHelpers.get_asset_tools()
MASTER = "/Game/Materials/M_AsdaCloudShadowLF"
# Ровно столько узлов строит build_master(): wp, mxy, tile, dv, tm, su, sv, ap, mult, ad, t, ctr, mc,
#   clm, str_, ms, om = 17. При изменении графа — обновить.
MASTER_EXPRESSIONS = 17

_asset = _MAPS.map_asset_name  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)

def build_master():
    name = MASTER.rsplit("/",1)[-1]
    if EAL.does_asset_exist(MASTER):
        m = EAL.load_asset(MASTER)
        # ★ В ЦИКЛЕ до пустого: один delete_all_material_expressions оставляет каждый второй узел
        #   (UE 5.8, MaterialEditingLibrary.cpp:559-568) → фантомы и дубли параметров в ассете.
        _MG.clear_expressions(m, log=unreal.log, tag="[CSH]")
    else:
        m = AT.create_asset(name, MASTER.rsplit("/",1)[0], unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    def expr(cls, x, y): return MEL.create_material_expression(m, cls, x, y)
    wp = expr(unreal.MaterialExpressionWorldPosition, -1200, 0)
    mxy= expr(unreal.MaterialExpressionComponentMask, -1050, 0)
    mxy.set_editor_property("r", True); mxy.set_editor_property("g", True)
    mxy.set_editor_property("b", False); mxy.set_editor_property("a", False)
    MEL.connect_material_expressions(wp, "", mxy, "")
    def sparam(nm, dv, x, y):
        s = expr(unreal.MaterialExpressionScalarParameter, x, y)
        s.set_editor_property("parameter_name", nm); s.set_editor_property("default_value", dv)
        return s
    tile = sparam("TileSize", 60000.0, -1050, 150)      # мир-единиц на повтор текстуры (600 м)
    dv = expr(unreal.MaterialExpressionDivide, -900, 0)
    MEL.connect_material_expressions(mxy, "", dv, "A"); MEL.connect_material_expressions(tile, "", dv, "B")
    tm = expr(unreal.MaterialExpressionTime, -1050, 300)
    su = sparam("SpeedU", 0.01, -1050, 380); sv = sparam("SpeedV", 0.0, -1050, 460)
    ap = expr(unreal.MaterialExpressionAppendVector, -900, 380)
    MEL.connect_material_expressions(su, "", ap, "A"); MEL.connect_material_expressions(sv, "", ap, "B")
    mult = expr(unreal.MaterialExpressionMultiply, -750, 340)
    MEL.connect_material_expressions(ap, "", mult, "A"); MEL.connect_material_expressions(tm, "", mult, "B")
    ad = expr(unreal.MaterialExpressionAdd, -600, 100)
    MEL.connect_material_expressions(dv, "", ad, "A"); MEL.connect_material_expressions(mult, "", ad, "B")
    t = expr(unreal.MaterialExpressionTextureSampleParameter2D, -450, 60)
    t.set_editor_property("parameter_name", "CloudTex")
    MEL.connect_material_expressions(ad, "", t, "UVs")
    # маски CLOUDE2 разрежены (alpen: mean 0.108, >0.5 всего 1.6% площади) → без усиления теней не видно
    # ★★★ 2026-08-27: было 3.0, но актор `AAsdaCloudShadowsActor` перебивал это в рантайме на 5.0 —
    #   значит В ИГРЕ РАБОТАЛО 5.0. Рантайм-присвоение убрано (§6.2 аудита
    #   `Reports/audit_runtime_style.md`), поэтому дефолт мастера = рабочее значение.
    ctr = sparam("Contrast", 5.0, -600, 320)
    mc = expr(unreal.MaterialExpressionMultiply, -450, 120)
    MEL.connect_material_expressions(t, "A", mc, "A")   # альфа cloude2 = маска облака
    MEL.connect_material_expressions(ctr, "", mc, "B")
    clm = expr(unreal.MaterialExpressionClamp, -350, 120)
    MEL.connect_material_expressions(mc, "", clm, "")
    # ★★★ 2026-08-27: было 0.6, актор ставил 1.0 ⇒ в игре работало 1.0 (ядро облака гасит солнце
    #   полностью, как обычные тени). Дефолт приведён к рабочему значению — см. комментарий выше.
    str_ = sparam("Strength", 1.0, -350, 320)           # глубина тени (0..1)
    ms = expr(unreal.MaterialExpressionMultiply, -250, 120)
    MEL.connect_material_expressions(clm, "", ms, "A")
    MEL.connect_material_expressions(str_, "", ms, "B")
    om = expr(unreal.MaterialExpressionOneMinus, -100, 120)   # свет = 1 − тень (вход OneMinus = "")
    MEL.connect_material_expressions(ms, "", om, "")
    MEL.connect_material_property(om, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # ★ Контроль графа ДО сохранения: ровно MASTER_EXPRESSIONS узлов, без дублей имён параметров;
    #   иначе RuntimeError и ассет не сохраняется (фантомы старого графа).
    _MG.check_graph(m, expected=MASTER_EXPRESSIONS, log=unreal.log, tag="[CSH]")
    MEL.recompile_material(m); m.modify(); EAL.save_loaded_asset(m)
    node = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.log("[CSH] мастер: Emissive=%s" % (node.get_class().get_name() if node else "ОБРЫВ!"))

# Текстура по имени в <root>/Texture — общая реализация `asda_model_import_ue.find_texture_in_dir` (дедуп №2 Т13).
find_texture = _MIU.find_texture_in_dir

def process(mp):
    root = "/Game/Map/%s" % _asset(mp); lvl = "%s/%s" % (root, _asset(mp))
    # ★ 16.09.2026 (обратный обход ревизии savedir14). Инструмент грузит уровень, СОХРАНЯЕТ его
    #   (save_current_level), пишет MIC под корнем карты и удаляет MI_volclouds — гейт сцены логина стоит
    #   ДО load_level и до первой записи. Проверяется КОРЕНЬ карты: под ним лежит всё, что инструмент трогает,
    #   а у сцены логина корень /Game/Map/Login и есть сам уровень со светом владельца.
    if _LG.login_map_write_blocked(root, "asda_add_cloud_shadows.py", quiet=True):
        return
    if not EAL.does_asset_exist(lvl):
        unreal.log("[CSH] %s: нет уровня — пропуск" % mp); return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(lvl)
    tex = find_texture(root, "SKY_DOME_CLOUDE2")
    if not tex:
        unreal.log("[CSH] %s: нет CLOUDE2 — пропуск" % mp); return
    nm = "MI_cloudshadow"; pth = root + "/Materials/" + nm
    mic = EAL.load_asset(pth) if EAL.does_asset_exist(pth) else \
          AT.create_asset(nm, root + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mic, EAL.load_asset(MASTER))
    MEL.set_material_instance_texture_parameter_value(mic, "CloudTex", tex)
    EAL.save_loaded_asset(mic)
    # −10% скорости всех облаков (просьба юзера 2026-07-28): ScrollBoost 10 → 9
    sd = root + "/Materials/MI_skydome"
    if EAL.does_asset_exist(sd):
        mi = EAL.load_asset(sd)
        MEL.set_material_instance_scalar_parameter_value(mi, "ScrollBoost", 9.0)
        EAL.save_loaded_asset(mi)
    sun = None
    for a in list(eas.get_all_level_actors()):
        cls = a.get_class().get_name()
        if cls == "VolumetricCloud": eas.destroy_actor(a)                     # демонтаж волюметрик-пути
        elif cls == "SkyAtmosphere" and a.get_actor_label().startswith("SkyAtmosphere_ForClouds"):
            eas.destroy_actor(a)
        elif cls == "DirectionalLight": sun = a
    if EAL.does_asset_exist(root + "/Materials/MI_volclouds"):
        EAL.delete_asset(root + "/Materials/MI_volclouds")
    if not sun:
        unreal.log_warning("[CSH] %s: нет DirectionalLight" % mp); return
    lc = sun.light_component
    try: lc.set_editor_property("atmosphere_sun_light", False)
    except Exception: pass
    lc.set_editor_property("light_function_material", mic)
    les.save_current_level()
    unreal.log("[CSH] %s: тени облаков на солнце (MI_cloudshadow)" % mp)

_only = os.environ.get("ASDA_SKY_MAPS", "")


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global mp
    build_master()
    # ★ Список карт — ЕДИНЫЙ источник asda_maps.py (18 открытых карт (без 4 закрытых/инстансных) — небо/облака).
    #   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
    MAPS = _MAPS.OUTDOOR_MAPS
    if _only: MAPS = [m for m in MAPS if m in _only.split("||")]
    for mp in MAPS:
        try: process(mp)
        except Exception as e: unreal.log_error("[CSH] %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("=== CSH ЗАВЕРШЕНО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
