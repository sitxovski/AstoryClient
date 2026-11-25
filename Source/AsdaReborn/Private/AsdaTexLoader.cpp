// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaTexLoader.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "PixelFormat.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTex, Log, All);

namespace AsdaTex
{
	static FORCEINLINE uint32 ReadU32(const uint8* P)
	{
		uint32 V;
		FMemory::Memcpy(&V, P, 4);
		return V;
	}

	// Размер mip0 в байтах для блочно-сжатого формата.
	static int64 BlockCompressedMip0Size(int32 W, int32 H, EPixelFormat PF)
	{
		const int64 BlockBytes = (PF == PF_DXT1) ? 8 : 16; // DXT1=8B/4x4, DXT3/5=16B/4x4
		const int64 BlocksX = (int64)(W + 3) / 4;
		const int64 BlocksY = (int64)(H + 3) / 4;
		return BlocksX * BlocksY * BlockBytes;
	}

	// Создать transient-текстуру и залить ровно одни данные mip0 (Mip0Bytes).
	static UTexture2D* CreateAndFill(int32 W, int32 H, EPixelFormat PF, const uint8* Src, int64 Mip0Bytes)
	{
		if (W <= 0 || H <= 0 || Mip0Bytes <= 0)
		{
			return nullptr;
		}

		UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF);
		if (!Tex)
		{
			return nullptr;
		}

		Tex->NeverStream = true;
#if WITH_EDITORONLY_DATA
		Tex->MipGenSettings = TMGS_NoMipmaps;
#endif

		FTexturePlatformData* PD = Tex->GetPlatformData();
		if (!PD || PD->Mips.Num() == 0)
		{
			return nullptr;
		}

		FTexture2DMipMap& Mip0 = PD->Mips[0];
		void* Dst = Mip0.BulkData.Lock(LOCK_READ_WRITE);
		if (!Dst)
		{
			Mip0.BulkData.Unlock();
			return nullptr;
		}

		// Платформенный mip0 ровно нужного размера — копируем столько, сколько ожидает RHI,
		// но не больше, чем есть в источнике (защита от OOB на «коротких» payload).
		const int64 DstBytes = (int64)Mip0.BulkData.GetBulkDataSize();
		const int64 CopyBytes = FMath::Min(DstBytes, Mip0Bytes);
		FMemory::Memcpy(Dst, Src, CopyBytes);
		Mip0.BulkData.Unlock();

