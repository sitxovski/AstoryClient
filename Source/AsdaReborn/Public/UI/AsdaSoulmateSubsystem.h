// Copyright (c) 2026 @sitxovski (Discord)
// Подсистема соулмейта («Друг») — содержимое вкладки 0 окна COMMUNITY_WIN (6402 COMMUNITY_SOUL_WIN)
//   плюс окна-спутники: приём предложения SMate_ReqAnsr_Win (8400) и предложение по имени
//   SOULMATE_LINK_WIN (9100). Разбор — `Reports/soulmate_6400.md` §3–§5.
// Сеть (отправка/приём) живёт в UAsdaNetSubsystem; здесь — состояние карточки, привязка контролов,
//   реакция на делегаты и жизненный цикл окон-спутников.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"   // FAsdaMiniBarFill + ClipFill — ОДНО ядро полос на порт
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Layout/Margin.h"         // FMargin
#include "Styling/SlateTypes.h"         // FButtonStyle — храним исходные стили позиций 8202
#include "Styling/SlateBrush.h"         // FSlateBrush  — храним исходные кисти планов 8202
#include "AsdaBytes.h"   // ★ R5-fix-c (13.11.2026): общие чтения i16/i32 записи Друга
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "Tickable.h"            // FTickableGameObject — покадровый опрос мыши над строками окна 8200 (С-3, 07.10.2026)
#include "Containers/Ticker.h"   // FTSTicker::FDelegateHandle — ★ 24.10.2026: отложенный запрос 2244 (сведения о Друге вне игры)
#include "AsdaSoulmateSubsystem.generated.h"

class UUserWidget;
class UButton;
class UCheckBox;   // галки блока «доступ к персонажу» 6543..6546 — скин Box_Check = CheckButton → UCheckBox
class UTextBlock;
class UEditableText;
class UProgressBar;
class UImage;
class UMultiLineEditableText;

// ★★★ ЗАПИСЬ СОУЛМЕЙТА — точная копия `CSoulmateWnd+1357` (`gap_54D`) эталона, 251 байт.
//   Держим её сырой, ровно как оригинал: 5110 переписывает всю, 5111 — только свой список полей
//   (см. комментарий у `UAsdaNetSubsystem::OnSoulmateCard`). Это исключает выдумывание полей.
struct FAsdaSoulmateRecord
{
	static constexpr int32 kSize = 251;
	uint8 B[kSize] = {};

	// Смещения доказаны `CSoulmateWnd_RefreshInfo@0xE63410` + `soulmateUpdateInfo@0xE64C30`.
	int32  Slot()          const { return Rd32(0);  }   // gap_878[47]
	int32  PartnerCharId() const { return Rd32(4);  }   // gap_8B4[0..3]
	int32  PartnerUserNum()const { return Rd16(8);  }   // gap_8B4[4..5]
	uint8  Level()         const { return B[29]; }      // уровень партнёра  → 6521
	// ★★★ 23.10.2026 МИСНЕЙМ ИСПРАВЛЕН: rec[38]/rec[39] — КАРТА и КАНАЛ Друга, а не класс/ступень. Сервер кладёт туда
	//   `sub_40C960` (номер карты своего GameServer) и `sub_4A7EB0` (канал) — `sub_4940C0@0x4940C0`, сообщение 3313;
	//   эталон `CSoulmateWnd_RefreshInfo` печатает их в 6523 «Локация» (loc 12109 у подписи 6554) как
	//   «  <канал+1> <loc 1187 "кан."> <имя карты>» по таблице карт 0x2BCD3F0 (шаг 72); карта ≥ 101 — поле пустое.
	uint8  MapId()         const { return B[38]; }      // карта Друга        → 6523, фильтр маркера Couple
	uint8  Channel()       const { return B[39]; }      // канал Друга (0..)  → 6523 (==6 → loc 13927)
	uint8  ClassId()       const { return B[38]; }      // ⚠ прежнее имя (мис-нейм) — только для ключа отката asda.Soulmate.LocationText 0
	uint8  ClassTier()     const { return B[39]; }      // ⚠ прежнее имя (мис-нейм) — то же
	int32  TileX()         const { return Rd16(25); }   // тайл X Друга (CUser+21734) — 5110/5111
	int32  TileZ()         const { return Rd16(27); }   // тайл Z Друга (CUser+21736) — 5110/5111
	uint8  Grade()         const { return B[41] ? B[41] : B[40]; }   // звание → 6556
	int32  SoulIndex()     const { return Rd32(42); }   // iSoulMateIndex
	uint8  FriendLevel()   const { return B[50]; }      // уровень Дружбы    → 6557 / 6501
	// Имя партнёра — cp1251-строка с rec[63] до NUL.
	int32  NameOffset()    const { return 63; }

	// ── ПОЛЯ ПОП-КАРТОЧКИ 6500. Читает их ТОЛЬКО `unitFrame_Refresh@0xE63810` и его помощники;
	//   до аудита 31.08 порт поп-панель не строил вовсе, поэтому и аксессоров не было. ──
	// 6508 `SOULMATE_GAUGE02` (`progressUpdateBar@0xE63F20`, game.c:471707-471712):
	//   доля = *(int*)(win+1374) / *(int*)(win+1370) = rec[17..20] / rec[13..16].
	int32  Bar2Max()       const { return Rd32(13); }
	int32  Bar2Cur()       const { return Rd32(17); }
	// 6509 `SOULMATE_GAUGE03` (`PartyMark_Helper1@0xE64060`, game.c:471754-471757):
	//   доля = *((i16*)win+690) / *((i16*)win+689) = rec[23..24] / rec[21..22].
	int32  Bar3Max()       const { return Rd16(21); }
	int32  Bar3Cur()       const { return Rd16(23); }
	// Текущий опыт Дружбы. `unitFrame_Refresh` отдаёт его в `uiProgressBar_RefreshPercent`
	//   КАК QWORD с `CSoulmateWnd+1399` (1399 = 1357 + 42), то есть rec[42..49] — ОДНО 64-битное
	//   число. Имя `iSoulMateIndex` у первых четырёх байт — миснейм Hex-Rays: полю присваивают
	//   `*(int*)((char*)&iSoulMateIndex + 3)`, что уже само по себе признак неверной разметки.
	int64  FriendExp()     const
	{
		uint64 V = 0;
		for (int32 i = 7; i >= 0; --i) { V = (V << 8) | (uint64)B[42 + i]; }
		return (int64)V;
	}
	// Атрибут i (0..5) — i16 rec[51 + 2i]. Эталон: `*((__int16 *)this + 704 + i)`,
	//   704*2 = 1408 = 1357 + 51 (`unitFrame_Refresh`, game.c:471546-471551).
	int32  Attr(int32 I)   const { return (I >= 0 && I < 6) ? Rd16(51 + 2 * I) : 0; }

	// ★ R5-fix-c (13.11.2026): i16/i32 записи по смещению O — общими чтениями `AsdaBytes` (были свои дословные копии тех же выражений).
	//   Границы не проверяются, как и раньше: вызывающие передают смещения внутри `kSize`. (Число строк файла сохранено.)
	int32 Rd16(int32 O) const { return AsdaBytes::RdI16(B + O); }
	int32 Rd32(int32 O) const { return AsdaBytes::RdI32(B + O); }
};

// Состояние вкладки поверх записи (то, что живёт вне пакета).
struct FAsdaSoulmateCard
{
	bool    bHasSoulmate = false;   // gap_8B4[6] == 1: ДРУГ В ИГРЕ (запись @25, CUser+99020), а НЕ «пара есть» — пара = SlotIndex != −1 (gap_878[47])
	int32   SlotIndex    = -1;      // номер слота соулмейта (для 5204)
	FString PartnerName;            // имя партнёра (из записи либо из 5203)
	uint8   Modes[4]     = { 0, 0, 0, 0 };  // галки 6543..6546 / SOUL_CHECK00..03
	// ★★★ Имя цели МОЕГО исходящего предложения. Эталон кладёт его в `&win[3].field_8 + 1`
	//   (`coupleSendProposalReq@0xE658E0`) и подставляет в нотисы 5201 кодов 4..7
	//   (`coupleRecvProposalNotice@0xE65BB0`). Без него эти четыре строки печатать нечем.
	FString OutgoingName;
};

// ★★★ ЗАПИСЬ КАНДИДАТА ПОИСКА — структура эталона, собираемая `soulmateParseSearchResult@0xE73E00`.
//   ⚠ ПОПРАВКА К ОТЧЁТУ §8 D8: «в список кладутся только userNum и charId» — НЕВЕРНО. Локальные
//     `Buffer/v8/v9/v10/v11` Hex-Rays показал по отдельности, но это СОСЕДНИЕ ПОЛЯ ОДНОЙ структуры
//     на стеке: смещения +9/+41/+73/+93/+125/+328 сходятся с `findSoulmateWin_Init@0xE740E0`
//     (memset-ы) и с чтениями в `soulmateWin_RefreshList@0xE72EE0`. Строки НЕ теряются.
struct FAsdaSoulmateCandidate
{
	int32   UserNum = -1;    // +0  i16 — rec[24..25] (−1 = конец списка)
	int32   CharId  = -1;    // +4  i32 — rec[26..29] (−1 = конец списка)
	uint8   Members = 0;     // +8  u8  — rec[30]
	FString SexText;         // +9   char[32] — loc (11701 + rec[23])
	FString LevelText;       // +41  char[32] — «%d» от rec[0]
	FString Name;            // +73  char[20] — rec[3..22]
	FString JobText;         // +93  char[32] — loc (12300 + rec[1])
	FString Intro;           // +125 char[203] — rec[33 .. 33+rec[31]]
	int32   ConstFlag = 0;   // +328 i32 — rec[32] (созвездие/совместимость)
	uint8   Sex = 0;         // rec[23] — по нему же фильтр окна (радио 8203/8204/8205)

