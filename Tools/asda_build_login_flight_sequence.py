# Copyright (c) 2026 @sitxovski (Discord)
# Строит LevelSequence перелёта логина из запечённого трека (AsdaLoginFlightTrack.inl: 101 точка, 30fps, 3.333с).
# Editor-Friendly: дизайнер правит перелёт в Sequencer.
#
# ★★★ ДВА РЕЖИМА (13.09.2026, решение владельца «перелёт выполняет КАМЕРА, фон и шкатулка стоят»).
#
#   camera (ПО УМОЛЧАНИЮ) → /Game/AsdaAssets/Cinematics/LS_LoginFlight
#     Фон (LoginBackdrop_Flight → Anchor → 697 элементов) и шкатулка СТОЯТ в КОНЕЧНОЙ позе перелёта
#     (последний ключ трека; замер 13.09: это ТОЖДЕСТВО — позиция 0, поворот 0). Скрипт сам ставит их туда
#     и сохраняет уровень. Секвенция поссессит ТОЛЬКО LoginCamera и ведёт её по 101 ключу:
#         Cam(t) = Cam_fixed · S(t)⁻¹ · S_end        (порядок умножения FTransform: сначала левый)
#     где S(t) — прежняя поза сцены (позиция + поворот, БЕЗ масштаба: у шкатулки отрицательный масштаб X,
#     он остаётся на её акторе и в камеру не попадает), Cam_fixed — прежняя неподвижная камера
#     (= нынешняя позиция актора LoginCamera = поза камеры в последнем кадре).
#     Зрителю перелёт показывается ТЕМ ЖЕ: поза камеры ОТНОСИТЕЛЬНО сцены в каждом ключе совпадает с прежней
#     (самопроверка ниже, ошибка печатается в лог).
#     ЗАЧЕМ: владелец ставит свет в редакторе (SpotLight на шкатулку), а при летящей сцене в игре шкатулка
#     уезжала от света. Теперь сцена в редакторе стоит ровно там, где её видит игрок.
#
#   scene (ОТКАТ, ASDA_LOGIN_FLIGHT_MODE=scene) → /Game/AsdaAssets/Cinematics/LS_LoginFlight_SceneMove
#     Прежняя схема: камера неподвижна, секвенция поссессит шкатулку + LoginBackdrop_Flight и двигает их
#     ОДИНАКОВЫМ transform-треком (замер 13.09: треки шкатулки и фона совпадали бит-в-бит, собственного
#     движения у шкатулки нет). Уровень в этом режиме не трогается (секвенция сама задаёт позу сцены).
#     Переключение режима в игре — поле bFlightMovesCamera в DA_LoginConfig (см. AsdaLoginConfig.h).
#
# Completion = Keep State (после перелёта поза остаётся финальной — там char-select).
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/asda_build_login_flight_sequence.py --marker "LS_LoginFlight готов"
import os, re, math, unreal

INL = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Source/AsdaReborn/Private/Login/AsdaLoginFlightTrack.inl"
LEVEL = "/Game/Map/Login"
PKG = "/Game/AsdaAssets/Cinematics"
MODE = os.environ.get("ASDA_LOGIN_FLIGHT_MODE", "camera").strip().lower()
NAME = "LS_LoginFlight" if MODE == "camera" else "LS_LoginFlight_SceneMove"
PATH = "%s/%s" % (PKG, NAME)
FPS = 30
CAMERA_LABEL = "LoginCamera"
FLIGHT_LABEL = "LoginBackdrop_Flight"
CASKET_PREFIX = "LoginCasket"
# ★ Ожидаемая неподвижная камера (последний кадр перелёта) при GLoginScale = 1: (0, −800, 195).
#   Проверка защищает от случайно «запечённой» в уровень промежуточной позы (например, если секвенцию
#   открыли в редакторе и сохранили уровень посреди перелёта). Переопределить: ASDA_LOGIN_CAM_EXPECT="x,y,z".
CAM_EXPECT = [float(v) for v in os.environ.get("ASDA_LOGIN_CAM_EXPECT", "0,-799.96,195").split(",")]
CAM_EXPECT_TOL = float(os.environ.get("ASDA_LOGIN_CAM_EXPECT_TOL", "2.0"))

def grab(arr):
    m = re.search(arr + r"\[[^\]]*\]\s*=\s*\{(.*?)\};", txt, re.S)
    return [[float(x) for x in r.split(",")] for r in re.findall(r"\{([^}]*)\}", m.group(1))]

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

LIN = unreal.MovieSceneKeyInterpolation.LINEAR
DR = unreal.MovieSceneTimeUnit.DISPLAY_RATE


def _channels(sec):
    chans = unreal.MovieSceneSectionExtensions.get_all_channels(sec)
    def ch(prefix): return next(c for c in chans if c.get_name().startswith(prefix))
    return (ch("Location.X"), ch("Location.Y"), ch("Location.Z"),
            ch("Rotation.X"), ch("Rotation.Y"), ch("Rotation.Z"),   # X=Roll Y=Pitch Z=Yaw
            ch("Scale.X"), ch("Scale.Y"), ch("Scale.Z"))


