# Copyright (c) 2026 @sitxovski (Discord)
# asda_jbm.py — ЕДИНЫЙ разбор шапки и геометрии (Pass1) модели AsdaStory `.jbm`.
#
# Раскладка (порт LoadJBM из MapViewer/src/asda_formats.cpp + Reports/jbm_skinned_layout.md,
# эталон AsdaStory_game.c:894002):
#   +0x000  u32  magic = 0x20050706
#   H = 4 (шапка идёт СО СМЕЩЕНИЕМ 4 от начала файла)
#   H+0x004 u32  meshCount            H+0x030 u32  matCount
#   H+0x178 char[0x60] имя модели     H+0x204 u32  blobSize
#   H+0x20C           начало blob:  matTable [matCount × 0x20] — имя текстуры материала (ASCII, backslash-путь)
#                     затем meshRec [meshCount × 0x29C]:
#                        mr+0x000 u32 meshType (значимы младшие 12 бит)
#                        mr+0x004 u32 renderFlags ← ★ бит 0x1 = меш рисуется С АЛЬФА-БЛЕНДИНГОМ
#                        mr+0x028 u32 vtxCount     mr+0x02C u32 triCount
#                        mr+0x194 u32 fvf (РАНТАЙМ, в файле не нужен — вершина всегда 44Б)
#                        mr+0x1F4 u32 matIdx  ← ★ ПЕР-МЕШ индекс материала в matTable
#                        mr+0x1F8 u32 M (размер палитры костей)   mr+0x244 u32 B (кол-во костей)
#                        mr+0x1FC char[0x60] имя узла (node)
#                     затем ГЕОМЕТРИЯ (Pass1), все меши подряд одним потоком:
#                        индексы u16 × 3·tri | вершины 44Б (11 float) | pNormalArray 12Б × vtx
#                        ★ вершина 44Б = D3D FVF 0x252 XYZ|NORMAL|DIFFUSE|TEX2:
#                          float 0..2 = pos (МОДЕЛЬ), float 3..5 = НОРМАЛЬ (МОДЕЛЬ, MapViewer LoadJBM
#                          asda_formats.cpp:509-511 читает её @12), float 6 = DIFFUSE (0xCDCDCDCD мусор),
#                          float 7..8 = uv0 @28, float 9..10 = uv1 (нули).
#                        ★ pNormalArray (12Б-стрим ПОСЛЕ вершин) — нормаль в ЛОКАЛЕ КОСТИ 0 (как
#                          POSITION0 скин-блока): эталон ParticleMesh_FillVertexBuffer@0x11AE4C0 пишет
#                          её в рантайм-VB рядом с POSITION0/1, и шейдер SKIN вращает обе одной матрицей
#                          кости (`m4x3 r0,v0,c[a0.x]` / `m3x3 r1,v2,c[a0.x]`). Для ригид-узлов 0x200/0x800
#                          стримы совпадают побайтно. См. review_setD.md §2 (2026-09-05).
#                     затем скин-блок (Pass2) — ЗДЕСЬ НЕ РАЗБИРАЕТСЯ (см. asda_skinned_to_gltf.py).
#
# ★ ПЕР-МЕШ МАТЕРИАЛ (закон npc-texture-source): текстура берётся по matIdx@0x1F4 КАЖДОГО меша,
#   а НЕ первая запись matTable на все меши. Копия в asda_jbm_to_gltf.py этот фикс потеряла
#   (брала tex0 для всех) — сведено сюда 2026-08-01.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (правило tools-no-import-side-effects): только определения.

import os
import struct

MAGIC = 0x20050706

HDR_OFF        = 4        # H
OFF_MESH_COUNT = 0x004    # H+
OFF_MAT_COUNT  = 0x030    # H+
OFF_NAME       = 0x178    # H+ , char[0x60]
OFF_BLOB_SIZE  = 0x204    # H+
OFF_BLOB       = 0x20C    # H+

MAT_ENTRY   = 0x20        # запись matTable
MESH_REC    = 0x29C       # запись meshRec
VTX_STRIDE  = 44          # вершина Pass1 = 11 float
NRM_STRIDE  = 12          # отдельный стрим нормалей

