// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemDbRows.h — АВТО-СГЕНЕРИРОВАНО gen_itemdb_structs.py. НЕ РЕДАКТИРОВАТЬ ВРУЧНУЮ.
// USTRUCT(BlueprintType) FTableRow-строка на КАЖДУЮ таблицу item-БД (Bin.bbz).
// Типы полей 1:1 из Tools/decode_item_db.py REGISTRY (реверс форматов подтверждён).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaItemDbRows.generated.h"

// item_list  (stride=264, entry=item_list.bin)
USTRUCT(BlueprintType)
struct FAsdaItemRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 ItemCode = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") FString ItemName;   // +0x4 s64
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 EnchantOrSocket = 0;   // +0x4C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 RequiredLevel = 0;   // +0x50 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 RequiredLevelMax = 0;   // +0x54 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 ClassRestriction = 0;   // +0x58 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 UsageCategory = 0;   // +0x5C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 LinkCode = 0;   // +0x60 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 LinkedGroupCode = 0;   // +0x64 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 ItemType = 0;   // +0x80 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 ItemSubType = 0;   // +0x84 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 AttackRangeTiles = 0;   // +0x8C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 ItemVariant = 0;   // +0x90 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 TradableFlag = 0;   // +0x98 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 CategoryFlag = 0;   // +0x9C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 SubCategory = 0;   // +0xA0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 Price = 0;   // +0xA4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 FlagWord176 = 0;   // +0xB0 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 TooltipFlag200 = 0;   // +0xC8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 DurationOrEnchant = 0;   // +0xD8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 LinkWordCode = 0;   // +0xDC i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 GroupCode = 0;   // +0xE4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 SetCode = 0;   // +0xE8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 ItemGrade = 0;   // +0xEC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") int32 ImgIndex = 0;   // +0xF0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_list") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// item_img_table  (stride=300, entry=item_img_table.bin)
USTRUCT(BlueprintType)
struct FAsdaImgRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") FString MeshMale;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") FString TexMale;   // +0x24 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") FString MeshFemale;   // +0x44 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") FString TexFemale;   // +0x64 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") FString IconAtlasMale;   // +0x84 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") int32 IconCellMale = 0;   // +0xA4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") int32 IconSmallFlag = 0;   // +0xA8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") FString IconAtlasFemale;   // +0xAC s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") int32 IconCellFemale = 0;   // +0xCC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") int32 DropSoundCat = 0;   // +0xD0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") int32 ImgSubType = 0;   // +0xD4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") int32 SubTypeMale = 0;   // +0x120 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") int32 SubTypeFemale = 0;   // +0x124 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_img_table") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// item_tooltip  (stride=220, entry=item_tooltip.bin)
//   note: запись = ItemCode + Description char[200]; числового хвоста нет (было заужено s164)
USTRUCT(BlueprintType)
struct FAsdaItemTooltipRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_tooltip") int32 ItemCode = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_tooltip") FString Description;   // +0x4 s200
};

// item_upgrade_cost  (stride=72, entry=item_upgrade_cost.bin)
//   note: Name=char[64] (CP949 корейское имя материала — БЫЛ порезан на 16 int); ItemCode/Value HIGH
USTRUCT(BlueprintType)
struct FAsdaItemUpgradeCostRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_upgrade_cost") FString Name;   // +0x0 s64
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_upgrade_cost") int32 ItemCode = 0;   // +0x40 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_upgrade_cost") int32 Value = 0;   // +0x44 i32
};

// item_upgrade_price  (stride=8, entry=item_upgrade_price.bin)
USTRUCT(BlueprintType)
struct FAsdaItemUpgradePriceRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_upgrade_price") int32 Index = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="item_upgrade_price") int32 Value = 0;   // +0x4 i32
};

// itemoptiontable  (stride=404, entry=itemoptiontable.bin)
//   note: 2 поля HIGH: OptionId + FmtString printf-шаблон (БЫЛ ошибочно 101 dword — это буфер строки)
USTRUCT(BlueprintType)
struct FAsdaItemoptiontableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptiontable") int64 OptionId = 0;   // +0x0 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptiontable") FString FmtString;   // +0x4 s400
};

// itemoptionvalue  (stride=164, entry=itemoptionvalue.bin)
//   note: LoadItemOptionValue2 (packed в Bin.bbz): id + 20 пар (Level=ступень, Value=значение опции) MED
USTRUCT(BlueprintType)
struct FAsdaItemoptionvalueRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Id = 0;   // +0x0 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level00 = 0;   // +0x4 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level01 = 0;   // +0xC u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level02 = 0;   // +0x14 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level03 = 0;   // +0x1C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level04 = 0;   // +0x24 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level05 = 0;   // +0x2C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level06 = 0;   // +0x34 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level07 = 0;   // +0x3C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level08 = 0;   // +0x44 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level09 = 0;   // +0x4C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level10 = 0;   // +0x54 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level11 = 0;   // +0x5C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level12 = 0;   // +0x64 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level13 = 0;   // +0x6C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level14 = 0;   // +0x74 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level15 = 0;   // +0x7C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level16 = 0;   // +0x84 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level17 = 0;   // +0x8C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level18 = 0;   // +0x94 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Level19 = 0;   // +0x9C u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value00 = 0;   // +0x8 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value01 = 0;   // +0x10 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value02 = 0;   // +0x18 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value03 = 0;   // +0x20 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value04 = 0;   // +0x28 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value05 = 0;   // +0x30 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value06 = 0;   // +0x38 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value07 = 0;   // +0x40 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value08 = 0;   // +0x48 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value09 = 0;   // +0x50 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value10 = 0;   // +0x58 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value11 = 0;   // +0x60 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value12 = 0;   // +0x68 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value13 = 0;   // +0x70 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value14 = 0;   // +0x78 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value15 = 0;   // +0x80 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value16 = 0;   // +0x88 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value17 = 0;   // +0x90 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value18 = 0;   // +0x98 u32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionvalue") int64 Value19 = 0;   // +0xA0 u32
};

