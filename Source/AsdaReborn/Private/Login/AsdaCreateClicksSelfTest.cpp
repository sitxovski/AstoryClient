// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОКНА СОЗДАНИЯ ПЕРСОНАЖА — `asda.Login.SelfTestCreateClicks [dump=<файл>] [shot] [delay=N] [quit]` (06.11.2026, жалоба игрока:
//   список созвездий ниже Virgo не выбирается, клики уходят в сетку одежды; «Создать»/«Отмена» отзываются лишь частью площади).
//   Путь игрока: настоящие события мыши Slate (`ProcessMouseMoveEvent`/`ProcessMouseButtonDownEvent`/`Up`), геометрия — `GetCachedGeometry`.
//   Этапы: 1) полный дамп контролов окна (имя, класс, родитель, видимость, Z, прямоугольник); 2) «Создать»/«Отмена»: вся площадь по
//   сетке — верхний виджет под точкой и настоящее нажатие (отпускание вне кнопки, клик не срабатывает); 3) 12 созвездий × 3 точки по ширине:
//   открыть настоящим кликом, выбрать, список закрыт, знак принят, сетка одежды нажатий не получила; 4) все кнопки окна: центр не перекрыт;
//   5) настоящий клик «Отмена» закрывает окно; повторное открытие; настоящий клик «Создать» с пустым именем показывает окно сообщения
//   (пакет 1014 не уходит: имя пустое, клиентская проверка отсекает его раньше).
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "Login/AsdaLoginGameMode.h"
#include "Login/AsdaButtonRelay.h"
#include "AsdaNetSubsystem.h"                  // FAsdaCharRecord::ConstellationName
#include "UObject/StrongObjectPtr.h"           // реле нажатий живут до конца прогона
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"                      // FScreenshotRequest: кадр вместе со слоем UI
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCreateClicks, Log, All);   // журнал самотеста

// Доступ самотеста к закрытому состоянию экрана создания (friend в AsdaLoginGameMode.h).
struct FAsdaCreateClicksAccess
{
	static void Show(AAsdaLoginGameMode* G) { G->ShowCharCreateUI(0); }
	static UUserWidget* Win(AAsdaLoginGameMode* G) { return G->CharCreateWidget; }
	static UPanelWidget* Drop(AAsdaLoginGameMode* G) { return G->ConstellationDropdown; }
	static UTextBlock* Display(AAsdaLoginGameMode* G) { return G->ConstellationDisplay; }
	static int32 Picked(AAsdaLoginGameMode* G) { return G->CreateConstellation; }
	static void ClearPick(AAsdaLoginGameMode* G) { G->CreateConstellation = 0; }
	static bool OnCreateScreen(AAsdaLoginGameMode* G) { return G->CurrentScreen == EAsdaScreen::CharCreate; }
};

namespace AsdaCreateClicksLocal
{
	using FAcc = FAsdaCreateClicksAccess;
	static const TCHAR* kCmd = TEXT("asda.Login.SelfTestCreateClicks");   // имя команды
	static constexpr float kStep = 0.06f;                                 // пауза между кликами: кадр отрисовки обновляет сетку попаданий

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                       // мир
		TWeakObjectPtr<AAsdaLoginGameMode> G;           // режим логина
		bool bQuit = false;                             // выйти после итога
		FString DumpFile;                               // файл дампа (dump=)
		bool bShot = false;                             // снять кадры окна (shot): закрытый и открытый список
		int32 Ok = 0, Fail = 0;                         // счётчики
		int32 PressOk = 0, PressCancel = 0, PressCell = 0;   // настоящие нажатия, дошедшие до кнопок
		TArray<TStrongObjectPtr<UAsdaButtonRelay>> Relays;   // реле нажатий (держим живыми)
		TArray<FVector2D> Pts;                          // очередь точек площади кнопки
		int32 PtIdx = 0;                                // текущая точка
		bool bCancelBtn = false;                        // какая кнопка проверяется
		int32 Sign = 1, XIdx = 0;                       // созвездие и точка по ширине

