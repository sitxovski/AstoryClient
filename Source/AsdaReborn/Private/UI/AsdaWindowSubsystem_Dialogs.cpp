// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_Dialogs.cpp — ★ 23.10.2026 часть UAsdaWindowSubsystem, вынесенная из AsdaWindowSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaWindowSubsystem.plan.json).
//   Очередь окон-сообщений ONEBUT/TWOBUT, окно имени подарка GIFT_MSGBOX, ввод текста SHOUTING_WIN, ввод количества NUMPAD_WIN.
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
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::Loc — подписи кнопок ТОЛЬКО из таблицы DT_GameText
#include "Text/AsdaTextSubsystem.h"   // игровые loc-подписи (2288 «ОК» / 2289 «Отмена») для TWOBUT_MSGBOX
#include "UI/AsdaUiHelpers.h"         // D1b: общая `AsdaUi::WidgetAbsRect` (хит-тест окна и рамки)
#include "UI/AsdaMessageSubsystem.h"  // D2: фолбэк однокнопочного msgbox, когда WBP не загрузился
#include "UI/AsdaHotbarSubsystem.h"      // шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара
// шаги 23-24: сворачивание трея и запрет его кнопок   ★ R3: комментарий снятого включения "UI/AsdaSystemMenuSubsystem.h" (файл им не пользовался) — сохранён
#include "Resources/AsdaSoundSubsystem.h" // звуки открытия/закрытия окна количества (5 / 6)
#include "UI/AsdaItemTooltipSubsystem.h"  // ★ 27.09.2026: панель 1530 для `DropTopMsgBoxBeforeNotice`
#include "UI/AsdaPurchaseBoxSubsystem.h"  // ★ 27.09.2026: панель 1559 для `DropTopMsgBoxBeforeNotice`
#include "Styling/SlateStyleRegistry.h"   // ★ гашение синей рамки фокуса: подмена кисти FocusRectangle
#include "Styling/SlateStyle.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateNoResource.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Login/AsdaText.h"                 // FAsdaText::Loc — строки диалога `asda.UI.ConfirmPreview` (07.10.2026)
#include "Login/AsdaLoginGameMode.h"          // ★ 08.10.2026: подъём при показе не трогает экран логина
#include "World/AsdaWorldKind.h"               // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "GameFramework/GameModeBase.h"     // GetAuthGameMode — проверка экрана логина
#include "UnrealClient.h"                   // С-1 (07.10.2026): FViewport::ViewportResizedEvent — смена размера вьюпорта
#include "Blueprint/GameViewportSubsystem.h" // М-1 (07.10.2026): слот окна во вьюпорте — полноэкранный ли он
#include "Widgets/SOverlay.h"               // ★ 08.10.2026: подъём окна БЕЗ снятия с вьюпорта (слот оверлея вьюпорта)
#include "UI/AsdaNotice.h"                  // ★ 08.10.2026 (ревизия В3 рыбалки): нотис 6835 строгого нумпада — LocRaw / ChatColored
#include "Widgets/SViewport.h"                // ★ 25.10.2026: цель фокуса режима ввода — игровой вьюпорт (`AsdaUiOnlyFocusTarget`)
#include "Widgets/Input/SEditableText.h"     // ★ 25.10.2026: поле 1412 — курсор в конец (`GoTo`), фильтр символов (`SetOnKeyCharHandler`)
#include "UI/AsdaChatSubsystem.h"           // ★ 08.10.2026: ColGold — цвет −11514 нотиса 6835
#include "UI/AsdaMarkup.h"                  // ★ 09.10.2026: UAsdaRichText::ApplyFormatted — теги <FC=…> в теле окон-сообщений
#include "Components/RichTextBlock.h"      // ★ 09.10.2026: тело окна-сообщения после первой подмены — URichTextBlock
#include "AsdaWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWindowSubsystem*.cpp (LogAsdaWindow, чистые хелперы)
#include "UI/AsdaEnterGuard.h"            // ★ 25.10.2026: пометка «без Enter» окон-вопросов (FAsdaMsgBoxEntry::bNoEnter)


// ══════════ ★★★ ОЧЕРЕДЬ ОКОН-СООБЩЕНИЙ ONEBUT 1500 / TWOBUT 1520 (26.09) ═════════════════════════
//   Полный разбор — шапка раздела в `AsdaWindowSubsystem.h` и `Reports/msgqueue/msgqueue.md`.
//   Коротко: эталонный менеджер `CMsgBoxMgr` держит СТЕК записей `+0x3340`; показ кладёт запись наверх и сразу
//   рисует её (25), кнопка снимает верхнюю (4), действие, закрытие вида возвращает следующую (25) и играет 5.

// Ключ отката исправлений дефектов ОРИГИНАЛА в очереди (закон fix-original-defects: исправляем и помечаем).
bool AsdaRelayAnchorSplitOn();   // ключ `asda.UI.RelayAnchorSplit` (UI/AsdaUiHelpers.cpp) — свой GC-якорь реле у каждого окна

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] — якорь кнопок TWOBUT_MSGBOX (разбор у `TwoButRelays` в заголовке).
// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] — полоса и гейт «Да» своего окна подтверждения (разбор у `FAsdaConfirmGauge` в заголовке).
static ASDA_DEV_CVAR(int32, CVarAsdaConfirmGaugeSplit,
	TEXT("asda.UI.ConfirmGaugeSplit"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = у логин-диалога MESSAGE2_WIN и игрового TWOBUT_MSGBOX своя полоса, таймер и гейт кнопки «Да» ")
	TEXT("(показ/закрытие одного не трогает другое); 0 = прежнее (одно общее состояние на оба окна)."),
	ECVF_Default);

UAsdaWindowSubsystem::FAsdaConfirmGauge& UAsdaWindowSubsystem::GaugeOf(bool bTwoBut)
{
	return (bTwoBut && bTwoButOwnGauge) ? TwoButGauge : LoginGauge;
}

void UAsdaWindowSubsystem::ReleaseTwoButGauge()
{
	if (bTwoButOwnGauge)
	{
		if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr) { W->GetTimerManager().ClearTimer(TwoButGauge.AnimTimer); }
	}
	GaugeOf(true).Dlg = nullptr;
}

TArray<TObjectPtr<UAsdaButtonRelay>>& UAsdaWindowSubsystem::TwoButRelayAnchor()
{
	return AsdaRelayAnchorSplitOn() ? TwoButRelays : ConfirmRelays;
}

static ASDA_DEV_CVAR(int32, CVarMsgBoxQueueFixes,
	TEXT("asda.MsgBox.QueueFixes"), 1,
	TEXT("Очередь окон-сообщений: 1 = исправления дефектов оригинала (возврат ВЕРХНЕЙ записи — той, что ответит кнопка; "
	     "без второго снятия записи после ответа на id 38/5001/5002; смерть снимает свою запись и возвращает следующую); "
	     "0 = как эталон байт в байт (возврат ПЕРВОЙ записи `sub_D7E120`, двойное снятие, снятие с конца)."),
	ECVF_Default);

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GCmdMsgBoxDumpQueue(
	TEXT("asda.MsgBox.DumpQueue"),
	TEXT("Дамп очереди окон-сообщений (стек CMsgBoxMgr+0x3340): вид, id, текст, текущие вид/id менеджера."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->DumpMsgBoxQueue(); }
	}));
#endif // ASDA_DEV_CONSOLE

namespace
{
	const FName kOneButId(TEXT("ONEBUT_MSGBOX"));   // эталонный контрол 1500
	const FName kTwoButId(TEXT("TWOBUT_MSGBOX"));   // эталонный контрол 1520
	const FName kGiftId(TEXT("GIFT_MSGBOX"));       // эталонный контрол 1552 (виды 59/60, волна 2-Г)

	// `resolvedType` эталона (item_inventory.c:50710-50797): для перечисленных видов в запись кладётся САМ вид,
	//   для остальных — noticeId. Дедуп при этом сравнивает ВХОДНОЙ noticeId с сохранённым resolvedType.
	int32 AsdaMsgBoxResolveId(int32 Kind, int32 NoticeId)
	{
		switch (Kind)
		{
		case 3: case 4: case 54: case 55: case 56: case 57: case 58: case 59: case 60: case 61: case 62:
		case 65: case 66: case 67: case 68: case 69: case 70: case 71: case 72: case 73:
		case 77: case 78: case 79: case 80: case 81: case 82: case 83: case 84: case 85:
			return Kind;
		default:
			return NoticeId;
		}
	}

	// Окно вида по ветвям `CGameUI_CloseWindow` (game_main.c:9800-9846): 0 → ONEBUT; 1,3,4,5,6,7,8 → TWOBUT;
	//   ★ 06.10.2026 (волна 2-Г): 59/60 → GIFT_MSGBOX 1552 (ввод имени для свитков призыва/перемещения).
	//   Виды 2 (1530) и 31 (1559) очередью порта не ведутся (свои подсистемы), вид 9 (1545) — мёртвый в оригинале
	//   (ставящих нет: 858 вызовов `CItem_BuildTooltipText` + `ConfirmDialog_Setup` без вызывающих) → NAME_None.
	FName AsdaMsgBoxWindowOfKind(int32 Kind)
	{
		if (Kind == 0) { return kOneButId; }
		if (Kind == 1 || (Kind >= 3 && Kind <= 8)) { return kTwoButId; }
		if (Kind == 59 || Kind == 60) { return kGiftId; }
		return NAME_None;
	}
}

UUserWidget* UAsdaWindowSubsystem::ShowGameConfirm(const FString& Message, TFunction<void(bool)> OnResult,
	float GaugeGateSec, int32 OkLocId, int32 CancelLocId, int32 EtalonNoticeId, int32 EtalonKind,
	float GaugeTimerSec, double GaugeTimerStartReal)
{
	// Вид обязан вести в TWOBUT (1,3..8); иначе — ошибка вызывающего, окно всё равно двухкнопочное.
	if (AsdaMsgBoxWindowOfKind(EtalonKind) != kTwoButId)
	{
		UE_LOG(LogAsdaWindow, Error, TEXT("[msgqueue] ShowGameConfirm: вид %d не двухкнопочный — принят вид 1"), EtalonKind);
		EtalonKind = 1;
	}
	FAsdaMsgBoxEntry E;
	E.bNoEnter = AsdaEnterGuard::ConsumeNextNoEnter();   // ★ 25.10.2026: «без Enter» от вызывающего (AsdaEnterGuard::NoEnter)
	E.Kind = EtalonKind;
	E.Id = (EtalonNoticeId == AsdaMsgBox::kNoKey) ? AsdaMsgBox::kNoKey : AsdaMsgBoxResolveId(EtalonKind, EtalonNoticeId);
	E.Text = Message;
	E.OkLocId = OkLocId;
	E.CancelLocId = CancelLocId;
	E.OnResult = MoveTemp(OnResult);
	E.GaugeGateSec = GaugeGateSec;
	E.GaugeTimerSec = GaugeTimerSec;              // ★ 27.09.2026: полоса-таймер без гейта (приглашение в клан, 30 с)
	E.GaugeTimerStartReal = GaugeTimerStartReal;
	E.ZOrder = 810;  // поверх окна смерти (kDeathZ 800); модалка всё равно берёт полосу kModalZ
	return PushMsgBoxChecked(MoveTemp(E), EtalonNoticeId);
}

UUserWidget* UAsdaWindowSubsystem::ShowGameMessage(const FString& Message, int32 OkLocId,
	EAsdaMsgCategory FallbackCategory, TFunction<void()> OnOk, int32 ZOrder, int32 EtalonNoticeId)
{
	const bool bNoEnter = AsdaEnterGuard::ConsumeNextNoEnter();   // ★ 25.10.2026: пометку снимает и пустой показ
	if (Message.IsEmpty()) { return nullptr; }   // пустой текст — ничего не показываем (как все сведённые копии)
	FAsdaMsgBoxEntry E;
	E.bNoEnter = bNoEnter;
	E.Kind = 0;
	E.Id = EtalonNoticeId;   // для вида 0 resolvedType = noticeId
	E.Text = Message;
	E.OkLocId = OkLocId;
	E.FallbackCategory = FallbackCategory;
	E.OnOk = MoveTemp(OnOk);
	E.ZOrder = ZOrder;
	return PushMsgBoxChecked(MoveTemp(E), EtalonNoticeId);
}

// Постановка = эталон `CItem_BuildTooltipText` (item_inventory.c:50699-50812): дедуп по (вид, ВХОДНОЙ noticeId)
//   против (вид, resolvedType) записей → в конец → текущие вид/id → показ.
UUserWidget* UAsdaWindowSubsystem::PushMsgBoxChecked(FAsdaMsgBoxEntry&& Entry, int32 RawNoticeId)
{
	if (RawNoticeId != AsdaMsgBox::kNoKey && IsMsgBoxQueued(Entry.Kind, RawNoticeId))
	{
		UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] ОТБРОШЕН дубль (вид %d, id %d): «%s» — пара уже в очереди (objList_ContainsPair), без звука"),
			Entry.Kind, RawNoticeId, *Entry.Text);
		return nullptr;
	}
	Entry.Serial = ++MsgBoxSerial;
	MsgBoxQueue.Add(MoveTemp(Entry));
	FAsdaMsgBoxEntry& E = MsgBoxQueue.Last();
	if (MsgBoxQueue.Num() > AsdaMsgBox::kEtalonCapacity)
	{
		UE_LOG(LogAsdaWindow, Warning, TEXT("[msgqueue] в очереди %d записей — у эталона ёмкость %d и запись за границу массива "
			"(дефект оригинала `vecPushBack40`); порт очередь не ограничивает"), MsgBoxQueue.Num(), AsdaMsgBox::kEtalonCapacity);
	}
	MsgBoxCurKind = E.Kind;       // `*(pMsgBox + 3262) = msgClass`
	MsgBoxCurId = RawNoticeId;    // `*(pMsgBox + 3263) = noticeId`
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] +#%u (вид %d, id %d) «%s» → в очереди %d"),
		E.Serial, E.Kind, E.Id, *E.Text, MsgBoxQueue.Num());
	UUserWidget* W = DisplayMsgBoxEntry(E, /*bSound*/true);
	if (!W)
	{
		// WBP не загрузился: запись ответить нечем — снимаем её (иначе дедуп вечно глушил бы такие же показы).
		FAsdaMsgBoxEntry Dead = MsgBoxQueue.Pop();
		if (Dead.OnResult) { Dead.OnResult(false); }   // прежнее поведение `ShowGameConfirm`: окна нет ⇒ «нет»
	}
	return W;
}

