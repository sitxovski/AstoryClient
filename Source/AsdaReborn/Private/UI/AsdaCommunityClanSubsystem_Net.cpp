// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCommunityClanSubsystem_Net.cpp — ★ R3-c 12.11.2026 (разрез AsdaCommunityClanSubsystem.cpp, дословный перенос): приём: список членов, сведения о клане, объявления, результат выхода; права, поля ввода, тексты, дамп.
//   Общее для частей — в AsdaCommunityClanSubsystem_Private.h; шапка и реверс — в AsdaCommunityClanSubsystem.cpp.
#include "UI/AsdaCommunityClanSubsystem.h"

#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaCommunitySubsystem.h"

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Login/AsdaLoginData.h"
#include "Login/AsdaButtonRelay.h"          // UAsdaTextLimiter — мостик OnTextChanged (стоп набора)
#include "Blueprint/WidgetTree.h"           // обход дерева окна: границы страниц панели управления
#include "Components/TextBlock.h"
#include "Components/EditableText.h"        // поля ввода 14021 «заметка» и 14029 «объявление»
#include "UI/AsdaUiHelpers.h"   // ★ 31.10.2026: AsdaUi::AnchorRelay — общий GC-якорь реле
#include "UI/AsdaJuiComboBox.h"  // ★ 01.11.2026: списки званий страницы «Ранги»
#include "UI/AsdaHotbarSubsystem.h"   // ★ 01.11.2026: значок 13 (предложение стать главой)
#include "AsdaCommunityClanSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCommunityClanSubsystem*.cpp

namespace {
	// ★★★ ДЕФЕКТ ОРИГИНАЛА (доказан полным сканом писателей `char+0x97B`, `clan_soulmate_gaps.md`
	//   §1.2 и «Что осталось недостижимым» п.3): писателей звания РОВНО ТРИ — 4105 (ставит 1 при
	//   создании клана), 4133 (ставит 0 при выходе) и 6523 (ставит присланное значение. Ни один не
	//   привязан ко ВХОДУ В ИГРУ, и сервер 6523 на логине не шлёт (перечислены все три места его
	//   сборки: GameServer.c:3832, :3856, :4230). ⇒ после релогина глава клана локально получает
	//   звание 0, и клиентский гейт `звание == 1` перестаёт защищать кнопку «выйти из клана».
	//   ЛЕЧЕНИЕ НЕ ВЫДУМАНО: сервер кладёт то же самое поле в байт `[33]` записи 4125 (§1.6), и
	//   своя запись в списке членов есть всегда. Дефолт = 1 (лечить), потому что ЭТОТ ЖЕ порт
	//   теперь включает гейт прав подрежима «Предводитель»: с нулевым званием он запер бы главу
	//   клана в его собственном окне — регрессия хуже исходного дефекта.
	ASDA_DEV_CVAR(int32, CVarRestoreGrade,
		TEXT("asda.Community.Clan.RestoreGradeFrom4125"), 1,
		TEXT("1 (по умолчанию) = своё звание в клане восстанавливается из байта [33] СВОЕЙ записи\n")
		TEXT("списка 4125 (лечение дефекта оригинала: после релогина звание не приходит ничем).\n")
		TEXT("0 = как в оригинале: до первого 6523 звание остаётся 0."),
		ECVF_Default);

	// ★★★ ЛИМИТЫ НАБОРА — ИЗ РАЗМЕТКИ, НЕ «НА ГЛАЗ» (закон `never-invent-only-original`).
	//   `gamemain_jui.ini:193479` у 14029 `CMTY_CLAN_NOITICE_WRITE`: `LimitText = "200"`;
	//   `gamemain_jui.ini:189298` у 14021 `Clan_MyIntroTxt`:          `LimitText = "64"`.
	//   ⚠ ДЕФЕКТ ОРИГИНАЛА, НЕ ЧИНИМ: сервер принимает объявление до 500 байт
	//     (`Recv_User_Notice_Write@0x4072C0`: `strlen(&body[50])` в 1…500), а разметка не даёт
	//     набрать больше 200. Это ограничение ОРИГИНАЛА — ревизия аудита прямо помечает его
	//     `[ОРИГИНАЛ]` и запрещает «лечить» (`14_soulmate_ревизия.md`, раздел «Сопутствующее»).
	//   У заметки 64 совпадает с серверным потолком `strlen(&body[6]) ≤ 64` — расхождения нет.
	//   В CP1251 (кодировка клиента) каждый представимый символ — РОВНО один байт, кириллица тоже,
	//   поэтому лимит в символах и есть лимит в байтах, которым меряет сервер.
	constexpr int32 kLimitNoticeText = 200;
	constexpr int32 kLimitMemoText   = 64;

	// ★ «Нижняя полоса» 14101 `CMTY_CLAN_SLASH` и всё, что лежит ВНУТРИ неё в разметке
	//   (пагинация 14230…14235). Импортёр иерархию расплющивает, поэтому гасим по префиксам
	//   (закон `jui-uneven-node-layout`). Список вынесен из `ApplyModeVisibility` в общее место:
	//   ту же полосу дёргает хвост отправки объявления `sub_922772` (кнопка 14031).
	static const TCHAR* kBottomBar[] = {
		TEXT("CMTY_CLAN_SLASH"), TEXT("CMTY_CLAN_PAGE"), TEXT("CMTY_CLAN_NEXTBTN"),
		TEXT("CMTY_CLAN_PREBTN"), TEXT("CMTY_CLAN_ENDBTN"), TEXT("CMTY_CLAN_STATBTN"),
	};

