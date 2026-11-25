// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterShopSubsystem.cpp — МАГАЗИН БУСТЕРОВ `BOOSTER_SHOP_WIN` (15300) + панель покупки
//   `BOOSTER_PUR_WIN` (15515) + подложка `SHELLITEM_MsgBase` (460).
//   Весь реверс с адресами, доказательство живости входа и список переиспользованных общих точек —
//   в шапке `Public/UI/AsdaBoosterShopSubsystem.h`. Ни одно число здесь не выдумано.
//
// ═══════════ ТАБЛИЦА КОНТРОЛОВ (IDNum → узел WBP_BOOSTER_SHOP_WIN; снято дампом ассета) ════════
//   15300 BOOSTER_SHOP_WIN        BOOSTER_SHOP_WIN_0_0…_0_8            рамка 3×3
//   15301 BOOSTER_SHOP_CLOSE      BOOSTER_SHOP_CLOSE                   крестик
//   15303/15304 чекбокс пола      BOOSTER_SHOP_SEXCHK / _SEXTXT        ★ скрыты (Д-3/Д-4)
//   15599 Booster_Shop_Part2      Booster_Shop_Part2_grp               подложка сетки
//   15306+9i ячейка               BOOSTER_ITEM{nn}_grp                 21 шт.
//     15308 имя                   BOOSTER_ITEM_TITLE{nn}_Text          ★ у ячеек 01…20 узла НЕТ в
//                                                                      импорте — его кладёт шаг
//                                                                      `asda_ui_booster_cell_nodes`
//     15309 цена                  BOOSTER_ITEM_PRICE{nn}
//     15310 иконка                BOOSTER_ITEM_ICON{nn}_<n>_0
//     15311 «Купить»              BOOSTER_ITEM_PUR{nn} + _Text
//     15307 «Подарок»             BOOSTER_ITEM_PRESENT{nn} + _Text     ★ скрыта (Д-1 + 5074 мёртв)
//     15313/15314 бейджи          BOOSTER_ITEM_NEW{nn}_<n>_0 / _HOT{nn}_<n>_0
//     15560/15570/15561…15569     BOOSTER_Stamp_Base* / BOOSTER_Num_Base* / BOOSTER_Stamp{1..9}*
//   15496 рамка выбора            BOOSTER_ITEM_SEL_grp
//   15498/15499 счётчики          BOOSTER_SHOP_NUM1 / _NUM2
//   15501/15503/15505/15507       BOOSTER_SHOP_NEXTP / _BACP / _FIRSTP / _ENDP
//   15510/15511                   BOOSTER_SHOP_TXT1 / _MONEY
//   15513/15514                   BOOSTER_SHOP_CHARGE / Booster_CashCheck_Btn
//   40004/40005                   Booster_PreCashTxt / Booster_PreCash  ★ скрыты (Д-11)
//   15545 категории               BOOSTER_SHOP_MENU_OPT0…6 + _Text…_Text7
//   15515 панель покупки          BOOSTER_PUR_WIN_grp
//   15538/15536/15537/15533       BOOSTER_PUR_ITEM_NAME / _NARR / _TXT7 / _ICON_<n>_0
//   15518/15519/15520             BOOSTER_PUR_TXT1 / _CHRNAME / _CONFIRM
//   15543/15541/15539             BOOSTER_PUR_TXT4 / _TXT5 / _TXT6
//   15544/15542/15540             BOOSTER_PUR_COMBO1 / _COMBO2 / _COMBO3   (`UAsdaJuiComboBox`)
//   15521/15522/15524/15525       BOOSTER_PUR_TXT2 / _PRI1 / _TXT3 / _PRI2
//   15528/15529                   BOOSTER_PUR_CHK1 / _WARE (+ группа `BOOSTER_PUR_WARE_Base_grp`)
//   15527/15530/15547             BOOSTER_PUR_OKBTN / _CANCEL / _PREV
//   15590/15591                   BOOSTER_PUR_GuideTxt00 / GuideTxt01
#include "UI/AsdaBoosterShopSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaDataTableCache.h"
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ окно предпросмотра 15600 — общая точка, уже есть
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка 1559, режим F
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "TimerManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaCashGiftSubsystem.h"       // ★ 01.11.2026 P101_CashGift: подарок 5072/5074 — общее состояние g_pShopWnd
#include "AsdaBoosterShopSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaBoosterShopSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaBoosterShop);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ★★★ 06.11.2026 [ИСПРАВЛЕНИЕ] Клик-зоны пустых карточек. Зоны `BoosterShop_HITnn` лежат в корне окна с Z=50 и строятся для всех 21 ячейки. На странице
//   из 15 карточек ячейки 15-20 погашены, а их зоны оставались включёнными и перекрывали «Пополнить» 15513 и кнопку баланса 15514 (аудит
//   `asda.UI.ZoneOverlapAudit`). В оригинале скрытая ячейка клик не получает. 1 = зоны гаснут вместе с ячейками; 0 = как раньше.
ASDA_DEV_CVAR(int32, CVarAsdaBoosterGridFollowCells,
	TEXT("asda.UI.BoosterGridFollowCells"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = клик-зоны магазина бустеров следуют за видимостью карточек; 0 = как раньше, зоны пустых карточек перекрывают кнопки окна."),
	ECVF_Default);

// ★ 12.11.2026 [ИСПРАВЛЕНИЕ, дефект оригинала] Рамка выбора 15496 без выбранной ячейки. В оригинале рамку только ставят и
//   показывают (`uiSlotControlPlaceAndShow@0xB88720`, все три вызова с «показать»), а сброс окна
//   (`uiStorageWindow_ResetState@0xB83930`) лишь пишет «ячейка не выбрана» (−1) и рамку не гасит: после закрытия и
//   повторного открытия окна рамка стоит на прежнем месте, хотя ничего не выбрано. С ключом 1 рамка гаснет там же, где
//   сбрасывается выбор. Определение — здесь (читают две части, объявление — в AsdaBoosterShopSubsystem_Private.h).
ASDA_DEV_CVAR(int32, CVarAsdaBoosterSelFrameResetOnOpen,
	TEXT("asda.BoosterShop.SelFrameResetOnOpen"), 1,
	TEXT("[ИСПРАВЛЕНИЕ, дефект оригинала] 1 = рамка выбора 15496 магазина бустеров гаснет, когда выбранная ячейка сброшена "
	     "(повторное открытие окна, сброс сессии); 0 = как оригинал — рамка остаётся на прежней ячейке."),
	ECVF_Default);

namespace {
	const TCHAR* kTablePath = TEXT("/Game/AsdaAssets/Data/NPC/DT_BoosterShopList.DT_BoosterShopList");
	// [ПОРТ] 05.11.2026: раскладка полосы вкладок 15545 (редактор витрины `Tools/BoosterShopEditor`).
	const TCHAR* kTabsPath = TEXT("/Game/AsdaAssets/Data/NPC/DT_BoosterShopTabs.DT_BoosterShopTabs");

	// ★ [ПОРТ] 05.11.2026 (задача владельца «редактор бустер-шопа»): порядок и набор вкладок полосы 15545 — из данных.
	//   В оригинале они зашиты в коде (кнопка i = фильтр i, все семь). Таблицы нет или ключ 0 — ровно эталон.
	static ASDA_DEV_CVAR(int32, CVarTabsFromTable,
		TEXT("asda.PremiumMall.BoosterTabsFromTable"), 1,
		TEXT("[ПОРТ] 1 = порядок и набор вкладок магазина бустеров 15545 из DT_BoosterShopTabs (редактор витрины); "
		     "0 = как оригинал — семь вкладок в эталонном порядке."),
		ECVF_Default);

	// Адрес доната — эталон `UI_OpenDonatePage_ECACA0@0xECACA0` (`ShellExecuteA("open", …)`).
	const TCHAR* kDonateUrl = TEXT("");   // ★ 27.10.2026 (владелец): ссылка на area-game.ru снята — «Пополнить» пока ничего не открывает

	// ★★★ Д-16 — МЁРТВАЯ ПОЛОВИНА УСЛОВИЯ У ВКЛАДОК «Доспех» (5) и «Инструмент» (6).
	//   Эталон `itemGridRefreshPage@0xB81460` (`ida.c:421056-421061`) требует:
	//       вкладка 5: showcaseType == 12 && itemRecord->itemSubType == 0
	//       вкладка 6: showcaseType == 13 && itemRecord->itemSubType == 0
	//   У вкладок 2/3/4 второй операнд осмыслен — он РАЗБИВАЕТ оружие (`showcaseType == 9`) на
	//   классы по `itemSubType` (0…3 воин, 4…7 маг, 8…11 лучник; та же нарезка, что выбирает набор
	//   анимаций в `CCharacter_LoadMotionAnims@0x9B84D0`). У вкладок 5 и 6 нарезки нет, а
	//   `itemSubType == 0` осталось — и это БАГ, а не данные. Доказательство ВТОРЫМ набором данных:
	//   в корейской сборке («Фантазиум») `Bpremium.BIN` содержит три записи, помеченные КАК РАЗ
	//   категорией 13 (коды 22645/22646 — «Бустер зелья восстановления», «Бустер эликсира
	//   здоровья»), и у них `item_list.ItemType` тоже 13, то есть автор данных целил ровно во
	//   вкладку «Инструмент». Но `item_list.ItemSubType` у них 66 (как у ВСЕХ 47 предметов типа 13
	//   в таблице) ⇒ условие не выполняется НИ ДЛЯ ОДНОЙ записи ни в одной из сборок. Вкладки 5 и 6
	//   недостижимы физически.
	//   Исправление: у вкладок 5 и 6 оставляем только категорию витрины. Вкладки 2/3/4 не трогаем.
	static ASDA_DEV_CVAR(int32, CVarCatSubTypeStrict,
		TEXT("asda.PremiumMall.BoosterCatSubTypeStrict"), 0,
		TEXT("0 = вкладки «Доспех»/«Инструмент» фильтруют только по категории витрины (исправление "
		     "Д-16: в оригинале к ним приклеено мёртвое условие itemSubType==0, из-за которого они "
		     "пусты в ЛЮБОЙ сборке); 1 = как оригинал."),
		ECVF_Default);

	// Д-1 — кнопка «Подарок» 15307 в ячейках: `Visible="0"`, включения нет. Держим скрытой НЕ
	//   только «как в оригинале»: её путь — опкод **5074**, которого сервер НЕ ПРИНИМАЕТ
	//   (нет `case 0x13D2` ни в декомпиле, ни в живом `GameServer.exe`), то есть кнопка была бы
	//   заведомо мёртвой. Ключ оставлен для проверки на пропатченном сервере.
	static ASDA_DEV_CVAR(int32, CVarShowGiftBtn,
		TEXT("asda.PremiumMall.ShowBoosterGift"), 0,
		TEXT("0 = кнопка «Подарок» в ячейках скрыта, как в оригинале (Д-1) И потому, что её опкод "
		     "5074 сервер не принимает; 1 = показать (★ 2-Г С-2: «ОК» в режиме подарка ничего не отправляет)."),
		ECVF_Default);
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaBoosterShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Cells.SetNum(kCells);
	CatButtons.SetNum(kCats);
	CatLabels.SetNum(kCats);

	if (UGameInstance* GI = GetGameInstance())
	{
		// ★ 10.10.2026 [ОРИГИНАЛ]: ESC по окну 15300 — эталонная ветка события 0x15 (разбор у `EscClose`).
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaBoosterShopSubsystem::EscClose));
		}
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			// ★ ОБЩИЕ ТОЧКИ: приём 5071 и 5467 уже реализованы для окна 6000 — второй копии нет.
			HBuyResult = Net->OnPremiumShopResult.AddUObject(this, &UAsdaBoosterShopSubsystem::OnBuyResult);
			HBalance   = Net->OnPremiumBalance.AddUObject(this, &UAsdaBoosterShopSubsystem::OnBalance);
		}
	}
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] подсистема поднята: окно 15300, до %d ячеек, категорий %d"),
		kCells, kCats);
}

void UAsdaBoosterShopSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnPremiumShopResult.Remove(HBuyResult);
			Net->OnPremiumBalance.Remove(HBalance);
		}
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterEscCloser(kWindowId);
			WM->UnregisterUiZone(kWindowId);
			WM->CloseWindow(kWindowId);
			WM->CloseWindow(kMsgBaseId);
		}
	}
	Widget = nullptr;
	bOpen = false;
	bReady = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaBoosterShopSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseWindow();
	bPending = false;                 // ждали 5071 → нотис 1026
	Cat = TabFilters.Num() > 0 ? TabFilters[0] : 0;   // [ПОРТ] 05.11.2026: первая видимая вкладка (эталон — 0)
	Page = 1; MaxPage = 1;
	CashGxp = 0;
	SelCell = -1; TipCell = -1;
	// ★ 12.11.2026 [ИСПРАВЛЕНИЕ, дефект оригинала]: выбор сброшен — рамка 15496 гаснет (ключ SelFrameResetOnOpen).
	if (CVarAsdaBoosterSelFrameResetOnOpen.GetValueOnGameThread() != 0)
	{
		for (const TObjectPtr<UWidget>& W : SelFrame) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	}
	PurItemCode = 0; PurBaseCode = 0; PurPriceOne = 0; PurQty = 1; PurJob = 0; PurLevel = 0;
	BuiltForCells = 0;
	PendingBuyCodes.Reset();
	BuyRun.Stop();                    // [ИСПРАВЛЕНИЕ] 25.10.2026: поштучная покупка не переживает сессию
	(void)Reason;
}

