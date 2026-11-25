// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCommunityClanSubsystem_List.cpp — ★ R3-c 12.11.2026 (разрез AsdaCommunityClanSubsystem.cpp, дословный перенос): список членов, страницы, действия (выход, объявления, заметка).
//   Общее для частей — в AsdaCommunityClanSubsystem_Private.h; шапка и реверс — в AsdaCommunityClanSubsystem.cpp.
#include "UI/AsdaCommunityClanSubsystem.h"

#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Components/EditableText.h"        // поля ввода 14021 «заметка» и 14029 «объявление»
#include "UI/AsdaUiHelpers.h"   // ★ 31.10.2026: AsdaUi::AnchorRelay — общий GC-якорь реле
#include "AsdaCommunityClanSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCommunityClanSubsystem*.cpp

namespace {
	// D-C3 (★): эталонная ветка `if (флаг) { if (флаг == 1) {…} }` при значении флага > 1 НЕ ставит
	//   текст вовсе — контрол остаётся пустым. Сервер сейчас шлёт только 0/1, ветка мертва ⇒
	//   дефолт «как в оригинале».
	ASDA_DEV_CVAR(int32, CVarInGameAnyNonZero,
		TEXT("asda.Community.Clan.InGameAnyNonZero"), 0,
		TEXT("0 (по умолчанию) = как в оригинале: «В игре» печатается только при флаге РОВНО 1,\n")
		TEXT("при флаге > 1 строка остаётся пустой (дефект D-C3).\n")
		TEXT("1 = любой ненулевой флаг считается «в игре»."),
		ECVF_Default);

	constexpr int32 kLocHavePt    = 17007;   // «ОД в наличии» — строка списка
	constexpr int32 kLocSpentPt   = 17008;   // «Потрачено ОД» — строка списка

	// Модалка YES/NO подтверждения удаления объявления: эталон
	//   `CItem_BuildTooltipText(g_pItemTooltipUI, 1, 17182, loc 17182, 1)`; «Да» — ветка
	//   `case 0x431E` (0x431E = 17182) диспетчера модалок → `netSendAuthToken6515@0xBCC470`.
	constexpr int32 kLocNoticeDelAsk = 17182;

	constexpr int32 kLocLeaveAsk  = 12432;   // «Ты действительно хочешь уйти из клана?» — МОДАЛКА (код 45)
	// Гейт клиента `00BCA639`: при звании 1 (глава) 4116 НЕ шлётся, показывается этот нотис.
	constexpr int32 kLocLeaveMaster = 12433; // «Глава клана не может покинуть клан»

	constexpr int32 kLocInGame    = 12227;   // «<FC=FF93FF93>В игре</F>»
	constexpr int32 kLocOutGame   = 12228;   // «<FC=FFABABAB>Не в игре</F>»
	constexpr int32 kLocSexBase   = 11701;   // loc 11701 + пол → «Все» / «М» / «Ж»
	constexpr int32 kLocJobBase   = 12300;   // loc 12300 + код класса
}

// ★ ПОРТ `uiChannelListDispatch@0xBC6E00` — ЕДИНАЯ ТОЧКА «перерисуй текущий список».
//   Эталон ДОСЛОВНО:
//     if (currentSubTab) { if (currentSubTab == 2 && gap_608 == 2) listUI_RefreshPage7Rows(this); }
//     else               Recv_ServerChannelList(this);
//   То есть перерисовываются РОВНО два списка: состав клана (подрежим 0) и страница званий
//   панели управления (подрежим 2 + страница 2); в остальных состояниях — ничего.
void UAsdaCommunityClanSubsystem::DispatchListRefresh()
{
	if (Mode == 0)                            { RefreshMemberList(); }
	else if (Mode == 2 && ManageSubPage == 2) { RefreshGradePage();  }
}

