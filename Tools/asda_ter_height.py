# Copyright (c) 2026 @sitxovski (Discord)
# asda_ter_height.py — ЕДИНЫЙ источник высоты земли для offline-инструментов (system python, без UE).
# Оригинал берёт высоту НЕ трейсом по рендер-мешу, а из ЛОГИЧЕСКОЙ сетки <map>.TER:
#   terrainGetCellByPos_A840 @0x95A840 → terrainInterpHeightAtXY @0x1130F90 (RE/etalon/world_map.c).
# Раскладка .TER (см. asda_export_walkgrid.py / walkable_grid_re.md): magic 0x20050802, blobSize@4,
#   tableSize@8, шапка 0x2F6C@12 (tileCountX@12, tileCountY@16, cellSize@20, nodeCount@12+0x2a0c),
#   тело = 7 массивов длины N (N = узлы квадродерева; ЛИСТЬЯ первыми row-major, индекс тайла = tz·512+tx):
#     [0] u32 флаги (4·N), [1] f32×4 угловые высоты (16·N) в порядке
#     (minX,maxZ) (minX,minZ) (maxX,minZ) (maxX,maxZ), [2] centerY (4·N), [3] f32 (4·N).
# Швы между тайлами нулевые на всех 22 картах → храним вершинную сетку (W+1)×(H+1), как UAsdaWalkGrid.
# Формула интерполяции 1:1 с UAsdaWalkGrid::GetGroundY (AsdaWalkGrid.h): зажим, тайл, диагональ
#   (minX,minZ)—(maxX,maxZ), выбор треугольника v <= u, линейная интерполяция.
# ★ Модуль БЕЗ побочных эффектов при импорте: только определения (никакого main()).
#
# ★ 2026-08-02 (волна дублей 2): сюда сведены ВСЕ ЧЕТЫРЕ копии разбора .TER —
#   asda_export_walkgrid.parse_walk_bits (флаги проходимости + бит города + вершинная сетка),
#   asda_verify_ground_height.ter_vertgrid (вершинная сетка),
#   asda_export_map_zone_data (строки BGM в заголовке),
#   asda_build_sky_dome.parse_ter_sky / asda_rollout_sky_full.wind_yaw_from_ter (float-поля заголовка).
#   Шапка/офсеты/инвариант теперь в ОДНОМ месте (class TerFile); TerHeight построен на нём.
import os, struct

import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P

TER_DIR = _P.MAP_DIR            # <CLIENT_MINE>\map. .TER в сборках ORIG и MINE побайтово совпадают
                                # на всех 22 картах (сверено 2026-08-02), поэтому корень один.

# UE-имя карты (папка Content) -> (папка .TER, имя файла .TER). Как в asda_export_walkgrid.py.
MAP_TER = {
    "Mountain Flamion": ("Mountain Flamion", "Mountain Flamion.TER"),
    "StagnantDesert":   ("StagnantDesert", "StagnantDesert.TER"),
    "castle_of_chaos":  ("castle_of_chaos", "Castle_of_Chaos.TER"),
    "dragonlair":       ("dragonlair", "DragonLaiR.TER"),
    "firewayforest":    ("firewayforest", "FirewayForest.TER"),
    "nightvalley":      ("nightvalley", "nightvalley.TER"),
}

# ---------------------------------------------------------------- константы формата .TER
TER_MAGIC   = 0x20050802
HDR_AT      = 12          # заголовок начинается сразу за magic/blobSize/tableSize
HDR_SIZE    = 0x2F6C      # длина заголовка
OFF_NODECNT = 0x2A0C      # внутри заголовка: число узлов квадродерева
# BGM-слоты (asda_export_map_zone_data): офсеты ВНУТРИ заголовка, файл = HDR_AT + off
OFF_BGM0    = 0x2F24      # городской трек, 32 байта ASCIIZ
OFF_BGM1    = 0x2F44      # полевой трек, 32 байта ASCIIZ
OFF_HAS0    = 0x2F64      # u32 «слот 0 заполнен» (g_bMapAmbientSound0)
OFF_HAS1    = 0x2F68      # u32 «слот 1 заполнен» (g_bMapAmbientSound1)
# Небо (asda_build_sky_dome / asda_rollout_sky_full): индексы float'ов в заголовке (файл = 12 + idx*4)
IDX_SKY_SPD = 2912        # 4 float: скорости скролла слоёв купола
IDX_SKY_C5  = 2916        # 4 float: VS c5 — вертикальный fog-градиент неба
# Атрибут тайла: БАЙТ 1 из u32-флага. Значения-БЛОК и бит «город».
BLOCK_VALUES = {1, 2, 129, 130, 65, 193}
PEACE_BIT    = 0x80


