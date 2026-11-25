# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_terrain_patches.py — резка террейна карты на ПАТЧИ-АКТОРЫ (08.09.2026, terrain_lod_report.md).
#
# ЗАЧЕМ. `Tools/asda_build_map_level.py:build_terrain` склеивает все патчи `.mapraw` в ОДИН
#   `SM_terrain` (на alpen — 64 патча по 65×65 вершин, 8192 треугольника каждый = 524 288 тр-ков,
#   64 секции) и вешает его на ОДИН `AStaticMeshActor`. Один примитив на всю карту 51 200 юн
#   означает: (1) отсечение по пирамиде видимости и запросы перекрытия работают «всё или ничего»,
#   (2) КАЖДЫЙ каскад теней рисует весь террейн целиком, (3) уровни детализации бесполезны —
#   экранный размер примитива всегда максимальный. Замер drawdistance_report.md §1: 4.51 млн из
#   4.58 млн треугольников кадра — это террейн, 73 % draw calls кадра — проход теней.
#
# ЧТО ДЕЛАЕТ. Свой `SM_terrain_P<i>` и свой `AStaticMeshActor` на каждый патч. Имя меша начинается
#   на «SM_terrain», поэтому `UAsdaCullDistanceSubsystem` (AsdaCullDistanceSubsystem.cpp:298)
#   по-прежнему относит патчи к классу Terrain (в ini у него bCull=False — землю по дальности не
#   отсекаем; выигрыш даёт пирамида видимости, запросы перекрытия и каскады теней).
#
# ★ ШВЫ. Нормали НЕ пересчитываются по патчу (это и был дефект 07.09.2026, terrain_seams_report.md):
#   они берутся ГОТОВЫМИ из сшитого `SM_terrain` по позиции вершины и передаются билдеру как
#   авторские (`bRecomputeNormals=false`). Освещение патчей то же, что у склеенного меша, — новых
#   швов нет по построению, а не «по осмотру». Геометрия патча — те же вершины источника, поэтому
#   щелей между патчами нет (границу они делят вершина-в-вершину).
#
# Запуск на уже собранном уровне (карту целиком не перегенерируем):
#   python Tools/run_headless_safe.py Tools/asda_terrain_patches.py --marker "ПАТЧИ ЗАВЕРШЕНО"
#   переменные окружения: ASDA_MAP=<карта> (по умолчанию alpen).
import os
import unreal

# ★ ASDA_TERRAIN_STEP — шаг прореживания сетки патча (1 = полное разрешение 65×65 = 8192 треугольника).
#   Шаг общий для всех патчей карты, поэтому границы патчей остаются общими и щелей не возникает.
#   Заведён для ЗАМЕРА цены треугольников террейна (terrain_lod_report.md): шаг 2 даёт вчетверо
#   меньше треугольников при той же покрываемой площади — прямой ответ на вопрос «поможет ли LOD».
STEP = int(os.environ.get("ASDA_TERRAIN_STEP", "1"))
# ★ Уровни детализации патча: (шаг прореживания сетки, экранный размер перехода). Пустой список —
#   без уровней (ASDA_TERRAIN_LODS=0). Экранный размер — доля высоты кадра, которую занимает сфера
#   габаритов патча (радиус ≈ 4700 юн): 0.55 ≈ 17 000 юн до патча, 0.28 ≈ 33 500 юн. Оба порога
#   дальше начала тумана карты (10 000 юн с настройкой владельца), поэтому подмена сетки происходит
#   там, где рельеф уже затянут туманом. Треугольников на патч: 8192 → 2176 → 704.
LOD_LEVELS = [] if os.environ.get("ASDA_TERRAIN_LODS", "1") != "1" else [(2, 0.55), (4, 0.28)]


def _grid_of(pos, vtx):
    """(сторона, at[c][r] → номер вершины) регулярной сетки патча, восстановленная ИЗ КООРДИНАТ.

    Порядок вершин в `.mapraw` форматом не задан, поэтому сетка строится по уникальным x и z патча
    (их ровно 65 на сторону). None — если патч не регулярная сетка (тогда прореживание невозможно)."""
    xs = sorted(set(round(pos[i * 3 + 0], 2) for i in range(vtx)))
    zs = sorted(set(round(pos[i * 3 + 2], 2) for i in range(vtx)))
    side = len(xs)
    if side != len(zs) or side * side != vtx:
        return None, None
    xi = {v: i for i, v in enumerate(xs)}
    zi = {v: i for i, v in enumerate(zs)}
    at = [[0] * side for _ in range(side)]
    for i in range(vtx):
        at[xi[round(pos[i * 3 + 0], 2)]][zi[round(pos[i * 3 + 2], 2)]] = i
    return side, at


