// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMapRegistry.h — РЕЕСТР КАРТ для C++: id карт, континенты карты мира, области карты мира, кроп городской
//   большой карты, кнопки поиска группы, граф межкартовых переходов, приведение имени оригинала к имени ассета UE.
//
// ★ ГЕНЕРИРУЕТСЯ Tools/asda_map_registry.py из Tools/data/asda_map_registry.json — НЕ ПРАВИТЬ РУКАМИ.
//   Правка: источник (JSON) → `python Tools/asda_map_registry.py`; сверка со всеми слоями — `--check`.
//   Строки карт (DirName, уровень, BGM, текстуры названий) — в DT_MapTable (FAsdaMapRow), из того же источника.
#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"

namespace AsdaMapRegistry
{
	// Число карт в реестре; id идут подряд 0…kMapCount-1.
	inline constexpr int32 kMapCount = 24;

	// ── id карт, на которые ссылается код ──
	inline constexpr int32 kBattlefield = 19;   // battlefield (kind=campwar, campwar/PK=1)
	inline constexpr int32 kMineWar = 23;   // mine (kind=campwar, campwar/PK=1)

	// Имя оригинала (папка карты, текстура названия, звук) → имя ассета UE: пробел в имени пакета недопустим → «_».
	//   Python-двойник — asda_maps.map_asset_name (он заменяет любой не [A-Za-z0-9_]); для имён из реестра правила
	//   совпадают — это проверяет --check.
	inline FString ToAssetName(const FString& Name)
	{
		return Name.Replace(TEXT(" "), TEXT("_"));
	}

	// Путь сетки проходимости/высот карты (DA_WalkGrid) по имени папки карты (DT_MapTable.DirName). Символы,
	//   запрещённые FPaths::MakeValidFileName (/?:&\\*"<>|%#@^), отбрасываются — как в прежних двух копиях пути.
	inline FString WalkGridObjectPath(const FString& DirName)
	{
		return FString::Printf(TEXT("/Game/Map/%s/DA_WalkGrid.DA_WalkGrid"), *ToAssetName(FPaths::MakeValidFileName(DirName)));
	}

