// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterShopSubsystem_Buy.cpp — ★ R3-c 12.11.2026 (разрез AsdaBoosterShopSubsystem.cpp, дословный перенос): покупка: панель покупки, комбо уровней, цена, клики ячеек, превью, подтверждение, отправка, ответы, баланс, Tick.
//   Общее для частей — в AsdaBoosterShopSubsystem_Private.h; шапка и реверс — в AsdaBoosterShopSubsystem.cpp.
#include "UI/AsdaBoosterShopSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ окно предпросмотра 15600 — общая точка, уже есть
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка 1559, режим F
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
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

namespace {
	constexpr int32 kLocGiftName  = 13230;  // 15518 «Имя персонажа»
	// ★★★ ИМЕНА КОНТРОЛОВ ВРУТ (Д-17). Строка «TXT4» стоит НАД строкой «TXT5», и эталон кладёт
	//   в них подписи НАКРЕСТ относительно номеров: `boosterWindowPopulate` (`ida.c:421415-421419`)
	//   пишет 8019 в `self[547]`, а 8020 в `self[549]`, где `[547]` = `pPurTxt5` (**15541**) и
	//   `[549]` = `pPurTxt4` (**15543**). Индексы пришпилены `uiList_OnCategorySelectFilterSub`
	//   (`ida.c:421159-421178`): `self[550]` — список ПРОФЕССИЙ (его выбор идёт в `self[607]`),
	//   `self[548]` — список УРОВНЕЙ (заполняется числами `entrySubId`); поля Init идут подряд
	//   15539…15544 ⇒ [545] TXT6, [546] COMBO3, [547] TXT5, [548] COMBO2, [549] TXT4, [550] COMBO1.
	constexpr int32 kLocGiftJob   = 13231;  // → 15543 (TXT4, ряд 1) «Профессия персонажа»
	constexpr int32 kLocGiftLvl   = 13232;  // → 15541 (TXT5, ряд 2) «Уровень персонажа»
	constexpr int32 kLocBuyJob    = 8020;   // → 15543 (TXT4, ряд 1) «Профессия:» — рядом с COMBO1
	constexpr int32 kLocBuyLvl    = 8019;   // → 15541 (TXT5, ряд 2) «Уровень:»   — рядом с COMBO2

	constexpr int32 kLocWeight    = 2295;   // 15537 «Вес»

	constexpr int32 kLocJobBase   = 12556;  // подписи комбо-1: 12556 + usageCategory

	// ── Нотисы ветки (таблица «событие → id → канал» — в отчёте порта) ──
	constexpr int32 kLocNoAnswer  = 1026;   // «Сервер не отвечает.» — чат (запрос уже в полёте)
	constexpr int32 kLocBadItem   = 12934;  // «Ошибка данных о предмете…» — плашка (Д-13)
	constexpr int32 kLocNoCharGxp = 12942;  // 5466/5467 ст.0 — чат
	constexpr int32 kLocGxpOk     = 12943;  // 5467 ст.1 — чат
	constexpr int32 kLocGxpBad    = 12944;  // 5467 ст.2 — чат
	constexpr int32 kLocGxpFriend = 12165;  // 5467 ст.3 — чат
	// Коды результата 5071 → строка (§3.6 отчёта); та же таблица, что у окна 6000.
	//   ★ R5-b 12.11.2026: таблица одна — `AsdaPremiumBuy::kResLoc5071` (AsdaPremiumBuyQueue.h), значения те же.
	constexpr const int32 (&kResLoc)[15] = AsdaPremiumBuy::kResLoc5071;
	constexpr int32 kOkGxpStore   = 20062;  // GXP,     в хранилище   [ss1] имя, [ss2] сумма
	constexpr int32 kOkCharmStore = 20061;  // обаяние, в хранилище
	constexpr int32 kOkGxpBag     = 20064;  // GXP,     в инвентарь
	constexpr int32 kOkCharmBag   = 20063;  // обаяние, в инвентарь
	constexpr int32 kResFallback  = 9570;   // фолбэк для отсутствующей 12961 (К11)
	constexpr int32 kLocNoGxpChat = 6778;   // код 4 идёт В ЧАТ, а не в плашку

	// ★★ Д-18 — ЗАЩЁЛКА «ЗАПРОС В ПОЛЁТЕ» НЕ СНИМАЕТСЯ, ЕСЛИ ОТВЕТА НЕТ.
	//   Эталон ставит `this[2071]` перед отправкой 5070 и снимает ТОЛЬКО в обработчике 5071
	//   (`shop_OnRecvTransactionResult@0xC30BD0`, тело 407 б = `memcpy(payload, pkt+9, 0x197)`).
	//   Если сервер не ответил (или ответ не дошёл), защёлка висит до закрытия окна, и КАЖДАЯ
	//   следующая покупка молча отбивается нотисом 1026 — снаружи это выглядит как «бустеры не
	//   покупаются». Отступление от оригинала: снимаем защёлку по таймауту и пишем причину.
	static ASDA_DEV_CVAR(float, CVarPendingTimeout,
		TEXT("asda.PremiumMall.BoosterPendingTimeout"), 10.f,
		TEXT("Секунд ожидания ответа 5071, после которых защёлка «запрос в полёте» снимается "
		     "(исправление Д-18). 0 = как оригинал: защёлка держится до закрытия окна."),
		ECVF_Default);

	// ★★★ [ИСПРАВЛЕНИЕ] 26.10.2026 «баланс −1 после отказа»: тот же дефект порта, что у окна 6000 (разбор — у
	//   `asda.PremiumMall.FailKeepsBalance` в AsdaPremiumShopSubsystem.cpp). Отказ 5071 сервер собирает конструктором
	//   `sub_4F9F00` (баланс −1, GameServer.c:96572); эталон `shop_OnRecvTransactionResult@0xC30BD0` пишет баланс
	//   только при коде 1. Ключ 0 = прежнее поведение порта (баланс из любого ответа).
	static ASDA_DEV_CVAR(int32, CVarFailKeepsBalance,
		TEXT("asda.PremiumMall.BoosterFailKeepsBalance"), 1,
		TEXT("1 = как эталон: баланс магазина бустеров 15300 берётся из ответа 5071 только при коде 1; "
		     "0 = прежний порт: баланс из любого ответа 5071 (на отказ сервер шлёт −1)."),
		ECVF_Default);

	// ★ Д-19 — ЗНАЧОК «МИНУС» НА КНОПКЕ ПОКУПКИ 15527. В `.JUI` у кнопки есть ребёнок
	//   `BOOSTER_PUR_OKBTN_Icon` со скином **`WkBtn_IconMinus`**, `Visible = "1"`, 16×16 в левом
	//   краю кнопки. `IDNum = 4294967295` (то есть id нет вовсе) ⇒ кодом его не достать, и
	//   `SetVisible` к нему в бинаре отсутствовать ОБЯЗАН — ОРИГИНАЛ ЕГО ПОКАЗЫВАЕТ. На кнопке
	//   «купить» минус — очевидная промашка художника оригинала: у кнопок покупки в карточках
	//   стоит `WkIcon_CirPlus`, у «Отмены» — `RdBtn_IconCancel`. Владелец просил убрать
	//   (2026-08-28) ⇒ это ОТСТУПЛЕНИЕ от оригинала, а не исправление дефекта, поэтому с ключом.
	static ASDA_DEV_CVAR(int32, CVarShowOkBtnIcon,
		TEXT("asda.PremiumMall.BoosterOkBtnIcon"), 0,
		TEXT("0 = скрыть значок «минус» на кнопке покупки 15527 (просьба владельца); "
		     "1 = как оригинал — значок показан."),
		ECVF_Default);

	// ★★ Д-20 — СТРОКА «ПРОФЕССИЯ» ПРИ ЕДИНСТВЕННОМ ВАРИАНТЕ. Эталон показывает комбобокс всегда,
	//   даже когда выбирать не из чего (у бустеров оружия профессия одна — `usageCategory` 40,
	//   loc 12596 «1-я работа»). Владелец просил прятать строку целиком и поднимать нижние.
	//   ОТСТУПЛЕНИЕ от оригинала ⇒ ключ. Сдвиг — общей точкой `AsdaUi::ShiftSlotYFromBase`
	//   (от ИСХОДНОГО смещения, иначе повторный вызов сложил бы сдвиг дважды).
	static ASDA_DEV_CVAR(int32, CVarHideSingleJob,
		TEXT("asda.PremiumMall.BoosterHideSingleJob"), 1,
		TEXT("1 = прятать строку «Профессия» и поднимать нижние строки, когда профессия одна "
		     "(просьба владельца); 0 = как оригинал — строка всегда на месте."),
		ECVF_Default);
	// ★ Д-23 — ПОЛОЖЕНИЕ ОКНА «ВИД» 15600. Эталонная формула привязана к окну барабана 10050,
	//   которого в магазине бустеров нет ⇒ опорная точка нулевая и окно уезжало в угол.
	//   Ставим слева от рамки магазина на уровне выбранной строки (просьба владельца).
	static ASDA_DEV_CVAR(int32, CVarPrevBesideRow,
		TEXT("asda.PremiumMall.BoosterPrevBesideRow"), 1,
		TEXT("1 = окно «Вид» 15600 слева от магазина бустеров и на уровне выбранной строки; "
		     "0 = как эталон (формула барабана 10050)."),
		ECVF_Default);
	// Д-10 — гайд-строки 15590/15591: эталон заполняет их loc 6960/6961 и ТУТ ЖЕ гасит
	//   (`ida.c:420542-420549`), и игрок не видит предупреждения «нельзя вернуть или обменять».
	static ASDA_DEV_CVAR(int32, CVarShowGuideTxt,
		TEXT("asda.PremiumMall.ShowBoosterGuideTxt"), 1,
		TEXT("1 = показывать строки 15590/15591 панели покупки (исправление Д-10: оригинал их "
		     "заполняет и тут же гасит); 0 = как оригинал."),
		ECVF_Default);

