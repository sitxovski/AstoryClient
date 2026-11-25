// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishingToolSubsystem — ОКНО «ВЗЯТЬ РЫБОЛОВНУЮ СНАСТЬ» (JUI `Inven_Fishrod_Win`, IDNum 15900)
//   и вся его механика: список удочек, выбор, подтверждение, сеть 6165 → 6166.
//   Полный реверс и таблица КАЖДОГО контрола — `Reports/fishing_window_port.md`.
//
// ═════════════════════ ЧТО ЭТО ЗА ОКНО (а не то, что написано в имени функции) ══════════════════
//   Это НЕ «мешок» и НЕ «слоты экипировки». Это ВЫБОР УДОЧКИ, на которую надевается расходуемая
//   снасть — ЛЕСКА (`itemSubType == 208`) или КРЮЧОК (`itemSubType == 209`).
//   Заголовок = нотис **111080** «Взять рыболовную снасть»; подсказка при открытии = нотис
//   **111051** «Используется <FC=FF99FF99>[%s]</F>. Дважды щелкни нужную удочку. Для отмены
//   щелкни правой кнопкой.»
//   ★ ГРАБЛЯ ИМЕНОВАНИЯ ЭТАЛОНА: обработчик клика по ячейкам зовётся `uiInventorySelectEquipSlot
//     @0xD34C10`, а глобальное состояние — `g_InventoryUI` (0x3520314). Ни то, ни другое к
//     «экипировке»/«UI инвентаря» отношения не имеет: диапазон id 0x3E26…0x3E43 = 15910…15939 —
//     это ячейки ИМЕННО ЭТОГО окна. Проверено дважды: по диапазону id и по телу функции
//     (`itemSubType == 10 && itemVariant == 1` — фильтр удочек).
//
// ═════════════════════ ПОЛНЫЙ ПОТОК (эталон, все ветки) ═════════════════════════════════════════
//   1. ОТКРЫТИЕ. Двойной клик по расходнику КОНТЕЙНЕРА 2 с `itemSubType ∈ {208, 209}`
//      (`item_inventory.c:37864-37872`, ветка `pSlotItem = &g_InventoryArray2 + 57*idx`):
//        • `sub_92CB37(g_InventoryUI, slot)` → `sub_FC9C80`: `this[3] = slot; *(u8*)(this+16) = 1`
//          — ВЗВЕСТИ ОЖИДАНИЕ (материал запомнен);
//        • нотис 111051 с именем материала → однокнопочный msgbox
//          (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, -1, текст, 1)` = ONEBUT_MSGBOX, кнопка 2288);
//        • `uiInventoryClearSelection@0xD34B10`: `this[758] = −1`,
//          `CControl_SetVisible(this[664] /*15900*/, 1)`, `uiInventoryRefreshFilteredGrid`.
//      ДРУГОГО пути показа окна 15900 в бинаре НЕТ (единственный `SetVisible(..., 1)` — здесь).
//   2. СПИСОК. `uiInventoryRefreshFilteredGrid@0xD34D30`: 30 ячеек чистятся (пустая иконка слота,
//      текст ""), курсор 15990 прячется; затем перебираются ВСЕ 30 ячеек КОНТЕЙНЕРА 1
//      (`g_InventoryBag`, шаг 57 б) и в список подряд кладутся записи с
//      `itemSubType == 10 && itemVariant == 1` (УДОЧКИ; в `item_list` их 12, `ItemType == 9`).
//      Страниц/прокрутки НЕТ — контейнер 1 и сетка окна оба по 30 ячеек.
//   3. ВЫБОР. Событие 0xB (`uiInventorySelectFilter@0xD34B90`) — только сдвинуть курсор 15990;
//      событие 8 (`uiSlot_BuildHoverTooltip@0xD35070`) — тултип предмета по 57-байтовой записи;
//      событие 0xD (`uiInventorySelectEquipSlot@0xD34C10`) — выбрать И подтвердить.
//   4. ПОДТВЕРЖДЕНИЕ. `itemConfirmPendingAction@0xFC9D70`: снять взвод, проверить совместимость
//      (`itemCheckUpgradeCompat@0xFC9CB0`: удочка 10/1 И материал 208|209), запомнить удочку в
//      `this[2]` и открыть ДВУХКНОПОЧНЫЙ msgbox `msgClass = 1, noticeId = 78` (кнопки 108510 «OK» /
//      108511 «Отмена»). Текст (`case 'N'` = 78 в `CItem_BuildTooltipText@0xD6E790`):
//        материал 208 → удочка+41 (i16) == 96/97 «уже стоит такая же леска» ? нотис **111028**
//                       (аргументы ПО БАЙТАМ: имя материала, имя удочки, «имя материала +N»; N = удочка+43)
//                     : нотис **111026** (имя материала, имя удочки);
//          ★ 09.10.2026 (`Reports/owner_1009/fmtarg_audit.md`): `fmtArg0..2` декомпила — порядок PUSH,
//          обратный порядку `%s`; прежний порядок порта (удочка первой) был перевёрнут.
//        материал 209 → то же по удочка+45 / удочка+47, нотисы **111030** / **111029**.
//        Ключ выбора 96/97 — это `LinkCode` материала: 1 ⇒ 96 (0x60 '`', «органика»),
//        2 ⇒ 97 (0x61 'a', «синтетика»). При `LinkCode ∉ {1,2}` эталон оставляет текст ПУСТЫМ.
//   5. СЕТЬ. «OK» диалога (`ui_core.c:57122`, `case 0x4E` = 78) → `itemNet_SendItemActionPacket
//      @0xFC9E20` → **6165** (51 б). Ответ — **6166** (117 б), `invHandleItemSlotSyncPacket
//      @0xFCA590`: `result == 1` ⇒ нотис 111027 «Запрос выполнен» + две записи по 57 б (удочка и
//      материал) + новый вес; иначе в msgbox печатается ЧИСЛО кода отказа.
//   6. ЗАКРЫТИЕ. Крестик 15902 (события 0x10401 / 66561) → `sub_928A8C` → `sub_D34B60`:
//      `SetVisible(this[664], 0)`. Плюс окно гасится и ожидание сбрасывается при ОТКРЫТИИ
//      инвентаря (`uiOpenInventoryWindow@0xCFF300`: `sub_928A8C` + `sub_9241EE`).
//      Сброс ожидания без закрытия — двойной клик по ЛЮБОМУ не-удочке (`item_inventory.c:38138`).
//
// ═════════════════════ ЧТО МЕНЯЕТ СЕРВЕР (авторитет данных) ═════════════════════════════════════
//   `Recv_FishingToolUse@0x40E230` (GameServer): проверяет удочку (`itemTemplate+26 == 9`,
//   `+27 == 10`, `+33 == 1`) и материал (`+27 == 208|209`), после чего ПИШЕТ В 57-БАЙТОВУЮ ЗАПИСЬ
//   УДОЧКИ пару «код снасти / запас»:
//       леска  (208): запись+41 (i16) = 96 или 97, запись+43 (i16) = `itemTemplate+14` материала;
//       крючок (209): запись+45 (i16) = 96 или 97, запись+47 (i16) = то же.
//   Это 4-я и 5-я пары массива заточки записи (пары идут с +29 шагом 4) — отдельного места под
//   снасть в записи нет. Клиент эти же смещения ЧИТАЕТ для текста подтверждения (см. п.4),
//   что и подтверждает раскладку с двух сторон.
//
// ═════════════════════ ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ОРИГИНАЛА (закон fix-original-defects) ══════════════
//   Ф1 ПКМ-ОТМЕНА, ОБЕЩАННАЯ В ДАННЫХ, НО НЕ НАПИСАННАЯ. Нотис 111051 прямо говорит «Для отмены
//      щелкни правой кнопкой», однако обработчика ПКМ, снимающего взвод/закрывающего 15900, в
//      срезе бинаря НЕТ (единственные вызовы `sub_9241EE` — открытие инвентаря и двойной клик по
//      не-удочке). Реализуем то, что обещают данные.       откат: `asda.Fishrod.LegacyNoRmbCancel 1`
//   Ф2 ОТКАЗ СЕРВЕРА ПОКАЗЫВАЕТСЯ ГОЛЫМ ЧИСЛОМ (`sprintf("%d", payload[0])`). Число оставляем
//      (текстов под эти коды в таблицах НЕТ — выдумывать нельзя), но рядом пишем в лог
//      расшифровку по `Recv_FishingToolUse`. Это не UI-отступление, а диагностика.
//
// ═════════════════════ ГДЕ ЖИВУТ ВИДЖЕТЫ ════════════════════════════════════════════════════════
//   Окно физически лежит ВНУТРИ `WBP_INVEN_WIN` (в .JUI оно ребёнок `INVEN_WIN` 400), поэтому
//   своего WBP у него нет и перегенерация не нужна — импортёр уже собрал все 71 узел поддерева.
//   Перетаскивание за заголовок и сохранение позиции в ini работают САМИ: ручка объявлена в
//   данных (`EVS{EVT=6, RET=0x20003, CtrlID=15900}` у `Inven_Fishrod_TitleTxt`), импортёр положил
//   её в `DA_JuiWindowMeta` окна `INVEN_WIN` с целью `Inven_Fishrod_Win`, а двигает и сохраняет её
//   общий механизм `UAsdaWindowSubsystem` (закон `ui-window-drag-hittest`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaFishingToolSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;

