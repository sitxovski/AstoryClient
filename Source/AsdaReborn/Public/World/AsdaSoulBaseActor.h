// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaSoulBaseActor — ДУХ ДРУГА (слот Дружбы 0). Порт объекта `g_pSoulBaseMgr` эталона.
//
// ЧТО ЭТО. Пока у персонажа действует слот 0, над его левым плечом парит дух. Эталон создаёт объект при запуске слота
//   (`SkillEffect_OnAnimEventSpawnSoulRecall@0xAD3D20` → `soulBaseFindOrCreateControl@0xFB48E0`) и убирает при его конце
//   (`CCharFx_SoulRite_EndSlot@0xAD3750` → `soulBaseRemoveById@0xFB4B10`). Виден всем, у кого владелец в мире.
// МОДЕЛЬ. `soul_base_idle01.jbm`, три дорожки (`SoulBase_LoadStep@0xFB6790`): 0 `soul_base_idle01`, 1 `soul_base_idle02`,
//   2 `soul_base_move01`. Сборка: `Tools/asda_build_soulbase_model.py` → `Tools/asda_import_soulbase_model.py`.
// СЛЕДОВАНИЕ (`soulEffAttachToNeckBone@0xFB6B80`). Матрица цели = Translate(−30, +150, 0)[игровые оси] × матрица владельца. Пока в очереди
//   меньше 10 снимков, дух стоит прямо в цели. Дальше берётся самый старый снимок, а из очереди удаляются ВСЕ равные ему
//   (`std::list::remove`). Поэтому на ходу дух отстаёт на 10 кадров, а когда владелец встал, очередь пустеет.
// АНИМАЦИЯ (`UpdateList_AttachedAnim2@0xFB5180`). Дорожка меняется только на конце клипа: очередь ≥ 10 — бег (2), иначе покой:
//   `(rand() + rand()) % 10 >= 2` → дорожка 1, иначе 0.
// ЭФФЕКТЫ (`SoulEff_UpdateByHp@0xFB6ED0`). HP владельца ≥ 35 % — `souleff_base_idle01`, меньше — `souleff_base_hp01`; действует один из
//   двух. Запуск слота даёт разовую вспышку `eff_1_char_99_soulritec-a_03`. Все эффекты стоят на кости `Bip01 Neck` духа.
// ВИДИМОСТЬ (`entity_char.c:8452`, `UpdateList_AttachedAnim2`). Как у владельца.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaSoulBaseActor.generated.h"

class AAsdaEffectActor;
class USceneComponent;
class USkeletalMeshComponent;

UCLASS()
class ASDAREBORN_API AAsdaSoulBaseActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaSoulBaseActor();

	// Корень актора: сюда пишется позиция следования.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Soul")
	TObjectPtr<USceneComponent> Root;
	// Узел масштаба и зеркала, как у персонажей и питомцев: метры glTF → см, компенсация хиральности.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Soul")
	TObjectPtr<USceneComponent> MeshRoot;
	// Скелетный меш духа с тремя клипами.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Soul")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	// Смещение от тела владельца в его локальных осях. Эталон Translate(−30, +150, 0)[игровые оси] → UE (0, −30, 150),
	//   перевод осей тот же, что у питомца (`AAsdaPetActor::FollowOffset`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Soul")
	FVector FollowOffset = FVector(0.f, -30.f, 150.f);

	// Длина очереди следования в кадрах (эталон `>= 0xA`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Soul")
	int32 TrailFrames = 10;

	// Порог HP в долях максимума (эталон 0.34999999).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Soul")
	float LowHpFraction = 0.34999999f;

	// Собрать духа. bBirthFlash — запуск слота; при восстановлении после смены карты вспышки нет.
	void InitSoul(AActor* InOwnerChar, int32 InUserIdx, bool bBirthFlash);

	// Номер владельца.
	int32 GetUserIdx() const { return UserIdx; }
	// Самотест: текущая дорожка.
	int32 DebugTrack() const { return Track; }
	// Самотест: длина очереди.
	int32 DebugQueueNum() const { return Queue.Num(); }
	// Самотест: живы ли эффекты покоя, тревоги и вспышки.
	bool DebugIdleFxOn() const { return IdleFx.IsValid(); }
	bool DebugHpFxOn() const { return HpFx.IsValid(); }
	bool DebugBirthFxOn() const { return BirthFx.IsValid(); }
	// Самотест: загружена ли модель, есть ли кость шеи.
	bool DebugHasMesh() const;
	bool DebugHasNeck() const;
	// Самотест: позиции актора, меша, кости шеи и эффектов одной строкой.
	FString DebugDescribe() const;
	// Самотест: скрыть или показать эффекты отдельно от тела.
	void DebugHideFx(bool bHide) { SetFxHidden(bHide); }

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	// Один снимок цели следования.
	struct FTrailPoint
	{
		FVector Loc = FVector::ZeroVector;   // позиция
		float Yaw = 0.f;                     // поворот
		// Равенство снимков: у эталона побайтовое сравнение матриц, здесь с допуском на поплавок.
		bool Same(const FTrailPoint& O) const { return Loc.Equals(O.Loc, 0.01f) && FMath::IsNearlyEqual(Yaw, O.Yaw, 0.01f); }
	};

	// Включить клип дорожки с начала, он играет один раз.
	void PlayTrack(int32 InTrack);
	// Следование за владельцем (`soulEffAttachToNeckBone`).
	void UpdateFollow(AActor* Own);
	// Выбор дорожки на конце клипа.
	void UpdateTrack(float DeltaSeconds);
	// Эффект покоя или тревоги по HP владельца (`SoulEff_UpdateByHp`).
	void UpdateHpFx();
	// Эффект на кости шеи духа; bOneShot — со сроком по самому долгому эмиттеру.
	AAsdaEffectActor* SpawnNeckFx(const TCHAR* Jeb, bool bOneShot);
	// Скрыть или показать эффекты вместе с духом.
	void SetFxHidden(bool bHide);

	TWeakObjectPtr<AActor> OwnerChar;           // владелец
	int32 UserIdx = -1;                         // номер владельца
	TArray<FTrailPoint> Queue;                  // очередь снимков
	int32 Track = -1;                           // текущая дорожка
	float ClipLeftSec = 0.f;                    // сколько осталось клипу
	bool bBirthPending = false;                 // вспышка ждёт первой постановки духа
	TWeakObjectPtr<AAsdaEffectActor> IdleFx;    // `souleff_base_idle01`
	TWeakObjectPtr<AAsdaEffectActor> HpFx;      // `souleff_base_hp01`
	TWeakObjectPtr<AAsdaEffectActor> BirthFx;   // `soulritec-a_03`
};
