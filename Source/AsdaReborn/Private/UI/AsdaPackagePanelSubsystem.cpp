// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPackagePanelSubsystem.cpp — ПАНЕЛЬ «СОСТАВ ПАКЕТА» (JUI 26500 `Package_Contents_Win`).
//   Реверс, адреса и разбор каждого шага — шапка `Public/UI/AsdaPackagePanelSubsystem.h`
//   и отчёт `Reports/item_use_panels.md`. Ни одно число здесь не выдумано.
//
// ═══════════ ТАБЛИЦА КОНТРОЛОВ (gamemain_jui.ini + имена узлов WBP, снятые дампом) ═════════════
//   Дамп WBP: `Reports/headless__probe_itempanels_widgets.log` (257 узлов).
//
//   idnum  idc                        узел WBP                       что делает
//   26500  Package_Contents_Win       Package_Contents_Win_0_0…_0_8   9 планов рамки 3×3; ширина растёт
//   26505  Package_Contents_Title     Package_Contents_Title_grp      титул-бар, ширина растёт
//   26501  Package_Contents_Move      Package_Contents_Move           МЕТКА ИМЕНИ КОРОБКИ (имя idc врёт);
//                                                                     она же ручка EVS{6→0x20003}
//   26506  Package_ClsBtn             Package_ClsBtn                  крестик
//   26507  Package_Contents_BtnBase   Package_Contents_BtnBase_grp    подложка кнопки, центрируется
//   26502  Package_UnpackBtn          Package_UnpackBtn + _Text       «Confirm» (99300) / «Open Package» (99305)
//   26503  Package_Contents_AType     Package_Contents_AType_grp      МЁРТВЫЙ (0 ссылок в IDA) — не трогаем
//   26510+20r  Package_Bag{r}         Package_Bag{r}_grp              рамка блока r (её видимость = «блок показан»)
//   26511+20r  Package_BagIcon{r}     Package_BagIcon{r}_grp          ячейка k=0 (только вложенная коробка)
//   26701+10r  Package_ItemIcon{r}_0  Package_ItemIcon{r}_0_grp       ячейка k=1
//   26703+10r  Package_ItemIcon{r}_1  …_1_grp                         ячейка k=2
//   26705+10r  Package_ItemIcon{r}_2  …_2_grp                         ячейка k=3
//   26513+20r+2c Package_BagItemIcon{r}_{c}  …_grp                    нижние ячейки c=0…4
//
// ★★★ ДЕФЕКТЫ idc В .JUI (idnum — авторитетный ключ, имена в WBP от них пострадали):
//   • 26721 `Package_ItemIcon2\x1f_0` — управляющий байт ⇒ узел зовётся `Package_ItemIcon2__0`;
//   • 26743/26745 названы `Package_ItemIcon3_1`/`3_2` (должно быть `4_1`/`4_2`) ⇒ импортёр развёл
//     дубли в `Package_ItemIcon3_1_grp_1` / `…3_2_grp_1`;
//   • 26542 названо `Package_BagItemName1_5` (должно быть `1_4`).
//   ⇒ БИНДИМ НЕ ПО ИМЕНИ, а по структуре: внутри `Package_Bag{r}_grp` берём дочерние канвасы и
//     раскладываем их по СМЕЩЕНИЮ слота (оно приходит прямо из .JUI и дефектами не задето).
//     Это и есть защита от миснеймов (закон `false-negatives-method`).
#include "UI/AsdaPackagePanelSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U2/U13 (дедуп №2, В2-A): MsgBox и ConsoleEcho — общие точки нотисов и эха
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI

#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ 31.10.2026: щелчок по бустеру в составе открывает окно «Вид» 15600
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaLocIds.h"                // нотис 2288 — подпись кнопки ONEBUT_MSGBOX
#include "UI/AsdaMessageSubsystem.h"      // канал ленты-фолбэка, если WBP модалки не загрузился
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPkgPanel, Log, All);

namespace
{
	const FName kWindowId(TEXT("Package_Contents_Win"));
	const FName kZoneId(TEXT("PACKAGE_CONTENTS"));
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Package_Contents_Win");

	// Нотисы (`error list.bin`, проверены построчно — см. §6 отчёта агента-реверсера):
	//   99300 «Confirm» · 99305 «Open Package» · 99306 «…[sd1] slots… [sd2] slots…».
	constexpr int32 kLocBtnPreview = 99300;
	constexpr int32 kLocBtnUnpack  = 99305;
	constexpr int32 kLocNoRoom     = 99306;

	// Смещения дочерних канвасов внутри `Package_Bag{r}_grp` — прямо из .JUI (Origin.y).
	//   Верхние ячейки: k=0 и k=1 ЛЕЖАТ ДРУГ НА ДРУГЕ (обе y=4) — различаются только тем, что k=0
	//   это `BagIcon` (вложенная коробка), а k=1 — `ItemIcon…_0`. k=2 → y=94, k=3 → y=184.
	constexpr float kBigCellY[UAsdaPackagePanelSubsystem::kBigCells] = { 4.f, 4.f, 94.f, 184.f };
	// Нижние ячейки c=0…4: y = 93, 129, 165, 201, 237 (шаг 36).
	constexpr float kSmallCellY0   = 93.f;
	constexpr float kSmallCellStep = 36.f;

	// ★★★ Константы `kNameOffset (0,68)` / `kNameSize (276,20)` УДАЛЕНЫ 2026-08-27 вместе с
	//   рантайм-достройкой подписей: геометрия переехала в сам ассет (см. `SetupControls`).
	//   Ширина подписи по-прежнему нужна как ПРИЗНАК: подпись отличается от «числа на иконке»
	//   именно ей (276 против 64), по этому и опознаётся `Cell.Name` ниже.
	constexpr float kNameWidthPx = 276.f;

	// Размер отрисовки иконок (эталон `CControl_DrawTextureRect(..., 64, 64)` у больших ячеек и
	//   `..., 32, 32` у нижних — `item_enhance.c:3444/3502`).
	constexpr float kBigIconPx   = 64.f;
	constexpr float kSmallIconPx = 32.f;

	// Найти дочерний канвас панели, чей слот стоит на заданном Y (с допуском на округление).
	UCanvasPanel* ChildCanvasAtY(UPanelWidget* Parent, float Y, bool bWantBagIcon, bool* bOutIsBagIcon = nullptr)
	{
		if (!Parent) { return nullptr; }
		for (int32 i = 0; i < Parent->GetChildrenCount(); ++i)
		{
			UCanvasPanel* C = Cast<UCanvasPanel>(Parent->GetChildAt(i));
			if (!C) { continue; }
			const UCanvasPanelSlot* S = AsdaUi::SlotOf(C);
			if (!S) { continue; }
			if (!FMath::IsNearlyEqual(S->GetOffsets().Top, Y, 1.f)) { continue; }
			// Ячейки k=0 и k=1 стоят на одном Y — различаем по имени узла: у k=0 в имени «BagIcon».
			const bool bIsBagIcon = C->GetName().Contains(TEXT("BagIcon"));
			if (bIsBagIcon != bWantBagIcon) { continue; }
			if (bOutIsBagIcon) { *bOutIsBagIcon = bIsBagIcon; }
			return C;
		}
		return nullptr;
	}
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaPackagePanelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `Package_Contents_Win` (IDNum 26500) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `UIPackageStampWnd_Init@0xDAEA20`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPackagePanelSubsystem::ClosePanel));
	}
	Blocks.SetNum(kBlocks);
	for (FAsdaPackageBlockUi& B : Blocks)
	{
		B.Big.SetNum(kBigCells);
		B.Small.SetNum(kSmallCells);
	}
	UE_LOG(LogAsdaPkgPanel, Log,
		TEXT("[пакеты] подсистема поднята: окно 26500, %d блоков × (%d больших + %d малых) ячеек"),
		kBlocks, kBigCells, kSmallCells);
}

void UAsdaPackagePanelSubsystem::Deinitialize()
{
	UnregisterZones();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kWindowId);
		}
	}
	PanelWidget = nullptr;
	bReady = false;
	bPanelVisible = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaPackagePanelSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (bPanelVisible) { ClosePanel(); }
	StampMode = 0;
	SelItemCode = 0;
	UsedBlocks = 0;
	bSendArmed = false;               // «отправка взведена» — защёлка одного открытия коробки
	bSendAllow = false;
	ShortSpend = 0; ShortBag = 0;
	(void)Reason;
}

// ═══════════════════════════════ СОЗДАНИЕ И ПРИВЯЗКА ═══════════════════════════════════════════
bool UAsdaPackagePanelSubsystem::EnsurePanel()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kPanelZ).Ensure(GetGameInstance(), PanelWidget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			UnregisterZones();
			WM.CloseWindow(kWindowId);
			PanelWidget = nullptr;
			bReady = false;
			bPanelVisible = false;
		},
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			RegisterZones();
			// ★ ТРЕБОВАНИЕ ВЛАДЕЛЬЦА 2026-08-26: окна коробок/бустеров ВСЕГДА поверх остальных окон —
			//   иначе панель содержимого открывается позади инвентаря, из которого её и вызвали.
			//   Полоса приоритетных z и обоснование — у `kPriorityZ` в `AsdaWindowSubsystem.h`.
			WM.SetWindowPriority(kWindowId, true);
			// Эталон держит панель скрытой до заполнения (.JUI Visible="0"; показ — `item_enhance.c:3592`).
		},
		[]()
		{
			UE_LOG(LogAsdaPkgPanel, Error, TEXT("[пакеты] WBP_Package_Contents_Win не загрузился (%s)"), kWbpPath);
		},
		[this](UAsdaWindowSubsystem&)
		{
			bPanelVisible = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R);
}

