// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AsdaRenderSettings.generated.h"

class UMaterialInterface;
class UTextureLightProfile;

// ★ Сет E, фаза E-4 (07.09.2026; setE4_prep_report.md §2.3, setE_plan_addendum_tiers_and_lights.md §3): НАСТРОЙКИ РЕНДЕРА ПРОЕКТА.
//   Страница Project Settings → Asda → Render (UDeveloperSettings, config=Game defaultconfig → Config/DefaultGame.ini секция
//   [/Script/AsdaReborn.AsdaRenderSettings]). Единственный источник чисел — Tools/asda_light_presets.json: секцию ini ГЕНЕРИРУЕТ
//   Tools/asda_gen_render_settings_ini.py (режим --check сверяет ini с JSON; правка в редакторе → сохранить → прогнать --check,
//   чтобы JSON и ini не разошлись: port-shared-points «один источник»). В C++ чисел пресетов НЕТ намеренно.
//   Читают: AAsdaLocalLightActor (пресеты), UAsdaLightBudgetSubsystem (дефолты дальности, период перебалансировки),
//   Tools/asda_audit_local_lights.py (пороги — из того же JSON).

// Пресет локального света (ключи = presets в asda_light_presets.json; Custom = все значения руками на экземпляре актора).
UENUM(BlueprintType)
enum class EAsdaLightPreset : uint8
{
	StreetLantern UMETA(DisplayName = "Фонарь уличный"),   // Point / Main; тени в пределах бюджета
	Torch         UMETA(DisplayName = "Факел"),            // Point / Fill; тёплый 1800 K
	Candle        UMETA(DisplayName = "Свеча"),            // Point / Fill; теней никогда
	CrystalGlow   UMETA(DisplayName = "Кристалл-свечение"),// Point-спутник эмиссива / Glow; теней никогда, без GI
	Showcase      UMETA(DisplayName = "Витрина"),          // Spot 25°/45° / Fill
	Custom        UMETA(DisplayName = "Свои значения"),    // пресет не применяется; инварианты (Movable, inv², кд, MaxDraw>0) — да
};

// Роль света: пишется тегом в Tags актора — тот же тег читают аудит (Tools/asda_audit_local_lights.py) и бюджет теней.
UENUM(BlueprintType)
enum class EAsdaLightRole : uint8
{
	Main UMETA(DisplayName = "Main — главный (тени)"),      // площадь/вход/у NPC; единственные с тенями, ≤ бюджет уровня
	Fill UMETA(DisplayName = "Fill — заполняющий"),         // ряд вдоль дороги, факелы; без теней
	Glow UMETA(DisplayName = "Glow — спутник свечения"),    // свет рядом с эмиссивным мешем, чтобы светило и на Low/Medium
};

// Класс компонента света пресета (класс компонента после конструктора не сменить — актор держит оба и включает один).
UENUM(BlueprintType)
enum class EAsdaLightClass : uint8
{
	Point UMETA(DisplayName = "PointLight"),
	Spot  UMETA(DisplayName = "SpotLight"),
};

