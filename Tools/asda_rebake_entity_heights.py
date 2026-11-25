# Copyright (c) 2026 @sitxovski (Discord)
# asda_rebake_entity_heights.py — ПЕРЕЗАПЕКАНИЕ ВЫСОТЫ уже размещённых в уровнях NPC/мобов
#   под новый (правильный) источник высоты — логическую сетку <map>.TER.
#
# ЗАЧЕМ: позиции NPC/мобов запекались сэмплом по РЕНДЕР-мешу террейна (старый asda_build_*_manifest.py).
#   Там, где над оврагом стоит меш-площадка/мост, «пол» запечён в .TER, а рендер-террейн уходит вниз —
#   сущности оказывались под видимым полом (до 1116 юнитов, см. Reports/rebake_entity_heights.md).
#   Рантайм-высота (AsdaGround::GroundZ) уже читает сетку; запечённые позиции — нет.
#
# ПОЧЕМУ НЕ ПЕРЕЗАПУСК asda_place_npcs/mobs: те скрипты СНОСЯТ и ПЕРЕСОЗДАЮТ акторов — любые ручные
#   правки в уровне были бы потеряны (грабли asda_place_effects/tower). Здесь акторы НЕ пересоздаются:
#   правится ТОЛЬКО Z-компонента location, XY не трогается вообще (ассерт ниже это проверяет).
#
# ИСТОЧНИК ВЫСОТЫ: сам боевой ассет /Game/Map/<map>/DA_WalkGrid (UAsdaWalkGrid::GetGroundY) — то же,
#   что видит игра в рантайме. Питон-реализация asda_ter_height используется как независимая сверка.
# Игровые координаты актора берутся из его же свойств tile_x/tile_z (мир = тайл*100+50) — ровно как
#   их писали place-скрипты; заодно это проверка, что XY актора не разъехались с тайлом.
#
# Режим: ASDA_DRYRUN=1 — только отчёт, уровни не сохраняются. ASDA_MAPS="alpen||rein" — подмножество карт.
# Запуск: run_headless.bat asda_rebake_entity_heights.py
import os, re, sys, unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ter_height as TERH          # чистый питон, без побочных эффектов при импорте
import os as _os, sys as _sys
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина

DRY = os.environ.get("ASDA_DRYRUN", "") == "1"
EPS = 1.0                                # порог «значимого» расхождения, юниты
EAL = unreal.EditorAssetLibrary

# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (все 22 карты порта).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
ALL_MAPS = _MAPS.ALL_MAPS

def log(m): unreal.log("[rebake] " + str(m))

asset_name = _MAPS.map_asset_name  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)

def grid_y(da, gx, gz):
    """Высота из боевого ассета DA_WalkGrid.
    NB: UE-Python для UFUNCTION `bool GetGroundY(.., float& OutY)` возвращает ТОЛЬКО out-параметр
        (float), bool-возврат отбрасывается — проверено пробой (_probe_walkgrid_gy.py)."""
    r = da.get_ground_y(float(gx), float(gz))
    if isinstance(r, (tuple, list)):        # на случай иной биндинг-схемы движка
        return float(r[1]) if r[0] else None
    return float(r)

