// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/AsdaVisState.h"   // ★ R3: EAsdaVisLevel, FAsdaVisState (вынесено отсюда)
#include "AsdaVisibilitySubsystem.generated.h"

class APawn;   // кадровый кэш локального павна (см. GetLocalPawn)

// ★ ВОЛНА ОПТИМИЗАЦИИ ПРОЦЕССОРА (07.09.2026, cpu_visibility_report.md).
//   Приказ владельца: «если npc или моба не видно в кадре — не рендерить его эффекты, плашки… но делать умно,
//   чтобы при резком повороте не было видно их появления, т.е. должна быть зона рендера чуть больше чем игроку видно».
//
//   ЕДИНАЯ ТОЧКА ЗНАЧИМОСТИ. Вход — мировая позиция и радиус объекта, выход — уровень 0/1/2:
//     0 (Off)  — вне расширенного конуса камеры и дальше ближнего радиуса: потребитель замирает;
//     1 (Far)  — в кадре (с запасом), но далеко: потребитель работает с половинной частотой;
//     2 (Near) — вплотную к камере (< asda.Vis.NearRadius) ЛИБО в кадре и близко: полная частота.
//
//   ПОЧЕМУ НЕ USignificanceManager (закон check-existing-before-creating; движок 5.8,
//   Engine/Plugins/Runtime/SignificanceManager). Он проверен и НЕ подходит как есть:
//     * `USignificanceManager::Update` (SignificanceManager.cpp:478-523) каждый кадр гоняет ParallelFor по ВСЕМ
//       зарегистрированным объектам и вдобавок StableSort по каждому тегу — это ровно та пер-кадровая стоимость,
//       которую волна убирает (у нас ~430 плашек + эффекты);
//     * ни расширенного конуса, ни гистерезиса, ни разноса по кадрам, ни бакетов→TickInterval внутри НЕТ —
//       всё это пишется в пользовательской SignificanceFunction, т.е. 90 % логики всё равно наша;
//     * плагин пришлось бы включать в .uproject и тянуть модуль в Build.cs ради реестра объектов.
//   Берём его ИДЕЮ (значимость по расстоянию/экранному размеру → бакеты → частота работы; так устроен Fortnite),
//   реализуем «pull»-моделью без реестра: состояние живёт у потребителя (FAsdaVisState), подсистема считает
//   камеру ОДИН раз за кадр, а объект — раз в asda.Vis.UpdateHz со своим слотом-фазой.

// ★ R3 (11.11.2026): `EAsdaVisLevel`, `FAsdaVisState` вынесено дословно в `World/AsdaVisState.h` (включён в шапке этого файла).

// ★★★ КАДРОВЫЙ КЭШ ПРОЕКЦИИ МИР→ЭКРАН (правка 07.09.2026, cpu_nameplate_report.md).
//   ЗАЧЕМ. `APlayerController::ProjectWorldLocationToScreen` (PlayerController.cpp:2203) на КАЖДЫЙ вызов
//   делает `ULocalPlayer::GetProjectionData` — полную сборку данных вида (камера, FOV, матрицы вида и
//   проекции, прямоугольник вьюпорта) — и следом `ComputeViewProjectionMatrix()` (умножение матриц 4×4).
//   Для одного объекта это копейки, но плашек в людном городе ~430, и каждая звала это КАЖДЫЙ кадр:
//   430 полных сборок вида на кадр. Сами матрицы за кадр не меняются, поэтому считаем их ОДИН РАЗ
//   в Tick подсистемы, а плашка делает только само умножение точки на матрицу
//   (`FSceneView::ProjectWorldToScreen`) — ровно то, что делает движок ПОСЛЕ GetProjectionData.
//   Сюда же сложены две другие пер-плашечные величины: размер вьюпорта (для фрустум-отсечки с запасом)
//   и коэффициент DPI. Последний особенно важен: `UWidgetLayoutLibrary::GetViewportScale`
//   (WidgetLayoutLibrary.cpp:87) кэширует значение на кадр только в НЕредакторной сборке — под
//   `#if WITH_EDITOR` кэш отключён (`|| WITH_EDITOR`), и в редакторных прогонах он считается заново
//   на каждую плашку (поиск мира → размер вьюпорта → кривая DPI из UUserInterfaceSettings).
struct FAsdaFrameProjection
{
	FMatrix   ViewProj = FMatrix::Identity;         // матрица вид×проекция этого кадра
	FIntRect  ViewRect = FIntRect(0, 0, 0, 0);      // прямоугольник вида (для сплит-скрина Min ≠ 0)
	FVector2D ViewportSize = FVector2D::ZeroVector; // размер вьюпорта в пикселях (фрустум-отсечка с запасом)
	float     DpiScale = 1.f;                       // коэффициент DPI UMG (тот же, что GetViewportScale)
	bool      bValid = false;                       // данные вида собраны в этом кадре
};

