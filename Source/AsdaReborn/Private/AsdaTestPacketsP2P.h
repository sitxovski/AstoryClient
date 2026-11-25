// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ФИКСТУРЫ ТЕЛ ПАКЕТОВ ОБМЕНА МЕЖДУ ИГРОКАМИ ДЛЯ САМОТЕСТОВ (R5-c, волна дублей, 12.11.2026). [ПОРТ]
//   Аудит и отчёт: `Reports/refactor_2026-11/r5/c/AUDIT.md`, `REPORT.md`. Продолжение `AsdaTestPackets.h`
//   (отдельный файл — ради энкодера cp1251, который остальным самотестам не нужен).
//
//   ЗАЧЕМ. Самотесты обмена (`AsdaExchangeP2PSelfTest*.cpp`, четыре файла) держали по своей дословной копии тел 5283,
//   5285, 5289, 5292 и ожидаемых тел 5288, 5290, 5293 (20 копий на 7 раскладок). Здесь — одна раскладка байт на опкод.
//   Значения полей — аргументы: каждый самотест кормит разбор и сверяет отправки теми же байтами, что и раньше.
//   Только для самотестов; пакеты не шлются — тела идут в `UAsdaNetSubsystem::DebugFeedPacket` и в сверку перехвата.
#pragma once

#include "CoreMinimal.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::EncodeCp1251 — единственный энкодер cp1251 порта

namespace AsdaTestPacketsP2P
{
	// Тело 5283 (24 б): [0] код, [1..2] userNum инициатора, [3..22] имя cp1251, [23] тип.
	inline TArray<uint8> Body5283(int32 Code, int32 From, const FString& Name, int32 Type)
	{
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = (uint8)Code;
		AsdaBytes::WrI16(B.GetData() + 1, From);
		FAsdaLoginData::EncodeCp1251(Name, B.GetData() + 3, 19);
		B[23] = (uint8)Type;
		return B;
	}

	// Тело 5285 (50 б): [0] код, [1] тип, [2..5] windowIndex, [6..7] userNum 1, [8..27] имя 1, [28..29] userNum 2, [30..49] имя 2.
	inline TArray<uint8> Body5285(int32 Code, int32 Type, int32 Idx, int32 N1, const FString& S1, int32 N2, const FString& S2)
	{
		TArray<uint8> B; B.SetNumZeroed(50);
		B[0] = (uint8)Code; B[1] = (uint8)Type;
		AsdaBytes::WrI32(B.GetData() + 2, Idx);
		AsdaBytes::WrI16(B.GetData() + 6, N1);
		FAsdaLoginData::EncodeCp1251(S1, B.GetData() + 8, 19);
		AsdaBytes::WrI16(B.GetData() + 28, N2);
		FAsdaLoginData::EncodeCp1251(S2, B.GetData() + 30, 19);
		return B;
	}

	// Тело 5289 (8 б): [0] код, [1] invType, [2..3] invSlot, [4..7] кол-во.
	inline TArray<uint8> Body5289(int32 Code, int32 Type, int32 Slot, int32 Count)
	{
		TArray<uint8> B; B.SetNumZeroed(8);
		B[0] = (uint8)Code; B[1] = (uint8)Type;
		AsdaBytes::WrI16(B.GetData() + 2, Slot);
		AsdaBytes::WrI32(B.GetData() + 4, Count);
		return B;
	}

	// Ожидаемое тело 5288 (12 б).
	inline TArray<uint8> Want5288(int32 Idx, int32 TraySlot, int32 Type, int32 Slot, int32 Count)
	{
		TArray<uint8> B; B.SetNumZeroed(12);
		AsdaBytes::WrI32(B.GetData(), Idx); B[4] = (uint8)TraySlot; B[5] = (uint8)Type;
		AsdaBytes::WrI16(B.GetData() + 6, Slot); AsdaBytes::WrI32(B.GetData() + 8, Count);
		return B;
	}

	// Ожидаемое тело 5290 (11 б).
	inline TArray<uint8> Want5290(int32 Idx, int32 Type, int32 Slot, int32 Count)
	{
		TArray<uint8> B; B.SetNumZeroed(11);
		AsdaBytes::WrI32(B.GetData(), Idx); B[4] = (uint8)Type;
		AsdaBytes::WrI16(B.GetData() + 5, Slot); AsdaBytes::WrI32(B.GetData() + 7, Count);
		return B;
	}

	// Тело 5292 (261 б) — раскладка сервера `CharExchangeFixReq` (GameServer.c:89195-89212, `sub_4ED270`): [0..3] windowIndex,
	//   [4] действие, [5] lock, 5 × 51 с [6]: код @0 (пустая −1), кол-во @7, заточка @11, «+N%» @14.
	inline TArray<uint8> Body5292(int32 Act, int32 Lock, int32 Code0 = -1, int32 Count0 = 0)
	{
		TArray<uint8> B; B.SetNumZeroed(261);
		AsdaBytes::WrI32(B.GetData(), 70);
		B[4] = (uint8)Act; B[5] = (uint8)Lock;
		for (int32 k = 0; k < 5; ++k) { AsdaBytes::WrI32(B.GetData() + 6 + 51 * k, -1); }
		AsdaBytes::WrI32(B.GetData() + 6, Code0);
		AsdaBytes::WrI32(B.GetData() + 6 + 7, Count0);
		return B;
	}

	// Ожидаемое тело 5293 (4 б).
	inline TArray<uint8> Want5293(int32 Idx) { TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI32(B.GetData(), Idx); return B; }
}
