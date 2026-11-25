// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaQuestMarkerSubsystem — КВЕСТОВЫЕ МЕТКИ NPC: «!» над головой + точки на мини-карте и карте мира.
//   Полный разбор эталона (пакеты, таблицы, формулы, доказательство «какой знак чему») — в
//   Public/Quest/AsdaQuestTypes.h. Здесь — только порт логики.
//
// ТРИ МЕСТА ОТРИСОВКИ (как в оригинале):
//   1) НАД ГОЛОВОЙ NPC — 3D-модель eff_0_npc_99_exclam-a_0{0|1|2}.jbm со своей анимацией _0.jbb.
//      Эталон: загрузка entity_char.c:59290-59380 (case 5..7 = .jbb, case 9..B = .jbm), три слота в
//      сущности NPC — dword 207/208/209 (модель/скелет/флаг), 322/323/324, 437/438/439;
//      включение — npcSlotLoadModelAndSetup@0xD8A6B0 (switch по метке: 1→set_oD1, 2→set_o144,
//      3→set_o1B7, 0→все три в 0); высота — posSetWithChildOffset(..., helper_getSumF(poseMatrix)+60, ...),
//      то есть Y_модели_верх + 60 игровых единиц. Одновременно активна РОВНО ОДНА метка.
//   2) МИНИ-КАРТА и 3) КАРТА МИРА — квад 16×16 текстурой miniQuest (метка 1) / miniQuest2 (метка 2),
//      мигание 1000 мс видно / 200 мс скрыто (uiTextRenderGlyphQuads@0xB537E0). Список точек строит
//      npcUi_BuildNpcActionList@0xD97650: идёт по npc_list.BIN, берёт записи ТЕКУЩЕЙ карты с типом
//      0/4/5, для каждой считает метку и кладёт (npcId, worldX, worldY, метка==2) в список карты
//      (partyListPushEntry@0xB54180). МЕТКА 3 НА КАРТУ НЕ ПОПАДАЕТ (push только при метке 1 или 2).
//
// ИСТОЧНИК СОСТОЯНИЯ КВЕСТОВ: пакет 5047 (0x13B7), 688 байт = 12 слотов по 44 б + 160 б битовой
//   карты «квест завершён». Приём — UAsdaNetSubsystem (OnQuestStateSync).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Quest/AsdaQuestTypes.h"
#include "Styling/SlateTypes.h"        // FTextBlockStyle — стиль подписи трекера до подмены на RichText
#include "Widgets/Text/STextBlock.h"   // ETextJustify
#include "Quest/AsdaQuestAwaitTypes.h" // TIM_List / NMS_list — справочники карт подсказки «Минопио»
#include "Npc/AsdaNpcDialogTypes.h"   // FAsdaNsrCond — ЕДИНАЯ структура условия (см. EvalNsrCond)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"                // FAsdaHeldTables — удержание таблиц квестов на сессию (15.09.2026, dt_gc_hold)
#include "AsdaQuestMarkerSubsystem.generated.h"

// ★ Что именно изменилось в целях квеста — для экранной строки прогресса.
//   Эталон (`questItemResult_apply@0xC63010`) показывает ОДНУ выросшую цель в формате
//   «< Название задания > Название предмета : есть/надо», окно 221, цвет −256 (чистый жёлтый).
struct FAsdaQuestProgressDelta
{
	bool  bValid   = false;   // выросшая цель найдена
	int32 SlotNo   = -1;      // слот журнала 0…11
	int32 QuestNo  = -1;      // номер задания
	int32 ItemId   = 0;       // предмет-цель, который начислили
	int32 NewCount = 0;       // сколько стало
	int32 OldCount = 0;       // сколько было
	// ★ 25.10.2026 (баг «2/0»): стадия слота ДО и ПОСЛЕ пакета. Последний предмет переводит задание на следующую
	//   стадию в том же 5052; эталон `ItemSlot_FormatQuantityTooltip@0xC64050` тогда берёт «надо» из под-записи
	//   ПРЕДЫДУЩЕЙ стадии. −1 — слот до пакета был занят другим заданием (сравнивать нечего).
	int32 OldStage = -1;      // стадия до пакета (того же задания)
	int32 NewStage = -1;      // стадия после пакета
};

class UDataTable;
class AAsdaNpcActor;

// ── КАКАЯ ИЗ ТРЁХ РОДСТВЕННЫХ РАСКЛАДОК УСЛОВИЯ ПЕРЕД НАМИ ──────────────────────────────────────
// Одна и та же «восьмикатегорийная» проверка живёт в эталоне в трёх экземплярах, отличающихся
// ТОЛЬКО смещениями двух последних категорий (слот сумки и «прочее»):
//   · `charEquip_VerifyStatChecksum@0xD850D0` — 144-байтовая ЗАПИСЬ СЦЕНАРИЯ (`DT_NpcQuestScript`),
//     слот сумки `u16 +136`, прочее `i32 +140`  → EAsdaCondLayout::Script;
//   · `netSerializeCharStatPacketA@0xD871C0` (кнопка меню) и `netSerializeCharStatPacketB@0xD88F00`
//     (шаг страницы) — 148-байтовый блок ПОД-ЗАПИСИ, слот сумки `u16 +142`, прочее `i32 +144`
//     → EAsdaCondLayout::SubRecord.
// Разбор и доказательство на байтах — в шапке `Npc/AsdaNpcDialogTypes.h`.
UENUM(BlueprintType)
enum class EAsdaCondLayout : uint8
{
	Script     UMETA(DisplayName = "запись сценария (144 б)"),
	SubRecord  UMETA(DisplayName = "под-запись кнопки/шага (148 б)"),
};

// Какие категории эталон СЧИТАЕТ ЗАДАННЫМИ у кнопки меню и у шага (`charEquip_CountStatCategories`
// и `questEvalConditionFlags`) — они расходятся ровно в одной категории, см. .cpp.
UENUM(BlueprintType)
enum class EAsdaCondOwner : uint8
{
	MenuButton UMETA(DisplayName = "кнопка меню"),
	Step       UMETA(DisplayName = "шаг страницы"),
};

