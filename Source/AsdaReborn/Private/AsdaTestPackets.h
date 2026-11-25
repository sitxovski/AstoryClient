// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ФИКСТУРЫ ТЕЛ ПАКЕТОВ ДЛЯ САМОТЕСТОВ (дедуп №2, В1-C, F18, 11.10.2026). [ПОРТ]
//   План — `Reports/dedup_audit2/00_PLAN.md` §5 В1, находка F18 — `03_fx_render.md`, отчёт — `wave1_C.md`.
//
//   ЗАЧЕМ. Тело записи 4013 (спаун моба, шаг 174) самотесты собирали своими копиями, и копии разошлись (состояние @8
//   1 против 0, скорость 10 против 100). Здесь — ОДНА раскладка байт; состояние и скорость — аргументы, поэтому каждый
//   самотест кормит мир прежними значениями (вынос, а не правка; какое состояние «правильное» — вопрос В2).
//   Только для самотестов (вне Shipping); пакеты не шлются — тело идёт в `UAsdaNetSubsystem::DebugFeedPacket`.
#pragma once

#include "CoreMinimal.h"
#include "AsdaBytes.h"

namespace AsdaTestPackets
{
	// Длина записи 4013 (шаг 174, эталон Recv_MobSpawn).
	inline constexpr int32 kMobSpawn4013Size = 174;

	// Запись 4013: rid@0 (i16), тип@2 (i16), состояние спауна@8 (u8), HP@9 (i32 = 100), знаменатель скорости@13 (i16),
	//   тайлы cur@19/21 и dst@23/25 (i16, оба = `Tile`), гейт `*(u32)(pkt+27)` = 1. Прочие байты — нули.
	inline TArray<uint8> MobSpawn4013(int32 Rid, int32 Type, const FIntPoint& Tile, uint8 State, int32 Speed)
	{
		TArray<uint8> B; B.SetNumZeroed(kMobSpawn4013Size);
		AsdaBytes::WrI16(&B[0], (int16)Rid);
		AsdaBytes::WrI16(&B[2], (int16)Type);
		B[8] = State;
		AsdaBytes::WrI32(&B[9], 100);
		AsdaBytes::WrI16(&B[13], (int16)Speed);
		AsdaBytes::WrI16(&B[19], (int16)Tile.X); AsdaBytes::WrI16(&B[21], (int16)Tile.Y);
		AsdaBytes::WrI16(&B[23], (int16)Tile.X); AsdaBytes::WrI16(&B[25], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[27], 1);
		return B;
	}

	// ★ 15.10.2026 (волна mobpos_swing): та же запись 4013, но cur и dst раздельно (живой случай — «действие 2: 164→170»).
	inline TArray<uint8> MobSpawn4013Move(int32 Rid, int32 Type, const FIntPoint& Cur, const FIntPoint& Dst, uint8 State, int32 Speed)
	{
		TArray<uint8> B = MobSpawn4013(Rid, Type, Cur, State, Speed);
		AsdaBytes::WrI16(&B[23], (int16)Dst.X); AsdaBytes::WrI16(&B[25], (int16)Dst.Y);
		return B;
	}

	// ── ★ R5-c (волна дублей, 12.11.2026): тела, которые самотесты держали дословными копиями (аудит —
	//   `Reports/refactor_2026-11/r5/c/AUDIT.md`): 4003 — 11 копий, 5251 — 5, 4029 — 4, 4053 — 3. Раскладка байт та же;
	//   в самотесте на месте копии — её комментарий и `using AsdaTestPackets::Body…;`. ──

	// Тело 4003 (9 б): userNum, тайлы, состояние, показ шлема.
	inline TArray<uint8> Body4003(int32 Me)
	{
		TArray<uint8> B; B.SetNumZeroed(9);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)Me);
		AsdaBytes::WrI16(B.GetData() + 2, 256);
		AsdaBytes::WrI16(B.GetData() + 4, 256);
		B[8] = 1;
		return B;
	}

	// Тело 4029 (49 б, `Recv_CastState@0xF41620`): @0 i16 атакующий, @2 i16 rid, @4 i32 HP цели, @8 i32[4] очередь, @25 i16[4] (−1).
	inline TArray<uint8> Body4029(int32 AtkU, int32 Rid, int32 Hp, const TArray<int32>& Q)
	{
		TArray<uint8> B; B.SetNumZeroed(49);
		AsdaBytes::WrI16(&B[0], (int16)AtkU);
		AsdaBytes::WrI16(&B[2], (int16)Rid);
		AsdaBytes::WrI32(&B[4], Hp);
		for (int32 i = 0; i < 4 && i < Q.Num(); ++i) { AsdaBytes::WrI32(&B[8 + 4 * i], Q[i]); }
		for (int32 i = 0; i < 4; ++i) { AsdaBytes::WrI16(&B[25 + 2 * i], -1); }
		return B;
	}

	// 4053 (4 б): @0 i16 умерший, @2 i16 убийца.
	inline TArray<uint8> Body4053(int32 Uidx, int32 Killer)
	{
		TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(&B[0], (int16)Uidx); AsdaBytes::WrI16(&B[2], (int16)Killer); return B;
	}

	// 5251 — изученные навыки (34 б на запись: код, уровень, «можно вкачивать», откат).
	inline TArray<uint8> Body5251(const TArray<int32>& Codes)
	{
		TArray<uint8> B; B.SetNumZeroed(34 * Codes.Num());
		for (int32 i = 0; i < Codes.Num(); ++i) { uint8* P = B.GetData() + 34 * i; AsdaBytes::WrI16(P, Codes[i]); P[2] = 5; P[3] = 1; }
		return B;
	}
}
