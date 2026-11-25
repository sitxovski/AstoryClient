// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaHuntReportSubsystem — ★★★ ОКНО «ДОКЛАД ОБ ОХОТЕ»: `Hunt report_Win` (JUI IDNum **56450**), волна 2-Б (06.10.2026).
//   Открывается кнопкой диалога NPC **30068** «Доложить о результатах охоты» (Элла, NPC 107).
//   Полный реверс потока с адресами — `Reports/owner_1006/wave2B_hunt_donation.md` §2.
//   Структурный разбор контролов — `Docs/UI/Character/Hunt report_Win.md` (+ живой дамп `asda.HuntReport.Dump`).
//
// ═══════════════════ ★★★ ИСПРАВЛЕННЫЕ МИСНЕЙМЫ ЭТАЛОНА ════════════════════════════════════════════════════════
//   • Класс `CSowelWindow`, глобал `g_pSowelWindow`, функции `uiSowelWindow_*` — НЕ самоцветы: это окно 56450.
//   • `itemGrid_OnSlotContextMenu@0xED00D0` — не «контекст-меню»: ЛКМ (событие 0xB) по ячейке → нумпад режима 9.
//   • `inventoryUI_onItemMovePacket@0xED0820` — не «перемещение предмета»: приём 6151 (ответ на сдачу знаков).
//   • `net_sendBlobSyncOpcode6150@0xED0170` — отправка 6150 `OP_RECV_CLIENT_TITLE_ITEM_CHECK`.
//   • `uiUpgradeQtyDlgOk@0xDA67B0` — ОК нумпада режима 9 (не «апгрейд»).
//
// ═══════════════════ ЧТО ДЕЛАЕТ ОКНО ═══════════════════════════════════════════════════════════════════════════
//   Сдаёт «знаки охоты» (предметы `item_list.LinkCode == 208`, контейнер 2) в зачёт ЗВАНИЯ: у знака в `pricetable.dw05`
//   лежит id звания (`DT_title_list`). Сетка 30 ячеек 56461…56490 заполняется знаками ПОДРЯД из 60 ячеек контейнера 2;
//   ЛКМ по ячейке открывает нумпад (максимум — стопка), ОК делает знак ВЫБРАННЫМ: его иконка и количество — в слоте 56451,
//   имя звания — в 56452 (нотис 110214), анимированная рамка 56491 — на его ячейке. Кнопка 56493 шлёт 6150, ответ 6151.
//   56492 и крестик 56499 только прячут окно; выбор при этом НЕ сбрасывается (эталон). ESC окно НЕ закрывает — его нет
//   в эталонной таблице `kEscCloseWindows` (признак `CControl+0x228` оригиналом не ставится).
//
// ═══════════════════ ОТСТУПЛЕНИЯ / ИСПРАВЛЕНИЯ ДЕФЕКТОВ ОРИГИНАЛА [ПОРТ] ════════════════════════════════════════
//   Д1 (ключ `asda.HuntReport.LiveRefresh`, 1 — исправление): эталон перерисовывает сетку ТОЛЬКО при показе, клике, ОК
//       нумпада и 6151 — при изменении сумки извне (подбор, продажа, выброс) окно показывает протухшие иконки, а ячейки
//       смотрят в живые записи (клик по «знаку» открывал бы нумпад чужой стопки). Порт перерисовывает и по `OnCellChanged`.
//   Д2 (без ключа): эталон останавливает заполнение на 30-й ячейке ТОЛЬКО в ветке выбранного знака; при > 30 знаках он
//       писал бы за конец массива из 30 ячеек (порча полей окна). Порт ограничивает заполнение 30 ячейками всегда.
//   Протокол не меняется: тело 6150 и разбор 6151 — 1:1.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"      // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHuntReportSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaButtonRelay;
enum class EAsdaMsgCategory : uint8;   // `UI/AsdaMessageSubsystem.h` — категория (канал) нотиса

// Виджеты одной ячейки сетки знаков: план-иконка `Hunt_report_Mark_N_<jui>_0` + одноимённый `TextBlock` (число стопки).
USTRUCT()
struct FAsdaHuntCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;   // план иконки 32×32
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;   // число стопки (`CControl_SetText(ячейка, "%d")`)
};

