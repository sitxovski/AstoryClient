// Copyright (c) 2026 @sitxovski (Discord)
// Прямой обмен между игроками — ОКНО `TRAN_WIN` 2008 (волна О2 торговли, 29.09.2026). [ПОРТ]
//   Назначение, границы волны, исправленные дефекты — в шапке `Public/UI/AsdaExchangeP2PSubsystem.h`.
//   Разборы: `Reports/trade/A_exchange_client.md` §2.2, §6; дерево окна — `Reports/trade/w0/`; итог — `wave_O2.md`.
//   Эталон: `exchangeWnd_OnMessage@0xC6F400` и его ветки (shop_trade.c:6283-7700, item_inventory.c:23174-24200,
//   ui_core.c:35032-35510, math_geom.c:1772-1880, item_enhance.c:133-400, quest.c:4915), `Recv_Exchange_SlotUpdate`
//   (network.c:14378-14510), 5288/5290 (shop_trade.c:8646-9781).
//   ★ Волна О3 (30.09.2026, итог — `wave_O3.md`): готовность 2048 → 5291 (`exchangeOnLockToggleMsg@0xC78940`,
//   `Exchange_SendLockToggle@0xC81990`), приём 5292 (`Recv_ExchangeLockState@0xC85000`, network.c:14533-14583), итог 2011 → 5293
//   (LABEL_72 shop_trade.c:6452, `Exchange_SendComplete@0xC82A80`); исправления по ревизии О2 (Д18, Д20, МЕЛК-1/4/5/6/7/8).
//   ★ Волна О4 (30.09.2026, итог — `wave_O4.md`): Д21 — откат готовности после отказа фиксации (`RollbackRefusedLock`); по ревизии О3 —
//   сверка 5289 с эхом (Д18), откат только своего инкремента счётчика. Итог 5294/5295 — `AsdaExchangeP2PComplete.cpp`.
//
// ★ МОДЕЛЬ — СЫРЫЕ ЗАПИСИ ЭТАЛОНА. Списки-источники, лоток и копии лотка — те же массивы 57/64/51 б, что у `CExchange`
//   (смещения в заголовке). Предмет, положенный в лоток, ИСЧЕЗАЕТ из списка локально (как у эталона), а инвентарь не
//   меняется до итога 5294/5295 (О4): списки — это КОПИИ ячеек контейнеров, снятые при открытии окна.
#include "UI/AsdaExchangeP2PSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaShopDblClick.h"   // ★ U1 (В2-A дедупа №2): FAsdaDblClick::WindowSec — общий порог Windows
#include "UI/AsdaTradeStateSubsystem.h"     // +0x2C «окно 2008 открыто»
#include "UI/AsdaJuiWindowHandle.h"         // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaWindowSubsystem.h"         // окна, колесо, нумпад
#include "UI/AsdaNotice.h"                  // цветная строка чата, loc
#include "UI/AsdaIconCellGrid.h"            // `ApplyItemIcon` — иконка предмета в один UImage (общая точка)
#include "UI/AsdaItemTooltipSubsystem.h"    // подсказки предметов
#include "UI/AsdaTipPanel.h"                // EAsdaTipOwner::ExchangeP2P
#include "UI/AsdaInventorySubsystem.h"      // ячейки контейнеров 1/2 (`inventoryGetSlotRecord`)
#include "UI/AsdaAnimatedPlane.h"           // шестерня 2059 и рамка 2047 (кадровые планы)
#include "Items/AsdaItemTableSubsystem.h"   // item_list / special_itemlist / sowell_list
#include "Items/AsdaItemDbRows.h"
// «мёртв» ([ПОРТ] закрытие при смерти)   ★ R3: комментарий снятого включения "World/AsdaPlayerController.h" (файл им не пользовался) — сохранён
#include "World/AsdaWorldSubsystem.h"       // уровень выбранного персонажа (фильтр типа 1)
#include "Login/AsdaButtonRelay.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"                       // мой уровень
#include "AsdaBytes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "UI/AsdaPlayerMenuSubsystem.h"     // `g_nPickedUserIdx` — последний ПКМ-пик игрока (фильтр типа 1, О3)
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"             // `GetDoubleClickTime` — окно WM_LBUTTONDBLCLK эталона (0xD)
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP2PWin, Log, All);

namespace
{
	const FName  kTranId(TEXT("TRAN_WIN"));   // id окна 2008 в реестре окон (он же id зоны)
	const TCHAR* kTranWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_TRAN_WIN.WBP_TRAN_WIN_C");   // ассет окна

	// [ОРИГИНАЛ] Цвета строк чата и надписей по обработчику.
	const FColor kColDefault(0xFE, 0xD9, 0xF1, 0xFF);   // −75279 = 0xFFFED9F1 — почти все нотисы окна
	const FColor kColStack(0xFF, 0xD3, 0x06, 0xFF);     // −11514 = 0xFFFFD306 — 5066 в ветке стака типа 0
	const FColor kColLock(0xFF, 0xD3, 0x06, 0xFF);      // −11514 = 0xFFFFD306 — 5071/5073 отказа фиксации (`Recv_ExchangeLockState`)
	const FColor kTxtWhite(0xFF, 0xFF, 0xFF, 0xFF);     // −1 — `CControl_SetField923(…, −1)` количество
	const FColor kTxtYellow(0xFF, 0xFF, 0x00, 0xFF);    // −256 = 0xFFFFFF00 — «+N%»

	constexpr int32 kGoldCode    = 20551;   // золото (`ItemRecord->itemCode == 20551`)
	constexpr int32 kLevelTrade  = 24;      // `level >= 24` / `level > 23` фильтров
	// [ПОРТ] Заливка 5 «рисуется у курсора»: исходная ячейка приглушается до 40 % (значение согласовано с юзером и одно
	//   у всех окон с призраком — `UAsdaHotbarSubsystem::DragSourceOpacity`), призрак — тоже 40 %.
	constexpr float kDragAlpha   = 0.4f;

	// ★ Ревизия О2 (МЕЛК-6): 0xD эталона — WM_LBUTTONDBLCLK, то есть ВТОРОЕ НАЖАТИЕ в пределах системного интервала Windows
	//   (`GetDoubleClickTime`), а не второе отпускание с порогом 0,35 с.
	double DoubleClickSec()
	{
		return FAsdaDblClick::WindowSec();   // ★ U1 (В2-A дедупа №2): общая точка порога Windows (то же значение, что у оригинала)
	}

	// ★ [ПОРТ] Ключ закрытия окна обмена при смерти (обоснование — в `UAsdaExchangeP2PSubsystem::Tick`).
	ASDA_DEV_CVAR(int32, CVarP2PCloseOnDeath,
		TEXT("asda.Trade.P2PCloseOnDeath"), 1,
		TEXT("Обмен игрок-игрок: 1 — при смерти своего персонажа с открытым окном 2008 закрыть его путём кнопки «Отмена» "
		     "(5286(windowIndex) + 5041); 0 — как оригинал: окно остаётся (сервер снимет обмен при регене/по P44)."),
		ECVF_Default);

	// Поля записи 57 б (`CSlot_Init`): +0 код, +4 контейнер, +5 ячейка, +9 кол-во, +13 прочность, +24 заточка, +27 «+N%», +50.
	int32 RCode(const uint8* R)   { return R ? AsdaBytes::RdI32(R) : 0; }       // +0 i32 код
	int32 RType(const uint8* R)   { return R ? (int32)R[4] : 0; }               // +4 u8 контейнер
	int32 RSlot(const uint8* R)   { return R ? AsdaBytes::RdI16(R + 5) : -1; }  // +5 i16 ячейка
	int32 RCount(const uint8* R)  { return R ? AsdaBytes::RdI32(R + 9) : 0; }   // +9 i32 кол-во
	int32 RB24(const uint8* R)    { return R ? (int32)R[24] : 0; }              // +24 u8 заточка (гейт «+N%»)
	int32 RPct(const uint8* R)    { return R ? AsdaBytes::RdI16(R + 27) : 0; }  // +27 i16 «+N%»
	int32 RB50(const uint8* R)    { return R ? (int32)R[50] : 0; }              // +50 u8 «тип слота»/привязка
	void  WCount(uint8* R, int32 V) { if (R) { AsdaBytes::WrI32(R + 9, V); } }  // запись +9

	// Запись 51 б лотка партнёра (`equipSlotShowTooltip`, item_inventory.c:23372-23391): +0 код, +4 прочность, +5 вес,
	//   +7 кол-во, +11 заточка, +12/+13, +14 «+N%», +22 5×i32 опции, +43 4×i16 самоцветы.
	int32 PCount(const uint8* P)  { return P ? AsdaBytes::RdI32(P + 7) : 0; }   // +7 i32 кол-во
	int32 PB11(const uint8* P)    { return P ? (int32)P[11] : 0; }              // +11 u8 заточка
	int32 PPct(const uint8* P)    { return P ? AsdaBytes::RdI16(P + 14) : 0; }  // +14 i16 «+N%»

	// Синтетическая запись 57 б из записи партнёра — раскладка `equipSlotShowTooltip` (item_inventory.c:23397-23411 — ветка
	//   ItemRecord типа 1; :23426-23436 — ветка второй таблицы). ★ Ревизия О2 (МЕЛК-7): 4 слова самоцветов (+43 → +16) эталон
	//   переносит ТОЛЬКО в ветке ItemRecord типа 1 — `bGems`.
	//   ★ U17 (В1 дедупа №2): сборка через `FAsdaItemRecordWriter` (байты те же; самотест `asda.Dedup.W1ASelfTest`
	//   зовёт её через `UAsdaExchangeP2PSubsystem::PartnerRecordTo57`).
	TArray<uint8> PartnerTo57(const uint8* P, bool bGems)
	{
		FAsdaItemRecordWriter Rw;
		if (!P) { return Rw.ToArray(); }
		Rw.Code(AsdaBytes::RdI32(P + 0));                               // v6[0] = +0
		Rw.Amount(AsdaBytes::RdI32(P + 7));                             // (&v6[2])+1 = +7
		Rw.Durability(P[4]);                                            // BYTE1(v6[3]) = +4
		Rw.StackWeight(AsdaBytes::RdI16(P + 5));                        // HIWORD(v6[3]) = +5
		Rw.GemGrade(P[11]);                                             // LOBYTE(v6[6]) = +11
		Rw.Byte25(P[13]);                                               // BYTE1(v6[6]) = +13
		Rw.Byte26(P[12]);                                               // BYTE2(v6[6]) = +12
		Rw.EnchantPercent(AsdaBytes::RdI16(P + 14));                    // (&v6[6])+3 = +14
		for (int32 i = 0; i < 5; ++i) { Rw.EnchantDword(i, AsdaBytes::RdI32(P + 22 + 4 * i)); }   // +22+4i
		if (bGems) { for (int32 j = 0; j < 4; ++j) { Rw.Sowel(j, AsdaBytes::RdI16(P + 43 + 2 * j)); } }   // +43+2j
		Rw.Flag50(0);                                                   // BYTE2(v6[12]) = 0
		return Rw.ToArray();
	}

	void SetTxt(UTextBlock* T, const FString& S) { if (T) { T->SetText(FText::FromString(S)); } }   // `CControl_SetText`
}

// ═════════════════════════════ КАРКАС ОКНА ═════════════════════════════════════════════════════

bool UAsdaExchangeP2PSubsystem::CloseOnDeath() const
{
	return CVarP2PCloseOnDeath.GetValueOnGameThread() != 0;
}

void UAsdaExchangeP2PSubsystem::PlaneShow(UWidget* W)
{
	// `CControl_Show@0x12E1040`: перезапустить анимацию плана и показать его (как `UAsdaExtractShopSubsystem::PlaneShow`).
	AsdaUi::PlaneShowRestart(W);   // ★ R5-b 12.11.2026: общая точка (то же тело)
}

void UAsdaExchangeP2PSubsystem::PlaneHide(UWidget* W)
{
	// `CControl_CloseHide@0x12E1420`: остановить анимацию без события «завершено» и спрятать.
	AsdaUi::PlaneHideStop(W);   // ★ R5-b 12.11.2026: общая точка (то же тело)
}

void UAsdaExchangeP2PSubsystem::RegisterWindowHooks()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Колесо 2051 `TRAN_WHEELWIN` (событие 23): узла в WBP нет (импортёр не строит WheelWin, волна 0 §1.3), зона колеса по
	//   `.JUI` — (11,63) 198×132 от корня окна = ровно сетка 2013…2036 ⇒ курсор над любой ячейкой списка.
	WM->RegisterWheelHandler(kTranId, FAsdaUiWheelHandler::CreateWeakLambda(this, [this](int32 Notches) -> bool
	{
		if (!bWinShown || !bWinReady) { return false; }
		bool bOver = false;
		for (int32 i = 0; i < kListCells && !bOver; ++i) { if (CellImg.IsValidIndex(i) && CellImg[i] && AsdaUi::IsCursorOverWidget(CellImg[i])) { bOver = true; } }
		if (!bOver) { return false; }
		OnWheel(Notches);
		return true;
	}));
}

void UAsdaExchangeP2PSubsystem::UnregisterWindowHooks()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterWheelHandler(kTranId);
	}
}

bool UAsdaExchangeP2PSubsystem::EnsureWindow()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kTranId, kTranWbp, kWinZ).Ensure(GetGameInstance(), WinWidget, bWinReady,
		[this](UAsdaWindowSubsystem&) { ResetWindowWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupWindowControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kTranId, this, WinWidget, bWinShown, TEXT("TRAN_WIN_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaP2PWin, Error, TEXT("[обмен] WBP_TRAN_WIN не загрузился (%s)"), kTranWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bWinReady;
}

void UAsdaExchangeP2PSubsystem::ResetWindowWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kTranId);
		if (WinWidget) { WM->CloseWindow(kTranId); }
	}
	WinWidget = nullptr;
	bWinReady = false;
	bWinShown = false;
	CellImg.Reset(); CellTxt.Reset();
	BtnTopClose = BtnOk = BtnCancel = nullptr;
	OkGroup = Gear = AniGear = TraySel = nullptr;
	TxtOk = TxtCancel = TxtChkMine = TxtChkPartner = TxtTab01 = TxtTab02 = TxtGuide = nullptr;
	ChkMine = ChkPartner = nullptr;
	WinRelays.Reset();
	DragGhost = FAsdaDragGhost();   // призрак жил в дереве окна — структуру сбросить целиком (общая точка)
	DragArm.Disarm();
	DragSrcIdNum = -1;
	TipIdNum = -1;
	FoundControls = 0;
}