// itemoptionrange  (stride=20, entry=itemoptionrange.bin)
//   note: loose IPV (ItemOptionValue.BIN): OptionType=ItemOptionTable.OptionId; Min/MaxValue=сырой диапазон стата (сервер роллит) — всё HIGH
USTRUCT(BlueprintType)
struct FAsdaItemoptionrangeRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionrange") int32 RowId = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionrange") int32 GroupId = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionrange") int32 OptionType = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionrange") int32 MinValue = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemoptionrange") int32 MaxValue = 0;   // +0x10 i32
};

// premiumleveltable  (stride=28, entry=premiumleveltable.bin)
//   note: loose PremiumLevelTable.bin: ключ = (Level, ItemGrade, ItemType, UsageCategory|ItemSubType), значение = StatMin/StatMax (диапазон «Эффект ЖК» shell-предмета) — всё HIGH (подтверждено единственным консьюмером)
USTRUCT(BlueprintType)
struct FAsdaPremiumleveltableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premiumleveltable") int32 Level = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premiumleveltable") int32 ItemGrade = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premiumleveltable") int32 ItemType = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premiumleveltable") int32 UsageCategory = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premiumleveltable") int32 ItemSubType = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premiumleveltable") int32 StatMin = 0;   // +0x14 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premiumleveltable") int32 StatMax = 0;   // +0x18 i32
};

// making_recipe  (stride=128, entry=making_recipe.bin)
//   note: loose NPC/making_recipe.BIN: рецепты крафта. Mat0..Mat5 = ЕДИНСТВЕННЫЙ источник списка сырья (категория 7 автосортировки инвентаря). Pad64/Pad70/Pad126 в файле всегда 0, Unk68/Unk69 клиент не читает
USTRUCT(BlueprintType)
struct FAsdaMakingRecipeRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Id = 0;   // +0x0 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") FString RecipeName;   // +0x2 s62
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Pad64 = 0;   // +0x40 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Category = 0;   // +0x42 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 CraftLevel = 0;   // +0x43 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Unk68 = 0;   // +0x44 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Unk69 = 0;   // +0x45 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Pad70 = 0;   // +0x46 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 ResultCode = 0;   // +0x48 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Mat0 = 0;   // +0x4C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Mat1 = 0;   // +0x50 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Mat2 = 0;   // +0x54 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Mat3 = 0;   // +0x58 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Mat4 = 0;   // +0x5C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Mat5 = 0;   // +0x60 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 MatCount0 = 0;   // +0x64 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 MatCount1 = 0;   // +0x66 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 MatCount2 = 0;   // +0x68 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 MatCount3 = 0;   // +0x6A i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 MatCount4 = 0;   // +0x6C i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 MatCount5 = 0;   // +0x6E i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 SubRecipe0 = 0;   // +0x70 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 SubRecipe1 = 0;   // +0x72 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 SubRecipe2 = 0;   // +0x74 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 SubRecipe3 = 0;   // +0x76 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 SubRecipe4 = 0;   // +0x78 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 SubRecipe5 = 0;   // +0x7A i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 IdCopy = 0;   // +0x7C i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_recipe") int32 Pad126 = 0;   // +0x7E i16
};

// making_rate  (stride=52, entry=making_rate.bin)
//   note: loose NPC/making_rate.BIN: исходы ступеней ремесла по MakeIndex рецепта (making_recipe +124). OutItem0..6 — иконки ступеней окна 77500; без записи окно изготовления не открывается
USTRUCT(BlueprintType)
struct FAsdaMakingRateRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 MakeId = 0;   // +0x0 u16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 Flag2 = 0;   // +0x2 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 Pad3 = 0;   // +0x3 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 BonusQtyChance = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutItem0 = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutItem1 = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutItem2 = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutItem3 = 0;   // +0x14 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutItem4 = 0;   // +0x18 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutItem5 = 0;   // +0x1C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutItem6 = 0;   // +0x20 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutQty0 = 0;   // +0x24 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutQty1 = 0;   // +0x26 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutQty2 = 0;   // +0x28 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutQty3 = 0;   // +0x2A i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutQty4 = 0;   // +0x2C i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutQty5 = 0;   // +0x2E i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 OutQty6 = 0;   // +0x30 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="making_rate") int32 Pad50 = 0;   // +0x32 i16
};

