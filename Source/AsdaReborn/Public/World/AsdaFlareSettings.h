// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AsdaFlareSettings.generated.h"

// ★★★ ВОЛНА «СВОЙ БЛИК» (10.09.2026): НАБОРЫ ПРИЗРАКОВ И ДОПУСКИ ПЕРЕКРЫТИЯ.
//   Страница Project Settings → Asda → FX Lens Flare (config = Game, defaultconfig → секция
//   [/Script/AsdaReborn.AsdaFlareSettings] в Config/DefaultGame.ini).
//   ЕДИНСТВЕННЫЙ ИСТОЧНИК ЧИСЕЛ — Tools/asda_flare_sets.json; секцию ini генерирует
//   Tools/asda_gen_flare_ini.py (--check сверяет ini с JSON). В C++ чисел таблицы НЕТ намеренно:
//   тот же приём, что у asda_fx_light.json → UAsdaFxLightSettings (закон port-shared-points).
//
//   ЧТО ТАКОЕ ПРИЗРАК. Настоящий блик объектива — не одно пятно, а ЦЕПОЧКА отражений между линзами.
//   Все они лежат на прямой «источник → центр кадра»: чем дальше источник от центра, тем дальше
//   разъезжается цепочка. Один призрак = одна запись таблицы: где он на этой прямой, какого размера,
//   какой яркости, пятно это или кольцо, насколько расходятся цвета по краю.

// Один призрак цепочки.
USTRUCT()
struct ASDAREBORN_API FAsdaFlareGhost
{
	GENERATED_BODY()

	// Положение на прямой «центр кадра → источник»: 1 = ровно в источнике, 0 = в центре кадра,
	// отрицательное = по другую сторону центра (там лежит большая часть настоящей цепочки)
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Положение по линии"))
	float Offset = 1.f;

	// Радиус призрака как ДОЛЯ ВЫСОТЫ КАДРА (0.05 = двадцатая часть высоты). Доля, а не пиксели:
	// иначе на 2560×1440 владельца блик был бы вдвое мельче, чем на 720p замера
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Размер (доля высоты кадра)", ClampMin = "0.0"))
	float Size = 0.05f;

	// Вес (сила) этого призрака относительно общей силы блика
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Вес", ClampMin = "0.0"))
	float Weight = 1.f;

	// Степень спада от центра призрака к краю: 1 — линейный, 4 — узкое ядро с мягким ореолом
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Степень спада", ClampMin = "0.01"))
	float Falloff = 2.f;

	// 0 — сплошное пятно, 1 — кольцо (радужное колечко цепочки)
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Кольцо (0 пятно … 1 кольцо)", ClampMin = "0.0", ClampMax = "1.0"))
	float Ring = 0.f;

	// Толщина кольца в долях радиуса (действует при Ring > 0)
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Толщина кольца", ClampMin = "0.01", ClampMax = "1.0"))
	float RingWidth = 0.3f;

	// Хроматизм: насколько расходятся радиусы каналов R и B (0 — нет, 0.15 — заметная радужная кайма)
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Хроматизм", ClampMin = "0.0", ClampMax = "0.5"))
	float Chroma = 0.f;

	// Оттенок призрака (умножается на цвет эффекта)
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Оттенок"))
	FLinearColor Tint = FLinearColor::White;

	// Насколько оттенок подмешивается: 0 — призрак ровно цвета эффекта, 1 — цвет эффекта × оттенок
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Доля оттенка", ClampMin = "0.0", ClampMax = "1.0"))
	float TintAmount = 0.f;
};

// Набор призраков = «характер оптики». Эффект выбирает набор по имени (поле «Набор бликов»).
USTRUCT()
struct ASDAREBORN_API FAsdaFlareSet
{
	GENERATED_BODY()

	// Имя набора; пустое имя эффекта = набор `default`
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Имя набора"))
	FName Id;

	// Призраки по порядку (порядок — часть данных: первым идёт ядро в самом источнике)
	UPROPERTY(EditAnywhere, Category = "Asda|Flare", meta = (DisplayName = "Призраки"))
	TArray<FAsdaFlareGhost> Ghosts;
};