UCLASS()
class ASDAREBORN_API UAsdaVisibilitySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Только игровые миры: в редакторе значимость не считается (Get вернёт nullptr → потребители работают как раньше).
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Раз за кадр: камера, её угловая скорость, пороги конуса, счётчик кадров. Вся стоимость подсистемы — здесь и в Evaluate.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaVisibilitySubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return false; }

	// Подсистема мира объекта (nullptr в редакторе/без мира) — единственный способ добраться до значимости.
	static UAsdaVisibilitySubsystem* Get(const UObject* WorldContext);

	// ★ ГЛАВНАЯ ТОЧКА. Уровень значимости объекта в точке `Loc` c габаритным радиусом `Radius` (юниты).
	//   Пересчитывает не чаще раза в период (asda.Vis.UpdateHz) и только на «своём» кадре объекта (Phase),
	//   на остальных возвращает закэшированный уровень — стоимость кадра-пропуска = два сравнения.
	//   При выключенном asda.Vis.Enable и при отсутствии камеры всегда возвращает 2 (полная работа, как до волны).
	int32 Evaluate(FAsdaVisState& State, const FVector& Loc, float Radius);

	// Общий рубильник asda.Vis.Enable (строка Ctrl+N «Оптимизация видимости»).
	static bool IsEnabled();
	// Ключи отката по потребителям: asda.Vis.Nameplates / asda.Vis.Effects (облака — asda.Clouds.UpdateEvery).
	static bool NameplatesEnabled();
	static bool EffectsEnabled();
	// ★ Доводка 07.09.2026 (asda.Vis.GateBeforeSuper): выполнять гейт плашки ДО Super::TickComponent (1)
	//   или после (0, порядок волны видимости). Ключ нужен для замера «до/после» в одном прогоне.
	static bool GateBeforeSuper();

	// ★ Локальный павн, найденный ОДИН РАЗ ЗА КАДР в Tick (nullptr до первого тика/без камеры).
	//   Потребитель сравнивает с ним владельца, чтобы не искать PlayerController на каждый объект.
	const APawn* GetLocalPawn() const;

	// ★ Контроллер локального игрока — КАДРОВЫЙ кэш (правка 07.09.2026). До неё каждая из ~430 плашек
	//   звала UGameplayStatics::GetPlayerController в теле UpdateScreenPosition (перебор списка игроков).
	APlayerController* GetLocalPC() const;

	// ★ Данные проекции этого кадра (см. FAsdaFrameProjection). При bValid=false потребитель обязан
	//   упасть на прежний путь (PC->ProjectWorldLocationToScreen), иначе получит нули на кадре загрузки.
	const FAsdaFrameProjection& GetFrameProjection() const { return FrameProj; }

	// Ключ отката быстрого пути плашки: asda.Plate.FastPath (1 = кэш проекции + дедуп слота, 0 = как было).
	static bool PlateFastPath();
	// Ключ отката «одного виджета на плашку»: asda.Plate.SingleWidget (читается в OnRegister компонента).
	static bool PlateSingleWidget();

	// Диагностика asda.Vis.Dump: камера, пороги, угловая скорость, средняя цена подсистемы за кадр.
	void Dump();

	// ★ ДОКАЗАТЕЛЬСТВО «нет появления при резком повороте» (asda.Vis.PopInCheck, по умолчанию вкл).
	//   Событие поп-ина = объект, который РЕАЛЬНО в кадре (угол ≤ полудиагонали настоящего конуса),
	//   но получил значимость 0, т.е. был заморожен, будучи видимым. Это точная проверка, а не сравнение
	//   пикселей: попарный дифф кадров при вращении бесполезен (двигается вся сцена).
	//   Счётчики: всего событий с обнуления и наименьший УГЛОВОЙ ЗАПАС (град) между объектом и границей
	//   настоящего конуса среди замороженных — положительный запас = запас конуса не был исчерпан.
	int32 GetPopInEvents() const { return PopInEvents; }
	float GetWorstSlackDeg() const { return WorstSlackDeg; }
	void ResetPopInCounters() { PopInEvents = 0; WorstSlackDeg = 1.0e9f; PeakAngSpeedDeg = 0.f; }
	float GetPeakAngularSpeed() const { return PeakAngSpeedDeg; }

	// Сколько кадров в периоде обновления (≥1) при текущем asda.Vis.UpdateHz и частоте кадров.
	int32 GetUpdatePeriodFrames() const { return UpdatePeriodFrames; }
	// Текущая угловая скорость камеры, град/с (для отчётов и самопроверки поворота).
	float GetCameraAngularSpeed() const { return CamAngSpeedDeg; }

