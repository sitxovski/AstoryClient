// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMobTextureRow.h — строка DataTable `DT_MobTextures`: ЭТАЛОННЫЕ текстуры моба ПО ТИПУ (MobId).
//
// ★ ЗАЧЕМ ОТДЕЛЬНАЯ ТАБЛИЦА. В оригинале текстура сабмеша моба НЕ хранится в модели: `matTable`
//   в `.jbm` содержит авторские имена материалов из 3ds Max («ragepig.png», кракозябры), и клиент
//   их для мобов не читает. Реальные имена .TEX лежат в ПЕР-КАРТОВЫХ файлах и выбираются ПО ТИПУ МОБА:
//     • `<map>.MON` — список id мобов карты;
//     • `<map>.MLK` — для каждого id таблица ПАР (A, B) по одной на сабмеш;
//     • `<map>.MBB` — для каждой группы A путь «\Data\Mob\texture\<имя>.TEX».
//   Эталон: modelBuildMeshFromCache@0x11A45E0 копирует дескриптор текстуры `a8[2] + 32*A`
//   в слот сабмеша (+8); таблица пар выбирается по позиции MobId в `.MON`
//   (mobEffect_LoadArchiveStep@0xF81B90, состояние 0x15); имена групп читает CMap_LoadNLK@0x1165760.
//   Следствие: ОДНА модель обслуживает много типов с РАЗНЫМИ текстурами (перекраски: синий/красный/
//   жёлтый «деревень» — это `mon_m_0_ewood01-a_00`.jbm с текстурами -a_00…-a_06). Поэтому материал
//   нельзя держать только на ассете меша — нужен пер-типовой оверрайд.
//
// Импортёр: Tools/asda_build_mob_texture_dt.py (правило резолва — Tools/asda_mob_texmap.py).
// Применение: AAsdaMobActor::ApplyMobTextures() (OnConstruction + BeginPlay).
// RowName = MobId-1 (та же индексация, что у DT_MobNames/DT_MobSounds).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaMobTextureRow.generated.h"

USTRUCT(BlueprintType)
struct FAsdaMobTextureRow : public FTableRowBase
{
	GENERATED_BODY()

	// Имя модели `.jbm` этого типа моба — Mob list.BIN поле **+0x04** (эталон CMap_LoadMOB@0xF71A50).
	// Гард применения: если у актора меш ДРУГОЙ модели, оверрайд не применяем.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobTex") FString Model;

	// Имена слотов материала меша (как в ассете). Параллельны Materials.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobTex") TArray<FName> Slots;

	// Материалы по слотам (MI_<текстура> из /Game/AsdaAssets/Models/MobTex). Параллельны Slots.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobTex")
	TArray<TSoftObjectPtr<class UMaterialInterface>> Materials;

	// Имена .TEX эталона по слотам (для сверки/отладки; не используется в рантайме).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobTex") TArray<FString> TexNames;
};