UCLASS()
class ASDAREBORN_API UAsdaQuestMarkerSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка Р1 отчёта 06_quest_ревизия: bStateSynced не сбрасывался НИГДЕ,
	// а Slots/DoneBits/TrackEntries чистил только Deinitialize, который при выходе
	// в выбор персонажа не вызывается — метки строились по квестам ПРОШЛОГО персонажа.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaQuestMarkerSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Состояние квестов игрока (пакет 5047, 688 байт тела) ──
	// Разбирает блоб 1:1 по раскладке CQuest+0x264 и пересчитывает все метки.
	UFUNCTION(BlueprintCallable, Category="Asda|Quest")
	void ApplyQuestStateBlob(const TArray<uint8>& Blob);
	// Пришло ли состояние с сервера (эталон CQuest+0x758 stateSyncedFlag). Пока false — меток нет.
	UFUNCTION(BlueprintCallable, Category="Asda|Quest")
	bool IsQuestStateSynced() const { return bStateSynced; }

	// Метка для NPC по его type-id (1-based, как в npc_list.BIN): 0 нет, 1 жёлтый «!», 2 красный «!», 3 «?».
	UFUNCTION(BlueprintCallable, Category="Asda|Quest")
	int32 GetMarkForNpcId(int32 NpcId) const;

	// Принудительный пересчёт (после смены карты/уровня/квеста). Сам зовётся по событиям и раз в секунду.
	UFUNCTION(BlueprintCallable, Category="Asda|Quest")
	void RebuildMarks();

	// Диагностика для консоли Asda.Quest.Marks — что посчитано и почему.
	FString DescribeMarks() const;

	// ═══════════════ ЕДИНЫЙ ВЫЧИСЛИТЕЛЬ УСЛОВИЙ (порт всех трёх эталонных близнецов) ═════════════
	// Порт связки «сколько категорий задано» (`charEquip_CountStatCategories@0xD84C90` /
	// `questEvalConditionFlags@0xD88990` / `itemTooltip_BuildStatFlagsTable1@0xD84810`) +
	// «все ли заданные сошлись» (`netSerializeCharStatPacketA/B`, `charEquip_VerifyStatChecksum`).
	// Возврат эталона дословно: `задано == сошлось && задано > 0 && сошлось > 0`.
	// ★ ЗДЕСЬ ЖИВЁТ ЕДИНСТВЕННАЯ КОПИЯ ЭТОЙ ЛОГИКИ В ПОРТЕ. Окно диалога NPC
	//   (`UAsdaNpcDialogSubsystem`) зовёт её же — своего вычислителя у него нет.
	// `OutWhy` (необязательно) — построчный разбор «категория → задано/сошлось» для консоли.
	// `ExplicitSet` — необязательная маска «категория задана» из ВНЕШНЕГО источника: у записей
	//   `SNQICT.Bin` включение категории задаётся не значением поля, а отдельными байтами-флагами
	//   (`itemTooltip_BuildStatFlagsTable2@0xD89E80`), поэтому вывести её из самих данных нельзя.
	bool EvalNsrCond(const FAsdaNsrCond& C, EAsdaCondLayout Layout, EAsdaCondOwner Owner,
	                 FString* OutWhy = nullptr, const bool* ExplicitSet = nullptr) const;

	// Условие записи `SNQICT.Bin` (строки облачка `MULTYQUEST_WIN`): собирает общую структуру и
	//   явную маску категорий, затем зовёт тот же `EvalNsrCond`.
	bool EvalSnqIctCond(const FAsdaSnqIctRow& R, FString* OutWhy = nullptr) const;

	// `questSlots_HasFreeSlot@0xC64A30` (имя в эталоне ошибочно — к баффам отношения нет):
	//   журнал квестов вмещает 12 записей, слот занят при `questId > 0`.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	bool HasFreeQuestSlot() const;

	// `questEpisode_FindWordKeyById@0xC648F0` (в эталоне — `buffFindIconByBuffId`):
	//   код эпизода (`Episode_Table +200`) → ключ квеста (`+236`). −1, если записи нет.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	int32 EpisodeIdToQuestKey(int32 EpisodeId) const;

	// `CQuest_Episode_FindByWord236@0xC60A30`: ключ квеста (`+236`) → НАЗВАНИЕ эпизода (`+0`).
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	FString EpisodeTitleByQuestKey(int32 QuestKey) const;

	// `CQuest_Episode_FindById@0xC60A30`-парная: КОД ЭПИЗОДА (`+200`) → НАЗВАНИЕ эпизода (`+0`).
	//   Нужна нотису приёма квеста: `CQuest_RecvPacket` берёт название именно по КОДУ ЭПИЗОДА из
	//   тела 5044 (`+3`), а не по ключу квеста. Ключ строки `DT_EpisodeTable` — сам код эпизода,
	//   поэтому это прямой поиск строки, без перебора.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	FString EpisodeTitleByEpisodeId(int32 EpisodeId) const;

	// Порт `CQuest_HasActiveQuestOrReq@0xC62E10` в режиме `a2 == 0` ЦЕЛИКОМ (в отличие от
	//   `IsQuestTaken`, где перенесён только финальный цикл). Нужен гейту сдачи
	//   `questCheckAcceptPrereq@0xD8AE50`: он проверяет этой функцией квест, который ВЫДАЁТСЯ
	//   вместе со сдачей (поля блока цели `+32` и `+96`). Разбор — в .cpp над телом.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	bool HasActiveQuestOrReqByKey(int32 QuestKey) const;

	// ★ ИСПРАВЛЕН ВРУЩИЙ КОММЕНТАРИЙ (аудит 2026-09-02). БЫЛО написано, что это
	//   `CQuest_HasActiveQuestOrReq@0xC62E10` режим `a2 == 0` — ЭТО НЕВЕРНО, и именно из-за
	//   этой подписи гейт «NPC выдаёт квест» в `AsdaNpcDialogSubsystem` был поставлен не тот
	//   (терялось эталонное правило «двух сюжетных квестов сразу нельзя»: 49 эпизодов,
	//   31 запись выдачи у 13 NPC).
	//   НА САМОМ ДЕЛЕ это порт `questFindActiveSlotByType@0xC61EE0` (разбор — в .cpp над телом,
	//   у строки с этим именем): занятый слот (`+0 >= 2001`), у которого `слот+8` равен номеру.
	//   ⚠ Эталонный `CQuest_HasActiveQuestOrReq` режим 0 — это СОСЕДНЯЯ `HasActiveQuestOrReqByKey`
	//   (объявлена выше). Не путать: у неё другой ключ (QuestKey 0…467, а не EpisodeId 2001…2466)
	//   и другое правило отбора.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	bool IsQuestTaken(int32 QuestNo) const;

	// «Квест готов к сдаче» — `CQuest_HasActiveQuestOrReq@0xC62E10`, режим `a2 == 1`.
	//   ЛЮБОЙ из 12 слотов журнала, у которого `i16 +8` равен номеру: эталон здесь занятость
	//   не проверяет и полноту целей НЕ смотрит (она проверяется отдельно, в
	//   `questNpc_CollectDeliverableItems@0xD89550`). Разбор — в .cpp над телом.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	bool IsQuestReadyToHandIn(int32 QuestNo) const;
	// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] (questmark-vasa): ячейка «ПРИНИМАЕТ» открыта на текущей стадии задания (правило сервера,
	//   `DT_QuestTurnIn`). Зовут и счёт метки, и список сдачи разговора. Ключ `asda.Quest.TakeCellByStage 0` — всегда true.
	bool TakeCellOpenAtStage(int32 NpcId, int32 QuestNo, int32 Key) const;
	// ★★★ ПОРТ `cquestStoreQuestSlotData@0xC61F60` + `questRewardActivateSlot@0xC689C0`.
	//   После УСПЕШНОГО ответа 5044 (код 0) сервер присылает ГОТОВУЮ запись слота журнала прямо
	//   в теле пакета (см. отправитель `sub_45CA70@0x45CA70` в GameServer). Эталон её тут же
	//   кладёт в журнал и НЕ ждёт следующего 5047. Порт этого не делал — из-за чего локальное
	//   состояние оставалось «квест не взят», шаг диалога продолжал предлагаться, и повторное
	//   нажатие получало от сервера отказ 6 («квест уже взят или уже пройден»).
	//   Разбор: `Reports/quest_accept_fix.md`.
	//   Тело 47 б: +3 i32 episodeId, +8 i16 номер слота журнала, +11 i16 номер квеста,
	//   +13 i16 стадия, далее 5 пар (i32 id цели, i16 счётчик) с шагом 6 начиная с +17.
	//   Возвращает номер занятого слота или −1, если тело короткое / слот не определён.
	int32 ApplyAcceptedQuestSlot(const TArray<uint8>& Body);

	// ★ ПОРТ приёма 5042 (`recvRewardQuestProcess@0x45CF90`, ответ на 5041 «сдать шаг»).
	//   Тело 501 б; запись слота журнала (44 б) лежит по смещению +15. `bFinished` = квест
	//   завершён целиком (`+5 == 2`) — тогда слот ЧИСТИТСЯ и взводится бит карты завершения.
	//   Разбор: `Reports/quest_accept_fix.md` §5.
	//   ★ Тело несёт ЕЩЁ ДВЕ записи слота — `+59` и `+103`: это квесты, ВЫДАННЫЕ вместе со сдачей
	//     (эталон кладёт их `CQuest_SetActiveSlot@0xC62120`). Их коды эпизодов возвращаются наружу —
	//     по ним показывается нотис 20197 «Получено задание …», как в `chatBuildTransactionNotice`.
	TArray<int32> ApplyTurnInResult(int32 QuestNo, int32 Stage, bool bFinished, const TArray<uint8>& Body);

	// ★ 31.10.2026 ПОРТ `uiCraftSlotRemoveAndReset@0xC61AD0` (ui_core.c:33646): сервер подтвердил отказ от задания (5007,
	//   результат 1, тип 4) — запись ячейки журнала, чей номер ячейки сервера (+5) равен `ServerSlot`, ЧИСТИТСЯ, затем метки
	//   NPC/миникарты пересобираются (`npcUiRefreshActionList`) и рассылается `OnQuestStateChanged` (окно 700, трекер 1800).
	//   `PreferIdx` — ячейка, от которой отказывались (эталон `acceptSlotIndex`); −1 — искать по номеру ячейки сервера.
	//   Возвращает индекс очищенной ячейки или −1 (совпадения нет — ничего не тронуто).
	int32 ApplyAbandonAck(int32 ServerSlot, int32 PreferIdx);

	// ★ 06.11.2026 ПОРТ `questSlotMoveSwap@0xC5CEE0`: сервер подтвердил перенос (5001, результат 1, тип 4). `bSwap` — обмен двух записей
	//   (подвид 1), иначе переезд в пустую ячейку (подвид 2). Поле записи +5 становится новым номером ячейки (`CQuest_Slot_Set`).
	//   Рассылает `OnQuestStateChanged`. false — ячейки вне 0…11 или источник пуст, ничего не тронуто.
	bool ApplyQuestSlotMove(int32 SrcSlot, int32 DstSlot, bool bSwap);

	// ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ: порт `questClearObjectiveEntries@0xC63A80` (quest.c:3612) — приём 6594 код 1 снимает
	//   слот журнала сданного задания: для каждого слота с `questId (+0) == QuestId` и `номер ячейки (+5) == SlotIndex` —
	//   бит карты завершения и очистка слота, затем перерисовка (`OnQuestStateChanged`). Только при `QuestId >= 2001`.
	//   ★ [ИСПРАВЛЕНИЕ ОРИГИНАЛА] бит ставится лишь внутри 160-байтовой карты (эталон для 20000+ пишет `CQuest+4·(285+2·(id/64))`
	//   далеко за её концом). Возврат — сколько слотов снято.
	int32 ClearBoardQuestSlot(int32 QuestId, int32 SlotIndex);
	// ★ 28.10.2026: уровень своего персонажа для условий заданий (та же точка `ReadSelfLevelClass`, что у NSR-меток:
	//   живой `SelfLevel` 4005/4036, иначе снимок char-list; офлайн-подмена `asda.Quest.FakeSelf`). Её читает доска.
	int32 GetSelfLevelForQuests() const { int32 L = 0, C = 0, S = 0; ReadSelfLevelClass(L, C, S); return L; }

	// Номер слота журнала, в котором лежит квест (для тела 5043 режима «сдать»). −1 — нет.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	int32 FindQuestSlotNo(int32 QuestNo) const;
	// «Квест завершён» (битовая карта пакета 5047) — публичная обёртка приватной проверки.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	bool IsQuestCompletedPublic(int32 QuestNo) const { return IsQuestCompleted(QuestNo); }

	// ════════════ ★ ДОСТУП К СОСТОЯНИЮ ЖУРНАЛА ДЛЯ ОКНА 700 (добавлено 2026-08-22) ═══════════════
	// ★★★ ЕДИНСТВЕННОЕ хранилище состояния квестов в порту — вот эти `Slots` + `DoneBits`.
	//   Журнал заданий (`UAsdaQuestLogSubsystem`) СВОЕЙ копии не держит и читает отсюда.
	//   В оригинале так же: и окно 700 (`CQuest`), и метки, и трекер смотрят в один массив
	//   `CQuest+0x264`, который наполняет пакет 5047.
	const TArray<FAsdaQuestSlot>& GetSlots() const { return Slots; }

	// Состояние журнала изменилось (5047 / 5044 / 5042 / 5052) — перерисоваться должны ВСЕ
	//   потребители: окно 700, экранный трекер 1800, метки. Эталонный аналог — то, что
	//   `CQuestLog_RefreshSlotIcons@0xC5D500` в конце дёргает и мини-карту, и список NPC.
	DECLARE_MULTICAST_DELEGATE(FOnAsdaQuestStateChanged);
	FOnAsdaQuestStateChanged OnQuestStateChanged;

	// ★ ПОРТ приёма 5052 (`questItemResult_apply@0xC63010`, `network.c:1921`), тело 508 б.
	//   Раскладка тела (проверена по смещениям эталона от базы `CQuest+1324`):
	//     +0  u16 episodeId · +4 u8 objType (`==1` — ветка «подобрано/завершено») ·
	//     +13 i32 (`>0` — ветка завершения, иначе обновление слота) ·
	//     +22 44 б ОБНОВЛЁННАЯ ЗАПИСЬ СЛОТА ЖУРНАЛА (та же раскладка, что в 5047):
	//         слот+0 questId, слот+4 категория, слот+5 (i16) НОМЕР СЛОТА журнала,
	//         слот+7 objType, слот+8 (i16) questNo, слот+10 (i16) стадия,
	//         слот+14+6j (i32) id цели, слот+18+6j (i16) счётчик.
	//   ★ До 2026-08-22 пакет 5052 в порту НЕ ОБРАБАТЫВАЛСЯ ВОВСЕ — из-за чего счётчики целей
	//     («убито 3/10») не двигались никогда: журнал ждал следующего полного 5047, а сервер
	//     его после каждого убитого моба не шлёт.
	//   Возвращает номер обновлённого слота журнала (−1 — тело короткое / слот вне 0..11).
	//   ★ `bOutQuestFinished` (2026-08-22, `Reports/quest_notice_sound_fix.md` §2.3):
	//     true — сработала ЭТАЛОННАЯ ВЕТКА ЗАВЕРШЕНИЯ (`missionSetEntryFlag`, тело+13 > 0 при
	//     objType == 1). Слот при этом не переписывается и возврат остаётся −1, поэтому по одному
	//     лишь возврату вызывающий отличить «завершено» от «нечего применять» не мог — и терял
	//     эталонный звук 14. Признак нужен именно для звука, состояние по-прежнему живёт здесь.
	//   ★ `OutDelta` (2026-08-24): какая именно цель выросла — нужно для ЭКРАННОЙ строки прогресса
	//     («< Задание > Предмет : есть/надо»). Эталон `itemSlot_DiffGridSnapshot` показывает ровно
	//     ОДНУ выросшую цель, поэтому и здесь заполняется первая пара, у которой счётчик прибавился.
	int32 ApplyQuestProgressBlob(const TArray<uint8>& Body, bool* bOutQuestFinished = nullptr,
	                             struct FAsdaQuestProgressDelta* OutDelta = nullptr);

	// ★★★ ПОРТ `charStateFindAndBuildRequest@0xC65080` + `netBuildCharStateRequest@0xC65140`:
	//   «условия ЗАВЕРШЕНИЯ квеста выполнены» по таблице `DT_QuestComplete` (файл `QCT.BIN`).
	//   Линейный поиск строки с этим `QuestNo`, затем ТОТ ЖЕ `EvalNsrCond` — второго вычислителя
	//   условий в порту нет и не появляется. Раскладка `Script` (слот сумки u16 +132 QCT,
	//   прочее i32 +136 QCT), режим счёта категорий `MenuButton` (класс включается по `>= 0`).
	//   Используется заголовком 703 окна 700: на последнем шаге к названию добавляется
	//   « (Готово)» — нотис 6163.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	bool IsQuestObjectiveDone(int32 QuestNo) const;

	// Диагностика: почему `IsQuestObjectiveDone` дала такой ответ (для консоли).
	FString DescribeQuestComplete(int32 QuestNo) const;

	// ★★★ СЫРАЯ ПАРА ПОЛЕЙ СТРОКИ `QCT.BIN` ДЛЯ ОКНА `OTHERCHARA_DATA` 4200 (17.09.2026).
	//   ЗАЧЕМ ОТДЕЛЬНЫЙ ГЕТТЕР. `CPlayerInfoWin_SetData@0xDA7E30` (`AsdaStory_game.c:401387-401392`)
	//   бежит по ТОЙ ЖЕ таблице (`g_pQuestRecordArray`, шаг 140, ключ `rec+0 == questId`) и берёт
	//   из неё ДВА СЫРЫХ ЧИСЛА, БЕЗ вычисления условий:
	//     `rec+32`  → сравнивается со стадией квеста цели: равно ⇒ к названию идёт префикс
	//                 «[нотис 6004]» («Готово»); в порту это поле называется `QuestStage`;
	//     `rec+132` → switch 1/2/3 выбирает ЦВЕТ строки и какую из двух иконок квеста показать;
	//                 в порту поле называется `BagSlot` (u16 +132 раскладки `Script`).
	//   ⚠ МИСНЕЙМ ЧЕСТНО ОСТАВЛЕН: разбор окна (`Docs/UI/Character/OTHERCHARA_DATA.md`) зовёт
	//     это поле «questColorType», но у той же записи в раскладке `.NSR`/QCT оно — «слот сумки».
	//     Доказательство, что таблица ОДНА и та же: `charStateFindAndBuildRequest@0xC65080` держит
	//     счётчик в `CQuest+5393*4` и массив в `CQuest+5394*4`, а окно читает `dword_2BF7D44` и
	//     `g_pQuestRecordArray = 0x2BF7D48` — соседние слова одного объекта, шаг записи 140 у обоих.
	//     Своего имени полю не выдумываем и ЗНАЧЕНИЕ не переинтерпретируем — отдаём как есть.
	//   false = строки с таким `QuestNo` в таблице нет (эталон в этом случае оставляет оба нуля).
	bool GetQuestCompleteRawPair(int32 QuestNo, int32& OutStageAt32, int32& OutFieldAt132) const;

	// ★ УДЕРЖАНИЕ ТАБЛИЦ КВЕСТОВ (15.09.2026, отчёт dt_gc_hold). `DT_EpisodeTable`/`DT_QuestComplete`/`DT_NpcNsrHeader`/
	//   `DT_NpcSnq`/`DT_SnqIct`/`DT_QuestGuide` здесь читаются через слабый `AsdaDt::Cached` (в отличие от
	//   `ScriptTable`/`NpcTable`/`TimTable`/`NmsTable`, которые уже `UPROPERTY`) — в -game GC выгружал их между
	//   пересборками меток и трекера. Держим сильно, лениво при первом обращении. Возврат — сколько удержано.
	int32 HoldGameTables() const;
	const FAsdaHeldTables& GetHeldTables() const { return HeldTables; }

	// Перенос 144-байтовой записи сценария в общую структуру условия (поля совпадают один в один,
	//   различаются только имена двух последних — см. EAsdaCondLayout).
	static FAsdaNsrCond CondFromScriptRow(const FAsdaNpcQuestScriptRow& R);

	// `itemTooltip_BuildStatFlagsTable1@0xD84810` + `charEquip_VerifyStatChecksum@0xD850D0` над
	//   144-байтовой ЗАПИСЬЮ СЦЕНАРИЯ: «все заданные категории сошлись». Публично, потому что тем
	//   же вызовом пользуется ветка перебора записей в окне диалога NPC
	//   (`UAsdaNpcDialogSubsystem::RunScriptRecordScan`) — второй копии этой пары в порту нет.
	bool CheckRecord(const FAsdaNpcQuestScriptRow& R) const;

	// Порт `CInventory_CountItemById@0x9998E0`: сколько ШТУК предмета лежит в сумке. Экипируемые
	//   коды считаются по контейнеру 1 (30 ячеек), прочие — по контейнеру 2 (60); у «торгуемых»
	//   складывается количество в стопке, у остальных считаются сами ячейки.
	//   ★ Волна 9 заточки: тело — в общей точке `UAsdaInventorySubsystem::CountItemById`, здесь переадресация.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	int32 CountItemById(int32 ItemCode) const;

