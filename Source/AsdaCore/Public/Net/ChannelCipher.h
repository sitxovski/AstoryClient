// Copyright (c) 2026 @sitxovski (Discord)
// ChannelCipher — канальный XOR-32 AsdaStory (1:1 verified).
// Источник истины: RE/capture/decode_capture.py (декодит реальный трафик 1:1) +
//   memory network-crypto-layers. AES в игре НЕ используется — только этот слой.
//
// Алгоритм: фикс. 32-байтный ключ. На кадр размера `size`:
//   nb = (size - 3) >> 3                 // число 8-байтных блоков
//   for k in [0 .. nb*8):  frame[3 + k] ^= KEY[k % 32]
// XOR симметричен: encrypt == decrypt. Маркеры FB(0)/FE(size-1) и Size(1..2) НЕ шифруются.
// Опкод (off 7..8) ПОПАДАЕТ под шифр. Применяется ВСЕГДА после connect к серверу.
#pragma once
#include <cstdint>
#include <cstddef>

// Экспорт наружу модуля (как в PacketFrame.h): профиль ключа переключает логин-сцена из AsdaReborn.
#ifndef ASDACORE_API
#define ASDACORE_API
#endif

namespace Asda
{
	// ★ Профиль ключа: у КАЖДОЙ сборки сервера свой 32-байтный ключ (доказано живым захватом
	//   трафика клиента XyT — `Reports/xyt_traffic_capture.md` §7). Механизм шифра при этом
	//   общий, меняется только таблица ключа.
	enum class EKeyProfile : uint8_t
	{
		Default = 0,   // наш эталон (Arab_Closed сборка, ключ из декомпила AsdaStory.exe)
		XyT     = 1,   // сборка XyT — ключ восстановлен из захвата, валидирован 100 % на C→S
	};

	class ChannelCipher
	{
	public:
		// 32-байтный канальный ключ (зашит в оригинальном EXE; verified live-захватом).
		static constexpr int KEY_LEN = 32;
		static const uint8_t Key[KEY_LEN];
		// ★ Ключ сборки XyT (восстановлен known-plaintext, см. отчёт §7).
		static const uint8_t KeyXyT[KEY_LEN];

		// Активный профиль — глобально на процесс: соединение живёт одно, и обе стороны
		//   (шифр отправки и разбор приёма) обязаны использовать одну таблицу.
		// ⚠ Состояние живёт в AsdaCore, поэтому функции ЭКСПОРТИРУЮТСЯ: их зовёт логин-сцена
		//   из модуля AsdaReborn. Без экспорта линковка падает (LNK2019).
		ASDACORE_API static void SetProfile(EKeyProfile Profile);
		ASDACORE_API static EKeyProfile GetProfile();
		// Ключ активного профиля. bReceive выбирает таблицу ПРИЁМА (у XyT она отдельная и меняется).
		ASDACORE_API static const uint8_t* ActiveKey(bool bReceive = false);

		// ★★★ КЛЮЧ ПРИЁМА XyT — ЖИВОЙ, меняется по ходу сессии.
		//   Их сервер шлёт лаунчеру служебный кадр `FC | (len+2)^0x37 | seed[len]`, лаунчер считает
		//   новый ключ приёма по формуле recv[i] = (seed[T(i) mod seedLen] + 0x0B) ^ send[i],
		//   где T(i)=i(i+1)/2, и вписывает его в код СВОЕГО клиента через WriteProcessMemory.
		//   Нас он не патчит (мы другой процесс), поэтому ключ приходит снаружи — см. SetRecvKeyXyT.
		//   Ключ ОТПРАВКИ при этом неизменен всю сессию.
		ASDACORE_API static void SetRecvKeyXyT(const uint8_t* Key32);
		// Счётчик смен — чтобы вызывающий видел, что ключ обновился (для журнала/диагностики).
		ASDACORE_API static uint32_t RecvKeyVersion();
		// Вывести ключ приёма из семени кадра 0xFC (та самая формула; ключ отправки берётся из профиля).
		ASDACORE_API static void DeriveRecvKeyFromSeed(const uint8_t* Seed, int SeedLen);

		// Шифрует/дешифрует кадр НА МЕСТЕ (XOR симметричен).
		// frame указывает на начало кадра (байт 0xFB), size = полная длина кадра.
		// bReceive: true для S→C (recv, блоков (size-4)>>3), false для C→S (send, (size-3)>>3). Асимметрия из декомпила.
		// Безопасно при size < 5 (ничего не делает).
		static void Apply(uint8_t* frame, size_t size, bool bReceive = false);
		// ★ R4, доля a (14.11.2026): тот же XOR, что `Apply`, но по ЧАСТИ кадра, лежащей отдельно. `data[0]` — байт кадра с
		//   номером `3 + firstK` (номер ключа `firstK`), `len` — сколько байтов, `frameSize` — полная длина кадра (по ней
		//   считается число шифруемых байтов, как в `Apply`: байты с номером ключа ≥ ((frameSize−4)>>3)·8 не трогаются).
		//   Нужен разбору кадра одной копией (`PacketFrame::ParseOneCopy`).
		static void ApplyRange(uint8_t* data, size_t len, size_t firstK, size_t frameSize, bool bReceive);
	};
}
