// Copyright (c) 2026 @sitxovski (Discord)
// Реализация военного магазина — весь реверс с адресами в шапке UI/AsdaWarShopSubsystem.h
//   и в отчёте `Reports/repair_and_warshop_port.md` §2.
#include "UI/AsdaWarShopSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Login/AsdaButtonRelay.h"
#include "AsdaDataTableCache.h"
#include "Text/AsdaLangRows.h"   // [ПОРТ] 09.10.2026 (мультиязычность): текст строк таблицы на языке сессии (копия, не сама строка)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWarShop, Log, All);

// ── Константы окон ──
static const FName  kWarShopWindowId(TEXT("HONOR_itemmall_Win"));
static const TCHAR* kWarShopWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_HONOR_itemmall_Win.WBP_HONOR_itemmall_Win_C");
static const FName  kConfirmWindowId(TEXT("ICON_mssageBOX"));
static const TCHAR* kConfirmWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_ICON_mssageBOX.WBP_ICON_mssageBOX_C");

// ★★★ ВИТРИНА И ЗВАНИЯ — ДВЕ DataTable, А НЕ СЫРЫЕ `.bin` ИЗ Content.
//   Было до 2026-08-26: `FFileHelper::LoadFileToArray` двух файлов оригинала, положенных в
//   `Content/AsdaAssets/Data/Character/`. Сырым файлам в Content не место — их не видит ни
//   редактор, ни упаковщик ассетов: исходники живут в `RawAssets/Data/`, а данные — в DataTable.
//   Импортёр обеих таблиц: `Tools/asda_import_war_shop.py` (разбор формата — в его шапке).
static const TCHAR* kPriceTablePath = TEXT("/Game/AsdaAssets/Data/Character/DT_WarShopList.DT_WarShopList");
static const TCHAR* kRankTablePath  = TEXT("/Game/AsdaAssets/Data/Character/DT_WarRank.DT_WarRank");

// ── Loc-идентификаторы. ВСЕ сняты с эталонных вызовов `UI_FormatLocalizedNotice`. ──
namespace WarShopLoc
{
	constexpr int32 kCatWarrior   = 110520;   // «Воин»
	constexpr int32 kCatArcher    = 110521;   // «Лучник»
	constexpr int32 kCatMage      = 110522;   // «Маг»
	constexpr int32 kCatCommon    = 110523;   // «Обычный»
	constexpr int32 kBuyCaption   = 6857;     // «OK» — подпись кнопки покупки карточки
	constexpr int32 kCoinLabel    = 111928;   // «Монеты славы» (67335)
	constexpr int32 kRankOrAbove  = 110607;   // «%s или выше»
	constexpr int32 kVictorOnly   = 111926;   // «Этот предмет для членов победившей армии.»
	constexpr int32 kVictorRefuse = 111925;   // «Особые предметы доступны только членам…»
	constexpr int32 kNoteTitle    = 12208;    // «Примечание» — заголовок подсказки значка
	constexpr int32 kConfirmFmt   = 110600;   // «Покупка: [%s]. Стоимость: [%s] x [%d]. Продолжить?»
	constexpr int32 kBought       = 110601;   // «Предмет [%s] куплен.»
	constexpr int32 kBtnOk        = 12519;    // «OK»     (57503)
	constexpr int32 kBtnCancel    = 12864;    // «Отмена» (57504)
	// Коды ответа 6163.
	constexpr int32 kResNoItem    = 9570;     // 0
	constexpr int32 kResBagFull   = 1216;     // 2
	constexpr int32 kResWeight    = 12676;    // 3
	constexpr int32 kResNoMoney   = 12547;    // 4
	constexpr int32 kResNoPrice   = 110608;   // 5
	constexpr int32 kResNoGlory   = 110609;   // 6
	constexpr int32 kResNotSold   = 110610;   // 7
	constexpr int32 kResNotOwner  = 111926;   // 8
	constexpr int32 kResCannotBuy = 111927;   // 9
}

// ★ ПРИМЕРКА ТОВАРА НА СВОЁМ ПЕРСОНАЖЕ. Эталон делает это ВСЕГДА
//   (`charToggleEquipSlotByItem` + `charRefreshEquipSlots`), ключ нужен на случай, если игроку
//   мешает подмена внешности в мире (у нас превью-сцены внутри окна нет — см. шапку .h).
static ASDA_DEV_CVAR(int32, CVarWarShopPreview,
	TEXT("asda.WarShop.Preview"), 1,
	TEXT("Примерка товара военного магазина на своём персонаже: 1 — как в оригинале, 0 — выключить."),
	ECVF_Default);

// ★ ДЕФЕКТ ОРИГИНАЛА Д4 (`charEquipFromUniconSlot@0xB76EB0`, entity_char.c:44096-44107).
//   Слот города там считается ДО проверки `v5 == −1`, и на НЕвоенной карте эталон читает
//   `g_UniconWarFactionState[91 * (−1)]` — байты ПЕРЕД массивом. Значение мусорное, поэтому
//   «пропустит или откажет» зависит от содержимого соседней памяти.
//   Мы мусор не читаем: на невоенной карте товар «только победившей армии» НЕ выдаётся
//   (то же, что «фракция не совпала»). Откат — `asda.WarShop.LegacyOwnerGate 1` (не блокировать).
static ASDA_DEV_CVAR(int32, CVarWarShopLegacyOwnerGate,
	TEXT("asda.WarShop.LegacyOwnerGate"), 0,
	TEXT("Гейт «только победившая армия» на НЕвоенной карте: 0 — запрещать (исправление Д4), ")
	TEXT("1 — пропускать."),
	ECVF_Default);

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaWarShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Cards.SetNum(kCards);
	PreviewReal.SetNumZeroed(kEquipSlots);
	PreviewNow.SetNumZeroed(kEquipSlots);
	PreviewUndo.Init(-1, kEquipSlots);

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: колбэк только КОПИТ.
		HBuyResult = Net->OnWarShopBuyResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32 /*Container*/, int32 /*Cell*/)
		{
			// Количество «Монет славы» — часть окна; эталон перечитывает его каждой перерисовкой.
			//   Из колбэка только МЕТИМ (один ответ 6163 двигает несколько ячеек подряд).
			bCoinDirty = true;
		});
	}
	UE_LOG(LogAsdaWarShop, Log, TEXT("[военный магазин] подсистема поднята"));
}

void UAsdaWarShopSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnWarShopBuyResult.Remove(HBuyResult);
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
void UAsdaWarShopSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	ResetWidget();
	bOpen = false;
	bPending = false;                 // ждали 6163
	Category = 0; Page = 0; LastPage = 0;
	SelCard = -1; SelRow = -1; TipCard = -1;
	PreviewReal.Reset(); PreviewNow.Reset(); PreviewUndo.Reset(); PreviewApplied.Reset();
	bCoinDirty = false;
	PendingResults.Clear();
	Mouse.Reset();
}

void UAsdaWarShopSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.WarShop");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	if (!bOpen || !bControlsReady) { return; }
	if (bCoinDirty) { bCoinDirty = false; RefreshCoinText(); }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (TipCard != -1)   // висящую плашку товара/значка ГАСИМ, а не просто «не показываем»
		{
			TipCard = -1;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::WarShop);
			}
		}
		Mouse.Reset();
		return;
	}
	TickCards();
}

// ═════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════

