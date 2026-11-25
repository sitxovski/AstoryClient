// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна «Передача параметров» (JUI 16000). Весь реверс с адресами — в шапке
//   UI/AsdaExtractShopSubsystem.h и в отчёте `Reports/npc_transfer_params_30035.md`.
#include "UI/AsdaExtractShopSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Npc/AsdaNpcDialogSubsystem.h"   // ★ волна В4 ремесла: выход из разговора при закрытии окна (5045)
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "AsdaDataTableCache.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Login/AsdaButtonRelay.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaExtract, Log, All);
// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): ёмкость половин окна = ёмкость сумки (как у остальных окон-потребителей).
static_assert(UAsdaExtractShopSubsystem::kGridCells == UAsdaInventorySubsystem::kShellSlots, "ёмкость списков окна разошлась с ёмкостью сумки");

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;

// ── Константы окна ──
static const FName  kExtractWindowId(TEXT("EXTRACT_WIN"));
static const TCHAR* kExtractWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXTRACT_WIN.WBP_EXTRACT_WIN_C");
static const TCHAR* kOpGraftDt = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_itemopgraft.DT_itemopgraft");

// ── Ключи отката дефектов оригинала (закон `fix-original-defects`) ──
static ASDA_DEV_CVAR(int32, CVarFixPageOnConfirm,
	TEXT("asda.Extract.FixPageOnConfirm"), 1,
	TEXT("1 (по умолчанию) — кнопка 16091 проверяет ИМЕННО выбранный предмет-источник.\n")
	TEXT("0 — дефект оригинала: проверки идут по предмету со страницы 0 того же ряда/колонки\n")
	TEXT("    (в эталоне терма 342*страница в этом чтении отсутствует, asm 0xC90312)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarClearStatusOnLeave,
	TEXT("asda.Extract.ClearStatusOnLeave"), 1,
	TEXT("1 (по умолчанию) — уход курсора с ячейки чистит строку-подсказку 16145.\n")
	TEXT("0 — как в оригинале: последний текст остаётся висеть."),
	ECVF_Default);

// ── Loc-идентификаторы. ВСЕ сняты с эталонных вызовов `UI_FormatLocalizedNotice`. ──
namespace ExtractLoc
{
	constexpr int32 kFrontTitle  = 13253;   // «Извлекаемый предмет»          (16030)
	constexpr int32 kBackTitle   = 13254;   // «Создаваемый предмет»          (16080)
	constexpr int32 kBtnRefresh  = 13255;   // «Повторить»                    (16090)
	constexpr int32 kBtnExtract  = 13256;   // «Настройки передачи»           (16091)
	constexpr int32 kBtnCancel   = 2289;    // «Отмена»                       (16092)
	constexpr int32 kCostLabel   = 13257;   // «Цена:»                        (16093)
	constexpr int32 kGoldLabel   = 12006;   // «Золото»                       (16140)
	constexpr int32 kGuide0      = 99900;   // 99900…99904 — пять строк правил (16380…16384)
	constexpr int32 kScrollTitle = 100200;  // «Выбери удачные улучшения»      (16411)
	constexpr int32 kScrollText  = 100201;  // «Использовать выбранный предмет» (16413)

	constexpr int32 kStatus0     = 13260;   // 13260…13271 — коды состояния 0…−11 (см. шапку .h)
	constexpr int32 kStatusOther = 100103;  // код −12 «Это другой тип оружия.»

	constexpr int32 kBusyWait    = 9419;    // «Подключение к серверу. Ожидается ответ.»
	constexpr int32 kNoGold      = 2050;    // «Недостаточно золота.»
	constexpr int32 kGemConfirm  = 99905;   // «…содержит живой камень. Он будет уничтожен. Продолжить?»
	constexpr int32 kBindConfirm = 109900;  // «Эта вещь не привязана к владельцу… Продолжить?»
	constexpr int32 kResult0     = 13800;   // 13800…13807 — коды ответа 0…7
	constexpr int32 kResultOther = 13808;   // «Не удалось переставить компоненты…»
	constexpr int32 kSuccess     = 13809;   // «Компоненты успешно переставлены.»
	constexpr int32 kFail        = 13810;   // «Перемещение не удалось. Попробуй еще раз.»
	constexpr int32 kBtnOk       = AsdaLoc::kBtnOk;   // подпись «ОК» msgbox (2288; ★ R5-b 12.11.2026: общая константа кнопок общих окон)
}

// ── Звуки и эффекты. Имена сняты дословно с эталона, ни одно не выдумано. ──
namespace ExtractFx
{
	static const TCHAR* kSndBoost   = TEXT("s_item_boosting02");     // `exchangeItemShowBoostingUI`
	static const TCHAR* kSndSuccess = TEXT("S_e_shovelling02.wav");  // `Recv_ExtractShop`, ветка 9
	static const TCHAR* kSndFail    = TEXT("S_e_shovelling03.wav");  // `Recv_ExtractShop`, ветка 10
	static const TCHAR* kJebSuccess = TEXT("Eff_0_char_99_c-suc-a_00");
	static const TCHAR* kJebFail    = TEXT("Eff_0_char_99_c-fail-a_00");
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaExtractShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `EXTRACT_WIN` (IDNum 16000) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `CExtractShop_InitUI@0xC8D040`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kExtractWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaExtractShopSubsystem::CloseWindow));
	}
	FrontUi.SetNum(kViewCells);
	BackUi.SetNum(kViewCells);
	FrontOpt.SetNum(kOptRows);
	BackOpt.SetNum(kOptRows);
	Front.SetNum(kGridCells);
	Back.SetNum(kGridCells);
	FrontState.SetNumZeroed(kViewCells);
	BackState.SetNumZeroed(kViewCells);
	TxtGuide.SetNum(5);
	FrontLamps.SetNum(kOptRows);
	BackLamps.SetNum(kOptRows);
	FrontBulbs.SetNum(kOptRows);
	BackBulbs.SetNum(kOptRows);
	HolyFx.SetNum(kOptRows);
	HolyFxHome.SetNum(kOptRows);
	ScrollIcon.SetNum(kScrollCells);
	ScrollCount.SetNum(kScrollCells);

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HExtractResult = Net->OnExtractShopResult.AddLambda(
			[this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// Обе сетки строятся из контейнера 1 — любое его изменение обязано их пересобрать
		//   (эталон зовёт `uiInventoryBuildSlotGrid` из каждого обработчика инвентаря).
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32 Container, int32 /*Cell*/)
		{
			if (Container == 1) { bDirty = true; }
		});
	}
	UE_LOG(LogAsdaExtract, Log, TEXT("[передача параметров] подсистема поднята"));
}

void UAsdaExtractShopSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnExtractShopResult.Remove(HExtractResult);
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->OnCellChanged.Remove(HInvCell);
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaExtractShopSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	ResetWidget();
	bDirty = false;
	BackPage = 0;
	SelFrontPage = 0; SelBackRow = -1; SelBackCol = -1; SelBackPage = 0;
	SelCost = 0;
	BoostStartS = 0.0;
	TipCell = -1; bTipBack = false; bConfirmOpen = false;
	ResultDstRecord.Reset();
	PendingResults.Clear();
	Mouse.Reset();
}

void UAsdaExtractShopSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.ExtractShop");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	if (!bOpen || !bControlsReady) { return; }
	// Пока идёт фаза машины — сетки НЕ пересобираем: эталон в этот момент держит `m_bBusy`
	//   и `exchangeItemRefreshSlotGrids` уходит в `uiSlotGridInit` (все ячейки закрыты).
	if (bDirty && !bBusy && !bApplying) { bDirty = false; RebuildAll(); }
	if (bBusy) { TickBoost(DeltaTime); }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (TipCell != -1)   // уже висящую плашку ГАСИМ, а не просто «не показываем» (закон `ui-shared-tip-ownership`)
		{
			TipCell = -1; bTipBack = false;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::ExtractShop);
			}
		}
		Mouse.Reset();
		return;
	}
	TickCells();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaExtractShopSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kExtractWindowId, kExtractWbp, kExtractZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			RegisterZone();
			// ★ 08.10.2026 [ОРИГИНАЛ] (window_default_pos.md §6 п.8): `CExtractShop_InitUI@0xC8D040` (item_enhance.c:680-684) при
			//   создании пишет Origin = ((W>>1) − w/2, (H>>1) − h/2), а якорь .JUI «16 42 18 / 32 42 18» добавляется поверх:
			//   x = trunc(42 %·W + Origin.x) − (w>>1), y = trunc(18 %·H + Origin.y) − (h>>1) — (96,92) при 1024×768, (920,304)
			//   при 1920×1080. Порт стоял в дефолте разметки (83,85). Откат: `asda.UI.JuiDefaultPos 0`.
			if (UAsdaWindowSubsystem::UseOriginalDefaultPos())
			{
				WM.PlaceWindowKeepingSavedOffset(kExtractWindowId, FName(TEXT("EXTRACT_WIN")), nullptr, [](const FVector2D& V, const FVector2D& F)
				{
					const int32 W = FMath::TruncToInt32(V.X), H = FMath::TruncToInt32(V.Y);
					const int32 Fw = FMath::TruncToInt32(F.X), Fh = FMath::TruncToInt32(F.Y);
					const int32 Ox = (W >> 1) - Fw / 2, Oy = (H >> 1) - Fh / 2;   // Origin после `CControl_SetField74_75`
					return FVector2D(FMath::TruncToInt32(0.42 * W + Ox) - (Fw >> 1), FMath::TruncToInt32(0.18 * H + Oy) - (Fh >> 1));
				});
			}
		},
		[]()
		{
			UE_LOG(LogAsdaExtract, Error, TEXT("[передача параметров] WBP_EXTRACT_WIN не загрузился (%s)"), kExtractWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaExtractShopSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterWheelHandler(kExtractWindowId);
		WM->UnregisterUiZone(kExtractWindowId);
		WM->CloseWindow(kExtractWindowId);
	}
	Widget = nullptr;
	GridFront = GridBack = GridScroll = nullptr;
	Relays.Reset();
	for (FAsdaExtractCellUi& U : FrontUi) { U = FAsdaExtractCellUi(); }
	for (FAsdaExtractCellUi& U : BackUi)  { U = FAsdaExtractCellUi(); }
	for (FAsdaExtractOptUi& U : FrontOpt) { U = FAsdaExtractOptUi(); }
	for (FAsdaExtractOptUi& U : BackOpt)  { U = FAsdaExtractOptUi(); }
	FrontBigIcon = BackBigIcon = nullptr;
	FrontCursor = BackCursor = FrontWheel = BackWheel = nullptr;
	TxtFrontTitle = TxtBackTitle = TxtCostLabel = TxtCostValue = nullptr;
	TxtGoldLabel = TxtGoldValue = TxtStatus = nullptr;
	for (TObjectPtr<UTextBlock>& T : TxtGuide) { T = nullptr; }
	BtnRefresh = BtnExtract = BtnCancel = nullptr;
	Gauge = GaugeBack = nullptr;
	GaugeHome = FVector2D::ZeroVector; GaugeFullW = 0.f;
	FrontChimney = FrontClock = BackChimney = BackClock = nullptr;
	for (TObjectPtr<UWidget>& W : FrontLamps) { W = nullptr; }
	for (TObjectPtr<UWidget>& W : BackLamps)  { W = nullptr; }
	for (TObjectPtr<UWidget>& W : FrontBulbs) { W = nullptr; }
	for (TObjectPtr<UWidget>& W : BackBulbs)  { W = nullptr; }
	for (TObjectPtr<UWidget>& W : HolyFx)     { W = nullptr; }
	BurnIcon = BackIconFx = nullptr;
	DestructFx = nullptr;
	for (TObjectPtr<UImage>& I : ScrollIcon)     { I = nullptr; }
	for (TObjectPtr<UTextBlock>& T : ScrollCount) { T = nullptr; }
	ScrollCursor = nullptr; ScrollCheck = nullptr;
	TxtScrollTitle = TxtScrollText = nullptr;
	bControlsReady = false;
	TipCell = -1;
	Mouse.Reset();
}

