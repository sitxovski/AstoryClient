# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_orient_check.py — РЕГРЕСС-ТЕСТ ОРИЕНТАЦИИ ЭФФЕКТОВ КАРТ.
#
# ЗАЧЕМ. Дважды подряд правка ОБЩЕГО кода эффектов (сначала крылья, потом шлейф транспорта) разворачивала
# уже откалиброванные эффекты карт — радуги на дирижаблях, фонари alpen. Ловил это только глаз юзера,
# потому что прежние сверки смотрели на РАЗМАХ и ВЫСОТУ, а разворот на 180° вокруг вертикали их не меняет.
# Тест считает ПОЛНЫЙ ортонормированный БАЗИС (ВСЕ ТРИ ОСИ) итоговой мировой ориентации каждого реперного
# эмиттера и печатает расхождение в ГРАДУСАХ по каждой оси.
#
# ТРИ ПРОВЕРКИ (урок регрессии №2: проверка «той же математикой» — циклическая и слепа к C++):
#   (1) ОРАКУЛ РАЗМЕЩЕНИЯ. Трансформы акторов уровня против авторских матриц effects.json (статические
#       данные игры). Ловит уезд трансформов размещения. НЕ ловит ни печку узлов, ни C++.
#   (2) СНИМОК ДАННЫХ. Базисы эмиттеров из ДАННЫХ узлов против Tools/fx_orient_baseline.json.
#       Ловит изменение запечённых данных (узлы/флаги/chiral).
#   (3) ★ РАНТАЙМ-СЕКЦИИ (главная — именно её не хватало, чтобы поймать регрессию №2). Для каждого
#       реперного актора спавнится ВРЕМЕННЫЙ ДВОЙНИК (тот же трансформ+эмиттеры) — спавн гарантирует
#       OnConstruction+прогрев ТЕКУЩЕЙ DLL (set_editor_property в headless конструкцию НЕ дёргает!) —
#       и с его PMC снимаются центроиды/габариты ДЕТЕРМИНИРОВАННЫХ секций (кольца/квады/меши; частицы
#       и биллборды пропускаются — FRand/камера). Сравнение с эталоном ловит ЛЮБУЮ правку C++-математики
#       (регрессия №2 — basePos кольца — давала тут 77–338 юн). Двойники удаляются, save нет.
#   Эталонный снимок фиксируется от КАЛИБРОВАННОГО состояния (уровень 29.07 + DLL с восстановленным
#   историческим поведением, дифф-доказательство _verify_ring_basepos_diff: ошибка 0.000).
#
# КАК ГОНЯТЬ — ОБЯЗАТЕЛЬНО ПЕРЕД И ПОСЛЕ ЛЮБОЙ ПРАВКИ ОБЩЕГО КОДА ЭФФЕКТОВ
# (AsdaEffectActor.cpp/.h, asda_place_effects.build_emitter, asda_jeb_parse, asda_jem_parse):
#     Tools\run_headless.bat Tools\asda_fx_orient_check.py                 — сверка, вердикт ОК/РЕГРЕССИЯ
#     set ASDA_FX_SNAPSHOT=1 && Tools\run_headless.bat Tools\asda_fx_orient_check.py
#                                                                         — перезаписать эталонный снимок
#         (перезаписывать ТОЛЬКО когда расхождение осознанное и подтверждено визуально)
# Лог: Reports/headless_asda_fx_orient_check.log
#
# ★ ЗАПУСК С 27.09.2026 (волна 0 графики, Reports/owner_0927/gfx_w0.md) — через обёртку, успех по логу:
#     python Tools/run_headless_safe.py Tools/asda_fx_orient_check.py --log <лог> --marker "ВЕРДИКТ:" --idle 400
#   (≈90 с; --idle 400 обязателен, когда параллельно работают другие UE — иначе обёртка добивает на загрузке).
#   Переменные окружения:
#     ASDA_FX_SNAPSHOT=1        — перефиксировать эталон (прежний копируется в fx_orient_baseline.json.bak_<время>);
#     ASDA_FX_CUR_OUT=<файл>    — выгрузить текущий снимок (разбор расхождений без перезаписи эталона);
#     ASDA_FX_CONTROL=data|twin|place|all — КОНТРОЛЬНАЯ ПОРЧА копии данных в памяти (см. _control_corrupt):
#                                 обязана дать «РЕГРЕССИЯ»; эталон при этом не пишется. Гонять после любой
#                                 правки самого теста — тест, ни разу не виденный красным, ничего не доказывает.
#   Эталон перефиксирован 27.09.2026 19:12: три прежних «вечных» расхождения разобраны и признаны намеренными
#   (rainbow2 — схема чистых 0xE от 31.07 без хиральной нормали; alpenwave — актор теперь стоит РОВНО в
#   позиции effects.json; alpenflooer — строгая орбита квада 10.09).
#
# ВАЖНО: скрипт ТОЛЬКО ЧИТАЕТ уровень — никаких save. Гонять безопасно в любой момент.
# Математика 1:1 повторяет AsdaEffectActor.cpp (ветки ring / single-quad / mesh12 / конус частиц):
# правишь ветку в C++ — правь и здесь, иначе тест перестанет ловить регрессию.
import os, sys, json, math
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

