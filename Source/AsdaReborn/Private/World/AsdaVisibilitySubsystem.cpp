// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaVisibilitySubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"   // GetViewportSize — соотношение сторон для диагонального угла
#include "Engine/LocalPlayer.h"           // GetProjectionData — кадровый кэш проекции мир→экран
#include "Engine/UserInterfaceSettings.h" // GetDPIScaleBasedOnSize — тот же DPI, что у UWidgetLayoutLibrary
#include "SceneView.h"                    // FSceneViewProjectionData::ComputeViewProjectionMatrix
#include "UnrealClient.h"                 // FViewport (аргумент GetProjectionData)
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"   // владелец камеры → локальный павн (кадровый кэш)
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "UObject/UObjectIterator.h"
#include "World/AsdaAnimThrottle.h"   // ★ волна производительности 29.10.2026: урезание анимации чужих скелетов
#include "AsdaCloudFieldActor.h"   // ★ М-2: общий рубильник asda.Vis.Enable транслируется на шаг облаков

DEFINE_LOG_CATEGORY_STATIC(LogAsdaVis, Log, All);

// ── Ключи. Все регистрации `asda.Vis.*` — ТОЛЬКО здесь (закон ue-console-duplicate-registration:
//    у команд выживает последняя регистрация, у переменных первая; дубль = молча мёртвый ключ).
//    Проверено до правки: grep "asda.Vis." по Source/ давал 0 совпадений.

// Общий рубильник волны. 0 = всё как до 07.09.2026 (Evaluate всегда возвращает 2). Строка Ctrl+N «Оптимизация видимости».
// ★ М-2 ревизии review_cpu_visibility.md (07.09.2026): ОДНА строка должна выключать волну ЦЕЛИКОМ, включая облака.
//   До правки asda.Vis.Enable гейтил только плашки/эффекты, а облака жили на отдельном ключе asda.Clouds.UpdateEvery —
//   выключение «Оптимизации видимости» не возвращало облакам покадровый шаг. Теперь смена asda.Vis.Enable колбэком
//   транслируется на облака: 0 → шаг облаков 1 (покадрово), 1 → прежняя база asda.Clouds.UpdateEvery.
static int32 GAsdaVisEnable = 1;
static void OnAsdaVisEnableChanged(IConsoleVariable* Var)
{
	// bEnabled=false при 0 → облакам эффективный шаг 1; true → вернуть базу. База (asda.Clouds.UpdateEvery) не трогается.
	AAsdaCloudFieldActor::SetVisMasterEnabled(Var->GetInt() != 0);
}
static FAutoConsoleVariableRef CVarAsdaVisEnable(
	TEXT("asda.Vis.Enable"), GAsdaVisEnable,
	TEXT("1 = единая значимость по видимости (плашки/эффекты/облака работают только рядом или в расширенном кадре), 0 = полная работа как раньше."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaVisEnableChanged),
	ECVF_Default);

// Запас по углу сверх реального конуса камеры, градусов. Это и есть «зона рендера чуть больше, чем видно».
static float GAsdaVisFovMargin = 20.f;
static FAutoConsoleVariableRef CVarAsdaVisFovMargin(
	TEXT("asda.Vis.FovMargin"), GAsdaVisFovMargin,
	TEXT("Запас конуса значимости сверх угла обзора, градусов (по умолчанию 20). К нему добавляется предсказание по угловой скорости камеры."),
	ECVF_Default);

// Ближняя сфера «всегда активно, что бы ни было за спиной», юниты (1500 = 15 м).
static float GAsdaVisNearRadius = 1500.f;
static FAutoConsoleVariableRef CVarAsdaVisNearRadius(
	TEXT("asda.Vis.NearRadius"), GAsdaVisNearRadius,
	TEXT("Радиус ближней зоны (юниты): всё в ней имеет значимость 2 независимо от направления взгляда. По умолчанию 1500."),
	ECVF_Default);