def main():
    only = os.environ.get("ASDA_MAPS", "")
    maps = [m.strip() for m in only.split("||")] if only else ALL_MAPS
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    grand = {"ent": 0, "fixed": 0, "maps": 0}
    gmax = 0.0; gmax_who = ""
    rows = []
    for mp in maps:
        an = asset_name(mp)
        lvl = _U.level_path(mp)
        wg  = "/Game/Map/%s/DA_WalkGrid" % an
        # ★ 16.09.2026 (обратный обход ревизии savedir14): инструмент двигает акторов в уровне и
        #   СОХРАНЯЕТ его — гейт сцены логина ДО load_level. Проверяется КОРЕНЬ карты: под ним и сам
        #   уровень, и DA_WalkGrid. Имя карты приходит из ASDA_MAPS.
        if _LG.login_map_write_blocked("/Game/Map/%s" % an, "asda_rebake_entity_heights.py", quiet=True):
            continue
        if not EAL.does_asset_exist(lvl):
            log("нет уровня: %s (пропуск)" % mp); continue
        if not EAL.does_asset_exist(wg):
            log("нет DA_WalkGrid: %s (пропуск — высоту брать неоткуда)" % mp); continue
        da = unreal.load_asset(wg)
        th = TERH.get(mp)                     # независимая сверка по самому .TER
        les.load_level(lvl)

        actors = [a for a in eas.get_all_level_actors()
                  if isinstance(a, (unreal.AsdaNpcActor, unreal.AsdaMobActor))]
        stat = {"NPC": [0, 0, 0.0, ""], "MOB": [0, 0, 0.0, ""]}   # total, fixed, maxDelta, who
        xy_bad = 0; asset_mismatch = 0.0; skipped = 0
        for a in actors:
            kind = "NPC" if isinstance(a, unreal.AsdaNpcActor) else "MOB"
            tx = a.get_editor_property("tile_x"); tz = a.get_editor_property("tile_z")
            if tx < 0 or tz < 0:                       # отключённый NPC (сервер его не спавнит)
                skipped += 1; continue
            gx, gz = tx * 100 + 50, tz * 100 + 50
            loc = a.get_actor_location()
            # Контроль XY: место в мире должно ровно соответствовать тайлу (конверсия (x,y,z)→(-z,x,y)).
            if abs(loc.x - (-gz)) > 0.5 or abs(loc.y - gx) > 0.5:
                xy_bad += 1
                continue                               # чужая/ручная расстановка — Z не трогаем
            ny = grid_y(da, gx, gz)
            if ny is None:
                skipped += 1; continue
            if th is not None:
                asset_mismatch = max(asset_mismatch, abs(ny - th.GroundY(gx, gz)))
            stat[kind][0] += 1
            dz = ny - loc.z
            if abs(dz) > EPS:
                stat[kind][1] += 1
                if abs(dz) > abs(stat[kind][2]):
                    stat[kind][2] = dz; stat[kind][3] = "%s t(%d,%d) %.1f→%.1f" % (a.get_actor_label(), tx, tz, loc.z, ny)
                if not DRY:
                    # ТОЛЬКО Z: X и Y подставляются ПРЕЖНИЕ. sweep=False (без физического «протаскивания»).
                    a.set_actor_location(unreal.Vector(loc.x, loc.y, ny), False, False)
        if not DRY and (stat["NPC"][1] or stat["MOB"][1]):
            les.save_current_level()
        for kind in ("NPC", "MOB"):
            t, f, mx, who = stat[kind]
            if t == 0: continue
            rows.append((mp, kind, t, f, mx, who))
            grand["ent"] += t; grand["fixed"] += f
            if abs(mx) > abs(gmax): gmax = mx; gmax_who = "%s/%s %s" % (mp, kind, who)
        log("%-20s NPC %d/%d  MOB %d/%d  (пропущено %d, XY-несовпадений %d, |ассет−.TER| max %.4f)"
            % (mp, stat["NPC"][1], stat["NPC"][0], stat["MOB"][1], stat["MOB"][0],
               skipped, xy_bad, asset_mismatch))
        grand["maps"] += 1
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass

    log("=" * 100)
    log("%-20s %-4s %7s %7s %10s  %s" % ("map", "kind", "total", "fixed", "maxΔ", "worst"))
    for mp, kind, t, f, mx, who in rows:
        log("%-20s %-4s %7d %7d %10.2f  %s" % (mp, kind, t, f, mx, who))
    log("ИТОГО%s: карт %d, сущностей %d, исправлено %d, max Δ %.2f (%s)"
        % (" (DRY-RUN, ничего не сохранено)" if DRY else "", grand["maps"], grand["ent"],
           grand["fixed"], gmax, gmax_who))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
