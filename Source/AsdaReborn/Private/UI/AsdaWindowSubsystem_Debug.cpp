// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_Debug.cpp — ★ 23.10.2026 часть UAsdaWindowSubsystem, вынесенная из AsdaWindowSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaWindowSubsystem.plan.json).
//   Консольная диагностика перетаскивания и хит-теста окон (только вне Shipping).
//   #include — те же, что у AsdaWindowSubsystem.cpp, в том же порядке; общее — AsdaWindowSubsystemInternal.h.
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableText.h"   // поле ввода 1583 диалога SHOUTING_WIN
#include "Components/CheckBox.h"         // ★ приёмка: галки под ручкой (INVEN_NEW_WIN 83503)
#include "Components/EditableTextBox.h"  // ★ приёмка: поля ввода под ручкой
#include "Components/MultiLineEditableText.h"  // ★ приёмка: многострочный ввод
#include "Components/ComboBoxString.h"   // ★ приёмка: выпадающие списки
#include "Components/Slider.h"           // ★ приёмка: ползунки тянут мышь сами
#include "Components/ScrollBox.h"        // ★ приёмка: прокрутка тянет мышь сама
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"   // DPI-скейл вьюпорта: абсолютная дельта курсора → юниты канвы
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"   // метрики строки для asda.UI.DumpTextMetrics   // фокус на игровой вьюпорт при базовом GameAndUI
#include "Misc/ConfigCacheIni.h"   // раскладка окон между сессиями: секция [AsdaWindowLayout] в ini игры
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::Loc — подписи кнопок ТОЛЬКО из таблицы DT_GameText
// игровые loc-подписи (2288 «ОК» / 2289 «Отмена») для TWOBUT_MSGBOX   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaUiHelpers.h"         // D1b: общая `AsdaUi::WidgetAbsRect` (хит-тест окна и рамки)
#include "UI/AsdaMessageSubsystem.h"  // D2: фолбэк однокнопочного msgbox, когда WBP не загрузился
#include "UI/AsdaHotbarSubsystem.h"      // шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара
// шаги 23-24: сворачивание трея и запрет его кнопок   ★ R3: комментарий снятого включения "UI/AsdaSystemMenuSubsystem.h" (файл им не пользовался) — сохранён
#include "Resources/AsdaSoundSubsystem.h" // звуки открытия/закрытия окна количества (5 / 6)
// ★ 27.09.2026: панель 1530 для `DropTopMsgBoxBeforeNotice`   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaPurchaseBoxSubsystem.h"  // ★ 27.09.2026: панель 1559 для `DropTopMsgBoxBeforeNotice`
#include "Styling/SlateStyleRegistry.h"   // ★ гашение синей рамки фокуса: подмена кисти FocusRectangle
#include "Styling/SlateStyle.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateNoResource.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Login/AsdaText.h"                 // FAsdaText::Loc — строки диалога `asda.UI.ConfirmPreview` (07.10.2026)
#include "Login/AsdaLoginGameMode.h"          // ★ 08.10.2026: подъём при показе не трогает экран логина
#include "World/AsdaWorldKind.h"               // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "GameFramework/GameModeBase.h"     // GetAuthGameMode — проверка экрана логина
#include "UnrealClient.h"                   // С-1 (07.10.2026): FViewport::ViewportResizedEvent — смена размера вьюпорта
#include "Blueprint/GameViewportSubsystem.h" // М-1 (07.10.2026): слот окна во вьюпорте — полноэкранный ли он
#include "Widgets/SOverlay.h"               // ★ 08.10.2026: подъём окна БЕЗ снятия с вьюпорта (слот оверлея вьюпорта)
#include "UI/AsdaNotice.h"                  // ★ 08.10.2026 (ревизия В3 рыбалки): нотис 6835 строгого нумпада — LocRaw / ChatColored
// ★ 08.10.2026: ColGold — цвет −11514 нотиса 6835   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
// ★ 09.10.2026: UAsdaRichText::ApplyFormatted — теги <FC=…> в теле окон-сообщений   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Components/RichTextBlock.h"      // ★ 09.10.2026: тело окна-сообщения после первой подмены — URichTextBlock
#include "AsdaWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWindowSubsystem*.cpp (LogAsdaWindow, чистые хелперы)


// ═══════════════ КОНСОЛЬНАЯ ДИАГНОСТИКА ПЕРЕТАСКИВАНИЯ И ХИТ-ТЕСТА ОКОН ══════════════════════════
// Сверка с реверсом: `asda.UI.DumpDrag` печатает по каждому ОТКРЫТОМУ окну его ручки и их живые
// прямоугольники; `asda.UI.DumpZones` — все зоны интерфейса и окно под курсором.
static UAsdaWindowSubsystem* AsdaGetWindowSub(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
}

