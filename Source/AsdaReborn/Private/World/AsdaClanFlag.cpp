// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaClanFlag — см. AsdaClanFlag.h (эталон CMiniMap_BakeIconToRenderTarget@0xFB2E10).
#include "World/AsdaClanFlag.h"
#include "AsdaBytes.h"   // ★ N6 (В1 дедупа №2): единые читалки little-endian
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт

#include "Engine/Canvas.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): текстуры только через менеджер
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/StrongObjectPtr.h"   // кэш эмблем без AddToRoot (иначе крэш !IsRooted() при выходе)
#include "Kismet/KismetRenderingLibrary.h"
#include "Styling/SlateBrush.h"
#include "UI/AsdaUiHelpers.h"                 // ★ 06.10.2026: ApplyJuiIconCell / ClearImageBrush — ячейка атласа лагерей
#include "World/AsdaLoadManagerSubsystem.h"   // ★ 06.10.2026: текущая карта (байт +69)
#include "World/AsdaMapTable.h"
#include "Components/Image.h"
#include "Engine/GameInstance.h"   // ★ 06.10.2026: MakePlateBrush — кисть эмблемы для панели цели и портрета
#include "Login/AsdaGameText.h"   // FAsdaTextRow (таблицы Clan_Sign / Clan_BorderImage: id + tex)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClanFlag, Log, All);

// ── Константы эталона ────────────────────────────────────────────────────────────────────────────
// Размер испечённого флага (D3D_CreateRenderTargetTexture 128×64).
static constexpr int32 kFlagW = 128;
static constexpr int32 kFlagH = 64;
// Атласы символов/рамок — 512×512 (подтверждено заголовками .tex).
static constexpr float kAtlas = 512.f;
// Сетка СИМВОЛА: cell 64×64, 8 столбцов (эталон: tileCol = idx&7, tileRow = idx/8, uvStep = 64/512).
static constexpr float kSignCell = 64.f;
static constexpr int32 kSignCols = 8;
// Сетка РАМКИ: cell 144×96 (uvStep 0.28125/0.1875 × 512), 3 столбца, 15 записей на текстуру.
static constexpr float kBorderCellW = 144.f;
static constexpr float kBorderCellH = 96.f;
static constexpr int32 kBorderCols  = 3;
static constexpr int32 kBorderPerTex = 15;
// Прямоугольник квада СИМВОЛА в пикселях RT (декод ортоквада: X[−37,20], Y[−20,18], y_px = 32 − Y).
static const FVector2D kSignPos(27.f, 14.f);
static const FVector2D kSignSize(57.f, 38.f);

// Под-прямоугольник, который эталон рисует на плашке: DrawTextureRect(+972, tex, 23, 12, 64, 42).
const FBox2D UAsdaClanFlag::kPlateUV(
	FVector2D(23.f / kFlagW, 12.f / kFlagH),
	FVector2D((23.f + 64.f) / kFlagW, (12.f + 42.f) / kFlagH));

FString FAsdaClanFlagData::CacheKey() const
{
	return FString::Printf(TEXT("%d_%08x_%08x_%d_%08x"), BorderIdx, (uint32)BorderColor, (uint32)BgColor, SignIdx, (uint32)SignColor);
}

FAsdaClanFlagData FAsdaClanFlagData::FromWire(const uint8* B40)
{
	FAsdaClanFlagData D;
	if (!B40) { return D; }
	auto Dw = [B40](int32 Off) -> int32 { return AsdaBytes::RdI32(B40 + Off); };   // ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`
	// Эталон читает дворды с шагом 8 байт внутри блока +2506 (см. вызов бейка).
	D.BorderIdx   = Dw(0);
	D.BorderColor = Dw(8);
	D.BgColor     = Dw(16);
	D.SignIdx     = Dw(24);
	D.SignColor   = Dw(32);
	return D;
}

FLinearColor UAsdaClanFlag::ArgbToLinear(int32 Argb)
{
	// ★ Данные игры — sRGB (правило ui-srgb-gamma-rule): переводим в linear конструктором FLinearColor(FColor).
	const uint32 U = (uint32)Argb;
	const FColor C((U >> 16) & 0xFF, (U >> 8) & 0xFF, U & 0xFF, (U >> 24) & 0xFF);
	return FLinearColor(C);
}

