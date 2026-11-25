// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaUpgradeSplitSubsystem — ОКНО «РАЗДЕЛЕНИЕ УЛУЧШЕНИЯ» `Strength_Part_Win` (JUI 50500). 31.10.2026.
//   Улучшение вещи (в оригинале +88 % и выше; с 01.11 [ПОРТ] — любое, `asda.UpgradeSplit.MinPercent`) переносится в «готовый» камень улучшения (401 / 363), который потом камнем
//   переноса (`UAsdaUpgradeStoneSubsystem`, 6551) кладётся на другую вещь. Расход — пустые камни 205 (оружие) / 203
//   (доспех) по одному на уровень улучшения (+24).
//
// ═════════════════ ★★★ ВХОД В ОКНО — [ПОРТ] (решение владельца 31.10.2026) ════════════════════════════════════
//   В оригинале окно НЕДОСТИЖИМО: объект `g_pItemUpgradeWin` (0x2BDD148) ни одна функция не показывает (все 15
//   ссылок бинаря — сеть, события окна, сумка, init), `push 50500` во всех донорских exe — только в init окна
//   (`clanWindowInitControls@0xB64A40`), в JUI у 50500 `Visible = 0`. Владелец: «Двойной щелчок по камню. Так как
//   эталон никогда не использовал — нужно имплементировать всю систему». Вход: двойной щелчок по ПУСТОМУ камню
//   (special_itemlist SubType 203/205) в сумке. Ключ `asda.UpgradeSplit.Enable 0` — окна нет (как в оригинале).
//
// ═════════════════ ПОЛЯ ЭТАЛОНА (CClanWnd, dword-индексы) → ПОРТ ═══════════════════════════════════════════════
//   [0] 50500 окно · [1] 50501 заголовок 108500 · [2] 50589 крестик · [3] 50510 «Предмет разделения» 108501 ·
//   [4] 50516 иконка предмета · [5] 50511 имя · [6]/[7] 50512/50514 подписи 108502/108503 · [8] 50513 «+N%» ·
//   [9] 50515 «N(%)» · [10] 50520 108504 · [11] 50526 иконка пустого камня · [12] 50521 108520 «есть %d / нужно %d» ·
//   [14]/[17] 50522/50524 108521/108522 · [15]/[18] 50523/50525 число пустых камней оружия/доспеха ·
//   [19] 50530 108505 · [20] 50536 иконка готового камня · [21]/[22] 50532/50534 · [23] 50533 · [24] 50535 ·
//   [25] 50541 предупреждение 108509 · [26] 50540 «Рюкзак» 108506 · [27+2i] ячейки 50551…50580, [28+2i] их записи ·
//   [87] 50581 рамка выбора · [88] 50582 «Разделение» 108507 · [89] 50583 «Отмена» 108508 · [90…93] 50584…50587
//   надписи исхода · [94] выбранная вещь → `SelCell` · [95…102] пустые камни → `StoneCells` · [103] готовый камень →
//   `ResultCell` · [104]/[105] число пустых камней оружия/доспеха · [106] уровень выбранной вещи.
//
// ═════════════════ ПОТОК (эталон, все ветки) ══════════════════════════════════════════════════════════════════
//   Сетка (`invGridRefreshEnchanted@0xB68140`): вещи сумки с записью item_list, подтип не 66/200/201, уровень +24 > 0
//   и +27 ≥ 88 — подряд; «+N%» под иконкой; рамка 50581 — у выбранной. Двойной щелчок по ячейке (msg 0xD →
//   `uiDispatchMsgByIdTable@0xB676E0`): сброс выбора, камней, готового камня и надписей исхода
//   (`uiItemSlots_ResetState@0xB68540`), выбор (`sub_B67760`), перерисовка. Наведение (msg 8) — подсказка
//   (`itemTooltip_ShowForControlId@0xB68690`: 50516 — вещь, 50526 — первый камень, 50536 — готовый, ячейки).
//   «Разделение» 50582 (66561): нет свободной ячейки сумки ⇒ ONEBUT 108518; нет выбора ⇒ ничего; пересчёт пустых
//   камней (`invCountColorMixMaterials@0xB65400`) и набор камней до уровня (`inventoryCountAmmoForBudget@0xB651D0`:
//   до 8 стопок, 205 для оружия / 203 для прочего); хватает ⇒ TWOBUT (1, 68) 108509 с кнопками 108510/108511,
//   нет ⇒ ONEBUT 108517 «не хватает %d». «ОК» вопроса (ui_core.c:57094) ⇒ 6549. 50583 / 50589 / Esc ⇒ скрыть окно.
//   6550 (`netHandleItemOpResultPacket@0xB66C20`): 1 — надпись исхода 50584+исход, нотис 109904/109905 (исход 0/1)
//   или 109906/109907 (2/3) с числом камней (оружие/доспех — по `itemType` выбранной вещи), вес, 10 записей,
//   первая в сумке — готовый камень `[103]`, перерисовка и пересчёт; 0/2/3/4/5/98/99 — ONEBUT 108600/108602…108607.
//   Звуков у окна нет (кроме общих звуков окон-сообщений); нумпада нет.
//
// ═════════════════ ОТСТУПЛЕНИЯ ═══════════════════════════════════════════════════════════════════════════════
//   [ПОРТ] Вход (выше). Открытие = сброс выбора/камней/надписей + пересчёт пустых камней + перерисовка + показ поверх.
//   [ПОРТ] Сетка листает сумку ×2 (60) колесом по рядам 10×3 (у `FAsdaBagListScroll` сетка 6×5 — не подходит).
//   [ИСПРАВЛЕНИЕ] Число пустых камней пересчитывается при любом изменении сумки (у эталона — только кнопкой и 6550,
//     поэтому оно бывало устаревшим). Ключ `asda.UpgradeSplit.LiveCounts 0` — как эталон.
//   [ИСПРАВЛЕНИЕ] Код 6 сервера («нет места в сумке», `Recv_UserUpgradeExtraction(10)`) эталон молча глотал —
//     показываем 108518. Ключ `asda.UpgradeSplit.Code6Notice 0` — молча.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "UI/AsdaShopDblClick.h"    // FAsdaDblClick — двойной щелчок по порогу Windows
#include "AsdaNetQueue.h"           // TAsdaNetQueue
#include "AsdaSessionReset.h"       // IAsdaSessionResettable
#include "AsdaUpgradeSplitSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;

