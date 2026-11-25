// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPremiumGachaSubsystem — ПРЕМИУМ-ГАЧА (лутбокс за премиум-валюту), сцена `CGachaScene`.
//
// ═══════════════════ КЛАССИФИКАЦИЯ (закон `classify-before-building`) ═══════════════════
//   НОВАЯ СИСТЕМА, а не доделка. ★★★ ЭТО НЕ «БУСТЕР». Обычный бустер (окно 10050
//   `BOOSTING_WIN`, барабан 10068, опкоды 5468/5469, платится предметом из сумки) реализован
//   отдельно в `UAsdaBoosterPanelSubsystem` и к премиум-гаче отношения не имеет: доказано пятью
//   независимыми признаками (Reports/gacha_net_re.md §4.1) — разные окна, разные опкоды,
//   разная валюта, разные таблицы, ноль общего кода. Совпадает только слово «гача» в именах.
//
// ═══════════════════ ЧТО ЭТО ЗА ФИЧА ═══════════════════
//   Вход: кнопка HUD **73410** `NEW_Primiumpick` в окне `SYSTEMMAIN_NEW_WIN` 73400, подсказка
//   loc 9801 «Лотобокс». Маршрут доказан: `uiHotkey_DispatchCommand@0xE8F670` case 73410 →
//   `premiumGacha_BeginRoll@0xB6DF80`; отжатие → `uiGuildPanelClose@0xB6E920`.
//   Сцена состоит из СЕМИ окон JUI:
//     68000 `Premium_PickBAR_Win`        — корень сцены (25 именованных контролов)
//     68075 `Premium_PickBAR_daybtn_X_B` — подложка кнопки выхода (сама кнопка 68076)
//     68100 `Premium_Pick_select`        — диалог «Играть / Отмена» + цены (68103/68105/68107)
//     68200 `Premium_Pick_Click_Win`     — итог/клик + «пополнить»
//     68300 `Premium_Pick_View_Win`      — сетка призов 5×5 (68311…68335) + скролл
//     68450 `Premium_Pick_mssageBOX`     — мессадж-бокс приза
//     68500 `Premium_Pick_select_2`      — выбор варианта приза (профессия + уровень)
//
// ═══════════════════ ВИЗУАЛ ═══════════════════
//   ★ «Барабан» здесь — НЕ UI-контрол, а 3D-модель: `md_gachya_{a..e}_00` (20 мешей, кости 33→20)
//   с клипами в слотах, отрисованная в render-target (общий с окном питомца `PetWindow2`).
//   Клипы и ЗАМЕРЕННЫЕ длительности (совпали с реверсом):
//     anim_01 = 2.000 с покой · anim_02 = 2.000 с прокрутка · anim_03 = 2.333 с выдача
//     anim_04 = 2.333 с вскрытие шара по клику · anim_00 = 2.333 с (в оригинале НЕ загружается —
//     дефект оригинала: `md_gachya_%c_00.jbb` не читается никогда).
//   Кроссфейд между клипами в оригинале недостижим (флаг +464 нигде не ставится в 1) ⇒ склейка жёсткая.
//   ★ Эффектов `.JEB/.JEM` у премиум-гачи НЕТ (проверено сплошным сканом 159 функций
//     0xB6A000…0xB74000). Единственная «вспышка» — кадровый анимплан 68058 `Apple_Make3`
//     (16 кадров / 1000 мс). Конфетти `eff_lucky_a_01.jeb` принадлежит БУСТЕРУ, не сюда.
//   ★ Звуков нет и в самом оригинале: 4 имени `S_e_pr_gc_{idle,rol,click,open}_00.wav` есть в exe,
//     а файлов нет ни в `Data/Sound` (1483 шт.), ни в `SFT.BIN`, ни в 16 архивах, ни в 4 сборках.
//     По решению юзера подбираются близкие из имеющихся — это ОТСТУПЛЕНИЕ от оригинала (помечено).
//
// ═══════════════════ СЕТЬ ═══════════════════
//   5466 → 5467  запрос/ответ премиум-баланса (баланс → `CCharacter+0x44C`)
//   6577 → 6578  крутить / результат прокрута (приз 56 б + новый баланс)
//   6581 → 6582  забрать приз / приз зачислен
//   Приз выбирает ВНЕШНИЙ агент GP7 (порт 15022, команда 0x50 subtype 2 «LuckyBox Item Buy
//   Request»), а не клиент и не GameServer. Серверная сторона включена нашими патчами P05/P06
//   (в стоке `case 105` был недостижим — родной код разработчиков не был заведён).
//
// ═══════════════════ ГЕЙТ ═══════════════════
//   `sub_922D5D@0xB6F370` = `init && открыто`. Пока сцена открыта, оригинал блокирует движение,
//   ввод, рендер мира и своего персонажа, применение предметов, обмен, соулмейт, клик по тайлу
//   (8 точек проверки). Здесь это `IsSceneOpen()`, который опрашивают те же места порта.
#pragma once
#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaScrollBarNomal.h"   // FAsdaScrollBarNomal — общая полоса прокрутки скина `Nomal_Scr`
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Engine/DataTable.h"      // FTableRowBase / UDataTable
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaPremiumGachaSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UTextBlock;
class UAsdaButtonRelay;
class UButton;
class UDataTable;
class UImage;
class UComboBoxString;
class UAsdaIconCellGrid;
class UAudioComponent;
class AAsdaGachaSceneActor;

/** Фаза сцены. Порядок и длительности — из `gachaScene_UpdateChildAnims@0xB6B0B0`. */
UENUM(BlueprintType)
enum class EAsdaGachaPhase : uint8
{
	Closed      UMETA(DisplayName = "Закрыта"),
	Idle        UMETA(DisplayName = "Покой"),          // клип 1, 2.000 с, зациклен
	Rolling     UMETA(DisplayName = "Прокрутка"),      // клип 2, 2.000 с — ждём 6578
	Dispensing  UMETA(DisplayName = "Выдача"),         // клип 3, 2.333 с
	AwaitClick  UMETA(DisplayName = "Ждём клик"),      // шар 68057 виден, игрок кликает
	Opening     UMETA(DisplayName = "Вскрытие"),       // клип 4, 2.333 с
	Prize       UMETA(DisplayName = "Показ приза")     // мессадж-бокс 68450
};

