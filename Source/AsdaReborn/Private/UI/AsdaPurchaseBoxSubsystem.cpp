// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPurchaseBoxSubsystem.cpp — ОБЩАЯ МОДАЛКА ПОКУПКИ `SHELLITEM_PURCHASE_BOX` (1559).
//   Реверс, адреса, режимы и дефекты — в шапке `Public/UI/AsdaPurchaseBoxSubsystem.h`
//   и в отчёте `Reports/purchase_box_1559.md`. Ни одно число не выдумано.
//
// ═══════════ ТАБЛИЦА КОНТРОЛОВ (JUI IDNum → узел WBP) ═════════════════════════════════════════
//   1559 SHELLITEM_PURCHASE_BOX  SHELLITEM_PURCHASE_BOX_0_0…_0_8 + _0_DRAG   рамка 3×3 и ручка
//   1560 PURCHASE_TXT1           PURCHASE_TXT1                              «Кому: [ss1]»
//   1570 PURCHASE_TXT0           PURCHASE_TXT0                              вопрос
//   1563 PURCHASE_TITLE          PURCHASE_TITLE_Text                        имя предмета
//   1564 PURCHASE_TXT2           PURCHASE_TXT2         / 1573 PURCHASE_TXT2_Txt
//   1565 PURCHASE_TXT3           PURCHASE_TXT3         / 1574 PURCHASE_TXT3_Txt
//   1566 PURCHASE_TXT4           PURCHASE_TXT4         / 1575 PURCHASE_TXT4_Txt
//   1567 PURCHASE_TXT5           PURCHASE_TXT5         / 1576 PURCHASE_TXT5_Txt
//   1568 PURCHASE_TXT6           PURCHASE_TXT6         / 1577 PURCHASE_TXT6_Txt
//   1571 PURCHASE_TXT7           PURCHASE_TXT7         / 1578 PURCHASE_TXT7_Txt
//   1569 PURCHASE_PRICE          PURCHASE_PRICE        / 1579 PURCHASE_PRICE_Txt (+ монетка `_TxtMac`)
//   1561 PURCHASE_BTN01          PURCHASE_BTN01 + _Text    1562 PURCHASE_BTN02 + _Text
//   1572 PURCHASE_LINE1          PURCHASE_LINE1_30_0        линия, кодом не трогается
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPurchase, Log, All);

namespace
{
	const FName  kWindowId(TEXT("SHELLITEM_PURCHASE_BOX"));
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_PURCHASE_BOX");

	// ── Строки окна (`error list.bin`; все выписаны из эталона, §2.1 отчёта) ──
	constexpr int32 kLocGiftTo      = 20196;   // «Кому: [ss1]»   ★ единственный маркер окна
	constexpr int32 kLocAskGift     = 12922;   // «Хочешь послать эту вещь в подарок?»
	constexpr int32 kLocAskBuy      = 12923;   // «Хочешь купить эту вещь?»
	constexpr int32 kLocJob         = 2297;    // «Профессия»
	constexpr int32 kLocJobBase     = 8438;    // 8438 + usageCategory
	constexpr int32 kLocLevel       = 2330;    // «Уровень»
	constexpr int32 kLocStat        = 2360;    // «Эффект ЖК»
	constexpr int32 kLocOpt1        = 12927;   // «Вариант 1»
	constexpr int32 kLocOpt2        = 12928;   // «Вариант 2»
	constexpr int32 kLocNone        = 2335;    // «Нет»
	constexpr int32 kLocStoreLbl    = 12926;   // «Поместить на хранение»
	constexpr int32 kLocWarehouse   = 12924;   // «хранилище»
	constexpr int32 kLocBag         = 12925;   // «Рюкзак»
	constexpr int32 kLocPrice       = 2283;    // «Цена»
	constexpr int32 kLocQty         = 6794;    // «Кол-во» (в формате имени «%s (%d %s)»)
	constexpr int32 kLocBtnOk       = 6857;    // подпись 1561
	constexpr int32 kLocBtnCancel   = 2289;    // подпись 1562

	// Цвета значений — ровно те, что ставит `CControl_SetField923` эталона (`ida.c:593636-593644`).
	const FLinearColor kValGrey = FLinearColor(FColor(0xC0, 0xC0, 0xC0, 0xFF));   // −4144960
	const FLinearColor kValRed  = FLinearColor(FColor(0xFF, 0x00, 0x00, 0xFF));   // −65536

	// ★★★ ДЕФЕКТ ОРИГИНАЛА D3 (веерная рассылка «Подтвердить» по всем взведённым магазинам).
	//   1 = вернуть эталонное поведение (окно зовёт колбэк, а владелец сам решает, его ли это
	//   нажатие); 0 (по умолчанию) = адресный колбэк ровно того, кто окно открыл.
	//   Само эталонное «разослать всем» в порту физически невозможно — владелец у окна ровно один,
	//   поэтому ключ ЛОГИРУЕТ намерение и оставляет адресную доставку: см. `OnConfirmClicked`.
	static ASDA_DEV_CVAR(int32, CVarLegacyFanout,
		TEXT("asda.PremiumMall.LegacyPurchaseFanout"), 0,
		TEXT("1 = эталонная «веерная» доставка нажатия 1561 (дефект D3: два пакета за нажатие); "
		     "0 = адресный колбэк владельца (по умолчанию, исправление)."),
		ECVF_Default);