def _decimated_indices(side, at, step):
    """Треугольники сетки патча с шагом `step` (1 = полное разрешение).

    Шаг применяется ко ВСЕМ патчам карты одинаково, поэтому граница патча остаётся общей для соседей
    (те же узлы 0, step, …, side−1) и щелей между патчами не возникает. Используется для замера цены
    треугольников террейна: 65×65 при шаге 2 даёт вчетверо меньше треугольников при той же площади."""
    ks = list(range(0, side, step))
    if ks[-1] != side - 1:
        ks.append(side - 1)
    idx = []
    for ci in range(len(ks) - 1):
        for ri in range(len(ks) - 1):
            a = at[ks[ci]][ks[ri]]; b = at[ks[ci + 1]][ks[ri]]
            d = at[ks[ci]][ks[ri + 1]]; e = at[ks[ci + 1]][ks[ri + 1]]
            idx.extend((a, b, d, d, b, e))
    return idx


def _lod_indices(side, at, step):
    """Треугольники уровня детализации патча: внутренность прорежена шагом `step`, ГРАНИЧНОЕ КОЛЬЦО
    оставлено в полном разрешении.

    Зачем полное кольцо: соседние патчи выбирают уровень детализации независимо (по своему экранному
    размеру). Если бы граница прореживалась вместе с внутренностью, у соседей с разными уровнями
    граничные вершины не совпали бы и по стыку пошли бы щели (T-стыки). С полным кольцом граница
    патча одинакова на ВСЕХ уровнях, поэтому любая пара соседей стыкуется вершина-в-вершину.

    Разбиение: (1) регулярная коарс-сетка внутри [step … last−step]; (2) четыре полосы вдоль сторон —
    веер из внутреннего коарс-узла по всем граничным вершинам полного разрешения; (3) четыре угловых
    квадрата — веер из внутреннего угла по ломаной из двух граничных сторон."""
    last = side - 1
    ks = [v for v in range(0, side, step)]
    if ks[-1] != last:
        ks.append(last)
    inner = [v for v in ks if step <= v <= last - step]
    idx = []

    def emit(p, q, s):
        """Треугольник с обходом, согласованным с полной сеткой (положительная площадь в (c, r))."""
        (pc, pr), (qc, qr), (sc, sr) = grid[p], grid[q], grid[s]
        if (qc - pc) * (sr - pr) - (qr - pr) * (sc - pc) < 0:
            q, s = s, q
        idx.extend((p, q, s))

    grid = {}
    for c in range(side):
        for r in range(side):
            grid[at[c][r]] = (c, r)
    # 1) внутренняя коарс-сетка
    for ci in range(len(inner) - 1):
        for ri in range(len(inner) - 1):
            c0, c1, r0, r1 = inner[ci], inner[ci + 1], inner[ri], inner[ri + 1]
            emit(at[c0][r0], at[c1][r0], at[c0][r1])
            emit(at[c0][r1], at[c1][r0], at[c1][r1])
    # 2) полосы вдоль сторон: веер из внутреннего коарс-узла по граничным вершинам шага 1
    def strip(apex_of, outer_of):
        """apex_of(v) — внутренний коарс-узел полосы, outer_of(v) — граничный узел полного разрешения."""
        for j in range(len(inner) - 1):
            a0, a1 = inner[j], inner[j + 1]
            apex = apex_of(a0)
            chain = [outer_of(v) for v in range(a0, a1 + 1)]
            for t in range(len(chain) - 1):
                emit(apex, chain[t], chain[t + 1])
            emit(apex, chain[-1], apex_of(a1))
    strip(lambda r: at[step][r],        lambda r: at[0][r])                      # левая (c = 0)
    strip(lambda r: at[last - step][r], lambda r: at[last][r])                   # правая (c = last)
    strip(lambda c: at[c][step],        lambda c: at[c][0])                      # нижняя (r = 0)
    strip(lambda c: at[c][last - step], lambda c: at[c][last])                   # верхняя (r = last)
    # 3) угловые квадраты: веер из внутреннего угла по ломаной из двух граничных сторон
    for cc, rr in ((0, 0), (0, last), (last, 0), (last, last)):
        ic = step if cc == 0 else last - step
        ir = step if rr == 0 else last - step
        chain = [at[cc][r] for r in (range(ir, rr - 1, -1) if rr == 0 else range(ir, rr + 1))]
        chain += [at[c][rr] for c in (range(cc + 1, ic + 1) if cc == 0 else range(cc - 1, ic - 1, -1))]
        apex = at[ic][ir]
        for t in range(len(chain) - 1):
            emit(apex, chain[t], chain[t + 1])
    return idx