	// ★★★ КОНТРОЛ 14009 «Всего ОД» — ИМЯ УЗЛА В АССЕТЕ НЕ СОВПАДАЕТ С `ID` РАЗМЕТКИ.
	//   ФАКТ ДАННЫХ: у 14008 (подпись «Всего ОД») и у 14009 (его значение) в `gamemain_jui.ini`
	//   ОДИН И ТОТ ЖЕ `ID = "CmtyClan_TotalPointsTxt"` (строки 186971 и 186995) — дизайнер
	//   скопировал контрол и не переименовал. Импортёр (`Tools/asda_jui_generic_umg.py`,
	//   `uniq_name`) при повторе имени приписывает `_1`, а порядок обхода в JUI — родитель 14008,
	//   затем ребёнок 14009. ⇒ в ассете `WBP_COMMUNITY_WIN` узел 14008 зовётся
	//   `CmtyClan_TotalPointsTxt`, а узел 14009 — `CmtyClan_TotalPointsTxt_1`.
	//   ПОСЛЕДСТВИЕ ДО ПРАВКИ: порт писал значение в несуществующий `CmtyClan_TotalPoints`
	//   (`SetTxt` молча выходит), и на экране оставался ТЕКСТ ДИЗАЙНЕРА «9,000,000,000»
	//   из разметки — это и увидел владелец.
	//   ⚠ Имя со суффиксом `_1` — не «магия»: это детерминированный результат `uniq_name` для
	//     ВТОРОГО вхождения имени, и переживает перегенерацию окна (порядок обхода не меняется).
	const TCHAR* const kIdcTotalPointsValue = TEXT("CmtyClan_TotalPointsTxt_1");

	// Ответ 4117 (`uiInventoryItemNameResultHandler@0xBCA870`): код 0 → 12424, код 1 → 12425.
	constexpr int32 kLocLeaveFail = 12424;   // отказ сервера
	constexpr int32 kLocLeaveOk   = 12425;   // успешный выход
}

// ═══════════════════════════ ПРИЁМ СЕТИ ═══════════════════════════════════════════════════════
int32 UAsdaCommunityClanSubsystem::IndexOf(int32 UserIndex, uint8 CharIndex) const
{
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		if (Members[i].UserIndex() == UserIndex && Members[i].CharIndex() == CharIndex) { return i; }
	}
	return INDEX_NONE;
}

// 4125 — порция списка членов. Порт `recvParseServerChannelList@0xBCB060`: записи по 131 байту.
void UAsdaCommunityClanSubsystem::OnMemberList(const TArray<uint8>& Body)
{
	int32 Ofs = 0;
	if ((Body.Num() % FAsdaClanMemberRecord::kSize) != 0
		&& Body.Num() > 9 && ((Body.Num() - 9) % FAsdaClanMemberRecord::kSize) == 0)
	{
		Ofs = 9;   // тело пришло с 9-байтовой шапкой пакета — читаем с +9, как эталон
		UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] 4125: тело с шапкой — читаю с +9 (len=%d)"), Body.Num());
	}

	int32 Added = 0, Updated = 0;
	for (; Ofs + FAsdaClanMemberRecord::kSize <= Body.Num(); Ofs += FAsdaClanMemberRecord::kSize)
	{
		FAsdaClanMemberRecord R;
		FMemory::Memcpy(R.B, Body.GetData() + Ofs, FAsdaClanMemberRecord::kSize);
		const int32 Idx = IndexOf(R.UserIndex(), R.CharIndex());
		if (Idx != INDEX_NONE) { Members[Idx] = R; ++Updated; }
		else                   { Members.Add(R);   ++Added;   }
	}
	++Rx4125;
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] 4125: добавлено %d, обновлено %d, всего членов %d"),
		Added, Updated, Members.Num());
	RestoreSelfGradeFromMemberList();
	// ★ Не `RefreshMemberList`, а диспетчер: список членов 4125 питает ОБА списка вкладки —
	//   состав клана (подрежим 0) и страницу званий панели управления (подрежим 2, стр. 2).
	DispatchListRefresh();
}

// 4126 — ЧЕСТНЫЙ конец списка членов. payload 0: эталон (`case 0x101E`) тело не читает вовсе,
//   снимает флаг «запрос в пути» (`g_bChannelListReqPending = 0`) и перерисовывает список.
void UAsdaCommunityClanSubsystem::OnMemberListEnd()
{
	++Rx4126;
	bMemberListLoading = false;
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] 4126: список членов закрыт, всего %d"), Members.Num());
	RestoreSelfGradeFromMemberList();
	// ★ Не `RefreshMemberList`, а диспетчер: список членов 4125 питает ОБА списка вкладки —
	//   состав клана (подрежим 0) и страницу званий панели управления (подрежим 2, стр. 2).
	DispatchListRefresh();
}