	// ★★★ ПОЗИЦИЯ ОКНА 1559 (бывшее «исправление D5»).
	//   Эталон считает центр по размерам ONEBUT_MSGBOX 1500 (360×140) и применяет к 1559 (290×276):
	//   окно уезжает на 35 px влево и 68 px вниз от своего центра (`CMsgBoxMgr_InitControls@0xD6AD20`, ui_core.c:55953-55961).
	//   ★ 08.10.2026 (пакет 2): РЕШЕНИЕ ВЛАДЕЛЬЦА «модальные окна — в позиции оригинала» (то же сделано у 1530 из этого же
	//   семейства) ⇒ умолчание 1 = как оригинал. Прежний ключ был МЁРТВ: при 1 он только писал Warning, а окно всё равно
	//   центрировалось по своим 290×276. 0 = прежнее [ИСПРАВЛЕНИЕ] D5 — центр по собственным габаритам.
	static ASDA_DEV_CVAR(int32, CVarLegacyCenter,
		TEXT("asda.PremiumMall.LegacyBoxCenter"), 1,
		TEXT("1 = левый-верх 1559 там же, где у ONEBUT_MSGBOX 360×140 по центру, как оригинал (умолчание с 08.10.2026, "
		     "решение владельца); 0 = [ИСПРАВЛЕНИЕ] D5 — центр по собственным 290×276."),
		ECVF_Default);

	// ★★★ ДЕФЕКТ ОРИГИНАЛА D7 («Вариант 1/2» всегда «Нет»).
	//   Эталон жёстко кладёт loc 2335 в 1576/1577 и опции предмета не читает — две из семи строк
	//   окна бесполезны всегда. 1 = оставить как в оригинале (по умолчанию: данных об опциях
	//   ПОКУПАЕМОГО (ещё не существующего) экземпляра у клиента нет, выдумывать их нельзя).
	static ASDA_DEV_CVAR(int32, CVarLegacyOptions,
		TEXT("asda.PremiumMall.LegacyBoxOptions"), 1,
		TEXT("1 = «Вариант 1/2» всегда «Нет», как оригинал (D7); 0 = скрыть обе строки."),
		ECVF_Default);

	// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 (баг игроков «Purchase and create character buttons are misplaced», парикмахерская):
	//   кнопки 1561/1562 «Купить/Отмена» уезжали от окна (на скрине игрока — к верху экрана, окно внизу).
	//   [ОРИГИНАЛ] кнопки — ДЕТИ окна 1559 (.JUI: Origin 38/149, −38, AlignYPos 8 = от НИЗА родителя), эталон двигает только
	//   окно (`CControl_SetField74_75(pPurchaseBox, …)`, ui_core.c:55961) — кнопки едут вместе с ним всегда.
	//   ПРИЧИНА В ПОРТЕ: кнопки лежат в КОРНЕВОМ канвасе WBP, и постановщик окна (`PlaceWindowKeepingSavedOffset`) при каждом
	//   показе переводит их слоты на анкор (0,0) с абсолютным смещением. А схлопывание полосы режимов D/E
	//   (`ApplyHairModeLayout`) ставило им `Top = ИСХОДНОЕ_смещение − dY` — смещение, запомненное ещё при ИМПОРТНОМ анкоре
	//   (или до сдвига окна игроком). Стоило режиму смениться после первого показа (купил в кэш-магазине → потом причёску,
	//   или наоборот) — кнопки получали «чужое» смещение под новым анкором и улетали в сторону от рамки.
	//   1 (умолчание) = узлы корневого канваса (кнопки) сдвигаются ПРИРАЩЕНИЕМ от текущей позиции на разницу дельт —
	//   так они идут за рамкой при любой постановке и сдвиге окна; при пересоздании виджета запомненные смещения сбрасываются.
	//   0 = прежнее поведение (сдвиг от запомненного исходного смещения).
	static ASDA_DEV_CVAR(int32, CVarBtnFollowFrame,
		TEXT("asda.PremiumMall.BoxBtnFollowFrame"), 1,
		TEXT("1 = кнопки 1561/1562 при схлопывании полосы режимов D/E двигаются приращением и остаются у рамки окна 1559 "
		     "(исправление 25.10.2026); 0 = прежний сдвиг от запомненного исходного смещения (кнопки уезжали после смены режима)."),
		ECVF_Default);
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaPurchaseBoxSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogAsdaPurchase, Log,
		TEXT("[покупка] общая модалка 1559 поднята: 7 режимов, колбэк адресный (исправление D3)"));
}

void UAsdaPurchaseBoxSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterEscModal(kWindowId);   // ★ 10.10.2026: роспись «модалка эталона» (см. `Open`)
			WM->CloseWindow(kWindowId);
		}
	}
	Result = nullptr;
	Widget = nullptr;
	bOpen = false;
	bReady = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaPurchaseBoxSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// `Close()` намеренно НЕ зовёт колбэк-результат («окно-владелец само закрылось»),
	// поэтому он безопасен и в обработчике разрыва.
	if (bOpen) { Close(); }
	Req = FAsdaPurchaseBoxRequest();
	Result = nullptr;
	(void)Reason;
}

UWidget* UAsdaPurchaseBoxSubsystem::DebugOkWidget() const
{
	return BtnOk.Get();
}

FString UAsdaPurchaseBoxSubsystem::DebugShownName() const
{
	return Txt1563 ? Txt1563->GetText().ToString() : FString();
}

FString UAsdaPurchaseBoxSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

bool UAsdaPurchaseBoxSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kBoxZ).Ensure(GetGameInstance(), Widget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.CloseWindow(kWindowId);
			Widget = nullptr; bReady = false; bOpen = false;
		},
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			// Окно поверх всех — иначе оно откроется ПОЗАДИ магазина, который его вызвал.
			WM.SetWindowPriority(kWindowId, true);
		},
		[]()
		{
			UE_LOG(LogAsdaPurchase, Error, TEXT("[покупка] WBP_SHELLITEM_PURCHASE_BOX не загрузился (%s)"), kWbpPath);
		},
		[this](UAsdaWindowSubsystem&)
		{
			bOpen = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

// ★★ СХЛОПНУТЬ / ВЕРНУТЬ ПУСТУЮ ПОЛОСУ СТРОК (режимы D/E — парикмахерская). Разбор — в .h.
//   Дельта НЕ ЗАШИТА ЧИСЛОМ: она равна расстоянию от ПЕРВОЙ пустой строки до разделительной линии,
//   посчитанному по ИСХОДНЫМ смещениям слотов. Поменяется раскладка `.JUI` — поедет и дельта.
void UAsdaPurchaseBoxSubsystem::ApplyHairModeLayout(bool bCollapse)
{
	if (HairEmptyRows.Num() == 0 || HairShiftDown.Num() == 0) { return; }
	if (bCollapse == bHairLayoutOn && BaseSlots.Num() > 0) { return; }   // уже в нужном состоянии

	// Запомнить ИСХОДНЫЕ смещения всех участников (один раз за жизнь окна).
	auto Remember = [this](UWidget* W)
	{
		if (!W) { return; }
		if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(W))
		{
			if (!BaseSlots.Contains(W)) { BaseSlots.Add(W, S->GetOffsets()); }
		}
	};
	for (const TObjectPtr<UWidget>& W : HairEmptyRows) { Remember(W.Get()); }
	for (const TObjectPtr<UWidget>& W : HairShiftDown) { Remember(W.Get()); }
	for (const TObjectPtr<UWidget>& W : FramePlanes)   { Remember(W.Get()); }
	Remember(TitleGroup.Get());
	Remember(DragPlane.Get());

	// ★ ДЕЛЬТА = (верх блока, который поедет) − (верх первой пустой строки), обе величины из
	//   ИСХОДНЫХ смещений. ⚠ Сравнивать можно только узлы ОДНОГО родителя: строки и линия с ценой
	//   лежат внутри `PURCHASE_TITLE_grp`, а кнопки — в корневом канвасе окна, и их Y несопоставимы.
	//   Поэтому верх блока ищем среди СОСЕДЕЙ строк (линия + группа цены), а кнопки просто едут
	//   на ту же дельту.
	const UPanelWidget* RowsParent = (HairEmptyRows.Num() > 0 && HairEmptyRows[0])
		? HairEmptyRows[0]->GetParent() : nullptr;
	float FirstRowY = TNumericLimits<float>::Max();
	for (const TObjectPtr<UWidget>& W : HairEmptyRows)
	{
		if (const FMargin* M = BaseSlots.Find(W.Get())) { FirstRowY = FMath::Min(FirstRowY, M->Top); }
	}
	float BlockTopY = TNumericLimits<float>::Max();
	for (const TObjectPtr<UWidget>& W : HairShiftDown)
	{
		if (!W || W->GetParent() != RowsParent) { continue; }   // кнопки — в другом канвасе
		if (const FMargin* M = BaseSlots.Find(W.Get())) { BlockTopY = FMath::Min(BlockTopY, M->Top); }
	}
	if (FirstRowY >= TNumericLimits<float>::Max() || BlockTopY >= TNumericLimits<float>::Max()) { return; }
	const float CollapseDY = FMath::Max(0.f, BlockTopY - FirstRowY);   // дельта схлопывания (из исходных смещений)
	const float dY = bCollapse ? CollapseDY : 0.f;
	// Дельта, которая стоит СЕЙЧАС (до этого вызова): схлопнуто — CollapseDY, иначе 0 (в т. ч. первый вызов за жизнь окна).
	const float PrevDY = bHairLayoutOn ? CollapseDY : 0.f;
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 — разбор у `CVarBtnFollowFrame`: узлы корневого канваса (кнопки) — приращением.
	const bool bFollowFrame = CVarBtnFollowFrame.GetValueOnGameThread() != 0;

	for (const TObjectPtr<UWidget>& W : HairEmptyRows)
	{
		AsdaUi::SetShownDecor(W, !bCollapse);
	}
	for (const TObjectPtr<UWidget>& W : HairShiftDown)
	{
		UCanvasPanelSlot* S = (bFollowFrame && W && W->GetParent() != RowsParent) ? AsdaUi::SlotOf(W.Get()) : nullptr;
		if (S)
		{
			// Кнопка в корневом канвасе: её анкор и смещение мог переписать постановщик окна или сдвиг игроком —
			//   двигаем от ТЕКУЩЕЙ позиции на разницу дельт (`Top = база − dY` ⇒ шаг = −(dY − PrevDY)).
			FMargin O = S->GetOffsets();
			O.Top -= (dY - PrevDY);
			S->SetOffsets(O);
			continue;
		}
		AsdaUi::ShiftSlotYFromBase(BaseSlots, W.Get(), dY);   // узлы группы заголовка: их слоты никто, кроме нас, не трогает
	}

	// Группа заголовка и рамка окна укорачиваются на ту же дельту — иначе пустота просто переедет вниз.
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(TitleGroup.Get()))
	{
		if (const FMargin* M = BaseSlots.Find(TitleGroup.Get()))
		{
			S->SetSize(FVector2D(M->Right, FMath::Max(1.f, M->Bottom - dY)));
		}
	}
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(DragPlane.Get()))
	{
		if (const FMargin* M = BaseSlots.Find(DragPlane.Get()))
		{
			S->SetSize(FVector2D(M->Right, FMath::Max(1.f, M->Bottom - dY)));
		}
	}
	if (FramePlanes.Num() == 9)
	{
		// ИСХОДНУЮ высоту рамки берём из ЗАПОМНЕННЫХ смещений (а не из текущих и не из статика):
		//   она равна «низ нижнего ряда − верх верхнего». Так возврат (dY = 0) точно восстановит
		//   ту же высоту, сколько бы раз ни переключали режимы.
		const FMargin* TopM = BaseSlots.Find(FramePlanes[0].Get());
		const FMargin* BotM = BaseSlots.Find(FramePlanes[6].Get());
		if (TopM && BotM)
		{
			const float BaseFrameH = (BotM->Top + BotM->Bottom) - TopM->Top;
			if (BaseFrameH > 0.f) { AsdaUi::StretchFramePlanesHeight(FramePlanes, BaseFrameH - dY); }
		}
	}
	bHairLayoutOn = bCollapse;
	UE_LOG(LogAsdaPurchase, Log,
		TEXT("[покупка] раскладка 1559: %s пустой полосы, дельта %.0f px (строк %d, едет узлов %d)"),
		bCollapse ? TEXT("схлопывание") : TEXT("возврат"), dY, HairEmptyRows.Num(), HairShiftDown.Num());
}

