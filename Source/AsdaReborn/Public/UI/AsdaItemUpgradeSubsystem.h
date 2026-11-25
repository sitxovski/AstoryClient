// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaItemUpgradeSubsystem — ОКНО «УЛУЧШЕНИЕ СНАРЯЖЕНИЯ» (ЗАТОЧКА): `Item_Upgrade_Win` (JUI **20000**)
//   и его попап «получена опция» `Get_UpgradeOtion` (JUI **19100**). Порт 25.09.2026, волны 2–4 плана
//   `Reports/gear_upgrade_re_report.md` §11-бис + клиентские дефекты волны 6.
//   Отчёт захода — `Reports/gear_upgrade/wave2-4_client.md`.
//
// ═════════════════ ★★★ ЧТО ЭТО ЗА ОКНО В ОРИГИНАЛЕ ═════════════════════════════════════════════
//   Своего класса-окна у заточки НЕТ: все поля живут в объекте инвентаря `g_pInventoryWnd`
//   (индексы полей 657…936), контролы привязывает `CItemUpgradeWin_InitControls@0xD26860`
//   (item_inventory.c:43670), вызываемый изнутри `InventoryWindow_InitControls`. Порт держит
//   это состояние в отдельной подсистеме — так же, как окно-спутник 20200 (`UAsdaItemResetSubsystem`).
//   Заголовок 20001 = loc 50000 «Улучшен. снаряжения».
//
// ═════════════════ ★★★ СНЯТЫЕ МИСНЕЙМЫ (важно для чтения эталона) ═════════════════════════════
//   • `uiCombineWindow_Close@0xD28340` — НЕ «close»: это ТУМБЛЕР окна 20000 (видимо ⇒ спрятать,
//     иначе полный сброс `uiCombineWindowResetOpen@0xD29EF0` и показ). Гейта открытия НЕТ.
//   • `netSendBoneMaskRequest@0xD2AAB0` — НЕ «костная маска»: это отправитель заточки 6504.
//   • `Recv_ItemUpgradeBatch@0xD2B1E0` (он же `OnRecv_ItemUpgradeBatch`) — приём ответа 6505.
//   • `itemEnhanceUI_OnControlMessage@0xD2A1C0` — обработчик событий «анимация доиграла» (0x18).
//   • `CItem_BuildTooltipText@0xD6E790` — открытие MSGBOX (1500 одна кнопка / 1520 две), не тултип.
//   • `g_pItemUpgradeWin` / `itemUpgradeWindow_open` — это ДРУГИЕ окна (50500 / 50300).
//   • В `MessageBoxA` эталона имена контролов местами сдвинуты на строку относительно `.JUI`
//     (20007 зовётся «Item_Upgrade_Step», в JUI — `Item_Upgrade_Grade`). Авторитет — IDNum.
//   • Поля записи предмета (57 б): **+24** ступень (в порту `GemGrade`), **+25** израсходовано
//     попыток, **+26** предел попыток, **+27** i16 накопленный процент (`EnchantPercent`).
//     Полей +25/+26 в `FAsdaItemInstance` нет — читаем из `Raw` (прецедент `UAsdaItemResetSubsystem`).
//
// ═════════════════ ФЛАГИ ОКНА ЭТАЛОНА (байты объекта инвентаря) ═══════════════════════════════
//   +3392 «окно заточки живо» (тик 1000 мс) · +3394 «свободен» (1 — можно начать попытку; 0 — идёт
//   попытка или пакет в полёте) · +3404 «взведён» (1 — анимация 20070 доиграла, иконка ждёт клика) ·
//   +3396/+3400 слоты свитка и цели, запомненные на старте · +3405 буфер текста результата.
//   ★ Проверено ДИЗАССЕМБЛЕРОМ (0xD2B005 `mov byte [eax+0xD42], 0`): отправитель 6504 сам сбрасывает
//   +3394 в 0, а ответ 6505 взводит его в 1. То есть «замок до ответа» у оригинала ЕСТЬ, но его
//   снимает любое повторное открытие окна (`uiCombineWindowResetOpen` ставит +3394 = 1) и нет ни
//   разрыва, ни таймаута. Это и есть клиентская часть дефекта N-2 (см. CVar `asda.Upgrade.SingleInFlight`).
//
// ═════════════════ ПОТОК (§6.1 отчёта) ═══════════════════════════════════════════════════════
//   20190 «Улучшить» → `uiItemUpgradeValidate@0xD27FC0` → гейты (110114 / 110115 / 110116 / 50011 /
//   диалог 72) → показать 20070 → конец 20070 → 20071 + 20072, «взведён» → ОДИНОЧНЫЙ клик по иконке
//   20003 (msgType 0xB и 0xE) → 6504 → 6505 → 20004 (код 1) / 20005 (код 8) / нотисы 50006…50011 →
//   конец 20004 → текст 50004 + попап 19100; конец 20005 → 20073 + текст 50005.
//   ★ Нумпад в системе НЕ вызывается (§8 отчёта, два метода) — количеств в 6504 нет вовсе.
//
// ═════════════════ ЗВУКИ И ЭФФЕКТЫ (волна 5, §6.2–6.4 отчёта) ════════════════════════════════
//   Семь звуков — через ОБЩУЮ звуковую систему порта `UAsdaSoundSubsystem` (второго механизма нет):
//     id 5 `S_e_button06` — любой клик по кнопке окна (ветка msgType 66561, ui_core.c:47649);
//     `S_e_button09` — НАВЕДЕНИЕ на иконку 20003, когда она взведена (msg 8, ui_core.c:47843);
//     `S_e_Itemupgrade1` — старт попытки (ui_core.c:48310 и «Да» диалога 72, ui_core.c:57114);
//     `S_e_button10` — клик по взведённой иконке 20003 перед отправкой 6504 (ui_core.c:47966/48014);
//     `S_e_Itemupgrade2` / `S_e_Itemupgrade3` — ответ 6505 код 1 / 8 (network.c:19645/19754).
//   ★ РЕШЕНИЕ КООРДИНАТОРА (как в оригинале): звук ДОИГРЫВАЕТ до конца — `PlaySound2D` одноразовый,
//     анимация его не обрезает (звук 768 КБ против анимации 20070 в 1200 мс).
//   Шесть анимаций — `UAsdaAnimatedPlane` (шаг кадра 1:1 с ui_core.c:108838, стартовый кадр учтён),
//   цепочка по событию 0x18; ветки конца 20071/20072 мертвы в эталоне и не перенесены.
//   Свечение оружия в мире по ступени заточки (§6.4) уже живёт в общей саге свечения
//   (`UAsdaWorldSubsystem::BuildGlowJebName` ← ступень надетого предмета +24); окно её не трогает —
//   заточка меняет только сумку, свечение пересобирает смена экипировки. Проверяется самотестом.
//   Журнал «анимация/звук → момент» (`FxJournal`) — для проверки порядка и моментов в самотесте.
//
// ═════════════════ ОКНО 78200 «ОСОБЫЙ» (волна 7) ═════════════════════════════════════════════
//   Кнопка 20197 открывает окно высокой переплавки `Smelting3_Win` — отдельная система
//   `UAsdaHighRefineSubsystem` (эталон `craftWnd_Open@0xCF3B00`, ui_core.c:47684).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaReplyLatch.h"        // FAsdaReplyLatch — засов «запрос в полёте»
#include "UI/AsdaBagListScroll.h"      // ★ 29.10.2026 (инвентарь ×2): FAsdaBagListScroll — окно 30 позиций над списком до 60
#include "UI/AsdaMarkup.h"             // FAsdaFmtText — стиль строки JUI до подмены на rich (общая точка разметки; ★ волна 15: без зависимости от окна 900)
#include "AsdaSessionReset.h"          // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaItemUpgradeSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
class UAsdaAnimatedPlane;
struct FAsdaItemInstance;