// ★★★ ЛЕЧЕНИЕ ДЕФЕКТА ОРИГИНАЛА: звание после релогина. Подробности — у ключа `RestoreGradeFrom4125`.
//   Своя запись ищется ПО ИМЕНИ ПЕРСОНАЖА — ровно так эталон опознаёт локального игрока в 4133
//   (`Recv_AuthRelogin` сверяет имя из тела со своим). Ключ записи (userIndex+charIndex) для этого
//   не годится: в 4125 поле @0 — «id аккаунта в БД», а `SelectedCharId` порта — charId из 1017,
//   и их равенство НЕ ДОКАЗАНО, поэтому по нему не матчим.
void UAsdaCommunityClanSubsystem::RestoreSelfGradeFromMemberList()
{
	if (CVarRestoreGrade.GetValueOnGameThread() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!N || !WS) { return; }
	const FString MyName = WS->GetSelfCharName();
	if (MyName.IsEmpty()) { return; }

	for (const FAsdaClanMemberRecord& R : Members)
	{
		if (!R.Name().Equals(MyName, ESearchCase::IgnoreCase)) { continue; }
		const uint8 Grade = R.Grade();                      // байт [33] записи — то же поле сервера
		if (Grade != 0 && Grade != N->SelfClanRank)
		{
			UE_LOG(LogAsdaClanTab, Warning,
				TEXT("[клан-вкладка] ★ ДЕФЕКТ ОРИГИНАЛА ИСПРАВЛЕН: своё звание было %d, из байта [33] ")
				TEXT("своей записи 4125 восстановлено %d (эталон после релогина не восстанавливает ")
				TEXT("его ничем; откат — asda.Community.Clan.RestoreGradeFrom4125 0)"),
				(int32)N->SelfClanRank, (int32)Grade);
			N->SelfClanRank = Grade;
			ApplyModeVisibility(Mode);   // права подрежима 2 могли измениться
		}
		return;
	}
}

// 4123 — ИНФО-БЛОК КЛАНА (191 б). Порт `recvTradeWindowState@0xBCAE60`: блок сохраняется ЦЕЛИКОМ,
//   разбор — в `RefreshClanInfoPanel` (эталонный `uiTradeWindowRefresh@0xBC3BF0`).
void UAsdaCommunityClanSubsystem::OnBaseInfo(const TArray<uint8>& Body)
{
	if (Body.Num() < FAsdaClanBaseInfo::kSize)
	{
		UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] 4123: тело %d б, ожидалось %d — блок отброшен"),
			Body.Num(), FAsdaClanBaseInfo::kSize);
		return;
	}
	FMemory::Memcpy(Info.B, Body.GetData(), FAsdaClanBaseInfo::kSize);
	Info.bValid = true;
	++InfoRevision;   // ★ 27.09.2026: окно расширения 14450 перечитает числа
	++Rx4123;
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] 4123: клан %d «%s», глава «%s», уровень %d, ОД всего %d, осталось %d"),
		Info.ClanIndex(), *Info.ClanName(), *Info.ChiefName(), (int32)Info.LevelRaw() + 1,
		Info.TotalPoints(), Info.RemainPoints());
	RefreshClanInfoPanel();
	ApplyModeVisibility(Mode);   // маски прав пришли ⇒ гейт подрежима 2 мог измениться
	// ★ ИСПРАВЛЕНО 02.09.2026. `ApplyModeVisibility` показывает поддерево подрежима ЦЕЛИКОМ, в том
	//   числе значки «в сети» `CMTY_CLAN_MEMON{n}_On` — а их состояние ведёт `FillRow` по флагу
	//   [113] записи. Без перерисовки списка после показа у ВСЕХ шести строк загорался значок
	//   «в сети», даже у оффлайн-членов. Порядок «сначала видимость, потом данные» соблюдён и на
	//   пути 4125 (`RestoreSelfGradeFromMemberList` → `RefreshMemberList`).
	DispatchListRefresh();
	// Страница «Права членов клана» рисуется ИЗ МАСОК блока 4123 — если она открыта, обновляем.
	if (Mode == 2 && ManageSubPage == 1) { RefreshRankPermPage(); }
}

