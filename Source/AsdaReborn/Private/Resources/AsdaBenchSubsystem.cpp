// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaBenchSubsystem — автоподбор настроек графики замером настоящего кадра. Устройство — в заголовке.
#include "Resources/AsdaBenchSubsystem.h"
#include "AsdaDevConsole.h"                 // ASDA_DEV_CVAR / ASDA_DEV_CONSOLE
#include "AsdaNetSubsystem.h"               // отключение от логин-сервера и вето отправок на время замера
#include "World/AsdaWorldSubsystem.h"       // офлайн-спавн героя (та же точка, что пакет 4003)
#include "World/AsdaLoadManagerSubsystem.h" // штатная загрузка карты с экраном загрузки
#include "World/AsdaPlayerController.h"     // орбит-угол камеры (поворот по ходу прохода)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "World/AsdaWorldKind.h"            // IsLoadingMap / IsLoginScene — ждём именно карту замера
#include "Text/AsdaLang.h"                  // язык сессии — фон экрана загрузки теста (pt_RU / pt_EN)
#include "Resources/AsdaMaterialNames.h"    // AsdaLoadScreen::kBenchLoadTex*
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"               // AsdaUi::BindButton
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"            // FAsdaLoginData::Loc
#include "Test/AsdaSelfTestKit.h"           // самотест: каркас аргументов/итога
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"               // задник первого запуска
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Resources/AsdaGameUserSettings.h"  // kRenderScaleMin/Max — границы масштаба ступени
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "RenderTimer.h"                    // GGameThreadTime
#include "RHI.h"                            // RHIGetGPUFrameCycles
#include "Scalability.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBench, Log, All);

// ── Ключи (единственная регистрация в Source/; закон ue-console-duplicate-registration) ─────────────────────────────
static ASDA_DEV_CVAR(float, CVarAsdaBenchTargetFps, TEXT("asda.Bench.TargetFps"), 80.f,
	TEXT("Эталон частоты кадров автоподбора (решение владельца 12.10.2026: 80)."), ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaBenchHeadroom, TEXT("asda.Bench.Headroom"), 0.92f,
	TEXT("Запас: конфигурация годна, если медиана кадра <= бюджет × запас (0.92 → 11.5 мс при 80 FPS)."), ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaBenchWarmSec, TEXT("asda.Bench.WarmSec"), 1.0f,
	TEXT("Прогрев после смены конфигурации, с (шейдеры, подкачка, кэш VSM) — выборки не собираются."), ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaBenchMeasureSec, TEXT("asda.Bench.MeasureSec"), 3.0f,
	TEXT("Длительность сбора выборок одного прохода, с."), ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaBenchSettleSec, TEXT("asda.Bench.SettleSec"), 2.0f,
	TEXT("Пауза после постановки героя на карте замера до первого прохода, с."), ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaBenchPanDeg, TEXT("asda.Bench.PanDeg"), 25.f,
	TEXT("Поворот орбит-камеры за проход, градусы (0 — камера неподвижна; статичная камера занижает цену теней)."), ECVF_Default);
static ASDA_DEV_CVAR(float, CVarAsdaBenchUpStepFrac, TEXT("asda.Bench.UpStepFrac"), 0.75f,
	TEXT("Пробовать ступень выше стартовой, если медиана кадра <= бюджет × эта доля (0.75 → 9.4 мс)."), ECVF_Default);

// Пути и имена узлов окна.
const TCHAR* const UAsdaBenchSubsystem::kWidgetPath = TEXT("/Game/AsdaAssets/UI/CommonWin/WBP_BENCH_WIN.WBP_BENCH_WIN_C");
const FName UAsdaBenchSubsystem::kWindowId(TEXT("BENCH_WIN"));
const TCHAR* const UAsdaBenchSubsystem::kNodeTitle  = TEXT("MESSAGE2_TXT00");
const TCHAR* const UAsdaBenchSubsystem::kNodeLog    = TEXT("BENCH_LOG");
const TCHAR* const UAsdaBenchSubsystem::kNodeStatus = TEXT("BENCH_STATUS");
const TCHAR* const UAsdaBenchSubsystem::kBtnOk      = TEXT("MESSAGE2_BUT00");
const TCHAR* const UAsdaBenchSubsystem::kBtnSkip    = TEXT("MESSAGE2_BUT01");

// Loc-строки окна (наш диапазон 9909xx, ведомость `Tools/data/gametext_overrides.json`, EN — `Tools/data/loc/en/GameText.json`).
namespace BenchLoc
{
	static constexpr int32 kTitle      = 990969;   // «Проверка производительности»
	static constexpr int32 kTarget     = 990970;   // «Эталон: [ss1] FPS ([ss2] мс на кадр)»
	static constexpr int32 kLoading    = 990971;   // «Загрузка тестовой сцены…»
	static constexpr int32 kPassWarm   = 990972;   // «Проход [ss1]: [ss2], масштаб [ss3] % — прогрев…»
	static constexpr int32 kPassDone   = 990973;   // «Проход [ss1]: [ss2], масштаб [ss3] % — [ss4] мс ([ss5] FPS), видеокарта [ss6] мс, процессор [ss7] мс»
	static constexpr int32 kRecommend  = 990974;   // «Рекомендуется: [ss1], масштаб [ss2] %»
	static constexpr int32 kCpuLimited = 990975;   // «Процессор ограничивает частоту кадров: снижение качества графики не поможет»
	static constexpr int32 kMissed     = 990976;   // «Эталон не достигнут даже на минимальных настройках»
	static constexpr int32 kHint       = 990977;   // «Нажмите «ОК», чтобы применить, или «Пропустить», чтобы оставить прежние настройки»
	static constexpr int32 kOk         = 990978;   // «ОК»
	static constexpr int32 kSkip       = 990979;   // «Пропустить»
	static constexpr int32 kMeasuring  = 990980;   // «Замер: [ss1] с…»
	static constexpr int32 kDone       = 990981;   // «Готово»
	static constexpr int32 kApplied    = 990982;   // «Применено: [ss1], масштаб [ss2] %»
	// Имена уровней — те же, что в окне настроек (AsdaSystemOptionSubsystem SysOptLoc::kLvl*).
	static constexpr int32 kLvlLow   = 990314;
	static constexpr int32 kLvlMed   = 990315;
	static constexpr int32 kLvlHigh  = 990316;
	static constexpr int32 kLvlMax   = 990317;
	static constexpr int32 kLvlUltra = 990318;
}

// Раздел профиля с маркерами видео (тот же, что `[AsdaVideo] bInitialized`).
static const TCHAR* kBenchIniSection = TEXT("AsdaVideo");
static const TCHAR* kBenchIniDone    = TEXT("bBenchDone");
// Раздел проекта с картой и камерой замера (Config/DefaultGame.ini).
static const TCHAR* kBenchCfgSection = TEXT("AsdaBench");
// Экран логина (возврат после замера первого запуска; тот же путь, что у окна настроек «выход в меню»).
static const TCHAR* kLoginLevel = TEXT("/Game/Map/Login");

void UAsdaBenchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadLadder();   // ступени-пресеты владельца — Config/DefaultScalability.ini [AsdaBenchLadder]
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UAsdaBenchSubsystem::OnPostLoadMap);
}

