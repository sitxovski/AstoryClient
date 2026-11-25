// Copyright (c) 2026 @sitxovski (Discord)
// Носимые части персонажа `FAsdaWornParts` — раскладка внешности для сборки визуала (тело, штаны, обувь, перчатки, шлем, лицо, причёска, оружие, щит).
//   ★ R3 рефакторинга (11.11.2026): дословно вынесено из `Characters/AsdaCharAssembly.h` (он включает этот файл — прежние потребители ничего
//   не теряют). Причина: `World/AsdaPlayerCharacter.h` и `World/AsdaWorldSubsystem.h` включали весь `Characters/AsdaCharAssembly.h` только ради этой структуры.
//   Отражение UE (имя типа, модуль `/Script/AsdaReborn`) не менялось.
#pragma once
#include "CoreMinimal.h"
#include "AsdaWornParts.generated.h"

// Носимые части персонажа в КАНОНИЧЕСКОМ порядке сборки. itemId==0 → часть не надета:
// соответствующая базовая секция «голого тела» остаётся видимой (у newbie тело не надето →
// базовый chst001 с руками виден). Эта раскладка = enum itemType эталона (0 шлем..4 перчатки),
// но собрана в поля, а вызывающий сам маппит свой источник (Equip[] char-select / CreatePartCodes[] char-create).
USTRUCT(BlueprintType)
struct FAsdaWornParts
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 Chest = 0;   // тело (itemType 1) → скрыть базовый chst (руки берёт часть тела)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 Pants = 0;   // штаны (2) → скрыть базовый pant
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 Shoes = 0;   // обувь (3) → скрыть базовый shoe
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 Gloves = 0;  // перчатки (4) → оверлей, базу не трогаем (в базе нет grov)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 Helmet = 0;  // шлем (0) → оверлей, базу не трогаем (в базе нет hcap)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 FaceIndex = -1; // лицо 0..10 → текстура секции face001 (эталон SetParts_FaceTexture); -1 = не менять
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 HairStyle = -1; // причёска (HairStyle из hairtable, 0..37) → меш из колонки Mesh таблицы; -1 = без волос
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 HairColor = 0;  // цвет волос 0..2 → текстура ch1_{s}_00_hair00{style+1}_00_0{color}_0 (формула эталона)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 Weapon = 0;    // оружие (itemType 6/9) → StaticMesh ch1_b_XX_* на кости M_DUMMY (эталон CCharacter_SetupModelFromItem@0x9E97A0)
	// ★ 25.10.2026 [ОРИГИНАЛ] ЩИТ — экип-слот 8 (`CCharacter+1588+57*8`, у эталона `self[511]`). Модель —
	//   `CCharacter_SetParts_Armor@0x9EB3C0` (entity_char.c:15670): жёсткий меш ImgRecord+4 (муж) / +68 (жен) на кости
	//   "_7_a1" (в руке) или "_2_a1" (убран за спину), текстура ImgRecord+36 / +100. 0 = щита нет. Сборка — `AttachShield`.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") int32 Shield = 0;
	// ★★★ 25.10.2026 [ОРИГИНАЛ] ПОКАЗ ШЛЕМА — байт персонажа `CCharacter+0x32F78` (`LOBYTE(char[1].worldMatrix[2])`).
	//   `CCharacter_LoadHelmetPart@0x9E3390` (entity_char.c:12003) при 0 НЕ грузит шлем, гасит его эффекты и зовёт
	//   `CCharacter_LoadHairModel(char, 0)` — ОБЫЧНАЯ причёска на месте шлема. `Helmet` (код надетого) при этом не
	//   меняется: шлем надет, просто не рисуется. Конструктор персонажа ставит 1 (entity_char.c:21600), поэтому
	//   превью выбора/создания персонажа шлем показывают всегда. Источники флага: свой — 4003 [8] и окно настроек
	//   (`optApplySettingsFromConfig`), сосед — 4005 [6] при появлении и 4025 действие 111 (живое переключение).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Char") bool bHelmetShown = true;
};
