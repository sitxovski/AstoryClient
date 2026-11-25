// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна разборки снаряжения `Disassembly_Win` (JUI 32000). Весь разбор эталона, поля объекта и поток —
//   в шапке `AsdaDisassemblySubsystem.h` и в отчёте `Reports/owner_1006/wave2A_disassembly.md`.
//   Структурный дамп ассета (каждый контрол) — `Reports/owner_1006/wave2A/_disassembly_wbp_tree.txt`.

#include "UI/AsdaDisassemblySubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaNotice.h"            // фасад нотисов (Loc / MsgBox / ChatColored)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaOutgoingGate.h"         // офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"   // DoubleClickTime — интервал двойного клика (0xD), как у сумки
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDisasm, Log, All);
// ★ U21 (В1 дедупа №2): имя константы — по контейнеру; значение обязано совпадать с размером контейнера инвентаря.
static_assert(UAsdaDisassemblySubsystem::kShellCells == UAsdaInventorySubsystem::kShellSlots, "размер контейнера разошёлся с инвентарём");
// ★ [ПОРТ] 29.10.2026 (инвентарь ×2, ревизия 30.10): `RefreshList` зовут по колесу — рамка выбора 32090 переставляется
//   за выбранной вещью даже из скрытого состояния (вещь вернулась в окно прокрутки). Взводится только на время вызова.
static bool GDisasmWheelRefresh = false;

// ═════════════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕНИЙ ═════════════════════════════════════════
//   Каждое видимое игроку отступление от оригинала — с ключом; значение по умолчанию = «исправлено».

// ★ Д-1. Порог «переноса улучшения» в превью: клиент `upgradeUiOnSlotClick` подменяет 6 превью на 363/401 при
//   проценте заточки +27 ≥ **77**, а сервер (`Recv_User_Disassemble`, GameServer.c:98327) переносит улучшение при
//   ≥ **88**. Между 77 и 87 % игрок видит «перенос», а получает материал по таблице; результат не находится среди
//   превью (слот −1) — полоса обрывается сразу, рамки результата нет. [ПОРТ] порог = серверный (88). Тот же порог
//   уже принят окном заточки (`AsdaItemUpgradeSubsystem.cpp`, Д-11 `kTransferThreshold`).
static ASDA_DEV_CVAR(int32, CVarTransferThreshold,
	TEXT("asda.Disassembly.TransferThreshold"), 88,
	TEXT("Дефект оригинала Д-1: процент заточки, с которого превью окна 32000 показывает «перенос улучшения» (363/401). "
	     "88 = как сервер (исправлено); 77 = как клиент оригинала."),
	ECVF_Default);

// ★ Д-2. «Начать» (32002) не меняет состояния окна до ответа: второе нажатие до 6503 шлёт второй 6502 по тому же
//   предмету. Сервер флаг «запрос к БД в полёте» для 6502 не проверяет (до ответа БД предмет ещё в памяти
//   сервера), поэтому второй запрос уходит в БД. [ПОРТ] замок «6502 в полёте» (снимают ответ, сброс сессии, таймаут).
static ASDA_DEV_CVAR(int32, CVarSingleInFlight,
	TEXT("asda.Disassembly.SingleInFlight"), 1,
	TEXT("Дефект оригинала Д-2: 1 = пока 6502 без ответа 6503, новый не отправлять; 0 = как оригинал."),
	ECVF_Default);

// ★ Д-2: закон «не защёлкивать состояние заранее» — молчание сервера не даёт вечный тупик.
static ASDA_DEV_CVAR(float, CVarReplyTimeoutSec,
	TEXT("asda.Disassembly.ReplyTimeoutSec"), 20.f,
	TEXT("Секунд ожидания 6503, после которых замок «6502 в полёте» снимается. 0 = без таймаута."),
	ECVF_Default);

// ★ Д-3. Закрытие окна во время полосы (`upgradeUiWnd_close` ставит [89] = −1) применяет результат, но петлю
//   `S_e_disjoint_process.wav` не глушит — глушит её только конец полосы, а полоса при [89] ≠ 1 больше не идёт:
//   звук «жара» играет бесконечно, до следующей разборки. [ПОРТ] закрытие глушит петлю.
static ASDA_DEV_CVAR(int32, CVarStopLoopOnClose,
	TEXT("asda.Disassembly.StopLoopOnClose"), 1,
	TEXT("Дефект оригинала Д-3: 1 = закрытие окна 32000 глушит петлю S_e_disjoint_process; 0 = как оригинал (играет вечно)."),
	ECVF_Default);

// ★ Д-4. Рамка выбора 32090 гаснет только в конце полосы: клик по ПУСТОЙ позиции списка и двойной клик по 32004
//   (сброс) снимают выбор ([179] = 0), а рамка остаётся на прежней позиции — игрок видит «выбран», «Начать» молчит.
//   [ПОРТ] рамка видна, только пока что-то выбрано.
static ASDA_DEV_CVAR(int32, CVarHideFrameWithoutSelection,
	TEXT("asda.Disassembly.HideFrameWithoutSelection"), 1,
	TEXT("Дефект оригинала Д-4: 1 = рамка 32090 гаснет вместе с выбором (пустая позиция, сброс 32004); 0 = как оригинал."),
	ECVF_Default);

// ★ Д-5. Полоса «жара» растёт на 1 px ЗА КАДР (`CDisjoint_TickIfActive` в игровом цикле, game_main.c:1483) —
//   скорость зависит от FPS (на 30 кадрах слот 6 = 5 с, на 120 — 1.25 с). Масштаб времени самого клиента —
//   30 кадров/с (`g_FrameTimeScale = 30 / FPS`, game_main.c:1488-1494). [ПОРТ] кадры эталона считаются от
//   времени с этой частотой. 0 = как оригинал (кадр движка = кадр эталона).
static ASDA_DEV_CVAR(float, CVarAnimFps,
	TEXT("asda.Disassembly.AnimFps"), 30.f,
	TEXT("Дефект оригинала Д-5: частота «кадров эталона» полосы 32030 (1 px за кадр). 30 = как масштаб клиента; "
	     "0 = как оригинал — каждый кадр движка."),
	ECVF_Default);

// ★ Д-6 (ревизия 2-А, С-1). Тот же дефект, что Д-3, другим путём: «Начать» → окно закрыли ДО ответа (при [89] = 0
//   закрытие разрешено) → 6503 код 1 запускает полосу при СКРЫТОМ окне → игрок открывает окно раньше её конца.
//   `upgradeUiOnOpen` ставит [89] = 0 без применения и без стопа петли: полоса больше не идёт, петля «жара» играет
//   вечно, результат в инвентарь не попадает (разобранный предмет остаётся «призраком», следующий 6503 затирает тело).
//   [ПОРТ] открытие при идущей полосе сначала применяет результат и глушит петлю.
static ASDA_DEV_CVAR(int32, CVarFinishOnReopen,
	TEXT("asda.Disassembly.FinishOnReopen"), 1,
	TEXT("Дефект оригинала Д-6: 1 = открытие окна 32000 при идущей (скрытой) полосе применяет результат и глушит петлю "
	     "S_e_disjoint_process; 0 = как оригинал (результат не применён, петля играет вечно)."),
	ECVF_Default);

namespace
{
	// Окно и его виджет. Ассет уже импортирован заходом JUI (не перегенерировать!).
	const FName  kWindowId(TEXT("Disassembly_Win"));
	const TCHAR* kWindowWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Disassembly_Win.WBP_Disassembly_Win_C");

	// Цвет строк 6503 в системном чате: −11514 = 0xFFFFD306 (золотой, `CChat_AddTokenizedLines(…, −11514, 10000, 0)`).
	const FColor kColResultChat(0xFF, 0xD3, 0x06, 0xFF);

	// IDNum контролов (из `.JUI` и `CDecomposeWindow_InitControls`).
	constexpr int32 kIdClose  = 32001;   // X
	constexpr int32 kIdStart  = 32002;   // «Приступить к демонтажу»
	constexpr int32 kIdTarget = 32004;   // выбранный предмет
	constexpr int32 kIdEff    = 32005;   // эффект «идёт»
	constexpr int32 kIdPrev0  = 32010;   // первый слот превью
	constexpr int32 kIdResult = 32016;   // результат
	constexpr int32 kIdNote0  = 32020;   // рамка слота результата
	constexpr int32 kIdGauge  = 32030;   // «жар»
	constexpr int32 kIdBest   = 32031;   // блёстки лучшего результата
	constexpr int32 kIdList0  = 32050;   // первая позиция списка
	constexpr int32 kIdFrame  = 32090;   // рамка выбора списка

	// Положение слота канваса (Left/Top) или ноль. ★ R5-b 12.11.2026: тело — общая точка `AsdaUi::SlotPos` (дословно то же).
	using AsdaUi::SlotPos;

	// Размер слота канваса (Right/Bottom при точечных якорях) или ноль. ★ R5-b 12.11.2026: тело — общая точка `AsdaUi::SlotSize`.
	using AsdaUi::SlotSize;

	// Текст подписи в контрол (nullptr — молча).
	void SetLabel(UTextBlock* Tb, const FString& S)
	{
		if (Tb) { Tb->SetText(FText::FromString(S)); }
	}

