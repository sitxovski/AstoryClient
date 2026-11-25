// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterPanelSubsystem.cpp — ОКНО «БАРАБАН ГАЧИ» (JUI 10050 `BOOSTING_WIN`).
//   Реверс, адреса и разбор каждого шага — шапка `Public/UI/AsdaBoosterPanelSubsystem.h`
//   и отчёт `Reports/item_use_panels.md`. Ни одно число здесь не выдумано.
//
// ═══════════ ТАБЛИЦА КОНТРОЛОВ (gamemain_jui.ini + имена узлов WBP, снятые дампом) ═════════════
//   Дамп WBP: `Reports/headless__probe_itempanels_widgets.log` (51 узел).
//   Привязка эталона: `InventoryWindow_InitControls` (`item_inventory.c:35490-35597`) — окно
//   принадлежит `CInventoryWindow`, индексы полей 223…248.
//
//   idnum  idc                    узел WBP                  скин (кадры из JuiSkin)      роль
//   10050  BOOSTING_WIN           BOOSTING_WIN_0_0…_0_8      Popup_SmWin                  рамка 3×3
//   10051  BOOSTER_BG             BOOSTER_BG_1_0 / _1_DRAG   Booster_BG                   фон + ручка EVS{6→0x20003}
//   10052  BOOSTER_SIREN          BOOSTER_SIREN_2_0          Booster_Siren 12 кадров @700 сирена (всегда крутится)
//   10053  BOOSTER_CHIMNEY_BG     BOOSTER_CHIMNEY_BG_3_0     Booster_Chimney_BG статич.   труба «выкл»
//   10054  BOOSTER_CLOCK_BG       BOOSTER_CLOCK_BG_4_0       Booster_Clock_BG статич.     часы «выкл»
//   10055  BOOSTER_CHIMNEY        BOOSTER_CHIMNEY_5_0        Booster_Chimney 16 @1200     труба «вкл»
//   10056  BOOSTER_CLOCK          BOOSTER_CLOCK_6_0          Booster_Clock 16 @800        часы «вкл»
//   10057  BOOSTER_EYE1           BOOSTER_EYE1_7_0           Booster_Eye_White 3 @300     глаз
//   10058  BOOSTER_EYE2           BOOSTER_EYE2_8_0           Booster_Eye_White 3 @300     глаз
//   10059  BOOSTER_SPOT1          BOOSTER_SPOT1_9_0          Booster_Eye_Yellow 3 @300    лампа
//   10060  BOOSTER_SPOT2          BOOSTER_SPOT2_10_0         Booster_Eye_Blue 3 @300      лампа
//   10061  BOOSTER_SPOT3          BOOSTER_SPOT3_11_0         Booster_Eye_Blue 3 @300      лампа
//   10062  BOOSTER_SPOT4          BOOSTER_SPOT4_12_0         Booster_Eye_Yellow 3 @300    лампа
//   10063  BOOSTER_BORDER         BOOSTER_BORDER_13_0        Booster_Border 16 @900       рамка вокруг ячейки
//   10064  BOOSTER_QUESTION       BOOSTER_QUESTION_14_0      Booster_Question 25 @1500    «?» до старта
//   10065  BOOSTER_BANG           BOOSTER_BANG_15_0          Booster_Center 15 @800       «взрыв» после остановки
//   10066  BOOSTER_PROGRESS       BOOSTER_PROGRESS_16_0/_16_1 —                           подложка + ЗАЛИВКА
//   10067  BOOSTER_NAME           BOOSTER_NAME               —                            имя приза
//   10068  BOOSTER_RESULT         BOOSTER_RESULT_18_0        —      ★ ЕДИНСТВЕННАЯ ЯЧЕЙКА БАРАБАНА
//   10069  BOOSTER_START          BOOSTER_START + _Text      —                            «Старт» (нотис 12162)
//   10070  BOOSTER_OK             BOOSTER_OK + _Text         —                            «Подтвердить» (2288)
//   10071  BOOSTER_CLOSE          BOOSTER_CLOSE              —                            крестик
//   10072  BOOSTER_PreviewBtn     BOOSTER_PreviewBtn + _Text —                            «Просмотр» (12870)
//   10073/10074/10075  подложки кнопок  …BtnBase_grp          —                            START / View / OK
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (дедуп №2, В2-A): ConsoleEcho — общее эхо консольных команд
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI

#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"          // «взрыв» 10065 — анимплан, ему нужен перезапуск прогона
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaHudSubsystem.h"           // ★ анонс редкой добычи по призу гачи (NotifyItemInfo)
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"  // ★ 31.10.2026: щелчок по набору в окне «Вид» открывает окно состава 26500
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBooster, Log, All);

namespace
{
	const FName kWindowId(TEXT("BOOSTING_WIN"));
	const FName kZoneId(TEXT("BOOSTING_WIN"));
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTING_WIN");

	// Нотисы (`error list.bin`): подписи кнопок и текст таймаута.
	constexpr int32 kLocCapStart   = 12162;   // «Старт»
	constexpr int32 kLocCapOk      = 2288;    // «Подтвердить»
	constexpr int32 kLocCapPreview = 12870;   // «Просмотр»
	constexpr int32 kLocTimeout    = 13114;   // «Window will close due to exceeded standby time»

	// Звуки (имена сняты дословно с эталона `itemBoosterWnd_Open` и `Recv_BoosterShopResult_0`).
	const FName kSndOpen(TEXT("s_item_boosting01"));   // зациклённый, играет пока идёт ожидание
	const FName kSndSpin(TEXT("s_item_boosting02"));   // одиночный, в момент приёма 5469

	// Ширина заливки прогресс-бара 10066 из .JUI (слот 175×11). Позицию (Left) НЕ трогаем, чтобы не
	//   спорить со сдвигами перетаскивания — заливка живёт внутри группы фона и едет вместе с ней.
	constexpr float kProgressWidth = 175.f;

	// Размер отрисовки иконки барабана — эталон рисует 64×64 (`shop_trade.c:12261`).
	constexpr float kDrumIconPx = 64.f;

	// ── ★★★ Окно «Вид» / «Просмотр» — JUI `BOOSTER_PREV_WIN`, IDNum 15600 ──
	const FName kPrevWindowId(TEXT("BOOSTER_PREV_WIN"));
	const TCHAR* kPrevWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_BOOSTER_PREV_WIN");
	// Ячейки окна 15600 в .JUI имеют размер 64×64 (`BOOSTER_PREV_SITEM00`… и `BOOSTER_PREV_ITEM05`…),
	//   а эталон рисует в них иконку тем же `CControl_DrawTextureRect(..., 64, 64)`.
	constexpr float kPrevCellPx = 64.f;
	// ★★★ ПРИВЯЗКА ОКНА «ВИД» К ОКНУ БАРАБАНА — дословно `boosterWnd_OnMessage@0xD16ED0`,
	//   `case 0x2758u` (`shop_trade.c:11953-11960`):
	//       Class_GetVec2_74(v13[223], &x, &y);              // v13[223] = КОРЕНЬ окна 10050
	//       x += ((Rect*)vtbl[24](v13[223]))->w;             // + ШИРИНА окна барабана
	//       y += ((Rect*)vtbl[24](v13[223]))->h;             // + ВЫСОТА окна барабана
	//       CControl_SetField74_75(g_pHoverTooltipWnd, x, y − 263);
	//   ⇒ левый край окна «Вид» = ПРАВЫЙ край окна барабана (зазор 0), а верх = верх барабана + 3.
	//   Размеры взяты из .JUI (`Data/Decoded/jui/gamemain_jui.ini`, `BOOSTING_WIN` Size = "266 266").
	constexpr float kWinW = 266.f;
	constexpr float kWinH = 266.f;
	// Подъём окна: третий аргумент `CControl_SetField74_75` — `y + h − 263`.
	constexpr float kPrevOffsetY = 263.f;
}

// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (закон `fix-original-defects`), ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНО.
//   В оригинале кнопка START (10069) во время идущего процесса НЕ блокируется: в её `case` нет ни
//   одной проверки (`shop_trade.c:11939-11947`), повторный клик шлёт 5468 заново и перезапускает
//   30-секундный таймаут. Это дефект. Включить защиту: asda.Booster.GuardStart 1.
static ASDA_DEV_CVAR(int32, CVarBoosterGuardStart,
	TEXT("asda.Booster.GuardStart"), 0,
	TEXT("Кнопка START окна 10050 во время идущего процесса:\n")
	TEXT("  0 — как в эталоне: не блокируется, повторный клик шлёт 5468 снова (умолчание)\n")
	TEXT("  1 — наша защита от дефекта оригинала: повторный клик игнорируется"),
	ECVF_Default);

// ★ 31.10.2026 [ОРИГИНАЛ]: подсказки ячеек окна «Вид» 15600 — при любом открытии окна (магазин NPC, барабан), разбор у `Tick`.
static ASDA_DEV_CVAR(int32, CVarBoosterPrevTipAnyOwner,
	TEXT("asda.Booster.PrevTipAnyOwner"), 1,
	TEXT("1 — как в оригинале: наведение на «особые» и «обычные» предметы окна «Вид» бустера показывает подсказку предмета, "
	     "откуда бы окно ни открыли (магазин «Усилители», барабан); 0 — прежний порт: только при открытом барабане 10050."),
	ECVF_Default);

// ★ 31.10.2026 [ОРИГИНАЛ]: входы окна «Вид» из премиум-магазина 6000 и окна состава набора 26500, щелчок по ячейкам самого
//   окна (набор → окно 26500) и закрытие «Вида» вместе с окном 26500. Поток — `Reports/booster_view/FLOW.md`.
static ASDA_DEV_CVAR(int32, CVarBoosterPrevPaths,
	TEXT("asda.Booster.PrevPaths"), 1,
	TEXT("1 — как в оригинале: окно «Вид» бустера открывает и премиум-магазин (клик по карточке бустера), и окно состава набора "
	     "(клик по бустеру в составе); клик по набору внутри «Вида» открывает его состав; закрытие окна состава закрывает «Вид». "
	     "0 — прежний порт: «Вид» только из барабана и магазина «Усилители»."),
	ECVF_Default);

// ★ 31.10.2026 [ОРИГИНАЛ]: каждое открытие «Вида» начинает с первой страницы (эталон: `uiStorageWindow_ResetState` / `v62[662] = 0`),
//   а окно, открытое НЕ барабаном, при перезаполнении (смена страницы) остаётся на месте.
static ASDA_DEV_CVAR(int32, CVarBoosterPrevOpenReset,
	TEXT("asda.Booster.PrevOpenReset"), 1,
	TEXT("1 — как в оригинале: новое открытие окна «Вид» показывает первую страницу состава; при смене страницы окно, открытое "
	     "магазином или окном набора, не прыгает к барабану. 0 — прежний порт: страница сохранялась, перезаполнение ставило окно "
	     "по формуле барабана."),
	ECVF_Default);

bool UAsdaBoosterPanelSubsystem::PrevPathsOn()
{
	return CVarBoosterPrevPaths.GetValueOnGameThread() != 0;
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaBoosterPanelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Spots.SetNum(4);
	UE_LOG(LogAsdaBooster, Log,
		TEXT("[гача] подсистема поднята: окно 10050, барабан = 1 контрол (10068) на %d позиций"), kDrumSlots);
}

void UAsdaBoosterPanelSubsystem::Deinitialize()
{
	StopLoopStart();
	UnregisterZones();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kWindowId);
		}
	}
	WinWidget = nullptr;
	bReady = false;
	bWindowOpen = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaBoosterPanelSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Прокрутка бустера — сессия одного открытия: без сброса `bProcess`/`bSpinning` окно
	// осталось бы «в работе» и не приняло бы следующий запуск.
	StopLoopStart();
	ClosePrevWindow();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWindowId, false);
		}
	}
	bWindowOpen = false;
	bProcess    = false;
	bSpinning   = false;
	DrumIdx = 0;
	Decel = kDecelStart;
	DecelStep = 0;
	ResetDeadlineS = 0.0;
	ProgressStartS = 0.0;
	bProgressDone = false;
	BangStartS = 0.0;
	SelItemCode = 0; SelContainer = -1; SelSlot = -1;
	Packet68.Reset();
	bHavePacket = false;
	(void)Reason;
}

// ═══════════════════════════════ СОЗДАНИЕ И ПРИВЯЗКА ═══════════════════════════════════════════
bool UAsdaBoosterPanelSubsystem::EnsureWindow()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kWinZ).Ensure(GetGameInstance(), WinWidget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			UnregisterZones();
			WM.CloseWindow(kWindowId);
			WinWidget = nullptr;
			bReady = false;
			bWindowOpen = false;
		},
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			RegisterZones();
			// ★ ТРЕБОВАНИЕ ВЛАДЕЛЬЦА 2026-08-26: окно бустера ВСЕГДА поверх остальных окон (см. `kPriorityZ`).
			WM.SetWindowPriority(kWindowId, true);
		},
		[]()
		{
			UE_LOG(LogAsdaBooster, Error, TEXT("[гача] WBP_BOOSTING_WIN не загрузился (%s)"), kWbpPath);
		},
		[this](UAsdaWindowSubsystem&)
		{
			// Скрытие окна (шаг каркаса): .JUI Visible="0"
			bWindowOpen = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R);
}

