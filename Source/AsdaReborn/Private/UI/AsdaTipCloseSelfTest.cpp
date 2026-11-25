// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПОДСКАЗКА ГАСНЕТ ВМЕСТЕ С ОКНОМ» (31.10.2026): `asda.SelfTest.TipClose [quit]`. [ПОРТ]
//   Баг владельца: навёл на самоцвет в оружии («Живой камень GM 1»), закрыл инвентарь КЛАВИШЕЙ — подсказка зависла посреди
//   экрана. Эталон гасит плашку, когда показавшее её окно уходит с экрана (`sowelWindowClose@0xCFF760` → `CControl_KillFocus`,
//   у прочих окон — событие 0xA). Лечение — общая точка `UAsdaItemTooltipSubsystem::Tick` (ключ `asda.UI.TipFollowsWindow`)
//   плюс гашение в опросе гнёзд инфо-панели. Путь игрока (закон `ui-check-game-path-geometry`): живая геометрия, настоящие
//   события мыши Slate, клавиши — тем же вводом, что у игрока (I — ввод контроллера, ESC и Alt+Z — события клавиш Slate),
//   офлайн (-AsdaNoNet). Разделы:
//     S. гнездо самоцвета инфо-панели (оружие с камнем, выделено щелчком) → клавиша I → подсказки нет;
//     I. ячейка сумки → клавиша I → подсказки нет; ключ `asda.UI.TipFollowsWindow 0` — что было прежде (печатается);
//     E. ячейка сумки → ESC → подсказки нет;
//     M. кнопка 1704 мини-карты → щелчок «закрыть» (курсор не уходит) → подсказки нет, мини-карта возвращается;
//     Z. ячейка сумки → Alt+Z → подсказки нет, Alt+Z возвращает интерфейс;
//     B. окно «Вид» бустера 15600, открытое как из магазина «Усилители» (барабан закрыт): наведение → подсказка предмета,
//        закрытие окна → подсказки нет (второй баг владельца: из магазина подсказок не было вовсе);
//     C. ячейка сумки → снос окон, как при смене карты (`CloseAll`) → подсказки нет.
//   ⚠ Раздел C делает НАСТОЯЩИЙ сброс сессии «смена карты» — в пакете `asda_selftest_batch.py` ставить этот самотест ПОСЛЕДНИМ.
//   Сумка, ключи и интерфейс возвращаются в конце. Итог: «ПОДСКАЗКА-ОКНО ИТОГ: ok=N fail=M». Карта мира (`--map world`).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTipCloseTest, Log, All);