UUserWidget* UAsdaWindowSubsystem::DisplayMsgBoxEntry(const FAsdaMsgBoxEntry& E, bool bSound)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const FName WinId = AsdaMsgBoxWindowOfKind(E.Kind);
	if (WinId == kGiftId) { return DisplayGiftMsgBox(E, bSound); }   // ★ волна 2-Г: окно 1552 — своё тело показа
	const bool bTwoBut = (WinId == kTwoButId);
	const bool bWasOpen = IsOpen(WinId);

	UUserWidget* Box = OpenWindowByPath(WinId, bTwoBut
			? TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_TWOBUT_MSGBOX")
			: TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_ONEBUT_MSGBOX"),
		E.ZOrder, ESlateVisibility::Visible, /*bModal*/true);
	if (!Box)
	{
		UE_LOG(LogAsdaWindow, Warning, TEXT("[msgbox%s] WBP_%s не загрузился; сообщение: %s"),
			bTwoBut ? TEXT("") : TEXT("1"), *WinId.ToString(), *E.Text);
		if (!bTwoBut)
		{
			if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				Msg->PostText(E.FallbackCategory, E.Text);   // ★ Аудит №3: канал задаёт вызывающий (книга навыков — SkillRefused)
			}
		}
		return nullptr;
	}

	// ★ Новое содержимое окна, уже стоящего под другой модалкой, эталон делает модальным (`helper_setupFields`):
	//   поднимаем окно над остальными модалками. Только если оно не верхнее — перевешивание съедает зажатый клик.
	if (bWasOpen)
	{
		const FAsdaWindowEntry* Me = Windows.Find(WinId);
		int32 MaxOther = TNumericLimits<int32>::Lowest();
		for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
		{
			if (Kv.Key != WinId && Kv.Value.bModal && Kv.Value.Widget) { MaxOther = FMath::Max(MaxOther, Kv.Value.ZOrder); }
		}
		if (Me && Me->ZOrder <= MaxOther) { SetWindowZOrder(WinId, AllocateModalZ()); }
	}

	if (bTwoBut)
	{
		// Тело сообщения TWOBUT_TEXT: JUI rect 300×90 @ (45,5), TextAlign 5 = центр/центр, TextColor FFFFFFFF.
		//   ★ 09.10.2026: текст несёт теги `<FC=…>` (111018 и ещё 59 строк) — показ через единую точку тегов
		//   `SetMsgBoxBodyFormatted` (эталон `CControl_SetText`, item_inventory.c:51348, теги разбирает рендер JUI).
		if (!SetMsgBoxBodyFormatted(Box, TEXT("TWOBUT_TEXT"), E.Text, /*WrapW*/300.f, /*bForceWhite*/true))
		{
			UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox] TWOBUT_TEXT не найден — текст поставить некуда"));
		}

		// Подписи кнопок ТОЛЬКО из DT_GameText: BUT00 = loc 2288 «ОК», BUT01 = loc 2289 «Отмена»
		//   (эталон CMsgBox_SetContent LABEL_65, ui_core.c:56755-56758). CaptionInfo обеих: align 5 → центр/центр.
		auto SetBtnLabel = [&](const TCHAR* N, int32 LocId)
		{
			UTextBlock* T = Cast<UTextBlock>(Box->GetWidgetFromName(FName(*(FString(N) + TEXT("_Text")))));
			if (!T) { UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox] %s_Text не найден — подпись кнопки не поставлена"), N); return; }
			const FString S = (Txt && LocId > 0) ? Txt->GetTextById(LocId) : FString();
			if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
			T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			CenterTextBlockInSlot(T, /*WrapW*/0.f);   // подпись однострочная → без переноса (закон autowrap)
			UE_LOG(LogAsdaWindow, Log, TEXT("[msgbox] подпись %s = loc %d '%s' (шрифт %.0f, центр/центр)"),
				N, LocId, *S, T->GetFont().Size);
		};
		SetBtnLabel(TEXT("TWOBUT_BUT00"), E.OkLocId);
		SetBtnLabel(TEXT("TWOBUT_BUT01"), E.CancelLocId);

		// Кнопки: BUT00 = согласие, BUT01 = отмена. Реле не знает записи: ответ снимает верхнюю (как эталон).
		TwoButRelayAnchor().Reset();   // ★ 31.10.2026: свой якорь TWOBUT (`asda.UI.RelayAnchorSplit`), логин-диалога не трогаем
		// ★ 31.10.2026 (ревизия): экземпляр полосы TWOBUT решается здесь, при показе, и запоминается до следующего показа.
		bTwoButOwnGauge = CVarAsdaConfirmGaugeSplit.GetValueOnGameThread() != 0;
		GaugeOf(true).YesButton = nullptr;
		GaugeOf(true).bGateYes = (E.GaugeGateSec > 0.f);
		// ★ 27.09.2026: полоса-таймер без гейта (`uiProgressTickTimedAction@0xD78C20`, ui_core.c:58335) — кнопки активны.
		const bool bTimerGauge = !GaugeOf(true).bGateYes && E.GaugeTimerSec > 0.f && E.GaugeTimerStartReal >= 0.0;
		GaugeOf(true).FullW = 0.f;
		GaugeOf(true).LastLoggedQuarter = -1;
		auto BindBtn = [&](const TCHAR* N, bool bYes)
		{
			UButton* B = Cast<UButton>(Box->GetWidgetFromName(FName(N)));
			if (!B) { UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox] кнопка %s не найдена"), N); return; }
			B->OnClicked.Clear();
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Callback = [this, bYes](int32) { AnswerMsgBox(/*bTwoBut*/true, bYes); };
			B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(TwoButRelayAnchor(), R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
			if (bYes)
			{
				GaugeOf(true).YesButton = B;
				// Disabled-кадра в JUI-атласе НЕТ (кадры 0..3 = normal/hover/pressed/selected) — блокировку
				//   показываем притушением кнопки; полный вид вернёт заполнение полосы (TickConfirmGauge).
				B->SetIsEnabled(!GaugeOf(true).bGateYes);
				B->SetRenderOpacity(GaugeOf(true).bGateYes ? 0.55f : 1.f);
			}
		};
		BindBtn(TEXT("TWOBUT_BUT00"), true);
		BindBtn(TEXT("TWOBUT_BUT01"), false);

		// Полоса TWOBUT_GAUGE: в JUI её подложка Visible=0 и эталон скрывает полосу при init (ui_core.c:55952).
		//   Показываем ТОЛЬКО в gate-режиме; иначе прячем (как оригинал).
		if (Box->WidgetTree)
		{
			TArray<UWidget*> All; Box->WidgetTree->GetAllWidgets(All);
			int32 NGauge = 0;
			for (UWidget* Wd : All)
			{
				if (!Wd || !Wd->GetName().StartsWith(TEXT("TWOBUT_GAUGE"))) { continue; }
				++NGauge;
				Wd->SetVisibility((GaugeOf(true).bGateYes || bTimerGauge) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			}
			UE_LOG(LogAsdaWindow, Log, TEXT("[msgbox] полоса TWOBUT_GAUGE*: виджетов %d → %s"),
				NGauge, GaugeOf(true).bGateYes ? TEXT("ПОКАЗАНА (gate)") : (bTimerGauge ? TEXT("ПОКАЗАНА (таймер эталона)") : TEXT("скрыта (эталон)")));
		}

		// Заливка полосы + разблокировка ОК по заполнении (TickConfirmGauge). Авто-исхода по таймеру НЕТ:
		//   полоса только открывает согласие, решение всегда за игроком (требование юзера). При возврате записи
		//   из очереди полоса запускается заново — запись показывается как новая (эталон полосы здесь не имеет).
		//   ★ 27.09.2026: всё сказанное — про gate-полосу (`GaugeGateSec`). КРОМЕ полосы-таймера (`GaugeTimerSec`,
		//   окно приглашения в клан (1, 44)): она заливается от старта ВЛАДЕЛЬЦА (`GaugeTimerStartReal`), при возврате
		//   из очереди НЕ с нуля, а исход по истечении (4111 «нет» и снятие записи) делает сам владелец, не это окно.
		GaugeOf(true).Dlg = Box;
		GaugeOf(true).Prefix = TEXT("TWOBUT_GAUGE");
		GaugeOf(true).Duration = bTimerGauge ? E.GaugeTimerSec : E.GaugeGateSec;
		// ★ 27.09.2026: полоса-таймер тянется от старта владельца — при возврате записи из очереди НЕ с нуля.
		GaugeOf(true).RealStart = bTimerGauge ? E.GaugeTimerStartReal : -1.0;
		if (UWorld* W = GI ? GI->GetWorld() : nullptr)
		{
			W->GetTimerManager().ClearTimer(GaugeOf(true).AnimTimer);
			if (GaugeOf(true).bGateYes || bTimerGauge)
			{
				GaugeOf(true).StartTime = W->GetTimeSeconds();
				TickConfirmGaugeOf(true);   // сразу 0 % + лог «заблокирована»
				W->GetTimerManager().SetTimer(GaugeOf(true).AnimTimer, FTimerDelegate::CreateUObject(this, &UAsdaWindowSubsystem::TickConfirmGaugeOf, true), 0.033f, true);
			}
		}
		UE_LOG(LogAsdaWindow, Log, TEXT("[msgbox] TWOBUT показано: %s"), *E.Text);
		CloseWorldMapForMsgBox();
		if (bSound) { PlayMsgBoxSfx(25, TEXT("показ TWOBUT_MSGBOX (CItem_BuildTooltipText / CMsgBox_SetContent)")); }
		return Box;
	}

	// ── ONEBUT 1500 (эталон `CMsgBoxMgr_InitControls` ui_core.c:55951-55958: kind 0 ⇒ полоса `ONEBUT_GAUGE` скрыта,
	//   окно центрируется по экрану — bModal-путь `OpenWindowByPath` → CenterModalContent, текст 1501 центр/центр по
	//   rect 270×100, подпись кнопки 1503 = loc 2288 «ОК»).
	// Тело сообщения ONEBUT_TEXT 1501: перенос делает CenterTextBlockInSlot по авторской ширине 270
	//   (AutoWrap в CanvasPanel — закон `ui-autowrap-canvas-gotcha`, поэтому он явно выключен).
	//   ★ 09.10.2026: теги `<FC=…>` разбирает единая точка `SetMsgBoxBodyFormatted` (раньше были видны как текст).
	if (!SetMsgBoxBodyFormatted(Box, TEXT("ONEBUT_TEXT"), E.Text, /*WrapW*/270.f, /*bForceWhite*/false))
	{
		UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox1] ONEBUT_TEXT не найден — текст поставить некуда"));
	}

	// Подпись кнопки ТОЛЬКО из DT_GameText (закон `no-invented-ui`): пусто ⇒ оставляем как в WBP.
	if (UTextBlock* BT = Cast<UTextBlock>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00_Text"))))
	{
		const FString S = (Txt && E.OkLocId > 0) ? Txt->GetTextById(E.OkLocId) : FString();
		if (!S.IsEmpty()) { BT->SetText(FText::FromString(S)); }
		BT->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		CenterTextBlockInSlot(BT, /*WrapW*/0.f);   // подпись однострочная
	}

	// Полоса ONEBUT_GAUGE*: в ветке kind 0 эталон её не показывает — прячем все планы.
	if (Box->WidgetTree)
	{
		TArray<UWidget*> All; Box->WidgetTree->GetAllWidgets(All);
		for (UWidget* W : All)
		{
			if (W && W->GetName().StartsWith(TEXT("ONEBUT_GAUGE"))) { W->SetVisibility(ESlateVisibility::Collapsed); }
		}
	}

	// Кнопка ONEBUT_BUT00 1503 — единственный выход из окна. Реле не знает записи: ответ снимает верхнюю (как эталон).
	MessageRelays.Reset();
	if (UButton* B = Cast<UButton>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00"))))
	{
		B->OnClicked.Clear();
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { AnswerMsgBox(/*bTwoBut*/false, /*bYes*/true); };
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(MessageRelays, R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	else { UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox1] кнопка ONEBUT_BUT00 не найдена — окно нечем закрыть")); }

	UE_LOG(LogAsdaWindow, Log, TEXT("[msgbox1] показано: %s"), *E.Text);
	CloseWorldMapForMsgBox();
	if (bSound) { PlayMsgBoxSfx(25, TEXT("показ ONEBUT_MSGBOX (CItem_BuildTooltipText / CMsgBox_SetContent)")); }
	return Box;
}

// Эталон: показ окна-сообщения (`CItem_BuildTooltipText` LABEL_360, item_inventory.c:51588, и `CMsgBox_SetContent`
//   LABEL_94, ui_core.c:56764) зовёт `miniMapWndClose(g_pMiniMapWnd)` перед звуком 25 — закрывает КАРТУ МИРА 1710
//   (миснейм: `this[10]` = `pBigMapWin`), если она открыта, со звуком 6. В порту это роспись закрытия `MAXMAP_WIN`
//   (её регистрирует `UAsdaMinimapSubsystem`, гвард «открыта» — внутри неё). Одна точка на оба окна.
void UAsdaWindowSubsystem::CloseWorldMapForMsgBox()
{
	for (const TPair<FName, FSimpleDelegate>& Kv : WindowClosers)
	{
		if (Kv.Key == FName(TEXT("MAXMAP_WIN")))
		{
			const FSimpleDelegate D = Kv.Value;   // копия: закрыватель вправе тронуть реестр
			D.ExecuteIfBound();
			return;
		}
	}
}

int32 UAsdaWindowSubsystem::FindMsgBoxAnswerIndex(bool bTwoBut) const
{
	if (MsgBoxQueue.Num() == 0) { return INDEX_NONE; }
	// Эталон: `recordVec_popBack` — всегда ПОСЛЕДНЯЯ запись, чья бы она ни была.
	if (CVarMsgBoxQueueFixes.GetValueOnGameThread() == 0) { return MsgBoxQueue.Num() - 1; }
	const FName Want = bTwoBut ? kTwoButId : kOneButId;
	for (int32 i = MsgBoxQueue.Num() - 1; i >= 0; --i)
	{
		if (AsdaMsgBoxWindowOfKind(MsgBoxQueue[i].Kind) == Want)
		{
			if (i != MsgBoxQueue.Num() - 1)
			{
				UE_LOG(LogAsdaWindow, Warning, TEXT("[msgqueue] ответ окна %s, а сверху запись другого окна — снята запись #%u "
					"(у эталона нижнее окно не нажать: модальна верхняя)"), *Want.ToString(), MsgBoxQueue[i].Serial);
			}
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UAsdaWindowSubsystem::GetMsgBoxRedisplayIndex() const
{
	if (MsgBoxQueue.Num() == 0) { return INDEX_NONE; }
	// ★ Дефект ОРИГИНАЛА: `sub_92A049` → `sub_D7E120` отдаёт НАЧАЛО массива, а кнопка снимает КОНЕЦ.
	return CVarMsgBoxQueueFixes.GetValueOnGameThread() == 0 ? 0 : MsgBoxQueue.Num() - 1;
}

// Ответ = эталон `OnSendItemUpgrade@0xD74180`: снять запись (ui_core.c:56948-56953), звук 4 (1503 :56957, 1523 :57052,
//   1524 :57682), действие по id (у порта — колбэк вызывающего), `CGameUI_CloseWindow(вид)` (5 + возврат следующей).
void UAsdaWindowSubsystem::AnswerMsgBox(bool bTwoBut, bool bYes)
{
	const FName WinId = bTwoBut ? kTwoButId : kOneButId;
	const int32 Idx = FindMsgBoxAnswerIndex(bTwoBut);
	if (Idx == INDEX_NONE)
	{
		// Окно открыто без записи (не должно случаться): эталон снял бы «пустую» запись (вид/id −1) и закрыл вид −1,
		//   т.е. ничего не спрятал бы. Порт окно всё же убирает — иначе игрок заперт модалкой.
		UE_LOG(LogAsdaWindow, Warning, TEXT("[msgqueue] ответ %s без записи в очереди — окно снято"), *WinId.ToString());
		MsgBoxCurKind = -1; MsgBoxCurId = -1;
		PlayMsgBoxSfx(4, bTwoBut ? (bYes ? TEXT("TWOBUT «Да» 1523") : TEXT("TWOBUT «Нет» 1524")) : TEXT("ONEBUT «ОК» 1503"));
		RemoveMsgBoxWidget(bTwoBut);
		PlayMsgBoxSfx(5, TEXT("закрытие без записи (CGameUI_CloseWindow)"));
		return;
	}
	FAsdaMsgBoxEntry E = MoveTemp(MsgBoxQueue[Idx]);
	MsgBoxQueue.RemoveAt(Idx);
	MsgBoxCurKind = E.Kind;   // `*(menuWnd + 3262) = menuSel[0]`
	MsgBoxCurId = E.Id;       // `*(menuWnd + 3263) = menuSel[1]` (resolvedType)
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] -#%u ответ %s (вид %d, id %d) «%s» → в очереди %d"),
		E.Serial, bTwoBut ? (bYes ? TEXT("«Да»") : TEXT("«Нет»")) : TEXT("«ОК»"), E.Kind, E.Id, *E.Text, MsgBoxQueue.Num());
	if (bTwoBut)
	{
		if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr) { W->GetTimerManager().ClearTimer(GaugeOf(true).AnimTimer); }
		ReleaseTwoButGauge();
	}
	PlayMsgBoxSfx(4, bTwoBut ? (bYes ? TEXT("TWOBUT «Да» 1523") : TEXT("TWOBUT «Нет» 1524")) : TEXT("ONEBUT «ОК» 1503"));
	// ★ Редакция 1 (ревизия С-1, ОПРОВЕРГНУТО): сразу после звука 4 эталон прячет НЕ окно, а ПОЛОСУ окна:
	//   1503 — `CControl_SetVisible(menuWnd + 4, 0)` = dword 4 = +0x10 = `pOneBtnGauge` (контрол 1502), 1523/1524 —
	//   `menuWnd + 9` = +0x24 = `pTwoBtnGauge` (1522) (ui_core.c:56958/57053/57683; смещения — `structs_ru.h:1847-1857`,
	//   id — `CMsgBoxMgr_InitControls` ui_core.c:55662/55681; второй метод: `CItem_BuildTooltipText` зовёт по тем же
	//   +4/+9 `CProgressBar_SetValue`, item_inventory.c:50906/50974, а окна — это +1/+6). Окно остаётся видимым и
	//   модальным на всё время действия и прячется только в `CGameUI_CloseWindow`. Порт: прячем полосу своего окна.
	if (UUserWidget* Box = FindWindow(WinId))
	{
		if (Box->WidgetTree)
		{
			const FString Prefix = bTwoBut ? TEXT("TWOBUT_GAUGE") : TEXT("ONEBUT_GAUGE");
			TArray<UWidget*> All; Box->WidgetTree->GetAllWidgets(All);
			for (UWidget* Wd : All) { if (Wd && Wd->GetName().StartsWith(Prefix)) { Wd->SetVisibility(ESlateVisibility::Collapsed); } }
		}
	}
	// Действие — ДО закрытия, как у эталона: колбэк может поставить новое сообщение, и `CGameUI_CloseWindow`
	//   ниже вернёт его из очереди (25) — цепочка «ответ → новое окно» звучит 4, 25, 25, 5, как оригинал.
	if (bTwoBut) { if (E.OnResult) { E.OnResult(bYes); } }
	else if (E.OnOk) { E.OnOk(); }
	CloseMsgBoxKind(E.Kind, /*bHide*/true, /*bSilent*/false, /*bAfterAnswer*/true, /*bDeferDisplay*/false);
}

// Порт `CGameUI_CloseWindow@0xD73DD0` (game_main.c:9788-9889).
void UAsdaWindowSubsystem::CloseMsgBoxKind(int32 Kind, bool bHide, bool bSilent, bool bAfterAnswer, bool bDeferDisplay)
{
	const FName WinId = AsdaMsgBoxWindowOfKind(Kind);
	if (bHide && !WinId.IsNone()) { RemoveMsgBoxWidgetOfKind(Kind); }   // ★ волна 2-Г: вид → своё окно (в т.ч. 1552)
	const bool bFixes = CVarMsgBoxQueueFixes.GetValueOnGameThread() != 0;
	// Виды 1,3,4,5,6 с текущим id 38 (зов друга) и любой вид с текущим id 5001/5002 (обмен) снимают ЕЩЁ одну
	//   запись (:9811-9818, :9853-9861). Для программного закрытия это и есть снятие своей записи; после ответа
	//   кнопкой запись уже снята — второе снятие у эталона уносит ЧУЖУЮ запись (дефект, исправлен при QueueFixes).
	const bool bExtraPop38 = (MsgBoxCurId == 38) && (Kind == 1 || (Kind >= 3 && Kind <= 6));
	const bool b500x = (MsgBoxCurId == 5001 || MsgBoxCurId == 5002);
	if ((bExtraPop38 || b500x) && MsgBoxQueue.Num() && (!bAfterAnswer || !bFixes))
	{
		const FAsdaMsgBoxEntry Gone = MsgBoxQueue.Pop();
		UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] закрытие (id %d): снята запись #%u (вид %d, id %d)%s"),
			MsgBoxCurId, Gone.Serial, Gone.Kind, Gone.Id, bAfterAnswer ? TEXT(" — ВТОРОЕ снятие эталона") : TEXT(""));
	}
	if (b500x)
	{
		// 5001/5002: только снять модальность — следующая запись НЕ возвращается (:9853-9862).
		if (!bSilent) { PlayMsgBoxSfx(5, TEXT("закрытие обмена 5001/5002 (CGameUI_CloseWindow)")); }
		return;
	}
	if (MsgBoxQueue.Num())
	{
		const int32 Idx = GetMsgBoxRedisplayIndex();
		if (bDeferDisplay)
		{
			// Загрузка мира: окна виджетами не рисуем (экран загрузки), звук возврата — сейчас, окно — после `CloseAll`.
			if (!bSilent) { PlayMsgBoxSfx(25, TEXT("возврат записи из очереди при загрузке мира (CMsgBox_SetContent)")); }
		}
		else
		{
			const FAsdaMsgBoxEntry Copy = MsgBoxQueue[Idx];   // копия: показ может тронуть очередь через реле
			MsgBoxCurKind = Copy.Kind;
			MsgBoxCurId = Copy.Id;
			UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] возврат #%u (вид %d, id %d) «%s»"), Copy.Serial, Copy.Kind, Copy.Id, *Copy.Text);
			DisplayMsgBoxEntry(Copy, /*bSound*/!bSilent);
		}
	}
	if (!bSilent)
	{
		PlayMsgBoxSfx(5, *FString::Printf(TEXT("закрытие вида %d (CGameUI_CloseWindow)"), Kind));
	}
}

