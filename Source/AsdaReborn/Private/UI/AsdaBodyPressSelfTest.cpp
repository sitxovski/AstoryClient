// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «НАЖАТИЕ ПО ТЕЛУ ОКНА» (06.11.2026): `asda.UI.SelfTestBodyPress [quit]`. Офлайн (`-AsdaNoNet`), карта мира.
//   Вопрос: доходит ли нажатие по телу настоящего окна до контролов окон ниже (эталон `uiWindowMgrDispatchMouse@0x12AF540` — нет).
//   Цели находятся на живом экране: сетка по вьюпорту, лист пути Slate в каждой точке, каждый лист окна реестра — цель
//   (панель быстрых слотов, трей меню, кнопки мини-карты, чат и всё, что открыто в HUD).
//   Кто взял нажатие — по отчёту самого Slate (`FSlateDebugging::InputEvent`: виджет, ответивший Handled).
//   Проба нажимает в точке, снимает захват мыши и отпускает вдали: `SButton` теряет нажатие и не кликает, побочных действий нет.
//   Шаги: БАЗА — сумка закрыта, цель берёт нажатие сама (ключ 1, то есть гейт свободные контролы не трогает).
//   ПОКРЫТИЕ — настоящая сумка `INVEN_WIN` за ручку (`SimulateDrag`) ставится телом на цель.
//   КЛЮЧ 0 — нажатие доходит до цели (дефект); КЛЮЧ 1 — ЛКМ, ПКМ, двойной щелчок не доходят; настоящий клик `SimulateClickAt` ничего
//   не открывает и не закрывает. Цель выше сумки (окно поверх всех) — нажатие остаётся у цели, это не дефект.
//   ПРИМЕР — настоящий клик сквозь сумку при ключе 0: листание панели срабатывает; трей окна не открывает (частная проверка меню).
//   КОНТРОЛЬ — свой контрол сумки, мир, потерянное отпускание, кнопки мыши у опроса.
//   ОПРОС — контролы, которые Slate нажатие не отдают (вкладки чата, лента, слоты панели): ключ `asda.UI.TopWindowPoll` 0 — виджет виден
//   под телом сумки, 1 — нет; слот панели — `HitTestSlotUnderCursor`, вкладка чата — настоящий клик и активная вкладка.
//   СКЛАД, ЛАВКА — те же пробы по целям окон `WARE_WIN` и `NPCSHOP_WIN`; колесо над списком склада под сумкой. Склад живёт в полосе
//   приоритетных окон, игрок сумку на него не положит: на этом этапе z сумки поднимает сам тест.
//   ФОКУС — поле окна, поймавшего нажатие, фокус теряет; поле чата под сумкой держит.
//   Итог: «ТЕЛО-ОКНА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"   // ★ R5-fix8-b 14.11.2026: счёт отказов мыши панели (ТРЕЙ0)
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableText.h"
#include "Debugging/SlateDebugging.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Layout/WidgetPath.h"
#include "TimerManager.h"
#include "Types/ReflectionMetadata.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBodyPressTest, Log, All);