void UAsdaExchangeP2PSubsystem::SetupWindowControls()
{
	// `CExchange_BuildUI@0xC77720` (shop_trade.c:7355-7442): привязки по IDNum; имена — правило импортёра
	//   (`Reports/trade/w0/jui_vs_wbp.txt`: `<idc>`, `<idc>_Text`, `<idc>_<juiIdx>_<план>`, `_grp`).
	bWinReady = false;
	if (!WinWidget || !WinWidget->WidgetTree) { return; }
	CellImg.SetNum(kCells); CellTxt.SetNum(kCells);
	TArray<UWidget*> All;
	WinWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("TRAN_TOPCLOSE"))  { BtnTopClose = B; continue; }   // 2050
			if (N == TEXT("TRAN_OKBUT"))     { BtnOk = B; continue; }         // 2011
			if (N == TEXT("TRAN_CANCELBUT")) { BtnCancel = B; continue; }     // 2012
			continue;
		}
		if (UCheckBox* C = Cast<UCheckBox>(W))
		{
			if (N == TEXT("TRAN_BUT01")) { ChkMine = C; continue; }            // 2048
			if (N == TEXT("TRAN_BUT02")) { ChkPartner = C; continue; }         // 2049
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("TRAN_OKBUT_Text"))           { TxtOk = T; continue; }          // подпись 2011
			if (N == TEXT("TRAN_CANCELBUT_Text"))       { TxtCancel = T; continue; }      // подпись 2012
			if (N == TEXT("TRAN_BUT01_Text"))           { TxtChkMine = T; continue; }     // подпись 2048
			if (N == TEXT("TRAN_BUT02_Text"))           { TxtChkPartner = T; continue; }  // подпись 2049
			if (N == TEXT("Tran_SlotWin01_Tab_Text"))   { TxtTab01 = T; continue; }       // 2056
			if (N == TEXT("Tran_SlotWin02_Tab_Text"))   { TxtTab02 = T; continue; }       // 2057
			if (N == TEXT("Tran_GuideTxt"))             { TxtGuide = T; continue; }       // 2061
			for (int32 i = 0; i < kCells; ++i)
			{
				if (N == FString::Printf(TEXT("TRAN_TRAY%02d"), i + 1)) { CellTxt[i] = T; break; }   // 2013+i
			}
			continue;
		}
		if (N == TEXT("TRAN_OKBUT_grp")) { OkGroup = W; continue; }   // 2011 вместе с подписью и значком (дети в `.JUI`)
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane) || Plane != 0) { continue; }
		if (Idc == TEXT("Tran_Gaer"))    { Gear = W; continue; }      // 2058
		if (Idc == TEXT("Tran_AniGaer")) { AniGear = W; continue; }   // 2059
		if (Idc == TEXT("TRAN_TRAYSEL")) { TraySel = W; continue; }   // 2047
		for (int32 i = 0; i < kCells; ++i)
		{
			if (Idc == FString::Printf(TEXT("TRAN_TRAY%02d"), i + 1)) { CellImg[i] = Cast<UImage>(W); break; }   // план 2013+i
		}
	}

	// ── Кнопки (0x10401 → `exchangeUiOnCommand`): 2050 / 2012 — отмена (LABEL_76); 2011 — итог 5293 (0x7DB → LABEL_72, О3) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, WinRelays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnTopClose, 2050, [this]() { CloseByCancelButton(TEXT("крестик 2050")); });
	Bind(BtnCancel,   2012, [this]() { CloseByCancelButton(TEXT("«Отмена» 2012")); });
	Bind(BtnOk,       2011, [this]() { OnOkClicked(); });
	// 2048 — CheckButton: 0x10601 (взвели) / 0x10602 (сняли) → `exchangeOnLockToggleMsg`. 2049 — HitTestInvisible в ассете
	//   (волна 0, Д-W0-1), обработчика нет: галку ставит код по 5292 партнёра (`CraftSlots_RefreshIcons`, О3).
	if (ChkMine)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 2048;
		R->CheckCallback = [this](int32, bool bChecked) { OnMyLockChecked(bChecked); };
		ChkMine->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(WinRelays, R, ChkMine);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	// ── Подписи `CExchange_BuildUI`: 2011 = 6805, 2012 = 2289, 2048/2049 = 13244, 2056 = 13245, 2057 = 13246, 2061 = 2379 ──
	UGameInstance* GI = GetGameInstance();
	SetTxt(TxtOk,         AsdaNotice::Loc(GI, 6805));
	SetTxt(TxtCancel,     AsdaNotice::Loc(GI, 2289));
	SetTxt(TxtChkMine,    AsdaNotice::Loc(GI, 13244));
	SetTxt(TxtChkPartner, AsdaNotice::Loc(GI, 13244));
	SetTxt(TxtTab01,      AsdaNotice::Loc(GI, 13245));
	SetTxt(TxtTab02,      AsdaNotice::Loc(GI, 13246));
	SetTxt(TxtGuide,      AsdaNotice::Loc(GI, 2379));
	// Хвост BuildUI: окно, 2011 и рамка 2047 скрыты; ячейки кликают кодом (`CClass_SetField926(ячейка, 0x800000)`).
	SetOkButtonShown(false);
	if (TraySel) { PlaneHide(TraySel); }

	// ── Итог разбора: 48 привязок эталона (2051 — колесо без узла WBP, обслуживается кодом) ──
	int32 Found = WinWidget ? 1 : 0;   // 2008
	auto Has = [&Found](const UObject* O) { if (O) { ++Found; } };
	Has(Gear); Has(AniGear); Has(BtnTopClose); Has(BtnOk); Has(BtnCancel); Has(TraySel); Has(ChkMine); Has(ChkPartner);
	Has(TxtTab01); Has(TxtTab02); Has(TxtGuide);
	for (int32 i = 0; i < kCells; ++i) { if (CellImg[i] && CellTxt[i]) { ++Found; } }
	// Tran_TitleBg 2009 — декор, его эталон ищет, но не трогает: привязка считается по наличию плана.
	for (UWidget* W : All) { if (W && W->GetName().StartsWith(TEXT("Tran_TitleBg_"))) { ++Found; break; } }
	FoundControls = Found;
	bWinReady = CellImg[0] && CellImg[kCells - 1] && CellTxt[0] && BtnCancel && ChkMine;
	UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] окно 2008 разобрано: привязок %d из 47 (+ колесо 2051 кодом), готово=%d"), Found, bWinReady ? 1 : 0);
}

void UAsdaExchangeP2PSubsystem::ShowWindow()
{
	// ui_core.c:35205-35207: `CClass_MapInsert(g_pWindowMgr, 2008)` + `CControl_SetVisible(2008, 1)`.
	if (!EnsureWindow()) { UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] окно 2008 не показано: WBP не разобран")); return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM)
	{
		WM->SetWindowVisible(kTranId, true);
		WM->BringWindowToFront(kTranId);
	}
	bWinShown = true;
	Mouse.Reset();
	DragArm.Disarm();
	LastClickIdNum = -1;
}

void UAsdaExchangeP2PSubsystem::HideWindow(const TCHAR* Why)
{
	// `CControl_SetVisible(pSlotCtrl1 = 2008, 0)` (ui_core.c:35231) + `CControl_KillFocus(&g_ItemTooltip)` при `tooltipShown`.
	CancelDragVisual();
	TickTooltip(-1);
	if (bWinShown)
	{
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kTranId, false);
		}
		UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] окно 2008 скрыто (%s)"), Why);
	}
	bWinShown = false;
	bGearHover = false;
	Mouse.Reset();
	DragArm.Disarm();
	LastClickIdNum = -1;
}

void UAsdaExchangeP2PSubsystem::CancelDragVisual()
{
	if (DragGhost.IsActive()) { DragGhost.Cancel(); }
	DragSrcIdNum = -1;
}

// ═════════════════════════════ МЫШЬ ОКНА (события 8/9/0xA, 0xB, 5/6/7, 0xD) ════════════════════════════

int32 UAsdaExchangeP2PSubsystem::HitTestCellAt(const FVector2D& AbsPoint) const
{
	for (int32 i = 0; i < kCells; ++i)
	{
		const UImage* I = CellImg.IsValidIndex(i) ? CellImg[i].Get() : nullptr;
		if (!I || !I->IsVisible()) { continue; }
		FVector2D Mn, Mx;
		if (!AsdaUi::WidgetAbsRect(I, Mn, Mx)) { continue; }
		if (AbsPoint.X >= Mn.X && AbsPoint.X < Mx.X && AbsPoint.Y >= Mn.Y && AbsPoint.Y < Mx.Y) { return kCtrlList + i; }
	}
	return -1;
}

void UAsdaExchangeP2PSubsystem::TickWindowMouse()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Mouse.Update();
	if (AsdaUi::IsModalDispatchBlocked(GI))
	{
		// Модальное окно (вопрос, нумпад) — окно обмена событий не получает; перенос возвращается на место.
		if (DragGhost.IsActive()) { OnDrop(-1); }
		TickTooltip(-1);
		DragArm.Disarm();
		Mouse.Reset();
		return;
	}
	const bool bBlocked = WM && (WM->IsPointerBlockedByTopmost(kTranId) || WM->IsCursorOverHigherWindow(kTranId));
	const int32 Over = bBlocked ? -1 : HitTestCellAt(Mouse.Cursor);
	TickGearHover(!bBlocked && BtnTopClose && AsdaUi::IsCursorOverWidget(BtnTopClose));

	// ── 8/9/0xA — подсказка ячейки (гейт эталона: `!helper_getField1(g_pUIManager)` = ничего не в руке) ──
	if (DragGhost.IsActive() || FAsdaDragGhost::IsAnyActive()) { TickTooltip(-1); }
	else { TickTooltip(Over); }

	// ── 0xB — нажатие ЛКМ по контролу окна (не ячейка — +0x10 = 0: `itemSlotCmdDispatch` вернёт 0) ──
	bool bOverWindow = false;
	if (!bBlocked && WinWidget)
	{
		FVector2D Mn, Mx;
		bOverWindow = (Over >= 0) || (AsdaUi::FramePlanesAbsRect(WinWidget, TEXT("TRAN_WIN_0_%d"), 9, Mn, Mx)
			&& Mouse.Cursor.X >= Mn.X && Mouse.Cursor.X <= Mx.X && Mouse.Cursor.Y >= Mn.Y && Mouse.Cursor.Y <= Mx.Y);
	}
	if (Mouse.bLmbDown && !DragGhost.IsActive() && bOverWindow)
	{
		// ★ Ревизия О2 (МЕЛК-6): второе НАЖАТИЕ по тому же контролу в пределах `GetDoubleClickTime` — это WM_LBUTTONDBLCLK
		//   эталона: состояние левой кнопки 2 → событие 0xD (`CControl_OnLButtonDblClick`), а не 0xB. Взятие ячейки (+0x10)
		//   остаётся от первого нажатия. [ПОРТ] «Тот же контрол» стоит вместо прямоугольника двойного щелчка Windows (4×4 px):
		//   ячейки 32×32, другой контрол под вторым нажатием эталон и так отдал бы другому обработчику.
		const double Now = FPlatformTime::Seconds();
		const bool bDouble = (Over >= 0 && LastClickIdNum == Over && (Now - LastClickTime) <= DoubleClickSec());
		LastClickIdNum = bDouble ? -1 : Over;
		LastClickTime = Now;
		if (bDouble)
		{
			DragArm.Disarm();
			OnCellDoubleClick(Over);
		}
		else
		{
			OnCellPressed(Over);
			if (Over >= kCtrlList && Over < kCtrlPartner && bCellTaken == 1) { DragArm.Arm(Over, -1, Mouse.Cursor); }
		}
	}
	// ── 5 — порог переноса пройден ──
	if (DragArm.IsArmed() && Mouse.bLmb && DragArm.ShouldStart(Mouse.Cursor))
	{
		DragArm.MarkStarted();
		if (!OnDragBegin(DragArm.KeyA)) { DragArm.Disarm(); bDragFailed = true; }
	}
	// ── 6 — кадр переноса: призрак за курсором ──
	if (DragGhost.IsActive()) { DragGhost.Tick(GetWorld()); }
	// ── отпускание: 7 (бросок); простое отпускание без переноса событий эталона не даёт (0xD — на втором НАЖАТИИ выше) ──
	if (Mouse.bLmbUp)
	{
		ON_SCOPE_EXIT { bDragFailed = false; };
		if (DragGhost.IsActive() || DragSrcIdNum >= 0)
		{
			OnDrop(Over);
			DragArm.Disarm();
			LastClickIdNum = -1;   // бросок — не первый щелчок двойного
		}
		else if (bDragFailed)
		{
			// Перенос не начался (гейт 5 отказал): эталон не шлёт ни 7, ни 0xD — отпускание ничего не значит.
			LastClickIdNum = -1;
		}
		else
		{
			DragArm.Disarm();
		}
	}
}

void UAsdaExchangeP2PSubsystem::TickGearHover(bool bOverClose)
{
	// exchangeWnd_OnMessage 8/0xA по 2050 (shop_trade.c:6358-6363 / 6376-6381): 2058 ↔ 2059 (`CControl_Show` / `CloseHide`).
	if (AsdaCloseGear::IsCommonOn()) { bGearHover = false; return; }   // ★ 08.11.2026: ведёт общая точка UAsdaCloseGearSubsystem
	if (bOverClose == bGearHover) { return; }
	bGearHover = bOverClose;
	AsdaUi::SetShownDecor(Gear, !bOverClose);
	if (bOverClose) { PlaneShow(AniGear); } else { PlaneHide(AniGear); }
}

void UAsdaExchangeP2PSubsystem::TickTooltip(int32 IdNum)
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { TipIdNum = -1; return; }
	if (IdNum == TipIdNum) { return; }
	TipIdNum = IdNum;
	if (IdNum < 0) { Tip->HideTooltipFor(EAsdaTipOwner::ExchangeP2P); return; }   // 0xA → `CControl_KillFocus(&g_ItemTooltip)`
	// 8 → `equipSlotShowTooltip@0xC70DB0` (item_inventory.c:23324-23540).
	const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	const bool bType1 = ExchangeType != 0;
	TArray<uint8> Rec57;
	int32 Code = 0;
	if (IdNum >= kCtrlList && IdNum < kCtrlTray)
	{
		const int32 Idx = (IdNum - kCtrlList) + kCols * ScrollRow;
		if (!ListIndexValid(Idx)) { Tip->HideTooltipFor(EAsdaTipOwner::ExchangeP2P); return; }
		if (const uint8* R = ListRec(bType1 ? 1 : 0, Idx)) { Rec57.Append(R, kRec); Code = RCode(R); }
	}
	else if (IdNum >= kCtrlTray && IdNum < kCtrlPartner)
	{
		if (const uint8* R = TrayRec(IdNum - kCtrlTray)) { Rec57.Append(R, kRec); Code = RCode(R); }
	}
	else if (IdNum >= kCtrlPartner && IdNum < kCtrlPartner + kTray)
	{
		const uint8* P = PartnerRecAt(IdNum - kCtrlPartner);
		Rec57 = BuildPartnerRec57(IdNum - kCtrlPartner);
		Code = P ? AsdaBytes::RdI32(P) : 0;
	}
	if (Code <= 0 || !IT) { Tip->HideTooltipFor(EAsdaTipOwner::ExchangeP2P); return; }
	FAsdaItemInstance It;
	It.SetRaw(Rec57.GetData(), Rec57.Num());
	// Тип 0 и золото — `CControl_SetState(&g_ItemTooltip, 0, код, кол-во, …)` (простой билдер = вкладка «Расходники»,
	//   источник 2); тип 1 — `ItemTooltip_Build` по записи (полный, источник 1); самоцвет — свой билдер (выбирает сам тултип).
	const int32 SourceKind = (!bType1 || Code == kGoldCode) ? 2 : 1;
	Tip->ShowItemTooltip(It, /*bSuppressCompare*/ false, EAsdaTipOwner::ExchangeP2P, SourceKind);
}

TArray<uint8> UAsdaExchangeP2PSubsystem::PartnerRecordTo57(const uint8* P, bool bGems)
{
	return PartnerTo57(P, bGems);
}

TArray<uint8> UAsdaExchangeP2PSubsystem::BuildPartnerRec57(int32 Slot) const
{
	// `equipSlotShowTooltip` (item_inventory.c:23364-23441), ячейки 2042…2046: самоцветы — только ветка ItemRecord типа 1.
	const uint8* P = PartnerRecAt(Slot);
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const bool bGems = P && ExchangeType != 0 && IT && IT->GetItem(AsdaBytes::RdI32(P)) != nullptr;
	return PartnerTo57(P, bGems);
}

// ═════════════════════════════ ПЕРЕРИСОВКИ ═════════════════════════════════════════════════════