MR_TYPE    = 0x000
# ★★★ РЕНДЕР-ФЛАГИ МЕША (mr+0x004) — ПОЛНЫЙ разбор: `Reports/jbm_blend_flag_study.md` §1.2.
#   Бит 0x1 — это НЕ «альфа-блендинг сам по себе», а МАРШРУТ ОТРИСОВКИ: помеченный меш ИСКЛЮЧАЕТСЯ
#   из непрозрачного прохода (`meshNodeRenderDispatch@0x111E110`, model_anim.c:25098 — при взведённом
#   0x1/0x2 реального DrawIndexedPrimitive НЕТ, кроме теневого прохода) и уходит в очередь
#   `g_RenderQueue`, отсортированную по d² до камеры (`CRenderQueue_AddObject@0x1143120`).
#   Очередь сливается ОДИН раз в конце кадра (`CRenderer_RenderFrame@0x975EF0`, render_core.c:820-827):
#       ALPHABLENDENABLE=1, SRCBLEND=SRCALPHA(5), DESTBLEND=INVSRCALPHA(6),
#       ALPHATESTENABLE=1, ALPHAREF=8, ALPHAFUNC=GREATEREQUAL(7),  ГЛУБИНА ПИШЕТСЯ
#   (запись глубины гасит только бит 0x10 — `rendApplyZWriteOnState@0x979910` ставит ZWRITEENABLE=0,
#    имя функции врёт).
#   Биты: 0x1 = маршрут в очередь (файловый); 0x2 = то же, но взводится в рантайме при дистанционном
#   фейде (`meshPartsSetRenderPassFlags@0x11C2720`); 0x4 — ЧТЕНИЙ В СРЕЗЕ ЭТАЛОНА НЕТ (стоит у 59
#   мешей: зеркала, стволы, надгробия); 0x8 = альтернативный вершинный шейдер той же техники
#   (`CMeshNode_DrawDispatch@0x11166A0`), к прозрачности отношения не имеет; 0x10 = не писать глубину.
MR_FLAGS   = 0x004
MR_BLEND_BIT = 0x1          # файловый маршрут «в отсортированную очередь»
MR_BLEND_BIT_RUNTIME = 0x2  # тот же маршрут, взводится движком при дистанционном фейде
MR_NOZWRITE_BIT = 0x10      # в очереди рисовать без записи глубины

# ★★★ ТЕХНИКА ШЕЙДЕРА (mr+0x000, младшие 12 бит) — ОНА решает, идёт ли альфа текстуры в ПРОЗРАЧНОСТЬ.
#   Эталон — шипнутый с клиентом `system/EmptyProject.fx` (`CRenderer_CacheEffectHandles@0x111E610`
#   кэширует техники tSkin/tRigid/tNoSkin/...):
#     tSkin  (0x080/0x081) и tRigid (0x100/0x101) → PShade():
#         Output.RGBColor.w = (1 * (In.fog*4.0f)) * (NoSkinTexScroll.w*0.25f);
#       ↑ автор ЯВНО заменил Tex.w на 1 ⇒ альфа текстуры = МАСКА БЛИКА/КУБМАПА, а НЕ прозрачность.
#     tNoSkin (0x200/0x201/0x800/0x801) → PShadeNoSkin():
#         Output.RGBColor.w = (Tex.w * (In.fog*2.0f)) * (NoSkinTexScroll.w*0.5f);
#       ↑ здесь Tex.w ИДЁТ в альфу ⇒ прозрачность = альфа текстуры × фейд объекта.
#   Замер по данным клиента (jbm_blend_flag_study.md §3.3): у tNoSkin связь «бит ↔ градиентная альфа»
#   96 % против 10 % без бита; у tSkin/tRigid связь объясняется тем, что маска блика рисуется на тех же
#   деталях. Отсюда единственный корректный критерий включения прозрачности в порту — `is_alpha_blend`.
TNOSKIN_TYPES = frozenset((0x200, 0x201, 0x800, 0x801))

MR_VTX     = 0x028
MR_TRI     = 0x02C
MR_MATIDX  = 0x1F4
MR_M       = 0x1F8
MR_NODE    = 0x1FC        # char[0x60]
MR_B       = 0x244

