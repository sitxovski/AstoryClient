// Copyright (c) 2026 @sitxovski (Discord)
// Подсистема вкладки 2 «Клан» окна COMMUNITY_WIN 6400 — окно `COMMUNITY_CLAN_WIN` 14033.
//   ПОЛНЫЙ РАЗБОР-ИСТОЧНИК: `Reports/community_tabs_frnd_clan_who.md` РАЗДЕЛ 2 (§2.1…§2.9).
//
// ★★★ ГЛАВНОЕ ОТКРЫТИЕ РАЗДЕЛА 2: вкладку обслуживает объект `g_pCommunityWnd` (IDA типизирует его
//   то как `CmtyClan`, то как `CCommunityWnd` — это ОДИН объект), а НЕ `g_pCampWarClan`. Имена
//   `Recv_ServerChannelList`, `uiChannelListDispatch`, `recvTradeWindowState`, `uiSwitchPanelMode`,
//   `netSendAntiCheatToken*` — МИСНЕЙМЫ: никаких «каналов», «обмена» и «античита» там нет, это
//   чистая клановая логика. Доказательство — loc-строки, которые эти функции подставляют
//   (17007 «ОД в наличии», 12227/12228 «В игре»/«Не в игре», 17046−n звания).
//   Существующий `UAsdaClanSubsystem` покрывает только окна 14400/14470 и к 14033 отношения не имеет.
//
// Эталонные адреса: сборка UI `CmtyClan_Intro_BuildUI@0xBBCDF0` (+ `CmtyClan_Info_BuildUI@0xBBCF90`,
//   `CmtyClan_MemberSel_BuildUI@0xBBD770`, `CmtyClan_MyClanInfo_BuildUI@0xBBD9D0`,
//   `CmtyClan_Manage_BuildUI@0xBBE320`) · команды `uiPanel_HandleCommand@0xBC16D0` ·
//   подрежимы `uiSwitchPanelMode@0xBC2A10` · отрисовка списка `Recv_ServerChannelList@0xBC3160`
//   (миснейм, это RefreshMemberList) · разбор 4125 `recvParseServerChannelList@0xBCB060`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // общие UI-примитивы трёх вкладок (AsdaCommunityUi)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "Containers/Ticker.h"   // ★ 01.11.2026: FTSTicker — покадровый опрос панели «Предводитель»
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "UI/AsdaJuiHSlider.h"    // ★ 01.11.2026: ползунки RGB герба
#include "AsdaCommunityClanSubsystem.generated.h"

class UUserWidget;

// ═══════════════════ ЗАПИСЬ ЧЛЕНА КЛАНА — 131 БАЙТ (отчёт §2.5; опкоды 4125 и 4133) ═══════════
//   Раскладка сведена из клиента (`Recv_ServerChannelList`, `listGetElementByIndex@0xBC3820`
//   копирует ровно 131 байт) и сервера (ctor `sub_40B820`, заполнение `sub_405260`); расхождений нет.
struct FAsdaClanMemberRecord
{
	static constexpr int32 kSize = 131;
	uint8 B[kSize] = {};

	int32   UserIndex() const;                 // [0..3]  i32 id аккаунта в БД (ключ записи)
	uint8   CharIndex() const { return B[4]; } // [4]     слот персонажа 0..2 (вторая половина ключа)
	FString Name()      const;                 // [5..24] имя, cp1251
	uint8   Sex()       const { return B[25]; }// [25]    пол → loc 11701 + пол
	uint8   Level()     const { return B[26]; }// [26]    уровень
	uint8   Job1()      const { return B[27]; }// [27]    класс 1-й профессии
	uint8   Job2()      const { return B[28]; }// [28]    класс 2-й профессии (приоритетный)
	uint8   Grade()     const { return B[33]; }// [33]    звание 1..5 → loc 17046 − звание
	/** ★★★ ИСПРАВЛЕНО АУДИТОМ 31.08.2026: «ОД в наличии» лежит в [34..37], а НЕ в [29..32].
	 *  Доказано ДВУМЯ сторонами:
	 *   · клиент `Recv_ServerChannelList` — дизасм `00BC33C7  mov eax, dword ptr [ebp-0x72]`,
	 *     буфер записи в `[ebp-0x94]` ⇒ смещение 0x94−0x72 = 34; далее `sprintf("%s: %d", loc 17007, …)`
	 *     и вывод в слот 1 строки (контрол `CmtyClan_DAPoints{n}`);
	 *     соседний `00BC3382 movzx eax, byte [ebp-0x73]` ⇒ 33 = звание (сходится с известным фактом);
	 *   · сервер `GameServer.c:sub_405260@0x405260` — поле `v19` на `[ebp-0x6B]` при базе структуры
	 *     `[ebp-0x90]` и старте проводной записи с +3 ⇒ ровно [34..37]; `v17` на `[ebp-0x70]` ⇒ [29..32]
	 *     (это ДРУГОЕ поле, `clanMemberRec__+2`, клиентом не читается).
	 *  До правки колонка «ОД в наличии» показывала чужой i32. */
	int32   Points()    const;                 // [34..37] i32 активные очки (ОД) участника
	uint8   Channel()   const { return B[111]; } // [111] номер канала (в тексте печатается +1)
	uint8   MapId()     const { return B[112]; } // [112] id карты
	uint8   InGame()    const { return B[113]; } // [113] флаг «в игре» (сервер шлёт 0/1)
	// [29..32], [38..46], [114] — клиентом НЕ используются, семантика НЕ выведена (отчёт §6 п.2).
	// [47..110] — memo «Заметки» участника (пишет опкод 6518), в списке не используется.
	// [115..130] — дата/час последнего выхода (сервер пишет только в ветке «оффлайн»).

	uint8 JobForCaption() const { return B[28] ? B[28] : B[27]; }
};

// ═══════════ ИНФО-БЛОК КЛАНА — 191 БАЙТ (опкод 4123; `clan_soulmate_gaps.md` §2.2) ═════════════
//   Приходит ответом на 4122; эталон `recvTradeWindowState@0xBCAE60` копирует его в `wnd+0x614`
//   целиком (`qmemcpy(…, 0xBF)`), а поля разбирает `uiTradeWindowRefresh@0xBC3BF0`.
//   ⚠ Оба имени эталона ВРУТ: ни торговли, ни «состояния окна» здесь нет — это клан.
struct FAsdaClanBaseInfo
{
	static constexpr int32 kSize = 191;
	uint8 B[kSize] = {};
	bool bValid = false;                        // пришёл ли 4123 хоть раз