// ★ ВИТРИНА И ЗВАНИЯ ИЗ DataTable. Раскладки исходных записей (48 б и 56 б) остались в шапке .h —
//   их разбирает ИМПОРТЁР `Tools/asda_import_war_shop.py`, а рантайм получает уже готовые строки.
//   Эталонные загрузчики: `HonorPriceTable_Load@0xB76BB0` и `CMap_LoadCampWarHonorTable@0xCA0CC0`.
//   ⚠ Ключ строки DT_WarShopList = `StorePos` (он же порядковый номер и то, что уходит в пакет
//     6162), ключ DT_WarRank = `RankIndex`. Обе таблицы читаются ПОЗИЦИОННО: `Rows`/`Ranks` —
//     плотные массивы, индекс = номер, ровно как массивы эталона. Поэтому строки таблицы
//     раскладываются по индексу из САМОЙ строки, а не по порядку обхода `GetRowMap`
//     (у `TMap` порядок не гарантирован — при обходе «как есть» витрина перемешалась бы).
bool UAsdaWarShopSubsystem::LoadTables()
{
	if (bTablesLoaded) { return Rows.Num() > 0; }
	bTablesLoaded = true;

	// ── Витрина: 140 строк, индекс = `StorePos` ──
	if (const UDataTable* Dt = AsdaDt::Cached(kPriceTablePath))
	{
		const TMap<FName, uint8*>& Map = Dt->GetRowMap();
		Rows.SetNum(Map.Num());
		int32 Bad = 0;
		for (const TPair<FName, uint8*>& It : Map)
		{
			const FAsdaWarShopRow* R = reinterpret_cast<const FAsdaWarShopRow*>(It.Value);
			if (!R) { continue; }
			if (Rows.IsValidIndex(R->StorePos)) { Rows[R->StorePos] = *R; }
			else                                { ++Bad; }
		}
		if (Bad > 0)
		{
			UE_LOG(LogAsdaWarShop, Error,
				TEXT("[военный магазин] DT_WarShopList: %d строк с StorePos вне 0…%d — витрина неполная"),
				Bad, Rows.Num() - 1);
		}
	}
	else
	{
		UE_LOG(LogAsdaWarShop, Error, TEXT("[военный магазин] нет таблицы %s — витрина пуста"), kPriceTablePath);
	}

	// ── Звания: 21 строка, индекс = `RankIndex` ──
	if (const UDataTable* Dt = AsdaDt::Cached(kRankTablePath))
	{
		const TMap<FName, uint8*>& Map = Dt->GetRowMap();
		Ranks.SetNum(Map.Num());
		for (const TPair<FName, uint8*>& It : Map)
		{
			const FAsdaWarRankRow* R = reinterpret_cast<const FAsdaWarRankRow*>(It.Value);
			if (R && Ranks.IsValidIndex(R->RankIndex))
			{
				Ranks[R->RankIndex] = *R;
				// ★ [ПОРТ] 09.10.2026 (мультиязычность): звание — на языке сессии (оверлей `WarRank`, ключ `<строка>.Title`).
				Ranks[R->RankIndex].Title = AsdaLangRows::Text(Dt, R, TEXT("Title"), R->Title);
			}
		}
	}
	else
	{
		UE_LOG(LogAsdaWarShop, Error, TEXT("[военный магазин] нет таблицы %s — званий не будет"), kRankTablePath);
	}

	UE_LOG(LogAsdaWarShop, Log, TEXT("[военный магазин] таблицы: витрина %d строк, званий %d"),
		Rows.Num(), Ranks.Num());
	return Rows.Num() > 0;
}

FString UAsdaWarShopSubsystem::RankName(int32 Grade) const
{
	// `sub_CA1530`: номер вне диапазона ⇒ запись 0 ⇒ эталон печатает ПУСТУЮ строку.
	//   (У записи 0 поле `Title` в данных и правда пустое — см. импортёр.)
	// ★ 08.11.2026 (пачка 15, баг 9): вторым читателем стало окно персонажа 900 (`CFactionUI_UpdateInfo@0xB932C0` → 926).
	//   Эталон грузит таблицу званий при старте карты (`CMap_LoadCampWarHonorTable@0xCA0CC0`), не при открытии
	//   магазина, поэтому таблицы читаются здесь лениво (`LoadTables` однократна — `bTablesLoaded`).
	if (!bTablesLoaded) { const_cast<UAsdaWarShopSubsystem*>(this)->LoadTables(); }
	return Ranks.IsValidIndex(Grade) ? Ranks[Grade].Title : FString();
}

int32 UAsdaWarShopSubsystem::CoinCount() const
{
	// Эталон (`GuildRankUI_BuildPage`, social.c:1516-1519): цикл по 60 ячейкам КОНТЕЙНЕРА 2.
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return 0; }
	int32 Sum = 0;
	const int32 N = Inv->GetContainerSize(2);
	for (int32 i = 0; i < N; ++i)
	{
		const FAsdaItemInstance* It = Inv->GetCell(2, i);
		if (It && It->ItemCode == kCoinItem) { Sum += It->Amount; }
	}
	return Sum;
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaWarShopSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWarShopWindowId, kWarShopWbp, kWarShopZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaWarShop, Error, TEXT("[военный магазин] WBP не загрузился (%s)"), kWarShopWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaWarShopSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWarShopWindowId);
		WM->CloseWindow(kWarShopWindowId);
		WM->CloseWindow(kConfirmWindowId);
	}
	Widget = nullptr;
	ConfirmWidget = nullptr;
	ConfirmIcon = nullptr; ConfirmText = nullptr; ConfirmOk = nullptr; ConfirmNo = nullptr;
	ConfirmOkCap = nullptr; ConfirmNoCap = nullptr;
	ConfirmRelays.Reset();
	IconGrid = nullptr;
	Relays.Reset();
	CatButtons.Reset();
	CatLabels.Reset();
	for (FAsdaWarShopCardUi& C : Cards) { C = FAsdaWarShopCardUi(); }
	BtnClose = BtnPrev = BtnNext = nullptr;
	TxtPage = TxtPages = TxtCoinLbl = TxtCoin = nullptr;
	bControlsReady = false;
	TipCard = -1;
	Mouse.Reset();
}