void UAsdaExchangeP2PSubsystem::DrawCell(int32 IdNum, int32 Code, bool bClearIfEmpty)
{
	const int32 i = IdNum - kCtrlList;
	UImage* I = CellImg.IsValidIndex(i) ? CellImg[i].Get() : nullptr;
	if (!I) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bMale = !Net || Net->ResolveSelfGender() != 2;   // `g_ItemImgOptionVariant`
	// `CControl_DrawTextureRect(ячейка, текстура, x, y, 32, 32)`; `Code <= 0` — пустая текстура `CClass_GetSubObjectAtFixedOff`.
	UAsdaIconCellGrid::ApplyItemIcon(WinWidget, I, Code, bMale, 32.f, 32.f, bClearIfEmpty);
}

void UAsdaExchangeP2PSubsystem::SetCellText(int32 IdNum, const FString& Text)
{
	const int32 i = IdNum - kCtrlList;
	if (CellTxt.IsValidIndex(i)) { SetTxt(CellTxt[i], Text); }
}

void UAsdaExchangeP2PSubsystem::SetCellTextColor(int32 IdNum, const FColor& Color)
{
	const int32 i = IdNum - kCtrlList;
	if (CellTxt.IsValidIndex(i) && CellTxt[i]) { CellTxt[i]->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(Color))); }
}

void UAsdaExchangeP2PSubsystem::SetCellFill(int32 IdNum, int32 Mode)
{
	// `CPlane_SetFillMode(ячейка, 0 / 5)`. [ПОРТ] 5 — план «уехал за курсором»: ячейка приглушена (призрак рисуется отдельно).
	const int32 i = IdNum - kCtrlList;
	UImage* I = CellImg.IsValidIndex(i) ? CellImg[i].Get() : nullptr;
	if (!I) { return; }
	FLinearColor C = I->GetColorAndOpacity();
	C.A = (Mode == 5) ? kDragAlpha : 1.f;
	I->SetColorAndOpacity(C);
}

