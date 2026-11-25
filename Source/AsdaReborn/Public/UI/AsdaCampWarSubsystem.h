// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCampWarSubsystem — «БИТВА АРМИЙ» (клиентское имя CampWar, серверное RoseWar/BattleField): объект оригинала
//   `CCampWar` (статический `g_pCampWarClan = 0x2BFE5C0`, 0x28CC б). Порт 30.10.2026 по решению владельца «вся система
//   CampWar целиком». Полный разбор: регистрация и вход — `Reports/faction_select/REG_SPEC.md`; боевая фаза —
//   `BATTLE_SPEC.md`; общий поток — `FLOW.md`; дампы ассетов — `campwar_wbp_tree.txt`.
//   Выбор армии (окна 44000/46000, 6702/6703) — соседняя `UAsdaFactionSelectSubsystem` (численность армий берёт отсюда).
//
// ═════════════════ ★★★ РЕГИСТРАЦИЯ И ВХОД (этапы «а» и «б») ═════════════════════════════════════
//   [0] Разговор с NPC 133/179/180/184 → 6700 (режим 0) → 6701 → МОДЕЛЬ (окна не трогаются).
//   [1] Кнопка NPC 30057 (180npc, Альпия) / 30074 (179npc, Силарис) / 30075 (133npc, Пламения) → `CCampWar_ShowInfoWindow`
//       (окно 47000 «Битва армий») + `UI_resetAllWindowsAndLeave_5045` (разговор закрыт).
//   [2] 47000: щелчок строки списка битв j → рамка 47430 + 6700(режим j) → 6701 → перерисовка;
//       «Участвовать» 47600 → 6706 → 6707 (11 кодов): 1/2/5 → кнопка-индикатор 49530 + нотис; «Вход на поле боя» 47601 → 6714
//       → 5055; «Закрыть» 47700 / X 47015 / ESC → скрыть (+0x231C := 0).
//   [3] 49530 «Ожидание начала столкновения» → окно 49540 «Ожидание списка участников битвы»: строка 49541 → «Выйти из очереди»
//       49570 → TWOBUT 10036 → «Да» 6708(0) → 6709; «Закрыть» 49571 / X 49599. Сервер: 6710 (место в очереди), 6711 (вызов),
//       6740 (очистка очереди: 0 — минуты в окно NPC, 1/2 — всё сбросить + нотис).
//   [4] 6711 [0]==1 → окно 48099 «Подтверди участие» с отсчётом 30 с (`ClanTimer_Tick` раз в 1000 мс): «Войти в битву» 48050 →
//       6712 → 5055; «Выйти из очереди» 48060 / X 48010 → TWOBUT 100360 → «Да» 6708(1); отсчёт 0 → 6708(1) без вопроса.
//       6711 [0]≠1 → ONEBUT 10044.
//   [5] 5055 код 1 → переход на карту 19; если HUD кампвара разрешён — скрыть 49530 (дальше — боевая фаза); уход С карты
//       кампвара → `CCampWar_ResetState`.
//   Сброс армии предметом категории 78 (TWOBUT 10141 → 6704 → 6705) — тоже здесь (`OnSquadResetItemConfirmed`).
//
// ═════════════════ ДЕФЕКТЫ ОРИГИНАЛА (исправлены с ключами, [ИСПРАВЛЕНИЕ]) ══════════════════════════════
//   Д-КВ-1 (НЕ исправлен, 1:1): окно 49540 пишет имя и место ВСЕХ регионов в контролы строки 0 (дизасм C9BDB4/C9BDEC без
//     индекса), события обрабатываются только для строки 49541, а «Выйти из очереди» шлёт регион ТЕКУЩЕЙ карты. Разнести
//     регионы по строкам нельзя без смены того, ЧТО уходит в 6708 (выход стал бы недоступен для строк 1/2) — оставлено как у эталона.
//   Д-КВ-2: имя битвы региона 2 (Пламения) — `buildLocalPlayerNameStr(2)` берёт запись карты −1 (32 б мусора ПЕРЕД таблицей).
//     [ИСПРАВЛЕНИЕ] имя карты поля боя (19) для любого региона. Ключ `asda.CampWar.Region2Name`.
//   Строки списка битв у эталона наложены в памяти на соседние массивы (`+0x22EC+12j` при j ≥ 1 совпадает с `+0x22F8…`);
//     число строк = OpenGameCount сервера (у таблиц оригинала 1). Порт держит три строки раздельно — при одной строке разницы нет.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Engine/DataTable.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaNetQueue.h"              // TAsdaNetQueue — очередь тел 67xx (закон no-reentrant-net-callbacks)
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "UI/AsdaMarkup.h"             // FAsdaFmtText — строка «загруженности» с тегом <FC=…>
#include "AsdaCampWarSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaButtonRelay;

