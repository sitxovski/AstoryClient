// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaItemResetSubsystem — ОКНО СБРОСА/ВОССТАНОВЛЕНИЯ УЛУЧШЕНИЙ: `Item_Initializ_Win` (JUI **20200**).
//   Открывается кнопками диалога NPC **30046** (режим 0) и **30047** (режим 1) — это ОДНО И ТО ЖЕ
//   окно в двух режимах, а не два разных окна. Эталон: `refineWnd_Open@0xD30170(this, mode)`,
//   `game_main.c:10164-10169` (`case 30046: refineWnd_Open(g_pInventoryWnd, 0);`
//   `case 30047: refineWnd_Open(g_pInventoryWnd, 1);`).
//   Полный реверс потока с адресами — `Reports/npc_reset_upgrade_30046.md`.
//   Структурный разбор контролов — `Docs/UI/Craft/Item_Initializ_Win.md`.
//
// ═════════════════ ★★★ ЧТО ЭТО ЗА ОКНО НА САМОМ ДЕЛЕ ══════════════════════════════════════════
//   Подпись «рефайн, режим 0», стоявшая в `AsdaNpcDialogSubsystem`, по СМЫСЛУ верна, но имя
//   «рефайн» уводит: с окнами рефайн-шопа (`CRefineWnd_*@0xDC….`, звуки `S_e_refine_*`) эта
//   ветка не связана НИЧЕМ — там своя пара окон и свои пакеты. Здесь:
//     • **режим 0** — «Полностью сбросить улучшения» (заголовок 20202 = нотис **50030**):
//       уровень заточки предмета → **0**, счётчик улучшений → 0, попытки → максимум.
//       ПЛАТИТСЯ ЗОЛОТОМ.
//     • **режим 1** — «Восстановить улучшения» (заголовок = нотис **50039**): уровень заточки
//       ОСТАЁТСЯ, а попытки восстанавливаются до `максимум − уровень`, то есть возвращаются
//       НЕУДАЧНЫЕ попытки. ПЛАТИТСЯ СВИТКАМИ.
//   Оба режима работают с одной очередью целей (до 5 предметов) и шлют один и тот же пакет 6506,
//   различаясь только байтом режима и наличием блока материалов.
//
// ═════════════════ ★★★ СНЯТЫЕ МИСНЕЙМЫ (важно для чтения эталона) ═════════════════════════════
//   • `CRefineWnd_refreshMaterialSlots@0xD2FCA0` — НЕ материалы: перерисовывает пять слотов
//     **ЦЕЛЕЙ** 20330…20334 (капшн 20320 в `.JUI` = «초기화할 장비» = «снаряжение для сброса»).
//     Материалы (свитки) в этих слотах не показываются вообще.
//   • `craftWnd_processCraftRequest@0xD30810` — НЕ крафт: это покадровый насос отправки 6506.
//   • `clanWindowOnButtonEvent@0xD30480` — НЕ клан: обработчик кнопок ЭТОГО окна (20201/20350/20370).
//   • `uiInventoryOnRecvSlotUpdate@0xD31430` — приём ответа **6507** именно этого окна.
//   • Четвёртый аргумент `netSendPacket6506(this, slotKind, pSlots, itemCount)` — имя `itemCount`
//     ВРЁТ: это **слот целевого предмета** (поле записи +5), сервер читает его как `v20[2]`.
//   • `All_Reset` (20390) / `OneTree_Reset` (20391) — НЕ кнопки и НЕ чекбоксы: это `Box_WhiteOt20`
//     размером 208×1, то есть **контейнеры-режимы**. Под `All_Reset` лежит блок золота
//     (20360…20363), под `OneTree_Reset` — блок свитков (20364…20367, 20371…20374).
//     Эталон переключает режим одной парой `SetVisible`, а видимость в JUI НАСЛЕДУЕТСЯ.
//   • `honorPoints_resetDisplayIfZero` в 20363 — это ЗОЛОТО игрока, а не очки чести.
//
// ═════════════════ ПОЛЯ ЗАПИСИ ПРЕДМЕТА (57 б), на которых держится всё окно ═══════════════════
//   **+24** — уровень заточки (в порту `FAsdaItemInstance::GemGrade`);
//   **+25** — сколько улучшений уже сделано («потрачено попыток»);
//   **+26** — МАКСИМУМ попыток улучшения. Полей +25/+26 в `FAsdaItemInstance` нет — читаем из
//   `Raw` (см. `UsedTries`/`MaxTries`), чтобы не править общий заголовок инвентаря.
//   Превью «до → после» (`ItemInfoWin_Update@0xD31B60`):
//     20211 уровень до = +24 · 20212 уровень после = (режим 1 ? +24 : 0)
//     20214 улучшений до = +25 · 20215 после = 0 ВСЕГДА
//     20217 попыток до = «(+26 − +25)/+26» · 20218 после = режим 1 ? «(+26 − +24)/+26» : «+26/+26»
//
// ═════════════════ ЦЕНА И МАТЕРИАЛЫ ═══════════════════════════════════════════════════════════
//   `craftWnd_updateMaterialTotals@0xD2F920`, суммы по ВСЕЙ очереди целей:
//     золото  += `DT_pricetable[код].Price` × (`DT_graderate[ItemType].RatePercent` / 10), а если
//               `RatePercent == 0` — то просто `Price` (эталон явно проверяет ноль);
//     свитки  += `DT_pricetable[код].dw02`, в корзину ОРУЖИЯ при `ItemType == 9`, иначе БРОНИ.
//   Свитки в наличии (`uiInventoryCraftListRefresh@0xD2F360`) — сумма количеств по сумке
//   (контейнер 1, 30 ячеек) у предметов с `DT_special_itemlist.SubType`:
//     **210** = свиток для ОРУЖИЯ, **211** = свиток для БРОНИ (проверено по данным: коды
//     208…210 «Свиток ремонта оружия», 211…213 «Свиток ремонта брони»). Сервер требует ровно
//     эти подтипы (`Recv_User_ShellItemUpgradeReset`, ветки `!= 210` / `!= 211`).
//   ⚠ РАСХОЖДЕНИЕ КЛИЕНТ↔СЕРВЕР (оригинал): сервер множит на `RatePercent` БЕЗ проверки нуля,
//   поэтому для видов предметов с `RatePercent == 0` (5, 6, 7) он посчитает цену 0, а клиент
//   покажет полную `Price`. Наблюдаемо только если такие виды вообще попадут в список.
//
// ═════════════════ ЗВУКИ И ЭФФЕКТЫ ════════════════════════════════════════════════════════════
//   ★ ПРОВЕРЕННОЕ ОТРИЦАТЕЛЬНОЕ: своих звуков и эффектов у окна НЕТ. Прогон по телам всех 15
//   функций окна (`refineWnd_Open`, `craftWnd_processCraftRequest`, `clanWindowOnButtonEvent`,
//   `uiCraftListSelectItem`, `uiTradeItemSelectHandler`, `uiRefineMaterialSelectHandler`,
//   `uiItemInfoHandleOpcode4F6A`, `uiInventoryCraftListRefresh`, `CRefineWnd_refreshMaterialSlots`,
//   `craftWnd_updateMaterialTotals`, `ItemInfoWin_Update`, `uiInventoryShowItemTooltip`,
//   `itemInitWin_ShowSlotTooltip`, `set_o10A0`, `netSendPacket6506`) не дал ни одного вызова
//   звука, эффекта или нумпада. Единственный звук — ОБЩИЙ клик №5, который `CGameWindow_OnCommand
//   @0xD1B140` играет для любой кнопки любого окна (`csoundPlayName2DStore(g_pSoundMgr, 5, 0)`),
//   то есть к этому окну не относится и отдельно портировать нечего.
//   ★ НУМПАД НЕ ЗОВЁТСЯ (тем же прогоном: `uiOpenContextPopupWithHandlers` не встречается) —
//   количество здесь не вводится, цели набираются двойным щелчком. Запись — в `NUMPAD_FLOW.md`.
//   Контролы 20340…20344 (`Ani_ShellEffect`) в `.JUI` скрыты и их видимость НИКТО не меняет —
//   в оригинале это мёртвый декоративный задел, анимации успеха нет.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaBagListScroll.h"   // ★ 29.10.2026 (инвентарь ×2): FAsdaBagListScroll — окно 30 позиций над списком до 60
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaShopDblClick.h"   // ★ U1 (В2-A дедупа №2): FAsdaDblClick — двойной щелчок по порогу Windows
#include "AsdaItemResetSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
struct FAsdaItemInstance;