// Дальний предел значимости. 0 = не ограничивать (у потребителей есть свои дистанц-куллы:
// плашки — секторная AoI сервера, эффекты — CullDistance 10000). Оставлен ключом на будущее.
static float GAsdaVisFarRadius = 0.f;
static FAutoConsoleVariableRef CVarAsdaVisFarRadius(
	TEXT("asda.Vis.FarRadius"), GAsdaVisFarRadius,
	TEXT("Дальний предел значимости, юниты (0 = без предела; у плашек своя AoI, у эффектов CullDistance)."),
	ECVF_Default);

// Гистерезис: включение по порогу R, выключение по R×H. Разные пороги = нет «дрожания» на границе.
static float GAsdaVisHysteresis = 1.25f;
static FAutoConsoleVariableRef CVarAsdaVisHysteresis(
	TEXT("asda.Vis.Hysteresis"), GAsdaVisHysteresis,
	TEXT("Множитель порога ВЫКЛЮЧЕНИЯ относительно порога включения (по умолчанию 1.25). Гасит мигание объектов на границе зоны."),
	ECVF_Default);

// Частота пересчёта списка. Объекты разнесены по кадрам периода (staggered ticks).
static float GAsdaVisUpdateHz = 10.f;
static FAutoConsoleVariableRef CVarAsdaVisUpdateHz(
	TEXT("asda.Vis.UpdateHz"), GAsdaVisUpdateHz,
	TEXT("Частота пересчёта значимости, Гц (по умолчанию 10). Объекты разнесены по кадрам периода; при резком повороте период отменяется."),
	ECVF_Default);

// Порог «резкого поворота», град/с: выше него период разноса отменяется — все объекты пересчитываются в том же кадре.
static float GAsdaVisFastTurn = 120.f;
static FAutoConsoleVariableRef CVarAsdaVisFastTurn(
	TEXT("asda.Vis.FastTurnDegPerSec"), GAsdaVisFastTurn,
	TEXT("Угловая скорость камеры (град/с), с которой значимость пересчитывается КАЖДЫЙ кадр для всех объектов (по умолчанию 120)."),
	ECVF_Default);

// Потолок предсказательного запаса, градусов: сколько максимум добавляем к конусу по угловой скорости.
static float GAsdaVisMaxPredict = 90.f;
static FAutoConsoleVariableRef CVarAsdaVisMaxPredict(
	TEXT("asda.Vis.MaxPredictDeg"), GAsdaVisMaxPredict,
	TEXT("Потолок предсказательного расширения конуса по угловой скорости камеры, градусов (по умолчанию 90)."),
	ECVF_Default);

// Ключи отката по потребителям (владелец может отключить любой отдельно).
static int32 GAsdaVisNameplates = 1;
static FAutoConsoleVariableRef CVarAsdaVisNameplates(
	TEXT("asda.Vis.Nameplates"), GAsdaVisNameplates,
	TEXT("1 = плашки имён работают только при значимости >= 1 (вне зоны виджет освобождается), 0 = как раньше."),
	ECVF_Default);

static int32 GAsdaVisEffects = 1;
static FAutoConsoleVariableRef CVarAsdaVisEffects(
	TEXT("asda.Vis.Effects"), GAsdaVisEffects,
	TEXT("1 = симуляция AAsdaEffectActor гаснет вне зоны и идёт через кадр на дальней значимости, 0 = как раньше."),
	ECVF_Default);

// Проверка поп-ина (см. GetPopInEvents в заголовке). Дешёвая (сравнение уже посчитанного угла), поэтому
// включена по умолчанию: если владелец где-то поймает пропажу — в логе сразу будет строка «ПОП-ИН».
static int32 GAsdaVisPopInCheck = 1;
static FAutoConsoleVariableRef CVarAsdaVisPopInCheck(
	TEXT("asda.Vis.PopInCheck"), GAsdaVisPopInCheck,
	TEXT("1 = считать и логировать случаи, когда объект заморожен, находясь в НАСТОЯЩЕМ кадре камеры (должно быть 0 случаев)."),
	ECVF_Default);