# FVF → (stride, uvOff, normalOff)  [asda_formats.cpp:595-624]; нужен только внешним потребителям.
FVF_STRIDE = {0x002: 12, 0x042: 16, 0x044: 20, 0x102: 20, 0x104: 24, 0x142: 24, 0x144: 28,
              0x152: 36, 0x242: 32, 0x252: 44, 0x256: 48, 0x304: 40, 0x60252: 72}
FVF_UVOFF  = {0x102: 12, 0x104: 16, 0x142: 16, 0x144: 20, 0x152: 28, 0x242: 16, 0x252: 28,
              0x256: 32, 0x304: 16, 0x60252: 28}
FVF_NRMOFF = {0x152: 12, 0x252: 12, 0x60252: 12, 0x256: 16}


def is_alpha_blend(rec):
    """★ ЕДИНСТВЕННЫЙ критерий «мешу надо включать ПРОЗРАЧНОСТЬ» (jbm_blend_flag_study.md §5).

    `(flags & (0x1|0x2)) И техника tNoSkin`. Проверка ТЕХНИКИ обязательна: у tSkin/tRigid тот же
    бит стоит у 553 мешей (NPC/экип/мобы), но их альфа по `PShade()` — маска блика, и включение
    прозрачности воспроизвело бы баг 2026-07-31 («дырявое оружие и броня»).

    ★ ТОЧКА ПРАВДЫ О КЛАССЕ ПОДСЕТКИ — `asda_mesh_render_class.mesh_class()` (там же разобран
      порядок проверки битов техники в `renderApplyMeshState@0x1116FE0`). Здесь только тонкая
      обёртка «класс == BLEND», чтобы у двух модулей не разъехались критерии. Локальная копия
      условия оставлена лишь как страховка на случай, если модуль недоступен (офлайн-сборка).

    Принимает как запись `parse_header()['recs'][i]`, так и меш `parse_geometry()['meshes'][i]`
    (у второго те же ключи — он делает `dict(r)`).
    """
    mt = int(rec.get("mtype", 0)) & 0xFFF
    fl = int(rec.get("flags", 0))
    try:
        import asda_mesh_render_class as _MRC
        return _MRC.mesh_class(mt, fl) == _MRC.CLS_BLEND
    except Exception:
        return bool(fl & (MR_BLEND_BIT | MR_BLEND_BIT_RUNTIME)) and mt in TNOSKIN_TYPES


import asda_bintable as _BT   # общий разбор C-строк (волна 2 аудита дублей)


def _cstr(b, off, size):
    """ASCII-строка фиксированного поля (обрезка по первому 0) — общий `asda_bintable.cstr`."""
    return _BT.cstr(b, off, size, "latin1")


def is_jbm(b):
    return len(b) >= 4 and struct.unpack_from("<I", b, 0)[0] == MAGIC


