// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ЗОН МАГАЗИНА БУСТЕРОВ — `asda.PremiumMall.BoosterZonesSelfTest [delay=N] [quit]` (06.11.2026, тот же дефект, что в окне создания:
//   клик-зоны скрытых ячеек перекрывали кнопки). Путь игрока: настоящие события мыши Slate, геометрия `GetCachedGeometry`.
//   Проверки: 1) «Пополнить» 15513 и кнопка баланса 15514: вся площадь сеткой 3 px (верхний виджет = сама кнопка) и 21 настоящее нажатие
//   (отпускание в стороне: клик не срабатывает, исходящих нет); 2) каждая карточка страницы: зона видна и нажимается; 3) зоны скрытых ячеек
//   погашены; 4) панель подарка: настоящий клик открывает её, вся площадь её кнопок своя, отмена возвращает зоны; 5) пустая вкладка: все зоны
//   погашены; возврат на вкладку «Все»: карточки снова нажимаются. Кнопка «Подарок» видна при `asda.PremiumMall.ShowBoosterGift 1` или включённом подарке.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Login/AsdaButtonRelay.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoosterZones, Log, All);   // журнал самотеста

namespace AsdaBoosterZonesLocal
{
	static const TCHAR* kCmd = TEXT("asda.PremiumMall.BoosterZonesSelfTest");   // имя команды
	static constexpr float kStep = 0.04f;                                       // пауза между событиями мыши
	static constexpr int32 kCells = 21;                                         // ячеек сетки

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                              // мир
		TWeakObjectPtr<UAsdaBoosterShopSubsystem> Bs;          // подсистема окна
		TWeakObjectPtr<UUserWidget> Win;                       // окно
		bool bQuit = false;                                    // выйти после итога
		int32 Ok = 0, Fail = 0;                                // счётчики
		int32 Pressed = 0;                                     // настоящие нажатия цели
		TArray<TStrongObjectPtr<UAsdaButtonRelay>> Relays;     // реле нажатий
		TArray<FName> Targets;                                 // очередь кнопок
		TSet<FName> Bound;                                     // кнопки со счётчиком: второй проход не подключает его повторно
		TArray<FVector2D> Pts;                                 // точки текущей кнопки
		int32 TargetIdx = 0, PtIdx = 0;                        // текущая кнопка и точка
		bool bAfterEmpty = false;                              // второй проход: после пустой вкладки
		int32 ZonesBefore = 0;                                 // зон карточек до панели подарка
		bool bGiftPass = false;                                // идёт проход панели подарка
		bool bGiftDone = false;                                // панель подарка проверена

