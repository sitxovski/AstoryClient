// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ОКНО МОЖНО ДЕРЖАТЬ ЧАСТИЧНО ЗА КРАЕМ» — `asda.UI.SelfTestWindowDrag [check] [file=<путь>] [delay=N] [quit]`
//   (04.11.2026, жалоба игрока через владельца: «окно нельзя отодвинуть за край экрана — отскакивает назад»).
//   Правило — у `CVarOffscreenKeepGrab` в AsdaWindowSubsystem.cpp (оригинал: клампа нет, ui_core.c:114010
//   `uiControl_RepositionByDelta@0x12F1C30`; схваченная точка остаётся на экране, т. к. `SetCapture` клиент не зовёт).
//   Путь игрока: НАСТОЯЩЕЕ перетаскивание Slate (`SimulateDrag`, тот же путь, что мышь) за ручку окна, геометрия — по
//   `GetCachedGeometry` ручки после отрисовки (`DebugGetDragHandleRect`), экран — геометрия самого окна во вьюпорте.
//   Окна: настройки 69500 `SYSTEM_Option_NEW`, сумка 400 `INVEN_WIN`, сообщество 6400 `COMMUNITY_WIN` — по одному.
//   Режим прогона (без `check`), для каждого окна:
//     1) за левый / правый / верхний / нижний край: ручка уезжает ровно на дельту курсора (±1.5 px) — окно НЕ отскочило,
//        рамка частично за экраном, ручка досягаема (кусок ≥ порогов `asda.UI.OffscreenMinGrabPx`/`…Y`);
//        затем перетаскивание ЗА ВИДИМЫЙ кусок ручки обратно — ручка возвращается на место (заголовок схватывается);
//     2) «слишком далеко» влево и вверх: ручка подтянута ровно до порога (24 / 8 px на экране);
//     3) итоговая позиция частично за краем (настройки — справа, сумка — снизу, сообщество — слева) пишется в ini;
//     4) все три окна открыты: `r.SetRes 800x600w` → у каждого ручка досягаема; обратно на исходный размер → окна там же,
//        где их оставили (сохранённая позиция вернулась); ini копируется в `file=` (для режима `check`).
//   Режим `check` (новый запуск игры с тем ini): окна открываются там же, где оставлены (частично за краем), ручка досягаема.
//   Офлайн (-AsdaNoNet); исходящие не шлёт (окна открываются штатными подсистемами, перетаскивание — Slate).
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"            // ручки, SimulateDrag, геометрия окна
#include "UI/AsdaJuiWindowMeta.h"              // ★ R5-fix8-b 14.11.2026: имена ручек из меты JUI (REVIEW6 М-5)
#include "UI/AsdaSystemOptionSubsystem.h"      // настройки 69500
#include "UI/AsdaInventorySubsystem.h"         // сумка 400
#include "UI/AsdaCommunitySubsystem.h"         // сообщество 6400
#include "UI/AsdaUiHelpers.h"                  // AsdaUi::WidgetAbsRect
#include "Test/AsdaSelfTestKit.h"              // каркас: офлайн-гейт, итог, выход
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаги по событию «ввод доставлен», а не по часам
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"                    // ★ R5-fix-b 13.11.2026 (заключительный заход): узлы окна (показана ли ручка)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"                   // поиск ini раскладки (копия для режима check)
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWinDragTest, Log, All);   // журнал самотеста