def parse_header(b):
    """Шапка + таблица материалов + записи мешей. Возвращает dict или None (не .jbm / битый).

    Ключи: name, meshCount, matCount, blobSize, blobStart, blobEnd, matTable (список имён текстур,
    basename), tex0, geoStart, recs (список записей мешей).
    Запись меша: mr, mtype, flags, blend, blendAlpha, V, tri, M, B, matIdx, tex, node.
    ★ `blend` — СЫРОЙ бит 0x1 (маршрут в очередь). Для решения «делать материал прозрачным»
      использовать `blendAlpha` / `is_alpha_blend()` — там дополнительно проверена техника tNoSkin.
    """
    if len(b) < HDR_OFF + OFF_BLOB or not is_jbm(b):
        return None
    U = lambda o: struct.unpack_from("<I", b, o)[0] if 0 <= o and o + 4 <= len(b) else 0
    H = HDR_OFF
    mesh_count = U(H + OFF_MESH_COUNT)
    mat_count  = U(H + OFF_MAT_COUNT)
    blob_size  = U(H + OFF_BLOB_SIZE)
    if mesh_count == 0 or mesh_count > 4096:
        return None
    name = _cstr(b, H + OFF_NAME, 0x60)

    blob_start = H + OFF_BLOB
    mat_table  = blob_start
    mesh_rec   = mat_table + mat_count * MAT_ENTRY
    geo_start  = mesh_rec + mesh_count * MESH_REC
    blob_end   = min(blob_start + blob_size, len(b))

    # ГЛОБАЛЬНАЯ таблица материалов: имя текстуры каждого материала (basename, слэши нормализованы).
    mats = [os.path.basename(_cstr(b, mat_table + i * MAT_ENTRY, MAT_ENTRY).replace("\\", "/"))
            for i in range(mat_count)]
    tex0 = mats[0] if mats else ""

    recs = []
    for m in range(mesh_count):
        mr = mesh_rec + m * MESH_REC
        mi = U(mr + MR_MATIDX)                      # ★ пер-меш индекс материала
        flags = U(mr + MR_FLAGS)
        mtype = U(mr + MR_TYPE) & 0xFFF
        recs.append(dict(mr=mr,
                         mtype=mtype,
                         flags=flags,                                   # сырое поле mr+0x004
                         blend=bool(flags & MR_BLEND_BIT),              # СЫРОЙ бит маршрута (без теста техники!)
                         # ★ готовый вердикт «включать прозрачность» — бит И техника tNoSkin
                         blendAlpha=(bool(flags & (MR_BLEND_BIT | MR_BLEND_BIT_RUNTIME))
                                     and mtype in TNOSKIN_TYPES),
                         V=U(mr + MR_VTX),
                         tri=U(mr + MR_TRI),
                         M=U(mr + MR_M),
                         B=U(mr + MR_B),
                         matIdx=mi,
                         tex=(mats[mi] if 0 <= mi < mat_count else tex0),
                         node=_cstr(b, mr + MR_NODE, 0x60)))
    return dict(name=name, meshCount=mesh_count, matCount=mat_count, blobSize=blob_size,
                blobStart=blob_start, blobEnd=blob_end, matTable=mats, tex0=tex0,
                geoStart=geo_start, recs=recs)


def pass1_offsets(hdr):
    """Смещения Pass1 для каждого меша: (idxOff, vtxOff, nrmOff, endOff). Без numpy.

    Возвращает (offsets, geo_end). geo_end — начало скин-блока (Pass2).
    Строгий инвариант: меши идут подряд idx|vtx|nrm.
    """
    geo = hdr["geoStart"]
    out = []
    for r in hdr["recs"]:
        V, tri = r["V"], r["tri"]
        i_off = geo
        v_off = i_off + tri * 3 * 2
        n_off = v_off + V * VTX_STRIDE
        end   = n_off + V * NRM_STRIDE
        out.append((i_off, v_off, n_off, end))
        geo = end
    return out, geo


def _geo_vertex_normals(pos, idx):
    """Площадно-взвешенная вершинная нормаль по ВИНДУ ФАЙЛА (LH): cross(p1−p0, p2−p0).
    Знак проверен по данным: у валидных модельных нормалей float 3..5 cos с этой геометрией
    +0.88…+0.94 в среднем по CharModel/NpcModel/MobModel/Item (verify_normals, 2026-09-05)."""
    import numpy as np
    tri = idx.reshape(-1, 3)
    p0, p1, p2 = pos[tri[:, 0]], pos[tri[:, 1]], pos[tri[:, 2]]
    fn = np.cross(p1 - p0, p2 - p0)
    vn = np.zeros_like(pos)
    for k in range(3):
        np.add.at(vn, tri[:, k], fn)
    ln = np.linalg.norm(vn, axis=1)
    ok = ln > 0
    vn[ok] /= ln[ok][:, None]
    return vn


# Порог валидности модельной нормали (|n| < этого или NaN → геометрическая замена).
NRM_MIN_LEN = 0.05


def legacy_normals():
    """★ Флаг совместимости: ASDA_JBM_LEGACY_NORMALS=1 → `nrm` = бон-локальный pNormalArray (поведение
    до 2026-09-05). Нужен только для воспроизведения СТАРЫХ glb (сверка/диагностика); по умолчанию ВЫКЛ."""
    return os.environ.get("ASDA_JBM_LEGACY_NORMALS", "") not in ("", "0")