void UAsdaBoosterPanelSubsystem::SetupControls()
{
	bReady = false;
	if (!WinWidget) { return; }
	auto Find = [this](const TCHAR* N) -> UWidget* { return AsdaUi::FindNodeByName(WinWidget, N, nullptr); };
	auto FindPfx = [this](const TCHAR* P) -> UWidget* { return AsdaUi::FindNodeByPrefix(WinWidget, P); };

	// Планы (не Text и не кнопки) импортёр называет `{idc}_{juiIdx}_{plane}` — ищем по ПРЕФИКСУ
	//   (закон `jui-importer-widget-naming`).
	// ★ Ручка перетаскивания окна: узел `_1_DRAG`, созданный импортёром из EVS{6→0x20003} контрола
	//   10051. Само перетаскивание делает `UAsdaWindowSubsystem` по мета-данным .JUI — здесь узел
	//   нужен только для дампа контролов (закон `full-structural-analysis-always`).
	BgDrag     = Find(TEXT("BOOSTER_BG_1_DRAG"));
	ChimneyBg  = FindPfx(TEXT("BOOSTER_CHIMNEY_BG_"));
	ClockBg    = FindPfx(TEXT("BOOSTER_CLOCK_BG_"));
	Chimney    = FindPfx(TEXT("BOOSTER_CHIMNEY_5"));
	Clock      = FindPfx(TEXT("BOOSTER_CLOCK_6"));
	Eye1       = FindPfx(TEXT("BOOSTER_EYE1_"));
	Eye2       = FindPfx(TEXT("BOOSTER_EYE2_"));
	Spots[0]   = FindPfx(TEXT("BOOSTER_SPOT1_"));
	Spots[1]   = FindPfx(TEXT("BOOSTER_SPOT2_"));
	Spots[2]   = FindPfx(TEXT("BOOSTER_SPOT3_"));
	Spots[3]   = FindPfx(TEXT("BOOSTER_SPOT4_"));
	Border     = FindPfx(TEXT("BOOSTER_BORDER_"));
	Question   = FindPfx(TEXT("BOOSTER_QUESTION_"));
	Bang       = FindPfx(TEXT("BOOSTER_BANG_"));
	// ★ Закон `ui-progressbar-animate-existing`: заливка — это ВТОРОЙ план (ES1) того же контрола,
	//   его и анимируем клипом, браш не подменяем. `_16_0` — подложка, `_16_1` — заливка.
	ProgressBase = Cast<UImage>(Find(TEXT("BOOSTER_PROGRESS_16_0")));
	ProgressFill = Cast<UImage>(Find(TEXT("BOOSTER_PROGRESS_16_1")));
	PrizeName    = Cast<UTextBlock>(Find(TEXT("BOOSTER_NAME")));
	DrumIcon     = Cast<UImage>(FindPfx(TEXT("BOOSTER_RESULT_")));
	BtnStart     = Cast<UButton>(Find(TEXT("BOOSTER_START")));
	BtnOk        = Cast<UButton>(Find(TEXT("BOOSTER_OK")));
	BtnClose     = Cast<UButton>(Find(TEXT("BOOSTER_CLOSE")));
	BtnPreview   = Cast<UButton>(Find(TEXT("BOOSTER_PreviewBtn")));
	CapStart     = Cast<UTextBlock>(Find(TEXT("BOOSTER_START_Text")));
	CapOk        = Cast<UTextBlock>(Find(TEXT("BOOSTER_OK_Text")));
	CapPreview   = Cast<UTextBlock>(Find(TEXT("BOOSTER_PreviewBtn_Text")));
	StartBtnBase = Find(TEXT("BOOSTER_StrartBtnBase_grp"));   // ★ опечатка «Strart» — так в .JUI
	ViewBtnBase  = Find(TEXT("BOOSTER_ViewBtnBase_grp"));
	OkBtnBase    = Find(TEXT("BOOSTER_OKBtnBase_grp"));

	// Подписи кнопок ставит КОД (`item_inventory.c:35584/35618/35620`), а не .JUI.
	UGameInstance* GI = GetGameInstance();
	auto SetCap = [GI](UTextBlock* T, int32 Loc)
	{
		if (!T) { return; }
		const FString S = AsdaUi::LocRaw(GI, Loc, /*bDebugFallback*/ false);
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
		T->SetAutoWrapText(false);   // закон `ui-autowrap-canvas-gotcha`
	};
	SetCap(CapStart,   kLocCapStart);
	SetCap(CapOk,      kLocCapOk);
	SetCap(CapPreview, kLocCapPreview);
	if (PrizeName) { PrizeName->SetAutoWrapText(false); }

	auto Bind = [this](UButton* Btn, int32 Index, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, Index, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	// ★ Волна 9 заточки: окно 10050 — процедура инвентаря (группа 100). `CGameWindow_OnCommand`, ветка 66561:
	//   СНАЧАЛА `boosterWnd_OnMessage` (у 10069/10070/10071 внутри — свой звук id 4, shop_trade.c:11939-11952),
	//   ПОТОМ безусловно id 5 (ui_core.c:47648-47649); у 10072 — только id 5. Порядок id4 → действие → id5 — как там.
	Bind(BtnStart,   10069, [this](int32) { OnStartButton();   AsdaUi::PlayInvenButtonSfx(GetGameInstance()); });
	Bind(BtnOk,      10070, [this](int32) { OnOkButton();      AsdaUi::PlayInvenButtonSfx(GetGameInstance()); });
	Bind(BtnClose,   10071, [this](int32) { OnCloseButton();   AsdaUi::PlayInvenButtonSfx(GetGameInstance()); });
	Bind(BtnPreview, 10072, [this](int32) { OnPreviewButton(); AsdaUi::PlayInvenButtonSfx(GetGameInstance()); });

	bReady = (DrumIcon != nullptr && BtnStart != nullptr && BtnClose != nullptr);
	UE_LOG(LogAsdaBooster, Log,
		TEXT("[гача] окно 10050 разобрано: барабан(10068)=%s прогресс=%s имя=%s START=%s OK=%s CLOSE=%s PREVIEW=%s"),
		DrumIcon ? TEXT("да") : TEXT("НЕТ"), ProgressFill ? TEXT("да") : TEXT("НЕТ"),
		PrizeName ? TEXT("да") : TEXT("НЕТ"), BtnStart ? TEXT("да") : TEXT("НЕТ"),
		BtnOk ? TEXT("да") : TEXT("НЕТ"), BtnClose ? TEXT("да") : TEXT("НЕТ"),
		BtnPreview ? TEXT("да") : TEXT("НЕТ"));
}

// ═══════════════════════════════ ЗОНЫ МЫШИ ═════════════════════════════════════════════════════
// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (баг игроков «во время открытия бустера камера не вращается ПКМ», волна booster-loot): зона окна —
//   девять планов рамки `BOOSTING_WIN_0_0…_0_8`, а не корень WBP. Корень — `CanvasPanel` на весь вьюпорт, поэтому прежняя зона
//   занимала ВЕСЬ экран: `IsCursorOverUi` в `OnRightPressed` считал любую точку «над окном», и ПКМ-орбита не стартовала, пока
//   окно барабана открыто (тот же дефект, что у окна 900 — шапка `AsdaUi::FramePlanesAbsRect`). Эталон камеру при бустере не
//   запирает: гейт `updatePlayerMovement` (math_geom.c:417) и `LocalPlayer_UpdateRender` (entity_char.c:7978) — только сцена
//   премиум-гачи, у WM_RBUTTONDOWN гейта нет (game_main.c:5843). Ключ 0 — прежняя зона по корню.
static ASDA_DEV_CVAR(int32, CVarBoosterZoneFrame,
	TEXT("asda.Booster.ZoneFrame"), 1,
	TEXT("1 — зона окна барабана бустера 10050 = рамка окна (клик/ПКМ/колесо вне рамки идут в мир, камера вращается); "
	     "0 — прежний порт: зона = корень WBP во весь экран (ПКМ-орбита не стартует, пока окно открыто)."),
	ECVF_Default);

bool UAsdaBoosterPanelSubsystem::GetWinRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bWindowOpen || !WinWidget) { return false; }
	if (CVarBoosterZoneFrame.GetValueOnGameThread() != 0)
	{
		return AsdaUi::FramePlanesAbsRect(WinWidget, TEXT("BOOSTING_WIN_0_%d"), 9, OutMin, OutMax);
	}
	return AsdaUi::WidgetAbsRect(WinWidget->GetRootWidget(), OutMin, OutMax);
}

void UAsdaBoosterPanelSubsystem::RegisterZones()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RegisterUiZone(kZoneId, FAsdaUiZoneRect::CreateUObject(this, &UAsdaBoosterPanelSubsystem::GetWinRect));
	}
}

void UAsdaBoosterPanelSubsystem::UnregisterZones()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kZoneId);
	}
}

// ★★★ ИСПРАВЛЕНО (живой прогон 13.08, лог `AsdaReborn.log` 13:04:55…13:05:20).
//   Было: любой контрол получал `SelfHitTestInvisible`. Для КНОПКИ это значит «видна, но кликов не
//   ловит» — закон `ui-button-hover-click-rule` требует у кнопок ровно `Visible`, а у подписей
//   `SelfHitTestInvisible`. Из-за общего режима крестик 10071, возвращаемый после результата
//   (`RefreshSelectedItemDisplay`), переставал нажиматься, а кнопка 10070 не показывалась вовсе.
// ★★★ ДЕДУПЛИКАЦИЯ 2026-09-02. Здесь была своя копия правила видимости
//   («`W->IsA<UButton>() ? Visible : SelfHitTestInvisible`»). Правило одно на весь порт —
//   `AsdaCommunityUi::ShownVisibilityFor` (`UI/AsdaCommunityFriendSubsystem.h`), откат
//   `asda.UI.InteractiveHitTest 0`.
//   ⚠ ПОВЕДЕНИЕ НЕ МЕНЯЕТСЯ: в разметке обоих окон подсистемы — `BOOSTING_WIN` (10050, 32 узла)
//   и `BOOSTER_PREV_WIN` (15600, 31 узел) — нет НИ ОДНОГО контрола со скином
//   `EditBox`/`CheckButton`/`ComboBox`, только `StaticWindow`/`Text`/`PushButton`/`RadioButton`/
//   `ResizeWindow`/`ProgressBar`. Кнопки получали `Visible` и раньше.
// ★ Волна 1 (14.09.2026, В3): общая точка `AsdaUi::SetShown` (то же правило по классу узла).
void UAsdaBoosterPanelSubsystem::SetVis(UWidget* W, bool bVisible)
{
	AsdaUi::SetShown(W, bVisible);
}

// ═══════════════════════════════ ЗВУК ══════════════════════════════════════════════════════════
void UAsdaBoosterPanelSubsystem::PlayLoopStart()
{
	StopLoopStart();
	UGameInstance* GI = GetGameInstance();
	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	if (!Snd) { return; }
	// Эталон: `Audio_Play3DSound(g_pSoundMgr, поз. игрока, "s_item_boosting01.wav", /*loop*/ 1)`.
	FVector Loc = FVector::ZeroVector;
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (APawn* P = PC->GetPawn()) { Loc = P->GetActorLocation(); }
		}
	}
	LoopSound = Snd->PlaySound3D(kSndOpen, Loc, 100.f, 10000.f, /*bLoop*/ true);
}

void UAsdaBoosterPanelSubsystem::StopLoopStart()
{
	// Эталон: `sub_9349C7(g_pSoundMgr, "s_item_boosting01.wav")` — остановка именно этого звука.
	if (LoopSound) { LoopSound->Stop(); LoopSound = nullptr; }
}

// ═══════════════════════════════ ★★★ ЗАПОЛНЕНИЕ БАРАБАНА ══════════════════════════════════════
// Порт `craftWnd_populateBoosterList@0xD1FBB0` (`item_enhance.c:1771-1845`) — имя ВРЁТ, к крафту
//   отношения не имеет. Эталон крутит 35 кандидатов по кругу, пока не наберёт 36 позиций.
void UAsdaBoosterPanelSubsystem::PopulateDrum(int32 ItemCode)
{
	FMemory::Memzero(DrumCodes, sizeof(DrumCodes));
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	// `GetBoosterPreview` уже делает эталонный резолв: строка `booster_list` по `DurationOrEnchant`,
	//   35 кодов, отсев нулевых (общая точка — своей копии не заводим).
	TArray<int32> Codes;
	IT->GetBoosterPreview(ItemCode, Codes);
	if (Codes.Num() == 0)
	{
		UE_LOG(LogAsdaBooster, Warning, TEXT("[гача] предмет %d: в booster_list нет ни одного кода приза"), ItemCode);
		return;
	}
	// ★★★ ДЕФЕКТ ОРИГИНАЛА, ВОСПРОИЗВОДИМ НАБЛЮДАЕМОЕ ПОВЕДЕНИЕ:
	//   сброс чистит массив циклом на 35 записей (`ui_core.c:48183-48206`), а заполнение и перебор
	//   идут на 36. 36-я позиция в оригинале лежит ровно на буфере пакета 5469 и «кодом предмета»
	//   там оказывается мусор, который ни одна из трёх таблиц не находит ⇒ на этом шаге иконка
	//   не перерисовывается — один «пустой» кадр за оборот. За границу массива мы НЕ читаем
	//   (это UB), а просто оставляем 36-ю позицию нулевой — визуально то же самое.
	for (int32 i = 0; i < kDrumFilled; ++i)
	{
		DrumCodes[i] = Codes[i % Codes.Num()];
	}
	DrumCodes[kDrumSlots - 1] = 0;
	UE_LOG(LogAsdaBooster, Log,
		TEXT("[гача] барабан заполнен: %d призов из booster_list разложены по %d позициям (36-я пуста — дефект оригинала)"),
		Codes.Num(), kDrumFilled);
}

// Порт `uiQuestSlotPanelResetFields@0xD1FFC0` (имя ВРЁТ — это сброс состояния бустера).
void UAsdaBoosterPanelSubsystem::ResetFields()
{
	bProcess = false;
	bSpinning = false;
	DrumIdx = 0;
	Decel = kDecelStart;
	DrumAlpha = 0;
	CloseDeadlineS = 0.0;
	ResetDeadlineS = 0.0;
	ProgressStartS = 0.0;
	bProgressDone = false;
	BangStartS = 0.0;
	FMemory::Memzero(DrumCodes, sizeof(DrumCodes));
}