UTextBlock* UAsdaWarShopSubsystem::MakeLabelLike(UWidget* Ref, const TCHAR* NewName,
	const UTextBlock* FontDonor, bool bRightAlign)
{
	// ★ Волна 3: общая точка `AsdaShopUi::MakeLabelLike` (магазин снаряжения ≡ военный магазин, тело без изменений).
	return AsdaShopUi::MakeLabelLike(Widget, Ref, NewName, FontDonor, bRightAlign);
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`).
//   Карточки адресуются по индексам WBP `14 + 11*i` — см. отступление 1 в шапке .h.
void UAsdaWarShopSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	// ── Именованные контролы (у них `ID` в `.JUI` есть) ──
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	TMap<FString, UWidget*> ByName;
	ByName.Reserve(All.Num());
	for (UWidget* W : All) { if (W) { ByName.Add(W->GetName(), W); } }

	auto Find = [&ByName](const FString& N) -> UWidget* { UWidget** P = ByName.Find(N); return P ? *P : nullptr; };

	BtnClose  = Cast<UButton>(Find(TEXT("HONOR_item_X")));
	BtnPrev   = Cast<UButton>(Find(TEXT("HONOR_item_BTN1")));
	BtnNext   = Cast<UButton>(Find(TEXT("HONOR_item_BTN2")));
	TxtPage   = Cast<UTextBlock>(Find(TEXT("HONOR_item_p1")));
	TxtPages  = Cast<UTextBlock>(Find(TEXT("HONOR_itemp2")));
	TxtCoinLbl= Cast<UTextBlock>(Find(TEXT("HONOR_item_T1")));
	TxtCoin   = Cast<UTextBlock>(Find(TEXT("HONOR_item_T2")));

	CatButtons.SetNum(4);
	CatLabels.SetNum(4);
	for (int32 K = 0; K < 4; ++K)
	{
		CatButtons[K] = Cast<UButton>(Find(FString::Printf(TEXT("HONOR_item_Radio_OPT%d"), K)));
	}

	// ── Карточки ──
	TArray<FName> IconNames; IconNames.SetNum(kCards);
	int32 Resolved = 0;
	// ★ 10.10.2026 (`Reports/owner_1010/overlaps_fixes.md`, находка живого скана): строки столбцов карточки 0 из ассета
	//   (`HONOR_itemmall_Win_TXT00` = 67215, `_TXT01` = 67217) — образец оформления подписей остальных карточек. В `.JUI`
	//   (gamemain_jui.ini:311535/311585 и 311782/311832) у 67215+10i/67217+10i ВСЕХ карточек `TextAlign = "6"` (вправо) и
	//   `TextPos = "−4 0"`; строки импортёр создал только у карточки 0 (у неё одной образец текста). Было: подписи
	//   остальных карточек — влево от левого края хит-зоны, звание «[…] или выше» уходило под значок звания и в соседнюю
	//   карточку, а у карточки 0 строки ассета («Мл. лейтенант и выше», «500~») лежали под рантайм-подписями.
	struct FColStyle { UTextBlock* Own = nullptr; FVector2D DPos = FVector2D::ZeroVector; FVector2D DSize = FVector2D::ZeroVector; };
	FColStyle RankStyle, PriceStyle;   // столбец звания (67215) и цены (67217): строка карточки 0 и её сдвиг от хит-зоны
	// ★ 10.10.2026 (`owner_1010/esc_ranks.md`): строка ассета столбца — текстовый сосед хит-зоны в том же канвасе с ТЕМ ЖЕ
	//   правым краем и той же полосой по Y (столбец выравнен вправо, `TextAlign = "6"`). Прежний поиск «та же геометрия ±5»
	//   (`AsdaUi::FindOwnLabelLike`) терял строку звания, когда её поле в ассете расширили влево под русские звания
	//   (`HONOR_itemmall_Win_TXT00`: x −66, 166 × 15 при хит-зоне 0, 100 × 15) — и карточка 0 получала вторую подпись поверх
	//   образца ассета, а остальные — поле 100 вместо 166.
	auto FindColumnOwnLabel = [](UWidget* Zone) -> UTextBlock*
	{
		if (UTextBlock* Same = AsdaUi::FindOwnLabelLike(Zone, 5.f)) { return Same; }
		const UCanvasPanelSlot* ZS = AsdaUi::SlotOf(Zone);
		const UPanelWidget* Parent = Zone ? Zone->GetParent() : nullptr;
		if (!ZS || !Parent) { return nullptr; }
		const float ZRight = ZS->GetPosition().X + ZS->GetSize().X;
		for (int32 k = 0; k < Parent->GetChildrenCount(); ++k)
		{
			UTextBlock* Tb = Cast<UTextBlock>(Parent->GetChildAt(k));   // кандидат — текстовый сосед хит-зоны
			const UCanvasPanelSlot* S = AsdaUi::SlotOf(Tb);
			if (!S) { continue; }
			const bool bRight = FMath::IsNearlyEqual(S->GetPosition().X + S->GetSize().X, ZRight, 5.f);
			const bool bBand = FMath::IsNearlyEqual(S->GetPosition().Y, ZS->GetPosition().Y, 5.f)
			                && FMath::IsNearlyEqual(S->GetSize().Y, ZS->GetSize().Y, 5.f);
			if (bRight && bBand) { return Tb; }
		}
		return nullptr;
	};
	auto ColumnLabel = [this, FindColumnOwnLabel](UWidget* Zone, const TCHAR* NewName, FColStyle& St) -> UTextBlock*
	{
		const UCanvasPanelSlot* ZS = AsdaUi::SlotOf(Zone);
		if (UTextBlock* Own = FindColumnOwnLabel(Zone))   // строка ассета (только у карточки 0)
		{
			Own->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (!St.Own && ZS)
			{
				if (const UCanvasPanelSlot* OS = AsdaUi::SlotOf(Own))
				{
					St.Own = Own;
					St.DPos = OS->GetPosition() - ZS->GetPosition();
					St.DSize = OS->GetSize() - ZS->GetSize();
				}
			}
			return Own;
		}
		UTextBlock* Tb = MakeLabelLike(Zone, NewName, TxtCoin, true);   // `TextAlign = "6"` — вправо
		if (Tb && St.Own)
		{
			Tb->SetFont(St.Own->GetFont());
			Tb->SetColorAndOpacity(St.Own->GetColorAndOpacity());
			Tb->SetJustification(AsdaUi::ReadJustification(St.Own));
			if (UCanvasPanelSlot* TS = AsdaUi::SlotOf(Tb))   // `TextPos` контрола — тот же сдвиг, что у строки карточки 0
			{
				TS->SetPosition(TS->GetPosition() + St.DPos);
				TS->SetSize(TS->GetSize() + St.DSize);
			}
		}
		return Tb;
	};
	for (int32 i = 0; i < kCards; ++i)
	{
		const int32 B = kCardIdxBase + kCardIdxStep * i;
		FAsdaWarShopCardUi& C = Cards[i];
		C.CardParts.Reset();
		C.SelParts.Reset();

		// 67210+10i — рамка карточки: восемь планов ResizeWindow (центр у скина невидим).
		for (int32 P = 0; P < 8; ++P)
		{
			if (UWidget* W = Find(FString::Printf(TEXT("Box_White1Stroke_Ot15_%d_%d"), B, P))) { C.CardParts.Add(W); }
		}
		// 67213+10i — рамка выбора (тоже восемь планов).
		for (int32 P = 0; P < 8; ++P)
		{
			if (UWidget* W = Find(FString::Printf(TEXT("Box_White3Stroke_%d_%d"), B + 4, P))) { C.SelParts.Add(W); }
		}
		// 67211+10i — подложка имени; 67212+10i — кнопка покупки и её подпись; CirPlus — иконка кнопки.
		if (UWidget* W = Find(FString::Printf(TEXT("Box_WhiteOt20_%d_0"), B + 1)))  { C.CardParts.Add(W); C.NameZone = W; }
		// ★ 10.10.2026: образец имени из `CaptionText` («Ёнгари 2», «1231412414141») — гасим НАВСЕГДА и в `CardParts` не
		//   кладём: `ShowCard` поднимал его вместе с карточкой, и он лежал под рантайм-именем товара.
		if (UWidget* W = Find(FString::Printf(TEXT("Box_WhiteOt20_%d_txt"), B + 1))) { W->SetVisibility(ESlateVisibility::Collapsed); }
		if (UWidget* W = Find(FString::Printf(TEXT("BKBtn_W122_%d_0"), B + 2)))     { C.CardParts.Add(W); C.BuyButton = W; }
		C.BuyCaption = Cast<UTextBlock>(Find(FString::Printf(TEXT("BKBtn_W122_%d_txt"), B + 2)));
		if (C.BuyCaption) { C.CardParts.Add(C.BuyCaption); }
		if (UWidget* W = Find(FString::Printf(TEXT("WkIcon_CirPlus_%d_0"), B + 3))) { C.CardParts.Add(W); }
		// 67214+10i — иконка товара 64×64.
		if (UImage* Img = Cast<UImage>(Find(FString::Printf(TEXT("NoImage_icon_%d_0"), B + 5))))
		{
			C.ItemIcon = Img; C.CardParts.Add(Img); IconNames[i] = FName(*Img->GetName());
		}
		// 67215+10i — текст колонки 1 (звание), 67216+10i — её значок.
		C.Price1Zone = Find(FString::Printf(TEXT("NoImage_%d_HIT"), B + 6));
		if (UImage* Img = Cast<UImage>(Find(FString::Printf(TEXT("NoImage_icon_%d_0"), B + 7))))
		{
			C.RankIcon = Img; C.CardParts.Add(Img);
		}
		// 67217+10i — текст колонки 0 (цена в «Монетах славы»), 67218+10i — статичный значок монеты.
		C.Price0Zone = Find(FString::Printf(TEXT("NoImage_%d_HIT"), B + 8));
		if (UWidget* W = Find(FString::Printf(TEXT("Honor_Coin_%d_0"), B + 9))) { C.CardParts.Add(W); }
		// 67401+i — значок «только для победившей армии».
		C.Badge = Find(FString::Printf(TEXT("NEW_GFONT_Strength3_%d_0"), B + 10));

		// Рантайм-подписи по прямоугольникам хит-зон (см. отступление 2 в шапке .h).
		C.NameText   = MakeLabelLike(C.NameZone,   *FString::Printf(TEXT("WarShop_Name_%d"), i),   TxtCoin, false);
		// ★ 10.10.2026: столбцы — строкой ассета (карточка 0) или подписью в её оформлении (см. `ColumnLabel` выше).
		C.Price1Text = ColumnLabel(C.Price1Zone, *FString::Printf(TEXT("WarShop_Rank_%d"), i),  RankStyle);
		C.Price0Text = ColumnLabel(C.Price0Zone, *FString::Printf(TEXT("WarShop_Price_%d"), i), PriceStyle);
		if (C.NameText)   { C.CardParts.Add(C.NameText); }
		if (C.Price1Text) { C.CardParts.Add(C.Price1Text); }
		if (C.Price0Text) { C.CardParts.Add(C.Price0Text); }

		if (C.ItemIcon && C.BuyButton) { ++Resolved; }
		else
		{
			UE_LOG(LogAsdaWarShop, Warning,
				TEXT("[военный магазин] карточка %d (индекс WBP %d): иконка %s, кнопка %s — не разобрана"),
				i, B, C.ItemIcon ? TEXT("есть") : TEXT("НЕТ"), C.BuyButton ? TEXT("есть") : TEXT("НЕТ"));
		}
	}

	// ★ 10.10.2026: собственные подписи переключателя категорий из `.JUI` (образец «Оружие воина» у первой кнопки и пустые
	//   у остальных) — гасим, как `SHELLITEM_STORE2_MENU_Text` у магазина снаряжения: подписи у нас рантайм (ниже), и
	//   образец лежал под «Воин» (находка живого скана).
	for (const TCHAR* Cap : { TEXT("HONOR_item_Radio_Text"), TEXT("HONOR_item_Radio_Text2"), TEXT("HONOR_item_Radio_Text3"), TEXT("HONOR_item_Radio_Text4") })
	{
		if (UWidget* W = Find(Cap)) { W->SetVisibility(ESlateVisibility::Collapsed); }
	}
	// Подписи категорий — четыре рантайм-`TextBlock` над кнопками (см. отступление 3 в шапке .h).
	static const int32 kCatLoc[4] = { WarShopLoc::kCatWarrior, WarShopLoc::kCatArcher,
	                                  WarShopLoc::kCatMage,    WarShopLoc::kCatCommon };
	for (int32 K = 0; K < 4; ++K)
	{
		if (!CatButtons[K]) { continue; }
		CatLabels[K] = MakeLabelLike(CatButtons[K], *FString::Printf(TEXT("WarShop_Cat_%d"), K), TxtCoin, false);
		if (CatLabels[K])
		{
			CatLabels[K]->SetJustification(ETextJustify::Center);
			const FString S = Loc(kCatLoc[K]);
			if (!S.IsEmpty()) { CatLabels[K]->SetText(FText::FromString(S)); }
		}
	}

	// ── Иконки товара через ОБЩУЮ точку (закон `port-shared-points`) ──
	//   Клик ловим САМИ по живой геометрии (`TickCards`).
	IconGrid = NewObject<UAsdaIconCellGrid>(this);
	IconGrid->SetCellSizePx(64.f);          // `.JUI`: Size = "64 64" у 67214+10i
	IconGrid->SetClickOverlayEnabled(false);
	// Эталон `GuildRankUI_BuildPage` перед заполнением ЗАТИРАЕТ иконку каждой карточки пустой
	//   текстурой 32×32 — иначе на неполной последней странице остаются картинки предыдущей.
	IconGrid->bClearEmptyCells = true;
	IconGrid->SetupNamed(Widget, IconNames, /*bMale*/true,
		[this](int32 Card) -> int32
		{
			return (Cards.IsValidIndex(Card) && PageRows.IsValidIndex(Card) && Rows.IsValidIndex(PageRows[Card]))
				? Rows[PageRows[Card]].ItemCode : 0;
		},
		[this](int32 Card) { OnCardIconClicked(Card); });

	// ── Кнопки ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose, 67349, [this](int32) { CloseWindow(); });
	Bind(BtnPrev,  67333, [this](int32) { SetPage(Page - 1); });
	Bind(BtnNext,  67334, [this](int32) { SetPage(Page + 1); });
	for (int32 K = 0; K < 4; ++K)
	{
		Bind(CatButtons[K], 67202, [this, K](int32) { SetCategory(K); });
	}

	if (TxtCoinLbl) { const FString S = Loc(WarShopLoc::kCoinLabel); if (!S.IsEmpty()) { TxtCoinLbl->SetText(FText::FromString(S)); } }

	bControlsReady = (Resolved == kCards) && (BtnClose != nullptr);
	UE_LOG(LogAsdaWarShop, Log,
		TEXT("[военный магазин] окно 67200 разобрано: карточек %d/%d, кнопки X/BTN1/BTN2 = %s/%s/%s, ")
		TEXT("категорий %d, страницы %s/%s, валюта %s"),
		Resolved, kCards,
		BtnClose ? TEXT("да") : TEXT("НЕТ"), BtnPrev ? TEXT("да") : TEXT("НЕТ"), BtnNext ? TEXT("да") : TEXT("НЕТ"),
		(CatButtons[0] ? 1 : 0) + (CatButtons[1] ? 1 : 0) + (CatButtons[2] ? 1 : 0) + (CatButtons[3] ? 1 : 0),
		TxtPage ? TEXT("да") : TEXT("НЕТ"), TxtPages ? TEXT("да") : TEXT("НЕТ"),
		TxtCoin ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaWarShopSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWarShopWindowId, this, Widget, bOpen, TEXT("HONOR_itemmall_Win_0_%d"), 9);
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

void UAsdaWarShopSubsystem::OpenWindow()
{
	if (!LoadTables())
	{
		UE_LOG(LogAsdaWarShop, Error, TEXT("[военный магазин] витрина не прочитана — окно не открываем"));
		return;
	}
	if (!EnsureWidget()) { return; }
	if (bOpen) { return; }

	bOpen = true;
	bPending = false;
	Page = 0;
	SelCard = -1;
	SelRow  = -1;
	TipCard = -1;
	SnapshotEquip();
	BuildPage();
	RefreshCoinText();
	HideConfirm();

	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWarShopWindowId, true);
	}
	UE_LOG(LogAsdaWarShop, Log, TEXT("[военный магазин] окно открыто: категория %d, строк %d"), Category, Rows.Num());
}

void UAsdaWarShopSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	TipCard = -1;
	HideConfirm();
	RestorePreview();                 // `honorShopBackupSlots` + `charRefreshEquipSlots`
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::WarShop);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWarShopWindowId, false);
	}
	UE_LOG(LogAsdaWarShop, Log, TEXT("[военный магазин] окно закрыто"));
}

// ═════════════════════════════ СТРАНИЦА ═══════════════════════════════════════════════════════

void UAsdaWarShopSubsystem::ShowCard(int32 Card, bool bVisible)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	const ESlateVisibility V = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	for (const TObjectPtr<UWidget>& W : Cards[Card].CardParts) { if (W) { W->SetVisibility(V); } }
	if (!bVisible) { ShowCardSelection(Card, false); }
}

void UAsdaWarShopSubsystem::ShowCardSelection(int32 Card, bool bVisible)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	AsdaShopUi::SetSelectionVisible(Cards[Card].SelParts, bVisible);   // ★ волна 3: общая точка рамки выбора
}

// `GuildRankUI_BuildPage@0xB75DD0` — отбор строк по категории и городу, раскладка по 12 карточкам.
void UAsdaWarShopSubsystem::BuildPage()
{
	// ★ СОСТОЯНИЕ «какая вкладка активна» — ОБЩАЯ ТОЧКА (закон `port-shared-points`).
	//   ★ У ЭТОГО окна скин вкладок ТЁМНЫЙ, а подписи — рантайм-`TextBlock` (`MakeLabelLike`), то есть
	//     ЧИСТО БЕЛЫЕ. Общая точка отрабатывает ЗЕРКАЛЬНЫЙ случай (правка 2026-08-27): активная
	//     остаётся БЕЛОЙ, а неактивные ПРИГЛУШАЮТСЯ — иначе при белом базовом цвете активную
	//     было бы не отличить. См. `TabCaptionActiveColor`/`TabCaptionInactiveColor`.
	AsdaUi::ApplyTabCaptionState(CatLabels, Category, CatIdleColors);
	PageRows.Reset();
	for (int32 i = 0; i < kCards; ++i)
	{
		ShowCard(i, false);
		Cards[i].RowIndex = -1;
	}

	// Фильтр города: тот же слот, что у налога (общая точка `AsdaUi::WarCitySlot`).
	const int32 RegionId = AsdaUi::WarCitySlot(GetGameInstance());
	const int32 PageStart = kCards * Page;
	int32 MatchCount = 0;

	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	for (int32 j = 0; j < Rows.Num(); ++j)
	{
		const FAsdaWarShopRow& R = Rows[j];
		if (R.Category != Category) { continue; }
		if (R.RegionFilter != -1 && R.RegionFilter != RegionId) { continue; }

		const int32 DisplayRow = MatchCount - PageStart;
		if (DisplayRow >= 0 && DisplayRow < kCards)
		{
			FAsdaWarShopCardUi& C = Cards[DisplayRow];
			C.RowIndex = j;
			PageRows.Add(j);
			ShowCard(DisplayRow, true);
			// Имя товара.
			if (C.NameText)
			{
				FString Name = IT ? IT->GetItemName(R.ItemCode) : FString();
				if (Name.IsEmpty()) { Name = FString::FromInt(R.ItemCode); }
				C.NameText->SetText(FText::FromString(Name));
			}
			// Подпись кнопки покупки — нотис 6857 (эталон ставит её в `UI_HonorShopDlg_Init`).
			if (C.BuyCaption) { const FString S = Loc(WarShopLoc::kBuyCaption); if (!S.IsEmpty()) { C.BuyCaption->SetText(FText::FromString(S)); } }
			// Колонка 0 — цена ПРЕДМЕТОМ: «{имя цены} {количество} ».
			if (C.Price0Text)
			{
				if (R.PriceType0 == 1)
				{
					FString PriceName = IT ? IT->GetItemName(R.PriceCode0) : FString();
					if (PriceName.IsEmpty()) { PriceName = FString::FromInt(R.PriceCode0); }
					C.Price0Text->SetText(FText::FromString(FString::Printf(TEXT("%s %d "), *PriceName, R.PriceQty0)));
				}
				else { C.Price0Text->SetText(FText::GetEmpty()); }
			}
			// Колонка 1 — требование ЗВАНИЯ: «[Название] {110607}» + значок клетки `Grade−1`.
			if (C.Price1Text)
			{
				if (R.PriceType1 == 2)
				{
					const FString RName = RankName(R.PriceQty1);
					// ★ 10.10.2026 [ИСПРАВЛЕН ДЕФЕКТ ОРИГИНАЛА]: эталон (social.c:1481-1482) печатает `sprintf("[%s]%s", звание,
					//   loc 110607)`, а строка 110607 в RU и EN сама содержит формат («%s или выше» / «%s or above»,
					//   `_sources_GameText.tsv`: ORIG) — на экране было «[Private 1st]%s или выше». Если в строке есть `%s`,
					//   звание в скобках подставляется в неё; без `%s` — склейка эталона.
					const FString Suffix = Loc(WarShopLoc::kRankOrAbove);
					const FString Bracketed = FString::Printf(TEXT("[%s]"), *RName);
					C.Price1Text->SetText(RName.IsEmpty()
						? FText::GetEmpty()
						: FText::FromString(Suffix.Contains(TEXT("%s"))
							? AsdaUi::FormatPrintfArgs(Suffix, { Bracketed })
							: Bracketed + Suffix));
					if (C.RankIcon)
					{
						if (R.PriceQty1 > 0)
						{
							// `uiGrid_CellPosFromIndex@0xCA1C20`: сетка 8 колонок по 24 px, индекс = звание−1.
							const int32 Idx = R.PriceQty1 - 1;
							AsdaUi::ApplyJuiIconCell(C.RankIcon, TEXT("Faction_Grade"),
								24 * (Idx % 8), 24 * (Idx / 8), 24, 24.f);
							C.RankIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
						}
						else { C.RankIcon->SetVisibility(ESlateVisibility::Collapsed); }
					}
				}
				else { C.Price1Text->SetText(FText::GetEmpty()); }
			}
			// Значок «только для победившей армии» — виден при поле записи +40 == 1.
			if (C.Badge)
			{
				AsdaUi::SetShownNoHit(C.Badge, R.VictorOnly == 1);
			}
			ShowCardSelection(DisplayRow, SelCard == DisplayRow);
		}
		++MatchCount;
	}

	LastPage = FMath::Max(0, (MatchCount - 1) / kCards);
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`GuildRankUI_BuildPage@0xB75DD0`, social.c:1504/1506).
	if (TxtPage)  { TxtPage->SetText(FText::FromString(FString::FromInt(Page + 1))); }
	if (TxtPages) { TxtPages->SetText(FText::FromString(FString::FromInt(LastPage + 1))); }
	if (IconGrid) { IconGrid->Refresh(); }
	RefreshCoinText();
}

