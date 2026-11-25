// Copyright (c) 2026 @sitxovski (Discord)
// Реализация магазина NPC — см. шапку UI/AsdaNpcShopSubsystem.h (там весь реверс с адресами).
#include "UI/AsdaNpcShopSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"   // гейт диспетчера 5021 (окно 6800 перехватывает ответ)
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "AsdaDataTableCache.h"
#include "AsdaBytes.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): ссылки 5018 через общий писатель

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Login/AsdaButtonRelay.h"
#include "Resources/AsdaSoundSubsystem.h"   // ★ 06.11.2026: звуки открытия и закрытия окна (5/6)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaShop, Log, All);

// ★ Звук покупки/продажи — наша добавка поверх оригинала (обоснование у `PlayTradeSound`).
static ASDA_DEV_CVAR(int32, CVarShopTradeSound,
	TEXT("asda.Shop.TradeSound"), 1,
	TEXT("Звук при покупке/продаже (родной звук падения предмета): 1 — играть (наша добавка), ")
	TEXT("0 — молча, как в оригинале."),
	ECVF_Default);

// ★ 06.11.2026 [ОРИГИНАЛ] звуки окна магазина: `npcShop_ShowWindow@0xE26280` кончается `csoundPlayName2DStore(5)`
//   (npc_mob_pet.c:6271), `npcShop_Close@0xE26570` — звуком 6 (:6298). Порт окно открывал и закрывал молча.
static ASDA_DEV_CVAR(int32, CVarShopWindowSound,
	TEXT("asda.Shop.WindowSound"), 1,
	TEXT("Звуки открытия (5) и закрытия (6) окна магазина NPC, как в оригинале: 1 — играть, 0 — молча (прежнее поведение порта)."),
	ECVF_Default);

// ★ 06.11.2026 [ПОРТ] запасной звук сделки: у 2 908 записей item_img_table категория звука 255 («нет»: зелья, свитки, расходники) —
//   133 из 220 товаров магазинов NPC покупались и продавались молча. Такому предмету — родной `Sod_drop_Etc_0`.
static ASDA_DEV_CVAR(int32, CVarShopTradeSoundFallback,
	TEXT("asda.Shop.TradeSoundFallback"), 1,
	TEXT("Сделка с предметом без категории звука (зелья, свитки): 1 — родной звук Sod_drop_Etc_0, 0 — молча (прежнее поведение порта)."),
	ECVF_Default);

// ★★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] потолок цены продажи по правилу сервера (`ServerSellUnitCap`).
static ASDA_DEV_CVAR(int32, CVarShopServerSellCap,
	TEXT("asda.Shop.ServerSellCap"), 1,
	TEXT("Потолок цены продажи торговцу, как у сервера CharItemSell: цена единицы не выше ")
	TEXT("«покупка − 25 % покупки». 1 — показывать то, что сервер реально начислит (исправление), ")
	TEXT("0 — как в оригинальном клиенте (без потолка)."),
	ECVF_Default);

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;
using AsdaBytes::WrI16;
using AsdaBytes::WrI32;

// ── Константы окна и данных ──
static const FName  kShopWindowId(TEXT("NPCSHOP_WIN"));
static const TCHAR* kShopWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_NPCSHOP_WIN.WBP_NPCSHOP_WIN_C");
static const TCHAR* kDtNpcShop = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcShopList.DT_NpcShopList");

// ── Loc-идентификаторы. ВСЕ сняты с эталонных вызовов `UI_FormatLocalizedNotice`, ни один не выдуман.
namespace ShopLoc
{
	constexpr int32 kNoMoney        = 2231;    // «Недостаточно золота.»          → чат
	constexpr int32 kBagFull        = 2233;    // «Нет места в рюкзаке.»          → msgbox
	constexpr int32 kSellFailed     = 2234;    // «Не удается продать предмет.»   → чат
	constexpr int32 kBought         = 2236;    // vtbl+16(1) после нумпада        → чат
	constexpr int32 kHdrShopBuy     = 2281;    // заголовок колонки, режим покупки
	constexpr int32 kHdrBuy         = 2282;
	constexpr int32 kHdrPrice       = 2283;
	constexpr int32 kHdrShopSell    = 2284;    // заголовок колонки, режим продажи
	constexpr int32 kHdrSell        = 2285;
	constexpr int32 kHdrSellPrice   = 2286;
	constexpr int32 kBtnCancel      = 2289;    // подпись кнопки «Выход»
	constexpr int32 kCartFull       = 2312;    // «Нет места в списке покупок.»   → msgbox
	constexpr int32 kBagFullPickup  = 1012;    // ответ 5021 код 3               → msgbox
	constexpr int32 kNoGoldBuy      = 2048;    // ответ 5021 код 4               → msgbox
	constexpr int32 kFriendsOnly    = 8013;    // ответ 5019 код 2               → чат
	constexpr int32 kNotSoldHere    = 9569;    // ответ 5021 код 5               → msgbox
	constexpr int32 kNoItem         = 9570;    // ответ 5021 код 0               → msgbox
	constexpr int32 kGold           = 12006;   // подпись «Золото» (1308)
	constexpr int32 kBtnOk          = 13249;   // подпись BUYBUT
	constexpr int32 kBtnOkSell      = 13250;   // подпись SELLBUT
	constexpr int32 kBoughtLine     = 20065;   // «Ты покупаешь [ss1] за [ss2] золота.» → чат
	constexpr int32 kNetPriceLabel  = 105006;  // «Цена продажи» (1382)
	constexpr int32 kPriceLabel     = 111914;  // «Цена» (1307, режим покупки)
	constexpr int32 kBuyDiscount    = 111915;  // «Скидка (%d%%)»  — покупка, своя фракция
	constexpr int32 kBuyMarkup      = 111916;  // «Наценка (%d%%)» — покупка, чужая фракция
	constexpr int32 kSellBonus      = 111917;  // «Бонус (%d%%)»   — продажа, своя фракция
	constexpr int32 kSellTax        = 111918;  // «Налог (%d%%)»   — продажа, чужая фракция
}

// Ключ отката дефекта Д1 («купленное снаряжение затирается») живёт в `AsdaInventorySubsystem.cpp`.

// ★ ДЕФЕКТ ОРИГИНАЛА Д2. `Send_CharItemSell@0xE32800` берёт поля `запись+7` (второй индекс) и
//   `запись+14` (вес стопки) ВСЕГДА из контейнера 2: символы `word_2902005` и `g_aShopItemRecords`
//   — это `g_InventoryArray2 + 7` и `g_InventoryArray2 + 14` (проверено арифметикой смещений от
//   `g_InventoryBag = 0x2901950`: контейнер 2 начинается на 30·57 = 1710 байт дальше, 0x2901FFE).
//   При продаже СНАРЯЖЕНИЯ (режим 10, контейнер 1) индекс берётся тот же, но читается из ЧУЖОГО
//   массива ⇒ в пакет уходят посторонние `secondIndex` и `weightOfStack`. Сервер
//   `CharItemSell@0x50D240` эти два поля в ветке контейнера 1 не использует (он читает запись сам
//   через `sub_4D61E0`), поэтому вреда не видно — но мусор в пакете остаётся мусором.
//   ⇒ Берём поля ИЗ СОБСТВЕННОГО контейнера позиции.
static ASDA_DEV_CVAR(int32, CVarShopLegacySellRef,
	TEXT("asda.Shop.LegacySellRefFromSpendBag"),
	0,
	TEXT("Пакет продажи 5018: 0 = поля +7/+14 берутся из контейнера самой позиции (исправление Д2); "
	     "1 = как эталон — всегда из контейнера 2."),
	ECVF_Default);

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ══════════════════════════════════════════════════

void UAsdaNpcShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `NPCSHOP_WIN` (IDNum 1300) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `NPCShop_InitControls@0xE233F0`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kShopWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaNpcShopSubsystem::CloseShop));
	}
	GridUi.SetNum(kGridCells);
	CartUi.SetNum(kCartCells);
	for (int32 i = 0; i < kCartCells; ++i) { Cart[i].Clear(); }

	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		// ★★★ 1:1 ДИСПЕТЧЕР ЭТАЛОНА (network.c, `case 0x139D`): ответ 5021 разбирает ТО окно,
		//   которое сейчас открыто. Пока открыт МАГАЗИН СНАРЯЖЕНИЯ (`SHELLITEM_STORE2_WIN` 6800),
		//   пакет уходит в `Recv_ShopSell_Result_0@0xC3D430` (его подсистема), а сюда
		//   (`Recv_ShopSell_Result@0xE28FD0`) — только когда то окно закрыто. Без этого гейта одна
		//   покупка применялась бы ДВАЖДЫ и печатала бы отказ в обоих окнах.
		HBuyResult = Net->OnShopBuyResult.AddLambda([this](const TArray<uint8>& B)
		{
			const UAsdaEquipShopSubsystem* Eq = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
			if (Eq && Eq->IsOpen()) { return; }
			PendingBuy.Push(B);
		});
		HSellResult = Net->OnShopSellResult.AddLambda([this](const TArray<uint8>& B) { PendingSell.Push(B); });
		// Смена владельца города меняет ставку — блок налога окна перерисовывается на месте.
		HCityOwner = Net->OnCityOwnerInfo.AddLambda([this]()
		{
			if (bOpen && bControlsReady) { RefreshTaxTexts(); RefreshMoneyTexts(); }
		});
	}
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] подсистема поднята"));
}

void UAsdaNpcShopSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnShopBuyResult.Remove(HBuyResult);
		Net->OnShopSellResult.Remove(HSellResult);
		Net->OnCityOwnerInfo.Remove(HCityOwner);
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaNpcShopSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Магазин привязан к КОНКРЕТНОМУ NPC на КОНКРЕТНОЙ карте — он не переживает даже травел.
	ResetWidget();
	bOpen = false;
	bPending = false;
	NpcId = -1;
	ShopType = 0;
	Page = 0; TotalPages = 0;
	SelGridCell = -1; SelAbsIndex = -1; SelCartCell = -1; SelMode = 0;
	GridDbl.Reset();   // [ПОРТ] 10.10: пара щелчков не переживает открытие/закрытие/сброс окна
	RunningTotal = 0; TaxDelta = 0;
	SellPriceBuffPercent = -1;
	LastTradeItemCode = 0;
	PendingBuy.Clear(); PendingSell.Clear(); PendingBuyLines.Reset();
	DragCell = -1; bTipFromCart = false;
	Mouse.Reset();
	if (AsdaIsFullSessionReset(Reason))
	{
		UE_LOG(LogAsdaShop, Log, TEXT("[магазин] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
	}
}

void UAsdaNpcShopSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.NpcShop");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	if (!bOpen || !bControlsReady) { return; }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		// Летящий призрак снимаем БЕЗ действия (ветка «отпустили мимо»): иначе он замер бы поверх
		//   нотиса, а отпускание после закрытия засчиталось бы как перенос в корзину.
		if (DragGhost.IsActive()) { DragGhost.Cancel(); DragCell = -1; }
		DragArm.Disarm();
		// Полосе прокрутки отдаём кадр «кнопка отпущена» — так снимается и латч захвата бегунка,
		//   и картинка нажатой стрелки (своей «снять захват» у полосы нет, это её штатный путь).
		TickScrollBar(/*bLmb*/false, /*bLmbDownEdge*/false);
		if (TipCell >= 0)   // висящую плашку товара ГАСИМ, а не просто «не показываем»
		{
			TipCell = -1; bTipFromCart = false;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::NpcShop);
			}
		}
		Mouse.Reset();
		GridDbl.Reset();   // [ПОРТ] ревизия 10.10 МЕЛК-1: модальный нотис между щелчками рвёт пару (как щелчок по другому окну)
		return;
	}
	TickScrollBar(Mouse.bLmb, Mouse.bLmbDown);
	TickDrag();
	TickTooltip();
}

// ═════════════════════════════ ОКНО ════════════════════════════════════════════════════════════