// ★ 27.09.2026: 4131 / 4132 — ИНФО-БЛОК КЛАНА «СВЕРХУ». Порт `recvTradeWindowStateAlt@0xBCB4E0`
//   (4131: `memcpy(wnd+0x614, тело, 0xBF)` + перерисовка) и `uiTradeWindowResetState@0xBCB530`
//   (4132: код 1 — то же с тела+1; код 2 — клан удалён, блок в «клана нет»; оба — перерисовка).
//   Колбэк в сеть не пишет (закон `no-reentrant-net-callbacks` соблюдён: только модель и панель,
//   как у соседнего `OnBaseInfo`).
void UAsdaCommunityClanSubsystem::OnBaseInfoPush(int32 Opcode, const TArray<uint8>& Body)
{
	if (Opcode == 4131)
	{
		++Rx4131;
		OnBaseInfo(Body);   // тот же блок 191 б и та же перерисовка, что у 4123
		return;
	}
	if (Opcode != 4132 || Body.Num() < 1) { return; }
	++Rx4132;
	const uint8 Code = Body[0];
	if (Code == 1)
	{
		if (Body.Num() < 1 + FAsdaClanBaseInfo::kSize)
		{
			UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] 4132 код 1: тело %d б < 192 — блок отброшен"), Body.Num());
			return;
		}
		OnBaseInfo(TArray<uint8>(Body.GetData() + 1, FAsdaClanBaseInfo::kSize));
		return;
	}
	if (Code == 2)
	{
		// ★ 27.09.2026 (`who_clan_fix.md`, МЕЛК-1 ревизии): эталон пишет в блок ТОЛЬКО эти поля
		//   (ui_core.c:17193-17215, дизасм 0xBCB574…0xBCB694; смещение блока = адрес − wnd+0x614),
		//   остальные байты — [44], [47] (число расширений → «%02dLv»), [49..105] (очки, маски прав,
		//   эмблема) — ОСТАЮТСЯ прежними. Раньше порт обнулял весь блок; теперь 1:1. Безопасно:
		//   [106..190] порт не читает, а [44]/[47]/[49..105] после clanId −1 гейтами не используются.
		//   Затем `uiTradeWindowRefresh`. Держим блок «пришедшим» (bValid), чтобы перерисовка шла как у эталона.
		//   ⚠ Часть ветки эталона вне блока — `CHUD_SetVisible(g_pHUD, 0)` и перерисовка значка
		//   эмблемы на HUD — в порт не переносится: эмблема клана в порте не реализована (40 б
		//   блока [66..105] не разобраны, см. `RefreshClanInfoPanel`).
		auto Fill = [this](int32 From, int32 Num, uint8 V) { FMemory::Memset(Info.B + From, V, Num); };   // запись диапазона блока
		Fill(0, 2, 0xFF);      // 0x614 `selMemberIdxA` = −1 (i16 clanId)
		Fill(2, 17, 0);        // 0x616 memset 0x11
		Fill(19, 1, 0);        // 0x627
		Fill(20, 20, 0);       // 0x628 memset 0x14
		Fill(40, 4, 0xFF);     // 0x63C dword −1 (`gap_616[38]`)
		Fill(45, 1, 0);        // 0x641 `gap_616[43]` — лимит членов
		Fill(46, 1, 0);        // 0x642 `gap_616[44]`
		Fill(48, 1, 0);        // 0x644 `gap_616[46]`
		Fill(106, 1, 0);       // 0x67E
		Fill(107, 2, 0xFF);    // 0x67F word −1
		Fill(109, 17, 0);      // 0x681 memset 0x11
		Fill(126, 2, 0xFF);    // 0x692 word −1
		Fill(128, 2, 0xFF);    // 0x694 word −1
		Fill(130, 17, 0);      // 0x696 memset 0x11
		Fill(147, 2, 0xFF);    // 0x6A7 word −1
		Fill(149, 2, 0xFF);    // 0x6A9 word −1
		Fill(151, 17, 0);      // 0x6AB memset 0x11
		Fill(168, 2, 0xFF);    // 0x6BC word −1
		Fill(170, 2, 0xFF);    // 0x6BE word −1
		Fill(172, 17, 0);      // 0x6C0 memset 0x11
		Fill(189, 2, 0xFF);    // 0x6D1 word −1
		Info.bValid = true;
		++InfoRevision;
		UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] 4132 код 2: клан удалён — инфо-блок сброшен"));
		RefreshClanInfoPanel();
		ApplyModeVisibility(Mode);
		return;
	}
	UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] 4132 код %d: эталон его игнорирует (только перерисовка)"), (int32)Code);
	RefreshClanInfoPanel();
}

// ★ 27.09.2026: 4137 код 1 (расширение клана) — эталон пишет новый лимит в блок (`gap_616[43]`
//   = блок+45) и НИЧЕГО не перерисовывает в клан-панели (лимита на ней нет). Число расширений
//   (блок+47) эталон из 4137 не берёт: его приносит следом 4132 от сервера.
void UAsdaCommunityClanSubsystem::ApplyIncreasedMaxMembers(uint8 NewMax)
{
	Info.B[45] = NewMax;
	++InfoRevision;
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] 4137: лимит членов клана = %d"), (int32)NewMax);
}

