// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTravelKeysSelfTest.cpp — самотест `asda.SelfTest.TravelKeys`: клавиатура жива после перемещений (баг владельца 07.11:
//   «после кристалла не работает ни одна клавиша»). Путь игрока: события клавиш идут в Slate без принудительного фокуса вьюпорту,
//   кнопка кристалла — настоящая ветка диалога NPC (`RunCommandForCheck(30033)`: 5477 + закрытие), ответ — настоящий 5055 на ту же
//   карту либо `OpenLevel` с офлайн-спавном (та же точка, что 4003). Проба: клавиша I открывает инвентарь, Enter открывает строку
//   чата. Только офлайн (-AsdaNoNet). Сравнение ключей — `--pre "asda.Chat.FocusIsTyping 0"`.
#include "CoreMinimal.h"
#include "UI/AsdaCampWarBattleSubsystem.h"   // ★ R4-b (15.11.2026): снять HUD «Битвы армий» после портала
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaAfterDeathSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Components/Button.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTravelKeysTest, Log, All);

namespace AsdaTravelKeysSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.TravelKeys");   // имя команды
	static constexpr int32 kCrystalNpc = 47;                       // NPC разговора (ветка 30033 от него не зависит)
	static constexpr TCHAR kCr = (TCHAR)13;                        // символ Enter

	// Шаг сценария: пауза и действие (true — сделано, false — ждать дальше).
	struct FStep { double Delay = 0.0; double Timeout = 60.0; TFunction<bool()> Fn; };

	// Состояние прогона: живёт в тикере и переживает смену карты.
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры
		bool bQuit = false;                 // `quit` в конце
		int32 Ok = 0, Fail = 0;             // счёт
		uint32 CapGen = 0;                  // поколение перехвата отправок
		FAsdaNetSelfScope SelfGuard;        // ★ R5-fix6 (13.11.2026): «свой» сети до прогона — возврат сторожем (в сводке пачки было «свой изменён»)
		int32 N5477 = 0;                    // сколько 5477 ушло
		bool bHudWas = false;               // ★ R4-b (15.11.2026) (ревизия, важн. 4): HUD «Битвы армий» до прогона
		uint8 CwTok1Was = 0xFF, CwTok2Was = 0xFF; bool bCwSupWas = false;   // ★ R4-b (15.11.2026): поля сети битвы армий до прогона
		bool bPrefixWas = false;            // ★ R4-b (15.11.2026): режим префикса канала чата до прогона
		int32 ChatStateWas = -1;            // ★ R4-b (15.11.2026): режим ввода чата до прогона (-1 — чата не было)
		bool bInGameWas = false;            // ★ R4-b (15.11.2026): признак сети «в игре» до прогона (тест ставит его сам)
		int32 SendFailWas = 0;              // ★ R4-b (15.11.2026): счёт неудачных отправок подряд до прогона
		UPTRINT OldWorld = 0;               // мир до смены карты
		TArray<FStep> Steps;                // шаги
		int32 Next = 0;                     // текущий шаг
		double StepT = 0.0;                 // вход в шаг

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaTravelKeysTest, Display, TEXT("КЛАВИШИ-ПЕРЕХОД ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaTravelKeysTest, Error, TEXT("КЛАВИШИ-ПЕРЕХОД FAIL %s"), *What); }
		}
		void Do(double Delay, TFunction<void()> F) { FStep S; S.Delay = Delay; S.Fn = [F = MoveTemp(F)]() { F(); return true; }; Steps.Add(MoveTemp(S)); }
		void Wait(double Delay, double Timeout, TFunction<bool()> F) { FStep S; S.Delay = Delay; S.Timeout = Timeout; S.Fn = MoveTemp(F); Steps.Add(MoveTemp(S)); }
	};
	using FRef = TSharedRef<FState>;

	// Клавиша, как её строит Slate из WM_KEYDOWN + WM_CHAR + WM_KEYUP; фокус не трогаем.
	static void Key(const FKey& K, uint32 Vk, TCHAR Ch, bool bDown)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication& App = FSlateApplication::Get();
		const FKeyEvent E(K, FModifierKeysState(), 0, false, (uint32)Ch, Vk);
		if (bDown)
		{
			App.ProcessKeyDownEvent(E);
			if (Ch) { App.ProcessKeyCharEvent(FCharacterEvent(Ch, FModifierKeysState(), 0, false)); }
		}
		else { App.ProcessKeyUpEvent(E); }
	}
	static void Tap(const FRef& S, const FKey& K, uint32 Vk, TCHAR Ch)
	{
		S->Do(0.12, [K, Vk, Ch]() { Key(K, Vk, Ch, true); });
		S->Do(0.12, [K, Vk, Ch]() { Key(K, Vk, Ch, false); });
	}
	static FString FocusName()
	{
		if (!FSlateApplication::IsInitialized()) { return TEXT("?"); }
		const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return F.IsValid() ? F->GetTypeAsString() : FString(TEXT("(нет)"));
	}

	// Клавиатура жива: I открывает инвентарь, Enter открывает строку чата.
	static void AddProbe(const FRef& S, const FString& Tag)
	{
		S->Do(0.5, [S, Tag]()
		{
			const UAsdaChatWindowSubsystem* C = S->Sub<UAsdaChatWindowSubsystem>();
			UE_LOG(LogAsdaTravelKeysTest, Display, TEXT("КЛАВИШИ-ПЕРЕХОД %s: фокус «%s», %s"), *Tag, *FocusName(), C ? *C->DebugInputState() : TEXT("нет чата"));
		});
		TSharedRef<bool> Was = MakeShared<bool>(false);   // инвентарь до нажатия (мог вернуться после смены карты)
		S->Do(0.05, [S, Was]() { UAsdaInventorySubsystem* Inv = S->Sub<UAsdaInventorySubsystem>(); *Was = Inv && Inv->IsWindowOpen(); });
		Tap(S, EKeys::I, 'I', TEXT('i'));
		S->Do(0.3, [S, Tag, Was]()
		{
			UAsdaInventorySubsystem* Inv = S->Sub<UAsdaInventorySubsystem>();
			S->Expect(Inv && Inv->IsWindowOpen() != *Was, FString::Printf(TEXT("%s: клавиша I переключила инвентарь (фокус «%s»)"), *Tag, *FocusName()));
			if (Inv && Inv->IsWindowOpen() && *Was) { *Was = false; }
		});
		S->Do(0.05, [S]() {});
		Tap(S, EKeys::I, 'I', TEXT('i'));
		S->Do(0.3, [S]() { UAsdaInventorySubsystem* Inv = S->Sub<UAsdaInventorySubsystem>(); if (Inv && Inv->IsWindowOpen()) { UE_LOG(LogAsdaTravelKeysTest, Display, TEXT("КЛАВИШИ-ПЕРЕХОД инвентарь остался открыт")); } });
		Tap(S, EKeys::Enter, 13, kCr);
		S->Do(0.3, [S, Tag]()
		{
			UAsdaChatWindowSubsystem* C = S->Sub<UAsdaChatWindowSubsystem>();
			S->Expect(C && C->IsInputActive(), FString::Printf(TEXT("%s: Enter открыл строку чата"), *Tag));
			if (C && C->IsInputActive()) { C->CloseInput(); }
		});
		S->Do(0.3, [](){});
	}

	// Отправить строку чата настоящими клавишами.
	static void AddChatSend(const FRef& S)
	{
		Tap(S, EKeys::Enter, 13, kCr);
		Tap(S, EKeys::A, 'A', TEXT('a'));
		Tap(S, EKeys::Enter, 13, kCr);
		S->Do(0.3, [S]() { if (UAsdaChatWindowSubsystem* C = S->Sub<UAsdaChatWindowSubsystem>()) { C->DebugResetAntiFlood(false); } });
	}

	// Кнопка кристалла: разговор и настоящая ветка 30033 (5477 + закрытие диалога).
	static void AddCrystalButton(const FRef& S, const FString& Tag)
	{
		S->Do(0.1, [S]() { if (UAsdaNpcDialogSubsystem* D = S->Sub<UAsdaNpcDialogSubsystem>()) { D->OpenForNpcId(kCrystalNpc); } });
		S->Do(0.4, [S, Tag]()
		{
			UAsdaNpcDialogSubsystem* D = S->Sub<UAsdaNpcDialogSubsystem>();
			S->Expect(D && D->IsDialogOpen(), FString::Printf(TEXT("%s: разговор с NPC открыт"), *Tag));
			const int32 Was = S->N5477;
			if (D) { D->RunCommandForCheck(30033, 0); }
			S->Expect(S->N5477 == Was + 1 && D && !D->IsDialogOpen(), FString::Printf(TEXT("%s: кнопка ушла 5477, диалог закрыт"), *Tag));
		});
	}

	// Настоящий щелчок мышью по виджету: движение, нажатие, отпускание через Slate.
	static void Click(UWidget* Wd, bool bDown)
	{
		if (!Wd || !FSlateApplication::IsInitialized()) { return; }
		const FGeometry G = Wd->GetCachedGeometry();
		const FVector2D At = G.LocalToAbsolute(G.GetLocalSize() * 0.5f);
		FSlateApplication& App = FSlateApplication::Get();
		TSet<FKey> Btn;
		if (bDown) { Btn.Add(EKeys::LeftMouseButton); }
		const FPointerEvent E(0, At, At, Btn, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (bDown)
		{
			App.ProcessMouseMoveEvent(FPointerEvent(0, At, At, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
			App.ProcessMouseButtonDownEvent(nullptr, E);
		}
		else { App.ProcessMouseButtonUpEvent(E); }
	}
	// Щелчок мышью по миру: середина вьюпорта через Slate, как рука игрока.
	static void ClickWorld(bool bDown)
	{
		if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) { return; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		if (!Vp.IsValid()) { return; }
		const FGeometry G = Vp->GetCachedGeometry();
		const FVector2D At = G.LocalToAbsolute(G.GetLocalSize() * 0.5f);
		FSlateApplication& App = FSlateApplication::Get();
		TSet<FKey> Btn;
		if (bDown) { Btn.Add(EKeys::LeftMouseButton); }
		const FPointerEvent E(0, At, At, Btn, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (bDown)
		{
			App.ProcessMouseMoveEvent(FPointerEvent(0, At, At, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
			App.ProcessMouseButtonDownEvent(nullptr, E);
		}
		else { App.ProcessMouseButtonUpEvent(E); }
	}
	// Чистый старт сценария: фокус у мира, поле пусто, антифлуд снят.
	static void AddCleanStart(const FRef& S)
	{
		S->Do(0.2, [S]()
		{
			if (UAsdaChatWindowSubsystem* C = S->Sub<UAsdaChatWindowSubsystem>()) { C->CloseInput(); C->DebugResetAntiFlood(false); }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		});
		S->Do(0.3, [](){});
	}
	// Что видит игрок сразу после шага: кто держит фокус.
	static void AddFocusNote(const FRef& S, const FString& Tag)
	{
		S->Do(0.2, [S, Tag]() { UE_LOG(LogAsdaTravelKeysTest, Display, TEXT("КЛАВИШИ-ПЕРЕХОД %s: фокус «%s»"), *Tag, *FocusName()); });
	}

	// Первая видимая кнопка меню разговора (NPC_SHOP_BUTnn, затем шаги NPC_QA_BUTnn).
	static UButton* FirstQa(const FRef& S)
	{
		UAsdaWindowSubsystem* WM = S->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("NPC_LINE_NPC"))) : nullptr;
		if (!Win || !Win->WidgetTree) { return nullptr; }
		UButton* Best = nullptr;
		Win->WidgetTree->ForEachWidget([&Best](UWidget* Wd)
		{
			UButton* B = Cast<UButton>(Wd);
			if (B && !Best && (B->GetName().Contains(TEXT("NPC_SHOP_BUT")) || B->GetName().Contains(TEXT("NPC_QA_BUT"))) && B->IsVisible() && B->GetCachedGeometry().GetLocalSize().X > 1.f) { Best = B; }
		});
		return Best;
	}
	// Кристалл путём игрока: разговор с NPC 50, щелчки мышью по пунктам меню, пока не уйдёт 5477.
	static void AddRealCrystal(const FRef& S, const FString& Tag)
	{
		S->Do(0.1, [S]() { if (UAsdaNpcDialogSubsystem* D = S->Sub<UAsdaNpcDialogSubsystem>()) { D->DebugResetNpcMet(); D->OpenForNpcId(50); } });
		TSharedRef<int32> Was = MakeShared<int32>(0);
		TSharedRef<TWeakObjectPtr<UButton>> Btn = MakeShared<TWeakObjectPtr<UButton>>();
		S->Do(0.6, [S, Was]() { *Was = S->N5477; });
		for (int32 i = 0; i < 6; ++i)
		{
			S->Do(0.5, [S, Was, Btn, Tag, i]()
			{
				*Btn = (S->N5477 == *Was) ? FirstQa(S) : nullptr;
				UE_LOG(LogAsdaTravelKeysTest, Display, TEXT("КЛАВИШИ-ПЕРЕХОД %s: щелчок %d по «%s»"), *Tag, i, Btn->IsValid() ? *(*Btn)->GetName() : TEXT("(нет)"));
				Click(Btn->Get(), true);
			});
			S->Do(0.1, [Btn]() { Click(Btn->Get(), false); });
		}
		S->Do(0.3, [S, Was, Tag]()
		{
			UAsdaNpcDialogSubsystem* D = S->Sub<UAsdaNpcDialogSubsystem>();
			S->Expect(S->N5477 == *Was + 1, FString::Printf(TEXT("%s: щелчок мышью ушёл 5477 (фокус «%s»)"), *Tag, *FocusName()));
			if (D && D->IsDialogOpen()) { D->Close(); }
		});
	}

	// Ответ 5055 код 1 на ту же карту.
	static void AddSameMapWarp(const FRef& S)
	{
		S->Do(0.2, [S]()
		{
			UAsdaNetSubsystem* Net = S->Sub<UAsdaNetSubsystem>();
			if (!Net) { return; }
			TArray<uint8> B; B.SetNumZeroed(53);
			B[0] = 1;
			AsdaBytes::WrI16(&B[23], (int16)Net->EnterMapId);
			AsdaBytes::WrI16(&B[25], 260); AsdaBytes::WrI16(&B[27], 260);
			Net->DebugFeedPacket(5055, B);
		});
	}

	static bool ArmCap(const FRef& S)
	{
		UAsdaNetSubsystem* Net = S->Sub<UAsdaNetSubsystem>();
		if (!Net) { return false; }
		if (S->CapGen != 0) { AsdaSelfTestKit::DisarmCapture(Net, S->CapGen); }
		S->SelfGuard.Capture(Net, kCmd);   // ★ R5-fix6: первый снимок главный (повторный вызов ничего не делает)
		TWeakPtr<FState> WS = S;
		S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WS](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			if (TSharedPtr<FState> P = WS.Pin()) { if (Op == 5477) { ++P->N5477; } }
		}, 0, kCmd);
		return S->CapGen != 0;
	}

	// Смена карты, как после 5055: OpenLevel и точка входа 4003.
	static void AddLevelTravel(const FRef& S)
	{
		S->Do(0.2, [S]()
		{
			UWorld* W = S->W();
			S->OldWorld = (UPTRINT)W;
			if (W) { UGameplayStatics::OpenLevel(W, FName(TEXT("alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
		});
		S->Wait(0.5, 120.0, [S]()
		{
			UWorld* W = S->W();
			if (!W || (UPTRINT)W == S->OldWorld || !W->HasBegunPlay()) { return false; }
			if (GEngine)
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			return true;
		});
		S->Wait(1.0, 30.0, [S]()
		{
			UWorld* W = S->W();
			const APlayerController* P = W ? W->GetFirstPlayerController() : nullptr;
			if (!P || !P->GetPawn()) { return false; }
			if (UAsdaNetSubsystem* Net = S->Sub<UAsdaNetSubsystem>()) { Net->bInGameConnected = true; }
			S->Expect(ArmCap(S), TEXT("перехват отправок жив после смены карты"));
			return true;
		});
	}

	// Окно-вопрос открыто в момент перемещения. Видимая модалка держит клавиши законно; после перемещения
	//   замка без видимого окна быть не должно. Окно закрываем его же ответом, потом проверяем клавиши.
	static void AddModalOpen(const FRef& S, bool bQuestion, const FString& Tag)
	{
		S->Do(0.2, [S, bQuestion, Tag]()
		{
			UAsdaWindowSubsystem* WM = S->Sub<UAsdaWindowSubsystem>();
			if (!WM) { return; }
			if (bQuestion) { WM->ShowGameConfirm(TEXT("travel keys"), [](bool) {}); }
			else { WM->ShowGameMessage(TEXT("travel keys")); }
		});
		S->Do(0.5, [S, Tag]()
		{
			const UAsdaWindowSubsystem* WM = S->Sub<UAsdaWindowSubsystem>();
			S->Expect(WM && WM->IsAnyModalOpen(), FString::Printf(TEXT("%s: модальное окно открыто"), *Tag));
		});
	}
	static void AddModalAfter(const FRef& S, const FString& Tag)
	{
		S->Do(0.8, [S, Tag]()
		{
			UAsdaWindowSubsystem* WM = S->Sub<UAsdaWindowSubsystem>();
			UUserWidget* Top = WM ? WM->GetTopModalWindow() : nullptr;
			const bool bIgnore = GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput();
			UE_LOG(LogAsdaTravelKeysTest, Display, TEXT("КЛАВИШИ-ПЕРЕХОД %s: модалка=%d верхняя «%s» видима=%d вьюпорт глух=%d фокус «%s»"), *Tag,
				WM && WM->IsAnyModalOpen() ? 1 : 0, Top ? *Top->GetName() : TEXT("(нет)"), Top && Top->IsVisible() ? 1 : 0, bIgnore ? 1 : 0, *FocusName());
			S->Expect(!bIgnore || (Top && Top->IsVisible()), FString::Printf(TEXT("%s: вьюпорт глух только при видимой модалке"), *Tag));
		});
		Tap(S, EKeys::Enter, 13, kCr);   // ответ окну клавишей, как рукой
		S->Do(0.5, [S, Tag]()
		{
			const UAsdaWindowSubsystem* WM = S->Sub<UAsdaWindowSubsystem>();
			S->Expect(WM && !WM->IsAnyModalOpen(), FString::Printf(TEXT("%s: Enter закрыл окно"), *Tag));
			if (UAsdaChatWindowSubsystem* C = S->Sub<UAsdaChatWindowSubsystem>()) { if (C->IsInputActive()) { C->CloseInput(); } }
		});
		S->Do(0.3, [](){});
	}

	static void Build(const FRef& S)
	{
		S->Do(0.2, [S]()
		{
			S->Expect(ArmCap(S), TEXT("перехват отправок взведён"));
			if (UAsdaNetSubsystem* Net = S->Sub<UAsdaNetSubsystem>()) { Net->bInGameConnected = true; }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		});
		AddProbe(S, TEXT("0 до перемещений"));

		// 1. Кристалл на ту же карту.
		AddCrystalButton(S, TEXT("1 кристалл, та же карта"));
		AddSameMapWarp(S);
		AddProbe(S, TEXT("1 кристалл, та же карта"));

		// 2. Отправил чат, затем кристалл на ту же карту.
		AddChatSend(S);
		AddCrystalButton(S, TEXT("2 чат + кристалл, та же карта"));
		AddSameMapWarp(S);
		AddProbe(S, TEXT("2 чат + кристалл, та же карта"));

		// 3. Кристалл со сменой карты.
		AddCrystalButton(S, TEXT("3 кристалл, смена карты"));
		AddLevelTravel(S);
		AddProbe(S, TEXT("3 кристалл, смена карты"));

		// 4. Отправил чат, затем кристалл со сменой карты.
		AddChatSend(S);
		AddCrystalButton(S, TEXT("4 чат + кристалл, смена карты"));
		AddLevelTravel(S);
		AddProbe(S, TEXT("4 чат + кристалл, смена карты"));

		// 5. Смена карты с открытой строкой чата.
		Tap(S, EKeys::Enter, 13, kCr);
		AddLevelTravel(S);
		AddProbe(S, TEXT("5 смена карты с открытой строкой"));

		// 6. Кристалл настоящим щелчком мыши, та же карта и смена карты.
		AddRealCrystal(S, TEXT("6 щелчок, та же карта"));
		AddSameMapWarp(S);
		AddProbe(S, TEXT("6 щелчок, та же карта"));
		AddRealCrystal(S, TEXT("7 щелчок, смена карты"));
		AddLevelTravel(S);
		AddProbe(S, TEXT("7 щелчок, смена карты"));

		// 8. Открытый инвентарь при смене карты (окно возвращается после входа).
		Tap(S, EKeys::I, 'I', TEXT('i'));
		AddLevelTravel(S);
		AddProbe(S, TEXT("8 смена карты с открытым инвентарём"));

		// 9. Смерть и возврат в город на другую карту: меню смерти открыто до смены карты.
		S->Do(0.2, [S]()
		{
			UAsdaNetSubsystem* Net = S->Sub<UAsdaNetSubsystem>();
			if (!Net) { return; }
			if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 7; Net->bSelfUserNumKnown = true; }
			TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(&B[0], (int16)Net->SelfUserNum); AsdaBytes::WrI16(&B[2], -1);
			Net->DebugFeedPacket(4053, B);
		});
		S->Do(1.0, [S]() { const UAsdaAfterDeathSubsystem* Ad = S->Sub<UAsdaAfterDeathSubsystem>(); S->Expect(Ad && Ad->IsDeathMenuOpen(), TEXT("9: меню смерти открыто")); });
		AddLevelTravel(S);
		S->Do(0.3, [S]() { const UAsdaAfterDeathSubsystem* Ad = S->Sub<UAsdaAfterDeathSubsystem>(); S->Expect(Ad && !Ad->IsDeathMenuOpen(), TEXT("9: после смены карты меню смерти закрыто, замок снят")); });
		AddProbe(S, TEXT("9 смерть и смена карты"));

		// 10. Только чат, без кристалла: нужен ли кристалл вообще.
		AddCleanStart(S);
		AddChatSend(S);
		AddFocusNote(S, TEXT("10 после отправки"));
		AddProbe(S, TEXT("10 только чат"));

		// 11. Чат, затем щелчок по миру.
		AddCleanStart(S);
		AddChatSend(S);
		S->Do(0.2, []() { ClickWorld(true); });
		S->Do(0.1, []() { ClickWorld(false); });
		AddFocusNote(S, TEXT("11 после щелчка по миру"));
		AddProbe(S, TEXT("11 чат и щелчок по миру"));

		// 12. Чат, затем кристалл щелчками мыши по меню, та же карта.
		AddCleanStart(S);
		AddChatSend(S);
		AddRealCrystal(S, TEXT("12 чат и щелчки, та же карта"));
		AddSameMapWarp(S);
		AddProbe(S, TEXT("12 чат и щелчки, та же карта"));

		// 13. Чат, щелчок по кристаллу в мире, щелчки по меню, та же карта: полный путь руки игрока.
		AddCleanStart(S);
		AddChatSend(S);
		S->Do(0.2, []() { ClickWorld(true); });
		S->Do(0.1, []() { ClickWorld(false); });
		AddRealCrystal(S, TEXT("13 чат, мир, щелчки, та же карта"));
		AddSameMapWarp(S);
		AddProbe(S, TEXT("13 чат, мир, щелчки, та же карта"));

		// 14. Чат и щелчки со сменой карты.
		AddCleanStart(S);
		AddChatSend(S);
		AddRealCrystal(S, TEXT("14 чат и щелчки, смена карты"));
		AddLevelTravel(S);
		AddProbe(S, TEXT("14 чат и щелчки, смена карты"));

		// 15–18. Окно-вопрос и окно-сообщение открыты в момент перемещения.
		AddCleanStart(S);
		AddModalOpen(S, true, TEXT("15 вопрос, та же карта"));
		AddSameMapWarp(S);
		AddModalAfter(S, TEXT("15 вопрос, та же карта"));
		AddProbe(S, TEXT("15 вопрос, та же карта"));
		AddCleanStart(S);
		AddModalOpen(S, false, TEXT("16 сообщение, та же карта"));
		AddSameMapWarp(S);
		AddModalAfter(S, TEXT("16 сообщение, та же карта"));
		AddProbe(S, TEXT("16 сообщение, та же карта"));
		AddCleanStart(S);
		AddModalOpen(S, true, TEXT("17 вопрос, смена карты"));
		AddLevelTravel(S);
		AddModalAfter(S, TEXT("17 вопрос, смена карты"));
		AddProbe(S, TEXT("17 вопрос, смена карты"));
		AddCleanStart(S);
		AddModalOpen(S, false, TEXT("18 сообщение, смена карты"));
		AddLevelTravel(S);
		AddModalAfter(S, TEXT("18 сообщение, смена карты"));
		AddProbe(S, TEXT("18 сообщение, смена карты"));
	}

	static void Finish(const FRef& S)
	{
		if (S->CapGen != 0) { AsdaSelfTestKit::DisarmCapture(S->Sub<UAsdaNetSubsystem>(), S->CapGen); S->CapGen = 0; }
		S->SelfGuard.Restore();   // ★ R5-fix6: смерть шага 9 и номер — как до прогона
		// ★ R4-b (15.11.2026) (ревизия, важн. 4): портал 5055 включил HUD «Битвы армий» (окна 49500, карта, очки) — снять сбросом сессии боя,
		//   как `SelfTestCampWarReg`, если до прогона его не было; ДО возврата чата (снятие HUD пишет в чат свой сохранённый режим).
		if (UAsdaCampWarBattleSubsystem* Bt = S->Sub<UAsdaCampWarBattleSubsystem>()) { if (!S->bHudWas && Bt->IsHudOn()) { Bt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); } }
		if (UAsdaNetSubsystem* NetCw = S->Sub<UAsdaNetSubsystem>()) { NetCw->CampWarToken1 = S->CwTok1Was; NetCw->CampWarToken2 = S->CwTok2Was; NetCw->bCampWarHudSuppressed = S->bCwSupWas; }
		// ★ R4-b (15.11.2026): признак «в игре» и счёт неудач — как до прогона. Иначе под `-AsdaNoNet` (все отправки FAIL) третья неудача
		//   соседа объявляет обрыв (`TrackSend`): сброс сессии и окно 8000 — DblClick/Soul.Slot/Soul.Stone/Soulmate.Bar красные.
		if (UAsdaNetSubsystem* Net = S->Sub<UAsdaNetSubsystem>()) { Net->bInGameConnected = S->bInGameWas; Net->SendFailStreak = S->SendFailWas; }
		// ★ R4-b (15.11.2026): портал шага с картой 65535 поднимает HUD битвы армий — префикс канала и ввод 9; вернуть, как было
		//   (иначе сосед по пачке `asda.SelfTest.WhisperReply` видит строки «[Состояние] …»; так же убирает `SelfTestCampWarBattle`).
		if (UAsdaChatWindowSubsystem* C = S->Sub<UAsdaChatWindowSubsystem>())
		{
			C->SetChannelPrefixMode(S->bPrefixWas);
			if (S->ChatStateWas >= 0 && C->GetInputState() != S->ChatStateWas) { C->SetInputState(S->ChatStateWas); }
		}
		AsdaSelfTestKit::Finish(S->W(), kCmd, S->bQuit, LogAsdaTravelKeysTest, TEXT("КЛАВИШИ-ПЕРЕХОД"), S->Ok, S->Fail);
	}

	static bool Tick(const FRef& S)
	{
		if (!S->GI.IsValid()) { return false; }
		if (S->Next >= S->Steps.Num()) { Finish(S); return false; }
		const double Now = FPlatformTime::Seconds();
		FStep& St = S->Steps[S->Next];
		const double In = Now - S->StepT;
		if (In < St.Delay) { return true; }
		if (St.Fn()) { ++S->Next; S->StepT = Now; return true; }
		if (In > St.Delay + St.Timeout) { S->Expect(false, FString::Printf(TEXT("шаг %d: вышло время"), S->Next)); ++S->Next; S->StepT = Now; }
		return true;
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		const uint16 Extra[] = { 5477 };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 0.f, Extra)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
		}
		FRef S = MakeShared<FState>();
		S->GI = GI;
		S->bQuit = Args.bQuit;
		if (const UAsdaCampWarBattleSubsystem* Bt0 = GI->GetSubsystem<UAsdaCampWarBattleSubsystem>()) { S->bHudWas = Bt0->IsHudOn(); }   // ★ R4-b (15.11.2026)
		if (const UAsdaNetSubsystem* Nc0 = GI->GetSubsystem<UAsdaNetSubsystem>()) { S->CwTok1Was = Nc0->CampWarToken1; S->CwTok2Was = Nc0->CampWarToken2; S->bCwSupWas = Nc0->bCampWarHudSuppressed; }   // ★ R4-b (15.11.2026)
		if (const UAsdaChatWindowSubsystem* C0 = GI->GetSubsystem<UAsdaChatWindowSubsystem>()) { S->bPrefixWas = C0->IsChannelPrefixMode(); S->ChatStateWas = C0->GetInputState(); }   // ★ R4-b (15.11.2026)
		if (const UAsdaNetSubsystem* N0 = GI->GetSubsystem<UAsdaNetSubsystem>()) { S->bInGameWas = N0->bInGameConnected; S->SendFailWas = N0->SendFailStreak; }   // ★ R4-b (15.11.2026)
		Build(S);
		S->StepT = FPlatformTime::Seconds() + 2.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool { return Tick(S); }), 0.0f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestTravelKeys(
	TEXT("asda.SelfTest.TravelKeys"),
	TEXT("Самотест: клавиатура жива после кристалла (та же карта и смена карты), отправки чата и смены карты с открытой строкой. Только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaTravelKeysSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
