// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaDisassemblySubsystem — ОКНО РАЗБОРКИ (ДЕМОНТАЖА) СНАРЯЖЕНИЯ `Disassembly_Win` (JUI **32000**).
//   Порт 06.10.2026, волна 2-А (решение владельца «внимательно, надёжно»). Полный реверс, таблица нотисов,
//   структурный разбор и итог — `Reports/owner_1006/wave2A_disassembly.md`; дамп ассета —
//   `Reports/owner_1006/wave2A/_disassembly_wbp_tree.txt`.
//
// ═════════════════ ★★★ ЧТО ЭТО ЗА ОКНО В ОРИГИНАЛЕ ═════════════════════════════════════════════
//   Объект `g_pRecvUiHandler3` = 0x2C38030 (класс `CDisjoint`, ★ МИСНЕЙМЫ IDA: `upgradeUi*`, `upgradeWnd_*`,
//   `UpgradeUI_RefreshItemSlots`, `g_bRefineInProgress` — всё это окно 32000, к заточке отношения не имеет).
//   Игрок выбирает в окне (список 32050…32079 = сумка 0…29, только то, что есть в `DecompositionTable.BIN`
//   с флагом +4 == 1) предмет, видит 6 возможных результатов (32010…32015) и жмёт «Приступить к демонтажу»
//   (32002) → 6502 (контейнер + слот). Сервер (`Recv_User_Disassemble@0x4FD9B0`) бросает результат по
//   `DisassembleTable.txt`, БД удаляет предмет и кладёт материал → 6503 (119 б). Клиент поднимает «жар»
//   32030 по столбу до слота результата (25 px на слот, 1 px за кадр), в конце — звук, рамка слота, запись
//   в инвентарь. Предмет с переносом улучшения (≥ 88 % на сервере) превращается в 401 (оружие) / 363.
//
// ═════════════════ ФУНКЦИИ ЭТАЛОНА (все прочитаны целиком) ═══════════════════════════════════
//   `CDecomposeWindow_InitControls@0xE0D010` (ui_core.c:70021) · `CDisjointWnd_LoadDecompositionTable@0xE0F4B0`
//   · `upgradeUiOnOpen@0xE0D530` (тумблер) · `upgradeWnd_onMessage@0xE0C0F0` · `upgradeUiOnSlotClick@0xE0C390`
//   · `decomposeWnd_RefreshSlots@0xE0CC60` · `UpgradeUI_RefreshItemSlots@0xE0D840` · `upgradeUiConfirmSend@0xE0D420`
//   · `netSendByteWordCmd_6502@0xE0E460` · `Recv_ItemDisjoint@0xE0EAD0` · `CDisjoint_TickIfActive@0xE0E390`
//   · `CDisjointWnd_TickAnim@0xE0C8B0` · `upgradeUiWnd_close@0xE0E3C0` · `upgradeUiWnd_applyResult@0xE10340`
//   · `upgradeUiWnd_reset@0xE10610` · `itemSlotShowTooltipById@0xE0F140` · `inventoryHasFreeMainAndBagSlot@0xE0F3E0`
//   · `recordLookupByKeyId@0xE10560`.
//
// ═════════════════ ПОЛЯ ОБЪЕКТА ЭТАЛОНА (dword) → ПОЛЯ ПОРТА ═════════════════════════════════
//   [89] состояние −1 закрыто / 0 / 1 «идёт» / 2 «готово» → `State` · [93…178] 6 записей превью по 57 б →
//   `Preview` · [179] выбранная запись сумки → `SelCell` · [180] запись результата для 32016 → `bResultShown` +
//   `ResultRec` · [182…] тело 6503 → `Pkt` · [212] список → `List` · [215] слот результата 1…6 → `SelSlot` ·
//   [216] тик полосы → `AnimTick` · [217…220] позиция/размер полосы → `GaugeBase`/`GaugeSize` · [221] «тултип
//   показан» → `TipKind` · [222] «сумка была открыта при открытии окна» → `bInvWasOpen` · [223] момент
//   последнего применения результата → `LastApplyTime` (условие 4 `uiCheckRecentActivity`).
//
// ═════════════════ ПОТОК ═════════════════════════════════════════════════════════════════════
//   Вход (хоткей 40 / кнопка инвентаря 453 / доп-меню 4 / слот 6 снимка окон) → `Toggle` → список →
//   ЛКМ по позиции (0xB) → превью → «Начать» (66561 по 32002) → проверка 2 свободных ячеек → 6502, кнопка
//   98701, СУМКА ЗАКРЫВАЕТСЯ (`sowelWindowClose`) → 6503 код 1: «идёт», эффект 32005, петля звука →
//   полоса до слота → конец: звук результата, запись в превью и инвентарь, рамка 32020+слот, 98702.
//   Коды 2…9 → ONEBUT + чат 98600+(код−2); код 0 — молча. Закрытие: X 32001 / ESC (0x15) / тумблер — не
//   при «идёт»; «закрыть все окна» — всегда. Была открыта сумка — после закрытия окна открывается снова.
//   ★ Нумпад окно НЕ зовёт (0 вызовов в функциях окна; контрольная пара — 38 по эталону тем же поиском).
//   ★ Гейта «души» у 6502 нет (сервер: блок 0x1964…0x196A без проверки, GameServer.c:19089).
//   ★ Перетаскивания, ПКМ и выбора из окна сумки у эталона НЕТ — только ЛКМ по позиции списка окна.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaReplyLatch.h"        // FAsdaReplyLatch — засов «запрос в полёте»
#include "UI/AsdaBagListScroll.h"      // ★ 29.10.2026 (инвентарь ×2): FAsdaBagListScroll — окно 30 позиций над списком до 60
#include "UI/AsdaShopDblClick.h"   // ★ U1 (В2-A дедупа №2): FAsdaDblClick — двойной щелчок по порогу Windows
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "Items/AsdaItemDbRows.h"      // FAsdaDecompositiontableRow (DT_decompositiontable, генерируемый заголовок)
#include "AsdaDisassemblySubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAudioComponent;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
class UAsdaAnimatedPlane;
struct FAsdaItemInstance;

