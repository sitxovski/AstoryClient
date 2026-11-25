// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПОДЪЁМА ОКНА КЛИКОМ ПО ТЕЛУ — `asda.UI.SelfTestRaiseClick [delay=N] [quit]` (решение владельца 06.10.2026).
//   Отчёт: Reports/owner_1006/raise_on_body_click.md. ПУТЬ ИГРОКА: настоящие события мыши Slate (`SimulateClickAt`)
//   по точкам, найденным на ЖИВОЙ геометрии окон (`IsWindowHitAt` / `FindWindowCatchingAt` — тот же хит-тест, что у `TickDrag`).
//   Разделы:
//     A. Каталог: все окна реестра + все окна .JUI с ассетом WBP (кроме логина). Для каждого: открыть (если ещё не открыто),
//        накрыть «покрышкой» (обычное окно, сдвинутое верхним левым углом в центр проверяемого), щёлкнуть по ВИДИМОЙ части
//        тела (без кнопок) → поднимаемое встаёт над покрышкой; исключение — z не меняется.
//     B. Контрольная пара: `asda.UI.RaiseOnBodyClickAll 0` — неперетаскиваемые окна НЕ поднимаются.
//     C. Фокус: поле чата в фокусе + клик по телу другого окна → текст и фокус чата на месте (ключ 0 — контроль);
//        поле ввода окна под покрышкой: клик по полю → окно поднято, фокус и текст в поле; клик по телу → текст на месте.
//     D. 300 подъёмов попеременно двух окон со старта z 8600 → провалов порядка 0, верх < 8799; контроль ключом 0.
//     E. Нажатие ПКМ по телу нижнего окна тоже поднимает (эталон: события 11/14/17 — ЛКМ/ПКМ/СКМ).
//     F. Ревизия (КРИТ-1, СРЕДН-1): рабочий AlwaysTop (QUEST_WIN снят, EXTRACT_WIN поставлен) и полоса AlwaysTop; контроли —
//        признак из одной меты и `asda.UI.AlwaysTopBand 0`.
//     G. Ревизия (СРЕДН-2): фокус в строке чата + щелчок по фокусируемой кнопке, созданной кодом, — клик доставлен; контроли.
//     H. Ревизия (МЕЛК-1/2): однострочная подсказка общей плашки — щелчок окно не поднимает; контроль `RaiseTooltipGate 0`.
//     I. Ревизия (МЕЛК-4): трей меню, поднятый щелчком, при показе панели возвращается на z 93 (эталон: вниз списка).
//   Исходящие пакеты перехватываются (`UAsdaPetSubsystem::BeginSendCapture`) — в сеть не уходит ничего.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"            // реестр окон, хит-тест, подъём, синтетический клик
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaJuiWindowMeta.h"              // каталог окон .JUI
#include "UI/AsdaUiHelpers.h"                  // AsdaUi::WidgetAbsRect — живая геометрия
#include "UI/AsdaChatWindowSubsystem.h"        // поле ввода чата 2609 (раздел C)
#include "UI/AsdaItemTooltipSubsystem.h"       // погасить общую подсказку (гейт +0x1B0)
#include "Pet/AsdaPetSubsystem.h"              // перехват исходящих
#include "World/AsdaWorldSubsystem.h"           // свой павн офлайн (HUD и чат строятся только в мире)
// офлайн-гейт отладочных команд   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаги по событию «ввод доставлен», а не по часам
#include "AssetRegistry/AssetRegistryModule.h" // поиск WBP_<id>
#include "AsdaUiWbpCatalog.h"   // ★ R5-fix-b 13.11.2026 (третий заход): каталог WBP окон (общая точка)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"     // масштаб вьюпорта: абсолютные единицы → единицы канвы
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/EditableText.h"
#include "Components/Button.h"                 // фокусируемая кнопка раздела G
#include "Components/CanvasPanel.h"            // корневой канвас окна-покрышки
#include "Login/AsdaButtonRelay.h"             // приёмник OnClicked (UFUNCTION) для кнопки раздела G
#include "UI/AsdaTipPanel.h"                   // EAsdaTipOwner / FAsdaTipMetrics (раздел H)
#include "UI/AsdaSystemMenuSubsystem.h"        // трей меню 73400 (раздел I)
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRaiseTest, Log, All);

namespace AsdaRaiseTestLocal
{
	// Одно проверяемое окно каталога.
	struct FItem
	{
		FName Id;                     // id окна (он же суффикс WBP)
		FString Path;                 // путь WBP (пусто — окно уже в реестре, открыто не нами)
		bool bOpened = false;         // открыто самотестом (закрыть в конце)
		bool bVisForced = false;      // видимость включена самотестом (вернуть)
		ESlateVisibility VisWas = ESlateVisibility::SelfHitTestInvisible;   // прежняя видимость
		int32 ZWas = 0;               // z до проверки (вернуть окну, открытому не нами)
	};

