# Copyright (c) 2026 @sitxovski (Discord)
# asda_scan_level_transforms.py — ПОИСК НЕОРТОНОРМИРОВАННЫХ/БИТЫХ ТРАНСФОРМОВ НА УРОВНЯХ.
#
# ★ ЗАЧЕМ. Кук валит один «handled ensure» в FQuat(FMatrix) (Quat.h:789):
#   `FMath::Abs(1 - M.GetScaledAxis(<ось>).SizeSquared()) <= 1e-4` — в конструктор кватерниона
#   пришла матрица, оси которой не единичной длины. Источником может быть трансформ актора или
#   компонента с NaN, нулевым или зеркальным (отрицательным) масштабом, либо такой же трансформ
#   инстанса ISM/HISM.
#
# Скрипт обходит ВСЕ уровни проекта (все .umap в Content) и проверяет:
#   * актор     — трансформ актора;
#   * компонент — относительный трансформ каждого SceneComponent;
#   * ISM/HISM  — трансформ каждого инстанса.
# Признаки: NAN (нечисло/бесконечность), ZERO (компонента масштаба ~0 — матрица вырождена и
# ортонормировать её нельзя), MIRROR (отрицательная компонента масштаба — матрица правой тройки
# становится левой; для FQuat это тоже не ортонормированная матрица после RemoveScaling).
#
# Запуск: headless. ASDA_FIX=1 — чинить (NaN и нулевой масштаб → 1.0), иначе только отчёт.
# Отчёт: ASDA_OUT (по умолчанию @@ASTORY_DEV_ROOT_FWD@@/Reports/level_transform_scan.txt).
import os
import unreal

EPS_ZERO = 1e-6          # ниже этого модуль компоненты масштаба считается нулём
LINES = []


def log(m):
    unreal.log("[xform] " + str(m))
    LINES.append(str(m))


def bad(v):
    """NaN/бесконечность в числе."""
    return v != v or abs(v) > 1e30


def check_scale(s):
    """Список признаков дефекта у вектора масштаба."""
    flags = []
    comps = (s.x, s.y, s.z)
    if any(bad(c) for c in comps):
        flags.append("NAN")
    elif any(abs(c) < EPS_ZERO for c in comps):
        flags.append("ZERO")
    if any(c < 0.0 for c in comps):
        flags.append("MIRROR")
    return flags


def check_vec(v):
    return ["NAN"] if (bad(v.x) or bad(v.y) or bad(v.z)) else []


def check_rot(r):
    return ["NAN"] if (bad(r.roll) or bad(r.pitch) or bad(r.yaw)) else []


def all_levels():
    """Все .umap проекта → пути пакетов /Game/..."""
    content = unreal.Paths.project_content_dir()
    content = unreal.Paths.convert_relative_path_to_full(content).replace("\\", "/").rstrip("/")
    out = []
    for dp, _dn, fn in os.walk(content):
        for f in fn:
            if f.lower().endswith(".umap"):
                p = os.path.join(dp, f).replace("\\", "/")
                out.append("/Game" + p[len(content):-len(".umap")])
    out.sort()
    return out


def scan_level(pkg, do_fix):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not les.load_level(pkg):
        log("!! уровень не открылся: %s" % pkg)
        return 0, 0
    found = fixed = 0
    for actor in eas.get_all_level_actors():
        if actor is None:
            continue
        name = actor.get_path_name()
        t = actor.get_actor_transform()
        flags = check_scale(t.scale3d) + check_vec(t.translation) + check_rot(actor.get_actor_rotation())
        if flags:
            found += 1
            log("АКТОР %s: %s | loc=%s scale=%s" % (name, "+".join(flags), t.translation, t.scale3d))
            if do_fix and ("NAN" in flags or "ZERO" in flags):
                s = t.scale3d
                ns = unreal.Vector(*[1.0 if (bad(c) or abs(c) < EPS_ZERO) else c for c in (s.x, s.y, s.z)])
                actor.set_actor_scale3d(ns)
                fixed += 1

        for comp in actor.get_components_by_class(unreal.SceneComponent):
            rs = comp.get_editor_property("relative_scale3d")
            rl = comp.get_editor_property("relative_location")
            rr = comp.get_editor_property("relative_rotation")
            cflags = check_scale(rs) + check_vec(rl) + check_rot(rr)
            if cflags:
                found += 1
                log("КОМПОНЕНТ %s.%s: %s | loc=%s scale=%s"
                    % (name, comp.get_name(), "+".join(cflags), rl, rs))
                if do_fix and ("NAN" in cflags or "ZERO" in cflags):
                    ns = unreal.Vector(*[1.0 if (bad(c) or abs(c) < EPS_ZERO) else c
                                         for c in (rs.x, rs.y, rs.z)])
                    comp.set_editor_property("relative_scale3d", ns)
                    fixed += 1

            if isinstance(comp, unreal.InstancedStaticMeshComponent):
                n = comp.get_instance_count()
                for i in range(n):
                    res = comp.get_instance_transform(i, False)
                    it = res[1] if isinstance(res, tuple) else res
                    if it is None:
                        continue
                    iflags = check_scale(it.scale3d) + check_vec(it.translation)
                    if iflags:
                        found += 1
                        log("ИНСТАНС %s.%s[%d]: %s | loc=%s scale=%s"
                            % (name, comp.get_name(), i, "+".join(iflags), it.translation, it.scale3d))
                        if do_fix and ("NAN" in iflags or "ZERO" in iflags):
                            s = it.scale3d
                            it.scale3d = unreal.Vector(*[1.0 if (bad(c) or abs(c) < EPS_ZERO) else c
                                                         for c in (s.x, s.y, s.z)])
                            comp.update_instance_transform(i, it, False, True, True)
                            fixed += 1
    if do_fix and fixed:
        les.save_current_level()
    return found, fixed


def main():
    do_fix = os.environ.get("ASDA_FIX", "0") == "1"
    out = os.environ.get("ASDA_OUT", "@@ASTORY_DEV_ROOT_FWD@@/Reports/level_transform_scan.txt")
    levels = all_levels()
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА В Login.umap (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): /Game/Map/Login без
    #   ASDA_ALLOW_LOGIN_WRITE=1 не открывается и не пересохраняется (обход сохранял уровень при найденном). Остальные — как раньше.
    import sys as _sys_lg
    _sys_lg.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # Tools — для asda_login_guard
    import asda_login_guard as _LG
    login_skipped = [l for l in levels if _LG.login_package_blocked(l, "asda_scan_level_transforms.py", quiet=True)]
    if login_skipped:
        levels = [l for l in levels if l not in login_skipped]
        log("[LOGIN-GUARD] [xform] уровней сцены логина пропущено: %d (%s) — не открывались и не сохранялись (только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"
            % (len(login_skipped), ", ".join(login_skipped)))
    log("уровней: %d, режим правки: %s" % (len(levels), do_fix))
    total = totfix = 0
    for i, lv in enumerate(levels):
        f, x = scan_level(lv, do_fix)
        total += f
        totfix += x
        log("--- %s: дефектов %d, исправлено %d  (%d/%d)" % (lv, f, x, i + 1, len(levels)))
        unreal.SystemLibrary.collect_garbage()
    log("ИТОГ: дефектных трансформов %d, исправлено %d" % (total, totfix))
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(LINES))


if __name__ == "__main__":
    main()
