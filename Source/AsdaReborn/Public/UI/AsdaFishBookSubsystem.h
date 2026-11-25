// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishBookSubsystem — ★★★ «КНИГА РЫБАКА» (окно `FISH_BOOK_WIN`, JUI IDNum 74000), ОКНО РЕГИСТРАЦИИ РЫБЫ 74200
//   (`FISH_BOOK_JOINBOX`) и НУМПАД РЕЖИМА 10. Волна В3 рыбалки (08.10.2026).
//   Реверс (источник истины): `Reports/owner_1008/fishing_parts/ui_book.md` (весь), `client_net.md` §1.5-1.8/§2.7-2.12,
//   `server.md` §2.5-2.8/§6.3, сводка `fishing_reverse.md` §4/§5/§8 (Р6, Р7, Р12). Отчёт волны: `fishing_impl_W3.md`.
//
// ═══════════════════ ЧТО ЭТО В ЭТАЛОНЕ ══════════════════════════════════════════════════════════════════════════
//   Объект книги — ПОДОБЪЕКТ менеджера рыбалки `CMap+0x190C` (абсолютный 0x352137C, МИСНЕЙМ `g_pUpgradeWindow`),
//   все функции — `__thiscall` над ним; данные грузятся ОДИН раз за процесс (`cmapInitReset@0xFC3170` →
//   `CFishCollection_LoadPictureBookData@0xFD5FD0`), окно собирается лениво (`uiWindowInit_74000@0xFD2950`, флаг +1832).
//   Поля (dword-индексы ui_book §1) перенесены в поля подсистемы с теми же номерами в комментариях.
//
// ═══════════════════ ЧТО ДЕЛАЕТ ═════════════════════════════════════════════════════════════════════════════════
//   • Игрок регистрирует КНИГУ-ПРЕДМЕТ (подтип 210) — использование из сумки (`CInventory_OnSlotUse` g:341275) или
//     автомат рыбалки в состоянии 101 (`inventoryFindFirstValidItem@0xFDAF40`) → диалог 79 (111018) → 6176 → 6177.
//   • Сдаёт РЫБУ из рюкзака: двойной щелчок по ячейке пойманной рыбы → окно 74200 → ЛКМ по стопке → нумпад режима 10
//     (максимум `min(стопка, требуется − сдано)`) → «ОК» 74252 → 6180 (ИНДЕКС тома и ВВЕДЁННОЕ количество) → 6181.
//   • Удаляет книгу: 74271 → диалог 77 (111017) → 6178 → 6179.  • Получает награду: выбор 1 из 4 → 74090 → 6182 → 6183.
//   • Прогресс по томам приходит 6174 (вход/портал, по пакету на зарегистрированный том; 6175 — конец списка, у
//     оригинала обработчика нет). Поимка рыбы (6172 код 1) обновляет счёт/размеры локально (`questTableUpdateKeyMinMax`).
//
// ═══════════════════ ОТСТУПЛЕНИЯ / ИСПРАВЛЕНИЯ ДЕФЕКТОВ ОРИГИНАЛА [ПОРТ] ══════════════════════════════════════════
//   Р12 `asda.FishBook.ClientTable` (0 — [ПОРТ] авторитет сервера: рыбы/требования/награды/книга-предмет из
//       Fishing_BookInfo.txt + item_table.txt; 1 — клиентская таблица Fish_collection_data.BIN, как оригинал).
//   Р7/Д1 `asda.FishBook.LegacyCursor` (0 — [ИСПРАВЛЕНИЕ] курсор 74241 на ВЫБРАННОЙ стопке ровно поверх ячейки;
//       1 — оригинал: инверсия сравнения `!=` (0xFD6DB5 `je`) и сдвиг (+10,+7) от ячейки-соседа).
//   Р7/Д2 `asda.FishBook.LegacyReward5` (0 — [ИСПРАВЛЕНИЕ] ЛКМ по подписи 74085 награду не выбирает; 1 — оригинал:
//       диапазон 74081…74085 даёт t = 4 → читается `кол-во[0]` как 5-я награда).
//   Р6/Д3 (без ключа) 6174: граница индекса тома `<` (оригинал `<=` — запись за массив).
//   [ПОРТ] `asda.FishBook.LiveRefresh` (1): окно 74200 перерисовывается при изменении сумки извне. В оригинале 74200
//       МОДАЛЬНО (`helper_setupFields(g_pWindowMgr, 74200, 1)` — мышь получает только оно), поэтому сумка под ним не
//       меняется; в порту модальность подпанели держит сама книга (её прочие контролы под 74200 молчат), но окна
//       других подсистем не блокируются — без перерисовки указатели стопок могли бы протухнуть.
//       ★ Ревизия В3, ВАЖН-3: под тем же ключом выбор стопки [466]/[465] сверяется с сумкой (в ячейке всё ещё рыба окна,
//       стопка ≥ введённого) при перерисовке, ОК нумпада и «ОК» 74252 — иначе выбор снимается (74252 → 111060).
//   [ПОРТ] сброс сессии: состояние томов чистится при выходе в выбор персонажа / разрыве (оригинал при разрыве
//       закрывается, а данные книги живут весь процесс); при входе в мир и смене карты — НЕ чистится (6174 идут в
//       потоке загрузки персонажа, их порядок относительно 1021 не установлен — закон session-reset-drops-early-data).
//   Д5/Д7/Д11 — [ОРИГИНАЛ] без изменений (выбор вкладки режется до числа вкладок; ошибка 6181 — голым числом кода;
//       петля диалога 79 при ответе 6177 код 5).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Engine/DataTable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaMarkup.h"         // FAsdaFmtText — строки с тегами `<FC=…>` (111014, описание рыбы)
#include "AsdaSessionReset.h"      // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"        // FAsdaHeldTables — удержание DT от сборщика мусора
#include "AsdaFishBookSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UPanelWidget;
class UAsdaButtonRelay;