FString UAsdaBoosterShopSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

void UAsdaBoosterShopSubsystem::NoticeChat(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::Trade, LocId);
}

void UAsdaBoosterShopSubsystem::NoticeTip(int32 LocId) const
{
	// ★ МИСНЕЙМ (аудит 01_cpp_ui б-4): это МОДАЛЬНОЕ окно 1500, а не подсказка.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Loc(LocId));
}

// ═══════════════════════════════ ДАННЫЕ ВИТРИНЫ ════════════════════════════════════════════════
// Порт `BoosterShop_LoadBpremium@0xB882F0` + `BoosterShop_LoadFpremium@0xB884E0` — чтение самих
//   файлов делает импортёр (`Tools/asda_import_booster_shop.py`), рантайм читает таблицу.
bool UAsdaBoosterShopSubsystem::LoadAssortment()
{
	if (bLoaded) { return RowsB.Num() + RowsF.Num() > 0; }
	const UDataTable* Dt = AsdaDt::Cached(kTablePath);
	if (!Dt)
	{
		UE_LOG(LogAsdaBoosterShop, Error,
			TEXT("[магазин бустеров] нет таблицы %s — витрина пуста. Запусти "
			     "Tools/asda_import_booster_shop.py"), kTablePath);
		return false;
	}
	RowsB.Reset(); RowsF.Reset();
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaBoosterShopRow* R = reinterpret_cast<const FAsdaBoosterShopRow*>(It.Value);
		if (!R) { continue; }
		(R->Source == TEXT("F") ? RowsF : RowsB).Add(*R);
	}
	// Порядок в таблице неупорядочен (GetRowMap — хеш); значимый порядок хранится в `Order`.
	RowsB.Sort([](const FAsdaBoosterShopRow& A, const FAsdaBoosterShopRow& B) { return A.Order < B.Order; });
	RowsF.Sort([](const FAsdaBoosterShopRow& A, const FAsdaBoosterShopRow& B) { return A.Order < B.Order; });
	bLoaded = true;
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] ассортимент: Bpremium %d записей, Fpremium %d (вкладка «Популярные»)"),
		RowsB.Num(), RowsF.Num());
	return RowsB.Num() + RowsF.Num() > 0;
}

