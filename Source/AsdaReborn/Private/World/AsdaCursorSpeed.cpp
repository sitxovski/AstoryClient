// Copyright (c) 2026 @sitxovski (Discord)
// Реализация FAsdaCursorSpeed — см. AsdaCursorSpeed.h (★ 19.10.2026, строка I2 окна настроек 69500).
#include "World/AsdaCursorSpeed.h"

#include "Resources/AsdaGameUserSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/SWindow.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCursorSpeed, Log, All);

namespace AsdaCursorSpeedState
{
	static bool bStarted = false;          // Startup выполнен (подписки живы)
	static int32 OrigLevel = 0;            // уровень Windows игрока до нашей смены; 0 — мы ничего не меняли
	static int32 AppliedLevel = 0;         // уровень, который поставили мы (0 — не ставили)
	static FDelegateHandle ActivationHandle;
	static FDelegateHandle PreExitHandle;
	static FDelegateHandle SystemErrorHandle;
}

// Таблица множителей Windows (уровни 1…20 «Параметров указателя»; 10 = ×1 — «без ускорения» по умолчанию).
float FAsdaCursorSpeed::LevelMultiplier(int32 Level)
{
	static const float kMul[20] = { 1.f / 32.f, 1.f / 16.f, 1.f / 8.f, 2.f / 8.f, 3.f / 8.f, 4.f / 8.f, 5.f / 8.f, 6.f / 8.f, 7.f / 8.f, 1.f,
	                                1.25f, 1.5f, 1.75f, 2.f, 2.25f, 2.5f, 2.75f, 3.f, 3.25f, 3.5f };
	return kMul[FMath::Clamp(Level, 1, 20) - 1];
}

int32 FAsdaCursorSpeed::LevelForPercent(int32 BaseLevel, int32 Percent)
{
	const float Want = LevelMultiplier(BaseLevel) * (float)FMath::Max(Percent, 1) / 100.f;
	int32 Best = FMath::Clamp(BaseLevel, 1, 20);
	float BestErr = TNumericLimits<float>::Max();
	for (int32 L = 1; L <= 20; ++L)
	{
		const float Err = FMath::Abs(FMath::Loge(LevelMultiplier(L) / Want));   // по отношению: шкала Windows неравномерна
		if (Err < BestErr - KINDA_SMALL_NUMBER) { BestErr = Err; Best = L; }
	}
	return Best;
}

#if PLATFORM_WINDOWS
// Текущий уровень Windows (1…20); 0 — не прочитался.
static int32 AsdaReadWinMouseSpeed()
{
	int Speed = 0;
	return ::SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &Speed, 0) ? (int32)Speed : 0;
}
// Поставить уровень только на сеанс Windows (без записи в реестр).
static bool AsdaWriteWinMouseSpeed(int32 Level)
{
	return ::SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (void*)(intptr_t)FMath::Clamp(Level, 1, 20), 0) != 0;
}
#endif

// Окно игры существует и приложение активно — только тогда системная скорость «наша».
static bool AsdaGameWindowActive()
{
	if (GIsEditor || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive()) { return false; }
	if (!GEngine || !GEngine->GameViewport) { return false; }
	const TSharedPtr<SWindow> W = GEngine->GameViewport->GetWindow();
	const TSharedPtr<FGenericWindow> Native = W.IsValid() ? W->GetNativeWindow() : nullptr;
	return Native.IsValid() && Native->GetOSWindowHandle() != nullptr;
}

// Метка «долга» в профиле: уровень, который надо вернуть; 0 — стереть. bSave — сохранить профиль на диск.
static void AsdaSetRestoreMark(int32 Level, bool bSave)
{
	UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
	if (!P || P->CursorSpeedRestoreLevel == Level) { return; }
	P->CursorSpeedRestoreLevel = Level;
	if (bSave) { P->SaveSettings(); }
}

// Вернуть скорость игрока. bWriteProfile = false — путь падения: на диск не пишем (метка останется, следующий запуск
//   вернёт ту же скорость повторно — безвредно).
static void AsdaRestoreCursorSpeed(bool bWriteProfile, const TCHAR* Why)
{
	using namespace AsdaCursorSpeedState;
	if (OrigLevel <= 0) { return; }
#if PLATFORM_WINDOWS
	const bool bOk = AsdaWriteWinMouseSpeed(OrigLevel);
	UE_LOG(LogAsdaCursorSpeed, Log, TEXT("[мышь] скорость указателя Windows возвращена: %d → %d (%s)%s"), AppliedLevel, OrigLevel, Why,
		bOk ? TEXT("") : TEXT(" — ОШИБКА SystemParametersInfo"));
#endif
	OrigLevel = 0;
	AppliedLevel = 0;
	if (bWriteProfile) { AsdaSetRestoreMark(0, true); }
}

