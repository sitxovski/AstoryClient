// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSoulGuardSubsystem — СИСТЕМА «ХРАНИТЕЛЯ» (Soul Guardian / Soul Boost), ЭТАП 3 MINISTATE_WIN.
//   Окно `Soul_Boost_Win` (IDNum 56000) + плавающая плашка `Soul_Boost_Texts` (56200), «монеты душ»
//   (3 ряда × 10), 3 скилла хранителя с хоткеями 45/46/47, пассивный бафф уровня хранителя,
//   опкоды 6156/6157 (приём) и 6158 (отправка), две клиентские таблицы.
//   Оба под-окна физически лежат ВНУТРИ WBP_MINISTATE_WIN — виджет берётся у UAsdaMiniStateSubsystem,
//   свои виджеты НЕ создаются (законы no-invented-ui / ui-editable-in-ue).
//
// ════════════════════════════════ ЭТАЛОН (сборка IDB) ══════════════════════════════════════════
// Класс окна в клиенте — глобаль `g_pSoundOrAnimCtx`, конструктор `soulCraftWnd_init@0xEF2F40`
//   (ui_core.c:92214). Поля объекта (dword-индексы) → наши члены:
//     [0]        корневой контрол 56000                    → RootHit / RootGroup
//     [23i+1]    Soul_Boost_BOX_{i+1}   (56010/56050/56100) → RowBox[i]
//     [23i+2]    Soul_Boost_MARK_{i+1}  (56013/56053/56103) → RowMark[i] (иконка скилла 32×32)
//     [23i+3]    Soul_Boost_GTEXT{i+1}  (56334..56336)      → RowGText[i], alpha 0x80
//     [23i+4]    BOLT (56337/56338, ТОЛЬКО i<=1)            → RowBolt[i],  alpha 0x40
//     [23i+5]    Soul_Boost_main_{i+1}  (56011/…)           → RowMain[i],  alpha 0x40
//     [23i+6]    Soul_Boost_bb_back_{i+1}(56014/…)          → RowBallBack[i]
//     [23i+7]    Soul_Boost_backg_{i+1} (56012/…)           → RowBackg[i]
//     [23i+8+j]  шары ряда, j = 0..9                        → RowBall[i][j]
//                ★ ПОРЯДОК: цикл `for (j = 9; j >= 0; --j) obj[23i + (9-j) + 8] = ID(v6 + j + 56021)`
//                  ⇒ [0] = D{i+1}_10 (X = 0), [9] = D{i+1}_1 (X = 36): заполнение СЛЕВА НАПРАВО
//                  (5 зелёных, 3 синих, 2 красных).
//     [23i+18]   BLACKCIRCLE (56341..56343)                 → RowBlackCircle[i], alpha 0x40
//     [23i+19]   Soul_Boost_D{i+1} (56020/56060/56110)      → RowDn[i]
//     [23i+20]   soul_boost_mark_ani_{i+1} (56251..56253)   → RowMarkAni[i]  (SOULBALL_ANI, ≥5 монет)
//     [23i+21]   soul_boost_ballani_{i+1}  (56255..56257)   → RowBallAni[i]  (SOULBAR_ANI,  ≥8 монет)
//     [23i+22]   soul_boost_hot{i+1}       (56261..56263)   → RowHotText[i]
//     [23i+23]   байт-флаг «показать mark_ani один раз»     → bMarkAniPending[i]
//     [70]/[71]  SOULBar_right 56345 / SOULBar_left 56346   → BarRight / BarLeft (ручки перетаскивания)
//     [72]/[73]/[74] Ball_Red 56331 / Ball_green 56333 / Ball_Blue 56332 → FlashRed/Green/Blue
//     [75]/[76]  b_ani2 56208 (стрелка ВВЕРХ) / b_ani1 56207 (стрелка ВНИЗ) → ArrowUp / ArrowDown
//     [78]       список ЛЕТЯЩИХ МОНЕТ                        → FlyingCoins
//     [81..83]   мировая точка, к которой летят монеты       → CoinTargetWorld
//     [84..86]   ТЕКУЩИЕ счётчики монет по рядам             → Coin[3]
//     [87..89]   ПРЕДЫДУЩИЕ счётчики                         → PrevCoin[3]
//     [91]/[92]  SoulGuarderTable: count / ptr (шаг 1956)    → GuarderTable
//     [93]/[94]  SkillCoinTable:   count / ptr (шаг 20)      → SkillCoinTable
//     [95]       карта «userIdx → SoulGuardIndex» из 6156    → SoulGuardIndexByUser
//     [102+2i]   байт «ряд доступен» (count >= 5)            → bRowAvailable[i]
//     [103+2i]   skillCode ряда (−1 = нет)                   → RowSkillCode[i]
//     [108]      Soul_Boost_Texts 56200 (плавающая плашка)   → TextsHit / TextsGroup
//     [109..111] Texts_1..3 (SOUL BURST, green/blue/red)     → BurstPanel[i]
//     [112..114]   их дети Text_burst_g/b/r                  → BurstLabel[i]
//     [115..117]   их дети SOUL_1/2/3 (цифра уровня)         → BurstDigit[i]
//     [118..120] Texts_4..6 (SOUL GUARDER)                   → GuardPanel[i]
//     [121..123]   их дети Text_gaurder_g/b/r                → GuardLabel[i]
//     [124..126]   их дети SOUL_1/2/3                        → GuardDigit[i]
//     [128]      Soul_Boost_cbox 56264 (CheckButton)         → PinCheck
//     [129]/[130] X/Y «приколотой» позиции окна              → PinnedPos
//     [308] байт  «окно показано»                            → bWindowShown
//     [524] байт  «инициализация завершена»                  → bInitDone
//
// ФУНКЦИИ ЭТАЛОНА → НАШИ МЕТОДЫ (все портированы построчно):
//   `hudQuickSlotLoadSkills@0xEF44E0`        → LoadRowSkills(SkillType)
//   `hudQuickSlot_TryUseSkill@0xEF4600`      → TryUseSkill(Row)
//   `hudQuickSlot_RefreshIcons@0xEF4800`     → RefreshIcons()
//   `uiSlotState_UpdateArrowIndicators@0xEF56D0` → UpdateArrowIndicators(c0,c1,c2)
//   `hudSkillPopup_RefreshOrHide@0xEF6A30`   → RefreshOrHide()
//   `uiZoneFadeAndNameplate_update@0xEF38E0` → UpdateBanner()  (плашка SOUL BURST / SOUL GUARDER)
//   `cameraTarget_UpdateAbovePlayer@0xEF6790`→ TickFrame()     (порядок вызовов 1:1)
//   `coinDropMgr_UpdateFlyToPlayer@0xEF5BC0` → TickFlyingCoins()
//   `CharFx_CreateSkillCastEffect@0xEF50E0`  → SpawnFlyingCoin(c0,c1,c2,skillId)
//   `recvSkillSlotState@0xEF6B10`            → OnSlotState6157(...)
//   `charHandleSlotKeyPacket@0xEF6D00`       → OnSoulGuardIndex6156(...)
//   `CItemMgr_LoadSoulGuarderTable@0xEF4E30` / `CItemMgr_LoadSkillCoinTable@0xEF4F90` → LoadTables()
//
// ★★★ ГЛАВНОЕ ОТКРЫТИЕ ЭТОГО ЭТАПА (снимает «кажущийся дефект» реверса):
//   при `AddFlag == 1` (НАЧИСЛЕНИЕ) `recvSkillSlotState` НЕ обновляет счётчики напрямую — он создаёт
//   ЛЕТЯЩУЮ МОНЕТУ (`CharFx_CreateSkillCastEffect`, jeb `eff_1_char_99_soulritec-a_03.jeb`, звук
//   `S_e_sb_coin_00.wav`) и кладёт в неё новые значения (+2200/+2204/+2208). Счётчики применяются
//   ТОЛЬКО когда монета ДОЛЕТЕЛА до игрока (`coinDropMgr_UpdateFlyToPlayer`, math_geom.c:2063):
//   там и `uiSlotState_UpdateArrowIndicators(новые)`, и звук `S_e_sb_coin_01.wav`, и `RefreshIcons`.
//   Единственное исключение — самый первый пакет: если все три счётчика были 0, они присваиваются
//   сразу (network.c:33987). Так что «шары не обновляются на начислении» — НЕ баг, а анимация.
//
// ═══════════════════════════ МЕХАНИКА (АВТОРИТЕТ = СЕРВЕР) ═════════════════════════════════════
// Всё ниже — из `RE/ida/server_work/GameServer.c` (класс `CSoulGuarderManager`), ПОДТВЕРЖДЕНО
//   живыми логами сервера `GameServer/Log/Log_SoulGuarder/*.txt` (см. отчёт ЭТАП 3):
//   • Монеты дают ТОЛЬКО боевые скиллы с `500 <= skillId < 1200` (`sub_48AFF0`, GameServer.c:55633).
//     Вызывается из ПАКЕТОВ БОЕВОГО СЕРВЕРА 8206/8208, не от клиента.
//   • Сколько монет в какой ряд — `SoulGuarderCoinTable[skillId].coin[0..2]` (клиентская копия =
//     `SkillCoinTable.bin`).
//   • Ряды выше грейда игрока не копятся, а ОБНУЛЯЮТСЯ: гейт `i < CUser+435`, где `CUser+435`
//     это `iJobCount` — число пройденных смен класса (0..5, GameServer.c:39163/39208).
//   • Кап 10 монет на ряд (GameServer.c:55649).
//   • РАСПАД: −1 по КАЖДОМУ ряду каждые 15 000 мс (`sub_48B4C0` из `TimerProcess`, гейт
//     `GetTickCount() >= CUser+259169`, затем `+= 15000`). Лог 2026-01-06 подтверждает шаг ровно 15 с.
//   • Порог **5** монет в ряду = скилл ряда ДОСТУПЕН (сервер `sub_48B780`, клиент `count >= 5`).
//   • Порог **8** монет = «уровень хранителя»: `for (i=0; i<3 && coin[i] >= 8; ++i) lvl = i`
//     (`sub_48B210`, GameServer.c:55712). Лог: 6→8 монет → `[CHANGE] SoulGuardIndex:15`;
//     распад 8→7 → `[CHANGE] SoulGuardIndex:-1`.
//   • По уровню и SkillType сервер выбирает запись `SoulGuarderInfoTable` и накладывает ПАССИВНЫЙ
//     бафф `buffSkillId` (1201..1227) — обычный абнормал с `id >= 1201`, которому клиент НАМЕРЕННО
//     НЕ РИСУЕТ ТАЙМЕР (`Skill_DrawBuffTimerIcon`: `rec+2 >= 1201` → скрыть). Наложение идёт
//     штатным путём эффектов (5272/4102/6154) — ЭТАП 2 его уже принимает.
//   • Использование скилла хранителя списывает **5** монет с ряда этого скилла — жёсткая константа
//     `sub_48B570(mgr, uid, row, 5)` (GameServer.c:57366 и :57475), а НЕ стоимость из
//     `SoulGuarderSkillTable[+108..110]`. Табличная стоимость применяется к ЦЕЛЯМ (`sub_48E260`).
//   • Смерть игрока сбрасывает всё (`sub_48DD60`: 6157 AddFlag=0 нулями + 6156 index=−1).
//
// ═══════════════════════════════════ ЗАКОНЫ ════════════════════════════════════════════════════
//   no-invented-ui / ui-editable-in-ue — только узлы импортированного WBP, все параметры в UPROPERTY.
//   never-invent-only-original — каждое число здесь имеет источник (адрес функции / строка таблицы /
//     строка серверного лога); ничего «по аналогии».
//   ui-window-drag-hittest — перетаскивание окна 56000 = ДАННЫЕ JUI (EVS 6/0x20003 у 56345/56346),
//     его исполняет UAsdaWindowSubsystem по DA_JuiWindowMeta; здесь только ВКЛ/ВЫКЛ ручек чекбоксом.
//   fix-original-defects — исправления помечены ★ и вынесены во флаг (см. bLegacy*).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Data/AsdaSoulGuardRows.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaSoulGuardSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UCheckBox;