	// «+N%» эталона (`sprintf(buf, "+%d%%", v)`) при v > 0, иначе пусто.
	FString PercentText(int32 V)
	{
		return V > 0 ? FString::Printf(TEXT("+%d%%"), V) : FString();
	}
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaDisassemblySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HResult = Net->OnDisassembleResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ ESC — порт события 0x15 (`upgradeWnd_onMessage`: `if ([89] != 1) upgradeUiWnd_close`). Признак «закрываюсь по
		//   ESC» окну ставит `CDecomposeWindow_InitControls` (`helper_setByteField552(32000, 1)`), окно стоит в таблице
		//   `kEscCloseWindows`. Закрыватель идемпотентен.
		WM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaDisassemblySubsystem::OnEscPressed));
		// ★★★ [ПОРТ] 29.10.2026 (инвентарь ×2): КОЛЕСО НАД СПИСКОМ 32050…32079 — прокрутка по рядам (`BagScroll`, шапка
		//   `AsdaBagListScroll.h`). Список эталона — 30 позиций без прокрутки; при 60 ячейках сумки хвост был бы недостижим.
		WM->RegisterWheelHandler(kWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this, [this](int32 Notches) -> bool
		{
			if (!bOpen || !bControlsReady) { return false; }
			bool bOver = false;
			for (const TObjectPtr<UImage>& I : ListIcons) { if (I && AsdaUi::IsCursorOverWidget(I)) { bOver = true; break; } }
			if (!bOver) { return false; }
			if (BagScroll.Wheel(Notches))
			{
				TickTooltip(-1);   // плашка прежней позиции — к другой вещи
				GDisasmWheelRefresh = true;    // рамка выбора едет за выбранной вещью (или прячется вне окна)
				RefreshList();
				GDisasmWheelRefresh = false;
			}
			return true;
		}));
		// ★ «Закрыть все окна» — эталонный `uiCloseAllWindows@0xD61960` (ui_core.c:54362-54363):
		//   `if (sub_933CA7(g_pRecvUiHandler3) == 1) upgradeUiWnd_close(…)` — БЕЗ проверки «идёт».
		WM->RegisterWindowCloser(kWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			if (bOpen) { CloseWindow(); }
		}));
	}
	for (TArray<uint8>& P : Preview) { P.SetNumZeroed(FAsdaItemInstance::kRecordSize); }   // ★ U17: длина записи — одна константа
	ResultRec.SetNumZeroed(FAsdaItemInstance::kRecordSize);
	UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] подсистема окна 32000 поднята"));
}

void UAsdaDisassemblySubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnDisassembleResult.Remove(HResult);
	}
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterEscCloser(kWindowId);
		WM->UnregisterWindowCloser(kWindowId);
	}
	StopLoop(TEXT("Deinitialize"));
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaDisassemblySubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Эталон при перезагрузке интерфейсов (`LoadAllInterfaces2` → `CDecomposeWindow_InitControls`, ui_core.c:54069):
	//   [89] = −1, окно скрыто ([222] = 0). Замок «6502 в полёте» снимается — ответ старого сокета уже не придёт.
	StopLoop(AsdaSessionResetReasonName(Reason));
	ReleaseInFlight(AsdaSessionResetReasonName(Reason));
	if (bOpen)
	{
		bOpen = false;
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kWindowId, false);
		}
	}
	TickTooltip(-1);
	State = -1;
	SelCell = -1;
	bResultShown = false;
	ClearPreviews();
	List.Reset();
	ListIconCodes.Reset();
	BagScroll.Reset();   // ★ 29.10.2026 (инвентарь ×2)
	Pkt.Reset();
	SelSlot = -1;
	AnimTick = 0.f;
	AnimAccum = 0.0;
	bInvWasOpen = false;
	LastApplyTime = -1.0;
	PendingResults.Clear();
	TargetDbl.Reset();
	bWasOverWindow = false;
	Mouse.Reset();
}

void UAsdaDisassemblySubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Disassembly");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	// ★ Д-2: таймаут замка «6502 в полёте» — работает и при закрытом окне. ★ S2 (В2-A дедупа №2, 11.10.2026): `FAsdaReplyLatch::IsExpired` (та же формула).
	if (ReplyLatch.IsExpired(CVarReplyTimeoutSec.GetValueOnGameThread()))
	{
		ReleaseInFlight(TEXT("таймаут ответа 6503"));
		JournalAdd(TEXT("timeout 6503 — замок снят"));
	}
	// `CDisjoint_TickIfActive@0xE0E390`: при [89] == 1 — кадр полосы. Идёт и при скрытом окне (эталон не смотрит на видимость).
	if (State == 1)
	{
		const float Fps = CVarAnimFps.GetValueOnGameThread();
		if (Fps <= 0.f)
		{
			TickAnim();
		}
		else
		{
			const double Step = 1.0 / (double)Fps;
			AnimAccum += (double)DeltaTime;
			int32 Guard = 0;
			while (State == 1 && AnimAccum >= Step && Guard++ < 16)   // 16 — защита от лавины после долгого кадра
			{
				AnimAccum -= Step;
				TickAnim();
			}
			if (Guard >= 16) { AnimAccum = 0.0; }
		}
	}
	TickLoop();
	if (!bControlsReady || !bOpen) { bWasOverWindow = false; return; }
	Mouse.Update();
	// ★ Модальный гейт: под модальным нотисом (ONEBUT 100800/100300) эталон не отдаёт мышь окнам ниже.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		TickTooltip(-1);
		Mouse.Reset();
		return;
	}
	TickMouse();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaDisassemblySubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWindowZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZones();
		},
		[]() { UE_LOG(LogAsdaDisasm, Error, TEXT("[разборка] WBP_Disassembly_Win не загрузился (%s)"), kWindowWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaDisassemblySubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	bControlsReady = false;
	bOpen = false;
	TxtTitle = TxtBtnStart = ResLabel = TargetLabel = nullptr;
	DragHandle = BestGroup = Failed06 = TargetEff = SelFrame = nullptr;
	BtnStart = BtnClose = nullptr;
	ResIcon = TargetIcon = Gauge = nullptr;
	PrevIcons.Reset(); PrevLabels.Reset(); PrevNotice.Reset(); PrevFailed.Reset();
	ListIcons.Reset(); ListLabels.Reset(); ListDisable.Reset();
	BestShines.Reset();
	ListGrid = PrevGrid = TargetGrid = ResGrid = nullptr;
	Relays.Reset();
	Mouse.Reset();
	TipId = -1;
	bWasOverWindow = false;
}

void UAsdaDisassemblySubsystem::RegisterZones()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — восемь планов `Disassembly_Win_0_0…7` (скин рамки без центра, дамп ассета).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("Disassembly_Win_0_%d"), 8);
}

void UAsdaDisassemblySubsystem::LayoutLabel(UTextBlock* Tb)
{
	// `CClass_SetField926(контрол, 0x800000)` — обводка 1 px (стиль кода эталона, не разметка); выравнивание 9
	//   (`CControl_SetField924(…, 9)`) в `.JUI` у этих контролов уже 9 — импортёр его разложил, повторять не нужно.
	if (Tb) { AsdaUi::SetTextOutline(Tb, 1); }
}

void UAsdaDisassemblySubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	PrevIcons.SetNum(kPreviews); PrevLabels.SetNum(kPreviews); PrevNotice.SetNum(kPreviews); PrevFailed.SetNum(kPreviews);
	ListIcons.SetNum(kListCells); ListLabels.SetNum(kListCells); ListDisable.SetNum(kListCells);
	BestShines.Reset();
	TArray<FName> ListNames; ListNames.SetNum(kListCells);
	TArray<FName> PrevNames; PrevNames.SetNum(kPreviews);
	FName TargetName, ResName;

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("Disassembly_Btn"))    { BtnStart = Btn; }   // 32002
			if (N == TEXT("Disassembly_ClsBtn")) { BtnClose = Btn; }   // 32001
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Disassembly_Title"))    { TxtTitle = Tb; continue; }      // 32003
			if (N == TEXT("Disassembly_Btn_Text")) { TxtBtnStart = Tb; continue; }   // подпись 32002
			if (N == TEXT("Disassembly_Target"))   { TargetLabel = Tb; continue; }   // 32004
			for (int32 K = 0; K <= kPreviews; ++K)
			{
				if (N == FString::Printf(TEXT("Disassembly_RIcon%02d"), K))
				{
					if (K < kPreviews) { PrevLabels[K] = Tb; } else { ResLabel = Tb; }   // 32010…32015 / 32016
				}
			}
			for (int32 C = 0; C < kListCells; ++C)
			{
				if (N == FString::Printf(TEXT("Disassembly_Icon%02d"), C)) { ListLabels[C] = Tb; }   // 32050+C
			}
			continue;
		}
		if (N == TEXT("Disassembly_BestResult_grp")) { BestGroup = W; continue; }   // 32031 (группа блёсток)
		if (N.StartsWith(TEXT("Disassembly_Title_")) && N.EndsWith(TEXT("_DRAG"))) { DragHandle = W; continue; }
		// Планы `<idc>_<juiIdx>_<plane>` (закон `jui-importer-widget-naming`).
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }
		if (Idc == TEXT("Effect_Shining")) { if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(W)) { BestShines.Add(P); } continue; }
		if (Idc == TEXT("Disassembly_TargetEff")) { TargetEff = W; continue; }   // 32005
		if (Idc == TEXT("Disassembly_Select"))    { SelFrame = W; SelFrameBase = SlotPos(W); continue; }   // 32090
		if (Plane != 0) { continue; }
		if (Idc == TEXT("Disassembly_VProBar")) { Gauge = Cast<UImage>(W); continue; }   // 32030
		if (Idc == TEXT("Disassembly_Target"))  { TargetIcon = Cast<UImage>(W); TargetName = FName(*N); continue; }   // 32004
		if (Idc == TEXT("Disassembly_Failed06")) { Failed06 = W; continue; }   // 32156
		for (int32 K = 0; K <= kPreviews; ++K)
		{
			if (Idc == FString::Printf(TEXT("Disassembly_RIcon%02d"), K))
			{
				if (K < kPreviews) { PrevIcons[K] = Cast<UImage>(W); PrevNames[K] = FName(*N); }
				else { ResIcon = Cast<UImage>(W); ResName = FName(*N); }
			}
			if (K < kPreviews && Idc == FString::Printf(TEXT("Disassembly_Notice%02d"), K)) { PrevNotice[K] = W; }   // 32020+K
			if (K < kPreviews && Idc == FString::Printf(TEXT("Disassembly_Failed%02d"), K)) { PrevFailed[K] = W; }   // 32150+K
		}
		for (int32 C = 0; C < kListCells; ++C)
		{
			if (Idc == FString::Printf(TEXT("Disassembly_Icon%02d"), C))    { ListIcons[C] = Cast<UImage>(W); ListNames[C] = FName(*N); }
			if (Idc == FString::Printf(TEXT("Disassembly_Disable%02d"), C)) { ListDisable[C] = W; }   // 32100+C
		}
	}

	// ── Иконки — через ОБЩУЮ точку `UAsdaIconCellGrid`; клик ловим сами по живой геометрии (`TickMouse`). ──
	auto MakeGrid = [this](const TArray<FName>& Names, float Px, TFunction<int32(int32)>&& Code) -> UAsdaIconCellGrid*
	{
		UAsdaIconCellGrid* G = NewObject<UAsdaIconCellGrid>(this);
		G->SetCellSizePx(Px);
		G->SetClickOverlayEnabled(false);
		G->bClearEmptyCells = true;   // эталон затирает иконку пустой текстурой (`DrawTextureRect(пусто, 0, 0, 32, 32)`)
		G->SetupNamed(Widget, Names, /*bMale*/true, MoveTemp(Code), [](int32) {});
		return G;
	};
	// Список — СНИМОК кода на момент `decomposeWnd_RefreshSlots` (иконки рисуются там и только там).
	// ★ 29.10.2026 (инвентарь ×2): видимая позиция → индекс списка через окно прокрутки `BagScroll`.
	ListGrid = MakeGrid(ListNames, 32.f, [this](int32 C) { const int32 L = BagScroll.ToList(C); return ListIconCodes.IsValidIndex(L) ? ListIconCodes[L] : 0; });
	PrevGrid = MakeGrid(PrevNames, 32.f, [this](int32 K) { return GetPreviewCode(K); });
	if (!TargetName.IsNone())
	{
		TArray<FName> One; One.Add(TargetName);
		// 32004 рисует только запись item_list (`CItemManager_GetItemRecord` в `UpgradeUI_RefreshItemSlots`).
		TargetGrid = MakeGrid(One, 64.f, [this](int32)
		{
			const FAsdaItemInstance* L = LiveBag(SelCell);
			const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			return (L && !L->IsEmpty() && IT && IT->GetItem(L->ItemCode)) ? L->ItemCode : 0;
		});
	}
	if (!ResName.IsNone())
	{
		TArray<FName> One; One.Add(ResName);
		ResGrid = MakeGrid(One, 32.f, [this](int32) { return bResultShown ? RecCode(ResultRec) : 0; });
	}

	// ── Кнопки: ветка 66561 `upgradeWnd_onMessage`. Звука клика у окна НЕТ (у стиля кнопок — тоже, дамп ассета). ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnStart, kIdStart, [this]() { OnStartClicked(); });
	Bind(BtnClose, kIdClose, [this]() { OnCloseClicked(); });

	// ── Статичные подписи и стиль `CDecomposeWindow_InitControls` (ui_core.c:70043-70073) ──
	SetLabel(TxtTitle, Loc(AsdaDisassemblyLoc::kTitle));   // 32003 ← 100000
	if (TxtTitle) { AsdaUi::ApplyPseudoBold(TxtTitle); }   // `CClass_SetField926(32003, 0x200000)` — псевдо-жирный
	for (UTextBlock* Tb : PrevLabels) { LayoutLabel(Tb); }   // 32010…32015: 0x800000 + 9
	LayoutLabel(TargetLabel);                                // 32004
	LayoutLabel(ResLabel);                                   // 32016
	for (UTextBlock* Tb : ListLabels) { LayoutLabel(Tb); }  // 32050…32079
	// Разметка `.JUI`: 32005/32031/32090/32020…32025/32150…32156/32100…32129 — `Visible = 0`; ассет совпадает (дамп).

	// ── Счёт разобранного (структурный разбор — закон `full-structural-analysis-always`) ──
	FoundControls = 0; TotalControls = 0;
	auto Count = [this](const UObject* O) { ++TotalControls; if (O) { ++FoundControls; } };
	Count(TxtTitle); Count(DragHandle); Count(BtnStart); Count(TxtBtnStart); Count(BtnClose);
	for (int32 K = 0; K < kPreviews; ++K) { Count(PrevIcons[K]); Count(PrevLabels[K]); Count(PrevNotice[K]); Count(PrevFailed[K]); }
	Count(ResIcon); Count(ResLabel); Count(BestGroup); Count(Failed06);
	Count(TargetIcon); Count(TargetLabel); Count(TargetEff); Count(Gauge); Count(SelFrame);
	for (int32 C = 0; C < kListCells; ++C) { Count(ListIcons[C]); Count(ListLabels[C]); Count(ListDisable[C]); }
	TotalControls += 6; FoundControls += FMath::Min(6, BestShines.Num());   // блёстки 32031: 6 планов `Effect_Shining`

	bControlsReady = (BtnStart && ListIcons[0] && PrevIcons[0] && Gauge && TargetIcon && ResIcon);
	UE_LOG(LogAsdaDisasm, Log, TEXT("%s"), *DumpControls());
	if (!bControlsReady)
	{
		UE_LOG(LogAsdaDisasm, Error, TEXT("[разборка] ключевые контролы не разобраны (найдено %d/%d)"), FoundControls, TotalControls);
	}
}

void UAsdaDisassemblySubsystem::EnsureTable()
{
	// `CDisjointWnd_LoadDecompositionTable@0xE0F4B0`: `Bin.bbz/DecompositionTable.BIN`, записи по 104 б (MD5 сверяется
	//   загрузчиком эталона). В порту — `DT_decompositiontable` (+ ведомость значений сервера `_value_overlay`).
	//   Поиск эталона (`recordLookupByKeyId@0xE10560`) — ПО ИНДЕКСУ `код − первый код` при `первый ≤ код ≤ последний`.
	if (bTableLoaded) { return; }
	bTableLoaded = true;
	Table.Reset(); TableIndex.Reset();
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UDataTable* DT = IT ? IT->GetDataTable(FName(TEXT("decompositiontable"))) : nullptr;
	if (!DT)
	{
		UE_LOG(LogAsdaDisasm, Error, TEXT("[разборка] DT_decompositiontable недоступна — список окна 32000 будет пуст"));
		return;
	}
	TArray<FAsdaDecompositiontableRow*> Rows;
	DT->GetAllRows<FAsdaDecompositiontableRow>(TEXT("Disassembly"), Rows);
	for (const FAsdaDecompositiontableRow* R : Rows) { if (R) { Table.Add(*R); } }
	Table.Sort([](const FAsdaDecompositiontableRow& A, const FAsdaDecompositiontableRow& B) { return A.ItemCode < B.ItemCode; });
	TableFirstCode = Table.Num() > 0 ? Table[0].ItemCode : 0;
	bTableDense = true;
	int32 Flag1 = 0;
	for (int32 I = 0; I < Table.Num(); ++I)
	{
		if (Table[I].ItemCode != TableFirstCode + I) { bTableDense = false; }
		TableIndex.Add(Table[I].ItemCode, I);
		if (Table[I].ResultDrawCount == 1) { ++Flag1; }
	}
	UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] таблица: записей %d (коды %d…%d, подряд=%d), разбираемых (флаг +4 = 1) %d"),
		Table.Num(), TableFirstCode, Table.Num() ? Table.Last().ItemCode : 0, bTableDense ? 1 : 0, Flag1);
}

const FAsdaDecompositiontableRow* UAsdaDisassemblySubsystem::FindRecord(int32 ItemCode) const
{
	// `recordLookupByKeyId@0xE10560`: таблицы нет / код вне [первый; последний] ⇒ nullptr; иначе запись `код − первый`.
	if (Table.Num() == 0) { return nullptr; }
	if (ItemCode < TableFirstCode || ItemCode > Table.Last().ItemCode) { return nullptr; }
	if (bTableDense) { return &Table[ItemCode - TableFirstCode]; }
	const int32* I = TableIndex.Find(ItemCode);   // [ПОРТ] коды не подряд — по ключу (данные порта плотные, проверка в логе)
	return I ? &Table[*I] : nullptr;
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

void UAsdaDisassemblySubsystem::Toggle(const FString& Source)
{
	// `upgradeUiOnOpen@0xE0D530`: ПЕРВОЙ строкой `[222] = sub_93248D(g_pInventoryWnd)` — «сумка открыта» (и при закрытии).
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	bInvWasOpen = Inv && Inv->IsWindowOpen();
	if (bOpen)
	{
		// Видимое окно — закрыть, если не «идёт» ([89] != 1); при «идёт» — ничего.
		if (State != 1) { CloseWindow(); }
		JournalAdd(FString::Printf(TEXT("toggle (%s): %s"), *Source, State != 1 ? TEXT("закрыто") : TEXT("идёт — не закрыто")));
		UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] тумблер (%s): окно %s"), *Source, bOpen ? TEXT("осталось (идёт разборка)") : TEXT("закрыто"));
		return;
	}
	OpenFresh();
	JournalAdd(FString::Printf(TEXT("toggle (%s): открыто"), *Source));
	UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] тумблер (%s): окно открыто, в списке %d предметов, сумка открыта=%d"),
		*Source, List.Num(), bInvWasOpen ? 1 : 0);
}

