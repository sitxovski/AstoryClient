// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "AsdaRenderQualitySubsystemBase.generated.h"

class AActor;

// ★★★ ВОЛНА 3 п.12 (15.09.2026, Reports/dedup_audit/wave3_quality_fxlight.md): ОБЩИЙ КАРКАС ПОДСИСТЕМ КАЧЕСТВА РЕНДЕРА.
//   Наследники: UAsdaCullDistanceSubsystem (отсечка дальности мешей), UAsdaLightBudgetSubsystem (бюджет теней локальных
//   светов), UAsdaFxLightSubsystem (отбор эффект-светов). До выноса каждая держала СВОЮ копию одного и того же:
//     * подписку на смену уровня качества (Scalability::OnScalabilitySettingsChanged) и её снятие;
//     * подписку на появление актора в мире (AddOnActorSpawnedHandler) и её снятие;
//     * обход миров процесса «найти мою подсистему в каждом мире и дёрнуть» (MarkAllDirty / RescanAll / ArmDumpAll);
//     * чтение уровня sg.ShadowQuality / sg.EffectsQuality из UGameUserSettings (3, если настроек нет);
//     * чтение уровневого числа из Scalability-ini «секция@N, ключ, запасное значение»;
//     * множитель дальности светов r.LightMaxDrawDistanceScale (ещё и в UAsdaFxLightComponent).
//   ЧТО НЕ ВЫНЕСЕНО НАМЕРЕННО. Тик, период, применение к акторам и откат у трёх подсистем РАЗНЫЕ (троттлинг полного прохода
//   у отсечки, перебалансировка по дистанции у бюджета теней, отбор с арендой мест у эффект-светов) — общая «база применения»
//   была бы выдуманной абстракцией. Разные и ShouldCreateSubsystem (списки миров), и реакция на смену уровня.
//   ПОВЕДЕНИЕ НАСЛЕДНИКОВ НЕ МЕНЯЛОСЬ: те же ключи консоли и умолчания, тот же порядок подписок, те же логи.
UCLASS(Abstract)
class ASDAREBORN_API UAsdaRenderQualitySubsystemBase : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Super::Initialize + подписка на смену уровня качества (колбэк — OnQualityLevelsChanged наследника)
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снять обе подписки (UnbindQualityHooks) + Super::Deinitialize
	virtual void Deinitialize() override;

	// ★ Дедуп №2, В2-C (хвост F17, 11.10.2026): оба уровня ЗАЖАТЫ в 0..4 здесь, в базе (раньше зажимало только меню). Без потерь:
	//   движок и так держит sg.* в 0..4 (`Scalability::SetQualityLevels` → `FQualityLevels::Set*`, Scalability.cpp:344), а выбор
	//   секции `[Группа@N]` (`ScalabilityLevelValue`: <0 → @0, ≥4 → @Cine) от зажима не зависит — самотест `asda.Dedup.W2C.Quality`.
	// Уровень sg.ShadowQuality из UGameUserSettings (3, если настроек нет)
	static int32 ShadowQualityLevel();
	// Уровень sg.VisualEffectQuality (sg.EffectsQuality) из UGameUserSettings (3, если настроек нет)
	static int32 EffectsQualityLevel();

	// ★ Волна 3, хвосты (15.09.2026, аудит 04 (д)3): ЕДИНСТВЕННЫЙ ЧИТАТЕЛЬ УРОВНЕВЫХ КЛЮЧЕЙ Scalability-ini. Перенесён сюда из
	//   UI-подсистемы меню (бывший UAsdaDevMenuSubsystem::ScalabilityLevelValue, тело дословно): значение ключа Key в секции
	//   «<Group>@<N>» (N = max(Level, 0); 4 и выше — «@Cine», Scalability.cpp:340-358) объединённого ini (BaseScalability +
	//   Config/DefaultScalability.ini); хвост «; комментарий» отрезается, пробелы по краям снимаются. Def — если ключа нет или он пуст.
	//   Зовут: меню разработчика (подписи «[пресет]», база дальности теней, матрица «Рельеф») и наследники через LevelValue.
	static FString ScalabilityLevelValue(const TCHAR* Group, int32 Level, const TCHAR* Key, const TCHAR* Def);

	// Строка уровневого ключа из объединённого Scalability-ini: секция «<Group>@<Level>» (4 и выше — «@Cine»).
	//   Пустая строка = ключа нет (или он пуст). Читатель один — ScalabilityLevelValue выше (им же пользуется меню).
	static FString LevelValue(const TCHAR* Group, int32 Level, const TCHAR* Key);
	// То же числом: ключа нет → Fallback, иначе FCString::Atoi (ровно прежняя формула «V.IsEmpty() ? Fallback : Atoi(V)»)
	static int32 LevelInt(const TCHAR* Group, int32 Level, const TCHAR* Key, int32 Fallback);
	// То же числом с плавающей точкой: ключа нет → Fallback, иначе FCString::Atof
	static float LevelFloat(const TCHAR* Group, int32 Level, const TCHAR* Key, float Fallback);

	// Уровневый множитель дальности светов r.LightMaxDrawDistanceScale (0.5 на Low/Medium); ≤ 0 или ключа нет → 1.
	//   Один на проект: гейты отбора эффект-светов и сам компонент обязаны гаснуть ровно там, где движок перестаёт рисовать свет.
	static float LightDrawDistanceScale();

	// Позвать Fn(Подсистема&) для подсистемы класса TSub в каждом мире процесса, где она есть (порядок — GEngine->GetWorldContexts()).
	//   Единая форма прежних MarkAllDirty / RescanAll / ArmDumpAll трёх подсистем.
	template <typename TSub, typename FnT>
	static void ForEachInstance(FnT&& Fn)
	{
		if (!GEngine) { return; }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (UWorld* W = Ctx.World())
			{
				if (TSub* S = W->GetSubsystem<TSub>()) { Fn(*S); }
			}
		}
	}

protected:
	// Уровень качества сменился (Scalability::OnScalabilitySettingsChanged). Наследник решает сам: пометить к переприменению
	//   (отсечка, бюджет теней) или форсировать отбор (эффект-света)
	virtual void OnQualityLevelsChanged() {}
	// Актор появился в мире (подписка ставится BindActorSpawned). По умолчанию — ничего
	virtual void OnActorSpawned(AActor* A) {}

	// Подписаться на появление акторов мира InWorld, если подписки ещё нет (зовётся из OnWorldBeginPlay наследника — там, где стояла копия)
	void BindActorSpawned(UWorld& InWorld);
	// Снять подписки на уровень качества и на появление актора (идемпотентно; Deinitialize базы зовёт само,
	//   наследник может позвать раньше — чтобы сохранить прежний порядок снятия подписок)
	void UnbindQualityHooks();

private:
	// Подписка на смену уровня качества
	FDelegateHandle ScalabilityHandle;
	// Подписка на появление актора в мире
	FDelegateHandle ActorSpawnedHandle;
};
