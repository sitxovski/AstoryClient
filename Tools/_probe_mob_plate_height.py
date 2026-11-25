# Copyright (c) 2026 @sitxovski (Discord)
# _probe_mob_plate_height.py — ЗАМЕР высоты плашки имени МОБА (до/после фикса) против эталона.
#
# ЭТАЛОН (реверс 2026-08-03, дизасм + декомпил):
#   • Спавн моба Recv_MobSpawn@0xF305E0 (network.c:37194):
#       v6 = sub_930AC5(g_pMobMgr, out, mob[117]);   → jmp 0xF31080:
#           rec = *(mgr+0xC) + typeIdx*0x110;  out[0..2] = *(float3*)(rec + 0x84)   ← ПОЛУ-РАЗМЕР AABB
#       mob[106..108] (+424/+428/+432) = out          ← half (X,Y,Z)
#       mob[109..111] (+436/+440/+444) = mob[84..86]  ← центр = мировая позиция
#       *(float*)(mob+440) += *(float*)(mob+428)      ← центр.Y += halfY
#   • Плашка: helper_getSumF(entity+424) = centerY + halfY, т.е. ВЕРХ AABB = земля + 2·halfY;
#     к нему прибавляется +50.0 (game_main.c:4541 hover-плашка, 4750 плашка выбранной цели).
#   ⇒ H(плашка над origin) = 2 · halfY(Mob list.BIN +0x84 +4) + 50
#
# Печатает по каждому типу моба карты: габариты меша, капсулу пикинга, ФАКТИЧЕСКИЙ локальный Z плашки
# и эталонное значение; в конце — сводка расхождений.
#   Запуск: Tools\run_headless.bat Tools\_probe_mob_plate_height.py
import unreal, struct, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MOB_LIST = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\Mob\Mob_list.bin"
MAPS = ["/Game/Map/alpen/alpen"]
PLAYER_LEVEL = 10   # «уровень игрока» для проверки лесенки цвета (в игре берётся из сети)
HDR, STRIDE = 0x14, 0x110


def log(m):
    unreal.log_warning("[mob-plate] %s" % m)


def lin2srgb(v):
    """linear → sRGB (проверка закона [[ui-srgb-gamma-rule]]: в таблице цвет sRGB, в UE — linear)."""
    v = max(0.0, min(1.0, v))
    return v * 12.92 if v <= 0.0031308 else 1.055 * (v ** (1.0 / 2.4)) - 0.055


def load_mob_list():
    """MobId(1-based) -> (модель, имя, halfY, level, aggression, rank)."""
    b = open(MOB_LIST, "rb").read()
    cnt = struct.unpack_from("<I", b, 4)[0]
    out = {}
    for i in range(cnt):
        r = HDR + i * STRIDE
        if r + STRIDE > len(b):
            break
        model = b[r + 0x04:r + 0x24].split(b"\0")[0].decode("cp1251", "ignore")
        name = b[r + 0x64:r + 0x82].split(b"\0")[0].decode("cp1251", "ignore")
        hx, hy, hz = struct.unpack_from("<3f", b, r + 0x84)
        lvl = struct.unpack_from("<i", b, r + 0x98)[0]
        out[i + 1] = (model, name, hy, lvl, b[r + 0xFC], b[r + 0xF8])
    return out