// Один пресет света = запись presets.<Имя> из asda_light_presets.json (имена полей 1:1 с JSON в CamelCase).
//   (имя …Spec: в python-API UE префиксы F/E отбрасываются — FAsdaLightPreset и EAsdaLightPreset слились бы в один AsdaLightPreset,
//   и enum стал недоступен скриптам — грабли первого прогона asda_build_light_test.py 07.09.2026)
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaLightPresetSpec
{
	GENERATED_BODY()

	// Человекочитаемое название (title)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light", meta = (DisplayName = "Название"))
	FString Title;

	// Класс компонента (light_class): PointLight / SpotLight
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light", meta = (DisplayName = "Класс света"))
	EAsdaLightClass LightClass = EAsdaLightClass::Point;

	// Роль по умолчанию (default_role) — актор берёт её, пока владелец не сменил Role руками
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light", meta = (DisplayName = "Роль по умолчанию"))
	EAsdaLightRole DefaultRole = EAsdaLightRole::Fill;

	// Сила относительно солнца карты (intensity_rel_sun): I[кд] = IntensityRelSun · E_sun[лк] · DRefM²
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Сила", meta = (DisplayName = "Сила × солнце", ClampMin = "0.0"))
	float IntensityRelSun = 1.f;

	// Опорная дистанция в метрах (d_ref_m): на ней освещённость от света = IntensityRelSun × солнце
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Сила", meta = (DisplayName = "Опорная дистанция, м", ClampMin = "0.01"))
	float DRefM = 3.f;

	// Та же сила, посчитанная для alpen (intensity_cd_alpen, E_sun 1.59 лк) — когда на карте нет солнца или bScaleBySun выкл
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Сила", meta = (DisplayName = "Сила для alpen, кд", ClampMin = "0.0"))
	float IntensityCdAlpen = 15.f;

	// Цвет температурой (use_temperature) или явным RGB
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Цвет", meta = (DisplayName = "Цвет температурой"))
	bool bUseTemperature = true;

	// Цветовая температура, K (temperature_k)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Цвет", meta = (DisplayName = "Температура, K", ClampMin = "1700", ClampMax = "12000", EditCondition = "bUseTemperature"))
	float TemperatureK = 2400.f;

	// Явный цвет sRGB (light_color_srgb), когда bUseTemperature = false
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Цвет", meta = (DisplayName = "Цвет sRGB", EditCondition = "!bUseTemperature"))
	FColor LightColorSRGB = FColor::White;

	// Радиус затухания, юн (attenuation_radius)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Форма", meta = (DisplayName = "Радиус, юн", ClampMin = "1.0"))
	float AttenuationRadius = 1000.f;

	// Обратные квадраты (use_inverse_squared_falloff) — всегда true у пресетов; иначе кд нефизичны
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Форма", meta = (DisplayName = "Обратные квадраты"))
	bool bUseInverseSquaredFalloff = true;

	// Радиус источника, юн (source_radius) — мягкий край тени; 0 при тенях = жёсткий край (заметка аудита N21)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Форма", meta = (DisplayName = "Радиус источника", ClampMin = "0.0"))
	float SourceRadius = 8.f;

	// Мягкий радиус источника, юн (soft_source_radius) — блик
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Форма", meta = (DisplayName = "Мягкий радиус", ClampMin = "0.0"))
	float SoftSourceRadius = 16.f;

	// Длина источника, юн (source_length) — трубчатый источник; 0 = точка
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Форма", meta = (DisplayName = "Длина источника", ClampMin = "0.0"))
	float SourceLength = 0.f;

	// Внутренний конус, ° (inner_cone_angle) — только Spot
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Форма", meta = (DisplayName = "Конус внутренний, °", ClampMin = "0.0", ClampMax = "80.0"))
	float InnerConeAngle = 25.f;

	// Внешний конус, ° (outer_cone_angle) — только Spot
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Форма", meta = (DisplayName = "Конус внешний, °", ClampMin = "1.0", ClampMax = "80.0"))
	float OuterConeAngle = 45.f;

	// Тени разрешены пресетом при роли Main (cast_shadows_if_role_allows); false = теней никогда (свеча, спутник)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Тени", meta = (DisplayName = "Тени при роли Main"))
	bool bShadowsIfRoleAllows = true;

	// Дальность отсечения света, юн (max_draw_distance); >0 обязательно — иначе r.LightMaxDrawDistanceScale бессилен
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Дальность", meta = (DisplayName = "MaxDrawDistance", ClampMin = "1.0"))
	float MaxDrawDistance = 4000.f;

	// Полоса затухания перед отсечением, юн (max_distance_fade_range)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Дальность", meta = (DisplayName = "Fade", ClampMin = "0.0"))
	float MaxDistanceFadeRange = 1000.f;

	// Участвует в GI Lumen (affect_global_illumination)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Lumen", meta = (DisplayName = "В GI"))
	bool bAffectGlobalIllumination = true;

	// Сила отскока (indirect_lighting_intensity)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Lumen", meta = (DisplayName = "Сила отскока", ClampMin = "0.0"))
	float IndirectLightingIntensity = 1.f;

	// Рассеяние в объёмном тумане (volumetric_scattering_intensity) — виден только на High/Epic (r.VolumetricFog по уровню)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Туман", meta = (DisplayName = "Свечение в тумане", ClampMin = "0.0"))
	float VolumetricScatteringIntensity = 1.f;

	// Объёмная тень (cast_volumetric_shadow) — отдельный проход на свет; у пресетов false
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Туман", meta = (DisplayName = "Объёмная тень"))
	bool bCastVolumetricShadow = false;

	// DF-тени (use_ray_traced_distance_field_shadows) — у локальных грубые; у пресетов false
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Тени", meta = (DisplayName = "DF-тени"))
	bool bUseRayTracedDistanceFieldShadows = false;

	// IES-профиль (ies), по желанию; яркость остаётся из кд (bUseIESBrightness = false)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Форма", meta = (DisplayName = "IES-профиль"))
	TSoftObjectPtr<UTextureLightProfile> IES;

	// Материал эмиссива для меша актора (emissive.material_base) — только у свечений (CrystalGlow)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Эмиссив", meta = (DisplayName = "Материал эмиссива"))
	TSoftObjectPtr<UMaterialInterface> EmissiveMaterial;

	// Множитель эмиссива (emissive.emissive_multiplier) — скаляр «EmissiveMultiplier» MID меша, если есть
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Эмиссив", meta = (DisplayName = "Множитель эмиссива", ClampMin = "0.0"))
	float EmissiveMultiplier = 0.f;

	// Меш актора помечать bEmissiveLightSource (emissive.mesh_flag_emissive_light_source) — карточка Lumen даже у мелкого меша
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Light|Эмиссив", meta = (DisplayName = "Меш — источник эмиссива"))
	bool bMeshEmissiveLightSource = false;
};

