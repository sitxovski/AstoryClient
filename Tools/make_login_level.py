# Copyright (c) 2026 @sitxovski (Discord)
# Создаёт /Game/Map/Login с GameMode=AAsdaLoginGameMode и делает его дефолтным уровнем.
# Запуск: run_headless.bat make_login_level.py
#
# ★★★ МАСШТАБ СЦЕНЫ (12.09.2026). Раньше скрипт жёстко строил логин в ×100 (1 игровая единица =
#   100 юнитов UE) — артефакт glTF-импорта задника и шкатулки. Логин был единственной локацией
#   порта не в ×1; все игровые карты — `asda_build_map_level.py SCALE = 1.0`. Из-за ×100 длины-
#   умолчания UE (радиус затухания точечного/конусного света 1000 юн, дальность каскадов теней)
#   на логине не покрывали ничего. Теперь масштаб — переменная SCALE (ASDA_LOGIN_SCALE, умолч. 1.0),
#   ОДНА И ТА ЖЕ у всех тулов логина (asda_build_backdrop_elements, asda_export_backdrop_anim,
#   asda_rescale_login_scene) и у GLoginScale в AsdaLoginGameMode.cpp. Откат: ASDA_LOGIN_SCALE=100.
import os
import unreal

SCALE = float(os.environ.get("ASDA_LOGIN_SCALE", "1.0"))   # было жёстко 100.0
# ★★★ РЕЖИМ ПЕРЕЛЁТА (13.09.2026, решение владельца). camera (умолч.) — фон и шкатулка СТОЯТ в КОНЕЧНОЙ
#   позе перелёта (последний ключ трека b_00_0 = тождество), а летит камера: LS_LoginFlight строит
#   asda_build_login_flight_sequence.py. scene — прежняя схема: сцена ставится в ДАЛЬНЮЮ позу (key0),
#   секвенция LS_LoginFlight_SceneMove везёт её к неподвижной камере. Камера в обоих режимах ставится
#   в одну и ту же позу — позу ПОСЛЕДНЕГО кадра (eye (0,195,800) эталона).
FLIGHT_MODE = os.environ.get("ASDA_LOGIN_FLIGHT_MODE", "camera").strip().lower()

MAP = "/Game/Map/Login"
GM  = "/Script/AsdaReborn.AsdaLoginGameMode"

def log(m): unreal.log("[make_login_level] " + str(m))

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


# 2) GameMode override в World Settings
world = ues.get_editor_world()

# 2b) ИДЕМПОТЕНТНОСТЬ: снести ранее заспавленные нами актёры (свет/старт/фон/камера), чтобы не плодить дубли
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
_KILL = (unreal.DirectionalLight, unreal.SkyLight, unreal.PlayerStart,
         unreal.StaticMeshActor, unreal.SkeletalMeshActor, unreal.CameraActor)


# 3b) 3D-фон логина cre_creback-a_00 = СКЕЛЕТНЫЙ + АНИМАЦИЯ (LoginCharMake_LoadBackdrop@0xF21150).
#     Статик-версия была НЕВЕРНА (узлы разлетались). Анимация позирует сцену/шкатулку.
SKEL = "/Game/AsdaAssets/Models/Char/cre_creback-a_00/SkeletalMeshes/cre_creback-a_00"
ANIM = "/Game/AsdaAssets/Models/Char/cre_creback-a_00/SkeletalMeshes/cre_creback-a_00_Anim"

