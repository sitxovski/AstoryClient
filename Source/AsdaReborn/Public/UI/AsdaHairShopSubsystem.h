// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaHairShopSubsystem — ★★★ ПАРИКМАХЕРСКАЯ, окно `HAIRSHOP_WIN` (JUI IDNum **15000**,
//   224 узла) + панель подтверждения покупки `HAIR_PURCHASE_WIN` (**15168**).
//   Полный реверс — `Reports/npc_hairshop_30029.md` (789 строк). Ни одно число не выдумано.
//
// ═══════════════ КЛАССИФИКАЦИЯ (закон `classify-before-building`) ══════════════════════════════
//   НОВАЯ СИСТЕМА: собственное корневое окно `.JUI`, собственный жизненный цикл («сессия»
//   парикмахерской — гейт для чужих окон), собственный протокол (5470/5471/5472) и собственная
//   таблица данных (`hairtable`, 228 записей). Переиспользуются ОБЩИЕ ТОЧКИ, а не копии:
//     • модалка подтверждения — `UAsdaPurchaseBoxSubsystem` (окно 1559), режим **E** (кнопка 15178);
//     • баланс премиум-валюты — `UAsdaNetSubsystem::SendPremiumBalanceQuery5466` + `OnPremiumBalance`;
//     • меш причёски — `UAsdaWorldSubsystem::ApplySelfHair` / `ApplyRemoteHair`
//       (обе ведут в единственную пересборку `UAsdaCharAssembly::BuildWornParts`);
//     • иконка из атласа — `AsdaUi::ApplyJuiIconCell` (эталон `Grid_IndexToCellOffset8x64`);
//     • нумпад 1410 — только `CloseNumPad` при открытии (§9 отчёта, сверено с `NUMPAD_FLOW.md`).
//
// ═══════════════ ★★★ ЕДИНСТВЕННЫЙ ПУБЛИЧНЫЙ ВХОД: `OpenWindow()` ══════════════════════════════
//   Оригинальные входы (§1.2 отчёта):
//     • код диалога NPC **30029** — В ДАННЫХ НЕ ИСПОЛЬЗУЕТСЯ (0 вхождений в `.nsr` семи клиентов,
//       положительный контроль 30035 найден) ⇒ живого NPC-входа у оригинала НЕТ;
//     • сегмент **2** переключателя `PremiumMall_Menu` 30002 — единственный живой вход оригинала.
//   ★ РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28: полоса-переключатель 30002 из игры УБРАНА, и вход в
//     парикмахерскую выводится КНОПКОЙ NPC **132 «Парикмахер Юки»**. Данные кнопки и ветку
//     диалога делает владелец; отсюда наружу торчит ровно этот метод — `OpenWindow()`.
//
// ═══════════════ ПОРЯДОК ОТКРЫТИЯ (`uiHairShopWindowOnShow@0xCEEE90`, `ui_core.c:45638-45680`) ══
//   1 ленивая привязка контролов · 2 ГЕЙТЫ `actionExecuteSlotAbility@0xCF0900` (7 шт., все —
//   молчаливый отказ) · 3 признак «в парикмахерской» = 1 · 4 полный сброс окна ·
//   5 запомнить клетку игрока · 6 запомнить шлем/причёску/цвет для отката · 7 СНЯТЬ ШЛЕМ ·
//   8 закрыть приёмники UI · 9 ★ ЗАКРЫТЬ ОКНО КОЛИЧЕСТВА 1410 · 10 запрос баланса **5466** ·
//   11 нотисы **13013** и **13014** в чат · 12 закрыть окно диалога NPC · 13 показать окно.
//
// ═══════════════ КАРТОЧКИ: 14 штук, шаг IDNum **9**, база **15005** ═══════════════════════════
//   `HAIR_ITEM{nn}` 15005+9i · `_TITLE{nn}` +1 имя · `_PRI{nn}` +2 цена · `_ICON{nn}` +3 иконка ·
//   `_BUY{nn}` +4 кнопка «Купить» (команда 0x3AA1+9i) · `_MAC{nn}` +5 значок кэша ·
//   `HAIR_NEW/HOT{nn}` +6/+7 бейджи (Vis=0) · `_LV{nn}` +8 требуемый уровень.
//   Раскладка 2×7: x = 7 и 223, y = 15 + 98·row.
//   ★★★ КАРТОЧКИ 11…14 (`HAIR_ITEM11…14`) имеют `Visible = "0"` В ДАННЫХ — и это НЕ прихоть:
//     подложка `HAIR_ITEMBASE` 15004 = 446×**520**, а строка 5 начинается на y = 505 и кончается
//     на 595, строка 6 — на 693. Они физически НЕ ВЛЕЗАЮТ в белую рамку. Поэтому эталон и держит
//     видимыми 10 гнёзд из 14 (ключ `asda.HairShop.Rows11to14Hidden`).
//     ⚠ НО эталон при этом РАСКЛАДЫВАЕТ СТРАНИЦУ ПО 14 и считает пагинацию по 14 — четыре записи
//     на каждой странице оказываются НЕДОСТИЖИМЫ (дефект **D8**). Порт по умолчанию считает
//     страницу по числу ВИДИМЫХ карточек (ключ `asda.HairShop.FixPageSize`).
//
// ═══════════════ ★★ СЕТКА ПО ВЫСОТЕ ЭКРАНА — РАСШИРЕНИЕ (`asda.HairShop.FitScreen`) ════════════
//   ⚠ ЧИСЛО КАРТОЧЕК В ОРИГИНАЛЕ ЖЁСТКОЕ: `refineWindowRefreshAll` дважды идёт `for (i=0;i<14;++i)`
//   (`item_enhance.c:1167, 1177`), а «лесенок» `uiRowCountByScreenHeight` / `uiList_GetVisibleRows`
//   (ими считают ячейки склад, инвентарь, бустер-шоп) в коде парикмахерской НЕТ ВОВСЕ — доказано
//   вторым методом: grep по всем трём диапазонам её функций (`ui_core.c` 45000-45700,
//   `item_enhance.c` 1100-1400, `item_inventory.c` 34400-34800) даёт 0 попаданий, при этом те же
//   имена в `item_inventory.c:20312, 21105, 21118` находятся (положительный контроль).
//   ⇒ Растягивание — РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28, ключ возвращает РОВНО оригинал.
//   ★ Формула из самих данных `.JUI`: `подложка = 98·строк + 30` (5 строк → 520 = `Size`
//   контрола `HAIR_ITEMBASE`), `пагинация = подложка + 14` (→ 534 = `Origin.y` у `HAIR_SLASH`),
//   снизу оставлен ряд кнопок (анкор 0.99, `Origin.y = −68`) ⇒ `98·строк ≤ 0.99·ВысотаUI − 212`.
//   На базовой высоте 768 это ровно 5 строк = 10 карточек, то есть исходная раскладка.
//   Карточек сверх четырнадцати в `.JUI` нет — недостающие КЛОНИРУЮТСЯ с `HAIR_ITEM01_grp`
//   общей точкой `AsdaUi::CloneWidgetSubtree` (прецедент — окно 1800).
//
// ═══════════════ ★★★ ДЕФЕКТ ПОРТА, ЗАКРЫТЫЙ 2026-08-28: «Купить» не нажималось ═════════════════
//   `UPanelWidget::ShiftChild` переставляет ТОЛЬКО UPROPERTY-массив `Slots` и НЕ трогает уже
//   построенный `SConstraintCanvas`. Прозрачная плашка-хит карточки, добавленная `AddChild` после
//   постройки окна, оставалась САМОЙ ВЕРХНЕЙ и съедала клик у кнопки «Купить» на ВСЕХ карточках
//   (карточка при этом выбиралась — отсюда и симптом). Глубину задаёт `ZOrder = −1`; ключ
//   воспроизведения `asda.HairShop.LegacyCardHitZ 1`, зонд `asda.HairShop.Probe <сек> slate`.
//
// ═══════════════ ЧТО ДЕЛАЕТ КЛИК (эталон, не догадка) ═════════════════════════════════════════
//   • клик по САМОЙ карточке (сообщение 0xB → `hairWin_SelectSlotByMsg@0xCEDCD0`) —
//     выбрать + ★ ПРИМЕРИТЬ причёску (`reloadHairOnEquipChange@0xCF1530` пишет стиль/цвет в
//     персонажа и зовёт `CCharacter_LoadHairModel`), панель НЕ открывать;
//   • кнопка «Купить» 15009+9i — то же самое И открыть панель 15168 (`uiHairShopWndShow@0xCF0D10`);
//   • 15182 «Вернуть как было» — `charReloadHairIfChanged@0xCF23B0`: вернуть сохранённые при входе
//     стиль/цвет;
//   • 15178 «Да» панели — открыть ОБЩУЮ модалку 1559 (`upgradeUiFillConfirmPanel@0xD7BD90`,
//     режим E), и только её «Подтвердить» шлёт **5470**;
//   • 15179 «Нет» — закрыть панель 15168;
//   • 15167 — повторный запрос баланса 5466; 15166 — внешний браузер (донат).
//   ★ Любое из действий при ОТКРЫТОЙ панели 15168 отбивается нотисом **13008** (гейт `sub_CF0E30`).
//
// ═══════════════ ДЕФЕКТЫ ОРИГИНАЛА (ключи отката `asda.HairShop.*`) ═══════════════════════════
//   D1  `BusyFlagLeak`      ★★★ после ПЕРВОЙ покупки все следующие отбиваются нотисом 1026:
//                           отправитель ставит `g_pHairShopWnd->gap_298[4] = 1`, а приёмник 5471
//                           сбрасывает ДРУГОЙ флаг (`g_pShopWnd[2068]`). ИСПРАВЛЕНО: флаг снимает
//                           приём 5471 (и таймаут).
//   D2  `PriceMismatch`     ★★★ на экране цена клиентской `hairtable` (1/90/120/150/180), а сервер
//                           списывает по своей `HairTypeTable.txt` (19/29). Это расхождение ДАННЫХ,
//                           кодом не лечится; ключ включает предупреждение в журнал.
//   D3  `GBoosterFlagLeak`  глобаль «магазин бустеров открыт» взводится и не снимается — в порту
//                           такой глобали нет вовсе, воспроизводить нечего (записано, не воспроизведено).
//   D4  `WrongWindowUpdate` результат 5471 обновлял контролы ЧУЖОГО окна (магазина 6000), а не
//                           15164. ИСПРАВЛЕНО: обновляем баланс СВОЕГО окна.
//   D5  `OpenNotice`        отказ гейтов молчалив; текст 13011 существует, но не эмитируется НИГДЕ.
//                           ИСПРАВЛЕНО: пишем причину отказа в журнал (в чат — только 1:1 нотисы).
//   D6  `NoDragHandle`      у окна 15000 нет ручки перетаскивания (единственная `EVT 6/RET 20003`
//                           в поддереве — 15173, и она двигает панель 15168). Оставлено 1:1.
//   D7  `FilterCheckHidden` чек-бокс фильтра 15002 `Visible = "0"`, хотя подпись 15003 видна.
//                           Оставлено 1:1 (ключ показывает чек-бокс).
//   D8  `Rows11to14Hidden`  см. выше. D9 `GuideTextHidden` — предупреждения 6960/6961 ставятся и
//                           тут же скрываются; оставлено 1:1 (ключ показывает).
//   D11 `PreCashDead`       узлы 40002/40003 «подарочный кэш» мертвы. Гасим явно.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHairShopSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UAsdaButtonRelay;

