// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishingSubsystem — СЕТЬ И КОНЕЧНЫЙ АВТОМАТ РЫБАЛКИ (волна В1, 08.10.2026).
//   Реверс (источник истины): `Reports/owner_1008/fishing_reverse.md` (§2 FSM, §4 сеть, §5 нотисы, §8 дефекты)
//   и части `Reports/owner_1008/fishing_parts/{client_net,server,ui_fishwin,world_anim}.md`.
//   Отчёт волны: `Reports/owner_1008/fishing_impl_W1.md`.
//
// ═════════════════════ ЧТО ЭТО В ЭТАЛОНЕ ═══════════════════════════════════════════════════════════
//   Объект `g_pMap` (0x351FA70) — НЕ «карта», а менеджер рыбалки. Его поля, которые здесь портированы:
//     +0x870 (0x35202E0, МИСНЕЙМ `g_pChatInputWnd`) — fishState, состояние автомата 0/100…128;
//     +0x874/+0x878/+0x87C — fs[1] (начало 100), fs[2] = `g_dwActionTimeMs` (начало 121), fs[3] (начало 120);
//     +0x884 (0x35202F4) — пауза между раундами (кнопка 71329), +0x885 (0x35202F5) — автоповтор (галка 71349);
//     +0x888 (0x35202F8, МИСНЕЙМ `g_pInventoryMgr`) — контекст авто-снаряжения удочки/наживки;
//     +0x8A0 (0x3520310, `g_pPickedMapObject`) — выбранная зона; +0x8B8 — очередь 6173 чужих;
//     +0x8C4 (0x3520334, МИСНЕЙМ `g_InputEditNumBuf`) — шкала улова {value, aux, max = 500};
//     +0x8D0 (0x3520340, `g_pNetSession_340`) — копии пакетов 6168 (+1, 7 б) и 6172 (+8, 121 б).
//   Драйвер — `charUpdateFishingAndState@0xFC3270` каждый кадр: `Fishing_UpdateStateMachine@0xFCEE90`
//   (g:608234) + разбор очереди 6173 `charApplyStateFromList@0xFCAC60` (g:606480).
//   Инициализация — `cmapInitReset@0xFC3170` (g:603046, из `LoadAllInterfaces2` стадия 1 = ОДИН РАЗ за процесс):
//   fishState = 0, пауза = 0, ★ АВТОПОВТОР = 1 (`gap_10[2165] = 1` = 0x35202F5; прежний разбор ui_fishwin §1
//   «по умолчанию 0 (BSS)» НЕВЕРЕН — второй метод: смещение 0x10+2165 = 0x885 от 0x351FA70).
//
// ═════════════════════ ЧТО СДЕЛАНО В ЭТОЙ ВОЛНЕ (В1) ═══════════════════════════════════════════════
//   • данные: DT_FishData / DT_FishNum / DT_FishSpots (Tools/asda_build_fishing_dt.py из клиентских .BIN);
//   • все состояния FSM §2 сводки (100…128, в т.ч. ожидающие 104/106/107/111/118) 1:1 по g:608234-608630;
//   • сеть: 6167/6169/6171 (через `UAsdaNetSubsystem`), приём 6164/6168/6170/6172/6173 (+6183 для Р2);
//   • диалоги 80/81/82/83 через общий TWOBUT (`UAsdaWindowSubsystem::ShowGameConfirm`, пара (1, ctx));
//   • авто-снаряжение (103/105/128) через общий «использовать из сумки» (`UseFromBagWindow` = `CInventory_OnSlotUse`);
//   • переходы по концу клипов 60…67 (`OnFishingClipEnd`) и [ПОРТ]-мост таймером до волны В5;
//   • дефекты оригинала Р1–Р5, Р9, Р10 (таблица §8 сводки), каждый с ключом отката (кроме безключевых по §8).
//   НЕ здесь: окно 71300 и мини-игра (В2 — реализует `IAsdaFishMiniGame`), книга (В3 — `BookRegisterProbe`),
//   зоны-актёры/меню/гейты (В4 — `PickSpot`/`RequestStart109`/`IsFishing`), анимации (В5 — `OnSelfFishAction`,
//   `OnPeerFishState`, `OnFishingClipEnd`), строка навыка окна персонажа (В6 — `OnFishSkillChanged`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Engine/DataTable.h"
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь пакетов из сетевого колбэка (★ N10: ядро, не UI-заголовок)
#include "AsdaSessionReset.h"      // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"        // FAsdaHeldTables — удержание DT от сборщика мусора
#include "AsdaFishingSubsystem.generated.h"

// ═════════════════════ СТРОКИ ТАБЛИЦ (RowStruct'ы DT_Fish*) ══════════════════════════════════════

// Запись `Fish_data.BIN` (184 б, `FishData_LoadFromBin@0xFD01E0`). Ключ строки DT = Order (порядок файла).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaFishDataRow : public FTableRowBase
{
	GENERATED_BODY()
	// Порядок записи в файле (0…85). ★ Значим: `fishGameFindRecordById@0xFD03C0` берёт ПЕРВУЮ с кодом.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Order = 0;
	// +0 код предмета рыбы (= FishCode пакета 6168 и id предмета улова).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 FishCode = 0;
	// +4 тип 0..4 (0 золотая, 1/2/3 обычные, 4 мусор `fs_stuff_00`); −1 у неиспользуемых строк.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 FishType = -1;
	// +8 раундов мини-игры (FSM 114 `g:608531`); < 0 → эталон берёт 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Rounds = -1;
	// +12 мс на раунд (ключ-гайд, `g:605260-605264`); нет записи → 30000.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 RoundMs = -1;
	// +16…+44 — восемь i32, клиентом в найденных путях не читаются; по виду совпадают с серверной
	//   Fishing_FishInfo (наживки «хорошая/плохая», тип и значение еды) — `?`, переносятся как есть.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Raw16 = 0;   // +16 `?` (у всех 69670)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Raw20 = 0;   // +20 `?`
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Raw24 = 0;   // +24 `?`
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Raw28 = 0;   // +28 `?`
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Raw32 = 0;   // +32 `?`
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Raw36 = 0;   // +36 `?`
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Raw40 = 0;   // +40 `?` (у рыб 1)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Raw44 = 0;   // +44 `?` (5…65)
	// +48 имя текстуры карточки (64 б, напр. `s_fish_l_00`; «-1» у пустых строк).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") FString Texture;
	// +112 колонка клетки в атласе текстуры.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 TexCol = -1;
	// +116 строка клетки в атласе текстуры.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 TexRow = -1;
	// +120 имя модели рыбы на крючке (64 б, напр. `Fs_fsm_crucian_00`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") FString Model;
};