// ★★★ ДОВОДКА 07.09.2026 (В3 ревизии review_cpu_insights.md, отчёт cpu_fix2_report.md): МЕСТО ГЕЙТА В ТИКЕ ПЛАШКИ.
//   Ревизия доказала числами (tstat_alpen_L3_GT_stand.csv): диспетчеризация тика стоит 0.6 мс из 7.0, а
//   4.235 мс — СОБСТВЕННОЕ тело UAsdaNameplateComponent::TickComponent, потому что гейт значимости стоял
//   ПОСЛЕ `Super::TickComponent` (полный тик UWidgetComponent: UpdateWidget → TakeWidget/Resize окна Slate).
//   Ключ переключает ПОРЯДОК (а не наличие) гейта, чтобы мерить «до/после» чередующимися срезами в ОДНОМ прогоне:
//     1 = гейт ВЫШЕ Super (правка доводки), 0 = гейт НИЖЕ Super (порядок до доводки, поведение волны видимости).
//   Поведение плашки в обоих положениях одинаковое — отличается только объём работы на невидимых плашках.
static int32 GAsdaVisGateBeforeSuper = 1;
static FAutoConsoleVariableRef CVarAsdaVisGateBeforeSuper(
	TEXT("asda.Vis.GateBeforeSuper"), GAsdaVisGateBeforeSuper,
	TEXT("1 = гейт значимости плашки выполняется ДО Super::TickComponent (дешевле), 0 = после (порядок до 07.09.2026)."),
	ECVF_Default);

// ★★★ БЫСТРЫЙ ПУТЬ ТЕЛА ПЛАШКИ (правка 07.09.2026, cpu_nameplate_report.md).
//   1 = UAsdaNameplateComponent::UpdateScreenPosition берёт КАДРОВЫЙ кэш (контроллер, матрица
//   вид×проекция, размер вьюпорта, DPI) и не переписывает слот виджета в вьюпорте, когда позиция
//   не изменилась; 0 = прежний путь (ProjectWorldLocationToScreen + GetViewportScale на КАЖДУЮ плашку
//   и безусловные SetPositionInViewport/SetDesiredSizeInViewport). Ключ нужен для замера ABBA в одном
//   прогоне и как откат: картинка в обоих положениях обязана быть одинаковой.
static int32 GAsdaPlateFastPath = 1;
static FAutoConsoleVariableRef CVarAsdaPlateFastPath(
	TEXT("asda.Plate.FastPath"), GAsdaPlateFastPath,
	TEXT("1 = плашка использует кадровый кэш проекции и не трогает слот вьюпорта без нужды, 0 = путь до 07.09.2026."),
	ECVF_Default);

// ★★★ ОДИН ВИДЖЕТ НА ПЛАШКУ (правка 07.09.2026). До неё на каждую плашку создавалось ДВА UUserWidget:
//   внутренний виджет UWidgetComponent (нужен только для 3D-превью в РЕДАКТОРЕ; в игре компонент скрыт —
//   OnRegister: SetVisibility(false)+SetHiddenInGame(true)) и наш экранный ScreenWidget, который и рисуется.
//   При 1 в ИГРОВОМ мире внутренний не создаётся вовсе. Ключ читается в OnRegister, поэтому на лету
//   он не переключается — менять до входа в мир (ini/командная строка).
static int32 GAsdaPlateSingleWidget = 1;
static FAutoConsoleVariableRef CVarAsdaPlateSingleWidget(
	TEXT("asda.Plate.SingleWidget"), GAsdaPlateSingleWidget,
	TEXT("1 = в игре на плашку создаётся ОДИН виджет (экранный), 0 = как до 07.09.2026 (плюс внутренний виджет компонента). Читается при регистрации компонента."),
	ECVF_Default);