// itemchangegrade  (stride=8, entry=itemchangegrade.bin)
USTRUCT(BlueprintType)
struct FAsdaItemchangegradeRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemchangegrade") int32 Grade = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemchangegrade") int32 Cost = 0;   // +0x4 i32
};

// itemopgraft  (stride=12, entry=itemopgraft.bin)
USTRUCT(BlueprintType)
struct FAsdaItemopgraftRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemopgraft") int32 LevelLow = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemopgraft") int32 LevelHigh = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="itemopgraft") int32 Value = 0;   // +0x8 i32
};

// special_itemlist  (stride=316, entry=special_itemlist.bin)
//   note: Name=char[32] (БЫЛ s128, перекрывал блок); SubType@80=ключ populateBooster; Price/IconPath/ModelEntry/EffectEntry HIGH; Field*=offset HIGH/семантика MED
USTRUCT(BlueprintType)
struct FAsdaSpecialItemlistRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Code = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") FString Name;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 RequiredLevel = 0;   // +0x44 u16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 ItemLevel = 0;   // +0x48 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Field76 = 0;   // +0x4C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 SubType = 0;   // +0x50 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 ResultModel = 0;   // +0x54 u16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Field88 = 0;   // +0x58 u16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Field92 = 0;   // +0x5C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 CooldownBase = 0;   // +0x60 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Flag101 = 0;   // +0x65 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Flag102 = 0;   // +0x66 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Type103 = 0;   // +0x67 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Value108 = 0;   // +0x6C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Price = 0;   // +0x70 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 LinkGroupId = 0;   // +0x75 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Field120 = 0;   // +0x78 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Flag124 = 0;   // +0x7C u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 Field128 = 0;   // +0x80 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") FString TexEntry132;   // +0x84 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") FString IconPath;   // +0xA4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 IconCell = 0;   // +0xC4 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") FString Field197;   // +0xC5 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") FString ModelEntry;   // +0xE6 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") FString EffectEntry;   // +0x106 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 HeightOffset = 0;   // +0x127 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") int32 DropColorIndex = 0;   // +0x128 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="special_itemlist") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// specialitem_tooltiplist  (stride=252, entry=specialitem_tooltiplist.bin)
USTRUCT(BlueprintType)
struct FAsdaSpecialitemTooltiplistRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="specialitem_tooltiplist") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="specialitem_tooltiplist") FString Name;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="specialitem_tooltiplist") FString Tooltip;   // +0x24 s180
};

// specialitem_buftooltiplist  (stride=236, entry=specialitem_buftooltiplist.bin)
USTRUCT(BlueprintType)
struct FAsdaSpecialitemBuftooltiplistRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="specialitem_buftooltiplist") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="specialitem_buftooltiplist") FString Name;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="specialitem_buftooltiplist") FString Desc;   // +0x24 s180
};

