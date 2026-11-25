// Copyright (c) 2026 @sitxovski (Discord)
// Реализация захвата курсора при зажатой ПКМ. Устройство и сверка с оригиналом — в шапке `Public/World/AsdaOrbitCursor.h`.
#include "World/AsdaOrbitCursor.h"

#include "AsdaDevConsole.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"
#include "World/AsdaCursorClip.h"    // входы окна: активность, передний план, свёрнуто
#include "World/AsdaCursorSpeed.h"   // множитель скорости указателя Windows

DEFINE_LOG_CATEGORY_STATIC(LogAsdaOrbitCursor, Log, All);

// ★ [ИСПРАВЛЕНИЕ] дефекта оригинала, слово владельца 07.11.2026: 1 — при зажатой ПКМ курсор скрыт и стоит на месте, поворот не упирается
//   в край окна; 0 — прежнее (курсор свободен и виден, у края поворот стоит, как у оригинала). В Shipping ключа нет (там 1).
static ASDA_DEV_CVAR(int32, CVarAsdaOrbitCursorLock,
	TEXT("asda.Camera.OrbitCursorLock"), 1,
	TEXT("1 — при зажатой ПКМ курсор скрыт и стоит на месте, поворот камеры не упирается в край окна, при отпускании курсор на прежнем месте; ")
	TEXT("0 — прежнее (курсор свободен, у края поворот стоит, как у оригинала)."),
	ECVF_Default);

// ★ 09.11.2026 [ИСПРАВЛЕНИЕ] (слова владельца: «в некоторых точках курсор вдруг виден и вращение стоит»; разбор — шапка
//   `Public/World/AsdaOrbitCursor.h`): 1 — поворот держится сквозь пересчёт режима ввода (любое окно, строка чата, показ мира отнимали
//   у вьюпорта захват мыши) и сквозь уход фокуса клавиатуры полю ввода; 0 — прежнее (поворот обрывался, курсор показывался).
static ASDA_DEV_CVAR(int32, CVarAsdaOrbitHoldThrough,
	TEXT("asda.Camera.OrbitHoldThrough"), 1,
	TEXT("1 — пока зажата ПКМ, поворот камеры не обрывают открытие/закрытие окон, нотисы, строка чата и смена режима ввода: захват мыши ")
	TEXT("возвращается вьюпорту, курсор остаётся скрытым; 0 — прежнее (захват отнимался, курсор появлялся, поворот вставал)."),
	ECVF_Default);

namespace AsdaOrbitCursorState
{
	constexpr uint64 kReportFreshFrames = 2;           // доклад тика считается живым столько кадров
	static bool bStarted = false;                      // сторож заведён
	static FTSTicker::FDelegateHandle TickHandle;      // тикер ядра
	static uint64 LastHeldFrame = 0;                   // кадр последнего истинного доклада
	static bool bWasReported = false;                  // прошлый кадр доклад был живым (фронт нажатия)
	static double HeldSinceS = 0.0;                    // начало поворота
	static FVector2D Anchor = FVector2D::ZeroVector;   // точка нажатия
	static bool bEngaged = false;                      // захват стоит
	static float Scale = 1.f;                          // множитель дельты на время захвата
	static int32 EngageCount = 0;                      // сколько раз захват ставился
	static TWeakPtr<FGenericWindow> EngagedWindow;     // окно, на котором включён сырой режим
	constexpr uint64 kRearmFreshFrames = 8;            // ★ 09.11: сброс клавиш ждёт возврата поворота столько кадров
	static uint64 FlushFrame = 0;                      // ★ 09.11: кадр сброса клавиш посреди поворота (0 — нет)
	static int32 RecaptureCount = 0;                   // ★ 09.11: сколько раз захват возвращён вьюпорту
	static int32 RearmCount = 0;                       // ★ 09.11: сколько раз поворот возвращён после сброса клавиш
	static bool bRecaptureFailLogged = false;          // ★ 09.11: неудача возврата захвата уже в журнале (одна строка на зажим)
#if !UE_BUILD_SHIPPING
	static bool bOverrideWindow = false;               // самотест: признаки окна подменены
	static bool bOverrideForeground = false;           // самотест: окно на переднем плане
#endif
}