// ═════════════════════ СТРОКА DT_FishBook (собирает Tools/asda_build_fishbook_dt.py) ═════════════════════════════
// Один том книги: ОБЪЕДИНЕНИЕ клиентской записи `Fish_collection_data.BIN` (348 б) и серверной строки
//   `Fishing_BookInfo.txt` (215 б в памяти сервера) по ключу «номер тома» (закон tables-merge-not-replace).
//   Ключ строки DT = номер тома (0…8).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaFishBookRow : public FTableRowBase
{
	GENERATED_BODY()
	// Номер тома (ключ объединения; = индекс тома эталона `[463]` и `idx` сервера).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") int32 Tome = 0;
	// Имя тома: клиент +8 char[64] (cp1251). Есть ТОЛЬКО у клиента; у томов 5…8 в клиенте MINE — `????` (как есть).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") FString Name;
	// Клиент +0 — номер тома в записи (шаблон `+0`; −1 = шаблон недействителен). −1 — тома нет в клиенте.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") int32 ClientTome = -1;
	// Клиент +4 — id книги-предмета тома (шаблон `+72`, ищется в рюкзаке FSM 101).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") int32 ClientBookItemId = -1;
	// Сервер: предмет item_table с классом 210 и номером книги (ItemDB+12) == тому; −1 — книги у сервера нет (тома 5…8).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") int32 ServerBookItemId = -1;
	// Клиент +72 — флаг (шаблон `+68`; клиентом НЕ читается, переносится как есть).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") int32 ClientFlag = 0;
	// Сервер +2 u8 — тип книги (0/1/2), сервер копирует его в запись игрока +4 (= байт 6174 @4).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") int32 ServerType = 0;
	// Том есть в клиентской таблице.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") bool bClient = false;
	// Том есть в серверной таблице.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") bool bServer = false;
	// Клиент +76 i32[30] — коды рыб (−1 пусто).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") TArray<int32> ClientFish;
	// Клиент +196 i32[30] — требуемое количество сдать.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") TArray<int32> ClientReq;
	// Клиент +316 i32[4] — коды наград (−1 пусто).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") TArray<int32> ClientReward;
	// Клиент +332 i32[4] — количества наград (показ при > 1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") TArray<int32> ClientRewardCnt;
	// Сервер +3 i32[30] — коды рыб.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") TArray<int32> ServerFish;
	// Сервер +123 i16[30] — требуемое количество.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") TArray<int32> ServerReq;
	// Сервер +183 i32[4] — коды наград (сервер сверяет код 6182 с этими четырьмя).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") TArray<int32> ServerReward;
	// Сервер +199 i32[4] — количества наград (`sub_5340B0(id, count)`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Книга рыбака") TArray<int32> ServerRewardCnt;
};

