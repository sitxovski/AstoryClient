// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCraftMakeWindowSubsystem — ОКНО ИЗГОТОВЛЕНИЯ `ITEM_making_Mark_WIN` (JUI 77500), класс эталона `CItemQtyMakeDlg` /
//   `CMakeingItem` (глобал `g_pItemQtyMakeDlg`). Порт 26.09.2026, волна В4 плана `Reports/craft/craft_re_report.md` §12
//   (+ ревизия `craft_re_REVIEW.md`, её поправки важнее). Отчёт волны — `Reports/craft/wave4.md`; структурный дамп ассета
//   (каждый контрол ДО кода) — `Reports/craft/_craft_wbp_tree.txt` (зонд, 133 узла) и `craft_jui_tree.txt` (69 узлов JUI,
//   41 с IDNum), дамп подсистемы в логе (`asda.Craft.MakeState`). Модель ремесла — `UAsdaCraftSubsystem`; вход — кнопки
//   77068/77151/77251 окна состава 77000 (`UAsdaCraftRecipeWindowSubsystem::OpenMakeWindow`).
//
// ═════════════════ ЭТАЛОН ════════════════════════════════════════════════════════════════════════════════════════
//   ctor `CMakeingItem_ctor@0xFE44A0`, bind `CMakeingItem_BindControls@0xFE47B0` (ui_core.c:101648), открытие
//   `uiItemUpgradeWindowOpen@0xFE4D70` (миснейм, :101749), команды `itemQtyDlg_OnCommand@0xFE51C0` (item_inventory.c:69595),
//   сброс `itemQtyDlgReset@0xFE4F50` (:69558), отправка `netSendItemCombineReq@0xFE5E30` (network.c:50080), ответ
//   `upgradeDlg_onResultPacket@0xFE65E0` (item_enhance.c:10335), тик `upgradeAnimDlg_tick@0xFE6E00` (item_enhance.c:10497, из
//   `MainLoop_Tick_SoundUpdate` game_main.c:1531), `craftCountActiveSlots@0xFE4ED0`, `craftWndUpdateAffordability@0xFE5AA0`,
//   `craftCalcMaxMakeableCount@0xFE5C40`, `uiUpgradeSlotsRedraw@0xFE57B0`, `uiUpgradeSlotsDisable@0xFE56D0`,
//   `chat_EnableFourIcons@0xFE5740` (миснейм: включить 4 контрола окна), `uiHideControlGroup7@0xFE5D20`.
//   Поля (dword `CMakeingItem`, `structs_ru.h:3004`): [1] корень 77500 · [2..3] 77501/77502 FMark_1/2 · [4..10] иконки
//   ступеней 77511…77517 · [11..17] анимации «пройдено» 77521…77527 · [18..24] подложки «не сыграно» 77551…77557 ·
//   [25..31] крестики «провал» 77541…77547 · [32] полоса 77531 · [33] статус 77532 · [34]/[35] ±1 77538/77539 · [36] 77536
//   · [37] поле 77537 · [38] «Создать» 77533 · [39] «Закрыть» 77534 · [40] код результата · [42] достигнутая ступень (+10)
//   · [43] «+1 шт.» (+11) · [44] осталось изготовить · [45] ступеней · [46] ступень анимации · [47..61] запись продукта (57)
//   · [62] рецепт · [63] запись rate · байты 256 «идёт анимация», 257 «отмена после текущей», 258 = ([43]==1), 259
//   «ступень отыграна» · [65] время старта ступени (мс).
//   Маршрутизация `IDNum/100 == 775` → `itemQtyDlg_OnCommand`; ESC окно не закрывает (флага `CControl+0x228` нет).
//
// ═════════════════ ПОТОК ════════════════════════════════════════════════════════════════════════════════════════
//   Открытие: нет записи rate по MakeIndex (+124) — окно не открывается; `uiCloseAllWindows` (+ снимок окна состава
//   сохраняется до сброса этого окна) и `uiWindowHideAndCleanup(76000)`; ступеней = число OutItem > 0; иконки ступеней
//   по центровке `g_UpgradeSlotBaseIndexTable` {0,2,2,2,2,1,1,0} (слот = k + база[ступеней]).
//   «Создать» 77533: спрятать ани/крестики, статус пуст; N = [44] (<= 0 ⇒ 1; больше максимума по материалам — урезать);
//   ОДИН 6602 на штуку: 4 контрола выключены, материалы ×1 (иначе ONEBUT 5566 + сброс). Ответ 6603 код 1/2 — старт
//   анимации: 2000 мс полоса 0→2000 на ступень, затем один раз «пройдено/бонус/провал», на 3000 мс — следующая ступень
//   или итог: продукт в сумку (анонс редкого `NotifyItemInfo`), 5577/5580 синим + чат −11514 + плашка (провал — 5578
//   красным), `uiCraftPanel_RefreshSummary(76000)`, --[44]; отмена (257) ⇒ сброс; [44] <= 0 ⇒ кнопки включены, поле «1»;
//   иначе СЛЕДУЮЩИЙ 6602 (серия «N штук = N пакетов», следующий только ПОСЛЕ ответа и анимации — патч P35).
//   «Закрыть» 77534: идёт анимация — «отмена после текущей», кнопка прячется; иначе — сброс.
//   Сброс: продукт идущей анимации кладётся сразу; окно скрыто; `uiExpandPartyPanel` (HUD) и `uiCloseAllToggleWindows`
//   (снимок окон: сумка/персонаж/… открытые до окна состава — их игровыми «открыть» со звуками).
//   Нумпада нет (два метода, §8 отчёта): количество — своё поле 77537, 2 символа, 1..99 стрелками.
//
// ═════════════════ НОТИСЫ (по обработчикам) И ЗВУКИ ════════════════════════════════════════════════════════════
//   `netSendItemCombineReq`: материалов меньше ×1 → ONEBUT 5566 + сброс. `upgradeDlg_onResultPacket`: 0/99 → 5563, 3 → 5566,
//   4 → 5567, 5 → 5568, 6 → 5585 (ONEBUT + сброс — у модели), 1/2 → статус 5575 белым. `upgradeAnimDlg_tick`: 5576/5579
//   синим (0xFF098AFF), 5578 красным (0xFFFF0000); итог 5577/5580 синим + чат/плашка −11514 (0xFFFFD306), 5578 красным +
//   чат/плашка. Своих звуков у окна НЕТ (два метода, §6.3 отчёта); косвенные — у окон-сообщений (модель) и у закрывателей
//   «закрыть всё» / восстановления снимка (их собственные точки).
//
// ═════════════════ КЛЮЧИ ОТКАТА (дефекты оригинала; умолчание = «исправлено», 0 = оригинал дословно) ═══════════════
//   asda.Craft.FixZeroTierResult (К-3) — провал на ПЕРВОЙ ступени: сервер шлёт код 1 со ступенью 0 и пустым продуктом;
//                                        эталон идёт веткой «получен» и печатает ПУСТУЮ строку в статус/чат/плашку, а
//                                        финальную подсветку ставит на слот «база − 1» (у 7-ступенчатого рецепта это −1 —
//                                        FMark_2 77502, ревизия Р-4). Исправлено: ветка «провал» (крестик, 5578 с именем
//                                        предмета проваленной ступени, чат + плашка), финальная подсветка не ставится.
//   asda.Craft.FixLateResult     (К-1, ключ модели) — ответ после сброса окна продукт не теряет; см. `AsdaCraftSubsystem.h`.
//   asda.Craft.FixStackPick      (К-6, ключ модели) — выбор стека в 6602, максимум и цвет поля — по стекам (сервер
//                                        списывает из одного стека); при 0 — сумма стеков и последний стек, как эталон.
//   asda.Craft.FixUnknownCode    (К-5, ключ модели) — 6603 код 10 сбрасывает окно (эталон вис с выключенными кнопками).
//   Своё поведение порта (не дефект оригинала, записано): 6602 не ушёл (замок «операция в полёте» / нет сети) — кнопки
//   возвращаются сразу; замок снят таймаутом (`asda.Craft.ReplyTimeoutSec`) — окно, ждущее 6603, возвращает кнопки.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaMiniStateSubsystem.h"   // FAsdaMiniBarFill + ClipFill — общее ядро полос порта
#include "AsdaSessionReset.h"            // IAsdaSessionResettable
#include "AsdaCraftMakeWindowSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UEditableText;
class UAsdaAnimatedPlane;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
struct FAsdaMakingRecipeRow;
struct FAsdaCraftEnterResult;