	int32   ClanIndex()  const;                 // [0..1]   i16 индекс клана (`00BCC1D8 mov ax,[esi+0x614]`)
	FString ClanName()   const;                 // [2..19]  char[18] имя клана        → 14003 (блок+2)
	FString ChiefName()  const;                 // [20..46] char[27] имя главы клана  → 14007 (блок+20)
	uint8   LevelRaw()   const { return B[47]; }// [47] уровень клана; печатается как (raw+1) «%02dLv»
	//   ★ 27.09.2026: [47] — это ЧИСЛО СДЕЛАННЫХ РАСШИРЕНИЙ клана (сервер `ClanBaseInfo_MaxClanUserCount_Req
	//     @0x404710`: локал v25 = блок+47, при 5 → отказ 5, при успехе пишет v25+1). «Уровень» 01..06Lv.
	/** [45] u8 — ЛИМИТ ЧЛЕНОВ клана (текущее число мест). Сервер: локал v24 = блок+45 (дизасм
	 *  живого образа 0x404948 `add ecx,5 / cmp ecx,0x64`), новое значение = v24 + 10 (0x404AE8).
	 *  Эталон клиента кладёт его в 14453 окна расширения (`gap_616[43]` = wnd+1601 = блок+45),
	 *  а ответ 4137 @3 перезаписывает это же поле. ⚠ Имя главы [20..] до [45] не доходит: имена
	 *  не длиннее 16 байт, [40..43]/[44] — userIndex/charIndex главы. */
	uint8   MaxMembers() const { return B[45]; }
	/** [49..52] i32 → эталон кладёт его в контрол **14013** `CmtyClan_RemainPoints`
	 *  (`uiTradeWindowRefresh`: `sprintf("%d", *(DWORD*)&filler[1605])` → `filler[1124]` = wnd+0x464,
	 *   а 0x464 привязан к ID 0x36BD = 14013, дизасм `00BBD42E push 0x36bd … 00BBD440 mov [ecx+0x464]`).
	 *  ⚠ Имя метода историческое; РОЛЬ поля — «Осталось ОД». */
	int32   RemainPoints() const;
	int32   TotalPoints()  const;               // [53..56] i32 → контрол 14009 (wnd+0x45C, ID 0x36B9)
	/** [65] флаг эмблемы: эталон `if (filler[1621] != 1) uiNumberDisplay_Reset(...)`;
	 *  1621 − 1556 = 65 (НЕ 66, как было записано до аудита). */
	uint8   HasEmblem()  const { return B[65]; }
	// [57..64] — четыре u16 масок прав для званий 2..5; читаются через `RankPermission`.
	// [66..105] — 40 б эмблемы/флага клана (`memcpy(&filler[2048], &filler[1622], 0x28)`; 1622−1556 = 66).
	// ⚠ [20..46] как «char[27]» — оценка сверху: эталон читает оттуда только C-строку. Поля
	//   «userIndex/charIndex главы» из блока НИКТО не читает — недоказанные объявления УДАЛЕНЫ аудитом.
	// [106..190] эталоном НЕ читаются вовсе — раскладка не доказана, полей не выдумываем.

	/** ★ Порт `ClanRankPermission@0xBC44A0` — ДОСЛОВНО:
	 *    `mask = *(WORD*)((char*)wnd + 0x649 + rank*2)`; база `wnd+0x614` ⇒ смещение в блоке
	 *    `0x649 − 0x614 = 53`, то есть `mask = u16 @ (53 + rank*2)` (звание 2 → 57, 3 → 59, 4 → 61,
	 *    5 → 63 — совпадает с таблицей отчёта); `bit = (perm − 1) & 15`; ответ `mask & (1 << bit)`.
	 *  Значения `perm` (места проверки на сервере): 1 приглашать · 2 объявления · 8 менять звания ·
	 *  0x10 исключать. ⚠ Права 4 и 0x20 — мест проверки в срезе сервера НЕТ (смысл не доказан).
	 *  ⚠ Звание 1 (глава) в таблице масок НЕ ЛЕЖИТ: сервер даёт ему 63 (все шесть бит) отдельной
	 *    веткой `switch` — поэтому вызывающий проверяет «глава» ОТДЕЛЬНО, как это делает эталон
	 *    (`ClanRankPermission(wnd, звание, 2) || IsClanMaster()`).
	 *  ★★★ ДЕФЕКТ ОРИГИНАЛА D-C4 (найден аудитом 31.08.2026): диапазона звания эталон НЕ проверяет
	 *    вовсе. При звании 0 читается u16 по смещению 53 — это МЛАДШЕЕ СЛОВО «Всего ОД» ([53..56]),
	 *    при звании 1 — старшее. То есть у члена без прав (или у главы после релогина, когда звание
	 *    сброшено в 0) право 2 «выдаётся» просто потому, что у клана в сумме ОД взведён бит 1.
	 *    Воспроизводим 1:1 (ограничиваем ТОЛЬКО выход за 191-байтовый буфер) — см. `.cpp`. */
	bool RankPermission(int32 Rank, int32 Perm) const;
};

