// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaExtractShopSubsystem — ОКНО «ПЕРЕДАЧА ПАРАМЕТРОВ» `EXTRACT_WIN` (JUI IDNum **16000**).
//   Открывается кнопкой диалога NPC **30035** (кузнец `136npc`, `alford`, `tid`).
//   Полный реверс потока с адресами — `Reports/npc_transfer_params_30035.md`.
//   Структурный разбор всех 227 контролов — `Docs/UI/Trade/EXTRACT_WIN.md`.
//
// ═══════════════════ ★★★ ЧТО ЭТО ЗА ОКНО НА САМОМ ДЕЛЕ ════════════════════════════════════════
//   Прежняя подпись порта — `Refuse("extract-shop")` — угадала лишь слово. Это **машина пересадки
//   ПЯТИ ПАР ЗАТОЧКИ/ЗАЧАРОВАНИЯ (поля записи +29…+48) с одного предмета снаряжения на другой**.
//   Класс эталона `CExtractShop` (0x1224), файл автора `interface extract.cpp`.
//     • ЛЕВАЯ (Front) половина — «Извлекаемый предмет» (нотис 13253): у него параметры ЗАБИРАЮТ,
//       и сам он ПОГИБАЕТ (нотис-гайд 99903 «Снаряжение, лишившееся параметров, уничтожается»).
//     • ПРАВАЯ (Back) половина — «Создаваемый предмет» (нотис 13254): ему параметры ОТДАЮТ,
//       и он становится ПРИВЯЗАННЫМ к владельцу (сервер ставит байт записи **+50 = 1**).
//     • Внизу — пять строк-правил 99900…99904, цена (13257), своё золото (12006), полоса
//       процесса и три кнопки: 13255 «Повторить», 13256 «Настройки передачи», 2289 «Отмена».
//   ⚠ Прежний документ `Docs/UI/Trade/EXTRACT_WIN.md` называл валюту «Honor-очками» — ЭТО
//   НЕВЕРНО. Подпись поля 16140 = нотис 12006 «Золото», рядом в `.JUI` значок `BoxIcon_Gold`,
//   отказ сервера кодом 7 = нотис 13807 «Недостаточно золота…», а `honorPoints_resetDisplayIfZero`
//   уже разоблачён заходом ремонта как ЧТЕНИЕ ЗОЛОТА. Валюта — ЗОЛОТО.
//
// ═══════════════════ ОТКУДА БЕРУТСЯ ОБЕ СЕТКИ ════════════════════════════════════════════════
//   ОБЕ половины наполняются из ОДНОГО источника — **контейнера 1 (сумка снаряжения, 30 ячеек)**:
//     `uiInventoryBuildSlotGrid@0xC91200` (Front) и `uiCompose_FillFilteredBagSlots@0xC91B90` (Back)
//     сканируют `g_InventoryBag` слот за слотом и УПЛОТНЯЮТ подходящие в свой массив 30×57 б.
//   Гейт вида одинаков у обеих: `sub_C90E30(itemType)` = `itemType <= 4 || == 8 || == 9`.
//   Сервер это подтверждает: `Recv_ShellItemOption_Extraction@0x513F80` отвергает пакет, если
//   контейнер источника или приёмника НЕ РАВЕН 1 (лог «Plate=%d PosX=%d»).
//   Показываются 12 ячеек (2 ряда × 6), колесо (16024/16074) прокручивает по ОДНОМУ РЯДУ:
//   `uiControlSetScrollRange(grid, 1, 4)` ⇒ четыре позиции, шаг 342 б = 6 записей, всего 5 рядов.
//
// ═══════════════════ КОДЫ СОСТОЯНИЯ ЯЧЕЕК (они же тексты строки 16145) ════════════════════════
//   Строку-подсказку 16145 печатает `uiSetStatusLabelText@0xC93600` ПО КОДУ состояния ячейки:
//     0 → 13260 «Нажми кнопку 'Повторить'.»          −1 → 13261 «Нет данных о предмете.»
//    −2 → 13262 «Это не предмет снаряжения.»          −3 → 13263 (в этом окне не выдаётся)
//    −4 → 13264 «Нельзя выбрать обычный предмет.»     −5 → 13265 «…без параметров.»
//    −6 → 13266 «…твой уровень слишком низок.»        −7 → 13267 «…не тот тип снаряжения.»
//    −8 → 13268 «…не та профессия.»                   −9 → 13269 «…извлекаемые предметы.»
//   −10 → 13270 «Выбери предмет, который будет извлечен.»   −11 → 13271 (не выдаётся)
//   −12 → 100103 «Это другой тип оружия.»
//   Front (`uiSlotGridValidateItems@0xC90FA0`) выдаёт −1/−2/−4/−5/1;
//   Back  (`itemCheckCompositionMatch@0xC916B0`) — −10 (пока источник не выбран), −1, −2, −6, −7,
//         −8, −9, −4, −12, 1. Ячейка со состоянием ≠ 1 закрыта чёрным оверлеем (16101…/16121…).
//
// ═══════════════════ ЦЕНА ════════════════════════════════════════════════════════════════════
//   `itemOpGraftLookupByRange@0xC93920` — линейный поиск по таблице `Itemopgraft.bin`
//   (20 строк по 12 б: `LevelLow`, `LevelHigh`, `Value`; в порту это `DT_itemopgraft`) по
//   **`ItemRecord->RequiredLevel` ПРЕДМЕТА-ИСТОЧНИКА**. Совпадений нет ⇒ 0.
//   Сервер берёт цену из СВОЕЙ копии той же таблицы и отказывает кодом 7 при `золото <= цена`
//   (именно `<=`, а не `<` — дефект оригинала №4, см. отчёт).
//
// ═══════════════════ ПАКЕТЫ ═══════════════════════════════════════════════════════════════════
//   C→S **5433** `OP_RECV_SHELL_ITEM_OPTION_EXTRACTION` (49 б = 8 токен + 16 MD5 + 25 тело),
//   собирает `itemExtract_sendCombineRequest@0xC921D0`. Тело 25 б:
//     @0  i16 слот источника (+5)      @2  i16 второй индекс источника (+7)
//     @4  u8  контейнер источника (+4) @5  i32 количество источника (+9)
//     @9  i16 вес стопки источника (+14)
//     @11…@21 — то же для приёмника    @22 i16 слот свитка-бустера   @24 u8 контейнер свитка
//   Свиток попадает в тело ТОЛЬКО если включён чекбокс 16412 И выбранная строка списка —
//   `special_itemlist.SubType == 96`; иначе @22 = 0xFFFF, @24 = 0xFF.
//   S→C **5434** `OP_EXTRACT_SHOP` (184 б), раскладка совпадает у клиента (`uiSlotContainer_Init`)
//   и у сервера (`sub_517320`):
//     @0 u8 код · @1 57 б запись ИСТОЧНИКА (он удалён) · @58 57 б запись ПРИЁМНИКА (новая)
//     @115 57 б запись СВИТКА после списания · @172 i16 вес · @174 i32 золото · @178 i32 (не читается)
//     @182 i16 userNum
//   Коды: **9** успех, **10** неудача броска, **7** мало золота, **0** общий отказ,
//   **8** ошибка БД, **11** свитка не хватило; клиент печатает 13800…13808 / 13809 / 13810.
//   ★ ШАНС УСПЕХА СЧИТАЕТ СЕРВЕР: `rand(0,10000) [+ бонус свитка] > 5000` ⇒ успех (базово 50 %).
//
// ═══════════════════ ЧТО ПРОИСХОДИТ НА ЭКРАНЕ ════════════════════════════════════════════════
//   Кнопка 16091 → проверки → подтверждение (99905 и/или 109900) → `exchangeItemShowBoostingUI`:
//   3D-звук `s_item_boosting02.wav`, полоса 16095 заполняется за **16 000 мс**
//   (`uiProgressBar_AnimStart(bar, 16000)`), пока она идёт — `uiListUpdateScroll@0xC90670`
//   каждый кадр двигает пять «святых» эффектов 16320…16324 по X на `доля × 445` пикселей
//   (константа `flt_13E9EC4` = 445.0, снята из бинаря). По достижении 100 % полоса шлёт событие
//   **24** (`CProgressBar_UpdateAnim@0x12E9400`), окно ловит его как команду 16095 и ОТПРАВЛЯЕТ
//   ПАКЕТ (`itemCombineDlgOnButton@0xC90530`).
//   Ответ 9: нотис 13809, эффект `Eff_0_char_99_c-suc-a_00.jeb` + звук `S_e_shovelling02.wav`,
//   тексты параметров ПЕРЕЕЗЖАЮТ с левой панели на правую, запускается анимация уничтожения
//   16146; её событие «доиграла» (то же 24) прибирает окно.
//   Ответ 10: нотис 13810, эффект `Eff_0_char_99_c-fail-a_00.jeb` + звук `S_e_shovelling03.wav`.
//
// ═══════════════════ НУМПАД ═══════════════════════════════════════════════════════════════════
//   Сверено с `Docs/Port/systems/NUMPAD_FLOW.md` и `PORT_PENDING_ON_SYSTEMS.md` (п. 48): окно количества
//   `NUMPAD_WIN` 1410 в этой ветке НЕ УЧАСТВУЕТ. В `CExtractShop` нет ни одного вызова
//   `uiNumPadOpen`/`CNumPad_*`, количество нигде не вводится: за раз переносится один комплект
//   параметров, свиток списывается ровно один (`UseAmount = 1` в `Recv_ShellItemOption_Extraction`).
//   Точка реестра «нумпад» для кода 30035 закрыта именно этим — за отсутствием предмета вызова.
//
// ═══════════════════ ДЕФЕКТЫ ОРИГИНАЛА (все с ключом отката) ═════════════════════════════════
//   №1 `asda.Extract.FixPageOnConfirm` — кнопка 16091 читает предмет-ИСТОЧНИК БЕЗ слагаемого
//      страницы (asm `mov ecx,[eax+0x2E8]; imul ecx,0x156; lea esi,[edx+ecx+0x2EC]` — терма
//      `342*page` нет), поэтому при прокрученном колесе проверки «есть самоцвет» и «привязан»
//      делаются по ЧУЖОМУ предмету с той же строки страницы 0. Сам пакет страницу учитывает.
//   №2 `asda.Extract.ClearStatusOnLeave` — уход курсора с ячейки строку 16145 не чистит
//      (`case 0xA` чистит её только по уходу с ЛЮБОГО контрола, а ячейки шлют 0xA не всегда).
//   Оба ключа по умолчанию **1** (исправлено), значение 0 возвращает поведение оригинала.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaExtractShopSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;

