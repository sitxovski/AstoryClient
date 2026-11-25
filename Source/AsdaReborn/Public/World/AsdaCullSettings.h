// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AsdaCullSettings.generated.h"

// ★★★ ОТСЕЧКА ДАЛЬНОСТИ ПРОРИСОВКИ ГЕОМЕТРИИ (08.09.2026, drawdistance_report.md).
//   ЗАЧЕМ. Ревизия review_perf2k.md §5 доказала: `sg.ViewDistanceQuality` и `r.ViewDistanceScale` в порту НЕ ДЕЙСТВУЮТ —
//   ключ доходит (`LastSetBy: Scalability`), но у примитивов мира `MaxDrawDistance = 0`, а движок считает
//   `MaxDrawDistance × r.ViewDistanceScale` (SceneVisibility.cpp:491-494) — масштаб множит ноль. Ни один объект мира
//   никогда не отсекается по дальности. Это единственный рычаг, который бьёт одновременно по draw calls и по
//   собственной работе рисующего потока (frame_bubble_report.md §5: настоящий потолок кадра).
//
//   ЗДЕСЬ — ТОЛЬКО ЧИСЛА (Project Settings → Asda → Cull; config=Game defaultconfig → Config/DefaultGame.ini секция
//   [/Script/AsdaReborn.AsdaCullSettings]). В C++ дефолты продублированы конструктором ТОЛЬКО как страховка «нет ini»
//   (как ShadowBudgetFallback у UAsdaRenderSettings); рабочие значения — в ini.
//   ПОЧЕМУ ОТДЕЛЬНЫЙ КЛАСС, а не поля в UAsdaRenderSettings: секцию [/Script/AsdaReborn.AsdaRenderSettings] ГЕНЕРИРУЕТ
//   Tools/asda_gen_render_settings_ini.py из Tools/asda_light_presets.json и помечает «НЕ ПРАВИТЬ РУКАМИ» —
//   перегенерация стёрла бы ручные ключи (памятка itemdb-header-generated-not-manual).
//
//   ПОЧЕМУ РАНТАЙМ, А НЕ В АССЕТЕ (закон no-runtime-widget-fixes «дефект разметки чинится в ассете»). Здесь не дефект
//   разметки, а НОВАЯ НАСТРОЙКА, которая обязана меняться на лету: по уровню качества (sg.ViewDistanceQuality), ползунком
//   в Ctrl+N и ключом отката. Запечённое в .umap число заморозило бы её (и всё равно стиралось бы при пересборке карты
//   Tools/asda_build_map_level.py). Авторская разметка при этом ГЛАВНЕЕ: компонент с ненулевым MaxDrawDistance/
//   InstanceEndCullDistance из ассета подсистема не трогает (см. UAsdaCullDistanceSubsystem).

// Класс геометрии для отсечки. Разбивка ровно та, по которой считался состав кадра (drawdistance_report.md §1).
UENUM(BlueprintType)
enum class EAsdaCullClass : uint8
{
	Terrain    UMETA(DisplayName = "Террейн и вода"),        // SM_terrain / SM_water: один примитив на всю карту — отсекать НЕЛЬЗЯ
	Sky        UMETA(DisplayName = "Небо и облака"),          // купол, слоистые облака, поле облаков — видны всегда
	Prop       UMETA(DisplayName = "Статичные меши"),         // одиночные UStaticMeshComponent (постройки, реквизит, лут)
	Instanced  UMETA(DisplayName = "ISM/HISM (застройка)"),   // UInstancedStaticMeshComponent карты — пер-инстансная отсечка
	Skeletal   UMETA(DisplayName = "Скелеталы (NPC/мобы)"),   // USkeletalMeshComponent: NPC, мобы, чужие игроки, питомцы
	Effect     UMETA(DisplayName = "Эффекты"),                // Niagara/Cascade — дальние всё равно в пиксель
	Count      UMETA(Hidden)
};

