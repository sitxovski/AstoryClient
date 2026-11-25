// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCraftRecipeWindowSubsystem — ОКНО СОСТАВА РЕЦЕПТА `ITEM_making_R_BOX` (JUI 77000) и его под-окна
//   подрецептов `ITEM_making_B_tooltip_1/2` (77100 / 77200). Порт 25.09.2026, волна В3 плана
//   `Reports/craft/craft_re_report.md` §12 (+ ревизия `craft_re_REVIEW.md`). Отчёт волны — `Reports/craft/wave3.md`;
//   структурный дамп ассета (каждый контрол) — `Reports/craft/_craft_wbp_tree.txt` (зонд) и дамп этой подсистемы в
//   логе (`asda.Craft.State`). Модель ремесла (уровень, изученные, материалы) — `UAsdaCraftSubsystem`; окно списка
//   76000 — `UAsdaCraftWindowSubsystem` (оно открывает это окно кликом по строке).
//
// ═════════════════ ЭТАЛОН (класс `CMakeItem`, лежит ВНУТРИ `CItemMainWnd` по `this+80`) ════════════════════════
//   init `CMakeItemSub_InitControls@0xFE1400` (ui_core.c:100981) + `CMakeItemSub_BindControls@0xFE1950` (:101065),
//   открытие `uiCraftWnd_OpenForItem@0xFE20D0` (:101157), состав `uiCraftPanel_Refresh@0xFE3160` (:101462),
//   под-окно `uiCraftWndSetSlotItem@0xFE23B0` (:101208) → `CCraftRecipe_RefreshSlotUI@0xFE3920` (item_enhance.c:10059),
//   очистка под-окна `uiItemWndClearSlot@0xFE2550`, диспетчер `uiItemCommandDispatch@0xFE2670` (:101250),
//   полный сброс `itemTooltipWndReset@0xFE2200` (item_inventory.c:69542), «всё заново» `uiCraftPanelRefreshAll@0xFE4460`.
//   Поля (dword `CMakeItem`): [1] выбранная строка состава / −1 · [2] корень 77000 · [5] рамка 77154 · [6] имя 77002 ·
//   [7] иконка 77003 · [8+7i..14+7i] строка i: plus/иконка/имя/подпись/кол-во/black/over · [50+i] «материал есть» ·
//   [58] видимый over строки · под-окно k (k = 0/1, шаг 39): [59+39k] корень, [61+39k] 77102/77202, [62] рамка 77153,
//   [90+39k] выбранная строка M2..M4 (1..3) / −1, [91+39k] id подрецепта / −1, [92+39k] запись подрецепта,
//   [93] видимый over под-окна 1 · [137] рецепт окна (запись) / 0.
//   Маршрутизация `IDNum/100` = 770/771/772 → `sub_9208A0` → `sub_FDD6D0` → `uiItemCommandDispatch(this+80)`.
//   Типы сообщений: 0xB — ЛКМ-выбор, 9/0xA — вход/выход курсора (over), 66561 — кнопка. Подсказок (msg 8) у окна НЕТ
//   (ревизия §1), своих звуков НЕТ (§6.3 отчёта; ONEBUT/TWOBUT звучат сами).
//
// ═════════════════ ЧТО ПОКАЗЫВАЕТ ════════════════════════════════════════════════════════════════════════════════
//   77002 имя рецепта (+2), 77003 иконка результата (+72). Строки D1…D6: иконка/имя материала (+76+4i), 77012+10i =
//   loc 5550, 77013+10i = «есть / нужно» (+100+2i), plus = у материала есть подрецепт (+112+2i >= 0), black = не хватает
//   ИЛИ уровень ремесла ниже `rec+67`; пустой слот (код < 0) — пустая иконка и тексты, black ПОКАЗАН, plus скрыт.
//   Под-окно k: 77102/77202 = имя РОДИТЕЛЬСКОГО рецепта (`pRecipeData + 2`, где родитель — рецепт 77000 для k=0 и
//   подрецепт под-окна 1 для k=1 — поправка к отчёту §3.2, там «имя подрецепта»); M1 = материал родителя, ради которого
//   открыто под-окно («есть / нужно родителю»); M2..M4 = первые три материала подрецепта. У под-окна 1 у M2..M4 есть
//   black/over/plus, у под-окна 2 — нет (привязка `j > 0 && !i`, ui_core.c:101138).
//
// ═════════════════ КНОПКИ И НОТИСЫ (по обработчику `uiItemCommandDispatch`, закон `loc-per-handler-not-by-family`) ═══
//   77068 «Создание предмета» (5521): материалов нет → ONEBUT 5569; уровень мал → ONEBUT 5584; иначе окно 77500 (В4).
//   77151 / 77251 (5521) под-окна: записи нет → 5570; уровень мал → 5584; материалов нет → 5569; рецепт не изучен → 5585;
//     иначе окно 77500 для ПОДРЕЦЕПТА (В4). ⚠ Порядок проверок у под-окон другой, чем у 77068 (уровень раньше материалов).
//   77067 «Закрыть» (5035) / 77069 XX → `itemTooltipWndReset` (закрыть всё окно состава и снять выбор в 76000).
//   77152 / 77159 → закрыть под-окно 1 и снять выбор строки 77000; 77252 / 77259 → закрыть под-окно 2 и снять выбор под-окна 1.
//
// ═════════════════ Z-СЛОЙ, ВЛАДЕЛЕЦ, ESC ══════════════════════════════════════════════════════════════════════════
//   Все три окна — корни `.JUI`? НЕТ: корень только 77000, под-окна 77100/77200 — его ДЕТИ (в ассете — группы того же
//   WBP). `uiShowChildWindow` нет ни разу ⇒ `SetWindowOwner` НЕ применяется (закон брифа). При открытии эталон делает
//   `CClass_MapInsert` + `set_o229(корень, 1)` (флаг `CControl+0x229` = AlwaysTop: окно уходит в верхний список менеджера
//   и больше не перекрывается поднятыми обычными окнами, `uiControl_AddChildOrdered@0x12B2B60`, ui_core.c:105923) — в
//   порту это полоса приоритетных окон `SetWindowPriority` (выше обычных, ниже тултипов 9099 и модалок 20000).
//   ESC окно не закрывает (флага `CControl+0x228` в init нет). В `uiCloseAllWindows` отдельной строки нет — окно
//   закрывается вместе с 76000 (`uiWindowHideAndCleanup` → `itemTooltipWndReset`).
//
// ═════════════════ СНИМОК ОТКРЫТЫХ ОКОН ══════════════════════════════════════════════════════════════════════════
//   Открытие делает `uiCaptureOpenWindowsState(g_pNpcWnd)` — общая точка `UAsdaWindowSubsystem::CaptureOpenWindows`
//   (владелец `Craft`); восстанавливает её сброс окна 77500 (волна В4) и уход от NPC.
//
// ═════════════════ КЛЮЧИ ОТКАТА (дефекты оригинала; умолчание = «исправлено», 0 = оригинал дословно) ═══════════════
//   asda.Craft.FixStaleSnapshot   (К-7) — окно состава закрыто без перехода в 77500 ⇒ его флаги снимка снимаются
//                                         (у эталона живут до чужого восстановления и переоткрывают давно закрытые окна).
//   asda.Craft.FixStackPick       (К-6) — «есть» материала и «хватает/не хватает» считаются по НАИБОЛЬШЕМУ стеку
//                                         (сервер списывает только из одного указанного стека); ключ — в `AsdaCraftSubsystem.cpp`.
//   asda.Craft.FixSubRowDarken    (Д-В3-1) — строка M2..M4 под-окна 1 без подрецепта затемняется «не хватает ИЛИ уровень
//                                         мал», как все прочие строки; у эталона (байты 0xFE4158…0xFE4205) при низком уровне
//                                         затемнения НЕТ, а при нехватке — только если уровень достаточен.
//   asda.Craft.FixOrphanSubWindow (Д-В3-2) — закрытие под-окна 1 (77152/77159) закрывает и под-окно 2: иначе остаётся
//                                         под-окно подрецепта, чьего родителя на экране уже нет (у эталона закрывается
//                                         только своё — `uiItemWndClearSlot(this, 0)`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "AsdaCraftRecipeWindowSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
struct FAsdaMakingRecipeRow;

