// Copyright (c) 2026 @sitxovski (Discord)
#include "Net/ChannelCipher.h"

namespace Asda
{
	// ★ 32 байта = keySchedule из ДЕКОМПИЛА (CNetwork_Send@0x12A9330 / Packet_XorDecrypt@0x12AC3D0):
	//   dword'ы [keySeed=0x57C57142, 0x8B113D35, 0x59947769, 0xD5699555, 0xAA9A3C95, 0xCD15E120,
	//            0x04E8AA50, 0xB4968740] → LE-байты ниже.
	//   ★ ИСПРАВЛЕНО byte[0]=0x42 (было 0x57 — decode_capture.py неверен в byte0, верификация циклическая).
	//   Байт[0] шифрует cred+2 ('a' в "Arab_Closed", k=32→Key[0]); при 0x57 сервер memcmp version fail →
	//   login error code 10 (0x1005). Теперь верно.
	const uint8_t ChannelCipher::Key[ChannelCipher::KEY_LEN] = {
		0x42, 0x71, 0xC5, 0x57, 0x35, 0x3D, 0x11, 0x8B,
		0x69, 0x77, 0x94, 0x59, 0x55, 0x95, 0x69, 0xD5,   // ★★★ Key[12]=0x55 — ГROUND TRUTH из декомпила Packet_XorDecrypt@0x12AC3D0: dword K3=v9[2]=-714500779=0xD5699555 (LE=55 95 69 D5). Прошлый эмпирический фикс ставил 0x59 — он совпадал для КОРОТКИХ пакетов (Key[12] на k=12 бьёт лишь шум-байт секрета obfTick[6]), но для ДЛИННОГО create-пакета (76б) Key[12] через k%32 используется ещё на k=44→payload[14] и k=76→payload[46], шифруя РЕАЛЬНЫЕ байты payload. Сервер (непропатченный LoginManager sub_4060E0) де-XOR-ил их неверно → payload≠хешу → "Send_CreateChar MD5 Error". Ключ теперь полностью совпадает с dword'ами v8/v9[0..6] декомпила.
		0x95, 0x3C, 0x9A, 0xAA, 0x20, 0xE1, 0x15, 0xCD,
		0x50, 0xAA, 0xE8, 0x04, 0x40, 0x87, 0x96, 0xB4
	};

	// ★★★ Ключ сборки XyT. Восстановлен из живого захвата loopback-трафика их клиента
	//   (`Reports/xyt_traffic_capture.md` §7) методом известного открытого текста:
	//     • HdrA/HdrB у C→S равны 0            → Key[0..3]
	//     • keepalive = опкод 1000             → Key[4..5]
	//     • паддинг-байты кадра = нули         → Key[18..31]
	//     • строка версии `Arab_Closed`+нули   → Key[8..17]
	//   Сам метод проверен на НАШЕМ ключе: восстановленные байты совпали с `Key` побайтово.
	//   Валидация: LOGIN C→S 11/11, GAME C→S 189/189 опкодов распознаны (100 %).
	//   ⚠ Направление СЕРВЕР→КЛИЕНТ на игровом канале этим ключом НЕ читается: там ключ
	//     ротируется каждые ~120 с (закон генерации не выведен, нужен реверс их Launcher.exe).
	const uint8_t ChannelCipher::KeyXyT[ChannelCipher::KEY_LEN] = {
		0x51, 0xAD, 0x2D, 0xAA, 0x2D, 0x1E, 0xFA, 0x5C,
		0xBD, 0x85, 0xAC, 0x3D, 0x75, 0x3A, 0x50, 0x86,
		// ★ Key[21]=0x27 (не 0x26): уточнено сравнением нашего логин-пакета с эталонным.
		//   Все расхождения приходились ровно на позиции тела с pos%32==15, т.е. на этот байт ключа.
		0xE5, 0x64, 0x00, 0x00, 0x0C, 0x27, 0xE2, 0x5D,
		0xDC, 0xA4, 0x09, 0xDC, 0x45, 0x29, 0x87, 0x28
	};

	static EKeyProfile GActiveProfile = EKeyProfile::Default;

	// ★ Живой ключ ПРИЁМА для профиля XyT. Стартово равен ключу отправки — ровно так же делает их
	//   лаунчер при подключении (recv = send, пока не пришёл первый кадр 0xFC).
	static uint8_t  GRecvKeyXyT[ChannelCipher::KEY_LEN] = {
		0x51, 0xAD, 0x2D, 0xAA, 0x2D, 0x1E, 0xFA, 0x5C,
		0xBD, 0x85, 0xAC, 0x3D, 0x75, 0x3A, 0x50, 0x86,
		0xE5, 0x64, 0x00, 0x00, 0x0C, 0x27, 0xE2, 0x5D,
		0xDC, 0xA4, 0x09, 0xDC, 0x45, 0x29, 0x87, 0x28
	};
	static uint32_t GRecvKeyVersion = 0;

