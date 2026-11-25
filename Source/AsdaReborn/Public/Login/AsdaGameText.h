// Copyright (c) 2026 @sitxovski (Discord)
// Строка игровой текст-таблицы (импорт из error list.bin → CSV → UE DataTable DT_GameText).
// Ключ строки (RowName) = string-ID игры; колонка SourceString = текст (cp1251-декод, с <FD>-разметкой).
// Правило never-invent: весь UI-текст берётся ОТСЮДА (из оригинала), не пишется руками.
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaGameText.generated.h"

USTRUCT()
struct FAsdaGameTextRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FString SourceString;
};

// ★★★ Аудит №4 (A8): `FAsdaLoginHelperRow` УДАЛЁН — это была структура-БЛИЗНЕЦ.
//   `loginhelpertext.bin` (помощник Минопио) была импортирована в ДВА ассета под две одинаковые
//   по полям структуры: `DT_LoginHelper` (Text/, эта структура) и `DT_loginhelpertext` (ItemDB/,
//   `FAsdaLoginhelpertextRow` из `Items/AsdaItemDbRows.h`). Даты разошлись на 19 дней, и живой
//   потребитель читал СТАРУЮ копию. Оставлен ассет ItemDB — он в манифесте и обновляется
//   пайплайном импорта; ассет `Text/DT_LoginHelper` выведен из Content.
//   Поля были ровно те же: TabIndex (mode 0=Sation/1=Praise/2=Warning) + Text (cp1251, <FB>-разметка).

// ★ Универсальная строка ЛЮБОЙ игровой текст-таблицы (Data/*.BIN → CSV → UE DataTable по категориям
//   /Game/AsdaAssets/Text/<Категория>/). RowName=id записи. Колонки = все варианты текст-полей (каждая
//   таблица использует подмножество). Правило never-invent: текст ТОЛЬКО из оригинала.
USTRUCT()
struct FAsdaTextRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) int32 id = 0;         // ID записи (дубль RowName, для удобства)
	UPROPERTY(EditAnywhere) FString name;         // имя (NPC/карта/скилл/предмет/рецепт/…)
	UPROPERTY(EditAnywhere) FString desc;         // описание/тултип
	UPROPERTY(EditAnywhere) FString subtitle;     // подзаголовок (квесты)
	UPROPERTY(EditAnywhere) FString text;         // текст (туториал)
	UPROPERTY(EditAnywhere) FString family;       // имя семьи (NFT)
	UPROPERTY(EditAnywhere) FString village;      // имя деревни (TIM)
	UPROPERTY(EditAnywhere) FString tex;          // ссылка на текстуру (клан)
	UPROPERTY(EditAnywhere) FString npc;          // база NPC (диалоги)
	UPROPERTY(EditAnywhere) int32 line = 0;       // индекс реплики (диалоги)
};
