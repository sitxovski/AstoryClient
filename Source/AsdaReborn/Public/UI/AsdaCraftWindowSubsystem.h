// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCraftWindowSubsystem — ОКНО «СОЗДАНИЕ ПРЕДМЕТА»: список изученных рецептов `ITEM_making_WIN` (JUI 76000).
//   Порт 25.09.2026, волна В2 плана `Reports/craft/craft_re_report.md` §12 (+ ревизия `craft_re_REVIEW.md`).
//   Модель (уровень/опыт/лимит/изученные) и протокол — `UAsdaCraftSubsystem`; здесь только окно и входы.
//   Отчёт захода — `Reports/craft/wave0-2.md`; структурный дамп ассета — `Reports/craft/_craft_wbp_tree_main.txt`.
//
// ═════════════════ ЭТАЛОН (класс `CItemMainWnd`, объект `g_pItemMainWnd`) ═══════════════════════════════
//   ctor `itemMainWnd_ctor@0xFDC220` (вкладка [4] = 4 «ВСЕ»), init `CItemMainWnd_InitControls@0xFDC6A0`
//   (item_inventory.c:69298), wndproc `shopGridWndProc@0xFDD720` (МИСНЕЙМ, shop_trade.c:22033),
//   открытие `tileInteractGateAndOpen@0xFDD1A0` (ui_core.c:100507, ТУМБЛЕР), сводка
//   `uiCraftPanelRefreshSummaryGated@0xFDE300`, строки `uiList_RefreshItemRows6@0xFDE700`, страница
//   `uiBuildFilteredItemPage@0xFE0A20`, закрытие `uiWindowHideAndCleanup@0xFDD590`, сброс выбора
//   `uiPanelHideAndReset@0xFDD020`, подсказка строки `uiCraft_ShowItemTooltip@0xFE0E50`.
//   Поля: [1] страниц · [2] страница (с 1) · [4] вкладка · [5] выбранная строка/−1 · [10..15] рецепты строк
//   текущей страницы · [16] видимый «over» · [20] рамка 76077 · [1383] число после фильтра · [1384..] список.
//
// ═════════════════ ВХОДЫ (все — один тумблер `GateAndToggle`) ══════════════════════════════════════════
//   кнопка инвентаря 452 · клавиша действия 41 (Z) · доп-меню команда 5 (и ячейка хотбара вида 5) ·
//   диалог NPC 30088 и 30009 (сначала закрыть, потом тумблер) — `GateAndToggleFromNpc`.
//   ESC окно НЕ закрывает (флага `CControl+0x228` в init нет — как в оригинале), закрыватель для
//   `uiCloseAllWindows` (`CloseAllPlayerWindows`) зарегистрирован. Своих звуков у окна нет (§6.3 отчёта).
//
// ═════════════════ ГЕЙТЫ ОТКРЫТИЯ (`tileInteractGateAndOpen`, порядок эталона) ══════════════════════════
//   1) `uiCheckPartyInviteInvLockGuard@0xFDD0F0`: ОКНО обмена 2008 открыто (`g_bPartyInviteFlag` = CExchange+0x2C;
//      ★ ПД-2 29.09.2026: не «заявка в полёте» — общая точка `UAsdaTradeStateSubsystem::IsExchangeWindowOpen`)
//      → чат 5590; ★ `g_bInventoryLocked` = `dword_2C0F2EC` = `g_pNpcWnd(0x2C0B2E0)+0x400C` = поле [4099]
//      «ИДЁТ РАЗГОВОР С NPC» (ставит `regionWindow_OpenAndPathMove@0xD99290`, снимает
//      `UI_resetAllWindowsAndLeave_5045` — `gap_75C[14512]`; тот же флаг NPC-порт уже проверяет как гейт 6
//      `NPC_OpenWindow` `cmp [this+0x400C],0`) → чат 5591 «Открыто окно диалога…». Отказ.
//   2) `objStateAllowsRender(g_LocalPlayer)` (миснейм: «персонаж занят») → молча.
//   3) окно видно → закрыть (`uiWindowHideAndCleanup`), конец.
//   4) молча: лавка заточки 17000 видна · разговор с NPC · элитный магазин (`g_pShopWnd`+2075) · лопата в руке ·
//      квест-обмен · окно продажи-лотка · `g_bSomeFeatureEnabled` · сцена гачи · элитный почтовый ящик ·
//      склад. Каких систем в порту нет — отмечено у самой проверки.
//   5) открыть: закрыть диалог NPC, остановить персонажа (4008), пересчитать лимит, страница 1, показать.
//
// ═════════════════ КЛЮЧИ ОТКАТА (дефекты оригинала; умолчание = «исправлено») ════════════════════════════
//   asda.Craft.FixDeleteAfterPaging (К-2) — листание снимает выбор; «Удалить инструкцию» шлёт рецепт,
//                                          который показан в окне состава (а не строку другой страницы).
//   asda.Craft.FixNpcEntry          (Д-NPC) — кнопка ремесла в диалоге NPC сначала закрывает разговор (иначе
//                                          собственный гейт 5591 оригинала не пускает никогда).
//   (К-4 — строк 115300/115301 в данных клиента нет, у оригинала подсказки показывают фолбэк индекса 99 «Ты ранишь
//    монстра.»; ★ 26.09.2026 по решению владельца строки внесены в `GameText.csv` из клиента AsdaFableClassic — подсказки
//    показывают их, ключ не нужен; прежний ключ `asda.Craft.MissingTipBody` убран волной В3, ревизия М-7.)
//   ★ Ветка NPC 30009/30010/30088 в текущих данных НЕДОСТИЖИМА (кнопок в сценариях нет, ревизия В0–В2 С-2).
//   Окно состава 77000 (волна В3) — `UAsdaCraftRecipeWindowSubsystem`: клик по строке его открывает, закрытие 76000
//   (`uiWindowHideAndCleanup`) его сбрасывает (`itemTooltipWndReset`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "UI/AsdaMiniStateSubsystem.h" // FAsdaMiniBarFill + ClipFill — общее ядро полос порта
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "Styling/SlateTypes.h"        // FButtonStyle — кэш стилей вкладок общей точки `ApplyTabState`
#include "AsdaCraftWindowSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
struct FAsdaMakingRecipeRow;