void UAsdaWarShopSubsystem::RefreshCoinText()
{
	// ★ ДЕФЕКТ ОРИГИНАЛА Д1: эталон пишет в 67336 ДВАЖДЫ — сперва золото, тут же количество
	//   «Монет славы». Первая запись мертва, видимое поведение — вторая; её и делаем.
	if (TxtCoin) { TxtCoin->SetText(FText::FromString(AsdaUi::FormatThousands(CoinCount()))); }
	if (TxtCoinLbl) { const FString S = Loc(WarShopLoc::kCoinLabel); if (!S.IsEmpty()) { TxtCoinLbl->SetText(FText::FromString(S)); } }
}

void UAsdaWarShopSubsystem::SetPage(int32 NewPage)
{
	// Эталон 67333: `if (--page < 0) page = 0;`  67334: `if (++page > last) page = last;`
	Page = FMath::Clamp(NewPage, 0, LastPage);
	BuildPage();
}

void UAsdaWarShopSubsystem::SetCategory(int32 Cat)
{
	// Эталон (66817 по 67202): сброс выбора, категория, страница 0, перестроение.
	if (Cat < 0 || Cat > 3) { return; }
	SelCard = -1;
	Category = Cat;
	Page = 0;
	BuildPage();
}

// ═════════════════════════════ ПРИМЕРКА ═══════════════════════════════════════════════════════