// ═══════════════════════════ СПИСОК ЧЛЕНОВ ════════════════════════════════════════════════════
void UAsdaCommunityClanSubsystem::RefreshMemberList()
{
	if (!IsBoundTo(CommWnd)) { return; }
	using namespace AsdaCommunityUi;

	// Порт `Recv_ServerChannelList@0xBC3160` (миснейм; это RefreshMemberList):
	//   lastIdx = count − 1; totalPages = lastIdx/6, при >0 ещё +1, иначе 1; страница 1-BASED.
	const int32 LastIdx = Members.Num() - 1;
	TotalPages = LastIdx / kRows;
	if (TotalPages > 0) { ++TotalPages; } else { TotalPages = 1; }
	Page = FMath::Clamp(Page, 1, TotalPages);

	const int32 Base = kRows * (Page - 1);
	for (int32 Row = 0; Row < kRows; ++Row)
	{
		const int32 Abs = Base + Row;
		if (Members.IsValidIndex(Abs)) { FillRow(Row, &Members[Abs]); }
		else                           { ClearRow(Row); }
		SyncRowHit(CommWnd, FString::Printf(TEXT("CMTY_CLAN_LIST%d"), Row + 1));
	}

	if (SelRow < 0 || !Members.IsValidIndex(Base + SelRow)) { Sel.Hide(); }
	else
	{
		// ★ ИСПРАВЛЕНО АУДИТОМ: эталон НЕ берёт смещение из разметки, он считает его в рантайме —
		//   `uiUpdateSlotCursor@0xBC3E50`: `Class_GetVec2_74(wnd[10*row + 289]); SetField74_75(wnd[269],
		//   pos.x − 2, pos.y − 2)`, где `wnd[10*row+289]` = контрол строки (`0x484 + 40*row`), а
		//   `wnd[269]` = 0x434 = рамка 14110. Смещение РОВНО −2/−2, а не −1/−1.
		Sel.MoveToRow(CommWnd, FString::Printf(TEXT("CMTY_CLAN_LIST%d"), SelRow + 1), FVector2D(-2.f, -2.f));
	}

	SetTxt(CommWnd, TEXT("CMTY_CLAN_PAGE1"), FString::FromInt(Page));        // 14234
	SetTxt(CommWnd, TEXT("CMTY_CLAN_PAGE2"), FString::FromInt(TotalPages));  // 14235
}

void UAsdaCommunityClanSubsystem::ClearRow(int32 Row)
{
	// Порт `uiPanelClearAllText`: все подписи строки очищаются, иконка «в сети» гаснет.
	using namespace AsdaCommunityUi;
	const int32 N = Row + 1;
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMNAME%d"), N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMTIT%d"),  N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_DAPoints%d"), N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMLV%d"),   N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMJOB%d"),  N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_MemSex%d"),   N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMON%d"),   N), FString());
	// ★ Контрол «+8» строки назван НЕОДНОРОДНО: у строки 1 это `CmtyClan_PAPoints1`, у строк 2..6 —
	//   `CMTY_CLAN_MEMSTUS{n}` (доказательство — цикл `uiBindRewardSlotControls` по формуле base+8).
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_PAPoints%d"), N), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMSTUS%d"), N), FString());
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMON%d_On"), N), false);
}

void UAsdaCommunityClanSubsystem::FillRow(int32 Row, const FAsdaClanMemberRecord* Rec)
{
	if (!Rec) { ClearRow(Row); return; }
	using namespace AsdaCommunityUi;
	const int32 N = Row + 1;

	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMNAME%d"), N), Rec->Name());
	// Звание: loc 17046 − звание (1 = «Глава клана» 17045 … 5 = «Новичок» 17041).
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMTIT%d"), N), Loc(kLocGradeBase - (int32)Rec->Grade()));
	// «ОД в наличии: N»
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_DAPoints%d"), N),
		FString::Printf(TEXT("%s: %d"), *Loc(kLocHavePt), Rec->Points()));
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMLV%d"), N), FString::FromInt((int32)Rec->Level()));
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMJOB%d"), N), Loc(kLocJobBase + (int32)Rec->JobForCaption()));
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_MemSex%d"), N), Loc(kLocSexBase + (int32)Rec->Sex()));

	// «В игре - Nch Карта» / «Не в игре» + иконка «в сети».
	const uint8 Flag = Rec->InGame();
	const bool bInGame = (CVarInGameAnyNonZero.GetValueOnGameThread() != 0) ? (Flag != 0) : (Flag == 1);
	if (Flag != 0 && !bInGame)
	{
		// ★ Дефект оригинала D-C3: флаг > 1 не попадает ни в одну ветку — строка остаётся пустой.
		UE_LOG(LogAsdaClanTab, Warning,
			TEXT("[клан-вкладка] флаг «в игре» = %d (не 0/1): строка статуса пуста — дефект D-C3"), (int32)Flag);
		SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMON%d"), N), FString());
	}
	else if (bInGame)
	{
		FString MapName;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
			{
				MapName = LM->GetMapDisplayName((int32)Rec->MapId()).ToString();
			}
		}
		SetTaggedTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMON%d"), N),
			FString::Printf(TEXT("%s - %dch %s"), *LocTagged(kLocInGame), (int32)Rec->Channel() + 1, *MapName));
	}
	else
	{
		SetTaggedTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMON%d"), N), LocTagged(kLocOutGame));
	}
	SetVisByPrefix(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMON%d_On"), N), bInGame);

	// ★ Дефект оригинала D-C2 (★★): «Потрачено ОД» эталон печатает КОНСТАНТОЙ 0 —
	//   `sprintf(buf, "%s: 0", loc 17008)`. Серверное поле для этого значения не выведено
	//   (байты 34..37 записи клиентом не используются, семантика не доказана, отчёт §6 п.2),
	//   поэтому «чинить» тут нечем — воспроизводим 1:1 и помечаем.
	const FString Spent = FString::Printf(TEXT("%s: 0"), *Loc(kLocSpentPt));
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_PAPoints%d"), N), Spent);
	SetTxt(CommWnd, FString::Printf(TEXT("CMTY_CLAN_MEMSTUS%d"), N), Spent);
}