// Виджеты одной ЯЧЕЙКИ списка удочек. Скин `NoImage_icon` имеет Type = Text, поэтому импортёр
//   сделал на ячейку ПАРУ: план-картинку `Inven_Fishrod_ItemNN_<juiIdx>_0` (иконка) и
//   `TextBlock Inven_Fishrod_ItemNN` (в этом окне эталон пишет туда ПУСТУЮ строку — счётчика нет).
USTRUCT()
struct FAsdaFishrodCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text = nullptr;
};

UCLASS()
class ASDAREBORN_API UAsdaFishingToolSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaFishingToolSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ══════════════════ ЧИСЛА ЭТАЛОНА (менять нельзя) ══════════════════
	static constexpr int32 kWinIdNum     = 15900;   // Inven_Fishrod_Win      (this+664)
	static constexpr int32 kTitleIdNum   = 15901;   // Inven_Fishrod_TitleTxt (this+665), нотис 111080
	static constexpr int32 kCloseIdNum   = 15902;   // Inven_Fishrod_CloseBtn (this+666)
	static constexpr int32 kSelIdNum     = 15990;   // Inven_Fishrod_Select   (this+667)
	static constexpr int32 kCellFirstId  = 15910;   // Inven_Fishrod_Item00   (this+668 + 3*i)
	static constexpr int32 kCellCount    = 30;      // 6 колонок × 5 рядов, шаг 33 px

	// Фильтр списка (эталон `uiInventoryRefreshFilteredGrid`): УДОЧКА = подтип 10 + вариант 1.
	static constexpr int32 kRodSubType   = 10;
	static constexpr int32 kRodVariant   = 1;
	// Материалы: 208 = ЛЕСКА (actionType 0), 209 = КРЮЧОК (actionType 1).
	static constexpr int32 kLineSubType  = 208;
	static constexpr int32 kHookSubType  = 209;
	// Смещения пары «код снасти / запас» в 57-байтовой записи УДОЧКИ (пишет сервер, читает клиент).
	static constexpr int32 kRodLineCodeOff = 41;
	static constexpr int32 kRodLineValOff  = 43;
	static constexpr int32 kRodHookCodeOff = 45;
	static constexpr int32 kRodHookValOff  = 47;
	// Значения кода снасти: 96 = 0x60 '`' (LinkCode 1, «органика»), 97 = 0x61 'a' (LinkCode 2).
	static constexpr int32 kToolCodeOrganic   = 96;
	static constexpr int32 kToolCodeSynthetic = 97;

	// Контейнеры инвентаря: удочки лежат в 1 (`g_InventoryBag`), леска/крючок — в 2 (`g_InventoryArray2`).
	static constexpr int32 kRodContainer      = 1;
	static constexpr int32 kMaterialContainer = 2;

	// ══════════════════ ТОЧКИ ВХОДА ДЛЯ СИСТЕМЫ ИНВЕНТАРЯ ══════════════════
	// ★ Двойной клик по ячейке инвентаря. Возвращает true, если событие СЪЕДЕНО этим окном
	//   (эталонные ветки: материал 208/209 в контейнере 2 → открыть окно; удочка при взведённом
	//   ожидании → подтвердить; ЛЮБОЙ другой предмет при взведённом ожидании → снять взвод и
	//   пропустить событие дальше, как в `item_inventory.c:38138`).
	bool OnInventoryDoubleClick(int32 ContainerId, int32 CellIndex);

	// ★ Эталон `uiOpenInventoryWindow@0xCFF300`: открытие окна инвентаря гасит 15900 и снимает взвод.
	UFUNCTION(BlueprintCallable, Category="Asda|Рыбалка")
	void OnInventoryOpened();

	// ══════════════════ ОКНО ══════════════════
	UFUNCTION(BlueprintPure, Category="Asda|Рыбалка")
	bool IsWindowOpen() const { return bWindowOpen; }

	// Эталон `sub_D34B60` — только спрятать окно (взвод НЕ трогает, как в оригинале).
	UFUNCTION(BlueprintCallable, Category="Asda|Рыбалка")
	void CloseWindow();

	// Снять взвод (эталон `sub_FC9DF0`) и — если bAlsoHide — спрятать окно.
	UFUNCTION(BlueprintCallable, Category="Asda|Рыбалка")
	void CancelPending(bool bAlsoHide = true);

	// Перерисовать список (эталон `uiInventoryRefreshFilteredGrid@0xD34D30`).
	UFUNCTION(BlueprintCallable, Category="Asda|Рыбалка")
	void RefreshList();

	// ══════════════════ ДЕЙСТВИЯ ══════════════════
	// Выбрать позицию СПИСКА (0..29) — эталон `uiInventorySelectFilter` (событие 0xB).
	UFUNCTION(BlueprintCallable, Category="Asda|Рыбалка")
	void SelectListIndex(int32 ListIndex);

	// Выбрать позицию списка И подтвердить (эталон `uiInventorySelectEquipSlot`, событие 0xD).
	UFUNCTION(BlueprintCallable, Category="Asda|Рыбалка")
	bool ConfirmListIndex(int32 ListIndex);

	// Проверить пару «удочка + материал» и показать диалог подтверждения
	//   (эталон `itemConfirmPendingAction@0xFC9D70` + текст `case 'N'` = 78).
	//   Удочка берётся из `RodContainer/RodCell`, материал — из взведённого ожидания.
	UFUNCTION(BlueprintCallable, Category="Asda|Рыбалка")
	bool ConfirmSelectedPair();

	// Отправить 6165 по уже выбранной паре (эталон «OK» диалога 78). false — пары нет/сеть занята.
	UFUNCTION(BlueprintCallable, Category="Asda|Рыбалка")
	bool SendAttachRequest();

	// ══════════════════ ДИАГНОСТИКА ══════════════════
	// Дамп КАЖДОГО разобранного контрола окна (закон `full-structural-analysis-always`).
	FString DumpControls() const;
	// Состояние: взвод, выбранная удочка, список, коды снасти в записи удочки.
	FString DumpState() const;
	// Эмуляция ответа 6166 без сервера: Result — код в payload[0].
	void SimulateResult(uint8 Result);

