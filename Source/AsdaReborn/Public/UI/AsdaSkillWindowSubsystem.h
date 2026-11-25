// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillWindowSubsystem.h — ОКНО НАВЫКОВ `SKILL_WIN` (JUI IDNum 12000), порт класса `CSkillWnd`.
//
// ПОЛНЫЙ РЕВЕРС ПЕРЕД КОДОМ: `Reports/skill_window_port.md` (структура окна, все 615 контролов,
//   данные навыков, классы, опкоды). Ниже — только то, что нужно читателю кода.
//
// ЧТО ЭТО ЗА ОКНО
//   Дерево навыков персонажа: сетка 9 рядов × 5 колонок (45 видимых узлов), 3 вкладки-дерева своей
//   боевой полосы + вкладка 4-й профессии, счётчик свободных очков, панель предпросмотра справа.
//
// ГЛАВНЫЕ ПРАВИЛА ЭТАЛОНА, КОТОРЫЕ ЗДЕСЬ ВОСПРОИЗВЕДЕНЫ 1:1
//   • Полоса классов: `itemGradeBandFromTypeIndex@0xEC97A0` — job 1..11 → Fighter, 12..22 → Archer,
//     23..33 → Mage, иначе окно НЕ открывается (нотис 9421).
//   • Дерево навыка: `skillCodeToElementGroup@0xE52C40` по полю `skillGrade` (.sif +0xBD).
//   • Позиция узла: `Skill_BuildTreeUI@0xE532E0` — slot = 5·spCostPerTier + spCostBase − 5·scrollRow,
//     годен при 0..44.
//   • Иконка: `Skilltooltip.BIN` +36; НЕвыученный навык — тот же файл с суффиксом «-g».
//   • Клик по узлу: `skillWndSelectSlot@0xE53C80` — очки > 0, уровень < `maxLevelOrTier` (.sif +0x2B),
//     байт «доступен» из серверной записи; затем подтверждение (нотис 9415) и пакет 5253.
//   • Ответ 5254 с кодами отказа → нотисы 2201/2202/2206/2207/2208/111700/111701, иначе 2200.
//
// ЧЕГО ЭТО ОКНО НЕ ДЕЛАЕТ (важно, чтобы не искать)
//   • Не шлёт «сброс навыков»: `Skill_SendSkillReset`/`Skill_SendLearnOrLevelUp` в эталоне названы
//     ошибочно — это ОБРАБОТЧИК DROP-а навыка на панель быстрого доступа (msg 7 → `Hotbar_DragDrop_SetSlot`
//     → 5067). Здесь он выражен методом `CanDragSkill()` для моста с хотбаром.
//   • Не хранит prereq: клиент его не знает вовсе (он живёт в серверной таблице +189/+190). Клиент
//     видит лишь байт «доступен» в записи и код отказа 4 в ответе 5254.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaHotbarSubsystem.h"
// ★ Аудит №3 (A1): общая полоса прокрутки скина `Nomal_Scr` — ОДНА на весь порт (окно навыков и
//   инвентарь). Раньше у окна навыков жила вторая полная копия со СВОИМИ UV, и правка UV в одном
//   окне разъезжала полосу второго.
#include "UI/AsdaScrollBarNomal.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaSkillWindowSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UButton;
class UCanvasPanel;
class UAsdaButtonRelay;
class UAsdaSkillDatabase;

// Запись выученного навыка — то, что приходит с сервера (34-б запись пакетов 5251/6056 либо
//   3-б запись компактного списка 5252).
USTRUCT(BlueprintType)
struct FAsdaLearnedSkill
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 SkillCode = -1;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 Level = 0;          // запись +2
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") bool  bAvailable = false; // запись +3 («можно вкачивать»)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 ReqCharLevel = 0;   // запись +10 (i16)
	// ★ 25.10.2026: запись +4 (i32) — откат навыка в мс, по которому эталон взводит откат при применении
	//   (`skillStartGroupCooldown@0xE57860`: `v5 = *(learnedEntry + 4)`). Приходит от сервера в 34-б записях 5251/6056
	//   (GameServer `sub_4769C0`: CUser +49352 = skill_info +47); у записей 5252 — `.sif +47` (`Net_RecvLearnedSkillList@0xE56090`).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Навыки") int32 CooldownMs = 0;
};