// ★ Вкладка 1 «Популярные» читает ДРУГОЙ вектор (`this+686` = Fpremium), остальные — Bpremium
//   (`itemGridRefreshDispatch@0xB80FA0`: `a2 == 1 ? itemTradeGridRefresh : itemGridRefreshPage`).
const TArray<FAsdaBoosterShopRow>& UAsdaBoosterShopSubsystem::ActiveSource() const
{
	return (Cat == 1) ? RowsF : RowsB;
}

// ★★★ ФИЛЬТР КАТЕГОРИЙ — ПО КАКОМУ ПОЛЮ ОН ИДЁТ НА САМОМ ДЕЛЕ.
//
// Эталон `itemGridRefreshPage@0xB81460` (`ida.c:421056-421061`) берёт ключ категории НЕ из таблицы
//   предметов, а ИЗ САМОЙ ЗАПИСИ ВИТРИНЫ: `itemType = *(u8 *)(запись + 68)`, то есть байт `+0x44`
//   80-байтовой записи `Bpremium.BIN`. Что итерируется именно сырая запись файла — доказано
//   загрузчиком `BoosterShop_LoadBpremium@0xB882F0` (`ida.c:423518-423524`): `stl_vectorReserve80`
//   + `fread(recBuf, 0x50, 1, f)` кладут блок из файла в вектор `this+682` БЕЗ преобразований, а
//   соседние поля той же записи (`+0x48` скидка, `+0x4C` бейдж) код читает тут же как `+72`/`+76`.
// Второй операнд каждой пары — `itemRecord->itemSubType` (смещение +132 записи `item_list`;
//   смещение подтверждено двумя независимыми путями: единственным совпадением по `DataHex` всех
//   записей таблицы и реверсом выбора набора анимаций `CCharacter_LoadMotionAnims@0x9B84D0`).
//
// ЗНАЧЕНИЯ КАТЕГОРИЙ ВИТРИНЫ (loc подписей — `ida.c:420398-420411`):
//   0 «Все» 12775 — фильтра нет; 1 «Популярные» 13240 — фильтра нет, ДРУГОЙ источник (Fpremium);
//   2 «Воин» 6915 = 9/подтип 0…3;  3 «Лучник» 6916 = 9/подтип 8…11;  4 «Маг» 6917 = 9/подтип 4…7;
//   5 «Доспех» 8434 = 12;          6 «Инструмент» 15150 = 13.
//   ⚠ Имена вкладок здесь НЕ врут: нарезка подтипов 0…3 / 4…7 / 8…11 — это ровно классы оружия
//   (меч-копьё-двуручник / посох-трость-жезл / арбалет-лук-баллиста).
//
// ★ ПОЧЕМУ В ИГРЕ ПУСТО. У ВСЕХ 72 записей `Bpremium.BIN` рабочей сборки байт `+0x44` равен 12
//   (проверено хекс-дампом; так же в арабской и в переводной сборке). Значит категории 9 и 13 в
//   данных не встречаются вовсе, и вкладки «Воин»/«Лучник»/«Маг»/«Инструмент» пусты ПО ДАННЫМ —
//   так же было и в оригинале. «Популярные» пусты потому, что `Fpremium.BIN` содержит 0 записей.
// ★★★ [ИСПРАВЛЕНИЕ] 31.10.2026 (задача владельца «вкладки магазина бустеров не распределены», сессия item-level-boxes):
//   второй операнд вкладок 2/3/4 у эталона — `itemSubType` САМОГО предмета витрины, а все предметы витрины — бустеры
//   (item_list 11/66) ⇒ подтип 66 не попадает ни в 0…3, ни в 4…7, ни в 8…11: даже с категорией 9 вкладки «Воин/Лучник/
//   Маг» пусты (дефект оригинала той же природы, что Д-16). Исправление: класс оружия — подтип оружия (item_list тип 9)
//   из СОСТАВА бустера (`booster_list`, большинство позиций). Категорию витрины задаёт ведомость импортёра
//   `Tools/data/booster_shop_category.csv`. 0 = как эталон (подтип самого предмета витрины).
static ASDA_DEV_CVAR(int32, CVarBoosterCatByContents,
	TEXT("asda.PremiumMall.BoosterCatByContents"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 = вкладки «Воин/Лучник/Маг» магазина бустеров по классу оружия в СОСТАВЕ бустера; "
	     "0 = как эталон — по подтипу самого бустера (66), вкладки пусты."),
	ECVF_Default);

