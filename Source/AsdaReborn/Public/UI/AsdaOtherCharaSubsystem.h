// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaOtherCharaSubsystem — ОКНО «СВЕДЕНИЯ О ЧУЖОМ ПЕРСОНАЖЕ» `OTHERCHARA_DATA` (JUI IDNum **4200**).
//
// Разбор контролов — `Docs/UI/Character/OTHERCHARA_DATA.md` (89 контролов, 72 с логикой).
// Ассет порта — `/Game/AsdaAssets/UI/Player_UI/Character/WBP_OTHERCHARA_DATA` (160 узлов, дамп
// `Reports/headless_win_common_dump.log`). Окно НЕ перегенерируется и кодом НЕ правится.
//
// ═════════ 1. ПОТОК ЦЕЛИКОМ (эталон) ═════════════════════════════════════════════════════════
//   Контекст-меню по игроку, пункт 2006 (`ui_core.c:32206`, case 5) →
//   `otherCharaWnd_SetTargetUser@0xDA7640` (`AsdaStory_game.c:400971`):
//       if (!инициализировано) COtherCharaWnd_InitControls();          // @0xDA6C90
//       this[235] = CharManager_FindByUserIdx(g_CharManager, userIdx); // актор цели
//       if (this[235]) { this[238] = userIdx; this[40] = 0; antiCheat_sendItemFlagDigest(this); }
//   `antiCheat_sendItemFlagDigest@0xDA9000` собирает 31-байтный конверт «с секретом»
//   (8 б метка + 16 б MD5 + 7 б тело) и шлёт **5478** `OP_SEND_TARGET_CHAR_INFORMATION`,
//   после чего ставит `this[40] = 1` — «ждём ответ».
//   ★ В порту отправку 5478 уже делает `UAsdaPlayerMenuSubsystem::ActionCharInfo`
//     (`Net->SendTargetCharInfo5478`), второй копии отправки здесь НЕТ: окно получает цель
//     через `BeginRequest` и ставит только собственный признак ожидания (эталонное `win[40]`).
//
//   ОТВЕТ ПРИХОДИТ ДВУМЯ ПАКЕТАМИ, которые сервер шлёт подряд из ОДНОЙ функции
//   `Send_TargetChar_Information@0x549D10` (`GameServer.c:126101-126102`):
//       sub_4C28F0(…, **5479**, 121, …)   → `playerInfoWin_onRecvCharData@0xDA8680`
//       sub_4C28F0(…, **7006**, 631, …)   → `netHandlePlayerInfoPacket@0xDA8BD0`
//   Обе половины кладутся в ОДИН И ТОТ ЖЕ 748-байтный буфер записи, а СКЛЕИВАЮТСЯ по 4-байтному
//   КЛЮЧУ ПАРЫ: сервер берёт `v13 = sub_5329A0(1, 100000)` (случайное число) и кладёт его И в
//   5479 (payload +116), И в 7006 (payload +0). Клиент держит СПИСОК недособранных половин
//   (`pSelf + 2`, std::list): пришедшая половина ищет в списке пару с тем же ключом —
//   нашла ⇒ дособирает запись, зовёт `CPlayerInfoWin_SetData@0xDA7E30` и УДАЛЯЕТ пару из списка;
//   не нашла ⇒ кладёт себя в список и ждёт вторую.
//   ⚠ Порядок прихода НЕ фиксирован — обе ветки эталона симметричны, и порт тоже симметричен.
//
// ═════════ 2. РАСКЛАДКА ЗАПИСИ (748 б) — доказана С ОБЕИХ СТОРОН ══════════════════════════════
//   Клиент: `slotTable12_Init@0xDA8A60` (инициализация) + оба приёмника; сервер:
//   `Send_TargetChar_Information` (кадр `v14[3]` + `v15[113]` + `v16` + `secondaryCurrency` = 121 б).
//     +0   u16   — служебное (сервер: `Nick_name_[14]`,`[12]`) — ОКНОМ НЕ ИСПОЛЬЗУЕТСЯ
//     +2   u8    — служебное (`Nick_name_[13]`)               — ОКНОМ НЕ ИСПОЛЬЗУЕТСЯ
//     +3   17 б  — строка cp1251: **имя клана** цели (сервер `memcpy(v15, &CUser+…[1242], 0x11)`).
//                  Что это именно клан — доказано подписью слева: TXT07 (4210) получает loc
//                  **12010** = «Клан», а значение ложится в соседний TXT08 (4211).
//     +20  12×i32 — id заданий (сервер: `CUser.gap_54F6[44*i + 18]`, гейт `> 0`) — ОКНОМ НЕ ЧИТАЕТСЯ
//     +68  12×i16 — КЛЮЧ ЗАДАНИЯ (`44*i + 26`): по нему берётся НАЗВАНИЕ эпизода
//     +92  12×i16 — СТАДИЯ задания у цели (`44*i + 28`)
//     +116 i32   — КЛЮЧ ПАРЫ 5479↔7006 (случайное число сервера)
//     +120 u8    — `CUser::secondaryCurrency`                  — ОКНОМ НЕ ИСПОЛЬЗУЕТСЯ
//     +121 627 б — 11 записей надетых предметов по 57 б (приходит ТОЛЬКО в 7006, байты 4…630)
//   ⚠ Тело 5479 = 121 б, тело 7006 = 631 б (4 б ключа + 627 б экипировки). Транспортные 9 байт
//     эталонного `pPacket + 9` порт срезает ещё в `UAsdaNetSubsystem`, поэтому смещения тел
//     совпадают со смещениями записи один в один.
//
// ═════════ 3. ЧТО ОТКУДА БЕРЁТСЯ ПРИ ЗАПОЛНЕНИИ (`CPlayerInfoWin_SetData@0xDA7E30`) ═══════════
//   ★ ГЛАВНОЕ: бóльшая часть подписей берётся НЕ ИЗ ПАКЕТА, а из АКТОРА ЦЕЛИ в мире
//     (`win[235] = CharManager_FindByUserIdx(g_CharManager, win[238])`). Если цель ушла из AoI —
//     эталон не рисует НИЧЕГО и окно не показывает. Порт берёт те же поля из реестра соседей
//     `UAsdaWorldSubsystem::NetPlayers` (он и есть `CharManager` порта):
//       TXT00 4202 ← `char+1056`  имя                       → `GetNetPlayerInfo` (OutName)
//       TXT04 4207 ← `char+1082`  уровень, формат «%s %s » = FormatThousands + loc 12215 «Ур.»
//       TXT06 4209 ← `char+1081` подкласс, иначе `char+1080` класс → имя из `JobType.BIN`
//                    (эталон `sub_926C05` = `base + 64*id`, у нас `UAsdaSkillDatabase::GetJobName`)
//       TXT08 4211 ← ПАКЕТ, 17 б с +3 (имя клана)
//       TXT11 4216 ← «<знак зодиака цели> | <строка влечения>»
//       4217/4218/4219 — три «сердца»: видна РОВНО ОДНА, по индексу влечения 0/1/2
//   ★★★ МИСНЕЙМ РАЗБОРА ИСПРАВЛЕН: `Docs/UI/Character/OTHERCHARA_DATA.md` называет `char+1120`
//     «фракцией/расой», а `uiFormatPartyRelationNotice` — «отношением фракции». Ни того, ни
//     другого в этом окне нет. Доказательства, что это ЗНАК ЗОДИАКА:
//       • подпись НАД полем — TXT10 (4215) c loc **110631** = «Знак зодиака»;
//       • `cfgFindRecordByKey65` ищет запись по первому байту в таблице, загруженной
//         `ConstellationTable_Load` из `./Data/Constellation.BIN` (12 знаков, запись 65 б);
//       • `uiFormatPartyRelationNotice@0xF1A520` (имя тоже МИСНЕЙМ — к отряду отношения не имеет)
//         сверяет знак цели с 5-байтной записью `Constellation_Match.BIN` МОЕГО знака и отдаёт
//         0 (нотис 110632) / 1 (110633) / 2 (110634);
//       • второй потребитель этого же индекса — строка списка соулмейтов
//         (`soulmateWin_RefreshList@0xE72EE0`), где речь заведомо о совместимости знаков.
//     ⇒ порт зовёт УЖЕ ПОРТИРОВАННЫЕ `UAsdaConstellationSubsystem::SignName` и `MatchIndex`
//     (второй копии таблиц влечения не заводим — закон `port-shared-points`).
//
//   СТАТИЧЕСКИЕ ПОДПИСИ (ставятся один раз в `COtherCharaWnd_InitControls`):
//       TXT03 4206 ← loc 12008 · TXT05 4208 ← loc 12009 · TXT07 4210 ← loc 12010
//       TXT09 4213 ← loc 12011 · TXT10 4215 ← loc 110631 · QstInfo_Title 4290 ← loc 13300
//
//   СПИСОК ЗАДАНИЙ (12 строк 4251+3m, иконки 4252+3m «обычный» и 4253+3m «особый»):
//       название = `CQuest_Episode_FindByWord236(ключ)`; пустое ⇒ строка вообще пропускается;
//       «[loc 6004]» перед названием, если стадия цели равна полю `+32` строки `QCT.BIN`;
//       цвет строки и выбор иконки — по полю `+132` той же строки QCT:
//           1 → −486263 (0xFFF89489), 2 → −52516 (0xFFFF32DC), 3 → −10197816 (0xFF6464C8) —
//               у всех трёх видна иконка «особого» квеста (4253+3m);
//           иначе → −4144960 (0xFFC0C0C0) и видна иконка «обычного» (4252+3m).
//
// ═════════ 4. ДЕФЕКТЫ ОРИГИНАЛА, НАЙДЕННЫЕ ПРИ ПОРТЕ ═════════════════════════════════════════
//   D-OC1 «РАСХОЖДЕНИЕ ИНДЕКСОВ В СПИСКЕ ЗАДАНИЙ». Цикл эталона берёт НАЗВАНИЕ по индексу `k`
//     (`CQuest_Episode_FindByWord236(…, *(&a19 + k), 0)`), а СТАДИЮ и ЦВЕТ — по `questSlot`
//     (`*(&a19 + questSlot)`, `*(&a25 + questSlot)`), где `questSlot` растёт ТОЛЬКО у строк с
//     найденным названием. Как только у какого-то задания названия нет, `questSlot` отстаёт от
//     `k`, и следующая строка получает СТАДИЮ И ЦВЕТ ОТ ЧУЖОГО ЗАДАНИЯ. Номер строки при этом
//     считается по `questSlot` ПРАВИЛЬНО (строки без названия не оставляют дырки).
//     ЛЕЧЕНИЕ (закон `fix-original-defects`): стадию и цвет берём по `k`, номер строки — по
//     `questSlot`. Откат к 1:1 — `asda.OtherChara.LegacyQuestIndex 1`.
//   D-OC2 «СПИСОК НЕДОСОБРАННЫХ ПОЛОВИН НЕ ЧИСТИТСЯ». Половина, к которой пара так и не пришла
//     (потеря пакета, закрытие окна), остаётся в списке эталона НАВСЕГДА — записи по 748 б
//     копятся на каждый просмотр. Лечение: список ограничен (`kMaxPendingHalves`), самая старая
//     запись вытесняется, и он полностью чистится при сбросе сессии.
//   D-OC3 «7006 ЗАСОРЯЕТ СПИСОК ВКЛАДКИ „КТО?“» — дефект D-W10, лечится НЕ здесь, а в
//     `UAsdaNetSubsystem` (7006 в приёмник списка «Кто?» не заводится вовсе). Разбор и
//     доказательство дизасмом — `Private/UI/AsdaCommunityWhoSubsystem.cpp:675`.
//
// ═════════ 5. НОТИСЫ ОКНА (закон `audit-notices-on-new-system`) ══════════════════════════════
//   Таблица «событие → строка → канал» для 4200. СОБСТВЕННЫХ сообщений в ленту/чат у окна НЕТ —
//   все строки идут В КОНТРОЛЫ САМОГО ОКНА (канал `EAsdaMsgTarget::None`), потому что ни один
//   путь эталона (`otherCharaWnd_SetTargetUser`, оба приёмника, `CPlayerInfoWin_SetData`,
//   `npcTalkWnd_HandleMessage`) не зовёт ни `Chat_AddChatLine`, ни `UI_ShowLocalizedNotice`,
//   ни `CItem_BuildTooltipText`:
//     открытие окна            → 12008/12009/12010/12011/110631/13300 → контролы 4206/4208/4210/4213/4215/4290
//     уровень цели             → 12215 «Ур.»                          → контрол 4207
//     знак зодиака цели        → имя из `Constellation.BIN`            → контрол 4216
//     влечение знаков          → 110632 / 110633 / 110634              → контрол 4216 (вторая половина строки)
//     задание выполнено        → 6004 «Готово» (в скобках перед названием) → контролы 4251+3m
//     цель ушла из зоны        → строки НЕТ, эталон молча не показывает окно → только лог порта
//     запрос уже в полёте      → 9419 (шлёт `UAsdaPlayerMenuSubsystem`, не это окно) → лента
//   ⚠ ЧИСЛОВОГО ВВОДА у окна НЕТ ⇒ `Docs/Port/systems/NUMPAD_FLOW.md` его не касается (проверено: ни 4200,
//     ни 8000 в файле не упоминаются).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaOtherCharaSubsystem.generated.h"

