# Copyright (c) 2026 @sitxovski (Discord)
# asda_portal_gfx_gen.py — генератор `Source/AsdaReborn/Public/World/AsdaPortalGfx.h` (04.11.2026, баг владельца «клик по модели
#   портала не ведёт к порталу»): объекты набора портала (*portal* / *gate* / *door*) из `.mapraw` у каждой серверной зоны.
# Запуск: python Tools/asda_portal_gfx_gen.py [путь .h] (по умолчанию — заголовок в Source). Без побочных эффектов при импорте.
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))   # папка Tools
DEFAULT_OUT = os.path.join(os.path.dirname(TOOLS_DIR), "Source", "AsdaReborn", "Public", "World", "AsdaPortalGfx.h")   # выход
MARGIN = 6                                # клеток вокруг зоны, в которых объект считается частью портала
KEYS = ("portal", "gate", "door")         # части имени модели набора портала
COLUMN_MIN = 900.0                        # высота «столба» зоны над землёй по умолчанию (кольцо + свечение), ед.
COLUMN_NEAR = 3                           # объект не дальше стольких клеток от зоны задаёт высоту её столба

HEADER = r'''// ★★ ГРАФИКА ПОРТАЛОВ КАРТ (04.11.2026, баг владельца «клик по модели/кольцу портала не ведёт к порталу»). [ИСПРАВЛЕНИЕ]
//   Объекты набора портала из `.mapraw` каждой карты: кольца `mdel_portaleff*`, домики `mdel_portal01*`, ворота `gate_*`,
//   `*steamgate*`, `chess_gate*`, `mdel_fortressdoor*`, арки `mdel_alpenportal*`, `inferionportal*` — всё с именем *portal* / *gate* /
//   *door*, чьи габариты лежат не дальше 6 клеток от серверной зоны портала (`kPortalGraph`). Имя — меш порта (`SM_<модель>`,
//   экземпляры ISM актора `AsdaMapObjects_<карта>`). Одна модель может стоять у нескольких зон (ворота flammio, домики Fireway):
//   зону выбирает вызывающий — ближайшую к точке попадания. ФАЙЛ СГЕНЕРИРОВАН: `Tools/asda_portal_gfx_gen.py` (руками не править).
#pragma once

#include "CoreMinimal.h"

namespace AsdaPortalGfx
{
	// Объект набора портала: карта (id) и имя меша порта.
	struct FEntry { int32 MapId; const TCHAR* Mesh; };
	inline constexpr FEntry kEntries[] = {
%s
	};

	// Меш Mesh на карте MapId — графика портала.
	inline bool IsPortalMesh(int32 MapId, const FString& Mesh)
	{
		for (const FEntry& E : kEntries)
		{
			if (E.MapId == MapId && Mesh.Equals(E.Mesh, ESearchCase::IgnoreCase)) { return true; }
		}
		return false;
	}

	// Высота «столба» зоны портала над землёй (луч мыши, прошедший сквозь него, — клик по порталу): максимум из 900 и верха
	//   самого высокого объекта набора портала не дальше 3 клеток от зоны над низшей землёй зоны (+100).
	struct FColumn { int32 MapId; int32 X0, Z0, X1, Z1; float Height; };
	inline constexpr FColumn kColumns[] = {
%s
	};

	// Высота столба зоны X0..X1 × Z0..Z1 карты MapId (900, если зоны нет в таблице).
	inline double ColumnHeight(int32 MapId, int32 X0, int32 Z0, int32 X1, int32 Z1)
	{
		for (const FColumn& C : kColumns)
		{
			if (C.MapId == MapId && C.X0 == X0 && C.Z0 == Z0 && C.X1 == X1 && C.Z1 == Z1) { return C.Height; }
		}
		return 900.0;
	}
}
'''


def collect():
    """Строки таблицы: (id карты, имя модели) для объектов набора портала у каждой зоны."""
    if TOOLS_DIR not in sys.path:
        sys.path.insert(0, TOOLS_DIR)
    import asda_portal_pockets as PP
    maps, edges = PP.load_registry()
    cache = {}
    seen = []
    columns = []
    for e in edges:
        m = maps[e["from"]]
        if e["from"] not in cache:
            cache[e["from"]] = PP.MapData(m["folder"], None)
        md = cache[e["from"]]
        g_low = min(md.ground(x, z) for x in range(e["x0"], e["x1"] + 1) for z in range(e["z0"], e["z1"] + 1))
        column = COLUMN_MIN
        for nm, pts, _idx in md.objects():
            if not any(k in nm.lower() for k in KEYS):
                continue
            x0 = min(p[0] for p in pts) / 100.0
            x1 = max(p[0] for p in pts) / 100.0
            z0 = min(p[2] for p in pts) / 100.0
            z1 = max(p[2] for p in pts) / 100.0
            if x1 < e["x0"] - MARGIN or x0 > e["x1"] + 1 + MARGIN or z1 < e["z0"] - MARGIN or z0 > e["z1"] + 1 + MARGIN:
                continue
            if (e["from"], nm) not in seen:
                seen.append((e["from"], nm))
            if not (x1 < e["x0"] - COLUMN_NEAR or x0 > e["x1"] + 1 + COLUMN_NEAR or z1 < e["z0"] - COLUMN_NEAR or z0 > e["z1"] + 1 + COLUMN_NEAR):
                column = max(column, max(p[1] for p in pts) - g_low + 100.0)
        columns.append((e["from"], e["x0"], e["z0"], e["x1"], e["z1"], round(column)))
    return seen, columns


def main():
    """Собрать таблицу и записать заголовок."""
    out = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_OUT
    seen, columns = collect()
    rows = ['\t\t{ %d, TEXT("SM_%s") },' % (mid, nm) for mid, nm in seen]
    cols = ['\t\t{ %d, %d, %d, %d, %d, %d.f },' % c for c in columns]
    with open(out, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(HEADER % ("\n".join(rows), "\n".join(cols)))
    print("%d объектов -> %s" % (len(rows), out))


if __name__ == "__main__":
    main()
