# Copyright (c) 2026 @sitxovski (Discord)
# asda_shot_target_highlight.py — ДИАГНОСТИКА «белый силуэт» у подсветки цели.
# Повторяет РАНТАЙМ-путь компонента (overlay_material = MID) на мобе из уровня и снимает кадры
# ЧЕРЕЗ SceneCapture2D (синхронно — high-res-скриншот в headless не успевает отрендериться):
#   0_before   — без подсветки
#   1_red      — Color=КРАСНЫЙ (если силуэт всё равно БЕЛЫЙ → материал подменён движком)
#   2_fill_only— только заливка FillAdd=0.2, Rim=0 (эталонный вид: моб чуть светлее, текстура видна)
#   3_full     — заливка + краевое свечение
# Запуск: Tools\run_headless_rhi.bat <абс.путь>  (БЕЗ -NullRHI). env ASDA_MAP, ASDA_CLASS, ASDA_NPC_IDX.
import os, unreal

MAP = os.environ.get("ASDA_MAP", "alpen")
SEL = int(os.environ.get("ASDA_NPC_IDX", "0"))
WANT = os.environ.get("ASDA_CLASS", "AsdaMobActor")
OUTDIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/target_hl"
MATP = "/Game/Materials/M_AsdaTargetHighlight"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


IGN = ["QuestMark", "Nameplate", "Plate", "Marker", "Decal"]
meshes = []



def shot(name):
    cc.capture_scene()
    unreal.RenderingLibrary.export_render_target(world, rt, OUTDIR, name + ".png")
    unreal.log("[HL] SHOT -> %s/%s.png" % (OUTDIR, name))


def apply(color, fill, rim, rimp=3.0):
    for c in meshes:
        mid = unreal.MaterialLibrary.create_dynamic_material_instance(c, master)
        mid.set_vector_parameter_value("Color", color)
        mid.set_scalar_parameter_value("FillAdd", fill)
        mid.set_scalar_parameter_value("RimIntensity", rim)
        mid.set_scalar_parameter_value("RimPower", rimp)
        mid.set_scalar_parameter_value("MaxAdd", 0.6)
        # ★ ИМЕННО функция, а не set_editor_property: она делает MarkRenderStateDirty,
        #   иначе прокси сцены не узнает про оверлей и кадр не изменится.
        c.set_overlay_material(mid)

def clear():
    for c in meshes:
        c.set_overlay_material(None)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global c, cc, master, rt
    les.load_level("/Game/Map/%s/%s" % (MAP, MAP))
    acts = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == WANT]
    acts.sort(key=lambda a: a.get_actor_label())
    if not acts:
        unreal.log_error("[HL] акторов класса %s в уровне нет" % WANT); raise SystemExit
    sel = acts[min(SEL, len(acts) - 1)]
    for c in sel.get_components_by_class(unreal.MeshComponent):
        n = c.get_name()
        if any(s in n for s in IGN):
            unreal.log("[HL] пропуск компонента %s" % n); continue
        meshes.append(c)
    unreal.log("[HL] актор '%s', меш-компонентов к покраске: %d" % (sel.get_actor_label(), len(meshes)))
    for c in meshes:
        mats = [m.get_name() if m else "None" for m in c.get_materials()]
        unreal.log("[HL]   %s (%s) материалы=%s" % (c.get_name(), c.get_class().get_name(), mats))
    t = sel.get_actor_transform(); c0 = t.translation
    try: org, ext = sel.get_actor_bounds(False)
    except Exception: org, ext = c0, unreal.Vector(100, 100, 100)
    h = max(ext.z * 2, 150.0)
    eye = unreal.Vector(c0.x - h * 1.3, c0.y - h * 1.3, org.z + h * 0.35)
    rot = unreal.MathLibrary.find_look_at_rotation(eye, unreal.Vector(org.x, org.y, org.z))
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, eye, rot)
    cc = cap.capture_component2d
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1024, 1024, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cc.set_editor_property("texture_target", rt)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    for _k in ("capture_every_frame", "capture_on_movement"):
        try: cc.set_editor_property(_k, False)
        except Exception as _e: unreal.log_warning("[HL] %s: %s" % (_k, _e))
    cc.set_editor_property("fov_angle", 55.0)
    master = unreal.EditorAssetLibrary.load_asset(MATP)
    unreal.log("[HL] материал=%s blend=%s shading=%s skel=%s two_sided=%s" %
               (master.get_name(), master.get_editor_property("blend_mode"),
                master.get_editor_property("shading_model"),
                master.get_editor_property("used_with_skeletal_mesh"),
                master.get_editor_property("two_sided")))
    clear();                                          shot("0_before")
    apply(unreal.LinearColor(1, 0, 0, 1), 0.2, 1.6);  shot("1_red")
    apply(unreal.LinearColor(1, 1, 1, 1), 0.2, 0.0);  shot("2_fill_only")
    apply(unreal.LinearColor(1, 1, 1, 1), 0.2, 1.6);  shot("3_full")
    clear()
    unreal.log("[HL] готово")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