// Порт `uiTradeWindowRefresh@0xBC3BF0`: раскладка блока 4123 по контролам инфо-панели.
//   Контролы взяты из дизасма (`wnd[0x450]` → 14003, `wnd[0x454]` → 14007 и т.д., отчёт §2.2).
void UAsdaCommunityClanSubsystem::RefreshClanInfoPanel()
{
	if (!IsBoundTo(CommWnd) || !Info.bValid) { return; }
	using namespace AsdaCommunityUi;

	SetTxt(CommWnd, TEXT("CMTY_CLAN_NAME_V"),  Info.ClanName());    // 14003 — имя клана
	SetTxt(CommWnd, TEXT("CMTY_CLAN_CHIEF_V"), Info.ChiefName());   // 14007 — имя главы
	// Уровень печатается КАК (raw + 1) в формате «%02dLv» — 1:1 эталон (`00BC3C49`).
	SetTxt(CommWnd, TEXT("CMTY_CLAN_Level"),   FString::Printf(TEXT("%02dLv"), (int32)Info.LevelRaw() + 1));
	// ★★★ ИСПРАВЛЕНО АУДИТОМ 31.08.2026: 14011 и 14013 БЫЛИ ПЕРЕПУТАНЫ МЕСТАМИ.
	//   `uiTradeWindowRefresh@0xBC3BF0` по порядку пишет:
	//     `filler[1116]` (wnd+0x45C) ← "%d" от блока@53   — а 0x45C привязан к ID 0x36B9 = 14009;
	//     `filler[1124]` (wnd+0x464) ← "%d" от блока@49   — 0x464 = ID 0x36BD = 14013 (строка эталона
	//                                                       «CmtyClan_RemainPoints» в `00BBD469`);
	//     `filler[1120]` (wnd+0x460) ← ЛИТЕРАЛ "0"        — 0x460 = ID 0x36BB = 14011.
	//   То есть литерал «0» получает «Необходимо ОД» (14011), а число из блока — «Осталось ОД» (14013).
	SetTotalPointsValue(FString::FromInt(Info.TotalPoints()));                               // 14009 ← блок@53
	SetTxt(CommWnd, TEXT("CmtyClan_NeededPoints"), TEXT("0"));                               // 14011 ← литерал
	SetTxt(CommWnd, TEXT("CmtyClan_RemainPoints"), FString::FromInt(Info.RemainPoints()));   // 14013 ← блок@49
	// Моё положение в клане: эталон ставит в `wnd[0x28]` текст loc `17046 − char[0x97B]`.
	SetTxt(CommWnd, TEXT("Clan_MyGrade2"), Loc(kLocGradeBase - SelfGrade()));   // 14015 — значение
	// ★ 01.11.2026 [ОРИГИНАЛ]: хвост `uiTradeWindowRefresh` — `if ([65] != 1) uiNumberDisplay_Reset else uiNumberDisplay_Refresh`:
	//   редактор герба перечитывает герб клана из блока при каждом 4123/6527.
	if (Info.HasEmblem() == 1) { EmblemLoadFromBlock(); } else { EmblemReset(); }
	// ⚠ Эмблема клана ([66] флаг + 40 б с [67]) в порт НЕ переносится: формат этих 40 байт
	//   реверсом не разобран (эталон отдаёт их своему рисовальщику флага). Не выдумываем.
}

// 6512 — порция объявлений клана (546 б). Порт `guildMemberListRecvUpdate@0xBCC3A0`.
void UAsdaCommunityClanSubsystem::OnNotice(const TArray<uint8>& Body)
{
	// Гейт «флаг == 1» уже отработала сеть; здесь тело гарантированно 546 б и флаг 1.
	if (Body.Num() < 546) { return; }
	++Rx6512;
	// Первые 4 байта записи — числовой ключ; эталон копит в `wnd+0x78C` его МАКСИМУМ (`00BCC3EB`).
	const int32 Key = AsdaBytes::RdI32(Body.GetData() + 2);
	NoticeCursor = FMath::Max(NoticeCursor, Key);

	TArray<uint8> Rec;
	Rec.SetNumUninitialized(544);
	FMemory::Memcpy(Rec.GetData(), Body.GetData() + 2, 544);
	Notices.Add(MoveTemp(Rec));
	// 1:1 эталон: `if (size(wnd+0x780) > 5) pop_front(...)` — хранится не более пяти объявлений.
	while (Notices.Num() > kMaxNotices) { Notices.RemoveAt(0); }

	const bool bLast = (Body[1] == 2);   // маркер порции: 2 = последняя
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] 6512: объявление ключ=%d, маркер=%u, в накопителе %d/%d%s"),
		Key, (uint32)Body[1], Notices.Num(), kMaxNotices, bLast ? TEXT(" (последняя порция)") : TEXT(""));
	if (bLast) { RefreshNoticePanel(); }   // эталон на маркере 2 зовёт `uiMemberListPopulate@0xBC6490`
}