private:
	// Пересчёт уровня для объекта (без гейтов периода) — вся геометрия конуса и гистерезиса.
	//   OutSlackDeg — запас до границы НАСТОЯЩЕГО конуса камеры (>0 объект за кадром, <0 объект в кадре);
	//   для ближней зоны и «дистанция ноль» отдаётся +∞ (угол не при чём).
	int32 ComputeLevel(const FAsdaVisState& State, const FVector& Loc, float Radius, float& OutSlackDeg) const;

	// ── Кадровый кэш камеры ────────────────────────────────────────────────────────────────────────
	TWeakObjectPtr<const APawn> LocalPawn;     // павн локального игрока на этом кадре (для «своей» плашки)
	TWeakObjectPtr<APlayerController> LocalPC; // контроллер локального игрока на этом кадре (кэш для плашек)
	FAsdaFrameProjection FrameProj;            // матрицы вида/проекции, размер вьюпорта и DPI этого кадра
	FVector CamLoc = FVector::ZeroVector;      // позиция камеры игрока
	FVector CamFwd = FVector::ForwardVector;   // направление взгляда (единичный)
	bool    bCamValid = false;                 // камера найдена в этом кадре
	float   CamAngSpeedDeg = 0.f;              // угловая скорость взгляда, град/с (сглажена)
	float   HalfDiagDeg = 45.f;                // половина ДИАГОНАЛЬНОГО угла обзора (из FOV и соотношения сторон)
	float   LimitOnDeg = 65.f;                 // порог включения: HalfDiag + запас (в т.ч. предсказание по угл. скорости)
	float   LimitOffDeg = 70.f;                // порог выключения: LimitOn + гистерезис (объект гаснет позже, чем зажёгся)
	float   NearRadiusOn = 1500.f;             // ближняя зона «всегда активно» (порог включения)
	float   NearRadiusOff = 1875.f;            // та же зона с гистерезисом (порог выключения)
	float   FarRadiusOn = 0.f;                 // 0 = дальнего предела нет (у потребителей свои дистанц-куллы)
	float   FarRadiusOff = 0.f;
	// Номер кадра подсистемы (свой, а не GFrameCounter: у миров разная история) и слоты разноса.
	uint32  FrameCounter = 0;
	int32   UpdatePeriodFrames = 6;            // период пересчёта в кадрах (округление 1/UpdateHz к текущему кадру)
	uint32  NextPhase = 0;                     // раздатчик слотов новым объектам (круговой)
	// Резкий поворот: на этом кадре пересчитываются ВСЕ объекты (разнос по фазам временно отменён).
	bool    bForceAllThisFrame = false;

	// ── Собственная цена (без stat-групп: они сами стоят 2–3 мс, cpu_gamethread_plan.md §0) ────────
	uint64  EvalCycles = 0;    // такты, накопленные Evaluate+Tick за окно
	int32   EvalCalls = 0;     // вызовов Evaluate за окно
	int32   EvalRecomputes = 0;// из них с реальным пересчётом
	int32   FramesInWindow = 0;// кадров в окне
	double  LastWindowMsPerFrame = 0.0;   // средняя цена подсистемы на кадр, мс (последнее закрытое окно)
	float   WindowTimer = 0.f; // окно усреднения, с

	// ── Счётчики поп-ина (см. GetPopInEvents) ──────────────────────────────────────────────────────
	int32   PopInEvents = 0;            // объектов, замороженных БУДУЧИ в настоящем кадре (должен быть 0)
	float   WorstSlackDeg = 1.0e9f;     // наименьший угловой запас среди замороженных, град
	float   PeakAngSpeedDeg = 0.f;      // пиковая угловая скорость камеры с обнуления счётчиков, град/с
};