// `honorShopFillSlotsFromBag@0xB767C0` + хвост `uiHonorShopDlgShow@0xB768D0`:
//   реальный снимок надетого кладётся И в «оригинал», И в превью, откат обнуляется.
void UAsdaWarShopSubsystem::SnapshotEquip()
{
	PreviewReal.Init(0, kEquipSlots);
	PreviewUndo.Init(-1, kEquipSlots);
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Inv)
	{
		for (int32 S = 0; S < kEquipSlots; ++S)
		{
			const FAsdaItemInstance* It = Inv->GetCell(3, S);
			PreviewReal[S] = (It && !It->IsEmpty()) ? It->ItemCode : 0;
		}
	}
	PreviewNow = PreviewReal;
	PreviewApplied = PreviewReal;   // модель уже в этом состоянии — толкать нечего
}

// `charToggleEquipSlotByItem@0xB77380`: индекс превью = `ItemRecord->itemType`
//   (гейт `itemType <= 4 || 8…9`; записям второй таблицы с subType 15 отведён индекс 10).
void UAsdaWarShopSubsystem::TogglePreview(int32 ItemCode)
{
	if (CVarWarShopPreview.GetValueOnGameThread() == 0 || ItemCode <= 0) { return; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	int32 Type = -1;
	if (const FAsdaItemRecord* R = IT->GetItem(ItemCode))
	{
		if (R->ItemType <= 4 || (R->ItemType >= 8 && R->ItemType <= 9)) { Type = R->ItemType; }
	}
	else
	{
		FAsdaSpecialItemlistRow Sp;
		if (IT->GetSpecialItem(ItemCode, Sp) && Sp.SubType == 15) { Type = 10; }   // транспорт
	}
	if (!PreviewNow.IsValidIndex(Type)) { return; }

	if (PreviewNow[Type] == ItemCode)
	{
		// Снять примерку — вернуть то, что было (эталон: `preview = undo; undo = -1`).
		PreviewNow[Type] = (PreviewUndo[Type] >= 0) ? PreviewUndo[Type] : 0;
		PreviewUndo[Type] = -1;
	}
	else
	{
		PreviewUndo[Type] = PreviewNow[Type];
		PreviewNow[Type] = ItemCode;
	}
	ApplyPreview();
}

// `charRefreshEquipSlots@0xB77530` — раскатать набор превью на модель своего персонажа.
//   У нас это ОБЩАЯ точка `UAsdaWorldSubsystem::ApplySelfEquipPart` (закон `port-shared-points`).
void UAsdaWarShopSubsystem::ApplyPreview()
{
	if (CVarWarShopPreview.GetValueOnGameThread() == 0) { return; }
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return; }
	// Части, которые эталон трогает: 0…4 (шлем/грудь/штаны/обувь/перчатки), 8 щит, 9 оружие.
	//   ★ Толкаем в модель ТОЛЬКО ИЗМЕНИВШИЕСЯ части: `ApplySelfEquipPart` пересобирает меш,
	//   а эталонная перерисовка в оригинале идёт по D3D-слою и стоит дёшево.
	static const int32 kParts[] = { 0, 1, 2, 3, 4, 8, 9 };
	if (PreviewApplied.Num() != PreviewNow.Num()) { PreviewApplied.Init(-1, PreviewNow.Num()); }
	for (int32 T : kParts)
	{
		if (!PreviewNow.IsValidIndex(T)) { continue; }
		if (PreviewApplied[T] == PreviewNow[T]) { continue; }
		PreviewApplied[T] = PreviewNow[T];
		WS->ApplySelfEquipPart(T, PreviewNow[T]);
	}
}

