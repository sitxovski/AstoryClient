// Copyright (c) 2026 @sitxovski (Discord)
// Реализация стража «курсор не покидает окно игры». Устройство и сверка с оригиналом — в шапке `Public/World/AsdaCursorClip.h`.
#include "World/AsdaCursorClip.h"

#include "AsdaDevConsole.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/InputSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"
#include "Widgets/SWindow.h"
#include <atomic>

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCursorClip, Log, All);

// ★ [ИСПРАВЛЕНИЕ] по слову владельца 01.11.2026: 1 — курсор зажат в окне игры, пока оно на переднем плане (полный экран и
//   безрамочное — клиентская область, окно с рамкой — всё окно с заголовком); 2 — клиентская область всегда; 0 — прежнее
//   (курсор свободно покидает окно, как у оригинала). В Shipping ключа нет (там 1).
static ASDA_DEV_CVAR(int32, CVarAsdaConfineCursor,
	TEXT("asda.Input.ConfineCursor"), 1,
	TEXT("1 — курсор мыши не покидает окно игры (полный экран и безрамочное — клиентская область, окно с рамкой — всё окно с "
	     "заголовком); 2 — клиентская область всегда; зажим снимается без фокуса и при сворачивании; 0 — прежнее (курсор свободен, "
	     "как у оригинала)."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] по слову владельца 05.11.2026: 1 — F11 полный экран не переключает (клавиша целиком игровая, команда 28
//   раскладки); 0 — прежнее (движок `bF11TogglesFullscreen`: F11 переключает режим окна). В Shipping ключа нет (там 1).
static ASDA_DEV_CVAR(int32, CVarAsdaF11NoFullscreen,
	TEXT("asda.Input.F11NoFullscreen"), 1,
	TEXT("1 — F11 не переключает полноэкранный режим (клавиша только игровая); 0 — прежнее (движок переключает режим по F11)."),
	ECVF_Default);

namespace AsdaCursorClipState
{
	static bool bStarted = false;                            // Startup выполнен (тикер и подписки живы)
	static FAsdaCursorClip::FState State;                    // наш зажим
	static FString LastWhy;                                  // причина последнего решения (журнал/самотест)
	static bool bLockModeOurs = false;                       // режим лока вьюпорта поставили мы
	static EMouseLockMode PrevLockMode = EMouseLockMode::DoNotLock;   // режим лока вьюпорта до нас
	static FTSTicker::FDelegateHandle TickHandle;            // тикер ядра
	static FDelegateHandle ActivationHandle;                 // подписка на активацию окна
	static FDelegateHandle PreExitHandle;                    // подписка на выход

	// ── Общее с потоком-сторожем (игровой поток пишет, сторож читает) ──
	static std::atomic<void*> WatchHwnd{ nullptr };          // окно игры, чей зажим стоит (nullptr — зажима нет)
	static FCriticalSection WatchLock;                       // защита WatchRect
	static FIntRect WatchRect;                               // прямоугольник нашего зажима

	// Сторож: раз в 100 мс снимает наш зажим, если окно игры не на переднем плане (игровой поток может стоять — модальный
	//   диалог, блокирующая загрузка, — и тик тогда не идёт).
	class FWatchdog : public FRunnable
	{
	public:
		std::atomic<bool> bStop{ false };                    // просьба остановиться
		// Цикл сторожа.
		virtual uint32 Run() override;
		// Остановить цикл.
		virtual void Stop() override { bStop = true; }
	};
	static FWatchdog* Watchdog = nullptr;                    // объект сторожа
	static FRunnableThread* WatchThread = nullptr;           // его поток

	// Сообщить сторожу: наш зажим стоит на окне Hwnd (nullptr — зажима нет) по прямоугольнику Rect.
	static void Publish(void* Hwnd, const FIntRect& Rect)
	{
		{ FScopeLock L(&WatchLock); WatchRect = Rect; }
		WatchHwnd = Hwnd;
	}
}

// Синхронно снять зажим ОС, если она держит ровно Rect (чужой зажим другой программы не трогаем). true — снят.
static bool AsdaClipReleaseIfOurs(const FIntRect& Rect)
{
#if PLATFORM_WINDOWS
	RECT A;
	if (::GetClipCursor(&A) && FIntRect(A.left, A.top, A.right, A.bottom) == Rect)
	{
		return ::ClipCursor(nullptr) != 0;
	}
#endif
	return false;
}

uint32 AsdaCursorClipState::FWatchdog::Run()
{
	while (!bStop)
	{
		FPlatformProcess::Sleep(0.1f);
#if PLATFORM_WINDOWS
		void* const H = WatchHwnd.load();
		if (H && ::GetForegroundWindow() != (HWND)H)
		{
			FIntRect R;
			{ FScopeLock L(&WatchLock); R = WatchRect; }
			// Игровой поток, когда оживёт, сам увидит потерю фокуса (снимет) или её возврат (поставит заново: ОС держит иное).
			if (AsdaClipReleaseIfOurs(R)) { WatchHwnd = nullptr; }
		}
#endif
	}
	return 0;
}

FIntRect FAsdaCursorClip::TargetRect(const FInputs& In)
{
	return (In.bFramed && !In.bClientOnly && In.Frame.Width() > 0 && In.Frame.Height() > 0) ? In.Frame : In.Client;
}

bool FAsdaCursorClip::Decide(const FInputs& In, FString& OutWhy)
{
	if (!In.bKeyOn)      { OutWhy = TEXT("ключ выключен"); return false; }
	if (In.bEditor)      { OutWhy = TEXT("редактор"); return false; }
	if (!In.bHasWindow)  { OutWhy = TEXT("нет окна ОС"); return false; }
	if (!In.bAppActive)  { OutWhy = TEXT("приложение неактивно"); return false; }
	if (!In.bForeground) { OutWhy = TEXT("окно игры не на переднем плане"); return false; }
	if (In.bMinimized)   { OutWhy = TEXT("окно свёрнуто"); return false; }
	const FIntRect T = TargetRect(In);
	if (T.Width() <= 0 || T.Height() <= 0) { OutWhy = TEXT("пустая клиентская область"); return false; }
	OutWhy = (T == In.Client) ? TEXT("держим: клиентская область") : TEXT("держим: всё окно с рамкой");
	return true;
}

FAsdaCursorClip::EAction FAsdaCursorClip::Plan(const FInputs& In, const FState& St, const FIntRect& Actual, bool bActualValid,
	FIntRect& OutRect, FString& OutWhy)
{
	if (Decide(In, OutWhy))
	{
		OutRect = TargetRect(In);
		// Ставим, если не стоит, область сменилась (режим/разрешение/перенос) или ОС держит иное (движок снял лок `SetInputMode`,
		//   сторож снял на время модального диалога).
		const bool bNeed = !St.bApplied || St.Rect != OutRect || !bActualValid || Actual != OutRect;
		return bNeed ? EAction::Lock : EAction::None;
	}
	OutRect = FIntRect();
	return St.bApplied ? EAction::Unlock : EAction::None;
}

void FAsdaCursorClip::Commit(FState& St, EAction A, const FIntRect& Rect)
{
	if (A == EAction::Lock)        { St.bApplied = true;  St.Rect = Rect; }
	else if (A == EAction::Unlock) { St.bApplied = false; St.Rect = FIntRect(); }
}

// Вьюпорт игры (нет — пусто).
static UGameViewportClient* AsdaClipViewport()
{
	return GEngine ? GEngine->GameViewport.Get() : nullptr;
}

// Хэндл ОС окна игры (нет — nullptr).
static void* AsdaClipHwnd()
{
	const UGameViewportClient* VP = AsdaClipViewport();
	const TSharedPtr<SWindow> W = VP ? VP->GetWindow() : nullptr;
	const TSharedPtr<FGenericWindow> Native = W.IsValid() ? W->GetNativeWindow() : nullptr;
	return Native.IsValid() ? Native->GetOSWindowHandle() : nullptr;
}

FAsdaCursorClip::FInputs FAsdaCursorClip::GatherInputs()
{
	FInputs In;
	const int32 Key = CVarAsdaConfineCursor.GetValueOnGameThread();
	In.bKeyOn = Key != 0;
	In.bClientOnly = Key == 2;
	In.bEditor = GIsEditor;
	if (!FSlateApplication::IsInitialized()) { return In; }
	In.bAppActive = FSlateApplication::Get().IsActive();
	const UGameViewportClient* VP = AsdaClipViewport();
	const TSharedPtr<SWindow> W = VP ? VP->GetWindow() : nullptr;
	const TSharedPtr<FGenericWindow> Native = W.IsValid() ? W->GetNativeWindow() : nullptr;
	void* const Handle = Native.IsValid() ? Native->GetOSWindowHandle() : nullptr;
	In.bHasWindow = Handle != nullptr;
	// Оконный режим с рамкой ОС: заголовок и крестик рисует Windows (у безрамочного окна UE их нет).
	In.bFramed = Native.IsValid() && Native->GetWindowMode() == EWindowMode::Windowed && W->HasOSWindowBorder();
#if PLATFORM_WINDOWS
	if (Handle)
	{
		const HWND H = (HWND)Handle;
		In.bForeground = ::GetForegroundWindow() == H;
		In.bMinimized = ::IsIconic(H) != 0;
		RECT R;
		POINT TL{ 0, 0 };
		if (::GetClientRect(H, &R) && ::ClientToScreen(H, &TL))
		{
			In.Client = FIntRect(TL.x, TL.y, TL.x + (R.right - R.left), TL.y + (R.bottom - R.top));
		}
		RECT F;
		if (::GetWindowRect(H, &F)) { In.Frame = FIntRect(F.left, F.top, F.right, F.bottom); }   // с невидимой полосой размера
	}
#endif
	return In;
}

// Режим лока вьюпорта: на время зажима — `LockAlways` (иначе отпускание кнопки снимает зажим), без зажима — прежний.
static void AsdaClipSetViewportLock(bool bHold)
{
	using namespace AsdaCursorClipState;
	UGameViewportClient* VP = AsdaClipViewport();
	if (!VP) { bLockModeOurs = false; return; }
	if (bHold)
	{
		if (VP->GetMouseLockMode() == EMouseLockMode::LockAlways) { return; }
		if (!bLockModeOurs) { PrevLockMode = VP->GetMouseLockMode(); }   // `SetInputMode` порта мог вернуть DoNotLock — прежний не перетираем
		VP->SetMouseLockMode(EMouseLockMode::LockAlways);
		bLockModeOurs = true;
	}
	else if (bLockModeOurs)
	{
		if (VP->GetMouseLockMode() == EMouseLockMode::LockAlways) { VP->SetMouseLockMode(PrevLockMode); }
		bLockModeOurs = false;
	}
}

// Один шаг стража: собрать входы, спланировать, исполнить. Src — источник шага для журнала.
static void AsdaClipStep(const TCHAR* Src)
{
	using namespace AsdaCursorClipState;
	FAsdaCursorClip::ApplyFullscreenKeys();
	if (!FSlateApplication::IsInitialized()) { return; }
	const TSharedPtr<ICursor> Cursor = FSlateApplication::Get().GetPlatformCursor();
	if (!Cursor.IsValid()) { return; }
	const FAsdaCursorClip::FInputs In = FAsdaCursorClip::GatherInputs();
	FIntRect Actual;
	bool bActualValid = false;
#if PLATFORM_WINDOWS
	RECT A;
	if (::GetClipCursor(&A)) { Actual = FIntRect(A.left, A.top, A.right, A.bottom); bActualValid = true; }
#endif
	FIntRect Rect;
	FString Why;
	const FAsdaCursorClip::EAction Act = FAsdaCursorClip::Plan(In, State, Actual, bActualValid, Rect, Why);
	const bool bWasApplied = State.bApplied;
	const FIntRect WasRect = State.Rect;
	if (Act == FAsdaCursorClip::EAction::Lock)
	{
		AsdaClipSetViewportLock(true);
#if PLATFORM_WINDOWS
		const RECT R{ Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y };
		Cursor->Lock(&R);
#endif
		Publish(AsdaClipHwnd(), Rect);
	}
	else if (Act == FAsdaCursorClip::EAction::Unlock)
	{
		Publish(nullptr, FIntRect());
		Cursor->Lock(nullptr);
		AsdaClipSetViewportLock(false);
	}
	else if (State.bApplied)
	{
		AsdaClipSetViewportLock(true);   // `SetInputMode` порта (DoNotLock) мог сбросить режим, зажим ОС ещё цел
	}
	else if (!In.bKeyOn)
	{
		AsdaClipSetViewportLock(false);
	}
	FAsdaCursorClip::Commit(State, Act, Rect);
	if (State.bApplied != bWasApplied || (State.bApplied && State.Rect != WasRect))
	{
		UE_LOG(LogAsdaCursorClip, Log, TEXT("[курсор] зажим в окне %s: (%d,%d)-(%d,%d) — %s (%s)"),
			State.bApplied ? TEXT("ПОСТАВЛЕН") : TEXT("СНЯТ"), Rect.Min.X, Rect.Min.Y, Rect.Max.X, Rect.Max.Y, *Why, Src);
	}
	LastWhy = Why;
}

void FAsdaCursorClip::Startup()
{
	using namespace AsdaCursorClipState;
	if (bStarted || GIsEditor || IsRunningCommandlet()) { return; }
	bStarted = true;
	ApplyFullscreenKeys();   // до первого нажатия
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic([](float) { AsdaClipStep(TEXT("кадр")); return true; }), 0.f);
	if (FSlateApplication::IsInitialized())
	{
		// Фокус ушёл/вернулся — сразу, не дожидаясь кадра (Alt+Tab должен отпустить курсор немедленно).
		ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddStatic([](bool bActive)
		{
			AsdaClipStep(bActive ? TEXT("окно активировано") : TEXT("окно потеряло фокус"));
		});
	}
	// Выход: синхронно — `ICursor::Lock` асинхронный и может не успеть до закрытия процесса.
	PreExitHandle = FCoreDelegates::OnPreExit.AddStatic([]() { FAsdaCursorClip::ReleaseNow(TEXT("выход из игры")); });
	Watchdog = new FWatchdog();
	WatchThread = FRunnableThread::Create(Watchdog, TEXT("AsdaCursorClipWatch"), 0, TPri_BelowNormal);
	UE_LOG(LogAsdaCursorClip, Log, TEXT("[курсор] страж зажима курсора в окне запущен (сторож-поток %d)"), WatchThread ? 1 : 0);
}

void FAsdaCursorClip::Shutdown()
{
	using namespace AsdaCursorClipState;
	if (!bStarted) { return; }
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FCoreDelegates::OnPreExit.Remove(PreExitHandle);
	if (WatchThread) { WatchThread->Kill(true); delete WatchThread; WatchThread = nullptr; }   // Kill зовёт Stop и ждёт конца цикла
	delete Watchdog;
	Watchdog = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
		if (State.bApplied)
		{
			// Сбросить и запомненный Slate прямоугольник (иначе `FWindowsCursor::Tick` переклипует).
			if (const TSharedPtr<ICursor> Cursor = FSlateApplication::Get().GetPlatformCursor()) { Cursor->Lock(nullptr); }
		}
	}
	ReleaseNow(TEXT("завершение подсистемы"));
	AsdaClipSetViewportLock(false);
	State = FState();
	bStarted = false;
}