void UAsdaWindowSubsystem::RemoveMsgBoxWidget(bool bTwoBut)
{
	TGuardValue<bool> Raw(bMsgBoxRawClose, true);
	CloseWindow(bTwoBut ? kTwoButId : kOneButId);
	if (bTwoBut) { TwoButRelayAnchor().Reset(); } else { MessageRelays.Reset(); }   // ★ 31.10.2026: свой якорь TWOBUT
}

// ══════ ★ 06.10.2026 (волна 2-Г): ОКНО ВВОДА ИМЕНИ `GIFT_MSGBOX` 1552 (виды 59/60) — разбор в объявлении ══════

void UAsdaWindowSubsystem::RemoveMsgBoxWidgetOfKind(int32 Kind)
{
	const FName WinId = AsdaMsgBoxWindowOfKind(Kind);
	if (WinId == kGiftId)
	{
		TGuardValue<bool> Raw(bMsgBoxRawClose, true);
		CloseWindow(kGiftId);
		GiftRelays.Reset();
		return;
	}
	if (WinId.IsNone()) { return; }
	RemoveMsgBoxWidget(WinId == kTwoButId);
}

int32 UAsdaWindowSubsystem::FindMsgBoxAnswerIndexFor(FName WinId) const
{
	if (MsgBoxQueue.Num() == 0) { return INDEX_NONE; }
	// Эталон: `recordVec_popBack` — всегда ПОСЛЕДНЯЯ запись (как `FindMsgBoxAnswerIndex`).
	if (CVarMsgBoxQueueFixes.GetValueOnGameThread() == 0) { return MsgBoxQueue.Num() - 1; }
	for (int32 i = MsgBoxQueue.Num() - 1; i >= 0; --i)
	{
		if (AsdaMsgBoxWindowOfKind(MsgBoxQueue[i].Kind) == WinId) { return i; }
	}
	return INDEX_NONE;
}

UUserWidget* UAsdaWindowSubsystem::ShowGiftMsgBox(int32 Kind, const FString& Text, TFunction<void(bool, const FString&)> OnTextResult)
{
	if (Kind != 59 && Kind != 60)
	{
		UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox1552] вид %d не окна 1552 — показ отклонён"), Kind);
		return nullptr;
	}
	FAsdaMsgBoxEntry E;
	E.bNoEnter = AsdaEnterGuard::ConsumeNextNoEnter();   // ★ 25.10.2026: «без Enter» от вызывающего
	E.Kind = Kind;
	E.Id = AsdaMsgBoxResolveId(Kind, -1);   // `resolvedType` = сам вид (item_inventory.c:50728-50733)
	E.Text = Text;
	E.OkLocId = 2288;                       // 1554 «ОК» (`CMsgBoxMgr_InitControls`, ui_core.c:55976)
	E.CancelLocId = 2289;                   // 1555 «Отмена» (:55978)
	E.OnTextResult = MoveTemp(OnTextResult);
	E.ZOrder = 810;
	UUserWidget* W = PushMsgBoxChecked(MoveTemp(E), /*RawNoticeId*/-1);
	// Только ПЕРВЫЙ показ (`CItem_BuildTooltipText`, item_inventory.c:51247-51251): чат теряет фокус ввода, фокус — в 1558.
	//   Возврат записи из очереди (`CMsgBox_SetContent`) фокус не ставит. ★ Фокус — ПОСЛЕ модального открытия
	//   (`RefreshModalInput` уводит фокус на корень модалки — та же грабля, что у окна ввода 1580).
	if (W)
	{
		if (UEditableText* Ed = Cast<UEditableText>(W->GetWidgetFromName(TEXT("GIFT_EDIT")))) { Ed->SetKeyboardFocus(); }
		// ★ 07.10.2026 (ревизия 2-Г, С-1): вход с ПАНЕЛИ БЫСТРЫХ СЛОТОВ применяет слот на отпускании ЛКМ — Slate в том же
		//   и следующем кадре отдаёт фокус вьюпорту/корню модалки, и набранное имя уходило мимо 1558. Фокус ставим ещё раз
		//   следующим тиком (пока окно 1552 на экране и его запись верхняя) — у эталона фокус после показа держит поле.
		if (UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
		{
			Wd->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (MsgBoxQueue.Num() == 0 || !MsgBoxQueue.Last().IsGift()) { return; }
				UUserWidget* Box = FindWindow(kGiftId);
				if (UEditableText* Ed2 = Box ? Cast<UEditableText>(Box->GetWidgetFromName(TEXT("GIFT_EDIT"))) : nullptr) { Ed2->SetKeyboardFocus(); }
			}));
		}
	}
	return W;
}

UUserWidget* UAsdaWindowSubsystem::DisplayGiftMsgBox(const FAsdaMsgBoxEntry& E, bool bSound)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const bool bWasOpen = IsOpen(kGiftId);
	UUserWidget* Box = OpenWindowByPath(kGiftId, TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_GIFT_MSGBOX"),
		E.ZOrder, ESlateVisibility::Visible, /*bModal*/true);   // окно центрируется по экрану (InitControls :55980-55986)
	if (!Box)
	{
		UE_LOG(LogAsdaWindow, Warning, TEXT("[msgbox1552] WBP_GIFT_MSGBOX не загрузился; сообщение: %s"), *E.Text);
		if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr) { Msg->PostText(E.FallbackCategory, E.Text); }
		return nullptr;
	}
	if (bWasOpen)
	{
		const FAsdaWindowEntry* Me = Windows.Find(kGiftId);
		int32 MaxOther = TNumericLimits<int32>::Lowest();
		for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
		{
			if (Kv.Key != kGiftId && Kv.Value.bModal && Kv.Value.Widget) { MaxOther = FMath::Max(MaxOther, Kv.Value.ZOrder); }
		}
		if (Me && Me->ZOrder <= MaxOther) { SetWindowZOrder(kGiftId, AllocateModalZ()); }
	}
	// 1553 `GIFT_TEXT00` — rect 270×60, TextAlign 5 (центр/центр), белый (`.JUI`).
	//   ★ 09.10.2026: теги `<FC=…>` разбирает единая точка `SetMsgBoxBodyFormatted`.
	if (!SetMsgBoxBodyFormatted(Box, TEXT("GIFT_TEXT00"), E.Text, /*WrapW*/270.f, /*bForceWhite*/false))
	{
		UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox1552] GIFT_TEXT00 не найден — текст поставить некуда"));
	}
	// Подписи 1554/1555 — ТОЛЬКО из таблицы текстов (2288 / 2289).
	auto SetBtnLabel = [&](const TCHAR* N, int32 LocId)
	{
		UTextBlock* T = Cast<UTextBlock>(Box->GetWidgetFromName(FName(*(FString(N) + TEXT("_Text")))));
		if (!T) { UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox1552] %s_Text не найден"), N); return; }
		const FString S = (Txt && LocId > 0) ? Txt->GetTextById(LocId) : FString();
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
		T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		CenterTextBlockInSlot(T, /*WrapW*/0.f);
	};
	SetBtnLabel(TEXT("GIFT_BUT00"), E.OkLocId);
	SetBtnLabel(TEXT("GIFT_BUT01"), E.CancelLocId);
	// Реле: ответ снимает запись окна 1552 (как эталон — верхнюю).
	GiftRelays.Reset();
	auto BindBtn = [&](const TCHAR* N, bool bOk)
	{
		UButton* B = Cast<UButton>(Box->GetWidgetFromName(FName(N)));
		if (!B) { UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox1552] кнопка %s не найдена"), N); return; }
		B->OnClicked.Clear();
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this, bOk](int32) { AnswerGiftMsgBox(bOk); };
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(GiftRelays, R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	};
	BindBtn(TEXT("GIFT_BUT00"), true);    // 1554
	BindBtn(TEXT("GIFT_BUT01"), false);   // 1555
	// 1558 `GIFT_EDIT` — `CControl_SetText_12F9850(pGiftEdit, "")` при каждом показе (и возврате из очереди).
	//   LimitText = 20 (`.JUI`): жёсткий стоп набора в байтах cp1251 (= символах), как у окна ввода 1580.
	if (UEditableText* Ed = Cast<UEditableText>(Box->GetWidgetFromName(TEXT("GIFT_EDIT"))))
	{
		Ed->SetText(FText::GetEmpty());
		Ed->SetVisibility(ESlateVisibility::Visible);
		Ed->OnTextChanged.Clear();
		Ed->OnTextCommitted.Clear();
		UAsdaTextLimiter* L = NewObject<UAsdaTextLimiter>(this);
		TWeakObjectPtr<UEditableText> WeakEd(Ed);
		L->Callback = [WeakEd](const FText& NewText)
		{
			UEditableText* F = WeakEd.Get();
			const FString S = NewText.ToString();
			// [ПОРТ] С4 08.10.2026: стоп в БАЙТАХ ПРОВОДА (`strcpy` в char[20] пакета), по границе символа; cp1251 — как прежде.
			const FString Cut = FAsdaLoginData::TruncateToWireLimit(S.Left(20), 20, 20);
			if (F && Cut != S) { F->SetText(FText::FromString(Cut)); }
		};
		Ed->OnTextChanged.AddDynamic(L, &UAsdaTextLimiter::OnTextChanged);
		AsdaUi::AnchorRelay(GiftRelays, L, Ed);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		// Enter (событие 67587) = кнопка 1554; потеря фокуса/Esc — НЕ ответ (у Win32-поля эталона их нет).
		UAsdaTextLimiter* C = NewObject<UAsdaTextLimiter>(this);
		C->CommitCallback = [this](const FText&, ETextCommit::Type Method)
		{
			if (Method == ETextCommit::OnEnter) { AnswerGiftMsgBox(true); }
		};
		Ed->OnTextCommitted.AddDynamic(C, &UAsdaTextLimiter::OnTextCommitted);
		AsdaUi::AnchorRelay(GiftRelays, C, Ed);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	else { UE_LOG(LogAsdaWindow, Error, TEXT("[msgbox1552] GIFT_EDIT не найден — имя вводить некуда")); }
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgbox1552] показано (вид %d): %s"), E.Kind, *E.Text);
	CloseWorldMapForMsgBox();
	if (bSound) { PlayMsgBoxSfx(25, TEXT("показ GIFT_MSGBOX (CItem_BuildTooltipText / CMsgBox_SetContent)")); }
	return Box;
}