	// Д-15 — группа `BOOSTER_PUR_WARE_Base` (чекбокс 15528 + подпись 15529) имеет `Visible="0"`,
	//   включения в коде нет ⇒ байт 2 тела 5070 всегда 1 и покупка НА СКЛАД недоступна, хотя
	//   сервер её поддерживает (`v39 = body[2] ? 1 : 5`, `GameServer.c:104655`).
	static ASDA_DEV_CVAR(int32, CVarShowWarehouse,
		TEXT("asda.PremiumMall.ShowBoosterWarehouse"), 1,
		TEXT("1 = показывать чекбокс «В хранилище» 15528/15529 (исправление Д-15: в оригинале группа "
		     "скрыта и покупка на склад недостижима); 0 = как оригинал."),
		ECVF_Default);

	// Д-13 — `BoosterShop_SendBuyPresent` при отсутствии записи предмета возвращает 0 МОЛЧА.
	//   Показываем ту же строку, какой эталон пользуется в аналогичном случае окна 6000
	//   (`shopConfirmBuyItem@0xC43440` → loc 12934). Свою строку не выдумываем.
	static ASDA_DEV_CVAR(int32, CVarNoticeBadItem,
		TEXT("asda.PremiumMall.NoticeBadBoosterItem"), 1,
		TEXT("1 = при отсутствии записи предмета показывать нотис 12934 (исправление Д-13: оригинал "
		     "молча ничего не делает); 0 = как оригинал."),
		ECVF_Default);

	// ★ Б-1 (найден при порте). Кнопка 15527 зовёт `CUpgradeUI_FillItemInfoPanelEx(..., 1)`, и
	//   модалка 1559 ВСЕГДА печатает «Рюкзак» (loc 12925) — даже когда чекбокс «В хранилище»
	//   взведён и в пакет уйдёт контейнер 5 (склад). Игрок видит не то, за что платит.
	static ASDA_DEV_CVAR(int32, CVarLegacyBoxTrade,
		TEXT("asda.PremiumMall.LegacyBoosterBoxTrade"), 0,
		TEXT("0 = строка «Поместить на хранение» модалки 1559 показывает РЕАЛЬНОЕ назначение "
		     "(исправление Б-1); 1 = как оригинал — всегда «Рюкзак» (жёсткий аргумент 1)."),
		ECVF_Default);

	// ★ Б-2 (найден при порте). Та же кнопка передаёт `stackCount = 1`, поэтому модалка печатает
	//   цену ЗА ШТУКУ, а панель 15522 рядом — цену за выбранное количество (`[609] * [614]`).
	static ASDA_DEV_CVAR(int32, CVarLegacyBoxPrice,
		TEXT("asda.PremiumMall.LegacyBoosterBoxPrice"), 0,
		TEXT("0 = модалка 1559 показывает количество и ПОЛНУЮ сумму покупки (исправление Б-2); "
		     "1 = как оригинал — количество 1 и цена за штуку."),
		ECVF_Default);

	// ★ Б-4 (найден при порте). `uiList_OnCategorySelectFilterSub@0xB81B90` индексирует локальный
	//   массив `seenSubFlags[500]` значением ТРЕБУЕМОГО УРОВНЯ без проверки границы — запись за
	//   пределы стекового буфера, если в данных появится уровень ≥ 500.
	static ASDA_DEV_CVAR(int32, CVarFixLevelBound,
		TEXT("asda.PremiumMall.FixBoosterLevelBound"), 1,
		TEXT("1 = уровни вариантов дедуплицируются без ограничения по индексу (исправление Б-4: "
		     "оригинал пишет в `seenSubFlags[level]` при размере массива 500); 0 = как оригинал."),
		ECVF_Default);

	// НАША ДОБАВКА (как у окон 6000/6800): подсказка предмета по наведению — в оригинале у
	//   `CBoosterShop` обработчика 8/9/0xA нет вовсе.
	static ASDA_DEV_CVAR(int32, CVarTooltips,
		TEXT("asda.PremiumMall.BoosterTooltips"), 1,
		TEXT("1 = подсказка товара по наведению в магазине бустеров (НАША ДОБАВКА, в оригинале её "
		     "нет); 0 = выкл."),
		ECVF_Default);
}

