// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "World/AsdaFxLightSpec.h"
#include "AsdaFxLightSettings.generated.h"

// Лог эффект-светов (настройки, подсистема). ★ волна 3 п.12 (15.09.2026): ОДНА категория — раньше две статические копии с одним именем
// ★ Ревизия 15.09: экспорт из модуля — иначе UE_LOG(LogAsdaFxLight) из плагина (AsdaItemBrowser) не слинкуется
ASDAREBORN_API DECLARE_LOG_CATEGORY_EXTERN(LogAsdaFxLight, Log, All);

// ★★★ ВОЛНА Ф2-ВСЕ (09.09.2026): ДЕФОЛТЫ СВЕТА ПО СЕМЕЙСТВАМ ЭФФЕКТОВ.
//   Страница Project Settings → Asda → FX Light (UDeveloperSettings, config=Game defaultconfig → секция
//   [/Script/AsdaReborn.AsdaFxLightSettings] в Config/DefaultGame.ini). Единственный источник чисел —
//   Tools/asda_fx_light.json: секцию ini ГЕНЕРИРУЕТ Tools/asda_gen_fx_light_ini.py (--check сверяет ini с JSON).
//   В C++ чисел таблицы НЕТ намеренно — тот же приём, что у asda_light_presets.json → UAsdaRenderSettings
//   (закон port-shared-points «один источник»).
//
//   ★ СМЕНА МОДЕЛИ ПРОТИВ ВОЛНЫ Ф2. Раньше таблица была БЕЛЫМ СПИСКОМ: не попал в семейство — света нет.
//   Теперь она задаёт ДЕФОЛТЫ: свет по умолчанию есть у ВСЕХ эффектов (последнее правило таблицы —
//   всеобщее `.`), а семейство лишь говорит, с какими числами. Правило с bEnabled=false (дым, метки
//   прицела) — это дефолт «свет выключен», а не запрет: поле «Свет включён» есть у каждого эффекта,
//   и владелец может включить его поштучно.
//
//   ЗАЧЕМ ВСЁ ЭТО. Материалы эффектов Unlit (MSM_Unlit) — эффект светится сам, но не освещает ни пол, ни перила,
//   ни персонажа: со светом Unlit не взаимодействует вовсе (аудит Reports/effects_audit_2026-09-09.md §3).
//   Единственный способ — завести рядом с эффектом НАСТОЯЩИЙ источник света.
//
//   Читают: Tools/asda_apply_fx_light_defaults.py (простановка дефолтов в ассеты BP_Fx_*),
//           UAsdaFxLightSubsystem (заведение кандидатов, бюджет, отбор) и UAsdaFxLightComponent (сам свет).

// Одно ПРАВИЛО таблицы = запись families[i] из Tools/asda_fx_light.json.
USTRUCT()
struct ASDAREBORN_API FAsdaFxLightRule
{
	GENERATED_BODY()

	// Идентификатор семейства (families[].id) — им правило зовётся в логах, в дампе и в поле «Семейство» эффекта
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Идентификатор"))
	FString Id;

	// Шаблон имени .jeb (families[].jeb), БЕЗ учёта регистра; проверка — поиск (не полное совпадение), поэтому `^` якорит.
	// Последнее правило таблицы — всеобщее `.`: под него попадает любое непустое имя (fallback-семейство `other`)
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Шаблон имени .jeb"))
	FString Jeb;

	// ДЕФОЛТНЫЕ числа света этого семейства. Ровно та же структура, что лежит на самом эффекте
	// (AAsdaEffectActor::FxLight): генератор дефолтов копирует её в ассет один в один, а рантайм берёт
	// её же для эффектов, которым дефолты ещё не проставлены
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Дефолты семейства"))
	FAsdaFxLightSpec Spec;
};