void UAsdaDisassemblySubsystem::OpenFresh()
{
	// Ветка «окно скрыто» `upgradeUiOnOpen` (item_enhance.c:7712-7746).
	if (!EnsureWidget()) { return; }
	EnsureTable();
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Д-6: полоса шла при скрытом окне — довести её итог (применение + стоп петли) до `[89] = 0`.
	if (State == 1 && CVarFinishOnReopen.GetValueOnGameThread() != 0)
	{
		ApplyResult();
		StopLoop(TEXT("открытие при идущей полосе (Д-6)"));
		JournalAdd(TEXT("Д-6: результат применён при открытии"));
	}
	State = 0;                                   // `pUpgradeWnd[89] = 0`
	bOpen = true;
	WM->SetWindowVisible(kWindowId, true);       // `CControl_SetVisible(*pUpgradeWnd, 1)`
	SetButtonText(AsdaDisassemblyLoc::kBtnStart);   // 98700
	ClearPreviews();                             // `memset(pUpgradeWnd + 93, 0, 0x156)`
	SelCell = -1;                                // [179] = 0
	BagScroll.Reset();                           // ★ 29.10.2026 (инвентарь ×2): список — с первого ряда
	bResultShown = false;                        // [180] = 0
	RefreshList();                               // `decomposeWnd_RefreshSlots(…, 0)`
	AnimTick = 0.f;                              // [216] = 0
	if (SelFrame) { SelFrame->SetVisibility(ESlateVisibility::Collapsed); }   // `CControl_SetVisible([84], 0)`
	ShowPlane(BestGroup, false);                 // `CloseHide`+`SetVisible(0)` 32031
	ShowPlane(TargetEff, false);                 // 32005
	HidePreviewMarks();                          // 32020…32025
	RefreshItemSlots();
	if (!bGaugeBaseTaken && Gauge)
	{
		// `g_bUpgradeUiNeedReposition`: при ПЕРВОМ открытии база полосы = позиция и размер контрола 32030 (из ассета).
		bGaugeBaseTaken = true;
		GaugeBase = SlotPos(Gauge);
		GaugeSize = SlotSize(Gauge);
	}
	PlaceGauge();                                // `(x, y + 50)`, размер `(w, 0)`
	WM->BringWindowToFront(kWindowId);           // `CClass_MapInsert(g_pWindowMgr, окно)`
	Mouse.Reset();
	TipId = -1;
	bWasOverWindow = false;
}

void UAsdaDisassemblySubsystem::CloseWindow()
{
	// `upgradeUiWnd_close@0xE0E3C0`: снять тултип ([221]), применить отложенный результат, [89] = −1, скрыть; была
	//   открыта сумка ([222]) — `uiOpenInventoryWindow(g_pInventoryWnd, 1)`.
	if (TipId >= 0) { TickTooltip(-1); }
	const bool bWasRunning = (State == 1);
	ApplyResult();
	State = -1;
	// ★ Д-3: петлю «жара» эталон здесь не глушит — при закрытии посреди полосы она играет вечно.
	if (bWasRunning && CVarStopLoopOnClose.GetValueOnGameThread() != 0) { StopLoop(TEXT("закрытие окна (Д-3)")); }
	const bool bWasOpen = bOpen;
	bOpen = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	Mouse.Reset();
	bWasOverWindow = false;
	JournalAdd(TEXT("close"));
	if (bInvWasOpen)
	{
		bInvWasOpen = false;
		UGameInstance* GI = GetGameInstance();
		if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			// `uiOpenInventoryWindow(…, 1)` открывает сумку (у открытой — снова показывает и поднимает) и играет id 5.
			if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
			else if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->BringWindowToFront(FName(TEXT("INVEN_WIN"))); }
			if (Inv->IsWindowOpen())
			{
				if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound2DById(5); }
				JournalAdd(TEXT("сумка снова открыта ([222])"));
			}
		}
	}
	UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] окно 32000 закрыто%s"), bWasOpen ? TEXT("") : TEXT(" (уже было скрыто)"));
}

bool UAsdaDisassemblySubsystem::IsRecentActivity() const
{
	// Условия 2 и 4 `uiCheckRecentActivity@0xD326A0` (ui_core.c:49889-49895).
	if (State == 1) { return true; }
	return LastApplyTime >= 0.0 && (FPlatformTime::Seconds() - LastApplyTime) * 1000.0 < kRecentMs;
}

// ═════════════════════════════ ПОРТ ФУНКЦИЙ ОКНА ══════════════════════════════════════════════

void UAsdaDisassemblySubsystem::ClearPreviews()
{
	for (TArray<uint8>& P : Preview) { P.Reset(); P.SetNumZeroed(FAsdaItemInstance::kRecordSize); }
}

void UAsdaDisassemblySubsystem::HidePreviewMarks()
{
	for (UWidget* W : PrevNotice) { ShowPlane(W, false); }
}

void UAsdaDisassemblySubsystem::RefreshList()
{
	// `decomposeWnd_RefreshSlots@0xE0CC60` (a2 = 0): список = сумка 0…29, у которой есть запись таблицы с флагом +4 == 1;
	//   все 30 позиций — пустой текст и пустая текстура; иконки и «+N%» — по списку.
	//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): сумка 0…59 (`kShellCells`), список до 60; на экране — окно из 30 позиций
	//   (`BagScroll`, колесо по рядам), рамка выбора 32090 — над позицией выбранной вещи, если она в окне.
	List.Reset();
	ListIconCodes.Reset();
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	for (int32 I = 0; Inv && I < FMath::Min(kShellCells, Inv->GetContainerSize(1)); ++I)
	{
		const FAsdaItemInstance* C = Inv->GetCell(1, I);
		const int32 Code = C ? C->ItemCode : 0;
		const FAsdaDecompositiontableRow* R = FindRecord(Code);
		if (R && R->ResultDrawCount == 1) { List.Add(I); ListIconCodes.Add(Code); }   // `if (v29[1] == 1)` (+4 — флаг «разбирается»)
	}
	BagScroll.SetTotal(List.Num());   // ★ 29.10.2026 (инвентарь ×2): ряд клампится, если список укоротился
	for (int32 J = 0; J < kListCells; ++J) { SetLabel(ListLabels.IsValidIndex(J) ? ListLabels[J].Get() : nullptr, FString()); }
	// Иконка рисуется только у записи item_list (`if (itemRecord)`), и счёт позиций идёт ТОЛЬКО по нарисованным.
	//   У всех разбираемых кодов запись item_list есть (проверка данных: 44 357 из 44 357), поэтому позиция иконки
	//   совпадает с индексом списка — как у эталона на живых данных.
	for (int32 K = 0; K < List.Num(); ++K)
	{
		if (!IT || !IT->GetItem(ListIconCodes[K])) { ListIconCodes[K] = 0; continue; }
		const FAsdaItemInstance* C = Inv ? Inv->GetCell(1, List[K]) : nullptr;
		const int32 V = BagScroll.ToVisible(K);   // ★ 29.10.2026: подпись — у видимой позиции окна прокрутки
		if (C && V >= 0 && V < kListCells) { SetLabel(ListLabels[V], PercentText(C->EnchantPercent)); }   // `Util_Identity16(+27) > 0`
	}
	if (ListGrid) { ListGrid->Refresh(); }
	// ★ 29.10.2026 (инвентарь ×2): рамка выбора 32090 — за выбранной вещью (ячейкой сумки `SelCell`): в окне — над её
	//   позицией, вне окна прокрутки — спрятана.
	//   ★ Ревизия 30.10: переставляем ТОЛЬКО видимую рамку или по колесу (`GDisasmWheelRefresh`): приём 6503 прячет рамку
	//   и зовёт `RefreshList` ДО сброса `SelCell` (порядок эталона) — показывать её тут снова нельзя.
	if (SelFrame && SelCell >= 0 && (GDisasmWheelRefresh || SelFrame->GetVisibility() != ESlateVisibility::Collapsed))
	{
		const int32 K = List.IndexOfByKey(SelCell);
		const int32 V = (K != INDEX_NONE) ? BagScroll.ToVisible(K) : -1;
		if (V >= 0 && ListIcons.IsValidIndex(V) && ListIcons[V])
		{
			AsdaUi::PlaceFrameOver(SelFrame, ListIcons[V], /*bRestartAnim*/false);
			SelFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			SelFrame->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	JournalAdd(FString::Printf(TEXT("list %d"), List.Num()));
}

void UAsdaDisassemblySubsystem::RefreshItemSlots()
{
	// `UpgradeUI_RefreshItemSlots@0xE0D840`.
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	auto Kind = [IT](int32 Code) -> int32
	{
		// 1 — item_list, 2 — special_itemlist (Table2), 3 — самоцвет (Sowel), 0 — ничего.
		if (!IT || Code <= 0) { return 0; }
		if (IT->GetItem(Code)) { return 1; }
		FAsdaSpecialItemlistRow S;
		if (IT->GetSpecialItem(Code, S)) { return 2; }
		if (IT->IsSowelCode(Code)) { return 3; }
		return 0;
	};
	for (int32 K = 0; K < kPreviews; ++K)
	{
		UTextBlock* Tb = PrevLabels.IsValidIndex(K) ? PrevLabels[K].Get() : nullptr;
		SetLabel(Tb, FString());
		const int32 Code = GetPreviewCode(K);
		const int32 Kd = Kind(Code);
		if (Kd == 1 || Kd == 2) { SetLabel(Tb, PercentText(GetPreviewPercent(K))); }   // у самоцвета «+N%» нет
	}
	if (PrevGrid) { PrevGrid->Refresh(); }
	// 32004 — выбранная запись сумки ([179]); рисуется только запись item_list.
	SetLabel(TargetLabel, FString());
	if (const FAsdaItemInstance* L = LiveBag(SelCell))
	{
		if (IT && !L->IsEmpty() && IT->GetItem(L->ItemCode)) { SetLabel(TargetLabel, PercentText(L->EnchantPercent)); }
	}
	if (TargetGrid) { TargetGrid->Refresh(); }
	// 32016 — запись результата ([180]).
	SetLabel(ResLabel, FString());
	if (bResultShown)
	{
		const int32 Kd = Kind(RecCode(ResultRec));
		if (Kd == 1 || Kd == 2) { SetLabel(ResLabel, PercentText(RecPercent(ResultRec))); }
	}
	if (ResGrid) { ResGrid->Refresh(); }
}

void UAsdaDisassemblySubsystem::ResetWindow()
{
	// `upgradeUiWnd_reset@0xE10610`: только при [89] != 1.
	if (State == 1) { return; }
	AnimTick = 0.f;
	SelCell = -1;
	bResultShown = false;
	for (int32 K = 0; K < kPreviews; ++K) { SetLabel(PrevLabels.IsValidIndex(K) ? PrevLabels[K].Get() : nullptr, FString()); }
	HidePreviewMarks();
	ShowPlane(BestGroup, false);
	ShowPlane(TargetEff, false);
	SetButtonText(AsdaDisassemblyLoc::kBtnStart);
	ClearPreviews();
	PlaceGauge();
	State = 0;
	RefreshItemSlots();
	if (TipId >= 0) { TickTooltip(-1); }
	// ★ Д-4: рамка выбора гаснет вместе с выбором.
	if (SelFrame && CVarHideFrameWithoutSelection.GetValueOnGameThread() != 0) { SelFrame->SetVisibility(ESlateVisibility::Collapsed); }
	JournalAdd(TEXT("reset"));
}

void UAsdaDisassemblySubsystem::PlaceGauge()
{
	// `CControl_SetField74_75(32030, x, y + 50 − тик)` + `SetSize(w, тик)` — полоса растёт ВВЕРХ от низа контрола.
	//   [ПОРТ] тик < 0 (слот результата не найден, `25 × −1`) у эталона уходит в `(unsigned __int64)` отрицательного
	//   float — размер-мусор; порт держит 0 (UB не воспроизводим).
	if (!Gauge || !bGaugeBaseTaken) { return; }
	UCanvasPanelSlot* S = AsdaUi::SlotOf(Gauge);
	if (!S) { return; }
	const float T = FMath::Max(0.f, AnimTick);
	S->SetPosition(FVector2D(GaugeBase.X, GaugeBase.Y + (float)kGaugeLift - T));
	S->SetSize(FVector2D(GaugeSize.X, T));
}

void UAsdaDisassemblySubsystem::ApplyResult()
{
	// `upgradeUiWnd_applyResult@0xE10340`: только при коде 1 непрочитанного тела — [89] = 2, вес, две записи
	//   (+3 разобранный предмет, +60 материал). Контейнер 2 — запись в расходники КАК ЕСТЬ; контейнер 1 — `memset` +
	//   `UI_OnInventoryRemove` (МИСНЕЙМ: `Inventory_SetBagSlot@0xD61650`, кладёт запись при коде > 0). Затем [223] =
	//   «сейчас» и тело помечено прочитанным (`gap_2D8[0] = −1`) — ВСЕГДА, даже без результата.
	if (Pkt.Num() >= kBodyLen && Pkt[0] == 1)
	{
		State = 2;
		UGameInstance* GI = GetGameInstance();
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(Pkt.GetData() + 1)), -1, TEXT("6503 (применение)"));   // ★ 29.10.2026: вес u16   // `HUD_UpdateMiniWeightBar`
		}
		if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Inv->BeginCellBatch();   // хотбар (`CClass_BuildItemRecord(g_pQuickSlotBar, 2, запись)`) — одна пересборка в конце
			for (int32 I = 0; I < 2; ++I)
			{
				const uint8* Rec = Pkt.GetData() + 3 + 57 * I;
				const int32 Code = AsdaBytes::RdI32(Rec);
				const int32 Cont = (int32)Rec[4];
				const int32 Slot = AsdaBytes::RdI16(Rec + 5);
				if (Cont != 1 && Cont != 2) { continue; }   // эталон: прочие контейнеры не трогает
				const int32 Limit = FMath::Min(Cont == 1 ? kShellCells : kSpendCells, Inv->GetContainerSize(Cont));
				if (Slot < 0 || Slot >= Limit)
				{
					UE_LOG(LogAsdaDisasm, Warning, TEXT("[разборка] 6503 запись %d: контейнер %d слот %d вне границ — отброшена"), I, Cont, Slot);
					continue;
				}
				if (Code > 0)
				{
					TArray<uint8> R57; R57.Append(Rec, 57);
					Inv->ApplyExternalRecord(R57, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
				}
				else
				{
					Inv->ClearCell(Cont, Slot);
				}
				JournalAdd(FString::Printf(TEXT("apply %d: к=%d сл=%d код=%d кол=%d"), I, Cont, Slot, Code, AsdaBytes::RdI32(Rec + 9)));
			}
			Inv->EndCellBatch();
		}
	}
	LastApplyTime = FPlatformTime::Seconds();   // [223] = `CObject_GetField2(g_dwDisplayOptionFlags)` — «сейчас»
	if (Pkt.Num() > 0) { Pkt[0] = 0xFF; }       // `gap_2D8[0] = −1`
}