if __name__ == "__main__":   # ★ без побочных эффектов при импорте (правило проекта)
    TBL = load_mob_list()
    log("Mob list.bin: %d записей" % len(TBL))

    worst = []
    for mp in MAPS:
        unreal.EditorLoadingAndSavingUtils.load_map(mp)
        w = unreal.EditorLevelLibrary.get_editor_world()
        cls = unreal.load_class(None, "/Script/AsdaReborn.AsdaMobActor")
        acts = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
        log("=== %s: акторов мобов = %d ===" % (mp, len(acts)))
        log("%-5s %-24s %-22s %8s %8s %8s %9s %9s %8s %4s %4s" % (
            "id", "имя", "модель", "extZ", "capHalf", "верхКапс", "плашкаZ", "эталонH", "дельта",
            "lvl", "agg"))   # + дельта уровней и цвет имени
        seen = set()
        for a in acts:
            mid = a.get_editor_property("MobId")
            if mid in seen:
                continue
            seen.add(mid)
            m = a.get_editor_property("Mesh")
            cap = a.get_editor_property("PickHitbox")
            np_ = a.get_editor_property("Nameplate")
            sc = a.get_actor_scale3d().z
            extz = -1.0
            if m:
                b = m.get_editor_property("skeletal_mesh_asset")
                if b:
                    extz = b.get_bounds().box_extent.z * sc * m.get_editor_property("relative_scale3d").z
            ch = cap.get_scaled_capsule_half_height() if cap else -1.0
            cz = cap.get_editor_property("relative_location").z * sc if cap else 0.0
            top = cz + ch
            pz = np_.get_editor_property("relative_location").z * sc if np_ else -1.0
            model, nm, hy, lvl, agg, rank = TBL.get(mid, ("?", "?", -1.0, -1, 0, 0))
            eh = 2.0 * hy + 50.0 if hy > 0 else -1.0
            # ★ Цвет имени по разнице уровней — через ТУ ЖЕ рантайм-функцию, что и в игре.
            try:
                c, has, diff = unreal.AsdaNameplateComponent.resolve_mob_name_color(lvl, PLAYER_LEVEL, agg)
                # linear → sRGB (обратно к константе эталона): проверяем, что закон гаммы соблюдён.
                hexc = "#" + "".join("%02X" % round(255.0 * lin2srgb(v)) for v in (c.r, c.g, c.b))
            except Exception as ex:
                hexc, diff, has = "err:%s" % ex, 0, False
            log("%-5d %-24s %-22s %8.1f %8.1f %8.1f %9.1f %9.1f %8.1f %4d %4d  d=%-4d %s%s" % (
                mid, nm[:24], model[:22], extz, ch, top, pz, eh, pz - eh, lvl, agg,
                diff, hexc, "" if has else " (нет правила)"))
            if eh > 0:
                worst.append((abs(pz - eh), mid, nm, pz, eh))
    worst.sort(reverse=True)
    log("--- ТОП-10 расхождений (|факт − эталон|) ---")
    for d, mid, nm, pz, eh in worst[:10]:
        log("  id=%-5d %-26s факт=%8.1f эталон=%8.1f дельта=%8.1f" % (mid, nm[:26], pz, eh, pz - eh))
    if worst:
        log("СРЕДНЕЕ |дельта| = %.1f по %d типам" % (sum(x[0] for x in worst) / len(worst), len(worst)))

    # ── ПОЛНАЯ ЛЕСЕНКА ЦВЕТА (синтетика): на карте alpen встречаются не все ветки ──
    #   Ожидание = константы эталона (sRGB) из game_main.c:4544/4763.
    EXPECT = {}
    for d in range(-12, 13):
        if d >= 7:               EXPECT[d] = "#808080"
        elif 2 <= d <= 6:        EXPECT[d] = "#00FF00"
        elif -1 <= d <= 1:       EXPECT[d] = "#FFFFFF"
        elif -5 <= d <= -2:      EXPECT[d] = "#FFA200"
        else:                    EXPECT[d] = "#FF0000"
    log("--- ЛЕСЕНКА ЦВЕТА (агрессия 1/4/5): дельта → цвет ---")
    bad = 0
    for aggr in (1, 4, 5):
        line = []
        for d in range(-12, 13):
            c, has, dd = unreal.AsdaNameplateComponent.resolve_mob_name_color(50, 50 + d, aggr)
            hexc = "#" + "".join("%02X" % round(255.0 * lin2srgb(v)) for v in (c.r, c.g, c.b))
            if not has or hexc != EXPECT[d] or dd != d:
                bad += 1
                line.append("%d:%s!=%s" % (d, hexc, EXPECT[d]))
            else:
                line.append("%d:%s" % (d, hexc))
        log("  агрессия %d: %s" % (aggr, " ".join(line)))
    for aggr, exp in ((2, "#E24ACB"), (3, "#C3A868")):
        c, has, dd = unreal.AsdaNameplateComponent.resolve_mob_name_color(50, 1, aggr)
        hexc = "#" + "".join("%02X" % round(255.0 * lin2srgb(v)) for v in (c.r, c.g, c.b))
        ok = has and hexc == exp
        bad += 0 if ok else 1
        log("  агрессия %d (ФИКС, дельта не влияет): %s ожидалось %s — %s" % (aggr, hexc, exp, "OK" if ok else "ОШИБКА"))
    c, has, dd = unreal.AsdaNameplateComponent.resolve_mob_name_color(50, 50, 0)
    log("  агрессия 0 (вне 1..5): правило %s — эталон цвет НЕ трогает" % ("сработало (ОШИБКА)" if has else "не сработало (OK)"))
    if has:
        bad += 1
    log("ЛЕСЕНКА: расхождений %d" % bad)
    log("ГОТОВО")
