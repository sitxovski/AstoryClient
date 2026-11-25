// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/AsdaRenderSettings.h"
#include "AsdaLocalLightActor.generated.h"

class UPointLightComponent;
class USpotLightComponent;
class ULocalLightComponent;
class UStaticMeshComponent;
class UCurveFloat;
class UMaterialInstanceDynamic;

// ★ Сет E, фаза E-4 (07.09.2026; setE4_prep_report.md §2.3): АКТОР-ОБЁРТКА ЛОКАЛЬНОГО ДИНАМИЧЕСКОГО СВЕТА владельца.
//   Владелец перетаскивает актор (или BP-наследник с мешем), выбирает Preset и Role — OnConstruction применяет пресет из
//   UAsdaRenderSettings (Project Settings → Asda → Render; числа из Tools/asda_light_presets.json) и инварианты проекта:
//   компонент ВСЕГДА Movable (r.AllowStaticLighting=False — Static/Stationary не светит), единицы Candelas, обратные квадраты,
//   MaxDrawDistance > 0 (иначе r.LightMaxDrawDistanceScale уровня бессилен), без объёмной и DF-тени; тени = Role==Main &&
//   пресет разрешает (авторское; UAsdaLightBudgetSubsystem может временно снять по бюджету уровня). Роль пишется тегом в Tags —
//   тот же тег читают аудит (Tools/asda_audit_local_lights.py) и бюджет.
//   bScaleBySun: сила I[кд] = IntensityRelSun · E_sun(карты) · DRefM², E_sun = Intensity (лк) DirectionalLight карты (Sun_<map>,
//   иначе самый яркий) — шкала яркости проекта относительная (солнце alpen 1.59 лк); нет солнца → IntensityCdAlpen.
//   Custom = пресет не применяется (все значения руками на компоненте), инварианты — да. Всё EditAnywhere (ue-editor-friendly).
UCLASS(BlueprintType, Blueprintable, HideCategories = (Input, Replication, Collision, Rendering, HLOD, Cooking, DataLayers, WorldPartition))
class ASDAREBORN_API AAsdaLocalLightActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaLocalLightActor();

	// Корень (позиция света)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asda|Light")
	TObjectPtr<USceneComponent> Root;

	// Point-компонент (активен при LightClass Point; иначе скрыт и bAffectsWorld=false — класс компонента не сменить после конструктора)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asda|Light")
	TObjectPtr<UPointLightComponent> Point;

	// Spot-компонент (активен при LightClass Spot; направлен по forward актора)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asda|Light")
	TObjectPtr<USpotLightComponent> Spot;

	// Опциональный меш (столб/факел/кристалл); для Glow — эмиссив-материал пресета + bEmissiveLightSource
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Asda|Light")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// Пресет (ключ UAsdaRenderSettings::LightPresets)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light", meta = (DisplayName = "Пресет"))
	EAsdaLightPreset Preset = EAsdaLightPreset::StreetLantern;

	// Роль (тег актора Main/Fill/Glow); при смене пресета в редакторе берётся DefaultRole пресета
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light", meta = (DisplayName = "Роль"))
	EAsdaLightRole LightRole = EAsdaLightRole::Main;   // (имя LightRole: Role занято AActor::Role — репликация)

	// Множитель силы поверх пресета (1 = пресет)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light", meta = (DisplayName = "Множитель силы", ClampMin = "0.0", UIMax = "5.0"))
	float IntensityScale = 1.f;

	// Пересчитывать силу от солнца карты (I = rel·E_sun·d²); false = IntensityCdAlpen пресета × множитель
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light", meta = (DisplayName = "Сила от солнца карты"))
	bool bScaleBySun = true;

	// Явная сила в кд вместо пресета/солнца
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Переопределения", meta = (DisplayName = "Своя сила", InlineEditConditionToggle))
	bool bOverrideIntensity = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Переопределения", meta = (DisplayName = "Сила, кд", EditCondition = "bOverrideIntensity", ClampMin = "0.0"))
	float IntensityCd = 15.f;

	// Явный цвет (sRGB) вместо температуры/цвета пресета
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Переопределения", meta = (DisplayName = "Свой цвет", InlineEditConditionToggle))
	bool bOverrideColor = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Переопределения", meta = (DisplayName = "Цвет sRGB", EditCondition = "bOverrideColor"))
	FColor LightColorSRGB = FColor(255, 214, 170);

	// Явный радиус затухания, юн
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Переопределения", meta = (DisplayName = "Свой радиус", InlineEditConditionToggle))
	bool bOverrideRadius = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Переопределения", meta = (DisplayName = "Радиус, юн", EditCondition = "bOverrideRadius", ClampMin = "1.0"))
	float AttenuationRadius = 1000.f;

	// Явное авторское решение о тенях (вместо «Role==Main && пресет разрешает»); бюджет уровня всё равно поверх
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Переопределения", meta = (DisplayName = "Свои тени", InlineEditConditionToggle))
	bool bOverrideShadows = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Переопределения", meta = (DisplayName = "Тени", EditCondition = "bOverrideShadows"))
	bool bCastShadows = false;

	// Кривая мерцания (факел): множитель силы по времени (X = фаза 0..1 одного периода, Y ≈ 1 ± амплитуда); пусто = без Tick
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Мерцание", meta = (DisplayName = "Кривая мерцания"))
	TObjectPtr<UCurveFloat> FlickerCurve;

	// Частота мерцания, Гц (период кривой в секунду)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Мерцание", meta = (DisplayName = "Частота, Гц", ClampMin = "0.1", ClampMax = "60.0"))
	float FlickerHz = 8.f;

	// Амплитуда мерцания (0.15 = ±15 %): I = I₀ · (1 + Amp · (Curve(t) − 1))
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Light|Мерцание", meta = (DisplayName = "Амплитуда", ClampMin = "0.0", ClampMax = "1.0"))
	float FlickerAmplitude = 0.15f;

	// Результат последнего применения (для контроля в деталях): сила кд, E_sun, откуда сила
	UPROPERTY(VisibleAnywhere, Transient, Category = "Asda|Light|Результат", meta = (DisplayName = "Сила итог, кд"))
	float ResolvedIntensityCd = 0.f;
	UPROPERTY(VisibleAnywhere, Transient, Category = "Asda|Light|Результат", meta = (DisplayName = "E_sun карты, лк"))
	float ResolvedSunLux = -1.f;
	UPROPERTY(VisibleAnywhere, Transient, Category = "Asda|Light|Результат", meta = (DisplayName = "Источник силы"))
	FString ResolvedIntensitySource;

	// Применить пресет + инварианты к компонентам (вызывается сам в OnConstruction/BeginPlay; кнопка в деталях)
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Asda|Light", meta = (DisplayName = "Применить пресет"))
	void ApplyPreset();

	// Активный компонент света (Point или Spot по пресету)
	UFUNCTION(BlueprintCallable, Category = "Asda|Light")
	ULocalLightComponent* GetActiveLight() const;

	// Авторское «тени включены» этого актора (до бюджета): Role==Main && пресет разрешает, либо bOverrideShadows
	UFUNCTION(BlueprintCallable, Category = "Asda|Light")
	bool GetAuthoredCastShadows() const;

	// E_sun карты (лк): Intensity DirectionalLight `Sun_*` мира (иначе самого яркого); < 0 = солнца нет
	static float FindSunLux(UWorld* World);

	// Имя тега роли (Main/Fill/Glow) — общий с аудитом и бюджетом
	static FName RoleTag(EAsdaLightRole R);

	virtual void OnConstruction(const FTransform& Xf) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& E) override;
