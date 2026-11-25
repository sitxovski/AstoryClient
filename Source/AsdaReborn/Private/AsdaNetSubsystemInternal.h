// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystemInternal.h — ★ 22.10.2026 (разнос AsdaNetSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaNetSubsystem*.cpp.
//   Только то, что нужно нескольким частям: категория журнала LogAsdaNet (один экземпляр — DEFINE_LOG_CATEGORY в
//   AsdaNetSubsystem.cpp) и чистые static-хелперы без состояния (текст перенесён байт в байт из AsdaNetSubsystem.cpp;
//   каждая часть получает свою копию функции — состояния у них нет, поведение одно). Подключать ПОСЛЕ #include пролога.
#pragma once

#include "CoreMinimal.h"
#include "AsdaNetSubsystem.h"
#include "AsdaPacketWriter.h"
#include "AsdaAntiCheatSubsystem.h"
#include "Engine/GameInstance.h"

// Категория журнала сети: объявление; определение — AsdaNetSubsystem.cpp (до разноса там был DEFINE_LOG_CATEGORY_STATIC).
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaNet, Log, All);


// ★ EAC Э1: подсистема клиентского античита того же GameInstance (nullptr — нет GameInstance).
static UAsdaAntiCheatSubsystem* AsdaEacOf(const UGameInstanceSubsystem* Self)
{
	UGameInstance* GI = Self ? Self->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaAntiCheatSubsystem>() : nullptr;
}

// ★ ПРОВЕРКА РАСКЛАДКИ ТЕЛА ПЕРЕД ОТПРАВКОЙ (хвост волны 3 п.10, замечание ревизора). Запись писателя за
//   границу тела — ошибка программиста: `ensure` в Shipping пуст, и без этой проверки пакет ушёл бы с
//   нетронутой «дырой» (байтом заполнения) на месте поля. Каждый отправитель на `FAsdaPacketWriter` зовёт
//   помощник ПЕРЕД конвертом и при true возвращает то же, что при отказе отправки (false), не трогая
//   сокет, счётчик секрета и счётчик обрывов `TrackSend` (сеть тут ни при чём). На валидных раскладках
//   `IsOk()` всегда true — провод не меняется (доказано стендом `asda.Net.PacketStand`).
static bool AsdaPacketLayoutBroken(const FAsdaPacketWriter& W, uint16 Opcode, const TCHAR* Sender)
{
	if (W.IsOk()) { return false; }
	UE_LOG(LogAsdaNet, Error,
		TEXT("%s: опкод %u НЕ отправлен — ошибка раскладки тела (запись за границу %d б, FAsdaPacketWriter::IsOk() = false)"),
		Sender, (uint32)Opcode, W.Num());
	return true;
}

// ★ Рантайм-доказательство: тело пакета целиком в hex + печатаемый ASCII-срез.
//   Нужно, чтобы спор «какой номер канала реально уехал/приехал» решался цифрами, а не догадками.
static FString HexDumpBody(const uint8* B, int32 N)
{
	if (!B || N <= 0) { return TEXT("<пусто>"); }
#if UE_BUILD_SHIPPING
	// ★ 19.10.2026 (ревизия безопасности журналов, М9): все вызовы — тела ЧАТА (5086/6560/4120 свои, 5087/6561 чужие): в Shipping
	//   содержимое в журнал не идёт, только длина.
	return FString::Printf(TEXT("<тело скрыто: %d б>"), N);
#else
	const int32 Lim = FMath::Min(N, 96);
	FString Hex, Ascii;
	for (int32 i = 0; i < Lim; ++i)
	{
		Hex += FString::Printf(TEXT("%02X "), B[i]);
		Ascii += (B[i] >= 32 && B[i] < 127) ? FString::Chr((TCHAR)B[i]) : FString(TEXT("."));
	}
	if (N > Lim) { Hex += FString::Printf(TEXT("… (+%d б)"), N - Lim); }
	return FString::Printf(TEXT("%s| «%s»"), *Hex, *Ascii);
#endif
}