// ═══════════════════════════ ДЕЙСТВИЯ ═════════════════════════════════════════════════════════
void UAsdaCommunityClanSubsystem::SelectRow(int32 Row)
{
	if (Row < 0 || Row >= kRows) { return; }
	const int32 Abs = kRows * (Page - 1) + Row;
	SelRow = Members.IsValidIndex(Abs) ? Row : -1;
	RefreshMemberList();
}

// ★ ИСПРАВЛЕНО АУДИТОМ: эталон (`uiPanel_HandleCommand`, ветки 0x3796…0x3799 = 14230…14233) меняет
//   ТОЛЬКО номер страницы `wnd[494]` и зовёт `uiChannelListDispatch` → `Recv_ServerChannelList`.
//   Выбранную строку он НЕ сбрасывает и рамку 14110 не трогает вовсе. Порт сбрасывал `SelRow = -1`
//   на каждой кнопке — это была отсебятина, снята.
// ★ ИСПРАВЛЕНО 02.09.2026: после смены номера страницы эталон зовёт НЕ `Recv_ServerChannelList`,
//   а диспетчер `uiChannelListDispatch@0xBC6E00` (`AsdaStory_game.c:233880`) — он выбирает, какой
//   именно список перерисовать: состав клана (подрежим 0) или страницу званий панели управления
//   (подрежим 2 + страница 2). Обе пользуются ОДНИМИ И ТЕМИ ЖЕ полями `wnd[493]`/`wnd[494]`
//   (`TotalPages`/`Page`), поэтому пагинация у них общая — см. `DispatchListRefresh`.
void UAsdaCommunityClanSubsystem::PageFirst() { Page = 1;                             DispatchListRefresh(); }   // 14230
void UAsdaCommunityClanSubsystem::PagePrev()  { if (Page > 1)          { --Page; }    DispatchListRefresh(); }   // 14231
void UAsdaCommunityClanSubsystem::PageNext()  { if (Page < TotalPages) { ++Page; }    DispatchListRefresh(); }   // 14232
void UAsdaCommunityClanSubsystem::PageLast()  { Page = TotalPages;                    DispatchListRefresh(); }   // 14233

