// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEacFrame.cpp — кодек кадров канала клиент ↔ сайдкар EAC (CONTRACT.md §3). Описание — в заголовке.
#include "AsdaEacFrame.h"

namespace AsdaEacFrame
{
	// Дописать u16 little-endian.
	static void PutU16(TArray<uint8>& Out, uint16 V)
	{
		Out.Add((uint8)(V & 0xFF));
		Out.Add((uint8)(V >> 8));
	}

	// Прочитать u16 little-endian.
	static uint16 GetU16(const uint8* P)
	{
		return (uint16)(P[0] | (P[1] << 8));
	}

	// Строка → ASCII-байты (false — есть символ вне 0x20..0x7E: PUID и JWT по определению печатный ASCII).
	static bool ToAscii(const FString& S, TArray<uint8>& Out)
	{
		Out.Reset(S.Len());
		for (TCHAR C : S)
		{
			if (C < 0x20 || C > 0x7E) { return false; }
			Out.Add((uint8)C);
		}
		return true;
	}

	bool Encode(uint8 Type, const uint8* Payload, int32 PayloadLen, TArray<uint8>& Out)
	{
		if (PayloadLen < 0 || (PayloadLen > 0 && !Payload)) { return false; }
		const int32 Len = 1 + PayloadLen;                       // len = type + payload
		if (Len > AsdaEacWire::kMaxLen) { return false; }
		Out.Reserve(Out.Num() + 2 + Len);
		PutU16(Out, (uint16)Len);
		Out.Add(Type);
		if (PayloadLen > 0) { Out.Append(Payload, PayloadLen); }
		return true;
	}

	bool BuildReg(const FString& Puid, const FString& IdToken, uint8 Platform, TArray<uint8>& Out)
	{
		TArray<uint8> P, T;
		if (Puid.IsEmpty() || IdToken.IsEmpty() || !ToAscii(Puid, P) || !ToAscii(IdToken, T)) { return false; }
		if (P.Num() > 0xFFFF || T.Num() > 0xFFFF) { return false; }
		TArray<uint8> Payload;
		Payload.Reserve(2 + 2 + P.Num() + 2 + T.Num());
		Payload.Add(AsdaEacWire::kRegVer);
		Payload.Add(Platform);
		PutU16(Payload, (uint16)P.Num()); Payload.Append(P);
		PutU16(Payload, (uint16)T.Num()); Payload.Append(T);
		return Encode(AsdaEacWire::kReg, Payload.GetData(), Payload.Num(), Out);
	}

	bool BuildAcMsg(const uint8* Data, int32 Len, TArray<uint8>& Out)
	{
		if (!Data || Len < 1 || Len > AsdaEacWire::kMaxAcMsg) { return false; }
		return Encode(AsdaEacWire::kAcMsg, Data, Len, Out);
	}

	void BuildPing(TArray<uint8>& Out)
	{
		Encode(AsdaEacWire::kPing, nullptr, 0, Out);
	}

	bool BuildBind(uint8 ServerKind, const uint8* Bind32, TArray<uint8>& Out)
	{
		if (!Bind32) { return false; }
		uint8 Payload[1 + 32];
		Payload[0] = ServerKind;
		FMemory::Memcpy(Payload + 1, Bind32, 32);
		const bool bOk = Encode(AsdaEacWire::kBind, Payload, sizeof(Payload), Out);
		FMemory::Memzero(Payload, sizeof(Payload));
		return bOk;
	}

	bool ParseBind(const TArray<uint8>& Payload, uint8& OutKind, uint8 OutBind32[32])
	{
		if (Payload.Num() != 1 + 32) { return false; }
		OutKind = Payload[0];
		FMemory::Memcpy(OutBind32, Payload.GetData() + 1, 32);
		return true;
	}

	bool ParseBindResult(const TArray<uint8>& Payload, uint8& OutStatus)
	{
		if (Payload.Num() != 1) { return false; }
		OutStatus = Payload[0];
		return true;
	}

	bool ParseRegResult(const TArray<uint8>& Payload, uint8& OutStatus)
	{
		if (Payload.Num() != 1) { return false; }
		OutStatus = Payload[0];
		return true;
	}

	bool ParseKick(const TArray<uint8>& Payload, uint8& OutOur, uint8& OutEos, FString& OutDetails)
	{
		if (Payload.Num() < 4) { return false; }
		const int32 N = GetU16(Payload.GetData() + 2);
		if (4 + N != Payload.Num()) { return false; }
		OutOur = Payload[0];
		OutEos = Payload[1];
		OutDetails.Reset();
		if (N > 0)
		{
			const FUTF8ToTCHAR Conv((const ANSICHAR*)Payload.GetData() + 4, N);
			OutDetails = FString::ConstructFromPtrSize(Conv.Get(), Conv.Length());
		}
		return true;
	}

	bool ParseReg(const TArray<uint8>& Payload, uint8& OutVer, uint8& OutPlatform, FString& OutPuid, FString& OutIdToken)
	{
		const int32 L = Payload.Num();
		if (L < 4) { return false; }
		OutVer = Payload[0];
		OutPlatform = Payload[1];
		int32 Pos = 2;
		const int32 NP = GetU16(Payload.GetData() + Pos); Pos += 2;
		if (Pos + NP + 2 > L) { return false; }
		OutPuid = FString::ConstructFromPtrSize((const ANSICHAR*)Payload.GetData() + Pos, NP); Pos += NP;
		const int32 NT = GetU16(Payload.GetData() + Pos); Pos += 2;
		if (Pos + NT != L) { return false; }
		OutIdToken = FString::ConstructFromPtrSize((const ANSICHAR*)Payload.GetData() + Pos, NT);
		return true;
	}

	void FDecoder::Feed(const uint8* Data, int32 Len)
	{
		if (bError || !Data || Len <= 0) { return; }
		// Сжать разобранное, чтобы накопитель не рос бесконечно на долгой сессии.
		if (Off > 0 && Off >= Buf.Num() / 2)
		{
			Buf.RemoveAt(0, Off, EAllowShrinking::No);
			Off = 0;
		}
		Buf.Append(Data, Len);
	}

	bool FDecoder::Next(FFrame& Out)
	{
		if (bError) { return false; }
		const int32 Avail = Buf.Num() - Off;
		if (Avail < 2) { return false; }
		const int32 Len = GetU16(Buf.GetData() + Off);
		if (Len < 1 || Len > AsdaEacWire::kMaxLen) { bError = true; return false; }   // len=0 / >4096 — поток испорчен
		if (Avail < 2 + Len) { return false; }
		Out.Type = Buf[Off + 2];
		Out.Payload.Reset(Len - 1);
		if (Len > 1) { Out.Payload.Append(Buf.GetData() + Off + 3, Len - 1); }
		Off += 2 + Len;
		if (Off == Buf.Num()) { Buf.Reset(); Off = 0; }
		return true;
	}
}