// Запись `Fish_num.BIN` (12 б, `CFishGame_LoadFishNumTable@0xFD0440`): {min, max, value}.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaFishNumRow : public FTableRowBase
{
	GENERATED_BODY()
	// Порядок в файле — `rangeTableLookupValue@0xFD0570` берёт ПЕРВЫЙ подходящий диапазон.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Order = 0;
	// Нижняя граница (включительно).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Min = 0;
	// Верхняя граница (включительно).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Max = 0;
	// Значение (число стрелок в раунде мини-игры).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Value = 1;
};

// Элемент таблицы спотов (152 б в памяти эталона, `CMap_LoadMapInfoAndFishAreas@0xFCFC00`).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaFishSpotRow : public FTableRowBase
{
	GENERATED_BODY()
	// Индекс в общей таблице (порядок «карта по файлу → слот 0..9», пустые слоты пропущены) = `+460` объекта зоны.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Order = 0;
	// +0 id карты (Map_info +0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 MapId = -1;
	// +4 X спота, тайлы (мир = ×100), Map_info +0xC0[j].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 X = 0;
	// +8 Y(= мировой Z) спота, тайлы, Map_info +0xE8[j].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Y = 0;
	// +12 радиус, тайлы (= метры; клиент — круг r·100 по XZ), Map_info +0x110[j].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Radius = 0;
	// +16 требуемое умение спота (клиент читает только как ключ Fish_num), Map_info +0x160[j].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Skill = 0;
	// Индекс области Fish_area_data (Map_info +0x138[j]) — откуда взяты имена ниже.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 AreaIdx = -1;
	// +20 имя (Fish_area_data +4); у всех областей `Fs_zone_00`, клиентом больше нигде не читается.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") FString AreaName1;
	// +84 имя (Fish_area_data +68); то же.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") FString AreaName2;
	// +148 номер слота 0..9 = поле Spot пакета 6167 (сервер проверяет зону ИМЕННО по нему).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Рыбалка") int32 Slot = 0;
};

// Состояние ЧУЖОГО рыбака, собранное из 6173 (`charApplyStateFromList`): то, что эталон пишет в поля
//   `CCharacter[1].gap_A42` найденного по charId персонажа. Анимацию по нему строит волна В5.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaFishPeerState
{
	GENERATED_BODY()
	// charId персонажа (i32, НЕ userNum — закон charid-vs-usernum).
	UPROPERTY(BlueprintReadOnly, Category="Рыбалка") int32 CharId = 0;
	// Последняя фаза 6173 [0] (1 старт/итог-флаг, 2 сброс, 3 подсечка, 0 провал/сорвалась).
	UPROPERTY(BlueprintReadOnly, Category="Рыбалка") int32 Phase = 0;
	// [15036] флаг «рыбачит» = (фаза ≠ 0 && фаза ≠ 2).
	UPROPERTY(BlueprintReadOnly, Category="Рыбалка") bool bFishingFlag = false;
	// [15076] FishCode / id пойманного (6173 @5).
	UPROPERTY(BlueprintReadOnly, Category="Рыбалка") int32 FishCode = 0;
	// [15068] размер рыбы, % (6173 @9).
	UPROPERTY(BlueprintReadOnly, Category="Рыбалка") int32 SizePct = 100;
	// [15066] рыба на крючке = (FishCode ≠ 0).
	UPROPERTY(BlueprintReadOnly, Category="Рыбалка") bool bHasFish = false;
	// Последнее выставленное этой записью действие (60…67 / 0; −1 — запись действие не меняла).
	UPROPERTY(BlueprintReadOnly, Category="Рыбалка") int32 Action = -1;
};

// ★★★ ТОЧКА РАСШИРЕНИЯ ДЛЯ ВОЛНЫ В2 — ОКНО 71300 И МИНИ-ИГРА (ключ-гайд `CTutorialKeyGuide`).
//   FSM зовёт ровно те функции окна/гайда, что зовёт эталон, в тех же состояниях. В2 реализует интерфейс и
//   регистрирует себя `SetMiniGame(this)`. До В2 работает встроенный [ПОРТ]-эмулятор (`FAsdaFishMiniGameEmu`
//   в .cpp): окна нет, раунд завершается командой `asda.Fish.EmuRound <всего> <верно>` / самотестом.
class ASDAREBORN_API IAsdaFishMiniGame
{
public:
	// Виртуальный деструктор интерфейса.
	virtual ~IAsdaFishMiniGame() {}
	// FSM 110 (0x6E): `FishingWindow_Show(g_pFishingWindow)` + `CControl_SetVisible(71307 FISH_image_1, 1)` (g:608519).
	virtual void FishWinShow() = 0;
	// FSM 114 (0x72): `uiFishingSlotInit(ключ-гайд, Rounds)` — старт гайда (g:608537).
	virtual void GuideStart(int32 Rounds) = 0;
	// FSM 115 (0x73): `CTutorialKeyGuide_Update@0xFC7460` → 5 (раунд закончен, 6169 УЖЕ отправлен гайдом),
	//   8 (все раунды), прочее — идёт. Отправку 6169 гайд делает через `UAsdaFishingSubsystem::SendPhaseResult`.
	virtual int32 GuideTick() = 0;
	// FSM 120 (0x78): видна ли всплывашка улова 71400 (`*(dword_3520C1C + 292)`).
	virtual bool IsCatchPopupShown() const = 0;
	// FSM 120: `uiDrawPickedItemIcon(g_pFishingWindow, id улова)` — показать всплывашку 71400.
	virtual void ShowCatchPopup(int32 CaughtItemId) = 0;
	// FSM 120 → 121: `sub_92E8F1(g_pFishingWindow)` — спрятать всплывашку 71400.
	virtual void HideCatchPopup() = 0;
	// FSM 128 (0x80): `uiFishingPanel_setVisible(0)` + `uiFishingPanelClearSlot()` (g:608626-608627).
	virtual void PanelHideAndClear() = 0;
	// Шкала улова изменилась (`gauge_SetValue@0xFC81D0` — эталон тут же перерисовывает 71305/71326).
	virtual void OnGaugeChanged(float Value, float Max) = 0;
	// ★ Ревизия В1 МЕЛК-7 (контракт подключения, 08.10.2026): точка сброса мини-игры. Автомат зовёт её из своего
	//   `ResetSessionState` (bFull — полный сброс: разрыв/выход/вход; false — смена карты). Окно В2 обязано вернуть
	//   гайд в 0 и спрятаться. Эталон при разрыве закрывает клиент (original-quits-client-on-disconnect).
	virtual void OnFishSessionReset(bool bFull) = 0;
};

