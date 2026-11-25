// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "World/AsdaRenderQualitySubsystemBase.h"   // ★ волна 3 п.12 (15.09.2026): общий каркас подсистем качества рендера
#include "World/AsdaCullSettings.h"
#include "AsdaCullDistanceSubsystem.generated.h"

class UPrimitiveComponent;
class UInstancedStaticMeshComponent;
class AActor;

// ★★★ ОТСЕЧКА ДАЛЬНОСТИ ПРОРИСОВКИ ГЕОМЕТРИИ МИРА (08.09.2026, drawdistance_report.md).
//   ЗАДАЧА. Сделать так, чтобы `sg.ViewDistanceQuality` и ползунок в меню РЕАЛЬНО работали. Сейчас не работают:
//   ревизия review_perf2k.md §5 — у всех примитивов мира `MaxDrawDistance = 0` (никогда не отсекать), а движок считает
//   `MaxDrawDistance × r.ViewDistanceScale` (SceneVisibility.cpp:491-494) и `EndCullDistance × r.ViewDistanceScale`
//   для инстансов (InstancedStaticMesh.cpp:1317,1376,2046) — множитель уровня умножает НОЛЬ.
//
//   ЧТО ДЕЛАЕТ. На старте мира (и при появлении акторов) обходит примитивы, относит каждый к классу EAsdaCullClass
//   и ставит дальность: обычным компонентам — `SetCullDistance` (LDMaxDrawDistance), ISM/HISM —
//   `SetCullDistances(Start, End)` (пер-инстансная отсечка: у ISM карты габариты во всю карту, отсечь компонент целиком
//   нельзя, а инстансы отсекаются каждый сам). Числа — из UAsdaCullSettings (Config/DefaultGame.ini), масштаб уровня —
//   `r.ViewDistanceScale` из Config/DefaultScalability.ini [ViewDistanceQuality@N] (движок множит сам, второй раз мы не множим).
//
//   ЧЕГО НЕ ТРОГАЕТ (принципиально):
//     * террейн, воду, купол неба, облака — один примитив на всю карту, отсечка = «мир исчез»;
//     * локальные света — у них своя система (UAsdaLightBudgetSubsystem + r.LightMaxDrawDistanceScale);
//     * плашки и значимость NPC — это asda.Vis.* (UAsdaVisibilitySubsystem), другая подсистема и другая задача;
//     * эффекты AAsdaEffectActor — у них свой CullDistance (AsdaEffectActor.cpp:729), дублировать незачем
//       (в ini класс Effect по умолчанию выключен);
//     * компоненты с АВТОРСКОЙ дальностью из ассета (MaxDrawDistance/InstanceEndCullDistance ≠ 0) — разметка главнее;
//     * актора с тегом `AsdaNoCull` и павна локального игрока.
//
//   КЛЮЧИ. `asda.Cull.Enable` 1/0 — общий рубильник и ОТКАТ (0 возвращает авторские значения, поведение до правки);
//   `asda.Cull.Scale` — общий множитель радиусов (ползунок Ctrl+N «Дальность прорисовки», персист [AsdaRender]);
//   `asda.Cull.Dump` — таблица по классам в лог; `asda.Cull.Set <класс> <SizeFactor> <Min> <Max>` — правка правила
//   класса на лету (замерные развёртки без пересборки); `asda.Cull.VisFloor` — пол видимости в юнитах (−1 = из конфига);
//   `asda.Cull.FogFloor` — множитель привязки пола к началу тумана (−1 = из конфига);
//   `asda.Cull.DumpAfterSec` — отложенный дамп после загрузки мира
//   (офлайн-стенд: -ExecCmds выполняется ДО построения мира, немедленный дамп показал бы пустую сцену).
//
//   ★★★ ЦЕНА ПРИМЕНЕНИЯ (доводка 08.09.2026 по ревизии review_drawdistance.md §6). Полный проход мира стоит дорого,
//   а ползунок «Дальность прорисовки» дёргает ключ КАЖДЫЙ КАДР перетаскивания. Поэтому здесь два ограничителя:
//     1) throttle: полный проход не чаще UAsdaCullSettings::ApplyThrottleSec (0.2 с), последнее значение применяется
//        всегда (флаг bDirty живёт до фактического прохода);
//     2) дальность ставится НАПРЯМУЮ (LDMaxDrawDistance + SetCachedMaxDrawDistance), минуя UPrimitiveComponent::SetCullDistance,
//        которая при УВЕЛИЧЕНИИ дальности звала UWorld::UpdateCullDistanceVolumes с TActorIterator по всему миру
//        (PrimitiveComponent.cpp:2473-2480, World.cpp:3068-3073) — то есть полный обход акторов НА КАЖДЫЙ из 3238 компонентов.
//   Проверяется режимом `asda.DevMenu.SelfTest 5 culldrag`: он ТЯНЕТ ползунок, а не только рисует его.
UCLASS()
class ASDAREBORN_API UAsdaCullDistanceSubsystem : public UAsdaRenderQualitySubsystemBase
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	// ★ волна 3 п.12 (15.09.2026): Initialize/Deinitialize (подписка на уровень качества и на появление актора) — в базе
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// Tick: отложенное применение (bDirty), доработка появившихся акторов, отложенный дамп
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCullDistanceSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return false; }

	// Пересчитать и переприменить дальности (карта загрузилась / сменился ключ / уровень качества). Проход выполняется
	// на ближайшем Tick, но не чаще ApplyThrottleSec — флаг снимается только фактическим проходом, поэтому последнее
	// значение ползунка применяется всегда (ревизия §6: перетаскивание давало полный обход мира каждый кадр).
	void MarkDirty() { bDirty = true; }

	// Применить сейчас: полный обход мира, классификация, простановка дальностей. bLog — сводка в лог.
	// bForce — выполнить даже если настройки не изменились с прошлого прохода (нужен дампу: он пересчитывает счётчики).
	void Apply(bool bLog, bool bForce = false);

	// ★ Сколько полных проходов мира выполнено за процесс. Самопроверка ползунка (asda.DevMenu.SelfTest culldrag) читает
	//   счётчик до и после перетаскивания: рост должен быть ≈ длительность/ApplyThrottleSec, а не «по проходу на кадр».
	static int32 GetApplyPassCount();
	// ★ Значение asda.Cull.Scale, с которым выполнен последний проход — доказательство, что отпущенный ползунок ДОЕХАЛ до мира
	float LastAppliedScale = -1.f;

	// Таблица по классам в лог (asda.Cull.Dump): сколько примитивов, какие дальности, сколько пропущено и почему
	void Dump();

	// Все подсистемы игровых миров процесса → MarkDirty (колбэк CVar / смены уровня качества)
	static void MarkAllDirty();

	// Взвести отложенный дамп во всех игровых мирах процесса. Нужен, потому что -ExecCmds офлайн-стенда выполняется
	// НЕ РАНЬШЕ первого тика после загрузки мира — на момент OnWorldBeginPlay ключ asda.Cull.DumpAfterSec ещё 0.
	static void ArmDumpAll(float Seconds);

	// Класс примитива по его типу и владельцу (публично — тем же правилом классифицирует дамп и отчёт)
	static EAsdaCullClass ClassifyPrimitive(const UPrimitiveComponent* C);

	// Итоги последнего Apply — для лога, дампа и самопроверки
	int32 LastTouched = 0;                                     // сколько компонентов получили дальность
	int32 LastSkippedAuthored = 0;                             // пропущено: авторская дальность из ассета
	int32 LastSkippedExcluded = 0;                             // пропущено: исключения (небо, террейн, игрок, тег)
	int32 LastByClass[(int32)EAsdaCullClass::Count] = { 0 };   // сколько компонентов в каждом классе
	// Сколько материальных СЕКЦИЙ у примитивов класса — статическая оценка вклада класса в draw calls
	// (один проход рисует секцию отдельным вызовом; теневые проходы умножают это на число каскадов).
	int32 LastSectionsByClass[(int32)EAsdaCullClass::Count] = { 0 };
	// Сколько ИНСТАНСОВ в ISM класса (у Instanced — реальное число объектов на карте)
	int32 LastInstancesByClass[(int32)EAsdaCullClass::Count] = { 0 };

