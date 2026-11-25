// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ALT+Z «СКРЫТЬ ИНТЕРФЕЙС» (21.10.2026): `asda.SelfTest.HideUi [quit]`.
//   Офлайн (`-AsdaNoNet`), на карте мира, путём игрока (законы diagnostic-must-use-real-path, ui-check-game-path-geometry):
//   нажатие Alt+Z подаётся НАСТОЯЩИМ событием клавиши в `FSlateApplication::ProcessKeyDownEvent` (препроцессор ввода → владелец
//   флага), видимость окон проверяется по живому Slate-дереву (обёртка вьюпорта), хит-тест — в центре окна по его
//   `GetCachedGeometry` (`IsScreenPointOverAnyUiWindow` — тот же код, которым `IsCursorOverAnyUiWindow` решает клик «в мир или в окно»;
//   при -RenderOffscreen курсор ОС не переносится, поэтому точку подаём сами).
//   Проверяется:
//     A) Alt+Z скрывает окна реестра и показывает их повторным Alt+Z; автоповтор не переключает [ИСПРАВЛЕНИЕ 1];
//        Z без Alt и Ctrl+Alt+Z не переключают;
//     B) скрытое окно не ловит мышь: хит-тест в центре окна, которое до скрытия было «над окном», — «не над окном» [ИСПРАВЛЕНИЕ 4];
//     C) окно, открытое при скрытом интерфейсе, сразу скрыто; посторонний виджет вьюпорта под `TrackFor` — тоже;
//     D) гейты эталона: модальное окно — Alt+Z отклонён; модалка при скрытом интерфейсе показывает его [ИСПРАВЛЕНИЕ 2];
//     E) значок 5274 при скрытом интерфейсе не рисуется, но тикает;
//     F) полный сброс сессии показывает интерфейс [ИСПРАВЛЕНИЕ 5], смена карты — нет;
//     G) гейт «мёртв»; откат исправлений `asda.UI.HideFixes 0` — автоповтор мигает, сброс 1:1, разрыв связи всё равно показывает;
//     K) курсор мыши (01.11.2026, [ИСПРАВЛЕНИЕ] владельца): Alt+Z прячет курсор вьюпорта (`GetMouseCursor` = None — его UE отдаёт
//        Slate), вид курсора игры (Pick камня) и режим ввода не тронуты; новый контроллер после смены карты прячется тиком;
//        повторный Alt+Z, модалка, разрыв связи возвращают курсор прежнего вида; ключ `asda.UI.HideCursor 0` — курсор виден.
//   B дополнительно: виджет спрятанного окна «в скрытом UI», путь фокуса цел, настоящая ЛКМ через Slate — кадр мыши окон
//   слеп, вывеска лавки (`bIgnoreUiHidden`) видит.
//   Итог: «СКРЫТЬ-ИНТЕРФЕЙС ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaBytes.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaCooldownReadyIconSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHideUiTest, Log, All);

namespace AsdaHideUiTest
{
	// Подать Alt+Z (или иное сочетание) в Slate так же, как его подаёт ОС: через общий вход событий клавиш.
	static bool PressKey(FKey Key, bool bAlt, bool bCtrl, bool bRepeat)
	{
		if (!FSlateApplication::IsInitialized()) { return false; }
		const FModifierKeysState Mods(false, false, bCtrl, false, bAlt, false, false, false, false);
		const FKeyEvent Down(Key, Mods, 0, bRepeat, 0, 0);
		const bool bHandled = FSlateApplication::Get().ProcessKeyDownEvent(Down);
		const FKeyEvent Up(Key, Mods, 0, false, 0, 0);
		FSlateApplication::Get().ProcessKeyUpEvent(Up);
		return bHandled;
	}

	// Точка на живом элементе окна: корень окна растянут на весь экран, поэтому берётся центр первого видимого листа дерева
	//   (не панели) с ненулевой геометрией (`GetCachedGeometry`, абсолютные пиксели Slate), который хит-тест реестра считает окном.
	static bool PointOnWindow(UAsdaWindowSubsystem* WM, UUserWidget* W, FVector2D& OutPt)
	{
		bool bFound = false;
		if (!W || !W->WidgetTree) { return false; }
		W->WidgetTree->ForEachWidget([&](UWidget* C)
		{
			if (bFound || !C || C->IsA<UPanelWidget>() || !C->IsVisible()) { return; }
			const FGeometry G = C->GetCachedGeometry();
			const FVector2D Sz = G.GetLocalSize() * G.Scale;
			if (Sz.X < 8.f || Sz.Y < 8.f) { return; }
			const FVector2D Pt = G.GetAbsolutePosition() + Sz * 0.5f;
			FName Zone;
			if (WM->IsScreenPointOverAnyUiWindow(Pt, Zone)) { OutPt = Pt; bFound = true; }
		});
		return bFound;
	}