bool UAsdaNpcShopSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kShopWindowId, kShopWbp, kShopZ).Ensure(GetGameInstance(), ShopWidget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			BuildScrollBar();
			RegisterZone();
			// ★ 08.10.2026 [ОРИГИНАЛ] (window_default_pos.md §6 п.10): `regionWindow_OpenAndPathMove@0xD99290` (ui_core.c:61121)
			//   при входе в диалог NPC зовёт `uiSetControlPosByConfigPct@0xE2B180`: Origin окна 1300 = (16·W/100, 23·H/100) —
			//   (163,176) при 1024×768; якорь «5 0 0» ничего не добавляет. Магазин открывается только из диалога, поэтому
			//   постановка — при создании окна; сдвиг игрока (память позиций порта) ложится поверх. Откат: `asda.UI.JuiDefaultPos 0`.
			if (UAsdaWindowSubsystem::UseOriginalDefaultPos())
			{
				WM.PlaceWindowKeepingSavedOffset(kShopWindowId, FName(TEXT("NPCSHOP_WIN")), nullptr, [](const FVector2D& V, const FVector2D&)
				{
					return FVector2D(16 * FMath::TruncToInt32(V.X) / 100, 23 * FMath::TruncToInt32(V.Y) / 100);
				});
			}
		},
		[]()
		{
			UE_LOG(LogAsdaShop, Error, TEXT("[магазин] WBP_NPCSHOP_WIN не загрузился (%s)"), kShopWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaNpcShopSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kShopWindowId);
		WM->UnregisterWheelHandler(kShopWindowId);
		WM->CloseWindow(kShopWindowId);
	}
	ShopWidget = nullptr;
	GridClicks = nullptr;
	CartClicks = nullptr;
	Relays.Reset();
	ScrollBar = FAsdaScrollBarNomal();
	for (FAsdaShopCellUi& U : GridUi) { U = FAsdaShopCellUi(); }
	for (FAsdaShopCellUi& U : CartUi) { U = FAsdaShopCellUi(); }
	BtnClose = BtnExit = BtnBuy = BtnSell = nullptr;
	CapBuy = CapSell = CapExit = nullptr;
	TxtList00 = TxtList02 = TxtList04 = TxtList06 = TxtTotal = TxtMoney = nullptr;
	TxtTaxPct = TxtTaxVal = TxtNetLbl = TxtNetVal = nullptr;
	GrpSowelTitle = GrpMiscTitle = GrpWeaponTitle = nullptr;
	Gear = nullptr; AniGear = nullptr; GridSel = nullptr; CartSel = nullptr;
	bControlsReady = false;
	Mouse.Reset();
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`).
void UAsdaNpcShopSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!ShopWidget || !ShopWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	ShopWidget->WidgetTree->GetAllWidgets(All);
	// Имена план-иконок ячеек: импортёр даёт `{idc}_{juiIdx}_0`, а число `juiIdx` привязано к
	//   порядку в `.JUI` и при перегенерации меняется — поэтому ищем ПО РАЗОБРАННОМУ idc.
	TArray<FName> GridIconNames; GridIconNames.SetNum(kGridCells);
	TArray<FName> CartIconNames; CartIconNames.SetNum(kCartCells);

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("NPCSHOP_CLOSE"))   { BtnClose = Btn; continue; }
			if (N == TEXT("NPCSHOP_EXITBUT")) { BtnExit  = Btn; continue; }
			if (N == TEXT("NPCSHOP_BUYBUT"))  { BtnBuy   = Btn; continue; }
			if (N == TEXT("NPCSHOP_SELLBUT")) { BtnSell  = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("NPCSHOP_LIST00_Text"))  { TxtList00 = Tb; continue; }
			if (N == TEXT("NPCSHOP_LIST02_Text"))  { TxtList02 = Tb; continue; }
			if (N == TEXT("NPCSHOP_LIST04"))       { TxtList04 = Tb; continue; }
			if (N == TEXT("NPCSHOP_LIST06"))       { TxtList06 = Tb; continue; }
			if (N == TEXT("NPCSHOP_TEXT00"))       { TxtTotal  = Tb; continue; }
			if (N == TEXT("NPCSHOP_TEXT01"))       { TxtMoney  = Tb; continue; }
			if (N == TEXT("NPCSHOP_LISTxx_ss1"))   { TxtTaxPct = Tb; continue; }
			if (N == TEXT("NPCSHOP_LISTxx_ss2"))   { TxtTaxVal = Tb; continue; }
			if (N == TEXT("NPCSHOP_LISTxx_ss3"))   { TxtNetLbl = Tb; continue; }
			if (N == TEXT("NPCSHOP_LISTxx_ss4"))   { TxtNetVal = Tb; continue; }
			if (N == TEXT("NPCSHOP_BUYBUT_Text"))  { CapBuy    = Tb; continue; }
			if (N == TEXT("NPCSHOP_SELLBUT_Text")) { CapSell   = Tb; continue; }
			if (N == TEXT("NPCSHOP_EXITBUT_Text")) { CapExit   = Tb; continue; }
			// Счётчики стака ячеек: TextBlock зовут ровно как контрол (`NPCSHOP_TRAY07`).
			bool bTaken = false;
			for (int32 C = 0; C < kGridCells && !bTaken; ++C)
			{
				if (N == FString::Printf(TEXT("NPCSHOP_TRAY%02d"), C)) { GridUi[C].CountText = Tb; bTaken = true; }
			}
			for (int32 C = 0; C < kCartCells && !bTaken; ++C)
			{
				if (N == FString::Printf(TEXT("NPCSHOP_BTRAY%02d"), C)) { CartUi[C].CountText = Tb; bTaken = true; }
			}
			if (bTaken) { Tb->SetText(FText::GetEmpty()); }
			continue;
		}
		// Группы-обёртки заголовков (координаты их детей считаются ВНУТРИ группы — см. закон
		//   об импортёре: гасить/показывать надо именно группу).
		if (N == TEXT("NpcShop_Sowel_Title_grp"))     { GrpSowelTitle  = W; continue; }
		if (N == TEXT("NpcShop_MisStore_Title_grp"))  { GrpMiscTitle   = W; continue; }
		if (N == TEXT("NpcShop_Weapon_Title_grp"))    { GrpWeaponTitle = W; continue; }

		// Планы.
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx)) { continue; }
		if (UAsdaAnimatedPlane* Anim = Cast<UAsdaAnimatedPlane>(W))
		{
			if (Idc == TEXT("NpcShop_AniGaer"))   { AniGear = Anim; continue; }
			if (Idc == TEXT("NPCSHOP_CURSOR00"))  { GridSel = Anim; continue; }
			if (Idc == TEXT("NPCSHOP_CURSOR01"))  { CartSel = Anim; continue; }
			continue;
		}
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }
		if (Idc == TEXT("NpcShop_Gear")) { Gear = Img; continue; }
		bool bTaken = false;
		for (int32 C = 0; C < kGridCells && !bTaken; ++C)
		{
			if (Idc == FString::Printf(TEXT("NPCSHOP_TRAY%02d"), C))
			{
				GridUi[C].Icon = Img;
				GridUi[C].IconRootPos = AsdaUi::CanvasRootPos(Img);
				GridIconNames[C] = FName(*N);
				bTaken = true;
			}
		}
		for (int32 C = 0; C < kCartCells && !bTaken; ++C)
		{
			if (Idc == FString::Printf(TEXT("NPCSHOP_BTRAY%02d"), C))
			{
				CartUi[C].Icon = Img;
				CartUi[C].IconRootPos = AsdaUi::CanvasRootPos(Img);
				CartIconNames[C] = FName(*N);
				bTaken = true;
			}
		}
	}

	// ★ Дедуп №2, В3-S10: база канваса курсора здесь больше не снимается (был кэш `ParentRootOf` один раз при привязке) —
	//   её считает `AsdaUi::PlaceFrameOver` по живым слотам при каждой постановке (см. `PlaceCursorOnCell`).

	// ── Клик-оверлеи через ОБЩУЮ точку (закон `port-shared-points`): она же ставит иконки. ──
	GridClicks = NewObject<UAsdaIconCellGrid>(this);
	GridClicks->SetCellSizePx((float)kCellPx);
	// ★ Клик магазин ловит САМ (`TickDrag`, по живой геометрии) — кнопки-оверлеи не строим:
	//   ячейки окна 1300 заданы цепочкой вложенности, и их зоны съезжали на чужую панель.
	GridClicks->SetClickOverlayEnabled(false);
	GridClicks->SetupNamed(ShopWidget, GridIconNames, /*bMale*/true,
		[this](int32 Cell) -> int32
		{
			const int32 Abs = Cell + kGridCols * Page;
			return Items.IsValidIndex(Abs) ? Items[Abs].Code : 0;
		},
		[this](int32 Cell) { OnGridCellClicked(Cell); });

	CartClicks = NewObject<UAsdaIconCellGrid>(this);
	CartClicks->SetCellSizePx((float)kCellPx);
	CartClicks->SetClickOverlayEnabled(false);
	CartClicks->SetupNamed(ShopWidget, CartIconNames, /*bMale*/true,
		[this](int32 Cell) -> int32 { return (Cell >= 0 && Cell < kCartCells) ? Cart[Cell].Code : 0; },
		[this](int32 Cell) { OnCartCellClicked(Cell); });

	// ── Кнопки. Диспетчер эталона `uiNPCShop_DispatchHotkeyCmd@0xE24890`. ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose, 1302, [this](int32) { CloseShop(); });   // → vtbl+20
	Bind(BtnExit,  1313, [this](int32) { CloseShop(); });   // → vtbl+20 (то же действие)
	Bind(BtnBuy,   1311, [this](int32) { OnConfirmClicked(); });
	Bind(BtnSell,  1312, [this](int32) { OnConfirmClicked(); });

	// Подписи кнопок из локали (эталон ставит их в `NPCShop_InitControls`).
	if (CapBuy)  { const FString S = Loc(ShopLoc::kBtnOk);      if (!S.IsEmpty()) { CapBuy->SetText(FText::FromString(S)); } }
	if (CapSell) { const FString S = Loc(ShopLoc::kBtnOkSell);  if (!S.IsEmpty()) { CapSell->SetText(FText::FromString(S)); } }
	if (CapExit) { const FString S = Loc(ShopLoc::kBtnCancel);  if (!S.IsEmpty()) { CapExit->SetText(FText::FromString(S)); } }
	// 1308 — статичная подпись «Золото» (эталон `NPCShop_InitControls`, нотис 12006).
	if (TxtList06) { const FString S = Loc(ShopLoc::kGold); if (!S.IsEmpty()) { TxtList06->SetText(FText::FromString(S)); } }
	// 1382 — «Цена продажи» (эталон ставит её один раз при инициализации).
	if (TxtNetLbl) { const FString S = Loc(ShopLoc::kNetPriceLabel); if (!S.IsEmpty()) { TxtNetLbl->SetText(FText::FromString(S)); } }

	// Шестерёнка hover-состояния скрыта, статичная показана (эталон при инициализации).
	if (AniGear) { AniGear->SetVisibility(ESlateVisibility::Collapsed); }
	if (Gear)    { Gear->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	HideCursors();

	bControlsReady = (GridUi[0].Icon != nullptr && BtnBuy != nullptr && BtnSell != nullptr);
	UE_LOG(LogAsdaShop, Log,
		TEXT("[магазин] окно 1300 разобрано: ячеек витрины %d, корзины %d, кнопки B/S/X = %s/%s/%s"),
		GridIconNames.Num(), CartIconNames.Num(),
		BtnBuy ? TEXT("да") : TEXT("НЕТ"), BtnSell ? TEXT("да") : TEXT("НЕТ"), BtnExit ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaNpcShopSubsystem::RegisterZone()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Корпус окна — девять планов рамки `NPCSHOP_WIN_0_0…0_8` (закон «rect только по узлам,
	//   которые окно и образуют»: объединение всех узлов раздувает зону).
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !ShopWidget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kShopWindowId, this, ShopWidget, bOpen, TEXT("NPCSHOP_WIN_0_%d"), 9);
	WM->RegisterWheelHandler(kShopWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this,
		[this](int32 Notches) -> bool
		{
			// Эталон: событие 0x17 у контрола 1365 листает витрину страницами (по ряду за щелчок).
			if (!bOpen || !ShopWidget) { return false; }
			FVector2D Mn, Mx;
			if (!AsdaUi::FramePlanesAbsRect(ShopWidget, TEXT("NPCSHOP_WIN_0_%d"), 9, Mn, Mx)) { return false; }
			ScrollBy(-Notches);
			return true;
		}));
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═════════════════════════════════════════════

bool UAsdaNpcShopSubsystem::HasShopList(int32 InNpcId) const
{
	return AsdaDt::RowByInt<FAsdaNpcShopRow>(kDtNpcShop, InNpcId, TEXT("NpcShop")) != nullptr;
}

// ★ Удержание DT_NpcShopList на сессию (15.09.2026, dt_gc_hold) — обоснование в объявлении.
int32 UAsdaNpcShopSubsystem::HoldGameTables()
{
	static const FName Group(TEXT("NpcShop"));
	const TCHAR* const Paths[] = { kDtNpcShop };
	return HeldTables.Hold(Group, Paths);
}

// ★ 06.11.2026: звук окна по числовому id таблицы SFT (ключ `asda.Shop.WindowSound`).
void UAsdaNpcShopSubsystem::PlayWindowSound(int32 SoundId) const
{
	if (CVarShopWindowSound.GetValueOnGameThread() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2DById(SoundId); }
}

// ★ 06.11.2026: самотест звуков — товар в ячейку 0 корзины, как после подтверждённого количества.
void UAsdaNpcShopSubsystem::DebugPutCart(int32 ItemCode, int32 Amount)
{
	Cart[0].Clear();
	Cart[0].Code = ItemCode;
	Cart[0].Amount = Amount;
}

void UAsdaNpcShopSubsystem::OpenShop(int32 InShopType, int32 InNpcId)
{
	if (!EnsureWidget()) { return; }
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию

	// `NPCShop_OpenWindow` / `npcShopOpenWindow`: полный сброс состояния ПЕРЕД наполнением.
	ShopType = InShopType;
	NpcId    = InNpcId;
	bPending = false;
	Items.Reset();
	for (int32 i = 0; i < kCartCells; ++i) { Cart[i].Clear(); }
	Page = 0; TotalPages = 0;
	SelGridCell = -1; SelAbsIndex = -1; SelCartCell = -1; SelMode = 0;
	GridDbl.Reset();   // [ПОРТ] 10.10: пара щелчков не переживает открытие/закрытие/сброс окна
	RunningTotal = 0; TaxDelta = 0;
	TipCell = -1;

	const bool bBuy = (ShopType == 5 || ShopType == 7);
	if (bBuy)                { LoadBuyList(); }
	else if (ShopType == 10) { BuildSellListShellBag(); }
	else                     { BuildSellListSpendBag(); }
	RecalcPages();

	bOpen = true;
	ApplyTitles();
	RefreshList();
	RefreshCart();
	RefreshMoneyTexts();
	RefreshTaxTexts();
	LayoutScrollBar();
	HideCursors();

	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kShopWindowId, true);
	}
	PlayWindowSound(5);   // ★ 06.11.2026 [ОРИГИНАЛ]: хвост `npcShop_ShowWindow@0xE26280`
	Mouse.Reset();
	UE_LOG(LogAsdaShop, Log,
		TEXT("[магазин] открыт: тип %d (%s), npcId %d, товаров %d, страниц %d"),
		ShopType, bBuy ? TEXT("покупка") : TEXT("продажа"), NpcId, Items.Num(), TotalPages);
}

void UAsdaNpcShopSubsystem::CloseShop()
{
	if (!bOpen) { return; }
	bOpen = false;
	bPending = false;
	Items.Reset();
	for (int32 i = 0; i < kCartCells; ++i) { Cart[i].Clear(); }
	Page = 0; TotalPages = 0;
	SelGridCell = -1; SelAbsIndex = -1; SelCartCell = -1; SelMode = 0;
	GridDbl.Reset();   // [ПОРТ] 10.10: пара щелчков не переживает открытие/закрытие/сброс окна
	RunningTotal = 0; TaxDelta = 0;
	HideCursors();
	ScrollBar.Hide();

	UGameInstance* GI = GetGameInstance();
	if (UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::NpcShop);
	}
	TipCell = -1;
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kShopWindowId, false);
	}
	PlayWindowSound(6);   // ★ 06.11.2026 [ОРИГИНАЛ]: хвост `npcShop_Close@0xE26570`
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] закрыт (npcId %d)"), NpcId);
}

// ═════════════════════════════ НАПОЛНЕНИЕ СПИСКА ═══════════════════════════════════════════════

void UAsdaNpcShopSubsystem::LoadBuyList()
{
	const FAsdaNpcShopRow* Row = AsdaDt::RowByInt<FAsdaNpcShopRow>(kDtNpcShop, NpcId, TEXT("NpcShop"));
	if (!Row)
	{
		// Эталон в этом случае просто возвращает 0 и оставляет витрину пустой (нотиса нет).
		UE_LOG(LogAsdaShop, Warning, TEXT("[магазин] у NPC %d нет строки в DT_NpcShopList — витрина пуста"), NpcId);
		return;
	}
	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	for (int32 Code : Row->ItemCodes)
	{
		FAsdaShopEntry E;
		E.Code = Code;
		// Пятое поле = ПОТОЛОК СТОПКИ (`ItemRecord->flagWord176`, ≤ 1 ⇒ 1); у самоцвета — 0.
		if (IT && IT->IsSowelCode(Code)) { E.Amount = 0; }
		else
		{
			const FAsdaItemRecord* R = IT ? IT->GetItem(Code) : nullptr;
			const int32 Cap = R ? R->FlagWord176 : 0;
			E.Amount = (Cap <= 1) ? 1 : Cap;
		}
		Items.Add(E);
	}
}

void UAsdaNpcShopSubsystem::BuildSellListSpendBag()
{
	// `shopBuildBuybackItemList@0xE31B50`: 60 записей контейнера 2, пропуская пустые и ЗОЛОТО 20551.
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	for (int32 i = 0; i < UAsdaInventorySubsystem::kSpendSlots; ++i)
	{
		const FAsdaItemInstance* It = Inv->GetCell(2, i);
		if (!It || It->ItemCode <= 0 || It->ItemCode == UAsdaInventorySubsystem::kGoldItemCode) { continue; }
		FAsdaShopEntry E;
		E.Code       = It->ItemCode;
		E.SlotIndex  = It->SlotIndex;      // запись+5
		E.ArrayIndex = i;
		E.Container  = It->ContainerType;  // запись+4
		E.Amount     = It->Amount;         // запись+9
		Items.Add(E);
	}
}

void UAsdaNpcShopSubsystem::BuildSellListShellBag()
{
	// `shopBuildSellableItemList@0xE31DB0`: 30 записей контейнера 1 с `ContainerType == 1`;
	//   запись ВТОРОЙ таблицы (`special_itemlist`) берётся только при нулевом байте +50 («не привязана»).
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv || !IT) { return; }
	for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i)
	{
		const FAsdaItemInstance* It = Inv->GetCell(1, i);
		if (!It || It->ItemCode <= 0 || It->ContainerType != 1) { continue; }
		const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);
		FAsdaSpecialItemlistRow Sp;
		const bool bSpecial = IT->GetSpecialItem(It->ItemCode, Sp);
		if (!R && !bSpecial) { continue; }
		if (!R && bSpecial)
		{
			// Байт +50 записи слота — «привязан»; эталон такие в список продажи НЕ кладёт.
			const bool bBound = It->Raw.IsValidIndex(50) && It->Raw[50] != 0;
			if (bBound) { continue; }
		}
		FAsdaShopEntry E;
		E.Code       = It->ItemCode;
		E.SlotIndex  = It->SlotIndex;
		E.ArrayIndex = i;
		E.Container  = It->ContainerType;
		E.Amount     = It->Amount;
		Items.Add(E);
	}
}

void UAsdaNpcShopSubsystem::RecalcPages()
{
	// `NPCShop_OpenWindow`: страниц = Size/6, остаток даёт +1. Полоса скрыта при ≤ 6 страниц.
	TotalPages = Items.Num() / kGridCols;
	if ((Items.Num() % kGridCols) > 0) { ++TotalPages; }
	Page = FMath::Clamp(Page, 0, GetMaxPage());
}

int32 UAsdaNpcShopSubsystem::GetMaxPage() const
{
	return FMath::Max(0, TotalPages - kGridRows);
}

void UAsdaNpcShopSubsystem::SetPage(int32 NewPage)
{
	const int32 P = FMath::Clamp(NewPage, 0, GetMaxPage());
	if (P == Page) { return; }
	Page = P;
	RefreshList();
	LayoutScrollBar();
	HideCursors();
}

void UAsdaNpcShopSubsystem::ScrollBy(int32 DeltaPages)
{
	SetPage(Page + DeltaPages);
}

// ═════════════════════════════ РЕНДЕР ══════════════════════════════════════════════════════════

void UAsdaNpcShopSubsystem::ApplyTitles()
{
	// `npcShop_ShowWindow@0xE26280` (типы 5/7) и `shopCraftWnd_openAndSelectTab@0xE2C4F0` (6/8/10).
	// ★★★ ЗАГОЛОВКИ — `HitTestInvisible`, А НЕ `SelfHitTestInvisible` (дефект порта, найден по логу
	//   2026-08-26: «первые 4 предмета не кладутся в корзину»).
	//   Разница принципиальна: `SelfHitTestInvisible` снимает хит-тест ТОЛЬКО с самой группы, а её
	//   ДЕТИ мышь по-прежнему ловят. Картинка заголовка лежит поверх верхнего ряда витрины и
	//   съедала клики по ячейкам 0…3 — до обработчика они вообще не доходили (в логе кликов по ним
	//   нет, а по ячейке 5 есть). Заголовок чисто декоративный, обработчиков у него нет ни одного,
	//   поэтому закрываем хит-тест ВСЕМУ поддереву.
	auto SetVis = [](UWidget* W, bool bShow)
	{
		AsdaUi::SetShownNoHit(W, bShow);
	};
	const bool bBuy = (ShopType == 5 || ShopType == 7);
	// Иконка заголовка: 5 и 6 → «Разное», 7 и 8 → «Самоцветы», 10 → «Оружейный».
	SetVis(GrpMiscTitle,   ShopType == 5 || ShopType == 6);
	SetVis(GrpSowelTitle,  ShopType == 7 || ShopType == 8);
	SetVis(GrpWeaponTitle, ShopType == 10);
	// Кнопки: в режиме покупки живёт только BUYBUT, в режиме продажи — только SELLBUT
	//   (у второй кнопки соответствующий слот vtable ПУСТ, см. шапку .h).
	AsdaUi::SetShownHit(BtnBuy, bBuy);
	AsdaUi::SetShownHit(BtnSell, !bBuy);
	AsdaUi::SetShownDecor(CapBuy, bBuy);
	AsdaUi::SetShownDecor(CapSell, !bBuy);

	// Подписи колонок: у покупки 2281/2282/111914, у продажи 2284/2285/2286.
	auto SetTxt = [](UTextBlock* T, const FString& S) { if (T && !S.IsEmpty()) { T->SetText(FText::FromString(S)); } };
	SetTxt(TxtList00, Loc(bBuy ? ShopLoc::kHdrShopBuy : ShopLoc::kHdrShopSell));
	SetTxt(TxtList02, Loc(bBuy ? ShopLoc::kHdrBuy     : ShopLoc::kHdrSell));
	// ★ У покупки эталон ставит сюда СНАЧАЛА 2283, а следующей строкой перезаписывает на 111914 —
	//   побеждает 111914 (npc_mob_pet.c:6260-6263). У продажи стоит 2286.
	SetTxt(TxtList04, Loc(bBuy ? ShopLoc::kPriceLabel : ShopLoc::kHdrSellPrice));
}

void UAsdaNpcShopSubsystem::RefreshList()
{
	if (!bControlsReady) { return; }
	if (GridClicks) { GridClicks->Refresh(); }        // иконки страницы (общая точка)
	for (int32 C = 0; C < kGridCells; ++C) { UpdateCellVisual(C, /*bCart*/false); }
}

void UAsdaNpcShopSubsystem::RefreshCart()
{
	if (!bControlsReady) { return; }
	if (CartClicks) { CartClicks->Refresh(); }
	for (int32 C = 0; C < kCartCells; ++C) { UpdateCellVisual(C, /*bCart*/true); }
}

// Счётчик под иконкой — правила `ItemSlot_UpdateIcon@0xE23FC0` (item_inventory.c:56293).
void UAsdaNpcShopSubsystem::UpdateCellVisual(int32 Cell, bool bCart)
{
	TArray<FAsdaShopCellUi>& Ui = bCart ? CartUi : GridUi;
	if (!Ui.IsValidIndex(Cell) || !Ui[Cell].CountText) { return; }
	UTextBlock* Tb = Ui[Cell].CountText;

	const FAsdaShopEntry* E = nullptr;
	if (bCart) { if (Cell < kCartCells && !Cart[Cell].IsEmpty()) { E = &Cart[Cell]; } }
	else
	{
		const int32 Abs = Cell + kGridCols * Page;
		if (Items.IsValidIndex(Abs) && !Items[Abs].IsEmpty()) { E = &Items[Abs]; }
	}
	if (!E) { Tb->SetText(FText::GetEmpty()); return; }

	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(E->Code) : nullptr;
	FString S;
	if (R)
	{
		const bool bBuyGrid = (!bCart && (ShopType == 5 || ShopType == 7));
		if (bBuyGrid)
		{
			// Витрина покупки: число показывается ТОЛЬКО у стекуемых (`flagWord176 != 0`)
			//   и равно потолку стопки, положенному загрузчиком в пятое поле.
			if (R->FlagWord176 != 0 && E->Amount != 0) { S = FString::FromInt(E->Amount); }
		}
		else if (R->ItemSubType == 32 || E->Container == 1)
		{
			// Соул-стоун и всё из контейнера 1 не стекуются — числа нет.
		}
		else if ((E->Amount != 0 && E->Code != UAsdaInventorySubsystem::kGoldItemCode && R->TradableFlag != 0)
			|| R->FlagWord176 != 0)
		{
			S = FString::FromInt(E->Amount);
		}
	}
	Tb->SetText(S.IsEmpty() ? FText::GetEmpty() : FText::FromString(S));
}

void UAsdaNpcShopSubsystem::RefreshMoneyTexts()
{
	// `NPCShop_RenderList`: 1309 = бегущая сумма сделки, 1310 = деньги игрока.
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (TxtMoney) { TxtMoney->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0))); }
	if (TxtTotal)
	{
		// Эталон красит сумму в красный, когда денег не хватает (`<FC=FFFF0000>`); у нас цвет
		//   ставится напрямую — разметку в TextBlock писать не надо.
		const int32 Gold = Inv ? Inv->GetGold() : 0;
		TxtTotal->SetText(FText::FromString(AsdaUi::FormatThousands(RunningTotal)));
		TxtTotal->SetColorAndOpacity(FSlateColor(Gold >= RunningTotal
			? FLinearColor::White
			: AsdaUi::SrgbHexToLinear(TEXT("FFFF0000"))));
	}
}

void UAsdaNpcShopSubsystem::RefreshTaxTexts()
{
	// `npcShop_UpdateTradeTaxDisplay@0xE29760` (покупка) и `shopComputePriceAndUpdateUI@0xE31FB0`
	//   (продажа). Разница между ними ровно в двух местах:
	//     • ПОКУПКА своей фракцией — СКИДКА: 1380 = нотис 111915, 1383 = сумма − налог, `TaxDelta` = −налог;
	//     • ПОКУПКА чужой  фракцией — НАЦЕНКА: 1380 = 111916, 1383 = сумма + налог, `TaxDelta` = +налог;
	//     • ПРОДАЖА своей  фракцией — БОНУС:  1380 = 111917, 1383 = сумма + бонус;
	//     • ПРОДАЖА чужой  фракцией — НАЛОГ:  1380 = 111918, 1383 = сумма − налог.
	//   Карта не военная, город не занят или у игрока нет фракции ⇒ 1380 = «-», 1381 = 0, 1383 = сумма.
	auto SetTxt = [](UTextBlock* T, const FString& S) { if (T) { T->SetText(FText::FromString(S)); } };
	const int32 Slot = WarSlot();
	int32 Percent = 0; bool bNeutral = true; bool bNoFaction = true;
	const bool bOwn = (Slot >= 0) && IsOwnFactionCity(Slot, Percent, bNeutral, &bNoFaction);
	const bool bBuy = (ShopType == 5 || ShopType == 7);

	// Не военная карта или нулевой процент — налога нет вовсе (эталон `shopComputePriceAndUpdateUI
	//   @0xE31FB0`, ветка `warMapSlot == -1`).
	if (Slot < 0 || Percent <= 0)
	{
		SetTxt(TxtTaxPct, TEXT("-"));
		SetTxt(TxtTaxVal, AsdaUi::FormatThousands(0));
		SetTxt(TxtNetVal, AsdaUi::FormatThousands(RunningTotal));
		TaxDelta = 0;
		return;
	}
	// ★ ИСПРАВЛЕНИЕ денежного дефекта (`fix_helpers_dedup.md`, тот же корень, что у `ApplyWarPercent`):
	//   доля обнуляется ТОЛЬКО когда у игрока нет фракции (`bNoFaction`) — эталон
	//   `shop_trade.c:21510` `if (φ == −1) taxAmount = 0;`. На НИЧЬЁМ городе (Owner == 3, фракция
	//   у игрока есть) эталон налог ПРИМЕНЯЕТ к НЕТТО и списанию (`field 56 = RunningTotal − tax`),
	//   а гасит ТОЛЬКО подписи процента и суммы (`if cityFaction == 3` → «-»/«-»). Прежний код
	//   уходил в ветку «налога нет» на ничьём городе и терял и НЕТТО, и `TaxDelta` (гейт покупки).
	int32 Tax = bNoFaction ? 0 : (int32)((double)RunningTotal / 100.0 * (double)Percent);
	// Знак итога: у покупки своя фракция вычитает, чужая прибавляет; у продажи наоборот.
	int32 Net = bBuy ? (bOwn ? RunningTotal - Tax : RunningTotal + Tax)
	                 : (bOwn ? RunningTotal + Tax : RunningTotal - Tax);
	// ★★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] 27.09.2026 (itemsfix, МЕЛК-6 ревизии items_consistency). ПРОДАЖА при
	//   включённом потолке сервера (`asda.Shop.ServerSellCap 1`): НЕТТО считается В ПОРЯДКЕ СЕРВЕРА, а не
	//   «налог от итоговой суммы». `CharItemSell` (GameServer.c) для КАЖДОЙ позиции:
	//     1) цена единицы: предмет `+37` (:103477, :103574-103617 — ветки сумки) / самоцвет `+15` (:103522);
	//     2) доля города ДО всего остального (:103478, :103627 — предмет; :103523 — самоцвет):
	//        `hasDiscount` = карта военного города, владелец 0…2 (НЕ ничей), ставка > 0, у игрока есть
	//        фракция (:103408-103423); `v34 = (int)(unit*ставка/100.0)`; при `v34 > 0` своя фракция +v34,
	//        чужая −v34;
	//     3) бафф 38: `unit = (int)(unit*b/100.0 + unit)` (:103651-103657);
	//     4) потолок `min(unit, buy − 25·buy/100)` при `buy > 1` (:103659-103678);
	//     5) × количество — уже после (у спец-предметов второй таблицы доли и потолка нет).
	//   Порт раньше брал потолок от цены БЕЗ доли, а долю — от итога ПОСЛЕ потолка: когда потолок
	//   срабатывал на военной карте, НЕТТО расходилось с тем, что начислит сервер. Теперь НЕТТО = валовая
	//   сумма окна (как была, по эталону клиента) + Σ по позициям корзины «сервер − валовая цена позиции»;
	//   подпись налога = |НЕТТО − валовая|. Ничей город: у сервера доли нет — НЕТТО = валовая (подписи «-»,
	//   как было). Покупка и `asda.Shop.ServerSellCap 0` — прежняя формула оригинала без изменений.
	if (!bBuy && CVarShopServerSellCap.GetValueOnGameThread() != 0)
	{
		UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const bool bDiscount = !bNeutral && Percent > 0;              // == `hasDiscount` сервера
		auto ServerUnit = [&](int32 U, int32 BuyPrice)
		{
			if (bDiscount)
			{
				const int32 V34 = (int32)((double)((int64)U * (int64)Percent) / 100.0);
				if (V34 > 0) { U = bOwn ? U + V34 : U - V34; }
			}
			if (SellPriceBuffPercent >= 0)
			{
				U = (int32)((double)((int64)U * (int64)SellPriceBuffPercent) / 100.0 + (double)U);
			}
			return ServerSellUnitCap(U, BuyPrice);
		};
		int64 Adjust = 0;
		for (int32 c = 0; c < kCartCells && IT; ++c)
		{
			const FAsdaShopEntry& E = Cart[c];
			if (E.IsEmpty() || E.Code <= 0) { continue; }
			int32 Srv = -1;
			if (IT->IsSowelCode(E.Code))
			{
				FAsdaSowellRow Row;
				if (IT->GetSowell(E.Code, Row)) { Srv = ServerUnit(Row.SellPrice, Row.BuyPrice); }
			}
			else if (!IT->IsSpecialItemCode(E.Code))
			{
				if (const FAsdaItemRecord* R = IT->GetItem(E.Code))
				{
					const int32 U = ServerUnit(R->SubCategory, R->Price);
					// Количество — ровно как `PriceOf` (стек только при TradableFlag == 1 и FlagWord176 == 0).
					Srv = (E.Amount <= 0 || R->FlagWord176 != 0) ? U : (R->TradableFlag == 1 ? E.Amount * U : U);
				}
			}
			if (Srv >= 0) { Adjust += (int64)Srv - (int64)PriceOf(E.Code, E.Amount, /*bBuy*/false); }
		}
		Net = (int32)FMath::Clamp<int64>((int64)RunningTotal + Adjust, 0, MAX_int32);
		Tax = FMath::Abs(Net - RunningTotal);
	}
	const int32 LocId = bBuy ? (bOwn ? ShopLoc::kBuyDiscount : ShopLoc::kBuyMarkup)
	                         : (bOwn ? ShopLoc::kSellBonus   : ShopLoc::kSellTax);
	// Эталон отдаёт loc-строку в `sprintf` как ФОРМАТ (`%d%%`) — это printf-подстановка, не слоты.
	UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	TArray<FString> Args; Args.Add(FString::FromInt(Percent));
	SetTxt(TxtTaxPct, TS ? TS->FormatPrintfById(LocId, Args) : FString::Printf(TEXT("%d%%"), Percent));
	SetTxt(TxtTaxVal, AsdaUi::FormatThousands(Tax));
	SetTxt(TxtNetVal, AsdaUi::FormatThousands(Net));
	// `TaxDelta` участвует в проверке «хватит ли денег» вместе с `RunningTotal` (эталон F+432).
	TaxDelta = Net - RunningTotal;
	// ★ Ничей город (Owner == 3): эталон гасит подписи процента и суммы налога, но НЕТТО оставляет.
	if (bNeutral) { SetTxt(TxtTaxPct, TEXT("-")); SetTxt(TxtTaxVal, TEXT("-")); }
}