void UAsdaPurchaseBoxSubsystem::SetupControls()
{
	bReady = false;
	Relays.Reset();
	PriceGroup.Reset();
	if (!Widget || !Widget->WidgetTree) { return; }

	auto F = [this](const TCHAR* N) -> UWidget* { return AsdaUi::FindNodeByName(Widget, N, nullptr); };
	auto T = [&F](const TCHAR* N) -> UTextBlock* { return Cast<UTextBlock>(F(N)); };

	Txt1560 = T(TEXT("PURCHASE_TXT1"));
	Txt1570 = T(TEXT("PURCHASE_TXT0"));
	Txt1563 = T(TEXT("PURCHASE_TITLE_Text"));
	Lbl1564 = T(TEXT("PURCHASE_TXT2"));   Val1573 = T(TEXT("PURCHASE_TXT2_Txt"));
	Lbl1565 = T(TEXT("PURCHASE_TXT3"));   Val1574 = T(TEXT("PURCHASE_TXT3_Txt"));
	Lbl1566 = T(TEXT("PURCHASE_TXT4"));   Val1575 = T(TEXT("PURCHASE_TXT4_Txt"));
	Lbl1567 = T(TEXT("PURCHASE_TXT5"));   Val1576 = T(TEXT("PURCHASE_TXT5_Txt"));
	Lbl1568 = T(TEXT("PURCHASE_TXT6"));   Val1577 = T(TEXT("PURCHASE_TXT6_Txt"));
	Lbl1571 = T(TEXT("PURCHASE_TXT7"));   Val1578 = T(TEXT("PURCHASE_TXT7_Txt"));
	Lbl1569 = T(TEXT("PURCHASE_PRICE"));  Val1579 = T(TEXT("PURCHASE_PRICE_Txt"));
	BtnOk     = Cast<UButton>(F(TEXT("PURCHASE_BTN01")));
	BtnCancel = Cast<UButton>(F(TEXT("PURCHASE_BTN02")));
	CapOk     = T(TEXT("PURCHASE_BTN01_Text"));
	CapCancel = T(TEXT("PURCHASE_BTN02_Text"));

	// ★ Закон `ui-autowrap-canvas-gotcha`: в Canvas-слоте автоперенос ломает раскладку.
	for (UTextBlock* Tb : { Txt1560.Get(), Txt1570.Get(), Txt1563.Get(),
	                        Lbl1564.Get(), Val1573.Get(), Lbl1565.Get(), Val1574.Get(),
	                        Lbl1566.Get(), Val1575.Get(), Lbl1567.Get(), Val1576.Get(),
	                        Lbl1568.Get(), Val1577.Get(), Lbl1571.Get(), Val1578.Get(),
	                        Lbl1569.Get(), Val1579.Get() })
	{
		if (Tb) { Tb->SetAutoWrapText(false); }
	}

	// Подписи кнопок — как в `CMsgBoxMgr_InitControls` (`ida.c:588891/588893`), один раз.
	if (CapOk)     { const FString S = Loc(kLocBtnOk);     if (!S.IsEmpty()) { CapOk->SetText(FText::FromString(S)); } }
	if (CapCancel) { const FString S = Loc(kLocBtnCancel); if (!S.IsEmpty()) { CapCancel->SetText(FText::FromString(S)); } }

	// ★★★ ВСЯ ГРУППА СТРОКИ ЦЕНЫ: сама подпись 1569, значение 1579, монетка `PURCHASE_TXT4_TxtMac`
	//   и фиолетовый буллет `PURCHASE_TXT4_Bullet`. Эталон гасит ДВА контрола (1569 и 1579), но у
	//   нас видимость НЕ наследуется через группы канваса так же, как в оригинале, поэтому берём
	//   родительские группы целиком — иначе от скрытой цены остались бы монетка и буллет.
	if (UWidget* PriceGrp = F(TEXT("PURCHASE_PRICE_grp")))
	{
		PriceGroup.Add(PriceGrp);
	}
	else
	{
		// Ассет ещё не переимпортирован — гасим хотя бы точечно, как эталон.
		if (Lbl1569) { PriceGroup.Add(Lbl1569.Get()); }
		if (Val1579) { PriceGroup.Add(Val1579.Get()); }
	}

	// ── ★★ Узлы для схлопывания пустой полосы в режимах D/E (см. `ApplyHairModeLayout`) ──
	HairEmptyRows.Reset(); HairShiftDown.Reset(); FramePlanes.Reset();
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 (`CVarBtnFollowFrame`): виджет создан заново — его слоты в исходной раскладке, поэтому
	//   запомненные смещения прежнего виджета и признак «схлопнуто» к нему не относятся.
	if (CVarBtnFollowFrame.GetValueOnGameThread() != 0) { BaseSlots.Reset(); bHairLayoutOn = false; }
	for (const TCHAR* N : { TEXT("PURCHASE_TXT2_grp"), TEXT("PURCHASE_TXT3_grp"), TEXT("PURCHASE_TXT4_grp"),
	                        TEXT("PURCHASE_TXT5_grp"), TEXT("PURCHASE_TXT6_grp"), TEXT("PURCHASE_TXT7_grp") })
	{
		if (UWidget* W = AsdaUi::FindNodeExact(Widget, N)) { HairEmptyRows.Add(W); }
	}
	for (const TCHAR* N : { TEXT("PURCHASE_PRICE_grp"), TEXT("PURCHASE_BTN01_Base_grp"), TEXT("PURCHASE_BTN02_Base_grp") })
	{
		if (UWidget* W = AsdaUi::FindNodeExact(Widget, N)) { HairShiftDown.Add(W); }
	}
	// Разделительная линия — план с индексом импортёра (`PURCHASE_LINE1_<n>_0`).
	if (UWidget* W = AsdaUi::FindNodeByPrefix(Widget, TEXT("PURCHASE_LINE1_"))) { HairShiftDown.Add(W); }
	TitleGroup = AsdaUi::FindNodeExact(Widget, TEXT("PURCHASE_TITLE_grp"));
	DragPlane  = AsdaUi::FindNodeByPrefix(Widget, TEXT("SHELLITEM_PURCHASE_BOX_0_DRAG"));
	if (UPanelWidget* RootP = Cast<UPanelWidget>(Widget->GetRootWidget()))
	{
		// Девять планов рамки окна — первые девять детей корня с именем `SHELLITEM_PURCHASE_BOX_0_<k>`.
		for (int32 i = 0; i < RootP->GetChildrenCount() && FramePlanes.Num() < 9; ++i)
		{
			UWidget* Kid = RootP->GetChildAt(i);
			if (Kid && Kid->GetName().StartsWith(TEXT("SHELLITEM_PURCHASE_BOX_0_"))
			        && !Kid->GetName().EndsWith(TEXT("_DRAG")))
			{
				FramePlanes.Add(Kid);
			}
		}
	}

	// Кнопки → мостики.
	auto Bind = [this](UButton* B, int32 Idx, TFunction<void(int32)> Cb)
	{
		if (!B) { return; }
		AsdaUi::BindButton(this, B, Idx, MoveTemp(Cb), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnOk,     1561, [this](int32) { OnConfirmClicked(); });
	Bind(BtnCancel, 1562, [this](int32) { OnCancelClicked(); });

	bReady = (BtnOk != nullptr && BtnCancel != nullptr);
	if (!bReady)
	{
		UE_LOG(LogAsdaPurchase, Error,
			TEXT("[покупка] 1559: кнопка 1561 %s, 1562 %s — окно не готово"),
			BtnOk ? TEXT("есть") : TEXT("НЕТ"), BtnCancel ? TEXT("есть") : TEXT("НЕТ"));
	}
}

// ★ Порт `uiClearAllFieldTexts@0xD7D150` (`ida.c:594473`): чистятся поля 40…56, то есть
//   1560,1570,1563,1564,1565,1566,1567,1568,1569,1571,1573,1574,1575,1576,1577,1579,1578.
//   ★ 1572 (линия) и кнопки 1561/1562 НЕ ТРОГАЮТСЯ НИКОГДА — 1:1 эталон.
void UAsdaPurchaseBoxSubsystem::ClearAllFieldTexts()
{
	const FText Empty = FText::GetEmpty();
	for (UTextBlock* Tb : { Txt1560.Get(), Txt1570.Get(), Txt1563.Get(),
	                        Lbl1564.Get(), Val1573.Get(), Lbl1565.Get(), Val1574.Get(),
	                        Lbl1566.Get(), Val1575.Get(), Lbl1567.Get(), Val1576.Get(),
	                        Lbl1568.Get(), Val1577.Get(), Lbl1571.Get(), Val1578.Get(),
	                        Lbl1569.Get(), Val1579.Get() })
	{
		if (Tb) { Tb->SetText(Empty); }
	}
}

// ═══════════════════════════════ ЗАПОЛНЕНИЕ ════════════════════════════════════════════════════
void UAsdaPurchaseBoxSubsystem::Populate()
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	ClearAllFieldTexts();

	// ── Запись предмета: сперва `item_list`, затем `special_itemlist` (порядок эталона) ──
	const FAsdaItemRecord* Rec = (IT && Req.ItemCode > 0) ? IT->GetItem(Req.ItemCode) : nullptr;
	FAsdaSpecialItemlistRow Sp;
	const bool bSpecial = (!Rec && IT && Req.ItemCode > 0) ? IT->GetSpecialItem(Req.ItemCode, Sp) : false;

	auto Set = [](UTextBlock* Tb, const FString& S) { if (Tb) { Tb->SetText(FText::FromString(S)); } };

	// ★★★ РЕЖИМЫ **D** и **E** (парикмахерская: кнопка 58818 внутри окна 6000 и кнопка 15178
	//   окна 15000) заполняет ОТДЕЛЬНЫЙ эталонный заполнитель `upgradeUiFillConfirmPanel@0xD7BD90`
	//   (`ida.c:594098-594131`), и он трогает РОВНО ЧЕТЫРЕ поля:
	//       widget[41] = 1570 вопрос (loc **12923**) · widget[42] = 1563 имя (параметр `itemName`)
	//       widget[48] = 1569 метка цены (loc **2283**) · widget[55] = 1579 цена (`requiredAmount`)
	//   Всё остальное (`widget[40]`, 43…47, 49…54, 56) он ОЧИЩАЕТ пустой строкой: у причёски нет
	//   записи предмета, а значит нет ни профессии, ни уровня, ни «эффекта ЖК», ни вариантов.
	//   ⇒ в этих режимах строки «Профессия»/«Уровень»/«Эффект ЖК»/«Вариант 1/2» ДОЛЖНЫ ОСТАТЬСЯ
	//   ПУСТЫМИ. Без этого флага порт печатал бы «Уровень 0» там, где оригинал не пишет ничего.
	const bool bHairMode = (Req.Mode == EAsdaPurchaseMode::HairInShop)
	                    || (Req.Mode == EAsdaPurchaseMode::HairShop);
	// ★★ Полоса пустых строк в этих режимах схлопывается (пункт 3 владельца 2026-08-28).
	ApplyHairModeLayout(bHairMode);

	// ── 1560: «Кому: [ss1]» — ТОЛЬКО подарок. Маркер `[ss1]` заполняется через общую точку. ──
	if (Req.PanelMode == 1)
	{
		const FString S = TS ? TS->FormatById(kLocGiftTo, { Req.RecipientName }, {}) : Req.RecipientName;
		Set(Txt1560, S);
	}

	// ── 1570: вопрос ──
	Set(Txt1570, Loc(Req.PanelMode == 1 ? kLocAskGift : kLocAskBuy));

	// ── 1563: имя предмета (+ «(N Кол-во)» при количестве > 1) ──
	FString Name = Req.NameOverride;
	if (Name.IsEmpty())
	{
		if (Rec)           { Name = IT->GetItemName(Req.ItemCode); }   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
		else if (bSpecial) { Name = Sp.Name; }
	}
	if (Req.Quantity > 1 && !Name.IsEmpty())
	{
		Name = FString::Printf(TEXT("%s (%d %s)"), *Name, Req.Quantity, *Loc(kLocQty));
	}
	Set(Txt1563, Name);

	// ── 1564/1573 «Профессия». Значение — loc (8438 + usageCategory); цвет СЕРЫЙ, если предмет
	//    подходит классу игрока, иначе КРАСНЫЙ (`ida.c:593636-593639`).
	if (!bHairMode) { Set(Lbl1564, Loc(kLocJob)); }
	if (Rec && !bHairMode)
	{
		Set(Val1573, Loc(kLocJobBase + Rec->UsageCategory));
		const int32 MyJob = AsdaSelf::GetClassId(Net);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5d): класс — общая точка `AsdaSelf::GetClassId` (байт +0x438, фолбэк — запись char-select rec+24) (было сырое −1 до 4005)
		// Эталон: серый, если usageCategory == 0 ИЛИ (та же «ветвь» профессии И usageCategory <= моей).
		const bool bOk = (Rec->UsageCategory == 0) || (MyJob >= 0 && Rec->UsageCategory <= MyJob);
		if (Val1573) { Val1573->SetColorAndOpacity(FSlateColor(bOk ? kValGrey : kValRed)); }
	}

	// ── 1565/1574 «Уровень» ── (в режимах D/E эталон эту строку НЕ заполняет — см. `bHairMode`)
	if (!bHairMode)
	{
		Set(Lbl1565, Loc(kLocLevel));
		// ★★★ РЕЖИМ F (бустер-шоп): уровень приходит ОТ ВЛАДЕЛЬЦА (комбобокс 15542), а не из
		//   записи предмета — так делает эталонный заполнитель `CUpgradeUI_FillItemInfoPanelEx`
		//   (`itemLevel = sub_91FDF6(CBoosterShop)`, `ida.c:594213`). Для остальных режимов
		//   `LevelOverride` равен −1 и берётся уровень записи, как раньше.
		const int32 Need = (Req.LevelOverride >= 0)
			? Req.LevelOverride
			: (Rec ? Rec->RequiredLevel : (bSpecial ? Sp.ItemLevel : 0));
		Set(Val1574, FString::Printf(TEXT("%d"), Need));
		const int32 MyLvl = AsdaSelf::GetLevel(Net);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5c): уровень — общая точка `AsdaSelf::GetLevel` (до 4005/4036 — уровень выбранного персонажа, а не 0)
		if (Val1574) { Val1574->SetColorAndOpacity(FSlateColor(Need <= MyLvl ? kValGrey : kValRed)); }
	}

	// ── 1566/1575 «Эффект ЖК». ★ Дефект D8 оригинала: для записей `special_itemlist` НИ метка,
	//    НИ значение не заполняются — строка остаётся пустой, но ВИДИМОЙ. Оставляем 1:1: своих
	//    данных для таких предметов у клиента здесь нет (закон «не выдумывать»).
	if (Rec && !bHairMode)
	{
		Set(Lbl1566, Loc(kLocStat));
		Set(Val1575, FString::Printf(TEXT("%d ~ %d"), Rec->StatBaseLo, Rec->StatBaseHi));
	}

	// ── 1567/1576 и 1568/1577 «Вариант 1/2» — в оригинале ВСЕГДА «Нет» (дефект D7) ──
	if (CVarLegacyOptions.GetValueOnGameThread() != 0 && !bHairMode)
	{
		Set(Lbl1567, Loc(kLocOpt1)); Set(Val1576, Loc(kLocNone));
		Set(Lbl1568, Loc(kLocOpt2)); Set(Val1577, Loc(kLocNone));
	}

	// ── 1571/1578 «Поместить на хранение» — только при tradeMode 0/1 ──
	if (Req.TradeMode == 0 || Req.TradeMode == 1)
	{
		Set(Lbl1571, Loc(kLocStoreLbl));
		Set(Val1578, Loc(Req.TradeMode == 0 ? kLocWarehouse : kLocBag));
	}

	// ── 1569/1579 «Цена» ──
	int32 Price = Req.PriceOverride;
	if (Price < 0)
	{
		if (Rec)           { Price = Rec->Price; }
		else if (bSpecial) { Price = Sp.Price * FMath::Max(1, Req.Quantity); }
		else               { Price = 0; }
	}
	const bool bShowPrice = !Req.bHidePrice && Req.PanelMode != 2;
	if (bShowPrice)
	{
		Set(Lbl1569, Loc(kLocPrice));
		Set(Val1579, AsdaUi::FormatThousands(Price));
	}
	for (const TObjectPtr<UWidget>& W : PriceGroup)
	{
		AsdaUi::SetShownDecor(W, bShowPrice);
	}

	UE_LOG(LogAsdaPurchase, Log,
		TEXT("[покупка] 1559 показ: режим=%d код=%d («%s») panelMode=%d кол=%d trade=%d цена=%d%s"),
		(int32)Req.Mode, Req.ItemCode, *Name, Req.PanelMode, Req.Quantity, Req.TradeMode, Price,
		bShowPrice ? TEXT("") : TEXT(" (цена скрыта)"));
}