// Виджеты ОДНОГО из 45 узлов сетки. Имена берутся из группы `SKILLTREE_ICON{NN}_grp`
//   (импортёр JUI: `{idc}_{juiIdx}_{plane}` у картинок, `{idc}` у текстов — закон jui-importer-widget-naming).
USTRUCT()
struct FAsdaSkillSlotWidgets
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Group      = nullptr;  // SKILLTREE_ICON{NN}_grp
	UPROPERTY(Transient) TObjectPtr<UImage>       Frame      = nullptr;  // рамка слота (Skill_SlotBaseX_Ot70)
	UPROPERTY(Transient) TObjectPtr<UImage>       Icon       = nullptr;  // картинка навыка 32×32
	UPROPERTY(Transient) TObjectPtr<UTextBlock>   Level      = nullptr;  // SKILLTREE_DLV{NN}
	UPROPERTY(Transient) TObjectPtr<UImage>       Selectable = nullptr;  // «доступен для изучения»
	UPROPERTY(Transient) TObjectPtr<UWidget>      Selected   = nullptr;  // «выбран» (анимация)
	UPROPERTY(Transient) TObjectPtr<UWidget>      Cool       = nullptr;  // SKILLTREE_COOL{NN} — шторка отката
	UPROPERTY(Transient) TObjectPtr<UTextBlock>   CoolText   = nullptr;  // остаток кулдауна
	UPROPERTY(Transient) TObjectPtr<UWidget>      Ced        = nullptr;  // вспышка после кулдауна

	// ★★★ ШТОРКА ОТКАТА УЗЛА + кэш «что на ней запущено». `Curtain.Plane` — тот же виджет, что `Cool`,
	//   приведённый к кадровому плану (скин `CoolTime_Effect`, 118 кадров).
	//   ⚠ ЧТО ЗДЕСЬ ОРИГИНАЛ, А ЧТО НАШЕ (уточнение к `Reports/item_cooldown_premium.md` §2,
	//   снято по `skill_buff.c` 2026-08-14):
	//     • САМ ПОКАЗ шторки на узле — ОРИГИНАЛЬНЫЙ: `skillAddAndRefreshBars@0xE49A20` делает
	//       `uiControl_ShowWithItem(m_aSkillSlot[68·слот+12], 120, 127, 8, 1000, 0)`, а
	//       `skillRemoveAndRefreshBars@0xE49BB0` — `CControl_CloseHide` того же контрола;
	//     • НАШЕ ОТСТУПЛЕНИЕ (решение юзера 2026-08-14) — что мы гоним по ней ЖИВОЙ ПРОГОН кадров,
	//       синхронный с остатком отката, вместо эталонного ЗАМОРОЖЕННОГО кадра 120 (animate = 0).
	//   Вернуть эталон: `asda.Cooldown.ShowEverywhere 0`.
	UPROPERTY(Transient) FAsdaCooldownCurtain Curtain;

	// Созданы в РАНТАЙМЕ, как в эталоне (`CControl_InstantiateChild` от шаблонов 56320/56325..56327/
	//   56321..56323): контейнер цены на (39,2), три шарика на (0,0)/(0,12)/(0,24), числа на (+14,−1).
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     CoinIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CoinTexts;

	int32 SkillCode = -1;   // что сейчас лежит в узле (−1 = пусто)
};

// ОДНА из ДВУХ строк левой панели «Хранитель души» (56300).
//   Эталон `skillPanelBindControls@0xE583C0` связывает два одинаковых набора:
//     строка 0 → 56302 (иллюстрация 126×190), 56303 (имя), 56304 (иконка навыка 32×32),
//               56305/56306/56307 (плашки «1ST»/«2ND»/«3RD»);
//     строка 1 → 56308,            56309,      56310,        56311/56312/56313.
//   Наполняет `skillPanelRefreshNextLevelSlots@0xE587C0` из `Data/SoulGuarderTable.bin`
//   (шаг 1956; поиск `findRecordByTwoKeys@0xEF76B0` по ключам SkillType и RowIndex).
USTRUCT()
struct FAsdaSoulPreviewRow
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UImage>     Illust   = nullptr;  // 56302 / 56308 — портрет хранителя 126×190
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Name     = nullptr;  // 56303 / 56309 — имя записи (поле +16)
	UPROPERTY(Transient) TObjectPtr<UImage>     NextIcon = nullptr;  // 56304 / 56310 — иконка навыка хранителя (+152), 32×32
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Tiers;          // 56305..56307 / 56311..56313 — «1ST»/«2ND»/«3RD»

	int32 GuardIndex      = -1;   // запись +0   — ключ подсказки (эталон gap_E4[60]/[100])
	int32 GuardianSkillId = -1;   // запись +152 — навык хранителя (gap_E4[64]/[104])
	int32 BuffSkillId     = -1;   // запись +12  — пассивный бафф   (gap_E4[68]/[108])
	int32 Tier            = -1;   // запись +8   — ступень 0..2     (gap_E4[72]/[112])
};