// ═══ ВИДЖЕТЫ ОДНОЙ КАРТОЧКИ `HAIR_ITEM{nn}` (IDNum 15005 + 9i) ═══
USTRUCT()
struct FAsdaHairCardUi
{
	GENERATED_BODY()
	// ВСЁ поддерево карточки (собрано по её группе `HAIR_ITEM{nn}_grp`): раскладка `.JUI`
	//   неоднородна, точечные ссылки покрывают не все планы (закон `jui-uneven-node-layout`).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Parts;
	// Узлы, которые не показываются НИКОГДА (бейджи NEW/HOT — эталон держит их скрытыми).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> AlwaysHidden;
	UPROPERTY(Transient) TObjectPtr<UWidget>    Root      = nullptr;   // `HAIR_ITEM{nn}_grp`
	UPROPERTY(Transient) TObjectPtr<UButton>    BuyButton = nullptr;   // +4 `HAIR_ITEM_BUY{nn}`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText  = nullptr;   // +1 `HAIR_ITEM_TITLE{nn}_Text`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceText = nullptr;   // +2 `HAIR_ITEM_PRI{nn}`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LevelText = nullptr;   // +8 `HAIR_ITEM_LV{nn}`
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon      = nullptr;   // +3 `HAIR_ITEM_ICON{nn}_N_0`
	// Подложка заголовка карточки (`Box_WhiteOt20`, план `HAIR_ITEM_TITLE{nn}_<n>_0`) и подпись
	//   кнопки «Купить» — их подменяет отметка «Куплено» (★ расширение, `asda.HairShop.MarkOwned`).
	UPROPERTY(Transient) TObjectPtr<UImage>     TitlePlane = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyCaption = nullptr;
	// ★ Красный бейдж «L» — скин `BoxIcon_Level`, узел `HAIR_ITEM_LV{nn}_Icon` (IDNum −1, ребёнок
	//   надписи уровня). Эталон его НИКОГДА не гасит, но `RequiredLevel` в данных 0 у всех записей,
	//   поэтому бейдж висит без числа. Показываем только вместе с числом (`LevelBadgeAlways`).
	UPROPERTY(Transient) TObjectPtr<UWidget>    LevelBadge = nullptr;
	// Прозрачная кнопка-хит на всю карточку: в оригинале клик ловит САМА карточка (сообщение 0xB),
	//   а в UMG у `Image` кликов нет. Кладём её РАНТАЙМОМ поверх поддерева карточки — узел в
	//   ассете не рисуем (перегенерация UI запрещена, а лишних механизмов не плодим).
	UPROPERTY(Transient) TObjectPtr<UButton>    HitButton = nullptr;
	int32 HairId = -1;
};