// ── Нотисы окна. Все id проверены по `RawAssets/Text/GameText.csv`; ничего не выдумано. ──
namespace AsdaItemUpgradeLoc
{
	// Статичные подписи `CItemUpgradeWin_InitControls@0xD26860`.
	constexpr int32 kTitle          = 50000;   // 20001 «Улучшен. снаряжения»
	constexpr int32 kLabelLevel     = 50020;   // 20007 «Уровень»
	constexpr int32 kLabelEffect    = 50021;   // 20010 «Эффект»
	constexpr int32 kLabelTries     = 50022;   // 20015 «Попыток»
	constexpr int32 kTabScroll      = 50023;   // 20065 «Свиток улучшения»
	constexpr int32 kTabHelpers     = 50040;   // 20142 «Дополнительные материалы»
	constexpr int32 kTabGear        = 50041;   // 20066 «Снаряжение»
	constexpr int32 kCardScroll     = 99700;   // 20151 «Свиток улучшения»
	constexpr int32 kCardHelper97   = 99701;   // 20154 «Дополнительные улучшения»
	constexpr int32 kCardHelper98   = 99702;   // 20157 «Удачные улучшения»
	constexpr int32 kBtnUpgrade     = 2323;    // 20190 «Улучшить»
	constexpr int32 kBtnCancel      = 2289;    // 20191 «Отмена»
	constexpr int32 kBtnSpecial     = 15109;   // 20197 «Особый»
	// Превью: суффикс «Предел» строки «Уровень после» и класс имени `50100 + ступень`.
	constexpr int32 kLimitSuffix    = 110113;  // «Предел»
	constexpr int32 kGradeNameBase  = 50100;   // 50105 «[Шедевр]» … (к ступени ≥ 5)
	// Гейты кнопки 20190 (`uiItemUpgradeValidate`) и гайд 20013 (`uiItemPanelTickValidateSelection`).
	constexpr int32 kNeedTarget     = 110114;  // «Выбери предмет для улучшения…»
	constexpr int32 kNeedScroll     = 110115;  // «Будет использован свиток улучшения…»
	constexpr int32 kTriesSpent     = 110116;  // «…воспользуйся свитком восстановления…»
	constexpr int32 kGuideMaxWeapon = 110117;  // «…предмет будет уничтожен» (не доспех)
	constexpr int32 kGuideMaxArmor  = 110118;  // «…все улучшения предмета будут сброшены» (categoryFlag 3)
	constexpr int32 kNotUpgradable  = 50011;   // «Предмет нельзя улучшить.»
	// Диалог подтверждения 72 (`CItem_BuildTooltipText(…, 1, 72, …)`, item_inventory.c:51118-51140).
	constexpr int32 kConfirmWeapon  = 110110;  // «…предмет будет уничтожен. Все равно попробовать?»
	constexpr int32 kConfirmArmor   = 110111;  // «…все его улучшения сбросятся. Продолжить?»
	constexpr int32 kConfirmOk      = 108510;  // «OK»
	constexpr int32 kConfirmCancel  = 108511;  // «Отмена»
	// Ответ 6505 (`Recv_ItemUpgradeBatch@0xD2B1E0`).
	constexpr int32 kResultOk       = 50004;   // код 1 «Создано: [[ss1] + [sd1]].»
	constexpr int32 kResultFail     = 50005;   // код 8 «Попытка улучшения провалилась…»
	constexpr int32 kResultCode2    = 50006;   // «Превышено максимальное количество улучшений.»
	constexpr int32 kResultCode3    = 50007;   // «Недостаточно купонов.»
	constexpr int32 kResultCode4    = 50008;   // «Это не личный предмет.»
	constexpr int32 kResultCode5    = 50009;   // «Улучшение не удалось.»      (сервер код 5 не шлёт)
	constexpr int32 kResultCode6    = 50010;   // «Неподходящий уровень»       (сервер код 6 не шлёт)
	constexpr int32 kResultCode7    = 50011;   // «Предмет нельзя улучшить.»
	constexpr int32 kDestroyed      = 110119;  // код 8 + запись цели пуста — «…предмет уничтожен»
	// Попап 19100 (`CItemEnhanceUI_ShowResultPopup@0xD2A470`).
	constexpr int32 kPopTitle6      = 99110;   // ступень ровно 6 — «Получено дополнительное улучшение»
	constexpr int32 kPopTitle7      = 99111;   // ступень ≥ 7 — «Увеличено количество возможных улучшений»
	constexpr int32 kPopText6       = 99100;   // «< [ss1] +[sd1] >: … Получено дополнительное улучшение.»
	constexpr int32 kPopText7       = 99101;   // «< [ss1] +[sd1] >: … Увеличено количество…»
	constexpr int32 kPopOptTitle    = 99112;   // 19104 «Дополнительное улучшение»
	constexpr int32 kPopOk          = 2288;    // 19106 «ОК»
	// Подсказка полосы «+N%» 20195 (msg 8, ui_core.c:47824): заголовок 110113, тело 110112.
	constexpr int32 kHintTitle      = 110113;  // «Предел»
	constexpr int32 kHintBody       = 110112;  // «Если сила улучшения предмета выше +88%…»
}