// ── Тексты окна. Все id проверены по `RawAssets/Text/GameText.csv`; каждый — из СВОЕГО обработчика. ──
namespace AsdaDisassemblyLoc
{
	constexpr int32 kTitle     = 100000;   // 32003 «Демонтаж предмета» (`CDecomposeWindow_InitControls`)
	constexpr int32 kBtnStart  = 98700;    // 32002 «Приступить к демонтажу» (открытие, сброс, клик по списку)
	constexpr int32 kBtnBusy   = 98701;    // 32002 «идет демонтаж» (`upgradeUiConfirmSend` после 6502)
	constexpr int32 kBtnDone   = 98702;    // 32002 «выполнено» (`CDisjointWnd_TickAnim`, конец полосы)
	constexpr int32 kNoSpace   = 100300;   // ONEBUT: «…не менее двух свободных ячеек рюкзака.» (`upgradeUiConfirmSend`)
	constexpr int32 kGemWarn   = 100800;   // ONEBUT: «Живой камень уничтожается…» (`upgradeUiOnSlotClick`)
	constexpr int32 kResBase   = 98600;    // 6503 код N (2…9) → 98600 + N − 2: ONEBUT + чат −11514 (`Recv_ItemDisjoint`)
}

UCLASS()
class ASDAREBORN_API UAsdaDisassemblySubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписка на 6503, роспись ESC и «закрыть все окна».
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписка, снятие виджета, остановка петли звука.
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable ── окно, состояние и замок «6502 в полёте» принадлежат СЕССИИ.
	//   Эталон: `LoadAllInterfaces2` → `CDecomposeWindow_InitControls` (ui_core.c:54069) — состояние −1, окно скрыто.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Очередь 6503, полоса «жара» (`CDisjoint_TickIfActive`, каждый кадр), мышь и тултипы.
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaDisassemblySubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем (эталон стоит в игровом цикле).
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (из `.JUI` и `CDecomposeWindow_InitControls`) ──
	static constexpr int32 kListCells   = 30;    // 32050…32079 — ВИДИМЫЕ позиции списка (★ 29.10: окно `BagScroll` над `List`)
	static constexpr int32 kPreviews    = 6;     // 32010…32015 — возможные результаты (+ 32020… рамка, 32150… «провал»)
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон перебирает РОВНО 30 ячеек сумки — у нас всю сумку (60 =
	//   `UAsdaInventorySubsystem::kShellSlots`, static_assert в .cpp; ★ U21 (В1 дедупа №2): было `kBagCells`).
	static constexpr int32 kShellCells  = 60;
	static constexpr int32 kSpendCells  = 60;    // и проверяет расходники 1…59 (`inventoryHasFreeMainAndBagSlot`)
	static constexpr int32 kBodyLen     = 119;   // 6503: `memcpy(recvPkt, src + 9, 0x77)`
	static constexpr int32 kTicksPerSlot = 25;   // `targetTicks = 25 * m_nSelSlotIdx`
	static constexpr int32 kGaugeLift   = 50;    // полоса стоит на `базовый Y + 50` (высота 32030 в `.JUI`)
	static constexpr int32 kWindowZ     = 734;   // над окном высокой переплавки (733)
	static constexpr double kRecentMs   = 1000.0;   // `uiCheckRecentActivity`: `now − [223] < 0x3E8`
	static constexpr int32 kTransferUpgradeType9 = 401;   // превью «перенос улучшения» оружия (`itemType == 9`)
	static constexpr int32 kTransferUpgradeOther = 363;   // превью «перенос улучшения» доспеха (`itemType <= 4 || == 8`)

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	// Порт `upgradeUiOnOpen@0xE0D530` — ТУМБЛЕР всех трёх входов (видимое окно закрывает, если не «идёт»).
	//   `Source` — для лога (хоткей 40 / кнопка 453 / доп-меню / снимок окон).
	UFUNCTION(BlueprintCallable, Category="Asda|Disassembly")
	void Toggle(const FString& Source);
	// Порт `upgradeUiWnd_close@0xE0E3C0` — БЕЗ проверки состояния (так его зовут X/ESC после своей проверки и
	//   «закрыть все окна» — вообще без неё).
	UFUNCTION(BlueprintCallable, Category="Asda|Disassembly")
	void CloseWindow();
	// Окно видно (`sub_933CA7` = байт +292 корня 32000).
	UFUNCTION(BlueprintPure, Category="Asda|Disassembly")
	bool IsOpen() const { return bOpen; }
	// Состояние [89]: −1 закрыто / 0 / 1 «идёт разборка» / 2 «готово».
	int32 GetState() const { return State; }
	// Условия 2 и 4 эталонной `uiCheckRecentActivity@0xD326A0`: [89] == 1 или `now − [223] < 1000`.
	//   Читает ОБЩАЯ точка `AsdaUi::CheckRecentActivity` (своих копий у мест вызова нет).
	bool IsRecentActivity() const;
	// [89] == 1 — «идёт разборка» (`g_bRefineInProgress` — миснейм IDA; читает «ОК» окна 78200).
	bool IsDisassembling() const { return State == 1; }

	// ═══ ДЕЙСТВИЯ (боевые обработчики; опрос мыши, консоль и самотест зовут их же) ═══
	void OnSlotClick(int32 IdNum);     // msg 0xB → `upgradeUiOnSlotClick` (список 32050…32079, иначе только перерисовка)
	void OnStartClicked();             // 66561 по 32002 → `upgradeUiConfirmSend`
	void OnCloseClicked();             // 66561 по 32001 — `if ([89] != 1) upgradeUiWnd_close`
	void OnEscPressed();               // 0x15 — `if ([89] != 1) upgradeUiWnd_close`
	void OnTargetDoubleClick();        // 0xD по 32004 → `upgradeUiWnd_reset`
	void OnMainWindowEnter();          // 8 по 32000 → `decomposeWnd_RefreshSlots(0)` + [221] = 1

	// ═══ ДИАГНОСТИКА / САМОТЕСТ ═══
	FString DumpControls() const;      // КАЖДЫЙ разобранный контрол + итог «найдено N/M»
	FString DumpState() const;         // поля эталона, список, превью, полоса, замок
	int32 GetFoundControls() const { return FoundControls; }   // сколько контролов разобрано
	int32 GetTotalControls() const { return TotalControls; }   // сколько ожидалось
	const TArray<FString>& GetJournal() const { return Journal; }   // журнал «событие → момент» (звук, нотисы, отправка)
	void ClearJournal() { Journal.Reset(); JournalStart = -1.0; }   // очистить журнал
	const TArray<int32>& GetListCells() const { return List; }      // ячейки сумки позиций списка
	int32 GetSelCell() const { return SelCell; }                    // [179] — ячейка сумки выбранного (−1 нет)
	int32 GetPreviewCode(int32 I) const;                            // код превью I (0 — пусто)
	int32 GetPreviewPercent(int32 I) const;                         // «+N%» превью I (поле записи +27)
	int32 GetSelSlot() const { return SelSlot; }                    // [215] — слот результата 1…6 (−1 не найден)
	float GetAnimTick() const { return AnimTick; }                  // [216] — тик полосы
	bool IsInFlight() const { return ReplyLatch.IsArmed(); }                   // ★ [ПОРТ] замок «6502 в полёте»
	bool IsResultShown() const { return bResultShown; }             // [180] ≠ 0 — 32016 рисует результат
	int32 GetResultCode() const;                                    // код записи результата 32016
	FString GetButtonText() const;                                  // текст кнопки 32002
	FString GetLabelText(int32 IdNum) const;                        // текст подписи контрола (32004/32016/32010…32015/32050…)
	bool IsControlShown(int32 IdNum) const;                         // виден ли контрол (32005/32031/32090/32020…32025/32030)
	UWidget* GetHitWidget(int32 IdNum) const;                       // узел для настоящего клика Slate (кнопки, иконки)
	FVector2D GetGaugeSlotPos() const;                              // позиция полосы 32030 в канвасе
	FVector2D GetGaugeSlotSize() const;                             // размер полосы 32030
	FVector2D GetSelFramePos() const;                               // позиция рамки 32090 в канвасе
	// Тело 6502 для текущего выбора — ТЕМ ЖЕ сборщиком, что отправка (раскладка без сервера).
	bool DebugBuildRequest(TArray<uint8>& OutBody) const;
	// Пропустить анимацию: довести тик до цели (тем же `TickAnim`, что кадр) — для длинных проверок.
	void DebugFinishAnim();
	// Таблица разборки загружена (кол-во записей) и запись по коду (nullptr — нет).
	int32 GetTableCount() const { return Table.Num(); }
	const FAsdaDecompositiontableRow* FindRecord(int32 ItemCode) const;   // порт `recordLookupByKeyId@0xE10560`

private:
	// ── Жизненный цикл окна ──
	bool EnsureWidget();               // каркас `FAsdaJuiWindowHandle::Ensure`
	void ResetWidget();                // снять ссылки на виджет (пересоздание/выход)
	void SetupControls();              // порт `CDecomposeWindow_InitControls` (разбор + статичные тексты)
	void RegisterZones();              // зона окна по рамке 3×3
	void EnsureTable();                // порт `CDisjointWnd_LoadDecompositionTable` ← `DT_decompositiontable`

	// ── Порт функций окна ──
	void OpenFresh();                  // ветка «окно скрыто» `upgradeUiOnOpen`
	void RefreshList();                // `decomposeWnd_RefreshSlots(this, 0)`
	void RefreshItemSlots();           // `UpgradeUI_RefreshItemSlots`
	void ResetWindow();                // `upgradeUiWnd_reset` (только при [89] != 1)
	void ApplyResult();                // `upgradeUiWnd_applyResult`
	void TickAnim();                   // `CDisjointWnd_TickAnim` (один кадр эталона)
	void PlaceGauge();                 // хвост `TickAnim`/сброса: позиция `(x, y + 50 − тик)` и размер `(w, тик)`
	void ClearPreviews();              // `memset(this + 93, 0, 0x156)`
	void HidePreviewMarks();           // `CloseHide` + скрыть 32020…32025
	bool HasFreeSlots() const;         // `inventoryHasFreeMainAndBagSlot@0xE0F3E0`
	void OnResult(const TArray<uint8>& Body);   // `Recv_ItemDisjoint@0xE0EAD0`
	void DrainPendingPackets();        // закон `no-reentrant-net-callbacks`: тела 6503 разбираются из Tick
	void ReleaseInFlight(const TCHAR* Why);   // ★ [ПОРТ] снять замок «6502 в полёте»
	void SetButtonText(int32 LocId);   // `CWidget_SetTextAll4(32002, нотис)`
	void ShowPlane(UWidget* W, bool bShow);    // `CControl_Show`+`SetVisible(1)` / `CloseHide`+`SetVisible(0)`
	void NoticeResult(int32 LocId);    // ONEBUT + чат −11514, 10 с (`Recv_ItemDisjoint` коды 2…9)
	void StartLoop();                  // `CSoundFlag_PlaySound2D("S_e_disjoint_process.wav", 1)` — петля
	void StopLoop(const TCHAR* Why);   // `sub_9349C7(g_pSoundMgr, "S_e_disjoint_process.wav")`
	void TickLoop();                   // петля: ассет без флага петли — перезапуск, пока [89] == 1
	void PlaySfx(const TCHAR* Name);   // одноразовый 2D по имени
	void JournalAdd(const FString& Event);   // журнал самотеста
	FString Loc(int32 LocId) const;    // loc-строка (теги срезаны)

	// ── Данные ──
	const FAsdaItemInstance* LiveBag(int32 Cell) const;           // живая запись ячейки сумки
	static int32 RecCode(const TArray<uint8>& R);                  // код записи 57 б (+0)
	static int32 RecPercent(const TArray<uint8>& R);               // процент заточки записи (+27, i16)

	// ── Опрос мыши и тултипы (msg 8/9/0xA/0xB/0xD) ──
	void TickMouse();
	void TickTooltip(int32 IdNum);     // `itemSlotShowTooltipById` (−1 — снять, `CControl_KillFocus`)
	int32 HoverIdNum() const;          // IDNum контрола под курсором (32004/32016/32010…/32050…), −1 — нет
	void LayoutLabel(UTextBlock* Tb);  // стиль подписи иконки: обводка 0x800000 (выравнивание 9 — уже в `.JUI`)

	// ═══ ВИДЖЕТЫ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle      = nullptr;   // 32003 `Disassembly_Title`
	UPROPERTY(Transient) TObjectPtr<UWidget>    DragHandle    = nullptr;   // 32003 `_DRAG` — ручка окна
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnStart      = nullptr;   // 32002 `Disassembly_Btn`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBtnStart   = nullptr;   // подпись 32002
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose      = nullptr;   // 32001 `Disassembly_ClsBtn`
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     PrevIcons;           // 32010…32015 (план)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PrevLabels;          // 32010…32015 (текст «+N%» / «N»)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    PrevNotice;          // 32020…32025 `Booster_Border` (анимация)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    PrevFailed;          // 32150…32155 (разобраны, кодом не включаются)
	UPROPERTY(Transient) TObjectPtr<UImage>     ResIcon       = nullptr;   // 32016 `Disassembly_RIcon06` (план)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResLabel      = nullptr;   // 32016 (текст количества)
	UPROPERTY(Transient) TObjectPtr<UWidget>    BestGroup     = nullptr;   // 32031 `Disassembly_BestResult` (группа блёсток)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaAnimatedPlane>> BestShines;  // 6 × `Effect_Shining` внутри 32031
	UPROPERTY(Transient) TObjectPtr<UWidget>    Failed06      = nullptr;   // 32156 (в коде эталона не встречается)
	UPROPERTY(Transient) TObjectPtr<UImage>     TargetIcon    = nullptr;   // 32004 `Disassembly_Target` (план 64×64)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TargetLabel   = nullptr;   // 32004 (текст «+N%»)
	UPROPERTY(Transient) TObjectPtr<UWidget>    TargetEff     = nullptr;   // 32005 `Disassembly_TargetEff` (жёлтое кольцо)
	UPROPERTY(Transient) TObjectPtr<UImage>     Gauge         = nullptr;   // 32030 `Disassembly_VProBar` («жар»)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     ListIcons;           // 32050…32079 (план)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ListLabels;          // 32050…32079 (текст «+N%»)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    ListDisable;         // 32100…32129 (разобраны, кодом не включаются)
	UPROPERTY(Transient) TObjectPtr<UWidget>    SelFrame      = nullptr;   // 32090 `Disassembly_Select` (рамка выбора)
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> ListGrid   = nullptr;   // иконки списка
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> PrevGrid   = nullptr;   // иконки превью
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> TargetGrid = nullptr;   // иконка 32004
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> ResGrid    = nullptr;   // иконка 32016
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;          // реле кнопок 32001/32002
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> LoopSound = nullptr;      // петля `S_e_disjoint_process` (nullptr при -nosound)
	bool bLoopActive = false;                         // петля «жара» логически играет (между запуском и `sub_9349C7`)
	FVector2D SelFrameBase = FVector2D::ZeroVector;   // позиция рамки 32090 в канвасе «как в .JUI» (над ячейкой 0)
	int32 FoundControls = 0;                          // разобрано контролов
	int32 TotalControls = 0;                          // ожидалось контролов

	// ═══ МОДЕЛЬ (поля объекта эталона) ═══
	bool bOpen = false;            // окно видимо
	bool bControlsReady = false;   // контролы разобраны
	int32 State = -1;              // [89]
	int32 SelCell = -1;            // [179] — ячейка сумки выбранного предмета (эталон держит указатель на запись)
	bool bResultShown = false;     // [180] ≠ 0 — 32016 рисует запись результата
	TArray<uint8> ResultRec;       // запись результата (57 б), на которую указывает [180]
	TArray<uint8> Preview[kPreviews];   // [93…178] — 6 записей превью по 57 б (код +0, процент +27)
	TArray<int32> List;            // [212] — ячейки сумки позиций списка (в порядке сумки)
	FAsdaBagListScroll BagScroll;  // ★ 29.10.2026: окно 30 видимых позиций над `List` (ряд прокрутки колесом)
	TArray<int32> ListIconCodes;   // код предмета позиции на момент построения (иконка — снимок, как у эталона)
	TArray<uint8> Pkt;             // [182…] — тело последнего 6503 (119 б); `Pkt[0] == 1` — результат не применён
	int32 SelSlot = -1;            // [215] — слот результата 1…6 (−1 — код результата не нашёлся в превью)
	float AnimTick = 0.f;          // [216]
	FVector2D GaugeBase = FVector2D::ZeroVector;   // [217]/[218] — позиция 32030 из ассета
	FVector2D GaugeSize = FVector2D::ZeroVector;   // [219]/[220] — размер 32030 из ассета
	bool bGaugeBaseTaken = false;  // `g_bUpgradeUiNeedReposition == 0` — база взята при первом открытии
	bool bInvWasOpen = false;      // [222] — сумка была открыта, когда окно открывали/закрывали тумблером
	double LastApplyTime = -1.0;   // [223] — момент последнего `applyResult` (сек), <0 — не было
	double AnimAccum = 0.0;        // ★ [ПОРТ] накопитель времени для кадров эталона (ключ `asda.Disassembly.AnimFps`)
	FAsdaReplyLatch ReplyLatch;    // ★ [ПОРТ] 6502 ушёл, 6503 ещё нет (момент отправки — `ArmedAt`). ★ S2 (В2-A дедупа №2, 11.10.2026): общая точка вместо `bInFlight + InFlightSince`
	bool bWasOverWindow = false;   // курсор был над окном в прошлом кадре — фронт msg 8 по 32000
	int32 TipId = -1;              // IDNum, чей тултип показан ([221] = TipId ≥ 0)
	FAsdaDblClick TargetDbl;       // двойной клик (0xD) по 32004 — ★ U1 (В2-A): общий распознаватель, порог Windows
public:
	// ★ U1 (В2-A дедупа №2): КОПИЯ распознавателя двойного щелчка с прежним порогом окна (`LegacySec`) — для таблицы
	//   порогов самотеста `asda.Shop.SelfTestDblClick` (раздел U1). Своё состояние пары окна не трогает.
	FAsdaDblClick DebugDblClickProbe() const;
private:
	FAsdaMouseFrame Mouse;         // опрос мыши кадра
	TAsdaNetQueue<TArray<uint8>> PendingResults;   // тела 6503, пришедшие из сети (разбор — в Tick)
	TArray<FAsdaDecompositiontableRow> Table;   // `DecompositionTable.BIN` в порядке кодов (плотно от первого)
	int32 TableFirstCode = 0;      // код первой записи (`**(this + 91)`)
	bool bTableDense = false;      // коды идут подряд (индекс = код − первый, как у эталона)
	TMap<int32, int32> TableIndex; // запасной поиск, если коды не подряд (данные порта иначе не бывают — проверка)
	bool bTableLoaded = false;     // таблица прочитана
	TArray<FString> Journal;       // журнал «событие → момент»
	double JournalStart = -1.0;    // начало отсчёта журнала
	FDelegateHandle HResult;       // подписка на 6503
};