UCLASS()
class ASDAREBORN_API UAsdaSkillWindowSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSkillWindowSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Окно ──
	// Эталон `skillwin_OpenSkillWindow@0xE52DE0`: гейт «профессия выбрана» (нотис 9421 при job == 0),
	//   сборка всех 4 деревьев, вкладка 0, звук 5.
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") void OpenWindow();
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") void CloseWindow();
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") void ToggleWindow();
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") bool IsWindowOpen() const;
	UUserWidget* GetWindowWidget() const { return Win; }

	// ── Управление содержимым ──
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") void SetTree(int32 TreeIdx);
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") void SetScrollRow(int32 Row);
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") int32 GetTree() const { return CurTree; }
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") int32 GetScrollRow() const { return ScrollRow; }

	// Отправить запрос «вложить очко» (после всех гейтов эталона). true = пакет ушёл.
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") bool RequestLearn(int32 SkillCode);

	// ── Модель для других систем ──
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") int32 GetLearnedLevel(int32 SkillCode) const;
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") int32 GetSkillPoints() const { return SkillPoints; }
	// Гейт эталона `skillCheckResetEligible@0xE55DA0`: навык ВЫУЧЕН и его подтип не 3/4.
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") bool CanDragSkill(int32 SkillCode) const;

	// ★ Уложить навык в слот панели быстрого доступа (порт `Skill_SendLearnOrLevelUp@0xE55FB0`).
	//   Единая точка для броска мышью и для консоли `asda.Skill.ToHotbar`.
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки")
	bool DropSkillToHotbarSlot(int32 SkillCode, int32 AbsSlot);
	// Кулдаун с сервера (5465): продублировать в хотбар и подсветить узел сетки.
	//   ★ `TotalSecOverride > 0` — полная длительность ЭТОГО отката, если она НЕ равна `.sif`-кулдауну
	//     самого навыка. Так работает групповой откат эталона `skillStartGroupCooldown@0xE57860`:
	//     всем членам цепочки ставится длительность ПРИМЕНЁННОГО навыка (`skillStartCooldown(id, v5)`),
	//     а не их собственная. Полная длительность нужна только для доли пройденного (стартовый кадр
	//     шторки); 0 = брать из `.sif`, как при разборе 5465 (`Recv_SkillCoolTime@0xE4B180` там тоже
	//     делит на `SkillRecord->cooldownMs` самого навыка).
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") void PushCooldownFromServer(int32 SkillCode, int32 RemainSec, int32 TotalSecOverride = 0);
	// Остаток кулдауна навыка в секундах (0 = готов). ★ Реестр кулдаунов у ОКНА НАВЫКОВ — так же,
	//   как в эталоне (`classListFindStateByField4` читает вектор `CSkillWnd + 56`); боевому
	//   диспетчеру он нужен для гейта 5 (нотис 2203), поэтому вынесен наружу одним геттером.
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки") float GetCooldownRemainSec(int32 SkillCode) const;
	// ★ 25.10.2026: откат с точностью до мс (применение навыка, `skillStartCooldown@0xE579E0` — без округления до секунд).
	//   `TotalMs` — полная длительность для стартового кадра шторки (0 = равна остатку).
	void PushCooldownMs(int32 SkillCode, int32 RemainMs, int32 TotalMs = 0);
	// ★ 25.10.2026 (ревизия `wave_1025b_review` 1.1) [ОРИГИНАЛ]: откаты навыков применяются В ПОРЯДКЕ ПРИХОДА пакетов, как у эталона
	//   (там 5465/5271/5274 разбираются синхронно). 5465 и 5274 книга применяет в момент приёма; 5271 разбирает тик диспетчера
	//   навыков, поэтому каждый приход получает номер `NextCooldownArrivalSeq()`, а отложенный взвод 5271 отменяется, если ПОСЛЕ его
	//   прихода были снимок 5465 или 5274 этого навыка. Ключ `asda.Cooldown.ArrivalOrder` (0 — прежнее: 5465 в тике книги).
	// Номер очередного прихода 5465/5271/5274 (монотонный; 0 не выдаётся — это «номер неизвестен»).
	uint64 NextCooldownArrivalSeq() { return ++CooldownArrivalSeq; }
	// true — взвод отката `SkillCode` по 5271 с номером прихода `ArrivalSeq` устарел (после него пришёл 5465 или 5274 навыка).
	bool IsCooldownArmSuperseded(int32 SkillCode, uint64 ArrivalSeq) const;
	// ★ 25.10.2026: запись выученного навыка (эталон `Skill_GetLearnedSkillEntry@0xE57680`); nullptr — не выучен.
	const FAsdaLearnedSkill* FindLearned(int32 SkillCode) const { return Learned.Find(SkillCode); }

	const UAsdaSkillDatabase* GetDatabase() const { return Db; }
	// Класс/подкласс локального игрока (с учётом консольной подмены для проверки без сервера).
	int32 GetSelfJob() const;
	int32 GetSelfSubJob() const;
	int32 GetSelfBand() const;

	// ★ ТОЧКА ПОДКЛЮЧЕНИЯ К ХОТБАРУ: `Hotbar->SetSkillBridge(Skill->MakeHotbarBridge())`.
	//   Мост отдаёт хотбару иконку (с «-g» у невыученного), тип цели, кулдаун и показ подсказки.
	FAsdaHotbarSkillBridge MakeHotbarBridge();

	// ── Дампы для консоли ──
	FString DumpControls() const;
	// ★ Состояние ШТОРОК ОТКАТА на узлах дерева (`asda.Cooldown.Dump`).
	FString DumpCooldownCurtains() const;
	FString DumpState() const;

	// ── Эмуляция серверных пакетов (проверка без сервера) ──
	void SimulateCompactList(bool bAllAvailable);
	void SimulateFullList(bool bAllAvailable);
	void SimulateUpgradeResult(int32 SkillCode, int32 NewLevel, int32 NewPoints);
	void SimulateUpgradeFail(int32 Code);
	void SetPointsForTest(int32 Points);
	void SetJobForTest(int32 JobId, int32 SubJobId);

	// ── Открытие окна снаружи: кнопка главного меню 73403 и клавиша «K» ──
	//   ⚠ Оба делегата DYNAMIC (`AddDynamic`), поэтому обработчики обязаны быть `UFUNCTION`.
	//   Команда 32 — это НОМЕР СТРОКИ `INTERFACE SKILL: K` в `system/Shortcutkey.ini`
	//   (у инвентаря по той же схеме 33 = `INTERFACE INVENTORY: I`).
	UFUNCTION() void OnMenuCommand(int32 ButtonIdNum, int32 TargetWindowId, bool bOpen);
	UFUNCTION() void OnGameCommand(int32 Cmd);

	static constexpr int32 kMenuButtonIdNum   = 73403;   // NEW_SYSTEMMAIN_SKILLBUT
	static constexpr int32 kCmdInterfaceSkill = 32;      // Shortcutkey.ini «INTERFACE SKILL»

	bool bMenuBound = false;
	TWeakObjectPtr<class AAsdaPlayerController> BoundController;

	// ★ 19.10.2026 (GM-команда 6058): сбросить ВСЕ откаты навыков — порт `skillHotbarRefreshLocked@CSkillWnd` (skill_buff.c:30418):
	//   реестр `m_listActiveSkills` очищается целиком, затем перестраивается дерево окна (если окно открыто).
	void ResetAllSkillCooldowns();