UCLASS()
class ASDAREBORN_API UAsdaPremiumGachaSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPremiumGachaSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** Кнопка 73410: открыть сцену (порт `premiumGacha_BeginRoll@0xB6DF80`). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") bool OpenScene();
	/** Отжатие кнопки / выход 68076 (порт `uiGuildPanelClose@0xB6E920`). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void CloseScene();
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void ToggleScene();
	/** ★ 07.11.2026: закрытие по действию игрока (кнопки, меню): пока приз не забран — отказ с модальным окном. `CloseScene` остаётся вынужденным путём. */
	void RequestCloseScene();
	/** ★ 07.11.2026: приз не забран — ждём 6578 (не дольше 15 с), идёт розыгрыш, шар, вскрытие или показано окно приза 68450. */
	bool IsPrizePending() const;
	/** ★ 10.10.2026 [ОРИГИНАЛ] Роспись ESC окна 68000 (`RegisterEscCloser`; признак `CControl+0x228` ставит
	 *  `premiumGatya_InitWindow@0xB6CB50`, world_map.c:4561). Эталон `GachaScene_OnControlMessage@0xB704C0`, событие 21
	 *  (ui_core.c:7664-7670): `if (sub_92839D(gap_254[52]) == 1) { uiGuildPanelClose; inventoryBag_Close; }` — сцена
	 *  закрывается ТОЛЬКО в клипе 1 «покой» выбранного автомата; в прокрутке/выдаче/показе приза ESC съедается молча. */
	void EscClose();

	/** ★ Гейт оригинала `sub_922D5D`: пока истинно — мир и ввод заблокированы. */
	UFUNCTION(BlueprintPure, Category="Asda|Премиум-гача") bool IsSceneOpen() const { return bSceneOpen; }
	UFUNCTION(BlueprintPure, Category="Asda|Премиум-гача") EAsdaGachaPhase GetPhase() const { return Phase; }
	UFUNCTION(BlueprintPure, Category="Asda|Премиум-гача") int32 GetBalance() const { return Balance; }
	UFUNCTION(BlueprintPure, Category="Asda|Премиум-гача") int32 GetSelectedSlot() const { return SelectedSlot; }

	/** Выбрать автомат 0…4 (поле `CGachaScene` dword 162). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void SelectSlot(int32 Slot);
	/** Кнопка «крутить»: показать диалог подтверждения 68100 с ценой (`uiShowMoneyConfirmDialog@0xB6F130`). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void RequestRoll();
	/** Кнопка «Играть» в 68100 — отправляет 6577. */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void ConfirmRoll();
	/** Кнопка «Отмена» в 68100. */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void CancelRoll();
	/** Клик по «золотому шару» 68057 — запускает клип вскрытия. */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void ClickBall();
	/** Забрать приз (6581). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void TakePrize();

	/** ★★★ МОСТ «11007 → ГАЧА» — ЗАКРЫТИЕ ШВА ОРИГИНАЛА (`Reports/elite_mailbox_re.md` §7.3).
	 *  КТО ЗОВЁТ: `UAsdaPremiumPostSubsystem`, когда пакет 11007 (забор заказа из элитного ящика)
	 *  пришёл с `BuyType == 3` (приз гачи) и запись прошла фильтр эталона
	 *  (`inventorySyncApplyPacket@0xEF1320`, ветка `j == 0`).
	 *
	 *  ЧТО ЧИНИТ. Стоковый клиент на этом маршруте открывает окно результата
	 *  (`uiItemFilterListPopulate` + `uiItemInfoPopupOpen(g_pGachaScene)`), но КОНТЕЙНЕР и СЛОТ
	 *  приза в состояние сцены НЕ кладёт: `sowelWndSendSocketRequest@0xB71D70` (тело 6581) читает
	 *  их из `CGachaScene + 724/725`, а туда пишет ТОЛЬКО обработчик 6578. Итог оригинала —
	 *  окно, открытое из 11007, работает как витрина, а обмен варианта из него уходит на сервер
	 *  с контейнером 0 и слотом −1 и получает 6582 со статусом 10.
	 *  Здесь мы кладём в состояние сцены ЗАПИСЬ ИЗ 11007 целиком (её +4 контейнер, +5 слот) —
	 *  ровно то, чего оригиналу не хватало, — и открываем окно 68500.
	 *
	 *  ⚠ Пока этот мост не работал, единственной причиной держать серверный патч **P16**
	 *  (авто-выдача приза гачи в обход ящика) была именно эта дыра. Инструкция по выключению
	 *  P16 — в отчёте `Reports/elite_mailbox_impl.md`; сам патч этим заходом НЕ трогается.
	 *  @param Rec57 — 57-байтовая запись предмета из 11007 (первая непустая).
	 *  @return true, если окно результата показано. */
	bool OnPrizeTakenFromPost(const TArray<uint8>& Rec57);

	/** ★ Полный разбор ОКНА 68500 для живой проверки (консоль `asda.Gacha.VarDump`): видимость,
	 *  z-порядок относительно ящика 69000, ручка перетаскивания, состав ОБОИХ списков и подписи.
	 *  Нужен потому, что офлайн-дамп .uasset последнего шага (наполнение списков кодом) не видит. */
	FString DumpVariantWindow() const;

	/** ★ Живая проверка списков 68500 без мыши (в headless-PIE курсором не кликнуть):
	 *  раскрыть/свернуть список и выбрать пункт ТЕМ ЖЕ кодом, которым это делает клик.
	 *  @param bJob — true: список профессий 68507, false: список уровней 68508. */
	FString SimulateVariantCombo(bool bJob, bool bOpen, int32 PickIdx);

	/** Клик по кнопке HUD (подписка на UAsdaSystemMenuSubsystem::OnMenuCommand). */
	UFUNCTION() void OnMenuButton(int32 IdNum, int32 TargetWindowId, bool bChecked);

	/** Позиции витрины призов (сетка 68300) — коды предметов из DT_premium_gatya. */
	UFUNCTION(BlueprintPure, Category="Asda|Премиум-гача") const TArray<int32>& GetPrizes() const { return Prizes; }

	/** Цена выбранного слота из `premium_gatya_data.bin` (`list_GetSelectedEntryCode@0xB6F2C0`). */
	UFUNCTION(BlueprintPure, Category="Asda|Премиум-гача") int32 GetSlotPrice(int32 Slot) const;

	// ── Окно 68300 `Premium_Pick_View_Win` — витрина призов 5×5 ──────────────────────────────────
	/** Кнопки 68011+10i («поставить»): открыть витрину призов слота i (`openItemGridIfReady`). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void OpenPrizeGrid(int32 Slot);
	/** Крестик 68359 и кнопка «Закрыть» 68357 (`sub_91F36A@0xB6EE40`). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void CloseGridWindow();
	/** Прокрутка витрины: страница = 5 записей (`v6 = 5·scrollPage`), поле `CGachaScene` dword 179. */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void SetGridScrollRow(int32 Row);
	UFUNCTION(BlueprintPure, Category="Asda|Премиум-гача") int32 GetGridScrollRow() const { return GridScrollRow; }

	// ── Окно 68200 `Premium_Pick_Click_Win` — итог/баланс + «пополнить» ──────────────────────────
	/** Кнопка 68206: повторный запрос баланса (`net_SendVerifiedRequest5466`). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void RefreshBalanceButton();
	/** Кнопка 68205: страница пополнения (`UI_OpenDonatePage(g_pShopWnd)`). */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void OpenDonatePage();

	// ── Окно 68500 `Premium_Pick_select_2` — выбор варианта приза ────────────────────────────────
	/** Кнопка 68511 «ОК»: резолв варианта (`itemFindVariantBySlotOption@0xB71840`) и отправка 6581. */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void ConfirmVariant();
	/** Крестик 68519: закрыть окно выбора варианта. */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void CloseVariantWindow();
	/** ★ Сообщить элитному ящику, чем кончилась история с выбором варианта (нужно только его
	 *  отступлению «не показывать предмет до выбора», ключ `asda.Post.HideUntilChoice`).
	 *  @param bSwapApplied true — сервер провёл обмен и выбранный вариант уже положен. */
	void NotifyPostVariantFinished(bool bSwapApplied);
	/** Закрыть окно 68500 БЕЗ выбора (крестик 68519 и консоль `asda.Gacha.VarClose`) — одна точка. */
	UFUNCTION(BlueprintCallable, Category="Asda|Премиум-гача") void AbortVariantChoice();

	// ── [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор ДО получения» (ключ `asda.Post.ChooseBeforeTake`, разбор у `OpenPreTakeChoice`) ──
	/** Есть ли у предмета из ящика выбор «профессия × уровень» (обёртка `HasVariantChoice` для элитного ящика). */
	bool PostItemHasVariantChoice(int32 BaseCode) const;
	/** Открыть 68500 ДО отправки 11006: заказ ещё в ящике; ОК вернёт ящику вариант, крестик — отмену. */
	bool OpenPreTakeChoice(int32 BaseCode);
	/** Открыто ли сейчас окно 68500 в режиме «выбор до получения». */
	bool IsPreTakeChoiceOpen() const;
	/** Снять режим «выбор до получения» и скрыть окно, ящику НЕ сообщая (его зовёт сам ящик). */
	void CancelPreTakeChoice();
	/** Запомнить выбранный вариант: когда 11007 положит этот код, мост сам отправит 6581. */
	void ArmPostVariant(int32 BaseCode, int32 Variant);
	/** Забыть заряженный вариант (11007 пришёл, но мост не позвали). */
	void DisarmPostVariant();
	/** Ушёл ли 6581 по заряженному варианту и ответ 6582 ещё не пришёл. */
	bool IsPostSwapPending() const { return bPostSwapPending; }
	/** Код варианта последнего 6581, отправленного по заряженному выбору (самотест). */
	int32 GetLastPostSwapVariant() const { return LastPostSwapVariant; }
	/** Какой вариант даст ОК при текущем выборе в списках окна 68500 (−1 — такого нет; самотест). */
	int32 ResolveCurrentVariant() const { return ResolveVariant(VariantBaseCode); }

	/** ★ Эмуляция ответа сервера 6582 тем же путём, что сетевой колбэк (`OnPrizeApplied` →
	 *  отложенное применение в `Tick`). Живая проверка того, что обмен варианта применяется и при
	 *  ЗАКРЫТОЙ сцене гачи — маршрут «из элитного ящика». Команда `asda.Gacha.SimPrizeApply`. */
	void SimulatePrizeApply(int32 VariantCode, int32 ContainerType, int32 SlotIndex, int32 Amount);

	/** ★ 10.10.2026 (barber_minors): эмуляция ответа сервера 6578 (результат прокрута) тем же путём, что сетевой колбэк
	 *  (`OnRollResult`). Тело — настоящее, 63 байта: @0 статус, @1 запись приза 56 б (код предмета @1), @59 баланс (текущий —
	 *  чтобы офлайн-проверка его не меняла). Статус 1 — автомат уходит в клип прокрутки (`Rolling`). Нужна самотесту
	 *  `asda.UI.SelfTestEscOnTop` (ветка эталона «ESC при автомате не в покое съедается»). Только офлайн. */
	void SimulateRollResult(uint8 Status, int32 PrizeCode);

	/**
	 * ★★★ ДАМП ЦЕПОЧКИ ВИДЖЕТОВ ПОД КИСТЬЮ 3D-СЦЕНЫ (контрол 68056 и все его родители).
	 * ЗАЧЕМ: Slate умножает цвет материала на ВЕРШИННЫЙ ЦВЕТ элемента
	 * (`SlateElementPixelShader.usf:318` — `FinalColor = BaseColor * InVertex.Color`), а тот
	 * собирается из тинта браша, `ColorAndOpacity` самого `UImage` и `RenderOpacity`
	 * ВСЕХ родителей. Любая альфа < 1 или небелый тинт на этой цепочке выцветит
	 * картинку ровно так, как жалуется владелец, и НИ ОДИН оффлайн-зонд этого не видит.
	 * Вызывается командой `asda.Gacha.BrushDump`.
	 */
	FString DumpSceneBrushChain() const;

	/**
	 * ★★★ ДАМП ПЯТИ ПОЛОС СЛОТОВ 68010+10i НА ЖИВОМ КАДРЕ (команда `asda.Gacha.BarsDump`).
	 * ЗАЧЕМ: проверка «имя виджета есть в WBP» НЕ отличает живой контрол от свёрнутого — ровно
	 * на этом прокололась приёмка пяти автоматов (полосы 3…5 оказались пустыми плашками, хотя
	 * все имена в дампе присутствовали). Здесь для каждой полосы печатается ЭФФЕКТИВНАЯ
	 * видимость (с проходом вверх по родителям — у свёрнутого родителя ребёнок не рисуется и
	 * не ловит мышь), UV-регион плана-ЦИФРЫ (номер автомата — кусок атласа `NE_PvP2`, а не текст)
	 * и наличие обеих кнопок: витрины 68011+10i и «крутить» 68061+i.
	 */
	FString DumpSlotBars() const;

	/**
	 * ★★★ ПРОВЕРКА ОБЕИХ КНОПОК У ВСЕХ ПЯТИ ПОЛОС НАЖАТИЕМ (команда `asda.Gacha.BarsClick`).
	 * «Виджет виден» ещё не значит «за ним рабочий обработчик своего слота», поэтому у каждой
	 * полосы дёргается `OnClicked` — тот же мультикаст, что вызывает мышь. Сеть не трогается:
	 * «крутить» доводится только до диалога 68100 и тут же отменяется.
	 */
	void ClickAllSlotButtons();

	/** ★ 05.11.2026: кнопка «Играть» 68061+i автомата `Slot` (0…4) — порт `uiTradeConfirmMoney@0xB70420` с гейтом
	 *  `GachaScene_OnControlMessage`: выбор меняется и диалог оплаты открывается, только если 6577 не в полёте и все
	 *  автоматы в покое (ключ `asda.Gacha.SlotSwitchGate`). Сюда приходит щелчок мыши по кнопке. */
	void OnSpinButton(int32 Slot);

	/** ★ 05.11.2026 (самотест `asda.SelfTest.GachaOverlap`): нажать НАСТОЯЩУЮ кнопку полосы автомата `Slot` — «Играть»
	 *  (`bSpin`) или «Основные предметы» — тем же мультикастом `OnClicked`, что вызывает мышь. false — кнопки нет. */
	bool PressSlotButton(int32 Slot, bool bSpin);
	/** Нажать «Играть» 68108 диалога оплаты 68100 (тот же мультикаст `OnClicked`). false — кнопки нет. */
	bool PressConfirmPlay();
	/** ★ 07.11.2026 (самотест): нажать настоящую кнопку закрытия — 68076 (`bExitBar`) или крестик 68209. false — кнопки нет. */
	bool PressCloseButton(bool bExitBar);
	/** Запрос 6577 отправлен и ответа 6578 ещё нет (эталонный антидубль `CGachaScene+644`). */
	bool IsRequestInFlight() const { return bRequestInFlight; }
	/** Показан ли диалог оплаты 68100 / витрина 68300. */
	bool IsConfirmDialogShown() const;
	bool IsPrizeGridShown() const;
	/** Код предмета в записи последнего приза (`CGachaScene+720`), −1 — записи нет. */
	int32 GetPrizeItemCode() const { return PrizeItemCode; }
	/** Номер клипа, который сейчас играет автомат `Slot` в 3D-сцене (−1 — сцены/автомата нет). */
	int32 GetMachineClip(int32 Slot) const;