// Одна ЛЕТЯЩАЯ МОНЕТА — узел списка `g_pSoundOrAnimCtx[78]` (эталонный объект 0x8A4 б).
//   Смещения полей эталона указаны в комментариях: они и есть смысл каждого члена.
USTRUCT()
struct FAsdaSoulFlyingCoin
{
	GENERATED_BODY()
	UPROPERTY() FVector Pos = FVector::ZeroVector;   // +2176/2180/2184 — текущая позиция (мир)
	UPROPERTY() float   Accel = 0.f;                 // +2188 — общий множитель, растёт 0 → 1 по 0.1/кадр
	UPROPERTY() float   UpSpeed = 15.f;              // +2192 — вертикальная составляющая, падает по 0.1/кадр до 0
	UPROPERTY() float   Speed = 0.f;                 // +2196 — скорость к игроку, растёт 0 → 30 по 1.0/кадр
	UPROPERTY() int32   Coin0 = 0;                   // +2200 — новые счётчики, применяются ПО ПРИЛЁТЕ
	UPROPERTY() int32   Coin1 = 0;                   // +2204
	UPROPERTY() int32   Coin2 = 0;                   // +2208
	UPROPERTY() int32   SkillId = -1;                // скилл, породивший монету (для лога)
};

UCLASS()
class ASDAREBORN_API UAsdaSoulGuardSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSoulGuardSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ══════════════════════════ Публичный API ══════════════════════════════════════════════════

	// Приём 6157 (11 б) — `recvSkillSlotState@0xEF6B10` 1:1.
	void OnSlotState6157(int32 UserIdx, int32 AddFlag, int32 SkillType, int32 SkillIndex,
	                     int32 C0, int32 C1, int32 C2);
	// Приём 6156 (5 б) — `charHandleSlotKeyPacket@0xEF6D00` + применение в `charVisibilityStateRefresh`.
	//   ★★★ Ключ пакета — charId (`CUser+372`), НЕ сессионный userIdx: сверка с
	//   `UAsdaNetSubsystem::SelectedCharId`. Обоснование — у делегата `OnSoulGuardIdx`.
	void OnSoulGuardIndex6156(int32 CharId, int32 SoulGuardIdx);

	// ══════ ★★★ СЕТЕВОЙ ТРАКТ ПРИМЕНЕНИЯ СКИЛЛА ХРАНИТЕЛЯ (свой И ЧУЖИЕ), 2026-08-08 ═══════════
	// Сервер после подтверждения BattleServer рассылает ПО ВСЕМУ СЕКТОРУ (включая кастера):
	//   6161 (7 б, «каст начался/кончился») + 6159 (8 б шапка + N×29 записей, результат каста);
	//   у скиллов с типом мотиона «каст» вместо 6159 идёт 6160. Списание 5 монет сервер делает
	//   ТАМ ЖЕ (`sub_48B570(uid, ряд, 5)`, GameServer.c:57366/57475) и присылает владельцу 6157
	//   с AddFlag = 0 — то есть монеты снимает СЕРВЕР, клиент их не трогает.
	// Приём 6159 — `Recv_SkillCast@0xF4E370` (network.c:44800).
	void OnSkillCast6159(int32 Result, int32 CasterUserIdx, int32 SkillCode, int32 Flag, int32 HitCount);
	// Приём 6160 — `Recv_SkillCastIdle@0xF4D9C0` (network.c:44642), начало долгого каста.
	void OnSkillCastIdle6160(int32 CasterUserIdx, int32 SkillCode, int32 Flag, int32 HitCount);
	// Приём 6161 / 5271 — `recvOtherCharSkillUse@0xF4D7D0` (network.c:44599).
	void OnSkillCastState(int32 Result, int32 UserIdx, int32 SkillId, int32 Flag, bool bGuardian);
	// ★★★ П15 (2026-08-11): ПРИНАДЛЕЖНОСТЬ БЕРЁТСЯ ИЗ ДАННЫХ, А НЕ ИЗ ЗАХАРДКОЖЕННОГО ДИАПАЗОНА.
	//   Сравнения `>= 1228 && <= 1254` в оригинале НЕТ НИ В КЛИЕНТЕ, НИ НА СЕРВЕРЕ: константы 1228
	//   в бинаре клиента нет вообще. Активность скилла ряда задают ДВА источника данных:
	//     · `Data\SoulGuarderTable.BIN` — поле `+152 GuardianSkillId` (27 записей, по одному на ряд);
	//     · `SkillCoinTable.bin` — `+16 Field16` = 1/2/3 ровно у 1201..1254 и −1 у всего остального
	//       (это же поле эталон читает для выбора мотиона 5/10/11, `entity_char.c:61316-61325`).
	//   Диапазон-фолбэк остался ТОЛЬКО на случай, когда обе таблицы ещё не загружены (старт сессии).
	bool IsGuardianSkillCode(int32 SkillCode) const;
	// Показать нотис отказа каста по коду результата (эталонная таблица dword_15B5D78).
	void PostSkillFailNotice(int32 ResultCode, int32 SkillCode) const;
	// Код скилла → вариант боевого мотиона (SkillCoinTable +16) → заявка миру (применит его тик).
	void QueueGuardianMotion(int32 CasterUserIdx, int32 SkillCode, const TCHAR* Src);
	// ★★★ П7 (2026-08-11): ОБЫЧНЫЙ навык владельца → компаньону мотион 5 БЕЗ эффекта слота.
	//   Эталон `UpdateList_ModelAnim@0xFA7CE0` (`entity_char.c:61329-61331`): при состоянии владельца
	//   24/14/27/13 и заведённом слоте показа ветка `else` от `activeId > 0x4B1` даёт
	//   `Char_SetMotion(angel, 5, 0, 0)` для ЛЮБОГО навыка с кодом ≤ 1201.
	//   `SkillCoinTable` в этой ветке НЕ читается ⇒ вариант строго 0.
	void QueueNormalSkillMotion(int32 CasterUserIdx, int32 SkillCode, const TCHAR* Src);
	// Разбор очереди выше (в `Tick`): на момент прихода 5257/5270 компаньона могло ещё не быть.
	void DrainNormalSkillMotions();
	// userIdx кастеров обычных навыков, ждущих мотиона компаньона (вариант всегда 0).
	TArray<int32> PendingNormalSkillMotion;
	// ★ Локальное предсказание боевого мотиона компаньона при отправке 6158. В эталоне его НЕТ:
	//   мотион ставит пришедший от сервера каст (6159/6160 → состояние 24/14 → UpdateList_ModelAnim).
	//   Включается автоматически, когда соединения нет (офлайн-проверка эмулятором); при живом
	//   сервере мотион ведёт сервер. `1` = всегда предсказывать, `0` = никогда, `-1` = авто (по умолч.).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard") int32 PredictGuardMotionMode = -1;

	// Хоткеи 45/46/47 (Insert / Home / PageUp) — `hudQuickSlot_TryUseSkill@0xEF4600` 1:1.
	//   Возвращает true, если пакет 6158 ушёл.
	UFUNCTION(BlueprintCallable, Category="Asda|SoulGuard")
	bool TryUseSkill(int32 Row);

	// Перезагрузить skillCode трёх рядов по типу хранителя — `hudQuickSlotLoadSkills@0xEF44E0`.
	void LoadRowSkills(int32 InSkillType);

	// ★ 3D-КОМПАНЬОН: сообщить актору-хранителю, что применён АКТИВНЫЙ скилл (1228..1254).
	//   Вариант боевого мотиона = SkillCoinTable[skillId].Field16 (1/2/3 → мотион 5/10/11 и
	//   эффект слота 31/32/33); −1 у обычного скилла → просто мотион 5 без эффекта (эталон §7.2).
	//   Публично: команда `asda.SoulFx.Skill` дёргает её же.
	void NotifyGuardianSkillUsed(int32 SkillId);

	// Текущее состояние (для дампа/Blueprint).
	UFUNCTION(BlueprintCallable, Category="Asda|SoulGuard") int32 GetCoin(int32 Row) const;
	UFUNCTION(BlueprintCallable, Category="Asda|SoulGuard") bool  IsRowAvailable(int32 Row) const;
	UFUNCTION(BlueprintCallable, Category="Asda|SoulGuard") int32 GetRowSkillCode(int32 Row) const;
	// «Уровень хранителя» = наибольший ряд i, у которого подряд с 0 монет >= 8; −1 = нет.
	//   Формула сервера `sub_48B210` (GameServer.c:55712).
	UFUNCTION(BlueprintCallable, Category="Asda|SoulGuard") int32 GetGuardLevel() const;
	// Запись SoulGuarderTable по SoulGuardIndex (nullptr, если нет).
	const FAsdaSoulGuarderRow* FindGuarderByIndex(int32 Index) const;
	// Запись по паре (SkillType, RowIndex) — эталон `findRecordByTwoKeys@0xEF76B0`.
	const FAsdaSoulGuarderRow* FindGuarderByTypeRow(int32 InSkillType, int32 RowIndex) const;
	// Монеты, которые даёт боевой скилл (клиентская SkillCoinTable). false = записи нет.
	bool GetSkillCoins(int32 SkillId, int32& OutC0, int32& OutC1, int32& OutC2) const;

	// `asda.Soul.Show` — ТОЛЬКО показать окно (зарядить по монете, если пусто). Ни авто-привязку,
	//   ни позицию НЕ трогает: диагностика обязана менять ровно то, что заявлено в её имени.
	void ShowWindowForTest();
	// `asda.Soul.MoveTo x y` — ручное перемещение окна (честно снимает авто-якорь, как и перетаскивание).
	void MoveWindowTo(const FVector2D& DesignPos);
	// Вернуть окно на «авто-якорь» и пересчитать позицию (после правки отступов из консоли).
	void ReanchorNow();

	// Многострочные дампы (консоль).
	FString DumpState() const;      // asda.Soul.Dump
	FString DumpControls() const;   // asda.Soul.Controls — КАЖДЫЙ из 95 узлов
	FString DumpTables() const;     // asda.Soul.Tables
	FString DumpNet() const;        // asda.Soul.Net — сводка сетевого тракта хранителя

	// ★ ПРОВЕРКА БЕЗ СЕРВЕРА: показать надпись «Душа Взрыв N» / «Душа Хранит N» на себе или на соседе.
	//   bGuard=true → «Хранит» (тройка 56204/56214/56224+), false → «Взрыв» (56201/56211/56221+).
	//   Level 1..3 = цвет+цифра. PeerKey: 0 = свой игрок, иначе userIdx/charId соседа.
	//   Возвращает описание того, что реально показано (для эха в консоль).
	FString DebugShowBanner(bool bGuard, int32 Level, int32 PeerKey);
	// Сводка по надписям: что сейчас висит на себе и над кем висят чужие.
	FString DumpBanners() const;

	// ── Счётчики тракта (диагностика; на поведение не влияют) ──
	int32 NetCnt6156 = 0, NetCnt6157 = 0, NetCnt6159 = 0, NetCnt6160 = 0, NetCnt6161 = 0, NetSent6158 = 0;
	int32 LastCastResult = -1, LastCastSkill = -1, LastCastUser = -1, LastCoinAddFlag = -1;

	// ── ЛОКАЛЬНЫЙ ЭМУЛЯТОР СЕРВЕРА (только для проверки без сервера; в бою НЕ участвует) ────────
	// ★ Это НЕ поведение оригинального клиента: клиент монеты сам НИКОГДА не считает, он лишь
	//   рисует присланное. Эмулятор повторяет формулы GameServer построчно, чтобы весь цикл
	//   (начисление → распад → пороги → уровень → бафф → списание) можно было прогнать локально.
	//   Включается командой `asda.Soul.SimServer 1`; при живом сервере держать ВЫКЛЮЧЕННЫМ.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Эмулятор") bool bSimulateServer = false;
	// Грейд игрока = `CUser+435` (iJobCount): сколько рядов монет разблокировано (0..3 значимы).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Эмулятор") int32 SimJobCount = 3;
	// Тип хранителя (элемент) для эмуляции; сервер берёт его из боевой SkillTable по первому скиллу.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Эмулятор") int32 SimSkillType = 1;
	// Эмуляция «игрок применил боевой скилл id» → начисление по SkillCoinTable (сервер sub_48AFF0).
	void SimSkillUsed(int32 SkillId);
	// Эмуляция тика распада (сервер sub_48B4C0 + sub_48B570(row=3, amount=1)).
	void SimDecayTick();
	// Эмуляция списания при использовании скилла хранителя (сервер sub_48B570(row, 5)).
	void SimSpend(int32 Row, int32 Amount);
	// Эмуляция смерти (сервер sub_48DD60: обнуление + 6157 AddFlag=0 + 6156 index=−1).
	void SimDeath();
	// Эмуляция `sub_48B210` (GameServer.c:55687): пересчёт уровня хранителя по порогу 8, выбор
	//   записи SoulGuarderTable по (уровень, SkillType), наложение/смена/снятие ПАССИВНОГО баффа
	//   (`sub_4920C0`, id 1201..1227) и рассылка 6156. Зовётся из эмулятора после каждого
	//   изменения монет — ровно там же, где это делает сервер.
	void SimRecomputeGuard();

	// ══════════════════════════ Параметры (все правятся в редакторе) ═══════════════════════════

	// Порог «скилл ряда доступен». Эталон: клиент `rowSlotCounts[k] >= 5` (ui_core.c:92786),
	//   сервер `sub_48B780` → `iSoulCoin[row] >= 5`. Совпадают.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Пороги") int32 AvailableThreshold = 5;
	// Порог «уровень хранителя». Эталон: сервер `sub_48B210` → `coin[i] >= 8`; клиент по этому же
	//   числу показывает SOULBAR_ANI (`rowSlotCounts[i] < 8` → скрыть, ui_core.c:92800).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Пороги") int32 GuardLevelThreshold = 8;
	// Кап монет в ряду (сервер GameServer.c:55649; клиент рисует не более 10 шаров).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Пороги") int32 CoinCap = 10;
	// Период распада, мс (сервер: `CUser+259169 = GetTickCount() + 15000`).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Пороги") int32 DecayPeriodMs = 15000;
	// Сколько монет списывает применение скилла хранителя (сервер: жёсткая 5).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Пороги") int32 SkillCoinCost = 5;
	// Диапазон боевых скиллов, дающих монеты (сервер `a3 >= 500 && a3 < 1200`).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Пороги") int32 CoinSkillIdMin = 500;
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Пороги") int32 CoinSkillIdMax = 1200;

	// Антиспам применения скилла, мс — эталон `timeGetTime() - g_dwQuickSlotUseLastTick > 0xFA`.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Гейты") int32 UseCooldownMs = 250;
	// Второй антиспам (проверка карты) — эталон `> 0xC8`.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Гейты") int32 MapCheckCooldownMs = 200;
	// Типы карт, на которых скилл хранителя ЗАПРЕЩЁН (эталон: 13, 14, 15, 24, 25, 27).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Гейты") TArray<int32> BannedMapTypes;
	// Эффект-блокировщик: при наличии скиллового эффекта с этим id скилл хранителя запрещён
	//   (эталон `CList_FindByTypeSub(char+2552, 547, 0)`).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Гейты") int32 BlockingBuffId = 547;
	// loc-строка отказа «ряд ещё не заряжен» (эталон 112503, белым, 10 000 мс).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Гейты") int32 NotAvailableLocId = 112503;
	// ★ Значение эталонного `Net_GetCurrentMapType(LocalPlayer)` — ТЕЛА ЭТОЙ ФУНКЦИИ В СРЕЗЕ НЕТ,
	//   имя пришло из прежнего разбора и может быть мисномером (соседний гейт читает
	//   `effectSlots[112].field_8`, у которого эталон сам подписал «= СОСТОЯНИЕ персонажа,
	//   1 = движение», entity_char.c:62207, и сравнивает его в т. ч. с 13/24/27 — тем же набором).
	//   В ПОРТУ это поле НЕ ЗАПОЛНЯЕТСЯ ничем (ни в структуре персонажа, ни в DT_MapTable его нет).
	//   Гейт реализован и включится, как только источник появится; −1 = «неизвестно» ⇒ ничего не
	//   запрещается. Для проверки задаётся вручную: `asda.Soul.MapType 13`.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Гейты") int32 CurrentMapType = -1;
	// ★ Эталонное `LocalPlayer->effectSlots[112].field_8` — ЧИСЛОВОЕ СОСТОЯНИЕ персонажа
	//   (`hudSkillPopup_RefreshOrHide`: `== 6` → обнулить счётчики и спрятать окно). В порту
	//   числового состояния нет (есть EAsdaPlayerState из 5 значений), поэтому поле тоже не
	//   заполняется; −1 = «неизвестно». Проверка: `asda.Soul.State 6`.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Гейты") int32 SelfActionState = -1;
	// ★★★ РУЧНОЕ ПЕРЕОПРЕДЕЛЕНИЕ индекса цели в теле 6158. −1 = «не переопределять»: индекс берётся
	//   из ВЫБРАННОЙ ЦЕЛИ, как в эталоне (`Target_GetSelectedUserIdx(localPlayer)`,
	//   `skillCastSelfSendPacket@0x9FE210`, skill_buff.c:2222-2223). Задаётся `asda.Soul.Target <idx>`.
	//   ⚠ ИСПРАВЛЕНО 2026-08-10. Прежний комментарий утверждал «сервер это поле у скиллов хранителя
	//     не читает, поэтому шлём −1» — ЭТО ЛОЖЬ, и именно она ломала всю систему:
	//     `CSoulGuarderManager::RecvChar_SoulGuarderSkillUse@0x48E4F0` (GameServer.c:57139-57230)
	//     делает `switch (SoulGuarderSkillTable[skill − 1201] + 46)` и при индексе цели == −1
	//     отвечает кодом **20** («Выбери цель») в вариантах 1/2/5/6/7 — то есть ВСЕГДА, кроме
	//     default. Живой лог порта 2026-08-10 06:00:37 показывает это дословно:
	//       `→6158 … цель(тип=1, idx=-1)` → `6159 result=20` → нотис «Выбери цель.»
	//     Отказ приходит ДО списания монет (`sub_48B570(uid, ряд, 5)` зовётся только из
	//     `RecvBattle_SoulGuarderSkillUse@0x48F1B0`, GameServer.c:57268+, то есть уже ПОСЛЕ успеха),
	//     поэтому монеты копились и не тратились, а урона и импакта не было вовсе.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Гейты") int32 OverrideTargetUserIdx = -1;

	// Суффикс «серой» иконки недоступного ряда. Эталон: литерал `unk_14347E4` = "_g"
	//   (.rdata 0x14347E4, байты 5F 67 00); в Content/AsdaAssets/Textures/JUI лежат ровно парные
	//   `skl_sg_*` и `skl_sg_*_g` на все 54 скилла — проверено.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Иконки") FString DisabledIconSuffix = TEXT("_g");
	// Каталог атласов иконок скиллов (те же 32×32, что у окна скиллов).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Иконки") FString IconPackagePath = AsdaAssetPaths::kJuiRoot;   // ★ волна 2 п. 7: умолчание — корень JUI из реестра путей (значение то же)

	// Альфа «затемнённых» элементов окна (эталон `CControl_SetColor4(ctrl, 0x40, 0)`).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float DimAlpha = 64.f / 255.f;
	// Альфа подписи ряда GTEXT (эталон 0x80).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float GTextAlpha = 128.f / 255.f;
	// Скорость альфы плашки SOUL BURST/GUARDER: ±15 за кадр при масштабе времени 1 (эталон
	//   `g_flFadeAlpha ± 15.0 * g_FrameTimeScale`); разворот на 500, показ клампится к 255.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float BannerFadeStep = 15.f;
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float BannerFadePeak = 500.f;
	// ★★★ ЭТАЛОННЫЙ МАСШТАБ ВРЕМЕНИ. `g_FrameTimeScale` НЕ «доля кадра при 60 FPS», а
	//   `30.0 / FPS` (game_main.c:1488-1494: раз в секунду `g_FrameTimeScale = 30.0 / кадров_за_секунду`).
	//   ⇒ шаг «X за кадр» = `X * 30 * dt` в секунду, то есть база 30 кадров/с, а НЕ 60.
	//   БЫЛО `dt * 60` — вся кадровая арифметика хранителя (фейд плашки, разгон летящих монет)
	//   шла РОВНО ВДВОЕ БЫСТРЕЕ эталона: подъём альфы 0→500 занимал 0.56 с вместо 1.11 с,
	//   надпись «моргала» и юзер не успевал её прочитать.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float EtalonFrameFps = 30.f;

	// ── ★ НАДПИСИ НАД ЧУЖИМИ ИГРОКАМИ — ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (осознанное) ────────────────────
	// В ОРИГИНАЛЕ этих надписей над соседями НЕТ и БЫТЬ НЕ МОЖЕТ: контролов ровно ОДИН комплект
	//   (56201..56206 + 56211..56216 + 56221..56226), а `uiZoneFadeAndNameplate_update` читает
	//   ТОЛЬКО `CharManager_GetLocalPlayer` (проверено: других ссылок на слоты [108..126] в срезе
	//   нет — только `soulCraftWnd_init`, который их находит и гасит). Данные для соседей у клиента
	//   ЕСТЬ (6156 приходит и про них), поэтому надпись «Душа Хранит N» над чужим мы РИСУЕМ КОПИЯМИ
	//   тех же самых JUI-планов. Откат в один шаг: `asda.Soul.BannerOthers 0`.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") bool bPeerBanners = true;
	// «Душа Взрыв N» над СОСЕДОМ в момент каста скилла хранителя (6159 рассылается по сектору).
	//   Тоже отступление: у соседа нет счётчиков монет, поэтому эталонного повода («ряд достиг 5»)
	//   у клиента для чужого игрока не существует в принципе.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") bool bPeerBurstOnCast = true;
	// ★ «Душа Взрыв N» на СВОЁМ игроке в момент каста. ЭТАЛОН ЭТОГО НЕ ДЕЛАЕТ (у него «Взрыв»
	//   показывается по достижению 5 монет в ряду), поэтому по умолчанию ВЫКЛЮЧЕНО.
	//   Включается `asda.Soul.BurstOnCast 1`.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") bool bBurstOnCast = false;
	// Высота плашки над игроком: +30, затем +250 (или +400 верхом) — эталон ui_core.c:92455-92464.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float BannerBaseZ = 30.f;
	// ★ УТОЧНЕНО РЕВЕРСОМ КОМПАНЬОНА (soul_guardian_companion_re.md §10, ui_core.c:92455-92464):
	//   `Y += 30; Y += (актёр-ХРАНИТЕЛЬ существует ? 400 : 250); Y += heightOffset ТРАНСПОРТА`.
	//   То есть 400 против 250 выбирает НАЛИЧИЕ 3D-КОМПАНЬОНА (плашке надо подняться над ним),
	//   а высота транспорта — ОТДЕЛЬНОЕ слагаемое. Раньше 400 ставилось по признаку «в седле» —
	//   источника у порта тогда не было, теперь есть (UAsdaWorldSubsystem::HasSoulGuardian).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float BannerFootZ = 250.f;
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float BannerGuardianZ = 400.f;
	// Отступ «приколотого» окна от правого края и от мини-карты (эталон
	//   `X = screenW − minimapW − 145`, `Y = 20`).
	// ════════════ ПОЗИЦИЯ ОКНА ПО УМОЛЧАНИЮ («авто-якорь», пока не трогали чекбокс) ═════════════
	// ЭТАЛОН (`cameraTarget_UpdateAbovePlayer@0xEF6790`, render_core.c:5836-5843) — дословно:
	//     screenWidth = CConfig_GetWidth(&g_hMainWnd);              // ширина экрана
	//     sub_932299(g_pMiniMapWnd, &miniMapWidth);                 // → GetSize (vtable+24) окна
	//                                                               //   мини-карты, пишет {w,h}
	//     this[129] = screenWidth − miniMapWidth − 145;             // X контрола 56000
	//     this[130] = 20;                                           // Y контрола 56000
	//   Позиция ставится `CControl_SetField74_75(this[0], X, Y)` = `posX/posY` (+0x128/+0x12C).
	// ★ ПО ГОРИЗОНТАЛИ воспроизводим 1:1: правый край экрана минус ширина мини-карты минус 145.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Позиция") float PinRightMargin = 145.f;
	// ПО ВЕРТИКАЛИ — ЭТАЛОН, СВЕРЕННЫЙ ДО МАШИННОГО КОДА (AsdaStory.exe сборки IDB, 13 344 768 б,
	//   база 0x400000; дизассемблер по файловому смещению 0x5D9790). Дословно, без Hex-Rays:
	//     00EF68BA  movzx ecx, byte [eax+0x20C]   ; this[524] — «чекбокс ещё не трогали»
	//     00EF68C3  jne  0xEF6900                 ; != 0 → блок авто-якоря
	//     00EF6900  mov  ecx, 0x15CB460
	//     00EF6905  call CConfig_GetWidth         ; ширина экрана
	//     00EF690A  mov  [ebp-0x50], eax          ; screenWidth
	//     00EF690D  lea  eax, [ebp-0x58]          ; ← приёмник GetSize (w в -0x58, h в -0x54)
	//     00EF6911  mov  ecx, 0x2C07138           ; g_pMiniMapWnd
	//     00EF6916  call GetSize
	//     00EF691B  mov  eax, [ebp-0x50]          ; screenWidth (НЕ затёрт: GetSize писал в -0x58/-0x54)
	//     00EF691E  sub  eax, [ebp-0x58]          ; − ширина мини-карты
	//     00EF6921  sub  eax, 0x91                ; − 145
	//     00EF6929  mov  [ecx+0x204], eax         ; this[129] = X
	//     00EF6932  mov  dword [eax+0x208], 0x14  ; this[130] = 20   ← ЛИТЕРАЛ, ВЕРХ ЭКРАНА
	//   ⇒ Эталонная стартовая позиция = ВЕРХ, левее мини-карты (Y = 20 локально, экранно 26).
	//   ЭТАЛОН — ПОВЕДЕНИЕ ПО УМОЛЧАНИЮ (требование юзера «в эталоне всё есть»). Прежний режим
	//   «низ экрана» (наше отступление из-за тогдашнего наблюдения) оставлен за флагом
	//   `bEtalonTopAnchor = false` + `PinBottomMargin`.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Позиция") bool  bEtalonTopAnchor = true;
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Позиция") float PinTop = 20.f;
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Позиция") float PinBottomMargin = 145.f;
	// ★ Фолбэк ширины окна мини-карты, если подсистема мини-карты недоступна: .JUI MINIMAP_WIN
	//   (IDNum 1702) `Size = "133 134"` — компактный режим. Штатно ширину отдаёт
	//   UAsdaMinimapSubsystem::GetWindowWidthPx() = порт эталонного GetSize(g_pMiniMapWnd).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Позиция") float MiniMapFallbackWidth = 133.f;
	// ★ 56000 — КОНТРОЛ ВНУТРИ `MINISTATE_WIN`, а координаты контролов в оригинале ЛОКАЛЬНЫ
	//   (доказано `uiSlotState_UpdateArrowIndicators`: чтобы попасть стрелкой глубины 2 в шар
	//   глубины 4, эталон СКЛАДЫВАЕТ posX/posY по цепочке предков — при абсолютных координатах
	//   складывать было бы нечего). Значит `Y = 20` из машинного кода — это локальное смещение
	//   относительно `MINISTATE_WIN`, а само окно мини-статуса стоит в абсолюте (0, 6).
	//   ⇒ Экранная позиция = ЭТО + локальная. Импортёр JUI уже разложил всё в абсолют, поэтому
	//   при переводе эталонной формулы в наши координаты добавляем начало родителя.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Позиция") FVector2D ParentOrigin = FVector2D(0.f, 6.f);
	// ★ СТРАХОВКА, а не эталон: минимальный зазор от краёв экрана при клампе. Эталонная формула
	//   задаёт X ЛЕВОГО края контрола 56000, а содержимое (178 px) уходит вправо — при ЗАКРЫТОЙ
	//   мини-карте окно упиралось бы в правый край. При открытой мини-карте (штатный HUD) кламп
	//   не срабатывает вовсе и позиция остаётся дословно эталонной.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Позиция") float PinEdgeGuard = 20.f;
	// Точка, к которой летят монеты: игрок + (−right)·310 (эталон render_core.c:5827).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float CoinTargetSideOffset = 310.f;
	// ★ Подъём точки прилёта монеты над игроком при СНЯТОМ авто-якоре — литерал `+ 30.0` эталона
	//   (`cameraTarget_UpdateAbovePlayer@0xEF6790`, `render_core.c:5851`), игровая ось Y = UE +Z.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") float CoinTargetUpOffset = 30.f;
	// ★ Крайний срок жизни «вспышки шара» (56331..56333), мс. Штатно её гасит событие 24 стрелки
	//   56208; это дубль на случай, если у плана стрелки нет данных скина и событие не придёт.
	//   Длительность прогона стрелки в скине — 700 мс, берём с запасом.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Вид") int32 FlashFallbackHideMs = 1500;

	// ★ ОТСТУПЛЕНИЕ (закон fix-original-defects), по умолчанию ВЫКЛЮЧЕНО = ведём себя как эталон.
	//   В эталоне при `AddFlag == 1` счётчики применяются только по прилёте монеты; если сцена
	//   без игрока (нет локального персонажа) монета не создаётся ВООБЩЕ и пакет теряется.
	//   true = применять счётчики сразу, когда монету создать не из чего.
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Отступления") bool bApplyCoinsWithoutFlight = true;
	// ★ Эталон обновляет флаги доступности рядов ТОЛЬКО внутри ветки «у ряда есть skillCode и запись
	//   тултипа найдена» (ui_core.c:92786 лежит под `if (RecordByIndex)`). Пока сервер не прислал
	//   SkillType, все три флага остаются нулями. true = считать флаги всегда (наше исправление).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Отступления") bool bAlwaysRecomputeAvailability = true;

	// Пути к DataTable таблиц (правятся в редакторе).
	UPROPERTY(EditAnywhere, Category="Asda|SoulGuard|Таблицы")
	FString GuarderTablePath = TEXT("/Game/AsdaAssets/Data/DT_SoulGuarder.DT_SoulGuarder");
	// ★ Аудит №3 (A3): путь к `DT_SkillCoin` здесь БОЛЬШЕ НЕ ХРАНИТСЯ — таблицу грузит и держит
	//   `UAsdaItemTableSubsystem` (единственный держатель, запись `GExtraTables` в его .cpp).
	// ★★★ 2026-08-26: поля `GuarderBinFile`/`SkillCoinBinFile` УДАЛЕНЫ вместе с .bin-фолбэком.
	//   Сырые `SoulGuarderTable.BIN` и `SkillCoinTable.bin` уехали из `Content/` в `RawAssets/Data/` —
	//   там место исходников импортёров. Рантайм берёт только DataTable.

	// ══════════════════════════ Состояние (видно в редакторе) ══════════════════════════════════
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") TArray<int32> Coin;          // [84..86]
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") TArray<int32> PrevCoin;      // [87..89]
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") TArray<int32> RowSkillCode;  // [103+2i]
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") TArray<bool>  bRowAvailable; // [102+2i]
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") int32 SkillType = 0;         // тип хранителя
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") int32 SoulGuardIndex = -1;   // из 6156
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") bool  bWindowShown = false;  // [308]
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") bool  bPinned = true;        // чекбокс 56264
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Состояние") TArray<FAsdaSoulFlyingCoin> FlyingCoins;

	// Загруженные таблицы (только чтение).
	UPROPERTY(VisibleAnywhere, Category="Asda|SoulGuard|Таблицы") TArray<FAsdaSoulGuarderRow> GuarderTable;

	// Счётчики монет изменились → внешние потребители (например, эффекты) могут отреагировать.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAsdaSoulCoins, int32, int32, int32);
	FOnAsdaSoulCoins OnCoinsChanged;
	// Сменился призванный хранитель (SoulGuardIndex; −1 = снят).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSoulGuardIndex, int32);
	FOnAsdaSoulGuardIndex OnGuardIndexChanged;