	void ChannelCipher::SetProfile(EKeyProfile Profile) { GActiveProfile = Profile; }
	EKeyProfile ChannelCipher::GetProfile()             { return GActiveProfile; }

	const uint8_t* ChannelCipher::ActiveKey(bool bReceive)
	{
		if (GActiveProfile != EKeyProfile::XyT) { return Key; }   // у нашей сборки таблица одна на оба направления
		return bReceive ? GRecvKeyXyT : KeyXyT;
	}

	void ChannelCipher::SetRecvKeyXyT(const uint8_t* Key32)
	{
		if (Key32 == nullptr) { return; }
		bool bChanged = false;
		for (int i = 0; i < KEY_LEN; ++i)
		{
			if (GRecvKeyXyT[i] != Key32[i]) { bChanged = true; }
			GRecvKeyXyT[i] = Key32[i];
		}
		if (bChanged) { ++GRecvKeyVersion; }
	}

	uint32_t ChannelCipher::RecvKeyVersion() { return GRecvKeyVersion; }

	// Формула их лаунчера (функция 0x00E894F0), проверена алгебраически на живой паре ключей:
	//   recv[i] = (seed[ T(i) mod seedLen ] + 0x0B) ^ send[i],  T(i) = i*(i+1)/2
	// Новый ключ выводится из НЕИЗМЕННОГО ключа отправки и свежего семени — не из предыдущего ключа.
	void ChannelCipher::DeriveRecvKeyFromSeed(const uint8_t* Seed, int SeedLen)
	{
		if (Seed == nullptr || SeedLen <= 0) { return; }
		uint8_t Out[KEY_LEN];
		for (int i = 0; i < KEY_LEN; ++i)
		{
			const int t   = (i * (i + 1) / 2) % SeedLen;
			const uint8_t s = Seed[t];
			Out[i] = (uint8_t)((s + 0x0B) ^ KeyXyT[i]);
		}
		SetRecvKeyXyT(Out);
	}

	void ChannelCipher::Apply(uint8_t* frame, size_t size, bool bReceive)
	{
		if (frame == nullptr || size < 5)
		{
			return; // нет шифруемого тела
		}
		// ★ Число блоков = (size−4)>>3 для ОБОИХ направлений; «асимметрия −3/−4» была иллюзией системы отсчёта.
		//   Эталон CNetwork_Send@0x12A9330 (стр.51539-51567): blockCount=(Size−3)>>3 считается, когда Size ещё
		//   БЕЗ терминатора (Size=9+body+pad); байт 0xFE дописывается и Size инкрементируется ПОСЛЕ шифрования.
		//   В терминах ПОЛНОГО размера кадра (как здесь, с 0xFE) это ровно (size−4)>>3, и терминатор в шифр
		//   НИКОГДА не попадает. Порт брал (size−3)>>3 от полного размера → при (size−4)%8==0 шифровался сам
		//   байт 0xFE → сервер бракует кадр: `CopyToWriteBuffer() BrokenPacket1` (проверка frame[Size−1]==0xFE,
		//   LoginManager.c:9378) + сброс входного буфера. Проявлялось как «удаление/пакет срабатывает со второго
		//   раза»: размер зависит от pad-счётчика, попадание в кратность — периодическое (live 2026-07-27).
		const size_t nb = (size - 4) >> 3;
		const size_t count = nb * 8;
		const uint8_t* K = ActiveKey(bReceive);   // у XyT таблицы приёма и отправки РАЗНЫЕ
		for (size_t k = 0; k < count; ++k)
		{
			frame[3 + k] ^= K[k % KEY_LEN];
		}
	}

	// ★ R4, доля a (14.11.2026): XOR части кадра — те же ключ, число шифруемых байтов и номера ключа, что у `Apply`.
	void ChannelCipher::ApplyRange(uint8_t* data, size_t len, size_t firstK, size_t frameSize, bool bReceive)
	{
		if (data == nullptr || frameSize < 5)
		{
			return; // нет шифруемого тела (как в `Apply`)
		}
		const size_t count = ((frameSize - 4) >> 3) * 8;
		const uint8_t* K = ActiveKey(bReceive);
		for (size_t j = 0; j < len && firstK + j < count; ++j)
		{
			data[j] ^= K[(firstK + j) % KEY_LEN];
		}
	}
}