// ── Нотисы окна. Все id проверены по `GameText.csv`; ничего не выдумано. ──
namespace AsdaItemResetLoc
{
	// Заголовок 20202 по режиму.
	constexpr int32 kTitleFullReset   = 50030;  // «Полностью сбросить улучшения»   (режим 0)
	constexpr int32 kTitleRestore     = 50039;  // «Восстановить улучшения»          (режим 1)
	// Подписи блока свитков (ставятся при открытии в ОБОИХ режимах).
	constexpr int32 kNeedWeaponScroll = 50300;  // «Необходимо свитков оружия»
	constexpr int32 kNeedArmorScroll  = 50301;  // «Необходимо свитков брони»
	constexpr int32 kHaveWeaponScroll = 50302;  // «Свитков оружия в наличии:»
	constexpr int32 kHaveArmorScroll  = 50303;  // «Свитков брони в наличии:»
	// Статичные подписи блока «до → после» и стоимости (`CItemInitializeWin_InitControls`).
	constexpr int32 kLabelGrade       = 50031;  // «Уровень»
	constexpr int32 kLabelPower       = 50032;  // «Количество улучшений»
	constexpr int32 kLabelChance      = 50033;  // «Попыток»
	constexpr int32 kLabelTargetTitle = 50034;  // «Вернуть в исходное состояние» (капшн 20320)
	constexpr int32 kBtnOkCaption     = 50035;  // «OK»  (капшн 20350)
	constexpr int32 kLabelFee         = 50036;  // «Стоимость обнуления»
	constexpr int32 kLabelGold        = 50037;  // «Золото:»
	constexpr int32 kBtnCancelCaption = 50038;  // «Отмена» (капшн 20370)
	constexpr int32 kGuideDefault     = 50056;  // «дважды щелкни, чтобы подготовить предмет…»
	// Подсказка 20230 при открытии — по режиму.
	constexpr int32 kGuideOpenFull    = 98500;  // «Выбери предмет, который хочешь вернуть…»
	constexpr int32 kGuideOpenRestore = 98502;  // «Выбери предметы, чтобы восстановить попытки…»
	// Подсказка 20230 при выборе/добавлении предмета.
	constexpr int32 kGuideAddedFull   = 98501;  // «Выбери предметы и нажми кнопку 'Сброс'» (режим 0)
	constexpr int32 kGuideAddedRestore= 98503;  // то же (режим 1)
	constexpr int32 kGuideAtMax       = 98504;  // «Сбрасывать улучшения не нужно — достиг максимума»
	constexpr int32 kGuideNothingLost = 98505;  // «Не нужно ничего восстанавливать…»
	constexpr int32 kGuideNotUpgrade  = 98506;  // «Этот предмет не подлежит улучшению.»
	constexpr int32 kGuideListFull    = 98507;  // «Не удается добавить в список»
	constexpr int32 kNoticeNoTarget   = 98508;  // «Нет предмета, с которого можно снять улучшения»
	constexpr int32 kGuideReadyPart   = 98510;  // «Предмет готов к частичному сбросу улучшений»
	constexpr int32 kGuideReadyFull   = 98511;  // «Предмет готов к полному сбросу улучшений»
	// Модалка подтверждения (класс 54 → `case 0x36` в `CItem_BuildTooltipText@0xD6E790`).
	constexpr int32 kConfirmText      = 50200;  // «Все указанные предметы вернутся в исходное…»
	constexpr int32 kConfirmOk        = 50201;  // «ОК»
	constexpr int32 kConfirmCancel    = 50202;  // «Отмена»
	// Ответ сервера 6507 → нотис (модалка + строка в чат, цвет −11514, время 10000).
	constexpr int32 kResultOk         = 50057;  // код 1 — «предмет возвращен в исходное состояние»
	constexpr int32 kResultNotOwn     = 50052;  // код 2 — сервер этим опкодом не шлёт
	constexpr int32 kResultBadLevel   = 50053;  // код 3 — сервер этим опкодом не шлёт
	constexpr int32 kResultNoGold     = 50054;  // код 4 — «Недостаточно золота.»
	constexpr int32 kResultNoScroll   = 50055;  // код 5 — «Нет купона.»
	constexpr int32 kResultCantReset  = 50058;  // код 6 — «не удается обнулить улучшения предмета.»
	// Код 0 — ОБЩИЙ ОТКАЗ, и у эталона на него нотиса НЕТ (ветка `default: break;`).
}