RAW_DIR = os.environ.get("ASDA_RAW", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
# реперы: разнотипные — кольцо 0xE, меш 0x12 (RotChain), квад 0xB, частицы-конус 0xD
REPERS = {
    "alpen": [
        "eff_rainbow_a_00",        # 0xE кольцо + 2×0xD, type-2 attach к дирижаблю (зеркальный носитель)
        "eff_rainbow2_a_00",       # 0xE кольцо, attach, basePos≠0, scale 23
        "eff_alpenredlight_a_00",  # 0xE фонарь (attach к лампе, хиральность по декор-меш-кости)
        "eff_alpenbluelight_a_00", # 0xE фонарь
        "eff_alpengreenlight_a_00",
        "eff_alpenyellowlight_a_00",
        "eff_alpenjewels_a_00",    # 2×0x12 меш-эмиттер с канальными вращениями (ветка RotChain)
        "eff_alpenwave_a_00",      # 2×0xB квад плоскости земли (волна)
        "eff_mdel_alpen_pado-00",  # 0xB квад-прибой (keyframe-трек)
        "eff_mdel_alpen_smoke_00", # 0xD конус-частицы (ось конуса)
        "eff_alpenflooer_a_00",    # 0xB квад-орбита
    ],
}
TOL_DEG = 0.5          # расхождение больше — регрессия
BASE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fx_orient_baseline.json")
D2R = math.pi / 180.0


def log(m): unreal.log_warning("[fxorient] %s" % m)


# ---------- элементарные вращения (game-оси, row-vector) — как в AsdaEffectActor.cpp ----------
# ИСТОРИЧЕСКИЕ (транспонированные) — только соулмейт/char-mount пути:
def _ry(p, a):
    c, s = math.cos(a), math.sin(a); return [p[0]*c - p[2]*s, p[1], p[0]*s + p[2]*c]
def _rx(p, a):
    c, s = math.cos(a), math.sin(a); return [p[0], p[1]*c + p[2]*s, -p[1]*s + p[2]*c]
def _rz(p, a):
    c, s = math.cos(a), math.sin(a); return [p[0]*c + p[1]*s, -p[0]*s + p[1]*c, p[2]]
# ★ ЭТАЛОННЫЕ D3DX row-vector (канон 0xE, дизасм 0xFF4ADD/BDA/CED; ring0xE_etalon_re.md) — карт-путь колец:
def _rxd(p, a):
    c, s = math.cos(a), math.sin(a); return [p[0], p[1]*c - p[2]*s, p[1]*s + p[2]*c]
def _ryd(p, a):
    c, s = math.cos(a), math.sin(a); return [p[0]*c + p[2]*s, p[1], -p[0]*s + p[2]*c]
def _rzd(p, a):
    c, s = math.cos(a), math.sin(a); return [p[0]*c - p[1]*s, p[0]*s + p[1]*c, p[2]]
def _reflect(p, n):
    if not n or (abs(n[0]) + abs(n[1]) + abs(n[2])) < 1e-6: return list(p)
    d = p[0]*n[0] + p[1]*n[1] + p[2]*n[2]
    return [p[i] - 2.0*d*n[i] for i in range(3)]
def _to_ue(g): return [-g[2], g[0], g[1]]        # game→UE (в коде: UE=(-Gz,Gx,Gy))
def _norm(v):
    L = math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]) or 1.0
    return [v[0]/L, v[1]/L, v[2]/L]
