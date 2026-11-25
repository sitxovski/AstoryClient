// Copyright (c) 2026 @sitxovski (Discord)
// Внутренние общие точки боевой фазы «Битвы армий» (два .cpp: `AsdaCampWarBattleSubsystem.cpp` — модель/сеть/тик,
//   `AsdaCampWarBattleWindows.cpp` — окна). Ключи `asda.CampWar.*` определены в первом; второму — только аксессоры
//   (имена ключей в рабочий код второго файла не вписываются).
#pragma once

#include "CoreMinimal.h"

namespace AsdaCwBattle
{
	// ── Аксессоры ключей (определения — `AsdaCampWarBattleSubsystem.cpp`) ──
	bool TimerThirdDigitFix();   // Д-Б3: третья цифра таймера точки — в глиф [3]
	bool InviteFix();            // Д-Б4: 49410 — нотис 1047 и имя найденного члена
	bool HoldBarKeep();          // Д-Б7: шаг 2 тика прячет только свою полосу 49510
	bool ZoneUnionLegacy();      // ★ 31.10: 1 — прежняя зона-объединение табло/карты (накрывала верх экрана)

	// ── Корни окон (IDNum эталона) ──
	inline constexpr int32 kWinPoint = 49600;   // CAMPWAR_Point
	inline constexpr int32 kWinMap   = 49900;   // CAMPWAR_L_MAP
	inline constexpr int32 kWinFont  = 49910;   // CAMPWAR_FONT_Win
	inline constexpr int32 kWinTime  = 49940;   // CAMPWAR_Time_Win (с кнопкой 49500 «Рейд»)
	inline constexpr int32 kWinHold  = 49510;   // CAPWAR_Hold_ING
	inline constexpr int32 kWinEnd   = 49800;   // CAMPWAR_End
	inline constexpr int32 kWinAtk   = 49399;   // CAMPWAR_ATTACK
	inline constexpr int32 kWinPop   = 49400;   // CAMPWAR_ATTACK_Position

	// ── Производные ключи узлов без собственного IDNum ──
	inline constexpr int32 kAlt = 1000000;      // «второй узел» того же контрола (подложка полосы, текст кнопки, счётчик иконки)
	// Разряды окна очков: армия F (0…2), позиция J (0…5), цифра K (0…9).
	inline int32 DigitKey(int32 F, int32 J, int32 K) { return 2000000 + F * 1000 + J * 10 + K; }
	// Группа позиции разряда J армии F.
	inline int32 DigitPosKey(int32 F, int32 J) { return 2000000 + F * 1000 + 100 + J; }
	// Запятая армии F (клон 49661).
	inline int32 CommaKey(int32 F) { return 2000000 + F * 1000 + 900; }
	// Набор цифр армии F (клон 49604 `countpack`).
	inline int32 PackKey(int32 F) { return 2000000 + F * 1000 + 999; }
	// План рамки выбора 49330 (8 планов).
	inline int32 FrameKey(int32 K) { return 4933000 + K; }
	// Главный контрол таймера точки I (49950+10i при i < 5, иначе 49960+10i).
	inline int32 TimerMainKey(int32 I) { return (I < 5) ? 49950 + 10 * I : 49960 + 10 * I; }
	// NoImage[J] таймера I (J = 0 — подложка `CAMPWAR_BACK_n`, 1…4 — глифы).
	inline int32 TimerGlyphKey(int32 I, int32 J) { return TimerMainKey(I) + 1 + J; }
	// Строка R списка «Рейд»: [0] фон 49030+20r, [1…10] 49020+20r…49029+20r.
	inline int32 RowKey(int32 R, int32 K) { return (K == 0) ? 49030 + 20 * R : 49020 + 20 * R + (K - 1); }

	// Номер армии в цвет: «половинный» (+0x23E4+24f) и «полный» (+0x23F0+24f), ctor 0xC956C2…0xC95695.
	inline FVector HalfColor(int32 F) { return F == 0 ? FVector(0, 0, .5) : F == 1 ? FVector(.5, 0, 0) : F == 2 ? FVector(0, .5, 0) : FVector(1, 1, 1); }
	inline FVector FullColor(int32 F) { return F == 0 ? FVector(0, 0, 1) : F == 1 ? FVector(1, 0, 0) : F == 2 ? FVector(0, 1, 0) : FVector(1, 1, 1); }

	// ARGB эталона → FColor (sRGB).
	inline FColor Argb(uint32 V) { return FColor((V >> 16) & 0xFF, (V >> 8) & 0xFF, V & 0xFF, (V >> 24) & 0xFF); }
}