// Одна позиция списка кандидатов или очереди целей.
USTRUCT()
struct FAsdaItemResetEntry
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode  = 0;    // 0 ⇒ позиция пуста
	UPROPERTY(Transient) int32 Container = 1;    // поле записи +4 (эталон всегда шлёт 1)
	UPROPERTY(Transient) int32 Slot      = -1;   // поле записи +5 — ИМЕННО ЭТО едет в пакет
	UPROPERTY(Transient) int32 CellIndex = -1;   // номер ячейки контейнера (для тултипа)
	UPROPERTY(Transient) int32 Level     = 0;    // запись +24
	UPROPERTY(Transient) int32 UsedTries = 0;    // запись +25
	UPROPERTY(Transient) int32 MaxTries  = 0;    // запись +26

	bool IsEmpty() const { return ItemCode <= 0; }
	void Clear() { *this = FAsdaItemResetEntry(); }
	// Ключ сравнения «тот же предмет»: эталон сравнивает поле узла +4, которое для узла сумки
	//   есть номер ячейки. Пара (контейнер, ячейка) однозначна и не зависит от уплотнения списка.
	bool SameAs(const FAsdaItemResetEntry& O) const
	{ return Container == O.Container && CellIndex == O.CellIndex; }
};

UCLASS()
class ASDAREBORN_API UAsdaItemResetSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaItemResetSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (все из `.JUI` и `CItemInitializeWin_InitControls@0xD2DCA0`) ──
	static constexpr int32 kListCells   = 30;   // 20240…20269 (+ маски 20280…20309), сетка 6×5
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): предел перебора сумки и длина списка кандидатов `List` — вся сумка
	//   (= `UAsdaInventorySubsystem::kShellSlots`, static_assert в .cpp); сетка показывает окно из 30 (`BagScroll`).
	static constexpr int32 kListMax     = 60;
	static constexpr int32 kTargetCells = 5;    // 20330…20334
	static constexpr int32 kCellPx      = 32;   // Size = "32 32" у обеих сеток
	static constexpr int32 kResetZ      = 731;  // как у окна ремонта — выше инвентаря (720)
	static constexpr int32 kBagContainer= 1;    // `g_InventoryBag` — сумка снаряжения

	// Режимы `refineWnd_Open(this, a2)`.
	static constexpr int32 kModeFullReset = 0;  // 30046 — сброс за золото
	static constexpr int32 kModeRestore   = 1;  // 30047 — возврат попыток за свитки

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	// `refineWnd_Open@0xD30170`. Mode: 0 = полный сброс, 1 = восстановление попыток.
	UFUNCTION(BlueprintCallable, Category="Asda|ItemReset")
	void OpenWindow(int32 InMode);

	UFUNCTION(BlueprintCallable, Category="Asda|ItemReset")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category="Asda|ItemReset")
	bool IsOpen() const { return bOpen; }

	UFUNCTION(BlueprintPure, Category="Asda|ItemReset")
	int32 GetMode() const { return Mode; }

	// ═══ ДЕЙСТВИЯ (в эталоне — обработчики контролов) ═══
	void OnListCellClicked(int32 Cell, bool bDouble);    // 20240…20269: одиночный/двойной щелчок
	void OnTargetCellClicked(int32 Cell, bool bDouble);  // 20330…20334: одиночный/двойной щелчок
	void OnOkClicked();                                  // 20350 `Item_Initializ_Btn`
	void OnCancelClicked();                              // 20370 / 20201 — оба на `sub_93C0A5`

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // список, очередь, суммы, свитки, режим, флаги насоса
	// ★ 25.10.2026 (сброс у кузнеца): тексты инфо-панели 20211/20212/20214/20215/20217/20218 одной строкой
	//   «ур=+A→+B исп=C→D поп=E→F» — для самотеста `asda.SelfTest.ItemReset` (окно закрыто ⇒ пустая строка).
	FString DebugInfoPanel() const;