class UUserWidget;
class UImage;
class UTextBlock;
class UWidget;
class UAsdaIconCellGrid;

// ═══ ПОЛОВИНА ОТВЕТА (эталонный элемент списка `pSelf + 2`) ════════════════════════════════════
//   Хранит ровно те поля 748-байтной записи, которые читает `CPlayerInfoWin_SetData`, плюс
//   служебные байты — их окно не использует, но они есть в пакете и печатаются в дампе
//   (закон `full-structural-analysis-always`: сначала полный разбор, потом правка).
USTRUCT()
struct FAsdaOtherCharaRecord
{
	GENERATED_BODY()

	// +116 у 5479 / +0 у 7006 — ключ, по которому склеиваются половины.
	UPROPERTY() int32 PairKey = 0;

	UPROPERTY() bool bHasCharHalf = false;   // пришёл 5479
	UPROPERTY() bool bHasGearHalf = false;   // пришёл 7006

	// ── половина 5479 ──
	UPROPERTY() int32 Svc0 = 0;              // +0 u16 — служебное, окном не используется
	UPROPERTY() int32 Svc2 = 0;              // +2 u8  — служебное
	UPROPERTY() FString ClanName;            // +3 17 б cp1251
	UPROPERTY() TArray<int32> QuestId;       // +20  12×i32 — окном не используется
	UPROPERTY() TArray<int32> QuestKey;      // +68  12×i16 — ключ эпизода
	UPROPERTY() TArray<int32> QuestStage;    // +92  12×i16 — стадия у цели
	UPROPERTY() int32 Svc120 = 0;            // +120 u8 — `CUser::secondaryCurrency`