UCLASS()
class ASDAREBORN_API UAsdaHuntReportSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable ── окно, выбор знака и очередь 6151 принадлежат сессии (эталон `sub_ECF8F0` при сбросе сцены).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaHuntReportSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (из `.JUI` и `uiSowelWindow_InitControls`, ни одно не выдумано) ──
	static constexpr int32 kCells        = 30;      // 56461…56490
	static constexpr int32 kFirstCellId  = 56461;   // IDNum первой ячейки
	static constexpr int32 kSpendCells   = 60;      // `g_InventoryArray2` — 60 записей контейнера 2 (★ U21 (В1 дедупа №2): было `kBagCells`)
	static constexpr int32 kBagContainer = 2;       // контейнер расходников (`g_InventoryArray2`)
	static constexpr int32 kLinkCodeMark = 208;     // `ItemRecord->linkCode == 208` — знак охоты
	static constexpr int32 kZ            = 731;     // выше инвентаря (720), как окно ремонта

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	// `uiSowelWindow_Show@0xECFFF0`: уже открыто — ничего; иначе показ + перерисовка.
	UFUNCTION(BlueprintCallable, Category="Asda|HuntReport")
	void OpenWindow();
	// `uiSowelWindowToggle@0xED0070` (56492 / 56499): скрыть окно, выбор знака не трогается.
	UFUNCTION(BlueprintCallable, Category="Asda|HuntReport")
	void CloseWindow();
	// Открыто ли окно (эталонный `filler[272]`).
	UFUNCTION(BlueprintPure, Category="Asda|HuntReport")
	bool IsOpen() const { return bOpen; }

	// ═══ ДЕЙСТВИЯ (в эталоне — обработчики контролов; консоль и самотест зовут ИХ ЖЕ) ═══
	void OnCellClicked(int32 Cell);       // `itemGrid_OnSlotContextMenu@0xED00D0` (ячейка 0…29)
	void OnNumPadAccepted(int32 Qty);     // `uiUpgradeQtyDlgOk@0xDA67B0` → `itemGridSetDataAndRefresh@0xED0E90`
	void OnSendClicked();                 // 56493 → `net_sendBlobSyncOpcode6150@0xED0170`
	// Разобрать очередь 6151 немедленно (самотест; в игре это делает Tick).
	void PumpPending() { DrainPendingPackets(); }

	// ═══ ДИАГНОСТИКА / САМОТЕСТ ═══
	FString DumpControls() const;                          // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;                             // сетка, выбор, количество, отправленная копия
	UUserWidget* DebugWidget() const { return Widget; }    // корень окна — для кликов по живой геометрии
	int32 DebugCellCode(int32 Cell) const;                 // код знака в ячейке (0 — пусто)
	FString DebugCellText(int32 Cell) const;               // число стопки в ячейке
	int32 DebugSelectedBagCell() const { return SelBagCell; }   // выбранная ячейка контейнера 2 (−1 — нет)
	int32 DebugSelectedQty() const { return SelQty; }      // выбранное количество (`filler[344]`)
	FString DebugResultText() const;                       // 56452 (звание выбранного)
	FString DebugResultQtyText() const;                    // число в слоте 56451
	bool DebugSelectFrameShown() const;                    // рамка 56491 видна
	int32 DebugTipCell() const { return TipCell; }         // ячейка под тултипом (−1 — нет; 30 — слот 56451)

private:
	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();

	// ── Рендер ──
	// `itemGrid_RefreshSlots@0xECF930` — полная перерисовка (сетка, выбранный знак, звание, рамка).
	void Refresh();
	void HideSelectFrame();
	void MoveSelectFrame(const UWidget* OverIcon);
	// Строка звания знака (`CItemManager_GetTable2Record_0` → `dw05` → `dataRecLookupById` → `+16`). false — нет.
	bool TitleNameOf(int32 ItemCode, FString& OutName) const;

	// ── Сеть ──
	void DrainPendingPackets();
	void OnResult6151(const TArray<uint8>& Body);   // `inventoryUI_onItemMovePacket@0xED0820`

	// ── Покадровые опросы ──
	void TickCells();   // наведение (8) / уход (0xA) / ЛКМ (0xB) по ЖИВОЙ геометрии

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	void MsgBoxText(const FString& Text, EAsdaMsgCategory Cat) const;   // ONEBUT_MSGBOX 1500 (`CItem_BuildTooltipText(…,0,−1,…)`)

	// ═══ ВИДЖЕТЫ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaHuntCellUi> CellUi;                   // 30 ячеек 56461…56490
	UPROPERTY(Transient) TObjectPtr<UImage>      ResultIcon   = nullptr;   // 56451 `Hunt_report_Mark_0` (64×64)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  ResultQty    = nullptr;   // 56451 текст (выбранное количество)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  ResultText   = nullptr;   // 56452 `Hunt_report_Text1`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  Caption      = nullptr;   // 56494 `Hunt_report_Text_Move`
	UPROPERTY(Transient) TObjectPtr<UWidget>     SelectFrame  = nullptr;   // 56491 `Hunt_report_Select` (Slot_AniSelect)
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnOk        = nullptr;   // 56493 `Hunt_report_Ok`
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnNo        = nullptr;   // 56492 `Hunt_report_No`
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnClose     = nullptr;   // 56499 `Hunt_report_X`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  BtnOkText    = nullptr;   // подпись 56493 (110211)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  BtnNoText    = nullptr;   // подпись 56492 (110212)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;      // реле кнопок

	// ═══ МОДЕЛЬ (поля эталона) ═══
	bool bOpen = false;              // `filler[272]`
	bool bControlsReady = false;     // `filler[348]`
	bool bDirty = false;             // [ПОРТ] Д1: сумка изменилась — перерисовать в Tick
	int32 CellBag[kCells];           // `filler[8i+24]`: ячейка контейнера 2 знака в ячейке окна i (−1 — пусто)
	int32 PendingBagCell = -1;       // `filler[280]`: ячейка, по которой кликнули (ждёт ОК нумпада)
	int32 SelBagCell = -1;           // `filler[276]`: ВЫБРАННЫЙ знак (ячейка контейнера 2, сравнение по слоту)
	int32 SelQty = 0;                // `filler[344]`: выбранное количество
	int32 SentItemCode = 0;          // `filler[284]` (+0): код из копии записи, отправленной в 6150
	int32 TipCell = -1;              // ячейка под тултипом (30 — слот 56451)

	FAsdaMouseFrame Mouse;           // покадровый опрос мыши
	TAsdaNetQueue<TArray<uint8>> PendingResults;   // очередь 6151 (закон `no-reentrant-net-callbacks`)
	FDelegateHandle HResult;         // подписка на 6151
	FDelegateHandle HInvCell;        // подписка на изменение сумки (Д1)
};