namespace AsdaWinDragLocal
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestWindowDrag");   // имя команды (итог, ключи)

	// Проверяемое окно: id, как открыть/закрыть, и куда оставить его в конце (край: 0 лево, 1 право, 3 низ).
	struct FWin
	{
		FName Id;          // окно в реестре
		int32 FinalEdge;   // край итоговой позиции
	};

	// Прямоугольник (абсолютные пиксели Slate).
	struct FRect
	{
		FVector2D Mn = FVector2D::ZeroVector;   // левый-верх
		FVector2D Mx = FVector2D::ZeroVector;   // правый-низ
		FVector2D Size() const { return Mx - Mn; }
	};

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;      // мир
		bool bQuit = false;            // выйти после прогона
		bool bCheck = false;           // режим проверки после перезапуска
		FString File;                  // файл итоговых позиций / копия ini
		int32 Ok = 0, Fail = 0;        // счёт
		TArray<FWin> Wins;             // окна
		int32 WinIdx = 0;              // текущее окно
		int32 Case = 0;                // текущий случай
		FRect H0;                      // ручка до случая
		FVector2D Delta;               // заказанная дельта
		FIntPoint OrigRes;             // размер вьюпорта до смены
		TMap<FName, FRect> Final;      // итоговые ручки (прогон) / ожидаемые (check)
		TArray<FString> Table;         // таблица в журнал

		void Expect(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaWinDragTest, Display, TEXT("[самотест окон] %s %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *What);
		}
	};

	static UGameInstance* GIOf(const TSharedRef<FCtx>& C) { return C->W.Get() ? C->W->GetGameInstance() : nullptr; }
	static UAsdaWindowSubsystem* WMOf(const TSharedRef<FCtx>& C) { UGameInstance* GI = GIOf(C); return GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr; }

	// Значение целого ключа консоли (пороги правила; читаем по имени — это дев-самотест, не рабочий код).
	static int32 CvInt(const TCHAR* Name, int32 Def)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetInt() : Def;
	}

	// Открыть окно штатной подсистемой.
	static void OpenWin(const TSharedRef<FCtx>& C, FName Id)
	{
		UGameInstance* GI = GIOf(C);
		if (!GI) { return; }
		if (Id == FName(TEXT("SYSTEM_Option_NEW"))) { if (auto* S = GI->GetSubsystem<UAsdaSystemOptionSubsystem>()) { if (!S->IsOptionsShown()) { S->ShowOptions(); } } }
		else if (Id == FName(TEXT("INVEN_WIN"))) { if (auto* S = GI->GetSubsystem<UAsdaInventorySubsystem>()) { if (!S->IsWindowOpen()) { S->OpenWindow(); } } }
		else if (Id == FName(TEXT("COMMUNITY_WIN"))) { if (auto* S = GI->GetSubsystem<UAsdaCommunitySubsystem>()) { if (!S->IsWindowOpen()) { S->OpenOnTab(0); /* ★ R5-fix6-c (13.11.2026): вкладку 0 ставит сам тест штатным путём (хоткей 35), а не берёт оставшуюся от соседа пачки */ } } }
	}

	// Закрыть окно штатной подсистемой.
	static void CloseWin(const TSharedRef<FCtx>& C, FName Id)
	{
		UGameInstance* GI = GIOf(C);
		if (!GI) { return; }
		if (Id == FName(TEXT("SYSTEM_Option_NEW"))) { if (auto* S = GI->GetSubsystem<UAsdaSystemOptionSubsystem>()) { if (S->IsOptionsShown()) { S->CloseOptions(false); } } }
		else if (Id == FName(TEXT("INVEN_WIN"))) { if (auto* S = GI->GetSubsystem<UAsdaInventorySubsystem>()) { if (S->IsWindowOpen()) { S->CloseWindow(); } } }
		else if (Id == FName(TEXT("COMMUNITY_WIN"))) { if (auto* S = GI->GetSubsystem<UAsdaCommunitySubsystem>()) { if (S->IsWindowOpen()) { S->CloseWindow(); } } }
	}

	// Экран = геометрия окна-виджета во вьюпорте (полноэкранный слот).
	static bool ViewRect(const TSharedRef<FCtx>& C, FName Id, FRect& Out)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UUserWidget* Wd = WM ? WM->FindWindow(Id) : nullptr;
		return Wd && AsdaUi::WidgetAbsRect(Wd, Out.Mn, Out.Mx) && Out.Size().X > 0.0;
	}

	// ★ R5-fix-b 13.11.2026 (заключительный заход): ручка ПОКАЗАНА — в дереве окна есть узел ровно с этим прямоугольником, показанный сам и со всеми
	//   предками. `DebugGetDragHandleRect` видимость не смотрит, а у скрытого узла остаётся прежняя геометрия: в повторах прогона
	//   `rx_r5fix3` у сообщества 6400 крупнейшей «ручкой» был прямоугольник 785×24 при окне шириной 404 — за него тест и хватал
	//   (нажатие уходило во вьюпорт, сдвиг 0, семь провалов). Мышь игрока скрытую ручку не ловит. Дерева нет — как было.
	static bool ShownRect(const TSharedRef<FCtx>& C, FName Id, const FRect& R)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UUserWidget* Wd = WM ? WM->FindWindow(Id) : nullptr;
		if (!Wd || !Wd->WidgetTree) { return true; }
		auto ShownChain = [](const UWidget* X) { bool b = true; for (const UWidget* P = X; P && b; P = P->GetParent()) { b = AsdaUi::IsShown(P); } return b; };
		// ★ R5-fix8-b 14.11.2026 (REVIEW6 М-5): ручку меты JUI опознаём по ИМЕНИ (то же, что берёт `DebugGetDragHandleRect`), а не по совпадению
		//   прямоугольника с любым узлом. Окно без меты JUI или ручка, чьё имя в мете не нашлось или не совпало прямоугольником, —
		//   прежним (нестрогим) поиском по прямоугольнику ниже (добавленные порту ручки лежат в той же мете с `bAddedByPort`). ★ R5-fix8-b 14.11.2026 (пятая запись, ревизия REVIEW8b) (М-4)
		const UAsdaJuiWindowMetaAsset* Meta = WM->GetJuiMeta();
		if (const FAsdaJuiWindowMeta* M = Meta ? Meta->FindWindow(Id) : nullptr)
		{
			for (const FAsdaJuiDragHandle& H : M->DragHandles)
			{
				const UWidget* X = Wd->GetWidgetFromName(H.HandleWidget);
				FVector2D Mn, Mx;
				if (!X || !AsdaUi::WidgetAbsRect(X, Mn, Mx) || (Mn - R.Mn).GetAbsMax() > 0.5 || (Mx - R.Mx).GetAbsMax() > 0.5) { continue; }
				// ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-7): ручка, добавленная порту (`bAddedByPort`), при `asda.UI.AddedDragHandles 0` выключена — не живая.
				if (H.bAddedByPort && CvInt(TEXT("asda.UI.AddedDragHandles"), 1) == 0) { return false; }
				return ShownChain(X);   // ручка найдена по имени — её видимость и есть ответ
			}
		}
		TArray<UWidget*> All;
		Wd->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All)
		{
			FVector2D Mn, Mx;
			if (!AsdaUi::WidgetAbsRect(X, Mn, Mx) || (Mn - R.Mn).GetAbsMax() > 0.5 || (Mx - R.Mx).GetAbsMax() > 0.5) { continue; }
			bool bShown = true;
			for (const UWidget* P = X; P && bShown; P = P->GetParent()) { bShown = AsdaUi::IsShown(P); }
			if (bShown) { return true; }
		}
		return false;
	}

	// Самая крупная видимая ручка окна (у окон бывает несколько; берём ручку корня — наибольшую).
	static bool HandleRect(const TSharedRef<FCtx>& C, FName Id, FRect& Out, int32* OutIdx = nullptr)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { return false; }
		double Best = -1.0;
		for (int32 i = 0; i < 16; ++i)
		{
			FRect R;
			if (!WM->DebugGetDragHandleRect(Id, i, R.Mn, R.Mx) || !ShownRect(C, Id, R)) { continue; }   // ★ R5-fix-b 13.11.2026 (заключительный заход): скрытые ручки не в счёт
			const double A = R.Size().X * R.Size().Y;
			if (A > Best) { Best = A; Out = R; if (OutIdx) { *OutIdx = i; } }
		}
		return Best > 0.0;
	}

	// Видимый кусок ручки по осям.
	static FVector2D Visible(const FRect& H, const FRect& V)
	{
		return FVector2D(FMath::Max(0.0, FMath::Min(H.Mx.X, V.Mx.X) - FMath::Max(H.Mn.X, V.Mn.X)),
			FMath::Max(0.0, FMath::Min(H.Mx.Y, V.Mx.Y) - FMath::Max(H.Mn.Y, V.Mn.Y)));
	}

	// Нужный кусок ручки (пороги правила, не больше ручки).
	static FVector2D Need(const FRect& H)
	{
		return FVector2D(FMath::Min((double)CvInt(TEXT("asda.UI.OffscreenMinGrabPx"), 24), H.Size().X),
			FMath::Min((double)CvInt(TEXT("asda.UI.OffscreenMinGrabPxY"), 8), H.Size().Y));
	}

	// Ручка досягаема.
	static bool Reachable(const FRect& H, const FRect& V)
	{
		const FVector2D Vi = Visible(H, V), Ne = Need(H);
		return Vi.X >= Ne.X - 0.6 && Vi.Y >= Ne.Y - 0.6;
	}

	static FString RS(const FRect& R) { return FString::Printf(TEXT("%.0f,%.0f..%.0f,%.0f"), R.Mn.X, R.Mn.Y, R.Mx.X, R.Mx.Y); }

	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// ★ R5-fix-b 13.11.2026 (заключительный заход): проверка после перетаскивания — когда оно ДОСТАВЛЕНО (отпускание), через 3 кадра и 0,3 с, а не через
	//   1,0 с от заказа. 20 кадров удержания при ~20 к/с (три игры разом, прогон `rx_r5fix3`) — это 1 с: проверка читала ручку
	//   до отпускания (сдвиг короче дельты курсора; 16, 7, 7 провалов в трёх попытках подряд). Потолок 20 с.
	static void AfterDrag(TSharedRef<FCtx> C, void (*Stage)(TSharedRef<FCtx>))
	{
		AsdaSelfTestSeq::AfterInput(C->W.Get(), [C, Stage]() { Stage(C); }, 0.3f, 3);
	}

	// Случаи перетаскивания: 0 лево, 1 право, 2 верх, 3 низ, 4 слишком влево, 5 слишком вверх, 6 итоговая позиция.
	static const TCHAR* CaseName(int32 K)
	{
		static const TCHAR* N[] = { TEXT("за левый край"), TEXT("за правый край"), TEXT("за верхний край"), TEXT("за нижний край"),
			TEXT("слишком далеко влево"), TEXT("слишком далеко вверх"), TEXT("итоговая позиция") };
		return (K >= 0 && K < 7) ? N[K] : TEXT("?");
	}

	// Точка захвата и точка отпускания для случая K (обе на экране, как у мыши оригинала).
	static void CasePoints(int32 K, int32 FinalEdge, const FRect& H, const FRect& V, FVector2D& From, FVector2D& To)
	{
		const FVector2D Cn = (H.Mn + H.Mx) * 0.5;
		const double Gx = FMath::Min(40.0, H.Size().X * 0.5);   // отступ захвата от края ручки
		const int32 Edge = (K == 6) ? FinalEdge : K;
		switch (Edge)
		{
		case 0: From = FVector2D(H.Mx.X - Gx, Cn.Y); To = FVector2D(V.Mn.X + 10, Cn.Y); break;        // ручка: левый кусок за краем
		case 1: From = FVector2D(H.Mn.X + Gx, Cn.Y); To = FVector2D(V.Mx.X - 10, Cn.Y); break;        // правый кусок за краем
		case 2: From = Cn; To = FVector2D(Cn.X, V.Mn.Y + 4); break;                                    // верхняя половина за краем
		case 3: From = Cn; To = FVector2D(Cn.X, V.Mx.Y - 6); break;                                    // окно ниже экрана
		case 4: From = FVector2D(H.Mx.X - 4, Cn.Y); To = FVector2D(V.Mn.X + 2, Cn.Y); break;          // виден 6 px < 24
		case 5: From = FVector2D(Cn.X, H.Mx.Y - 2); To = FVector2D(Cn.X, V.Mn.Y + 1); break;          // виден 3 px < 8
		default: From = To = Cn; break;
		}
	}

	static void StageCase(TSharedRef<FCtx> C);
	static void StageNextWin(TSharedRef<FCtx> C);
	static void StageAllOpen(TSharedRef<FCtx> C);

	static void Finish(TSharedRef<FCtx> C)
	{
		// ★ R5-fix8-b 14.11.2026 (четвёртая запись; ★ R5-fix8-b 14.11.2026 (пятая запись, ревизия REVIEW8b), М-5 — на всех выходах): без `file=` (пачка) — раскладка своих окон
		//   к умолчанию: следующий тест не должен получать сумку частично за краем (`SelfTest.DragDelete` после этого теста — 39/49).
		//   С `file=` (пара прогон → `check`) и в режиме `check` раскладка остаётся.
		if (!C->bCheck && C->File.IsEmpty())
		{
			if (UAsdaWindowSubsystem* WM = WMOf(C))
			{
				for (const FWin& Wn : C->Wins) { WM->ResetWindowLayout(Wn.Id); }
				WM->FlushWindowLayout();
				UE_LOG(LogAsdaWinDragTest, Display, TEXT("[самотест окон] уборка: раскладка %d окон возвращена к умолчанию"), C->Wins.Num());
			}
		}
		for (const FString& L : C->Table) { UE_LOG(LogAsdaWinDragTest, Display, TEXT("[самотест окон] | %s"), *L); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaWinDragTest, TEXT("SelfTestWindowDrag"), C->Ok, C->Fail);
	}

	// ── проверка «обратно за видимый кусок ручки» ──
	static void StageBackCheck(TSharedRef<FCtx> C)
	{
		const FWin& Wn = C->Wins[C->WinIdx];
		FRect H;
		const bool bH = HandleRect(C, Wn.Id, H);
		C->Expect(bH && (H.Mn - C->H0.Mn).GetAbsMax() <= 1.5,
			FString::Printf(TEXT("%s, %s: схвачен за видимый кусок и возвращён — ручка %s (было %s)"),
				*Wn.Id.ToString(), CaseName(C->Case), bH ? *RS(H) : TEXT("нет"), *RS(C->H0)));
		++C->Case;
		After(C, 0.3f, &StageCase);
	}

	// ── проверка после отпускания ──
	static void StageCaseCheck(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		const FWin& Wn = C->Wins[C->WinIdx];
		FRect H, V;
		if (!WM || !HandleRect(C, Wn.Id, H) || !ViewRect(C, Wn.Id, V))
		{
			C->Expect(false, FString::Printf(TEXT("%s, %s: нет ручки/экрана после отпускания"), *Wn.Id.ToString(), CaseName(C->Case)));
			CloseWin(C, Wn.Id); ++C->WinIdx; After(C, 0.5f, &StageNextWin); return;
		}
		const FVector2D Moved = H.Mn - C->H0.Mn;
		const FVector2D Vi = Visible(H, V), Ne = Need(H);
		const bool bReach = Reachable(H, V);
		const bool bOff = H.Mn.X < V.Mn.X - 0.5 || H.Mn.Y < V.Mn.Y - 0.5 || H.Mx.X > V.Mx.X + 0.5 || H.Mx.Y > V.Mx.Y + 0.5
			|| C->Case == 3 || (C->Case == 6 && Wn.FinalEdge == 3);   // снизу за край уходит тело окна, не ручка
		bool bPos = false;
		FString Want;
		if (C->Case == 4)   // подтянута до порога: правый край ручки = левый край экрана + порог
		{
			bPos = FMath::Abs(H.Mx.X - (V.Mn.X + Ne.X)) <= 1.5 && FMath::Abs(Moved.Y - C->Delta.Y) <= 1.5;
			Want = FString::Printf(TEXT("правый край ручки %.0f (= экран + %.0f)"), V.Mn.X + Ne.X, Ne.X);
		}
		else if (C->Case == 5)   // подтянута до порога: низ ручки = верх экрана + порог
		{
			bPos = FMath::Abs(H.Mx.Y - (V.Mn.Y + Ne.Y)) <= 1.5 && FMath::Abs(Moved.X - C->Delta.X) <= 1.5;
			Want = FString::Printf(TEXT("низ ручки %.0f (= экран + %.0f)"), V.Mn.Y + Ne.Y, Ne.Y);
		}
		else
		{
			bPos = (Moved - C->Delta).GetAbsMax() <= 1.5;
			Want = FString::Printf(TEXT("сдвиг (%.0f,%.0f) = дельта курсора, окно не отскочило"), C->Delta.X, C->Delta.Y);
		}
		C->Expect(bPos && bReach && bOff,
			FString::Printf(TEXT("%s, %s: ручка %s при экране %s, сдвиг (%.0f,%.0f); ожидание: %s; на экране %.0f×%.0f из нужных %.0f×%.0f; частично за краем=%d"),
				*Wn.Id.ToString(), CaseName(C->Case), *RS(H), *RS(V), Moved.X, Moved.Y, *Want, Vi.X, Vi.Y, Ne.X, Ne.Y, (int32)bOff));
		C->Table.Add(FString::Printf(TEXT("%-18s | %-22s | ручка %s | экран %s | видно %.0f×%.0f"),
			*Wn.Id.ToString(), CaseName(C->Case), *RS(H), *RS(V), Vi.X, Vi.Y));
		if (C->Case == 6)   // итог: запомнить и закрыть окно
		{
			C->Final.Add(Wn.Id, H);
			CloseWin(C, Wn.Id);
			++C->WinIdx;
			After(C, 0.6f, &StageNextWin);
			return;
		}
		// Обратно: схватить за середину ВИДИМОГО куска ручки и вернуть на исходное место.
		const FVector2D VisMn(FMath::Max(H.Mn.X, V.Mn.X), FMath::Max(H.Mn.Y, V.Mn.Y));
		const FVector2D VisMx(FMath::Min(H.Mx.X, V.Mx.X), FMath::Min(H.Mx.Y, V.Mx.Y));
		const FVector2D G = (VisMn + VisMx) * 0.5;
		WM->SimulateDrag(G, G + (C->H0.Mn - H.Mn), 20);
		AfterDrag(C, &StageBackCheck);
	}

	// ── случай перетаскивания ──
	static void StageCase(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		const FWin& Wn = C->Wins[C->WinIdx];
		FRect H, V;
		if (!WM || !HandleRect(C, Wn.Id, H) || !ViewRect(C, Wn.Id, V))
		{
			C->Expect(false, FString::Printf(TEXT("%s: окно не открылось или нет ручки"), *Wn.Id.ToString()));
			CloseWin(C, Wn.Id); ++C->WinIdx; After(C, 0.5f, &StageNextWin); return;
		}
		C->H0 = H;
		FVector2D From, To;
		CasePoints(C->Case, Wn.FinalEdge, H, V, From, To);
		C->Delta = To - From;
		WM->SimulateDrag(From, To, 20);
		AfterDrag(C, &StageCaseCheck);
	}

	// ── следующее окно (или все сразу) ──
	static void StageNextWin(TSharedRef<FCtx> C)
	{
		if (C->WinIdx >= C->Wins.Num()) { StageAllOpen(C); return; }
		const FWin& Wn = C->Wins[C->WinIdx];
		if (UAsdaWindowSubsystem* WM = WMOf(C)) { WM->ResetWindowLayout(Wn.Id); }   // от дефолта — повторяемо
		OpenWin(C, Wn.Id);
		C->Case = 0;
		After(C, 1.2f, &StageCase);
	}

	// ── все три окна: смена разрешения и возврат ──
	static void StageResBack(TSharedRef<FCtx> C);
	static void StageRes800(TSharedRef<FCtx> C)
	{
		for (const FWin& Wn : C->Wins)
		{
			FRect H, V;
			const bool b = HandleRect(C, Wn.Id, H) && ViewRect(C, Wn.Id, V);
			C->Expect(b && Reachable(H, V), FString::Printf(TEXT("%s после r.SetRes 800x600w: ручка %s при экране %s — досягаема"),
				*Wn.Id.ToString(), b ? *RS(H) : TEXT("нет"), b ? *RS(V) : TEXT("нет")));
		}
		if (UWorld* W = C->W.Get()) { GEngine->Exec(W, *FString::Printf(TEXT("r.SetRes %dx%dw"), C->OrigRes.X, C->OrigRes.Y)); }
		After(C, 2.5f, &StageResBack);
	}
	static void StageResBack(TSharedRef<FCtx> C)
	{
		for (const FWin& Wn : C->Wins)
		{
			FRect H, V;
			const FRect* F = C->Final.Find(Wn.Id);
			const bool b = HandleRect(C, Wn.Id, H) && ViewRect(C, Wn.Id, V) && F;
			// Экран мог сместиться (оконный режим) — сравниваем позицию ручки относительно экрана.
			C->Expect(b && ((H.Mn - V.Mn) - (F->Mn - C->Final.FindRef(NAME_None).Mn)).GetAbsMax() <= 1.5 && Reachable(H, V),
				FString::Printf(TEXT("%s после возврата %dx%d: ручка %s (оставлена %s) — на месте, досягаема"),
					*Wn.Id.ToString(), C->OrigRes.X, C->OrigRes.Y, b ? *RS(H) : TEXT("нет"), F ? *RS(*F) : TEXT("нет")));
		}
		// Итог для режима check: позиции ручек относительно экрана + копия ini.
		if (UAsdaWindowSubsystem* WM = WMOf(C)) { WM->FlushWindowLayout(); }
		if (!C->File.IsEmpty())
		{
			FString S;
			const FRect V0 = C->Final.FindRef(NAME_None);
			for (const FWin& Wn : C->Wins)
			{
				const FRect* F = C->Final.Find(Wn.Id);
				if (F) { S += FString::Printf(TEXT("%s %.3f %.3f %.3f %.3f\n"), *Wn.Id.ToString(), F->Mn.X - V0.Mn.X, F->Mn.Y - V0.Mn.Y, F->Mx.X - V0.Mn.X, F->Mx.Y - V0.Mn.Y); }
			}
			FFileHelper::SaveStringToFile(S, *C->File);
			const FString IniCopy = C->File + TEXT(".GameUserSettings.ini");
			// В UE 5.8 `GGameUserSettingsIni` — имя конфига, не путь: файл — `<Saved>/Config/<платформа>/GameUserSettings.ini`.
			if (GConfig) { GConfig->Flush(false, GGameUserSettingsIni); }
			//   Подпапка платформы (`WindowsEditor` у -game редактора) — ищем файл под `GeneratedConfigDir()`.
			TArray<FString> Found;
			IFileManager::Get().FindFilesRecursive(Found, *FPaths::GeneratedConfigDir(), TEXT("GameUserSettings.ini"), true, false);
			const FString IniPath = Found.Num() > 0 ? FPaths::ConvertRelativePathToFull(Found[0]) : FString(TEXT("(нет GameUserSettings.ini)"));
			const bool bCopied = IPlatformFile::GetPlatformPhysical().CopyFile(*IniCopy, *IniPath);
			UE_LOG(LogAsdaWinDragTest, Display, TEXT("[самотест окон] итог записан в %s; ini %s → %s: %s"), *C->File, *IniPath, *IniCopy,
				bCopied ? TEXT("скопирован") : TEXT("НЕ скопирован"));
		}
		// ★ R5-fix8-b 14.11.2026 (четвёртая запись: уборка раскладки) — ★ R5-fix8-b 14.11.2026 (пятая запись, ревизия REVIEW8b) (М-5): уборка перенесена в `Finish` (все выходы).
		for (const FWin& Wn : C->Wins) { CloseWin(C, Wn.Id); }
		After(C, 0.5f, &Finish);
	}
	static void StageAllOpenCheck(TSharedRef<FCtx> C)
	{
		FRect V;
		if (!ViewRect(C, C->Wins[0].Id, V)) { C->Expect(false, TEXT("все окна: нет экрана")); Finish(C); return; }
		C->Final.Add(NAME_None, V);   // экран при итоге (для сравнения относительно экрана)
		for (const FWin& Wn : C->Wins)
		{
			FRect H;
			const FRect* F = C->Final.Find(Wn.Id);
			const bool b = HandleRect(C, Wn.Id, H) && F;
			C->Expect(b && (H.Mn - F->Mn).GetAbsMax() <= 1.5,
				FString::Printf(TEXT("%s повторно открыто: ручка %s (оставлена %s) — встало туда же, частично за краем"),
					*Wn.Id.ToString(), b ? *RS(H) : TEXT("нет"), F ? *RS(*F) : TEXT("нет")));
		}
		const FVector2D Sz = V.Size();
		C->OrigRes = FIntPoint(FMath::RoundToInt(Sz.X), FMath::RoundToInt(Sz.Y));
		if (UWorld* W = C->W.Get()) { GEngine->Exec(W, TEXT("r.SetRes 800x600w")); }
		After(C, 2.5f, &StageRes800);
	}
	static void StageAllOpen(TSharedRef<FCtx> C)
	{
		for (const FWin& Wn : C->Wins) { OpenWin(C, Wn.Id); }
		After(C, 1.5f, &StageAllOpenCheck);
	}

	// ── режим check (после перезапуска) ──
	static void StageCheckRun(TSharedRef<FCtx> C)
	{
		FString S;
		if (!FFileHelper::LoadFileToString(S, *C->File)) { C->Expect(false, FString::Printf(TEXT("нет файла итога %s"), *C->File)); Finish(C); return; }
		TArray<FString> Lines;
		S.ParseIntoArrayLines(Lines);
		for (const FString& L : Lines)
		{
			TArray<FString> P;
			L.ParseIntoArrayWS(P);
			if (P.Num() < 5) { continue; }
			const FName Id(*P[0]);
			FRect Want, H, V;
			Want.Mn = FVector2D(FCString::Atod(*P[1]), FCString::Atod(*P[2]));
			Want.Mx = FVector2D(FCString::Atod(*P[3]), FCString::Atod(*P[4]));
			const bool b = HandleRect(C, Id, H) && ViewRect(C, Id, V);
			const FRect Rel{ H.Mn - V.Mn, H.Mx - V.Mn };
			C->Expect(b && (Rel.Mn - Want.Mn).GetAbsMax() <= 1.5 && Reachable(H, V),
				FString::Printf(TEXT("%s после ПЕРЕЗАПУСКА: ручка (от экрана) %s, оставлена %s — та же позиция, досягаема, экран %s"),
					*Id.ToString(), b ? *RS(Rel) : TEXT("нет"), *RS(Want), b ? *RS(V) : TEXT("нет")));
		}
		for (const FWin& Wn : C->Wins) { CloseWin(C, Wn.Id); }
		After(C, 0.5f, &Finish);
	}

	static void Run(TSharedRef<FCtx> C)
	{
		C->Wins = { { FName(TEXT("SYSTEM_Option_NEW")), 1 }, { FName(TEXT("INVEN_WIN")), 3 }, { FName(TEXT("COMMUNITY_WIN")), 0 } };
		UE_LOG(LogAsdaWinDragTest, Display, TEXT("[самотест окон] старт: режим %s, asda.UI.OffscreenKeepGrab=%d, пороги %d×%d px, ini %s"),
			C->bCheck ? TEXT("ПРОВЕРКА ПОСЛЕ ПЕРЕЗАПУСКА") : TEXT("прогон"), CvInt(TEXT("asda.UI.OffscreenKeepGrab"), -1),
			CvInt(TEXT("asda.UI.OffscreenMinGrabPx"), -1), CvInt(TEXT("asda.UI.OffscreenMinGrabPxY"), -1), *GGameUserSettingsIni);
		if (C->bCheck)
		{
			for (const FWin& Wn : C->Wins) { OpenWin(C, Wn.Id); }
			After(C, 1.5f, &StageCheckRun);
			return;
		}
		C->WinIdx = 0;
		StageNextWin(C);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSelfTestWindowDrag(
	TEXT("asda.UI.SelfTestWindowDrag"),
	TEXT("asda.UI.SelfTestWindowDrag [check] [file=<путь>] [delay=N] [quit] — ОФЛАЙН-самотест 04.11.2026: окна (настройки 69500, сумка 400, ")
	TEXT("сообщество 6400) настоящим перетаскиванием частично за каждый край остаются где отпущены, ручка досягаема, обратно схватываются; ")
	TEXT("смена разрешения 800x600 и возврат; `check` — после перезапуска окна там же (файл итога file= от прогона)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, AsdaWinDragLocal::kCmd);
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaWinDragLocal::kCmd, KitArgs)) { return; }
		if (!W) { return; }
		TSharedRef<AsdaWinDragLocal::FCtx> C = MakeShared<AsdaWinDragLocal::FCtx>();
		C->W = W;
		C->bQuit = KitArgs.bQuit;
		C->bCheck = KitArgs.Has(TEXT("check"));
		for (const FString& P : KitArgs.Pos) { if (P.StartsWith(TEXT("file="))) { C->File = P.Mid(5); } }
		auto Go = [C]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(C->W.Get(), AsdaWinDragLocal::kCmd, C->bQuit)) { return; }
			AsdaWinDragLocal::Run(C);
		};
		if (KitArgs.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), KitArgs.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