Scalability::FQualityLevels FAsdaBenchConfig::ToLevels() const
{
	Scalability::FQualityLevels L = Scalability::GetQualityLevels();
	L.SetFromSingleQualityLevel(UAsdaVideoSettingsSubsystem::PresetToLevel(Preset));
	if (View >= 0)        { L.ViewDistanceQuality = View; }
	if (AA >= 0)          { L.AntiAliasingQuality = AA; }
	if (Shadow >= 0)      { L.ShadowQuality = Shadow; }
	if (GI >= 0)          { L.GlobalIlluminationQuality = GI; }
	if (Reflection >= 0)  { L.ReflectionQuality = Reflection; }
	if (PostProcess >= 0) { L.PostProcessQuality = PostProcess; }
	if (Texture >= 0)     { L.TextureQuality = Texture; }
	if (Effects >= 0)     { L.EffectsQuality = Effects; }
	if (Foliage >= 0)     { L.FoliageQuality = Foliage; }
	if (Shading >= 0)     { L.ShadingQuality = Shading; }
	if (Landscape >= 0)   { L.LandscapeQuality = Landscape; }
	L.ResolutionQuality = (float)FMath::Min(100, ScalePct);   // > 100 держит пин профиля, не группа sg
	return L;
}

void UAsdaBenchSubsystem::LoadLadder()
{
	Ladder.Reset();
	StartIndex = -1;
	TArray<FString> Steps;
	if (GConfig) { GConfig->GetArray(TEXT("AsdaBenchLadder"), TEXT("Step"), Steps, GScalabilityIni); }
	// Ключ → поле переопределения (имена — как в ini; регистр не важен).
	struct FKey { const TCHAR* Name; int32 FAsdaBenchConfig::*Field; };
	static const FKey Keys[] = {
		{ TEXT("View"), &FAsdaBenchConfig::View }, { TEXT("AA"), &FAsdaBenchConfig::AA }, { TEXT("Shadow"), &FAsdaBenchConfig::Shadow },
		{ TEXT("GI"), &FAsdaBenchConfig::GI }, { TEXT("Reflection"), &FAsdaBenchConfig::Reflection }, { TEXT("PostProcess"), &FAsdaBenchConfig::PostProcess },
		{ TEXT("Texture"), &FAsdaBenchConfig::Texture }, { TEXT("Effects"), &FAsdaBenchConfig::Effects }, { TEXT("Foliage"), &FAsdaBenchConfig::Foliage },
		{ TEXT("Shading"), &FAsdaBenchConfig::Shading }, { TEXT("Landscape"), &FAsdaBenchConfig::Landscape },
	};
	for (const FString& Step : Steps)
	{
		FAsdaBenchConfig Cfg;
		bool bLevel = false, bStart = false;
		TArray<FString> Parts;
		Step.ParseIntoArray(Parts, TEXT(","), true);
		for (FString P : Parts)
		{
			FString K, V;
			if (!P.TrimStartAndEnd().Split(TEXT("="), &K, &V)) { continue; }
			K.TrimStartAndEndInline(); V.TrimStartAndEndInline();
			const int32 N = FCString::Atoi(*V);
			if (K.Equals(TEXT("Level"), ESearchCase::IgnoreCase)) { Cfg.Preset = (EAsdaQualityPreset)FMath::Clamp(N, 0, 4); bLevel = true; continue; }
			if (K.Equals(TEXT("Scale"), ESearchCase::IgnoreCase)) { Cfg.ScalePct = FMath::Clamp(N, UAsdaGameUserSettings::kRenderScaleMin, UAsdaGameUserSettings::kRenderScaleMax); continue; }
			if (K.Equals(TEXT("Start"), ESearchCase::IgnoreCase)) { bStart = (N != 0); continue; }
			bool bKnown = false;
			for (const FKey& Key : Keys)
			{
				if (K.Equals(Key.Name, ESearchCase::IgnoreCase)) { Cfg.*Key.Field = FMath::Clamp(N, 0, 4); bKnown = true; break; }
			}
			if (!bKnown) { UE_LOG(LogAsdaBench, Warning, TEXT("[замер] [AsdaBenchLadder] неизвестный ключ «%s» в «%s» — пропущен"), *K, *Step); }
		}
		if (!bLevel) { UE_LOG(LogAsdaBench, Warning, TEXT("[замер] [AsdaBenchLadder] ступень без Level= пропущена: «%s»"), *Step); continue; }
		if (bStart) { StartIndex = Ladder.Num(); }
		Ladder.Add(Cfg);
	}
	if (Ladder.Num() == 0)
	{
		UE_LOG(LogAsdaBench, Warning, TEXT("[замер] [AsdaBenchLadder] в Config/DefaultScalability.ini нет ступеней — встроенная запасная лестница"));
		FAsdaBenchConfig E; E.Preset = EAsdaQualityPreset::Epic;   Ladder.Add(E);
		FAsdaBenchConfig Hh; Hh.Preset = EAsdaQualityPreset::High; Ladder.Add(Hh);
		FAsdaBenchConfig M; M.Preset = EAsdaQualityPreset::Medium; Ladder.Add(M);
		FAsdaBenchConfig L0; L0.Preset = EAsdaQualityPreset::Low;  Ladder.Add(L0);
		FAsdaBenchConfig L87 = L0; L87.ScalePct = 87; Ladder.Add(L87);
		FAsdaBenchConfig L71 = L0; L71.ScalePct = 71; Ladder.Add(L71);
		StartIndex = 1;
	}
	if (StartIndex < 0) { StartIndex = Ladder.Num() / 2; }
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] лестница: %d ступеней, старт — индекс %d"), Ladder.Num(), StartIndex);
}

FString UAsdaBenchSubsystem::ConfigCaption(const FAsdaBenchConfig& C)
{
	FString S = PresetCaption(C.Preset);
	if (!C.HasOverrides()) { return S; }
	// Переопределения — через имена групп окна настроек (те же loc), только те, что заданы.
	struct FG { int32 FAsdaBenchConfig::*Field; int32 Loc; };
	static const FG Groups[] = {
		{ &FAsdaBenchConfig::Shadow, 990984 }, { &FAsdaBenchConfig::Effects, 990985 }, { &FAsdaBenchConfig::GI, 990986 },
		{ &FAsdaBenchConfig::View, 990987 }, { &FAsdaBenchConfig::Texture, 990988 }, { &FAsdaBenchConfig::Reflection, 990989 },
		{ &FAsdaBenchConfig::PostProcess, 990990 }, { &FAsdaBenchConfig::Foliage, 990991 }, { &FAsdaBenchConfig::AA, 990992 },
		{ &FAsdaBenchConfig::Shading, 990993 }, { &FAsdaBenchConfig::Landscape, 990994 },
	};
	TArray<FString> Parts;
	for (const FG& G : Groups)
	{
		const int32 V = C.*G.Field;
		if (V < 0) { continue; }
		const FString Name = FAsdaLoginData::Loc(G.Loc);
		Parts.Add(FString::Printf(TEXT("%s %s"), *Name, *PresetCaption((EAsdaQualityPreset)FMath::Clamp(V, 0, 4)).ToLower()));
	}
	return Parts.Num() ? FString::Printf(TEXT("%s (%s)"), *S, *FString::Join(Parts, TEXT(", "))) : S;
}