// Одна строка списка (группа контролов 76020+10j … 76026+10j).
USTRUCT()
struct FAsdaCraftRowUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage> Mark = nullptr;                 // 76020+10j `…_mark_N` (план 32×32)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MarkText = nullptr;         // подпись того же контрола (пустая)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> T;                  // 76021…76024+10j `t1..t4`
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Hit;                   // планы mark + t1..t4 — зона строки
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Black;                 // 76025+10j `black` (Black_Ot125)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Over;                  // 76026+10j `over` (Box_BlueOt20)
};

UCLASS()
class ASDAREBORN_API UAsdaCraftWindowSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCraftWindowSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (все из `.JUI` и эталона) ──
	static constexpr int32 kRows       = 6;     // строк на страницу (`uiBuildFilteredItemPage`: /6)
	static constexpr int32 kTabs       = 5;     // 76007 `TabBar_W64_5Btn_2`: 0..3 категории, 4 «ВСЕ»
	static constexpr int32 kTabAll     = 4;     // ctor `itemMainWnd_ctor`: [4] = 4
	static constexpr int32 kWinZ       = 733;   // выше инвентаря (720) — как окна-соседи; наверх поднимает показ
	static constexpr int32 kControlsTotal = 63; // узлов группы 760 в `.JUI` (корень + 62 контрола)

	// ═══ ВХОДЫ ═══
	// Порт `tileInteractGateAndOpen@0xFDD1A0` — ТУМБЛЕР со всеми гейтами. true — открыто/закрыто; false — гейт.
	bool GateAndToggle(const TCHAR* Source);
	// Вход из диалога NPC (30009/30088): при `asda.Craft.FixNpcEntry 1` разговор закрывается ДО гейтов.
	bool GateAndToggleFromNpc(int32 NpcCommand);
	// Порт `uiWindowHideAndCleanup@0xFDD590`: сброс выбора, сброс окна состава, спрятать. Идемпотентен.
	void HideAndCleanup();
	bool IsOpen() const { return bOpen; }   // корень 76000 показан (`tileInteract_isControlVisible`)
	// ★ Волна В8 (ревизия В7 МЕЛК-4): строка entity_char.c:16908-16909 блока смерти `CCharacter_SetAction` — `if (видно)
	//   uiWindowHideAndCleanup`. Зовёт ТОЛЬКО блок смерти диалога NPC — место относительно 5045 задаёт ключ К-9
	//   `asda.Craft.FixDeathRestoreOrder` (1 — до 5045, 0 — после, как эталон); порядок детерминирован.
	//   bSnapshotTo5045 = true (исправление К-9, W8-2): закрытие идёт ДО 5045 — снимок окна 77000 не снимается К-7,
	//   его восстановит 5045 в том же блоке.
	void CloseAfterSelfDeath(bool bSnapshotTo5045);

	// ═══ ДЕЙСТВИЯ (ветки `shopGridWndProc`) ═══
	void OnRowClicked(int32 Row);   // msg 0xB по 76020…76076
	void OnPrevPage();              // 76082 (0x12932)
	void OnNextPage();              // 76083 (0x12933)
	void OnDeleteClicked();         // 76096 (0x12940) «Удалить инструкцию» → 6600
	void OnCloseClicked();          // 76097 / 76099 (0x12941 / 0x12943)
	void OnTabSelected(int32 Tab);  // msg 66817 радио-бара 76007

	// ═══ ДИАГНОСТИКА И САМОТЕСТ ═══
	FString DumpControls() const;   // каждый контрол группы 760 + итог «найдено N/63»
	FString DumpState() const;      // вкладка, страница, строки, выбор, over, подсказка
	int32 GetFoundControls() const { return FoundControls; }   // узлов группы 760 разобрано (из 63)
	int32 GetPage() const { return Page; }       // [2] текущая страница (с 1)
	int32 GetPages() const { return Pages; }     // [1] всего страниц
	int32 GetTab() const { return Tab; }         // [4] вкладка (4 — «ВСЕ»)
	int32 GetSelRow() const { return SelRow; }   // [5] выбранная строка (−1 — нет)
	// Рецепт, показанный в окне состава 77000 (`[80+137]`), −1 — окно пусто. Хранит его окно 77000.
	int32 GetSelectedRecipe() const;
	// Хвост `itemTooltipWndReset` окна 77000: `uiPanelHideAndReset(g_pItemMainWnd)` — снять выбор/over, пересобрать.
	void OnRecipeWindowReset();
	// ★ Волна В4: `uiCraftPanel_RefreshSummary@0xFDE4A0` — сводка окна после каждой штуки 77500 (уровень, полоса, «N / M»,
	//   страницы, затемнение выбранной строки — дефект С-4 ревизии В3, ключ `asda.Craft.FixSummaryRowDarken`).
	void RefreshSummary();
	int32 GetFilteredCount() const { return Filtered.Num(); }   // [1383] рецептов во вкладке
	// id рецепта строки Row текущей страницы ([10+Row]), −1 — пусто.
	int32 GetRowRecipe(int32 Row) const { return RowRecipe.IsValidIndex(Row) ? RowRecipe[Row] : -1; }
	// Текст контрола по IDNum (самотест сверяет подписи): 76001, 76004, 76006, 76080, 76081, 76021+10j…
	FString DebugText(int32 IdNum) const;
	bool DebugRowBlack(int32 Row) const;       // затемнение строки видно
	bool DebugFrameVisible() const;            // рамка 76077 видна
	FVector2D DebugFrameSlotPos() const;       // позиция группы рамки в канвасе
	float DebugBarFrac() const { return BarFrac; }
	// Нажать кнопку окна так, как её жмёт игрок (делегат `OnClicked` → реле → обработчик).
	//   IdNum: 76082, 76083, 76096, 76097, 76099, 76007+вкладка×1000 (76007000…76007004 — OPT0..4).
	bool DebugPressButton(int32 IdNum);
	// Экранный центр виджета строки/кнопки — для настоящего клика Slate (`SimulateClickAt`).
	bool DebugRowCenter(int32 Row, FVector2D& Out) const;
	// Журнал «событие → момент» (клики, подсказки, отказы) — самотест.
	const TArray<FString>& GetJournal() const { return Journal; }
	void ClearJournal() { Journal.Reset(); }