// Строка таблицы `Data/CampWar_BaseTable.BIN` (`CMap_LoadCampWarBaseTable@0xC9F670`; 3 записи по 836 б, dword −43, символ −1).
//   Источник DataTable `DT_CampWarBase` — `Tools/asda_build_campwar_base_dt.py`. Поля переносятся ВСЕ (резать данные нельзя);
//   эталон в регистрации читает только `@16` (знаменатель «N/макс» окна 47000 и «загруженности» строк списка).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaCampWarBaseRow : public FTableRowBase
{
	GENERATED_BODY()

	// @0 — номер записи (= MapType 0 Альпия / 1 Силарис / 2 Пламения).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") int32 Index = 0;
	// @4 — значение оригинала (200/500/1000); в регистрации не читается.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") int32 Field04 = 0;
	// @8 — значение оригинала (10/25/40); в регистрации не читается.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") int32 Field08 = 0;
	// @12 — значение оригинала (1); в регистрации не читается.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") int32 Field0C = 0;
	// @16 — знаменатель «N/макс» (47120+100i) и доли «загруженности» строк списка битв (`CCampWar_UpdateRankingList`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") int32 Field10 = 0;
	// @20 — значение оригинала (0).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") int32 Field14 = 0;
	// @24 — значение оригинала (12) — число непустых строк времени ниже.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") int32 Field18 = 0;
	// @28 — 12 строк по 32 б («00 : 00» …).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") TArray<FString> Times1;
	// @412 — 12 строк по 32 б («01 : 00» …).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") TArray<FString> Times2;
	// @796 — значение оригинала (1000).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") int32 Field31C = 0;
	// @800 — три dword.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") TArray<int32> Field320;
	// @812 — три dword.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") TArray<int32> Field32C;
	// @824 — три dword.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CampWar") TArray<int32> Field338;
};

