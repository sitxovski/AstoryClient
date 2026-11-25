// Copyright (c) 2026 @sitxovski (Discord)
// AsdaServerAddress.cpp — правило адреса сервера (см. шапку AsdaServerAddress.h).
#include "Login/AsdaServerAddress.h"

// Строгий разбор IPv4-литерала «a.b.c.d» (ровно 4 десятичных октета 0..255, 1..3 цифры, без знаков и пробелов).
//   Свой разбор вместо FIPv4Address::Parse: модуль AsdaReborn не зависит от Networking, а правило подмены должно
//   быть однозначным (строка из пакета либо литерал, либо имя хоста). true — Out = октеты по порядку.
static bool AsdaParseIPv4Strict(const FString& S, uint8 Out[4])
{
	int32 Part = 0, Digits = 0, Val = 0;
	for (int32 i = 0; i <= S.Len(); ++i)
	{
		const TCHAR C = (i < S.Len()) ? S[i] : TEXT('.');   // конец строки = завершение последнего октета
		if (C >= TEXT('0') && C <= TEXT('9'))
		{
			if (++Digits > 3) { return false; }
			Val = Val * 10 + (C - TEXT('0'));
			continue;
		}
		if (C != TEXT('.') || Digits == 0 || Val > 255 || Part >= 4) { return false; }
		Out[Part++] = (uint8)Val;
		Digits = 0; Val = 0;
	}
	return Part == 4;
}

bool AsdaServerAddr::IsNonPublicHost(const FString& Host)
{
	const FString H = Host.TrimStartAndEnd();
	if (H.Equals(TEXT("localhost"), ESearchCase::IgnoreCase)) { return true; }   // имя петли
	uint8 Oct[4];
	if (!AsdaParseIPv4Strict(H, Oct)) { return false; }  // имя хоста — публичное по определению (резолвит DNS)
	const uint8 O1 = Oct[0];
	const uint8 O2 = Oct[1];
	if (O1 == 10)                          { return true; }   // RFC1918 10.0.0.0/8
	if (O1 == 172 && (O2 & 0xF0) == 16)    { return true; }   // RFC1918 172.16.0.0/12
	if (O1 == 192 && O2 == 168)            { return true; }   // RFC1918 192.168.0.0/16
	if (O1 == 127)                         { return true; }   // loopback 127.0.0.0/8
	if (O1 == 169 && O2 == 254)            { return true; }   // link-local 169.254.0.0/16
	if (O1 == 0)                           { return true; }   // 0.0.0.0/8 «эта сеть» — адресом назначения не бывает
	return false;
}

FString AsdaServerAddr::MapPacketHost(const FString& Host, bool* OutSubstituted)
{
	const bool bSub = IsNonPublicHost(Host);
	if (OutSubstituted) { *OutSubstituted = bSub; }
	return bSub ? FString(LoginHost()) : Host.TrimStartAndEnd();
}
