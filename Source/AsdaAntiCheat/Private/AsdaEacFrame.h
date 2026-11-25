// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEacFrame.h — кодек кадров канала клиент ↔ сайдкар EAC (TCP 15901 внутри AsdaSecure), CONTRACT.md §3.
//   Кадр (little-endian): u16 len (длина type+payload, 1..4096) · u8 type · payload.
//   Чистая логика без сокетов и EOS — проверяется самотестом asda.SelfTest.Eac.
#pragma once

#include "CoreMinimal.h"
#include "AsdaEacTypes.h"

namespace AsdaEacFrame
{
	// Один разобранный кадр.
	struct FFrame
	{
		uint8 Type = 0;          // тип кадра (AsdaEacWire::k*)
		TArray<uint8> Payload;   // полезная нагрузка (без len и type)
	};

	// Собрать кадр: false — длина вне 1..4096 (кадр не дописан). Кадр ДОПИСЫВАЕТСЯ в Out.
	bool Encode(uint8 Type, const uint8* Payload, int32 PayloadLen, TArray<uint8>& Out);

	// REG: u8 ver=1 · u8 platform · u16 n + PUID ASCII · u16 n + IdToken ASCII. false — поля не ASCII / не влезли в 4096.
	bool BuildReg(const FString& Puid, const FString& IdToken, uint8 Platform, TArray<uint8>& Out);
	// ACMSG: сырые байты EAC 1..512. false — длина вне предела.
	bool BuildAcMsg(const uint8* Data, int32 Len, TArray<uint8>& Out);
	// PING: пустой.
	void BuildPing(TArray<uint8>& Out);
	// ★ ред. 3 (CONTRACT.md §8.1) BIND: u8 serverKind (2 Game, 3 Indun) · u8[32] bind игрового соединения. false — Bind == nullptr.
	bool BuildBind(uint8 ServerKind, const uint8* Bind32, TArray<uint8>& Out);
	// ★ ред. 3 разбор BIND (сторона сайдкара — самотест): false — payload не ровно 33 байта.
	bool ParseBind(const TArray<uint8>& Payload, uint8& OutKind, uint8 OutBind32[32]);
	// ★ ред. 3 разбор BIND_RESULT: false — payload не ровно 1 байт.
	bool ParseBindResult(const TArray<uint8>& Payload, uint8& OutStatus);

	// Разбор REG_RESULT: false — payload не ровно 1 байт.
	bool ParseRegResult(const TArray<uint8>& Payload, uint8& OutStatus);
	// Разбор KICK: u8 ourReason · u8 eosReason · u16 n + UTF-8 детали. false — длины не сходятся.
	bool ParseKick(const TArray<uint8>& Payload, uint8& OutOur, uint8& OutEos, FString& OutDetails);
	// Разбор REG (сторона сайдкара — нужна самотесту и стенду): false — формат нарушен.
	bool ParseReg(const TArray<uint8>& Payload, uint8& OutVer, uint8& OutPlatform, FString& OutPuid, FString& OutIdToken);

	// Потоковый разборщик (поток после расшифровки AsdaSecure может прийти любой нарезкой).
	class FDecoder
	{
	public:
		// Дописать принятые байты.
		void Feed(const uint8* Data, int32 Len);
		// Вынуть следующий целый кадр. false — целого кадра нет (IsError() == false) или поток испорчен (IsError()).
		bool Next(FFrame& Out);
		// Поток испорчен (len == 0 или len > 4096) — канал закрывать.
		bool IsError() const { return bError; }
		// Сбросить накопитель и ошибку (новое соединение).
		void Reset() { Buf.Reset(); Off = 0; bError = false; }
		// Сколько байт ждёт разбора.
		int32 Pending() const { return Buf.Num() - Off; }

	private:
		TArray<uint8> Buf;     // накопитель
		int32 Off = 0;         // сколько уже разобрано
		bool bError = false;   // поток испорчен
	};
}