bool FAsdaCursorClip::IsStarted() { return AsdaCursorClipState::bStarted; }

bool FAsdaCursorClip::IsApplied() { return AsdaCursorClipState::State.bApplied; }

void FAsdaCursorClip::ApplyFullscreenKeys()
{
	UInputSettings* S = GetMutableDefault<UInputSettings>();
	const bool bWant = CVarAsdaF11NoFullscreen.GetValueOnGameThread() == 0;   // ключ 0 — прежнее: движок переключает по F11
	if (!S || (bool)S->bF11TogglesFullscreen == bWant) { return; }
	S->bF11TogglesFullscreen = bWant;
	UE_LOG(LogAsdaCursorClip, Log, TEXT("[клавиши] F11: переключение полного экрана движком %s"), bWant ? TEXT("ВКЛЮЧЕНО (прежнее)") : TEXT("СНЯТО"));
}

bool FAsdaCursorClip::IsF11FullscreenOn()
{
	const UInputSettings* S = GetDefault<UInputSettings>();
	return S && S->bF11TogglesFullscreen;
}

bool FAsdaCursorClip::IsWatchdogRunning() { return AsdaCursorClipState::WatchThread != nullptr; }

void FAsdaCursorClip::ReleaseNow(const TCHAR* Why)
{
	using namespace AsdaCursorClipState;
	if (!State.bApplied) { return; }
	const bool bOk = AsdaClipReleaseIfOurs(State.Rect);
	Publish(nullptr, FIntRect());
	UE_LOG(LogAsdaCursorClip, Log, TEXT("[курсор] зажим снят синхронно (%s)%s"), Why ? Why : TEXT(""), bOk ? TEXT("") : TEXT(" — ОС уже не держала наш"));
}

FString FAsdaCursorClip::Describe()
{
	using namespace AsdaCursorClipState;
	const UGameViewportClient* VP = AsdaClipViewport();
	const FInputs In = GatherInputs();
	return FString::Printf(TEXT("страж %d, сторож-поток %d, ключ %d, зажим %d (%d,%d)-(%d,%d), причина «%s», окно %d, рамка %d, "
	                            "активно %d, передний план %d, свёрнуто %d, режим лока вьюпорта %d (наш %d)"),
		bStarted ? 1 : 0, WatchThread ? 1 : 0, In.bKeyOn ? (In.bClientOnly ? 2 : 1) : 0, State.bApplied ? 1 : 0,
		State.Rect.Min.X, State.Rect.Min.Y, State.Rect.Max.X, State.Rect.Max.Y,
		*LastWhy, In.bHasWindow ? 1 : 0, In.bFramed ? 1 : 0, In.bAppActive ? 1 : 0, In.bForeground ? 1 : 0, In.bMinimized ? 1 : 0,
		VP ? (int32)VP->GetMouseLockMode() : -1, bLockModeOurs ? 1 : 0);
}