FString UAsdaWindowSubsystem::GetGiftMsgBoxEditText() const
{
	const UUserWidget* Box = FindWindow(kGiftId);
	const UEditableText* Ed = Box ? Cast<UEditableText>(Box->GetWidgetFromName(TEXT("GIFT_EDIT"))) : nullptr;
	return Ed ? Ed->GetText().ToString() : FString();
}

// Ответ окна 1552 = `OnSendItemUpgrade` case 1554/1555 (ui_core.c:57955-57989), см. объявление.
void UAsdaWindowSubsystem::AnswerGiftMsgBox(bool bOk)
{
	const int32 Idx = FindMsgBoxAnswerIndexFor(kGiftId);
	const FString Name = GetGiftMsgBoxEditText();   // `GetEditText(pGiftEdit)` — ДО закрытия окна
	// `CControl_SetFocus(1558, 0)` эталона: у порта фокус уходит вместе с виджетом в `CloseMsgBoxKind` (окно снимается).
	if (Idx == INDEX_NONE || !MsgBoxQueue[Idx].IsGift())
	{
		UE_LOG(LogAsdaWindow, Warning, TEXT("[msgbox1552] ответ %s без записи вида 59/60 — окно снято"), bOk ? TEXT("1554") : TEXT("1555"));
		PlayMsgBoxSfx(4, bOk ? TEXT("GIFT «ОК» 1554") : TEXT("GIFT «Отмена» 1555"));
		RemoveMsgBoxWidgetOfKind(59);
		PlayMsgBoxSfx(5, TEXT("закрытие 1552 без записи (CGameUI_CloseWindow)"));
		return;
	}
	FAsdaMsgBoxEntry E = MoveTemp(MsgBoxQueue[Idx]);
	MsgBoxQueue.RemoveAt(Idx);
	MsgBoxCurKind = E.Kind;
	MsgBoxCurId = E.Id;
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] -#%u ответ 1552 %s (вид %d) имя «%s» → в очереди %d"),
		E.Serial, bOk ? TEXT("«ОК»") : TEXT("«Отмена»"), E.Kind, *Name, MsgBoxQueue.Num());
	PlayMsgBoxSfx(4, bOk ? TEXT("GIFT «ОК» 1554") : TEXT("GIFT «Отмена» 1555"));
	if (E.OnTextResult) { E.OnTextResult(bOk, Name); }
	CloseMsgBoxKind(E.Kind, /*bHide*/true, /*bSilent*/false, /*bAfterAnswer*/true, /*bDeferDisplay*/false);
}

void UAsdaWindowSubsystem::DismissGiftMsgBox(bool bSilent)
{
	const int32 Idx = FindMsgBoxAnswerIndexFor(kGiftId);
	if (Idx == INDEX_NONE || !MsgBoxQueue[Idx].IsGift())
	{
		const bool bWasOpen = IsOpen(kGiftId);
		RemoveMsgBoxWidgetOfKind(59);
		if (bWasOpen && !bSilent) { PlayMsgBoxSfx(5, TEXT("программное закрытие 1552 без записи (CGameUI_CloseWindow)")); }
		return;
	}
	const FAsdaMsgBoxEntry E = MsgBoxQueue[Idx];
	MsgBoxQueue.RemoveAt(Idx);
	MsgBoxCurKind = E.Kind;
	MsgBoxCurId = E.Id;
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] -#%u программное закрытие 1552 (вид %d) → в очереди %d%s"),
		E.Serial, E.Kind, MsgBoxQueue.Num(), bSilent ? TEXT(", молча") : TEXT(""));
	CloseMsgBoxKind(E.Kind, /*bHide*/true, bSilent, /*bAfterAnswer*/true, /*bDeferDisplay*/false);
}

// Программное закрытие окна-сообщения чужой системой (`CloseWindow(ONEBUT/TWOBUT)`): снять запись этого окна
//   без действия, закрыть вид (5, возврат следующей). Так ведёт себя эталон для программных закрытий со своей
//   записью (id 38 — `ui_core.c:58895-58919`); тихий вариант — `CloseWindowSilently`.
void UAsdaWindowSubsystem::DismissMsgBoxWindow(bool bTwoBut, bool bSilent)
{
	const int32 Idx = FindMsgBoxAnswerIndex(bTwoBut);
	if (Idx == INDEX_NONE)
	{
		const bool bWasOpen = IsOpen(bTwoBut ? kTwoButId : kOneButId);
		RemoveMsgBoxWidget(bTwoBut);
		if (bWasOpen && !bSilent) { PlayMsgBoxSfx(5, TEXT("программное закрытие без записи (CGameUI_CloseWindow)")); }
		return;
	}
	const FAsdaMsgBoxEntry E = MsgBoxQueue[Idx];
	MsgBoxQueue.RemoveAt(Idx);
	MsgBoxCurKind = E.Kind;
	MsgBoxCurId = E.Id;
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] -#%u программное закрытие (вид %d, id %d) «%s» → в очереди %d%s"),
		E.Serial, E.Kind, E.Id, *E.Text, MsgBoxQueue.Num(), bSilent ? TEXT(", молча") : TEXT(""));
	if (bTwoBut) { ReleaseTwoButGauge(); }
	CloseMsgBoxKind(E.Kind, /*bHide*/true, bSilent, /*bAfterAnswer*/true, /*bDeferDisplay*/false);
}

// Порт `uiCloseOnExchangeItemType@0xD79D30` (ui_core.c:58576-58610; имя IDA врёт): зовут вход своего персонажа
//   на карту (`char_onLocalPlayerMapEntry`, entity_char.c:53192) и `GameNet_OnLocalPlayerMapEntry` (game_main.c:13020).
bool UAsdaWindowSubsystem::CloseDeathMsgBoxOnMapEntry(bool bRequireVisible)
{
	if (MsgBoxQueue.Num() == 0) { return false; }
	const bool bFixes = CVarMsgBoxQueueFixes.GetValueOnGameThread() != 0;
	// Эталонный гейт: хотя бы одно из окон 1500/1520 видно (ui_core.c:58585).
	//   ★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] (редакция 2, арбитр А-2): если ни одно окно не нарисовано (короткий промежуток
	//   после травела до `RedisplayMsgBoxAfterCloseAll`), эталон запись смерти НЕ снимает — модалка «воскреснуть в
	//   городе» остаётся у живого персонажа. Порт снимает её и тогда; `asda.MsgBox.QueueFixes 0` — эталон (выход).
	const bool bAnyVisible = IsWindowVisible(kOneButId) || IsWindowVisible(kTwoButId);
	if (bRequireVisible && !bAnyVisible && !bFixes) { return false; }
	// ★ А-1 (паритет с эталоном, оставлено): ищется ПЕРВАЯ запись с id 2370 ИЛИ 2371 ИЛИ 2376 — не обязательно своя
	//   (0, 2370). Порт id 2371/2376 не ставит (поиск по вызовам общей точки), поэтому на деле снимается 2370.
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < MsgBoxQueue.Num(); ++i)
	{
		const int32 Id = MsgBoxQueue[i].Id;
		if (Id == 2370 || Id == 2371 || Id == 2376) { Found = i; break; }
	}
	if (Found == INDEX_NONE) { return false; }
	if (bRequireVisible && !bAnyVisible)
	{
		UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] вход на карту: окна-сообщения не нарисованы, запись смерти снята всё равно (исправление А-2)"));
	}
	// Эталон снимает ПОСЛЕДНЮЮ запись (`sub_929801`), а не найденную; порт снимает найденную (исправление).
	const int32 Kill = bFixes ? Found : MsgBoxQueue.Num() - 1;
	const FAsdaMsgBoxEntry Gone = MsgBoxQueue[Kill];
	MsgBoxQueue.RemoveAt(Kill);
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] вход на карту: снята запись смерти #%u (вид %d, id %d), оба окна спрятаны молча; в очереди %d"),
		Gone.Serial, Gone.Kind, Gone.Id, MsgBoxQueue.Num());
	RemoveMsgBoxWidget(false);   // `CControl_SetVisible(v5[1], 0)` — ONEBUT
	RemoveMsgBoxWidget(true);    // `CControl_SetVisible(v5[6], 0)` — TWOBUT
	ReleaseTwoButGauge();
	// ★ Эталон оставшиеся записи НЕ возвращает: они висят невидимыми и глушат дедупом такие же показы до
	//   следующего закрытия (дефект). Порт возвращает верхнюю молча (звуков у эталона на этом пути нет).
	if (bFixes && MsgBoxQueue.Num())
	{
		const FAsdaMsgBoxEntry Copy = MsgBoxQueue[GetMsgBoxRedisplayIndex()];
		MsgBoxCurKind = Copy.Kind; MsgBoxCurId = Copy.Id;
		DisplayMsgBoxEntry(Copy, /*bSound*/false);
	}
	return true;
}

// ★ 22.10.2026 — разбор у объявления. Эталон (ui_core.c:58614-58648): гейт — ВИДЕН TWOBUT (`this+6`) и очередь не пуста;
//   поиск записи с id 31; снимается ПОСЛЕДНЯЯ запись (`sub_929801`) — порт при `asda.MsgBox.QueueFixes 1` снимает найденную
//   (то же исправление, что у `CloseDeathMsgBoxOnMapEntry`); прячется только TWOBUT; `sub_92E450(g_pWindowMgr)` — снять модальность.
bool UAsdaWindowSubsystem::CloseMsgBoxIdOnMapEntry(int32 Id, bool bTwoButOnly, const TCHAR* Why)
{
	if (MsgBoxQueue.Num() == 0) { return false; }
	const bool bVisible = bTwoButOnly ? IsWindowVisible(kTwoButId) : (IsWindowVisible(kOneButId) || IsWindowVisible(kTwoButId));
	if (!bVisible) { return false; }
	int32 Found = INDEX_NONE;
	for (int32 i = 0; i < MsgBoxQueue.Num(); ++i) { if (MsgBoxQueue[i].Id == Id) { Found = i; break; } }
	if (Found == INDEX_NONE) { return false; }
	const bool bFixes = CVarMsgBoxQueueFixes.GetValueOnGameThread() != 0;
	const int32 Kill = bFixes ? Found : MsgBoxQueue.Num() - 1;
	const FAsdaMsgBoxEntry Gone = MsgBoxQueue[Kill];
	MsgBoxQueue.RemoveAt(Kill);
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] %s: снята запись #%u (вид %d, id %d), TWOBUT спрятан молча; в очереди %d"),
		Why, Gone.Serial, Gone.Kind, Gone.Id, MsgBoxQueue.Num());
	RemoveMsgBoxWidget(true);   // `CControl_SetVisible(v5[6], 0)` — TWOBUT
	if (!bTwoButOnly) { RemoveMsgBoxWidget(false); }
	ReleaseTwoButGauge();
	if (bFixes && MsgBoxQueue.Num())
	{
		const FAsdaMsgBoxEntry Copy = MsgBoxQueue[GetMsgBoxRedisplayIndex()];
		MsgBoxCurKind = Copy.Kind; MsgBoxCurId = Copy.Id;
		DisplayMsgBoxEntry(Copy, /*bSound*/false);
	}
	RefreshModalInput();
	return true;
}

bool UAsdaWindowSubsystem::IsMsgBoxQueued(int32 Kind, int32 Id) const
{
	for (const FAsdaMsgBoxEntry& E : MsgBoxQueue)
	{
		if (E.Kind == Kind && E.Id == Id && E.Id != AsdaMsgBox::kNoKey) { return true; }
	}
	return false;
}

bool UAsdaWindowSubsystem::IsMsgBoxKindQueued(int32 Kind) const
{
	for (const FAsdaMsgBoxEntry& E : MsgBoxQueue)
	{
		if (E.Kind == Kind) { return true; }
	}
	return false;
}

int32 UAsdaWindowSubsystem::RemoveMsgBoxEntries(int32 Kind, int32 Id, const TCHAR* Why, bool bRedisplayNow)
{
	const int32 Before = MsgBoxQueue.Num();
	const uint32 TopSerial = Before ? MsgBoxQueue.Last().Serial : 0;
	MsgBoxQueue.RemoveAll([Kind, Id](const FAsdaMsgBoxEntry& E) { return E.Kind == Kind && E.Id == Id; });
	const int32 Removed = Before - MsgBoxQueue.Num();
	if (Removed == 0) { return 0; }
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] сняты молча записи (вид %d, id %d): %d — %s"), Kind, Id, Removed, Why);
	const bool bTopGone = !MsgBoxQueue.Num() || MsgBoxQueue.Last().Serial != TopSerial;
	if (bTopGone)
	{
		// Верхняя запись ушла: окно её вида снимаем, следующую (если есть) показываем без звука.
		RemoveMsgBoxWidgetOfKind(Kind);   // ★ волна 2-Г: прежде `== kTwoButId ? 1520 : 1500` — вид 59/60 снял бы чужое окно
		if (AsdaMsgBoxWindowOfKind(Kind) == kTwoButId) { ReleaseTwoButGauge(); }   // ★ 2-Г ревизия М-6: прежде `Kind != 0` (задел бы 59/60)
		if (MsgBoxQueue.Num() && bRedisplayNow)
		{
			const FAsdaMsgBoxEntry Copy = MsgBoxQueue[GetMsgBoxRedisplayIndex()];
			MsgBoxCurKind = Copy.Kind; MsgBoxCurId = Copy.Id;
			DisplayMsgBoxEntry(Copy, /*bSound*/false);
		}
		else if (MsgBoxQueue.Num() == 0) { MsgBoxCurKind = -1; MsgBoxCurId = -1; }
		// !bRedisplayNow (смена карты, ревизия М-3): окно не рисуем посреди травела — верхнюю запись вернёт
		//   `RedisplayMsgBoxAfterCloseAll` после `CloseAll` входа в мир.
	}
	return Removed;
}

void UAsdaWindowSubsystem::ClearMsgBoxQueue(const TCHAR* Why)
{
	const int32 N = MsgBoxQueue.Num();
	MsgBoxQueue.Reset();   // колбэки не зовём: ответа игрок не давал (закон no-reentrant-net-callbacks)
	MsgBoxCurKind = -1; MsgBoxCurId = -1;
	bMsgBoxRedisplayPending = false;
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr) { W->GetTimerManager().ClearTimer(GaugeOf(true).AnimTimer); }
	ReleaseTwoButGauge();
	RemoveMsgBoxWidget(false);
	RemoveMsgBoxWidget(true);
	RemoveMsgBoxWidgetOfKind(59);   // ★ волна 2-Г: окно ввода имени 1552
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] очередь снята целиком (%d записей), молча: %s"), N, Why);
}