// Виджеты одной ячейки сетки (клон шаблона 74020, i = 0…29; узлы кладёт шаг ассета `asda_ui_fishbook_clones`).
USTRUCT()
struct FAsdaFishBookCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWidget>    Root = nullptr;   // [5+10i] клон 74020 (группа; её прямоугольник = клон 74029 — зона ЛКМ)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Name = nullptr;   // [7+10i] клон 74021 — имя рыбы / «?????»
	UPROPERTY(Transient) TObjectPtr<UWidget>    Que  = nullptr;   // [8+10i] клон 74022 `FISH_QUE` — «?» (подложка, видна всегда)
	UPROPERTY(Transient) TObjectPtr<UWidget>    Done = nullptr;   // [9+10i] клон 74023 `GFONT_SU` — «собрано»
	UPROPERTY(Transient) TObjectPtr<UImage>     Pic  = nullptr;   // [10+10i] клон 74024 — кадр атласа рыбы 196×128
	UPROPERTY(Transient) TObjectPtr<UTextBlock> No   = nullptr;   // [11+10i] клон 74025 — «%03d»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Cnt  = nullptr;   // [12+10i] клон 74026 — «%d/%d»
	UPROPERTY(Transient) TObjectPtr<UWidget>    Red  = nullptr;   // [14+10i] клон 74030 — красная рамка выбранной ячейки
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Click;          // [13+10i] клон 74029 — 8 планов хит-рамки (−1,−1 / 102×92), зона ЛКМ/0xD
};

// Виджеты одной награды тома (t = 0…3): 74081+t иконка, 74085+t имя, 74111+t рамка выбора.
USTRUCT()
struct FAsdaFishBookRewardUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;   // [336+4t] план иконки 64×64
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Qty   = nullptr;   // текст той же иконки — количество (при > 1)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Name  = nullptr;   // [337+4t] имя (≤ 13 знаков + «...»)
	UPROPERTY(Transient) TObjectPtr<UWidget>    Frame = nullptr;   // [339+4t] `Slot_AniSelect` — выбранная награда
};

// Виджеты одной стопки окна регистрации (k = 0…29): 74211+k.
USTRUCT()
struct FAsdaFishBookInvUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon  = nullptr;   // [362+3k] иконка предмета
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;   // текст той же ячейки — число стопки
};

UCLASS()
class ASDAREBORN_API UAsdaFishBookSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

	friend struct FAsdaFishBookSelfTest;   // офлайн-самотест `asda.FishBook.SelfTest` (внутренние поля, шаги)

