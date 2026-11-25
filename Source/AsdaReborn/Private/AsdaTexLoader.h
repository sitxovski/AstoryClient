// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTexLoader — рантайм-загрузка .TEX (AsdaStory) в UTexture2D.
//
// Формат .TEX (подтверждён): кастомный header 0x24 байта (9×u32):
//   [0] magic 0x20040603, [1] dataSize, [2] width, [3] height,
//   [4] layers, [5] mipCount, [6] format/FOURCC, [7] ?, [8] encoding
// затем payload @0x24 = СТАНДАРТНЫЙ контейнер: «DDS » (DXT1/2/3/4/5 + мипы) |
// PNG (0x89 'P' 'N' 'G') | BMP ('B' 'M').
//
// Тайлы террейна (TerrainColor.TEX / TerrainAlpha.TEX) = массив вложенных .TEX,
// каждый со своим 0x24 header на байт-смещении p.colorOff / p.alphaOff.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

namespace AsdaTex
{
	// Распарсить .TEX из памяти → UTexture2D (transient, NeverStream). nullptr при ошибке.
	// data указывает на начало 0x24-header (для тайла — на его offset внутри массива).
	UTexture2D* LoadTexFromMemory(const uint8* Data, int32 Size);

	// ★ Функции LoadTexFile/ClearCache УДАЛЕНЫ: рантайм .TEX с диска НЕ читает (закон проекта —
	//   данные только из ассетов UE; файлы оригинала в шипнутый пак не пакуются). Остался разбор
	//   уже прочитанного буфера — для вложенных тайлов террейна и блобов из архивов.
}