// ★ 27.09.2026: порт `itemBuyPanelRefreshOrHide@0xD7A110` (обоснование и адреса — в объявлении).
void UAsdaWindowSubsystem::DropTopMsgBoxBeforeNotice(const TCHAR* Why)
{
	if (MsgBoxQueue.Num() > 0)
	{
		// Цикл эталона `for (i < size) if (elem(i)) { popBack; sub_12B2900; return; }` — `elem(i)` при i < size
		//   всегда ненулевой (`sub_D7E1B0`), значит при непустом стеке снимается ровно ПОСЛЕДНЯЯ запись.
		const FAsdaMsgBoxEntry Gone = MsgBoxQueue.Pop();   // колбэки не зовём: ответа игрок не давал
		RemoveMsgBoxWidgetOfKind(Gone.Kind);               // [ПОРТ] модальность снимается вместе с виджетом (см. .h); ★ 2-Г: и 1552
		if (Gone.IsTwoBut()) { ReleaseTwoButGauge(); }
		UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] -#%u снята молча перед нотисом (вид %d, id %d) «%s» → в очереди %d: %s"),
			Gone.Serial, Gone.Kind, Gone.Id, *Gone.Text, MsgBoxQueue.Num(), Why);
		return;
	}
	// Стек пуст: `CControl_SetVisible(…, 0)` шести панелей менеджера, без звуков.
	RemoveMsgBoxWidget(false);   // +4    — ONEBUT_MSGBOX 1500
	RemoveMsgBoxWidget(true);    // +0x18 — TWOBUT_MSGBOX 1520
	RemoveMsgBoxWidgetOfKind(59);   // +0x80 — GIFT_MSGBOX 1552 (★ волна 2-Г); +0x30 THREEBUT 1545 — мёртвое в оригинале
	ReleaseTwoButGauge();
	UGameInstance* GI = GetGameInstance();
	if (UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		if (Tip->IsSowelStonesWindowShown()) { Tip->HideSowelStonesWindow(); }   // +0x4C — SOWELL_SELBOX 1530
	}
	if (UAsdaPurchaseBoxSubsystem* Pb = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr)
	{
		if (Pb->IsOpen()) { Pb->Close(); }   // +0x9C — SHELLITEM_PURCHASE_BOX 1559 (колбэк не зовётся — как у эталона)
	}
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] очередь пуста — панели менеджера сообщений спрятаны перед нотисом: %s"), Why);
}

// Шаг загрузки мира `g_LoadProgress == 0` (game_main.c:3540-3549): `CGameUI_CloseWindow(5001)`, `(5002)`, `(0)`.
//   Виды 5001/5002 окна не прячут (таких ветвей в switch нет), вид 0 прячет ONEBUT. Окно на экране загрузки не
//   рисуем — его вернёт `CloseAll` входа в мир; звуки (25 возврата и 5) — здесь, в тех же местах, что эталон.
void UAsdaWindowSubsystem::MapLoadCloseMsgBoxes(bool bDeferDisplay)
{
	static const int32 kKinds[3] = { 5001, 5002, 0 };
	for (int32 K : kKinds)
	{
		CloseMsgBoxKind(K, /*bHide*/K == 0, /*bSilent*/false, /*bAfterAnswer*/false, bDeferDisplay);
	}
	if (bDeferDisplay) { bMsgBoxRedisplayPending = MsgBoxQueue.Num() > 0; }
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] загрузка мира: CGameUI_CloseWindow(5001/5002/0); в очереди %d%s"),
		MsgBoxQueue.Num(), bMsgBoxRedisplayPending ? TEXT(" — верхняя вернётся после входа в мир") : TEXT(""));
}

void UAsdaWindowSubsystem::RepeatMsgBoxClose(int32 Kind, const TCHAR* Why)
{
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] повторное закрытие вида %d: %s"), Kind, Why);
	CloseMsgBoxKind(Kind, /*bHide*/true, /*bSilent*/false, /*bAfterAnswer*/true, /*bDeferDisplay*/false);
}

// Возврат верхней записи после `CloseAll` (вход в мир снёс все окна; эталонные окна травел не сносит).
void UAsdaWindowSubsystem::RedisplayMsgBoxAfterCloseAll()
{
	if (MsgBoxQueue.Num() == 0) { bMsgBoxRedisplayPending = false; return; }
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	// Следующим тиком: `RevealWorld` после `CloseAll` ставит базовый режим ввода — модалка должна встать ПОСЛЕ него.
	W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (MsgBoxQueue.Num() == 0) { bMsgBoxRedisplayPending = false; return; }
		const FAsdaMsgBoxEntry Copy = MsgBoxQueue[GetMsgBoxRedisplayIndex()];
		if (IsOpen(AsdaMsgBoxWindowOfKind(Copy.Kind))) { bMsgBoxRedisplayPending = false; return; }
		MsgBoxCurKind = Copy.Kind; MsgBoxCurId = Copy.Id;
		UE_LOG(LogAsdaWindow, Log, TEXT("[msgqueue] возврат #%u после входа в мир (вид %d, id %d) «%s»%s"),
			Copy.Serial, Copy.Kind, Copy.Id, *Copy.Text, bMsgBoxRedisplayPending ? TEXT(", 25 сыгран при загрузке") : TEXT(""));
		bMsgBoxRedisplayPending = false;
		DisplayMsgBoxEntry(Copy, /*bSound*/false);
	}));
}

void UAsdaWindowSubsystem::DumpMsgBoxQueue() const
{
	UE_LOG(LogAsdaWindow, Warning, TEXT("[msgqueue] ДАМП: записей %d, текущие вид %d / id %d, исправления %s"),
		MsgBoxQueue.Num(), MsgBoxCurKind, MsgBoxCurId,
		CVarMsgBoxQueueFixes.GetValueOnGameThread() ? TEXT("вкл") : TEXT("выкл (эталон)"));
	for (int32 i = MsgBoxQueue.Num() - 1; i >= 0; --i)
	{
		const FAsdaMsgBoxEntry& E = MsgBoxQueue[i];
		UE_LOG(LogAsdaWindow, Warning, TEXT("[msgqueue]   [%d]%s #%u вид %d id %d «%s»"), i,
			i == MsgBoxQueue.Num() - 1 ? TEXT(" верх") : TEXT(""), E.Serial, E.Kind, E.Id, *E.Text);
	}
}

void UAsdaWindowSubsystem::PlayMsgBoxSfx(int32 SoundId, const TCHAR* Why)
{
	MsgBoxSfxCounts.FindOrAdd(SoundId)++;
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(SoundId);   // `csoundPlayName2DStore(g_pSoundMgr, id, 0)`
	}
	UE_LOG(LogAsdaWindow, Log, TEXT("[msgbox-звук] id %d — %s"), SoundId, Why);
}

void UAsdaWindowSubsystem::CloseWindowSilently(FName WindowId)
{
	TGuardValue<bool> Silent(bMsgBoxSilentClose, true);
	CloseWindow(WindowId);
}

// ★ 25.10.2026: объявления наперёд — определения ниже, у окна количества 1410 (общая точка фокуса поля модалки).
bool AsdaInputDialogFocusOn();   // ключ `asda.UI.InputDialogFocus`
namespace { void AsdaFocusModalField(UAsdaWindowSubsystem* WM, UEditableText* E, FName WinId, const TCHAR* FieldName, const TCHAR* Tag); }

// ══════════ ★★★ МОДАЛЬНЫЙ ДИАЛОГ ВВОДА ТЕКСТА — эталонное окно `SHOUTING_WIN` (1580) ═══════════
// РЕВЕРС: `Reports/chat_tab_editor_2590_re.md` §5.1 (`uiConfirmDialog_Setup @0xD7D7D0`,
//   ui_core.c:58834-58843). Своего окна ввода ни у чата, ни у инвентаря НЕТ — игра переиспользует
//   окно «крика»: заголовок 1581, подсказка 1582, поле 1583, ОК 1584, Отмена 1585.
//   Эталон различает вызывающего числом-режимом в `CItemTooltipUI+0x3490` (0 = предмет,
//   1 = переименовать вкладку, 2 = добавить вкладку) и диспетчеризует по нему в обработчике ОК.
//   ★ В ПОРТЕ роль режима играет КОЛБЭК — это то же самое ветвление, только без глобального
//   состояния; поэтому одно окно так же обслуживает любое число вызывающих.
const FName UAsdaWindowSubsystem::kInputWindowId(TEXT("SHOUTING_WIN"));

