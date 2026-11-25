// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЗАМОК ВВОДА МОДАЛКОЙ ЭТАЛОНА» — `asda.UI.SelfTestModalLock [delay=N] [quit]` (10.10.2026, отчёт
//   `Reports/owner_1010/modal_lock.md`). Эталон: `helper_setupFields` → `sub_12AFBB0` ставит флаг `CWindowMgr+0x200`, его читает
//   `get_o200@0x9864A0` в раскладке клавиш (KeyUp/KeyDown/Alt — `MainWndProc`), Enter → чат, ESC-меню, хотбаре и системном меню;
//   модальный контрол `+0x1A0` забирает мышь. Порт: `UAsdaWindowSubsystem::IsAnyModalOpen` (роспись `RegisterEscModal` /
//   `RegisterOriginalModal`), гейт `PollKeys`, блокер Slate под модалкой.
//   Каждая проверка — ПАРА: то же действие без модалки обязано сработать (иначе тест слеп), под модалкой — нет.
//   Ввод — путём игрока: клавиши событиями Slate (`ProcessKeyDownEvent` / `ProcessKeyUpEvent`) и вторым методом через
//   `UPlayerInput` (`InputKey`, как `asda.Keybinds.HoldSim`) — второй путь проверяет сам эталонный гейт `PollKeys`, мимо
//   `FInputModeUIOnly`; мышь — настоящими событиями Slate (`SimulateClickAt`).
//   Разделы:
//     K. Контроль (модалки нет): клавиша сумки, Enter → строка чата, Alt+клавиша (временный бинд Alt на команду сумки),
//        гейт хотбара, кнопка 73402 системного меню (вызов и настоящий щелчок), ESC-меню отперто.
//     M. Парикмахерская 15000: настоящий щелчок «Купить» → общая модалка 1559. Под ней: те же клавиши обоими путями, хотбар,
//        системное меню (вызов), щелчок по другой карточке «Купить» — ничего не срабатывает; ESC-меню заперто;
//        ключ `asda.UI.ModalLock 0` снимает замок (ESC-меню остаётся запертым — так было до правки). Щелчок «Отмена» 1562
//        модалке доходит.
//     A1. После закрытия 1559 щелчок «Купить» снова открывает 1559 (мышь окну под модалкой вернулась).
//     M2. 1559 штатным `Open` без магазина под ней (окно 15000 накрывает кнопку 73402): настоящий щелчок по 73402 съеден.
//     A2. После закрытия — всё снова работает (те же проверки, что в K, включая щелчок по 73402).
//     F. Книга рыбака 74000 + окно регистрации 74200 (`fishCollectionOpenWindow`, модальный контрол ВНУТРИ окна): клавиша сумки и
//        системное меню заперты, ESC ничего не закрывает, опрос книги не заперт (`IsModalDispatchBlockedFor`), чужой — заперт;
//        после `CloseJoin` всё работает.
//   ★ 10.10.2026 (modal_lock2, СРЕДН-2 ревизии `modal_lock_REVIEW.md`) — пути обхода мышью, тоже парами:
//     M3. Сумка 400 открыта: ПКМ по ячейке сетки → меню 28000 (эталон ui_core.c:48036); перетаскивание сумки за ручку — окно
//        сдвинулось; колесо над миром — зум камеры, над окном — нет. Под 1559: ПКМ съеден (★ modal_lock3: и при ключе отката
//        `asda.UI.ModalLockRightClick 0` — окнам под модалкой эталон ПКМ не отдаёт), сумка не тащится, колесо над миром
//        ЗУМИТ (эталон game_main.c:5864 — только «курсор не над окном»), над 1559 — нет. Под окном `bModal` 1500 — колесо над
//        миром тоже зумит (общий механизм).
//     F2. Порядок z книги после снятия 74200 (МЕЛК-4): выше всех видимых обычных окон, ниже всех видимых AlwaysTop.
//     F3. Двойной щелчок по ячейке книги: без 74200 открывает 74200 рыбы ячейки; под 74200 по другой ячейке (вне узла 74200) —
//        окно на другую рыбу НЕ перерисовывается; хит-тест окон над ячейкой книги при 74200 книгу не ловит, над узлом 74200 —
//        ловит, без 74200 — ловит (МЕЛК-2); ручек перетаскивания у книги нет (`.JUI`); при 74200 колесо в мир идёт
//        препроцессором, над книгой вне узла зума нет.
//   ★ 10.10.2026 (modal_lock3, МЕЛК-1…4 ревизии `modal_lock2_REVIEW.md`), тоже парами:
//     M3+. Колесо по спискам: сетка сумки и лента чата 2521 без модалки листаются, под 1559 — нет; временный обработчик колеса
//        у самой 1559 вызывается ровно один раз; после закрытия 1559 и 1500 колесо над миром — ровно один зум. Наведение на
//        ячейку сумки: без модалки — подсказка, под 1559 с ключом `asda.UI.ModalLockRightClick 0` и зажатой ПКМ — ни подсказки,
//        ни меню 28000 (эталон окнам ПКМ не отдаёт). ПКМ-проводка: поворот камеры под 1559 с ключом 0 равен повороту без
//        модалки (оси через `UPlayerInput`), с ключом 1 — нулевой.
//     W. Склад 4000 «Хранить»: список листается колесом без модалки и после неё, под 1559 — нет.
//     M4. 1500 закрыто при «потерянном» контроллере (`DebugSimulateNoController`), затем режим мира, как у `RevealWorld`: ввод
//        мира модалкой не отобран, колесо над миром — ровно один зум.
//   Исходящие пакеты перехватываются (`UAsdaPetSubsystem::BeginSendCapture`) — в сеть не уходит ничего.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"            // реестр окон, замок модалки, синтетический клик
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaInventorySubsystem.h"         // сумка 400
#include "UI/AsdaChatWindowSubsystem.h"        // строка чата (Enter)
#include "UI/AsdaHotbarSubsystem.h"            // гейт активации слота
#include "UI/AsdaSystemMenuSubsystem.h"        // системное меню 73401…73410
#include "UI/AsdaSystemOptionSubsystem.h"      // гейт ESC-меню 69400
#include "UI/AsdaHairShopSubsystem.h"          // парикмахерская 15000
#include "UI/AsdaPurchaseBoxSubsystem.h"       // общая модалка покупки 1559
#include "UI/AsdaFishBookSubsystem.h"          // книга рыбака 74000 / окно регистрации 74200
#include "UI/AsdaUiHelpers.h"                  // WidgetAbsRect, IsModalDispatchBlocked(For)
#include "World/AsdaPlayerController.h"        // раскладка клавиш (Keys)
#include "World/AsdaKeybindConfig.h"           // бинды, VK ↔ FKey
#include "World/AsdaWorldSubsystem.h"          // свой павн офлайн
#include "Pet/AsdaPetSubsystem.h"              // перехват исходящих
#include "AsdaOutgoingGate.h"                  // офлайн-гейт отладочных команд
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "UI/AsdaShowOffSubsystem.h"           // modal_lock2: меню 28000 по ПКМ ячейки сумки
#include "World/AsdaPlayerCharacter.h"         // modal_lock2: пружина камеры (зум колесом)
#include "AsdaNetSubsystem.h"                  // modal_lock2: штатный приём 6174 (тома книги с уловом)
#include "AsdaBytes.h"                         // modal_lock2: сборка тела 6174
#include "GameFramework/SpringArmComponent.h"  // modal_lock2: TargetArmLength
#include "Engine/GameViewportClient.h"         // modal_lock2: геометрия игрового вьюпорта
#include "Widgets/SViewport.h"                 // modal_lock2: то же
#include "Containers/Ticker.h"                 // modal_lock2: второй щелчок двойного — после доставки первого
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Internationalization/Regex.h"
#include "UI/AsdaItemTooltipSubsystem.h"        // modal_lock3: подсказка предмета при наведении на ячейку сумки
#include "UI/AsdaWarehouseSubsystem.h"          // modal_lock3: прокрутка списка склада колесом
#include "GameFramework/InputSettings.h"       // modal_lock3: сглаживание мыши на время точной пары вращения

DEFINE_LOG_CATEGORY_STATIC(LogAsdaModalLockTest, Log, All);