	// ── Колонки 7 и 8 строки (T9). Считаются при разборе 5484, чтобы `RefreshSearchRows`
	//   оставалась чистой отрисовкой (эталон считает их в самой отрисовке, но там таблицы
	//   лежат в статике, а у нас подсистема созвездий резолвится через GameInstance). ──
	FString ConstName;       // 8275 `pList_Constellation` ← `cfgFindRecordByKey65(rec[32])`
	FString MatchText;       // 8276 `pList_ConsteMatch`   ← `uiFormatPartyRelationNotice`
	int32   MatchIcon = -1;  // ВОЗВРАТ той же функции: 0/1/2 = какая из 3 картинок горит; −1 = нет данных
};

// ★ 24.10.2026 (агент sm-offline, P79): СВЕДЕНИЯ О ДРУГЕ ВНЕ ИГРЫ — из ответа 2243 (запрос 2244). Серверная половина — P79-SQL
//   (`AstoryServer/Docs/DBManager/soulmate-offline-friend-p79.md`): процедура `UPCharSoulMateInactive` после стоковых строк прежних
//   Друзей дописывает две строки 27 б активной пары: SmLvl 254 — персонаж, которым Друг вышел последним (Lvl, Job = Subjob ?: Job,
//   Charname); SmLvl 253 — Lvl = карта, Job = канал, Charname = время последнего выхода UTC «yyyyMMddHHmmss» ('' — неизвестно).
struct FAsdaOfflineFriend
{
	bool     bReplied  = false;  // ответ 2243 на эту пару пришёл (пусть и без строк Друга — старый сервер / у Друга нет персонажей)
	bool     bValid    = false;  // строка 254 (персонаж) найдена
	int32    Smidx     = -1;     // пара, к которой относятся сведения (= свой слот `Card.SlotIndex` на момент ответа)
	uint8    Level     = 0;      // 6521 «%d <12215>»
	uint8    Grade     = 0;      // 6556 — `FAsdaCharRecord::ClassName(Grade)` (то же правило, что rec[41] ?: rec[40] у 5110)
	FString  Name;               // 6520
	int32    Map       = 255;    // 6523 «Локация» — карта (≥ 101 — поле пусто, как у эталона)
	int32    Channel   = 0;      // 6523 — канал
	bool     bHasTime  = false;  // 6522 — время последнего выхода известно
	FDateTime LogoutUtc;         // 6522 — время последнего выхода (UTC)
};

// Данные входящего предложения Дружбы (5201 код 0) — нужны, чтобы ответить 5202.
struct FAsdaSoulmateProposal
{
	bool    bPending      = false;
	int32   InitCharId    = -1;   // charId инициатора (из тела 5201)
	int32   InitUserNum   = -1;   // userNum инициатора
	uint8   B7 = 0, B8 = 0, B9 = 0;
	uint8   Channel       = 0;    // [10] — уходит в 5202 ТОЛЬКО в ветке «принять»
	uint8   SkillType     = 0;    // [31]
	FString InitName;             // [11..30]
	int32   Level = 0;            // [32..33] i16 — слот числа в loc 20272
	uint8   Sex = 0;              // [34] — строка loc (20272 + пол)
	uint8   Job = 0;              // [35] — строка loc (12300 + профессия)
	FString Intro;                // [36..163] самопредставление (128 б)
};

// ★ 26.10.2026 (агент smmessage): ЗАПИСКА ДРУГУ — элемент очереди `CSoulmateWnd+2044` (`this+511`, deque записей 93 б) эталона.
//   Раскладка записи 93 б (`sub_91ED43`; её же несёт 6055 и собирает из 6053 `guildHandleEventNotify@0xE6C840`):
//   @0 i32 charId ПОЛУЧАТЕЛЯ · @4 i32 номер записки (`SoulMateNote.idx`, уходит в 6054) · @8 char[65] текст (cp1251) ·
//   @73..@92 пять i32 — год, месяц, день, час, минута (в loc 12177 слоты [sd1]..[sd5]). Разбор потока — шапка `AsdaSoulmateNote.cpp`.
struct FAsdaSoulNote
{
	int32   CharId = -1;      // @0  charId получателя (гейт «это мне» — свой charId)
	int32   NoteId = -1;      // @4  номер записки в БД — подтверждение 6054 удаляет её (`UPSoulMateNoteDel`)
	FString Text;             // @8  текст записки (char[65], cp1251 — поле хранится в БД varchar(65))
	int32   Stamp[5] = { 0, 0, 0, 0, 0 };   // @73..@92 год, месяц, день, час, минута
};

