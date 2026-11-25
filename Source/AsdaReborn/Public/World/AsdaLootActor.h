// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaLootActor — наземный лут (порт DropItemMgr_SpawnGroundItem@0xF443D0, реверс Reports/loot_system_re.md).
//   Сущность = «биллборд» оригинала: ключ = ПАРА ТАЙЛОВ (на тайле максимум один предмет), позиция =
//   центр тайла, Y+10 над землёй, непрерывное вращение вокруг Y 0.0005 рад/мс (~28.6°/с), подпрыгивания нет.
//   Модель: по itemType (0-4 части Itm1_n_00_{hcap|chst|pant|shoe|grov}_{v}_00, v=floor(usageCategory/10);
//   8 щит shld_00_00; 9 оружие по subType swrd/spir/thnd/stap/cbow/lbow/bbow; иначе/золото 20551 — модель из
//   img-записи MeshMale). Текстура = Data\item\texture\<модель>.TEX (порт: Textures/Items/Texture).
//   Имя над лутом: в ЭТАЛОНЕ — только hover (окно 1660/контрол 1661); в ПОРТЕ ПО ПРОСЬБЕ ЮЗЕРА —
//   ВСЕГДА видно, цвет = таблица грейдов GetDropColor@0x9C45C0 (10 цветов, сверено с live.dmp @0x2907A28).
//   Удаление: сервер 5015 (подбор/таймаут 5 мин) — мгновенно; клиентский AoI-деспаун (±1 сектор, 10 с)
//   делает мир-подсистема. Editor-Friendly: все поля видны/правятся в редакторе.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaLootActor.generated.h"

UCLASS()
class ASDAREBORN_API AAsdaLootActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaLootActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	TObjectPtr<class USceneComponent> Root;
	// Вращающийся узел (эталон: угол += 0.0005 рад/мс вокруг Y) — масштаб-зеркало на меше внутри.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	TObjectPtr<class USceneComponent> Spin;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	TObjectPtr<class UStaticMeshComponent> Mesh;

	// Хитбокс пикинга (наведение-курсор «рука» + клик-подбор): маленькая капсула QueryOnly.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	TObjectPtr<class UCapsuleComponent> PickHitbox;

	// Имя предмета над лутом (штатная плашка, Kind=Npc = имя без HP; NameColor = цвет грейда).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	TObjectPtr<class UAsdaNameplateComponent> Nameplate;

	// ── Данные записи 5005 ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	int32 ItemCode = 0;          // 20551 = золото
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	int32 Count = 1;             // стек / сумма золота (в имени НЕ выводится — эталон)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	FIntPoint Tile = FIntPoint(0, 0);   // КЛЮЧ сущности (5012/5015 адресуют тайлами)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	int32 Grade = 0;             // грейд → цвет имени
	// Категория звука дропа (imgRecord+208, 0..11 → Sod_drop_*; -1/255 = нет) — мир-подсистема
	//   играет ОДИН звук на батч 5005 (эталон network.c после цикла спавна).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	int32 DropSoundCat = -1;

	// Время последнего подтверждения жизни (для клиентского AoI-деспауна 10 с вне ±1 сектора).
	double SpawnedAtS = 0.0;

	// ★ БЛЁСТКИ лежащего предмета (эталон DropItem_InitVisual@0xF30040: eff_0_item_99_drop01 создаётся
	//   при спавне, хэндл хранится в биллборде +624 и гасится StopByOwner при удалении — эффект ЖИВЁТ,
	//   пока предмет лежит; JEB: 13×0xC growQuad endMode=1 restart-луп 1.5с + 0xD частицы).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Loot")
	TObjectPtr<AActor> DropFx;

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Собрать визуал по itemCode (модель/текстура/имя/цвет) через item-таблицы. Зовётся после спавна.
	void InitFromItemCode(int32 InItemCode, int32 InCount, const FIntPoint& InTile);

	// Цвет грейда (эталон CItemManager_GetDropColor@0x9C45C0, live.dmp @0x2907A28): 0..9, >9 → [0].
	static FLinearColor GradeColor(int32 InGrade);

	virtual void Tick(float DeltaSeconds) override;

private:
	float SpinAngleRad = 0.f;   // накопленный угол вращения (wrap 2π — эталон billboardList_Update)
};
