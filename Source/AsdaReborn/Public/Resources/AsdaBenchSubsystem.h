// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaBenchSubsystem — ★★★ [ПОРТ] 12.10.2026 АВТОПОДБОР НАСТРОЕК ГРАФИКИ ЗАМЕРОМ НАСТОЯЩЕГО КАДРА (решение владельца:
//   вариант В2 из `Reports/owner_1012/autodetect_options.md` — «с отображением в модальном окне процесса: от старта
//   тестирования, текущих шагов и результатов; применять после того, как человек нажмёт ОК»).
//
// ЗАЧЕМ НЕ ПРОБНИК ДВИЖКА. `UGameUserSettings::RunHardwareBenchmark` (SynthBenchmark) — синтетика ALU/пропускной
//   способности: не меряет ни VSM/SMRT-тени, ни Lumen, ни наше свечение, ни разрешение; индексы плавают между версиями
//   движка и на гибридных ноутбуках попадают на iGPU. Здесь меряется ИМЕННО НАШ кадр: сцена alpen с офлайн-спавном героя
//   и камерой `k1_a1_0208` — той же, что во всей таблице кампании производительности 08–09.09.2026
//   (`Reports/render_sets_2026-09-05/`, `set0_alpen_cams.json`), поэтому числа сравнимы с замерами владельца.
//
// ЭТАЛОН: 80 FPS = 12.5 мс на кадр (`asda.Bench.TargetFps`), с запасом `asda.Bench.Headroom` (0.92 → 11.5 мс).
//   Кадр порта складывается ПОСЛЕДОВАТЕЛЬНО: своя работа рисующего потока + GPU (памятка perf2k-frame-serial);
//   игровой поток под нагрузкой 10–11 мс — это «процессорный пол», который графикой не лечится. Поэтому меряются
//   ТРИ величины: кадр (стенное время), GPU (`RHIGetGPUFrameCycles`) и игровой поток (`GGameThreadTime`), и решение
//   различает «GPU не тянет» (шаг вниз по лестнице) и «процессор ограничивает» (честное сообщение, графику не режем).
//
// ЛЕСТНИЦА (от лучшего к худшему; масштаб рендера трогаем ПОСЛЕДНИМ — у владельца FXAA, и масштаб ниже 100 % под ним
//   даёт мыло): Максимальное@100 → Высокое@100 → Среднее@100 → Низкое@100 → Низкое@87 → Низкое@71.
//   Старт — «Высокое» (умолчание первого запуска, `UAsdaVideoSettingsSubsystem::kDefaultPreset`). Прошло с большим
//   запасом (кадр ≤ 75 % бюджета) — пробуем «Максимальное»; не прошло — шаг вниз, пока GPU не уложится.
//   Уровни «Ультра»/Cine автоматика не ставит (выбор владельца только руками).
//
// РЕЖИМЫ:
//   • ПЕРВЫЙ ЗАПУСК (`StartFirstRun`): после выбора языка (или после заставки, если язык уже выбран) — отключение от
//     логин-сервера, вето всех отправок (`UAsdaNetSubsystem::bBenchSendVeto`), штатная загрузка карты alpen через
//     `UAsdaLoadManagerSubsystem::BeginMapLoad` (экран загрузки как в игре), офлайн-спавн героя
//     (`UAsdaWorldSubsystem::DebugSpawnSelfAt` — та же точка, что пакет 4003), камера на `k1_a1`, окно замера
//     (`WBP_BENCH_WIN`, модальное), проходы, результат; «ОК» — применить и сохранить, «Пропустить» — оставить «Высокое»;
//     в обоих случаях маркер `[AsdaVideo] bBenchDone=1` и возврат на экран логина (заставка второй раз не показывается).
//     Не запускается: в редакторе/PIE (`GIsEditor`), при `-AsdaBench=0`, при прогонах без сети (`-AsdaNoNet`) без явного
//     `-AsdaBench=1` — иначе каждый самотест агентов упирался бы в окно замера.
//   • НА МЕСТЕ (`StartInPlace`, кнопка «Автоподбор» окна настроек 69500): без смены карты и спавна — камера игрока там,
//     где он стоит; те же проходы и то же окно; «ОК» применяет, «Пропустить» возвращает прежние уровни.
//
// ОКНО. Разметка — АССЕТ `/Game/AsdaAssets/UI/CommonWin/WBP_BENCH_WIN` (генератор `Tools/asda_ui_bench_window.py`: клон
//   окна выбора языка V3 — рамка и полоса заголовка окна настроек; закон no-runtime-widget-fixes). Код ставит только
//   подписи (loc 990969…990982) и обработчики. Узлы: заголовок `MESSAGE2_TXT00`, журнал `BENCH_LOG` (многострочный),
//   строка состояния `BENCH_STATUS`, кнопки `MESSAGE2_BUT00` («ОК», до результата выключена) и `MESSAGE2_BUT01`
//   («Пропустить»). Состояние (строки журнала) живёт в подсистеме, окно — только вид: при смене уровня движок снимает
//   виджеты с вьюпорта, и окно открывается заново уже на карте замера.
//
// ЗАМЕР. Каждый проход: применить конфигурацию живьём (`Scalability::SetQualityLevels`, без записи профиля), прогрев
//   `asda.Bench.WarmSec` (шейдеры, подкачка, кэш VSM), затем `asda.Bench.MeasureSec` выборок по кадрам: стенное время
//   кадра, GPU-время, время игрового потока; берётся МЕДИАНА (выбросы первых кадров и фоновых процессов не влияют).
//   На время замера снимаются лимит кадров и вертикальная синхронизация (возвращаются после). Камера плавно
//   поворачивается на `asda.Bench.PanDeg` градусов за проход — статичная камера под кэшем VSM даёт заниженную цену теней.
//
// ПРИМЕНЕНИЕ: только по «ОК» — `UAsdaVideoSettingsSubsystem::SetQualityPreset` + `SetRenderScalePercent` (штатный профиль
//   игрока, запись на диск). До «ОК» выбранная конфигурация лишь показана живьём (сцена за окном — предпросмотр).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"   // EAsdaQualityPreset
#include "AsdaBenchSubsystem.generated.h"

