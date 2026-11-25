// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaHighRefineSubsystem — ОКНО ВЫСОКОЙ ПЕРЕПЛАВКИ `Smelting3_Win` (JUI **78200**), «Особый» из окна
//   заточки 20000 (кнопка 20197). Порт 25.09.2026, волна 7 плана `Reports/gear_upgrade_re_report.md`
//   §3.3 / §11-бис. Отчёт захода — `Reports/gear_upgrade/wave5-7.md`; структурный дамп ассета —
//   `Reports/gear_upgrade/_smelting3_wbp_tree.txt`.
//
// ═════════════════ ★★★ ЧТО ЭТО ЗА ОКНО В ОРИГИНАЛЕ ═════════════════════════════════════════════
//   Класс `CHighRefineUI` (исходник `interface highrefine.cpp`), глобал `g_pTooltipWnd` — ★ МИСНЕЙМ IDA,
//   к тултипам отношения не имеет. Игрок выбирает предмет СНАРЯЖЕНИЯ из сумки (сетка 14 ячеек), окно
//   находит РЕЦЕПТ в `RefineHigh.BIN` (68 б × 994, в порту `DT_refinehigh`) по трём полям предмета —
//   `requiredLevel` (+4), `itemType` (+12), `itemGrade` (+16) — первой подходящей записью, показывает
//   три материала «есть / надо», цену (+64) и опыт за успех (+8), и по «ОК» после полосы прогресса шлёт
//   6500. Сервер (`Recv_User_UpperRefine@0x4FC7E0`) бросает шанс и добавляет/меняет опции предмета.
//
// ═════════════════ ФУНКЦИИ ЭТАЛОНА (все прочитаны целиком) ═══════════════════════════════════
//   `CHighRefineUI_Init@0xCF3870` (item_enhance.c:1395) · `CHighRefineUI_InitControls@0xCF2AD0` (ui_core.c:45998)
//   · `craftWnd_Open@0xCF3B00` · `craftWnd_Close@0xCF3BA0` · `craftWnd_OnButtonMsg@0xCF3D00` (кнопки)
//   · `uiTooltipWndProc@0xCF36B0` (диспетчер сообщений окна) · `craftWndOnButtonCommand@0xCF4050` (события
//   0x18 полосы 78280 и эффектов 78272/78273) · `craftWndRefreshMoneyAndRecipe@0xCF3F60` (клик)
//   · `craftWnd_RefreshRecipeInfo@0xCF48D0` · `uiInventoryBagPanelRefresh@0xCF4400` (сетка)
//   · `uiResetTradeWindow@0xCF5130` · `uiItemTooltipOnHoverDispatch@0xCF4120` · `itemComposeValidateSelection
//   @0xCF5290` · `itemCompose_SendCraftRequest@0xCF5390` (6500) · `Recv_ItemUpdate@0xCF5B90` (6501)
//   · `craftWndResetMoney@0xCF3600` · `craftWndResetMoneyTexture@0xCF3F10` · `sub_CF3EC0` (занято, кнопки выкл.)
//   · `sub_CF3E80` (смена страницы, msg 0x17) · `CHighRefine_LoadRefineHighBin@0xCF5FE0`.
//   Миснеймы: `craftWnd*`, `uiTooltipWndProc`, `uiResetTradeWindow`, `uiInventoryBagPanelRefresh`,
//   `Recv_ItemUpdate`, `itemCompose_*` — всё это ОДНО окно 78200. `CChat_SetIconState(btn, 0/1)` — это
//   байт +293 контрола «принимает ввод» (читается в обработчиках мыши, 0x12E3580), не иконка чата.
//   `uiSetHonorPointsDisplay` — установка суммы ЗОЛОТА.
//
// ═════════════════ ФЛАГИ ОКНА (байты/поля объекта эталона) ═══════════════════════════════════
//   +0 инициализировано · +2 «занято» (кнопки ОК и X не принимают ввод) · +3 «последний ответ — успех»
//   · +4 «идёт переплавка» (полоса/ожидание ответа) · [92] страница сетки · [93] выбранная ячейка
//   страницы · [94] ключ рецепта (−1 — нет) · [95] «материалов хватает» · [96] момент последнего ответа.
//
// ═════════════════ ПОТОК ═════════════════════════════════════════════════════════════════════
//   20197 → `craftWnd_Open` (видимое окно не трогает) → сброс денег/флагов, сброс выбора, сетка →
//   клик по ячейке → рецепт (иконки, «есть / надо» белым/красным, цена, опыт, гайд 15302) → «ОК»:
//   материалов хватает ⇒ полоса 0→100 за 1000 мс «до ширины плана» (заполняется за ≈388 мс), звук
//   `S_e_refine_process.wav`, «идёт» = 1, «занято» = 1; иначе гайд 15303 → конец полосы (событие 0x18
//   от 78280) → проверка (рецепт / материалы 15303 / золото 15304) → 6500 → 6501: записи материалов,
//   при коде 1 запись предмета, вес и золото (коды 1/8), нотисы отказов в чат, эффект 78272 (успех)
//   или 78273 (иначе) → конец эффекта — сброс полосы и «занято».
//   ★ Нумпад окно НЕ зовёт (23 функции окна, `uiOpenContextPopupWithHandlers` / `g_pNumPadDialog` —
//   0 вызовов; контрольная пара — 26 вызовов в бинаре тем же поиском).
//   ★ Звук у окна ОДИН — `S_e_refine_process.wav` на «ОК» (`CSoundFlag_PlaySound2D`, 2D по имени).
//   Звука клика кнопок у окна НЕТ (ветка 66561 `uiTooltipWndProc` звука не играет).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaReplyLatch.h"        // FAsdaReplyLatch — засов «запрос в полёте»
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "Items/AsdaItemDbRows.h"      // FAsdaRefinehighRow (DT_refinehigh, генерируемый заголовок)
#include "UI/AsdaScrollBarNomal.h"     // FAsdaScrollBarNomal — общая полоса `Nomal_Scr` (78226, волна 12)
#include "AsdaHighRefineSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
class UAsdaAnimatedPlane;
struct FAsdaItemInstance;