// ═══════════════════════════════ ПАНЕЛЬ ПОКУПКИ 15515 ══════════════════════════════════════════
// Порт `uiPopulateItemComposeDialog@0xB81E70` (`ida.c:421238-421450`).
void UAsdaBoosterShopSubsystem::PopulatePurPanel(int32 ItemCode, int32 DialogMode, bool bShow)
{
	// ★ 02.11.2026 P101 (ревизия С-2) [ИСПРАВЛЕНИЕ] Д-К5: показ/сброс панели снимает «ждём 5073», поставленный этим окном.
	if (UAsdaCashGiftSubsystem* GiftR = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr) { GiftR->OnBoosterPanelReset(false); }
	const ESlateVisibility V = bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	for (const TObjectPtr<UWidget>& W : PurTree) { if (W) { W->SetVisibility(V); } }
	// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] Зоны под панелью: рамка панели готова на следующем кадре (разбор у `AsdaBoosterZonesFollowPanel`).
	if (UWorld* ZoneWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		ZoneWorld->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			AsdaBoosterZonesFollowPanel(IconGrid, (PurMode != 0 && Widget) ? Widget->GetWidgetFromName(TEXT("BOOSTER_PUR_WIN_grp")) : nullptr, kCells);
		}));
	}
	if (!bShow)
	{
		PurMode = 0;
		PurItemCode = 0;
		PurBaseCode = 0;
		Variants.Reset(); JobList.Reset(); LevelList.Reset();
		if (ComboJob)   { ComboJob->SetOpen(false); }
		if (ComboLevel) { ComboLevel->SetOpen(false); }
		if (ComboQty)   { ComboQty->SetOpen(false); }
		return;
	}
	// Кнопки обязаны ЛОВИТЬ мышь.
	for (UButton* B : { PurOk.Get(), PurCancel.Get(), PurPreview.Get(), PurConfirm.Get() })
	{
		if (B) { B->SetVisibility(ESlateVisibility::Visible); }
	}

	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
	// Эталонный гейт: запись обязана быть И у неё `durationOrEnchant != 0`, иначе панель пуста.
	if (!Rec || Rec->DurationOrEnchant == 0)
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] панель покупки: у предмета %d нет записи или durationOrEnchant == 0 — "
			     "эталон здесь тоже ничего не заполняет"), ItemCode);
		if (CVarNoticeBadItem.GetValueOnGameThread() != 0) { NoticeTip(kLocBadItem); }
		PopulatePurPanel(0, 0, false);
		return;
	}

	PurMode     = DialogMode;
	PurBaseCode = ItemCode;
	PurItemCode = ItemCode;
	PurPriceOne = Rec->Price;
	PurQty      = 1;
	if (ComboQty) { ComboQty->SetSelectedIndex(0); }

	// ── Имя, описание, вес, иконка ──
	if (PurItemName) { PurItemName->SetText(FText::FromString(IT->GetItemName(ItemCode))); }   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
	if (PurNarr)     { PurNarr->SetText(FText::FromString(IT->GetItemName(ItemCode))); }
	if (PurWeight)
	{
		// Эталон: `sprintf(buf, "%s : %d", loc(2295), LOWORD(itemRecord->field72))`.
		PurWeight->SetText(FText::FromString(FString::Printf(TEXT("%s : %d"), *Loc(kLocWeight), Rec->Weight)));
	}
	if (PurIcon)
	{
		// ★ Д-22 (жалоба владельца «нет иконки слева от названия»): контрол 15533
		//   `BOOSTER_PUR_ICON` (64×64) эталон заполняет тем же способом, что ячейку витрины —
		//   `DrawTextureRect` по `imgIndex` записи предмета (`ida.c:421324-421332`). Раньше порт
		//   узел только показывал и НИЧЕГО в него не клал. Рисуем ОБЩЕЙ ТОЧКОЙ сетки иконок
		//   (`UAsdaIconCellGrid::ApplyItemIcon`), второй копии кода атласа/UV не заводим.
		PurIcon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		// ★ Размер иконки берём ИЗ ДАННЫХ АССЕТА (слот узла 15533, в .JUI `Size = "64 64"`),
		//   а не константой: поменяется раскладка — иконка поедет за ней сама.
		FVector2D IconPos, IconSize(64.f, 64.f);
		AsdaUi::SlotRect(PurIcon.Get(), IconPos, IconSize);
		const bool bIcon = UAsdaIconCellGrid::ApplyItemIcon(Widget, PurIcon.Get(), ItemCode,
			AsdaUi::IconSexSelf, IconSize.X, IconSize.Y, /*bClearIfEmpty*/true); // ★ 08.11.2026: атлас пола своего персонажа, как эталон `g_ItemImgOptionVariant`
		if (!bIcon)
		{
			UE_LOG(LogAsdaBoosterShop, Warning,
				TEXT("[магазин бустеров] иконка 15533 для предмета %d не нарисована (нет записи атласа)"), ItemCode);
		}
	}

	// ── ВАРИАНТЫ предмета (эталонный контейнер `this+690`) ──
	//   itemType == 13 → вариант ОДИН (сам предмет); иначе — все записи `item_list` с тем же
	//   `itemSubType` и `setCode`, у которых `durationOrEnchant != 0` (`ida.c:421348-421372`).
	Variants.Reset();
	if (Rec->ItemType == 13)
	{
		Variants.Add({ Rec->ItemCode, Rec->UsageCategory, Rec->RequiredLevel });
	}
	else if (IT)
	{
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if (R.DurationOrEnchant == 0) { continue; }
			if (R.ItemSubType != Rec->ItemSubType || R.SetCode != Rec->SetCode) { continue; }
			Variants.Add({ R.ItemCode, R.UsageCategory, R.RequiredLevel });
		}
		// Эталон сортирует контейнер (`introSortWrap(..., sub_91FDBA)`); порядок нужен только
		//   для устойчивости показа — сортируем по (профессия, уровень, код).
		Variants.Sort([](const FAsdaBoosterVariant& A, const FAsdaBoosterVariant& B)
		{
			if (A.UsageCategory != B.UsageCategory) { return A.UsageCategory < B.UsageCategory; }
			if (A.RequiredLevel != B.RequiredLevel) { return A.RequiredLevel < B.RequiredLevel; }
			return A.ItemCode < B.ItemCode;
		});
	}

	// ── Комбо-1: РАЗНЫЕ usageCategory вариантов, подпись loc (12556 + cat) ──
	JobList.Reset();
	TArray<FString> JobLabels;
	for (const FAsdaBoosterVariant& Vn : Variants)
	{
		if (JobList.Contains(Vn.UsageCategory)) { continue; }
		JobList.Add(Vn.UsageCategory);
		JobLabels.Add(Loc(kLocJobBase + Vn.UsageCategory));
	}
	if (ComboJob) { ComboJob->SetItems(JobLabels); ComboJob->SetSelectedIndex(JobList.Num() > 0 ? 0 : -1); }
	PurJob = JobList.Num() > 0 ? JobList[0] : 0;
	RebuildLevelCombo();

	// ── Подписи режима (подарок / обычная покупка) ──
	const bool bGift = (DialogMode == 2);
	// ★★★ Д-17 (жалоба владельца «поля перепутаны»): подписи идут К СВОИМ РЯДАМ, а не по номерам.
	//   Ряд 1 — `BOOSTER_PUR_TXT4` (15543) + `COMBO1` (15544, ПРОФЕССИЯ, `.JUI` y=41/132);
	//   ряд 2 — `BOOSTER_PUR_TXT5` (15541) + `COMBO2` (15542, УРОВЕНЬ, y=67/158);
	//   ряд 3 — `TXT6` (15539) + `COMBO3` (15540, количество, y=93/184). Шаг рядов 26 в обеих
	//   колонках. Порт клал 8019 «Уровень:» в TXT4 и 8020 «Профессия:» в TXT5 — то есть подписи
	//   стояли накрест спискам, и над списком профессий читалось «Уровень:».
	if (PurTxt4) { const FString S = Loc(bGift ? kLocGiftJob : kLocBuyJob); if (!S.IsEmpty()) { PurTxt4->SetText(FText::FromString(S)); } }
	if (PurTxt5) { const FString S = Loc(bGift ? kLocGiftLvl : kLocBuyLvl); if (!S.IsEmpty()) { PurTxt5->SetText(FText::FromString(S)); } }
	if (PurTxt1)
	{
		if (bGift) { const FString S = Loc(kLocGiftName); if (!S.IsEmpty()) { PurTxt1->SetText(FText::FromString(S)); } }
		AsdaUi::SetShownDecor(PurTxt1, bGift);
	}
	// Поле имени получателя и кнопка «ОК» подарка — только в режиме подарка.
	AsdaUi::SetShownDecor(PurNameBase, bGift);
	if (PurName)     { AsdaUi::SetShownHit(PurName, bGift); if (bGift) { PurName->SetText(FText::GetEmpty()); } }
	AsdaUi::SetShownHit(PurConfirm, bGift);
	// ★ Д-21 (жалоба владельца «фантомная надпись ок справа вверху»): подпись кнопки подарка
	//   `BOOSTER_PUR_CONFIRM_Text` лежит в поддереве панели и получала общую видимость PurTree,
	//   тогда как сама кнопка 15520 в обычном режиме скрыта. Гасим подпись ВМЕСТЕ с кнопкой.
	AsdaUi::SetShownDecor(PurConfirmCap, bGift);

	// ── ★★ Д-20: профессия ОДНА ⇒ прячем РЯД 1 и поднимаем ряды 2 и 3 на его шаг ──────────────
	//   ★★★ ЧТО ТАКОЕ «РЯД» В ЭТОМ АССЕТЕ (снято дампом, не угадано):
	//     • группа `BOOSTER_PUR_TXT{4|5|6}_grp` — (5,37) / (5,63) / (5,89), шаг 26; внутри неё
	//       лежат подпись и ПЛАНЫ РАМКИ комбобокса (`BOOSTER_PUR_COMBO{N}_Base_*`);
	//     • а тело, стрелка и текст самого комбобокса (`BOOSTER_PUR_COMBO{N}`,
	//       `..._330_0/_330_1`, `..._Text`) — СИБЛИНГИ УРОВНЕМ ВЫШЕ, в группе их нет.
	//   Поэтому ряд двигается и гасится ДВУМЯ пачками. Подписи всех рядов стоят на y=4 внутри
	//   своей группы — мерить шаг по ним нельзя (выходит 0), меряем по ГРУППАМ.
	//   ⚠ `ShiftSlotYFromBase` кладёт `Top = base − dY`, то есть ПОЛОЖИТЕЛЬНЫЙ dY поднимает.
	{
		const bool bHideJob = (CVarHideSingleJob.GetValueOnGameThread() != 0) && (JobList.Num() <= 1);
		// Узлы ряда: группа + внешние узлы комбобокса (без `_Base_` — те уже внутри группы).
		auto RowNodes = [this](int32 Row) -> TArray<UWidget*>
		{
			TArray<UWidget*> Out;
			const FString GrpName = FString::Printf(TEXT("BOOSTER_PUR_TXT%d_grp"), Row + 3);
			if (UWidget* G = AsdaUi::FindNodeByName(Widget, *GrpName, nullptr)) { Out.Add(G); }
			const FString Pfx = FString::Printf(TEXT("BOOSTER_PUR_COMBO%d"), Row);
			TArray<UWidget*> All;
			Widget->WidgetTree->GetAllWidgets(All);
			for (UWidget* W : All)
			{
				if (!W) { continue; }
				const FString N = W->GetName();
				if (N.StartsWith(Pfx) && !N.Contains(TEXT("_Base_"))) { Out.Add(W); }
			}
			return Out;
		};

		// Шаг рядов — по ГРУППАМ, от ИСХОДНЫХ смещений (ряд 2 мог быть сдвинут прошлым вызовом).
		UWidget* Grp1 = AsdaUi::FindNodeByName(Widget, TEXT("BOOSTER_PUR_TXT4_grp"), nullptr);
		UWidget* Grp2 = AsdaUi::FindNodeByName(Widget, TEXT("BOOSTER_PUR_TXT5_grp"), nullptr);
		FVector2D P1, S1, P2, S2;
		if (Grp1 && Grp2 && AsdaUi::SlotRect(Grp1, P1, S1) && AsdaUi::SlotRect(Grp2, P2, S2))
		{
			float BaseTop2 = P2.Y;
			if (const FMargin* B = RowShiftBase.Find(Grp2)) { BaseTop2 = B->Top; }
			const float Step = BaseTop2 - P1.Y;
			if (Step > 0.f) { RowStepY = Step; }
		}
		const float dY = bHideJob ? RowStepY : 0.f;   // + = вверх
		for (int32 Row = 2; Row <= 3; ++Row)
		{
			for (UWidget* W : RowNodes(Row)) { AsdaUi::ShiftSlotYFromBase(RowShiftBase, W, dY); }
		}
		for (UWidget* W : RowNodes(1))
		{
			// Кнопки/интерактив ряда должны ЛОВИТЬ мышь, когда ряд показан.
			// ★★★ ДЕДУПЛИКАЦИЯ 2026-09-02. Здесь была своя копия правила видимости
			//   («`W->IsA<UButton>() ? Visible : SelfHitTestInvisible`»). Правило одно на весь
			//   порт — `AsdaCommunityUi::ShownVisibilityFor`, откат `asda.UI.InteractiveHitTest 0`.
			// ★ ЗАОДНО ЧИНИТСЯ ДЕФЕКТ ПОРТА: в `RowNodes(1)` попадает и САМ движковый
			//   `UComboBoxString` `BOOSTER_PUR_COMBO1` (имя начинается с префикса и не содержит
			//   `_Base_`). Старая ветка отдавала ему `SelfHitTestInvisible`, то есть ЗАЖИГАЛА
			//   белый дропдаун UE поверх плана-скина, хотя `UAsdaJuiComboBox::Setup` его
			//   специально погасил (`AsdaJuiComboBox.cpp:101`). Это ровно та жалоба владельца
			//   от 2026-08-28, что уже лечилась в аукционе (`AsdaExchangeSubsystem.cpp:769-779`).
			//   Общая точка возвращает движковому комбобоксу `Collapsed`.
			W->SetVisibility(bHideJob ? ESlateVisibility::Collapsed
				: AsdaCommunityUi::ShownVisibilityFor(W));
		}
		if (bHideJob && ComboJob) { ComboJob->SetOpen(false); }
		UE_LOG(LogAsdaBoosterShop, Log,
			TEXT("[магазин бустеров] Д-20: профессий %d ⇒ ряд «Профессия» %s, шаг %.0f px, сдвиг вверх %.0f"),
			JobList.Num(), bHideJob ? TEXT("СКРЫТ") : TEXT("показан"), RowStepY, dY);
	}

	// ── itemType == 13: комбо профессии и его подпись эталон ГАСИТ (`ida.c:421400-421412`) ──
	//   Эталон гасит `self[550]` (COMBO1, профессия) и `self[549]` = **TXT4**, а не TXT5 (Д-17).
	if (Rec->ItemType == 13)
	{
		if (PurTxt4) { PurTxt4->SetVisibility(ESlateVisibility::Collapsed); }
		if (UWidget* W = AsdaUi::FindNodeByName(Widget, TEXT("BOOSTER_PUR_COMBO1"), nullptr)) { W->SetVisibility(ESlateVisibility::Collapsed); }
		if (UWidget* W = AsdaUi::FindNodeByName(Widget, TEXT("BOOSTER_PUR_COMBO1_Text"), nullptr)) { W->SetVisibility(ESlateVisibility::Collapsed); }
	}

	// ── Д-19: значок «минус» на кнопке покупки (отступление по просьбе владельца) ──
	if (PurOkIcon)
	{
		PurOkIcon->SetVisibility(CVarShowOkBtnIcon.GetValueOnGameThread() != 0
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	// ── Д-10: гайд-строки. Эталон их заполняет и ТУТ ЖЕ гасит. ──
	{
		const bool bShowGuide = CVarShowGuideTxt.GetValueOnGameThread() != 0;
		const ESlateVisibility GV = bShowGuide ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
		if (PurGuide0) { PurGuide0->SetVisibility(GV); }
		if (PurGuide1) { PurGuide1->SetVisibility(GV); }
	}
	// ── Д-15: группа «В хранилище». Эталон держит её скрытой ⇒ покупка на склад недостижима. ──
	if (PurWareGrp)
	{
		const bool bShowWare = CVarShowWarehouse.GetValueOnGameThread() != 0;
		AsdaUi::SetShownDecor(PurWareGrp, bShowWare);
		AsdaUi::SetShownHit(PurWareChk, bShowWare);
		if (!bShowWare) { bPurToWarehouse = false; }
	}

	RefreshPurPrice();
	CollectSelections();
}

// Порт `uiList_OnCategorySelectFilterSub@0xB81B90`: по выбранной профессии собрать УРОВНИ.
void UAsdaBoosterShopSubsystem::RebuildLevelCombo()
{
	LevelList.Reset();
	TArray<FString> Labels;
	// ★ Б-4: эталон дедуплицирует уровни через `seenSubFlags[level]` при размере массива 500 —
	//   запись за границу стека, если уровень ≥ 500. Здесь дедупликация без ограничения.
	for (const FAsdaBoosterVariant& Vn : Variants)
	{
		if (Vn.UsageCategory != PurJob) { continue; }
		if (CVarFixLevelBound.GetValueOnGameThread() == 0 && (Vn.RequiredLevel < 0 || Vn.RequiredLevel >= 500))
		{
			continue;   // эталонное поведение без записи за границу: уровень просто не попадает в список
		}
		if (LevelList.Contains(Vn.RequiredLevel)) { continue; }
		LevelList.Add(Vn.RequiredLevel);
		Labels.Add(FString::Printf(TEXT("%d"), Vn.RequiredLevel));
	}
	if (ComboLevel) { ComboLevel->SetItems(Labels); ComboLevel->SetSelectedIndex(LevelList.Num() > 0 ? 0 : -1); }
	PurLevel = LevelList.Num() > 0 ? LevelList[0] : 0;
	CollectSelections();
}

// Порт `formCollectInputAndSelections@0xB862E0`: свести выбор комбо в itemCode варианта.
void UAsdaBoosterShopSubsystem::CollectSelections()
{
	// `mapFindValueByXY(this, job, level)` — поиск кода варианта по паре (профессия, уровень).
	int32 Found = 0;
	for (const FAsdaBoosterVariant& Vn : Variants)
	{
		if (Vn.UsageCategory == PurJob && Vn.RequiredLevel == PurLevel) { Found = Vn.ItemCode; break; }
	}
	PurItemCode = Found;
	if (PurWareChk) { bPurToWarehouse = PurWareChk->IsChecked(); }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (const FAsdaItemRecord* R = (IT && Found > 0) ? IT->GetItem(Found) : nullptr) { PurPriceOne = R->Price; }
	RefreshPurPrice();
}

void UAsdaBoosterShopSubsystem::RefreshPurPrice()
{
	// Эталон: 15522 = `[609] * [614]` (кол-во × цена), 15525 = `inventoryGold_FormatToBuf()` (GXP).
	if (PurPrice) { PurPrice->SetText(FText::FromString(AsdaUi::FormatThousands(PurQty * PurPriceOne))); }
	if (PurCash)  { PurCash->SetText(FText::FromString(AsdaUi::FormatThousands(CashGxp))); }
}

// ═══════════════════════════════ ДЕЙСТВИЯ ══════════════════════════════════════════════════════
void UAsdaBoosterShopSubsystem::OnCellClicked(int32 Cell)
{
	if (!bOpen || !Cells.IsValidIndex(Cell) || Cells[Cell].ItemCode <= 0) { return; }
	SelCell = Cell;
	// Рамка выбора 15496: эталон переносит её на выбранную ячейку (`uiSlotControlPlaceAndShow`).
	//   ★ Рамка на 2 px шире ячейки с каждой стороны (216×94 против 212×90 — `.JUI`), поэтому
	//   её левый-верх = левый-верх ячейки минус (2, 2). Части двигаем пачкой, сохранив взаимные
	//   смещения (в ассете это восемь картинок, а не группа).
	if (SelFrame.Num() == 0 || Cells[Cell].Parts.Num() == 0) { return; }
	const UCanvasPanelSlot* Src = AsdaUi::SlotOf(Cells[Cell].Parts[0].Get());
	if (!Src) { return; }
	const FMargin S = Src->GetLayout().Offsets;
	const FVector2D Base(S.Left - 2.f, S.Top - 2.f);
	// ★ ИСПРАВЛЕНИЕ 12.10.2026 (дедуп №2, В3-S10, остаток U15): план встаёт в «угол ячейки − 2 + его смещение от первого плана»
	//   сдвигом через общую точку `AsdaUi::ShiftCanvasSlot` (SPAN-безопасно). Было прямое `Offsets.Left/Top = цель` — у
	//   растянутого плана это растягивало бы его, а не двигало. У текущего ассета все 8 планов `BOOSTER_ITEM_SEL_*` POINT
	//   (`ui_snapshot_after.txt`) — результат прежний, ось POINT получает ровно ту же цель. Эталон — `uiSlotControlPlaceAndShow
	//   @0xB88720` (ui_core.c:10546): `SetField74_75(рамка, Vec2_74(ячейка) − 2)`.
	for (int32 k = 0; k < SelFrame.Num(); ++k)
	{
		UWidget* W = SelFrame[k].Get();
		const UCanvasPanelSlot* Dst = W ? AsdaUi::SlotOf(W) : nullptr;
		if (!Dst) { continue; }
		const FVector2D D = SelFrameDelta.IsValidIndex(k) ? SelFrameDelta[k] : FVector2D::ZeroVector;
		const FMargin Cur = Dst->GetLayout().Offsets;
		AsdaUi::ShiftCanvasSlot(W, (Base + D) - FVector2D(Cur.Left, Cur.Top));
		W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

// ★★ ПОЗИЦИЯ ОКНА «ВИД» 15600 ДЛЯ МАГАЗИНА БУСТЕРОВ (просьба владельца 2026-08-28).
//   По вертикали — верхняя кромка окна к ВЕРХНЕЙ КРОМКЕ ВЫБРАННОЙ СТРОКИ витрины; по горизонтали
//   — СЛЕВА от рамки магазина (правая грань «Вида» упирается в левую грань магазина).
//   Всё считается по ЖИВОЙ геометрии и ширине окна ИЗ ДАННЫХ АССЕТА — констант координат нет.
//   Эталон ставит окно по формуле барабана 10050 (`boosterWnd_OnMessage`, `case 0x2758`), но у
//   магазина бустеров окна барабана нет вовсе, поэтому там опорная точка выходила нулевой и
//   «Вид» вылезал в левом верхнем углу. ОТСТУПЛЕНИЕ ⇒ ключ отката.
void UAsdaBoosterShopSubsystem::PlacePreviewBesideRow(int32 Cell)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaBoosterPanelSubsystem* Prev = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
	if (!Prev || !Widget)
	{
		UE_LOG(LogAsdaBoosterShop, Warning, TEXT("[магазин бустеров] «Вид»: нет подсистемы/виджета"));
		return;
	}
	if (CVarPrevBesideRow.GetValueOnGameThread() == 0) { return; }   // как эталон
	FVector2D ShopMn, ShopMx;
	if (!AsdaUi::FramePlanesAbsRect(Widget, TEXT("BOOSTER_SHOP_WIN_0_%d"), 9, ShopMn, ShopMx))
	{
		// Окно ещё не отрисовано (первый кадр) — живой геометрии нет. Не сдаёмся: помечаем заявку,
		//   `Tick` повторит, как только Slate посчитает прямоугольники.
		PrevPlaceCellPending = Cell;
		return;
	}
	// Верх строки: берём живой прямоугольник ГРУППЫ выбранной ячейки (сама строка = её ряд).
	float RowTop = ShopMn.Y;
	if (Cells.IsValidIndex(Cell) && Cells[Cell].Parts.Num() > 0)
	{
		FVector2D CMn, CMx;
		if (AsdaUi::WidgetAbsRect(Cells[Cell].Parts[0].Get(), CMn, CMx) && !(CMx - CMn).IsNearlyZero())
		{
			RowTop = CMn.Y;
		}
	}
	const float PrevW = Prev->GetPrevWindowWidth();
	if (PrevW <= 0.f)
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] «Вид»: ширина окна 15600 не определена (планы рамки не найдены)"));
		return;
	}
	Prev->RequestPrevPlacementAbs(FVector2D(ShopMn.X - PrevW, RowTop));
	PrevPlaceCellPending = -1;
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] «Вид» 15600: слева от рамки (левый край магазина %.0f, ширина окна %.0f) ")
		TEXT("на уровне строки ячейки %d (верх %.0f)"), ShopMn.X, PrevW, Cell, RowTop);
}
// ★★★ КЛИК ПО ИКОНКЕ КАРТОЧКИ → ОКНО ПРЕДПРОСМОТРА 15600 «Вид».
//
// ЭТАЛОН. Сообщение `0xB` окна 15300 (`boosterShopOnMessage@0xB876F0`, `ida.c:423475`) идёт в
//   `uiInventorySlotUseItem@0xB86EE0`. Там `findIndexInIdTable21@0xB86730` (`ida.c:422797-422829`)
//   ищет id контрола в таблице из 21 значения — **15310, 15319, 15329, 15338, …, 15491**, и это
//   ровно `BOOSTER_ITEM_ICON00…20` (сверено с `gamemain_jui.ini`: `BOOSTER_ITEM_ICON00` = 15310,
//   `…01` = 15319, `…02` = 15329). Перебор ограничен `this[666]` — числом ячеек на странице.
//   Найдя ячейку, эталон кладёт её код в `this[526]`, ставит рамку выбора
//   (`uiSlotControlPlaceAndShow(idx, 1)`) и зовёт `boosterWindowPopulate(this, 1, -1)` —
//   то есть режим 1 = ПОКАЗАТЬ ОКНО 15600 (`ida.c:423035-423042`).
//
// ОТКУДА СОДЕРЖИМОЕ. `boosterWindowPopulate@0xB82D90` берёт `GetBoosterRecord(itemRecord[54])`
//   (`ida.c:421591`) — поле записи предмета по смещению **216 = `DurationOrEnchant`** (пришпилено
//   сверкой всех записей `item_list` с их `DataHex`) — и читает из найденной строки пары
//   «код+вес» шагом 8 байт: 5 слотов в первом ряду и 30 во втором (`ida.c:421603-421660`), итого
//   **35** — ровно столько пар в записи `booster_list.bin` (шаг 284 б). Таблица уже в порту:
//   `DT_booster_list` (4300 строк, ключ `Code`), и её же читает
//   `UAsdaItemTableSubsystem::GetBoosterSlotsRaw`. Пример: «Бустер баллисты Молотобоя» (61959,
//   `DurationOrEnchant` = 2861) ⇒ строка 2861 даёт 4 непустых слота — баллисты Дьявола,
//   Молотобоя, проклятого Молотобоя и затмения.
void UAsdaBoosterShopSubsystem::OnCellIconClicked(int32 Cell)
{
	OnCellClicked(Cell);
	if (!bOpen || !Cells.IsValidIndex(Cell)) { return; }
	const int32 Code = Cells[Cell].ItemCode;
	if (Code <= 0)
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] клик по иконке ячейки %d: товара нет — окно «Вид» не открываем"), Cell);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaBoosterPanelSubsystem* Prev = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
	if (!Prev)
	{
		UE_LOG(LogAsdaBoosterShop, Error, TEXT("[магазин бустеров] нет подсистемы окна «Вид» 15600"));
		return;
	}
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] клик по иконке ячейки %d (код=%d) → окно «Вид» 15600"), Cell, Code);
	// ★ 31.10.2026: общий вход «Вида» (страница 0, как `uiInventorySlotUseItem`: `v11[662] = 0`; щелчок по ячейкам жив —
	//   флаг `+697` стоит с открытия магазина, `uiStorageWindow_Open@0xB83B60`).
	Prev->OpenPrevWindowFrom(Code, EAsdaPrevSource::BoosterShop, /*bCellsActive*/ true);
	PlacePreviewBesideRow(Cell);
}