// ── Таблицы (Clan_Sign.bin / Clan_BorderImage.bin → DataTable, ключ строки = id 1-based) ─────────
static bool LookupTexInTable(const TCHAR* AssetPath, int32 Idx0, FString& OutTex)
{
	OutTex.Reset();
	if (Idx0 < 0) { return false; }
	// ★★★ Аудит №4 (B2/A13): кэша не было — ОБЕ таблицы грузились заново на КАЖДУЮ пересборку
	//   эмблемы клана. Теперь через общую точку `AsdaDt`.
	//   Эталон индексирует массив записей 0-based, а id в файле 1-based → строка = idx+1.
	if (const FAsdaTextRow* R = AsdaDt::RowByInt<FAsdaTextRow>(AssetPath, Idx0 + 1, TEXT("AsdaClanFlag")))
	{
		OutTex = R->tex;
		return !OutTex.IsEmpty();
	}
	return false;
}

bool UAsdaClanFlag::LookupSignTex(int32 Idx0, FString& OutTex)
{
	return LookupTexInTable(kSignTablePath, Idx0, OutTex);
}

bool UAsdaClanFlag::LookupBorderTex(int32 Idx0, FString& OutTex)
{
	return LookupTexInTable(kBorderTablePath, Idx0, OutTex);
}

UTexture2D* UAsdaClanFlag::LoadJuiTexture(const FString& TexFileName)
{
	if (TexFileName.IsEmpty()) { return nullptr; }
	// "ClanInsgnia_00.tex" → базовое имя в нижнем регистре (ассеты импортированы lowercase).
	const FString Base = FPaths::GetBaseFilename(TexFileName.Replace(TEXT("\\"), TEXT("/"))).ToLower();
	// ★ Аудит №3 (A8/B8): JUI-атлас — только через менеджер (кэш + refCount).
	//   `LoadUITexture` строит тот же путь `/Game/AsdaAssets/Textures/JUI/<имя>.<имя>`.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	return TexMgr ? TexMgr->LoadUITexture(FName(*Base)) : nullptr;
}

// ── Кэш испечённых флагов (в оригинале — объект флага на клан, uiClanFlagGetOrCreate) ────────────
// ★★★ КЭШ ДЕРЖИТ ССЫЛКИ ЧЕРЕЗ `TStrongObjectPtr`, А НЕ ЧЕРЕЗ `AddToRoot()`.
//   ДЕФЕКТ (2026-08-26): раньше испечённый render target прибивался к корню GC вызовом
//   `AddToRoot()` и НИКОГДА оттуда не снимался. При выходе из игры движок проверяет
//   `Assertion failed: !IsRooted()` (UObjectBaseUtility.h:209) и падает — владелец видел это
//   как крэш редактора при закрытии.
//   `TStrongObjectPtr` держит объект от сборки штатной ссылкой GC (флаг `Rooted` НЕ ставится)
//   и корректно отпускает его при разрушении карты.
static TMap<FString, TStrongObjectPtr<UTextureRenderTarget2D>>& FlagCache()
{
	static TMap<FString, TStrongObjectPtr<UTextureRenderTarget2D>> Cache;
	return Cache;
}