void UAsdaBenchSubsystem::Deinitialize()
{
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
	if (PostLoadHandle.IsValid()) { FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle); PostLoadHandle.Reset(); }
	Super::Deinitialize();
}

// ── Первый запуск ──────────────────────────────────────────────────────────────────────────────────────────────────

bool UAsdaBenchSubsystem::ShouldRunFirstRun() const
{
	// ★ 12.10.2026 (владелец проверяет из редактора кнопкой Play): в PIE замер идёт как в игре — смена уровня в PIE
	//   штатная (так же перезагружается логин после выбора языка). Только не в командлетах (headless-скрипты).
	if (IsRunningCommandlet()) { return false; }
	int32 Override = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("-AsdaBench="), Override))
	{
		if (Override == 0) { return false; }
	}
	else if (UAsdaNetSubsystem::IsNoNetRun())
	{
		return false;   // прогоны агентов без сети: только по явному -AsdaBench=1
	}
	bool bDone = false;
	if (GConfig) { GConfig->GetBool(kBenchIniSection, kBenchIniDone, bDone, GGameUserSettingsIni); }
	return !bDone;
}

void UAsdaBenchSubsystem::MarkFirstRunDone(const TCHAR* Why)
{
	if (!GConfig) { return; }
	GConfig->SetBool(kBenchIniSection, kBenchIniDone, true, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] маркер bBenchDone=1 (%s)"), Why);
}

bool UAsdaBenchSubsystem::ConsumeSkipStartLogo()
{
	const bool b = bSkipStartLogoOnce;
	bSkipStartLogoOnce = false;
	return b;
}

bool UAsdaBenchSubsystem::StartFirstRun(UWorld* World)
{
	if (IsRunning() || !World) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!LM || !Net) { return false; }
	int32 MapId = 3;   // alpen
	if (GConfig) { GConfig->GetInt(kBenchCfgSection, TEXT("MapId"), MapId, GGameIni); }
	if (!LM->GetMapRow(MapId)) { UE_LOG(LogAsdaBench, Warning, TEXT("[замер] карта %d не в реестре — замер не запущен"), MapId); return false; }
	// Камера k1_a1_0208 (кампания производительности): координаты UE и углы орбит-камеры — из проекта, не из кода.
	float X = -39800.f, Y = 14200.f, Z = 3600.f;
	if (GConfig)
	{
		GConfig->GetFloat(kBenchCfgSection, TEXT("CamX"), X, GGameIni);
		GConfig->GetFloat(kBenchCfgSection, TEXT("CamY"), Y, GGameIni);
		GConfig->GetFloat(kBenchCfgSection, TEXT("CamZ"), Z, GGameIni);
		GConfig->GetFloat(kBenchCfgSection, TEXT("CamYaw"), BenchYaw, GGameIni);
		GConfig->GetFloat(kBenchCfgSection, TEXT("CamPitch"), BenchPitch, GGameIni);
	}
	BenchLoc = FVector(X, Y, Z);
	bFirstRunMode = true;
	ScaleBefore = 100;   // чистый профиль первого запуска (ApplyDefaultVideo): масштаб 100
	Passes.Reset(); LogLines.Reset(); StatusLine.Reset();
	ChosenIndex = -1; CurrentIndex = -1; bClimbing = false; bCpuLimited = false; bTargetMissed = false; PassNo = 0;
	// Сеть: логин-сервер нам не нужен, а карта замера не должна отправить ни байта (офлайн-спавн зовёт боевой путь входа).
	if (Net->IsConnected()) { Net->Disconnect(); }
	Net->bBenchSendVeto = true;
	AddLog(Fmt(BenchLoc::kTarget, { FString::FromInt(FMath::RoundToInt(CVarAsdaBenchTargetFps.GetValueOnGameThread())),
	                                FString::Printf(TEXT("%.1f"), BudgetMs()) }));
	AddLog(FAsdaLoginData::Loc(BenchLoc::kLoading));
	Phase = EPhase::Travel;
	PhaseStartS = FPlatformTime::Seconds();
	bTravelArmed = true;
	if (!TickHandle.IsValid()) { TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaBenchSubsystem::Tick), 0.f); }
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] первый запуск: карта %d, камера (%.0f %.0f %.0f) yaw %.0f pitch %.0f → загрузка"),
		MapId, X, Y, Z, BenchYaw, BenchPitch);
	// ★ 12.10.2026 (владелец): экран загрузки теста — свой фон на языке сессии (pt_RU / pt_EN), разово.
	LM->SetNextLoadingTexture(FAsdaLang::IsRu() ? AsdaLoadScreen::kBenchLoadTexRu : AsdaLoadScreen::kBenchLoadTexEn);
	LM->BeginMapLoad(MapId);
	return true;
}

void UAsdaBenchSubsystem::OnPostLoadMap(UWorld* Loaded)
{
	if (!bTravelArmed || Phase != EPhase::Travel || !Loaded) { return; }
	if (Loaded->GetGameInstance() != GetGameInstance()) { return; }
	UAsdaLoadManagerSubsystem* LM = GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>();
	// Промежуточный уровень загрузки (L_Loading) — ждём целевую карту.
	if (!LM || LM->CurrentMapId < 0 || AsdaWorldKind::IsLoadingMap(Loaded) || AsdaWorldKind::IsLoginScene(Loaded)) { return; }
	bTravelArmed = false;
	if (!PlaceHero(Loaded))
	{
		UE_LOG(LogAsdaBench, Warning, TEXT("[замер] герой не поставлен — замер отменён, возврат на логин"));
		MarkFirstRunDone(TEXT("герой не поставлен"));
		Finish(false);
		return;
	}
	Phase = EPhase::Settle;
	PhaseStartS = FPlatformTime::Seconds();
}

bool UAsdaBenchSubsystem::PlaceHero(UWorld* W)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	if (!WS || !LM || !W) { return false; }
	// Тайл под камерой — тот же пересчёт осей, что у спавна (AsdaCoord: UE → игра → тайл 100 ед.).
	const FVector2D G = AsdaCoord::UeToGame2D(BenchLoc);
	const int32 TileX = FMath::FloorToInt(G.X / AsdaCoord::kTileUnits);
	const int32 TileZ = FMath::FloorToInt(G.Y / AsdaCoord::kTileUnits);
	WS->DebugSpawnSelfAt(TileX, TileZ, TEXT("Bench"));
	AActor* Hero = IsValid(WS->LocalPlayerActor) ? WS->LocalPlayerActor.Get() : nullptr;   // публичное поле (LiveSelfActor приватен)
	if (!Hero) { return false; }
	// Как BugItGo кампании: герой в точке камеры, орбит-камера под углами k1_a1.
	Hero->SetActorLocation(BenchLoc, false, nullptr, ETeleportType::TeleportPhysics);
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
	{
		PC->SetOrbitAnglesForBench(BenchYaw, BenchPitch);
	}
	// Экран загрузки менеджер снимает сам (ветка без game-соединения в его OnPostLoadMap) — здесь не трогаем (ревизия МЕЛК-13).
	// Без окна замер невозможен — игроку нечем нажать «ОК»/«Пропустить» (ревизия КРИТ-2): отмена, возврат на логин.
	if (!OpenWindow(W))
	{
		MarkFirstRunDone(TEXT("окно замера не загрузилось"));
		return false;
	}
	ShowBackdrop();   // владелец 12.10: сцену замера игроку не показываем — только окно хода
	return true;
}