void UAsdaExchangeP2PSubsystem::SetOkButtonShown(bool bShown)
{
	// `CControl_SetVisible(2011, …)`: в `.JUI` подпись и значок — дети 2011, в WBP — соседи в `TRAN_OKBUT_grp`.
	if (OkGroup) { OkGroup->SetVisibility(bShown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	else if (BtnOk) { BtnOk->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
}

void UAsdaExchangeP2PSubsystem::SetCheckState(int32 IdNum, bool bChecked)
{
	// `CControl_SetVisibleState(2048/2049, …)` — галка флажка.
	UCheckBox* C = (IdNum == 2048) ? ChkMine.Get() : (IdNum == 2049 ? ChkPartner.Get() : nullptr);
	if (C && C->IsChecked() != bChecked) { C->SetIsChecked(bChecked); }
}

void UAsdaExchangeP2PSubsystem::SlotIconsReset()
{
	// `uiSlotIconsReset@0xC77570` (ui_core.c:35441-35476): 24 кандидата, 5 + 5 лотков — пустая текстура и пустая надпись;
	//   галки 2048 / 2049 сняты (`SetVisibleState(…, 0)`, `ClearField609(2048)`).
	for (int32 i = 0; i < kCells; ++i)
	{
		DrawCell(kCtrlList + i, 0, /*bClearIfEmpty*/ true);
		SetCellText(kCtrlList + i, FString());
		SetCellFill(kCtrlList + i, 0);
	}
	SetCheckState(2048, false);
	SetCheckState(2049, false);
	bMyLockLatch = false;   // `CControl_ClearField609(2048)` (ui_core.c:35466) — флажок готовности снова принимает клик
}

bool UAsdaExchangeP2PSubsystem::ListIndexValid(int32 Index) const
{
	// `sub_C7A8A0`: `return a2 <= +0x26C8` (Д14: равенство числу записей проходит — дальше запись пуста и безвредна).
	return Index <= ListCount;
}

uint8* UAsdaExchangeP2PSubsystem::ListRec(int32 Type, int32 Index)
{
	TArray<uint8>& L = Type ? List1 : List0;
	const int32 Max = Type ? kList1Max : kList0Max;
	return (Index >= 0 && Index < Max && L.Num() >= Max * kRec) ? L.GetData() + kRec * Index : nullptr;
}

const uint8* UAsdaExchangeP2PSubsystem::ListRec(int32 Type, int32 Index) const
{
	const TArray<uint8>& L = Type ? List1 : List0;
	const int32 Max = Type ? kList1Max : kList0Max;
	return (Index >= 0 && Index < Max && L.Num() >= Max * kRec) ? L.GetData() + kRec * Index : nullptr;
}

uint8* UAsdaExchangeP2PSubsystem::TrayRec(int32 Slot)
{
	return (Slot >= 0 && Slot < kTray && MyTray.Num() >= kTray * kRec) ? MyTray.GetData() + kRec * Slot : nullptr;
}

const uint8* UAsdaExchangeP2PSubsystem::TrayRec(int32 Slot) const
{
	return (Slot >= 0 && Slot < kTray && MyTray.Num() >= kTray * kRec) ? MyTray.GetData() + kRec * Slot : nullptr;
}

uint8* UAsdaExchangeP2PSubsystem::CopyRec(int32 Slot)
{
	return (Slot >= 0 && Slot < kTray && TrayCopy.Num() >= kTray * kCopyRec) ? TrayCopy.GetData() + kCopyRec * Slot : nullptr;
}

const uint8* UAsdaExchangeP2PSubsystem::PartnerRecAt(int32 Slot) const
{
	return (Slot >= 0 && Slot < kTray && PartnerTray.Num() >= kTray * kPartnerRec) ? PartnerTray.GetData() + kPartnerRec * Slot : nullptr;
}

void UAsdaExchangeP2PSubsystem::BuildSourceList(int32 Type)
{
	// `invListFilterConsumablesRefresh@0xC78E20` (item_inventory.c:23637-23647): фильтр по типу, сброс выбора с перерисовкой,
	//   `uiInvSetScrollAndCountFilled` — колесо 4 видимых ряда из 6 (тип 1) / 10 (тип 0); число заполненных эталон считает,
	//   но не хранит.
	if (Type) { FilterType1(); } else { FilterType0(); }
	ResetSelectionAndRefresh();
}

void UAsdaExchangeP2PSubsystem::FilterType0()
{
	// `invFilterSowelsToList@0xC78E90` (МИСНЕЙМ; item_inventory.c:23649-23694): контейнер 2 (60 ячеек) → +0xBFC.
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const int32 Level = AsdaSelf::GetLevel(GI);
	List0.Init(0, kList0Max * kRec);   // memset(+3068, 0, 0xD5C)
	int32 N = 0;
	for (int32 i = 0; i < kList0Max && IT && Inv; ++i)
	{
		const FAsdaItemInstance* Cell = Inv->GetCell(2, i);
		if (!Cell || Cell->ItemCode <= 0 || Cell->Raw.Num() < kRec) { continue; }
		const uint8* Src = Cell->Raw.GetData();
		if (Src[50] != 0) { continue; }   // `!*(&g_aInvSlotType + 57 * i)` (+50)
		const FAsdaItemRecord* Rec = IT->GetItem(Cell->ItemCode);
		bool bAdd = false;
		if (Rec)
		{
			// `tooltipFlag200 != 2 && (+50 != 3 || subType != 32)`; `categoryFlag == 3` — только с 24 ур.
			if (Rec->TooltipFlag200 != 2 && (Src[50] != 3 || Rec->ItemSubType != 32))
			{
				bAdd = (Rec->CategoryFlag == 3) ? (Level >= kLevelTrade) : true;
			}
		}
		else
		{
			FAsdaSowellRow Sw;
			// `SowelRecord && !SowelRecord->gap_41[1] && itemGetCategoryById(код) == 2`.
			if (IT->IsSowelCode(Cell->ItemCode) && IT->GetSowell(Cell->ItemCode, Sw)
				&& UAsdaItemTableSubsystem::SowelRawByte(Sw, 0x42) == 0 && IT->GetItemCategory(Cell->ItemCode) == 2)
			{
				bAdd = true;
			}
		}
		// [ПОРТ] потолок `N < 60` — защита массива; у эталона его нет, но источник — те же 60 ячеек, переполнения не бывает.
		if (bAdd && N < kList0Max) { FMemory::Memcpy(List0.GetData() + kRec * N, Src, kRec); ++N; }
	}
	ListCount = N;   // `*(this + 2482) = v6` (+0x26C8)
}

void UAsdaExchangeP2PSubsystem::FilterType1()
{
	// `invFilterConsumablesToList@0xC79110` (МИСНЕЙМ; item_inventory.c:23696-23728): контейнер 1 (30) + всё золото контейнера 2.
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const int32 Level = AsdaSelf::GetLevel(GI);
	List1.Init(0, kList1Max * kRec);   // memset(+1301, 0, 0x6E7)
	int32 N = 0;
	// [ПОРТ] потолок `N < 31` — защита массива +0x515 (31 × 57); у эталона его нет: 30 ячеек сумки + ячейки золота контейнера 2
	//   (у сервера золото — одна ячейка), переполнения при штатных данных не бывает.
	auto Push = [this, &N](const uint8* Src) { if (N < kList1Max) { FMemory::Memcpy(List1.GetData() + kRec * N, Src, kRec); ++N; } };
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон перебирает 30 ячеек сумки — у нас всю доступную сумку (60; `kList1Max` = 61).
	for (int32 i = 0; IT && Inv && i < Inv->GetContainerSize(1); ++i)
	{
		const FAsdaItemInstance* Cell = Inv->GetCell(1, i);
		if (!Cell || Cell->ItemCode <= 0 || Cell->Raw.Num() < kRec) { continue; }
		const uint8* Src = Cell->Raw.GetData();
		if (Src[50] != 0) { continue; }   // `!byte_2901982[57 * i]` (+50)
		const int32 Code = Cell->ItemCode;
		const int32 Cat = IT->GetItemCategory(Code);   // `itemGetCategoryById`
		if (const FAsdaItemRecord* Rec = IT->GetItem(Code))
		{
			if (Rec->TooltipFlag200 == 2) { continue; }
			if (Rec->CategoryFlag == 3)
			{
				if ((Cat == 3 || Cat == 4) && Rec->ItemType != 11 && Rec->ItemType != 13 && Level >= kLevelTrade) { Push(Src); }
				if ((Rec->ItemType == 11 || Rec->ItemType == 13) && Level >= kLevelTrade) { Push(Src); }
			}
			else if (Cat == 3 || Cat == 4)
			{
				Push(Src);
			}
		}
		if (Cat == 3 || Cat == 4)
		{
			FAsdaSpecialItemlistRow Sp;
			if (IT->GetSpecialItem(Code, Sp) && Sp.Flag124 != 2)
			{
				// Коды 335…337 и 418…420 — только если мне ≥ 24 и «подхваченный» персонаж не найден или ≤ 23 ур.
				const bool bLvl = Level > 23;
				const bool bOk1 = (Sp.Code < 335 || Sp.Code > 337 || (bLvl && PickedIsLowOrAbsent()));
				const bool bOk2 = (Sp.Code < 418 || Sp.Code > 420 || (bLvl && PickedIsLowOrAbsent()));
				if (bOk1 && bOk2)
				{
					// `Table2Record->flag_106[0] == 2` (+0x6A) — только с 24 ур.
					if (UAsdaItemTableSubsystem::SpecialItemRawByte(Sp, 0x6A) == 2) { if (Level >= kLevelTrade) { Push(Src); } }
					else { Push(Src); }
				}
			}
		}
	}
	for (int32 j = 0; j < kList0Max && Inv; ++j)
	{
		const FAsdaItemInstance* Cell = Inv->GetCell(2, j);
		if (Cell && Cell->ItemCode == kGoldCode && Cell->Raw.Num() >= kRec) { Push(Cell->Raw.GetData()); }   // всё золото
	}
	ListCount = N;
}

bool UAsdaExchangeP2PSubsystem::PickedIsLowOrAbsent() const
{
	// item_inventory.c:23745-23746: `(p = CharManager_FindByUserIdx(g_CharManager, g_nPickedUserIdx)) == nullptr || p->level <= 0x17`.
	//   ★ Ревизия О2 (СРЕДН-2): источник — `g_nPickedUserIdx` (последний удачный ПКМ-пик игрока, `UAsdaPlayerMenuSubsystem`),
	//   а не цель меню +0x2700. Менеджер персонажей эталона держит и своего: пик по себе даёт мой уровень.
	const UGameInstance* GI = GetGameInstance();
	const UAsdaPlayerMenuSubsystem* PM = GI ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
	const int32 U = PM ? PM->GetPickedUserIdx() : -1;
	if (U < 0) { return true; }
	if (AsdaSelf::IsSelfUserIdx(GI, U)) { return AsdaSelf::GetLevel(GI) <= 23; }
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	FString Nm; int32 Cls = 0, Lvl = 0, Clan = -1;
	if (!WS || !WS->GetNetPlayerInfo(U, Nm, Cls, Lvl, Clan)) { return true; }   // не найден в менеджере — `== nullptr`
	return Lvl <= 23;   // `pByUserIdx->level <= 0x17u`
}

void UAsdaExchangeP2PSubsystem::ResetSelectionAndRefresh()
{
	// `CItemListWin_ResetSelectionAndRefresh@0xC7B5A0`: +0x26D0 = −1, +0x26B8 = −1, перерисовать страницу +0x26B4.
	SelSlotIndex = -1;
	CachedPage = -1;
	RefreshItemPage(ScrollRow);
}

void UAsdaExchangeP2PSubsystem::RefreshItemPage(int32 Page)
{
	// `CItemListWin_RefreshItemPage@0xC79600` (item_inventory.c:23774-23990).
	if (Page == CachedPage) { return; }
	CachedPage = Page;   // `*(_DWORD *)pWin[5].gap_1B0 = pageIndex` (+0x26B8)
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const bool bType1 = ExchangeType != 0;
	int32 Slot = kCols * Page;
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): граница типа 1 — ёмкость сумки (эталон 30).
	if (bType1 ? (Slot >= UAsdaInventorySubsystem::GetShellCapacity()) : (Slot >= kList0Max)) { Slot = 0; }
	// Эталон охраняет ВЕСЬ блок `if (+0x26D0 < 24)` и в нём же сбрасывает заливку ячейки с НОМЕРОМ = индексу выбора.
	//   [ПОРТ, Д17] при исправлении охраняется только сброс заливки (иначе колесо при выборе ≥ 24 не листает список).
	const bool bGuardOk = SelSlotIndex < kListCells;
	if (!bGuardOk && !FixDefects()) { return; }
	// [ПОРТ] `SelSlotIndex >= 0` — у эталона при −1 — обращение к контролу с индексом −1 (`questSlots[-1]`, память перед
	//   массивом); порт его не повторяет.
	if (bGuardOk && SelSlotIndex >= 0) { SetCellFill(kCtrlList + SelSlotIndex, 0); }
	if (!IT) { return; }
	for (int32 i = 0; i < kListCells; ++i, ++Slot)
	{
		const int32 Id = kCtrlList + i;
		auto Empty = [this, Id]() { DrawCell(Id, 0, true); SetCellText(Id, FString()); };
		// «Идёт перенос этого кандидата» — ячейка пустая (`+0x26D0 == slotIdx && +0x24 == 1`).
		if (SelSlotIndex == Slot && bDragging == 1) { Empty(); continue; }
		const uint8* R = ListRec(bType1 ? 1 : 0, Slot);
		const int32 Code = RCode(R);
		if (Code <= 0) { Empty(); continue; }
		if (!bType1)
		{
			if (IT->IsSowelCode(Code))
			{
				FAsdaSowellRow Sw;
				if (!IT->GetSowell(Code, Sw)) { Empty(); continue; }
				DrawCell(Id, Code, true);   // `sowelRecord->iconPath` 32×32 (пустой путь — пустая текстура)
				SetCellText(Id, FString());
				continue;
			}
			const FAsdaItemRecord* Rec = IT->GetItem(Code);
			if (!Rec) { continue; }   // записи нет — ячейка остаётся как была (эталон ничего не рисует)
			if (Rec->ItemCode <= 0) { Empty(); continue; }
			DrawCell(Id, Code, true);
			// `tradableFlag == 0` / subType 32 (при +50 == 3 — «bind_soulstone»: недостижимо, фильтр пропускает только +50 == 0) /
			//   золото — без надписи; иначе количество `%d` (цвет эталон не ставит).
			if (!Rec->TradableFlag || Rec->ItemSubType == 32 || Code == kGoldCode) { SetCellText(Id, FString()); }
			else { SetCellText(Id, FString::Printf(TEXT("%d"), RCount(R))); }
			continue;
		}
		// Тип 1.
		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = IT->GetSpecialItem(Code, Sp);
		if (Rec)
		{
			if (Rec->ItemCode <= 0) { Empty(); continue; }
			DrawCell(Id, Code, true);
			SetCellText(Id, FString());
			SetCellTextColor(Id, kTxtWhite);
			if (RB24(R)) { SetCellText(Id, FString::Printf(TEXT("+%d%%"), RPct(R))); SetCellTextColor(Id, kTxtYellow); }
			continue;
		}
		if (!bSp || Sp.Code <= 0 || Sp.IconPath.IsEmpty()) { Empty(); continue; }
		DrawCell(Id, Code, true);   // `Grid_IndexToCellOffset8x64(iconCell)` 64×64 — клетка атласа из данных
		if (RCount(R) > 0) { SetCellText(Id, FString::Printf(TEXT("%d"), RCount(R))); SetCellTextColor(Id, kTxtWhite); }
		if (RB24(R)) { SetCellText(Id, FString::Printf(TEXT("+%d%%"), RPct(R))); SetCellTextColor(Id, kTxtYellow); }
	}
}

void UAsdaExchangeP2PSubsystem::RefreshTrayIcons()
{
	// `CQuest_RefreshItemIconGrid@0xC766E0` (quest.c:4915-5090): +0x26B8 = −2, страница списка, затем 5 ячеек моего лотка.
	CachedPage = -2;
	RefreshItemPage(ScrollRow);
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	const bool bType1 = ExchangeType != 0;
	for (int32 k = 0; k < kTray; ++k)
	{
		const int32 Id = kCtrlTray + k;
		const uint8* R = TrayRec(k);
		const int32 Code = RCode(R);
		auto Empty = [this, Id]() { DrawCell(Id, 0, true); SetCellText(Id, FString()); };
		if (Code <= 0) { Empty(); continue; }
		if (!bType1)
		{
			if (IT->IsSowelCode(Code))
			{
				FAsdaSowellRow Sw;
				if (!IT->GetSowell(Code, Sw)) { Empty(); continue; }
				DrawCell(Id, Code, true); SetCellText(Id, FString());
				continue;
			}
			const FAsdaItemRecord* Rec = IT->GetItem(Code);
			if (!Rec || Rec->ItemCode <= 0) { Empty(); continue; }
			DrawCell(Id, Code, true);
			SetCellText(Id, FString());
			if (Rec->TradableFlag == 1 && Code != kGoldCode)
			{
				// Кол-во `%d` при > 0 (цвет −1 эталон пишет в сдвинутый указатель `gap_D[4i+8]` — надпись цвета не получает);
				//   заточка — «+N%» жёлтым.
				if (RCount(R) > 0) { SetCellText(Id, FString::Printf(TEXT("%d"), RCount(R))); }
				if (RB24(R)) { SetCellText(Id, FString::Printf(TEXT("+%d%%"), RPct(R))); SetCellTextColor(Id, kTxtYellow); }
			}
			continue;
		}
		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = IT->GetSpecialItem(Code, Sp);
		if (Rec)
		{
			if (Rec->ItemCode > 0)
			{
				DrawCell(Id, Code, true);
				SetCellText(Id, FString());
				if (RB24(R)) { SetCellText(Id, FString::Printf(TEXT("+%d%%"), RPct(R))); SetCellTextColor(Id, kTxtYellow); }
			}
			continue;
		}
		if (!bSp) { Empty(); continue; }
		if (Sp.Code <= 0) { continue; }
		if (Sp.IconPath.IsEmpty()) { Empty(); continue; }
		DrawCell(Id, Code, true);
		if (RCount(R) <= 0) { SetCellText(Id, FString()); }
		else { SetCellText(Id, FString::Printf(TEXT("%d"), RCount(R))); SetCellTextColor(Id, kTxtWhite); }
		if (RB24(R)) { SetCellText(Id, FString::Printf(TEXT("+%d%%"), RPct(R))); SetCellTextColor(Id, kTxtYellow); }
	}
}

void UAsdaExchangeP2PSubsystem::RefreshPartnerTray(bool bShow)
{
	// `CraftSlots_RefreshIcons@0xC73F30` (item_enhance.c:190-400). bShow = 0: 2011 скрыт, галка 2049 снята, лоток партнёра
	//   обнулён и пуст; bShow ≠ 0: галка 2049, при счётчике 2 — показать 2011, нарисовать 5 записей партнёра.
	if (!bShow)
	{
		SetOkButtonShown(false);
		SetCheckState(2049, false);
		PartnerTray.Init(0, kTray * kPartnerRec);
		for (int32 k = 0; k < kTray; ++k) { DrawCell(kCtrlPartner + k, 0, true); SetCellText(kCtrlPartner + k, FString()); }
		return;
	}
	SetCheckState(2049, true);
	if (ReadyCounter == 2) { SetOkButtonShown(true); }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	const bool bType1 = ExchangeType != 0;
	for (int32 k = 0; k < kTray; ++k)
	{
		const int32 Id = kCtrlPartner + k;
		const uint8* P = PartnerRecAt(k);
		const int32 Code = P ? AsdaBytes::RdI32(P) : 0;
		auto Empty = [this, Id]() { DrawCell(Id, 0, true); SetCellText(Id, FString()); };
		if (!bType1)
		{
			if (IT->IsSowelCode(Code))
			{
				FAsdaSowellRow Sw;
				if (!IT->GetSowell(Code, Sw)) { Empty(); continue; }
				DrawCell(Id, Code, true); SetCellText(Id, FString());
				continue;
			}
			const FAsdaItemRecord* Rec = IT->GetItem(Code);
			if (!Rec) { Empty(); continue; }
			if (Code == kGoldCode) { if (Rec->ItemCode > 0) { DrawCell(Id, Code, true); SetCellText(Id, FString()); } continue; }
			if (Rec->ItemCode > 0 && Rec->TradableFlag == 1) { DrawCell(Id, Code, true); SetCellText(Id, FString::Printf(TEXT("%d"), PCount(P))); continue; }
			if (Rec->ItemCode > 0 && !Rec->TradableFlag && !Rec->FlagWord176) { DrawCell(Id, Code, true); SetCellText(Id, FString()); }
			continue;   // `tradableFlag == 0 && flagWord176` — ни одна ветка не рисует (ячейка как была)
		}
		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = IT->GetSpecialItem(Code, Sp);
		if (Rec)
		{
			if (Rec->ItemCode <= 0) { Empty(); continue; }
			DrawCell(Id, Code, true);
			SetCellText(Id, FString());
			SetCellTextColor(Id, kTxtWhite);
			if (PB11(P)) { SetCellText(Id, FString::Printf(TEXT("+%d%%"), PPct(P))); SetCellTextColor(Id, kTxtYellow); }
			continue;
		}
		if (!bSp || Sp.Code <= 0 || Sp.IconPath.IsEmpty()) { Empty(); continue; }
		DrawCell(Id, Code, true);
		if (PCount(P) <= 0) { SetCellText(Id, FString()); }
		else { SetCellText(Id, FString::Printf(TEXT("%d"), PCount(P))); SetCellTextColor(Id, kTxtWhite); }
		if (PB11(P)) { SetCellText(Id, FString::Printf(TEXT("+%d%%"), PPct(P))); SetCellTextColor(Id, kTxtYellow); }
	}
}

void UAsdaExchangeP2PSubsystem::MoveCursorToSel()
{
	// `uiItemSlotMoveCursorToSel@0xC75D00`: рамка 2047 — на позицию кандидата +0x26BC, показать.
	if (!TraySel || !CellImg.IsValidIndex(SelListCell) || !CellImg[SelListCell]) { return; }
	UCanvasPanelSlot* Dst = AsdaUi::SlotOf(TraySel);
	UCanvasPanelSlot* Src = AsdaUi::SlotOf(CellImg[SelListCell]);
	if (Dst && Src) { Dst->SetPosition(Src->GetPosition()); }   // рамка и ячейки — один родитель `Tran_TraySlot01_grp`
	PlaneShow(TraySel);
}

void UAsdaExchangeP2PSubsystem::RefreshSlotFillText()
{
	// `exchangeWnd_RefreshSlotFillText@0xC72440` (6): источник-список — рамка 2047 скрыта, ячейка +0x26BC «у курсора»,
	//   надпись пуста; источник-лоток — то же для ячейки лотка +0x26C4.
	if (SelSource == 0)
	{
		if (TraySel) { PlaneHide(TraySel); }
		SetCellFill(ListCtrl(SelListCell), 5);
		SetCellText(ListCtrl(SelListCell), FString());
	}
	if (SelSource == 1)
	{
		SetCellFill(TrayCtrl(SelTrayCell), 5);
		SetCellText(TrayCtrl(SelTrayCell), FString());
	}
}

void UAsdaExchangeP2PSubsystem::RefreshSlotText()
{
	// `uiInventoryRefreshSlotText@0xC75D70` (ui_core.c:35281-35440): вернуть ячейку-источник после отмены переноса/нумпада.
	if (TraySel) { PlaneHide(TraySel); }   // `CControl_SetVisible(2047, 0)`
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 PageLo = kCols * ScrollRow;
	const int32 PageHi = PageLo + kListCells;
	auto Tail = [this]() { ResetSelectionAndRefresh(); bCellTaken = 0; bDragging = 0; };
	if (ExchangeType != 0)
	{
		if (SelSource != 0)
		{
			const uint8* R = TrayRec(SelTrayCell);
			SetCellFill(TrayCtrl(SelTrayCell), 0);
			FAsdaSpecialItemlistRow Sp;
			if (IT && R && !IT->GetItem(RCode(R)) && IT->GetSpecialItem(RCode(R), Sp))
			{
				const int32 Id = TrayCtrl(SelTrayCell);
				if (RCount(R) <= 0) { SetCellText(Id, FString()); }
				else { SetCellText(Id, FString::Printf(TEXT("%d"), RCount(R))); SetCellTextColor(Id, kTxtWhite); }
				if (RB24(R)) { SetCellText(Id, FString::Printf(TEXT("+%d%%"), RPct(R))); SetCellTextColor(Id, kTxtYellow); }
			}
		}
		else
		{
			SetCellFill(ListCtrl(SelListCell), 0);
		}
		Tail();
		return;
	}
	if (SelSource != 0)
	{
		SetCellFill(TrayCtrl(SelTrayCell), 0);
		const uint8* R = TrayRec(SelTrayCell);
		const FAsdaItemRecord* Rec = (IT && R) ? IT->GetItem(RCode(R)) : nullptr;
		if (!Rec) { return; }   // эталон выходит, ничего не сбрасывая
		const int32 Id = TrayCtrl(SelTrayCell);
		if (IT->IsSowelCode(RCode(R)) || Rec->TradableFlag != 1 || RCode(R) == kGoldCode) { SetCellText(Id, FString()); Tail(); return; }
		if (RCode(R) <= 0) { return; }
		SetCellText(Id, FString::Printf(TEXT("%d"), RCount(R)));
		Tail();
		return;
	}
	SetCellFill(ListCtrl(SelListCell), 0);
	const uint8* R = ListRec(0, SelSlotIndex);
	const FAsdaItemRecord* Rec = (IT && R) ? IT->GetItem(RCode(R)) : nullptr;
	if (!Rec) { return; }
	const int32 Id = ListCtrl(SelListCell);
	if ((RB50(R) == 3 && Rec->ItemSubType == 32) || IT->IsSowelCode(RCode(R))) { SetCellText(Id, FString()); Tail(); return; }
	if (RCode(R) <= 0) { return; }
	if (Rec->TradableFlag == 1)
	{
		if (RCode(R) == kGoldCode) { SetCellText(Id, FString()); Tail(); return; }
		if (SelSlotIndex >= PageLo && SelSlotIndex <= PageHi) { SetCellText(Id, FString::Printf(TEXT("%d"), RCount(R))); }
		Tail();
		return;
	}
	SetCellText(Id, FString());
	Tail();
}

// ═════════════════════════════ СОБЫТИЯ ОКНА ════════════════════════════════════════════════════

bool UAsdaExchangeP2PSubsystem::SelectListCell(int32 Cell)
{
	// `itemSlotSelectByIndex@0xC70860` (item_inventory.c:23198-23269).
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 Idx = Cell + kCols * ScrollRow;
	if (!ListIndexValid(Idx) || !IT) { return false; }
	SelListCell = Cell;   // `this->selectedTraySlot = a2` (+0x26BC)
	const bool bType1 = ExchangeType != 0;
	const uint8* R = ListRec(bType1 ? 1 : 0, Idx);
	const int32 Code = RCode(R);
	const FAsdaItemRecord* Rec = nullptr;
	FAsdaSpecialItemlistRow Sp;
	bool bSp = false;
	if (bType1)
	{
		if (Code <= 0) { return false; }
		Rec = IT->GetItem(Code);
		bSp = IT->GetSpecialItem(Code, Sp);
		if (Rec) { if (Rec->ItemCode > 0) { SelCode = Rec->ItemCode; } }
		else if (bSp && Sp.Code > 0) { SelCode = Sp.Code; }
	}
	else if (!IT->IsSowelCode(Code))
	{
		if (Code <= 0) { return false; }
		Rec = IT->GetItem(Code);
		if (Rec && Rec->ItemCode > 0) { SelCode = Rec->ItemCode; }
	}
	else
	{
		FAsdaSowellRow Sw;
		if (IT->GetSowell(Code, Sw) && Code > 0)
		{
			SelCode = Code;
			SelSlotIndex = Idx;
			MoveCursorToSel();
			return true;
		}
	}
	if (Rec)
	{
		if (Rec->ItemCode <= 0) { return false; }
		SelSlotIndex = Idx;
		MoveCursorToSel();
		return true;
	}
	if (bSp)
	{
		if (Sp.Code <= 0) { return false; }
		SelSlotIndex = Idx;
		MoveCursorToSel();
		return true;
	}
	return false;
}

bool UAsdaExchangeP2PSubsystem::ResolveTrayCell(int32 Cell)
{
	// `itemSlotResolveIconRecord@0xC70B40` (item_inventory.c:23271-23322).
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	SelTrayCell = Cell;   // +0x26C4
	if (Cell >= kTray || !IT) { return false; }
	const uint8* R = TrayRec(Cell);
	const int32 Code = RCode(R);
	const FAsdaItemRecord* Rec = nullptr;
	FAsdaSpecialItemlistRow Sp;
	bool bSp = false;
	if (ExchangeType != 0)
	{
		if (Code <= 0) { return false; }
		Rec = IT->GetItem(Code);
		bSp = IT->GetSpecialItem(Code, Sp);
		if (Rec) { if (Rec->ItemCode > 0) { SelCode = Rec->ItemCode; } }
		else if (bSp && Sp.Code) { SelCode = Sp.Code; }
	}
	else if (!IT->IsSowelCode(Code))
	{
		if (Code <= 0) { return false; }
		Rec = IT->GetItem(Code);
		if (Rec && Rec->ItemCode > 0) { SelCode = Rec->ItemCode; }
	}
	else
	{
		FAsdaSowellRow Sw;
		if (IT->GetSowell(Code, Sw) && Code > 0) { SelCode = Code; return true; }
	}
	if (Rec) { return Rec->ItemCode > 0; }
	if (bSp) { return Sp.Code > 0; }
	return false;
}

void UAsdaExchangeP2PSubsystem::OnCellPressed(int32 IdNum)
{
	// 0xB → `itemSlotCmdDispatch@0xC6F700`: 2013…2036 — выбор кандидата, 2037…2041 — ячейка лотка; прочее — 0.
	bool bTaken = false;
	if (IdNum >= kCtrlList && IdNum < kCtrlTray) { bTaken = SelectListCell(IdNum - kCtrlList); }
	else if (IdNum >= kCtrlTray && IdNum < kCtrlPartner) { bTaken = ResolveTrayCell(IdNum - kCtrlTray); }
	bCellTaken = bTaken ? 1 : 0;   // `this->isActive = …`
}

bool UAsdaExchangeP2PSubsystem::EquipValidate(int32 IdNum)
{
	// `itemEquipValidate@0xC7A8E0` (item_inventory.c:24131-24191): только кандидаты 2013…2036, по +0x26D0.
	if (IdNum < kCtrlList || IdNum >= kCtrlTray) { return true; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return true; }
	if (ExchangeType != 0)
	{
		const uint8* R = ListRec(1, SelSlotIndex);
		const FAsdaItemRecord* Rec = IT->GetItem(RCode(R));
		if (Rec && Rec->ItemCode > 0)
		{
			if (Rec->ItemCode == kGoldCode) { return true; }
			// `itemCode > 7000 && itemCode <= 2001` — всегда ложь (Д7): 5013 недостижим, не переносится.
			if (Rec->EnchantOrSocket == 4) { Notice(5015, kColDefault); return false; }
		}
		return true;
	}
	const uint8* R = ListRec(0, SelSlotIndex);
	const int32 Code = RCode(R);
	if (IT->IsSowelCode(Code)) { return true; }
	const FAsdaItemRecord* Rec = IT->GetItem(Code);
	if (Rec && Rec->ItemCode > 0)
	{
		if (Rec->ItemSubType == 32 && RB50(R) == 3) { Notice(5066, kColDefault); return false; }
		if (Rec->EnchantOrSocket < 4) { Notice(5014, kColDefault); return false; }
	}
	return true;
}

int32 UAsdaExchangeP2PSubsystem::DragGate(int32 IdNum)
{
	// `notifyHandleOpcodeRange7DD@0xC722D0` (shop_trade.c:6906-6940): при моей готовности (+0x4C4) — 5045 и 1 (перенос не
	//   начат); иначе +0x24 = 1, источник +0x26D4 = 0 (кандидат) / 1 (лоток), 2 — перенос начат.
	const bool bList = (IdNum >= kCtrlList && IdNum < kCtrlTray);
	const bool bTray = (IdNum >= kCtrlTray && IdNum < kCtrlPartner);
	if (!bList && !bTray) { return 1; }
	if (bMyLock == 1) { Notice(5045, kColDefault); return 1; }
	bDragging = 1;
	SelSource = bList ? 0 : 1;
	return 2;
}

bool UAsdaExchangeP2PSubsystem::OnDragBegin(int32 IdNum)
{
	// 5: `if (isActive != 1) return; if (!itemEquipValidate) return; notifyHandleOpcodeRange7DD`.
	if (bCellTaken != 1) { return false; }
	if (!EquipValidate(IdNum)) { return false; }
	if (DragGate(IdNum) != 2) { return false; }
	DragSrcIdNum = IdNum;
	// Призрак — общая точка (эталон рисует план у курсора заливкой 5), исходная ячейка — `RefreshSlotFillText` (6).
	const int32 i = IdNum - kCtrlList;
	UImage* Src = CellImg.IsValidIndex(i) ? CellImg[i].Get() : nullptr;
	if (Src && WinWidget && WinWidget->WidgetTree)
	{
		if (UCanvasPanel* Root = Cast<UCanvasPanel>(WinWidget->WidgetTree->RootWidget))
		{
			DragGhost.Begin(WinWidget, Root, Src->GetBrush(), FVector2D(32.f, 32.f), kDragAlpha, FVector2D(-16.f, -16.f),
				TEXT("TRAN_DragGhost"), GetWorld());
		}
	}
	TickTooltip(-1);   // подсказку на время переноса гасим (гейт `helper_getField1` эталона)
	RefreshSlotFillText();
	return true;
}

void UAsdaExchangeP2PSubsystem::OnDrop(int32 TargetIdNum)
{
	// 7: `if (!bBusy && isActive == 1) exchangeWnd_HandleItemSlotClick(this, a2, a6)` (a6 — контрол под курсором).
	CancelDragVisual();
	if (bMyLock || bCellTaken != 1)
	{
		// Бросок не дошёл до обработчика — вернуть ячейку на место (эталон её и не приглушал бы без 6).
		SetCellFill(ListCtrl(SelListCell), 0);
		SetCellFill(TrayCtrl(SelTrayCell), 0);
		return;
	}
	// `exchangeWnd_HandleItemSlotClick@0xC72520` (shop_trade.c:6959-7126).
	if (TargetIdNum < 0) { RefreshSlotText(); return; }   // `if (!a3) uiInventoryRefreshSlotText`
	const int32 F = TargetIdNum;   // `helper_getField91(a3)` — IDNum контрола под курсором
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { RefreshSlotText(); return; }
	if (SelSource != 0)
	{
		if (SelSource != 1) { return; }
		// Источник — мой лоток: бросок на кандидатов — снять (5290), иначе вернуть.
		if (F >= kCtrlList && F < kCtrlTray)
		{
			RemoveTrayCell();
			ResetSelectionAndRefresh();
			RefreshTrayIcons();
			return;
		}
		RefreshSlotText();
		return;
	}
	if ((F >= kCtrlList && F < kCtrlTray) || F < kCtrlTray || F >= kCtrlPartner) { RefreshSlotText(); return; }   // LABEL_57
	if (ExchangeType != 0)
	{
		const uint8* R = ListRec(1, SelSlotIndex);
		if (!ItemRecordExists(RCode(R), RB50(R)))
		{
			RefreshSlotText();
			Notice(5066, kColDefault);
			return;
		}
		if (!HasFreeTraySlot())
		{
			Notice(5031, kColDefault);   // LABEL_32
			RefreshSlotText();
			bCellTaken = 0; bDragging = 0;
			return;
		}
		const FAsdaItemRecord* Rec = IT->GetItem(RCode(R));
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = IT->GetSpecialItem(RCode(R), Sp);
		if (Rec)
		{
			if (Rec->ItemCode > 0)
			{
				if (Rec->ItemCode == kGoldCode)
				{
					if (RCount(R) < 2)
					{
						Notice(5070, kColDefault);
						RefreshSlotText();
						bCellTaken = 0; bDragging = 0;
					}
					else { OpenNumPad(RCount(R)); }
				}
				else
				{
					AllocSlotRecord();
					if (!SendAddItem(3)) { RollbackUnsentAdd(3, TEXT("5288 (AddItem3) не ушёл")); }
				}
			}
		}
		else if (bSp && Sp.Code > 0)
		{
			AllocSlotRecord();
			if (!SendAddItem(3)) { RollbackUnsentAdd(3, TEXT("5288 (AddItem3) не ушёл")); }
		}
		return;
	}
	if (!HasFreeTraySlot())
	{
		Notice(5031, kColDefault);
		RefreshSlotText();
		bCellTaken = 0; bDragging = 0;
		return;
	}
	const uint8* R = ListRec(0, SelSlotIndex);
	const int32 Code = RCode(R);
	if (SelectedMatchesSlot())
	{
		if (IsSlotItemTypeState(SelSlotIndex)) { Notice(5066, kColStack); return; }
		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		if (Rec && Rec->ItemCode > 0)
		{
			if (RCount(R) >= 2) { OpenNumPad(RCount(R)); return; }   // LABEL_15
			SelectedQty = 1;
			PlaneRecallStoredCard();
			if (!SendAddItem(2)) { RollbackUnsentAdd(2, TEXT("5288 (AddItem2) не ушёл")); }
		}
		return;
	}
	if (!IT->IsSowelCode(Code))
	{
		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		if (Rec)
		{
			if (Rec->ItemCode > 0)
			{
				if (Rec->FlagWord176)
				{
					PlaneMoveCardToEmptySlot();
					if (!SendAddItem(1)) { RollbackUnsentAdd(1, TEXT("5288 (AddItem) не ушёл")); }
				}
				else
				{
					if (RCount(R) >= 2) { OpenNumPad(RCount(R)); return; }
					SelectedQty = 1;
					PlaneSlotActivateFree();
					if (!SendAddItem(1)) { RollbackUnsentAdd(1, TEXT("5288 (AddItem) не ушёл")); }
				}
			}
		}
		else
		{
			RefreshSlotText();
			bCellTaken = 0; bDragging = 0;
		}
		return;
	}
	FAsdaSowellRow Sw;
	if (IT->GetSowell(Code, Sw) && Code > 0)
	{
		SlotSwapIntoFreeSlot();
		if (!SendAddItem(1)) { RollbackUnsentAdd(1, TEXT("5288 (AddItem, самоцвет) не ушёл")); }
	}
}

void UAsdaExchangeP2PSubsystem::OnCellDoubleClick(int32 IdNum)
{
	// 0xD: `if (isActive == 1) exchangeWndOnAddItem(this, a2)` (shop_trade.c:6735-6904).
	if (bCellTaken != 1) { return; }
	if (bMyLock == 1) { return; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	if (IdNum >= kCtrlList && IdNum < kCtrlTray)
	{
		if (!ListIndexValid((IdNum - kCtrlList) + kCols * ScrollRow)) { return; }
		if (ExchangeType != 0)
		{
			const uint8* R = ListRec(1, SelSlotIndex);
			if (!ItemRecordExists(RCode(R), RB50(R))) { Notice(5066, kColDefault); return; }
			if (!HasFreeTraySlot()) { Notice(5031, kColDefault); return; }
			if (RCode(R) <= 0) { return; }
			const FAsdaItemRecord* Rec = IT->GetItem(RCode(R));
			FAsdaSpecialItemlistRow Sp;
			const bool bSp = IT->GetSpecialItem(RCode(R), Sp);
			if (Rec)
			{
				if (Rec->ItemCode > 0)
				{
					if (Rec->ItemCode == kGoldCode)
					{
						if (RCount(R) < 2) { Notice(5070, kColDefault); return; }
						SelSource = 0;
						OpenNumPad(RCount(R));
						return;
					}
					SelSource = 0;
					AllocSlotRecord();
					if (!SendAddItem(3)) { RollbackUnsentAdd(3, TEXT("5288 (AddItem3) не ушёл")); }
				}
			}
			else if (bSp && Sp.Code > 0)
			{
				SelSource = 0;
				AllocSlotRecord();
				if (!SendAddItem(3)) { RollbackUnsentAdd(3, TEXT("5288 (AddItem3) не ушёл")); }
			}
			return;
		}
		if (!HasFreeTraySlot()) { Notice(5031, kColDefault); return; }
		const uint8* R = ListRec(0, SelSlotIndex);
		const int32 Code = RCode(R);
		const FAsdaItemRecord* Rec = nullptr;
		if (SelectedMatchesSlot())
		{
			if (IsSlotItemTypeState(SelSlotIndex)) { Notice(5066, kColStack); return; }
			SelSource = 0;
			Rec = IT->GetItem(Code);
			if (Rec && Rec->ItemCode > 0)
			{
				if (RCount(R) < 2)
				{
					SelectedQty = 1;
					PlaneRecallStoredCard();
					if (!SendAddItem(2)) { RollbackUnsentAdd(2, TEXT("5288 (AddItem2) не ушёл")); }
				}
				else
				{
					SelSource = 0;
					OpenNumPad(RCount(R));
				}
				return;
			}
		}
		if (!IT->IsSowelCode(Code))
		{
			if (Code > 0) { Rec = IT->GetItem(Code); }
		}
		else
		{
			FAsdaSowellRow Sw;
			if (IT->GetSowell(Code, Sw) && Code > 0)
			{
				SelSource = 0;
				SlotSwapIntoFreeSlot();
				if (!SendAddItem(1)) { RollbackUnsentAdd(1, TEXT("5288 (AddItem, самоцвет) не ушёл")); }
				return;
			}
		}
		if (!Rec || Rec->ItemCode <= 0) { return; }
		if (Rec->FlagWord176)
		{
			SelSource = 0;
			PlaneMoveCardToEmptySlot();
			if (!SendAddItem(1)) { RollbackUnsentAdd(1, TEXT("5288 (AddItem) не ушёл")); }
			return;
		}
		if (RCount(R) < 2)
		{
			SelSource = 0;
			SelectedQty = 1;
			PlaneSlotActivateFree();
			if (!SendAddItem(1)) { RollbackUnsentAdd(1, TEXT("5288 (AddItem) не ушёл")); }
			return;
		}
		SelSource = 0;
		OpenNumPad(RCount(R));
		return;
	}
	if (IdNum < kCtrlTray || IdNum >= kCtrlPartner) { return; }
	// Ячейка лотка → снять (5290): `+0x26D4 = 1` + возврат записи в список + отправка по +0x26C4.
	SelSource = 1;
	RemoveTrayCell();
	ResetSelectionAndRefresh();
	RefreshTrayIcons();
}

void UAsdaExchangeP2PSubsystem::OnWheel(int32 Notches)
{
	// 23 → `itemListWinHandleMsg2051@0xC707F0`: +0x26B4 = позиция, перерисовать страницу, рамку 2047 спрятать. Диапазон
	//   `uiControlSetScrollRange(2051, 4, 10 / 6)` — 4 видимых ряда из 10 (тип 0) / 6 (тип 1) ⇒ позиции 0…6 / 0…2.
	if (!bWinShown) { return; }
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): рядов типа 1 = ceil((сумка + золото) / 6) — эталон 6 при сумке 30, у нас 11 при 60.
	const int32 Type1Rows = FMath::DivideAndRoundUp(UAsdaInventorySubsystem::GetShellCapacity() + 1, kCols);
	const int32 MaxRow = (ExchangeType != 0 ? Type1Rows : 10) - 4;
	const int32 NewRow = FMath::Clamp(ScrollRow - Notches, 0, MaxRow);
	if (NewRow == ScrollRow) { return; }
	ScrollRow = NewRow;
	RefreshItemPage(ScrollRow);
	if (TraySel) { PlaneHide(TraySel); }
}

// ═════════════════════════════ ГОТОВНОСТЬ 5291 / 5292 И ИТОГ 5293 (волна О3) ══════════════════════
void UAsdaExchangeP2PSubsystem::OnMyLockChecked(bool bChecked)
{
	// Латч +609 (`sub_9350A2` ниже): `checkBox_OnToggle@0x12FDF70` при взведённом флаге НЕ меняет кадр и НЕ шлёт 0x10601/0x10602.
	//   Флажок UE переключился сам — возвращаем галку к модели (тот же приём, что у запертых кнопок системного меню).
	if (bMyLockLatch)
	{
		SetCheckState(2048, bMyLock == 1);
		UE_LOG(LogAsdaP2PWin, Verbose, TEXT("[обмен] 2048 заперт (латч +609) — событие не идёт, галка по модели"));
		return;
	}
	// `exchangeOnLockToggleMsg@0xC78940` (shop_trade.c:7544-7568), a2 = 2048, a3 = 1 (0x10601) / 0 (0x10602).
	if (bMyLock)
	{
		SetCheckState(2048, true);   // `CControl_SetVisibleState(2048, 1)` — готовность уже взведена, галка держится
		return;
	}
	bMyLock = 1;                     // +0x4C4 = 1
	SetCheckState(2048, bChecked);   // `CControl_SetVisibleState(2048, a3 != 0)`
	bMyLockLatch = true;             // `sub_9350A2(2048)` — латч +609: флажок больше не принимает клик
	if (!SendLockToggle()) { RollbackUnsentLock(TEXT("5291 не ушёл")); return; }
	if (ReadyCounter == 2 && !bCompleteSent)
	{
		Notice(5046, kColDefault);   // «Подготовка к обмену завершена.»
		Notice(5069, kColDefault);   // «Нажми кнопку 'Обмен' для завершения торгов»
	}
}

bool UAsdaExchangeP2PSubsystem::SendLockToggle()
{
	// `Exchange_SendLockToggle@0xC81990` (shop_trade.c:9782-10084). Ветка взвода: счётчик +0x26E0 (< 2) ++, при 2 — 2011 виден,
	//   +0x0C = 0; ветка снятия (у эталона недостижима — единственный вызов после +0x4C4 = 1): счётчик−−, 2011 скрыт, +0x0C = 1.
	//   Тело: windowIndex, +0x4C4, 5 записей лотка — у ячейки с записью item_list / самоцвета / второй таблицы: кол-во +9,
	//   контейнер +4, ячейка +5; иначе 0, 0, −1.
	if (bMyLock)
	{
		// ★ Ревизия О3 (МЕЛК-3): [ПОРТ] запоминаем, увеличил ли МОЙ взвод счётчик (при 2 — нет: дубль 5292 lock 1 партнёра, сток
		//   повторную фиксацию не запрещает) — откат (`RollbackUnsentLock`, Д21) снимает только собственный инкремент.
		bLockCounted = ReadyCounter < 2;
		if (ReadyCounter < 2) { ++ReadyCounter; }
		if (ReadyCounter == 2) { SetOkButtonShown(true); }
		bLockOff = 0;
	}
	else
	{
		if (ReadyCounter) { --ReadyCounter; }
		SetOkButtonShown(false);
		bLockOff = 1;
	}
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	int32 Types[kTray], Slots[kTray], Counts[kTray];
	for (int32 k = 0; k < kTray; ++k)
	{
		const uint8* T = TrayRec(k);
		const int32 Code = RCode(T);
		FAsdaSowellRow Sw; FAsdaSpecialItemlistRow Sp;
		const bool bRec = IT && T && (IT->GetItem(Code) || IT->GetSowell(Code, Sw) || IT->GetSpecialItem(Code, Sp));
		Types[k]  = bRec ? RType(T) : 0;
		Slots[k]  = bRec ? RSlot(T) : -1;
		Counts[k] = bRec ? RCount(T) : 0;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendExchangeFix5291(WindowIndex, (uint8)bMyLock, Types, Slots, Counts);
	UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] 5291 (готовность %d): окно #%d, счётчик %d — %s"), bMyLock, WindowIndex, ReadyCounter,
		bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

void UAsdaExchangeP2PSubsystem::RollbackUnsentLock(const TCHAR* Why)
{
	// [ПОРТ] Эталон отправку не проваливает. У порта 5291 не уходит при гейте «души» (`kSoulGatedOpcodes`) или без связи — сервер
	//   готовность не взвёл и партнёру 5292 не пошлёт. Защёлкивать готовность без пакета нельзя (no-state-latch-before-shown):
	//   возвращаем модель и флажок к «не готов» — счётчик назад, 2011 по счётчику, +0x0C = 1, латч +609 снят.
	bMyLock = 0;
	if (bLockCounted && ReadyCounter) { --ReadyCounter; }   // ревизия О3 (МЕЛК-3): только собственный инкремент `SendLockToggle`
	bLockCounted = false;
	SetOkButtonShown(ReadyCounter == 2);
	bLockOff = 1;
	bMyLockLatch = false;
	SetCheckState(2048, false);
	UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] %s — готовность не взведена, флажок 2048 снова свободен (счётчик %d)"), Why, ReadyCounter);
}

void UAsdaExchangeP2PSubsystem::RollbackRefusedLock(int32 Act)
{
	// ★ [ПОРТ, Д21] (ревизия О3, СРЕДН-1; реестр О3-1). Эталон на отказ фиксации 5292 [4] = 0/2 только печатает 5071/5073: +0x4C4,
	//   латч +609, счётчик и +0x28 остаются. Сервер оба отказа возвращает ДО `+657/+658 = 1` (GameServer.c:89183, :89239, :89302) —
	//   фиксации у него нет, а партнёр 5292 lock 1 не получил (счётчик у него застрял на 1). У отказанного же при готовности
	//   партнёра появлялись 2011 и 5046/5069, а итог не проводился ни стоком, ни P44 — тупик до «Отмены».
	//   Откат готовности согласован с сервером: +0x4C4 = 0, свой инкремент счётчика назад, латч снят, 2048 свободна, 2011 по
	//   счётчику, +0x0C = 1 — тем же путём, что неотправленный 5291. Игрок правит лоток и взводит готовность заново.
	//   ⚠ Если 5293 уже ушёл (+0x28 = 1) — НЕ откатываем: сервер хранит моё подтверждение +659/+660, снять его протоколом нельзя;
	//   после отката можно было бы сменить лоток, взвести заново, и первый 5293 партнёра провёл бы обмен по старому подтверждению на
	//   новый лоток. Ключ 0 — как эталон.
	if (!FixDefects() || !bMyLock) { return; }
	if (bCompleteSent)
	{
		UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] [ПОРТ, Д21] отказ фиксации (5292 [4]=%d) после 5293 — готовность не откатывается (подтверждение на сервере)"), Act);
		return;
	}
	RollbackUnsentLock(*FString::Printf(TEXT("[ПОРТ, Д21] отказ фиксации 5292 [4]=%d"), Act));
}

void UAsdaExchangeP2PSubsystem::OnOkClicked()
{
	// `exchangeUiOnCommand` 0x7DB → LABEL_72 (shop_trade.c:6452-6458): `if (+0x26E0 == 2 && !+0x28) { +0x28 = 1; SendConfirm; }`.
	if (ReadyCounter != 2 || bCompleteSent)
	{
		UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] 2011: счётчик %d, +0x28=%d — 5293 не шлётся (LABEL_72)"), ReadyCounter, bCompleteSent);
		return;
	}
	bCompleteSent = 1;
	if (!SendComplete())
	{
		// [ПОРТ] 5293 не ушёл (душа/нет связи): «отправлен» не защёлкиваем — повторный клик по 2011 пошлёт его снова.
		bCompleteSent = 0;
		UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] 2011: 5293 не ушёл — +0x28 не взведён"));
	}
}

