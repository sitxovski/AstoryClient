// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ЩЕЛЧОК МИМО МЕНЮ» (07.11.2026): `asda.UI.SelfTestClickAway [quit]`. Офлайн (`-AsdaNoNet`), карта мира.
//   Вопрос: гаснет ли всплывающее меню от нажатия мимо него (`asda.UI.ClickAwayMenu`).
//   Нажатия настоящие: события идут через `FSlateApplication`, то есть через препроцессор `FAsdaTopPressGate`.
//   На каждое меню (2670, 80000, 2000, 28000, 8100, 43058): ключ 0 — меню остаётся (дефект); ключ 1 — нажатие внутри его не гасит,
//   ЛКМ и ПКМ мимо гасят, потеря фокуса приложения гасит. Меню 49400 проверяет `asda.CampWar.BattleSelfTest` (нужен живой бой).
//   Отдельно: щелчок по другой вкладке гасит 2670 и выбирает вкладку; ПКМ по вкладке снова открывает 2670; ESC закрывает 2670 и не трогает
//   80000 (как в оригинале); раскрытый список каналов гаснет от щелчка по вкладке (`asda.Chat.ComboClickAway`).
//   Итог: «ЩЕЛЧОК-МИМО ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaPartySubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaShowOffSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerController.h"   // ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): уборка хода в конце
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClickAwayTest, Log, All);