	// Континент карты мира (комбобокс 1721): 0 «Альпеон», 1 «Инферно». Индекс — id карты.
	inline constexpr int8 kContinentById[kMapCount] = { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	inline int32 ContinentOf(int32 MapId)
	{
		if (MapId >= 0 && MapId < kMapCount) { return kContinentById[MapId]; }
		// Вне реестра — правило эталона worldMapDlg_SelectCurrentMap@0xD66A20: континент 1, если id >= 12 или == 7.
		return (MapId >= 12 || MapId == 7) ? 1 : 0;
	}

	// Кроп городской большой карты (hudIconSetUvBySlot@0xB54450, isSpecialSlotIndex): начало и масштаб в пикселях.
	struct FTownCrop { int32 MapId; float OriginX; float OriginY; float Scale; };
	inline constexpr FTownCrop kTownCrops[] = {
		{ 0, 256.f, 256.f, 2.f },   // silalis
		{ 3, 25.f, 231.f, 2.f },   // alpen
		{ 7, 0.f, 0.f, 2.f },   // flammio
	};
	inline const FTownCrop* FindTownCrop(int32 MapId)
	{
		for (const FTownCrop& C : kTownCrops) { if (C.MapId == MapId) { return &C; } }
		return nullptr;
	}

	// ★ 19.09.2026: «туман-альфа» карты из map\<папка>\<папка>.SCE (эталон `CMap_LoadExTileInfo@0x11D3370`, поля
	//   +0x0C…+0x18 → c90/FogInfo EmptyProject.fx): fog = saturate(Near + (Far − Near)·max(0, d² − Start²)/(End² − Start²)),
	//   d — расстояние камера–точка (игровые юниты = юниты UE). Ключ — имя ассета уровня (/Game/Map/<Asset>/<Asset>).
	struct FSceFog { int32 MapId; const TCHAR* Asset; float Start; float End; float Near; float Far; };
	inline constexpr FSceFog kSceFogs[] = {
		{  0, TEXT("silalis"), 8000.f, 12500.f, 1.f, 0.f },
		{  1, TEXT("rein"), 5000.f, 10000.f, 1.f, 0.3f },
		{  2, TEXT("conquester"), 8000.f, 11000.f, 1.f, 0.f },
		{  3, TEXT("alpen"), 5000.f, 11000.f, 1.f, 0.3f },
		{  4, TEXT("alpen_dungeon"), 4000.f, 12500.f, 1.f, 0.f },
		{  5, TEXT("nightvalley"), 50.f, 7000.f, 0.85f, 0.f },
		{  6, TEXT("sunnycoast"), 11500.f, 12500.f, 1.f, 0.8f },
		{  7, TEXT("flammio"), 10000.f, 11500.f, 1.f, 0.6f },
		{  8, TEXT("palace_of_queen"), 3000.f, 5000.f, 0.95f, 0.5f },
		{  9, TEXT("castle_of_chess"), 2000.f, 5000.f, 0.9f, 0.1f },
		{ 10, TEXT("t_palace_of_queen"), 3000.f, 5000.f, 0.95f, 0.5f },
		{ 11, TEXT("t_castle_of_chess"), 2000.f, 5000.f, 0.9f, 0.1f },
		{ 12, TEXT("Mountain_Flamion"), 700.f, 9000.f, 0.8f, 0.2f },
		{ 13, TEXT("flabis"), 3000.f, 10000.f, 0.9f, 0.2f },
		{ 14, TEXT("StagnantDesert"), 3000.f, 10000.f, 0.9f, 0.2f },
		{ 15, TEXT("dragonlair"), 5000.f, 10000.f, 0.85f, 0.2f },
		{ 16, TEXT("firewayforest"), 5000.f, 20000.f, 0.8f, 0.f },
		{ 17, TEXT("castle_of_chaos"), 2000.f, 10000.f, 0.9f, 0.1f },
		{ 18, TEXT("inferion"), 1000.f, 5000.f, 1.f, 0.2f },
		{ 19, TEXT("battlefield"), 8000.f, 12500.f, 1.f, 0.f },
		{ 22, TEXT("quizstage"), 1000.f, 28000.f, 1.f, 0.f },
		{ 23, TEXT("mine"), 5000.f, 10000.f, 1.f, 0.3f },
	};
	// Запись «туман-альфы» по имени ассета уровня (без регистра); nullptr — карты нет в реестре или .SCE не было.
	inline const FSceFog* FindSceFogByAsset(const FString& AssetName)
	{
		for (const FSceFog& F : kSceFogs) { if (AssetName.Equals(F.Asset, ESearchCase::IgnoreCase)) { return &F; } }
		return nullptr;
	}

	// Области карты мира в порядке ключей комбобокса «Область» (uiInitSlotIndexMaps@0xD66320;
	//   подписи helpWndFillNoticeList@0xD66880: string-ID в DT_GameText).
	struct FWorldMapAreaDef { int32 Continent; int32 MapId; bool bTown; int32 TextId; };
	inline constexpr FWorldMapAreaDef kWorldMapAreas[] = {
		{ 0,  3, true , 96110 },   // [Город] Альпия
		{ 0,  3, false, 96111 },   // [Поле]  Альпия
		{ 0,  1, false, 96112 },   // [Поле]  Река Дождя
		{ 0,  0, false, 96113 },   // [Поле]  Силарис
		{ 0,  0, true , 96114 },   // [Город] Силарис
		{ 0,  6, false, 96115 },   // [Поле]  Солнечный Берег
		{ 0,  2, false, 96116 },   // [Поле]  Страна Веч. Войн
		{ 0,  8, false, 96117 },   // [Особый] Дворец Королевы
		{ 0,  4, false, 96118 },   // [Особый] Долина Ночи
		{ 0,  9, false, 96119 },   // [Особый] Шахматный замок
		{ 1,  7, true , 96210 },   // [Город] Пламения
		{ 1,  7, false, 96211 },   // [Поле]  Пламения
		{ 1, 12, false, 96212 },   // [Особый] Гора Пламень
		{ 1, 13, false, 96213 },   // [Поле]  Флабис
		{ 1, 14, false, 96214 },   // [Особый] Застывшая Пустыня
		{ 1, 15, false, 96215 },   // [Особый] Логово Дракона
		{ 1, 16, false, 96216 },   // [Поле]  Огненная Чаща
		{ 1, 17, false, 96217 },   // [Особый] Замок Хаоса
		{ 1, 18, false, 96218 },   // [Особый] Инферион
	};

	// Поиск группы: кнопки карт-фильтров (IDNum контрола → UI-индекс 1…N, он же смещение подписи от 11801).
	//   Проволочный код карты в пакетах = id карты (доказано подписями 11802…11817 ↔ remapIndexForward@0xDBA900).
	struct FPartyFindButton { int32 IdNum; int32 UiIndex; const TCHAR* Idc; };
	inline constexpr FPartyFindButton kPartyFindButtons[] = {
		{ 8664,  1, TEXT("Find_Party_MapN00") },   // alpen
		{ 8665,  2, TEXT("Find_Party_MapN01") },   // rein
		{ 8666,  4, TEXT("Find_Party_MapN02") },   // silalis
		{ 8667,  6, TEXT("Find_Party_MapN03") },   // conquester
		{ 8668,  7, TEXT("Find_Party_MapN04") },   // sunnycoast
		{ 8669, 12, TEXT("Find_Party_MapN05") },   // flammio
		{ 8670, 13, TEXT("Find_Party_MapN06") },   // flabis
		{ 8674,  3, TEXT("Find_Party_MapD00") },   // palace_of_queen
		{ 8675,  5, TEXT("Find_Party_MapD01") },   // castle_of_chess
		{ 8676,  8, TEXT("Find_Party_MapD02") },   // t_palace_of_queen
		{ 8677, 10, TEXT("Find_Party_MapD03") },   // t_castle_of_chess
		{ 8684,  9, TEXT("Find_Party_MapE00") },   // nightvalley
		{ 8685, 11, TEXT("Find_Party_MapE01") },   // alpen_dungeon
		{ 8686, 14, TEXT("Find_Party_MapE02") },   // Mountain Flamion
		{ 8687, 15, TEXT("Find_Party_MapE03") },   // StagnantDesert
		{ 8688, 16, TEXT("Find_Party_MapE04") },   // DragonLaiR
	};
	// UI-индекс 0…16 → id карты (−1 = «все»). Порт remapIndexForward@0xDBA900.
	inline constexpr int32 kPartyFindUiToMap[] = { -1, 3, 1, 8, 0, 9, 2, 6, 10, 5, 11, 4, 7, 13, 12, 14, 15 };
	// [id карты + 1] (−1…15) → UI-индекс. Порт remapIndexReverse@0xDBAA50.
	inline constexpr int32 kPartyFindMapToUi[] = { 0, 4, 2, 6, 1, 11, 9, 7, 12, 3, 5, 8, 10, 14, 13, 15, 16 };

	// ★ Открыта ли карта на СЕРВЕРЕ: зеркало OpenMapInfo.ini (MAP_<id> / Use) из maps[].server.open_use.
	//   ОБНОВЛЯТЬ ПРИ ОТКРЫТИИ/ЗАКРЫТИИ КАРТЫ на сервере (правка JSON → генерация; `--check` сверяет с живым).
	//   Клиенту взять это неоткуда: напольный портал сервера (sub_52C4C0) молча игнорирует зону, ведущую на карту
	//   с Use=0, пакета об этом нет; у оригинала граф трекера (pathGraph_BuildNavMesh) закрытость не знает.
	//   Нет записи в OpenMapInfo (open_use = null) — 0: сервер такую карту не открывает.
	inline constexpr int8 kServerMapOpen[kMapCount] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1 };
	inline bool IsMapOpenOnServer(int32 MapId)
	{
		// Вне реестра — закрыта: сервер отвергает номер карты вне своей таблицы (dword_5F5EE8 <= id).
		return MapId >= 0 && MapId < kMapCount && kServerMapOpen[MapId] != 0;
	}