// Порт `uiPositionControlFromVec2AndShow@0xE23C70` (витрина, курсор 1314) и `uiControl_PositionAndShow@0xE23D20` (корзина,
//   курсор 1315), ui_core.c:71127/71150: `Class_GetVec2_74(ячейка)` → `CControl_SetField74_75(курсор)` → `CControl_SetVisible(1)`;
//   кадры курсора эталон НЕ перезапускает.
// ★ ИСПРАВЛЕНИЕ 12.10.2026 (дедуп №2, В3-S10, остаток U19): общая точка `AsdaUi::PlaceFrameOver(курсор, ячейка, без перезапуска)`.
//   Было: база родителя курсора снималась ОДИН раз при привязке (`GridSelParentRoot/CartSelParentRoot`); курсор лежит во вложенной
//   группе `NPCSHOP_BACKPANNEL00/01_grp`, и после перетаскивания окна (оконная подсистема сдвигает слоты детей корня) кэш
//   устаревал — курсор вставал со сдвигом на величину перетаскивания. Эталон держит позиции относительно окна — курсор всегда
//   на ячейке. Слота нет / ячейки нет — курсор не трогается (как было). Видимость прежняя — `HitTestInvisible`.
void UAsdaNpcShopSubsystem::PlaceCursorOnCell(UAsdaAnimatedPlane* Cur, const UWidget* Icon)
{
	if (!Cur || !Icon || !AsdaUi::SlotOf(Cur)) { return; }
	AsdaUi::PlaceFrameOver(Cur, Icon, /*bRestartAnim*/ false);
}