public:
	// Поднять подсистему: подписки на сеть/инвентарь, точки автомата рыбалки (В1).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снять подписки и точки автомата.
	virtual void Deinitialize() override;
	// IAsdaSessionResettable — что сохраняется при входе/смене карты, см. шапку.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Кадр: очередь пакетов → опрос мыши окна.
	virtual void Tick(float DeltaTime) override;
	// Стат тикера.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaFishBookSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═════════════════════ ЧИСЛА ЭТАЛОНА ═════════════════════
	static constexpr int32 kFish = 30;            // рыб в томе / ячеек сетки / стопок окна регистрации
	static constexpr int32 kRewards = 4;          // наград в томе (74081…74084)
	static constexpr int32 kMaxTabs = 4;          // вкладок томов не больше 4 (`gameUi_refreshPagedListBox`)
	// ★ fishfix2 09.10 (ревизия fishing_fixes_REVIEW МЕЛК-1): внутреннее поле подписи с КАЖДОЙ стороны — обрезанная подпись не
	//   залезает на скругление скина вкладки. Вкладка — 6 px (тот же запас, что у целых «…, том N»: 145 из 158); награда — 2 px.
	static constexpr float kTabCaptionPad = 6.f;
	static constexpr float kRewardCaptionPad = 2.f;
	static constexpr int32 kInitTabs = 3;         // ячеек списка 74105 при создании (`TabBar_W108_3Btn`: 3 плана)
	static constexpr int32 kActiveCap = 100;      // список зарегистрированных томов [472…571]
	static constexpr int32 kSpendCells = 60;        // перебор `g_InventoryArray2` (контейнер 2; ★ U21 (В1 дедупа №2): было `kBagCells`)
	static constexpr int32 kBagContainer = 2;     // контейнер расходников
	static constexpr int32 kBookSubType = 210;    // подтип книги-предмета
	static constexpr int32 kZ = 731;              // z окна (полоса обычных окон; подъём при показе — общий)
	static constexpr int32 kFirstCellId = 74211;  // IDNum первой стопки окна регистрации

	// ═════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═════════════════════
	// `itemCollectUiOpen@0xFD3A20`: уже видно — НИЧЕГО (не тоггл); иначе сброс выбора, закрыть сумку, перерисовка, показ.
	UFUNCTION(BlueprintCallable, Category="Asda|FishBook")
	void Open();
	// `itemCollectUiShowOrToggle@0xFD3B00` (74099, ESC, ответ 6177): окно регистрации видно → закрыть его; иначе спрятать книгу.
	UFUNCTION(BlueprintCallable, Category="Asda|FishBook")
	void CloseOrToggle();
	// Видна ли книга (эталонный байт +292 корня).
	UFUNCTION(BlueprintPure, Category="Asda|FishBook")
	bool IsOpen() const { return bOpen; }
	// Видно ли окно регистрации 74200.
	UFUNCTION(BlueprintPure, Category="Asda|FishBook")
	bool IsJoinOpen() const { return bJoinOpen; }

	// ═════════════════════ ВХОДЫ ИЗ ДРУГИХ СИСТЕМ ═════════════════════
	// `CInventory_OnSlotUse@0xD07460` (g:341273-341277): использован предмет подтипа 210 из ячейки контейнера 2 →
	//   `itemTooltipSetCountAndRefresh@0xFD7150` ([469] = запись, диалог 79). true — обработано (дальше не идти).
	bool OnUseBookItem(int32 Container, int32 Cell);

	// ═════════════════════ ДЕЙСТВИЯ (обработчики контролов; консоль и самотест зовут ИХ ЖЕ) ═════════════════════
	void OnCellClick(int32 Cell);        // 0xB по клону 74029 (`itemCollectUISelectColAndRefresh@0xFD5DB0`)
	void OnCellDoubleClick(int32 Cell);  // 0xD по клону 74029 (`itemCollectUiSelectSlot@0xFD5E40`)
	void OnRewardClick(int32 T);         // 0xB по 74081+T (`itemCollectUiSelectSlot_5F20@0xFD5F20`; T = 4 — Д2)
	void OnTabSelected(int32 N);         // 66817 по 74105, a6 = N
	void OnArrow(int32 Dir);             // 74106 (−1) / 74107 (+1) — `uiRebuildSelectableListPaged@0xFDA430`
	void OnDeleteClicked();              // 74271 → диалог 77 / нотис 111069
	void OnRewardButton();               // 74090 → `item_sendConfirmWithCostCheck@0xFD94A0` (6182)
	void OnInvClick(int32 K);            // 0xB по 74211+K → нумпад режима 10 (`uiOpenFishDetailContextPopup@0xFD6FC0`)
	void OnNumPadAccepted(int32 Qty);    // ОК нумпада (`uiUpgradeDlgOnOk@0xDA6850` → `fishCollectionSelectDetail@0xFD70F0`)
	void OnJoinOk();                     // 74252 → `netSendPacket6180@0xFD8800`
	void CloseJoin();                    // 74253 / `uiWindowMgr_HideControlAndClose@0xFD67C0`

	// ═════════════════════ ДИАГНОСТИКА ═════════════════════
	FString DumpControls() const;        // КАЖДЫЙ разобранный контрол с живой геометрией (закон full-structural-analysis)
	FString DumpState() const;           // тома, выбор, вкладки, окно регистрации
	UUserWidget* DebugWidget() const { return Widget; }   // корень окна — для кликов по живой геометрии
	// ★ 10.10.2026: показать окно регистрации 74200 штатным `OpenJoin` (`fishCollectionOpenWindow`) — для самотеста замка ввода
	//   `asda.UI.SelfTestModalLock` (там нет томов с уловом, двойной щелчок по ячейке окно не откроет). Только при открытой книге.
	void DebugOpenJoin(int32 FishId) { if (bOpen) { OpenJoin(FishId); } }
	// ★ 10.10.2026 (modal_lock2, СРЕДН-2): рыба окна регистрации [468] (−1 — окно не показано) — «двойной щелчок по ячейке книги
	//   под 74200 не перерисовывает 74200 на другую рыбу».
	int32 DebugJoinFish() const { return bJoinOpen ? JoinFish : -1; }
	// ★ 10.10.2026 (modal_lock2): корень клона ячейки сетки (цель настоящего двойного щелчка); nullptr — ячейки нет.
	UWidget* DebugCellRoot(int32 Cell) const { return CellUi.IsValidIndex(Cell) ? CellUi[Cell].Root.Get() : nullptr; }
	// ★ 10.10.2026 (modal_lock2): рыба ячейки текущего тома, если её двойной щелчок откроет 74200 (рыба есть и поймана; условие
	//   `OnCellDoubleClick`); иначе −1.
	int32 DebugCellFish(int32 Cell) const
	{
		const FTpl* T = TplAt(Page);
		const FState* S = StAt(Page);
		if (!T || !S || T->Id == -1 || Cell < 0 || Cell >= kFish || T->Fish[Cell] == -1 || S->Cnt[Cell] <= 0) { return -1; }
		return T->Fish[Cell];
	}
	// Разобрать очередь пакетов немедленно (самотест; в игре это делает Tick).
	void PumpPending() { DrainPackets(); }