// ★★★ ВОЛНА Ф10 (11.09.2026): МАСШТАБ ДЛИНЫ МИРА ДЛЯ ЭФФЕКТ-СВЕТОВ.
//   ПОВОД — сцена логина. Она собрана в масштабе ×100 (ось (x,−z,y)×100: камера в 68 000…87 000 юн от
//   объектов, кристалл слота 26 000…28 000 юн в поперечнике), а числа семейств откалиброваны под
//   обычные карты (радиус 120…450 юн, 5.12 кд). Замер 10.09: на логине свет ГОРЕЛ (3 из 4), но радиус
//   был срезан потолком семейства до 450 при габарите ~28 000, а 5.12 кд на дистанциях ×100 дают
//   освещённость в 10⁴ раз меньше — кристалл не освещал ничего, кроме себя самого.
//   ЧТО ДЕЛАЕТ. Для мира из списка все ДЛИНЫ дефолтов семейства (радиусы, дальности, смещение,
//   размеры источника) умножаются на K, а сила — на K²: освещённость E = I/d², и при d → K·d тот же
//   E даёт только I → K²·I. Уровневый потолок радиуса [AsdaFxLight@N] тоже умножается на K. Экранный
//   бюджет (R/d)² от масштаба не зависит — он остаётся честным ограничителем и здесь.
//   ★ Ручные числа (замок «Переопределено вручную», поэмиттерное «Задано вручную») НЕ масштабируются:
//     владелец правит их в панели деталей в юнитах ЭТОГО мира — зеркало панели на логине показывает
//     уже умноженные дефолты, и правка начинается ровно с них.
USTRUCT()
struct ASDAREBORN_API FAsdaFxLightWorldScale
{
	GENERATED_BODY()

	// Имя мира (UWorld::GetName, без PIE-префикса) — «Login»
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Мир"))
	FName World;

	// Во сколько раз длины этого мира больше обычной карты (логин — 100)
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Масштаб длины, x", ClampMin = "0.001"))
	float LengthScale = 1.f;
};

// ★★★ 13.09.2026 — КОНФИГУРАТОР СВЕЧЕНИЯ: ПЕРЕОПРЕДЕЛЕНИЕ ДЛЯ КОНКРЕТНОГО ЭФФЕКТА ИЛИ ПРЕДМЕТА.
//   ПОВОД — просьба владельца: «у разного оружия разные эффекты — у каждого предмета я должен иметь
//   возможность менять параметры свечения». До этого уровней было три: семейство (таблица), ассет
//   эффекта (замок bManual) и эмиттер. Свечение оружия и блёстки дропа спавнятся В РАНТАЙМЕ, в редакторе
//   их не выделить, а один ассет BP_Fx_<класс>_<буква>_<редкость>_ma_<ступень> общий у многих предметов —
//   поэтому ни ассет, ни семейство не дают настроить «вот этот меч».
//   ЧТО ДАЁТ. Запись таблицы Overrides: точное имя .jeb + код предмета (0 = для всех предметов с этим
//   эффектом) + полный набор чисел. Порядок разрешения (UAsdaFxLightComponent::ResolveSpec,
//   AAsdaEffectActor::ResolveEmissiveOverride):
//       эмиттер (ассет) → ПРЕДМЕТ+эффект → ЭФФЕКТ (все предметы) → ассет (bManual) → семейство → поля ассета.
//   ИСТОЧНИК ЧИСЕЛ — тот же конвейер, что у семейств: Tools/asda_fx_light.json (раздел overrides; в нём
//   хранятся ТОЛЬКО изменённые ключи) → Tools/asda_gen_fx_light_ini.py сводит «defaults ← семейство ←
//   эффект ← предмет» и пишет строки +Overrides=(…) в Config/DefaultGame.ini. Правит JSON конфигуратор
//   (Tools → Asda → «Конфигуратор свечения Asda») через Tools/asda_fx_glow_config.py.
//   Откат всего уровня (и силы свечения семейств) — консоль asda.Fx.Glow.Overrides 0.
USTRUCT()
struct ASDAREBORN_API FAsdaFxGlowOverride
{
	GENERATED_BODY()

	// Точное имя .jeb эффекта (без учёта регистра), например ohs_a_co_ma_03 или eff_0_item_99_drop01-a_00
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Имя .jeb"))
	FString Jeb;

	// Код предмета item_list, ради которого заспавнен эффект (AAsdaEffectActor::SourceItemId); 0 = все предметы с этим эффектом
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Код предмета (0 = все)"))
	int32 ItemId = 0;

	// true = числа СВЕТА берутся из Spec; false = переопределена только сила свечения (эмиссия), а свет идёт дальше по цепочке
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Переопределяет свет"))
	bool bLight = false;

	// Сведённые генератором числа (defaults ← семейство ← эффект ← предмет). Эмиссия (bOverrideEmissive) взведена,
	// только если её задала сама запись или запись эффекта — унаследованная от семейства сюда не попадает
	UPROPERTY(EditAnywhere, Category = "Asda|FxLight", meta = (DisplayName = "Числа"))
	FAsdaFxLightSpec Spec;
};