def _ang(a, b):
    d = max(-1.0, min(1.0, a[0]*b[0] + a[1]*b[1] + a[2]*b[2]))
    return math.degrees(math.acos(d))


def _rotchain_mesh12(p, rx, ry, rz, strict):
    """RotChain меш-ветки 0x11/0x12/0x1F (stored-UE оси). strict = FAsdaFxEmitter::bStrictChannelRot."""
    cx, sx = math.cos(rx*D2R), math.sin(rx*D2R)
    cy, sy = math.cos(ry*D2R), math.sin(ry*D2R)
    cz, sz = math.cos(-rz*D2R), math.sin(-rz*D2R)
    if strict:
        p = [p[0]*cx - p[2]*sx, p[1], p[0]*sx + p[2]*cx]
        p = [p[0]*cy + p[1]*sy, -p[0]*sy + p[1]*cy, p[2]]
    else:
        p = [p[0]*cx + p[2]*sx, p[1], -p[0]*sx + p[2]*cx]
        p = [p[0]*cy - p[1]*sy, p[0]*sy + p[1]*cy, p[2]]
    return [p[0], p[1]*cz + p[2]*sz, -p[1]*sz + p[2]*cz]


def _xf_dir(xf, v):
    """Мировое направление через ориентацию актора (масштаб не участвует — сравниваем оси)."""
    u = unreal.MathLibrary.quat_rotate_vector(xf.rotation, unreal.Vector(v[0], v[1], v[2]))
    return [u.x, u.y, u.z]


# ---------- выборка узла трека ----------
def _node_channels(nd):
    r = nd.get_editor_property("ring_rot"); ca = nd.get_editor_property("cone_axis")
    return (float(nd.get_editor_property("ring_roll")), r.x, r.y, r.z, [ca.x, ca.y, ca.z])


def _mid_channels(nodes, frac):
    """Каналы ориентации в доле frac трека. Критично брать НЕСКОЛЬКО точек: у alpenjewels rotY идёт
    0→360, и на КОНЦАХ трека (0°/360°) ориентация совпадает, а в СЕРЕДИНЕ (180°) строгий и исторический
    знак дают разницу 2·180°≡0 — обе точки вырождены. Смену знака вращения (регрессия §H6) ловит
    ЧЕТВЕРТЬ трека: rotY=90° → расхождение ровно 180°."""
    t0 = float(nodes[0].get_editor_property("time"))
    tE = float(nodes[-1].get_editor_property("time"))
    tm = t0 + (tE - t0) * frac
    a, b = nodes[0], nodes[-1]
    for i in range(1, len(nodes)):
        if float(nodes[i].get_editor_property("time")) >= tm:
            a, b = nodes[i-1], nodes[i]; break
    ta = float(a.get_editor_property("time")); tb = float(b.get_editor_property("time"))
    k = 0.0 if tb <= ta else max(0.0, min(1.0, (tm - ta) / (tb - ta)))
    ra, rb = a.get_editor_property("ring_rot"), b.get_editor_property("ring_rot")
    aa, ab = a.get_editor_property("cone_axis"), b.get_editor_property("cone_axis")
    lerp = lambda x, y: x + (y - x) * k
    return (lerp(float(a.get_editor_property("ring_roll")), float(b.get_editor_property("ring_roll"))),
            lerp(ra.x, rb.x), lerp(ra.y, rb.y), lerp(ra.z, rb.z),
            [lerp(aa.x, ab.x), lerp(aa.y, ab.y), lerp(aa.z, ab.z)])