void UAsdaCommunityClanSubsystem::ActionLeaveClan()
{
	// Порт ветки 0x36D0 = 14032: при наличии клана — имя клана в слот 0 и модальное YES/NO
	//   с кодом 45 и текстом loc 12432; «Да» → `netSendNameConfirmPacket@0xBCA620` = опкод 4116.
	if (SelfClanId() < 0) { return; }

	// ★★★ ИСПРАВЛЕНО АУДИТОМ 31.08.2026 — ПОРЯДОК ГЕЙТА. Раньше порт проверял «я глава» ДО показа
	//   модалки, и глава вообще не видел вопроса 12432. В эталоне это не так:
	//     · `uiPanel_HandleCommand`, ветка `case 0x36D0u` (= 14032) — НИКАКОЙ проверки звания нет,
	//       только «клан есть» (`(i16)char+0x968 != −1`), затем слот 0 ← имя клана и модалка
	//       `CItem_BuildTooltipText(g_pItemTooltipUI, 1, 45, loc 12432, 1)`;
	//     · проверка звания живёт в ОТПРАВИТЕЛЕ, то есть уже ПОСЛЕ ответа «Да»:
	//       `netSendNameConfirmPacket@0xBCA620`, дизасм `00BCA639 movzx eax, byte [eax+0x97B];
	//        cmp eax,1; jne …` → нотис 0x3091 = 12433 и пакет НЕ уходит.
	//   Значит глава видит: вопрос 12432 → «Да» → нотис 12433. Воспроизводим ровно так (гейт ниже,
	//   внутри колбэка подтверждения). Зеркальный гейт есть и на сервере
	//   (`ClanMemberLeaveReq_0@0x403930`: `CUser+99115 == 1` → отказ 4117 с кодом 0).

	// ★ 1:1 эталон: имя клана в слот 0 берётся ТОЛЬКО из блока 4123 (`(char*)pPanel + 1558`, то есть
	//   блок+2). Прежний фолбэк на текст контрола 14003 был отсебятиной — снят.
	const FString ClanName = Info.bValid ? Info.ClanName() : FString();

	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(LocSlotStr(kLocLeaveAsk, ClanName), [this](bool bYes)
	{
		if (!bYes) { return; }
		// ★ 1:1 `netSendNameConfirmPacket@0xBCA620`: гейт «глава» СРАБАТЫВАЕТ ЗДЕСЬ, после «Да».
		if (SelfGrade() == 1)
		{
			// ★ 27.09.2026 (`friend_gate_12435.md`): перед нотисом эталон зовёт `itemBuyPanelRefreshOrHide(g_pItemTooltipUI)`
			//   (network.c:12240) — снять верхнюю запись окон-сообщений или спрятать панели. Своя запись (12432) к этому
			//   моменту уже снята ответом «Да», значит уходит СЛЕДУЮЩАЯ (или прячутся панели) — как у эталона.
			if (UAsdaWindowSubsystem* WM2 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
			{
				WM2->DropTopMsgBoxBeforeNotice(TEXT("клан-вкладка 14032 → 4116: глава (network.c:12240)"));
			}
			NoticeBox(Loc(kLocLeaveMaster));   // 12433 «Глава клана не может покинуть клан»
			UE_LOG(LogAsdaClanTab, Log,
				TEXT("[клан-вкладка] 4116 не отправлен: я глава (звание 1) — нотис 12433 (1:1 00BCA639)"));
			return;
		}
		UGameInstance* GI2 = GetGameInstance();
		UAsdaNetSubsystem* N = GI2 ? GI2->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaWorldSubsystem* WS = GI2 ? GI2->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!N || !WS) { return; }
		// payload(24): @0 i32 мой charId (`char+0x41C`) · @4 char[20] моё имя (`char+0x420`).
		//   ★ charId (i32) ≠ userNum (i16): сервер сверяет тело с `CUser+372`, ошибка = молчание.
		if (N->SendClanLeaveReq4116(N->SelectedCharId, WS->GetSelfCharName())) { ++Tx4116; }
	}, 0.f, 2288, 2289, /*очередь: эталон (1, 45) ui_core.c:15329*/45, 1);
}

// 14025 «Обновить» и вход в подрежим 1 — оба зовут 6511 (эталон `netSendAntiCheatTokenPacket`).
void UAsdaCommunityClanSubsystem::ActionReadNotices()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!N) { return; }
	const int32 ClanId = Info.bValid ? Info.ClanIndex() : SelfClanId();
	if (ClanId <= 0)
	{
		UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] 6511 не отправлен: клан-индекс неизвестен (%d)"), ClanId);
		return;
	}
	// Курсор — `CCommunityWnd+0x78C` эталона: максимум числового ключа уже полученных объявлений.
	if (N->SendClanNoticeReadReq6511(N->SelectedCharId, ClanId, NoticeCursor)) { ++Tx6511; }
}