// Одна позиция уплотнённой сетки: какая ячейка контейнера 1 сюда попала.
USTRUCT()
struct FAsdaExtractEntry
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode  = 0;    // 0 ⇒ позиция пуста
	UPROPERTY(Transient) int32 CellIndex = -1;   // индекс ячейки в контейнере 1

	bool IsEmpty() const { return ItemCode <= 0 || CellIndex < 0; }
	void Clear() { ItemCode = 0; CellIndex = -1; }
};

// Виджеты одной ячейки-иконки сетки: план-иконка + чёрный оверлей «недоступно».
USTRUCT()
struct FAsdaExtractCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage> Icon    = nullptr;   // 16011…16022 / 16061…16072
	UPROPERTY(Transient) TObjectPtr<UWidget> Disable = nullptr;  // 16101…16112 / 16121…16132
};

// Одна строка параметра (пять на каждой половине): подложка + текст.
USTRUCT()
struct FAsdaExtractOptUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWidget>    Base = nullptr;  // 16005…16009 / 16220…16224
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text = nullptr;  // 16031…16035 / 16230…16234
};

UCLASS()
class ASDAREBORN_API UAsdaExtractShopSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaExtractShopSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна. Все из `.JUI` 16000 и `CExtractShop_InitUI@0xC8D040`, ни одно не выдумано ──
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон 30 (5 рядов × 6) — по ёмкости сумки; сумка 60 ⇒ 10 рядов × 6.
	static constexpr int32 kGridCells   = 60;   // столько записей держит каждая половина (= `UAsdaInventorySubsystem::kShellSlots`)
	static constexpr int32 kViewRows    = 2;    // видимых рядов
	static constexpr int32 kViewCols    = 6;
	static constexpr int32 kViewCells   = kViewRows * kViewCols;   // 12 ячеек на экране
	// ★ [ПОРТ] 29.10.2026: эталон 4 = 5 рядов − 2 видимых + 1 (`uiControlSetScrollRange(grid, 1, 4)`); при 10 рядах — 9.
	//   Фактический предел считается по ДОСТУПНОЙ ёмкости сумки (`GetShellCapacity`), это — наибольший.
	static constexpr int32 kPages       = kGridCells / kViewCols - kViewRows + 1;
	static constexpr int32 kOptRows     = 5;    // пять пар заточки записи (+29…+48)
	static constexpr int32 kScrollCells = 6;    // 16400…16405
	static constexpr int32 kCellPx      = 32;   // Size = "32 32" у всех ячеек
	static constexpr int32 kExtractZ    = 733;  // выше инвентаря 720, магазина 730, ремонта 731, лавки 732
	static constexpr float kBoostSec    = 16.f; // `uiProgressBar_AnimStart(bar, 16000)`
	static constexpr float kFxTravelPx  = 445.f;// `flt_13E9EC4` — путь эффектов 16320…16324 по X
	static constexpr int32 kScrollSubType = 96; // `special_itemlist.SubType` свитка-бустера

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ (эталон `extractShop_Open@0xC8E390` / `extractShop_Close@0xC8E600`) ═══
	UFUNCTION(BlueprintCallable, Category="Asda|Extract")
	void OpenWindow();

	UFUNCTION(BlueprintCallable, Category="Asda|Extract")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category="Asda|Extract")
	bool IsOpen() const { return bOpen; }
	// ★ 08.11.2026 (пачка 19): идёт фаза извлечения (+0x1064), 5433 в полёте (+0x106C) или применение ответа (+0x1068) —
	//   общая точка остановки хода `AsdaSelfMoveHeldForUiAction`.
	bool IsExtractInProgress() const { return bOpen && (bBusy || bPending || bApplying); }

	// ═══ ДЕЙСТВИЯ (в эталоне — обработчики контролов) ═══
	void OnCellClicked(int32 ViewCell, bool bBack);  // `itemDlg_OnSlotClick@0xC8EC40`
	void OnBigIconClicked(bool bBack);               // 16045 / 16088 — сброс выбора
	void OnWheelScroll(bool bBack, int32 Delta);     // `itemDlgSetSlotValue@0xC90240` (16024 / 16074)
	void OnRefreshClicked();                         // 16090 → сброс подбора
	void OnExtractClicked();                         // 16091 → проверки и запуск
	void OnCancelClicked();                          // 16092 → закрыть
	void OnScrollCellClicked(int32 Cell);            // 16400…16405 — выбрать/снять свиток
	void OnScrollCheckChanged(bool bChecked);        // 16412

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // обе сетки, состояния, выбор, цена, свиток, фаза