// ★★★★★ РАСШИФРОВКА СДВИНУТЫХ ИМЁН ПОЛЕЙ (третий заход, 2026-08-14) — ЧИТАТЬ ПЕРЕД ПРАВКОЙ.
//   IDA дала полям `CInventoryWindow` имена `m_pBooster*` со СДВИГОМ НА 4 ПОЗИЦИИ (16 байт):
//   поле, названное «m_pBoosterX», физически хранит контрол, стоящий в порядке привязки
//   `InventoryWindow_InitControls` (`item_inventory.c:35490-35597`) на ЧЕТЫРЕ раньше.
//   Порядок привязки: 223→10050, 224→10051, 225→10052, 226→10053, 227→10054, 228→10055,
//   229→10056, 230→10057, 231→10058, 232→10059, 233→10060, 234→10061, 235→10062, 236→10063,
//   237→10064, 238→10065, 239→10066, 240→10067, 241→10068, 242→10073, 243→10069, 244→10075,
//   245→10070, 246→10074, 247→10072, 248→10071.
//   ДОКАЗАТЕЛЬСТВА (каждое проверяемо по телу функции, закон `false-negatives-method`):
//     1. `itemBoosterWnd_Open@0xD1F4A0` центрирует `m_pBoosterClockBG`, делает
//        `CClass_MapInsert(g_pWindowMgr, m_pBoosterClockBG)` и показывает его — это операции над
//        КОРНЕМ окна. Номинально «ClockBG» = idx 227, фактически обязано быть idx 223 = 10050. ⇒ −4.
//     2. Там же `CProgressBar_SetValue(m_pBoosterStart, 0)`, а в приёме 5469
//        `uiProgressBar_AnimStart(m_pBoosterStart, 9200)` (`network.c:17052`) — это ПРОГРЕСС-БАР.
//        Номинально «Start» = idx 243, фактически 239 = 10066. ⇒ −4.
//     3. `boosterShopHidePanelControls` через мис-каст берёт `cell[7].gap_54` и делает
//        `CProgressBar_SetValue` — при базе массива 0x100 и шаге 88 это ровно idx 239 = 10066.
//     4. `boosterApplyResult@0xD1F830` работает СЫРЫМИ индексами и показывает на закрытии
//        `v13[226]`/`v13[227]` = 10053/10054 — то же, что `ResetSelection` показывает под именами
//        «Eye1/Eye2» (номинально 230/231, фактически 226/227). ⇒ −4.
//     5. ФУНКЦИОНАЛЬНОЕ доказательство: показ приза висит на событии 24 с `cmdId == 10065`
//        (`ui_core.c:48103-48104`), а событие 24 анимплан шлёт В КОНЦЕ ПРОГОНА, сам себя погасив
//        (`ui_core.c:108899-108901`). Значит «взрыв» 10065 обязан быть ЗАПУЩЕН в `ResetSelection`.
//        При старом (сдвинутом) чтении `ResetSelection` его ГАСИЛ — и тогда приз не показался бы
//        НИКОГДА. Прежний заход поэтому и подменял триггер прогресс-баром.
//   Ниже все три функции переписаны по расшифрованным индексам.

// ★ Порт `boosterShopHidePanelControls@0xD200E0` (`shop_trade.c:12118-12148`).
//   Мис-каст `(CBoosterShop*)pInvenWnd`: массив `m_aBoosterCells` лежит с 0x100, шаг 88
//   (`gap_F8[88*i+4]`), поле `pSlot` = +0x04 ⇒ cell[7] = idx 218, cell[8] = idx 240. Дословно:
//       hide cell[7].pStamp(228=10055 CHIMNEY) · pStamp2(229=10056 CLOCK)
//       hide pDigit[2..5] (232…235 = SPOT1…SPOT4)
//       CControl_CloseHide(pDigit[8] = 238 = 10065 BANG)   ← остановить «взрыв»
//       hide pDigit[6] (236 = 10063 BORDER)
//       CProgressBar_SetValue(gap_54 = 239 = 10066, 0); SetVisible(10066, 1)  ← бар ВИДЕН и пуст
//       SetText(cell[8].gap_00 = 240 = 10067 NAME, ""); hide 10067
//       SetText/DrawTextureRect/SetColor4 на pSlot (241 = 10068 RESULT) — иконка в «пусто», альфа 255
//       hide pName (242 = 10073 StartBtnBase) · pPur (244 = 10075 OKBtnBase) · gap_18 (246 = 10074 ViewBtnBase)
void UAsdaBoosterPanelSubsystem::HidePanelControls()
{
	SetVis(Chimney, false);      // 10055
	SetVis(Clock,   false);      // 10056
	for (UWidget* S : Spots) { SetVis(S, false); }   // 10059…10062
	SetVis(Bang,   false);       // 10065 — `CControl_CloseHide`
	SetVis(Border, false);       // 10063
	// ★ Прогресс-бар эталон здесь именно ПОКАЗЫВАЕТ с нулевым значением, а не прячет.
	SetVis(ProgressBase, true);
	SetVis(ProgressFill, true);
	if (ProgressFill)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(ProgressFill))
		{
			FMargin M = S->GetOffsets(); M.Right = 0.f; S->SetOffsets(M);
		}
	}
	if (PrizeName) { PrizeName->SetText(FText::GetEmpty()); }
	SetVis(PrizeName, false);    // 10067
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ClearIcon(DrumIcon);
	}
	if (DrumIcon) { DrumIcon->SetRenderOpacity(1.f); }
	DrumAlpha = 255;
	// ★★★ Три подложки кнопок. В .JUI сами кнопки — ДЕТИ подложек (10069 в 10073, 10070 в 10075,
	//   10072 в 10074), и эталон гасит родителя. UMG видимость вниз НЕ наследует, поэтому гасим
	//   и подложку, и кнопку, и подпись (та же причина, что была разобрана для кнопки 10070).
	SetVis(StartBtnBase, false); SetVis(BtnStart,   false); SetVis(CapStart,   false);
	SetVis(OkBtnBase,    false); SetVis(BtnOk,      false); SetVis(CapOk,      false);
	SetVis(ViewBtnBase,  false); SetVis(BtnPreview, false); SetVis(CapPreview, false);
	ProgressStartS = 0.0;
	bProgressDone = false;
	BangStartS = 0.0;
}

// ★ Порт `boosterShopShowPanelControls@0xD1FEB0` (`shop_trade.c:12098-12115`) — КЛИК «НАЧАТЬ».
//   Дословно по расшифрованным индексам:
//       hide cell[8].pName (242 = 10073 BOOSTER_StrartBtnBase)  ← ★ ЭТО И ЕСТЬ «кнопка Начать скрывается»
//       hide *(cell[8].gap_18) (246 = 10074 BOOSTER_ViewBtnBase)
//       show cell[7].pStamp (228 = 10055 CHIMNEY «вкл») · pStamp2 (229 = 10056 CLOCK «вкл»)
//       show pDigit[2..5] (232…235 = SPOT1…SPOT4)
//       DrawTextureRect(pSlot 241 = 10068, пусто); SetColor4(10068, 0xFF, 0); SetVisible(10068, 1)
void UAsdaBoosterPanelSubsystem::ShowPanelControls()
{
	// ★★★ Кнопка «Начать» (10069) и её подложка (10073) — гаснут по клику. Так в оригинале.
	SetVis(StartBtnBase, false); SetVis(BtnStart,   false); SetVis(CapStart,   false);
	// Кнопка «Вид» (10072) и её подложка (10074) — тоже.
	SetVis(ViewBtnBase,  false); SetVis(BtnPreview, false); SetVis(CapPreview, false);
	SetVis(Chimney, true);       // 10055 труба «вкл»
	SetVis(Clock,   true);       // 10056 часы «вкл»
	for (UWidget* S : Spots) { SetVis(S, true); }   // 10059…10062 — все четыре лампы
	// Эталон сбрасывает иконку ячейки барабана в пустую и ставит альфу 255 (`SetColor4(0xFF, 0)`).
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ClearIcon(DrumIcon);
	}
	if (DrumIcon) { DrumIcon->SetRenderOpacity(1.f); }
	DrumAlpha = 255;
	SetVis(DrumIcon, true);      // 10068 — контрол ячейки появляется именно здесь
}

// ★ 05.11.2026 [ОРИГИНАЛ] (баг игроков «несколько бустеров подряд», волна booster-loot): `itemBoosterWnd_Open` последней
//   строкой зовёт `sowelWindowClose(pBoosterWnd, 1)` (`item_inventory.c:43103`; мисноним — это ЗАКРЫТИЕ СУМКИ, тело
//   `item_inventory.c:35780`, тот же вызов портирован у разборки 6502). Порт сумку не закрывал — второй бустер оставался
//   под рукой. 0 — прежний порт (сумка остаётся открытой).
static ASDA_DEV_CVAR(int32, CVarBoosterOpenClosesBag,
	TEXT("asda.Booster.OpenClosesBag"), 1,
	TEXT("1 — как оригинал: открытие окна барабана бустера 10050 закрывает сумку (`sowelWindowClose`); 0 — прежний порт: сумка остаётся."),
	ECVF_Default);

// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (тот же баг): повторный вход в `OpenWindow`, пока у открытого барабана идёт процесс (5468 ушёл) или
//   лежит неприменённый 5469, ОБНУЛЯЛ буфер пакета и выбранную ячейку — приз первого бустера не ложился в сумку, потраченный бустер
//   оставался в модели («призрак»), ячейка приза пустела до перезахода. У эталона буфер так же затирается, но до него не дойти:
//   сумка закрыта (`sowelWindowClose`), её двойной щелчок и хотбар заперты полем 254 (13106/13105). Здесь — последняя защита
//   для любого входа: такой вызов отклоняется, состояние первого бустера не трогается. 0 — прежний порт.
static ASDA_DEV_CVAR(int32, CVarBoosterBusyReopenGuard,
	TEXT("asda.Booster.BusyReopenGuard"), 1,
	TEXT("1 — открытие барабана бустера, пока у открытого окна идёт процесс или лежит неприменённый результат 5469, отклоняется "
	     "(приз первого не теряется); 0 — прежний порт: окно перезапускается и буфер результата обнуляется."),
	ECVF_Default);

// ═══════════════════════════════ ★★★ ОТКРЫТИЕ ══════════════════════════════════════════════════
// Полный порт `itemBoosterWnd_Open@0xD1F4A0` (`item_inventory.c:43043-43110`).
bool UAsdaBoosterPanelSubsystem::OpenWindow(int32 ItemCode, int32 ContainerId, int32 CellIndex)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	if (bWindowOpen && (bProcess || bHavePacket) && CVarBoosterBusyReopenGuard.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaBooster, Log,
			TEXT("[гача] открытие бустера %d (%d:%d) отклонено: барабан занят бустером %d (%d:%d), процесс=%d результат=%d "
			     "(asda.Booster.BusyReopenGuard 1)"),
			ItemCode, ContainerId, CellIndex, SelItemCode, SelContainer, SelSlot, bProcess ? 1 : 0, bHavePacket ? 1 : 0);
		return false;
	}
	// Гейт 1: запись `item_list`. Гейт 2: строка `booster_list` по `DurationOrEnchant`.
	//   Оба — «return 0», и вызывающий уходит в обычное применение предмета.
	if (!IT->GetItem(ItemCode))      { return false; }
	if (!IT->IsBoosterItem(ItemCode)) { return false; }
	if (!EnsureWindow() || !bReady)   { return false; }

	SelItemCode  = ItemCode;
	SelContainer = ContainerId;
	SelSlot      = CellIndex;

	ResetFields();          // `uiQuestSlotPanelResetFields`
	HidePanelControls();    // `boosterShopHidePanelControls`
	PopulateDrum(ItemCode); // `craftWnd_populateBoosterList`
	Packet68.Reset();
	bHavePacket = false;

	// ★★★ Эталонный набор видимостей при открытии (`item_inventory.c:43085-43095`), с расшифровкой
	//   сдвинутых имён (см. шапку `HidePanelControls`):
	//       sub_9391AC(m_pBoosterResult → 237 = 10064 QUESTION); gap_3F0[24] = альфа; SetVisible(10064, 1)
	//       sub_9391AC(m_pBoosterName   → 236 = 10063 BORDER);   gap_3F0[28] = альфа   ← ТОЛЬКО чтение
	//       SetVisible(m_pBoosterViewBtnBase → 242 = 10073 StartBtnBase, 1)
	//       SetVisible(gap_3D4[12] = 248 = 10071 CLOSE, 1)         ← сырой индекс
	//       SetVisible(m_pBoosterEye1 → 226 = 10053 CHIMNEY_BG, 1)
	//       SetVisible(m_pBoosterEye2 → 227 = 10054 CLOCK_BG,   1)
	//       SetVisible(gap_3D4[4]  = 246 = 10074 ViewBtnBase, 1)   ← сырой индекс
	//       CProgressBar_SetValue(m_pBoosterStart → 239 = 10066 PROGRESS, 0)
	//   ★ Чего в открытии НЕТ: показа рамки 10063 (её погасил `HidePanelControls` и зажжёт только
	//     остановка барабана) и показа ячейки 10068 (её зажигает клик START). Прежний порт показывал
	//     обе — из-за этого «?» соседствовал с пустой рамкой и пустой ячейкой.
	//   ★ Глаза 10057/10058 эталон при открытии не трогает вовсе: в .JUI у них Visible="1",
	//     и НИ ОДНА функция окна их не переключает (сдвиг имён «Eye1/Eye2» указывает на 10053/10054).
	SetVis(Question, true);       // 10064 «?»
	SetVis(Border,   false);      // 10063 — до остановки барабана скрыта
	SetVis(Eye1,     true);       // 10057 — .JUI Visible="1", кодом не управляется
	SetVis(Eye2,     true);       // 10058
	SetVis(ChimneyBg, true);      // 10053
	SetVis(ClockBg,   true);      // 10054
	SetVis(StartBtnBase, true); SetVis(BtnStart,   true); SetVis(CapStart,   true);   // 10073 + 10069
	SetVis(ViewBtnBase,  true); SetVis(BtnPreview, true); SetVis(CapPreview, true);   // 10074 + 10072
	SetVis(BtnClose,     true);   // 10071
	SetVis(OkBtnBase,    false);  // 10075 — до результата скрыт
	SetVis(BtnOk,        false);  // ★★★ и сама кнопка 10070 с подписью (в WBP `Collapsed`)
	SetVis(CapOk,        false);
	SetVis(DrumIcon,     false);  // 10068 — .JUI Visible="0"; появится по клику START
	if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>()) { Inv->ClearIcon(DrumIcon); }
	// Прогресс-бар: диапазон 0..100, значение 0 (`item_inventory.c:35625`, `43095`); сам бар виден
	//   (его показал `HidePanelControls`, в .JUI у 10066 тоже Visible="1").
	if (ProgressFill)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(ProgressFill))
		{
			// Только ШИРИНА: Left не трогаем, чтобы не спорить со сдвигами перетаскивания
			//   (закон `ui-window-drag-hittest`) — заливка живёт внутри группы фона и едет с ней.
			FMargin M = S->GetOffsets(); M.Right = 0.f; S->SetOffsets(M);
		}
	}

	// Центрирование окна на экране (`CConfig_GetWidth/Height >> 1`).
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kWindowId, true);
		WM->BringWindowToFront(kWindowId);
	}
	// ⚠ Импортёр РАСПЛЮЩИЛ иерархию .JUI — все узлы окна лежат сёстрами в корневом канвасе с
	//   абсолютными координатами базы 1024×768. «Подвинуть окно» = сдвинуть ВСЕ узлы корня на одну
	//   дельту (тот же способ, что у меню хвастовства `PlaceMenuAt`); `SetPositionInViewport` здесь
	//   неприменим — виджет растянут на весь вьюпорт. Заодно так сдвиг складывается с
	//   перетаскиванием (закон `ui-window-drag-hittest`), а не конфликтует с ним.
	if (UCanvasPanel* Root = Cast<UCanvasPanel>(WinWidget ? WinWidget->GetRootWidget() : nullptr))
	{
		UWidget* Ref = AsdaUi::FindNodeByName(WinWidget, TEXT("BOOSTING_WIN_0_0"), nullptr);
		UCanvasPanelSlot* SRef = Ref ? AsdaUi::SlotOf(Ref) : nullptr;
		// ★ Дедуп №2, В1-B (U16, 11.10.2026): общая точка `AsdaUi::CenterRootOnViewport` (окно 266×266; тот же расчёт и сдвиг).
		AsdaUi::CenterRootOnViewport(Root, SRef, FVector2D(266.f, 266.f), GetWorld());
	}
	bWindowOpen = true;
	++CntOpen;
	PlayLoopStart();   // `s_item_boosting01.wav`, зациклённый
	// `sowelWindowClose(pBoosterWnd, 1)` — закрыть сумку (ключ `asda.Booster.OpenClosesBag`).
	if (CVarBoosterOpenClosesBag.GetValueOnGameThread() != 0)
	{
		if (UAsdaInventorySubsystem* InvB = GI->GetSubsystem<UAsdaInventorySubsystem>()) { if (InvB->IsWindowOpen()) { InvB->CloseWindow(); } }
	}

	UE_LOG(LogAsdaBooster, Log,
		TEXT("[гача] окно 10050 открыто: бустер %d («%s»), слот %d:%d"),
		ItemCode, *IT->GetItemName(ItemCode), ContainerId, CellIndex);
	return true;
}