// ★★★ 14090…14094 «Очистить» → модалка 17182 → опкод 6515. Разбор веток эталона и раскладка
//   пакета — в объявлении (`AsdaCommunityClanSubsystem.h`). До 31.08.2026 кнопки были мертвы.
void UAsdaCommunityClanSubsystem::ActionDeleteNotice(int32 Slot)
{
	if (!IsBoundTo(CommWnd)) { return; }

	// ── Источник «ключа записи» (эталонный `wnd+0x79C`) ──
	int32 Key = INDEX_NONE;
	if (Slot == 0)
	{
		// Ветка `case 0x370A`: ключ «раскрытого» объявления. Гейт эталона — `wnd[483] != −1`;
		//   у нас вместо накопителя максимума лежит ключ реально показанной записи (лечение D-C7).
		Key = NoticeDetailKey;
	}
	else
	{
		const int32 Row = Slot - 1;
		if (Row < 0 || Row >= kNoticeRows) { return; }
		// Ветка `0x370B…0x370E`: гейт «в строке есть текст» (`00BC1AC6 call strlen; 00BC1AD1 jb`)
		//   у нас выражен ключом строки: он ставится ровно там, где эталон пишет текст (D-C6).
		Key = NoticeRowKeys[Row];
	}
	if (Key == INDEX_NONE)
	{
		UE_LOG(LogAsdaClanTab, Log,
			TEXT("[клан-вкладка] «Очистить» слот %d: объявления в этой позиции нет — 6515 не шлём ")
			TEXT("(1:1 гейты `strlen(текст)` и `ключ != −1`)"), Slot);
		return;
	}

	// Гейт прав — 1:1 `sub_92E478(pPanel, звание, 2) || sub_92CE3E()`, то есть
	//   `ClanRankPermission(звание, 2) ‖ IsClanMaster`; отказ = ОДНОКНОПОЧНЫЙ нотис 17006.
	if (!SelfHasPermission(kPermManage))
	{
		NoticeBox(Loc(kLocForbidden));
		UE_LOG(LogAsdaClanTab, Log,
			TEXT("[клан-вкладка] «Очистить» запрещено: моё звание %d, права на объявления нет (нотис 17006)"),
			SelfGrade());
		return;
	}

	const int32 ClanId = Info.bValid ? Info.ClanIndex() : SelfClanId();
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Модалка YES/NO с кодом 17182 (эталон: `CItem_BuildTooltipText(…, 1, 17182, loc 17182, 1)`).
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(kLocNoticeDelAsk), [this, Key, ClanId](bool bYes)
	{
		if (!bYes) { return; }
		UGameInstance* GI2 = GetGameInstance();
		UAsdaNetSubsystem* N = GI2 ? GI2->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!N) { return; }
		// payload(10): @0 i32 charId (`g_LocalPlayer+1052`) · @4 u16 clanIndex (`wnd[778]`) ·
		//   @6 i32 ключ записи (`wnd[487]`). ★ charId (i32) ≠ userNum (i16).
		N->SendClanNoticeDelete6515(N->SelectedCharId, ClanId, Key);
		// ⚠ 1:1: локально эталон НИЧЕГО не чистит и перечитывать список не просит — строка
		//   остаётся на экране до следующего 6511 (кнопка 14025 «Обновить»). Отсебятины не вносим.
	}, 0.f, 2288, 2289, /*очередь: эталон (1, 17182) ui_core.c:15339*/17182, 1);
}

// ═══════════ ТРИ КНОПКИ КЛАН-ПАНЕЛИ: 14023 «Сохранить», 14031 «ОК», 14030 «Отмена» ════════════
//   Полный разбор каждой ветки эталона — в объявлениях (`AsdaCommunityClanSubsystem.h`).
//   ★ ОБЩЕЕ ДЛЯ ВСЕХ ТРЁХ: у клиента гейтов НЕТ ВООБЩЕ. Ни прав в клане, ни минимальной длины,
//     ни проверки на пустую строку — диспетчер `uiPanel_HandleCommand` зовёт отправитель сразу.
//     Все проверки живут на сервере, а его отказы (6514 и 6519) эталон принимает ПУСТЫМИ
//     обработчиками (`return a1;`) — то есть на отказ клиент не реагирует ничем. Придумывать
//     реакцию нельзя (закон `never-invent-only-original`), поэтому здесь только журнал.
void UAsdaCommunityClanSubsystem::ActionSaveMyMemo()
{
	if (!IsBoundTo(CommWnd)) { return; }
	UEditableText* Edit = FindEdit(TEXT("Clan_MyIntroTxt"));   // 14021 — источник текста заметки
	if (!Edit)
	{
		UE_LOG(LogAsdaClanTab, Warning,
			TEXT("[клан-вкладка] 14023 «Сохранить»: поле 14021 `Clan_MyIntroTxt` в разметке не найдено"));
		return;
	}
	const FString Text = Edit->GetText().ToString();

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!N) { return; }
	// Индекс клана — тот же источник, что у 6511/6515: блок 4123, иначе зеркало `CCharacter+0x968`.
	//   (Эталон берёт его из `wnd+1556` — это и есть первое поле сохранённого блока 4123.)
	const int32 ClanId = Info.bValid ? Info.ClanIndex() : SelfClanId();
	// Апостроф → обратная кавычка (`chat.c:5777-5783`) и обрезку по 64 б (`:5784-5785`) делает
	//   САМ отправитель `UAsdaNetSubsystem::SendClanMemoWrite6518` — здесь их дублировать нельзя.
	if (N->SendClanMemoWrite6518(N->SelectedCharId, ClanId, Text)) { ++Tx6518; }
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] 14023 «Сохранить»: заметка (%d симв.) → 6518, клан %d ")
		TEXT("(сервер примет ≤ 64 б; пустая строка законна и СТИРАЕТ прежнюю заметку)"),
		Text.Len(), ClanId);

	// 1:1 хвост `chatSendMessage_C7D0` (`chat.c:5798`): сразу после отправки поле ОЧИЩАЕТСЯ.
	//   Делается БЕЗУСЛОВНО — эталон результата отправки не смотрит (функция `void`).
	Edit->SetText(FText::GetEmpty());
}