private:
	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();

	// ── Наполнение модели ──
	void RebuildFront();     // `uiInventoryBuildSlotGrid@0xC91200`
	void RebuildBack();      // `uiCompose_FillFilteredBagSlots@0xC91B90`
	void ValidateFront();    // `uiSlotGridValidateItems@0xC90FA0`
	void ValidateBack();     // `itemCheckCompositionMatch@0xC916B0`
	void RebuildAll();

	// ── Рендер ──
	void RefreshFrontGrid();  // `exchangeItemRefreshSlotGrids@0xC91380`
	void RefreshBackGrid();   // `uiRefreshCompositionSlots@0xC91D10`
	void RefreshFrontDetail(bool bHasSelection);   // `itemDlgFillSlotDetail@0xC8EF10`
	void RefreshBackDetail(bool bHasSelection);    // `itemInfoPanel_Refresh@0xC8F3F0`
	void RefreshGoldText();
	void RefreshScrollList();  // `petUiRefreshGrid@0xC93E90`
	void MoveFrontCursor();    // `uiSlotCursorUpdateHighlight@0xC8FA10`
	void MoveBackCursor();     // `itemDlgUpdateSelCursor_FBE0@0xC8FBE0`
	void SetStatusText(int32 StateCode);           // `uiSetStatusLabelText@0xC93600`
	void ClearSelection();     // `uiClearItemExchangeDialog@0xC904A0`

	// ── Данные ──
	const struct FAsdaItemInstance* CellItem(const FAsdaExtractEntry& E) const;
	bool  PassesTypeGate(int32 ItemCode) const;        // `sub_C90E30`: type<=4 || 8 || 9
	bool  PassesWeaponGate(int32 ItemCode) const;      // `sub_C90E90`: type<=4 || 8
	static int32 GradeBand(int32 UsageCategory);       // `itemGradeBandFromTypeIndex@0xEC97A0`
	int32 CostForLevel(int32 RequiredLevel) const;     // `itemOpGraftLookupByRange@0xC93920`
	// Абсолютный индекс записи по (страница, ряд, колонка) — шаг страницы РОВНО один ряд.
	static int32 AbsIndex(int32 Page, int32 Row, int32 Col) { return 6 * (Page + Row) + Col; }
	const FAsdaExtractEntry* SelectedFront() const;
	const FAsdaExtractEntry* SelectedBack() const;

	// ── Фаза «машина работает» ──
	void StartBoost();                 // `exchangeItemShowBoostingUI@0xC909B0`
	void TickBoost(float DeltaSeconds);// `uiListUpdateScroll@0xC90670` + `CProgressBar_UpdateAnim`
	void StopBoostVisuals();           // `uiQuestPanelResetControls@0xC90C70`
	void FinishDestructFx();           // ветка 16146 в `itemCombineDlgOnButton@0xC90530`

	// ── Подтверждение и сеть ──
	void AskGemConfirm();              // нотис 99905  (msgClass 1, код 0x18641)
	void AskBindConfirm();             // нотис 109900 (msgClass 1, код 0x1AD4C)
	void CheckGoldAndBoost();          // `exchangeOpenBoostingOrWarn@0xC93D80`
	void SendExtract();                // `itemExtract_sendCombineRequest@0xC921D0`
	void OnExtractResult(const TArray<uint8>& Body);   // `Recv_ExtractShop@0xC92AD0`
	void DrainPendingPackets();

	// ── Покадровые опросы ──
	void TickCells();
	// Колесо мыши над окном — обработчик оконной подсистемы (эталонные «колёсные окна» 16024/16074).
	bool OnWheel(int32 Notches);

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	void NoticeMsgBox(int32 LocId) const;    // ONEBUT_MSGBOX 1500

	// ── Мелочи показа/скрытия анимированных планов (эталон CControl_Show / CControl_CloseHide) ──
	static void PlaneShow(UWidget* W);
	static void PlaneHide(UWidget* W);

	// ═══ СОСТОЯНИЕ: ВИДЖЕТЫ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;

	UPROPERTY(Transient) TArray<FAsdaExtractCellUi> FrontUi;   // 12
	UPROPERTY(Transient) TArray<FAsdaExtractCellUi> BackUi;    // 12
	UPROPERTY(Transient) TArray<FAsdaExtractOptUi>  FrontOpt;  // 5
	UPROPERTY(Transient) TArray<FAsdaExtractOptUi>  BackOpt;   // 5

	UPROPERTY(Transient) TObjectPtr<UImage>     FrontBigIcon = nullptr;   // 16045
	UPROPERTY(Transient) TObjectPtr<UImage>     BackBigIcon  = nullptr;   // 16088
	UPROPERTY(Transient) TObjectPtr<UWidget>    FrontCursor  = nullptr;   // 16023
	UPROPERTY(Transient) TObjectPtr<UWidget>    BackCursor   = nullptr;   // 16073
	UPROPERTY(Transient) TObjectPtr<UWidget>    FrontWheel   = nullptr;   // 16024
	UPROPERTY(Transient) TObjectPtr<UWidget>    BackWheel    = nullptr;   // 16074

	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtFrontTitle = nullptr;  // 16030 (13253)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBackTitle  = nullptr;  // 16080 (13254)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCostLabel  = nullptr;  // 16093 (13257)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCostValue  = nullptr;  // 16094
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGoldLabel  = nullptr;  // 16140 (12006)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGoldValue  = nullptr;  // 16141
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtStatus     = nullptr;  // 16145
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TxtGuide;         // 16380…16384 (99900…99904)

	UPROPERTY(Transient) TObjectPtr<UButton>    BtnRefresh = nullptr;     // 16090 (13255)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnExtract = nullptr;     // 16091 (13256)
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel  = nullptr;     // 16092 (2289)

	UPROPERTY(Transient) TObjectPtr<UWidget>    Gauge     = nullptr;      // 16095, план-ЗАЛИВКА (ES1)
	UPROPERTY(Transient) TObjectPtr<UWidget>    GaugeBack = nullptr;      // 16095, план-подложка (ES0)
	// Исходная геометрия заливки: по `.JUI` 348×4. Ширина клипается долей (закон
	//   `ui-progressbar-animate-existing`: анимируем САМУ полосу скина, а не свою обёртку).
	FVector2D GaugeHome = FVector2D::ZeroVector;
	float     GaugeFullW = 0.f;

	// Антураж машины: две трубы, двое часов, по пять ламп и лампочек на половину.
	UPROPERTY(Transient) TObjectPtr<UWidget> FrontChimney = nullptr;      // 16002
	UPROPERTY(Transient) TObjectPtr<UWidget> FrontClock   = nullptr;      // 16001
	UPROPERTY(Transient) TObjectPtr<UWidget> BackChimney  = nullptr;      // 16050
	UPROPERTY(Transient) TObjectPtr<UWidget> BackClock    = nullptr;      // 16051
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> FrontLamps;          // 16200…16204
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BackLamps;           // 16210…16214
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> FrontBulbs;          // 16300…16304
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BackBulbs;           // 16350…16354
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> HolyFx;              // 16320…16324
	UPROPERTY(Transient) TArray<FVector2D>           HolyFxHome;          // их исходные X/Y
	UPROPERTY(Transient) TObjectPtr<UWidget> BurnIcon    = nullptr;       // 16040
	UPROPERTY(Transient) TObjectPtr<UWidget> BackIconFx  = nullptr;       // 16087
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> DestructFx = nullptr;  // 16146

	// Под-панель свитка-бустера.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     ScrollIcon;       // 16400…16405 (план)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ScrollCount;      // 16400…16405 (подпись)
	UPROPERTY(Transient) TObjectPtr<UWidget>    ScrollCursor = nullptr;   // 16410
	UPROPERTY(Transient) TObjectPtr<UCheckBox>  ScrollCheck  = nullptr;   // 16412
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtScrollTitle = nullptr; // 16411 (100200)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtScrollText  = nullptr; // 16413 (100201)

	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridFront  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridBack   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridScroll = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	// Реле состояний подписей кнопок — их держит общая точка `AsdaUi::BindButtonCaptionStates`.
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> CaptionRelays;

	// ═══ СОСТОЯНИЕ: МОДЕЛЬ ═══
	bool bOpen = false;           // `m_bOpen`   (+0x1060)
	bool bBusy = false;           // `m_bBusy`   (+0x1064) — идёт фаза машины
	bool bApplying = false;       // `m_dwField1050` (+0x1068) — применяем ответ, клики закрыты
	bool bPending = false;        // `m_dwField1051` (+0x106C) — 5433 в полёте
	bool bControlsReady = false;
	bool bDirty = false;          // инвентарь изменился — пересобрать сетки

	UPROPERTY(Transient) TArray<FAsdaExtractEntry> Front;   // kGridCells = 60 (★ 29.10.2026, инвентарь ×2; эталон 30)
	UPROPERTY(Transient) TArray<FAsdaExtractEntry> Back;    // kGridCells = 60 (★ 29.10.2026, инвентарь ×2; эталон 30)
	TArray<int32> FrontState;     // 12 — коды видимых ячеек
	TArray<int32> BackState;      // 12

	int32 FrontPage = 0;          // `m_pHelpField184` (dword 184)
	int32 BackPage  = 0;          // dword 617
	int32 SelFrontRow = -1;       // `m_nSelOptRow` (dword 186) — ряд В ВИДИМОМ ОКНЕ на миг клика
	int32 SelFrontCol = -1;       // `m_nSelOptCol` (dword 185)
	int32 SelFrontPage = 0;       // dword 183 — страница, зафиксированная кликом
	int32 SelBackRow = -1;        // dword 619
	int32 SelBackCol = -1;        // dword 618
	int32 SelBackPage = 0;        // dword 616
	int32 SelCost = 0;            // напечатанное в 16094

	// Свиток-бустер: индексы ячеек контейнера 1 с `SubType == 96`.
	TArray<int32> ScrollCells;
	int32 SelScrollCell = -1;     // `m_dwField1125` (+0x1194); −1 = не выбран

	double BoostStartS = 0.0;     // отсчёт полосы 16095
	int32  TipCell = -1;          // ячейка, по которой показан тултип
	bool   bTipBack = false;
	bool   bConfirmOpen = false;
	// Запись приёмника, пришедшая в ответе, — эталон держит её в буфере +0x11E4 и показывает
	//   в правой инфо-панели, когда ячейка приёмника уже опустела.
	TArray<uint8> ResultDstRecord;

	FAsdaMouseFrame Mouse;
	TAsdaNetQueue<TArray<uint8>> PendingResults;   // закон `no-reentrant-net-callbacks`
	FDelegateHandle HExtractResult;
	FDelegateHandle HInvCell;
};