// ★★★ ДОСТРОЙКА ПОДПИСЕЙ ИМЁН УБРАНА — УЗЛЫ ПЕРЕЕХАЛИ В АССЕТ (2026-08-27).
//   ЧТО БЫЛО ЗДЕСЬ. Метод `EnsureNameLabel(UCanvasPanel* Parent, const FString& NodeName)`:
//   искал в канвасе ячейки готовую подпись по ширине слота, а если не находил — создавал
//   `UTextBlock` с off=(0, 68, 276, 20), клонировал шрифт и цвет у `Package_BagName0_Text`,
//   ставил центровку, запрет переноса и `SelfHitTestInvisible`.
//   ПОЧЕМУ УБРАНО. Геометрия — две константы, стиль — копия соседа; ничего
//   рантайм-зависимого (меняется только САМ текст, и его кладёт `Refresh*`).
//   ГДЕ ЖИВЁТ ТЕПЕРЬ. 19 узлов (`Package_BagName{1..4}_Text` и `Package_ItemName{r}_{k}_Text`)
//   лежат в `WBP_Package_Contents_Win`; создаёт их шаг `Tools/asda_ui_geometry_bake.py` (блок G5),
//   он в `Tools/asda_ui_post_import.STEPS`, сторожа — в `HANDMADE`.
//   ★ Подсистема ищет подпись НЕ ПО ИМЕНИ, а по ШИРИНЕ слота (276 против 64 у числа
//   на иконке) — см. цикл в `SetupControls`. Поэтому запечённые узлы подхватываются
//   штатным путём, и имена могут отличаться от старых `Package_Name{r}_{k}_RT`.

void UAsdaPackagePanelSubsystem::SetupControls()
{
	bReady = false;
	if (!PanelWidget) { return; }
	// ★★★ 04.11.2026 (баг владельца «после смены карты в просмотре набора нет иконок», Reports/levelup_potion):
	//   окно пересоздаётся при каждом переходе (движок сносит виджеты мира, `Ensure` открывает WBP заново), а ячейки
	//   ниже привязывались под гардом «иконки ещё нет» (`if (!Cell.Icon)`) — после первой карты гард не пускал новую
	//   картинку, и `FillCell` красил иконку СНЕСЁННОГО окна: подпись в новом окне есть, иконки нет. Самотест
	//   `asda.SelfTest.IconsAfterTravel`. Привязку каждого окна начинаем с чистых ячеек и реле (закон
	//   `session-reset-mapchanged-traps`: массивы контролов в SetupControls — всегда сброс).
	for (FAsdaPackageBlockUi& B : Blocks)
	{
		B.Frame = nullptr;
		for (FAsdaPackageCellUi& C : B.Big)   { C = FAsdaPackageCellUi(); }
		for (FAsdaPackageCellUi& C : B.Small) { C = FAsdaPackageCellUi(); }
	}
	Relays.Reset();
	RootCanvas = Cast<UCanvasPanel>(PanelWidget->GetRootWidget());

	BoxNameLabel = Cast<UTextBlock>(AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_Contents_Move"), nullptr));
	TitleBar     = AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_Contents_Title_grp"), nullptr);
	BtnBase      = AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_Contents_BtnBase_grp"), nullptr);
	BtnUnpack    = Cast<UButton>(AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_UnpackBtn"), nullptr));
	BtnUnpackCap = Cast<UTextBlock>(AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_UnpackBtn_Text"), nullptr));
	BtnClose     = Cast<UButton>(AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_ClsBtn"), nullptr));

	// ★ Эталон ставит метке имени коробки флаг стиля 0x400000 = ТЕНЬ (`ui_core.c:63043`,
	//   `CClass_SetField926(pSowelItemNameLabel, 0x400000)`). Импортёр тень из .JUI не переносит —
	//   её ставит КОД, поэтому включаем здесь. Та же константа, что у строк чата и окна хвастовства.
	if (BoxNameLabel)
	{
		BoxNameLabel->SetShadowOffset(FVector2D(1.f, 1.f));
		BoxNameLabel->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.65f));
		BoxNameLabel->SetAutoWrapText(false);   // закон `ui-autowrap-canvas-gotcha`
	}

	// ── Пять блоков ──
	for (int32 r = 0; r < kBlocks; ++r)
	{
		FAsdaPackageBlockUi& Blk = Blocks[r];
		UPanelWidget* BagGrp = Cast<UPanelWidget>(
			AsdaUi::FindNodeByName(PanelWidget, *FString::Printf(TEXT("Package_Bag%d_grp"), r), nullptr));
		Blk.Frame = BagGrp;
		if (!BagGrp)
		{
			UE_LOG(LogAsdaPkgPanel, Warning, TEXT("[пакеты] блок %d: узла Package_Bag%d_grp нет в WBP"), r, r);
			continue;
		}

		// Верхние 4 ячейки. k=0 — «BagIcon» (вложенная коробка), k=1…3 — «ItemIcon».
		for (int32 k = 0; k < kBigCells; ++k)
		{
			FAsdaPackageCellUi& Cell = Blk.Big[k];
			Cell.IdNum = (k == 0) ? (26511 + 20 * r) : (26701 + 10 * r + 2 * (k - 1));
			UCanvasPanel* Grp = ChildCanvasAtY(BagGrp, kBigCellY[k], /*bWantBagIcon*/ k == 0);
			if (!Grp)
			{
				UE_LOG(LogAsdaPkgPanel, Warning,
					TEXT("[пакеты] блок %d ячейка k=%d (idnum %d): канвас на Y=%.0f не найден"),
					r, k, Cell.IdNum, kBigCellY[k]);
				continue;
			}
			// Порядок детей канваса ячейки задан импортёром: [0] иконка-Image, [1] число-TextBlock,
			//   [2] подложка подписи-Image, [3] подпись-TextBlock (её может не быть — создаём).
			for (int32 i = 0; i < Grp->GetChildrenCount(); ++i)
			{
				UWidget* Ch = Grp->GetChildAt(i);
				if (UImage* Img = Cast<UImage>(Ch))
				{
					const UCanvasPanelSlot* S = AsdaUi::SlotOf(Img);
					// Иконка — квадрат 64×64; подложка подписи — 276×20. Различаем по ширине.
					if (!Cell.Icon && S && S->GetOffsets().Right <= kBigIconPx + 1.f) { Cell.Icon = Img; }
				}
				else if (UTextBlock* T = Cast<UTextBlock>(Ch))
				{
					const UCanvasPanelSlot* S = AsdaUi::SlotOf(T);
					if (S && S->GetOffsets().Right <= kBigIconPx + 1.f) { Cell.Count = T; }
					else { Cell.Name = T; }
				}
			}
			// ★ Подпись больше НЕ ДОСТРАИВАЕТСЯ (2026-08-27): 19 узлов запечены в ассет
			//   шагом `Tools/asda_ui_geometry_bake.py` (G5) — цикл выше находит их по ширине слота.
			if (!Cell.Name)
			{
				UE_LOG(LogAsdaPkgPanel, Warning,
					TEXT("[пакеты] блок %d ячейка k=%d: подпись шириной %.0f px не найдена в ассете — "
					     "прогони Tools/asda_ui_geometry_bake.py (шаг G5)"), r, k, kNameWidthPx);
			}
			if (Cell.Count) { Cell.Count->SetAutoWrapText(false); }
			// Иконка — цель хит-теста подсказки: ей нужна ловящая курсор видимость.
			if (Cell.Icon) { Cell.Icon->SetVisibility(ESlateVisibility::Visible); }
		}

		// Нижние 5 ячеек.
		for (int32 c = 0; c < kSmallCells; ++c)
		{
			FAsdaPackageCellUi& Cell = Blk.Small[c];
			Cell.IdNum = 26513 + 20 * r + 2 * c;
			UCanvasPanel* Grp = ChildCanvasAtY(BagGrp, kSmallCellY0 + kSmallCellStep * c, /*bWantBagIcon*/ false);
			if (!Grp)
			{
				UE_LOG(LogAsdaPkgPanel, Warning,
					TEXT("[пакеты] блок %d нижняя ячейка c=%d (idnum %d): канвас на Y=%.0f не найден"),
					r, c, Cell.IdNum, kSmallCellY0 + kSmallCellStep * c);
				continue;
			}
			for (int32 i = 0; i < Grp->GetChildrenCount(); ++i)
			{
				UWidget* Ch = Grp->GetChildAt(i);
				if (UImage* Img = Cast<UImage>(Ch)) { if (!Cell.Icon) { Cell.Icon = Img; } }
				else if (UTextBlock* T = Cast<UTextBlock>(Ch))
				{
					const UCanvasPanelSlot* S = AsdaUi::SlotOf(T);
					// Число на иконке — слот 32 px, подпись — 158 px.
					if (S && S->GetOffsets().Right <= kSmallIconPx + 1.f) { Cell.Count = T; }
					else { Cell.Name = T; }
				}
			}
			if (Cell.Name) { Cell.Name->SetAutoWrapText(false); }   // закон `ui-autowrap-canvas-gotcha`
			if (Cell.Count) { Cell.Count->SetAutoWrapText(false); }
			if (Cell.Icon)  { Cell.Icon->SetVisibility(ESlateVisibility::Visible); }
		}
	}

	// ── Кнопки ──
	auto Bind = [this](UButton* Btn, int32 Index, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, Index, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnUnpack, 26502, [this](int32) { OnUnpackButton(); });
	Bind(BtnClose,  26506, [this](int32) { OnCloseButton();  });

	int32 CellsBound = 0;
	for (const FAsdaPackageBlockUi& B : Blocks)
	{
		for (const FAsdaPackageCellUi& C : B.Big)   { if (C.Icon) { ++CellsBound; } }
		for (const FAsdaPackageCellUi& C : B.Small) { if (C.Icon) { ++CellsBound; } }
	}
	bReady = (BtnUnpack != nullptr && BtnClose != nullptr && CellsBound > 0);
	UE_LOG(LogAsdaPkgPanel, Log,
		TEXT("[пакеты] окно 26500 разобрано: имя=%s, титул=%s, подложка=%s, 26502=%s, 26506=%s, ячеек=%d из %d"),
		BoxNameLabel ? TEXT("да") : TEXT("НЕТ"), TitleBar ? TEXT("да") : TEXT("НЕТ"),
		BtnBase ? TEXT("да") : TEXT("НЕТ"), BtnUnpack ? TEXT("да") : TEXT("НЕТ"),
		BtnClose ? TEXT("да") : TEXT("НЕТ"), CellsBound, kBlocks * (kBigCells + kSmallCells));
}

// ═══════════════════════════════ ЗОНЫ МЫШИ ═════════════════════════════════════════════════════
// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (волна booster-loot, тот же дефект, что у барабана бустера 10050): зона окна состава 26500 —
//   девять планов рамки `Package_Contents_Win_0_0…_0_8`, а не `RootCanvas` (корень WBP — холст на весь вьюпорт). Прежняя зона
//   занимала весь экран: пока окно открыто (вскрытие коробки, превью), ПКМ-орбита камеры не стартовала, а клики/колесо в мир
//   считались «над окном». Ключ 0 — прежняя зона по корню.
static ASDA_DEV_CVAR(int32, CVarPackageZoneFrame,
	TEXT("asda.Package.ZoneFrame"), 1,
	TEXT("1 — зона окна состава коробки 26500 = рамка окна (вне рамки ПКМ вращает камеру, клик и колесо идут в мир); "
	     "0 — прежний порт: зона = корень WBP во весь экран."),
	ECVF_Default);

bool UAsdaPackagePanelSubsystem::GetPanelRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bPanelVisible || !RootCanvas) { return false; }
	if (CVarPackageZoneFrame.GetValueOnGameThread() != 0)
	{
		return AsdaUi::FramePlanesAbsRect(PanelWidget, TEXT("Package_Contents_Win_0_%d"), 9, OutMin, OutMax);
	}
	return AsdaUi::WidgetAbsRect(RootCanvas, OutMin, OutMax);
}