UCLASS()
class ASDAREBORN_API UAsdaUpgradeSplitSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписки (6550, ESC, «закрыть все», колесо, изменения сумки).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписки, снятие виджета.
	virtual void Deinitialize() override;
	// Окно и выбор принадлежат сессии.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Очередь 6550, мышь, подсказки, перерисовка по изменению сумки.
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaUpgradeSplitSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }

	// ── Числа окна (`.JUI` 50500 и `clanWindowInitControls@0xB64A40`) ──
	static constexpr int32 kCells    = 30;    // 50551…50580 — видимые ячейки
	static constexpr int32 kCols     = 10;    // сетка 10 × 3 (шаг 33 по `.JUI`)
	static constexpr int32 kRows     = 3;
	static constexpr int32 kListMax  = 60;    // вся сумка ×2
	static constexpr int32 kStonesMax = 8;    // `this+95…102`
	static constexpr int32 kEffects  = 4;     // 50584…50587
	static constexpr int32 kMinPercent = 88;  // порог сетки оригинала: +27 ≥ 88 (в порту — ключ asda.UpgradeSplit.MinPercent, умолчание 0)
	static constexpr int32 kWindowZ  = 735;   // над окном разборки (734)

	// Пустой камень улучшения: 205 — оружие, 203 — доспех.
	static bool IsEmptyStoneSubType(int32 SubType) { return SubType == 203 || SubType == 205; }

	// ★ [ПОРТ] Вход: двойной щелчок по ячейке СУМКИ. true — щелчок поглощён (открыто окно).
	bool OnBagDoubleClick(int32 CellIndex);

	// Открыть окно (поверх, с чистым выбором) / скрыть (`sub_932F64` = `sub_B68660`).
	void OpenWindow();    // открыть поверх, с чистым выбором и свежими счётчиками ([ПОРТ])
	void CloseWindow();   // скрыть окно (`sub_B68660`: только `SetVisible(0)`)
	// Окно показано.
	bool IsOpen() const { return bOpen; }

	// ── Действия (обработчики контролов эталона; публичны для самотеста) ──
	void OnCellDoubleClick(int32 VisibleCell);   // msg 0xD по 50551+i
	void OnSplitClicked();                       // 66561 по 50582
	void OnCancelClicked();                      // 66561 по 50583 / 50589, msg 21
	void SendRequest();                          // «ОК» вопроса (1, 68)

	// ── Диагностика и самотест ──
	int32 GetSelectedCell() const { return SelCell; }        // ячейка сумки выбранной вещи (−1 — нет)
	int32 GetResultCell() const { return ResultCell; }       // ячейка сумки готового камня (−1 — нет)
	const TArray<int32>& GetStoneCells() const { return StoneCells; }   // набранные пустые камни
	int32 GetListNum() const { return List.Num(); }           // вещей в сетке
	int32 GetListCell(int32 ListIndex) const { return List.IsValidIndex(ListIndex) ? List[ListIndex] : -1; }   // ячейка сумки позиции сетки
	int32 GetEmptyCount(bool bWeapon) const { return bWeapon ? WeaponEmpty : ArmorEmpty; }   // единиц пустых камней 205 / 203
	bool IsEffectShown(int32 K) const;                        // надпись исхода 50584+K видна
	FString GetControlText(int32 IdNum) const;                // текст контрола по idnum (50501…50583)
	UUserWidget* GetWidget() const { return Widget; }   // виджет окна (самотест жмёт кнопки)
	FString DumpState() const;      // состояние строкой (консоль `asda.UpgradeSplit.Dump`)
	FString DumpControls() const;   // разобранные контролы (закон `full-structural-analysis-always`)
	const TArray<FString>& GetShownNotices() const { return ShownNotices; }   // тексты ONEBUT по порядку (самотест)
	void ClearShownNotices() { ShownNotices.Reset(); }                       // забыть их