// event_item_list  (stride=148, entry=event_item_list.bin)
//   note: полная раскладка 148 MED
USTRUCT(BlueprintType)
struct FAsdaEventItemListRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="event_item_list") int32 Idx = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="event_item_list") FString Str;   // +0x4 s64
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="event_item_list") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// booster_list  (stride=284, entry=booster_list.bin)
//   note: 35 пар (ItemCode + Weight=шанс/100000, Σвесов=100000) — всё HIGH
USTRUCT(BlueprintType)
struct FAsdaBoosterListRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Code = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot00ItemCode = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot01ItemCode = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot02ItemCode = 0;   // +0x14 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot03ItemCode = 0;   // +0x1C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot04ItemCode = 0;   // +0x24 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot05ItemCode = 0;   // +0x2C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot06ItemCode = 0;   // +0x34 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot07ItemCode = 0;   // +0x3C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot08ItemCode = 0;   // +0x44 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot09ItemCode = 0;   // +0x4C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot10ItemCode = 0;   // +0x54 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot11ItemCode = 0;   // +0x5C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot12ItemCode = 0;   // +0x64 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot13ItemCode = 0;   // +0x6C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot14ItemCode = 0;   // +0x74 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot15ItemCode = 0;   // +0x7C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot16ItemCode = 0;   // +0x84 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot17ItemCode = 0;   // +0x8C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot18ItemCode = 0;   // +0x94 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot19ItemCode = 0;   // +0x9C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot20ItemCode = 0;   // +0xA4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot21ItemCode = 0;   // +0xAC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot22ItemCode = 0;   // +0xB4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot23ItemCode = 0;   // +0xBC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot24ItemCode = 0;   // +0xC4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot25ItemCode = 0;   // +0xCC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot26ItemCode = 0;   // +0xD4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot27ItemCode = 0;   // +0xDC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot28ItemCode = 0;   // +0xE4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot29ItemCode = 0;   // +0xEC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot30ItemCode = 0;   // +0xF4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot31ItemCode = 0;   // +0xFC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot32ItemCode = 0;   // +0x104 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot33ItemCode = 0;   // +0x10C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot34ItemCode = 0;   // +0x114 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot00Weight = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot01Weight = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot02Weight = 0;   // +0x18 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot03Weight = 0;   // +0x20 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot04Weight = 0;   // +0x28 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot05Weight = 0;   // +0x30 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot06Weight = 0;   // +0x38 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot07Weight = 0;   // +0x40 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot08Weight = 0;   // +0x48 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot09Weight = 0;   // +0x50 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot10Weight = 0;   // +0x58 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot11Weight = 0;   // +0x60 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot12Weight = 0;   // +0x68 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot13Weight = 0;   // +0x70 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot14Weight = 0;   // +0x78 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot15Weight = 0;   // +0x80 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot16Weight = 0;   // +0x88 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot17Weight = 0;   // +0x90 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot18Weight = 0;   // +0x98 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot19Weight = 0;   // +0xA0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot20Weight = 0;   // +0xA8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot21Weight = 0;   // +0xB0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot22Weight = 0;   // +0xB8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot23Weight = 0;   // +0xC0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot24Weight = 0;   // +0xC8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot25Weight = 0;   // +0xD0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot26Weight = 0;   // +0xD8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot27Weight = 0;   // +0xE0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot28Weight = 0;   // +0xE8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot29Weight = 0;   // +0xF0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot30Weight = 0;   // +0xF8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot31Weight = 0;   // +0x100 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot32Weight = 0;   // +0x108 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot33Weight = 0;   // +0x110 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") int32 Slot34Weight = 0;   // +0x118 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="booster_list") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// premium_gatya  (stride=20, entry=premium_gatya.bin)
//   note: пул наград ПРЕМИУМ-гачи; зеркало серверной ItemTable_Info\PrimiumGaChaTable.txt. SlotNumber(+4) = серверный iSlotNumber, т.е. КАКОЙ АВТОМАТ выдаёт приз (2-я колонка txt, парсер sub_4BCAD0 кладёт её в байт rec+2, гейт Recv_UserPrimiumGaCha@0x4F6F10 требует iSlotNumber < числа строк PrimiumGaChaPriceTable.txt). ViewSlot(+12) = в ВИТРИНЕ какого автомата приз показан; эталон uiRefreshItemGridPage@0xB6EE70 фильтрует сетку 68300 именно по нему (*(rec+12) == выбранный автомат), −1 = не показывать. Quantity(+16) = 4-я колонка txt
USTRUCT(BlueprintType)
struct FAsdaPremiumGatyaRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premium_gatya") int32 Index = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premium_gatya") int32 SlotNumber = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premium_gatya") int32 ItemCode = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premium_gatya") int32 ViewSlot = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premium_gatya") int32 Quantity = 0;   // +0x10 i32
};

// premium_gatya_data  (stride=8, entry=premium_gatya_data.bin)
//   note: ЦЕНА РОЗЫГРЫША премиум-гачи по слоту-автомату; зеркало серверной ItemTable_Info\PrimiumGaChaPriceTable.txt. Читается PremiumGatya_LoadData@0xB6F720 (заголовок 20 б, счётчик @4, магия @12 "PMD", затем fread(ptr, 8, dataCount, f)), значение берёт list_GetSelectedEntryCode@0xB6F2C0 — 2-й dword записи по индексу слота, вне диапазона возвращает 0. ⚠ ДЕФЕКТ ДАННЫХ ОРИГИНАЛА: в файле ДВЕ записи ({0,690} и {1,690}), а счётчик @4 = 1, поэтому оригинал видит только первый автомат; импортируем ОБЕ (закон fix-original-defects). Столько же строк обязано быть в серверном файле — гейт Recv_UserPrimiumGaCha@0x4F6F10 требует slot < числа строк, иначе 6578 статус 0
USTRUCT(BlueprintType)
struct FAsdaPremiumGatyaDataRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premium_gatya_data") int32 Slot = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="premium_gatya_data") int32 Price = 0;   // +0x4 i32
};

// packagelist  (stride=36, entry=packagelist.bin)
USTRUCT(BlueprintType)
struct FAsdaPackagelistRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagelist") int32 Code = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagelist") FString AssetName;   // +0x4 s32
};

// packagetable  (stride=56, entry=packagetable.bin)
USTRUCT(BlueprintType)
struct FAsdaPackagetableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Index = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Item0 = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Qty0 = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Item1 = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Qty1 = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Item2 = 0;   // +0x14 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Qty2 = 0;   // +0x18 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Item3 = 0;   // +0x1C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Qty3 = 0;   // +0x20 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Item4 = 0;   // +0x24 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Qty4 = 0;   // +0x28 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Item5 = 0;   // +0x2C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Qty5 = 0;   // +0x30 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="packagetable") int32 Reserved52 = 0;   // +0x34 i32
};

// pricetable  (stride=68, entry=pricetable.bin)
USTRUCT(BlueprintType)
struct FAsdaPriceRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 ItemCode = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 Price = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw02 = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw03 = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw04 = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw05 = 0;   // +0x14 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw06 = 0;   // +0x18 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw07 = 0;   // +0x1C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw08 = 0;   // +0x20 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw09 = 0;   // +0x24 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw10 = 0;   // +0x28 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw11 = 0;   // +0x2C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pricetable") int32 dw12 = 0;   // +0x30 i32
};