void UAsdaPackagePanelSubsystem::RegisterZones()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RegisterUiZone(kZoneId, FAsdaUiZoneRect::CreateUObject(this, &UAsdaPackagePanelSubsystem::GetPanelRect));
	}
}

void UAsdaPackagePanelSubsystem::UnregisterZones()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kZoneId);
	}
}

// ═══════════════════════════════ ОЧИСТКА ЯЧЕЕК ═════════════════════════════════════════════════
// Порт `item_enhance.c:3314-3338`: код = −1, пустая текстура, пустые подписи, ОБА контрола скрыты.
void UAsdaPackagePanelSubsystem::HideCell(FAsdaPackageCellUi& Cell)
{
	Cell.ItemCode = -1;
	UGameInstance* GI = GetGameInstance();
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ClearIcon(Cell.Icon);   // общая точка: пустая текстура (закон `port-shared-points`)
	}
	if (Cell.Icon)  { Cell.Icon->SetVisibility(ESlateVisibility::Collapsed); }
	if (Cell.Name)  { Cell.Name->SetText(FText::GetEmpty()); Cell.Name->SetVisibility(ESlateVisibility::Collapsed); }
	if (Cell.Count) { Cell.Count->SetText(FText::GetEmpty()); Cell.Count->SetVisibility(ESlateVisibility::Collapsed); }
}