// ★★★ ОТКРЫТЬ ЛЮБОЕ ОКНО .JUI ПО ПУТИ АССЕТА — общая проверка ПЕРЕИМПОРТА (2026-08-22).
//   Зачем отдельная команда: у каждой подсистемы своя точка входа (`asda.Inven.Open`,
//   `asda.Skill.Open`, `asda.Gacha.Open`…), а окон .JUI 251, и у большинства подсистемы ещё нет.
//   Чтобы доказать, что состояния подписи зажглись НЕ у одного окна, а во всей игре, нужно уметь
//   поднять произвольное окно из ассета и навести на его кнопку мышь. Это ровно `OpenWindowByPath`,
//   то есть тот же путь, которым окна открывает игра, — ничего специального для проверки не пишем.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiOpenByPath(
	TEXT("asda.UI.OpenByPath"),
	TEXT("asda.UI.OpenByPath <ИдОкна> <ПутьWBP> [ZOrder] — открыть окно .JUI прямо из ассета "
	     "(напр. asda.UI.OpenByPath COMMUNITY_WIN /Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN). "
	     "Нужна общей проверке переимпорта: подсистема есть не у каждого окна."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 2)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.OpenByPath <ИдОкна> <ПутьWBP> [ZOrder]"));
			return;
		}
		const int32 Z = A.Num() >= 3 ? FCString::Atoi(*A[2]) : 0;
		UUserWidget* Win = S->OpenWindowByPath(FName(*A[0]), A[1], Z,
			ESlateVisibility::SelfHitTestInvisible, /*bModal=*/false);
		UE_LOG(LogAsdaWindow, Log, TEXT("[окна] asda.UI.OpenByPath «%s» ← %s: %s"),
			*A[0], *A[1], Win ? TEXT("ОТКРЫТО") : TEXT("НЕ ОТКРЫЛОСЬ (нет ассета?)"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ПРОСМОТР ДИАЛОГА ПОДТВЕРЖДЕНИЯ MESSAGE2 БЕЗ ВЫБОРА ПЕРСОНАЖА (07.10.2026, правки ревизии расширения окон, п.4).
//   Окно живёт только на выборе персонажа (удаление — `AAsdaLoginGameMode`, loc 129/132 + кнопки 200000/200001;
//   режим входа — нейтральный вид, 990132/990135 + 990133/990134), а туда офлайн не попасть. Команда зовёт ТОТ ЖЕ
//   `ShowConfirm` с ТЕМИ ЖЕ строками, поэтому подписи кнопок ставит штатный код, а не проверка. Ответ никуда не идёт:
//   колбэк пустой, таймера и гейта нет. Офлайн-гейт: при живом соединении команда молчит.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiConfirmPreview(
	TEXT("asda.UI.ConfirmPreview"),
	TEXT("asda.UI.ConfirmPreview [delete|enter] — показать MESSAGE2_WIN штатным ShowConfirm с подписями эталона: "
	     "delete — диалог удаления персонажа (129, «Удалить»/«Отменить»), enter — нейтральный выбор режима входа "
	     "(990132+990135, «Обычный»/«Душа»). Колбэк пустой — только для проверки разметки путём игрока."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.UI.ConfirmPreview"))) { return; }
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S) { return; }
		const bool bEnter = A.Num() >= 1 && A[0].Equals(TEXT("enter"), ESearchCase::IgnoreCase);
		if (bEnter)
		{
			S->ShowConfirm(FAsdaText::Loc(990132) + TEXT("\n") + FAsdaText::Loc(990135), [](bool) {},
				0.f, false, 990133, 990134, false, /*bNeutral*/true);
		}
		else
		{
			S->ShowConfirm(FAsdaText::Loc(129), [](bool) {}, 0.f, false, 200000, 200001, false, /*bNeutral*/false);
		}
		UE_LOG(LogAsdaWindow, Log, TEXT("[окна] asda.UI.ConfirmPreview: показан %s"),
			bEnter ? TEXT("нейтральный MESSAGE2 (режим входа)") : TEXT("MESSAGE2 (удаление персонажа)"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Живая проверка окна количества БЕЗ торговли и склада: открыть, ввести, увидеть результат в логе.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiNumPad(
	TEXT("asda.UI.NumPad"),
	TEXT("asda.UI.NumPad <максимум> [center] [at <X> <Y>] — открыть окно ввода количества 1410 (эталонный NUMPAD_WIN). "
	     "Введённое значение пишется в лог. center — центрировать по экрану (как режим 6 эталона). "
	     "at X Y — сперва настоящее движение мыши Slate в точку (X,Y) абсолютных координат (как `asda.UI.SimHoverWidget`)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 1)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.NumPad <максимум> [center] [at <X> <Y>]"));
			return;
		}
		const int32 Max = FCString::Atoi(*A[0]);
		const bool bCenter = A.Num() > 1 && A[1].Equals(TEXT("center"), ESearchCase::IgnoreCase);
		// ★ 08.10.2026 (пакет 3, проверка СРЕДН-1): «at X Y» — курсор в точку до открытия (окно встаёт у курсора).
		const int32 AtIdx = A.IndexOfByPredicate([](const FString& X) { return X.Equals(TEXT("at"), ESearchCase::IgnoreCase); });
		if (AtIdx != INDEX_NONE && A.Num() > AtIdx + 2 && FSlateApplication::IsInitialized())
		{
			const FVector2D Pos(FCString::Atod(*A[AtIdx + 1]), FCString::Atod(*A[AtIdx + 2]));   // точка курсора, абсолютные
			FSlateApplication& App = FSlateApplication::Get();
			App.SetCursorPos(Pos);
			const FPointerEvent Ev(0, Pos, Pos, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
			App.ProcessMouseMoveEvent(Ev);
			UE_LOG(LogAsdaWindow, Log, TEXT("[количество] ПРОВЕРКА: курсор в (%.0f,%.0f)"), Pos.X, Pos.Y);
		}
		S->ShowNumPad(Max, [](bool bOk, int32 Qty)
		{
			UE_LOG(LogAsdaWindow, Log, TEXT("[количество] ПРОВЕРКА: результат %s, количество %d"),
				bOk ? TEXT("ОК") : TEXT("Отмена"), Qty);
		}, bCenter);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiFront(
	TEXT("asda.UI.Front"),
	TEXT("asda.UI.Front <ИдОкна> — вывести окно наверх обычного слоя (то же, что делает клик по нему). "
	     "Нужен живой проверке жалобы «нажимать по два раза»: чтобы опустить проверяемое окно под чужое."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 1) { UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.Front <ИдОкна>")); return; }
		S->BringWindowToFront(FName(*A[0]));
		UE_LOG(LogAsdaWindow, Log, TEXT("[окна] «%s» выведено наверх (asda.UI.Front), z=%d"), *A[0], S->GetWindowZOrder(FName(*A[0])));
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ЖИВАЯ ПРОВЕРКА СОСТОЯНИЙ ПОДПИСИ КНОПОК (наведение/нажатие) — пара к `SimClickWidget`.
//   Наведение мышью в headless-PIE иначе не воспроизвести: `SetCursorPos` сам по себе события
//   Slate не порождает, а звать `OnHovered` напрямую бесполезно — проверять надо ровно доставку.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSimHoverWidget(
	TEXT("asda.UI.SimHoverWidget"),
	TEXT("asda.UI.SimHoverWidget <ИдОкна> <ИмяВиджета> — настоящее ДВИЖЕНИЕ МЫШИ Slate в центр "
	     "виджета (порождает OnMouseEnter/OnHovered). Без имени виджета — увести курсор в (0,0), "
	     "то есть снять наведение со всего."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		FVector2D Pos(0.f, 0.f);
		if (S && A.Num() >= 2)
		{
			UUserWidget* Win = S->FindWindow(FName(*A[0]));
			UWidget* Target = Win ? Win->GetWidgetFromName(FName(*A[1])) : nullptr;
			FVector2D Mn, Mx;
			if (!Target || !AsdaUi::WidgetAbsRect(Target, Mn, Mx))
			{
				UE_LOG(LogAsdaWindow, Warning, TEXT("[симнаведение] «%s»/«%s» — виджета нет либо он не отрисован"),
					*A[0], *A[1]);
				return;
			}
			Pos = (Mn + Mx) * 0.5f;
		}
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(Pos);
		const FPointerEvent Ev(0, Pos, Pos, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
		App.ProcessMouseMoveEvent(Ev);
		UE_LOG(LogAsdaWindow, Log, TEXT("[симнаведение] курсор в (%.0f,%.0f); Slate-курсор (%.0f,%.0f)"),
			Pos.X, Pos.Y, App.GetCursorPos().X, App.GetCursorPos().Y);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Дамп СОСТОЯНИЙ ПОДПИСИ: что скин положил в стиль кнопки и какой цвет/позиция у подписи ПРЯМО
//   СЕЙЧАС. Нужен потому, что глазами по снимку 1-пиксельный сдвиг и оттенок не проверить.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiDumpCaptionStates(
	TEXT("asda.UI.DumpCaptionStates"),
	TEXT("asda.UI.DumpCaptionStates [ИдОкна] — по каждой кнопке со состояниями подписи: цвета "
	     "покоя/наведения/нажатия из скина, ТЕКУЩИЙ цвет подписи и её позиция."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S) { return; }
		UE_LOG(LogAsdaWindow, Log, TEXT("%s"), *S->DebugCaptionStates(A.Num() >= 1 ? FName(*A[0]) : NAME_None));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSimClickWidget(
	TEXT("asda.UI.SimClickWidget"),
	TEXT("asda.UI.SimClickWidget <ИдОкна> <ИмяВиджета> [кадров удержания=8] — настоящий клик Slate "
	     "в ЦЕНТР виджета. Удобнее координат: имена печатают дампы хит-теста, и координаты не надо "
	     "переносить руками."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетический клик по виджету — при живом соединении сработает боевой обработчик кнопки.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.UI.SimClickWidget"))) { return; }
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 2)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.SimClickWidget <ИдОкна> <ИмяВиджета> [кадров]"));
			return;
		}
		UUserWidget* Win = S->FindWindow(FName(*A[0]));
		UWidget* Target = Win ? Win->GetWidgetFromName(FName(*A[1])) : nullptr;
		if (!Target)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("[симклик] в окне «%s» нет виджета «%s»"), *A[0], *A[1]);
			return;
		}
		FVector2D Mn, Mx;
		if (!AsdaUi::WidgetAbsRect(Target, Mn, Mx))
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("[симклик] у «%s» нет экранного прямоугольника (не отрисован)"), *A[1]);
			return;
		}
		S->SimulateClickAt((Mn + Mx) * 0.5f, A.Num() >= 3 ? FCString::Atoi(*A[2]) : 8);
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ СИНТЕТИЧЕСКОЕ ПЕРЕТАСКИВАНИЕ. Единственный способ проверить путём игрока захват иконки
//   в headless-прогоне: курсор ведётся настоящими событиями Slate, поэтому срабатывает и порог
//   `FAsdaDragArm`, и хит-тесты по `GetCursorPos()`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSimDrag(
	TEXT("asda.UI.SimDrag"),
	TEXT("asda.UI.SimDrag <X1> <Y1> <X2> <Y2> [кадров удержания=20] — НАСТОЯЩЕЕ перетаскивание Slate: "
	     "нажатие в (X1,Y1), плавное движение курсора и отпускание в (X2,Y2). Координаты абсолютные — "
	     "те же, что печатают дампы хит-теста."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетическое перетаскивание — при живом соединении 5000/5067/5002 и т. п. по координатам вслепую.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.UI.SimDrag"))) { return; }
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 4)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.SimDrag <X1> <Y1> <X2> <Y2> [кадров]"));
			return;
		}
		S->SimulateDrag(
			FVector2D(FCString::Atof(*A[0]), FCString::Atof(*A[1])),
			FVector2D(FCString::Atof(*A[2]), FCString::Atof(*A[3])),
			A.Num() >= 5 ? FCString::Atoi(*A[4]) : 20);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSimClick(
	TEXT("asda.UI.SimClick"),
	TEXT("asda.UI.SimClick <X> <Y> [кадров удержания=8] [L|R=L] — НАСТОЯЩИЙ клик Slate в точке экрана "
	     "(абсолютные координаты — те же, что печатают дампы хит-теста). Заведён под жалобу "
	     "«приходится нажимать по два раза»: мышью в headless-PIE не кликнуть, а звать колбэк "
	     "кнопки напрямую бесполезно — теряется как раз доставка события между нажатием и отпусканием. "
	     "★ 24.09.2026: четвёртый аргумент R шлёт ПРАВУЮ кнопку. До него правый клик в Slate послать "
	     "было нечем, и ПКМ-путь окна 43000 проверялся прямым вызовом обработчика — то есть не "
	     "проверялся вовсе (закон diagnostic-must-use-real-path). Именно так дефект «ПКМ по слоту "
	     "не открывает панель 43058» и дожил до живой игры."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: синтетический клик вслепую по координатам — при живом соединении нажмёт боевую кнопку/мир (покупка, ОК, удаление, 4006/4026…).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.UI.SimClick"))) { return; }
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 2)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.SimClick <X> <Y> [кадров] [L|R]"));
			return;
		}
		const FVector2D P(FCString::Atof(*A[0]), FCString::Atof(*A[1]));
		const bool bRight = A.Num() >= 4 && A[3].StartsWith(TEXT("R"), ESearchCase::IgnoreCase);
		S->SimulateClickAt(P, A.Num() >= 3 ? FCString::Atoi(*A[2]) : 8, bRight);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiDumpDrag(
	TEXT("asda.UI.DumpDrag"),
	TEXT("Ручки перетаскивания открытых окон: виджет, прямоугольник, цель, признак починенного бага оригинала"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaWindowSubsystem* S = AsdaGetWindowSub(W))
		{
			UE_LOG(LogAsdaWindow, Display, TEXT("%s"), *S->DebugDragHandles());
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ── ★★★ РАСКЛАДКА ОКОН МЕЖДУ СЕССИЯМИ: диагностика и сброс ──────────────────────────────────────
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiDumpWindowPos(
	TEXT("asda.UI.DumpWindowPos"),
	TEXT("Сохранённые в ini позиции и размеры окон: путь к файлу, что лежит в нём, что применено ")
	TEXT("сейчас, и по какому признаку каждое открытое окно сохраняется (или нет)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaWindowSubsystem* S = AsdaGetWindowSub(W))
		{
			UE_LOG(LogAsdaWindow, Display, TEXT("%s"), *S->DebugWindowLayout());
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ДИАГНОСТИКА ЗАКРЫТИЯ ПО ESC (25.09.2026). ЗАЧЕМ: проверить «окно закрывается по ESC»
//   настоящей клавишей на офскрин-стенде НЕЛЬЗЯ — синтетическая клавиша до обработчика ввода не
//   доходит (контрольная пара: инвентарь по «I» тоже не открывается, `IsInputKeyDown` = 0 при
//   «зажата»). Поэтому проверяем ВТОРУЮ половину пути — сам проход по реестру `EscClosers`,
//   который и закрывает верхнее окно. ⚠ ЭТО НЕ ЗАМЕНА проверке клавиши: если клавиша до
//   подсистемы не дойдёт, окно не закроется, сколько бы ни работал проход. Честная граница
//   названа здесь, чтобы следующая волна не приняла эту команду за доказательство целого.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiEscClose(
	TEXT("asda.UI.EscClose"),
	TEXT("asda.UI.EscClose — выполнить проход «закрыть верхнее окно по ESC» (`CloseTopmostOnEsc`) ")
	TEXT("БЕЗ настоящей клавиши. Проверяет реестр закрывателей, а не доставку ввода."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S) { return; }
		const bool bClosed = S->CloseTopmostOnEsc();
		UE_LOG(LogAsdaWindow, Display,
			TEXT("[окна] asda.UI.EscClose → %s"),
			bClosed ? TEXT("окно закрыто проходом ESC") : TEXT("закрывать было нечего"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiResetWindowPos(
	TEXT("asda.UI.ResetWindowPos"),
	TEXT("Сбросить сохранённые позиции/размеры окон к раскладке .JUI. Без аргумента — ВСЕ окна, ")
	TEXT("с аргументом — одно (имя окна как в asda.UI.DumpWindowPos, напр. INVEN_WIN)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S) { return; }
		S->ResetWindowLayout(Args.Num() > 0 ? FName(*Args[0]) : NAME_None);
		UE_LOG(LogAsdaWindow, Display, TEXT("%s"), *S->DebugWindowLayout());
	}));
#endif // ASDA_DEV_CONSOLE

// ── Дамп эталонной таблицы «окно закрывается по ESC» (порт `uiContainerHandleKey@0x12AF390`) ──
//   Нужен владельцу, чтобы сверить на живом: какие окна оригинала закрываются ESC, какие из них
//   есть в порту, что сейчас видимо и у кого подписан закрыватель.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiDumpEscWindows(
	TEXT("asda.UI.DumpEscWindows"),
	TEXT("Таблица окон оригинала с признаком «закрываюсь по ESC» (CControl+0x228): IDNum, есть ли ")
	TEXT("окно в реестре порта, видимо ли сейчас, подписан ли закрыватель, эталонная функция-источник."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S) { return; }
		UE_LOG(LogAsdaWindow, Display, TEXT("%s"), *S->DebugEscWindows());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiDumpZones(
	TEXT("asda.UI.DumpZones"),
	TEXT("Зоны интерфейса + окно под курсором (перехват мирового клика)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S) { return; }
		FName Zone;
		const bool bOver = S->IsCursorOverAnyUiWindow(Zone);
		// ★★★ ПОПРАВКА ФИНАЛЬНОЙ ПРИЁМКИ ВОЛНЫ 8: МОДАЛЬНЫЙ ГЕЙТ ТЕПЕРЬ ВИДЕН В ДАМПЕ.
		//   ЧТО СТОИЛО ЭТОГО МОЛЧАНИЯ. Открытый модальный `ONEBUT_MSGBOX` ставит ПОЛНОЭКРАННЫЙ
		//   прозрачный хит-тестируемый блокер на z = (z модалки − 1) (`RefreshModalInput`), и все
		//   окна под ним перестают получать клики Slate — это ЗАДУМАНО и повторяет эталон, где
		//   модальный контрол забирает весь диспетч мыши. Но дамп об этом МОЛЧАЛ: печатал
		//   «СЛОИ „ПОВЕРХ ВСЕГО“: нет активных» (блокер — не `TopmostClaim`) и бодрое
		//   «КУРСОР НАД UI: ДА, <окно>» (это НАША геометрия, а не хит-тест Slate).
		//   В итоге живой прогон выглядел как «окно 44200 не управляется мышью»: настоящие клики
		//   по 44239/44233/44232 не доходили, а прямой вызов обработчика окно закрывал. Диагноз
		//   «сломан путь доставки» был неверен — путь цел, клики ел модальный блокер.
		//   ДОКАЗАТЕЛЬСТВО (`Reports/pet/v8/game_44200_nomodal.log`): стоит закрыть нотис
		//   настоящим кликом по `ONEBUT_BUT00`, и та же кнопка 44239 тем же кликом закрывает
		//   окно («окно открыто=нет», :4117), а подпись `Pet_LimitBreak_OK` по наведению
		//   переключается с цвета N на H (`сейчас цвет=FF808080`, :4108) — то есть Slate доходит.
		//   Закон `false-negatives-method`: диагностика, которая молчит о причине, порождает
		//   ложные «нет». Печатаем модальное состояние явно.
		const bool bModalUp = S->IsAnyModalOpen();
		UE_LOG(LogAsdaWindow, Display, TEXT("%s\n%s\n%s\n  МОДАЛЬНЫЙ ГЕЙТ: %s\n  КУРСОР НАД UI: %s%s"),
			*S->DebugUiZones(), *S->DebugTopmost(), *S->DebugWheelHandlers(),
			bModalUp
				? TEXT("★ ОТКРЫТ — полноэкранный блокер ест клики ко ВСЕМ окнам ниже модалки "
				       "(это задумано; чтобы кликать нижние окна, сперва закройте модальный нотис)")
				: TEXT("нет, клики доходят до окон"),
			bOver ? TEXT("ДА, ") : TEXT("нет"),
			bOver ? (bModalUp
				? *FString::Printf(TEXT("%s — НО ЭТО ТОЛЬКО ГЕОМЕТРИЯ: под открытой модалкой "
				                          "Slate сюда НИЧЕГО НЕ ДОСТАВИТ"), *Zone.ToString())
				: *Zone.ToString())
				: TEXT(""));
	}));
#endif // ASDA_DEV_CONSOLE

// ── ★★★ МЕТРИКИ ТЕКСТА ОКНА (диагностика вертикального выравнивания подписей) ────────────────────
//   Заведено под дефект «имя персонажа в мини-статусе стоит не по центру плашки». Глазами по
//   снимку разницу в 2-3 пикселя не доказать, а вся арифметика центровки держится на ОДНОМ числе,
//   которого нет в офлайн-дампе `.uasset`: высоте строки шрифта (`GetMaxCharacterHeight`). Команда
//   печатает её вместе с базовой линией и реальным размером строки — по каждому TextBlock окна.
//   Общая, а не «для мини-статуса»: тот же перекос возможен у любой подписи любого окна.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiDumpTextMetrics(
	TEXT("asda.UI.DumpTextMetrics"),
	TEXT("asda.UI.DumpTextMetrics <ИдОкна> [ИмяВиджета] — по каждой подписи окна: слот (Top/высота), ")
	TEXT("высота строки шрифта, базовая линия, размер строки и экранный прямоугольник. Нужна, чтобы ")
	TEXT("проверять вертикальную центровку числом, а не на глаз."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 1)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.DumpTextMetrics <ИдОкна> [ИмяВиджета]"));
			return;
		}
		UUserWidget* Win = S->FindWindow(FName(*A[0]));
		if (!Win || !Win->WidgetTree)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("[метрики текста] окно «%s» не открыто"), *A[0]);
			return;
		}
		const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		FString Out = FString::Printf(TEXT("[метрики текста] окно «%s»:"), *A[0]);
		for (UWidget* Wg : All)
		{
			UTextBlock* Tb = Cast<UTextBlock>(Wg);
			if (!Tb) { continue; }
			if (A.Num() >= 2 && Tb->GetName() != A[1]) { continue; }
			const FSlateFontInfo Font = Tb->GetFont();
			const float LineH = (float)FM->GetMaxCharacterHeight(Font, 1.f);
			const float Base  = (float)FM->GetBaseline(Font, 1.f);
			const FVector2D Sz = FM->Measure(Tb->GetText().ToString(), Font, 1.f);
			FString SlotTxt = TEXT("слот: не канвас");
			if (UCanvasPanelSlot* Cs = Cast<UCanvasPanelSlot>(Tb->Slot))
			{
				const FMargin O = Cs->GetOffsets();
				SlotTxt = FString::Printf(TEXT("слот(L=%.2f T=%.2f W=%.2f H=%.2f)"), O.Left, O.Top, O.Right, O.Bottom);
			}
			FVector2D Mn(0.f), Mx(0.f);
			const bool bRect = AsdaUi::WidgetAbsRect(Tb, Mn, Mx);
			// ★ Горизонтальное выравнивание печатаем через рефлексию: у `UTextBlock` геттера нет,
			//   а свойство `Justification` объявлено в `UTextLayoutWidget` как protected.
			int32 JustNum = -1;
			if (const FProperty* JP = Tb->GetClass()->FindPropertyByName(FName(TEXT("Justification"))))
			{
				if (const FByteProperty* BP2 = CastField<FByteProperty>(JP))
				{
					JustNum = (int32)BP2->GetPropertyValue_InContainer(Tb);
				}
			}
			const TCHAR* JustTxt = (JustNum == (int32)ETextJustify::Center) ? TEXT("центр")
			                     : (JustNum == (int32)ETextJustify::Right)  ? TEXT("право")
			                     : (JustNum == (int32)ETextJustify::Left)   ? TEXT("лево") : TEXT("?");
			Out += FString::Printf(
				TEXT("\n  %-26s %s шрифт(%s %.0f) строка=%.2f базлиния=%.2f измерено=(%.1f x %.1f) вырав=%s экран=%s текст='%s'"),
				*Tb->GetName(), *SlotTxt, *Font.TypefaceFontName.ToString(), (float)Font.Size,
				LineH, Base, (float)Sz.X, (float)Sz.Y, JustTxt,
				bRect ? *FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : TEXT("нет"),
				*Tb->GetText().ToString());
		}
		UE_LOG(LogAsdaWindow, Display, TEXT("%s"), *Out);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Подстановка текста в подпись окна — нужна для ЖИВЫХ проверок раскладки: окно, открытое
//   `asda.UI.OpenByPath`, приходит с пустыми подписями (их наполняет рантайм подсистемы, которому
//   нужны данные сервера), и проверить центровку на пустой строке нельзя.
// ★★★ ДИАГНОСТИКА ПРАВИЛ ХИТ-ТЕСТА ЗАХВАТА (П8-11, поправка ревизии волны 9).
//   ЗАЧЕМ. `FindSlateCapturingWidgetUnderCursor` получила три правила — обрыв ветки на
//   `HitTestInvisible`, пропуск самого узла на `SelfHitTestInvisible`, наследование выключенности
//   родителя. Проверить их живьём было НЕЧЕМ: в порту нет ни одного окна, где такой узел лежал бы
//   под ручкой перетаскивания, а без опыта правило — это слова (закон `false-negatives-method`).
//   Эти две команды позволяют СОЗДАТЬ нужное состояние на живом окне и поставить пару
//   «до/после»: поменяли режим ⇒ окно начало (или перестало) таскаться за кнопку.
//   ⚠ Инструмент ПРОГОНА, не игровой путь: в бою видимость раздают импортёр и `ShownVisibilityFor`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSetVis(
	TEXT("asda.UI.SetVis"),
	TEXT("asda.UI.SetVis <ИдОкна> <ИмяВиджета> <Visible|HitTestInvisible|SelfHitTestInvisible|Hidden|Collapsed> ")
	TEXT("— сменить режим видимости узла открытого окна (для проверок хит-теста захвата)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 3)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.SetVis <ИдОкна> <ИмяВиджета> <режим>"));
			return;
		}
		UUserWidget* Win = S->FindWindow(FName(*A[0]));
		UWidget* Wg = Win ? Win->GetWidgetFromName(FName(*A[1])) : nullptr;
		if (!Wg)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("[смена видимости] в окне «%s» нет узла «%s»"), *A[0], *A[1]);
			return;
		}
		ESlateVisibility V = ESlateVisibility::Visible;
		if      (A[2].Equals(TEXT("HitTestInvisible"), ESearchCase::IgnoreCase))     { V = ESlateVisibility::HitTestInvisible; }
		else if (A[2].Equals(TEXT("SelfHitTestInvisible"), ESearchCase::IgnoreCase)) { V = ESlateVisibility::SelfHitTestInvisible; }
		else if (A[2].Equals(TEXT("Hidden"), ESearchCase::IgnoreCase))               { V = ESlateVisibility::Hidden; }
		else if (A[2].Equals(TEXT("Collapsed"), ESearchCase::IgnoreCase))            { V = ESlateVisibility::Collapsed; }
		Wg->SetVisibility(V);
		UE_LOG(LogAsdaWindow, Display, TEXT("[смена видимости] %s.%s ← %s"), *A[0], *A[1], *A[2]);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSetEnabled(
	TEXT("asda.UI.SetEnabled"),
	TEXT("asda.UI.SetEnabled <ИдОкна> <ИмяВиджета> <0|1> — включить/выключить узел открытого окна ")
	TEXT("(для проверки наследования выключенности родителя в хит-тесте захвата)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 3)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.SetEnabled <ИдОкна> <ИмяВиджета> <0|1>"));
			return;
		}
		UUserWidget* Win = S->FindWindow(FName(*A[0]));
		UWidget* Wg = Win ? Win->GetWidgetFromName(FName(*A[1])) : nullptr;
		if (!Wg)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("[вкл/выкл узла] в окне «%s» нет узла «%s»"), *A[0], *A[1]);
			return;
		}
		const bool bOn = (FCString::Atoi(*A[2]) != 0);
		Wg->SetIsEnabled(bOn);
		UE_LOG(LogAsdaWindow, Display, TEXT("[вкл/выкл узла] %s.%s ← %s"), *A[0], *A[1], bOn ? TEXT("включён") : TEXT("ВЫКЛЮЧЕН"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSetText(
	TEXT("asda.UI.SetText"),
	TEXT("asda.UI.SetText <ИдОкна> <ИмяВиджета> <текст…> — подставить текст в подпись открытого окна ")
	TEXT("(для проверок раскладки; в игре текст ставит подсистема окна)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 3)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.SetText <ИдОкна> <ИмяВиджета> <текст…>"));
			return;
		}
		UUserWidget* Win = S->FindWindow(FName(*A[0]));
		UTextBlock* Tb = Win ? Cast<UTextBlock>(Win->GetWidgetFromName(FName(*A[1]))) : nullptr;
		if (!Tb)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("[подстановка текста] в окне «%s» нет подписи «%s»"), *A[0], *A[1]);
			return;
		}
		TArray<FString> Rest = A;
		Rest.RemoveAt(0, 2);
		const FString Text = FString::Join(Rest, TEXT(" "));
		Tb->SetText(FText::FromString(Text));
		UE_LOG(LogAsdaWindow, Display, TEXT("[подстановка текста] %s.%s = «%s»"), *A[0], *A[1], *Text);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Применить к подписи открытого окна ВЕРТИКАЛЬНОЕ ВЫРАВНИВАНИЕ .JUI тем же общим кодом, что и