// Кнопка ячейки «Купить» 15311 — открыть панель покупки в режиме 1.
void UAsdaBoosterShopSubsystem::OnCellBuyClicked(int32 Cell)
{
	// ★ Каждый отказ обязан назвать причину — «ничего не происходит» быть не должно.
	if (!bOpen || !Cells.IsValidIndex(Cell))
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] клик по ячейке %d отбит: окно %s, ячейка %s"),
			Cell, bOpen ? TEXT("открыто") : TEXT("ЗАКРЫТО"),
			Cells.IsValidIndex(Cell) ? TEXT("есть") : TEXT("вне диапазона"));
		return;
	}
	const int32 Code = Cells[Cell].ItemCode;
	if (Code <= 0)
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] клик по ячейке %d отбит: в ячейке нет товара (страница %d/%d)"),
			Cell, Page, MaxPage);
		return;
	}
	UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] клик «купить» по ячейке %d, код=%d"), Cell, Code);
	OnCellClicked(Cell);
	PopulatePurPanel(Code, /*DialogMode*/1, /*bShow*/true);
}

// Кнопка ячейки «Подарок» 15307 — режим 2 (`uiShopDialogCommandHandler`, ui_core.c:10142-10167). ★ В оригинале кнопка СКРЫТА (Д-1);
//   ★ 01.11.2026 P101_CashGift: видна при включённом подарке (`AsdaCashGiftOn`), путь 15520 → 5072, OK 1559 → 5074.
void UAsdaBoosterShopSubsystem::OnCellGiftClicked(int32 Cell)
{
	if (!bOpen || !Cells.IsValidIndex(Cell)) { return; }
	const int32 Code = Cells[Cell].ItemCode;
	if (Code <= 0) { return; }
	OnCellClicked(Cell);
	PopulatePurPanel(Code, /*DialogMode*/2, /*bShow*/true);
	UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] режим подарка: ячейка %d, код %d (подарок %s)"),
		Cell, Code, AsdaCashGiftOn() ? TEXT("включён") : TEXT("ВЫКЛЮЧЕН — 5072/5074 не уйдут"));
}