def ter_path(map_name):
    """Путь к <map>.TER или None."""
    folder, fname = MAP_TER.get(map_name, (map_name, map_name + ".TER"))
    p = os.path.join(TER_DIR, folder, fname)
    return p if os.path.isfile(p) else None


class TerFile(object):
    """Разбор шапки <map>.TER + доступ к телу. ЕДИНЫЙ источник раскладки формата.

    Раскладка (walkable_grid_re.md / CMap_LoadTER@0x1133580):
      magic@0, blobSize@4, tableSize@8, заголовок 0x2F6C@12
      (tileCountX@12, tileCountY@16, cellSize@20, nodeCount@12+0x2A0C), blob tableSize,
      тело = 7 массивов длины N: [0] u32 флаги, [1] f32×4 углы тайла, [2] centerY, [3] f32.
    Инвариант: 12 + 0x2F6C + tableSize + 7·N·4 == размер файла (сверено на 22 картах).
    """

    def __init__(self, path, data=None):
        self.path = path
        b = data if data is not None else open(path, "rb").read()
        self.b = b
        self.magic, self.blob_size, self.table_size = struct.unpack_from("<III", b, 0)
        if self.magic != TER_MAGIC:
            raise ValueError("не .TER (magic 0x%08X): %s" % (self.magic, path))
        self.width, self.height = struct.unpack_from("<II", b, HDR_AT)
        self.cell = struct.unpack_from("<f", b, 20)[0]
        self.node_count = struct.unpack_from("<I", b, HDR_AT + OFF_NODECNT)[0]
        self.cell_off = HDR_AT + HDR_SIZE + self.table_size
        self.tile_count = self.width * self.height

    # --- инвариант тела (копии либо assert'или, либо логировали и отдавали None) ---
    def body_error(self):
        """None, если тело сходится; иначе текст ошибки (для тулов, которые не хотят исключения)."""
        if self.cell_off + 7 * 4 * self.node_count != len(self.b):
            return "инвариант тела не сходится (N=%d, файл %d)" % (self.node_count, len(self.b))
        if self.cell_off + 4 * self.tile_count > len(self.b):
            return "cellFlags за пределами файла"
        return None

    def check_body(self):
        e = self.body_error()
        if e:
            raise ValueError("%s: %s" % (e, self.path))

    # --- поля заголовка ---
    def header_floats(self, idx, n):
        """n float'ов заголовка начиная с индекса idx (file offset = 12 + idx*4)."""
        return struct.unpack_from("<%df" % n, self.b, HDR_AT + idx * 4)

    def header_str(self, off_in_hdr, size=32, encoding="latin1"):
        """ASCIIZ-строка длиной size по офсету off_in_hdr ВНУТРИ заголовка.

        encoding='latin1' — как в asda_export_map_zone_data (имена BGM чисто ASCII, кодировка не важна).
        """
        raw = self.b[HDR_AT + off_in_hdr: HDR_AT + off_in_hdr + size]
        return raw.split(b"\x00")[0].decode(encoding, "ignore").strip()

    def header_u32(self, off_in_hdr):
        return struct.unpack_from("<I", self.b, HDR_AT + off_in_hdr)[0]

    def sky_scroll(self):
        """(spd, c5) — скорости скролла слоёв купола и c5 fog-градиента (asda_build_sky_dome)."""
        return self.header_floats(IDX_SKY_SPD, 4), self.header_floats(IDX_SKY_C5, 4)

    def bgm_slots(self):
        """(городской, полевой, есть0, есть1) — BGM-слоты заголовка (asda_export_map_zone_data)."""
        return (self.header_str(OFF_BGM0), self.header_str(OFF_BGM1),
                self.header_u32(OFF_HAS0), self.header_u32(OFF_HAS1))

    # --- тело ---
    def tile_attr_byte1(self):
        """БАЙТ 1 u32-флага каждого тайла, row-major, длина tile_count (проходимость + бит 0x80)."""
        b = self.b; off = self.cell_off
        return [b[off + 4 * i + 1] for i in range(self.tile_count)]

    def tile_step_material(self):
        """БАЙТ 0 u32-флага каждого тайла — МАТЕРИАЛ ПОВЕРХНОСТИ для следов и звука шагов.

        Прежде считался «мёртвым полем»: читателя не нашли, потому что он спрятан за тремя
        обёртками (`sub_A1A620` → `sub_A1A5E0` → thunk `sub_922687`). На деле его дважды
        вызывает `CCharacter_ApplyMoveState@0xA199E0` и по значению выбирает эффект следа и
        звук шага: 0 — пыль и `S_c_run1`, 1 — всплеск и `S_c_run0` (только над водой),
        2 — `S_c_run2`, 3 — `S_c_run3`, 4 — `S_c_run0`, прочее — `S_c_run1`.
        Разбор: `Reports/alpen_dancefloor_trigger.md` §5.
        """
        b = self.b; off = self.cell_off
        return [b[off + 4 * i] for i in range(self.tile_count)]

    def corner_heights(self):
        """Массив [1]: 4 угловые высоты на тайл, порядок (minX,maxZ)(minX,minZ)(maxX,minZ)(maxX,maxZ)."""
        h_off = self.cell_off + 4 * self.node_count
        return struct.unpack_from("<%df" % (4 * self.tile_count), self.b, h_off)

    def vertex_grid(self):
        """Вершинная сетка высот (W+1)×(H+1) — швы между тайлами нулевые на всех 22 картах.

        Возвращает (vw, vh). Тело 1:1 с прежними копиями (TerHeight / export_walkgrid / verify_ground).
        """
        tcx, tcy = self.width, self.height
        H = self.corner_heights()
        vw = tcx + 1
        vh = [0.0] * (vw * (tcy + 1))
        for tz in range(tcy):
            row = tz * vw
            base = tz * tcx * 4
            for tx in range(tcx):
                vh[row + tx] = H[base + tx * 4 + 1]           # (minX,minZ)
            vh[row + tcx] = H[base + (tcx - 1) * 4 + 2]       # правый край: (maxX,minZ)
        top = tcy * vw
        base = (tcy - 1) * tcx * 4
        for tx in range(tcx):
            vh[top + tx] = H[base + tx * 4 + 0]               # верхний край: (minX,maxZ)
        vh[top + tcx] = H[base + (tcx - 1) * 4 + 3]           # угол: (maxX,maxZ)
        return vw, vh