		// Проверка: счётчик и строка журнала.
		void Expect(bool b, const FString& M)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaCreateClicks, Display, TEXT("[создание] %s %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *M);
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Следующий этап через Sec секунд.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Настоящее движение мыши.
	using AsdaSelfTestInput::Move;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Настоящее нажатие/отпускание левой кнопки.
	static void Btn(const FVector2D& P, bool bDown)
	{
		FSlateApplication& App = FSlateApplication::Get();
		TSet<FKey> Keys; if (bDown) { Keys.Add(EKeys::LeftMouseButton); }
		const FPointerEvent E(0, P, P, Keys, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (bDown) { App.ProcessMouseButtonDownEvent(nullptr, E); } else { App.ProcessMouseButtonUpEvent(E); }
	}
	// Настоящий клик.
	static void Click(const FVector2D& P) { Move(P); Btn(P, true); Btn(P, false); }

	// Имя UMG-виджета, верхнего под точкой: обход пути Slate от листа к корню до первого известного виджета.
	static FString TopAt(const FVector2D& P, UUserWidget* Win, const UWidget** OutW = nullptr)
	{
		FSlateApplication& App = FSlateApplication::Get();
		TMap<const SWidget*, const UWidget*> Map;
		if (Win && Win->WidgetTree)
		{
			TArray<UWidget*> All; Win->WidgetTree->GetAllWidgets(All);
			for (UWidget* X : All) { if (X) { if (TSharedPtr<SWidget> S = X->GetCachedWidget()) { Map.Add(S.Get(), X); } } }
		}
		TArray<TSharedRef<SWindow>> Wins = App.GetInteractiveTopLevelWindows();
		const FWidgetPath Path = App.LocateWindowUnderMouse(P, Wins, false, 0);
		if (OutW) { *OutW = nullptr; }
		if (!Path.IsValid()) { return TEXT("(нет пути)"); }
		for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
		{
			const SWidget* S = &Path.Widgets[i].Widget.Get();
			if (const UWidget** F = Map.Find(S)) { if (OutW) { *OutW = *F; } return (*F)->GetName(); }
		}
		return FString::Printf(TEXT("(чужое окно: %s)"), *Path.Widgets.Last().Widget->GetTypeAsString());
	}

	// Полный дамп контролов окна в файл.
	static void Dump(FCtxRef C, UUserWidget* Win)
	{
		TArray<FString> Lines;
		TArray<UWidget*> All; Win->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All)
		{
			if (!X) { continue; }
			FVector2D Mn(0.0, 0.0), Mx(0.0, 0.0); const bool bR = AsdaUi::WidgetAbsRect(X, Mn, Mx);
			const UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(X->Slot);
			Lines.Add(FString::Printf(TEXT("%s\t%s\tparent=%s\tvis=%d\tz=%d\trect=%s%.1f,%.1f..%.1f,%.1f"), *X->GetName(), *X->GetClass()->GetName(),
				X->GetParent() ? *X->GetParent()->GetName() : TEXT("-"), (int32)X->GetVisibility(), S ? S->GetZOrder() : -1, bR ? TEXT("") : TEXT("?"), Mn.X, Mn.Y, Mx.X, Mx.Y));
		}
		if (!C->DumpFile.IsEmpty()) { FFileHelper::SaveStringArrayToFile(Lines, *C->DumpFile, FFileHelper::EEncodingOptions::ForceUTF8); }
		UE_LOG(LogAsdaCreateClicks, Display, TEXT("[создание] дамп: контролов %d → %s"), Lines.Num(), *C->DumpFile);
	}

	// Итог прогона.
	static void Finish(FCtxRef C)
	{
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaCreateClicks, TEXT("SelfTestCreateClicks"), C->Ok, C->Fail);
	}

	// Этап: после клика «Создать» с пустым именем должно быть окно сообщения.
	static void StageOkMsg(FCtxRef C)
	{
		UGameInstance* GI = C->W.IsValid() ? C->W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		C->Expect(WM && WM->IsOpen(FName(TEXT("MESSAGE_WIN"))), TEXT("настоящий клик «Создать» с пустым именем: окно сообщения показано"));
		Finish(C);
	}
	// Этап: окно открыто повторно — настоящий клик «Создать».
	static void StageReopened(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get(); UUserWidget* Win = G ? FAcc::Win(G) : nullptr;
		if (!Win) { C->Expect(false, TEXT("окно не открылось повторно")); Finish(C); return; }
		if (UEditableText* E = Cast<UEditableText>(Win->GetWidgetFromName(TEXT("CARAMAKE_NAMEINPUT")))) { E->SetText(FText::GetEmpty()); }
		FVector2D Mn, Mx;
		if (!AsdaUi::WidgetAbsRect(Win->GetWidgetFromName(TEXT("CARAMAKE_OKBUT")), Mn, Mx)) { C->Expect(false, TEXT("нет рамки «Создать»")); Finish(C); return; }
		Click((Mn + Mx) * 0.5);
		After(C, 0.5f, &StageOkMsg);
	}
	// Этап: после клика «Отмена» экран закрыт; открыть снова.
	static void StageCancelDone(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get();
		C->Expect(G && !FAcc::OnCreateScreen(G), TEXT("настоящий клик «Отмена»: экран создания закрыт"));
		if (G) { FAcc::Show(G); }
		After(C, 1.0f, &StageReopened);
	}