bool UAsdaVisibilitySubsystem::IsEnabled()          { return GAsdaVisEnable != 0; }
bool UAsdaVisibilitySubsystem::NameplatesEnabled()  { return GAsdaVisEnable != 0 && GAsdaVisNameplates != 0; }
bool UAsdaVisibilitySubsystem::EffectsEnabled()     { return GAsdaVisEnable != 0 && GAsdaVisEffects != 0; }
bool UAsdaVisibilitySubsystem::GateBeforeSuper()    { return GAsdaVisGateBeforeSuper != 0; }
bool UAsdaVisibilitySubsystem::PlateFastPath()      { return GAsdaPlateFastPath != 0; }
bool UAsdaVisibilitySubsystem::PlateSingleWidget()  { return GAsdaPlateSingleWidget != 0; }

APlayerController* UAsdaVisibilitySubsystem::GetLocalPC() const
{
	// Тот же кадровый кэш, что и у LocalPawn: контроллер берётся у менеджера камеры один раз за кадр.
	return LocalPC.Get();
}

const APawn* UAsdaVisibilitySubsystem::GetLocalPawn() const
{
	// Кэш обновляется РАЗ ЗА КАДР в Tick. Потребителей (плашек) сотни, и каждая раньше искала
	//   PlayerController через UGameplayStatics — теперь это один поиск на кадр, а не на плашку.
	return LocalPawn.Get();
}

bool UAsdaVisibilitySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Только игровой мир: в редакторе камера/фрустум другие, а значимость там только мешала бы правкам.
	const UWorld* W = Cast<UWorld>(Outer);
	return W && W->IsGameWorld();
}

void UAsdaVisibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogAsdaVis, Log, TEXT("[видимость] подсистема поднята: Enable=%d FovMargin=%.0f° NearRadius=%.0f Hyst=%.2f UpdateHz=%.0f"),
		GAsdaVisEnable, GAsdaVisFovMargin, GAsdaVisNearRadius, GAsdaVisHysteresis, GAsdaVisUpdateHz);
	// ★ М-2: синхронизировать облака с текущим значением рубильника при старте мира (колбэк срабатывает только на СМЕНУ;
	//   если asda.Vis.Enable=0 пришёл из ini до этого — облака иначе остались бы на базовом шаге).
	AAsdaCloudFieldActor::SetVisMasterEnabled(GAsdaVisEnable != 0);
}

void UAsdaVisibilitySubsystem::Deinitialize()
{
	Super::Deinitialize();
}

UAsdaVisibilitySubsystem* UAsdaVisibilitySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W || !W->IsGameWorld()) { return nullptr; }
	return W->GetSubsystem<UAsdaVisibilitySubsystem>();
}

void UAsdaVisibilitySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const uint64 T0 = FPlatformTime::Cycles64();

	++FrameCounter;
	++FramesInWindow;
	bForceAllThisFrame = false;

	const UWorld* W = GetWorld();
	const APlayerCameraManager* Cam = AsdaView::GetPlayerCameraManager(W);   // ★ волна 3 п.11: побайтно прежний вызов UGameplayStatics::GetPlayerCameraManager(W, 0)
	// ★ Локальный павн — КАДРОВЫЙ кэш (доводка 07.09.2026). Плашка своего персонажа не гейтится никогда
	//   (закон self-nameplate-same-as-others), и до доводки каждая из ~430 плашек искала контроллер сама.
	//   Берём его у менеджера камеры: он и так уже найден здесь, лишнего поиска нет.
	APlayerController* PC = Cam ? Cam->GetOwningPlayerController() : nullptr;
	LocalPawn = PC ? PC->GetPawn() : nullptr;
	LocalPC = PC;

	// ★★★ КАДРОВЫЙ КЭШ ПРОЕКЦИИ (обоснование — у FAsdaFrameProjection в заголовке). Собираем данные вида
	//   ОДИН раз за кадр; плашка потом делает только умножение точки на матрицу. Если собрать не удалось
	//   (нет локального игрока/вьюпорта — загрузка, титры), bValid=false, и потребитель обязан упасть
	//   на прежний путь, а не рисовать по нулям.
	FrameProj.bValid = false;
	if (PC)
	{
		if (const ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (LP->ViewportClient && LP->ViewportClient->Viewport)
			{
				FSceneViewProjectionData PD;
				if (LP->GetProjectionData(LP->ViewportClient->Viewport, PD))
				{
					FrameProj.ViewProj = PD.ComputeViewProjectionMatrix();
					FrameProj.ViewRect = PD.GetConstrainedViewRect();
					FrameProj.bValid = true;
				}
			}
		}
	}
	if (FrameProj.bValid)
	{
		// Размер вьюпорта и DPI — из ТЕХ ЖЕ источников, что у UWidgetLayoutLibrary (иначе плашка сдвинулась бы
		//   на долю пикселя относительно прежнего пути, и при переключении ключа было бы видно смещение).
		FVector2D VpPx = FVector2D::ZeroVector;
		if (UGameViewportClient* GVC = W ? W->GetGameViewport() : nullptr) { GVC->GetViewportSize(VpPx); }
		FrameProj.ViewportSize = VpPx;
		FrameProj.DpiScale = (VpPx.X > 1.f && VpPx.Y > 1.f)
			? GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(
				FIntPoint(FMath::TruncToInt32(VpPx.X), FMath::TruncToInt32(VpPx.Y)))
			: 1.f;
	}

	// ★ Волна производительности 29.10.2026: урезание анимации чужих скелетов (World/AsdaAnimThrottle.h). Разбор
	//   очереди новых акторов и смена ключа asda.Anim.Throttle — каждый кадр, независимо от наличия камеры.
	AsdaAnimThrottle::TickWorld(GetWorld());

	if (!Cam)
	{
		// Камеры нет (загрузка, титры) — значимость не считаем; Evaluate вернёт 2 (полная работа).
		bCamValid = false;
		CamAngSpeedDeg = 0.f;
		EvalCycles += FPlatformTime::Cycles64() - T0;
		return;
	}

	const FVector NewLoc = Cam->GetCameraLocation();
	const FVector NewFwd = Cam->GetCameraRotation().Vector();

	// Угловая скорость взгляда, град/с. Именно она решает, хватит ли запаса конуса при рывке:
	// за задержку периода (1/UpdateHz + кадр) камера успевает повернуться на AngSpeed × Latency градусов.
	if (bCamValid && DeltaTime > KINDA_SMALL_NUMBER)
	{
		const float Dot = FMath::Clamp(FVector::DotProduct(CamFwd, NewFwd), -1.f, 1.f);
		const float StepDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
		const float Inst = StepDeg / DeltaTime;
		// Берём максимум мгновенной и затухающей: скорость должна расти МГНОВЕННО (иначе рывок опоздает),
		// а спадать плавно (иначе запас исчезнет раньше, чем объекты пересчитаются на новых порогах).
		CamAngSpeedDeg = FMath::Max(Inst, CamAngSpeedDeg * FMath::Exp(-DeltaTime / 0.25f));
		PeakAngSpeedDeg = FMath::Max(PeakAngSpeedDeg, Inst);   // пик за окно проверки поворота
	}
	CamLoc = NewLoc;
	CamFwd = NewFwd;
	bCamValid = true;

	// ── Углы конуса ────────────────────────────────────────────────────────────────────────────────
	// FOV менеджера камеры — ГОРИЗОНТАЛЬНЫЙ. Значимость меряем по ДИАГОНАЛИ кадра: объект в углу экрана
	// виден, а по горизонтальному полу-углу он бы уже «выпал» (пропажа плашек по углам).
	const float FovDeg = FMath::Clamp(Cam->GetFOVAngle(), 5.f, 170.f);
	float Aspect = 16.f / 9.f;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D VpSize = FVector2D::ZeroVector;
		GEngine->GameViewport->GetViewportSize(VpSize);
		if (VpSize.X > 1.f && VpSize.Y > 1.f) { Aspect = VpSize.X / VpSize.Y; }
	}
	const float TanH = FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5f));
	const float TanV = TanH / FMath::Max(Aspect, 0.1f);
	HalfDiagDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(TanH * TanH + TanV * TanV)));

	// Период разноса по кадрам: 1/UpdateHz, переведённый в кадры по ТЕКУЩЕМУ DeltaTime (на 60 fps и 10 Гц = 6 кадров).
	const float Hz = FMath::Clamp(GAsdaVisUpdateHz, 0.5f, 240.f);
	const float PeriodSec = 1.f / Hz;
	UpdatePeriodFrames = (DeltaTime > KINDA_SMALL_NUMBER)
		? FMath::Clamp(FMath::RoundToInt(PeriodSec / DeltaTime), 1, 64)
		: 6;

	// ★ ЗАПАС ПРОТИВ ПОЯВЛЕНИЯ ПРИ РЕЗКОМ ПОВОРОТЕ (dilated frustum + предсказание, как в больших играх):
	//   к фиксированному запасу FovMargin добавляем угол, который камера пройдёт за задержку обновления.
	//   Latency = период + один кадр (объект мог быть посчитан на предыдущем кадре своего слота).
	const float LatencySec = PeriodSec + FMath::Max(DeltaTime, 0.f);
	const float PredictDeg = FMath::Min(CamAngSpeedDeg * LatencySec, FMath::Max(GAsdaVisMaxPredict, 0.f));
	const float MarginDeg = FMath::Max(GAsdaVisFovMargin, 0.f) + PredictDeg;
	const float Hyst = FMath::Max(GAsdaVisHysteresis, 1.f);

	LimitOnDeg  = HalfDiagDeg + MarginDeg;
	// Гистерезис по углу: гасим на (Hyst−1)×FovMargin градусов позже, чем зажгли.
	LimitOffDeg = LimitOnDeg + FMath::Max(GAsdaVisFovMargin, 0.f) * (Hyst - 1.f);

	NearRadiusOn  = FMath::Max(GAsdaVisNearRadius, 0.f);
	NearRadiusOff = NearRadiusOn * Hyst;
	FarRadiusOn   = FMath::Max(GAsdaVisFarRadius, 0.f);
	FarRadiusOff  = FarRadiusOn * Hyst;

	// Резкий поворот — разнос по кадрам отменяем: латентность 100 мс на рывке 900 °/с = 90°, никакой запас не спасёт.
	// Пересчёт всех объектов стоит ~сотни наносекунд на объект и случается только на время рывка.
	bForceAllThisFrame = (CamAngSpeedDeg >= FMath::Max(GAsdaVisFastTurn, 1.f));

	EvalCycles += FPlatformTime::Cycles64() - T0;

	// Окно усреднения собственной цены (без stat-групп — они сами стоят 2–3 мс).
	WindowTimer += DeltaTime;
	if (WindowTimer >= 1.f)
	{
		const double TotalMs = FPlatformTime::ToMilliseconds64(EvalCycles);
		LastWindowMsPerFrame = (FramesInWindow > 0) ? (TotalMs / FramesInWindow) : 0.0;
		EvalCycles = 0; EvalCalls = 0; EvalRecomputes = 0; FramesInWindow = 0; WindowTimer = 0.f;
	}
}