// ★★★ ПОРТ `uiMemberListPopulate@0xBC6490` — ЗАГЛУШКА СНЯТА АУДИТОМ 31.08.2026.
//   Прежняя пометка «раскладка записи не доказана» была ошибкой метода: раскладка выводится
//   ОДНОЗНАЧНО из конструктора записи `uiSlotListCtxCtor@0xBC6760`
//     (`*this = 0; memset(this+1,0,0x14); memset(this+6,0,0x14); memset(this+11,0,0x1F4)`
//      ⇒ 4 + 20 + 20 + 500 = 544, границы 0 / 4 / 24 / 44)
//   и подтверждается чтением в самом рисовальщике (см. заголовок).
//   Порядок эталона: сброс выбора → очистка 4 строк → очистка 14089/14027 → обход накопителя.
void UAsdaCommunityClanSubsystem::RefreshNoticePanel()
{
	if (!IsBoundTo(CommWnd)) { return; }
	using namespace AsdaCommunityUi;

	// ★ Ключи строк снимаются заново вместе с текстом: пустая строка = `INDEX_NONE`, и кнопка
	//   «Очистить» этой строки не сработает (эталонный гейт `strlen(текст) != 0`).
	NoticeDetailKey = INDEX_NONE;
	for (int32 R = 0; R < kNoticeRows; ++R) { NoticeRowKeys[R] = INDEX_NONE; }

	// 1:1 `for (clearIdx = 0; clearIdx < 4; ++clearIdx) ui_ClearThreeControlsText(self + 5*clearIdx + 19)`
	//   — гасятся ровно три текстовых контрола строки (_T1/_T2/_T3), сам контейнер не трогается.
	for (int32 Row = 0; Row < kNotices; ++Row)
	{
		const int32 N = Row + 1;
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T1"), N), FString());
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T2"), N), FString());
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T3"), N), FString());
	}
	SetTxt(CommWnd, TEXT("CMTY_CLAN_NOTICE_TEXT"), FString());   // 14089 — `*(self+17)` = wnd+0x44
	SetTxt(CommWnd, TEXT("CMTY_CLAN_NOTICE_TEXT2"), FString());   // 14027 — `*(self+18)` = wnd+0x48

	int32 Row = 0;
	for (const TArray<uint8>& Rec : Notices)
	{
		if (Rec.Num() < 544) { continue; }
		const int32   Key    = AsdaBytes::RdI32(Rec.GetData());                        // rec+0
		const FString Author = FAsdaLoginData::DecodeCp1251(Rec.GetData() + 4,  20);   // rec+4  (20 б)
		const FString Date   = FAsdaLoginData::DecodeCp1251(Rec.GetData() + 24, 20);   // rec+24 (20 б)
		const FString Text   = FAsdaLoginData::DecodeCp1251(Rec.GetData() + 44, 500);  // rec+44 (500 б)

		// 1:1 `if (*memberCharId == *((_DWORD*)self + 483))` — «раскрытым» показывается объявление,
		//   чей ключ равен `wnd+0x78C` (то есть МАКСИМАЛЬНЫЙ из полученных, он же курсор 6511).
		if (Key == NoticeCursor)
		{
			SetTxt(CommWnd, TEXT("CMTY_CLAN_NOTICE_TEXT"), Text);                        // rec+44 целиком
			SetTxt(CommWnd, TEXT("CMTY_CLAN_NOTICE_TEXT2"),                               // "%s %s"
				FString::Printf(TEXT("%s %s"), *Date, *Author));
			NoticeDetailKey = Key;   // ключ для кнопки 14090 (эталонный `wnd[483]`, но без D-C7)
			continue;                                        // эталон такую запись в строки НЕ кладёт
		}
		if (Row >= kNotices) { continue; }                   // 1:1 `else if (memberRow < 4)`
		const int32 N = Row + 1;
		// `_T3` ← rec+4 (`*(self + 5*row + 22)`).
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T3"), N), Author);
		// `_T2` ← ПЕРВЫЕ 10 СИМВОЛОВ rec+24 (`memcpy(buf, rec+24, 0xA); buf[10] = 0`).
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T2"), N), Date.Left(10));
		// `_T1` ← rec+44, у эталона обрезается по ШИРИНЕ 250 px (`textTruncateWithEllipsis`).
		//   Ширину в UE даёт сам виджет, поэтому кладём полный текст — обрезку делает разметка.
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Notice%02d_T1"), N), Text);
		// ★ Ключ РЕАЛЬНО НАРИСОВАННОЙ строки — им и удаляет кнопка 14091+Row (лечение D-C6).
		NoticeRowKeys[Row] = Key;
		++Row;
	}
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] объявления разложены: записей %d, строк заполнено %d, раскрытый ключ %d"),
		Notices.Num(), Row, NoticeCursor);
}

// 4117 — ответ на 4116/4114. `body[0]`: 0 → нотис 12424, 1 → нотис 12425 + чистка и закрытие окна.
void UAsdaCommunityClanSubsystem::OnLeaveResult(const TArray<uint8>& Body)
{
	if (Body.Num() < 1) { return; }
	++Rx4117;
	const uint8 Code = Body[0];
	// 1:1 `uiInventoryItemNameResultHandler@0xBCA870`: только два кода, остальные молча игнорируются.
	if (Code == 1)
	{
		NoticeBox(Loc(kLocLeaveOk));       // 12425 — вышел из клана
		Members.Reset();
		Notices.Reset();
		NoticeCursor = 0;
		SelRow = -1;
		Page = 1;
		Info = FAsdaClanBaseInfo();        // эталон чистит клановый блок персонажа (136 б)
		++InfoRevision;                    // ★ 27.09.2026: окно расширения 14450 перечитает числа
		UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] 4117 код 1: клан покинут, вкладка сброшена"));
		RefreshMemberList();
		// ★ Эталон здесь ЗАКРЫВАЕТ всё окно сообщества (`communityUI_Close(&g_CommunityUI)`):
		//   без клана вкладка 2 всё равно недоступна (гейт clanId == -1). Зовём владельца окна.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaCommunitySubsystem* Comm = GI->GetSubsystem<UAsdaCommunitySubsystem>())
			{
				Comm->CloseWindow();
			}
		}
	}
	else if (Code == 0)
	{
		NoticeBox(Loc(kLocLeaveFail));     // 12424 — не удалось выйти
		UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] 4117 код 0: сервер отказал в выходе из клана"));
	}
	else
	{
		// ⚠ Эталон коды кроме 0/1 не обрабатывает вовсе (ветка `if (*a2) { if (*a2 == 1) … }`) —
		//   пакет проходит молча. Воспроизводим 1:1, но в журнал пишем, чтобы «тишина» была видна.
		UE_LOG(LogAsdaClanTab, Warning,
			TEXT("[клан-вкладка] 4117 код %u: эталон такой код НЕ обрабатывает (молчание 1:1)"), (uint32)Code);
	}
}

// ═══════════════════════════ ЗВАНИЕ И ПРАВА ═══════════════════════════════════════════════════
int32 UAsdaCommunityClanSubsystem::SelfGrade() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return N ? (int32)N->GetSelfClanGrade() : 0;   // зеркало `CCharacter+0x97B`
}