// ── На месте ───────────────────────────────────────────────────────────────────────────────────────────────────────

bool UAsdaBenchSubsystem::StartInPlace(UWorld* World)
{
	if (IsRunning() || !World) { return false; }
	bFirstRunMode = false;
	Passes.Reset(); LogLines.Reset(); StatusLine.Reset();
	ChosenIndex = -1; CurrentIndex = -1; bClimbing = false; bCpuLimited = false; bTargetMissed = false; PassNo = 0;
	LevelsBefore = Scalability::GetQualityLevels();
	if (const UAsdaVideoSettingsSubsystem* V = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr)
	{
		ScaleBefore = FMath::Max(1, V->GetRenderScalePercent());   // у владельца может быть > 100 (суперсэмплинг)
	}
	AddLog(Fmt(BenchLoc::kTarget, { FString::FromInt(FMath::RoundToInt(CVarAsdaBenchTargetFps.GetValueOnGameThread())),
	                                FString::Printf(TEXT("%.1f"), BudgetMs()) }));
	if (!OpenWindow(World)) { return false; }   // ревизия КРИТ-2: без окна не начинаем (уровни не тронуты)
	Phase = EPhase::Settle;
	PhaseStartS = FPlatformTime::Seconds();
	if (!TickHandle.IsValid()) { TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsdaBenchSubsystem::Tick), 0.f); }
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] на месте: %s"), *UAsdaVideoSettingsSubsystem::DescribeQualityLevels());
	return true;
}

// ── Машина состояний ───────────────────────────────────────────────────────────────────────────────────────────────

bool UAsdaBenchSubsystem::Tick(float /*DeltaTime*/)
{
	const double Now = FPlatformTime::Seconds();
	const double WallDt = (LastTickS > 0.0) ? (Now - LastTickS) : 0.0;
	LastTickS = Now;
	const float Settle = (TestSettleSecOverride > 0.f) ? TestSettleSecOverride : CVarAsdaBenchSettleSec.GetValueOnGameThread();
	const float Warm   = (TestWarmSecOverride > 0.f) ? TestWarmSecOverride : CVarAsdaBenchWarmSec.GetValueOnGameThread();
	const float Meas   = (TestMeasureSecOverride > 0.f) ? TestMeasureSecOverride : CVarAsdaBenchMeasureSec.GetValueOnGameThread();
	switch (Phase)
	{
	case EPhase::Idle:
		return true;
	case EPhase::Travel:
		// Страховка: карта не загрузилась за 90 с — возврат на логин без замера.
		if (Now - PhaseStartS > 90.0)
		{
			UE_LOG(LogAsdaBench, Warning, TEXT("[замер] карта замера не загрузилась за 90 с — отмена"));
			MarkFirstRunDone(TEXT("таймаут загрузки"));
			Finish(false);
		}
		return true;
	case EPhase::Settle:
		if (Now - PhaseStartS >= Settle)
		{
			SuspendFrameCaps();
			BeginPass(StartIndex);   // стартовая ступень лестницы ([AsdaBenchLadder] Start=1)
		}
		return true;
	case EPhase::Warm:
		if (Now - PhaseStartS >= Warm)
		{
			Phase = EPhase::Measure;
			PhaseStartS = Now;
			SampleFrameMs.Reset(); SampleGpuMs.Reset(); SampleGtMs.Reset();
			bPanInited = false;
		}
		return true;
	case EPhase::Measure:
	{
		const float Elapsed = (float)(Now - PhaseStartS);
		if (WallDt > 0.0 && WallDt < 1.0)   // отбрасываем паузы (свёрнутое окно, подвисание загрузчика)
		{
			SampleFrameMs.Add((float)(WallDt * 1000.0));
			SampleGpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
			SampleGtMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		}
		SetPan(FMath::Clamp(Elapsed / FMath::Max(0.01f, Meas), 0.f, 1.f));
		SetStatus(Fmt(BenchLoc::kMeasuring, { FString::Printf(TEXT("%.0f"), FMath::Max(0.f, Meas - Elapsed)) }));
		if (Elapsed >= Meas) { FinishPass(); }
		return true;
	}
	case EPhase::Result:
		return true;
	}
	return true;
}

void UAsdaBenchSubsystem::BeginPass(int32 ConfigIndex)
{
	CurrentIndex = FMath::Clamp(ConfigIndex, 0, Ladder.Num() - 1);
	const FAsdaBenchConfig& C = Ladder[CurrentIndex];
	++PassNo;
	ApplyConfigLive(C);
	AddLog(Fmt(BenchLoc::kPassWarm, { FString::FromInt(PassNo), ConfigCaption(C), FString::FromInt(C.ScalePct) }));
	Phase = EPhase::Warm;
	PhaseStartS = FPlatformTime::Seconds();
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] проход %d: %s @%d%% — прогрев"), PassNo, UAsdaVideoSettingsSubsystem::PresetName(C.Preset), C.ScalePct);
}

void UAsdaBenchSubsystem::FinishPass()
{
	FAsdaBenchPassResult R;
	R.ConfigIndex = CurrentIndex;
	R.Samples = SampleFrameMs.Num();
	R.FrameMs = Percentile(SampleFrameMs, 0.5f);
	R.GpuMs = Percentile(SampleGpuMs, 0.5f);
	R.GameThreadMs = Percentile(SampleGtMs, 0.5f);
	R.P95FrameMs = Percentile(SampleFrameMs, 0.95f);
	Passes.Add(R);
	const FAsdaBenchConfig& C = Ladder[CurrentIndex];
	const float Fps = (R.FrameMs > 0.f) ? (1000.f / R.FrameMs) : 0.f;
	// Последняя строка журнала — «прогрев…» этого прохода; заменяем её итогом.
	if (LogLines.Num() > 0) { LogLines.Pop(); }
	AddLog(Fmt(BenchLoc::kPassDone, { FString::FromInt(PassNo), ConfigCaption(C), FString::FromInt(C.ScalePct),
		FString::Printf(TEXT("%.1f"), R.FrameMs), FString::FromInt(FMath::RoundToInt(Fps)),
		FString::Printf(TEXT("%.1f"), R.GpuMs), FString::Printf(TEXT("%.1f"), R.GameThreadMs) }));
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] проход %d: %s @%d%% — кадр %.2f мс (p95 %.2f, %d выборок), GPU %.2f, игровой поток %.2f"),
		PassNo, UAsdaVideoSettingsSubsystem::PresetName(C.Preset), C.ScalePct, R.FrameMs, R.P95FrameMs, R.Samples, R.GpuMs, R.GameThreadMs);
	Decide(R);
}