// `uiHonorShopDlgOpen@0xB76AA0` (это ЗАКРЫТИЕ) → `honorShopBackupSlots@0xB76870`:
//   превью восстанавливается из снимка реального надетого.
void UAsdaWarShopSubsystem::RestorePreview()
{
	if (CVarWarShopPreview.GetValueOnGameThread() == 0) { return; }
	PreviewNow = PreviewReal;
	PreviewUndo.Init(-1, kEquipSlots);
	ApplyPreview();
}

// ═════════════════════════════ ПОКАДРОВЫЕ ОПРОСЫ ══════════════════════════════════════════════

void UAsdaWarShopSubsystem::TickCards()
{
	// Клик по иконке и по кнопке покупки ловим ПО ЖИВОЙ ГЕОМЕТРИИ: у карточек в `.JUI` нет `ID`,
	//   поэтому кнопок-оверлеев для них нет вовсе.
	int32 OverIconCard = -1, OverBuyCard = -1, OverBadgeCard = -1;
	for (int32 i = 0; i < kCards; ++i)
	{
		if (Cards[i].RowIndex < 0) { continue; }
		if (OverIconCard < 0 && Cards[i].ItemIcon && AsdaUi::IsCursorOverWidget(Cards[i].ItemIcon)) { OverIconCard = i; }
		if (OverBuyCard  < 0 && Cards[i].BuyButton && AsdaUi::IsCursorOverWidget(Cards[i].BuyButton)) { OverBuyCard = i; }
		if (OverBadgeCard < 0 && Cards[i].Badge
			&& Cards[i].Badge->GetVisibility() != ESlateVisibility::Collapsed
			&& AsdaUi::IsCursorOverWidget(Cards[i].Badge)) { OverBadgeCard = i; }
	}

	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;

	// Подсказки: значок «победившей армии» (12208 + 111926) и тултип товара.
	const int32 TipTarget = (OverBadgeCard >= 0) ? (1000 + OverBadgeCard) : OverIconCard;
	if (TipTarget != TipCard)
	{
		TipCard = TipTarget;
		if (Tip)
		{
			if (OverBadgeCard >= 0)
			{
				FAsdaTipMetrics M; M.FontPx = 8; M.MaxW = 218.f;
				Tip->ShowTextTip(EAsdaTipOwner::WarShop, Loc(WarShopLoc::kNoteTitle), Loc(WarShopLoc::kVictorOnly), M);
			}
			else if (OverIconCard >= 0 && Rows.IsValidIndex(Cards[OverIconCard].RowIndex))
			{
				// `uiEquip_OnSlotHoverTooltip@0xB778C0` — обычный тултип предмета по коду.
				Tip->ShowItemTooltipByCode(Rows[Cards[OverIconCard].RowIndex].ItemCode, EAsdaTipOwner::WarShop);
			}
			else
			{
				Tip->HideTooltipFor(EAsdaTipOwner::WarShop);
			}
		}
	}

	if (!Mouse.bLmbDown) { return; }
	if (OverBuyCard >= 0)      { OnCardBuyClicked(OverBuyCard); }
	else if (OverIconCard >= 0) { OnCardIconClicked(OverIconCard); }
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

// Эталон, сообщение 0xB по `67214+10i` (`guildRankUiOnEvent`, social.c:1572-1585):
//   выбрать карточку, примерить товар, перестроить страницу.
void UAsdaWarShopSubsystem::OnCardIconClicked(int32 Card)
{
	if (!Cards.IsValidIndex(Card) || Cards[Card].RowIndex < 0) { return; }
	SelCard = Card;
	TogglePreview(Rows[Cards[Card].RowIndex].ItemCode);
	BuildPage();
}

// `charEquipFromUniconSlot@0xB76EB0` — клик по КНОПКЕ покупки карточки.
void UAsdaWarShopSubsystem::OnCardBuyClicked(int32 Card)
{
	if (!Cards.IsValidIndex(Card) || Cards[Card].RowIndex < 0) { return; }
	const FAsdaWarShopRow& R = Rows[Cards[Card].RowIndex];

	if (R.VictorOnly == 1)
	{
		// Гейт «только победившая армия»: город текущей карты должен принадлежать своей фракции.
		const int32 Slot = AsdaUi::WarCitySlot(GetGameInstance());
		bool bAllowed = false;
		if (Slot < 0)
		{
			// ★ ДЕФЕКТ Д4: эталон здесь читает память ЗА массивом; мы отказываем (см. CVar выше).
			bAllowed = (CVarWarShopLegacyOwnerGate.GetValueOnGameThread() != 0);
		}
		else
		{
			int32 Pct = 0; bool bNeutral = true;
			bAllowed = AsdaUi::WarCityOwnedBySelf(GetGameInstance(), Slot, Pct, bNeutral);
		}
		if (!bAllowed)
		{
			NoticeMsgBox(WarShopLoc::kVictorRefuse);
			return;
		}
	}

	SelCard = Card;
	SelRow  = Cards[Card].RowIndex;
	ShowConfirm(SelRow);                       // `itemTooltipBuildText@0xB76D00`
	TogglePreview(R.ItemCode);                 // эталон примеряет ТУТ ЖЕ
	BuildPage();
}

// ═════════════════════════════ ОКНО ПОДТВЕРЖДЕНИЯ 57500 ═══════════════════════════════════════

bool UAsdaWarShopSubsystem::EnsureConfirmWidget()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(ConfirmWidget) && ConfirmOk) { return true; }

	ConfirmWidget = WM->OpenWindowByPath(kConfirmWindowId, kConfirmWbp, kConfirmZ, ESlateVisibility::SelfHitTestInvisible);
	if (!ConfirmWidget)
	{
		UE_LOG(LogAsdaWarShop, Error, TEXT("[военный магазин] WBP_ICON_mssageBOX не загрузился"));
		return false;
	}
	// ★ У контрола 57501 (`NoImage_icon`, скин-тип `Text`) импортёр создал ДВА виджета: пустой
	//   `TextBlock` с точным именем idc и план-`UImage` `{idc}_{juiIdx}_0`. Нужен именно план,
	//   поэтому идём по дереву и разбираем имя (закон `jui-importer-widget-naming`).
	ConfirmIcon = nullptr;
	if (ConfirmWidget->WidgetTree)
	{
		TArray<UWidget*> AllC;
		ConfirmWidget->WidgetTree->GetAllWidgets(AllC);
		for (UWidget* W : AllC)
		{
			UImage* Img = Cast<UImage>(W);
			if (!Img) { continue; }
			FString Idc; int32 Ji = 0, Pi = 0;
			if (AsdaUi::ParsePlaneName(Img->GetName(), Idc, Ji, Pi) && Idc == TEXT("ICON_mssageBOX_Mark"))
			{
				ConfirmIcon = Img;
				break;
			}
		}
	}
	ConfirmText  = Cast<UTextBlock>(AsdaUi::FindNodeExact(ConfirmWidget, TEXT("ICON_mssageBOX_TEXT")));
	ConfirmOk    = Cast<UButton>(AsdaUi::FindNodeExact(ConfirmWidget, TEXT("ICON_mssageBOX_OK")));
	ConfirmNo    = Cast<UButton>(AsdaUi::FindNodeExact(ConfirmWidget, TEXT("ICON_mssageBOX_NO")));
	ConfirmOkCap = Cast<UTextBlock>(AsdaUi::FindNodeExact(ConfirmWidget, TEXT("ICON_mssageBOX_OK_Text")));
	ConfirmNoCap = Cast<UTextBlock>(AsdaUi::FindNodeExact(ConfirmWidget, TEXT("ICON_mssageBOX_NO_Text")));

	ConfirmRelays.Reset();
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), ConfirmRelays);   // ★ U20: реле клика — общая точка
	};
	Bind(ConfirmOk, 57503, [this](int32) { OnConfirmOk(); });
	Bind(ConfirmNo, 57504, [this](int32) { OnConfirmCancel(); });
	// Подписи кнопок — нотисы 12519 / 12864 (эталон ставит их в `UI_HonorShopDlg_Init`).
	if (ConfirmOkCap) { const FString S = Loc(WarShopLoc::kBtnOk);     if (!S.IsEmpty()) { ConfirmOkCap->SetText(FText::FromString(S)); } }
	if (ConfirmNoCap) { const FString S = Loc(WarShopLoc::kBtnCancel); if (!S.IsEmpty()) { ConfirmNoCap->SetText(FText::FromString(S)); } }

	WM->SetWindowVisible(kConfirmWindowId, false);
	UE_LOG(LogAsdaWarShop, Log, TEXT("[военный магазин] окно подтверждения 57500 разобрано: текст %s, OK %s, Отмена %s"),
		ConfirmText ? TEXT("есть") : TEXT("НЕТ"), ConfirmOk ? TEXT("есть") : TEXT("НЕТ"), ConfirmNo ? TEXT("есть") : TEXT("НЕТ"));
	return ConfirmOk != nullptr;
}

