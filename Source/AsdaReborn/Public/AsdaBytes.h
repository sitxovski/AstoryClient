// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ЕДИНЫЕ ЧИТАЛКИ/ПИСАЛКИ СЫРЫХ ЗАПИСЕЙ ПОРТА (little-endian + hex-колонка `DataHex` + ASCII-поле).
//   Аудит дублей: `Reports/code_dedup_audit_2.md`, кластер D4/D4b — семь наборов одних и тех же
//   функций жили по подсистемам (`RawByteAt`/`SkU8`/`SwRdI16`/`FrRdI32`/`RdI16`/`RawI32`/`HexToBytes`…),
//   плюс лямбды и десятки открытых выражений `(int32)(int16)(B[o] | (B[o+1] << 8))` в сетевой подсистеме.
//
// ★ ЗАЧЕМ ОДНА ТОЧКА. Именно здесь живут СМЕЩЕНИЯ, на которых порт уже горел: `field68` эталона —
//   ДЕСЯТИЧНОЕ имя (68 = 0x44), а порт читал `0x68` (= 104). «Своя читалка» в каждом файле = свой
//   шанс off-by-one и своя трактовка битой записи. Теперь правило одно на всех:
//     • чтение ЗА границей буфера ⇒ 0 (безопасные `Safe*` по `TArray`) либо −1 (hex-байт «нет такого»);
//     • hex-строка разбирается строго: не-hex символ ⇒ −1 / false, а не «тихий 0».
//
// Заголовок header-only (всё `FORCEINLINE`): ни .cpp, ни экспорта не требуется, включается и из
//   `AsdaReborn`, и из любого другого модуля порта.
#pragma once
#include "CoreMinimal.h"

namespace AsdaBytes
{
	// ── Little-endian из сырого указателя (вызывающий уже проверил границы) ──────────────────────
	// i16 со знаком. Так эталон читает коды предметов/слоты во ВСЕХ 57-байтовых записях.
	FORCEINLINE int32 RdI16(const uint8* P)
	{
		return (int32)(int16)((uint16)P[0] | ((uint16)P[1] << 8));
	}

	// i32 со знаком (НЕвыровненный: в записи слота `*(int*)(pItemSlot + 9)` лежит именно так).
	FORCEINLINE int32 RdI32(const uint8* P)
	{
		return (int32)((uint32)P[0] | ((uint32)P[1] << 8) | ((uint32)P[2] << 16) | ((uint32)P[3] << 24));
	}

	// u16 без знака (длины, индексы пользователей, номера каналов).
	FORCEINLINE uint32 RdU16(const uint8* P)
	{
		return (uint32)P[0] | ((uint32)P[1] << 8);
	}

	// u32 без знака (userIndex, деньги, тайминги).
	FORCEINLINE uint32 RdU32(const uint8* P)
	{
		return (uint32)P[0] | ((uint32)P[1] << 8) | ((uint32)P[2] << 16) | ((uint32)P[3] << 24);
	}

	// float IEEE-754 из 4 байт (побайтовая копия; порт и эталон — x86/x64 little-endian). ★ N6 (В1 дедупа №2): заменяет
	//   копии `float V; Memcpy(&V, P, 4)` в разборах 4006/4008/4024 и в самотестах.
	FORCEINLINE float RdF32(const uint8* P)
	{
		float V;
		FMemory::Memcpy(&V, P, 4);
		return V;
	}

	// Запись i16/i32 в том же порядке байт (сборка тел пакетов).
	FORCEINLINE void WrI16(uint8* P, int32 V)
	{
		const uint16 U = (uint16)(int16)V;
		P[0] = (uint8)(U & 0xFF); P[1] = (uint8)(U >> 8);
	}

	FORCEINLINE void WrI32(uint8* P, int32 V)
	{
		const uint32 U = (uint32)V;
		P[0] = (uint8)(U & 0xFF);         P[1] = (uint8)((U >> 8) & 0xFF);
		P[2] = (uint8)((U >> 16) & 0xFF); P[3] = (uint8)((U >> 24) & 0xFF);
	}

	// ── Безопасные по границам (запись может прийти короткой/битой) ──────────────────────────────
	// Вне границ ⇒ 0. Именно так трактуют «нет поля» все прежние копии (`RawU8`/`RawI16`/`RawI32`).
	FORCEINLINE int32 SafeU8(const TArray<uint8>& R, int32 Off)
	{
		return R.IsValidIndex(Off) ? (int32)R[Off] : 0;
	}

	FORCEINLINE int32 SafeI16(const TArray<uint8>& R, int32 Off)
	{
		return R.IsValidIndex(Off + 1) ? RdI16(R.GetData() + Off) : 0;
	}

	FORCEINLINE int32 SafeI32(const TArray<uint8>& R, int32 Off)
	{
		return R.IsValidIndex(Off + 3) ? RdI32(R.GetData() + Off) : 0;
	}