private:
	// ── Шаблон тома (раскладка эталона `[459]`, шаг 348) — выбранный набор (сервер/клиент, Р12) ──
	struct FTpl
	{
		int32 Id = -1;             // +0 номер тома (−1 — шаблон недействителен)
		int32 BookItemId = -1;     // +72 книга-предмет
		int32 Flag68 = 0;          // +68 флаг (не читается)
		int32 Fish[30];            // +76 рыбы
		int32 Req[30];             // +196 требуется
		int32 Reward[4];           // +316 награды
		int32 RewardCnt[4];        // +332 количества наград
	};
	// ── Состояние тома (раскладка эталона `[460]`, шаг 436) ──
	struct FState
	{
		bool bReg = false;         // +0 том зарегистрирован
		int32 Id = -1;             // +4 id тома (`fishBookRecord_Reset` ставит −1; 6174 пишет индекс)
		int32 B8 = 0;              // +8 байт 6174 @4 (тип книги сервера; клиент не читает)
		FString Name;              // +12 имя тома (из клиентской таблицы)
		int32 Cnt[30];             // +76 сдано / поймано
		int32 MaxSz[30];           // +196 максимальный размер, см
		int32 MinSz[30];           // +316 минимальный размер, см
	};

	// ── Данные ──
	void EnsureData();                         // загрузить DT_FishBook один раз (`CFishCollection_LoadPictureBookData`)
	void ApplyTableChoice();                   // Р12: заполнить шаблоны из сервера/клиента по ключу (при смене ключа)
	bool bDataLoaded = false;                  // попытка загрузки была
	int32 TableChoice = -1;                    // какой набор сейчас в шаблонах (0 сервер / 1 клиент)
	TArray<FAsdaFishBookRow> Rows;             // строки DT по номеру тома
	TArray<FTpl> Tpl;                          // [459] шаблоны
	TArray<FState> St;                         // [460] состояния
	UPROPERTY(Transient) FAsdaHeldTables HeldTables;   // удержание DT (закон dt_gc_hold)
	const FTpl* TplAt(int32 I) const { return Tpl.IsValidIndex(I) ? &Tpl[I] : nullptr; }       // шаблон тома (nullptr — нет)
	const FState* StAt(int32 I) const { return St.IsValidIndex(I) ? &St[I] : nullptr; }        // состояние тома

	// ── Окно ──
	bool EnsureWidget();                       // ленивое создание окна (`uiWindowInit_74000`, флаг +1832)
	void ResetWidget();                        // забыть виджеты (окно снесено)
	void SetupControls();                      // разбор ВСЕХ контролов окна (1:1 с `uiWindowInit_74000`)
	void RegisterZone();                       // зона окна (корпус `Win_Mg`, 9 планов)
	void RefreshAll();                         // `CItemCollectUI_RefreshAll@0xFD59F0`
	void RebuildTabs(int32 Pick);              // `gameUi_refreshPagedListBox@0xFDA5A0`
	void RefreshProgress();                    // `itemCollectUiRefreshProgress@0xFD3B70` (74102/74104)
	void RefreshGrid();                        // `CItemCollectUI_RefreshGrid@0xFD3E40`
	void RefreshItemDetail();                  // `CItemCollectUI_RefreshItemDetail@0xFD4440` (74062…74070)
	void DrawCountAndRate();                   // `CItemCollectUI_DrawCountAndRate@0xFD4880` (74045…74049)
	void RefreshSelectedDetail();              // `CItemCollectUI_RefreshSelectedDetail@0xFD4C40` (74051…74054)
	void RefreshRewards();                     // `itemCollectUI_RefreshInputSlots@0xFD5020` (74081…74114)
	void OpenJoin(int32 FishId);               // `fishCollectionOpenWindow@0xFD6720`
	void RefreshJoin(int32 FishId);            // `CFishCollection_RefreshDetailUI@0xFD6800`
	void PlaceCursorOn(const UWidget* Cell, bool bLegacyOffset);   // курсор 74241 на стопку (Р7/Д1)
	bool DrawFishFrame(UImage* Img, int32 FishId) const;          // кадр 196×128 атласа `Fish_data +48` (`%s.tex`)
	void DrawDigit(UImage* Img, int32 D) const;                   // цифра `premium_cc.TEX` (32·d, 0, 32×32)
	static FString Trunc13(const FString& S);  // имя > 13 знаков → первые 13 + «...» (эталонная обрезка)
	// ★ fishfix 09.10 (МЕЛК-4): подпись вкладки/награды для поля `Field` — целиком, а не входящая — по ширине поля + «...»
	//   (`textTruncateWithEllipsis@0xBC6E60` эталона); при `asda.FishBook.Trunc13 1` — `Trunc13` (как оригинал).
	//   ★ fishfix2 09.10 (МЕЛК-1): `SidePad` — внутреннее поле с каждой стороны: предел = ширина поля − 2·SidePad (− «...»).
	static FString CaptionCut(const FString& S, const class UTextBlock* Field, float SidePad);
	FString LocRaw(int32 Id) const;            // loc-строка с тегами (общий форматтер)
	FString Loc(int32 Id) const;               // loc-строка без тегов
	void MsgBox(const FString& Text, int32 LocId);   // ONEBUT_MSGBOX (`CItem_BuildTooltipText(…, 0, −1, …)`)
	void ShowConfirm(int32 Ctx, const FString& Text);   // TWOBUT (пара (1, Ctx)): 77 удаление, 79 регистрация
	void OnConfirmAnswer(int32 Ctx, bool bOk); // `OnSendItemUpgrade@0xD74180` ветки 0x4D/0x4F (OK) и 79 (Отмена)
	FString Dialog79Text() const;              // `CItem_BuildTooltipText` case 'O' (111018: имя предмета-книги, имя игрока)
	void TickInput();                          // покадровый опрос: ЛКМ/двойной/наведение (события 0xB/0xD/8/0xA)
	bool IsCursorOverCellHit(int32 I) const;   // курсор над хит-рамкой клона 74029 ячейки I (МЕЛК-5 ревизии В3)
	static double DoubleClickSec();            // окно 0xD = `GetDoubleClickTime` Windows (ВАЖН-6 ревизии В3, правило ревизии О2)
	bool IsJoinCellValid(int32 Cell, int32 MinAmount) const;   // [ПОРТ] LiveRefresh: в ячейке сумки всё ещё рыба окна 74200 (ВАЖН-3)
	bool RevalidateJoinChoice();               // [ПОРТ] LiveRefresh: единое правило [465]/[466]/[467] против сумки (W3fix2 МЕЛК-2); true — что-то снято/урезано
	void HideRewardTip();                      // `CControl_KillFocus(&g_ItemTooltip)` своего владельца

	// ── Сеть ──
	struct FPkt { int32 Op = 0; TArray<uint8> Body; };   // пакет в очереди (порядок прихода сохраняется)
	TAsdaNetQueue<FPkt> PendingPkts;           // очередь из сетевых колбэков (закон no-reentrant-net-callbacks)
	void DrainPackets();                       // разобрать очередь (Tick)
	void Apply6174(const TArray<uint8>& B);    // `listParseRosterEntryPacket@0xFD71E0`
	void SetRosterEntry(int32 Idx, const uint8* Rec305);   // `listSetRosterEntry@0xFD86A0` (Р6: граница `<`)
	void Apply6177(const TArray<uint8>& B);    // `invHandleItemUseResultPacket@0xFD81C0`
	void Apply6179(const TArray<uint8>& B);    // `itemCollectOnRemovePacket@0xFD79B0`
	void Apply6181(const TArray<uint8>& B);    // `netHandleInventoryUpdate_9020@0xFD9020`
	void Apply6183(const TArray<uint8>& B);    // `netHandleInventoryUpdate@0xFD9DF0`
	void ApplyBagRecord(const uint8* Rec57);   // запись предмета 57 б из ответа (общий путь инвентаря)
	void SetWeight(int32 W, const TCHAR* Src); // `HUD_UpdateMiniWeightBar`
	bool Send6176();                           // `acSendChallenge6176@0xFD7B20` по записи [469]
	bool Send6178();                           // `netSendSecuredOp6178@0xFD7360` по тому [463]
	FDelegateHandle H6174, H6177, H6179, H6181, H6183, H6175;   // подписки на сеть
	FDelegateHandle HInvCell;                  // подписка на изменение сумки ([ПОРТ] LiveRefresh)
	FDelegateHandle HCaught;                   // подписка на улов 6172 (автомат В1)
	bool bJoinDirty = false;                   // [ПОРТ] сумка изменилась — перерисовать 74200 в Tick

	// ── Автомат рыбалки (В1) ──
	bool BookRegisterProbe();                  // FSM 101 (g:608344-608352): true — остаёмся в 101
	bool FindFirstValidItem();                 // `inventoryFindFirstValidItem@0xFDAF40` → [469]
	void OnFishCaught(int32 ItemId, int32 SizeCm);   // `questTableUpdateKeyMinMax@0xFDACF0` (без перерисовки)

	// ═══ ВИДЖЕТЫ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;                  // [0] корень 74000
	UPROPERTY(Transient) TArray<FAsdaFishBookCellUi> CellUi;                        // [5…304] 30 клонов ячейки
	UPROPERTY(Transient) TObjectPtr<UWidget> CellTemplate = nullptr;                // 74020 шаблон (прячется, как эталон)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TabPlanes;                      // 74105: планы вкладок (до 4)
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TabButtons;                    // 74105: кнопки вкладок OPT0…3
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TabTexts;                   // 74105: подписи вкладок
	TArray<FSlateBrush> TabIdleBrush;                                               // кэш планов покоя (`ApplyTabState`)
	TArray<FButtonStyle> TabIdleStyle;                                              // кэш стилей кнопок (`ApplyTabState`)
	TArray<FLinearColor> TabIdleCaption;                                            // кэш цветов подписей (`ApplyTabState`)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TopText;   // (4 шт.)    // [305…308] 74101…74104
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> GgText;   // (4 шт.)     // [309…312] 74041…74044
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> Digit;   // (5 шт.)  // [313…317] 74045…74049
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> InfoCap;   // (5 шт.)    // [318…322] 74061/63/65/67/69
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> InfoVal;   // (4 шт.)    // [323…326] 74062/64/66/68
	UPROPERTY(Transient) FAsdaFmtText InfoDesc;                                                          // [327] 74070 описание (теги)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CardName = nullptr;                 // [330] 74051
	UPROPERTY(Transient) TObjectPtr<UImage> CardPic = nullptr;                      // [331] 74052
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CardNo = nullptr;                   // [333] 74053
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CardCnt = nullptr;                  // [334] 74054
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RewardCap = nullptr;                // [335] 74080
	UPROPERTY(Transient) TArray<FAsdaFishBookRewardUi> RewardUi;                    // [336…351] 4 награды
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RewardLabel5 = nullptr;             // 74085 (= подпись 1-й награды; Д2)
	UPROPERTY(Transient) TObjectPtr<UButton> BtnReward = nullptr;                   // [352] 74090
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnRewardText = nullptr;            // подпись 74090 (111012)
	UPROPERTY(Transient) TObjectPtr<UWidget> Join = nullptr;                        // [353] 74200 (группа)
	UPROPERTY(Transient) FAsdaFmtText JoinTitle;                                                         // [354] 74201 (111014, теги)
	UPROPERTY(Transient) TObjectPtr<UImage> JoinPic = nullptr;                      // [355] 74202
	UPROPERTY(Transient) TObjectPtr<UWidget> JoinDone = nullptr;                    // [356] 74203 `GFONT_SU`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinK = nullptr;                    // [357] 74204 «%d/%d»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinR1 = nullptr;                   // [358] 74205 сдано
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinR2 = nullptr;                   // [359] 74206 к сдаче
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinR3 = nullptr;                   // [360] 74207 требуется
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinCaught = nullptr;               // [361] 74210 (111015)
	UPROPERTY(Transient) TArray<FAsdaFishBookInvUi> InvUi;                          // [362…451] 74211…74240
	UPROPERTY(Transient) TObjectPtr<UWidget> JoinCheck = nullptr;                   // [452] 74241 курсор стопки
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JoinHint = nullptr;                 // [453] 74251 (111016)
	UPROPERTY(Transient) TObjectPtr<UButton> BtnJoinOk = nullptr;                   // [454] 74252
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnJoinOkText = nullptr;            // подпись 74252 (12863)
	UPROPERTY(Transient) TObjectPtr<UButton> BtnJoinNo = nullptr;                   // [455] 74253
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnJoinNoText = nullptr;            // подпись 74253 (12864)
	UPROPERTY(Transient) TObjectPtr<UWidget> DelPanel = nullptr;                    // [456] 74270
	UPROPERTY(Transient) TObjectPtr<UButton> BtnDel = nullptr;                      // [457] 74271
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnDelText = nullptr;               // подпись 74271 (111047)
	UPROPERTY(Transient) TObjectPtr<UButton> BtnClose = nullptr;                    // [1] 74099
	UPROPERTY(Transient) TObjectPtr<UButton> BtnLeft = nullptr;                     // [3] 74106
	UPROPERTY(Transient) TObjectPtr<UButton> BtnRight = nullptr;                    // [4] 74107
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;               // реле кнопок

	// ═══ МОДЕЛЬ (поля эталона) ═══
	bool bOpen = false;                // байт +292 корня 74000
	bool bJoinOpen = false;            // байт +292 контрола 74200
	bool bControlsReady = false;       // байт +1832 «init выполнен»
	int32 TabCount = kInitTabs;        // число ячеек списка 74105 (`GetListItemCount`)
	int32 Sel = 0;                     // [462] выбранная ячейка
	int32 Page = 0;                    // [463] текущий том = `g_nUpgradeListIndex`
	int32 TabOfs = 0;                  // [464] сдвиг ленты вкладок
	int32 PickedCell = -1;             // [465] стопка под кликом (ячейка контейнера 2; −1 = 0 эталона)
	int32 ChosenCell = -1;             // [466] подтверждённая стопка (ячейка контейнера 2)
	int32 Qty = 0;                     // [467] количество к сдаче
	int32 JoinFish = 0;                // [468] id рыбы окна регистрации
	int32 BookCell = -1;               // [469] ячейка контейнера 2 книги-предмета для 6176
	int32 RewardSel = -1;              // [470] выбранная награда
	bool bAwaitRegist = false;         // байт +1884 «ждём 6177»
	int32 ActiveList[kActiveCap];      // [472…571] индексы зарегистрированных томов
	int32 ActiveNum = 0;               // [572] их число
	int32 TabSel = 0;                  // [573] выбранная вкладка
	int32 InvCell[kFish];              // [363+3k] ячейка контейнера 2 стопки k окна регистрации (−1 — пусто)

	// ── Ввод ──
	FAsdaMouseFrame Mouse;             // покадровый опрос мыши
	double LastClickTime = 0.0;        // ручное распознавание двойного щелчка (0xD): время прошлого нажатия ЛКМ по ячейке
	int32 LastClickCell = -1;          // ячейка прошлого нажатия ЛКМ (−1 — нет); окно 0xD — `DoubleClickSec()`
	int32 TipReward = -1;              // награда под подсказкой (−1 — нет)

	// ── Самотест ──
	int32 DebugLastNumPadMax = -1;     // максимум последнего открытого нумпада режима 10 (−1 — не открывался)
	double DebugLastClickDelta = -1.0; // интервал последнего нажатия ЛКМ по ячейке от прошлого по ТОЙ ЖЕ ячейке, с (−1 — другая/первая; W3fix2 МЕЛК-3)
};