private:
	// ── Таблицы ──
	UPROPERTY(Transient) TObjectPtr<UDataTable> ScriptTable;   // DT_NpcQuestScript (записи .NSR)
	UPROPERTY(Transient) TObjectPtr<UDataTable> NpcTable;      // DT_NpcQuestNpc    (npc_list.BIN)
	bool bTablesLoaded = false;
	void EnsureTables();
	// Сильные ссылки на таблицы, читаемые через `AsdaDt::Cached` (см. `HoldGameTables`).
	UPROPERTY(Transient)
	mutable FAsdaHeldTables HeldTables;
	// Часть удержания с путём из AsdaQuestTracker.cpp (`DT_QuestGuide`, группа "QuestTracker").
	int32 HoldTrackerTables() const;

	// NsrName (нижний регистр) → записи сценария, отсортированные по RecIndex.
	TMap<FString, TArray<const FAsdaNpcQuestScriptRow*>> ScriptByNsr;
	// Все NPC из npc_list.BIN (в порядке таблицы — порядок важен, как в эталонном цикле).
	TArray<const FAsdaNpcQuestNpcRow*> NpcRows;

	// ── Состояние квестов ──
	TArray<FAsdaQuestSlot> Slots;        // 12 слотов
	TArray<uint8> DoneBits;              // 160 байт битовой карты завершения
	bool bStateSynced = false;

	// ── Результат ──
	TMap<int32, int32> MarkByNpcId;      // NpcId → метка 1/2/3
	// ★ ДИАГНОСТИКА (консоль `Asda.Quest.MarkAll`): принудительная метка на ВСЕХ NPC карты.
	//   Нужна для проверки высоты: без сервера состояние квестов пусто и настоящих меток нет,
	//   а периодический RebuildMarks иначе гасил бы выставленные вручную. −1 = выключено.
	//   Учитывается ТОЛЬКО в PushOverheadMarks — второго пути постановки метки в порту нет.
	int32 ForcedMarkForAll = -1;
	int32 ShownMapId = -1;
	double RebuildAccum = 0.0;
	bool bDirty = true;

	// ── Порт эталонных проверок ──
	// questIsCompleteFlagSet@0xC61E20: бит QuestNo в карте завершения.
	bool IsQuestCompleted(int32 QuestNo) const;
	// questFindActiveSlotByType@0xC61EE0: есть занятый слот с этим номером квеста.
	bool IsQuestActive(int32 QuestNo) const;
	// questHasActiveSlotByTypeAndParam@0xC61DA0: занятый слот с номером и стадией.
	bool IsQuestActiveWithStage(int32 QuestNo, int32 Stage) const;
	// CQuest_MatchObjective / _typed @0xC626B0 / 0xC621E0 — сверка целей активного квеста.
	bool MatchObjectives(const FAsdaNsrCond& C, bool bTyped) const;
	// questCheckAllPrereqDone@0xD86770 (категория 2).
	bool CheckQuestPrereq(const FAsdaNsrCond& C) const;
	// playerClassRangeMatch@0xD85530 (категория 0) и playerClassMatchesRequirement@0xD85600 (категория 1).
	bool CheckLevel(const FAsdaNsrCond& C) const;
	bool CheckClass(const FAsdaNsrCond& C) const;
	// ── Категории 3…7: сумка (появились вместе с `UAsdaInventorySubsystem`) ──
	// questCheckRequiredItems5@0xD88... → invHasRequiredItems@0xD86E40 (пары «код/количество»).
	bool CheckRequiredItems(const FAsdaNsrCond& C) const;
	// invCountItemsById@0xD86C60 — предмет НАДЕТ (скан 11 слотов надетого).
	int32 CountEquippedById(int32 ItemCode) const;
	// invIsActiveBagTab@0xD86CF0 — открыта такая вкладка сумки.
	bool IsActiveBagTab(int32 A, int32 B) const;
	// inventoryBagHasSlot@0xD86D80 — «есть выбранная ячейка» (0) / «есть соулмейт» (1).
	bool BagHasSlot(int32 Kind) const;
	// questFindMatchingActionMode1/2@0xD8AB90 — метка NPC по его сценарию.
	int32 EvalMarkForNsr(const FString& NsrName) const;

	// `questNpc_CollectDeliverableItems@0xD89550` в режиме СЧЁТА (`a2 == 1`; у `Mode2` — `a2 == 2`,
	//   логика та же, различается только служебный буфер флагов). «Сколько строк облачка
	//   `MULTYQUEST_WIN` этот NPC мог бы показать прямо сейчас»: 10 записей списка «выдаёт» +
	//   10 записей списка «принимает» из `SNQInfo.Bin`, у каждой по 5 ключей `SNQICT.Bin`.
	//   ВОЗВРАТ ДОСЛОВНО ЭТАЛОННЫЙ: 99, если сошлась хоть одна строка СДАЧИ, иначе — суммарное
	//   число сошедшихся строк. Вызывающий (`EvalMarkForNsr`) трактует 99 как метку 2, «≥ 2» как
	//   метку 1, «< 2» — как «эта запись сценария метки не дала, идём к следующей».
	//   Режим `a2 == 0` (заполнение самих списков) живёт в `UAsdaNpcDialogSubsystem::BuildQuestLists`
	//   — второй копии здесь не заводим, считаем только количества.
	int32 CollectDeliverableForNsr(const FString& NsrName) const;

	// Данные персонажа (уровень/класс) из выбранного персонажа сетевой подсистемы.
	void ReadSelfLevelClass(int32& OutLevel, int32& OutClass, int32& OutSubClass) const;