// Подтип оружия (item_list тип 9), которого в составе бустера больше всего; −1 — оружия в составе нет.
static int32 BoosterContentWeaponSubType(const UAsdaItemTableSubsystem& IT, int32 BoosterCode)
{
	TArray<int32> Codes;
	if (!IT.GetBoosterPreview(BoosterCode, Codes)) { return -1; }
	TMap<int32, int32> Votes;
	for (const int32 C : Codes)
	{
		const FAsdaItemRecord* R = IT.GetItem(C);
		if (R && R->ItemType == 9) { ++Votes.FindOrAdd(R->ItemSubType); }
	}
	int32 Best = -1, BestN = 0;
	for (const TPair<int32, int32>& V : Votes)
	{
		if (V.Value > BestN || (V.Value == BestN && V.Key < Best)) { Best = V.Key; BestN = V.Value; }
	}
	return Best;
}

bool UAsdaBoosterShopSubsystem::PassesCategory(const FAsdaBoosterShopRow& Row) const
{
	if (Cat == 0 || Cat == 1) { return true; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Row.ItemCode) : nullptr;
	if (!Rec) { return false; }                       // эталон: нет записи — запись пропускается
	const int32 T = Row.ItemType & 0xFF;              // эталон читает поле +0x44 как u8
	const int32 S = (T == 9 && CVarBoosterCatByContents.GetValueOnGameThread() != 0)
		? BoosterContentWeaponSubType(*IT, Row.ItemCode)   // [ИСПРАВЛЕНИЕ] класс по составу
		: Rec->ItemSubType;
	// Д-16: у вкладок 5/6 второй операнд `S == 0` мёртв в любой сборке (см. ключ отката).
	const bool bStrict = CVarCatSubTypeStrict.GetValueOnGameThread() != 0;
	switch (Cat)
	{
	case 2: return T == 9  && S >= 0 && S <= 3;       // Воин (S < 0 — в составе нет оружия: класс не определён)
	case 3: return T == 9  && S >= 8 && S <= 11;      // Лучник
	case 4: return T == 9  && S >= 4 && S <= 7;       // Маг
	case 5: return T == 12 && (!bStrict || S == 0);   // Доспех
	case 6: return T == 13 && (!bStrict || S == 0);   // Инструмент
	default: return false;
	}
}