// Настройки рендера AsdaReborn (Project Settings → Asda → Render).
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asda Render"))
class ASDAREBORN_API UAsdaRenderSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAsdaRenderSettings();

	// Категория страницы Project Settings
	virtual FName GetCategoryName() const override { return FName(TEXT("Asda")); }

	// Пресеты локальных светов (из asda_light_presets.json через asda_gen_render_settings_ini.py; правятся и здесь)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Локальные света", meta = (DisplayName = "Пресеты света"))
	TMap<EAsdaLightPreset, FAsdaLightPresetSpec> LightPresets;

	// Опорная освещённость солнца alpen, лк (E_sun = mx·SUN_LUX = 0.61·2.6) — к ней посчитаны IntensityCdAlpen
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Локальные света", meta = (DisplayName = "Солнце alpen, лк", ClampMin = "0.01"))
	float SunLuxAlpen = 1.59f;

	// Дефолт MaxDrawDistance для света владельца с 0 (аудит E02/W03; fix_defaults.max_draw_distance) — ставит бюджет-подсистема
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Локальные света", meta = (DisplayName = "Дефолт MaxDrawDistance", ClampMin = "1.0"))
	float DefaultMaxDrawDistance = 4000.f;

	// Дефолт Fade для света с MaxDrawDistance>0 и fade 0 (fix_defaults.max_distance_fade_range)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Локальные света", meta = (DisplayName = "Дефолт Fade", ClampMin = "0.0"))
	float DefaultMaxDistanceFadeRange = 1000.f;

	// Период перебалансировки теней по дистанции до игрока, с (0 = только при загрузке/смене уровня качества).
	//   Смена CastShadows инвалидирует кэш теней (кадр дороже) — не чаще этого периода и только когда кандидатов > бюджета.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Бюджет теней", meta = (DisplayName = "Период перебалансировки, с", ClampMin = "0.0"))
	float ShadowRebalanceIntervalSec = 2.f;

	// Сдвиг игрока, после которого перебалансировка по дистанции имеет смысл, юн
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Бюджет теней", meta = (DisplayName = "Порог сдвига игрока, юн", ClampMin = "0.0"))
	float ShadowRebalanceMoveThreshold = 300.f;

	// Применять бюджет и в PIE (в редакторских (не игровых) мирах — никогда: там владелец должен видеть авторские тени)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Бюджет теней", meta = (DisplayName = "Бюджет в PIE"))
	bool bApplyBudgetInPIE = true;

	// Бюджет по умолчанию, если у уровня нет секции [AsdaShadow@N] в Scalability-ini (fallback; норма — секция есть)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Бюджет теней", meta = (DisplayName = "Бюджет без секции ini", ClampMin = "0"))
	int32 ShadowBudgetFallback = 8;

	// ★ ВОЛНА Ф6 (10.09.2026): ОТДЕЛЬНАЯ ПОЛОСА БЮДЖЕТА ДЛЯ ТЕНЕЙ ЭФФЕКТ-СВЕТОВ.
	//   Почему отдельная, а не общая очередь: эффектов на плотной карте сотни (castle_of_chess — 437 кандидатов),
	//   и они почти всегда ближе к игроку, чем фонари. В ОДНОЙ очереди «ближе — важнее» искры под ногами
	//   вытеснили бы тени фонарей, ради которых бюджет и заводился, и владелец увидел бы регресс на ровном месте.
	//   Полосы независимы: фонари карты берут своё K, эффекты — своё KFx; подсистема и кэш при этом ОДНИ
	//   (второй бюджет не пишем — закон port-shared-points).
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Бюджет теней", meta = (DisplayName = "Бюджет теней эффектов без секции ini", ClampMin = "0"))
	int32 FxShadowBudgetFallback = 1;

	// ★★★ МЕТКА ЧТЕНИЯ СЕКЦИИ (14.09.2026, реестр настроек D7; как ConfigStamp у UAsdaCullSettings).
	//   Умолчание C++ = 0 — такого значения в ini не бывает (генератор пишет дату). Если после загрузки конфига здесь 0,
	//   секция [/Script/AsdaReborn.AsdaRenderSettings] не прочиталась, и пресеты светов молча работают на умолчаниях C++
	//   (фонарь IntensityRelSun 1.0 вместо 3.0 из данных) — Get() один раз за процесс пишет об этом Warning в лог.
	//   Значение пишет Tools/asda_gen_render_settings_ini.py (CONFIG_STAMP); на поведение метка не влияет.
	UPROPERTY(config, VisibleAnywhere, BlueprintReadOnly, Category = "Бюджет теней", meta = (DisplayName = "Метка ini (0 = секция не прочитана)"))
	int32 ConfigStamp = 0;

	// Пресет по ключу; nullptr, если в ini его нет (лог-предупреждение один раз на ключ)
	const FAsdaLightPresetSpec* FindPreset(EAsdaLightPreset Key) const;

	// Единый доступ (CDO с загруженным config)
	static const UAsdaRenderSettings* Get();
};