def parse_geometry(b, guard=True):
    """Шапка + Pass1-геометрия через numpy. Возвращает dict(name, tex, meshes, geoEnd) или None.

    meshes[i]: pos (V×3), nrm (V×3), nrm_model (V×3), nrm_bone (V×3), nrm_fallback (V bool),
               uv (V×2), idx (3·tri), tex, node, mtype, V, tri, M, B, matIdx, mr.

    ★★★ НОРМАЛИ (фикс 2026-09-05, review_setD.md §2.3):
      nrm_model — МОДЕЛЬНАЯ нормаль из самой вершины (float 3..5 44-байтной вершины), нормирована;
                  вершины с NaN или |n| < NRM_MIN_LEN заменены геометрической по винду файла
                  (`nrm_fallback[v]=True`; в архивах таких 0.01–0.02 % вершин, 33+11+8 мешей с NaN).
      nrm_bone  — pNormalArray как есть: нормаль в ЛОКАЛЕ КОСТИ 0 (см. шапку модуля). Раньше
                  ИМЕННО ОНА отдавалась как `nrm` и клалась экспортёрами в glb рядом с модельной
                  позицией → в UE нормаль повёрнута bind-ротацией кости (лицо ≈ разворот); флип
                  знака не лечит (17–40 % треугольников остаются против).
      nrm       — то, что берут ВСЕ потребители (asda_skinned_to_gltf, asda_test_export_one,
                  asda_jbm_to_gltf, asda_mapanim_to_gltf, asda_e5_billboards, asda_export_raw,
                  asda_build_map_level, asda_verify_mesh_normals) = nrm_model;
                  при ASDA_JBM_LEGACY_NORMALS=1 = nrm_bone (старое поведение, только для сверки).
      Знак, винд, LH→RH-зеркало и флаги Interchange здесь НЕ трогаются.

    guard=True — поведение asda_jbm_to_gltf: при выходе за blob / битом индексе меши после
    проблемного ОТБРАСЫВАЮТСЯ (break), файл не роняется. guard=False — строгое чтение (как в
    asda_skinned_to_gltf / asda_test_export_one), исключение наружу.
    """
    import numpy as np
    hdr = parse_header(b)
    if hdr is None:
        return None
    legacy = legacy_normals()
    meshes = []
    geo = hdr["geoStart"]
    for r in hdr["recs"]:
        V, tri = r["V"], r["tri"]
        if guard and (V == 0 or tri == 0):
            break
        i_off = geo
        v_off = i_off + tri * 3 * 2
        n_off = v_off + V * VTX_STRIDE
        end   = n_off + V * NRM_STRIDE
        if guard and end > hdr["blobEnd"]:
            break
        idx = np.frombuffer(b, np.uint16, tri * 3, i_off).astype(np.uint32)
        if guard and idx.size and idx.max() >= V:
            break
        Varr = np.frombuffer(b, np.float32, V * 11, v_off).reshape(V, 11)
        pos  = Varr[:, 0:3].copy()      # позиция  @0   (модель)
        uv   = Varr[:, 7:9].copy()      # uv0      @28 (float #7)
        nrm_bone = np.frombuffer(b, np.float32, V * 3, n_off).reshape(V, 3).copy()   # локаль кости 0
        # ★ модельная нормаль @12 (float 3..5) + геометрическая замена невалидных
        raw = Varr[:, 3:6]
        finite = np.isfinite(raw).all(axis=1)
        nrm_model = np.where(finite[:, None], raw, 0.0).astype(np.float32)
        ln = np.linalg.norm(nrm_model, axis=1)
        good = finite & (ln >= NRM_MIN_LEN)
        nrm_model[good] /= ln[good][:, None]
        fallback = ~good
        if fallback.any() and idx.size and (idx.max() < V):
            nrm_model[fallback] = _geo_vertex_normals(np.nan_to_num(pos), idx)[fallback]
        me = dict(r)
        me.update(pos=pos, uv=uv, idx=idx, nrm_model=nrm_model, nrm_bone=nrm_bone, nrm_fallback=fallback,
                  nrm=(nrm_bone if legacy else nrm_model))
        meshes.append(me)
        geo = end
    return dict(name=hdr["name"], tex=hdr["tex0"], matTable=hdr["matTable"],
                meshes=meshes, geoEnd=geo, hdr=hdr)


def parse_file(path, guard=True):
    with open(path, "rb") as f:
        return parse_geometry(f.read(), guard=guard)