// decompositiontable  (stride=120, entry=decompositiontable.bin)
//   note: 6 групп (Code,Weight,MinQty,MaxQty) — взвеш.рандом-дроп, Σвесов=100000; всё HIGH (было неверно 24 слота)
USTRUCT(BlueprintType)
struct FAsdaDecompositiontableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 ItemCode = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 ResultDrawCount = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result0Code = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result0Weight = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result0MinQty = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result0MaxQty = 0;   // +0x14 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result1Code = 0;   // +0x18 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result1Weight = 0;   // +0x1C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result1MinQty = 0;   // +0x20 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result1MaxQty = 0;   // +0x24 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result2Code = 0;   // +0x28 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result2Weight = 0;   // +0x2C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result2MinQty = 0;   // +0x30 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result2MaxQty = 0;   // +0x34 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result3Code = 0;   // +0x38 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result3Weight = 0;   // +0x3C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result3MinQty = 0;   // +0x40 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result3MaxQty = 0;   // +0x44 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result4Code = 0;   // +0x48 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result4Weight = 0;   // +0x4C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result4MinQty = 0;   // +0x50 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result4MaxQty = 0;   // +0x54 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result5Code = 0;   // +0x58 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result5Weight = 0;   // +0x5C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result5MinQty = 0;   // +0x60 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="decompositiontable") int32 Result5MaxQty = 0;   // +0x64 i32
};

// graderate  (stride=24, entry=graderate.bin)
USTRUCT(BlueprintType)
struct FAsdaGraderateRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="graderate") int32 GradeIndex = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="graderate") int32 RatePercent = 0;   // +0x4 i32
};

// upgradetable  (stride=44, entry=upgradetable.bin)
//   note: RateBasePercent=базовый % успеха; ApplyItemTypeGroup=класс цели; ReqTargetGrade 5=любой — всё HIGH
USTRUCT(BlueprintType)
struct FAsdaUpgradetableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="upgradetable") int32 EventId = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="upgradetable") int32 LvlMin = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="upgradetable") int32 LvlMax = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="upgradetable") int32 Cost = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="upgradetable") int32 RateBasePercent = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="upgradetable") int32 ApplyItemTypeGroup = 0;   // +0x14 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="upgradetable") int32 ReqTargetGrade = 0;   // +0x18 i32
};

// refinehigh  (stride=68, entry=refinehigh.bin)
//   note: loose Data\NPC\RefineHigh.BIN (994 записи, = живому серверу 1:1); ResultItemType/ResultItemGrade=ключ матча результата (HIGH); GoldCost геом.рост; ConstField60=90000
USTRUCT(BlueprintType)
struct FAsdaRefinehighRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Idx = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 ResultRequiredLevel = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 RefineLevel = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 ResultItemType = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 ResultItemGrade = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Reserved20 = 0;   // +0x14 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat1Code = 0;   // +0x18 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat1Qty = 0;   // +0x1C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat1Price = 0;   // +0x20 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat2Code = 0;   // +0x24 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat2Qty = 0;   // +0x28 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat2Price = 0;   // +0x2C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat3Code = 0;   // +0x30 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat3Qty = 0;   // +0x34 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 Mat3Price = 0;   // +0x38 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 ConstField60 = 0;   // +0x3C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refinehigh") int32 GoldCost = 0;   // +0x40 i32
};

// refineupmoney  (stride=4, entry=refineupmoney.bin)
//   note: фикс.блок 40б, count(header) игнор; строка=уровень заточки
USTRUCT(BlueprintType)
struct FAsdaRefineupmoneyRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="refineupmoney") int32 Money = 0;   // +0x0 i32
};

// advalue  (stride=12, entry=advalue.bin)
//   note: Index HIGH; ValA/ValB LOW (константы=3, консьюмер не локализован)
USTRUCT(BlueprintType)
struct FAsdaAdvalueRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="advalue") int32 Index = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="advalue") int32 ValA = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="advalue") int32 ValB = 0;   // +0x8 i32
};

// combo_time  (stride=156, entry=combo time.bin)
//   note: ClipName=имя аним-клипа (БЫЛ порезан на ComboFlag); StepCount; 3 трека FrameTimeA/B/C ×33.333 кадры→мс
USTRUCT(BlueprintType)
struct FAsdaComboTimeRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") int32 Field0 = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") int32 Field4 = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") FString ClipName;   // +0x8 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") int32 StepCount = 0;   // +0x28 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float WindowTime = 0.f;   // +0x2C f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA0 = 0.f;   // +0x30 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA1 = 0.f;   // +0x34 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA2 = 0.f;   // +0x38 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA3 = 0.f;   // +0x3C f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA4 = 0.f;   // +0x40 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA5 = 0.f;   // +0x44 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA6 = 0.f;   // +0x48 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA7 = 0.f;   // +0x4C f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeA8 = 0.f;   // +0x50 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB0 = 0.f;   // +0x54 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB1 = 0.f;   // +0x58 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB2 = 0.f;   // +0x5C f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB3 = 0.f;   // +0x60 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB4 = 0.f;   // +0x64 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB5 = 0.f;   // +0x68 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB6 = 0.f;   // +0x6C f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB7 = 0.f;   // +0x70 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeB8 = 0.f;   // +0x74 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC0 = 0.f;   // +0x78 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC1 = 0.f;   // +0x7C f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC2 = 0.f;   // +0x80 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC3 = 0.f;   // +0x84 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC4 = 0.f;   // +0x88 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC5 = 0.f;   // +0x8C f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC6 = 0.f;   // +0x90 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC7 = 0.f;   // +0x94 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="combo_time") float FrameTimeC8 = 0.f;   // +0x98 f32
};