public:
	// ══════════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ ПОДСКАЗКА РОБОТА «МИНОПИО»: «Тебя кое-кто разыскивает…» (окно `QUEST_INFO_WIN`, 599)
	// ══════════════════════════════════════════════════════════════════════════════════════════════
	// ПОЛНЫЙ реверс — `Reports/minopio_window.md`. Здесь живёт СБОРКА СПИСКА КАРТ; сам баннер
	//   показывает `UAsdaNpcDialogSubsystem::ShowQuestInfo` (порт `npcExchangeUiSetText@0xDA3D60`),
	//   потому что в оригинале окно 599 принадлежит тому же объекту `g_pNpcWnd`, что и диалог NPC.
	//
	// ПОЧЕМУ СЧИТАЕТСЯ ИМЕННО ЗДЕСЬ. В эталоне сбор идёт из `npcUiRefreshActionList@0xD97510` —
	//   ровно той функции, чей порт называется `RebuildMarks()`, и пользуется теми же данными:
	//   `npc_list.BIN` + `questFindMatchingActionMode2` (= `EvalMarkForNsr`). Своего хранилища
	//   квестового состояния и своего вычислителя условий у подсказки НЕТ и быть не должно.

	// Карты, где игрока ждёт НОВЫЙ квест (порядок и лимит 10 — как в эталонном массиве
	//   `CQuestMgr+1420`). Пересчитывается вместе с метками.
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	const TArray<int32>& GetAwaitMaps() const { return AwaitMaps; }

	// СЫРАЯ строка подсказки С ТЕГАМИ `<FC=…>` — ровно то, что эталон кладёт в контрол 587.
	//   Пусто = показывать нечего. (Почему в окно уходит версия без тегов — см. §8.3 отчёта.)
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	FString GetAwaitHintMarkup() const { return AwaitMarkup; }

	// Диагностика для консоли `Asda.Quest.AwaitMaps`.
	FString DescribeAwaitMaps() const;

