// Copyright (c) 2026 @sitxovski (Discord)
#include "Net/PacketFrame.h"
#include "Net/ChannelCipher.h"
#include <cstring>

namespace Asda
{
	namespace PacketFrame
	{
		static inline void PutU16(std::vector<uint8_t>& v, size_t at, uint16_t x)
		{
			v[at]     = static_cast<uint8_t>(x & 0xFF);
			v[at + 1] = static_cast<uint8_t>((x >> 8) & 0xFF);
		}
		static inline uint16_t GetU16(const uint8_t* p)
		{
			return static_cast<uint16_t>(p[0] | (p[1] << 8));
		}

		std::vector<uint8_t> Build(uint16_t Opcode, const uint8_t* Body, size_t BodyLen,
		                           uint16_t HdrA, uint16_t HdrB, bool bPad)
		{
			// ★ ПАДДИНГ (эталон CNetwork_Send@0x12A9330, флаг a6=1): padLen=(g_nPacketPadCounter+=11)%20 добавляется
			//   ПОСЛЕ тела (Size+=padLen), если padLen+base<700. a6=0 (bPad=false) — enter-хендшейк 20000 (без пада).
			//   Прим.: пад — это ОРИГИНАЛЬНОЕ поведение, а НЕ лечение «зашифрованного терминатора». Тот баг был в
			//   числе блоков ((size−3) вместо (size−4) от полного размера) и починен в ChannelCipher::Apply —
			//   пад лишь маскировал его, меняя размер (оттого «срабатывало со второго раза»).
			static uint32_t s_padCounter = 0;
			size_t padLen = 0;
			if (bPad)
			{
				s_padCounter += 11;
				const size_t p = s_padCounter % 20;
				if (p + (static_cast<size_t>(HEADER) + BodyLen + TRAILER) < 700) { padLen = p; }
			}
			const size_t size = static_cast<size_t>(HEADER) + BodyLen + padLen + TRAILER;
			std::vector<uint8_t> frame(size, 0);
			frame[0] = MARK_BEGIN;
			PutU16(frame, 1, static_cast<uint16_t>(size));
			PutU16(frame, 3, HdrA);
			PutU16(frame, 5, HdrB);
			PutU16(frame, 7, Opcode);
			if (Body != nullptr && BodyLen > 0)
			{
				std::memcpy(frame.data() + HEADER, Body, BodyLen);
			}
			// padding-байты [HEADER+BodyLen .. size-TRAILER) остаются 0 (сервер игнорит хвост за телом)
			frame[size - 1] = MARK_END;
			// зашифровать тело [3 .. 3+nb*8)
			ChannelCipher::Apply(frame.data(), size);
			return frame;
		}

		size_t Parse(const uint8_t* buffer, size_t available, size_t offset, ParsedPacket& Out)
		{
			if (buffer == nullptr || offset >= available) return 0;
			if (buffer[offset] != MARK_BEGIN) return 0;
			if (offset + 3 > available) return 0;          // нет даже size
			const uint16_t size = GetU16(buffer + offset + 1);
			if (size < HEADER + TRAILER) return 0;          // битый размер
			if (offset + size > available) return 0;        // кадр ещё не дочитан

			// копия кадра + дешифр (XOR симметричен); RECV-путь → block count (size-4)>>3
			std::vector<uint8_t> fr(buffer + offset, buffer + offset + size);
			ChannelCipher::Apply(fr.data(), size, /*bReceive=*/true);

			Out.Size   = size;
			Out.HdrA   = GetU16(fr.data() + 3);
			Out.HdrB   = GetU16(fr.data() + 5);
			Out.Opcode = GetU16(fr.data() + 7);
			Out.Body.assign(fr.begin() + HEADER, fr.begin() + (size - TRAILER));
			return size;
		}

		// ★ R4, доля a (14.11.2026): разбор одной копией (объявление и довод — в заголовке). Проверки — дословно как у `Parse`.
		size_t ParseOneCopy(const uint8_t* buffer, size_t available, size_t offset, ParsedPacket& Out)
		{
			if (buffer == nullptr || offset >= available) return 0;
			if (buffer[offset] != MARK_BEGIN) return 0;
			if (offset + 3 > available) return 0;          // нет даже size
			const uint16_t size = GetU16(buffer + offset + 1);
			if (size < HEADER + TRAILER) return 0;          // битый размер
			if (offset + size > available) return 0;        // кадр ещё не дочитан

			// Заголовок под шифром — байты кадра [3..9): номера ключа 0..5.
			uint8_t hdr[6];
			std::memcpy(hdr, buffer + offset + 3, sizeof(hdr));
			ChannelCipher::ApplyRange(hdr, sizeof(hdr), 0, size, /*bReceive=*/true);

			Out.Size   = size;
			Out.HdrA   = GetU16(hdr);
			Out.HdrB   = GetU16(hdr + 2);
			Out.Opcode = GetU16(hdr + 4);
			// Тело — байты кадра [9 .. size-1): номера ключа с 6.
			const uint8_t* body = buffer + offset + HEADER;
			const size_t bodyLen = static_cast<size_t>(size) - HEADER - TRAILER;
			Out.Body.assign(body, body + bodyLen);
			ChannelCipher::ApplyRange(Out.Body.data(), bodyLen, 6, size, /*bReceive=*/true);
			return size;
		}
	}
}
