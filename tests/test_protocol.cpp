// Copyright (c) 2026 @sitxovski (Discord)
// Standalone-тест ядра AsdaCore (вне UE): крипто/кадр 1:1.
// Сборка: g++ -std=c++17 -I../Source/AsdaCore/Public test_protocol.cpp \
//            ../Source/AsdaCore/Private/Net/ChannelCipher.cpp \
//            ../Source/AsdaCore/Private/Net/PacketFrame.cpp -o test_protocol
#include "Net/ChannelCipher.h"
#include "Net/PacketFrame.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cassert>

using namespace Asda;

static int fails = 0;
#define CHECK(c, msg) do{ if(!(c)){ printf("FAIL: %s\n", msg); ++fails; } else { printf("ok  : %s\n", msg);} }while(0)

int main()
{
	// 1) XOR симметричен (encrypt==decrypt).
	{
		uint8_t a[24]; for (int i=0;i<24;i++) a[i]=(uint8_t)(i*7+1);
		uint8_t b[24]; memcpy(b,a,24);
		ChannelCipher::Apply(b,24);
		ChannelCipher::Apply(b,24);
		CHECK(memcmp(a,b,24)==0, "XOR двойное применение = identity");
		// маркеры/size (0..2) и хвост незатронуты вне блоков
		uint8_t c[24]; memcpy(c,a,24);
		ChannelCipher::Apply(c,24);
		CHECK(c[0]==a[0] && c[1]==a[1] && c[2]==a[2], "байты 0..2 (FB/size) не шифруются");
	}

	// 2) Round-trip кадра: Build (encrypt) -> Parse (decrypt) восстанавливает поля.
	{
		const uint8_t body[] = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03};
		auto frame = PacketFrame::Build(1003 /*OP_SEND_LOGIN_PACKET_1*/, body, sizeof(body), 0x1234, 0x5678);
		CHECK(frame.front()==0xFB && frame.back()==0xFE, "маркеры FB/FE на месте");
		CHECK((frame[1]|(frame[2]<<8))==(int)frame.size(), "size в заголовке = длине кадра");

		ParsedPacket p;
		size_t n = PacketFrame::Parse(frame.data(), frame.size(), 0, p);
		CHECK(n==frame.size(), "Parse поглотил весь кадр");
		CHECK(p.Opcode==1003, "опкод восстановлен");
		CHECK(p.HdrA==0x1234 && p.HdrB==0x5678, "HdrA/HdrB восстановлены");
		CHECK(p.Body.size()==sizeof(body) && memcmp(p.Body.data(),body,sizeof(body))==0, "тело восстановлено");
	}

	// 3) Опкод реально под шифром (в зашифрованном кадре байты 7..8 != плейнтекст-опкод).
	{
		const uint8_t body[] = {0x00};
		auto frame = PacketFrame::Build(0x0FAF /*combat*/, body, sizeof(body));
		const uint16_t rawOp = (uint16_t)(frame[7]|(frame[8]<<8));
		CHECK(rawOp != 0x0FAF, "опкод в кадре зашифрован (не равен плейнтексту)");
	}

	// 4) Частичный буфер: Parse не трогает недочитанный кадр.
	{
		const uint8_t body[] = {1,2,3,4,5,6,7,8,9,10};
		auto frame = PacketFrame::Build(2000, body, sizeof(body));
		ParsedPacket p;
		size_t n = PacketFrame::Parse(frame.data(), frame.size()-2, 0, p); // не хватает 2 байт
		CHECK(n==0, "неполный кадр не парсится (ждём дочитки)");
	}

	printf(fails==0 ? "\nALL PASS\n" : "\n%d FAIL\n", fails);
	return fails==0 ? 0 : 1;
}