// Одна позиция списка окна (сетка, свитки, помощники) или выбранный предмет. В эталоне это узел
//   STL-списка `{…, bagSlot, …, ItemRecord*}`: указатель на ячейку контейнера. Порт держит пару
//   (контейнер, ячейка) и перечитывает запись из инвентаря — так же «живо», как указатель эталона.
USTRUCT()
struct FAsdaItemUpgradeEntry
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode  = 0;    // 0 ⇒ позиция пуста
	UPROPERTY(Transient) int32 Container = 0;    // 1 сумка снаряжения · 2 расходники
	UPROPERTY(Transient) int32 CellIndex = -1;   // ячейка контейнера (адрес записи у эталона)

	bool IsEmpty() const { return ItemCode <= 0 || CellIndex < 0; }
	void Clear() { *this = FAsdaItemUpgradeEntry(); }
	// «Тот же указатель» эталона = та же ячейка того же контейнера.
	bool SameCell(const FAsdaItemUpgradeEntry& O) const { return Container == O.Container && CellIndex == O.CellIndex; }
};

// Шесть эффект-контролов иконки 20003 (все — дети иконки, `gamemain_jui.ini:250678-250845`).
UENUM()
enum class EAsdaUpgradeFx : uint8
{
	Effect1  UMETA(DisplayName = "20070 Effect_Upgrade1 — старт, одноразовая 1200 мс"),
	Effect2  UMETA(DisplayName = "20071 Effect_Upgrade2 — статичная"),
	Click    UMETA(DisplayName = "20072 Ani_Click — «жми сюда», цикл"),
	Success  UMETA(DisplayName = "20004 Effect_Success — успех, одноразовая 900 мс"),
	FailAni  UMETA(DisplayName = "20005 Effect_Fail — провал, одноразовая 600 мс"),
	Fail     UMETA(DisplayName = "20073 Effect_Fail2 — «трещина», статичная"),
	Count    UMETA(Hidden)
};