// Одна ступень: `z_mark_N` 7751N (группа — иконка + подпись + дети), подложка `_b` 7755N, крестик `x` 7754N, ани `a` 7752N.
USTRUCT()
struct FAsdaCraftTierUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWidget> Group = nullptr;              // `ITEM_making_z_mark_N_grp` — видимость контрола 7751N
	UPROPERTY(Transient) TObjectPtr<UImage> Icon = nullptr;                // план иконки `ITEM_making_z_mark_N_<k>_0` (32×32)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IconText = nullptr;        // подпись того же `NoImage_icon` (пустая)
	UPROPERTY(Transient) TObjectPtr<UWidget> Back = nullptr;               // `ITEM_making_z_mark_N_b_grp` — 7755N `Box_WhiteOt20` + затемнение
	UPROPERTY(Transient) TObjectPtr<UWidget> Fail = nullptr;               // `ITEM_making_z_mark_Nx_<k>_0` — 7754N `NEW_st_X`
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> Ani = nullptr;     // `ITEM_making_z_mark_Na_<k>_0` — 7752N `Ani_Soul2` (22 кадра, 1 с, один проход)
	FName IconName;                                                        // имя плана иконки (сетка иконок)
};

UCLASS()
class ASDAREBORN_API UAsdaCraftMakeWindowSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписки: результат 6603 модели, таймаут замка, смерть; закрыватель «закрыть всё» (`uiCloseAllWindows`, ui_core.c:54385).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписки и снос виджета.
	virtual void Deinitialize() override;
	// Сброс сессии: окно прячется, поля в исходное, снимок окон НЕ восстанавливается (его чистит `UAsdaWindowSubsystem`).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// `upgradeAnimDlg_tick` (каждый кадр, пока идёт анимация) + отложенный сброс по смерти.
	virtual void Tick(float DeltaTime) override;
	// Статистика тика (группа `STATGROUP_Tickables`) — обязательна для `FTickableGameObject`.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCraftMakeWindowSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }   // только в игре
	virtual bool IsTickableWhenPaused() const override { return false; } // на паузе анимация стоит

	// ── Числа окна (все из `.JUI` и эталона) ──
	static constexpr int32 kTiers = 7;              // 77511…77517 (7 ступеней лестницы)
	static constexpr int32 kControlsTotal = 41;     // контролов с IDNum в группе 775 (ревизия реверса §1)
	static constexpr int32 kWinZ = 735;             // выше окна состава 77000 (734) и списка 76000 (733)
	static constexpr int32 kQtyMax = 99;            // `itemQtyDlg_OnCommand` 77538: `if (++[44] > 99) [44] = 99`
	static constexpr int32 kQtyEditLen = 2;         // `ctrlEditSetTextLength(77537, 2)`
	static constexpr double kTierMs = 2000.0;       // полоса ступени 0→2000 (`CProgressBar_SetRange(0, 2000.0f)`)
	static constexpr double kTierEndMs = 3000.0;    // конец ступени: подложки, следующая ступень или итог
	// Центровка ступеней `g_UpgradeSlotBaseIndexTable@0x15B5F38` {0,2,2,2,2,1,1,0} — `kSlotBase` в `.cpp`.

	// ═══ ВХОДЫ ═══
	// Порт `uiItemUpgradeWindowOpen@0xFE4D70`: окно уже видно / рецепта нет / нет записи rate — false; иначе «закрыть всё»,
	//   76000 закрыт, поля в исходное, иконки ступеней, показ. `Source` — для лога.
	bool OpenForRecipe(int32 RecipeId, const TCHAR* Source);
	// Порт `itemQtyDlgReset@0xFE4F50` (зовут: «Закрыть» до анимации, отмена после текущей, коды отказа 6603 у модели,
	//   `uiCloseAllWindows`, `uiCloseAllWindowsOnLeave`, смерть). Идемпотентен.
	void ResetDialog(const TCHAR* Why);
	// ★ Волна В8 (ревизия В7 МЕЛК-4): строка entity_char.c:16910-16911 блока смерти — `if (видно) itemQtyDlgReset`. Зовёт
	//   ТОЛЬКО блок смерти диалога NPC, после закрытия 76000; до или после 5045 — ключ К-9 `asda.Craft.FixDeathRestoreOrder`.
	void ResetAfterSelfDeath();
	// Порт (закон `no-state-latch-before-shown`): ответа на ушедший 6602 не будет — таймаут замка или неизвестный код 6603
	//   (ревизия В4 МЕЛК-3). Кнопки назад, окно остаётся открытым. Ничего не делает, если окно не ждёт ответа.
	void ReturnButtons(const TCHAR* Why);
	bool IsOpen() const { return bOpen; }             // корень 77500 показан (`isBoundControlVisible`)
	bool IsAnimating() const { return bAnimating; }   // байт 256
	bool IsAwaitingReply() const { return bAwaitingReply; }   // 6602 ушёл, 6603 ещё нет (порт)
	int32 GetRecipeId() const { return RecipeId; }    // [62] (−1 = нет)
	int32 GetRemaining() const { return Remaining; }  // [44]
	int32 GetTierCount() const { return TierCount; }  // [45]
	int32 GetCurTier() const { return CurTier; }      // [46]

	// ═══ ДЕЙСТВИЯ (ветки `itemQtyDlg_OnCommand`, msg 66561) ═══
	void OnCreateClicked();                // 77533 «Создать»
	void OnCloseClicked();                 // 77534 «Закрыть»
	void OnQtyUp();                        // 77538 +1 (кламп 99)
	void OnQtyDown();                      // 77539 −1 (кламп 1)

	// ═══ ДИАГНОСТИКА И САМОТЕСТ ═══
	FString DumpControls() const;          // каждый из 41 контрола группы 775 + итог «найдено N/41»
	FString DumpState() const;             // поля `CMakeingItem` и видимость ступеней
	int32 GetFoundControls() const { return FoundControls; }   // найдено контролов из 41 (разбор ассета при первом открытии)
	// Текст контрола: 77532 статус, 77533/77534 подписи кнопок, 77536 «Кол-во», 77537 поле.
	FString DebugText(int32 IdNum) const;
	// Видимость: 77511…77517 иконка ступени, 77521…77527 ани, 77541…77547 крестик, 77551…77557 подложка, 77534 кнопка.
	bool DebugShown(int32 IdNum) const;
	int32 DebugIconCode(int32 Slot) const;             // код иконки слота 0..6 (сетка иконок)
	FLinearColor DebugStatusColor() const;             // цвет статуса 77532
	FLinearColor DebugEditColor() const;               // цвет текста поля 77537
	bool DebugControlsEnabled() const;                 // 77533/77538/77539/77537 включены (`chat_EnableFourIcons`)
	float DebugBarFrac() const { return BarFrac; }     // доля полосы 77531
	bool DebugTierFlag(int32 DwordIdx) const;          // флаг `set_o24F` контрола по dword-индексу эталона (3..10)
	// Нажать кнопку так, как её жмёт игрок (делегат `OnClicked` → реле → обработчик): 77533, 77534, 77538, 77539.
	bool DebugPressButton(int32 IdNum);
	// Ввести текст в поле 77537 так, как это делает Slate (`SetText` → `OnTextChanged`).
	void DebugTypeQty(const FString& Text);
	// Переход из окна состава «без показа» (самотест окна 77000 проверяет свои кнопки, не это окно).
	void DebugSetDryOpen(bool bDry) { bDryOpen = bDry; }
	// Самотест: подменить исходы ступеней (7-ступенчатой записи rate в данных нет — индекс «база − 1» = −1, ревизия Р-4).
	void DebugOverrideRateOut(const TArray<int32>& OutItems);
	const TArray<FString>& GetJournal() const { return Journal; }   // журнал событий окна (самотест читает строки)
	void ClearJournal() { Journal.Reset(); }                          // очистить журнал (самотест — между шагами)
	// Самотест С-1 (волна В5): узел ступени по dword-индексу и его текущий `ZOrder` в канвасе окна (−1 — узла/слота нет).
	UWidget* DebugControlByDword(int32 DwordIdx) const { return ControlByDword(DwordIdx); }
	int32 DebugZOrder(int32 DwordIdx) const;
	// Самотест С-1 путём игрока: по фактической раскладке Slate-канваса узел рисуется ПОСЛЕ всех видимых соседних ступеней.
	bool DebugDrawnAboveNeighbors(int32 DwordIdx, FString& OutWhy) const;