// Одна строка материала: D1…D6 окна 77000 или M1…M4 под-окна.
USTRUCT()
struct FAsdaCraftMatRowUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage> Icon = nullptr;          // план иконки `…_mark_N_0` / `…_M{k}_N_0` (32×32)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IconText = nullptr;  // подпись того же `NoImage_icon` (пустая)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Name = nullptr;      // t1 / s1 — имя материала
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Sub = nullptr;       // t2 / s2 — loc 5550 «Сейчас/Нужно»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Num = nullptr;       // t3 / s3 — «есть / нужно»
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Black;          // `black` (план + подпись скина Black_Ot125)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Over;           // `over` (Box_BlueOt20)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Plus;           // `plus` (N_PLUS)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Hit;            // иконка + HIT-планы t1..t3 — зона строки
	UPROPERTY(Transient) TObjectPtr<UWidget> Group = nullptr;        // группа строки (под-окна: `…_M{k}_grp` — видимость строки)
	FName IconName;                                                  // имя плана иконки (сетка иконок)
};

// Одно под-окно подрецепта (77100 / 77200).
USTRUCT()
struct FAsdaCraftSubWinUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWidget> Root = nullptr;         // `ITEM_making_B_tooltip_{n}_grp` (77100/77200)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Title = nullptr;     // 77101/77201 `…_top1` (5531/5532)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Parent = nullptr;    // 77102/77202 `…tooltip{n}_top2` — имя родителя
	UPROPERTY(Transient) TObjectPtr<UButton> BtnOk = nullptr;        // 77151/77251 `…_okbtn` (5521)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOk = nullptr;     // подпись `…_okbtn_Text` (loc 5521)
	UPROPERTY(Transient) TObjectPtr<UButton> BtnX = nullptr;         // 77152/77252 `…_xbtn` (5035)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtX = nullptr;      // подпись `…_xbtn_Text` (loc 5035)
	UPROPERTY(Transient) TObjectPtr<UButton> BtnXX = nullptr;        // 77159/77259 `…_XX`
	UPROPERTY(Transient) TObjectPtr<UWidget> Drag = nullptr;         // ручка `…_top1_N_DRAG` (EV 6/0x20003 → 77100/77200)
	UPROPERTY(Transient) TArray<FAsdaCraftMatRowUi> Rows;            // M1..M4
};