// `itemTooltipBuildText@0xB76D00`: иконка товара, текст 110600 («товар», «цена», «количество»).
void UAsdaWarShopSubsystem::ShowConfirm(int32 RowIndex)
{
	if (!Rows.IsValidIndex(RowIndex) || !EnsureConfirmWidget()) { return; }
	// ★ 1:1 эталон `itemTooltipBuildText@0xB76D00`: содержимое строится, ТОЛЬКО пока окно СКРЫТО
	//   (`if (LOBYTE((*(this + 189))->isVisible) != 1)`). Повторный клик по другой карточке при
	//   открытом окне меняет ВЫБРАННУЮ СТРОКУ, но не текст — это поведение оригинала, не наш баг.
	if (UAsdaWindowSubsystem* WMv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WMv->IsWindowVisible(kConfirmWindowId)) { return; }
	}
	const FAsdaWarShopRow& R = Rows[RowIndex];
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	FString ItemName  = IT ? IT->GetItemName(R.ItemCode)   : FString();
	FString PriceName = IT ? IT->GetItemName(R.PriceCode0) : FString();
	if (ItemName.IsEmpty())  { ItemName  = FString::FromInt(R.ItemCode); }
	if (PriceName.IsEmpty()) { PriceName = FString::FromInt(R.PriceCode0); }
	if (ConfirmText)
	{
		ConfirmText->SetText(FText::FromString(AsdaUi::FormatPrintfArgs(
			Loc(WarShopLoc::kConfirmFmt), { ItemName, PriceName, FString::FromInt(R.PriceQty0) })));
	}
	// Иконка товара — та же общая точка, что у сеток (атлас + UV клетки).
	// ★ Дедуп №2, В1-B (U4c, 11.10.2026): `GetItemIcon` + `ApplyJuiIconCell` → общая точка иконки предмета
	//   `AsdaUi::ApplyItemIcon` (кисть 64×64, заворот по модулю включён — как было). Отличие загрузчиков (`LoadUITexture`
	//   срезает расширение/путь у имени, `LoadTextureCached(JuiTextureObject)` — нет) на данных не проявляется: имена атласов
	//   item_img_table/special_itemlist/sowell_list без «.», «/», «\» (сверка 415 имён, отчёт `wave1_B.md`).
	if (ConfirmIcon && IT)
	{
		AsdaUi::ApplyItemIcon(GetGameInstance(), ConfirmIcon, R.ItemCode, FVector2D(64.f, 64.f), AsdaUi::IconSexSelf, /*bWrap*/true); // ★ 08.11.2026: атлас пола своего персонажа, как эталон `g_ItemImgOptionVariant`
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kConfirmWindowId, true);
	}
}

void UAsdaWarShopSubsystem::HideConfirm()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kConfirmWindowId, false);
	}
}

void UAsdaWarShopSubsystem::OnConfirmOk()   { SendBuy(); }
void UAsdaWarShopSubsystem::OnConfirmCancel() { HideConfirm(); SelRow = -1; }

// ═════════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════

void UAsdaWarShopSubsystem::SendBuy()
{
	// Эталон: повторный запрос не уходит, пока `filler[932] == 1`.
	if (bPending || !Rows.IsValidIndex(SelRow)) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const FAsdaWarShopRow& R = Rows[SelRow];
	if (Net->SendWarShopBuy6162(R.StorePos, R.ItemCode))
	{
		bPending = true;
		UE_LOG(LogAsdaWarShop, Log, TEXT("[военный магазин] отправлен 6162: строка %d предмет %d"),
			R.StorePos, R.ItemCode);
	}
}

void UAsdaWarShopSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок: во время разбора новые тела в очередь
	//   не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	if (TArray<TArray<uint8>> Local; PendingResults.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnBuyResult(B); }
	}
}