# 3b-2) ШКАТУЛКА cre_creation01-a_00 (charCreationBuildPreview@0xF9ED20, позиция (0,0,0) game = UE origin).
#       Клип anim_00 = idle/закрыта. Модель отдельная от задника (её не было — корень бага «шкатулки нет»).
CASKET = "/Game/AsdaAssets/Models/Char/cre_creation01-a_00/SkeletalMeshes/cre_creation01-a_00"
CASKET_ANIM = "/Game/AsdaAssets/Models/Char/cre_creation01-a_00/SkeletalMeshes/cre_creation01-a_00anim_00"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'make_login_level.py'
    # 1) новый пустой уровень (создаёт .umap и открывает его)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        log("уровень уже есть — открываю")
        les.load_level(MAP)
    else:
        les.new_level(MAP)
        log("создан уровень " + MAP)
    ws = world.get_world_settings()
    gm_class = unreal.load_object(None, GM)
    if gm_class is None:
        gm_class = unreal.load_class(None, GM)
    ws.set_editor_property("default_game_mode", gm_class)
    log("GameMode override = " + GM)
    for a in list(eas.get_all_level_actors()):
        if isinstance(a, _KILL):
            eas.destroy_actor(a)
    log("очищены прежние свет/старт/фон-актёры")
    # 3) свет + небо, чтобы сцена не была чёрной (до 3D-фона cre_creback)
    try:
        eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,300), unreal.Rotator(roll=0.0, pitch=-45.0, yaw=0.0))  # ★ по ключам: позиционный Rotator(-45,0,0) ставил ROLL, солнце лежало (Д-17)
        eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,300))
        eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0,0,100))
        log("добавлены DirectionalLight/SkyLight/PlayerStart")
    except Exception as e:
        log("свет/старт: " + str(e))
    cam_target = unreal.Vector(0, 0, 0)
    try:
        skmesh = unreal.load_object(None, SKEL)
        anim   = unreal.load_object(None, ANIM)
        if skmesh:
            # ★★ WORLD-ТРАНСФОРМ СЦЕНЫ НА ЛОГИНЕ = ЯКОРЬ «cre_creback-a_00_all link», ДАЛЬНЯЯ поза
            #    = key0 клипа cre_creback-b_00_0.jbb (Game_UpdateAndRenderFrame стр.539867-539881:
            #    матрица якоря копируется в renderCtxA/B + posSetWithChildOffset). Камера при этом = формула
            #    eye(0,195,800) БЕЗ поворота (live view @0x290092C). Проверка: шкатулка проецируется fx=-0.52
            #    (слева), depth 2461 — совпадает со скрином оригинала.
            #    Числа = точный parse b_00_0 key0 (jointWorld, glTF-конвенция) → UE (P=swap yz, ×100; ★ 12.09 делятся на 100 и умножаются на SCALE):
            #    ЗЕРКАЛО по горизонтали (подтверждено сверкой с оригиналом): итог = Mirror_X · M_anchor.
            #    Разложение: loc'=(−x,y,z), R2 = S·R·S (S=diag(−1,1,1)), scale=(−1,1,1).
            # ★ 12.09: числа — в ×100-единицах разбора (жёсткий parse key0), приводим к масштабу сцены.
            SCENE_LOC = unreal.Vector(33997.97 * SCALE / 100.0, 147589.38 * SCALE / 100.0, 11248.37 * SCALE / 100.0)
            SCENE_ROT = unreal.MathLibrary.make_rotation_from_axes(
                unreal.Vector(0.739478, -0.652249, -0.166567),  # forward (S·R·S col0)
                unreal.Vector(0.673160,  0.718404,  0.175357),  # right   (col1)
                unreal.Vector(0.005286, -0.241799,  0.970312))  # up      (col2)
            # ★ 12.09: меши задника и шкатулки запечены glTF-импортом в ×100 → масштаб актора
            #   несёт масштаб сцены. Знак X — авторское зеркало (Mirror_X), его сохраняем.
            SCENE_SCALE = unreal.Vector(-1.0 * SCALE / 100.0, 1.0 * SCALE / 100.0, 1.0 * SCALE / 100.0)
            if FLIGHT_MODE == "camera":
                # ★ 13.09: сцена статична в КОНЕЧНОЙ позе перелёта (тождество). Зеркало — в SCENE_SCALE, оно остаётся.
                SCENE_LOC = unreal.Vector(0.0, 0.0, 0.0)
                SCENE_ROT = unreal.Rotator(0.0, 0.0, 0.0)
            ska = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, SCENE_LOC, SCENE_ROT)
            ska.set_actor_scale3d(SCENE_SCALE)
            ska.set_actor_label("LoginBackdrop_cre_creback")
            comp = ska.skeletal_mesh_component
            comp.set_skeletal_mesh_asset(skmesh)
            import os as _osb
            freeze_bg = _osb.environ.get("ASDA_FREEZE_BACKDROP", "") == "1"
            if anim:
                comp.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                comp.set_animation(anim)
                if freeze_bg:
                    comp.set_position(0.0, False); comp.set_play_rate(0.0)   # ТЕСТ: заморозить задник на кадре 0
                else:
                    comp.play(True)  # луп сцена-клипа
            b = skmesh.get_bounds(); cam_target = b.origin
            log("СКЕЛЕТНЫЙ фон cre_creback размещён (freeze=%s)" % freeze_bg)
        else:
            log("ВНИМАНИЕ: скелетный меш не загрузился: " + SKEL)
    except Exception as e:
        log("фон: " + str(e))
    try:
        import os as _os2
        skip_casket = _os2.environ.get("ASDA_NO_CASKET", "") == "1"
        csk = None if skip_casket else unreal.load_object(None, CASKET)
        cska_anim = unreal.load_object(None, CASKET_ANIM)
        if skip_casket:
            log("ТЕСТ: cre_creation01 НЕ размещена (ASDA_NO_CASKET)")
        if csk:
            # Шкатулка получает ТОТ ЖЕ якорь-трансформ (+зеркало), что и задник.
            cact = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, SCENE_LOC, SCENE_ROT)
            cact.set_actor_scale3d(SCENE_SCALE)
            cact.set_actor_label("LoginCasket_cre_creation01")
            ccomp = cact.skeletal_mesh_component
            ccomp.set_skeletal_mesh_asset(csk)
            # ★ jbm-вершины авторены в ОТКРЫТОЙ позе; ЗАКРЫТАЯ шкатулка = клип _0 замороженный на КАДРЕ 0
            #   (анализ клипов: frame0 z-размах 483 компактно=закрыто, frameEnd 917=открыто). Логин показывает закрытую.
            if cska_anim:
                ccomp.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                ccomp.set_animation(cska_anim)   # anim_00 = клип _0
                _ct = _os2.environ.get("ASDA_CASKET_TIME", "")
                if _os2.environ.get("ASDA_CASKET_PLAY", "") == "1":
                    ccomp.play(True)             # ТЕСТ: проиграть клип (искать закрытый кадр)
                else:
                    ccomp.set_position(float(_ct) if _ct else 0.0, False)
                    ccomp.set_play_rate(0.0)     # заморозить на кадре
            log("ШКАТУЛКА cre_creation01: время=%s play=%s" % (_os2.environ.get("ASDA_CASKET_TIME","0"), _os2.environ.get("ASDA_CASKET_PLAY","")))
        else:
            log("ВНИМАНИЕ: шкатулка не загрузилась: " + CASKET)
    except Exception as e:
        log("шкатулка: " + str(e))
    # 3c) Камера логина ТОЧНО по эталону (login_scene_flow.md §S2/S3):
    #     target_game=(0,187,0); статика логина = конец фазы «отъезд»: dist=1700, pitch=-0.5 рад; yaw=roll=0.
    #     eye = target + Rot(pitch)·(0,0,dist) (row-vector D3DX): eye_game=(0, 187+d·sin0.5, d·cos0.5).
    #     Оси: скелетка импортирована Interchange'ом → game(x,y,z) → UE(100·x, −100·z, 100·y) (сверено пробой костей).
    #     FOV 30° у D3DX — ВЕРТИКАЛЬНЫЙ; UE field_of_view — горизонтальный → при родном 4:3: 2·atan(tan15°·4/3)=39.31°
    #     + constrain_aspect_ratio 4:3 (кадр 1:1 с оригиналом 1024×768).
    try:
        import math
        G2U = lambda x, y, z: unreal.Vector(SCALE*x, -SCALE*z, SCALE*y)   # ★ 12.09: было жёстко 100.0
        # ★ ЖИВОЙ КЛИЕНТ (ReadProcessMemory на state 2, view @0x290092C): eye=(0,195,800), pitch=-0.01,
        #   target (0,187,0), fovY 30. dist=800 (слот @0x2900C6C), НЕ 680. Но клиент рендерит сцену с
        #   world-поворотом инстанса → относительный орбитальный yaw ≈ ±52° (фит: шкатулка fx=-0.42,
        #   спиральная стена fx=+0.40 по эталонному скрину). Знак подобран UE-рендером.
        import os
        # Камера СТРОГО по live view-матрице клиента (@0x290092C): eye=(0,195,800), target (0,187,0), yaw=0.
        # Вращение сцены даёт МОДЕЛЬ (см. выше model_yaw), не камера.
        # ★ Камера логина = ТОЧНО live view-матрица клиента (@0x290092C): eye=(0,195,800), pitch=-0.01, yaw=0.
        #   Вид «сбоку издалека» даёт НЕ камера, а якорь-трансформ сцены (см. SCENE_ROT выше).
        DIST = float(os.environ.get("ASDA_LOGIN_DIST", "800.0"))
        PITCH = float(os.environ.get("ASDA_LOGIN_PITCH", "-0.01"))
        YAW = math.radians(float(os.environ.get("ASDA_LOGIN_YAW", "0.0")))
        # Пан по горизонтали: камера на оси +z, смотрит на (LOOK_X, 187, 0). LOOK_X>0 → сцена панится вправо,
        #   шкатулка (game x≈69) уходит ВЛЕВО, справа открывается дерево/папоротники (как в оригинале).
        LOOK_X = float(os.environ.get("ASDA_LOOK_X", "0.0"))
        tgt_g = (LOOK_X, 187.0, 0.0)
        eye_g = (DIST*math.sin(YAW)*math.cos(PITCH),
                 187.0 - DIST*math.sin(PITCH),
                 DIST*math.cos(YAW)*math.cos(PITCH))
        eye, tgt = G2U(*eye_g), G2U(*tgt_g)
        cam = eas.spawn_actor_from_class(unreal.CameraActor, eye, unreal.Rotator(0,0,0))
        cam.set_actor_label("LoginCamera")
        cc = cam.camera_component
        # ★ 16:9 БЕЗ чёрных полос: НЕ констрейним аспект — камера заполняет окно (как оригинал: «картинка всегда
        #   заполняет окно 1:1», RESOLUTION_SYSTEM_RE). Оригинал (D3DX) держит ВЕРТИКАЛЬНЫЙ fovY=30° при любом
        #   аспекте (проекция с fixed fovY, hFOV=f(aspect)) → в UE это MaintainYFOV (DefaultEngine.ini
        #   [/Script/Engine.LocalPlayer]). field_of_view (UE=горизонтальный) = hFOV для vFOV30 при 16:9 = 2·atan(tan15°·16/9)=50.93°.
        cc.set_editor_property("field_of_view", math.degrees(2.0*math.atan(math.tan(math.radians(15.0))*16.0/9.0)))
        cc.set_editor_property("constrain_aspect_ratio", False)
        cc.set_editor_property("aspect_ratio", 16.0/9.0)
        cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(eye, tgt), False)
        cam.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.PLAYER0)
        log("камера логина: eye=%s target=%s fovY=30 (4:3)" % (eye, tgt))
    except Exception as e:
        log("камера: " + str(e))
    # 4) сохранить
    les.save_current_level()
    unreal.EditorAssetLibrary.save_asset(MAP)
    log("СОХРАНЕНО. Готово.")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