UCLASS()
class ASDAREBORN_API UAsdaCraftRecipeWindowSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписка на модель ремесла (6601 код 1 сбрасывает окно); ESC-закрывателя у окна нет.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписка и снос виджета.
	virtual void Deinitialize() override;
	// Сброс сессии: окно прячется, состояние `CMakeItem` в исходное (снимок окон чистит `UAsdaWindowSubsystem`).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Покадровый опрос мыши (over/клик строк) под модальным гейтом.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCraftRecipeWindowSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }   // только в игре
	virtual bool IsTickableWhenPaused() const override { return false; } // на паузе не опрашиваем

	// ── Числа окна (все из `.JUI` и эталона) ──
	static constexpr int32 kMatRows     = 6;     // строк состава D1…D6
	static constexpr int32 kSubRows     = 4;     // M1…M4 под-окна (M1 — материал родителя)
	static constexpr int32 kSubWins     = 2;     // 77100 / 77200
	static constexpr int32 kWinZ        = 734;   // базовый z до `SetWindowPriority` (над 76000 = 733)
	static constexpr int32 kControlsTotal = 104; // контролов с IDNum в группах 770/771/772 (49 + 33 + 22, ревизия §1)

	// ═══ ВХОД (из окна 76000) ═══
	// Порт `uiCraftWnd_OpenForItem@0xFE20D0`: нет рецепта или тот же рецепт — false (ничего); иначе состав, оба под-окна
	//   очищены, выбор снят, показ, наверх (приоритетная полоса), снимок открытых окон. true — окно открыто этим вызовом.
	bool OpenForRecipe(int32 RecipeId, const TCHAR* Source);
	// Порт `itemTooltipWndReset@0xFE2200`: оба под-окна, рецепт = 0, over/рамка, окно скрыто, `uiPanelHideAndReset(76000)`.
	//   Идемпотентен. ★ К-7: если не идёт переход в 77500 — снимает флаги снимка владельца `Craft`.
	void ResetWindow(const TCHAR* Why);
	// Порт `uiCraftPanelRefreshAll@0xFE4460` (зовёт `uiCraftPanel_RefreshSummary` 76000 после каждой штуки в 77500 — В4).
	void RefreshAll();
	// ★ Волна В4: на время перехода «77000 → 77500» (`uiItemUpgradeWindowOpen` закрывает всё) снимок НЕ снимать —
	//   его восстановит сброс 77500. Взводит/снимает защёлку, которую читает `ResetWindow`.
	void SetHoldSnapshotForMake(bool bHold) { bHoldSnapshotForMake = bHold; }
	// ★ W8-2 (исправление К-9, ключ `asda.Craft.FixDeathRestoreOrder` 1): блок смерти закрывает окна ремесла ДО 5045 —
	//   снимок 77000 в этот миг не «протух» (К-7), его сразу восстанавливает 5045 (entity_char.c:16900). Защёлку взводит
	//   и снимает только `UAsdaCraftWindowSubsystem::CloseAfterSelfDeath`; её читает `ResetWindow`.
	void SetHoldSnapshotForDeathRestore(bool bHold) { bHoldSnapshotForDeath = bHold; }
	// Значение ключа К-7 `asda.Craft.FixStaleSnapshot` (волна В8: его читает и `UAsdaCraftSubsystem::CloseMainWindow`, С-2).
	static bool IsStaleSnapshotFixEnabled();

	bool IsOpen() const { return bOpen; }                       // корень 77000 показан
	int32 GetRecipeId() const { return RecipeId; }              // [137] (−1 = пусто)
	int32 GetSelRow() const { return MainSel; }                 // [1]
	bool IsSubOpen(int32 K) const { return K >= 0 && K < kSubWins && SubOpen[K]; }
	int32 GetSubRecipeId(int32 K) const { return (K >= 0 && K < kSubWins) ? SubId[K] : -1; }   // [91+39k]
	int32 GetSubSelRow(int32 K) const { return (K >= 0 && K < kSubWins) ? SubSel[K] : -1; }    // [90+39k]

	// ═══ ДЕЙСТВИЯ (ветки `uiItemCommandDispatch`) ═══
	void OnMainRowClicked(int32 Row);      // msg 0xB по 77010…77066
	void OnSubRowClicked(int32 Idx);       // msg 0xB по 77120…77150 (Idx 1..3 — M2..M4 под-окна 1)
	void OnMakeClicked();                  // 77068 (0x12D0C)
	void OnSubMakeClicked(int32 K);        // 77151 (0x12D5F) / 77251 (0x12DC3)
	void OnSubCloseClicked(int32 K);       // 77152/77159 (0x12D60/0x12D67) и 77252/77259 (0x12DC4/0x12DCB)
	void OnCloseClicked();                 // 77067 / 77069 (0x12D0B / 0x12D0D)

	// ═══ ДИАГНОСТИКА И САМОТЕСТ ═══
	FString DumpControls() const;          // каждый контрол групп 770/771/772 + итог «найдено N/104»
	FString DumpState() const;             // состояние окна: рецепт, выборы, строки D1..D6
	int32 GetFoundControls() const { return FoundControls; }   // сколько контролов с IDNum разобрано (из 104)
	// Текст контрола по IDNum: 77001, 77002, 77011…77063 (t1..t3 строк), 77067, 77068, 77101/77102/77111…77143/77151/77152,
	//   77201/77202/77211…77243/77251/77252.
	FString DebugText(int32 IdNum) const;
	bool DebugShown(int32 IdNum) const;    // видимость: 77014+10i black, 77015+10i over, 77016+10i plus, 77154, 77153,
	                                       //   77124/77134/77144 black под-окна 1, 77126/77136/77146 plus, 77100/77200, 77120+10j (строка)
	FVector2D DebugSlotPos(int32 IdNum) const;   // позиция в канвасе: 77154 (группа рамки), 77153 (план 0 рамки)
	int32 DebugIconCode(int32 IdNum) const;      // код, который сетка поставила иконке: 77003, 77010+10i, 77110+10j, 77210+10j
	// Нажать кнопку так, как её жмёт игрок (делегат `OnClicked` → реле → обработчик): 77067, 77068, 77069, 77151, 77152,
	//   77159, 77251, 77252, 77259.
	bool DebugPressButton(int32 IdNum);
	// Экранный центр зоны строки (для настоящего клика Slate): Kind 0 — строка 77000 (Row 0..5), 1 — под-окно 1 (Row 1..3).
	bool DebugRowCenter(int32 Kind, int32 Row, FVector2D& Out) const;
	const TArray<FString>& GetJournal() const { return Journal; }   // журнал «событие → момент» самотеста
	void ClearJournal() { Journal.Reset(); }