private:
	// ── Окно ──
	bool EnsureWidget();    // каркас `FAsdaJuiWindowHandle::Ensure`: открыть WBP, разобрать, зона по рамке
	void ResetWidget();     // снять виджет и все ссылки на узлы
	void SetupControls();   // разбор ВСЕХ контролов, иконки-сетки, кнопки, статичные подписи (`clanWindowInitControls`)

	// ── Модель (эталонные функции) ──
	void ResetSlots();                       // `uiItemSlots_ResetState@0xB68540`
	void RefreshAll();                       // `itemInfoPanel_RefreshAll@0xB677B0`
	void RebuildGrid();                      // `invGridRefreshEnchanted@0xB68140`
	void RefreshMainItem();                  // `ItemInfoPanel_UpdateMainItem@0xB67810`
	void RefreshStonePanel();                // `uiItemInfoRefresh@0xB67C10`
	void RefreshResult();                    // `ItemInfoPanel_UpdateTable2Item@0xB67E90`
	void CountMaterials();                   // `invCountColorMixMaterials@0xB65400` (без перерисовки)
	int32 CollectStones(int32 Budget);       // `inventoryCountAmmoForBudget@0xB651D0`
	bool HasFreeBagCell() const;             // `inventoryBag_hasFreeSlot@0xB689F0`
	int32 SelectedItemType() const;          // `itemType` выбранной вещи (−1 — нет)

	// ── Мышь ──
	void TickMouse();                        // наведение (msg 8/0xA) и двойной щелчок по ячейке (msg 0xD)
	int32 HoverIdNum() const;                // idnum контрола под курсором (−1 — ничего)
	void TickTooltip(int32 IdNum);           // подсказка контрола (`itemTooltip_ShowForControlId@0xB68690`)
	void PlaceSelFrame();                    // рамка 50581 у ячейки выбранной вещи

	// ── Сеть ──
	void OnResult(const TArray<uint8>& Body577);   // разбор 6550 (`netHandleItemOpResultPacket@0xB66C20`)

	// ── Мелочи ──
	void MsgBox(const FString& Text) const;              // ONEBUT 1500 (`CItem_BuildTooltipText(ui, 0, −1, …)`) + запись для самотеста
	FString Loc(int32 Id) const;                         // loc-строка языка сессии
	void SetText(UTextBlock* Tb, const FString& S) const;   // текст подписи (nullptr — ничего)
	void SetPercentLabel(UTextBlock* Tb, const struct FAsdaItemInstance* It, const FSlateColor& Base) const;   // «+N%» под иконкой
	const struct FAsdaItemInstance* Bag(int32 Cell) const;   // ячейка сумки (контейнер 1) или nullptr
	int32 MaxRow() const { return FMath::Max(0, FMath::DivideAndRoundUp(List.Num(), kCols) - kRows); }   // последний ряд прокрутки сетки

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> Grid = nullptr;        // 50551…50580
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridItem = nullptr;    // 50516
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridStone = nullptr;   // 50526
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridResult = nullptr;  // 50536
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CellIcons;          // планы иконок 50551…50580
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CellLabels;     // подписи «+N%» ячеек
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UTextBlock>> Texts;           // idnum → подпись
	UPROPERTY(Transient) TObjectPtr<UImage> IconItem = nullptr;     // план 50516
	UPROPERTY(Transient) TObjectPtr<UImage> IconStone = nullptr;    // план 50526
	UPROPERTY(Transient) TObjectPtr<UImage> IconResult = nullptr;   // план 50536
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> SelFrame = nullptr;   // 50581
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Effects;                 // 50584…50587
	UPROPERTY(Transient) TObjectPtr<UButton> BtnSplit = nullptr;              // 50582
	UPROPERTY(Transient) TObjectPtr<UButton> BtnCancel = nullptr;             // 50583
	UPROPERTY(Transient) TObjectPtr<UButton> BtnClose = nullptr;              // 50589
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;   // реле кликов кнопок
	FSlateColor CellBaseColor = FSlateColor(FLinearColor::White);             // авторский цвет подписей ячеек

	// ── Состояние ──
	TArray<int32> List;              // ячейки сумки, прошедшие гейт сетки, подряд
	int32 Row = 0;                   // первый видимый ряд сетки
	int32 SelCell = -1;              // `[94]`
	int32 SelLevel = 0;              // `[106]`
	TArray<int32> StoneCells;        // `[95…102]`
	int32 ResultCell = -1;           // `[103]`
	int32 WeaponEmpty = 0;           // `[104]`
	int32 ArmorEmpty = 0;            // `[105]`
	int32 ShownEffect = -1;          // какая надпись исхода видна (−1 — ни одна)
	bool bOpen = false;              // окно показано
	bool bControlsReady = false;     // контролы разобраны
	bool bDirty = false;             // сумка поменялась — перерисовать в Tick
	int32 TipId = -1;                // idnum контрола, чья подсказка показана
	FAsdaMouseFrame Mouse;           // кнопки мыши этого кадра
	FAsdaDblClick CellDbl;           // распознаватель двойного щелчка по ячейке
	mutable TArray<FString> ShownNotices;   // тексты ONEBUT по порядку (последние 16)

	// ── Подписки ──
	FDelegateHandle HResult;         // подписка на 6550
	FDelegateHandle HInvCell;        // подписка на изменения сумки
	TAsdaNetQueue<TArray<uint8>> PendingResults;   // тела 6550 до разбора в Tick
};