namespace AsdaClickAwayTest
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestClickAway");   // имя команды
	static const TCHAR* kKey = TEXT("asda.UI.ClickAwayMenu");       // проверяемый ключ меню
	static const TCHAR* kComboKey = TEXT("asda.Chat.ComboClickAway");   // проверяемый ключ списка каналов
	static const FName kChatZone(TEXT("CHAT_NEW_CONTEXT_MENU"));    // зона меню чата
	static const FName kChatWin(TEXT("CHAT_NEW"));                  // окно чата

	// Меню под проверкой.
	struct FMenu
	{
		const TCHAR* Name = nullptr;      // подпись в журнале
		FName Zone;                       // зона меню в реестре окон
		TFunction<void(struct FRun&)> Open;    // открыть меню
		TFunction<void(struct FRun&)> Close;   // закрыть меню штатным путём
		FKey Key0Button = EKeys::LeftMouseButton;   // кнопка, которой ключ 0 показывает дефект
		bool bInsideAtEnd = false;                  // точка внутри — у нижнего правого угла: верхний левый занят кнопкой
	};

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;                 // мир
		TWeakObjectPtr<UAsdaWindowSubsystem> WM;      // окна
		TWeakObjectPtr<UAsdaChatWindowSubsystem> Chat;   // чат
		TWeakObjectPtr<UAsdaShowOffSubsystem> ShowOff;   // меню 28000
		TWeakObjectPtr<UAsdaPartySubsystem> Party;    // меню 8100
		TWeakObjectPtr<UAsdaPetCageSubsystem> Pet;    // панель 43058
		TWeakObjectPtr<UAsdaMinimapSubsystem> Mini;   // панель каналов 17501
		FVector2D BtnPt = FVector2D::ZeroVector;      // точка кнопки 17500
		bool BtnFound = false;                        // кнопка 17500 найдена
		TUniquePtr<FAsdaCVarScope> Keys;              // ключи вернутся сами
		TArray<TFunction<bool(FRun&)>> Steps;         // шаги; false — повторить шаг
		int32 Next = 0;                               // следующий шаг
		FTimerHandle Timer;                           // таймер шагов
		FVector2D Far = FVector2D::ZeroVector;        // точка мира
		FVector2D OpenAt = FVector2D::ZeroVector;     // курсор при открытии меню
		bool bQuit = false;                           // выйти после прогона
		int32 Ok = 0;                                 // пройдено
		int32 Fail = 0;                               // провалено
		int32 TabBefore = 0;                          // активная вкладка до щелчка
	};

	// Проверка с записью в журнал.
	static void Check(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok;   UE_LOG(LogAsdaClickAwayTest, Log,   TEXT("ЩЕЛЧОК-МИМО ok   %s"), *What); }
		else   { ++R.Fail; UE_LOG(LogAsdaClickAwayTest, Error, TEXT("ЩЕЛЧОК-МИМО FAIL %s"), *What); }
	}

	// Настоящее нажатие и отпускание кнопки мыши в точке.
	static void Press(const FVector2D& P, const FKey& Button)
	{
		FSlateApplication& App = FSlateApplication::Get();
		TSet<FKey> Down;
		Down.Add(Button);
		App.SetCursorPos(P);
		App.ProcessMouseMoveEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, P, P, Down, Button, 0.f, FModifierKeysState()));
	}

	// Отпускание кнопки мыши в точке.
	static void Release(const FVector2D& P, const FKey& Button)
	{
		FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(0, P, P, TSet<FKey>(), Button, 0.f, FModifierKeysState()));
	}

	// Видно ли меню: зона отдаёт прямоугольник только показанному меню.
	static bool IsOpen(const FRun& R, FName Zone)
	{
		FVector2D Mn, Mx;
		return R.WM.IsValid() && R.WM->DebugGetClickAwayRect(Zone, Mn, Mx);
	}

	// Точка рамки меню: внутри зоны, но не на кнопке пункта.
	static bool InsidePoint(const FRun& R, FName Zone, bool bAtEnd, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!R.WM.IsValid() || !R.WM->DebugGetClickAwayRect(Zone, Mn, Mx)) { return false; }
		Out = bAtEnd ? Mx - FVector2D(2.0, 2.0) : Mn + FVector2D(2.0, 2.0);
		return true;
	}

	// Центр вкладки чата по началу имени узла.
	static bool TabPoint(const FRun& R, const TCHAR* Prefix, FVector2D& Out)
	{
		UUserWidget* Win = R.WM.IsValid() ? R.WM->FindWindow(kChatWin) : nullptr;
		if (!Win || !Win->WidgetTree) { return false; }
		bool bFound = false;
		Win->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			FVector2D Mn, Mx;
			if (!bFound && W && W->GetName().StartsWith(Prefix) && W->IsVisible() && AsdaUi::WidgetAbsRect(W, Mn, Mx))
			{
				Out = (Mn + Mx) * 0.5;
				bFound = true;
			}
		});
		return bFound;
	}

	// Центр кнопки 17500 на мини-карте.
	static bool ChannelButtonPoint(const FRun& R, FVector2D& Out)
	{
		UUserWidget* Win = R.WM.IsValid() ? R.WM->FindWindow(FName(TEXT("MINIMAP_WIN"))) : nullptr;
		FVector2D Mn, Mx;
		if (!Win || !AsdaUi::WidgetAbsRect(Win->GetWidgetFromName(TEXT("Change_Channel_Btn")), Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5;
		return true;
	}

	// Шаги одного меню.
	static void AddMenu(TSharedRef<FRun> R, const FMenu& M)
	{
		auto Add = [&R](TFunction<bool(FRun&)> F) { R->Steps.Add(MoveTemp(F)); };
		auto Pause = [&R](int32 N) { for (int32 i = 0; i < N; ++i) { R->Steps.Add([](FRun&) { return true; }); } };
		auto Open = [&](int32 Key)
		{
			// Меню у курсора: курсор уводим от точки щелчка мимо, иначе меню откроется прямо на ней.
			// Положение курсора меню читает на следующем кадре: перенос и открытие идут разными шагами.
			Add([Key](FRun& X) { X.Keys->Set(kKey, Key); FSlateApplication::Get().SetCursorPos(X.OpenAt); return true; });
			Pause(5);
			Add([M](FRun& X) { M.Open(X); return true; });
			Pause(10);
		};
		const FString Name(M.Name);

		// К0: ключ 0 воспроизводит дефект.
		Open(0);
		Add([M, Name](FRun& X) { Check(X, IsOpen(X, M.Zone), Name + TEXT(" ключ 0: меню открыто")); Press(X.Far, M.Key0Button); return true; });
		Pause(3);
		Add([M, Name](FRun& X) { Release(X.Far, M.Key0Button); Check(X, IsOpen(X, M.Zone), Name + TEXT(" ключ 0: щелчок мимо меню не гасит (дефект)")); M.Close(X); return true; });
		Pause(3);

		// К1: нажатие внутри не гасит, ЛКМ мимо гасит.
		Open(1);
		Add([M, Name](FRun& X)
		{
			FVector2D P;
			const bool bPt = InsidePoint(X, M.Zone, M.bInsideAtEnd, P);
			Check(X, bPt, Name + TEXT(" ключ 1: меню открыто"));
			if (bPt) { Press(P, EKeys::LeftMouseButton); Release(P, EKeys::LeftMouseButton); }
			return true;
		});
		Pause(3);
		Add([M, Name](FRun& X) { Check(X, IsOpen(X, M.Zone), Name + TEXT(": нажатие по рамке меню его не гасит")); Press(X.Far, EKeys::LeftMouseButton); return true; });
		Pause(3);
		Add([M, Name](FRun& X) { Release(X.Far, EKeys::LeftMouseButton); Check(X, !IsOpen(X, M.Zone), Name + TEXT(": ЛКМ по миру гасит меню")); return true; });
		Pause(3);

		// К2: ПКМ мимо гасит.
		Open(1);
		Add([](FRun& X) { Press(X.Far, EKeys::RightMouseButton); return true; });
		Pause(3);
		Add([M, Name](FRun& X) { Release(X.Far, EKeys::RightMouseButton); Check(X, !IsOpen(X, M.Zone), Name + TEXT(": ПКМ по миру гасит меню")); return true; });
		Pause(3);

		// К3: потеря фокуса приложения гасит.
		Open(1);
		Add([M, Name](FRun& X)
		{
			FSlateApplication::Get().ProcessApplicationActivationEvent(false);
			Check(X, !IsOpen(X, M.Zone), Name + TEXT(": потеря фокуса приложения гасит меню"));
			FSlateApplication::Get().ProcessApplicationActivationEvent(true);
			return true;
		});
		Pause(3);
	}

	// Следующий шаг или итог.
	static void Pump(TSharedRef<FRun> R)
	{
		UWorld* W = R->World.Get();
		if (!W) { return; }
		if (R->Next >= R->Steps.Num())
		{
			R->Keys.Reset();
			// ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): щелчки ЛКМ по миру строят маршрут — тест гасит свой ход, чтобы следующий тест пачки не начинал на бегу
			//   (`UI.SelfTestClickThrough` в rx_r5fix4 снял позицию на бегу: 8/4). Офлайн 4008 не уходит (`StopMoveAndNotify`).
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { PC->StopMoveAndNotify(TEXT("самотест щелчок-мимо: уборка")); }
			AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaClickAwayTest, TEXT("ЩЕЛЧОК-МИМО"), R->Ok, R->Fail);
			return;
		}
		if (R->Steps[R->Next](*R)) { ++R->Next; }
		W->GetTimerManager().SetTimer(R->Timer, FTimerDelegate::CreateLambda([R]() { Pump(R); }), 0.02f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestClickAway(
	TEXT("asda.UI.SelfTestClickAway"),
	TEXT("Самотест щелчка мимо всплывающего меню: настоящие нажатия по 2670, 80000, 2000, 28000, 8100, 43058, потеря фокуса, ESC. Аргумент: quit."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaClickAwayTest;
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
		UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
		TSharedPtr<SWindow> Win = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
		if (!WM || !Chat || !Win.IsValid() || !FSlateApplication::IsInitialized())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет окон / чата / окна игры"));
			return;
		}
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->World = W; R->WM = WM; R->Chat = Chat; R->bQuit = KA.bQuit;
		R->ShowOff = GI->GetSubsystem<UAsdaShowOffSubsystem>();
		R->Party = GI->GetSubsystem<UAsdaPartySubsystem>();
		R->Pet = GI->GetSubsystem<UAsdaPetCageSubsystem>();
		R->Mini = GI->GetSubsystem<UAsdaMinimapSubsystem>();
		const FVector2D ScreenMn(Win->GetPositionInScreen());
		const FVector2D ScreenSize(Win->GetSizeInScreen());
		R->Far = ScreenMn + FVector2D(ScreenSize.X * 0.5, ScreenSize.Y * 0.35);
		R->OpenAt = ScreenMn + FVector2D(ScreenSize.X * 0.72, ScreenSize.Y * 0.5);
		R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
		if (UAsdaUiVisibilitySubsystem* Vis = GI->GetSubsystem<UAsdaUiVisibilitySubsystem>()) { Vis->SetUiShown(true, TEXT("самотест щелчка мимо")); }
		auto Add = [&R](TFunction<bool(FRun&)> F) { R->Steps.Add(MoveTemp(F)); };
		auto Pause = [&R](int32 N) { for (int32 i = 0; i < N; ++i) { R->Steps.Add([](FRun&) { return true; }); } };

		// Подготовка: сосед для меню игрока, отряд для меню строки.
		Add([](FRun& X)
		{
			UWorld* Wd = X.World.Get();
			GEngine->Exec(Wd, TEXT("asda.PlayerMenu.FakePeer 7 Сосед"));
			GEngine->Exec(Wd, TEXT("asda.Party.FakeRoster 1 2 3"));
			Check(X, X.WM->FindWindowCatchingAt(X.Far).IsNone(), TEXT("точка щелчка мимо лежит в мире, а не в окне"));
			return true;
		});
		Pause(30);

		FMenu Tab;
		Tab.Name = TEXT("2670 меню вкладок"); Tab.Zone = kChatZone;
		Tab.Open = [](FRun& X) { X.Chat->DebugShowTabMenu(); };
		Tab.Close = [](FRun& X) { X.Chat->DebugHideContextMenus(); };
		AddMenu(R, Tab);

		FMenu Nick;
		Nick.Name = TEXT("80000 меню по нику"); Nick.Zone = kChatZone;
		Nick.Open = [](FRun& X) { X.Chat->DebugShowNickMenu(TEXT("Сосед")); };
		Nick.Close = [](FRun& X) { X.Chat->DebugHideContextMenus(); };
		AddMenu(R, Nick);

		FMenu Player;
		Player.Name = TEXT("2000 меню игрока"); Player.Zone = FName(TEXT("PLAYER_MENU"));
		Player.Open = [](FRun& X) { GEngine->Exec(X.World.Get(), TEXT("asda.PlayerMenu.Open 7")); };
		Player.Close = [](FRun& X) { GEngine->Exec(X.World.Get(), TEXT("asda.PlayerMenu.Click 2052")); };
		Player.Key0Button = EKeys::RightMouseButton;   // ЛКМ по земле 2000 гасит и при ключе 0: шаг героя закрывает окна NPC
		AddMenu(R, Player);

		if (R->ShowOff.IsValid())
		{
			FMenu Show;
			Show.Name = TEXT("28000 меню хвастовства"); Show.Zone = FName(TEXT("SHOWOFF_MENU"));
			Show.Open = [](FRun& X) { X.ShowOff->OpenMenuForCell(0, 0, 1); };
			Show.Close = [](FRun& X) { X.ShowOff->CloseMenu(); };
			AddMenu(R, Show);
		}
		if (R->Party.IsValid())
		{
			FMenu Row;
			Row.Name = TEXT("8100 меню строки отряда"); Row.Zone = FName(TEXT("Follow_Another_win"));
			Row.Open = [](FRun& X) { X.Party->OpenRowMenu(0); };
			Row.Close = [](FRun& X) { X.Party->CloseRowMenu(); };
			AddMenu(R, Row);
		}
		if (R->Pet.IsValid())
		{
			FMenu Panel;
			Panel.Name = TEXT("43058 панель питомца"); Panel.Zone = FName(TEXT("Pet_Button_Win"));
			Panel.Open = [](FRun& X) { X.Pet->DebugShowActionPanel(); };
			Panel.Close = [](FRun& X) { X.Pet->DebugHideActionPanel(); };
			AddMenu(R, Panel);
		}

		if (R->Mini.IsValid())
		{
			FMenu Channels;
			Channels.Name = TEXT("17501 панель каналов"); Channels.Zone = FName(TEXT("MINIMAP_CHANNEL_PANEL"));
			Channels.Open = [](FRun& X) { X.Mini->SetChannelPanelOpen(true); };
			Channels.Close = [](FRun& X) { X.Mini->SetChannelPanelOpen(false); };
			Channels.bInsideAtEnd = true;
			AddMenu(R, Channels);

			// Кнопка 17500 — переключатель: щелчок по ней при открытой панели закрывает её, а не открывает заново.
			Add([](FRun& X) { X.Keys->Set(kKey, 1); X.Mini->SetChannelPanelOpen(true); return true; });
			Pause(10);
			Add([](FRun& X)
			{
				X.BtnFound = ChannelButtonPoint(X, X.BtnPt);
				Check(X, X.BtnFound, TEXT("кнопка 17500 найдена"));
				if (X.BtnFound) { Press(X.BtnPt, EKeys::LeftMouseButton); }
				return true;
			});
			Pause(3);
			Add([](FRun& X) { if (X.BtnFound) { Release(X.BtnPt, EKeys::LeftMouseButton); } return true; });
			Pause(5);
			Add([](FRun& X)
			{
				Check(X, !IsOpen(X, FName(TEXT("MINIMAP_CHANNEL_PANEL"))), TEXT("щелчок по кнопке 17500 закрывает открытую панель и не открывает её заново"));
				X.Mini->SetChannelPanelOpen(false);
				return true;
			});
			Pause(3);
		}

		// Щелчок по другой вкладке: меню гаснет, вкладка выбирается.
		Add([](FRun& X) { X.Keys->Set(kKey, 1); X.Chat->DebugShowTabMenu(); return true; });
		Pause(10);
		Add([](FRun& X)
		{
			FVector2D P;
			const bool bPt = TabPoint(X, TEXT("CHAT_NEW_Tab1"), P);
			Check(X, bPt, TEXT("вкладка 1 найдена"));
			X.TabBefore = X.Chat->DebugActiveTab();
			if (bPt) { Press(P, EKeys::LeftMouseButton); }
			return true;
		});
		Pause(3);
		Add([](FRun& X)
		{
			FVector2D P;
			if (TabPoint(X, TEXT("CHAT_NEW_Tab1"), P)) { Release(P, EKeys::LeftMouseButton); }
			Check(X, !X.Chat->IsTabMenuShown(), TEXT("щелчок по другой вкладке гасит 2670"));
			Check(X, X.Chat->DebugActiveTab() != X.TabBefore, FString::Printf(TEXT("тот же щелчок выбрал вкладку: была %d, стала %d"), X.TabBefore, X.Chat->DebugActiveTab()));
			return true;
		});
		Pause(5);

		// Путь игрока: ПКМ по вкладке открывает 2670, ПКМ по ней же при открытом меню открывает его снова.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			Add([](FRun& X)
			{
				FVector2D P;
				if (TabPoint(X, TEXT("CHAT_NEW_Tab0"), P)) { Press(P, EKeys::RightMouseButton); }
				return true;
			});
			Pause(3);
			Add([](FRun& X)
			{
				FVector2D P;
				if (TabPoint(X, TEXT("CHAT_NEW_Tab0"), P)) { Release(P, EKeys::RightMouseButton); }
				return true;
			});
			Pause(5);
			Add([Pass](FRun& X) { Check(X, X.Chat->IsTabMenuShown(), FString::Printf(TEXT("ПКМ по вкладке открывает 2670, проход %d"), Pass)); return true; });
		}

		// ESC: 2670 закрывается, 80000 нет (как в оригинале).
		Add([](FRun& X) { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); return true; });
		Pause(3);
		Add([](FRun& X)
		{
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27));
			Check(X, !X.Chat->IsTabMenuShown(), TEXT("ESC закрывает 2670"));
			X.Chat->DebugShowNickMenu(TEXT("Сосед"));
			return true;
		});
		Pause(10);
		Add([](FRun& X) { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); return true; });
		Pause(3);
		Add([](FRun& X)
		{
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27));
			Check(X, IsOpen(X, kChatZone), TEXT("ESC не закрывает 80000: у него нет признака 0x228, как в оригинале"));
			X.Chat->DebugHideContextMenus();
			return true;
		});
		Pause(5);

		// Список каналов: щелчок по вкладке гасит его при ключе 1 и не гасит при ключе 0. Вкладка 2: центр вкладки 1 лежит под самим списком.
		for (int32 Key = 0; Key < 2; ++Key)
		{
			Add([Key](FRun& X) { X.Keys->Set(kComboKey, Key); X.Chat->DebugSetChannelListOpen(true); return true; });
			Pause(10);
			Add([](FRun& X)
			{
				FVector2D P;
				const bool bPt = TabPoint(X, TEXT("CHAT_NEW_Tab2"), P);
				Check(X, bPt, TEXT("вкладка 2 найдена"));
				if (bPt) { Press(P, EKeys::LeftMouseButton); }
				return true;
			});
			Pause(3);
			Add([Key](FRun& X)
			{
				FVector2D P;
				if (TabPoint(X, TEXT("CHAT_NEW_Tab2"), P)) { Release(P, EKeys::LeftMouseButton); }
				Check(X, X.Chat->DebugIsChannelListOpen() == (Key == 0), FString::Printf(TEXT("ключ %d: щелчок по вкладке %s список каналов"), Key, Key == 0 ? TEXT("не гасит") : TEXT("гасит")));
				X.Chat->DebugSetChannelListOpen(false);
				return true;
			});
			Pause(5);
		}

		// Вернуть вкладку 0: пакет гоняет самотесты в одном процессе, а следующий ждёт вкладку по умолчанию.
		Add([](FRun& X)
		{
			FVector2D P;
			if (TabPoint(X, TEXT("CHAT_NEW_Tab0"), P)) { Press(P, EKeys::LeftMouseButton); }
			return true;
		});
		Pause(3);
		Add([](FRun& X)
		{
			FVector2D P;
			if (TabPoint(X, TEXT("CHAT_NEW_Tab0"), P)) { Release(P, EKeys::LeftMouseButton); }
			Check(X, X.Chat->DebugActiveTab() == 0, FString::Printf(TEXT("вкладка 0 возвращена (%d)"), X.Chat->DebugActiveTab()));
			return true;
		});
		Pause(5);

		Pump(R);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
