// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ENTER = КНОПКА СОГЛАСИЯ» (25.10.2026): `asda.SelfTest.EnterConfirm [quit]`. Разбор — `UAsdaEnterConfirmSubsystem`.
//   Путь игрока: клавиши — настоящие события Slate (`ProcessKeyDownEvent` / `ProcessKeyUpEvent` / `ProcessKeyCharEvent`), то есть
//   через препроцессоры, фокус, вьюпорт и `UPlayerInput`, как от клавиатуры; окна — штатными точками показа игры. Без сети
//   (-AsdaNoNet, второй барьер на все опкоды + перехват отправок). Запускать в мире (`--map world`). Итог: «ENTER ИТОГ: ok=N fail=M».
//   Разделы:
//     T. Таблица окон сверена с ассетами (каждая кнопка — UButton в WBP); коды клавиши: VK_RETURN (13) = Enter (у Win32 Enter
//        цифрового блока — тот же VK_RETURN с битом «расширенная»), событие с кодами 13/13 даёт ту же клавишу.
//     A. Контроль: окон нет — Enter открывает строку чата (прежний путь, эталон `uiChatInputToggle`).
//     B. ONEBUT 1500: нажатие ещё не жмёт (действие на отпускании), отпускание — ОК ровно один раз, окно закрыто, чат не открыт.
//     C. TWOBUT 1520: Enter = «ОК/Да» (колбэк true).
//     D. Недоступная кнопка: TWOBUT с полосой-гейтом — Enter съеден, ничего не нажато, окно на месте; после заливки — нажимается.
//     E. Удержание: два ONEBUT в очереди, Enter + 6 автоповторов + отпускание — закрыто ровно одно; второе — новым нажатием.
//     F. Окно, появившееся во время удержания, не подтверждается этим удержанием.
//     G. Модальное поверх окна: редактор вкладок чата 2590 + TWOBUT — Enter жмёт TWOBUT, редактор цел; следующий Enter — «ОК» 2590.
//     H. Строка чата открыта при окне 2590 — Enter достаётся чату, кнопка окна не нажата.
//     N. Недоступная кнопка НЕмодального окна — не нажата, Enter прежним путём (строка чата).
//     I. Нумпад 1410: фокус в поле — Enter по правилу поля (ОК с числом), механизм не вмешивается; фокус вне поля — «ОК» 1415.
//     P. Модалка покупки 1559 (трата премиум-валюты): Enter НЕ подтверждает (редакция 3.0.1).
//   ★ Редакция 3.0.1 (ревизия `wave_1025c_review`): S — 36 опасных/входящих окон в таблицу не попали; W — окно младше 400 мс
//     Enter не подтверждает; X — вопрос «без Enter» (TWOBUT и MESSAGE2 удаления персонажа) не подтверждается, пометка не залипает;
//     Q — подмена вопроса в том же окне TWOBUT во время удержания не подтверждается; M — модалка при открытой строке чата
//     не нажимается; Z — ESC реальной клавишей под модалкой: TWOBUT = «Отмена», ONEBUT — съеден (эталон).
//     L. Alt+Enter — не наш.   K. Ключ asda.UI.EnterConfirm 0 — Enter кнопку не жмёт (как до правки).
//   ★ `asda.SelfTest.EnterConfirmLogin [quit]` — сцена входа (`--map login`): только РАЗБОР цели, без нажатия (нажатие «Войти»
//     шло бы по пути входа): фокус вне полей — «LOGIN_CONFORM» окна LOGIN_WIN; фокус в поле пароля — правило поля (Enter = вход).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaEnterConfirmSubsystem.h"
#include "UI/AsdaEnterGuard.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "AsdaCVarSave.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "Components/EditableText.h"
#include "Engine/Engine.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEnterTest, Log, All);