namespace AsdaTipCloseTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.TipClose");   // имя команды
	static const TCHAR* kPfx = TEXT("ПОДСКАЗКА-ОКНО");           // префикс строк и итога

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		TArray<TArray<uint8>> SavedBag;            // сумка до прогона (контейнер 1, ячейки 0…1)
		int32 Weapon = 0;                          // оружие для ячейки 0 (с камнем в гнезде 0)
		int32 Sowel = 0;                           // код самоцвета
		int32 Booster = 0;                         // бустер с составом (окно «Вид» 15600)
		FVector2D WorldPt = FVector2D::ZeroVector; // точка мира без интерфейса

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaTipCloseTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaTipCloseTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живого прямоугольника узла (false — не отрисован / скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Курсор в точку + событие движения (наведение без щелчка).
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Клавиша игровой раскладки — штатным вводом контроллера (так её получает диспетчер клавиш порта).
	static void GameKey(const FCtxRef& C, const FKey& K, bool bDown)
	{
		APlayerController* PC = C->W.IsValid() ? C->W->GetFirstPlayerController() : nullptr;
		if (!PC) { return; }
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	}
	// Клавиша через вход событий Slate (как её подаёт ОС): ESC, Alt+Z.
	static void SlateKey(const FKey& K, bool bAlt, bool bDown)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		const FModifierKeysState Mods(false, false, false, false, bAlt, false, false, false, false);
		if (bDown) { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K, Mods, 0, false, 0, 0)); }
		else       { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K, Mods, 0, false, 0, 0)); }
	}
	// Плашка показана владельцем `O`.
	static bool TipOf(const FCtxRef& C, EAsdaTipOwner O)
	{
		const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		return T && T->IsTooltipShown() && T->GetTipOwner() == O;
	}
	// Плашки нет на экране (флаг и сама плашка).
	static bool TipGone(const FCtxRef& C)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		UAsdaTipPanel* P = T ? T->GetMainPanel() : nullptr;
		const UUserWidget* Wd = P ? P->GetWidget() : nullptr;
		return T && !T->IsTooltipShown() && (!Wd || !Wd->IsVisible());
	}
	// Состояние плашки строкой.
	static FString TipState(const FCtxRef& C)
	{
		const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		return T ? FString::Printf(TEXT("показана=%d владелец=%d"), T->IsTooltipShown() ? 1 : 0, (int32)T->GetTipOwner()) : TEXT("нет подсистемы");
	}
	// Узел окна инвентаря по началу имени — план иконки гнезда `INVEN_SOWELL0_<idx>_0`.
	static UWidget* InvNode(const FCtxRef& C, const FString& Pfx)
	{
		const UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		UUserWidget* Wd = I ? I->GetWindowWidget() : nullptr;
		if (!Wd || !Wd->WidgetTree) { return nullptr; }
		TArray<UWidget*> All;
		Wd->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All) { if (X && X->GetName().StartsWith(Pfx) && X->GetName().EndsWith(TEXT("_0"))) { return X; } }
		return nullptr;
	}

	static void Finish(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>())
		{
			for (int32 S = 0; S < C->SavedBag.Num(); ++S)
			{
				I->ApplyExternalRecord(C->SavedBag[S].Num() == FAsdaItemRecordWriter::kSize ? C->SavedBag[S] : FAsdaItemRecordWriter::ClearCell(1, S),
					UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
		}
		if (UAsdaUiVisibilitySubsystem* V = C->Sub<UAsdaUiVisibilitySubsystem>()) { V->SetUiShown(true, kCmd); }
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaTipCloseTest, kPfx, C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (!Inv || !IT || !WM) { C->Expect(false, TEXT("подсистемы инвентаря/таблиц/окон есть")); Finish(C); return; }
		// Оружие (тип 9) и самоцвет из таблиц игры.
		for (int32 Code = 20000; Code < 80000 && !C->Weapon; ++Code) { const FAsdaItemRecord* R = IT->GetItem(Code); if (R && R->ItemType == 9) { C->Weapon = Code; } }
		for (int32 Code = 1; Code < 80000 && !C->Sowel; ++Code) { if (IT->IsSowelCode(Code) && !IT->GetItem(Code)) { C->Sowel = Code; } }
		for (int32 Code = 1; Code < 80000 && !C->Sowel; ++Code) { if (IT->IsSowelCode(Code)) { C->Sowel = Code; } }
		C->Expect(C->Weapon > 0 && C->Sowel > 0, FString::Printf(TEXT("данные: оружие %d, самоцвет %d"), C->Weapon, C->Sowel));
		// Сумка 0…1 до прогона.
		for (int32 S = 0; S < 2; ++S)
		{
			const FAsdaItemInstance* It = Inv->GetCell(1, S);
			C->SavedBag.Add((It && !It->IsEmpty()) ? It->Raw : TArray<uint8>());
		}
		// Ячейка 0 — оружие с камнем в гнезде 0, ячейка 1 — то же оружие без камня.
		Inv->ApplyExternalRecord(FAsdaItemRecordWriter().Code(C->Weapon).Container(1).Slot(0).Amount(1).Durability(100).Sowel(0, C->Sowel).ToArray(),
			UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(C->Weapon, 1, 1, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		// Точка мира без интерфейса.
		FGeometry G;
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->GetGameViewportWidget().IsValid())
		{
			G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
			for (const FVector2D F : { FVector2D(0.5f, 0.22f), FVector2D(0.3f, 0.3f), FVector2D(0.7f, 0.3f), FVector2D(0.5f, 0.4f) })
			{
				const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * F);
				FName Zone;
				if (!WM->IsScreenPointOverAnyUiWindow(P, Zone)) { C->WorldPt = P; break; }
			}
		}
		Inv->OpenWindow();
		Inv->SetTab(1);   // вкладка «Снаряжение» — сетка контейнера 1 (сумка), где лежит оружие (то же, что щелчок по вкладке)

		// Типовой цикл: открыть окно → (кадры) → навести на ячейку сумки → (кадры) → проверить подсказку → нажать клавишу
		//   → (кадр) → отпустить → (кадры) → проверить, что подсказки нет. Нажатие и отпускание — на РАЗНЫХ кадрах, как у игрока
		//   (диспетчер клавиш порта ловит фронт по кадрам, ср. `asda.SelfTest.EscChain`).
		// Наведение «мир → ячейка 1» на РАЗНЫХ кадрах: опрос инвентаря видит смену ячейки только между кадрами.
		auto HoverBag = [](const FCtxRef& Cx)
		{
			Hover(Cx->WorldPt);
			At(Cx, 0.1f, [Cx]()
			{
				UAsdaInventorySubsystem* I = Cx->Sub<UAsdaInventorySubsystem>();
				FVector2D P;
				if (I && Center(I->DebugGetGridIcon(1), P)) { Hover(P); }
			});
		};
		auto OpenInv = [](const FCtxRef& Cx) { if (UAsdaInventorySubsystem* I = Cx->Sub<UAsdaInventorySubsystem>()) { if (!I->IsWindowOpen()) { I->OpenWindow(); } I->SetTab(1); } };

		// ═══ S. Гнездо самоцвета инфо-панели → клавиша I ═══
		At(C, 0.8f, [C]()
		{
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P;
			if (I && Center(I->DebugGetGridIcon(0), P)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, /*HoldFrames*/ 2); }   // выделить оружие
		});
		At(C, 1.4f, [C]()
		{
			FVector2D P;
			const UWidget* Node = InvNode(C, TEXT("INVEN_SOWELL0_"));
			const UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			const bool bGeo = Center(Node, P);
			C->Expect(bGeo, FString::Printf(TEXT("S0 гнездо самоцвета 0 инфо-панели на экране (%.0f,%.0f; узел %s, показан %d, панель раскрыта %d)"),
				P.X, P.Y, Node ? *Node->GetName() : TEXT("НЕТ"), Node && AsdaUi::IsShown(Node) ? 1 : 0, I && I->IsPanelExpanded() ? 1 : 0));
			if (bGeo) { Hover(C->WorldPt); Hover(P); }
		});
		At(C, 1.8f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::InfoSocket), FString::Printf(TEXT("S1 наведение на гнездо → подсказка самоцвета (%s)"), *TipState(C)));
			GameKey(C, EKeys::I, true);   // закрыть инвентарь клавишей, курсор не уходит
		});
		At(C, 1.9f, [C]() { GameKey(C, EKeys::I, false); });
		At(C, 2.3f, [C]()
		{
			const UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			C->Expect(I && !I->IsWindowOpen() && TipGone(C),
				FString::Printf(TEXT("S2 клавиша I закрыла инвентарь (открыт=%d) — подсказки самоцвета нет (%s)"), I && I->IsWindowOpen() ? 1 : 0, *TipState(C)));
		});

		// ═══ I. Ячейка сумки → клавиша I; ключ 0 — прежнее поведение (печать) ═══
		At(C, 2.6f, [C, OpenInv]() { OpenInv(C); });
		At(C, 2.9f, [C, HoverBag]() { HoverBag(C); });
		At(C, 3.3f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Item), FString::Printf(TEXT("I0 наведение на ячейку сумки → подсказка предмета (%s)"), *TipState(C)));
			GameKey(C, EKeys::I, true);
		});
		At(C, 3.4f, [C]() { GameKey(C, EKeys::I, false); });
		At(C, 3.8f, [C, OpenInv]()
		{
			const UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			C->Expect(I && !I->IsWindowOpen() && TipGone(C),
				FString::Printf(TEXT("I1 клавиша I закрыла инвентарь (открыт=%d) — подсказки предмета нет (%s)"), I && I->IsWindowOpen() ? 1 : 0, *TipState(C)));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.TipFollowsWindow"), 0);
			OpenInv(C);
		});
		At(C, 4.1f, [C, HoverBag]() { HoverBag(C); });
		At(C, 4.5f, [C]() { GameKey(C, EKeys::I, true); });
		At(C, 4.6f, [C]() { GameKey(C, EKeys::I, false); });
		At(C, 5.0f, [C]()
		{
			UE_LOG(LogAsdaTipCloseTest, Log, TEXT("%s инфо  I2 ключ asda.UI.TipFollowsWindow 0 (прежний порт): после клавиши I подсказка %s (%s)"),
				kPfx, TipGone(C) ? TEXT("погасла (её гасит сам владелец)") : TEXT("ОСТАЛАСЬ на экране"), *TipState(C));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.TipFollowsWindow"), 1);
			if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		});

		// ═══ E. Ячейка сумки → ESC ═══
		At(C, 5.3f, [C, OpenInv]() { OpenInv(C); });
		At(C, 5.6f, [C, HoverBag]() { HoverBag(C); });
		At(C, 6.0f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Item), FString::Printf(TEXT("E0 подсказка предмета (%s)"), *TipState(C)));
			SlateKey(EKeys::Escape, false, true);
		});
		At(C, 6.1f, [C]() { SlateKey(EKeys::Escape, false, false); });
		At(C, 6.5f, [C]()
		{
			const UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			C->Expect(I && !I->IsWindowOpen() && TipGone(C),
				FString::Printf(TEXT("E1 ESC закрыл инвентарь (открыт=%d) — подсказки нет (%s)"), I && I->IsWindowOpen() ? 1 : 0, *TipState(C)));
		});

		// ═══ M. Кнопка 1704 мини-карты → щелчок «закрыть» (курсор на месте) ═══
		At(C, 6.8f, [C]()
		{
			FVector2D P;
			UWidget* Btn = nullptr;
			for (TObjectIterator<UWidget> It; It; ++It)
			{
				if (It->GetFName() == FName(TEXT("MINIMAP_CLOSE")) && It->GetWorld() == C->W.Get() && It->GetCachedWidget().IsValid()) { Btn = *It; break; }
			}
			const bool bGeo = Center(Btn, P);
			C->Expect(bGeo, FString::Printf(TEXT("M0 кнопка 1704 мини-карты на экране (%.0f,%.0f)"), P.X, P.Y));
			if (bGeo) { Hover(C->WorldPt); Hover(P); }
		});
		At(C, 7.2f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Minimap), FString::Printf(TEXT("M1 справка кнопки 1704 (%s)"), *TipState(C)));
			const FVector2D Cur = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
			C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(Cur, /*HoldFrames*/ 2);
		});
		At(C, 7.8f, [C]()
		{
			C->Expect(TipGone(C), FString::Printf(TEXT("M2 мини-карта свёрнута щелчком 1704 — справки нет (%s)"), *TipState(C)));
			if (UAsdaMinimapSubsystem* M = C->Sub<UAsdaMinimapSubsystem>()) { M->SetMinimapVisible(true); }
		});

		// ═══ Z. Ячейка сумки → Alt+Z ═══
		At(C, 8.1f, [C, OpenInv]() { OpenInv(C); });
		At(C, 8.4f, [C, HoverBag]() { HoverBag(C); });
		At(C, 8.8f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Item), FString::Printf(TEXT("Z0 подсказка предмета (%s)"), *TipState(C)));
			SlateKey(EKeys::Z, true, true);
		});
		At(C, 8.9f, [C]() { SlateKey(EKeys::Z, true, false); });
		At(C, 9.3f, [C]()
		{
			const UAsdaUiVisibilitySubsystem* V = C->Sub<UAsdaUiVisibilitySubsystem>();
			C->Expect(V && !V->IsUiShown() && TipGone(C), FString::Printf(TEXT("Z1 Alt+Z спрятал интерфейс (показан=%d) — подсказки нет (%s)"),
				V && V->IsUiShown() ? 1 : 0, *TipState(C)));
			SlateKey(EKeys::Z, true, true);
		});
		At(C, 9.4f, [C]() { SlateKey(EKeys::Z, true, false); });
		At(C, 9.8f, [C]()
		{
			const UAsdaUiVisibilitySubsystem* V = C->Sub<UAsdaUiVisibilitySubsystem>();
			C->Expect(V && V->IsUiShown(), TEXT("Z2 Alt+Z вернул интерфейс"));
		});

		// ═══ B. Окно «Вид» бустера 15600, открытое как из магазина «Усилители» (барабан 10050 закрыт) ═══
		At(C, 10.1f, [C]()
		{
			UAsdaItemTableSubsystem* T = C->Sub<UAsdaItemTableSubsystem>();
			UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			TArray<int32> Slots;
			for (int32 Code = 1; T && Code < 80000 && !C->Booster; ++Code)
			{
				if (T->GetItem(Code) && T->GetBoosterSlotsRaw(Code, Slots) && Slots.Num() > 0 && Slots[0] > 0) { C->Booster = Code; }
			}
			C->Expect(Bp && C->Booster > 0, FString::Printf(TEXT("B0 бустер с составом: %d"), C->Booster));
			if (Bp && C->Booster > 0) { Bp->PopulatePrevWindow(C->Booster); }   // то же, что клик по иконке карточки магазина
		});
		At(C, 10.7f, [C]()
		{
			UUserWidget* Win = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("BOOSTER_PREV_WIN")));
			FVector2D P = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
			const bool bGeo = Win && Center(AsdaUi::FindNodeByPrefix(Win, TEXT("BOOSTER_PREV_SITEM00")), P);
			C->Expect(bGeo, FString::Printf(TEXT("B1 окно «Вид» открыто, «особая» ячейка 0 на экране (%.0f,%.0f)"), P.X, P.Y));
			if (bGeo) { Hover(C->WorldPt); Hover(P); }
		});
		At(C, 11.1f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::BoosterPrev),
				FString::Printf(TEXT("B2 наведение на «особый» предмет окна «Вид» (без барабана) → подсказка предмета (%s)"), *TipState(C)));
			UUserWidget* Win = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("BOOSTER_PREV_WIN")));
			FVector2D P;
			if (Win && Center(AsdaUi::FindNodeByPrefix(Win, TEXT("BOOSTER_PREV_ITEM05")), P)) { Hover(P); }
		});
		At(C, 11.5f, [C]()
		{
			// «Обычный» раздел — ячейка 05 может быть пустой (у бустера бывает меньше обычных) — печать без провала.
			UE_LOG(LogAsdaTipCloseTest, Log, TEXT("%s инфо  B3 наведение на «обычный» предмет 05: %s"), kPfx, *TipState(C));
			UUserWidget* Win = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("BOOSTER_PREV_WIN")));
			FVector2D P;
			if (Win && Center(AsdaUi::FindNodeByPrefix(Win, TEXT("BOOSTER_PREV_SITEM00")), P)) { Hover(C->WorldPt); Hover(P); }
		});
		At(C, 11.9f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::BoosterPrev), FString::Printf(TEXT("B4 снова подсказка «особого» (%s)"), *TipState(C)));
			if (UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>()) { Bp->ClosePrevWindow(); }   // крестик 15648 окна
		});
		At(C, 12.3f, [C]()
		{
			C->Expect(TipGone(C), FString::Printf(TEXT("B5 окно «Вид» закрыто (курсор на месте) — подсказки нет (%s)"), *TipState(C)));
		});

		// ═══ C. Ячейка сумки → снос окон, как при смене карты ═══
		At(C, 12.6f, [C, OpenInv]() { OpenInv(C); });
		At(C, 12.9f, [C, HoverBag]() { HoverBag(C); });
		At(C, 13.3f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Item), FString::Printf(TEXT("C0 подсказка предмета (%s)"), *TipState(C)));
			// Смена карты так, как её делает игра: общий сброс сессии «смена карты» + снос окон (`RevealWorld` → `CloseAll`).
			if (UAsdaSessionResetSubsystem* Rs = C->Sub<UAsdaSessionResetSubsystem>()) { Rs->Broadcast(EAsdaSessionResetReason::MapChanged); }
			if (UAsdaWindowSubsystem* WMx = C->Sub<UAsdaWindowSubsystem>()) { WMx->CloseAll(); }
		});
		At(C, 13.7f, [C]()
		{
			C->Expect(TipGone(C), FString::Printf(TEXT("C1 окна сняты (смена карты) — подсказки нет (%s)"), *TipState(C)));
		});
		At(C, 14.1f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestTipClose(
	TEXT("asda.SelfTest.TipClose"),
	TEXT("Самотест «подсказка гаснет вместе с окном»: гнездо самоцвета инфо-панели и ячейка сумки + клавиша I, ESC, Alt+Z, снос окон; "
	     "справка кнопки 1704 + щелчок «закрыть мини-карту». Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaTipCloseTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
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
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