// Настройки эффект-светов (Project Settings → Asda → FX Light).
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asda FX Light"))
class ASDAREBORN_API UAsdaFxLightSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAsdaFxLightSettings();

	// Категория страницы Project Settings
	virtual FName GetCategoryName() const override { return FName(TEXT("Asda")); }

	// Механизм включён. Рантайм-откат — CVar asda.Fx.Light 0
	UPROPERTY(config, EditAnywhere, Category = "Эффект-света", meta = (DisplayName = "Механизм включён"))
	bool bEnabled = true;

	// Показывать эффект-света в ОКНЕ РЕДАКТОРА без запуска Play. Рантайм-откат — CVar asda.Fx.Light.Editor 0.
	// Без этого крутить яркость руками невозможно: числа правятся в панели деталей, а результат виден
	// только после Play — и обратно к полю уже не вернуться, не потеряв кадр
	UPROPERTY(config, EditAnywhere, Category = "Эффект-света", meta = (DisplayName = "Показывать в редакторе"))
	bool bShowInEditor = true;

	// ★ 27.10.2026 (владелец): ПОКАРТОВЫЙ УРОВЕНЬ ЭФФЕКТ-СВЕТОВ. Карта (имя .umap без пути) → уровень 0..4 (Low/Medium/High/Epic/Cine),
	//   по которому на этой карте берутся [AsdaFxLight@N] (Light.FxBudget/FxScreenBudget/FxRadiusCap/FxPerEffect) — ВМЕСТО
	//   sg.EffectsQuality игрока. Нужно тёмным картам, чей свет — сотни свечей-эффектов (castle_of_chess и др.: 80–220 светов
	//   на карту): на High горели только 32 ближайших, и карта в игре была заметно темнее, чем владелец выстроил в редакторе.
	//   Значения по умолчанию — в конструкторе (секцию ini целиком пишет Tools/asda_gen_fx_light_ini.py).
	UPROPERTY(config, EditAnywhere, Category = "Эффект-света", meta = (DisplayName = "Уровень эффект-светов по картам"))
	TMap<FString, int32> MapEffectsLevel;

	// ★ 27.10.2026 (владелец): УРОВЕНЬ ЭФФЕКТ-СВЕТОВ В РЕДАКТОРЕ (окно уровня и PIE) — чтобы редактор показывал то же, что
	//   игра. По умолчанию 2 (High) — стартовое качество игрока (first-run quality High). −1 — как раньше, по sg.EffectsQuality
	//   редактора (обычно Epic: больше светов, чем увидит игрок). Покартовый уровень выше главнее и в редакторе.
	UPROPERTY(config, EditAnywhere, Category = "Эффект-света", meta = (DisplayName = "Уровень эффект-светов в редакторе (-1 = по настройкам редактора)", ClampMin = "-1", ClampMax = "4"))
	int32 EditorEffectsLevel = 2;

	// Правила «семейство → дефолты света» (families из asda_fx_light.json), ПО ПОРЯДКУ: срабатывает первое подходящее
	UPROPERTY(config, EditAnywhere, Category = "Эффект-света", meta = (DisplayName = "Дефолты семейств"))
	TArray<FAsdaFxLightRule> Families;

	// Предохранитель: больше этого числа кандидатов на карту не заводится (max_candidates)
	UPROPERTY(config, EditAnywhere, Category = "Эффект-света", meta = (DisplayName = "Предел кандидатов", ClampMin = "1"))
	int32 MaxCandidates = 2048;

	// Бюджет КОЛИЧЕСТВА по умолчанию, если у уровня нет секции [AsdaFxLight@N] в Scalability-ini (норма — секция есть)
	UPROPERTY(config, EditAnywhere, Category = "Счётчик", meta = (DisplayName = "Бюджет количества без секции ini", ClampMin = "0"))
	int32 BudgetFallback = 32;

	// ★ ЭКРАННЫЙ БЮДЖЕТ по умолчанию: сумма (радиус/дистанция)² по светящим. Замер Ф2 показал, что цена
	//   бестеневого света определяется не их числом, а ПЛОЩАДЬЮ ЭКРАННОГО СЛЕДА и перекрытием в тайлах
	//   light grid: от 10 до 102 светов проход Lights не рос вовсе, а удвоение радиуса ДВУХ светов стоило
	//   столько же, сколько сотня светов обычного радиуса. Поэтому главный ограничитель — этот, а не счёт
	UPROPERTY(config, EditAnywhere, Category = "Счётчик", meta = (DisplayName = "Экранный бюджет без секции ini", ClampMin = "0.0"))
	float ScreenBudgetFallback = 24.f;

	// Потолок радиуса затухания по умолчанию, юн (уровневый — [AsdaFxLight@N] Light.FxRadiusCap)
	UPROPERTY(config, EditAnywhere, Category = "Счётчик", meta = (DisplayName = "Потолок радиуса без секции ini", ClampMin = "1.0"))
	float RadiusCapFallback = 1000.f;

	// Минимальный срок аренды места в бюджете, с (lease_min_sec) — раньше него место не отбирают
	UPROPERTY(config, EditAnywhere, Category = "Счётчик", meta = (DisplayName = "Мин. срок аренды, с", ClampMin = "0.0"))
	float LeaseMinSec = 0.4f;

	// Во сколько раз претендент должен выигрывать у светящего, чтобы отобрать место (lease_hysteresis)
	UPROPERTY(config, EditAnywhere, Category = "Счётчик", meta = (DisplayName = "Гистерезис отбора", ClampMin = "1.0"))
	float LeaseHysteresis = 1.25f;

	// Период пересчёта отбора, с (select_interval_sec); 0 = каждый кадр
	UPROPERTY(config, EditAnywhere, Category = "Счётчик", meta = (DisplayName = "Период отбора, с", ClampMin = "0.0"))
	float SelectIntervalSec = 0.f;

	// ★ Ф10: миры с нестандартным масштабом длины (world_scales из asda_fx_light.json). Откат — asda.Fx.Light.WorldScale 0
	UPROPERTY(config, EditAnywhere, Category = "Эффект-света", meta = (DisplayName = "Масштаб длины миров"))
	TArray<FAsdaFxLightWorldScale> WorldScales;

	// ★ 13.09.2026: переопределения свечения для конкретного эффекта / предмета (overrides из asda_fx_light.json)
	UPROPERTY(config, EditAnywhere, Category = "Эффект-света", meta = (DisplayName = "Переопределения эффектов и предметов"))
	TArray<FAsdaFxGlowOverride> Overrides;

	// ★★★ МЕТКА ЧТЕНИЯ СЕКЦИИ (14.09.2026, реестр настроек D7; как ConfigStamp у UAsdaCullSettings).
	//   Умолчание C++ = 0 — такого значения в ini не бывает (генератор пишет дату). Если после загрузки конфига здесь 0,
	//   секция [/Script/AsdaReborn.AsdaFxLightSettings] не прочиталась: таблица семейств пуста, бюджеты на умолчаниях C++ —
	//   Get() один раз за процесс пишет об этом Warning в лог. Значение пишет Tools/asda_gen_fx_light_ini.py (CONFIG_STAMP);
	//   на поведение метка не влияет.
	UPROPERTY(config, VisibleAnywhere, Category = "Эффект-света", meta = (DisplayName = "Метка ini (0 = секция не прочитана)"))
	int32 ConfigStamp = 0;

	// Правило для имени .jeb (первое подходящее) или nullptr. Регулярки компилируются один раз (кэш CDO)
	const FAsdaFxLightRule* FindRule(const FString& JebName) const;

	// ★ 13.09.2026: переопределение для пары (эффект, предмет): сначала «этот предмет + эффект», затем «эффект для
	//   всех предметов»; nullptr — нет. Ключ отката asda.Fx.Glow.Overrides НЕ читает (это делает вызывающий)
	const FAsdaFxGlowOverride* FindOverride(const FString& JebName, int32 ItemId) const;

	// ★★★ Волна 3 п.16 (15.09.2026): ЕДИНАЯ ФОРМУЛА поиска переопределения — «этот предмет + эффект» (ItemId ≠ 0), затем
	//   «эффект у всех предметов» (ItemId 0); имя без учёта регистра; при повторе ключа — ПЕРВАЯ запись; пустое имя → nullptr.
	//   Линейная и без кэша: её зовёт конфигуратор свечения, который правит массив на месте без смены ревизии (индекс
	//   FindOverride там устарел бы). FindOverride — индексированная форма ТОЙ ЖЕ формулы для тика рантайма; Python-пара —
	//   find_override в Tools/asda_gen_fx_light_ini.py. Эквивалентность — стенд Reports/dedup_audit/wave3_quality_fxlight.md.
	static const FAsdaFxGlowOverride* FindOverrideIn(const TArray<FAsdaFxGlowOverride>& Arr, const FString& JebName, int32 ItemId);
	// Номер записи с ТОЧНО этой парой (имя без учёта регистра, код предмета) или INDEX_NONE; при повторе — первая.
	//   Без цепочки «предмет → эффект»: нужна правке конкретной записи (конфигуратор) и самой FindOverrideIn
	static int32 IndexOfOverride(const TArray<FAsdaFxGlowOverride>& Arr, const FString& JebName, int32 ItemId);

	// Ревизия таблиц Families/Overrides: растёт при перечитывании из ini и правке в Project Settings.
	// Ключ всех кэшей «индекс правила / переопределения» (на CDO и на акторах эффектов)
	static int32 GetTablesRevision();
	static void BumpTablesRevision();