def _unwrap(prev, cur):
    """Эйлеров угол в пределах ±180° от предыдущего ключа: Sequencer интерполирует каналы ЛИНЕЙНО,
    скачок 179→−179 провернул бы камеру на полный оборот между ключами."""
    if prev is None:
        return cur
    while cur - prev > 180.0: cur -= 360.0
    while cur - prev < -180.0: cur += 360.0
    return cur


def add_transform_track(actor, locs_, rots_, scale_const):
    """Трек трансформа на possessable: ключ на каждый кадр (loc + Euler), масштаб — константа на 0 и конец."""
    binding = seq.add_possessable(actor)
    tr = binding.add_track(unreal.MovieScene3DTransformTrack)
    sec = tr.add_section()
    sec.set_range(0, N - 1)
    sec.set_completion_mode(unreal.MovieSceneCompletionMode.KEEP_STATE)   # держать финальную позу после перелёта
    LX, LY, LZ, RX, RY, RZ, SX, SY, SZ = _channels(sec)
    pr = [None, None, None]
    for i in range(N):
        lx, ly, lz = locs_[i]; r = rots_[i]
        roll, pitch, yaw = _unwrap(pr[0], r.roll), _unwrap(pr[1], r.pitch), _unwrap(pr[2], r.yaw)
        pr = [roll, pitch, yaw]
        for c, v in ((LX, lx), (LY, ly), (LZ, lz), (RX, roll), (RY, pitch), (RZ, yaw)):
            c.add_key(unreal.FrameNumber(i), float(v), 0.0, DR, LIN)
    # ★ СОХРАНИТЬ placed-масштаб актора (шкатулка = (−0.01,0.01,0.01) зеркало по X). Без ключей scale-каналы
    #   дефолтят в 1.0 → зеркало теряется. Кеим константу на 0 и конец.
    for c, v in ((SX, scale_const.x), (SY, scale_const.y), (SZ, scale_const.z)):
        c.add_key(unreal.FrameNumber(0), float(v), 0.0, DR, LIN)
        c.add_key(unreal.FrameNumber(N - 1), float(v), 0.0, DR, LIN)
    return "%s scale=(%.4f,%.4f,%.4f)" % (actor.get_actor_label(), scale_const.x, scale_const.y, scale_const.z)


def _xf(loc, quat):
    """Жёсткий трансформ позы сцены из ключа трека (масштаб 1 — масштаб шкатулки в перелёт не входит)."""
    return unreal.Transform(unreal.Vector(loc[0], loc[1], loc[2]),
                            unreal.Quat(quat[0], quat[1], quat[2], quat[3]).rotator(),
                            unreal.Vector(1.0, 1.0, 1.0))