bool UAsdaExchangeP2PSubsystem::SendComplete()
{
	// `Exchange_SendComplete@0xC82A80` (shop_trade.c:10088-10213): 4 байта — windowIndex.
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendExchangeComplete5293(WindowIndex);
	UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] 5293: окно #%d — %s"), WindowIndex, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

void UAsdaExchangeP2PSubsystem::Recv5292(const TArray<uint8>& Body)
{
	// `Recv_ExchangeLockState@0xC85000` (network.c:14533-14583): `memcpy(pkt, Src + 9, 0x105)` — [0..3] не читается, [4] действие,
	//   [5] lock партнёра, [6..260] 5 × 51 записей его лотка.
	if (Body.Num() < 6 + kTray * kPartnerRec)
	{
		UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] 5292: тело %d б < 261 — пропуск"), Body.Num());
		return;
	}
	const int32 Act = Body[4];
	const int32 Lock = Body[5];
	UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] 5292: действие %d, lock %d (счётчик %d, +0x28=%d)"), Act, Lock, ReadyCounter, bCompleteSent);
	if (Act == 0)
	{
		// Сервер: «Money Amout Error» (наличных ≤ отдаваемого золота) или отказ индекса окна — в т. ч. P44 «в полёте» (тот же путь
		//   `iWindowIndex < 0`, GameServer.c:89302-89306); пакет идёт ОТПРАВИТЕЛЮ фиксации. Эталон только печатает 5071.
		Notice(5071, kColLock);
		RollbackRefusedLock(Act);   // [ПОРТ, Д21]
		return;
	}
	if (Act == 2)
	{
		// Сервер: записи 5291 не совпали с лотком окна или вещи в инвентаре меньше (GameServer.c:89160-89185, LABEL_23).
		Notice(5073, kColLock);
		RollbackRefusedLock(Act);   // [ПОРТ, Д21]
		return;
	}
	if (Lock)
	{
		// lock = 1: 5 записей партнёра → +0x3C5, счётчик++ (< 2), `CraftSlots_RefreshIcons(1)` (2049, 2011 при 2, иконки).
		if (PartnerTray.Num() < kTray * kPartnerRec) { PartnerTray.Init(0, kTray * kPartnerRec); }
		FMemory::Memcpy(PartnerTray.GetData(), Body.GetData() + 6, kTray * kPartnerRec);
		if (ReadyCounter < 2) { ++ReadyCounter; }
		RefreshPartnerTray(true);
		if (ReadyCounter == 2 && !bCompleteSent)
		{
			Notice(5046, kColDefault);
			Notice(5069, kColDefault);
		}
		return;
	}
	// lock = 0 (партнёр снял готовность; штатный клиент его не шлёт): счётчик−−, `CraftSlots_RefreshIcons(0)`, +0x28 = 0.
	if (ReadyCounter) { --ReadyCounter; }
	RefreshPartnerTray(false);
	bCompleteSent = 0;
}