void UAsdaNpcShopSubsystem::HideCursors()
{
	// `npcShopHideCursorKillFocus@0xE23D90`.
	if (GridSel) { GridSel->SetVisibility(ESlateVisibility::Collapsed); }
	if (CartSel) { CartSel->SetVisibility(ESlateVisibility::Collapsed); }
}

// ═════════════════════════════ ЦЕНА ════════════════════════════════════════════════════════════

int32 UAsdaNpcShopSubsystem::WarSlot() const
{
	// ★ ОБЩАЯ ТОЧКА (закон `port-shared-points`): ту же лесенку «карта → слот города» зовут окно
	//   ремонта (57300) и военный магазин (67200). Раньше она жила здесь копией.
	return AsdaUi::WarCitySlot(GetGameInstance());
}

// ★ «Своя ли фракция владеет этим городом» — тоже общая точка `AsdaUi::WarCityOwnedBySelf`.
//   `bOutNoFaction` (опц.) — «у игрока нет фракции»: нужен ЦЕНЕ отдельно от `bOutNeutral` (подписи).
bool UAsdaNpcShopSubsystem::IsOwnFactionCity(int32 Slot, int32& OutPercent, bool& bOutNeutral, bool* bOutNoFaction) const
{
	return AsdaUi::WarCityOwnedBySelf(GetGameInstance(), Slot, OutPercent, bOutNeutral, bOutNoFaction);
}

int32 UAsdaNpcShopSubsystem::ApplyWarPercent(int32 Value, bool bAdd) const
{
	// 1:1 `uniconWar_ApplyOptionPercent@0xBCEEC0` (entity_char.c:44748):
	//   доля = Value/100 * процент; своя фракция ⇒ `a2 ? +доля : −доля`, чужая ⇒ наоборот,
	//   а при `фракция игрока == -1` доля обнуляется.
	const int32 Slot = WarSlot();
	if (Slot < 0) { return Value; }
	int32 Percent = 0; bool bNeutral = true; bool bNoFaction = true;
	const bool bOwn = IsOwnFactionCity(Slot, Percent, bNeutral, &bNoFaction);
	// ★ ИСПРАВЛЕНИЕ денежного дефекта (`fix_helpers_dedup.md`): доля обнуляется ТОЛЬКО когда у
	//   игрока НЕТ фракции (`bNoFaction`), а НЕ на ничьём городе. Оригинал (эталон) на ничьём
	//   городе долю ПРИМЕНЯЕТ (`uniconWar_ApplyOptionPercent@0xBCEEC0` — ветки «город ничей» нет).
	//   Раньше здесь стоял `if (bNeutral) return Value;` — он терял наценку на ничьём городе.
	if (Percent <= 0 || bNoFaction) { return Value; }
	const int32 Delta = (int32)((double)Value / 100.0 * (double)Percent);
	if (bOwn) { return bAdd ? (Value + Delta) : (Value - Delta); }
	return bAdd ? (Value - Delta) : (Value + Delta);
}

int32 UAsdaNpcShopSubsystem::PriceOf(int32 ItemCode, int32 Quantity, bool bBuy) const
{
	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return 0; }
	// Самоцвет: покупка — `sowell_list+0x34` (BuyPrice), продажа — `+0x30` (SellPrice).
	if (IT->IsSowelCode(ItemCode))
	{
		FAsdaSowellRow Row;
		if (!IT->GetSowell(ItemCode, Row)) { return 0; }
		const int32 SowUnit = ApplySellPriceBuff(bBuy ? Row.BuyPrice : Row.SellPrice, bBuy);
		// ★★ Потолок сервера для самоцвета — по его цене покупки (`+19` серверной таблицы = BuyPrice).
		return bBuy ? SowUnit : ServerSellUnitCap(SowUnit, Row.BuyPrice);
	}
	// Запись второй таблицы (в эталоне встречается только в ветке ПРОДАЖИ — `Table2Record->value_108`).
	if (IT->IsSpecialItemCode(ItemCode))
	{
		FAsdaSpecialItemlistRow Sp;
		if (!IT->GetSpecialItem(ItemCode, Sp)) { return 0; }
		return ApplySellPriceBuff(bBuy ? 0 : Sp.Value108, bBuy);
	}
	const FAsdaItemRecord* R = IT->GetItem(ItemCode);
	if (!R) { return 0; }
	// ★ ПОКУПКА — поле +0xA4 `Price`; ПРОДАЖА — поле +0xA0 `SubCategory` (ИМЯ ВРЁТ: это цена продажи).
	int32 Unit = bBuy ? R->Price : R->SubCategory;
	// ★ Бафф «ставка цены» (тип 38) — ТОЛЬКО у продажи, ТОЛЬКО на цену ЕДИНИЦЫ и ДО умножения на
	//   количество: эталон правит `unitPrice` сразу после выбора поля цены и до ветвления по стеку.
	Unit = ApplySellPriceBuff(Unit, bBuy);
	// ★★ Потолок сервера — на цену ЕДИНИЦЫ, после баффа и ДО умножения на количество (сервер режет
	//   цену позиции, количество применяет потом).
	if (!bBuy) { Unit = ServerSellUnitCap(Unit, R->Price); }
	if (Quantity <= 0 || R->FlagWord176 != 0) { return Unit; }
	if (R->TradableFlag == 1) { return Quantity * Unit; }
	return Unit;
}

int32 UAsdaNpcShopSubsystem::ServerSellUnitCap(int32 SellUnit, int32 BuyPrice)
{
	// `CharItemSell` (GameServer.c:103662-103684): `if (buy > 1) { v = 25*buy/100; if (unit > buy - v) unit = buy - v; }`.
	if (CVarShopServerSellCap.GetValueOnGameThread() == 0 || BuyPrice <= 1) { return SellUnit; }
	// ★ 27.09.2026 (itemsfix, МЕЛК-7): арифметика 1:1 с сервером — `25*buy` в 32 битах (imul с переносом),
	//   затем знаковое деление (idiv). При buy > 85 899 345 сервер переполняется; порт считал в int64 и
	//   показывал бы другое. На данных 27.09 разницы нет (у кодов с buy 1e8/5e8 потолок не срабатывает),
	//   но «ровно то, что начислит сервер» — значит и переполнение как у сервера.
	const int32 V = (int32)(uint32)(25u * (uint32)BuyPrice) / 100;
	const int32 Cap = (int32)((uint32)BuyPrice - (uint32)V);
	return SellUnit > Cap ? Cap : SellUnit;
}

// Формула 1:1 (shop_trade.c:21449-21453). Доказательство недостижимости ветки в оригинале —
//   в объявлении `SetSellPriceBuffPercent` (.h): писателя `g_pActiveBuffEntry` в `.text` нет вовсе.
int32 UAsdaNpcShopSubsystem::ApplySellPriceBuff(int32 UnitPrice, bool bBuy) const
{
	if (bBuy || SellPriceBuffPercent < 0) { return UnitPrice; }
	const double K = (double)SellPriceBuffPercent / 100.0;
	return (int32)((double)UnitPrice * K + 0.5 + (double)UnitPrice);
}

int32 UAsdaNpcShopSubsystem::CalcMaxBuyableQty(int32 ItemCode) const
{
	// `shopWnd_calcMaxBuyableQty@0xE2AEC0`.
	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!R) { return -1; }
	const int32 Rest = (Inv ? Inv->GetGold() : 0) - (TaxDelta + RunningTotal);
	if (Rest <= 0 || Rest < R->Price)
	{
		NoticeChat(ShopLoc::kNoMoney);
		return -1;
	}
	if (R->Price == 0) { return 0; }
	return Rest / ApplyWarPercent(R->Price, /*bAdd*/false);
}

void UAsdaNpcShopSubsystem::AddToRunningTotal(int32 ItemCode, int32 Quantity, bool bAdd)
{
	const bool bBuy = (ShopType == 5 || ShopType == 7);
	const int32 Unit = PriceOf(ItemCode, Quantity, bBuy);
	RunningTotal = bAdd ? (RunningTotal + Unit) : (RunningTotal - Unit);
	RunningTotal = FMath::Max(0, RunningTotal);
	RefreshMoneyTexts();
	RefreshTaxTexts();
}

// ═════════════════════════════ КОРЗИНА ═════════════════════════════════════════════════════════

int32 UAsdaNpcShopSubsystem::FindCartSlotWithSame(int32 Code, int32 Key) const
{
	// `slotFindMatchingPair@0xE32680`: совпадение по (код, второй ключ) — у продажи это слот записи.
	for (int32 i = 0; i < kCartCells; ++i)
	{
		if (Cart[i].Code == Code && Cart[i].SlotIndex == Key) { return i; }
	}
	return -1;
}

// `shopSelectTabById@0xE29FC0`: совпадение ТОЛЬКО по коду; индекс найденной ячейки эталон
//   запоминает в F+424 и дальше докладывает именно в неё.
int32 UAsdaNpcShopSubsystem::FindCartSlotWithCode(int32 Code) const
{
	for (int32 i = 0; i < kCartCells; ++i)
	{
		if (Cart[i].Code == Code) { return i; }
	}
	return -1;
}

int32 UAsdaNpcShopSubsystem::FindFreeCartSlot() const
{
	for (int32 i = 0; i < kCartCells; ++i) { if (Cart[i].IsEmpty()) { return i; } }
	return -1;
}

void UAsdaNpcShopSubsystem::PutIntoCart(int32 CartSlot, int32 ListIdx, int32 Quantity)
{
	if (CartSlot < 0 || CartSlot >= kCartCells || !Items.IsValidIndex(ListIdx)) { return; }
	const bool bBuy = (ShopType == 5 || ShopType == 7);
	const FAsdaShopEntry Src = Items[ListIdx];
	if (Src.IsEmpty()) { return; }
	// ★ Эталон кладёт РОВНО переданное число: у покупки без нумпада это пятое поле записи
	//   витрины (`cart[+128] = entry[16]`) — размер фиксированной пачки. У самоцвета там ноль,
	//   и это нормально: `Send_CharItemBuy_1` для самоцвета всё равно ставит в пакет 1.
	const int32 Qty = Quantity;

	FAsdaShopEntry& Dst = Cart[CartSlot];
	Dst.Code       = Src.Code;
	Dst.SlotIndex  = Src.SlotIndex;
	Dst.ArrayIndex = Src.ArrayIndex;
	Dst.Container  = Src.Container;
	Dst.ListIndex  = ListIdx;
	Dst.Amount    += Qty;
	// Эталон: `UpdateTradeTaxDisplay(код, ДОБАВЛЕННОЕ количество, isBuy)` — цена считается по дельте.
	AddToRunningTotal(Dst.Code, Qty, /*bAdd*/true);

	if (!bBuy)
	{
		// `shopBuySlotMoveItem@0xE30AA0`: у ПРОДАЖИ количество СНИМАЕТСЯ с записи списка,
		//   а когда снято всё — запись обнуляется (`memset(destSlotRec, 0, 0x14)`), оставляя дырку.
		if (Qty >= Items[ListIdx].Amount) { Items[ListIdx].Clear(); }
		else { Items[ListIdx].Amount -= Qty; }
	}
	RefreshList();
	RefreshCart();
	HideCursors();
}