UCLASS()
class ASDAREBORN_API UAsdaItemUpgradeSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
#if !UE_BUILD_SHIPPING
	// ★ 31.10.2026 самотест GC-якорей (`asda.SelfTest.RelayAnchors`): создать попап 19100, затем ПЕРЕСОЗДАТЬ окно заточки (тот же
	//   путь `Ensure`) при живом попапе; вернуть «ОК» 19106 попапа.
	UButton* DebugRecreateWindowKeepPopup();
#endif
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable ── окно, замок «пакет в полёте» и выбор принадлежат СЕССИИ.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaItemUpgradeSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (все из `.JUI` и `CItemUpgradeWin_InitControls`) ──
	static constexpr int32 kGridCells    = 30;   // 20020…20049 (+ затемнители 20100…20129), сетка 6×5
	static constexpr int32 kScrollCells  = 6;    // 20050…20055 — ВИДИМЫХ; сам список не ограничен (§7.5)
	static constexpr int32 kHelperCells  = 3;    // 20081…20083 (subType 97) и 20090…20092 (subType 98)
	static constexpr int32 kBag          = 1;    // `g_InventoryBag` — сумка снаряжения (эталон 30 ячеек, ★ 29.10 порт 60)
	static constexpr int32 kSpend        = 2;    // `g_InventoryArray2` — расходники, 60 ячеек
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): сумка 30 → 60. Эталон перебирает РОВНО 30 ячеек сумки — у нас всю сумку
	//   (= `UAsdaInventorySubsystem::kShellSlots`, static_assert в .cpp; ★ U21 (В1 дедупа №2): было `kBagCells`);
	//   сетка 20020…20049 (`kGridCells` = 30) показывает окно над `GridList`, хвост листается колесом (`BagScroll`).
	static constexpr int32 kShellCells   = 60;
	static constexpr int32 kSpendCells   = 60;   // и РОВНО 60 ячеек расходников
	static constexpr int32 kUpgradeZ     = 731;  // как у окна-спутника 20200 — выше инвентаря (720)
	static constexpr int32 kPopupZ       = 732;  // попап 19100 — над окном
	static constexpr uint32 kTickValidateMs = 1000;   // `timeGetTime() - g_dwInvRefreshLastTick > 0x3E8`

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	// Порт `uiCombineWindow_Close@0xD28340` — ТУМБЛЕР (кнопка 449, доп-меню case 6, ESC-событие 21).
	UFUNCTION(BlueprintCallable, Category="Asda|ItemUpgrade")
	void ToggleWindow();

	// Показ с полным сбросом (`uiCombineWindowResetOpen@0xD29EF0`). Гейта открытия у эталона НЕТ.
	UFUNCTION(BlueprintCallable, Category="Asda|ItemUpgrade")
	void OpenWindow();

	// Спрятать окно без сброса состояния (ветка «видимо ⇒ SetVisible(0)» тумблера; ESC; uiCloseAllWindows).
	UFUNCTION(BlueprintCallable, Category="Asda|ItemUpgrade")
	void HideWindow();

	UFUNCTION(BlueprintPure, Category="Asda|ItemUpgrade")
	bool IsOpen() const { return bOpen; }
	// ★ 08.11.2026 (пачка 19, остановка хода на время действия UI): идёт попытка заточки — от «Да»/клика по иконке (+3394 = 0)
	//   до ответа 6505 и конца анимации исхода; читает общая точка остановки хода `AsdaSelfMoveHeldForUiAction`.
	bool IsAttemptInProgress() const { return bOpen && (!bIdle || ReplyLatch.IsArmed() || bResultPending); }

	// ═══ ДЕЙСТВИЯ (в эталоне — ветки `CGameWindow_OnCommand@0xD1B140`, ui_core.c:47640-48040) ═══
	void OnGridCellClicked(int32 Cell);        // 20020…20049, msgType 0xB → `itemCombine_SelectSlot@0xD27930` (★ 29.10: Cell — позиция списка `GridList`)
	// ★ 29.10.2026 (инвентарь ×2): видимая ячейка сетки 20020…20049 → позиция списка `GridList` (окно прокрутки `BagScroll`).
	int32 GridListIndexOfVisible(int32 VisibleCell) const { return BagScroll.ToList(VisibleCell); }
	void OnScrollCellClicked(int32 Cell);      // 20050…20055 → `itemWindow_SelectCompareSlotA@0xD27B70`
	void OnHelper97CellClicked(int32 Cell);    // 20081…20083 → `itemWindow_SelectCompareSlotB@0xD27CE0`
	void OnHelper98CellClicked(int32 Cell);    // 20090…20092 → `itemWindow_SelectCompareSlotC@0xD27E50`
	void OnCardIconClicked(int32 Kind);        // 20150/20153/20156 (Kind 0/1/2), msgType 0xB — сброс без рамки
	void OnCardClearClicked(int32 Kind);       // крестики 20152/20155/20158 — сброс и прятать рамку
	void OnUpgradeClicked();                   // 20190 → `uiItemUpgradeValidate@0xD27FC0`
	void OnCancelClicked();                    // 20191 и 20002 → `uiHidePopups@0xD2A400`
	void OnIconClicked();                      // 20003 при «свободен == 0 && взведён» → 6504
	void OnSpecialClicked();                   // 20197 «Особый» → окно 78200 `Smelting3_Win` (волна 7)
	void OnPopupOkClicked();                   // 19106 → спрятать 19100

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол + итог «найдено N/M»
	FString DumpState() const;      // списки, выбор, флаги эталона, замок, таймеры эффектов
	// Положить тело 6505 в очередь приёма — ТЕМ ЖЕ путём, что сеть (закон diagnostic-must-use-real-path).
	void DebugInjectResult(const TArray<uint8>& Body231) { PendingResults.Push(Body231); }
	// Собрать правдоподобное тело 6505 из ТЕКУЩЕГО выбора окна (для проверки без сервера).
	bool DebugBuildResult(int32 Code, int32 NewLevel, TArray<uint8>& OutBody) const;
	// Досрочно «доиграть» эффект (как событие 0x18) — для проверки цепочки без отрисовки.
	void DebugFinishEffect(EAsdaUpgradeFx Fx) { OnEffectFinished(Fx); }
	// Состояние «6504 ушёл» БЕЗ сервера (то же, что ставит `SendAttempt` после удачной отправки) —
	//   для проверки замка N-2: отказ новой попытки, переоткрытие окна, таймаут.
	void DebugMarkSent();
	// ★ Волна 5: журнал «событие анимации/звука → момент (мс от первой записи)». Самотест сверяет
	//   по нему ПОРЯДОК и МОМЕНТЫ запуска (покадровая сверка звука невозможна — длительностей звуков в
	//   данных нет, §11-бис В5 п.4). Записи вида «snd S_e_Itemupgrade1», «fx+ 20070», «fx- 20070».
	const TArray<FString>& GetFxJournal() const { return FxJournal; }
	// Очистить журнал (самотест — перед каждым проверяемым шагом).
	void ClearFxJournal() { FxJournal.Reset(); FxJournalStart = -1.0; }
	// Наведение на иконку 20003 (msg 8) — тем же путём, что опрос мыши (самотест без реальной мыши).
	void DebugHoverIcon() { OnIconHoverEnter(); }
	// Нажать КНОПКУ окна так, как её жмёт игрок: `OnClicked` кнопки → реле → звук id 5 → обработчик.
	//   IdNum: 20002, 20190, 20191, 20152/20155/20158, 20197, 19106. false — кнопки нет.
	bool DebugPressButton(int32 IdNum);
	// ★ Волна 12 (снимки панели «+N%» для владельца, ТОЛЬКО для кадра — ассет не трогается): 0 — как сейчас
	//   (по ключу `asda.Upgrade.EnhancePanel`), 1 — панель вкл как в разметке (под «Особым»), 2 — вкл и подложка/
	//   кнопка «Особый» 20196/20197 спрятаны, 3 — как 2 и вдобавок 20194 (его код не включает никогда); −1 — вернуть
	//   всё как было. `Percent` — процент выбранного предмета (+27). Возврат — экранный прямоугольник панели
	//   (объединение 20193…20196) для кропа; пустой — панели нет.
	FBox2D DebugPanelShotMode(int32 Mode, int32 Percent);
	// Режим кадра 3: 20194 держится видимым и при покадровом `UpdateEnhancePanel` (только для снимка).
	bool bDebugShow20194 = false;
	// ★ 04.11.2026 (самотест видов свитков `asda.SelfTest.UpgradeScrollKinds`, клики настоящей мышью): живой виджет ячейки.
	//   Kind: 0 — видимая ячейка сетки 20020+Idx, 1 — свиток 20050+Idx, 2 — помощник 97, 3 — помощник 98, 4 — карточка 20150/53/56,
	//   5 — кнопка «Улучшить» 20190, 6 — иконка цели 20003. nullptr — нет такого.
	UWidget* DebugCellWidget(int32 Kind, int32 Idx) const;
	// Выбранные коды (цель, свиток, помощник 97, 98; 0 — не выбран) и текст «эффект после» 20012.
	void DebugSelection(int32& OutTarget, int32& OutScroll, int32& OutH97, int32& OutH98, FString& OutEffectAfter) const;
	// Коды списка свитков (все позиции `ScrollList`, не только 6 видимых).
	TArray<int32> DebugScrollCodes() const;
	// «Свободен» (+3394) — false после старта попытки (`StartAttempt`).
	bool DebugIsIdle() const { return bIdle; }

