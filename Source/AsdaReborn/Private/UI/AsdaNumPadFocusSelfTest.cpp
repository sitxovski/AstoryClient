// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ФОКУС ПОЛЯ 1412 ОКНА КОЛИЧЕСТВА» (25.10.2026): `asda.SelfTest.NumPadFocus [quit]`. Мир, только офлайн (`--map world`).
//   Правка — `UAsdaWindowSubsystem::ShowNumPad` / `RefreshModalInput` (ключ `asda.UI.NumPadFocus`), ESC — `UAsdaEnterConfirmSubsystem`.
//   Эталон: `uiOpenContextPopupWithHandlers@0xDA55A0` ставит фокус полю 1412 (ui_core.c:61849), курсор поля с фокусом после смены
//   текста — в конец (`CControl_SetText_12F9850`, EM_SETSEL 500,500), ESC из поля идёт менеджеру окон (`CWindowMgr_QueueKeyUp`) → отмена.
//   Путь игрока: клавиши — настоящие события Slate (`ProcessKeyDownEvent` / `ProcessKeyCharEvent` / `ProcessKeyUpEvent`, как от
//   WM_KEYDOWN/WM_CHAR/WM_KEYUP), мышь — `UAsdaWindowSubsystem::SimulateClickAt` (нажатие и отпускание Slate на разных кадрах)
//   по живой геометрии. Окно — общая точка `ShowNumPad`, через которую идут все 17 мест открытия (магазин, склад, обмен, лавка,
//   аукцион, книга рыбака, пожертвование, отчёт охоты, инвентарь, GM-окна). Итог: «NUMPAD ИТОГ: ok=N fail=M».
//   Разделы:
//     A. Открытие по центру: фокус в поле БЕЗ щелчка; цифры с клавиатуры — в поле, мир клавишу не видит, чат не открыт; Enter = ОК;
//        после закрытия фокус — игровому вьюпорту, ввод мира возвращён.
//     B. ESC в поле с фокусом — отмена (колбэк «Отмена», окно закрыто), фокус миру.
//     C. Курсор после программной смены текста — в конце (кламп 940 → 500, Backspace → 50); буква не вставляется и курсор не двигает.
//     D. Открытие у курсора, строгий максимум (книга рыбака, режим 10): фокус в поле, кламп 25 → 20, Enter = ОК 20.
//     E. Строка чата открыта, затем окно: цифры — в поле, не в чат; Enter — ОК, чат после закрытия не открыт.
//     F. Пересчёт модалок при открытом окне (`RefreshModalInputNow`) фокус поля не отбирает.
//     G. Мышь: щелчок по кнопке «1» — цифра дописана, поле фокус теряет (как у эталона: обойдённый контрол — SetFocus(0));
//        клавиатура тогда в поле не пишет; щелчок по полю — фокус вернулся, набор продолжается; Enter = ОК.
//     H. Ключ `asda.UI.NumPadFocus 0` — прежнее поведение (фокус не в поле), ESC проходом — отмена.
//     I. Всплывашка улова 71400: кнопка 71403 на экране отсутствует (её группа 71402 скрыта .JUI, эталон её не ищет и не показывает),
//        под её местом мышь кнопку не находит; Enter её не жмёт.
//   ★ Окно ввода текста 1580 (поле 1583; ключи `asda.UI.InputDialogFocus` [ОРИГИНАЛ], `asda.UI.InputDialogEsc` [ИСПРАВЛЕНИЕ]):
//     J. Открытие с предзаполнением: фокус в поле без щелчка, курсор в конце (набор дописывает), мир клавишу не видит; Enter = ОК
//        с текстом; фокус миру. Журнал без «Attempting to focus Non-Focusable widget» (`asda.UI.ModalFocusNoRoot`).
//     K. ESC из поля — «Отмена»; ESC вне поля (игрок щёлкнул мимо) — тоже «Отмена».
//     L. Пересчёт модалок фокус поля не отбирает; щелчок мышью по «ОК» — ОК с набранным текстом.
//     M. Путь игрока из меню вкладок чата 2670: щелчок мышью по «Изменить заголовок» (2674) → 1580 с именем вкладки, фокус в поле.
//     N. Ключи 0: `InputDialogEsc 0` — ESC окно не закрывает (как оригинал); `InputDialogFocus 0` — фокус не в поле (как до правки).
//   ★ Ревизия REVIEW_FOCUS (Ф-1, Ф-5):
//     P. Нумпад поверх модального контрола 74200 книги рыбака: Enter в поле — ровно одно закрытие (одна строка «окно 1410 закрыто»,
//        звук 6 — в той же функции), колбэк открывает новое окно 1410 — оно не закрывается само и получает фокус.
//     Q. То же для окна ввода текста 1580 поверх 74200.
//     R. Alt+Z: при открытом нумпаде не срабатывает (гейт эталона «модальное окно»), набор идёт; нумпад, открытый при скрытом
//        интерфейсе, показывает интерфейс — фокус и набор только в видимом окне.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEnterConfirmSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaFishWindowSubsystem.h"
#include "UI/AsdaFishBookSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "AsdaCVarSave.h"
#include "World/AsdaWorldSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Widgets/SViewport.h"
#include "Misc/OutputDevice.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaNumPadTest, Log, All);