private:
	// ★ 22.10.2026: самотест `asda.Quest.SelfTestJobChangeSkills` (`Private/Quest/AsdaQuestJobChangeSelfTest.cpp`) — читает модель
	//   `Learned`, сетку `Slots` и зовёт штатные `DrainPending`/`OnSlotClicked` (тот же путь, что Tick и клик игрока).
	friend struct FAsdaQuestJobChangeSelfTest;
	// ★ 25.10.2026: самотест `asda.SelfTest.InvRelog` (`Private/UI/AsdaInvRelogSelfTest.cpp`) — кладёт и читает `SkillPoints`
	//   (очки «учётки А» до перезахода); дружба с необъявленным в Shipping типом безвредна.
	friend struct FAsdaInvRelogSelfTest;
	// ★ 05.11.2026: самотест `asda.SelfTest.SkillJob4Points` (`Private/UI/AsdaSkillJob4PointsSelfTest.cpp`) — модель, сетка, клик узла,
	//   счётчики двух пулов очков (основные деревья / дерево 4-й профессии).
	friend struct FAsdaSkillJob4PointsSelfTest;
	// ★ 05.11.2026: самотест `asda.Skill.SelfTestNoneLines` (`Private/Skill/AsdaSkillNoneLinesSelfTest.cpp`, сессия gold-boast) — зовёт
	//   `ExpandSkillDescription` для 13 описаний с повтором единицы после [sdt*]/[sdtt*] (ключ `asda.Skill.DurUnitDedup`).
	friend struct FAsdaSkillNoneLinesSelfTest;

	// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (`asda.Skill.Job4Pools`) — ДВА ПУЛА ОЧКОВ НАВЫКОВ. Счёт у сервера один (CUser+0x19B), но эталон
	//   окна делит его на «основные деревья» (лимит 110 — все очки до 59-го уровня, 2·59−8) и «дерево 4-й профессии» (всё сверх 110
	//   с 60-го уровня): `Skill_BuildTreeUI@0xE532E0` и `skillUiUpdatePointText@0xE56720`. Разбор — шапка `ComputePointPools` (.cpp).
	struct FAsdaSkillPointPools
	{
		bool  bOn       = false;  // ключ asda.Skill.Job4Pools включён
		int32 Free      = 0;      // свободных очков у сервера (−1 → 0)
		int32 Level     = 0;      // уровень своего персонажа
		int32 MainSpent = 0;      // вложено в основные деревья (вкладки 0..2 своей полосы)
		int32 Job4Spent = 0;      // вложено в дерево 4-й профессии (вкладка 3)
		int32 MainShown = 0;      // число в строке очков 1092 (может быть < 0 — долг старых данных)
		int32 Job4Shown = 0;      // число в строке очков 4-й профессии 12413 (может быть < 0 — долг старых данных)
	};
	// Посчитать пулы по модели `Learned` и свободным очкам.
	FAsdaSkillPointPools ComputePointPools() const;
	// Сколько очков можно вложить в навык `SkillCode` (свой пул навыка, не больше свободных; ≥ 0).
	int32 PointsLeftForSkill(int32 SkillCode) const;
	// Навык из дерева 4-й профессии (вкладка 3)?
	bool IsJob4TreeSkill(int32 SkillCode) const;
	// ── Сеть: колбэки ТОЛЬКО копят, работа в Tick (закон no-reentrant-net-callbacks) ──
	void OnNetListFull(const TArray<uint8>& Body);
	void OnNetListUpdate(const TArray<uint8>& Body);
	void OnNetListCompact(const TArray<uint8>& Body);
	void OnNetUpgradeResult(const TArray<uint8>& Body);
	void OnNetTreeReset();
	void OnNetSlotsReset(int32 NewPoints);
	// 5464 — НЕ «изучение»: сервер шлёт его из ветки пересчёта навыков под характеристику (текст 12854).
	void OnNetSkillsRealigned(int32 NewPoints, int32 Grade);
	void OnNetCooldowns(const TArray<uint8>& Body);
	// ★ Свободные очки умений: блок char-select 1017 +39 (`CCharacter+0x443`) и все три «очковых» ответа.
	void OnNetSelfSkillPoints(int32 Points);
	// ★ 18.10.2026: смена профессии 5042 (`UAsdaNetSubsystem::OnSelfJobChanged`) — эталон `uiPopulateUpgradeOptionList`
	//   + `skillWnd_msgProc(g_pSkillWnd, 12005, 0)` (network.c:22021-22022): подписи вкладок по новому классу, вкладка 0, прокрутка 0.
	void OnNetSelfJobChanged(int32 OldClassId, int32 Job, int32 ClassId, int32 SubJob);

	void ApplyListRecords(const TArray<uint8>& Body, bool bReplace);
	// 5252 (`Net_RecvLearnedSkillList@0xE56090`): первые 35 записей по 3 б, каждой — «можно вкачивать» = 1 (★ 22.10.2026, см. .cpp).
	void ApplyCompactList(const TArray<uint8>& Body);
	void ApplyUpgradeResult(const TArray<uint8>& Body);
	void ApplyCooldowns(const TArray<uint8>& Body);
	void DrainPending();

	// ── Виджеты ──
	bool EnsureWidget();
	void ResetWidgets();
	void ScanWidgets();
	void BindSlotGroup(UCanvasPanel* Grp, int32 SlotIdx);
	void BuildCoinChildren(int32 SlotIdx);
	void BindButtons();

	// ── Наполнение ──
	void RebuildAll();          // вкладки + сетка + счётчики (эталон: связка Refresh+BuildTree)
	void RebuildTabs();         // подписи и видимость селекторов вкладок и кнопки 4-й профессии
	void RebuildGrid();         // 45 узлов: иконка, уровень, оверлеи, цены
	// ★★★ ШТОРКИ ОТКАТА НА УЗЛАХ. Покадрово из `Tick`: откат непрерывен, а `RebuildGrid` идёт по
	//   событию. Данные — ТОЛЬКО через `UAsdaHotbarSubsystem::QuerySkillCooldown` (закон
	//   `port-shared-points`), механика кадров — там же (`DriveCurtain`), своих копий нет.
	void TickCooldownCurtains();
	void RefreshPointTexts();   // счётчики очков (свободные + вложенные в дерево)
	void RefreshPreviewPanel(); // левая панель «Хранитель души» (56300) из SoulGuarderTable
	void RefreshTreeArt();      // план-подложка дерева (SKILLTREE_ARR_*), сдвиг по прокрутке
	void BuildTabCaptions();    // подписи опций селекторов вкладок (импортёр свёл их в один TextBlock)
	void ApplyTextStyles();     // цвет/выравнивание ВСЕХ текстов окна по блокам EXS (ui-text-color-exs)
	void ShowSoulTooltip(int32 RowIdx, bool bBig);
	// ── Полоса прокрутки сетки. Контрола в JUI НЕТ: движок строит её из ключа скина колёсного
	//   окна 12291 `ScrollBarFileNameVert = "Nomal_Scr"` (тот же механизм, что у инвентаря).
	//   ★ Аудит №3 (A1): скин, раскладка планов и разбор ввода — в `FAsdaScrollBarNomal`; эти три
	//     метода остались как СВЯЗКА с моделью окна (позиция полосы, диапазон рядов, применение).
	void BuildScrollBar();
	void LayoutScrollBar();
	void TickScrollBar(bool bLmb, bool bLmbDownEdge);
	bool IsCursorOverScrollBar() const;   // подсказка панели (эталон msg 9 контролов 56302/56304/56308/56310)
	// Показать/спрятать ВСЁ поддерево виджета. Нужно планам `SKILLTREE_ARR_*`: в JUI все их потомки
	//   vis=1, а импортёр свернул созданные им union-группы — без рекурсии подложка остаётся пустой.
	static void SetSubtreeVisible(UWidget* Root, bool bVisible);

	// ── Курсор: hover/клик по узлам (эталон msg 8/9/0xA/0xC) ──
	// ★ Аудит №3 (A1): своего `IsCursorOver` больше нет — хит-тест «курсор над виджетом» у всего
	//   порта один (`AsdaUi::IsCursorOverWidget`, тело было побайтово таким же).
	void TickPointer();
	void ShowSlotTooltip(int32 SlotIdx);
	// ── Подсказка навыка 1:1 (`CSkillTooltip_BuildAndRender@0xEA7260`). Разбор — в .cpp над реализацией.
	void BuildSkillTooltip(int32 SkillCode, int32 Level, bool bAvailable, bool bAllowNextPreview,
	                       FString& OutTitle, FString& OutBody) const;
	// ── Подсказка НАВЫКА СОУЛМЕЙТА 1:1 (`SkillTooltip_BuildSoulmateTooltip@0xEAF450`,
	//    skill_buff.c:36778-37105). Две таблицы: `soulmateskilltable` (272 б — имя +4, описание +100)
	//    и `soulmateskill` (112 б — требования и характеристики). Разбор — в .cpp над реализацией.
	bool BuildSoulmateSkillTooltip(int32 SkillId, FString& OutTitle, FString& OutBody) const;
	// Подстановка токенов описания ([ms]/[s1]/[sr]/[sdt1]…) — `CSkillInfo_BuildSkillDescription@0xEB2D50`.
	FString ExpandSkillDescription(int32 SkillCode, int32 Level) const;
	// Категория навыка 0..3: цвет заголовка и нотис «Тип навыка» (9507..9510).
	static int32 SkillCategoryOf(const struct FAsdaSkillDef& D);
	void HideOwnTooltip();
	void OnSlotClicked(int32 SlotIdx);
	// ★★★ ПРИМЕНЕНИЕ НАВЫКА ИЗ ОКНА = эталонное событие **0xD** окна навыков
	//   (`skillWndOnUICommand@0xE4A8E0` → `skillDispatchUseByBaseId@0xE57CB0`), а 0xD — это
	//   ДВОЙНОЙ ЛЕВЫЙ КЛИК. Таблица событий контрола и её вывод — в шапке `OnSlotClicked` (.cpp).
	void OnSlotDoubleClicked(int32 SlotIdx);

	// ── Перетаскивание навыка в панель быстрого доступа (эталон msg 5 → msg 7, разбор в .cpp) ──
	bool TickSkillDrag();              // true = событие мыши обслужено перетаскиванием
	void BeginSkillDrag(int32 SlotIdx);
	void UpdateSkillDragGhost();
	void CancelSkillDragVisual();
	void EndSkillDrag();

	// ── Зона интерфейса и колесо ──
	bool GetWindowRect(FVector2D& OutMin, FVector2D& OutMax) const;
	bool OnWheel(int32 Notches);

	// ── Утилиты ──
	// ★ Аудит №3 (A1): обёртка над общей точкой `AsdaUi::LocRaw(GI, Id, /*bDebugFallback*/ true)` —
	//   своего тела у неё больше нет. `GetTextByIdRaw`, потому что `GetTextById` режет inline-теги.
	FString LocRaw(int32 Id) const;
	// ★ Эталон различает ДВА канала: однокнопочный msgbox (`CItem_BuildTooltipText`, ONEBUT_MSGBOX 1500)
	//   и строку в чат (`CChat_AddTokenizedLines`). В этом окне в чат уходит ровно один текст — 107022.
	// ★ Аудит №3 (A1): полотно msgbox собирает оконная подсистема (`ShowGameMessage`), здесь — вызов.
	void    MsgBox1(const FString& Text) const;           // ONEBUT_MSGBOX 1500 с кнопкой 2288 «ОК»
	void    Notice(int32 LocId) const;                    // loc → msgbox (9421, 12854, 12855, отказы 5254)
	void    ChatNotice(int32 LocId) const;                // loc → строка в чат (только 107022)
	void    RefusalHint(int32 LocId) const;               // «пустой» клик по узлу: эталон молчит, у нас лог
	void    ApplyIcon(UImage* Img, const FString& TexName) const;
	// То же, но произвольным прямоугольником (портрет хранителя 126x190 — эталонный DrawTextureRect).
	void    ApplyPanelTexture(UImage* Img, const FString& TexName, const FVector2D& DrawSize) const;
	int32   MaxScrollRow() const;

	UFUNCTION() void OnCloseClicked();
	UFUNCTION() void OnCloseHovered();
	UFUNCTION() void OnCloseUnhovered();

	// ── Данные ──
	UPROPERTY(Transient) TObjectPtr<UAsdaSkillDatabase> Db = nullptr;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;
	UPROPERTY(Transient) TArray<FAsdaSkillSlotWidgets> Slots;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// Именованные контролы окна (адресация по idnum задокументирована в отчёте).
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSkillPoint   = nullptr;  // 1092  SKILL_POINTTXT
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPointBack    = nullptr;  // 12262 SKILL_POINTBACK (нотис 12136)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTreeCaption  = nullptr;  // 12261 SKILL_TXT00
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTreeSpent    = nullptr;  // 12007 SKILL_POINT_TXT
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtJob4Point    = nullptr;  // 12413 SKILL_POINTTXT2
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtJob4Back     = nullptr;  // 12411 SKILL_POINTBACK2 (нотис 111501)
	UPROPERTY(Transient) TObjectPtr<UWidget>    Job4Frame       = nullptr;  // 12400 SOUL_SP_W
	UPROPERTY(Transient) TObjectPtr<UButton>    Job4Button      = nullptr;  // 12401 SOUL_SP_W_T
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Job4ButtonText  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UWidget>    Job4PointFrame  = nullptr;  // 12410 SKILL_BLINE_2
	UPROPERTY(Transient) TObjectPtr<UButton>    CloseButton     = nullptr;  // 12009
	UPROPERTY(Transient) TObjectPtr<UImage>     GearStatic      = nullptr;  // 12002
	UPROPERTY(Transient) TObjectPtr<UWidget>    GearAnimated    = nullptr;  // 12003
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    Tab3Buttons;        // 12005 SKILL_CHARA_BTN1_OPT0..2
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Tab3Texts;          // подписи опций (создаются в рантайме)
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    Tab4Buttons;        // 12006 SKILL_CHARA_BTN2_OPT0..3
	// Базовые цвета подписей вкладок: снимает ИЗ АССЕТА общая точка `AsdaUi::ApplyTabCaptionState`
	//   (подсветка активной — там же). Сбрасываются, когда подписи пересоздаются.
	TArray<FLinearColor> Tab3IdleColors;
	TArray<FLinearColor> Tab4IdleColors;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Tab4Texts;
	// Исходные (сведённые импортёром) TextBlock'и селекторов — служат ШАБЛОНОМ шрифта/цвета для подписей опций.
	// Планы-фоны опций 12005 (`SKILL_CHARA_BTN1_{juiIdx}_{опция}`) — на них переключается UV «выбрано».
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> Tab3Planes;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Tab3TextTemplate = nullptr;  // SKILL_CHARA_BTN1_Text
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Tab4TextTemplate = nullptr;  // SKILL_CHARA_BTN2_Text
	// Панель «Хранитель души» слева (56300 и её содержимое). ⚠ Это НЕ превью выбранного навыка:
	//   эталон берёт содержимое из `SoulGuarderTable.bin` по (SkillType, RowIndex), см. FAsdaSoulPreviewRow.
	UPROPERTY(Transient) TObjectPtr<UWidget>    PreviewPanel    = nullptr;  // 56300 SKILL_SOUL_MAIN
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PreviewTitle    = nullptr;  // 56301 SKILL_SOUL_TEXT_p1 (нотис 110500)
	UPROPERTY(Transient) TArray<FAsdaSoulPreviewRow> SoulRows;              // 2 строки: 56302.. и 56308..
	// Планы-подложки дерева (эталон `skillPanel_selectTreeByClass@0xE581A0`, таблица из 15 контролов
	//   `SKILLTREE_ARR_*`: 12191..12199 по (полоса, вкладка) + 12264/12266/12268 под вкладку 4-й профессии).
	//   Индексы 0..8 = 3·(полоса−1)+вкладка, 9/11/13 = вкладка 3 полос 1/2/3.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> TreeArt;
	// Полоса прокрутки: канва-владелец (группа колёсного окна 12291) и сама полоса.
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> WheelPanel  = nullptr;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> ScrollHost  = nullptr;  // родитель колёсного окна
	// ★ Аудит №3 (A1): четыре плана, их UV, латч захвата бегунка и разбор ввода живут в ОБЩЕЙ полосе
	//   (`FAsdaScrollBarNomal`), а у окна остаётся только своё: где полоса стоит и что значит
	//   «прокрутить». Прежние поля `ScrollTrack/ScrollThumb/ScrollUp/ScrollDown/ScrollPressedPlane/
	//   bScrollThumbGrab` были второй копией того же самого и здесь больше не нужны.
	UPROPERTY(Transient) FAsdaScrollBarNomal ScrollBar;
	// Шаблоны цены слота (эталон 56320/56325..56327): из них копируются кисть и шрифт.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     CoinTemplates;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CoinTextTemplates;

	// ── Состояние ──
	TMap<int32, FAsdaLearnedSkill> Learned;
	// Откат узла: полная длительность, остаток и (★ 25.10.2026) момент готовности по РЕАЛЬНЫМ часам `AsdaCooldownNowS()`.
	//   [ИСПРАВЛЕНИЕ] Прежде остаток уменьшался на `DeltaTime` мира, а мир режет кадр до 0,4 с (`MaxUndilatedFrameTime`):
	//   любой рывок (загрузка модели, портал) терял секунды, и шторка/цифра отставали от сервера. Ключ `asda.Cooldown.RealClock`.
	struct FSkillCooldown
	{
		float  TotalSec  = 0.f;    // полная длительность, с (знаменатель доли для стартового кадра шторки)
		float  RemainSec = 0.f;    // остаток, с (при ключе 1 пересчитывается из EndS каждый тик)
		double EndS      = 0.0;    // момент готовности по `AsdaCooldownNowS()` (0 — не задан)
	};
	TMap<int32, FSkillCooldown> Cooldowns;
	// Живой остаток записи: при `asda.Cooldown.RealClock 1` — EndS − сейчас (реальные часы), иначе — накопленный RemainSec.
	float CooldownRemainLive(const FSkillCooldown& C) const;
	// ★ 25.10.2026 (ревизия 1.1): порядок прихода откатов (разбор — у `NextCooldownArrivalSeq`).
	uint64 CooldownArrivalSeq = 0;            // счётчик приходов 5465/5271/5274 (общий для книги и диспетчера навыков)
	uint64 CooldownSnapshotSeq = 0;           // номер прихода последнего снимка 5465 (0 — снимка не было)
	TMap<int32, uint64> CooldownReleasedSeq;  // навык → номер прихода последнего 5274 (только пришедшие после снимка)
	bool bCooldownsRebuild = false;           // 5465 применён в момент приёма — перерисовать сетку в ближайшем `DrainPending`

	int32 SkillPoints  = -1;   // −1 = сервер ещё не присылал (пакет полного статуса в порт не заведён)
	int32 CurTree      = 0;    // вкладка 0..3 (эталон m_nCurTreeIdx)
	int32 ScrollRow    = 0;    // прокрутка сетки в РЯДАХ (эталон m_nSelectedOptionIdx)
	int32 SelectedSlot = -1;   // выбранный узел (эталон m_nSelectedSlotIdx)
	int32 PendingLearnCode = -1;   // навык, по которому открыт диалог подтверждения
	int32 HoverSlot    = -1;
	TMap<int32, float> CedTimers;  // узел → остаток показа вспышки «кулдаун закончился»
	int32 SoulHoverRow = -1;       // строка левой панели под курсором (0/1), −1 = нет
	bool  bSoulHoverBig = false;   // true = портрет (56302/56308), false = иконка навыка (56304/56310)
	// ★★★ Аудит №3 (B3): ДВА экземпляра общей точки, и это НЕ дубль (прежние `bMouseWasDown` и
	//   `bScrollWasDown`). `TickSkillDrag` идёт ПЕРВЫМ и при активном перетаскивании возвращает true,
	//   после чего `TickPointer` выходит НЕ ДОЙДЯ до полосы прокрутки — её латч в такие кадры не
	//   обновляется. Один общий латч склеил бы два разных потока событий и «съел» бы фронт полосы.
	FAsdaMouseFrame DragMouse;     // фронт нажатия ЛКМ у сетки навыков (перетаскивание и клики)
	FAsdaMouseFrame ScrollMouse;   // отдельный фронт для полосы прокрутки
	// Перетаскивание навыка: источник и код в руке.
	int32 DragSrcSlot   = -1;
	int32 DragSkillCode = -1;
	bool  bDragActive   = false;
	// ★★★ ВЗВОД ЗАХВАТА — общая точка `FAsdaDragArm` (`AsdaUiHelpers.h`), одна на три окна.
	//   `KeyA` = номер узла сетки, `KeyB` не используется (−1). Раньше здесь лежали СВОИ поля
	//   `DragArmSlot`/`DragArmPos` и свой порог `DragStartPixels` — единственный порог из трёх окон;
	//   сумка и панель тащили прямо с нажатия. Именно это расхождение и увидел юзер в живом тесте
	//   (закон `port-shared-points`). Разбор: `Reports/icon_drag_threshold.md`.
	FAsdaDragArm DragArm;
	// ★★★ ДВОЙНОЙ ЛЕВЫЙ КЛИК ПО УЗЛУ = эталонное событие **0xD** (`skillDispatchUseByBaseId`).
	//   Оконный менеджер оригинала получает его от Windows (`WM_LBUTTONDBLCLK` 0x203 →
	//   `CWindowMgr_QueueLButtonUp@0x12EFFF0`, вид очереди **4**); у нас Slate отдаёт только «нажата
	//   /не нажата», поэтому второе нажатие по ТОМУ ЖЕ узлу в пределах системного интервала
	//   (`FSlateApplication::GetDoubleClickTime`) распознаём сами. Полное доказательство раскладки
	//   событий — в шапке `OnSlotClicked` (.cpp).
	int32  LastDownSlot  = -1;     // узел предыдущего нажатия (для распознавания дубля)
	double LastDownTimeS = 0.0;    // время предыдущего нажатия, сек реального времени
	// ★ Аудит №3 (B4): призрак перетаскивания — общая точка `FAsdaDragGhost` (третий из трёх
	//   экземпляров; тело слежения совпадало с инвентарём ПОБАЙТОВО).
	UPROPERTY(Transient) FAsdaDragGhost DragGhost;
	bool  bTipShown    = false;
	bool  bNetBound    = false;
	bool  bZoneBound   = false;
	int32 JobOverride    = -1; // консольная подмена класса (проверка всех наборов без сервера)
	int32 SubJobOverride = -1;

	// Очереди сетевых тел (колбэк только копит).
	//   ★ Аудит №3 (B10): шесть массивов под общим `FCriticalSection` — шесть общих `TAsdaNetQueue`.
	//   Раздельные замки поведение не меняют: порядок применения МЕЖДУ очередями и раньше был
	//   фиксированным (полный список → добавления → уплотнение → результат → откаты → очки),
	//   независимо от порядка прихода пакетов, а сам приём идёт на игровом потоке.
	TAsdaNetQueue<TArray<uint8>> PendingFull, PendingUpdate, PendingCompact, PendingResult, PendingCool;
	TAsdaNetQueue<int32> PendingPoints;
	// ⚠ ЭТИ ДВА — НЕ ОЧЕРЕДИ, А ЗАЩЁЛКИ, и в `TAsdaNetQueue` их переводить НЕЛЬЗЯ: сервер шлёт 6057
	//   пачками, и очередь дала бы N очисток вместо одной. Поэтому у них остаётся свой замок —
	//   он больше не общий с телами пакетов, а только их.
	bool bPendingListClear = false;   // 6057: тихая очистка списков (эталон ничего не показывает)
	bool bPendingFullReset = false;   // 5462: полный сброс навыков + msgbox 12855
	bool bPendingJobChanged = false;  // 5042: профессия сменилась — вкладка 0 и перерисовка по новому классу (защёлка, не очередь)
	FCriticalSection PendingFlagsLock;