// ★★★ Кнопка 15527 → ОБЩАЯ МОДАЛКА 1559, РЕЖИМ F. Второй модалки не заводим.
//   Эталон: `CUpgradeUI_FillItemInfoPanelEx(g_pItemTooltipUI, this[611], 0, 1, 1)` (`ida.c:422597`).
void UAsdaBoosterShopSubsystem::OnPurOkClicked()
{
	if (!bOpen || PurMode == 0)
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] «ОК» 15527 отбит: окно %s, панель покупки %s"),
			bOpen ? TEXT("открыто") : TEXT("ЗАКРЫТО"),
			PurMode == 0 ? TEXT("НЕ ПОКАЗАНА") : TEXT("показана"));
		return;
	}
	// ★ 07.10.2026 (волна 2-Г, ревизия С-2) [ПОРТ]: режим ПОДАРКА (2, кнопка 15307 — видна только при
	//   `asda.PremiumMall.ShowBoosterGift 1`) у эталона ведёт к 5074 (`BoosterShop_SendBuyPresent`), а сервер этого
	//   опкода НЕ принимает (нет `case 0x13D2`; 6 неизвестных пакетов = разрыв, отчёт owner_1006/wave2D_gift_townwar.md §1.4).
	//   Прежний порт открывал модалку 1559 «подарок» и слал 5070 — покупку СЕБЕ со списанием. Отправку в этом режиме
	//   блокируем целиком (ни 5070, ни 5074), пока нет серверного патча; игрок видит ту же строку, что у 15520.
	if (PurMode == 2 && !AsdaCashGiftOn())
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] «ОК» 15527 в режиме подарка: подарок выключен (сервер без P101) — отправки нет "
			     "(5070 купил бы себе)"));
		if (CVarNoticeBadItem.GetValueOnGameThread() != 0) { NoticeTip(kLocBadItem); }
		return;
	}
	// ★ 01.11.2026 P101_CashGift: режим ПОДАРКА — модалка 1559, её ОК → 5074 (`BoosterShop_OnBuyConfirm@0xB88660`, shop_trade.c:1813-1832).
	//   Гейт 5071 «покупка в полёте» здесь не стоит: у эталона его нет, а подарок — не покупка 5070.
	if (PurMode == 2)
	{
		CollectSelections();
		if (PurItemCode <= 0)
		{
			if (CVarNoticeBadItem.GetValueOnGameThread() != 0) { NoticeTip(kLocBadItem); }
			return;
		}
		UGameInstance* GIg = GetGameInstance();
		UAsdaPurchaseBoxSubsystem* BoxG = GIg ? GIg->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
		UAsdaCashGiftSubsystem* Gift = GIg ? GIg->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr;
		if (!BoxG || !Gift) { UE_LOG(LogAsdaBoosterShop, Error, TEXT("[магазин бустеров] нет модалки 1559 / подсистемы подарка — отмена")); return; }
		FAsdaPurchaseBoxRequest ReqG;
		ReqG.Mode      = EAsdaPurchaseMode::Booster;   // ★ режим F
		ReqG.ItemCode  = PurItemCode;
		// [ИСПРАВЛЕНИЕ] Д-К1 (`asda.CashGift.FixK1`): эталон `CUpgradeUI_FillItemInfoPanelEx(…, panelMode = 0, …)` (ui_core.c:10127)
		//   показывал при подарке «Хочешь купить эту вещь?» 12923 без «Кому»; исправлено — «Кому» 20196 (подтверждённое имя
		//   `byte_2BE811E`, куда реально уйдёт 5074) + вопрос 12922.
		ReqG.PanelMode = AsdaCashGiftFixOn(1) ? 1 : 0;
		ReqG.RecipientName = Gift->GetConfirmedName();
		ReqG.Quantity  = CVarLegacyBoxPrice.GetValueOnGameThread() != 0 ? 1 : PurQty;
		ReqG.PriceOverride = CVarLegacyBoxPrice.GetValueOnGameThread() != 0 ? -1 : (PurQty * PurPriceOne);
		ReqG.TradeMode = 1;                             // подарок — в сумку получателя (строка «Рюкзак»)
		ReqG.LevelOverride = PurLevel;
		BoxG->Open(ReqG, [this](bool bOk)
		{
			if (!bOk) { return; }
			CollectSelections();   // `formCollectInputAndSelections` — первым делом в `BoosterShop_OnBuyConfirm`
			UAsdaCashGiftSubsystem* G = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr;
			const FString Field = PurName ? PurName->GetText().ToString() : FString();
			if (G && G->SendPresent(EAsdaGiftSource::Booster15300, PurItemCode, PurQty, CashGxp, Field))
			{
				// Успех: `SetFocus(15519, 0)`, панель сброшена, окно «Вид» закрыто (shop_trade.c:1823-1826).
				OnPurCancelClicked();
			}
		});
		return;
	}
	CollectSelections();
	if (PurItemCode <= 0)
	{
		// Эталон здесь молча ничего не делает (Д-13) — показываем причину.
		if (CVarNoticeBadItem.GetValueOnGameThread() != 0) { NoticeTip(kLocBadItem); }
		return;
	}
	// Гейт «запрос уже в полёте» — 1:1 окно 6000 (`this[2071]` → нотис 1026).
	//   [ИСПРАВЛЕНИЕ] 25.10.2026: идущая поштучная покупка — тоже «в полёте» (между ответом и следующей штукой).
	if (bPending || BuyRun.bRun)
	{
		NoticeChat(kLocNoAnswer);
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] «ОК» 15527 отбит: ждём ответ 5071 на прошлый запрос "
			     "(защёлка снимается ответом сервера, закрытием окна или по таймауту %.0f с)"),
			CVarPendingTimeout.GetValueOnGameThread());
		return;
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaPurchaseBoxSubsystem* Box = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
	if (!Box)
	{
		// Правило владельца: не защёлкивать состояние, если экран показать нечем.
		UE_LOG(LogAsdaBoosterShop, Error, TEXT("[магазин бустеров] нет подсистемы модалки 1559 — покупка отменена"));
		return;
	}

	const bool bLegacyTrade = CVarLegacyBoxTrade.GetValueOnGameThread() != 0;
	const bool bLegacyPrice = CVarLegacyBoxPrice.GetValueOnGameThread() != 0;

	FAsdaPurchaseBoxRequest Req;
	Req.Mode      = EAsdaPurchaseMode::Booster;          // ★ режим F
	Req.ItemCode  = PurItemCode;
	Req.PanelMode = (PurMode == 2) ? 1 : 0;              // 1 = подарок (строка 1560 + вопрос 12922)
	Req.RecipientName = (PurName && PurMode == 2) ? PurName->GetText().ToString() : FString();
	// ★ Б-2: эталон передаёт `stackCount = 1` ⇒ модалка печатает цену ЗА ШТУКУ, хотя рядом в
	//   панели стоит сумма за выбранное количество. Исправляем; ключ возвращает оригинал.
	Req.Quantity  = bLegacyPrice ? 1 : PurQty;
	Req.PriceOverride = bLegacyPrice ? -1 : (PurQty * PurPriceOne);
	// ★ Б-1: эталон передаёт `upgradeStateOverride = 1` ⇒ строка «Поместить на хранение» ВСЕГДА
	//   «Рюкзак», даже когда покупка уйдёт на склад. Показываем реальное назначение.
	Req.TradeMode = bLegacyTrade ? 1 : (bPurToWarehouse ? 0 : 1);
	// ★ Б-3 (особенность, не дефект): у режима F уровень в модалке берётся НЕ из записи предмета,
	//   а из КОМБО-2 магазина (`sub_91FDF6(g_bModalNoticeActive)`, `ida.c:594213`). Передаём его.
	Req.LevelOverride = PurLevel;

	Box->Open(Req, [this](bool bOk)
	{
		if (!bOk) { return; }         // отказ доходит до владельца (исправление D6 модалки)
		SendBuy();
	});
}