void UAsdaBenchSubsystem::Decide(const FAsdaBenchPassResult& R)
{
	const float Budget = BudgetMs() * CVarAsdaBenchHeadroom.GetValueOnGameThread();
	const bool bFrameOk = R.FrameMs <= Budget;
	const bool bGpuOk = R.GpuMs <= Budget;
	// Процессорный пол: игровой поток сам по себе не укладывается в бюджет — графикой не лечится.
	const bool bCpuBound = R.GameThreadMs > Budget;
	if (bFrameOk)
	{
		// Укладываемся. С запасом (кадр <= бюджет × UpStepFrac) — поднимаемся на ступень выше, пока есть куда и пока
		//   проходим; каждая прошедшая ступень запоминается как итог. Без запаса — итог здесь.
		const float UpFrac = CVarAsdaBenchUpStepFrac.GetValueOnGameThread();
		ChosenIndex = CurrentIndex;
		if (CurrentIndex > 0 && R.FrameMs <= BudgetMs() * UpFrac)
		{
			bClimbing = true;
			BeginPass(CurrentIndex - 1);
			return;
		}
		ShowResult();
		return;
	}
	// Не укладываемся. Шли вверх — итог известен: последняя прошедшая ступень (возвращаем её живьём).
	if (bClimbing && ChosenIndex >= 0)
	{
		ApplyConfigLive(Ladder[ChosenIndex]);
		ShowResult();
		return;
	}
	if (bCpuBound && bGpuOk)
	{
		// GPU в бюджете, упираемся в процессор: ниже по лестнице идти бессмысленно.
		bCpuLimited = true;
		ChosenIndex = CurrentIndex;
		ShowResult();
		return;
	}
	if (CurrentIndex + 1 < Ladder.Num())
	{
		BeginPass(CurrentIndex + 1);
		return;
	}
	// Последняя ступень и всё равно не укладываемся.
	bTargetMissed = true;
	bCpuLimited = bCpuBound;
	ChosenIndex = CurrentIndex;
	ShowResult();
}

void UAsdaBenchSubsystem::ShowResult()
{
	RestoreFrameCaps();
	SetPan(0.f);
	const FAsdaBenchConfig& C = Ladder[FMath::Clamp(ChosenIndex, 0, Ladder.Num() - 1)];
	AddLog(Fmt(BenchLoc::kRecommend, { ConfigCaption(C), FString::FromInt(C.ScalePct) }));
	if (bCpuLimited) { AddLog(FAsdaLoginData::Loc(BenchLoc::kCpuLimited)); }
	if (bTargetMissed) { AddLog(FAsdaLoginData::Loc(BenchLoc::kMissed)); }
	AddLog(FAsdaLoginData::Loc(BenchLoc::kHint));
	SetStatus(FAsdaLoginData::Loc(BenchLoc::kDone));
	Phase = EPhase::Result;
	PhaseStartS = FPlatformTime::Seconds();
	SetOkEnabled(true);
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] итог: %s @%d%% (индекс %d)%s%s"), UAsdaVideoSettingsSubsystem::PresetName(C.Preset), C.ScalePct, ChosenIndex,
		bCpuLimited ? TEXT(", процессор ограничивает") : TEXT(""), bTargetMissed ? TEXT(", эталон не достигнут") : TEXT(""));
}

void UAsdaBenchSubsystem::PressOk()
{
	if (Phase != EPhase::Result) { return; }
	ApplyChosenToProfile();
	if (bFirstRunMode) { MarkFirstRunDone(TEXT("ОК")); }
	Finish(true);
}

void UAsdaBenchSubsystem::PressSkip()
{
	// В фазе Travel окна ещё нет, а менеджер загрузки держит незавершённый травел — отменять нечем (ревизия СРЕДН-3).
	if (Phase == EPhase::Idle || Phase == EPhase::Travel) { return; }
	RestoreFrameCaps();
	if (bFirstRunMode)
	{
		// Первый запуск: профиль не менялся — живьём возвращаем его уровни и масштаб.
		if (UAsdaVideoSettingsSubsystem* V = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr) { V->ReapplyQuality(); }
		MarkFirstRunDone(TEXT("Пропустить"));
	}
	else
	{
		RestoreScaleLive(ScaleBefore);
		Scalability::SetQualityLevels(LevelsBefore);   // как было до замера
	}
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] пропущено игроком (фаза %d)"), (int32)Phase);
	Finish(false);
}

void UAsdaBenchSubsystem::ApplyChosenToProfile()
{
	UAsdaVideoSettingsSubsystem* V = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
	if (!V || ChosenIndex < 0) { return; }
	const FAsdaBenchConfig& C = Ladder[ChosenIndex];
	// Штатный профиль игрока: пресет (все группы) + масштаб рендера, с записью на диск. Ступень «@100» — масштаб
	//   игрока как есть (у владельца > 100), ступени 87/71 — именно столько.
	const int32 Scale = EffectiveScale(C);
	V->SetQualityPreset(C.Preset);
	// Переопределения групп ступени (как в настройках владельца: тени 4, эффекты 4) — штатными сеттерами профиля + запись.
	if (C.HasOverrides())
	{
		if (C.View >= 0)        { V->SetViewDistanceQuality(C.View); }
		if (C.AA >= 0)          { V->SetAntiAliasingQuality(C.AA); }
		if (C.Shadow >= 0)      { V->SetShadowQuality(C.Shadow); }
		if (C.GI >= 0)          { V->SetGlobalIlluminationQuality(C.GI); }
		if (C.Reflection >= 0)  { V->SetReflectionQuality(C.Reflection); }
		if (C.PostProcess >= 0) { V->SetPostProcessQuality(C.PostProcess); }
		if (C.Texture >= 0)     { V->SetTextureQuality(C.Texture); }
		if (C.Effects >= 0)     { V->SetEffectsQuality(C.Effects); }
		if (C.Foliage >= 0)     { V->SetFoliageQuality(C.Foliage); }
		if (C.Shading >= 0)     { V->SetShadingQuality(C.Shading); }
		if (C.Landscape >= 0)   { if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr) { S->SetLandscapeQuality(C.Landscape); } }
		V->CommitQuality();
	}
	V->SetRenderScalePercent(Scale, /*bSave*/ true);
	AddLog(Fmt(BenchLoc::kApplied, { ConfigCaption(C), FString::FromInt(Scale) }));
	UE_LOG(LogAsdaBench, Log, TEXT("[замер] применено и сохранено: %s @%d%%; %s"), UAsdaVideoSettingsSubsystem::PresetName(C.Preset), Scale,
		*UAsdaVideoSettingsSubsystem::DescribeQualityLevels());
}

void UAsdaBenchSubsystem::Finish(bool bApplied)
{
	RestoreFrameCaps();
	CloseWindow();
	HideBackdrop();
	const bool bWasFirstRun = bFirstRunMode;
	Phase = EPhase::Idle;
	if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
	if (bWasFirstRun)
	{
		// Назад на экран логина: соединение, если оно всё же было, рвём — режим логина поднимет СВЕЖЕЕ (анти-реплей-счётчик
		//   с нуля; ревизия КРИТ-1), вето снимаем, заставку не показываем.
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			if (Net->IsConnected()) { Net->Disconnect(); }
			Net->bBenchSendVeto = false;
		}
		bSkipStartLogoOnce = true;
		bTravelArmed = false;
		UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		UE_LOG(LogAsdaBench, Log, TEXT("[замер] завершён (%s) → экран логина"), bApplied ? TEXT("применено") : TEXT("без применения"));
		if (W) { UGameplayStatics::OpenLevel(W, FName(kLoginLevel)); }
	}
	else
	{
		UE_LOG(LogAsdaBench, Log, TEXT("[замер] завершён на месте (%s)"), bApplied ? TEXT("применено") : TEXT("без применения"));
	}
}

