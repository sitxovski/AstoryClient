// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// UAsdaSoulmateSkillSubsystem — НАВЫКИ ДРУЖБЫ (5231/5232/5233/2247) и ЗОВ ДРУГА (5236/5237/5427).
//
// Источник разбора: `Reports/soulmate_skills_recall_roulette.md` (§A, §B) + прямой реверс
//   `RE/etalon/*.c` и `RE/ida/server_work/GameServer.c`, выполненный при портировании (см. ниже
//   раздел «ПОПРАВКИ К ОТЧЁТУ» — часть утверждений отчёта опровергнута кодом и данными).
//
// ЧТО ЗДЕСЬ ЖИВЁТ:
//   • таблица навыков Дружбы (id 30..44, запись 112 б) — ТОЛЬКО из DataTable, без хардкода;
//   • лоток из 12 ячеек `SOUL_TRAY00..11` на вкладке 0 окна 6400 (клик = применение навыка);
//   • приём 5232 (состояние применения) и 5233 (отложенное применение) — нотисы и кулдауны;
//   • приём 2247 (таблица `SMSkillCoolTime`: до 30 пар «навык / остаток мс»);
//   • ЗОВ ДРУГА целиком: приём 5236 → двухкнопочный вопрос → 5427 (та же карта) либо переход
//     канала (другая карта/канал) → приём ответа 5237.
//
// ЧЕГО ЗДЕСЬ НЕТ (и почему):
//   • отправки пакетов — их держит `UAsdaNetSubsystem` (владелец другой агент). Здесь только
//     ПОТРЕБЛЕНИЕ его API: `SendSoulmateSkillUse5231`, `SendMateRecallAnswer5237`,
//     `SendMateRecallPosition5427` и делегаты `OnSoulSkillState/Apply/Cooldowns`, `OnMateRecall`;
//   • карточки соулмейта, окон поиска/предложения — это `UAsdaSoulmateSubsystem`;
//   • рулетки — это `UAsdaSoulRouletteSubsystem`.
//
// ═══════════════════ ПОПРАВКИ К ОТЧЁТУ (доказаны кодом и данными, 2026-08-31) ═══════════════════
// ★★★ П1. «ЗОВ ДРУГА = НАВЫК 36» — НЕВЕРНО. Зов друга = навык **32**.
//     Доказательство прямое, на сервере: `RecvChar_SoulMateSkillUse` (GameServer.c:64016-64019)
//         if ( a7 == 32 ) { sub_49E960(this, a1); return; }
//     где `a7` — id навыка (тот же индекс, которым берётся запись `dword_5F5E38 + 112*a7 + …`),
//     а `sub_49E960@0x49E960` — сборщик и отправитель 5236 (отчёт §B.1 п.2).
//     Второе доказательство — ДАННЫЕ (`Data\character\Bin.bbz` → `soulmateskilltable.bin`, 45 × 272 б,
//     имя записи по +4): id 32 = «Summon Soulmate» (призвать соулмейта), id 40 = «Move to Soulmate».
//     Отчёт вывел «36» из тройки 30/36/41 в `social.c:8006` — но эта тройка есть просто ГРУППА
//     одного навыка «Love Love Power» (см. П2), у неё лишь другой билдер тултипа.
// ★★★ П2. Поле записи **+88 (i32) = ГРУППА** навыка, а не «неизвестное». Реальные значения:
//     группа 1 = {30, 36, 41} «Love Love Power», 2 = {34, 38, 43} «Combine Souls»,
//     3 = {33, 37, 42} «Heal Soul», −1 = одиночки {31, 32, 35, 39, 40, 44}.
//     На группе построен весь отбор иконок лотка (`skillHotbar_FindBestSkillSlot@0xE691A0`).
// ★  П3. Отчёт «Заполнение лотка не найдено» (недоказанное №8) — НАЙДЕНО:
//     `skillHotbarRefreshSlots@0xE69440` (`skill_buff.c:34766-34840`), алгоритм воспроизведён 1:1
//     в `RefreshTray()`. Клик по ячейке — `uiQuickSlotActivateByMsgId@0xE68940` (нажатие: выбор +
//     рамка) и `uiQuickSlotSkillActivate@0xE68AC0` (отпускание на той же ячейке: применение).
// ★  П4. Отчёт «где лежит таблица навыков Дружбы» — она в архиве `Data\character\Bin.bbz`,
//     запись `SoulmateSkill.ssi`, 45 × 112 б, БЕЗ заголовка, индекс записи == id
//     (`CSkillMgr_LoadSkillData@0xA79F30`, `skill_buff.c:17204-17241`: `count = size / 0x70`).
//     Иконки и имена — соседняя запись `soulmateskilltable.bin` (заголовок 20 б, 45 × 272 б,
//     +36 char[32] имя файла иконки — та же схема, что у `Skilltooltip.BIN`).
//
// ═══════════════════════ ТАБЛИЦА НОТИСОВ «СОБЫТИЕ → loc → КАНАЛ» ═══════════════════════════════
//   Полная — в шапке .cpp (37 строк). Здесь коротко: все отказы 5232 (коды 0..35) и все коды
//   5237 идут ОДНОЙ строкой в канал «подсказка» эталона (`CControl_SetTooltipText(g_pChatWnd,…,
//   −11514, 10000)`), что у порта соответствует категории `EAsdaMsgCategory::SkillRefused`
//   (маршрут решает `DT_MessageRoutes`, цвет −11514 = 0xFFFFD306 передаётся перекрытием);
//   приход зова (5236) — единственный ОКОННЫЙ нотис: двухкнопочный msgbox, loc 8163.
//
// ═══════════════════════════════ НУМПАД (окно ввода количества 1410) ════════════════════════════
//   НЕ УЧАСТВУЕТ. Проверено двумя методами (закон `check-numpad-on-new-system`):
//     1) `Docs/Port/systems/NUMPAD_FLOW.md` §3 — перечень всех 24 мест вызова `uiOpenContextPopupWithHandlers`;
//        ни одного в соулмейт-коде (`social.c` / `0xE6xxxx`);
//     2) `grep uiOpenContextPopupWithHandlers|g_pNumPadDialog|1410` по `social.c` и `network.c` —
//        ноль совпадений.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Engine/DataTable.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Items/AsdaItemDbRows.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaSoulmateSkillSubsystem.generated.h"

// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: включён ли ключ `asda.Soul.TrayFlow` — лоток навыков Дружбы как эталон (разбор у ключа в .cpp).
ASDAREBORN_API bool AsdaSoulTrayFlowOn();

class UUserWidget;
class UWidget;
class UImage;
class UAsdaNetSubsystem;

// ★★★ ТАБЛИЦЫ НАВЫКОВ ДРУЖБЫ БЕРУТСЯ ИЗ ОБЩЕГО КОНВЕЙЕРА ItemDB — СВОИХ СТРУКТУР НЕТ.
//   Один источник правды на файл: `soulmateskill.ssi` → `FAsdaSoulmateskillRow` / `DT_soulmateskill`,
//   `soulmateskilltable.bin` → `FAsdaSoulmateskilltableRow` / `DT_soulmateskilltable`.
//   Раскладка живёт в реестре `@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB\decode_item_db.py`; поля ApplyBranch(+43),
//   CooldownMs(+46), PartnerCooldown(+85), GroupId(+88) ДОБАВЛЕНЫ туда 2026-08-31 по этому реверсу.
//   Соответствие имён: agent.SkillId → Id, Name → DisplayName, IconName → SkillName, Description → Text.



// ───────────────────────────────────────────────────────────────────────────────────────────────
// ЗАПИСЬ ТАБЛИЦЫ НАВЫКОВ ДРУЖБЫ — `SoulmateSkill.ssi`, 112 байт, индекс записи == id.
//   Имя строки DataTable = id («30» … «44»); заполняются ВСЕ 45 записей, лоток берёт 30..44.
//   Комментарий у поля = смещение в записи + место, которым смещение доказано.
// ───────────────────────────────────────────────────────────────────────────────────────────────

// ───────────────────────────────────────────────────────────────────────────────────────────────
// ЗАПИСЬ ТЕКСТОВОЙ ТАБЛИЦЫ — `soulmateskilltable.bin` (заголовок 20 б, 45 × 272 б).
//   Раскладка снята с файла и подтверждена чтением эталона: `skillFindEntryById(g_pSkillWnd, k)`
//   → `entry + 36` = имя файла иконки (skill_buff.c:34821-34824) — та же схема, что `Skilltooltip.BIN`.
// ───────────────────────────────────────────────────────────────────────────────────────────────