// charequiptable  (stride=16, entry=charequiptable.bin)
USTRUCT(BlueprintType)
struct FAsdaCharequiptableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="charequiptable") int32 ItemCode = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="charequiptable") int32 Gender = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="charequiptable") int32 SlotType = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="charequiptable") int32 PrevCode = 0;   // +0xC i32
};

// charexp  (stride=16, entry=charexp.bin)
USTRUCT(BlueprintType)
struct FAsdaCharexpRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="charexp") int32 ExpToNext = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="charexp") int32 Zero = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="charexp") int32 CumExp = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="charexp") int32 Zero2 = 0;   // +0xC i32
};

// smcharexp  (stride=16, entry=smcharexp.bin)
USTRUCT(BlueprintType)
struct FAsdaSmcharexpRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="smcharexp") int32 ExpToNext = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="smcharexp") int32 Zero = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="smcharexp") int32 CumExp = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="smcharexp") int32 Zero2 = 0;   // +0xC i32
};

// hairtable  (stride=312, entry=hairtable.bin)
//   note: Gender(1М/2Ж)/Enabled/HairStyle/IconCell HIGH; хвост@111=Description (текст тултипа, не 2-й меш)
USTRUCT(BlueprintType)
struct FAsdaHairtableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") int32 Id = 0;   // +0x0 u16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") FString Name;   // +0x2 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") int32 Gender = 0;   // +0x22 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") int32 RequiredLevel = 0;   // +0x23 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") int32 Enabled = 0;   // +0x24 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") int32 Price = 0;   // +0x28 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") int32 HairStyle = 0;   // +0x2C u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") FString Mesh;   // +0x2D s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") int32 ColorIdx = 0;   // +0x4D u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") FString Icon;   // +0x4E s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") int32 IconCell = 0;   // +0x6E u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") FString Description;   // +0x6F s200
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="hairtable") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// jobtype  (stride=64, entry=jobtype.bin)
USTRUCT(BlueprintType)
struct FAsdaJobtypeRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="jobtype") FString JobName;   // +0x0 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="jobtype") FString ParentJob;   // +0x20 s32
};

// sowell_list  (stride=188, entry=sowell_list.bin)
//   note: полная SowelRecord (границы доказаны пофилдовым дешифром+capstone 241 консьюмера): BonusStatType/Value, PartGrade, GradeColorIndex, GradeReq HIGH
USTRUCT(BlueprintType)
struct FAsdaSowellRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") FString Name;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 BonusStatType = 0;   // +0x24 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 PartGrade = 0;   // +0x25 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 BonusValue = 0;   // +0x28 i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 SpecialFlag = 0;   // +0x2C u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 SocketTargetType = 0;   // +0x2D u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 SocketTargetType2 = 0;   // +0x2E u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 SellPrice = 0;   // +0x30 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 BuyPrice = 0;   // +0x34 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 GradeReq = 0;   // +0x40 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 RequiredLevel = 0;   // +0x41 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 OptionMask = 0;   // +0x44 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") FString IconPath;   // +0x49 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") FString MeshEntry2;   // +0x69 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") FString MeshEntry;   // +0x89 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") int32 GradeColorIndex = 0;   // +0xA9 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_list") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// sowell_tooltip  (stride=220, entry=sowell_tooltip.bin)
USTRUCT(BlueprintType)
struct FAsdaSowellTooltipRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_tooltip") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_tooltip") FString Text;   // +0x4 s200
};

// sowell_calc_table  (stride=24, entry=sowell_calc_table.bin)
//   note: коэффициенты формулы силы совела (роль каждого LOW)
USTRUCT(BlueprintType)
struct FAsdaSowellCalcTableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_calc_table") float Coef0 = 0.f;   // +0x0 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_calc_table") float Coef1 = 0.f;   // +0x4 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_calc_table") float Coef2 = 0.f;   // +0x8 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_calc_table") float Coef3 = 0.f;   // +0xC f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_calc_table") float Coef4 = 0.f;   // +0x10 f32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="sowell_calc_table") float Coef5 = 0.f;   // +0x14 f32
};