#if WITH_EDITOR
	// Перечитать Families и Overrides из Config/DefaultGame.ini (после записи генератором из конфигуратора).
	//   Разбор строк «+Families=(…)» / «+Overrides=(…)» тем же импортом текста структуры, что делает LoadConfig;
	//   GConfig НЕ трогается (иначе редактор записал бы копию таблиц в Saved/Config и она перебила бы DefaultGame.ini).
	//   false + OutError — секция не найдена или строка не разобралась (таблицы тогда не меняются вовсе)
	bool ReloadGlowTablesFromDefaultIni(FString& OutError);

	// Правка таблиц в Project Settings → сбросить кэши индексов
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

	// ★ Ф10: масштаб длины мира по ИМЕНИ (без PIE-префикса) из списка WorldScales; 1, если мира в списке нет.
	//   Ключ отката не читает — это делает UAsdaFxLightSubsystem::WorldLengthScale (единая точка для всех
	//   потребителей, она же кэширует имя мира: иначе строка собиралась бы на каждого кандидата каждый кадр)
	float FindWorldLengthScale(FName WorldName) const;

	// Единый доступ (CDO с загруженным config)
	static const UAsdaFxLightSettings* Get();

private:
	// Кэш скомпилированных шаблонов Families (индекс = индекс правила). FRegexPattern не копируется в config
	mutable TArray<TSharedPtr<class FRegexPattern>> Compiled;
	// Число правил, под которое собран кэш (правки в редакторе меняют массив — пересобрать)
	mutable int32 CompiledFor = -1;
	// ★ 13.09.2026: ревизия таблиц, под которую собран кэш шаблонов (перечитывание ini меняет шаблоны при том же числе правил)
	mutable int32 CompiledRev = -1;

	// Индекс переопределений «jeb в нижнем регистре|код предмета» → номер в Overrides и ревизия, под которую он собран
	mutable TMap<FString, int32> OverrideIndex;
	mutable int32 OverrideIndexRev = -1;
	mutable int32 OverrideIndexNum = -1;
};