UCLASS()
class ASDAREBORN_API UAsdaCommunityClanSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка Р5 отчёта 14_soulmate: подсистема жила на GameInstance и переживала
	// разрыв, смену персонажа и травел вместе со списками и защёлками сессии.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Вкладка 2 показана/скрыта. Сигнатура — как у `UAsdaSoulmateSubsystem::OnTabActivated`.
	//   Показ = эталонный `communityUI_SwitchTab` case 2: запросить инфу клана и список членов,
	//   сбросить подрежим в 0 (`uiSwitchPanelMode(this, −1)`) и показать панель.
	void OnTabActivated(bool bVisible, UUserWidget* CommunityWindow);

	// ── Подрежимы 14000 CMTY_CLAN_3BTN (порт `uiSwitchPanelMode@0xBC2A10`) ──
	//   0 «Состав» (12201) · 1 «Информация» (12200) · 2 «Предводитель» (12202).
	//   ★ `Mode == −1` — эталонный «войти во вкладку»: подрежим 0 И принудительная установка
	//     выбранного пункта полосы 14000 в 0 (`if (a2 == −1) { a2 = 0;
	//     CControl_SetSelectedIndex(modeTabCombo, 0); }`). Зовётся из `OnTabActivated`, как
	//     `communityUI_SwitchTab` case 2 зовёт `uiSwitchPanelMode(g_pCommunityWnd, −1)`.
	void SwitchMode(int32 Mode);

	// ── Пагинация списка членов (страница 1-based, как в эталоне) ──
	void PageFirst();   // 14230 CMTY_CLAN_STATBTN1
	void PagePrev();    // 14231 CMTY_CLAN_PREBTN1
	void PageNext();    // 14232 CMTY_CLAN_NEXTBTN1
	void PageLast();    // 14233 CMTY_CLAN_ENDBTN1

	/** ★★★ СТРАНИЦЫ ПАНЕЛИ УПРАВЛЕНИЯ 35000 — порт `uiSelectPanelTab@0xBC5C20`.
	 *  Привязана к шести пунктам полосы 35001 `CmtyClan_SubMenuBtn` (эталон:
	 *  `uiWndDispatchCommand@0xBC2240`, сообщение 66817, `if (a3 == 35001) uiSelectPanelTab(this, a6)`).
	 *  Страницы: 0 «Кандидаты» (ВСЕГДА отказ, нотис 17107) · 1 «Права членов клана» ·
	 *  2 «Ранги членов клана» · 3 «Объявления» · 4 «Передача полномочий» · 5 «Герб клана».
	 *  Гейты эталона: страница 3 — право 2 по маске звания ЛИБО «я глава»; страницы 1/2/4/5 —
	 *  ТОЛЬКО глава клана; отказ — нотис 17006 и возврат на прежнюю страницу. */
	void SelectManagePage(int32 PageIdx);

	void SelectRow(int32 Row);      // клик по строке 14120…14220 (рамка 14110 CMTY_CLAN_MEMSEL)
	void ActionLeaveClan();         // 14032 CMTY_CLAN_SEC_BTN → модалка 12432 (код 45) → 4116
	void ActionReadNotices();       // 14025 CMTY_CLAN_NOTICE_BTN → 6511 «прочитать объявления»

	/** ★★★ КНОПКИ «ОЧИСТИТЬ» ОБЪЯВЛЕНИЕ КЛАНА — 14090…14094 (`CmtyClan_NoticeDel00…04`).
	 *  До правки 31.08.2026 они были МЕРТВЫ: подписи ставились, обработчиков не было (находка M7
	 *  аудита `Reports/audit_clan.md`).
	 *  `Slot`: **0** — кнопка 14090 у «раскрытого» объявления (панель 14089/14027);
	 *          **1…4** — кнопки 14091…14094 у строк списка 0…3.
	 *  Эталон — `uiPanel_HandleCommand@0xBC16D0`, две разные ветки:
	 *   · `case 0x370A` (14090): `if (wnd[483] != −1) { … wnd[487] = wnd[483]; модалка 17182 }`;
	 *   · `0x370B…0x370E` (14091…14094): дизасм `00BC1AA5 sub eax,0x370b` → индекс 0..3;
	 *     `00BC1AB6 imul eax,eax,0x14; 00BC1ABC mov ecx,[ecx+eax+0x50]` — контрол строки,
	 *     `00BC1AC6 call strlen; 00BC1AD1 jb` — гейт «в строке есть текст»; затем обход списка
	 *     объявлений `wnd+0x780` до элемента с этим индексом и `00BC1B61 mov [ecx+0x79c],edx` —
	 *     ПЕРВЫЙ dword записи (её числовой ключ) кладётся в `wnd+0x79C`.
	 *  Далее обе ветки одинаковы (`00BC1B6E`…): `if (wnd[487] != −1)` → гейт
	 *  `ClanRankPermission(звание, 2) ‖ IsClanMaster` → модалка `loc 17182` с кодом 17182;
	 *  «Да» (`case 0x431E` того же диспетчера) → `netSendAuthToken6515@0xBCC470` = опкод 6515. */
	void ActionDeleteNotice(int32 Slot);

	/** ★★★ 14023 `Clan_MyIntroSave` «Сохранить» — ЗАМЕТКА О СЕБЕ (опкод 6518).
	 *  Эталон: `uiPanel_HandleCommand@0xBC16D0` `case 0x36C7` (0x36C7 = 14023, `ui_core.c:15311`)
	 *    → `chatSendMessage_C7D0@0xBCC7D0` (`chat.c:5753`).
	 *  Отправитель эталона ДОСЛОВНО:
	 *    `text = GetEditText(wnd[14])` — контрол 14021 `Clan_MyIntroTxt`;
	 *    все `'` меняются на `` ` `` (защита оригинала от SQL-инъекции; делает наш отправитель);
	 *    тело 70 б: @0 i32 charId · @4 i16 clanIndex · @6 char[64] текст; пакет 94 б, конверт А;
	 *    `if (strlen >= 0x40) обнулить последний байт тела` — обрезка внутри отправителя;
	 *    **после отправки** `CControl_SetText_12F9850(wnd[14], "")` — поле ввода ОЧИЩАЕТСЯ.
	 *  ГЕЙТОВ У КЛИЕНТА НЕТ ВООБЩЕ — ни по правам, ни по длине, ни на пустую строку: пакет уходит
	 *    всегда. Проверяет сервер (`Recv_User_Memo_Write@0x409320`): `clanIndex ≤ 10000`, клан
	 *    совпадает с `CUser+99096`, `strlen(&body[6]) ≤ 64`; НИЖНЕЙ границы нет — пустая заметка
	 *    законна и стирает прежнюю. Отказ — 6519, эталонный приёмник которого ПУСТ (`return a1`),
	 *    поэтому реакции на отказ нет и выдумывать её нельзя. */
	void ActionSaveMyMemo();

	/** ★★★ 14031 `CMTY_CLAN_NOITICE_OK` «ОК» — НАПИСАТЬ ОБЪЯВЛЕНИЕ КЛАНА (опкод 6513).
	 *  Эталон: `case 0x36CF` (= 14031, `ui_core.c:15320`) → `chatSendMessage_BE30@0xBCBE30`
	 *    (`chat.c:5702`) И СРАЗУ ЗА НИМ `sub_922772` (`ui_core.c:15322`).
	 *  Отправитель: `text = GetEditText(wnd[173])` — контрол 14029 `CMTY_CLAN_NOITICE_WRITE`;
	 *    тело 550 б: @0 i32 charId · @4 i16 clanIndex · @6…@49 нули · **@50 char[500] текст**
	 *    (★ текст по +50, а НЕ по +6 — `sprintf((char*)&chatPacket[12] + 2, …)`); пакет 574 б,
	 *    конверт А; `if (strlen >= 0x1F4) обнулить последний байт тела`.
	 *  Гейтов у клиента снова НЕТ. Сервер (`Recv_User_Notice_Write@0x4072C0`): `clanIndex ≤ 10000`,
	 *    свой клан, право `Authority & 2` по званию (звание 1 «глава» получает 2 отдельной веткой
	 *    `switch`, звание 0 — 0, звания 2…5 — маску из таблицы клана) и длина **1…500**. Отказ —
	 *    6514, приёмник эталона ПУСТ.
	 *  `sub_922772@0x922772` — thunk на `uiClanDelete_FocusInput@0xBC61B0` (★ МИСНЕЙМ, к удалению
	 *    клана отношения не имеет), тело ровно три действия:
	 *      `CControl_SetText(wnd[173], "")` — очистить поле 14029;
	 *      `CControl_SetFocus(wnd[173], 1)` — вернуть в него каретку;
	 *      `CControl_SetVisible(wnd[265], 0)` — СКРЫТЬ контрол 14101 `CMTY_CLAN_SLASH`
	 *      (нижняя полоса с пагинацией; 265*4 = 0x424, а 0x424 привязан к ID 0x3715 = 14101). */
	void ActionWriteNotice();

	/** ★★★ 14030 `CMTY_CLAN_NOITICE_CAN` «Отмена».
	 *  Эталон: `case 0x36CE` (= 14030, `ui_core.c:15317`) — РОВНО ОДНА строка,
	 *    `CControl_SetText_12F9850(wnd[173], "")`: очищается поле ввода объявления 14029.
	 *  Ни фокуса, ни скрытия полосы 14101, ни закрытия панели, ни пакета — «Отмена» здесь значит
	 *    «стереть набранное», а не «уйти с экрана». */
	void ActionCancelNotice();

	int32 GetMemberCount() const { return Members.Num(); }
	/** ★ 27.09.2026: инфо-блок клана (4123/4131/4132) для окна расширения 14450 (`UAsdaClanSubsystem`):
	 *  эталон держит блок в ОДНОМ объекте кланового окна, у нас владелец блока — эта подсистема. */
	const FAsdaClanBaseInfo& GetBaseInfo() const { return Info; }
	/** Счётчик изменений блока: окно 14450 перечитывает числа, когда он сдвинулся. */
	int32 GetBaseInfoRevision() const { return InfoRevision; }
	/** Порт записи эталона `uiInventoryRepairResultHandler` код 1: `gap_616[43] = пакет@3` —
	 *  новый лимит членов из 4137 кладётся в блок (+45) и панель перерисовывается. */
	void ApplyIncreasedMaxMembers(uint8 NewMax);
	/** Моё звание в клане (`CCharacter+0x97B`, 1 = глава) — общий геттер сетевой подсистемы. */
	int32 SelfGrade() const;
	/** Есть ли у МЕНЯ право `Perm` (эталонный гейт: маска звания ЛИБО «я глава»). */
	bool  SelfHasPermission(int32 Perm) const;
	/** ★ 27.09.2026 (СРЕДН-1 ревизии friend_gate): порт хвоста `Recv_ClanMemberLeave_Notice` case 1 (network.c:12061-12066):
	 *  `if (CControl_IsVisible(g_CommunityUI+0x20)) { 4122; 4124; }` — панель вкладки «Клан» ВИДИМА, то есть последней
	 *  выбрана вкладка 2 (флаг панели живёт и при закрытом окне 6400, как `ActiveTab` порта). Шлёт БЕЗ ключа
	 *  `RequestOnTabOpen` — у эталона здесь условия нет. true — запрос ушёл (вкладка видима и есть клан). */
	bool RequestClanDataIfTabShown(const TCHAR* Why);
	FString DumpState() const;
	/** `sub_91F5D6` → `sub_BC8150@0xBC8150`: снять каретку с полей 14029 и 14021. ★ 10.10.2026 (barber_minors): открыт —
	 *  его зовёт и закрытие окна 6400 (`communityUI_Close@0xBD3F20` → `sub_91F5D6(g_pCommunityWnd)`, social.c:3825). */
	void ClearEditFocus();

	/** ★★★ 01.11.2026 (сессия clan-window) [ОРИГИНАЛ] — ПРИЁМ КЛАНОВЫХ ПАКЕТОВ ПАНЕЛИ УПРАВЛЕНИЯ И СОСТАВА.
	 *  Зовёт `UAsdaNetSubsystem::HandlePacket` (свой разбор длины там же). Опкоды и эталонные приёмники:
	 *   4115 `channelListRespHandler@0xBCA410` (исключение) · 4133 `Recv_AuthRelogin@0xBCB760` (вид 1/2/3 — запись члена) ·
	 *   6509 `netHandleItemTooltipNotice@0xBCCDC0` (права званий) · 6517 `memberListSelectById@0xBCC680` (объявление удалено) ·
	 *   6520 `partyUpdateMemberFromItem@0xBCCB20` (заметка члена) · 6522 `uiSlotResultShowNotice17172@0xBCD070` ·
	 *   6523 `uiHandleSlotResultPacket@0xBCD0E0` (звание) · 6525 `netOnRefineSlotUpdate@0xBCDB20` (герб: сертификат списан) ·
	 *   6527 `uiTradeWindowSetData@0xBCD840` (блок 191 б) · 6528 `uiPanelSelectModeNotify@0xBCD570` (смена главы) ·
	 *   6529 `quickSlot_RecvPartyMemberDispatch@0xBCD810` (мне предлагают стать главой — значок 13).
	 *  Сеть из колбэка не шлёт (закон `no-reentrant-net-callbacks`): ответные запросы 4124/4138 — флагом, в `TickManage`. */
	void OnAdminPacket(int32 Opcode, const TArray<uint8>& Body);
	/** Самотест: виджет-строка выпадающего списка звания строки `Row` (0…6) страницы «Ранги» (nullptr — нет). */
	class UAsdaJuiComboBox* DebugGradeCombo(int32 Row) const { return GradeCombos.IsValidIndex(Row) ? GradeCombos[Row].Get() : nullptr; }
	/** Самотест: текущее состояние редактора герба (цвета фон/символ/рамка, ячейки и страницы). */
	FString DebugEmblemState() const;