// ───────────────────────────────────────────────────────────────────────────────────────────────
// ЗАПРОС ЗОВА ДРУГА — приём 5236, 42 байта (отчёт §B.2). Эталон держит его целиком в глобале
//   `g_aUpgradeResultRecord` (★ МИСНЕЙМ: это `g_MateRecallRequest`, база 0x2906853) и потом
//   ЭХОМ возвращает пять байт в 5237. Поэтому и мы храним ВСЕ поля, а не только нужные нам.
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaMateRecallRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Зов друга") bool    bPending = false;
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   InitCharId  = -1;   // +0  i32
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   InitUserNum = -1;   // +4  i16
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   InitWorld   = 0;    // +6  u8  (sub_40E110)
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   InitMap     = 0;    // +7  u8  (sub_40C960)
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   InitZone    = 0;    // +8  u8  (sub_40E0F0)
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   InitChannel = 0;    // +9  u8  (sub_4A7EB0)
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") FString ServerIp;           // +10 char[16]
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   ServerPort  = 0;    // +26 i16
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   TileX       = 0;    // +28 i16 → 5427 arg1
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   TileZ       = 0;    // +30 i16 → 5427 arg2
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   TargetCharId  = -1; // +32 i32
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   TargetUserNum = -1; // +36 i16
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   TargetB38   = 0;    // +38 u8
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   TargetMap   = 0;    // +39 u8
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   TargetB40   = 0;    // +40 u8
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") int32   TargetChannel = 0;  // +41 u8 → 5237+11

	/** Эталонный `g_bUpgradeResultGradeChanged` (★ МИСНЕЙМ) = «зов ведёт на другую карту/канал»:
	 *  `(rec[7] != rec[39]) || (rec[41] != rec[9])` (network.c:47171). */
	UPROPERTY(BlueprintReadOnly, Category="Зов друга") bool bOtherMapOrChannel = false;
};

// Живой кулдаун одного навыка Дружбы (порт записи вектора `g_pSkillWnd+56`, 40 б, отчёт §A.5).
USTRUCT()
struct FAsdaSoulSkillCooldown
{
	GENERATED_BODY()

	UPROPERTY() int32 SkillId  = -1;
	UPROPERTY() float TotalMs  = 0.f;   // запись +24: полный кулдаун (rec[46])
	double EndSeconds = 0.0;            // момент истечения (эталон держит старт + timeGetTime)
};

UCLASS()
class ASDAREBORN_API UAsdaSoulmateSkillSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ── Пути таблиц (данные — ТОЛЬКО отсюда, закон «магазины/данные из таблиц») ──
	static const TCHAR* kSkillTablePath;   // DT_SoulmateSkill      (импорт SoulmateSkill.ssi)
	static const TCHAR* kTextTablePath;    // DT_SoulmateSkillText  (импорт soulmateskilltable.bin)

	// Диапазон id навыков Дружбы: `for (i = 30; i < 45; ++i)` (skillHotbarClearAutoSlots@0xE68ED0).
	static constexpr int32 kFirstSkillId = 30;
	static constexpr int32 kLastSkillId  = 44;   // включительно
	static constexpr int32 kSkillCount   = kLastSkillId - kFirstSkillId + 1;   // 15

	// ЗОВ ДРУГА — навык 32 (см. П1 в шапке; сервер: `if (a7 == 32) sub_49E960(...)`).
	static constexpr int32 kMateRecallSkillId = 32;

	// Ячеек лотка ровно 12 (`while (i < 6538)`, social.c:6090-6096) — IDNum 6526..6537.
	static constexpr int32 kTraySlots = 12;

	// Слоты панели быстрого доступа ТИПА 4 = навыки соулмейта (`EAsdaHotbarSlotType::SkillAuto`).
	static constexpr int32 kHotbarSlotTypeSoul = 4;

	// ── Жизненный цикл ──
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка Р5 отчёта 14_soulmate: подсистема жила на GameInstance и переживала
	// разрыв, смену персонажа и травел вместе со списками и защёлками сессии.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// FTickableGameObject — нужен ТОЛЬКО для мыши лотка (клик по живой геометрии, закон
	//   `jui-cell-chain-nesting`) и для протухания кулдаунов. Тикаем лишь при видимом лотке.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSoulmateSkillSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Вкладка 0 окна 6400: показана/скрыта. Зовёт `UAsdaCommunitySubsystem::ApplyTabVisibility`
	//    (или `UAsdaSoulmateSubsystem::OnTabActivated`) — см. «ТРЕБУЕТСЯ ОТ ОРКЕСТРАТОРА».
	//    ★ Состояние НЕ защёлкиваем заранее (закон `no-state-latch-before-shown`): привязка узлов
	//      и первая отрисовка происходят ТОЛЬКО когда окно реально показано.
	void OnTabActivated(bool bVisible, UUserWidget* CommunityWindow);

	// Применить навык Дружбы (общая точка: и клик по лотку, и слот хотбара типа 4).
	//   Порт `skillCastActivate@0xE4D900` → `netSkill_SendSoulmateUse@0xA07540` (5231).
	//   Отправку выполняет `UAsdaSkillUseSubsystem::CastSoulmateSkill` — она уже портирована
	//   и держит гейты состояния; дублировать её здесь нельзя (закон общих точек).
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки Дружбы")
	bool UseSkill(int32 SkillId);