private:
	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();
	// Собрать ВСЕ виджеты, принадлежащие одному контролу `.JUI` (сам `TextBlock`/`Button`,
	//   его планы `_<seq>_<plane>`, группу `_grp`, капшн `_Text`, хит-зону `_HIT`).
	//   Гасить/показывать надо ВСЁ поддерево по префиксу — раскладка однотипных узлов
	//   у импортёра НЕОДНОРОДНА (закон из разбора ячеек JUI).
	void CollectByIdc(const FString& Idc, TArray<TObjectPtr<UWidget>>& Out) const;
	static void SetGroupVisible(const TArray<TObjectPtr<UWidget>>& Group, bool bVisible);

	// ── Наполнение и рендер ──
	void RebuildList();          // `uiInventoryCraftListRefresh@0xD2F360`
	void RebuildTargets();       // `CRefineWnd_refreshMaterialSlots@0xD2FCA0` (миснейм — это ЦЕЛИ)
	void UpdateTotals();         // `craftWnd_updateMaterialTotals@0xD2F920`
	void UpdateInfoPanel();      // `ItemInfoWin_Update@0xD31B60`
	void ApplyModeVisibility();  // переключение блоков 20390/20391
	void SetGuide(int32 LocId);  // текст 20230

	// ── Цена ──
	int32 GoldCostOf(int32 ItemCode) const;      // Price × RatePercent/10 (0 ⇒ просто Price)
	int32 ScrollCostOf(int32 ItemCode) const;    // `DT_pricetable.dw02`
	// Подтип свитка предмета из `DT_special_itemlist`: 210 оружие, 211 броня, иначе 0.
	int32 ScrollSubTypeOf(int32 ItemCode) const;
	// Собрать слоты свитков нужного вида на требуемое количество —
	//   порт `inventoryCollectItemsByType@0xD30530`.
	void CollectScrollSlots(bool bWeapon, int32 NeedCount, TArray<int32>& OutSlots) const;

	// ── Подтверждение, отправка, приём ──
	void AskConfirm();                              // 20350 → модалка класса 54
	void PumpSendQueue();                           // `craftWnd_processCraftRequest@0xD30810`
	void OnResetResult(const TArray<uint8>& Body);  // `uiInventoryOnRecvSlotUpdate@0xD31430`
	// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 — ветка `case 1` эталонного приёмника 6507, которой у порта не было:
	//   золото @3 (`uiSetHonorPointsDisplay`), вес @1 (`HUD_UpdateMiniWeightBar`), новая запись ЦЕЛИ @7 (57 б:
	//   `memset(g_InventoryBag + 57*слот)` + `Inventory_SetBagSlot`, кладёт при коде > 0) и 10 записей
	//   МАТЕРИАЛОВ @64+9i (u8 контейнер · i16 слот · i32 количество · i16 вес стека: количество ≠ 0 ⇒ +9/+14
	//   ячейки, иначе ячейка очищается). Без этого предмет оставался улучшенным до перезахода (жалоба игроков).
	//   Ключ `asda.ItemReset.ApplyResult` (0 — прежнее поведение: только нотис). Возвращает число изменённых ячеек.
	int32 ApplyResultToInventory(const TArray<uint8>& Body);
	void DrainPendingPackets();

	// ── Покадровые опросы ──
	void TickCells();   // наведение/клик по ОБЕИМ сеткам ПО ЖИВОЙ ГЕОМЕТРИИ

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	// Эталон на результат 6507 шлёт нотис ДВАЖДЫ: модалка (`CItem_BuildTooltipText(…,0,−1,…)`)
	//   И строка в чат (`CChat_AddTokenizedLines(…, −11514, 10000, 0)`). Повторяем оба канала.
	void NoticeBoth(int32 LocId) const;

	// ═══ СОСТОЯНИЕ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     ListIcons;    // 30 планов 20240…20269
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ListLabels;   // 30 подписей «остаток/макс»
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     TargetIcons;  // 5 планов 20330…20334
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TargetLabels; // 5 подписей
	// Маски 20280…20309: каждая — ЦЕЛОЕ поддерево (`TextBlock` + планы).
	TArray<TArray<TObjectPtr<UWidget>>> DisableGroups;                // 30
	// Блоки-режимы 20390 / 20391 со всеми потомками.
	TArray<TObjectPtr<UWidget>> GroupFullReset;                       // All_Reset   (20390)
	TArray<TObjectPtr<UWidget>> GroupRestore;                         // OneTree_Reset (20391)

	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose   = nullptr;  // 20201
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOk      = nullptr;  // 20350
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel  = nullptr;  // 20370
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle   = nullptr;  // 20202
	UPROPERTY(Transient) TObjectPtr<UImage>     ImgSelItem = nullptr;  // 20203
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSelName = nullptr;  // 20204
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGradeLbl= nullptr;  // 20210 (50031)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGradeBef= nullptr;  // 20211
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGradeAft= nullptr;  // 20212
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPowerLbl= nullptr;  // 20213 (50032)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPowerBef= nullptr;  // 20214
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPowerAft= nullptr;  // 20215
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtChanceLbl=nullptr;  // 20216 (50033)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtChanceBef=nullptr;  // 20217
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtChanceAft=nullptr;  // 20218
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGuide   = nullptr;  // 20230
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTargetTitle=nullptr;// 20320 (капшн `_Text`)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFeeLbl  = nullptr;  // 20360 (50036)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFee     = nullptr;  // 20361
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGoldLbl = nullptr;  // 20362 (50037)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGold    = nullptr;  // 20363
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNeedWpnLbl=nullptr; // 20364 (50300)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNeedWpn = nullptr;  // 20365
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHaveWpnLbl=nullptr; // 20366 (50302)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHaveWpn = nullptr;  // 20367
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNeedArmLbl=nullptr; // 20371 (50301)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtNeedArm = nullptr;  // 20372
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHaveArmLbl=nullptr; // 20373 (50303)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHaveArm = nullptr;  // 20374
	UPROPERTY(Transient) TObjectPtr<UWidget>    ListSelFrame  = nullptr; // 20220 `Slot_AniSelect`
	UPROPERTY(Transient) TObjectPtr<UWidget>    TargetSelFrame= nullptr; // 20349 `Slot_AniSelect`

	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridList   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridTarget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridSel    = nullptr;  // одна ячейка 20203
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Модель ──
	int32 Mode = kModeFullReset;    // байт записи окна +4213
	bool bOpen = false;
	bool bControlsReady = false;
	bool bDirty = false;            // инвентарь изменился — пересобрать в Tick
	bool bConfirmOpen = false;      // модалка 54 открыта — второй не открываем
	// ★ Насос эталона: `bArmed` = байт +4256 («подтверждено, гоним очередь»),
	//   `bReady` = байт +4257 («можно слать следующий»). `refineWnd_Open` ставит bReady = 1,
	//   ответ 6507 ставит его снова — так очередь идёт ПО ОДНОМУ пакету на ответ.
	bool bArmed = false;
	bool bReady = true;

	TArray<FAsdaItemResetEntry> List;      // до 60 кандидатов (this+1043; ★ 29.10: вся сумка, `kListMax`)
	FAsdaBagListScroll BagScroll;          // ★ 29.10.2026: окно сетки 30 над `List` (ряд прокрутки колесом)
	TArray<FAsdaItemResetEntry> Targets;   // до 5 целей       (this+1046)
	FAsdaItemResetEntry Selected;          // подсвеченная позиция (this+1049)
	bool bSelectedValid = false;

	int32 TotalGold     = 0;   // this+1052
	int32 NeedWeapon    = 0;   // this+1055
	int32 NeedArmor     = 0;   // this+1057
	int32 HaveWeapon    = 0;   // this+1054
	int32 HaveArmor     = 0;   // this+1056

	int32 TipListCell   = -1;  // ячейка списка под курсором (−1 — нет)
	int32 TipTargetCell = -1;
	// Ручное распознавание двойного щелчка: `FAsdaMouseFrame` фронтов двойного клика не даёт,
	//   а оригинал различает одиночный (msgType 0xB) и двойной (0xD) по одним и тем же ячейкам.
	// Двойной щелчок по ячейке (A = ячейка, B = «цель/список»). ★ U1 (В2-A): общий `FAsdaDblClick`, порог Windows (было
	//   `kDoubleClickSec` 0,35 с; прежний — `LegacySec`, ключ `asda.UI.DblClickLegacy 1`).
	FAsdaDblClick CellDbl{ -1, -1, -1.0, 0.35 };
public:
	// ★ U1 (В2-A дедупа №2): КОПИЯ распознавателя двойного щелчка с прежним порогом окна (`LegacySec`) — для таблицы
	//   порогов самотеста `asda.Shop.SelfTestDblClick` (раздел U1). Своё состояние пары окна не трогает.
	FAsdaDblClick DebugDblClickProbe() const;
private:

	FAsdaMouseFrame Mouse;
	TAsdaNetQueue<TArray<uint8>> PendingResults;   // закон `no-reentrant-net-callbacks`
	FDelegateHandle HResetResult;
	FDelegateHandle HInvCell;
};