UUserWidget* UAsdaWindowSubsystem::ShowInputDialog(const FString& Title, const FString& Guide,
	const FString& InitialText, int32 MaxBytes, TFunction<void(bool, const FString&)> OnResult)
{
	// Открыт предыдущий — закрываем как «Отмена» (эталон держит ровно одно окно 1580).
	if (IsOpen(kInputWindowId)) { CloseInputDialog(false); }

	// z = 811 — на 1 выше диалога подтверждения (810): из подтверждения ввод не открывается, но
	//   порядок фиксируем явно. bModal → ввод к нижним окнам заблокирован.
	UUserWidget* Dlg = OpenWindowByPath(kInputWindowId,
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_SHOUTING_WIN"), 811, ESlateVisibility::Visible, /*bModal*/true);
	if (!Dlg) { if (OnResult) { OnResult(false, FString()); } return nullptr; }
	InputCallback = MoveTemp(OnResult);

	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	// 1581 Shouting_TitleTxt — заголовок (TextAlign 1 = слева), 1582 Shouting_GuideTxt — подсказка
	//   (TextAlign 5 = центр/центр, rect 390×30 ⇒ длинная строка переносится).
	if (UTextBlock* T = Cast<UTextBlock>(Dlg->GetWidgetFromName(TEXT("Shouting_TitleTxt"))))
	{
		T->SetText(FText::FromString(Title));
		T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (UTextBlock* T = Cast<UTextBlock>(Dlg->GetWidgetFromName(TEXT("Shouting_GuideTxt"))))
	{
		T->SetText(FText::FromString(Guide));
		T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		CenterTextBlockInSlot(T, /*WrapW*/390.f);
	}

	// 1583 Shouting_InputBox — поле ввода: предзаполнение + фокус (эталон `CControl_SetFocus(...,1)`).
	InputField = nullptr;
	InputLimiter = nullptr;
	if (UEditableText* E = Cast<UEditableText>(Dlg->GetWidgetFromName(TEXT("Shouting_InputBox"))))
	{
		E->SetText(FText::FromString(InitialText));
		E->SetVisibility(ESlateVisibility::Visible);
		InputField = E;
		// ★ ЖЁСТКИЙ СТОП НАБОРА В БАЙТАХ. Эталон меряет ИМЕНА `strlen`'ом (лимит имени вкладки —
		//   10 БАЙТ, а не символов), поэтому и здесь считаем байты кодировки клиента CP1251.
		if (MaxBytes > 0)
		{
			UAsdaTextLimiter* L = NewObject<UAsdaTextLimiter>(this);
			// ★ В CP1251 (кодировка клиента) КАЖДЫЙ представимый символ — РОВНО ОДИН байт, и
			//   кириллица тоже, поэтому число символов = число байт `strlen`, которым меряет эталон.
			//   (В корейском оригинале CP949 один слог = 2 байта — отсюда «10 байт = 5 символов».)
			L->Callback = [this, MaxBytes](const FText& NewText)
			{
				UEditableText* Fld = InputField.Get();
				if (!Fld) { return; }
				const FString S = NewText.ToString();
				// [ПОРТ] С4 08.10.2026: байты ПРОВОДА (арабский — маркер + cp1256, смешанный — UTF-8), по границе символа;
				//   представимое в cp1251 — ровно как прежде (символ = байт).
				const FString Cut = FAsdaLoginData::TruncateToWireLimit(S.Left(MaxBytes), MaxBytes, MaxBytes);
				if (Cut == S) { return; }
				Fld->SetText(FText::FromString(Cut));
			};
			E->OnTextChanged.AddDynamic(L, &UAsdaTextLimiter::OnTextChanged);
			InputLimiter = L;
		}
		// ★★★ ПОДТВЕРЖДЕНИЕ — ТОЛЬКО ПО ENTER, Esc = отмена. ОСТАЛЬНЫЕ способы игнорируем.
		//   ЖИВОЙ ТЕСТ 2026-08-06: «никаких окон не вылазит ни при изменении, ни при добавлении,
		//   только красная надпись про длину заголовка». ПРИЧИНА: обработчик не смотрел на
		//   `ETextCommit::Type` и принимал ЛЮБОЙ commit. Slate шлёт `OnUserMovedFocus` при потере
		//   фокуса, а фокус у поля отбирает `RefreshModalInput` (он ставит `FInputModeUIOnly` с
		//   фокусом на КОРЕНЬ модального окна) — он вызывается из `CloseWindow`, а меню 2670
		//   закрывается сразу после команды. Итог: окно 1580 открывалось и в тот же кадр
		//   «подтверждалось» пустой строкой ⇒ `AddChatTab("")` ⇒ отказ по длине. Ровно то, что
		//   видел юзер. Эталонное Win32-поле подтверждает ТОЛЬКО по Enter (WM_COMMAND от edit).
		UAsdaTextLimiter* C = NewObject<UAsdaTextLimiter>(this);
		// ★ 25.10.2026 (ревизия REVIEW_FOCUS Ф-1): commit признаётся, только пока это поле — поле ОТКРЫТОГО окна. После закрытия по Enter
		//   при модалке ниже (74200) `HandleCarriageReturn` снимал фокус (`ClearKeyboardFocusOnCommit`) → `OnCleared` → второе закрытие.
		TWeakObjectPtr<UEditableText> WeakField = E;
		C->CommitCallback = [this, WeakField](const FText&, ETextCommit::Type Method)
		{
			if (!WeakField.IsValid() || InputField.Get() != WeakField.Get()) { return; }   // окно уже закрыто — не наше
			if (Method == ETextCommit::OnEnter)        { CloseInputDialog(true); }
			// `OnCleared` — НЕ ESC (ESC поля Slate ничего не коммитит; отмену по ESC делает препроцессор Enter-подсистемы), а снятие фокуса
			//   с причиной Cleared: после Enter `ClearKeyboardFocusOnCommit` снимает фокус и с поля НОВОГО окна, если колбэк его открыл
			//   (экземпляр окна переиспользуется). При ключе фокуса — не отмена; при 0 — прежнее.
			else if (Method == ETextCommit::OnCleared && !AsdaInputDialogFocusOn()) { CloseInputDialog(false); }
			// OnUserMovedFocus / Default — НЕ подтверждение и НЕ отмена: окно остаётся открытым.
		};
		E->OnTextCommitted.Clear();   // прежние привязки того же экземпляра окна (переиспользуется) не тянем
		E->OnTextCommitted.AddDynamic(C, &UAsdaTextLimiter::OnTextCommitted);
		InputCommitRelay = C;
		// ★ 25.10.2026 [ОРИГИНАЛ] (`asda.UI.InputDialogFocus`): фокус полю 1583 — общая точка (как у окна количества 1410).
		//   Прежний `SetKeyboardFocus` перетирала отложенная операция `RefreshModalInput` (фокус корню → вьюпорту).
		if (AsdaInputDialogFocusOn()) { AsdaFocusModalField(this, E, kInputWindowId, TEXT("Shouting_InputBox"), TEXT("ввод 1583")); }
		else                          { E->SetKeyboardFocus(); }
	}
	else
	{
		UE_LOG(LogAsdaWindow, Error, TEXT("[ввод] Shouting_InputBox (1583) не найден — вводить некуда"));
	}

	// Подписи кнопок ТОЛЬКО из таблицы текстов: ОК — loc 2288, Отмена — loc **2378**
	//   (у этого окна отмена берёт 2378, а не 2289 — ui_core.c:55879-55888).
	auto SetBtnLabel = [&](const TCHAR* N, int32 LocId)
	{
		UTextBlock* T = Cast<UTextBlock>(Dlg->GetWidgetFromName(FName(*(FString(N) + TEXT("_Text")))));
		if (!T) { return; }
		const FString S = (Txt && LocId > 0) ? Txt->GetTextById(LocId) : FString();
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
		T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		CenterTextBlockInSlot(T, 0.f);
	};
	SetBtnLabel(TEXT("Shouting_OKBtn"), 2288);
	SetBtnLabel(TEXT("Shouting_CacelBtn"), 2378);

	InputRelays.Reset();
	auto BindBtn = [&](const TCHAR* N, bool bAccept)
	{
		UButton* B = Cast<UButton>(Dlg->GetWidgetFromName(FName(N)));
		if (!B) { UE_LOG(LogAsdaWindow, Error, TEXT("[ввод] кнопка %s не найдена"), N); return; }
		B->OnClicked.Clear();
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this, bAccept](int32) { CloseInputDialog(bAccept); };
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(InputRelays, R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	};
	BindBtn(TEXT("Shouting_OKBtn"), true);       // 1584
	BindBtn(TEXT("Shouting_CacelBtn"), false);   // 1585

	UE_LOG(LogAsdaWindow, Log,
		TEXT("[ввод] окно 1580 открыто: заголовок «%s», подсказка «%s», поле «%s», лимит %d байт CP1251"),
		*Title, *Guide, *InitialText, MaxBytes);
	return Dlg;
}

void UAsdaWindowSubsystem::CloseInputDialog(bool bAccepted)
{
	// Текст читается ИЗ ПОЛЯ в момент подтверждения — ровно как в эталоне (`GetEditText(menuWnd+88)`
	//   в обработчике 1584); отдельного хранилища у введённой строки нет.
	FString Text;
	if (UEditableText* E = InputField.Get())
	{
		Text = E->GetText().ToString();
		E->OnTextCommitted.Clear();   // ★ Ф-1: снятие фокуса после закрытия (`OnCleared`) окно больше не трогает
	}
	InputField = nullptr;
	InputLimiter = nullptr;
	InputCommitRelay = nullptr;
	CloseWindow(kInputWindowId);
	InputRelays.Reset();

	TFunction<void(bool, const FString&)> Cb = MoveTemp(InputCallback);
	InputCallback = nullptr;
	UE_LOG(LogAsdaWindow, Log, TEXT("[ввод] окно 1580 закрыто: %s, текст «%s»"),
		bAccepted ? TEXT("ОК") : TEXT("Отмена"), *Text);
	if (Cb) { Cb(bAccepted, Text); }
}

// ═══════════ ★★★ ОКНО ВВОДА КОЛИЧЕСТВА — эталонный NUMPAD_WIN 1410 (`CNumPadDialog`) ═══════════
//   Реверс контролов: `Docs/UI/Common/NUMPAD_WIN.md` (все CONFIRMED).
//   Открытие/режимы: `uiOpenContextPopupWithHandlers@0xDA55A0` (ui_core.c:61746).
//   Кнопки: `uiNumPadDialogProc@0xDA6040`; Enter/ESC/клампинг: `uiQuantityNumPadDialogProc@0xDA6300`;
//   обрезка: `uiClampQuantityToMax@0xDA6410`; закрытие: `uiDialogClose@0xDA5A30`.
const FName UAsdaWindowSubsystem::kNumPadWindowId(TEXT("NUMPAD_WIN"));

// ★★ [ИСПРАВЛЕНИЕ] 08.10.2026 (пакет 3, СРЕДН-1): ключ клампа окна 1410 «у курсора» в экран — разбор в `ShowNumPad`.
static ASDA_DEV_CVAR(int32, CVarNumPadClampToScreen,
	TEXT("asda.UI.NumPadClampToScreen"), 1,
	TEXT("1 — окно ввода количества 1410 у курсора прижимается к краю экрана ([ИСПРАВЛЕНИЕ] дефекта оригинала, 08.10.2026); ")
	TEXT("0 — как оригинал: левый-верх ровно в точке курсора, окно может уйти за экран. Режим 6 (центр) ключ не касается."),
	ECVF_Default);

// ★★ [ОРИГИНАЛ] 25.10.2026: фокус клавиатуры поля 1412 при открытии окна количества (находка аудита Enter, AUDIT.md §5 п. 2).
//   Эталон `uiOpenContextPopupWithHandlers@0xDA55A0` (ui_core.c:61849-61850): `CControl_SetFocus(поле 1412, 1)`, затем пустой текст;
//   `CControl_SetText_12F9850` у поля с фокусом ставит курсор в конец без выделения (`EM_SETSEL 500,500`, ui_core.c:116557-116564).
//   Порт ставил фокус полю ДО того, как отложенная операция Slate от `RefreshModalInput` (`FInputModeUIOnly` с фокусом на
//   нефокусируемый корень модалки) отработала в конце кадра: корень фокус не берёт, Slate отдаёт его ближайшему фокусируемому
//   предку — игровому вьюпорту, а тот под модалкой ввод игнорирует. Цифры не набирались, пока игрок не щёлкнет по полю.
//   0 — как было до правки (фокус уходил вьюпорту).
static ASDA_DEV_CVAR(int32, CVarNumPadFocus,
	TEXT("asda.UI.NumPadFocus"), 1,
	TEXT("1 — [ОРИГИНАЛ] окно количества 1410 при открытии отдаёт фокус клавиатуры полю 1412 (цифры набираются сразу, Enter — ОК, ")
	TEXT("ESC — отмена), курсор после программной смены текста — в конце, не-цифры с клавиатуры в поле не попадают; ")
	TEXT("0 — как до 25.10.2026: фокус уходил игровому вьюпорту, набор — только после щелчка по полю."),
	ECVF_Default);

// Ключ фокуса поля 1412 (аксессор для `RefreshModalInput` и ESC-прохода `UAsdaEnterConfirmSubsystem` — без имени ключа в их коде).
bool AsdaNumPadFocusOn() { return CVarNumPadFocus.GetValueOnGameThread() != 0; }

// ★★ [ОРИГИНАЛ] 25.10.2026: фокус клавиатуры поля 1583 окна ввода текста 1580 при открытии — та же причина, что у 1410.
//   Эталон `uiConfirmDialog_Setup@0xD7D7D0` / `confirmDlg_ShowWithDefaultText@0xD7D660` (ui_core.c:58811-58812, 58838-58839):
//   `CControl_SetFocus(поле 1583, 1)`, затем текст поля (у поля с фокусом — курсор в конец без выделения, EM_SETSEL 500,500).
//   0 — как до правки (фокус уходил вьюпорту, набор — только после щелчка по полю).
static ASDA_DEV_CVAR(int32, CVarInputDialogFocus,
	TEXT("asda.UI.InputDialogFocus"), 1,
	TEXT("1 — [ОРИГИНАЛ] окно ввода текста 1580 при открытии отдаёт фокус клавиатуры полю 1583 (курсор в конце предзаполненного ")
	TEXT("текста), пересчёт модалок фокус поля не отбирает; 0 — как до 25.10.2026: фокус уходил игровому вьюпорту."),
	ECVF_Default);

// ★★ [ИСПРАВЛЕНИЕ] 25.10.2026 (поручение координатора): ESC в окне 1580 = «Отмена» 1585.
//   ОРИГИНАЛ так НЕ делает: у 1580 нет признака «ESC закрывает» (`CControl+0x228`, 61 вызов `helper_setByteField552` —
//   `kEscCloseWindows`), обработчик 15xx `uiItemUpgradeWnd_OnCommand@0xD79260` событие 21 разбирает только для 1520/1530,
//   а событие ESC самого поля 67588 (`ctrlEditImeWndProc`, ui_core.c:116246) не разбирает никто — у оригинала ESC окно не закрывает.
//   0 — как оригинал (ESC в окне 1580 ничего не делает).
static ASDA_DEV_CVAR(int32, CVarInputDialogEsc,
	TEXT("asda.UI.InputDialogEsc"), 1,
	TEXT("1 — [ИСПРАВЛЕНИЕ] ESC при открытом окне ввода текста 1580 закрывает его как «Отмена» (из поля и вне поля); ")
	TEXT("0 — как оригинал: признака «ESC закрывает» у 1580 нет, ESC окно не трогает."),
	ECVF_Default);

// ★ 25.10.2026 [ПОРТ] (поведение не меняется, только журнал): корень модалки (`SObjectWidget` окна) фокус клавиатуры не принимает — `FInputModeUIOnly`
//   с ним писал «Attempting to focus Non-Focusable widget», а Slate всё равно отдавал фокус ближайшему фокусируемому предку —
//   игровому `SViewport`. Здесь фокус режима ввода сразу ставится вьюпорту: тот же итог, без ошибки. Разметка не меняется.
//   0 — прежнее (фокус корню, ошибка в журнале).
static ASDA_DEV_CVAR(int32, CVarModalFocusNoRoot,
	TEXT("asda.UI.ModalFocusNoRoot"), 1,
	TEXT("1 — режим ввода модалки/экрана, чей корень фокус не принимает, фокусирует игровой вьюпорт (куда Slate и так уводил фокус) — ")
	TEXT("без ошибки «Attempting to focus Non-Focusable widget»; 0 — прежнее: фокус корню, ошибка в журнале."),
	ECVF_Default);

// Аксессоры ключей окна 1580 (для `RefreshModalInput` и ESC-прохода Enter-подсистемы).
bool AsdaInputDialogFocusOn() { return CVarInputDialogFocus.GetValueOnGameThread() != 0; }
bool AsdaInputDialogEscOn()   { return CVarInputDialogEsc.GetValueOnGameThread() != 0; }

// Цель фокуса режима `FInputModeUIOnly` для корня окна: сам корень, если он фокус принимает, иначе игровой вьюпорт
//   (пользователи — `RefreshModalInput` в AsdaWindowSubsystem_Close.cpp, `AAsdaLoginGameMode::FocusUI`).
TSharedPtr<SWidget> AsdaUiOnlyFocusTarget(UUserWidget* Root)
{
	if (!Root) { return nullptr; }
	const TSharedRef<SWidget> W = Root->TakeWidget();
	if (CVarModalFocusNoRoot.GetValueOnGameThread() == 0 || W->SupportsKeyboardFocus()) { return W; }
	if (GEngine && GEngine->GameViewport)
	{
		if (TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget()) { return Vp; }
	}
	return W;
}

namespace
{
	// Курсор поля ввода — в конец текста без выделения (эталон: `EM_SETSEL 500,500` после смены текста поля с фокусом).
	void AsdaFieldCaretToEnd(UEditableText* F)
	{
		if (!F || !F->HasKeyboardFocus()) { return; }
		const TSharedPtr<SWidget> W = F->GetCachedWidget();
		if (!W.IsValid() || W->GetType() != FName(TEXT("SEditableText"))) { return; }
		StaticCastSharedPtr<SEditableText>(W)->GoTo(ETextLocation::EndOfDocument);
	}
	// То же для поля 1412 — под ключом окна количества.
	void AsdaNumPadCaretToEnd(UEditableText* F) { if (AsdaNumPadFocusOn()) { AsdaFieldCaretToEnd(F); } }

	// ★ 25.10.2026 [ОРИГИНАЛ] Общая точка «фокус полю модального окна при открытии» (1410 / 1580).
	//   `SetUserFocus(PC)` при успехе ОТМЕНЯЕТ отложенный запрос фокуса локального игрока, который `OpenWindowByPath → RefreshModalInput`
	//   только что поставил (UWidget::SetUserFocus, Widget.cpp:743-751); при неудаче сам ставит отложенный запрос на поле.
	//   Страховка следующего кадра (окно открыто из обработчика мыши): только если окно WinId открыто и верхняя модалка,
	//   поле — по-прежнему его узел FieldName и фокус не у другого поля ввода.
	void AsdaFocusModalField(UAsdaWindowSubsystem* WM, UEditableText* E, FName WinId, const TCHAR* FieldName, const TCHAR* Tag)
	{
		if (!WM || !E) { return; }
		UGameInstance* GI = WM->GetGameInstance();
		APlayerController* PC = GI ? GI->GetFirstLocalPlayerController(GI->GetWorld()) : nullptr;
		if (PC) { E->SetUserFocus(PC); } else { E->SetKeyboardFocus(); }
		AsdaFieldCaretToEnd(E);
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (!W) { return; }
		TWeakObjectPtr<UEditableText> WeakE = E;
		const FName FieldId(FieldName);
		const FString TagS(Tag);
		W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(WM, [WM, WeakE, WinId, FieldId, TagS]()
		{
			UEditableText* F = WeakE.Get();
			UUserWidget* Root = WM->FindWindow(WinId);
			if (!F || !Root || !WM->IsOpen(WinId) || WM->GetTopModalWindow() != Root || Root->GetWidgetFromName(FieldId) != F) { return; }
			bool bOtherField = false;   // фокус уже у другого поля ввода (игрок сам щёлкнул) — не трогаем
			if (FSlateApplication::IsInitialized())
			{
				const TSharedPtr<SWidget> Cur = FSlateApplication::Get().GetKeyboardFocusedWidget();
				bOtherField = Cur.IsValid() && Cur != F->GetCachedWidget() && Cur->GetTypeAsString().Contains(TEXT("EditableText"));
			}
			if (!F->HasKeyboardFocus() && !bOtherField)
			{
				UGameInstance* G = WM->GetGameInstance();
				APlayerController* P = G ? G->GetFirstLocalPlayerController(G->GetWorld()) : nullptr;
				if (P) { F->SetUserFocus(P); } else { F->SetKeyboardFocus(); }
				AsdaFieldCaretToEnd(F);
				UE_LOG(LogAsdaWindow, Log, TEXT("[%s] фокус клавиатуры возвращён полю следующим кадром"), *TagS);
			}
			UE_LOG(LogAsdaWindow, Log, TEXT("[%s] фокус клавиатуры после открытия: поле — %s"), *TagS, F->HasKeyboardFocus() ? TEXT("да") : TEXT("НЕТ"));
		}));
	}

	// ★ Имена цифровых кнопок СМЕЩЕНЫ относительно самих цифр: idnum 1400 (`NUMPAD_NO09`) = «0»,
	//   1401 (`NUMPAD_NO00`) = «1», … 1409 (`NUMPAD_NO08`) = «9». Цифра = idnum − 1400
	//   (ui_core.c:62062-62074, `a2 % 0x578`). Таблица — единственный источник этого соответствия.
	struct FAsdaNumPadDigit { const TCHAR* Widget; int32 Digit; };
	const FAsdaNumPadDigit kAsdaNumPadDigits[] = {
		{ TEXT("NUMPAD_NO09"), 0 }, { TEXT("NUMPAD_NO00"), 1 }, { TEXT("NUMPAD_NO01"), 2 },
		{ TEXT("NUMPAD_NO02"), 3 }, { TEXT("NUMPAD_NO03"), 4 }, { TEXT("NUMPAD_NO04"), 5 },
		{ TEXT("NUMPAD_NO05"), 6 }, { TEXT("NUMPAD_NO06"), 7 }, { TEXT("NUMPAD_NO07"), 8 },
		{ TEXT("NUMPAD_NO08"), 9 },
	};
	// Эталонный предел длины поля (ui_core.c:62066: дописываем, только если длина < 10).
	constexpr int32 kAsdaNumPadMaxChars = 10;
	// Нотис подписи окна (ui_core.c:61774) и звуки открытия/закрытия (ui_core.c:61845 / 61872).
	constexpr int32 kAsdaNumPadNoticeId = 12124;
	// Нотис среза поля по максимуму (`uiClampQuantityToMax@0xDA6410`, g:400590: loc 6835 в чат цветом −11514 на 10 с) —
	//   пишется только в строгом режиме `ShowNumPad(…, bStrictMax)` (08.10.2026, ревизия В3 рыбалки, ВАЖН-1).
	constexpr int32 kAsdaNumPadClampNoticeId = 6835;
	constexpr int32 kAsdaNumPadOpenSound = 5;
	constexpr int32 kAsdaNumPadCloseSound = 6;
	// z окна: на 1 выше диалога ввода 1580 (811) — количество может спрашиваться поверх любого окна.
	constexpr int32 kAsdaNumPadZ = 812;
}

void UAsdaWindowSubsystem::ClampNumPadField()
{
	// Пере-вход: SetText ниже снова поднимет OnTextChanged.
	if (bNumPadClamping) { return; }
	UEditableText* F = NumPadField.Get();
	if (!F) { return; }

	const FString Src = F->GetText().ToString();
	// Поле эталона — числовое: всё, что не цифра, в нём появиться не может (кнопки дописывают
	//   только цифры). С физической клавиатуры в UE ввести букву можно, поэтому фильтруем.
	FString Out;
	Out.Reserve(Src.Len());
	for (const TCHAR C : Src)
	{
		// Строгий режим: ведущий «−» сохраняется, только если его вписала кнопка MAX (эталон пишет `sprintf("%d", max)`
		//   и при max < 0). ★ W3fix2 МЕЛК-1: «−» с клавиатуры отбрасывается — в поле эталона его не набрать.
		// ★ 25.10.2026 (REVIEW_FOCUS Ф-3): цифра — только ASCII 0–9 (`FChar::IsDigit` = `iswdigit` пропускал ٠–٩ и ²³¹, `Atoi64` давал 0).
		if ((C >= TEXT('0') && C <= TEXT('9')) || (bNumPadStrictMax && bNumPadMinusFromMax && C == TEXT('-') && Out.IsEmpty())) { Out.AppendChar(C); }
	}
	// «−» от MAX живёт, пока он ведущий: стёрли (DELETE/клавиатура) — дальше снова только цифры.
	if (!Out.StartsWith(TEXT("-"))) { bNumPadMinusFromMax = false; }
	if (Out.Len() > kAsdaNumPadMaxChars) { Out = Out.Left(kAsdaNumPadMaxChars); }
	if (!Out.IsEmpty())
	{
		// Значение может не влезть в int32 (10 знаков) — считаем в 64 битах, потом обрезаем.
		const int64 Value = FCString::Atoi64(*Out);
		if (bNumPadStrictMax)
		{
			// ★ Строгий режим = эталон `uiClampQuantityToMax` дословно: `if (atoi > max)` БЕЗ условия `max > 0` —
			//   при максимуме 0 любое ненулевое число режется в «0»; каждый срез — нотис 6835 в чат (−11514, 10 с).
			if (Value > (int64)NumPadMaxQuantity)
			{
				Out = FString::FromInt(NumPadMaxQuantity);
				const FString Note = AsdaNotice::LocRaw(GetGameInstance(), kAsdaNumPadClampNoticeId, /*bDebugFallback*/true);
				AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, Note, UAsdaChatSubsystem::ColGold(), 10000.f);
				UE_LOG(LogAsdaWindow, Log, TEXT("[количество] строгий максимум: %lld > %d → поле «%s», нотис %d"),
					Value, NumPadMaxQuantity, *Out, kAsdaNumPadClampNoticeId);
			}
		}
		else if (NumPadMaxQuantity > 0 && Value > (int64)NumPadMaxQuantity)
		{
			Out = FString::FromInt(NumPadMaxQuantity);
		}
	}
	if (Out != Src)
	{
		bNumPadClamping = true;
		F->SetText(FText::FromString(Out));
		bNumPadClamping = false;
		AsdaNumPadCaretToEnd(F);   // ★ 25.10.2026 [ОРИГИНАЛ]: `CControl_SetText_12F9850` у поля с фокусом — курсор в конец
	}
}