# ---------- итоговый мировой базис одного эмиттера ----------
def emitter_basis(act, xf, em, chans, attached, chiral):
    """(kind, [ex,ey,ez]) — три мировых единичных вектора локального базиса эмиттера.
    Точно повторяет цепочку соответствующей ветки AsdaEffectActor::BuildQuads/SimulateStep."""
    roll, rx, ry, rz, cax = chans
    holder = float(act.get_editor_property("holder_yaw_start")) * D2R   # ElapsedTime=0
    axes = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]

    if em.get_editor_property("ring"):
        # ★ КАНОН 0xE (2026-07-31): карт-путь (не strict, не base_already_ue) = эталон 0x1215C10:
        #   D3DX-ротации, БЕЗ RotY(180°); strict-путь (char/mount) — исторические лямбды + RotY(π).
        strict_bp = bool(em.get_editor_property("strict_ring_base_pos")) or \
                    bool(act.get_editor_property("base_already_ue"))
        out = []
        for e in axes:
            if strict_bp:
                g = _ry(e, roll*D2R)
                g = _rx(g, rx*D2R); g = _ry(g, ry*D2R); g = _rz(g, rz*D2R)
                g = _ry(g, math.pi)                   # швартовка (истор. соулмейт/char-mount)
                g = _ry(g, holder)
            else:
                g = _ryd(e, roll*D2R)
                g = _rxd(g, rx*D2R); g = _ryd(g, ry*D2R); g = _rzd(g, rz*D2R)
                g = _ryd(g, holder)                   # N48
            g = _reflect(g, chiral)
            out.append(_norm(_xf_dir(xf, _to_ue(g))))
        return "ring", out

    if em.get_editor_property("mesh_emitter"):
        strict = bool(em.get_editor_property("strict_channel_rot"))
        out = [_norm(_xf_dir(xf, _rotchain_mesh12(e, rx, ry, rz, strict))) for e in axes]
        return ("mesh12-strict" if strict else "mesh12"), out

    if em.get_editor_property("single_quad"):
        inv = -1.0 if act.get_editor_property("invert_node_rot") else 1.0
        out = []
        for e in axes:
            g = _ry(e, roll*D2R)
            g = _rx(g, rx*D2R*inv); g = _ry(g, ry*D2R*inv); g = _rz(g, rz*D2R*inv)
            if attached: g = _ry(g, math.pi)          # швартовка attach-квада (соосность с кольцами)
            g = _ry(g, holder)
            g = _reflect(g, chiral)
            out.append(_norm(_xf_dir(xf, _to_ue(g))))
        return "quad", out

    # частицы: ориентация = ось конуса (в узле уже UE-оси); bAttachedMap → ось в мир-осях
    world = cax if attached else _xf_dir(xf, cax)
    return "cone", [_norm(world), [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]


# ---------- ОРАКУЛ: ожидаемая ориентация актора прямо из данных игры ----------
_C  = [0, 0, -1, 1, 0, 0, 0, 1, 0]
_CT = [0, 1, 0, 0, 0, 1, -1, 0, 0]
def _mm(a, b):
    o = [0.0]*9
    for i in range(3):
        for j in range(3):
            o[i*3+j] = a[i*3]*b[j] + a[i*3+1]*b[3+j] + a[i*3+2]*b[6+j]
    return o
def oracle_axes(m):
    """Мировые оси актора (X=fwd, Y=rgt, Z=up) из авторской матрицы .jeb-размещения — то же,
    что делает asda_place_effects.place_transform (C·Aᵀ·Cᵀ, столбцы нормированы)."""
    AT = [m[0], m[4], m[8], m[1], m[5], m[9], m[2], m[6], m[10]]
    R = _mm(_mm(_C, AT), _CT)
    return [_norm([R[0], R[3], R[6]]), _norm([R[1], R[4], R[7]]), _norm([R[2], R[5], R[8]])]


def load_oracle(map_name, names):
    """{метка актора: [ex,ey,ez]} — ожидаемые мировые оси по данным игры."""
    p = os.path.join(RAW_DIR, map_name, "effects.json")
    if not os.path.isfile(p):
        log("оракул недоступен: нет %s" % p); return {}
    data = json.load(open(p, encoding="utf-8"))
    out = {}; cnt = {}
    for pl in data["placements"]:
        nm = pl["name"]
        if not data["jeb"].get(nm): continue          # без .jeb актор не ставится — счётчик не двигаем
        cnt[nm] = cnt.get(nm, 0) + 1
        if nm not in names: continue
        use_pmat = pl.get("type") == 1 and pl.get("attached")
        out["MapEffect_%s_%02d" % (nm, cnt[nm])] = oracle_axes(pl["pmat"] if use_pmat else pl["matrix"])
    return out


def runtime_sections(eas, a, xf, ems, twin_hook=None):
    """(3) РАНТАЙМ: спавн двойника → PMC-секции ТЕКУЩЕЙ DLL → центроид+габариты детерминированных секций.
    ems — эмиттеры, прочитанные с актора (при контрольной порче — уже испорченная КОПИЯ, актор не трогается).
    twin_hook(a, ems) -> ems — только для контрольной порчи (ASDA_FX_CONTROL): подмена данных ДВОЙНИКА."""
    b = eas.spawn_actor_from_class(unreal.AsdaEffectActor, xf.translation)
    b.set_actor_transform(xf, False, False)
    # ★ 27.09.2026 (волна 0 графики): python-имена bool-полей UE — БЕЗ префикса «b» (bBaseAlreadyUE →
    #   base_already_ue). Прежде здесь стояли «b_base_already_ue»/«b_invert_node_rot»/«b_flip_quad_v»:
    #   set_editor_property молча падал в except, и двойник собирался БЕЗ этих флагов — то есть рантайм-
    #   секции мерили не ту ветку, что рисуется у настоящего актора. Ошибка теперь не глотается молча.
    for pn in ("chiral_normal", "holder_yaw_start", "holder_yaw_speed", "holder_pos_vel",
               "base_already_ue", "effect_emitter_scale", "invert_node_rot", "flip_quad_v"):
        try: b.set_editor_property(pn, a.get_editor_property(pn))
        except Exception as ex: log("двойник: поле %s не скопировано: %s" % (pn, ex))
    if twin_hook: ems = twin_hook(a, ems)
    b.set_editor_property("emitters", ems)
    pmc = b.get_component_by_class(unreal.ProceduralMeshComponent)
    out = {}
    for i in range(len(ems)):
        em = ems[i]
        det = em.get_editor_property("ring") or em.get_editor_property("mesh_emitter") or \
              (em.get_editor_property("single_quad") and em.get_editor_property("billboard_mode") == 0)
        if not det: continue
        try:
            r = unreal.ProceduralMeshLibrary.get_section_from_procedural_mesh(pmc, i)
            V = r[0]
        except Exception:
            continue
        if not V: continue
        c = [0.0, 0.0, 0.0]; lo = [1e18]*3; hi = [-1e18]*3
        for v in V:
            for j, x in enumerate((v.x, v.y, v.z)):
                c[j] += x/len(V); lo[j] = min(lo[j], x); hi[j] = max(hi[j], x)
        out[str(i)] = {"n": len(V), "c": [round(x, 2) for x in c],
                       "ext": [round(hi[j]-lo[j], 2) for j in range(3)]}
    eas.destroy_actor(b)
    return out


def _map_asset_name(name):
    """Имя карты → имя ассета уровня (★ ревизия 15.09: единое правило реестра карт; было сырое имя — «Mountain Flamion» промахивался)."""
    here = os.path.dirname(os.path.abspath(__file__))
    if here not in sys.path:
        sys.path.insert(0, here)
    import asda_maps
    return asda_maps.map_asset_name(name)


# ---------- КОНТРОЛЬНАЯ ПОРЧА (волна 0 графики, 27.09.2026) ----------
# Закон [[pixel-metric-needs-control-pairs]]: проверка, которую ни разу не видели «красной» на заведомо
# сломанном входе, ничего не доказывает. ASDA_FX_CONTROL=<список через запятую> портит В ПАМЯТИ КОПИЮ
# данных одного репера СРАЗУ ПОСЛЕ ЧТЕНИЯ с актора — дальше порча идёт ВСЕМ настоящим путём проверки
# (базисы, двойник на текущей DLL, оракул). Актор уровня не трогается, save нет.
#   data  — eff_rainbow_a_00: у кольца 0xE ко всем узлам rotY += 90° (порча запечённых данных узлов)
#           → обязаны сработать (2) СНИМОК ДАННЫХ и (3) РАНТАЙМ;
#   twin  — eff_alpenredlight_a_00: у ДВОЙНИКА взводится strict_ring_base_pos (другая C++-ветка колец,
#           имитация правки C++-математики) → обязан сработать ТОЛЬКО (3) РАНТАЙМ;
#   place — eff_alpenjewels_a_00: трансформ размещения повёрнут на 90° по yaw → обязан сработать (1) ОРАКУЛ
#           (и (2): оси эмиттеров едут вместе с актором).
#   all   — все три сразу. При ASDA_FX_CONTROL снимок-эталон НЕ записывается ни при каких условиях.
CONTROL = set(x.strip() for x in os.environ.get("ASDA_FX_CONTROL", "").split(",") if x.strip())
if "all" in CONTROL: CONTROL = {"data", "twin", "place"}


def _control_corrupt(stem, xf, ems):
    """(xf, ems, twin_hook) — испорченные КОПИИ для контрольного прогона; для остальных реперов — как есть."""
    twin_hook = None
    if "data" in CONTROL and stem == "eff_rainbow_a_00":
        out = []
        for em in ems:
            if em.get_editor_property("ring"):
                nn = []
                for nd in em.get_editor_property("nodes"):
                    r = nd.get_editor_property("ring_rot")
                    nd.set_editor_property("ring_rot", unreal.Vector(r.x, r.y + 90.0, r.z)); nn.append(nd)
                em.set_editor_property("nodes", nn)
            out.append(em)
        ems = out; log("КОНТРОЛЬ data: %s — кольцам rotY += 90°" % stem)
    if "twin" in CONTROL and stem == "eff_alpenredlight_a_00":
        def twin_hook(_a, e_in):
            out = []
            for em in e_in:
                if em.get_editor_property("ring"):
                    em.set_editor_property("strict_ring_base_pos", not em.get_editor_property("strict_ring_base_pos"))
                out.append(em)
            log("КОНТРОЛЬ twin: %s — у двойника переключён strict_ring_base_pos" % stem)
            return out
    if "place" in CONTROL and stem == "eff_alpenjewels_a_00":
        rot = unreal.MathLibrary.compose_rotators(xf.rotation.rotator(), unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0))
        xf = unreal.Transform(xf.translation, rot, xf.scale3d)
        log("КОНТРОЛЬ place: %s — размещение повёрнуто на 90° по yaw" % stem)
    return xf, ems, twin_hook