// ── Тексты фазы регистрации. Все id сверены с `RawAssets/Text/GameText.csv`; каждый — из своего обработчика. ──
namespace AsdaCampWarLoc
{
	constexpr int32 kJoinTitle    = 10021;   // 47010 «Битва армий» (контрол скрыт в .JUI и остаётся скрытым)
	constexpr int32 kGameCount    = 10022;   // 47030 «Количество битв»
	constexpr int32 kDayCount     = 10023;   // 47070 «Битв в день»
	constexpr int32 kStart        = 10024;   // 47050 «Начало»
	constexpr int32 kEnd          = 10025;   // 47090 «Окончание»
	constexpr int32 kChance       = 10026;   // 47130+100i «Шанс»
	constexpr int32 kEnterBtn     = 10028;   // 47601 «Вход на поле боя»
	constexpr int32 kApplied      = 10029;   // 47403+10j «Подать заявку»
	constexpr int32 kJoinBtn      = 10030;   // 47600 «Участвовать»
	constexpr int32 kWaitStart    = 10032;   // 49530 «Ожидание начала столкновения»
	constexpr int32 kWaitList     = 10033;   // 49573 «Ожидание списка участников битвы»
	constexpr int32 kQuitQueue    = 10034;   // 49570 / 48060 «Выйти из очереди»
	constexpr int32 kCloseList    = 10035;   // 49571 «Закрыть»
	constexpr int32 kStopWait     = 10036;   // TWOBUT «Прекратить ожидание [ss1]?» (id 10036 / 100360)
	constexpr int32 kConfirmTitle = 10037;   // 48000 «Подтверди участие»
	constexpr int32 kTimeLeft     = 10038;   // 48030 «Осталось времени до начала»
	constexpr int32 kEnterWar     = 10039;   // 48050 «Войти в битву»
	constexpr int32 kLoadNormal   = 10040;   // 47402+10j «Обычный»
	constexpr int32 kLoadCrowded  = 10041;   // «Битком»
	constexpr int32 kLoadFull     = 10042;   // «Переполнено»
	constexpr int32 kCanEnterName = 10043;   // 48020 «Ты можешь участвовать в битве армий '[ss1]'. Продолжить?»
	constexpr int32 kCanEnter     = 10044;   // 6711 [0]≠1 — ONEBUT
	constexpr int32 kSd1          = 10045;   // «[sd1]» — 47040 / 47080
	constexpr int32 kBattleList   = 10048;   // 47405 «Список битв»
	constexpr int32 kNoSelect     = 10049;   // 47600/47601 без строки — ONEBUT
	constexpr int32 kNoSelect2    = 10050;   // 47601 без строки при заявке — ONEBUT
	constexpr int32 kFactionBase  = 10191;   // имя армии = индекс + 10191
	constexpr int32 kDayBase      = 10210;   // день недели = 10210 + день (7 — пусто)
	constexpr int32 kLevelCaption = 111302;  // 47061 «Для уровней»
	constexpr int32 kNoEnterHint  = 111947;  // 47602 (★ строки нет ни в RU, ни в EN — показывается то, что даст таблица)
	constexpr int32 kSquadReset   = 10141;   // TWOBUT «Сменить армию?» (предмет категории 78)
	constexpr int32 kClearMinutes = 10201;   // 6740 [0]==0 — «…Осталось %d мин.» в окно NPC
	constexpr int32 kClose        = 5035;    // 47700 «Закрыть»
	// Итоги 6707 по коду 0…8 (код 9 → 111938, код 10 → 111301 «уровни %d-%d»; оба с id окна 10159).
	constexpr int32 kRegistResult[9] = { 10151, 10157, 10152, 10153, 10154, 10155, 10156, 10158, 10159 };
	constexpr int32 kKicked       = 111938;
	constexpr int32 kLevelRange   = 111301;
	// Итоги 6709 по коду 0…4.
	constexpr int32 kCancelResult[5] = { 10161, 10162, 10163, 10164, 10165 };
	// Итоги 6705 по коду: 0/2/3 → 10143, 1 → 10142, 4 → 10144, 5 → 10145.
	constexpr int32 kResetOk      = 10142;
	constexpr int32 kResetFail    = 10143;
	constexpr int32 kResetFail4   = 10144;
	constexpr int32 kResetFail5   = 10145;
	// 6740 коды 1/2.
	constexpr int32 kQueueCleared = 10202;
	constexpr int32 kFieldClosed  = 10203;
	// 6738 — «[армия] …» (в таблице `$d`, а не `%d`: подстановки нет — дефект оригинала, оставлен).
	constexpr int32 kHolder       = 10111;
}