private:
	// ── окна ──
	bool EnsureWindows();
	void SetupControls();
	void SetVis(UWidget* W, bool bVisible);
	void ShowConfirmDialog();          // 68100 + заливка 68103/68105/68107
	void HideConfirmDialog();          // `sub_92387F@0xB6F320`
	void HideGridWindow();             // `sub_91F36A@0xB6EE40`
	void ShowPrizeBox();               // 68450
	/** Порт `sub_92373F` → `sub_B70B80@0xB70B80` = `CControl_SetVisible(dword 133 = ctrl 68450, 0)`.
	 *  Тезис доказан по бинарю: тонк `0x92373F` — это `jmp 0xB70B80`, а тело `sub_B70B80`
	 *  (ui_core.c:125096-125099) состоит ровно из одной строки `SetVisible(*(this + 133), 0)`;
	 *  dword 133 = `gap_10[516]` = контрол **68450** (`Reports/gacha_ui_re.md` §2.0).
	 *  ★ Эталон зовёт его В ОБЕИХ ветках клика по кнопке 68452 (ui_core.c:7650 и внутри
	 *  `uiItemInfoPopupOpen@0xB732F0`, ui_core.c:7977) — то есть мессадж-бокс закрывается ВСЕГДА. */
	void HidePrizeBox();               // `sub_92373F` = SetVisible(68450, 0)
	void PopulatePrizeGrid();          // 68300: 25 ячеек 68311…68335 из DT_premium_gatya
	void EnsureSceneActor();           // 3D-автоматы в render-target (порт uiBuildPortraitCameraMatrices)
	void LayoutByProjection();         // покадровая раскладка полос/шара (порт deathRespawnUI_LayoutResurrectPanel@0xB6E480)
	void ApplyFullScreenPanels();      // ШАГИ 11 и 13 premiumGacha_BeginRoll: 68059 и 68056 = размер бэкбуфера
	void ApplySceneBrush();            // кисть контейнера 68056: материал сцены поверх ЧИСТОГО браша

	/** ★★★ ПОРТ ПАРЫ `UI_FormatLocalizedNotice(g_pLocNoticeCtx, id, 0, 1)` + `CControl_SetText` /
	 *  `CWidget_SetTextAll4` — единственный способ, которым сцена гачи раздаёт СТАТИЧЕСКИЕ подписи
	 *  (`premiumGatya_InitWindow@0xB6CB50`, world_map.c:4563-4667; 17 вызовов).
	 *  ЗАЧЕМ ОТДЕЛЬНАЯ ТОЧКА. Импортёр JUI кладёт в WBP ПУСТОЙ `UTextBlock` для каждого
	 *  Text-контрола (`asda_jui_generic_umg.py:429` — «даже пустой: C++ ставит текст в рантайме»),
	 *  потому что корейский `EXS TextContent` в `gamemain_jui.ini` — дизайн-заглушка, а не текст
	 *  игры. Если код молчит, игрок видит ПУСТОЕ МЕСТО, и в логе при этом ни одной ошибки.
	 *  Ровно этот дефект был у 68201/68203 (`Reports/gacha_ui_fix2.md` §0 п.4) и у всего
	 *  окна 68100 (`Reports/gacha_modal_texts.md`).
	 *  У кнопок подпись — отдельный виджет `{idc}_Text` (или `{скин}_{idx}_txt`, если `ID` пуст),
	 *  и там лежит СТАТИЧЕСКИ ПЕРЕВЕДЁННАЯ заглушка из `CaptionText`; эталон её перезаписывает,
	 *  поэтому имя такого виджета передаём сюда же.
	 *  @return true, если виджет найден И строка непустая. Оба промаха пишутся в лог. */
	bool SetLocText(UUserWidget* Win, const TCHAR* WidgetName, int32 LocId);

	// ── привязка контролов по окнам (имена сняты дампом WBP, см. .cpp) ──────────────────────────
	void SetupSelectWinControls();     // 68100
	void SetupGridWinControls();       // 68300
	void SetupMsgWinControls();        // 68450
	void SetupClickWinControls();      // 68200
	void SetupVariantWinControls();    // 68500

	/** Обновить страницу витрины 68300 (порт `uiRefreshItemGridPage@0xB6EE70`). */
	void RefreshPrizeGridPage();
	/** Максимум позиции прокрутки витрины = рядов − 5 (`SetScrollRange(68310, 5, ceil(count / 5))`, шаг 5 записей). */
	int32 GridMaxScrollRow() const;
	int32 GridRowCount() const;   // ★ волна 14: рядов витрины = ceil(призов / 5) — второй аргумент `SetScrollRange(68310, 5, …)`
	/** Полоса прокрутки 68310 (скин `Nomal_Scr`, создаётся движком в рантайме). */
	void BuildGridScrollBar();
	void LayoutGridScrollBar();
	void TickGridScrollBar();
	/** Прямоугольник КОЛЁСНОГО ОКНА 68310 в экранных координатах Slate (по живой ячейке 68311).
	 *  false — ячейка ещё ни разу не отрисована. */
	bool GetWheelRect(FVector2D& OutMin, FVector2D& OutMax) const;
	/** Колесо мыши над колёсным окном 68310 — эталонное сообщение `0x17` (`GachaScene_OnControlMessage`:
	 *  `a2 == 68310` → `gap_254[120] = a6` + `uiRefreshItemGridPage`). */
	bool OnGridWheel(int32 Notches);
	/** Наведение на ячейку витрины 68311…68335 и на иконку приза 68453 → общая плашка подсказки
	 *  (эталон msg 8 → `cItemBar_BuildSlotTooltip@0xB6F3C0`, msg 0xA → `KillFocus`). */
	void TickTooltips();

	/** Заливка мессадж-бокса 68450: иконка 68453 + текст 68451 (loc 110621 «Поздравляем…»). */
	void FillPrizeBox();
	/** Заливка окна 68200: 68202 обычный премиум-баланс, 68204 подарочный. */
	void RefreshClickWinTexts();

	// ── варианты приза (окно 68500) ─────────────────────────────────────────────────────────────
	/** Кандидаты-варианты приза (порт цикла `uiItemFilterListPopulate@0xB70BB0`). */
	void CollectVariantCandidates(int32 BaseCode, TArray<int32>& OutCodes) const;
	/** Есть ли среди кандидатов запись, ОТЛИЧНАЯ от самого приза (есть ли что выбирать вообще). */
	bool HasVariantChoice(int32 BaseCode) const;
	/** Наполнить комбобоксы SS1 (профессия, поле +92) и SS2 (уровень, поле +80) и показать 68500. */
	bool OpenVariantWindow(int32 BaseCode);
	/** Кадровый разбор мыши окна 68500 (живёт и при ЗАКРЫТОЙ сцене — приход из элитного ящика). */
	void TickVariantWindow();
	/** Порт `itemFindVariantBySlotOption@0xB71840`: код варианта либо −1. */
	int32 ResolveVariant(int32 BaseCode) const;
	/** Применение ответа 6582 (порт `uiGachaResultApply@0xB72440`) — зовётся из `Tick`. */
	void ApplyPrizeResult(const TArray<uint8>& Body61);

	// ── звук (★ ЗАМЕНА: оригинальных S_e_pr_gc_* нет ни в одной сборке, см. .cpp) ────────────────
	void PlayGachaSound(FName Name);
	void StartIdleLoop();
	void StopIdleLoop();

	/** Прозрачная клик-кнопка поверх плана-картинки (у контрола нет `idc` ⇒ импортёр кнопки не сделал).
	 *  Возвращает САМУ кнопку: искать её потом по имени через `GetWidgetFromName` нельзя —
	 *  `ConstructWidget` при занятом имени молча выдаёт уникализованное (`…_1`), и поиск даёт nullptr. */
	UButton* AddPlaneButton(UUserWidget* Win, UWidget* Plane, const TCHAR* Name, TFunction<void(int32)> OnClick);

	/** ★★★ РАЗВЕРНУТЬ ВЕТКУ ИМПОРТЁРА. Узлы `.JUI` с `Visible = 0` импортёр помечает `Collapsed`,
	 *  причём и САМ узел, и созданную им union-ГРУППУ, и всех детей
	 *  (`Tools/asda_jui_generic_umg.py`, `eff_hidden` → `SlateVisibility.COLLAPSED`).
	 *  У свёрнутого родителя ребёнок НЕ рисуется и НЕ ловит мышь, сколько его ни показывай —
	 *  поэтому «показать» = пройти вверх по цепочке родителей до корня окна.
	 *  Ровно та же грабля, что у подложки 499 самоцветов и у стрелок дерева навыков. */
	static void ExpandBranch(UWidget* W);

	/** ★★★ Привести видимость ПОДДЕРЕВА полосы слота к состоянию эталонной полосы 68010.
	 *  ЗАЧЕМ: в `gamemain_jui.ini` полосы 68030/68040/68050 объявлены `Visible = "0"` (в оригинале
	 *  данных хватало на один-два автомата), а импортёр сворачивает такое поддерево ЦЕЛИКОМ —
	 *  вывеска с цифрой, обе кнопки и торцы планки становятся `Collapsed`, и на экране от полосы
	 *  остаётся одна тёмная планка. Эталон же (`deathRespawnUI_LayoutResurrectPanel@0xB6E480`)
	 *  сам гасит все пять полос и показывает первые N, то есть `Visible = 0` в `.JUI` — заготовка,
	 *  а не запрет. Полосы структурно одинаковы, поэтому узлы сопоставляются ПУТЁМ ИНДЕКСОВ
	 *  (имена не годятся: `uniq_name` даёт планам разные суффиксы) с проверкой класса узла.
	 *  @return число сопоставленных узлов; `OutChanged` — сколько поменяли, `OutMismatch` — сколько
	 *          узлов не сошлось по структуре (ненулевое значение вызывающая сторона пишет ошибкой). */
	static int32 MirrorSubtreeVisibility(UWidget* Src, UWidget* Dst, int32& OutChanged, int32& OutMismatch);

	/** Сколько узлов поддерева спрятано (`Collapsed`/`Hidden`) — мера «полоса пустая» для лога. */
	static int32 CountCollapsed(UWidget* Root);

	// ── сеть ──
	void OnBalance(uint8 Status, int32 NewBalance);
	void OnRollResult(const TArray<uint8>& Body63);
	void OnPrizeApplied(const TArray<uint8>& Body61);
	void RequestBalance();             // 5466

	// ── фазы ──
	void EnterPhase(EAsdaGachaPhase NewPhase);
	/** Длительность клипа выбранного автомата в секундах (из ассета, не из константы). */
	float ClipLen(int32 ClipSlot, float Fallback) const;
	/** Длительность клипа прокрутки (слот 2) — фаза `Rolling` длится ровно столько. */
	float RollingLen() const;

	// ── состояние ──
	bool bSceneOpen = false;
	bool bReady = false;
	bool bRequestInFlight = false;     // `gap_10[628]` — гейт от двойной отправки
	double RollSentAt = 0.0;           // ★ 07.11.2026: время отправки 6577 (удержание сцены до ответа — не дольше kRollReplyHoldSec)
	bool bResumePrize = false;         // ★ 07.11.2026: есть незабранный приз — при открытии сцены автомат снова выдаст шар
	int32 ResumePrizeSlot = 0;         // автомат незабранного приза
	int32 ResumePrizeCode = -1;        // код незабранного приза
	TArray<uint8> ResumePrizeRecord;   // запись незабранного приза (56 б, как `PrizeRecord`)
	bool IsPrizeBoxShown() const;      // показано ли окно приза 68450
	bool RefuseCloseForPrize();        // приз не забран — показать модальное окно 991202 и вернуть true
	void StashPrizeForReopen();        // запомнить незабранный приз при закрытии сцены
	void ResumePrizeIfAny();           // вернуть шар при открытии сцены
	void PostRollRefusal(int32 LocId);            // отказ прокрутки строкой таблицы — окном-сообщением (`asda.Gacha.RefusalMsgBox`)
	void PostRollRefusalText(const FString& Text); // то же для готового текста
	bool bWheelBound = false;          // обработчик колеса зарегистрирован в UAsdaWindowSubsystem
	EAsdaGachaPhase Phase = EAsdaGachaPhase::Closed;
	float PhaseTime = 0.f;             // секунд в текущей фазе
	int32 Balance = 0;                 // `CCharacter+0x44C`
	// ★★★ ВТОРОЙ КОШЕЛЁК. Эталон везде считает деньги СУММОЙ ДВУХ полей:
	//   `premiumGatya_CheckGoldAndSend@0xB70350` (ui_core.c:7499) и `uiShowMoneyConfirmDialog@0xB6F130`
	//   (ui_core.c:7374): `total = Field142C->gap_43B[17] + sub_929A63(g_pGameClient)`.
	//   Первое — деньги персонажа (их перезаписывает ответ 5467), второе — `get_oBC0D5@0xB6DF50`
	//   = `*(g_pGameClient + 0x2BCFD4)`, КЛИЕНТСКИЙ кэш «денег в подарок» (loc 12959).
	//   Именно эти два поля печатает `inventoryBag_RefreshGoldUi@0xB6DE60` в 68202 и 68204.
	//   ⚠ Писателя `*(g_pGameClient + 0x2BCFD4)` в срезе эталона НЕТ (единственная ссылка — чтение
	//     в `get_oBC0D5`), отдельного поля в канале 5466/5467 тоже нет ⇒ в оригинале этот кошелёк
	//     всегда 0. Держим его явным полем, чтобы формула суммы совпадала с эталоном 1:1.
	int32 GiftBalance = 0;             // `*(g_pGameClient + 0x2BCFD4)` (`get_oBC0D5@0xB6DF50`)
	int32 SelectedSlot = 0;            // `CGachaScene` dword 162 = `gap_254[52]`
	// ★★★ РАЗДЕЛЕНЫ ДВА РАЗНЫХ ПОЛЯ ЭТАЛОНА, которые раньше были одним `PrizeItemCode`.
	//   Смещения посчитаны от `gap_254` = `this + 0x254` (596): `gap_254[124]` = 720 = dword 180,
	//   `gap_254[288]` = 884 = dword 221 — сходится сразу по трём независимым местам эталона
	//   (`gap_254[52]` = 648 = dword 162 «выбранный слот» у `v15[162]`; `gap_4[8]` = 0xC = ctrl 68055;
	//   `gap_254[188]/[192]` = 784/788 = байт и dword, которые чистит `uiResetHideControl`).
	//
	//   `PrizeItemCode` = ЗАПИСЬ ПРИЗА `gap_254[124]` (dword 180), поле +0 = код предмета.
	//     Пишется ответом 6578 (`GachaScene_OnServerResult`, ui_core.c:7431-7437).
	//     Читается: `uiItemTooltipShow@0xB70A60` — гейт показа 68450, иконка 68453, имя в 110621;
	//               `sowelWndSendSocketRequest@0xB71D70` — тело 6581 (код/слот/контейнер).
	int32 PrizeItemCode = -1;          // запись приза +0 (`gap_254[124]` = dword 180)
	TArray<uint8> PrizeRecord;         // 56 б записи приза из 6578 (+4 контейнер, +5 слот i16)
	// ★★★ `VariantBaseCode` = `gap_254[288]` (dword 221) — «код предмета, для которого выбирают
	//   ВАРИАНТ». Это ДРУГОЕ поле, и путать их нельзя: именно по нему кнопка 68452 выбирает ветку
	//   (`if (gap_254[288] == -1)`, ui_core.c:7647). В эталоне его пишут ВСЕГО ДВА места
	//   (сплошной поиск по срезу: единственные записи — ui_core.c:7429 и ui_core.c:7950):
	//     · ui_core.c:7429 — ответ 6578 ставит **−1** (после КАЖДОГО прокрута);
	//     · ui_core.c:7950 — `uiGachaResultApply` (ответ **6582**, успех) ставит код выданного
	//       предмета, и только если тип подходит (`itemType ≤ 4 | 8…9 | subType 200/201`).
	//   ⇒ Сразу после прокрута оно ВСЕГДА −1, поэтому эталонная кнопка «Нажми» уходит в ветку
	//     «приз ещё не определён»: гасит 68450, возвращает автомат в клип покоя и включает
	//     петлю покоя. Ни 6581, ни окна 68500 в этот момент в оригинале НЕТ.
	int32 VariantBaseCode = -1;        // `gap_254[288]` (dword 221)
	bool bPreTakeChoice = false;       // окно 68500 открыто ДО 11006 (заказ ещё в ящике)
	int32 PreTakeSavedBase = -1;       // `VariantBaseCode` сцены до выбора «до получения» — вернуть после
	int32 ArmedPostBase = -1;          // код из ящика, для которого вариант уже выбран
	int32 ArmedPostVariant = -1;       // выбранный для него вариант (уйдёт в 6581 после 11007)
	bool bPostSwapPending = false;     // 6581 по заряженному варианту ушёл, 6582 ещё нет
	int32 LastPostSwapVariant = -1;    // вариант последнего такого 6581 (самотест)
	/** Записать `gap_254[288]` сцены; при открытом выборе из ящика — в сохранённое значение, вернётся после выбора. */
	void SetSceneVariantBase(int32 Code);
	/** Отправить 6581 по записи `PrizeRecord` с готовым кодом варианта (хвост `ConfirmVariant`). */
	void SendVariantSwap(int32 Variant);
	// Отложенное применение 6582 (закон `no-reentrant-net-callbacks`: из сетевого колбэка модель
	//   инвентаря не трогаем — кладём тело и применяем в `Tick`).
	TArray<uint8> PendingApplyBody;
	bool bApplyPending = false;
	// Тайл игрока на момент входа в сцену (порт `g_nPlayerTileX/Y = pos / 100`, шаг 9 BeginRoll).
	int32 SceneEnterTileX = 0;
	int32 SceneEnterTileY = 0;

	// ── виджеты (idnum → узел WBP) ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> RootWin  = nullptr;  // 68000
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ExitWin  = nullptr;  // 68075
	UPROPERTY(Transient) TObjectPtr<UUserWidget> SelectWin = nullptr; // 68100
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ClickWin = nullptr;  // 68200
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ViewWin  = nullptr;  // 68300
	UPROPERTY(Transient) TObjectPtr<UUserWidget> MsgWin   = nullptr;  // 68450
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Select2Win = nullptr;// 68500

	UPROPERTY(Transient) TObjectPtr<UWidget> Ball        = nullptr;   // 68057 «золотой шар»
	UPROPERTY(Transient) TObjectPtr<UWidget> BallHit     = nullptr;   // прозрачная кнопка над шаром (у 68057 нет idc)
	UPROPERTY(Transient) TObjectPtr<UWidget> BallGroup   = nullptr;   // группа импортёра, в которой лежат 68057 и 68058
	UPROPERTY(Transient) TObjectPtr<UWidget> BallBang    = nullptr;   // 68058 анимплан Apple_Make3
	UPROPERTY(Transient) TObjectPtr<UWidget> SceneHost   = nullptr;   // 68056 контейнер 1024-окна: сюда идёт render-target
	UPROPERTY(Transient) TObjectPtr<UImage>  SceneBack   = nullptr;   // 68059 `capback` — подложка ПОД 3D
	// ★★★ 68055 `Premium_PickBAR_Gtxt1` — ПОДСКАЗКА «НАЖМИ НА ШАР» (скин `GOLD_roullete_FONT4`,
	//   385×38, готовый золотой спрайт-текст на атласе `ne_pvp2`, UV v 0.926…1.0 / u 0…0.752).
	//   ЭТО НЕ «ТЕКСТ СУММЫ»: `SetText` для 68055 в эталоне нет НИ ОДНОГО, есть только показ/скрытие
	//   и покадровая раскладка. Роль доказана кодом:
	//     · `premiumGatya_InitWindow` (world_map.c:4578) и шаг 12 `premiumGacha_BeginRoll`
	//       (world_map.c:4732) — `SetVisible(68055, 0)`;
	//     · клип 3 («выдача») доиграл → `CControl_SetVisible(g_pPauseMenuCtrl, 1)` (ui_core.c:6634)
	//       вместе со снятием NoSelect с шара — «шар выпал, нажми»;
	//     · клик по шару (ui_core.c:7686) РАЗРЕШЁН только при видимом `this->gap_4[8]` (смещение
	//       структуры 0xC = поле `tileWorldSize`, куда init кладёт именно 68055) и первым делом
	//       ГАСИТ его (ui_core.c:7690);
	//     · раскладка (ui_core.c:7167-7171) ставит его на проекцию кости `Bone19` со сдвигом
	//       (0, −70) — то есть НАД шаром; 385×38 — баннер, а не 128×128 вспышка.
	//   `g_pPauseMenuCtrl` = 68055 доказан и исключением: больше 68055 показывать НЕКОМУ, а без
	//   показа клик по шару не проходил бы никогда и фича была бы мертва.
	UPROPERTY(Transient) TObjectPtr<UWidget> HintPlane   = nullptr;   // 68055 «нажми на шар»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TotalText = nullptr;  // 68103 всего GXP
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceText = nullptr;  // 68105 цена
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RestText  = nullptr;  // 68107 остаток
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays; // GC-якоря обработчиков

	// ── 68300 витрина призов ────────────────────────────────────────────────────────────────────
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> PrizeGrid = nullptr;  // 25 ячеек 68311…68335
	UPROPERTY(Transient) TObjectPtr<UWidget> GridSelectMark = nullptr;       // 68356 оверлей выбора
	UPROPERTY(Transient) FAsdaScrollBarNomal GridScrollBar;                  // 68310 (создаётся кодом)
	/** Канва-ХОЗЯИН полосы = та же группа, где лежат ряды ячеек (`Win_Tab_grp`). Полоса обязана жить
	 *  в ТОЙ ЖЕ системе координат, что ячейки: у окна 68300 анкор 0.5/0.5, а планы полосы общий
	 *  компонент создаёт с анкором 0/0 — в корневой канве они уехали бы в левый верхний угол ЭКРАНА. */
	UPROPERTY(Transient) TObjectPtr<class UCanvasPanel> GridScrollHost = nullptr;
	int32 GridScrollRow = 0;                                                 // `CGachaScene` dword 179
	int32 GridSelectedCell = -1;                                             // подсвеченная ячейка
	FAsdaMouseFrame GridMouse;                                               // фронты ЛКМ для полосы
	/** Ячейка витрины, по которой сейчас показана общая плашка (−1 — ни одной, −2 — иконка 68453). */
	int32 TipHoverCell = -1;

	// ── 68450 мессадж-бокс приза ────────────────────────────────────────────────────────────────
	UPROPERTY(Transient) TObjectPtr<UImage>     PrizeIcon = nullptr;   // 68453
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PrizeText = nullptr;   // 68451

	// ── 68200 итог/баланс ───────────────────────────────────────────────────────────────────────
	//   ★ ДВЕ СТРОКИ — ЭТО ЭТАЛОН, а не дефект порта: `premiumGatya_InitWindow` (world_map.c:4608-4617)
	//     резолвит подпись 68201 = loc 12904 «Сейчас GXP» + значение 68202 и подпись 68203 =
	//     loc 12959 «Деньги в подарок» + значение 68204, и оба значения печатает
	//     `inventoryBag_RefreshGoldUi@0xB6DE60` (item_inventory.c:5398-5419) из ДВУХ РАЗНЫХ полей.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ClickCashLabel = nullptr;     // 68201 подпись loc 12904
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ClickCashText = nullptr;      // 68202 обычный премиум
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ClickGiftLabel = nullptr;     // 68203 подпись loc 12959
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ClickGiftCashText = nullptr;  // 68204 подарочный

	// ── 68500 выбор варианта ────────────────────────────────────────────────────────────────────
	// ★ 68507/68508 — ОБЩАЯ точка `UAsdaJuiComboBox` (скин `Bk_ComboBox` + список `DarkGrey_List`),
	//   а не движковый `UComboBoxString`: замечания владельца №4 «списки UE5 default» и №5
	//   «плейсхолдеры вместо строк».
	UPROPERTY(Transient) TObjectPtr<class UAsdaJuiComboBox> VarJobCombo = nullptr;   // 68507 SS1 профессия
	UPROPERTY(Transient) TObjectPtr<class UAsdaJuiComboBox> VarLevelCombo = nullptr; // 68508 SS2 уровень
	/** Кадровый опрос мыши для окна 68500 — оно живёт и при ЗАКРЫТОЙ сцене (приход из ящика). */
	FAsdaMouseFrame VarMouse;
	UPROPERTY(Transient) TObjectPtr<UImage>     VarIcon = nullptr;            // 68512 иконка предмета
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VarTitle = nullptr;           // 68501 имя предмета
	UPROPERTY(Transient) TObjectPtr<UTextBlock> VarDesc = nullptr;            // 68502 описание
	/** Значения полей записи предмета, отвечающие пунктам комбобоксов (порт `gap_254[208]/[248]`). */
	TArray<int32> VarLevelValues;   // +80 RequiredLevel → SS2 «Выбран уровень»
	TArray<int32> VarJobValues;     // +92 UsageCategory → SS1 «Выбрана профессия»

	// ── звук ────────────────────────────────────────────────────────────────────────────────────
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> IdleLoop = nullptr;      // замена S_e_pr_gc_idle_00

	/** Таблица цен `DT_premium_gatya_data` (импорт `premium_gatya_data.bin`), лениво. */
	UPROPERTY(Transient) mutable TObjectPtr<UDataTable> PriceTable = nullptr;
	/** Таблица призов `DT_premium_gatya` + разобранный список кодов для сетки 68300. */
	UPROPERTY(Transient) TObjectPtr<UDataTable> PrizeTable = nullptr;
	UPROPERTY(Transient) TArray<int32> Prizes;

	/** 3D-часть сцены: автоматы + капчер. Живёт только пока сцена открыта. */
	UPROPERTY(Transient) TObjectPtr<AAsdaGachaSceneActor> SceneActor = nullptr;
	UPROPERTY(Transient) TObjectPtr<UImage> SceneImage = nullptr;
	/** Полосы слотов 68010+10i — их позиции задаёт проекция якоря, а не .JUI.
	 *  Здесь лежат ГРУППЫ импортёра (`Premium_PickBAR_{i+1}_grp`): в оригинале двигается сам
	 *  контрол 68010+10i, а его дети (кнопки, подписи, купон) едут вместе с ним — в UMG роль
	 *  «контрол вместе с детьми» играет группа-канвас. */
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SlotBars;
	/** Сам контрол-полоса 68010+10i ВНУТРИ группы: по нему берётся эталонный размер (76×62)
	 *  и локальное смещение группы относительно контрола (импортёр сдвигает группу на union детей). */
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SlotBarInner;
	/** Кнопки витрины 68011+10i (`Premium_PickBAR_{i+1}_B{i+1}`) — по одной на полосу.
	 *  Держим ссылками, а не ищем по имени каждый раз: имя кнопки уникально только у первой
	 *  (`ConstructWidget` при занятом имени молча выдаёт `…_1`), а проверять надо ИМЕННО ту,
	 *  на которую повешен обработчик. */
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> ViewButtons;
	/** Кнопки «крутить» 68061+i — прозрачные оверлеи над планами `Mac_Btn_2_{13,27,41,55,69}_0`
	 *  (у контрола в `.JUI` пустой `ID`, поэтому импортёр кнопки не создал). */
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SpinButtons;
	/** Планы-ЦИФРЫ автоматов `GOLD_roullete_FONT3_{1..5}` — номер на вывеске это КУСОК АТЛАСА
	 *  `NE_PvP2`, а не текст, поэтому проверяется UV-регион, а не строка. */
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SlotDigits;
	/** Размер вьюпорта, под который уже растянуты 68059/68056 — чтобы не дёргать слоты каждый кадр. */
	FVector2D FullScreenApplied = FVector2D::ZeroVector;

	FDelegateHandle HBalance, HRoll, HApply;
};