	// Этап: все видимые кнопки окна — центр не перекрыт чужим виджетом.
	static void StageAllButtons(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get(); UUserWidget* Win = G ? FAcc::Win(G) : nullptr;
		if (!Win) { Finish(C); return; }
		TArray<UWidget*> All; Win->WidgetTree->GetAllWidgets(All);
		int32 N = 0, Bad = 0; FString BadList;
		for (UWidget* X : All)
		{
			UButton* B = Cast<UButton>(X);
			if (!B || !B->IsVisible()) { continue; }
			bool bShown = true;
			for (UWidget* A = B->GetParent(); A; A = A->GetParent()) { bShown &= A->IsVisible(); }
			FVector2D Mn, Mx;
			if (!bShown || !AsdaUi::WidgetAbsRect(B, Mn, Mx) || (Mx - Mn).X < 1.0) { continue; }
			++N;
			const UWidget* Top = nullptr; const FString T = TopAt((Mn + Mx) * 0.5, Win, &Top);
			if (Top != B) { ++Bad; BadList += FString::Printf(TEXT(" %s←%s"), *B->GetName(), *T); }
		}
		C->Expect(Bad == 0, FString::Printf(TEXT("кнопки окна: %d, перекрыто в центре %d:%s"), N, Bad, *BadList));
		FVector2D Mn, Mx;
		if (!AsdaUi::WidgetAbsRect(Win->GetWidgetFromName(TEXT("CARAMAKE_CANCELBUT")), Mn, Mx)) { Finish(C); return; }
		Click((Mn + Mx) * 0.5);
		After(C, 0.7f, &StageCancelDone);
	}

	// Переключатель списка: последняя кнопка в родителе списка.
	static UButton* FindToggle(UPanelWidget* Drop)
	{
		UPanelWidget* CP = Drop ? Drop->GetParent() : nullptr;
		if (!CP) { return nullptr; }
		for (int32 i = CP->GetChildrenCount() - 1; i >= 0; --i) { if (UButton* B = Cast<UButton>(CP->GetChildAt(i))) { return B; } }
		return nullptr;
	}
	// Кнопка пункта списка №Sign (1..12).
	static UButton* ItemBtn(UPanelWidget* Drop, int32 Sign)
	{
		int32 K = 0;
		for (int32 i = 0; i < Drop->GetChildrenCount(); ++i) { if (UButton* B = Cast<UButton>(Drop->GetChildAt(i))) { if (++K == Sign) { return B; } } }
		return nullptr;
	}

	static void StageSignOpen(FCtxRef C);
	// Этап: проверить выбор знака.
	static void StageSignCheck(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get(); if (!G) { Finish(C); return; }
		UPanelWidget* Drop = FAcc::Drop(G);
		const bool bClosed = Drop && Drop->GetVisibility() == ESlateVisibility::Collapsed;
		const FString Want = FAsdaCharRecord::ConstellationName((uint8)C->Sign);
		const FString Shown = FAcc::Display(G) ? FAcc::Display(G)->GetText().ToString() : TEXT("");
		C->Expect(FAcc::Picked(G) == C->Sign && bClosed && Shown == Want && C->PressCell == 0,
			FString::Printf(TEXT("созвездие %d «%s» точка %d: принято %d, показано «%s», список закрыт=%d, нажатий в сетку %d"),
				C->Sign, *Want, C->XIdx, FAcc::Picked(G), *Shown, bClosed ? 1 : 0, C->PressCell));
		if (!bClosed && Drop) { Drop->SetVisibility(ESlateVisibility::Collapsed); }   // провал не тянем на следующий знак
		C->PressCell = 0;
		if (++C->XIdx >= 3) { C->XIdx = 0; ++C->Sign; }
		if (C->Sign > 12) { FAcc::ClearPick(G); After(C, kStep, &StageAllButtons); return; }
		After(C, kStep, &StageSignOpen);
	}
	// Этап: настоящий клик по пункту.
	static void StageSignPick(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get(); UPanelWidget* Drop = G ? FAcc::Drop(G) : nullptr;
		UButton* B = Drop ? ItemBtn(Drop, C->Sign) : nullptr;
		FVector2D Mn, Mx;
		if (!B || !AsdaUi::WidgetAbsRect(B, Mn, Mx)) { C->Expect(false, FString::Printf(TEXT("созвездие %d: нет пункта"), C->Sign)); Finish(C); return; }
		static const double Fx[3] = { 0.08, 0.5, 0.92 };
		const FVector2D P(Mn.X + (Mx.X - Mn.X) * Fx[C->XIdx], (Mn.Y + Mx.Y) * 0.5);
		if (C->XIdx == 1) { UE_LOG(LogAsdaCreateClicks, Display, TEXT("[создание] пункт %d рамка %.0f,%.0f..%.0f,%.0f, верхний: %s"), C->Sign, Mn.X, Mn.Y, Mx.X, Mx.Y, *TopAt(P, FAcc::Win(G))); }
		Click(P);
		After(C, kStep, &StageSignCheck);
	}
	// Этап: открыть список настоящим кликом.
	static void StageSignOpen(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get(); UPanelWidget* Drop = G ? FAcc::Drop(G) : nullptr;
		UButton* T = FindToggle(Drop);
		FVector2D Mn, Mx;
		if (!T || !AsdaUi::WidgetAbsRect(T, Mn, Mx)) { C->Expect(false, TEXT("нет переключателя списка")); Finish(C); return; }
		FAcc::ClearPick(G);
		Click((Mn + Mx) * 0.5);
		After(C, kStep, &StageSignPick);
	}