namespace AsdaNumPadFocusSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.NumPadFocus");   // имя команды (итог, отказ, возврат ключей)
	static const FName kNumPad(TEXT("NUMPAD_WIN"));                  // окно количества 1410
	static const FName kFish(TEXT("FISH_WIN"));                      // окно рыбалки 71300
	static constexpr int32 kSettle = 16;                             // кадров «окно отстоялось» (≥ 0,5 с: порог Enter 400 мс)
	static constexpr int32 kFishItem = 69577;                        // код рыбы (как у `asda.Fish.WinSelfTest`, F4)

	// Контекст прогона (тот же устройство, что у `asda.SelfTest.EnterConfirm`).
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		bool bQuit = false;                       // выйти после итога
		int32 Ok = 0, Fail = 0;                   // счёт проверок
		FString Phase;                            // раздел
		TArray<TFunction<int32(FRun&)>> Steps;    // шаги: тело возвращает, сколько кадров ждать
		int32 Step = 0;                           // текущий шаг
		uint64 WaitUntilFrame = 0;                // ждать до кадра
		double WaitUntilTime = 0.0;               // и до времени
		uint32 CapGen = 0;                        // перехват отправок
		int32 Sent = 0;                           // перехвачено отправок
		int32 Presses0 = 0;                       // счётчик нажатий механизма Enter на начало проверки

		// Проверка: строка журнала и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaNumPadTest, Log, TEXT("NUMPAD ok   [%s] %s"), *Phase, *What); }
			else   { ++Fail; UE_LOG(LogAsdaNumPadTest, Error, TEXT("NUMPAD FAIL [%s] %s"), *Phase, *What); }
		}
		UGameInstance* GI() const { UWorld* X = W.Get(); return X ? X->GetGameInstance() : nullptr; }
		template<typename T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		UAsdaWindowSubsystem* WM() const { return Sub<UAsdaWindowSubsystem>(); }
		APlayerController* PC() const { UGameInstance* G = GI(); return G ? G->GetFirstLocalPlayerController(G->GetWorld()) : nullptr; }
		int32 Presses() const { const UAsdaEnterConfirmSubsystem* E = Sub<UAsdaEnterConfirmSubsystem>(); return E ? E->GetPressCount() : -1; }
		bool ChatOn() const { const UAsdaChatWindowSubsystem* C = Sub<UAsdaChatWindowSubsystem>(); return C && C->IsInputActive(); }
		bool Open() const { const UAsdaWindowSubsystem* M = WM(); return M && M->IsNumPadOpen(); }
	};
	static TSharedPtr<FRun> GRun;

	// Ответы окна количества. Статические: колбэк, пришедший после итога, не пишет в освобождённую память.
	struct FCounters
	{
		int32 Ok = 0;        // сколько раз «ОК»
		int32 Cancel = 0;    // сколько раз «Отмена»
		int32 Qty = -1;      // последнее подтверждённое количество
	};
	static FCounters GC;
	static bool GChatBeforeEnter = false;   // раздел E: открыта ли строка чата перед Enter окна количества

	// Ответы окна ввода текста 1580 (статические — по той же причине, что GC).
	struct FInputCounters
	{
		int32 Ok = 0;        // сколько раз «ОК»
		int32 Cancel = 0;    // сколько раз «Отмена»
		FString Text;        // текст последнего ответа
	};
	static FInputCounters GI1580;
	static const FName kInputWin(TEXT("SHOUTING_WIN"));   // окно ввода текста 1580
	static int32 GNonFocusErrors = 0;                     // строк «Attempting to focus Non-Focusable widget» за прогон
	static int32 GPadCloses = 0;                          // строк «окно 1410 закрыто» (P)
	static int32 GInputCloses = 0;                        // строк «окно 1580 закрыто» (Q)
	static int32 GSecondOk = 0, GSecondCancel = 0;        // ответы ВТОРОГО окна, открытого колбэком первого (P/Q)

	// Ловец строки журнала об ошибке фокуса нефокусируемого корня (раздел J).
	class FNonFocusLogCatcher : public FOutputDevice
	{
	public:
		virtual bool CanBeUsedOnAnyThread() const override { return true; }   // строки любого потока (вместе со строкой ниже)
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			if (!V) { return; }
			if (FCString::Strstr(V, TEXT("Attempting to focus Non-Focusable widget"))) { ++GNonFocusErrors; }
			if (FCString::Strstr(V, TEXT("[количество] окно 1410 закрыто:"))) { ++GPadCloses; }
			if (FCString::Strstr(V, TEXT("[ввод] окно 1580 закрыто:"))) { ++GInputCloses; }
		}
	};
	static FNonFocusLogCatcher GNonFocusCatcher;

	// Поле 1412 открытого окна.
	static UEditableText* Field(FRun& R)
	{
		UUserWidget* Root = R.WM() ? R.WM()->FindWindow(kNumPad) : nullptr;
		return Root ? Cast<UEditableText>(Root->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))) : nullptr;
	}
	static FString FieldText(FRun& R) { UEditableText* F = Field(R); return F ? F->GetText().ToString() : FString(TEXT("<нет поля>")); }
	// Тип виджета с фокусом клавиатуры (строки проверок).
	static FString FocusName()
	{
		if (!FSlateApplication::IsInitialized()) { return TEXT("—"); }
		const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return F.IsValid() ? F->GetTypeAsString() : FString(TEXT("нет"));
	}
	// Фокус клавиатуры — в поле 1412.
	static bool FieldFocused(FRun& R) { UEditableText* F = Field(R); return F && F->HasKeyboardFocus(); }
	// Фокус клавиатуры — у игрового вьюпорта (мир получает ввод).
	static bool ViewportFocused()
	{
		if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) { return false; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		return Vp.IsValid() && FSlateApplication::Get().GetKeyboardFocusedWidget() == Vp;
	}
	// Ввод мира свободен (модалка его не держит, вьюпорт клавиши принимает).
	static bool WorldInputFree(FRun& R)
	{
		const UAsdaWindowSubsystem* M = R.WM();
		return M && !M->IsWorldInputCapturedByModal() && GEngine && GEngine->GameViewport && !GEngine->GameViewport->IgnoreInput();
	}

	// Клавиша с символом, как её строит Slate из WM_KEYDOWN + WM_CHAR + WM_KEYUP. Vk — виртуальный код, Ch — символ (0 — без WM_CHAR).
	static void KeyDown(const FKey& K, uint32 Vk, TCHAR Ch)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K, FModifierKeysState(), 0, false, (uint32)Ch, Vk));
		if (Ch) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Ch, FModifierKeysState(), 0, false)); }
	}
	static void KeyUp(const FKey& K, uint32 Vk, TCHAR Ch)
	{
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K, FModifierKeysState(), 0, false, (uint32)Ch, Vk)); }
	}
	// Цифра или буква целиком (нажатие, символ, отпускание).
	static void TypeChar(TCHAR Ch)
	{
		const uint32 Vk = (uint32)FChar::ToUpper(Ch);   // VK_0…VK_9 = '0'…'9', VK_A… = 'A'…
		const FKey K = FInputKeyManager::Get().GetKeyFromCodes(Vk, (uint32)Ch);
		KeyDown(K, Vk, Ch);
		KeyUp(K, Vk, Ch);
	}
	static void TypeStr(const TCHAR* S) { for (const TCHAR* P = S; *P; ++P) { TypeChar(*P); } }
	static void PressHome()      { KeyDown(EKeys::Home, 0x24, 0); KeyUp(EKeys::Home, 0x24, 0); }
	static void PressBackspace() { KeyDown(EKeys::BackSpace, 0x08, TEXT('\b')); KeyUp(EKeys::BackSpace, 0x08, TEXT('\b')); }

	// Шаг-тело (и сколько кадров ждать после него).
	static void Add(FRun& R, TFunction<void(FRun&)> Fn, int32 Frames = 3)
	{
		R.Steps.Add([Fn, Frames](FRun& X) -> int32 { Fn(X); return Frames; });
	}
	// Enter нажатием и отпусканием на разных кадрах (VK_RETURN 13 / символ CR).
	static void AddEnter(FRun& R)
	{
		Add(R, [](FRun&) { KeyDown(EKeys::Enter, 13, TEXT('\r')); }, 2);
		Add(R, [](FRun&) { KeyUp(EKeys::Enter, 13, TEXT('\r')); }, 3);
	}
	// ESC нажатием и отпусканием на разных кадрах (VK_ESCAPE 27).
	static void AddEsc(FRun& R)
	{
		Add(R, [](FRun&) { KeyDown(EKeys::Escape, 27, 0); }, 2);
		Add(R, [](FRun&) { KeyUp(EKeys::Escape, 27, 0); }, 4);
	}
	// Открыть окно количества (по центру — `bCenter`, иначе у курсора) с ответом в GC.
	static void ShowPad(FRun& X, int32 Max, bool bCenter = true, bool bStrict = false)
	{
		GC = FCounters();
		if (UAsdaWindowSubsystem* M = X.WM())
		{
			M->ShowNumPad(Max, [](bool b, int32 Q) { if (b) { ++GC.Ok; GC.Qty = Q; } else { ++GC.Cancel; } }, bCenter, bStrict);
		}
	}
	// Проверка «открылось с фокусом в поле без щелчка».
	static void ExpectFocusOnOpen(FRun& X, const TCHAR* Where)
	{
		X.Expect(X.Open() && FieldFocused(X) && FieldText(X).IsEmpty(),
			FString::Printf(TEXT("%s: окно открыто, фокус клавиатуры в поле 1412 без щелчка (фокус у «%s»), поле пустое"), Where, *FocusName()));
	}
	// Поле 1583 открытого окна 1580.
	static UEditableText* InputFieldOf(FRun& R)
	{
		UUserWidget* Root = R.WM() ? R.WM()->FindWindow(kInputWin) : nullptr;
		return Root ? Cast<UEditableText>(Root->GetWidgetFromName(TEXT("Shouting_InputBox"))) : nullptr;
	}
	static FString InputText(FRun& R) { UEditableText* F = InputFieldOf(R); return F ? F->GetText().ToString() : FString(TEXT("<нет поля>")); }
	static bool InputFocused(FRun& R) { UEditableText* F = InputFieldOf(R); return F && F->HasKeyboardFocus(); }
	static bool InputOpen(FRun& R) { const UAsdaWindowSubsystem* M = R.WM(); return M && M->IsWindowVisible(kInputWin); }
	// Открыть окно 1580 с ответом в GI1580.
	static void ShowInput(FRun& X, const TCHAR* Initial)
	{
		GI1580 = FInputCounters();
		if (UAsdaWindowSubsystem* M = X.WM())
		{
			M->ShowInputDialog(TEXT("Проверка ввода"), TEXT("Подсказка"), Initial, 20,
				[](bool b, const FString& T) { if (b) { ++GI1580.Ok; } else { ++GI1580.Cancel; } GI1580.Text = T; });
		}
	}

	// Уборка: окно закрыть «Отменой», фокус миру.
	static void Cleanup(FRun& X)
	{
		if (UAsdaWindowSubsystem* M = X.WM()) { if (M->IsWindowVisible(kInputWin)) { M->CloseInputDialog(false); } }
		if (UAsdaWindowSubsystem* M = X.WM()) { if (M->IsNumPadOpen()) { M->CloseNumPad(false); } }
		if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { if (C->IsInputActive()) { C->CloseInput(); } }
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
	}
	// Центр живого прямоугольника виджета (абсолютные координаты Slate).
	static bool CenterOf(const UWidget* W, FVector2D& Out)
	{
		FVector2D A, B;
		if (!W || !AsdaUi::WidgetAbsRect(W, A, B) || (B - A).IsNearlyZero()) { return false; }
		Out = (A + B) * 0.5;
		return true;
	}

	// Тикер ядра: шаги по кадрам.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UWorld* W = R.W.Get();
		if (!W)
		{
			if (GLog) { GLog->RemoveOutputDevice(&GNonFocusCatcher); }   // прогон оборван — ловца журнала не оставляем
			AsdaCVarSave::RestoreAll(kCmd);
			GRun.Reset();
			return false;
		}
		if (GFrameCounter < R.WaitUntilFrame || FPlatformTime::Seconds() < R.WaitUntilTime) { return true; }
		if (!R.Steps.IsValidIndex(R.Step))
		{
			AsdaSelfTestKit::DisarmCapture(AsdaSelfTestKit::NetOf(W), R.CapGen);
			const FString Tail = FString::Printf(TEXT(" (перехвачено отправок: %d)"), R.Sent);
			AsdaSelfTestKit::Finish(W, kCmd, R.bQuit, LogAsdaNumPadTest, TEXT("NUMPAD"), R.Ok, R.Fail, *Tail);
			GRun.Reset();
			return false;
		}
		const int32 Frames = FMath::Max(1, R.Steps[R.Step](R));
		++R.Step;
		R.WaitUntilFrame = GFrameCounter + (uint64)Frames;
		R.WaitUntilTime = FPlatformTime::Seconds() + Frames / 30.0;
		return true;
	}

	static void Build(FRun& R)
	{
		// ── A. Открытие по центру: фокус, цифры, Enter, возврат фокуса миру ──
		Add(R, [](FRun& X) { X.Phase = TEXT("A центр"); Cleanup(X); X.Presses0 = X.Presses(); ShowPad(X, 50); }, kSettle);
		Add(R, [](FRun& X)
		{
			ExpectFocusOnOpen(X, TEXT("центр экрана (режим 6)"));
			X.Expect(X.WM() && X.WM()->IsWorldInputCapturedByModal() && GEngine && GEngine->GameViewport && GEngine->GameViewport->IgnoreInput(),
				TEXT("ввод мира забран модалкой: вьюпорт клавиши игнорирует"));
			KeyDown(EKeys::One, '1', TEXT('1'));   // «1» нажата и держится
		}, 3);
		Add(R, [](FRun& X)
		{
			const APlayerController* P = X.PC();
			X.Expect(FieldText(X) == TEXT("1") && P && !P->IsInputKeyDown(EKeys::One) && !X.ChatOn(),
				FString::Printf(TEXT("«1» с клавиатуры — в поле («%s»), мир клавишу не видит (зажата в мире: %s), чат %s"),
					*FieldText(X), (P && P->IsInputKeyDown(EKeys::One)) ? TEXT("ДА") : TEXT("нет"), X.ChatOn() ? TEXT("ОТКРЫТ") : TEXT("закрыт")));
			KeyUp(EKeys::One, '1', TEXT('1'));
			TypeChar(TEXT('2'));
		}, 3);
		Add(R, [](FRun& X) { X.Expect(FieldText(X) == TEXT("12"), FString::Printf(TEXT("«2» — в поле: «%s»"), *FieldText(X))); });
		AddEnter(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.Ok == 1 && GC.Qty == 12 && GC.Cancel == 0 && !X.Open(),
				FString::Printf(TEXT("Enter в поле = ОК: ОК %d, количество %d, отмен %d, окно %s"), GC.Ok, GC.Qty, GC.Cancel, X.Open() ? TEXT("ОТКРЫТО") : TEXT("закрыто")));
			X.Expect(ViewportFocused() && WorldInputFree(X) && !X.ChatOn(),
				FString::Printf(TEXT("после закрытия фокус — игровому вьюпорту («%s»), ввод мира свободен, чат не открыт Enter-ом"), *FocusName()));
		});

		// ── B. ESC в поле с фокусом — отмена ──
		Add(R, [](FRun& X) { X.Phase = TEXT("B ESC"); Cleanup(X); ShowPad(X, 50); }, kSettle);
		Add(R, [](FRun& X) { ExpectFocusOnOpen(X, TEXT("повторное открытие")); TypeChar(TEXT('3')); });
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.Cancel == 1 && GC.Ok == 0 && !X.Open() && ViewportFocused() && WorldInputFree(X),
				FString::Printf(TEXT("ESC в поле = отмена: отмен %d, ОК %d, окно %s, фокус «%s»"), GC.Cancel, GC.Ok, X.Open() ? TEXT("ОТКРЫТО") : TEXT("закрыто"), *FocusName()));
		});

		// ── C. Курсор в конце после смены текста; буква не вставляется ──
		Add(R, [](FRun& X) { X.Phase = TEXT("C курсор"); Cleanup(X); ShowPad(X, 500); }, kSettle);
		Add(R, [](FRun& X)
		{
			TypeStr(TEXT("40"));
			PressHome();          // курсор в начало
			TypeChar(TEXT('9'));  // «940» > 500 → поле режется в «500»
		});
		Add(R, [](FRun& X)
		{
			const FString S1 = FieldText(X);
			PressBackspace();     // курсор в конце → «50» (в середине стёр бы «5» → «00»)
			X.Expect(S1 == TEXT("500") && FieldText(X) == TEXT("50"),
				FString::Printf(TEXT("кламп «940» → «%s», Backspace → «%s»: курсор после смены текста — в конце (EM_SETSEL 500,500)"), *S1, *FieldText(X)));
			PressHome();
			TypeChar(TEXT('a'));  // буква — не вставляется, курсор на месте
			TypeChar(TEXT('1'));  // «1» в начало → «150»
		});
		Add(R, [](FRun& X)
		{
			X.Expect(FieldText(X) == TEXT("150"), FString::Printf(TEXT("буква в числовое поле не попала, курсор не сдвинулся: «%s» (ждём «150»)"), *FieldText(X)));
		});
		AddEsc(R);
		Add(R, [](FRun& X) { X.Expect(GC.Cancel == 1 && !X.Open(), TEXT("ESC — отмена")); });

		// ── D. У курсора, строгий максимум (книга рыбака) ──
		Add(R, [](FRun& X) { X.Phase = TEXT("D у курсора, строгий"); Cleanup(X); ShowPad(X, 20, /*bCenter*/false, /*bStrict*/true); }, kSettle);
		Add(R, [](FRun& X) { ExpectFocusOnOpen(X, TEXT("у курсора, строгий максимум 20")); TypeStr(TEXT("25")); });
		Add(R, [](FRun& X) { X.Expect(FieldText(X) == TEXT("20"), FString::Printf(TEXT("«25» > 20 → «%s»"), *FieldText(X))); });
		AddEnter(R);
		Add(R, [](FRun& X) { X.Expect(GC.Ok == 1 && GC.Qty == 20 && !X.Open() && ViewportFocused(), FString::Printf(TEXT("Enter = ОК %d, количество %d"), GC.Ok, GC.Qty)); });

		// ── E. Строка чата открыта, затем окно количества ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("E чат");
			Cleanup(X);
			if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { C->OpenInput(0); }
		}, 6);
		Add(R, [](FRun& X)
		{
			X.Expect(X.ChatOn(), TEXT("строка чата открыта до окна"));
			ShowPad(X, 50);
		}, kSettle);
		Add(R, [](FRun& X)
		{
			ExpectFocusOnOpen(X, TEXT("поверх открытой строки чата"));
			TypeStr(TEXT("42"));
		});
		Add(R, [](FRun& X)
		{
			X.Expect(FieldText(X) == TEXT("42"), FString::Printf(TEXT("цифры — в поле, не в чат: поле «%s», строка чата %s"), *FieldText(X), X.ChatOn() ? TEXT("открыта") : TEXT("закрыта")));
			GChatBeforeEnter = X.ChatOn();
		});
		AddEnter(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.Ok == 1 && GC.Qty == 42 && !X.Open(), FString::Printf(TEXT("Enter = ОК %d, количество %d"), GC.Ok, GC.Qty));
		}, 6);
		Add(R, [](FRun& X)
		{
			X.Expect(X.ChatOn() == GChatBeforeEnter, FString::Printf(TEXT("Enter окна не переключил строку чата вторым действием (до %s, после %s)"),
				GChatBeforeEnter ? TEXT("открыта") : TEXT("закрыта"), X.ChatOn() ? TEXT("открыта") : TEXT("закрыта")));
			Cleanup(X);
		});

		// ── F. Пересчёт модалок при открытом окне ──
		Add(R, [](FRun& X) { X.Phase = TEXT("F пересчёт модалок"); Cleanup(X); ShowPad(X, 99); }, kSettle);
		Add(R, [](FRun& X) { TypeChar(TEXT('5')); if (X.WM()) { X.WM()->RefreshModalInputNow(); } }, 4);
		Add(R, [](FRun& X)
		{
			X.Expect(FieldFocused(X), FString::Printf(TEXT("после `RefreshModalInput` фокус у поля (фокус «%s»)"), *FocusName()));
			TypeChar(TEXT('6'));
		});
		Add(R, [](FRun& X) { X.Expect(FieldText(X) == TEXT("56"), FString::Printf(TEXT("набор продолжается: «%s»"), *FieldText(X))); });
		AddEsc(R);
		Add(R, [](FRun& X) { X.Expect(GC.Cancel == 1 && !X.Open(), TEXT("ESC — отмена")); });

		// ── G. Мышь: кнопка «1», затем щелчок по полю ──
		Add(R, [](FRun& X) { X.Phase = TEXT("G мышь"); Cleanup(X); ShowPad(X, 50); }, kSettle);
		Add(R, [](FRun& X)
		{
			UUserWidget* Root = X.WM() ? X.WM()->FindWindow(kNumPad) : nullptr;
			FVector2D P(0.0, 0.0);
			const bool bOk = Root && CenterOf(Root->GetWidgetFromName(TEXT("NUMPAD_NO00")), P);   // idnum 1401 = «1»
			X.Expect(bOk, FString::Printf(TEXT("кнопка «1» (NUMPAD_NO00) на экране в (%.0f,%.0f)"), P.X, P.Y));
			if (bOk) { X.WM()->SimulateClickAt(P, /*HoldFrames*/ 3); }
		}, 10);
		Add(R, [](FRun& X)
		{
			X.Expect(FieldText(X) == TEXT("1") && !FieldFocused(X),
				FString::Printf(TEXT("щелчок мышью по «1»: поле «%s», поле фокус потеряло (фокус «%s») — как у эталона"), *FieldText(X), *FocusName()));
			TypeChar(TEXT('7'));   // клавиатура — не в поле
		});
		Add(R, [](FRun& X)
		{
			X.Expect(FieldText(X) == TEXT("1") && X.Open() && GC.Ok == 0, FString::Printf(TEXT("без фокуса клавиатура в поле не пишет: «%s»"), *FieldText(X)));
			FVector2D P(0.0, 0.0);
			const bool bOk = CenterOf(Field(X), P);
			X.Expect(bOk, FString::Printf(TEXT("поле 1412 на экране в (%.0f,%.0f)"), P.X, P.Y));
			if (bOk) { X.WM()->SimulateClickAt(P, /*HoldFrames*/ 3); }
		}, 10);
		Add(R, [](FRun& X)
		{
			X.Expect(FieldFocused(X), FString::Printf(TEXT("щелчок по полю — фокус в поле (фокус «%s»)"), *FocusName()));
			TypeChar(TEXT('2'));
		});
		Add(R, [](FRun& X) { X.Expect(FieldText(X) == TEXT("12"), FString::Printf(TEXT("набор с клавиатуры после щелчка: «%s»"), *FieldText(X))); });
		AddEnter(R);
		Add(R, [](FRun& X) { X.Expect(GC.Ok == 1 && GC.Qty == 12 && !X.Open() && ViewportFocused(), FString::Printf(TEXT("Enter = ОК %d, количество %d, фокус «%s»"), GC.Ok, GC.Qty, *FocusName())); });

		// ── H. Ключ 0 — прежнее поведение ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("H ключ 0");
			Cleanup(X);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.NumPadFocus"), 0);
			ShowPad(X, 50);
		}, kSettle);
		Add(R, [](FRun& X)
		{
			X.Expect(X.Open() && !FieldFocused(X), FString::Printf(TEXT("ключ 0: фокус не в поле, как до правки (фокус «%s»)"), *FocusName()));
		});
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.Cancel == 1 && !X.Open(), TEXT("ключ 0: ESC проходом окон — отмена"));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.NumPadFocus"), 1);
			Cleanup(X);
		});

		// ── I. Всплывашка улова 71400: кнопка 71403 ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("I всплывашка улова");
			Cleanup(X);
			if (UAsdaFishWindowSubsystem* FW = X.Sub<UAsdaFishWindowSubsystem>()) { FW->FishWinShow(); FW->ShowCatchPopup(kFishItem); }
		}, kSettle);
		Add(R, [](FRun& X)
		{
			UAsdaFishWindowSubsystem* FW = X.Sub<UAsdaFishWindowSubsystem>();
			UUserWidget* Root = X.WM() ? X.WM()->FindWindow(kFish) : nullptr;
			UWidget* Grp = Root ? Root->GetWidgetFromName(TEXT("FISH_icon_Messagebox_grp")) : nullptr;
			UWidget* Bb = Root ? Root->GetWidgetFromName(TEXT("FISH_icon_Messagebox_bb_grp")) : nullptr;
			UButton* Btn = Root ? Cast<UButton>(Root->GetWidgetFromName(TEXT("FISH_icon_Messagebox_okbtn"))) : nullptr;
			X.Expect(FW && FW->IsCatchPopupShown() && Grp && Bb && Btn, TEXT("всплывашка улова 71400 показана, узлы 71402/71403 в окне есть"));
			X.Expect(Bb && Bb->GetVisibility() == ESlateVisibility::Collapsed && !AsdaUi::IsShown(Bb),
				FString::Printf(TEXT("группа кнопки 71402 скрыта (.JUI Visible=\"0\"; эталон 71402/71403 не ищет и не показывает): %s"),
					Bb ? *UEnum::GetValueAsString(Bb->GetVisibility()) : TEXT("—")));
			// Мышь под местом кнопки (рамка 71400 + 166+3, 102+3 + полкнопки): путь Slate кнопку не содержит.
			FVector2D A, B;
			bool bHitBtn = false;
			if (Grp && Btn && AsdaUi::WidgetAbsRect(Grp, A, B) && FSlateApplication::IsInitialized())
			{
				const FVector2D Sc((B.X - A.X) / 430.0, (B.Y - A.Y) / 140.0);
				const FVector2D Pt(A.X + (169.0 + 50.0) * Sc.X, A.Y + (105.0 + 11.0) * Sc.Y);
				FSlateApplication& App = FSlateApplication::Get();
				const FWidgetPath Path = App.LocateWindowUnderMouse(Pt, App.GetInteractiveTopLevelWindows(), false, 0);
				const TSharedPtr<SWidget> BtnW = Btn->GetCachedWidget();
				for (int32 i = 0; i < Path.Widgets.Num(); ++i) { if (BtnW.IsValid() && Path.Widgets[i].Widget == BtnW) { bHitBtn = true; } }
				X.Expect(!bHitBtn, FString::Printf(TEXT("в точке кнопки (%.0f,%.0f) мышь кнопку 71403 не находит (узлов пути %d) — нажать нечего, как у оригинала"),
					Pt.X, Pt.Y, Path.Widgets.Num()));
			}
			else { X.Expect(false, TEXT("геометрия всплывашки не снята")); }
			X.Expect(!UAsdaEnterConfirmSubsystem::IsWindowInTable(kFish), TEXT("окна рыбалки нет в таблице Enter (кнопки согласия у всплывашки нет)"));
			X.Presses0 = X.Presses();
		});
		AddEnter(R);
		Add(R, [](FRun& X)
		{
			UAsdaFishWindowSubsystem* FW = X.Sub<UAsdaFishWindowSubsystem>();
			X.Expect(X.Presses() == X.Presses0 && FW && FW->IsCatchPopupShown(),
				FString::Printf(TEXT("Enter кнопок окна не жмёт (+%d), всплывашка на месте (её снимает автомат через 3 с, FSM 120 → 121)"), X.Presses() - X.Presses0));
			if (FW) { FW->HideCatchPopup(); FW->PanelHideAndClear(); }
			if (X.WM()) { X.WM()->SetWindowVisible(kFish, false); }
			Cleanup(X);
		});
	}

	// Разделы окна ввода текста 1580.
	static void BuildInput(FRun& R)
	{
		// ── J. Открытие, курсор в конце, Enter = ОК ──
		Add(R, [](FRun& X) { X.Phase = TEXT("J ввод 1580"); Cleanup(X); GNonFocusErrors = 0; ShowInput(X, TEXT("abc")); }, kSettle);
		Add(R, [](FRun& X)
		{
			X.Expect(InputOpen(X) && InputFocused(X) && InputText(X) == TEXT("abc"),
				FString::Printf(TEXT("окно 1580 открыто, фокус в поле 1583 без щелчка (фокус «%s»), предзаполнено «%s»"), *FocusName(), *InputText(X)));
			X.Expect(GNonFocusErrors == 0, FString::Printf(TEXT("в журнале нет «Attempting to focus Non-Focusable widget» (строк %d)"), GNonFocusErrors));
			KeyDown(EKeys::D, 'D', TEXT('d'));   // «d» нажата и держится
		}, 3);
		Add(R, [](FRun& X)
		{
			const APlayerController* P = X.PC();
			X.Expect(InputText(X) == TEXT("abcd") && P && !P->IsInputKeyDown(EKeys::D) && !X.ChatOn(),
				FString::Printf(TEXT("«d» дописана в конец: «%s» (курсор в конце, EM_SETSEL 500,500), мир клавишу не видит, чат %s"),
					*InputText(X), X.ChatOn() ? TEXT("ОТКРЫТ") : TEXT("закрыт")));
			KeyUp(EKeys::D, 'D', TEXT('d'));
		});
		AddEnter(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GI1580.Ok == 1 && GI1580.Text == TEXT("abcd") && !InputOpen(X),
				FString::Printf(TEXT("Enter в поле = ОК (правило окна): ОК %d, текст «%s»"), GI1580.Ok, *GI1580.Text));
			X.Expect(ViewportFocused() && WorldInputFree(X) && !X.ChatOn(),
				FString::Printf(TEXT("после закрытия фокус — игровому вьюпорту («%s»), ввод мира свободен, чат не открыт"), *FocusName()));
		});

		// ── K. ESC из поля и вне поля ──
		Add(R, [](FRun& X) { X.Phase = TEXT("K ESC 1580"); Cleanup(X); ShowInput(X, TEXT("")); }, kSettle);
		Add(R, [](FRun& X) { X.Expect(InputFocused(X), TEXT("фокус в поле (пустое поле)")); TypeStr(TEXT("xy")); });
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GI1580.Cancel == 1 && GI1580.Ok == 0 && !InputOpen(X) && ViewportFocused(),
				FString::Printf(TEXT("ESC из поля = «Отмена» [ИСПРАВЛЕНИЕ]: отмен %d, ОК %d, фокус «%s»"), GI1580.Cancel, GI1580.Ok, *FocusName()));
			ShowInput(X, TEXT("q"));
		}, kSettle);
		Add(R, [](FRun&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); } });   // щелчок мимо поля
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GI1580.Cancel == 1 && !InputOpen(X), FString::Printf(TEXT("ESC вне поля = «Отмена»: отмен %d"), GI1580.Cancel));
		});

		// ── L. Пересчёт модалок, щелчок мышью по «ОК» ──
		Add(R, [](FRun& X) { X.Phase = TEXT("L мышь 1580"); Cleanup(X); ShowInput(X, TEXT("")); }, kSettle);
		Add(R, [](FRun& X) { TypeStr(TEXT("ab")); if (X.WM()) { X.WM()->RefreshModalInputNow(); } }, 4);
		Add(R, [](FRun& X)
		{
			X.Expect(InputFocused(X), FString::Printf(TEXT("после `RefreshModalInput` фокус у поля (фокус «%s»)"), *FocusName()));
			TypeChar(TEXT('c'));
		});
		Add(R, [](FRun& X)
		{
			X.Expect(InputText(X) == TEXT("abc"), FString::Printf(TEXT("набор продолжается: «%s»"), *InputText(X)));
			UUserWidget* Root = X.WM() ? X.WM()->FindWindow(kInputWin) : nullptr;
			FVector2D P(0.0, 0.0);
			const bool bOk = Root && CenterOf(Root->GetWidgetFromName(TEXT("Shouting_OKBtn")), P);
			X.Expect(bOk, FString::Printf(TEXT("кнопка «ОК» 1584 на экране в (%.0f,%.0f)"), P.X, P.Y));
			if (bOk) { X.WM()->SimulateClickAt(P, /*HoldFrames*/ 3); }
		}, 10);
		Add(R, [](FRun& X)
		{
			X.Expect(GI1580.Ok == 1 && GI1580.Text == TEXT("abc") && !InputOpen(X) && ViewportFocused(),
				FString::Printf(TEXT("щелчок мышью по «ОК»: ОК %d, текст «%s», фокус «%s»"), GI1580.Ok, *GI1580.Text, *FocusName()));
		});

		// ── M. Путь игрока: меню вкладок чата 2670 → «Изменить заголовок» 2674 ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("M меню вкладок 2670");
			Cleanup(X);
			if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { C->DebugShowTabMenu(); }
		}, 8);
		Add(R, [](FRun& X)
		{
			const UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>();
			UWidget* Btn = nullptr;   // плашка-кнопка «Изменить заголовок» живого меню этого мира (меню строит код, клик — опрос курсора)
			for (TObjectIterator<UWidget> It; It; ++It)
			{
				if (It->GetFName() == FName(TEXT("CHAT_SETUP_T4")) && It->GetWorld() == X.W.Get() && It->GetCachedWidget().IsValid() && AsdaUi::IsShown(*It)) { Btn = *It; break; }
			}
			FVector2D P(0.0, 0.0);
			const bool bOk = C && C->IsTabMenuShown() && Btn && CenterOf(Btn, P);
			X.Expect(bOk, FString::Printf(TEXT("меню 2670 показано, «Изменить заголовок» (CHAT_SETUP_T4) на экране в (%.0f,%.0f)"), P.X, P.Y));
			if (bOk) { X.WM()->SimulateClickAt(P, /*HoldFrames*/ 3); }
		}, kSettle);
		Add(R, [](FRun& X)
		{
			const FString Was = InputText(X);
			X.Expect(InputOpen(X) && InputFocused(X) && !Was.IsEmpty(),
				FString::Printf(TEXT("щелчок мышью по пункту меню → окно 1580 «%s», фокус в поле (фокус «%s») без щелчка по полю"), *Was, *FocusName()));
			TypeChar(TEXT('z'));
			GI1580.Text = Was;   // имя вкладки до набора (уборка ниже — «Отмена», имя не меняется)
		});
		Add(R, [](FRun& X)
		{
			X.Expect(InputText(X) == GI1580.Text + TEXT("z"), FString::Printf(TEXT("набор с клавиатуры дописан в конец имени: «%s»"), *InputText(X)));
		});
		AddEsc(R);
		Add(R, [](FRun& X) { X.Expect(!InputOpen(X), TEXT("ESC — «Отмена», имя вкладки не тронуто")); Cleanup(X); });

		// ── N. Ключи 0 ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("N ключи 0");
			Cleanup(X);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.InputDialogEsc"), 0);
			ShowInput(X, TEXT(""));
		}, kSettle);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(InputOpen(X) && GI1580.Cancel == 0, TEXT("InputDialogEsc 0: ESC окно 1580 не закрыл — как оригинал (признака «ESC закрывает» у 1580 нет)"));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.InputDialogEsc"), 1);
			Cleanup(X);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.InputDialogFocus"), 0);
			ShowInput(X, TEXT(""));
		}, kSettle);
		Add(R, [](FRun& X)
		{
			X.Expect(InputOpen(X) && !InputFocused(X), FString::Printf(TEXT("InputDialogFocus 0: фокус не в поле, как до правки (фокус «%s»)"), *FocusName()));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.InputDialogFocus"), 1);
			Cleanup(X);
		});
	}

	// Разделы ревизии REVIEW_FOCUS: поверх модального контрола 74200 и Alt+Z.
	static void BuildReview(FRun& R)
	{
		// ── P. Нумпад поверх 74200 ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("P нумпад поверх 74200");
			Cleanup(X);
			if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>()) { B->Open(); }
		}, 30);
		Add(R, [](FRun& X) { if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>()) { B->DebugOpenJoin(0); } }, 6);
		Add(R, [](FRun& X)
		{
			const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			X.Expect(B && B->IsJoinOpen() && X.WM() && X.WM()->IsAnyModalOpen(), TEXT("окно регистрации 74200 открыто (модальный контрол книги)"));
			GPadCloses = 0; GSecondOk = GSecondCancel = 0;
			GC = FCounters();
			if (UAsdaWindowSubsystem* M = X.WM())
			{
				TWeakObjectPtr<UAsdaWindowSubsystem> WeakM = M;
				M->ShowNumPad(10, [WeakM](bool b, int32 Q)
				{
					if (b) { ++GC.Ok; GC.Qty = Q; } else { ++GC.Cancel; }
					// Колбэк первого окна открывает новое 1410 — оно не должно закрыться само.
					if (UAsdaWindowSubsystem* M2 = WeakM.Get()) { M2->ShowNumPad(10, [](bool b2, int32) { ++(b2 ? GSecondOk : GSecondCancel); }, true); }
				}, /*bCenter*/false, /*bStrict*/true);
			}
		}, kSettle);
		Add(R, [](FRun& X) { ExpectFocusOnOpen(X, TEXT("нумпад поверх 74200")); TypeChar(TEXT('3')); });
		AddEnter(R);
		Add(R, [](FRun&) {}, 8);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.Ok == 1 && GC.Qty == 3 && GC.Cancel == 0 && GPadCloses == 1,
				FString::Printf(TEXT("Enter в поле: ОК %d (количество %d), отмен %d, закрытий окна 1410 %d — ровно одно (один звук 6)"), GC.Ok, GC.Qty, GC.Cancel, GPadCloses));
			X.Expect(X.Open() && GSecondCancel == 0 && GSecondOk == 0 && FieldFocused(X) && FieldText(X).IsEmpty(),
				FString::Printf(TEXT("новое окно 1410 из колбэка открыто, само не закрылось (отмен %d), фокус в поле (фокус «%s»)"), GSecondCancel, *FocusName()));
			const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			X.Expect(B && B->IsJoinOpen(), TEXT("74200 под нумпадом на месте"));
			if (X.WM()) { X.WM()->CloseNumPad(false); }
		}, 4);
		Add(R, [](FRun& X)
		{
			X.Expect(GSecondCancel == 1 && GPadCloses == 2, FString::Printf(TEXT("новое окно закрыто один раз (отмен %d, закрытий всего %d)"), GSecondCancel, GPadCloses));
		});

		// ── Q. Окно 1580 поверх 74200 ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("Q ввод 1580 поверх 74200");
			const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			X.Expect(B && B->IsJoinOpen(), TEXT("74200 открыто"));
			GInputCloses = 0; GSecondOk = GSecondCancel = 0;
			GI1580 = FInputCounters();
			if (UAsdaWindowSubsystem* M = X.WM())
			{
				TWeakObjectPtr<UAsdaWindowSubsystem> WeakM = M;
				M->ShowInputDialog(TEXT("Проверка"), TEXT("поверх 74200"), TEXT("a"), 20, [WeakM](bool b, const FString& T)
				{
					if (b) { ++GI1580.Ok; } else { ++GI1580.Cancel; }
					GI1580.Text = T;
					if (UAsdaWindowSubsystem* M2 = WeakM.Get())
					{
						M2->ShowInputDialog(TEXT("Второе"), TEXT(""), TEXT(""), 20, [](bool b2, const FString&) { ++(b2 ? GSecondOk : GSecondCancel); });
					}
				});
			}
		}, kSettle);
		Add(R, [](FRun& X) { X.Expect(InputFocused(X), FString::Printf(TEXT("фокус в поле 1583 (фокус «%s»)"), *FocusName())); TypeChar(TEXT('b')); });
		AddEnter(R);
		Add(R, [](FRun&) {}, 8);
		Add(R, [](FRun& X)
		{
			X.Expect(GI1580.Ok == 1 && GI1580.Text == TEXT("ab") && GI1580.Cancel == 0 && GInputCloses == 1,
				FString::Printf(TEXT("Enter в поле: ОК %d («%s»), отмен %d, закрытий окна 1580 %d — ровно одно"), GI1580.Ok, *GI1580.Text, GI1580.Cancel, GInputCloses));
			X.Expect(InputOpen(X) && GSecondCancel == 0 && GSecondOk == 0 && InputFocused(X),
				FString::Printf(TEXT("новое окно 1580 из колбэка открыто, само не закрылось (отмен %d), фокус в поле (фокус «%s»)"), GSecondCancel, *FocusName()));
			if (X.WM()) { X.WM()->CloseInputDialog(false); }
			if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>()) { B->CloseJoin(); B->CloseOrToggle(); }
		}, 6);
		Add(R, [](FRun& X)
		{
			const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			X.Expect(B && !B->IsOpen() && X.WM() && !X.WM()->IsAnyModalOpen(), TEXT("уборка: книга закрыта, модалок нет"));
			Cleanup(X);
		});

		// ── R. Alt+Z ──
		Add(R, [](FRun& X) { X.Phase = TEXT("R Alt+Z"); Cleanup(X); ShowPad(X, 50); }, kSettle);
		Add(R, [](FRun&)
		{
			// Alt+Z — настоящими событиями Slate (нажатие с Alt, отпускание).
			const FModifierKeysState Alt(false, false, false, false, true, false, false, false, false);
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Z, Alt, 0, false, 0, 'Z'));
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Z, Alt, 0, false, 0, 'Z'));
			}
		}, 4);
		Add(R, [](FRun& X)
		{
			const UAsdaUiVisibilitySubsystem* V = X.Sub<UAsdaUiVisibilitySubsystem>();
			X.Expect(V && V->IsUiShown() && X.Open() && FieldFocused(X) && FieldText(X).IsEmpty(),
				FString::Printf(TEXT("Alt+Z при открытом нумпаде не сработал (эталон: гейт «модальное окно» CWindowMgr+0x200), интерфейс %s, «z» в поле не попала: «%s»"),
					(V && V->IsUiShown()) ? TEXT("виден") : TEXT("СКРЫТ"), *FieldText(X)));
			TypeChar(TEXT('4'));
		});
		Add(R, [](FRun& X)
		{
			X.Expect(FieldText(X) == TEXT("4"), FString::Printf(TEXT("набор в видимое окно: «%s»"), *FieldText(X)));
			Cleanup(X);
			if (UAsdaUiVisibilitySubsystem* V = X.Sub<UAsdaUiVisibilitySubsystem>()) { V->SetUiShown(false, TEXT("самотест: интерфейс скрыт")); }
		}, 4);
		Add(R, [](FRun& X)
		{
			const UAsdaUiVisibilitySubsystem* V = X.Sub<UAsdaUiVisibilitySubsystem>();
			X.Expect(V && !V->IsUiShown(), TEXT("интерфейс скрыт (Alt+Z) до открытия окна"));
			ShowPad(X, 50);
		}, kSettle);
		Add(R, [](FRun& X)
		{
			const UAsdaUiVisibilitySubsystem* V = X.Sub<UAsdaUiVisibilitySubsystem>();
			X.Expect(V && V->IsUiShown() && X.Open() && FieldFocused(X),
				FString::Printf(TEXT("окно, открытое при скрытом интерфейсе, показало интерфейс ([ИСПРАВЛЕНИЕ 2–3] Alt+Z): интерфейс %s, фокус в поле %s"),
					(V && V->IsUiShown()) ? TEXT("виден") : TEXT("СКРЫТ"), FieldFocused(X) ? TEXT("да") : TEXT("нет")));
			TypeChar(TEXT('7'));
		});
		AddEnter(R);
		Add(R, [](FRun& X)
		{
			X.Expect(GC.Ok == 1 && GC.Qty == 7, FString::Printf(TEXT("Enter — в видимом окне: ОК %d, количество %d"), GC.Ok, GC.Qty));
			if (UAsdaUiVisibilitySubsystem* V = X.Sub<UAsdaUiVisibilitySubsystem>()) { V->SetUiShown(true, TEXT("самотест: уборка")); }
			Cleanup(X);
		});
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		if (!W || !W->GetGameInstance()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.NumPadFocus"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.InputDialogFocus"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.InputDialogEsc"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalFocusNoRoot"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.EnterConfirm"), 1);
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.W = W;
		R.bQuit = Args.bQuit;
		TWeakPtr<FRun> Weak = GRun;
		R.CapGen = AsdaSelfTestKit::ArmCapture(AsdaSelfTestKit::NetOf(W),
			[Weak](uint16, const uint8*, int32, bool, TCHAR) { if (TSharedPtr<FRun> P = Weak.Pin()) { ++P->Sent; } }, 0, kCmd);
		// Свой персонаж офлайн — HUD, строка чата и окна мира строятся только в мире с персонажем.
		if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			if (UAsdaWorldSubsystem* WS = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
			}
		}
		Build(R);
		BuildInput(R);
		BuildReview(R);
		// Снять ловца журнала последним шагом (итог печатается после шагов).
		Add(R, [](FRun&) { if (GLog) { GLog->RemoveOutputDevice(&GNonFocusCatcher); } }, 1);
		if (GLog) { GLog->AddOutputDevice(&GNonFocusCatcher); }
		R.WaitUntilTime = FPlatformTime::Seconds() + 2.5;   // первый шаг — после построения HUD и окон
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestNumPadFocus(
	TEXT("asda.SelfTest.NumPadFocus"),
	TEXT("Самотест окна количества 1410: фокус поля 1412 при открытии без щелчка, цифры с клавиатуры, Enter = ОК, ESC = отмена, курсор в конце, "
	     "чат/мир ввода не получают, щелчки мышью путём Slate, ключ отката; кнопка 71403 всплывашки улова не на экране. Мир, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaNumPadFocusSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