	// Видимое окно реестра с точкой «над окном» по хит-тесту реестра (им решается клик «в мир или в окно»).
	static UUserWidget* PickVisibleWindow(UAsdaWindowSubsystem* WM, FName& OutId, FVector2D& OutPt)
	{
		for (const FName& Id : WM->GetRegisteredWindowIds())
		{
			UUserWidget* W = WM->FindWindow(Id);
			if (!W || !WM->IsWindowVisible(Id) || !W->IsInViewport()) { continue; }
			if (PointOnWindow(WM, W, OutPt)) { OutId = Id; return W; }
		}
		return nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestHideUi(
	TEXT("asda.SelfTest.HideUi"),
	TEXT("Самотест Alt+Z «скрыть интерфейс»: событие клавиши в Slate → окна реестра скрыты/показаны, хит-тест по геометрии, "
	     "автоповтор, гейты, модалка, значок 5274, сброс. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaHideUiTest;
		static const TCHAR* kCmd = TEXT("asda.SelfTest.HideUi");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен гейту «мир» — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaUiVisibilitySubsystem* Vis = GI ? GI->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaCooldownReadyIconSubsystem* Cd = GI ? GI->GetSubsystem<UAsdaCooldownReadyIconSubsystem>() : nullptr;
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!Vis || !WM || !Cd || !PC || !FSlateApplication::IsInitialized())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем / контроллера / Slate"));
			return;
		}
		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaHideUiTest, Log,   TEXT("СКРЫТЬ-ИНТЕРФЕЙС ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaHideUiTest, Error, TEXT("СКРЫТЬ-ИНТЕРФЕЙС FAIL %s"), *What); }
		};
		Vis->SetUiShown(true, TEXT("самотест: старт"));

		FName WinId;
		FVector2D WinPt = FVector2D::ZeroVector;
		UUserWidget* Win = PickVisibleWindow(WM, WinId, WinPt);
		Check(Win != nullptr && Vis->GetToggleBlockReason().IsEmpty(),
			FString::Printf(TEXT("0 подготовка: окно «%s», точка %s «над окном»; Alt+Z разрешён (%s)"), *WinId.ToString(), *WinPt.ToString(), *Vis->GetToggleBlockReason()));

		// ═══ A. Нажатия ═══
		const bool bEaten = PressKey(EKeys::Z, /*Alt*/true, false, false);
		Check(bEaten && !Vis->IsUiShown(), TEXT("A1 Alt+Z (событие клавиши Slate) — интерфейс скрыт, нажатие съедено"));
		Check(Win && !UAsdaUiVisibilitySubsystem::IsViewportHostVisible(Win) && WM->IsWindowVisible(WinId),
			TEXT("A2 окно реестра: обёртка вьюпорта свёрнута, собственная видимость окна не тронута"));
		PressKey(EKeys::Z, true, false, /*repeat*/true);
		Check(!Vis->IsUiShown(), TEXT("A3 автоповтор Alt+Z не переключает [ИСПРАВЛЕНИЕ мигания]"));
		PressKey(EKeys::Z, false, false, false);
		Check(!Vis->IsUiShown(), TEXT("A4 Z без Alt — не переключает"));
		PressKey(EKeys::Z, true, /*Ctrl*/true, false);
		Check(!Vis->IsUiShown(), TEXT("A5 Ctrl+Alt+Z — не переключает (у оригинала это WM_KEYDOWN)"));

		// ═══ B. Хит-тест по живой геометрии ═══
		if (Win)
		{
			FName Zone;
			Check(!WM->IsScreenPointOverAnyUiWindow(WinPt, Zone), TEXT("B1 скрыто: та же точка окна — «не над окном», клик идёт в мир [ИСПРАВЛЕНИЕ]"));
			// Опрос мыши окон по геометрии: виджет спрятанного окна — «в скрытом UI», общий кадр мыши слеп к настоящему нажатию.
			UWidget* Leaf = nullptr;
			if (Win->WidgetTree) { Win->WidgetTree->ForEachWidget([&Leaf](UWidget* C) { if (!Leaf && C && !C->IsA<UPanelWidget>()) { Leaf = C; } }); }
			Check(Leaf && AsdaUi::IsWidgetInHiddenUi(Leaf) && !AsdaUi::IsCursorOverWidget(Leaf),
				TEXT("B1a виджет спрятанного окна: «в скрытом UI», IsCursorOverWidget — нет [ИСПРАВЛЕНИЕ: невидимые окна ловили мышь]"));
			Check(UAsdaUiVisibilitySubsystem::IsViewportHostFocusable(Win), TEXT("B1b путь фокуса не рвётся (набор в чате при скрытом интерфейсе работает, как у оригинала)"));
			{
				// Настоящее нажатие ЛКМ через общий вход Slate (как от ОС) — общий кадр мыши окон его не видит, вывеска лавки — видит.
				const FVector2D Pt = FSlateApplication::Get().GetCursorPos();
				FPointerEvent Down(0, Pt, Pt, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
				FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, Down);
				FAsdaMouseFrame UiMouse; UiMouse.Update();
				FAsdaMouseFrame SignMouse; SignMouse.bIgnoreUiHidden = true; SignMouse.Update();
				const bool bSlateSees = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
				FPointerEvent Up(0, Pt, Pt, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
				FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
				Check(bSlateSees && !UiMouse.bLmb && !UiMouse.bLmbDown && SignMouse.bLmb && SignMouse.bLmbDown,
					FString::Printf(TEXT("B1c ЛКМ нажата (Slate %s): кадр мыши окон — не видит, вывески лавки — видит"), bSlateSees ? TEXT("да") : TEXT("нет")));
			}
			PressKey(EKeys::Z, true, false, false);
			Check(Vis->IsUiShown() && UAsdaUiVisibilitySubsystem::IsViewportHostVisible(Win), TEXT("B2 повторный Alt+Z — интерфейс и обёртка окна показаны"));
		}
		else { PressKey(EKeys::Z, true, false, false); }

		// ═══ C. Открытие при скрытом и посторонний виджет ═══
		{
			PressKey(EKeys::Z, true, false, false);
			const FName TestId(TEXT("ASDA_SELFTEST_HIDEUI_WIN"));
			// Класс — реальное окно игры (у абстрактного `UUserWidget` экземпляра нет): берём класс выбранного окна.
			const TSubclassOf<UUserWidget> Cls = Win ? Win->GetClass() : nullptr;
			UUserWidget* NewWin = Cls ? WM->OpenWindow(TestId, Cls, 5, ESlateVisibility::SelfHitTestInvisible, false) : nullptr;
			Check(NewWin && !UAsdaUiVisibilitySubsystem::IsViewportHostVisible(NewWin), TEXT("C1 окно, открытое при скрытом интерфейсе, сразу скрыто"));
			UUserWidget* Extra = Cls ? CreateWidget<UUserWidget>(PC, Cls) : nullptr;
			if (Extra) { Extra->AddToViewport(50); UAsdaUiVisibilitySubsystem::TrackFor(GI, Extra); }
			Check(Extra && !UAsdaUiVisibilitySubsystem::IsViewportHostVisible(Extra), TEXT("C2 виджет вьюпорта под TrackFor (баннер) — скрыт"));
			PressKey(EKeys::Z, true, false, false);
			Check(NewWin && UAsdaUiVisibilitySubsystem::IsViewportHostVisible(NewWin) && Extra && UAsdaUiVisibilitySubsystem::IsViewportHostVisible(Extra),
				TEXT("C3 Alt+Z — оба показаны"));
			WM->CloseWindow(TestId);
			if (Extra) { Extra->RemoveFromParent(); }
		}

		// ═══ D. Модальное окно ═══
		{
			WM->ShowGameConfirm(TEXT("самотест Alt+Z"), [](bool) {});
			Check(WM->IsAnyModalOpen() && !Vis->GetToggleBlockReason().IsEmpty(), FString::Printf(TEXT("D1 модальное окно — Alt+Z отклонён (%s)"), *Vis->GetToggleBlockReason()));
			PressKey(EKeys::Z, true, false, false);
			Check(Vis->IsUiShown(), TEXT("D2 при модалке Alt+Z не скрывает"));
			WM->AnswerMsgBox(true, false);
			PressKey(EKeys::Z, true, false, false);
			Check(!Vis->IsUiShown(), TEXT("D3 подготовка: скрыто"));
			WM->ShowGameConfirm(TEXT("самотест Alt+Z — модалка при скрытом"), [](bool) {});
			Vis->Tick(0.f);
			Check(Vis->IsUiShown(), TEXT("D4 модалка при скрытом интерфейсе показывает его [ИСПРАВЛЕНИЕ невидимой модалки]"));
			WM->AnswerMsgBox(true, false);
		}

		// ═══ E. Значок 5274 ═══
		{
			Cd->ResetSessionState(EAsdaSessionResetReason::Disconnected);
			int32 IconSkill = -1;
			if (const UAsdaSkillDatabase* Db = GI->GetSubsystem<UAsdaSkillDatabase>())
			{
				for (int32 Id = 100; Id < 5000 && IconSkill < 0; ++Id) { const FAsdaSkillDef* D = Db->Get(Id); if (D && !D->IconName.IsEmpty()) { IconSkill = Id; } }
			}
			Cd->AddIcon(IconSkill);
			PressKey(EKeys::Z, true, false, false);
			float S0 = 0.f, A = 0.f; int32 Ph = 0, Sid = 0;
			Cd->DebugStep(1.f / 60.f); Cd->DebugEntry(0, S0, A, Ph, Sid);
			Cd->DebugStep(0.1f);
			float S1 = 0.f; Cd->DebugEntry(0, S1, A, Ph, Sid);
			Check(!Vis->IsUiShown() && !Cd->DebugDrawAllowed() && !Cd->DebugImageVisible(0) && S1 < S0,
				FString::Printf(TEXT("E1 скрыто: значок 5274 не рисуется, но тикает (%.0f → %.0f)"), S0, S1));
			PressKey(EKeys::Z, true, false, false);
			Cd->DebugStep(1.f / 60.f);
			Check(Vis->IsUiShown() && Cd->DebugImageVisible(0) == Cd->DebugDrawAllowed(), TEXT("E2 показано: значок рисуется по своему гейту (мини-карта)"));
			Cd->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		}

		// ═══ G. Гейт «мёртв» и режим отката исправлений ═══
		if (UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI))
		{
			const bool bDeadWas = AsdaTestNet::bSelfDead(Net);
			AsdaTestNet::bSelfDead(Net) = true;
			const FString Why = Vis->GetToggleBlockReason();
			PressKey(EKeys::Z, true, false, false);
			Check(!Why.IsEmpty() && Vis->IsUiShown(), FString::Printf(TEXT("G1 персонаж мёртв — Alt+Z отклонён (%s)"), *Why));
			AsdaTestNet::bSelfDead(Net) = bDeadWas;
		}
		{
			FAsdaCVarScope Keys(W, kCmd);
			Keys.Set(TEXT("asda.UI.HideFixes"), 0);
			PressKey(EKeys::Z, true, false, false);
			PressKey(EKeys::Z, true, false, /*repeat*/true);
			Check(Vis->IsUiShown(), TEXT("G2 откат исправлений (HideFixes 0): автоповтор переключает, как у оригинала (скрыть → показать)"));
			PressKey(EKeys::Z, true, false, false);
			Vis->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			Check(!Vis->IsUiShown(), TEXT("G3 откат: выход в выбор персонажа скрытие не снимает (1:1)"));
			Vis->ResetSessionState(EAsdaSessionResetReason::Disconnected);
			Check(Vis->IsUiShown(), TEXT("G4 разрыв связи показывает интерфейс всегда — и при откате (экран входа порта не должен остаться невидимым)"));
		}

		// ═══ K. Курсор мыши вместе с интерфейсом ═══
		if (AAsdaPlayerController* APC = Cast<AAsdaPlayerController>(PC))
		{
			Vis->SetUiShown(true, TEXT("самотест: курсор"));
			const EMouseCursor::Type CurWas = PC->GetMouseCursor();
			APC->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Pick);   // взведённый камень переноса (курсор P0)
			const bool bInputWas = PC->bShowMouseCursor;
			PressKey(EKeys::Z, true, false, false);
			Check(CurWas != EMouseCursor::None && !Vis->IsUiShown() && Vis->IsCursorHidden() && PC->GetMouseCursor() == EMouseCursor::None,
				FString::Printf(TEXT("K1 Alt+Z — курсор вьюпорта скрыт (GetMouseCursor %d → %d)"), (int32)CurWas, (int32)PC->GetMouseCursor()));
			Check(APC->CurrentCursor == AAsdaPlayerController::EAsdaCursor::Pick && PC->bShowMouseCursor == bInputWas,
				TEXT("K2 скрыт только показ: вид курсора игры (Pick) и режим ввода мыши не тронуты"));
			// Смена карты: новый контроллер (`InitInputSystem`) приходит с `DefaultMouseCursor` — тик скрытого режима прячет его.
			PC->CurrentMouseCursor = PC->DefaultMouseCursor;
			Vis->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Vis->Tick(0.f);
			Check(!Vis->IsUiShown() && PC->GetMouseCursor() == EMouseCursor::None, TEXT("K3 смена карты при скрытом интерфейсе — курсор остаётся скрытым"));
			PressKey(EKeys::Z, true, false, false);
			Check(Vis->IsUiShown() && !Vis->IsCursorHidden() && PC->GetMouseCursor() == CurWas && APC->CurrentCursor == AAsdaPlayerController::EAsdaCursor::Pick,
				FString::Printf(TEXT("K4 повторный Alt+Z — курсор вернулся (%d) прежнего вида (Pick, не сброшен в обычный)"), (int32)PC->GetMouseCursor()));
			PressKey(EKeys::Z, true, false, false);
			WM->ShowGameConfirm(TEXT("самотест Alt+Z — курсор и модалка"), [](bool) {});
			Vis->Tick(0.f);
			Check(Vis->IsUiShown() && PC->GetMouseCursor() == CurWas, TEXT("K5 модалка при скрытом интерфейсе возвращает интерфейс и курсор"));
			WM->AnswerMsgBox(true, false);
			PressKey(EKeys::Z, true, false, false);
			Vis->ResetSessionState(EAsdaSessionResetReason::Disconnected);
			Check(Vis->IsUiShown() && PC->GetMouseCursor() == CurWas, TEXT("K6 разрыв связи при скрытом интерфейсе — курсор возвращён"));
			{
				FAsdaCVarScope Keys(W, kCmd);
				Keys.Set(TEXT("asda.UI.HideCursor"), 0);
				PressKey(EKeys::Z, true, false, false);
				Check(!Vis->IsUiShown() && !Vis->IsCursorHidden() && PC->GetMouseCursor() == CurWas, TEXT("K7 ключ HideCursor 0 — интерфейс скрыт, курсор виден (прежнее)"));
				Keys.Set(TEXT("asda.UI.HideCursor"), 1);
				Vis->Tick(0.f);
				Check(PC->GetMouseCursor() == EMouseCursor::None, TEXT("K8 ключ включён при скрытом интерфейсе — курсор скрыт тиком"));
				Keys.Set(TEXT("asda.UI.HideCursor"), 0);
				Vis->Tick(0.f);
				Check(PC->GetMouseCursor() == CurWas, TEXT("K9 ключ выключен при скрытом интерфейсе — курсор возвращён тиком"));
				PressKey(EKeys::Z, true, false, false);
			}
			APC->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Normal);
			Vis->SetUiShown(true, TEXT("самотест: курсор — конец"));
		}
		else { Check(false, TEXT("K0 нет AAsdaPlayerController")); }

		// ═══ F. Сброс ═══
		PressKey(EKeys::Z, true, false, false);
		Vis->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Check(!Vis->IsUiShown(), TEXT("F1 смена карты скрытие не снимает (как оригинал)"));
		Vis->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Check(Vis->IsUiShown() && (!Win || UAsdaUiVisibilitySubsystem::IsViewportHostVisible(Win)), TEXT("F2 выход в выбор персонажа показывает интерфейс [ИСПРАВЛЕНИЕ]"));

		Vis->SetUiShown(true, TEXT("самотест: конец"));
		AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaHideUiTest, TEXT("СКРЫТЬ-ИНТЕРФЕЙС"), Ok, Fail);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