def snapshot_map(map_name, names):
    _mn = _map_asset_name(map_name)
    lvl = "/Game/Map/%s/%s" % (_mn, _mn)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not unreal.EditorAssetLibrary.does_asset_exist(lvl):
        log("НЕТ уровня %s" % lvl); return {}
    les.load_level(lvl)
    out = {}
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.AsdaEffectActor): continue
        lbl = a.get_actor_label()
        if not lbl.startswith("MapEffect_"): continue
        base = lbl[len("MapEffect_"):]
        stem = base.rsplit("_", 1)[0] if base.rsplit("_", 1)[-1].isdigit() else base
        if stem not in names: continue
        xf = a.get_actor_transform()
        ems = a.get_editor_property("emitters")      # КОПИЯ массива (python) — актор уровня не меняется
        twin_hook = None
        if CONTROL:
            xf, ems, twin_hook = _control_corrupt(stem, xf, ems)
        cn = a.get_editor_property("chiral_normal"); chiral = [cn.x, cn.y, cn.z]
        attached = (a.get_editor_property("follow_actor") is not None) or \
                   (a.root_component.get_attach_parent() is not None)
        q = xf.rotation
        act_axes = [[round(c, 6) for c in _norm(_xf_dir(xf, e))]
                    for e in ([1, 0, 0], [0, 1, 0], [0, 0, 1])]
        rec = {
            "loc": [round(xf.translation.x, 3), round(xf.translation.y, 3), round(xf.translation.z, 3)],
            "rot": [round(q.rotator().pitch, 4), round(q.rotator().yaw, 4), round(q.rotator().roll, 4)],
            "scale": [round(xf.scale3d.x, 5), round(xf.scale3d.y, 5), round(xf.scale3d.z, 5)],
            "chiral": [round(c, 5) for c in chiral], "attached": attached,
            "actor_axes": act_axes, "emitters": [],
            "runtime_sections": runtime_sections(eas, a, xf, ems, twin_hook),
        }
        for em in ems:
            nodes = em.get_editor_property("nodes")
            if not nodes: continue
            # t¼ и t¾ обязательны: только на четвертях трека расхождение знака вращения не вырождено
            for tag in ("t0", "t1/4", "t1/2", "t3/4", "tE"):
                chans = _node_channels(nodes[0]) if tag == "t0" else (
                        _node_channels(nodes[-1]) if tag == "tE" else
                        _mid_channels(nodes, {"t1/4": 0.25, "t1/2": 0.5, "t3/4": 0.75}[tag]))
                kind, bas = emitter_basis(a, xf, em, chans, attached, chiral)
                rec["emitters"].append({
                    "name": str(em.get_editor_property("name")),
                    "type": "0x%X" % em.get_editor_property("jeb_type"), "kind": kind, "node": tag,
                    "strict_rot": bool(em.get_editor_property("strict_channel_rot")),
                    "strict_orbit": bool(em.get_editor_property("strict_quad_orbit")),
                    "strict_pos": bool(em.get_editor_property("strict_mesh_pos")),
                    "skin_bones": int(em.get_editor_property("skin_bones")),
                    "basis": [[round(c, 6) for c in v] for v in bas],
                })
        out[lbl] = rec
    return out