private:
	bool EnsureWidget();                   // каркас `FAsdaJuiWindowHandle::Ensure`
	void ResetWidget();                    // снести виджет и зону
	void SetupControls();                  // `CMakeingItem_BindControls`: разбор 41 контрола + подписи init
	void RegisterZone();                   // зона рамки окна (клики не уходят в мир)

	// `netSendItemCombineReq@0xFE5E30` — ОДИН 6602; false — не ушёл (отказ 5566 со сбросом или замок/сеть).
	bool SendOne();
	// Ответ 6603 код 1/2 (`upgradeDlg_onResultPacket`, общая ветка): забрать результат, старт анимации, статус 5575.
	void OnEnterResult(const FAsdaCraftEnterResult& R);
	void OnRequestTimedOut(int32 Opcode);  // замок снят таймаутом: окно, ждущее 6603, возвращает кнопки
	void TickAnim();                       // `upgradeAnimDlg_tick@0xFE6E00` построчно
	void FinishPiece(int32 SlotBase);      // итог штуки (хвост тика после 3000 мс без следующей ступени)

	// ── помощники эталона ──
	int32 CountActiveSlots() const;        // `craftCountActiveSlots`: OutItem > 0
	int32 CalcMaxMakeable() const;         // `craftCalcMaxMakeableCount` (К-6 — по стекам)
	void UpdateAffordability();            // `craftWndUpdateAffordability`: текст поля = [44], цвет белый/красный
	void RedrawSlots();                    // `uiUpgradeSlotsRedraw`: иконки ступеней по центровке
	void HideGroup7();                     // `uiHideControlGroup7`: ани/крестики скрыты, подложки видимых иконок показаны
	void SetFourEnabled(bool bEnabled);    // `uiUpgradeSlotsDisable` / `chat_EnableFourIcons`: 77533, 77538, 77539, 77537
	void SetStatus(const FString& Text, FColor Color);   // `CControl_SetText(77532)` + `CControl_SetField923(цвет)`
	void SetQtyText(int32 V);              // `sprintf("%d") → CControl_SetText_12F9850(77537)`
	UFUNCTION()
	void OnQtyTextChanged(const FText& Text);   // notify 67585…67600 поля 77537: `atoi` → [44] → цвет
	// Контрол по dword-индексу `CMakeingItem` (2..3 FMark, 4..10 иконки, 11..17 ани, 18..24 подложки, 25..31 крестики).
	//   Индекс вне диапазона (`slot = −1` у К-3 оригинала попадает на 3 и 17) — корректный контрол или nullptr, без падения.
	UWidget* ControlByDword(int32 Idx) const;
	void SetCtrlVisible(int32 Idx, bool bVisible);   // `CControl_SetVisible(self[Idx], …)`
	void ShowAni(int32 Idx);                // `CControl_SetVisible(…, 1)` + `CControl_Show` — запуск одноразовой анимации
	void CloseHideAni(int32 Idx);           // `CControl_SetVisible(…, 0)` + `CControl_CloseHide`
	void SetTierFlag(int32 Idx, bool bOn);  // `sub_92E798 = set_o24F(self[Idx], …)` — флаг +0x24F: контрол рисуется поверх соседей (`AsdaUi::SetTopRender`)
	void ClearTierFlags();                  // снять все флаги +0x24F вместе с порядком рисования (сброс сессии)
	void ResetVisuals();                    // визуальная часть `itemQtyDlgReset` без HUD/снимка (сброс окна и сброс сессии)
	int32 RateOut(int32 Tier) const;        // `rate + 8 + 4·k` (или подмена самотеста)
	bool RateBonusFlagZero() const;         // `rate[2] == 0` (u8 +2) — у рецептов с «+1 шт.»
	FString ItemName(int32 ItemCode) const; // `ItemData_FindByCode` → имя
	FString FormatLocName(int32 LocId, int32 ItemCode) const;   // `wsprintfA(Loc(LocId), имя)`; нет записи — пусто
	void ChatAndPlank(const FString& Text);   // `CChat_AddTokenizedLines(−11514)` + `CControl_SetTooltipText(g_pChatWnd, −11514)`
	void JournalAdd(const FString& Line);     // строка журнала + лог
	FString Loc(int32 Id) const;              // loc-строка по id

	// ── Виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;        // корень WBP_ITEM_making_Mark_WIN (JUI 77500)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMove = nullptr;         // 77535 `ITEM_making_movetext` (пусто)
	UPROPERTY(Transient) TObjectPtr<UWidget>    DragHandle = nullptr;      // 77535 `_DRAG` (EV 6/20003 → 77500)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> FMark;                // 77501/77502 планы `FMark_1/2` (не используются эталоном)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> FMarkText;         // подписи 77501/77502
	UPROPERTY(Transient) TArray<FAsdaCraftTierUi> Tiers;                   // 7 ступеней
	UPROPERTY(Transient) FAsdaMiniBarFill Bar;                             // 77531 `Mark_Mbar` (ES0 подложка / ES1 заливка)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStatus = nullptr;       // 77532 `M_text`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCreate = nullptr;       // 77533 `BOX_Ingbtn`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCreate = nullptr;       // подпись 77533 (loc 5533)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose = nullptr;        // 77534 `Mark_Xbtn`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtClose = nullptr;        // подпись 77534 (loc 5035)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtQtyLabel = nullptr;     // 77536 `BOX_text_ea` (loc 5520)
	UPROPERTY(Transient) TObjectPtr<UEditableText> EditQty = nullptr;      // 77537 `BOX_edit_ea`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EditQtyCaption = nullptr;  // `BOX_edit_ea_Text` — подпись контрола поля (пустая)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnUp = nullptr;           // 77538 `BOX_Ar_h`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnDown = nullptr;         // 77539 `BOX_Ar_d`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMax = nullptr;          // 77548 `BOX_text_max` (не привязан эталоном, скрыт)
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> IconGrid = nullptr; // 7 иконок ступеней (общая точка сеток)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;      // реле кнопок (GC-якорь)

	// ── Состояние (поля `CMakeingItem`) ──
	bool bOpen = false;                    // [1] видим
	bool bControlsReady = false;           // WBP разобран, ключевые контролы найдены
	int32 FoundControls = 0;               // найдено контролов с IDNum (из 41)
	int32 ResultCode = 0;                  // [40]
	int32 Reached = 0;                     // [42] (+10 ответа: сколько ступеней пройдено; 0 — провал на первой)
	int32 BonusFlag = 0;                   // [43] (+11 ответа)
	int32 Remaining = 1;                   // [44]
	int32 TierCount = 0;                   // [45]
	int32 CurTier = 0;                     // [46]
	TArray<uint8> Product57;               // [47..61] запись продукта (57 б); код −1 после итога
	int32 RecipeId = -1;                   // [62]
	bool bRateValid = false;               // [63] != 0
	int32 RateOutItems[kTiers] = { -1, -1, -1, -1, -1, -1, -1 };   // rate +8 + 4k
	int32 RateFlag2 = 0xFF;                // rate +2 (u8): 0 — у рецептов с «+1 шт.»
	bool bAnimating = false;               // байт 256
	bool bCancelAfter = false;             // байт 257
	bool bBonusByte = false;               // байт 258 = ([43] == 1)
	bool bTierPlayed = false;              // байт 259
	double AnimStartS = 0.0;               // [65] (в секундах FPlatformTime)
	bool bAwaitingReply = false;           // порт: 6602 ушёл, ответа ещё нет (решает, забирать ли 6603 — К-1)
	TMap<int32, bool> TierFlags;           // флаги `set_o24F` по dword-индексу (дамп; порядок рисования ведёт `AsdaUi::SetTopRender`)
	TArray<int32> IconCodes;               // коды 7 иконок (0 = пусто)
	FColor StatusColor = FColor::White;    // цвет статуса 77532
	FColor EditColor = FColor(0xC6, 0xC6, 0xC6, 0xFF);   // цвет текста поля (JUI `TextColor=FFC6C6C6`)
	float BarFrac = 0.f;                   // доля полосы 77531
	bool bSettingQtyText = false;          // защита от повторного входа `OnTextChanged` при записи текста кодом
	bool bDryOpen = false;                 // самотест окна 77000: переход без показа
	TArray<int32> DebugRateOut;            // подмена исходов ступеней (самотест Р-4)

	FDelegateHandle HEnter;                // `UAsdaCraftSubsystem::OnEnterResult`
	FDelegateHandle HTimeout;              // `UAsdaCraftSubsystem::OnRequestTimedOut`
	TArray<FString> Journal;               // журнал самотеста
};