class UUserWidget;
class UAsdaButtonRelay;

// Конфигурация одной ступени лестницы: пресет качества (все группы sg.* разом) и масштаб рендера, %.
// ★ 12.10.2026 (владелец: «в настройках настройки гораздо лучше — используй их в пресетах теста»): ступень — это
//   уровень всех групп + ПЕРЕОПРЕДЕЛЕНИЯ отдельных групп (как у владельца: тени 4, эффекты 4 при остальных 3) + масштаб
//   рендера, в том числе выше 100 % (суперсэмплинг владельца 125 %). Таблица ступеней — ДАННЫЕ:
//   Config/DefaultScalability.ini [AsdaBenchLadder] (числа в коде не дублируются, port-shared-points).
struct FAsdaBenchConfig
{
	EAsdaQualityPreset Preset = EAsdaQualityPreset::High;   // уровень всех групп sg.* (0..4)
	int32 ScalePct = 100;                                    // масштаб рендера, % (71…200; > 100 — пин профиля)
	// Переопределения групп (−1 = как Preset). Порядок — как у Scalability::FQualityLevels.
	int32 View = -1, AA = -1, Shadow = -1, GI = -1, Reflection = -1, PostProcess = -1, Texture = -1, Effects = -1,
	      Foliage = -1, Shading = -1, Landscape = -1;
	// Уровни ступени, разложенные по группам (Preset + переопределения; ResolutionQuality = min(Scale, 100)).
	Scalability::FQualityLevels ToLevels() const;
	// Есть ли переопределения (для подписи «(тени 4, эффекты 4)»).
	bool HasOverrides() const { return View >= 0 || AA >= 0 || Shadow >= 0 || GI >= 0 || Reflection >= 0 || PostProcess >= 0 || Texture >= 0 || Effects >= 0 || Foliage >= 0 || Shading >= 0 || Landscape >= 0; }
};

// Итог одного прохода замера (медианы по выборкам).
struct FAsdaBenchPassResult
{
	int32 ConfigIndex = -1;      // индекс в лестнице
	float FrameMs = 0.f;         // медиана стенного времени кадра
	float GpuMs = 0.f;           // медиана GPU-времени кадра
	float GameThreadMs = 0.f;    // медиана времени игрового потока
	float P95FrameMs = 0.f;      // 95-й перцентиль стенного времени (для журнала)
	int32 Samples = 0;           // число выборок
};