UCLASS()
class ASDAREBORN_API UAsdaCampWarSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписка на пакеты 67xx (`OnCampWarSquadPacket`), роспись ESC окна 47000, таблица `DT_CampWarBase`.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписка, снятие виджетов.
	virtual void Deinitialize() override;
	// Сброс сессии: окна скрыты, модель — как после `CCampWar_ctor` (смена карты НЕ сбрасывает — у эталона объект статический).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Очередь пакетов, `ClanTimer_Tick` (раз в 1000 мс), опрос мыши строк-панелей.
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCampWarSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	static constexpr int32 kRegions  = 3;     // регионов (MapType) и армий у оригинала ровно три
	static constexpr int32 kRows     = 3;     // строк списка битв окна 47000 (47400/47410/47420)
	static constexpr int32 kJoinZ    = 700;   // окно 47000 (уровень окон сообщества)
	static constexpr int32 kIngZ     = 690;   // кнопка-индикатор 49530
	static constexpr int32 kInListZ  = 705;   // окно 49540
	static constexpr int32 kConfirmZ = 720;   // окно 48099
	static constexpr int32 kCountdownStart = 30;   // `uiCampWarPointWndShow`: +0x2324 = 30
	static constexpr float kBarMax   = 100.f; // `CProgressBar_ctor`: диапазон 0…100.0

	// ═══ ВХОДЫ ═══
	// Кнопки NPC 30057/30074/30075 — порт `CCampWar_ShowInfoWindow@0xC9ADF0` (разговор закрывает вызывающий).
	void OpenJoinWindow();
	// Порт `sub_C9B2C0`: X 47015, «Закрыть» 47700, ESC — скрыть 47000, +0x231C = 0.
	void CloseJoinWindow();
	// Сетевой слой: 5055 код 1 (переход). bHudOn — HUD кампвара разрешён (`(+51 & 0x80) == 0 && карта ≠ 23`).
	//   Состояние «уходим С карты кампвара» снимается здесь же (до смены карты). Разбор — в Tick.
	//   ★ 30.10.2026 (боевая фаза): TargetMapId — карта назначения; `bCampWarVisible = (+69 записи карты-цели == 1)` — второй
	//   аргумент `campWarWnd_ToggleVisible` (выйти из отряда, G:556112/556116). −1 — карта неизвестна (не выходить).
	void OnPortalTransfer5055(bool bHudOn, int32 TargetMapId = -1);
	// Предмет категории 78 (сброс армии) — после «Да» на TWOBUT 10141 шлёт 6704 (`CItemManager_UseInventoryItem@0xA0FDF0`).
	//   Аргументы — как в эталонном буфере +0x23AC: dword предмета, a4, a2.
	void OnSquadResetItemConfirmed(int32 ItemDword, uint8 A4, int32 A2);
	// Порт `CCampWar_ResetState@0xC96F20` — общий сброс (зовут 6709/6740/6737/5055 и боевая фаза).
	void ResetState();

	// ═══ МОДЕЛЬ (читают окно выбора армии и боевая фаза) ═══
	bool HasMainInfo() const { return bMainInfoReceived; }                                              // 6701 приходил
	int32 GetTeamCount(int32 Squad) const { return IsReg(Squad) ? (int32)TeamCount[Squad] : -1; }       // +0xBAF+2i
	int32 GetMapType() const { return (int32)MapType; }                                                 // +0xBAE
	int32 GetSelRow() const { return SelRow; }                                                          // +0x231C
	int32 GetCallRegion() const { return CallRegion; }                                                  // +0x230C
	int32 GetCountdown() const { return Countdown; }                                                    // +0x2324
	bool IsCountdownOn() const { return bCountdown; }                                                   // +0x2328
	int32 GetWaitBefore(int32 K) const { return IsReg(K) ? WaitBefore[K] : -2; }                        // +0x22F8+4k
	int32 GetRegGameIndex(int32 K) const { return IsReg(K) ? RegGameIndex[K] : -2; }                    // +0x22D4+4k
	int32 GetRegIndex(int32 K) const { return IsReg(K) ? RegIndex[K] : -2; }                            // +0x22E0+4k
	// ★ 09.11.2026: сырые поля для портального входа 5058 — эталон `sub_F489B0` (+0x22D4) / `sub_F48980` (+0x22E0) читает их без
	//   проверки заявки; без заявки там −1 (иначе сервер в CharPortalLogonReq считает игрока записанным в очередь слота 0).
	int32 GetRawRegGameIndex(int32 K) const { return (K >= 0 && K < 3) ? RegGameIndex[K] : -1; }
	int32 GetRawRegIndex(int32 K) const { return (K >= 0 && K < 3) ? RegIndex[K] : -1; }
	int32 GetLastRegIndex() const { return (int32)LastRegIndex; }                                       // +0xB80
	bool IsRowApplied(int32 J) const { return J >= 0 && J < kRows && RowApplied[J] != 0; }               // +0x22F4+12j
	// Строка `DT_CampWarBase` слота (nullptr — нет; эталон `sub_CA1440`).
	const FAsdaCampWarBaseRow* GetBaseRow(int32 Slot) const;
	// Слот региона по текущей карте (3 → 0, 0 → 1, 7 → 2, иначе 0) — `CCampWar_ShowInfoWindow` и др.
	int32 CurrentMapSlot() const;
	// Имя битвы (`buildLocalPlayerNameStr@0xCA1580`, имя врёт): служебное имя карты 19 для регионов 0/1 (Д-КВ-2 — и для 2).
	FString BattleName(int32 Region) const;
	// ★ Боевая фаза: таймер боя идёт (+0x2334) — `ClanTimer_Tick(1)` проходит блок 48099 второй раз.
	void SetBattleTimerOn(bool bOn) { bBattleTimer = bOn; }
	// ★ 30.10.2026 (боевая фаза): таймер боя идёт (+0x2334).
	bool IsBattleTimerOn() const { return bBattleTimer; }
	// ★ 30.10.2026 (боевая фаза): расписание 6701 — начало +0x2338/+0x233C, конец +0x2340/+0x2344 (читают 6736 и код 8 6728).
	int32 GetStartH() const { return StartH; }
	int32 GetStartM() const { return StartM; }
	int32 GetEndH() const { return EndH; }
	int32 GetEndM() const { return EndM; }
	// ★ 30.10.2026 (боевая фаза): 6739 `campWarRankingApply` пишет конец +0x2340/+0x2344 (у оператора).
	void SetEndTime(int32 H, int32 M) { EndH = H; EndM = M; }
	// ★ 30.10.2026 (боевая фаза): `ClanTimer_Tick(this, Mode)` вызовом извне (6736 зовёт режим 1 сразу после записи таймера).
	void StepClanTimer(int32 Mode) { ClanTimerTick(Mode); }
	// ★ 30.10.2026 (боевая фаза): 6715 разбирает бой (одна очередь с 6716/6721 — порядок пакетов сохраняется); своя запись → +0xB80 = −1.
	void OnSelfInSquadList() { LastRegIndex = -1; }
	// ★ 30.10.2026 (боевая фаза): 6737 — `uiCtrlHideAtField374` (49530) + `sub_C9BF10` (49540); разбор 6737 у боя (та же очередь).
	void HideWaitWindows() { HideIng(); CloseInList(); }

	// ═══ ДЕЙСТВИЯ (боевые обработчики кнопок и событий строк; самотест зовёт их же через клики Slate) ═══
	void OnRowClick(int32 Row);          // событие 0xB по 47400+10j → `campWarSlotShowControl` + 6700(режим j)
	void OnRowHover(int32 Row, bool bOn);   // события 8/0xA → `campWarSlotResetAndShow`
	void OnJoinParticipate();            // 47600 → `Clan_Send_Op6706`
	void OnJoinEnter();                  // 47601 → `Clan_Send_6714`
	void OnIngClick();                   // 49530 → `uiCampWarWindowShow` (окно 49540)
	void OnInListRowClick();             // 0xB по 49541 → `campWarPointWnd_SelectShow` (имя врёт)
	void OnInListRowHover(bool bOn);     // 8/0xA по 49541 → `sub_C9BF50`
	void OnInListQuit();                 // 49570 → TWOBUT 10036
	void CloseInList();                  // 49571 / X 49599 → `sub_C9BF10`
	void OnConfirmEnter();               // 48050 → скрыть 48099 + 6712
	void OnConfirmQuit();                // 48060 / X 48010 → TWOBUT 100360
	void SendCancel6708(int32 Mode);     // `Clan_Send_Op6708@0xCA28F0` (0 — регион карты, 1 — регион вызова + скрыть 48099)

	// ═══ ДИАГНОСТИКА / САМОТЕСТ ═══
	bool IsJoinOpen() const { return bJoinOpen; }          // 47000 видно
	bool IsIngShown() const { return bIngShown; }          // 49530 видно
	bool IsInListOpen() const { return bInListOpen; }      // 49540 видно
	bool IsConfirmOpen() const { return bConfirmOpen; }    // 48099 видно
	int32 GetInListSel() const { return InListSel; }       // +0x2320
	void DebugSetCountdown(int32 Sec) { Countdown = Sec; }   // самотест: дотянуть отсчёт 48099 до конца без 30 с ожидания
	FString GetText(int32 IdNum) const;                    // текст контрола по IDNum (окна 47000/49530/49540/48099)
	bool IsShown(int32 IdNum) const;                       // видим ли контрол (затемнения 47101+j, 49549+10i, значки 47425+j, рамки)
	float GetBarFrac(int32 Squad) const;                   // доля заливки полосы 47150+100i (−1 — нет)
	UWidget* GetHitWidget(int32 IdNum) const;              // кнопка/строка по IDNum (для кликов самотеста)
	FVector2D GetSelFramePos() const;                      // позиция рамки 47430 (левый верх первого плана) в канвасе списка
	const TArray<int32>& GetNoticeTrace() const { return NoticeTrace; }   // loc-id показанных нотисов
	FString GetLastNoticeText() const { return LastNoticeText; }          // текст последнего нотиса
	void ClearTrace() { NoticeTrace.Reset(); LastNoticeText.Reset(); }    // сбросить след
	FString DumpControls() const;                          // все разобранные контролы окон
	int32 GetFoundControls() const { return FoundControls; }   // разобрано
	int32 GetTotalControls() const { return TotalControls; }   // ожидалось

