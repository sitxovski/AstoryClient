// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaScrollCallSubsystem — СВИТКИ ПЕРЕМЕЩЕНИЯ К ИГРОКУ И ПРИЗЫВА ИГРОКА: окно ввода имени `GIFT_MSGBOX` (JUI **1552**),
//   отправки 5458 / 6134, ответы 5459 / 6135, запрос цели 6136 → ответ 6137.
//   Порт 06.10.2026, волна 2-Г. Реверс, таблица нотисов и итог — `Reports/owner_1006/wave2D_gift_townwar.md` §1.
//
// ═════════════════ ★★★ ЧТО ЭТО ═══════════════════════════════════════════════════════════════════
//   ИМЯ ОКНА ВРЁТ: `GIFT_MSGBOX` — не «подарок премиум-магазина». Окно ставит ТОЛЬКО `CItemManager_UseInventoryItem@0xA0FDF0`
//   (использование премиум-предмета, `switch (special_itemlist.subType − 35)`; второй метод — скан всех 858 вызовов
//   `CItem_BuildTooltipText` с константным видом в образе 13 344 768 б: виды 59/60 толкают ровно 0xA0FF61/0xA0FF26):
//     • SubType **38** — свиток ПЕРЕМЕЩЕНИЯ к игроку (коды 1/2/3/33/34/35/202/203/204/226/240/248/273/283/347/351/426/454/507):
//       ячейка → `CMsgBoxMgr+0x32F0` (`set_oCBC`), окно 1552 вид **60**, текст 12832;
//     • SubType **64** — свиток ПРИЗЫВА (коды 354/355/356/455/509/545): ячейка → `+0x32F4` (`set_oCBD`), вид **59**, текст 104000.
//   Кнопка 1554 (или Enter в 1558) → вид 59: 6134 (charId, ячейка, имя); вид 60: 5458 (ячейка, имя). 1555 — закрыть.
//
// ═════════════════ ОТВЕТЫ (все ветки эталонных обработчиков) ════════════════════════════════════
//   6135 (62 б) `netRecvInventoryItemUpdate@0xDCE180` (network.c:26431-26553): +4 код, +5 запись предмета 57 б.
//     код 1 — свиток израсходован: количество (+14) == 0 → ячейка (+9 контейнер, +10 слот) очищается, иначе запись
//     кладётся целиком; перерисовка сумки; ONEBUT 104004. Коды 0/2…11 → ONEBUT 104006/104007…104016. Прочие — молча.
//   5459 (84 б) `recvItemStatusNotice@0xF34C20` → `uiTooltip_ShowItemStatusNotice@0xF26880` (ui_core.c:97921-97995):
//     код 1 — откат предмета (+25 запись: код, +29 контейнер, +30 слот; `CooldownBase` таблицы) и ПЕРЕХОД
//     `netPortalChannelTransfer` (+1 IP[16], +17 порт, +19/+21 тайл, +23 карта). Отказы — ПЛАШКА 221 (−11514, 10 с):
//     0 → 12837, 2 → 12838, 3 → 12839, 4 → 12844, 5 → 12853, 6 → 12846, 7 → 12165, 8 → 12169, 9 → 13919, 26 → 12857.
//   6136 (62 б) `campWarBuildResultTooltip@0xDCED80` (имя врёт; combat_pvp.c:6090-6118): меня зовут. Если запрос уже
//     висит (байт +62 буфера `byte_2C2F380`) — молча; иначе тело копируется, TWOBUT (1, 61) «104100 %s» (имя +24),
//     кнопки 104002 «OK» / 104003 «Отказать». «Да»/«Нет» → 6137 (20 байт тела + 1 / 10), признак снимается.
//     Перенос самой цели приходит отдельно — 6013 (`portalChannelTransferBegin`, уже в порту).
//   ★ Гейты сервера: 5458 — «душа» молча (диспетчер `case 0x1552`), одержимость → 5459 код 7; 6134 — одержимость →
//     6135 код 3. Оба — таблицами `AsdaPossessGate` (нотис оригинала до провода). Нумпад окно не зовёт.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"          // IAsdaSessionResettable
#include "AsdaNetQueue.h"              // ★ U7: TAsdaNetQueue — очередь пакетов из сетевого колбэка
#include "AsdaScrollCallSubsystem.generated.h"