int32 UAsdaVisibilitySubsystem::ComputeLevel(const FAsdaVisState& State, const FVector& Loc, float Radius, float& OutSlackDeg) const
{
	OutSlackDeg = 1.0e9f;   // «угол не при чём» (ближняя зона / дальний предел / нулевая дистанция)
	const bool bWasOn = (State.Level > (uint8)EAsdaVisLevel::Off);
	const bool bWasNear = (State.Level >= (uint8)EAsdaVisLevel::Near);

	const FVector D = Loc - CamLoc;
	const float Dist = D.Size();

	// (1) Ближняя зона «что бы ни было за спиной». Гистерезис: выйти из неё нужно дальше, чем войти.
	const float NearR = bWasNear ? NearRadiusOff : NearRadiusOn;
	if (Dist <= NearR) { return (int32)EAsdaVisLevel::Near; }

	// (2) Дальний предел (0 = нет предела; у потребителей свои дистанц-куллы).
	if (FarRadiusOn > 0.f)
	{
		const float FarR = bWasOn ? FarRadiusOff : FarRadiusOn;
		if (Dist > FarR) { return (int32)EAsdaVisLevel::Off; }
	}
	if (Dist <= KINDA_SMALL_NUMBER) { return (int32)EAsdaVisLevel::Near; }

	// (3) Расширенный конус камеры. Угол между взглядом и направлением на объект минус УГЛОВОЙ РАЗМЕР объекта
	//     (крупный эффект виден краем, когда его центр ещё за границей кадра).
	const float Dot = FMath::Clamp(FVector::DotProduct(D / Dist, CamFwd), -1.f, 1.f);
	const float AngDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
	const float RadDeg = (Radius > 0.f && Dist > Radius)
		? FMath::RadiansToDegrees(FMath::Asin(Radius / Dist))
		: (Radius > 0.f ? 90.f : 0.f);
	const float Limit = bWasOn ? LimitOffDeg : LimitOnDeg;
	// Запас до границы НАСТОЯЩЕГО конуса (без запаса и предсказания): >0 — объект действительно за кадром.
	OutSlackDeg = (AngDeg - RadDeg) - HalfDiagDeg;
	return (AngDeg - RadDeg <= Limit) ? (int32)EAsdaVisLevel::Far : (int32)EAsdaVisLevel::Off;
}