def _err(a, b):
    """(ошибка позиции, юн; ошибка поворота, град) между двумя трансформами."""
    dl = (a.translation - b.translation).length()
    qa, qb = a.rotation, b.rotation
    dot = abs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w)
    return dl, math.degrees(2.0 * math.acos(min(1.0, dot)))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_build_login_flight_sequence.py'
    global N, seq, txt
    if MODE not in ("camera", "scene"):
        raise RuntimeError("ASDA_LOGIN_FLIGHT_MODE=%r: ожидалось camera или scene" % MODE)
    txt = open(INL, "r", encoding="latin1").read()
    locs = grab("GLoginFlightLoc")
    quats = grab("GLoginFlightQuat")
    N = min(len(locs), len(quats))
    unreal.log("Трек: locs=%d quats=%d → N=%d, режим %s → %s" % (len(locs), len(quats), N, MODE, PATH))
    les.load_level(LEVEL)
    actors = eas.get_all_level_actors()
    casket = next((a for a in actors if a.get_actor_label().startswith(CASKET_PREFIX)), None)
    flight = next((a for a in actors if a.get_actor_label() == FLIGHT_LABEL), None)
    camera = next((a for a in actors if a.get_actor_label() == CAMERA_LABEL), None)

    if EAL.does_asset_exist(PATH):
        EAL.delete_asset(PATH)
    if not EAL.does_directory_exist(PKG):
        EAL.make_directory(PKG)

    if MODE == "scene":
        # ── ОТКАТ: прежняя схема «сцена летит к неподвижной камере» ──
        anchors = [a for a in (casket, flight) if a is not None]
        if len(anchors) < 2:
            unreal.log_warning("Найдено якорей=%d (ожидали backdrop+casket)" % len(anchors))
        if not anchors:
            raise SystemExit
        seq = ATOOLS.create_asset(NAME, PKG, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
        seq.set_display_rate(unreal.FrameRate(FPS, 1))
        seq.set_playback_start(0)
        seq.set_playback_end(N - 1)
        euls = [unreal.Quat(q[0], q[1], q[2], q[3]).rotator() for q in quats]
        names = [add_transform_track(a, locs, euls, a.get_actor_scale3d()) for a in anchors]
        EAL.save_loaded_asset(seq)
        unreal.log("=== LS_LoginFlight готов (режим scene → %s): %d ключей ×%d якоря (%s), %d fps, %.2fс ===" % (
            NAME, N, len(anchors), ", ".join(names), FPS, (N - 1) / float(FPS)))
        return

    # ── РЕЖИМ camera ──
    if camera is None or flight is None or casket is None:
        raise RuntimeError("нужны LoginCamera, LoginBackdrop_Flight и шкатулка: камера=%s фон=%s шкатулка=%s"
                           % (camera is not None, flight is not None, casket is not None))
    cam_fixed = camera.get_actor_transform()
    c = cam_fixed.translation
    off = math.sqrt((c.x - CAM_EXPECT[0]) ** 2 + (c.y - CAM_EXPECT[1]) ** 2 + (c.z - CAM_EXPECT[2]) ** 2)
    if off > CAM_EXPECT_TOL:
        raise RuntimeError("LoginCamera стоит в (%.3f, %.3f, %.3f), ожидалась конечная поза (%s) ±%.1f — "
                           "похоже, в уровень запечена промежуточная поза перелёта. Поставьте камеру в конечную "
                           "позу или задайте ASDA_LOGIN_CAM_EXPECT." % (c.x, c.y, c.z, CAM_EXPECT, CAM_EXPECT_TOL))
    cam_fixed = unreal.Transform(cam_fixed.translation, cam_fixed.rotation.rotator(), unreal.Vector(1.0, 1.0, 1.0))
    s_end = _xf(locs[N - 1], quats[N - 1])

    # 1) сцена статично в конечной позе. Масштаб актора (зеркало шкатулки) сохраняем.
    for a in (flight, casket):
        sc = a.get_actor_scale3d()
        was = a.get_actor_transform()
        a.set_actor_transform(unreal.Transform(s_end.translation, s_end.rotation.rotator(), sc), False, False)
        unreal.log("сцена статично: %s loc (%.3f,%.3f,%.3f)->(%.3f,%.3f,%.3f) rot %s->%s scale (%.4f,%.4f,%.4f)" % (
            a.get_actor_label(), was.translation.x, was.translation.y, was.translation.z,
            s_end.translation.x, s_end.translation.y, s_end.translation.z,
            was.rotation.rotator(), s_end.rotation.rotator(), sc.x, sc.y, sc.z))

    # 2) трек камеры: Cam(t) = Cam_fixed · S(t)⁻¹ · S_end ; самопроверка относительной позы
    cam_locs, cam_rots = [], []
    max_dl = max_da = 0.0
    for i in range(N):
        s_i = _xf(locs[i], quats[i])
        cam_i = cam_fixed.multiply(s_i.inverse()).multiply(s_end)
        rel_new = cam_i.multiply(s_end.inverse())          # камера относительно сцены в новой схеме
        rel_old = cam_fixed.multiply(s_i.inverse())         # ... и в прежней
        dl, da = _err(rel_new, rel_old)
        max_dl, max_da = max(max_dl, dl), max(max_da, da)
        t = cam_i.translation
        cam_locs.append([t.x, t.y, t.z])
        cam_rots.append(cam_i.rotation.rotator())
    dl_end, da_end = _err(unreal.Transform(unreal.Vector(*cam_locs[-1]), cam_rots[-1], unreal.Vector(1, 1, 1)), cam_fixed)
    unreal.log("САМОПРОВЕРКА: камера относительно сцены новая≡прежняя — макс ошибка позиции %.6f юн, поворота %.6f°;"
               " последний ключ против неподвижной камеры — %.6f юн, %.6f°" % (max_dl, max_da, dl_end, da_end))
    if max_dl > 0.05 or max_da > 0.01 or dl_end > 0.05 or da_end > 0.01:
        raise RuntimeError("самопроверка трека камеры не прошла — ассет НЕ сохранён")
    for i in (0, 30, 60, N - 1):
        unreal.log("  камера кадр %3d: loc (%.3f, %.3f, %.3f) rot %s" % (i, cam_locs[i][0], cam_locs[i][1], cam_locs[i][2], cam_rots[i]))

    seq = ATOOLS.create_asset(NAME, PKG, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(unreal.FrameRate(FPS, 1))
    seq.set_playback_start(0)
    seq.set_playback_end(N - 1)
    name = add_transform_track(camera, cam_locs, cam_rots, unreal.Vector(1.0, 1.0, 1.0))
    EAL.save_loaded_asset(seq)
    les.save_current_level()
    unreal.log("=== LS_LoginFlight готов (режим camera): %d ключей × 1 камера (%s), %d fps, %.2fс; сцена статична ===" % (
        N, name, FPS, (N - 1) / float(FPS)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