void UAsdaBoosterShopSubsystem::OnPurCancelClicked()
{
	// Эталон `case 15530`: сброс панели + снятие фокуса с поля имени + закрытие окна «Вид».
	PopulatePurPanel(0, 0, false);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaBoosterPanelSubsystem* Prev = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>()) { Prev->ClosePrevWindow(); }
	}
}

// Кнопка 15547 «Вид» → окно предпросмотра 15600. ★ ОБЩАЯ ТОЧКА: то же окно и та же функция
//   заполнения (`boosterWindowPopulate@0xB82D90`) уже портированы в `UAsdaBoosterPanelSubsystem`.
void UAsdaBoosterShopSubsystem::OnPurPreviewClicked()
{
	if (!bOpen || PurMode == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaBoosterPanelSubsystem* Prev = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
	if (!Prev)
	{
		UE_LOG(LogAsdaBoosterShop, Error, TEXT("[магазин бустеров] нет подсистемы окна «Вид» 15600"));
		return;
	}
	// ★ 31.10.2026: общий вход «Вида» (эталон `case 15547`: `v12[662] = 0` + `SetSelectedIndex(661, 0)`; флаг `+697` = 1).
	Prev->OpenPrevWindowFrom(PurItemCode > 0 ? PurItemCode : PurBaseCode, EAsdaPrevSource::BoosterShop, /*bCellsActive*/ true);
	PlacePreviewBesideRow(SelCell);
}

// Кнопка 15520 «ОК» подарка — эталон шлёт **5072** (`uiBoosterComposeOnCommand` case 15520, ui_core.c:10115-10124:
//   `uiEditGetWindowTextWithSuffix(15519, 0x14)` → `BoosterShop_SendBuy`). ★ 01.11.2026 P101_CashGift: отправитель — общий
//   `UAsdaCashGiftSubsystem::SendCheckName` (гейтов у эталона нет; [ИСПРАВЛЕНИЕ] Д-К5 — гейты 6000 под ключом).
void UAsdaBoosterShopSubsystem::OnPurConfirmClicked()
{
	if (!AsdaCashGiftOn())
	{
		UE_LOG(LogAsdaBoosterShop, Warning,
			TEXT("[магазин бустеров] кнопка 15520: подарок выключен (сервер без P101) — 5072 не уходит"));
		if (CVarNoticeBadItem.GetValueOnGameThread() != 0) { NoticeTip(kLocBadItem); }
		return;
	}
	if (!bOpen || PurMode != 2) { return; }
	UAsdaCashGiftSubsystem* Gift = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCashGiftSubsystem>() : nullptr;
	const FString Name = PurName ? PurName->GetText().ToString() : FString();
	if (Gift) { Gift->SendCheckName(EAsdaGiftSource::Booster15300, Name); }
}

void UAsdaBoosterShopSubsystem::SetQuantity(int32 Qty)
{
	PurQty = FMath::Clamp(Qty, 1, kMaxQty);
	RefreshPurPrice();
}

void UAsdaBoosterShopSubsystem::SetVariantJob(int32 Idx)
{
	if (!JobList.IsValidIndex(Idx)) { return; }
	PurJob = JobList[Idx];
	RebuildLevelCombo();
}

void UAsdaBoosterShopSubsystem::SetVariantLevel(int32 Idx)
{
	if (!LevelList.IsValidIndex(Idx)) { return; }
	PurLevel = LevelList[Idx];
	CollectSelections();
}

void UAsdaBoosterShopSubsystem::SetToWarehouse(bool bOn)
{
	bPurToWarehouse = bOn;
	if (PurWareChk) { PurWareChk->SetIsChecked(bOn); }
}

// Кнопка 15513 «Пополнить» — эталон `UI_OpenDonatePage_ECACA0@0xECACA0` (внешний браузер).
//   ★ Пароль в ссылку НЕ кладём (решение этапа 1: пакет 11002 не обрабатываем).
void UAsdaBoosterShopSubsystem::OnChargeClicked()
{
	// ★ 27.10.2026 (владелец): внешний браузер не открываем — ссылка на area-game.ru снята до своего сайта.
	UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] «пополнить» (15513) → ссылка снята, ничего не открываем"));
}