private:
	// ★ 29.10.2026 (инвентарь ×2): видимое окно сетки над `GridList` — подписи, затемнители, иконки, рамка цели 20060.
	void RefreshGridView();

	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	bool EnsurePopup();
	void ResetWidget();
	void ResetPopup();
	void SetupControls();
	void SetupPopupControls();
	void RegisterZones();
	// Собрать ВСЕ виджеты одного контрола `.JUI` по idc (сам узел, `_grp`, `_Text`, планы `<idc>_<n>_<p>`).
	void CollectByIdc(const FString& Idc, TArray<TObjectPtr<UWidget>>& Out) const;
	static void SetGroupVisible(const TArray<TObjectPtr<UWidget>>& Group, bool bVisible);

	// ── Порт функций окна ──
	void ResetOpen();                 // `uiCombineWindowResetOpen@0xD29EF0`
	void PanelResetAndHide();         // `uiPanel_ResetAndHide@0xD29830`
	void RefreshInventorySlots();     // `uiRefreshInventorySlots@0xD29A20` (+ помощники 97/98)
	void RefreshHelpers97();          // `invGridRefreshTable2Type97@0xD28420`
	void RefreshHelpers98();          // `uiPanelRefreshItemsCat98@0xD28750`
	void RefreshScrolls();            // `uiCombineRefreshSlots@0xD28A80`
	void RedrawCards();               // `itemWindow_RedrawCompareSlots@0xD293C0` (+ UpdateItemInfo)
	void UpdateItemInfo();            // `CItemWindow_UpdateItemInfo@0xD2C810`
	void UpdateEnhancePanel(int32 Percent, bool bHasTarget);   // панель «+N%» 20192…20195 (Д-11, N-6)
	void TickValidateSelection();     // `uiItemPanelTickValidateSelection@0xD2BD40`
	void StartAttempt();              // ветка «проверки пройдены» валидатора и «Да» диалога 72 (case 0x48)
	void AskConfirm72();              // `CItem_BuildTooltipText(…, 1, 72, …)`
	void SendAttempt();               // `netSendBoneMaskRequest@0xD2AAB0`
	void OnEffectFinished(EAsdaUpgradeFx Fx);   // `itemEnhanceUI_OnControlMessage@0xD2A1C0`
	void OnUpgradeResult(const TArray<uint8>& Body);   // `Recv_ItemUpgradeBatch@0xD2B1E0`
	int32 ApplyResultRecords(const uint8* Body, bool bFailBranch);   // 4 записи ответа в модель (N-3)
	void ShowResultPopup();           // `CItemEnhanceUI_ShowResultPopup@0xD2A470`
	void DeliverResultText();         // вывод буфера +3405: чат (голубой, 10 с) + ONEBUT_MSGBOX
	void FlushPendingResultIfAny();   // ★ ResultNeverLost: эффекты гасят раньше конца анимации — отдать буфер сейчас
	void ReleaseInFlight(const TCHAR* Why);   // снять замок N-2 (ответ / разрыв / таймаут)
	void HidePopup();
	void DrainPendingPackets();

	// ── Звуки (волна 5) ──
	void PlaySfx(const TCHAR* Name);           // `csoundPlayName2D(mgr, 18, "имя.wav")` — по имени, аргумент 18 игнорируется
	void PlayButtonSfx(int32 IdNum);           // `csoundPlayName2DStore(mgr, 5, 0)` — клик по кнопке окна (id 5 = S_e_button06)
	void OnIconHoverEnter();                   // msg 8 по 20003: звук «S_e_button09», если иконка взведена
	void JournalAdd(const FString& Event);     // запись в журнал «событие → момент» (см. GetFxJournal)

	// ── Эффекты ──
	void ShowFx(EAsdaUpgradeFx Fx, bool bShow);
	void HideAllFx();
	UFUNCTION() void HandleEffect1Finished();   // событие 0x18 контрола 20070
	UFUNCTION() void HandleSuccessFinished();   // событие 0x18 контрола 20004
	UFUNCTION() void HandleFailAniFinished();   // событие 0x18 контрола 20005

	// ── Данные ──
	const FAsdaItemInstance* LiveOf(const FAsdaItemUpgradeEntry& E) const;   // запись ячейки или nullptr
	static int32 RecByte(const FAsdaItemInstance& It, int32 Off);           // байт записи из `Raw`
	bool IsScrollCompatible(int32 ScrollEventId, const FAsdaItemInstance& Target) const;   // §7.5
	int32 SpecialSubTypeOf(int32 ItemCode) const;  // `Table2Record->subType` или −1
	int32 SpecialResultModelOf(int32 ItemCode) const;   // `Table2Record->resultModel` (u16) или 0
	void PlaceFrameAt(UWidget* Frame, const FVector2D& Base, UWidget* Cell0, UWidget* Cell) const;   // `CControl_SetField74_75`
	void SetFmt(FAsdaFmtText& F, const FString& Raw);   // строка с тегами <FC=…> в узел JUI
	void CaptureFmt(FAsdaFmtText& F, UTextBlock* T);    // снять стиль JUI до первой подмены на rich
	FString Loc(int32 LocId) const;       // теги срезаны
	FString LocRaw(int32 LocId) const;    // теги сохранены
	void MsgBox(int32 LocId) const;       // ONEBUT_MSGBOX 1500 (`CItem_BuildTooltipText(…, 0, −1, …)`)

	// ── Опросы ──
	void TickMouse();     // клик/наведение ПО ЖИВОЙ ГЕОМЕТРИИ (ячейки лежат внутри групп)
	void TickTooltip(int32 OverKind, int32 OverIdx);

	// ═══ ВИДЖЕТЫ ОКНА 20000 ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose   = nullptr;   // 20002
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnUpgrade = nullptr;   // 20190
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCancel  = nullptr;   // 20191
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnSpecial = nullptr;   // 20197
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> BtnCardClear;      // 20152/20155/20158
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTitle      = nullptr;   // 20001
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLevelLbl   = nullptr;   // 20007
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLevelBef   = nullptr;   // 20008
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLevelAft   = nullptr;   // 20009
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtEffectLbl  = nullptr;   // 20010
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtEffectBef  = nullptr;   // 20011
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtEffectAft  = nullptr;   // 20012
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTriesLbl   = nullptr;   // 20015
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTriesBef   = nullptr;   // 20016
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTriesAft   = nullptr;   // 20017
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTabGear    = nullptr;   // 20066 (капшн `_Text`)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTabScroll  = nullptr;   // 20065
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTabHelpers = nullptr;   // 20142
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TxtCardTitle;      // 20151/20154/20157
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBtnUpgrade = nullptr;   // подпись 20190
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBtnCancel  = nullptr;   // подпись 20191
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtBtnSpecial = nullptr;   // подпись 20197
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPanelValue = nullptr;   // 20192 «+N»
	UPROPERTY(Transient) FAsdaFmtText FmtName;     // 20006 — имя с цветом грейда и тегами <FC=…>
	UPROPERTY(Transient) FAsdaFmtText FmtGuide;    // 20013 — гайд с тегами <FC=…>
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     GridIcons;       // 30 планов 20020…20049
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GridLabels;      // 30 подписей «+N%»
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     ScrollIcons;     // 6 планов 20050…20055
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ScrollLabels;    // 6 подписей «кол-во»
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     H97Icons;        // 3 плана 20081…20083
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> H97Labels;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     H98Icons;        // 3 плана 20090…20092
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> H98Labels;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     CardIcons;       // 20150 / 20153 / 20156
	UPROPERTY(Transient) TObjectPtr<UImage> TargetIcon = nullptr;        // 20003 (64×64)
	UPROPERTY(Transient) TObjectPtr<UWidget> FrameItemSel   = nullptr;   // 20060 `Slot_AniSelect`
	UPROPERTY(Transient) TObjectPtr<UWidget> FrameScrollSel = nullptr;   // 20058
	UPROPERTY(Transient) TObjectPtr<UWidget> FrameSubSel0   = nullptr;   // 20140
	UPROPERTY(Transient) TObjectPtr<UWidget> FrameSubSel1   = nullptr;   // 20141
	FVector2D BaseItemSel = FVector2D::ZeroVector;     // позиции рамок «как в .JUI» (над ячейкой 0 своей группы)
	FVector2D BaseScrollSel = FVector2D::ZeroVector;
	FVector2D BaseSubSel0 = FVector2D::ZeroVector;
	FVector2D BaseSubSel1 = FVector2D::ZeroVector;
	TArray<TArray<TObjectPtr<UWidget>>> DisableGroups;   // 20100…20129 целыми поддеревьями
	TArray<TArray<TObjectPtr<UWidget>>> FxGroups;        // 6 эффектов (EAsdaUpgradeFx)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaAnimatedPlane>> FxPlanes;   // анимированный план эффекта (или null)
	UPROPERTY(Transient) TObjectPtr<UWidget> Panel20193 = nullptr;   // `NoImage_icon_grp` («Отменить привязку… +N Осталось %.»)
	UPROPERTY(Transient) TObjectPtr<UWidget> Panel20194 = nullptr;   // `GFONT_item_S_grp`
	UPROPERTY(Transient) TObjectPtr<UWidget> Panel20195 = nullptr;   // `Box_Brown_226_0` — подложка полосы + подсказка
	UPROPERTY(Transient) TObjectPtr<UWidget> Panel20196 = nullptr;   // `Option_st_w_grp` — подложка кнопки «Особый»
	UPROPERTY(Transient) TObjectPtr<UWidget> DragHandle20014 = nullptr;   // `Item_Upgrade_Move_214_DRAG` (ручка окна)
	UPROPERTY(Transient) TObjectPtr<UWidget> Group20080 = nullptr;        // `Item_Ugrade_SubMat_grp` (блок помощников)

	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> GridIconsGrid = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> ScrollGrid    = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> H97Grid       = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> H98Grid       = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> CardGrid      = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> TargetGrid    = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.RelayAnchorSplit`): якорь кнопки «ОК» 19106 попапа 19100 (`Get_UpgradeOtion`) — отдельно от
	//   окна заточки. Раньше он лежал в `Relays`, а его сбрасывает `ResetWidget` окна заточки, не закрывая попап (владелец окна —
	//   только z-порядок), — после сборки мусора «ОК» попапа молча не работал. Сбрасывается вместе с попапом (`ResetPopup`).
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> PopupRelays;
	TArray<FSlateColor> GridLabelBase;   // цвета подписей из JUI (вернуть, когда «+N%» гаснет)
	TArray<FSlateColor> H97LabelBase;
	TArray<FSlateColor> H98LabelBase;
	FSlateColor LevelAftBase;            // цвет 20009 из JUI (эталон перекрашивает его всегда — держим для дампа)
	FSlateColor EffectAftBase;           // цвет 20012 из JUI

	// ═══ ВИДЖЕТЫ ПОПАПА 19100 ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> PopupWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PopTitle     = nullptr;   // 19101
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PopText      = nullptr;   // 19102
	UPROPERTY(Transient) TObjectPtr<UImage>     PopIcon      = nullptr;   // 19103 (план)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PopIconLabel = nullptr;   // 19103 (текст «+N%»)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PopOptTitle  = nullptr;   // 19104
	UPROPERTY(Transient) FAsdaFmtText FmtPopOpt;                          // 19105 (с тегом цвета)
	UPROPERTY(Transient) TObjectPtr<UButton>    PopOkBtn     = nullptr;   // 19106
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PopOkText    = nullptr;   // подпись 19106
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> PopIconGrid = nullptr;

	// ═══ МОДЕЛЬ ═══
	bool bOpen = false;             // окно показано
	bool bLive = false;             // +3392 «окно заточки живо» (тик 1000 мс), снимает только `uiHidePopups`
	bool bControlsReady = false;
	bool bPopupReady = false;
	bool bPopupOpen = false;
	bool bDirty = false;            // инвентарь изменился — пересобрать списки в Tick
	bool bIdle = true;              // +3394 «свободен»
	bool bArmed = false;            // +3404 «взведён»
	bool bConfirmOpen = false;      // диалог 72 открыт — второй не открываем
	FAsdaReplyLatch ReplyLatch;     // ★ N-2: 6504 ушёл, ответа 6505 ещё нет (когда ушёл — `ArmedAt`). ★ S2 (В2-A дедупа №2, 11.10.2026): общая точка вместо `bInFlight + InFlightSince`
	double LastValidateTime = -1.0; // последний проход тика 1000 мс

	TArray<FAsdaItemUpgradeEntry> GridList;   // this+759 — сумка без subType 66/200/201
	FAsdaBagListScroll BagScroll;             // ★ 29.10.2026: окно сетки 30 над `GridList` (ряд прокрутки колесом)
	TArray<FAsdaItemUpgradeEntry> ScrollList; // this+762 — совместимые свитки/купоны (без ограничения)
	TArray<FAsdaItemUpgradeEntry> H97List;    // this+765 — сумка, `special.subType == 97`
	TArray<FAsdaItemUpgradeEntry> H98List;    // this+768 — сумка, `special.subType == 98`
	FAsdaItemUpgradeEntry Target;             // this+844 (+3376)
	FAsdaItemUpgradeEntry Scroll;             // this+845 (+3380)
	FAsdaItemUpgradeEntry Helper97;           // this+846 (+3384)
	FAsdaItemUpgradeEntry Helper98;           // this+847 (+3388)
	int32 TargetIdx = -1;                     // this+843 — индекс цели в GridList
	int32 ScrollIdx = -1;                     // this+925
	int32 H97Idx    = -1;                     // this+927
	int32 H98Idx    = -1;                     // this+929
	int32 SavedScrollSlot = -1;               // +3396 — слот свитка на момент старта
	int32 SavedTargetSlot = -1;               // +3400 — слот цели на момент старта
	int32 SavedScrollContainer = 0;           // ★ наше: контейнер свитка на момент старта (см. SendAttempt)
	FString ResultText;                       // +3405 — буфер текста результата
	// ★ Ревизия R-1: запись улучшенного предмета ИЗ ПАКЕТА 6505 (@3, 57 б). Текст 50004 и попап 19100
	//   берут имя/ступень/опцию отсюда, а не из текущей цели окна: к приходу ответа цель могла быть
	//   сброшена (переоткрытие окна при живом замке N-2, поздний ответ после таймаута, сдвиг сетки).
	TArray<uint8> ResultRecord;
	TArray<FString> FxJournal;                // журнал волны 5 «событие → момент» (самотест)
	double FxJournalStart = -1.0;             // момент первой записи журнала (FPlatformTime::Seconds)
	bool bIconHover = false;                  // курсор над 20003 в прошлом кадре — фронт msg 8 (наведение)
	bool bResultPending = false;              // буфер заполнен и ждёт конца 20004/20005 (наш признак, ResultNeverLost)
	EAsdaUpgradeFx PendingFx = EAsdaUpgradeFx::Success;   // какая анимация результата его выведет (20004 / 20005)
	double FxDeadline[(int32)EAsdaUpgradeFx::Count] = {};   // крайний срок «доиграл» одноразового эффекта (0 — нет)

	int32 TipKind = -1;     // что под курсором показано плашкой (вид ячейки)
	int32 TipIdx  = -1;     // и её номер
	FAsdaMouseFrame Mouse;
	TAsdaNetQueue<TArray<uint8>> PendingResults;   // закон `no-reentrant-net-callbacks`
	FDelegateHandle HUpgradeResult;
	FDelegateHandle HInvCell;
};