// Правило отсечки одного класса. Дальность считается ОТ РАЗМЕРА объекта — как у ACullDistanceVolume движка:
// мелкий бочонок незачем рисовать за 8 км, а замок нельзя гасить на 2 км. Итог:
//   Dist = Clamp(BoundsRadius × SizeFactor, MinDistance, MaxDistance) × asda.Cull.Scale,
// и дальше движок сам умножает на r.ViewDistanceScale уровня (Config/DefaultScalability.ini [ViewDistanceQuality@N]).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaCullClassSpec
{
	GENERATED_BODY()

	// Отсекать этот класс вообще (false = ставим 0 = «никогда не отсекать», прежнее поведение порта)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Asda|Cull", meta = (DisplayName = "Отсекать"))
	bool bCull = true;

	// Множитель к радиусу габаритов: дальность = радиус × этот множитель (до зажима Min/Max)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Asda|Cull", meta = (DisplayName = "Радиус × ", ClampMin = "0.0"))
	float SizeFactor = 40.f;

	// Нижний зажим, юн — ближе этой дальности не отсекаем никогда (иначе мелочь мигает под ногами)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Asda|Cull", meta = (DisplayName = "Минимум, юн", ClampMin = "0.0"))
	float MinDistance = 6000.f;

	// Верхний зажим, юн — дальше него не рисуем даже самое крупное этого класса. ★ У Prop/Instanced 30 000 (было 20 000
	// «по границе динамических теней»): аргумент «выше границы теней отсечка не даёт ничего» верен только для ТЕНЕЙ,
	// крупные объекты за ней видны в базовом проходе — замер nightvalley 08.09.2026, 25 000 эфф. юн = 1.84 % изменённых
	// пикселей при шуме 0.00 %, 37 500 = 0.06 %. Числа и цена — Config/DefaultGame.ini над Classes.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Asda|Cull", meta = (DisplayName = "Максимум, юн", ClampMin = "0.0"))
	float MaxDistance = 20000.f;

	// Поднимать дальность до «пола видимости» (UAsdaCullSettings::VisibilityFloorMin + привязка к туману). Нужно классам,
	// чьё исчезновение видно на фоне неба (мир: постройки, ISM); скелеталам и эффектам не нужно — они на такой дистанции
	// в пиксель, и их гасит раньше серверная сетка AoI (AsdaNameplateComponent.cpp, радиус 1 сектор = 3000..6000 юн).
	// ИМЯ ПОЛЯ ИСТОРИЧЕСКОЕ (ключ ini bUseFogFloor): читается как «учитывать пол видимости», туман — лишь одно его слагаемое.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Asda|Cull", meta = (DisplayName = "Учитывать пол видимости"))
	bool bUseFogFloor = true;
};