void UAsdaDisassemblySubsystem::TickAnim()
{
	// `CDisjointWnd_TickAnim@0xE0C8B0` — один кадр эталона.
	const int32 Target = kTicksPerSlot * SelSlot;
	AnimTick += 1.f;
	if ((float)Target <= AnimTick)
	{
		StopLoop(TEXT("конец полосы"));
		PlaySfx(TEXT("S_e_disjoint_result"));
		State = 2;
		AnimTick = (float)Target;
		// [180] = [181] — запись результата из тела 6503 (+60).
		bResultShown = true;
		ResultRec.Reset();
		if (Pkt.Num() >= kBodyLen) { ResultRec.Append(Pkt.GetData() + 60, FAsdaItemInstance::kRecordSize); } else { ResultRec.SetNumZeroed(FAsdaItemInstance::kRecordSize); }
		// `qmemcpy(превью[слот − 1], результат, 57)`. [ПОРТ] слот вне 1…6 (код не нашёлся) — эталон пишет ЗА массив
		//   превью (в поля окна), порт не пишет.
		if (SelSlot >= 1 && SelSlot <= kPreviews) { Preview[SelSlot - 1] = ResultRec; }
		ApplyResult();
		if (SelFrame) { SelFrame->SetVisibility(ESlateVisibility::Collapsed); }   // `CControl_SetVisible(m_pSelectedInven, 0)`
		RefreshList();
		if (SelSlot == kPreviews) { ShowPlane(BestGroup, true); }   // 32031 — лучший результат (верхний слот)
		for (int32 I = 0; I < kPreviews; ++I) { ShowPlane(PrevNotice.IsValidIndex(I) ? PrevNotice[I].Get() : nullptr, I == SelSlot - 1); }
		ShowPlane(TargetEff, false);
		SetButtonText(AsdaDisassemblyLoc::kBtnDone);   // 98702
		SelCell = -1;                                  // [179] = 0
		RefreshItemSlots();
		const uint32 SlotIdx = (uint32)(SelSlot - 1);
		if (SlotIdx <= 5)
		{
			const int32 Given = AsdaBytes::RdI16(Pkt.GetData() + 117);
			const FString Txt = Given > 0 ? FString::Printf(TEXT("%d"), Given) : FString();
			SetLabel(PrevLabels.IsValidIndex((int32)SlotIdx) ? PrevLabels[SlotIdx].Get() : nullptr, Txt);
			SetLabel(ResLabel, Txt);
		}
		JournalAdd(FString::Printf(TEXT("anim end: слот %d, тик %.0f"), SelSlot, AnimTick));
		UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] полоса доиграла: слот %d, результат %d"), SelSlot, RecCode(ResultRec));
	}
	PlaceGauge();
}

bool UAsdaDisassemblySubsystem::HasFreeSlots() const
{
	// `inventoryHasFreeMainAndBagSlot@0xE0F3E0`: расходники 1…59 — код == 0; сумка 0…29 — код 0 или −1. Нужны ОБА.
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return false; }
	bool bSpend = false, bBag = false;
	for (int32 I = 1; I < kSpendCells && !bSpend; ++I)
	{
		const FAsdaItemInstance* C = Inv->GetCell(2, I);
		if (!C || C->ItemCode == 0) { bSpend = true; }
	}
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон 0…29 — у нас доступная сумка (60, либо 30 при `asda.Inven.ShellX2 0`:
	//   ячейки 30..59 тогда пусты в модели, но места в них нет — поэтому не `kShellCells`, а размер контейнера).
	for (int32 J = 0; J < Inv->GetContainerSize(1) && !bBag; ++J)
	{
		const FAsdaItemInstance* C = Inv->GetCell(1, J);
		if (!C || C->ItemCode == 0 || C->ItemCode == -1) { bBag = true; }
	}
	return bSpend && bBag;
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

void UAsdaDisassemblySubsystem::OnSlotClick(int32 IdNum)
{
	// msg 0xB → `upgradeUiOnSlotClick@0xE0C390` (item_enhance.c:7450-7553).
	if (State == 1) { return; }
	if (IdNum >= kIdList0 && IdNum < kIdList0 + kListCells)
	{
		AnimTick = 0.f;                          // [216]
		SelCell = -1;                            // [179]
		bResultShown = false;                    // [180]
		ShowPlane(BestGroup, false);             // [86]
		ShowPlane(TargetEff, false);             // [88]
		SetLabel(ResLabel, FString());           // [22]
		for (int32 I = 0; I < kPreviews; ++I)
		{
			SetLabel(PrevLabels.IsValidIndex(I) ? PrevLabels[I].Get() : nullptr, FString());
			ShowPlane(PrevNotice.IsValidIndex(I) ? PrevNotice[I].Get() : nullptr, false);
		}
		SetButtonText(AsdaDisassemblyLoc::kBtnStart);
		ClearPreviews();
		PlaceGauge();
		const int32 VisIdx = IdNum - kIdList0;              // ★ 29.10.2026 (инвентарь ×2): видимая позиция окна
		const int32 Idx = BagScroll.ToList(VisIdx);         //   → индекс списка (окно прокрутки `BagScroll`)
		if (List.IsValidIndex(Idx))
		{
			SelCell = List[Idx];
			if (SelFrame && ListIcons.IsValidIndex(VisIdx) && ListIcons[VisIdx])
			{
				// `Class_GetVec2_74(иконка позиции)` → `CControl_SetField74_75(рамка)`.
				// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U19): общая точка рамки выбора. Было `SetPosition(SlotPos(иконка))` — верно только
				//   пока рамка и иконки в одном канвасе (сейчас так и есть — результат прежний); общий расчёт переводит позицию
				//   иконки в координаты родителя рамки при любой иерархии. Перезапуск кадров — как было у этого окна.
				AsdaUi::PlaceFrameOver(SelFrame, ListIcons[VisIdx], /*bRestartAnim*/true);
			}
			const FAsdaItemInstance* L = LiveBag(SelCell);
			const int32 Code = L ? L->ItemCode : 0;
			if (const FAsdaDecompositiontableRow* R = FindRecord(Code))
			{
				const int32 Res[kPreviews] = { R->Result0Code, R->Result1Code, R->Result2Code, R->Result3Code, R->Result4Code, R->Result5Code };
				for (int32 J = 0; J < kPreviews; ++J) { AsdaBytes::WrI32(Preview[J].GetData(), Res[J]); }
				// Перенос улучшения: процент +27 ≥ порога ⇒ все 6 превью = 363 (доспех: itemType ≤ 4 или 8) / 401 (оружие: 9)
				//   с тем же процентом. ★ Д-1: порог — серверный (88), у клиента оригинала 77.
				const int32 Pct = L ? L->EnchantPercent : 0;
				const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
				const FAsdaItemRecord* IR = IT ? IT->GetItem(Code) : nullptr;
				if (Pct >= CVarTransferThreshold.GetValueOnGameThread() && IR)
				{
					for (int32 K = 0; K < kPreviews; ++K)
					{
						if (IR->ItemType <= 4 || IR->ItemType == 8) { AsdaBytes::WrI32(Preview[K].GetData(), kTransferUpgradeOther); }
						if (IR->ItemType == 9) { AsdaBytes::WrI32(Preview[K].GetData(), kTransferUpgradeType9); }
						AsdaBytes::WrI16(Preview[K].GetData() + 27, Pct);
					}
					JournalAdd(FString::Printf(TEXT("перенос улучшения: %d%% ≥ порога %d"), Pct, CVarTransferThreshold.GetValueOnGameThread()));
				}
			}
			JournalAdd(FString::Printf(TEXT("select %d (ячейка %d, код %d)"), Idx, SelCell, Code));
		}
		else
		{
			// ★ Д-4: пустая позиция снимает выбор — у эталона рамка остаётся на прежней.
			if (SelFrame && CVarHideFrameWithoutSelection.GetValueOnGameThread() != 0) { SelFrame->SetVisibility(ESlateVisibility::Collapsed); }
			JournalAdd(FString::Printf(TEXT("select %d — пусто"), Idx));
		}
		// Гнёзда самоцветов (+16/+18/+20/+22 > 0) ⇒ ONEBUT 100800 «Живой камень уничтожается…».
		if (const FAsdaItemInstance* L = LiveBag(SelCell))
		{
			const bool bGems = L->Raw.Num() >= 57
				&& (AsdaBytes::RdI16(L->Raw.GetData() + 16) > 0 || AsdaBytes::RdI16(L->Raw.GetData() + 18) > 0
				 || AsdaBytes::RdI16(L->Raw.GetData() + 20) > 0 || AsdaBytes::RdI16(L->Raw.GetData() + 22) > 0);
			if (bGems)
			{
				AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Loc(AsdaDisassemblyLoc::kGemWarn));
				JournalAdd(FString::Printf(TEXT("msgbox %d"), AsdaDisassemblyLoc::kGemWarn));
			}
		}
	}
	RefreshItemSlots();
}