// ── Применение, камера, ключи ──────────────────────────────────────────────────────────────────────────────────────

int32 UAsdaBenchSubsystem::EffectiveScale(const FAsdaBenchConfig& C) const
{
	// Ступени задают масштаб явно, в том числе > 100 (суперсэмплинг владельца 125 % — пин r.ScreenPercentage через профиль,
	//   ревизия СРЕДН-2): измеряем и применяем одно и то же число.
	return C.ScalePct;
}

void UAsdaBenchSubsystem::RestoreScaleLive(int32 Percent)
{
	// Масштаб — через профиль (единственная точка, снимает/ставит и пин > 100 %), БЕЗ записи на диск; штатное применение
	//   профиля перепишет живые уровни своими — поэтому уровни ставятся ПОСЛЕ него (см. ApplyConfigLive).
	UAsdaVideoSettingsSubsystem* V = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!V || !S) { return; }
	if (V->GetRenderScalePercent() == Percent) { return; }
	V->SetRenderScalePercent(Percent, /*bSave*/ false);
	S->ApplyNonResolutionSettings();
}

void UAsdaBenchSubsystem::ApplyConfigLive(const FAsdaBenchConfig& C)
{
	RestoreScaleLive(EffectiveScale(C));           // масштаб через профиль (в т.ч. пин > 100); уровни — ПОСЛЕ него
	Scalability::SetQualityLevels(C.ToLevels());   // уровень + переопределения групп ступени
}

void UAsdaBenchSubsystem::SetPan(float Alpha01)
{
	const float Pan = CVarAsdaBenchPanDeg.GetValueOnGameThread();
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (!PC || Pan == 0.f) { return; }
	if (!bPanInited) { PanStartYaw = PC->GetCameraYawUE(); bPanInited = true; }
	// Плавный ход «туда и обратно»: синус — нулевая скорость на концах, без рывка в начале прохода.
	const float Offset = Pan * FMath::Sin(Alpha01 * PI);
	PC->SetOrbitAnglesForBench(PanStartYaw + Offset, PC->GetCameraPitchUE());
}

void UAsdaBenchSubsystem::SuspendFrameCaps()
{
	IConsoleManager& CM = IConsoleManager::Get();
	if (IConsoleVariable* V = CM.FindConsoleVariable(TEXT("t.MaxFPS"))) { SavedMaxFps = V->GetInt(); V->Set(0, ECVF_SetByConsole); }
	if (IConsoleVariable* V = CM.FindConsoleVariable(TEXT("r.VSync"))) { SavedVSync = V->GetInt(); V->Set(0, ECVF_SetByConsole); }
}

void UAsdaBenchSubsystem::RestoreFrameCaps()
{
	// `Unset(SetByConsole)` возвращает значение и приоритет, которые были ДО замера (у профиля — SetByGameSetting);
	//   повторный `Set(…, SetByConsole)` оставил бы пин консоли, и штатные настройки лимита/VSync до конца процесса
	//   молча игнорировались бы (ревизия СРЕДН-1, лог «r.VSync … ignored … previous SetByConsole»).
	IConsoleManager& CM = IConsoleManager::Get();
	if (SavedMaxFps >= 0) { if (IConsoleVariable* V = CM.FindConsoleVariable(TEXT("t.MaxFPS"))) { V->Unset(ECVF_SetByConsole); } SavedMaxFps = -1; }
	if (SavedVSync >= 0) { if (IConsoleVariable* V = CM.FindConsoleVariable(TEXT("r.VSync"))) { V->Unset(ECVF_SetByConsole); } SavedVSync = -1; }
}

// ── Окно ───────────────────────────────────────────────────────────────────────────────────────────────────────────

bool UAsdaBenchSubsystem::OpenWindow(UWorld* /*W*/)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// z 30 — полоса окна выбора языка (над окнами логина), модальное: ввод к миру/логину не доходит.
	Window = WM->OpenWindowByPath(kWindowId, FString(kWidgetPath), 30, ESlateVisibility::Visible, /*bModal*/ true);
	if (!Window)
	{
		UE_LOG(LogAsdaBench, Warning, TEXT("[замер] окно %s не загрузилось — не прогнан шаг ассета asda_ui_bench_window? замер отменён"), kWidgetPath);
		return false;
	}
	auto SetText = [this](const TCHAR* Node, const FString& S)
	{
		if (UTextBlock* T = Cast<UTextBlock>(Window->GetWidgetFromName(FName(Node)))) { T->SetText(FText::FromString(S)); }
	};
	SetText(kNodeTitle, FAsdaLoginData::Loc(BenchLoc::kTitle));
	SetText(*(FString(kBtnOk) + TEXT("_Text")), FAsdaLoginData::Loc(BenchLoc::kOk));
	SetText(*(FString(kBtnSkip) + TEXT("_Text")), FAsdaLoginData::Loc(BenchLoc::kSkip));
	Relays.Reset();
	if (UButton* Ok = Cast<UButton>(Window->GetWidgetFromName(FName(kBtnOk))))
	{
		Ok->OnClicked.Clear();
		AsdaUi::BindButton(this, Ok, 0, [this](int32) { PressOk(); }, Relays);
	}
	SetOkEnabled(Phase == EPhase::Result);
	if (UButton* Skip = Cast<UButton>(Window->GetWidgetFromName(FName(kBtnSkip))))
	{
		Skip->OnClicked.Clear();
		AsdaUi::BindButton(this, Skip, 1, [this](int32) { PressSkip(); }, Relays);
	}
	RefreshWindow();
	return true;
}

void UAsdaBenchSubsystem::SetOkEnabled(bool bEnabled)
{
	// Кнопка «ОК» до результата выключена; у скина кнопки нет отдельного вида «выключена», поэтому подпись — серая
	//   (как у неактивной «العربية» окна языка), иначе кнопки выглядят одинаково (снимок прогона bench_en 12.10).
	if (!Window) { return; }
	if (UButton* Ok = Cast<UButton>(Window->GetWidgetFromName(FName(kBtnOk)))) { Ok->SetIsEnabled(bEnabled); }
	if (UTextBlock* T = Cast<UTextBlock>(Window->GetWidgetFromName(FName(*(FString(kBtnOk) + TEXT("_Text"))))))
	{
		T->SetColorAndOpacity(FSlateColor(bEnabled ? FLinearColor::White : FLinearColor(0.35f, 0.35f, 0.35f, 1.f)));
	}
}

void UAsdaBenchSubsystem::RefreshWindow()
{
	if (!Window) { return; }
	if (UTextBlock* T = Cast<UTextBlock>(Window->GetWidgetFromName(FName(kNodeLog)))) { T->SetText(FText::FromString(FString::Join(LogLines, TEXT("\n")))); }
	if (UTextBlock* T = Cast<UTextBlock>(Window->GetWidgetFromName(FName(kNodeStatus)))) { T->SetText(FText::FromString(StatusLine)); }
}

void UAsdaBenchSubsystem::CloseWindow()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->FindWindow(kWindowId)) { WM->CloseWindow(kWindowId); }
	}
	Window = nullptr;
	Relays.Reset();
}