UCLASS()
class ASDAREBORN_API UAsdaBenchSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Путь класса окна и его id в оконной подсистеме.
	static const TCHAR* const kWidgetPath;
	static const FName kWindowId;
	// Имена узлов окна (см. шапку).
	static const TCHAR* const kNodeTitle;    // MESSAGE2_TXT00 — заголовок на полосе
	static const TCHAR* const kNodeLog;      // BENCH_LOG — журнал хода (многострочный)
	static const TCHAR* const kNodeStatus;   // BENCH_STATUS — строка состояния
	static const TCHAR* const kBtnOk;        // MESSAGE2_BUT00 — «ОК»
	static const TCHAR* const kBtnSkip;      // MESSAGE2_BUT01 — «Пропустить»

	// Фазы замера.
	enum class EPhase : uint8
	{
		Idle,        // ничего не идёт
		Travel,      // первый запуск: идёт загрузка карты замера
		Settle,      // карта загружена, герой поставлен — пауза перед первым проходом
		Warm,        // проход: прогрев после смены конфигурации
		Measure,     // проход: сбор выборок
		Result,      // результат показан, ждём «ОК»/«Пропустить»
	};

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── Первый запуск ──
	// Нужен ли замер при этом старте (см. шапку: маркер bBenchDone, редактор, ключи командной строки).
	bool ShouldRunFirstRun() const;
	// Запустить замер первого запуска из мира `World` (экран логина): отключение от сети, вето отправок, загрузка карты.
	//   false — не запущен (нет подсистем/карты); вызывающий тогда показывает экран логина обычным путём.
	bool StartFirstRun(UWorld* World);
	// Забрать признак «вернулись с карты замера» — заставку логина второй раз не показывать.
	bool ConsumeSkipStartLogo();
	// Отметить в профиле, что замер первого запуска пройден/пропущен (больше не предлагать).
	void MarkFirstRunDone(const TCHAR* Why);

	// ── На месте (окно настроек) ──
	// Запустить замер на текущей карте и камере без смены уровня. false — уже идёт/нет мира.
	bool StartInPlace(UWorld* World);

	// Сейчас идёт замер (любая фаза, кроме Idle).
	bool IsRunning() const { return Phase != EPhase::Idle; }
	EPhase GetPhase() const { return Phase; }
	// Выбранная конфигурация (после фазы Result); индекс в лестнице, −1 — нет.
	int32 GetChosenIndex() const { return ChosenIndex; }
	const TArray<FAsdaBenchConfig>& GetLadder() const { return Ladder; }
	const TArray<FAsdaBenchPassResult>& GetPasses() const { return Passes; }
	// Строки журнала окна (копия для самотеста).
	const TArray<FString>& GetLogLines() const { return LogLines; }
	// Нажатие «ОК»/«Пропустить» (то же, что клик по кнопке окна; самотест зовёт напрямую).
	void PressOk();
	void PressSkip();
	// Имя уровня пресета для журнала/окна (loc 990314…990318 — те же, что в окне настроек).
	static FString PresetCaption(EAsdaQualityPreset P);
	// Подпись ступени для окна: имя уровня + переопределения («Максимальное (тени максимальное, эффекты максимальное)»).
	static FString ConfigCaption(const FAsdaBenchConfig& C);

	// ★ Самотест (`asda.Bench.SelfTest`): укороченные времена прогрева/замера на этот процесс (секунды; ≤ 0 — штатные).
	float TestWarmSecOverride = -1.f;
	float TestMeasureSecOverride = -1.f;
	float TestSettleSecOverride = -1.f;

