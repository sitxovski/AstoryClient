// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaClanFlag — КОМПОЗИТОР ФЛАГА (эмблемы) КЛАНА, порт 1:1 с эталона.
//
// Эталон: CMiniMap_BakeIconToRenderTarget@0xFB2E10 (world_map.c:10188) — печёт эмблему клана в
//   render-target 128×64 из ДВУХ квадов и трёх цветов; вызов из entity_char.c:6656:
//     CMiniMap_BakeIconToRenderTarget(clanFlagObj, +2530, +2506, +2522, +2538, +2514)
//   ⇒ раскладка 40-байтного блока ClanFlagData (+2506, приходит пакетом 6542):
//     +0  int32 BorderIdx    (0-based индекс в Clan_BorderImage.bin — ФОН-рамка флага)
//     +8  int32 BorderColor  (ARGB, D3DRS_TEXTUREFACTOR × текстура рамки)
//     +16 int32 BgColor      (ARGB, Clear render-target'а)
//     +24 int32 SignIdx      (0-based индекс в Clan_Sign.bin — СИМВОЛ/инсигния)
//     +32 int32 SignColor    (ARGB, тинт символа)
//
//   Геометрия (ортопроекция RT 128×64, X −64..64, Y −32..32 «вверх»; декод float-констант эталона):
//     квад РАМКИ  — весь RT: X[−64,64] Y[−32,32]  → пиксели (0,0)-(128,64)
//     квад СИМВОЛА— X[−37,20] Y[−20,18]           → пиксели (27,14)-(84,52), т.е. 57×38
//   UV-сетки (обе текстуры 512×512, подтверждено заголовками .tex и полями srcX/srcY таблиц):
//     рамка:  cell 144×96, 3 столбца → tex = Clan_BorderImage[idx].tex, cell = (idx%15)%3, (idx%15)/3
//     символ: cell 64×64, 8 столбцов → uiGrid-подобное (idx%8, idx/8) в ClanInsgnia_00.tex
//   Блендинг: SRCALPHA/INVSRCALPHA, COLOROP=MODULATE(текстура × TFACTOR).
//
// На плашке над головой эмблема рисуется НЕ целиком: CControl_DrawTextureRect(+972, flagTex, 23,12,64,42)
//   — из RT берётся под-прямоугольник (23,12)+(64×42) и вписывается в контрол 24×24 (см. kPlateUV).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "World/AsdaClanFlagData.h"   // ★ R3: FAsdaClanFlagData (вынесено отсюда)
#include "AsdaClanFlag.generated.h"

class UTextureRenderTarget2D;

// ★ R3 (11.11.2026): `FAsdaClanFlagData` вынесено дословно в `World/AsdaClanFlagData.h` (включён в шапке этого файла).

UCLASS()
class ASDAREBORN_API UAsdaClanFlag : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// Под-прямоугольник RT, который эталон рисует на плашке: (23,12) размером 64×42 из 128×64 → доли UV.
	static const FBox2D kPlateUV;

	// Готовый (кэшированный) render-target 128×64 с испечённой эмблемой. nullptr, если данных нет.
	UFUNCTION(BlueprintCallable, Category = "Asda|ClanFlag", meta = (WorldContext = "WorldCtx"))
	static UTextureRenderTarget2D* GetOrBake(UObject* WorldCtx, const FAsdaClanFlagData& Data);

	// ★ 06.10.2026 (портрет своего персонажа): ОБЩАЯ ТОЧКА кисти «эмблема клана в контроле».
	//   Эталон рисует один и тот же вырез двумя местами — панель цели (`ui_core.c:75982`) и портрет
	//   своего персонажа (`uiDrawPlayerPortrait@0xE81FF0`, `ui_core.c:77897`):
	//   `CControl_DrawTextureRect(ctrl, flagTex, 23, 12, 64, 42)`. Здесь — испечь (кэш `GetOrBake`)
	//   и собрать кисть с UV-вырезом `kPlateUV`, вписанным в квадрат DisplayPx. false = печь нечего.
	static bool MakePlateBrush(UObject* WorldCtx, const FAsdaClanFlagData& Data, float DisplayPx, struct FSlateBrush& OutBrush);

	// ★ 06.10.2026 (ревизия, вне задачи): ОБЩАЯ ТОЧКА «эмблема лагеря кампвара» — портрет своего персонажа
	//   (`uiDrawPlayerPortrait`, ui_core.c:77899-77905) и панель цели (`ui_core.c:75970-75976`) рисуют её одинаково:
	//   `uiMakeAxisVec2(g_pCampWarClan, uv, лагерь)` → 0:(0,0), 1:(64,0), 2:(0,64) (ui_core.c:36642) и
	//   `CControl_DrawTextureRect(ctrl, "CampWar_MarkImage.tex", u, v, 64, 64)` (атлас — combat_pvp.c:3999).
	//   Карта кампвара — байт +69 записи ТЕКУЩЕЙ карты (`*(g_pLocalEntity + 69) == 1`, колонка CampwarPkFlag).
	static bool IsCampWarMap(const UObject* WorldCtx);
	// Поставить в Img ячейку лагеря Camp (0..2), вписанную в квадрат DisplayPx. false = лагерь вне 0..2
	//   (эталон индексирует без проверки и читает мусор стека — у нас пустая кисть) или атласа нет.
	static bool ApplyCampMark(class UImage* Img, int32 Camp, float DisplayPx);

	// Имя текстуры символа/рамки по 0-based индексу (таблицы Clan_Sign.bin / Clan_BorderImage.bin).
	static bool LookupSignTex(int32 Idx0, FString& OutTex);
	static bool LookupBorderTex(int32 Idx0, FString& OutTex);
	// ★ Объектные пути этих таблиц — одно объявление (15.09.2026, dt_gc_hold): читают `LookupSignTex`/`LookupBorderTex`,
	//   удерживает на сессию `UAsdaWorldSubsystem::HoldSpawnTables(Clan)`.
	static constexpr const TCHAR* kSignTablePath   = TEXT("/Game/AsdaAssets/Text/Clan/DT_Clan_Sign.DT_Clan_Sign");
	static constexpr const TCHAR* kBorderTablePath = TEXT("/Game/AsdaAssets/Text/Clan/DT_Clan_BorderImage.DT_Clan_BorderImage");

	// Текстура JUI-атласа по имени файла из таблицы ("ClanInsgnia_00.tex" → .../Textures/JUI/claninsgnia_00).
	static class UTexture2D* LoadJuiTexture(const FString& TexFileName);

	// ARGB int32 (как в оригинале) → FLinearColor.
	static FLinearColor ArgbToLinear(int32 Argb);
};