namespace AsdaModalLockLocal
{
	static const FName kInven(TEXT("INVEN_WIN"));               // сумка 400
	static const FName kHair(TEXT("HAIRSHOP_WIN"));             // парикмахерская 15000
	static const FName kBox(TEXT("SHELLITEM_PURCHASE_BOX"));    // общая модалка покупки 1559
	static const FName kSysMenu(TEXT("SYSTEMMAIN_NEW_WIN"));    // системное меню 73400
	static const FName kBook(TEXT("FISH_BOOK_WIN"));            // книга рыбака 74000
	static const FName kOneBut(TEXT("ONEBUT_MSGBOX"));          // окно-сообщение 1500 (`bModal`) — общий механизм
	static constexpr int32 kTestItem = 20966;                   // предмет для ячейки сумки (тот же, что у самотеста отправителей charId)

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0, Skip = 0;       // пройдено / провалено / пропущено
		TWeakObjectPtr<UWorld> W;               // мир прогона
		bool bQuit = false;                     // выйти после прогона
		uint32 CapGen = 0;                      // перехват исходящих взведён
		int32 Sent = 0;                         // перехвачено отправок
		TArray<TFunction<int32(FCtx&)>> Steps;  // шаги: тело возвращает, сколько кадров ждать до следующего
		int32 Step = 0;                         // текущий шаг
		TArray<FString> Table;                  // итоговая таблица
		FKey KeyInv;                            // клавиша команды 33 (сумка)
		FKey KeyEnter;                          // клавиша команды 66 (строка чата)
		FAsdaKeyBind SavedInvBind;              // бинд команды 33 до временного Alt
		int32 BoxShowsBefore = 0;               // счётчик показов 1559 до щелчка
		FString Phase;                          // подпись раздела в строках проверок
		bool bClickArmed = false;               // щелчок по 73402 заказан (кнопку ничто, кроме модалки, не накрывало)
		// ── modal_lock2 ──
		bool bWheelArmed = false;               // колесо послано (точка нашлась, павн есть)
		FVector2D WheelPt = FVector2D::ZeroVector;   // точка колеса
		bool bRmbArmed = false;                 // ПКМ по ячейке послан
		FVector2D RmbPt = FVector2D::ZeroVector;     // точка ПКМ
		int32 PutCont = 0;                      // контейнер ячейки 0 сетки сумки
		bool bPutByTest = false;                // предмет в ячейку положил тест (убрать в конце)
		bool bDragArmed = false;                // перетаскивание послано
		FVector2D HandleBefore = FVector2D::ZeroVector;   // левый верх ручки до перетаскивания
		int32 RmbCvarWas = 1;                   // `asda.UI.ModalLockRightClick` до пары отката
		int32 CellA = -1, CellB = -1;           // ячейки книги: A — откуда открываем 74200, B — щелчок под ним (вне узла 74200)
		int32 FishA = -1, FishB = -1;           // их рыбы
		bool bDblArmed = false;                 // двойной щелчок заказан
		// ── modal_lock3 ──
		bool bScrollArmed = false;              // колесо по списку послано
		float ScrollBefore = 0.f;               // позиция списка до колеса
		FVector2D ScrollPt = FVector2D::ZeroVector;  // точка колеса по списку
		bool bTipArmed = false;                 // проверка подсказки сумки заказана
		bool bTipRmb = false;                   // в ней зажата ПКМ
		FVector2D TipPt = FVector2D::ZeroVector;     // точка наведения (ячейка 0 сумки)
		bool bOrbitArmed = false;               // ПКМ-проводка начата
		FVector2D OrbitPt = FVector2D::ZeroVector;   // начало проводки (точка мира)
		float OrbitYaw0 = 0.f;                  // `CamYawDeg` до проводки
		float OrbitNoModal = NAN;               // поворот камеры проводкой без модалки, градусы
		float OrbitModalKey0 = NAN;             // то же под 1559 при ключе `asda.UI.ModalLockRightClick 0`
		float OrbitModalKey1 = NAN;             // то же под 1559 при ключе 1 (по умолчанию)
		float OrbitNoModalRaw = NAN;            // поворот без модалки при выключенном сглаживании мыши
		float OrbitModalKey0Raw = NAN;          // то же под 1559 при ключе 0
		bool bSmoothWas = true;                 // `bEnableMouseSmoothing` до проводки
		int32 ModalWheelCount = 0;              // вызовы временного обработчика колеса у 1559
		FVector2D WarePt = FVector2D::ZeroVector;    // точка над списком склада 4000 (найдена без модалки)
		bool bWarePt = false;                   // точка склада найдена
		// ── whwheel (10.10.2026): колесо склада только над списком 4189 ──
		FVector2D WareOffPt = FVector2D::ZeroVector; // точка над окном 4000, но ВНЕ области списка 4189
		bool bWareOffPt = false;                // такая точка найдена
		int32 WareRowBefore = -1;               // ряд списка склада до колеса над миром / вне списка

		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК ok   [%s] %s"), *Phase, *What); }
			else   { ++Fail; UE_LOG(LogAsdaModalLockTest, Error, TEXT("МОДАЛ-ЗАМОК FAIL [%s] %s"), *Phase, *What); }
			Table.Add(FString::Printf(TEXT("%s | %s | %s"), *Phase, *What, b ? TEXT("ok") : TEXT("FAIL")));
		}
		// Пропуск (офлайн окно не открылось) — не провал, но в отчёт.
		void SkipCase(const FString& What)
		{
			++Skip; UE_LOG(LogAsdaModalLockTest, Warning, TEXT("МОДАЛ-ЗАМОК ПРОПУСК [%s] %s"), *Phase, *What);
			Table.Add(FString::Printf(TEXT("%s | %s | пропуск"), *Phase, *What));
		}
		UGameInstance* GI() const { UWorld* X = W.Get(); return X ? X->GetGameInstance() : nullptr; }
		template<typename T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* X = W.Get(); return X ? Cast<AAsdaPlayerController>(X->GetFirstPlayerController()) : nullptr; }
	};

	// Выполнить шаг и запланировать следующий не раньше чем через N кадров (и не раньше 1/30 с — длинные кадры загрузки).
	static void RunStep(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		if (!W) { AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestModalLock")); return; }   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернуть и на этом выходе
		if (!C->Steps.IsValidIndex(C->Step)) { return; }
		const int32 Wait = FMath::Max(1, C->Steps[C->Step](*C));
		++C->Step;
		struct FPoll
		{
			// Ожидание кадров/времени до следующего шага.
			static void Tick(TSharedRef<FCtx> C, uint64 FEnd, double TEnd)
			{
				UWorld* W = C->W.Get();
				if (!W) { AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestModalLock")); return; }   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернуть и на этом выходе
				if (GFrameCounter >= FEnd && FPlatformTime::Seconds() >= TEnd) { RunStep(C); return; }
				W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, FEnd, TEnd]() { Tick(C, FEnd, TEnd); }));
			}
		};
		const uint64 FEnd = GFrameCounter + (uint64)Wait;
		const double TEnd = FPlatformTime::Seconds() + Wait / 30.0;
		W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, FEnd, TEnd]() { FPoll::Tick(C, FEnd, TEnd); }));
	}

	// Клавиша событием Slate (путь физической клавиатуры: фокус → вьюпорт → `UPlayerInput`). bAlt — зажат левый Alt.
	static void SlateKey(const FKey& K, bool bDown, bool bAlt)
	{
		if (!FSlateApplication::IsInitialized() || !K.IsValid()) { return; }
		const FModifierKeysState M(false, false, false, false, bAlt, false, false, false, false);
		const FKeyEvent Ev(K, M, 0, false, 0, 0);
		if (bDown) { FSlateApplication::Get().ProcessKeyDownEvent(Ev); } else { FSlateApplication::Get().ProcessKeyUpEvent(Ev); }
	}
	// Клавиша прямо в `UPlayerInput` (второй метод, как `asda.Keybinds.HoldSim`) — мимо режима ввода Slate, до гейта `PollKeys`.
	static void PiKey(AAsdaPlayerController* PC, const FKey& K, bool bDown)
	{
		if (!PC || !K.IsValid()) { return; }
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	}
	// Нажать клавишу выбранным путём: шаги «нажать» и «отпустить» (разные кадры — `PollKeys` стреляет по фронту отпускания).
	//   bPi — путь `UPlayerInput`; bAlt — с левым Alt; bFocusGame — перед нажатием отдать фокус вьюпорту (игрок щёлкнул по миру).
	static void AddPress(TSharedRef<FCtx> C, TFunction<FKey(FCtx&)> KeyOf, bool bPi, bool bAlt, bool bFocusGame)
	{
		C->Steps.Add([KeyOf, bPi, bAlt, bFocusGame](FCtx& X) -> int32
		{
			if (bFocusGame && FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
			const FKey K = KeyOf(X);
			if (bPi) { if (bAlt) { PiKey(X.PC(), EKeys::LeftAlt, true); } PiKey(X.PC(), K, true); }
			else     { if (bAlt) { SlateKey(EKeys::LeftAlt, true, true); } SlateKey(K, true, bAlt); }
			return 3;
		});
		C->Steps.Add([KeyOf, bPi, bAlt](FCtx& X) -> int32
		{
			const FKey K = KeyOf(X);
			if (bPi) { PiKey(X.PC(), K, false); } else { SlateKey(K, false, bAlt); }
			return 3;   // фронт отпускания клавиши — пока Alt ещё зажат (модификатор `PollKeys` живой, на кадре отпускания)
		});
		C->Steps.Add([bPi, bAlt](FCtx& X) -> int32
		{
			if (bAlt) { if (bPi) { PiKey(X.PC(), EKeys::LeftAlt, false); } else { SlateKey(EKeys::LeftAlt, false, false); } }
			return 3;   // кадр открытия окна
		});
	}
	// Шаг-тело без ожидания сверх двух кадров.
	static void AddDo(TSharedRef<FCtx> C, TFunction<void(FCtx&)> Fn, int32 Frames = 2)
	{
		C->Steps.Add([Fn, Frames](FCtx& X) -> int32 { Fn(X); return Frames; });
	}

	// Состояния и действия.
	static bool InvShown(FCtx& X) { const UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>(); return I && I->IsWindowOpen(); }
	static void InvClose(FCtx& X) { if (UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>()) { if (I->IsWindowOpen()) { I->CloseWindow(); } } }
	static bool ChatActive(FCtx& X) { const UAsdaChatWindowSubsystem* Cw = X.Sub<UAsdaChatWindowSubsystem>(); return Cw && Cw->IsInputActive(); }
	static void ChatClose(FCtx& X)
	{
		if (UAsdaChatWindowSubsystem* Cw = X.Sub<UAsdaChatWindowSubsystem>()) { if (Cw->IsInputActive()) { Cw->CloseInput(); } }
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
	}
	static bool HotbarBlocked(FCtx& X, FString& Why) { const UAsdaHotbarSubsystem* H = X.Sub<UAsdaHotbarSubsystem>(); return H && H->IsActivationBlockedForCheck(Why); }
	static bool EscMenuBlocked(FCtx& X, FString& Why) { const UAsdaSystemOptionSubsystem* S = X.Sub<UAsdaSystemOptionSubsystem>(); return S && S->IsEscMenuGateBlocked(Why); }
	static bool ModalUp(FCtx& X) { const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>(); return WM && WM->IsAnyModalOpen(); }
	// ★ 11.10.2026 (дедуп №2, В1-B, U9): окна питомцев (клетка 43000, эволюция 43500, рост 44200, инкубатор 42000) запирали
	//   покадровый опрос мыши своим условием `WM->IsAnyModalOpen()`; теперь — общей точкой `AsdaUi::IsModalDispatchBlocked`.
	//   При ключе `asda.UI.ModalGatePolling 1` (умолчание) обе формы обязаны давать одно и то же в каждом состоянии модалки.
	static void PetGateCheck(FCtx& X, const TCHAR* Where)
	{
		const bool bOld = ModalUp(X);
		const bool bNew = AsdaUi::IsModalDispatchBlocked(X.GI());
		X.Expect(bOld == bNew, FString::Printf(TEXT("питомцы (43000/43500/44200/42000) %s: гейт опроса = прежнее условие IsAnyModalOpen (%d = %d)"),
			Where, bNew ? 1 : 0, bOld ? 1 : 0));
	}
	// Центр виджета окна в абсолютных координатах Slate (false — нет виджета или он не отрисован).
	static bool CenterOf(FCtx& X, FName Win, const TCHAR* Name, FVector2D& Out)
	{
		const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
		UUserWidget* Root = WM ? WM->FindWindow(Win) : nullptr;
		const UWidget* Wd = Root ? Root->GetWidgetFromName(FName(Name)) : nullptr;
		FVector2D Mn, Mx;
		if (!Wd || !AsdaUi::IsShown(Wd) || !AsdaUi::WidgetAbsRect(Wd, Mn, Mx) || (Mx - Mn).IsNearlyZero()) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Счётчик показов 1559 (`DumpState`: «показов=N»).
	static int32 BoxShows(FCtx& X)
	{
		const UAsdaPurchaseBoxSubsystem* B = X.Sub<UAsdaPurchaseBoxSubsystem>();
		if (!B) { return -1; }
		const FString S = B->DumpState();
		FRegexMatcher M(FRegexPattern(TEXT("показов=(\\d+)")), S);
		return M.FindNext() ? FCString::Atoi(*M.GetCaptureGroup(1)) : -1;
	}
	static bool BoxOpen(FCtx& X) { const UAsdaPurchaseBoxSubsystem* B = X.Sub<UAsdaPurchaseBoxSubsystem>(); return B && B->IsOpen(); }
	static bool HairOpen(FCtx& X) { const UAsdaHairShopSubsystem* H = X.Sub<UAsdaHairShopSubsystem>(); return H && H->IsSessionOpen(); }

	static void AddSysMenuClick(TSharedRef<FCtx> C, bool bLocked, const FString& Tag);   // ниже: настоящий щелчок по 73402

	// Набор клавишных и прочих проверок раздела. bLocked — ждём замок (модалка открыта), иначе — срабатывание (контроль).
	//   Путь Slate проверяется всегда; путь `UPlayerInput` — всегда (под модалкой он и проверяет сам гейт `PollKeys`).
	//   bFocusGame — в контроле фокус отдаётся вьюпорту (игрок щёлкнул по миру), под модалкой фокус не трогаем (путь игрока).
	//   bMouse — проверять и настоящий щелчок по кнопке 73402 (только когда кнопку не накрывает другое окно, иначе пара слепа).
	static void AddKeyChecks(TSharedRef<FCtx> C, bool bLocked, const FString& Tag, bool bMouse = true)
	{
		const bool bFocus = !bLocked;
		for (int32 Path = 0; Path < 2; ++Path)
		{
			const bool bPi = (Path == 1);
			const FString PathName = bPi ? TEXT("UPlayerInput") : TEXT("Slate");
			// 1. Клавиша сумки (команда 33).
			AddDo(C, [](FCtx& X) { InvClose(X); }, 3);
			AddPress(C, [](FCtx& X) { return X.KeyInv; }, bPi, false, bFocus);
			AddDo(C, [bLocked, PathName, Tag](FCtx& X)
			{
				const bool bOpen = InvShown(X);
				X.Expect(bLocked ? !bOpen : bOpen, FString::Printf(TEXT("%s: клавиша сумки «%s» (%s) — сумка %s (ждём %s)"), *Tag,
					*X.KeyInv.ToString(), *PathName, bOpen ? TEXT("ОТКРЫТА") : TEXT("не открыта"), bLocked ? TEXT("не открыта") : TEXT("открыта")));
				InvClose(X);
			}, 3);
			// 2. Enter → строка чата (команда 66).
			AddPress(C, [](FCtx& X) { return X.KeyEnter; }, bPi, false, bFocus);
			AddDo(C, [bLocked, PathName, Tag](FCtx& X)
			{
				const bool bChat = ChatActive(X);
				X.Expect(bLocked ? !bChat : bChat, FString::Printf(TEXT("%s: Enter «%s» (%s) — строка чата %s (ждём %s)"), *Tag,
					*X.KeyEnter.ToString(), *PathName, bChat ? TEXT("ОТКРЫТА") : TEXT("не открыта"), bLocked ? TEXT("не открыта") : TEXT("открыта")));
				ChatClose(X);
			}, 3);
			// 3. Alt-сочетание: временный бинд «Alt + клавиша сумки» на команду 33 (Alt-биндов в раскладке оригинала нет).
			AddDo(C, [](FCtx& X)
			{
				AAsdaPlayerController* P = X.PC();
				if (P && P->Keys) { P->Keys->GetBind(33, X.SavedInvBind); FAsdaKeyBind B = X.SavedInvBind; B.Mod = EAsdaKeyMod::Alt; P->Keys->Binds.Add(33, B); }
			}, 2);
			AddPress(C, [](FCtx& X) { return X.KeyInv; }, bPi, true, bFocus);
			AddDo(C, [bLocked, PathName, Tag](FCtx& X)
			{
				const bool bOpen = InvShown(X);
				X.Expect(bLocked ? !bOpen : bOpen, FString::Printf(TEXT("%s: Alt+«%s» (временный бинд Alt на команду 33, %s) — сумка %s (ждём %s)"), *Tag,
					*X.KeyInv.ToString(), *PathName, bOpen ? TEXT("ОТКРЫТА") : TEXT("не открыта"), bLocked ? TEXT("не открыта") : TEXT("открыта")));
				InvClose(X);
				AAsdaPlayerController* P = X.PC();
				if (P && P->Keys) { P->Keys->Binds.Add(33, X.SavedInvBind); }   // бинд на место (в ini не пишется)
			}, 3);
		}
		// 4. Гейт хотбара (общая точка клавиши и щелчка по слоту — `IsActivationBlocked`).
		AddDo(C, [bLocked, Tag](FCtx& X)
		{
			FString Why;
			const bool bBlk = HotbarBlocked(X, Why);
			X.Expect(bLocked ? (bBlk && Why.Contains(TEXT("модальн"))) : !bBlk,
				FString::Printf(TEXT("%s: гейт слота хотбара — %s%s (ждём %s)"), *Tag, bBlk ? TEXT("заперт: ") : TEXT("открыт"), *Why,
					bLocked ? TEXT("заперт модалкой") : TEXT("открыт")));
		});
		// 5. Кнопка 73402 системного меню — вызов обработчика кнопки (горячая клавиша меню идёт им же).
		AddDo(C, [](FCtx& X) { InvClose(X); if (UAsdaSystemMenuSubsystem* M = X.Sub<UAsdaSystemMenuSubsystem>()) { M->ClickButton(73402); } }, 8);
		AddDo(C, [bLocked, Tag](FCtx& X)
		{
			const bool bOpen = InvShown(X);
			X.Expect(bLocked ? !bOpen : bOpen, FString::Printf(TEXT("%s: кнопка 73402 системного меню (обработчик) — сумка %s (ждём %s)"), *Tag,
				bOpen ? TEXT("ОТКРЫТА") : TEXT("не открыта"), bLocked ? TEXT("не открыта") : TEXT("открыта")));
			InvClose(X);
		}, 6);
		// 6. Настоящий щелчок мыши по кнопке 73402 (под модалкой его обязан съесть блокер).
		if (bMouse) { AddSysMenuClick(C, bLocked, Tag); }
		// 7. Гейт ESC-меню 69400 (настоящий гейт подсистемы настроек).
		AddDo(C, [bLocked, Tag](FCtx& X)
		{
			FString Why;
			const bool bBlk = EscMenuBlocked(X, Why);
			X.Expect(bLocked ? bBlk : !bBlk, FString::Printf(TEXT("%s: гейт ESC-меню 69400 — %s %s (ждём %s)"), *Tag,
				bBlk ? TEXT("заперт:") : TEXT("отперт"), *Why, bLocked ? TEXT("заперт") : TEXT("отперт")));
		});
	}

	// Настоящий щелчок мыши по кнопке 73402 системного меню. Кнопку не должно накрывать ни одно окно, кроме модалки
	//   (иначе «не сработало» доказывало бы перекрытие, а не замок): проверяется хит-тестом порта до щелчка.
	static void AddSysMenuClick(TSharedRef<FCtx> C, bool bLocked, const FString& Tag)
	{
		AddDo(C, [Tag](FCtx& X)
		{
			FVector2D P;
			if (!CenterOf(X, kSysMenu, TEXT("NEW_SYSTEMMAIN_ITEMBUT"), P)) { X.SkipCase(Tag + TEXT(": кнопка 73402 не отрисована — щелчок мышью не проверен")); return; }
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			if (!WM) { return; }
			for (const FName& Other : { kHair, kBook, kInven })
			{
				if (WM->IsWindowVisible(Other) && WM->IsWindowHitAt(Other, P))
				{
					X.SkipCase(FString::Printf(TEXT("%s: кнопку 73402 накрывает окно «%s» — щелчок не проверен"), *Tag, *Other.ToString()));
					return;
				}
			}
			if (WM->IsWindowVisible(kBox) && WM->IsWindowHitAt(kBox, P))
			{
				X.SkipCase(Tag + TEXT(": кнопку 73402 накрывает сама модалка 1559 — щелчок дошёл бы до неё, не проверен"));
				return;
			}
			UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК [%s] щелчок по 73402 в (%.0f,%.0f), хит-тест порта: ловит окно «%s»"),
				*Tag, P.X, P.Y, *WM->FindWindowCatchingAt(P).ToString());
			X.bClickArmed = true;
			WM->SimulateClickAt(P, 3);
		}, 14);
		AddDo(C, [bLocked, Tag](FCtx& X)
		{
			FVector2D P;
			if (!X.bClickArmed || !CenterOf(X, kSysMenu, TEXT("NEW_SYSTEMMAIN_ITEMBUT"), P)) { return; }
			X.bClickArmed = false;
			const bool bOpen = InvShown(X);
			X.Expect(bLocked ? !bOpen : bOpen, FString::Printf(TEXT("%s: щелчок мышью по 73402 (%.0f,%.0f) — сумка %s (ждём %s)"), *Tag, P.X, P.Y,
				bOpen ? TEXT("ОТКРЫТА") : TEXT("не открыта"), bLocked ? TEXT("не открыта") : TEXT("открыта")));
			InvClose(X);
		}, 6);
	}

	// Щелчок по «Купить» карточки, не накрытой 1559. bExpectOpen — ждём показ 1559 (иначе — что щелчок съеден).
	static void AddHairBuyClick(TSharedRef<FCtx> C, bool bExpectOpen, const FString& Tag)
	{
		AddDo(C, [Tag](FCtx& X)
		{
			X.BoxShowsBefore = BoxShows(X);
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			for (int32 i = 2; i <= 14 && WM; ++i)
			{
				FVector2D P;
				if (!CenterOf(X, kHair, *FString::Printf(TEXT("HAIR_ITEM_BUY%02d"), i), P)) { continue; }
				if (WM->IsWindowHitAt(kBox, P)) { continue; }   // 1559 над этой кнопкой — щелчок дойдёт до модалки, а не до карточки
				WM->SimulateClickAt(P, 3);
				UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК [%s] щелчок «Купить» карточки %02d в (%.0f,%.0f), ловит окно «%s»"),
					*Tag, i, P.X, P.Y, *WM->FindWindowCatchingAt(P).ToString());
				return;
			}
			X.SkipCase(Tag + TEXT(": нет отрисованной кнопки «Купить» вне 1559"));
		}, 16);
		AddDo(C, [bExpectOpen, Tag](FCtx& X)
		{
			const int32 Now = BoxShows(X);
			const bool bShown = Now > X.BoxShowsBefore;
			X.Expect(bExpectOpen ? (bShown && BoxOpen(X)) : !bShown, FString::Printf(TEXT("%s: щелчок мышью по «Купить» другой карточки — показов 1559 %d → %d (ждём %s)"),
				*Tag, X.BoxShowsBefore, Now, bExpectOpen ? TEXT("+1") : TEXT("без изменений — щелчок съеден")));
		}, 4);
	}

	// ══════ ★ 10.10.2026 (modal_lock2, СРЕДН-2): ПКМ, колесо, перетаскивание, двойной щелчок ══════

	// Пружина камеры своего павна (nullptr — павна нет).
	static USpringArmComponent* ArmOf(FCtx& X)
	{
		AAsdaPlayerController* P = X.PC();
		AAsdaPlayerCharacter* Ch = P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr;
		return Ch ? Ch->CameraArm.Get() : nullptr;
	}
	// Точка мира: курсор над игровым вьюпортом и НЕ над окном (`IsCursorOverAnyUiWindow` — порт эталонного
	//   `CControl_HitTest(g_pWindowMgr)`, game_main.c:5874). Сетка 37×37 по вьюпорту (шаг 1/40 — книга 74000 занимает 810×756 из
	//   1024×768, свободны лишь поля по краям); курсор возвращается на место.
	static bool WorldPoint(FCtx& X, FVector2D& Out)
	{
		UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
		if (!WM || !FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) { return false; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		if (!Vp.IsValid()) { return false; }
		const FGeometry& G = Vp->GetTickSpaceGeometry();
		const FVector2D Org = G.GetAbsolutePosition();   // левый верх вьюпорта (абсолютные координаты Slate)
		const FVector2D Sz = G.GetAbsoluteSize();        // размер вьюпорта
		FSlateApplication& App = FSlateApplication::Get();
		const FVector2D Was = App.GetCursorPos();        // курсор до поиска
		bool bFound = false;                             // точка найдена
		for (int32 Iy = 2; Iy <= 38 && !bFound; ++Iy)
		{
			for (int32 Ix = 2; Ix <= 38 && !bFound; ++Ix)
			{
				const FVector2D P = Org + FVector2D(Sz.X * Ix / 40.f, Sz.Y * Iy / 40.f);
				App.SetCursorPos(P);
				if (!App.GetCursorPos().Equals(P, 1.f)) { continue; }   // курсор не встал — проверить точку нельзя
				if (!WM->IsCursorOverAnyUiWindow()) { Out = P; bFound = true; }
			}
		}
		App.SetCursorPos(Was);
		return bFound;
	}
	// Колесо НАСТОЯЩИМ событием Slate (`ProcessMouseWheelOrGestureEvent`, щелчок вверх) в точке PointOf. До события дистанция
	//   камеры ставится 600; эталонный зум ×0.75 даёт 450 (`Camera_MouseWheelZoom`, кламп 170..1270). bExpectZoom — ждём зум.
	static void AddWheel(TSharedRef<FCtx> C, TFunction<bool(FCtx&, FVector2D&)> PointOf, bool bExpectZoom, const FString& What)
	{
		AddDo(C, [PointOf, What](FCtx& X)
		{
			X.bWheelArmed = false;
			USpringArmComponent* A = ArmOf(X);
			FVector2D P;
			if (!A) { X.SkipCase(What + TEXT(": нет своего павна/пружины камеры — колесо не проверено")); return; }
			if (!PointOf(X, P)) { X.SkipCase(What + TEXT(": точка для колеса не найдена")); return; }
			A->TargetArmLength = 600.f;
			FSlateApplication& App = FSlateApplication::Get();
			App.SetCursorPos(P);
			App.ProcessMouseMoveEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
			App.ProcessMouseWheelOrGestureEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, 1.f, FModifierKeysState()), nullptr);
			X.WheelPt = P;
			X.bWheelArmed = true;
		}, 4);
		AddDo(C, [bExpectZoom, What](FCtx& X)
		{
			if (!X.bWheelArmed) { return; }
			X.bWheelArmed = false;
			const USpringArmComponent* A = ArmOf(X);
			const float L = A ? A->TargetArmLength : -1.f;   // дистанция после колеса
			const bool bZoom = A && FMath::IsNearlyEqual(L, 450.f, 1.f);
			const bool bSame = A && FMath::IsNearlyEqual(L, 600.f, 0.5f);
			X.Expect(bExpectZoom ? bZoom : bSame, FString::Printf(TEXT("%s: колесо вверх в (%.0f,%.0f) — дистанция камеры 600 → %.0f (ждём %s)"),
				*What, X.WheelPt.X, X.WheelPt.Y, L, bExpectZoom ? TEXT("450 — зум, как у эталона") : TEXT("600 — курсор над окном, зума нет")));
		}, 2);
	}

	// ══════ ★ 10.10.2026 (modal_lock3, МЕЛК-1…4 ревизии `modal_lock2_REVIEW.md`): прокрутка колесом, вращение камеры, наведение ══════

	// Точка P накрыта видимым окном реестра, кроме Except (имя окна — в OutBy). Чат — зона без записи в реестре, его не видно.
	static bool CoveredByWindow(FCtx& X, const FVector2D& P, FName Except, FString& OutBy)
	{
		UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
		if (!WM) { return false; }
		for (const FName& Id : WM->GetRegisteredWindowIds())
		{
			if (Id == Except || !WM->IsWindowVisible(Id) || !WM->IsWindowHitAt(Id, P)) { continue; }
			OutBy = Id.ToString();
			return true;
		}
		return false;
	}
	// Точка над окном Win, где хит-тест окон ловит именно его (сетка 37×37 по вьюпорту).
	static bool WindowPoint(FCtx& X, FName Win, FVector2D& Out)
	{
		UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
		if (!WM || !GEngine || !GEngine->GameViewport) { return false; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		if (!Vp.IsValid()) { return false; }
		const FGeometry& G = Vp->GetTickSpaceGeometry();
		const FVector2D Org = G.GetAbsolutePosition();   // левый верх вьюпорта
		const FVector2D Sz = G.GetAbsoluteSize();        // размер вьюпорта
		for (int32 Iy = 2; Iy <= 38; ++Iy)
		{
			for (int32 Ix = 2; Ix <= 38; ++Ix)
			{
				const FVector2D P = Org + FVector2D(Sz.X * Ix / 40.f, Sz.Y * Iy / 40.f);
				if (WM->FindWindowCatchingAt(P) == Win) { Out = P; return true; }
			}
		}
		return false;
	}
	// Колесо настоящим событием Slate в точке P (Delta: +1 «от себя», −1 «к себе»); курсор ставится туда же.
	static void SendWheel(const FVector2D& P, float Delta)
	{
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		App.ProcessMouseMoveEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		App.ProcessMouseWheelOrGestureEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, Delta, FModifierKeysState()), nullptr);
	}

	// Прокрутка СПИСКА колесом (МЕЛК-2): Prep готовит список и отдаёт точку (false — пропуск с причиной в Why), Read — позиция
	//   прокрутки. Колесо — настоящим событием Slate, знак Delta. bExpectScroll — ждём сдвиг позиции (без модалки), иначе — на месте.
	//   Попутно: дистанция камеры не меняется (курсор над окном — зума нет ни в одном случае).
	static void AddListWheel(TSharedRef<FCtx> C, TFunction<bool(FCtx&, FVector2D&, FString&)> Prep, TFunction<float(FCtx&)> Read,
		float Delta, bool bExpectScroll, const FString& What)
	{
		AddDo(C, [Prep, Read, Delta, What](FCtx& X)
		{
			X.bScrollArmed = false;
			FVector2D P;
			FString Why;
			if (!Prep(X, P, Why)) { X.SkipCase(What + TEXT(": ") + Why); return; }
			if (USpringArmComponent* A = ArmOf(X)) { A->TargetArmLength = 600.f; }
			X.ScrollBefore = Read(X);
			X.ScrollPt = P;
			X.bScrollArmed = true;
			SendWheel(P, Delta);
		}, 4);
		AddDo(C, [Read, bExpectScroll, What](FCtx& X)
		{
			if (!X.bScrollArmed) { return; }
			X.bScrollArmed = false;
			const float After = Read(X);   // позиция после колеса
			const bool bMoved = !FMath::IsNearlyEqual(After, X.ScrollBefore, 0.5f);
			const USpringArmComponent* A = ArmOf(X);
			const bool bNoZoom = !A || FMath::IsNearlyEqual(A->TargetArmLength, 600.f, 0.5f);
			X.Expect((bExpectScroll ? bMoved : !bMoved) && bNoZoom, FString::Printf(TEXT("%s: колесо в (%.0f,%.0f) — позиция списка %.1f → %.1f (ждём %s), дистанция камеры %.0f (ждём 600)"),
				*What, X.ScrollPt.X, X.ScrollPt.Y, X.ScrollBefore, After, bExpectScroll ? TEXT("сдвиг") : TEXT("на месте"), A ? A->TargetArmLength : -1.f));
		}, 2);
	}
	// Сумка: ряд 0, центр ячейки 0 сетки (над лотком), колесо «к себе» = ряд вниз.
	static void AddBagWheel(TSharedRef<FCtx> C, bool bExpectScroll, const FString& What)
	{
		AddListWheel(C, [](FCtx& X, FVector2D& P, FString& Why) -> bool
		{
			UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>();
			if (!I || !I->IsWindowOpen()) { Why = TEXT("сумка не открыта"); return false; }
			if (I->GetMaxScrollRow() <= 0) { Why = FString::Printf(TEXT("у сетки сумки нечего листать (предел ряда %d)"), I->GetMaxScrollRow()); return false; }
			I->SetScrollRow(0);
			const UWidget* Ic = I->DebugGetGridIcon(0);
			FVector2D Mn, Mx;
			if (!Ic || !AsdaUi::IsShown(Ic) || !AsdaUi::WidgetAbsRect(Ic, Mn, Mx)) { Why = TEXT("ячейка 0 сетки не отрисована"); return false; }
			P = (Mn + Mx) * 0.5f;
			FString By;
			if (CoveredByWindow(X, P, kInven, By)) { Why = TEXT("ячейку накрывает окно ") + By; return false; }
			return true;
		},
		[](FCtx& X) -> float { const UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>(); return I ? (float)I->GetScrollRow() : -1.f; },
		-1.f, bExpectScroll, What + TEXT(", сетка сумки 400"));
		// Ряд — снова 0: ячейка 0 сетки (предмет теста) остаётся на виду для ПКМ и подсказки.
		AddDo(C, [](FCtx& X) { if (UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>()) { I->SetScrollRow(0); } }, 3);
	}
	// Лента чата 2521: в конец, колесо «от себя» = к старым строкам (смещение уменьшается). Строк добавляется столько, чтобы
	//   лента прокручивалась (служебный канал, только офлайн).
	static void AddChatWheel(TSharedRef<FCtx> C, bool bExpectScroll, const FString& What)
	{
		AddDo(C, [](FCtx& X)
		{
			UAsdaChatWindowSubsystem* Cw = X.Sub<UAsdaChatWindowSubsystem>();
			if (!Cw) { return; }
			float Off = 0.f, End = 0.f;   // смещение ленты и смещение её конца
			FVector2D Cn;                 // центр ленты
			if (!Cw->DebugLinesScroll(Off, End, Cn) || End < 40.f)
			{
				for (int32 i = 0; i < 40; ++i) { Cw->AddChatLine(EAsdaChatCat::Service, FString::Printf(TEXT("modal_lock3: строка самотеста колеса %d"), i + 1)); }
			}
			Cw->DebugLinesToEnd();
		}, 6);
		AddListWheel(C, [](FCtx& X, FVector2D& P, FString& Why) -> bool
		{
			UAsdaChatWindowSubsystem* Cw = X.Sub<UAsdaChatWindowSubsystem>();
			float Off = 0.f, End = 0.f;   // смещение ленты и смещение её конца
			if (!Cw || !Cw->DebugLinesScroll(Off, End, P)) { Why = TEXT("лента чата 2521 не отрисована"); return false; }
			if (End < 1.f || Off < 1.f) { Why = FString::Printf(TEXT("ленте чата нечего листать (смещение %.1f, конец %.1f)"), Off, End); return false; }
			FString By;
			if (CoveredByWindow(X, P, FName(TEXT("CHAT_NEW")), By)) { Why = TEXT("ленту накрывает окно ") + By; return false; }   // само окно чата 2500 — не помеха
			return true;
		},
		[](FCtx& X) -> float
		{
			const UAsdaChatWindowSubsystem* Cw = X.Sub<UAsdaChatWindowSubsystem>();
			float Off = -1.f, End = 0.f;   // смещение ленты и смещение её конца
			FVector2D Cn;                  // центр ленты
			if (Cw) { Cw->DebugLinesScroll(Off, End, Cn); }
			return Off;
		},
		+1.f, bExpectScroll, What + TEXT(", лента чата 2521"));
	}

	// Подсказка предмета над ячейкой 0 сумки (МЕЛК-4). bRmb — всё это время зажата ПКМ (нажата настоящим событием Slate).
	//   bExpectTip — ждём подсказку (без модалки, контроль). Под модалкой проверяется и сам гейт опроса окна сумки.
	static void AddBagTip(TSharedRef<FCtx> C, bool bRmb, bool bExpectTip, const FString& What)
	{
		AddDo(C, [bRmb, What](FCtx& X)
		{
			X.bTipArmed = false;
			UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>();
			if (I) { I->SetScrollRow(0); }   // ячейка 0 сетки — предмет теста
			const UWidget* Ic = I ? I->DebugGetGridIcon(0) : nullptr;
			FVector2D Mn, Mx;
			if (!I || !I->IsWindowOpen() || !Ic || !AsdaUi::IsShown(Ic) || !AsdaUi::WidgetAbsRect(Ic, Mn, Mx)) { X.SkipCase(What + TEXT(": ячейка 0 сумки не отрисована")); return; }
			const FVector2D P = (Mn + Mx) * 0.5f;
			FString By;
			if (CoveredByWindow(X, P, kInven, By)) { X.SkipCase(What + TEXT(": ячейку накрывает окно ") + By); return; }
			// Курсор сначала уводится с ячейки (подсказка прошлых шагов гаснет), на следующем шаге — наводится.
			FSlateApplication& App = FSlateApplication::Get();
			const FVector2D Away = P + FVector2D(0.f, -400.f);
			App.SetCursorPos(Away);
			App.ProcessMouseMoveEvent(FPointerEvent(0, Away, Away, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
			X.TipPt = P;
			X.bTipArmed = true;
			X.bTipRmb = bRmb;
		}, 10);
		AddDo(C, [](FCtx& X)
		{
			if (!X.bTipArmed) { return; }
			FSlateApplication& App = FSlateApplication::Get();
			const FVector2D P = X.TipPt;
			TSet<FKey> Btn;   // зажатые кнопки события
			App.SetCursorPos(P);
			if (X.bTipRmb)
			{
				Btn.Add(EKeys::RightMouseButton);
				App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, P, P, Btn, EKeys::RightMouseButton, 0.f, FModifierKeysState()));
			}
			App.ProcessMouseMoveEvent(FPointerEvent(0, P, P + FVector2D(1.f, 0.f), Btn, EKeys::Invalid, 0.f, FModifierKeysState()));
		}, 30);
		AddDo(C, [bExpectTip, What](FCtx& X)
		{
			if (!X.bTipArmed) { return; }
			X.bTipArmed = false;
			const UAsdaItemTooltipSubsystem* T = X.Sub<UAsdaItemTooltipSubsystem>();
			const bool bTip = T && T->IsTooltipShown();
			const bool bBlocked = AsdaUi::IsModalDispatchBlockedFor(X.GI(), kInven);   // гейт покадрового опроса сумки в этом кадре
			const bool bRmbNow = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::RightMouseButton);
			X.Expect((bExpectTip ? bTip : !bTip) && (bExpectTip || bBlocked) && (!X.bTipRmb || bRmbNow),
				FString::Printf(TEXT("%s: курсор над ячейкой 0 сумки%s — подсказка %s (ждём %s), опрос сумки заперт=%d, ПКМ зажата=%d"), *What,
					X.bTipRmb ? TEXT(" с зажатой ПКМ") : TEXT(""), bTip ? TEXT("ПОКАЗАНА") : TEXT("нет"), bExpectTip ? TEXT("показана") : TEXT("нет"),
					bBlocked ? 1 : 0, bRmbNow ? 1 : 0));
			FSlateApplication& App = FSlateApplication::Get();
			if (X.bTipRmb)
			{
				App.ProcessMouseButtonUpEvent(FPointerEvent(0, X.TipPt, X.TipPt, TSet<FKey>(), EKeys::RightMouseButton, 0.f, FModifierKeysState()));
			}
			const FVector2D Away = X.TipPt + FVector2D(0.f, -400.f);
			App.SetCursorPos(Away);
			App.ProcessMouseMoveEvent(FPointerEvent(0, Away, Away, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		}, 6);
	}

	// Вращение камеры ПКМ-проводкой (МЕЛК-1): ПКМ нажата над миром, kOrbitSteps событий перемещения по kOrbitDx пикселей (по
	//   одному на кадр), отпускание. Результат — поворот `CamYawDeg` (`GetCameraYawUE`) в поле Out (NaN — не проверено).
	static constexpr int32 kOrbitSteps = 10;     // событий перемещения
	static constexpr float kOrbitDx = 4.f;       // пикселей за событие
	//   bNoSmooth — на время проводки выключить сглаживание мыши (`UInputSettings::bEnableMouseSmoothing`, читается движком на
	//   каждом кадре): без него поворот детерминирован (чувствительность × поправка на угол обзора), и пара сравнивается точно;
	//   со сглаживанием итог зависит от темпа кадров (заполнение пропусков `SmoothMouse`).
	static void AddOrbit(TSharedRef<FCtx> C, float FCtx::* Out, const FString& What, bool bNoSmooth = false)
	{
		AddDo(C, [Out, What, bNoSmooth](FCtx& X)
		{
			X.bOrbitArmed = false;
			X.*Out = NAN;
			X.bSmoothWas = GetDefault<UInputSettings>()->bEnableMouseSmoothing;
			if (bNoSmooth) { GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing = false; }
			AAsdaPlayerController* P = X.PC();
			FVector2D Pt;
			if (!P || !ArmOf(X) || !WorldPoint(X, Pt))
			{
				GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing = X.bSmoothWas;
				X.SkipCase(What + TEXT(": нет своего павна или точки мира — вращение не проверено"));
				return;
			}
			X.OrbitPt = Pt;
			X.OrbitYaw0 = P->GetCameraYawUE();
			X.bOrbitArmed = true;
			FSlateApplication& App = FSlateApplication::Get();
			App.SetCursorPos(Pt);
			App.ProcessMouseMoveEvent(FPointerEvent(0, Pt, Pt, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
			TSet<FKey> Btn;   // зажатые кнопки события
			Btn.Add(EKeys::RightMouseButton);
			App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, Pt, Pt, Btn, EKeys::RightMouseButton, 0.f, FModifierKeysState()));
		}, 3);
		for (int32 k = 0; k < kOrbitSteps; ++k)
		{
			C->Steps.Add([k](FCtx& X) -> int32
			{
				if (!X.bOrbitArmed) { return 1; }
				const FVector2D Prev = X.OrbitPt + FVector2D(kOrbitDx * k, 0.f);   // курсор до события
				const FVector2D Cur = Prev + FVector2D(kOrbitDx, 0.f);             // курсор после события
				TSet<FKey> Btn;   // зажатые кнопки события
				Btn.Add(EKeys::RightMouseButton);
				FSlateApplication& App = FSlateApplication::Get();
				App.SetCursorPos(Cur);
				App.ProcessMouseMoveEvent(FPointerEvent(0, Cur, Prev, Btn, EKeys::Invalid, 0.f, FModifierKeysState()));
				return 1;
			});
		}
		AddDo(C, [Out, What](FCtx& X)
		{
			if (!X.bOrbitArmed) { return; }
			X.bOrbitArmed = false;
			const FVector2D End = X.OrbitPt + FVector2D(kOrbitDx * kOrbitSteps, 0.f);
			AAsdaPlayerController* P = X.PC();
			const float Yaw = P ? FMath::FindDeltaAngleDegrees(X.OrbitYaw0, P->GetCameraYawUE()) : NAN;   // поворот за проводку
			FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(0, End, End, TSet<FKey>(), EKeys::RightMouseButton, 0.f, FModifierKeysState()));
			GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing = X.bSmoothWas;   // настройку — как была
			X.*Out = Yaw;
			UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК [%s] %s: проводка ПКМ %d×%.0f px от (%.0f,%.0f) — поворот камеры %.3f°"),
				*X.Phase, *What, kOrbitSteps, kOrbitDx, X.OrbitPt.X, X.OrbitPt.Y, Yaw);
		}, 8);
	}
	// Временный обработчик колеса у модалки 1559 (список «внутри модалки»: у порта ни у одной модалки своего списка с колесом
	//   нет — проверяется сам путь колеса до модалки). Считает вызовы; снимается `ModalWheelProbeOff`.
	static void ModalWheelProbeOn(FCtx& X)
	{
		UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
		if (!WM) { return; }
		X.ModalWheelCount = 0;
		int32* Cnt = &X.ModalWheelCount;   // счётчик в контексте: контекст живёт дольше обработчика (снимается в том же прогоне)
		WM->RegisterWheelHandler(kBox, FAsdaUiWheelHandler::CreateLambda([Cnt](int32) -> bool { ++*Cnt; return true; }));
	}
	static void ModalWheelProbeOff(FCtx& X)
	{
		if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->UnregisterWheelHandler(kBox); }
	}

	// Цель над ячейкой/ручкой сумки не накрыта ни одним видимым окном выше сумки (кроме модалки 1559 — её проверяют отдельно).
	static bool BagPointFree(FCtx& X, const FVector2D& P, FString& OutBy)
	{
		UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
		if (!WM) { return false; }
		const int32 Zi = WM->GetWindowZOrder(kInven);
		for (const FName& Id : WM->GetRegisteredWindowIds())
		{
			if (Id == kInven || !WM->IsWindowVisible(Id) || WM->GetWindowZOrder(Id) <= Zi || !WM->IsWindowHitAt(Id, P)) { continue; }
			OutBy = Id.ToString();
			return false;
		}
		return true;
	}

	// ПКМ НАСТОЯЩИМ событием Slate (`SimulateClickAt`, правая кнопка) по ячейке 0 сетки сумки. Эталон: событие 0xE по непустой
	//   ячейке открывает меню 28000 «похвастаться» (ui_core.c:48036). bExpectMenu — ждём меню.
	static void AddRmbCell(TSharedRef<FCtx> C, bool bExpectMenu, const FString& What)
	{
		AddDo(C, [What](FCtx& X)
		{
			X.bRmbArmed = false;
			UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>();
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			UAsdaShowOffSubsystem* SO = X.Sub<UAsdaShowOffSubsystem>();
			if (!I || !WM || !SO || !I->IsWindowOpen()) { X.SkipCase(What + TEXT(": сумка не открыта — ПКМ не проверен")); return; }
			if (SO->IsMenuOpen()) { SO->CloseMenu(); }
			I->SetScrollRow(0);   // modal_lock3: после колеса по сетке — снова ряд 0 (в ячейке 0 предмет теста)
			const UWidget* Ic = I->DebugGetGridIcon(0);
			FVector2D Mn, Mx;
			if (!Ic || !AsdaUi::IsShown(Ic) || !AsdaUi::WidgetAbsRect(Ic, Mn, Mx) || (Mx - Mn).IsNearlyZero()) { X.SkipCase(What + TEXT(": ячейка 0 сетки не отрисована")); return; }
			const FVector2D P = (Mn + Mx) * 0.5f;
			FString By;
			if (WM->IsWindowVisible(kBox) && WM->IsWindowHitAt(kBox, P)) { X.SkipCase(What + TEXT(": ячейку накрывает сама 1559 — ПКМ дошёл бы до неё, не проверен")); return; }
			if (!BagPointFree(X, P, By)) { X.SkipCase(FString::Printf(TEXT("%s: ячейку накрывает окно «%s»"), *What, *By)); return; }
			X.RmbPt = P;
			X.bRmbArmed = true;
			WM->SimulateClickAt(P, 3, /*bRightButton*/ true);
		}, 12);
		AddDo(C, [bExpectMenu, What](FCtx& X)
		{
			if (!X.bRmbArmed) { return; }
			X.bRmbArmed = false;
			UAsdaShowOffSubsystem* SO = X.Sub<UAsdaShowOffSubsystem>();
			const bool bOpen = SO && SO->IsMenuOpen();
			X.Expect(bExpectMenu ? bOpen : !bOpen, FString::Printf(TEXT("%s: ПКМ по ячейке 0 сумки (%.0f,%.0f) — меню 28000 %s (ждём %s)"), *What,
				X.RmbPt.X, X.RmbPt.Y, bOpen ? TEXT("ОТКРЫТО") : TEXT("не открыто"), bExpectMenu ? TEXT("открыто") : TEXT("не открыто")));
			if (bOpen) { SO->CloseMenu(); }
		}, 4);
	}

	// Перетаскивание окна НАСТОЯЩИМИ событиями Slate (`SimulateDrag`) за первую отрисованную ручку на Dx. bExpectMove — ждём сдвиг.
	static void AddWinDrag(TSharedRef<FCtx> C, FName Win, float Dx, bool bExpectMove, const FString& What)
	{
		AddDo(C, [Win, Dx, What](FCtx& X)
		{
			X.bDragArmed = false;
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			FVector2D Mn, Mx;
			bool bR = false;   // ручка нашлась
			for (int32 i = 0; i < 8 && WM && !bR; ++i) { bR = WM->DebugGetDragHandleRect(Win, i, Mn, Mx); }
			if (!WM || !bR || !WM->IsWindowVisible(Win))
			{
				// Второй метод (закон «нет = ошибка метода»): сводка ручек реестра — у окна их нет вовсе или нет виджета.
				if (WM) { UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК [%s] сводка ручек: %s"), *What, *WM->DebugDragHandles()); }
				X.SkipCase(FString::Printf(TEXT("%s: у окна «%s» нет отрисованной ручки"), *What, *Win.ToString()));
				return;
			}
			const FVector2D P = (Mn + Mx) * 0.5f;
			if (Win != kBox && WM->IsWindowVisible(kBox) && WM->IsWindowHitAt(kBox, P)) { X.SkipCase(What + TEXT(": ручку накрывает 1559 — не проверено")); return; }
			X.HandleBefore = Mn;
			X.bDragArmed = true;
			WM->SimulateDrag(P, P + FVector2D(Dx, 0.f), /*HoldFrames*/ 12);
		}, 26);
		AddDo(C, [Win, Dx, bExpectMove, What](FCtx& X)
		{
			if (!X.bDragArmed) { return; }
			X.bDragArmed = false;
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			FVector2D Mn(0.f, 0.f), Mx(0.f, 0.f);
			bool bR = false;
			for (int32 i = 0; i < 8 && WM && !bR; ++i) { bR = WM->DebugGetDragHandleRect(Win, i, Mn, Mx); }
			const float Moved = bR ? (Mn.X - X.HandleBefore.X) : -9999.f;   // сдвиг ручки по X
			X.Expect(bR && (bExpectMove ? FMath::Abs(Moved - Dx) <= 3.f : FMath::Abs(Moved) <= 1.f),
				FString::Printf(TEXT("%s: перетаскивание «%s» за ручку на %+.0f — сдвиг %+.0f (ждём %s)"), *What, *Win.ToString(), Dx, Moved,
					bExpectMove ? TEXT("сдвиг") : TEXT("на месте")));
		}, 4);
	}

	// Центр ячейки сетки книги (корень клона 74020).
	static bool BookCellCenter(FCtx& X, int32 Cell, FVector2D& Out)
	{
		const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
		const UWidget* R = B ? B->DebugCellRoot(Cell) : nullptr;
		FVector2D Mn, Mx;
		if (!R || !AsdaUi::IsShown(R) || !AsdaUi::WidgetAbsRect(R, Mn, Mx) || (Mx - Mn).IsNearlyZero()) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Двойной щелчок НАСТОЯЩИМИ событиями Slate: второй симклик — как только первый доставлен (`IsSimClickPending`), в пределах
	//   `GetDoubleClickTime` (тот же приём, что у `asda.FishBook.SelfTest`).
	static void AddDoubleClick(TSharedRef<FCtx> C, TFunction<bool(FCtx&, FVector2D&)> PointOf, const FString& What)
	{
		AddDo(C, [PointOf, What](FCtx& X)
		{
			X.bDblArmed = false;
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			FVector2D P;
			if (!WM || !PointOf(X, P)) { X.SkipCase(What + TEXT(": точка не отрисована — двойной щелчок не проверен")); return; }
			X.bDblArmed = true;
			WM->SimulateClickAt(P, /*HoldFrames*/ 1);
			TWeakObjectPtr<UAsdaWindowSubsystem> WW(WM);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, P](float) -> bool
			{
				UAsdaWindowSubsystem* W2 = WW.Get();
				if (!W2) { return false; }
				if (W2->IsSimClickPending()) { return true; }   // первый ещё не доставлен
				W2->SimulateClickAt(P, /*HoldFrames*/ 1);
				return false;
			}), 0.f);
		}, 30);
	}
	// Тело 6174 (том 305 б): @0 индекс тома, @4 тип, @125 счёт[30], @185 макс.[30], @245 мин.[30] (как у `asda.FishBook.SelfTest`).
	static TArray<uint8> Page305(int32 Idx, const TArray<int32>& Cnt)
	{
		TArray<uint8> B;
		B.SetNumZeroed(305);
		AsdaBytes::WrI32(B.GetData(), Idx);
		for (int32 i = 0; i < 30; ++i)
		{
			const int32 N = Cnt.IsValidIndex(i) ? Cnt[i] : 0;   // счёт рыбы i
			AsdaBytes::WrI16(B.GetData() + 125 + 2 * i, N);
			AsdaBytes::WrI16(B.GetData() + 185 + 2 * i, N ? 40 : 0);
			AsdaBytes::WrI16(B.GetData() + 245 + 2 * i, N ? 10 : 0);
		}
		return B;
	}

	// Итог.
	static void Finish(FCtx& X)
	{
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): все ключи прогона (HairShop.LegacyConfirmPanel, UI.ModalLock, UI.ModalLockRightClick) — к исходному
		//   значению И приоритету SetBy. Прежде возвращался только LegacyConfirmPanel (с SetByConsole; если раздел M не дошёл —
		//   нулём по умолчанию поля), ModalLock — константой 1.
		AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestModalLock"));
		if (X.CapGen) { if (UAsdaPetSubsystem* Pet = X.Sub<UAsdaPetSubsystem>()) { Pet->EndSendCapture(); } }
		UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК ТАБЛИЦА (%d строк):"), X.Table.Num());
		for (const FString& L : X.Table) { UE_LOG(LogAsdaModalLockTest, Log, TEXT("[МОДАЛ-ТАБЛ] %s"), *L); }
		UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК ИТОГ: ok=%d fail=%d пропуск=%d, перехвачено отправок %d"), X.Ok, X.Fail, X.Skip, X.Sent);
		if (X.bQuit && GEngine && X.W.Get()) { GEngine->Exec(X.W.Get(), TEXT("quit")); }
	}

	static void Build(TSharedRef<FCtx> C)
	{
		// ── Подготовка: клавиши из раскладки ──
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("подготовка");
			// ★ 25.10.2026: самотест проверяет ЭТАЛОННЫЙ замок (Enter под модалкой не открывает чат и ничего не жмёт). С новым
			//   механизмом «Enter = кнопка согласия» Enter под 1559 нажал бы «Подтвердить» — на время прогона он выключен (его проверяет
			//   свой самотест `asda.SelfTest.EnterConfirm`); ключ вернёт `AsdaCVarSave::RestoreAll` в конце.
			AsdaCVarSave::SetForTest(TEXT("asda.UI.EnterConfirm"), 0);
			AAsdaPlayerController* P = X.PC();
			FAsdaKeyBind B;
			X.KeyInv = (P && P->Keys && P->Keys->GetBind(33, B) && B.Vk != 0 && B.Mod == EAsdaKeyMod::None) ? UAsdaKeybindConfig::VkToFKey(B.Vk) : FKey();
			X.KeyEnter = (P && P->Keys && P->Keys->GetBind(66, B) && B.Vk != 0 && B.Mod == EAsdaKeyMod::None) ? UAsdaKeybindConfig::VkToFKey(B.Vk) : FKey();
			X.Expect(X.KeyInv.IsValid() && X.KeyEnter.IsValid(), FString::Printf(TEXT("раскладка: команда 33 (сумка) = «%s», команда 66 (чат) = «%s»"),
				*X.KeyInv.ToString(), *X.KeyEnter.ToString()));
			ChatClose(X);
			InvClose(X);
		}, 4);

		// ── K. Контроль: модалки нет ──
		// ★ 22.10.2026 (изоляция в пакете): окна, оставленные предыдущим самотестом пакета (журнал заданий QUEST_WIN после
		//   `asda.SelfTest.EntryWindows`), накрывали кнопку 73402 и ячейки сумки — щелчки и колесо сценария попадали не туда.
		AddDo(C, [](FCtx& X)
		{
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>())
			{
				for (const TCHAR* Id : { TEXT("QUEST_WIN"), TEXT("COMMUNITY_WIN") }) { if (WM->IsWindowVisible(FName(Id))) { WM->CloseWindow(FName(Id)); } }
				// И любое иное чужое окно, которое ловит мышь в точке кнопки 73402 (сценарий щёлкает именно туда).
				FVector2D P;
				if (CenterOf(X, kSysMenu, TEXT("NEW_SYSTEMMAIN_ITEMBUT"), P))
				{
					for (int32 i = 0; i < 6; ++i)
					{
						const FName Top = WM->FindWindowCatchingAt(P);
						if (Top.IsNone() || Top == kSysMenu || !WM->IsWindowVisible(Top)) { break; }
						UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК [изоляция] кнопку 73402 накрывает окно «%s» от прошлого самотеста — закрыто"), *Top.ToString());
						WM->CloseWindow(Top);
					}
				}
			}
		}, 3);
		AddDo(C, [](FCtx& X) { X.Phase = TEXT("K контроль"); X.Expect(!ModalUp(X), TEXT("модалок нет (IsAnyModalOpen = 0)")); PetGateCheck(X, TEXT("без модалки")); });
		AddKeyChecks(C, /*bLocked*/false, TEXT("без модалки"));

		// ── M. Парикмахерская → 1559 настоящим щелчком ──
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("M модалка 1559");
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное значение ключа запоминает `AsdaCVarSave` (прежде — поле HairLegacyWas).
			AsdaCVarSave::SetForTest(TEXT("asda.HairShop.LegacyConfirmPanel"), 0);   // путь по умолчанию: «Купить» сразу открывает 1559
			if (UAsdaHairShopSubsystem* H = X.Sub<UAsdaHairShopSubsystem>()) { H->OpenWindow(); }
		}, 30);
		AddDo(C, [](FCtx& X)
		{
			X.BoxShowsBefore = BoxShows(X);
			FVector2D P;
			if (!HairOpen(X) || !CenterOf(X, kHair, TEXT("HAIR_ITEM_BUY01"), P)) { X.SkipCase(TEXT("парикмахерская/кнопка «Купить» 01 не отрисованы")); return; }
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->SimulateClickAt(P, 3); }
		}, 20);
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			X.Expect(BoxOpen(X) && BoxShows(X) == X.BoxShowsBefore + 1 && ModalUp(X),
				FString::Printf(TEXT("настоящий щелчок «Купить» 01 → модалка 1559 показана (z=%d), IsAnyModalOpen = %d"),
					WM ? WM->GetWindowZOrder(kBox) : -1, ModalUp(X) ? 1 : 0));
		});
		// Мышь окнам под модалкой не отдаётся — и хит-тест порта, и Slate.
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			FVector2D P;
			if (!WM || !CenterOf(X, kHair, TEXT("HAIR_ITEM_BUY01"), P)) { return; }
			FVector2D Q;
			const bool bQ = CenterOf(X, kBox, TEXT("PURCHASE_BTN02"), Q);
			const FName AtBox = bQ ? WM->FindWindowCatchingAt(Q) : NAME_None;
			X.Expect(bQ && AtBox == kBox, FString::Printf(TEXT("хит-тест окон: над «Отмена» 1562 ловит «%s» (ждём 1559)"), *AtBox.ToString()));
			X.Expect(AsdaUi::IsModalDispatchBlocked(X.GI()) && AsdaUi::IsModalDispatchBlockedFor(X.GI(), kHair),
				TEXT("покадровый опрос мыши окон под модалкой заперт (IsModalDispatchBlocked, в т.ч. для окна 15000)"));
			PetGateCheck(X, TEXT("под 1559"));
			// ★ В1-B (U9): ключ отката гейта действует и на окна питомцев (как на все прочие окна с покадровым опросом).
			AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalGatePolling"), 0);
			X.Expect(!AsdaUi::IsModalDispatchBlocked(X.GI()) && ModalUp(X),
				TEXT("ключ asda.UI.ModalGatePolling 0: гейт опроса снят (в т.ч. у окон питомцев), модалка 1559 при этом стоит"));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalGatePolling"), 1);
		});
		// Кнопку 73402 здесь накрывает окно 15000 — щелчок по ней проверяется в разделе M2 (1559 без магазина под ней).
		AddKeyChecks(C, /*bLocked*/true, TEXT("под 1559"), /*bMouse*/false);
		AddHairBuyClick(C, /*bExpectOpen*/false, TEXT("под 1559"));
		// Ключ диагностики: замок снят — IsAnyModalOpen = 0, ESC-меню по-прежнему заперто (корневая роспись, как до правки).
		AddDo(C, [](FCtx& X)
		{
			IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.ModalLock"));
			if (!Cv) { X.SkipCase(TEXT("ключа asda.UI.ModalLock нет")); return; }
			AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalLock"), 0);
			FString Why;
			const bool bModal0 = ModalUp(X), bEsc0 = EscMenuBlocked(X, Why);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalLock"), 1);
			const bool bModal1 = ModalUp(X);
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->NotifyModalStateChanged(); }
			X.Expect(!bModal0 && bEsc0 && bModal1, FString::Printf(TEXT("ключ asda.UI.ModalLock: 0 → IsAnyModalOpen=%d (ждём 0), ESC-меню заперто=%d (ждём 1); 1 → IsAnyModalOpen=%d (ждём 1)"),
				bModal0 ? 1 : 0, bEsc0 ? 1 : 0, bModal1 ? 1 : 0));
		}, 4);
		// «Отмена» 1562 настоящим щелчком — модалке мышь доходит.
		AddDo(C, [](FCtx& X)
		{
			FVector2D P;
			if (!CenterOf(X, kBox, TEXT("PURCHASE_BTN02"), P)) { X.SkipCase(TEXT("кнопка «Отмена» 1562 не отрисована")); return; }
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->SimulateClickAt(P, 3); }
		}, 16);
		AddDo(C, [](FCtx& X)
		{
			X.Expect(!BoxOpen(X) && !ModalUp(X) && HairOpen(X), FString::Printf(TEXT("настоящий щелчок «Отмена» 1562 дошёл до модалки: 1559 закрыта=%d, замок снят=%d, окно 15000 осталось=%d"),
				BoxOpen(X) ? 0 : 1, ModalUp(X) ? 0 : 1, HairOpen(X) ? 1 : 0));
		}, 4);

		// ── A1. После закрытия 1559 щелчок по карточке магазина снова доходит ──
		AddDo(C, [](FCtx& X) { X.Phase = TEXT("A1 после 1559"); });
		AddHairBuyClick(C, /*bExpectOpen*/true, TEXT("после 1559"));
		AddDo(C, [](FCtx& X)
		{
			if (UAsdaPurchaseBoxSubsystem* B = X.Sub<UAsdaPurchaseBoxSubsystem>()) { if (B->IsOpen()) { B->OnCancelClicked(); } }
			if (UAsdaHairShopSubsystem* H = X.Sub<UAsdaHairShopSubsystem>()) { if (H->IsSessionOpen()) { H->CloseWindow(); } }
		}, 10);

		// ── M2. 1559 без магазина под ней (штатный `Open`, как у кнопок режимов A…G): щелчок по 73402 съеден ──
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("M2 1559 без магазина");
			UAsdaPurchaseBoxSubsystem* B = X.Sub<UAsdaPurchaseBoxSubsystem>();
			if (!B) { return; }
			FAsdaPurchaseBoxRequest Req;
			Req.Mode = EAsdaPurchaseMode::HairShop;
			Req.ItemCode = 76452;   // «Сброс фракции аккаунта» (special_itemlist) — любой существующий код; в сеть ничего не уходит
			Req.Quantity = 1;
			Req.TradeMode = 1;
			B->Open(Req, [](bool) {});
		}, 10);
		AddDo(C, [](FCtx& X)
		{
			X.Expect(BoxOpen(X) && ModalUp(X) && !HairOpen(X), FString::Printf(TEXT("1559 показана без магазина: открыта=%d, IsAnyModalOpen=%d"),
				BoxOpen(X) ? 1 : 0, ModalUp(X) ? 1 : 0));
		});
		AddSysMenuClick(C, /*bLocked*/true, TEXT("под 1559"));
		AddDo(C, [](FCtx& X)
		{
			FVector2D P;
			if (!CenterOf(X, kBox, TEXT("PURCHASE_BTN02"), P)) { X.SkipCase(TEXT("кнопка «Отмена» 1562 не отрисована")); return; }
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->SimulateClickAt(P, 3); }
		}, 16);
		AddDo(C, [](FCtx& X)
		{
			X.Expect(!BoxOpen(X) && !ModalUp(X), FString::Printf(TEXT("щелчок «Отмена» 1562 закрыл 1559=%d, замок снят=%d"), BoxOpen(X) ? 0 : 1, ModalUp(X) ? 0 : 1));
		}, 4);

		// ── A2. После закрытия — всё снова работает (те же проверки, что в K) ──
		AddDo(C, [](FCtx& X) { X.Phase = TEXT("A2 после 1559"); });
		AddKeyChecks(C, /*bLocked*/false, TEXT("после 1559"));

		// ── M3 (modal_lock2, СРЕДН-2). Сумка открыта: ПКМ, перетаскивание, колесо — без модалки (контроль) и под 1559 ──
		// Точка «над сумкой» — центр ячейки 0 сетки.
		auto BagCellPoint = [](FCtx& X, FVector2D& P) -> bool
		{
			const UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>();
			const UWidget* Ic = I ? I->DebugGetGridIcon(0) : nullptr;
			FVector2D Mn, Mx;
			if (!Ic || !AsdaUi::IsShown(Ic) || !AsdaUi::WidgetAbsRect(Ic, Mn, Mx)) { return false; }
			P = (Mn + Mx) * 0.5f;
			return true;
		};
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("M3 мышь без модалки");
			UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>();
			if (!I) { return; }
			ChatClose(X);
			if (!I->IsWindowOpen()) { I->OpenWindow(); }
			I->SetScrollRow(0);
			X.PutCont = I->GetGridContainer();
			if (I->GetCellItemCode(X.PutCont, 0) == 0) { X.bPutByTest = I->DebugPutItem(X.PutCont, 0, kTestItem, 1); }
		}, 12);
		AddDo(C, [](FCtx& X)
		{
			const UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>();
			const int32 Code = I ? I->GetCellItemCode(X.PutCont, 0) : -1;
			X.Expect(I && I->IsWindowOpen() && Code > 0, FString::Printf(TEXT("сумка открыта, в ячейке 0 контейнера %d предмет %d%s"),
				X.PutCont, Code, X.bPutByTest ? TEXT(" (положен тестом)") : TEXT("")));
		});
		AddRmbCell(C, /*bExpectMenu*/true, TEXT("без модалки"));
		AddWinDrag(C, kInven, 60.f, /*bExpectMove*/true, TEXT("без модалки"));
		AddWinDrag(C, kInven, -60.f, /*bExpectMove*/true, TEXT("без модалки, обратно"));
		AddWheel(C, WorldPoint, /*bExpectZoom*/true, TEXT("без модалки, над миром"));
		AddWheel(C, BagCellPoint, /*bExpectZoom*/false, TEXT("без модалки, над сумкой"));
		// modal_lock3 (МЕЛК-2): списки листаются колесом без модалки — контроль к парам под 1559.
		AddBagWheel(C, /*bExpectScroll*/true, TEXT("без модалки"));
		AddChatWheel(C, /*bExpectScroll*/true, TEXT("без модалки"));
		// modal_lock3 (МЕЛК-4): контроль — наведение на ячейку сумки без модалки показывает подсказку.
		AddBagTip(C, /*bRmb*/false, /*bExpectTip*/true, TEXT("без модалки"));
		// modal_lock3 (МЕЛК-1): поворот камеры ПКМ-проводкой без модалки — эталон для пары под 1559 с ключом 0.
		AddOrbit(C, &FCtx::OrbitNoModal, TEXT("без модалки"));
		AddOrbit(C, &FCtx::OrbitNoModalRaw, TEXT("без модалки, сглаживание мыши выключено"), /*bNoSmooth*/true);
		// 1559 штатным `Open` поверх открытой сумки.
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("M3 мышь под 1559");
			UAsdaPurchaseBoxSubsystem* B = X.Sub<UAsdaPurchaseBoxSubsystem>();
			if (!B) { return; }
			FAsdaPurchaseBoxRequest Req;
			Req.Mode = EAsdaPurchaseMode::HairShop;
			Req.ItemCode = 76452;   // как в M2: любой существующий код; в сеть ничего не уходит
			Req.Quantity = 1;
			Req.TradeMode = 1;
			B->Open(Req, [](bool) {});
		}, 10);
		AddDo(C, [](FCtx& X)
		{
			X.Expect(BoxOpen(X) && ModalUp(X) && InvShown(X), FString::Printf(TEXT("1559 поверх сумки: 1559=%d, IsAnyModalOpen=%d, сумка=%d"),
				BoxOpen(X) ? 1 : 0, ModalUp(X) ? 1 : 0, InvShown(X) ? 1 : 0));
		});
		AddRmbCell(C, /*bExpectMenu*/false, TEXT("под 1559 [ИСПРАВЛЕНИЕ]"));
		AddDo(C, [](FCtx& X)
		{
			if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.ModalLockRightClick"))) { X.RmbCvarWas = Cv->GetInt(); AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalLockRightClick"), 0); }
			else { X.SkipCase(TEXT("ключа asda.UI.ModalLockRightClick нет")); }
		});
		// modal_lock3 (МЕЛК-4): эталон окнам под модалкой ПКМ не отдаёт (нажатие — только `+0x1A0`, слот 3 vtable менеджера окон) —
		//   меню 28000 не открывается и при ключе отката; наведения и подсказки с зажатой ПКМ тоже нет.
		AddRmbCell(C, /*bExpectMenu*/false, TEXT("под 1559, ключ отката asda.UI.ModalLockRightClick 0 — окнам ПКМ не доходит, как у оригинала"));
		AddBagTip(C, /*bRmb*/true, /*bExpectTip*/false, TEXT("под 1559, ключ 0"));
		// modal_lock3 (МЕЛК-1): мир ПКМ получает — поворот камеры той же проводкой, что без модалки.
		AddOrbit(C, &FCtx::OrbitModalKey0, TEXT("под 1559, ключ 0"));
		AddOrbit(C, &FCtx::OrbitModalKey0Raw, TEXT("под 1559, ключ 0, сглаживание мыши выключено"), /*bNoSmooth*/true);
		AddDo(C, [](FCtx& X)
		{
			// Точная пара — без сглаживания: тот же путь `UPlayerInput` даёт тот же поворот.
			const float A = X.OrbitNoModalRaw, B = X.OrbitModalKey0Raw;   // повороты без модалки и под 1559 (ключ 0)
			if (FMath::IsNaN(A) || FMath::IsNaN(B)) { X.SkipCase(TEXT("пара вращения камеры без сглаживания не собрана")); }
			else
			{
				const float R = FMath::Abs(A) > KINDA_SMALL_NUMBER ? B / A : 0.f;   // отношение скоростей
				X.Expect(FMath::Abs(A) > 0.05f && R >= 0.97f && R <= 1.03f,
					FString::Printf(TEXT("ПКМ-проводка %d×%.0f px без сглаживания: поворот камеры без модалки %.3f°, под 1559 с ключом 0 — %.3f° (отношение %.3f, ждём 1 ± 0.03; прежний путь давал ~14)"),
						kOrbitSteps, kOrbitDx, A, B, R));
			}
			// Со сглаживанием (как у игрока) — тот же порядок величины; разброс — от темпа кадров.
			const float As = X.OrbitNoModal, Bs = X.OrbitModalKey0;   // то же со сглаживанием
			if (FMath::IsNaN(As) || FMath::IsNaN(Bs)) { X.SkipCase(TEXT("пара вращения камеры со сглаживанием не собрана")); return; }
			const float Rs = FMath::Abs(As) > KINDA_SMALL_NUMBER ? Bs / As : 0.f;
			X.Expect(FMath::Abs(As) > 0.05f && Rs >= 0.6f && Rs <= 1.6f,
				FString::Printf(TEXT("та же проводка со сглаживанием: без модалки %.3f°, под 1559 с ключом 0 — %.3f° (отношение %.2f, ждём 0.6…1.6 — разброс сглаживания; прежний путь давал ~14)"),
					As, Bs, Rs));
		});
		AddDo(C, [](FCtx& X)
		{
			AsdaCVarSave::SetForTest(TEXT("asda.UI.ModalLockRightClick"), X.RmbCvarWas);
			X.Expect(BoxOpen(X) && ModalUp(X), TEXT("после пары отката ПКМ: 1559 на месте, замок есть"));
		});
		// Ключ 1 (по умолчанию, [ИСПРАВЛЕНИЕ]): мир ПКМ под модалкой не получает — камера не поворачивается.
		AddOrbit(C, &FCtx::OrbitModalKey1, TEXT("под 1559, ключ 1"));
		AddDo(C, [](FCtx& X)
		{
			if (FMath::IsNaN(X.OrbitModalKey1)) { return; }
			X.Expect(FMath::Abs(X.OrbitModalKey1) < 0.001f, FString::Printf(TEXT("под 1559, ключ 1 [ИСПРАВЛЕНИЕ]: ПКМ-проводка — поворот камеры %.3f° (ждём 0)"), X.OrbitModalKey1));
		});
		AddWinDrag(C, kInven, 60.f, /*bExpectMove*/false, TEXT("под 1559"));
		AddWheel(C, WorldPoint, /*bExpectZoom*/true, TEXT("под 1559, над миром (эталон game_main.c:5864 — только «курсор не над окном»)"));
		AddWheel(C, [](FCtx& X, FVector2D& P) { return CenterOf(X, kBox, TEXT("PURCHASE_BTN02"), P); }, /*bExpectZoom*/false, TEXT("под 1559, над 1559"));
		// modal_lock3 (МЕЛК-2): списки окон под модалкой колесом не листаются.
		AddBagWheel(C, /*bExpectScroll*/false, TEXT("под 1559"));
		AddChatWheel(C, /*bExpectScroll*/false, TEXT("под 1559"));
		// Список внутри модалки: временный обработчик колеса у 1559 получает колесо ровно один раз, камера не зумит.
		AddDo(C, [](FCtx& X)
		{
			ModalWheelProbeOn(X);
			FVector2D P;
			if (!CenterOf(X, kBox, TEXT("PURCHASE_BTN02"), P)) { X.SkipCase(TEXT("1559 не отрисована — колесо внутри модалки не проверено")); ModalWheelProbeOff(X); return; }
			if (USpringArmComponent* A = ArmOf(X)) { A->TargetArmLength = 600.f; }
			X.ScrollPt = P;
			X.bScrollArmed = true;
			SendWheel(P, +1.f);
		}, 4);
		AddDo(C, [](FCtx& X)
		{
			if (!X.bScrollArmed) { return; }
			X.bScrollArmed = false;
			ModalWheelProbeOff(X);
			const USpringArmComponent* A = ArmOf(X);
			const float L = A ? A->TargetArmLength : -1.f;   // дистанция камеры после колеса
			X.Expect(X.ModalWheelCount == 1 && (!A || FMath::IsNearlyEqual(L, 600.f, 0.5f)),
				FString::Printf(TEXT("под 1559, колесо над самой 1559 (%.0f,%.0f): обработчик колеса модалки вызван %d раз (ждём 1), дистанция камеры %.0f (ждём 600)"),
					X.ScrollPt.X, X.ScrollPt.Y, X.ModalWheelCount, L));
		}, 2);
		AddDo(C, [](FCtx& X)
		{
			FVector2D P;
			if (!CenterOf(X, kBox, TEXT("PURCHASE_BTN02"), P)) { X.SkipCase(TEXT("кнопка «Отмена» 1562 не отрисована")); return; }
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->SimulateClickAt(P, 3); }
		}, 16);
		AddDo(C, [](FCtx& X)
		{
			X.Expect(!BoxOpen(X) && !ModalUp(X), FString::Printf(TEXT("щелчок «Отмена» 1562 закрыл 1559=%d, замок снят=%d"), BoxOpen(X) ? 0 : 1, ModalUp(X) ? 0 : 1));
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			X.Expect(WM && !WM->IsWorldInputCapturedByModal(), TEXT("после закрытия 1559: ввод мира модалкой не отобран (IsWorldInputCapturedByModal = 0)"));
		}, 4);
		// modal_lock3 (МЕЛК-2/3): после закрытия модалки колесо над миром — ровно один зум (двойной дал бы 337.5).
		AddWheel(C, WorldPoint, /*bExpectZoom*/true, TEXT("после закрытия 1559, над миром — ровно один зум"));
		AddBagWheel(C, /*bExpectScroll*/true, TEXT("после закрытия 1559"));
		// Окно `bModal` 1500 (общий механизм): колесо над миром тоже зумит.
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("M3 окно bModal 1500");
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->ShowGameMessage(TEXT("modal_lock2: самотест колеса")); }
		}, 12);
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			X.Expect(ModalUp(X) && WM && WM->IsWindowVisible(kOneBut), FString::Printf(TEXT("окно 1500 показано: IsAnyModalOpen=%d, видимо=%d"),
				ModalUp(X) ? 1 : 0, (WM && WM->IsWindowVisible(kOneBut)) ? 1 : 0));
			PetGateCheck(X, TEXT("под 1500"));
		});
		AddWheel(C, WorldPoint, /*bExpectZoom*/true, TEXT("под 1500 (bModal), над миром"));
		AddWheel(C, [](FCtx& X, FVector2D& P) { return CenterOf(X, kOneBut, TEXT("ONEBUT_BUT00"), P); }, /*bExpectZoom*/false, TEXT("под 1500, над 1500"));
		AddDo(C, [](FCtx& X)
		{
			FVector2D P;
			if (!CenterOf(X, kOneBut, TEXT("ONEBUT_BUT00"), P)) { X.SkipCase(TEXT("кнопка «ОК» 1500 не отрисована")); return; }
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->SimulateClickAt(P, 3); }
		}, 16);
		AddDo(C, [](FCtx& X)
		{
			X.Expect(!ModalUp(X), FString::Printf(TEXT("щелчок «ОК» закрыл 1500: замок снят=%d"), ModalUp(X) ? 0 : 1));
			UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>();
			if (I && X.bPutByTest) { I->DebugPutItem(X.PutCont, 0, 0, 0); X.bPutByTest = false; }   // ячейку — как была
			InvClose(X);
		}, 4);
		AddWheel(C, WorldPoint, /*bExpectZoom*/true, TEXT("после закрытия 1500, над миром — ровно один зум"));

		// ── W (modal_lock3, МЕЛК-2). Склад 4000 «Хранить»: список листается колесом без модалки, под 1559 — нет ──
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("W склад: колесо");
			InvClose(X);
			if (UAsdaWarehouseSubsystem* Wh = X.Sub<UAsdaWarehouseSubsystem>()) { Wh->OpenWindow(UAsdaWarehouseSubsystem::kModeKeep); }
		}, 20);
		AddDo(C, [](FCtx& X)
		{
			// ★ whwheel: точка — центр области списка 4189 (колесо склада теперь только над ней), и её ловит окно 4000.
			const UAsdaWarehouseSubsystem* Wh = X.Sub<UAsdaWarehouseSubsystem>();
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			FVector2D Mn, Mx;                    // область списка 4189 (абсолютные координаты Slate)
			const bool bRect = Wh && Wh->IsOpen() && Wh->GetKeepListAbsRect(Mn, Mx);
			X.WarePt = (Mn + Mx) * 0.5f;
			X.bWarePt = bRect && WM && WM->FindWindowCatchingAt(X.WarePt) == FName(TEXT("WARE_WIN"));
			X.Expect(Wh && Wh->IsOpen() && X.bWarePt && Wh->GetMaxScrollRow() > 0,
				FString::Printf(TEXT("склад открыт в режиме «Хранить»=%d, область списка 4189 (%.0f,%.0f)-(%.0f,%.0f), центр ловит окно 4000=%d, предел ряда %d (ждём > 0)"),
					(Wh && Wh->IsOpen()) ? 1 : 0, Mn.X, Mn.Y, Mx.X, Mx.Y, X.bWarePt ? 1 : 0, Wh ? Wh->GetMaxScrollRow() : -1));
			// Точка над окном 4000 вне области списка (сетка 37×37 по вьюпорту).
			X.bWareOffPt = false;
			if (bRect && WM && GEngine && GEngine->GameViewport)
			{
				const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
				const FGeometry G = Vp.IsValid() ? Vp->GetTickSpaceGeometry() : FGeometry();
				const FVector2D Org = G.GetAbsolutePosition();   // левый верх вьюпорта
				const FVector2D Sz = G.GetAbsoluteSize();        // размер вьюпорта
				for (int32 Iy = 2; Iy <= 38 && !X.bWareOffPt; ++Iy)
				{
					for (int32 Ix = 2; Ix <= 38 && !X.bWareOffPt; ++Ix)
					{
						const FVector2D P = Org + FVector2D(Sz.X * Ix / 40.f, Sz.Y * Iy / 40.f);
						const bool bInList = P.X >= Mn.X - 2.f && P.X <= Mx.X + 2.f && P.Y >= Mn.Y - 2.f && P.Y <= Mx.Y + 2.f;
						if (!bInList && WM->FindWindowCatchingAt(P) == FName(TEXT("WARE_WIN"))) { X.WareOffPt = P; X.bWareOffPt = true; }
					}
				}
			}
		});
		auto WarePrep = [](FCtx& X, FVector2D& P, FString& Why) -> bool
		{
			UAsdaWarehouseSubsystem* Wh = X.Sub<UAsdaWarehouseSubsystem>();
			if (!Wh || !Wh->IsOpen() || !X.bWarePt) { Why = TEXT("склад не открыт или точка над ним не найдена"); return false; }
			Wh->SetScrollRow(0);
			P = X.WarePt;
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			if (WM && WM->IsWindowVisible(kBox) && WM->IsWindowHitAt(kBox, P)) { Why = TEXT("точку накрывает сама 1559"); return false; }
			return true;
		};
		auto WareRead = [](FCtx& X) -> float { const UAsdaWarehouseSubsystem* Wh = X.Sub<UAsdaWarehouseSubsystem>(); return Wh ? (float)Wh->GetScrollRow() : -1.f; };
		AddListWheel(C, WarePrep, WareRead, -1.f, /*bExpectScroll*/true, TEXT("без модалки, список склада 4000"));
		// ── whwheel (10.10.2026): склад открыт — колесо над МИРОМ даёт зум, список стоит (эталон game_main.c:5864 + слот 7 по хит-тесту) ──
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("W склад: колесо над миром");
			if (UAsdaWarehouseSubsystem* Wh = X.Sub<UAsdaWarehouseSubsystem>()) { Wh->SetScrollRow(0); X.WareRowBefore = Wh->GetScrollRow(); }
		});
		AddWheel(C, WorldPoint, /*bExpectZoom*/true, TEXT("склад 4000 «Хранить» открыт, колесо над миром — зум"));
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWarehouseSubsystem* Wh = X.Sub<UAsdaWarehouseSubsystem>();
			const int32 Row = Wh ? Wh->GetScrollRow() : -2;   // ряд после колеса над миром
			X.Expect(Wh && Wh->IsOpen() && Row == X.WareRowBefore, FString::Printf(TEXT("колесо над миром: ряд списка склада %d → %d (ждём на месте — колесо не съедено складом)"),
				X.WareRowBefore, Row));
		});
		// ── whwheel: колесо над окном 4000, но вне списка 4189 — ни прокрутки, ни зума (курсор над окном) ──
		AddDo(C, [](FCtx& X) { X.Phase = TEXT("W склад: колесо вне списка"); });
		AddListWheel(C, [](FCtx& X, FVector2D& P, FString& Why) -> bool
		{
			UAsdaWarehouseSubsystem* Wh = X.Sub<UAsdaWarehouseSubsystem>();
			if (!Wh || !Wh->IsOpen() || !X.bWareOffPt) { Why = TEXT("склад не открыт или точка над окном вне списка не найдена"); return false; }
			Wh->SetScrollRow(0);
			P = X.WareOffPt;
			return true;
		}, WareRead, -1.f, /*bExpectScroll*/false, TEXT("без модалки, над окном склада вне списка 4189"));
		AddDo(C, [](FCtx& X) { X.Phase = TEXT("W склад: колесо"); });
		AddDo(C, [](FCtx& X)
		{
			UAsdaPurchaseBoxSubsystem* B = X.Sub<UAsdaPurchaseBoxSubsystem>();
			if (!B) { return; }
			FAsdaPurchaseBoxRequest Req;
			Req.Mode = EAsdaPurchaseMode::HairShop;
			Req.ItemCode = 76452;   // как в M2: любой существующий код; в сеть ничего не уходит
			Req.Quantity = 1;
			Req.TradeMode = 1;
			B->Open(Req, [](bool) {});
		}, 10);
		AddDo(C, [](FCtx& X) { X.Expect(BoxOpen(X) && ModalUp(X), TEXT("1559 поверх склада показана, замок есть")); });
		AddListWheel(C, WarePrep, WareRead, -1.f, /*bExpectScroll*/false, TEXT("под 1559, список склада 4000"));
		AddDo(C, [](FCtx& X)
		{
			FVector2D P;
			if (!CenterOf(X, kBox, TEXT("PURCHASE_BTN02"), P)) { X.SkipCase(TEXT("кнопка «Отмена» 1562 не отрисована")); return; }
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->SimulateClickAt(P, 3); }
		}, 16);
		AddDo(C, [](FCtx& X) { X.Expect(!BoxOpen(X) && !ModalUp(X), TEXT("щелчок «Отмена» 1562 закрыл 1559 поверх склада")); }, 2);
		AddListWheel(C, WarePrep, WareRead, -1.f, /*bExpectScroll*/true, TEXT("после закрытия 1559, список склада 4000"));
		AddDo(C, [](FCtx& X)
		{
			if (UAsdaWarehouseSubsystem* Wh = X.Sub<UAsdaWarehouseSubsystem>()) { if (Wh->IsOpen()) { Wh->CloseWindow(); } }
		}, 10);

		// ── M4 (modal_lock3, МЕЛК-3). Модалка закрыта, когда контроллера нет (смена карты), — без залипания и двойного зума ──
		//   Имитация: `DebugSimulateNoController` — `RefreshModalInput` не видит контроллера; затем «новый» контроллер получает
		//   режим мира так же, как в `UAsdaWorldSubsystem::RevealWorld` (GameAndUI напрямую).
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("M4 потеря контроллера");
			if (UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>()) { WM->ShowGameMessage(TEXT("modal_lock3: самотест потери контроллера")); }
		}, 12);
		AddDo(C, [](FCtx& X)
		{
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			X.Expect(WM && ModalUp(X) && WM->IsWorldInputCapturedByModal(), FString::Printf(TEXT("окно 1500 показано, ввод мира отобран модалкой=%d"),
				(WM && WM->IsWorldInputCapturedByModal()) ? 1 : 0));
			if (WM) { WM->DebugSimulateNoController(true); }
			FVector2D P;
			if (!CenterOf(X, kOneBut, TEXT("ONEBUT_BUT00"), P)) { X.SkipCase(TEXT("кнопка «ОК» 1500 не отрисована")); return; }
			if (WM) { WM->SimulateClickAt(P, 3); }
		}, 16);
		AddDo(C, [](FCtx& X)
		{
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			const bool bCap = WM && WM->IsWorldInputCapturedByModal();
			X.Expect(!ModalUp(X) && !bCap, FString::Printf(TEXT("1500 закрыто без контроллера: замок снят=%d, ввод мира модалкой отобран=%d (ждём 0 — флаг не залипает)"),
				ModalUp(X) ? 0 : 1, bCap ? 1 : 0));
			if (WM) { WM->DebugSimulateNoController(false); }
			// «Новый» контроллер: режим мира, как `RevealWorld`.
			if (APlayerController* P = X.PC())
			{
				FInputModeGameAndUI M;
				M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
				M.SetHideCursorDuringCapture(false);
				P->SetInputMode(M);
				P->bShowMouseCursor = true;
			}
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		}, 4);
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			X.Expect(WM && !WM->IsWorldInputCapturedByModal(), TEXT("режим мира у контроллера: ввод мира модалкой не отобран"));
		});
		AddWheel(C, WorldPoint, /*bExpectZoom*/true, TEXT("после закрытия без контроллера и режима мира, над миром — ровно один зум"));

		// ── F. Книга рыбака: модальный контрол 74200 ВНУТРИ окна ──
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("F окно 74200");
			InvClose(X);
			if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>()) { B->Open(); }
		}, 30);
		AddDo(C, [](FCtx& X)
		{
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			if (!B || !B->IsOpen() || !WM) { X.SkipCase(TEXT("книга 74000 офлайн не открылась")); return; }
			X.Expect(!ModalUp(X), FString::Printf(TEXT("книга открыта без окна регистрации — замка нет (z книги %d)"), WM->GetWindowZOrder(kBook)));
			B->DebugOpenJoin(0);
		}, 6);
		AddDo(C, [](FCtx& X)
		{
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			if (!B || !B->IsJoinOpen() || !WM) { X.SkipCase(TEXT("окно регистрации 74200 не открылось")); return; }
			const int32 Z = WM->GetWindowZOrder(kBook);
			X.Expect(ModalUp(X) && WM->GetOriginalModalOwner() == kBook,
				FString::Printf(TEXT("74200 показано → IsAnyModalOpen=%d, ввод отдан корню «%s» (z=%d)"), ModalUp(X) ? 1 : 0, *WM->GetOriginalModalOwner().ToString(), Z));
			X.Expect(!AsdaUi::IsModalDispatchBlockedFor(X.GI(), kBook) && AsdaUi::IsModalDispatchBlockedFor(X.GI(), kInven),
				TEXT("опрос мыши книги не заперт своим контролом, опрос чужого окна (сумка) заперт"));
			PetGateCheck(X, TEXT("под 74200"));
			FString Why;
			const bool bEsc = EscMenuBlocked(X, Why);
			const bool bEaten = WM->CloseTopmostOnEsc();
			X.Expect(bEsc && !bEaten && B->IsJoinOpen() && B->IsOpen(),
				FString::Printf(TEXT("ESC при 74200: съеден=%d (ждём 0), 74200 и книга на месте=%d, ESC-меню заперто=%d (%s)"),
					bEaten ? 1 : 0, (B->IsJoinOpen() && B->IsOpen()) ? 1 : 0, bEsc ? 1 : 0, *Why));
		});
		AddPress(C, [](FCtx& X) { return X.KeyInv; }, /*bPi*/true, false, false);
		AddDo(C, [](FCtx& X)
		{
			const bool bOpen = InvShown(X);
			X.Expect(!bOpen, FString::Printf(TEXT("клавиша сумки (UPlayerInput) при 74200 — сумка %s (ждём не открыта)"), bOpen ? TEXT("ОТКРЫТА") : TEXT("не открыта")));
			InvClose(X);
			FString Why;
			X.Expect(HotbarBlocked(X, Why), FString::Printf(TEXT("гейт хотбара при 74200 заперт: %s"), *Why));
			if (UAsdaSystemMenuSubsystem* M = X.Sub<UAsdaSystemMenuSubsystem>()) { M->ClickButton(73402); }
		}, 8);
		AddDo(C, [](FCtx& X)
		{
			const bool bOpen = InvShown(X);
			X.Expect(!bOpen, FString::Printf(TEXT("кнопка 73402 при 74200 — сумка %s (ждём не открыта)"), bOpen ? TEXT("ОТКРЫТА") : TEXT("не открыта")));
			InvClose(X);
			if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>()) { B->CloseJoin(); }   // 74253 «Нет» — снятие модальности
		}, 4);
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			const int32 Z = WM ? WM->GetWindowZOrder(kBook) : -1;
			X.Expect(!ModalUp(X) && B && B->IsOpen() && !B->IsJoinOpen() && (Z < UAsdaWindowSubsystem::kPriorityZ || Z > UAsdaWindowSubsystem::kPriorityZMax),
				FString::Printf(TEXT("после CloseJoin: замок снят=%d, книга открыта=%d, z книги %d вне полосы приоритетных"), ModalUp(X) ? 0 : 1, (B && B->IsOpen()) ? 1 : 0, Z));
		});
		AddPress(C, [](FCtx& X) { return X.KeyInv; }, /*bPi*/true, false, true);
		AddDo(C, [](FCtx& X)
		{
			const bool bOpen = InvShown(X);
			X.Expect(bOpen, FString::Printf(TEXT("контроль: клавиша сумки после CloseJoin — сумка %s (ждём открыта)"), bOpen ? TEXT("ОТКРЫТА") : TEXT("не открыта")));
			InvClose(X);
			if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>()) { if (B->IsOpen()) { B->CloseOrToggle(); } }
		}, 4);

		// ── F2 (modal_lock2, МЕЛК-4). Порядок z книги после снятия 74200: выше обычных видимых окон, ниже видимых AlwaysTop ──
		//   Сумка открывается ПОСЛЕ книги (она выше книги) — затем 74200 и его снятие: книга, как у эталона (её поднял двойной
		//   щелчок, открывший 74200), должна оказаться выше сумки и всё так же ниже окон AlwaysTop.
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("F2 порядок z книги");
			InvClose(X);
			if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>()) { B->Open(); }
		}, 30);
		AddDo(C, [](FCtx& X)
		{
			if (UAsdaInventorySubsystem* I = X.Sub<UAsdaInventorySubsystem>()) { I->OpenWindow(); }
		}, 10);
		AddDo(C, [](FCtx& X)
		{
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			if (!B || !B->IsOpen() || !WM) { X.SkipCase(TEXT("книга не открыта — порядок z не проверен")); return; }
			UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК [F2] до 74200: z книги %d, z сумки %d (сумка открыта=%d)"),
				WM->GetWindowZOrder(kBook), WM->GetWindowZOrder(kInven), InvShown(X) ? 1 : 0);
			B->DebugOpenJoin(0);
		}, 6);
		AddDo(C, [](FCtx& X)
		{
			if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>()) { B->CloseJoin(); }
		}, 4);
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			if (!WM || !B || !B->IsOpen()) { X.SkipCase(TEXT("книга закрылась — порядок z не проверен")); return; }
			const int32 Zb = WM->GetWindowZOrder(kBook);   // z книги после снятия 74200
			int32 NOrd = 0, NAt = 0;                       // сколько обычных / AlwaysTop видимых окон сравнено
			FString Bad;                                   // нарушители порядка
			for (const FName& Id : WM->GetRegisteredWindowIds())
			{
				if (Id == kBook || !WM->IsWindowVisible(Id)) { continue; }
				const int32 Z = WM->GetWindowZOrder(Id);
				if (WM->IsWindowAlwaysTop(Id))
				{
					++NAt;
					if (Z <= Zb) { Bad += FString::Printf(TEXT(" AlwaysTop «%s» z %d ≤ книги;"), *Id.ToString(), Z); }
				}
				else if (Z < UAsdaWindowSubsystem::kAlwaysTopZ)
				{
					++NOrd;
					if (Z >= Zb) { Bad += FString::Printf(TEXT(" обычное «%s» z %d ≥ книги;"), *Id.ToString(), Z); }
				}
			}
			if (NOrd == 0 || NAt == 0) { X.SkipCase(FString::Printf(TEXT("сравнивать не с чем: обычных видимых %d, AlwaysTop видимых %d"), NOrd, NAt)); return; }
			X.Expect(Bad.IsEmpty() && Zb < UAsdaWindowSubsystem::kAlwaysTopZ,
				FString::Printf(TEXT("после снятия 74200 книга (z %d) выше всех %d видимых обычных окон и ниже всех %d видимых AlwaysTop%s"),
					Zb, NOrd, NAt, Bad.IsEmpty() ? TEXT("") : *(TEXT(" — НАРУШЕНО:") + Bad)));
		});

		// ── F3 (modal_lock2, СРЕДН-2 + МЕЛК-2). Двойной щелчок по ячейке книги и перетаскивание книги под 74200 ──
		//   Тома с уловом приходят штатным приёмом 6174 (без них двойной щелчок окно не откроет).
		AddDo(C, [](FCtx& X)
		{
			X.Phase = TEXT("F3 книга: двойной щелчок и перетаскивание");
			InvClose(X);
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			UAsdaNetSubsystem* Net = X.Sub<UAsdaNetSubsystem>();
			if (!B || !Net) { return; }
			Net->DebugFeedPacket(6174, Page305(1, { 5, 11, 3, 4 }));   // том 1: четыре рыбы пойманы
			B->PumpPending();
			if (!B->IsOpen()) { B->Open(); }
			// Вкладка, на которой у ячеек 0..3 есть пойманные рыбы.
			for (int32 T = 0; T < UAsdaFishBookSubsystem::kMaxTabs; ++T)
			{
				B->OnTabSelected(T);
				if (B->DebugCellFish(0) != -1 && B->DebugCellFish(1) != -1) { break; }
			}
		}, 20);
		AddDo(C, [](FCtx& X)
		{
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			X.CellA = X.CellB = -1;
			if (!B || !B->IsOpen()) { X.SkipCase(TEXT("книга не открыта")); return; }
			// A — первая пойманная ячейка; узел 74200 показывается на рыбе A, и B — пойманная ячейка ВНЕ его прямоугольника.
			for (int32 c = 0; c < UAsdaFishBookSubsystem::kFish && X.CellA < 0; ++c) { if (B->DebugCellFish(c) != -1) { X.CellA = c; } }
			if (X.CellA < 0) { X.SkipCase(TEXT("6174 не дал пойманных ячеек")); return; }
			X.FishA = B->DebugCellFish(X.CellA);
			B->DebugOpenJoin(X.FishA);
		}, 6);
		AddDo(C, [](FCtx& X)
		{
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			if (!B || X.CellA < 0) { return; }
			const UWidget* J = B->DebugWidget() ? B->DebugWidget()->GetWidgetFromName(TEXT("FISH_BOOK_JOINBOX_grp")) : nullptr;
			FVector2D Mn(0.f, 0.f), Mx(0.f, 0.f);
			const bool bJ = J && AsdaUi::WidgetAbsRect(J, Mn, Mx);
			for (int32 c = 0; c < UAsdaFishBookSubsystem::kFish && X.CellB < 0; ++c)
			{
				FVector2D P;
				if (c == X.CellA || B->DebugCellFish(c) == -1 || B->DebugCellFish(c) == X.FishA || !BookCellCenter(X, c, P)) { continue; }
				if (bJ && P.X >= Mn.X && P.X <= Mx.X && P.Y >= Mn.Y && P.Y <= Mx.Y) { continue; }   // под узлом 74200 — щелчок дойдёт до него
				X.CellB = c;
			}
			X.FishB = (X.CellB >= 0) ? B->DebugCellFish(X.CellB) : -1;
			UE_LOG(LogAsdaModalLockTest, Log, TEXT("МОДАЛ-ЗАМОК [F3] ячейки: A=%d (рыба %d), B=%d (рыба %d), узел 74200 (%.0f,%.0f)-(%.0f,%.0f)"),
				X.CellA, X.FishA, X.CellB, X.FishB, Mn.X, Mn.Y, Mx.X, Mx.Y);
			if (X.CellB < 0) { X.SkipCase(TEXT("нет второй пойманной ячейки вне узла 74200")); }
			B->CloseJoin();
		}, 4);
		// Контроль МЕЛК-2: без 74200 хит-тест окон над ячейкой B ловит книгу.
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			FVector2D P;
			if (!WM || X.CellB < 0 || !BookCellCenter(X, X.CellB, P)) { return; }
			const FName At = WM->FindWindowCatchingAt(P);   // кто ловит мышь в точке
			X.Expect(At == kBook, FString::Printf(TEXT("МЕЛК-2 контроль: без 74200 хит-тест окон над ячейкой B (%.0f,%.0f) ловит «%s» (ждём книгу)"), P.X, P.Y, *At.ToString()));
		});
		// Контроль: без 74200 двойной щелчок по B открывает 74200 рыбы B.
		AddDoubleClick(C, [](FCtx& X, FVector2D& P) { return X.CellB >= 0 && BookCellCenter(X, X.CellB, P); }, TEXT("без 74200, ячейка B"));
		AddDo(C, [](FCtx& X)
		{
			if (!X.bDblArmed) { return; }
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			const int32 F = B ? B->DebugJoinFish() : -1;
			X.Expect(F == X.FishB, FString::Printf(TEXT("без 74200: двойной щелчок по ячейке B → 74200 рыбы %d (ждём %d)"), F, X.FishB));
			if (B && B->IsJoinOpen()) { B->CloseJoin(); }
		}, 4);
		// МЕЛК-2, перетаскивание: у книги ручек нет вовсе — в `.JUI` у 74000 нет события EVT 6 (gamemain_jui.ini:337621…342384),
		//   реестр (`asda.UI.DumpDrag`) печатает «ручек=0». Тащить книгу нечем ни с 74200, ни без; гейт мыши проверен хит-тестом выше.
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			FVector2D Mn, Mx;
			const bool bAny = WM && WM->DebugGetDragHandleRect(kBook, 0, Mn, Mx);
			X.Expect(!bAny, FString::Printf(TEXT("у книги 74000 ручек перетаскивания нет (данные .JUI: EVT 6 нет) — ручка №0 %s"), bAny ? TEXT("ЕСТЬ") : TEXT("нет")));
		});
		// Двойной щелчок по A открывает 74200 рыбы A.
		AddDoubleClick(C, [](FCtx& X, FVector2D& P) { return X.CellA >= 0 && X.CellB >= 0 && BookCellCenter(X, X.CellA, P); }, TEXT("ячейка A"));
		AddDo(C, [](FCtx& X)
		{
			if (!X.bDblArmed) { return; }
			X.Phase = TEXT("F3 под 74200");
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			const int32 F = B ? B->DebugJoinFish() : -1;
			X.Expect(F == X.FishA && ModalUp(X), FString::Printf(TEXT("двойной щелчок по ячейке A → 74200 рыбы %d (ждём %d), замок=%d"), F, X.FishA, ModalUp(X) ? 1 : 0));
		}, 4);
		// МЕЛК-2: под 74200 мышь книге — только в прямоугольнике узла 74200 (эталон `+0x1A0`): над ячейкой B хит-тест книгу НЕ ловит,
		//   над центром узла — ловит.
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			FVector2D P, Mn(0.f, 0.f), Mx(0.f, 0.f);
			if (!WM || !B || !B->IsJoinOpen() || X.CellB < 0 || !BookCellCenter(X, X.CellB, P)) { return; }
			const UWidget* J = B->DebugWidget() ? B->DebugWidget()->GetWidgetFromName(TEXT("FISH_BOOK_JOINBOX_grp")) : nullptr;
			if (!J || !AsdaUi::WidgetAbsRect(J, Mn, Mx)) { X.SkipCase(TEXT("узел 74200 не отрисован")); return; }
			const FVector2D Q = (Mn + Mx) * 0.5f;   // центр узла 74200
			const FName AtB = WM->FindWindowCatchingAt(P), AtJ = WM->FindWindowCatchingAt(Q);
			X.Expect(AtB != kBook && AtJ == kBook, FString::Printf(TEXT("МЕЛК-2: под 74200 хит-тест над ячейкой B ловит «%s» (ждём не книгу), над центром узла 74200 (%.0f,%.0f) — «%s» (ждём книгу)"),
				*AtB.ToString(), Q.X, Q.Y, *AtJ.ToString()));
		});
		// Под 74200: двойной щелчок по B (вне узла) окно на рыбу B не перерисовывает.
		AddDoubleClick(C, [](FCtx& X, FVector2D& P)
		{
			const UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			return B && B->IsJoinOpen() && X.CellB >= 0 && BookCellCenter(X, X.CellB, P);
		}, TEXT("под 74200, ячейка B"));
		AddDo(C, [](FCtx& X)
		{
			if (!X.bDblArmed) { return; }
			UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>();
			const int32 F = B ? B->DebugJoinFish() : -1;
			X.Expect(F == X.FishA, FString::Printf(TEXT("под 74200: двойной щелчок по ячейке B — 74200 рыбы %d (ждём %d — не перерисован на %d)"), F, X.FishA, X.FishB));
		}, 4);
		// Колесо под 74200. Точки мира при 1024×768 нет: книга 810×756 поднята и накрывает почти весь экран, поля по краям заняты
		//   HUD. Путь колеса тот же, что под 1559 и 1500 (препроцессор контроллера при `IsWorldInputCapturedByModal`) — проверяем,
		//   что он включён, и что над книгой вне узла зума нет (курсор над окном, эталон game_main.c:5874).
		AddDo(C, [](FCtx& X)
		{
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			X.Expect(WM && WM->IsWorldInputCapturedByModal(), TEXT("под 74200 ввод мира отобран модалкой — колесо в мир идёт препроцессором (IsWorldInputCapturedByModal = 1)"));
		});
		AddWheel(C, [](FCtx& X, FVector2D& P) { return X.CellB >= 0 && BookCellCenter(X, X.CellB, P); }, /*bExpectZoom*/false, TEXT("под 74200, над книгой вне узла"));
		AddDo(C, [](FCtx& X)
		{
			if (UAsdaFishBookSubsystem* B = X.Sub<UAsdaFishBookSubsystem>())
			{
				if (B->IsJoinOpen()) { B->CloseJoin(); }
				if (B->IsOpen()) { B->CloseOrToggle(); }
			}
			X.Expect(!ModalUp(X), TEXT("F3 конец: 74200 и книга закрыты, замка нет"));
		}, 4);

		AddDo(C, [](FCtx& X) { Finish(X); });
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI || !GI->GetSubsystem<UAsdaWindowSubsystem>()) { UE_LOG(LogAsdaModalLockTest, Error, TEXT("МОДАЛ-ЗАМОК: нет подсистемы окон")); return; }
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
		Build(C);
		// Первый шаг — после построения HUD и окон (как у `asda.UI.SelfTestEscOnTop`).
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { RunStep(C); }), 2.5f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSelfTestModalLock(
	TEXT("asda.UI.SelfTestModalLock"),
	TEXT("asda.UI.SelfTestModalLock [delay=N] [quit] — ОФЛАЙН-самотест замка ввода модалкой эталона (флаг CWindowMgr+0x200): ")
	TEXT("пары «без модалки срабатывает / под модалкой нет» — клавиша сумки, Enter, Alt-сочетание (Slate и UPlayerInput), хотбар, ")
	TEXT("системное меню (обработчик и щелчок), щелчок по окну под модалкой, ESC-меню; модалка 1559 парикмахерской (открыта и ")
	TEXT("закрыта настоящими щелчками), ключ asda.UI.ModalLock, окно регистрации 74200 внутри книги 74000"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.UI.SelfTestModalLock"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestModalLock"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit]()
		{
			if (!WW.Get()) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(WW.Get(), TEXT("asda.UI.SelfTestModalLock"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			TSharedRef<AsdaModalLockLocal::FCtx> C = MakeShared<AsdaModalLockLocal::FCtx>();
			C->W = WW; C->bQuit = bQuit;
			AsdaModalLockLocal::Run(C);
		};
		if (Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