//   подсистемы окон (`AsdaUi::ApplyJuiTextAlign`). Нужна живым проверкам: окно, поднятое
//   `asda.UI.OpenByPath`, приходит БЕЗ разбора контролов своей подсистемой, и проверить правку
//   центровки на нём иначе нечем.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiAlignText(
	TEXT("asda.UI.AlignText"),
	TEXT("asda.UI.AlignText <ИдОкна> <ИмяВиджета> <маскаTextAlign> <ВысотаКонтроляJUI> [TextPos.y] — применить ")
	TEXT("выравнивание подписи по маске .JUI (0x1 центр, 0x2 вправо, 0x4 центр по вертикали, 0x8 низ). ")
	TEXT("⚠ ОДИН раз на подпись: функция читает Slot.Top как «запечённый генератором»."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWindowSubsystem* S = AsdaGetWindowSub(W);
		if (!S || A.Num() < 4)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("использование: asda.UI.AlignText <ИдОкна> <ИмяВиджета> <маска> <ВысотаJUI>"));
			return;
		}
		UUserWidget* Win = S->FindWindow(FName(*A[0]));
		UTextBlock* Tb = Win ? Cast<UTextBlock>(Win->GetWidgetFromName(FName(*A[1]))) : nullptr;
		if (!Tb)
		{
			UE_LOG(LogAsdaWindow, Warning, TEXT("[выравнивание текста] в окне «%s» нет подписи «%s»"), *A[0], *A[1]);
			return;
		}
		const int32 Mask = FCString::Atoi(*A[2]);
		const float JuiH = FCString::Atof(*A[3]);
		const float TPosY = (A.Num() >= 5) ? FCString::Atof(*A[4]) : 0.f;
		const float Was  = Cast<UCanvasPanelSlot>(Tb->Slot) ? Cast<UCanvasPanelSlot>(Tb->Slot)->GetOffsets().Top : 0.f;
		AsdaUi::ApplyJuiTextAlign(Tb, Mask, JuiH, TPosY);
		const float Now  = Cast<UCanvasPanelSlot>(Tb->Slot) ? Cast<UCanvasPanelSlot>(Tb->Slot)->GetOffsets().Top : 0.f;
		UE_LOG(LogAsdaWindow, Display,
			TEXT("[выравнивание текста] %s.%s маска=%d h(JUI)=%.1f TextPos.y=%.1f строка=%.2f запечено=%.2f Top %.2f→%.2f"),
			*A[0], *A[1], Mask, JuiH, TPosY, AsdaUi::MeasureLineHeight(Tb->GetFont(), JuiH),
			AsdaUi::JuiBakedTextVOffset(Mask, JuiH), Was, Now);
	}));
#endif // ASDA_DEV_CONSOLE