void UAsdaBenchSubsystem::ShowBackdrop()
{
	if (Backdrop) { return; }
	UGameInstance* GI = GetGameInstance();
	UClass* WC = LoadClass<UUserWidget>(nullptr, TEXT("/Game/AsdaAssets/UI/Dev/WBP_LoadingScreen.WBP_LoadingScreen_C"));
	if (!GI || !WC) { UE_LOG(LogAsdaBench, Warning, TEXT("[замер] задник: нет класса WBP_LoadingScreen — сцена останется видна")); return; }
	Backdrop = CreateWidget<UUserWidget>(GI, WC);
	UCanvasPanel* Root = (Backdrop && Backdrop->WidgetTree) ? Cast<UCanvasPanel>(Backdrop->WidgetTree->RootWidget) : nullptr;
	if (!Root) { Backdrop = nullptr; return; }
	UImage* Img = Backdrop->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("AsdaBenchBackdrop"));
	FSlateBrush B;
	B.DrawAs = ESlateBrushDrawType::Box;
	B.TintColor = FSlateColor(FLinearColor::Black);
	Img->SetBrush(B);
	Img->SetColorAndOpacity(FLinearColor::Black);
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Root->AddChild(Img)))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));   // весь экран при любом размере окна
		S->SetOffsets(FMargin(0.f));
	}
	Img->SetVisibility(ESlateVisibility::HitTestInvisible);
	Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	Backdrop->AddToViewport(19000);   // ниже полосы модалок (20000), выше окон мира
}

void UAsdaBenchSubsystem::HideBackdrop()
{
	if (Backdrop) { Backdrop->RemoveFromParent(); Backdrop = nullptr; }
}

void UAsdaBenchSubsystem::AddLog(const FString& Line)
{
	LogLines.Add(Line);
	RefreshWindow();
}

void UAsdaBenchSubsystem::SetStatus(const FString& Line)
{
	if (StatusLine == Line) { return; }
	StatusLine = Line;
	RefreshWindow();
}

// ── Утилиты ────────────────────────────────────────────────────────────────────────────────────────────────────────

float UAsdaBenchSubsystem::BudgetMs()
{
	const float Fps = FMath::Max(1.f, CVarAsdaBenchTargetFps.GetValueOnGameThread());
	return 1000.f / Fps;
}

FString UAsdaBenchSubsystem::PresetCaption(EAsdaQualityPreset P)
{
	int32 Id = BenchLoc::kLvlHigh;
	switch (P)
	{
		case EAsdaQualityPreset::Low:       Id = BenchLoc::kLvlLow;   break;
		case EAsdaQualityPreset::Medium:    Id = BenchLoc::kLvlMed;   break;
		case EAsdaQualityPreset::High:      Id = BenchLoc::kLvlHigh;  break;
		case EAsdaQualityPreset::Epic:      Id = BenchLoc::kLvlMax;   break;
		case EAsdaQualityPreset::Cinematic: Id = BenchLoc::kLvlUltra; break;
		default: break;
	}
	const FString S = FAsdaLoginData::Loc(Id);
	return S.IsEmpty() ? FString(UAsdaVideoSettingsSubsystem::PresetName(P)) : S;
}

FString UAsdaBenchSubsystem::Fmt(int32 LocId, std::initializer_list<FString> Args)
{
	FString S = FAsdaLoginData::Loc(LocId);
	int32 i = 1;
	for (const FString& A : Args)
	{
		S.ReplaceInline(*FString::Printf(TEXT("[ss%d]"), i), *A);
		++i;
	}
	S.ReplaceInline(TEXT("[/n]"), TEXT("\n"));
	return S;
}

float UAsdaBenchSubsystem::Percentile(TArray<float> V, float P01)
{
	if (V.Num() == 0) { return 0.f; }
	V.Sort();
	const int32 Idx = FMath::Clamp(FMath::RoundToInt(P01 * (V.Num() - 1)), 0, V.Num() - 1);
	return V[Idx];
}

// ── `asda.Bench.Press <ok|skip>` — нажать кнопку окна замера из прогона (-game). Подсистема берётся из ИГРОВОГО контекста
//    движка, а не из мира-подсказки команды: после травела на карту замера очередь `asda.UI.Seq` живёт в старом мире, и
//    `asda.UI.SimClickWidget` там отказывает («нет сетевой подсистемы»). Та же точка, что клик по кнопке.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdBenchPress(
	TEXT("asda.Bench.Press"),
	TEXT("[ПОРТ] Нажать кнопку окна замера графики: ok | skip (прогоны)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* /*Hint*/)
	{
		UAsdaBenchSubsystem* B = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if ((Ctx.WorldType == EWorldType::Game || Ctx.WorldType == EWorldType::PIE) && Ctx.OwningGameInstance)
				{
					B = Ctx.OwningGameInstance->GetSubsystem<UAsdaBenchSubsystem>();
					if (B) { break; }
				}
			}
		}
		if (!B) { UE_LOG(LogAsdaBench, Warning, TEXT("asda.Bench.Press: нет подсистемы замера")); return; }
		const bool bOk = Args.Num() > 0 && Args[0].Equals(TEXT("ok"), ESearchCase::IgnoreCase);
		UE_LOG(LogAsdaBench, Log, TEXT("asda.Bench.Press: %s (фаза %d)"), bOk ? TEXT("ОК") : TEXT("Пропустить"), (int32)B->GetPhase());
		if (bOk) { B->PressOk(); } else { B->PressSkip(); }
	}));
#endif // ASDA_DEV_CONSOLE