void UAsdaWindowSubsystem::AppendNumPadDigit(int32 Digit)
{
	UEditableText* F = NumPadField.Get();
	if (!F) { return; }
	FString S = F->GetText().ToString();
	// Эталон дописывает ТОЛЬКО при длине < 10; на пределе нажатие просто игнорируется.
	if (S.Len() >= kAsdaNumPadMaxChars) { return; }
	S.AppendChar((TCHAR)('0' + FMath::Clamp(Digit, 0, 9)));
	bNumPadClamping = true;
	F->SetText(FText::FromString(S));
	bNumPadClamping = false;
	AsdaNumPadCaretToEnd(F);   // ★ 25.10.2026 [ОРИГИНАЛ]: курсор в конец (см. `AsdaNumPadCaretToEnd`)
	ClampNumPadField();   // эталон клампит сразу после дописывания
}

bool UAsdaWindowSubsystem::IsNumPadOpen() const
{
	return const_cast<UAsdaWindowSubsystem*>(this)->IsOpen(kNumPadWindowId);
}

UUserWidget* UAsdaWindowSubsystem::ShowNumPad(int32 MaxQuantity, TFunction<void(bool, int32)> OnResult,
	bool bCenterOnScreen, bool bStrictMax)
{
	// Эталон держит РОВНО ОДИН экземпляр окна (`g_pNumPadDialog`): повторное открытие поверх
	//   уже открытого закрывает прежнее как «Отмена», иначе повис бы чужой колбэк.
	if (IsOpen(kNumPadWindowId)) { CloseNumPad(false); }

	UUserWidget* Dlg = OpenWindowByPath(kNumPadWindowId,
		TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_NUMPAD_WIN"), kAsdaNumPadZ,
		ESlateVisibility::Visible, /*bModal*/true);
	if (!Dlg)
	{
		UE_LOG(LogAsdaWindow, Error, TEXT("[количество] WBP_NUMPAD_WIN не загрузился — операция отменена"));
		if (OnResult) { OnResult(false, 0); }
		return nullptr;
	}

	NumPadCallback = MoveTemp(OnResult);
	// Строгий режим хранит максимум как есть (может быть ≤ 0 — эталон `pDialog[8] = a2` без клампа); обычный — прежнее Max(0, …).
	bNumPadStrictMax = bStrictMax;
	bNumPadMinusFromMax = false;
	NumPadMaxQuantity = bStrictMax ? MaxQuantity : FMath::Max(0, MaxQuantity);
	bNumPadClosing = false;
	bNumPadClamping = false;

	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	// 1411 NUMPAD_MESSAGE — подпись-приглашение, нотис 12124 (ui_core.c:61774-61775).
	if (UTextBlock* T = Cast<UTextBlock>(Dlg->GetWidgetFromName(TEXT("NUMPAD_MESSAGE"))))
	{
		const FString S = Txt ? Txt->GetTextById(kAsdaNumPadNoticeId) : FString();
		T->SetText(FText::FromString(S.IsEmpty() ? FAsdaLang::MissingTag(kAsdaNumPadNoticeId) : S));   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// 1412 NUMPAD_NUMBOX — поле ввода. Эталон при открытии даёт ему фокус (`CControl_SetFocus(...,1)`,
	//   ui_core.c:61849) и оставляет пустым: значение набирается кнопками либо клавиатурой.
	NumPadField = nullptr;
	NumPadClampRelay = nullptr;
	NumPadCommitRelay = nullptr;
	if (UEditableText* E = Cast<UEditableText>(Dlg->GetWidgetFromName(TEXT("NUMPAD_NUMBOX"))))
	{
		E->SetText(FText::GetEmpty());
		E->SetVisibility(ESlateVisibility::Visible);
		NumPadField = E;

		// Клампинг на КАЖДОЕ изменение — эталонный путь 0x10807 (`uiClampQuantityToMax`,
		//   ui_core.c:62117): что бы ни ввели, значение не выйдет за максимум и за 10 знаков.
		UAsdaTextLimiter* L = NewObject<UAsdaTextLimiter>(this);
		L->Callback = [this](const FText&) { ClampNumPadField(); };
		E->OnTextChanged.AddDynamic(L, &UAsdaTextLimiter::OnTextChanged);
		NumPadClampRelay = L;

		// Enter = ОК, Esc = отмена (эталон `uiQuantityNumPadDialogProc`: 0x10803 → pfnOnOk,
		//   0x15 → pfnOnCancel). Потеря фокуса — НЕ подтверждение: ровно та же грабля, что
		//   разобрана у ShowInputDialog (RefreshModalInput отбирает фокус и «подтверждал» пустую строку).
		UAsdaTextLimiter* C = NewObject<UAsdaTextLimiter>(this);
		// ★ 25.10.2026 (ревизия REVIEW_FOCUS Ф-1): то же, что у 1580, — commit только от поля открытого окна (иначе второе закрытие
		//   `OnCleared` после Enter при модалке ниже, и новое окно, открытое колбэком первого, закрылось бы «Отменой»).
		TWeakObjectPtr<UEditableText> WeakField = E;
		C->CommitCallback = [this, WeakField](const FText&, ETextCommit::Type Method)
		{
			if (!WeakField.IsValid() || NumPadField.Get() != WeakField.Get()) { return; }   // окно уже закрыто — не наше
			if (Method == ETextCommit::OnEnter)        { CloseNumPad(true); }
			// `OnCleared` — снятие фокуса (причина Cleared), не ESC: при ключе фокуса не отмена (см. то же у 1580); при 0 — прежнее.
			else if (Method == ETextCommit::OnCleared && !AsdaNumPadFocusOn()) { CloseNumPad(false); }
		};
		E->OnTextCommitted.Clear();   // прежние привязки того же экземпляра окна (переиспользуется) не тянем
		E->OnTextCommitted.AddDynamic(C, &UAsdaTextLimiter::OnTextCommitted);
		NumPadCommitRelay = C;
		if (!AsdaNumPadFocusOn())
		{
			// Ключ сняли после прошлого открытия того же экземпляра окна — фильтр символов не оставляем.
			if (const TSharedPtr<SWidget> SW = E->GetCachedWidget(); SW.IsValid() && SW->GetType() == FName(TEXT("SEditableText"))) { StaticCastSharedPtr<SEditableText>(SW)->SetOnKeyCharHandler(FOnKeyChar()); }
			E->SetKeyboardFocus();   // прежнее (ключ 0): фокус тут же отбирала отложенная операция `RefreshModalInput`
		}
		else
		{
			// ★ 25.10.2026 [ОРИГИНАЛ] Поле эталона — числовое: символ не-цифры с клавиатуры в него не попадает и курсор не двигает.
			//   Прежний фильтр `ClampNumPadField` (вставка из буфера и прочее) остаётся, но символ с клавиатуры гасится до вставки,
			//   иначе курсор после чистки уезжал бы в конец. «−» с клавиатуры — тоже нет (W3fix2 МЕЛК-1: его вписывает только MAX).
			//   Управляющие символы (Backspace и т. п.) проходят — их разбирает само поле.
			if (const TSharedPtr<SWidget> SW = E->TakeWidget(); SW.IsValid() && SW->GetType() == FName(TEXT("SEditableText")))
			{
				StaticCastSharedPtr<SEditableText>(SW)->SetOnKeyCharHandler(FOnKeyChar::CreateLambda(
					[](const FGeometry&, const FCharacterEvent& Ev) -> FReply
					{
						const TCHAR Ch = Ev.GetCharacter();
						// ★ Ф-3: только ASCII 0–9 (`FChar::IsDigit` = `iswdigit` пропускал бы арабско-индийские ٠–٩ и ²³¹ — `Atoi64` дал бы 0).
						return (Ch >= 32 && !(Ch >= TEXT('0') && Ch <= TEXT('9'))) ? FReply::Handled() : FReply::Unhandled();
					}));
			}
			// Фокус полю — общая точка `AsdaFocusModalField` (отмена отложенного фокуса корня + страховка следующего кадра).
			AsdaFocusModalField(this, E, kNumPadWindowId, TEXT("NUMPAD_NUMBOX"), TEXT("количество 1412"));
		}
	}
	else
	{
		UE_LOG(LogAsdaWindow, Error, TEXT("[количество] NUMPAD_NUMBOX (1412) не найден — вводить некуда"));
	}

	// ── Кнопки: 10 цифр, MAX (1413), DELETE (1414), OK (1415), CLOSE (1416) ──
	NumPadRelays.Reset();
	auto Bind = [&](const TCHAR* Name, TFunction<void()> Action)
	{
		UButton* B = Cast<UButton>(Dlg->GetWidgetFromName(FName(Name)));
		if (!B) { UE_LOG(LogAsdaWindow, Error, TEXT("[количество] кнопка %s не найдена"), Name); return; }
		B->OnClicked.Clear();
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [Act = MoveTemp(Action)](int32) { Act(); };
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(NumPadRelays, R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	};

	for (const FAsdaNumPadDigit& D : kAsdaNumPadDigits)
	{
		const int32 Digit = D.Digit;
		Bind(D.Widget, [this, Digit]() { AppendNumPadDigit(Digit); });
	}
	// 1413 MAX: подставляет максимум целиком (`sprintf("%d", maxQuantity)`, ui_core.c:62033-62039).
	Bind(TEXT("NUMPAD_MAX"), [this]()
	{
		if (UEditableText* F = NumPadField.Get())
		{
			// ★ W3fix2 МЕЛК-1: отрицательный максимум строгого режима — «−» законно пришёл от MAX.
			bNumPadMinusFromMax = bNumPadStrictMax && NumPadMaxQuantity < 0;
			bNumPadClamping = true;
			F->SetText(FText::FromString(FString::FromInt(NumPadMaxQuantity)));
			bNumPadClamping = false;
			AsdaNumPadCaretToEnd(F);   // ★ 25.10.2026 [ОРИГИНАЛ]: курсор в конец
			ClampNumPadField();
		}
	});
	// 1414 DELETE: снимает ОДИН символ с конца (`strncpy` до len−1, ui_core.c:62040-62051).
	//   Это backspace, а не полная очистка — имя кнопки обманчиво.
	Bind(TEXT("NUMPAD_DELETE"), [this]()
	{
		if (UEditableText* F = NumPadField.Get())
		{
			const FString S = F->GetText().ToString();
			if (S.IsEmpty()) { return; }
			bNumPadClamping = true;
			F->SetText(FText::FromString(S.LeftChop(1)));
			bNumPadClamping = false;
			AsdaNumPadCaretToEnd(F);   // ★ 25.10.2026 [ОРИГИНАЛ]: курсор в конец
		}
	});
	Bind(TEXT("NUMPAD_OK"),     [this]() { CloseNumPad(true);  });   // 1415
	Bind(TEXT("NUMPAD_CLOSED"), [this]() { CloseNumPad(false); });   // 1416

	// ── ПОЗИЦИЯ ОКНА ────────────────────────────────────────────────────────────────────────────
	//   ★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (фиксируем явно, закон never-invent-only-original).
	//   Эталон ставит окно У КУРСОРА и позицию не кламппит (ui_core.c:61843), а по центру экрана
	//   центрирует только режим 6. В порте так делать НЕЛЬЗЯ: любое МОДАЛЬНОЕ окно проходит через
	//   `CenterModalContent` → `CenterWindowOnScreen`, который двигает point-анкор контролы
	//   в ДИЗАЙН-БАЗЕ 1024×768. Первая версия этого метода двигала окно ещё раз, уже в единицах
	//   вьюпорта (`CursorInViewportUnits`), — системы координат разъезжались, и окно уходило
	//   за экран: живой лог 2026-08-26 «окно 1410 открыто у (250,737)», а на экране его не было.
	//   Поэтому позиционирование отдано общему правилу окон порта: модалка центрируется сама.
	//   Параметр `bCenterOnScreen` сохранён в сигнатуре — он остаётся признаком эталонного режима 6
	//   для вызывающих и пригодится, когда появится позиционирование у курсора в дизайн-базе.
	// ★★★ [ОРИГИНАЛ] 08.10.2026 (пакет 2, решение владельца «модальные — как в оригинале»): отступление СНЯТО.
	//   Позиционирование теперь общей точкой `PlaceWindowKeepingSavedOffset` — она считает в юнитах канваса ТЕКУЩЕГО экрана
	//   (анкор детей — левый-верх экрана), так что прежней беды с двумя системами координат больше нет.
	//   Эталон (ui_core.c:61835-61843): Origin := курсор в клиентских координатах (`GetCursorPos` + `ScreenToClient`);
	//   режим 6 — ((W>>1) − (w>>1), (H>>1) − (h>>1)) по своему размеру. Якорь .JUI «5 0 0» ничего не добавляет. Клампа в экран
	//   у оригинала нет — его нет и здесь. Откат: `asda.UI.ModalOriginalPos 0` — центр (модалка центрируется в `OpenWindow`).
	// ★★ [ИСПРАВЛЕНИЕ] 08.10.2026 (пакет 3, СРЕДН-1 ревизии пакета 2): ДЕФЕКТ ОРИГИНАЛА — окно 114×130 у курсора без клампа
	//   выходит за экран (нижний ряд инвентаря при 768 — до 8 px вниз, правый край — до 114 px вправо), что противоречит правилу
	//   «окна целиком на экране». Левый-верх прижимается: X ∈ [0; W−w], Y ∈ [0; H−h] (w×h — рамка окна). Режим 6 не трогается.
	//   Откат: `asda.UI.NumPadClampToScreen 0` — как оригинал, без клампа.
	//   ★ 08.10.2026 (пакет 3, МЕЛК-1): постановка запоминается в записи окна и повторяется при смене размера вьюпорта
	//   (точка курсора — та, что была при открытии; кламп — под НОВЫЙ экран).
	const bool bOrigPos = UseOriginalModalPos();
	FVector2D CursorPos(0.0, 0.0);   // курсор в юнитах канваса (DPI = 1 ⇒ клиентские пиксели), целые как у эталона
	if (bOrigPos)
	{
		UWorld* World = GI ? GI->GetWorld() : nullptr;
		const FVector2D M = World ? UWidgetLayoutLibrary::GetMousePositionOnViewport(World) : FVector2D::ZeroVector;
		CursorPos = FVector2D(FMath::FloorToDouble(M.X), FMath::FloorToDouble(M.Y));
		// Постановщик окна 1410: режим 6 — центр по своему размеру; у курсора — точка курсора, прижатая к краю экрана.
		TFunction<void()> Place = [this, bCenterOnScreen, CursorPos]()
		{
			if (!UseOriginalModalPos())   // ключ сняли после открытия — прежний центр порта
			{
				if (FAsdaWindowEntry* E = Windows.Find(kNumPadWindowId)) { CenterModalContent(E->Widget); }
				return;
			}
			FVector2D Placed(0.0, 0.0);   // итоговый левый-верх рамки (для лога)
			PlaceWindowKeepingSavedOffset(kNumPadWindowId, kNumPadWindowId, nullptr,
				[bCenterOnScreen, CursorPos, &Placed](const FVector2D& V, const FVector2D& F)
				{
					const int32 W = FMath::RoundToInt32(V.X), H = FMath::RoundToInt32(V.Y);
					const int32 Fw = FMath::RoundToInt32(F.X), Fh = FMath::RoundToInt32(F.Y);
					if (bCenterOnScreen)
					{
						Placed = FVector2D((double)((W >> 1) - (Fw >> 1)), (double)((H >> 1) - (Fh >> 1)));
						return Placed;
					}
					Placed = CursorPos;
					if (CVarNumPadClampToScreen.GetValueOnGameThread() != 0)
					{
						// Max(0, Min(…)): окно больше экрана — левый-верх в 0 (видна шапка с кнопкой закрытия).
						Placed.X = (double)FMath::Max(0, FMath::Min((int32)CursorPos.X, W - Fw));
						Placed.Y = (double)FMath::Max(0, FMath::Min((int32)CursorPos.Y, H - Fh));
					}
					return Placed;
				});
			if (!bCenterOnScreen && !Placed.Equals(CursorPos))
			{
				UE_LOG(LogAsdaWindow, Log, TEXT("[количество] окно 1410: курсор (%.0f,%.0f) → прижато к краю экрана (%.0f,%.0f) [ИСПРАВЛЕНИЕ]"),
					CursorPos.X, CursorPos.Y, Placed.X, Placed.Y);
			}
		};
		Place();
		SetWindowRePlacer(kNumPadWindowId, MoveTemp(Place));
	}
	UE_LOG(LogAsdaWindow, Log, TEXT("[количество] окно 1410 открыто (%s), максимум %d%s"),
		!bOrigPos ? TEXT("по центру экрана — откат asda.UI.ModalOriginalPos 0")
			: (bCenterOnScreen ? TEXT("по центру экрана — эталонный режим 6") : *FString::Printf(TEXT("у курсора (%.0f,%.0f), как оригинал"), CursorPos.X, CursorPos.Y)),
		NumPadMaxQuantity, bCenterOnScreen ? TEXT(" — эталонный режим 6") : TEXT(""));

	// Звук открытия 5 (ui_core.c:61845).
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(kAsdaNumPadOpenSound);
	}
	return Dlg;
}

// Закрыть нумпад без колбэка режима (объявление — в .h). Колбэк снимается ДО закрытия, остальное — общий `CloseNumPad`.
void UAsdaWindowSubsystem::DismissNumPad(const TCHAR* Who)
{
	if (!IsNumPadOpen()) { return; }
	NumPadCallback = nullptr;
	UE_LOG(LogAsdaWindow, Log, TEXT("[количество] окно 1410 закрыто без колбэка режима (%s)"), Who ? Who : TEXT("?"));
	CloseNumPad(false);
}

void UAsdaWindowSubsystem::CloseNumPad(bool bAccepted)
{
	// Повторный вход: OK-кнопка и commit-реле поля могут сработать в одном кадре.
	if (bNumPadClosing) { return; }
	bNumPadClosing = true;

	// Значение читается ИЗ ПОЛЯ в момент подтверждения — как в эталоне
	//   (`atoi(GetEditText(...))` внутри pfnOnOk), отдельного хранилища у него нет.
	int32 Qty = 0;
	if (UEditableText* E = NumPadField.Get())
	{
		const int64 V = FCString::Atoi64(*E->GetText().ToString());
		Qty = (int32)FMath::Clamp<int64>(V, 0, (int64)MAX_int32);
	}

	// ★ Эталонные OK-хендлеры проверяют диапазон 1..maxQuantity и при выходе за него операцию
	//   НЕ запускают (окно при этом закрывается). Считаем такой исход отменой — вызывающему
	//   не придётся повторять проверку у себя.
	const int32 Max = NumPadMaxQuantity;
	// Строгий режим: максимум ≤ 0 — ничего не проходит (эталон: поле уже срезано в «max», а ОК режима 10 требует `> 0`).
	bool bOk = bAccepted && Qty >= 1 && (bNumPadStrictMax ? (Qty <= Max) : (Max <= 0 || Qty <= Max));
	if (bAccepted && !bOk)
	{
		UE_LOG(LogAsdaWindow, Warning,
			TEXT("[количество] введено %d при максимуме %d — вне диапазона 1..%d, операция отменена"),
			Qty, Max, Max);
	}

	if (UEditableText* E = NumPadField.Get()) { E->OnTextCommitted.Clear(); }   // ★ Ф-1: снятие фокуса после закрытия окно не трогает
	NumPadField = nullptr;
	NumPadClampRelay = nullptr;
	NumPadCommitRelay = nullptr;
	CloseWindow(kNumPadWindowId);
	NumPadRelays.Reset();
	NumPadMaxQuantity = 0;
	bNumPadStrictMax = false;
	bNumPadMinusFromMax = false;

	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(kAsdaNumPadCloseSound);   // звук закрытия 6 (ui_core.c:61872)
	}

	TFunction<void(bool, int32)> Cb = MoveTemp(NumPadCallback);
	NumPadCallback = nullptr;
	bNumPadClosing = false;
	UE_LOG(LogAsdaWindow, Log, TEXT("[количество] окно 1410 закрыто: %s, количество %d"),
		bOk ? TEXT("ОК") : TEXT("Отмена"), bOk ? Qty : 0);
	if (Cb) { Cb(bOk, bOk ? Qty : 0); }
}