// ★ ВСТРОЕННЫЙ ЭМУЛЯТОР МИНИ-ИГРЫ ([ПОРТ] до волны В2). Окна 71300 и ключ-гайда ещё нет. Эмулятор повторяет КОНТРАКТ
//   гайда (`CTutorialKeyGuide_Update@0xFC7460`): раунд завершён, когда его «ответили» (`asda.Fish.EmuRound` /
//   самотест), тогда — как гайд в своём состоянии 5 — уходит 6169 и возвращается 5; после последнего раунда — 8
//   (по одному вызову, как состояния 5 → 8 → 0 гайда). Ни таймера раунда, ни стрелок здесь нет — это В2.
class ASDAREBORN_API FAsdaFishMiniGameEmu final : public IAsdaFishMiniGame
{
public:
	// Владелец (подсистема рыбалки).
	explicit FAsdaFishMiniGameEmu(class UAsdaFishingSubsystem* InOwner) : Owner(InOwner) {}
	// FSM 110: «окно показано».
	virtual void FishWinShow() override;
	// FSM 114: старт гайда на Rounds раундов.
	virtual void GuideStart(int32 InRounds) override;
	// FSM 115: тик гайда (0 / 5 / 8).
	virtual int32 GuideTick() override;
	// FSM 120: видна ли всплывашка.
	virtual bool IsCatchPopupShown() const override { return bPopup; }
	// FSM 120: показать всплывашку.
	virtual void ShowCatchPopup(int32 ItemId) override;
	// FSM 120 → 121: спрятать всплывашку.
	virtual void HideCatchPopup() override { bPopup = false; }
	// FSM 128: спрятать панель и очистить.
	virtual void PanelHideAndClear() override { bWinShown = false; bPopup = false; State = 0; Answers.Reset(); }
	// Шкала изменилась — рисовать нечего.
	virtual void OnGaugeChanged(float, float) override {}
	// Сброс сессии автомата — то же, что `Reset`.
	virtual void OnFishSessionReset(bool) override { Reset(); }
	// Поставить ответ на раунд (всего, верно). false — гайд не запущен.
	bool QueueRound(int32 Total, int32 Correct);
	// Сброс сессии.
	void Reset() { bWinShown = false; bPopup = false; Rounds = 0; CurRound = 0; State = 0; Answers.Reset(); }

	class UAsdaFishingSubsystem* Owner = nullptr;   // владелец
	bool bWinShown = false;                         // «окно» показано
	bool bPopup = false;                            // «всплывашка» показана
	int32 Rounds = 0;                               // всего раундов (totalPages)
	int32 CurRound = 0;                             // завершено раундов (curPage)
	int32 State = 0;                                // 0 нет, 1 идёт, 8 «все раунды» (как state гайда)
	TArray<FIntPoint> Answers;                      // очередь ответов (всего, верно)
};

// ★ Точка для книги (В3): FSM 101 (`g:608344-608352`). Вернуть true — «остаёмся в 101» (диалог 79 уже в
//   очереди, ИЛИ регистрация 6176 в полёте, ИЛИ найден предмет и диалог 79 показан); false — «нечего
//   регистрировать» → FSM 100. Не привязан (книги ещё нет) ⇒ как эталон без подходящего предмета: 100.
DECLARE_DELEGATE_RetVal(bool, FAsdaFishBookProbe);
// Своё действие рыбалки 60…67 / 0 выставлено (`CharFx_AutoSabzil_State`) — В5 ставит клип персонажу и удочке.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSelfFishAction, int32 /*Action*/);
// Навык рыбалки изменился (6164 / 6172 @1) — В6 обновляет строку 997 окна персонажа.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaFishSkillChanged, int32 /*Skill*/);
// Применена запись 6173 к найденному по charId персонажу — В5 ставит клип/удочку/рыбу чужому.
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaPeerFishState, AActor* /*Char*/, const FAsdaFishPeerState& /*State*/);
// Улов 6172 код 1 — книга (В3) делает `questTableUpdateKeyMinMax(книга, id, размер)`.
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaFishCaught, int32 /*ItemId*/, int32 /*SizeCm*/);

UCLASS()
class ASDAREBORN_API UAsdaFishingSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

	friend struct FAsdaFishingSelfTest;   // офлайн-самотест `asda.Fish.SelfTest` (часы, гейты, внутренние поля)
	friend class FAsdaFishMiniGameEmu;    // встроенный эмулятор мини-игры (шлёт 6169 через SendPhaseResult)
	friend struct FAsdaFishWinSelfTest;   // самотест окна В2 `asda.Fish.WinSelfTest` (часы, шаги автомата, поля)
	friend struct FAsdaFishZoneSelfTest;  // ★ 08.10.2026: самотест зон и гейтов В4 `asda.Fish.ZoneSelfTest` (состояние автомата/гайда)
	friend struct FAsdaFishAnimSelfTest;  // ★ 08.10.2026: самотест анимаций В5 `asda.Fish.AnimSelfTest` (часы, шаги автомата, поля)