private:
	// ── Состояние ──
	EPhase Phase = EPhase::Idle;
	bool bFirstRunMode = false;        // true — первый запуск (с картой), false — на месте
	bool bSkipStartLogoOnce = false;   // вернулись с карты замера — заставку не показывать
	bool bTravelArmed = false;         // ждём PostLoadMap карты замера
	double PhaseStartS = 0.0;          // FPlatformTime начала фазы
	int32 CurrentIndex = -1;           // индекс конфигурации текущего прохода
	int32 ChosenIndex = -1;            // итог
	bool bClimbing = false;            // идём ВВЕРХ по лестнице (стартовая ступень прошла с запасом); провал — итог = последняя прошедшая
	int32 StartIndex = 0;              // стартовая ступень (из [AsdaBenchLadder] Start=1 у ступени; иначе — середина)
	bool bCpuLimited = false;          // процессор ограничивает (игровой поток ≥ бюджета)
	bool bTargetMissed = false;        // эталон не достигнут даже на последней ступени
	int32 PassNo = 0;                  // порядковый номер прохода (для журнала)
	TArray<FAsdaBenchConfig> Ladder;   // лестница ступеней (сверху — лучшая), из [AsdaBenchLadder]
	// Прочитать лестницу из Config/DefaultScalability.ini [AsdaBenchLadder] (+Step=Level=3,Shadow=4,Effects=4,Scale=125[,Start=1]);
	//   нет/пусто — встроенная запасная (Epic…Low@71) с предупреждением.
	void LoadLadder();
	TArray<FAsdaBenchPassResult> Passes;      // итоги проходов по порядку (журнал, самотест)
	TArray<float> SampleFrameMs, SampleGpuMs, SampleGtMs;   // выборки текущего прохода
	double LastTickS = 0.0;            // стенное время прошлого тика (для дельты кадра)
	FTSTicker::FDelegateHandle TickHandle;   // покадровый тик машины состояний (снимается в Finish/Deinitialize)
	FDelegateHandle PostLoadHandle;          // PostLoadMapWithWorld — ждём карту замера (первый запуск)
	// Что было до замера (режим «на месте» возвращает по «Пропустить»; первый запуск — не нужно, профиль High).
	Scalability::FQualityLevels LevelsBefore;
	// Масштаб рендера профиля до замера, % (у владельца бывает > 100 — суперсэмплинг `RenderScaleAbove100`, пин
	//   `r.ScreenPercentage` приоритетом консоли). Ступени «@100» означают «масштаб игрока как есть», 87/71 — ниже него.
	int32 ScaleBefore = 100;
	// Ключи, снятые на время замера приоритетом консоли (лимит кадров, VSync) — для журнала; возврат — `Unset`.
	int32 SavedMaxFps = -1;
	int32 SavedVSync = -1;
	// Камера: исходный орбит-угол и угол на старте прохода (поворот на PanDeg за проход).
	float PanStartYaw = 0.f;
	bool bPanInited = false;
	// Герой замера (первый запуск) — где стоит (координаты UE) и куда смотрит.
	FVector BenchLoc = FVector::ZeroVector;
	float BenchYaw = 0.f;
	float BenchPitch = -20.f;
	// Окно и реле кнопок.
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Window;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	// ★ Задник замера первого запуска (владелец 12.10: «кадр игроку не показываем»): непрозрачный чёрный слой на весь экран
	//   ПОД окном (z 19000 — ниже полосы модалок 20000, выше всех окон мира). Сцена под ним рендерится как есть — замер
	//   настоящий, игрок видит только окно. Виджет — пустой WBP_LoadingScreen (как у заставки логина), картинка — код.
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Backdrop;
	TArray<FString> LogLines;          // журнал окна (строки сверху вниз)
	FString StatusLine;                // строка состояния

	// ── Шаги ──
	bool Tick(float DeltaTime);                       // ядро машины состояний (FTSTicker)
	void OnPostLoadMap(UWorld* Loaded);               // карта замера загружена (первый запуск)
	bool PlaceHero(UWorld* W);                        // офлайн-спавн героя и камера k1_a1
	void BeginPass(int32 ConfigIndex);                // применить конфигурацию живьём, прогрев
	void FinishPass();                                // медианы, журнал, решение о следующем шаге
	void Decide(const FAsdaBenchPassResult& R);       // логика лестницы
	void ShowResult();                                // итоговые строки, кнопка «ОК»
	void ApplyChosenToProfile();                      // «ОК»: штатный профиль + запись
	void Finish(bool bApplied);                       // общий хвост: ключи, окно, возврат/закрытие
	void ApplyConfigLive(const FAsdaBenchConfig& C);  // живьём, без записи профиля (масштаб — через профиль, уровни — после)
	int32 EffectiveScale(const FAsdaBenchConfig& C) const;   // = C.ScalePct (ступени задают масштаб явно, в т.ч. > 100) // «@100» → масштаб игрока как есть, иначе число ступени
	void RestoreScaleLive(int32 Percent);             // масштаб рендера живьём через профиль, без записи на диск
	void SetPan(float Alpha01);                       // поворот камеры по ходу прохода
	void SuspendFrameCaps();                          // снять t.MaxFPS/r.VSync на время замера
	void RestoreFrameCaps();
	// Окно: открыть/обновить/закрыть.
	bool OpenWindow(UWorld* W);   // false — ассет окна не загрузился (замер без окна невозможен: нечем нажать «ОК»)
	void RefreshWindow();
	void SetOkEnabled(bool bEnabled);   // «ОК» и её подпись (серая, пока результата нет)
	void CloseWindow();
	void ShowBackdrop();          // задник первого запуска (см. поле Backdrop)
	void HideBackdrop();
	void AddLog(const FString& Line);
	void SetStatus(const FString& Line);
	// Бюджет кадра, мс (из `asda.Bench.TargetFps`).
	static float BudgetMs();
	// Подстановка [ss1]…[ss5] в loc-строку (общая схема маркеров оригинала, памятка loc-string-placeholders).
	static FString Fmt(int32 LocId, std::initializer_list<FString> Args);
	// Медиана/перцентиль массива (копия сортируется).
	static float Percentile(TArray<float> V, float P01);
};