void UAsdaExtractShopSubsystem::PlaneShow(UWidget* W)
{
	// Эталон `CControl_Show@0x12E1040`: перезапустить анимацию плана и показать его.
	AsdaUi::PlaneShowRestart(W);   // ★ R5-b 12.11.2026: общая точка (то же тело)
}

void UAsdaExtractShopSubsystem::PlaneHide(UWidget* W)
{
	// Эталон `CControl_CloseHide@0x12E1420`: остановить анимацию БЕЗ события «завершено» и спрятать.
	AsdaUi::PlaneHideStop(W);   // ★ R5-b 12.11.2026: общая точка (то же тело)
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`).
void UAsdaExtractShopSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	TArray<FName> FrontIconNames; FrontIconNames.SetNum(kViewCells);
	TArray<FName> BackIconNames;  BackIconNames.SetNum(kViewCells);
	TArray<FName> ScrollIconNames; ScrollIconNames.SetNum(kScrollCells);

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("EXTRACT_BTN_REFRESH")) { BtnRefresh = Btn; continue; }
			if (N == TEXT("EXTRACT_BTN_EXTRACT")) { BtnExtract = Btn; continue; }
			if (N == TEXT("EXTRACT_BTN_CANCLE"))  { BtnCancel  = Btn; continue; }
			continue;
		}
		if (UCheckBox* Chk = Cast<UCheckBox>(W))
		{
			if (N == TEXT("EXTRACT_ScrollChk")) { ScrollCheck = Chk; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("EXTRACT_F_TITLE"))    { TxtFrontTitle = Tb; continue; }
			if (N == TEXT("EXTRACT_B_TITLE"))    { TxtBackTitle  = Tb; continue; }
			if (N == TEXT("EXTRACT_TXT01"))      { TxtCostLabel  = Tb; continue; }
			if (N == TEXT("EXTRACT_TXT02"))      { TxtCostValue  = Tb; continue; }
			if (N == TEXT("EXTRACT_TXT03"))      { TxtGoldLabel  = Tb; continue; }
			if (N == TEXT("EXTRACT_TXT04"))      { TxtGoldValue  = Tb; continue; }
			if (N == TEXT("EXTRACT_Message01"))  { TxtStatus     = Tb; continue; }
			if (N == TEXT("EXTRACT_ScrollText")) { TxtScrollText = Tb; continue; }
			if (N == TEXT("EXTRACT_ScrollTitle_Text")) { TxtScrollTitle = Tb; continue; }
			bool bTaken = false;
			for (int32 i = 0; i < 5 && !bTaken; ++i)
			{
				if (N == FString::Printf(TEXT("EXTRACT_Guide%02d"), i)) { TxtGuide[i] = Tb; bTaken = true; }
			}
			for (int32 i = 0; i < kOptRows && !bTaken; ++i)
			{
				if (N == FString::Printf(TEXT("EXTRACT_F_OPTION%d"), i + 1)) { FrontOpt[i].Text = Tb; bTaken = true; }
				else if (N == FString::Printf(TEXT("EXTRACT_B_OPTION%d"), i + 1)) { BackOpt[i].Text = Tb; bTaken = true; }
			}
			// Подпись строки списка свитков — эталон печатает в неё КОЛИЧЕСТВО (`sprintf("%d", rec+9)`).
			for (int32 i = 0; i < kScrollCells && !bTaken; ++i)
			{
				if (N == FString::Printf(TEXT("EXTRACT_Scroll%02d"), i)) { ScrollCount[i] = Tb; bTaken = true; }
			}
			if (bTaken) { Tb->SetText(FText::GetEmpty()); }
			continue;
		}

		// Планы: имя `{idc}_{juiIdx}_{planeIdx}` (закон `jui-importer-widget-naming`).
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx)) { continue; }

		bool bTaken = false;
		for (int32 c = 0; c < kViewCells && !bTaken; ++c)
		{
			if (Idc == FString::Printf(TEXT("EXTRACT_F_SLOT%02d"), c + 1))
			{
				FrontUi[c].Icon = Cast<UImage>(W); FrontIconNames[c] = FName(*N); bTaken = true;
			}
			else if (Idc == FString::Printf(TEXT("EXTRACT_B_SLOT%02d"), c + 1))
			{
				BackUi[c].Icon = Cast<UImage>(W); BackIconNames[c] = FName(*N); bTaken = true;
			}
			else if (Idc == FString::Printf(TEXT("EXTRACT_F_Disable%02d"), c + 1))
			{
				FrontUi[c].Disable = W; bTaken = true;
			}
			else if (Idc == FString::Printf(TEXT("EXTRACT_B_Disable%02d"), c + 1))
			{
				BackUi[c].Disable = W; bTaken = true;
			}
		}
		for (int32 i = 0; i < kOptRows && !bTaken; ++i)
		{
			if      (Idc == FString::Printf(TEXT("EXTRACT_F_OpBase%d"), i + 1)) { FrontOpt[i].Base = W; bTaken = true; }
			else if (Idc == FString::Printf(TEXT("EXTRACT_B_OpBase%d"), i + 1)) { BackOpt[i].Base  = W; bTaken = true; }
			else if (Idc == FString::Printf(TEXT("EXTRACT_F_LAMP%d"),   i + 1)) { FrontLamps[i] = W; bTaken = true; }
			else if (Idc == FString::Printf(TEXT("EXTRACT_B_LAMP%d"),   i + 1)) { BackLamps[i]  = W; bTaken = true; }
			else if (Idc == FString::Printf(TEXT("EXTRACT_Effect%d"),   i + 1)) { HolyFx[i]     = W; bTaken = true; }
		}
		if (!bTaken)
		{
			// Лампочки-«глаза»: у эталона это МАССИВЫ 16300…16304 и 16350…16354, но в `.JUI`
			//   их имена чередуют цвет (Y1/R2/Y3/R4/Y5 и R1/M2/R3/M4/R5) — берём по позиции.
			static const TCHAR* kFrontBulb[kOptRows] = {
				TEXT("EXTRACT_F_BULB_Y1"), TEXT("EXTRACT_F_BULB_R2"), TEXT("EXTRACT_F_BULB_Y3"),
				TEXT("EXTRACT_F_BULB_R4"), TEXT("EXTRACT_F_BULB_Y5") };
			static const TCHAR* kBackBulb[kOptRows] = {
				TEXT("EXTRACT_B_BULB_R1"), TEXT("EXTRACT_B_BULB_M2"), TEXT("EXTRACT_B_BULB_R3"),
				TEXT("EXTRACT_B_BULB_M4"), TEXT("EXTRACT_B_BULB_R5") };
			for (int32 i = 0; i < kOptRows && !bTaken; ++i)
			{
				if      (Idc == kFrontBulb[i]) { FrontBulbs[i] = W; bTaken = true; }
				else if (Idc == kBackBulb[i])  { BackBulbs[i]  = W; bTaken = true; }
			}
		}
		if (!bTaken)
		{
			for (int32 i = 0; i < kScrollCells && !bTaken; ++i)
			{
				if (Idc == FString::Printf(TEXT("EXTRACT_Scroll%02d"), i))
				{
					ScrollIcon[i] = Cast<UImage>(W); ScrollIconNames[i] = FName(*N); bTaken = true;
				}
			}
		}
		if (bTaken) { continue; }

		if      (Idc == TEXT("EXTRACT_F_ICON"))          { FrontBigIcon = Cast<UImage>(W); }
		else if (Idc == TEXT("EXTRACT_B_ICON"))          { BackBigIcon  = Cast<UImage>(W); }
		else if (Idc == TEXT("EXTRACT_F_SLOT_CURSOR"))   { FrontCursor  = W; }
		else if (Idc == TEXT("EXTRACT_B_SLOTCURSOR"))    { BackCursor   = W; }
		else if (Idc == TEXT("EXTRACT_F_WHELL"))         { FrontWheel   = W; }
		else if (Idc == TEXT("EXTRACT_B_WHEEL"))         { BackWheel    = W; }
		else if (Idc == TEXT("EXTRACT_F_CHIMN"))         { FrontChimney = W; }
		else if (Idc == TEXT("EXTRACT_F_CLOCK"))         { FrontClock   = W; }
		else if (Idc == TEXT("EXTRACT_B_CHIMN"))         { BackChimney  = W; }
		else if (Idc == TEXT("EXTRACT_B_CLOCK"))         { BackClock    = W; }
		else if (Idc == TEXT("EXTRACT_F_BURNICON"))      { BurnIcon     = W; }
		else if (Idc == TEXT("EXTRACT_B_ICONEF"))        { BackIconFx   = W; }
		else if (Idc == TEXT("EXTRACT_F_DistrucEffect")) { DestructFx   = Cast<UAsdaAnimatedPlane>(W); }
		else if (Idc == TEXT("EXTRACT_ScrollSel"))       { ScrollCursor = W; }
		// Скин `LightBlue_Bar` даёт ДВА ES-плана: 0 — подложка, 1 — заливка (закон
		//   `ui-progressbar-animate-existing`). Клипаем ширину именно заливки.
		else if (Idc == TEXT("EXTRACT_GAUGE"))           { if (PlaneIdx >= 1) { Gauge = W; } else { GaugeBack = W; } }
	}

	// ── Сетки иконок через ОБЩУЮ точку (закон `port-shared-points`) ──
	//   Клик ловим САМИ по живой геометрии (`TickCells`) — оверлеи гасим (грабли дня, п. 1).
	auto MakeGrid = [this](const TArray<FName>& Names, TFunction<int32(int32)> Codes) -> UAsdaIconCellGrid*
	{
		UAsdaIconCellGrid* G = NewObject<UAsdaIconCellGrid>(this);
		G->SetCellSizePx((float)kCellPx);
		G->SetClickOverlayEnabled(false);
		// Эталон в начале КАЖДОГО прохода затирает иконку пустой текстурой 32×32
		//   (`CControl_DrawTextureRect(ctrl, пусто, 0, 0, 32, 32)`) — иначе на уплотнённой сетке
		//   остаются картинки прошлого состава.
		G->bClearEmptyCells = true;
		G->SetupNamed(Widget, Names, /*bMale*/true, MoveTemp(Codes), [](int32) {});
		return G;
	};
	GridFront = MakeGrid(FrontIconNames, [this](int32 View) -> int32
	{
		const int32 Abs = AbsIndex(FrontPage, View / kViewCols, View % kViewCols);
		return Front.IsValidIndex(Abs) ? Front[Abs].ItemCode : 0;
	});
	GridBack = MakeGrid(BackIconNames, [this](int32 View) -> int32
	{
		const int32 Abs = AbsIndex(BackPage, View / kViewCols, View % kViewCols);
		return Back.IsValidIndex(Abs) ? Back[Abs].ItemCode : 0;
	});
	GridScroll = MakeGrid(ScrollIconNames, [this](int32 Cell) -> int32
	{
		if (!ScrollCells.IsValidIndex(Cell)) { return 0; }
		const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		const FAsdaItemInstance* It = Inv ? Inv->GetCell(1, ScrollCells[Cell]) : nullptr;
		return It ? It->ItemCode : 0;
	});

	// ── Кнопки ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnRefresh, 16090, [this](int32) { OnRefreshClicked(); });
	Bind(BtnExtract, 16091, [this](int32) { OnExtractClicked(); });
	Bind(BtnCancel,  16092, [this](int32) { OnCancelClicked(); });

	if (ScrollCheck)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 16412;
		R->CheckCallback = [this](int32, bool bChecked) { OnScrollCheckChanged(bChecked); };
		ScrollCheck->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(Relays, R, ScrollCheck);
	}
	// Конец анимации уничтожения источника — эталонное событие 24, ветка 16146
	//   в `itemCombineDlgOnButton@0xC90530`.
	if (DestructFx)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 16146;
		R->Callback = [this](int32) { FinishDestructFx(); };
		DestructFx->OnAnimFinished.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, DestructFx);
	}

	// ── Статичные подписи (эталон ставит их в `CExtractShop_InitUI`) ──
	auto SetLoc = [this](UTextBlock* T, int32 Id)
	{
		if (!T) { return; }
		const FString S = Loc(Id);
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtFrontTitle,  ExtractLoc::kFrontTitle);
	SetLoc(TxtBackTitle,   ExtractLoc::kBackTitle);
	SetLoc(TxtCostLabel,   ExtractLoc::kCostLabel);
	SetLoc(TxtGoldLabel,   ExtractLoc::kGoldLabel);
	SetLoc(TxtScrollTitle, ExtractLoc::kScrollTitle);
	SetLoc(TxtScrollText,  ExtractLoc::kScrollText);
	for (int32 i = 0; i < 5; ++i) { SetLoc(TxtGuide[i], ExtractLoc::kGuide0 + i); }
	if (TxtCostValue) { TxtCostValue->SetText(FText::FromString(TEXT("0"))); }
	if (TxtGoldValue) { TxtGoldValue->SetText(FText::FromString(TEXT("0"))); }
	if (TxtStatus)    { TxtStatus->SetText(FText::GetEmpty()); }
	// Состояния подписей кнопок (наведение/нажатие) — общая точка порта (закон `jui-caption-states`).
	AsdaUi::BindButtonCaptionStates(Widget, CaptionRelays);
	// Подписи кнопок эталон ставит нотисами; в `.JUI` они корейские.
	auto SetBtnCaption = [this](UButton* Btn, int32 LocId)
	{
		if (!Btn || !Widget || !Widget->WidgetTree) { return; }
		const FString S = Loc(LocId);
		if (S.IsEmpty()) { return; }
		// Импортёр кладёт подпись кнопки в `TextBlock` с именем `{idc}_Text`.
		const FString Want = Btn->GetName() + TEXT("_Text");
		if (UTextBlock* Tb = Cast<UTextBlock>(AsdaUi::FindNodeExact(Widget, *Want)))
		{
			Tb->SetText(FText::FromString(S));
		}
	};
	SetBtnCaption(BtnRefresh, ExtractLoc::kBtnRefresh);
	SetBtnCaption(BtnExtract, ExtractLoc::kBtnExtract);
	SetBtnCaption(BtnCancel,  ExtractLoc::kBtnCancel);

	// Исходная геометрия полосы: её полная ширина — то, что задал автор в `.JUI` (348×4).
	if (Gauge)
	{
		FVector2D Pos, Size;
		if (AsdaUi::SlotRect(Gauge, Pos, Size)) { GaugeHome = Pos; GaugeFullW = (float)Size.X; }
	}
	// Эталон при инициализации прячет полосу целиком (`CControl_SetVisible(m_pProgress171, 0)`),
	//   а в `.JUI` она объявлена видимой — гасим ОБА её плана.
	if (Gauge)     { Gauge->SetVisibility(ESlateVisibility::Collapsed); }
	if (GaugeBack) { GaugeBack->SetVisibility(ESlateVisibility::Collapsed); }

	// Исходные позиции пяти «святых» эффектов — от них считается их ход по X во время работы.
	for (int32 i = 0; i < kOptRows; ++i)
	{
		FVector2D Pos, Size;
		HolyFxHome[i] = (HolyFx[i] && AsdaUi::SlotRect(HolyFx[i], Pos, Size)) ? Pos : FVector2D::ZeroVector;
	}

	bControlsReady = (FrontUi[0].Icon != nullptr && BackUi[0].Icon != nullptr
		&& BtnExtract != nullptr && BtnCancel != nullptr);
	UE_LOG(LogAsdaExtract, Log,
		TEXT("[передача параметров] окно 16000 разобрано: ячейки %d/%d, строки параметров %d/%d, ")
		TEXT("кнопки 16090/16091/16092 = %s/%s/%s, полоса %s, свитки %s, анимация уничтожения %s"),
		kViewCells, kViewCells, kOptRows, kOptRows,
		BtnRefresh ? TEXT("да") : TEXT("НЕТ"), BtnExtract ? TEXT("да") : TEXT("НЕТ"),
		BtnCancel ? TEXT("да") : TEXT("НЕТ"), Gauge ? TEXT("да") : TEXT("НЕТ"),
		ScrollIcon[0] ? TEXT("да") : TEXT("НЕТ"), DestructFx ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaExtractShopSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Колесо над окном — обработчик оконной подсистемы (эталонные «колёсные окна» 16024/16074).
	WM->RegisterWheelHandler(kExtractWindowId, FAsdaUiWheelHandler::CreateUObject(this, &UAsdaExtractShopSubsystem::OnWheel));
	WM->RegisterUiZone(kExtractWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) -> bool
		{
			if (!bOpen || !Widget) { return false; }
			// Корпус окна — хит-зона корневого контрола 16000 (в `.JUI` он `NoImage2` 846×430,
			//   собственных планов рамки у него нет, поэтому берём именно её).
			const UWidget* Root = AsdaUi::FindNodeExact(Widget, TEXT("EXTRACT_WIN_0_HIT"));
			return Root && AsdaUi::WidgetAbsRect(Root, Mn, Mx);
		}));
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

void UAsdaExtractShopSubsystem::OpenWindow()
{
	if (!EnsureWidget()) { return; }
	// Эталон `extractShop_Open@0xC8E390`: повторный вызов при уже открытом окне НИЧЕГО не делает.
	if (bOpen) { return; }

	bOpen = true;
	bBusy = false;
	bApplying = false;
	bPending = false;
	FrontPage = BackPage = 0;
	SelCost = 0;
	SelScrollCell = -1;
	ResultDstRecord.Reset();
	TipCell = -1;

	// Эталон гасит пять эффектов 16320…16324, курсор списка 16410 и выключает чекбокс 16412.
	for (int32 i = 0; i < kOptRows; ++i) { PlaneHide(HolyFx[i]); }
	if (ScrollCursor) { ScrollCursor->SetVisibility(ESlateVisibility::Collapsed); }
	if (ScrollCheck)  { ScrollCheck->SetIsChecked(false); ScrollCheck->SetIsEnabled(false); }
	StopBoostVisuals();

	// Латч мыши: без сброса удержанная с прошлого кадра кнопка не даст фронта и первый клик
	//   по только что открытому окну потеряется (см. комментарий у `FAsdaMouseFrame::Reset`).
	Mouse.Reset();

	ClearSelection();
	RebuildAll();
	RefreshScrollList();
	RefreshGoldText();
	SetStatusText(-10);   // 13270 «Выбери предмет, который будет извлечен.»

	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kExtractWindowId, true);
	}
	UE_LOG(LogAsdaExtract, Log, TEXT("[передача параметров] окно открыто: источников %d, приёмников %d, свитков %d"),
		Front.Num(), Back.Num(), ScrollCells.Num());
}

void UAsdaExtractShopSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	// Эталон `extractShop_Close@0xC8E600`: пока машина работает, окно НЕ закрывается
	//   (`if (this->m_bBusy) return 0;`).
	if (bBusy) { return; }
	bOpen = false;
	bApplying = false;
	TipCell = -1;
	bConfirmOpen = false;
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::ExtractShop);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kExtractWindowId, false);
	}
	UE_LOG(LogAsdaExtract, Log, TEXT("[передача параметров] окно закрыто"));
	// ★ Волна В4 ремесла (ревизия В3 С-3): хвост `extractShop_Close` — `UI_resetAllWindowsAndLeave_5045(g_pNpcWnd)`
	//   (item_enhance.c:1003): выход из разговора, спрятанного при открытии окна, и восстановление снимка окон.
	if (UAsdaNpcDialogSubsystem* Dlg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
	{
		Dlg->ResetAllWindowsAndLeave(TEXT("закрытие окна 16000 (extractShop_Close)"));
	}
}

// ═════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════

const FAsdaItemInstance* UAsdaExtractShopSubsystem::CellItem(const FAsdaExtractEntry& E) const
{
	if (E.IsEmpty()) { return nullptr; }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return Inv ? Inv->GetCell(1, E.CellIndex) : nullptr;
}

// `sub_C90E30@0xC90E30`: `a1 <= 4 || a1 == 8 || a1 == 9` — вид предмета допущен в обе сетки.
bool UAsdaExtractShopSubsystem::PassesTypeGate(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!R) { return false; }
	return (R->ItemType <= 4) || (R->ItemType == 8) || (R->ItemType == 9);
}

// `sub_C90E90@0xC90E90`: `a1 <= 4 || a1 == 8` — при этом виде дополнительно сверяются профессии.
bool UAsdaExtractShopSubsystem::PassesWeaponGate(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!R) { return false; }
	return (R->ItemType <= 4) || (R->ItemType == 8);
}

// `itemGradeBandFromTypeIndex@0xEC97A0`: 1…11 → 1, 12…22 → 2, 23…33 → 3, иначе 0.
int32 UAsdaExtractShopSubsystem::GradeBand(int32 UsageCategory)
{
	if (UsageCategory >= 1  && UsageCategory <= 11) { return 1; }
	if (UsageCategory >= 12 && UsageCategory <= 22) { return 2; }
	if (UsageCategory >= 23 && UsageCategory <= 33) { return 3; }
	return 0;
}

// `itemOpGraftLookupByRange@0xC93920`: линейный проход по 20 строкам, первое попадание в диапазон.
int32 UAsdaExtractShopSubsystem::CostForLevel(int32 RequiredLevel) const
{
	const UDataTable* Dt = AsdaDt::Cached(kOpGraftDt);
	if (!Dt) { return 0; }
	// Порядок строк важен (диапазоны идут подряд) — идём по индексу, как эталон по массиву.
	for (int32 i = 0; i < 20; ++i)
	{
		const FAsdaItemopgraftRow* R = Dt->FindRow<FAsdaItemopgraftRow>(
			FName(*FString::FromInt(i)), TEXT("Extract:cost"), /*bWarnIfRowMissing*/false);
		if (!R) { continue; }
		if (RequiredLevel >= R->LevelLow && RequiredLevel <= R->LevelHigh) { return R->Value; }
	}
	return 0;
}

const FAsdaExtractEntry* UAsdaExtractShopSubsystem::SelectedFront() const
{
	if (SelFrontRow < 0 || SelFrontCol < 0) { return nullptr; }
	// ★ ДЕФЕКТ ОРИГИНАЛА №1 (ключ `asda.Extract.FixPageOnConfirm`): эталон в кнопке 16091 читает
	//   источник БЕЗ слагаемого страницы. Пакет её учитывает, проверки — нет.
	const int32 Page = (CVarFixPageOnConfirm.GetValueOnAnyThread() != 0) ? SelFrontPage : 0;
	const int32 Abs = AbsIndex(Page, SelFrontRow, SelFrontCol);
	return Front.IsValidIndex(Abs) ? &Front[Abs] : nullptr;
}

const FAsdaExtractEntry* UAsdaExtractShopSubsystem::SelectedBack() const
{
	if (SelBackRow < 0 || SelBackCol < 0) { return nullptr; }
	const int32 Abs = AbsIndex(SelBackPage, SelBackRow, SelBackCol);
	return Back.IsValidIndex(Abs) ? &Back[Abs] : nullptr;
}

// ═════════════════════════════ НАПОЛНЕНИЕ МОДЕЛИ ══════════════════════════════════════════════

// `uiInventoryBuildSlotGrid@0xC91200`: скан 30 ячеек контейнера 1, УПЛОТНЕНИЕ подходящих.
//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): скан всей доступной сумки (до 60, `kGridCells`), страниц прокрутки до 9.
void UAsdaExtractShopSubsystem::RebuildFront()
{
	for (FAsdaExtractEntry& E : Front) { E.Clear(); }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	int32 Out = 0;
	const int32 Cells = FMath::Min(Inv->GetContainerSize(1), kGridCells);
	for (int32 C = 0; C < Cells && Out < kGridCells; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(1, C);
		if (!It || It->IsEmpty() || !PassesTypeGate(It->ItemCode)) { continue; }
		Front[Out].ItemCode  = It->ItemCode;
		Front[Out].CellIndex = C;
		++Out;
	}
}

// `uiCompose_FillFilteredBagSlots@0xC91B90` — ТОТ ЖЕ проход и тот же гейт, второй массив.
void UAsdaExtractShopSubsystem::RebuildBack()
{
	for (FAsdaExtractEntry& E : Back) { E.Clear(); }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	int32 Out = 0;
	const int32 Cells = FMath::Min(Inv->GetContainerSize(1), kGridCells);
	for (int32 C = 0; C < Cells && Out < kGridCells; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(1, C);
		if (!It || It->IsEmpty() || !PassesTypeGate(It->ItemCode)) { continue; }
		Back[Out].ItemCode  = It->ItemCode;
		Back[Out].CellIndex = C;
		++Out;
	}
}

// `uiSlotGridValidateItems@0xC90FA0` — коды состояния ВИДИМЫХ 12 ячеек источника.
void UAsdaExtractShopSubsystem::ValidateFront()
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	for (int32 V = 0; V < kViewCells; ++V)
	{
		FrontState[V] = -1;
		// Пока применяем ответ сервера, эталон подменяет проверку на `uiSlotGridInit` (все нули) —
		//   ячейки закрыты, кликать нельзя.
		if (bApplying) { FrontState[V] = 0; continue; }
		const int32 Abs = AbsIndex(FrontPage, V / kViewCols, V % kViewCols);
		const FAsdaExtractEntry* E = Front.IsValidIndex(Abs) ? &Front[Abs] : nullptr;
		const FAsdaItemInstance* It = E ? CellItem(*E) : nullptr;
		const FAsdaItemRecord* R = (It && IT) ? IT->GetItem(It->ItemCode) : nullptr;
		if (!R) { FrontState[V] = -1; continue; }                       // 13261 «Нет данных о предмете.»
		if (!PassesTypeGate(It->ItemCode)) { FrontState[V] = -2; continue; }  // 13262 «Это не предмет снаряжения.»
		int32 OptCount = 0;
		for (int32 i = 0; i < kOptRows; ++i)
		{
			if (It->EnchantCode.IsValidIndex(i) && It->EnchantCode[i] > 0) { ++OptCount; }
		}
		if (OptCount <= 0) { FrontState[V] = -5; continue; }            // 13265 «…без параметров.»
		FrontState[V] = (R->UsageCategory != 0) ? 1 : -4;               // -4 → 13264 «…обычный предмет.»
	}
}

// `itemCheckCompositionMatch@0xC916B0` — коды состояния ВИДИМЫХ 12 ячеек приёмника.
//   Без выбранного источника ВСЕ ячейки получают −10 (13270 «Выбери предмет…»).
void UAsdaExtractShopSubsystem::ValidateBack()
{
	for (int32 V = 0; V < kViewCells; ++V) { BackState[V] = -10; }
	if (bApplying) { for (int32 V = 0; V < kViewCells; ++V) { BackState[V] = 0; } return; }

	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	// Эталон берёт базу по СОХРАНЁННОЙ странице (dword 183), а не по текущей.
	if (SelFrontRow < 0 || SelFrontCol < 0) { return; }
	const int32 BaseAbs = AbsIndex(SelFrontPage, SelFrontRow, SelFrontCol);
	const FAsdaExtractEntry* BaseE = Front.IsValidIndex(BaseAbs) ? &Front[BaseAbs] : nullptr;
	const FAsdaItemInstance* BaseIt = BaseE ? CellItem(*BaseE) : nullptr;
	const FAsdaItemRecord* Base = BaseIt ? IT->GetItem(BaseIt->ItemCode) : nullptr;
	if (!Base) { return; }

	for (int32 V = 0; V < kViewCells; ++V)
	{
		const int32 Abs = AbsIndex(BackPage, V / kViewCols, V % kViewCols);
		const FAsdaExtractEntry* E = Back.IsValidIndex(Abs) ? &Back[Abs] : nullptr;
		const FAsdaItemInstance* It = E ? CellItem(*E) : nullptr;
		const FAsdaItemRecord* Mat = It ? IT->GetItem(It->ItemCode) : nullptr;
		if (!Mat)                          { BackState[V] = -1;  continue; }   // 13261
		if (!PassesTypeGate(It->ItemCode)) { BackState[V] = -2;  continue; }   // 13262
		if (Base->RequiredLevel > Mat->RequiredLevel) { BackState[V] = -6; continue; }  // 13266 «…уровень низок»
		if (Base->ItemType != Mat->ItemType)          { BackState[V] = -7; continue; }  // 13267 «…не тот тип»
		// Профессии сверяются ТОЛЬКО у видов 0…4 и 8 (`sub_C90E90`); у 9 — нет.
		if (PassesWeaponGate(It->ItemCode) && GradeBand(Base->UsageCategory) != GradeBand(Mat->UsageCategory))
		{
			BackState[V] = -8; continue;                                        // 13268 «…не та профессия»
		}
		// Тот же слот сумки, что и у источника ⇒ это он сам.
		if (BaseIt->SlotIndex == It->SlotIndex) { BackState[V] = -9; continue; } // 13269
		if (Mat->UsageCategory == 0)            { BackState[V] = -4; continue; } // 13264
		// Подтип (у оружия — вид оружия) обязан совпасть, если он у источника задан.
		if (Base->ItemType != 0 && Base->ItemSubType != Mat->ItemSubType)
		{
			BackState[V] = -12; continue;                                       // 100103 «другой тип оружия»
		}
		BackState[V] = 1;
	}
}

void UAsdaExtractShopSubsystem::RebuildAll()
{
	RebuildFront();
	RebuildBack();
	RefreshFrontGrid();
	RefreshBackGrid();
	RefreshGoldText();
	// Список свитков строится из того же контейнера 1 — пересобираем вместе с сетками.
	RefreshScrollList();
}

// ═════════════════════════════ РЕНДЕР ═════════════════════════════════════════════════════════

void UAsdaExtractShopSubsystem::RefreshFrontGrid()
{
	ValidateFront();
	if (GridFront) { GridFront->Refresh(); }
	for (int32 V = 0; V < kViewCells; ++V)
	{
		if (!FrontUi.IsValidIndex(V) || !FrontUi[V].Disable) { continue; }
		// Эталон: состояние 1 ⇒ оверлей СКРЫТ, любое другое ⇒ ПОКАЗАН.
		FrontUi[V].Disable->SetVisibility(FrontState[V] == 1
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	MoveFrontCursor();
}

void UAsdaExtractShopSubsystem::RefreshBackGrid()
{
	ValidateBack();
	if (GridBack) { GridBack->Refresh(); }
	for (int32 V = 0; V < kViewCells; ++V)
	{
		if (!BackUi.IsValidIndex(V) || !BackUi[V].Disable) { continue; }
		BackUi[V].Disable->SetVisibility(BackState[V] == 1
			? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	MoveBackCursor();
}

// `itemDlgFillSlotDetail@0xC8EF10`: крупная иконка 16045 + пять строк параметров + цена 16094.
void UAsdaExtractShopSubsystem::RefreshFrontDetail(bool bHasSelection)
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaExtractEntry* E = bHasSelection ? SelectedFront() : nullptr;
	const FAsdaItemInstance* It = E ? CellItem(*E) : nullptr;
	const FAsdaItemRecord* R = (It && IT) ? IT->GetItem(It->ItemCode) : nullptr;

	if (!R)
	{
		// Эталон рисует «пустую» текстуру 32×32 и гасит все пять строк.
		if (FrontBigIcon) { AsdaUi::ClearImageBrush(FrontBigIcon); }
		for (int32 i = 0; i < kOptRows; ++i)
		{
			if (FrontOpt[i].Text) { FrontOpt[i].Text->SetText(FText::GetEmpty()); }
			if (FrontOpt[i].Base) { FrontOpt[i].Base->SetVisibility(ESlateVisibility::Collapsed); }
		}
		SelCost = 0;
		if (TxtCostValue) { TxtCostValue->SetText(FText::FromString(TEXT("0"))); }
		return;
	}

	// Иконка — через ОБЩУЮ точку порта (закон `port-shared-points`), эталонный размер 64 px.
	if (FrontBigIcon)
	{
		const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (Inv) { Inv->ApplyIconToImage(FrontBigIcon, It->ItemCode, 64.f); }
	}
	for (int32 i = 0; i < kOptRows; ++i)
	{
		const int32 Code = It->EnchantCode.IsValidIndex(i)  ? It->EnchantCode[i]  : 0;
		const int32 Val  = It->EnchantValue.IsValidIndex(i) ? It->EnchantValue[i] : 0;
		FString Line = (Code > 0 && Tip) ? Tip->FormatOptionValue(Code, Val) : FString();
		const bool bShow = (Code > 0 && !Line.IsEmpty());
		if (FrontOpt[i].Text) { FrontOpt[i].Text->SetText(bShow ? FText::FromString(Line) : FText::GetEmpty()); }
		AsdaUi::SetShownNoHit(FrontOpt[i].Base, bShow);
	}
	SelCost = CostForLevel(R->RequiredLevel);
	if (TxtCostValue) { TxtCostValue->SetText(FText::FromString(AsdaUi::FormatThousands(SelCost))); }
}

// `itemInfoPanel_Refresh@0xC8F3F0`: крупная иконка 16088 + пять строк параметров приёмника.
void UAsdaExtractShopSubsystem::RefreshBackDetail(bool bHasSelection)
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaExtractEntry* E = bHasSelection ? SelectedBack() : nullptr;
	const FAsdaItemInstance* It = E ? CellItem(*E) : nullptr;

	// ★ Эталон: если выбранная ячейка приёмника уже пуста (после применения ответа), он берёт
	//   ЗАПИСЬ ИЗ ОТВЕТА (буфер +0x11E4) — иначе результат исчезал бы с экрана в тот же кадр.
	FAsdaItemInstance FromResult;
	if ((!It || It->IsEmpty()) && ResultDstRecord.Num() >= FAsdaItemInstance::kRecordSize)
	{
		FromResult.SetRaw(ResultDstRecord.GetData(), FAsdaItemInstance::kRecordSize);
		if (!FromResult.IsEmpty()) { It = &FromResult; }
	}
	const FAsdaItemRecord* R = (It && IT) ? IT->GetItem(It->ItemCode) : nullptr;

	if (!R)
	{
		if (BackBigIcon) { AsdaUi::ClearImageBrush(BackBigIcon); }
		for (int32 i = 0; i < kOptRows; ++i)
		{
			if (BackOpt[i].Text) { BackOpt[i].Text->SetText(FText::GetEmpty()); }
			if (BackOpt[i].Base) { BackOpt[i].Base->SetVisibility(ESlateVisibility::Collapsed); }
		}
		return;
	}
	if (BackBigIcon)
	{
		const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (Inv) { Inv->ApplyIconToImage(BackBigIcon, It->ItemCode, 64.f); }
	}
	for (int32 i = 0; i < kOptRows; ++i)
	{
		const int32 Code = It->EnchantCode.IsValidIndex(i)  ? It->EnchantCode[i]  : 0;
		const int32 Val  = It->EnchantValue.IsValidIndex(i) ? It->EnchantValue[i] : 0;
		FString Line = (Code > 0 && Tip) ? Tip->FormatOptionValue(Code, Val) : FString();
		const bool bShow = (Code > 0 && !Line.IsEmpty());
		if (BackOpt[i].Text) { BackOpt[i].Text->SetText(bShow ? FText::FromString(Line) : FText::GetEmpty()); }
		AsdaUi::SetShownNoHit(BackOpt[i].Base, bShow);
	}
}

void UAsdaExtractShopSubsystem::RefreshGoldText()
{
	if (!TxtGoldValue) { return; }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	TxtGoldValue->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0)));
}

// `petUiRefreshGrid@0xC93E90` (имя эталона ВРЁТ — к питомцам отношения не имеет): список
//   свитков-бустеров из контейнера 1 по `special_itemlist.SubType == 96`, первые шесть.
void UAsdaExtractShopSubsystem::RefreshScrollList()
{
	ScrollCells.Reset();
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Inv && IT)
	{
		const int32 Cells = FMath::Min(Inv->GetContainerSize(1), kGridCells);
		for (int32 C = 0; C < Cells; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(1, C);
			if (!It || It->IsEmpty() || !IT->IsSpecialItemCode(It->ItemCode)) { continue; }
			FAsdaSpecialItemlistRow Sp;
			if (!IT->GetSpecialItem(It->ItemCode, Sp) || Sp.SubType != kScrollSubType) { continue; }
			ScrollCells.Add(C);
		}
	}
	// Выбранная строка могла исчезнуть (стопка кончилась) — снимаем выбор.
	if (SelScrollCell >= 0 && !ScrollCells.Contains(SelScrollCell)) { SelScrollCell = -1; }

	if (GridScroll) { GridScroll->Refresh(); }
	for (int32 i = 0; i < kScrollCells; ++i)
	{
		if (!ScrollCount.IsValidIndex(i) || !ScrollCount[i]) { continue; }
		if (!ScrollCells.IsValidIndex(i)) { ScrollCount[i]->SetText(FText::GetEmpty()); continue; }
		const FAsdaItemInstance* It = Inv ? Inv->GetCell(1, ScrollCells[i]) : nullptr;
		// Эталон печатает СЫРОЕ количество (`sprintf("%d", *(int*)(rec+9))`), без разделителей.
		ScrollCount[i]->SetText(It ? FText::AsNumber(It->Amount) : FText::GetEmpty());
	}
	// Курсор 16410 — над выбранной строкой; чекбокс 16412 доступен, только пока свиток выбран
	//   (`uiListUpdateScroll`: `SetVisibleState(check, subType == 96 ? 1 : 0)`).
	const int32 SelRow = ScrollCells.IndexOfByKey(SelScrollCell);
	if (ScrollCursor)
	{
		if (SelRow >= 0 && ScrollIcon.IsValidIndex(SelRow) && ScrollIcon[SelRow])
		{
			// ★ Дедуп №2, В2-B (U19): общая точка рамки выбора (тот же расчёт, без перезапуска кадров — как было).
			AsdaUi::PlaceFrameOver(ScrollCursor, ScrollIcon[SelRow], /*bRestartAnim*/false);
		}
		else { ScrollCursor->SetVisibility(ESlateVisibility::Collapsed); }
	}
	if (ScrollCheck)
	{
		ScrollCheck->SetIsEnabled(SelRow >= 0);
		if (SelRow < 0) { ScrollCheck->SetIsChecked(false); }
	}
}

// `uiSlotCursorUpdateHighlight@0xC8FA10`: ряд выбора в ТЕКУЩЕМ окне = сохранённая страница +
//   выбранный ряд − текущая страница; выход за два видимых ряда ⇒ курсор прячется.
void UAsdaExtractShopSubsystem::MoveFrontCursor()
{
	if (!FrontCursor) { return; }
	if (SelFrontRow < 0 || SelFrontCol < 0)
	{
		FrontCursor->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const int32 ViewRow = SelFrontRow + SelFrontPage - FrontPage;
	if (ViewRow < 0 || ViewRow >= kViewRows)
	{
		FrontCursor->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const int32 V = ViewRow * kViewCols + SelFrontCol;
	const UWidget* Cell = FrontUi.IsValidIndex(V) ? FrontUi[V].Icon.Get() : nullptr;
	if (!Cell) { FrontCursor->SetVisibility(ESlateVisibility::Collapsed); return; }
	// ★ Дедуп №2, В2-B (U19): общая точка рамки выбора (тот же расчёт, без перезапуска кадров — как было).
	AsdaUi::PlaceFrameOver(FrontCursor, Cell, /*bRestartAnim*/false);
}

// `itemDlgUpdateSelCursor_FBE0@0xC8FBE0` — то же для приёмника.
void UAsdaExtractShopSubsystem::MoveBackCursor()
{
	if (!BackCursor) { return; }
	if (SelBackRow < 0 || SelBackCol < 0)
	{
		BackCursor->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const int32 ViewRow = SelBackRow + SelBackPage - BackPage;
	if (ViewRow < 0 || ViewRow >= kViewRows)
	{
		BackCursor->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	const int32 V = ViewRow * kViewCols + SelBackCol;
	const UWidget* Cell = BackUi.IsValidIndex(V) ? BackUi[V].Icon.Get() : nullptr;
	if (!Cell) { BackCursor->SetVisibility(ESlateVisibility::Collapsed); return; }
	// ★ Дедуп №2, В2-B (U19): общая точка рамки выбора (тот же расчёт, без перезапуска кадров — как было).
	AsdaUi::PlaceFrameOver(BackCursor, Cell, /*bRestartAnim*/false);
}

// `uiSetStatusLabelText@0xC93600` — строка 16145 по КОДУ состояния ячейки.
void UAsdaExtractShopSubsystem::SetStatusText(int32 StateCode)
{
	if (!TxtStatus) { return; }
	FString S;
	if (StateCode == -12)                       { S = Loc(ExtractLoc::kStatusOther); }
	else if (StateCode <= 0 && StateCode >= -11) { S = Loc(ExtractLoc::kStatus0 + (-StateCode)); }
	// Прочие коды (в т. ч. 1 = «годится») эталон печатает пустой строкой.
	TxtStatus->SetText(FText::FromString(S));
	TxtStatus->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

// `uiClearItemExchangeDialog@0xC904A0` — сброс подбора обеих половин.
void UAsdaExtractShopSubsystem::ClearSelection()
{
	SelFrontRow = SelFrontCol = -1;
	SelBackRow = SelBackCol = -1;
	SelFrontPage = FrontPage;
	SelBackPage = BackPage;
	SelCost = 0;
	ResultDstRecord.Reset();
	// Эталон сбрасывает прокрутку правой сетки в ноль (`uiCtrl_SetChildScrollbarPos(m_pField87, 0)`)
	//   ДО перерисовки — иначе сетка осталась бы отрисованной по старой странице.
	BackPage = 0;
	SelBackPage = 0;
	RefreshFrontDetail(false);
	RefreshBackDetail(false);
	RefreshFrontGrid();
	RefreshBackGrid();
	// И печатает «0» в поле цены 16094.
	if (TxtCostValue) { TxtCostValue->SetText(FText::FromString(TEXT("0"))); }
}

// ═════════════════════════════ ПОКАДРОВЫЕ ОПРОСЫ ══════════════════════════════════════════════

void UAsdaExtractShopSubsystem::TickCells()
{
	// Клик и наведение ловим ПО ЖИВОЙ ГЕОМЕТРИИ: ячейки в `.JUI` лежат внутри групп,
	//   и посчитанные зоны уезжают (грабли дня, п. 1).
	int32 OverCell = -1; bool bOverBack = false; int32 OverScroll = -1; int32 OverBig = -1;
	for (int32 V = 0; V < kViewCells && OverCell < 0; ++V)
	{
		if (FrontUi[V].Icon && AsdaUi::IsCursorOverWidget(FrontUi[V].Icon)) { OverCell = V; bOverBack = false; }
	}
	for (int32 V = 0; V < kViewCells && OverCell < 0; ++V)
	{
		if (BackUi[V].Icon && AsdaUi::IsCursorOverWidget(BackUi[V].Icon)) { OverCell = V; bOverBack = true; }
	}
	if (OverCell < 0)
	{
		for (int32 i = 0; i < kScrollCells && OverScroll < 0; ++i)
		{
			if (ScrollCells.IsValidIndex(i) && ScrollIcon[i] && AsdaUi::IsCursorOverWidget(ScrollIcon[i])) { OverScroll = i; }
		}
	}
	if (OverCell < 0 && OverScroll < 0)
	{
		if (FrontBigIcon && AsdaUi::IsCursorOverWidget(FrontBigIcon)) { OverBig = 16045; }
		else if (BackBigIcon && AsdaUi::IsCursorOverWidget(BackBigIcon)) { OverBig = 16088; }
	}

	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;

	// ── Тултип и строка-подсказка ──
	const int32 Key = (OverCell >= 0) ? (bOverBack ? 1000 + OverCell : OverCell)
	                : (OverScroll >= 0) ? 2000 + OverScroll
	                : (OverBig > 0) ? OverBig : -1;
	if (Key != TipCell)
	{
		TipCell = Key; bTipBack = bOverBack;
		if (Tip) { Tip->HideTooltipFor(EAsdaTipOwner::ExtractShop); }
		if (OverCell >= 0)
		{
			const int32 Abs = AbsIndex(bOverBack ? BackPage : FrontPage, OverCell / kViewCols, OverCell % kViewCols);
			const TArray<FAsdaExtractEntry>& Src = bOverBack ? Back : Front;
			const FAsdaItemInstance* It = Src.IsValidIndex(Abs) ? CellItem(Src[Abs]) : nullptr;
			if (It && !It->IsEmpty())
			{
				if (Tip) { Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::ExtractShop, /*SourceKind*/1); }
				SetStatusText(bOverBack ? BackState[OverCell] : FrontState[OverCell]);
			}
		}
		else if (OverScroll >= 0)
		{
			// `petUiWin_ShowItemTooltip@0xC941B0`: тултип строится ПО КОДУ (записи слота эталон не берёт).
			const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
			const FAsdaItemInstance* It = Inv ? Inv->GetCell(1, ScrollCells[OverScroll]) : nullptr;
			if (It && Tip) { Tip->ShowItemTooltipByCode(It->ItemCode, EAsdaTipOwner::ExtractShop); }
		}
		else if (OverBig > 0)
		{
			// `itemDlgShowSlotTooltip` ветки 16045/16088 — тултип ВЫБРАННОГО предмета половины.
			const FAsdaExtractEntry* E = (OverBig == 16045) ? SelectedFront() : SelectedBack();
			const FAsdaItemInstance* It = E ? CellItem(*E) : nullptr;
			if (It && !It->IsEmpty() && Tip)
			{
				Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::ExtractShop, /*SourceKind*/1);
			}
		}
		else if (CVarClearStatusOnLeave.GetValueOnAnyThread() != 0)
		{
			// ★ ДЕФЕКТ ОРИГИНАЛА №2: уход курсора строку 16145 не чистил.
			if (TxtStatus) { TxtStatus->SetText(FText::GetEmpty()); }
		}
	}

	if (!Mouse.bLmbDown) { return; }
	if (bApplying || bBusy) { return; }   // эталон гейтит клики флагом `m_dwField1050`/`m_bBusy`
	if (OverCell >= 0)      { OnCellClicked(OverCell, bOverBack); }
	else if (OverScroll >= 0) { OnScrollCellClicked(OverScroll); }
	else if (OverBig == 16045) { OnBigIconClicked(false); }
	else if (OverBig == 16088) { OnBigIconClicked(true); }
}

// Колёса 16024 / 16074 — прокрутка на ОДИН РЯД (`itemDlgSetSlotValue@0xC90240`).
//   ⚠ В `.JUI` это невидимые «колёсные окна» (`WheelWin_Nomal`, 198×65) РОВНО поверх своей сетки —
//   поэтому половину выбираем по тому, над каким из них курсор, а не по ячейкам.
bool UAsdaExtractShopSubsystem::OnWheel(int32 Notches)
{
	if (!bOpen || !bControlsReady || bApplying || bBusy) { return false; }
	bool bBack = false; bool bAny = false;
	if (FrontWheel && AsdaUi::IsCursorOverWidget(FrontWheel)) { bBack = false; bAny = true; }
	else if (BackWheel && AsdaUi::IsCursorOverWidget(BackWheel)) { bBack = true; bAny = true; }
	if (!bAny)
	{
		for (int32 V = 0; V < kViewCells && !bAny; ++V)
		{
			if (FrontUi[V].Icon && AsdaUi::IsCursorOverWidget(FrontUi[V].Icon)) { bBack = false; bAny = true; }
		}
		for (int32 V = 0; V < kViewCells && !bAny; ++V)
		{
			if (BackUi[V].Icon && AsdaUi::IsCursorOverWidget(BackUi[V].Icon)) { bBack = true; bAny = true; }
		}
	}
	if (!bAny) { return false; }
	// «От себя» (+1) = вверх по списку = меньший ряд — то же соглашение, что у инвентаря.
	OnWheelScroll(bBack, -Notches);
	return true;
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

void UAsdaExtractShopSubsystem::OnWheelScroll(bool bBack, int32 Delta)
{
	int32& Page = bBack ? BackPage : FrontPage;
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): позиций прокрутки — по ДОСТУПНОЙ ёмкости сумки: 30 → 4 (эталон), 60 → 9 (`kPages`).
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const int32 Cap = Inv ? FMath::Min(Inv->GetContainerSize(1), kGridCells) : UAsdaInventorySubsystem::kShellSlotsLegacy;
	const int32 Pages = FMath::Clamp(FMath::DivideAndRoundUp(Cap, kViewCols) - kViewRows + 1, 1, kPages);
	const int32 New = FMath::Clamp(Page + Delta, 0, Pages - 1);
	if (New == Page) { return; }
	Page = New;
	if (bBack) { RefreshBackGrid(); } else { RefreshFrontGrid(); RefreshBackGrid(); }
}

// `itemDlg_OnSlotClick@0xC8EC40`: клик работает ТОЛЬКО по ячейке со состоянием 1.
void UAsdaExtractShopSubsystem::OnCellClicked(int32 ViewCell, bool bBack)
{
	if (!FrontState.IsValidIndex(ViewCell)) { return; }
	const int32 Row = ViewCell / kViewCols;
	const int32 Col = ViewCell % kViewCols;
	if (bBack)
	{
		if (BackState[ViewCell] != 1) { RefreshBackDetail(false); MoveBackCursor(); return; }
		SelBackRow = Row; SelBackCol = Col; SelBackPage = BackPage;
		ResultDstRecord.Reset();
		RefreshBackDetail(true);
		MoveBackCursor();
	}
	else
	{
		if (FrontState[ViewCell] != 1) { ClearSelection(); return; }
		SelFrontRow = Row; SelFrontCol = Col; SelFrontPage = FrontPage;
		RefreshFrontDetail(true);
		MoveFrontCursor();
		// Выбор источника МЕНЯЕТ пригодность всех ячеек приёмника — пересчитываем правую сетку.
		RefreshBackGrid();
	}
}

// Клик по крупной иконке: 16045 сбрасывает ВЕСЬ подбор, 16088 — только правую половину.
void UAsdaExtractShopSubsystem::OnBigIconClicked(bool bBack)
{
	if (bBack)
	{
		SelBackRow = SelBackCol = -1;
		ResultDstRecord.Reset();
		RefreshBackDetail(false);
		MoveBackCursor();
	}
	else
	{
		ClearSelection();
	}
}

// `itemDlgOnCommand@0xC902C0` case 16090.
void UAsdaExtractShopSubsystem::OnRefreshClicked()
{
	if (bBusy || bApplying) { return; }
	ClearSelection();
	RebuildAll();
	SetStatusText(-10);
}

// `itemDlgOnCommand@0xC902C0` case 16091.
void UAsdaExtractShopSubsystem::OnExtractClicked()
{
	if (bBusy || bApplying) { return; }
	if (bPending) { NoticeMsgBox(ExtractLoc::kBusyWait); return; }   // 9419
	const FAsdaExtractEntry* SrcE = SelectedFront();
	const FAsdaExtractEntry* DstE = SelectedBack();
	const FAsdaItemInstance* Src = SrcE ? CellItem(*SrcE) : nullptr;
	const FAsdaItemInstance* Dst = DstE ? CellItem(*DstE) : nullptr;
	if (!Src || Src->IsEmpty() || !Dst || Dst->IsEmpty())
	{
		SetStatusText(-10);   // 13270 «Выбери предмет, который будет извлечен.»
		return;
	}
	// Эталон читает ЧЕТЫРЕ гнезда (+16/+18/+20/+22) ИСТОЧНИКА: если хоть одно занято —
	//   предупреждение 99905 «…содержит живой камень. Он будет уничтожен. Продолжить?».
	const bool bHasGem = (Src->Sowel0 > 0) || (Src->Sowel1 > 0) || (Src->Sowel2 > 0) || (Src->Sowel3 > 0);
	if (bHasGem) { AskGemConfirm(); return; }
	// Байт записи +50 — «привязан к владельцу». Не привязан ⇒ предупреждение 109900.
	const int32 Bound = (Src->Raw.Num() > 50) ? (int32)Src->Raw[50] : 0;
	if (Bound != 0) { CheckGoldAndBoost(); }
	else            { AskBindConfirm(); }
}

void UAsdaExtractShopSubsystem::OnCancelClicked()
{
	CloseWindow();
}

// `petUiWin_OnCommand@0xC94310` ветка 16400…16405: повторный клик по той же строке СНИМАЕТ выбор.
void UAsdaExtractShopSubsystem::OnScrollCellClicked(int32 Cell)
{
	if (!ScrollCells.IsValidIndex(Cell)) { return; }
	const int32 New = ScrollCells[Cell];
	SelScrollCell = (SelScrollCell == New) ? -1 : New;
	RefreshScrollList();
}

void UAsdaExtractShopSubsystem::OnScrollCheckChanged(bool /*bChecked*/)
{
	// Состояние чекбокса читается прямо в момент отправки (`getFlagByte_p608(m_pField1123)`),
	//   отдельной модели у эталона нет — здесь только журнал.
	UE_LOG(LogAsdaExtract, Verbose, TEXT("[передача параметров] чекбокс свитка 16412 переключён"));
}

// ═════════════════════════════ ПОДТВЕРЖДЕНИЯ И СЕТЬ ═══════════════════════════════════════════

void UAsdaExtractShopSubsystem::AskGemConfirm()
{
	if (bConfirmOpen) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FString S = Loc(ExtractLoc::kGemConfirm);
	if (!WM || S.IsEmpty()) { return; }
	bConfirmOpen = true;
	// Эталон (msgbox код 0x18641): «ОК» ⇒ если предмет ещё и НЕ привязан — второе подтверждение
	//   109900, иначе сразу проверка золота.
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(S, [this](bool bYes)
	{
		bConfirmOpen = false;
		if (!bYes) { return; }
		const FAsdaExtractEntry* E = SelectedFront();
		const FAsdaItemInstance* Src = E ? CellItem(*E) : nullptr;
		const int32 Bound = (Src && Src->Raw.Num() > 50) ? (int32)Src->Raw[50] : 0;
		if (Bound != 0) { CheckGoldAndBoost(); } else { AskBindConfirm(); }
	}, /*GaugeGateSec*/ 0.f, ExtractLoc::kBtnOk, ExtractLoc::kBtnCancel, /*очередь: эталон (1, 99905) item_inventory.c:25578*/99905, 1);
}

void UAsdaExtractShopSubsystem::AskBindConfirm()
{
	if (bConfirmOpen) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FString S = Loc(ExtractLoc::kBindConfirm);
	if (!WM || S.IsEmpty()) { return; }
	bConfirmOpen = true;
	// Эталон (msgbox код 0x1AD4C): «ОК» ⇒ `exchangeOpenBoostingOrWarn`.
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(S, [this](bool bYes)
	{
		bConfirmOpen = false;
		if (bYes) { CheckGoldAndBoost(); }
	}, /*GaugeGateSec*/ 0.f, ExtractLoc::kBtnOk, ExtractLoc::kBtnCancel, /*очередь: эталон (1, 109900) item_inventory.c:25587*/109900, 1);
}

// `exchangeOpenBoostingOrWarn@0xC93D80`: цена по уровню ИСТОЧНИКА; золота не хватило ⇒ 2050.
void UAsdaExtractShopSubsystem::CheckGoldAndBoost()
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FAsdaExtractEntry* E = SelectedFront();
	const FAsdaItemInstance* Src = E ? CellItem(*E) : nullptr;
	const FAsdaItemRecord* R = (Src && IT) ? IT->GetItem(Src->ItemCode) : nullptr;
	if (!R) { return; }
	const int32 Cost = CostForLevel(R->RequiredLevel);
	const int32 Gold = Inv ? Inv->GetGold() : 0;
	// ⚠ 1:1 эталон — сравнение `цена <= золото` (клиент), сервер же отказывает при `золото <= цена`.
	if (Cost <= Gold) { StartBoost(); }
	else              { NoticeMsgBox(ExtractLoc::kNoGold); }
}

// `exchangeItemShowBoostingUI@0xC909B0` — машина заработала.
void UAsdaExtractShopSubsystem::StartBoost()
{
	if (bBusy) { return; }
	bBusy = true;
	BoostStartS = FPlatformTime::Seconds();

	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(FName(ExtractFx::kSndBoost));
	}
	// Эталон: `CControl_SetVisible(m_pProgress171, 1)` + `uiProgressBar_AnimStart(..., 16000)`.
	if (Gauge)
	{
		FVector2D Pos, Size;
		if (AsdaUi::SlotRect(Gauge, Pos, Size)) { AsdaUi::SetSlotRect(Gauge, GaugeHome, FVector2D(0.f, Size.Y)); }
		Gauge->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (GaugeBack) { GaugeBack->SetVisibility(ESlateVisibility::HitTestInvisible); }
	PlaneShow(BurnIcon);
	PlaneShow(BackIconFx);
	PlaneShow(FrontChimney);
	PlaneShow(FrontClock);
	PlaneShow(BackChimney);
	PlaneShow(BackClock);

	// ⚠ 1:1 эталон: три его цикла идут `for (i = 0; i < 3; ++i)`, а НЕ до пяти — четвёртая и пятая
	//   лампы/лампочки/эффекты не зажигаются НИКОГДА. Это его поведение, и оно воспроизведено.
	const FAsdaExtractEntry* SrcE = SelectedFront();
	const FAsdaExtractEntry* DstE = SelectedBack();
	const FAsdaItemInstance* Src = SrcE ? CellItem(*SrcE) : nullptr;
	const FAsdaItemInstance* Dst = DstE ? CellItem(*DstE) : nullptr;
	for (int32 i = 0; i < 3; ++i)
	{
		const bool bSrcOpt = Src && Src->EnchantCode.IsValidIndex(i) && Src->EnchantCode[i] > 0;
		const bool bDstOpt = Dst && Dst->EnchantCode.IsValidIndex(i) && Dst->EnchantCode[i] > 0;
		if (bSrcOpt)
		{
			PlaneShow(FrontLamps[i]);
			if (FrontOpt[i].Base) { FrontOpt[i].Base->SetVisibility(ESlateVisibility::Collapsed); }
			PlaneShow(FrontBulbs[i]);
			PlaneShow(HolyFx[i]);
		}
		else { PlaneHide(HolyFx[i]); }
		if (bDstOpt)
		{
			PlaneShow(BackLamps[i]);
			if (BackOpt[i].Base) { BackOpt[i].Base->SetVisibility(ESlateVisibility::Collapsed); }
			PlaneShow(BackBulbs[i]);
		}
	}
	UE_LOG(LogAsdaExtract, Log, TEXT("[передача параметров] машина пошла: %0.0f с до отправки 5433"), kBoostSec);
}

// `uiListUpdateScroll@0xC90670` + `CProgressBar_UpdateAnim@0x12E9400`.
void UAsdaExtractShopSubsystem::TickBoost(float /*DeltaSeconds*/)
{
	const double Elapsed = FPlatformTime::Seconds() - BoostStartS;
	const float Ratio = FMath::Clamp((float)(Elapsed / (double)kBoostSec), 0.f, 1.f);

	// Полоса 16095: у импортёра это план-заливка, ширину клипаем (закон `ui-progressbar-animate-existing`).
	if (Gauge && GaugeFullW > 0.f)
	{
		FVector2D Pos, Size;
		if (AsdaUi::SlotRect(Gauge, Pos, Size))
		{
			AsdaUi::SetSlotRect(Gauge, GaugeHome, FVector2D(GaugeFullW * Ratio, Size.Y));
		}
	}
	// Пять «святых» эффектов едут по X на `доля × 445` (константа `flt_13E9EC4` из бинаря).
	for (int32 i = 0; i < kOptRows; ++i)
	{
		if (!HolyFx[i]) { continue; }
		AsdaUi::SetSlotPos(HolyFx[i], HolyFxHome[i] + FVector2D(kFxTravelPx * Ratio, 0.f));
	}

	if (Ratio < 1.f) { return; }
	// 100 % ⇒ событие 24 ⇒ `itemCombineDlgOnButton(16095)` ⇒ отправка пакета.
	bBusy = false;
	StopBoostVisuals();
	SendExtract();
}

// `uiQuestPanelResetControls@0xC90C70` — погасить полосу и весь антураж.
void UAsdaExtractShopSubsystem::StopBoostVisuals()
{
	if (Gauge)
	{
		FVector2D Pos, Size;
		if (AsdaUi::SlotRect(Gauge, Pos, Size)) { AsdaUi::SetSlotRect(Gauge, GaugeHome, FVector2D(0.f, Size.Y)); }
		Gauge->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (GaugeBack) { GaugeBack->SetVisibility(ESlateVisibility::Collapsed); }
	PlaneHide(FrontChimney);
	PlaneHide(FrontClock);
	PlaneHide(BackChimney);
	PlaneHide(BackClock);
	for (int32 i = 0; i < kOptRows; ++i)
	{
		// Эталон: лампу гасит, а ПОДЛОЖКУ строки параметра возвращает — если параметр там есть.
		PlaneHide(FrontLamps[i]);
		PlaneHide(BackLamps[i]);
		PlaneHide(FrontBulbs[i]);
		PlaneHide(BackBulbs[i]);
		PlaneHide(HolyFx[i]);
		if (HolyFx[i]) { AsdaUi::SetSlotPos(HolyFx[i], HolyFxHome[i]); }
	}
	const FAsdaExtractEntry* SrcE = SelectedFront();
	const FAsdaExtractEntry* DstE = SelectedBack();
	const FAsdaItemInstance* Src = SrcE ? CellItem(*SrcE) : nullptr;
	const FAsdaItemInstance* Dst = DstE ? CellItem(*DstE) : nullptr;
	for (int32 i = 0; i < kOptRows; ++i)
	{
		const bool bSrcOpt = Src && Src->EnchantCode.IsValidIndex(i) && Src->EnchantCode[i] > 0;
		const bool bDstOpt = Dst && Dst->EnchantCode.IsValidIndex(i) && Dst->EnchantCode[i] > 0;
		AsdaUi::SetShownNoHit(FrontOpt[i].Base, bSrcOpt);
		AsdaUi::SetShownNoHit(BackOpt[i].Base, bDstOpt);
	}
}

// `itemExtract_sendCombineRequest@0xC921D0`.
void UAsdaExtractShopSubsystem::SendExtract()
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	if (bPending) { NoticeMsgBox(ExtractLoc::kBusyWait); return; }   // 9419 — эталонный гейт

	const FAsdaExtractEntry* SrcE = SelectedFront();
	const FAsdaExtractEntry* DstE = SelectedBack();
	const FAsdaItemInstance* Src = SrcE ? CellItem(*SrcE) : nullptr;
	const FAsdaItemInstance* Dst = DstE ? CellItem(*DstE) : nullptr;
	if (!Src || !Dst || Src->Raw.Num() < FAsdaItemInstance::kRecordSize
	           || Dst->Raw.Num() < FAsdaItemInstance::kRecordSize) { return; }
	// Эталон: «источник и приёмник — одна и та же запись» ⇒ пакет НЕ уходит вовсе.
	if (Src->SlotIndex == Dst->SlotIndex && Src->ContainerType == Dst->ContainerType) { return; }

	// Свиток попадает в пакет ТОЛЬКО при включённом чекбоксе 16412 и подтипе 96.
	TArray<uint8> ScrollRec;
	const bool bChecked = ScrollCheck && ScrollCheck->IsChecked();
	if (bChecked && SelScrollCell >= 0)
	{
		const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const FAsdaItemInstance* It = Inv ? Inv->GetCell(1, SelScrollCell) : nullptr;
		FAsdaSpecialItemlistRow Sp;
		if (It && !It->IsEmpty() && IT && IT->GetSpecialItem(It->ItemCode, Sp) && Sp.SubType == kScrollSubType)
		{
			ScrollRec = It->Raw;
		}
	}

	if (Net->SendItemOptionExtraction5433(Src->Raw, Dst->Raw, ScrollRec))
	{
		bPending = true;   // эталонное `*(this + 1051) = 1`
		UE_LOG(LogAsdaExtract, Log,
			TEXT("[передача параметров] 5433 отправлен: источник слот=%d, приёмник слот=%d, свиток=%s"),
			Src->SlotIndex, Dst->SlotIndex, ScrollRec.Num() ? TEXT("да") : TEXT("нет"));
	}
}

void UAsdaExtractShopSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок: во время разбора новые тела в очередь
	//   не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	if (TArray<TArray<uint8>> Local; PendingResults.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnExtractResult(B); }
	}
}

// `Recv_ExtractShop@0xC92AD0`.
void UAsdaExtractShopSubsystem::OnExtractResult(const TArray<uint8>& Body)
{
	// Эталон входит в разбор ТОЛЬКО при `*(this+1051) == 1`; иначе флаг просто снимается.
	if (!bPending) { return; }
	bPending = false;
	if (Body.Num() < 184) { return; }

	const int32 Code = (int32)Body[0];
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 SelfIdx = Net ? AsdaSelf::GetUserIdxOr(GI, INDEX_NONE) : -1;   // ★ волна 1 (E2): −1 до пакета 4003

	if (Code == 9)
	{
		bApplying = true;   // эталонное `m_dwField1050 = 1` — клики закрыты на время перестройки
		if (Inv) { Inv->ApplyExtractShopRecords(Body, /*bSuccess*/true); }
		// Запись приёмника — в буфер, из него правая инфо-панель показывает результат.
		ResultDstRecord.SetNumUninitialized(FAsdaItemInstance::kRecordSize);
		FMemory::Memcpy(ResultDstRecord.GetData(), Body.GetData() + 58, FAsdaItemInstance::kRecordSize);
		NoticeMsgBox(ExtractLoc::kSuccess);   // 13809

		// ★ 1:1 эталон: тексты параметров ПЕРЕЕЗЖАЮТ с левой панели на правую.
		for (int32 i = 0; i < kOptRows; ++i)
		{
			if (!FrontOpt[i].Text || !BackOpt[i].Text) { continue; }
			const FText Moved = FrontOpt[i].Text->GetText();
			if (Moved.IsEmpty()) { continue; }
			BackOpt[i].Text->SetText(Moved);
			if (BackOpt[i].Base)  { BackOpt[i].Base->SetVisibility(ESlateVisibility::HitTestInvisible); }
			FrontOpt[i].Text->SetText(FText::GetEmpty());
			if (FrontOpt[i].Base) { FrontOpt[i].Base->SetVisibility(ESlateVisibility::Collapsed); }
		}
		// Анимация уничтожения источника; её завершение (событие 24) приберёт окно.
		PlaneShow(DestructFx);
		if (Fx) { Fx->SpawnItemUseFx(SelfIdx, ExtractFx::kJebSuccess, /*SoundId*/-1, ExtractFx::kSndSuccess); }

		bApplying = false;
		RebuildAll();
		RefreshBackDetail(true);
		// ★ Страховка: если плана 16146 в собранном WBP нет (импортёр не сделал из скина
		//   `Ani_DistructnEffect` анимированный план), событие «доиграла» не придёт НИКОГДА,
		//   и окно осталось бы с горящей иконкой. Прибираем сами.
		if (!DestructFx)
		{
			UE_LOG(LogAsdaExtract, Warning,
				TEXT("[передача параметров] плана 16146 нет — прибираем окно без анимации уничтожения"));
			FinishDestructFx();
		}
	}
	else if (Code == 10)
	{
		bApplying = true;
		if (Inv) { Inv->ApplyExtractShopRecords(Body, /*bSuccess*/false); }
		NoticeMsgBox(ExtractLoc::kFail);   // 13810
		// Эталон гасит иконку горения и подсветку приёмника (анимации уничтожения НЕТ).
		PlaneHide(BurnIcon);
		PlaneHide(BackIconFx);
		if (Fx) { Fx->SpawnItemUseFx(SelfIdx, ExtractFx::kJebFail, /*SoundId*/-1, ExtractFx::kSndFail); }
		bApplying = false;
		ClearSelection();
		RebuildAll();
	}
	else
	{
		// Отказы: 0…7 → 13800…13807, всё прочее (8, 11, …) → 13808.
		const int32 LocId = (Code >= 0 && Code <= 7) ? (ExtractLoc::kResult0 + Code) : ExtractLoc::kResultOther;
		NoticeMsgBox(LocId);
		StopBoostVisuals();
		PlaneHide(BurnIcon);
		PlaneHide(BackIconFx);
	}
	RefreshGoldText();
	UE_LOG(LogAsdaExtract, Log, TEXT("[передача параметров] 5434 код=%d обработан"), Code);
}

// Ветка 16146 в `itemCombineDlgOnButton@0xC90530` — анимация уничтожения доиграла.
void UAsdaExtractShopSubsystem::FinishDestructFx()
{
	PlaneHide(DestructFx);
	PlaneHide(BurnIcon);
	PlaneHide(BackIconFx);
	for (int32 i = 0; i < kOptRows; ++i) { PlaneHide(HolyFx[i]); }
	RebuildAll();
	RefreshFrontDetail(false);
	SelFrontRow = SelFrontCol = -1;
	MoveFrontCursor();
	SelBackRow = SelBackCol = -1;
	MoveBackCursor();
	UE_LOG(LogAsdaExtract, Log, TEXT("[передача параметров] анимация 16146 доиграла — окно прибрано"));
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaExtractShopSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaExtractShopSubsystem::NoticeMsgBox(int32 LocId) const
{
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBoxLoc` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId, ExtractLoc::kBtnOk);
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaExtractShopSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно передачи параметров 16000: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W) { S += FString::Printf(TEXT("  %-40s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("16090 EXTRACT_BTN_REFRESH"), BtnRefresh);
	Line(TEXT("16091 EXTRACT_BTN_EXTRACT"), BtnExtract);
	Line(TEXT("16092 EXTRACT_BTN_CANCLE"),  BtnCancel);
	Line(TEXT("16095 EXTRACT_GAUGE (заливка)"), Gauge);
	Line(TEXT("16095 EXTRACT_GAUGE (подложка)"), GaugeBack);
	Line(TEXT("16030 EXTRACT_F_TITLE"),     TxtFrontTitle);
	Line(TEXT("16080 EXTRACT_B_TITLE"),     TxtBackTitle);
	Line(TEXT("16093 EXTRACT_TXT01"),       TxtCostLabel);
	Line(TEXT("16094 EXTRACT_TXT02"),       TxtCostValue);
	Line(TEXT("16140 EXTRACT_TXT03"),       TxtGoldLabel);
	Line(TEXT("16141 EXTRACT_TXT04"),       TxtGoldValue);
	Line(TEXT("16145 EXTRACT_Message01"),   TxtStatus);
	Line(TEXT("16045 EXTRACT_F_ICON"),      FrontBigIcon);
	Line(TEXT("16088 EXTRACT_B_ICON"),      BackBigIcon);
	Line(TEXT("16023 EXTRACT_F_SLOT_CURSOR"), FrontCursor);
	Line(TEXT("16073 EXTRACT_B_SLOTCURSOR"),  BackCursor);
	Line(TEXT("16024 EXTRACT_F_WHELL"),     FrontWheel);
	Line(TEXT("16074 EXTRACT_B_WHEEL"),     BackWheel);
	Line(TEXT("16040 EXTRACT_F_BURNICON"),  BurnIcon);
	Line(TEXT("16087 EXTRACT_B_ICONEF"),    BackIconFx);
	Line(TEXT("16146 EXTRACT_F_DistrucEffect"), DestructFx);
	Line(TEXT("16002 EXTRACT_F_CHIMN"),     FrontChimney);
	Line(TEXT("16001 EXTRACT_F_CLOCK"),     FrontClock);
	Line(TEXT("16050 EXTRACT_B_CHIMN"),     BackChimney);
	Line(TEXT("16051 EXTRACT_B_CLOCK"),     BackClock);
	Line(TEXT("16410 EXTRACT_ScrollSel"),   ScrollCursor);
	Line(TEXT("16412 EXTRACT_ScrollChk"),   ScrollCheck);
	Line(TEXT("16411 EXTRACT_ScrollTitle"), TxtScrollTitle);
	Line(TEXT("16413 EXTRACT_ScrollText"),  TxtScrollText);
	for (int32 i = 0; i < 5; ++i)
	{
		S += FString::Printf(TEXT("  %d EXTRACT_Guide%02d                       %s\n"),
			16380 + i, i, TxtGuide[i] ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 i = 0; i < kOptRows; ++i)
	{
		S += FString::Printf(TEXT("  строка параметра %d: %d подложка %s / %d текст %s | %d подложка %s / %d текст %s\n"),
			i, 16005 + i, FrontOpt[i].Base ? TEXT("есть") : TEXT("НЕТ"),
			16031 + i, FrontOpt[i].Text ? TEXT("есть") : TEXT("НЕТ"),
			16220 + i, BackOpt[i].Base ? TEXT("есть") : TEXT("НЕТ"),
			16230 + i, BackOpt[i].Text ? TEXT("есть") : TEXT("НЕТ"));
		S += FString::Printf(TEXT("  антураж %d: лампа %d %s / %d %s, лампочка %d %s / %d %s, эффект %d %s\n"),
			i, 16200 + i, FrontLamps[i] ? TEXT("есть") : TEXT("НЕТ"),
			16210 + i, BackLamps[i] ? TEXT("есть") : TEXT("НЕТ"),
			16300 + i, FrontBulbs[i] ? TEXT("есть") : TEXT("НЕТ"),
			16350 + i, BackBulbs[i] ? TEXT("есть") : TEXT("НЕТ"),
			16320 + i, HolyFx[i] ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 i = 0; i < kViewCells; ++i)
	{
		S += FString::Printf(TEXT("  ячейка %2d: %d икона %s оверлей %s | %d икона %s оверлей %s\n"), i,
			16011 + i, FrontUi[i].Icon ? TEXT("есть") : TEXT("НЕТ"), FrontUi[i].Disable ? TEXT("есть") : TEXT("НЕТ"),
			16061 + i, BackUi[i].Icon ? TEXT("есть") : TEXT("НЕТ"), BackUi[i].Disable ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 i = 0; i < kScrollCells; ++i)
	{
		S += FString::Printf(TEXT("  свиток %d: %d икона %s подпись %s\n"), i, 16400 + i,
			ScrollIcon[i] ? TEXT("есть") : TEXT("НЕТ"), ScrollCount[i] ? TEXT("есть") : TEXT("НЕТ"));
	}
	return S;
}

FString UAsdaExtractShopSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("=== окно передачи параметров: состояние ===\n")
		TEXT("  открыто=%s машина=%s применяем=%s ждём ответ=%s\n")
		TEXT("  страницы: источник=%d приёмник=%d; выбор: источник (стр=%d ряд=%d кол=%d) приёмник (стр=%d ряд=%d кол=%d)\n")
		TEXT("  цена=%d, свиток(ячейка сумки)=%d, чекбокс=%s\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bBusy ? TEXT("да") : TEXT("нет"),
		bApplying ? TEXT("да") : TEXT("нет"), bPending ? TEXT("да") : TEXT("нет"),
		FrontPage, BackPage, SelFrontPage, SelFrontRow, SelFrontCol, SelBackPage, SelBackRow, SelBackCol,
		SelCost, SelScrollCell, (ScrollCheck && ScrollCheck->IsChecked()) ? TEXT("вкл") : TEXT("выкл"));
	for (int32 i = 0; i < kGridCells; ++i)
	{
		if (Front[i].IsEmpty() && Back[i].IsEmpty()) { continue; }
		S += FString::Printf(TEXT("  [%2d] источник код=%-6d ячейка=%-3d | приёмник код=%-6d ячейка=%d\n"),
			i, Front[i].ItemCode, Front[i].CellIndex, Back[i].ItemCode, Back[i].CellIndex);
	}
	S += TEXT("  состояния видимых ячеек (источник / приёмник):\n   ");
	for (int32 i = 0; i < kViewCells; ++i) { S += FString::Printf(TEXT("%4d"), FrontState[i]); }
	S += TEXT("\n   ");
	for (int32 i = 0; i < kViewCells; ++i) { S += FString::Printf(TEXT("%4d"), BackState[i]); }
	S += TEXT("\n");
	return S;
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без NPC) ════════════════════════════
namespace
{
	UAsdaExtractShopSubsystem* ExtractSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaExtractShopSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GExtractOpen(
	TEXT("asda.Extract.Open"),
	TEXT("asda.Extract.Open — открыть окно передачи параметров 16000 так же, как код диалога NPC 30035"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaExtractShopSubsystem* S = ExtractSub(W);
		if (!S) { UE_LOG(LogAsdaExtract, Warning, TEXT("подсистема недоступна")); return; }
		S->OpenWindow();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GExtractClose(
	TEXT("asda.Extract.Close"),
	TEXT("asda.Extract.Close — закрыть окно (эталонный extractShop_Close)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaExtractShopSubsystem* S = ExtractSub(W)) { S->CloseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GExtractDump(
	TEXT("asda.Extract.Dump"),
	TEXT("asda.Extract.Dump — дамп КАЖДОГО контрола окна 16000 + обе сетки, состояния и выбор"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaExtractShopSubsystem* S = ExtractSub(W);
		if (!S) { UE_LOG(LogAsdaExtract, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaExtract, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GExtractClick(
	TEXT("asda.Extract.Click"),
	TEXT("asda.Extract.Click <ячейка 0..11> [back] — прогнать клик ТЕМ ЖЕ путём, что и мышь "
	     "(в headless-PIE мышью не кликнуть, а второй путь обработки заводить нельзя)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaExtractShopSubsystem* S = ExtractSub(W);
		if (!S) { UE_LOG(LogAsdaExtract, Warning, TEXT("подсистема недоступна")); return; }
		const int32 Cell = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		const bool bBack = A.Num() > 1 && A[1].Equals(TEXT("back"), ESearchCase::IgnoreCase);
		S->OnCellClicked(Cell, bBack);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GExtractButton(
	TEXT("asda.Extract.Button"),
	TEXT("asda.Extract.Button <refresh|extract|cancel> — нажать 16090 / 16091 / 16092"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaExtractShopSubsystem* S = ExtractSub(W);
		if (!S) { UE_LOG(LogAsdaExtract, Warning, TEXT("подсистема недоступна")); return; }
		const FString Which = A.Num() > 0 ? A[0] : FString(TEXT("extract"));
		if      (Which.Equals(TEXT("refresh"), ESearchCase::IgnoreCase)) { S->OnRefreshClicked(); }
		else if (Which.Equals(TEXT("cancel"),  ESearchCase::IgnoreCase)) { S->OnCancelClicked(); }
		else                                                             { S->OnExtractClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GExtractScroll(
	TEXT("asda.Extract.Scroll"),
	TEXT("asda.Extract.Scroll <front|back> <-1|1> — прокрутить колесо 16024 / 16074 на ряд"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaExtractShopSubsystem* S = ExtractSub(W);
		if (!S) { UE_LOG(LogAsdaExtract, Warning, TEXT("подсистема недоступна")); return; }
		const bool bBack = A.Num() > 0 && A[0].Equals(TEXT("back"), ESearchCase::IgnoreCase);
		const int32 Delta = A.Num() > 1 ? FCString::Atoi(*A[1]) : 1;
		S->OnWheelScroll(bBack, Delta);
	}));
#endif // ASDA_DEV_CONSOLE