bool FAsdaOrbitCursor::Decide(const FInputs& In, bool bEngaged, FString& OutWhy)
{
	if (!In.bKeyOn)            { OutWhy = TEXT("ключ выключен"); return false; }
	if (!In.bOrbitReported)    { OutWhy = TEXT("поворота мышью нет"); return false; }
	if (!In.bButtonDown)       { OutWhy = TEXT("ПКМ отпущена"); return false; }
	if (!In.bHasWindow)        { OutWhy = TEXT("нет окна ОС"); return false; }
	if (!In.bAppActive)        { OutWhy = TEXT("приложение неактивно"); return false; }
	if (!In.bForeground)       { OutWhy = TEXT("окно игры не на переднем плане"); return false; }
	if (In.bMinimized)         { OutWhy = TEXT("окно свёрнуто"); return false; }
	if (!In.bViewportCaptured) { OutWhy = TEXT("вьюпорт не держит мышь"); return false; }
	if (bEngaged)              { OutWhy = TEXT("держим"); return true; }
	if (In.MovedPx >= kEngageMovePx || In.HeldSec >= kEngageHoldSec) { OutWhy = TEXT("начало поворота"); return true; }
	OutWhy = TEXT("ждём сдвига или удержания");
	return false;
}

// Окно ОС вьюпорта игры (нет — пусто).
static TSharedPtr<FGenericWindow> AsdaOrbitNativeWindow()
{
	const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
	const TSharedPtr<SWindow> W = VP ? VP->GetWindow() : nullptr;
	return W.IsValid() ? W->GetNativeWindow() : nullptr;
}

// Поставить захват: сырой режим, курсор скрыт, курсор в точке нажатия.
static void AsdaOrbitEngage(FSlateApplication& App, const TCHAR* Why)
{
	using namespace AsdaOrbitCursorState;
	const TSharedPtr<FGenericWindow> Native = AsdaOrbitNativeWindow();
	const TSharedPtr<GenericApplication> Platform = App.GetPlatformApplication();
	if (Platform.IsValid() && !Platform->IsUsingHighPrecisionMouseMode())
	{
		Platform->SetCapture(Native);
		Platform->SetHighPrecisionMouseMode(true, Native);
	}
	if (const TSharedPtr<ICursor> Cursor = App.GetPlatformCursor()) { Cursor->Show(false); }
	App.SetCursorPos(Anchor);
	const int32 Level = FAsdaCursorSpeed::ReadWindowsLevel();
	Scale = Level > 0 ? FAsdaCursorSpeed::LevelMultiplier(Level) : 1.f;
	EngagedWindow = Native;
	bEngaged = true;
	++EngageCount;
	UE_LOG(LogAsdaOrbitCursor, Log, TEXT("[курсор ПКМ] захват ПОСТАВЛЕН в точке (%.0f,%.0f), множитель дельты %.2f — %s"), Anchor.X, Anchor.Y, Scale, Why);
}

// Снять захват. bRestore — вернуть курсор в точку нажатия.
static void AsdaOrbitRelease(FSlateApplication& App, bool bRestore, const TCHAR* Why)
{
	using namespace AsdaOrbitCursorState;
	const TSharedPtr<GenericApplication> Platform = App.GetPlatformApplication();
	if (Platform.IsValid() && Platform->IsUsingHighPrecisionMouseMode())
	{
		Platform->SetHighPrecisionMouseMode(false, EngagedWindow.Pin());
		Platform->SetCapture(nullptr);
	}
	if (bRestore) { App.SetCursorPos(Anchor); }
	if (const TSharedPtr<ICursor> Cursor = App.GetPlatformCursor()) { Cursor->Show(true); }
	App.QueryCursor();   // вид курсора порта и скрытие Alt+Z возвращаются сразу
	EngagedWindow.Reset();
	Scale = 1.f;
	bEngaged = false;
	UE_LOG(LogAsdaOrbitCursor, Log, TEXT("[курсор ПКМ] захват СНЯТ — %s"), Why);
}