void UAsdaPackagePanelSubsystem::ClearAllCells()
{
	for (FAsdaPackageBlockUi& B : Blocks)
	{
		for (FAsdaPackageCellUi& C : B.Big)   { HideCell(C); }
		for (FAsdaPackageCellUi& C : B.Small) { HideCell(C); }
		if (B.Frame) { B.Frame->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

// ═══════════════════════════════ ЗАПОЛНЕНИЕ ОДНОЙ ЯЧЕЙКИ ═══════════════════════════════════════
// Порт `item_enhance.c:3426-3491`: три ветки по трём таблицам, число >1 пишется НА ИКОНКУ,
//   а подпись = «имя» либо «имя (N)».
bool UAsdaPackagePanelSubsystem::FillCell(FAsdaPackageCellUi& Cell, int32 ItemCode, int32 Amount)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!IT || !Inv || ItemCode <= 0) { return false; }

	// Эталон пробует ТРИ таблицы подряд; ни одна не дала записи ⇒ позиция пропускается и ячейка
	//   остаётся скрытой (`item_enhance.c:3367-3370`).
	const bool bKnown = (IT->GetItem(ItemCode) != nullptr) || IT->IsSpecialItemCode(ItemCode) || IT->IsSowelCode(ItemCode);
	if (!bKnown) { return false; }

	Cell.ItemCode = ItemCode;
	// Иконка: у больших ячеек эталон рисует 64×64, у нижних — 32×32 (размер берём из слота).
	float DrawPx = kSmallIconPx;
	if (Cell.Icon)
	{
		if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(Cell.Icon))
		{
			DrawPx = FMath::Max(1.f, S->GetOffsets().Right);
		}
		// ★ 04.11.2026 (баг владельца «у состава набора нет иконок», Reports/levelup_potion): неудача отрисовки больше
		//   не молчит — в журнал (и в журнал ритейла) уходят код, запись иконки и атлас, чтобы причину было видно у игрока.
		if (!Inv->ApplyIconToImage(Cell.Icon, ItemCode, DrawPx))
		{
			const FAsdaItemIcon Ic = IT->GetItemIconSelf(ItemCode); // ★ 08.11.2026: атлас пола своего персонажа, как эталон `g_ItemImgOptionVariant`
			UE_LOG(LogAsdaPkgPanel, Warning,
				TEXT("[пакеты] ячейка %d: иконка предмета %d не нарисована — запись иконки %s, атлас «%s» (%d,%d,%d)"),
				Cell.IdNum, ItemCode, Ic.IsValid() ? TEXT("есть") : TEXT("НЕТ"), *Ic.Atlas, Ic.CellX, Ic.CellY, Ic.CellSize);
		}
		Cell.Icon->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		UE_LOG(LogAsdaPkgPanel, Warning, TEXT("[пакеты] ячейка %d: виджета иконки нет — предмет %d без иконки"), Cell.IdNum, ItemCode);
	}
	// ★ Число ПРЯМО НА ИКОНКЕ — только при количестве > 1 (`item_enhance.c:3445-3449`).
	if (Cell.Count)
	{
		if (Amount > 1)
		{
			Cell.Count->SetText(FText::AsNumber(Amount));
			Cell.Count->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			Cell.Count->SetText(FText::GetEmpty());
			Cell.Count->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	// Подпись: «имя» либо «имя (N)» (`item_enhance.c:3450-3459`).
	if (Cell.Name)
	{
		const FString Nm = IT->GetItemName(ItemCode);
		Cell.Name->SetText(FText::FromString(Amount > 1 ? FString::Printf(TEXT("%s (%d)"), *Nm, Amount) : Nm));
		Cell.Name->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	return true;
}

// ★ 11.11.2026 (А1 red_triage) [ОРИГИНАЛ] пустая строка `packagetable` — окно состава ВСЁ РАВНО показывается.
//   Эталон `stampWnd_PopulateItemSlots` (item_enhance.c:3354-3600) ветвится только по НАЙДЕННОЙ строке
//   (`charDataFindPackAgeRecord != 0`): пустые позиции (код 0/−1) пропускает и показывает окно с именем коробки
//   без ячеек. Порт отказывал, если в строке нет ни одной позиции, — двойной щелчок по коробкам 22747, 56032,
//   68764, 68768, 68781, 75212, 75547 (состава нет ни в эталонном packagetable, ни в серверном PackageItem_Table)
//   ничего не открывал. То же для вложенной коробки (item_enhance.c:3372-3380). 1 — как эталон, 0 — прежний отказ.
static ASDA_DEV_CVAR(int32, CVarPackageEmptyRowShowsPanel,
	TEXT("asda.Package.EmptyRowShowsPanel"), 1,
	TEXT("1 (по умолчанию) — коробка с найденной, но пустой строкой packagetable открывает пустое окно состава, как эталон ")
	TEXT("(stampWnd_PopulateItemSlots). 0 — прежнее поведение порта: окно не открывается."),
	ECVF_Default);
// Аксессор ключа для резерва ячеек вскрытия (`UAsdaInventorySubsystem::ReservePackageSlots`, другой .cpp).
bool AsdaPackageEmptyRowShowsPanelOn() { return CVarPackageEmptyRowShowsPanel.GetValueOnGameThread() != 0; }

// ═══════════════════════════════ ★★★ ЗАПОЛНЕНИЕ ПАНЕЛИ ════════════════════════════════════════
// Полный порт `stampWnd_PopulateItemSlots@0xDACF10` (`item_enhance.c:3212-3601`).
bool UAsdaPackagePanelSubsystem::Populate(int32 ItemCode, int32 InStampMode,
                                          int32 SrcContainer, int32 SrcSlot, bool bShowTooltip)
{
	if (!EnsurePanel() || !bReady) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }

	// Шаг 2 эталона: очистка ВСЕХ ячеек всех блоков — до всякой проверки.
	ClearAllCells();

	// Шаг 3: режим и подпись кнопки 26502.
	StampMode = InStampMode;
	if (BtnUnpackCap)
	{
		const int32 Loc = (StampMode == 1) ? kLocBtnUnpack : kLocBtnPreview;
		const FString Cap = AsdaUi::LocRaw(GI, Loc, /*bDebugFallback*/ false);
		if (!Cap.IsEmpty()) { BtnUnpackCap->SetText(FText::FromString(Cap)); }
	}

	// Шаг 4: запись предмета + имя коробки в метку 26501; гейт `itemSubType ∈ {200, 201}`.
	const FAsdaItemRecord* Rec = IT->GetItem(ItemCode);
	if (!Rec) { return false; }
	if (BoxNameLabel) { BoxNameLabel->SetText(FText::FromString(IT->GetItemName(ItemCode))); }
	if (Rec->ItemSubType != 200 && Rec->ItemSubType != 201)
	{
		UE_LOG(LogAsdaPkgPanel, Log,
			TEXT("[пакеты] предмет %d: itemSubType=%d ∉ {200,201} — эталон панель не показывает"),
			ItemCode, Rec->ItemSubType);
		return false;
	}

	// Шаг 5: строка `packagetable` по полю `DurationOrEnchant`.
	TArray<FAsdaPackageEntry> Contents;
	const bool bEmptyRowShows = CVarPackageEmptyRowShowsPanel.GetValueOnGameThread() != 0;   // ключ А1 (см. выше)
	if (!IT->GetPackageContents(ItemCode, Contents) && !(bEmptyRowShows && IT->IsPackageItem(ItemCode)))
	{
		UE_LOG(LogAsdaPkgPanel, Log,
			TEXT("[пакеты] предмет %d: строки packagetable нет (DurationOrEnchant=%d) — панель не показываем"),
			ItemCode, Rec->DurationOrEnchant);
		return false;
	}

	SelItemCode  = ItemCode;
	SelContainer = SrcContainer;
	SelSlot      = SrcSlot;

	// ── Шаг 6: раскладка пяти записей ──
	//   Счётчики эталона (имена переменных в декомпиле ПЕРЕПУТАНЫ местами, здесь — по смыслу):
	//     BlockIdx (эталонный `slotColumn`) — индекс блока 0…4;
	//     BigIdx   (эталонный `slotRow`)    — индекс ВЕРХНЕЙ ячейки 0…3.
	int32 BlockIdx = 0;
	int32 BigIdx = 0;
	bool bRowHasPackage = false;

	for (int32 s = 0; s < kPkgSlots; ++s)
	{
		if (!Contents.IsValidIndex(s)) { continue; }
		const int32 Code = Contents[s].ItemCode;
		const int32 Amount = Contents[s].Amount;
		if (Code <= 0) { continue; }

		const FAsdaItemRecord* SubRec = IT->GetItem(Code);
		const bool bKnown = (SubRec != nullptr) || IT->IsSpecialItemCode(Code) || IT->IsSowelCode(Code);
		if (!bKnown) { continue; }   // эталон такую позицию просто пропускает

		// ★ Вложенная коробка: своя строка packagetable ⇒ занимает НОВЫЙ блок и ячейку k=0.
		TArray<FAsdaPackageEntry> Nested;
		const bool bNested = SubRec
			&& (SubRec->ItemSubType == 200 || SubRec->ItemSubType == 201)
			&& (IT->GetPackageContents(Code, Nested) || (bEmptyRowShows && IT->IsPackageItem(Code)));

		if (bNested)
		{
			if (s != 0) { ++BlockIdx; }
			BigIdx = 0;
			bRowHasPackage = true;
		}
		else
		{
			// Обычная запись: ячейки 1…3; на четвёртой — переход в следующий блок.
			++BigIdx;
			if (BigIdx == kBigCells && !bRowHasPackage) { ++BlockIdx; BigIdx = 1; }
			if (bRowHasPackage) { bRowHasPackage = false; ++BlockIdx; BigIdx = 1; }
		}
		if (!Blocks.IsValidIndex(BlockIdx) || !Blocks[BlockIdx].Big.IsValidIndex(BigIdx))
		{
			UE_LOG(LogAsdaPkgPanel, Warning,
				TEXT("[пакеты] позиция %d предмета %d вышла за раскладку (блок %d, ячейка %d) — пропуск"),
				s, ItemCode, BlockIdx, BigIdx);
			continue;
		}

		FillCell(Blocks[BlockIdx].Big[BigIdx], Code, Amount);

		// ── Шаг 7: содержимое вложенной коробки — в НИЖНИЕ 5 ячеек ТОГО ЖЕ блока.
		//   Глубина ровно ДВА уровня, третьего эталон не делает.
		if (bNested)
		{
			for (int32 j = 0; j < kSmallCells && j < Nested.Num(); ++j)
			{
				FillCell(Blocks[BlockIdx].Small[j], Nested[j].ItemCode, Nested[j].Amount);
			}
		}
	}

	// ── Шаг 8: показать блоки 0…BlockIdx ──
	for (int32 r = 0; r < kBlocks; ++r)
	{
		if (Blocks[r].Frame)
		{
			Blocks[r].Frame->SetVisibility(r <= BlockIdx ? ESlateVisibility::SelfHitTestInvisible
			                                             : ESlateVisibility::Collapsed);
		}
	}
	UsedBlocks = BlockIdx;

	// ── Шаг 9: геометрия + показ + центрирование ──
	ApplyGeometry(BlockIdx);

	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kWindowId, true);
		WM->BringWindowToFront(kWindowId);
	}
	bPanelVisible = true;
	++CntPopulate;
	PopulateFrame = GFrameCounter;   // ★ 31.10.2026: см. `TickCellClick`

	// Шаг 10: эталонный 4-й аргумент — `positionHoverTooltipWnd@0xDB3900` (`item_inventory.c:54313`): панель уезжает к правому
	//   краю, рядом встаёт окно «Вид» 15600. ★ 31.10.2026: «hover-подсказка» — НЕВЕРНОЕ имя IDA: `g_pHoverTooltipWnd` (0x02BDF5B8) —
	//   это корень окна «Вид» 15600, поле `+616*4` синглтона `CBoosterShop` (доказательство адресами — шапка
	//   `UAsdaBoosterPanelSubsystem`, блок «ОКНО «ВИД»/«ПРОСМОТР»»). Вход с аргументом 1 — щелчок по набору внутри «Вида»
	//   (`uiInventorySlotUseItem@0xB86EE0`) и наведение на слот экипировки (`ui_core.c:10421`, в порту не перенесено).
	if (bShowTooltip)
	{
		if (UAsdaBoosterPanelSubsystem::PrevPathsOn()) { DockBesidePrevWindow(); }
		else
		{
			UE_LOG(LogAsdaPkgPanel, Log,
				TEXT("[пакеты] bShowTooltip=1: стыковка с окном «Вид» выключена ключом входов «Вида» — шаг пропущен"));
		}
	}

	UE_LOG(LogAsdaPkgPanel, Log,
		TEXT("[пакеты] панель 26500 заполнена: коробка %d («%s»), режим=%d, блоков=%d, выбор=%d:%d"),
		ItemCode, *IT->GetItemName(ItemCode), StampMode, BlockIdx + 1, SelContainer, SelSlot);
	return true;
}