// Диагностика: сколько карточек даёт каждая вкладка при текущих данных и текущем ключе Д-16.
//   Считает ровно тем же телом, что и `BuildPage`, — иначе цифра в самотесте врала бы.
int32 UAsdaBoosterShopSubsystem::CountForCategory(int32 Which) const
{
	if (Which == 1) { return RowsF.Num(); }
	const TArray<FAsdaBoosterShopRow>& Src = RowsB;
	const int32 Save = Cat;
	int32 N = 0;
	const_cast<UAsdaBoosterShopSubsystem*>(this)->Cat = Which;
	for (const FAsdaBoosterShopRow& R : Src) { if (PassesCategory(R)) { ++N; } }
	const_cast<UAsdaBoosterShopSubsystem*>(this)->Cat = Save;
	return N;
}

// ★★★ СКОЛЬКО ЯЧЕЕК НА СТРАНИЦЕ. Порт `uiStorageWindow_Open@0xB83B60` (`ida.c:421813-421818`).
//   Эталон пишет условие ДВАЖДЫ (`<= 0x400` и `>= 0x400`), и при ровно 1024 срабатывают обе
//   ветки — итог всё равно 21 (дефект Д-12: логика небрежна, результат безобиден). Пишем один раз.
int32 UAsdaBoosterShopSubsystem::CellsPerPage() const
{
	float H = 768.f;
	if (const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		const FVector2D S = AsdaUi::ViewportSizeUnits(W);
		if (S.Y > 1.f) { H = (float)S.Y; }
	}
	return (H >= 1024.f) ? kCellsTall : kCellsShort;
}

// Порт `itemGridRefreshDispatch@0xB80FA0`: сперва `itemGridHideColumns` гасит ВСЕ 21 ячейки,
//   затем страница заполняется заново. Счётчик страниц — `ceil(matched / cellsPerPage)`.
void UAsdaBoosterShopSubsystem::BuildPage()
{
	const int32 PerPage = CellsPerPage();
	for (int32 i = 0; i < kCells; ++i) { Cells[i].ItemCode = 0; ShowCell(i, false); }

	const TArray<FAsdaBoosterShopRow>& Src = ActiveSource();
	int32 Matched = 0;
	int32 Slot = 0;
	for (const FAsdaBoosterShopRow& Row : Src)
	{
		if (!PassesCategory(Row)) { continue; }
		// Эталон: `perPage * (page − 1) <= matched && matched < page * perPage`.
		if (PerPage * (Page - 1) <= Matched && Matched < Page * PerPage && Slot < PerPage)
		{
			PopulateCell(Slot++, Row);
		}
		++Matched;
	}
	MaxPage = Matched / PerPage + ((Matched % PerPage) ? 1 : 0);
	if (MaxPage < 1) { MaxPage = 1; }
	if (Page > MaxPage) { Page = MaxPage; }
	if (IconGrid)
	{
		IconGrid->Refresh();
		// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] Зоны сетки следуют за видимостью ячеек (разбор у ключа).
		AsdaBoosterZonesFollowPanel(IconGrid, (PurMode != 0 && Widget) ? Widget->GetWidgetFromName(TEXT("BOOSTER_PUR_WIN_grp")) : nullptr, kCells);
	}
	RefreshStatTexts();
}