private:
	// ── Виджеты ──
	bool EnsureWidgets();
	void ResetWidgets();
	void ScanWidgets();
	void ApplyVisibility();
	// ★★★ ГРАБЛЯ ИМПОРТЁРА: `Visible="0"` корня 15900 импортёр РАСПРОСТРАНЯЕТ на ВСЁ поддерево
	//   (`asda_jui_generic_umg.py`, `eff_hidden = hidden or visible == 0` передаётся детям), поэтому
	//   в WBP свёрнут КАЖДЫЙ узел окна, а не только его группа. Показ одной группы дал бы пустое
	//   место. Разворачиваем поддерево целиком; по данным .JUI внутри окна `Visible = 0` стоит
	//   ТОЛЬКО у самого 15900 и у курсора выбора 15990 — курсор и пустые ячейки прячет `RefreshList`.
	static void SetSubtreeVisible(UWidget* Root, bool bVisible);
	bool GetWindowRect(FVector2D& OutMin, FVector2D& OutMax) const;
	void RegisterZone();
	void UnregisterZone();

	// ── Ввод (опрос курсора: `UImage` клик не поглощает, как и во всех окнах порта) ──
	void TickPointer();
	bool IsCursorOver(const UWidget* W) const;
	int32 HitTestCell() const;          // позиция СПИСКА под курсором (−1 нет)
	void ShowCellTip(int32 ListIndex);
	void HideCellTip();

	// ── Модель ──
	// Показать окно (эталон `uiInventoryClearSelection@0xD34B10`).
	void ShowWindow();
	// Взвести ожидание материалом (эталон `sub_FC9C80`).
	void ArmPending(int32 ContainerId, int32 CellIndex);
	// Текст подтверждения (эталон `CItem_BuildTooltipText` msgClass 1, `case 'N'` = 78).
	FString BuildConfirmText(const struct FAsdaItemInstance& Rod, const struct FAsdaItemInstance& Mat,
	                         const struct FAsdaItemRecord& MatRec) const;
	// Ответ сервера 6166 (кладём в очередь, применяем в Tick — закон `no-reentrant-net-callbacks`).
	void OnFishingToolResult(const TArray<uint8>& Body117);
	void ApplyFishingToolResult(const TArray<uint8>& Body117);

	// ── Помощники ──
	void  Msg1Button(const FString& Text);          // эталонный ONEBUT_MSGBOX (msgClass 0, id −1)
	FString LocRaw(int32 Id) const;                 // ★ GetTextByIdRaw — иначе теги <FC=…> срезаются
	// Подстановка `%s`/`%d` подряд, как это делает `sprintf(buf, UI_FormatLocalizedNotice(...))`
	//   с глобалями `fmtArg0..2` эталона. ★ `fmtArg0` — ПОСЛЕДНИЙ аргумент формата (порядок PUSH), Args — по `%s`.
	static FString FormatPrintf(const FString& Fmt, const TArray<FString>& Args);
	void  PlayUiSound(int32 SoundId) const;
	class UAsdaInventorySubsystem* Inv() const;

	UFUNCTION() void OnCloseClicked();
	UFUNCTION() void OnCloseHovered();

	// ══════════════════ ДАННЫЕ ══════════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> HostWidget = nullptr;   // = WBP_INVEN_WIN
	UPROPERTY(Transient) TObjectPtr<UWidget>     WinGroup = nullptr;     // Inven_Fishrod_Win_grp
	UPROPERTY(Transient) TObjectPtr<UWidget>     BaseGroup = nullptr;    // Inven_Fishrod_Base_grp
	UPROPERTY(Transient) TObjectPtr<UWidget>     TitleGroup = nullptr;   // Inven_Fishrod_TitleTxt_grp
	UPROPERTY(Transient) TObjectPtr<UWidget>     TitleBarGroup = nullptr;// Inven_Fishrod_TtlBar_grp
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TitleText = nullptr;    // 15901
	UPROPERTY(Transient) TObjectPtr<UButton>     CloseBtn = nullptr;     // 15902
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> SelCursor = nullptr;  // 15990
	UPROPERTY(Transient) TArray<FAsdaFishrodCellUi> Cells;               // 15910…15939
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	bool bWindowOpen = false;
	bool bControlsReady = false;

	// Эталонное состояние `g_InventoryUI` (0x3520314):
	bool  bPendingArmed = false;      // +16 — материал взведён
	int32 PendingContainer = 0;       // +12 — контейнер материала (всегда 2)
	int32 PendingCell = -1;           // +12 — ячейка материала
	int32 RodContainer = 0;           // +8  — контейнер выбранной удочки (всегда 1)
	int32 RodCell = -1;               // +8  — ячейка выбранной удочки
	int32 SelIndex = -1;              // this[758] — позиция в СПИСКЕ окна (не ячейка контейнера)

	// this[3*i + 669] — какая ячейка КОНТЕЙНЕРА 1 показана в позиции i списка (−1 = пусто).
	int32 ListToBagCell[kCellCount];

	// Ввод
	// ★ Аудит №3 (B3): прежние латчи `bLmbWasDown` + `bRmbWasDown` — один экземпляр общей точки
	//   (обе кнопки опрашивал один `TickPointer`, там же и латч).
	FAsdaMouseFrame Mouse;
	int32 PressedIndex = -1;
	int32 HoverIndex = -1;
	bool  bTipShown = false;
	bool  bCloseHoverSoundPlayed = false;

	// Очередь ответов 6166 (закон `no-reentrant-net-callbacks`).
	//   ★ Аудит №3 (B10): массив + свой `FCriticalSection` заменены общей `TAsdaNetQueue`.
	TAsdaNetQueue<TArray<uint8>> PendingResults;

	// Ждём 6166 — второй 6165 до ответа не шлём (аналог `g_bInventoryActionPending` инвентаря).
	bool   bAwaitingResult = false;
	double SentAtS = 0.0;

	bool bNetBound = false;

public:
	// ══════════════════ ПАРАМЕТРЫ (правятся в редакторе UE, закон ue-editor-friendly) ═══════════
	// Звук нажатия крестика окна. ЭТАЛОН: `csoundPlayName2DStore(g_pSoundMgr, 5, 0)` в ветке
	//   события 66561 (`ui_core.c`, обработчик, где `cmdId == 15902 → sub_928A8C`).
	UPROPERTY(EditAnywhere, Category="Asda|Рыбалка|Звук")
	int32 CloseSoundId = 5;

	// Звук выбора удочки в списке. ★ У ячеек 15910…15939 эталон звука НЕ играет (в ветках
	//   `uiInventorySelectFilter`/`uiInventorySelectEquipSlot` вызовов звука нет) — 0 = молчать.
	UPROPERTY(EditAnywhere, Category="Asda|Рыбалка|Звук")
	int32 SelectSoundId = 0;

	// Сколько секунд ждать 6166, прежде чем разрешить новую отправку. ★ Эталон флага ожидания под
	//   6165 не держит вовсе — это наш предохранитель от «залипания» при потерянном ответе.
	UPROPERTY(EditAnywhere, Category="Asda|Рыбалка")
	float ResultTimeoutS = 5.f;
};