namespace AsdaEnterConfirmSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.EnterConfirm");   // имя команды (итог, отказ, возврат ключей)
	static const FName kOneBut(TEXT("ONEBUT_MSGBOX"));                // окно-сообщение 1500
	static const FName kTwoBut(TEXT("TWOBUT_MSGBOX"));                // окно-подтверждение 1520
	static const FName kTabWin(TEXT("CHAT_TAB_WIN"));                 // редактор вкладок чата 2590
	static const FName kNumPad(TEXT("NUMPAD_WIN"));                   // окно количества 1410
	static const FName kBox(TEXT("SHELLITEM_PURCHASE_BOX"));          // модалка покупки 1559

	// Контекст прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		bool bQuit = false;                       // выйти после итога
		int32 Ok = 0, Fail = 0;                   // счёт проверок
		FString Phase;                            // раздел (в строках проверок)
		TArray<TFunction<int32(FRun&)>> Steps;    // шаги: тело возвращает, сколько кадров ждать
		int32 Step = 0;                           // текущий шаг
		uint64 WaitUntilFrame = 0;                // ждать до кадра
		double WaitUntilTime = 0.0;               // и до времени
		uint32 CapGen = 0;                        // перехват отправок
		int32 Sent = 0;                           // перехвачено отправок
		int32 Presses0 = 0;                       // счётчик нажатий механизма на начало проверки

		// Проверка: строка журнала и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaEnterTest, Log, TEXT("ENTER ok   [%s] %s"), *Phase, *What); }
			else   { ++Fail; UE_LOG(LogAsdaEnterTest, Error, TEXT("ENTER FAIL [%s] %s"), *Phase, *What); }
		}
		UGameInstance* GI() const { UWorld* X = W.Get(); return X ? X->GetGameInstance() : nullptr; }
		template<typename T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		UAsdaEnterConfirmSubsystem* Enter() const { return Sub<UAsdaEnterConfirmSubsystem>(); }
		UAsdaWindowSubsystem* WM() const { return Sub<UAsdaWindowSubsystem>(); }
		int32 Presses() const { const UAsdaEnterConfirmSubsystem* E = Enter(); return E ? E->GetPressCount() : -1; }
		bool ChatOn() const { const UAsdaChatWindowSubsystem* C = Sub<UAsdaChatWindowSubsystem>(); return C && C->IsInputActive(); }
		bool Visible(FName Id) const { const UAsdaWindowSubsystem* M = WM(); return M && M->IsWindowVisible(Id); }
	};
	static TSharedPtr<FRun> GRun;

	// Счётчики ответов окон. Статические, а не в контексте прогона: колбэк окна, вызванный уже после итога (окно закрыли позже),
	//   не должен писать в освобождённую память.
	struct FCounters
	{
		int32 OkA = 0, OkB = 0, OkC = 0;          // срабатывания ОК окон-сообщений A/B/C
		int32 ConfirmYes = 0, ConfirmNo = 0;      // ответы TWOBUT
		int32 NumOk = 0, NumQty = -1;             // ответ нумпада: сколько раз ОК и введённое количество
		int32 NumCancel = 0;                      // отмены нумпада
		int32 BoxYes = 0, BoxNo = 0;              // ответы 1559
	};
	static FCounters GC;

	// Событие клавиши Enter, как его строит Slate из WM_KEYDOWN/WM_KEYUP VK_RETURN (коды 13/13). bRepeat — автоповтор.
	static FKeyEvent EnterEv(bool bRepeat, bool bAlt = false)
	{
		const FModifierKeysState M(false, false, false, false, bAlt, false, false, false, false);
		return FKeyEvent(EKeys::Enter, M, 0, bRepeat, 13, 13);
	}
	static void Down(bool bRepeat = false, bool bAlt = false)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication::Get().ProcessKeyDownEvent(EnterEv(bRepeat, bAlt));
		if (!bRepeat) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('\r'), FModifierKeysState(), 0, false)); }
	}
	static void Up(bool bAlt = false) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(EnterEv(false, bAlt)); } }
	// Фокус клавиатуры — игровому вьюпорту (игрок щёлкнул по миру).
	static void FocusGame() { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); } }
	// Тип виджета с фокусом клавиатуры (диагностика строк проверок).
	static FString FocusName()
	{
		if (!FSlateApplication::IsInitialized()) { return TEXT("—"); }
		const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return F.IsValid() ? F->GetTypeAsString() : FString(TEXT("нет"));
	}
	// Поле количества нумпада 1412 (щелчок игрока по полю = фокус клавиатуры в нём).
	static UWidget* NumPadField(FRun& R)
	{
		UUserWidget* Root = R.WM() ? R.WM()->FindWindow(kNumPad) : nullptr;
		return Root ? Root->GetWidgetFromName(TEXT("NUMPAD_NUMBOX")) : nullptr;
	}
	// Закрыть строку чата, если открыта.
	static void ChatClose(FRun& R)
	{
		if (UAsdaChatWindowSubsystem* C = R.Sub<UAsdaChatWindowSubsystem>()) { if (C->IsInputActive()) { C->CloseInput(); } }
		FocusGame();
	}
	// Ответить на верхнее окно-сообщение её кнопкой (уборка между разделами).
	static void CloseMsgBoxes(FRun& R)
	{
		UAsdaWindowSubsystem* M = R.WM();
		for (int32 i = 0; M && i < 8 && M->GetMsgBoxQueueNum() > 0; ++i)
		{
			const FName Id = M->IsWindowVisible(kTwoBut) ? kTwoBut : kOneBut;
			UUserWidget* Root = M->FindWindow(Id);
			UButton* B = Root ? Cast<UButton>(Root->GetWidgetFromName(Id == kTwoBut ? TEXT("TWOBUT_BUT01") : TEXT("ONEBUT_BUT00"))) : nullptr;
			if (!B) { break; }
			B->OnClicked.Broadcast();
		}
	}

	// Шаг-тело (и сколько кадров ждать после него).
	static void Add(FRun& R, TFunction<void(FRun&)> Fn, int32 Frames = 3)
	{
		R.Steps.Add([Fn, Frames](FRun& X) -> int32 { Fn(X); return Frames; });
	}
	// Нажатие Enter на разных кадрах: нажатие (+ символ), отпускание.
	static void AddPress(FRun& R, bool bAlt = false)
	{
		Add(R, [bAlt](FRun&) { Down(false, bAlt); }, 2);
		Add(R, [bAlt](FRun&) { Up(bAlt); }, 3);
	}

	// Тикер ядра: шаги по кадрам.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UWorld* W = R.W.Get();
		if (!W)
		{
			AsdaCVarSave::RestoreAll(kCmd);
			GRun.Reset();
			return false;
		}
		if (GFrameCounter < R.WaitUntilFrame || FPlatformTime::Seconds() < R.WaitUntilTime) { return true; }
		if (!R.Steps.IsValidIndex(R.Step))
		{
			AsdaSelfTestKit::DisarmCapture(AsdaSelfTestKit::NetOf(W), R.CapGen);
			const FString Tail = FString::Printf(TEXT(" (перехвачено отправок: %d)"), R.Sent);
			AsdaSelfTestKit::Finish(W, kCmd, R.bQuit, LogAsdaEnterTest, TEXT("ENTER"), R.Ok, R.Fail, *Tail);
			GRun.Reset();
			return false;
		}
		const int32 Frames = FMath::Max(1, R.Steps[R.Step](R));
		++R.Step;
		R.WaitUntilFrame = GFrameCounter + (uint64)Frames;
		R.WaitUntilTime = FPlatformTime::Seconds() + Frames / 30.0;
		return true;
	}

	// Кадров на «окно отстоялось» после показа: ≥ 0,5 с при пороге 400 мс (шаг ждёт и кадры, и кадры/30 с).
	static constexpr int32 kSettle = 16;

	// Показать TWOBUT с ответом в GC.ConfirmYes/No. bNoEnter — как у вызывающих необратимого/входящего.
	static void ShowConfirm(FRun& X, const TCHAR* Text, bool bNoEnter, float GaugeSec = 0.f)
	{
		UAsdaWindowSubsystem* M = X.WM();
		if (!M) { return; }
		if (bNoEnter) { M = AsdaEnterGuard::NoEnter(M); }
		M->ShowGameConfirm(Text, [](bool b) { ++(b ? GC.ConfirmYes : GC.ConfirmNo); }, GaugeSec, 2288, 2289, AsdaMsgBox::kNoKey, 1);
	}
	// Показать ONEBUT с ОК в счётчик Cnt.
	static void ShowMsg(FRun& X, const TCHAR* Text, int32* Cnt)
	{
		if (UAsdaWindowSubsystem* M = X.WM()) { M->ShowGameMessage(Text, 2288, EAsdaMsgCategory::ItemRefused, [Cnt]() { ++*Cnt; }, 0, AsdaMsgBox::kNoKey); }
	}
	// ESC реальной клавишей (события Slate нажатие/отпускание на разных кадрах).
	static void AddEsc(FRun& R)
	{
		Add(R, [](FRun&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); } }, 2);
		Add(R, [](FRun&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); } }, 4);
	}
	// Ответить «нет» кнопкой MESSAGE2 (уборка).
	static void Msg2No(FRun& X)
	{
		UUserWidget* Root = X.WM() ? X.WM()->FindWindow(FName(TEXT("MESSAGE2_WIN"))) : nullptr;
		if (UButton* No = Root ? Cast<UButton>(Root->GetWidgetFromName(TEXT("MESSAGE2_BUT01"))) : nullptr) { No->OnClicked.Broadcast(); }
	}

	static void Build(FRun& R)
	{
		// ── T. Таблица и коды клавиши ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("T таблица");
			ChatClose(X);
			CloseMsgBoxes(X);
			TArray<FString> Lines;
			const int32 Bad = X.Enter() ? X.Enter()->VerifyTableAgainstAssets(Lines) : -1;
			for (const FString& L : Lines) { UE_LOG(LogAsdaEnterTest, Log, TEXT("ENTER таблица: %s"), *L); }
			X.Expect(Bad == 0 && Lines.Num() > 0, FString::Printf(TEXT("таблица окон сверена с ассетами: строк %d, расхождений %d"), Lines.Num(), Bad));
			const FKey K = FInputKeyManager::Get().GetKeyFromCodes(13, 0);
			const FKey K2 = FInputKeyManager::Get().GetKeyFromCodes(13, 13);
			X.Expect(K == EKeys::Enter && K2 == EKeys::Enter,
				FString::Printf(TEXT("VK_RETURN (13) → «%s», (13, CR) → «%s»: Enter цифрового блока у Win32 = тот же VK_RETURN"),
					*K.ToString(), *K2.ToString()));
		});

		// ── S. Опасные окна в таблицу Enter не попали (ВАЖНО-1/2/4в) ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("S опасные окна");
			static const TCHAR* const kDanger[] = {
				TEXT("CLAN_DELETE_BOX"), TEXT("CLAN_INCREASE_BOX"), TEXT("CLAN_MAKING_WIN"), TEXT("Disassembly_Win"), TEXT("Constellation_S_WIN"),
				TEXT("NPCSHOP_WIN"), TEXT("WARE_WIN"), TEXT("EXCHANGE_WIN"), TEXT("TRAN_WIN"), TEXT("StreetStall_Win"), TEXT("Smelting3_Win"),
				TEXT("Item_Upgrade_Win"), TEXT("Item_Initializ_Win"), TEXT("ItemOptionValue_WIN"), TEXT("Get_UpgradeOtion"), TEXT("EXTRACT_WIN"),
				TEXT("ITEM_making_R_BOX"), TEXT("ITEM_making_Mark_WIN"), TEXT("Pet_Hatchery_Win"), TEXT("Pet_Evolution_Win"), TEXT("Pet_LimitBreak_Win"),
				TEXT("Package_Contents_Win"), TEXT("Primium_Item_post_WIN"), TEXT("Premium_Pick_select"), TEXT("BOOSTER_SHOP_WIN"), TEXT("HAIRSHOP_WIN"),
				TEXT("SHELLITEM_PURCHASE_BOX"), TEXT("ICON_mssageBOX"), TEXT("SOWELL_SELBOX"), TEXT("GIFT_MSGBOX"), TEXT("FISH_BOOK_WIN"),
				TEXT("Hunt_report_Win"), TEXT("Town_as_WIN"), TEXT("PARTYBOX_WIN"), TEXT("PARTYDISPERSION"), TEXT("SMate_ReqAnsr_Win") };
			TArray<FString> In;
			for (const TCHAR* Id : kDanger) { if (UAsdaEnterConfirmSubsystem::IsWindowInTable(FName(Id))) { In.Add(Id); } }
			X.Expect(In.Num() == 0, FString::Printf(TEXT("%d опасных/входящих окон — ни одного в таблице Enter%s"), (int32)UE_ARRAY_COUNT(kDanger),
				In.Num() ? *(FString(TEXT(": ЕСТЬ ")) + FString::Join(In, TEXT(", "))) : TEXT("")));
		});

		// ── A. Контроль: окон нет — Enter открывает строку чата ──
		Add(R, [](FRun& X) { X.Phase = TEXT("A контроль"); ChatClose(X); X.Presses0 = X.Presses(); }, 3);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(X.ChatOn() && X.Presses() == X.Presses0, FString::Printf(TEXT("окон нет: Enter → строка чата %s, нажатий механизма +%d (фокус «%s»)"),
				X.ChatOn() ? TEXT("открыта") : TEXT("НЕ открыта"), X.Presses() - X.Presses0, *FocusName()));
			ChatClose(X);
		});

		// ── W. 400 мс: только что показанное окно Enter не подтверждает ──
		Add(R, [](FRun&) {}, 2 * kSettle);   // строка чата контроля закрыта ≥ 0,5 с: модалка ниже не «всплыла во время набора»
		Add(R, [](FRun& X) { X.Phase = TEXT("W 400 мс"); GC.OkA = 0; X.Presses0 = X.Presses(); ShowMsg(X, TEXT("Проверка Enter: сразу"), &GC.OkA); }, 2);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.OkA == 0 && X.Visible(kOneBut) && !X.ChatOn(), FString::Printf(TEXT("Enter в первые 400 мс: ОК %d, окно %s — съеден, ничего не нажато"),
				GC.OkA, X.Visible(kOneBut) ? TEXT("на месте") : TEXT("ЗАКРЫТО")));
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.OkA == 1 && !X.Visible(kOneBut), FString::Printf(TEXT("через 0,5 с Enter = ОК: %d"), GC.OkA));
			CloseMsgBoxes(X); ChatClose(X);
		});

		// ── B. ONEBUT: действие на отпускании, ровно одно ──
		Add(R, [](FRun& X) { X.Phase = TEXT("B ONEBUT 1500"); GC.OkA = 0; X.Presses0 = X.Presses(); ShowMsg(X, TEXT("Проверка Enter: ОК"), &GC.OkA); }, kSettle);
		Add(R, [](FRun& X) { X.Expect(X.Visible(kOneBut), TEXT("ONEBUT показано")); Down(); }, 3);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.OkA == 0 && X.Visible(kOneBut), FString::Printf(TEXT("после нажатия (до отпускания) ОК не нажат: срабатываний %d"), GC.OkA));
			Up();
		}, 3);
		Add(R, [](FRun& X)
		{
			const UAsdaEnterConfirmSubsystem* E = X.Enter();
			X.Expect(GC.OkA == 1 && !X.Visible(kOneBut) && !X.ChatOn() && X.Presses() == X.Presses0 + 1
				&& E && E->GetLastPressedWindow() == kOneBut && E->GetLastPressedButton() == FName(TEXT("ONEBUT_BUT00")),
				FString::Printf(TEXT("отпускание: ОК %d раз, окно %s, чат %s"), GC.OkA, X.Visible(kOneBut) ? TEXT("НЕ закрыто") : TEXT("закрыто"),
					X.ChatOn() ? TEXT("ОТКРЫТ") : TEXT("не открыт")));
			ChatClose(X);
		});

		// ── C. TWOBUT обычный вопрос: «ОК/Да» ──
		Add(R, [](FRun& X) { X.Phase = TEXT("C TWOBUT 1520"); GC.ConfirmYes = GC.ConfirmNo = 0; ShowConfirm(X, TEXT("Проверка Enter: да?"), false); }, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 1 && GC.ConfirmNo == 0 && !X.Visible(kTwoBut), FString::Printf(TEXT("Enter = «ОК»: да %d, нет %d"), GC.ConfirmYes, GC.ConfirmNo));
			ChatClose(X);
		});

		// ── X. Вопрос «без Enter» (необратимое / входящее от сервера): Enter НЕ подтверждает ──
		Add(R, [](FRun& X) { X.Phase = TEXT("X вопрос без Enter"); GC.ConfirmYes = GC.ConfirmNo = 0; X.Presses0 = X.Presses(); ShowConfirm(X, TEXT("Проверка Enter: удалить?"), true); }, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 0 && GC.ConfirmNo == 0 && X.Visible(kTwoBut) && X.Presses() == X.Presses0,
				FString::Printf(TEXT("TWOBUT «без Enter»: Enter ничего не нажал (да %d, нет %d, +%d), окно на месте"), GC.ConfirmYes, GC.ConfirmNo, X.Presses() - X.Presses0));
			CloseMsgBoxes(X);
		}, 4);
		Add(R, [](FRun& X)
		{
			GC.ConfirmYes = GC.ConfirmNo = 0; X.Presses0 = X.Presses();
			if (UAsdaWindowSubsystem* M = X.WM()) { AsdaEnterGuard::NoEnter(M)->ShowConfirm(TEXT("Проверка Enter: удалить персонажа?"), [](bool b) { ++(b ? GC.ConfirmYes : GC.ConfirmNo); }); }
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 0 && X.Visible(FName(TEXT("MESSAGE2_WIN"))) && X.Presses() == X.Presses0,
				FString::Printf(TEXT("MESSAGE2 «без Enter» (удаление персонажа): Enter ничего не нажал (да %d, +%d)"), GC.ConfirmYes, X.Presses() - X.Presses0));
			Msg2No(X);
		}, 4);
		Add(R, [](FRun& X)
		{
			GC.ConfirmYes = GC.ConfirmNo = 0;
			if (UAsdaWindowSubsystem* M = X.WM()) { M->ShowConfirm(TEXT("Проверка Enter: обычный MESSAGE2"), [](bool b) { ++(b ? GC.ConfirmYes : GC.ConfirmNo); }); }
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 1 && !X.Visible(FName(TEXT("MESSAGE2_WIN"))), FString::Printf(TEXT("MESSAGE2 без пометки: Enter = «Да» (%d) — пометка не «залипла»"), GC.ConfirmYes));
			if (X.Visible(FName(TEXT("MESSAGE2_WIN")))) { Msg2No(X); }
			ChatClose(X);
		});

		// ── D. Недоступная кнопка (полоса-гейт): Enter съеден, после заливки — нажимается ──
		Add(R, [](FRun& X) { X.Phase = TEXT("D недоступная кнопка"); GC.ConfirmYes = GC.ConfirmNo = 0; X.Presses0 = X.Presses(); ShowConfirm(X, TEXT("Проверка Enter: гейт"), false, 1.5f); }, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 0 && GC.ConfirmNo == 0 && X.Visible(kTwoBut) && X.Presses() == X.Presses0 && !X.ChatOn(),
				FString::Printf(TEXT("«ОК» недоступна: ничего не нажато (да %d, +%d), окно на месте"), GC.ConfirmYes, X.Presses() - X.Presses0));
		}, 60);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 1 && !X.Visible(kTwoBut), FString::Printf(TEXT("после заливки полосы Enter нажал «ОК»: да %d"), GC.ConfirmYes));
			CloseMsgBoxes(X); ChatClose(X);
		});

		// ── Q. Подмена вопроса во время удержания (ВАЖНО-4а): то же окно TWOBUT, новая запись — не подтверждается ──
		Add(R, [](FRun& X) { X.Phase = TEXT("Q подмена при удержании"); GC.ConfirmYes = GC.ConfirmNo = 0; ShowConfirm(X, TEXT("Проверка Enter: вопрос А"), false); }, kSettle);
		Add(R, [](FRun&) { Down(); }, 3);
		Add(R, [](FRun& X) { ShowConfirm(X, TEXT("Проверка Enter: вопрос Б от сервера"), false); }, 3);   // показ в ТОМ ЖЕ окне 1520
		for (int32 i = 0; i < 4; ++i) { Add(R, [](FRun&) { Down(/*bRepeat*/true); }, 2); }
		Add(R, [](FRun&) { Up(); }, 4);
		Add(R, [](FRun& X)
		{
			const int32 Q = X.WM() ? X.WM()->GetMsgBoxQueueNum() : -1;
			X.Expect(GC.ConfirmYes == 0 && GC.ConfirmNo == 0 && Q == 2 && X.Visible(kTwoBut),
				FString::Printf(TEXT("подменённый во время удержания вопрос не подтверждён: да %d, нет %d, в очереди %d"), GC.ConfirmYes, GC.ConfirmNo, Q));
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			const int32 Q = X.WM() ? X.WM()->GetMsgBoxQueueNum() : -1;
			X.Expect(GC.ConfirmYes == 1 && Q == 1, FString::Printf(TEXT("новое нажатие после показа Б: да %d, в очереди %d"), GC.ConfirmYes, Q));
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 2 && !X.Visible(kTwoBut), FString::Printf(TEXT("и вопрос А — своим нажатием: да %d"), GC.ConfirmYes));
			CloseMsgBoxes(X); ChatClose(X);
		});

		// ── M. Модалка во время набора в чате (ВАЖНО-4б): Enter её НЕ жмёт ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("M модалка при наборе в чате");
			GC.ConfirmYes = GC.ConfirmNo = 0; X.Presses0 = X.Presses();
			if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { C->OpenInput(67); }
		}, 4);
		Add(R, [](FRun& X) { X.Expect(X.ChatOn(), TEXT("строка чата открыта")); ShowConfirm(X, TEXT("Проверка Enter: вопрос во время набора"), false); }, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 0 && GC.ConfirmNo == 0 && X.Visible(kTwoBut) && X.Presses() == X.Presses0,
				FString::Printf(TEXT("строка чата открыта: Enter вопрос не подтвердил (да %d, +%d), окно на месте (фокус «%s»)"),
					GC.ConfirmYes, X.Presses() - X.Presses0, *FocusName()));
			ChatClose(X);
		}, 4);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 0 && X.Visible(kTwoBut), FString::Printf(TEXT("вопрос, всплывший во время набора, Enter не жмёт и после закрытия чата (да %d) — только щелчок"), GC.ConfirmYes));
			CloseMsgBoxes(X); ChatClose(X);
		}, 4);
		Add(R, [](FRun& X) { GC.ConfirmYes = GC.ConfirmNo = 0; ShowConfirm(X, TEXT("Проверка Enter: вопрос без чата"), false); }, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmYes == 1 && !X.Visible(kTwoBut), FString::Printf(TEXT("следующий вопрос, показанный без чата: Enter = «ОК» (%d)"), GC.ConfirmYes));
			CloseMsgBoxes(X); ChatClose(X);
		});

		// ── E. Удержание: два окна в очереди — закрыто ровно одно ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("E удержание");
			GC.OkA = GC.OkB = 0;
			ShowMsg(X, TEXT("Проверка Enter: окно А"), &GC.OkA);
			ShowMsg(X, TEXT("Проверка Enter: окно Б"), &GC.OkB);
		}, kSettle);
		Add(R, [](FRun& X) { X.Expect(X.WM() && X.WM()->GetMsgBoxQueueNum() == 2, TEXT("в очереди два окна-сообщения")); Down(); }, 2);
		for (int32 i = 0; i < 6; ++i) { Add(R, [](FRun&) { Down(/*bRepeat*/true); }, 2); }
		Add(R, [](FRun&) { Up(); }, 3);
		Add(R, [](FRun& X)
		{
			const int32 Q = X.WM() ? X.WM()->GetMsgBoxQueueNum() : -1;
			X.Expect(GC.OkB == 1 && GC.OkA == 0 && Q == 1 && X.Visible(kOneBut),
				FString::Printf(TEXT("Enter с 6 автоповторами: Б нажато %d, А %d, в очереди %d — одно нажатие = одно действие"), GC.OkB, GC.OkA, Q));
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			const int32 Q = X.WM() ? X.WM()->GetMsgBoxQueueNum() : -1;
			X.Expect(GC.OkA == 1 && Q == 0, FString::Printf(TEXT("новое нажатие закрыло окно А: %d, очередь %d"), GC.OkA, Q));
			ChatClose(X);
		});

		// ── F. Окно появилось во время удержания — не подтверждается ──
		Add(R, [](FRun& X) { X.Phase = TEXT("F окно во время удержания"); GC.OkC = 0; ChatClose(X); Down(); }, 3);
		Add(R, [](FRun& X) { ShowMsg(X, TEXT("Проверка Enter: окно В"), &GC.OkC); }, kSettle);
		for (int32 i = 0; i < 4; ++i) { Add(R, [](FRun&) { Down(/*bRepeat*/true); }, 2); }
		Add(R, [](FRun&) { Up(); }, 4);
		Add(R, [](FRun& X) { X.Expect(GC.OkC == 0 && X.Visible(kOneBut), FString::Printf(TEXT("удержание, начатое до окна, его не подтвердило: ОК %d"), GC.OkC)); });
		AddPress(R);
		Add(R, [](FRun& X) { X.Expect(GC.OkC == 1 && !X.Visible(kOneBut), FString::Printf(TEXT("новое нажатие подтвердило окно В: %d"), GC.OkC)); CloseMsgBoxes(X); ChatClose(X); });

		// ── Z. ESC реальной клавишей под модалкой (СРЕДН-1): TWOBUT → «Отмена», ONEBUT — съеден ──
		Add(R, [](FRun& X) { X.Phase = TEXT("Z ESC под модалкой"); GC.ConfirmYes = GC.ConfirmNo = 0; ShowConfirm(X, TEXT("Проверка ESC"), false); }, kSettle);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.ConfirmNo == 1 && GC.ConfirmYes == 0 && !X.Visible(kTwoBut),
				FString::Printf(TEXT("ESC под TWOBUT = «Отмена» 1524 (эталон): нет %d, да %d, окно %s"), GC.ConfirmNo, GC.ConfirmYes,
					X.Visible(kTwoBut) ? TEXT("НЕ закрыто") : TEXT("закрыто")));
			GC.OkA = 0;
			ShowMsg(X, TEXT("Проверка ESC: сообщение"), &GC.OkA);
		}, kSettle);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.OkA == 0 && X.Visible(kOneBut), FString::Printf(TEXT("ESC под ONEBUT съеден и ничего не закрыл (эталон: 1500): ОК %d, окно %s"),
				GC.OkA, X.Visible(kOneBut) ? TEXT("на месте") : TEXT("ЗАКРЫТО")));
			CloseMsgBoxes(X); ChatClose(X);
		});

		// ── G. Модальное поверх окна ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("G модальное поверх окна");
			if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { C->ShowTabEditor(true); }
		}, kSettle);
		Add(R, [](FRun& X)
		{
			X.Expect(X.Visible(kTabWin), TEXT("редактор вкладок чата 2590 показан"));
			GC.ConfirmYes = GC.ConfirmNo = 0;
			ShowConfirm(X, TEXT("Проверка Enter: поверх окна"), false);
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			const UAsdaEnterConfirmSubsystem* E = X.Enter();
			X.Expect(GC.ConfirmYes == 1 && X.Visible(kTabWin) && E && E->GetLastPressedWindow() == kTwoBut,
				FString::Printf(TEXT("Enter нажал модальное TWOBUT (да %d), окно 2590 под ним цело"), GC.ConfirmYes));
			FocusGame();
		});
		AddPress(R);
		Add(R, [](FRun& X)
		{
			const UAsdaEnterConfirmSubsystem* E = X.Enter();
			X.Expect(!X.Visible(kTabWin) && E && E->GetLastPressedButton() == FName(TEXT("CHAT_TAB_WIN_OK")) && !X.ChatOn(),
				TEXT("следующий Enter — «ОК» окна 2590, чат не открыт"));
			ChatClose(X);
		});

		// ── H. Строка чата открыта при окне ──
		Add(R, [](FRun& X) { X.Phase = TEXT("H чат при окне"); if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { C->ShowTabEditor(true); } }, kSettle);
		Add(R, [](FRun& X) { X.Presses0 = X.Presses(); if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { C->OpenInput(67); } }, 4);
		Add(R, [](FRun& X) { X.Expect(X.ChatOn() && X.Visible(kTabWin), TEXT("строка чата открыта поверх окна 2590")); }, 2);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(X.Presses() == X.Presses0 && X.Visible(kTabWin), FString::Printf(TEXT("Enter достался чату, «ОК» 2590 не нажат (+%d)"), X.Presses() - X.Presses0));
			ChatClose(X);
		});

		// ── N. Недоступная кнопка немодального окна — Enter прежним путём (строка чата) ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("N недоступная, немодальное");
			X.Presses0 = X.Presses();
			UUserWidget* Root = X.WM() ? X.WM()->FindWindow(kTabWin) : nullptr;
			UButton* B = Root ? Cast<UButton>(Root->GetWidgetFromName(TEXT("CHAT_TAB_WIN_OK"))) : nullptr;
			X.Expect(B != nullptr && X.Visible(kTabWin), TEXT("окно 2590 и кнопка «ОК» на месте"));
			if (B) { B->SetIsEnabled(false); }   // имитация «кнопка сейчас недоступна»
			FocusGame();
		}, 2);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(X.Presses() == X.Presses0 && X.Visible(kTabWin) && X.ChatOn(),
				FString::Printf(TEXT("недоступная «ОК»: не нажата (+%d), Enter — в строку чата (%s)"), X.Presses() - X.Presses0, X.ChatOn() ? TEXT("открыта") : TEXT("НЕ открыта")));
			ChatClose(X);
			UUserWidget* Root = X.WM() ? X.WM()->FindWindow(kTabWin) : nullptr;
			if (UButton* B = Root ? Cast<UButton>(Root->GetWidgetFromName(TEXT("CHAT_TAB_WIN_OK"))) : nullptr) { B->SetIsEnabled(true); }
			if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { C->HideTabEditor(); }
		});

		// ── I. Нумпад: правило поля и кнопка «ОК» ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("I нумпад 1410");
			GC.NumOk = GC.NumCancel = 0; GC.NumQty = -1; X.Presses0 = X.Presses();
			if (UAsdaWindowSubsystem* M = X.WM()) { M->ShowNumPad(50, [](bool b, int32 Q) { if (b) { ++GC.NumOk; GC.NumQty = Q; } else { ++GC.NumCancel; } }, /*bCenter*/true); }
		}, kSettle);
		Add(R, [](FRun& X)
		{
			UWidget* F = NumPadField(X);
			const FString Was = FocusName();
			// ★ 25.10.2026 (`asda.UI.NumPadFocus`): фокус в поле ставит само окно при открытии, как эталон — щелчок по полю не нужен.
			X.Expect(X.Visible(kNumPad) && F && F->HasKeyboardFocus(), FString::Printf(TEXT("нумпад показан, фокус в поле 1412 без щелчка (фокус у «%s»)"), *Was));
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('7'), FModifierKeysState(), 0, false)); }
		}, 3);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.NumOk == 1 && GC.NumQty == 7 && X.Presses() == X.Presses0 && !X.Visible(kNumPad),
				FString::Printf(TEXT("фокус в поле: Enter по правилу поля — ОК %d, количество %d, механизм не вмешался (+%d)"), GC.NumOk, GC.NumQty, X.Presses() - X.Presses0));
			GC.NumOk = GC.NumCancel = 0; GC.NumQty = -1; X.Presses0 = X.Presses();
			if (UAsdaWindowSubsystem* M = X.WM()) { M->ShowNumPad(50, [](bool b, int32 Q) { if (b) { ++GC.NumOk; GC.NumQty = Q; } else { ++GC.NumCancel; } }, /*bCenter*/true); }
		}, kSettle);
		Add(R, [](FRun& X)
		{
			if (UWidget* F = NumPadField(X)) { if (!F->HasKeyboardFocus()) { F->SetKeyboardFocus(); } }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('5'), FModifierKeysState(), 0, false)); }
		}, 2);
		Add(R, [](FRun&) { FocusGame(); }, 2);   // игрок щёлкнул мимо поля — фокус ушёл
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.NumOk == 1 && GC.NumQty == 5 && X.Presses() == X.Presses0 + 1,
				FString::Printf(TEXT("фокус вне поля: Enter = «ОК» 1415 — ОК %d, количество %d"), GC.NumOk, GC.NumQty));
			if (X.WM() && X.WM()->IsNumPadOpen()) { X.WM()->CloseNumPad(false); }
			ChatClose(X);
		});

		// ── P. Модалка покупки 1559 (премиум): Enter НЕ подтверждает ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("P покупка 1559");
			GC.BoxYes = GC.BoxNo = 0; X.Presses0 = X.Presses();
			if (UAsdaPurchaseBoxSubsystem* B = X.Sub<UAsdaPurchaseBoxSubsystem>())
			{
				FAsdaPurchaseBoxRequest Req;
				Req.Mode = EAsdaPurchaseMode::HairShop;
				Req.ItemCode = 76452;   // существующий код (как у самотеста замка модалки); в сеть ничего не уходит
				Req.Quantity = 1;
				Req.TradeMode = 1;
				B->Open(Req, [](bool b) { ++(b ? GC.BoxYes : GC.BoxNo); });
			}
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			const UAsdaPurchaseBoxSubsystem* B = X.Sub<UAsdaPurchaseBoxSubsystem>();
			X.Expect(GC.BoxYes == 0 && B && B->IsOpen() && X.Presses() == X.Presses0,
				FString::Printf(TEXT("1559 (трата премиум-валюты): Enter не подтвердил (да %d, +%d), окно на месте"), GC.BoxYes, X.Presses() - X.Presses0));
			if (UAsdaPurchaseBoxSubsystem* B2 = X.Sub<UAsdaPurchaseBoxSubsystem>()) { if (B2->IsOpen()) { B2->Close(); } }
			ChatClose(X);
		});

		// ── L. Alt+Enter — не наш ──
		Add(R, [](FRun& X) { X.Phase = TEXT("L Alt+Enter"); GC.OkA = 0; ShowMsg(X, TEXT("Проверка Enter: Alt"), &GC.OkA); }, kSettle);
		AddPress(R, /*bAlt*/true);
		Add(R, [](FRun& X) { X.Expect(GC.OkA == 0 && X.Visible(kOneBut), FString::Printf(TEXT("Alt+Enter окно не подтвердил: %d"), GC.OkA)); });
		AddPress(R);
		Add(R, [](FRun& X) { X.Expect(GC.OkA == 1 && !X.Visible(kOneBut), FString::Printf(TEXT("простой Enter подтвердил: %d"), GC.OkA)); CloseMsgBoxes(X); });

		// ── K. Ключ отката ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("K ключ 0");
			AsdaCVarSave::SetForTest(TEXT("asda.UI.EnterConfirm"), 0);
			GC.OkA = 0; X.Presses0 = X.Presses();
			ShowMsg(X, TEXT("Проверка Enter: ключ 0"), &GC.OkA);
		}, kSettle);
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.OkA == 0 && X.Visible(kOneBut) && X.Presses() == X.Presses0, FString::Printf(TEXT("ключ 0: Enter окно не подтвердил (%d)"), GC.OkA));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.EnterConfirm"), 1);
		});
		AddPress(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.OkA == 1 && !X.Visible(kOneBut), FString::Printf(TEXT("ключ 1: Enter подтвердил (%d)"), GC.OkA));
			CloseMsgBoxes(X);
			ChatClose(X);
		});
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		if (!W || !W->GetGameInstance()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		if (!W->GetGameInstance()->GetSubsystem<UAsdaEnterConfirmSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы Enter")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.EnterConfirm"), 1);
		// ★ 25.10.2026 (REVIEW_FOCUS Ф-4): раздел I ждёт фокус в поле 1412 без щелчка — ключ фокуса окна количества на время прогона.
		AsdaCVarSave::SetForTest(TEXT("asda.UI.NumPadFocus"), 1);
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.W = W;
		R.bQuit = Args.bQuit;
		TWeakPtr<FRun> Weak = GRun;
		R.CapGen = AsdaSelfTestKit::ArmCapture(AsdaSelfTestKit::NetOf(W),
			[Weak](uint16, const uint8*, int32, bool, TCHAR) { if (TSharedPtr<FRun> P = Weak.Pin()) { ++P->Sent; } }, 0, kCmd);
		// Свой персонаж офлайн — HUD, строка чата и окна мира строятся только в мире с персонажем (как у самотеста замка модалки).
		if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			if (UAsdaWorldSubsystem* WS = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
			}
		}
		Build(R);
		R.WaitUntilTime = FPlatformTime::Seconds() + 2.5;   // первый шаг — после построения HUD и окон
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.f);
	}
	// ══════ Сцена входа: разбор цели без нажатия ══════
	static const TCHAR* kCmdLogin = TEXT("asda.SelfTest.EnterConfirmLogin");   // имя команды сцены входа
	static void RunLogin(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmdLogin, Args)) { return; }
		if (!W || !W->GetGameInstance() || !AsdaWorldKind::IsLoginScene(W)) { AsdaSelfTestKit::Refused(W, kCmdLogin, Args.bQuit, TEXT("запускать на сцене входа")); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = Args.bQuit;
		// Экран входа собирается не сразу — проверка через 4 с.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
		{
			UWorld* W2 = WW.Get();
			if (!W2) { return false; }
			FRun R;
			R.W = W2;
			R.Phase = TEXT("вход");
			UAsdaEnterConfirmSubsystem* E = R.Enter();
			UUserWidget* Login = R.WM() ? R.WM()->FindWindow(FName(TEXT("LOGIN_WIN"))) : nullptr;
			R.Expect(E && Login && R.Visible(FName(TEXT("LOGIN_WIN"))), TEXT("окно LOGIN_WIN показано"));
			if (E && Login)
			{
				FocusGame();
				UAsdaEnterConfirmSubsystem::FTarget T = E->ResolveTarget();
				R.Expect(T.Verdict == UAsdaEnterConfirmSubsystem::EVerdict::Press && T.WindowId == FName(TEXT("LOGIN_WIN")) && T.ButtonName == FName(TEXT("LOGIN_CONFORM")),
					FString::Printf(TEXT("фокус вне полей: Enter → %s (%s)"), UAsdaEnterConfirmSubsystem::VerdictName(T.Verdict), *T.Why));
				if (UEditableText* Pw = Cast<UEditableText>(Login->GetWidgetFromName(TEXT("LOGIN_EDIT01")))) { Pw->SetKeyboardFocus(); }
				T = E->ResolveTarget();
				R.Expect(T.Verdict == UAsdaEnterConfirmSubsystem::EVerdict::TextField,
					FString::Printf(TEXT("фокус в поле пароля: Enter → %s (%s) — правило поля «Enter в пароле = вход»"), UAsdaEnterConfirmSubsystem::VerdictName(T.Verdict), *T.Why));
				FocusGame();
				UE_LOG(LogAsdaEnterTest, Log, TEXT("%s"), *E->DumpState());
			}
			AsdaSelfTestKit::Finish(W2, kCmdLogin, bQuit, LogAsdaEnterTest, TEXT("ENTER-ВХОД"), R.Ok, R.Fail);
			return false;
		}), 4.f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEnterConfirm(
	TEXT("asda.SelfTest.EnterConfirm"),
	TEXT("Самотест: Enter нажимает кнопку согласия верхнего окна (модального — первым), на отпускании, одно нажатие — одно действие; "
	     "поле ввода, строка чата, недоступная кнопка, удержание, Alt+Enter, ключ отката. Мир, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaEnterConfirmSelfTest::Run(A, W);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEnterConfirmLogin(
	TEXT("asda.SelfTest.EnterConfirmLogin"),
	TEXT("Самотест сцены входа: Enter при фокусе вне полей → «LOGIN_CONFORM», в поле пароля — правило поля (только разбор цели). `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaEnterConfirmSelfTest::RunLogin(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