UCLASS()
class ASDAREBORN_API UAsdaHairShopSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── Числа окна: все из `gamemain_jui.ini` и эталона ──
	static constexpr int32 kWinIdNum  = 15000;   // HAIRSHOP_WIN
	static constexpr int32 kPanelId   = 15168;   // HAIR_PURCHASE_WIN
	static constexpr int32 kCards     = 14;      // HAIR_ITEM01…14, база 15005, шаг 9 — СКОЛЬКО В .JUI
	static constexpr int32 kCardsFit  = 10;      // сколько влезает в подложку 15004 (446×520)

	// ═══ ГЕОМЕТРИЯ СЕТКИ — все числа из `gamemain_jui.ini`, ни одно не подобрано ═══
	//   Карточка `HAIR_ITEM01` Origin "7 15" Size "212 90"; вторая колонка x = 223 (шаг 216);
	//   строки y = 15, 113, 211, 309, 407, 505, 603 (шаг 98). Подложка `HAIR_ITEMBASE` 15004
	//   Origin "5 69" Size "446 520"; пагинация `HAIR_SLASH` внутри подложки на y = 534, высота 23.
	static constexpr float kCardW    = 212.f;
	static constexpr float kCardH    = 90.f;
	static constexpr float kColX0    = 7.f;
	static constexpr float kColStep  = 216.f;    // 223 − 7
	static constexpr float kRowY0    = 15.f;
	static constexpr float kRowStep  = 98.f;     // 113 − 15
	static constexpr float kBaseTop  = 69.f;     // `HAIR_ITEMBASE` Origin.y
	// ★ ПРОВЕРКА ФОРМУЛЫ ПОДЛОЖКИ НА ДАННЫХ: 98·строк + 30 при 5 строках = 520 — РОВНО размер
	//   `HAIR_ITEMBASE` в `.JUI`. Значит формула не выдумана, а вычитана из оригинальной раскладки.
	static constexpr float kFramePad = 30.f;     // frameH = kRowStep·строк + kFramePad
	static constexpr float kPageGap  = 14.f;     // 534 − 520: зазор от подложки до пагинации
	static constexpr float kPageH    = 23.f;     // высота ряда пагинации (`Index_*` 23×23)
	// Нижний ряд окна (`HAIR_LMAC_CHARGE`/`HAIR_CashCheck_Btn`) — анкор 0.99, Origin.y = −68, h 25.
	static constexpr float kBottomAnchor = 0.99f;
	static constexpr float kBottomBtnY   = -68.f;
	static constexpr float kBottomMargin = 8.f;  // воздух между пагинацией и нижним рядом
	static constexpr int32 kMaxRows      = 40;   // потолок клонирования (80 карточек) — здравый смысл
	// Окно 15000 — обычное окно тела UI; модалка 1559 (8850) обязана лежать НАД ним.
	static constexpr int32 kWinZ = 735;

	// ═══════════ ★★★ ПУБЛИЧНЫЙ ВХОД (его подключает кнопка NPC 132 «Парикмахер Юки») ═══════════
	//   Полный порядок — 13 шагов `uiHairShopWindowOnShow@0xCEEE90` (см. шапку).
	//   Возвращает true, если окно реально открылось (false = не прошли гейты).
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская")
	bool OpenWindow();

	// Выход — `hairShopWindowLeave@0xCEF3A0`: откат примерки, сброс, скрытие окна, нотис 13015.
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская")
	void CloseWindow();

	// ★ 10.10.2026 [ОРИГИНАЛ] Роспись ESC окна 15000 (`RegisterEscCloser`; признак `CControl+0x228` ставит
	//   `boosterShopWindowReset@0xCEE440`, shop_trade.c:11602). Эталон `boosterShopWndProc@0xCEDB90`, событие 0x15:
	//   открыта панель покупки 15168 → `sub_9363F3` (закрыть только её); иначе `uiLeaveAndResetAllWindows` (= `CloseWindow`).
	//   До 10.10 ESC шёл в общий закрыватель «уход от NPC» и закрывал окно целиком даже при открытой панели.
	void EscClose();

	// ★ ГЕЙТ ДЛЯ ЧУЖИХ СИСТЕМ (эталонное поле `+0x298`, `CObject_GetField166`): пока сессия
	//   открыта, разговор с NPC отбивается нотисом **13009**, перемещение — **13010**, а журнал
	//   заданий и окно характеристик просто не открываются (§2.4 отчёта).
	UFUNCTION(BlueprintPure, Category="Asda|Парикмахерская")
	bool IsSessionOpen() const { return bOpen; }
	// ★ 10.10.2026: панель покупки 15168 открыта (для самотеста прохода ESC `asda.UI.SelfTestEscOnTop`).
	bool IsPurchasePanelShown() const { return bPanelOpen; }

	// ═══ ДЕЙСТВИЯ (публичны — их дёргают и мышь, и самотест) ═══
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void OnCardClicked(int32 Card);     // 0xB: выбрать + примерить
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void OnCardBuyClicked(int32 Card);  // 15009+9i
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void OnOriginHairClicked();         // 15182
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void OnPanelConfirmClicked();       // 15178 → модалка 1559
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void OnPanelCancelClicked();        // 15179
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void OnChargeClicked();             // 15166
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void OnBalanceClicked();            // 15167
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void SetPage(int32 NewPage);        // 15154/15156/15158/15160
	UFUNCTION(BlueprintCallable, Category="Asda|Парикмахерская") void SetFilterOnlyMine(bool bOn);   // 15002 (D7)

	// ★ ДИАГНОСТИКА БЕЗ СЕРВЕРА: подставить баланс кэша, который приносит 5467.
	void DebugSetCash(int32 Value) { CashGxp = Value; RefreshCash(); }

	// ═══ ДИАГНОСТИКА (закон `full-structural-analysis-always`) ═══
	FString DumpControls() const;
	FString DumpState() const;
	FString SelfTest() const;   // ФАКТИЧЕСКАЯ экранная геометрия после отрисовки

	// ★★★ ЖИВОЙ ЗОНД «КТО СЪЕЛ КЛИК» (тот же приём, что у под-вкладок окна 900):
	//   для КАЖДОЙ карточки печатает кнопку «Купить» (имя, вкл/выкл, вид, слот, живой прямоугольник)
	//   и спрашивает у Slate, какой виджет реально лежит под её центром
	//   (`FSlateApplication::LocateWindowUnderMouse`). Если кнопки нет в пути — её перекрыли, и это
	//   видно ОДНОЙ строкой, а не догадкой по дампу ассета.
	FString ProbeCards(bool bSlate = true) const;
	// НАСТОЯЩИЙ клик мышью по кнопке «Купить» карточки: хит-тест путём + `SButton::SimulateClick`.
	//   Проверяется ИМЕННО доставка клика, а не наша привязка (её проверяет `asda.HairShop.Card`).
	void ClickCardBuy(int32 Card);