void UAsdaBoosterShopSubsystem::ShowCell(int32 Cell, bool bVisible)
{
	if (!Cells.IsValidIndex(Cell)) { return; }
	FAsdaBoosterCellUi& C = Cells[Cell];
	const ESlateVisibility V = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	for (const TObjectPtr<UWidget>& W : C.Parts) { if (W) { W->SetVisibility(V); } }
	// Кнопки обязаны ЛОВИТЬ мышь (закон `selfhittest-vs-hittest-invisible`).
	AsdaUi::SetShownHit(C.BuyButton, bVisible);
	AsdaUi::SetShownHit(C.ItemIcon, bVisible);
	// ★ Условные узлы гасим ПОСЛЕ общего показа — иначе они вернулись бы на экран.
	for (const TObjectPtr<UWidget>& W : C.Conditional) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	// ★ 01.11.2026 P101_CashGift [ИСПРАВЛЕНИЕ]: при включённом подарке (`AsdaCashGiftOn`, сервер с P101) кнопка «Подарок» видна —
	//   в оригинале её не показывал никто (Д-1), потому что серверной половины не было.
	if (bVisible && (CVarShowGiftBtn.GetValueOnGameThread() != 0 || AsdaCashGiftOn()) && C.GiftButton)
	{
		C.GiftButton->SetVisibility(ESlateVisibility::Visible);
		if (C.GiftCaption) { C.GiftCaption->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	}
}

// Порт тела цикла `itemGridRefreshPage@0xB81460` (`ida.c:421066-421105`).
void UAsdaBoosterShopSubsystem::PopulateCell(int32 Cell, const FAsdaBoosterShopRow& Row)
{
	if (!Cells.IsValidIndex(Cell)) { return; }
	FAsdaBoosterCellUi& C = Cells[Cell];
	C.ItemCode = Row.ItemCode;

	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Row.ItemCode) : nullptr;
	// ★ ИМЯ И ЦЕНА — ИЗ `item_list`, а НЕ из файла витрины (эталон: `ItemData_FindByCode` и
	//   `itemRecord->price`); имя в `Bpremium.BIN` кодом отрисовки не читается вовсе.
	if (C.NameText)  { C.NameText->SetText(FText::FromString(Rec ? IT->GetItemName(Row.ItemCode) : FString())); }   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
	if (C.PriceText) { C.PriceText->SetText(FText::FromString(AsdaUi::FormatThousands(Rec ? Rec->Price : 0))); }

	ShowCell(Cell, true);

	// ── Плашка скидки (запись +0x48 > 0): видима цифра k, у которой 10*(k+1) == процент ──
	const int32 Disc = Row.DiscountPct;
	if (C.Stamp)
	{
		AsdaUi::SetShownDecor(C.Stamp, Disc > 0);
	}
	for (int32 d = 0; d < C.Digits.Num(); ++d)
	{
		if (!C.Digits[d]) { continue; }
		const bool bOn = (Disc > 0) && (10 * (d + 1) == Disc);
		AsdaUi::SetShownDecor(C.Digits[d], bOn);
	}
	// ── Бейджи (запись +0x4C): 1 → HOT, 2 → NEW ──
	const int32 Badge = Row.Badge & 0xFF;
	AsdaUi::SetShownDecor(C.BadgeHot, Badge == 1);
	AsdaUi::SetShownDecor(C.BadgeNew, Badge == 2);
}

// Порт `uiRefreshStatTexts@0xB80E20`: 15498 = страница, 15499 = всего, 15511 = GXP.
void UAsdaBoosterShopSubsystem::RefreshStatTexts()
{
	if (TxtPage)  { TxtPage->SetText(FText::FromString(FString::Printf(TEXT("%d"), Page))); }
	if (TxtPages) { TxtPages->SetText(FText::FromString(FString::Printf(TEXT("%d"), MaxPage))); }
	if (TxtCash)  { TxtCash->SetText(FText::FromString(AsdaUi::FormatThousands(CashGxp))); }
}

void UAsdaBoosterShopSubsystem::RefreshTabState()
{
	// [ПОРТ] 05.11.2026: подсвечивается КНОПКА, на которой стоит текущий фильтр (эталонная раскладка — кнопка = фильтр).
	AsdaUi::ApplyTabCaptionState(CatLabels, CatButtons, TabSlotOfFilter(Cat), CatIdleColors);
}

// ═══════════════════════ [ПОРТ] 05.11.2026 РАСКЛАДКА ПОЛОСЫ ВКЛАДОК 15545 ИЗ ДАННЫХ ═══════════════════════
// Задача владельца «редактор бустер-шопа»: порядок и набор вкладок правит инструмент `Tools/BoosterShopEditor`.
//   Таблица `DT_BoosterShopTabs` (строки «Slot → Filter»); видимые вкладки идут кнопками 0…N−1 подряд, кнопки N…6 гаснут.
//   Нет таблицы, нет строк, ключ `asda.PremiumMall.BoosterTabsFromTable 0` или в строках ошибка — эталон: кнопка i = фильтр i.
void UAsdaBoosterShopSubsystem::LoadTabs()
{
	if (bTabsLoaded) { return; }
	bTabsLoaded = true;
	TabFilters.Reset();
	for (int32 i = 0; i < kCats; ++i) { TabFilters.Add(i); }   // эталонная раскладка
	if (CVarTabsFromTable.GetValueOnGameThread() == 0) { return; }
	const UDataTable* Dt = AsdaDt::Cached(kTabsPath);
	if (!Dt || Dt->GetRowMap().Num() == 0)
	{
		UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] таблицы вкладок нет — семь вкладок как в оригинале"));
		return;
	}
	TArray<FAsdaBoosterShopTabRow> Rows;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		if (const FAsdaBoosterShopTabRow* R = reinterpret_cast<const FAsdaBoosterShopTabRow*>(It.Value)) { Rows.Add(*R); }
	}
	Rows.Sort([](const FAsdaBoosterShopTabRow& A, const FAsdaBoosterShopTabRow& B) { return A.Slot < B.Slot; });
	TArray<int32> Got;
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FAsdaBoosterShopTabRow& R = Rows[i];
		// Кнопки подряд с нуля, фильтр в 0…6 без повторов — иначе данные битые и берём эталон (без полупоказа).
		if (R.Slot != i || R.Slot >= kCats || R.Filter < 0 || R.Filter >= kCats || Got.Contains(R.Filter))
		{
			UE_LOG(LogAsdaBoosterShop, Error,
				TEXT("[магазин бустеров] DT_BoosterShopTabs: строка Slot=%d Filter=%d неверна — вкладки как в оригинале"),
				R.Slot, R.Filter);
			return;
		}
		Got.Add(R.Filter);
	}
	TabFilters = MoveTemp(Got);
	UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] вкладок по таблице: %d"), TabFilters.Num());
}