// Кнопка 15514 «Проверить баланс» → опкод 5466 (`ida.c:422554-422558`).
void UAsdaBoosterShopSubsystem::OnBalanceClicked()
{
	AsdaShopUi::QueryPremiumBalance(GetGameInstance());   // ★ волна 3: пакет 5466 с выбранным персонажем — общая точка
}

void UAsdaBoosterShopSubsystem::DebugSetGxp(int32 Value)
{
	CashGxp = Value;
	RefreshStatTexts();
	RefreshPurPrice();
}

// ═══════════════════════════════ СЕТЬ ══════════════════════════════════════════════════════════
// ★★★ ЖИВОЙ ПУТЬ ОДИН: 5070 → 5071. Сборщик тела и отправитель — ОБЩИЕ ТОЧКИ `UAsdaNetSubsystem`
//   (паддинг `a6 = 1` ставится). Покупка N штук = N одинаковых кодов в первых N слотах, как у
//   эталона (`BoosterShop_SendBuyVariant@0xB85380`).
void UAsdaBoosterShopSubsystem::SendBuy()
{
	if (PurItemCode <= 0) { return; }

	TArray<int32> Codes;
	// ★ К1/К2 оригинала: цикл шёл до `count` с условием `slotIdx != 7` при буфере 172 б.
	//   Сборщик тела кладёт ровно 7 записей; здесь количество и так ограничено комбо (1…7).
	const int32 N = FMath::Clamp(PurQty, 1, kMaxQty);
	for (int32 i = 0; i < N; ++i) { Codes.Add(PurItemCode); }
	const int32 Code = PurItemCode;
	bBuyRunToInventory = !bPurToWarehouse;   // доставка — снимок чекбокса 15528 на момент «ОК»

	// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 «купил N — пришла 1» (шапка `AsdaPremiumBuyQueue.h`): старый GameServer с шлюзом GP7
	//   из пакета 5070 выдаёт только позицию 0, а списывает за все N. Поэтому N > 1 — по одной штуке на пакет,
	//   следующая — после ответа 5071 с кодом 1. Ключ `asda.PremiumMall.CartOnePerPacket 0` — как оригинал (одним пакетом).
	if (N > 1 && AsdaPremiumCartOnePerPacketOn())
	{
		BuyRun.Start(Codes);
		UE_LOG(LogAsdaBoosterShop, Log,
			TEXT("[магазин бустеров] покупка поштучно: код=%d ×%d, по одному 5070 (сервер с GP7 выдаёт из пакета одну вещь)"),
			Code, N);
		SendNextBuyItem();
		if (BuyRun.bRun) { PopulatePurPanel(0, 0, false); }
		return;
	}

	if (!SendOneBuy(Codes)) { return; }
	PopulatePurPanel(0, 0, false);
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] →5070: код=%d ×%d доставка=%s"),
		Code, N, bBuyRunToInventory ? TEXT("инвентарь") : TEXT("хранилище"));
}

// Один пакет 5070 с этими кодами (тело — общая точка `BuildPremiumShopBody`); true = ушёл и защёлка взведена.
bool UAsdaBoosterShopSubsystem::SendOneBuy(const TArray<int32>& Codes)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || Codes.Num() == 0) { return false; }

	uint8 Body[171];
	// `+0` режим = 1 (константа эталона); `+1` таблица: 1 = `item_list`;
	// `+2` доставка: 1 = рюкзак, 0 = склад (`= (чекбокс == 0)`).
	UAsdaNetSubsystem::BuildPremiumShopBody(Codes, /*Mode*/1, /*bTable1*/true,
		/*bToInventory*/bBuyRunToInventory, Body);
#if ASDA_DEV_CONSOLE   // [ИСПРАВЛЕНИЕ] ревизия wave_1025 М-6: перехват самотеста — только в дев-сборке
	if (bDebugCapture)
	{
		DebugSent.Add(TArray<uint8>(Body, 171));   // самотест: тело перехвачено, в сокет не уходит
	}
	else
#endif
	if (!Net->SendPremiumShopBuy5070(Body))
	{
		// ★ Молчать здесь нельзя: снаружи это ровно «бустеры не покупаются».
		UE_LOG(LogAsdaBoosterShop, Error,
			TEXT("[магазин бустеров] 5070 НЕ УШЁЛ (нет соединения с сервером): код=%d ×%d — покупки не будет"),
			Codes[0], Codes.Num());
		return false;
	}

	// ★ Защёлку ставим ТОЛЬКО после успешной отправки.
	bPending = true;
	PendingSince = FPlatformTime::Seconds();
	PendingBuyCodes = Codes;
	++CntBuy;
	return true;
}

// [ИСПРАВЛЕНИЕ] 25.10.2026: следующая штука запуска (зовётся из Tick, не из сетевого колбэка 5071).
void UAsdaBoosterShopSubsystem::SendNextBuyItem()
{
	const int32 Code = BuyRun.PopNext();
	if (Code <= 0) { StopBuyRun(TEXT("позиций не осталось")); return; }
	if (!SendOneBuy({ Code })) { StopBuyRun(TEXT("позиция не ушла")); return; }
	UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] покупка поштучно: штука %d из %d — код %d доставка=%s"),
		BuyRun.Sent, BuyRun.Total, Code, bBuyRunToInventory ? TEXT("инвентарь") : TEXT("хранилище"));
}

// [ИСПРАВЛЕНИЕ] 25.10.2026: прервать запуск — неотправленные штуки не покупаются и не списываются.
void UAsdaBoosterShopSubsystem::StopBuyRun(const TCHAR* Why)
{
	if (!BuyRun.bRun) { return; }
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] покупка поштучно остановлена: %s — куплено %d из %d, не отправлено %d"),
		Why, BuyRun.Ok, BuyRun.Total, BuyRun.Left.Num());
	BuyRun.Stop();
}

