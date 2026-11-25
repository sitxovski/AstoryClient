// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaViewRangeSubsystem — ДАЛЬНОСТЬ ВИДИМОСТИ: только графика, сеть и создание сущностей не меняются. [ПОРТ]
//   Настройка — ползунок «Дальность прорисовки» (Q9, `asda.Cull.Scale`), значение хранит профиль игрока.
//   Туман карты на ходу настраивается так, чтобы к заданной дальности всё уходило в него; цвет тумана не меняется.
//   Геометрию отсекает UAsdaCullDistanceSubsystem сразу за полным туманом. Объект в полном тумане — плоский силуэт цвета тумана,
//   а небо другого цвета, поэтому купол (M_AsdaSkyDome, M_AsdaSkyHorizon) по глубине пикселя растворяет силуэт в небе ещё до отсечки:
//   числа идут через MPC_AsdaViewRange (ключ `asda.ViewRange.SkyFade`, 0 — откат). [ИСПРАВЛЕНИЕ] 07.11.2026.
//   Мобы и NPC гаснут на дальности / PlayerMul, игроки на самой дальности: фейд, затем без рендера, анимации и эффектов.
//   Актор и его тик живут: положение, HP, усиления и бой обновляются как обычно. Логин и экран загрузки не затрагиваются.
//   Разбор: Reports/view_distance_1106/PLAN.md. Откат — `asda.ViewRange.Enable 0`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AsdaViewRangeSubsystem.generated.h"

class AAsdaEffectActor;
class AAsdaMobActor;
class AAsdaNpcActor;
class AAsdaPetActor;
class UExponentialHeightFogComponent;
class UMaterialParameterCollection;

UCLASS()
class ASDAREBORN_API UAsdaViewRangeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Только игровые миры.
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	// Шаг раз в 0,1 с, обход игроков, NPC и эффектов раз в секунду.
	virtual void Tick(float DeltaTime) override;
	// Метка статистики движка.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaViewRangeSubsystem, STATGROUP_Tickables); }

	// Подсистема мира или nullptr.
	static UAsdaViewRangeSubsystem* Get(const UWorld* W);
	// Дальность видимости мира, юниты; 0 — ключ выключен, логин или загрузка.
	static float RangeUnits(const UWorld* W);

	// Рубильник `asda.ViewRange.Enable`.
	static bool IsOn();
	// Рубильник `asda.ViewRange.SkyFade`: купол растворяет дальние силуэты.
	static bool IsSkyFadeOn();
	// Самотест: начало и конец растворения, которые получил купол мира; юниты.
	void DebugSkyFade(float& OutStart, float& OutEnd) const;
	// Радиус мобов и NPC, тайлы.
	int32 MobTiles() const;
	// Радиус игроков, тайлы.
	int32 PlayerTiles() const;
	// Сколько мобов скрыто дальностью.
	int32 HiddenMobCount() const { return Mobs.Num(); }
	// Сколько игроков скрыто дальностью.
	int32 HiddenPlayerCount() const { return Players.Num(); }
	// Сколько NPC скрыто дальностью.
	int32 HiddenNpcCount() const { return Npcs.Num(); }
	// Сколько эффектов карты скрыто дальностью.
	int32 HiddenFxCount() const { return Fx.Num(); }
	// Самотест: свой клан и клан соседа.
	void DebugSetClan(int32 UserIdx, int32 ClanId);
	// Самотест: питомец соседа в реестр мира.
	void DebugAddPet(int32 CharId, AAsdaPetActor* Pet);
	// ★ R4, доля a (14.11.2026): снять подписки списка компонентов тумана.
	virtual void Deinitialize() override;

private:
	// Состояние скрытого моба.
	enum class EMobHide : uint8
	{
		Fading,   // идёт фейд, тики работают
		Hidden,   // актор скрыт, плашка ещё гасит себя следующим тиком
		Dormant,  // тики меша и плашки выключены
	};
	// Туман одного компонента: значения карты и то, что записала подсистема.
	struct FFogBase
	{
		float Density = 0.f;     // плотность карты с настройками владельца
		float Start = 0.f;       // начало тумана карты
		float MaxOpacity = 1.f;  // потолок непрозрачности карты
		float WroteDensity = -1.f;   // последняя запись подсистемы: другое значение — это новый накат настроек тумана
		float WroteStart = -1.f;     // то же для начала
	};
	// Подогнать туман мира под дальность; 0 — вернуть значения карты.
	void ApplyFog(float Range);
	// Подгонка одного компонента тумана (тело прежнего цикла `ApplyFog`, общее для старого и нового пути).
	void ApplyFogOne(UExponentialHeightFogComponent* C, float Range, float Opacity, float StartFrac);
	// ★ R4: поставить подписки и пересобрать список компонентов тумана, если стоит признак или вышел страховочный срок.
	//   `bCountAge` false — страховочный срок не двигать и не применять (отладочное чтение наборов самотестом).
	void RefreshFogList(bool bCountAge = true);
	// Отдать куполу числа растворения дальних силуэтов.
	void ApplySkyFade(float Range);
	// Шаг мобов: скрытие, показ, выключение тиков.
	void StepMobs(const FIntPoint& SelfTile, bool bOn);
	// Обход игроков.
	void StepPlayers(const FIntPoint& SelfTile, bool bOn);
	// Обход NPC.
	void StepNpcs(const FIntPoint& SelfTile, bool bOn);
	// Обход эффектов карты.
	void StepFx(const FVector& SelfPos, float Range);
	// Показать моба.
	void ShowMob(AAsdaMobActor* M, EMobHide State);
	// Цель, бой или босс: моб не скрывается.
	bool IsMobProtected(const AAsdaMobActor* M) const;

	TMap<TWeakObjectPtr<AAsdaMobActor>, EMobHide> Mobs;            // мобы, скрытые дальностью
	TSet<TWeakObjectPtr<AAsdaMobActor>> KnownMobs;                 // мобы, уже прошедшие проверку: новый вдали скрывается сразу, без фейда
	TSet<int32> Players;                                           // userIdx скрытых игроков
	TSet<int32> Npcs;                                              // ключи скрытых NPC
	TSet<TWeakObjectPtr<AAsdaEffectActor>> Fx;                     // скрытые эффекты карты
	TMap<TWeakObjectPtr<UExponentialHeightFogComponent>, FFogBase> Fog;   // туман мира
	TArray<TWeakObjectPtr<UExponentialHeightFogComponent>> FogList;      // ★ R4: компоненты тумана мира (вместо обхода всех объектов 10 раз в секунду)
	bool bFogListDirty = true;                                           // ★ R4: список надо пересобрать (старт, появился актор с туманом, добавлен уровень)
	bool bFogHooks = false;                                              // ★ R4: подписки на появление актора и добавление уровня стоят
	int32 FogListAge = 0;                                                // ★ R4: шагов с последней пересборки (страховочная пересборка раз в 20 шагов ≈ 2 с)
	FDelegateHandle FogSpawnHandle;                                      // ★ R4: подписка на появление актора в мире
	FDelegateHandle FogLevelHandle;                                      // ★ R4: подписка на добавление уровня в мир
	float Accum = 0.f;                                             // время до следующего шага
	int32 Steps = 0;                                               // счётчик шагов до обхода раз в секунду
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> FadeMpc;              // MPC_AsdaViewRange: числа растворения для купола
	float LastFadeStart = -1.f;                                    // начало растворения, отданное куполу
	float LastFadeEnd = -1.f;                                      // конец растворения, отданный куполу
	float LastRange = -1.f;                                        // дальность прошлого шага: смена перестраивает отсечку
};