// ★ 09.11.2026: вернуть вьюпорту игры захват мыши, отнятый посреди поворота (`ReleaseMouseCapture` режима ввода). Мир отдан модалке
//   (`IgnoreInput`) — не возвращаем: там ПКМ мира заперта (`asda.UI.ModalLockRightClick`). Фокус клавиатуры не трогается. true — захват есть.
static bool AsdaOrbitRecapture(FSlateApplication& App, const TCHAR* Why)
{
	using namespace AsdaOrbitCursorState;
	UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
	if (!VP || VP->IgnoreInput()) { return false; }
	const TSharedPtr<SViewport> Widget = VP->GetGameViewportWidget();
	if (!Widget.IsValid()) { return false; }
	FWidgetPath Path;
	App.GeneratePathToWidgetUnchecked(Widget.ToSharedRef(), Path);
	if (!Path.IsValid()) { return false; }
	App.ProcessExternalReply(Path, FReply::Handled().CaptureMouse(Widget.ToSharedRef()), App.GetUserIndexForMouse());
	const FSceneViewport* Scene = VP->GetGameViewport();
	const bool bOk = Scene && Scene->HasMouseCapture();
	if (bOk && bEngaged)
	{
		// Ответ со снятием захвата выключил и сырой режим, и захват ОС (`ProcessReply`) — ставим обратно, курсор снова скрыт.
		const TSharedPtr<FGenericWindow> Native = AsdaOrbitNativeWindow();
		const TSharedPtr<GenericApplication> Platform = App.GetPlatformApplication();
		if (Platform.IsValid() && !Platform->IsUsingHighPrecisionMouseMode())
		{
			Platform->SetCapture(Native);
			Platform->SetHighPrecisionMouseMode(true, Native);
		}
		if (const TSharedPtr<ICursor> Cursor = App.GetPlatformCursor()) { Cursor->Show(false); }
		EngagedWindow = Native;
	}
	if (bOk) { ++RecaptureCount; bRecaptureFailLogged = false; }
	if (bOk || !bRecaptureFailLogged)   // неудача пишется один раз, а не каждый кадр (ревизия 09.11, п. 5)
	{
		UE_LOG(LogAsdaOrbitCursor, Log, TEXT("[курсор ПКМ] захват вьюпорта отнят посреди поворота (%s) — %s"), Why, bOk ? TEXT("возвращён") : TEXT("вернуть не удалось"));
		bRecaptureFailLogged = !bOk;
	}
	return bOk;
}

// Один шаг сторожа.
static void AsdaOrbitStep()
{
	using namespace AsdaOrbitCursorState;
	if (!FSlateApplication::IsInitialized()) { return; }
	FSlateApplication& App = FSlateApplication::Get();
	FAsdaOrbitCursor::FInputs In;
	In.bKeyOn = CVarAsdaOrbitCursorLock.GetValueOnGameThread() != 0;
	In.bOrbitReported = LastHeldFrame != 0 && GFrameCounter - LastHeldFrame <= kReportFreshFrames;
	In.bButtonDown = App.GetPressedMouseButtons().Contains(EKeys::RightMouseButton);
	const FAsdaCursorClip::FInputs Win = FAsdaCursorClip::GatherInputs();
	In.bHasWindow = Win.bHasWindow;
	In.bAppActive = Win.bAppActive;
	In.bForeground = Win.bForeground;
	In.bMinimized = Win.bMinimized;
#if !UE_BUILD_SHIPPING
	if (bOverrideWindow) { In.bHasWindow = true; In.bAppActive = bOverrideForeground; In.bForeground = bOverrideForeground; In.bMinimized = false; }
#endif
	const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
	const FSceneViewport* Scene = VP ? VP->GetGameViewport() : nullptr;
	In.bViewportCaptured = Scene && Scene->HasMouseCapture();
	// ★ 09.11.2026: журнал смены захвата вьюпорта, пока мышь крутит камеру (кто и когда отнял захват — по соседним строкам журнала).
	{
		static bool bPrevCaptured = false;
		if (In.bOrbitReported && bPrevCaptured != In.bViewportCaptured)
		{
			UE_LOG(LogAsdaOrbitCursor, Log, TEXT("[курсор ПКМ] захват вьюпорта %s посреди поворота (кадр %llu)"), In.bViewportCaptured ? TEXT("есть") : TEXT("ОТНЯТ"), (unsigned long long)GFrameCounter);
		}
		bPrevCaptured = In.bViewportCaptured;
	}
	// ★ 09.11.2026 (`asda.Camera.OrbitHoldThrough`): поворот идёт, а захват отнят пересчётом режима ввода — вернуть до решения.
	if (!In.bViewportCaptured && In.bKeyOn && In.bOrbitReported && In.bButtonDown && In.bHasWindow && In.bAppActive && In.bForeground
		&& !In.bMinimized && CVarAsdaOrbitHoldThrough.GetValueOnGameThread() != 0)
	{
		In.bViewportCaptured = AsdaOrbitRecapture(App, bEngaged ? TEXT("захват стоял") : TEXT("до захвата"));
	}

	// Фронт поворота: запомнить точку и время. Захват ещё не стоит, поэтому курсор здесь — там, где игрок нажал (с точностью до кадра).
	const double Now = FPlatformTime::Seconds();
	const FVector2D Cur = App.GetCursorPos();
	if (In.bOrbitReported && !bWasReported) { Anchor = Cur; HeldSinceS = Now; }
	bWasReported = In.bOrbitReported;
	In.HeldSec = In.bOrbitReported ? (float)(Now - HeldSinceS) : 0.f;
	In.MovedPx = In.bOrbitReported ? (float)FVector2D::Distance(Cur, Anchor) : 0.f;

	FString Why;
	const bool bWant = FAsdaOrbitCursor::Decide(In, bEngaged, Why);
	if (bWant && !bEngaged) { AsdaOrbitEngage(App, *Why); }
	else if (!bWant && bEngaged) { AsdaOrbitRelease(App, In.bForeground && In.bAppActive, *Why); }
	else if (bEngaged) { App.SetCursorPos(Anchor); }
}