int32 UAsdaVisibilitySubsystem::Evaluate(FAsdaVisState& State, const FVector& Loc, float Radius)
{
	// Рубильник и «камеры нет» — полная работа как до волны (никаких сюрпризов на загрузке/логине).
	if (GAsdaVisEnable == 0 || !bCamValid)
	{
		State.Level = (uint8)EAsdaVisLevel::Near;
		State.bChanged = 0;
		return (int32)EAsdaVisLevel::Near;
	}

	const uint64 T0 = FPlatformTime::Cycles64();
	++EvalCalls;

	const int32 Period = FMath::Max(UpdatePeriodFrames, 1);
	bool bDue = bForceAllThisFrame;
	if (!State.bHasPhase)
	{
		// Новый объект — свой слот из кругового раздатчика (разнос по кадрам) и немедленный первый пересчёт.
		State.Phase = (uint8)(NextPhase++ % (uint32)FMath::Max(Period, 1));
		State.bHasPhase = 1;
		bDue = true;
	}
	else if (!bDue)
	{
		bDue = ((FrameCounter % (uint32)Period) == (uint32)(State.Phase % Period));
	}

	if (!bDue)
	{
		State.bChanged = 0;
		EvalCycles += FPlatformTime::Cycles64() - T0;
		return (int32)State.Level;
	}

	++EvalRecomputes;
	float SlackDeg = 1.0e9f;
	const int32 NewLevel = ComputeLevel(State, Loc, Radius, SlackDeg);

	// ★ Проверка поп-ина: объект заморожен (0), а по НАСТОЯЩЕМУ конусу он в кадре (запас ≤ 0) —
	//   это ровно то «появление после попадания в кадр», которого быть не должно. Считаем и логируем.
	if (GAsdaVisPopInCheck != 0 && NewLevel == (int32)EAsdaVisLevel::Off && SlackDeg < 1.0e8f)
	{
		WorstSlackDeg = FMath::Min(WorstSlackDeg, SlackDeg);
		if (SlackDeg <= 0.f)
		{
			++PopInEvents;
			UE_LOG(LogAsdaVis, Warning, TEXT("[видимость] ПОП-ИН: объект в кадре заморожен (запас %.1f°, угл.скорость %.0f °/с, кадр %u)"),
				SlackDeg, CamAngSpeedDeg, FrameCounter);
		}
	}

	State.bChanged = (NewLevel != (int32)State.Level) ? 1 : 0;
	State.Level = (uint8)NewLevel;
	EvalCycles += FPlatformTime::Cycles64() - T0;
	return NewLevel;
}