// ═════════════════════════════ ЛОТОК: ОПЕРАЦИИ ЭТАЛОНА ═════════════════════════════════════════

bool UAsdaExchangeP2PSubsystem::HasFreeTraySlot() const
{
	// `hasFreeQuickSlot@0xC72D60`: занятых (код > 0) меньше 5.
	int32 Busy = 0;
	for (int32 k = 0; k < kTray; ++k) { if (RCode(TrayRec(k)) > 0) { ++Busy; } }
	return Busy < kTray;
}

bool UAsdaExchangeP2PSubsystem::ItemRecordExists(int32 Code, int32 B50) const
{
	// `isItemRecordExists@0xC86D70`: запись item_list и `a2 == 0` → 1; иначе запись второй таблицы → 1; иначе 0.
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	if (IT->GetItem(Code)) { return B50 == 0; }
	FAsdaSpecialItemlistRow Sp;
	return IT->GetSpecialItem(Code, Sp);
}

bool UAsdaExchangeP2PSubsystem::IsSlotItemTypeState(int32 Index) const
{
	// `isSlotItemTypeState@0xC86E00`: `+0x26D0 < 0` → 0; иначе item_list subType 32 и +50 == 3 у записи `Index` списка типа 0.
	if (SelSlotIndex < 0) { return false; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const uint8* R = ListRec(0, Index);
	const FAsdaItemRecord* Rec = (IT && R) ? IT->GetItem(RCode(R)) : nullptr;
	return Rec && Rec->ItemSubType == 32 && RB50(R) == 3;
}

bool UAsdaExchangeP2PSubsystem::SelectedMatchesSlot() const
{
	// `cItemListWinSelectedMatchesSlot@0xC7B300`: тот же код, ячейка (+5), контейнер (+4) в лотке и кол-во кандидата > 0.
	const uint8* R = ListRec(0, SelSlotIndex);
	if (!R) { return false; }
	for (int32 k = 0; k < kTray; ++k)
	{
		const uint8* T = TrayRec(k);
		if (RCode(R) == RCode(T) && RSlot(R) == RSlot(T) && RType(R) == RType(T) && RCount(R) > 0) { return true; }
	}
	return false;
}

bool UAsdaExchangeP2PSubsystem::SelectedIsGoldInTray() const
{
	// `cItemListWinSelectedIsSpecialItem@0xC7B1C0` (МИСНЕЙМ): кандидат типа 1 уже лежит в лотке, кол-во > 0 и это золото.
	const uint8* R = ListRec(1, SelSlotIndex);
	if (!R) { return false; }
	for (int32 k = 0; k < kTray; ++k)
	{
		const uint8* T = TrayRec(k);
		if (RCode(R) == RCode(T) && RSlot(R) == RSlot(T) && RType(R) == RType(T) && RCount(R) > 0 && RCode(R) == kGoldCode) { return true; }
	}
	return false;
}

bool UAsdaExchangeP2PSubsystem::SelectedIsAllGold() const
{
	// Тип 1 — встроенная проверка `exchange_AddSelectedItem`; тип 0 — `sub_C7B410`: золото и выбрано ВСЁ (кол-во == +9).
	const uint8* R = ListRec(ExchangeType != 0 ? 1 : 0, SelSlotIndex);
	return R && RCode(R) == kGoldCode && SelectedQty == RCount(R);
}

void UAsdaExchangeP2PSubsystem::AllocSlotRecord()
{
	// `allocSlotRecord@0xC739A0`: первая пустая ячейка лотка ← вся запись кандидата; копия + индекс; кандидат обнулён.
	uint8* Src = ListRec(1, SelSlotIndex);
	if (!Src) { return; }
	for (int32 k = 0; k < kTray; ++k)
	{
		if (RCode(TrayRec(k)) > 0) { continue; }
		ActiveSlotIndex = k;
		FMemory::Memcpy(TrayRec(k), Src, kRec);
		FMemory::Memcpy(CopyRec(k), TrayRec(k), kRec);
		AsdaBytes::WrI32(CopyRec(k) + 60, SelSlotIndex);
		FMemory::Memzero(Src, kRec);
		SetCellFill(ListCtrl(SelListCell), 0);
		return;
	}
}

void UAsdaExchangeP2PSubsystem::PlaneRecallStoredCard()
{
	// `plane_RecallStoredCard@0xC73790`: тот же стак в лотке — кол-во += +0x26D8, кандидат обнулён, копия = нули + индекс.
	uint8* Src = ListRec(0, SelSlotIndex);
	if (!Src) { return; }
	for (int32 k = 0; k < kTray; ++k)
	{
		uint8* T = TrayRec(k);
		if (!(RCode(Src) == RCode(T) && RSlot(Src) == RSlot(T) && RType(Src) == RType(T))) { continue; }
		ActiveSlotIndex = k;
		WCount(T, RCount(T) + SelectedQty);
		FMemory::Memzero(Src, kRec);
		SetCellFill(ListCtrl(SelListCell), 0);
		FMemory::Memcpy(CopyRec(k), Src, kRec);   // эталон копирует УЖЕ обнулённого кандидата
		AsdaBytes::WrI32(CopyRec(k) + 60, SelSlotIndex);
		return;
	}
}

void UAsdaExchangeP2PSubsystem::SlotSwapIntoFreeSlot()
{
	// `slotSwapIntoFreeSlot@0xC73180`: самоцвет целиком — в первую пустую ячейку, +0x26D8 = кол-во кандидата.
	uint8* Src = ListRec(0, SelSlotIndex);
	if (!Src) { return; }
	for (int32 k = 0; k < kTray; ++k)
	{
		if (RCode(TrayRec(k)) > 0) { continue; }
		ActiveSlotIndex = k;
		FMemory::Memcpy(TrayRec(k), Src, kRec);
		FMemory::Memcpy(CopyRec(k), TrayRec(k), kRec);
		AsdaBytes::WrI32(CopyRec(k) + 60, SelSlotIndex);
		SelectedQty = RCount(Src);
		FMemory::Memzero(Src, kRec);
		SetCellFill(ListCtrl(SelListCell), 0);
		return;
	}
}

void UAsdaExchangeP2PSubsystem::PlaneMoveCardToEmptySlot()
{
	// `planeMoveCardToEmptySlot@0xC73320` — тело то же, что у `slotSwapIntoFreeSlot` (нештучный `flagWord176` целиком).
	//   ★ R5-b 12.11.2026: обе эталонные функции остаются (1:1 с местами вызова), тело одно — у `SlotSwapIntoFreeSlot`.
	SlotSwapIntoFreeSlot();
}

void UAsdaExchangeP2PSubsystem::PlaneSlotActivateFree()
{
	// `planeSlotActivateFree@0xC73010`: 1 шт. — вся запись в пустую ячейку, кандидат обнулён (+0x26D8 = 1 выставил вызывающий).
	uint8* Src = ListRec(0, SelSlotIndex);
	if (!Src) { return; }
	for (int32 k = 0; k < kTray; ++k)
	{
		if (RCode(TrayRec(k)) > 0) { continue; }
		ActiveSlotIndex = k;
		FMemory::Memcpy(TrayRec(k), Src, kRec);
		FMemory::Memcpy(CopyRec(k), TrayRec(k), kRec);
		AsdaBytes::WrI32(CopyRec(k) + 60, SelSlotIndex);
		FMemory::Memzero(Src, kRec);
		SetCellFill(ListCtrl(SelListCell), 0);
		return;
	}
}

void UAsdaExchangeP2PSubsystem::CraftMatchSlotByItem()
{
	// `craftMatchSlotByItem@0xC73CE0` (тип 1, докладка золота): тот же предмет в лотке — копия, и при кол-ве меньше стопки
	//   лоток += +0x26D8, кандидат −= +0x26D8.
	uint8* Src = ListRec(1, SelSlotIndex);
	if (!Src) { return; }
	for (int32 k = 0; k < kTray; ++k)
	{
		uint8* T = TrayRec(k);
		if (!(RCode(Src) == RCode(T) && RSlot(Src) == RSlot(T) && RType(Src) == RType(T))) { continue; }
		ActiveSlotIndex = k;
		FMemory::Memcpy(CopyRec(k), Src, kRec);
		AsdaBytes::WrI32(CopyRec(k) + 60, SelSlotIndex);
		if (SelectedQty < RCount(Src))
		{
			WCount(T, RCount(T) + SelectedQty);
			WCount(Src, RCount(Src) - SelectedQty);
		}
		SetCellFill(ListCtrl(SelListCell), 0);
		return;
	}
}

void UAsdaExchangeP2PSubsystem::CraftSlotsAssignFreeSlot()
{
	// `craftSlotsAssignFreeSlot@0xC73B10` (тип 1): в пустую ячейку — запись кандидата, копия + индекс; при кол-ве меньше
	//   стопки кандидат −= +0x26D8; кол-во в лотке = +0x26D8.
	uint8* Src = ListRec(1, SelSlotIndex);
	if (!Src) { return; }
	for (int32 k = 0; k < kTray; ++k)
	{
		if (RCode(TrayRec(k)) > 0) { continue; }
		ActiveSlotIndex = k;
		FMemory::Memcpy(TrayRec(k), Src, kRec);
		FMemory::Memcpy(CopyRec(k), TrayRec(k), kRec);
		AsdaBytes::WrI32(CopyRec(k) + 60, SelSlotIndex);
		if (SelectedQty < RCount(Src)) { WCount(Src, RCount(Src) - SelectedQty); }
		SetCellFill(ListCtrl(SelListCell), 0);
		WCount(TrayRec(k), SelectedQty);
		return;
	}
}

void UAsdaExchangeP2PSubsystem::PlaneStoreCardIntoSlot()
{
	// `plane_StoreCardIntoSlot@0xC734C0` (тип 0, докладка с нумпада): копия + индекс; кол-во == стопке — лоток += и кандидат
	//   обнулён; меньше — лоток +=, кандидат −=.
	uint8* Src = ListRec(0, SelSlotIndex);
	if (!Src) { return; }
	for (int32 k = 0; k < kTray; ++k)
	{
		uint8* T = TrayRec(k);
		if (!(RCode(Src) == RCode(T) && RSlot(Src) == RSlot(T) && RType(Src) == RType(T))) { continue; }
		ActiveSlotIndex = k;
		FMemory::Memcpy(CopyRec(k), Src, kRec);
		AsdaBytes::WrI32(CopyRec(k) + 60, SelSlotIndex);
		if (SelectedQty == RCount(Src))
		{
			WCount(T, RCount(T) + SelectedQty);
			FMemory::Memzero(Src, kRec);
		}
		else if (SelectedQty < RCount(Src))
		{
			WCount(T, RCount(T) + SelectedQty);
			WCount(Src, RCount(Src) - SelectedQty);
		}
		SetCellFill(ListCtrl(SelListCell), 0);
		return;
	}
}

void UAsdaExchangeP2PSubsystem::SlotActivateFreeWithData()
{
	// `slotActivateFreeWithData@0xC72DE0` (тип 0, кол-во с нумпада): в пустую ячейку — запись и копия + индекс; кол-во ==
	//   стопке — кандидат обнулён, меньше — кандидат −=; кол-во в лотке = +0x26D8.
	uint8* Src = ListRec(0, SelSlotIndex);
	if (!Src) { return; }
	for (int32 k = 0; k < kTray; ++k)
	{
		if (RCode(TrayRec(k)) > 0) { continue; }
		ActiveSlotIndex = k;
		FMemory::Memcpy(TrayRec(k), Src, kRec);
		FMemory::Memcpy(CopyRec(k), Src, kRec);
		AsdaBytes::WrI32(CopyRec(k) + 60, SelSlotIndex);
		if (SelectedQty == RCount(Src)) { FMemory::Memzero(Src, kRec); }
		else if (SelectedQty < RCount(Src)) { WCount(Src, RCount(Src) - SelectedQty); }
		SetCellFill(ListCtrl(SelListCell), 0);
		WCount(TrayRec(k), SelectedQty);
		return;
	}
}

void UAsdaExchangeP2PSubsystem::CommitFoundResource()
{
	// `airPlane_CommitFoundResource@0xC75190` (тип 0, снятие): ячейка лотка +0x26C4, индекс-источник из копии (@60, 0…59):
	//   запись кандидата пуста — вернуть запись лотка на место; иначе — найти в списке тот же код и ячейку и прибавить кол-во.
	const int32 K = SelTrayCell;
	uint8* C = CopyRec(K);
	if (!C) { return; }
	const int32 SrcIdx = AsdaBytes::RdI32(C + 60);
	if (SrcIdx < 0 || SrcIdx >= kList0Max) { return; }
	SetCellFill(kCtrlTray + K, 0);
	uint8* L = ListRec(0, SrcIdx);
	const uint8* T = TrayRec(K);
	if (RCode(L) <= 0)
	{
		FMemory::Memcpy(L, T, kRec);
		FMemory::Memzero(C, kCopyRec);
		return;
	}
	for (int32 i = 0; i < kList0Max; ++i)
	{
		uint8* Li = ListRec(0, i);
		if (RCode(T) == RCode(Li) && RSlot(T) == RSlot(Li))
		{
			WCount(Li, RCount(Li) + RCount(T));
			FMemory::Memzero(C, kCopyRec);
			return;
		}
	}
}

void UAsdaExchangeP2PSubsystem::ApplySelectedSlotItemCode()
{
	// `cInven_ApplySelectedSlotItemCode@0xC75400` (тип 1, снятие): индекс-источник из копии (0…30); у кандидата код > 0 и это
	//   золото — кол-во кандидата := фактический запас золота контейнера 2 (та же ячейка); иначе — вернуть запись лотка на место.
	const int32 K = SelTrayCell;
	uint8* C = CopyRec(K);
	if (!C) { return; }
	const int32 SrcIdx = AsdaBytes::RdI32(C + 60);
	if (SrcIdx < 0 || SrcIdx >= kList1Max) { return; }
	SetCellFill(kCtrlTray + K, 0);
	uint8* L = ListRec(1, SrcIdx);
	const uint8* T = TrayRec(K);
	if (RCode(L) > 0 && RCode(L) == kGoldCode)
	{
		const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		for (int32 i = 0; i < kList0Max && Inv; ++i)
		{
			const FAsdaItemInstance* Cell = Inv->GetCell(2, i);
			if (Cell && RCode(T) == Cell->ItemCode && RSlot(T) == Cell->SlotIndex)
			{
				WCount(L, Cell->Amount);
				FMemory::Memzero(C, kCopyRec);
				return;
			}
		}
		return;
	}
	FMemory::Memcpy(L, T, kRec);
	FMemory::Memzero(C, kCopyRec);
}

int32 UAsdaExchangeP2PSubsystem::SelListIndexForReset() const
{
	// [ПОРТ, Д16] индекс-источник из копии лотка (+0x168 + 64·k + 60); ключом 0 — как эталон, текущий выбор +0x26D0.
	//   [ПОРТ, Д18] при разборе 5289 — индекс, запомненный при отправке этого 5288 (копию могла переписать докладка).
	if (!FixDefects()) { return SelSlotIndex; }
	if (ReplySrcIdx >= 0) { return ReplySrcIdx; }
	const int32 K = ActiveSlotIndex;
	if (K < 0 || K >= kTray || TrayCopy.Num() < kTray * kCopyRec) { return SelSlotIndex; }
	return AsdaBytes::RdI32(TrayCopy.GetData() + kCopyRec * K + 60);
}

void UAsdaExchangeP2PSubsystem::ExchangeSlotReset()
{
	// `exchangeSlot_Reset@0xC7AC00` (shop_trade.c:7573-7604): откат ячейки +0x26DC после ошибки 5289.
	bStackAdd = 0;   // +0x30
	const int32 K = ActiveSlotIndex;
	uint8* T = TrayRec(K);
	uint8* C = CopyRec(K);
	const int32 Idx = SelListIndexForReset();
	auto Tail = [this]() { bDragging = 0; bCellTaken = 0; };
	if (!T || !C) { Tail(); return; }
	if (ExchangeType != 0)
	{
		uint8* L = ListRec(1, Idx);
		if (L)
		{
			FMemory::Memcpy(L, C, kRec);
			if (RCode(L) == RCode(T) && RSlot(L) == RSlot(T)) { FMemory::Memzero(T, kRec); }
		}
		FMemory::Memzero(C, kCopyRec);
		SetCellFill(ListCtrl(SelListCell), 0);
		Tail();
		return;
	}
	if (SlotAccum.IsValidIndex(K)) { SlotAccum[K] = 0; }
	uint8* L = ListRec(0, Idx);
	if (L && RCount(L) > 0)
	{
		WCount(L, RCount(L) + RCount(T));
		if (RCode(L) == RCode(T) && RSlot(L) == RSlot(T)) { FMemory::Memzero(T, kRec); }
		FMemory::Memzero(C, kCopyRec);
		SetCellFill(ListCtrl(SelListCell), 0);
		Tail();
		return;
	}
	if (L) { FMemory::Memcpy(L, T, kRec); }
	FMemory::Memzero(T, kRec);
	FMemory::Memzero(C, kCopyRec);
	SetCellFill(ListCtrl(SelListCell), 0);
	Tail();
}

void UAsdaExchangeP2PSubsystem::RollbackUnsentAdd(int32 Kind, const TCHAR* Why)
{
	// [ПОРТ] Эталон отправку не проваливает. У порта 5288 не уходит при гейте «души» (`kSoulGatedOpcodes`) или без связи — сервер
	//   о предмете не знает, 5289 не придёт. Новая ячейка (виды 1/3) возвращается в список тем же откатом, что после ошибки 5289;
	//   докладка (виды 2/4) — только на отправленное количество: подтверждённая сервером часть в лотке остаётся. 5290 не шлётся.
	if (Kind == 2 || Kind == 4)
	{
		uint8* T = TrayRec(ActiveSlotIndex);
		const int32 Idx = SelListIndexForReset();
		uint8* L = ListRec(Kind == 4 ? 1 : 0, Idx);
		if (T && L)
		{
			WCount(T, RCount(T) - SelectedQty);
			if (RCode(L) > 0) { WCount(L, RCount(L) + SelectedQty); }
			else { FMemory::Memcpy(L, T, kRec); WCount(L, SelectedQty); }
		}
		bStackAdd = 0; bDragging = 0; bCellTaken = 0;
		SetCellFill(ListCtrl(SelListCell), 0);
	}
	else
	{
		ExchangeSlotReset();
	}
	RefreshTrayIcons();
	ResetSelectionAndRefresh();
	UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] %s — лоток откатан локально (как после ошибки 5289, без 5290)"), Why);
}