private:
	bool EnsureWidget();                   // каркас `FAsdaJuiWindowHandle::Ensure`: поднять WBP и разобрать контролы
	void ResetWidget();                    // снести виджет и зоны (протухший/разборка)
	void SetupControls();                  // разбор дерева WBP по именам `.JUI`, иконки, кнопки, подписи init
	void RegisterZones();                  // зоны рамок окна и двух под-окон (клики не уходят в мир)
	// `uiCraftPanel_Refresh@0xFE3160` — состав рецепта [137]. false — рецепта/предмета-результата нет.
	bool RefreshPanel();
	// `CCraftRecipe_RefreshSlotUI@0xFE3920` — под-окно K.
	bool RefreshSlotUI(int32 K);
	// `uiCraftWndSetSlotItem@0xFE23B0` — открыть под-окно K подрецептом SubRecipeId. false — тот же / записи нет.
	bool SetSlotItem(int32 K, int32 SubRecipeId);
	void ClearSlot(int32 K);               // `uiItemWndClearSlot@0xFE2550`
	void HideTooltipControlReset();        // `uiHideTooltipControlReset@0xFE22B0`: over, рамка 77154, [1] = −1
	void CraftSlotHideAndReset();          // `uiCraftSlotHideAndReset@0xFE2320`: over под-окна 1, рамка 77153, [90] = −1
	void PlaceMainFrame(int32 Row);        // `CControl_SetField74_75(77154, 64, 40*i + 7)`
	void PlaceSubFrame(int32 Idx);         // `CControl_SetField74_75(77153, 40, 34*idx + 48)`
	void SetMainOver(int32 Row);           // [58]
	void SetSubOver(int32 Idx);            // [93]
	// Материал в строке: «есть / нужно» и признак нехватки — по общей точке модели (К-6 — наибольший стек).
	void FillMatRow(FAsdaCraftMatRowUi& Ui, int32 Code, int32 Need, int32& OutHave);
	void ClearMatRow(FAsdaCraftMatRowUi& Ui);   // пустой материал: тексты строки пусты
	// Порт перехода в `uiItemUpgradeWindowOpen@0xFE4D70` — окно изготовления 77500 (`UAsdaCraftMakeWindowSubsystem`).
	void OpenMakeWindow(int32 ForRecipeId, const TCHAR* Source);
	void TickMouse();                      // over (msg 9/0xA) и ЛКМ-выбор (0xB) строк состава и под-окна 1
	void OnModelChanged(int32 Opcode);     // 6601 код 1 → `itemTooltipWndReset`
	void JournalAdd(const FString& Line);  // строка журнала + лог
	FString Loc(int32 Id) const;           // loc-строка по id (DT_GameText)
	const FAsdaMakingRecipeRow* RecipeRow(int32 Id) const;   // `findRecordById16` модели
	const FAsdaMakingRecipeRow* SubRecord(int32 K) const;    // запись под-окна `[92+39k]` (пустая при `bSubRecNull`)
	int32 CraftLevel() const;              // `sub_929CF7(g_pItemMainWnd)` = [221]

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;        // корень WBP_ITEM_making_R_BOX (JUI 77000)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle = nullptr;        // 77001
	UPROPERTY(Transient) TObjectPtr<UWidget>    DragHandle = nullptr;      // 77001 `_DRAG`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtName = nullptr;         // 77002
	UPROPERTY(Transient) TObjectPtr<UImage>     ResultIcon = nullptr;      // 77003 план
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultText = nullptr;      // 77003 подпись
	FName ResultIconName;                                                  // имя плана иконки 77003 в сетке `IconGrid`
	UPROPERTY(Transient) TArray<FAsdaCraftMatRowUi> MatRows;               // D1…D6
	UPROPERTY(Transient) TObjectPtr<UWidget>    MainFrame = nullptr;       // 77154 `ITEM_making_B_select_grp`
	FVector2D MainFrameBase = FVector2D::ZeroVector;                       // позиция группы рамки в ассете
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel = nullptr;       // 77067
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCancel = nullptr;       // подпись 77067 (loc 5035)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnMake = nullptr;         // 77068
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMake = nullptr;         // подпись 77068 (loc 5521)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnExit = nullptr;         // 77069
	UPROPERTY(Transient) TArray<FAsdaCraftSubWinUi> Subs;                  // 77100 / 77200
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SubFrame;             // 77153 — 8 планов `…_1_select_124_0…7`
	TArray<FVector2D> SubFrameBase;                                        // позиции планов рамки в ассете
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> IconGrid = nullptr; // все 15 иконок окна (общая точка)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;    // реле `OnClicked` кнопок окна и под-окон (держат делегаты)

	// ── Состояние (поля `CMakeItem`) ──
	bool bOpen = false;                    // корень 77000 показан (`[2]` видим)
	bool bControlsReady = false;           // WBP разобран, ключевые контролы найдены
	int32 FoundControls = 0;               // найдено контролов с IDNum (дамп/самотест)
	int32 RecipeId = -1;                   // [137]
	int32 MainSel = -1;                    // [1]
	bool MatPresent[kMatRows] = {};        // [50..55]
	int32 MainOver = -1;                   // [58]
	bool SubOpen[kSubWins] = {};           // видимость [59+39k]
	int32 SubSel[kSubWins] = { -1, -1 };   // [90+39k]
	int32 SubId[kSubWins] = { -1, -1 };    // [91+39k] (запись [92+39k] — `SubRecord(k)`)
	bool bSubRecNull[kSubWins] = {};       // запись [92+39k] = NULL при прежнем [91+39k] (МЕЛК-8, `uiCraftWndSetSlotItem`)
	int32 SubOver = -1;                    // [93]
	TArray<int32> IconCodes;               // коды 15 иконок (0 = пусто): 0 результат, 1..6 D1..D6, 7..10 M1..M4 (1), 11..14 (2)
	bool bSubRaised = false;               // под-окно 1 поднято над 2 (`CClass_MapInsert(aSubWin[0])`)
	bool bHoldSnapshotForMake = false;     // переход в 77500 — снимок не снимать (В4)
	bool bHoldSnapshotForDeath = false;    // закрытие из блока смерти до 5045 — снимок отдаётся 5045 (W8-2)
	FAsdaMouseFrame Mouse;                 // латч нажатий мыши окна
	FDelegateHandle HModel;                // подписка на `OnModelChanged` модели
	TArray<FString> Journal;               // журнал самотеста
};
