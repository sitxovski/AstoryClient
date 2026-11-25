// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПОДСКАЗКИ О ДРУГЕ НА ВЫБОРЕ ПЕРСОНАЖА — `asda.Login.SelfTestSoulInfoTip [shotdir=<папка>] [delay=N] [quit]` (08.11.2026, пачка 16, баг 9).
//   Жалоба владельца: окно 114 (SOULINFO_WIN, «If you meet someone you like…») при наведении на призрак Друга стоит далеко справа,
//   а должно быть у курсора и следовать за ним. Правка — `AsdaPlaceSoulInfoPanel` (AsdaLoginGameMode_Soul.cpp), ключ
//   `asda.SoulInfo.FollowCursor`.
//   Путь игрока: офлайн-экран выбора (`asda.Login.CharSelectOffline`), шкатулка открывается сама, затем НАСТОЯЩИЕ движения мыши Slate
//   (`ProcessMouseMoveEvent`) по сетке экрана — точки, где наведение показало окно 114, и есть фигура призрака. Геометрия окна —
//   `GetCachedGeometry` планов рамки (то, что реально нарисовано), а не слоты.
//   Проверки: окно видно над призраком; рамка целиком на экране; курсор рамкой не закрыт; без прижатия левый-верх = курсор +32/+32;
//   при движении курсора рамка сдвигается вслед; ушли с фигуры — окно скрыто и сдвиг снят; ключ 0 — окно на эталонном месте (сдвиг 0).
//   Кадры: `soulinfo_tip_<n>` (Saved/Screenshots), положения курсора — в журнале.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "Login/AsdaLoginGameMode.h"
#include "AsdaNetSubsystem.h"                  // CharList + OnCharListReady (событие пакета 1006)
#include "UI/AsdaWindowSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Slate/SGameLayerManager.h"
#include "UnrealClient.h"                      // FScreenshotRequest: кадр вместе со слоем UI
#include "TimerManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulInfoTip, Log, All);   // журнал самотеста

namespace AsdaSoulInfoTipLocal
{
	static const TCHAR* kCmd = TEXT("asda.Login.SelfTestSoulInfoTip");   // имя команды
	static constexpr float kStep = 0.05f;                                // пауза между точками: тик наведения + кадр раскладки

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                // мир
		bool bQuit = false;                      // выйти после итога
		int32 Ok = 0, Fail = 0;                  // счётчики
		TArray<FVector2D> Grid;                  // точки сетки поиска (абсолютные координаты Slate)
		int32 GridIdx = 0;                       // текущая точка сетки
		TArray<FVector2D> Hits;                  // точки, где окно 114 показалось (фигура призрака/Друга)
		FVector2D Miss = FVector2D::ZeroVector;  // точка «мимо всего» (окно скрыто)
		bool bMissFound = false;                 // точка «мимо» найдена
		TArray<FVector2D> Probe;                 // точки проверки (верх/середина/низ фигуры)
		int32 ProbeIdx = 0;                      // текущая проверка
		FVector2D PrevFrame = FVector2D::ZeroVector, PrevCursor = FVector2D::ZeroVector;   // прошлая проверка (следование)
		bool bPrev = false;                      // прошлая проверка была без прижатия
		int32 Shot = 0;                          // номер кадра
		float WaitLeft = 0.f;                    // остаток ожидания экрана выбора, с
		FString ShotDir;                         // папка кадров (shotdir=; пусто — Saved/Screenshots песочницы)

		// Полное имя кадра: с папкой — путь (движок берёт его как есть), без — имя в Saved/Screenshots.
		FString ShotName(const FString& Base) const { return ShotDir.IsEmpty() ? Base : FPaths::Combine(ShotDir, Base + TEXT(".png")); }