	// Контекст многоэтапного прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0, Skip = 0;      // пройдено / провалено / пропущено (нечего щёлкнуть)
		TWeakObjectPtr<UWorld> W;              // мир прогона
		bool bQuit = false;                    // выйти после прогона
		int32 Limit = 0;                       // >0 — проверить в разделе A только первые N окон (отладка самотеста)
		TArray<FItem> Items;                   // каталог раздела A
		TArray<FItem> Ctrl;                    // выборка раздела B
		TArray<FItem>* Cur = nullptr;          // текущий список
		int32 Idx = 0;                         // индекс в текущем списке
		bool bCtrlMode = false;                // раздел B (ключ 0)
		FName CoverA, CoverB;                  // окна-покрышки
		FName Cover;                           // покрышка текущей проверки
		TArray<TPair<TWeakObjectPtr<UCanvasPanelSlot>, FMargin>> Moved;     // сдвинутые слоты покрышки: прежние отступы (откат)
		FVector2D ClickPt = FVector2D::ZeroVector;     // точка клика
		FVector2D OverlapPt = FVector2D::ZeroVector;   // точка перекрытия (нижнее под покрышкой)
		bool bOverlap = false;                 // перекрытие найдено
		bool bOnControl = false;               // щелчок пришёлся на контрол (видимого тела без контролов нет)
		int32 ZBefore = 0;                     // z проверяемого окна до клика
		bool bEligible = false;                // поднимается по правилам
		FString Why;                           // причина решения
		int32 CvTip = 1;  // ключ RaiseTooltipGate до прогона (раздел H возвращает его между шагами); ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): остальные — `AsdaCVarSave`
		uint32 CapGen = 0;                     // перехват исходящих взведён
		int32 Sent = 0;                        // перехвачено отправок
		int32 NRaised = 0, NExcl = 0, NNoOverlap = 0;   // статистика A
		TArray<FString> Skipped;               // пропущенные окна (нечего щёлкнуть)
		TSet<FName> RaisedInA;                 // окна, поднятые в разделе A (выборка контроля B)
		bool bRetried = false;                 // вторая раскладка покрышки (у мелкого окна не нашлось видимой части)
		// Раздел C.
		FName EditWin;                         // окно с полем ввода
		TWeakObjectPtr<UEditableText> Edit;    // его поле
		// Раздел D.
		FName SatA, SatB;                      // пара окон
		FVector2D PA = FVector2D::ZeroVector, PB = FVector2D::ZeroVector;   // точки «только A» / «только B»
		int32 SatIter = 0, SatN = 300, SatFail = 0, SatMaxZ = 0;            // счётчики
		bool bSatCtrl = false;                 // контрольный прогон D (ключ 0)
		int32 SatCtrlFlips = 0;                // перестановок в контроле
		bool bSatClickedA = false;             // в этой итерации щёлкнули по A (иначе по B)
		TArray<FName> OpenedF;                 // окна, открытые разделом F (закрыть)
		TWeakObjectPtr<UButton> Btn;           // фокусируемая кнопка раздела G
		TWeakObjectPtr<UAsdaButtonRelay> Relay;// приёмник OnClicked кнопки раздела G
		int32 BtnClicks = 0;                   // сколько раз пришёл OnClicked
		bool bTipWasShown = false;             // раздел H: подсказка была показана в момент щелчка

		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaRaiseTest, Log, TEXT("ПОДЪЁМ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaRaiseTest, Error, TEXT("ПОДЪЁМ FAIL %s"), *What); }
		}
	};

	using FStage = void (*)(TSharedRef<FCtx>);

	// Запустить этап, когда пройдёт И не меньше Sec секунд реального времени, И не меньше Frames кадров. Только время
	//   нельзя: первое открытие окна грузит ассеты, кадр длится 0.7 с, и мировой таймер срабатывает в СЛЕДУЮЩЕМ же кадре —
	//   клик ещё не доставлен (первый прогон дал 35 ложных провалов «z 750 → 750»).
	static void After(TSharedRef<FCtx> C, float Sec, FStage Stage, int32 Frames = 6)
	{
		struct FPoll
		{
			// Опрос раз в кадр до выполнения обоих условий.
			static void Tick(TSharedRef<FCtx> C, double TEnd, uint64 FEnd, FStage Stage)
			{
				UWorld* W = C->W.Get();
				if (!W) { return; }
				if (FPlatformTime::Seconds() >= TEnd && GFrameCounter >= FEnd) { Stage(C); return; }
				W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, TEnd, FEnd, Stage]() { Tick(C, TEnd, FEnd, Stage); }));
			}
		};
		UWorld* W = C->W.Get();
		if (!W) { return; }
		const double TEnd = FPlatformTime::Seconds() + Sec;
		const uint64 FEnd = GFrameCounter + (uint64)FMath::Max(Frames, 1);
		W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, TEnd, FEnd, Stage]() { FPoll::Tick(C, TEnd, FEnd, Stage); }));
	}

	// ★ R5-fix-b 13.11.2026 (заключительный заход): проверка щелчка — когда он ДОСТАВЛЕН (отпускание) и прошло 3 кадра, а не через 14 (A/B) и 10 (D) кадров
	//   от заказа. Щелчку с удержанием 3/1 кадра хватает 5/3 кадров; лишние ~9 кадров на каждое из ~240 окон и 6 на каждый
	//   из 310 подъёмов раздела D под нагрузкой (три игры, ~15 к/с) выводили прогон за потолок пачки 600 с без строки ИТОГ.
	//   Подъём окна (перевешивание z) Slate делает в кадре нажатия/отпускания — проверка читает тот же итог.
	static void AfterClick(TSharedRef<FCtx> C, FStage Stage)
	{
		AsdaSelfTestSeq::AfterInput(C->W.Get(), [C, Stage]() { Stage(C); }, 0.05f, 3);
	}

	// Подсистема окон мира прогона.
	static UAsdaWindowSubsystem* WMOf(const TSharedRef<FCtx>& C)
	{
		UWorld* W = C->W.Get();
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	}

	// Установить консольную переменную (int).
	static void SetCv(const TCHAR* Name, int32 V)
	{
		AsdaCVarSave::SetForTest(Name, V);
	}
	// Прочитать консольную переменную (int; −1 — нет).
	static int32 GetCv(const TCHAR* Name)
	{
		IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(Name);
		return Cv ? Cv->GetInt() : -1;
	}

	// Точки, где окно Id ловит мышь (сетка Step по прямоугольнику корня).
	static void SampleHits(UAsdaWindowSubsystem* WM, FName Id, float Step, TArray<FVector2D>& Out)
	{
		Out.Reset();
		UUserWidget* Wd = WM ? WM->FindWindow(Id) : nullptr;
		if (!Wd || !Wd->GetRootWidget()) { return; }
		FVector2D Mn, Mx;
		if (!AsdaUi::WidgetAbsRect(Wd->GetRootWidget(), Mn, Mx)) { return; }
		for (double Y = Mn.Y + Step * 0.5; Y < Mx.Y; Y += Step)
		{
			for (double X = Mn.X + Step * 0.5; X < Mx.X; X += Step)
			{
				const FVector2D P(X, Y);
				if (WM->IsWindowHitAt(Id, P)) { Out.Add(P); }
			}
		}
	}

	// Габарит набора точек.
	static FBox2D BoxOf(const TArray<FVector2D>& Pts)
	{
		FBox2D B(ForceInit);
		for (const FVector2D& P : Pts) { B += P; }
		return B;
	}

	// Сдвинуть покрышку (все слоты детей корневого канваса) на абсолютную дельту; запомнить для отката.
	static void MoveCover(const TSharedRef<FCtx>& C, UAsdaWindowSubsystem* WM, FVector2D AbsDelta)
	{
		UUserWidget* Wd = WM->FindWindow(C->Cover);
		UPanelWidget* Root = Wd ? Cast<UPanelWidget>(Wd->GetRootWidget()) : nullptr;
		if (!Root) { return; }
		// Масштаб «абсолютные единицы на единицу канваса» — из ЖИВОЙ геометрии корня (масштаб вьюпорта без DPI окна
		//   давал недосдвиг: покрышка оставалась поверх всего проверяемого окна).
		float Scale = (float)Root->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
		if (Scale <= KINDA_SMALL_NUMBER) { Scale = UWidgetLayoutLibrary::GetViewportScale(C->W.Get()); }
		if (Scale <= KINDA_SMALL_NUMBER) { Scale = 1.f; }
		const FVector2D Local = AbsDelta / Scale;
		int32 NSlots = 0;   // сколько слотов сдвинуто (для журнала)
		for (int32 i = 0; i < Root->GetChildrenCount(); ++i)
		{
			UWidget* Ch = Root->GetChildAt(i);
			UCanvasPanelSlot* S = Ch ? Cast<UCanvasPanelSlot>(Ch->Slot) : nullptr;
			if (!S) { continue; }
			// Как `ShiftCanvasSlot` перетаскивания: у растянутого по оси слота сдвигаются ОБА края (SetPosition сдвинул бы
			//   только левый/верхний, и полноэкранная подложка покрышки осталась бы на месте).
			const FAnchors An = S->GetAnchors();
			FMargin M = S->GetOffsets();
			C->Moved.Emplace(S, M);
			M.Left += Local.X;
			if (!FMath::IsNearlyEqual(An.Minimum.X, An.Maximum.X)) { M.Right -= Local.X; }
			M.Top += Local.Y;
			if (!FMath::IsNearlyEqual(An.Minimum.Y, An.Maximum.Y)) { M.Bottom -= Local.Y; }
			S->SetOffsets(M);
			++NSlots;
		}
		UE_LOG(LogAsdaRaiseTest, Log, TEXT("ПОДЪЁМ инфо: покрышка «%s» сдвинута на (%.0f,%.0f) абс. = (%.1f,%.1f) канв., масштаб %.3f, слотов %d из %d детей корня %s"),
			*C->Cover.ToString(), AbsDelta.X, AbsDelta.Y, Local.X, Local.Y, Scale, NSlots, Root->GetChildrenCount(), *Root->GetClass()->GetName());
	}

	// Откатить сдвиг покрышки.
	static void UndoCover(const TSharedRef<FCtx>& C)
	{
		// В ОБРАТНОМ порядке: при двух сдвигах подряд первая запись хранит исходную позицию и обязана лечь последней.
		for (int32 i = C->Moved.Num() - 1; i >= 0; --i)
		{
			if (UCanvasPanelSlot* S = C->Moved[i].Key.Get()) { S->SetOffsets(C->Moved[i].Value); }
		}
		C->Moved.Reset();
	}

	// Видимость окна (true — показано).
	static bool IsShown(UAsdaWindowSubsystem* WM, FName Id)
	{
		UUserWidget* Wd = WM->FindWindow(Id);
		if (!Wd) { return false; }
		const ESlateVisibility V = Wd->GetVisibility();
		return V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
	}

	// Модалка, всплывшая от прошлой проверки (окно-сообщение подсистемы), гасит разбор мыши у всех окон ниже — закрыть и записать.
	static void CloseStrayModals(UAsdaWindowSubsystem* WM, const FString& Before)
	{
		if (!WM->IsAnyModalOpen()) { return; }
		for (const FName Id : WM->GetRegisteredWindowIds())
		{
			if (WM->GetWindowZOrder(Id) >= UAsdaWindowSubsystem::kModalZ && IsShown(WM, Id))
			{
				UE_LOG(LogAsdaRaiseTest, Warning, TEXT("ПОДЪЁМ инфо: перед «%s» открыта модалка «%s» (z %d) — закрываю"),
					*Before, *Id.ToString(), WM->GetWindowZOrder(Id));
				WM->CloseWindowSilently(Id);
			}
		}
		WM->ClearMsgBoxQueue(TEXT("самотест подъёма: модалка от прошлой проверки"));
	}

	// Подготовить окно к проверке: открыть или показать. false — открыть не удалось.
	static bool Prepare(UAsdaWindowSubsystem* WM, FItem& It)
	{
		It.bOpened = It.bVisForced = false;
		if (WM->FindWindow(It.Id))
		{
			It.ZWas = WM->GetWindowZOrder(It.Id);
			UUserWidget* Wd = WM->FindWindow(It.Id);
			if (!IsShown(WM, It.Id))
			{
				It.VisWas = Wd->GetVisibility();
				Wd->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				It.bVisForced = true;
			}
			return true;
		}
		if (It.Path.IsEmpty()) { return false; }
		UUserWidget* Wd = WM->OpenWindowByPath(It.Id, It.Path, 750, ESlateVisibility::SelfHitTestInvisible, false);
		if (!Wd) { return false; }
		It.bOpened = true;
		It.ZWas = WM->GetWindowZOrder(It.Id);
		return true;
	}

	// Вернуть окно в прежнее состояние.
	static void Restore(UAsdaWindowSubsystem* WM, const FItem& It)
	{
		if (It.bOpened) { WM->CloseWindowSilently(It.Id); return; }
		if (WM->FindWindow(It.Id) && WM->GetWindowZOrder(It.Id) != It.ZWas) { WM->SetWindowZOrder(It.Id, It.ZWas); }
		if (It.bVisForced) { if (UUserWidget* Wd = WM->FindWindow(It.Id)) { Wd->SetVisibility(It.VisWas); } }
	}

	// ★ R5-fix-b 13.11.2026 (заключительный заход, вторая запись): сетка попаданий окна и «кто ловит мышь» в каждой её точке — один проход на щелчок.
	//   `FindWindowCatchingAt` обходит деревья ВСЕХ окон; `StageClick` звал его по сетке трижды (`FindOverlapPoint` и дважды
	//   одинаковый `FindBodyPoint`) — 3–7 с на окно под нагрузкой, прогон 598,7 с при `t.MaxFPS 30` у потолка пачки 600 с.
	//   Между вызовами ничего не меняется (чтение в одном кадре) — точки, порядок и итог те же.
	struct FCatchGrid
	{
		TArray<FVector2D> Pts;   // точки сетки, где попадает само окно (порядок `SampleHits`)
		TArray<FName> By;        // кто ловит мышь в каждой точке
		bool bBuilt = false;     // сетка снята
	};
	struct FBodyGrids { FCatchGrid G10, G3; };   // шаг 10 и частый шаг 3 (мелкое окно)

	// Снять сетку с шагом Step один раз.
	static const FCatchGrid& CatchGrid(UAsdaWindowSubsystem* WM, FName Id, float Step, FCatchGrid& G)
	{
		if (!G.bBuilt)
		{
			SampleHits(WM, Id, Step, G.Pts);
			G.By.Reset(G.Pts.Num());
			for (const FVector2D& P : G.Pts) { G.By.Add(WM->FindWindowCatchingAt(P)); }
			G.bBuilt = true;
		}
		return G;
	}

	// Видимая точка тела окна Id без контролов со своим захватом; Gs — сетки щелчка (nullptr — свои).
	static bool FindBodyPoint(UAsdaWindowSubsystem* WM, FName Id, FVector2D& Out, bool bAllowCapturing = false, FBodyGrids* Gs = nullptr)
	{
		FBodyGrids Own;
		FBodyGrids& S = Gs ? *Gs : Own;
		const FCatchGrid* G = &CatchGrid(WM, Id, 10.f, S.G10);
		if (G->Pts.Num() < 30) { G = &CatchGrid(WM, Id, 3.f, S.G3); }   // мелкое окно — частая сетка
		TArray<FVector2D> Good;
		for (int32 i = 0; i < G->Pts.Num(); ++i)
		{
			const FVector2D& P = G->Pts[i];
			if (G->By[i] != Id) { continue; }
			if (!bAllowCapturing && WM->HasCapturingControlAt(Id, P)) { continue; }
			Good.Add(P);
		}
		if (Good.Num() == 0) { return false; }
		// Ближайшая к «центру масс» видимой части — подальше от краёв.
		FVector2D Cm = FVector2D::ZeroVector;
		for (const FVector2D& P : Good) { Cm += P; }
		Cm /= (double)Good.Num();
		double Best = TNumericLimits<double>::Max();
		for (const FVector2D& P : Good)
		{
			const double D = FVector2D::DistSquared(P, Cm);
			if (D < Best) { Best = D; Out = P; }
		}
		return true;
	}

	// Почему у окна не нашлось точки: попаданий, из них ловит само окно, чаще всего ловит другое, видимость виджета.
	static FString HitDiag(UAsdaWindowSubsystem* WM, FName Id)
	{
		TArray<FVector2D> Pts;
		SampleHits(WM, Id, 10.f, Pts);
		int32 Own = 0, Caps = 0;
		TMap<FName, int32> Others;
		for (const FVector2D& P : Pts)
		{
			const FName By = WM->FindWindowCatchingAt(P);
			if (By == Id) { ++Own; if (WM->HasCapturingControlAt(Id, P)) { ++Caps; } } else { ++Others.FindOrAdd(By); }
		}
		FName Top; int32 TopN = 0;
		for (const TPair<FName, int32>& Kv : Others) { if (Kv.Value > TopN) { TopN = Kv.Value; Top = Kv.Key; } }
		UUserWidget* Wd = WM->FindWindow(Id);
		return FString::Printf(TEXT("попаданий %d, ловит само %d (из них на кнопках/полях %d), чаще другое «%s» ×%d, вид=%d"),
			Pts.Num(), Own, Caps, *Top.ToString(), TopN, Wd ? (int32)Wd->GetVisibility() : -1);
	}

	// Точка перекрытия: окно Id ловило бы мышь, но ловит покрышка. G10 — сетка щелчка шага 10 (nullptr — своя).
	static bool FindOverlapPoint(UAsdaWindowSubsystem* WM, FName Id, FName Cov, FVector2D& Out, FCatchGrid* G10 = nullptr)
	{
		FCatchGrid Own;
		const FCatchGrid& G = CatchGrid(WM, Id, 10.f, G10 ? *G10 : Own);
		for (int32 i = 0; i < G.Pts.Num(); ++i) { if (G.By[i] == Cov) { Out = G.Pts[i]; return true; } }
		return false;
	}

	static void StageOpen(TSharedRef<FCtx> C);
	static void StagePlace(TSharedRef<FCtx> C);
	static void StagePlace2(TSharedRef<FCtx> C);
	static void StageClick(TSharedRef<FCtx> C);
	static void StageVerify(TSharedRef<FCtx> C);
	static void StartCtrl(TSharedRef<FCtx> C);
	static void StageFocusChat(TSharedRef<FCtx> C);
	static void StartSat(TSharedRef<FCtx> C);
	static void Finish(TSharedRef<FCtx> C);

	// Следующее окно текущего списка (или следующий раздел).
	static void Next(TSharedRef<FCtx> C)
	{
		++C->Idx;
		if (C->Idx < C->Cur->Num()) { After(C, 0.03f, &StageOpen); return; }
		if (!C->bCtrlMode) { After(C, 0.1f, &StartCtrl); return; }
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), 1);
		C->bCtrlMode = false;
		After(C, 0.1f, &StageFocusChat);
	}

	// A/B-1: открыть окно и покрышку.
	static void StageOpen(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		CloseStrayModals(WM, (*C->Cur)[C->Idx].Id.ToString());
		// ★ 11.11.2026 (D4, разбор красных эталона 7.6.0): прошлая проверка щёлкала по кнопке трея, курсор остался на ней и висела
		//   её подсказка («Окно навыков ('K')»). Пока подсказка показана, нажатие окно не поднимает — правило оригинала
		//   (`asda.UI.RaiseTooltipGate`, AsdaWindowSubsystem_Drag.cpp), поэтому следующий щелчок (MINISTATE_WIN) давал ложный
		//   провал «z 95 → 95». Перед каждой проверкой курсор уводится в угол и подсказка гасится (гейт подсказки проверяет раздел H).
		FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 2.0));
		if (UWorld* TipW = C->W.Get())
		{
			if (UAsdaItemTooltipSubsystem* StrayTip = TipW->GetGameInstance() ? TipW->GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				StrayTip->HideTooltip();
			}
		}
		FItem& It = (*C->Cur)[C->Idx];
		C->bRetried = false;
		if (!Prepare(WM, It))
		{
			++C->Skip; C->Skipped.Add(FString::Printf(TEXT("%s (не открылось)"), *It.Id.ToString()));
			Next(C); return;
		}
		C->Cover = (It.Id == C->CoverA) ? C->CoverB : C->CoverA;
		const FName Other = (C->Cover == C->CoverA) ? C->CoverB : C->CoverA;
		if (UUserWidget* Ow = WM->FindWindow(Other)) { if (Other != It.Id) { Ow->SetVisibility(ESlateVisibility::Collapsed); } }
		if (UUserWidget* Cw = WM->FindWindow(C->Cover)) { Cw->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		After(C, 0.12f, &StagePlace);
	}

	// A/B-2: покрышку — над окном и верхним левым углом в центр его тела.
	static void StagePlace(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		FItem& It = (*C->Cur)[C->Idx];
		const int32 ZT = WM->GetWindowZOrder(It.Id);
		// Покрышка — ровно на ступень выше проверяемого (ревизия: с полосой AlwaysTop покрышка, оставшаяся в 86xx, была бы выше
		//   потолка обычного подъёма 8599, и поднятое окно законно оставалось бы под ней).
		if (ZT < UAsdaWindowSubsystem::kPriorityZ - 1 && WM->GetWindowZOrder(C->Cover) != ZT + 1)
		{
			WM->SetWindowZOrder(C->Cover, ZT + 1);
		}
		// Перевешивание сбросило геометрию покрышки — выборка и сдвиг через кадры.
		After(C, 0.05f, &StagePlace2, 6);
	}

	// A/B-2б: сдвиг покрышки верхним левым углом в центр тела проверяемого окна.
	static void StagePlace2(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		FItem& It = (*C->Cur)[C->Idx];
		TArray<FVector2D> PT, PC;
		SampleHits(WM, It.Id, 12.f, PT);
		SampleHits(WM, C->Cover, 12.f, PC);
		if (PT.Num() > 0 && PC.Num() > 0)
		{
			const FBox2D BT = BoxOf(PT), BC = BoxOf(PC);
			MoveCover(C, WM, BT.GetCenter() - BC.Min);
		}
		After(C, 0.12f, &StageClick);
	}

	// A/B-3: найти видимую точку тела и щёлкнуть.
	static void StageClick(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		FItem& It = (*C->Cur)[C->Idx];
		C->bEligible = WM->IsBodyRaiseEligible(It.Id, &C->Why);
		FBodyGrids Gs;   // ★ R5-fix-b 13.11.2026 (заключительный заход, вторая запись): одна сетка «кто ловит» на все поиски этого щелчка
		C->bOverlap = FindOverlapPoint(WM, It.Id, C->Cover, C->OverlapPt, &Gs.G10);
		C->bOnControl = false;
		const bool bBody = FindBodyPoint(WM, It.Id, C->ClickPt, false, &Gs);   // прежде — дважды тот же вызов
		if (!bBody && FindBodyPoint(WM, It.Id, C->ClickPt, /*bAllowCapturing*/ true, &Gs))
		{
			// Видимая часть — одни кнопки/поля: эталон поднимает и по ним (нажатие в плане ЛЮБОГО контрола окна).
			C->bOnControl = true;
		}
		else if (!bBody)
		{
			if (!C->bRetried)
			{
				// Вторая раскладка: покрышка накрывает только правый нижний угол окна (мелкое окно накрывалось целиком).
				C->bRetried = true;
				UndoCover(C);
				TArray<FVector2D> PT, PC;
				SampleHits(WM, It.Id, 6.f, PT);
				SampleHits(WM, C->Cover, 12.f, PC);
				if (PT.Num() > 0 && PC.Num() > 0)
				{
					const FBox2D BT = BoxOf(PT), BC = BoxOf(PC);
					MoveCover(C, WM, (BT.Max - BT.GetSize() * 0.25) - BC.Min);
				}
				After(C, 0.12f, &StageClick, 6);
				return;
			}
			++C->Skip;
			C->Skipped.Add(FString::Printf(TEXT("%s (z %d, видимого тела без кнопок нет: %s; %s)"), *It.Id.ToString(), WM->GetWindowZOrder(It.Id), *HitDiag(WM, It.Id), *C->Why));
			UndoCover(C); Restore(WM, It); Next(C); return;
		}
		C->ZBefore = WM->GetWindowZOrder(It.Id);
		WM->SimulateClickAt(C->ClickPt, /*HoldFrames*/ 3);
		AfterClick(C, &StageVerify);
	}

	// A/B-4: проверить итог и вернуть всё на место.
	static void StageVerify(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		FItem& It = (*C->Cur)[C->Idx];
		const int32 ZT = WM->GetWindowZOrder(It.Id), ZC = WM->GetWindowZOrder(C->Cover);
		const FString TagS = FString(C->bCtrlMode ? TEXT("B ключ 0") : TEXT("A")) + (C->bOnControl ? TEXT(" [клик по контролу]") : TEXT(""));
		const TCHAR* Tag = *TagS;
		if (C->bEligible)
		{
			const bool bTop = ZT > ZC;
			// В точке перекрытия мышь больше не ловит покрышка (ловит само окно или его дочернее окно — оно обязано быть выше).
			const FName AtOverlap = C->bOverlap ? WM->FindWindowCatchingAt(C->OverlapPt) : NAME_None;
			const bool bOverOk = !C->bOverlap || AtOverlap != C->Cover;
			C->Expect(bTop && bOverOk, FString::Printf(TEXT("%s «%s»: клик по телу (%.0f,%.0f) → поднято над покрышкой «%s»: z %d → %d (покрышка %d), перекрытие %s, в точке перекрытия ловит «%s» | %s"),
				Tag, *It.Id.ToString(), C->ClickPt.X, C->ClickPt.Y, *C->Cover.ToString(), C->ZBefore, ZT, ZC,
				C->bOverlap ? TEXT("было") : TEXT("нет"), *AtOverlap.ToString(), *C->Why));
			if (!C->bCtrlMode) { ++C->NRaised; if (bTop && bOverOk) { C->RaisedInA.Add(It.Id); } }
		}
		else
		{
			C->Expect(ZT == C->ZBefore, FString::Printf(TEXT("%s «%s»: исключение — клик по телу (%.0f,%.0f), z %d → %d (не поднято) | %s"),
				Tag, *It.Id.ToString(), C->ClickPt.X, C->ClickPt.Y, C->ZBefore, ZT, *C->Why));
			if (!C->bCtrlMode) { ++C->NExcl; }
		}
		if (!C->bOverlap && !C->bCtrlMode) { ++C->NNoOverlap; }
		UndoCover(C);
		Restore(WM, It);
		Next(C);
	}

	// B: контрольная пара — ключ 0, неперетаскиваемые поднимаемые окна (до 15).
	static void StartCtrl(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		UE_LOG(LogAsdaRaiseTest, Log, TEXT("ПОДЪЁМ инфо: раздел A окончен — поднято %d, исключений %d, без перекрытия %d, пропущено %d"),
			C->NRaised, C->NExcl, C->NNoOverlap, C->Skip);
		const UAsdaJuiWindowMetaAsset* Meta = WM->GetJuiMeta();
		// Выборка контроля — окна, которые раздел A ПОДНЯЛ кликом по телу и у которых нет ручек (прежнее правило их не поднимало).
		for (const FItem& It : C->Items)
		{
			if (C->Ctrl.Num() >= 15) { break; }
			if (!C->RaisedInA.Contains(It.Id) || It.Id == C->CoverA || It.Id == C->CoverB) { continue; }
			const FAsdaJuiWindowMeta* M = Meta ? Meta->FindWindow(It.Id) : nullptr;
			if (M && M->DragHandles.Num() > 0) { continue; }
			C->Ctrl.Add(It);
		}
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), 0);
		C->bCtrlMode = true;
		C->Cur = &C->Ctrl;
		C->Idx = 0;
		if (C->Ctrl.Num() == 0) { C->Expect(false, TEXT("B: выборка контроля пуста")); C->Idx = -1; Next(C); return; }
		After(C, 0.1f, &StageOpen);
	}

	// ── C. ФОКУС ─────────────────────────────────────────────────────────────────────────────────
	static void StageFocusChat2(TSharedRef<FCtx> C);
	static void StageFocusChat3(TSharedRef<FCtx> C);
	static void StageEdit1(TSharedRef<FCtx> C);
	static void StageEdit1b(TSharedRef<FCtx> C);
	static void StageEdit1c(TSharedRef<FCtx> C);
	static void StageEdit2(TSharedRef<FCtx> C);
	static void StageEdit3(TSharedRef<FCtx> C);
	static void StageEdit4(TSharedRef<FCtx> C);
	static int32 GChatPass = 0;            // 0 — ключ 1, 1 — контроль ключом 0

	// Фокус клавиатуры лежит в окне Id и это поле ввода.
	static bool FocusInWindow(UAsdaWindowSubsystem* WM, FName Id)
	{
		const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
		if (!F.IsValid() || !F->GetType().ToString().Contains(TEXT("Editable"))) { return false; }
		UUserWidget* Wd = WM->FindWindow(Id);
		const TSharedPtr<SWidget> Root = Wd ? Wd->GetCachedWidget() : nullptr;
		for (TSharedPtr<SWidget> P = F; P.IsValid(); P = P->GetParentWidget()) { if (P == Root) { return true; } }
		return false;
	}

	static void StageFocusChat(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaChatWindowSubsystem* Ch = W ? W->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
		if (!WM || !Ch) { C->Expect(false, TEXT("C: нет подсистем")); After(C, 0.1f, &StartSat); return; }
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), GChatPass == 0 ? 1 : 0);
		CloseStrayModals(WM, TEXT("раздел C"));
		if (UUserWidget* Cw = WM->FindWindow(C->CoverA)) { Cw->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		if (UUserWidget* Cw = WM->FindWindow(C->CoverB)) { Cw->SetVisibility(ESlateVisibility::Collapsed); }
		WM->SetWindowZOrder(C->CoverA, UAsdaWindowSubsystem::kRaiseBandFloor + 5);
		Ch->OpenInputWithText(TEXT("проверка фокуса"));
		After(C, 0.3f, &StageFocusChat2);
	}

	static void StageFocusChat2(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaChatWindowSubsystem* Ch = W->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>();
		C->Expect(Ch->IsInputActive() && FocusInWindow(WM, TEXT("CHAT_NEW")),
			FString::Printf(TEXT("C%d: поле чата открыто и в фокусе | %s"), GChatPass, *Ch->DebugInputState()));
		C->ZBefore = WM->GetWindowZOrder(C->CoverA);
		if (!FindBodyPoint(WM, C->CoverA, C->ClickPt)) { C->Expect(false, FString::Printf(TEXT("C: у покрышки нет видимого тела: %s"), *HitDiag(WM, C->CoverA))); After(C, 0.1f, &StartSat); return; }
		WM->SimulateClickAt(C->ClickPt, 3);
		After(C, 0.15f, &StageFocusChat3, 14);
	}

	static void StageFocusChat3(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaChatWindowSubsystem* Ch = W->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>();
		const FString St = Ch->DebugInputState();
		const bool bText = St.Contains(TEXT("текст«проверка фокуса»"));
		const bool bFocus = Ch->IsInputActive() && FocusInWindow(WM, TEXT("CHAT_NEW"));
		if (GChatPass == 0)
		{
			C->Expect(bText && bFocus, FString::Printf(TEXT("C0: клик по телу «%s» (окно выше чата) → поле чата сохранило ТЕКСТ и ФОКУС (эталон: чат в начале списка, не обойдён) | %s"),
				*C->CoverA.ToString(), *St));
			C->Expect(WM->GetWindowZOrder(C->CoverA) >= C->ZBefore, FString::Printf(TEXT("C0: окно «%s» осталось/встало наверх: z %d → %d"),
				*C->CoverA.ToString(), C->ZBefore, WM->GetWindowZOrder(C->CoverA)));
		}
		else
		{
			C->Expect(bText && !bFocus, FString::Printf(TEXT("C1 контроль (ключ 0): тот же клик → фокус чата СНЯТ Slate (прежнее поведение порта), текст на месте | %s"), *St));
		}
		Ch->CloseInput();
		++GChatPass;
		if (GChatPass < 2) { After(C, 0.2f, &StageFocusChat); return; }
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), 1);
		After(C, 0.2f, &StageEdit1);
	}

	// C2: окно с полем ввода под покрышкой.
	static int32 GEditPass = 0;            // 0 — ключ 1, 1 — контроль ключом 0
	static void StageEdit1(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), GEditPass == 0 ? 1 : 0);
		CloseStrayModals(WM, TEXT("раздел C2"));
		// Найти поднимаемое неперетаскиваемое окно каталога с полем ввода.
		if (C->EditWin.IsNone())
		{
			const UAsdaJuiWindowMetaAsset* Meta = WM->GetJuiMeta();
			for (FItem& It : C->Items)
			{
				if (It.Path.IsEmpty() || It.Id == C->CoverA || It.Id == C->CoverB) { continue; }
				const FAsdaJuiWindowMeta* M = Meta ? Meta->FindWindow(It.Id) : nullptr;
				if (!M || M->bAlwaysTop || M->DragHandles.Num() > 0 || It.Id.ToString().Contains(TEXT("MSGBOX"))) { continue; }
				if (!Prepare(WM, It)) { continue; }
				UEditableText* Found = nullptr;
				if (UUserWidget* Wd = WM->FindWindow(It.Id))
				{
					Wd->WidgetTree->ForEachWidget([&Found](UWidget* X) { if (!Found) { if (UEditableText* E = Cast<UEditableText>(X)) { if (E->IsVisible()) { Found = E; } } } });
				}
				FString Why;
				if (Found && WM->IsBodyRaiseEligible(It.Id, &Why)) { C->EditWin = It.Id; C->Edit = Found; break; }
				Restore(WM, It);
			}
		}
		else
		{
			for (FItem& It : C->Items) { if (It.Id == C->EditWin) { Prepare(WM, It); C->Edit = nullptr;
				if (UUserWidget* Wd = WM->FindWindow(It.Id)) { Wd->WidgetTree->ForEachWidget([C](UWidget* X) { if (!C->Edit.IsValid()) { if (UEditableText* E = Cast<UEditableText>(X)) { if (E->IsVisible()) { C->Edit = E; } } } }); }
				break; } }
		}
		if (C->EditWin.IsNone() || !C->Edit.IsValid()) { C->Expect(false, TEXT("C2: окно с полем ввода не найдено")); After(C, 0.1f, &StartSat); return; }
		// Геометрия только что открытого окна появится через кадры — раскладка покрышки в следующем этапе.
		After(C, 0.1f, &StageEdit1b, 6);
	}

	// C2-1b: покрышка над окном с полем (поле остаётся видимым), текст и фокус в поле.
	static void StageEdit1b(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM || !C->Edit.IsValid()) { C->Expect(false, TEXT("C2: поле пропало")); After(C, 0.1f, &StartSat); return; }
		C->Cover = C->CoverA;
		if (UUserWidget* Cw = WM->FindWindow(C->CoverA)) { Cw->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		WM->SetWindowZOrder(C->Cover, WM->GetWindowZOrder(C->EditWin) + 1);
		// Перевешивание сбросило геометрию покрышки — раскладка через кадры.
		After(C, 0.1f, &StageEdit1c, 6);
	}

	// C2-1c: покрышка накрывает окно, кроме полосы с полем: верхний левый угол — под полем; текст и фокус в поле.
	static void StageEdit1c(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM || !C->Edit.IsValid()) { C->Expect(false, TEXT("C2: поле пропало")); After(C, 0.1f, &StartSat); return; }
		FVector2D EMn, EMx;
		TArray<FVector2D> PC;
		SampleHits(WM, C->Cover, 12.f, PC);
		if (AsdaUi::WidgetAbsRect(C->Edit.Get(), EMn, EMx) && PC.Num() > 0)
		{
			MoveCover(C, WM, FVector2D(EMn.X - 40.0, EMx.Y + 4.0) - BoxOf(PC).Min);
		}
		C->Edit->SetText(FText::FromString(TEXT("abc123")));
		C->Edit->SetKeyboardFocus();
		After(C, 0.15f, &StageEdit2, 8);
	}

	// C2-2: клик по САМОМУ полю (окно ниже покрышки) → окно поднято на отпускании, фокус и текст в поле.
	static void StageEdit2(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		FVector2D EMn, EMx;
		const bool bR = C->Edit.IsValid() && AsdaUi::WidgetAbsRect(C->Edit.Get(), EMn, EMx);
		const FVector2D P = (EMn + EMx) * 0.5;
		C->ZBefore = WM->GetWindowZOrder(C->EditWin);
		const FName AtEdit = bR ? WM->FindWindowCatchingAt(P) : NAME_None;   // кто ловит мышь в центре поля
		const bool bVisible = bR && AtEdit == C->EditWin;
		C->bOverlap = FindOverlapPoint(WM, C->EditWin, C->Cover, C->OverlapPt);
		C->Expect(bVisible && C->bOverlap, FString::Printf(TEXT("C2-%d: «%s» под покрышкой «%s» (перекрытие %d), поле видно в (%.0f,%.0f) — ловит «%s», прямоугольник поля %d"),
			GEditPass, *C->EditWin.ToString(), *C->Cover.ToString(), C->bOverlap ? 1 : 0, P.X, P.Y, *AtEdit.ToString(), bR ? 1 : 0));
		if (!bVisible) { After(C, 0.1f, &StartSat); return; }
		WM->SimulateClickAt(P, 3);
		After(C, 0.15f, &StageEdit3, 14);
	}

	static void StageEdit3(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		const int32 ZT = WM->GetWindowZOrder(C->EditWin), ZC = WM->GetWindowZOrder(C->Cover);
		const bool bText = C->Edit.IsValid() && C->Edit->GetText().ToString() == TEXT("abc123");
		const bool bFocus = FocusInWindow(WM, C->EditWin);
		if (GEditPass == 0)
		{
			C->Expect(ZT > ZC && bText && bFocus, FString::Printf(TEXT("C2-0: клик по полю → «%s» поднято (z %d → %d, покрышка %d), текст «abc123» %s, фокус в поле %s"),
				*C->EditWin.ToString(), C->ZBefore, ZT, ZC, bText ? TEXT("на месте") : TEXT("ПОТЕРЯН"), bFocus ? TEXT("есть") : TEXT("ПОТЕРЯН")));
			// Ещё раз под покрышку и клик по ТЕЛУ (не по полю): текст обязан остаться.
			WM->SetWindowZOrder(C->Cover, ZT + 1);
			C->ZBefore = WM->GetWindowZOrder(C->EditWin);
			if (C->Edit.IsValid()) { C->Edit->SetKeyboardFocus(); }
			if (!FindBodyPoint(WM, C->EditWin, C->ClickPt)) { C->Expect(false, TEXT("C3: тела окна с полем не видно")); After(C, 0.1f, &StageEdit4); return; }
			WM->SimulateClickAt(C->ClickPt, 3);
			After(C, 0.15f, &StageEdit4, 14);
			return;
		}
		C->Expect(ZT == C->ZBefore && bText && bFocus, FString::Printf(TEXT("C2-1 контроль (ключ 0): клик по полю → окно НЕ поднято (z %d → %d), текст %s, фокус %s"),
			C->ZBefore, ZT, bText ? TEXT("на месте") : TEXT("ПОТЕРЯН"), bFocus ? TEXT("в поле") : TEXT("снят")));
		After(C, 0.05f, &StageEdit4);
	}

	static void StageEdit4(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (GEditPass == 0)
		{
			const int32 ZT = WM->GetWindowZOrder(C->EditWin), ZC = WM->GetWindowZOrder(C->Cover);
			const bool bText = C->Edit.IsValid() && C->Edit->GetText().ToString() == TEXT("abc123");
			const bool bFocus = FocusInWindow(WM, C->EditWin);
			C->Expect(ZT > ZC && bText, FString::Printf(TEXT("C3: клик по ТЕЛУ «%s» (%.0f,%.0f) → поднято (z %d → %d, покрышка %d), текст поля %s; фокус поля после клика по телу: %s (эталон: план корня обходит детей — поле фокус снимает)"),
				*C->EditWin.ToString(), C->ClickPt.X, C->ClickPt.Y, C->ZBefore, ZT, ZC, bText ? TEXT("на месте") : TEXT("ПОТЕРЯН"), bFocus ? TEXT("остался") : TEXT("снят")));
		}
		UndoCover(C);
		for (const FItem& It : C->Items) { if (It.Id == C->EditWin) { Restore(WM, It); break; } }
		++GEditPass;
		if (GEditPass < 2) { After(C, 0.2f, &StageEdit1); return; }
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), 1);
		After(C, 0.2f, &StartSat);
	}

	// ── D. 300 ПОДЪЁМОВ ─────────────────────────────────────────────────────────────────────────
	static void StartSat2(TSharedRef<FCtx> C);
	static void SatStep(TSharedRef<FCtx> C);
	static void SatCheck(TSharedRef<FCtx> C);
	static void StageRmb(TSharedRef<FCtx> C);
	static void StageRmb2(TSharedRef<FCtx> C);
	static void StageAt1(TSharedRef<FCtx> C);
	static void StageBtn1(TSharedRef<FCtx> C);
	static void StageBtn2(TSharedRef<FCtx> C);
	static void StageBtn3(TSharedRef<FCtx> C);
	static void StageTip1(TSharedRef<FCtx> C);
	static void StageTip2(TSharedRef<FCtx> C);
	static void StageTip3(TSharedRef<FCtx> C);
	static void StageTip4(TSharedRef<FCtx> C);
	static void StageTip5(TSharedRef<FCtx> C);
	static void StageAfterTip(TSharedRef<FCtx> C);
	static bool ExclusivePoint(UAsdaWindowSubsystem* WM, FName Id, FName Other, FVector2D& Out);
	static bool FocusInWindow(UAsdaWindowSubsystem* WM, FName Id);
	static int32 GBtnPass = 0;             // раздел G: 0 — ключи по умолчанию, 1 — RaiseFocusAtPress 1, 2 — RaiseOnBodyClickAll 0
	static int32 GTipPass = 0;             // раздел H: 0 — гейт подсказки 1, 1 — контроль 0

	static void StartSat(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), C->bSatCtrl ? 0 : 1);
		CloseStrayModals(WM, TEXT("раздел D"));
		C->SatA = C->CoverA; C->SatB = C->CoverB; C->Cover = C->SatB;
		for (const FName Id : { C->SatA, C->SatB }) { if (UUserWidget* Wd = WM->FindWindow(Id)) { Wd->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } }
		UndoCover(C);
		// Старт у потолка обычного слоя (8599 — с ревизии подъёма под полосой AlwaysTop 8600…8699).
		WM->SetWindowZOrder(C->SatA, C->bSatCtrl ? 1000 : UAsdaWindowSubsystem::kAlwaysTopZ - 200);
		WM->SetWindowZOrder(C->SatB, C->bSatCtrl ? 1001 : UAsdaWindowSubsystem::kAlwaysTopZ - 199);
		// Перевешивание сбросило геометрию пары — выборка точек и сдвиг через кадры.
		After(C, 0.1f, &StartSat2, 6);
	}

	// D-2: B верхним левым углом в центр A (иначе крупное B накрывает A целиком).
	static void StartSat2(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		TArray<FVector2D> PA, PB;
		SampleHits(WM, C->SatA, 12.f, PA);
		SampleHits(WM, C->SatB, 12.f, PB);
		if (PA.Num() && PB.Num()) { MoveCover(C, WM, BoxOf(PA).GetCenter() - BoxOf(PB).Min); }
		C->SatIter = 0; C->SatFail = 0; C->SatMaxZ = 0; C->SatCtrlFlips = 0;
		C->SatN = C->bSatCtrl ? 10 : 300;
		After(C, 0.15f, &SatStep);
	}

	// Точка тела окна Id, которую не ловит второе окно пары (видна при любом порядке).
	static bool ExclusivePoint(UAsdaWindowSubsystem* WM, FName Id, FName Other, FVector2D& Out)
	{
		TArray<FVector2D> Pts;
		SampleHits(WM, Id, 10.f, Pts);
		for (const FVector2D& P : Pts)
		{
			if (!WM->IsWindowHitAt(Other, P) && WM->FindWindowCatchingAt(P) == Id && !WM->HasCapturingControlAt(Id, P)) { Out = P; return true; }
		}
		return false;
	}

	static void SatStep(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (C->SatIter == 0)
		{
			const bool bA = ExclusivePoint(WM, C->SatA, C->SatB, C->PA), bB = ExclusivePoint(WM, C->SatB, C->SatA, C->PB);
			FVector2D Ov;
			const bool bOv = FindOverlapPoint(WM, C->SatA, C->SatB, Ov);
			C->Expect(bA && bB && bOv, FString::Printf(TEXT("D%s: пара «%s»/«%s» перекрыта (%d), точки только-A (%.0f,%.0f) и только-B (%.0f,%.0f)"),
				C->bSatCtrl ? TEXT(" контроль") : TEXT(""), *C->SatA.ToString(), *C->SatB.ToString(), bOv ? 1 : 0, C->PA.X, C->PA.Y, C->PB.X, C->PB.Y));
			if (!(bA && bB))
			{
				TArray<FVector2D> QA, QB;
				SampleHits(WM, C->SatA, 12.f, QA);
				SampleHits(WM, C->SatB, 12.f, QB);
				const FBox2D BA = BoxOf(QA), BB = BoxOf(QB);
				UE_LOG(LogAsdaRaiseTest, Warning, TEXT("ПОДЪЁМ D: A — %s, габарит (%.0f,%.0f)-(%.0f,%.0f); B — %s, габарит (%.0f,%.0f)-(%.0f,%.0f)"),
					*HitDiag(WM, C->SatA), BA.Min.X, BA.Min.Y, BA.Max.X, BA.Max.Y, *HitDiag(WM, C->SatB), BB.Min.X, BB.Min.Y, BB.Max.X, BB.Max.Y);
				Finish(C); return;
			}
		}
		// Щёлкаем по видимой части НИЖНЕГО (при равенстве z — по A).
		C->bSatClickedA = WM->GetWindowZOrder(C->SatA) <= WM->GetWindowZOrder(C->SatB);
		WM->SimulateClickAt(C->bSatClickedA ? C->PA : C->PB, 1);
		AfterClick(C, &SatCheck);
	}

	static void SatCheck(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		const int32 ZA = WM->GetWindowZOrder(C->SatA), ZB = WM->GetWindowZOrder(C->SatB);
		C->SatMaxZ = FMath::Max(C->SatMaxZ, FMath::Max(ZA, ZB));
		// Нижнее, по которому щёлкнули, обязано стать СТРОГО выше второго.
		const bool bRaised = C->bSatClickedA ? (ZA > ZB) : (ZB > ZA);
		if (C->bSatCtrl) { if (bRaised) { ++C->SatCtrlFlips; } }
		else if (!bRaised) { ++C->SatFail; UE_LOG(LogAsdaRaiseTest, Warning, TEXT("ПОДЪЁМ D: итерация %d — щёлкнутое %s не встало наверх (A %d, B %d)"), C->SatIter, C->bSatClickedA ? TEXT("A") : TEXT("B"), ZA, ZB); }
		++C->SatIter;
		if (C->SatIter < C->SatN) { After(C, 0.03f, &SatStep); return; }
		if (!C->bSatCtrl)
		{
			C->Expect(C->SatFail == 0 && C->SatMaxZ < UAsdaWindowSubsystem::kAlwaysTopZ - 1,
				FString::Printf(TEXT("D: %d подъёмов кликом по телу (старт z %d): провалов порядка %d, макс z %d (< %d — потолок обычного слоя не достигнут)"),
					C->SatN, UAsdaWindowSubsystem::kAlwaysTopZ - 200, C->SatFail, C->SatMaxZ, UAsdaWindowSubsystem::kAlwaysTopZ - 1));
			UndoCover(C);
			C->bSatCtrl = true;
			After(C, 0.15f, &StartSat);
			return;
		}
		FString WhyA;
		const bool bOldRaises = WM->IsBodyRaiseEligible(C->SatA, &WhyA);   // ключ 0 сейчас стоит
		C->Expect(bOldRaises ? C->SatCtrlFlips > 0 : C->SatCtrlFlips == 0,
			FString::Printf(TEXT("D контроль (ключ 0): %d кликов по телу нижнего — перестановок %d (ожидание: %s, «%s»)"),
				C->SatN, C->SatCtrlFlips, bOldRaises ? TEXT("есть — окно перетаскиваемое") : TEXT("нет"), *WhyA));
		// E: [ОРИГИНАЛ] нажатие ПКМ тоже поднимает (событие 14 `uiContainerHandleMouse`). Покрышка ещё сдвинута — пара та же.
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), 1);
		WM->SetWindowZOrder(C->SatA, 1000);
		WM->SetWindowZOrder(C->SatB, 1001);
		After(C, 0.1f, &StageRmb, 6);
	}

	// E-1: ПКМ по видимой части нижнего окна пары.
	static void StageRmb(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		C->ZBefore = WM->GetWindowZOrder(C->SatA);
		WM->SimulateClickAt(C->PA, 3, /*bRightButton*/ true);
		After(C, 0.15f, &StageRmb2, 14);
	}

	// E-2: проверить подъём по ПКМ.
	static void StageRmb2(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		const int32 ZA = WM->GetWindowZOrder(C->SatA), ZB = WM->GetWindowZOrder(C->SatB);
		C->Expect(ZA > ZB, FString::Printf(TEXT("E: ПКМ по телу нижнего «%s» (%.0f,%.0f) → поднято: z %d → %d (второе %d)"),
			*C->SatA.ToString(), C->PA.X, C->PA.Y, C->ZBefore, ZA, ZB));
		UndoCover(C);
		After(C, 0.1f, &StageAt1, 6);
	}

	// ── F. РАБОЧИЙ AlwaysTop И ПОЛОСА AlwaysTop (ревизия: КРИТ-1, СРЕДН-1) ─────────────────────────────────────
	// Путь WBP окна каталога (пусто — нет в каталоге).
	static FString PathOf(const TSharedRef<FCtx>& C, FName Id)
	{
		for (const FItem& It : C->Items) { if (It.Id == Id) { return It.Path; } }
		return FString();
	}

	static void StageAt1(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		CloseStrayModals(WM, TEXT("раздел F"));
		const FName kQ(TEXT("QUEST_WIN")), kX(TEXT("EXTRACT_WIN"));
		for (const FName Id : { kQ, kX })
		{
			if (!WM->FindWindow(Id)) { WM->OpenWindowByPath(Id, PathOf(C, Id), 750, ESlateVisibility::SelfHitTestInvisible, false); C->OpenedF.Add(Id); }
		}
		const int32 ZQ = WM->GetWindowZOrder(kQ), ZX = WM->GetWindowZOrder(kX);
		C->Expect(!WM->IsWindowAlwaysTop(kQ) && ZQ < UAsdaWindowSubsystem::kAlwaysTopZ,
			FString::Printf(TEXT("F1: QUEST_WIN — рабочий AlwaysTop снят (эталон quest.c:1524, в .JUI 1): z %d в обычном слое"), ZQ));
		C->Expect(WM->IsWindowAlwaysTop(kX) && ZX >= UAsdaWindowSubsystem::kAlwaysTopZ && ZX <= UAsdaWindowSubsystem::kAlwaysTopZMax,
			FString::Printf(TEXT("F2: EXTRACT_WIN — рабочий AlwaysTop (эталон item_enhance.c:679, в .JUI 0): z %d в полосе AlwaysTop 8600…8699"), ZX));
		FString WhyQ, WhyX;
		const bool bQ = WM->IsBodyRaiseEligible(kQ, &WhyQ), bX = WM->IsBodyRaiseEligible(kX, &WhyX);
		C->Expect(bQ && !bX, FString::Printf(TEXT("F3: решение подъёма — QUEST_WIN %d («%s»), EXTRACT_WIN %d («%s»)"), bQ ? 1 : 0, *WhyQ, bX ? 1 : 0, *WhyX));
		// Обычное окно, поднятое на верх, остаётся ПОД AlwaysTop (эталон: перед первым AlwaysTop).
		if (UUserWidget* Cw = WM->FindWindow(C->CoverA)) { Cw->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		WM->SetWindowZOrder(C->CoverA, 1000);
		WM->BringWindowToFront(C->CoverA);
		const int32 ZA = WM->GetWindowZOrder(C->CoverA);
		C->Expect(ZA < WM->GetWindowZOrder(kX) && ZA <= UAsdaWindowSubsystem::kAlwaysTopZ - 1,
			FString::Printf(TEXT("F4: обычное «%s» поднято на верх обычного слоя z %d — ниже AlwaysTop EXTRACT_WIN %d"), *C->CoverA.ToString(), ZA, WM->GetWindowZOrder(kX)));
		// Контрольная пара 1: признак только из меты (QUEST_WIN = 1) — окно уходит в полосу AlwaysTop и щелчком не поднимается.
		WM->SetWindowAlwaysTop(kQ, true);
		FString WhyQ1;
		const bool bQ1 = WM->IsBodyRaiseEligible(kQ, &WhyQ1);
		const int32 ZQ1 = WM->GetWindowZOrder(kQ);
		C->Expect(!bQ1 && ZQ1 >= UAsdaWindowSubsystem::kAlwaysTopZ,
			FString::Printf(TEXT("F5 контроль: QUEST_WIN с признаком меты (1) — z %d в полосе AlwaysTop, подъёма нет («%s»)"), ZQ1, *WhyQ1));
		WM->SetWindowAlwaysTop(kQ, false);
		C->Expect(WM->GetWindowZOrder(kQ) < UAsdaWindowSubsystem::kAlwaysTopZ,
			FString::Printf(TEXT("F6: признак снят снова — QUEST_WIN вернулось в обычный слой z %d"), WM->GetWindowZOrder(kQ)));
		// Контрольная пара 2: ключ `asda.UI.AlwaysTopBand 0` — прежняя единая полоса: обычное окно встаёт ВЫШЕ AlwaysTop.
		SetCv(TEXT("asda.UI.AlwaysTopBand"), 0);
		WM->SetWindowZOrder(C->CoverA, 1000);
		WM->BringWindowToFront(C->CoverA);
		const int32 ZA0 = WM->GetWindowZOrder(C->CoverA);
		C->Expect(ZA0 > WM->GetWindowZOrder(kX), FString::Printf(TEXT("F7 контроль (AlwaysTopBand 0): обычное окно z %d ВЫШЕ AlwaysTop EXTRACT_WIN %d (прежний дефект)"), ZA0, WM->GetWindowZOrder(kX)));
		SetCv(TEXT("asda.UI.AlwaysTopBand"), 1);
		// Ревизия (МЕЛК-Р1): потомок владельца-AlwaysTop у верха полосы не уходит выше 8699 (в полосу всплывающих 8700+).
		WM->SetWindowZOrder(kX, UAsdaWindowSubsystem::kAlwaysTopZMax);
		WM->SetWindowOwner(kQ, kX);
		const int32 ZKid = WM->GetWindowZOrder(kQ);
		C->Expect(ZKid > WM->GetWindowZOrder(kX) - 1 && ZKid <= UAsdaWindowSubsystem::kAlwaysTopZMax,
			FString::Printf(TEXT("F8: потомок QUEST_WIN владельца-AlwaysTop EXTRACT_WIN (z %d) — z %d, не выше %d"), WM->GetWindowZOrder(kX), ZKid, UAsdaWindowSubsystem::kAlwaysTopZMax));
		WM->SetWindowOwner(kQ, NAME_None);
		// Ревизия (МЕЛК-Р2): кнопка дня гачи до начала прокрута — значение `.JUI` (0), щелчок поднимает; флаг ставит прокрут.
		const FName kDay(TEXT("Premium_PickBAR_daybtn_X_B"));
		C->Expect(!WM->IsWindowAlwaysTop(kDay), TEXT("F9: Premium_PickBAR_daybtn_X_B вне прокрута — без AlwaysTop (флаг ставит premiumGacha_BeginRoll, world_map.c:4724)"));
		WM->SetWindowZOrder(C->CoverA, 705);
		for (const FName Id : C->OpenedF) { WM->CloseWindowSilently(Id); }
		C->OpenedF.Reset();
		GBtnPass = 0;
		After(C, 0.1f, &StageBtn1, 6);
	}

	// ── G. ФОКУСИРУЕМАЯ КНОПКА ПРИ КУРСОРЕ В ЧАТЕ (ревизия СРЕДН-2) ───────────────────────────────────────────
	//   Кнопка, созданная кодом ПОСЛЕ показа окна (как `UAsdaIconCellGrid::BuildClickOverlay`, кнопки окна смерти), остаётся
	//   фокусируемой. Проход 0 — возврат фокуса строке на отпускании (клик доставлен); 1 — контроль `RaiseFocusAtPress 1`
	//   (возврат на нажатии: клик съеден); 2 — контроль `RaiseOnBodyClickAll 0` (клик доставлен, фокус строки снят).
	static void StageBtn1(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaChatWindowSubsystem* Ch = W->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>();
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), GBtnPass == 2 ? 0 : 1);
		SetCv(TEXT("asda.UI.RaiseFocusAtPress"), GBtnPass == 1 ? 1 : 0);
		UUserWidget* Cw = WM->FindWindow(C->CoverA);
		UPanelWidget* Root = Cw ? Cast<UPanelWidget>(Cw->GetRootWidget()) : nullptr;
		// Вторая покрышка на своём месте накрывает первую целиком — на время раздела G скрыта.
		if (UUserWidget* Bw = WM->FindWindow(C->CoverB)) { Bw->SetVisibility(ESlateVisibility::Collapsed); }
		FVector2D P, RMn, RMx;
		if (!Ch || !Root || !FindBodyPoint(WM, C->CoverA, P) || !AsdaUi::WidgetAbsRect(Root, RMn, RMx))
		{
			C->Expect(false, TEXT("G: нет чата/покрышки/точки тела")); After(C, 0.1f, &StageTip1, 6); return;
		}
		const float Scale = FMath::Max((float)Root->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale(), 0.01f);
		if (!C->Relay.IsValid())
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>();
			R->AddToRoot();
			TWeakPtr<FCtx> WC = C;
			R->Callback = [WC](int32) { if (TSharedPtr<FCtx> S = WC.Pin()) { ++S->BtnClicks; } };
			C->Relay = R;
		}
		UButton* B = NewObject<UButton>(Cw, UButton::StaticClass(), *FString::Printf(TEXT("RaiseTest_FocusableBtn%d"), GBtnPass));
		B->OnClicked.AddDynamic(C->Relay.Get(), &UAsdaButtonRelay::OnClicked);
		if (UCanvasPanel* Cv = Cast<UCanvasPanel>(Root))
		{
			if (UCanvasPanelSlot* S = Cv->AddChildToCanvas(B))
			{
				S->SetAnchors(FAnchors(0.f, 0.f));
				S->SetSize(FVector2D(40.f, 24.f));
				S->SetPosition((P - RMn) / Scale - FVector2D(20.f, 12.f));
				S->SetZOrder(1000);
			}
		}
		C->Btn = B;
		C->BtnClicks = 0;
		Ch->OpenInputWithText(TEXT("клик по кнопке"));
		After(C, 0.2f, &StageBtn2, 8);
	}

	static void StageBtn2(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		FVector2D Mn, Mx;
		const bool bR = C->Btn.IsValid() && AsdaUi::WidgetAbsRect(C->Btn.Get(), Mn, Mx);
		const FVector2D P = (Mn + Mx) * 0.5;
		const TSharedPtr<SWidget> BtnSw = C->Btn.IsValid() ? C->Btn->GetCachedWidget() : nullptr;
		C->Expect(bR && FocusInWindow(WM, TEXT("CHAT_NEW")) && WM->FindWindowCatchingAt(P) == C->CoverA && BtnSw.IsValid() && BtnSw->SupportsKeyboardFocus(),
			FString::Printf(TEXT("G%d: строка чата в фокусе, фокусируемая кнопка в «%s» под курсором (%.0f,%.0f)"), GBtnPass, *C->CoverA.ToString(), P.X, P.Y));
		WM->SimulateClickAt(P, 3);
		After(C, 0.15f, &StageBtn3, 14);
	}

	static void StageBtn3(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaChatWindowSubsystem* Ch = W->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>();
		const FString St = Ch->DebugInputState();
		const bool bText = St.Contains(TEXT("текст«клик по кнопке»"));
		const bool bFocus = Ch->IsInputActive() && FocusInWindow(WM, TEXT("CHAT_NEW"));
		if (GBtnPass == 0)
		{
			C->Expect(C->BtnClicks == 1 && bText && bFocus, FString::Printf(TEXT("G0: клик по фокусируемой кнопке ДОСТАВЛЕН (OnClicked ×%d), строка чата: текст %d, фокус возвращён на отпускании %d"),
				C->BtnClicks, bText ? 1 : 0, bFocus ? 1 : 0));
		}
		else if (GBtnPass == 1)
		{
			C->Expect(C->BtnClicks == 0, FString::Printf(TEXT("G1 контроль (RaiseFocusAtPress 1 — возврат на нажатии): клик СЪЕДЕН (OnClicked ×%d) — механизм ревизии подтверждён"), C->BtnClicks));
		}
		else
		{
			C->Expect(C->BtnClicks == 1 && bText && !bFocus, FString::Printf(TEXT("G2 контроль (RaiseOnBodyClickAll 0): клик доставлен (×%d), фокус строки снят (прежнее поведение), текст %d"),
				C->BtnClicks, bText ? 1 : 0));
		}
		Ch->CloseInput();
		if (C->Btn.IsValid()) { C->Btn->RemoveFromParent(); C->Btn = nullptr; }
		++GBtnPass;
		if (GBtnPass < 3) { After(C, 0.15f, &StageBtn1, 6); return; }
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), 1);
		SetCv(TEXT("asda.UI.RaiseFocusAtPress"), 0);
		if (C->Relay.IsValid()) { C->Relay->RemoveFromRoot(); C->Relay = nullptr; }
		GTipPass = 0;
		After(C, 0.1f, &StageTip1, 6);
	}

	// ── H. ГЕЙТ ПОДСКАЗКИ (ревизия МЕЛК-1/МЕЛК-2) ───────────────────────────────────────────────────────────
	//   Однострочная подсказка общей плашки (как `tooltipShowAtCursor`) показана → щелчок по телу нижнего окна его НЕ поднимает
	//   (эталон: флаги +0x1B0/+0x1B1). Контроль — `asda.UI.RaiseTooltipGate 0`: поднимает.
	static void StageTip1(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		CloseStrayModals(WM, TEXT("раздел H"));
		SetCv(TEXT("asda.UI.RaiseTooltipGate"), GTipPass == 0 ? 1 : 0);
		C->Cover = C->SatB;
		UndoCover(C);
		for (const FName Id : { C->SatA, C->SatB }) { if (UUserWidget* Wd = WM->FindWindow(Id)) { Wd->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } }
		WM->SetWindowZOrder(C->SatA, 1000);
		WM->SetWindowZOrder(C->SatB, 1001);
		After(C, 0.1f, &StageTip2, 6);
	}

	static void StageTip2(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		TArray<FVector2D> PA, PB;
		SampleHits(WM, C->SatA, 12.f, PA);
		SampleHits(WM, C->SatB, 12.f, PB);
		if (PA.Num() && PB.Num()) { MoveCover(C, WM, BoxOf(PA).GetCenter() - BoxOf(PB).Min); }
		After(C, 0.1f, &StageTip3, 6);
	}

	static void StageTip3(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaItemTooltipSubsystem* Tip = W->GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>();
		if (!ExclusivePoint(WM, C->SatA, C->SatB, C->PA) || !Tip) { C->Expect(false, TEXT("H: нет видимой части нижнего окна / подсистемы подсказок")); After(C, 0.1f, &StageAfterTip, 2); return; }
		Tip->ShowTextTip(EAsdaTipOwner::Booster, FString(), TEXT("проверка гейта подсказки"), FAsdaTipMetrics());
		After(C, 0.1f, &StageTip4, 4);
	}

	static void StageTip4(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaItemTooltipSubsystem* Tip = W->GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>();
		C->bTipWasShown = Tip->IsTooltipShown();
		C->ZBefore = WM->GetWindowZOrder(C->SatA);
		WM->SimulateClickAt(C->PA, 3);
		After(C, 0.15f, &StageTip5, 14);
	}

	static void StageTip5(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaItemTooltipSubsystem* Tip = W->GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>();
		const int32 ZA = WM->GetWindowZOrder(C->SatA), ZB = WM->GetWindowZOrder(C->SatB);
		if (GTipPass == 0)
		{
			C->Expect(C->bTipWasShown && ZA == C->ZBefore && ZA < ZB, FString::Printf(TEXT("H0: подсказка показана (%d) — щелчок по телу нижнего «%s» его НЕ поднял: z %d → %d (верхнее %d)"),
				C->bTipWasShown ? 1 : 0, *C->SatA.ToString(), C->ZBefore, ZA, ZB));
		}
		else
		{
			C->Expect(C->bTipWasShown && ZA > ZB, FString::Printf(TEXT("H1 контроль (RaiseTooltipGate 0): подсказка показана (%d) — поднято: z %d → %d (второе %d)"),
				C->bTipWasShown ? 1 : 0, C->ZBefore, ZA, ZB));
		}
		Tip->HideTooltip();
		++GTipPass;
		if (GTipPass < 2) { After(C, 0.15f, &StageTip1, 6); return; }
		After(C, 0.1f, &StageAfterTip, 2);
	}

	// Конец разделов F–H: покрышка на место, гейт подсказки — как был, итог.
	static void StageAfterTip(TSharedRef<FCtx> C)
	{
		UndoCover(C);
		SetCv(TEXT("asda.UI.RaiseTooltipGate"), C->CvTip);
		// ── I. Трей меню (ревизия МЕЛК-4): поднятый щелчком трей при показе возвращается на свой z 93 — эталон
		//   `CUIWindow_ShowHide(…, 1)` → `listMoveToEnd` (ui_core.c:79409) = вставка в НАЧАЛО списка (вниз).
		UWorld* W = C->W.Get();
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaSystemMenuSubsystem* Sm = W ? W->GetGameInstance()->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr;
		const FName kTray(TEXT("SYSTEMMAIN_NEW_WIN"));
		if (Sm && WM && WM->FindWindow(kTray))
		{
			WM->BringWindowToFront(kTray);
			const int32 ZUp = WM->GetWindowZOrder(kTray);
			Sm->ShowHide(true, /*bPlaySound*/ false);
			const int32 ZShow = WM->GetWindowZOrder(kTray);
			C->Expect(ZUp >= UAsdaWindowSubsystem::kRaiseBandFloor && ZShow == UAsdaSystemMenuSubsystem::kSysMenuZ,
				FString::Printf(TEXT("I: трей поднят щелчком на z %d → показ панели вернул его вниз на z %d (ждём %d)"), ZUp, ZShow, UAsdaSystemMenuSubsystem::kSysMenuZ));
		}
		else { C->Expect(false, TEXT("I: нет трея меню")); }
		Finish(C);
	}

	// Итог и восстановление.
	static void Finish(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = WMOf(C);
		UndoCover(C);
		if (WM)
		{
			for (const FName Id : { C->CoverA, C->CoverB }) { if (!Id.IsNone() && WM->FindWindow(Id)) { WM->CloseWindowSilently(Id); } }
		}
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): все ключи прогона (UI.RaiseOnBodyClickAll, RaiseTooltipGate, ZCompact, AlwaysTopBand,
		//   RaiseFocusAtPress) — к исходному значению И приоритету SetBy. Прежде первые три — прежним значением с SetByConsole,
		//   AlwaysTopBand и RaiseFocusAtPress — константами «умолчаний» 1/0.
		AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestRaiseClick"));
		if (UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr) { if (C->CapGen) { Pet->EndSendCapture(); } }
		for (const FString& S : C->Skipped) { UE_LOG(LogAsdaRaiseTest, Log, TEXT("ПОДЪЁМ пропуск: %s"), *S); }
		UE_LOG(LogAsdaRaiseTest, Log, TEXT("ПОДЪЁМ ИТОГ: ok=%d fail=%d skip=%d | A: поднято %d, исключений %d, без перекрытия %d | перехвачено отправок %d"),
			C->Ok, C->Fail, C->Skip, C->NRaised, C->NExcl, C->NNoOverlap, C->Sent);
		if (C->bQuit && GEngine && W) { GEngine->Exec(W, TEXT("quit")); }
	}

	static void StageCatalog(TSharedRef<FCtx> C);
	static void StageCovers(TSharedRef<FCtx> C);
	static TArray<FName> GCoverCands;      // кандидаты в покрышки (открыты, ждут геометрии)

	// Старт: ключи, перехват, свой павн (офлайн — штатный крюк, как у самотеста 1-А: HUD и чат строятся только в мире).
	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { UE_LOG(LogAsdaRaiseTest, Error, TEXT("ПОДЪЁМ: нет подсистемы окон")); return; }
		C->CvTip = GetCv(TEXT("asda.UI.RaiseTooltipGate"));
		SetCv(TEXT("asda.UI.RaiseOnBodyClickAll"), 1);
		SetCv(TEXT("asda.UI.ZCompact"), 1);
		if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>())
		{
			TWeakPtr<FCtx> WC = C;
			C->CapGen = Pet->BeginSendCapture([WC](int32, const TArray<uint8>&, bool, TCHAR) { if (TSharedPtr<FCtx> S = WC.Pin()) { ++S->Sent; } }, 0) ? 1u : 0u;
		}
		UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		if (WS && !::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}
		After(C, 2.5f, &StageCatalog, 30);
	}

	// Каталог: окна реестра (HUD, чат — как есть) + окна .JUI с WBP; дамп решения; кандидаты в покрышки.
	static void StageCatalog(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>()) { Tip->HideTooltip(); }
		// WBP по имени ассета.
		TMap<FName, FString> Wbp;
		AsdaUiWbpCatalog::Collect(Wbp);   // ★ R5-fix-b 13.11.2026 (третий заход): общая точка трёх копий; класс `WBP_<id>_C` собранной игры — под id `<id>`
		// 1) окна реестра — дамп решения и (видимые) в каталог как есть.
		TSet<FName> Seen;
		TArray<FName> Reg = WM->GetRegisteredWindowIds();
		Reg.Sort([WM](const FName& A, const FName& B) { return WM->GetWindowZOrder(A) < WM->GetWindowZOrder(B); });
		for (const FName Id : Reg)
		{
			FString Why;
			const bool b = WM->IsBodyRaiseEligible(Id, &Why);
			UE_LOG(LogAsdaRaiseTest, Log, TEXT("[подъём-список] реестр: %s z=%d вид=%d → %s | %s"),
				*Id.ToString(), WM->GetWindowZOrder(Id), IsShown(WM, Id) ? 1 : 0, b ? TEXT("ПОДНИМАЕТСЯ") : TEXT("нет"), *Why);
			if (!IsShown(WM, Id)) { continue; }   // скрытые подсистемой — проверятся из каталога .JUI, если они там есть
			FItem It; It.Id = Id; Seen.Add(Id);
			C->Items.Add(It);
		}
		// 2) окна .JUI с ассетом; кандидаты в покрышки — средние неперетаскиваемые окна, ещё не открытые.
		const UAsdaJuiWindowMetaAsset* Meta = WM->GetJuiMeta();
		TArray<TPair<double, FName>> Big;
		if (Meta)
		{
			for (const FAsdaJuiWindowMeta& M : Meta->Windows)
			{
				const FString* P = Wbp.Find(M.WindowId);
				if (!P || Seen.Contains(M.WindowId)) { continue; }
				Seen.Add(M.WindowId);
				FItem It; It.Id = M.WindowId; It.Path = *P;
				C->Items.Add(It);
				if (!M.bAlwaysTop && M.DragHandles.Num() == 0 && !WM->FindWindow(M.WindowId)
					&& M.BaseSize.X >= 250 && M.BaseSize.X <= 600 && M.BaseSize.Y >= 200 && M.BaseSize.Y <= 500
					&& !M.WindowId.ToString().Contains(TEXT("MSGBOX")))
				{
					Big.Emplace(M.BaseSize.X * M.BaseSize.Y, M.WindowId);
				}
			}
		}
		Big.Sort([](const TPair<double, FName>& A, const TPair<double, FName>& B) { return A.Key > B.Key; });
		GCoverCands.Reset();
		for (const TPair<double, FName>& B : Big)
		{
			if (GCoverCands.Num() >= 8) { break; }
			if (!WM->OpenWindowByPath(B.Value, Wbp.FindRef(B.Value), 760, ESlateVisibility::SelfHitTestInvisible, false)) { continue; }
			if (!WM->IsBodyRaiseEligible(B.Value)) { WM->CloseWindowSilently(B.Value); continue; }
			GCoverCands.Add(B.Value);
		}
		After(C, 0.2f, &StageCovers, 8);
	}

	// Покрышки: первые два кандидата, у которых на живой геометрии есть тело без кнопок.
	static void StageCovers(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		for (const FName Id : GCoverCands)
		{
			// Проверяем каждого по одному: остальные кандидаты на время проверки скрыты.
			for (const FName O : GCoverCands) { if (UUserWidget* Wd = WM->FindWindow(O)) { Wd->SetVisibility(O == Id ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); } }
			FVector2D P;
			TArray<FVector2D> Pts;
			SampleHits(WM, Id, 12.f, Pts);
			if (Pts.Num() >= 200 && FindBodyPoint(WM, Id, P))
			{
				if (C->CoverA.IsNone()) { C->CoverA = Id; } else if (C->CoverB.IsNone()) { C->CoverB = Id; }
			}
		}
		for (const FName O : GCoverCands)
		{
			if (O == C->CoverA || O == C->CoverB) { if (UUserWidget* Wd = WM->FindWindow(O)) { Wd->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } }
			else { WM->CloseWindowSilently(O); }
		}
		if (C->CoverA.IsNone() || C->CoverB.IsNone()) { C->Expect(false, TEXT("нет покрышек")); Finish(C); return; }
		UE_LOG(LogAsdaRaiseTest, Log, TEXT("ПОДЪЁМ инфо: каталог %d окон (реестр + .JUI с WBP), покрышки «%s» и «%s»"),
			C->Items.Num(), *C->CoverA.ToString(), *C->CoverB.ToString());
		if (C->Limit > 0 && C->Items.Num() > C->Limit) { C->Items.SetNum(C->Limit); }
		C->Cur = &C->Items;
		C->Idx = 0;
		GChatPass = 0; GEditPass = 0;
		After(C, 0.1f, &StageOpen);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSelfTestRaiseClick(
	TEXT("asda.UI.SelfTestRaiseClick"),
	TEXT("asda.UI.SelfTestRaiseClick [delay=N] [quit] — ОФЛАЙН-самотест подъёма окна кликом по телу (решение владельца 06.10.2026): ")
	TEXT("все окна каталога, исключения эталона, фокус полей, 300 подъёмов, контрольные пары ключом asda.UI.RaiseOnBodyClickAll 0"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.UI.SelfTestRaiseClick"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestRaiseClick"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		int32 Limit = 0;     // limit=N — только первые N окон раздела A
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			else if (Arg.StartsWith(TEXT("limit="))) { Limit = FCString::Atoi(*Arg.Mid(6)); }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit, Limit]()
		{
			if (!WW.Get()) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(WW.Get(), TEXT("asda.UI.SelfTestRaiseClick"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			TSharedRef<AsdaRaiseTestLocal::FCtx> C = MakeShared<AsdaRaiseTestLocal::FCtx>();
			C->W = WW; C->bQuit = bQuit; C->Limit = Limit;
			AsdaRaiseTestLocal::Run(C);
		};
		if (Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

// Дамп решения «поднимается ли окно кликом по телу» по всем окнам реестра (для отчёта и ручной проверки).
static FAutoConsoleCommandWithWorld GAsdaUiDumpRaiseList(
	TEXT("asda.UI.DumpRaiseList"),
	TEXT("asda.UI.DumpRaiseList — для каждого окна реестра: z, видимость, мета .JUI, AlwaysTop, ручки и решение подъёма по телу с причиной"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UAsdaWindowSubsystem* WM = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		TArray<FName> Ids = WM->GetRegisteredWindowIds();
		Ids.Sort([WM](const FName& A, const FName& B) { return WM->GetWindowZOrder(A) < WM->GetWindowZOrder(B); });
		for (const FName Id : Ids)
		{
			FString Why;
			const bool b = WM->IsBodyRaiseEligible(Id, &Why);
			UE_LOG(LogAsdaRaiseTest, Log, TEXT("[подъём-список] %-28s z=%5d вид=%d → %s | %s"),
				*Id.ToString(), WM->GetWindowZOrder(Id), WM->IsWindowVisible(Id) ? 1 : 0, b ? TEXT("ПОДНИМАЕТСЯ") : TEXT("нет"), *Why);
		}
	}));

#endif // !UE_BUILD_SHIPPING