// ═══════════════════════════════ КНОПКИ ════════════════════════════════════════════════════════
// ★ 10069 START. Порт `shop_trade.c:11939-11947` + `boosterShopUseOrClose@0xD22250`.
void UAsdaBoosterPanelSubsystem::OnStartButton()
{
	if (!bWindowOpen) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(4, false);   // эталонный `csoundPlayName2DStore(g_pSoundMgr, 4, 0)`
	}
	// ★ Дефект оригинала: повторный клик во время процесса НЕ блокируется. Защита — по CVar.
	if (bProcess && CVarBoosterGuardStart.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaBooster, Log, TEXT("[гача] START во время процесса подавлен (asda.Booster.GuardStart 1)"));
		return;
	}
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// Единственная эталонная проверка: существует ли запись предмета. Ни денег, ни веса, ни
	//   «уже крутится» клиент не проверяет — всё это решает сервер кодами 1…8.
	if (!IT || !Net || !IT->GetItem(SelItemCode))
	{
		UE_LOG(LogAsdaBooster, Log, TEXT("[гача] START: записи предмета %d нет — эталон здесь закрывает окно"), SelItemCode);
		ApplyResultAndClose();
		return;
	}
	if (!Net->SendGachaItemUse5468(SelItemCode, (uint8)SelContainer, SelSlot))
	{
		UE_LOG(LogAsdaBooster, Warning, TEXT("[гача] START: 5468 не ушёл"));
		return;
	}
	++CntStart;
	bProcess = true;                                   // `set_oFF(1)`
	// ★★★ `boosterShopShowPanelControls` СКРЫВАЕТ кнопку «Начать» (10069 + подложка 10073) и кнопку
	//   «Вид» (10072 + подложка 10074) и зажигает трубу/часы/лампы — см. её тело.
	ShowPanelControls();
	CloseDeadlineS = FPlatformTime::Seconds();         // пуск 30-секундного таймаута
	// Эталонный хвост `case 0x2755u` (`shop_trade.c:11945-11946`):
	//   `SetVisible(v13[248] = 10071 CLOSE, 0); SetVisible(v13[246] = 10074 ViewBtnBase, 0);`
	SetVis(BtnClose, false);                           // 10071 скрыт на время процесса
	UE_LOG(LogAsdaBooster, Log,
		TEXT("[гача] START: 5468 отправлен (бустер %d, слот %d:%d), таймаут закрытия %d мс пущен"),
		SelItemCode, SelContainer, SelSlot, kCloseTimeoutMs);
}

// ★ 10070 OK. Порт `OnRecv_ItemEnchantResult@0xD20B30` (имя ВРЁТ — это обработчик кнопки OK).
void UAsdaBoosterPanelSubsystem::OnOkButton()
{
	if (!bWindowOpen) { return; }
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(4, false);
	}
	// ★★★ ВОССТАНОВЛЕНО (2026-08-13). Эталон `OnRecv_ItemEnchantResult@0xD20B30`
	//   (`shop_trade.c:12405-12412`) идёт в таком порядке:
	//       OnRecv_RemoveSlot_byPtr(this, {контейнер +2474, слот +2475});  // снять потраченную гачу
	//       CItemManager_NotifyItemInfo(this, this + 2477);                // ← АНОНС ПО ПРИЗУ
	//       OnRecv_ItemUpdate(this, this + 2477);                          // положить приз
	//   Вызов `CItemManager_NotifyItemInfo@0xD0C8F0` в порту ОТСУТСТВОВАЛ: приз гачи не давал ни
	//   своего HERO_NOTICE (loc 20250), ни C→S 6616 «объяви всем», ни C→S 6129 «покажи предмет
	//   окружающим». Кнопка «Подтвердить» — ЕДИНСТВЕННОЕ место ветки гачи, где эталон анонсирует:
	//   ни приём 5469 (`Recv_BoosterShopResult_0@0xCFF9F0`), ни закрытие окна
	//   (`boosterApplyResult@0xD1F830`) `NotifyItemInfo` не зовут — проверено по бинарю (четыре
	//   и только четыре `call` на трамплин `0x92B273`: 0xB70874, 0xD11228, 0xD20B8E, 0xFE7538).
	//   Аргумент эталона — УКАЗАТЕЛЬ НА ЗАПИСЬ ПРИЗА (пакет +9): код = запись+0, контейнер =
	//   запись+4 (пакет +13), слот = запись+5 (пакет +14) — последние два уходят в 6129.
	//   Фильтр «грейд ≥ 3 И (тип ≤ 9 или 11/200)» живёт внутри `NotifyItemInfo`, как в оригинале.
	if (bHavePacket && Packet68.Num() >= 68)
	{
		if (UAsdaHudSubsystem* Hud = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>() : nullptr)
		{
			const uint8* Prize = Packet68.GetData() + 9;
			int32  PrizeCode = 0;
			int16  PrizeSlot = 0;
			FMemory::Memcpy(&PrizeCode, Prize + 0, sizeof(int32));
			FMemory::Memcpy(&PrizeSlot, Prize + 5, sizeof(int16));
			const int32 PrizeCont = (int32)Prize[4];
			Hud->NotifyItemInfo(PrizeCode, /*bForce=*/false, PrizeCont, (int32)PrizeSlot);
		}
	}
	// Применение приза + эффект БЕЗ звука + нотис 20066/20067 + отправка 5474 — всё в общей точке
	//   инвентаря (закон `port-shared-points`: своей копии выдачи предмета не заводим).
	ApplyPrize(/*bSend5474*/ true);
	// Эталон обнуляет дескриптор и буфер пакета, затем закрывает окно.
	Packet68.Reset();
	bHavePacket = false;
	SelItemCode = 0; SelContainer = -1; SelSlot = -1;
	ApplyResultAndClose();
}

// ★ 10071 CLOSE. Порт `boosterApplyResult@0xD1F830` — имя `tagBoosterShop_CloseWindow` ВРЁТ:
//   это не «закрыть», а «ПРИМЕНИТЬ РЕЗУЛЬТАТ и закрыть».
void UAsdaBoosterPanelSubsystem::OnCloseButton()
{
	if (!bWindowOpen) { return; }
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(4, false);
	}
	ApplyResultAndClose();
}

// ★ 10072 PREVIEW. Порт `shop_trade.c:11956-11965`: показать список возможных призов.
//   Эталон открывает `g_pHoverTooltipWnd` со списком; отдельного окна hover-подсказки в порту нет,
//   поэтому состав барабана выводим в ОБЩУЮ ПЛАШКУ ПОДСКАЗКИ (`ui-shared-tip-ownership`,
//   владелец `Booster`) — тот же способ, которым эталон показывает отказы этого окна.
void UAsdaBoosterPanelSubsystem::OnPreviewButton()
{
	if (!bWindowOpen) { return; }
	// ★★★ ИСПРАВЛЕНО (дефект живого прогона «окно "Вид" просмотра не работает, не отображается»:
	//   в логе 13.08 кнопка нажата ШЕСТЬ раз подряд и каждый раз порт только писал строку в лог).
	//   Было: состав барабана выводился текстом в ОБЩУЮ ПЛАШКУ ПОДСКАЗКИ — на экране не появлялось
	//   ничего. Оригинал открывает ОТДЕЛЬНОЕ ОКНО `BOOSTER_PREV_WIN` (15600) с иконками призов —
	//   `boosterWnd_OnMessage@0xD16ED0`, `case 0x2758u` → `boosterWindowPopulate@0xB82D90`.
	// ★ 31.10.2026: эталон перед заполнением зовёт `uiStorageWindow_ResetState` (страница 0, флаг `+697` = 0 —
	//   щелчок по ячейкам «Вида» из барабана мёртв) — общий вход `OpenPrevWindowFrom`.
	OpenPrevWindowFrom(SelItemCode, EAsdaPrevSource::Drum, /*bCellsActive*/ false);
}

// ★★★ 31.10.2026 ОТКРЫТИЕ «ВИДА» ИЗ ЛЮБОГО ОКНА — порт тройки `uiStorageWindow_ResetState` → `boosterWindowPopulate(…, 1, код)` →
//   (`set_o2B9(…, 1)`). Входы эталона (все вызовы `boosterWindowPopulate` с `a2 = 1`, `AsdaStory_game.c`):
//     барабан 10072            `boosterWnd_OnMessage@0xD16ED0` :346522   ResetState, флаг 0, окно справа от барабана;
//     премиум-магазин, 58905   `uiHandleCommand@0xC23AC0`      :265921   ResetState, флаг 1, окно НЕ двигается;
//     премиум-магазин, карточка `itemOnUseDispatch@0xC267E0`   :266889   ResetState, флаг 1, окно НЕ двигается;
//     окно набора 26500         `invItemUseOrBooster@0xDB36D0`  :405615   ResetState, флаг 1 только при видимом 26000
//                               (его эталон не показывает никогда ⇒ 0), окно стыкуется справа от 26500;
//     магазин бустеров 15300    `uiShopDialogCommandHandler` 15547, `uiInventorySlotUseItem` (иконка карточки):
//                               ResetState НЕ зовут, но страницу 662 = 0 ставят сами; флаг остаётся 1 от открытия магазина.
//   ⚠ Из сброса `uiStorageWindow_ResetState@0xB83930` к окну 15600 относятся только поля 662/697/698; остальные — поля
//   магазина бустеров 15300, у порта это отдельная подсистема (`UAsdaBoosterShopSubsystem`), их не трогаем.
bool UAsdaBoosterPanelSubsystem::OpenPrevWindowFrom(int32 ItemCode, EAsdaPrevSource Source, bool bCellsActive)
{
	if (CVarBoosterPrevOpenReset.GetValueOnGameThread() != 0)
	{
		PrevPage     = 0;          // `v62[662] = 0`
		PrevItemCode = 0;          // `v62[698] = −1`
	}
	PrevSource       = Source;
	bPrevCellsActive = bCellsActive;   // `v62[697]`: сброс → 0, у премиум-магазина затем `set_o2B9(…, 1)`
	bPrevPlaceCanvasPending = false;   // старая заявка стыковки к новому открытию не относится
	PrevOpenFrame = GFrameCounter;
	PopulatePrevWindow(ItemCode);
	const bool bShown = bPrevVisible && PrevItemCode == ItemCode;
	UE_LOG(LogAsdaBooster, Log, TEXT("[гача] «Вид» 15600: открытие из %s, бустер %d, щелчок по ячейкам %s — %s"),
		Source == EAsdaPrevSource::Drum ? TEXT("барабана") : Source == EAsdaPrevSource::BoosterShop ? TEXT("магазина бустеров")
			: Source == EAsdaPrevSource::PremiumShop ? TEXT("премиум-магазина") : TEXT("окна состава набора"),
		ItemCode, bCellsActive ? TEXT("жив") : TEXT("мёртв"), bShown ? TEXT("показано") : TEXT("НЕ показано (нет состава)"));
	return bShown;
}

// ★ 31.10.2026 порт `CControl_SetField74_75(g_pHoverTooltipWnd, x, y)` из `positionHoverTooltipWnd@0xDB3900`.
void UAsdaBoosterPanelSubsystem::PlacePrevAtCanvas(const FVector2D& TopLeft)
{
	PrevPlaceCanvasWant     = TopLeft;
	bPrevPlaceCanvasPending = true;
	bPrevPlaceAbsPending    = false;   // абсолютная заявка магазина бустеров к этой постановке не относится
	if (PrevWidget) { PlacePrevWindow(); }
}

// ★ 31.10.2026: ячейка окна «Вид» под курсором — та же пара таблиц эталона, что у подсказки (`TickPrevTooltip`).
int32 UAsdaBoosterPanelSubsystem::PrevCellUnderCursor(int32& OutCode) const
{
	OutCode = 0;
	for (int32 i = 0; i < PrevSpecial.Num(); ++i)
	{
		const int32 Code = PrevSpecialCode.IsValidIndex(i) ? PrevSpecialCode[i] : -1;
		if (Code > 0 && PrevSpecial[i] && AsdaUi::IsCursorOverWidget(PrevSpecial[i])) { OutCode = Code; return i; }
	}
	for (int32 i = 0; i < PrevNormal.Num(); ++i)
	{
		const int32 Code = PrevNormalCode.IsValidIndex(i) ? PrevNormalCode[i] : -1;
		if (Code > 0 && PrevNormal[i] && AsdaUi::IsCursorOverWidget(PrevNormal[i])) { OutCode = Code; return 5 + i; }
	}
	return -1;
}