// ── Тексты окна. Все id проверены по `RawAssets/Text/GameText.csv`; каждый — из СВОЕГО обработчика. ──
namespace AsdaHighRefineLoc
{
	// `CHighRefineUI_InitControls` — статичные подписи.
	constexpr int32 kTitle       = 15145;   // 78201 «Настройки создания предмета»
	constexpr int32 kTopText     = 15142;   // 78202 «Добавление/изменение параметров предмета»
	constexpr int32 kOptionTitle = 15144;   // 78210 «Список возможных предметов» (`CWidget_SetTextAll4`)
	constexpr int32 kGuideInit   = 15143;   // 78230 при инициализации
	constexpr int32 kMatTitle    = 15307;   // 78231 «Необходимые материалы»
	constexpr int32 kExpLabel    = 15322;   // 78281 «Опыт за успех»
	constexpr int32 kUseGold     = 15107;   // 78284 «Стоимость»
	constexpr int32 kHaveGold    = 15108;   // 78286 «Золото»
	constexpr int32 kBtnOk       = 2288;    // 78291 «ОК»
	constexpr int32 kBtnCancel   = 2289;    // 78292 «Отмена»
	// Гайд 78230 — ТЕКСТ В КОНТРОЛ (не лента).
	constexpr int32 kGuideSelect = 15301;   // `uiResetTradeWindow` — «В списке снаряжения выбери предмет…»
	constexpr int32 kGuideReady  = 15302;   // `craftWnd_RefreshRecipeInfo` — рецепт найден
	constexpr int32 kGuideNoMats = 15303;   // «ОК» без материалов / проверка перед 6500
	constexpr int32 kGuideNoGold = 15304;   // проверка перед 6500 — золота меньше цены
	// Ответ 6501 (`Recv_ItemUpdate`) — системный чат, цвет −256 (0xFFFFFF00), 10 с, буфер A.
	constexpr int32 kRes0 = 15130;   // «Прервано: ошибка данных.»
	constexpr int32 kRes2 = 15132;   // «Не хватает золота.»
	constexpr int32 kRes3 = 15133;   // «Нет места в рюкзаке.»          (сервер не шлёт)
	constexpr int32 kRes4 = 15134;   // «Превышен максимальный вес.»    (сервер не шлёт)
	constexpr int32 kRes5 = 15135;   // «Не хватает материалов.»
	constexpr int32 kRes6 = 15136;   // «Нет материалов.»
	constexpr int32 kRes7 = 15137;   // «Уровень ремесла слишком низок.» (сервер не шлёт)
}