void UAsdaExchangeP2PSubsystem::AddSelectedItem()
{
	// `exchange_AddSelectedItem@0xC7AFE0` (shop_trade.c:7606-7660): только если источник — список (+0x26D4 == 0).
	if (SelSource != 0) { return; }
	if (ExchangeType != 0)
	{
		if (SelectedIsAllGold()) { SelectedQty = 0; RefreshSlotText(); Notice(5039, kColDefault); return; }   // LABEL_4
		if (SelectedIsGoldInTray())
		{
			CraftMatchSlotByItem();
			if (!SendAddItem(4)) { RollbackUnsentAdd(4, TEXT("5288 (AddItem4) не ушёл")); }
		}
		else
		{
			CraftSlotsAssignFreeSlot();
			if (!SendAddItem(3)) { RollbackUnsentAdd(3, TEXT("5288 (AddItem3) не ушёл")); }
		}
		return;
	}
	if (SelectedIsAllGold()) { SelectedQty = 0; RefreshSlotText(); Notice(5039, kColDefault); return; }
	if (SelectedMatchesSlot())
	{
		PlaneStoreCardIntoSlot();
		if (!SendAddItem(2)) { RollbackUnsentAdd(2, TEXT("5288 (AddItem2) не ушёл")); }
	}
	else
	{
		SlotActivateFreeWithData();
		if (!SendAddItem(1)) { RollbackUnsentAdd(1, TEXT("5288 (AddItem) не ушёл")); }
	}
}

void UAsdaExchangeP2PSubsystem::OpenNumPad(int32 MaxQty)
{
	// `uiOpenContextPopupWithHandlers(g_pNumPadDialog, макс, 3, 0)` (shop_trade.c:6783/6827/6871/7036/7078). ОК →
	//   `uiExchangeConfirmEnteredQty` (ui_core.c:61950): 1 ≤ кол-во ≤ макс → +0x26D8 = кол-во + `exchange_AddSelectedItem`,
	//   иначе `uiInventoryRefreshSlotText`; отмена → `uiNumPadCloseRefreshExchange` (то же обновление). Проверку диапазона
	//   делает общая точка `ShowNumPad` (вне диапазона приходит как отмена).
	//   ★ Ревизия О2 (МЕЛК-5): `*(_DWORD *)&gap_26C0[20] = 0` перед нумпадом у эталона (только в 0xD, shop_trade.c:6782/6826/6870) —
	//   это +0x26D4 (источник = список), а не +0x26D8: его ставят вызывающие (`SelSource = 0`); кол-во здесь не трогается.
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	TWeakObjectPtr<UAsdaExchangeP2PSubsystem> WeakThis(this);
	WM->ShowNumPad(MaxQty, [WeakThis](bool bOk, int32 Qty)
	{
		UAsdaExchangeP2PSubsystem* S = WeakThis.Get();
		if (!S) { return; }
		UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] нумпад режим 3: %s, кол-во %d"), bOk ? TEXT("ОК") : TEXT("отмена/вне диапазона"), Qty);
		if (bOk && Qty > 0)
		{
			S->SelectedQty = Qty;   // `sub_9267EB(g_pExchangeWnd, qty)` = `set_o9B6` (+0x26D8)
			S->AddSelectedItem();
		}
		else
		{
			S->RefreshSlotText();
		}
	});
	UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] нумпад режим 3: максимум %d (кандидат #%d)"), MaxQty, SelSlotIndex);
}

// ═════════════════════════════ 5288 / 5290 / 5289 ══════════════════════════════════════════════