void UAsdaVisibilitySubsystem::Dump()
{
	UE_LOG(LogAsdaVis, Log,
		TEXT("[видимость] мир=%s Enable=%d камера=%s поз=(%.0f %.0f %.0f) угл.скорость=%.0f °/с полудиагональ=%.1f° порог вкл=%.1f° выкл=%.1f° ")
		TEXT("ближняя=%.0f/%.0f дальняя=%.0f период=%d кадр(ов) резкий_поворот=%d цена=%.3f мс/кадр"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("-"), GAsdaVisEnable, bCamValid ? TEXT("есть") : TEXT("НЕТ"),
		CamLoc.X, CamLoc.Y, CamLoc.Z, CamAngSpeedDeg, HalfDiagDeg, LimitOnDeg, LimitOffDeg,
		NearRadiusOn, NearRadiusOff, FarRadiusOn, UpdatePeriodFrames, bForceAllThisFrame ? 1 : 0, LastWindowMsPerFrame);
	UE_LOG(LogAsdaVis, Log, TEXT("[видимость] потребители: плашки=%d эффекты=%d (asda.Vis.Nameplates / asda.Vis.Effects)"),
		GAsdaVisNameplates, GAsdaVisEffects);
	UE_LOG(LogAsdaVis, Log, TEXT("[видимость] ПОП-ИН: событий=%d наименьший запас=%.1f° пик угл.скорости=%.0f °/с"),
		PopInEvents, (WorstSlackDeg > 1.0e8f) ? 0.f : WorstSlackDeg, PeakAngSpeedDeg);
}

// Единственная регистрация имени `asda.Vis.Dump` в Source/ (закон ue-console-duplicate-registration).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand CmdAsdaVisDump(
	TEXT("asda.Vis.Dump"),
	TEXT("Состояние подсистемы значимости: камера, пороги конуса, угловая скорость, собственная цена (мс/кадр)."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		int32 N = 0;
		for (TObjectIterator<UAsdaVisibilitySubsystem> It; It; ++It)
		{
			if (!*It || It->IsTemplate()) { continue; }
			It->Dump();
			++N;
		}
		if (N == 0) { UE_LOG(LogAsdaVis, Log, TEXT("[видимость] подсистем нет (мир не игровой?)")); }
	}));
#endif // ASDA_DEV_CONSOLE

// Обнулить счётчики поп-ина и пик угловой скорости — вызывается перед каждым рывком в проверке поворота.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand CmdAsdaVisPopInReset(
	TEXT("asda.Vis.PopInReset"),
	TEXT("Обнулить счётчики проверки поп-ина (событий, наименьший запас, пик угловой скорости)."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		for (TObjectIterator<UAsdaVisibilitySubsystem> It; It; ++It)
		{
			if (*It && !It->IsTemplate()) { It->ResetPopInCounters(); }
		}
		UE_LOG(LogAsdaVis, Log, TEXT("[видимость] счётчики поп-ина обнулены"));
	}));
#endif // ASDA_DEV_CONSOLE