// Порт `shop_OnRecvTransactionResult@0xC30BD0` в части, касающейся нашего запроса.
//   ⚠ Делегат 5071 общий с окном 6000; реагируем ТОЛЬКО если ждали ответа сами.
void UAsdaBoosterShopSubsystem::OnBuyResult(const TArray<uint8>& B)
{
	if (!bPending || B.Num() < 407) { return; }
	++CntResult;
	bPending = false;

	const uint8 Code     = B[0];
	const bool  bGxp     = (B[1] != 0);
	const int32 Balance  = AsdaBytes::RdI32(&B[2]);
	const int32 Weight   = AsdaBytes::RdI16(&B[6]);
	const bool  bToStore = ((int32)B[12] == 5);

	// [ИСПРАВЛЕНИЕ] 26.10.2026: баланс — только из успешного ответа (код 1), как эталон; разбор — у `CVarFailKeepsBalance`.
	if (Code == 1 || CVarFailKeepsBalance.GetValueOnGameThread() == 0)
	{
		CashGxp = Balance;
		RefreshStatTexts();
		RefreshPurPrice();
	}
	else
	{
		UE_LOG(LogAsdaBoosterShop, Log,
			TEXT("[магазин бустеров] 5071 отказ код %d: баланс из пакета %d не применён (эталон пишет баланс только при коде 1), остаётся %d"),
			(int32)Code, Balance, CashGxp);
	}

	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaMessageSubsystem* MS = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;

	if (Code == 1)
	{
		// ★★★ УСПЕХ. Строки 20061…20064 несут ДВА СТРОКОВЫХ маркера: `[ss1]` имя, `[ss2]` сумма
		//   С РАЗРЯДАМИ (их ставит ВЫЗЫВАЮЩИЙ через `String_FormatThousands`, а не форматтер).
		const int32 LocId = bGxp ? (bToStore ? kOkGxpStore : kOkGxpBag)
		                         : (bToStore ? kOkCharmStore : kOkCharmBag);
		for (int32 ItemCode : PendingBuyCodes)
		{
			FString Name; int32 Price = 0;
			if (const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr) { Name = IT->GetItemName(ItemCode); Price = R->Price; }   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
			if (!bGxp)
			{
				// ⚠ Цена в очках обаяния лежит в `ItemRecord + 0xB8`, которого в `DT_item_list` нет.
				UE_LOG(LogAsdaBoosterShop, Warning,
					TEXT("[магазин бустеров] 5071: оплата ОЧКАМИ ОБАЯНИЯ; поле цены +0xB8 не выгружено — "
					     "в нотисе %d показана цена в GXP"), LocId);
			}
			if (MS) { MS->PostLocFmt(EAsdaMsgCategory::Trade, LocId, { Name, AsdaUi::FormatThousands(Price) }, {}); }
		}
		if (BuyRun.bRun)
		{
			// [ИСПРАВЛЕНИЕ] 25.10.2026 поштучно: штука подтверждена; следующую шлёт Tick (не из сетевого колбэка).
			++BuyRun.Ok;
			if (BuyRun.HasMore()) { BuyRun.bSendNext = true; }
			else
			{
				UE_LOG(LogAsdaBoosterShop, Log, TEXT("[магазин бустеров] покупка поштучно: куплено %d из %d"), BuyRun.Ok, BuyRun.Total);
				BuyRun.Stop();
			}
		}
	}
	else
	{
		// [ИСПРАВЛЕНИЕ] 25.10.2026 поштучно: отказ останавливает запуск — остальные штуки не покупаются.
		if (BuyRun.bRun) { StopBuyRun(TEXT("отказ сервера (код 5071 ≠ 1)")); }
		int32 LocId = (Code < UE_ARRAY_COUNT(kResLoc)) ? kResLoc[Code] : 0;
		if (Code == 0x0E && (!AsdaPremiumShow12961On() || AsdaPremium12961Missing(Loc(LocId))))   // ★ R5-fix-b 13.11.2026 (второй заход): ключ Show12961
		{
			// К11: в русском эталоне строки 12961 нет — оригинал показывал чужую запасную строку.
			//   ★ 13.11.2026 (строка 12961): строка записана (RU «Не продаётся», EN «Not For Sale»); 9570 — только если её нет.
			LocId = kResFallback;
			UE_LOG(LogAsdaBoosterShop, Warning,
				TEXT("[магазин бустеров] код результата 0x0E: %s — показана %d"),   // ★ R5-fix-b 13.11.2026 (третий заход): причина по ключу
				AsdaPremiumShow12961On() ? TEXT("строки 12961 нет в таблице текста (К11)") : TEXT("ключ показа 12961 = 0 (как до 13.11.2026)"), kResFallback);
		}
		// ★ Код 4 «Не хватает GXP» эталон шлёт В ЧАТ, остальные — в общую плашку (§3.6).
		if (LocId > 0) { if (LocId == kLocNoGxpChat) { NoticeChat(LocId); } else { NoticeTip(LocId); } }
	}
	PendingBuyCodes.Reset();
	UE_LOG(LogAsdaBoosterShop, Log,
		TEXT("[магазин бустеров] 5071: код=%d валюта=%s баланс=%d вес=%d доставка=%s"),
		(int32)Code, bGxp ? TEXT("GXP") : TEXT("обаяние"), Balance, Weight,
		bToStore ? TEXT("хранилище") : TEXT("инвентарь"));
}

// Порт `netHandleAuthResult@0xECB480` (`ida.c:721295-721325`) в части бустер-шопа.
void UAsdaBoosterShopSubsystem::OnBalance(uint8 Status, int32 Balance)
{
	if (!bOpen) { return; }
	if (Status == 1) { CashGxp = Balance; RefreshStatTexts(); RefreshPurPrice(); NoticeChat(kLocGxpOk); }
	else if (Status == 0) { NoticeChat(kLocNoCharGxp); }
	else if (Status == 2) { NoticeChat(kLocGxpBad); }
	else if (Status == 3) { NoticeChat(kLocGxpFriend); }
}

// ═══════════════════════════════ ТИК ═══════════════════════════════════════════════════════════
void UAsdaBoosterShopSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.BoosterShop");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// [ИСПРАВЛЕНИЕ] 25.10.2026 поштучная покупка: следующую штуку шлём отсюда — ответ 5071 пришёл в сетевом колбэке.
	//   Стоит ДО выхода по `bReady`: запуск идёт и тогда, когда окно ещё не дорисовано (самотест без окна — тоже).
	if (BuyRun.bSendNext)
	{
		BuyRun.bSendNext = false;
		if (!bOpen) { StopBuyRun(TEXT("окно 15300 закрыто")); }
		else if (bPending) { StopBuyRun(TEXT("защёлка уже взведена другим запросом")); }
		else { SendNextBuyItem(); }
	}
	if (!bOpen || !bReady) { return; }

	// Отложенная постановка окна «Вид»: на первом кадре живой геометрии рамки ещё нет.
	if (PrevPlaceCellPending >= 0) { PlacePreviewBesideRow(PrevPlaceCellPending); }

	// Д-18: снять зависшую защёлку «запрос в полёте», иначе покупки молча отбиваются навсегда.
	if (bPending)
	{
		const float Limit = CVarPendingTimeout.GetValueOnGameThread();
		if (Limit > 0.f && (FPlatformTime::Seconds() - PendingSince) > (double)Limit)
		{
			bPending = false;
			UE_LOG(LogAsdaBoosterShop, Warning,
				TEXT("[магазин бустеров] ответ 5071 не пришёл за %.0f с — защёлка снята (Д-18), "
				     "покупки снова доступны. Прошлый запрос: код=%d ×%d"),
				Limit, PurBaseCode, PendingBuyCodes.Num());
			if (BuyRun.bRun) { StopBuyRun(TEXT("ответ 5071 не пришёл (таймаут Д-18)")); }
		}
	}

	// ОДИН опрос мыши на окно (закон общей точки `FAsdaMouseFrame`): второй опрос в том же кадре
	//   съел бы фронты у комбобоксов.
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		TickTooltip(-1);        // уже висящую подсказку карточки ГАСИМ, а не просто «не показываем»
		Mouse.Reset();
		return;
	}
	if (PurMode != 0)
	{
		const bool bJobOpen = ComboJob && ComboJob->IsOpen();
		const bool bLvlOpen = ComboLevel && ComboLevel->IsOpen();
		const bool bQtyOpen = ComboQty && ComboQty->IsOpen();
		if (ComboJob   && !bLvlOpen && !bQtyOpen) { ComboJob->Tick(Mouse); }
		if (ComboLevel && !bJobOpen && !bQtyOpen) { ComboLevel->Tick(Mouse); }
		if (ComboQty   && !bJobOpen && !bLvlOpen) { ComboQty->Tick(Mouse); }
	}

	if (CVarTooltips.GetValueOnGameThread() == 0) { return; }
	int32 Over = -1;
	for (int32 i = 0; i < kCells; ++i)
	{
		if (Cells[i].ItemCode <= 0 || !Cells[i].ItemIcon) { continue; }
		if (AsdaUi::IsCursorOverWidget(Cells[i].ItemIcon.Get())) { Over = i; break; }
	}
	TickTooltip(Over);
}

// НАША ДОБАВКА (в оригинале у `CBoosterShop` обработчика 8/9/0xA нет). Гасим ТОЛЬКО СВОЮ
//   плашку — закон `ui-shared-tip-ownership`.
void UAsdaBoosterShopSubsystem::TickTooltip(int32 OverCell)
{
	// ★ Волна 3: общая точка `AsdaShopUi::TickCardTooltip` (бустер-шоп ≡ премиум-магазин; код предмета берётся только при OverCell >= 0).
	AsdaShopUi::TickCardTooltip(GetGameInstance(), TipCell, OverCell, EAsdaTipOwner::BoosterShop,
		[this](int32 I) { return Cells[I].ItemCode; });
}