// ★ 31.10.2026 щелчок по ячейке «Вида»: событие 0xB `boosterShopOnMessage@0xB876F0` (`AsdaStory_game.c:211851`) →
//   `uiInventorySlotUseItem@0xB86EE0` (:211385). Гейт эталона — `if (a1[697])` (флаг `bPrevCellsActive`). Нажатие, накрытое
//   модальным окном или окном выше «Вида», до ячейки не доходит (диспетч мыши менеджера окон, `AsdaUi::IsModalDispatchBlocked`).
//   ⚠ Опрос мыши идёт КАЖДЫЙ кадр и при скрытом окне (см. `Tick`): нажатие, которое само открыло «Вид» (клик по карточке
//   премиум-магазина), в следующем кадре уже «удержано» и вторым щелчком по ячейке под курсором не считается.
void UAsdaBoosterPanelSubsystem::TickPrevCellClick()
{
	PrevMouse.Update();
	if (!bPrevVisible || !PrevMouse.bLmbDown || !bPrevCellsActive || PrevOpenFrame == GFrameCounter) { return; }
	UGameInstance* GI = GetGameInstance();
	if (!GI || AsdaUi::IsModalDispatchBlocked(GI)) { return; }
	if (const UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		if (WM->IsPointerBlockedByTopmost(kPrevWindowId) || WM->IsCursorOverHigherWindow(kPrevWindowId)) { return; }
	}
	int32 Code = 0;
	if (PrevCellUnderCursor(Code) < 0) { return; }
	OnPrevCellClicked(Code);
}

// ★ 31.10.2026 ветки «особые» (`cmdToHairIndex5`) и «обычные» (`invLookupSpecialItemIndex`) `uiInventorySlotUseItem@0xB86EE0`:
//     запись `item_list` есть:  подтип 200/201 и `DurationOrEnchant > 0` (набор) → `stampWnd_PopulateItemSlots(код, 0, 1)` —
//                               окно состава 26500 в режиме превью, 4-й аргумент 1 = стыковка с «Видом»;
//                               иначе → `invAssignMountSlotItem@0xB86AA0` (примерка на своём персонаже магазина бустеров);
//     только `special_itemlist`, подтип 15 → `charApplyRidingParts@0xB86CC0` (примерка маунта).
//   [ПОРТ] Примерки магазина бустеров в порту нет ни у самого магазина 15300 (`uiInventorySlotUseItem` для его иконок),
//   ни здесь — это отдельная система (`charRebuildMountPartsModel` / `charApplyMountEquipSlots`); ветка пишет причину в журнал.
void UAsdaBoosterPanelSubsystem::OnPrevCellClicked(int32 ItemCode)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	if (const FAsdaItemRecord* R = IT->GetItem(ItemCode))
	{
		if ((R->ItemSubType == 200 || R->ItemSubType == 201) && R->DurationOrEnchant > 0)
		{
			UAsdaPackagePanelSubsystem* Pkg = GI->GetSubsystem<UAsdaPackagePanelSubsystem>();
			const bool bOk = Pkg && Pkg->Populate(ItemCode, /*StampMode*/ 0, /*SrcContainer*/ -1, /*SrcSlot*/ -1, /*bShowTooltip*/ true);
			UE_LOG(LogAsdaBooster, Log, TEXT("[гача] «Вид»: щелчок по набору %d → окно состава 26500 %s"),
				ItemCode, bOk ? TEXT("открыто рядом с «Видом»") : TEXT("НЕ открыто (нет строки packagetable)"));
			return;
		}
		UE_LOG(LogAsdaBooster, Log,
			TEXT("[гача] «Вид»: щелчок по предмету %d — эталон надевает его на своего персонажа (invAssignMountSlotItem@0xB86AA0); "
			     "примерки магазина бустеров в порту нет"), ItemCode);
		return;
	}
	FAsdaSpecialItemlistRow Sp;
	if (IT->GetSpecialItem(ItemCode, Sp) && Sp.SubType == 15)
	{
		UE_LOG(LogAsdaBooster, Log,
			TEXT("[гача] «Вид»: щелчок по маунту %d — эталон сажает своего персонажа на него (charApplyRidingParts@0xB86CC0); "
			     "примерки магазина бустеров в порту нет"), ItemCode);
	}
}

// ═══════════════ ★★★ ОКНО «ВИД» 15600 `BOOSTER_PREV_WIN` ═══════════════════════════════════════
bool UAsdaBoosterPanelSubsystem::EnsurePrevWindow()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kPrevWindowId, kPrevWbpPath, kPrevZ).Ensure(GetGameInstance(), PrevWidget, bPrevReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.CloseWindow(kPrevWindowId);
			PrevWidget = nullptr;
			bPrevReady = false;
			bPrevVisible = false;
		},
		[this](UAsdaWindowSubsystem& WM)
		{
			// ★ Окно предпросмотра бустера — та же полоса приоритета; внутри полосы оно ляжет ПОВЕРХ
			//   окна барабана, потому что открывается позже (см. `SetWindowPriority`).
			WM.SetWindowPriority(kPrevWindowId, true);
			SetupPrevControls();
			// В .JUI окно 15600 имеет `Visible = "0"` — до заполнения его никто не показывает.
		},
		[]()
		{
			UE_LOG(LogAsdaBooster, Error, TEXT("[гача] WBP_BOOSTER_PREV_WIN не загрузился (%s)"), kPrevWbpPath);
		},
		[this](UAsdaWindowSubsystem&)
		{
			bPrevVisible = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bPrevReady;
}

void UAsdaBoosterPanelSubsystem::SetupPrevControls()
{
	bPrevReady = false;
	if (!PrevWidget) { return; }
	auto Find = [this](const TCHAR* N) { return AsdaUi::FindNodeByName(PrevWidget, N, nullptr); };
	// Иконки — это ПЛАНЫ контролов (импортёр называет их `{idc}_{juiIdx}_{plane}`), поэтому ищем
	//   по префиксу: `BOOSTER_PREV_SITEM00_` → `BOOSTER_PREV_SITEM00_26_0` и т. д.
	auto FindPfx = [this](const FString& Pfx) -> UImage*
	{
		return Cast<UImage>(AsdaUi::FindNodeByPrefix(PrevWidget, *Pfx));
	};

	PrevSpecial.Reset(); PrevSpecial.SetNum(5);
	for (int32 i = 0; i < 5; ++i)
	{
		PrevSpecial[i] = FindPfx(FString::Printf(TEXT("BOOSTER_PREV_SITEM%02d_"), i));
	}
	PrevNormal.Reset(); PrevNormal.SetNum(15);
	for (int32 i = 0; i < 15; ++i)
	{
		// idc идут от `BOOSTER_PREV_ITEM05` до `BOOSTER_PREV_ITEM19` — то есть номер = 5 + i.
		PrevNormal[i] = FindPfx(FString::Printf(TEXT("BOOSTER_PREV_ITEM%02d_"), 5 + i));
	}
	PrevName    = Cast<UTextBlock>(Find(TEXT("BOOSTER_PREV_NAME")));
	PrevClose   = Cast<UButton>(Find(TEXT("BOOSTER_PREV_WIN_CLOSE")));
	if (PrevName) { PrevName->SetAutoWrapText(false); }   // закон `ui-autowrap-canvas-gotcha`

	// ★★★ ИСПРАВЛЕНО (замечание живого прогона: «стрелки и значок `<` видны при одной странице»).
	//   Эталон гасит ОДИН контрол 15644 (`CControl_SetVisible(v62[661], count > 15)`), но импортёр
	//   развернул его в ПЯТЬ узлов: `BOOSTER_PREV_PAGE_22_0` (план «<»), `_22_1` (план «>»),
	//   `_OPT0`/`_OPT1` (кнопки-опции RadioButton) и `_Text` (собственный `CaptionText="<"` контрола,
	//   рисуемый по центру всей полосы 340×18). Прежний порт искал узел `BOOSTER_PREV_PAGE_22`,
	//   которого в WBP НЕТ, — поэтому не гасил ничего. Собираем все узлы по префиксу.
	PrevPageParts.Reset();
	PrevPageBtns.Reset();
	if (UWidgetTree* Tree = PrevWidget->WidgetTree)
	{
		Tree->ForEachWidget([this](UWidget* W)
		{
			if (!W) { return; }
			if (W->GetName().StartsWith(TEXT("BOOSTER_PREV_PAGE")))
			{
				PrevPageParts.Add(W);
				if (UButton* B = Cast<UButton>(W)) { PrevPageBtns.Add(B); }
			}
		});
	}
	// Порт `case 15644` сообщения 0x10501 (`shop_trade.c:1511-1519`): выбранный индекс RadioButton
	//   кладётся в `v62[662]` и окно перезаполняется. Опций ровно две ⇒ индекс == номер страницы.
	PrevPageBtns.Sort([](const UButton& A, const UButton& B) { return A.GetName() < B.GetName(); });
	for (int32 i = 0; i < PrevPageBtns.Num(); ++i)
	{
		UAsdaButtonRelay* PR = NewObject<UAsdaButtonRelay>(this);
		PR->Index = i;
		PR->Callback = [this](int32 Page) { SetPrevPage(Page); };
		PrevPageBtns[i]->OnClicked.AddDynamic(PR, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, PR, PrevPageBtns[i]);
	}

	PrevSpecialCode.Init(-1, 5);
	PrevNormalCode.Init(-1, 15);
	PrevCellHovered = -1;

	if (PrevClose)
	{
		AsdaUi::BindButton(this, PrevClose, 15648, [this](int32) { ClosePrevWindow(); }, Relays);   // ★ U20: реле клика — общая точка
		PrevClose->SetVisibility(ESlateVisibility::Visible);
	}
	int32 Bound = 0;
	for (const TObjectPtr<UImage>& I : PrevSpecial) { if (I) { ++Bound; } }
	for (const TObjectPtr<UImage>& I : PrevNormal)  { if (I) { ++Bound; } }
	bPrevReady = (Bound > 0);
	UE_LOG(LogAsdaBooster, Log,
		TEXT("[гача] окно 15600 «Вид» разобрано: ячеек %d из 20, имя=%s крестик=%s узлов страниц %d (кнопок %d)"),
		Bound, PrevName ? TEXT("да") : TEXT("НЕТ"), PrevClose ? TEXT("да") : TEXT("НЕТ"),
		PrevPageParts.Num(), PrevPageBtns.Num());
}

// ★ Порт `boosterWindowPopulate@0xB82D90` (`shop_trade.c:563-825`), ветка `a2 != 0`.
void UAsdaBoosterPanelSubsystem::PopulatePrevWindow(int32 ItemCode)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!IT || !Inv) { return; }
	// Эталон: `CItemManager_GetItemRecord(a3)` → нет записи ⇒ return; затем `GetBoosterRecord(+0xD8)`.
	if (!IT->GetItem(ItemCode)) { return; }
	if (!IT->GetBoosterSlotsRaw(ItemCode, PrevSlots35))
	{
		UE_LOG(LogAsdaBooster, Log,
			TEXT("[гача] «Вид»: у предмета %d нет строки booster_list — эталон здесь тоже выходит"), ItemCode);
		return;
	}
	if (!EnsurePrevWindow()) { return; }
	PrevItemCode = ItemCode;   // эталонный `v62[698] = a3`

	// Имя набора — контрол 15646 (`ItemData_FindByCode(*boosterItemRecord)`).
	if (PrevName)
	{
		PrevName->SetText(FText::FromString(IT->GetItemName(ItemCode)));
		PrevName->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	// ── Пять «особых»: позиции 0…4 записи (`boosterRecord->itemName[8*i]`) ──
	//   Эталон параллельно ведёт массив кодов `v62[i + 623]` — именно из него читает подсказка
	//   по наведению (`uiInventoryShowSlotTooltip@0xB87200`). Ведём его же.
	PrevSpecialCode.Init(-1, PrevSpecial.Num());
	int32 ShownSpecial = 0;
	for (int32 i = 0; i < PrevSpecial.Num(); ++i)
	{
		const int32 Code = PrevSlots35.IsValidIndex(i) ? PrevSlots35[i] : 0;
		if (!PrevSpecial[i]) { continue; }
		if (Code > 0)
		{
			Inv->ApplyIconToImage(PrevSpecial[i], Code, kPrevCellPx);
			PrevSpecial[i]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			PrevSpecialCode[i] = Code;
			++ShownSpecial;
		}
		else
		{
			// Эталон рисует в пустую ячейку «заглушку» (`getSubObject_p3081112`) и пишет код −1.
			Inv->ClearIcon(PrevSpecial[i]);
			PrevSpecial[i]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ── Тридцать «обычных»: позиции 5…34, по 15 на страницу (эталонный `v62[662]` = номер страницы) ──
	PrevNormalCode.Init(-1, PrevNormal.Num());
	int32 Filled = 0;
	int32 TotalNormal = 0;
	for (int32 i = 5; i < PrevSlots35.Num(); ++i) { if (PrevSlots35[i] > 0) { ++TotalNormal; } }
	const int32 MaxPage = (TotalNormal > 15) ? 1 : 0;
	PrevPage = FMath::Clamp(PrevPage, 0, MaxPage);
	int32 Seen = 0;
	for (int32 i = 5; i < PrevSlots35.Num(); ++i)
	{
		const int32 Code = PrevSlots35[i];
		if (Code <= 0) { continue; }
		const int32 PageOfThis = Seen / 15;
		const int32 Cell = Seen % 15;
		++Seen;
		if (PageOfThis != PrevPage) { continue; }
		if (PrevNormal.IsValidIndex(Cell) && PrevNormal[Cell])
		{
			Inv->ApplyIconToImage(PrevNormal[Cell], Code, kPrevCellPx);
			PrevNormal[Cell]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			PrevNormalCode[Cell] = Code;    // эталонный `v62[gridSlot + 645] = itemCode`
			++Filled;
		}
	}
	for (int32 c = Filled; c < PrevNormal.Num(); ++c)
	{
		if (PrevNormal[c]) { Inv->ClearIcon(PrevNormal[c]); PrevNormal[c]->SetVisibility(ESlateVisibility::Collapsed); }
	}
	// ★★★ Эталон: `if (v62[663] > 15) SetVisible(15644, 1) else SetVisible(15644, 0)`
	//   (`shop_trade.c:786-789`). Контрол 15644 несёт ОБЕ стрелки и подпись «<» — при одной странице
	//   гаснет всё сразу. В порту это пять узлов (см. `SetupPrevControls`), гасим каждый.
	const bool bManyPages = (TotalNormal > 15);
	for (const TObjectPtr<UWidget>& W : PrevPageParts)
	{
		if (!W) { continue; }
		// ★ Дедупликация 2026-09-02: вторая копия того же правила в этом файле — зовём `SetVis`.
		SetVis(W, bManyPages);
	}

	PlacePrevWindow();
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kPrevWindowId, true);
		WM->BringWindowToFront(kPrevWindowId);
	}
	bPrevVisible = true;
	UE_LOG(LogAsdaBooster, Log,
		TEXT("[гача] «Вид» 15600 показано: бустер %d («%s»), особых %d/5, обычных %d (страница %d из %d)"),
		ItemCode, *IT->GetItemName(ItemCode), ShownSpecial, TotalNormal, PrevPage + 1, MaxPage + 1);
}