void UAsdaNpcShopSubsystem::TakeFromCart(int32 CartSlot)
{
	// `npcShopClearTradeSlot@0xE28330` (покупка) / `shopCraftWnd_onSlotClick@0xE2CAD0` (продажа):
	//   вычесть цену по ПОЛНОМУ количеству ячейки, вернуть штуки в список (у продажи — по
	//   запомненному индексу вектора), очистить ячейку корзины.
	if (CartSlot < 0 || CartSlot >= kCartCells || Cart[CartSlot].IsEmpty()) { return; }
	const FAsdaShopEntry E = Cart[CartSlot];
	AddToRunningTotal(E.Code, E.Amount, /*bAdd*/false);
	if (ShopType != 5 && ShopType != 7 && Items.IsValidIndex(E.ListIndex))
	{
		FAsdaShopEntry& Back = Items[E.ListIndex];
		Back.Code       = E.Code;
		Back.SlotIndex  = E.SlotIndex;
		Back.ArrayIndex = E.ArrayIndex;
		Back.Container  = E.Container;
		Back.Amount    += E.Amount;
	}
	Cart[CartSlot].Clear();
	SelCartCell = -1;
	SelMode = 0;
	RefreshList();
	RefreshCart();
	HideCursors();
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::NpcShop);
	}
	TipCell = -1;
}

// ═════════════════════════════ КЛИКИ ═══════════════════════════════════════════════════════════

// ★★★ ДВЕ РАЗНЫЕ ЭТАЛОННЫЕ ВЕТКИ, А НЕ ОДНА (исправление бага «нельзя купить два одинаковых»).
//   У покупки и у продажи клик по товару обслуживают РАЗНЫЕ функции с РАЗНЫМ порядком поиска
//   ячейки корзины — они лежат в разных слотах vtable своих классов:
//     ПОКУПКА  `npcShop_BuyItemIntoSlot@0xE27800` (CShopNpc, vtbl+44):
//        1) ищет ПЕРВУЮ СВОБОДНУЮ ячейку `for (i=0;i<5;++i) if (cart[i].code <= 0) …` и кладёт туда;
//        2) в хвост (все пять заняты) попадает ТОЛЬКО когда свободных нет — там `shopSelectTabById
//           @0xE29FC0` ищет ячейку с ТЕМ ЖЕ КОДОМ и предлагает докладку нумпадом, иначе нотис 2312.
//     ПРОДАЖА  `npcShop_onItemSlotClick@0xE33060` (CShopSell, vtbl+36):
//        1) СНАЧАЛА `slotFindMatchingPair@0xE32680` по ПАРЕ (код, слот контейнера) — это та же самая
//           стопка инвентаря, класть её во вторую ячейку нельзя;
//        2) только если пары нет — ищет свободную, а когда свободных нет, просто `return` (БЕЗ нотиса).
//   ⚠ ПРИЧИНА БАГА: обе ветки были слиты в одну по «продажному» порядку, поэтому второй такой же
//   товар в режиме ПОКУПКИ находил первую ячейку по коду и упирался в нотис 2312 вместо того,
//   чтобы занять следующую свободную.
void UAsdaNpcShopSubsystem::OnGridCellClicked(int32 Cell)
{
	// ★ ДИАГНОСТИКА (жалоба владельца 2026-08-26: «первые 4 предмета не кладутся в корзину»).
	//   Раньше молчаливых `return` тут было четыре, и по логу нельзя было понять, какой сработал.
	if (!bOpen || bPending)
	{
		UE_LOG(LogAsdaShop, Warning, TEXT("[магазин] клик по витрине %d отклонён: открыт=%d, ждём ответ=%d"),
			Cell, bOpen ? 1 : 0, bPending ? 1 : 0);
		return;
	}
	const int32 Abs = Cell + kGridCols * Page;
	if (!Items.IsValidIndex(Abs) || Items[Abs].IsEmpty())
	{
		UE_LOG(LogAsdaShop, Warning,
			TEXT("[магазин] клик по витрине: ячейка %d, страница %d → индекс %d — %s"),
			Cell, Page, Abs,
			Items.IsValidIndex(Abs) ? TEXT("запись ПУСТА (код <= 0)") : TEXT("индекс вне списка"));
		return;
	}
	UE_LOG(LogAsdaShop, Log,
		TEXT("[магазин] клик по витрине: ячейка %d, страница %d → индекс %d, код %d, количество записи %d"),
		Cell, Page, Abs, Items[Abs].Code, Items[Abs].Amount);

	SelGridCell = Cell;
	SelAbsIndex = Abs;
	SelMode = 0;
	PlaceCursorOnCell(GridSel, GridUi.IsValidIndex(Cell) ? GridUi[Cell].Icon.Get() : nullptr);

	const FAsdaShopEntry Src = Items[Abs];
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(Src.Code) : nullptr;
	const bool bSowel = IT && IT->IsSowelCode(Src.Code);
	const bool bBuy = (ShopType == 5 || ShopType == 7);

	// ★ «Спросить количество нумпадом» — общее условие обеих веток эталона:
	//   `!ItemRecord->flagWord176 && ItemRecord->tradableFlag`. ⚠ `flagWord176` — это ПОТОЛОК
	//   ФИКСИРОВАННОЙ ПАЧКИ (его же загрузчик витрины кладёт пятым полем записи, а `itemGetSellValue
	//   @0xE29EE0` при ненулевом поле НЕ умножает цену на количество). То есть ненулевое поле =
	//   «товар продаётся пачкой целиком, количество не спрашиваем».
	//   У ПОКУПКИ эталон добавляет ещё два исключения (`npcShop_BuyItemIntoSlot`): самоцвет и
	//   `itemSubType == 32` (камень души) — им нумпад не показывают.
	const bool bStackAsk = R && R->FlagWord176 == 0 && R->TradableFlag != 0;
	const bool bAskQtyBuy = bStackAsk && !bSowel && R->ItemSubType != 32;

	if (bBuy)
	{
		// ── ПОКУПКА: сперва СВОБОДНАЯ ячейка (`npcShop_BuyItemIntoSlot@0xE27800`) ──
		const int32 Free = FindFreeCartSlot();
		if (Free >= 0)
		{
			if (bAskQtyBuy && WM)
			{
				// Максимум — `shopWnd_calcMaxBuyableQty@0xE2AEC0` (сколько влезает в оставшиеся деньги).
				const int32 Max = CalcMaxBuyableQty(Src.Code);
				if (Max <= 0)
				{
					UE_LOG(LogAsdaShop, Warning,
						TEXT("[магазин] код %d: не хватает даже на одну штуку (максимум %d) — окно количества не открываем"),
						Src.Code, Max);
					return;
				}
				WM->ShowNumPad(Max, [this, Free, Abs](bool bOk, int32 Qty)
				{
					// Отмена нумпада — эталонный vtbl+16 с кодом 0: только перерисовка.
					if (!bOk || Qty <= 0) { RefreshList(); return; }
					PutIntoCart(Free, Abs, Qty);
					NoticeChat(ShopLoc::kBought);   // vtbl+16(1) → `shopSellListRefreshScroll` (нотис 2236)
				});
				return;
			}
			// Без нумпада: гейт денег (`availableGold = деньги − (F+432 + F+428)`), нотис 2231 В ЧАТ.
			UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			const int32 Need = PriceOf(Src.Code, 0, /*bBuy*/true);
			const int32 Rest = (Inv ? Inv->GetGold() : 0) - (TaxDelta + RunningTotal);
			if (Rest <= 0 || Rest < Need)
			{
				UE_LOG(LogAsdaShop, Warning,
					TEXT("[магазин] код %d: отказ по деньгам — цена %d, золото %d, в корзине %d, налог %d ⇒ остаток %d"),
					Src.Code, Need, Inv ? Inv->GetGold() : 0, RunningTotal, TaxDelta, Rest);
				NoticeChat(ShopLoc::kNoMoney);
				return;
			}
			// ★ Количество = ПЯТОЕ ПОЛЕ записи витрины (`cart[+128] = entry[16]`), то есть размер
			//   фиксированной пачки: у обычной вещи 1, у пачки стрел — вся пачка, у самоцвета 0.
			PutIntoCart(Free, Abs, Src.Amount);
			return;
		}
		// ── Корзина ПОЛНАЯ: докладка в ячейку с тем же кодом либо отказ ──
		const int32 SameCode = FindCartSlotWithCode(Src.Code);
		if (SameCode >= 0 && bStackAsk && WM)
		{
			const int32 Max = CalcMaxBuyableQty(Src.Code);
			if (Max <= 0) { return; }
			WM->ShowNumPad(Max, [this, SameCode, Abs](bool bOk, int32 Qty)
			{
				if (!bOk || Qty <= 0) { RefreshList(); return; }
				PutIntoCart(SameCode, Abs, Qty);
				NoticeChat(ShopLoc::kBought);
			});
			return;
		}
		NoticeMsgBox(ShopLoc::kCartFull);   // нотис 2312 «Нет места в списке покупок.»
		return;
	}

	// ── ПРОДАЖА — общая точка `SellIntoCart` (её же зовёт одиночный ПКМ, событие 0xF) ──
	SellIntoCart(Abs);
}

// ★ Тело `npcShop_onItemSlotClick@0xE33060` (npc_mob_pet.c:7298-7462) для ячеек витрины 1320…1355 в режиме ПРОДАЖИ.
//   Вынесено 10.10.2026 из `OnGridCellClicked`, чтобы ЛКМ (двойной, требование владельца) и ПКМ (одиночный — эталонный
//   0xF → vtbl+36) шли ОДНИМ кодом. Выбор ячейки (курсор 1314) здесь не ставится: у эталона его ставит только 0xB.
void UAsdaNpcShopSubsystem::SellIntoCart(int32 Abs)
{
	if (!Items.IsValidIndex(Abs) || Items[Abs].IsEmpty()) { return; }   // `NPCShop_ItemVec_Size` / `entry[0] <= 0` → выход
	const FAsdaShopEntry Src = Items[Abs];
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(Src.Code) : nullptr;
	// Нумпад докладки — `ItemRecord && !flagWord176 && tradableFlag` (npc_mob_pet.c:7343).
	const bool bStackAsk = R && R->FlagWord176 == 0 && R->TradableFlag != 0;
	// ── ПРОДАЖА: сперва ПАРА (код, слот контейнера) — `npcShop_onItemSlotClick@0xE33060` ──
	const int32 Same = FindCartSlotWithSame(Src.Code, Src.SlotIndex);
	if (Same >= 0)
	{
		if (bStackAsk && WM)
		{
			// Максимум докладки у продажи — размер стопки в инвентаре (эталон: `entry[16]`).
			const int32 Max = Src.Amount;
			if (Max <= 0) { return; }
			WM->ShowNumPad(Max, [this, Same, Abs](bool bOk, int32 Qty)
			{
				if (!bOk || Qty <= 0) { RefreshList(); return; }
				PutIntoCart(Same, Abs, Qty);
				NoticeChat(ShopLoc::kBought);
			});
			return;
		}
		NoticeMsgBox(ShopLoc::kCartFull);
		return;
	}
	// Свободных нет ⇒ эталон просто выходит, БЕЗ нотиса (`if (freeSlotIdx >= 5) return;`).
	const int32 Free = FindFreeCartSlot();
	if (Free < 0) { return; }
	// ★ 10.10.2026 (реверс ветки 0xF): `slotKind` по диапазону кода — самоцвет (запись Sowel) 1, предмет (запись ItemDB) 2,
	//   вторая таблица (`special_itemlist`) 3; 0 — записи нет ⇒ эталон молча ничего не делает (npc_mob_pet.c:7372-7397, `if (slotKind)`).
	int32 SlotKind = 0;
	FAsdaSowellRow SowRow;
	FAsdaSpecialItemlistRow SpRow;
	if (IT && IT->IsSowelCode(Src.Code)) { SlotKind = IT->GetSowell(Src.Code, SowRow) ? 1 : 0; }
	else if (R)                          { SlotKind = 2; }
	else if (IT && IT->GetSpecialItem(Src.Code, SpRow)) { SlotKind = 3; }
	if (SlotKind == 0)
	{
		UE_LOG(LogAsdaShop, Warning, TEXT("[магазин] продажа: код %d без записи в таблицах (slotKind 0) — эталон молча выходит"), Src.Code);
		return;
	}
	// ★ Эталонный `case 2`: если предмет стекуется и его больше одной штуки — количество берётся
	//   целиком (`itemPrice = entry[16] + cart[+288]`), иначе тоже целиком. Нумпад в этой ветке
	//   эталон НЕ показывает: он спрашивает количество только при ДОКЛАДКЕ в занятую пару.
	PutIntoCart(Free, Abs, Src.Amount);
}

// Первый щелчок пары по ВИТРИНЕ — выбор товара без укладки в корзину (10.10.2026). Повторяет эталонный
//   `npcShopMapClickToItem@0xE24970` (npc_mob_pet.c:6012-6040): F+384 = ячейка, F+388 = индекс, F+420 = 0
//   (режим «витрина») и показ курсора 1314 на ячейке.
void UAsdaNpcShopSubsystem::OnGridCellSelected(int32 Cell)
{
	if (!bOpen || bPending) { return; }
	const int32 Abs = Cell + kGridCols * Page;
	if (!Items.IsValidIndex(Abs) || Items[Abs].IsEmpty()) { return; }
	SelGridCell = Cell;
	SelAbsIndex = Abs;
	SelMode = 0;
	PlaceCursorOnCell(GridSel, GridUi.IsValidIndex(Cell) ? GridUi[Cell].Icon.Get() : nullptr);
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] одиночный щелчок по витрине: ячейка %d → индекс %d выбран (в корзину — двойным)"), Cell, Abs);
}

// Первый щелчок пары по КОРЗИНЕ — только выбор: ветка корзины `npcShopMapClickToItem@0xE24970`
//   (npc_mob_pet.c:6018-6024): F+392 = ячейка, F+420 = 1 (режим «корзина»), курсор 1315 на ячейке.
void UAsdaNpcShopSubsystem::OnCartCellSelected(int32 Cell)
{
	if (!bOpen || bPending) { return; }
	if (Cell < 0 || Cell >= kCartCells || Cart[Cell].IsEmpty()) { return; }
	SelCartCell = Cell;
	SelMode = 1;
	PlaceCursorOnCell(CartSel, CartUi.IsValidIndex(Cell) ? CartUi[Cell].Icon.Get() : nullptr);
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] одиночный щелчок по корзине: ячейка %d выбрана (вернуть — двойным)"), Cell);
}

void UAsdaNpcShopSubsystem::OnSellCellRightClick(int32 Cell, bool bCart)
{
	if (!bOpen || bPending) { return; }
	// Покупка (5/7): слот vtbl+36 ПУСТ (`0xE231F0`, таблица в `Docs/UI/Trade/NPCSHOP_WIN.md`) — ПКМ ничего не делает.
	if (ShopType == 5 || ShopType == 7) { return; }
	if (bCart)
	{
		// Ячейки 0x550…0x554 с позицией (npc_mob_pet.c:7328-7333): F+384 = ячейка, F+412 = 1, внешний vtbl+48 =
		//   `shopCommitPendingBuyItem@0xE30330` — вернуть позицию; в порту это `TakeFromCart` через `OnCartCellClicked`.
		UE_LOG(LogAsdaShop, Log, TEXT("[магазин] ПКМ по корзине продажи: ячейка %d — вернуть (эталон 0xF → vtbl+36 → +48)"), Cell);
		OnCartCellClicked(Cell);
		return;
	}
	const int32 Abs = Cell + kGridCols * Page;
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] ПКМ по витрине продажи: ячейка %d → индекс %d (эталон 0xF → vtbl+36 `npcShop_onItemSlotClick`)"), Cell, Abs);
	SellIntoCart(Abs);
}