void UAsdaDisassemblySubsystem::OnStartClicked()
{
	// 66561 по 32002 → `upgradeUiConfirmSend@0xE0D420`.
	if (State == 1) { return; }
	if (SelCell < 0)
	{
		if (State == 2) { CloseWindow(); }   // «выполнено» без выбора — закрыть окно
		return;
	}
	const FAsdaItemInstance* L = LiveBag(SelCell);
	if (!L || !FindRecord(L->ItemCode)) { return; }   // `recordLookupByKeyId(*[179])` — записи нет: молча
	if (!HasFreeSlots())
	{
		AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Loc(AsdaDisassemblyLoc::kNoSpace));
		JournalAdd(FString::Printf(TEXT("msgbox %d"), AsdaDisassemblyLoc::kNoSpace));
		return;
	}
	if (ReplyLatch.IsArmed() && CVarSingleInFlight.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] «Начать»: предыдущий 6502 ещё без ответа — не шлём (Д-2)"));
		JournalAdd(TEXT("send 6502 — замок (Д-2)"));
		return;
	}
	TArray<uint8> Body;
	if (!DebugBuildRequest(Body)) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendDisassemble6502(Body[0], AsdaBytes::RdI16(Body.GetData() + 1));
	JournalAdd(FString::Printf(TEXT("send 6502 %s (к=%d сл=%d)"), bOk ? TEXT("OK") : TEXT("FAIL"), (int32)Body[0], AsdaBytes::RdI16(Body.GetData() + 1)));
	if (!bOk)
	{
		// ★ Закон «не защёлкивать состояние заранее»: пакет не ушёл — ответа не будет; кнопку не переводим в «идёт». [ПОРТ]
		UE_LOG(LogAsdaDisasm, Warning, TEXT("[разборка] 6502 не ушёл (нет соединения/гейт) — окно не тронуто"));
		return;
	}
	ReplyLatch.Arm(6502);   // ★ S2 (В2-A дедупа №2, 11.10.2026)
	SetButtonText(AsdaDisassemblyLoc::kBtnBusy);   // 98701
	// `netSendByteWordCmd_6502` после отправки: `sowelWindowClose(g_pInventoryWnd, 1)` — МИСНЕЙМ: ЗАКРЫТЬ СУМКУ.
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		if (Inv->IsWindowOpen()) { Inv->CloseWindow(); JournalAdd(TEXT("сумка закрыта (sowelWindowClose)")); }
	}
}

bool UAsdaDisassemblySubsystem::DebugBuildRequest(TArray<uint8>& OutBody) const
{
	// Тело 6502 для текущего выбора — сборщиком сетевого слоя: контейнер (+4) и слот (+5) ЖИВОЙ записи [179].
	OutBody.Reset();
	const FAsdaItemInstance* L = LiveBag(SelCell);
	if (!L || L->IsEmpty() || L->Raw.Num() < 57) { return false; }
	OutBody = UAsdaNetSubsystem::BuildDisassemble6502Body(L->Raw[4], AsdaBytes::RdI16(L->Raw.GetData() + 5));
	return OutBody.Num() == 3;
}

void UAsdaDisassemblySubsystem::OnCloseClicked()
{
	// 66561 по 32001: `if ([89] != 1) upgradeUiWnd_close`.
	if (State == 1) { JournalAdd(TEXT("X — идёт, не закрыто")); return; }
	CloseWindow();
}

void UAsdaDisassemblySubsystem::OnEscPressed()
{
	// 0x15: `if ([89] != 1) upgradeUiWnd_close`.
	if (!bOpen) { return; }
	if (State == 1) { JournalAdd(TEXT("ESC — идёт, не закрыто")); return; }
	CloseWindow();
}

void UAsdaDisassemblySubsystem::OnTargetDoubleClick()
{
	// 0xD по 32004 → `upgradeUiWnd_reset`.
	JournalAdd(TEXT("0xD 32004"));
	ResetWindow();
}

void UAsdaDisassemblySubsystem::OnMainWindowEnter()
{
	// msg 8 по 32000: `decomposeWnd_RefreshSlots(this, 0)`, [221] = 1 (флаг «тултип», снимается 0xA).
	RefreshList();
}

// ═════════════════════════════ ПРИЁМ 6503 ═════════════════════════════════════════════════════

void UAsdaDisassemblySubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	TArray<TArray<uint8>> Local;
	if (!PendingResults.Drain(Local)) { return; }
	for (const TArray<uint8>& B : Local) { OnResult(B); }
}

void UAsdaDisassemblySubsystem::ReleaseInFlight(const TCHAR* Why)
{
	if (!ReplyLatch.IsArmed()) { return; }
	ReplyLatch.Disarm();
	UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] замок «6502 в полёте» снят: %s (%.1f с)"), Why, FPlatformTime::Seconds() - ReplyLatch.ArmedAt);
}

void UAsdaDisassemblySubsystem::OnResult(const TArray<uint8>& Body)
{
	// `Recv_ItemDisjoint@0xE0EAD0` (network.c:27860-27960).
	if (Body.Num() < kBodyLen) { return; }
	ReleaseInFlight(TEXT("ответ 6503"));
	Pkt = Body;
	Pkt.SetNum(kBodyLen);
	const int32 Code = (int32)Body[0];
	JournalAdd(FString::Printf(TEXT("recv 6503 код %d"), Code));
	UE_LOG(LogAsdaDisasm, Log, TEXT("[разборка] 6503 код=%d (окно %s, состояние %d)"), Code, bOpen ? TEXT("открыто") : TEXT("закрыто"), State);
	if (Code != 1) { ResetWindow(); }
	switch (Code)
	{
	case 1:
	{
		State = 1;
		AnimAccum = 0.0;
		ShowPlane(TargetEff, true);              // 32005 — `CControl_Show` + `SetVisible(1)`
		StartLoop();                             // `CSoundFlag_PlaySound2D("S_e_disjoint_process.wav", 1)`
		SelSlot = -1;
		// Вес — сетевой слой (`HUD_UpdateMiniWeightBar`); слот результата = первый слот превью с кодом записи +60.
		const int32 ResCode = AsdaBytes::RdI32(Pkt.GetData() + 60);
		for (int32 K = 0; K < kPreviews; ++K)
		{
			if (GetPreviewCode(K) == ResCode) { SelSlot = K + 1; break; }
		}
		JournalAdd(FString::Printf(TEXT("идёт: результат %d → слот %d"), ResCode, SelSlot));
		break;
	}
	case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
		NoticeResult(AsdaDisassemblyLoc::kResBase + Code - 2);   // 98600…98607; код 8 — ещё вес (сетевой слой)
		break;
	default:
		break;   // код 0 и прочие — молча (окно уже сброшено)
	}
}

// ═════════════════════════════ МЕЛОЧИ ═════════════════════════════════════════════════════════

void UAsdaDisassemblySubsystem::NoticeResult(int32 LocId)
{
	// `sprintf(buf, "%s", нотис)` → `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, buf, 1)` (ONEBUT 1500) →
	//   `CChat_AddTokenizedLines(g_pChatWnd, buf, −11514, 10000, 0)` (системный чат, золотой, 10 с).
	UGameInstance* GI = GetGameInstance();
	const FString T = Loc(LocId);
	AsdaNotice::MsgBox(GI, EAsdaMsgCategory::ItemRefused, T);
	if (!T.IsEmpty()) { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, T, kColResultChat, 10000.f); }
	JournalAdd(FString::Printf(TEXT("msgbox+chat %d"), LocId));
}