// ★★★ ИСПРАВЛЕНО (замечание живого прогона: «окно "Вид" открывается СВЕРХУ, а должно СПРАВА»).
//   Порт `boosterWnd_OnMessage@0xD16ED0`, `case 0x2758u` (`shop_trade.c:11953-11960`) — ДОСЛОВНО:
//       Class_GetVec2_74(v13[223], &x, &y);      // позиция КОРНЯ окна 10050
//       x += вернувшийся_прямоугольник->w;       // ★ + ШИРИНА окна барабана
//       y += вернувшийся_прямоугольник->h;       // ★ + ВЫСОТА окна барабана
//       CControl_SetField74_75(g_pHoverTooltipWnd /* = корень 15600 */, x, y − 263);
//   Прежний порт брал ТОЛЬКО позицию рамки и вычитал 263 — отсюда «окно сверху».
//   С обоими слагаемыми (окно 10050 в .JUI ровно 266×266):
//       X = x10050 + 266             ⇒ левая грань «Вида» = ПРАВАЯ грань бустера, зазор 0 px
//       Y = y10050 + 266 − 263 = y10050 + 3  ⇒ верхние кромки параллельны, сдвиг 3 px вниз
//   То есть требование пользователя («кромки параллельны, грани прилегают») — это и есть
//   ОРИГИНАЛЬНАЯ формула, отступления не потребовалось.
//   ⚠ Импортёр расплющил иерархию: «подвинуть окно» = сдвинуть все узлы корня на одну дельту
//   (тот же способ, что у самого окна барабана и у панели 26500, закон `ui-window-drag-hittest`).
// ★★ ЗАЯВКА НА АБСОЛЮТНУЮ ПОСТАНОВКУ ОКНА «ВИД» (см. «зачем» в заголовке).
void UAsdaBoosterPanelSubsystem::RequestPrevPlacementAbs(const FVector2D& TopLeftAbs)
{
	PrevPlaceAbsWant = TopLeftAbs;
	bPrevPlaceAbsPending = true;
}

// Ширина окна «Вид» ПО ДАННЫМ АССЕТА: объединение девяти планов рамки `BOOSTER_PREV_WIN_0_0..8`.
//   Читается из раскладки слотов, поэтому доступна сразу после Build (живой геометрии ещё нет).
float UAsdaBoosterPanelSubsystem::GetPrevWindowWidth() const
{
	if (!PrevWidget) { return 0.f; }
	float MinX = FLT_MAX, MaxX = -FLT_MAX;
	for (int32 i = 0; i < 9; ++i)
	{
		UWidget* W = AsdaUi::FindNodeByName(PrevWidget,
			*FString::Printf(TEXT("BOOSTER_PREV_WIN_0_%d"), i), nullptr);
		FVector2D P, Sz;
		if (!W || !AsdaUi::SlotRect(W, P, Sz)) { continue; }
		MinX = FMath::Min(MinX, P.X);
		MaxX = FMath::Max(MaxX, P.X + Sz.X);
	}
	return (MaxX > MinX) ? (MaxX - MinX) : 0.f;
}

void UAsdaBoosterPanelSubsystem::PlacePrevWindow()
{
	if (!PrevWidget) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(PrevWidget->GetRootWidget());
	UWidget* Ref = AsdaUi::FindNodeByName(PrevWidget, TEXT("BOOSTER_PREV_WIN_0_0"), nullptr);
	UCanvasPanelSlot* RefSlot = Ref ? AsdaUi::SlotOf(Ref) : nullptr;
	if (!Root || !RefSlot) { return; }

	// ★★ Ветка заявки: ставим по ЖИВОЙ абсолютной геометрии (анкоры окна-владельца при этом не
	//   важны). Пока окно ни разу не отрисовано, живой геометрии нет — заявка остаётся, `Tick`
	//   повторит. Сдвигаем ВСЕ узлы корня на одну дельту, как и эталонная ветка ниже.
	if (bPrevPlaceAbsPending)
	{
		FVector2D Mn, Mx;
		if (AsdaUi::WidgetAbsRect(Ref, Mn, Mx) && !(Mx - Mn).IsNearlyZero())
		{
			const FVector2D D = PrevPlaceAbsWant - Mn;
			if (!D.IsNearlyZero())
			{
				// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): SPAN-безопасный сдвиг детей корня (было `Left/Top += d`).
				AsdaUi::ShiftRootChildren(Root, D);
			}
			bPrevPlaceAbsPending = false;
			UE_LOG(LogAsdaBooster, Log,
				TEXT("[гача] «Вид» 15600 поставлено по заявке: (%.0f,%.0f)"),
				PrevPlaceAbsWant.X, PrevPlaceAbsWant.Y);
		}
		return;   // эталонную формулу барабана в этом случае не применяем
	}
	// ★ 31.10.2026: заявка «точка канвы» (стыковка с окном 26500, `positionHoverTooltipWnd@0xDB3900`) — сдвиг всех узлов
	//   корня на одну дельту, как у формулы барабана ниже; живой геометрии не требует.
	if (bPrevPlaceCanvasPending)
	{
		const FVector2D D(PrevPlaceCanvasWant.X - RefSlot->GetOffsets().Left, PrevPlaceCanvasWant.Y - RefSlot->GetOffsets().Top);
		if (!D.IsNearlyZero()) { AsdaUi::ShiftRootChildren(Root, D); }
		bPrevPlaceCanvasPending = false;
		UE_LOG(LogAsdaBooster, Log, TEXT("[гача] «Вид» 15600 поставлено в точку канвы (%.0f,%.0f)"), PrevPlaceCanvasWant.X, PrevPlaceCanvasWant.Y);
		return;
	}
	// ★ 31.10.2026 [ОРИГИНАЛ] (`asda.Booster.PrevOpenReset`): формулу барабана эталон применяет ТОЛЬКО во входе барабана
	//   (`boosterWnd_OnMessage` case 0x2758, `AsdaStory_game.c:346529`); `boosterWindowPopulate` сама окно не двигает (кроме ветки
	//   `+697 == 1`, см. FLOW.md §3 — дефект оригинала, не переносится). Прежний порт ставил окно к барабану при КАЖДОМ
	//   заполнении — смена страницы в «Виде» из магазина уводила окно к (закрытому) барабану.
	if (PrevSource != EAsdaPrevSource::Drum && CVarBoosterPrevOpenReset.GetValueOnGameThread() != 0) { return; }
	// Опорная точка окна барабана — его левый-верхний план рамки (= позиция корня 10050).
	FVector2D Anchor(0.f, 0.f);
	if (UWidget* WinRef = WinWidget ? AsdaUi::FindNodeByName(WinWidget, TEXT("BOOSTING_WIN_0_0"), nullptr) : nullptr)
	{
		if (const UCanvasPanelSlot* WS = AsdaUi::SlotOf(WinRef))
		{
			Anchor = FVector2D(WS->GetOffsets().Left, WS->GetOffsets().Top);
		}
	}
	const FVector2D Want(Anchor.X + kWinW, Anchor.Y + kWinH - kPrevOffsetY);
	const FVector2D Delta(Want.X - RefSlot->GetOffsets().Left, Want.Y - RefSlot->GetOffsets().Top);
	if (Delta.IsNearlyZero()) { return; }
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): SPAN-безопасный сдвиг детей корня (было `Left/Top += d`).
	AsdaUi::ShiftRootChildren(Root, Delta);
}

// ★ Порт `boosterWindowPopulate(this, 0, −1)`: `CControl_SetVisible(v62[616], 0)`, `v62[615] = 0`.
void UAsdaBoosterPanelSubsystem::ClosePrevWindow()
{
	if (!bPrevVisible) { return; }
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kPrevWindowId, false);
		}
	}
	bPrevVisible = false;
	// Своя плашка гаснет вместе с окном (закон `ui-shared-tip-ownership`: гасит только владелец).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
		{
			Tip->HideTooltipFor(EAsdaTipOwner::BoosterPrev);
		}
	}
	PrevCellHovered = -1;
	UE_LOG(LogAsdaBooster, Verbose, TEXT("[гача] «Вид» 15600 закрыто"));
}

// ★ Порт `case 15644` сообщения 0x10501 (`shop_trade.c:1511-1519`):
//       if (self[662] != newValue) { self[662] = newValue;
//           self[698] < 0 ? boosterWindowPopulate(self, 1, −1) : boosterWindowPopulate(self, 1, self[698]); }
void UAsdaBoosterPanelSubsystem::SetPrevPage(int32 NewPage)
{
	if (!bPrevVisible || PrevPage == NewPage) { return; }
	PrevPage = NewPage;
	PopulatePrevWindow(PrevItemCode > 0 ? PrevItemCode : SelItemCode);
}

// ★★★ ПОДСКАЗКА ПО НАВЕДЕНИЮ В ОКНЕ «ВИД» — порт `uiInventoryShowSlotTooltip@0xB87200`
//   (`AsdaStory_game.c:211457-211530`). Эталон по IDNum контрола-иконки определяет индекс ячейки
//   (`cmdToHairIndex5@0xB86860` — таблица 15603/15605/15607/15609/15611; `invLookupSpecialItemIndex@0xB868F0`
//   — таблица 15614…15642 шаг 2), достаёт код из `v15[idx+623]` / `v15[idx+645]` и строит
//   ОБЫЧНЫЙ тултип предмета:
//       ItemRecord      → ItemTooltip_Build(&g_ItemTooltip, slot, −1, 0, 1, 1, 0, 0)
//       SpecialItem     → тот же ItemTooltip_Build
//       SowelRecord     → CItem_BuildSowelTooltipText + uiTooltipShowAndLayout
//   То есть ровно тот же состав, что у предметов сумки ⇒ берём ОБЩУЮ точку
//   (закон `port-shared-points`), владелец — `BoosterPrev`.
//   ★★★ ИСПРАВЛЕНО (живой прогон: «в окне Вид только имя и описание»). Здесь стояла общая точка
//   `ShowItemTooltipByCode` — а это ДРУГОЙ построитель эталона (`itemTooltipBuildByCode@0xEA6B80`,
//   заголовок = имя, тело = описание из `Item_tooltip.BIN`, без единой характеристики). Эталон
//   `uiInventoryShowSlotTooltip` его НЕ зовёт: он собирает синтетическую 57-байтовую запись на
//   стеке (`CSlot_Init`) и отдаёт её ПОЛНОМУ `ItemTooltip_Build`. Разница построителей настоящая
//   (`system_dedup_audit_3.md`, B13) — поэтому не слили, а подключили нужный:
//   `ShowPreviewSlotTooltip` (порт `uiInventoryShowSlotTooltip@0xB87200`).
void UAsdaBoosterPanelSubsystem::TickPrevTooltip()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }

	int32 HitCode = 0;
	const int32 Hit = PrevCellUnderCursor(HitCode);   // ★ 31.10.2026: общая часть с щелчком по ячейке

	if (Hit < 0)
	{
		if (PrevCellHovered >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::BoosterPrev); PrevCellHovered = -1; }
		return;
	}
	if (Hit == PrevCellHovered) { return; }
	PrevCellHovered = Hit;
	Tip->ShowPreviewSlotTooltip(HitCode, EAsdaTipOwner::BoosterPrev);
}