		// Проверка: счётчик и строка журнала.
		void Expect(bool b, const FString& M)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaSoulInfoTip, Display, TEXT("[подсказка Друга] %s %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *M);
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Следующий этап через Sec секунд.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Настоящее движение мыши.
	using AsdaSelfTestInput::Move;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Окно 114 (как его открывает режим логина: id SOULINFO_WIN).
	static UUserWidget* Win(FCtxRef C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		return WM ? WM->FindWindow(FName(TEXT("SOULINFO_WIN"))) : nullptr;
	}
	static bool Shown(FCtxRef C)
	{
		UUserWidget* U = Win(C);
		return U && U->IsVisible();
	}

	// Абсолютный прямоугольник игрового вьюпорта (Slate).
	static bool ViewportAbs(FVector2D& Mn, FVector2D& Sz)
	{
		if (!GEngine || !GEngine->GameViewport) { return false; }
		TSharedPtr<SViewport> V = GEngine->GameViewport->GetGameViewportWidget();
		if (!V) { return false; }
		const FGeometry& G = V->GetCachedGeometry();
		Mn = G.GetAbsolutePosition(); Sz = G.GetAbsoluteSize();
		return Sz.X > 1.f && Sz.Y > 1.f;
	}

	// Нарисованная рамка окна 114 (планы 0 и 8 девятипатча) в абсолютных координатах.
	static bool FrameAbs(FCtxRef C, FVector2D& Mn, FVector2D& Mx, float& Scale)
	{
		UUserWidget* U = Win(C);
		if (!U) { return false; }
		UWidget* P0 = U->GetWidgetFromName(FName(TEXT("SOULINFO_WIN_0_0")));
		UWidget* P8 = U->GetWidgetFromName(FName(TEXT("SOULINFO_WIN_0_8")));
		if (!P0 || !P8) { return false; }
		const FGeometry& G0 = P0->GetCachedGeometry();
		const FGeometry& G8 = P8->GetCachedGeometry();
		Mn = G0.GetAbsolutePosition();
		Mx = G8.GetAbsolutePosition() + G8.GetAbsoluteSize();
		Scale = G0.Scale;
		return (Mx.X - Mn.X) > 1.f && Scale > 0.f;
	}

	static void Finish(FCtxRef C)
	{
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaSoulInfoTip, TEXT("SelfTestSoulInfoTip"), C->Ok, C->Fail);
	}

	static void StageProbe(FCtxRef C);
	static void StageProbeCheck(FCtxRef C);
	static void StageMiss(FCtxRef C);
	static void StageMissCheck(FCtxRef C);
	static void StageKeyOff(FCtxRef C);
	static void StageKeyOffCheck(FCtxRef C);

	// Сетка поиска: шаг 20 px по всему вьюпорту.
	static void StageGridStep(FCtxRef C)
	{
		if (C->GridIdx > 0)
		{
			const FVector2D Last = C->Grid[C->GridIdx - 1];
			if (Shown(C)) { C->Hits.Add(Last); }
			else if (!C->bMissFound) { C->Miss = Last; C->bMissFound = true; }
		}
		if (C->GridIdx >= C->Grid.Num())
		{
			UE_LOG(LogAsdaSoulInfoTip, Display, TEXT("[подсказка Друга] сетка: точек %d, окно 114 показано в %d"), C->Grid.Num(), C->Hits.Num());
			C->Expect(C->Hits.Num() > 0, TEXT("наведение на фигуру призрака показывает окно 114"));
			if (C->Hits.Num() == 0) { Finish(C); return; }
			// Точки проверки: верх, середина, низ фигуры (по Y) — разные положения курсора.
			TArray<FVector2D> S = C->Hits;
			S.Sort([](const FVector2D& A, const FVector2D& B) { return A.Y < B.Y || (A.Y == B.Y && A.X < B.X); });
			C->Probe = { S[S.Num() / 8], S[S.Num() / 2], S[(S.Num() * 7) / 8], S.Last() };
			C->ProbeIdx = 0;
			StageProbe(C);
			return;
		}
		Move(C->Grid[C->GridIdx++]);
		After(C, kStep, &StageGridStep);
	}

	// Курсор в точку проверки; ждём два кадра (тик наведения + раскладка Slate).
	static void StageProbe(FCtxRef C)
	{
		if (C->ProbeIdx >= C->Probe.Num()) { StageMiss(C); return; }
		Move(C->Probe[C->ProbeIdx]);
		After(C, 0.25f, &StageProbeCheck);
	}