UCLASS()
class ASDAREBORN_API UAsdaSoulmateSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── FTickableGameObject (С-3 ревизии расширения окон, 07.10.2026): наведение и выбор строки окна 8200 ──
	//   [ОРИГИНАЛ] `uiUserListWndProc@0xE72460`: событие 0xB (нажатие ЛКМ на панели строки 8208+7·k) →
	//   `partyInvite_OnListSelect@0xE72DD0`; события 8/0xA (курсор вошёл/ушёл) → `uiSelectTabPage_2D10@0xE72D10`.
	//   Клик и наведение ловим ПО ЖИВОЙ ГЕОМЕТРИИ строки (как `TickRowHover` окна 8500): кнопок строк в
	//   разметке нет, панель строки — картинка-план (закон `jui-cell-chain-nesting`).
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	// ★ М-3 ревизии `widen_fixes_REVIEW.md` (07.10.2026): тикаем, только пока окно 8200 ВИДНО. Было «пока виджет
	//   построен» — опрос мыши шёл всю сессию после первого открытия. Кадр мыши после показа сбрасывает `Tick`
	//   (разрыв в номерах кадров `RowPollFrame`), поэтому отдельного тика «на скрытие» не нужно.
	// ★ 23.10.2026 (панель Друга 6500): тикаем ещё и пока панель Друга ДОЛЖНА быть на экране (`bPopWanted`) или
	//   висит её подсказка — опрос мыши панели и её возврат после смены карты (виджет умирает вместе с миром).
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSoulmateSubsystem, STATGROUP_Tickables); }

	// ── IAsdaSessionResettable (К-3) ──
	// Находка Р5 отчёта 14_soulmate: карточка пары, заявка и защёлки жили от ПРОШЛОГО персонажа.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Вкладка 0 показана/скрыта (зовёт контейнер UAsdaCommunitySubsystem). Привязывает контролы и рисует.
	void OnTabActivated(bool bVisible, UUserWidget* CommunityWindow);

	// ── ★ Волна В4 ремесла (ревизия В3 С-2): поп-карточка 6500 в «закрыть всё» и в снимке окон (слот 11) ──
	// Предикат снимка `sub_9344D6(g_pSoulmateWnd)` — видна ли поп-карточка 6500.
	bool IsPopCardShown() const;
	// Закрыватель `uiCloseAllWindows` — `inventoryHidePanelsAndRefresh@0xE65510`: погасить поп-карточку.
	void HidePopCardForCloseAll();
	// ★ Ревизия 1-Б С-1: ПОЛНЫЙ порт `inventoryHidePanelsAndRefresh@0xE65510` — погасить поп-карточку 6500 и
	//   передать яблочную часть (`UAsdaSoulAppleSubsystem::OnHidePanelsAndRefresh`) с признаком пары `gap_8B4[6]`.
	//   Зовут ВСЕ места эталона: «закрыть всё», снятие галки 6546, разрыв пары (5207 коды 1/2/3), уход Друга (5226).
	void HidePanelsAndRefresh(const TCHAR* Why);
	// Признак пары `gap_8B4[6]` (`Card.bHasSoulmate`). ⚠ НЕ путать с `UAsdaSkillUseSubsystem::HasSoulmate` (gap_878[47]).
	bool HasSoulmatePair() const { return Card.bHasSoulmate; }
	// ★ 06.11.2026 (Камень души): номер пары `gap_878[47]` (меньше 0 — пары нет) и userNum Друга `gap_8B4[4]`.
	int32 GetSoulSlotIndex() const { return Card.SlotIndex; }
	int32 GetRecPartnerUserNum() const { return Rec.PartnerUserNum(); }
	// Самотест Камня души: userNum Друга в записи.
	void DebugSetRecPartner(int32 UserNum) { Rec.B[8] = (uint8)(UserNum & 0xFF); Rec.B[9] = (uint8)((UserNum >> 8) & 0xFF); }
	// ★ 12.10.2026 [ОРИГИНАЛ] (канал чата «Друг», режим 8): имя партнёра — адресат 5088 вида 1. Эталон берёт его из записи
	//   соулмейта `sub_93859A(g_pSoulmateWnd) + 63` (`ChatInput_SendWhisperReply@0xBA4020`, chat.c:2303-2311); у порта — карточка
	//   (`RecName()` после 5110, имя из 5203). Пустое — отказ loc 6929 (chat.c:2365-2369).
	const FString& GetPartnerName() const { return Card.PartnerName; }
	// ★ 12.10.2026: только самотест `asda.Chat.ChannelGateSelfTest` — выставить/вернуть слот, признак пары и имя партнёра
	//   (поля карточки, которые читает гейт канала «Друг»), без перерисовки вкладки и без сети.
	void DebugSetChatBond(int32 Slot, bool bPair, const FString& Name) { Card.SlotIndex = Slot; Card.bHasSoulmate = bPair; Card.PartnerName = Name; }
	// Восстановление слота 11 — `sub_92CBC8` = `sub_E65450@0xE65450`: показать, если стоит галка 6546.
	void ShowPopCardFromSnapshot();

	// ══════ ★★★ ПАНЕЛЬ ДРУГА 6500 «SM bar» (23.10.2026, жалоба владельца «SM bar is not showing up») ══════
	//   Реализация — `Private/UI/AsdaSoulmatePopBar.cpp` (шапка там: поток, адреса, нотисы, ключи).
	// [ОРИГИНАЛ] Своя запись 5220 (101 б: @0 charId, @4 userIdx, @6 блок 95 б = CUser+99001…) — ветка «свой»
	//   `Recv_PartyMinimapMark@0xE641A0` (миснейм): галки 6546/6543..6545 ← @33/@36..38, «Друг в игре» ← @25, слот ← @6,
	//   Друг ← @19 (charId) / @23 (userIdx); при @33 && @25 — нотис 110103 (если панель скрыта) и показ панели.
	void OnSelfSoulInfo5220(const TArray<uint8>& Rec101);
	// [ОРИГИНАЛ] 5112 (168 б = 28 × 6) — эффекты Друга в запись +83 (`buffUiLoadAndRefreshIcons@0xE65390`) и значки 33000…33027.
	void OnPartnerBuffs5112(const TArray<uint8>& Body168);
	// Своя запись 5220 ЦЕЛИКОМ (101 б, как пришла; пусто — ещё не приходила). Для волны вкладки соулмейта (уровень/опыт Дружбы).
	const TArray<uint8>& GetSelfSoulInfo5220() const { return SelfInfo5220; }
	// Галка режима i (0..2 = 6543..6545, 3 = 6546 «окно сведений») — `CSoulmateWnd+1854..1856` / `+1853`.
	uint8 GetMode(int32 I) const { return (I >= 0 && I < 4) ? Card.Modes[I] : 0; }
	// Серверный признак «показывать панель» `CCharacter::gap_8BE[4]` (= 5220 @33) — гейт хвоста `unitFrame_Refresh`.
	bool GetPopServerOption() const { return bPopSrvOpt; }
	// Друг: userIdx (`gap_8B4[4]`) и charId (`gap_8B4[0..3]`) — последние из 5110/5111/5220; −1 — неизвестен.
	int32 GetMateUserIdx() const { return MateUserIdxLive; }
	int32 GetMateCharId() const { return MateCharIdLive; }
	// Виджет панели 6500 (раскладка мини-карты — панель занятости под ним; самотесты — геометрия, узлы); nullptr — не поднята.
	UUserWidget* GetPopWindow() const { return PopWnd; }
	// ★ [ВЛАДЕЛЕЦ] ЛКМ по панели: Друг становится целью (общая точка `AAsdaPlayerController::SetTarget`). true — цель поставлена.
	bool SelectPartnerAsTarget(const TCHAR* Why);
	// Значков эффектов Друга на панели видно / id в слоте d (порядок отрисовки эталона; −1 — пусто).
	int32 GetPopBuffShownCount() const;
	int32 GetPopBuffSlotId(int32 Dst) const { return (Dst >= 0 && Dst < kPopBuffs) ? PopBuffId[Dst] : -1; }
	// Индекс зажжённого значка класса (0 Tender, 1 Sword, 2 Bow, 3 Staff; −1 — ни одного).
	int32 GetPopClassIconIndex() const { return PopClassIdx; }
	// Контрол, чья подсказка панели показана сейчас (6501/6504/6505/33000…33027); −1 — нет.
	int32 GetPopTipControl() const { return bPopTipShown ? PopTipId : -1; }
	// Хотел ли порт панель на экране (восстанавливается после смены карты).
	bool IsPopWanted() const { return bPopWanted; }
	// След нотисов панели (самотест): счётчик и последний loc (110103 из 5220, 8026 галки 6546, 20308 клика по панели).
	int32 GetPopNoticeSeq() const { return PopNoticeSeq; }
	int32 GetPopLastNoticeLoc() const { return PopLastNoticeLoc; }

	// ══════ ★ 26.10.2026 [ИСПРАВЛЕНИЕ сервера] «ЭХО СЕБЯ» вместо Друга (баг Player17: панель «Soulmate» с СВОИМ именем/ур. 70) ══════
	//   Реализация и разбор — `Private/UI/AsdaSoulmatePopBar.cpp` (раздел «ЭХО СЕБЯ»), ключ `asda.Soulmate.SelfEchoGuard`.
	// Пакет 5110 — эхо СВОИХ данных: rec[4] (charId «Друга» = отправитель) равен своему charId (Друг — всегда другая учётка).
	bool IsSelfEcho5110(const TArray<uint8>& Body) const;
	// Поток Друга (5111/5112/5227) принимать: ключ выключен, ИЛИ эхо в этой сессии не замечено и запись хранит НАСТОЯЩЕГО Друга.
	bool IsPartnerFeedTrusted() const;
	// Эхо замечено: Друг на деле не в игре (устаревший CharPos) — тихо, без нотиса, привести вкладку/панель к виду «Друг вне игры».
	void EnterSelfEchoOffline(const TCHAR* Why);
	// Самотест: эхо замечено в этой сессии / сколько пакетов эха отброшено.
	bool IsSelfEchoSeen() const { return bSelfEchoSeen; }
	int32 GetSelfEchoDropCount() const { return SelfEchoDrops; }

	// ── Действия кнопок вкладки (порт `SoulmateUI_HandleButtonCommand@0xE69F90`) ──
	void ActionMessage();     // 6583 Soul_MessageBtn — окно сообщения Другу (заметка 6052)
	void ActionOldSoul();     // 6584 Soul_OldSoul_Btn — сведения о прошлом Друге (5204... нет: запрос)
	void ActionCutoff();      // 6587 Soul_CutoffBtn — разорвать Дружбу (5204)
	void ActionFindNew();     // 6588 Soul_Find_Btn — ТОЛЬКО запрос 5483 (окно 8200 поднимет ответ сервера 5485)
	void SendNote();                // 6603 Soul_Msg_OkBtn — заметка Другу (6052), длина 1..64
	void HideNotePanel();           // 6604 Soul_Msg_ClBtn — отмена ввода
	void ResetSkillTrayAndHotbar(); // ветка 5207 код 3: лоток 12 слотов + слоты хотбара типа 4

	// ── Окна-спутники ──
	// Окно поиска кандидатов 8200 `Find_Soulmate_Win`: 5483 → 5484 (список, может прийти НЕСКОЛЬКИМИ пакетами) → 5485 (показ).
	// [ОРИГИНАЛ] Показ окна — порт `uiFindSoulmateWinOnShow@0xE72A70`: гейты (элитный магазин 6000 / окна ремесла) — МОЛЧА,
	//   затем показ, счётчики страниц, снятие выбора строки. Сам запроса НЕ шлёт (его шлют 6588, доп-меню, кнопки окна).
	void OpenSearchWindow();
	void CloseSearchWindow();        // `cFindSoulmateWinHide@0xE72BD0` (8267/8299/доп-меню при видимом окне)
	bool IsSearchWindowShown() const;   // `sub_938F68` = `*(BYTE*)(win+292)` — окно 8200 видно
	void SendSearch();               // `netSendMaskedPacket_op5483@0xE737D0`: [0] фильтр +496, [1] страница запроса +488; затем список очищается
	// [ОРИГИНАЛ] Доп-меню ETC 74600, команда 1 (`uiOpenWindowDispatch@0xECD570` case 1): окно 8200 видно → спрятать; иначе 5483.
	void ToggleSearchFromExtraMenu();
	void SetSexFilter(int32 Mode);   // позиция контрола 8202: 0 все / 1 / 2 (подписи 8203/8204/8205)
	void ApplySexFilterVisual();     // только вид выбранной позиции (без запроса 5483)
	void SetPage(int32 Page);        // 8258 первая / 8259 назад / 8260 вперёд / 8261 последняя
	// Выбор строки 0..6 (−1 — снять). [ОРИГИНАЛ] `partyInvite_OnListSelect`: выбирается ЛЮБАЯ строка, и пустая тоже;
	//   зелёная рамка 8257 `Find_SMate_ListSel` переносится к строке. Снятие — `partyInvite_CloseList@0xE73480`.
	void SelectRow(int32 Row);
	void ProposeToSelected();        // 8266 «Предложить Дружбу» → 5200 по выбранной строке
	void WhisperToSelected();        // 8264 «Шепот»
	// ★ ЕДИНАЯ точка отправки 5200 (обе эталонные ветки собирают ОДИН payload и имеют ОДНИ гейты).
	bool SendProposal5200(const FString& TargetName);

	// Окно 8300 `My_Introduction_Win` — ввод текста «о себе» (кнопка 8270 окна поиска).
	void OpenIntroWindow();          // 8270 `Find_SMate_EditBtn`
	void CloseIntroWindow();         // 8305 `My_Intro_CanBtn` / 8399 — просто спрятать
	bool IsIntroEditFocused() const; // ★ 10.10.2026: у поля 8302 клавиатурный фокус (самотест ESC — `cFindSoulmateWinHide`)
	void SendIntroText();            // 8304 `My_Intro_OKBtn` → 6080 + запомнить текст
	void InviteSelectedToParty();    // 8265 `Find_SMate_PtyBtn` → `party_inviteSelectedEntry@0xE734C0`

	void OpenLinkWindow();    // 9100 SOULMATE_LINK_WIN — порт `editBoxShowAndFocus@0xE69B40` (режим 2, пустое поле, фокус)
	void CloseLinkWindow();   // порт `editBoxClearAndHide@0xE69C20` (режим 1, пустое поле, скрыть)
	// [ОРИГИНАЛ] 9104 «ОК» и Enter в поле 9101 (событие 67587) — порт `coupleSendProposalReqTarget@0xE655B0`:
	//   цель обязана быть В МИРЕ рядом (`soulmateUI_IsTargetInRange@0xE69CF0`), иначе МОЛЧА и окно остаётся. true — 5200 ушёл.
	bool SubmitLinkWindow();

	// ── ★ ВОЛНА 1-А (06.10.2026): кнопки NPC соулмейта (`CGameUI_HandleControlCommand`, AsdaStory_game.c:387981-388012) ──
	// Слот Друга своего персонажа — эталонный `CCharacter::gap_878[47]` (−1 — Друга нет). Его читают гейты 30023/30024/30025.
	int32 GetFriendSlot() const { return Card.SlotIndex; }
	// [ОРИГИНАЛ] 30023, Друг уже есть: loc 8505 в журнал чата И в плашку 221 (цвет −1 = белый, 10 000 мс), разговор НЕ закрывается.
	void NoticeFriendAlreadyExists8505() const;
	// [ОРИГИНАЛ] 30024 «принудительный разрыв»: нет слота или запрос разрыва в полёте → ONEBUT (0, 24) loc 6035;
	//   иначе TWOBUT (1, 8036) loc 8036 (`soulmateShowNotice8036Tooltip@0xE67E40`) → «Да» = 5204 (`OnSendItemUpgrade` case 0x1F64).
	void NpcForceBreak30024();
	// [ОРИГИНАЛ] 30025 «разрыв»: тот же гейт (6035); иначе TWOBUT (1, 29) loc 8030 → «Да» = 5204 (`OnSendItemUpgrade` case 0x1D).
	void NpcBreak30025();

	// ── Чтение состояния для самотеста `asda.Soul.SelfTestWave1A` (только чтение, не меняет состояние) ──
	uint8 GetLinkWinMode() const { return LinkWinMode; }              // `CSoulmateWnd+1900`: 1 обычный, 2 — открыто 9100
	UUserWidget* GetLinkWindowForCheck() const { return LinkWnd; }     // живой виджет 9100 (подписи, кнопки, поле)
	UUserWidget* GetSearchWindowForCheck() const { return FindWnd; }   // живой виджет 8200 (строки списка)
	int32 GetSearchCandidateNum() const { return Candidates.Num(); }  // записей списка поиска (эталонный вектор +500)
	int32 GetSearchReqPage() const { return SearchReqPage; }          // `+488` — страница, которую уйдёт 5483
	bool IsBreakRequestInFlight() const { return bBreakReqInFlight; } // `+1892` (`this+473`) — запрос разрыва в полёте
	void ShowProposalWindow();// 8400 SMate_ReqAnsr_Win — принять/отказать входящее предложение
	void CloseProposalWindow();

	// Тип значка уведомления на панели быстрых слотов: эталон `uiNoticeButtonDispatch` case 1.
	static constexpr int32 kNoticeTypeSoulmate = 1;

	// ★★★ УРОВЕНЬ ДРУЖБЫ (rec[50]). Эталон кладёт его в `HIBYTE(CCharacter::field_8A5)`
	//   (`soulmateUpdateInfo@0xE64C30` : `HIBYTE(field_8A5) = v19[1407]`, 1407 = 1357 + 50) и
	//   ИМЕННО ЭТИМ значением сравнивает требования навыков Дружбы `rec+42`
	//   (`skillHotbar_FindBestSkillSlot@0xE691A0`, `skillHotbarRefreshSlots@0xE69440`).
	//   Уровень ПЕРСОНАЖА там ни при чём — писателей у HIBYTE(field_8A5) ровно три, и все три
	//   пишут rec[50] (game.c:471335 / 472431 / 472533).
	// ★ 23.10.2026 [ОРИГИНАЛ]: значение — `CCharacter+0x8B3` (блок соулмейта +12), его пишут И 5110/5111 (rec[50]), И своя 5220
	//   (@18) — поэтому уровень известен и когда Друга нет в игре (дизасм `itemTooltip_RefreshControls` 0xE62987: `[eax+0x8B3]`).
	//   Ключ `asda.Soulmate.OfflineInfo 0` — прежнее (только rec[50] записи 5110/5111).
	int32 GetFriendLevel() const;

	// ── ★ 23.10.2026 волна «сведения Друга / share / точка на карте» (агент sm-info) ──
	// Порт `itemTooltip_RefreshControls@0xE62710` (миснейм — это перерисовка ВКЛАДКИ соулмейта по своему блоку):
	//   нет пары → всё пусто; Друг вне игры → 6548 «Вне игры» (8054), имя/уровень/класс/локация пустые; всегда — уровень и
	//   доля опыта Дружбы из своего блока, строки и галки «поделиться», галка 6546; своя одержимость (блок +26 == 2) —
	//   статус пуст, галки погашены. Зовут 5220 / 5226 / 5207 и показ вкладки.
	void RefreshSelfBlockControls(const TCHAR* Why);
	// Отметка Друга на мини-карте и большой карте (`minimapDrawTargetMarker@0xB56BB0`, текстура miniCouplePos):
	//   пересобрать список `EAsdaMapMark::Couple` по текущему состоянию. bDeferred — ещё раз на следующем кадре
	//   (после сброса сессии мини-карта чистит ВСЕ отметки — порядок сбросов подсистем не задан).
	void PushCoupleMark(bool bDeferred);
	// Самотест: одна ли сейчас отметка Couple и её данные (false — отметки нет).
	bool GetCoupleMarkForCheck(int32& OutTileX, int32& OutTileZ, int32& OutMap) const;
	// Самотест: карта/канал Друга из своего блока (+22/+23; −1 — неизвестно) и последний текст поля 6523.
	int32 GetPartnerMapForCheck() const { return PartnerMapBlk; }
	int32 GetPartnerChannelForCheck() const { return PartnerChannelBlk; }
	// Самотест/гейт: заблокированы ли галки «поделиться» (своя одержимость).
	bool IsShareLocked() const;
	// Самотест: сколько раз 5221 НЕ ушёл из-за неизвестного состояния сервера / блокировки (счётчик с начала сессии).
	int32 GetShareSendRefusedCount() const { return ShareSendRefused; }

	// ── ★ 24.10.2026 (sm-offline, P79) сведения о Друге вне игры (2244 → 2243) и окно прежних Друзей 6640 ──
	// Самотест: сколько запросов 2244 ушло (кнопка 6584 + автозапросы) и сколько из них — автозапросы.
	int32 GetOldSoulReqSentCount() const { return OldSoulReqSent; }
	int32 GetOfflineAutoReqSentCount() const { return OfflineAutoReqSent; }
	// Самотест: стоит ли отложенный автозапрос (после 5226 или из-за предела частоты).
	bool IsOfflineReqScheduled() const { return OffReqTicker.IsValid(); }
	// Самотест: сведения о Друге вне игры (последний разобранный ответ 2243).
	const FAsdaOfflineFriend& GetOfflineFriendForCheck() const { return OffFriend; }
	// ★ 24.10.2026 (ревизия сведений о Друге вне сети) самотест: ждём ли ответ на кнопку 6584 (по нему встанет окно 6640).
	bool IsOldSoulGridShowPending() const { return bGridShowPending; }
	// ★ 24.10.2026 самотест: идёт проверка «ответ после 5226 свежее выхода» (0 — нет, 1 — ждём ответ, 2 — повтор уже потрачен).
	int32 GetOfflineLeaveCheckState() const { return !bOffLeaveCheck ? 0 : (bOffLeaveRetryUsed ? 2 : 1); }
	// ★ 24.10.2026 самотест: сведения помечены устаревшими (поля вкладки пусты до нового ответа).
	bool IsOfflineInfoStale() const { return bOffStale; }
	// Самотест: под-окно 6640 `OldSoulmate_Win` показано (узел `OldSoulmate_Win_grp` окна 6400).
	bool IsOldSoulGridShown() const;
	// 6642 `OldSoulmate_CloseBtn` — порт `SoulmateUI_HandleButtonCommand` case 0x19F2: `SetVisible(this+78, 0)`.
	void CloseOldSoulGrid();

	// ══════ ★★★ 26.10.2026 СООБЩЕНИЕ ДРУГУ (агент smmessage; реализация и разбор — `Private/UI/AsdaSoulmateNote.cpp`) ══════
	//   Отправка 6052 (кнопка 6603 окна 6605), доставка/итог 6053, непрочитанные при входе и портале 6055, значок `mes_tag1`
	//   (тип 9) на панели уведомлений, окна-свитки 6610/6620/6630 `Soul_RMsgWin*`, подтверждение прочтения 6054.
	static constexpr int32 kNoticeTypeNote = 9;   // `statusBarAddIcon(g_pQuickSlotBar, 9)` / `uiNoticeButtonDispatch` case 9
	static constexpr int32 kNoteWinCount  = 3;    // окна-свитки 6610 / 6620 / 6630 (`m_aRMsg[5*i]`, i = 0..2)
	// [ОРИГИНАЛ] 6053 — `guildHandleEventNotify@0xE6C840` (МИСНЕЙМ): тело 100 б, @10 вид (1 — записка МНЕ: в очередь + значок 9 +
	//   loc 12181; 3 — «отправлено» 12178; 2 — «больше трёх» 12179; 0 — «не удалось» 12180).
	void OnNote6053(const TArray<uint8>& Body);
	// [ОРИГИНАЛ] 6055 — `partyMemberSyncIcons@0xE6CB70` (МИСНЕЙМ): 279 б = 3 записи × 93 б — непрочитанные записки (сервер шлёт при
	//   CharMake и CharPortalMake). Первая запись моя → снять до трёх старых из очереди; каждая моя — в очередь и значок 9.
	void OnNotes6055(const TArray<uint8>& Body);
	// [ОРИГИНАЛ] Клик по значку 9 — `itemQueuePopAndShowTooltip@0xE6AD60` (МИСНЕЙМ): взять первую записку, показать в первом
	//   свободном окне 6610 → 6620 → 6630 (все заняты — в 6630), время loc 12177, затем 6054 с её номером. false — очередь пуста.
	bool OpenNextNote();
	// [ОРИГИНАЛ] 6612 / 6622 / 6632 `Soul_RMsg_CloseBtn*` — `SetVisible(окно i, 0)` (social.c:8240-8265). Index 0..2.
	void CloseNoteWindow(int32 Index);
	// ── чтение для самотеста `asda.Soulmate.NoteSelfTest` ──
	int32 GetNoteQueueNum() const { return NoteQueue.Num(); }                 // записок в очереди (deque `this+511`)
	int32 GetNoteIconsShown() const { return NoteIconsShown; }                // значков 9, выставленных подсистемой
	bool IsNoteWindowShown(int32 Index) const;                                // окно-свиток Index (0..2) видно
	UUserWidget* GetNoteWindowForCheck(int32 Index) const;                    // живой виджет окна-свитка (тексты, кнопка)
	bool IsNotePanelShown() const;                                            // окно ввода 6605 `Soul_MsgWin` видно
	bool IsNoteInputFocused() const;                                          // у поля 6602 `Soul_Msg_Input` фокус клавиатуры
	int32 GetNoteNoticeSeq() const { return NoteNoticeSeq; }                  // сколько нотисов записок напечатано
	int32 GetNoteLastNoticeLoc() const { return NoteLastNoticeLoc; }          // loc последнего из них
	int32 GetNoteConfirmSent() const { return NoteConfirmSent; }              // сколько 6054 ушло
	int32 GetNoteLastConfirmId() const { return NoteLastConfirmId; }          // номер записки в последнем 6054

	FString DumpState() const;

