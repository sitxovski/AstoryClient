// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ОКНО ИДЁТ ЗА КУРСОРОМ» — `asda.UI.SelfTestDragFollow [delay=N] [quit]` (09.11.2026, пачка 20, баг 5: окно при быстром
//   перетаскивании отстаёт от курсора и догоняет с задержкой).
//   Путь игрока: нажатие на ручке, рывки курсора настоящими событиями Slate (`ProcessMouseMoveEvent`) каждый кадр, отпускание. Замер — в начале
//   КАЖДОГО кадра (тикер): рамка ручки из `GetCachedGeometry` прошлой отрисовки против «рамка до захвата + (курсор − точка захвата)». Расхождение
//   в пикселях пишется по кадрам; итог — наибольшее расхождение. Отставание на кадр видно как расхождение, равное прошлому рывку.
//   Окна: настройки 69500 (растянутое на экран), сумка 400, сообщество 6400 (составное). Без сети.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDragFollow, Log, All);   // журнал самотеста

namespace AsdaDragFollowLocal
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestDragFollow");   // имя команды
	// Рывки курсора относительно точки захвата, по кадру на шаг (быстрые, в обе стороны).
	static const FVector2D kJerks[] = {
		{ 160, 0 }, { 320, 40 }, { 120, 140 }, { -60, 220 }, { 220, 60 }, { 380, -20 }, { 40, 100 }, { 260, 200 }, { 100, -40 }, { 300, 120 },
		{ -40, 30 }, { 180, 180 }, { 340, 90 }, { 60, 10 }, { 0, 0 } };

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;      // мир
		bool bQuit = false;            // выйти после итога
		int32 Ok = 0, Fail = 0;        // счётчики
		TArray<FName> Wins;            // окна
		int32 WinIdx = 0;              // текущее окно
		FVector2D Grab = FVector2D::ZeroVector;      // точка захвата
		FVector2D H0 = FVector2D::ZeroVector;        // левый верх ручки до захвата
		int32 Step = -1;               // шаг рывков (−1 — ещё не нажато)
		double MaxErr = 0.0;           // наибольшее расхождение по окну
		double MaxAll = 0.0;           // по всем окнам
		FTSTicker::FDelegateHandle Post;   // покадровый тикер замера
		FVector2D LastCur = FVector2D::ZeroVector;   // курсор, выставленный в прошлом кадре
		FVector2D DrawCur = FVector2D::ZeroVector;   // курсор, который видел `TickDrag` прошлого кадра (он идёт раньше этого тикера)
		int32 Handle = 0;              // индекс ручки окна
		int32 LagOn = 0;               // кадров с r.OneFrameThreadLag 0 во время рывков

		// Проверка: счётчик и строка журнала.
		void Expect(bool b, const FString& M)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaDragFollow, Display, TEXT("[за курсором] %s %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *M);
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		UAsdaWindowSubsystem* WM() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWindowSubsystem>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Следующий этап через Sec секунд.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что проверка мира записана `if (W) { … }` вместо раннего выхода.

	// Настоящее движение мыши.
	static void Move(const FVector2D& P, bool bHeld)
	{
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		TSet<FKey> Keys; if (bHeld) { Keys.Add(EKeys::LeftMouseButton); }
		App.ProcessMouseMoveEvent(FPointerEvent(0, P, App.GetLastCursorPos(), Keys, EKeys::Invalid, 0.f, FModifierKeysState()));
	}
	// Настоящее нажатие/отпускание левой кнопки.
	static void Btn(const FVector2D& P, bool bDown)
	{
		FSlateApplication& App = FSlateApplication::Get();
		TSet<FKey> Keys; if (bDown) { Keys.Add(EKeys::LeftMouseButton); }
		const FPointerEvent E(0, P, P, Keys, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (bDown) { App.ProcessMouseButtonDownEvent(nullptr, E); } else { App.ProcessMouseButtonUpEvent(E); }
	}

	// Открыть/закрыть окно штатной подсистемой.
	static void ShowWin(FCtxRef C, FName Id, bool bOpen)
	{
		UGameInstance* GI = C->GI();
		if (!GI) { return; }
		if (Id == FName(TEXT("SYSTEM_Option_NEW")))
		{
			UAsdaSystemOptionSubsystem* S = GI->GetSubsystem<UAsdaSystemOptionSubsystem>();
			if (bOpen && !S->IsOptionsShown()) { S->ShowOptions(); } else if (!bOpen && S->IsOptionsShown()) { S->CloseOptions(false); }
		}
		else if (Id == FName(TEXT("INVEN_WIN")))
		{
			UAsdaInventorySubsystem* S = GI->GetSubsystem<UAsdaInventorySubsystem>();
			if (bOpen && !S->IsWindowOpen()) { S->OpenWindow(); } else if (!bOpen && S->IsWindowOpen()) { S->CloseWindow(); }
		}
		else if (Id == FName(TEXT("COMMUNITY_WIN")))
		{
			UAsdaCommunitySubsystem* S = GI->GetSubsystem<UAsdaCommunitySubsystem>();
			if (bOpen && !S->IsWindowOpen()) { S->OpenOnTab(0); /* ★ R5-fix6-c (13.11.2026): вкладку 0 ставит сам тест штатным путём (хоткей 35), а не берёт оставшуюся от соседа пачки */ } else if (!bOpen && S->IsWindowOpen()) { S->CloseWindow(); }
		}
	}

	static void StageNextWin(FCtxRef C);
	// Итог.
	static void Finish(FCtxRef C)
	{
		if (IConsoleVariable* L = IConsoleManager::Get().FindConsoleVariable(TEXT("r.OneFrameThreadLag"))) { C->Expect(L->GetInt() == 1, FString::Printf(TEXT("после отпускания r.OneFrameThreadLag вернулся: %d"), L->GetInt())); }
		UE_LOG(LogAsdaDragFollow, Display, TEXT("[за курсором] наибольшее расхождение по всем окнам: %.1f px, FPS-предел t.MaxFPS=%d"), C->MaxAll,
			IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")) ? IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetInt() : -1);
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaDragFollow, TEXT("SelfTestDragFollow"), C->Ok, C->Fail);
	}

	// Начало кадра: геометрия, нарисованная в прошлом кадре, против курсора, по которому он рисовался; затем следующий рывок.
	static bool OnPost(TWeakPtr<FCtx> Weak)
	{
		TSharedPtr<FCtx> P = Weak.Pin();
		if (!P.IsValid() || P->Step < 0) { return false; }
		FCtxRef C = P.ToSharedRef();
		UAsdaWindowSubsystem* WM = C->WM();
		const FName Id = C->Wins[C->WinIdx];
		FVector2D Mn, Mx;
		const FVector2D Cur = C->DrawCur;
		if (IConsoleVariable* L = IConsoleManager::Get().FindConsoleVariable(TEXT("r.OneFrameThreadLag"))) { C->LagOn += (C->Step > 0 && L->GetInt() == 0) ? 1 : 0; }
		const bool bRect = WM && WM->DebugGetDragHandleRect(Id, C->Handle, Mn, Mx);
		if (!bRect) { UE_LOG(LogAsdaDragFollow, Display, TEXT("[за курсором] %s шаг %d: рамки ручки нет"), *Id.ToString(), C->Step); }
		if (bRect)
		{
			const FVector2D Want = C->H0 + (Cur - C->Grab);
			const double Err = (Mn - Want).Size();
			C->MaxErr = FMath::Max(C->MaxErr, Err);
			UE_LOG(LogAsdaDragFollow, Display, TEXT("[за курсором] %s кадр %llu шаг %d: курсор %.0f,%.0f, ручка %.1f,%.1f, ждём %.1f,%.1f, расхождение %.1f px"),
				*Id.ToString(), (uint64)GFrameCounter, C->Step, Cur.X, Cur.Y, Mn.X, Mn.Y, Want.X, Want.Y, Err);
		}
		if (C->Step >= (int32)UE_ARRAY_COUNT(kJerks))
		{
			// Конец рывков: отпустить и подвести итог окна.
			Btn(Cur, false);
			C->Step = -1;
			C->Expect(C->MaxErr <= 1.5, FString::Printf(TEXT("%s: точка захвата под курсором на всех кадрах, наибольшее расхождение %.1f px"), *Id.ToString(), C->MaxErr));
			{
				IConsoleVariable* Key = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.DragLowLatency"));
				const bool bKey = Key && Key->GetInt() != 0;
				C->Expect(bKey ? C->LagOn >= (int32)UE_ARRAY_COUNT(kJerks) - 2 : C->LagOn == 0, FString::Printf(TEXT("%s: кадров перетаскивания с короткой очередью %d (ключ asda.UI.DragLowLatency=%d)"), *Id.ToString(), C->LagOn, bKey ? 1 : 0));
			}
			C->MaxAll = FMath::Max(C->MaxAll, C->MaxErr);
			After(C, 0.4f, [](FCtxRef X) { ShowWin(X, X->Wins[X->WinIdx], false); ++X->WinIdx; After(X, 0.4f, &StageNextWin); });
			return false;
		}
		C->DrawCur = C->LastCur;
		C->LastCur = C->Grab + kJerks[C->Step];
		Move(C->LastCur, true);
		++C->Step;
		return true;
	}

	// Этап: окно открыто, нажать на ручке.
	static void StagePress(FCtxRef C)
	{
		UAsdaWindowSubsystem* WM = C->WM();
		const FName Id = C->Wins[C->WinIdx];
		FVector2D Mn(0, 0), Mx(0, 0);
		C->Handle = 0;
		while (WM && C->Handle < 16 && !WM->DebugGetDragHandleRect(Id, C->Handle, Mn, Mx)) { ++C->Handle; }
		const bool bRectDbg = WM && WM->DebugGetDragHandleRect(Id, C->Handle, Mn, Mx);
		UE_LOG(LogAsdaDragFollow, Display, TEXT("[за курсором] %s: нажатие, окно видно=%d, ручка=%d %.0f,%.0f..%.0f,%.0f"), *Id.ToString(), WM && WM->IsWindowVisible(Id) ? 1 : 0, bRectDbg ? 1 : 0, Mn.X, Mn.Y, Mx.X, Mx.Y);
		if (!WM || !WM->IsWindowVisible(Id) || !WM->DebugGetDragHandleRect(Id, C->Handle, Mn, Mx))
		{
			C->Expect(false, FString::Printf(TEXT("%s: окно или ручка не найдены"), *Id.ToString()));
			++C->WinIdx; After(C, 0.2f, &StageNextWin); return;
		}
		// Точка захвата: левая часть ручки (кнопки у ручки обычно справа), чтобы нажатие пришлось на саму ручку.
		C->Grab = FVector2D(Mn.X + FMath::Min(40.0, (Mx.X - Mn.X) * 0.25), (Mn.Y + Mx.Y) * 0.5);
		C->H0 = Mn;
		C->MaxErr = 0.0;
		Move(C->Grab, false);
		Btn(C->Grab, true);
		C->Step = 0;
		C->LastCur = C->Grab;
		C->DrawCur = C->Grab;
		C->LagOn = 0;
		C->Post = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return OnPost(TWeakPtr<FCtx>(C)); }), 0.f);   // сильная ссылка держит прогон, пока идут рывки
	}
	// Этап: следующее окно.
	static void StageNextWin(FCtxRef C)
	{
		UE_LOG(LogAsdaDragFollow, Display, TEXT("[за курсором] окно %d из %d"), C->WinIdx + 1, C->Wins.Num());
		if (C->WinIdx >= C->Wins.Num()) { Finish(C); return; }
		const FName Id = C->Wins[C->WinIdx];
		if (UAsdaWindowSubsystem* WM = C->WM()) { WM->ResetWindowLayout(Id); }
		ShowWin(C, Id, true);
		After(C, 1.0f, &StagePress);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSelfTestDragFollow(
	TEXT("asda.UI.SelfTestDragFollow"),
	TEXT("asda.UI.SelfTestDragFollow [delay=N] [quit] — ОФЛАЙН-самотест 09.11.2026: рывки курсора при перетаскивании окна настоящими событиями Slate, ")
	TEXT("расхождение точки захвата и курсора после отрисовки каждого кадра (настройки 69500, сумка 400, сообщество 6400)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaDragFollowLocal;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KitArgs.bQuit;
		C->Wins = { FName(TEXT("SYSTEM_Option_NEW")), FName(TEXT("INVEN_WIN")), FName(TEXT("COMMUNITY_WIN")) };
		auto Go = [C]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			StageNextWin(C);
		};
		if (KitArgs.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), KitArgs.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