// `uiHandleItemActionResult@0xB75580` — приём 6163.
void UAsdaWarShopSubsystem::OnBuyResult(const TArray<uint8>& Body121)
{
	// Эталон первым делом ГАСИТ окно подтверждения и снимает флаг (`sub_93211D`, `v24[932] = 0`).
	HideConfirm();
	bPending = false;
	if (Body121.Num() < 121) { return; }
	const int32 Code = (int32)Body121[0];

	if (Code == 1)
	{
		if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Inv->ApplyWarShopBuyRecords(Body121);
		}
		// Нотис 110601 «Предмет [%s] куплен.» — имя берётся из ВТОРОЙ записи ответа (@64).
		const int32 BoughtCode = RdI32(Body121.GetData() + 64);
		const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		FString Name = IT ? IT->GetItemName(BoughtCode) : FString();
		if (Name.IsEmpty()) { Name = FString::FromInt(BoughtCode); }
		NoticeMsgBoxText(AsdaUi::FormatPrintfArgs(Loc(WarShopLoc::kBought), { Name }));
		RefreshCoinText();
		BuildPage();
		UE_LOG(LogAsdaWarShop, Log, TEXT("[военный магазин] 6163 успех: предмет %d"), BoughtCode);
	}
	else
	{
		static const int32 kResLoc[10] =
		{
			WarShopLoc::kResNoItem, 0, WarShopLoc::kResBagFull, WarShopLoc::kResWeight,
			WarShopLoc::kResNoMoney, WarShopLoc::kResNoPrice, WarShopLoc::kResNoGlory,
			WarShopLoc::kResNotSold, WarShopLoc::kResNotOwner, WarShopLoc::kResCannotBuy
		};
		if (Code >= 0 && Code <= 9 && kResLoc[Code] != 0) { NoticeMsgBox(kResLoc[Code]); }
		else { UE_LOG(LogAsdaWarShop, Warning, TEXT("[военный магазин] 6163 код %d — текста под него в эталоне нет"), Code); }
	}
	SelRow = -1;
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaWarShopSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaWarShopSubsystem::NoticeMsgBox(int32 LocId) const
{
	NoticeMsgBoxText(Loc(LocId));
}

void UAsdaWarShopSubsystem::NoticeMsgBoxText(const FString& Text) const
{
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Trade, Text, 2288);
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaWarShopSubsystem::DumpControls() const
{
	FString S = TEXT("=== военный магазин 67200: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W) { S += FString::Printf(TEXT("  %-32s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("67349 HONOR_item_X"),    BtnClose);
	Line(TEXT("67333 HONOR_item_BTN1"), BtnPrev);
	Line(TEXT("67334 HONOR_item_BTN2"), BtnNext);
	Line(TEXT("67331 HONOR_item_p1"),   TxtPage);
	Line(TEXT("67332 HONOR_itemp2"),    TxtPages);
	Line(TEXT("67335 HONOR_item_T1"),   TxtCoinLbl);
	Line(TEXT("67336 HONOR_item_T2"),   TxtCoin);
	for (int32 K = 0; K < CatButtons.Num(); ++K)
	{
		S += FString::Printf(TEXT("  67202 OPT%d %s (подпись %s)\n"), K,
			CatButtons[K] ? TEXT("есть") : TEXT("НЕТ"),
			CatLabels.IsValidIndex(K) && CatLabels[K] ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		const FAsdaWarShopCardUi& C = Cards[i];
		S += FString::Printf(
			TEXT("  карточка %2d (WBP-индекс %3d): рамка %d планов, выбор %d планов, кнопка %s, иконка %s, ")
			TEXT("значок звания %s, значок армии %s, подписи имя/цена/звание %s/%s/%s\n"),
			i, kCardIdxBase + kCardIdxStep * i, C.CardParts.Num(), C.SelParts.Num(),
			C.BuyButton ? TEXT("да") : TEXT("НЕТ"), C.ItemIcon ? TEXT("да") : TEXT("НЕТ"),
			C.RankIcon ? TEXT("да") : TEXT("НЕТ"), C.Badge ? TEXT("да") : TEXT("НЕТ"),
			C.NameText ? TEXT("да") : TEXT("НЕТ"), C.Price0Text ? TEXT("да") : TEXT("НЕТ"),
			C.Price1Text ? TEXT("да") : TEXT("НЕТ"));
	}
	Line(TEXT("57500 ICON_mssageBOX"),       ConfirmWidget);
	Line(TEXT("57502 ICON_mssageBOX_TEXT"),  ConfirmText);
	Line(TEXT("57503 ICON_mssageBOX_OK"),    ConfirmOk);
	Line(TEXT("57504 ICON_mssageBOX_NO"),    ConfirmNo);
	return S;
}

FString UAsdaWarShopSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("=== военный магазин: состояние ===\n  открыто=%s ждём ответ=%s категория=%d страница %d/%d\n")
		TEXT("  строк витрины=%d, званий=%d, «Монет славы» у игрока=%d, выбрана карточка=%d строка=%d\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bPending ? TEXT("да") : TEXT("нет"),
		Category, Page + 1, LastPage + 1, Rows.Num(), Ranks.Num(), CoinCount(), SelCard, SelRow);
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		if (Cards[i].RowIndex < 0) { continue; }
		const FAsdaWarShopRow& R = Rows[Cards[i].RowIndex];
		S += FString::Printf(TEXT("  карточка %2d → строка %3d: товар=%d цена=%d×%d звание=%d армия=%d город=%d\n"),
			i, Cards[i].RowIndex, R.ItemCode, R.PriceCode0, R.PriceQty0, R.PriceQty1, R.VictorOnly, R.RegionFilter);
	}
	return S;
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без NPC) ════════════════════════════
namespace
{
	UAsdaWarShopSubsystem* WarShopSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWarShopSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarShopOpen(
	TEXT("asda.WarShop.Open"),
	TEXT("asda.WarShop.Open — открыть военный магазин 67200 так же, как код диалога NPC 30070"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaWarShopSubsystem* S = WarShopSub(W);
		if (!S) { UE_LOG(LogAsdaWarShop, Warning, TEXT("подсистема недоступна")); return; }
		S->OpenWindow();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarShopClose(
	TEXT("asda.WarShop.Close"),
	TEXT("asda.WarShop.Close — закрыть окно (эталонный uiHonorShopDlgOpen, имя врёт)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaWarShopSubsystem* S = WarShopSub(W)) { S->CloseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarShopDump(
	TEXT("asda.WarShop.Dump"),
	TEXT("asda.WarShop.Dump — дамп КАЖДОГО контрола окна 67200 + витрина, страница и валюта"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaWarShopSubsystem* S = WarShopSub(W);
		if (!S) { UE_LOG(LogAsdaWarShop, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaWarShop, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarShopCat(
	TEXT("asda.WarShop.Category"),
	TEXT("asda.WarShop.Category <0..3> — переключатель 67202: 0 Воин, 1 Лучник, 2 Маг, 3 Обычный"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaWarShopSubsystem* S = WarShopSub(W)) { S->SetCategory(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarShopPage(
	TEXT("asda.WarShop.Page"),
	TEXT("asda.WarShop.Page <страница> — кнопки 67333/67334 (страница = 12 карточек)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaWarShopSubsystem* S = WarShopSub(W)) { S->SetPage(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarShopClick(
	TEXT("asda.WarShop.Click"),
	TEXT("asda.WarShop.Click <карточка 0..11> [buy] — прогнать клик ТЕМ ЖЕ путём, что и мышь: "
	     "без `buy` — по иконке (примерка), с `buy` — по кнопке покупки"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWarShopSubsystem* S = WarShopSub(W);
		if (!S) { UE_LOG(LogAsdaWarShop, Warning, TEXT("подсистема недоступна")); return; }
		const int32 Card = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		if (A.Num() > 1 && A[1].Equals(TEXT("buy"), ESearchCase::IgnoreCase)) { S->OnCardBuyClicked(Card); }
		else { S->OnCardIconClicked(Card); }
	}));
#endif // ASDA_DEV_CONSOLE