private:
	static const TCHAR* kLinkWbp;     // WBP_SOULMATE_LINK_WIN
	static const TCHAR* kReqAnsrWbp;  // WBP_SMate_ReqAnsr_Win
	static const FName  kLinkWindowId;
	static const FName  kReqAnsrWindowId;
	static const TCHAR* kFindWbp;     // WBP_Find_Soulmate_Win (8200)
	static const FName  kFindWindowId;
	static const TCHAR* kPopWbp;      // WBP_SOULMATE_POP_WIN (6500)
	static const FName  kPopWindowId;
	static const TCHAR* kIntroWbp;    // WBP_My_Introduction_Win (8300)
	static const FName  kIntroWindowId;
	static constexpr int32 kWinZ = 420;   // поверх окна сообщества
	// Поп-карточка — не диалог, а постоянная плашка HUD: она обязана лежать НИЖЕ окон.
	static constexpr int32 kPopZ = 40;

	// ★★★ «ПРИВЯЗАНА ЛИ ВКЛАДКА К ИМЕННО ЭТОМУ ВИДЖЕТУ ОКНА» (дефект ПОРТА, правка 02.09.2026).
	//   Окно 6400 ПЕРЕСОЗДАЁТСЯ: `UAsdaCommunitySubsystem::EnsureWindow` выбрасывает виджет и
	//   строит новый, как только `UAsdaWindowSubsystem::IsWindowLive@1749` вернул false
	//   (`Widget->GetWorld() == GI->GetWorld() && Widget->IsInViewport()`), то есть после КАЖДОЙ
	//   смены карты. Прежний гейт `if (!bTabBound || !CommWnd)` новый виджет отличить НЕ МОГ:
	//   `CommWnd` присваивается строкой выше, поэтому `!CommWnd` не выполнялось никогда, а
	//   `bTabBound` оставался взведённым от ПРЕДЫДУЩЕГО дерева. В итоге на новом дереве
	//   `TxtStatus`/`TxtName`/`TabGauge01` и реле кнопок указывали на уничтоженные виджеты,
	//   `ApplyStaticCaptions` не звалась — и на экране оставались ЗАГЛУШКИ ДИЗАЙНЕРА из разметки
	//   («53» на сердце, «Soulmaye Level», «123 Lv.», «53.4%», «Аяя-аая-аая»), а кнопки были мертвы.
	//   Признак привязки — СЛАБАЯ ссылка `BoundWnd` на тот виджет, на котором её ставили.
	bool IsBoundTo(const UUserWidget* W) const;

	// Привязка/отрисовка карточки на вкладке 0
	void BindTabControls(UUserWidget* CommunityWindow);
	void RefreshCard();
	// ★★★ ПОРТ СТАТИЧЕСКОЙ ЧАСТИ `CSoulmateWnd_InitControls@0xE5E440` (social.c:6121-6291,
	//   6377-6407). Эталон при построении окна пишет В КАЖДЫЙ контрол вкладки либо loc-строку,
	//   либо ПУСТУЮ строку `&Src`, либо «0» (сердце 6585). Порт этого не делал вовсе, поэтому на
	//   экран доживал текст-заглушка дизайнера, запечённый импортёром в `WBP_COMMUNITY_WIN`
	//   («Отправить записку», «Soulmaye Level», «53» на сердце, «Аяя-аая-аая», «share~»).
	//   Зовём при КАЖДОМ показе вкладки: контейнер `UAsdaCommunitySubsystem::ApplyTabVisibility`
	//   перед этим показывает ВСЁ поддерево, и заглушки всплывают обратно.
	void ApplyStaticCaptions();
	// ★★★ УЗЛЫ, СКРЫТЫЕ САМОЙ РАЗМЕТКОЙ (`Visible="0"`). Контейнер вкладки показывает поддерево
	//   ЦЕЛИКОМ (`AsdaSetSubtreeVisibility`), поэтому их надо гасить ПОСЛЕ общего показа —
	//   закон `jui-uneven-node-layout` («обратная грабля»). Список — из дампа разметки
	//   `Reports/soulmate_win6400_jui_dump.md`.
	void ApplyAlwaysHidden();
	// ★★★ НАШЕ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (`fix-original-defects`, ключ `asda.Soulmate.ShareBlockVisible`).
	//   В ОРИГИНАЛЕ блок «доступ к персонажу» (6579/6540..6542/SHARE/SOUL_CHECK00..02) скрыт
	//   разметкой (`Visible="0"`), а сервер три имени персонажей НИКОГДА не заполняет
	//   (`soulmate_share_feature_client.md` §8). Владелец потребовал ПОКАЗАТЬ и оживить его в меню.
	//   Имена берём из НАШЕГО списка персонажей (`UAsdaNetSubsystem::CharList`, 3 слота), состояние
	//   галок — из `Card.Modes` (приёма 5220 в порту нет — см. отчёт, «требуется от других»).
	void RefreshShareBlock();
	// Видимость узла блока И его поддерева по правилу принадлежности импортёра (`<idc>`, `<idc>_grp`,
	//   `<idc>_Text`, `<idc>_HIT`, `<idc>_<цифры>`). Показ — через `ShownVisibilityFor` (интерактиву
	//   ловящая мышь `Visible`, декору `SelfHitTestInvisible`); гашение — `Collapsed`.
	void SetBlockNodeVisible(const TCHAR* Idc, bool bVisible);
	// Клик по галке 6543/6544/6545 (share) или 6546 (окно сведений) → `Card.Modes` + 5221.
	//   `UCheckBox` сообщает СВОЁ новое состояние — берём его как истину (без внутреннего флипа).
	void OnShareCheckChanged(int32 Which, bool bChecked);
	void ShowNotePanel(bool bShow);         // 6605 `Soul_MsgWin` — показать/спрятать окно заметки
	UButton*      FindBtn(const TCHAR* Idc) const;
	UCheckBox*    FindCheck(const TCHAR* Idc) const;   // галка по точному idc (импортёр называет UCheckBox по idc)
	UTextBlock*   FindTxt(const TCHAR* Idc) const;
	FString       RecName() const;          // имя партнёра из записи (rec[63..], cp1251 до NUL)

	// Делегаты сети
	void OnCard(int32 Opcode, const TArray<uint8>& Body);   // 5110 (запись целиком) / 5111 (сдвиг 13)
	void OnPlayerMenuResult(int32 Opcode, const TArray<uint8>& Body);  // 5201
	void OnBond(int32 Subcode, int32 PairIdx, int32 InitiatorUidx, FString PartnerName);  // 5203
	void OnBreak(int32 Subcode, FString Name);           // 5207
	void OnPartnerLeft5226();                            // ★ ревизия 1-Б: 5226 `tradeWnd_resetAndNotify` (миснейм) — «Друг уходит»
	void OnBreakRefused(int32 Reason);                   // 5205 (только код — старый делегат)
	void OnBreakRequest(const TArray<uint8>& Body);      // 5205 ЦЕЛИКОМ (61 б) — ветка кода 0
	void OnSearch(const TArray<uint8>& Body);            // 5484 — ДОПИСЫВАЕТ записи (до 7), окно не трогает
	void OnSearchShow();                                 // 5485 — порт `findSoulmateWin_Show@0xE741B0` (тело 0 б)
	// Порт `Soulmate_SendBondRequest@0xE66F70`: гейт `!+473`, 5204 со СЛОТОМ на момент отправки, затем `+473 = +474 = 1`.
	//   Единая точка для 6587 (8044), 30024 (8036) и 30025 (29). Why — для журнала.
	void SendBreakRequest5204(const TCHAR* Why);
	// Порт `soulmateUI_IsTargetInRange@0xE69CF0`: персонаж с ТОЧНО таким именем в мире, не я, |Δтайла| ≤ 10 по X и по Z.
	bool IsLinkTargetInRange(const FString& Name) const;
	void OnNoticeIcon(int32 NoticeType);                 // клик по значку панели быстрых слотов
	void NotifyProposalIcon();                           // порт statusEffectAddIconAndNotify(…, 5055)
	void ClearProposalIcon();                            // снять значок после ответа

	void ParseProposal5201(const TArray<uint8>& Body);   // тело 5201 код 0 → FAsdaSoulmateProposal
	void ClearBond();                                    // полная очистка связи (ветки 5207 1/3)

	// Утилиты
	FString Loc(int32 Id) const;
	FString LocSlotStr(int32 Id, const FString& S) const;   // подстановка [ss1]
	FString LocSlotNum(int32 Id, int32 V) const;            // подстановка [sd1]
	// ★★★ ЦВЕТ СТРОК СОУЛМЕЙТА. Эталон печатает их `CChat_AddTokenizedLines(g_pChatWnd, buf,
	//   −8847556, 10000, 0)`; −8847556 = 0xFF78FF3C = (120,255,60). Раньше порт цвет терял.
	void ChatSocial(const FString& Text) const;          // канал Social, цвет −8847556, TTL 10000
	// Строка канала «подсказка» эталона: `CChat_AddTokenizedLines(..., −11514, 10000, 0)`
	//   (0xFFFFD306). Ей печатается отказ 6035 у кнопки 6583.
	void ChatTip(const FString& Text) const;
	// ★★★ Нотис-окно ТИПА 0 (`CItem_BuildTooltipText(…, 0, id, …, 1)`) — однокнопочный
	//   `ONEBUT_MSGBOX` 1500. Им эталон выводит 6035 (кнопка 6587), 8008/8009 и 20167/20168 (5203).
	//   EtalonNoticeId — id эталонного вызова для очереди окон-сообщений: 6035 эталон зовёт с id 24 (social.c:8295).
	void ShowNotice0(const FString& Text, int32 EtalonNoticeId = -1) const;
	void WhisperTo(const FString& Name) const;           // подставить «/w имя » в поле чата

	UPROPERTY(Transient) UUserWidget* CommWnd = nullptr;   // окно сообщества (владелец вкладки 0)

	/** ★★★ GC-ЯКОРЯ ОБРАБОТЧИКОВ КНОПОК ВКЛАДКИ 0 (регрессия 02.09.2026).
	 *  Динамический делегат `UButton::OnClicked` держит реле СЛАБО (`TWeakObjectPtr` внутри
	 *  `FScriptDelegate`), а `NewObject<…>(this)` внутренний объект НЕ ЯКОРИТ: без массива первый же
	 *  проход сборщика мусора убивает реле, и кнопки вкладки МОЛЧА перестают работать в произвольный
	 *  момент. Тот же приём — у `EditLimiters` вкладки «Клан» и у `Relays` 30+ окон порта.
	 *  Чистится в `BindTabControls` (реле мёртвого дерева держать нельзя). */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Relays;

	/** То же для кнопок ОТДЕЛЬНЫХ окон соулмейта (9100 связь, 8200 поиск, 8300 «о себе», 8400 запрос).
	 *  Отдельный массив, потому что эти окна живут дольше вкладки и НЕ пересоздаются вместе с 6400:
	 *  чистка `Relays` их реле убила бы. */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> PopupRelays;
	// Виджет окна 6400, на котором СТОИТ текущая привязка (кэш контролов, реле кнопок, подписи).
	//   Ссылка СЛАБАЯ намеренно: держать пересозданное окно живым нельзя, а «протухла» —
	//   законный ответ, по нему `IsBoundTo` и решает переприязаться. Сравнение через
	//   `TWeakObjectPtr` (а не по сырому адресу) ещё и защищает от переиспользования индекса
	//   объекта под новое окно — у такой записи меняется serial number, и `Get()` даёт nullptr.
	TWeakObjectPtr<UUserWidget> BoundWnd;
	// ★ Дедуп №2, В1-B (U22): `TObjectPtr` вместо сырых указателей — поле передаётся по ссылке в общий каркас
	//   `FAsdaJuiWindowHandle::EnsureLazy` (для UPROPERTY-поля это одно и то же: сборщик мусора видит оба вида).
	UPROPERTY(Transient) TObjectPtr<UUserWidget> LinkWnd = nullptr;   // 9100
	UPROPERTY(Transient) TObjectPtr<UUserWidget> ReqWnd  = nullptr;   // 8400
	UPROPERTY(Transient) TObjectPtr<UUserWidget> FindWnd = nullptr;   // 8200
	UPROPERTY(Transient) TObjectPtr<UUserWidget> PopWnd  = nullptr;   // 6500 SOULMATE_POP_WIN (T7)
	UPROPERTY(Transient) TObjectPtr<UUserWidget> IntroWnd = nullptr;  // 8300 My_Introduction_Win (R23)

	// Состояние окна поиска
	TArray<FAsdaSoulmateCandidate> Candidates;   // не более 7 (лимит эталона)
	int32 SearchPage   = 1;    // `+484` curPage — текущая страница (из заголовка 5484; её печатает 8262)
	int32 SearchReqPage = 1;   // `+488` — страница ЗАПРОСА, уходит байтом [1] пакета 5483 (8258..8261/8207 ставят её)
	int32 SearchLast   = 1;    // `+492` последняя страница (из заголовка 5484)
	int32 SexFilter    = 0;    // окно+496: 0 = все, 1/2 = пол
	int32 SelectedRow  = -1;   // `+480` выбранная строка (зелёная рамка 8257), −1 — нет
	int32 HoverRow     = -1;   // строка под курсором (синяя подсветка 8214+7·k), −1 — нет
	FAsdaMouseFrame RowMouse;  // кадр мыши для опроса строк окна 8200 (фронт нажатия ЛКМ)
	uint64 RowPollFrame = 0;   // `GFrameCounter` последнего опроса строк 8200; разрыв = окно было скрыто → сброс `RowMouse`
	// Восемь планов зелёной рамки 8257 `Find_SMate_ListSel_<обход>_<план>` (девятисрез без середины).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SelFramePlanes;
	// Авторские смещения планов рамки (рамка стоит у строки 1) — переносим ДЕЛЬТОЙ от них, без накопления.
	TMap<TWeakObjectPtr<UWidget>, FMargin> SelFrameBase;
	void  RefreshSearchRows();
	void  TickSearchRows();          // наведение + клик по строкам (зовёт `Tick`, пока окно 8200 видно)
	void  ApplySelFrame();           // показать/спрятать и перенести рамку 8257 к `SelectedRow`
	UWidget* RowPlate(int32 Row) const;   // панель строки 8208+7·k — картинка `Find_SMate_ListBase0N_<обход>_0`
	// Нажатие пришлось в «мёртвую» колонку строки (сердечки, созвездие/влечение) — только при `asda.Soul.RowClickFullWidth 0` (D17).
	bool  IsRowClickSwallowed(int32 Row, const FVector2D& Cursor) const;
	void  BindSearchControls();
	// ★★★ T9: колонки 8275/8276 у импортёра БЕЗ УЗЛА — достраиваем их в рантайме (см. .cpp).
	void  EnsureRowSpecialLabels();
	void  SetRowSpecialText(int32 Row, bool bMatch, const FString& S);
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RowConstTxt;   // 8275 + 2*row
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RowMatchTxt;   // 8276 + 2*row
	// ★ М-4 (07.10.2026): планы ячеек 8275/8276 (`Black` 120×17) — прямоугольник контрола эталона. По ним, а не по
	//   подписям (те сдвинуты выравниванием текста на 0.5 px), меряется «мёртвая» ячейка при `RowClickFullWidth 0`.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RowConstPlane;    // план ячейки 8275 + 2*row
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RowMatchPlane;    // план ячейки 8276 + 2*row

	// Кэш контролов карточки (вкладка 0)
	// ★★★ ПОДПИСИ В ОТЧЁТЕ §3.1 ВРУТ. Реальное назначение снято с `CSoulmateWnd_RefreshInfo`:
	//   6548 TXT01 = СТАТУС (всегда loc 8053) · 6520 TXT02 = ИМЯ · 6521 TXT03 = УРОВЕНЬ
	//   6556 TXT10 = ЗВАНИЕ · 6523 TXT05 = ПРОФЕССИЯ · 6557 TXT12 = уровень Дружбы.
	UPROPERTY(Transient) UTextBlock*  TxtStatus  = nullptr;   // 6548 SOUL_TXT01 — «В игре» (loc 8053)
	UPROPERTY(Transient) UTextBlock*  TxtName    = nullptr;   // 6520 SOUL_TXT02 — имя партнёра
	UPROPERTY(Transient) UTextBlock*  TxtLevel   = nullptr;   // 6521 SOUL_TXT03 — «%d <12215>»
	UPROPERTY(Transient) UTextBlock*  TxtGrade   = nullptr;   // 6556 SOUL_TXT10 — звание
	UPROPERTY(Transient) UTextBlock*  TxtJob     = nullptr;   // 6523 SOUL_TXT05 — профессия
	UPROPERTY(Transient) UTextBlock*  TxtFriendLv= nullptr;   // 6557 SOUL_TXT12 — уровень Дружбы
	// ⚠ Дефект оригинала D9: 6522 SOUL_TXT04, 6558 SOUL_TXT13 и полоса 6559 SOUL_GAUGE01 в эталоне
	//   заполняются ПУСТОЙ строкой в InitControls и больше не трогаются никогда (полосе задан лишь
	//   диапазон 0..100, SetValue не зовётся). Поэтому здесь их нет — выдумывать данные нельзя.

	// ══════ ПОП-КАРТОЧКА 6500 `SOULMATE_POP_WIN` (T7) ══════════════════════════════════════════
	// Состав снят `CSoulmateWnd_InitControls@0xE5E440` (game.c:470122-470162) и заполнение —
	//   `CSoulmateWnd_RefreshInfo@0xE63410` + `unitFrame_Refresh@0xE63810`.
	//   ★ ПОКАЗ/ГАШЕНИЕ доказаны: `sub_E65450` (= `sub_92CBC8`, game.c:472565-472580) показывает
	//     окно, только если стоит галка 6546 (`CSoulmateWnd+1853` = `Modes[3]`), и заодно гасит
	//     окно-призрак 9125; `inventoryHidePanelsAndRefresh@0xE65510` гасит его, когда пары нет.
	void EnsurePopWindow();     // поднять/привязать WBP_SOULMATE_POP_WIN
	void RefreshPopCard();      // порт `unitFrame_Refresh` + поп-часть `CSoulmateWnd_RefreshInfo`
	void ApplyPopVisibility();  // порт `sub_E65450` / `inventoryHidePanelsAndRefresh`
	// Доля опыта Дружбы в процентах — порт `uiProgressBar_RefreshPercent@0xE62E20` по `DT_smcharexp`.
	//   ⚠ НЕ const: как и эталон, СНИМАЕТ признак «уровень сменился» после первого показа.
	float FriendExpPercent();

	UPROPERTY(Transient) UTextBlock* PopTitle   = nullptr;   // 6503 SoulPop_Title    ← loc 8014
	UPROPERTY(Transient) UTextBlock* PopName    = nullptr;   // 6502 SOULMATE_TXT00   ← rec[63..]
	UPROPERTY(Transient) UTextBlock* PopLv00    = nullptr;   // 6501 SOULMATE_POP_LV00← «%d» rec[50]
	UPROPERTY(Transient) UTextBlock* PopLv01    = nullptr;   // 6505 SOULMATE_POP_LV01← «%d» rec[29]
	UPROPERTY(Transient) FAsdaMiniBarFill PopGauge00;        // 6504 SOULMATE_GAUGE00 — опыт Дружбы
	UPROPERTY(Transient) FAsdaMiniBarFill PopGauge02;        // 6508 SOULMATE_GAUGE02 — Soul_Red_Bar
	UPROPERTY(Transient) FAsdaMiniBarFill PopGauge03;        // 6509 SOULMATE_GAUGE03 — Soul_Blue_Bar
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> PopAtt;  // 6512..6517 SOULMATE_ATT00..05
	bool bPopBound = false;

	// ★★★ ПОЛОСА И ПРОЦЕНТ НА ВКЛАДКЕ (D9 ОПРОВЕРГНУТ, см. .cpp): 6559 `SOUL_GAUGE01` и
	//   6558 `SOUL_TXT13` заполняет `uiProgressBar_RefreshPercent@0xE62E20` (game.c:471345-471358).
	UPROPERTY(Transient) FAsdaMiniBarFill TabGauge01;        // 6559 SOUL_GAUGE01
	UPROPERTY(Transient) UTextBlock* TxtPercent = nullptr;   // 6558 SOUL_TXT13 ← «%.1lf %%»

	// ★★★ ЗАЩЁЛКИ ЭТАЛОНА (R22 + второй гейт кнопки 6587).
	//   `bBreakReqInFlight` = `CSoulmateWnd+1892` (`this+473`) — его и читает `sub_938F2C`,
	//     который на живом бинаре оказался `get_o1D9@0xD88930`: `return *((DWORD*)this + 473)`
	//     (доказано дизассемблером: `938F2C: jmp 0xD88930`). Значит второй гейт кнопки 6587 —
	//     «мой запрос разрыва уже в полёте».
	//   ★ ВОЛНА 1-А (06.10.2026): прежняя защёлка `bProposeInFlight` (R21) СНЯТА. Оба гейта 5200 эталона в нём МЕРТВЫ:
	//     `+1888` (`this+472`) никто в `CSoulmateWnd` в 1 не ставит (отправители и 5201 пишут 0), а `+1968`
	//     (`gap_C[132]`) отправитель ставит в 1 и ТУТ ЖЕ обнуляет вызовом `editBoxClearAndHide` (ui_core.c:72741-72743,
	//     73094). Порт же держал «предложение в полёте» до ответа 5201, а сервер на пустое имя не отвечает вовсе
	//     (`RequestUser_SoulMateCreate@0x4924F0`) — вечный тупик (закон `no-state-latch-before-shown`).
	bool bBreakReqInFlight = false;
	// ★★★ РЕЖИМ ОКНА ПРЕДЛОЖЕНИЯ = `CSoulmateWnd+1900` (байт). InitControls и
	//   `editBoxClearAndHide@0xE69C20` ставят 1, `editBoxShowAndFocus@0xE69B40` (открытие 9100) — 2.
	//   Он уходит в payload[0] пакета 5200 (сервер зовёт это поле `iSoulMateSkillType`,
	//   `RequestUser_SoulMateCreate@0x4924F0`) и возвращается в 5201 байтом [31]; кнопка 8409
	//   («Отказать») шлёт 5202 ТОЛЬКО при `+1900 == body[31]` (`Soulmate_SendBondAccept@0xE66790`).
	uint8 LinkWinMode = 1;

	// Текст «о себе» окна 8300 (эталонный буфер `CFindSoulmateWin+516`, 128 б).
	FString IntroText;
	// Данные ветки 5205 кода 0 — из них собирается ответ 5206.
	bool    bBreakAskPending = false;
	int32   BreakAskCharId   = -1;
	int32   BreakAskUserNum  = -1;
	uint8   BreakAskB7 = 0, BreakAskB8 = 0, BreakAskChannel = 0;
	FString BreakAskName;

	FAsdaSoulmateRecord   Rec;    // сырая запись (gap_54D эталона)
	FAsdaSoulmateCard     Card;
	FAsdaSoulmateProposal Proposal;
	// ★★★ «ЗАПИСЬ СОУЛМЕЙТА ХОТЬ РАЗ ПРИХОДИЛА». В эталоне `CSoulmateWnd_RefreshInfo@0xE63410`
	//   зовётся ТОЛЬКО из обработчиков пакетов 5110/5111 и из `CSoulmateWnd_ResetAndRefresh`
	//   (разрыв связи); переключение вкладки (`communityUI_SwitchTab@0xBD4040`, ветка 0) её НЕ
	//   зовёт — там только `CControl_SetVisible(soulWin, 1)`. Значит до первого пакета поля
	//   карточки держат ПУСТЫЕ строки, записанные при построении окна. Порт же звал `RefreshCard`
	//   на каждый показ вкладки и при нулевой записи печатал «0 Ур.» и класс 0 — то есть выдумывал
	//   данные несуществующего соулмейта. Признак закрывает эту дыру.
	bool bCardReceived = false;
	bool bTabBound = false;
	bool bNetBound = false;
	// `CSoulmateWnd+1972` (`this+493`): «уровень Дружбы сменился» — по нему
	//   `uiProgressBar_RefreshPercent` ОДИН раз обнуляет полосу опыта (game.c:471330-471337).
	bool bFriendLevelDirty = false;

	// Трёхпозиционный переключатель пола 8202 (T5): кнопки-опции, планы и подписи.
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    SexBtns;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     SexPlanes;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SexCaptions;
	TArray<FSlateBrush>   SexIdlePlanes;
	TArray<FButtonStyle>  SexIdleStyles;
	TArray<FLinearColor>  SexIdleCaptions;

	FDelegateHandle HCard, HBond, HBreak, HBreakRefused, HBreakReq, HSearch, HMenu, HNotice;
	FDelegateHandle HSearchShow;   // ★ волна 1-А: подписка на приём 5485 (`OnSoulmateSearchShow`)
	FDelegateHandle HPartnerLeft;  // ★ ревизия 1-Б: подписка на приём 5226 (`OnSoulmatePartnerLeft`)

	// ══════ ★★★ ПАНЕЛЬ ДРУГА 6500 (23.10.2026) — состояние и помощники (`AsdaSoulmatePopBar.cpp`) ══════
	static constexpr int32 kPopBuffs = 28;   // 28 значков эффектов Друга (33000…33027), 28 записей по 6 б в rec+83
	// `sub_E65450@0xE65450` (= `sub_92CBC8`): галка 6546 стоит → показать 6500 (окно-призрак 9125 оригинал не показывает никогда).
	void TryShowPop(const TCHAR* Why);
	// `buffUiRefreshIcons` (skill_buff.c:34560): значки эффектов Друга из rec+83 (вид 1 — stateinfotip, 3 — soulstateinfotip).
	void RefreshPopBuffs();
	// ★ [ИСПРАВЛЕНИЕ] значок класса Друга (`SoulPop_Tender/Sword/Bow/Staff`) — у оригинала узлы без IDNum, горит «меч» всегда.
	void RefreshPopClassIcon();
	// Опрос мыши панели (наведение → подсказки, ЛКМ → цель) и возврат панели после смены карты. Зовёт `Tick`.
	void TickPopCard();
	// Подсказка контрола панели (help-темы 16/17/23 или эффект Друга); `Dst` — слот эффекта (−1 — не значок).
	void ShowPopTip(int32 ControlId, int32 Dst);
	void HidePopTip();
	// Галки 6543..6546 вкладки — из `Card.Modes` (часть `itemTooltip_RefreshControls@0xE62710`, ветка 5220).
	void SyncShareChecksFromModes();
	// Узлы панели (значки эффектов в порядке отрисовки, значки класса) — после пересоздания окна.
	void BindPopExtraNodes();

	TArray<uint8> SelfInfo5220;          // своя запись 5220, 101 б как пришла (пусто — не приходила)

	// ── ★ 23.10.2026 (агент sm-info): поля СВОЕГО блока соулмейта `CCharacter+2215`, которые пишут несколько пакетов ──
	uint8 FriendLevelBlk = 0;            // +12 (`+0x8B3`): 5220 @18, 5110/5111 rec[50]
	int64 FriendExpBlk   = 0;            // +4..11 (`+0x8AB`, QWORD): 5220 @10..17, 5110/5111 rec[42..49]
	int32 PartnerMapBlk  = -1;           // +22 (`+0x8BD`, миснейм iSoulMateSkillType): 5220 @28, 5110 rec[38], 5227 [1]
	int32 PartnerChannelBlk = -1;        // +23 (`gap_8BE[0]`): 5220 @29, 5110 rec[10], 5227 [0]
	uint8 SelfConnectType = 0;           // +26 (`connecttype` = свой SoulState): 5220 @32; 2 — я одержим
	bool  bBlkLevelKnown = false;        // уровень/опыт Дружбы хоть раз пришли (5220 или 5110/5111)
	bool  bCouplePushQueued = false;     // отложенная (на кадр) пересборка отметки Couple уже стоит
	int32 ShareSendRefused = 0;          // 5221 не отправлен (состояние сервера неизвестно / одержимость)
	// 5227 (2 б: [0] канал, [1] карта) — Друг сменил карту/канал (`tradeWnd_updateNoticeAndTarget@0xE67B60`, миснейм).
	void OnPartnerMoved5227(const TArray<uint8>& Body);
	// Текст 6523 «Локация» — порт ветки `CSoulmateWnd_RefreshInfo` / `tradeWnd_updateNoticeAndTarget`.
	//   b5227 — источник `tradeWnd_updateNoticeAndTarget` (канал 6: " %s %s" с ведущим пробелом), иначе `CSoulmateWnd_RefreshInfo` ("%s %s").
	FString LocationText(int32 Map, int32 Chan, bool b5227 = false) const;
	// Имя строки «поделиться» i (0..2): из своей 5220 (@39/@59/@79, 20 б cp1251); пусто — запасной путь (список персонажей).
	bool ShareRowNameFrom5220(int32 I, FString& OutName) const;
	// ── ★ 24.10.2026 (sm-offline, P79): сведения о Друге вне игры и окно 6640 ──
	// Приём 2243 (`uiRefreshSlotGrid3x3@0xE6C2D0`): 243 б = 9 строк × 27 б (Smidx i32, SmLvl u8, Lvl u8, Job u8, Charname char[20]).
	void OnOldSoulGrid2243(const TArray<uint8>& Body);
	// Порт `uiRefreshSlotGrid3x3` — 3 группы × 3 персонажа под-окна 6640 (строки P79 активной пары отфильтрованы) и показ окна.
	void RenderOldSoulGrid(const TArray<uint8>& Body);
	// Статическая часть 6640 из `CSoulmateWnd_InitControls` (social.c:6447-6577): заголовок 12171, подписи групп 12014/11029/11030,
	//   пустые 6643..6645, скрытые узлы 7500.. (Name4..6/Lv/Job) и N*Time.
	void ApplyOldSoulGridStatic();
	// Автозапрос 2244 (не чаще раза в `asda.Soulmate.OfflineFriendReqIntervalSec`): сейчас или отложенно (MinDelaySec — нижняя граница).
	//   bTabShowing — зов из показа вкладки (`OnTabActivated(true)` идёт ДО `SetWindowVisible` окна, поэтому «окно открыто» не проверяем).
	void RequestOfflineFriendInfo(const TCHAR* Why, float MinDelaySec, bool bTabShowing = false);
	// Пара есть, Друг вне игры, не одержим, ключ включён, вкладка привязана; bRequireOpen — ещё и окно 6400 открыто на вкладке «Друг».
	bool CanAutoRequestOfflineFriend(bool bRequireOpen) const;
	bool SendOldSoulReq2244(bool bAuto, const TCHAR* Why);   // единая точка отправки 2244 (кнопка и автозапрос)
	void CancelOfflineReqTimer();
	// Заполнить 6520/6521/6556/6523/6522 сведениями `OffFriend` (true — заполнено; иначе поля пусты, как у эталона).
	bool FillOfflineFriendFields();
	FString OfflineLogoutText() const;          // 6522 — местное время игрока, формат по языку
	FAsdaOfflineFriend OffFriend;                // последний разобранный ответ 2243 (сведения о Друге вне игры)
	bool   bOffStale = true;                     // сведения устарели (5226 / 5110 / смена пары) — нужен новый запрос
	double LastOldSoulReqSec = -1.0e9;           // `FPlatformTime::Seconds` последнего 2244 (кнопка или автозапрос) — предел частоты
	bool   bGridShowPending = false;             // ждём ответ на кнопку 6584 — по нему показать 6640
	double GridShowPendingSec = 0.0;             // ★ 24.10.2026: `FPlatformTime::Seconds` нажатия 6584 — срок годности ожидания (ключ OldSoulGridReplyTtlSec)
	// ★ 24.10.2026: проверка свежести ответа после 5226 — запись выхода в БД может отстать от 5226 больше, чем на OfflineFriendAfterLeaveSec.
	bool   bOffLeaveCheck = false;               // 5226 был — ждём ответ, время выхода в котором не раньше 5226
	bool   bOffLeaveRetryUsed = false;           // единственный повторный 2244 этой проверки уже потрачен (без бесконечного опроса)
	FDateTime OffLeaveUtc;                       // UTC прихода 5226 (сверяется со временем выхода строки P79 253)
	bool   bAutoReqPending  = false;             // ждём ответ на автозапрос — 6640 не показывать
	FTSTicker::FDelegateHandle OffReqTicker;     // отложенный автозапрос (одноразовый)
	int32  OldSoulReqSent = 0;                   // 2244 ушло за сессию подсистемы (самотест)
	int32  OfflineAutoReqSent = 0;               // из них автозапросов (самотест)
	UPROPERTY(Transient) UTextBlock* TxtLogout = nullptr;   // 6522 SOUL_TXT04 — «Последний выход» (эталон не пишет)
	bool  bPopSrvOpt = false;            // `CCharacter::gap_8BE[4]` (5220 @33) — гейт хвоста `unitFrame_Refresh`
	int32 MateUserIdxLive = -1;          // `gap_8B4[4]` — userIdx Друга (5110/5111 rec[8], 5220 @23)
	int32 MateCharIdLive  = -1;          // `gap_8B4[0..3]` — charId Друга (5110/5111 rec[4], 5220 @19)
	bool  bPopWanted = false;            // порт показал панель и держит её (возврат после смены карты); гасит HidePanelsAndRefresh
	int32 PopBuffId[kPopBuffs];          // `pBuffWnd[258 + 2d]` — id эффекта в слоте отрисовки d (−1 пусто)
	int32 PopBuffSrc[kPopBuffs];         // запись rec+83, из которой взят слот d (для вида 1/3 подсказки)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> PopBuffIcons;   // слот d → `SOULMATE_SICON` (d чёт → 33000+d/2, нечёт → 33014+d/2)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> PopClassIcons;  // 0 Tender, 1 Sword, 2 Bow, 3 Staff
	TWeakObjectPtr<UUserWidget> PopExtraBoundWnd;   // на каком виджете 6500 разобраны узлы выше
	int32 PopClassIdx = -1;              // зажжённый значок класса (−1 — ни одного)
	FAsdaMouseFrame PopMouse;            // кадр мыши панели (фронт ЛКМ)
	uint64 PopPollFrame = 0;             // `GFrameCounter` прошлого опроса; разрыв — панель была скрыта → сброс `PopMouse`
	int32 PopHoverId = -1;               // контрол под курсором (6501/6504/6505/33000+…); −1 — нет
	int32 PopTipId = -1;                 // контрол, чья подсказка показана
	bool  bPopTipShown = false;          // наша подсказка (владелец `SoulPop`) на экране
	int32 PopNoticeSeq = 0;              // сколько нотисов панели напечатано (след самотеста)
	int32 PopLastNoticeLoc = -1;         // loc последнего из них
	bool  bSelfEchoSeen = false;         // ★ 26.10.2026: сервер прислал эхо своих данных как Друга (5110 rec[4] / 5220 @19 = свой charId)
	int32 SelfEchoDrops = 0;             // ★ 26.10.2026: отброшено пакетов эха (5110/5111/5112/5227) — след самотеста

	// ══════ ★ 26.10.2026 СООБЩЕНИЕ ДРУГУ — состояние и помощники (`AsdaSoulmateNote.cpp`) ══════
	// `deque_PushBack(this+511, запись)` + `statusBarAddIcon(…, 9)`: записку в хвост очереди, значок — тиком (`TickNotes`).
	void PushNote(const FAsdaSoulNote& N, const TCHAR* Why);
	// Разбор записи 93 б (6055 / собранной из 6053) с позиции Ofs.
	static FAsdaSoulNote ParseNote93(const uint8* P);
	// Покадровое: значки 9 = записки в очереди (панель уведомлений пересоздаётся на каждой смене карты); окна-свитки после смены карты.
	void TickNotes();
	// Окно-свиток i: поднять/привязать WBP (подпись 12176, кнопка закрытия). false — не загрузилось / нет менеджера окон.
	bool EnsureNoteWindow(int32 I);
	// Записать в окно i текст и время записки N и показать его поверх.
	void FillAndShowNoteWindow(int32 I, const FAsdaSoulNote& N);
	// Строка чата записок: `CChat_AddTokenizedLines(g_pChatWnd, loc, −11514, 10000, 0)` (жёлтый, 10 с).
	void NoteChat(int32 LocId);
	// [ОРИГИНАЛ] `CControl_SetFocus(+60, 1)` кнопки 6583 — фокус полю 6602 (образец окна 1580: SetUserFocus + страховка кадра).
	void FocusNoteInput();
	// Поле окна-свитка i по имени узла (`Soul_RMsgTxt`, `Soul_RMsgTimeTxt`, … + суффикс «2»/«3» у второго/третьего окна).
	static FString NoteNodeName(const TCHAR* Base, int32 I);
	UUserWidget* NoteWndAt(int32 I) const;           // виджет окна-свитка i (nullptr — не поднят)
	TObjectPtr<UUserWidget>& NoteWndRef(int32 I);     // поле окна-свитка i (для каркаса `EnsureLazy`)

	TArray<FAsdaSoulNote> NoteQueue;                  // очередь непрочитанных записок (`this+511`)
	int32 NoteIconsShown = 0;                          // значков 9, выставленных нами и ещё висящих (снимает клик)
	bool  bNoteIconsDirty = false;                     // число значков надо сверить с очередью на ближайшем тике
	FAsdaSoulNote NoteShown[kNoteWinCount];            // что показано в окне i (возврат окна после смены карты)
	bool  bNoteWinWanted[kNoteWinCount] = { false, false, false };   // окно i должно быть на экране (оригинал окон не пересоздаёт)
	UPROPERTY(Transient) TObjectPtr<UUserWidget> NoteWnd0 = nullptr;   // 6610 Soul_RMsgWin
	UPROPERTY(Transient) TObjectPtr<UUserWidget> NoteWnd1 = nullptr;   // 6620 Soul_RMsgWin2
	UPROPERTY(Transient) TObjectPtr<UUserWidget> NoteWnd2 = nullptr;   // 6630 Soul_RMsgWin3
	// GC-якоря реле кнопок закрытия окон-свитков (окна живут отдельно от вкладки 6400 — свой массив).
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> NoteRelays;
	int32 NoteNoticeSeq = 0;                           // нотисов записок напечатано (самотест)
	int32 NoteLastNoticeLoc = -1;                      // loc последнего
	int32 NoteConfirmSent = 0;                         // 6054 ушло (самотест)
	int32 NoteLastConfirmId = -1;                      // номер записки в последнем 6054
};