private:
	// ══════ ★★★ 01.11.2026 (clan-window): ПАНЕЛЬ «ПРЕДВОДИТЕЛЬ» — ДЕЙСТВИЯ СТРАНИЦ (до этой правки — только подписи) ══════
	/** Покадровый опрос мыши вкладки: выпадающие списки званий, наведение/щелчок по строкам без кнопок, ползунки и сетки герба,
	 *  подсказка 14900, отложенные запросы. Тикер ядра (`FTSTicker`), пока вкладка привязана к живому окну. */
	bool TickManage(float Dt);
	FTSTicker::FDelegateHandle ManageTickHandle;
	FAsdaMouseFrame ManageMouse;   // кадр мыши вкладки (одна `FAsdaMouseFrame` на окно — закон общей точки)
	/** Колесо над окном 6400: прокрутка сеток герба (эталонные окна прокрутки 36201/36399, событие 0x17). */
	bool OnWheel(int32 Notches);
	/** Привязка действий страниц (зовёт `BindTabControls`, тем же якорем `Relays`). */
	void BindManageActions();
	/** После показа страницы по префиксам (`SetManagePageVisible`) вернуть узлам, которые в разметке СКРЫТЫ и ведутся рантаймом,
	 *  их состояние: рамка выбора 36020 и подсветки 35924+20i (стр. 4), раскрывающиеся списки званий (стр. 2), курсоры сеток (стр. 5).
	 *  Без этого показ страницы зажигал рамку и все пять подсветок разом. */
	void ReapplyPageRuntimeState(int32 PageIdx);

	// ── Страница 1 «Права членов клана»: 35590 `antiCheatValidateEquipSums@0xBC4330` (★ МИСНЕЙМ) → 6508; 35591 → перерисовка ──
	void ActionSaveRankPerms();

	// ── Страница 2 «Ранги членов клана» ──
	/** Строка страницы → индекс в `Members` (−1 — пусто). Эталон адресует запись как `база + строка + 1`, а глава пропускается
	 *  без сдвига строки (`listUI_RefreshPage7Rows`): при главе не первым в списке щелчок попадал в ЧУЖУЮ запись — см. ключ. */
	int32 GradeRowMember[7] = { -1, -1, -1, -1, -1, -1, -1 };
	/** Индекс записи по строке страницы «Ранги»: исправленная карта или эталонная формула (ключ `asda.Clan.GradeRowMap`). */
	int32 GradeRowToMember(int32 Row) const;
	/** Выбор пункта списка звания (событие 70145 → `uiBuildItemUpgradeNotice@0xBC4080`): вопрос 17167/17168. */
	void OnGradePicked(int32 Row, int32 Item);
	/** «Изгнать» 35624+20i (`uiCacheItemUpgradeRecord@0xBC3F50`): вопрос 17166 → «Да» → 4114. */
	void ActionExpel(int32 Row);
	/** Отправка 4114 (`netSendItemInscriptionPacket@0xBCA1B0`): гейт «в клане», иначе нотис 12428. */
	void SendExpel4114();
	/** Отправка 6521 (`netSendAntiCheatReport@0xBCCE40`): новое звание отложенной записи. */
	void SendGrade6521();
	UPROPERTY(Transient) TArray<TObjectPtr<class UAsdaJuiComboBox>> GradeCombos;   // 7 списков 35630+20i
	/** Отложенная запись члена (`CCommunityWnd+0x6E0`, 131 б): цель исключения / смены звания / передачи главы. */
	FAsdaClanMemberRecord PendingRec;
	bool bPendingRec = false;

	// ── Страница 4 «Передача полномочий» ──
	int32 MasterSelRow = -1;     // `this[486]` — выбранная строка 0…4 (рамка 36020), −1 — нет
	int32 MasterHoverRow = -1;   // строка под курсором (подсветка `CmtyClan_Sub5_COver<i>`)
	/** Щелчок по строке (событие 0xB → `uiHighlightRowByCoord@0xBC28B0`). */
	void MasterRowClick(int32 Row);
	/** 36021 (`mailSelectEntryShowTooltip@0xBC45D0`, ★ МИСНЕЙМ): в сети — вопрос 17204, нет — нотис 17209. */
	void ActionMasterTransfer();
	/** 6526 (`netSendNameRequest@0xBCD300`) + нотис 17208. */
	void SendMasterTrans6526();
	/** Значок 13 на панели уведомлений (6529) → вопрос 17207 → 6530 (1 — да, 2 — нет). */
	void OnNoticeIcon(int32 Type);
	FDelegateHandle HNoticeIcon;

	// ── Страница 5 «Герб клана» (`uiInvFilterPanelRefresh@0xBC6410` и соседи) ──
	uint8 EmBg[3] = { 255, 255, 255 };       // filler+2024…2026 — фон R,G,B
	uint8 EmBorder[3] = { 255, 255, 255 };   // filler+2027…2029 — рамка R,G,B
	uint8 EmSign[3] = { 255, 255, 255 };     // filler+2030…2032 — символ R,G,B
	int32 EmSignCell = 0, EmSignPage = 0;     // filler+2036 / +2128 — ячейка 0…14 и верхний ряд сетки символов
	int32 EmBorderCell = 0, EmBorderPage = 0; // filler+2040 / +2132 — то же для рамок
	uint8 EmDesign[40] = {};                  // filler+2088 — собранный к сохранению дизайн (раскладка `FAsdaClanFlagData`)
	TArray<uint8> EmCertRec;                  // filler+2136 — запись 57 б найденного сертификата герба (linkCode/подтип 62)
	TArray<FAsdaJuiHSlider> EmSliders;           // 9 ползунков: фон 36122/24/26, символ 36151/53/55, рамка 36321/22/23
	void EmblemBindWidgets();                 // привязка ползунков и сеток (из `BindTabControls`)
	void EmblemDrawGrids();                   // `uiSlotGridDrawIcons@0xBC7140` + `uiInvSlotDrawIcons@0xBC7280`
	void EmblemDrawPreview();                 // `uiSlotDrawIconAndTooltip` + `uiCursorDrawIconSlot` с a3 = 1 (без смены ячейки)
	void EmblemApplyColors();                 // `uiColorApplyRGBToControls@0xBC7EA0`
	void EmblemSyncSliders();                 // `uiColorOptionsSyncScrollbars@0xBC7FE0`
	void EmblemLoadFromBlock();               // `uiNumberDisplay_Refresh@0xBC7AA0` (герб из блока [66..105])
	void EmblemReset();                       // `uiNumberDisplay_Reset@0xBC79C0`
	void EmblemCellClick(bool bSign, int32 Cell);   // событие 0xB по 36170…/36340…
	void ActionEmblemSave();                  // 36310 → `uiNumberDisplay_RecalcColors@0xBC76A0` → вопрос 17231/17232
	void EmblemCheckCertificate();            // «Да» 17231/17232 → `invTryAddItemWithNotice@0xBC7910` → 17233 / нотис 17234
	void SendFlag6524();                      // «Да» 17233 → `netSendAntiCheatToken_D8B0@0xBCD8B0`
	int32 EmSignCount() const;                // записей Clan_Sign (38)
	int32 EmBorderCount() const;              // записей Clan_BorderIcon (30)

	// ── Подсказка 14900/14901 (`this[266]`/`this[267]`) ──
	TArray<TWeakObjectPtr<class UImage>> BubblePlanes;   // девять планов `CmtyClan_IntroBubble`
	/** ★ ревизия 01.11: планы окна по ключу «idc#план» — снимаются ОДИН раз при привязке (`BuildPlaneCache`), чтобы покадровый
	 *  опрос не обходил дерево окна 6400 (2000+ узлов) на каждый поиск. */
	TMap<FString, TWeakObjectPtr<class UWidget>> PlaneCache;
	void BuildPlaneCache();
	class UImage* PlaneImg(const FString& Idc, int32 Plane) const;
	class UWidget* PlaneAny(const FString& Idc, int32 Plane) const;
	/** Подписка на щелчок по значку уведомлений (13 — предложение стать главой). Зовётся из тикера: значок может прийти,
	 *  пока вкладку ни разу не открывали, а хотбар к `Initialize` ещё может быть не поднят. */
	void EnsureNoticeIconHook();
	FVector2D BubbleCornerTL = FVector2D::ZeroVector, BubbleCornerBR = FVector2D::ZeroVector;
	FVector2D WindowJuiOrigin = FVector2D::ZeroVector;   // начало координат окна 6400 в корне (бубл − его JUI-смещение)
	int32 BubbleKey = INDEX_NONE;                        // что сейчас показано (строка-источник), −1 — скрыто
	void ShowBubble(int32 Key, const FString& Text, float W, float H, const FVector2D& LocalPos);
	void HideBubble();
	void TickHover(const struct FAsdaMouseFrame& M);

	// ── Отложенные запросы (из сетевых колбэков) ──
	bool bWant4124 = false;   // 6523 → `netSendAntiCheatToken` (4124 — заново список членов)
	bool bWant4138 = false;   // 4133 вид 2 про меня → 4138 (освежить свою карточку)
	bool bWant4122 = false;   // ★ ревизия: 6529 при неизвестном блоке клана → 4122 (имя клана для [ss1] вопроса 17207)
	// ── Приём ──
	void OnRecvExpel4115(const TArray<uint8>& B);
	void OnRecvMember4133(const TArray<uint8>& B);
	void OnRecvNoticeGone6517(const TArray<uint8>& B);
	void OnRecvMemo6520(const TArray<uint8>& B);
	void OnRecvGrade6523(const TArray<uint8>& B);
	void OnRecvFlag6525(const TArray<uint8>& B);
	void OnRecvMaster6528(const TArray<uint8>& B);
	/** Строка loc с подстановкой до трёх строковых слотов (`CObject_SetSlotString(ctx, 0…2)` + `UI_FormatLocalizedNotice`). */
	FString LocSlots(int32 Id, const FString& S1, const FString& S2 = FString(), const FString& S3 = FString()) const;
	/** Окно-вопрос TWOBUT с эталонным ключом очереди (`CItem_BuildTooltipText(ui, 1, Key, …)`). */
	void AskConfirm(const FString& Text, int32 Key, TFunction<void(bool)> OnResult);