public:
	// Поднять подсистему, подписаться на сеть (зависимость инициализируется раньше).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снять подписки.
	virtual void Deinitialize() override;
	// IAsdaSessionResettable: разрыв/выход/вход/смена карты (см. реализацию — что сохраняется при EnteringWorld).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Покадровый драйвер = `charUpdateFishingAndState@0xFC3270`: очередь пакетов → FSM → очередь 6173 → мост клипов.
	virtual void Tick(float DeltaTime) override;
	// Стат тикера.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaFishingSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═════════════════════ ЧИСЛА ЭТАЛОНА ═════════════════════
	static constexpr float kGaugeMax = 500.f;        // `set_o2_36C0@0xFC36C0`: max шкалы = 0x43FA0000 = 500.0
	static constexpr int32 kRodSubType = 10;         // удочка: itemSubType 10 …
	static constexpr int32 kRodVariant = 1;          // … и itemVariant 1 (`playerCheckEquippedItemType10@0xFC7DE0`)
	static constexpr int32 kBaitSubType = 204;       // наживка (`localPlayerHasEquippedRecord204@0xFC7E90`)
	static constexpr int32 kEquipSlotRod = 9;        // слот снаряжения удочки (char+0x835; сервер — экип-слот 9)
	static constexpr int32 kEquipSlotBait = 10;      // слот снаряжения наживки (char+0x86E; сервер — экип-слот 10)
	static constexpr int32 kNoFishDataRounds = 1;    // Р10: записи нет → 1 раунд (эталон: при +8 < 0)
	static constexpr int32 kNoFishDataRoundMs = 30000; // нет записи → 30000 мс (`g:605263`)

	// ═════════════════════ ГЕЙТ «ИДЁТ РЫБАЛКА» (В2/В4) ═════════════════════
	// Эталон `Input_IsTextInputFocused(g_pChatInputWnd)` (МИСНЕЙМ) = `fishState != 0` — 20+2 гейта world_anim §5.
	UFUNCTION(BlueprintPure, Category="Asda|Рыбалка")
	bool IsFishing() const { return FishState != 0; }
	// ★ В3 S7 (дедуп №2, N14; 12.10.2026): ОДНА точка «идёт рыбалка» для всех потребителей без указателя на подсистему —
	//   контроллер (фильтр клавиш, клики, ПКМ), Slate-гейт окон, журнал заданий, мини-карта, хотбар, системное меню, окно
	//   мини-игры. Прежде — две одинаковые обёртки и 5 копий `Fish && Fish->IsFishing()`. `Ctx` — GameInstance, мир или любой
	//   объект мира (нет GameInstance / подсистемы — false).
	static bool IsFishingNow(const UObject* Ctx);
	// Текущее состояние автомата (0, 100…128).
	UFUNCTION(BlueprintPure, Category="Asda|Рыбалка")
	int32 GetFishState() const { return FishState; }
	// ★ R5-fix8-c (14.11.2026) [наблюдение для самотеста `asda.FishBook.SelfTest`, шаг M4]: сколько раз «Отмена» диалога 79 перевела автомат
	//   в 100 (`OnBookDialogCancelled`, g:380708) и из какого состояния был последний такой переход (−1 — не было).
	int32 GetBookCancelCountForCheck() const { return BookCancelCount; }
	int32 GetBookCancelFromStateForCheck() const { return BookCancelFromState; }

	// ═════════════════════ ДАННЫЕ ═════════════════════
	// `fishGameFindRecordById@0xFD03C0`: первая запись Fish_data с кодом (nullptr — нет).
	const FAsdaFishDataRow* FindFishData(int32 FishCode) const;
	// `rangeTableLookupValue@0xFD0570`: первый диапазон Fish_num с min ≤ V ≤ max → value, иначе 1.
	int32 LookupFishNum(int32 V) const;
	// `sub_FD0320`/`sub_91FB12`: элемент таблицы спотов по индексу (nullptr — вне таблицы).
	//   ★ Эталон проверяет `idx > count` (Р-граница, выход на 1 запись за массив) — у нас строгая граница.
	const FAsdaFishSpotRow* GetSpot(int32 SpotIdx) const;
	// Индексы спотов карты (= зоны, которые `worldMapPlaceMarkersByType@0xFC3340` создаёт для `mapId`).
	void GetSpotsOfMap(int32 MapId, TArray<int32>& OutIdx) const;
	// Число загруженных строк (диагностика/самотест).
	int32 GetFishDataNum() const { EnsureTables(); return FishRows.Num(); }
	// Число диапазонов Fish_num.
	int32 GetFishNumNum() const { EnsureTables(); return NumRows.Num(); }
	// Число спотов.
	int32 GetSpotNum() const { EnsureTables(); return SpotRows.Num(); }

	// ═════════════════════ ВХОД (В4 и консоль) ═════════════════════
	// `sub_FC3590`/`sub_9202A1`: выбрать зону — ТОЛЬКО при fishState == 0 (g:603165). true — выбрана.
	bool PickSpot(int32 SpotIdx);
	// `inputHotkeyAction109@0xFC3540`: при fishState == 0 → 109 (ЛКМ по зоне / ячейка хотбара типа 5).
	//   ★ Ревизия В1 ВАЖН-2 [ПОРТ] (`asda.Fish.Legacy109` 0): зона не выбрана → чат 110700 и 0 (не вечный 109).
	void RequestStart109();
	// `targetSelectNearestEntity@0xFC6490` (доп-меню cmd 8): ближайшая зона текущей карты в радиусе → выбрать + 109;
	//   нет → чат 110700 и fishState = 0 (★ безусловно, как эталон). true — зона найдена.
	bool StartAtNearestSpot();
	// Выбранная зона (−1 — нет).
	int32 GetPickedSpot() const { return PickedSpot; }

	// ═════════════════════ МИНИ-ИГРА/ОКНО (В2) ═════════════════════
	// Зарегистрировать окно/гайд волны В2 (nullptr — вернуть встроенный эмулятор).
	//   ★ Ревизия В1 МЕЛК-7: реализация-UObject передаёт себя в `InOwner` — автомат держит СЛАБУЮ ссылку и при
	//   уничтоженном владельце без снятия регистрации возвращается к эмулятору (висячего указателя нет).
	//   Владелец обязан снять регистрацию в своём `Deinitialize` (`SetMiniGame(nullptr)`).
	void SetMiniGame(IAsdaFishMiniGame* InMiniGame, UObject* InOwner = nullptr);
	// Зарегистрированная реализация В2 (nullptr — работает эмулятор); самотесты сохраняют и возвращают её.
	IAsdaFishMiniGame* GetRegisteredMiniGame() const;
	// Владелец зарегистрированной реализации (для возврата регистрации самотестами).
	UObject* GetRegisteredMiniGameOwner() const { return MiniGameOwner.Get(); }
	// Время автомата, мс (`timeGetTime()` + сдвиг часов самотеста) — общее для автомата и окна В2.
	uint32 GetNowMs() const { return NowMs(); }
	// Плашка 221 золотым по loc-id (`CControl_SetTooltipText(g_pChatWnd, txt, −11514, 10000)`) — окно В2 (пауза).
	void NoticePlankLoc(int32 LocId);
	// ★ Журнал последних нотисов рыбалки «канал:loc-id:текст» (канал chat/plank/msgbox) — для самотестов
	//   (ревизия В1 МЕЛК-3: каналы нотисов утверждаются, а не только видны в журнале). Не более 64 строк.
	const TArray<FString>& GetRecentNotices() const { return RecentNotices; }
	// Очистить журнал нотисов (самотест).
	void ClearRecentNotices() { RecentNotices.Reset(); }
	// Отправка итога раунда 6169 (её делает гайд в своём состоянии 5 — `CTutorialKeyGuide_Update` g:605291-605298).
	//   Тело: charId, i16 шкала (float→int), u8 всего, u8 верно. true — ушёл.
	bool SendPhaseResult(int32 Total, int32 Correct);
	// Кнопка X 71328 / ESC (`uiControlEventDispatch@0xFC9110`) → диалог 80 «Прекратить рыбалку?» (111046).
	void RequestCancelDialog80();
	// Шкала улова (`g_InputEditNumBuf`): значение / максимум.
	float GetGauge() const { return Gauge; }
	// Максимум шкалы (500).
	float GetGaugeMax() const { return GaugeMax; }
	// `gauge_SetValue@0xFC81D0`: 0 → обнулить вспом. поле; значение = min(V, max); окну — перерисовать.
	void SetGaugeValue(int32 V);
	// `sub_FC83A0`: шкала полна (value ≥ max).
	bool IsGaugeFull() const { return Gauge >= GaugeMax; }
	// Автоповтор (галка 71349, 0x35202F5). ★ `cmapInitReset` ставит 1.
	bool GetAutoRepeat() const { return bAutoRepeat; }
	// Переключить автоповтор (окно В2, событие 71349).
	void SetAutoRepeat(bool bOn) { bAutoRepeat = bOn; }
	// Пауза между раундами (кнопка 71329, 0x35202F4).
	bool IsPaused() const { return bPaused; }
	// Поставить/снять паузу (окно В2).
	void SetPaused(bool bOn) { bPaused = bOn; }
	// FishCode выбранной сервером рыбы (6168 @1 → `g_FishSelectedRecordId` 0x3520342).
	int32 GetSelectedFishCode() const { return SelectedFishCode; }
	// Лимит раунда, мс (`g:605260-605264`): запись есть → её +12 (как есть, в т.ч. −1), нет → 30000 (Р10).
	int32 GetRoundLimitMs() const;
	// ★ Р9: остаток времени раунда для таймера 71347 = max(0, лимит − прошло) (эталон без клампа рисует мусор).
	int32 ComputeRoundRemainMs(int32 ElapsedMs) const;
	// Стрелок в раунде: `Input_BuildKeySchemeString` → `rangeTableLookupValue(зона+16)` (выбранной зоны нет → ключ 0).
	int32 GetKeysPerRound() const;

	// ═════════════════════ КНИГА (В3) ═════════════════════
	// Точка FSM 101 (см. объявление делегата). Привязывает В3.
	FAsdaFishBookProbe BookRegisterProbe;
	// «Отмена» диалога 79 (`g:380708`): при fishState ≠ 0 → 100. Зовёт В3 из своего обработчика диалога.
	void OnBookDialogCancelled();
	// Улов (6172 код 1) — для книги.
	FOnAsdaFishCaught OnFishCaught;

	// ═════════════════════ АНИМАЦИИ (В5) ═════════════════════
	// ★ Конец клипа своего действия 60…67 = `Fishing_OnStateMessage@0xFC56E0` с кодом action−33 (0x1B…0x22).
	//   Зовёт персонаж по концу клипа (`AAsdaPlayerCharacter::OnFishClipEnd`, волна В5); [ПОРТ]-мост таймером
	//   (`asda.Fish.ClipBridge 1`) оставлен ключом отката — по умолчанию 0 с волны В5.
	void OnFishingClipEnd(int32 Action);
	// ★ В5fix (ревизия В5, МЕЛК-5): страховка от «вечного ожидания конца клипа» — клип своего действия не запустился
	//   (клипа нет / нет своего актора): проход доводится таймером длительности .jbb (`kClipMs`), как мост В1, но только
	//   для этого действия и независимо от `asda.Fish.ClipBridge`; в журнал — предупреждение. [ПОРТ]
	void ArmClipBridgeFallback(int32 Action, const TCHAR* Why);
	// ★ fishfix2 09.10 (ревизия fishing_fixes_REVIEW СРЕДН-1): `asda.Fish.LegacyDeadClip` = 1 — как оригинал (клип рыбалки
	//   и 4024 у трупа); 0 (по умолчанию) — [ПОРТ] исправление дефекта оригинала: мёртвому клип 60…67 не ставится.
	static bool IsLegacyDeadClip();
	// Текущее своё действие рыбалки (0 — нет).
	int32 GetSelfAction() const { return SelfAction; }
	// Своё действие сменилось (клип персонажа/удочки).
	FOnAsdaSelfFishAction OnSelfFishAction;
	// Применена запись 6173 к персонажу.
	FOnAsdaPeerFishState OnPeerFishState;
	// Последнее состояние чужого по charId (nullptr — записей не было).
	const FAsdaFishPeerState* FindPeerState(int32 CharId) const { return PeerStates.Find(CharId); }
	// ★ Волна В5: свой персонаж (актор) — та же точка, что у автомата (реестр мира, офлайн — павн контроллера).
	class AAsdaPlayerCharacter* GetSelfCharacter() const { return SelfChar(); }
	// Свои поля персонажа `CCharacter[1].gap_A42` (для В5): флаг «рыбачит» [15036].
	bool GetSelfFishingFlag() const { return bSelfFishingFlag; }
	// [15066] рыба на крючке.
	bool GetSelfHasFish() const { return bSelfHasFish; }
	// [15068] размер рыбы, %.
	int32 GetSelfFishSizePct() const { return SelfFishSizePct; }
	// [15076] FishCode / id улова.
	int32 GetSelfFishCode() const { return SelfFishCode; }
	// `dword_3520388`: id улова из 6172 @64 (> 0 — поймал).
	int32 GetCaughtItemId() const { return CaughtItemId; }
	// `word_352034D`: размер улова, см (6172 @5) — для всплывашки 71400 (111070).
	int32 GetCaughtSizeCm() const { return CaughtSizeCm; }

	// ═════════════════════ НАВЫК (В6) ═════════════════════
	// [15072] навык рыбалки (6164 при входе/портале, 6172 @1 после улова).
	int32 GetFishingSkill() const { return FishingSkill; }
	// Пришёл ли навык в этой сессии (до 6164 — «0», требование владельца 23.08, Р11).
	bool IsFishingSkillKnown() const { return bSkillKnown; }
	// Навык изменился.
	FOnAsdaFishSkillChanged OnFishSkillChanged;

	// ═════════════════════ ДИАГНОСТИКА ═════════════════════
	// Дамп состояния (консоль `asda.Fish.Dump`).
	FString DumpState() const;
	// Эмулятор: завершить раунд мини-игры (до В2; консоль `asda.Fish.EmuRound`). false — эмулятор не активен.
	bool EmuQueueRound(int32 Total, int32 Correct);