// Настройки отсечки дальности (Project Settings → Asda → Cull).
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Asda Cull"))
class ASDAREBORN_API UAsdaCullSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAsdaCullSettings();

	// Категория страницы Project Settings (рядом с «Asda Render»)
	virtual FName GetCategoryName() const override { return FName(TEXT("Asda")); }

	// Правила по классам геометрии (ключи = EAsdaCullClass). Нет ключа → класс не отсекается.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Правила по классам"))
	TMap<EAsdaCullClass, FAsdaCullClassSpec> Classes;

	// ★★★ ПОЛ ВИДИМОСТИ, юн — ГЛАВНОЕ ЧИСЛО ПОДСИСТЕМЫ. Ниже него дальность классов с bUseFogFloor не опускается никогда,
	//   на какой бы карте мы ни стояли. Итог: Floor = Clamp(max(VisibilityFloorMin, StartDistance тумана × FogSafetyFactor), 0, FogFloorMax).
	//
	//   ★ ОТКУДА 15 000 — ЭТО ПИКСЕЛЬНЫЙ ЗАМЕР, А НЕ ФИЗИКА ТУМАНА (исправление ревизии review_drawdistance.md §1).
	//   Прежний комментарий утверждал «дальше границы тумана объект и так не виден» — ЭТО НЕВЕРНО: непрозрачность
	//   экспоненциального тумана = 1 − e^(−(ln2)²·density/1000·(L − StartDistance)) (review_setA_fix.md §2), и на alpen
	//   на взятых 18 750 юн она всего 62 % при авторской плотности — а с реальной настройкой владельца
	//   (asda.Fog.DensityScale = 0.39, живой дамп 08.09.2026: начало 10 000 юн, плотность 0.0889) и вовсе 31 %:
	//   объект виден на две трети. Число выбрано КАДРАМИ
	//   путём игрока (drawdistance_report.md §4, alpen, 5 камер, сцена заморожена, доля пикселей с |Δ| > 8 против
	//   «отсечка выключена»); эффективный радиус = это число × r.ViewDistanceScale уровня (у владельца ×1.25):
	//       шум повтора  1.26 %  |  31250 юн → 2.38 %  |  18750 юн → 4.80 %  |  15000 юн → 5.79 %  |  7500 юн → 14.60 %
	//   15 000 юн × 1.25 = 18 750 эффективных — рабочая точка владельца: на четырёх камерах из пяти неотличимо от шума,
	//   на пятой 4.8 % против 1.3 %. Ниже 11 250 эффективных «выныривание» видно (6.6 %), на 7 500 — явно (14.6 %).
	//
	//   ★ ЗАЧЕМ ПОЛ ОТДЕЛЬНО ОТ ТУМАНА (ревизия §2). Пол, посчитанный ТОЛЬКО от StartDistance тумана, не защищал 4 карты
	//   с близким туманом: nightvalley (пол 150 юн), Mountain Flamion (2100), inferion (3000), quizstage (3000) — там
	//   правил MinDistance класса 6000 → 7500 эффективных, и ровно на nightvalley замер поймал 2.02 % изменённых пикселей
	//   при шуме 0.01 %. Причина: у этих карт туман НАЧИНАЕТСЯ под ногами, но он тонкий (density = FOGK/(fogEnd−fogStart)),
	//   и 85 % непрозрачности набирает лишь на 13 500…80 000 юн — «близкий туман» не значит «близкая видимость».
	//
	//   ★ ЧЕГО ПОЛ НЕ ЛЕЧИТ (проверено кадрами 08.09.2026, nightvalley, камера 3 «долина с NPC»). Пол поднимает МИНИМУМ,
	//   а там режет МАКСИМУМ класса: с полом 15 000 доля изменённых пикселей осталась 1.84 % при шуме 0.00 % и упала до
	//   0.06 % только после подъёма MaxDistance Prop/Instanced с 20 000 до 30 000 (см. FAsdaCullClassSpec::MaxDistance
	//   и Config/DefaultGame.ini). Ревизия предполагала виновником MinDistance — замер этого не подтвердил;
	//   скелеталы там ни при чём (их отдельный вклад 0.04 %).
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Пол видимости, юн", ClampMin = "0.0"))
	float VisibilityFloorMin = 15000.f;

	// Привязка к туману, множитель к StartDistance тумана карты (уже с учётом asda.Fog.DistanceScale владельца).
	//   ПОДНИМАЕТ пол выше VisibilityFloorMin на картах с ДАЛЬНИМ туманом (flammio 10 000, sunnycoast 11 500,
	//   battlefield/silalis/conquester 8 000 → пол упирается в FogFloorMax 20 000): там туман не прикрывает даже 15 000 юн,
	//   и запас нужен больше. Опустить пол ниже VisibilityFloorMin эта привязка не может (берётся максимум) — ни одна
	//   карта после доводки 08.09.2026 не получает дальность МЕНЬШЕ, чем до неё. 0 = привязку не применять.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Запас за началом тумана ×", ClampMin = "0.0"))
	float FogSafetyFactor = 1.5f;

	// Потолок пола видимости, юн. 20 000 = дальность динамических теней карты; 73 % draw calls кадра — проход теней,
	//   а теневые кастеры отсекаются по дальности ПРИМИТИВА (ShadowSetup.cpp:2293), поэтому выше 20 000 отсечка не даёт ничего.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Потолок пола видимости, юн", ClampMin = "0.0"))
	float FogFloorMax = 20000.f;

	// ДИАГНОСТИКА (в расчёт дальностей НЕ входит): доля непрозрачности тумана, которую asda.Cull.Dump считает «объект скрыт».
	//   Дамп печатает дистанцию, на которой туман карты набирает эту непрозрачность, и непрозрачность на самом полу видимости —
	//   чтобы обоснование «туман прикрывает» больше никогда не принималось на веру (ревизия §1 поймала именно это).
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Диагностика: непрозрачность «скрыт»", ClampMin = "0.0", ClampMax = "0.99"))
	float FogDiagOpacity = 0.85f;

	// Не чаще этого (секунды) выполняется полный проход мира при смене ключей/ползунка. 0 = каждый тик (поведение до доводки).
	//   ★ Ревизия §6: перетаскивание ползунка «Дальность прорисовки» давало полный обход мира КАЖДЫЙ кадр — см. заголовок
	//   UAsdaCullDistanceSubsystem::Tick. Последнее значение ползунка применяется всегда: bDirty не сбрасывается, пока проход не сделан.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Throttle полного прохода, с", ClampMin = "0.0"))
	float ApplyThrottleSec = 0.2f;

	// ★ Миры, где отсечку НЕ применять (имя уровня без пути). Сцена логина — авторская постановка с полётом камеры
	//   и без тумана: там нет проблемы с кадром, а «выныривание» декораций было бы регрессом (проверено 08.09.2026:
	//   в мире Login 699 статичных мешей получали 7500..25000 юн). Экран загрузки — тем же соображением.
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Не применять в мирах"))
	TArray<FName> SkipWorlds;

	// Применять отсечку и в PIE (в редакторских (не игровых) мирах — никогда: владелец должен видеть карту целиком)
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Отсечка в PIE"))
	bool bApplyInPIE = true;

	// ★★★ КАНАРЕЙКА КОНФИГА (ревизия review_drawdistance.md §3 «числа ini совпали с дефолтами C++ до последней цифры»).
	//   Дефолт C++ = 0 — такого значения в ini НЕТ НИКОГДА (там дата правки, 20260908). Если после загрузки конфига здесь 0,
	//   значит секция [/Script/AsdaReborn.AsdaCullSettings] не прочиталась вовсе (файл не тот, секция переименована,
	//   опечатка в имени класса) — ValidateConfig() кричит в лог Error. Отдельные ключи проверяются поимённо там же.
	UPROPERTY(config, VisibleAnywhere, BlueprintReadOnly, Category = "Отсечка дальности", meta = (DisplayName = "Штамп ini (0 = ini не прочитан)"))
	int32 ConfigStamp = 0;

	// Правило класса; nullptr, если в ini его нет (класс не отсекается)
	const FAsdaCullClassSpec* FindClass(EAsdaCullClass Key) const;

	// ★ Сверка «что реально прочиталось из ini» с страховочными дефолтами C++ (один раз за процесс, из первого Apply).
	//   Печатает штамп, число ключей секции и ПОИМЁННО каждый ожидаемый ключ: есть в Config/DefaultGame.ini или работает
	//   страховка C++. Так опечатка в ini перестаёт быть невидимой (числа ini и C++ намеренно одинаковы — см. .cpp).
	static void ValidateConfig();

	// Единый доступ (CDO с загруженным config)
	static const UAsdaCullSettings* Get();
};