class TerHeight(object):
    """Сетка высот карты из .TER. GroundY(gameX, gameZ) — 1:1 эталон/UAsdaWalkGrid::GetGroundY."""

    def __init__(self, path):
        t = TerFile(path)
        t.check_body()
        self.ter = t
        self.width, self.height, self.cell = t.width, t.height, t.cell
        self.vw, self.vh = t.vertex_grid()

    def GroundY(self, game_x, game_z):
        """Высота земли в мировой точке карты (игровые X/Z). Возвращает float."""
        cell = self.cell
        min_c = cell
        max_c = (self.width - 1) * cell
        x = game_x
        z = game_z
        if x < 0.0: x = min_c
        if x > max_c: x = max_c
        if z < 0.0: z = min_c
        if z > max_c: z = max_c
        tx = int(x / cell)
        tz = int(z / cell)
        if tx < 0: tx = 0
        if tx > self.width - 1: tx = self.width - 1
        if tz < 0: tz = 0
        if tz > self.height - 1: tz = self.height - 1
        vw = self.vw
        h00 = self.vh[tz * vw + tx]              # (minX,minZ)
        h10 = self.vh[tz * vw + tx + 1]          # (maxX,minZ)
        h01 = self.vh[(tz + 1) * vw + tx]        # (minX,maxZ)
        h11 = self.vh[(tz + 1) * vw + tx + 1]    # (maxX,maxZ)
        u = (x - tx * cell) / cell
        v = (z - tz * cell) / cell
        if v <= u:
            return h00 + (h10 - h00) * u + (h11 - h10) * v
        return h00 + (h01 - h00) * v + (h11 - h01) * u


_CACHE = {}


def get(map_name):
    """TerHeight карты (кэш) или None, если .TER не найден."""
    if map_name not in _CACHE:
        p = ter_path(map_name)
        _CACHE[map_name] = TerHeight(p) if p else None
    return _CACHE[map_name]