#endif

private:
	// Базовая сила после ApplyPreset (мерцание множит её)
	float BaseIntensityCd = 0.f;
	// Фаза мерцания 0..1
	float FlickerPhase = 0.f;
	// MID меша с эмиссивом (создаётся при первом применении пресета со материалом)
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EmissiveMID;

	// Инварианты проекта на ОДИН компонент (Movable, кд, inv², MaxDraw>0, без объёмной/DF тени)
	void ApplyInvariants(ULocalLightComponent* L, const FAsdaLightPresetSpec* P) const;
	// ★ Мир из UAsdaCullSettings::SkipWorlds (Login, L_Loading): отсечка дальности там не применяется НИ К ЧЕМУ —
	//   ни к мешам (UAsdaCullDistanceSubsystem), ни к фонарям. Сцена логина построена в масштабе ×100, камера
	//   стоит в 68 000–87 000 юн от постановки, и MaxDrawDistance пресета (1500–4000) гасил бы любой фонарь.
	bool IsNoDrawCullWorld() const;

public:
	// ★★★ ВОЛНА Ф9 (10.09.2026): ОТСЕЧКА ДАЛЬНОСТИ СВЕТОВ КАРТ ВЫКЛЮЧЕНА ГЛОБАЛЬНО (asda.Light.DrawCull 0).
	//   Требование владельца «свет не должен исчезать при удалении камеры» относится и к фонарям карт,
	//   а не только к эффект-светам. ПУБЛИЧНАЯ и статическая, потому что тот же ответ нужен
	//   UAsdaLightBudgetSubsystem: у него своя страховка «MaxDrawDistance=0 → 4000» (аудит E02/W03),
	//   и без общего ответа она в тот же кадр возвращала бы дальность обратно — то есть ключ
	//   молча не работал бы (проверено 10.09 на L_LightTest: «MaxDrawDistance=0 исправлено у 27»).
	static bool IsDrawCullDisabled();

private:
	// Записать тег роли (снять чужие роли)
	void ApplyRoleTag();
	// Сообщить бюджет-подсистеме авторское значение теней (в игровых мирах) или поставить прямо (в редакторе)
	void PushAuthoredShadows(ULocalLightComponent* L, bool bAuthored);
};