		// Проверка: счётчик и строка журнала.
		void Expect(bool b, const FString& M)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaBoosterZones, Display, TEXT("[зоны бустеров] %s %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *M);
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Следующий этап через Sec секунд.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что проверка мира записана `if (W) { … }` вместо раннего выхода.

	// Настоящее движение мыши.
	using AsdaSelfTestInput::Move;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Настоящее нажатие/отпускание левой кнопки.
	static void Press(const FVector2D& P, bool bDown)
	{
		FSlateApplication& App = FSlateApplication::Get();
		TSet<FKey> Keys; if (bDown) { Keys.Add(EKeys::LeftMouseButton); }
		const FPointerEvent E(0, P, P, Keys, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (bDown) { App.ProcessMouseButtonDownEvent(nullptr, E); } else { App.ProcessMouseButtonUpEvent(E); }
	}

	// Показан ли виджет с учётом всей цепочки родителей.
	static bool Shown(const UWidget* W)
	{
		for (; W; W = W->GetParent())
		{
			const ESlateVisibility V = W->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
		}
		return true;
	}

	// Есть ли кнопка в пути попадания Slate под точкой.
	static bool OnTop(const UWidget* B, const FVector2D& P)
	{
		FSlateApplication& App = FSlateApplication::Get();
		TArray<TSharedRef<SWindow>> Wins = App.GetInteractiveTopLevelWindows();
		const FWidgetPath Path = App.LocateWindowUnderMouse(P, Wins, false, 0);
		TSharedPtr<SWidget> S = B->GetCachedWidget();
		return Path.IsValid() && S.IsValid() && Path.ContainsWidget(S.Get());
	}

	// Итог: окно закрывается штатно.
	static void Finish(FCtxRef C)
	{
		if (UAsdaBoosterShopSubsystem* Bs = C->Bs.Get()) { Bs->CloseWindow(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaBoosterZones, TEXT("BoosterZonesSelfTest"), C->Ok, C->Fail);
	}

	static void StageTarget(FCtxRef C);
	static void StageRelease(FCtxRef C);
	// Этап: настоящее нажатие в очередной точке.
	static void StagePress(FCtxRef C)
	{
		const FVector2D P = C->Pts[C->PtIdx];
		Move(P); Press(P, true);
		After(C, kStep, &StageRelease);
	}
	// Этап: отпускание в стороне (клик не срабатывает); следующая точка или кнопка.
	static void StageRelease(FCtxRef C)
	{
		const FVector2D Off(2.0, 2.0);
		Move(Off); Press(Off, false);
		if (++C->PtIdx < C->Pts.Num()) { After(C, kStep, &StagePress); return; }
		C->Expect(C->Pressed == C->Pts.Num(), FString::Printf(TEXT("%s: настоящих нажатий %d из %d"), *C->Targets[C->TargetIdx].ToString(), C->Pressed, C->Pts.Num()));
		++C->TargetIdx;
		After(C, kStep, &StageTarget);
	}

	static void StageEmptyDone(FCtxRef C);
	static void StageGiftOpen(FCtxRef C);
	static void StageGiftClose(FCtxRef C);
	// Этап: очередная кнопка: карта площади и очередь нажатий.
	static void StageTarget(FCtxRef C)
	{
		UUserWidget* Win = C->Win.Get();
		if (!Win) { C->Expect(false, TEXT("окно пропало")); Finish(C); return; }
		if (C->TargetIdx >= C->Targets.Num())
		{
			if (C->bAfterEmpty) { Finish(C); return; }
			if (C->bGiftPass) { StageGiftClose(C); return; }
			if (!C->bGiftDone) { StageGiftOpen(C); return; }
			if (UAsdaBoosterShopSubsystem* Bs = C->Bs.Get()) { Bs->SetCategory(1); }   // «Популярные»: пустая вкладка (самотест вкладок: 0 карточек)
			After(C, 0.5f, &StageEmptyDone);
			return;
		}
		UButton* B = Cast<UButton>(Win->GetWidgetFromName(C->Targets[C->TargetIdx]));
		FVector2D Mn, Mx;
		if (!B || !AsdaUi::WidgetAbsRect(B, Mn, Mx)) { C->Expect(false, FString::Printf(TEXT("%s: нет рамки"), *C->Targets[C->TargetIdx].ToString())); ++C->TargetIdx; After(C, kStep, &StageTarget); return; }
		int32 Tot = 0, Mine = 0;
		for (double Y = Mn.Y + 1.0; Y <= Mx.Y - 1.0; Y += 3.0)
		{
			for (double X = Mn.X + 1.0; X <= Mx.X - 1.0; X += 3.0) { ++Tot; Mine += OnTop(B, FVector2D(X, Y)) ? 1 : 0; }
		}
		C->Expect(Mine == Tot, FString::Printf(TEXT("%s рамка %.1f,%.1f..%.1f,%.1f: своя площадь %d из %d"), *B->GetName(), Mn.X, Mn.Y, Mx.X, Mx.Y, Mine, Tot));
		if (!C->Bound.Contains(B->GetFName()))
		{
			C->Bound.Add(B->GetFName());
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(B);
			FCtx* Raw = &C.Get();
			R->PressCallback = [Raw](int32, bool bDown) { if (bDown) { ++Raw->Pressed; } };
			B->OnPressed.AddDynamic(R, &UAsdaButtonRelay::OnPressed);
			C->Relays.Emplace(R);
		}
		const bool bBig = (Mx - Mn).X > 40.0;
		C->Pts.Reset();
		for (int32 Iy = 0; Iy < (bBig ? 3 : 2); ++Iy)
		{
			for (int32 Ix = 0; Ix < (bBig ? 7 : 2); ++Ix)
			{
				C->Pts.Add(FVector2D(Mn.X + 2.0 + (Mx.X - Mn.X - 4.0) * Ix / (bBig ? 6.0 : 1.0), Mn.Y + 2.0 + (Mx.Y - Mn.Y - 4.0) * Iy / (bBig ? 2.0 : 1.0)));
			}
		}
		C->PtIdx = 0; C->Pressed = 0;
		After(C, kStep, &StagePress);
	}

	// Очередь кнопок: две кнопки окна и зоны показанных карточек.
	static int32 QueueTargets(FCtxRef C)
	{
		UUserWidget* Win = C->Win.Get();
		C->Targets = { FName(TEXT("BOOSTER_SHOP_CHARGE")), FName(TEXT("Booster_CashCheck_Btn")) };
		int32 Zones = 0;
		for (int32 i = 0; i < kCells; ++i)
		{
			const FName N(*FString::Printf(TEXT("BoosterShop_HIT%02d"), i));
			if (Win && Shown(Win->GetWidgetFromName(N))) { C->Targets.Add(N); ++Zones; }
		}
		C->TargetIdx = 0;
		return Zones;
	}

	// Настоящий клик по кнопке окна.
	static bool ClickBtn(FCtxRef C, const TCHAR* Name)
	{
		UUserWidget* Win = C->Win.Get();
		FVector2D Mn, Mx;
		UWidget* B = Win ? Win->GetWidgetFromName(FName(Name)) : nullptr;
		if (!B || !Shown(B) || !AsdaUi::WidgetAbsRect(B, Mn, Mx)) { C->Expect(false, FString::Printf(TEXT("%s: нет видимой кнопки"), Name)); return false; }
		const FVector2D P = (Mn + Mx) * 0.5;
		Move(P); Press(P, true); Press(P, false);
		return true;
	}
	// Зон карточек включено сейчас.
	static int32 ZonesOn(FCtxRef C)
	{
		int32 N = 0;
		for (int32 i = 0; i < kCells; ++i)
		{
			if (C->Win.IsValid() && Shown(C->Win->GetWidgetFromName(FName(*FString::Printf(TEXT("BoosterShop_HIT%02d"), i))))) { ++N; }
		}
		return N;
	}
	// Этап: панель подарка открыта: вся площадь её кнопок.
	static void StageGiftReady(FCtxRef C)
	{
		UAsdaBoosterShopSubsystem* Bs = C->Bs.Get();
		C->Expect(Bs && Bs->DebugPurMode() == 2, FString::Printf(TEXT("панель подарка открыта настоящим кликом: режим %d, зон %d из %d"), Bs ? Bs->DebugPurMode() : -1, ZonesOn(C), C->ZonesBefore));
		C->bGiftPass = true;
		C->Targets = { FName(TEXT("BOOSTER_PUR_CONFIRM")), FName(TEXT("BOOSTER_PUR_OKBTN")), FName(TEXT("BOOSTER_PUR_CANCEL")), FName(TEXT("BOOSTER_PUR_PREV")) };
		C->TargetIdx = 0;
		StageTarget(C);
	}
	// Этап: настоящий клик «Подарок» первой карточки.
	static void StageGiftOpen(FCtxRef C)
	{
		C->ZonesBefore = ZonesOn(C);
		if (!ClickBtn(C, TEXT("BOOSTER_ITEM_PRESENT00"))) { Finish(C); return; }
		After(C, 0.5f, &StageGiftReady);
	}
	// Этап: панель закрыта: все зоны карточек вернулись.
	static void StageGiftClosed(FCtxRef C)
	{
		UAsdaBoosterShopSubsystem* Bs = C->Bs.Get();
		const int32 Now = ZonesOn(C);
		C->Expect(Bs && Bs->DebugPurMode() == 0 && Now == C->ZonesBefore, FString::Printf(TEXT("панель закрыта настоящим кликом: режим %d, зон %d из %d"), Bs ? Bs->DebugPurMode() : -1, Now, C->ZonesBefore));
		C->bGiftPass = false; C->bGiftDone = true;
		C->Targets.Reset(); C->TargetIdx = 0;
		StageTarget(C);
	}
	// Этап: настоящий клик «Отмена» панели.
	static void StageGiftClose(FCtxRef C)
	{
		if (!ClickBtn(C, TEXT("BOOSTER_PUR_CANCEL"))) { Finish(C); return; }
		After(C, 0.5f, &StageGiftClosed);
	}

	// Этап: пустая вкладка: зон нет; возврат на «Все» и второй проход.
	static void StageBack(FCtxRef C)
	{
		C->bAfterEmpty = true;
		const int32 Zones = QueueTargets(C);
		C->Expect(Zones > 0, FString::Printf(TEXT("после возврата на «Все»: зон карточек %d"), Zones));
		StageTarget(C);
	}
	static void StageEmptyDone(FCtxRef C)
	{
		const int32 Zones = QueueTargets(C);
		C->Expect(Zones == 0, FString::Printf(TEXT("пустая вкладка: включённых зон %d"), Zones));
		if (UAsdaBoosterShopSubsystem* Bs = C->Bs.Get()) { Bs->SetCategory(0); }
		After(C, 0.5f, &StageBack);
	}

	// Этап: окно открыто.
	static void StageReady(FCtxRef C)
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			if (It->GetWorld() == C->W.Get() && It->IsInViewport() && It->GetClass()->GetName() == TEXT("WBP_BOOSTER_SHOP_WIN_C")) { C->Win = *It; break; }
		}
		if (!C->Win.IsValid()) { C->Expect(false, TEXT("окно 15300 не найдено")); Finish(C); return; }
		const int32 Zones = QueueTargets(C);
		C->Expect(Zones > 0 && Zones <= kCells, FString::Printf(TEXT("страница: включённых зон карточек %d"), Zones));
		StageTarget(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaBoosterZonesSelfTest(
	TEXT("asda.PremiumMall.BoosterZonesSelfTest"),
	TEXT("asda.PremiumMall.BoosterZonesSelfTest [delay=N] [quit] — ОФЛАЙН-самотест 06.11.2026: магазин бустеров настоящими нажатиями: вся площадь кнопок 15513/15514, ")
	TEXT("зоны показанных карточек, погашенные зоны на пустой вкладке и возврат."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaBoosterZonesLocal;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KitArgs.bQuit;
		auto Go = [C]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			UGameInstance* GI = C->W.IsValid() ? C->W->GetGameInstance() : nullptr;
			UAsdaBoosterShopSubsystem* Bs = GI ? GI->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr;
			if (!Bs) { AsdaSelfTestKit::Refused(C->W.Get(), kCmd, C->bQuit, TEXT("нет подсистемы магазина бустеров")); return; }
			C->Bs = Bs;
			Bs->OpenWindow();
			After(C, 1.5f, &StageReady);
		};
		if (KitArgs.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), KitArgs.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