private:
	// ── Данные ──
	void  BuildList();                    // фильтр `hairtable`: пол + Enabled (+ уровень по чек-боксу)
	int32 CardsPerPage() const;           // ★ D8: по видимым карточкам, а не по 14
	const struct FAsdaHairtableRow* RowById(int32 HairId) const;

	// ── Виджет ──
	bool EnsureWidget();
	void SetupControls();
	void SetupCards();

	// ═══ ★★ РАСКЛАДКА ПОД ВЫСОТУ ЭКРАНА (ключ `asda.HairShop.FitScreen`) ═══════════════════════
	//   ⚠ ОРИГИНАЛ ЭТОГО НЕ ДЕЛАЕТ: окно 15000 жёстко задаёт 14 карточек — `refineWindowRefreshAll`
	//     (`item_enhance.c:1167,1177`) дважды идёт `for (i = 0; i < 14; ++i)`, а помощников
	//     `uiRowCountByScreenHeight@…` / `uiList_GetVisibleRows` в КОДЕ ПАРИКМАХЕРСКОЙ нет вовсе
	//     (проверено двумя методами: grep по диапазонам её функций в `ui_core.c` 45000-45700,
	//     `item_enhance.c` 1100-1400, `item_inventory.c` 34400-34800 — 0 попаданий).
	//   ⇒ Это РАСШИРЕНИЕ по решению владельца, ключ отката возвращает ровно оригинал.
	//   ★ Формула откалибрована на данных: при высоте экрана 768 (база оригинала) она даёт РОВНО
	//     5 строк = 10 карточек и подложку 520 px — то есть в точности исходную раскладку `.JUI`.
	int32 RowsForScreen() const;
	// Гарантирует, что виджетов карточек не меньше `Want`: сверх 14 (столько в `.JUI`) карточки
	//   КЛОНИРУЮТСЯ с `HAIR_ITEM01_grp` общей точкой `AsdaUi::CloneWidgetSubtree` — рисовать
	//   «свои» ячейки нельзя (закон `no-invented-ui`), дописывать `.JUI` тоже (переимпорт затрёт).
	bool EnsureCardWidgets(int32 Want);
	// Расставляет карточки по сетке, тянет 9-срезовую подложку 15004 и опускает пагинацию под неё.
	void ApplyGridLayout();
	// ★ Пересобрать сетку, если число строк изменилось (первый кадр после открытия / смена
	//   разрешения окна игры). Дешёвая проверка: считает строки и сравнивает с `GridRows`.
	void RelayoutIfNeeded();

	// ── Рендер ──
	void BuildPage();
	void PopulateCard(int32 Card, int32 HairId);
	void ShowCard(int32 Card, bool bVisible);
	void RefreshPageLabels();
	void RefreshCash();                   // 15164
	void MoveSelectionFrame(int32 Card);  // порт `uiSlotControlShowAt@0xCF1230` (рамка 15149)

	// ── Примерка ──
	void PreviewHair(int32 HairId);       // порт `reloadHairOnEquipChange@0xCF1530`
	void RevertPreview();                 // порт `charReloadHairIfChanged@0xCF23B0`

	// ★★ ОТКРЫТЬ ОБЩУЮ МОДАЛКУ 1559 (режим E) — единственный шаг подтверждения по умолчанию.
	//   Выделено из `OnPanelConfirmClicked`, потому что теперь в неё ведут ДВА пути: прямой (кнопка
	//   «Купить» карточки) и старый двухшаговый через панель 15168 (ключ `LegacyConfirmPanel`).
	void OpenConfirmModal(int32 HairId);

	// ★ Id причёски, КОТОРАЯ НА ПЕРСОНАЖЕ СЕЙЧАС (−1 — не определена).
	//   Определяется ровно так же, как это делает эталон при сборке меша волос:
	//   `findRecordByThreeKeys(g_pHairShopWnd, gender, gap_43B[38], gap_43B[39])`
	//   (`CCharacter_LoadHairModel@0x9E2700`, `entity_char.c:11452` и `:11491`) — тройка
	//   ПОЛ + HairStyle + ColorIdx, а НЕ индекс в списке и не порядок карточек.
	//   ⚠ Берём СОХРАНЁННЫЕ при входе стиль/цвет: во время примерки текущие уже подменены.
	int32 OwnedHairId() const;

	// ── Панель 15168 ──
	void OpenPanel(int32 HairId);         // порт `uiHairShopWndShow` + `ItemDetailWnd_Fill`
	void ClosePanel();                    // порт `sub_CF0D70`
	bool IsPanelOpen() const { return bPanelOpen; }
	bool GateNoPanel();                   // порт `sub_CF0E30` + нотис 13008

	// ── Сеть ──
	void OnHairResult(const TArray<uint8>& Body9);
	void OnBalance(uint8 Status, int32 Balance);

	// ── Нотисы ──
	FString Loc(int32 Id) const;
	void NoticeChat(int32 LocId) const;

	// ═══ СОСТОЯНИЕ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaHairCardUi> Cards;
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose  = nullptr;   // 15001
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOrigin = nullptr;   // 15182
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCharge = nullptr;   // 15166
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBalance= nullptr;   // 15167
	// ★ ПАГИНАЦИЯ: имена ВРУТ (миснейм M12) — `FIRST` ведёт в КОНЕЦ, `END` — в НАЧАЛО
	//   (`uiListPaginationHandler@0xCEE1C0`, `ui_core.c:45563-45584`). Верим коду, не имени.
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNext   = nullptr;   // 15154 HAIR_PAGENEXT0
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnPrev   = nullptr;   // 15156 HAIR_PAGEPRE0
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnLast   = nullptr;   // 15158 «FIRST» → В КОНЕЦ
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnFirst  = nullptr;   // 15160 «END»   → В НАЧАЛО
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPage   = nullptr;   // 15151
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPages  = nullptr;   // 15152
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCash   = nullptr;   // 15164
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCashLbl= nullptr;   // 15163 (loc 12904)
	UPROPERTY(Transient) TObjectPtr<UCheckBox>  ChkOnlyMine = nullptr; // 15002 (D7)
	// Панель 15168 и её поля.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PanelTree;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PanelAlwaysHidden;  // 15190/15191 (D9), бейджи
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurTitle  = nullptr;   // 15173 имя
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurContent= nullptr;   // 15174 описание
	UPROPERTY(Transient) TObjectPtr<UImage>     PurIcon   = nullptr;   // 15170
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurLvLbl  = nullptr;   // 15175 (loc 8487)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurLvVal  = nullptr;   // 15183
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurMacLbl = nullptr;   // 15176 (loc 2303)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurMacVal = nullptr;   // 15184
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PurAsk    = nullptr;   // 15177 (loc 13002)
	UPROPERTY(Transient) TObjectPtr<UButton>    PurOk     = nullptr;   // 15178
	UPROPERTY(Transient) TObjectPtr<UButton>    PurCancel = nullptr;   // 15179
	// Рамка выделения 15149 (поддерево `HAIR_ITEM_SELECT_*`).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SelFrame;
	// Мёртвая пара «подарочный кэш» 40002/40003 (D11).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> DeadPreCash;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.RelayAnchorSplit`): якорь кнопок карточек («Купить»/плашка) — пересобирается целиком при
	//   каждой разметке карточек (`SetupCards` зовётся повторно из `ApplyGridLayout`). Раньше реле копились в `Relays` без очистки.
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> CardRelays;
	// Подложка сетки 15004 и её 9 срезов + ряд пагинации — их геометрию ведёт `ApplyGridLayout`.
	UPROPERTY(Transient) TObjectPtr<UWidget> ItemBaseRoot = nullptr;              // `HAIR_ITEMBASE_grp`
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> ItemBaseSlices;             // `HAIR_ITEMBASE_12_0..8`
	UPROPERTY(Transient) TObjectPtr<UWidget> PageRow = nullptr;                   // `HAIR_SLASH_grp`
	int32 GridRows = 0;   // сколько строк разложено сейчас (0 = раскладка ещё не делалась)
	// Высота UI-пространства, по которой посчитаны строки (для журнала и сравнения при ресайзе).
	mutable float LastUiHeight = 0.f;
	FTimerHandle RelayoutTimer;   // отложенная пересборка сетки (в кадре открытия геометрии ещё нет)

	// ── Модель ──
	TArray<int32> ListIds;          // отфильтрованные Id причёсок в порядке таблицы
	bool  bOpen      = false;       // ★ «сессия парикмахерской» (эталонное поле +0x298)
	bool  bReady     = false;
	bool  bPanelOpen = false;
	bool  bPending   = false;       // ждём 5471 (эталонный busy; D1 — чинится приёмом)
	bool  bOnlyMine  = false;       // фильтр 15002
	int32 Page       = 0;
	int32 MaxPage    = 0;
	int32 SelCard    = -1;
	int32 PanelHairId= -1;
	int32 CashGxp    = 0;
	// Сохранённое при входе — для отката примерки (эталон `this[224]`/`this[225]`).
	int32 SavedStyle = -1;
	int32 SavedColor = -1;
	// «Домашнее» место первого плана рамки выделения 15149 в `.JUI` (иерархия расплющена, планы двигаем дельтой — закон
	//   `jui-flattened-hierarchy-geometry`). Снимается в `SetupControls` с КАЖДОГО нового виджета.
	// ★ Дедуп №2, В3-S10: заменило `LastSelDelta` («текущий сдвиг» — переживал пересоздание виджета, и рамка новой копии окна
	//   уезжала на прежний сдвиг; см. `MoveSelectionFrame`).
	FVector2D SelFrameHome = FVector2D::ZeroVector;
	int32 CntOpen = 0, CntBuy = 0, CntResult = 0;
	// Исходные (из ассета) цвет подложки заголовка и цвет подписи «Купить» — чтобы отметка
	//   «Куплено» СНИМАЛАСЬ, а не оставалась на карточке после перелистывания страницы.
	bool     bOwnedStyleSaved = false;
	FLinearColor BaseTitleTint = FLinearColor::White;
	FLinearColor BaseTitleText = FLinearColor::White;
	// Исходная подпись «Купить» карточки 01 — по ней отметка «Куплено» снимается. ★ 10.10.2026 (barber_minors): FText, а
	//   не строка — подпись в ассете стала ССЫЛКОЙ на ST_GameText:13249 (решение владельца), и копия хранит ссылку.
	FText    BaseBuyCaption;
	int32    CachedOwnedId = -1;    // Id надетой причёски, считается раз на страницу
	FDelegateHandle HResult;
	FDelegateHandle HBalance;
};