// 1:1 эталонный гейт `ClanRankPermission(wnd, звание, право) || IsClanMaster()`:
//   глава (звание 1) в таблице масок не лежит — сервер выдаёт ему 63 отдельной веткой switch.
bool UAsdaCommunityClanSubsystem::SelfHasPermission(int32 Perm) const
{
	const int32 Grade = SelfGrade();
	if (Grade == 1) { return true; }                       // `IsClanMaster` эталона (`sub_BC7090`)
	return Info.RankPermission(Grade, Perm);
}

// ═══════════════════════════ УТИЛИТЫ ══════════════════════════════════════════════════════════
// Поле ввода вкладки по idc. Импортёр делает из JUI-контрола `EditBox` виджет `UEditableText`
//   и называет его ПО idc (`asda_jui_generic_umg.py:469-483`), но поиск всё равно идёт общей
//   цепочкой `AsdaCommunityUi::Find` — она закрывает суффиксы импортёра (`<idc>_grp` и прочие).
UEditableText* UAsdaCommunityClanSubsystem::FindEdit(const FString& Idc) const
{
	if (!CommWnd) { return nullptr; }
	return Cast<UEditableText>(AsdaCommunityUi::Find(CommWnd, Idc));
}

// Жёсткий стоп набора обоим полям вкладки — мотивация и источник цифр в объявлении.
void UAsdaCommunityClanSubsystem::SetupEditLimits()
{
	EditLimiters.Reset();
	// MaxBytes — LimitText разметки (символов, как у оригинала: cp1251 символ = байт); WireBytes — поле провода (байт).
	auto Limit = [this](const FString& Idc, int32 MaxBytes, int32 WireBytes)
	{
		UEditableText* E = FindEdit(Idc);
		if (!E)
		{
			UE_LOG(LogAsdaClanTab, Warning,
				TEXT("[клан-вкладка] поле ввода «%s» не найдено — лимит набора %d не поставлен"),
				*Idc, MaxBytes);
			return;
		}
		UAsdaTextLimiter* L = NewObject<UAsdaTextLimiter>(this);
		const TWeakObjectPtr<UEditableText> WeakEdit = E;
		L->Callback = [WeakEdit, MaxBytes, WireBytes](const FText& NewText)
		{
			UEditableText* Fld = WeakEdit.Get();
			if (!Fld) { return; }
			const FString S = NewText.ToString();
			// ★ [ПОРТ] Ревизия arabic_text_REVIEW С4/М9 (08.10.2026): кроме LimitText (символы) — байты ПРОВОДА: арабский уходит
			//   маркером + cp1256, смешанный — UTF-8 (2–3 б/символ), и посимвольный счёт молча терял хвост на проводе.
			//   Обрезка по границе символа (`TruncateToWireLimit`); русский/английский — ровно как прежде (символ = байт).
			const FString Cut = FAsdaLoginData::TruncateToWireLimit(S.Left(MaxBytes), WireBytes, WireBytes);
			if (Cut == S) { return; }
			Fld->SetText(FText::FromString(Cut));
		};
		E->OnTextChanged.AddDynamic(L, &UAsdaTextLimiter::OnTextChanged);
		AsdaUi::AnchorRelay(EditLimiters, L, E);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] поле «%s»: лимит набора %d симв. (LimitText разметки)"),
			*Idc, MaxBytes);
	};
	Limit(TEXT("CMTY_CLAN_NOITICE_WRITE"), kLimitNoticeText, 500);   // 14029 — объявление, LimitText 200, провод char[500] (6513 @50)
	Limit(TEXT("Clan_MyIntroTxt"),         kLimitMemoText,   64);    // 14021 — заметка,   LimitText 64,  провод char[64]  (6518 @6)
}

// ★★★ ЗНАЧЕНИЕ «ВСЕГО ОД» (контрол 14009) — УЗЕЛ С ТЕМ ЖЕ ИМЕНЕМ, ЧТО И ЕГО ПОДПИСЬ 14008.
//   ФАКТ ДАННЫХ: у 14008 и 14009 в `gamemain_jui.ini` ОДИН И ТОТ ЖЕ `ID = "CmtyClan_TotalPointsTxt"`
//   (строки 186971 и 186995) — дизайнер скопировал контрол и не переименовал. Импортёр
//   (`Tools/asda_jui_generic_umg.py`, `uniq_name`) даёт второму вхождению суффикс `_1`, а порядок
//   обхода JUI — сначала родитель 14008, затем ребёнок 14009.
//   ПОСЛЕДСТВИЕ ДО ПРАВКИ: порт писал значение в НЕСУЩЕСТВУЮЩИЙ `CmtyClan_TotalPoints`, `SetTxt`
//   молча выходил, и на экране оставался текст дизайнера «9,000,000,000» — это и увидел владелец.
//   ПОЧЕМУ НЕ ПРОСТО `SetTxt("CmtyClan_TotalPointsTxt_1")`: суффикс — деталь импортёра. Сначала
//   пробуем его, а если узла нет — берём ЛЮБУЮ подпись с этим префиксом, кроме самой 14008.
//   Так правка переживёт и смену схемы суффиксов, и перегенерацию окна.
void UAsdaCommunityClanSubsystem::SetTotalPointsValue(const FString& S)
{
	if (!CommWnd) { return; }
	if (UTextBlock* T = Cast<UTextBlock>(AsdaCommunityUi::Find(CommWnd, kIdcTotalPointsValue)))
	{
		T->SetText(FText::FromString(S));
		return;
	}
	if (!CommWnd->WidgetTree) { return; }
	const FString Base = TEXT("CmtyClan_TotalPointsTxt");
	UTextBlock* Found = nullptr;
	CommWnd->WidgetTree->ForEachWidget([&Base, &Found](UWidget* W)
	{
		if (Found || !W) { return; }
		UTextBlock* T = Cast<UTextBlock>(W);
		if (!T) { return; }
		const FString N = T->GetName();
		// Подпись 14008 зовётся РОВНО как база — её пропускаем, нам нужен «второй такой же».
		if (!N.StartsWith(Base, ESearchCase::CaseSensitive) || N.Equals(Base, ESearchCase::CaseSensitive))
		{
			return;
		}
		Found = T;
	});
	if (Found)
	{
		Found->SetText(FText::FromString(S));
		return;
	}
	UE_LOG(LogAsdaClanTab, Warning,
		TEXT("[клан-вкладка] контрол 14009 «Всего ОД» в дереве не найден — на экране останется ")
		TEXT("текст дизайнера из разметки"));
}