int32 UAsdaBoosterShopSubsystem::TabSlotOfFilter(int32 Filter) const
{
	return TabFilters.IndexOfByKey(Filter);
}

void UAsdaBoosterShopSubsystem::ApplyTabLayout()
{
	if (!bTabsLoaded) { LoadTabs(); }
	for (int32 i = 0; i < kCats; ++i)
	{
		const bool bShown = TabFilters.IsValidIndex(i);
		UButton* Btn = CatButtons.IsValidIndex(i) ? CatButtons[i].Get() : nullptr;
		UTextBlock* Lbl = CatLabels.IsValidIndex(i) ? CatLabels[i].Get() : nullptr;
		UWidget* Seg = CatSegments.IsValidIndex(i) ? CatSegments[i].Get() : nullptr;
		AsdaUi::SetShownHit(Btn, bShown);
		if (Lbl)
		{
			AsdaUi::SetShownNoHit(Lbl, bShown);
			// Подпись — эталонная строка ФИЛЬТРА этой кнопки (loc `kCatLoc`, язык сессии).
			if (bShown)
			{
				const FString S = Loc(kCatLoc[TabFilters[i]]);
				if (!S.IsEmpty()) { Lbl->SetText(FText::FromString(S)); }
			}
		}
		AsdaUi::SetShownDecor(Seg, bShown);
	}
	// Торцевая плашка встаёт сразу за последней видимой кнопкой (при семи — на своё место из ассета).
	if (UCanvasPanelSlot* S = AsdaUi::SlotOf(CatEndCap.Get()))
	{
		if (CatSlotStep > 0.f)
		{
			FAnchorData L = S->GetLayout();
			L.Offsets.Left = CatEndCapBase.X - CatSlotStep * (kCats - TabFilters.Num());
			S->SetLayout(L);
		}
	}
}

void UAsdaBoosterShopSubsystem::OnTabSlotClicked(int32 Slot)
{
	if (!TabFilters.IsValidIndex(Slot)) { return; }   // скрытая кнопка мышь не ловит; защита от прямого вызова
	SetCategory(TabFilters[Slot]);
}

bool UAsdaBoosterShopSubsystem::DebugTabState(int32 Slot, FString& OutCaption, bool& bOutShown, FVector2D& OutSize) const
{
	OutCaption.Reset(); bOutShown = false; OutSize = FVector2D::ZeroVector;
	const UButton* Btn = CatButtons.IsValidIndex(Slot) ? CatButtons[Slot].Get() : nullptr;
	const UTextBlock* Lbl = CatLabels.IsValidIndex(Slot) ? CatLabels[Slot].Get() : nullptr;
	if (!Btn || !Lbl) { return false; }
	OutCaption = Lbl->GetText().ToString();
	// Видимость — по всей цепочке родителей (как её видит игрок), размер — живая геометрия кнопки.
	bool bVis = true;
	for (const UWidget* Up = Btn; Up; Up = Up->GetParent()) { if (!Up->IsVisible()) { bVis = false; break; } }
	bOutShown = bVis;
	OutSize = Btn->GetCachedGeometry().GetLocalSize();
	return true;
}

bool UAsdaBoosterShopSubsystem::DebugCell(int32 Cell, int32& OutCode, FString& OutName, FString& OutPrice, bool& bOutShown) const
{
	OutCode = 0; OutName.Reset(); OutPrice.Reset(); bOutShown = false;
	if (!Cells.IsValidIndex(Cell)) { return false; }
	const FAsdaBoosterCellUi& C = Cells[Cell];
	OutCode = C.ItemCode;
	if (C.NameText)  { OutName = C.NameText->GetText().ToString(); }
	if (C.PriceText) { OutPrice = C.PriceText->GetText().ToString(); }
	bool bVis = C.NameText != nullptr;
	for (const UWidget* Up = C.NameText.Get(); Up; Up = Up->GetParent()) { if (!Up->IsVisible()) { bVis = false; break; } }
	bOutShown = bVis;
	return true;
}
