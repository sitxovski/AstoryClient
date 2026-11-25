// Copyright (c) 2026 @sitxovski (Discord)
// PacketFrame — кадр пакета AsdaStory (1:1).
// Раскладка (verified decode_capture.py + network_protocol_full.md + login_server.md):
//   [0]      = 0xFB                 (маркер начала, НЕ шифруется)
//   [1..2]   = Size  (u16 LE)       полная длина кадра, НЕ шифруется
//   [3..4]   = HdrA  (u16 LE)       суб-заголовок A (под шифром)
//   [5..6]   = HdrB  (u16 LE)       суб-заголовок B / slot (под шифром)
//   [7..8]   = Opcode(u16 LE)       опкод (под шифром)
//   [9 .. Size-2] = Body            тело (под шифром, может иметь pad в хвосте)
//   [Size-1] = 0xFE                 маркер конца, НЕ шифруется
// Шифр (ChannelCipher) применяется к байтам [3 .. 3+nb*8) после сборки / до разбора.
#pragma once
#include <cstdint>
#include <vector>

// Вне UE (standalone-тест) API-макрос модуля не определён — делаем его пустым.
#ifndef ASDACORE_API
#define ASDACORE_API
#endif

namespace Asda
{
	struct ParsedPacket
	{
		uint16_t Opcode = 0;
		uint16_t HdrA = 0;
		uint16_t HdrB = 0;
		std::vector<uint8_t> Body;   // тело без header/маркеров (с возможным pad)
		uint16_t Size = 0;           // исходная длина кадра
	};

	namespace PacketFrame
	{
		static constexpr uint8_t MARK_BEGIN = 0xFB;
		static constexpr uint8_t MARK_END   = 0xFE;
		static constexpr int     HEADER     = 9;   // FB+size2+A2+B2+op2
		static constexpr int     TRAILER    = 1;   // FE

		// Собрать ЗАШИФРОВАННЫЙ кадр для отправки.
		// На вход — плейнтекст-поля; на выходе — готовый к записи в сокет буфер (с XOR).
		// bPad: паддинг эталона a6=1 (по умолчанию ДА — все логин/геймплей-пакеты); false только для a6=0 (enter-хендшейк 20000).
		ASDACORE_API std::vector<uint8_t> Build(uint16_t Opcode, const uint8_t* Body, size_t BodyLen,
		                           uint16_t HdrA = 0, uint16_t HdrB = 0, bool bPad = true);

		// Разобрать ОДИН кадр из принятого (зашифрованного) буфера, начиная с offset.
		// frame[offset] должен быть 0xFB. Дешифрует копию, заполняет out.
		// Возвращает длину поглощённого кадра (>0) или 0, если кадр неполный/битый.
		ASDACORE_API size_t Parse(const uint8_t* buffer, size_t available, size_t offset, ParsedPacket& Out);

		// ★ R4, доля a (14.11.2026): то же, что `Parse` (те же проверки, тот же итог побайтно), но с одной копией вместо двух:
		//   шесть байтов заголовка расшифровываются на стеке, тело копируется сразу в `Out.Body` и расшифровывается на месте.
		//   Если `Out.Body` уже держит память (пакет переиспользуется), новое выделение не делается. Игра разбирает кадры только им
		//   (`FAsdaConnection::DrainFrames`); `Parse` оставлен для отдельного теста протокола (`tests/test_protocol.cpp`).
		ASDACORE_API size_t ParseOneCopy(const uint8_t* buffer, size_t available, size_t offset, ParsedPacket& Out);
	}
}