UTextureRenderTarget2D* UAsdaClanFlag::GetOrBake(UObject* WorldCtx, const FAsdaClanFlagData& Data)
{
	if (!Data.IsValid()) { return nullptr; }
	const FString Key = Data.CacheKey();
	if (TStrongObjectPtr<UTextureRenderTarget2D>* Found = FlagCache().Find(Key))
	{
		if (Found->IsValid() && IsValid(Found->Get())) { return Found->Get(); }
		FlagCache().Remove(Key);
	}
	UWorld* W = GEngine ? GEngine->GetWorldFromContextObject(WorldCtx, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!W) { return nullptr; }

	// Текстуры символа и рамки по индексам из таблиц клана.
	FString SignTexName, BorderTexName;
	LookupSignTex(Data.SignIdx, SignTexName);
	LookupBorderTex(Data.BorderIdx, BorderTexName);
	UTexture2D* SignTex   = LoadJuiTexture(SignTexName);
	UTexture2D* BorderTex = LoadJuiTexture(BorderTexName);
	if (!SignTex && !BorderTex)
	{
		UE_LOG(LogAsdaClanFlag, Warning, TEXT("[флаг клана] нет текстур: sign=%d('%s') border=%d('%s')"),
			Data.SignIdx, *SignTexName, Data.BorderIdx, *BorderTexName);
		return nullptr;
	}

	// ★★★ RENDER TARGET СОЗДАЁМ В TRANSIENT-ПАКЕТЕ, А НЕ В МИРЕ.
	//   ДЕФЕКТ (2026-08-26): `UKismetRenderingLibrary::CreateRenderTarget2D(W, …)` кладёт объект
	//   ВНУТРЬ переданного мира (`Outer = World`). Кэш эмблем переживает выход из PIE, держит эту
	//   текстуру, а та — свой мир, и редактор падает:
	//   «До сих пор имеется ссылка на объект World /Game/Map/…/UEDPIE_0_… → TextureRenderTarget2D».
	//   В transient-пакете объект ни к какому миру не привязан, и кэш может жить всю сессию.
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	if (!RT) { return nullptr; }
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->ClearColor = FLinearColor::Transparent;
	RT->bAutoGenerateMips = false;
	RT->InitAutoFormat(kFlagW, kFlagH);
	RT->UpdateResourceImmediate(true);

	// 1) Clear = цвет фона флага (эталон: Device->Clear(..., a4)).
	UKismetRenderingLibrary::ClearRenderTarget2D(W, RT, ArgbToLinear(Data.BgColor));

	UCanvas* C = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Ctx;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(W, RT, C, CanvasSize, Ctx);
	if (C)
	{
		// 2) РАМКА-фон на весь RT (эталон рисует quadB ПЕРВЫМ, тинт = +2514).
		if (BorderTex && Data.BorderIdx >= 0)
		{
			const int32 Sub = Data.BorderIdx % kBorderPerTex;
			const FVector2D UV0((Sub % kBorderCols) * kBorderCellW / kAtlas, (Sub / kBorderCols) * kBorderCellH / kAtlas);
			const FVector2D UVSz(kBorderCellW / kAtlas, kBorderCellH / kAtlas);
			C->K2_DrawTexture(BorderTex, FVector2D::ZeroVector, FVector2D(kFlagW, kFlagH), UV0, UVSz,
				ArgbToLinear(Data.BorderColor), BLEND_Translucent);
		}
		// 3) СИМВОЛ поверх (quadA, тинт = +2538), прямоугольник (27,14)+57×38.
		if (SignTex && Data.SignIdx >= 0)
		{
			const FVector2D UV0((Data.SignIdx % kSignCols) * kSignCell / kAtlas, (Data.SignIdx / kSignCols) * kSignCell / kAtlas);
			const FVector2D UVSz(kSignCell / kAtlas, kSignCell / kAtlas);
			C->K2_DrawTexture(SignTex, kSignPos, kSignSize, UV0, UVSz,
				ArgbToLinear(Data.SignColor), BLEND_Translucent);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(W, Ctx);

	FlagCache().Add(Key, TStrongObjectPtr<UTextureRenderTarget2D>(RT));
	UE_LOG(LogAsdaClanFlag, Log, TEXT("[флаг клана] испечён 128×64: sign=%d('%s') border=%d('%s') bg=%08x"),
		Data.SignIdx, *SignTexName, Data.BorderIdx, *BorderTexName, (uint32)Data.BgColor);
	return RT;
}

// ★ 06.10.2026: общая точка кисти эмблемы (панель цели + портрет своего персонажа) — см. объявление.
bool UAsdaClanFlag::MakePlateBrush(UObject* WorldCtx, const FAsdaClanFlagData& Data, float DisplayPx, FSlateBrush& OutBrush)
{
	UTextureRenderTarget2D* RT = GetOrBake(WorldCtx, Data);   // кэш: один RT на одинаковый дизайн
	if (!RT) { return false; }
	OutBrush = FSlateBrush();
	OutBrush.SetResourceObject(RT);
	OutBrush.ImageSize = FVector2D(DisplayPx, DisplayPx);
	OutBrush.SetUVRegion(FBox2D(kPlateUV.Min, kPlateUV.Max));   // вырез (23,12)+64×42 из RT 128×64
	return true;
}

// ★ 06.10.2026: общая точка «карта кампвара» — см. объявление.
bool UAsdaClanFlag::IsCampWarMap(const UObject* WorldCtx)
{
	const UWorld* W = (GEngine && WorldCtx) ? GEngine->GetWorldFromContextObject(WorldCtx, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FAsdaMapRow* Row = LM ? LM->GetMapRow(LM->CurrentMapId) : nullptr;   // запись текущей карты
	return Row && Row->CampwarPkFlag == 1;
}

// ★ 06.10.2026: общая точка «эмблема лагеря» — см. объявление.
bool UAsdaClanFlag::ApplyCampMark(UImage* Img, int32 Camp, float DisplayPx)
{
	static const int32 kU[3] = { 0, 64, 0 };   // v6 эталона uiMakeAxisVec2
	static const int32 kV[3] = { 0, 0, 64 };   // v5 эталона
	if (!Img) { return false; }
	if (Camp < 0 || Camp > 2) { AsdaUi::ClearImageBrush(Img); return false; }
	return AsdaUi::ApplyJuiIconCell(Img, TEXT("CampWar_MarkImage"), kU[Camp], kV[Camp], 64, DisplayPx);
}