	static void StageProbeCheck(FCtxRef C)
	{
		const FVector2D Cur = C->Probe[C->ProbeIdx];
		FVector2D VMn, VSz, FMn, FMx; float Sc = 1.f;
		const bool bV = ViewportAbs(VMn, VSz);
		const bool bF = Shown(C) && FrameAbs(C, FMn, FMx, Sc);
		C->Expect(bF, FString::Printf(TEXT("точка %d курсор (%.0f,%.0f): окно 114 показано"), C->ProbeIdx, Cur.X, Cur.Y));
		if (bF && bV)
		{
			const FVector2D Sz = FMx - FMn;
			const bool bInside = FMn.X >= VMn.X + 3.f && FMn.Y >= VMn.Y + 3.f && FMx.X <= VMn.X + VSz.X - 3.f && FMx.Y <= VMn.Y + VSz.Y - 3.f;
			C->Expect(bInside, FString::Printf(TEXT("  рамка (%.0f,%.0f)-(%.0f,%.0f) целиком на экране (%.0f,%.0f)-(%.0f,%.0f)"),
				FMn.X, FMn.Y, FMx.X, FMx.Y, VMn.X, VMn.Y, VMn.X + VSz.X, VMn.Y + VSz.Y));
			const bool bCovers = Cur.X >= FMn.X && Cur.X <= FMx.X && Cur.Y >= FMn.Y && Cur.Y <= FMx.Y;
			C->Expect(!bCovers, TEXT("  курсор рамкой не закрыт"));
			const FVector2D Want = Cur + FVector2D(32.f, 32.f) * Sc;   // эталон Tooltip_LayoutNearCursor
			const bool bFits = Want.X + Sz.X <= VMn.X + VSz.X && Want.Y + Sz.Y + 5.f * Sc <= VMn.Y + VSz.Y;
			if (bFits)
			{
				C->Expect(FMn.Equals(Want, 2.f), FString::Printf(TEXT("  без прижатия левый-верх = курсор +32/+32: (%.0f,%.0f), ждали (%.0f,%.0f)"),
					FMn.X, FMn.Y, Want.X, Want.Y));
				if (C->bPrev)
				{
					const FVector2D dC = Cur - C->PrevCursor, dF = FMn - C->PrevFrame;
					C->Expect(dF.Equals(dC, 2.f), FString::Printf(TEXT("  следует за курсором: курсор сдвинут на (%.0f,%.0f), рамка на (%.0f,%.0f)"),
						dC.X, dC.Y, dF.X, dF.Y));
				}
				C->PrevCursor = Cur; C->PrevFrame = FMn; C->bPrev = true;
			}
			else
			{
				UE_LOG(LogAsdaSoulInfoTip, Display, TEXT("[подсказка Друга]   у края — прижатие к экрану: рамка (%.0f,%.0f)"), FMn.X, FMn.Y);
			}
		}
		FScreenshotRequest::RequestScreenshot(C->ShotName(FString::Printf(TEXT("soulinfo_tip_%d"), C->Shot++)), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		++C->ProbeIdx;
		After(C, 0.4f, &StageProbe);
	}

	// Курсор мимо всего: окно скрыто, сдвиг снят.
	static void StageMiss(FCtxRef C)
	{
		if (!C->bMissFound) { C->Expect(false, TEXT("точка «мимо всего» не найдена")); StageKeyOff(C); return; }
		Move(C->Miss);
		After(C, 0.25f, &StageMissCheck);
	}
	static void StageMissCheck(FCtxRef C)
	{
		UUserWidget* U = Win(C);
		C->Expect(!Shown(C), FString::Printf(TEXT("курсор ушёл с фигуры (%.0f,%.0f): окно 114 скрыто"), C->Miss.X, C->Miss.Y));
		C->Expect(U && U->GetRenderTransform().Translation.IsNearlyZero(0.5f), TEXT("  сдвиг «у курсора» снят (следующий показ не из наведения — на эталонном месте)"));
		StageKeyOff(C);
	}

	// Ключ отката 0: над фигурой окно на эталонном месте.
	static void StageKeyOff(FCtxRef C)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.SoulInfo.FollowCursor"))) { V->Set(0, ECVF_SetByConsole); }
		Move(C->Probe.Num() ? C->Probe[1] : FVector2D::ZeroVector);
		After(C, 0.25f, &StageKeyOffCheck);
	}
	static void StageKeyOffCheck(FCtxRef C)
	{
		UUserWidget* U = Win(C);
		C->Expect(Shown(C) && U && U->GetRenderTransform().Translation.IsNearlyZero(0.5f),
			TEXT("asda.SoulInfo.FollowCursor 0: окно 114 показано на эталонном месте (сдвиг 0)"));
		FVector2D FMn, FMx; float Sc = 1.f;
		if (FrameAbs(C, FMn, FMx, Sc)) { UE_LOG(LogAsdaSoulInfoTip, Display, TEXT("[подсказка Друга]   эталонное место рамки (%.0f,%.0f)-(%.0f,%.0f)"), FMn.X, FMn.Y, FMx.X, FMx.Y); }
		FScreenshotRequest::RequestScreenshot(C->ShotName(FString::Printf(TEXT("soulinfo_tip_%d_key0"), C->Shot++)), true, false);
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.SoulInfo.FollowCursor"))) { V->Set(1, ECVF_SetByConsole); }
		After(C, 0.5f, &Finish);
	}