private:
	bool EnsureWidget();                        // каркас `FAsdaJuiWindowHandle::Ensure`: поднять WBP и разобрать контролы
	void ResetWidget();                         // снести виджет и зоны (протухший/разборка)
	void SetupControls();                       // разбор дерева WBP по именам `.JUI` (группа 760), кнопки, вкладки, подписи
	void RegisterZones();                       // зона рамки окна (клики не уходят в мир)
	void OpenAfterGates(const TCHAR* Source);   // шаг 5 `tileInteractGateAndOpen`
	void PanelHideAndReset();                   // `uiPanelHideAndReset@0xFDD020`
	void BuildFilteredPage(int32 Category);     // `uiBuildFilteredItemPage@0xFE0A20`
	void RefreshRows();                         // `uiList_RefreshItemRows6@0xFDE700`
	void RefreshSummaryGated();                 // `uiCraftPanelRefreshSummaryGated@0xFDE300`
	void RefreshTabs();                         // вид радио-бара (выбранная вкладка)
	void SetPageTexts();                        // 76080/76081
	void PlaceFrame(int32 Row);                 // `CControl_SetField74_75(рамка, 13, 37*row+23)` в координатах JUI
	void HideOver();                            // [16] over-подсветка строки → спрятать, [16] = −1
	void TickMouse();                           // over (msg 9/0xA) и ЛКМ-выбор (0xB) строк списка
	void TickTooltip(int32 Kind, int32 Idx);    // подсказка строки/кнопки (Kind −1 — спрятать)
	void OnModelChanged(int32 Opcode);          // модель ремесла изменилась → сводка/список
	void JournalAdd(const FString& Line);       // строка журнала + лог
	FString Loc(int32 Id) const;                // loc-строка по id (DT_GameText)
	class UAsdaCraftRecipeWindowSubsystem* RecipeWin() const;   // окно состава 77000 (волна В3)

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;        // корень WBP_ITEM_making_WIN (JUI 76000)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle = nullptr;        // 76001
	UPROPERTY(Transient) TObjectPtr<UWidget>    DragHandle = nullptr;      // 76001 `_DRAG`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnX = nullptr;            // 76099
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnDelete = nullptr;       // 76096
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtDelete = nullptr;      // подпись 76096
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose = nullptr;        // 76097
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtClose = nullptr;       // подпись 76097
	UPROPERTY(Transient) TObjectPtr<UWidget>    Frame = nullptr;           // 76077 группа рамки (`…_select_grp`)
	FVector2D FrameBase = FVector2D::ZeroVector;                          // позиция группы рамки в ассете
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCraftLbl = nullptr;     // 76002
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLvlLbl = nullptr;       // 76003
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLevel = nullptr;        // 76004
	UPROPERTY(Transient) FAsdaMiniBarFill Bar;                             // 76005 (ES0 подложка / ES1 заливка)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtRecipeNum = nullptr;    // 76006
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     TabPlanes;         // 76007 планы 5 вкладок
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    TabButtons;        // `…_OPT0..4`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TabCaptions;       // `…_Text`, `…_Text2..5`
	TArray<FSlateBrush> TabIdlePlanes;                                     // кэши общей точки `ApplyTabState`
	TArray<FButtonStyle> TabIdleStyles;                                    // исходные стили кнопок вкладок (`ApplyTabState`)
	TArray<FLinearColor> TabIdleCaptions;                                  // исходные цвета подписей вкладок (`ApplyTabState`)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ColHeads;          // 76011…76015
	UPROPERTY(Transient) TArray<FAsdaCraftRowUi> Rows;                      // 6 строк
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPage = nullptr;         // 76080
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPages = nullptr;        // 76081
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnPrev = nullptr;         // 76082
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNext = nullptr;         // 76083
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> MarkGrid = nullptr; // иконки результата строк
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;    // реле `OnClicked` кнопок окна (держат делегаты)

	// ── Состояние окна (поля `CItemMainWnd`) ──
	bool bOpen = false;           // корень 76000 показан ([17] видим)
	bool bControlsReady = false;  // WBP разобран, ключевые контролы найдены
	int32 FoundControls = 0;      // найдено узлов группы 760 (дамп/самотест)
	int32 Pages = 1;              // [1]
	int32 Page = 1;               // [2] (с 1)
	int32 Tab = kTabAll;          // [4]
	int32 SelRow = -1;            // [5]
	TArray<int32> RowRecipe;      // [10..15] — id рецепта строки или −1 (у эталона NULL)
	int32 OverRow = -1;           // [16]
	TArray<int32> Filtered;       // [1384..] (число — [1383])
	float BarFrac = 0.f;          // доля заливки 76005 (для дампа)

	int32 TipKind = -1;           // показанная подсказка: вид (−1 — нет)
	int32 TipIdx = -1;            // показанная подсказка: индекс строки/кнопки
	FAsdaMouseFrame Mouse;        // латч нажатий мыши окна
	FDelegateHandle HModel;       // подписка на `UAsdaCraftSubsystem::OnModelChanged`
	TArray<FString> Journal;      // журнал самотеста
};