private:
	// Авторские значения ОДНОГО компонента — чтобы asda.Cull.Enable 0 вернул всё как было
	struct FAuthored
	{
		float MaxDrawDistance = 0.f;   // LDMaxDrawDistance обычного примитива
		int32 InstStart = 0;           // InstanceStartCullDistance у ISM
		int32 InstEnd = 0;             // InstanceEndCullDistance у ISM
		bool  bInstanced = false;      // компонент — ISM/HISM (какие поля восстанавливать)
	};
	// Авторские значения по компоненту (заполняется при первом касании)
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FAuthored> Authored;

	// Отложенная доработка актора, появившегося после загрузки (NPC/моб/лут: компоненты создаются не в конструкторе).
	// Один актор обрабатывается ДВАЖДЫ — сразу и ещё раз позже (сборка персонажа доцепляет меши).
	struct FPending
	{
		TWeakObjectPtr<AActor> Actor;   // кого доработать
		float TimeLeft = 0.f;           // через сколько секунд
		int32 PassesLeft = 0;           // сколько проходов ещё осталось
	};
	TArray<FPending> Pending;

	bool bDirty = false;                 // применить на ближайшем Tick (снимается только фактическим проходом)
	bool bCountingPass = false;          // идёт ПОЛНЫЙ проход: только он копит LastByClass/LastSections/LastInstances
	                                     // (иначе доработка Pending считала одного актора дважды — косметика ревизии §5)
	double LastApplyTimeSec = -1.0;      // время последнего полного прохода (FPlatformTime::Seconds); < 0 — прохода не было
	uint32 LastApplySignature = 0;       // отпечаток настроек последнего прохода: одинаковый = ставить нечего (двойной Apply на загрузке)
	float DumpTimer = -1.f;              // отложенный дамп (asda.Cull.DumpAfterSec); < 0 — не запланирован

	// Актор появился в мире — доработать его компоненты отложенно (двумя проходами)
	virtual void OnActorSpawned(AActor* A) override;
	// Смена уровня качества: сам множитель r.ViewDistanceScale движок применит без нас, но лог и дамп показывают итог
	// с уровнем — переприменяем и печатаем новую сводку (★ волна 3 п.12 (15.09.2026): подписка — в базе)
	virtual void OnQualityLevelsChanged() override { MarkDirty(); }
	// Обработать один актор: пройти его примитивы и проставить дальность
	void ApplyToActor(AActor* A, int32& OutTouched, int32& OutAuthored, int32& OutExcluded);
	// Обработать один примитив. Возвращает true, если дальность поставлена (или снята при откате)
	bool ApplyToComponent(UPrimitiveComponent* C, int32& OutAuthored, int32& OutExcluded);
	// Итоговая дальность класса для примитива с данным радиусом габаритов, юн (0 = не отсекать)
	float DistanceFor(EAsdaCullClass Cls, float BoundsRadius) const;
	// ★ Пол видимости мира, юн: max(VisibilityFloorMin, начало тумана × FogSafetyFactor), зажатый FogFloorMax (0 = пола нет).
	//   Число 15 000 — из пиксельного замера, НЕ из физики тумана (см. UAsdaCullSettings::VisibilityFloorMin).
	float VisibilityFloor() const;
	// Параметры самого дальнего тумана мира: начало (юн), плотность (поле FogDensity), потолок непрозрачности.
	// false = тумана в мире нет. Начало и плотность уже несут asda.Fog.DistanceScale владельца (их ставит ApplyFogScale).
	bool GetWorldFog(float& OutStart, float& OutDensity, float& OutMaxOpacity) const;
	// ДИАГНОСТИКА: непрозрачность тумана на дистанции L, 0..1 (0 = тумана нет). Формула шейдера:
	// 1 − e^(−(ln2)²·density/1000·(L − Start)), зажатая FogMaxOpacity (HeightFogCommon.ush:207-213,313,394; review_setA_fix.md §2).
	float FogOpacityAt(float L) const;
	// ДИАГНОСТИКА: дистанция, на которой туман набирает непрозрачность Target, юн (0 = недостижимо: тумана нет или Target ≥ потолка)
	float FogDistanceForOpacity(float Target) const;
	// Примитив исключён из отсечки (небо/террейн/игрок/тег/не тот тип)
	bool IsExcluded(const UPrimitiveComponent* C) const;
	// ★ Поставить дальность примитива НАПРЯМУЮ, минуя UPrimitiveComponent::SetCullDistance. Причина — в шапке класса:
	//   штатный путь при увеличении дальности зовёт UWorld::UpdateCullDistanceVolumes (полный TActorIterator) на каждый
	//   компонент. Волюмов ACullDistanceVolume в проекте нет (карты строит Tools/asda_build_map_level.py, grep по
	//   Source/Content 08.09.2026 = 0), поэтому их пересчёт нам не нужен ни при каком значении.
	static void SetMaxDrawDistanceDirect(UPrimitiveComponent* C, float NewDistance);
	// Вернуть компоненту авторские значения (откат asda.Cull.Enable 0)
	static void RestoreAuthored(UPrimitiveComponent* C, const FAuthored& A);
};