	// ── половина 7006 ── 627 байт = 11 записей предмета по 57 б
	UPROPERTY() TArray<uint8> Gear;

	// Когда половина легла в список (секунды мира) — для вытеснения самой старой (D-OC2).
	double StampSec = 0.0;

	bool IsComplete() const { return bHasCharHalf && bHasGearHalf; }
};

UCLASS()
class ASDAREBORN_API UAsdaOtherCharaSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

	// Самотест «наведение не выбирает» (`asda.SelfTest.HoverNoSelect`) читает курсор 4223, слот и подменяет запись цели.
	friend struct FAsdaHoverNoSelectSelfTest;

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable ──
	//   Окно, признак «ждём ответ», цель и список недособранных половин принадлежат СЕССИИ.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Порт `otherCharaWnd_SetTargetUser@0xDA7640` БЕЗ отправки пакета (её делает меню игрока).
	//   Ставит цель и эталонный признак ожидания `win[40] = 1`. false — цели нет в реестре соседей
	//   (эталон в этом случае тоже ничего не делает: `if (this[235])`).
	UFUNCTION(BlueprintCallable, Category="Asda|ЧужойПерсонаж")
	bool BeginRequest(int32 TargetUserNum);

	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 16, баг 4; ключ `asda.OtherChara.SelfLocal`): окно 4200 о СЕБЕ. Сервер на 5478 о себе
	//   молчит (`Send_TargetChar_Information` отвечает только при UserIndex запросившего ≠ UserIndex цели), поэтому те же поля
	//   5479/7006 собираются из своих данных клиента (клан, 12 слотов журнала, надетое из контейнера 3) и окно заполняется
	//   тем же `SetData`. false — ключ выключен или своего номера ещё нет.
	bool ShowSelf(const TCHAR* Why);
	// Ключ `asda.OtherChara.SelfLocal` включён (меню игрока по нему решает, слать ли 5478 о себе).
	static bool IsSelfLocalEnabled();
	// Самотест: цель окна — я сам; последняя собранная запись.
	bool DebugIsSelfTarget() const { return bSelfTarget; }
	const FAsdaOtherCharaRecord& DebugShown() const { return Shown; }
	int32 DebugTargetUserNum() const { return TargetUserNum; }
	// Самотест (пачка 17): перерисовать надписи надетого после смены ключа `asda.OtherChara.EnchantText`.
	void DebugRefreshEquip() { RefreshEquipSlots(); }

	// Закрыть окно (кнопка 4201 `OTHERCHARA_DATACLOSE` и msg 21 самого окна 4200).
	UFUNCTION(BlueprintCallable, Category="Asda|ЧужойПерсонаж")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category="Asda|ЧужойПерсонаж")
	bool IsOpen() const;

	// Диагностика: КАЖДЫЙ разобранный контрол (закон `full-structural-analysis-always`).
	FString DumpControls() const;
	// Диагностика: цель, ожидание, содержимое списка половин и последняя собранная запись.
	FString DumpState() const;

	// Идентификатор окна в реестре `UAsdaWindowSubsystem` (он же имя окна .JUI).
	static const FName kWindowId;