// ── Самотест: `asda.Bench.SelfTest [quit]` (офлайн, на текущей карте) ─────────────────────────────────────────────
//    Укороченные времена (прогрев 0.3 с, замер 0.6 с, пауза 0.2 с), окно, журнал, итог, «ОК» → профиль. Проверки:
//    окно открыто и узлы есть; после проходов — фаза Result, ≥ 1 проход с выборками, строка «Рекомендуется»; после
//    PressOk — профиль содержит выбранный пресет (все группы) и масштаб, окно закрыто, фаза Idle.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdBenchSelfTest(
	TEXT("asda.Bench.SelfTest"),
	TEXT("[ПОРТ] Самотест автоподбора графики замером кадра (на месте, укороченные времена). [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* Hint)
	{
		UWorld* W = Hint;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaBenchSubsystem* B = GI ? GI->GetSubsystem<UAsdaBenchSubsystem>() : nullptr;
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Bench.SelfTest"), KitArgs)) { return; }
		if (!B) { UE_LOG(LogAsdaBench, Warning, TEXT("САМОТЕСТ-ЗАМЕР ИТОГ: ok=0 fail=1 (нет мира/подсистемы)")); return; }
		const bool bQuit = KitArgs.bQuit;
		B->TestWarmSecOverride = 0.3f; B->TestMeasureSecOverride = 0.6f; B->TestSettleSecOverride = 0.2f;
		const Scalability::FQualityLevels Before = Scalability::GetQualityLevels();
		if (!B->StartInPlace(W)) { UE_LOG(LogAsdaBench, Warning, TEXT("САМОТЕСТ-ЗАМЕР ИТОГ: ok=0 fail=1 (не запущен)")); AsdaSelfTestKit::Quit(W, bQuit); return; }
		struct FState { int32 Ok = 0, Fail = 0; double T0 = 0.0; bool bOkPressed = false; };
		TSharedRef<FState> St = MakeShared<FState>();
		St->T0 = FPlatformTime::Seconds();
		auto Check = [St](bool b, const FString& What)
		{
			if (b) { ++St->Ok; } else { ++St->Fail; }
			UE_LOG(LogAsdaBench, Warning, TEXT("САМОТЕСТ-ЗАМЕР %s %s"), b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		};
		// Окно и узлы — сразу после открытия.
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			UUserWidget* Win = WM->FindWindow(UAsdaBenchSubsystem::kWindowId);
			Check(Win != nullptr, TEXT("окно BENCH_WIN открыто"));
			if (Win)
			{
				for (const TCHAR* N : { UAsdaBenchSubsystem::kNodeTitle, UAsdaBenchSubsystem::kNodeLog, UAsdaBenchSubsystem::kNodeStatus, UAsdaBenchSubsystem::kBtnOk, UAsdaBenchSubsystem::kBtnSkip })
				{
					Check(Win->GetWidgetFromName(FName(N)) != nullptr, FString::Printf(TEXT("узел %s есть"), N));
				}
				UButton* Ok = Cast<UButton>(Win->GetWidgetFromName(FName(UAsdaBenchSubsystem::kBtnOk)));
				Check(Ok && !Ok->GetIsEnabled(), TEXT("кнопка «ОК» выключена до результата"));
			}
		}
		FTimerHandle H;
		TWeakObjectPtr<UAsdaBenchSubsystem> WB = B;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(B, [WB, W, GI, St, Check, Before, bQuit]()
		{
			UAsdaBenchSubsystem* Bn = WB.Get();
			if (!Bn) { return; }
			const double El = FPlatformTime::Seconds() - St->T0;
			if (!St->bOkPressed)
			{
				if (Bn->GetPhase() != UAsdaBenchSubsystem::EPhase::Result)
				{
					if (El < 40.0) { return; }   // ждём проходы
					Check(false, FString::Printf(TEXT("результат за 40 с не получен (фаза %d)"), (int32)Bn->GetPhase()));
				}
				else
				{
					Check(Bn->GetPasses().Num() >= 1 && Bn->GetPasses()[0].Samples >= 5, FString::Printf(TEXT("проходов %d, выборок в первом %d (ждали >= 5)"),
						Bn->GetPasses().Num(), Bn->GetPasses().Num() ? Bn->GetPasses()[0].Samples : 0));
					Check(Bn->GetChosenIndex() >= 0 && Bn->GetChosenIndex() < Bn->GetLadder().Num(), FString::Printf(TEXT("выбран индекс %d"), Bn->GetChosenIndex()));
					const FString Rec = UAsdaBenchSubsystem::ConfigCaption(Bn->GetLadder()[FMath::Max(0, Bn->GetChosenIndex())]);
					bool bHasRec = false;
					for (const FString& L : Bn->GetLogLines()) { if (L.Contains(Rec) && Bn->GetLogLines().Find(L) >= Bn->GetPasses().Num()) { bHasRec = true; } }
					Check(bHasRec, FString::Printf(TEXT("в журнале строка результата с «%s» (строк %d)"), *Rec, Bn->GetLogLines().Num()));
					if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
					{
						UUserWidget* Win = WM->FindWindow(UAsdaBenchSubsystem::kWindowId);
						UButton* Ok = Win ? Cast<UButton>(Win->GetWidgetFromName(FName(UAsdaBenchSubsystem::kBtnOk))) : nullptr;
						Check(Ok && Ok->GetIsEnabled(), TEXT("кнопка «ОК» включена после результата"));
					}
					// Ключи кадра возвращены.
					int32 MaxFps = -1; if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { MaxFps = V->GetInt(); }
					Check(MaxFps >= 0, FString::Printf(TEXT("t.MaxFPS после замера = %d"), MaxFps));
					Bn->PressOk();
					St->bOkPressed = true;
					return;
				}
			}
			// После «ОК»: профиль и окно.
			const int32 Ci = Bn->GetChosenIndex();
			if (Ci >= 0)
			{
				const FAsdaBenchConfig& C = Bn->GetLadder()[Ci];
				const Scalability::FQualityLevels L = Scalability::GetQualityLevels();
				const Scalability::FQualityLevels Want = C.ToLevels();
				Check(L.ShadowQuality == Want.ShadowQuality && L.TextureQuality == Want.TextureQuality && L.EffectsQuality == Want.EffectsQuality && L.ViewDistanceQuality == Want.ViewDistanceQuality,
					FString::Printf(TEXT("живые уровни = ступень (тени %d, текстуры %d, эффекты %d): %s"), Want.ShadowQuality, Want.TextureQuality, Want.EffectsQuality, *UAsdaVideoSettingsSubsystem::DescribeQualityLevels()));
				int32 SavedShadow = -1, SavedFx = -1;
				if (GConfig) { GConfig->GetInt(TEXT("ScalabilityGroups"), TEXT("sg.ShadowQuality"), SavedShadow, GGameUserSettingsIni); GConfig->GetInt(TEXT("ScalabilityGroups"), TEXT("sg.EffectsQuality"), SavedFx, GGameUserSettingsIni); }
				Check(SavedShadow == Want.ShadowQuality && SavedFx == Want.EffectsQuality, FString::Printf(TEXT("профиль [ScalabilityGroups] тени %d / эффекты %d (ждали %d / %d)"), SavedShadow, SavedFx, Want.ShadowQuality, Want.EffectsQuality));
				const UAsdaVideoSettingsSubsystem* V = GI->GetSubsystem<UAsdaVideoSettingsSubsystem>();
				Check(V && V->GetRenderScalePercent() == C.ScalePct, FString::Printf(TEXT("масштаб рендера %d (ждали %d)"), V ? V->GetRenderScalePercent() : -1, C.ScalePct));
			}
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { Check(WM->FindWindow(UAsdaBenchSubsystem::kWindowId) == nullptr, TEXT("окно закрыто после «ОК»")); }
			Check(Bn->GetPhase() == UAsdaBenchSubsystem::EPhase::Idle, TEXT("фаза Idle после «ОК»"));
			// Вернуть уровни, что были до самотеста (профиль — тоже: SetQualityPreset пишет на диск).
			if (UAsdaVideoSettingsSubsystem* V = GI->GetSubsystem<UAsdaVideoSettingsSubsystem>())
			{
				Scalability::SetQualityLevels(Before);
				Scalability::SaveState(GGameUserSettingsIni);
				if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr) { S->SaveSettings(); }
			}
			Bn->TestWarmSecOverride = -1.f; Bn->TestMeasureSecOverride = -1.f; Bn->TestSettleSecOverride = -1.f;
			UE_LOG(LogAsdaBench, Warning, TEXT("САМОТЕСТ-ЗАМЕР ИТОГ: ok=%d fail=%d"), St->Ok, St->Fail);
			W->GetTimerManager().ClearAllTimersForObject(Bn);
			AsdaSelfTestKit::Quit(W, bQuit);
		}), 0.25f, true);
	}));
#endif // ASDA_DEV_CONSOLE