// ── Тексты. Все id проверены по `RawAssets/Text/GameText.csv`; каждый — из СВОЕГО обработчика. ──
namespace AsdaScrollLoc
{
	constexpr int32 kMeetPrompt   = 12832;    // окно 1552 вид 60: «Чтобы переместиться к персонажу, введи его имя.»
	constexpr int32 kSummonPrompt = 104000;   // окно 1552 вид 59: «Введи имя персонажа, которого хочешь позвать»
	constexpr int32 kSummonOk     = 104004;   // 6135 код 1: «Вызов прошел удачно»
	constexpr int32 kSummonBase   = 104006;   // 6135 код 0 → 104006; коды 2…11 → 104007…104016 (`104005 + код`)
	constexpr int32 kCallPrompt   = 104100;   // 6136: «%s зовет тебя»
	constexpr int32 kCallYes      = 104002;   // TWOBUT 61 «OK»
	constexpr int32 kCallNo       = 104003;   // TWOBUT 61 «Отказать»
}

UCLASS()
class ASDAREBORN_API UAsdaScrollCallSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// Подписки: 6135/6136 (`OnWave2DPacket`), 5459 (`OnItemStatusNotice`).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Отписки.
	virtual void Deinitialize() override;
	// Полный сброс — признак «меня зовут» и очереди ответов (записи окон снимает очередь окон-сообщений сама).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Разбор пришедших тел (закон no-reentrant-net-callbacks).
	virtual void Tick(float DeltaTime) override;
	// Стат тикающего объекта.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaScrollCallSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// Вход из использования предмета (`UAsdaInventorySubsystem::UseByDoubleClick`, ветка премиум-предмета):
	//   SubType 38 → окно 1552 вид 60, SubType 64 → вид 59. Ячейка — слот СУМКИ (эталон адресует `&g_InventoryBag + 57·слот`).
	//   true — окно поставлено в очередь окон-сообщений.
	bool OpenNameBox(int32 ContainerId, int32 CellIndex, int32 SubType);

	// ── Диагностика / самотест ──
	int32 GetMeetCell() const { return MeetCell; }          // `CMsgBoxMgr+0x32F0` — ячейка свитка перемещения
	int32 GetSummonCell() const { return SummonCell; }      // `CMsgBoxMgr+0x32F4` — ячейка свитка призыва
	bool IsCallPending() const { return bCallPending; }     // байт +62 буфера 6136 — «меня зовут», ответ ещё не дан
	const TArray<int32>& GetNoticeTrace() const { return NoticeTrace; }   // loc-id выданных нотисов (по порядку)
	FString GetLastNoticeText() const { return LastNoticeText; }          // текст последнего нотиса/запроса
	void ClearTrace() { NoticeTrace.Reset(); LastNoticeText.Reset(); }

private:
	void OnMeetResult(const TArray<uint8>& B84);     // `uiTooltip_ShowItemStatusNotice@0xF26880`
	void OnSummonResult(const TArray<uint8>& B62);   // `netRecvInventoryItemUpdate@0xDCE180`
	void OnCallRequest(const TArray<uint8>& B62);    // `campWarBuildResultTooltip@0xDCED80`
	void OnNameBoxAnswer(int32 Kind, bool bOk, const FString& Name);   // `OnSendItemUpgrade` case 1554 (виды 59/60)
	void NoticeMsgBox(int32 LocId);                  // ONEBUT `CItem_BuildTooltipText(…, 0, −1, loc)` + след
	void NoticePlank(int32 LocId);                   // плашка 221 `CControl_SetTooltipText(g_pChatWnd, loc, −11514, 10000)` + след
	FString Loc(int32 LocId) const;                  // loc-строка (теги срезаны)

	int32 MeetCell = -1;                // `+0x32F0` (dword 3260) — ячейка свитка перемещения на момент открытия окна
	int32 SummonCell = -1;              // `+0x32F4` (dword 3261) — ячейка свитка призыва
	bool bCallPending = false;          // байт +62 буфера `byte_2C2F380`
	TArray<uint8> CallBody;             // буфер `byte_2C2F380` (62 б тела 6136)
	TAsdaNetQueue<TPair<int32, TArray<uint8>>> Pending;   // (опкод, тело) из сети — разбор в Tick
	FDelegateHandle HWave;              // подписка `OnWave2DPacket`
	FDelegateHandle HMeet;              // подписка `OnItemStatusNotice`
	TArray<int32> NoticeTrace;          // след нотисов (самотест)
	FString LastNoticeText;             // последний текст (самотест)
};