void UAsdaCommunityClanSubsystem::ActionWriteNotice()
{
	if (!IsBoundTo(CommWnd)) { return; }
	UEditableText* Edit = FindEdit(TEXT("CMTY_CLAN_NOITICE_WRITE"));   // 14029 — текст объявления
	if (!Edit)
	{
		UE_LOG(LogAsdaClanTab, Warning,
			TEXT("[клан-вкладка] 14031 «ОК»: поле 14029 `CMTY_CLAN_NOITICE_WRITE` в разметке не найдено"));
		return;
	}
	const FString Text = Edit->GetText().ToString();

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!N) { return; }
	const int32 ClanId = Info.bValid ? Info.ClanIndex() : SelfClanId();
	// Апостроф → обратная кавычка (`chat.c:5726-5732`), текст по **+50** (`:5733`) и обрезку по
	//   500 б (`:5734`) делает САМ отправитель `UAsdaNetSubsystem::SendClanNoticeWrite6513`.
	if (N->SendClanNoticeWrite6513(N->SelectedCharId, ClanId, Text)) { ++Tx6513; }
	// Гейтом это НЕ становится (эталон шлёт всегда) — но два серверных отказа стоит видеть в логе:
	//   пустой текст (`strlen` обязан быть 1…500) и отсутствие права `Authority & 2` по званию.
	if (Text.IsEmpty())
	{
		UE_LOG(LogAsdaClanTab, Warning,
			TEXT("[клан-вкладка] 14031 «ОК»: текст ПУСТ — сервер отклонит (нужно 1…500 б) и ответит ")
			TEXT("6514; эталон шлёт пакет всё равно, повторяем 1:1"));
	}
	if (!SelfHasPermission(kPermManage))
	{
		UE_LOG(LogAsdaClanTab, Warning,
			TEXT("[клан-вкладка] 14031 «ОК»: у моего звания %d нет права «объявления» (Authority & 2) — ")
			TEXT("сервер ответит отказом 6514; клиентского гейта у эталона в этой ветке НЕТ"),
			SelfGrade());
	}
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] 14031 «ОК»: объявление (%d симв.) → 6513, клан %d"), Text.Len(), ClanId);

	// 1:1 `sub_922772@0x922772` → `uiClanDelete_FocusInput@0xBC61B0` (★ МИСНЕЙМ), ровно три действия
	//   и все БЕЗУСЛОВНО (`ui_core.c:15322` вызывается сразу за отправителем, результата не смотрит).
	Edit->SetText(FText::GetEmpty());   // `CControl_SetText(wnd[173], "")`
	Edit->SetKeyboardFocus();           // `CControl_SetFocus(wnd[173], 1)`
	SetBottomBarVisible(false);         // `CControl_SetVisible(wnd[265], 0)` — 14101 `CMTY_CLAN_SLASH`
}

void UAsdaCommunityClanSubsystem::ActionCancelNotice()
{
	if (!IsBoundTo(CommWnd)) { return; }
	UEditableText* Edit = FindEdit(TEXT("CMTY_CLAN_NOITICE_WRITE"));   // 14029
	if (!Edit)
	{
		UE_LOG(LogAsdaClanTab, Warning,
			TEXT("[клан-вкладка] 14030 «Отмена»: поле 14029 `CMTY_CLAN_NOITICE_WRITE` не найдено"));
		return;
	}
	// 1:1 `ui_core.c:15317` — ровно одна строка: очистить поле ввода. Ни фокуса, ни скрытия полосы
	//   14101, ни закрытия панели эталон здесь НЕ делает (это хвост только у ветки 14031).
	Edit->SetText(FText::GetEmpty());
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] 14030 «Отмена»: поле объявления очищено"));
}