private:
	// ── эталонные функции ──
	void RefreshIcons();                                          // hudQuickSlot_RefreshIcons
	void UpdateArrowIndicators(int32 C0, int32 C1, int32 C2);     // uiSlotState_UpdateArrowIndicators
	void RefreshOrHide();                                         // hudSkillPopup_RefreshOrHide
	void UpdateBanner(float DeltaTime);                           // uiZoneFadeAndNameplate_update
	void TickFlyingCoins(float DeltaTime);                        // coinDropMgr_UpdateFlyToPlayer
	// ★ Откуда вылетает монета: цель игрока (моб → игрок → nullptr), эталон CharFx_CreateSkillCastEffect.
	const AActor* ResolveCoinSourceTarget(bool& bOutIsMob) const;
	void SpawnFlyingCoin(int32 C0, int32 C1, int32 C2, int32 SkillId);  // CharFx_CreateSkillCastEffect
	void UpdatePinnedPosition();                                  // cameraTarget_UpdateAbovePlayer
	// `uiDialogMsgProc@0xEF40C0` (ui_core.c:92512) — события мыши окна 56000:
	//   8/9/10 = наведение/держим/уход на MARK_1/2/3 → тултип скилла у курсора;
	//   0xD = ЛКМ по MARK_1/2/3 → применить скилл ряда (свой антиспам 250 мс);
	//   67073/67074 = чекбокс 56264 взведён/снят → запомнить позицию окна и снять авто-привязку.
	void TickPointer();
	// Событие 24 «анимация закончилась» для одноразовых планов (56207/56208/56251..56253).
	UFUNCTION() void OnArrowUpFinished();
	UFUNCTION() void OnArrowDownFinished();
	UFUNCTION() void OnMarkAni1Finished();
	UFUNCTION() void OnMarkAni2Finished();
	UFUNCTION() void OnMarkAni3Finished();
	void OnMarkAniFinished(int32 Row);

	// ── инфраструктура ──
	bool EnsureControls();
	void ResetControls();
	void LoadTables();
	// Иконка ряда: имя базы из skilltooltip.bin (+36) через DT_skilltooltip.
	FString GetSkillIconName(int32 SkillCode) const;
	// Подпись хоткея действия Cmd (эталон CKeybind_FormatHotkeyString): «Shift+/Ctrl+/Alt+» + клавиша.
	FString FormatHotkeyLabel(int32 Cmd) const;
	// Поставить в MARK-контрол текстуру 32×32 по имени (без расширения).
	bool SetRowIcon(int32 Row, const FString& TexName);
	// Локальные (в канвасе) позиция/размер узла и сдвиг группы.
	static bool GetSlotRect(const UWidget* W, FVector2D& OutPos, FVector2D& OutSize);
	// Позиция вложенного узла в координатах РОДИТЕЛЯ его группы (оффсеты в UMG-импорте ЛОКАЛЬНЫ).
	static FVector2D GroupSpacePos(const UWidget* Group, const UWidget* Inner);
	static void ShiftGroup(UWidget* Group, const FVector2D& Delta);
	static void SetVis(UWidget* W, bool bVisible);
	// Группа-контейнер: видимая = SelfHitTestInvisible (иначе прозрачный канвас съест клики по миру).
	static void SetGroupVis(UWidget* W, bool bVisible);
	static void SetAlpha(UWidget* W, float A);
	// Эталонная пара «Show + SetVisible(1)»: CControl_Show ПЕРЕЗАПУСКАЕТ кадровую анимацию плана
	//   (сброс таймеров + перезарядка счётчика кадров, 0x12E1350), поэтому у нас — RestartAnim().
	static void ShowAndRestart(UWidget* W);
	// Эталонный CControl_CloseHide: остановить анимацию, сбросить в кадр 0 и скрыть.
	static void CloseHide(UWidget* W);
	void PostNotAvailableNotice() const;
	int32 SelfUserIdx() const;
	// Локальный игрок (для позиции плашки и точки прилёта монет).
	class AAsdaPlayerCharacter* GetLocalChar() const;
	// «Мы мертвы» из живого источника порта — эталонное состояние действия 6 (смерть).
	bool IsSelfDeadNow() const;
	// Узел реально дойдёт до экрана (он и вся цепочка родителей не Collapsed/Hidden).
	static bool IsEffectivelyVisible(const UWidget* W);

	// ── виджеты (все — узлы WBP_MINISTATE_WIN) ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> MiniWidget;
	int32 WidgetGen = -1;                                  // поколение SetupControls мини-статуса
	bool  bControlsReady = false;

	UPROPERTY(Transient) TObjectPtr<UWidget> RootGroup;    // Soul_Boost_Win_grp   ([0] поддерево)
	UPROPERTY(Transient) TObjectPtr<UWidget> RootHit;      // Soul_Boost_Win_161_HIT (позиция 56000)
	UPROPERTY(Transient) TObjectPtr<UImage>  BarLeft;      // 56346 [71]
	UPROPERTY(Transient) TObjectPtr<UImage>  BarRight;     // 56345 [70]
	UPROPERTY(Transient) TObjectPtr<UWidget> BarLeftDrag;  // SOULBar_left_162_DRAG  (ручка окна)
	UPROPERTY(Transient) TObjectPtr<UWidget> BarRightDrag; // SOULBar_right_163_DRAG
	UPROPERTY(Transient) TObjectPtr<UCheckBox> PinCheck;   // 56264 [128]

	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RowBox;         // [23i+1]
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowMark;        // [23i+2]
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowGText;       // [23i+3]
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowBolt;        // [23i+4] (только i = 0,1)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowMain;        // [23i+5]
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowBallBack;    // [23i+6]
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowBackg;       // [23i+7]
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowBlackCircle; // [23i+18]
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RowDn;          // [23i+19]
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowMarkAni;     // [23i+20]
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowBallAni;     // [23i+21]
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RowHotText;  // [23i+22]
	// 3 × 10 шаров: RowBall[3*i + j], j = 0 → D_10 (левый), j = 9 → D_1 (правый).
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  RowBall;
	UPROPERTY(Transient) TArray<bool> bMarkAniPending;               // [23i+23]

	UPROPERTY(Transient) TObjectPtr<UImage> FlashRed;      // 56331 [72]
	UPROPERTY(Transient) TObjectPtr<UImage> FlashBlue;     // 56332 [74]
	UPROPERTY(Transient) TObjectPtr<UImage> FlashGreen;    // 56333 [73]
	UPROPERTY(Transient) TObjectPtr<UImage> ArrowUp;       // 56208 [75]
	UPROPERTY(Transient) TObjectPtr<UImage> ArrowDown;     // 56207 [76]

	UPROPERTY(Transient) TObjectPtr<UWidget> TextsGroup;   // Soul_Boost_Texts_grp   ([108] поддерево)
	UPROPERTY(Transient) TObjectPtr<UWidget> TextsHit;     // Soul_Boost_Texts_235_HIT (позиция 56200)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> BurstPanel;  // [109..111] Texts_1..3 (план)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> BurstLabel;  // [112..114] Text_burst_g/b/r (план)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> BurstDigit;  // [115..117] SOUL_1/2/3 (план)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> GuardPanel;  // [118..120] Texts_4..6 (план)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> GuardLabel;  // [121..123] Text_gaurder_g/b/r (план)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> GuardDigit;  // [124..126] SOUL_1/2/3 (план)
	// ★ Группы-контейнеры тех же узлов: в UMG-импорте узел с детьми = вложенный CanvasPanel,
	//   и показывать/прятать надо ИМЕННО ГРУППУ (иначе поддерево остаётся скрытым).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BurstGroup;       // Soul_Boost_Texts_{1..3}_grp
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BurstLabelGroup;  // Text_burst_{g,b,r}_grp
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GuardGroup;       // Soul_Boost_Texts_{4..6}_grp
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GuardLabelGroup;  // Text_gaurder_{g,b,r}_grp

	// Показать/скрыть тройку плашки (панель + подпись + цифра) вместе с их группами.
	void ShowBannerTriple(int32 Row, bool bGuard, bool bShow);
	// Альфа тройки (эталонный фейд g_flFadeAlpha на трёх контролах).
	void SetBannerTripleAlpha(int32 Row, bool bGuard, float A);

	// ── состояние плашки (эталонные глобали g_pFadeWindow / g_pDialogCtrlA / g_pDialogCtrlB) ──
	int32 BannerRow = -1;             // какая из троек сейчас на экране (−1 = никакая)
	bool  bBannerIsGuard = false;     // тройка GUARDER (иначе BURST)
	float BannerAlpha = 0.f;          // g_flFadeAlpha
	bool  bBannerFadeOut = false;     // g_bScreenFadeOut
	int32 ActiveBurstSlot = -1;       // g_nActiveZoneFadeSlot
	TArray<int32> BurstCache;         // g_aZoneFadeSlotCache[3]
	int32 LastBannerGuardIndex = -2;  // dword_15B5D40

	// ── ★ НАДПИСИ НАД ЧУЖИМИ (отступление, см. bPeerBanners) ─────────────────────────────────────
	// Один экземпляр = три УМG-копии тех же JUI-планов, добавленные в корневой канвас MINISTATE_WIN.
	//   Живут ровно столько, сколько длится эталонный фейд (0→500→0), затем удаляются.
	struct FPeerBanner
	{
		TWeakObjectPtr<AActor> Owner;      // над кем висит
		int32 CharId  = 0;                 // ключ 6156
		int32 UserIdx = -1;                // ключ 6159
		int32 Row     = 0;                 // 0..2 — цвет/цифра
		bool  bGuard  = true;              // «Хранит» (иначе «Взрыв»)
		float Alpha   = 0.f;               // тот же g_flFadeAlpha
		bool  bFadeOut = false;
		TWeakObjectPtr<UImage> Panel, Label, Digit;
	};
	TArray<FPeerBanner> PeerBanners;
	// Смещения подписи и цифры ОТНОСИТЕЛЬНО панели «Душа» — снимаются с самих JUI-узлов один раз
	//   (не константы: источник — импортированная разметка, [0]=подпись, [1]=цифра).
	FVector2D BurstRelOff[3][2] = {};
	FVector2D GuardRelOff[3][2] = {};
	FVector2D BurstRelSize[3][3] = {};   // [ряд][панель/подпись/цифра]
	FVector2D GuardRelSize[3][3] = {};
	bool bRelOffsetsCached = false;

	// Снять относительную раскладку троек с импортированных узлов (один раз на виджет).
	void CacheBannerLayout();
	// Показать надпись над ЧУЖИМ игроком (или над собой при ForceOwner) — создаёт копии планов.
	void ShowPeerBanner(AActor* Owner, int32 CharId, int32 UserIdx, int32 Row, bool bGuard);
	// Пер-кадровое ведение чужих надписей: фейд + проекция позиции + уборка мёртвых.
	void TickPeerBanners(float DeltaTime);
	// Убрать все чужие надписи (смена виджета/выход из мира).
	void ClearPeerBanners();
	// Ряд (0..2) по коду АКТИВНОГО скилла хранителя 1228..1254 (SoulGuarderTable.GuardianSkillId).
	bool RowByGuardianSkill(int32 SkillCode, int32& OutRow) const;
	// Эталонный масштаб кадра: `g_FrameTimeScale = 30 / FPS` ⇒ dt * 30 (game_main.c:1488-1494).
	float EtalonFrameScale(float DeltaTime) const;

	// ★ Аудит №3 (A3): строка `SkillCoinTable` по id навыка — только у менеджера таблиц
	//   (он её единственный держатель). .bin-фолбэка больше нет.
	bool FindSkillCoin(int32 SkillId, FAsdaSkillCoinRow& OutRow) const;
	// Сколько строк таблицы монет у менеджера. Нужен гейтам и диагностике.
	int32 SkillCoinCount() const;

	// ── прочее ──
	// Число строк таблицы монет у менеджера на момент `LoadTables` (нулевое = таблицы нет).
	int32 MgrSkillCoinRows = 0;
	TMap<int32, int32> SoulGuardIndexByUser;               // [95] — карта из 6156
	FVector CoinTargetWorld = FVector::ZeroVector;         // [81..83]
	double LastUseTickS = 0.0;                             // g_dwQuickSlotUseLastTick
	double LastMapCheckTickS = 0.0;                        // g_dwQuickSlotMapCheckLastTick
	double LastClickTickS = 0.0;                           // g_dwDialogBtnInputLastTick (250 мс)
	int32  HoverRow = -1;                                  // над каким MARK сейчас курсор (−1 = ни над каким)
	// ★★★ Аудит №3 (B3): прежний латч `bPrevMouseDown` — общая точка. Именно у ЭТОГО потребителя
	//   латч приходится сбрасывать РУКАМИ при закрытом окне (`Mouse.Reset()` в `TickPointer`) —
	//   без сброса удержанная с прошлого кадра ЛКМ не давала бы фронта после открытия окна и первый
	//   клик по ряду терялся. Поэтому у структуры есть `Reset()`, а не только `Update()`.
	FAsdaMouseFrame Mouse;                                 // фронт нажатия ЛКМ (мышь ловим опросом)
	bool   bPrevPinChecked = true;                         // прошлое состояние чекбокса (события 67073/67074)
	// ★ `this[524]` эталона: 1 сразу после `soulCraftWnd_init`, 0 — после ЛЮБОГО переключения
	//   чекбокса. Пока 1, окно каждый кадр авто-привязывается к (ширина − мини-карта − 145, 20)
	//   и точка прилёта монет = игрок + (−right)·310. После первого клика по чекбоксу позиция
	//   запоминается в PinnedPos, авто-привязка выключается, а монеты летят прямо в игрока.
	bool   bAutoAnchor = true;
	bool   bLoggedPin = false;          // разовый лог вычисленной «приколотой» позиции
	bool   bWarnedNoMapType = false;    // разовый лог «гейт типа карты пропущен: нет источника»
	FVector2D PinnedPos = FVector2D::ZeroVector;           // [129]/[130]
	// ★ Позиция восстановлена из настроек, но захватить её надо ЛЕНИВО (первым тиком показа):
	//   в момент EnsureControls общий механизм раскладки мог ещё не применить сдвиг из ini.
	bool   bPinnedPosStale = false;
	// ★ ДИЗАЙН-позиция контрола 56000 (координаты .JUI в корневом канвасе WBP): текущая позиция
	//   на момент разбора дерева МИНУС уже применённый общим механизмом сдвиг. База для
	//   SyncSavedWindowOffset — см. AsdaWindowSubsystem.h.
	FVector2D DesignPos56000 = FVector2D::ZeroVector;
	double NextSimDecayS = 0.0;                            // эмулятор: дедлайн распада
	// Крайний срок принудительного гашения «вспышек шара» (0 = нечего гасить). См. Tick.
	double FlashHideAtS = 0.0;
	bool bTablesLoaded = false;
};
