// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaPetActor — питомец игрока (порт, реверс `Reports/pet_system_re.md`).
//   ЭТАЛОН: никакого AI и физики — матрица пета жёстко пересчитывается КАЖДЫЙ КАДР:
//     petMatrix = Translate(−30, +20, −40)[game] × ownerWorldMatrix,
//   затем bHover ? Y += HoverHeight : прижать к рельефу. Анимации — 3 трека: владелец движется → трек 2
//   (бег), стоит → случайно трек 0 или 1 (80/20, эталон rand()%10>=2). Видимость наследуется от владельца.
//   Имя питомца рисуется над ним штатной плашкой (JUI-шаблон 1661 — тот же, что у имён игроков).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/DataTable.h"
#include "AsdaPetActor.generated.h"

// Строка DT_PetModels (импортёр генерит из PetTable.BIN: ModelFile@148, Anim0/1/2@180/212/244,
//   bHover@96, HoverHeight@100). Ключ строки — PetTypeId.
USTRUCT(BlueprintType)
struct FAsdaPetModelRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Pet") int32 PetTypeId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Pet") FString ModelName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Pet") FString Anim0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Pet") FString Anim1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Pet") FString Anim2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Pet") bool bHover = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|Pet") float HoverHeight = 0.f;
};

UCLASS()
class ASDAREBORN_API AAsdaPetActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaPetActor();

	// Корень актора. Пустой узел, а не меш: матрицу питомца эталон пересчитывает КАЖДЫЙ КАДР от
	//   матрицы владельца (см. шапку файла), поэтому трансформ пишется сюда, а меш висит под
	//   `MeshRoot` со своим неизменным масштабом.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Pet")
	TObjectPtr<class USceneComponent> Root;
	// Узел масштаба/зеркала (как у персонажей: glTF-метры → см + компенсация хиральности конвертера).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Pet")
	TObjectPtr<class USceneComponent> MeshRoot;
	// Скелетный меш питомца: модель берётся из `DT_PetModels` по `PetTypeId`, на нём же играются
	//   три клипа (`Anim0`/`Anim1` — покой, `Anim2` — бег). Скелетный, а не статический, именно
	//   из-за клипов: у эталона питомец анимирован всегда, даже стоя.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Pet")
	TObjectPtr<class USkeletalMeshComponent> Mesh;
	// Имя питомца над ним (штатная плашка, Kind=Npc — только имя, без полос).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Pet")
	TObjectPtr<class UAsdaNameplateComponent> Nameplate;

	// ── Данные из сети/таблицы ──
	// Порода питомца — ключ строки `DT_PetModels` (он же `PetIndex` таблицы `PetInfo_Table`
	//   сервера). −1 = актор создан, но ещё не наполнен `InitPet`.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Pet")
	int32 PetTypeId = -1;
	// Кличка питомца, как её прислал сервер. Пишется на плашку `Nameplate`; ПУСТАЯ строка законна —
	//   у неназванного питомца эталон показывает имя породы по умолчанию из `PetInfo_Table`.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Pet")
	FString PetName;
	// Парит ли питомец (bHover из PetTable) и высота парения (HoverHeight, game-единицы = см).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Pet")
	bool bHover = false;
	// Высота парения над рельефом в см. Читается ТОЛЬКО при `bHover == true`: иначе питомца
	//   прижимает к земле, и значение не участвует (см. шапку файла).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Pet")
	float HoverHeight = 0.f;

	// Владелец, за которым следует питомец (жёсткое следование, без AI).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Pet")
	TWeakObjectPtr<AActor> OwnerChar;

	// ★ Смещение питомца в ЛОКАЛЬНЫХ осях владельца (эталон Translate(−30,+20,−40) в game-осях →
	//   UE (+40, −30, +20): сзади-слева и чуть выше). Editor-Friendly: правится в редакторе.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Pet")
	FVector FollowOffset = FVector(40.f, -30.f, 20.f);

	// Собрать питомца: модель/анимации/парение из DT_PetModels по типу + имя на плашку.
	void InitPet(int32 InPetTypeId, const FString& InName, AActor* InOwner);
	// ★ Объектный путь `DT_PetModels` — одно объявление (15.09.2026, dt_gc_hold): читает `InitPet`,
	//   удерживает на сессию `UAsdaWorldSubsystem::HoldSpawnTables(Pet)`.
	static constexpr const TCHAR* kPetModelsPath = TEXT("/Game/AsdaAssets/Data/DT_PetModels.DT_PetModels");

	// Покадровое следование за владельцем: пересчёт матрицы от матрицы хозяина, прижатие к рельефу
	//   либо парение, выбор клипа по скорости владельца и наследование его видимости. Вся работа
	//   актора живёт здесь — ни AI, ни физики у питомца нет (шапка файла).
	virtual void Tick(float DeltaSeconds) override;

	// *** ЭТИ ДВА ОТКРЫТЫ НАРУЖУ РАДИ ПРЕВЬЮ (24.09.2026). Сцена превью сама ведёт клипы: её актор
	//   не тикает, а чередование покоя 0↔1 по концу цикла — правило ОКНА, не мира
	//   (`entity_char.c:62025-62035`, ветка ключа `byte_4AF41D`). Мир ими пользуется из `Tick`
	//   по-прежнему; смысл методов не изменился, изменилась только область видимости.
	// Проиграть клип питомца по номеру трека (0/1 = покой, 2 = движение). Пусто → тихо пропустить.
	void PlayPetClip(int32 TrackIdx, bool bLoop);

	/** Какой трек играет сейчас (−1 — ни одного). */
	UFUNCTION(BlueprintPure, Category="AsdaPet")
	int32 GetCurrentTrack() const { return CurrentTrack; }

private:
	FString ModelName;                 // база ассета модели (для путей клипов)
	int32 CurrentTrack = -1;           // играющий трек (чтобы не перезапускать каждый кадр)
	// Положение хозяина в ПРОШЛОМ кадре — по нему считается его скорость и выбирается клип.
	FVector PrevOwnerLoc = FVector::ZeroVector;
	double NextIdlePickS = 0.0;        // когда выбрать новый idle-вариант (эталон: случайный выбор)
};