// ★★★ Правый край, ОТНОСИТЕЛЬНО КОТОРОГО живёт крестик 26506, в системе координат ЕГО РОДИТЕЛЯ.
//   Зачем: импортёр расплющил иерархию `.JUI` НЕ до конца — часть контролов осталась внутри групп
//   (`*_grp`). У такого узла `Offsets.Left` отсчитывается ВНУТРИ ГРУППЫ, а не в корневом канвасе,
//   и если поставить ему абсолютную координату окна, он уедет на ширину группы (ровно это и
//   случилось: крестик оказался правее окна). Поэтому:
//     • крестик лежит прямо в корне  → правый край = левый край рамки + ширина окна;
//     • крестик лежит внутри группы → правый край = ШИРИНА САМОЙ ГРУППЫ (её локальные координаты).
float UAsdaPackagePanelSubsystem::CloseParentRightEdge(float WindowWidth) const
{
	if (!BtnClose) { return WindowWidth; }
	const UPanelWidget* Par = BtnClose->GetParent();
	const bool bInRoot = (Par == nullptr) || (Par == RootCanvas);
	if (bInRoot)
	{
		float BaseX = 10.f;
		if (const UWidget* F0 = AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_Contents_Win_0_0"), nullptr))
		{
			if (const UCanvasPanelSlot* SF = AsdaUi::SlotOf(F0)) { BaseX = SF->GetOffsets().Left; }
		}
		return BaseX + WindowWidth;
	}
	// Внутри группы: правый край = её собственная ширина (Offsets.Right при нулевых якорях).
	if (const UCanvasPanelSlot* PS = AsdaUi::SlotOf(Par))
	{
		const float PW = PS->GetOffsets().Right;
		if (PW > 0.f) { return PW; }
	}
	return WindowWidth;
}

// ═══════════════════════════════ ГЕОМЕТРИЯ ═════════════════════════════════════════════════════
// Порт `item_enhance.c:3571-3598`. Ширина растёт по числу занятых блоков, окно центрируется.
//   ⚠ Импортёр РАСПЛЮЩИЛ иерархию .JUI: все «дети окна» лежат СЁСТРАМИ в корневом канвасе с
//     абсолютными координатами базы 1024×768. Поэтому «подвинуть окно» = сдвинуть ВСЕ узлы корня на
//     одну дельту (тот же способ, что у меню хвастовства `PlaceMenuAt`), а НЕ звать
//     `SetPositionInViewport` — виджет растянут на весь вьюпорт, и это увело бы окно вдвое.
//     Заодно так мы не конфликтуем с перетаскиванием: оно двигает те же слоты (закон
//     `ui-window-drag-hittest`), и сдвиги складываются.
void UAsdaPackagePanelSubsystem::ApplyGeometry(int32 InUsedBlocks)
{
	if (!PanelWidget) { return; }
	const float TotalW = kGridCellWidth + kGridCellWidth * (float)InUsedBlocks;   // 284*(blocks+1)
	const float CenterW = kGridBaseX + kGridCellWidth * (float)InUsedBlocks;      // 178 + 284*blocks

	auto SlotOf = [this](const TCHAR* Name) -> UCanvasPanelSlot*
	{
		UWidget* W = AsdaUi::FindNodeByName(PanelWidget, Name, nullptr);
		return W ? AsdaUi::SlotOf(W) : nullptr;
	};
	auto SetWidth = [](UWidget* W, float NewW)
	{
		if (!W) { return; }
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(W))
		{
			FMargin M = S->GetOffsets();
			M.Right = NewW;                 // Offsets.Right при нулевых якорях = ШИРИНА
			S->SetOffsets(M);
		}
	};

	// ── Рамка окна: девять планов `Package_Contents_Win_0_0…_0_8`, сетка 3×3, углы по 2 px.
	//    Считаем ОТ ТЕКУЩЕЙ позиции левого плана, чтобы формула не зависела от того, куда окно
	//    уже сдвинуто центрированием или перетаскиванием.
	float BaseX = 10.f;
	if (const UCanvasPanelSlot* S0 = SlotOf(TEXT("Package_Contents_Win_0_0"))) { BaseX = S0->GetOffsets().Left; }
	// ★ 09.11.2026 (пачка 19, баг 4; `asda.UI.FrameKeepOverlap`): средняя колонка сохраняет заход под углы из ассета (шаг швов
	//   пост-импорта) — прежде ставилась встык, и на дробной позиции окна между колонками был просвет 1 px.
	const bool bKeepOv = AsdaUi::FrameKeepOverlapOn();
	const UCanvasPanelSlot* SL0 = SlotOf(TEXT("Package_Contents_Win_0_0"));
	const UCanvasPanelSlot* SL2 = SlotOf(TEXT("Package_Contents_Win_0_2"));
	const float CurMidL = SL0 ? SL0->GetOffsets().Left + SL0->GetOffsets().Right : BaseX + 2.f;   // край левой колонки встык
	const float CurMidR = SL2 ? SL2->GetOffsets().Left : CurMidL;                               // начало правой колонки
	for (int32 i = 0; i < 9; ++i)
	{
		UCanvasPanelSlot* S = SlotOf(*FString::Printf(TEXT("Package_Contents_Win_0_%d"), i));
		if (!S) { continue; }
		FMargin M = S->GetOffsets();
		const int32 Col = i % 3;
		if (Col == 1)
		{
			const float L = bKeepOv ? FMath::Max(0.f, CurMidL - M.Left) : 0.f;
			const float R = bKeepOv ? FMath::Max(0.f, (M.Left + M.Right) - CurMidR) : 0.f;
			M.Left = BaseX + 2.f - L; M.Right = TotalW - 4.f + L + R;
		}
		else if (Col == 2) { M.Left = BaseX + TotalW - 2.f; }
		S->SetOffsets(M);
	}

	// ── Титул-бар 26505 — трёхчастная полоса: левая шапка 53, ЦЕНТР (это и есть 26505) и правая 53.
	//    Эталон растит именно центр: 178 + 284*blocks.
	SetWidth(AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_Contents_Title_1_0"), nullptr), CenterW);
	if (UCanvasPanelSlot* SR = SlotOf(TEXT("WinSm_RBar_4_0")))
	{
		FMargin M = SR->GetOffsets(); M.Left = 53.f + CenterW; SR->SetOffsets(M);
	}
	SetWidth(TitleBar, 53.f + CenterW + 53.f);

	// ── Метка имени коробки 26501: 284*(blocks+1).
	SetWidth(BoxNameLabel, TotalW);

	// ── ★★★ КРЕСТИК 26506 И РУЧКА ПЕРЕТАСКИВАНИЯ — ЕДУТ ЗА ПРАВЫМ КРАЕМ ОКНА ────────────────────
	//   ДЕФЕКТ ПОРТА (жалоба владельца 2026-08-26, скриншот «Daily Attendance Package2»): у широких
	//   наборов крестик оказывался ПОСЕРЕДИНЕ верхней полосы, а тянуть окно можно было только за
	//   ЛЕВУЮ половину шапки.
	//   ПРИЧИНА — не в данных, а в импорте: в оригинале крестик и полоса-ручка ЛЕЖАТ ВНУТРИ
	//   титул-бара 26505, и при росте его ширины едут вместе с ним сами (потому `item_enhance.c:
	//   3571-3598` про них молчит — эталону нечего двигать). Импортёр же расплющил иерархию .JUI:
	//   все узлы стали СЁСТРАМИ в корневом канвасе с абсолютными координатами, и связь с баром
	//   потерялась. Поэтому здесь мы воспроизводим то, что в оригинале делала иерархия.
	//   Отступ крестика от правого края берём АВТОРСКИЙ (замеряем один раз на исходной раскладке),
	//   а не подбираем на глаз.
	{
		const float RightEdge = BaseX + TotalW;                 // правый край окна в юнитах канвы

		// ★★★ СНАЧАЛА РАСТЯГИВАЕМ ГРУППУ-ОБЁРТКУ ШАПКИ, потом ставим то, что внутри неё.
		//   ФАКТ ИЗ ЛОГА (2026-08-26): родитель крестика — `Package_Contents_Move_grp`, и её ширина
		//   так и оставалась авторской (284) при любом размере набора. Крестик внутри честно вставал
		//   по правому краю СВОЕЙ группы — то есть на 262, что у широкого окна и есть середина.
		//   Пока группа не растянута, двигать крестик бессмысленно: он привязан к её краю, а не к окну.
		SetWidth(AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_Contents_Move_grp"), nullptr), TotalW);

		// ★★★ КРЕСТИК: позиция ПРЯМО ИЗ ДАННЫХ `.JUI`, без всяких замеров.
		//   `gamemain_jui.ini`: контрол `Package_ClsBtn` (26506) имеет `Origin = "-22 6"`.
		//   ОТРИЦАТЕЛЬНЫЙ X в .JUI — это привязка к ПРАВОМУ краю родителя: x = ширина родителя − 22.
		//   Импортёр такую привязку не переносит: он один раз пересчитал её в абсолютную координату
		//   при АВТОРСКОЙ ширине окна (284), и она застыла — потому у широких наборов крестик
		//   оказывался посередине. Здесь просто воспроизводим правило данных.
		if (UCanvasPanelSlot* SC = BtnClose ? AsdaUi::SlotOf(BtnClose) : nullptr)
		{
			FMargin New = SC->GetOffsets();
			New.Left = CloseParentRightEdge(TotalW) - kCloseOriginRight;
			SC->SetOffsets(New);
			const UPanelWidget* Par = BtnClose->GetParent();
			UE_LOG(LogAsdaPkgPanel, Log,
				TEXT("[пакеты] крестик 26506: родитель «%s», правый край %.0f, Origin.x=-%.0f → x=%.0f"),
				Par ? *Par->GetName() : TEXT("(корень)"), CloseParentRightEdge(TotalW),
				kCloseOriginRight, New.Left);
		}
		// Ручка перетаскивания — невидимая хит-зона шапки (`*_DRAG`, закон ui-window-drag-hittest).
		//   Тянуть окно нужно за ВСЮ шапку, поэтому растягиваем её на ширину титул-бара.
		if (UWidget* Handle = AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_Contents_Move_101_DRAG"), nullptr))
		{
			if (UCanvasPanelSlot* SH = AsdaUi::SlotOf(Handle))
			{
				FMargin M = SH->GetOffsets();
				// Шапка = левая часть 53 + центр CenterW + правая 53; ручка занимает её целиком,
				//   но НЕ залезает под крестик — иначе клик по нему начинал бы перетаскивание.
				const float BarW = 53.f + CenterW + 53.f;
				// До крестика: он стоит на (ширина − 22), значит ручка тянется до этой точки.
				M.Right = FMath::Max(16.f, BarW - kCloseOriginRight - 2.f);
				SH->SetOffsets(M);
			}
		}
	}

	// ── Подложка кнопки 26507 центрируется: x = (TotalW − своя ширина) / 2, y не меняется.
	if (UCanvasPanelSlot* SB = BtnBase ? AsdaUi::SlotOf(BtnBase) : nullptr)
	{
		FMargin M = SB->GetOffsets();
		M.Left = BaseX + (TotalW - M.Right) * 0.5f;
		SB->SetOffsets(M);
	}

	// ── Центрирование на экране: x = экранW/2 − ширина/2, y = экранH/2 − высота/2.
	//    Опорная точка окна — левый-верхний план рамки; двигаем ВСЕ узлы корня на дельту.
	//    ★ Дедуп №2, В1-B (U16, 11.10.2026): общая точка `AsdaUi::CenterRootOnViewport` (тот же расчёт и тот же сдвиг).
	const UCanvasPanelSlot* SRef = SlotOf(TEXT("Package_Contents_Win_0_0"));
	if (!SRef || !RootCanvas) { return; }
	AsdaUi::CenterRootOnViewport(RootCanvas, SRef, FVector2D(TotalW, kWinHeight), GetWorld());
}

// ═══════════════════════════════ КНОПКИ ════════════════════════════════════════════════════════
// 26506 — крестик. Порт `ui_core.c:62735-62736`: только закрыть, пакетов нет.
void UAsdaPackagePanelSubsystem::OnCloseButton()
{
	ClosePanel();
}

// ★★★ 26502. Порт `uiCashShopButtonDispatch@0xDACAB0` (`ui_core.c:62796-62818`).
void UAsdaPackagePanelSubsystem::OnUnpackButton()
{
	// Режим 0 (превью): кнопка = «Confirm», просто закрывает панель.
	if (StampMode == 0)
	{
		ClosePanel();
		return;
	}
	// Режим 1: гейт «выбор не пуст» (эталонный `*(DWORD*)gap_34`).
	if (SelItemCode <= 0 || SelContainer < 0 || SelSlot < 0)
	{
		UE_LOG(LogAsdaPkgPanel, Log, TEXT("[пакеты] 26502: выбор пуст — эталон в этой ветке ничего не делает"));
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }

	// Порт `itemHasNoEmptySowelSlots@0xDAAAB0` — имя ВРЁТ, true = «места ХВАТАЕТ».
	//   Считает через тот же `itemFindSowelSlots`, что и отправка (общая точка `ReservePackageSlots`).
	TArray<uint8> Slots15;
	int32 Count = 0;
	ShortSpend = 0;
	ShortBag = 0;
	const bool bHaveContents = Inv->ReservePackageSlots(SelItemCode, Slots15, Count, ShortSpend, ShortBag);
	const bool bRoomOk = bHaveContents && ShortSpend <= 0 && ShortBag <= 0;

	if (bRoomOk)
	{
		// ★ Взвести ОДНОРАЗОВУЮ защёлку — эталон `gap_C64[84] = 1, [85] = 1, [88] = 0`.
		//   Пакет уйдёт СЛЕДУЮЩИМ КАДРОМ из `itemPackAgeToggleSendAction` (game_main.c:1480).
		bSendArmed = true;
		bSendAllow = true;
		UE_LOG(LogAsdaPkgPanel, Log,
			TEXT("[пакеты] 26502: места хватает — защёлка отправки взведена (6096 уйдёт следующим кадром)"));
		return;
	}

	// ★★★ ИСПРАВЛЕНО (живой прогон: «не хватило места — нужно ОРИГИНАЛЬНОЕ модальное окно»).
	//   Здесь нотис 99306 уходил в ОБЩУЮ ПЛАШКУ ПОДСКАЗКИ. Это была ошибка чтения имени:
	//   эталон зовёт `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` (ui_core.c:62812),
	//   а имя `g_pItemTooltipUI` ВРЁТ — это менеджер игровых MSGBOX, не плашка предмета.
	//   Разбор `CItem_BuildTooltipText@0xD6E790` (item_inventory.c:50818-50766, ветка `msgClass == 0`,
	//   `noticeId == −1` ⇒ `LABEL_87`):
	//       CControl_SetText(pMsgBox + 2, текст);                       // ONEBUT_TEXT 1501
	//       Collection_AddString(pMsgBox + 5, нотис 2288);              // подпись ONEBUT_BUT00 1503
	//       CControl_SetVisible(pMsgBox + 1, 1);                        // ONEBUT_MSGBOX 1500
	//       bForceShow(=1) ⇒ helper_setupFields(g_pWindowMgr, pMsgBox + 1, 1);   // показать МОДАЛЬНО
	//   То есть оригинал показывает ОДНОКНОПОЧНОЕ МОДАЛЬНОЕ ОКНО. Подтверждено вторым методом:
	//   ровно этот вызов уже разобран в порту как ONEBUT_MSGBOX у книги навыков
	//   (`AsdaSkillWindowSubsystem.cpp`, MsgBox1) и у снасти (`AsdaFishingToolSubsystem.cpp`,
	//   Msg1Button). Общая точка — `UAsdaWindowSubsystem::ShowGameMessage` (закон `port-shared-points`).
	//   Отступления от оригинала НЕТ: требование пользователя и эталон здесь совпали.
	const FString Fmt = AsdaUi::LocRaw(GI, kLocNoRoom, /*bDebugFallback*/ true);
	const FString Body = AsdaUi::FormatPrintfArgs(
		Fmt.Replace(TEXT("[sd1]"), TEXT("%d")).Replace(TEXT("[sd2]"), TEXT("%d")),
		{ FString::FromInt(ShortSpend), FString::FromInt(ShortBag) });
	// Свою плашку гасим до модалки — иначе она останется висеть поверх (закон `ui-shared-tip-ownership`).
	if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
	{
		Tip->HideTooltipFor(EAsdaTipOwner::Package);
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было «нет оконной подсистемы ⇒ только строка лога» — стало общая политика
	//   `AsdaNotice::MsgBox` (окна нет ⇒ текст в ленту категорией ItemRefused); лог оставлен.
	if (!AsdaNotice::MsgBox(GI, EAsdaMsgCategory::ItemRefused, Body, AsdaLoc::kBtnOk))
	{
		UE_LOG(LogAsdaPkgPanel, Warning,
			TEXT("[пакеты] 26502: нет UAsdaWindowSubsystem — модалку нехватки места показать нечем, текст в ленте: %s"), *Body);
	}
	UE_LOG(LogAsdaPkgPanel, Log,
		TEXT("[пакеты] 26502: не хватает ячеек (расходники %d, сумка %d) — нотис 99306 в модальное окно ")
		TEXT("ONEBUT_MSGBOX, панель закрыта"), ShortSpend, ShortBag);
	ClosePanel();
}

// ═══════════════════════════════ ЗАКРЫТИЕ ══════════════════════════════════════════════════════
// Порт `uiPanelCloseAndKillFocus@0xDAC900` (`ui_core.c:62660-62683`).
// ⚠ Как в эталоне: НЕ обнуляет ни режим, ни выбор, ни коды ячеек — очистка только при след. Populate.
void UAsdaPackagePanelSubsystem::ClosePanel()
{
	if (!bPanelVisible) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	// Эталон гасит и hover-подсказку, и общую плашку предмета (`CControl_KillFocus(&g_ItemTooltip)`).
	//   Гасим ТОЛЬКО свою (закон `ui-shared-tip-ownership`: чужую плашку трогать нельзя).
	if (UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::Package);
	}
	CellHovered = -1;
	bPanelVisible = false;
	// ★ 31.10.2026 [ОРИГИНАЛ] (`asda.Booster.PrevPaths`): `uiPanelCloseAndKillFocus@0xDAC900` (`AsdaStory_game.c:403666-403669`)
	//   гасит и окно «Вид» 15600, если оно видимо, — откуда бы его ни открыли. [ПОРТ] Ветка «режим 0 → снять примерку»
	//   (`charApplyMountEquipSlots`) не переносится: примерки магазина бустеров в порту нет.
	if (UAsdaBoosterPanelSubsystem::PrevPathsOn())
	{
		if (UAsdaBoosterPanelSubsystem* Bp = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr)
		{
			if (Bp->IsPrevVisible()) { Bp->ClosePrevWindow(); }
		}
	}
	UE_LOG(LogAsdaPkgPanel, Verbose, TEXT("[пакеты] панель 26500 закрыта (режим и выбор НЕ сброшены — как в эталоне)"));
}

// ★ Приход 6097: эталон ПЕРВЫМ ДЕЛОМ закрывает панель (`network.c:24677`).
void UAsdaPackagePanelSubsystem::NotifyPackageResult()
{
	ClosePanel();
}

// ═══════════════════════════════ ТИК ═══════════════════════════════════════════════════════════
void UAsdaPackagePanelSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.PackagePanel");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ Защёлка отправки работает ВСЕГДА, даже когда панель уже скрыта: эталон зовёт
	//   `itemPackAgeToggleSendAction` из главного цикла безусловно (`game_main.c:1480`).
	TickDeferredSend();
	if (bPanelVisible) { TickCellTooltip(); }
	// ★ 31.10.2026 [ОРИГИНАЛ] (`asda.Booster.PrevPaths`): щелчок по ячейке (бустер → окно «Вид»). Опрос мыши — каждый кадр,
	//   и при скрытой панели: нажатие, которое её открыло, вторым щелчком по ячейке не считается.
	if (UAsdaBoosterPanelSubsystem::PrevPathsOn()) { TickCellClick(); }
}

// ★★★ 31.10.2026 ЩЕЛЧОК ПО ЯЧЕЙКЕ — событие 0xB `stampCraftWnd_msgProc@0xDB2C80` (`AsdaStory_game.c:405391-405404`):
//   сначала строки витрины 26000 (`aPackageRows`, её эталон не показывает никогда — ветки нет), затем
//   `invItemUseOrBooster@0xDB36D0` (:405584) — поиск кода по IDNum иконки в тех же полях `+8+16k` / `+72+16c`:
//     запись `item_list`, подтип 66/67 и `DurationOrEnchant > 0` (бустер) →
//         `uiStorageWindow_ResetState` + `boosterWindowPopulate(…, 1, код)`; флаг `+697` = 1 только при видимом окне 26000
//         (эталон его не показывает ⇒ у «Вида» отсюда щелчок по ячейкам мёртв); затем `positionHoverTooltipWnd` (стыковка);
//     иначе, если режим 0 (превью) → `invAssignMountSlotItem` — примерка магазина бустеров ([ПОРТ] не перенесена, журнал).
//   Режим 1 (вскрытие из сумки) бустер в составе ТОЖЕ открывает «Вид» — проверка бустера стоит раньше проверки режима.
void UAsdaPackagePanelSubsystem::TickCellClick()
{
	Mouse.Update();
	if (!bPanelVisible || !Mouse.bLmbDown || PopulateFrame == GFrameCounter) { return; }
	UGameInstance* GI = GetGameInstance();
	if (!GI || AsdaUi::IsModalDispatchBlocked(GI)) { return; }
	if (const UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		if (WM->IsPointerBlockedByTopmost(kWindowId) || WM->IsCursorOverHigherWindow(kWindowId)) { return; }
	}
	int32 Code = 0;
	for (int32 r = 0; r < Blocks.Num() && Code <= 0; ++r)
	{
		if (Blocks[r].Frame && Blocks[r].Frame->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		for (const FAsdaPackageCellUi& C : Blocks[r].Big)   { if (C.ItemCode > 0 && AsdaUi::IsCursorOverWidget(C.Icon)) { Code = C.ItemCode; break; } }
		if (Code > 0) { break; }
		for (const FAsdaPackageCellUi& C : Blocks[r].Small) { if (C.ItemCode > 0 && AsdaUi::IsCursorOverWidget(C.Icon)) { Code = C.ItemCode; break; } }
	}
	if (Code <= 0) { return; }
	const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	const FAsdaItemRecord* R = IT ? IT->GetItem(Code) : nullptr;
	if (!R) { return; }
	if ((R->ItemSubType == 66 || R->ItemSubType == 67) && R->DurationOrEnchant > 0)
	{
		UAsdaBoosterPanelSubsystem* Bp = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>();
		const bool bOk = Bp && Bp->OpenPrevWindowFrom(Code, EAsdaPrevSource::PackagePanel, /*bCellsActive*/ false);
		if (bOk) { DockBesidePrevWindow(); }
		UE_LOG(LogAsdaPkgPanel, Log, TEXT("[пакеты] щелчок по бустеру %d в составе → окно «Вид» %s"),
			Code, bOk ? TEXT("открыто, панель пристыкована") : TEXT("НЕ открыто (нет строки booster_list)"));
		return;
	}
	if (StampMode == 0)
	{
		UE_LOG(LogAsdaPkgPanel, Log,
			TEXT("[пакеты] щелчок по предмету %d в составе: эталон надевает его на своего персонажа (invAssignMountSlotItem@0xB86AA0); ")
			TEXT("примерки магазина бустеров в порту нет"), Code);
	}
}

// ★★★ 31.10.2026 порт `positionHoverTooltipWnd@0xDB3900` (`AsdaStory_game.c:405637-405660`):
//       x = Width − (w15600 + w26500);  y = Height/2 − h26500/2  →  26500 в (x, y);
//       15600 в (x + w26500, y + h26500 − 343).
//   Ширина 26500 — уже пересчитанная `ApplyGeometry` (284·(блоков+1)), высота — 320 (её эталон не меняет); ширина «Вида» —
//   по данным ассета (`GetPrevWindowWidth`), до первой сборки окна — `.JUI` (360). Единицы — канва (как у `ApplyGeometry`).
void UAsdaPackagePanelSubsystem::DockBesidePrevWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaBoosterPanelSubsystem* Bp = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
	if (!PanelWidget || !RootCanvas || !Bp) { return; }
	UWidget* Ref = AsdaUi::FindNodeByName(PanelWidget, TEXT("Package_Contents_Win_0_0"), nullptr);
	const UCanvasPanelSlot* SRef = Ref ? AsdaUi::SlotOf(Ref) : nullptr;
	if (!SRef) { return; }
	const FVector2D View = AsdaUi::ViewportSizeUnits(GetWorld());
	const float PkgW  = kGridCellWidth + kGridCellWidth * (float)UsedBlocks;
	const float PrevW = (Bp->GetPrevWindowWidth() > 0.f) ? Bp->GetPrevWindowWidth() : 360.f;   // `.JUI` BOOSTER_PREV_WIN Size
	// Эталон считает в целых: `(unsigned)Height >> 1`, `h >> 1`.
	const float X = FMath::FloorToFloat(View.X) - (PrevW + PkgW);
	const float Y = FMath::FloorToFloat(View.Y * 0.5f) - FMath::FloorToFloat(kWinHeight * 0.5f);
	const FVector2D D(X - SRef->GetOffsets().Left, Y - SRef->GetOffsets().Top);
	if (!D.IsNearlyZero()) { AsdaUi::ShiftRootChildren(RootCanvas, D); }
	Bp->PlacePrevAtCanvas(FVector2D(X + PkgW, Y + kWinHeight - UAsdaBoosterPanelSubsystem::kPrevDockBelowTop));
	UE_LOG(LogAsdaPkgPanel, Log,
		TEXT("[пакеты] стыковка с окном «Вид» (positionHoverTooltipWnd): экран %.0fx%.0f, панель %.0f в (%.0f,%.0f), «Вид» %.0f в (%.0f,%.0f)"),
		View.X, View.Y, PkgW, X, Y, PrevW, X + PkgW, Y + kWinHeight - UAsdaBoosterPanelSubsystem::kPrevDockBelowTop);
}

// Иконка ячейки для самотеста (симклик по живой геометрии).
const UWidget* UAsdaPackagePanelSubsystem::DebugRootWidget() const
{
	return PanelWidget.Get();
}

UWidget* UAsdaPackagePanelSubsystem::DebugCellIcon(int32 Block, bool bBig, int32 Idx) const
{
	if (!Blocks.IsValidIndex(Block)) { return nullptr; }
	const TArray<FAsdaPackageCellUi>& Arr = bBig ? Blocks[Block].Big : Blocks[Block].Small;
	return Arr.IsValidIndex(Idx) ? Arr[Idx].Icon.Get() : nullptr;
}

// Код предмета ячейки для самотеста.
int32 UAsdaPackagePanelSubsystem::DebugCellCode(int32 Block, bool bBig, int32 Idx) const
{
	if (!Blocks.IsValidIndex(Block)) { return -1; }
	const TArray<FAsdaPackageCellUi>& Arr = bBig ? Blocks[Block].Big : Blocks[Block].Small;
	return Arr.IsValidIndex(Idx) ? Arr[Idx].ItemCode : -1;
}

// ★★★ Порт `itemPackAgeToggleSendAction@0xDAB6E0` (`item_inventory.c:53940-53968`).
//   Защёлка ОДНОРАЗОВАЯ; перед отправкой выбор перепроверяется по данным заново.
void UAsdaPackagePanelSubsystem::TickDeferredSend()
{
	if (!bSendArmed) { return; }
	// Эталон: `if (gap_C64[85] != 1) goto disarm;`
	if (!bSendAllow)
	{
		bSendArmed = false; bSendAllow = true;
		return;
	}
	bSendAllow = false;   // эталонный `gap_C64[85] = 0` — ровно один выстрел

	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(SelItemCode) : nullptr;
	// Эталонная перепроверка: запись есть, подтип 200/201, строка packagetable на месте.
	if (Rec && (Rec->ItemSubType == 200 || Rec->ItemSubType == 201) && IT->IsPackageItem(SelItemCode) && Inv)
	{
		if (Inv->SendOpenPackage6096(SelItemCode, SelContainer, SelSlot))
		{
			++CntSent;
			UE_LOG(LogAsdaPkgPanel, Log,
				TEXT("[пакеты] 6096 отправлен отложенно (кадр после клика 26502): коробка %d, слот %d:%d"),
				SelItemCode, SelContainer, SelSlot);
		}
	}
	else
	{
		UE_LOG(LogAsdaPkgPanel, Log,
			TEXT("[пакеты] отложенная отправка отменена: предмет %d перепроверку не прошёл"), SelItemCode);
	}
	// Эталонный `disarm`.
	bSendArmed = false;
	bSendAllow = true;
}

// ★ Порт `stampWnd_ShowItemTooltip@0xDB29B0` + маршрут msg 9/10 `stampCraftWnd_msgProc@0xDB2C80`.
//   Хит-тест по ИКОНКЕ ячейки — именно её idnum эталон кладёт в поле «IDNum ячейки».
//   ★★★ ИСПРАВЛЕНО вместе с окном «Вид»: показ шёл через `ShowItemTooltipByCode`
//   (`itemTooltipBuildByCode@0xEA6B80` — только имя и описание). У этой панели свой эталонный
//   маршрут: `itemType > 9` ⇒ ПРОСТОЙ билдер, иначе ПОЛНЫЙ `ItemTooltip_Build`, спец-предмет ⇒
//   ветка второй таблицы, самоцвет ⇒ билдер самоцвета. Общая точка — `ShowStampSlotTooltip`.
void UAsdaPackagePanelSubsystem::TickCellTooltip()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }

	int32 HitFlat = -1;
	int32 HitCode = 0;
	for (int32 r = 0; r < Blocks.Num() && HitFlat < 0; ++r)
	{
		if (Blocks[r].Frame && Blocks[r].Frame->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		for (int32 k = 0; k < Blocks[r].Big.Num(); ++k)
		{
			const FAsdaPackageCellUi& C = Blocks[r].Big[k];
			if (C.ItemCode > 0 && AsdaUi::IsCursorOverWidget(C.Icon))
			{
				HitFlat = r * (kBigCells + kSmallCells) + k; HitCode = C.ItemCode; break;
			}
		}
		if (HitFlat >= 0) { break; }
		for (int32 c = 0; c < Blocks[r].Small.Num(); ++c)
		{
			const FAsdaPackageCellUi& C = Blocks[r].Small[c];
			if (C.ItemCode > 0 && AsdaUi::IsCursorOverWidget(C.Icon))
			{
				HitFlat = r * (kBigCells + kSmallCells) + kBigCells + c; HitCode = C.ItemCode; break;
			}
		}
	}

	if (HitFlat < 0)
	{
		// Уход с ячейки (эталонный msg 10 → `CControl_KillFocus(&g_ItemTooltip)`).
		if (CellHovered >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::Package); CellHovered = -1; }
		return;
	}
	if (HitFlat == CellHovered) { return; }   // уже показан для этой ячейки
	CellHovered = HitFlat;
	Tip->ShowStampSlotTooltip(HitCode, EAsdaTipOwner::Package);
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaPackagePanelSubsystem::DumpControls() const
{
	FString S = FString::Printf(
		TEXT("панель 26500: виджет=%s готова=%s видима=%s режим=%d выбор=%d (%d:%d) блоков=%d заполнений=%d отправок=%d\n"),
		PanelWidget ? TEXT("да") : TEXT("НЕТ"), bReady ? TEXT("да") : TEXT("НЕТ"),
		bPanelVisible ? TEXT("да") : TEXT("НЕТ"), StampMode, SelItemCode, SelContainer, SelSlot,
		UsedBlocks + 1, CntPopulate, CntSent);
	S += FString::Printf(TEXT("  26501 имя=%s · 26505 титул=%s · 26507 подложка=%s · 26502=%s · 26506=%s\n"),
		BoxNameLabel ? TEXT("да") : TEXT("НЕТ"), TitleBar ? TEXT("да") : TEXT("НЕТ"),
		BtnBase ? TEXT("да") : TEXT("НЕТ"), BtnUnpack ? TEXT("да") : TEXT("НЕТ"), BtnClose ? TEXT("да") : TEXT("НЕТ"));
	for (int32 r = 0; r < Blocks.Num(); ++r)
	{
		S += FString::Printf(TEXT("  блок %d рамка=%s\n"), r, Blocks[r].Frame ? TEXT("да") : TEXT("НЕТ"));
		for (int32 k = 0; k < Blocks[r].Big.Num(); ++k)
		{
			const FAsdaPackageCellUi& C = Blocks[r].Big[k];
			S += FString::Printf(TEXT("    верх k=%d idnum=%d иконка=%s подпись=%s число=%s код=%d\n"),
				k, C.IdNum, C.Icon ? TEXT("да") : TEXT("НЕТ"), C.Name ? TEXT("да") : TEXT("НЕТ"),
				C.Count ? TEXT("да") : TEXT("НЕТ"), C.ItemCode);
		}
		for (int32 c = 0; c < Blocks[r].Small.Num(); ++c)
		{
			const FAsdaPackageCellUi& C = Blocks[r].Small[c];
			S += FString::Printf(TEXT("    низ  c=%d idnum=%d иконка=%s подпись=%s число=%s код=%d\n"),
				c, C.IdNum, C.Icon ? TEXT("да") : TEXT("НЕТ"), C.Name ? TEXT("да") : TEXT("НЕТ"),
				C.Count ? TEXT("да") : TEXT("НЕТ"), C.ItemCode);
		}
	}
	return S;
}

FString UAsdaPackagePanelSubsystem::DumpModel() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FString S = FString::Printf(TEXT("коробка %d, режим %d, занято блоков %d\n"), SelItemCode, StampMode, UsedBlocks + 1);
	for (int32 r = 0; r <= UsedBlocks && r < Blocks.Num(); ++r)
	{
		for (int32 k = 0; k < Blocks[r].Big.Num(); ++k)
		{
			const int32 Code = Blocks[r].Big[k].ItemCode;
			if (Code > 0) { S += FString::Printf(TEXT("  [%d.%d] %d «%s»\n"), r, k, Code, IT ? *IT->GetItemName(Code) : TEXT("?")); }
		}
		for (int32 c = 0; c < Blocks[r].Small.Num(); ++c)
		{
			const int32 Code = Blocks[r].Small[c].ItemCode;
			if (Code > 0) { S += FString::Printf(TEXT("    [%d..%d] %d «%s»\n"), r, c, Code, IT ? *IT->GetItemName(Code) : TEXT("?")); }
		}
	}
	return S;
}

// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ════════════════════════════════════════════
namespace
{
	UAsdaPackagePanelSubsystem* PkgSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaPackagePanelSubsystem>() : nullptr;
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U13): было `AddOnScreenDebugMessage` (12 с поверх экрана; в Shipping оверлей пуст) — стало общая точка `AsdaNotice::ConsoleEcho`: каждая строка в журнал (Display), непустая — в ленту Tip.
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
	void PkgEcho(UWorld* W, const FString& S) { AsdaNotice::ConsoleEcho(W, S, LogAsdaPkgPanel); }
#endif // !UE_BUILD_SHIPPING
}

// Открыть панель по коду коробки без сервера: `asda.Package.Show <код> [режим=1] [контейнер] [слот]`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPkgShow(
	TEXT("asda.Package.Show"),
	TEXT("asda.Package.Show <кодКоробки> [режим 0=превью/1=вскрытие] [контейнер] [слот] — открыть панель 26500 ")
	TEXT("ровно тем же путём, что двойной клик. Пример: asda.Package.Show 20816 1 2 0"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: взводит произвольные код/контейнер/слот (по умолчанию фиксированная ячейка) — Package.Click отправил бы 6096 по ним.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Package.Show"))) { return; }
		UAsdaPackagePanelSubsystem* P = PkgSub(W);
		if (!P || A.Num() < 1) { PkgEcho(W, TEXT("использование: asda.Package.Show <кодКоробки> [режим] [контейнер] [слот]")); return; }
		const int32 Code = FCString::Atoi(*A[0]);
		const int32 Mode = A.Num() > 1 ? FCString::Atoi(*A[1]) : 1;
		const int32 Cont = A.Num() > 2 ? FCString::Atoi(*A[2]) : 2;
		const int32 Slot = A.Num() > 3 ? FCString::Atoi(*A[3]) : 0;
		const bool bOk = P->Populate(Code, Mode, Cont, Slot, false);
		PkgEcho(W, FString::Printf(TEXT("панель 26500: %s"), bOk ? TEXT("открыта") : TEXT("НЕ открыта — см. лог")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPkgDump(
	TEXT("asda.Package.Dump"),
	TEXT("asda.Package.Dump — полный дамп контролов и содержимого панели 26500"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPackagePanelSubsystem* P = PkgSub(W)) { PkgEcho(W, P->DumpControls() + P->DumpModel()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPkgBtn(
	TEXT("asda.Package.Click"),
	TEXT("asda.Package.Click <26502|26506> — нажать кнопку панели 26500 (проверка без мыши)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPackagePanelSubsystem* P = PkgSub(W);
		if (!P || A.Num() < 1) { PkgEcho(W, TEXT("использование: asda.Package.Click <26502|26506>")); return; }
		const int32 Id = FCString::Atoi(*A[0]);
		if (Id == 26502) { P->OnUnpackButton(); PkgEcho(W, TEXT("26502 нажата")); }
		else if (Id == 26506) { P->OnCloseButton(); PkgEcho(W, TEXT("26506 нажата")); }
		else { PkgEcho(W, TEXT("известны только 26502 и 26506")); }
	}));
#endif // ASDA_DEV_CONSOLE