// ═══════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══════════════════════════════════════════
bool UAsdaPurchaseBoxSubsystem::Open(const FAsdaPurchaseBoxRequest& InReq, TFunction<void(bool)> OnResult)
{
	if (!EnsureWidget()) { return false; }

	// ★ ПРАВИЛО ВЛАДЕЛЬЦА «не защёлкивать состояние до того, как экран реально показан»: сперва
	//   заполняем и ПОКАЗЫВАЕМ, и только потом объявляем окно открытым и берём колбэк. Если WBP
	//   не загрузился, владелец не окажется в вечном ожидании ответа, которого не будет.
	Req    = InReq;
	Populate();

	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// ★ 10.10.2026 [ОРИГИНАЛ] (barber_minors): эталон показывает 1559 МОДАЛЬНО — `upgradeUiFillConfirmPanel@0xD7BD90`
	//   (item_enhance.c:2768) зовёт `helper_setupFields(g_pWindowMgr, окно, 1)`. Признака ESC у окна нет ⇒ при открытой
	//   модалке ESC не закрывает ни её, ни магазин под ней, и ESC-меню не открывается (`CWindowMgr+0x200`). Порт держит
	//   окно без флага `bModal` (своя полоса z и ввод), поэтому модальность для прохода ESC — отдельной росписью.
	//   Идемпотентно (множество).
	//   ★ 10.10.2026 (решение владельца, `owner_1010/modal_lock.md`): та же роспись запирает ВЕСЬ ввод, как флаг `+0x200`
	//   оригинала — раскладку, Alt, Enter, хотбар, системное меню, сумку; мышь отдаётся только 1559 (`asda.UI.ModalLock`).
	//   Замок пересчитывает `SetWindowVisible` ниже (показ) и в `Close`/кнопках (скрытие).
	WM->RegisterEscModal(kWindowId);
	WM->SetWindowVisible(kWindowId, true);
	WM->BringWindowToFront(kWindowId);

	// ★★★ ПОЗИЦИЯ (см. `CVarLegacyCenter`). [ОРИГИНАЛ] 08.10.2026 (пакет 2, решение владельца): левый-верх окна = левый-верх
	//   ONEBUT_MSGBOX 1500 по центру экрана (ui_core.c:55961, `CControl_SetField74_75(pPurchaseBox, centerX, centerY)`; якорь
	//   .JUI «0 0 0» ничего не добавляет) — общая точка `MsgBox1500TopLeft`. Ключ 0 — прежнее [ИСПРАВЛЕНИЕ] D5, центр по своим
	//   габаритам. Окно показывается при каждой покупке — постановка тоже при каждом показе (идемпотентна).
	// Постановщик 1559 — один и тот же при показе и при смене размера вьюпорта (`SetWindowRePlacer`, пакет 3 МЕЛК-1).
	//   ★ 08.10.2026 (пакет 4, МЕЛК-1 ревизии пакета 3): ключ `CVarLegacyCenter` читается при КАЖДОМ вызове, а не только
	//   в момент показа — переключение ключа при открытом/спрятанном окне учитывает уже ближайшая смена размера
	//   (как у 1530: его `PlaceModalByOriginal` сам проверяет свой ключ). Постановка идемпотентна.
	TWeakObjectPtr<UAsdaWindowSubsystem> WeakWM(WM);
	TWeakObjectPtr<UAsdaPurchaseBoxSubsystem> WeakSelf(this);
	TFunction<void()> Place = [WeakWM, WeakSelf]()
	{
		UAsdaWindowSubsystem* M = WeakWM.Get();
		UAsdaPurchaseBoxSubsystem* Self = WeakSelf.Get();
		if (!M || !Self) { return; }
		if (CVarLegacyCenter.GetValueOnGameThread() != 0)
		{
			// [ОРИГИНАЛ] левый-верх 1500 по центру ТЕКУЩЕГО экрана — постановка абсолютна, сама за экраном не следует.
			M->PlaceWindowKeepingSavedOffset(kWindowId, kWindowId, nullptr,
				[](const FVector2D& V, const FVector2D&) { return UAsdaWindowSubsystem::MsgBox1500TopLeft(V); });
		}
		else if (Self->Widget)
		{
			M->CenterWindowOnScreen(Self->Widget, nullptr);   // [ИСПРАВЛЕНИЕ] D5 — центр по собственным габаритам
		}
	};
	Place();
	WM->SetWindowRePlacer(kWindowId, MoveTemp(Place));
	if (CVarLegacyCenter.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaPurchase, Log, TEXT("[покупка] окно 1559 — позиция оригинала (левый-верх 1500; сдвиг от своего центра %+.0f/%+.0f)"),
			(kWinW - 360.f) * 0.5f, (kWinH - 140.f) * 0.5f);
	}

	Result = MoveTemp(OnResult);
	bOpen = true;
	++CntOpen;
	return true;
}