def _normals_by_position(BM, sm):
    """{позиция UE, округлённая до 0.1 юн → нормаль UE} из рендер-данных сшитого SM_terrain.

    Сшивка (`bWeldVertices`) уже усреднила нормали через границы патчей, поэтому таблица по позиции
    отдаёт ОДНУ нормаль на общую вершину соседних патчей — освещение стыков не меняется."""
    r = BM.MB.get_static_mesh_lod0_vertices(sm)
    # ★ Обёртка отдаёт ТОЛЬКО out-параметры (кортеж из 5: позиции, нормали, индексы, цвета, признак
    #   рендер-данных) — возвращаемого bool в кортеже НЕТ, а `unreal.Array` не является list/tuple.
    #   Проверено логом прогона 08.09.2026: типы кортежа ['Array','Array','Array','Array','bool'].
    pos, nrm = r[0], r[1]
    if not len(pos) or not len(nrm) or not isinstance(nrm[0], unreal.Vector):
        unreal.log_error("Патчи террейна: нормали сшитого меша не прочитаны (типы %s)"
                         % [type(x).__name__ for x in r])
        return {}
    d = {}
    for i in range(len(pos)):
        p = pos[i]
        n = nrm[i]
        d[(round(p.x, 1), round(p.y, 1), round(p.z, 1))] = (n.x, n.y, n.z)
    return d


def build_patches(data, eas, BM):
    """Собрать патч-меши `SM_terrain_P<i>` и (если `eas` задан) по актору на патч.

    `BM` — модуль `asda_build_map_level` (его константы, билдер мешей и материалы патчей).
    Возвращает число собранных патчей."""
    EAL = BM.EAL
    master = EAL.load_asset(BM.TER_MASTER)
    comb_path = BM.DEST_ROOT + "/Terrain/SM_terrain"
    comb = EAL.load_asset(comb_path) if EAL.does_asset_exist(comb_path) else None
    if comb is None:
        unreal.log_error("Патчи террейна: нет сшитого %s — сначала обычная сборка террейна" % comb_path)
        return 0
    nrm_by_pos = _normals_by_position(BM, comb)
    unreal.log("Патчи террейна: таблица нормалей сшитого меша — %d позиций" % len(nrm_by_pos))
    if not nrm_by_pos:
        # без таблицы каждый патч считал бы нормали по себе → вернулись бы швы освещения по сетке
        unreal.log_error("Патчи террейна: таблица нормалей ПУСТА — резка отменена")
        return 0
    indices = data["indices"]
    tri_src = [(indices[t], indices[t + 1], indices[t + 2]) for t in range(0, len(indices) - 2, 3)]
    made = 0
    tris = 0
    lod_tris = 0
    miss = 0
    for pi, p in enumerate(data["patches"]):
        vtx = p["vtx"]
        if vtx <= 0:
            continue
        name = "SM_terrain_P%d" % pi
        path = BM.DEST_ROOT + "/Terrain/" + name
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)          # свежий ассет — пересборка поверх портит габариты (см. build_mesh)
        sm = BM.ATOOLS.create_asset(name, BM.DEST_ROOT + "/Terrain", unreal.StaticMesh, None)
        if sm is None:
            unreal.log_warning("Патч %d: create_asset вернул None — пропуск" % pi)
            continue
        pos = list(p["pos"])
        uv = list(p["uv0"])
        col = list(p["col"]) if len(p["col"]) >= vtx * 4 else [255] * (vtx * 4)
        # нормали сшитого меша, обратно в игровое пространство: билдер сам сделает game→UE (-z, x, y)
        nrm = []
        for i in range(vtx):
            u = BM.to_ue(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2])
            n = nrm_by_pos.get((round(u.x, 1), round(u.y, 1), round(u.z, 1)))
            if n is None:
                miss += 1
                nrm.extend((0.0, 0.0, 0.0))             # нулевая = «нет в источнике», UE досчитает сам
            else:
                nrm.extend((n[1], n[2], -n[0]))         # UE (x, y, z) → game (y, z, −x)
        if STEP > 1:
            side, at = _grid_of(pos, vtx)
            if side is None:
                unreal.log_error("Патч %d: не регулярная сетка — прореживание невозможно" % pi)
                return made
            idx = _decimated_indices(side, at, STEP)
        else:
            idx = []
            for a, b, c in tri_src:
                if a >= vtx or b >= vtx or c >= vtx or a == b or b == c or a == c:
                    continue                            # тот же фильтр, что в склейке build_terrain
                idx.extend((a, b, c))
        st = BM._build_sm(sm, name, pos, nrm, uv, col, idx, None, ["P%d" % pi], cat="terpatch",
                          game_to_ue=True, recompute=False, weld=False)
        if st is None:
            continue
        tris += st.triangles
        # материал: существующий MIT_patch_<i> НЕ трогаем (его параметры уже выверены сборкой карты)
        mp = BM.DEST_ROOT + "/Materials/MIT_patch_%d" % pi
        mic = EAL.load_asset(mp) if EAL.does_asset_exist(mp) else BM.make_terrain_mic(pi, p, master)
        sm.set_editor_property("static_materials",
                               [unreal.StaticMaterial(material_interface=mic, material_slot_name="P%d" % pi)])
        # уровни детализации: прореженная сетка с полным граничным кольцом (см. _lod_indices).
        # Каждый уровень собирается тем же билдером во временный ассет и переносится в патч штатным
        # StaticMeshEditorSubsystem.set_lod_from_static_mesh, после чего временный ассет удаляется.
        if LOD_LEVELS:
            side, at = _grid_of(pos, vtx)
            if side is None:
                unreal.log_warning("Патч %d: не регулярная сетка — без уровней детализации" % pi)
            else:
                sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
                for n, (k, _ss) in enumerate(LOD_LEVELS, start=1):
                    tname = "%s_L%d" % (name, n)
                    tpath = BM.DEST_ROOT + "/Terrain/" + tname
                    if EAL.does_asset_exist(tpath):
                        EAL.delete_asset(tpath)
                    tsm = BM.ATOOLS.create_asset(tname, BM.DEST_ROOT + "/Terrain", unreal.StaticMesh, None)
                    if tsm is None:
                        unreal.log_warning("Патч %d: временный ассет %s не создан" % (pi, tname))
                        continue
                    ti = _lod_indices(side, at, k)
                    if BM._build_sm(tsm, tname, pos, nrm, uv, col, ti, None, ["P%d" % pi], cat="terpatch",
                                    game_to_ue=True, recompute=False, weld=False) is not None:
                        sub.set_lod_from_static_mesh(sm, n, tsm, 0, True)
                        lod_tris += len(ti) // 3
                    EAL.delete_asset(tpath)
                sub.set_lod_screen_sizes(sm, [1.0] + [ss for _k, ss in LOD_LEVELS])
        EAL.save_loaded_asset(sm)
        made += 1
        if eas is not None:
            a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
            a.set_mobility(unreal.ComponentMobility.STATIC)
            a.static_mesh_component.set_static_mesh(sm)
            a.set_actor_label("AsdaTerrain_P%d" % pi)
    unreal.log("Патчи террейна: мешей %d, треугольников LOD0 %d, треугольников остальных уровней %d, "
               "вершин без нормали в таблице %d" % (made, tris, lod_tris, miss))
    return made