private:
	// ── Справочники карт подсказки ──
	UPROPERTY(Transient) TObjectPtr<UDataTable> TimTable;   // DT_TimList (`TIM_List.BIN`)
	UPROPERTY(Transient) TObjectPtr<UDataTable> NmsTable;   // DT_NmsList (`NMS_list.BIN`)
	TArray<const FAsdaTimListRow*> TimRows;                 // белый список карт + их названия

	// ★ ДЕДУП Д5 (`Reports/quest_npc_dedup_audit.md`): «есть ли карта в белом списке TIM_List»
	//   спрашивают три места (сборка списка, склейка строки, диагностика), и линейный поиск
	//   эталона (`по +0`) жил тремя копиями. Теперь он ОДИН. nullptr = карты в списке нет.
	const FAsdaTimListRow* TimRowByMapId(int32 MapId) const;
	TArray<const FAsdaNmsListRow*> NmsRows;                 // индекс = mapId (как в файле)

	// ── Результат ──
	TArray<int32> AwaitMaps;      // порт `CQuestMgr+1420` (10 слотов, уплотнённые влево)
	FString AwaitMarkup;          // порт `finalText` из `BuffNameList_BuildColoredText@0xD98210`
	TArray<int32> ShownAwaitMaps; // что уже показано — чтобы не поднимать баннер на каждом тике
	int32 AwaitShownOnMap = -1;   // на какой карте показывали
	bool bAwaitHintShown = false; // баннер сейчас поднят НАМИ (а не подсказкой точки сохранения)

	// ★★★ ПОЧЕМУ ПОДСКАЗКА НЕ ПОЯВЛЯЛАСЬ (разбор — `Reports/minopio_window_fix.md`).
	//   Порт считал и ПОКАЗЫВАЛ подсказку по приходу пакета журнала 5047, а он приходит ещё под
	//   загрузочным экраном. Через 0,15–0,25 с `UAsdaWorldSubsystem::RevealWorld()` снимал экран и
	//   звал `UAsdaWindowSubsystem::CloseAll()` — снос ВСЕХ окон, вместе с нашим баннером 599;
	//   повторный показ навсегда блокировала страховка «состав списка не менялся».
	//   Эталон поднимает баннер в КОНЦЕ автомата загрузки, когда мир уже показан
	//   (`npcUiRefreshActionList` в `game_main.c:3445` и `:3872`), — то есть в момент, которому у нас
	//   соответствует `RevealWorld`. Здесь это воспроизведено отложенным показом: пока экран
	//   загрузки висит, показа нет и СОСТОЯНИЕ ПОКАЗА НЕ ЗАПИСЫВАЕТСЯ, а `Tick` поднимает баннер
	//   в первом же кадре после снятия экрана.
	bool bAwaitHintPending = false;

	// Причина, по которой подсказка не показана СЕЙЧАС. Нужна и для лога (одна строка на СМЕНУ
	//   причины, а не на каждый тик — пересчёт идёт раз в секунду), и для `Asda.Quest.AwaitMaps`.
	enum class EAsdaAwaitGate : uint8
	{
		Unknown,      // ещё не считали
		NoDialog,     // подсистема диалога NPC недоступна
		Option,       // опция `g_bOptFlag_15CB4E8` выключена (`Asda.Quest.AwaitHint 0`)
		Map19,        // карта 19 (Поле битвы) — эталон подсказку гасит
		Empty,        // ни одной карты не набралось (показывать нечего)
		Level,        // уровень персонажа <= 2
		DialogOpen,   // открыто окно разговора с NPC
		Loading,      // мир ещё не показан (висит загрузочный экран) — показ отложен
		Unchanged,    // состав списка и карта те же, что при прошлом показе
		Shown,        // показано
	};
	EAsdaAwaitGate AwaitGate = EAsdaAwaitGate::Unknown;

	// Записать причину в лог, ЕСЛИ она сменилась.
	void LogAwaitGate(EAsdaAwaitGate Gate, const FString& Detail);
	// Человекочитаемое имя причины — для `Asda.Quest.AwaitMaps`.
	static const TCHAR* AwaitGateName(EAsdaAwaitGate Gate);

	// Порт `itemFilterValidRecipeList@0xD97D30` + `npcData_buildTypeList@0xD98A70` +
	//   `npcData_LoadModelEntry@0xD98D40` + `listAddUniqueSlot10@0xD98E80` +
	//   `buffNameList_filterActiveAndRefresh@0xD98030`.
	void ComputeAwaitMaps(int32 MapId);
	// Порт склейки строки из `BuffNameList_BuildColoredText@0xD98210` (цвета по порядковому номеру).
	FString BuildAwaitHintMarkup() const;
	// Порт условий показа и вызова `npcExchangeUiSetText` / `npcExchangeUiReset`.
	void MaybeShowAwaitHint(int32 MapId);

	// Раздать метки: точки на карту (SetMarks Quest/Quest2) и модели над головами NPC.
	void PushMapMarks(int32 MapId);
	// ★ [ПОРТ] 31.10.2026 значки ЦЕЛЕЙ взятых заданий на показанной карте мира (`asda.Map.QuestTargets`): гид `QDT.bin`
	//   (`DT_QuestGuide`, координаты цели шага), иначе места мобов шага (`DT_QuestTargetArea`), а у готового к сдаче —
	//   NPC сдачи (`DT_NpcSnq` «принимает» → `npc_list`). Подсказка — название задания и строка цели (RU/EN).
	void BuildQuestTargetMarks(int32 ShownMap, TArray<struct FAsdaMapMark>& Out) const;
	// ★ 31.10.2026 значки целей — только у отслеживаемых трекером заданий: при смене состава трекера метки карты сразу.
	void RefreshMapMarksForTracker();
	// Задания, снятые с трекера выполненными (`TrackerRemoveEntryDone`) — до сдачи/отказа (чистка в `RebuildMarks`).
	TSet<int32> TrackedDoneQuests;
	void PushOverheadMarks();