// ═══════════════════════════════ ОСТАНОВКА И ПОКАЗ ПРИЗА ══════════════════════════════════════
// ★ Порт `boosterShopResetSelection@0xD21A90` (`shop_trade.c:12652-12673`) — СРАБАТЫВАЕТ ПО
//   ТАЙМАУТУ 5 000 мс от приёма 5469 и именно он ОСТАНАВЛИВАЕТ барабан.
void UAsdaBoosterPanelSubsystem::ResetSelection()
{
	// ★★★★★ ПЕРЕПИСАНО (третий заход, 2026-08-14) — ЭТО И БЫЛ ДЕФЕКТ «приз появляется в пустоте».
	//   Второй заход читал имена полей IDA БУКВАЛЬНО, а они сдвинуты на −4 позиции (полное
	//   доказательство сдвига — в шапке `HidePanelControls` выше). Тело
	//   `boosterShopResetSelection@0xD21A90` (`AsdaStory_ida.c:561923-561943`) с расшифровкой:
	//       gap_3F0[12] = 0;  gap_3F0[16] = 0;  gap_3F0[36] = 1000;
	//       SetVisible(m_pBoosterBorder   → 232 = 10059 SPOT1, 0)
	//       SetVisible(m_pBoosterQuestion → 233 = 10060 SPOT2, 0)
	//       SetVisible(m_pBoosterBang     → 234 = 10061 SPOT3, 0)
	//       SetVisible(m_pBoosterProgress → 235 = 10062 SPOT4, 0)
	//       SetVisible(m_pBoosterSpot1    → 228 = 10055 CHIMNEY, 0)
	//       SetVisible(m_pBoosterSpot2    → 229 = 10056 CLOCK,   0)
	//       SetVisible(m_pBoosterEye1     → 226 = 10053 CHIMNEY_BG, 1)
	//       SetVisible(m_pBoosterEye2     → 227 = 10054 CLOCK_BG,   1)
	//       SetVisible(m_pBoosterName     → 236 = 10063 BORDER,     1)   ← рамка вокруг ячейки
	//       sub_93345A(m_pBoosterResult   → 237 = 10064 QUESTION, сохранённая альфа); hide 10064
	//       CControl_Show(m_pBoosterStartBtnBase → 238 = 10065 BANG)     ← ★★★ ПУСК «ВЗРЫВА»
	//       SetColor4(m_pBoosterOK        → 241 = 10068 RESULT, 0xFF, 0); hide 10068
	//   ⇒ в момент остановки барабана в СЛОТЕ вместо перебираемой иконки зажигается анимированный
	//     «взрыв» `Booster_Center` (10065) в анимированной рамке (10063), прогресс-бар продолжает
	//     литься. Никакой «пустоты» между остановкой и показом приза в оригинале нет.
	//   ★ Прогресс-бар здесь НЕ ТРОГАЕТСЯ (его гасит только показ приза) — прежний код гасил его
	//     здесь, отчего полоса и «исчезала вместе с эффектом».
	bProcess = false;    // ★ гасит гейт update — со следующего кадра тик выходит на первой строке
	bSpinning = false;
	Decel = kDecelStart;
	for (UWidget* S : Spots) { SetVis(S, false); }   // 10059…10062 — гаснут ВСЕ ЧЕТЫРЕ лампы
	SetVis(Chimney,   false);    // 10055 труба «вкл»
	SetVis(Clock,     false);    // 10056 часы «вкл»
	SetVis(ChimneyBg, true);     // 10053 труба «выкл»
	SetVis(ClockBg,   true);     // 10054 часы «выкл»
	SetVis(Border,    true);     // 10063 — рамка ячейки зажигается
	SetVis(Question,  false);    // 10064 «?» гаснет (альфа восстановлена)
	if (DrumIcon) { DrumIcon->SetRenderOpacity(1.f); }
	DrumAlpha = 255;
	SetVis(DrumIcon,  false);    // 10068 — перебираемая иконка гаснет
	// ★★★ И СРАЗУ ЖЕ в том же слоте пускается «взрыв» 10065 `Booster_Center`.
	//   `CControl_Show` у анимплана = показать + прогнать анимацию с нуля.
	if (UAsdaAnimatedPlane* BangPlane = Cast<UAsdaAnimatedPlane>(Bang)) { BangPlane->RestartAnim(); }
	SetVis(Bang, true);
	// По концу прогона анимплан ГАСИТ САМ СЕБЯ и шлёт событие 24 (`ui_core.c:108899-108901`),
	//   а обработчик ловит его по `cmdId == 10065` (`ui_core.c:48103-48104`) и показывает приз.
	BangStartS = FPlatformTime::Seconds();
	UE_LOG(LogAsdaBooster, Log, TEXT("[гача] барабан остановлен по таймауту %d мс (позиция %d); ")
		TEXT("в слоте зажжён «взрыв» 10065 на %d мс, по его концу — показ приза"), kResetTimeoutMs, DrumIdx, kBangMs);
}

// ★ Порт `tagBoosterShop_RefreshSelectedItemDisplay@0xD21C00` (`shop_trade.c:12676-12784`).
//   Единственная точка вызова в эталоне — событие 24 «прогресс-бар дошёл до 100 %»
//   (`ui_core.c:48103-48105`). Код приза — первый i32 записи предмета, т.е. пакет +9.
void UAsdaBoosterPanelSubsystem::RefreshSelectedItemDisplay()
{
	if (!bHavePacket || Packet68.Num() < 68) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!IT || !Inv) { return; }
	int32 PrizeCode = 0;
	FMemory::Memcpy(&PrizeCode, Packet68.GetData() + 9, sizeof(int32));
	// Эталон: приз не резолвится ни в одну из таблиц ⇒ просто закрыть окно.
	const bool bKnown = (IT->GetItem(PrizeCode) != nullptr) || IT->IsSpecialItemCode(PrizeCode) || IT->IsSowelCode(PrizeCode);
	if (PrizeCode <= 0 || !bKnown)
	{
		UE_LOG(LogAsdaBooster, Warning, TEXT("[гача] приз %d не найден ни в одной таблице — окно закрывается"), PrizeCode);
		ApplyResultAndClose();
		return;
	}
	SetVis(Bang, false);
	Inv->ApplyIconToImage(DrumIcon, PrizeCode, kDrumIconPx);
	SetVis(DrumIcon, true);
	if (DrumIcon) { DrumIcon->SetRenderOpacity(1.f); }
	if (PrizeName)
	{
		PrizeName->SetText(FText::FromString(IT->GetItemName(PrizeCode)));
		SetVis(PrizeName, true);
	}
	// Эталон: `SetVisible(pThis[239] = 10066, 0); CProgressBar_SetValue(pThis[239], 0);`
	SetVis(ProgressBase, false);
	SetVis(ProgressFill, false);
	if (ProgressFill)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(ProgressFill))
		{
			FMargin M = S->GetOffsets(); M.Right = 0.f; S->SetOffsets(M);
		}
	}
	SetVis(OkBtnBase, true);      // ★ `SetVisible(pThis[244] = 10075, 1)` — единственный показ здесь
	SetVis(BtnOk,    true);       // ★★★ сама кнопка 10070 и её подпись — в WBP они `Collapsed`
	SetVis(CapOk,    true);       //     (см. комментарий в `HidePanelControls`)
	// ★★★ УБРАНО (третий заход): показа крестика 10071 в теле эталона НЕТ — он скрыт с клика START
	//   и до конца прокрута, окно закрывается кнопкой «Подтвердить». Прежняя строка
	//   `SetVis(BtnClose, true)` была отсебятиной без ссылки на эталон.
	++CntPrize;
	UE_LOG(LogAsdaBooster, Log, TEXT("[гача] приз показан: %d («%s»)"), PrizeCode, *IT->GetItemName(PrizeCode));
}

// Применение приза к инвентарю через общую точку + при необходимости 5474.
void UAsdaBoosterPanelSubsystem::ApplyPrize(bool bSend5474)
{
	if (!bHavePacket || Packet68.Num() < 68) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// ★ bPlayFxSound = false: в ветке кнопки OK эталон эффект спавнит, а звук НЕ играет.
		Inv->ApplyGachaPrize(Packet68, /*bPlayFxSound*/ false);
	}
	(void)bSend5474;   // 5474 уходит внутри `ApplyGachaPrize` — та же общая точка, что и в ветке В
}

// ★ Порт `boosterApplyResult@0xD1F830` (`shop_trade.c:12026-12095`): ПРИМЕНИТЬ РЕЗУЛЬТАТ и скрыть окно.
//
// ★★★ ИСПРАВЛЕНО ПРИ ПОВТОРНОЙ СВЕРКЕ (второй заход, 2026-08-13) — ПОТЕРЯ ПРИЗА.
//   Прежде эта функция только гасила контролы. Тело эталона (`shop_trade.c:12066-12093`) после
//   гашения делает РОВНО ТО ЖЕ, что и кнопка OK, если буфер пакета не пуст:
//       ItemRecord    = GetItemRecord(*(i32*)(this + 2470));   // потраченная гача
//       removedItemRec= GetItemRecord(*(i32*)(this + 2477));   // приз (запись 57 б, пакет +9)
//       if (ItemRecord || removedItemRec) {
//           OnRecv_RemoveSlot_byPtr(this, {контейнер +2474, слот +2475});
//           OnRecv_ItemUpdate(this, this + 2477);
//           HUD_UpdateMiniWeightBar(g_pHUD, *(i16*)(this + 2534));   // пакет +66
//           CInventoryUI_ResetAndRefresh(this);
//           ... нотис 20066 с именем, окрашенным по DropColor(itemGrade) ... }
//   (арифметика смещений: буфер 68 б лежит в `gap_3F0[1460]` = байт 2468 объекта, отсюда
//    +2 → 2470, +6 → 2474, +7 → 2475, +9 → 2477, +66 → 2534 — все пять сходятся.)
//   Ни `Effect_SpawnByName`, ни отправки 5474 в теле НЕТ — только остановка зациклённого
//   `s_item_boosting01.wav`. Поэтому применяем приз без эффекта, без звука и без 5474.
//   Последствие старого кода: приз, закрытый крестиком 10071 или потерянный по 30-секундному
//   таймауту, в порту ПРОПАДАЛ, хотя сервер его уже выдал.
void UAsdaBoosterPanelSubsystem::ApplyResultAndClose()
{
	// Выдача идёт ПЕРВОЙ строкой, до `ResetFields()`, потому что он обнуляет `bHavePacket`.
	if (bHavePacket && Packet68.Num() >= 68)
	{
		if (UAsdaInventorySubsystem* Inv2 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			int32 PrizeCode = 0;
			FMemory::Memcpy(&PrizeCode, Packet68.GetData() + 9, sizeof(int32));
			Inv2->ApplyGachaPrize(Packet68, /*bPlayFxSound*/ false, /*bSpawnFx*/ false,
			                      /*bSend5474*/ false, /*bRareNotice*/ false);
			UE_LOG(LogAsdaBooster, Log,
				TEXT("[гача] закрытие окна: приз %d ВЫДАН (эталон `boosterApplyResult@0xD1F830` "
				     "делает это же — без эффекта, без звука, без 5474)"), PrizeCode);
		}
		Packet68.Reset();
		bHavePacket = false;
	}
	ResetFields();
	HidePanelControls();
	SetVis(Question, false);
	SetVis(ChimneyBg, true);
	SetVis(ClockBg, true);
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ClearIcon(DrumIcon);
	}
	SetVis(DrumIcon, false);
	StopLoopStart();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetWindowVisible(kWindowId, false); }
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
		{
			Tip->HideTooltipFor(EAsdaTipOwner::Booster);   // гасим ТОЛЬКО свою плашку
		}
	}
	// ★ Эталон гасит окно «Вид» вместе с барабаном (`uiPanelCloseAndKillFocus@0xDAC900`,
	//   `ui_core.c:62673-62676` — там же скрывается корень 15600).
	ClosePrevWindow();
	bWindowOpen = false;
	UE_LOG(LogAsdaBooster, Verbose, TEXT("[гача] окно 10050 закрыто (состояние сброшено)"));
}

// ═══════════════════════════════ ★★★ ПРИЁМ 5469 ════════════════════════════════════════════════
// Порт `Recv_BoosterShopResult_0@0xCFF9F0` (`network.c:16985-17060`), ветки А и Б.
bool UAsdaBoosterPanelSubsystem::NotifyGachaResult(const TArray<uint8>& Body68)
{
	if (Body68.Num() < 68) { return false; }
	// Ветка В эталона: окна нет ⇒ вызывающий применяет результат немедленно и сам шлёт 5474.
	if (!bWindowOpen) { return false; }

	Packet68 = Body68;
	bHavePacket = true;
	CloseDeadlineS = 0.0;                        // сброс 30-секундного таймаута
	ResetDeadlineS = FPlatformTime::Seconds();   // ПУСК 5-секундного (остановка барабана)

	if (!bProcess)
	{
		// Ветка Б: окно открыто, но процесс не запускался — эталон просто взводит оба флага.
		bProcess = true;
		bSpinning = true;
		UE_LOG(LogAsdaBooster, Log, TEXT("[гача] 5469 принят при незапущенном процессе — ветка Б эталона"));
		return true;
	}

	// Ветка А.
	bSpinning = true;                            // ★ барабан НАЧИНАЕТ крутиться только ЗДЕСЬ
	DrumAlpha = 0;
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		FVector Loc = FVector::ZeroVector;
		if (UWorld* W = GetWorld())
		{
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				if (APawn* P = PC->GetPawn()) { Loc = P->GetActorLocation(); }
			}
		}
		Snd->PlaySound3D(kSndSpin, Loc, 100.f, 10000.f, /*bLoop*/ false);
	}
	StopLoopStart();                             // остановка зациклённого `s_item_boosting01.wav`
	// ★★★ ВОЗВРАЩЕНО К ПЕРВОМУ ЧТЕНИЮ (третий заход): `network.c:17050-17051` пишет
	//   `SetVisible(m_pBoosterEye1, 0); SetVisible(m_pBoosterEye2, 0);`, но имена полей сдвинуты
	//   на −4 позиции (доказательства — в шапке `HidePanelControls`), и это **10053 CHIMNEY_BG**
	//   и **10054 CLOCK_BG** — «выключенные» труба и часы гаснут, потому что на клик START уже
	//   зажглись их «включённые» двойники 10055/10056. Второй заход поменял это на глаза — ошибка.
	SetVis(ChimneyBg, false);   // 10053
	SetVis(ClockBg,   false);   // 10054
	ProgressStartS = FPlatformTime::Seconds();   // заливка 10066 за 9200 мс
	bProgressDone = false;
	SetVis(ProgressBase, true);
	SetVis(ProgressFill, true);
	UE_LOG(LogAsdaBooster, Log,
		TEXT("[гача] 5469 принят: барабан пущен, прогресс %d мс, остановка через %d мс"),
		kProgressMs, kResetTimeoutMs);
	return true;
}

// ═══════════════════════════════ ТИК ═══════════════════════════════════════════════════════════
void UAsdaBoosterPanelSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.BoosterPanel");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// Заявка на постановку окна «Вид» могла не примениться (окно ещё не отрисовано) — повторяем.
	if (bPrevVisible && bPrevPlaceAbsPending) { PlacePrevWindow(); }
	// ★ 31.10.2026 [ОРИГИНАЛ] (`asda.Booster.PrevPaths`): щелчок по ячейкам «Вида» (набор → окно 26500). Опрос — каждый кадр.
	if (CVarBoosterPrevPaths.GetValueOnGameThread() != 0) { TickPrevCellClick(); }
	// ★ 31.10.2026 [ОРИГИНАЛ] (`asda.Booster.PrevTipAnyOwner`): окно «Вид» 15600 принадлежит МАГАЗИНУ бустеров (эталон
	//   `boosterShopOnMessage@0xB876F0` — событие 8 → `uiInventoryShowSlotTooltip`, 9 → за курсором, 0xA → гашение; гейта по
	//   барабану нет). Порт опрашивал ячейки только при открытом барабане 10050 — из магазина NPC «Усилители» подсказок не было.
	if (CVarBoosterPrevTipAnyOwner.GetValueOnGameThread() != 0)
	{
		if (bPrevVisible) { TickPrevTooltip(); }
		else if (PrevCellHovered >= 0)
		{
			// Окно «Вид» закрыто с курсором на ячейке — своя подсказка гаснет (эталон: закрытие окна → `CControl_KillFocus`).
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::BoosterPrev);
			}
			PrevCellHovered = -1;
		}
	}
	if (!bWindowOpen) { return; }
	if (bPrevVisible && CVarBoosterPrevTipAnyOwner.GetValueOnGameThread() == 0) { TickPrevTooltip(); }   // прежний порт: только при барабане
	TickProgress();
	TickBang();
	TickDrum();
}