// title_list  (stride=256, entry=title_list.bin)
//   note: OptionTypeId[6]→title_option, IsPercent[6] HIGH (значение стата даёт сервер); ColorIndex=грейд HIGH
USTRUCT(BlueprintType)
struct FAsdaTitleRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 SortNum = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 ClassReq = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") FString Name;   // +0x10 s64
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 ColorIndex = 0;   // +0x50 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 Group = 0;   // +0x54 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 Position = 0;   // +0x58 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 CategoryLocIndex = 0;   // +0x5C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 Point = 0;   // +0x60 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") FString Desc;   // +0x64 s64
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 OptionTypeId0 = 0;   // +0xB0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 OptionTypeId1 = 0;   // +0xB4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 OptionTypeId2 = 0;   // +0xB8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 OptionTypeId3 = 0;   // +0xBC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 OptionTypeId4 = 0;   // +0xC0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 OptionTypeId5 = 0;   // +0xC4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 Enabled = 0;   // +0xC8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 IsPercent0 = 0;   // +0xCC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 IsPercent1 = 0;   // +0xD0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 IsPercent2 = 0;   // +0xD4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 IsPercent3 = 0;   // +0xD8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 IsPercent4 = 0;   // +0xDC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") int32 IsPercent5 = 0;   // +0xE0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// title_list_old  (stride=176, entry=title_list_old.bin)
//   note: ЛЕГАСИ: загрузчика в клиенте НЕТ, формат не реверсился -> raw
USTRUCT(BlueprintType)
struct FAsdaTitleListOldRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list_old") int32 Dw0 = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_list_old") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// title_effect  (stride=276, entry=title_effect.bin)
USTRUCT(BlueprintType)
struct FAsdaTitleEffectRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") FString EffectName0;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") FString EffectName1;   // +0x24 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") FString EffectName2;   // +0x44 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") FString EffectName3;   // +0x64 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") FString EffectName4;   // +0x84 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") FString EffectName5;   // +0xA4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") FString EffectName6;   // +0xC4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_effect") FString EffectName7;   // +0xE4 s32
};

// title_option  (stride=52, entry=title_option.bin)
//   note: словарь 109 статов (ссылка из title_list.OptionTypeId)
USTRUCT(BlueprintType)
struct FAsdaTitleOptionRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_option") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_option") FString OptionStatName;   // +0x4 s32
};

// title_rank  (stride=84, entry=title_rank.bin)
//   note: АТЛАС ЗНАЧКОВ рейтинга: IconTexName + IconSrcX/Y/Width/Height (DrawTextureRect) HIGH; CategoryKey/Threshold HIGH
USTRUCT(BlueprintType)
struct FAsdaTitleRankRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 CategoryKey = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 ThresholdMin = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 Unk12 = 0;   // +0xC i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 ThresholdMax = 0;   // +0x10 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") FString IconTexName;   // +0x14 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 IconSrcX = 0;   // +0x34 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 IconSrcY = 0;   // +0x38 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 IconWidth = 0;   // +0x3C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") int32 IconHeight = 0;   // +0x40 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="title_rank") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// soulmateskilltable  (stride=272, entry=soulmateskilltable.bin)
//   note: SkillName@36=ресурс иконки HIGH; Reserved268=0 (не читается)
USTRUCT(BlueprintType)
struct FAsdaSoulmateskilltableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskilltable") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskilltable") FString DisplayName;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskilltable") FString SkillName;   // +0x24 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskilltable") FString Text;   // +0x44 s200
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskilltable") int32 Reserved268 = 0;   // +0x10C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskilltable") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// soulsoulmatetext  (stride=204, entry=soulsoulmatetext.bin)
//   note: ИСТОЧНИК ПЕРЕОПРЕДЕЛЁН: в CLIENT_MINE файл вырожден (25 записей / 2 уникальные строки), берётся русский Фантазиум (25/25). Формат идентичен, раскладка не менялась
USTRUCT(BlueprintType)
struct FAsdaSoulsoulmatetextRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulsoulmatetext") int32 Type = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulsoulmatetext") FString Text;   // +0x4 s200
};

// soulmateskill  (stride=112, entry=soulmateskill.ssi)
//   note: БЕЗ header; раскладка из tooltip-билдеров+рендера иконок (capstone) HIGH. ДОБАВЛЕНО 2026-08-31 реверсом навыков Дружбы: +43 ApplyBranch (1=мгновенная ветка 5232, иначе 5233; в данных 0 у всех 45), +46 = ПОЛНЫЙ КУЛДАУН мс (был назван Cost; social.c:5872, network.c:28872), +85 PartnerCooldown (1=кулдаун стартует и у партнёра; network.c:45518, GameServer.c:63258), +88 GroupId = ГРУППА СТУПЕНЕЙ навыка (1={30,36,41} 2={34,38,43} 3={33,37,42}, -1=одиночный)
USTRUCT(BlueprintType)
struct FAsdaSoulmateskillRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 Id = 0;   // +0x0 u16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") FString Name;   // +0x2 s40
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 ReqLevel = 0;   // +0x2A u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 ApplyBranch = 0;   // +0x2B u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 CooldownMs = 0;   // +0x2E i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 Stat1Type = 0;   // +0x32 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 Stat2Type = 0;   // +0x33 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 Range = 0;   // +0x34 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 CooldownType = 0;   // +0x35 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 StatBonus = 0;   // +0x4C i16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 PartnerCooldown = 0;   // +0x55 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 ClassReq = 0;   // +0x57 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") int32 GroupId = 0;   // +0x58 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulmateskill") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// petexptable  (stride=44, entry=petexptable.bin)
//   note: StageName="Larva(1)".. (БЫЛ порезан на int); CostBase=база дюр/наград/fail% — всё HIGH
USTRUCT(BlueprintType)
struct FAsdaPetexptableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="petexptable") int32 Level = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="petexptable") int32 Exp = 0;   // +0x4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="petexptable") int32 CostBase = 0;   // +0x8 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="petexptable") FString StageName;   // +0xC s32
};