// Нижняя полоса 14101 `CMTY_CLAN_SLASH` вместе с пагинацией, лежащей внутри неё в разметке.
void UAsdaCommunityClanSubsystem::SetBottomBarVisible(bool bVisible)
{
	if (!CommWnd) { return; }
	for (const TCHAR* P : kBottomBar) { AsdaCommunityUi::SetVisByPrefix(CommWnd, P, bVisible); }
}

FString UAsdaCommunityClanSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

FString UAsdaCommunityClanSubsystem::LocTagged(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocTagged` (поведение то же).
	return AsdaNotice::LocTagged(GetGameInstance(), Id);
}

FString UAsdaCommunityClanSubsystem::LocSlotStr(int32 Id, const FString& S) const
{
	// Маркер `[ss1]` — общая подстановка порта (закон loc-string-placeholders).
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocSlot` (слот 1 + FormatNotice, как было).
	return AsdaNotice::LocSlot(GetGameInstance(), Id, S);
}

void UAsdaCommunityClanSubsystem::NoticeBox(const FString& Text) const
{
	// Эталонный «тултип-нотис» вкладки — `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст)` = `ONEBUT_MSGBOX` 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Social, Text, 2288);
}

int32 UAsdaCommunityClanSubsystem::SelfClanId() const
{
	// ★ Волна 3: общая точка `AsdaCommunityUi::SelfClanId` — зеркало `CCharacter+0x968` в мире (−1 = клана нет).
	return AsdaCommunityUi::SelfClanId(GetGameInstance());
}

FString UAsdaCommunityClanSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("членов=%d страница=%d/%d подрежим=%d выбор=%d клан=%d вкладка=%s список=%s\n"),
		Members.Num(), Page, TotalPages, Mode, SelRow, SelfClanId(),
		bTabVisible ? TEXT("видна") : TEXT("скрыта"),
		bMemberListLoading ? TEXT("грузится") : TEXT("закрыт 4126"));
	S += FString::Printf(
		TEXT("  сеть: 4123=%d 4131=%d 4132=%d 4125=%d 4126=%d 6512=%d 4117=%d · отправлено 4116=%d 6511=%d ")
		TEXT("6513=%d 6518=%d\n"),
		Rx4123, Rx4131, Rx4132, Rx4125, Rx4126, Rx6512, Rx4117, Tx4116, Tx6511, Tx6513, Tx6518);
	S += FString::Printf(TEXT("  моё звание=%d (1 = глава), право «управление»=%s, объявлений=%d, курсор=%d\n"),
		SelfGrade(), SelfHasPermission(kPermManage) ? TEXT("есть") : TEXT("нет"), Notices.Num(), NoticeCursor);
	// Ключи, которыми стреляют кнопки «Очистить» 14090…14094 (−1 = кнопка не сработает).
	S += FString::Printf(TEXT("  «Очистить»: раскрытое=%d, строки=%d/%d/%d/%d\n"),
		NoticeDetailKey, NoticeRowKeys[0], NoticeRowKeys[1], NoticeRowKeys[2], NoticeRowKeys[3]);
	if (Info.bValid)
	{
		S += FString::Printf(
			TEXT("  4123: клан %d «%s», глава «%s», уровень %d (расширений %d), лимит членов %d, ОД всего %d, осталось %d, маски прав 2..5 = %u/%u/%u/%u"),
			Info.ClanIndex(), *Info.ClanName(), *Info.ChiefName(), (int32)Info.LevelRaw() + 1,
			(int32)Info.LevelRaw(), (int32)Info.MaxMembers(),
			Info.TotalPoints(), Info.RemainPoints(),
			(uint32)AsdaBytes::RdU16(Info.B + 57), (uint32)AsdaBytes::RdU16(Info.B + 59),
			(uint32)AsdaBytes::RdU16(Info.B + 61), (uint32)AsdaBytes::RdU16(Info.B + 63));
	}
	else { S += TEXT("  4123: инфо-блок клана ещё не приходил"); }
	return S;
}