	// Ждём, пока шкатулка откроется и покажется панель выбора (CHARSELECT_WIN), затем 3 с на фейд-ин фигур.
	static void StageReady(FCtxRef C);
	static void StageWaitScene(FCtxRef C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UUserWidget* CS = WM ? WM->FindWindow(FName(TEXT("CHARSELECT_WIN"))) : nullptr;
		if (CS && CS->IsVisible()) { UE_LOG(LogAsdaSoulInfoTip, Display, TEXT("[подсказка Друга] экран выбора открыт")); After(C, 3.0f, &StageReady); return; }
		C->WaitLeft -= 1.f;
		if (C->WaitLeft <= 0.f) { C->Expect(false, TEXT("экран выбора персонажа не открылся за 60 с")); Finish(C); return; }
		After(C, 1.0f, &StageWaitScene);
	}

	// Экран выбора показан, шкатулка открыта — строим сетку.
	static void StageReady(FCtxRef C)
	{
		FVector2D VMn, VSz;
		if (!ViewportAbs(VMn, VSz)) { AsdaSelfTestKit::Refused(C->W.Get(), kCmd, C->bQuit, TEXT("нет вьюпорта")); return; }
		for (float y = VMn.Y + 8.f; y < VMn.Y + VSz.Y; y += 20.f)
		{
			for (float x = VMn.X + 8.f; x < VMn.X + VSz.X * 0.7f; x += 20.f) { C->Grid.Add(FVector2D(x, y)); }
		}
		UE_LOG(LogAsdaSoulInfoTip, Display, TEXT("[подсказка Друга] вьюпорт (%.0f,%.0f) %.0fx%.0f, точек сетки %d"), VMn.X, VMn.Y, VSz.X, VSz.Y, C->Grid.Num());
		C->GridIdx = 0;
		StageGridStep(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaLoginSelfTestSoulInfoTip(
	TEXT("asda.Login.SelfTestSoulInfoTip"),
	TEXT("asda.Login.SelfTestSoulInfoTip [shotdir=<папка>] [delay=N] [quit] — ОФЛАЙН-самотест 08.11.2026: окно 114 (подсказка о Друге) при наведении на призрак ")
	TEXT("стоит у курсора и следует за ним; уход с фигуры прячет его; ключ asda.SoulInfo.FollowCursor 0 — эталонное место. На карте логина."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaSoulInfoTipLocal;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KitArgs.bQuit;
		for (const FString& P : KitArgs.Pos) { if (P.StartsWith(TEXT("shotdir="))) { C->ShotDir = P.Mid(8); } }
		auto Go = [C]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			UWorld* W2 = C->W.Get();
			if (!W2 || !Cast<AAsdaLoginGameMode>(W2->GetAuthGameMode())) { AsdaSelfTestKit::Refused(W2, kCmd, C->bQuit, TEXT("нет AAsdaLoginGameMode")); return; }
			// Путь игрока: список персонажей «пришёл» — то же событие, что поднимает обработчик пакета 1006
			//   (`OnCharListReady`): перелёт камеры → открытие шкатулки → мой персонаж + призрак в слоте.
			UAsdaNetSubsystem* Net = W2->GetGameInstance() ? W2->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (!Net) { AsdaSelfTestKit::Refused(W2, kCmd, C->bQuit, TEXT("нет подсистемы сети")); return; }
			Net->CharList.SetNum(3);
			for (int32 i = 0; i < 3; ++i) { Net->CharList[i] = FAsdaCharRecord(); }
			FAsdaCharRecord& R = Net->CharList[0];   // условный персонаж (тот же, что у asda.Login.CharSelectOffline)
			R.Slot = 0; R.RawSlotId = 10; R.Name = TEXT("GM_sitxovski"); R.Job = 3; R.Level = 17; R.Exp = 0;
			R.HpMax = R.HpCur = 3270; R.MpMax = R.MpCur = 169; R.Stats = { 134, 206, 119, 146, 86, 206 };
			R.Constellation = 4;
			Net->OnCharListReady.Broadcast();
			C->WaitLeft = 60.f;
			After(C, 1.0f, &StageWaitScene);
		};
		if (KitArgs.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), KitArgs.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
