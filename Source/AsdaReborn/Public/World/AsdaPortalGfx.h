// Copyright (c) 2026 @sitxovski (Discord)
// ★★ ГРАФИКА ПОРТАЛОВ КАРТ (04.11.2026, баг владельца «клик по модели/кольцу портала не ведёт к порталу»). [ИСПРАВЛЕНИЕ]
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
		{ 3, TEXT("SM_mdel_alpenportal03_a_00") },
		{ 2, TEXT("SM_mdel_fortressdoor_a_00_2") },
		{ 2, TEXT("SM_mdel_portaleff_a_00") },
		{ 2, TEXT("SM_mdel_fortressdoor_a_00") },
		{ 2, TEXT("SM_mdel_fortressdoor_a_00_3") },
		{ 15, TEXT("SM_mdel_steamgate01_a_00") },
		{ 15, TEXT("SM_mdel_steamgate01_a_00_1") },
		{ 15, TEXT("SM_mdel_steamgate01_a_00_2") },
		{ 15, TEXT("SM_mdel_steamgate01_a_00_3") },
		{ 15, TEXT("SM_mdel_steamgate01_a_00_4") },
		{ 15, TEXT("SM_mdel_steamgate01_a_00_5") },
		{ 15, TEXT("SM_mdel_steamgate01_a_00_6") },
		{ 16, TEXT("SM_mdel_portal01_a_00") },
		{ 16, TEXT("SM_mdel_portal01_a_00_1") },
		{ 16, TEXT("SM_mdel_portal01_a_00_2") },
		{ 16, TEXT("SM_mdel_portal01_a_00_3") },
		{ 16, TEXT("SM_mdel_portal01_a_00_4") },
		{ 16, TEXT("SM_mdel_portaleff_a_00") },
		{ 16, TEXT("SM_mdel_portal01_a_00_5") },
		{ 16, TEXT("SM_mdel_portal01_a_00_6") },
		{ 16, TEXT("SM_mdel_portal01_a_00_7") },
		{ 16, TEXT("SM_mdel_portal01_a_00_8") },
		{ 16, TEXT("SM_mdel_portal01_a_00_9") },
		{ 16, TEXT("SM_mdel_portaleff_a_00_1") },
		{ 13, TEXT("SM_mdel_portal01_a_00_5") },
		{ 13, TEXT("SM_mdel_portal01_a_00_6") },
		{ 13, TEXT("SM_mdel_portal01_a_00_7") },
		{ 13, TEXT("SM_mdel_portal01_a_00_8") },
		{ 13, TEXT("SM_mdel_portal01_a_00_9") },
		{ 13, TEXT("SM_mdel_portal01_a_00_20") },
		{ 13, TEXT("SM_mdel_portal01_a_00_21") },
		{ 13, TEXT("SM_mdel_portal01_a_00_22") },
		{ 13, TEXT("SM_mdel_portal01_a_00_23") },
		{ 13, TEXT("SM_mdel_portal01_a_00_24") },
		{ 13, TEXT("SM_mdel_portal01_a_00_25") },
		{ 13, TEXT("SM_mdel_portal01_a_00_26") },
		{ 13, TEXT("SM_mdel_portal01_a_00_27") },
		{ 13, TEXT("SM_mdel_portal01_a_00_28") },
		{ 13, TEXT("SM_mdel_portal01_a_00_29") },
		{ 7, TEXT("SM_gate_004") },
		{ 18, TEXT("SM_inferionportal") },
		{ 18, TEXT("SM_inferionportal_1") },
		{ 5, TEXT("SM_mdel_steamgate_a_00") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_1") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_2") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_3") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_4") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_5") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_6") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_7") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_8") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_9") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_10") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_11") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_12") },
		{ 5, TEXT("SM_mdel_steamgate_a_00_15") },
		{ 12, TEXT("SM_mdel_steamgate_a_00") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_1") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_2") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_3") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_4") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_5") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_6") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_7") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_8") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_9") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_10") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_11") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_12") },
		{ 12, TEXT("SM_mdel_steamgate_a_00_15") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_16") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_17") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_18") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_19") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_20") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_21") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_22") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_23") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_24") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_27") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_28") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_29") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_30") },
		{ 4, TEXT("SM_mdel_steamgate_a_00_31") },
		{ 1, TEXT("SM_mdel_fortressdoor_a_00_1") },
		{ 1, TEXT("SM_mdel_portaleff_a_00") },
		{ 1, TEXT("SM_mdel_alpenportal02_a_00") },
		{ 1, TEXT("SM_mdel_alpenportal01_a_00") },
		{ 1, TEXT("SM_mdel_alpenportal01_a_00_1") },
		{ 1, TEXT("SM_mdel_alpenportal01_a_00_2") },
		{ 1, TEXT("SM_mdel_alpenportal01_a_00_3") },
		{ 1, TEXT("SM_mdel_alpenportal01_a_00_4") },
		{ 1, TEXT("SM_chess_gate") },
		{ 1, TEXT("SM_chess_gate_1") },
		{ 1, TEXT("SM_chess_gate_2") },
		{ 1, TEXT("SM_chess_gate_3") },
		{ 0, TEXT("SM_mdel_alpenportal02_a_00_1") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_1") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_2") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_3") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_4") },
		{ 0, TEXT("SM_mdel_alpenportal02_a_00") },
		{ 0, TEXT("SM_chess_gate") },
		{ 0, TEXT("SM_chess_gate_1") },
		{ 0, TEXT("SM_chess_gate_2") },
		{ 0, TEXT("SM_chess_gate_3") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_5") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_6") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_7") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_8") },
		{ 0, TEXT("SM_mdel_alpenportal01_a_00_9") },
		{ 14, TEXT("SM_mdel_steamgate01_a_00") },
		{ 14, TEXT("SM_mdel_steamgate01_a_00_1") },
		{ 14, TEXT("SM_mdel_steamgate01_a_00_2") },
		{ 14, TEXT("SM_mdel_steamgate01_a_00_3") },
		{ 14, TEXT("SM_mdel_steamgate01_a_00_4") },
		{ 14, TEXT("SM_mdel_steamgate01_a_00_5") },
		{ 14, TEXT("SM_mdel_steamgate01_a_00_6") },
		{ 6, TEXT("SM_mdel_alpenportal02_a_00") },
		{ 6, TEXT("SM_mdel_alpenportal01_a_00") },
		{ 6, TEXT("SM_mdel_alpenportal01_a_00_1") },
		{ 6, TEXT("SM_mdel_alpenportal01_a_00_2") },
		{ 6, TEXT("SM_mdel_alpenportal01_a_00_3") },
		{ 6, TEXT("SM_mdel_alpenportal01_a_00_4") },
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
		{ 3, 463, 345, 465, 349, 900.f },
		{ 17, 477, 249, 479, 259, 900.f },
		{ 17, 50, 450, 52, 465, 900.f },
		{ 17, 100, 223, 108, 225, 900.f },
		{ 9, 43, 458, 45, 462, 900.f },
		{ 9, 458, 43, 460, 47, 900.f },
		{ 2, 121, 454, 125, 456, 1248.f },
		{ 2, 159, 51, 164, 53, 1248.f },
		{ 2, 460, 80, 462, 84, 1248.f },
		{ 2, 363, 460, 365, 464, 1248.f },
		{ 15, 403, 490, 423, 495, 2930.f },
		{ 16, 83, 49, 89, 51, 948.f },
		{ 16, 145, 477, 147, 482, 979.f },
		{ 16, 464, 91, 469, 93, 1087.f },
		{ 13, 34, 128, 36, 133, 1005.f },
		{ 13, 481, 316, 483, 321, 900.f },
		{ 13, 147, 485, 152, 487, 900.f },
		{ 7, 90, 29, 95, 31, 900.f },
		{ 7, 27, 353, 29, 358, 900.f },
		{ 7, 480, 134, 485, 136, 900.f },
		{ 7, 389, 24, 394, 26, 900.f },
		{ 18, 39, 75, 41, 80, 1515.f },
		{ 5, 193, 477, 198, 479, 2618.f },
		{ 12, 493, 67, 495, 85, 2627.f },
		{ 4, 187, 471, 191, 473, 2618.f },
		{ 8, 127, 276, 131, 278, 900.f },
		{ 8, 118, 235, 122, 237, 900.f },
		{ 1, 488, 349, 490, 353, 1248.f },
		{ 1, 21, 343, 23, 347, 900.f },
		{ 1, 44, 394, 48, 398, 1517.f },
		{ 0, 41, 384, 43, 388, 900.f },
		{ 0, 144, 25, 148, 27, 900.f },
		{ 0, 366, 27, 370, 29, 1517.f },
		{ 0, 441, 482, 445, 484, 937.f },
		{ 14, 21, 81, 23, 91, 2671.f },
		{ 6, 337, 35, 342, 37, 941.f },
		{ 6, 66, 273, 68, 277, 947.f },
		{ 11, 43, 458, 45, 462, 900.f },
		{ 11, 458, 43, 460, 47, 900.f },
		{ 10, 127, 276, 131, 278, 900.f },
		{ 10, 118, 235, 122, 237, 900.f },
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
