# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_probe_material_light.py — ЧИСЛОВОЙ ПРОБНИК СВЕТОТЕНИ мастер-материалов.
#   Строит ИЗОЛИРОВАННУЮ сцену с ФИКСИРОВАННЫМ светом (один DirectionalLight, без skylight/атмосферы —
#   ambient=0, худший случай: видно ровно то, что материал даёт САМ), ставит эталонный меш,
#   назначает MID каждого проверяемого мастера с ОДНОЙ И ТОЙ ЖЕ текстурой, снимает SceneCapture2D
#   в RenderTarget и экспортирует PNG. Сравнение яркостей/гистограмм — вне UE (asda_probe_compare.py).
#
#   Сцена детерминирована: одинаковая при каждом запуске, отличается ТОЛЬКО материал ⇒ разница
#   пикселей = разница материалов, без примеси освещения сцены.
#
#   Запуск (нужен РЕНДЕР, поэтому БЕЗ -NullRHI, с -RenderOffscreen):
#     UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="...asda_probe_material_light.py <tag>"
#                          -unattended -nosplash -nosound -RenderOffscreen
#   <tag> — суффикс имён PNG (напр. "before" / "after").
import os, sys
import unreal

args = [a for a in sys.argv if not a.lower().endswith(".py")]
TAG = args[0] if args else "run"
OUTDIR = r"@@ASTORY_DEV_ROOT@@\RE\reports\matprobe"

# ★ Варианты задаются ПАРАМЕТРАМИ одного мастера, а не отдельными материалами: у скалярных
#   параметров ОДНА шейдер-перестановка ⇒ сравнение не зависит от того, успел ли скомпилироваться
#   новый шейдер (иначе оба «новых» материала рисуются одинаковым фолбэком и дифф ложно нулевой).
#   Регрессия точно воспроизводима на ФИКСИРОВАННОМ мастере:
#       AmbientFloor=0      → Emissive не даёт вклада (как когда он вовсе не был подключён)
#       SpecBase=0, SpecAlphaBoost=0.5 → Specular = 0.5·Tex.A (ровно прежняя формула SpecScale·Tex.A)
MASTERS = [("ObjectLit", "/Game/Materials/M_AsdaObjectLit", {}),                                    # БАЗА «как было»
           ("Regressed", "/Game/Materials/M_AsdaCharLit",
            {"AmbientFloor": 0.0, "SpecBase": 0.0, "SpecAlphaBoost": 0.5}),                         # регрессия
           ("CharLit",   "/Game/Materials/M_AsdaCharLit",
            {"AmbientFloor": 0.25, "SpecBase": 0.5, "SpecAlphaBoost": 0.0})]                        # после фикса (дефолты)
# (текстура, меш) — эталонные образцы: лук с «дырявой» альфой и меч со сплошной альфой
TEXTURES = [("bow", "/Game/AsdaAssets/Textures/Characters/Texture/ch1_n_90_bbow209_00_00_1"),
            ("swd", "/Game/AsdaAssets/Textures/Characters/Texture/ch1_n_00_swrd003_00_00_0")]
MESHES = [("sphere", "/Engine/BasicShapes/Sphere"),                                                   # чистая математика затенения (весь диапазон NdotL)
          ("bow",    "/Game/AsdaAssets/Models/Char/ch1_n_90_bbow209_00_00_0/StaticMeshes/ch1_n_90_bbow209_00_00_0")]
RES = 512

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def log(m): unreal.log_warning("[probe] %s" % m)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)



rt_path = "/Game/Temp/RT_MatProbe"



WARMUP = 4          # захватов «вхолостую» перед зачётным (шейдер/текстура/LOD должны устояться)