// Настройки блика (Project Settings → Asda → FX Lens Flare).
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asda FX Lens Flare"))
class ASDAREBORN_API UAsdaFlareSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Asda")); }

	// Механизм включён. Рантайм-откат — CVar asda.Fx.Flare 0
	UPROPERTY(config, EditAnywhere, Category = "Блики", meta = (DisplayName = "Механизм включён"))
	bool bEnabled = true;

	// Показывать блики в ОКНЕ РЕДАКТОРА без Play (владелец ставит галочку эффекту и сразу видит результат)
	UPROPERTY(config, EditAnywhere, Category = "Блики", meta = (DisplayName = "Показывать в редакторе"))
	bool bShowInEditor = true;

	// Наборы призраков (sets из Tools/asda_flare_sets.json)
	UPROPERTY(config, EditAnywhere, Category = "Блики", meta = (DisplayName = "Наборы призраков"))
	TArray<FAsdaFlareSet> Sets;

	// ★ Предел призраков НА ОДИН БЛИК. Это ШАГ ИНСТАНСИРОВАНИЯ на стороне рендера: инстансов рисуется
	//   ровно «бликов × это число», лишние вырождаются в шейдере. Набор длиннее предела режется.
	UPROPERTY(config, EditAnywhere, Category = "Блики", meta = (DisplayName = "Призраков на блик (предел)", ClampMin = "1", ClampMax = "16"))
	int32 MaxGhostsPerFlare = 8;

	// Бюджет количества бликов, если у уровня нет секции [AsdaFlare@N] в Scalability-ini (норма — секция есть)
	UPROPERTY(config, EditAnywhere, Category = "Бюджет", meta = (DisplayName = "Бюджет без секции ini", ClampMin = "0"))
	int32 BudgetFallback = 4;

	// Дальше этого расстояния блик не рисуется вовсе, юн
	UPROPERTY(config, EditAnywhere, Category = "Бюджет", meta = (DisplayName = "Дальность видимости, юн", ClampMin = "1.0"))
	float MaxDrawDistance = 8000.f;

	// Полоса плавного угасания перед дальностью видимости, юн
	UPROPERTY(config, EditAnywhere, Category = "Бюджет", meta = (DisplayName = "Затухание дальности, юн", ClampMin = "0.0"))
	float FadeRange = 2000.f;

	// ── ПЕРЕКРЫТИЕ ПО ГЛУБИНЕ КАДРА ──────────────────────────────────────────────────────────────────
	// ★ Проб в конусе. 1 = жёсткое «видно/не видно» (блик мигает на краю перекрытия — так делать нельзя),
	//   9 = мягкий переход, 16 = очень мягкий. Цена — столько выборок глубины НА БЛИК за кадр, не на пиксель
	UPROPERTY(config, EditAnywhere, Category = "Перекрытие", meta = (DisplayName = "Проб в конусе", ClampMin = "1", ClampMax = "32"))
	int32 Taps = 9;

	// Радиус конуса проб как доля габаритного радиуса источника
	UPROPERTY(config, EditAnywhere, Category = "Перекрытие", meta = (DisplayName = "Радиус конуса (доля габарита)", ClampMin = "0.0"))
	float ConeFraction = 0.6f;

	// Нижний предел радиуса конуса в пикселях: у далёкого источника угловой размер вырождается в точку
	UPROPERTY(config, EditAnywhere, Category = "Перекрытие", meta = (DisplayName = "Мин. радиус конуса, пикс", ClampMin = "0.0"))
	float MinConePx = 3.f;

	// Относительный допуск глубины: перекрытием считается только то, что ближе источника на эту долю расстояния
	UPROPERTY(config, EditAnywhere, Category = "Перекрытие", meta = (DisplayName = "Допуск глубины (доля)", ClampMin = "0.001", ClampMax = "0.5"))
	float DepthBiasRel = 0.03f;

	// Постоянная времени сглаживания видимости, с (0 = без сглаживания; блик тогда щёлкает за один кадр)
	UPROPERTY(config, EditAnywhere, Category = "Перекрытие", meta = (DisplayName = "Сглаживание видимости, с", ClampMin = "0.0"))
	float SmoothTimeSec = 0.12f;

	// ── ЗАТУХАНИЕ К КРАЯМ КАДРА ──────────────────────────────────────────────────────────────────────
	// С какой доли радиуса кадра блик начинает гаснуть (0 = гаснет от самого центра, 1 = только у краёв)
	UPROPERTY(config, EditAnywhere, Category = "Края кадра", meta = (DisplayName = "Начало затухания", ClampMin = "0.0", ClampMax = "1.4"))
	float EdgeFadeStart = 0.35f;

	// Степень затухания к краям
	UPROPERTY(config, EditAnywhere, Category = "Края кадра", meta = (DisplayName = "Степень затухания", ClampMin = "0.01"))
	float EdgeFadePower = 1.5f;

	// Набор по имени (пустое имя → `default`; нет такого — nullptr)
	const FAsdaFlareSet* FindSet(FName SetId) const;

	// Единый доступ (CDO с загруженным config)
	static const UAsdaFlareSettings* Get();
};