	// ── Колонка `DataHex` импортированных DataTable (вся запись таблицы 1:1, 2 символа на байт) ───
	// Один hex-символ → значение; не-hex ⇒ −1.
	FORCEINLINE int32 HexNibble(TCHAR C)
	{
		if (C >= TEXT('0') && C <= TEXT('9')) { return C - TEXT('0'); }
		if (C >= TEXT('a') && C <= TEXT('f')) { return C - TEXT('a') + 10; }
		if (C >= TEXT('A') && C <= TEXT('F')) { return C - TEXT('A') + 10; }
		return -1;
	}

	// Байт записи по СМЕЩЕНИЮ В БАЙТАХ. −1, если строка короткая или символы не hex
	//   (вызывающий трактует −1 как «условие не выполнено» / «поля нет»).
	FORCEINLINE int32 HexByteAt(const FString& Hex, int32 Off)
	{
		const int32 I = Off * 2;
		if (Off < 0 || I + 1 >= Hex.Len()) { return -1; }
		const int32 Hi = HexNibble(Hex[I]), Lo = HexNibble(Hex[I + 1]);
		return (Hi < 0 || Lo < 0) ? -1 : ((Hi << 4) | Lo);
	}

	// u16/i32 из hex-колонки (little-endian). Битьё/обрыв ⇒ 0, как во всех прежних копиях.
	FORCEINLINE int32 HexU16At(const FString& Hex, int32 Off)
	{
		const int32 B0 = HexByteAt(Hex, Off), B1 = HexByteAt(Hex, Off + 1);
		return (B0 < 0 || B1 < 0) ? 0 : (B0 | (B1 << 8));
	}

	FORCEINLINE int32 HexI32At(const FString& Hex, int32 Off)
	{
		int32 V = 0;
		for (int32 i = 3; i >= 0; --i)
		{
			const int32 B = HexByteAt(Hex, Off + i);
			if (B < 0) { return 0; }
			V = (V << 8) | B;
		}
		return V;
	}

	// Вся hex-строка → байты. false при пустой/битой строке (Out при этом не используется).
	FORCEINLINE bool HexToBytes(const FString& Hex, TArray<uint8>& Out)
	{
		const int32 N = Hex.Len() / 2;
		if (N <= 0) { return false; }
		Out.SetNumUninitialized(N);
		for (int32 i = 0; i < N; ++i)
		{
			const int32 H = HexNibble(Hex[2 * i]), L = HexNibble(Hex[2 * i + 1]);
			if (H < 0 || L < 0) { return false; }
			Out[i] = (uint8)((H << 4) | L);
		}
		return true;
	}

	// ── Строковое поле фиксированной записи ──────────────────────────────────────────────────────
	// Первая ASCII-строка (имя текстуры/тега) со смещения Off: нуль-терминатор обрывает, непечатный
	//   символ ⇒ поле считается НЕ строковым и возвращается пусто (так делали обе прежние копии).
	FORCEINLINE FString AsciiFieldAt(const TArray<uint8>& Rec, int32 Off, int32 MaxLen = 32)
	{
		if (!Rec.IsValidIndex(Off)) { return FString(); }
		FString S;
		for (int32 i = Off; i < Rec.Num() && i < Off + MaxLen; ++i)
		{
			const uint8 C = Rec[i];
			if (C == 0) { break; }
			if (C < 32 || C > 126) { return FString(); }
			S.AppendChar((TCHAR)C);
		}
		return S;
	}

	// ★ N7 (дедуп №2, В2-A, 11.10.2026): ANSI-поле фиксированной длины из тела пакета → FString.
	//   Читает не больше MaxLen байт (нуль-терминатор обрывает раньше; поле может занимать ВСЕ MaxLen байт
	//   без терминатора — так приходит IP «198.51.100.177» в 14 байтах 1021), перекодирует как прежние
	//   копии (`ANSI_TO_TCHAR`) и срезает пробелы по краям. Единая точка разбора IP канал-/гейм-сервера
	//   (1021, портал 5xxx, 6015, 20-байтовый перенос, 4055, свиток 5459) — вызывающий уже проверил длину тела.
	FORCEINLINE FString AnsiFieldTrim(const uint8* P, int32 MaxLen)
	{
		int32 N = 0;                                   // фактическая длина строки в поле (до нуля или MaxLen)
		while (N < MaxLen && P[N] != 0) { ++N; }
		TArray<ANSICHAR, TInlineAllocator<64>> Buf;    // копия поля + гарантированный нуль-терминатор
		Buf.SetNumZeroed(N + 1);
		FMemory::Memcpy(Buf.GetData(), P, N);
		return FString(ANSI_TO_TCHAR(Buf.GetData())).TrimStartAndEnd();
	}
}