def _apply_on_level():
    """Применить резку на УЖЕ СОБРАННОМ уровне карты: снять актор склеенного террейна, поставить
    по актору на патч, сохранить уровень. Модели/эффекты/NPC уровня не трогаются."""
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_build_map_level as BM
    import asda_maps as MAPS
    BM.ASSET = MAPS.map_asset_name(BM.MAP_NAME)
    BM.DEST_ROOT = "/Game/Map/" + BM.ASSET
    BM.TEX_ROOT = BM.DEST_ROOT + "/Texture"
    # ★★★ 16.09.2026 (обратный обход ревизии savedir12). ОБЕ проверки — ДО загрузки и сохранения уровня.
    #   Имя карты приходит сюда не напрямую из окружения, а через asda_build_map_level.MAP_NAME (он читает
    #   ASDA_MAP), поэтому инструмент не попал в прежний обзор. Опасность та же, что у 41 скрипта в начале
    #   серии: при ASDA_MAP=Login корень даёт /Game/Map/Login, уровня по такому пути нет, load_level молча
    #   не сработает — а save_current_level ниже запишет ТЕКУЩИЙ уровень, которым в headless-сессии является
    #   стартовая карта, то есть сцена логина со светом владельца.
    import asda_login_guard as _LG
    if _LG.login_map_write_blocked(BM.DEST_ROOT, "asda_terrain_patches.py") or \
       _LG.map_name_blocked(BM.DEST_ROOT, "asda_terrain_patches.py"):
        raise SystemExit(3)
    data = BM.read_mapraw(os.path.join(BM.RAW_DIR, BM.MAP_NAME + ".mapraw"))
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    lvl = BM.DEST_ROOT + "/" + BM.ASSET
    les.load_level(lvl)
    w = unreal.EditorLevelLibrary.get_editor_world()
    if lvl not in (w.get_path_name() if w else ""):
        unreal.log_error("ТЕКУЩИЙ МИР != %s — прерывание" % lvl)
        return
    # снять прежние акторы террейна (склеенный SM_terrain и патчи прошлого прогона)
    killed = 0
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        m = a.static_mesh_component.static_mesh
        if m and m.get_name().startswith("SM_terrain"):
            eas.destroy_actor(a)
            killed += 1
    unreal.log("Патчи террейна: снято прежних акторов террейна — %d" % killed)
    made = build_patches(data, eas, BM)
    les.save_current_level()
    unreal.log("ПАТЧИ ЗАВЕРШЕНО: карта %s, патчей %d" % (BM.MAP_NAME, made))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _apply_on_level()