	static void StageAreaStart(FCtxRef C);
	static void StageAreaRelease(FCtxRef C);
	// Этап: настоящее нажатие в очередной точке площади.
	static void StageAreaPress(FCtxRef C)
	{
		const FVector2D P = C->Pts[C->PtIdx];
		Move(P); Btn(P, true);
		After(C, kStep * 0.5f, &StageAreaRelease);
	}
	// Этап: отпускание в стороне — клик не срабатывает, окно остаётся.
	static void StageAreaRelease(FCtxRef C)
	{
		const FVector2D Off(2.0, 2.0);
		Move(Off); Btn(Off, false);
		if (++C->PtIdx < C->Pts.Num()) { After(C, kStep * 0.5f, &StageAreaPress); return; }
		const int32 Got = C->bCancelBtn ? C->PressCancel : C->PressOk;
		C->Expect(Got == C->Pts.Num(), FString::Printf(TEXT("%s: настоящих нажатий %d из %d точек"), C->bCancelBtn ? TEXT("«Отмена»") : TEXT("«Создать»"), Got, C->Pts.Num()));
		if (!C->bCancelBtn) { C->bCancelBtn = true; After(C, kStep, &StageAreaStart); return; }
		C->Sign = 1; C->XIdx = 0; C->PressCell = 0;
		After(C, kStep, &StageSignOpen);
	}
	// Этап: карта верхних виджетов по площади кнопки и очередь настоящих нажатий.
	static void StageAreaStart(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get(); UUserWidget* Win = G ? FAcc::Win(G) : nullptr;
		const TCHAR* Name = C->bCancelBtn ? TEXT("CARAMAKE_CANCELBUT") : TEXT("CARAMAKE_OKBUT");
		UWidget* Self = Win ? Win->GetWidgetFromName(Name) : nullptr;
		FVector2D Mn, Mx;
		if (!Self || !AsdaUi::WidgetAbsRect(Self, Mn, Mx)) { C->Expect(false, FString::Printf(TEXT("нет рамки %s"), Name)); Finish(C); return; }
		TMap<FString, int32> Hist; int32 Tot = 0, Mine = 0;
		for (double Y = Mn.Y + 1.0; Y <= Mx.Y - 1.0; Y += 3.0)
		{
			for (double X = Mn.X + 1.0; X <= Mx.X - 1.0; X += 3.0)
			{
				const UWidget* Top = nullptr; const FString T = TopAt(FVector2D(X, Y), Win, &Top);
				Hist.FindOrAdd(T)++; ++Tot; if (Top == Self) { ++Mine; }
			}
		}
		FString H; for (const auto& It : Hist) { H += FString::Printf(TEXT(" %s=%d"), *It.Key, It.Value); }
		C->Expect(Mine == Tot, FString::Printf(TEXT("%s рамка %.1f,%.1f..%.1f,%.1f: своя площадь %d из %d точек;%s"), Name, Mn.X, Mn.Y, Mx.X, Mx.Y, Mine, Tot, *H));
		C->Pts.Reset();
		for (int32 Iy = 0; Iy < 3; ++Iy) { for (int32 Ix = 0; Ix < 7; ++Ix) { C->Pts.Add(FVector2D(Mn.X + 2.0 + (Mx.X - Mn.X - 4.0) * Ix / 6.0, Mn.Y + 2.0 + (Mx.Y - Mn.Y - 4.0) * Iy / 2.0)); } }
		C->PtIdx = 0;
		(C->bCancelBtn ? C->PressCancel : C->PressOk) = 0;
		StageAreaPress(C);
	}