// Позиция сетки окна: ячейка сумки (контейнер 1) и КОД предмета на момент построения списка.
//   Эталон держит в векторе `this+88` указатель на запись сумки (живой) и строку иконки (снимок):
//   рецепт/тултип читаются по живой записи, а иконка сетки — снимок до следующего построения.
USTRUCT()
struct FAsdaHighRefineEntry
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 CellIndex = -1;   // ячейка сумки 0…29 (`j` цикла эталона)
	UPROPERTY(Transient) int32 IconCode  = 0;    // код предмета на момент построения (иконка)
};

UCLASS()
class ASDAREBORN_API UAsdaHighRefineSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable ── окно, флаги и замок «6500 в полёте» принадлежат СЕССИИ.
	//   Эталон: `LoadAllInterfaces2` case 13 → `sub_CF3AD0` = повторный `CHighRefineUI_Init` (окно скрыто).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaHighRefineSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (из `.JUI` и `CHighRefineUI_InitControls`) ──
	static constexpr int32 kGridCells  = 14;    // 78211…78224, страница сетки
	static constexpr int32 kMats       = 3;     // 78241/78251/78261 (+1 имя, +2 «есть / надо»)
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон перебирает РОВНО 30 ячеек сумки — у нас всю сумку (60 =
	//   `UAsdaInventorySubsystem::kShellSlots`, static_assert в .cpp; страницы по 14 уже есть; ★ U21: было `kBagCells`).
	static constexpr int32 kShellCells = 60;
	static constexpr int32 kSpendCells = 60;    // и 60 ячеек расходников (поиск материалов)
	static constexpr int32 kWindowZ    = 733;   // над окном заточки (731) и его попапом (732)
	static constexpr double kProgressAnimMs = 1000.0;   // `uiProgressBar_AnimStart(bar, 1000)`
	static constexpr double kRecentMs = 1000.0;          // `uiCheckRecentActivity`: `now − [96] < 0x3E8`

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	// Порт `craftWnd_Open@0xCF3B00`: видимое окно НЕ трогает (это не тумблер).
	UFUNCTION(BlueprintCallable, Category="Asda|HighRefine")
	void OpenWindow();
	// Порт `craftWnd_Close@0xCF3BA0`: закрывает, только если нет «недавней активности».
	UFUNCTION(BlueprintCallable, Category="Asda|HighRefine")
	void CloseWindow();
	UFUNCTION(BlueprintPure, Category="Asda|HighRefine")
	bool IsOpen() const { return bOpen; }
	// Порт СВОЕЙ части `uiCheckRecentActivity@0xD326A0`: +4 «идёт» или < 1000 мс после ответа 6501.
	//   ★ Волна 9: публичная — её читает общая точка `AsdaUi::CheckRecentActivity` (инвентарь и окна его процедуры).
	bool IsRecentActivity() const;

	// ═══ ДЕЙСТВИЯ (боевые обработчики; консоль и самотест зовут их же) ═══
	void OnGridCellClicked(int32 Cell);   // msg 0xB/0xF по 78211…78224 → `craftWndRefreshMoneyAndRecipe`
	void OnOkClicked();                   // 78291 → `craftWnd_OnButtonMsg`
	void OnCancelClicked();               // 78292 → `craftWnd_OnButtonMsg`
	void OnCloseClicked();                // 78299 → `craftWnd_OnButtonMsg`
	bool OnWheel(int32 Notches);          // ★ Д7-3: колесо над сеткой = событие 0x17 смены страницы
	// ★ Волна 12: действие полосы 78226 (стрелки 69633/69634, дорожка 69635/69636, бегунок 69637) →
	//   событие 0x17 (`sub_12F3540` колёсного окна) тем же путём, что колесо. true — действие принято
	//   (страница сменилась или съедено «занятостью»). Покадровый опрос мыши зовёт его же.
	bool OnListScroll(EAsdaScrollAction Action, int32 Value);

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол + итог «найдено N/M»
	FString DumpState() const;      // флаги эталона, выбор, рецепт, список, полоса, замок
	// Положить тело 6501 в очередь приёма — ТЕМ ЖЕ путём, что сеть (закон diagnostic-must-use-real-path).
	void DebugInjectResult(const TArray<uint8>& Body235) { PendingResults.Push(Body235); }
	// Собрать правдоподобное тело 6501 из текущего выбора (код, опция для записи предмета).
	bool DebugBuildResult(int32 Code, TArray<uint8>& OutBody) const;
	// Тело 6500 для текущего выбора — ТЕМ ЖЕ сборщиком, что отправка (для проверки раскладки без сервера).
	bool DebugBuildRequest(TArray<uint8>& OutBody) const;
	// Досрочно «доиграть» полосу 78280 (событие 0x18) — проверка цепочки без ожидания кадров.
	void DebugFinishProgress() { FinishProgress(); }
	// Журнал «событие → момент» (звук, полоса, эффекты, отправка) — самотест сверяет порядок.
	const TArray<FString>& GetJournal() const { return Journal; }
	void ClearJournal() { Journal.Reset(); JournalStart = -1.0; }
	// Текст гайда 78230 сейчас (самотест сверяет нотисы, которые окно пишет в контрол).
	FString GetGuideText() const;
	// Выбранный рецепт (ключ Idx) или −1.
	int32 GetRecipeKey() const { return RecipeKey; }
	// ★ Волна 9 (самотест таймаута Д7-6 без сервера): поставить те же флаги, что успешная отправка 6500 —
	//   «идёт» и «занято» держатся, полосы нет, замок «в полёте» с отсчётом таймаута от «сейчас».
	void DebugMarkInFlight();
	bool DebugIsBusy() const { return bBusy; }             // +2 «занято»
	bool DebugIsInProgress() const { return bInProgress; } // +4 «идёт»
	bool DebugIsInFlight() const { return ReplyLatch.IsArmed(); }     // ★ Д7-6: замок «6500 в полёте»
	// ★ Волна 11 (МЕЛК-1): снести виджет тем же `ResetWidget`, что зовёт пересоздание (`DropStale`) —
	//   самотест проверяет, что полоса без замка не оставляет «идёт» и 6500 не уходит при следующем открытии.
	void DebugResetWidget() { ResetWidget(); }
	// ★ Волна 12: страница сетки [92], её максимум и планы полосы 78226 (самотест кликает по ним Slate-кликом).
	int32 DebugGetPage() const { return Page; }
	int32 GetMaxPage() const;
	UWidget* DebugGetScrollPlane(int32 Plane) const;   // 0 дорожка, 1 ↑, 2 ↓, 3 бегунок; nullptr — нет
	bool DebugIsScrollShown() const;                   // полоса 78226 на экране
	FString DumpListScroll() const;                    // зона, планы, геометрия и бегунок полосы 78226
	// ★ Волна 13 (СРЕДН-1 / М-8): выбор [93] и ячейка сумки позиции списка (−1 — нет) — самотест сверяет
	//   с ними ячейку в теле 6500.
	int32 DebugGetSelCell() const { return SelCell; }
	int32 DebugGetListCell(int32 Abs) const { return List.IsValidIndex(Abs) ? List[Abs].CellIndex : -1; }
	// ★ Волна 13 (М-8): перехват тел 6500, уходящих БОЕВЫМ путём `SendCraftRequest` → `SendUpperRefine6500` →
	//   `SendSecured` (перехватчик стенда `UAsdaNetSubsystem::SendCaptureForStand`). Ставится самотестом на время
	//   проверки; в игре не используется.
	//   ★ Волна 14 (МЕЛК-3 ревизии волны 13): перехват УЗКИЙ — глушится ТОЛЬКО 6500 (`SendCaptureOnlyOpcode`), весь
	//   остальной трафик идёт как шёл. Снимается сам: в перехватчике после первого 6500, по таймауту
	//   `kCapture6500TimeoutSec` в `Tick`, в `Deinitialize` и при `bOn = false`. При живом соединении с сервером
	//   НЕ ставится (возврат false) — самотест тогда не жмёт «ОК», чтобы 6500 не ушёл на сервер.
	bool DebugCapture6500(bool bOn);
	// ★ Волна 14 (МЕЛК-3): состояние перехвата для самотеста — поставлен ли ЭТОЙ подсистемой, стоит ли вообще
	//   перехватчик у сетевой подсистемы, и не подменяет ли он «связь есть» (узкий перехват не должен).
	bool DebugIsCapture6500Armed() const { return bCapture6500Armed; }
	bool DebugNetHasSendCapture() const;
	bool DebugNetLinkFakedByCapture() const;
	const TArray<TArray<uint8>>& DebugGetSent6500() const { return DebugSent6500; }

private:
	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZones();
	void EnsureInit();                  // `CHighRefineUI_Init`: таблица рецептов и начальные поля (один раз)
	void LoadRecipes();                 // `CHighRefine_LoadRefineHighBin` ← `DT_refinehigh`

	// ── Порт функций окна ──
	void ResetMoney();                  // `craftWndResetMoney@0xCF3600`
	void ResetMoneyTexture();           // `craftWndResetMoneyTexture@0xCF3F10`: «занято» = 0, ОК и X снова принимают ввод
	void SetBusy();                     // `sub_CF3EC0`: «занято» = 1, ОК и X не принимают ввод
	void ResetTradeWindow();            // `uiResetTradeWindow@0xCF5130`
	void RefreshBagPanel();             // `uiInventoryBagPanelRefresh@0xCF4400`
	void RefreshRecipeInfo(int32 SelectedCell);   // `craftWnd_RefreshRecipeInfo@0xCF48D0` (−1 — по [93])
	void RefreshMoneyText();            // текст 78287 = `String_FormatThousands(золото)`
	bool ValidateSelection();           // `itemComposeValidateSelection@0xCF5290`
	void SendCraftRequest();            // `itemCompose_SendCraftRequest@0xCF5390`
	void OnResult(const TArray<uint8>& Body);   // `Recv_ItemUpdate@0xCF5B90`
	void ApplyResultRecords(const uint8* Body, int32 Code);   // записи материалов и предмета в модель
	void OnEffectFinished(int32 IdNum);         // `craftWndOnButtonCommand` case 78272 / 78273
	void FinishProgress();                      // `craftWndOnButtonCommand` case 78280
	void ReleaseInFlight(const TCHAR* Why);     // ★ Д7-6: снять замок «6500 в полёте»
	void OnReplyTimeout();                      // ★ Д7-6 (волна 9): таймаут = ответ «ничего» — окно освобождается
	void DrainPendingPackets();

	// ── Полоса 78280 (`uiProgressBar_AnimStart` + `CProgressBar_UpdateAnim` + `CProgressBar_SetValue`) ──
	void SetProgressValue(float Value);         // 0…100 → ширина плана заливки
	void SetProgressVisible(bool bVisible);
	void TickProgress();

	// ── Данные ──
	const FAsdaItemInstance* LiveOf(const FAsdaHighRefineEntry& E) const;   // живая запись ячейки сумки
	const FAsdaRefinehighRow* RecipeAt(int32 Index) const;                  // `containerFindByKey(this+84, i)`
	bool FindMaterialRecord(int32 ItemCode, int32 Need, TArray<uint8>& OutRec57) const;   // `itemTableFindRecordById`
	FString Loc(int32 LocId) const;

	// ── Опрос мыши и тултипы (msg 8/9/0xA/0xB) ──
	void TickMouse();
	void TickTooltip(int32 Kind, int32 Idx);
	// ★ Волна 12: полоса 78226 — страница сетки (общая точка колеса и полосы), раскладка, опрос мыши.
	void SetPage(int32 NewPage, const TCHAR* Source);   // `sub_CF3E80` + `uiResetTradeWindow` + снятие тултипа
	void LayoutListScroll();                            // `uiScrollbarSetRange`/`LayoutThumb` по странице и списку
	bool TickListScroll();                              // true — мышь у полосы (клик в сетку/окно не идёт)
	// ★ Волна 13 (СРЕДН-1): ключ Д7-3 = 0 ⇒ страница 0, как у оригинала (у него `Page ≡ 0`). Выключение ключа
	//   на ходу со 2-й страницы иначе оставляло сетку/рецепт на странице 2, а отправку — на странице 1.
	void EnforceSinglePageWhenKeyOff();
	void PlaySfx(const TCHAR* Name);
	void JournalAdd(const FString& Event);
	UFUNCTION() void HandleE1Finished();   // событие 0x18 контрола 78272
	UFUNCTION() void HandleE2Finished();   // событие 0x18 контрола 78273

	// ═══ ВИДЖЕТЫ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle      = nullptr;   // 78201
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTop        = nullptr;   // 78202
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtOptTitle   = nullptr;   // 78210 (подпись вкладки `_Text`)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     GridIcons;         // 78211…78224 (планы)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GridLabels;        // 78211…78224 (текст ячейки)
	UPROPERTY(Transient) TObjectPtr<UWidget>    CheckFrame    = nullptr;   // 78225 `Slot_AniSelect`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGuide      = nullptr;   // 78230
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtMatTitle   = nullptr;   // 78231
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     MatIcons;          // 78241/78251/78261 (планы)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MatIconLabels;     // 78241/78251/78261 (текст)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MatNames;          // 78242/78252/78262
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MatCounts;         // 78243/78253/78263
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtExpLabel   = nullptr;   // 78281
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtExp        = nullptr;   // 78282
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtUseGoldLbl = nullptr;   // 78284
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtUseGold    = nullptr;   // 78285
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHaveGoldLbl= nullptr;   // 78286
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtHaveGold   = nullptr;   // 78287
	UPROPERTY(Transient) TObjectPtr<UImage>     ResultIcon    = nullptr;   // 78271 (план 64×64)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultLabel   = nullptr;   // 78271 (текст)
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> FxE1  = nullptr;   // 78272 `Ani_ShellEffect`
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> FxE2  = nullptr;   // 78273 `Ani_DistructnEffect`
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnOk         = nullptr;   // 78291
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel     = nullptr;   // 78292
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnX          = nullptr;   // 78299
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBtnOk      = nullptr;   // подпись 78291
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBtnCancel  = nullptr;   // подпись 78292
	UPROPERTY(Transient) TObjectPtr<UImage>     BarBg         = nullptr;   // 78280 план 0 (фон, ширина = «максимум анимации»)
	UPROPERTY(Transient) TObjectPtr<UImage>     BarFill       = nullptr;   // 78280 план 1 (заливка)
	UPROPERTY(Transient) TObjectPtr<UWidget>    DragHandle    = nullptr;   // 78201 `_DRAG` — ручка окна
	// ★ Волна 12 (разрешение владельца 25.09): колёсные окна `WheelWin_Nomal` и полоса 78226 — узлы АССЕТА
	//   (шаг пост-импорта `Tools/asda_ui_smelting3_scroll.py`), код ведёт только бегунок и ввод.
	UPROPERTY(Transient) TObjectPtr<UWidget>    ListZone1     = nullptr;   // 78226 `Smelting3_list_1` (зона списка снаряжения)
	UPROPERTY(Transient) TObjectPtr<UWidget>    ListZone2     = nullptr;   // 78265 `Smelting3_list_2` (зона материалов, скрыта)
	UPROPERTY(Transient) FAsdaScrollBarNomal    ListScroll;                // полоса 78226 — планы `Smelting3_list_1_Scr_*`
	FVector2D ListScrollOrigin = FVector2D::ZeroVector;   // левый-верх полосы в канвасе её планов (из ассета: стрелка ↑)
	float ListScrollH = 0.f;                              // высота полосы (из ассета: верх ↑ … низ ↓)
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridIconGrid   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> MatIconGrid    = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> ResultIconGrid = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	FVector2D CheckBase = FVector2D::ZeroVector;   // позиция рамки 78225 «как в .JUI» (над ячейкой 0)
	float BarBaseLeft = 0.f;                       // левый край заливки из ассета
	float BarFullW = 0.f;                          // ширина плана 0 (эталон `animMax` = ширина плана 0)

	// ═══ МОДЕЛЬ (поля объекта эталона) ═══
	bool bInited = false;          // +0 — `CHighRefineUI_Init` выполнен (таблица рецептов загружена)
	bool bOpen = false;            // окно видимо (`CControl_IsVisible(m_pUI_Main)`)
	bool bControlsReady = false;
	bool bBusy = false;            // +2 — «занято»: ОК и X не принимают ввод
	bool bDone = false;            // +3 — последний ответ 6501 = успех (код 1)
	bool bInProgress = false;      // +4 — «идёт переплавка»
	int32 Page = 0;                // [92] — страница сетки
	int32 SelCell = -1;            // [93] — выбранная ячейка страницы
	int32 RecipeKey = -1;          // [94] — ключ рецепта (поле +0 записи), −1 — нет
	bool bMatsOk = false;          // [95] — материалов хватает
	double LastReplyTime = -1.0;   // [96] — момент последнего ответа 6501 (сек); у эталона — время Init
	TArray<FAsdaHighRefineEntry> List;     // this+88 — предметы снаряжения сумки
	TArray<FAsdaRefinehighRow> Recipes;    // this+84 — записи `RefineHigh.BIN` в порядке файла (Idx 0…N)
	int32 MatCodes[kMats] = { 0, 0, 0 };   // коды материалов, нарисованные в 78241/51/61 (0 — пусто)
	int32 ResultCode = 0;                  // код предмета в иконке 78271 (0 — пусто)
	double ProgressStart = -1.0;           // `uiProgressBar_AnimStart`: момент старта (сек), <0 — полоса стоит
	float ProgressValue = 0.f;             // текущее значение полосы 0…100
	FAsdaReplyLatch ReplyLatch;            // ★ Д7-6: 6500 ушёл, ответа 6501 ещё нет. ★ S2 (В2-A дедупа №2, 11.10.2026): общая точка вместо `bInFlight + InFlightSince`
	double AttemptStartTime = -1.0;        // ★ Д7-7: момент «ОК» текущей попытки (сек), <0 — попытки нет
	int32 TipKind = -1;                    // что под курсором показано плашкой
	int32 TipIdx = -1;
	FAsdaMouseFrame Mouse;
	TAsdaNetQueue<TArray<uint8>> PendingResults;  // закон `no-reentrant-net-callbacks`
	TArray<FString> Journal;               // журнал «событие → момент» (самотест)
	double JournalStart = -1.0;
	bool bBusyScrollLogged = false;        // ★ волна 13 (М-6): «полоса съедена» записана в журнал за этот захват/клик
	TArray<TArray<uint8>> DebugSent6500;   // ★ волна 13 (М-8): тела 6500, перехваченные самотестом
	bool bCapture6500Armed = false;        // ★ волна 14 (МЕЛК-3): перехватчик 6500 поставлен ЭТОЙ подсистемой
	double Capture6500ArmedAt = 0.0;       // ★ волна 14 (МЕЛК-3): когда поставлен (сек) — для таймаута в `Tick`
	uint32 Capture6500Gen = 0;             // ★ волна 15 (МЕЛК-2): поколение СВОЕГО перехватчика (`ArmSendCapture`), 0 — нет
	static constexpr double kCapture6500TimeoutSec = 5.0;   // ★ волна 14: шаг самотеста ≈0.9 с, запас ×5
	void DisarmCapture6500(const TCHAR* Why);                // ★ волна 14: снять СВОЙ перехватчик (чужой не трогает)
	FDelegateHandle HResult;
};