		Tex->UpdateResource();
		return Tex;
	}

	// Разобрать стандартный DDS-контейнер (128-байтовый заголовок), залить mip0.
	static UTexture2D* LoadDDS(const uint8* Dds, int32 DdsSize)
	{
		if (DdsSize < 128)
		{
			return nullptr;
		}

		// DDS header: magic(4) + 124. width@12, height@16, linearSize@20 (=4+0x10),
		// FOURCC@84. (Поля игры: height@(4+8)=12, width@(4+12)=16.)
		const int32 Height = (int32)ReadU32(Dds + 12);
		const int32 Width  = (int32)ReadU32(Dds + 16);
		const char F0 = (char)Dds[84], F1 = (char)Dds[85], F2 = (char)Dds[86], F3 = (char)Dds[87];

		EPixelFormat PF = PF_Unknown;
		if      (F0=='D' && F1=='X' && F2=='T' && F3=='1') PF = PF_DXT1;
		else if (F0=='D' && F1=='X' && F2=='T' && F3=='2') PF = PF_DXT3; // DXT2 == premult DXT3
		else if (F0=='D' && F1=='X' && F2=='T' && F3=='3') PF = PF_DXT3;
		else if (F0=='D' && F1=='X' && F2=='T' && F3=='4') PF = PF_DXT5; // DXT4 == premult DXT5
		else if (F0=='D' && F1=='X' && F2=='T' && F3=='5') PF = PF_DXT5;

		if (Width <= 0 || Height <= 0)
		{
			return nullptr;
		}

		if (PF == PF_Unknown)
		{
			// Несжатый DDS (DDPF_RGB, напр. D3DFMT_A8R8G8B8/X8R8G8B8 у SKY_DOME/Water): 32-бит BGRA.
			// pf.dwFlags@80, pf.dwRGBBitCount@88. Грузим как PF_B8G8R8A8 (DDS хранит BGRA — прямой копир).
			const uint32 PfFlags = ReadU32(Dds + 80);
			const uint32 RgbBits = ReadU32(Dds + 88);
			const bool bUncompressedRgb = (PfFlags & 0x40u /*DDPF_RGB*/) != 0 || (PfFlags & 0x4u /*DDPF_FOURCC*/) == 0;
			if (bUncompressedRgb && RgbBits == 32)
			{
				const int64 Need = (int64)Width * Height * 4;
				const int64 Av   = (int64)DdsSize - 128;
				if (Need <= Av)
				{
					return CreateAndFill(Width, Height, PF_B8G8R8A8, Dds + 128, Need);
				}
			}
			UE_LOG(LogAsdaTex, Warning, TEXT("LoadDDS: неподдержанный формат (fourcc '%c%c%c%c', bits=%u, w=%d h=%d)"),
				F0, F1, F2, F3, RgbBits, Width, Height);
			return nullptr;
		}

		// mip0 идёт СРАЗУ после 128-байтового DDS-заголовка.
		const uint8* Pixels = Dds + 128;
		const int64 Avail = (int64)DdsSize - 128;
		int64 Mip0Bytes = BlockCompressedMip0Size(Width, Height, PF);
		if (Mip0Bytes > Avail)
		{
			Mip0Bytes = Avail; // не читать за концом (доп. мипы и так игнорируем)
		}
		if (Mip0Bytes <= 0)
		{
			return nullptr;
		}

		return CreateAndFill(Width, Height, PF, Pixels, Mip0Bytes);
	}

	// PNG/BMP fallback через IImageWrapper → BGRA8.
	static UTexture2D* LoadImageWrapped(const uint8* Payload, int32 PayloadSize, EImageFormat Fmt)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Fmt);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Payload, PayloadSize))
		{
			return nullptr;
		}

		// Принудительно BGRA8 (формат UTexture2D PF_B8G8R8A8). Используем 3-арг.
		// перегрузку с TArray64 (1-арг. GetRaw вернул бы нативный формат файла).
		TArray64<uint8> Raw;
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const bool bOk = Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (!bOk)
		{
			return nullptr;
		}

		const int32 W = (int32)Wrapper->GetWidth();
		const int32 H = (int32)Wrapper->GetHeight();
		const int64 Expected = (int64)W * H * 4;
		if (W <= 0 || H <= 0 || (int64)Raw.Num() < Expected)
		{
			return nullptr;
		}

		return CreateAndFill(W, H, PF_B8G8R8A8, Raw.GetData(), Expected);
	}

	UTexture2D* LoadTexFromMemory(const uint8* Data, int32 Size)
	{
		// header 0x24 + минимум сигнатуры payload
		if (!Data || Size < 0x24 + 4)
		{
			return nullptr;
		}

		const uint32 Magic = ReadU32(Data);
		if (Magic != 0x20040603u)
		{
			UE_LOG(LogAsdaTex, Warning, TEXT("LoadTexFromMemory: неверный magic 0x%08X"), Magic);
			return nullptr;
		}

		const uint8* Payload = Data + 0x24;
		const int32 PayloadSize = Size - 0x24;
		if (PayloadSize < 4)
		{
			return nullptr;
		}

		// Контейнер payload по сигнатуре.
		if (Payload[0]=='D' && Payload[1]=='D' && Payload[2]=='S' && Payload[3]==' ')
		{
			return LoadDDS(Payload, PayloadSize);
		}
		if ((uint8)Payload[0]==0x89 && Payload[1]=='P' && Payload[2]=='N' && Payload[3]=='G')
		{
			return LoadImageWrapped(Payload, PayloadSize, EImageFormat::PNG);
		}
		if (Payload[0]=='B' && Payload[1]=='M')
		{
			return LoadImageWrapped(Payload, PayloadSize, EImageFormat::BMP);
		}

		UE_LOG(LogAsdaTex, Warning, TEXT("LoadTexFromMemory: неизвестный payload-контейнер (%02X %02X %02X %02X)"),
			Payload[0], Payload[1], Payload[2], Payload[3]);
		return nullptr;
	}

	// ★ LoadTexFile / ClearCache УДАЛЕНЫ. Рантайм-загрузка .TEX С ДИСКА запрещена законом проекта:
	//   данные берутся ТОЛЬКО из ассетов UE (файлы оригинала в шипнутый пак не пакуются). Разбор .TEX
	//   на этапе импорта делает AsdaImporters/Tools; здесь остался лишь разбор УЖЕ ПРОЧИТАННОГО буфера
	//   (LoadTexFromMemory) — им пользуются вложенные тайлы террейна и архивные блобы.
}