#if !UE_BUILD_SHIPPING
	// Самотест: ячейка лотка, id навыка в ней (−1 — пусто), число привязанных картинок.
	UImage* DebugTrayCell(int32 I) const { return TrayCells.IsValidIndex(I) ? TrayCells[I].Get() : nullptr; }
	int32 DebugTraySkill(int32 I) const { return (I >= 0 && I < kTraySlots) ? TraySkillId[I] : -1; }
	int32 DebugTrayBound() const { int32 N = 0; for (const TObjectPtr<UImage>& C : TrayCells) { N += C ? 1 : 0; } return N; }
	int32 DebugTx5231() const { return Tx5231; }
	bool DebugIsLit(int32 SkillId) const { return IsSkillLit(SkillId); }
	void DebugReleaseTray() { ReleaseTray(); }
#endif

	// Полный сброс при разрыве Дружбы (5207 код 3) — порт social.c:7806-7817.
	//   Чистит лоток, флаги 30..44, кулдауны и слоты хотбара типа 4.
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки Дружбы")
	void ResetTrayAndHotbar();

	/** Порт `skillHotbarClearAutoSlots@0xE68ED0`: погасить навыки Дружбы k = 30..44 (байт ровно 1) в блоке 5228/5238.
	 *  ★ Ревизия 1-Б: публично — его зовёт приём 5226 «Друг уходит» (`UAsdaSoulmateSubsystem::OnPartnerLeft5226`). */
	void ClearTrayLit();

	// Пересобрать лоток (порт `skillHotbarRefreshSlots@0xE69440`). Публично: нужно после
	//   получения карточки соулмейта (5110/5111) и после смены уровня персонажа.
	UFUNCTION(BlueprintCallable, Category="Asda|Навыки Дружбы")
	void RefreshTray();

	// ── Доступ к данным (потребители: тултип лотка, панель быстрого доступа, книга навыков) ──
	const FAsdaSoulmateskillRow*     GetSkill(int32 SkillId) const;
	const FAsdaSoulmateskilltableRow* GetSkillText(int32 SkillId) const;

	/** Остаток кулдауна навыка Дружбы, мс. 0 — навык готов. */
	UFUNCTION(BlueprintPure, Category="Asda|Навыки Дружбы")
	int32 GetCooldownRemainMs(int32 SkillId) const;

	/** Локальный старт кулдауна — порт `Soulmate_StartSkillCooldown@0xE48E70` (social.c:5847).
	 *  Гейты эталона: навык «изучен» (`sub_91E249`) и `record[46] > 0`. */
	void StartCooldown(int32 SkillId);

	/** ЗОВ ДРУГА: ответ игрока на вопрос loc 8163. bYes → 5427 либо переход канала; иначе 5237 код 2. */
	UFUNCTION(BlueprintCallable, Category="Asda|Зов друга")
	void AnswerMateRecall(bool bYes);

	/** Текущий запрос зова (для окна/диагностики). */
	const FAsdaMateRecallRequest& GetMateRecall() const { return Recall; }

	/** ЗОВ ДРУГА, приём итога у ИНИЦИАТОРА — S→C 5237, 12 б (порт `Recv_CharStateNotice@0xF548D0`).
	 *  ★ ТОЧКА ПУБЛИЧНАЯ, А НЕ ДЕЛЕГАТ: контракт `UAsdaNetSubsystem` делегата под этот опкод не даёт
	 *    (там сейчас только разбор в журнал). Её должен позвать `case OP_SEND_RESPONSE_USER_TO_
	 *    REQUEST_USER_MATE_RECALL_SKILL` сети — см. «ТРЕБУЕТСЯ ОТ ОРКЕСТРАТОРА» в отчёте порта. */
	void OnMateRecallAnswer5237(const TArray<uint8>& Body);

	/** Многострочный дамп состояния (`asda.Soul.Skills.Dump`). */
	FString DumpState() const;

	/** Самотест очереди окон-сообщений (`asda.MsgBox.SelfTest`): прогнать тело 5236 боевым приёмником. */
	void DebugInjectMateRecall5236(const TArray<uint8>& Body) { OnMateRecall5236(Body); }
	/** Самотест: есть ли у подсистемы указатель на окно-вопрос зова. */
	bool DebugHasRecallBox() const { return RecallBox != nullptr; }

	/** ★ 19.10.2026 (GM-команда 6058): сбросить все откаты навыков Дружбы. У эталона они лежат в том же реестре окна навыков
	 *  (`Soulmate_StartSkillCooldown` пишет в `CSkillWnd+56`, word0 = 1), который 6058 очищает целиком. */
	void ClearAllCooldowns();