	// Счётчик настоящих нажатий на кнопке (штатные обработчики остаются).
	static void Counter(FCtxRef C, UButton* B, TFunction<void()> Fn)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(B);
		R->PressCallback = [Fn](int32, bool bDown) { if (bDown) { Fn(); } };
		B->OnPressed.AddDynamic(R, &UAsdaButtonRelay::OnPressed);
		C->Relays.Emplace(R);
	}

	// Этап: кадр снят, список закрывается настоящим кликом, начало проверок.
	static void StageShotDone(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get();
		UButton* T = G ? FindToggle(FAcc::Drop(G)) : nullptr;
		FVector2D Mn, Mx;
		if (T && AsdaUi::WidgetAbsRect(T, Mn, Mx)) { Click((Mn + Mx) * 0.5); }
		C->bCancelBtn = false;
		After(C, 0.3f, &StageAreaStart);
	}
	// Этап: список открыт, кадр.
	static void StageShotOpen(FCtxRef C)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("caramake_list_open"), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		After(C, 1.0f, &StageShotDone);
	}
	// Этап: кадр с закрытым списком снят, список открывается настоящим кликом.
	static void StageShotClosed(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get();
		UButton* T = G ? FindToggle(FAcc::Drop(G)) : nullptr;
		FVector2D Mn, Mx;
		if (T && AsdaUi::WidgetAbsRect(T, Mn, Mx)) { Click((Mn + Mx) * 0.5); }
		After(C, 0.5f, &StageShotOpen);
	}

	// Этап: окно открыто — дамп, счётчики, начало проверок.
	static void StageReady(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get(); UUserWidget* Win = G ? FAcc::Win(G) : nullptr;
		if (!Win) { C->Expect(false, TEXT("окно создания не открылось")); Finish(C); return; }
		Dump(C, Win);
		TArray<UWidget*> All; Win->WidgetTree->GetAllWidgets(All);
		FCtx* P = &C.Get();
		for (UWidget* X : All)
		{
			UButton* B = Cast<UButton>(X);
			if (!B) { continue; }
			const FString N = B->GetName();
			if (N == TEXT("CARAMAKE_OKBUT")) { Counter(C, B, [P]() { ++P->PressOk; }); }
			else if (N == TEXT("CARAMAKE_CANCELBUT")) { Counter(C, B, [P]() { ++P->PressCancel; }); }
			else if (N.StartsWith(TEXT("CARAMAKE_ICONBOX_HIT"))) { Counter(C, B, [P]() { ++P->PressCell; }); }
		}
		C->bCancelBtn = false;
		if (C->bShot)
		{
			FScreenshotRequest::RequestScreenshot(TEXT("caramake_list_closed"), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			After(C, 1.0f, &StageShotClosed);
			return;
		}
		StageAreaStart(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaLoginSelfTestCreateClicks(
	TEXT("asda.Login.SelfTestCreateClicks"),
	TEXT("asda.Login.SelfTestCreateClicks [dump=<файл>] [delay=N] [quit] — ОФЛАЙН-самотест 06.11.2026: окно создания персонажа настоящими кликами: ")
	TEXT("вся площадь «Создать»/«Отмена», 12 созвездий × 3 точки, центры всех кнопок, закрытие и окно сообщения. На карте логина."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaCreateClicksLocal;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KitArgs.bQuit;
		for (const FString& P : KitArgs.Pos) { if (P.StartsWith(TEXT("dump="))) { C->DumpFile = P.Mid(5); } }
		C->bShot = KitArgs.Has(TEXT("shot"));
		auto Go = [C]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			AAsdaLoginGameMode* G = C->W.IsValid() ? Cast<AAsdaLoginGameMode>(C->W->GetAuthGameMode()) : nullptr;
			if (!G) { AsdaSelfTestKit::Refused(C->W.Get(), kCmd, C->bQuit, TEXT("нет AAsdaLoginGameMode")); return; }
			C->G = G;
			FAsdaCreateClicksAccess::Show(G);
			After(C, 2.0f, &StageReady);
		};
		if (KitArgs.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), KitArgs.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
