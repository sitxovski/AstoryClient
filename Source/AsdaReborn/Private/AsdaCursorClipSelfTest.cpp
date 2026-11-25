// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КУРСОР НЕ ПОКИДАЕТ ОКНО» (01.11.2026): `asda.SelfTest.CursorClip [quit]`. Офлайн (`-AsdaNoNet`), любой экран.
//   Мышь владельца НЕ зажимается: логика стража гоняется чистыми функциями `FAsdaCursorClip::Decide/Plan/Commit`, а живой страж
//   сверяется по состоянию (окно ОС, фокус, режим лока вьюпорта, факт зажима).
//   Проверяется:
//     A) решение по входам: ключ, редактор, нет окна, неактивно, не передний план (Alt+Tab, системный диалог), свёрнуто, пустая
//        область — зажима нет; всё в порядке — держим; область: окно с рамкой — всё окно (заголовок, крестик), безрамочное и
//        полный экран — клиентская, ключ 2 — клиентская всегда;
//     B) план по кадрам: поставить → держится без повторов → перенос окна / смена разрешения → новый прямоугольник → движок снял
//        лок (`SetInputMode` DoNotLock) → снова зажим → потеря фокуса / сворачивание / ключ 0 → снят → возврат фокуса → снова;
//     C) живой страж: запущен, сторож-поток (снятие при модальном диалоге / стоящем игровом потоке) жив; без окна ОС (`-RenderOffscreen`) зажима нет и режим лока вьюпорта не тронут; с окном — зажим
//        ровно тогда, когда его требует решение.
//     F) F11 (05.11.2026, [ИСПРАВЛЕНИЕ] владельца): ключ `asda.Input.F11NoFullscreen 1` — движок не переключает режим окна:
//        `TryToggleFullscreenOnInputKey(F11)` = «не моё», настоящее нажатие F11 через `FSlateApplication` режим окна не меняет;
//        ключ 0 — флаг движка `bF11TogglesFullscreen` возвращён (само переключение не зовём — оно сменило бы окно прогона).
//   Итог: «ЗАЖИМ-КУРСОРА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "World/AsdaCursorClip.h"
#include "Test/AsdaSelfTestKit.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCursorClipTest, Log, All);

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCursorClip(
	TEXT("asda.SelfTest.CursorClip"),
	TEXT("Самотест стража «курсор не покидает окно игры»: решение и план по кадрам (фокус, сворачивание, перенос окна, ключ), "
	     "живое состояние без зажима мыши владельца. Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.SelfTest.CursorClip");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaCursorClipTest, Log,   TEXT("ЗАЖИМ-КУРСОРА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCursorClipTest, Error, TEXT("ЗАЖИМ-КУРСОРА FAIL %s"), *What); }
		};
		using FC = FAsdaCursorClip;

		// Исправное окно игры на переднем плане (оконный режим 1024×768 на втором мониторе — координаты могут быть любыми).
		FC::FInputs Good;
		Good.bKeyOn = true; Good.bEditor = false; Good.bHasWindow = true; Good.bAppActive = true; Good.bForeground = true;
		Good.bMinimized = false; Good.Client = FIntRect(1928, 31, 2952, 799);

		// ═══ A. Решение ═══
		{
			FString Why;
			Check(FC::Decide(Good, Why), FString::Printf(TEXT("A1 окно на переднем плане — держим (%s)"), *Why));
			auto No = [&](FC::FInputs In, const TCHAR* Name)
			{
				FString Y;
				const bool b = FC::Decide(In, Y);
				Check(!b, FString::Printf(TEXT("A %s — зажима нет (%s)"), Name, *Y));
			};
			FC::FInputs In = Good; In.bKeyOn = false;      No(In, TEXT("ключ asda.Input.ConfineCursor 0 (прежнее)"));
			In = Good; In.bEditor = true;                  No(In, TEXT("редактор/PIE"));
			In = Good; In.bHasWindow = false;              No(In, TEXT("нет окна ОС (-RenderOffscreen)"));
			In = Good; In.bAppActive = false;              No(In, TEXT("приложение неактивно (Alt+Tab)"));
			In = Good; In.bForeground = false;             No(In, TEXT("на переднем плане чужое окно / системный диалог"));
			In = Good; In.bMinimized = true;               No(In, TEXT("окно свёрнуто"));
			In = Good; In.Client = FIntRect(0, 0, 0, 0);   No(In, TEXT("пустая клиентская область"));
			// Область зажима по режиму окна.
			FC::FInputs Fr = Good; Fr.bFramed = true; Fr.Frame = FIntRect(1920, 0, 2960, 807);
			Check(FC::Decide(Fr, Why) && FC::TargetRect(Fr) == Fr.Frame,
				FString::Printf(TEXT("A9 оконный с рамкой ОС — зажим по всему окну (заголовок, рамка, крестик доступны): %s"), *Why));
			Check(FC::TargetRect(Good) == Good.Client, TEXT("A10 безрамочное / полный экран — зажим по клиентской области"));
			FC::FInputs Fr2 = Fr; Fr2.bClientOnly = true;
			Check(FC::TargetRect(Fr2) == Fr2.Client, TEXT("A11 ключ 2 — клиентская область и в окне с рамкой"));
			FC::FInputs Fr3 = Fr; Fr3.Frame = FIntRect();
			Check(FC::TargetRect(Fr3) == Fr3.Client, TEXT("A12 рамка не прочиталась — клиентская область"));
		}

		// ═══ B. План по кадрам ═══
		{
			FC::FState St;
			FIntRect R; FString Why;
			const FIntRect Desk(-1920, 0, 3840, 1080);   // «зажима нет» — ОС отдаёт весь рабочий стол
			// Кадр стража: план → исполнение (здесь — фактический зажим ОС = поставленному прямоугольнику) → коммит.
			FIntRect Actual = Desk;
			auto Frame = [&](const FC::FInputs& In) -> FC::EAction
			{
				const FC::EAction A = FC::Plan(In, St, Actual, true, R, Why);
				if (A == FC::EAction::Lock) { Actual = R; }
				else if (A == FC::EAction::Unlock) { Actual = Desk; }
				FC::Commit(St, A, R);
				return A;
			};
			Check(Frame(Good) == FC::EAction::Lock && St.bApplied && St.Rect == Good.Client, TEXT("B1 первый кадр — зажим по клиентской области"));
			Check(Frame(Good) == FC::EAction::None && St.bApplied, TEXT("B2 следующий кадр — без повторного зажима"));
			FC::FInputs Moved = Good; Moved.Client = Good.Client + FIntPoint(-300, 120);
			Check(Frame(Moved) == FC::EAction::Lock && St.Rect == Moved.Client, TEXT("B3 окно перенесли — зажим по новой области"));
			FC::FInputs Full = Good; Full.Client = FIntRect(0, 0, 1920, 1080);
			Check(Frame(Full) == FC::EAction::Lock && St.Rect == Full.Client, TEXT("B4 смена режима/разрешения (полный экран 1920×1080) — новый зажим"));
			Actual = Desk;   // движок снял лок (`SetInputMode` с DoNotLock → `UnlockCursor`)
			Check(Frame(Full) == FC::EAction::Lock && Actual == Full.Client, TEXT("B5 движок снял зажим — следующий кадр ставит снова"));
			FC::FInputs Away = Full; Away.bForeground = false; Away.bAppActive = false;
			Check(Frame(Away) == FC::EAction::Unlock && !St.bApplied && Actual == Desk, TEXT("B6 Alt+Tab — зажим снят"));
			Check(Frame(Away) == FC::EAction::None, TEXT("B7 без фокуса — повторно не снимается"));
			Check(Frame(Full) == FC::EAction::Lock && St.bApplied, TEXT("B8 фокус вернулся — зажим восстановлен"));
			FC::FInputs Mini = Full; Mini.bMinimized = true;
			Check(Frame(Mini) == FC::EAction::Unlock && !St.bApplied, TEXT("B9 окно свёрнуто — зажим снят"));
			Frame(Full);
			FC::FInputs Dlg = Full; Dlg.bForeground = false;   // системный диалог своего процесса: приложение активно, окно — нет
			Check(Frame(Dlg) == FC::EAction::Unlock, TEXT("B10 системный диалог поверх окна — зажим снят (диалогу не мешает)"));
			Frame(Full);
			FC::FInputs Off = Full; Off.bKeyOn = false;
			Check(Frame(Off) == FC::EAction::Unlock && Frame(Off) == FC::EAction::None && Actual == Desk,
				TEXT("B11 ключ 0 при зажиме — снят один раз, дальше курсор свободен (прежнее)"));
			FC::FInputs Framed = Full; Framed.bFramed = true; Framed.Frame = FIntRect(-8, -31, 1928, 1088);
			Check(Frame(Framed) == FC::EAction::Lock && St.Rect == Framed.Frame, TEXT("B13 переход в окно с рамкой — зажим по всему окну"));
			// Фактический зажим ОС не прочитался (`GetClipCursor` отказал) — страж не верит своему состоянию и ставит заново.
			FIntRect Clipped;
			const FC::EAction A1 = FC::Plan(Full, St, Clipped, false, R, Why);
			Check(A1 == FC::EAction::Lock, TEXT("B12 фактический зажим ОС не прочитался — ставится"));
		}

		// ═══ C. Живой страж ═══
		{
			const FC::FInputs In = FC::GatherInputs();
			FString Why;
			const bool bWant = FC::Decide(In, Why);
			const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
			Check(FC::IsStarted() && FC::IsWatchdogRunning(), FString::Printf(TEXT("C1 страж и сторож-поток запущены: %s"), *FC::Describe()));
			if (!In.bHasWindow)
			{
				Check(!FC::IsApplied() && (!VP || VP->GetMouseLockMode() != EMouseLockMode::LockAlways),
					TEXT("C2 без окна ОС (-RenderOffscreen): зажима нет, режим лока вьюпорта не тронут"));
			}
			else
			{
				Check(FC::IsApplied() == bWant && (!bWant || (VP && VP->GetMouseLockMode() == EMouseLockMode::LockAlways)),
					FString::Printf(TEXT("C2 окно ОС есть: зажим %d = решению %d (%s), режим лока вьюпорта LockAlways при зажиме"),
						FC::IsApplied() ? 1 : 0, bWant ? 1 : 0, *Why));
			}
		}

		// ═══ F. F11 без полного экрана ═══
		if (UGameViewportClient* VPc = GEngine ? GEngine->GameViewport.Get() : nullptr)
		{
			FC::ApplyFullscreenKeys();
			const EWindowMode::Type ModeWas = GEngine->GetGameUserSettings() ? GEngine->GetGameUserSettings()->GetFullscreenMode() : EWindowMode::Windowed;
			Check(!FC::IsF11FullscreenOn() && !VPc->TryToggleFullscreenOnInputKey(EKeys::F11, IE_Pressed),
				TEXT("F1 ключ 1: движок F11 не переключает (TryToggleFullscreenOnInputKey — не моё, клавиша идёт в игру)"));
			if (FSlateApplication::IsInitialized())
			{
				const FKeyEvent Dn(EKeys::F11, FModifierKeysState(), 0, false, 0, 0);
				FSlateApplication::Get().ProcessKeyDownEvent(Dn);
				const FKeyEvent Up(EKeys::F11, FModifierKeysState(), 0, false, 0, 0);
				FSlateApplication::Get().ProcessKeyUpEvent(Up);
			}
			const EWindowMode::Type ModeNow = GEngine->GetGameUserSettings() ? GEngine->GetGameUserSettings()->GetFullscreenMode() : EWindowMode::Windowed;
			Check(ModeNow == ModeWas, FString::Printf(TEXT("F2 настоящее нажатие F11 (Slate) — режим окна не сменился (%d → %d)"), (int32)ModeWas, (int32)ModeNow));
			{
				FAsdaCVarScope Keys(W, kCmd);
				Keys.Set(TEXT("asda.Input.F11NoFullscreen"), 0);
				FC::ApplyFullscreenKeys();
				Check(FC::IsF11FullscreenOn(), TEXT("F3 ключ 0 — прежнее: флаг движка bF11TogglesFullscreen возвращён"));
			}
			FC::ApplyFullscreenKeys();
			Check(!FC::IsF11FullscreenOn(), TEXT("F4 ключ возвращён в 1 — флаг снова снят"));
		}
		else { Check(false, TEXT("F0 нет вьюпорта игры")); }

		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaCursorClipTest, TEXT("ЗАЖИМ-КУРСОРА"), Ok, Fail);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