bool UAsdaExchangeP2PSubsystem::SendAddItem(int32 Kind)
{
	// 5288: `Exchange_SendAddItem` (1, тип 0, кол-во лотка), `…2` (2, тип 0, +0x26D8, +0x30 = 1), `…3` (3, тип 1, кол-во
	//   лотка), `…4` (4, тип 1, +0x26D8). Чужой тип: 1/2 → 5017, 3/4 → 5018 (тексты инвертированы, Д10) + откат.
	const bool bType0Kind = (Kind == 1 || Kind == 2);
	if (bType0Kind ? (ExchangeType != 0) : (ExchangeType != 1))
	{
		Notice(bType0Kind ? 5017 : 5018, kColDefault);
		ExchangeSlotReset();
		return true;   // эталонная ветка: отправки нет, откат уже сделан
	}
	const uint8* T = TrayRec(ActiveSlotIndex);
	if (!T) { return false; }
	const int32 Count = (Kind == 2 || Kind == 4) ? SelectedQty : RCount(T);
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendExchangeAddItem5288(WindowIndex, ActiveSlotIndex, RType(T), RSlot(T), Count);
	if (bOk && Kind == 2) { bStackAdd = 1; }   // `v21->priceAccumFlag = 1` — только после отправки
	if (bOk && FixDefects())
	{
		// [ПОРТ, Д18] запрос ждёт свой 5289: ячейка, вид, индекс-источник (копия лотка +60), кол-во, докладка.
		FPendingAdd& Q = AddQueue.AddDefaulted_GetRef();
		Q.Cell = ActiveSlotIndex;
		Q.Kind = Kind;
		const uint8* C = CopyRec(ActiveSlotIndex);
		Q.SrcIdx = C ? AsdaBytes::RdI32(C + 60) : SelSlotIndex;
		Q.Qty = SelectedQty;
		Q.bStack = (Kind == 2);
		Q.InvType = RType(T);   // ★ ревизия О3 (МЕЛК-1): эхо 5289 @1 — сверка ответа с запросом
		Q.InvSlot = RSlot(T);   // эхо 5289 @2
	}
	UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] 5288 (вид %d): окно #%d, ячейка %d, контейнер %d, слот %d, кол-во %d — %s"),
		Kind, WindowIndex, ActiveSlotIndex, RType(T), RSlot(T), Count, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

bool UAsdaExchangeP2PSubsystem::SendRemoveItem(int32 Kind)
{
	// 5290: `Exchange_SendRemoveItem` (1, тип 0) / `…2` (2, тип 1) — по ячейке +0x26C4 (обнуление ячейки и +0x10/+0x24 после
	//   отправки делает `RemoveTrayCell`); `…3` (3, тип 0) / `…4` (4, тип 1) — по +0x26DC, лоток не трогают (откат — `exchangeSlot_Reset`).
	const bool bType0Kind = (Kind == 1 || Kind == 3);
	if (bType0Kind ? (ExchangeType != 0) : (ExchangeType != 1)) { return false; }   // эталон молча ничего
	const int32 K = (Kind <= 2) ? SelTrayCell : ActiveSlotIndex;
	uint8* T = TrayRec(K);
	if (!T) { return false; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendExchangeRemoveItem5290(WindowIndex, RType(T), RSlot(T), RCount(T));
	UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] 5290 (вид %d): окно #%d, ячейка лотка %d, контейнер %d, слот %d, кол-во %d — %s"),
		Kind, WindowIndex, K, RType(T), RSlot(T), RCount(T), bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

void UAsdaExchangeP2PSubsystem::RemoveTrayCell()
{
	// Снятие ячейки +0x26C4 (0xD по лотку, бросок лотка на список): эталон — возврат записи в список (`cInven_ApplySelectedSlotItemCode`
	//   / `airPlane_CommitFoundResource`), затем `Exchange_SendRemoveItem2/…` — 5290 и обнуление ячейки лотка, +0x10 = +0x24 = 0.
	// ★ Ревизия О2 (МЕЛК-1): [ПОРТ] у эталона отправка не проваливается; у порта 5290 может не уйти (душа/нет связи) — тогда сервер
	//   предмет в лотке держит, и порт не меняет НИ лоток, НИ список. Поэтому отправка идёт ПЕРВОЙ: тело 5290 читает только ячейку
	//   лотка, которую возврат в список не трогает, — пакет тот же байт в байт.
	const bool bType1 = ExchangeType != 0;
	if (!SendRemoveItem(bType1 ? 2 : 1))
	{
		SetCellFill(TrayCtrl(SelTrayCell), 0);
		bCellTaken = 0; bDragging = 0;
		UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] 5290 не ушёл — ячейка лотка %d и список не изменены"), SelTrayCell);
		return;
	}
	if (bType1) { ApplySelectedSlotItemCode(); } else { CommitFoundResource(); }
	if (uint8* T = TrayRec(SelTrayCell)) { FMemory::Memzero(T, kRec); }
	bCellTaken = 0; bDragging = 0;
}

bool UAsdaExchangeP2PSubsystem::SendRemoveByMode()
{
	// `exchangeSendRemoveItemByMode@0xC86EA0`: тип 0 → `SendRemoveItem3`, тип 1 → `SendRemoveItem4`.
	if (ExchangeType == 0) { return SendRemoveItem(3); }
	if (ExchangeType == 1) { return SendRemoveItem(4); }
	return false;
}

void UAsdaExchangeP2PSubsystem::Recv5289(const TArray<uint8>& Body)
{
	// `Recv_Exchange_SlotUpdate@0xC84820` (network.c:14378-14510): [0] код, [1] invType, [2..3] invSlot, [4..7] кол-во.
	if (Body.Num() < 8)
	{
		UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] 5289: тело %d б < 8 — пропуск"), Body.Num());
		return;
	}
	const int32 Code = Body[0];
	const int32 InvType = Body[1];
	const int32 InvSlot = AsdaBytes::RdI16(Body.GetData() + 2);
	const int32 Count = AsdaBytes::RdI32(Body.GetData() + 4);
	// [ПОРТ, Д18] ответ относится к САМОМУ СТАРОМУ неотвеченному 5288 (TCP хранит порядок): ячейка +0x26DC, кол-во +0x26D8, признак
	//   докладки +0x30 и индекс-источник для отката берутся из головы очереди. Ключ 0 или пустая очередь — как эталон (текущие).
	ReplySrcIdx = -1; ReplyKind = 0;
	ON_SCOPE_EXIT { ReplySrcIdx = -1; ReplyKind = 0; };
	// ★ Ревизия О3 (МЕЛК-1): строгий FIFO ломался, если сервер МОЛЧА не ответил на один 5288 (гейт души `sub_52EEA0`, `sub_523F00`,
	//   тип 0 с кодом вне всех таблиц — `return` в конце `CharExchangeDragDropItem`): все следующие 5289 уходили к чужим записям.
	//   5289 несёт эхо запроса (`memcpy(v45, &dragReq[5])` до всех веток сервера): invType @1, invSlot @2. Берём ПЕРВУЮ запись очереди
	//   с той же парой; более старые (сервер на них промолчал) снимаются с Warning. Совпадений нет — как эталон (текущие поля).
	int32 QIdx = -1;
	for (int32 i = 0; FixDefects() && i < AddQueue.Num(); ++i)
	{
		if (AddQueue[i].InvType == InvType && AddQueue[i].InvSlot == InvSlot) { QIdx = i; break; }
	}
	if (QIdx > 0)
	{
		for (int32 i = 0; i < QIdx; ++i)
		{
			UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] [ПОРТ, Д18] 5289 (контейнер %d, слот %d) не относится к ожидающему 5288 ячейки %d (контейнер %d, слот %d) — сервер на него промолчал, запись снята"),
				InvType, InvSlot, AddQueue[i].Cell, AddQueue[i].InvType, AddQueue[i].InvSlot);
		}
		AddQueue.RemoveAt(0, QIdx);
		QIdx = 0;
	}
	if (FixDefects() && QIdx == 0)
	{
		const FPendingAdd Q = AddQueue[0];
		AddQueue.RemoveAt(0);
		if (Q.Cell != ActiveSlotIndex)
		{
			UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] [ПОРТ, Д18] 5289 — ответ на ячейку %d (текущая %d): берётся из очереди"), Q.Cell, ActiveSlotIndex);
		}
		ActiveSlotIndex = Q.Cell;
		SelectedQty = Q.Qty;
		bStackAdd = Q.bStack ? 1 : 0;
		ReplySrcIdx = Q.SrcIdx;
		ReplyKind = Q.Kind;
	}
	else if (FixDefects())
	{
		UE_LOG(LogAsdaP2PWin, Warning, TEXT("[обмен] 5289 (контейнер %d, слот %d) без ожидающего 5288 с этой парой (в очереди %d) — применяется к текущей ячейке %d (как эталон)"),
			InvType, InvSlot, AddQueue.Num(), ActiveSlotIndex);
	}
	bCellTaken = 0; bDragging = 0;   // `gap_0[16] = 0; gap_18[12] = 0`
	SetCellFill(ListCtrl(SelListCell), 0);   // `CPlane_SetFillMode(apTrayCtrl + selectedTraySlot, 0)`
	UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] 5289: код %d, контейнер %d, слот %d, кол-во %d (ячейка лотка %d)"), Code, InvType, InvSlot, Count, ActiveSlotIndex);
	static const int32 kErrLoc[11] = { 5019, 0, 5020, 5021, 5022, 5023, 5024, 5047, 5067, 5068, 97101 };
	if (Code == 1)
	{
		if (TraySel) { PlaneHide(TraySel); }   // `CControl_SetVisible(pTraySel, 0)`
		uint8* T = TrayRec(ActiveSlotIndex);
		const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		const FAsdaItemInstance* Cell = Inv ? Inv->GetCell(InvType, InvSlot) : nullptr;   // `inventoryGetSlotRecord`
		TArray<uint8> Rec; Rec.SetNumZeroed(kRec);
		if (Cell && Cell->Raw.Num() >= kRec) { FMemory::Memcpy(Rec.GetData(), Cell->Raw.GetData(), kRec); }
		if (T)
		{
			T[4] = (uint8)InvType;                                  // лоток[+4] = [1]
			AsdaBytes::WrI16(T + 5, InvSlot);                        // лоток[+5] = [2..3]
			AsdaBytes::WrI32(T, AsdaBytes::RdI32(Rec.GetData()));   // код — из записи инвентаря
			if (ExchangeType != 0)
			{
				T[24] = Rec[24];   // `gap_4A[57*k + 630] = itemRec[6]` (+24)
				T[13] = Rec[13];   // `gap_4A[57*k + 619] = BYTE1(itemRec[3])` (+13 прочность)
			}
			else if (SlotAccum.IsValidIndex(ActiveSlotIndex))
			{
				int32& Acc = SlotAccum[ActiveSlotIndex];
				if (Acc <= 0) { Acc = Count; } else { Acc += Count; }
				if (bStackAdd == 1)
				{
					// Д9 (не исправлено — истины нет): `if (tray.count == accum) tray.count = accum` — пустое присваивание.
					if (RCount(T) == Acc) { WCount(T, Acc); }
				}
				else
				{
					WCount(T, Count);
				}
				bStackAdd = 0;
			}
		}
		RefreshTrayIcons();
		ResetSelectionAndRefresh();
		return;
	}
	if (Code < 0 || Code > 10) { return; }   // default: ничего
	Notice(kErrLoc[Code], kColDefault);
	SendRemoveByMode();
	const int32 GoldIdx = SelListIndexForReset();
	ExchangeSlotReset();
	if (FixDefects() && ExchangeType != 0 && ReplyKind == 4)
	{
		// [ПОРТ, Д20] отказ на докладку золота (вид 4): 5290 вида 4 у сервера стёр ВСЮ ячейку, а копия лотка — это запись ДО
		//   докладки; ранее принятая часть пропала бы с экрана. Список := фактический запас золота контейнера 2 — тот же приём,
		//   что у снятия `cInven_ApplySelectedSlotItemCode` (item_inventory.c, тип 1, ветка 20551).
		uint8* L = ListRec(1, GoldIdx);
		const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		for (int32 i = 0; L && Inv && RCode(L) == kGoldCode && i < kList0Max; ++i)
		{
			const FAsdaItemInstance* Cell = Inv->GetCell(2, i);
			if (Cell && Cell->ItemCode == kGoldCode && Cell->SlotIndex == RSlot(L))
			{
				WCount(L, Cell->Amount);
				UE_LOG(LogAsdaP2PWin, Log, TEXT("[обмен] [ПОРТ, Д20] отказ докладки золота: в списке весь запас %d"), Cell->Amount);
				break;
			}
		}
	}
	RefreshTrayIcons();
}

// ═════════════════════════════ ДИАГНОСТИКА ═════════════════════════════════════════════════════

FString UAsdaExchangeP2PSubsystem::DebugCellText(int32 IdNum) const
{
	const int32 i = IdNum - kCtrlList;
	return (CellTxt.IsValidIndex(i) && CellTxt[i]) ? CellTxt[i]->GetText().ToString() : FString();
}

float UAsdaExchangeP2PSubsystem::DebugCellAlpha(int32 IdNum) const
{
	const int32 i = IdNum - kCtrlList;
	return (CellImg.IsValidIndex(i) && CellImg[i]) ? CellImg[i]->GetColorAndOpacity().A : -1.f;
}

bool UAsdaExchangeP2PSubsystem::DebugCellHasIcon(int32 IdNum) const
{
	const int32 i = IdNum - kCtrlList;
	const UImage* I = CellImg.IsValidIndex(i) ? CellImg[i].Get() : nullptr;
	return I && I->GetBrush().GetResourceObject() != nullptr && I->GetBrush().TintColor.GetSpecifiedColor().A > 0.f;
}

UWidget* UAsdaExchangeP2PSubsystem::DebugCellWidget(int32 IdNum) const
{
	const int32 i = IdNum - kCtrlList;
	return CellImg.IsValidIndex(i) ? CellImg[i].Get() : nullptr;
}

UWidget* UAsdaExchangeP2PSubsystem::DebugNamedWidget(const TCHAR* Name) const
{
	if (!WinWidget || !WinWidget->WidgetTree || !Name) { return nullptr; }
	return WinWidget->WidgetTree->FindWidget(FName(Name));
}

FString UAsdaExchangeP2PSubsystem::DebugWidgetText(const TCHAR* Name) const
{
	const UTextBlock* T = Cast<UTextBlock>(DebugNamedWidget(Name));
	return T ? T->GetText().ToString() : FString();
}

bool UAsdaExchangeP2PSubsystem::DebugIsShown(const TCHAR* Name) const
{
	// Видимость по цепочке родителей (свёрнутый/скрытый предок — узел не виден).
	for (const UWidget* W = DebugNamedWidget(Name); W; W = W->GetParent())
	{
		const ESlateVisibility V = W->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
	}
	return DebugNamedWidget(Name) != nullptr && bWinShown;
}

bool UAsdaExchangeP2PSubsystem::DebugIsChecked(int32 IdNum) const
{
	const UCheckBox* C = (IdNum == 2048) ? ChkMine.Get() : (IdNum == 2049 ? ChkPartner.Get() : nullptr);
	return C && C->IsChecked();
}

FString UAsdaExchangeP2PSubsystem::DebugDumpModel() const
{
	FString Tray;
	for (int32 k = 0; k < kTray; ++k)
	{
		const uint8* T = TrayRec(k);
		Tray += FString::Printf(TEXT("%s%d×%d"), k ? TEXT(",") : TEXT(""), RCode(T), RCount(T));
	}
	return FString::Printf(TEXT("окно2008=%d готово=%d кандидатов=%d прокрутка=%d выбор(ячейка %d, индекс %d, код %d, источник %d) "
		"лоток-ячейка=%d активная=%d кол-во=%d взята=%d перенос=%d лоток[%s] готовность(+4C4)=%d латч2048=%d счётчик=%d +28=%d "
		"ждут5289=%d"),
		bWinShown ? 1 : 0, bWinReady ? 1 : 0, ListCount, ScrollRow, SelListCell, SelSlotIndex, SelCode, SelSource,
		SelTrayCell, ActiveSlotIndex, SelectedQty, bCellTaken, bDragging, *Tray, bMyLock, bMyLockLatch ? 1 : 0, ReadyCounter,
		bCompleteSent, AddQueue.Num());
}