void UAsdaPurchaseBoxSubsystem::Close()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetWindowVisible(kWindowId, false); }
	}
	// Колбэк НЕ зовём: это «окно-владелец само закрылось», а не решение игрока.
	Result = nullptr;
	bOpen = false;
	Req = FAsdaPurchaseBoxRequest();
}

// ═══════════════════════════════ КНОПКИ ════════════════════════════════════════════════════════
void UAsdaPurchaseBoxSubsystem::OnConfirmClicked()
{
	if (!bOpen) { return; }
	++CntOk;
	// Эталон здесь играет звук 4 и уходит в цепочку из пяти независимых `if` (дефект D3).
	//   У нас — ровно один адресат.
	// ★ Волна В6 ремесла (ревизия В5 СРЕДН-2): звук 4 кнопки 1561 (ui_core.c:57991) — общей точкой звуков окон-сообщений.
	PlayButtonSfx(TEXT("SHELLITEM_PURCHASE_BOX «ОК» 1561 (ui_core.c:57991)"));
	if (CVarLegacyFanout.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaPurchase, Warning,
			TEXT("[покупка] asda.PremiumMall.LegacyPurchaseFanout=1: эталон здесь опрашивал состояние "
			     "ВСЕХ магазинов и мог отправить два пакета за нажатие (D3). Порт держит одного "
			     "владельца — доставка всё равно адресная, режим %d"), (int32)Req.Mode);
	}
	TFunction<void(bool)> Cb = MoveTemp(Result);
	Result = nullptr;
	bOpen = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetWindowVisible(kWindowId, false); }
	}
	if (Cb) { Cb(true); }
}

