# Copyright (c) 2026 @sitxovski (Discord)
# _probe_fx_flags.py — приёмка изоляции флагов эффектов (регрессия ориентации карт 2026-07-30).
# Печатает по каждому семейству эффектов: какие «строгие» флаги стоят и НАСКОЛЬКО формулы расходятся
# в юнитах/градусах. Цель — доказать числами: карты вернулись на историческое поведение, а крылья /
# кольцо соулмейта / шлейф транспорта сохранили свои исправления.
# Запуск: run_headless.bat Tools\_probe_fx_flags.py
import os, sys, math, json
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

D2R = math.pi / 180.0
def log(m): unreal.log_warning("[fxflags] %s" % m)


def rotchain(p, rx, ry, rz, strict):
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


def mesh_rows(em, tag):
    """Разбор меш-эмиттера: флаги + расхождение двух формул pos-канала (юниты локали эффекта)."""
    nodes = em.get_editor_property("nodes")
    if not nodes: return None
    nd = nodes[-1]
    r = nd.get_editor_property("ring_rot"); a = nd.get_editor_property("cone_axis")
    P = nd.get_editor_property("ring_pos_ofs")
    # ring_pos_ofs хранится в game-осях → в stored-UE как в C++ (PosLocalUE)
    pue = [-P.z, P.x, P.y]
    S = [a.z or 1.0, a.x or 1.0, a.y or 1.0]                       # SclUE = (sz,sx,sy)
    strict_pos = bool(em.get_editor_property("strict_mesh_pos"))
    strict_rot = bool(em.get_editor_property("strict_channel_rot"))
    # Δ = |strict − историческое| для точки pos: RotChain(pos·S) против pos
    v = rotchain([pue[i]*S[i] for i in range(3)], r.x, r.y, r.z, strict_rot)
    d = math.sqrt(sum((v[i]-pue[i])**2 for i in range(3)))
    # Δ ориентации между строгим и историческим знаком вращений (ось X эмиттера), градусы
    A = rotchain([1, 0, 0], r.x, r.y, r.z, True)
    B = rotchain([1, 0, 0], r.x, r.y, r.z, False)
    dot = max(-1.0, min(1.0, sum(A[i]*B[i] for i in range(3))))
    return (tag, str(em.get_editor_property("name")), "0x%X" % em.get_editor_property("jeb_type"),
            strict_rot, strict_pos, round(d, 3), round(math.degrees(math.acos(dot)), 2),
            [round(r.x, 1), round(r.y, 1), round(r.z, 1)], int(em.get_editor_property("skin_bones")))


def probe_bp(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path): return []
    bp = unreal.EditorAssetLibrary.load_asset(path)
    cdo = unreal.get_default_object(bp.generated_class())
    out = []
    for em in cdo.get_editor_property("emitters"):
        if em.get_editor_property("mesh_emitter"):
            r = mesh_rows(em, os.path.basename(path))
            if r: out.append(r)
        elif em.get_editor_property("single_quad"):
            nodes = em.get_editor_property("nodes")
            if not nodes: continue
            po = nodes[-1].get_editor_property("ring_pos_ofs")
            rr = nodes[-1].get_editor_property("ring_rot")
            out.append((os.path.basename(path), str(em.get_editor_property("name")),
                        "0x%X" % em.get_editor_property("jeb_type"), None,
                        bool(em.get_editor_property("strict_quad_orbit")),
                        round(math.sqrt(po.x*po.x + po.y*po.y + po.z*po.z), 2),
                        round(rr.y, 1), "квад-орбита", 0))
    return out


def main():
    log("=== КРЫЛЬЯ (attach-10): строгий знак вращений + pos через полную W должны быть ВКЛ ===")
    for n in ("BP_Fx_eff_wing_m_ss_00", "BP_Fx_eff_wing_m_big_00", "BP_Fx_eff_wing_f_big_00",
              "BP_Fx_eff_wing_f_mid_00"):
        for r in probe_bp("/Game/AsdaAssets/Effects/" + n):
            log("  %-28s %-12s %s strictRot=%s strictPos=%s Δpos=%s юн Δrot=%s° rot=%s скин=%d" % r)

    log("=== КОЛЬЦО СОУЛМЕЙТА (0xC биллборд-квады): строгая орбита должна быть ВКЛ ===")
    for n in ("BP_Fx_eff_1_char_99_soullink01_a_00", "BP_Fx_eff_1_char_99_soulmatec_a_01"):
        rows = probe_bp("/Game/AsdaAssets/Effects/" + n)
        orb = sum(1 for r in rows if r[4] is True)
        log("  %-40s квад-эмиттеров %d, из них strictOrbit=True — %d, |pos|=%s rotY=%s" %
            (n, len(rows), orb, rows[0][5] if rows else "-", rows[0][6] if rows else "-"))

    log("=== ШЛЕЙФ ТРАНСПОРТА (attach-6): posBase в UE-осях (50,0,85) = 50 ПОЗАДИ по курсу, 85 ВЫШЕ ===")
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    best = []
    for ad in ar.get_assets_by_path("/Game/AsdaAssets/Effects", recursive=False):
        n = str(ad.asset_name)
        if not n.startswith("BP_Fx_itm1_n_96_vehicle"): continue
        cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_asset(
            "/Game/AsdaAssets/Effects/" + n).generated_class())
        for em in cdo.get_editor_property("emitters"):
            pb = em.get_editor_property("pos_base")
            m = math.sqrt(pb.x*pb.x + pb.y*pb.y + pb.z*pb.z)
            if m > 1.0:
                best.append((round(m, 1), n, str(em.get_editor_property("name")),
                             round(pb.x, 1), round(pb.y, 1), round(pb.z, 1),
                             bool(em.get_editor_property("strict_channel_rot")),
                             bool(em.get_editor_property("strict_mesh_pos"))))
    best.sort(reverse=True)
    if not best:
        log("  у всех шлейф-BP posBase=0 (смещение шлейфа задаёт не эмиттер, а точка спавна M_DUMMY)")
    for r in best[:8]:
        log("  |posBase|=%-7s %-32s %-12s (%.1f, %.1f, %.1f) strictRot=%s strictPos=%s" % r)

    log("=== КАРТЫ (alpen, меш-эмиттеры 0x12): ВСЕ строгие флаги должны быть ВЫКЛ ===")
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level("/Game/Map/alpen/alpen")
    nmesh = 0; bad = 0; worst_pos = 0.0; worst_rot = 0.0
    for a in eas.get_all_level_actors():
        if not isinstance(a, unreal.AsdaEffectActor): continue
        if not a.get_actor_label().startswith("MapEffect_"): continue
        for em in a.get_editor_property("emitters"):
            if not em.get_editor_property("mesh_emitter"): continue
            r = mesh_rows(em, a.get_actor_label())
            if not r: continue
            nmesh += 1
            if r[3] or r[4]: bad += 1; log("  ФЛАГ ВКЛ У КАРТЫ: %s" % (r,))
            worst_pos = max(worst_pos, r[5]); worst_rot = max(worst_rot, r[6])
            if r[5] > 0.01 or r[6] > 0.5:
                log("  %-34s %-10s %s Δpos=%s юн Δrot=%s° rot=%s" % (r[0], r[1], r[2], r[5], r[6], r[7]))
    log("  ИТОГ карт: меш-эмиттеров %d, со «строгими» флагами %d (должно быть 0); "
        "максимум Δpos=%.3f юн, Δrot=%.2f° — ровно столько порт УВОДИЛ БЫ карты без флагов"
        % (nmesh, bad, worst_pos, worst_rot))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