// Поставить скорость по проценту профиля (окно игры активно).
static void AsdaApplyCursorSpeed(int32 Percent)
{
	using namespace AsdaCursorSpeedState;
#if PLATFORM_WINDOWS
	const int32 Cur = AsdaReadWinMouseSpeed();
	if (Cur <= 0) { UE_LOG(LogAsdaCursorSpeed, Warning, TEXT("[мышь] скорость указателя Windows не прочиталась — не трогаю")); return; }
	// Своей смены нет — текущая скорость и есть скорость игрока. Наша есть, но Windows показывает другую — игрок сменил её
	//   в «Параметрах указателя» поверх нашей: принимаем её как новую скорость игрока.
	if (OrigLevel <= 0 || Cur != AppliedLevel)
	{
		OrigLevel = Cur;
		AsdaSetRestoreMark(Cur, true);   // долг пишется ДО смены: падение сразу после неё не оставит игрока без скорости
	}
	const int32 Want = FAsdaCursorSpeed::LevelForPercent(OrigLevel, Percent);
	if (Want != Cur && !AsdaWriteWinMouseSpeed(Want))
	{
		UE_LOG(LogAsdaCursorSpeed, Warning, TEXT("[мышь] SystemParametersInfo(SPI_SETMOUSESPEED, %d) не прошёл"), Want);
		return;
	}
	if (AppliedLevel != Want)
	{
		UE_LOG(LogAsdaCursorSpeed, Log, TEXT("[мышь] чувствительность %d %%: уровень Windows %d (×%.3f) → %d (×%.3f) на время игры"),
			Percent, OrigLevel, FAsdaCursorSpeed::LevelMultiplier(OrigLevel), Want, FAsdaCursorSpeed::LevelMultiplier(Want));
	}
	AppliedLevel = Want;
#endif
}

void FAsdaCursorSpeed::Refresh()
{
	const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
	const int32 Percent = P ? FMath::Clamp(P->CursorSpeedPercent, UAsdaGameUserSettings::kMouseSensMin, UAsdaGameUserSettings::kMouseSensMax)
	                        : UAsdaGameUserSettings::kMouseSensDefault;
	if (Percent == UAsdaGameUserSettings::kMouseSensDefault || !AsdaGameWindowActive())
	{
		AsdaRestoreCursorSpeed(true, Percent == UAsdaGameUserSettings::kMouseSensDefault ? TEXT("100 %") : TEXT("окно игры неактивно"));
		return;
	}
	AsdaApplyCursorSpeed(Percent);
}

void FAsdaCursorSpeed::Startup()
{
	using namespace AsdaCursorSpeedState;
	if (bStarted || GIsEditor || IsRunningCommandlet()) { return; }
	bStarted = true;
#if PLATFORM_WINDOWS
	// Долг прошлого запуска (процесс убит без обработчиков): вернуть скорость игрока.
	if (UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get())
	{
		if (P->CursorSpeedRestoreLevel > 0)
		{
			const bool bOk = AsdaWriteWinMouseSpeed(P->CursorSpeedRestoreLevel);
			UE_LOG(LogAsdaCursorSpeed, Log, TEXT("[мышь] прошлый запуск не вернул скорость указателя — возвращаю уровень %d%s"),
				P->CursorSpeedRestoreLevel, bOk ? TEXT("") : TEXT(" — ОШИБКА SystemParametersInfo"));
			AsdaSetRestoreMark(0, true);
		}
	}
#endif
	if (FSlateApplication::IsInitialized())
	{
		ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddStatic([](bool bActive)
		{
			if (bActive) { FAsdaCursorSpeed::Refresh(); }
			else         { AsdaRestoreCursorSpeed(true, TEXT("окно игры потеряло фокус")); }
		});
	}
	PreExitHandle = FCoreDelegates::OnPreExit.AddStatic([]() { AsdaRestoreCursorSpeed(true, TEXT("выход из игры")); });
	SystemErrorHandle = FCoreDelegates::OnHandleSystemError.AddStatic([]() { AsdaRestoreCursorSpeed(false, TEXT("падение процесса")); });
	Refresh();
}

void FAsdaCursorSpeed::Shutdown()
{
	using namespace AsdaCursorSpeedState;
	if (!bStarted) { return; }
	AsdaRestoreCursorSpeed(true, TEXT("завершение подсистемы"));
	if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle); }
	FCoreDelegates::OnPreExit.Remove(PreExitHandle);
	FCoreDelegates::OnHandleSystemError.Remove(SystemErrorHandle);
	bStarted = false;
}

int32 FAsdaCursorSpeed::ReadWindowsLevel()
{
#if PLATFORM_WINDOWS
	return AsdaReadWinMouseSpeed();
#else
	return 0;
#endif
}

FString FAsdaCursorSpeed::Describe()
{
	using namespace AsdaCursorSpeedState;
	const UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get();
	int32 Cur = 0;
#if PLATFORM_WINDOWS
	Cur = AsdaReadWinMouseSpeed();
#endif
	return FString::Printf(TEXT("мышь %d %%, уровень Windows сейчас %d, игрока %d, наш %d, окно игры активно %d, метка долга %d, подписки %d"),
		P ? P->CursorSpeedPercent : -1, Cur, OrigLevel, AppliedLevel, AsdaGameWindowActive() ? 1 : 0,
		P ? P->CursorSpeedRestoreLevel : -1, bStarted ? 1 : 0);
}