namespace AsdaBodyPressTest
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestBodyPress");   // имя команды
	static const TCHAR* kKey = TEXT("asda.UI.TopWindowPress");      // проверяемый ключ нажатий Slate
	static const TCHAR* kPoll = TEXT("asda.UI.TopWindowPoll");      // проверяемый ключ опроса
	static const FName kBag(TEXT("INVEN_WIN"));                     // окно поверх целей
	static constexpr double kGrid = 6.0;                            // шаг сетки поиска целей, px

	// Цель: лист Slate окна реестра.
	struct FTarget
	{
		FName Win;                  // окно цели
		FString Name;               // имя листа
		TWeakPtr<SWidget> Leaf;     // лист Slate
		FVector2D Sum = FVector2D::ZeroVector;   // сумма точек сетки
		int32 Count = 0;            // точек сетки
		FVector2D P = FVector2D::ZeroVector;     // точка проб
		bool bBase = false;         // без сумки нажатие берёт окно цели
		bool bPoll = false;         // цель опроса: нажатие не берёт, но виджет под курсором виден
		int32 HotbarSlot = INDEX_NONE;   // слот панели быстрых слотов (цель опроса)
		TWeakObjectPtr<UWidget> Widget;  // виджет цели опроса
		FString Status = TEXT("нет пробы");      // итог покрытия
		bool bKey0 = false;         // ключ 0: нажатие дошло до цели
		bool bHover = false;        // под телом сумки цель подсвечена
		int32 Tries = 0;            // попыток покрыть
	};

	// Точка тела сумки относительно центра ручки.
	struct FBody { FVector2D Off = FVector2D::ZeroVector; };

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;                 // мир прогона
		TWeakObjectPtr<UAsdaWindowSubsystem> WM;      // окна
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;  // сумка
		TArray<TFunction<bool(FRun&)>> Steps;         // шаги: true — шаг закончен
		int32 Next = 0;                               // текущий шаг
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		bool bQuit = false;                           // выйти по итогу
		TUniquePtr<FAsdaCVarScope> Keys;              // ключи на время прогона
		FTimerHandle Timer;                           // таймер шагов
		FDelegateHandle InputHook;                    // подписка на отчёт Slate
		FName Handler;                                // окно, взявшее нажатие
		bool bTaken = false;                          // нажатие взял кто-то (окно или вьюпорт)
		FVector2D ScreenMn = FVector2D::ZeroVector, ScreenMx = FVector2D::ZeroVector;   // экран игры
		FVector2D Far = FVector2D::ZeroVector;        // точка отпускания проб
		FVector2D World0 = FVector2D::ZeroVector;     // точка мира
		bool bWorld0 = false;                         // точка мира найдена
		FVector2D Home = FVector2D::ZeroVector;       // центр ручки сумки на месте
		FVector2D BagMn = FVector2D::ZeroVector, BagMx = FVector2D::ZeroVector;   // рамка сумки от центра ручки
		TArray<FBody> Body;                           // тело сумки
		TArray<FTarget> Targets;                      // цели
		int32 Cur = 0;                                // текущая цель
		int32 Phase = 0;                              // шаг внутри цели
		int32 Wait = 0;                               // кадры ожидания
		double Until = 0.0;                           // потолок ожидания события шага (0 — не ждём; ★ R5-fix-b 13.11.2026 (заключительный заход))
		TSet<FName> VisibleBefore;                    // окна до настоящего клика
		int32 EatenBefore = 0;                        // съедено нажатий до клика
		int32 RefusedBefore = 0;                      // ★ R5-fix8-b 14.11.2026: отказов мыши панели до клика ТРЕЙ0
		uint64 SettleFrame = 0;                       // ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-6): до какого кадра наблюдать после отказа меню
		int32 Example = INDEX_NONE;                   // кнопка трея для примера
		FName Opened;                                 // окно, открытое кнопкой примера
		TWeakObjectPtr<UAsdaHotbarSubsystem> Hotbar;  // панель быстрых слотов
		TWeakObjectPtr<UAsdaChatWindowSubsystem> Chat;   // окно чата
		int32 TabBefore = 0;                          // активная вкладка до клика
		int32 PageBefore = 0;                         // страница панели до клика
		FVector2D Tab0 = FVector2D::ZeroVector;       // точка первой вкладки
		int32 PollLeak = 0;                           // целей опроса: сквозь при ключе 0
		int32 PollCovered = 0;                        // целей опроса под телом сумки
		FVector2D OwnOff = FVector2D::ZeroVector;     // свой контрол сумки от центра ручки
		bool bOwnOff = false;                         // контрол найден
		FVector2D Pt = FVector2D::ZeroVector;         // точка этапа (колесо, фокус)
		bool bPt = false;                             // точка этапа найдена
		int32 RowBefore = 0;                          // строка списка до колеса
		TWeakObjectPtr<UEditableText> Edit;           // поле окна поверх сумки
		FName LiftOver;                               // окно, над которым тест держит сумку
		bool bLifted = false;                         // сумка поднята после перетаскивания
	};

	// Проверка с записью в журнал.
	static void Check(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok;   UE_LOG(LogAsdaBodyPressTest, Log,   TEXT("ТЕЛО-ОКНА ok   %s"), *What); }
		else   { ++R.Fail; UE_LOG(LogAsdaBodyPressTest, Error, TEXT("ТЕЛО-ОКНА FAIL %s"), *What); }
	}

	// Лист пути Slate в точке.
	static TSharedPtr<SWidget> LeafAt(const FVector2D& P)
	{
		FSlateApplication& App = FSlateApplication::Get();
		const FWidgetPath Path = App.LocateWindowUnderMouse(P, App.GetInteractiveTopLevelWindows());
		return Path.IsValid() ? TSharedPtr<SWidget>(Path.Widgets.Last().Widget) : nullptr;
	}

	// Настоящее движение мыши Slate в точку.
	using AsdaSelfTestInput::Move;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Нажатие в точке и отпускание вдали. Возвращает окно, взявшее нажатие (None — мир или никто).
	static FName Probe(FRun& R, const FVector2D& P, const FKey& Button, bool bDouble = false)
	{
		FSlateApplication& App = FSlateApplication::Get();
		R.Handler = NAME_None;
		R.bTaken = false;
		TSet<FKey> Down;
		Down.Add(Button);
		App.SetCursorPos(P);
		const FPointerEvent Ev(0, P, P, Down, Button, 0.f, FModifierKeysState());
		if (bDouble) { App.ProcessMouseButtonDoubleClickEvent(nullptr, Ev); }
		else { App.ProcessMouseButtonDownEvent(nullptr, Ev); }
		App.ReleaseAllPointerCapture();   // отпускание вдали с захватом всё равно кликнуло бы: кнопка считает себя под мышью
		App.SetCursorPos(R.Far);
		App.ProcessMouseButtonUpEvent(FPointerEvent(0, R.Far, R.Far, TSet<FKey>(), Button, 0.f, FModifierKeysState()));
		return R.Handler;
	}

	// Центр самой крупной ручки окна.
	static bool GrabPoint(const FRun& R, FName Id, FVector2D& Out)
	{
		double Best = -1.0;
		for (int32 i = 0; i < 16; ++i)
		{
			FVector2D Mn, Mx;
			if (!R.WM.IsValid() || !R.WM->DebugGetDragHandleRect(Id, i, Mn, Mx)) { continue; }
			const double A = (Mx.X - Mn.X) * (Mx.Y - Mn.Y);
			if (A > Best) { Best = A; Out = (Mn + Mx) * 0.5; }
		}
		return Best > 0.0;
	}

	// Лежит ли точка в какой-нибудь ручке окна.
	static bool InHandle(const FRun& R, FName Id, const FVector2D& P)
	{
		for (int32 i = 0; i < 16; ++i)
		{
			FVector2D Mn, Mx;
			if (R.WM->DebugGetDragHandleRect(Id, i, Mn, Mx) && P.X >= Mn.X && P.X <= Mx.X && P.Y >= Mn.Y && P.Y <= Mx.Y) { return true; }
		}
		return false;
	}

	// Видимые окна реестра.
	static TSet<FName> VisibleWindows(const FRun& R)
	{
		TSet<FName> Out;
		for (const FName& Id : R.WM->GetRegisteredWindowIds()) { if (R.WM->IsWindowVisible(Id)) { Out.Add(Id); } }
		return Out;
	}

	// Виджет листа по имени из отладочной строки «WBP_X [Имя]» или «WBP_X [Имя(SCheckBox.cpp(551))]».
	static UWidget* WidgetOf(const FRun& R, const FTarget& T)
	{
		int32 L = INDEX_NONE;
		if (!T.Name.FindChar(TEXT('['), L)) { return nullptr; }
		FString Name = T.Name.Mid(L + 1);
		int32 E = INDEX_NONE;
		if (Name.FindChar(TEXT('('), E) || Name.FindChar(TEXT(']'), E)) { Name.LeftInline(E); }
		UUserWidget* Win = R.WM->FindWindow(T.Win);
		return Win ? Win->GetWidgetFromName(FName(*Name)) : nullptr;
	}

	// Виден ли цели курсор сейчас (то, что спрашивает разбор опроса).
	static bool PollSees(const FRun& R, const FTarget& T)
	{
		if (T.HotbarSlot != INDEX_NONE) { return R.Hotbar.IsValid() && R.Hotbar->HitTestSlotUnderCursor() == T.HotbarSlot; }
		return T.Widget.IsValid() && AsdaUi::IsCursorOverWidget(T.Widget.Get());
	}

	// Цели на живом экране: листья окон реестра по сетке.
	static void CollectTargets(FRun& R)
	{
		TMap<const SWidget*, int32> Seen;   // лист → индекс цели (−1 — мир или вне реестра)
		for (double Y = R.ScreenMn.Y + 1.0; Y < R.ScreenMx.Y; Y += kGrid)
		{
			for (double X = R.ScreenMn.X + 1.0; X < R.ScreenMx.X; X += kGrid)
			{
				const FVector2D P(X, Y);
				const TSharedPtr<SWidget> Leaf = LeafAt(P);
				if (!Leaf.IsValid()) { continue; }
				int32* Known = Seen.Find(Leaf.Get());
				if (!Known)
				{
					const FName Owner = R.WM->DebugWindowOwningSlate(Leaf);
					int32 Idx = -1;
					if (!Owner.IsNone())
					{
						FTarget T;
						T.Win = Owner;
						T.Name = FReflectionMetaData::GetWidgetDebugInfo(Leaf.Get());
						T.Leaf = Leaf;
						Idx = R.Targets.Add(T);
					}
					Known = &Seen.Add(Leaf.Get(), Idx);
				}
				if (*Known >= 0) { R.Targets[*Known].Sum += P; ++R.Targets[*Known].Count; }
				else if (!R.bWorld0 && R.WM->FindWindowCatchingAt(P).IsNone()) { R.World0 = P; R.bWorld0 = true; }
			}
		}
		// Точка пробы — точка сетки цели, ближайшая к среднему (у невыпуклых листьев среднее может лежать вне их).
		for (FTarget& T : R.Targets)
		{
			const FVector2D Mean = T.Sum / FMath::Max(T.Count, 1);
			T.P = Mean;
			if (LeafAt(Mean) == T.Leaf.Pin()) { continue; }
			double Best = TNumericLimits<double>::Max();
			for (double Y = R.ScreenMn.Y + 1.0; Y < R.ScreenMx.Y; Y += kGrid)
			{
				for (double X = R.ScreenMn.X + 1.0; X < R.ScreenMx.X; X += kGrid)
				{
					const FVector2D P(X, Y);
					const double D = FVector2D::DistSquared(P, Mean);
					if (D < Best && LeafAt(P) == T.Leaf.Pin()) { Best = D; T.P = P; }
				}
			}
		}
	}

	// Тело сумки: её ловит хит-тест окон, но ни её контрол, ни её ручка точку не берут.
	static void CollectBody(FRun& R)
	{
		R.Body.Reset();
		R.BagMn = FVector2D(1e9, 1e9);
		R.BagMx = FVector2D(-1e9, -1e9);
		for (double Y = R.ScreenMn.Y + 1.0; Y < R.ScreenMx.Y; Y += 4.0)
		{
			for (double X = R.ScreenMn.X + 1.0; X < R.ScreenMx.X; X += 4.0)
			{
				const FVector2D P(X, Y);
				if (R.WM->FindWindowCatchingAt(P) != kBag) { continue; }
				R.BagMn = FVector2D(FMath::Min(R.BagMn.X, P.X - R.Home.X), FMath::Min(R.BagMn.Y, P.Y - R.Home.Y));
				R.BagMx = FVector2D(FMath::Max(R.BagMx.X, P.X - R.Home.X), FMath::Max(R.BagMx.Y, P.Y - R.Home.Y));
				if (R.WM->HasCapturingControlAt(kBag, P) || InHandle(R, kBag, P)) { continue; }
				if (R.WM->DebugWindowOwningSlate(LeafAt(P)) == kBag) { continue; }
				FBody B;
				B.Off = P - R.Home;
				R.Body.Add(B);
			}
		}
	}

	// Куда поставить ручку сумки, чтобы её тело легло на точку: перебор тела, сумка целиком на экране. Try — номер варианта.
	static bool PlaceFor(const FRun& R, const FVector2D& P, int32 Try, FVector2D& OutHandle)
	{
		TArray<FVector2D> Fit;
		for (const FBody& B : R.Body)
		{
			const FVector2D H = P - B.Off;
			if (H.X + R.BagMn.X >= R.ScreenMn.X && H.Y + R.BagMn.Y >= R.ScreenMn.Y && H.X + R.BagMx.X <= R.ScreenMx.X && H.Y + R.BagMx.Y <= R.ScreenMx.Y) { Fit.Add(H); }
		}
		if (Fit.Num() == 0) { return false; }
		OutHandle = Fit[FMath::Min((Try * Fit.Num()) / 4, Fit.Num() - 1)];
		return true;
	}

	// ★ R5-fix-b 13.11.2026 (заключительный заход): ждать СОБЫТИЯ шага (bDone) до Cap с вместо счёта опросов. true — ещё ждём (шаг повторится).
	//   Страницу панели листает отложенное действие персонажа 104 (`TickPageAction`) — в кадре ПОСЛЕ отпускания: под нагрузкой
	//   три опроса по 0,02 с укладывались в один кадр, и ПАНЕЛЬ0 читала страницу до смены (138/1, один хэш в трёх попытках).
	static bool StillWaiting(FRun& R, bool bDone, double Cap = 3.0)
	{
		const double Now = FPlatformTime::Seconds();
		if (bDone) { R.Until = 0.0; return false; }
		if (R.Until <= 0.0) { R.Until = Now + Cap; return true; }
		if (Now < R.Until) { return true; }
		R.Until = 0.0;
		return false;
	}

	// Одна цель: поставить сумку, дождаться, снять пробы. Возвращает true, когда цель закончена.
	static bool TargetStep(FRun& R)
	{
		if (R.Cur >= R.Targets.Num()) { return true; }
		FTarget& T = R.Targets[R.Cur];
		if (R.Wait > 0) { --R.Wait; return false; }
		if (R.WM->IsSimClickPending()) { return false; }
		auto NextTarget = [&R]() { ++R.Cur; R.Phase = 0; };
		switch (R.Phase)
		{
		case 0:   // поставить сумку
		{
			if (!T.bBase && !T.bPoll) { T.Status = TEXT("цель мышь не разбирает"); NextTarget(); return false; }
			FVector2D Want, Grab;
			if (!PlaceFor(R, T.P, T.Tries, Want) || !GrabPoint(R, kBag, Grab)) { T.Status = TEXT("сумкой не закрыть: край экрана"); NextTarget(); return false; }
			R.WM->SimulateDrag(Grab, Want, 6);
			R.bLifted = false;
			R.Phase = 1;
			R.Wait = 2;
			return false;
		}
		case 1:   // сумка легла?
		{
			if (!R.LiftOver.IsNone() && !R.bLifted)
			{
				R.WM->SetWindowZOrder(kBag, R.WM->GetWindowZOrder(R.LiftOver) + 1);
				R.bLifted = true;
				R.Wait = 6;
				return false;
			}
			const FName Top = R.WM->FindWindowCatchingAt(T.P);
			const bool bBagHere = R.WM->IsWindowHitAt(kBag, T.P);
			if (Top != kBag)
			{
				if (bBagHere && Top == T.Win) { T.Status = TEXT("цель выше сумки"); Move(T.P); NextTarget(); return false; }
				if (++T.Tries < 4) { R.Phase = 0; return false; }
				T.Status = FString::Printf(TEXT("не легла: под точкой %s"), *Top.ToString());
				NextTarget();
				return false;
			}
			if (R.WM->HasCapturingControlAt(kBag, T.P) || InHandle(R, kBag, T.P))
			{
				if (++T.Tries < 4) { R.Phase = 0; return false; }
				T.Status = TEXT("под точкой только контрол или ручка сумки");
				NextTarget();
				return false;
			}
			Move(T.P + FVector2D(1.0, 0.0));
			R.Phase = 2;
			R.Wait = 1;
			return false;
		}
		case 2:
			Move(T.P);
			R.Phase = 3;
			R.Wait = 2;
			return false;
		case 3:   // пробы
		{
			if (T.bPoll)
			{
				FSlateApplication::Get().SetCursorPos(T.P);
				R.Keys->Set(kPoll, 0);
				const bool bSee0 = PollSees(R, T);
				R.Keys->Set(kPoll, 1);
				const bool bSee1 = PollSees(R, T);
				++R.PollCovered;
				R.PollLeak += bSee0 ? 1 : 0;
				T.bKey0 = bSee0;
				Check(R, !bSee1, FString::Printf(TEXT("%s/%s (%.0f,%.0f): опрос, ключ 0 видит=%d, ключ 1 видит=%d"), *T.Win.ToString(), *T.Name, T.P.X, T.P.Y, bSee0 ? 1 : 0, bSee1 ? 1 : 0));
				T.Status = TEXT("опрос: закрыта телом");
				if (T.Name.Contains(TEXT("CHAT_NEW_Tab1")) && R.Chat.IsValid())
				{
					R.TabBefore = R.Chat->DebugActiveTab();
					R.WM->SimulateClickAt(T.P, 3);
					R.Phase = 5;
					R.Wait = 1;
					return false;
				}
				NextTarget();
				return false;
			}
			{
				FSlateApplication& App = FSlateApplication::Get();
				const TSharedPtr<SWidget> Under = LeafAt(T.P);
				const TSharedPtr<SWidget> Captor = App.GetCursorUser().IsValid() ? App.GetCursorUser()->GetCursorCaptor() : nullptr;
				UE_LOG(LogAsdaBodyPressTest, Log, TEXT("ТЕЛО-ОКНА цель %s: лист %s, захват %s, кнопки зажаты %d"), *T.Name,
					Under.IsValid() ? *FReflectionMetaData::GetWidgetDebugInfo(Under.Get()) : TEXT("—"),
					Captor.IsValid() ? *FReflectionMetaData::GetWidgetDebugInfo(Captor.Get()) : TEXT("нет"), App.GetPressedMouseButtons().Num());
			}
			const TSharedPtr<SWidget> Leaf = T.Leaf.Pin();
			T.bHover = Leaf.IsValid() && Leaf->IsHovered();
			R.Keys->Set(kKey, 0);
			const FName Key0 = Probe(R, T.P, EKeys::LeftMouseButton);
			T.bKey0 = Key0 == T.Win;
			R.Keys->Set(kKey, 1);
			const int32 Eaten = R.WM->GetTopPressEatenCount();
			const FName L = Probe(R, T.P, EKeys::LeftMouseButton);
			const FName Rt = Probe(R, T.P, EKeys::RightMouseButton);
			const FName Db = Probe(R, T.P, EKeys::LeftMouseButton, true);
			const int32 Got = R.WM->GetTopPressEatenCount() - Eaten;
			Check(R, L != T.Win && Rt != T.Win && Db != T.Win && Got == 3 && R.WM->GetTopPressPendingUps() == 0,
				FString::Printf(TEXT("%s/%s (%.0f,%.0f): ключ 1 — ЛКМ «%s», ПКМ «%s», двойной «%s», съедено %d"),
					*T.Win.ToString(), *T.Name, T.P.X, T.P.Y, *L.ToString(), *Rt.ToString(), *Db.ToString(), Got));
			if (T.Name.Contains(TEXT("CHAT_NEW_Edit2")) && T.Leaf.IsValid())
			{
				FSlateApplication& App = FSlateApplication::Get();
				App.SetKeyboardFocus(T.Leaf.Pin(), EFocusCause::SetDirectly);
				Probe(R, T.P, EKeys::LeftMouseButton);
				Check(R, App.GetKeyboardFocusedWidget() == T.Leaf.Pin(), TEXT("ФОК1 поле чата под сумкой фокус держит (окно ниже поймавшего)"));
			}
			R.VisibleBefore = VisibleWindows(R);
			R.EatenBefore = R.WM->GetTopPressEatenCount();
			R.WM->SimulateClickAt(T.P, 3);
			R.Phase = 4;
			R.Wait = 1;
			return false;
		}
		case 4:   // настоящий клик при ключе 1
		{
			const TSet<FName> Now = VisibleWindows(R);
			const bool bSame = Now.Difference(R.VisibleBefore).Num() == 0 && R.VisibleBefore.Difference(Now).Num() == 0;
			Check(R, bSame && R.WM->GetTopPressEatenCount() == R.EatenBefore + 1 && R.WM->FindWindowCatchingAt(T.P) == kBag,
				FString::Printf(TEXT("%s/%s: настоящий клик по телу сумки ничего не меняет (съедено %d)"), *T.Win.ToString(), *T.Name, R.WM->GetTopPressEatenCount() - R.EatenBefore));
			T.Status = TEXT("закрыта телом");
			if (T.Name.Contains(TEXT("QUICK_NEW_UPBUT")) && R.Hotbar.IsValid())
			{
				R.Keys->Set(kKey, 0);
				R.PageBefore = R.Hotbar->GetCurrentPage();
				R.WM->SimulateClickAt(T.P, 3);
				R.Phase = 9;
				R.Wait = 3;
				return false;
			}
			if (T.Name.Contains(TEXT("SYSTEMMAIN_SKILLBUT")))
			{
				R.Keys->Set(kKey, 0);
				R.VisibleBefore = Now;
				{ const UAsdaSystemMenuSubsystem* Menu = R.WM->GetGameInstance() ? R.WM->GetGameInstance()->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
				  R.RefusedBefore = Menu ? Menu->GetMouseRefusedCount() : 0; }   // ★ R5-fix8-b 14.11.2026
				R.WM->SimulateClickAt(T.P, 3);
				R.Phase = 8;
				R.Wait = 3;
				return false;
			}
			NextTarget();
			return false;
		}
		case 9:   // листание панели сквозь сумку при ключе 0
			if (StillWaiting(R, R.Hotbar->GetCurrentPage() != R.PageBefore)) { return false; }   // ★ R5-fix-b 13.11.2026 (заключительный заход)
			Check(R, R.Hotbar->GetCurrentPage() != R.PageBefore, FString::Printf(TEXT("ПАНЕЛЬ0 ключ 0: настоящий клик по телу сумки листает панель (было %d, стало %d) — дефект воспроизведён"), R.PageBefore, R.Hotbar->GetCurrentPage()));
			for (const FTarget& D : R.Targets) { if (D.Name.Contains(TEXT("QUICK_NEW_DOWNBUT"))) { R.WM->SimulateClickAt(D.P, 3); } }
			R.Phase = 10;
			R.Wait = 3;
			return false;
		case 10:
			if (StillWaiting(R, R.Hotbar->GetCurrentPage() == R.PageBefore)) { return false; }   // ★ R5-fix-b 13.11.2026 (заключительный заход)
			Check(R, R.Hotbar->GetCurrentPage() == R.PageBefore, FString::Printf(TEXT("ПАНЕЛЬ1 страница панели возвращена (%d)"), R.Hotbar->GetCurrentPage()));
			R.Keys->Set(kKey, 1);
			NextTarget();
			return false;
		case 8:   // кнопка трея сквозь сумку при ключе 0
		{
			const TSet<FName> Opened = VisibleWindows(R).Difference(R.VisibleBefore);
			// ★ R5-fix8-b 14.11.2026: ждём СОБЫТИЯ — меню отказало нажатию (решение синхронно в обработчике) или окно открылось (отложенно,
			//   через кадры) — потолок 3 с. Прежде проверка шла сразу после отпускания и не отличала «не открылось» от «не успело».
			const UAsdaSystemMenuSubsystem* Menu = R.WM->GetGameInstance() ? R.WM->GetGameInstance()->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
			const int32 Refused = Menu ? Menu->GetMouseRefusedCount() - R.RefusedBefore : 0;
			// ★ R5-fix8-b 14.11.2026 (вторая запись, ревизия REVIEW8) (МЕЛК-6): после отказа — ещё 8 кадров наблюдения (открытие в обход частной проверки тоже ловится);
			//   открылось окно — сразу к проверке (красная); ни отказа, ни окна за 3 с — к проверке (красная: нажатие не дошло).
			if (Opened.Num() == 0)
			{
				if (Refused > 0 && R.SettleFrame == 0) { R.SettleFrame = GFrameCounter + 8; }
				if (StillWaiting(R, Refused > 0 && GFrameCounter >= R.SettleFrame)) { return false; }
			}
			R.Until = 0.0; R.SettleFrame = 0;
			FString Names;
			for (const FName& Id : Opened) { Names += Id.ToString() + TEXT(" "); }
			Check(R, Refused == 1 && Opened.Num() == 0, FString::Printf(TEXT("ТРЕЙ0 ключ 0: нажатие дошло до кнопки, но окно не открылось (%s; отказов меню %d — ждём 1): частная проверка меню `IsCursorOverHigherWindow`"), *Names, Refused));
			for (const FName& Id : Opened) { R.WM->CloseWindow(Id); }
			R.Keys->Set(kKey, 1);
			R.Wait = 3;
			NextTarget();
			return false;
		}
		case 5:   // вкладка чата под сумкой, ключ 1: настоящий клик
			Check(R, R.Chat->DebugActiveTab() == R.TabBefore, FString::Printf(TEXT("ЧАТ1 ключ 1: клик по телу сумки над вкладкой — вкладка %d не сменилась (%d)"), R.TabBefore, R.Chat->DebugActiveTab()));
			R.Keys->Set(kPoll, 0);
			R.Keys->Set(kKey, 0);
			R.WM->SimulateClickAt(T.P, 3);
			R.Phase = 6;
			R.Wait = 1;
			return false;
		case 6:   // ключ 0: клик проходит, вкладка вернуть
			Check(R, R.Chat->DebugActiveTab() != R.TabBefore, FString::Printf(TEXT("ЧАТ0 ключ 0: клик проходит сквозь сумку во вкладку (было %d, стало %d) — дефект воспроизведён"), R.TabBefore, R.Chat->DebugActiveTab()));
			R.WM->SimulateClickAt(R.Tab0, 3);
			R.Phase = 7;
			R.Wait = 1;
			return false;
		case 7:
			R.Keys->Set(kPoll, 1);
			R.Keys->Set(kKey, 1);
			NextTarget();
			return false;
		default:
			NextTarget();
			return false;
		}
	}

	// Цели экрана (OnlyWin — только этого окна) и база: сумка закрыта, цель берёт нажатие сама.
	static void CollectAndBase(FRun& X, FName OnlyWin)
	{
	X.Targets.Reset();
	X.Cur = 0; X.Phase = 0; X.PollCovered = 0; X.PollLeak = 0;
	CollectTargets(X);
	if (!OnlyWin.IsNone()) { X.Targets.RemoveAll([OnlyWin](const FTarget& T) { return T.Win != OnlyWin; }); }
	TMap<FName, int32> PerWin;
	for (const FTarget& T : X.Targets) { ++PerWin.FindOrAdd(T.Win); }
	for (const TPair<FName, int32>& Kv : PerWin) { UE_LOG(LogAsdaBodyPressTest, Log, TEXT("ТЕЛО-ОКНА окно %s: целей %d"), *Kv.Key.ToString(), Kv.Value); }
	Check(X, X.Targets.Num() > 0, FString::Printf(TEXT("целей на экране: %d"), X.Targets.Num()));
	const int32 Eaten = X.WM->GetTopPressEatenCount();
	int32 Base = 0;
	for (FTarget& T : X.Targets)
	{
		T.bBase = Probe(X, T.P, EKeys::LeftMouseButton) == T.Win;
		Base += T.bBase ? 1 : 0;
		if (T.Name.Contains(TEXT("CHAT_NEW_Tab0"))) { X.Tab0 = T.P; }
		if (!T.bBase)
		{
			T.Widget = WidgetOf(X, T);
			FSlateApplication::Get().SetCursorPos(T.P);
			T.bPoll = T.Widget.IsValid() && PollSees(X, T);
			UE_LOG(LogAsdaBodyPressTest, Log, TEXT("ТЕЛО-ОКНА база: %s/%s (%.0f,%.0f) нажатие не взяла, взял «%s»"), *T.Win.ToString(), *T.Name, T.P.X, T.P.Y, *X.Handler.ToString());
		}
	}
	// Слоты панели: Slate их не видит, в сетку они не попали.
	if (X.Hotbar.IsValid() && OnlyWin.IsNone())
	{
		for (int32 i = 0; i < 256; ++i)
		{
			FVector2D Mn, Mx;
			if (!X.Hotbar->GetSlotIconAbsRect(i, Mn, Mx)) { continue; }
			FTarget T;
			T.Win = FName(TEXT("QUICK_NEW_WIN"));
			T.Name = FString::Printf(TEXT("слот %d"), i);
			T.P = (Mn + Mx) * 0.5;
			T.HotbarSlot = i;
			FSlateApplication::Get().SetCursorPos(T.P);
			T.bPoll = PollSees(X, T);
			X.Targets.Add(T);
		}
	}
	Check(X, Base > 0 && X.WM->GetTopPressEatenCount() == Eaten, FString::Printf(TEXT("Б1 без сумки нажатие берут %d целей из %d, гейт съел %d"), Base, X.Targets.Num(), X.WM->GetTopPressEatenCount() - Eaten));
	}

	// Итог этапа: таблица целей и счётчики.
	static void StageSummary(FRun& X, const TCHAR* Tag)
	{
	int32 Covered = 0, Leak = 0, Hover = 0, Above = 0, Edge = 0, Other = 0;
	for (const FTarget& T : X.Targets)
	{
		UE_LOG(LogAsdaBodyPressTest, Log, TEXT("ТЕЛО-ОКНА ЦЕЛЬ\t%s\t%s\t(%.0f,%.0f)\t%s\tключ0=%d\tнаведение=%d"), *T.Win.ToString(), *T.Name, T.P.X, T.P.Y, *T.Status, T.bKey0 ? 1 : 0, T.bHover ? 1 : 0);
		if (T.Status == TEXT("закрыта телом")) { ++Covered; Leak += T.bKey0 ? 1 : 0; Hover += T.bHover ? 1 : 0; }
		else if (T.Status.StartsWith(TEXT("опрос:"))) {}
		else if (T.Status == TEXT("цель выше сумки")) { ++Above; }
		else if (T.Status.StartsWith(TEXT("сумкой не закрыть"))) { ++Edge; }
		else if (T.bBase || T.bPoll) { ++Other; }
	}
	Check(X, Covered > 0 && Leak == Covered, FString::Printf(TEXT("К0 %s ключ 0: нажатие проходит сквозь сумку у %d целей из %d закрытых — дефект воспроизведён"), Tag, Leak, Covered));
	Check(X, Other == 0, FString::Printf(TEXT("К1 %s целей, которые не удалось разобрать: %d"), Tag, Other));
	Check(X, X.PollLeak == X.PollCovered, FString::Printf(TEXT("К2 %s ключ 0: опрос видит цель сквозь сумку у %d целей из %d — дефект воспроизведён"), Tag, X.PollLeak, X.PollCovered));
	UE_LOG(LogAsdaBodyPressTest, Log, TEXT("ТЕЛО-ОКНА СВОДКА %s закрыто %d, сквозь при ключе 0 %d, подсветка под телом %d, выше сумки %d, край экрана %d"), Tag, Covered, Leak, Hover, Above, Edge);
	}

	// Следующий шаг или итог.
	static void Pump(TSharedRef<FRun> R)
	{
		UWorld* W = R->World.Get();
		if (!W) { return; }
		if (R->Next >= R->Steps.Num())
		{
			FSlateDebugging::InputEvent.Remove(R->InputHook);
			R->Keys.Reset();
			AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaBodyPressTest, TEXT("ТЕЛО-ОКНА"), R->Ok, R->Fail);
			return;
		}
		if (R->Steps[R->Next](*R)) { ++R->Next; }
		W->GetTimerManager().SetTimer(R->Timer, FTimerDelegate::CreateLambda([R]() { Pump(R); }), 0.02f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBodyPress(
	TEXT("asda.UI.SelfTestBodyPress"),
	TEXT("Самотест нажатия по телу окна: настоящая сумка ложится на каждую кнопку окон HUD, склада и лавки, ключ `asda.UI.TopWindowPress` 0 — нажатие проходит, 1 — нет. Офлайн, карта мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBodyPressTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 3.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		TSharedPtr<SWindow> Win = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
		if (!WM || !Inv || !Win.IsValid() || !FSlateApplication::IsInitialized())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет окон / сумки / окна игры"));
			return;
		}
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->World = W; R->WM = WM; R->Inv = Inv; R->bQuit = KA.bQuit;
		R->Hotbar = GI->GetSubsystem<UAsdaHotbarSubsystem>();
		R->Chat = GI->GetSubsystem<UAsdaChatWindowSubsystem>();
		R->ScreenMn = FVector2D(Win->GetPositionInScreen());
		R->ScreenMx = R->ScreenMn + FVector2D(Win->GetSizeInScreen());
		R->Far = (R->ScreenMn + R->ScreenMx) * 0.5;
		R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
		if (UAsdaUiVisibilitySubsystem* Vis = GI->GetSubsystem<UAsdaUiVisibilitySubsystem>()) { Vis->SetUiShown(true, TEXT("самотест тела окна")); }
		// Кто взял нажатие: последний ответ Handled у нажатия или двойного щелчка.
		TWeakPtr<FRun> Wk = R;
		R->InputHook = FSlateDebugging::InputEvent.AddLambda([Wk](const FSlateDebuggingInputEventArgs& A)
		{
			const TSharedPtr<FRun> P = Wk.Pin();
			if (!P.IsValid() || !A.Reply.IsEventHandled() || !P->WM.IsValid()) { return; }
			if (A.InputEventType != ESlateDebuggingInputEvent::MouseButtonDown && A.InputEventType != ESlateDebuggingInputEvent::MouseButtonDoubleClick) { return; }
			P->bTaken = true;
			P->Handler = P->WM->DebugWindowOwningSlate(A.HandlerWidget);
		});
		auto Add = [&R](TFunction<bool(FRun&)> F) { R->Steps.Add(MoveTemp(F)); };
		auto Pause = [&R](int32 N) { for (int32 i = 0; i < N; ++i) { R->Steps.Add([](FRun&) { return true; }); } };

		// ═══ Сумка закрыта: цели и база ═══
		Add([](FRun& X) { X.Keys->Set(kKey, 1); X.Keys->Set(kPoll, 1); if (X.Inv->IsWindowOpen()) { X.Inv->CloseWindow(); } return true; });
		Pause(30);
		Add([](FRun& X)
		{
			CollectAndBase(X, NAME_None);
			return true;
		});
		// ═══ Сумка открыта: тело ═══
		Add([](FRun& X) { X.Inv->OpenWindow(); return true; });
		Pause(30);
		Add([](FRun& X)
		{
			const bool bGrab = GrabPoint(X, kBag, X.Home);
			Check(X, bGrab, TEXT("С0 сумка открыта, ручка есть"));
			if (bGrab) { CollectBody(X); }
			Check(X, X.Body.Num() > 0, FString::Printf(TEXT("С1 точек тела сумки: %d, рамка (%.0f,%.0f)-(%.0f,%.0f) от ручки"), X.Body.Num(), X.BagMn.X, X.BagMn.Y, X.BagMx.X, X.BagMx.Y));
			return true;
		});
		// ═══ Каждая цель ═══
		Add([](FRun& X) { return TargetStep(X); });
		// ═══ Итог по целям ═══
		Add([](FRun& X)
		{
			StageSummary(X, TEXT("HUD"));
			return true;
		});
		// ═══ Контроль ═══
		Add([](FRun& X)
		{
			// Свой контрол сумки поверх чужого окна.
			FVector2D Own = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
			bool bOwn = false;
			for (double Y = X.ScreenMn.Y + 1.0; Y < X.ScreenMx.Y && !bOwn; Y += 4.0)
			{
				for (double Xc = X.ScreenMn.X + 1.0; Xc < X.ScreenMx.X && !bOwn; Xc += 4.0)
				{
					const FVector2D P(Xc, Y);
					if (X.WM->FindWindowCatchingAt(P) == kBag && X.WM->DebugWindowOwningSlate(LeafAt(P)) == kBag && !InHandle(X, kBag, P)) { Own = P; bOwn = true; }
				}
			}
			FVector2D OwnGrab;
			if (bOwn && GrabPoint(X, kBag, OwnGrab)) { X.OwnOff = Own - OwnGrab; X.bOwnOff = true; }
			const int32 Eaten = X.WM->GetTopPressEatenCount();
			Check(X, bOwn && Probe(X, Own, EKeys::LeftMouseButton) == kBag && X.WM->GetTopPressEatenCount() == Eaten, FString::Printf(TEXT("Н1 свой контрол сумки (%.0f,%.0f) нажатие получает"), Own.X, Own.Y));
			// Мир.
			if (X.bWorld0 && X.WM->FindWindowCatchingAt(X.World0).IsNone())
			{
				Probe(X, X.World0, EKeys::LeftMouseButton);
				Check(X, X.WM->GetTopPressEatenCount() == Eaten, FString::Printf(TEXT("Н2 мир (%.0f,%.0f): гейт нажатие не трогает"), X.World0.X, X.World0.Y));
			}
			// Потерянное отпускание: новое нажатие свободной цели проходит, его отпускание не съедается.
			const FTarget* Hid = nullptr;
			const FTarget* Free = nullptr;
			for (const FTarget& T : X.Targets)
			{
				if (!T.bBase) { continue; }
				FName A, B;
				if (!Hid && X.WM->FindLowerWindowUnderTopAt(T.P, A, B)) { Hid = &T; }
				if (!Free && X.WM->FindWindowCatchingAt(T.P) == T.Win) { Free = &T; }
			}
			if (Hid && Free)
			{
				FSlateApplication& App = FSlateApplication::Get();
				TSet<FKey> Down;
				Down.Add(EKeys::LeftMouseButton);
				App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, Hid->P, Hid->P, Down, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
				Check(X, X.WM->GetTopPressPendingUps() == 1 && App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton), TEXT("Н3 съеденное нажатие опрос кнопок видит, отпускание ждёт"));
				const FName Got = Probe(X, Free->P, EKeys::LeftMouseButton);
				Check(X, Got == Free->Win && X.WM->GetTopPressPendingUps() == 0 && !App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton),
					FString::Printf(TEXT("Н4 после потерянного отпускания нажатие цели %s/%s дошло (взял «%s»), пометок %d, зажато %d"), *Free->Win.ToString(), *Free->Name, *Got.ToString(), X.WM->GetTopPressPendingUps(), App.GetPressedMouseButtons().Num()));
			}
			else { Check(X, false, TEXT("Н3 нет пары целей: закрытой и свободной")); }
			return true;
		});
		// ═══ Склад и лавка под сумкой ═══
		static const FName kWare(TEXT("WARE_WIN"));
		static const FName kShop(TEXT("NPCSHOP_WIN"));
		static const FName kLink(TEXT("SOULMATE_LINK_WIN"));
		auto Stage = [&Add, &Pause](FName Win, const TCHAR* Tag, TFunction<void(FRun&)> Open)
		{
			Add([Open](FRun& X) { if (X.Inv->IsWindowOpen()) { X.Inv->CloseWindow(); } Open(X); return true; });
			Pause(30);
			Add([](FRun& X) { if (X.Inv->IsWindowOpen()) { X.Inv->CloseWindow(); } return true; });
			Pause(20);
			Add([Win, Tag](FRun& X)
			{
				Check(X, X.WM->IsWindowVisible(Win), FString::Printf(TEXT("%s открыт"), Tag));
				X.LiftOver = Win == kWare ? Win : NAME_None;
				CollectAndBase(X, Win);
				X.Inv->OpenWindow();
				return true;
			});
			Pause(20);
			Add([](FRun& X) { return TargetStep(X); });
			Add([Tag](FRun& X) { StageSummary(X, Tag); return true; });
		};
		Stage(kWare, TEXT("СКЛАД"), [](FRun& X)
		{
			if (UAsdaWarehouseSubsystem* Wh = X.WM->GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>()) { Wh->OpenWindow(UAsdaWarehouseSubsystem::kModeKeep); }
		});
		// Колесо над списком склада под телом сумки.
		Add([](FRun& X)
		{
			const UAsdaWarehouseSubsystem* Wh = X.WM->GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>();
			FVector2D Mn, Mx, Want, Grab;
			X.bPt = Wh && Wh->GetKeepListAbsRect(Mn, Mx);
			X.Pt = (Mn + Mx) * 0.5;
			if (X.bPt && PlaceFor(X, X.Pt, 0, Want) && GrabPoint(X, kBag, Grab)) { X.WM->SimulateDrag(Grab, Want, 6); }
			return true;
		});
		Add([](FRun& X) { return !X.WM->IsSimClickPending(); });
		Pause(10);
		Add([](FRun& X) { X.WM->SetWindowZOrder(kBag, X.WM->GetWindowZOrder(kWare) + 1); return true; });
		Pause(10);
		Add([](FRun& X)
		{
			UAsdaWarehouseSubsystem* Wh = X.WM->GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>();
			FSlateApplication::Get().SetCursorPos(X.Pt);
			const FName Top = X.WM->FindWindowCatchingAt(X.Pt);
			Check(X, X.bPt && Top == kBag && Wh && Wh->GetMaxScrollRow() > 0, FString::Printf(TEXT("КОЛ0 сумка на списке склада (под точкой %s), строк прокрутки %d"), *Top.ToString(), Wh ? Wh->GetMaxScrollRow() : -1));
			if (!Wh) { return true; }
			X.RowBefore = Wh->GetScrollRow();
			X.Keys->Set(kPoll, 1);
			X.WM->HandleMouseWheel(-1);
			Check(X, Wh->GetScrollRow() == X.RowBefore, FString::Printf(TEXT("КОЛ1 ключ 1: колесо по телу сумки список склада не крутит (строка %d)"), Wh->GetScrollRow()));
			X.Keys->Set(kPoll, 0);
			X.WM->HandleMouseWheel(-1);
			Check(X, Wh->GetScrollRow() != X.RowBefore, FString::Printf(TEXT("КОЛ2 ключ 0: колесо проходит сквозь сумку в склад (было %d, стало %d) — дефект воспроизведён"), X.RowBefore, Wh->GetScrollRow()));
			X.WM->HandleMouseWheel(1);
			X.Keys->Set(kPoll, 1);
			Wh->CloseWindow();
			return true;
		});
		Pause(10);
		Stage(kShop, TEXT("ЛАВКА"), [](FRun& X)
		{
			UAsdaNpcShopSubsystem* Shop = X.WM->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>();
			UDataTable* Dt = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcShopList.DT_NpcShopList"));
			if (!Shop || !Dt) { return; }
			for (const TPair<FName, uint8*>& Row : Dt->GetRowMap())
			{
				const FAsdaNpcShopRow* R = reinterpret_cast<const FAsdaNpcShopRow*>(Row.Value);
				if (R && R->ItemCodes.Num() > 0) { Shop->OpenShop(5, R->NpcId); break; }
			}
		});
		Add([](FRun& X) { if (UAsdaNpcShopSubsystem* Shop = X.WM->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>()) { Shop->CloseShop(); } return true; });
		Pause(10);
		// ═══ Фокус поля окна, поймавшего нажатие: окно с полем лежит поверх контрола сумки ═══
		Add([](FRun& X)
		{
			if (!X.Inv->IsWindowOpen()) { X.Inv->OpenWindow(); }
			X.WM->OpenWindowByPath(kLink, TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_SOULMATE_LINK_WIN"), 0, ESlateVisibility::SelfHitTestInvisible);
			return true;
		});
		Pause(20);
		Add([](FRun& X)
		{
			X.bPt = false;
			for (double Y = X.ScreenMn.Y + 1.0; Y < X.ScreenMx.Y && !X.bPt; Y += 4.0)
			{
				for (double Xc = X.ScreenMn.X + 1.0; Xc < X.ScreenMx.X && !X.bPt; Xc += 4.0)
				{
					const FVector2D P(Xc, Y);
					if (X.WM->IsWindowHitAt(kLink, P) && !X.WM->HasCapturingControlAt(kLink, P) && X.WM->DebugWindowOwningSlate(LeafAt(P)) != kLink) { X.Pt = P; X.bPt = true; }
				}
			}
			FVector2D Grab;
			Check(X, X.bPt && X.bOwnOff, FString::Printf(TEXT("ФОК0 тело окна с полем (%.0f,%.0f)"), X.Pt.X, X.Pt.Y));
			if (X.bPt && X.bOwnOff && GrabPoint(X, kBag, Grab)) { X.WM->SimulateDrag(Grab, X.Pt - X.OwnOff, 6); }
			return true;
		});
		Add([](FRun& X) { return !X.WM->IsSimClickPending(); });
		Pause(10);
		Add([](FRun& X) { X.WM->SetWindowZOrder(kLink, X.WM->GetWindowZOrder(kBag) + 1); return true; });
		Pause(10);
		Add([](FRun& X)
		{
			X.Edit = nullptr;
			if (UUserWidget* Wd = X.WM->FindWindow(kLink))
			{
				Wd->WidgetTree->ForEachWidget([&X](UWidget* W) { if (!X.Edit.IsValid()) { if (UEditableText* E = Cast<UEditableText>(W)) { if (E->IsVisible()) { X.Edit = E; } } } });
			}
			if (X.Edit.IsValid()) { X.Edit->SetKeyboardFocus(); }
			return true;
		});
		Pause(5);
		Add([](FRun& X)
		{
			FSlateApplication& App = FSlateApplication::Get();
			const TSharedPtr<SWidget> Before = App.GetKeyboardFocusedWidget();
			FName Top, Lower;
			const bool bLower = X.WM->FindLowerWindowUnderTopAt(X.Pt, Top, Lower);
			Check(X, bLower && Top == kLink && Lower == kBag && Before.IsValid() && X.WM->DebugWindowOwningSlate(Before) == kLink,
				FString::Printf(TEXT("ФОК2 поле окна в фокусе, под телом окна контрол сумки (поймал %s, ниже %s)"), *Top.ToString(), *Lower.ToString()));
			const int32 Eaten = X.WM->GetTopPressEatenCount();
			Probe(X, X.Pt, EKeys::LeftMouseButton);
			Check(X, X.WM->GetTopPressEatenCount() == Eaten + 1 && App.GetKeyboardFocusedWidget() != Before, TEXT("ФОК3 съеденное нажатие снимает фокус с поля поймавшего окна"));
			X.WM->CloseWindow(kLink);
			return true;
		});
		Pause(5);
		// ═══ Сумка обратно ═══
		Add([](FRun& X)
		{
			FVector2D Grab;
			if (GrabPoint(X, kBag, Grab)) { X.WM->SimulateDrag(Grab, X.Home, 6); }
			return true;
		});
		Add([](FRun& X) { return !X.WM->IsSimClickPending(); });
		Pause(10);
		Add([](FRun& X) { X.Inv->CloseWindow(); return true; });
		Pause(5);
		Pump(R);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