private:
	// ── Загрузка таблиц ──
	void LoadTables();

	// ── Привязка и отрисовка лотка ──
	void BindTrayControls(UUserWidget* CommunityWindow);
	void ReleaseTray();
	/** Порт `uiQuickSlotCursorSet@0xE68A20`: рамка `SOUL_TRAYSEL` (6538) прыгает на ячейку. */
	void SetTraySelection(int32 SlotIdx);
	/** Порт `skillHotbar_FindBestSkillSlot@0xE691A0` — «показывать ли навык k в лотке».
	 *  Возвращает true, если k — представитель своей группы при текущем уровне персонажа. */
	bool IsTrayRepresentative(int32 SkillId) const;

	// ── Приём пакетов ──
	void OnSkillState5232(const TArray<uint8>& Body);      // 197 б
	void OnSkillApply5233(const TArray<uint8>& Body);      // 196 б
	void OnSkillCooldowns2247(const TArray<uint8>& Body);  // 246 б
	void OnMateRecall5236(const TArray<uint8>& Body);      //  42 б

	/** Разбор боевого блока 192 б, общий для 5232 (blk = pkt+5) и 5233 (blk = pkt+4).
	 *  `bFlagsBFromBlockA` — ветка дефекта D2 (5233 читает flagsB по blk+62 вместо blk+174). */
	void ApplyCombatBlock(int32 CasterUserIdx, int32 SkillId,
	                      const TArray<uint8>& Body, int32 BlockOffset, bool bFlagsBFromBlockA);

	// ── Нотисы (единый маршрутизатор `UAsdaMessageSubsystem`) ──
	/** loc-id нотиса по коду состояния 5232 (0..35). 0 = нотиса нет. Таблица — в .cpp. */
	static int32 LocIdForState5232(int32 Code, int32 SkillId);
	/** loc-id нотиса по коду ответа 5237 (0..5). 0 = нотиса нет. */
	static int32 LocIdForRecall5237(int32 Code);
	void PostTip(int32 LocId, const FString& Fallback) const;   // канал «подсказка» эталона

	// ── Утилиты ──
	FString Loc(int32 Id) const;
	UAsdaNetSubsystem* Net() const;
	int32 SelfLevel() const;
	int32 SelfGenderField() const;
	bool  HasSoulmate() const;

	// ── Данные ──
	UPROPERTY(Transient) TMap<int32, FAsdaSoulmateskillRow>     Skills;   // ключ = id
	UPROPERTY(Transient) TMap<int32, FAsdaSoulmateskilltableRow> Texts;    // ключ = id
	bool bTablesLoaded = false;

	// ── Состояние лотка ──
	UPROPERTY(Transient) UUserWidget* CommWnd = nullptr;                  // окно 6400
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TrayCells;            // 12 × SOUL_TRAY00..11
	UPROPERTY(Transient) UWidget*     TraySel = nullptr;                  // SOUL_TRAYSEL (6538)
	/** id навыка в ячейке лотка (эталон `CSoulmateWnd+0x80+4i`), −1 = ячейка пуста. */
	int32 TraySkillId[kTraySlots] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
	/** Эталонные поля выбора: +488 «id выбранного навыка», +490 «выбор взведён». */
	int32 PickedSkillId = -1;
	bool  bPickArmed    = false;
	FAsdaDragArm DragArm;                                                  // взвод перетаскивания (ключ — индекс ячейки)
	UPROPERTY(Transient) FAsdaDragGhost DragGhost;                         // призрак иконки под курсором
	int32  DragSkillId   = -1;                                             // навык «в руке», −1 — перетаскивания нет
	int32  TipSkillId    = -1;                                             // навык под подсказкой, −1 — подсказки нет
	int32  LastDownCell  = -1;                                             // ячейка прошлого нажатия (двойной щелчок, msg 0xD)
	double LastDownTimeS = 0.0;                                            // время прошлого нажатия
	// Подсказка ячейки под курсором (msg 8/0xA). `Hovered` −1 — курсор не на ячейке.
	void TickTrayTip(int32 Hovered);
	// Снять подсказку лотка.
	void HideTrayTip();
	// Начать перетаскивание из ячейки (`DragArm` прошёл порог).
	void BeginTrayDrag(int32 Cell);
	// Бросок: слот панели под курсором получает навык (`hotbarAssignQuickSlot`).
	void EndTrayDrag();
	// Отменить перетаскивание без броска.
	void CancelTrayDrag();
	bool  bTrayBound    = false;
	bool  bTrayVisible  = false;
	FAsdaMouseFrame Mouse;

	/** ★★★ ПИСАТЕЛИ БАЙТА «ИКОНКА НАВЫКА ЗАЖЖЕНА» НАЙДЕНЫ (`clan_soulmate_gaps.md` §3) —
	 *  прежний вывод «писателя в срезе нет» был ОШИБКОЙ МЕТОДА (Hex-Rays показал приёмник
	 *  посторонним глобалом `unk_2C3AB90` и спрятал `memcpy`).
	 *  Писателей ДВА, оба S→C и оба теперь принимает `UAsdaNetSubsystem`:
	 *    • 5228 `netRecvSkillHotbarFull@0xF50560` — весь блок 101 б в `объект+1256`;
	 *    • 5238 `netHandleSkillSlotEnable@0xF505A0` — один слот: `byte_2C3AB91[i16] = 1`.
	 *  Байт навыка k = `blob[k + 1]` (читатель эталона — `объект + 1257 + k`, skill_buff.c:34822).
	 *  ⇒ СВОЕЙ копии массива подсистема больше НЕ ДЕРЖИТ: единственный источник правды —
	 *  `UAsdaNetSubsystem::IsSoulTraySkillLit`. Ключ-костыль `asda.Soul.TrayLit` УБРАН.
	 *  Локальное гашение k = 30..44 (эталон `skillHotbarClearAutoSlots@0xE68ED0`) — `ClearTrayLit()`. */
	/** Зажжена ли иконка навыка `SkillId` — прокси к блоку 5228/5238 в сетевой подсистеме. */
	bool IsSkillLit(int32 SkillId) const;

	// ── Кулдауны навыков Дружбы (30 записей приходят в 2247) ──
	TArray<FAsdaSoulSkillCooldown> Cooldowns;

	// ── Зов друга ──
	FAsdaMateRecallRequest Recall;
	UPROPERTY(Transient) UUserWidget* RecallBox = nullptr;   // двухкнопочный msgbox (тип 38 эталона)

	/** 5228 (весь блок 101 б) / 5238 (один слот) — блок подсветки обновлён, лоток пересобрать.
	 *  `SkillId` = −1 для 5228, либо id открытого навыка для 5238. */
	void OnSkillTrayLit(int32 SkillId);

	// ── Дескрипторы делегатов сети ──
	FDelegateHandle HState, HApply, HCool, HRecall, HTray;
	bool bNetBound = false;

	// Счётчики для дампа: сколько пакетов какого вида принято.
	int32 Rx5232 = 0, Rx5233 = 0, Rx2247 = 0, Rx5236 = 0, Rx5237 = 0, Tx5231 = 0;
	/** Сколько раз пришло обновление блока подсветки лотка (5228 + 5238 суммарно). */
	int32 RxTrayLit = 0;
};