public:
	// ★ ДИАГНОСТИКА: включить/снять принудительную метку на всех NPC карты (см. ForcedMarkForAll).
	UFUNCTION(BlueprintCallable, Category="Asda|Quest")
	void SetForcedMarkForAll(int32 Mark);

	// ══════════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ ЭКРАННЫЙ ТРЕКЕР ЗАДАНИЙ — окно 1800 `Quest_State_Win` (добавлено 2026-08-22)
	// ══════════════════════════════════════════════════════════════════════════════════════════════
	// ПОЛНЫЙ разбор эталона (окно, лимит, расстояние, стрелка, сеть, дефекты) —
	//   `Reports/quest_tracker.md`. Реализация — `Private/Quest/AsdaQuestTracker.cpp`.
	//
	// ПОЧЕМУ ЗДЕСЬ, А НЕ В ОТДЕЛЬНОЙ ПОДСИСТЕМЕ. В оригинале записи трекера лежат в том же
	//   объекте `CQuestManager`, что и состояние журнала, и питаются из того же массива слотов
	//   (`CQuest+0x264`). Наш порт этого объекта — эта подсистема: тут уже живут `Slots`,
	//   `DoneBits`, `OnQuestStateChanged` и `Tick`. Заводить рядом вторую подсистему значило бы
	//   развести состояние по двум владельцам (закон «★★★ Запрет дублирования»).
	//   Строки «предмет : есть/надо» строит ЖУРНАЛ (`UAsdaQuestLogSubsystem::RebuildTrackerEntry`) —
	//   ровно как в эталоне, где их пишет `QuestReward_UpdateProgressList@0xC65AA0`, метод окна 700.

	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ ЛИМИТ ОДНОВРЕМЕННО ОТСЛЕЖИВАЕМЫХ ЗАДАНИЙ — ЕДИНСТВЕННАЯ ТОЧКА ПОРТА
	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (решение владельца 2026-08-22, разбор —
	//   `Reports/quest_tracker_limit12.md`). В оригинале лимит ШЕСТЬ:
	//   `questTracker_UpdateEntry@0xC58820` → `if (activeSlotCount >= 6) return;`, и в
	//   `gamemain_jui.ini` под записи заготовлены контролы 1801…1824 — ровно 6 групп по 4.
	//   Порт показывает ДВЕНАДЦАТЬ: записи 7…12 не выдумываются, а КЛОНИРУЮТСЯ с первой
	//   (`AsdaUi::CloneWidgetSubtree`), поэтому у них те же скины, шрифты, цвета и отступы.
	//
	// ★ КЛЮЧ ОТКАТА К ЭТАЛОНУ — консольная переменная `Asda.Quest.TrackerLimit`:
	//   `Asda.Quest.TrackerLimit 6` возвращает поведение оригинала прямо в игре, без пересборки
	//   (значение зажимается в 1…kTrackerHardMaxEntries). Отдельного «второго лимита» в коде нет:
	//   и трекер (эта подсистема), и журнал заданий (`UAsdaQuestLogSubsystem`, нотис 7008)
	//   спрашивают ОДНУ функцию `TrackerLimit()` — раньше число 6 было выписано в двух местах,
	//   и они могли разойтись.
	//
	// Значение по умолчанию (что показывает игра) — 12.
	static constexpr int32 kTrackerDefaultMaxEntries = 12;
	// Эталонное значение оригинала. Оставлено В КОДЕ как документированная точка отката.
	static constexpr int32 kTrackerEtalonMaxEntries = 6;
	// Жёсткий потолок: столько групп записей окно порождает клонированием и столько же
	//   указателей держат массивы `TrackGroups`/`TrackTitles`/`TrackConds`/`TrackArrows`.
	//   Поднять лимит выше него через CVar нельзя — контролов под записи просто не будет.
	static constexpr int32 kTrackerHardMaxEntries = 12;

	// ЕДИНАЯ точка чтения лимита (CVar с зажимом в 1…kTrackerHardMaxEntries).
	static int32 TrackerLimit();

	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА № 3 — задания БЕЗ требований по предметам (заявка владельца
	//   2026-08-23, полный разбор — `Reports/quest_tracker_fix.md` §Д1).
	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ЭТАЛОН строит запись трекера ТОЛЬКО из строк «предмет : есть/надо»
	//   (`QuestReward_UpdateProgressList@0xC65AA0`: `if (!itemListRec) return 0;`), поэтому у
	//   разговорного задания («сходи», «осмотри», «поговори») галка 743 гаснет МОЛЧА: под-запись
	//   шага в `.QST` равна −1, записи с таким ключом в `SMInfo.BIN` нет. Это не сломанные данные:
	//   195 книг заданий из 442 не имеют ни одной такой ссылки.
	// ПОРТ берёт такое задание в трекер, ЕСЛИ у него есть цель с координатами в `QDT` — тогда
	//   показывается заголовок + расстояние + стрелка, `LineCount = 0`. Нет координат — отказ,
	//   как в эталоне.
	// ★ КЛЮЧ ОТКАТА: `Asda.Quest.TrackerNoItemQuests 0` — вернуть эталонный отказ.
	static bool TrackerAllowNoItemQuests();

	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА № 4 — видимость стрелки-компаса на дистанции (заявка владельца
	//   2026-08-23, разбор и числа — `Reports/quest_tracker_fix.md` §Д2.3).
	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ЭТАЛОН (`CQuest_Episode_DrawWaypoints@0xC59660`):
	//   своя карта  → alpha = clamp(255 − 0.5714286·d, 55, 255);
	//   чужая карта → alpha = 56 (фиксированная, `0x38FFFFFF`).
	// Владелец: «далеко от цели стрелка слишком прозрачная — подними на 10 %». Понято как
	//   +10 % ПОЛНОЙ ШКАЛЫ (+0.10·255 = +25.5), а не +10 % от текущего значения: относительные
	//   10 % дали бы 55 → 60.5 (21.6 % → 23.7 % непрозрачности) — на 20-пиксельном значке
	//   не видно вообще. +25.5 дают 55 → 80.5 (21.6 % → 31.6 %, в 1.46 раза плотнее).
	// Поднимается ВСЯ кривая и её дно, и тем же числом — фиксированная альфа «чужой карты»
	//   (56 → 81.5): именно её игрок видит, пока цель на соседней карте.
	// ★ КЛЮЧ ОТКАТА: `Asda.Quest.TrackerArrowAlphaBoost 0` — эталонная формула.
	//   Значение — проценты полной шкалы, по умолчанию 10, зажим 0…50.
	static float TrackerArrowAlphaBoost();

	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА № 5 — НЕДОСТУПНАЯ галка 743 (заявка владельца 2026-08-23,
	//   разбор — `Reports/quest_tracker_fix2.md` §Е1).
	// ══════════════════════════════════════════════════════════════════════════════════════════
	// Владелец: «у заданий без координат вообще уберём возможность ставить галочку, а у
	//   разговорных, но с локацией и координатами — оставим».
	// ЭТАЛОН: галка ставится, построитель возвращает 0, обработчик 67073 снимает флаг и ГАСИТ
	//   галку — то есть она «ставится и тут же гаснет», молча, без объяснения. Запрет у чекбокса
	//   в оригинале есть (латч `+609`, `checkBox_OnToggle@0x12FDF70` при нём не реагирует вовсе),
	//   но ОТДЕЛЬНОГО ВИДА у запрещённого состояния нет: в скине чекбокса четыре кадра
	//   (вкл/выкл × наведение), «серого» пятого не существует.
	// ПОРТ: если у задания нет ни строк «предмет : есть/надо», ни цели с координатами в `QDT`
	//   (или условия уже выполнены), галка СРАЗУ недоступна — не реагирует (эталонный латч +609
	//   = `SetIsEnabled(false)`) и приглушена вместе с подписью 744, чтобы игрок видел, что
	//   отслеживать нечего. Приглушение — наше, оригинал так не умеет.
	// ★ КЛЮЧ ОТКАТА: `Asda.Quest.TrackerLockUselessCheckbox 0` — вернуть эталонное
	//   «ставится и тут же гаснет».
	static bool TrackerLockUselessCheckbox();

	// Строк условий на запись (эталон: три контрола `Quest_State_Condition{i}_{1..3}`).
	static constexpr int32 kTrackerMaxLines = 3;

	// Порт `questTracker_UpdateEntry@0xC58820`. Кладёт/обновляет строку `LineIdx` записи задания.
	//   Возврат: false — запись не создана, потому что занят весь лимит (эталон молча выходит).
	//   Как в эталоне, ОБНОВЛЁННАЯ запись меняется местами с нулевой.
	bool TrackerUpdateEntry(int32 QuestId, int32 QuestNo, const FString& Title,
	                        const FString& Line, int32 LineIdx);

	// Порт `questTracker_RemoveEntry@0xC58620`: снять задание с отслеживания (сдвиг хвоста влево).
	bool TrackerRemoveEntry(int32 QuestId);
	// ★ 31.10.2026 снять с трекера ПОТОМУ, ЧТО условие задания выполнено (авто-снятие журнала, как у эталона). Задание
	//   запоминается: его точка на карте мира переключается на NPC сдачи и живёт до сдачи/отказа (решение владельца).
	bool TrackerRemoveEntryDone(int32 QuestNo);
	// Значки целей карты мира показываются у этого задания: оно в трекере или снято с него выполненным (см. выше).
	bool IsQuestTargetShown(int32 QuestNo) const { return IsTrackedQuest(QuestNo) || TrackedDoneQuests.Contains(QuestNo); }

	// Порт `questTracker_Clear@0xC585B0`: смена персонажа/выход.
	void TrackerClear();

	// Сколько строк условий показывать у записи. Нужен ветке «задание без предметных требований»:
	//   эталон там оставляет `lineCount` от предыдущего наполнения, у нас же запись создаётся
	//   сразу с одной пустой строкой — и без этой поправки следующая запись уезжала бы на 15 px.
	void TrackerSetLineCount(int32 QuestId, int32 LineCount);

	// Что сейчас отслеживается (для окна и для консоли).
	const TArray<FAsdaQuestTrackEntry>& GetTrackEntries() const { return TrackEntries; }

	// Отслеживается ли задание (журнал сверяет с ним галку чекбокса 743).
	UFUNCTION(BlueprintPure, Category="Asda|Quest")
	bool IsTrackedQuest(int32 QuestId) const;

	// ── Цель отслеживаемого задания: куда идти и как далеко ────────────────────────────────────
	// Порт связки «поиск строки `DT_QuestGuide` по (questNo, стадия)» + `pathGetNextStepCoords`.
	struct FAsdaTrackTarget
	{
		bool  bValid     = false;   // цель найдена (есть запись гида с координатами)
		int32 MapId      = -1;      // карта ЦЕЛИ (не портала)
		int32 CellX      = -1;      // клетка, КУДА ИДТИ: цель либо портал на пути
		int32 CellY      = -1;
		bool  bSameMap   = false;   // цель на текущей карте (ветка стрелки `a4 == 1`)
		bool  bViaPortal = false;   // координаты подменены порталом следующего шага
		FString TargetName;         // имя моба/NPC (эталон собирает его тут же)
	};
	// `CurMapId` — карта, на которой стоит игрок.
	bool GetTrackTarget(int32 QuestNo, int32 CurMapId, FAsdaTrackTarget& Out) const;

	// Порт `pathGetNextStepCoords@0xFBE6A0` + `pathSearchRouteRecursive@0xFBE220`:
	//   координаты клетки портала, ведущего к `TargetMap` с `CurMap`. false — маршрута нет.
	//   ★ 27.09.2026: рёбра на карты, закрытые на сервере (`AsdaMapRegistry::IsMapOpenOnServer`, зеркало
	//     OpenMapInfo.ini), в маршрут не идут; цель на закрытой карте ⇒ false (ветка эталона «маршрута нет»).
	//   ★ `Grid` и `MeTile` необязательны: если переданы, цель выбирается как ближайшая ПРОХОДИМАЯ
	//     клетка внутри зоны перехода. Без них берётся центр зоны — а он вполне может стоять в стене
	//     (у Alpen → Rein свободна лишь одна колонка), и поход упрётся в неё, как было до 24.08.2026.
	//   ★ `OutApproach` — клетка ПОДВОДА снаружи зоны, ровно напротив цели: пройдя её, персонаж
	//     входит в зону по прямой и перпендикулярно кромке («в лицо порталу»).
	static bool NextPortalStep(int32 CurMap, int32 TargetMap, int32& OutCellX, int32& OutCellY,
	                           const class UAsdaWalkGrid* Grid = nullptr,
	                           const FIntPoint* MeTile = nullptr,
	                           FIntPoint* OutApproach = nullptr);

	// ★ Куда идти внутри зоны перехода: зона — прямоугольник, и она может быть почти целиком
	//   непроходимой. Выбираем ближайшую к игроку ПРОХОДИМУЮ клетку зоны; если сетки нет или
	//   свободных клеток не нашлось — центр зоны, как раньше.
	static void PickCellInPortalZone(int32 X0, int32 Z0, int32 X1, int32 Z1,
	                                 const class UAsdaWalkGrid* Grid, const FIntPoint* MeTile,
	                                 int32& OutCellX, int32& OutCellY,
	                                 FIntPoint* OutApproach = nullptr);

	// Диагностика для консоли `Asda.Quest.Tracker`.
	FString DescribeTracker() const;

private:
	// ── Состояние трекера ──
	TArray<FAsdaQuestTrackEntry> TrackEntries;

	// ── Окно 1800 ──
	UPROPERTY(Transient) TObjectPtr<class UUserWidget> TrackWin = nullptr;
	UPROPERTY(Transient) TObjectPtr<class UImage>      TrackBg = nullptr;      // `Quest_State_Win_0_0`
	UPROPERTY(Transient) TArray<TObjectPtr<class UCanvasPanel>> TrackGroups;   // `Quest_State_Title{i}_grp`
	// ★ Заголовок несёт теги `<FC=…>`, поэтому показывается через ЕДИНУЮ точку форматированного
	//   текста `UAsdaRichText::ApplyFormatted`, а она ПОДМЕНЯЕТ `UTextBlock` на `URichTextBlock`.
	//   Значит и хранить его надо как `UWidget*` — иначе указатель протухнет после первой подмены.
	UPROPERTY(Transient) TArray<TObjectPtr<class UWidget>>      TrackTitles;   // `Quest_State_Title{i}`
	// Стиль исходного `UTextBlock` из JUI (кегль/цвет/тень) — снимается ОДИН РАЗ, пока подписи ещё
	//   не подменены, иначе после первого `ApplyFormatted` его уже не прочитать.
	FTextBlockStyle TrackTitleStyle;
	TEnumAsByte<ETextJustify::Type> TrackTitleJustify = ETextJustify::Left;
	bool bTrackTitleStyleCaptured = false;
	UPROPERTY(Transient) TArray<TObjectPtr<class UTextBlock>>   TrackConds;    // (лимит)×3, плоско
	UPROPERTY(Transient) TArray<TObjectPtr<class UImage>>       TrackArrows;   // стрелка компаса на запись
	// Ручка перетаскивания и кнопка закрытия — их надо двигать ВМЕСТЕ с окном, когда список
	//   записей поднимает верх окна (см. `UpdateTrackerWindow`, блок «окно не должно уезжать»).
	UPROPERTY(Transient) TObjectPtr<class UWidget>     TrackDragZone = nullptr; // `Quest_State_Win_0_DRAG`
	UPROPERTY(Transient) TObjectPtr<class UWidget>     TrackCloseBtn = nullptr; // `Quest_State_CloseBtn` (1850)
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> ArrowTexSame = nullptr;  // `QuestDirAnother`
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> ArrowTexFar  = nullptr;  // `QuestDir`
	bool bTrackUiReady = false;
	// Верх окна в юнитах канваса — база, от которой эталон отсчитывает `lineY`
	//   (`15*строк + 5*i + 18`). Берётся из слота фона `Quest_State_Win_0_0` ОДИН РАЗ, до того как
	//   мы начнём двигать группы: иначе на втором кадре база уехала бы вместе с ними.
	//   ★ Из снятого значения ВЫЧИТАЕТСЯ уже применённый сдвиг перетаскивания, поэтому это именно
	//   ДИЗАЙН-верх окна из .JUI (382), а не «где окно оказалось» — иначе сохранённая между
	//   сессиями позиция складывалась бы сама с собой при каждом открытии.
	float TrackBaseTop = 0.f;
	// Смещение кнопки закрытия и ручки относительно верха окна (из .JUI: 384 − 382 = 2).
	//   Снимается там же и тем же способом — числами ассета, а не константой в коде.
	float TrackCloseDY = 2.f;
	float TrackDragDY  = 0.f;

	// ★ СНИМОК ПОСЛЕДНЕЙ РАСКЛАДКИ — чтобы проверка была ЧИСЛАМИ, а не «на глаз по кадру»
	//   (закон `false-negatives-method`). Печатается в `DescribeTracker` (консоль Asda.Quest.Tracker).
	float TrackLastTop = 0.f;      // верх плиты окна, юниты канваса
	float TrackLastH   = 0.f;      // высота плиты окна
	float TrackLastFit = 1.f;      // рендер-масштаб блока (1 = эталонный вид)
	float TrackLastBandTop = 0.f;  // границы свободной полосы правой колонки
	float TrackLastBandBot = 0.f;
	float TrackLastVpH = 0.f;      // высота вьюпорта в юнитах канваса

	// Печатали ли уже в лог, что раскладка окна отступила от эталонной (подъём/ужимание списка).
	//   Нужен, чтобы след писался ОДИН раз на изменение, а не каждый кадр (окно тикает всегда).
	bool bTrackLayoutDeviated = false;

	// Проявление окна по наведению (`CQuest_State_Update@0xC58340`): 0…60 из 255.
	float TrackSlideAnim = 40.f;

	// Открыть/найти окно и разобрать дерево (порт `CQuest_State_BuildUI@0xC580E0`).
	bool EnsureTrackerWidget();
	// Перестроить строки и стрелки (порт `CQuestTracker_BuildWaypointStrings@0xC58A10`).
	//   Зовётся каждый кадр из `Tick` — в эталоне так же (из `MiniMap_Render@0xB4FCC0`).
	void UpdateTrackerWindow(float DeltaTime);
	// Кнопка 1850 `Quest_State_CloseBtn` (порт `questTracker_Hide@0xC58500`, режим «спрятать»).
	UFUNCTION()
	void OnTrackerCloseClicked();

public:
	// Скрыть окно трекера, не трогая записи (эталон `questTracker_Hide(this, 1)` —
	//   так его гасит `uiCloseAllWindows`). Показ вернётся при следующем изменении состава.
	UFUNCTION(BlueprintCallable, Category="Asda|Quest")
	void TrackerHideWindow();

	// ★ Волна В4 ремесла (ревизия В3 С-2) — слот 7 снимка открытых окон. Предикат снимка —
	//   `CQuest_Episode_GetState(g_pQuestManager) == 1` (quest.c:1179: байт видимости окна трекера +292).
	bool IsTrackerWindowShown() const;
	// Восстановление слота 7 — `sub_91ECD5` → `sub_C584C0@0xC584C0` (quest.c:1171): показать окно трекера, если
	//   оно создано. Тот же показ, что делает `UpdateTrackerWindow` при непустом списке.
	void TrackerShowWindow();

private:
	// ★ 18.10.2026: самотест `asda.Quest.SelfTestJobChange` читает `EvalMarkForNsr` (тот же путь, что у меток).
	friend struct FAsdaQuestJobChangeSelfTest;
	// ★ 29.10.2026: самотест `asda.SelfTest.QuestDialogReward` (метки «!» по выровненным записям сценария) — тот же путь.
	friend struct FAsdaQuestDialogRewardSelfTest;
	// ★ 31.10.2026: самотест `asda.SelfTest.QuestFollow` (раздел T: «!» над Эллосой при ячейке слота 10 — таблица расширения
	//   `QuestNpcTableExt.Bin`) — `CollectDeliverableForNsr`, тот же путь, что у меток.
	friend struct FAsdaQuestFollowSelfTestAccess;
	// ★ 04.11.2026: самотест `asda.SelfTest.QuestMarkPage` (метка и разговор пропускают одни и те же записи без страницы).
	friend struct FAsdaQuestMarkPageSelfTest;
};