private:
	static bool IsReg(int32 K) { return K >= 0 && K < kRegions; }
	// ── Окна ──
	bool EnsureJoin();                 // каркас окна 47000
	bool EnsureIng();                  // каркас кнопки 49530
	bool EnsureInList();               // каркас окна 49540
	bool EnsureConfirm();              // каркас окна 48099
	void ResetWidgets();               // снять ссылки на все виджеты
	void SetupJoin();                  // порт `CCampWar_InitJoinInfo@0xC97890`
	void SetupIng();                   // порт `CCampWar_InitInList` (часть 49530)
	void SetupInList();                // порт `CCampWar_InitInList@0xC98BC0` (часть 49540)
	void SetupConfirm();               // порт `CCampWar_InitJoinConfirm@0xC988D0`
	void FillJoin();                   // тело `CCampWar_ShowInfoWindow` после показа
	void UpdateRankingList();          // порт `CCampWar_UpdateRankingList@0xC9B300`
	void SlotShowControl(int32 Row);   // порт `campWarSlotShowControl@0xC9BAF0`
	void SlotsHideAll();               // порт `campWarSlotsHideAll@0xC9B880`
	void ShowIng();                    // порт `uiCampWarWindow_Show@0xC9BC10`
	void HideIng();                    // порт `uiCtrlHideAtField374@0xC9BC90`
	void ShowInList();                 // порт `uiCampWarWindowShow@0xC9BCD0`
	void InListClearSel();             // порт `sub_C9C110` (+0x2320 = −1, рамка 49565 скрыта)
	void ShowConfirm();                // порт `uiCampWarPointWndShow@0xC9C150` (имя врёт)
	void HideConfirm();                // порт `uiCampWarPointWndHide@0xC9C280` (имя врёт)
	void ClanTimerTick(int32 Mode);    // порт `ClanTimer_Tick@0xCA18D0` (блок 48099)
	void TickMouse();                  // события 8/0xA/0xB по строкам-панелям 47400+10j и 49541
	void RegisterZones();              // зоны окон (клики не уходят в мир)
	// ── Сеть ──
	void HandlePacket(int32 Op, const TArray<uint8>& B);   // разбор одного тела (из Tick)
	void OnMainInfo(const TArray<uint8>& B);        // 6701 `Recv_ClanInfo@0xCA3C60`
	void OnSquadReset(const TArray<uint8>& B);      // 6705 `Recv_ClanStorageItem@0xCA4090` (имя врёт)
	void OnRegistResult(const TArray<uint8>& B);    // 6707 `Recv_ClanRankResult@0xCA42C0`
	void OnCancelResult(const TArray<uint8>& B);    // 6709 `Recv_ClanKickResult@0xCA4810` (имя врёт)
	void OnWaitInfo(const TArray<uint8>& B);        // 6710 `Recv_ClanMemberPoint@0xCA49F0` (имя врёт)
	void OnCallResult(const TArray<uint8>& B);      // 6711 `Recv_ClanApplyResult@0xCA4A30` (имя врёт)
	void OnQueueClear(const TArray<uint8>& B);      // 6740 `uiCampWarRankingResultHandler@0xCA7500`
	void OnMemberListBulk(const TArray<uint8>& B);  // 6715 `Recv_ClanMemberListBulk@0xCA4B50` (своя запись → +0xB80 = −1)
	void OnBattleReset(const TArray<uint8>& B);     // 6737 inline-ветка диспетчера: ResetState + Toggle(0,0) + скрыть 49530/49540
	void OnArmyHolder(const TArray<uint8>& B);      // 6738 `campWarSetFactionFlagTooltip@0xCA73B0` (+0x2354+2·армия = 1, нотис 10111)
	bool SendBody(uint16 Op, const TArray<uint8>& Body, const TCHAR* What);   // общий отправитель Net::SendCampWar
	TArray<uint8> MakeHead(int32 Len) const;        // тело с [0..3] charId, [4] слот персонажа
	// ── Прочее ──
	void Notice(int32 LocId, const FString& Text, int32 EtalonId);   // ONEBUT `CItem_BuildTooltipText(…, 0, id, …)` + след
	void NoticeLoc(int32 LocId);                     // ONEBUT по loc-id (id окна = loc)
	FString Loc(int32 LocId) const;                  // loc-строка (теги срезаны)
	FString LocSs1(int32 LocId, const FString& Ss1) const;   // loc-строка с подстановкой [ss1]
	FString LocSd1(int32 LocId, int32 Sd1) const;            // loc-строка с подстановкой [sd1]

	// ═══ ВИДЖЕТЫ ОКНА 47000 ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> JoinWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JTitle = nullptr;        // 47010 (скрыт)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JGameCap = nullptr;      // 47030
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JGameVal = nullptr;      // 47040
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JDayCap = nullptr;       // 47070
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JDayVal = nullptr;       // 47080
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JDay1 = nullptr;         // 47096
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JDay2 = nullptr;         // 47097
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JStartCap = nullptr;     // 47050
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JStartVal = nullptr;     // 47060
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JLevelCap = nullptr;     // 47061
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JLevelVal = nullptr;     // 47062
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JEndCap = nullptr;       // 47090
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JEndVal = nullptr;       // 47095
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JListCap = nullptr;      // 47405
	UPROPERTY(Transient) TObjectPtr<UImage>     JMark[3];                // 47100+100i
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JName[3];                // 47110+100i
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JCount[3];               // 47120+100i
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JChance[3];              // 47130+100i
	UPROPERTY(Transient) TObjectPtr<UImage>     JBarBack[3];             // 47150+100i ES0
	UPROPERTY(Transient) TObjectPtr<UImage>     JBarFill[3];             // 47150+100i ES1
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JPct[3];                 // 47140+100i
	UPROPERTY(Transient) TObjectPtr<UWidget>    JRow[3];                 // 47400+10j (панель строки)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JRowNum[3];              // 47432+j
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JRowName[3];             // 47401+10j
	UPROPERTY(Transient) FAsdaFmtText JRowState[3];                      // 47402+10j (текст с тегом <FC=…>)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JRowApply[3];            // 47403+10j
	UPROPERTY(Transient) TObjectPtr<UWidget>    JRowAni[3];              // 47425+j (анимированный значок)
	UPROPERTY(Transient) TObjectPtr<UImage>     JRowHover[3];            // 47404+10j
	UPROPERTY(Transient) TObjectPtr<UImage>     JRowBlack[3];            // 47101+j
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> JSelFrame;           // 47430 — 8 планов рамки
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JHint = nullptr;         // 47602
	UPROPERTY(Transient) TObjectPtr<UButton>    JBtnJoin = nullptr;      // 47600
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JBtnJoinText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    JBtnEnter = nullptr;     // 47601
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JBtnEnterText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    JBtnClose = nullptr;     // 47700
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JBtnCloseText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    JBtnX = nullptr;         // 47015
	FVector2D JSelFrameBase[8];        // исходные позиции планов рамки 47430 (в канвасе списка)
	FVector2D JRowBase[3];             // исходные позиции строк 47400+10j (в канвасе списка)
	float BarFrac[3] = { 0.f, 0.f, 0.f };   // доля заливки полос 47150+100i (диагностика)

	// ═══ ВИДЖЕТЫ 49530 ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> IngWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    IngBtn = nullptr;        // 49530 `CAMPWAR_Ing`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IngText = nullptr;       // подпись 49530

	// ═══ ВИДЖЕТЫ 49540 ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> InListWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LTitle = nullptr;        // 49573
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> LSelFrame;           // 49565 — планы рамки
	UPROPERTY(Transient) TObjectPtr<UButton>    LBtnQuit = nullptr;      // 49570
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LBtnQuitText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    LBtnClose = nullptr;     // 49571
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LBtnCloseText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    LBtnX = nullptr;         // 49599
	UPROPERTY(Transient) TObjectPtr<UWidget>    LRow[3];                 // 49541+10i
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LName[3];                // 49542+10i
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LPlace[3];               // 49543+10i
	UPROPERTY(Transient) TObjectPtr<UImage>     LHover[3];               // 49544+10i
	UPROPERTY(Transient) TObjectPtr<UImage>     LBlack[3];               // 49549+10i

	// ═══ ВИДЖЕТЫ 48099 ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ConfirmWidget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CTitle = nullptr;        // 48000
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CMsg = nullptr;          // 48020
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CTimeCap = nullptr;      // 48030
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CTime = nullptr;         // 48040
	UPROPERTY(Transient) TObjectPtr<UButton>    CBtnEnter = nullptr;     // 48050
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CBtnEnterText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    CBtnQuit = nullptr;      // 48060
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CBtnQuitText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>    CBtnX = nullptr;         // 48010

	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;   // реле кнопок всех окон (GC-якорь)
	UPROPERTY(Transient) TObjectPtr<UDataTable> BaseTable = nullptr;    // `DT_CampWarBase`
	int32 FoundControls = 0;           // разобрано контролов
	int32 TotalControls = 0;           // ожидалось контролов
	bool bJoinReady = false;           // контролы 47000 разобраны
	bool bIngReady = false;            // кнопка 49530 разобрана
	bool bInListReady = false;         // контролы 49540 разобраны
	bool bConfirmReady = false;        // контролы 48099 разобраны
	bool bJoinOpen = false;            // 47000 видно (`byte[+0x17C]+0x124`)
	bool bIngShown = false;            // 49530 видно
	bool bInListOpen = false;          // 49540 видно
	bool bConfirmOpen = false;         // 48099 видно

	// ═══ МОДЕЛЬ — поля `CCampWar` (смещения проверены дизасмом, REG_SPEC.md §2.0) ═══
	int8  MapType = 0;                 // +0xBAE  регион 0..2 (6701 b[18])
	int16 TeamCount[3] = { 0, 0, 0 };  // +0xBAF  численность армий (окно 44000)
	int16 WinCount[3] = { 0, 0, 0 };   // +0xBB5  побед армий («Шанс»)
	int16 LoseCount[3] = { 0, 0, 0 };  // +0xBBB  поражений
	int16 TotalGame = 0;               // +0xBC1  всего битв (47040 и знаменатель «Шанса»)
	int8  DayGame = 0;                 // +0xBC3  битв сегодня (47080 = значение + 1)
	int32 StartH = 0, StartM = 0;      // +0x2338/+0x233C начало
	int32 EndH = 0, EndM = 0;          // +0x2340/+0x2344 конец
	int32 LevelMin = 0, LevelMax = 0;  // +0x2348/+0x234C уровни
	int32 Day = 0;                     // +0x2350 день недели (7 — без дня)
	int16 GameCount[3] = { 1, 1, 1 };  // +0x2354 число битв (строк) в регионе
	int32 RegistCount[3] = { 0, 0, 0 };    // +0x235C заявок в армию i
	int32 RegGameIndex[3] = { -1, -1, -1 }; // +0x22D4 GameIndex заявки в регион k
	int32 RegIndex[3] = { -1, -1, -1 };     // +0x22E0 RegistIndex заявки в регион k
	int32 RowRegion[3] = { -1, -1, -1 };    // +0x22EC k, записанный в строку j (чтений в эталоне нет)
	int32 RowLoad[3] = { 0, 0, 0 };         // +0x22F0 числитель «загруженности» (эталон не пишет — всегда 0)
	uint8 RowApplied[3] = { 0, 0, 0 };      // +0x22F4 заявка подана в строку j
	int32 WaitBefore[3] = { -1, -1, -1 };   // +0x22F8 стоящих в очереди перед тобой (регион k)
	int16 LastRegIndex = -1;           // +0xB80 RegistIndex последней удачной заявки
	int32 CallRegion = -1;             // +0x230C регион вызова 6711
	int32 SelRow = -1;                 // +0x231C выбранная строка 47000
	int32 InListSel = -1;              // +0x2320 выбранная строка 49540
	int32 Countdown = 0;               // +0x2324 секунды отсчёта 48099
	bool bCountdown = false;           // +0x2328 отсчёт идёт
	bool bBattleTimer = false;         // +0x2334 таймер боя идёт (боевая фаза)
	bool bMainInfoReceived = false;    // 6701 приходил (сессия)
	TArray<uint8> LastMainInfo;        // последнее тело 6701 (44 б)
	double LastClanTickMs = 0.0;       // `g_dwClanTickLastTime`
	int32 PendingPortal = 0;           // 5055: 0 — нет, 1 — HUD вкл., 2 — HUD выкл. (разбор в Tick)
	bool bPendingLeaveParty = false;   // 5055: карта-цель кампварная (+69 == 1) — `ToggleVisible(…, a3 = 1)` (выйти из отряда)
	bool bPendingLeaveCampMap = false; // 5055 пришёл, когда мы стояли на карте кампвара (`g_pLocalEntity+69 == 1`)
	FAsdaMouseFrame Mouse;             // опрос мыши кадра
	int32 HoverRow = -1;               // строка 47000 под курсором (события 8/0xA)
	bool bInListHover = false;         // строка 49541 под курсором
	TAsdaNetQueue<TPair<int32, TArray<uint8>>> Pending;   // тела 67xx из сети (разбор — в Tick)
	FDelegateHandle HPacket;           // подписка на `OnCampWarSquadPacket`
	FDelegateHandle HSelfFaction;      // подписка на `OnSelfFaction` (6713): очки +0xB82 и звание +0xB86
	int32 LastSelfPoints = 0;          // +0xB82 очки армии из последнего 6713
	int32 LastSelfGrade = 0;           // +0xB86 звание из последнего 6713
	TArray<int32> NoticeTrace;         // след нотисов
	FString LastNoticeText;            // текст последнего нотиса
};