made = []


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★ ГРАБЛЯ: new_level ПАДАЕТ («An asset already exists at this location»), если от прошлого прогона
    #   остались уровень/MIC-и — и дальше скрипт молча работает в СТАРОМ мире (кадры получаются
    #   неотличимыми от предыдущего прогона, что выглядит как «фикс не подействовал»). Чистим заранее.
    for _stale in ("/Game/Temp/L_MatProbe", "/Game/Temp/L_MatProbe_" + TAG):
        if EAL.does_asset_exist(_stale):
            EAL.delete_asset(_stale)
    for _mt, _, _ in MASTERS:
        for _tt, _ in TEXTURES:
            _p = "/Game/Temp/MIC_probe_%s_%s" % (_mt, _tt)
            if EAL.does_asset_exist(_p):
                EAL.delete_asset(_p)
    if not les.new_level("/Game/Temp/L_MatProbe_" + TAG):
        raise RuntimeError("new_level не удался — прогон работал бы в чужом мире, результаты были бы ложными")
    # --- фиксированный свет: одна направленная лампа, ambient = 0 ---
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 300),
                                     unreal.Rotator(0, -35, -60))   # pitch -35°, yaw -60° — часть меша в свету, часть в тени
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)   # свойство-обёртка .directional_light_component в UE5.8 отсутствует
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    sc.set_editor_property("intensity", 3.14159)          # 1 люмен-эквивалент UE по умолчанию
    sc.set_editor_property("light_color", unreal.Color(255, 255, 255, 255))
    sc.set_editor_property("cast_shadows", True)
    sc.set_editor_property("dynamic_shadow_distance_movable_light", 5000.0)
    # --- камера-захват ---
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(-260, 0, 0), unreal.Rotator(0, 0, 0))
    cc = cap.get_component_by_class(unreal.SceneCaptureComponent2D)
    cc.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    cc.set_editor_property("capture_every_frame", False)
    cc.set_editor_property("capture_on_movement", False)
    cc.set_editor_property("fov_angle", 45.0)
    pp = cc.get_editor_property("post_process_settings")
    pp.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)   # ★ БЕЗ авто-экспозиции: иначе кадр сам себя «выравнивает» и дифф исчезает
    pp.set_editor_property("auto_exposure_bias", 0.0)
    pp.set_editor_property("motion_blur_amount", 0.0)
    pp.set_editor_property("bloom_intensity", 0.0)
    pp.set_editor_property("ambient_occlusion_intensity", 0.0)
    cc.set_editor_property("post_process_settings", pp)
    if EAL.does_asset_exist(rt_path):
        EAL.delete_asset(rt_path)
    rt = ATOOLS.create_asset("RT_MatProbe", "/Game/Temp", unreal.TextureRenderTarget2D,
                             unreal.TextureRenderTargetFactoryNew())
    rt.set_editor_property("size_x", RES)
    rt.set_editor_property("size_y", RES)
    rt.set_editor_property("render_target_format", unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB)
    cc.set_editor_property("texture_target", rt)
    # ★ ДЕТЕРМИНИЗМ: без этого первые кадры ловят ещё не подгруженные мип-уровни (стриминг), и
    #   один и тот же материал даёт РАЗНЫЕ снимки на разных итерациях (проверено: c1 != c3 при
    #   одинаковом материале). Полная загрузка текстур + прогревочные захваты снимают эффект.
    unreal.SystemLibrary.execute_console_command(world, "r.Streaming.FullyLoadUsedTextures 1")
    unreal.SystemLibrary.execute_console_command(world, "r.Streaming.MipBias -5")
    for _t, _p in TEXTURES:
        EAL.load_asset(_p)          # только подгрузить в память; свойства текстур НЕ меняем (не портим ассеты проекта)
    # ★ ГРАБЛЯ: если шейдеры мастера ещё не в DDC (материал только что пересобран), UE рисует ФОЛБЭК —
    #   и ВСЕ варианты такого мастера дают побитово одинаковый кадр (ложный вывод «фикс не работает»).
    #   Финализация асинхронной компиляции требует тика редактора, которого у блокирующего py-скрипта нет,
    #   поэтому принудительно компилируем каждый мастер синхронно ДО захватов.
    for _mt, _mp, _ in MASTERS:
        _m = EAL.load_asset(_mp)
        if _m:
            unreal.MaterialEditingLibrary.recompile_material(_m)
            log("шейдеры мастера %s: перекомпиляция запрошена" % _mt)
    os.makedirs(OUTDIR, exist_ok=True)
    for mesh_tag, mesh_path in MESHES:
        sm = EAL.load_asset(mesh_path)
        if not sm:
            log("НЕТ меша %s" % mesh_path); continue
        actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        comp = actor.get_component_by_class(unreal.StaticMeshComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_static_mesh(sm)
        # вписать меш в кадр: масштаб по габаритам
        ext = sm.get_bounds().box_extent
        r = max(ext.x, ext.y, ext.z) or 50.0
        s = 50.0 / r
        actor.set_actor_scale3d(unreal.Vector(s, s, s))
        for tex_tag, tex_path in TEXTURES:
            tex = EAL.load_asset(tex_path)
            if not tex:
                log("НЕТ текстуры %s" % tex_path); continue
            for mat_tag, mat_path, overrides in MASTERS:
                mat = EAL.load_asset(mat_path)
                if not mat:
                    log("НЕТ мастера %s" % mat_path); continue
                # ★ ГРАБЛЯ: MID (create_dynamic_material_instance + set_texture_parameter_value) в
                #   РЕДАКТОРСКОМ мире без тика НЕ пропихивает параметр в рендер-прокси — обе текстуры
                #   давали побайтово одинаковый кадр. Делаем настоящие MIC-ассеты (параметр — часть ассета).
                mic_name = "MIC_probe_%s_%s" % (mat_tag, tex_tag)
                mic_path = "/Game/Temp/" + mic_name
                if EAL.does_asset_exist(mic_path):
                    EAL.delete_asset(mic_path)
                mic = ATOOLS.create_asset(mic_name, "/Game/Temp", unreal.MaterialInstanceConstant,
                                          unreal.MaterialInstanceConstantFactoryNew())
                unreal.MaterialEditingLibrary.set_material_instance_parent(mic, mat)
                unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mic, "Tex", tex)
                for pname, pval in overrides.items():
                    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mic, pname, pval)
                unreal.MaterialEditingLibrary.update_material_instance(mic)
                for slot in range(max(1, comp.get_num_materials())):
                    comp.set_material(slot, mic)
                for _ in range(WARMUP):
                    cc.capture_scene()
                name = "%s_%s_%s_%s" % (mesh_tag, tex_tag, mat_tag, TAG)
                unreal.RenderingLibrary.export_render_target(world, rt, OUTDIR, name + ".png")
                made.append(name)
                log("снято %s" % name)
        eas.destroy_actor(actor)
    log("готово, кадров: %d → %s" % (len(made), OUTDIR))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