// petoptiontable  (stride=68, entry=petoptiontable.bin)
//   note: Format=char[64] printf-шаблон опции (БЫЛ порезан на Unk36..64) — оба HIGH
USTRUCT(BlueprintType)
struct FAsdaPetoptiontableRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="petoptiontable") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="petoptiontable") FString Format;   // +0x4 s64
};

// pettable  (stride=472, entry=pettable.bin)
//   note: Grade/факторы наград/OptionId1→petoption/модель+части/иконка HIGH; боевые статы серверные
USTRUCT(BlueprintType)
struct FAsdaPetRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString Name;   // +0x4 s64
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 Grade = 0;   // +0x44 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 Unk72 = 0;   // +0x48 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 Category = 0;   // +0x4C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 DurabilityFactor = 0;   // +0x50 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 FeedCostFactor = 0;   // +0x54 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 GoldRewardFactor = 0;   // +0x58 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 ExpRewardFactor = 0;   // +0x5C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 IsCombatPet = 0;   // +0x60 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 StatMod = 0;   // +0x64 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 EfficiencyPct = 0;   // +0x68 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 OptionId0 = 0;   // +0x6C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 OptionValue0 = 0;   // +0x70 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 OptionId1 = 0;   // +0x74 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 OptionValue1 = 0;   // +0x78 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 OptRarityCode0 = 0;   // +0x84 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 OptRarityCode1 = 0;   // +0x88 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 OptRarityCode2 = 0;   // +0x8C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 Unk144 = 0;   // +0x90 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString ModelBase;   // +0x94 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString ModelPart0;   // +0xB4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString ModelPart1;   // +0xD4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString ModelPart2;   // +0xF4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString ResultIcon0;   // +0x118 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 ResultIcon0U = 0;   // +0x138 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 ResultIcon0V = 0;   // +0x13C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString ResultIcon1;   // +0x140 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString IconTexture;   // +0x168 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 IconU = 0;   // +0x188 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 IconV = 0;   // +0x18C i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") int32 WeaponClass = 0;   // +0x1D4 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="pettable") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// skilltooltip  (stride=504, entry=skilltooltip.bin)
USTRUCT(BlueprintType)
struct FAsdaSkilltooltipRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="skilltooltip") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="skilltooltip") FString Name;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="skilltooltip") FString Text;   // +0x64 s404
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="skilltooltip") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// emotiontip  (stride=265, entry=emotiontip.bin)
USTRUCT(BlueprintType)
struct FAsdaEmotiontipRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="emotiontip") FString Name;   // +0x0 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="emotiontip") FString Tag;   // +0x20 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="emotiontip") int32 Type = 0;   // +0x40 u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="emotiontip") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// loginhelpertext  (stride=204, entry=loginhelpertext.bin)
//   note: TabIndex=UI-таб 0/1/2 (ui3Tab_SetActiveByMode) HIGH
USTRUCT(BlueprintType)
struct FAsdaLoginhelpertextRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="loginhelpertext") int32 TabIndex = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="loginhelpertext") FString Text;   // +0x4 s200
};

// stateinfotip  (stride=272, entry=stateinfotip.bin)
//   note: IconAtlasIndex@68 (IconAtlas_IndexToUV) HIGH — разрыв Icon..Text заполнен
USTRUCT(BlueprintType)
struct FAsdaStateinfotipRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="stateinfotip") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="stateinfotip") FString Name;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="stateinfotip") FString Icon;   // +0x24 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="stateinfotip") int32 IconAtlasIndex = 0;   // +0x44 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="stateinfotip") FString Text;   // +0x48 s200
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="stateinfotip") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// soulstateinfotip  (stride=272, entry=soulstateinfotip.bin)
//   note: IconAtlasIndex@68 (IconAtlas_IndexToUV) HIGH — разрыв Icon..Text заполнен
USTRUCT(BlueprintType)
struct FAsdaSoulstateinfotipRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulstateinfotip") int32 Id = 0;   // +0x0 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulstateinfotip") FString Name;   // +0x4 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulstateinfotip") FString Icon;   // +0x24 s32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulstateinfotip") int32 IconAtlasIndex = 0;   // +0x44 i32
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulstateinfotip") FString Text;   // +0x48 s200
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="soulstateinfotip") FString DataHex;   // сырой остаток записи (1:1, hex)
};

// skill_info  (stride=273, entry=skill_info.sif)
//   note: SkillRecord: SkillCode/Name/BehaviorType HIGH; ~40 числ.полей в мастер-IDB (не текст-эталон) — в DataHex
USTRUCT(BlueprintType)
struct FAsdaSkillInfoRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="skill_info") int32 SkillCode = 0;   // +0x0 u16
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="skill_info") FString Name;   // +0x2 s40
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="skill_info") int32 BehaviorType = 0;   // +0x2A u8
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="skill_info") FString DataHex;   // сырой остаток записи (1:1, hex)
};