private:
	void BindTabControls(UUserWidget* CommunityWindow);
	// ★ ИСПРАВЛЕН КОММЕНТАРИЙ (аудит): 4122 = ИНФА клана, 4124 = СПИСОК ЧЛЕНОВ (роли не путать).
	void RequestClanData();                  // 4122 (инфа) + 4124 (список членов)
	bool SendClanDataRequests();             // ★ 27.09.2026: сама пара 4122+4124 (общая для показа вкладки и 4109 код 1)
	void RefreshMemberList();                // порт `Recv_ServerChannelList@0xBC3160`
	void FillRow(int32 Row, const FAsdaClanMemberRecord* Rec);
	void ClearRow(int32 Row);
	void ApplyModeVisibility(int32 Mode);    // показ/скрытие панелей подрежимов

	// ── ПАНЕЛЬ УПРАВЛЕНИЯ 35000: шесть страниц (реализованы 02.09.2026) ──
	/** Границы поддеревьев шести страниц в индексах обхода JUI — снимаются с ЖИВОГО дерева
	 *  один раз в `BindTabControls`. Нужны для БЕЗЫМЯННЫХ украшений внутри страниц
	 *  (`BoxIcon_Level`, `BtnBase`, `Popup_TalkBox_Red` и пр.): имена у них одинаковые на разных
	 *  страницах, различает их только индекс. Размер — шесть якорей плюс верхняя граница. */
	TArray<int32> ManagePageAnchor;
	void CollectManagePageAnchors();
	void SetManagePageVisible(int32 PageIdx, bool bVisible);
	void BindManagePageCaptions();                 // подписи шести страниц (`CmtyClan_Manage_BuildUI`)
	void RefreshRankPermPage();                    // порт `cUI_RefreshTabControlsVisibility@0xBC5F80`
	void RefreshGradePage();                       // порт `listUI_RefreshPage7Rows@0xBC3910`
	void ClearGradeRow(int32 Row);                 // порт `uiComboBox_ClearTextsAndSelect@0xBC0530`
	void RefreshMasterTransferPage();              // порт `uiTradeListPopulate5Rows@0xBC6210`
	void ClearMasterRow(int32 Row);                // порт `wnd_ClearThreeTexts@0xBC05D0`
	void RefreshEmblemPage();                      // порт `uiInvFilterPanelRefresh@0xBC6410` (частично)
	/** Порт `uiChannelListDispatch@0xBC6E00`: перерисовать ТОТ список, который сейчас на экране, —
	 *  состав клана (подрежим 0) либо страницу званий (подрежим 2 + страница 2). Оба списка делят
	 *  ОДНИ И ТЕ ЖЕ поля `Page`/`TotalPages` и контролы 14234/14235. */
	void DispatchListRefresh();
	/** Галочка страницы прав (`UCheckBox`): `bEnabled == false` — контрол заперт, как после
	 *  `set_o261@0xBC60B0` эталона (байт +609 = 1). */
	void SetCheck(const FString& Idc, bool bChecked, bool bEnabled);
	/** Значение контрола 14009 «Всего ОД». Отдельный метод, потому что у 14008 и 14009 в разметке
	 *  ОДИН И ТОТ ЖЕ `ID`, и узел значения зовётся в ассете иначе, чем `ID` (подробности — в
	 *  определении). До 02.09.2026 порт писал в несуществующее имя, и на экране висел текст
	 *  дизайнера «9,000,000,000». */
	void SetTotalPointsValue(const FString& S);

	void OnMemberList(const TArray<uint8>& Body);   // 4125 — порция списка членов
	void OnMemberListEnd();                         // 4126 — ЧЕСТНЫЙ конец списка (payload 0)
	void OnBaseInfo(const TArray<uint8>& Body);     // 4123 — инфо-блок клана, 191 б
	/** ★ 27.09.2026: 4131 (блок 191 б) и 4132 (@0 код + блок) — порт `recvTradeWindowStateAlt@0xBCB4E0`
	 *  и `uiTradeWindowResetState@0xBCB530`. Код 1 — блок заменяется целиком; код 2 — клан удалён,
	 *  блок сбрасывается в «клана нет». Обоснование — в объявлении делегата `OnClanBaseInfoPush`. */
	void OnBaseInfoPush(int32 Opcode, const TArray<uint8>& Body);
	void OnNotice(const TArray<uint8>& Body);       // 6512 — порция объявлений клана, 546 б
	void OnLeaveResult(const TArray<uint8>& Body);  // 4117 — ответ на 4116 (0 отказ / 1 успех)
	/** Порт `uiTradeWindowRefresh@0xBC3BF0`: разложить блок 4123 по контролам инфо-панели. */
	void RefreshClanInfoPanel();
	/** ★★★ АУДИТ 31.08.2026: РАСКЛАДКА 544-БАЙТОВОЙ ЗАПИСИ ОБЪЯВЛЕНИЯ **ДОКАЗАНА** — прежняя пометка
	 *  «не доказана» была ошибкой метода (смотрели не туда). Два независимых доказательства:
	 *   1) конструктор записи `uiSlotListCtxCtor@0xBC6760`:
	 *        `*this = 0;  memset(this+1, 0, 0x14);  memset(this+6, 0, 0x14);  memset(this+11, 0, 0x1F4);`
	 *      ⇒ 4 + 20 + 20 + 500 = 544 байта РОВНО, границы полей 0 / 4 / 24 / 44;
	 *   2) рисовальщик `uiMemberListPopulate@0xBC6490` читает ровно эти границы:
	 *        `rec+0`  — i32 ключ (сверяется с `wnd[483]` = wnd+0x78C — «раскрытое» объявление);
	 *        `rec+4`  — строка → `CmtyClan_Notice{NN}_T3` (`*(self + 5*row + 22)`);
	 *        `rec+24` — строка, ПЕРВЫЕ 10 СИМВОЛОВ → `..._T2` (`memcpy(buf, rec+24, 0xA); buf[10]=0`);
	 *        `rec+44` — строка → `..._T1` через `textTruncateWithEllipsis(…, 250)`;
	 *      а для записи с ключом == `wnd[483]`: 14089 `CMTY_CLAN_NOTICE_TEXT` ← `rec+44` целиком,
	 *      14027 `CMTY_CLAN_NOTICE_TEXT2` ← `sprintf("%s %s", rec+24, rec+4)`.
	 *  Строк-объявлений РОВНО 4 (цикл `for (i=0; i<4; ++i)`, ID `5*i + 14036`), кнопок «Очистить» 5
	 *  (цикл `for (i=0; i<5; ++i)`, ID `i + 14090`). */
	void RefreshNoticePanel();
	/** ★ Восстановить своё звание из байта [33] СВОЕЙ записи 4125 — лечение дефекта оригинала
	 *  «после релогина звание не восстанавливается ничем» (см. ключ `RestoreGradeFrom4125`). */
	void RestoreSelfGradeFromMemberList();   // звание — байт [33] записи члена клана (131 б)

	/** Поле ввода вкладки по idc (импортёр делает из JUI-контрола `EditBox` виджет `UEditableText`
	 *  и называет его ПО idc). Поиск идёт общей цепочкой `AsdaCommunityUi::Find`
	 *  (точное имя → `<idc>_grp` → `<idc>_<цифры>` → поддерево `<idc>_*`), поэтому мисс из-за
	 *  суффиксов импортёра исключён. `nullptr`, если узла нет или он не текстовое поле. */
	class UEditableText* FindEdit(const FString& Idc) const;

	/** ★★★ ЖЁСТКИЙ СТОП НАБОРА для двух полей вкладки — аналог `LimitText` разметки, который
	 *  эталон отдаёт настоящему Win32-полю (`EM_LIMITTEXT`). Значения берутся ИЗ РАЗМЕТКИ, не «на
	 *  глаз»: 14029 `CMTY_CLAN_NOITICE_WRITE` — `LimitText = "200"`, 14021 `Clan_MyIntroTxt` —
	 *  `LimitText = "64"` (`gamemain_jui.ini:193479` и `:189298`). В CP1251 символ = байт, поэтому
	 *  число символов и есть длина `strlen`, которой меряет сервер. Зовётся один раз из
	 *  `BindTabControls`; реле хранятся в `EditLimiters`, иначе GC съест их вместе с лимитом. */
	void SetupEditLimits();

	/** Показать/скрыть контрол 14101 `CMTY_CLAN_SLASH` — «нижнюю полосу» с пагинацией. Отдельный
	 *  метод нужен потому, что её дёргают ТРИ разных эталонных места: `uiSwitchPanelMode`,
	 *  `uiSelectPanelTab` и `sub_922772` (хвост отправки объявления). Гасится ВСЁ поддерево по
	 *  префиксам: пагинация 14230…14235 в разметке лежит ВНУТРИ 14101, а импортёр иерархию
	 *  расплющивает (закон `jui-uneven-node-layout`). */
	void SetBottomBarVisible(bool bVisible);

	FString Loc(int32 Id) const;
	FString LocTagged(int32 Id) const;   // ★ Дедуп №2, В1-B (U10a): было `LocRaw` — по смыслу это `AsdaNotice::LocTagged` (теги сохранены, маркеры [ss1]/[sd1] не тронуты)
	FString LocSlotStr(int32 Id, const FString& S) const;
	void    NoticeBox(const FString& Text) const;   // тултип-нотис = ONEBUT_MSGBOX 1500
	int32   SelfClanId() const;                     // зеркало CCharacter+0x968 (UAsdaWorldSubsystem)
	int32   IndexOf(int32 UserIndex, uint8 CharIndex) const;

	UPROPERTY(Transient) UUserWidget* CommWnd = nullptr;

	// ── Модель ──
	TArray<FAsdaClanMemberRecord> Members;   // контейнер `gap_694 + 64` эталона
	int32 Page       = 1;                    // `gap_7B4[4]` — текущая страница, 1-BASED
	int32 TotalPages = 1;                    // `gap_7B4[0]`
	int32 SelRow     = -1;                   // выбранная строка на странице
	int32 Mode       = 0;                    // подрежим 0/1/2

	/** Инфо-блок клана из 4123 (191 б). Пока `bValid == false`, панель показывает нули эталона. */
	FAsdaClanBaseInfo Info;
	/** Объявления клана из 6512, каждое — 544-байтовая запись «как пришла» (раскладка 0/4/24/44
	 *  доказана, см. `RefreshNoticePanel`). Эталон держит НЕ БОЛЕЕ ПЯТИ (`if (size > 5) pop_front`). */
	TArray<TArray<uint8>> Notices;
	static constexpr int32 kMaxNotices = 5;
	/** `CCommunityWnd+0x78C` — МАКСИМУМ числового ключа объявлений. У него ДВЕ роли (обе доказаны):
	 *  едет в 6511 как курсор (`HIWORD(v8[1]) = v9[966]`, то есть на провод уходит только u16) и
	 *  служит ключом «раскрытого» объявления в `uiMemberListPopulate`. */
	int32 NoticeCursor = 0;
	/** Число строк-объявлений подрежима 1 (эталонный цикл `for (i = 0; i < 4; ++i)`, ID `5*i + 14036`). */
	static constexpr int32 kNoticeRows = 4;
	/** ★ Ключ объявления, ПОКАЗАННОГО В КАЖДОЙ строке 0…3, и ключ «раскрытого» объявления —
	 *  снимаются в `RefreshNoticePanel` ровно там, где эталон рисует текст. `INDEX_NONE` = строка
	 *  пуста (у эталона это гейт `strlen(текст) != 0`, дизасм `00BC1AC6 call strlen`).
	 *  ★★★ ДЕФЕКТ ОРИГИНАЛА D-C6 (исправлен и помечен). Эталон в ветках 14091…14094 берёт запись
	 *  по АБСОЛЮТНОМУ номеру в списке (`channelLoopIdx == channelIndex`), а рисовальщик
	 *  `uiMemberListPopulate@0xBC6490` «раскрытую» запись в строки НЕ КЛАДЁТ (`else if (memberRow
	 *  < 4)`). Как только раскрытое объявление стоит в списке не последним, номера расходятся и
	 *  кнопка «Очистить» удаляет ЧУЖОЕ объявление. Здесь ключ берётся ровно у той записи, что
	 *  реально нарисована в строке, — расхождение исключено по построению.
	 *  ★★★ ДЕФЕКТ ОРИГИНАЛА D-C7 (исправлен и помечен). Гейт кнопки 14090 у эталона —
	 *  `wnd[483] != −1`, но `wnd+0x78C` это НАКОПИТЕЛЬ МАКСИМУМА ключей (`00BCC3F9`), стартующий
	 *  нулём: при нуле объявлений он равен 0, гейт проходит, и на сервер уходит 6515 с ключом 0.
	 *  Здесь ключ хранится ОТДЕЛЬНО и равен `INDEX_NONE`, пока раскрытого объявления нет. */
	int32 NoticeDetailKey = INDEX_NONE;
	int32 NoticeRowKeys[kNoticeRows] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	/** ★ Порт `CCommunityWnd+0x608` — выбранная страница панели «Предводитель» (0..5).
	 *  Гейт входа в подрежим 2 зависит от НЕЁ: только при странице 3 эталон пускает по маске прав,
	 *  во всех остальных случаях — ТОЛЬКО глава клана (`uiSwitchPanelMode`, ветка `a2 == 2`).
	 *  ★ С 02.09.2026 страницы РЕАЛИЗОВАНЫ (`SelectManagePage`), и значение действительно
	 *  меняется; начальное 0 — как в эталоне (`CmtyClan_Manage_BuildUI` показывает страницу
	 *  `this[386]`, а она стартует нулём). */
	int32 ManageSubPage = 0;
	/** Идёт ли приём порций 4125 (взводится запросом, снимается честным 4126). */
	bool bMemberListLoading = false;

	AsdaCommunityUi::FSelFrame Sel;          // рамка 14110
	/** ★★★ ПРИЗНАК «КОНТРОЛЫ ПРИВЯЗАНЫ» — ЭТО САМ ВИДЖЕТ, А НЕ БУЛЕВ ФЛАГ.
	 *  БЫЛО (крит, найден 02.09.2026 во всех четырёх вкладках окна 6400): `bool bTabBound`.
	 *  Окно 6400 пересоздаётся при каждой смене карты (`UAsdaWindowSubsystem::EnsureWindow` →
	 *  `IsWindowLive`), а флаг переживал пересоздание и запрещал повторную привязку:
	 *  подписи не ставились, кнопки были мертвы, а на экран выходил текст-заглушка
	 *  дизайнера из ассета. Слабая ссылка сама обнуляется при сборке старого окна. */
	TWeakObjectPtr<UUserWidget> BoundWnd;
	/** Привязаны ли контролы ИМЕННО к этому виджету окна. Закрывает ВСЕ ранние
	 *  выходы подсистемы, включая сетевые пути: иначе приём 4123/4125 рисовал бы в дерево
	 *  уничтоженного окна. */
	bool IsBoundTo(const UUserWidget* W) const { return W != nullptr && BoundWnd.Get() == W; }
	bool bTabVisible = false;

	/** Реле-ограничители набора для полей 14021 и 14029 (см. `SetupEditLimits`). Держим ссылку:
	 *  `UAsdaTextLimiter` — обычный `UObject`, и без `UPROPERTY` его соберёт GC вместе с лимитом. */
	UPROPERTY(Transient) TArray<TObjectPtr<class UAsdaTextLimiter>> EditLimiters;

	/** ★★★ GC-ЯКОРЯ ОБРАБОТЧИКОВ КНОПОК ВКЛАДКИ (регрессия 02.09.2026).
	 *  Динамический делегат `UButton::OnClicked` держит реле СЛАБО (`TWeakObjectPtr` внутри
	 *  `FScriptDelegate`), а `NewObject<…>(this)` внутренний объект НЕ ЯКОРИТ. Без этого массива
	 *  первый же проход сборщика мусора убивает все реле, и кнопки вкладки МОЛЧА перестают
	 *  работать в произвольный момент (жалоба владельца «окно умирает в моменте»).
	 *  Тот же приём уже применён к `EditLimiters` и к `Relays` 30+ окон порта.
	 *  Чистится в `BindTabControls`: реле мёртвого дерева держать нельзя. */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Relays;

	FDelegateHandle HMemberList, HMemberListEnd, HBaseInfo, HNotice, HLeaveResult;
	FDelegateHandle HBaseInfoPush;   // ★ 27.09.2026: 4131/4132
	int32 InfoRevision = 0;          // растёт при каждом изменении `Info` (см. `GetBaseInfoRevision`)
	int32 Rx4131 = 0, Rx4132 = 0;    // счётчики для дампа
	// Счётчики для дампа состояния.
	int32 Rx4123 = 0, Rx4125 = 0, Rx4126 = 0, Rx6512 = 0, Rx4117 = 0, Tx4116 = 0, Tx6511 = 0;
	// Отправки трёх кнопок клан-панели: 6513 «объявление» (14031) и 6518 «заметка» (14023).
	int32 Tx6513 = 0, Tx6518 = 0;
};
