// Copyright (c) 2026 @sitxovski (Discord)
// Строка реестра карт (DataTable DT_MapTable). Реверс map_load_system.md: g_aMapDirNames/g_aMapEntityTable
//   @0x2BCD414 (72б/запись, индекс=mapId): +0 dir-имя, +40 отображаемое имя (cp1251), +68 item-restrict, +69 campwar/PK.
//   Порт: карта = экспортированный UE-уровень Content/Map/<dir>/<dir>. RowName DataTable = mapId (строкой).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaMapTable.generated.h"

USTRUCT(BlueprintType)
struct FAsdaMapRow : public FTableRowBase
{
	GENERATED_BODY()

	// Числовой id карты (дубль RowName для удобства; сервер шлёт mapId в 1021/снапшоте).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap")
	int32 MapId = -1;

	// Имя папки карты оригинала (g_aMapDirNames+0), напр. "alpen". Ключ для load-текстур map\<dir>\load.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap")
	FString DirName;

	// Отображаемое имя (g_aMapDirNames+40, cp1251), напр. "Долина Ночи".
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap")
	FText DisplayName;

	// UE-уровень карты (Content/Map/<dir>/<dir>). Пусто = уровень не экспортирован.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap")
	TSoftObjectPtr<UWorld> Level;

	// Ограничение предметов (g_aMapDirNames+68) — сырое значение оригинала.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap")
	int32 ItemRestrict = 0;

	// Флаг campwar/PK (g_aMapDirNames+69) — сырое значение оригинала.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap")
	int32 CampwarPkFlag = 0;

	// ── ★ ЗОНА «ГОРОД / НЕ ГОРОД» (бит 0x80 байта1 атрибута тайла .TER) ────────────────────────────
	// BGM карты. Оригинал держит ДВА музыкальных слота на карту и переключает их по биту 0x80:
	//   слот 0 — «городской», слот 1 — «полевой» (game_main.c:3406/3821, entity_char.c:20980).
	// Источник данных — ЗАГОЛОВОК <map>.TER: ФАЙЛ 0x2F30 (32б) и 0x2F50 (32б), флаги наличия
	//   0x2F70/0x2F74 (эталонные g_bMapAmbientSound0/1; в CMap это +12068/+12100, getRecord32ByIndex@0x965FF0).
	// Пусто = слот в оригинале НЕ заполнен → музыка в этой зоне не играет (1:1, не подставляем чужой трек).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap|Зона")
	FString BgmTown;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap|Зона")
	FString BgmField;

	// Текстура НАЗВАНИЯ ЛОКАЦИИ (баннер вверху экрана), Data/NPC/Image/<name>.TEX.
	//   Эталон Map_SetNameTextureByID@0xDA3700: свитч по mapId, второй аргумент 0 → «городской» вариант.
	//   NameTexTown пусто = у карты нет городского варианта → берётся NameTex (свитч возвращает то же имя).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap|Зона")
	FString NameTex;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AsdaMap|Зона")
	FString NameTexTown;
};