private:
	// ── СЕТЬ ──
	// Общий делегат ответов контекст-меню по игроку: сюда приходят и 5479, и 7006.
	void OnMenuResult(int32 Opcode, const TArray<uint8>& Body);
	void OnCharHalf(const TArray<uint8>& Body);   // 5479, 121 б — `playerInfoWin_onRecvCharData`
	void OnGearHalf(const TArray<uint8>& Body);   // 7006, 631 б — `netHandlePlayerInfoPacket`
	// Общий хвост обеих веток: найти пару по ключу, дособрать и показать, либо положить в список.
	void MergeOrQueue(FAsdaOtherCharaRecord&& Half);

	// ── ОКНО ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();        // порт `COtherCharaWnd_InitControls@0xDA6C90`
	void SetData();              // порт `CPlayerInfoWin_SetData@0xDA7E30`
	void RefreshEquipSlots();    // порт `uiRefreshItemOptionSlots@0xDA77E0`
	void DrawSowelsForSlot(int32 Slot);   // порт `itemSlotDrawIconAndSowels@0xDA96B0`
	void ResetSowelPanel();      // «предмета нет»: 4 иконки по умолчанию + все 4 затемнителя видимы

	// ★★★ [ОРИГИНАЛ] 30.10.2026 (баг игроков «наведение выделяет надетое без клика»): у эталона ДВА разных
	//   сообщения слота надетого. msg 8 (НАВЕДЕНИЕ, `CControl_OnRButtonDown@0x12DBC30` — мисномер: меняет
	//   «контрол под курсором» `pManager+107` и поднимает событие 8) → `npcTradeWndOnHover@0xDA98C0` — ТОЛЬКО
	//   подсказка предмета. msg 0xB (НАЖАТИЕ ЛКМ, `CControl_OnLButtonDblClick@0x12DC190` — мисномер: поднимает 11
	//   при состоянии кнопки 1, то есть одиночном нажатии) → `uiWindowSlot_OnHoverShowIcon@0xDA7C00` — курсор
	//   EQUIPSEL 4223 на слот и самоцветы слота. Порт сводил обе ветки в наведение (ключ `asda.OtherChara.SelectOnClick`).
	void OnEquipHover(int32 Slot);     // msg 8: подсказка (при ключе 0 — ещё и ветка msg 0xB, прежний порт)
	void OnEquipClick(int32 Slot);     // msg 0xB: курсор 4223 + самоцветы слота (нажатие ЛКМ по слоту)
	void OnEquipUnhover();             // msg 0xA: погасить подсказку
	// Наведение на гнездо самоцвета (msg 8, idnum 4227…4230) и уход.
	void OnSowelHover(int32 Index);
	void OnSowelUnhover();

	UFUNCTION() void OnCloseClicked();   // 4201

	// ── ДОСТУП К ДАННЫМ ЦЕЛИ ──
	// Поля актора цели (эталонный `win[235]`). false — цели в реестре соседей нет.
	bool GetTarget(FString& OutName, int32& OutClassId, int32& OutSubClassId,
		int32& OutLevel, int32& OutConstellation, bool& bOutMale) const;

	// id предмета слота: `*(int*)(gear + 57*slot)`; 0 — слот пуст или записи нет.
	int32 SlotItemId(int32 Slot) const;
	// Самоцвет гнезда `j` слота `Slot`: i16 по `gear + 57*slot + 16 + 2*j` (эталонное `+180`).
	int32 SlotSowelId(int32 Slot, int32 j) const;
	// 57 байт записи предмета слота (для тултипа). Пусто — слота нет.
	TArray<uint8> SlotRecord(int32 Slot) const;

	FString Loc(int32 Id) const;
	// Виджет по имени; nullptr и строка в лог, если его нет (эталон здесь звал MessageBoxA «Point NULL»).
	UWidget* Find(const TCHAR* Name) const;
	void SetShown(UWidget* W, bool bShown) const;

	// ── СОСТОЯНИЕ ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;

	// Эталонные `win[238]` (userIdx цели) и `win[40]` (ждём ответ).
	UPROPERTY(Transient) int32 TargetUserNum = -1;
	UPROPERTY(Transient) bool  bAwaitingReply = false;
	UPROPERTY(Transient) bool  bSelfTarget = false;   // ★ 08.11.2026: окно показано о себе (`ShowSelf`), поля актора — свои
	// Эталонный `win[198]` — слот, на котором сейчас стоит курсор EQUIPSEL (ставит ЩЕЛЧОК, msg 0xB). −1 = курсора нет.
	//   Имя историческое: до 30.10.2026 порт ставил курсор наведением.
	UPROPERTY(Transient) int32 HoverSlot = -1;

	// Список недособранных половин — эталонный `pSelf + 2`, но ОГРАНИЧЕННЫЙ (дефект D-OC2).
	UPROPERTY(Transient) TArray<FAsdaOtherCharaRecord> Pending;
	// Последняя ПОЛНОСТЬЮ собранная запись — то, что сейчас показано в окне.
	UPROPERTY(Transient) FAsdaOtherCharaRecord Shown;
	UPROPERTY(Transient) bool bHasShown = false;

	// ── РАЗОБРАННЫЕ КОНТРОЛЫ ──
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtName = nullptr;      // 4202
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtLevel = nullptr;     // 4207
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtClass = nullptr;     // 4209
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtClan = nullptr;      // 4211
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSign = nullptr;      // 4216
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Hearts;            // 4217…4219
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> QuestRows;      // 4251+3m
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    QuestIconN;     // 4252+3m «обычный»
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    QuestIconP;     // 4253+3m «особый»
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EquipQty;       // текст количества слота
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     SowelIcons;     // 4227…4230
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    SowelDims;      // 4234…4237
	UPROPERTY(Transient) TObjectPtr<UWidget> EquipSel = nullptr;        // 4223 — курсор слота
	UPROPERTY(Transient) TObjectPtr<UWidget> SowelSel = nullptr;        // 4233 — курсор гнезда

	// Сетки клик/hover-зон поверх JUI-ячеек (общая точка порта, второй копии оверлея не заводим).
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> EquipGrid = nullptr;   // 11 слотов 4240…4250
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> SowelGrid = nullptr;   // 4 гнезда 4227…4230

	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Relays;   // мостики кликов (иначе GC)

	bool bControlsReady = false;
	FDelegateHandle HMenuResult;
};