// ── ★ Текст по центру rect (эталон TextAlign/CaptionAlign 5; формулы font_caption_layout.md §3) ──
void UAsdaWindowSubsystem::CenterTextBlockInSlot(UTextBlock* Tb, float WrapW, FVector2D ExtraOffset)
{
	if (!Tb) { return; }
	Tb->SetJustification(ETextJustify::Center);   // hCENTER: каждая строка x = центр − textW/2
	UCanvasPanelSlot* S = AsdaUi::SlotOf(Tb);
	if (!S) { return; }
	if (S->GetAutoSize()) { return; }             // уже центрован (повторный вызов) — геометрию не трогаем
	// Импортёр кладёт Text-контрол сдвинутым на (h−14)/2 вниз (одна строка 14 px в vCenter) →
	//   истинный центр авторского rect = (slotL + slotW/2, slotT + 7) (см. объявление).
	const FMargin Off = S->GetOffsets();
	const FVector2D C(Off.Left + Off.Right * 0.5f + ExtraOffset.X, Off.Top + 7.f + ExtraOffset.Y);
	S->SetAutoSize(true);                         // размер = контент; alignment 0.5/0.5 центрует блок строк
	S->SetAlignment(FVector2D(0.5f, 0.5f));
	S->SetOffsets(FMargin(C.X, C.Y, 0.f, 0.f));
	// vCENTER: y = центр − lineH·N/2 — ровно эталонная формула textFont_DrawRichText (font_text.c:8454).
	if (WrapW > 0.f) { Tb->SetAutoWrapText(false); Tb->SetWrapTextAt(WrapW); }
	else             { Tb->SetAutoWrapText(false); Tb->SetWrapTextAt(0.f); }   // однострочный — без переноса
}

// ── ★ Тело окна-сообщения с тегами `<FC=…>` (09.10.2026, СРЕДН-1 ревизии autoshovel_fishbooks_REVIEW.md) ──
//   Эталон кладёт текст через `CControl_SetText` (item_inventory.c:51348-51352), и рендер JUI теги разбирает.
//   Порт клал сырую строку в `UTextBlock` — игрок видел `<FC=FF99FF99>[…]</F>` (111018 и ещё 59 строк GameText).
//   Первый показ: стиль контрола из JUI (кегль/цвет/тень) снимается ДО подмены, геометрия центруется
//   `CenterTextBlockInSlot` (autosize-слот + alignment 0.5 в центре авторского rect). Затем единая точка тегов
//   `UAsdaRichText::ApplyFormatted` подменяет узел на `URichTextBlock` с тем же именем и слотом. Она всегда
//   выключает AutoSize слота — возвращаем его и ставим перенос по авторской ширине (WrapTextAt, без AutoWrap —
//   закон ui-autowrap-canvas-gotcha), чтобы раскладка осталась прежней: центр/центр, строки по центру.
UWidget* UAsdaWindowSubsystem::SetMsgBoxBodyFormatted(UUserWidget* Box, const TCHAR* NodeName, const FString& Raw,
	float WrapW, bool bForceWhite)
{
	UWidget* W = Box ? Box->GetWidgetFromName(FName(NodeName)) : nullptr;
	if (!W) { return nullptr; }
	const FName Key(NodeName);   // ключ снятого стиля: имя узла (подмена имя сохраняет)
	if (UTextBlock* T = Cast<UTextBlock>(W))
	{
		// Узел ещё из JUI (первый показ этого экземпляра окна) — снять стиль и отцентровать геометрию.
		if (bForceWhite) { T->SetColorAndOpacity(FSlateColor(FLinearColor::White)); }   // TextColor FFFFFFFF (.JUI)
		T->SetAutoWrapText(false);
		CenterTextBlockInSlot(T, WrapW);
		MsgBodyStyles.Add(Key, FTextBlockStyle()
			.SetFont(T->GetFont())
			.SetColorAndOpacity(T->GetColorAndOpacity())
			.SetShadowOffset(T->GetShadowOffset())
			.SetShadowColorAndOpacity(T->GetShadowColorAndOpacity()));
	}
	FAsdaRichApplyOpts Opts;
	Opts.BaseStyle   = MsgBodyStyles.Find(Key);   // nullptr ⇒ строка `Default` (стиль ещё не снимался)
	Opts.bSetJustify = true;
	Opts.Justify     = ETextJustify::Center;      // TextAlign 5: hCENTER
	Opts.bWrap       = WrapW > 0.f;
	Opts.WrapAt      = WrapW;
	UWidget* New = UAsdaRichText::ApplyFormatted(W, Raw, &Opts);
	if (URichTextBlock* R = Cast<URichTextBlock>(New))
	{
		R->SetAutoWrapText(false);                   // перенос только по авторской ширине
		R->SetWrapTextAt(WrapW > 0.f ? WrapW : 0.f);
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(R))
		{
			S->SetAutoSize(true);                    // размер = контент, центр блока строк — в центре rect (vCENTER)
			S->SetAlignment(FVector2D(0.5f, 0.5f));
		}
		R->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	return New;
}

// ── ★ Видимый текст тела окна-сообщения (09.10.2026): то, что нарисовано, без разметки ──
//   `UTextBlock` — текст как есть; `URichTextBlock` — снять сегменты `<a …>`/`</>` разметки UE и
//   раскрыть экранирование `ToRichMarkup` (&lt; &gt; &quot; &amp;). Сырые теги `<FC=…>` при этом остаются
//   видимыми — ровно то, что проверяет самотест.
FString UAsdaWindowSubsystem::ReadMsgBoxShownText(const UWidget* W)
{
	if (const UTextBlock* T = Cast<UTextBlock>(W)) { return T->GetText().ToString(); }
	const URichTextBlock* R = Cast<URichTextBlock>(W);
	if (!R) { return FString(); }
	const FString M = R->GetText().ToString();
	FString Out;
	for (int32 i = 0; i < M.Len(); )
	{
		if (M[i] == TEXT('<') && (M.Mid(i, 3) == TEXT("<a ") || M.Mid(i, 3) == TEXT("<a>") || M.Mid(i, 3) == TEXT("</>")))
		{
			const int32 Close = M.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i);
			i = (Close == INDEX_NONE) ? M.Len() : Close + 1;
			continue;
		}
		Out.AppendChar(M[i]); ++i;
	}
	Out.ReplaceInline(TEXT("&lt;"), TEXT("<"));
	Out.ReplaceInline(TEXT("&gt;"), TEXT(">"));
	Out.ReplaceInline(TEXT("&quot;"), TEXT("\""));
	Out.ReplaceInline(TEXT("&amp;"), TEXT("&"));
	return Out;
}
