// Copyright (c) 2026 @sitxovski (Discord)
// ★ R5-fix D1 (13.11.2026) [ИСПРАВЛЕНИЕ ПОРТА]: РАЗБОР КЛЮЧА ПРИЁМА XyT ИЗ HEX-ФАЙЛА — одна точка на оба читателя
//   (`UAsdaNetSubsystem::ReloadXytRecvKeyNow` и опрос `TickXytRecvKey`).
//   Причина: обе копии разбирали символы через `FParse::HexDigit`, а она на не-hex символ возвращает 0, а не отрицательное.
//   Проверка «Hi < 0 || Lo < 0» не срабатывала никогда: мусор в файле (обрезанная запись, перевод строки внутри, чужой текст)
//   молча давал ключ с нулевыми полубайтами, и приём шифровался неверным ключом без единой строки в журнале.
//   В оригинале этого кода нет: чтение ключа из файла — наша отладочная связка с чужой сборкой XyT (в Shipping путь пуст).
//   Ключ `asda.XyT.RecvKeyStrictHex`: 1 — не-hex символ = отказ (ключ не меняется, предупреждение в журнале); 0 — как было.
#pragma once

#include "CoreMinimal.h"

namespace AsdaXytKeyHex
{
	// Разбор первых 64 hex-символов Hex в 32 байта ключа. bStrict = true — не-hex символ даёт false и позицию пары в OutBadPos
	//   (ключ при этом использовать нельзя); bStrict = false — прежнее поведение: не-hex символ читается как 0, отказа нет.
	//   Строка короче 64 символов — false, OutBadPos = длина строки (в обоих режимах; раньше длину проверял вызывающий).
	bool ParseWith(const FString& Hex, bool bStrict, uint8 OutKey[32], int32& OutBadPos);

	// То же в режиме ключа `asda.XyT.RecvKeyStrictHex` (в Shipping — всегда строго).
	bool Parse(const FString& Hex, uint8 OutKey[32], int32& OutBadPos);

	// Значение ключа `asda.XyT.RecvKeyStrictHex` (для самотеста и журнала).
	bool IsStrict();
}