void UAsdaNpcShopSubsystem::OnCartCellClicked(int32 Cell)
{
	if (!bOpen || bPending) { return; }
	if (Cell < 0 || Cell >= kCartCells || Cart[Cell].IsEmpty()) { return; }
	SelCartCell = Cell;
	SelMode = 1;
	PlaceCursorOnCell(CartSel, CartUi.IsValidIndex(Cell) ? CartUi[Cell].Icon.Get() : nullptr);
	TakeFromCart(Cell);
}

void UAsdaNpcShopSubsystem::OnConfirmClicked()
{
	if (!bOpen || bPending) { return; }
	const bool bBuy = (ShopType == 5 || ShopType == 7);
	if (bBuy) { SendBuy(); } else { SendSell(); }
}

// ═════════════════════════════ СЕТЬ ════════════════════════════════════════════════════════════

void UAsdaNpcShopSubsystem::SendBuy()
{
	// `Send_CharItemBuy_1@0xE2A240`: гейт «денег хватает», гейт «корзина не пуста», затем 5020.
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Net) { return; }

	bool bAny = false;
	for (int32 i = 0; i < kCartCells; ++i) { if (!Cart[i].IsEmpty()) { bAny = true; break; } }
	const int32 Total = TaxDelta + RunningTotal;
	if (Total >= (Inv ? Inv->GetGold() : 0))
	{
		NoticeMsgBox(ShopLoc::kNoMoney);
		return;
	}
	if (!bAny) { return; }

	TArray<int32> Codes, Qtys;
	TArray<FString> Lines;   // строки чата 20065, эталон готовит их ДО отправки
	for (int32 i = 0; i < kCartCells; ++i)
	{
		if (Cart[i].IsEmpty()) { continue; }
		int32 Qty = Cart[i].Amount;
		const FAsdaItemRecord* R = IT ? IT->GetItem(Cart[i].Code) : nullptr;
		// Эталон принудительно ставит 1 у самоцвета и у предмета с ненулевым `flagWord176`.
		if ((IT && IT->IsSowelCode(Cart[i].Code)) || (R && R->FlagWord176 != 0)) { Qty = 1; }
		Codes.Add(Cart[i].Code);
		Qtys.Add(Qty);
		// «Ты покупаешь [ss1] за [ss2] золота.» — имя предмета и итоговая цена позиции.
		if (UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			const int32 Price = ApplyWarPercent(PriceOf(Cart[i].Code, Cart[i].Amount, /*bBuy*/true), /*bAdd*/false);
			TArray<FString> Slots;
			Slots.Add(TS->GetItemName(Cart[i].Code));
			Slots.Add(AsdaUi::FormatThousands(Price));
			Lines.Add(TS->FormatById(ShopLoc::kBoughtLine, Slots, TArray<int32>()));
		}
	}
	PendingBuyLines = Lines;
	// Код первой позиции запоминаем ДО отправки: к моменту ответа корзина уже очищена,
	//   а звук сделки играется по предмету (см. `PlayTradeSound`).
	LastTradeItemCode = Codes.Num() > 0 ? Codes[0] : 0;
	// ★ ЗАЩЁЛКА «ЖДЁМ 5021» ВЗВОДИТСЯ ТОЛЬКО ПРИ РЕАЛЬНО УШЕДШЕМ ПАКЕТЕ (закон
	//   `no-state-latch-before-shown`; та же болезнь, что уже вылечена ниже у продажи 5018).
	//   БЫЛО: `bPending = true;` строкой ВЫШЕ отправки, результат `SendCharItemBuy5020` отброшен.
	//   Не ушедший пакет (нет соединения) вешал окно магазина навсегда: ответа 5021, который
	//   снимает защёлку (`OnBuyResult`), не будет, а `bPending` глушит и слоты (`:988`), и обе
	//   кнопки (`:1122`, `:1132`) до закрытия окна.
	//   ⚠ 5020 гейтом одержимости НЕ режется (предиката `sub_52EE40` в `CharItemBuy@0x50F170` нет,
	//   диспетчер `GameServer.c:19848`) — покупка у NPC одержимому РАЗРЕШЕНА; правка нужна ради
	//   мёртвого соединения и ради единообразия с продажей.
	bPending = Net->SendCharItemBuy5020(Codes, Qtys);
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] →5020: позиций %d, сумма %d -> %s"),
		Codes.Num(), Total, bPending ? TEXT("OK") : TEXT("НЕ УШЁЛ"));
}

void UAsdaNpcShopSubsystem::SendSell()
{
	// `Send_CharItemSell@0xE32800`: собирает 5 item-ref по 11 б ИЗ ЗАПИСЕЙ ИНВЕНТАРЯ.
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Net || !Inv) { return; }

	bool bAny = false;
	for (int32 i = 0; i < kCartCells; ++i) { if (!Cart[i].IsEmpty()) { bAny = true; break; } }
	if (!bAny) { return; }

	FAsdaPacketWriter Refs(11 * kCartCells);   // ★ U6: тело ссылок (было `TArray` + `SetNumZeroed`)
	int32 Packed = 0;
	for (int32 i = 0; i < kCartCells; ++i)
	{
		if (Cart[i].IsEmpty()) { continue; }
		// Д2 (см. объявление CVar в шапке файла): откуда читать поля `+7` и `+14`.
		const int32 RefCont = (CVarShopLegacySellRef.GetValueOnGameThread() != 0) ? 2 : Cart[i].Container;
		const FAsdaItemInstance* It = Inv->GetCell(RefCont, Cart[i].ArrayIndex);
		const int32 Second = It ? It->SecondIndex : 0;
		const int32 StackW = It ? It->StackWeight : 0;
		WriteSellRef11(Refs, 11 * Packed, Cart[i].SlotIndex, Second, Cart[i].Container, Cart[i].Amount, StackW);
		++Packed;
	}
	LastTradeItemCode = 0;
	for (int32 i = 0; i < kCartCells; ++i) { if (!Cart[i].IsEmpty()) { LastTradeItemCode = Cart[i].Code; break; } }
	// ★ ЗАЩЁЛКА «ЖДЁМ 5019» ВЗВОДИТСЯ ТОЛЬКО ПРИ РЕАЛЬНО УШЕДШЕМ ПАКЕТЕ (закон
	//   `no-state-latch-before-shown`). БЫЛО: `bPending = true` ставилось ДО отправки и без учёта
	//   её результата, поэтому не ушедший пакет (нет соединения, а с 2026-09-03 — ещё и гейт
	//   одержимости на 5018, `World/AsdaPossessGate.h`) вешал окно магазина: ответа 5019, который
	//   снимает защёлку, не будет никогда, а `bPending` глушит и слоты (`:988`), и обе кнопки
	//   (`:1122`, `:1132`) до закрытия окна.
	bPending = Net->SendCharItemSell5018(Refs.ToArray());
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] →5018: позиций %d, сумма %d -> %s"),
		Packed, RunningTotal, bPending ? TEXT("OK") : TEXT("НЕ УШЁЛ"));
}

// ★ U6 (В1 дедупа №2): запись через `FAsdaPacketWriter` (байты те же, что у прежнего ручного буфера).
void UAsdaNpcShopSubsystem::WriteSellRef11(FAsdaPacketWriter& W, int32 Off, int32 SlotIndex, int32 Second, int32 Container, int32 Amount, int32 StackW)
{
	W.I16(Off + 0, SlotIndex);
	W.I16(Off + 2, Second);
	W.U8(Off + 4, Container);
	W.I32(Off + 5, Amount);
	W.I16(Off + 9, StackW);
}

void UAsdaNpcShopSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок: во время разбора новые тела в очередь
	//   не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	if (TArray<TArray<uint8>> Local; PendingBuy.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnBuyResult(B); }
	}
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок: во время разбора новые тела в очередь
	//   не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	if (TArray<TArray<uint8>> Local; PendingSell.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnSellResult(B); }
	}
}

void UAsdaNpcShopSubsystem::OnBuyResult(const TArray<uint8>& Body406)
{
	bPending = false;
	if (Body406.Num() < 406) { return; }
	const int32 Code = (int32)Body406[0];
	UGameInstance* GI = GetGameInstance();
	if (Code != 1)
	{
		// `Recv_ShopSell_Result@0xE28FD0`: все отказы уходят однокнопочным окном.
		switch (Code)
		{
		case 0: NoticeMsgBox(ShopLoc::kNoItem);        break;
		case 2: NoticeMsgBox(ShopLoc::kBagFull);       break;
		case 3: NoticeMsgBox(ShopLoc::kBagFullPickup); break;
		case 4: NoticeMsgBox(ShopLoc::kNoGoldBuy);     break;
		case 5: NoticeMsgBox(ShopLoc::kNotSoldHere);   break;
		default: break;
		}
		PendingBuyLines.Reset();
		UE_LOG(LogAsdaShop, Log, TEXT("[магазин] 5021 отказ, код %d"), Code);
		return;
	}
	// Успех: записи в контейнеры, деньги и вес — ОДНОЙ точкой в инвентаре.
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ApplyShopBuyRecords(Body406);
	}
	// Корзина очищается, бегущие суммы обнуляются (эталон `memset(this + 272, 0, 0x64)`).
	for (int32 i = 0; i < kCartCells; ++i) { Cart[i].Clear(); }
	RunningTotal = 0; TaxDelta = 0;
	RefreshCart();
	RefreshMoneyTexts();
	RefreshTaxTexts();
	HideCursors();
	// Строки 20065, подготовленные при отправке, печатаются ИМЕННО ЗДЕСЬ (эталон, network.c:28238).
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		for (const FString& L : PendingBuyLines) { if (!L.IsEmpty()) { Msg->PostText(EAsdaMsgCategory::Trade, L); } }
	}
	PendingBuyLines.Reset();
	PlayTradeSound(LastTradeItemCode);   // ★ наш звук сделки (в оригинале его нет), см. PlayTradeSound
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] 5021 успех"));
}

// ★ ЗВУК СДЕЛКИ (требование владельца 2026-08-26). В КЛИЕНТЕ ОРИГИНАЛА ЕГО НЕТ: обработчики
//   результата покупки (`Recv_ShopSell_Result@0xE28FD0`) и продажи звуковых вызовов не содержат
//   вовсе — у магазина озвучены только открытие (5) и закрытие (6) окна.
//   Берём РОДНОЙ звук падения того же предмета — таблицу `Sod_drop_*`
//   (`CItemManager_InitDropSounds@0x9C4610`), которой уже озвучены наземный лут и наша корзина.
//   Общая точка — `UAsdaWorldSubsystem::PlayItemDropSound` (закон `port-shared-points`).
//   Откат: `asda.Shop.TradeSound 0`.
void UAsdaNpcShopSubsystem::PlayTradeSound(int32 ItemCode) const
{
	if (ItemCode <= 0 || CVarShopTradeSound.GetValueOnGameThread() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 Cat = IT ? IT->GetDropSoundCat(ItemCode) : INDEX_NONE;
	if ((Cat < 0 || Cat >= 12) && CVarShopTradeSoundFallback.GetValueOnGameThread() != 0)
	{
		if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2D(FName(TEXT("Sod_drop_Etc_0"))); }
		return;
	}
	if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		WS->PlayItemDropSound(ItemCode);
	}
}

void UAsdaNpcShopSubsystem::OnSellResult(const TArray<uint8>& Body62)
{
	bPending = false;
	if (Body62.Num() < 62) { return; }
	const int32 Code = (int32)Body62[0];
	UGameInstance* GI = GetGameInstance();
	if (Code != 1)
	{
		// `Recv_ShopBuy_Result@0xE31630`: оба отказа идут В ЧАТ (не окном).
		if (Code == 0)      { NoticeChat(ShopLoc::kSellFailed); }
		else if (Code == 2) { NoticeChat(ShopLoc::kFriendsOnly); }
		UE_LOG(LogAsdaShop, Log, TEXT("[магазин] 5019 отказ, код %d"), Code);
		return;
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ApplyShopSellRefs(Body62);
	}
	for (int32 i = 0; i < kCartCells; ++i) { Cart[i].Clear(); }
	RunningTotal = 0; TaxDelta = 0;
	// Эталон ПЕРЕСОБИРАЕТ список продажи из инвентаря — содержимое сумки изменилось.
	Items.Reset();
	GridDbl.Reset();   // [ПОРТ] ревизия 10.10 МЕЛК-2: под тем же номером ячейки теперь другой товар — пара щелчков рвётся
	if (ShopType == 10) { BuildSellListShellBag(); } else { BuildSellListSpendBag(); }
	RecalcPages();
	RefreshList();
	RefreshCart();
	RefreshMoneyTexts();
	RefreshTaxTexts();
	LayoutScrollBar();
	HideCursors();
	PlayTradeSound(LastTradeItemCode);   // ★ наш звук сделки (в оригинале его нет)
	UE_LOG(LogAsdaShop, Log, TEXT("[магазин] 5019 успех, список пересобран (%d позиций)"), Items.Num());
}

// ═════════════════════════════ НОТИСЫ ══════════════════════════════════════════════════════════

FString UAsdaNpcShopSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaNpcShopSubsystem::NoticeChat(int32 LocId) const
{
	// Эталон: `CChat_AddTokenizedLines(g_pChatWnd, текст, −11514, 10000, 0)` — системный чат.
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::Trade, LocId);
}