void UAsdaDisassemblySubsystem::SetButtonText(int32 LocId)
{
	SetLabel(TxtBtnStart, Loc(LocId));   // `CWidget_SetTextAll4(32002, …)` — все 4 состояния кнопки одним текстом
	JournalAdd(FString::Printf(TEXT("btn %d"), LocId));
}

void UAsdaDisassemblySubsystem::ShowPlane(UWidget* W, bool bShow)
{
	// `CControl_Show`+`SetVisible(1)` — показать и запустить кадры с начала; `CloseHide`+`SetVisible(0)` — спрятать.
	if (!W) { return; }
	AsdaUi::SetShownNoHit(W, bShow);   // U3 (дедуп №2, В2-A, 11.10.2026): правило «показать без хит-теста» — общая `AsdaUi::SetShownNoHit` (вынос, поведение то же)
	if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(W)) { if (bShow) { P->RestartAnim(); } else { P->StopAnim(); } }
	if (W == BestGroup)
	{
		// Группа 32031: её планы `Effect_Shining` — дети, у эталона они идут вместе с родителем.
		for (UAsdaAnimatedPlane* P : BestShines) { if (P) { if (bShow) { P->RestartAnim(); } else { P->StopAnim(); } } }
	}
}

void UAsdaDisassemblySubsystem::StartLoop()
{
	StopLoop(TEXT("перезапуск"));
	JournalAdd(TEXT("snd loop S_e_disjoint_process"));
	bLoopActive = true;
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		LoopSound = Snd->PlaySound2DManaged(FName(TEXT("S_e_disjoint_process")));
	}
}

void UAsdaDisassemblySubsystem::StopLoop(const TCHAR* Why)
{
	// Журнал ведётся по ЛОГИЧЕСКОМУ состоянию петли: при -nosound компонента нет, а порядок «стоп → результат» тот же.
	if (!bLoopActive) { return; }
	bLoopActive = false;
	JournalAdd(FString::Printf(TEXT("snd stop S_e_disjoint_process (%s)"), Why));
	if (LoopSound && ::IsValid(LoopSound)) { LoopSound->Stop(); }
	LoopSound = nullptr;
}

void UAsdaDisassemblySubsystem::TickLoop()
{
	// У FMOD петля — флаг вызова (`…, 1`); у ассета UE флага петли может не быть — пока [89] == 1, доигравший
	//   звук запускается снова (та же петля по смыслу).
	if (LoopSound && ::IsValid(LoopSound) && State == 1 && !LoopSound->IsPlaying()) { LoopSound->Play(); }
}

void UAsdaDisassemblySubsystem::PlaySfx(const TCHAR* Name)
{
	// `CSoundFlag_PlaySound2D(g_pSoundMgr, "имя.wav", 0)` — 2D по имени, без петли.
	JournalAdd(FString::Printf(TEXT("snd %s"), Name));
	AsdaUi::PlaySound2DByName(GetGameInstance(), Name);   // ★ R5-b 12.11.2026: общая точка (то же тело)
}

void UAsdaDisassemblySubsystem::JournalAdd(const FString& Event)
{
	AsdaUi::JournalAppend(Journal, JournalStart, Event);   // ★ R5-b 12.11.2026: общая точка (то же тело, хвост 256 строк)
}

FString UAsdaDisassemblySubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

const FAsdaItemInstance* UAsdaDisassemblySubsystem::LiveBag(int32 Cell) const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return (Inv && Cell >= 0) ? Inv->GetCell(1, Cell) : nullptr;
}

int32 UAsdaDisassemblySubsystem::RecCode(const TArray<uint8>& R)
{
	return R.Num() >= 4 ? AsdaBytes::RdI32(R.GetData()) : 0;
}

int32 UAsdaDisassemblySubsystem::RecPercent(const TArray<uint8>& R)
{
	return R.Num() >= 29 ? AsdaBytes::RdI16(R.GetData() + 27) : 0;
}

int32 UAsdaDisassemblySubsystem::GetPreviewCode(int32 I) const
{
	return (I >= 0 && I < kPreviews) ? RecCode(Preview[I]) : 0;
}

int32 UAsdaDisassemblySubsystem::GetPreviewPercent(int32 I) const
{
	return (I >= 0 && I < kPreviews) ? RecPercent(Preview[I]) : 0;
}

int32 UAsdaDisassemblySubsystem::GetResultCode() const
{
	return bResultShown ? RecCode(ResultRec) : 0;
}

// ═════════════════════════════ ОПРОС МЫШИ И ТУЛТИПЫ ═══════════════════════════════════════════

int32 UAsdaDisassemblySubsystem::HoverIdNum() const
{
	for (int32 C = 0; C < kListCells; ++C)
	{
		if (ListIcons.IsValidIndex(C) && ListIcons[C] && AsdaUi::IsCursorOverWidget(ListIcons[C])) { return kIdList0 + C; }
	}
	for (int32 K = 0; K < kPreviews; ++K)
	{
		if (PrevIcons.IsValidIndex(K) && PrevIcons[K] && AsdaUi::IsCursorOverWidget(PrevIcons[K])) { return kIdPrev0 + K; }
	}
	if (TargetIcon && AsdaUi::IsCursorOverWidget(TargetIcon)) { return kIdTarget; }
	if (ResIcon && AsdaUi::IsCursorOverWidget(ResIcon)) { return kIdResult; }
	return -1;
}

void UAsdaDisassemblySubsystem::TickMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// Закон шапки `AsdaWindowSubsystem.h`: ПЕРВОЙ строкой — чужой всплывающий слой / окно выше под курсором.
	if (!WM || WM->IsPointerBlockedByTopmost(kWindowId) || WM->IsCursorOverHigherWindow(kWindowId))
	{
		TickTooltip(-1);
		Mouse.Reset();
		bWasOverWindow = false;
		return;
	}
	const bool bOver = WM->IsCursorOverWindow(kWindowId);
	if (bOver && !bWasOverWindow) { OnMainWindowEnter(); }   // msg 8 по 32000
	bWasOverWindow = bOver;
	const int32 Id = bOver ? HoverIdNum() : -1;
	TickTooltip(Id);   // msg 9 / 0xA
	if (!Mouse.bLmbDown || !bOver) { return; }
	// msg 0xD по 32004 — двойной ЛКМ; иначе 0xB.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U1): общий `FAsdaDblClick` — порог Windows `GetDoubleClickTime`, реальное время (было — `UInputSettings::DoubleClickTime`
	//   0,2 с, как у сумки; прежний порог — `LegacySec`, ключ `asda.UI.DblClickLegacy 1`). Нажатия по другим узлам пару, как и
	//   прежде, не рвут: распознаватель зовётся только для 32004.
	if (Id == kIdTarget)
	{
		TargetDbl.LegacySec = (double)GetDefault<UInputSettings>()->DoubleClickTime;
		if (TargetDbl.Click(kIdTarget))
		{
			OnTargetDoubleClick();
			return;
		}
	}
	OnSlotClick(Id);   // 0xB по любому контролу окна: позиция списка — выбор, прочее — только перерисовка слотов
}

// ★ U1 (В2-A дедупа №2): копия распознавателя с прежним порогом окна — для самотеста (описание — у объявления).
FAsdaDblClick UAsdaDisassemblySubsystem::DebugDblClickProbe() const
{
	FAsdaDblClick C = TargetDbl;
	C.LegacySec = (double)GetDefault<UInputSettings>()->DoubleClickTime;   // то же выражение, что в опросе мыши (32004)
	return C;
}