// ★★★ Волна 3 п.16 (15.09.2026): ИМЯ .jeb СВЕЧЕНИЯ ЗАТОЧКИ ОРУЖИЯ/ЩИТА — ОДНА ФОРМУЛА И ОДНА ТАБЛИЦА БАЗ НА C++.
//   Эталон CCharacter_BuildWeaponJebFileName@0xA4CB10 / itemBuildJebEffectName@0xA4E790 (Reports/weapon_glow_re.md):
//   «<база>_<буква imgSubType>_<грейд>_ma_<ступень>». Таблица баз сверяется с Python: Tools/asda_gen_fx_light_ini.py --check
//   (WEAPON_GLOW_BASE_BY_SUBTYPE и регэксп семейства weapon_glow в asda_fx_light.json / asda_fx_boost.json).
//   Зовут рантайм UAsdaWorldSubsystem::BuildGlowJebName и браузер предметов FAsdaItemBrowserData::BuildGlowJebName
//   (★ хвосты волны 3, 15.09.2026: копия плагина удалена). Таблица грейдов switch в ComposeJeb сверяется тем же --check
//   (WEAPON_GLOW_GRADE_CODE) — формат «case N: Grade = TEXT("xx"); break;» не менять.
namespace AsdaWeaponGlow
{
	// Порог заточки, с которого есть свечение (эталон gradeThresh5_2 = 5)
	constexpr int32 MinEnhance = 5;
	// База имени по itemSubType оружия; nullptr — у подтипа свечения нет (эталон: буфер пуст)
	ASDAREBORN_API const TCHAR* BaseByWeaponSubType(int32 SubType);
	// База имени щита — фиксированная, от типа оружия не зависит
	ASDAREBORN_API const TCHAR* ShieldBase();
	// Спец-предметы эталона (ветка ДО всех таблиц): 41956/56048 — свечение «test-000» независимо от записей
	ASDAREBORN_API bool IsTestGlowItem(int32 ItemId);
	// Имя .jeb спец-предметов
	ASDAREBORN_API const TCHAR* TestGlowJeb();
	// Имя по полям записей предмета; пусто — свечения нет (заточка ниже порога, подтип без базы, imgSubType вне 1..12, грейд вне 0..4)
	ASDAREBORN_API FString ComposeJeb(bool bShield, int32 WeaponSubType, int32 ImgSubType, int32 ItemGrade, int32 Enhance);
}