void UAsdaNpcShopSubsystem::NoticeMsgBox(int32 LocId) const
{
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBoxLoc` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Trade, LocId, 2288);
}

// ═════════════════════════════ ПОЛОСА ПРОКРУТКИ ════════════════════════════════════════════════
// Контрол 1365 `NPCSHOP_WHEELWIN` (скин `WheelWin_Nomal`, Size = "198 198") в WBP НЕ приехал —
//   импортёр читает только `.JUI`, а полосу движок создаёт из ключа СКИНА `ScrollBarFileNameVert`
//   (`uiControl_AttachVertScrollbar@0x12F3160`). Строим её сами общей точкой `FAsdaScrollBarNomal`.
//   ГЕОМЕТРИЯ БЕРЁТСЯ ПО ЖИВОЙ СЕТКЕ и совпадает с авторской: у ячейки 32 px и шага 33 px правый
//   край шестой колонки = ячейка0.x + 5·33 + 32 + 1 = +198 (ширина колёсного окна), а высота
//   шести рядов = 5·33 + 32 = 197 ≈ 198.
void UAsdaNpcShopSubsystem::BuildScrollBar()
{
	if (ScrollBar.IsBuilt() || !ShopWidget) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(ShopWidget->GetRootWidget());
	if (!Root) { return; }
	ScrollBar.Build(ShopWidget, Root, TEXT("NPCSHOP_WHEELWIN_Scr"), /*ZTrack*/20);
}

bool UAsdaNpcShopSubsystem::IsScrollBarShown() const
{
	// `NPCShop_OpenWindow`: полоса скрыта, когда страниц ≤ 6.
	return bOpen && bControlsReady && ScrollBar.IsBuilt() && TotalPages > kGridRows;
}

void UAsdaNpcShopSubsystem::LayoutScrollBar()
{
	if (!ScrollBar.IsBuilt()) { return; }
	if (!IsScrollBarShown()) { ScrollBar.Hide(); return; }
	const UWidget* Cell0 = GridUi.IsValidIndex(0) ? GridUi[0].Icon.Get() : nullptr;
	if (!Cell0) { return; }
	const FVector2D Base = AsdaUi::CanvasRootPos(Cell0);
	float Step = (float)kCellStep;
	if (GridUi.IsValidIndex(1) && GridUi[1].Icon)
	{
		const float D = (float)(AsdaUi::CanvasRootPos(GridUi[1].Icon) - Base).X;
		if (D > 1.f) { Step = D; }
	}
	const float S = Step / (float)kCellStep;
	const float BarX = (kCellStep * (kGridCols - 1) + kCellPx + 1) * S;   // = 198 при масштабе 1
	const float BarH = (kCellStep * (kGridRows - 1) + kCellPx) * S;       // = 197 при масштабе 1
	const int32 MaxRow = GetMaxPage();
	// ★ Волна 14 (МЕЛК-1/2): целые аргументы эталона `uiControlSetScrollRange(6, страниц)` (npc_mob_pet.c:6170),
	//   длина бегунка — в пикселях `.JUI`, затем ×S.
	ScrollBar.Layout(Base + FVector2D(BarX, 0.f), BarH, S, kGridRows, TotalPages, MaxRow,
		FMath::Clamp(Page, 0, FMath::Max(0, MaxRow)));
}

void UAsdaNpcShopSubsystem::TickScrollBar(bool bLmb, bool bLmbDownEdge)
{
	if (!IsScrollBarShown())
	{
		ScrollBar.bThumbGrab = false;
		ScrollBar.PressedPlane = -1;
		return;
	}
	int32 Value = 0;
	switch (ScrollBar.Tick(bLmb, bLmbDownEdge, GetMaxPage(), Value))
	{
	case EAsdaScrollAction::ScrollBy: ScrollBy(Value);  break;
	case EAsdaScrollAction::SetRow:   SetPage(Value);   break;
	default: break;
	}
}

// ═════════════════════════════ ПОДСКАЗКИ ═══════════════════════════════════════════════════════
// Эталон: событие 8 (курсор вошёл в ячейку) → `npcShopRefreshSlotByMsgId@0xE244F0` → vtbl+40.
//   У ВИТРИНЫ это `itemTooltip_setStateFromRecord@0xE285B0` → `itemTooltipUpdateOnHover@0xEAC430`
//   → `Item_BuildTooltipText_EAC520@0xEAC520` (билдер «по коду и количеству»); у ПРОДАЖИ —
//   `inventory_FindItemAndShowTooltip@0xE307F0`, который находит НАСТОЯЩУЮ 57-байтовую запись в
//   контейнере и строит ПОЛНЫЙ тултип `ItemTooltip_Build`.
//   ⚠ Билдер `0xEAC520` в порт ещё не перенесён (в подсистеме тултипа его нет) — для витрины
//   временно зовём `ShowItemTooltipByCode`; точка записана в `PORT_PENDING_ON_SYSTEMS.md`.
void UAsdaNpcShopSubsystem::TickTooltip()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Tip) { return; }

	int32 HoverCell = -1;
	bool  bHoverCart = false;
	for (int32 C = 0; C < kCartCells; ++C)
	{
		if (CartUi.IsValidIndex(C) && CartUi[C].Icon && AsdaUi::IsCursorOverWidget(CartUi[C].Icon))
		{
			if (!Cart[C].IsEmpty()) { HoverCell = C; bHoverCart = true; }
			break;
		}
	}
	if (HoverCell < 0)
	{
		for (int32 C = 0; C < kGridCells; ++C)
		{
			if (GridUi.IsValidIndex(C) && GridUi[C].Icon && AsdaUi::IsCursorOverWidget(GridUi[C].Icon))
			{
				const int32 Abs = C + kGridCols * Page;
				if (Items.IsValidIndex(Abs) && !Items[Abs].IsEmpty()) { HoverCell = C; }
				break;
			}
		}
	}
	if (HoverCell < 0)
	{
		if (TipCell >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::NpcShop); TipCell = -1; }
		return;
	}
	if (HoverCell == TipCell && bHoverCart == bTipFromCart) { return; }
	TipCell = HoverCell;
	bTipFromCart = bHoverCart;

	const FAsdaShopEntry& E = bHoverCart ? Cart[HoverCell] : Items[HoverCell + kGridCols * Page];
	const bool bBuy = (ShopType == 5 || ShopType == 7);
	if (!bBuy && Inv)
	{
		if (const FAsdaItemInstance* It = Inv->GetCell(E.Container, E.ArrayIndex))
		{
			Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::NpcShop, E.Container);
			return;
		}
	}
	// ВИТРИНА ПОКУПКИ: эталонный `Item_BuildTooltipText_EAC520@0xEAC520` (isBuyMode = 0,
	//   showCount = 1, itemCount = потолок стопки из пятого поля записи витрины).
	//   В режимах ПРОДАЖИ сюда попадают только позиции корзины и записи без слота — там же
	//   эталон показывает цену ПРОДАЖИ (`isBuyMode = 1`).
	Tip->ShowShopSlotTooltip(E.Code, E.Amount, /*bSellMode*/ !bBuy, EAsdaTipOwner::NpcShop);
}

// ═════════════════════════════ ПЕРЕТАСКИВАНИЕ ══════════════════════════════════════════════════
// ЭТАЛОН. Сообщение 0xB (`npcShopMapClickToItem@0xE24970`) на НАЖАТИЕ ЛКМ запоминает слот и режим
//   (F+384/388 витрина, F+392 корзина, F+420 = 0/1) и возвращает 1 — это «схватили». Сообщение 0xD
//   на ОТПУСКАНИЕ идёт в `dispatchByState44or48@0xE24AE0`: режим 0 → vtbl+44 (положить в корзину),
//   режим 1 → vtbl+48 (вернуть из корзины). Летящую иконку рисует движок (`CPlane_SetFillMode(…,5)`
//   у схваченной ячейки, `uiControlApplySelectedHighlight@0xE24CA0`).
// ПОРТ. Призрак — ОБЩАЯ точка `FAsdaDragGhost`; порог захвата — общая `FAsdaDragArm` (осознанное
//   отступление порта, у оригинала порога нет: `CControl_OnLButtonDown@0x12DB870` начинает захват
//   тем же кадром). Пока порог не пройден, отпускание = ОБЫЧНЫЙ клик: с 10.10.2026 одиночный только
//   выбирает (эталонный 0xB), перенос — двойным (0xD) либо броском (7).
void UAsdaNpcShopSubsystem::TickDrag()
{
	if (!bControlsReady || !ShopWidget) { return; }

	// ── ★★ [ПОРТ] Ревизия shopdbl2 Р-1 (10.10.2026): ОКНО ВЫШЕ ПЕРЕХВАТЫВАЕТ МЫШЬ. Ячейки ищутся голым прямоугольником
	//   (`IsCursorOverWidget`) без порядка окон, поэтому щелчок по сумке, лежащей поверх сетки, доставался ещё и магазину:
	//   ПКМ клал товар под сумкой в корзину продажи, двойной ЛКМ — тоже. У эталона событие получает только верхнее окно
	//   (`CControl_OnEvent15@0x12DC530` — ПКМ «нажал и отпустил на ОДНОМ контроле»). Гейт — тот же, что у лавки
	//   (`UAsdaSelfStoreSubsystem::TickShopMouse`) и премиума (`TickCards`). При блокировке нажатие не запоминается
	//   (ни захват ЛКМ, ни ячейка ПКМ), пара двойного щелчка рвётся; отпускание уже взведённого захвата ЛКМ дорабатывает.
	UAsdaWindowSubsystem* BlockWM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;   // менеджер окон (порядок z)
	const bool bBlocked = BlockWM && (BlockWM->IsPointerBlockedByTopmost(kShopWindowId) || BlockWM->IsCursorOverHigherWindow(kShopWindowId));   // курсор над окном выше магазина / модалкой

	// ── ★ ПКМ (10.10.2026, решение владельца): эталонное событие 0xF окна 1300 — ЩЕЛЧОК ПКМ (нажатие и отпускание над
	//   одной ячейкой, `case 0xF` ui_core.c:71366 → vtbl+36). Ячейка — по живой геометрии, как у ЛКМ. Ключ
	//   `asda.Shop.DblClickToCart` сюда не входит: одиночный ПКМ — как в оригинале.
	auto CellUnderCursor = [this](bool& bOutCart) -> int32
	{
		for (int32 C = 0; C < kCartCells; ++C)
		{
			if (CartUi.IsValidIndex(C) && CartUi[C].Icon && AsdaUi::IsCursorOverWidget(CartUi[C].Icon) && !Cart[C].IsEmpty()) { bOutCart = true; return C; }
		}
		for (int32 C = 0; C < kGridCells; ++C)
		{
			const int32 Abs = C + kGridCols * Page;
			if (GridUi.IsValidIndex(C) && GridUi[C].Icon && AsdaUi::IsCursorOverWidget(GridUi[C].Icon)
				&& Items.IsValidIndex(Abs) && !Items[Abs].IsEmpty()) { bOutCart = false; return C; }
		}
		return -1;
	};
	if (Mouse.bRmbDown)
	{
		GridDbl.Reset();   // нажатие другой кнопкой рвёт пару двойного ЛКМ (как у Windows)
		bRmbCart = false;
		RmbCell = (DragGhost.IsActive() || bBlocked) ? -1 : CellUnderCursor(bRmbCart);   // Р-1: нажатие по окну выше — не наше
	}
	if (Mouse.bRmbUp)
	{
		bool bCartNow = false;
		// Р-1: отпускание над окном выше — уже другой контрол, щелчка ПКМ по ячейке нет (эталон: нажатие и отпускание на одном).
		const int32 Now = (RmbCell >= 0 && !bBlocked) ? CellUnderCursor(bCartNow) : -1;
		const int32 Was = RmbCell;
		RmbCell = -1;
		if (Was >= 0 && Now == Was && bCartNow == bRmbCart) { OnSellCellRightClick(Was, bRmbCart); }
	}

	// ── Нажатие: взводим захват над непустой ячейкой ──
	if (Mouse.bLmbDown && !DragGhost.IsActive() && !DragArm.IsArmed())
	{
		// Р-1: нажатие ЛКМ по окну выше — «мимо ячеек»: захват не взводим, пару двойного рвём.
		if (bBlocked) { GridDbl.Reset(); return; }
		for (int32 C = 0; C < kCartCells; ++C)
		{
			if (CartUi.IsValidIndex(C) && CartUi[C].Icon && AsdaUi::IsCursorOverWidget(CartUi[C].Icon)
				&& !Cart[C].IsEmpty())
			{
				DragArm.Arm(/*KeyA*/1, /*KeyB*/C, Mouse.Cursor);
				return;
			}
		}
		for (int32 C = 0; C < kGridCells; ++C)
		{
			const int32 Abs = C + kGridCols * Page;
			if (GridUi.IsValidIndex(C) && GridUi[C].Icon && AsdaUi::IsCursorOverWidget(GridUi[C].Icon)
				&& Items.IsValidIndex(Abs) && !Items[Abs].IsEmpty())
			{
				DragArm.Arm(/*KeyA*/0, /*KeyB*/C, Mouse.Cursor);
				return;
			}
		}
		// ★ [ПОРТ] Ревизия 10.10.2026, МЕЛК-1: нажатие мимо непустых ячеек (фон окна, пустая ячейка, полоса прокрутки)
		//   рвёт пару двойного щелчка — как у Windows, где второй щелчок пары обязан лечь в то же место. Прежде
		//   «ячейка 16 → мимо → ячейка 16» за 0,3 с клало товар. Контракт — в шапке `FAsdaShopDblClick::Reset`.
		GridDbl.Reset();
		return;
	}

	// ── Порог пройден: поднимаем призрак ──
	if (DragArm.IsArmed() && Mouse.bLmb && DragArm.ShouldStart(Mouse.Cursor))
	{
		DragArm.MarkStarted();
		BeginDrag(DragArm.KeyB, DragArm.KeyA == 1);
	}

	// ── Кадр перетаскивания ──
	if (DragGhost.IsActive())
	{
		DragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	}

	// ── Отпускание ──
	if (Mouse.bLmbUp)
	{
		if (DragGhost.IsActive()) { DropDrag(); }
		else if (DragArm.IsArmed())
		{
			// ★★★ КЛИК ПО ЯЧЕЙКЕ — ЗДЕСЬ, А НЕ ЧЕРЕЗ КНОПКИ-ОВЕРЛЕИ.
			//   ДЕФЕКТ (2026-08-26, три захода мимо): клик-зоны сетки строятся как прозрачные кнопки
			//   поверх иконок, и их положение считается по слоту. Но в `.JUI` ячейки заданы ЦЕПОЧКОЙ
			//   ВЛОЖЕННОСТИ (`TRAY01` внутри `TRAY00`, `TRAY02` внутри `TRAY01`, …), поэтому у части
			//   ячеек зона уезжала на чужую панель: клик по первым ячейкам витрины попадал в корзину.
			//   ПЕРЕТАСКИВАНИЕ при этом работало всегда — потому что оно ищет ячейку по ЖИВОЙ
			//   ГЕОМЕТРИИ (`IsCursorOverWidget`), а не по вычисленным координатам.
			//   Поэтому клик переведён на ТОТ ЖЕ способ: захват уже взведён над нужной ячейкой
			//   (`DragArm.KeyA` = откуда, `KeyB` = номер), порог перетаскивания не пройден ⇒ это клик.
			const int32 Cell = DragArm.KeyB;
			const bool bFromCart = (DragArm.KeyA == 1);
			DragArm.Disarm();
			// ★★ ТРЕБОВАНИЕ ВЛАДЕЛЬЦА 10.10.2026 «в корзину — ДВОЙНЫМ щелчком» = ЭТАЛОН 1:1. Оконная процедура
			//   `uiHandleWindowCommand@0xE24DE0` (ui_core.c:71293): 0xB (нажатие ЛКМ, :71359-71361) — ТОЛЬКО выбор
			//   (`npcShopMapClickToItem`, курсор, режим F+420); 0xD (двойной ЛКМ, :71363-71365) —
			//   `dispatchByState44or48@ui_core.c:71228`: витрина → vtbl+44 (в корзину), корзина → vtbl+48 (вернуть).
			//   Прежний порт клал/возвращал ОДИНОЧНЫМ щелчком — это был дефект порта, не оригинал.
			//   Пара щелчков засчитывается по тому же узлу в пределах `GetDoubleClickTime`, ровно один раз.
			//   Бросок перетаскиванием (событие 7, ветка `DropDrag`) кладёт/возвращает сразу, как в эталоне.
			//   Ключ отката (к прежнему поведению порта): `asda.Shop.DblClickToCart 0`.
			if (bFromCart)
			{
				if (GridDbl.Click(Cell, /*корзина*/ 1)) { OnCartCellClicked(Cell); }
				else                                    { OnCartCellSelected(Cell); }
			}
			else if (GridDbl.Click(Cell + kGridCols * Page, /*витрина*/ 0)) { OnGridCellClicked(Cell); }
			else                                                           { OnGridCellSelected(Cell); }
			return;
		}
		DragArm.Disarm();
	}
}

void UAsdaNpcShopSubsystem::BeginDrag(int32 Cell, bool bFromCart)
{
	DragCell = Cell;
	bDragFromCart = bFromCart;
	GridDbl.Reset();   // [ПОРТ] ревизия 10.10 МЕЛК-1: перетаскивание — не щелчок, пару двойного рвёт
	// Эталонный режим F+420 ставится ровно здесь (сообщение 0xB).
	SelMode = bFromCart ? 1 : 0;
	if (bFromCart) { SelCartCell = Cell; }
	else { SelGridCell = Cell; SelAbsIndex = Cell + kGridCols * Page; }

	const TArray<FAsdaShopCellUi>& Ui = bFromCart ? CartUi : GridUi;
	if (!Ui.IsValidIndex(Cell) || !Ui[Cell].Icon || !ShopWidget->WidgetTree) { return; }
	if (UCanvasPanel* Root = Cast<UCanvasPanel>(ShopWidget->WidgetTree->RootWidget))
	{
		DragGhost.Begin(ShopWidget, Root, Ui[Cell].Icon->GetBrush(),
			FVector2D((float)kCellPx, (float)kCellPx), /*Opacity*/ 0.75f,
			FVector2D(-16.f, -16.f), TEXT("NPCSHOP_DragGhost"),
			GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	}
	// Подсказку на время перетаскивания гасим — как эталон (`CControl_KillFocus(&g_ItemTooltip)`).
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::NpcShop);
	}
	TipCell = -1;
}

void UAsdaNpcShopSubsystem::DropDrag()
{
	DragGhost.Cancel();
	const int32 Cell = DragCell;
	const bool bFromCart = bDragFromCart;
	DragCell = -1;
	if (Cell < 0) { return; }

	// Куда отпустили. Эталон различает только «над корзиной» и «над витриной» — сами ячейки-приёмники
	//   у него не адресуются: `npcShop_BuyItemIntoSlot` сам ищет свободную ячейку корзины
	//   (`slotFindFirstFree5@0xE24630`), а `npcShopClearTradeSlot` возвращает предмет в свою строку.
	bool bOverCart = false, bOverGrid = false;
	for (int32 C = 0; C < kCartCells && !bOverCart; ++C)
	{
		if (CartUi.IsValidIndex(C) && CartUi[C].Icon && AsdaUi::IsCursorOverWidget(CartUi[C].Icon)) { bOverCart = true; }
	}
	for (int32 C = 0; C < kGridCells && !bOverGrid; ++C)
	{
		if (GridUi.IsValidIndex(C) && GridUi[C].Icon && AsdaUi::IsCursorOverWidget(GridUi[C].Icon)) { bOverGrid = true; }
	}

	if (!bFromCart && bOverCart)
	{
		OnGridCellClicked(Cell);          // vtbl+44: положить в корзину (с нумпадом, если стекуется)
	}
	else if (bFromCart && bOverGrid)
	{
		OnCartCellClicked(Cell);          // vtbl+48: вернуть из корзины
	}
	else
	{
		// Отпустили мимо — эталон ничего не делает, только снимает подсветку схваченной ячейки.
		HideCursors();
	}
}

// ═════════════════════════════ ДИАГНОСТИКА ═════════════════════════════════════════════════════

FString UAsdaNpcShopSubsystem::DumpControls() const
{
	FString S = TEXT("=== NPCSHOP_WIN (1300) ===\n");
	S += FString::Printf(TEXT("виджет: %s, контролы: %s\n"),
		ShopWidget ? TEXT("есть") : TEXT("НЕТ"), bControlsReady ? TEXT("готовы") : TEXT("нет"));
	for (int32 C = 0; C < kGridCells; ++C)
	{
		S += FString::Printf(TEXT("  TRAY%02d idnum=%d икона=%s счётчик=%s\n"), C, 1320 + C,
			GridUi.IsValidIndex(C) && GridUi[C].Icon ? *GridUi[C].Icon->GetName() : TEXT("НЕТ"),
			GridUi.IsValidIndex(C) && GridUi[C].CountText ? *GridUi[C].CountText->GetName() : TEXT("НЕТ"));
	}
	// ★ Координаты первых ячеек витрины и всех ячеек корзины — для поиска перекрытий клик-зон
	//   (жалоба 2026-08-26: клик по витрине срабатывал как клик по корзине).
	S += TEXT("  --- положение в корне окна (перекрытия клик-зон) ---\n");
	for (int32 C = 0; C < FMath::Min(6, kGridCells); ++C)
	{
		if (!GridUi.IsValidIndex(C) || !GridUi[C].Icon) { continue; }
		UImage* Ic = GridUi[C].Icon.Get();
		const FVector2D P = AsdaUi::CanvasRootPos(Ic);
		const UCanvasPanelSlot* IS = AsdaUi::SlotOf(Ic);
		S += FString::Printf(TEXT("  TRAY%02d «%s» родитель «%s» локально (%.0f,%.0f) в корне (%.0f,%.0f)\n"),
			C, *Ic->GetName(), Ic->GetParent() ? *Ic->GetParent()->GetName() : TEXT("?"),
			IS ? IS->GetPosition().X : -1.f, IS ? IS->GetPosition().Y : -1.f, P.X, P.Y);
	}
	for (int32 C = 0; C < kCartCells; ++C)
	{
		if (!CartUi.IsValidIndex(C) || !CartUi[C].Icon) { continue; }
		UImage* Ic = CartUi[C].Icon.Get();
		const FVector2D P = AsdaUi::CanvasRootPos(Ic);
		const UCanvasPanelSlot* IS = AsdaUi::SlotOf(Ic);
		S += FString::Printf(TEXT("  BTRAY%02d «%s» родитель «%s» локально (%.0f,%.0f) в корне (%.0f,%.0f)\n"),
			C, *Ic->GetName(), Ic->GetParent() ? *Ic->GetParent()->GetName() : TEXT("?"),
			IS ? IS->GetPosition().X : -1.f, IS ? IS->GetPosition().Y : -1.f, P.X, P.Y);
	}

	for (int32 C = 0; C < kCartCells; ++C)
	{
		S += FString::Printf(TEXT("  BTRAY%02d idnum=%d икона=%s счётчик=%s\n"), C, 1360 + C,
			CartUi.IsValidIndex(C) && CartUi[C].Icon ? *CartUi[C].Icon->GetName() : TEXT("НЕТ"),
			CartUi.IsValidIndex(C) && CartUi[C].CountText ? *CartUi[C].CountText->GetName() : TEXT("НЕТ"));
	}
	S += FString::Printf(TEXT("  1311 BUYBUT=%s · 1312 SELLBUT=%s · 1313 EXITBUT=%s · 1302 CLOSE=%s\n"),
		BtnBuy ? TEXT("да") : TEXT("НЕТ"), BtnSell ? TEXT("да") : TEXT("НЕТ"),
		BtnExit ? TEXT("да") : TEXT("НЕТ"), BtnClose ? TEXT("да") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  1301/1303/1370 заголовки=%s/%s/%s · 1314/1315 курсоры=%s/%s\n"),
		GrpSowelTitle ? TEXT("да") : TEXT("НЕТ"), GrpMiscTitle ? TEXT("да") : TEXT("НЕТ"),
		GrpWeaponTitle ? TEXT("да") : TEXT("НЕТ"),
		GridSel ? TEXT("да") : TEXT("НЕТ"), CartSel ? TEXT("да") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  1305/1306/1307/1308=%s/%s/%s/%s · 1309/1310=%s/%s · 1380..1383=%s/%s/%s/%s\n"),
		TxtList00 ? TEXT("да") : TEXT("НЕТ"), TxtList02 ? TEXT("да") : TEXT("НЕТ"),
		TxtList04 ? TEXT("да") : TEXT("НЕТ"), TxtList06 ? TEXT("да") : TEXT("НЕТ"),
		TxtTotal ? TEXT("да") : TEXT("НЕТ"), TxtMoney ? TEXT("да") : TEXT("НЕТ"),
		TxtTaxPct ? TEXT("да") : TEXT("НЕТ"), TxtTaxVal ? TEXT("да") : TEXT("НЕТ"),
		TxtNetLbl ? TEXT("да") : TEXT("НЕТ"), TxtNetVal ? TEXT("да") : TEXT("НЕТ"));
	if (GridClicks) { S += GridClicks->DumpHitZones(TEXT("витрина")); }
	if (CartClicks) { S += CartClicks->DumpHitZones(TEXT("корзина")); }
	S += ScrollBar.Dump();
	return S;
}

FString UAsdaNpcShopSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("=== Магазин NPC ===\nоткрыт: %s · тип %d · npcId %d · ждём ответ: %s\n")
		TEXT("товаров %d, страниц %d (страница %d из %d), сумма %d, налог %d\n"),
		bOpen ? TEXT("да") : TEXT("нет"), ShopType, NpcId, bPending ? TEXT("да") : TEXT("нет"),
		Items.Num(), TotalPages, Page, GetMaxPage(), RunningTotal, TaxDelta);
	for (int32 i = 0; i < kCartCells; ++i)
	{
		S += FString::Printf(TEXT("  корзина[%d]: код %d · слот %d · контейнер %d · кол-во %d\n"),
			i, Cart[i].Code, Cart[i].SlotIndex, Cart[i].Container, Cart[i].Amount);
	}
	for (int32 i = 0; i < FMath::Min(Items.Num(), 12); ++i)
	{
		S += FString::Printf(TEXT("  товар[%d]: код %d · слот %d · контейнер %d · кол-во %d\n"),
			i, Items[i].Code, Items[i].SlotIndex, Items[i].Container, Items[i].Amount);
	}
	if (Items.Num() > 12) { S += FString::Printf(TEXT("  … ещё %d\n"), Items.Num() - 12); }
	return S;
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без сервера) ═════════════════════════
namespace
{
	UAsdaNpcShopSubsystem* ShopSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaNpcShopSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShopOpen(
	TEXT("asda.Shop.Open"),
	TEXT("asda.Shop.Open <тип 5|6|7|8|10> <npcId> — открыть окно магазина NPC так же, как это "
	     "делают коды диалога 30001/30002/30003/30004/30026"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaNpcShopSubsystem* S = ShopSub(W);
		if (!S) { UE_LOG(LogAsdaShop, Warning, TEXT("подсистема недоступна")); return; }
		const int32 Type = A.Num() > 0 ? FCString::Atoi(*A[0]) : 5;
		const int32 Npc  = A.Num() > 1 ? FCString::Atoi(*A[1]) : 3;
		S->OpenShop(Type, Npc);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShopClose(
	TEXT("asda.Shop.Close"),
	TEXT("asda.Shop.Close — закрыть окно магазина (эталонный npcShop_Close)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaNpcShopSubsystem* S = ShopSub(W)) { S->CloseShop(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShopDump(
	TEXT("asda.Shop.Dump"),
	TEXT("asda.Shop.Dump — дамп КАЖДОГО контрола окна 1300 + список товаров, корзина и суммы"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaNpcShopSubsystem* S = ShopSub(W);
		if (!S) { UE_LOG(LogAsdaShop, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaShop, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShopClick(
	TEXT("asda.Shop.Click"),
	TEXT("asda.Shop.Click <ячейка 0..35> [cart] — прогнать клик ТЕМ ЖЕ путём, что и мышь "
	     "(в headless-PIE мышью не кликнуть, а второй путь обработки заводить нельзя)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaNpcShopSubsystem* S = ShopSub(W);
		if (!S) { UE_LOG(LogAsdaShop, Warning, TEXT("подсистема недоступна")); return; }
		const int32 Cell = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		const bool bCart = A.Num() > 1 && A[1].Equals(TEXT("cart"), ESearchCase::IgnoreCase);
		if (bCart) { S->OnCartCellClicked(Cell); } else { S->OnGridCellClicked(Cell); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShopConfirm(
	TEXT("asda.Shop.Confirm"),
	TEXT("asda.Shop.Confirm — нажать BUYBUT (1311) в режиме покупки либо SELLBUT (1312) в режиме продажи"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaNpcShopSubsystem* S = ShopSub(W)) { S->OnConfirmClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShopPage(
	TEXT("asda.Shop.Page"),
	TEXT("asda.Shop.Page <страница> — встать на страницу витрины (страница = ряд из 6 ячеек)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaNpcShopSubsystem* S = ShopSub(W)) { S->SetPage(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GShopSellBuff(
	TEXT("asda.Shop.SellBuff"),
	TEXT("asda.Shop.SellBuff <процент|-1> — включить/снять эталонный бафф «ставка цены» (тип 38) ")
	TEXT("на цену ПРОДАЖИ. В самом оригинале эта ветка недостижима (указатель g_pActiveBuffEntry ")
	TEXT("никогда не присваивается — байт-скан .text), команда нужна для проверки формулы"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaNpcShopSubsystem* S = ShopSub(W);
		if (!S) { UE_LOG(LogAsdaShop, Warning, TEXT("подсистема недоступна")); return; }
		S->SetSellPriceBuffPercent(A.Num() > 0 ? FCString::Atoi(*A[0]) : -1);
		UE_LOG(LogAsdaShop, Log, TEXT("[магазин] бафф цены продажи: %d%%"), S->GetSellPriceBuffPercent());
	}));
#endif // ASDA_DEV_CONSOLE

// ── 10.10.2026: доступ самотеста двойного щелчка (`asda.Shop.SelfTestDblClick`) ──
int32 UAsdaNpcShopSubsystem::DebugCartFilled() const
{
	int32 N = 0;
	for (int32 i = 0; i < kCartCells; ++i) { if (!Cart[i].IsEmpty()) { ++N; } }
	return N;
}

int32 UAsdaNpcShopSubsystem::DebugItemCode(int32 AbsIndex) const
{
	return Items.IsValidIndex(AbsIndex) ? Items[AbsIndex].Code : 0;
}

UWidget* UAsdaNpcShopSubsystem::DebugGridIcon(int32 Cell) const
{
	return GridUi.IsValidIndex(Cell) ? GridUi[Cell].Icon.Get() : nullptr;
}

UWidget* UAsdaNpcShopSubsystem::DebugCartIcon(int32 Cell) const
{
	return CartUi.IsValidIndex(Cell) ? CartUi[Cell].Icon.Get() : nullptr;
}