def main():
    snap = {}; oracle_bad = 0; oracle_total = 0; oracle_worst = 0.0
    for mp, names in REPERS.items():
        nameset = set(names)
        snap[mp] = snapshot_map(mp, nameset)
        log("карта %s: реперных акторов %d" % (mp, len(snap[mp])))
        orc = load_oracle(mp, nameset)
        for lbl, axes in orc.items():
            rec = snap[mp].get(lbl)
            if not rec: log("ОРАКУЛ: актора %s НЕТ в уровне" % lbl); oracle_bad += 1; continue
            for ax in range(3):
                oracle_total += 1
                d = _ang(axes[ax], rec["actor_axes"][ax])
                oracle_worst = max(oracle_worst, d)
                if d > TOL_DEG:
                    oracle_bad += 1
                    log("ОРАКУЛ ≠ УРОВЕНЬ %s ось%d: %.2f°  данные игры=%s уровень=%s" %
                        (lbl, ax, d, [round(x, 3) for x in axes[ax]],
                         [round(x, 3) for x in rec["actor_axes"][ax]]))
    log("--- ОРАКУЛ (ориентация акторов против авторских матриц effects.json): осей %d, "
        "расхождений >%.1f° — %d, максимум %.4f°" % (oracle_total, TOL_DEG, oracle_bad, oracle_worst))

    # ★ 27.09.2026: текущий снимок можно выгрузить в отдельный файл (разбор расхождений без перезаписи эталона)
    cur_out = os.environ.get("ASDA_FX_CUR_OUT", "")
    if cur_out:
        json.dump(snap, open(cur_out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        log("текущий снимок выгружен: %s" % cur_out)
    if CONTROL:
        log("КОНТРОЛЬНЫЙ ПРОГОН: порча %s — эталон НЕ перезаписывается" % sorted(CONTROL))
    elif os.environ.get("ASDA_FX_SNAPSHOT") == "1" or not os.path.isfile(BASE):
        # ★ 27.09.2026: прежний эталон не теряем — копия рядом с датой (перефиксация 27.09 и любая следующая)
        if os.path.isfile(BASE):
            import shutil, time as _t
            bak = BASE + ".bak_" + _t.strftime("%Y%m%d_%H%M%S")
            shutil.copy2(BASE, bak); log("прежний эталон сохранён: %s" % bak)
        json.dump(snap, open(BASE, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        log("ЭТАЛОННЫЙ СНИМОК ЗАПИСАН: %s" % BASE)
        log("ВЕРДИКТ ОРАКУЛА: %s" % ("РАСХОЖДЕНИЕ" if oracle_bad else "ОК"))
        return
    if os.environ.get("ASDA_FX_SNAPSHOT") == "1":
        log("ASDA_FX_SNAPSHOT=1 проигнорирован: идёт контрольный прогон")
    base = json.load(open(BASE, encoding="utf-8"))
    worst = 0.0; bad = 0; total = 0; miss = 0; rt_worst = 0.0
    for mp, actors in base.items():
        cur = snap.get(mp, {})
        for lbl, rec in actors.items():
            c = cur.get(lbl)
            if not c:
                log("ПРОПАЛ актор %s" % lbl); miss += 1; continue
            dl = max(abs(rec["loc"][i] - c["loc"][i]) for i in range(3))
            if dl > 1.0:
                bad += 1   # ★ 27.09.2026: сдвиг размещения — тоже расхождение (прежде только печатался)
                log("СДВИГ %s: Δpos=%.2f юн" % (lbl, dl))
            for i, e in enumerate(rec["emitters"]):
                if i >= len(c["emitters"]):
                    log("НЕТ эмиттера %d у %s" % (i, lbl)); bad += 1; continue
                ce = c["emitters"][i]
                for ax in range(3):
                    a, b = e["basis"][ax], ce["basis"][ax]
                    if sum(abs(x) for x in a) < 1e-6: continue
                    total += 1
                    d = _ang(a, b); worst = max(worst, d)
                    if d > TOL_DEG:
                        bad += 1
                        log("РАСХОЖДЕНИЕ %s / %s(%s,%s) ось%d: %.2f°  эталон=%s текущ=%s" %
                            (lbl, e["name"], e["type"], e["node"], ax, d,
                             [round(x, 3) for x in a], [round(x, 3) for x in b]))
            # (3) РАНТАЙМ-СЕКЦИИ: центроид/габариты детерминированной геометрии ТЕКУЩЕЙ DLL
            for si, sb in rec.get("runtime_sections", {}).items():
                sc = c.get("runtime_sections", {}).get(si)
                total += 1
                if not sc or sc["n"] != sb["n"]:
                    bad += 1; log("РАНТАЙМ %s sec%s: топология %s -> %s" % (lbl, si, sb["n"], sc and sc["n"]))
                    continue
                dc = math.sqrt(sum((sb["c"][j]-sc["c"][j])**2 for j in range(3)))
                de = max(abs(sb["ext"][j]-sc["ext"][j]) for j in range(3))
                rt_worst = max(rt_worst, dc, de)   # юниты — отдельный счёт, не в градусах
                if dc > 0.5 or de > 0.5:
                    bad += 1
                    log("РАНТАЙМ %s sec%s: Δцентроид=%.1f юн, Δгабарит=%.1f юн (эталон c=%s, текущ c=%s)" %
                        (lbl, si, dc, de, sb["c"], sc["c"]))
            # ★ 27.09.2026: секция, которой НЕ БЫЛО в эталоне, — тоже изменение топологии (прежде не видели)
            for si in c.get("runtime_sections", {}):
                if si not in rec.get("runtime_sections", {}):
                    total += 1; bad += 1
                    log("РАНТАЙМ %s sec%s: новая секция (в эталоне её не было)" % (lbl, si))
        # ★ 27.09.2026: актор, появившийся в уровне, но отсутствующий в эталоне, — сообщаем
        for lbl in cur:
            if lbl not in actors:
                miss += 1; log("НОВЫЙ реперный актор %s (в эталоне его нет)" % lbl)
    log("=== СНИМОК: осей и секций сверено %d, расхождений — %d, максимум по осям %.4f°, по секциям %.2f юн, "
        "пропавших/новых акторов %d" % (total, bad, worst, rt_worst, miss))
    if CONTROL:
        log("КОНТРОЛЬ: порча %s должна дать РЕГРЕССИЮ; если ниже «ОК» — проверка СЛЕПА" % sorted(CONTROL))
    log("ВЕРДИКТ: %s" % ("РЕГРЕССИЯ" if (bad or miss or oracle_bad) else
                         "ОК — ориентация эффектов карт не изменилась (все три оси)"))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