// ★ Окончание прогона анимплана 10065 `BOOSTER_BANG` — эталонное событие 24, единственный триггер
//   показа приза (`ui_core.c:48103-48104`: `case 0x18u: if (cmdId == 10065) …RefreshSelectedItemDisplay`).
void UAsdaBoosterPanelSubsystem::TickBang()
{
	if (BangStartS <= 0.0) { return; }
	if ((FPlatformTime::Seconds() - BangStartS) * 1000.0 < (double)kBangMs) { return; }
	BangStartS = 0.0;
	RefreshSelectedItemDisplay();
}

// ★ Порт `CProgressBar_UpdateAnim@0x12E9400` (`ui_core.c:111164-111189`): заливка за 9200 мс,
//   на 100 % — событие 24, которое в эталоне ловит `CGameWindow_OnCommand` и зовёт показ приза.
//   Закон `ui-progressbar-animate-existing`: анимируем ширину ПЛАНА ЗАЛИВКИ, браш не подменяем.
void UAsdaBoosterPanelSubsystem::TickProgress()
{
	if (ProgressStartS <= 0.0 || bProgressDone) { return; }
	const double ElapsedMs = (FPlatformTime::Seconds() - ProgressStartS) * 1000.0;
	// ★★★ ИСПРАВЛЕНО (дефект живого прогона «полоса прогресса не до конца доходит»).
	//   Раньше доля считалась как `Elapsed / 9200` — полоса успевала залиться лишь на 63 %, потому что
	//   на 5800 мс её прячет показ приза. Это НЕ соответствует оригиналу.
	//   Разбор `uiProgressBar_AnimStart@0x12E92E0` + `CProgressBar_UpdateAnim@0x12E9400` по бинарю:
	//     `animMin = 0`, `animMax = ШИРИНА ПЛАНА-0` (у 10066 в .JUI `Size = "175 11"` ⇒ 175),
	//     `tEnd = tStart + 9200`, каждый кадр `v = (now − tStart)/(tEnd − tStart) * (animMax − animMin)`,
	//     затем `if (v >= 100.0f) { bAnimating = 0; RaiseEvent(24); }` и `CProgressBar_SetValue(v)`,
	//     а сам бар имеет диапазон 0…100 (`CProgressBar_ctor@0x12E8910`: `*(this+153)=0`, `*(this+154)=100.0f`).
	//   То есть 9200 — длительность прогона ДО ЗНАЧЕНИЯ 175, а заполнение (значение 100) наступает
	//   на `9200 * 100 / 175 ≈ 5257 мс`. Это дефект оригинала (перепутаны пиксели и проценты),
	//   но именно он даёт наблюдаемую картину: на 5000 мс полоса ≈95 %, на ~5257 мс — 100 %,
	//   и она стоит полной ещё ~540 мс, пока доигрывает «взрыв» 10065.
	const double FullMs = (double)kProgressMs * 100.0 / (double)kProgressWidth;   // 9200*100/175
	const float Frac = FMath::Clamp((float)(ElapsedMs / FullMs), 0.f, 1.f);
	if (ProgressFill)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(ProgressFill))
		{
			FMargin M = S->GetOffsets();
			M.Right = kProgressWidth * Frac;   // при нулевых якорях Offsets.Right = ширина
			S->SetOffsets(M);
		}
	}
	if (Frac >= 1.f)
	{
		bProgressDone = true;
		ProgressStartS = 0.0;
		// ★ ИСПРАВЛЕНО: раньше отсюда звался `RefreshSelectedItemDisplay()` — «эталонное событие 24».
		//   Это НЕВЕРНО. Событие 24 шлют оба плана, но `CGameWindow_OnCommand` ловит его ТОЛЬКО с
		//   `cmdId == 10065` (`ui_core.c:48103-48104`), то есть от «взрыва» `BOOSTER_BANG`; событие
		//   от прогресс-бара (id 10066) в оригинале не обрабатывает никто. Из-за подмены приз
		//   показывался на 9200 мс после приёма 5469 вместо ~5800 мс (5000 остановка + 800 взрыв).
		UE_LOG(LogAsdaBooster, Verbose,
			TEXT("[гача] прогресс-бар 10066 заполнен — в эталоне это событие никем не обрабатывается"));
	}
}

// ★★★ Порт тела `boosterShopUpdate@0xD202A0` (`shop_trade.c:12204-12338`), кадр в кадр.
void UAsdaBoosterPanelSubsystem::TickDrum()
{
	// Эталон: `if (!gap_3F0[12]) return;` — процесс не запущен.
	if (!bProcess) { return; }

	const double NowS = FPlatformTime::Seconds();

	// Таймаут 30 000 мс от клика START: закрыть окно + чат-нотис 13114.
	if (CloseDeadlineS > 0.0 && (NowS - CloseDeadlineS) * 1000.0 >= (double)kCloseTimeoutMs)
	{
		bProcess = false;
		CloseDeadlineS = 0.0;
		ApplyResultAndClose();
		if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			Msg->PostLocFmt(EAsdaMsgCategory::ItemRefused, kLocTimeout, {}, {});
		}
		UE_LOG(LogAsdaBooster, Log, TEXT("[гача] таймаут %d мс без результата — окно закрыто, нотис 13114"), kCloseTimeoutMs);
		return;
	}
	// Таймаут 5 000 мс от приёма 5469: ОСТАНОВКА барабана.
	if (ResetDeadlineS > 0.0 && (NowS - ResetDeadlineS) * 1000.0 >= (double)kResetTimeoutMs)
	{
		ResetDeadlineS = 0.0;
		ResetSelection();
		return;
	}
	// Эталон: `if (!shop[256]) return;` — барабан не крутится.
	if (!bSpinning) { return; }

	// ★ Эталонная строка `shop[261] -= shop[635];`. `DecelStep` = 0, потому что источник в бинаре
	//   НЕ ЗАПИСЫВАЕТСЯ НИГДЕ ⇒ `Decel` вечно 1000 > 500 и вторая фаза недостижима. Оставлено 1:1,
	//   чтобы дефект оригинала был виден, а не «исправлен» молча.
	Decel -= DecelStep;

	auto StepToNextIcon = [this]()
	{
		DrumIdx = (DrumIdx < kDrumSlots - 1) ? (DrumIdx + 1) : 0;
		const int32 Code = DrumCodes[DrumIdx];
		UGameInstance* GI = GetGameInstance();
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (Code > 0 && Inv && Inv->ApplyIconToImage(DrumIcon, Code, kDrumIconPx))
		{
			DrumAlpha = 255;
			if (DrumIcon) { DrumIcon->SetRenderOpacity(1.f); }
		}
		// Код не резолвится (36-я позиция — дефект оригинала): эталон иконку НЕ перерисовывает и
		//   альфу не поднимает — получается один «пустой» кадр за оборот. Воспроизводим.
	};

	if (Decel > kDecelThreshold)
	{
		// Достижимая («медленная») фаза: шаг затухания 50.
		if (DrumAlpha <= 0) { StepToNextIcon(); return; }
		DrumAlpha = FMath::Max(0, DrumAlpha - kAlphaStepSlow);
		if (DrumIcon) { DrumIcon->SetRenderOpacity((float)DrumAlpha / 255.f); }
		return;
	}
	// НЕДОСТИЖИМАЯ фаза оригинала (шаг 95). Оставлена 1:1 — см. комментарий выше.
	if (DrumAlpha > 0)
	{
		DrumAlpha = FMath::Max(0, DrumAlpha - kAlphaStepFast);
		if (DrumIcon) { DrumIcon->SetRenderOpacity((float)DrumAlpha / 255.f); }
		return;
	}
	StepToNextIcon();
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaBoosterPanelSubsystem::DumpControls() const
{
	FString S = FString::Printf(
		TEXT("окно 10050: виджет=%s готово=%s открыто=%s процесс=%s вращение=%s\n")
		TEXT("  барабан(10068)=%s прогресс(10066)=%s имя(10067)=%s «?»(10064)=%s «взрыв»(10065)=%s рамка(10063)=%s\n")
		TEXT("  START=%s OK=%s CLOSE=%s PREVIEW=%s · подложки 10073=%s 10074=%s 10075=%s\n"),
		WinWidget ? TEXT("да") : TEXT("НЕТ"), bReady ? TEXT("да") : TEXT("НЕТ"),
		bWindowOpen ? TEXT("да") : TEXT("НЕТ"), bProcess ? TEXT("да") : TEXT("НЕТ"),
		bSpinning ? TEXT("да") : TEXT("НЕТ"),
		DrumIcon ? TEXT("да") : TEXT("НЕТ"), ProgressFill ? TEXT("да") : TEXT("НЕТ"),
		PrizeName ? TEXT("да") : TEXT("НЕТ"), Question ? TEXT("да") : TEXT("НЕТ"),
		Bang ? TEXT("да") : TEXT("НЕТ"), Border ? TEXT("да") : TEXT("НЕТ"),
		BtnStart ? TEXT("да") : TEXT("НЕТ"), BtnOk ? TEXT("да") : TEXT("НЕТ"),
		BtnClose ? TEXT("да") : TEXT("НЕТ"), BtnPreview ? TEXT("да") : TEXT("НЕТ"),
		StartBtnBase ? TEXT("да") : TEXT("НЕТ"), ViewBtnBase ? TEXT("да") : TEXT("НЕТ"),
		OkBtnBase ? TEXT("да") : TEXT("НЕТ"));
	int32 Spot = 0;
	for (const TObjectPtr<UWidget>& W : Spots) { S += FString::Printf(TEXT("  лампа 1005%d=%s\n"), 9 + Spot++, W ? TEXT("да") : TEXT("НЕТ")); }
	return S;
}

FString UAsdaBoosterPanelSubsystem::DumpModel() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FString S = FString::Printf(
		TEXT("бустер %d (%d:%d) · позиция барабана %d/%d · альфа %d · Decel %d (шаг %d) · пакет=%s\n")
		TEXT("открытий=%d стартов=%d призов=%d\n"),
		SelItemCode, SelContainer, SelSlot, DrumIdx, kDrumSlots, DrumAlpha, Decel, DecelStep,
		bHavePacket ? TEXT("есть") : TEXT("нет"), CntOpen, CntStart, CntPrize);
	for (int32 i = 0; i < kDrumSlots; ++i)
	{
		if (DrumCodes[i] > 0)
		{
			S += FString::Printf(TEXT("  [%02d] %d «%s»\n"), i, DrumCodes[i], IT ? *IT->GetItemName(DrumCodes[i]) : TEXT("?"));
		}
		else
		{
			S += FString::Printf(TEXT("  [%02d] — пусто%s\n"), i, i == kDrumSlots - 1 ? TEXT(" (36-я позиция, дефект оригинала)") : TEXT(""));
		}
	}
	return S;
}

// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ════════════════════════════════════════════
namespace
{
	UAsdaBoosterPanelSubsystem* BstSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U13): было `AddOnScreenDebugMessage` (12 с поверх экрана; в Shipping оверлей пуст) — стало общая точка `AsdaNotice::ConsoleEcho`: каждая строка в журнал (Display), непустая — в ленту Tip.
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
	void BstEcho(UWorld* W, const FString& S) { AsdaNotice::ConsoleEcho(W, S, LogAsdaBooster); }
#endif // !UE_BUILD_SHIPPING
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBstOpen(
	TEXT("asda.Booster.Open"),
	TEXT("asda.Booster.Open <кодБустера> [контейнер=2] [слот=0] — открыть окно 10050 ровно тем же путём, ")
	TEXT("что двойной клик по гача-предмету. Пример: asda.Booster.Open 20932 2 3"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaBoosterPanelSubsystem* B = BstSub(W);
		if (!B || A.Num() < 1) { BstEcho(W, TEXT("использование: asda.Booster.Open <кодБустера> [контейнер] [слот]")); return; }
		const int32 Code = FCString::Atoi(*A[0]);
		const int32 Cont = A.Num() > 1 ? FCString::Atoi(*A[1]) : 2;
		const int32 Slot = A.Num() > 2 ? FCString::Atoi(*A[2]) : 0;
		BstEcho(W, FString::Printf(TEXT("окно 10050: %s"),
			B->OpenWindow(Code, Cont, Slot) ? TEXT("открыто") : TEXT("НЕ открыто (нет записи item_list или booster_list)")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBstClick(
	TEXT("asda.Booster.Click"),
	TEXT("asda.Booster.Click <10069|10070|10071|10072> — нажать кнопку окна 10050 (проверка без мыши)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaBoosterPanelSubsystem* B = BstSub(W);
		if (!B || A.Num() < 1) { BstEcho(W, TEXT("использование: asda.Booster.Click <10069|10070|10071|10072>")); return; }
		switch (FCString::Atoi(*A[0]))
		{
		case 10069: B->OnStartButton();   BstEcho(W, TEXT("START нажата"));   break;
		case 10070: B->OnOkButton();      BstEcho(W, TEXT("OK нажата"));      break;
		case 10071: B->OnCloseButton();   BstEcho(W, TEXT("CLOSE нажата"));   break;
		case 10072: B->OnPreviewButton(); BstEcho(W, TEXT("PREVIEW нажата")); break;
		default: BstEcho(W, TEXT("известны 10069, 10070, 10071, 10072")); break;
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBstDump(
	TEXT("asda.Booster.Dump"),
	TEXT("asda.Booster.Dump — полный дамп контролов и состояния окна 10050 (включая все 36 позиций барабана)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaBoosterPanelSubsystem* B = BstSub(W)) { BstEcho(W, B->DumpControls() + B->DumpModel()); }
	}));
#endif // ASDA_DEV_CONSOLE