private:
	// ── данные ──
	// Загрузить DT (лениво, один раз; удержание от GC — `HeldTables`).
	void EnsureTables() const;
	// Сырые указатели строк в порядке файла (живут, пока таблицы удержаны).
	mutable TArray<const FAsdaFishDataRow*> FishRows;
	// Диапазоны Fish_num по порядку.
	mutable TArray<const FAsdaFishNumRow*> NumRows;
	// Споты по порядку (= индекс зоны эталона).
	mutable TArray<const FAsdaFishSpotRow*> SpotRows;
	// Попытка загрузки была.
	mutable bool bTablesTried = false;
	// Сильные ссылки на DT (закон dt_gc_hold).
	UPROPERTY(Transient) FAsdaHeldTables HeldTables;

	// ── сеть ──
	// Пакет в очереди: опкод + тело (порядок прихода сохраняется для всех опкодов).
	struct FPkt
	{
		int32 Op = 0;          // опкод
		TArray<uint8> Body;    // тело фиксированной длины (копия сетевого приёмника)
	};
	// Очередь из сетевых колбэков (закон no-reentrant-net-callbacks); разбор — в Tick.
	TAsdaNetQueue<FPkt> PendingPkts;
	// Колбэки сети: только кладут в очередь (закон no-reentrant-net-callbacks).
	void OnNet6164(const TArray<uint8>& B);   // навык (вход/портал)
	void OnNet6168(const TArray<uint8>& B);   // ответ на старт 6167
	void OnNet6170(const TArray<uint8>& B);   // новая шкала после 6169
	void OnNet6172(const TArray<uint8>& B);   // результат рыбалки (+ вес — ревизия В1 МЕЛК-8: применяется в Tick)
	void OnNet6173(const TArray<uint8>& B);   // чужой рыбак
	void OnNet6183(const TArray<uint8>& B);   // награда книги (здесь — только Р2)
	// Разобрать очередь (Tick; самотест зовёт напрямую).
	void DrainPackets();
	// Обработчики (порядок веток 1:1 с эталоном).
	void Apply6164(const TArray<uint8>& B);   // `uiUpdateStatValueLabel@0xFCAAF0`
	void Apply6168(const TArray<uint8>& B);   // `netHandleTooltipNoticePacket@0xFCBD80`
	void Apply6170(const TArray<uint8>& B);   // `recvSetInputGaugeValue@0xFCC7A0`
	void Apply6172(const TArray<uint8>& B);   // `netOnItemUpgradeResult@0xFCCF50` (результат рыбалки)
	void Apply6173(const TArray<uint8>& B);   // `stat_RecvStatSlotUpdate@0xFCAA10` → очередь чужих
	void Apply6183(const TArray<uint8>& B);   // только Р2: 6183 4/6 из `Recv_EndFishing` в состоянии 118
	// Отправить 6167 по выбранной зоне (`acSendChallenge6167@0xFCB720`): нет зоны/не ушёл → false.
	bool Send6167();
	// `netSendTsMaskedReq6171@0xFCC8D0`: режим 1/2; ранний выход «2 без флага → fs = 128» + Р4.
	void Send6171(int32 Mode);
	// Р4: 6171 в полёте (снимается ответом 6172/отказом/сбросом/fs → 0).
	bool b6171InFlight = false;
	// Р3: отмена по смерти уже вызвана в этом эпизоде (снимается, когда условие смерти ушло / fs = 0).
	bool bDeathCancelCalled = false;
	// Подписки на сеть поставлены.
	bool bNetBound = false;
	// Поставить подписки (Initialize; повтор безопасен).
	void BindNet();

	// ── автомат ──
	// Шаг автомата (`Fishing_UpdateStateMachine@0xFCEE90`).
	void UpdateFsm();
	// `storeFirstDword(g_pChatInputWnd, N)` — смена состояния с журналом.
	void SetState(int32 NewState, const TCHAR* Why);
	// Самостоятельная проверка «персонаж мёртв или флаг 1» (`sub_971D90`): действие 6 | `charTestFlags(1)`.
	bool IsSelfDeadOrBusyFlag() const;
	// ★ fishfix2 09.10 (СРЕДН-1): свой персонаж РЕАЛЬНО мёртв — поза трупа у актора или серверное «мёртв» (4053/6001),
	//   без подмены самотеста `TestDeadOverride` (она проверяет автомат, а не клипы).
	bool IsSelfReallyDead() const;
	// Дистанция до выбранной зоны: true — далеко (r·100 ≤ d, эталон `fcomp & 0x4100`); OutDelta — зона − игрок.
	bool IsFarFromPicked(FVector2D& OutDelta) const;
	// `CharFx_AutoSabzil_State(ч, 0,…)` + 4008 + поворот к зоне + 4008 (FSM 102/109).
	void StopAndFaceZone(const FVector2D& Delta, const TCHAR* Why);
	// `playerCheckEquippedItemType10@0xFC7DE0`: в слоте 9 удочка 10/1.
	bool HasRodEquipped() const;
	// `localPlayerHasEquippedRecord204@0xFC7E90`: в слоте 10 наживка 204.
	bool HasBaitEquipped() const;
	// `invUseFirstEquipType10Item@0xFC7A90` (FSM 103).
	void UseFirstRod();
	// `invUseFirstDyeItem@0xFC7C20` (FSM 103/105).
	void UseFirstBait();
	// `invFlaggedSlotTriggerUse@0xFC7920` (FSM 128): вернуть авто-надетое.
	void ReturnAutoEquipped();
	// Двухкнопочный диалог (пара (1, Ctx)) с ответами FSM.
	void ShowFishDialog(int32 Ctx, const FString& Text);
	// Ответ диалога (`OnSendItemUpgrade` OK g:380104-380126 / Отмена g:380704-380720).
	void OnFishDialogAnswer(int32 Ctx, bool bOk);
	// `CharFx_AutoSabzil_State(свой, Action)` — поставить действие (В5 + мост клипов).
	void SetSelfActionState(int32 Action);
	// `charEmoteSabzilAction@0xFC5590`: смена действия с 4024 для 60/62/65/66.
	void EmoteAction(int32 Action);
	// ★ fishfix 09.10 (ревизия МЕЛК-2): действие персонажа в смысле эталона `Net_GetCurrentMapType(свой)` (60…67 у персонажа /
	//   действие страховки-моста / 6 мёртв / 0) — с ним `EmoteAction` сравнивает повтор.
	int32 GetSelfCharEtalonAction() const;
	// ★ fishfix 09.10 (ревизия МЕЛК-5): после смены карты вернуть авто-надетое (ветка 128) — как оригинал, у которого флаги
	//   авто-снаряжения переживают портал и срабатывают на ближайшем 128. Зовётся из `UpdateFsm` в покое.
	void TickReturnAutoAfterMap();
	// Мир готов к отправке «использовать» (сессия в мире, свой персонаж есть, экран загрузки снят); самотест подменяет.
	bool IsWorldReadyForAutoReturn() const;
	// Эффект над головой (`Effect_SpawnByName(…, y + 130, …)`): `fish_end_01`/`fish_end_02`.
	void SpawnEndFx(const TCHAR* JebBase) const;
	// Звук 2D по имени (`CSoundFlag_PlaySound2D`).
	void Play2D(const TCHAR* Name) const;
	// Звук 3D у своего персонажа (`Audio_Play3DSound`).
	void Play3DAtSelf(const TCHAR* Name) const;
	// Чат «жёлто-оранжевым» (`CChat_AddTokenizedLines(g_pChatWnd, txt, −11514, 10000, 0)`); LocId — для журнала.
	void ChatGold(const FString& Text, int32 LocId = 0);
	// Плашка 221 (`CControl_SetTooltipText(g_pChatWnd, txt, −11514, 10000)`); LocId — `SrcId` маршрутизатора
	//   (ревизия В1 МЕЛК-9: loc-id, как у прочих вызывающих, а не опкод) и журнал.
	void PlankGold(const FString& Text, int32 LocId);
	// Однокнопочный msgbox (`CItem_BuildTooltipText(ui, 0, −1, txt, 1)`); LocId — для журнала (0 — «%d» кода).
	void MsgBox1(const FString& Text, int32 LocId = 0);
	// Добавить строку в журнал нотисов (`RecentNotices`).
	void NoteNotice(const TCHAR* Channel, int32 LocId, const FString& Text);
	// Журнал последних нотисов (см. `GetRecentNotices`).
	TArray<FString> RecentNotices;
	// Loc-строка с тегами (+ форматирование %s/%d по порядку).
	FString LocFmt(int32 Id, const TArray<FString>& Args = TArray<FString>()) const;
	// Время, мс (`timeGetTime()`); самотест сдвигает `TestClockOffsetMs`.
	uint32 NowMs() const;
	// Активная мини-игра (В2 или эмулятор).
	IAsdaFishMiniGame* MG() const;
	// Свой персонаж (актор).
	class AAsdaPlayerCharacter* SelfChar() const;

	// ── очередь 6173 ──
	// Запись очереди: 11 байт тела + время прихода (Р5).
	struct FPeerRec
	{
		TArray<uint8> Body;    // тело 6173 (11 б)
		uint32 RecvMs = 0;     // время прихода (для TTL Р5)
	};
	// Очередь чужих (`g_pMap+0x8B8` std::list).
	TArray<FPeerRec> PeerQueue;
	// `charApplyStateFromList@0xFCAC60`.
	void ApplyPeerQueue();
	// Последние состояния чужих по charId.
	TMap<int32, FAsdaFishPeerState> PeerStates;

	// ── мост клипов ([ПОРТ] до В5) ──
	// Действие, конец клипа которого ждёт мост (−1 — нет).
	int32 BridgeAction = -1;
	// Когда «закончится» клип моста, мс.
	uint32 BridgeEndMs = 0;
	// Тик моста.
	void TickClipBridge();
	// Мост этого прохода взведён страховкой (`ArmClipBridgeFallback`) — `asda.Fish.ClipBridge 0` его не гасит.
	bool bBridgeForced = false;

	// ── состояние (поля эталона) ──
	int32 FishState = 0;            // +0x870 fishState
	int32 BookCancelCount = 0;      // [наблюдение] переходов «Отмена 79 → 100» за жизнь подсистемы (на поведение не влияет)
	int32 BookCancelFromState = -1; // [наблюдение] состояние автомата перед последним таким переходом (−1 — не было)
	uint32 Fs1Ms = 0;               // +0x874 fs[1] — начало 107 (`timeGetTime()` после 6167)
	uint32 Fs2Ms = 0;               // +0x878 fs[2] = `g_dwActionTimeMs` — начало отсчёта 121
	uint32 Fs3Ms = 0;               // +0x87C fs[3] — начало всплывашки 120
	uint32 CountdownNoticeMs = 0;   // `g_dwFishGaugeUpdateTime` 0x3522344 — троттлинг 111050
	bool bPaused = false;           // +0x884 пауза (71329)
	bool bAutoRepeat = true;        // +0x885 автоповтор (71349) — `cmapInitReset` = 1
	int32 PickedSpot = -1;          // +0x8A0 выбранная зона (индекс в таблице спотов)
	float Gauge = 0.f;              // +0x8C4 шкала: значение
	float GaugeAux = 0.f;           // +0x8C8 шкала: вспом. поле (обнуляется при value = 0)
	float GaugeMax = kGaugeMax;     // +0x8CC шкала: максимум 500
	int32 SelectedFishCode = 0;     // 0x3520342 FishCode из 6168 код 1
	TArray<uint8> Last6168;         // 0x3520341 копия 7 б 6168 (код 1)
	TArray<uint8> Last6172;         // 0x3520348 копия 121 б 6172
	int32 CaughtItemId = 0;         // 0x3520388 = 6172 @64
	int32 CaughtSizeCm = 0;         // 0x352034D = 6172 @5
	int32 SelfAction = 0;           // действие своего персонажа (эталон: состояние +47551)
	// Контекст авто-снаряжения (`g_pInventoryMgr` 0x35202F8): +8/+0 удочка, +9/+4 наживка.
	bool bAutoRod = false;          // +8 удочка надета автоматически
	int32 AutoRodBagCell = -1;      // +0 ячейка контейнера 1, из которой надели удочку
	bool bAutoBait = false;         // +9 наживка надета автоматически
	int32 AutoBaitCell = -1;        // +4 ячейка контейнера 2, из которой надели наживку
	// ★ fishfix 09.10 (МЕЛК-5): смена карты посреди рыбалки при авто-надетой снасти — вернуть её ближайшим 128 в покое
	//   (`TickReturnAutoAfterMap`); флаги +8/+9 при этом СОХРАНЯЮТСЯ, как у эталона (их пишут только 0xFC7A90/0xFC7C20/0xFC7920).
	bool bReturnAutoAfterMap = false;
	// Свои поля персонажа `CCharacter[1].gap_A42`.
	bool bSelfFishingFlag = false;  // [15036]
	bool bSelfHasFish = false;      // [15066]
	int32 SelfFishSizePct = 100;    // [15068] (ctor = 100)
	int32 FishingSkill = 0;         // [15072]
	bool bSkillKnown = false;       // пришёл ли 6164/6172 в этой сессии
	int32 SelfFishCode = 0;         // [15076]

	// ── мини-игра ──
	// Зарегистрированная реализация В2 (nullptr — эмулятор).
	IAsdaFishMiniGame* MiniGame = nullptr;
	// ★ Ревизия В1 МЕЛК-7: слабая ссылка на владельца реализации (UObject) — `MiniGame` годен, только пока он жив.
	TWeakObjectPtr<UObject> MiniGameOwner;
	// Владелец передавался (иначе — не-UObject реализация, живёт по контракту «снять в Deinitialize»).
	bool bMiniGameHasOwner = false;
	// ★ Ревизия В1 МЕЛК-11: предупреждение об отсутствующем звуке `s_e_fishing_success_00` уже было (Р13).
	bool bSuccessSoundWarned = false;
	// ★ Ревизия В1 МЕЛК-2: счётчик вызовов `Send6171` (самотест различает Р3 с ключом и без).
	int32 DebugSend6171Calls = 0;
	// ★ Ревизия В1 МЕЛК-6: ячейка сумки, из которой `invFlaggedSlotTriggerUse` последний раз «использовал» удочку (−1 — нет).
	int32 DebugLastReturnRodCell = -1;
	// Встроенный эмулятор.
	TUniquePtr<FAsdaFishMiniGameEmu> Emu;

	// ── самотест ──
	int64 TestClockOffsetMs = 0;    // сдвиг часов автомата (только самотест)
	int32 TestDeadOverride = -1;    // −1 нет; 0/1 — подмена «мёртв» (самотест Р3)
	int32 TestWorldReadyOverride = -1;   // −1 нет; 0/1 — подмена «мир готов к возврату снасти» (самотест МЕЛК-5)
	bool bTestNoNetDrain = false;   // самотест сам зовёт DrainPackets/UpdateFsm
};