void FAsdaOrbitCursor::Startup()
{
	using namespace AsdaOrbitCursorState;
	if (bStarted || GIsEditor || IsRunningCommandlet()) { return; }
	bStarted = true;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic([](float) { AsdaOrbitStep(); return true; }), 0.f);
}

void FAsdaOrbitCursor::Shutdown()
{
	using namespace AsdaOrbitCursorState;
	if (!bStarted) { return; }
	bStarted = false;
	FTSTicker::RemoveTicker(TickHandle);
	if (bEngaged && FSlateApplication::IsInitialized()) { AsdaOrbitRelease(FSlateApplication::Get(), true, TEXT("выход")); }
}

void FAsdaOrbitCursor::ReportOrbit(bool bHeld)
{
	if (bHeld) { AsdaOrbitCursorState::LastHeldFrame = GFrameCounter; }
}

bool FAsdaOrbitCursor::HoldThroughOn() { return CVarAsdaOrbitHoldThrough.GetValueOnGameThread() != 0; }

void FAsdaOrbitCursor::NoteFlush(bool bOrbitHeld)
{
	AsdaOrbitCursorState::FlushFrame = (bOrbitHeld && HoldThroughOn()) ? FMath::Max<uint64>(GFrameCounter, 1) : 0;
}

bool FAsdaOrbitCursor::TakeRearm()
{
	using namespace AsdaOrbitCursorState;
	if (FlushFrame == 0) { return false; }
	const bool bFresh = GFrameCounter - FlushFrame <= kRearmFreshFrames;
	FlushFrame = 0;
	if (!bFresh || !HoldThroughOn() || CVarAsdaOrbitCursorLock.GetValueOnGameThread() == 0 || !FSlateApplication::IsInitialized()) { return false; }
	FSlateApplication& App = FSlateApplication::Get();
	if (!App.GetPressedMouseButtons().Contains(EKeys::RightMouseButton)) { return false; }   // кнопку и правда отпустили
	FAsdaCursorClip::FInputs Win = FAsdaCursorClip::GatherInputs();
#if !UE_BUILD_SHIPPING
	if (bOverrideWindow) { Win.bHasWindow = true; Win.bAppActive = bOverrideForeground; Win.bForeground = bOverrideForeground; Win.bMinimized = false; }
#endif
	if (!Win.bHasWindow || !Win.bAppActive || !Win.bForeground || Win.bMinimized) { return false; }   // Alt+Tab, сворачивание
	UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
	if (!VP || VP->IgnoreInput()) { return false; }   // мир отдан модалке — ПКМ мира заперта
	++RearmCount;
	UE_LOG(LogAsdaOrbitCursor, Log, TEXT("[курсор ПКМ] сброс клавиш посреди поворота (фокус ушёл виджету), ПКМ зажата — поворот продолжается"));
	return true;
}

int32 FAsdaOrbitCursor::GetRecaptureCount() { return AsdaOrbitCursorState::RecaptureCount; }
int32 FAsdaOrbitCursor::GetRearmCount() { return AsdaOrbitCursorState::RearmCount; }
bool FAsdaOrbitCursor::IsOrbitReported()
{
	using namespace AsdaOrbitCursorState;
	return LastHeldFrame != 0 && GFrameCounter - LastHeldFrame <= kReportFreshFrames;
}

float FAsdaOrbitCursor::DeltaScale() { return AsdaOrbitCursorState::bEngaged ? AsdaOrbitCursorState::Scale : 1.f; }
bool FAsdaOrbitCursor::IsEngaged() { return AsdaOrbitCursorState::bEngaged; }
FVector2D FAsdaOrbitCursor::GetAnchor() { return AsdaOrbitCursorState::Anchor; }
int32 FAsdaOrbitCursor::GetEngageCount() { return AsdaOrbitCursorState::EngageCount; }

#if !UE_BUILD_SHIPPING
void FAsdaOrbitCursor::DebugOverrideWindow(bool bOverride, bool bForeground)
{
	AsdaOrbitCursorState::bOverrideWindow = bOverride;
	AsdaOrbitCursorState::bOverrideForeground = bForeground;
}
#endif