	// Граф межкартовых переходов для трекера квестов: прямоугольники зон из серверных
	//   GameServer\PortalIP_Info\<Имя>_PortalConfig.Ini ([POSX, POSX+RANGEX] × [POSZ, POSZ+RANGEZ], в клетках).
	struct FPortalEdge { int32 From; int32 To; int32 X0; int32 Z0; int32 X1; int32 Z1; };
	inline constexpr FPortalEdge kPortalGraph[] = {
		// Alpen
		{  3,  1, 463,345, 465,349 },   // → Rein
		// Castle_of_Chaos
		{ 17, 16, 477,249, 479,259 },   // → firewayforest
		{ 17, 16,  50,450,  52,465 },   // → firewayforest
		{ 17, 16, 100,223, 108,225 },   // → firewayforest
		// Castle_of_Chess
		{  9,  0,  43,458,  45,462 },   // → Sillaris
		{  9,  0, 458, 43, 460, 47 },   // → Sillaris
		// Conquester
		{  2,  0, 121,454, 125,456 },   // → Sillaris
		{  2,  4, 159, 51, 164, 53 },   // → Night_Valley
		{  2,  7, 460, 80, 462, 84 },   // → Flammio
		{  2, 10, 363,460, 365,464 },   // → t_Palace_of_Queen
		// DragonLair
		{ 15,  7, 403,490, 423,495 },   // → Flammio
		// FirewayForest
		{ 16, 13,  83, 49,  89, 51 },   // → Flabis
		{ 16, 17, 145,477, 147,482 },   // → Castle_of_Chaos
		{ 16, 18, 464, 91, 469, 93 },   // → Inferion
		// Flabis
		{ 13,  7,  34,128,  36,133 },   // → Flammio
		{ 13, 14, 481,316, 483,321 },   // → StagnantDesert
		{ 13, 16, 147,485, 152,487 },   // → firewayforest
		// Flammio
		{  7,  2,  90, 29,  95, 31 },   // → Conquester
		{  7, 12,  27,353,  29,358 },   // → MountainFlamion
		{  7, 13, 480,134, 485,136 },   // → Flabis
		{  7, 15, 389, 24, 394, 26 },   // → DragonLaiR
		// Inferion
		{ 18, 16,  39, 75,  41, 80 },   // → firewayforest
		// LostValley
		{  5,  3, 193,477, 198,479 },   // → Alpen
		// MountainFlamion
		{ 12,  7, 493, 67, 495, 85 },   // → Flammio
		// Night_Valley
		{  4,  2, 187,471, 191,473 },   // → Conquester
		// Palace_of_Queen
		{  8,  1, 127,276, 131,278 },   // → Rein
		{  8,  1, 118,235, 122,237 },   // → Rein
		// Rein
		{  1,  0, 488,349, 490,353 },   // → Sillaris
		{  1,  3,  21,343,  23,347 },   // → Alpen
		{  1,  8,  44,394,  48,398 },   // → Palace_of_Queen
		// Sillaris
		{  0,  1,  41,384,  43,388 },   // → Rein
		{  0,  2, 144, 25, 148, 27 },   // → Conquester
		{  0,  9, 366, 27, 370, 29 },   // → Castle_of_Chess
		{  0,  6, 441,482, 445,484 },   // → SunnyCoast
		// StagnantDesert
		{ 14, 13,  21, 81,  23, 91 },   // → Flabis
		// SunnyCoast
		{  6,  0, 337, 35, 342, 37 },   // → Sillaris
		{  6, 11,  66,273,  68,277 },   // → t_Castle_of_chess
		// T_Castle_of_Chess
		{ 11,  6,  43,458,  45,462 },   // → SunnyCoast
		{ 11,  6, 458, 43, 460, 47 },   // → SunnyCoast
		// T_Palace_of_Queen
		{ 10,  2, 127,276, 131,278 },   // → Conquester
		{ 10,  2, 118,235, 122,237 },   // → Conquester
	};
}