public:
#if !UE_BUILD_SHIPPING
	// Самотест `asda.Soulmate.OfflineGateSelfTest`: заголовок подсказки навыка Дружбы (красный — навык недоступен).
	FString DebugSoulTooltipTitle(int32 SkillId) const { FString T, B; BuildSoulmateSkillTooltip(SkillId, T, B); return T; }
#endif
	// ★ 07.11.2026: текст подсказки ячейки лотка навыков Дружбы — та же сборка, что у слота панели.
	bool BuildSoulTrayTooltip(int32 SkillId, FString& OutTitle, FString& OutBody) const { return BuildSoulmateSkillTooltip(SkillId, OutTitle, OutBody); }
	// ── Editor-Friendly (закон ue-editable-in-ue) ──
	// Геометрия сетки взята из JUI (`Reports/_skillwin_jui.json`): узел 00 в (6,2), шаг 80×53.
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Сетка") int32 GridColumns = 5;
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Сетка") int32 GridRows = 9;
	// Шаг ряда сетки в пикселях. Эталон сдвигает план-подложку дерева на «−53 * прокрутка»
	//   (`skillPanel_selectTreeByClass@0xE581A0`), тот же шаг у групп узлов в JUI.
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Сетка") int32 TreeRowStep = 53;
	// Размер колёсного окна 12291 (JUI Size 400×477) — высота полосы прокрутки и её X (правый край).
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Сетка") FVector2D WheelRectSize = FVector2D(400.f, 477.f);
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Иконка") float IconDrawSize = 32.f;
	// Запасная длительность вспышки CED, если у плана в WBP не заполнен FrameInterval.
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Иконка") float CedFlashSeconds = 1.f;
	// ── Перетаскивание. Значения совпадают с панелью и инвентарём (единообразие по требованию юзера):
	//   призрак 40 % прозрачности, исходная иконка приглушена.
	//   ⚠ Порога здесь БОЛЬШЕ НЕТ: он переехал в общую точку `FAsdaDragArm::kDragStartPixels`
	//   (`AsdaUiHelpers.h`) — один на сумку, панель и книгу навыков. Редактируемым полем окна он
	//   быть не может, иначе три окна снова разъедутся.
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Перетаскивание") float DragGhostOpacity = 0.4f;
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Перетаскивание") float DragSourceOpacity = 0.35f;
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Перетаскивание") FVector2D DragGhostCursorOffset = FVector2D(-16.f, -16.f);
	// Шаблоны цены. ★ Смещения из ЭТАЛОНА, а не из JUI: `CSkillWindow_InitControls@0xE50DB0` делает
	//   `CControl_SetField74_75(шаблон 56320, 39, 2)` и клонирует его В КОНТРОЛ СЛОТА (`SKILLTREE_ICON{NN}`,
	//   42×42), поэтому (39,2) отсчитывается ОТ РАМКИ СЛОТА, а не от группы-обёртки, которую сделал
	//   импортёр (в ней рамка стоит на (11,11)). Отсчёт ведём от фактической позиции рамки — см. BuildCoinChildren.
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Цена") FVector2D CoinOrigin = FVector2D(39.f, 2.f);
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Цена") float CoinRowStep = 12.f;
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Цена") FVector2D CoinBallSize = FVector2D(14.f, 14.f);
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Цена") FVector2D CoinTextOffset = FVector2D(14.f, -1.f);
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Цена") FVector2D CoinTextSize = FVector2D(16.f, 13.f);
	// Z окна: выше инвентаря (92 у хотбара), ниже модалок.
	UPROPERTY(EditAnywhere, Category="Asda|Навыки") int32 WindowZ = 94;
	// Звуки открытия/закрытия окна (эталон csoundPlayName2DStore 5 / 6).
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Звук") int32 OpenSoundId = 5;
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Звук") int32 CloseSoundId = 6;
	// Ширина подсказки узла.
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Подсказка") int32 TipFontPx = 8;
	// Ширина плашки подсказки. Эталон (`CSkillTooltip_BuildAndRender`, хвост) ставит 218 + 40 = 258.
	UPROPERTY(EditAnywhere, Category="Asda|Навыки|Подсказка") int32 TipMaxWidth = 258;
};