void UAsdaPurchaseBoxSubsystem::OnCancelClicked()
{
	if (!bOpen) { return; }
	++CntCancel;
	// ★ Волна В6 ремесла (ревизия В5 СРЕДН-2): звук 4 кнопки 1562 (ui_core.c:58037); закрытие `uiPopupClose` — без 5.
	PlayButtonSfx(TEXT("SHELLITEM_PURCHASE_BOX «Отмена» 1562 (ui_core.c:58037)"));
	// ★★★ ИСПРАВЛЕНИЕ D6. Эталон (`ida.c:592640-592643`) при «Отмена» делает ТОЛЬКО звук и скрытие:
	//   ни один из шести режимов не узнаёт об отказе, и состояние магазина остаётся взведённым.
	//   Мы зовём колбэк с `false` — владелец снимает свой «запрос в полёте».
	TFunction<void(bool)> Cb = MoveTemp(Result);
	Result = nullptr;
	bOpen = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetWindowVisible(kWindowId, false); }
	}
	if (Cb) { Cb(false); }
}

// Звук кнопки окна 1559 — общей точкой звуков окон-сообщений (`UAsdaWindowSubsystem::PlayMsgBoxSfx`), id 4.
void UAsdaPurchaseBoxSubsystem::PlayButtonSfx(const TCHAR* Why)
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->PlayMsgBoxSfx(4, Why);
	}
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaPurchaseBoxSubsystem::DumpControls() const
{
	FString S = TEXT("=== SHELLITEM_PURCHASE_BOX 1559 ===\n");
	auto Row = [&S](const TCHAR* Id, const UWidget* W)
	{
		S += FString::Printf(TEXT("  %-28s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ"));
	};
	Row(TEXT("1560 PURCHASE_TXT1"),   Txt1560);
	Row(TEXT("1570 PURCHASE_TXT0"),   Txt1570);
	Row(TEXT("1563 PURCHASE_TITLE"),  Txt1563);
	Row(TEXT("1564/1573 профессия"),  Val1573);
	Row(TEXT("1565/1574 уровень"),    Val1574);
	Row(TEXT("1566/1575 эффект ЖК"),  Val1575);
	Row(TEXT("1567/1576 вариант 1"),  Val1576);
	Row(TEXT("1568/1577 вариант 2"),  Val1577);
	Row(TEXT("1571/1578 хранение"),   Val1578);
	Row(TEXT("1569/1579 цена"),       Val1579);
	Row(TEXT("1561 PURCHASE_BTN01"),  BtnOk);
	Row(TEXT("1562 PURCHASE_BTN02"),  BtnCancel);
	S += FString::Printf(TEXT("  группа цены: %d узлов\n"), PriceGroup.Num());
	return S;
}

FString UAsdaPurchaseBoxSubsystem::DumpState() const
{
	return FString::Printf(
		TEXT("1559: открыто=%s режим=%d код=%d panelMode=%d кол=%d trade=%d | показов=%d ОК=%d отмен=%d"),
		bOpen ? TEXT("да") : TEXT("нет"), (int32)Req.Mode, Req.ItemCode, Req.PanelMode,
		Req.Quantity, Req.TradeMode, CntOpen, CntOk, CntCancel);
}