void UAsdaDisassemblySubsystem::TickTooltip(int32 IdNum)
{
	// `itemSlotShowTooltipById@0xE0F140` (msg 9), msg 0xA — `CControl_KillFocus(&g_ItemTooltip)`.
	// ★ Дедуп №2, В1-B (U18, 11.10.2026): шапка трекера (ключ «что показано», гашение при уходе курсора) — общая точка
	//   `UAsdaItemTooltipSubsystem::BeginHoverTip`; тело показа (эталонные билдеры окна) — ниже, как было.
	UAsdaItemTooltipSubsystem* Tip = UAsdaItemTooltipSubsystem::BeginHoverTip(GetGameInstance(), EAsdaTipOwner::Disassembly, TipId, IdNum);
	if (!Tip) { return; }
	// Запись под контролом: 32016 — [180], 32004 — [179], 32010+i — превью i, список — запись сумки позиции.
	FAsdaItemInstance Slot;
	bool bHave = false;
	auto FromRec = [&Slot, &bHave](const TArray<uint8>& R) { if (R.Num() >= 57) { Slot.SetRaw(R.GetData(), 57); bHave = true; } };
	if (IdNum == kIdResult && bResultShown) { FromRec(ResultRec); }
	if (IdNum == kIdTarget) { if (const FAsdaItemInstance* L = LiveBag(SelCell)) { Slot = *L; bHave = true; } }
	if (IdNum >= kIdPrev0 && IdNum < kIdPrev0 + kPreviews) { FromRec(Preview[IdNum - kIdPrev0]); }
	if (IdNum >= kIdList0 && IdNum < kIdList0 + kListCells && List.IsValidIndex(BagScroll.ToList(IdNum - kIdList0)))   // ★ 29.10.2026: окно прокрутки
	{
		if (const FAsdaItemInstance* L = LiveBag(List[BagScroll.ToList(IdNum - kIdList0)])) { Slot = *L; bHave = true; }
	}
	bool bShown = false;
	const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	if (bHave && !Slot.IsEmpty() && IT)
	{
		const int32 Code = Slot.ItemCode;
		FAsdaSpecialItemlistRow S;
		if (const FAsdaItemRecord* R = IT->GetItem(Code))
		{
			// itemType 5…7 и > 9 — простой по коду (`CControl_SetState(&g_ItemTooltip, 0, код, …)`), иначе полный по записи.
			if (R->ItemType > 4 && (R->ItemType < 8 || R->ItemType > 9)) { Tip->ShowItemTooltipByCode(Code, EAsdaTipOwner::Disassembly); }
			else { Tip->ShowItemTooltip(Slot, /*bSuppressCompare*/false, EAsdaTipOwner::Disassembly, 1); }
			bShown = true;
		}
		else if (IT->IsSowelCode(Code)) { Tip->ShowItemTooltipByCode(Code, EAsdaTipOwner::Disassembly); bShown = true; }   // `SetState(1, код)`
		else if (IT->GetSpecialItem(Code, S)) { Tip->ShowItemTooltip(Slot, false, EAsdaTipOwner::Disassembly, 1); bShown = true; }
	}
	if (!bShown) { Tip->HideTooltipFor(EAsdaTipOwner::Disassembly); }
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaDisassemblySubsystem::GetButtonText() const
{
	return TxtBtnStart ? TxtBtnStart->GetText().ToString() : FString();
}

FString UAsdaDisassemblySubsystem::GetLabelText(int32 IdNum) const
{
	const UTextBlock* Tb = nullptr;
	if (IdNum == kIdTarget) { Tb = TargetLabel; }
	if (IdNum == kIdResult) { Tb = ResLabel; }
	if (IdNum == 32003) { Tb = TxtTitle; }
	if (IdNum >= kIdPrev0 && IdNum < kIdPrev0 + kPreviews && PrevLabels.IsValidIndex(IdNum - kIdPrev0)) { Tb = PrevLabels[IdNum - kIdPrev0]; }
	if (IdNum >= kIdList0 && IdNum < kIdList0 + kListCells && ListLabels.IsValidIndex(IdNum - kIdList0)) { Tb = ListLabels[IdNum - kIdList0]; }
	return Tb ? Tb->GetText().ToString() : FString();
}

bool UAsdaDisassemblySubsystem::IsControlShown(int32 IdNum) const
{
	const UWidget* W = nullptr;
	if (IdNum == kIdEff)   { W = TargetEff; }
	if (IdNum == kIdBest)  { W = BestGroup; }
	if (IdNum == kIdFrame) { W = SelFrame; }
	if (IdNum == kIdGauge) { W = Gauge; }
	if (IdNum >= kIdNote0 && IdNum < kIdNote0 + kPreviews && PrevNotice.IsValidIndex(IdNum - kIdNote0)) { W = PrevNotice[IdNum - kIdNote0]; }
	if (!W) { return false; }
	const ESlateVisibility V = W->GetVisibility();
	return V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
}

UWidget* UAsdaDisassemblySubsystem::GetHitWidget(int32 IdNum) const
{
	if (IdNum == kIdStart)  { return BtnStart; }
	if (IdNum == kIdClose)  { return BtnClose; }
	if (IdNum == kIdTarget) { return TargetIcon; }
	if (IdNum == kIdResult) { return ResIcon; }
	if (IdNum >= kIdPrev0 && IdNum < kIdPrev0 + kPreviews && PrevIcons.IsValidIndex(IdNum - kIdPrev0)) { return PrevIcons[IdNum - kIdPrev0]; }
	if (IdNum >= kIdList0 && IdNum < kIdList0 + kListCells && ListIcons.IsValidIndex(IdNum - kIdList0)) { return ListIcons[IdNum - kIdList0]; }
	return nullptr;
}

FVector2D UAsdaDisassemblySubsystem::GetGaugeSlotPos() const { return SlotPos(Gauge); }
FVector2D UAsdaDisassemblySubsystem::GetGaugeSlotSize() const { return SlotSize(Gauge); }
FVector2D UAsdaDisassemblySubsystem::GetSelFramePos() const { return SlotPos(SelFrame); }

void UAsdaDisassemblySubsystem::DebugFinishAnim()
{
	int32 Guard = 0;
	while (State == 1 && Guard++ < 1000) { TickAnim(); }
}

FString UAsdaDisassemblySubsystem::DumpControls() const
{
	FString S = TEXT("=== окно разборки 32000: разобранные контролы ===\n");
	auto Line = [&S](const FString& Id, const UObject* W) { S += FString::Printf(TEXT("  %-46s %s\n"), *Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("32003 Disassembly_Title (заголовок)"), TxtTitle);
	Line(TEXT("32003 _DRAG — ручка окна"), DragHandle);
	Line(TEXT("32002 Disassembly_Btn «Начать»"), BtnStart);
	Line(TEXT("32002 подпись"), TxtBtnStart);
	Line(TEXT("32001 Disassembly_ClsBtn"), BtnClose);
	for (int32 K = 0; K < kPreviews; ++K)
	{
		Line(FString::Printf(TEXT("%d RIcon%02d план / текст"), kIdPrev0 + K, K), (PrevIcons.IsValidIndex(K) && PrevIcons[K] && PrevLabels.IsValidIndex(K) && PrevLabels[K]) ? PrevIcons[K].Get() : nullptr);
		Line(FString::Printf(TEXT("%d Notice%02d (рамка)"), kIdNote0 + K, K), PrevNotice.IsValidIndex(K) ? PrevNotice[K].Get() : nullptr);
		Line(FString::Printf(TEXT("%d Failed%02d (код не включает)"), 32150 + K, K), PrevFailed.IsValidIndex(K) ? PrevFailed[K].Get() : nullptr);
	}
	Line(TEXT("32016 RIcon06 план"), ResIcon);
	Line(TEXT("32016 RIcon06 текст"), ResLabel);
	Line(TEXT("32031 BestResult (группа)"), BestGroup);
	S += FString::Printf(TEXT("  32031 блёстки Effect_Shining: %d/6\n"), BestShines.Num());
	Line(TEXT("32156 Failed06 (код не включает)"), Failed06);
	Line(TEXT("32004 Target план 64×64"), TargetIcon);
	Line(TEXT("32004 Target текст"), TargetLabel);
	Line(TEXT("32005 TargetEff (анимация)"), TargetEff);
	Line(TEXT("32030 VProBar («жар»)"), Gauge);
	Line(TEXT("32090 Select (рамка списка)"), SelFrame);
	int32 Li = 0, Ll = 0, Ld = 0;
	for (const auto& P : ListIcons) { if (P) { ++Li; } }
	for (const auto& P : ListLabels) { if (P) { ++Ll; } }
	for (const auto& P : ListDisable) { if (P) { ++Ld; } }
	S += FString::Printf(TEXT("  32050…32079 список: планы %d/30, тексты %d/30; 32100…32129 затемнение %d/30\n"), Li, Ll, Ld);
	S += FString::Printf(TEXT("  полоса: база (%.0f, %.0f) размер %.0f×%.0f (взята=%d)\n"), GaugeBase.X, GaugeBase.Y, GaugeSize.X, GaugeSize.Y, bGaugeBaseTaken ? 1 : 0);
	S += FString::Printf(TEXT("  ИТОГО: найдено %d/%d\n"), FoundControls, TotalControls);
	return S;
}

FString UAsdaDisassemblySubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("=== окно разборки: состояние ===\n")
		TEXT("  открыто=%d [89]=%d [179]ячейка=%d [180]=%d (код %d) [215]слот=%d [216]тик=%.0f [222]сумка=%d недавно=%d замок=%d таблица=%d\n"),
		bOpen ? 1 : 0, State, SelCell, bResultShown ? 1 : 0, GetResultCode(), SelSlot, AnimTick, bInvWasOpen ? 1 : 0,
		IsRecentActivity() ? 1 : 0, ReplyLatch.IsArmed() ? 1 : 0, Table.Num());
	S += FString::Printf(TEXT("  кнопка: «%s»\n"), *GetButtonText());
	S += TEXT("  превью:");
	for (int32 K = 0; K < kPreviews; ++K) { S += FString::Printf(TEXT(" [%d]%d/+%d «%s»"), K, GetPreviewCode(K), GetPreviewPercent(K), *GetLabelText(kIdPrev0 + K)); }
	S += FString::Printf(TEXT("\n  список (%d), ряд прокрутки %d/%d:"), List.Num(), BagScroll.Row, BagScroll.MaxRow());
	for (int32 I = 0; I < List.Num(); ++I) { S += FString::Printf(TEXT(" [%d]яч%d=%d"), I, List[I], ListIconCodes.IsValidIndex(I) ? ListIconCodes[I] : 0); }
	S += TEXT("\n");
	return S;
}

// ═════════════════════════════ КОНСОЛЬ ════════════════════════════════════════════════════════

namespace
{
	UAsdaDisassemblySubsystem* DisasmFromWorld(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaDisassemblySubsystem>() : nullptr;
	}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdDisasmToggle(
		TEXT("asda.Disassembly.Toggle"),
		TEXT("asda.Disassembly.Toggle — окно 32000 (порт upgradeUiOnOpen: тумблер, как хоткей 40 / кнопка 453 / доп-меню)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			if (UAsdaDisassemblySubsystem* S = DisasmFromWorld(W)) { S->Toggle(TEXT("консоль")); }
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdDisasmState(
		TEXT("asda.Disassembly.State"),
		TEXT("asda.Disassembly.State — контролы, поля эталона, список, превью, журнал."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			if (UAsdaDisassemblySubsystem* S = DisasmFromWorld(W))
			{
				UE_LOG(LogAsdaDisasm, Log, TEXT("%s%s"), *S->DumpControls(), *S->DumpState());
				for (const FString& L : S->GetJournal()) { UE_LOG(LogAsdaDisasm, Log, TEXT("  журнал: %s"), *L); }
			}
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdDisasmClick(
		TEXT("asda.Disassembly.Click"),
		TEXT("asda.Disassembly.Click <IDNum> — боевой обработчик контрола окна 32000 (32050…32079, 32002, 32001, 32004 = двойной)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaDisassemblySubsystem* S = DisasmFromWorld(W);
			if (!S || Args.Num() < 1) { return; }
			const int32 Id = FCString::Atoi(*Args[0]);
			if (Id == 32002) { S->OnStartClicked(); }
			else if (Id == 32001) { S->OnCloseClicked(); }
			else if (Id == 32004) { S->OnTargetDoubleClick(); }
			else { S->OnSlotClick(Id); }
		}));
#endif // ASDA_DEV_CONSOLE
}
