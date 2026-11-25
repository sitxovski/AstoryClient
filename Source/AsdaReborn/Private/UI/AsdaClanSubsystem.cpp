// Copyright (c) 2026 @sitxovski (Discord)
// Реализация трёх под-боксов кланового окна: `CLAN_MAKING_WIN` (14400, код NPC 30015),
//   `CLAN_DELETE_BOX` (14470, код NPC 30016) и `CLAN_INCREASE_BOX` (14450, код NPC 30027;
//   ★ реализовано 27.09.2026 по правилам сервера). Весь разбор эталона, адреса, гейты, раскладки
//   пакетов, дефекты и таблица «клиент против сервера» — в шапке `AsdaClanSubsystem.h`, в отчётах
//   `Reports/npc_repair_variants_30015.md` и `Reports/owner_0927/who_clan.md` §2.

#include "UI/AsdaClanSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaCommunityFriendSubsystem.h"   // ★ волна 3: AsdaCommunityUi::SelfClanId
#include "UI/AsdaCommunityClanSubsystem.h"    // ★ 27.09.2026: блок клана (лимит членов, число расширений)
#include "UI/AsdaInventorySubsystem.h"        // ★ 27.09.2026: золото (`uiSetHonorPointsDisplay`) для 14459 и 4137
#include "AsdaBytes.h"                        // ★ 27.09.2026: RdI32 — золото @4 из 4137
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"          // EncodeCp1251/DecodeCp1251 — единая пара кодека
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"   // общий мат-фильтр `IsCursed` (эталон Chat_MatchBannedWord)
#include "UI/AsdaLocIds.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"                   // ★ 27.09.2026: `delay=N` самотеста
#include "Engine/Engine.h"                  // ★ 27.09.2026: GEngine->Exec(quit) самотеста
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "Engine/World.h"                   // ★ 27.09.2026: таймер мира самотеста
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "UI/AsdaHotbarSubsystem.h"         // ★ 27.09.2026: значок уведомления 6 (приглашение в клан)
#include "UI/AsdaPlayerMenuSubsystem.h"     // ★ 27.09.2026: самотест гейта «Друг» пункта 2003 (4108)
#include "Resources/AsdaSoundSubsystem.h"   // ★ 27.09.2026: звук 215 при значке приглашения
#include "HAL/PlatformTime.h"               // ★ 27.09.2026: таймер окна ответа на приглашение (30 с)
#include "Npc/AsdaNpcDialogSubsystem.h"      // ★ 27.09.2026: самотест — штатный вход кодов NPC 30015/30016/30027
#include "UI/AsdaCommunitySubsystem.h"        // ★ 27.09.2026: самотест — вкладка «Клан» окна 6400 (хвост 4109 код 1)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClan, Log, All);

namespace
{
	// Ассеты уже импортированы заходом JUI (закон `no-invented-ui`) — ничего не генерируем.
	const FName  kWinCreate(TEXT("CLAN_MAKING_WIN"));
	const FName  kWinDisband(TEXT("CLAN_DELETE_BOX"));
	const TCHAR* kWbpCreate =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_CLAN_MAKING_WIN.WBP_CLAN_MAKING_WIN_C");
	const TCHAR* kWbpDisband =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_CLAN_DELETE_BOX.WBP_CLAN_DELETE_BOX_C");
	// ★ 27.09.2026: окно расширения — ассет импортирован давно (`WBP_CLAN_INCREASE_BOX`), не генерируем.
	const FName  kWinIncrease(TEXT("CLAN_INCREASE_BOX"));
	const TCHAR* kWbpIncrease =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_CLAN_INCREASE_BOX.WBP_CLAN_INCREASE_BOX_C");

	// idc контролов — ровно те, что эталон печатает в диагностических `MessageBoxA`
	//   (`CmtyClan_Delete_BuildUI@0xBC0650`).
	const TCHAR* kIdcMakeWin    = TEXT("CLAN_MAKING_WIN");
	const TCHAR* kIdcMakeTitle  = TEXT("CLAN_TITLE");
	const TCHAR* kIdcMakeNameLb = TEXT("CLAN_NAME");
	const TCHAR* kIdcMakeInput  = TEXT("CLAN_NAME_INPUT");
	const TCHAR* kIdcMakeTypeLb = TEXT("CLAN_TYPE");
	const TCHAR* kIdcMakeTypeBt = TEXT("CLAN_TYPE_BTN");
	const TCHAR* kIdcMakeType1  = TEXT("CLAN_TYPE1");
	const TCHAR* kIdcMakeType2  = TEXT("CLAN_TYPE2");
	const TCHAR* kIdcMakeFeeLb  = TEXT("CLAN_FEE");
	const TCHAR* kIdcMakeFeeVal = TEXT("CLAN_FEE_VALUE");
	const TCHAR* kIdcMakeMonLb  = TEXT("CLAN_MONEY");
	const TCHAR* kIdcMakeMonVal = TEXT("CLAN_MONEY_VALUE");
	const TCHAR* kIdcMakeOk     = TEXT("CLAN_MAKE_BTN");
	const TCHAR* kIdcMakeCancel = TEXT("CLAN_CANCEL_BTN");
	const TCHAR* kIdcDelBox     = TEXT("CLAN_DELETE_BOX");
	const TCHAR* kIdcDelName    = TEXT("CLAN_DEL_NAME");
	const TCHAR* kIdcDelText    = TEXT("CLAN_DEL_TEXT");
	const TCHAR* kIdcDelOk      = TEXT("CLAN_DEL_OKBTN");
	const TCHAR* kIdcDelCancel  = TEXT("CLAN_DEL_CABTN");
	// Окно 14450 — имена узлов сняты headless-дампом ассета (`AsdaUmgBuilder.dump_widget_tree`,
	//   27.09.2026: 55 узлов) и совпадают с idc эталона (`CmtyClan_Delete_BuildUI`, ui_core.c:15064-15168).
	const TCHAR* kIdcIncName    = TEXT("CLAN_INC_NAME");
	const TCHAR* kIdcIncNowLb   = TEXT("CLAN_INC_MEMNOW");
	const TCHAR* kIdcIncNowVal  = TEXT("CLAN_INC_MEMNOW_V");
	const TCHAR* kIdcIncNewLb   = TEXT("CLAN_INC_MEMINC");
	const TCHAR* kIdcIncNewVal  = TEXT("CLAN_INC_MEMINC_V");
	const TCHAR* kIdcIncCostLb  = TEXT("CLAN_INC_COST");
	const TCHAR* kIdcIncCostVal = TEXT("CLAN_INC_COST_V");
	const TCHAR* kIdcIncMonLb   = TEXT("CLAN_INC_MONEY");
	const TCHAR* kIdcIncMonVal  = TEXT("CLAN_INC_MONEY_V");
	const TCHAR* kIdcIncText    = TEXT("CLAN_INC_TEXT");
	const TCHAR* kIdcIncOk      = TEXT("CLAN_INC_OKBTN");
	const TCHAR* kIdcIncCancel  = TEXT("CLAN_INC_CABTN");

	// ★ 27.09.2026: цена ступени расширения (индекс = число уже сделанных расширений). СЕРВЕР,
	//   пять ступеней: `ClanBaseInfo_MaxClanUserCount_Req@0x404710` кладёт `v19[0..4]` =
	//   0x13880 / 0x2E630 / 0x61A80 / 0xC3500 / 0x1E8480 (дизасм живого образа @0x4049F0…0x404A18)
	//   и требует `золото > v19[v25]`, иначе код 2. Первые четыре совпадают с таблицей клиента
	//   (`uiCharWnd_Ctor@0xBBBAA0`: wnd+1952…+1964); пятой у клиента НЕТ — на её месте (wnd+1968)
	//   лежит взнос за создание 10 000.
	constexpr int32 kIncCostTable[UAsdaClanSubsystem::kIncStepsServer] = { 80000, 190000, 400000, 800000, 2000000 };

	// ★ 27.09.2026 (`Reports/owner_0927/friend_gate_12435.md`): входящее приглашение в клан 4110 → значок 6 →
	//   окно (1, 44) → 4111. Все числа — из тела эталона (строки у каждого).
	constexpr int32  kNoticeTypeClanInvite = 6;      // тип значка: `Recv_PartyMember(…, 6, …)` network.c:2150; иконка `mes_clan`
	constexpr int32  kInviteBoxKind        = 1;      // вид TWOBUT 1520: `CItem_BuildTooltipText(…, 1, 44, …)` ui_core.c:69422
	constexpr int32  kInviteBoxId          = 44;     // id записи окна-сообщения (там же); ветки ответа `case 0x2C`
	constexpr double kInviteTimeoutSec     = 30.0;   // `v3 / 30000.0 * 100.0`, исход при > 100 % (ui_core.c:58361-58368)
	constexpr int32  kSoundInvite          = 215;    // `csoundPlayName2DStore(g_pSoundMgr, 215, 0)` network.c:27388
	constexpr int32  kInviteBodyLen        = 23;     // тело 4110 (сервер `sub_4C28F0(…, 4110, 23, …)`, GameServer.c:804)
	constexpr int32  kInviteNameLen        = 17;     // имя клана в 4110 @2..@18 (`_BYTE v9[17]` сервера)

	// ── Нотисы. КАЖДЫЙ id снят из тела эталона (адреса — в шапке заголовка) ──
	namespace ClanLoc
	{
		constexpr int32 kTitleMake     = 12216;  // «Создание клана»                 → 14404
		constexpr int32 kLabelName     = 12217;  // «Название клана:»                → 14403
		constexpr int32 kLabelType     = 12218;  // «Тип клана:»                     → 14405
		constexpr int32 kTypePeace     = 12219;  // «Мирные отношения»               → 14407
		constexpr int32 kTypeWar       = 12220;  // «Тип сражений»                   → 14408
		constexpr int32 kLabelFee      = 12221;  // «Затраты на создание:»           → 14409
		constexpr int32 kLabelMoney    = 12222;  // «Золото»                         → 14412
		constexpr int32 kBtnMake       = 12223;  // «Создание клана»                 → 14215
		constexpr int32 kAskDisband    = 12449;  // «Распустить клан?»               → 14272
		constexpr int32 kBadWord       = 7009;   // «Использованы недопустимые слова.»
		constexpr int32 kNameTooLong   = 12436;  // «Название клана не может быть длиннее 16 символов»
		constexpr int32 kAlreadyInClan = 12418;  // «Ты создаешь или вступаешь в клан» — гейт 30015
		constexpr int32 kCaptainOnly   = 12431;  // «Привилегия только главы клана.»  — гейт 30016/30027
		constexpr int32 kSoulmateDeny  = 12435;  // «Друзьям этот навык недоступен»   — гейт «Друг»
		constexpr int32 kNotInClan     = 991210; // ★ 08.11.2026 [ИСПРАВЛЕНИЕ] D8 «Ты не состоишь в клане.» / «You are not in a clan.» (ведомости gametext_overrides.json + loc/en/GameText.json)
		constexpr int32 kInviteAsk     = 12430;  // «Тебя приглашают в клан » — окно (1, 44), ui_core.c:69421 (★ 27.09.2026)
		// Коды 4105 (создание)
		constexpr int32 kMake_0        = 12400;  // «…нужно достичь 10 уровня и 1-й степени…»
		constexpr int32 kMake_2        = 12402;  // «Чтобы создать клан, выполни соответствующее задание»
		constexpr int32 kMake_3        = 12403;  // «Недостаточно средств для создания клана»
		constexpr int32 kMake_4        = 12404;  // «Пока ты состоишь в клане, создать собственный клан нельзя»
		constexpr int32 kMake_6        = 12406;  // «Ты больше не можешь создавать кланы»
		constexpr int32 kMake_7        = 12407;  // «Такой клан уже есть. Выбери другое название»
		constexpr int32 kMake_8        = 12408;  // (та же строка, что 12400 — так в данных)
		constexpr int32 kMake_9        = 12458;  // «Такое название клана недопустимо»
		constexpr int32 kMake_10       = 12168;  // «Нельзя применить: у цели есть Друг»
		constexpr int32 kMakeOk        = 20050;  // «Поздравляем.\nКлан '[ss1]'\nсоздан.»
		// Коды 4107 (роспуск)
		constexpr int32 kDel_0         = 12409;  // «Не удается распустить клан»
		constexpr int32 kDel_1         = 12410;  // «Клан распущен» (эталон печатает ИМЯ + эту строку)
		constexpr int32 kDel_2         = 12411;  // «Распустить клан может только его глава»
		constexpr int32 kDel_3         = 12412;  // «Пока в клане кто-то состоит, распустить его нельзя»
		constexpr int32 kDel_4         = 12167;  // «Если ты чей-то Друг, ты не можешь уйти из клана»
		// Окно расширения 14450 — подписи билдера (`ui_core.c:15082/15097/15112/15127/15142`)
		constexpr int32 kIncNowLb      = 12444;  // «- Текущие вакансии в кланах:»  → 14452
		constexpr int32 kIncNewLb      = 12445;  // «- Новый лимит членов клана:»   → 14454 (★ 27.09 перевод поправлен по решению владельца, clan_btn_6099.md)
		constexpr int32 kIncCostLb     = 12446;  // «Взносы:»                         → 14456
		constexpr int32 kIncMonLb      = 12447;  // «Золото:»                         → 14458
		constexpr int32 kIncAsk        = 12448;  // «Увеличить количество вакансий на указанное число?» → 14460
		// Коды 4137 (`uiInventoryRepairResultHandler@0xBC5310`)
		constexpr int32 kInc_0         = 12450;  // «Уровень клана не позволяет набирать новых членов»
		constexpr int32 kInc_1         = 12451;  // «Расширение клана подтверждено.»
		constexpr int32 kInc_2         = 12452;  // «Недостаточно средств для расширения клана.»
		constexpr int32 kInc_3         = 12453;  // «Клану больше некуда расти.»
		constexpr int32 kInc_4         = 12454;  // «Только глава клана может принять решение о расширении.»
		constexpr int32 kInc_5         = 12455;  // «Клану больше некуда расти.» (он же гейт открытия окна)
	}

	// ★ ДЕФЕКТ ОРИГИНАЛА D2 (см. шапку): подставляем имя клана в 14471. 0 = как в оригинале (пусто).
	ASDA_DEV_CVAR(int32, CVarShowNameInDisband,
		TEXT("asda.Clan.ShowNameInDisbandBox"), 1,
		TEXT("1 (по умолчанию) = в окне роспуска 14470 подпись 14471 показывает имя своего клана.\n")
		TEXT("0 = как в оригинале: билдер кладёт туда пустую строку и больше её никто не трогает."),
		ECVF_Default);

	// ★ 08.11.2026 (пачка 16, баг 7) [ИСПРАВЛЕНИЕ] ДЕФЕКТ ОРИГИНАЛА D8 (см. шапку заголовка): без клана — «Ты не состоишь в клане.».
	ASDA_DEV_CVAR(int32, CVarNoClanNotice,
		TEXT("asda.Clan.NoClanNotice"), 1,
		TEXT("1 (по умолчанию) = игроку без клана роспуск (30016), расширение (30027) и отказ приглашения 4109 код 4\n")
		TEXT("показывают loc 991210 «Ты не состоишь в клане.» [ИСПРАВЛЕНИЕ].\n")
		TEXT("0 = как в оригинале: 12431 «Привилегия только главы клана.» / 12417 «…добавляет только его глава»."),
		ECVF_Default);

	// ★ ДЕФЕКТ ОРИГИНАЛА D4: 14404 `CLAN_TITLE` и 14405 `CLAN_TYPE` объявлены в `.JUI` с
	//   Visible="0", хотя билдер кладёт в них локализованный текст. 0 = 1:1 (скрыты).
	ASDA_DEV_CVAR(int32, CVarShowHiddenLabels,
		TEXT("asda.Clan.ShowHiddenLabels"), 0,
		TEXT("1 = показать скрытые в .JUI подписи окна создания клана (14404 заголовок и 14405 «Тип\n")
		TEXT("клана:»). 0 (по умолчанию) = как в оригинале: Visible=\"0\", подписи невидимы."),
		ECVF_Default);

	// ★ ДЕФЕКТ ОРИГИНАЛА D1: тип клана всегда 0 (переключатель 14406 не обработан нигде).
	//   CVar оставлен для экспериментов с сервером; значение по умолчанию = поведение эталона.
	ASDA_DEV_CVAR(int32, CVarSendType,
		TEXT("asda.Clan.SendType"), 0,
		TEXT("Байт «тип клана» в теле 4104 (@4). Эталон ВСЕГДА шлёт 0: переключатель 14406\n")
		TEXT("`CLAN_TYPE_BTN` не резолвит билдер и не знает диспетчер команд (дефект D1)."),
		ECVF_Default);

	// ★ 27.09.2026 — РАСШИРЕНИЕ КЛАНА: чьи правила показывать в окне 14450.
	//   0 (по умолчанию) = СЕРВЕР: +10 мест, 5 расширений, 5 цен (решение владельца 27.09.2026 —
	//     «авторитет — сервер»; закон `client-server-table-desync`);
	//   1 = клиент-эталон 1:1: +5 мест, гейт «< 4 расширений», 4 цены — числа, которые сервер НЕ
	//     применяет (оставлено для сравнения).
	ASDA_DEV_CVAR(int32, CVarIncreaseRule,
		TEXT("asda.Clan.IncreaseRule"), 0,
		TEXT("Окно расширения клана 14450: 0 (по умолчанию) = правила СЕРВЕРА (+10 мест, до 5 расширений,\n")
		TEXT("5-я ступень 2 000 000); 1 = как в клиенте-эталоне (+5 мест, до 4 расширений) — числа,\n")
		TEXT("которые сервер не применяет."),
		ECVF_Default);
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaClanSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HCreate  = Net->OnClanCreateResult.AddLambda(
			[this](const TArray<uint8>& B) { PendingCreate.Push(B); });
		HDisband = Net->OnClanDisbandResult.AddLambda(
			[this](const TArray<uint8>& B) { PendingDisband.Push(B); });
		HIncrease = Net->OnClanIncreaseResult.AddLambda(
			[this](const TArray<uint8>& B) { PendingIncrease.Push(B); });
		// ★ 27.09.2026: 4110 — входящее приглашение в клан (разбор в Tick).
		HInvite = Net->OnClanInviteRecv.AddLambda(
			[this](const TArray<uint8>& B) { PendingInvite.Push(B); });
	}
	// ★ 27.09.2026: клик по значку уведомления 6 — `uiNoticeButtonDispatch` case 6 (ui_core.c:69418-69424).
	//   Панель поднимается ДО нас (зависимость), иначе подписка потерялась бы при порядке инициализации.
	if (UAsdaHotbarSubsystem* Hb = Collection.InitializeDependency<UAsdaHotbarSubsystem>())
	{
		HNoticeIcon = Hb->OnNoticeIconClicked.AddWeakLambda(this, [this](int32 Type)
		{
			if (Type == kNoticeTypeClanInvite) { OnInviteNoticeIconClicked(); }
		});
	}
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] подсистема поднята (окна 14400 создание / 14470 роспуск / 14450 расширение)"));
}

void UAsdaClanSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnClanCreateResult.Remove(HCreate);
		Net->OnClanDisbandResult.Remove(HDisband);
		Net->OnClanIncreaseResult.Remove(HIncrease);
		Net->OnClanInviteRecv.Remove(HInvite);   // ★ 27.09.2026
	}
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->OnNoticeIconClicked.Remove(HNoticeIcon);   // ★ 27.09.2026
	}
	ResetCreateWidget();
	ResetDisbandWidget();
	ResetIncreaseWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaClanSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Защёлки `bAwaitCreate`/`bAwaitDisband` снимаются ТОЛЬКО ответом сервера (4106/4134):
	// после разрыва они заперли бы создание и роспуск клана до перезапуска игры.
	bAwaitCreate = false;
	bAwaitDisband = false;
	bAwaitIncrease = false;
	PendingCreate.Clear();
	PendingDisband.Clear();
	PendingIncrease.Clear();
	// ★ 27.09.2026: приглашение в клан — данные сессии (окно-сообщение снимает очередь оконной подсистемы сама).
	PendingInvite.Clear();
	InviteClanId = -1; InviteClanName.Reset(); InviteReqUser = -1; InviteRespUser = -1;
	bInviteBoxActive = false;
	InviteBoxStart = -1.0;
	ResetCreateWidget();
	ResetDisbandWidget();
	ResetIncreaseWidget();
	bCreateOpen = false;
	bDisbandOpen = false;
	bIncOpen = false;
	IncShownRevision = -1;
	Mouse.Reset();
	if (AsdaIsFullSessionReset(Reason))
	{
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] сброс сессии (%s): окна создания/роспуска закрыты, ожидания сняты"),
			AsdaSessionResetReasonName(Reason));
	}
}

void UAsdaClanSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Clan");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// Ответы 4105/4107 могут прийти и после закрытия окна — разбираем их ВСЕГДА (как окно 74500).
	if (TArray<TArray<uint8>> Batch; PendingCreate.Drain(Batch))   // ★ U7: TAsdaNetQueue
	{
		for (const TArray<uint8>& B : Batch) { HandleCreateResult(B); }
	}
	if (TArray<TArray<uint8>> Batch; PendingDisband.Drain(Batch))   // ★ U7: TAsdaNetQueue
	{
		for (const TArray<uint8>& B : Batch) { HandleDisbandResult(B); }
	}
	if (TArray<TArray<uint8>> Batch; PendingIncrease.Drain(Batch))   // ★ U7: TAsdaNetQueue
	{
		for (const TArray<uint8>& B : Batch) { HandleIncreaseResult(B); }
	}
	// ★ 27.09.2026: приглашения 4110 и таймер окна ответа (эталон — тот же игровой цикл, game_main.c:2057-2058).
	if (TArray<TArray<uint8>> Batch; PendingInvite.Drain(Batch))   // ★ U7: TAsdaNetQueue
	{
		for (const TArray<uint8>& B : Batch) { HandleInviteRecv(B); }
	}
	TickInviteTimer();
	// ★ 27.09.2026: блок клана сменился (4123/4131/4132/4137) при открытом окне 14450 — перечитать числа.
	if (bIncOpen)
	{
		UGameInstance* GI = GetGameInstance();
		const UAsdaCommunityClanSubsystem* CC = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr;
		if (CC && CC->GetBaseInfoRevision() != IncShownRevision) { FillIncreaseNumbers(); }
	}
	if (bCreateOpen || bDisbandOpen || bIncOpen)
	{
		Mouse.Update();
		// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
		//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
		//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
		//   ⚠ Защёлку кадра сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия
		//   сошлись бы в клик, которого игрок не делал. Своих защёлок у окон 14400/14470 нет —
		//   кадр мыши держится только ради фронтов полей ввода и кнопок.
		if (AsdaUi::IsModalDispatchBlocked(GetGameInstance())) { Mouse.Reset(); }
	}
}

// ═════════════════════════════ ВИДЖЕТЫ ════════════════════════════════════════════════════════

bool UAsdaClanSubsystem::EnsureCreateWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWinCreate, kWbpCreate, kWinZ).Ensure(GetGameInstance(), CreateWidget, bCreateReady,
		[this](UAsdaWindowSubsystem&) { ResetCreateWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupCreateControls();
		},
		[]()
		{
			UE_LOG(LogAsdaClan, Error, TEXT("[клан] WBP_CLAN_MAKING_WIN не загрузился (%s)"), kWbpCreate);
		});
	// Скрытие окна (шаг каркаса): в `.JUI` у 14400 Visible="0"
	return FAsdaJuiWindowHandle::Succeeded(R) && bCreateReady;
}

bool UAsdaClanSubsystem::EnsureDisbandWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWinDisband, kWbpDisband, kWinZ).Ensure(GetGameInstance(), DisbandWidget, bDisbandReady,
		[this](UAsdaWindowSubsystem&) { ResetDisbandWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupDisbandControls();
		},
		[]()
		{
			UE_LOG(LogAsdaClan, Error, TEXT("[клан] WBP_CLAN_DELETE_BOX не загрузился (%s)"), kWbpDisband);
		});
	// Скрытие окна (шаг каркаса): в `.JUI` у 14470 Visible="0"
	return FAsdaJuiWindowHandle::Succeeded(R) && bDisbandReady;
}

bool UAsdaClanSubsystem::EnsureIncreaseWidget()
{
	// ★ 27.09.2026: тот же каркас `FAsdaJuiWindowHandle::Ensure`, что у окон 14400/14470.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWinIncrease, kWbpIncrease, kWinZ).Ensure(GetGameInstance(), IncWidget, bIncReady,
		[this](UAsdaWindowSubsystem&) { ResetIncreaseWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupIncreaseControls();
		},
		[]()
		{
			UE_LOG(LogAsdaClan, Error, TEXT("[клан] WBP_CLAN_INCREASE_BOX не загрузился (%s)"), kWbpIncrease);
		});
	// Скрытие окна (шаг каркаса): в `.JUI` у 14450 Visible="0"
	return FAsdaJuiWindowHandle::Succeeded(R) && bIncReady;
}

void UAsdaClanSubsystem::ResetCreateWidget()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kWinCreate);
		}
	}
	CreateWidget = nullptr; NameInput = nullptr;
	TxtMakeTitle = nullptr; TxtMakeNameLb = nullptr; TxtMakeTypeLb = nullptr;
	TxtMakeType1 = nullptr; TxtMakeType2 = nullptr;
	TxtMakeFeeLb = nullptr; TxtMakeFeeVal = nullptr;
	TxtMakeMonLb = nullptr; TxtMakeMonVal = nullptr;
	BtnMakeOk = nullptr; TxtMakeOk = nullptr;
	BtnMakeCancel = nullptr; TxtMakeCancel = nullptr;
	bCreateReady = false;
	bCreateOpen = false;
}

void UAsdaClanSubsystem::ResetDisbandWidget()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kWinDisband);
		}
	}
	DisbandWidget = nullptr;
	TxtDelName = nullptr; TxtDelText = nullptr;
	BtnDelOk = nullptr; TxtDelOk = nullptr;
	BtnDelCancel = nullptr; TxtDelCancel = nullptr;
	bDisbandReady = false;
	bDisbandOpen = false;
}

void UAsdaClanSubsystem::ResetIncreaseWidget()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kWinIncrease);
		}
	}
	IncWidget = nullptr;
	TxtIncName = nullptr; TxtIncNowLb = nullptr; TxtIncNowVal = nullptr;
	TxtIncNewLb = nullptr; TxtIncNewVal = nullptr;
	TxtIncCostLb = nullptr; TxtIncCostVal = nullptr;
	TxtIncMonLb = nullptr; TxtIncMonVal = nullptr; TxtIncText = nullptr;
	BtnIncOk = nullptr; TxtIncOk = nullptr;
	BtnIncCancel = nullptr; TxtIncCancel = nullptr;
	bIncReady = false;
	bIncOpen = false;
	IncShownRevision = -1;
}

void UAsdaClanSubsystem::SetupCreateControls()
{
	bCreateReady = false;
	if (!CreateWidget || !CreateWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	CreateWidget->WidgetTree->GetAllWidgets(All);
	UWidget* TypeBtn = nullptr;
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UEditableText* Ed = Cast<UEditableText>(W))
		{
			if (N == kIdcMakeInput) { NameInput = Ed; }
			continue;
		}
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == kIdcMakeOk)     { BtnMakeOk = Btn; continue; }
			if (N == kIdcMakeCancel) { BtnMakeCancel = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == kIdcMakeTitle)  { TxtMakeTitle = Tb;  continue; }
			if (N == kIdcMakeNameLb) { TxtMakeNameLb = Tb; continue; }
			if (N == kIdcMakeTypeLb) { TxtMakeTypeLb = Tb; continue; }
			if (N == kIdcMakeType1)  { TxtMakeType1 = Tb;  continue; }
			if (N == kIdcMakeType2)  { TxtMakeType2 = Tb;  continue; }
			if (N == kIdcMakeFeeLb)  { TxtMakeFeeLb = Tb;  continue; }
			if (N == kIdcMakeFeeVal) { TxtMakeFeeVal = Tb; continue; }
			if (N == kIdcMakeMonLb)  { TxtMakeMonLb = Tb;  continue; }
			if (N == kIdcMakeMonVal) { TxtMakeMonVal = Tb; continue; }
			if (N == FString(kIdcMakeOk)     + TEXT("_Text")) { TxtMakeOk = Tb;     continue; }
			if (N == FString(kIdcMakeCancel) + TEXT("_Text")) { TxtMakeCancel = Tb; continue; }
			continue;
		}
		if (N == kIdcMakeTypeBt) { TypeBtn = W; }
	}

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnMakeOk,     kIdMakeOk,     [this](int32) { OnCreateOkClicked(); });
	Bind(BtnMakeCancel, kIdMakeCancel, [this](int32) { OnCreateCancelClicked(); });

	// Статичные подписи — ровно те, что ставит `CmtyClan_Delete_BuildUI`.
	auto SetLoc = [this](UTextBlock* Tb, int32 LocId)
	{
		if (!Tb) { return; }
		const FString S = Loc(LocId);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtMakeTitle,  ClanLoc::kTitleMake);
	SetLoc(TxtMakeNameLb, ClanLoc::kLabelName);
	SetLoc(TxtMakeTypeLb, ClanLoc::kLabelType);
	SetLoc(TxtMakeType1,  ClanLoc::kTypePeace);
	SetLoc(TxtMakeType2,  ClanLoc::kTypeWar);
	SetLoc(TxtMakeFeeLb,  ClanLoc::kLabelFee);
	SetLoc(TxtMakeMonLb,  ClanLoc::kLabelMoney);
	SetLoc(TxtMakeOk,     ClanLoc::kBtnMake);
	SetLoc(TxtMakeCancel, AsdaLoc::kBtnCancel);

	// ★ ДЕФЕКТ ОРИГИНАЛА D4: 14404 и 14405 в `.JUI` имеют Visible="0". Воспроизводим 1:1.
	const bool bShowHidden = CVarShowHiddenLabels.GetValueOnGameThread() != 0;
	if (TxtMakeTitle)  { TxtMakeTitle->SetVisibility(bShowHidden ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	if (TxtMakeTypeLb) { TxtMakeTypeLb->SetVisibility(bShowHidden ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }

	// ★ ДЕФЕКТ ОРИГИНАЛА D1: переключатель типа клана 14406 не обработан НИГДЕ — оставляем его
	//   некликабельным, чтобы игрок не думал, что выбор что-то меняет (пакет всегда шлёт 0).
	if (TypeBtn) { TypeBtn->SetVisibility(ESlateVisibility::HitTestInvisible); }

	bCreateReady = (NameInput != nullptr && BtnMakeOk != nullptr && BtnMakeCancel != nullptr);
	UE_LOG(LogAsdaClan, Log,
		TEXT("[клан] окно 14400 разобрано: поле=%s Создать=%s Отмена=%s стоимость=%s золото=%s тип-кнопка=%s"),
		NameInput ? TEXT("да") : TEXT("НЕТ"), BtnMakeOk ? TEXT("да") : TEXT("НЕТ"),
		BtnMakeCancel ? TEXT("да") : TEXT("НЕТ"), TxtMakeFeeVal ? TEXT("да") : TEXT("НЕТ"),
		TxtMakeMonVal ? TEXT("да") : TEXT("НЕТ"), TypeBtn ? TEXT("да (мертва, D1)") : TEXT("НЕТ"));
}

void UAsdaClanSubsystem::SetupDisbandControls()
{
	bDisbandReady = false;
	if (!DisbandWidget || !DisbandWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	DisbandWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == kIdcDelOk)     { BtnDelOk = Btn; continue; }
			if (N == kIdcDelCancel) { BtnDelCancel = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == kIdcDelName) { TxtDelName = Tb; continue; }
			if (N == kIdcDelText) { TxtDelText = Tb; continue; }
			if (N == FString(kIdcDelOk)     + TEXT("_Text")) { TxtDelOk = Tb;     continue; }
			if (N == FString(kIdcDelCancel) + TEXT("_Text")) { TxtDelCancel = Tb; continue; }
			continue;
		}
	}

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnDelOk,     kIdDelOk,     [this](int32) { OnDisbandOkClicked(); });
	Bind(BtnDelCancel, kIdDelCancel, [this](int32) { OnDisbandCancelClicked(); });

	if (TxtDelText)   { const FString S = Loc(ClanLoc::kAskDisband);   if (!S.IsEmpty()) { TxtDelText->SetText(FText::FromString(S)); } }
	if (TxtDelOk)     { const FString S = Loc(AsdaLoc::kBtnOk);        if (!S.IsEmpty()) { TxtDelOk->SetText(FText::FromString(S)); } }
	if (TxtDelCancel) { const FString S = Loc(AsdaLoc::kBtnCancel);    if (!S.IsEmpty()) { TxtDelCancel->SetText(FText::FromString(S)); } }
	// Эталон кладёт в 14471 ПУСТУЮ строку (дефект D2) — здесь только повторяем это; текст
	//   подставляется при открытии, если CVar не вернули в 1:1.
	if (TxtDelName)   { TxtDelName->SetText(FText::GetEmpty()); }

	bDisbandReady = (BtnDelOk != nullptr && BtnDelCancel != nullptr);
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] окно 14470 разобрано: ОК=%s Отмена=%s имя=%s вопрос=%s"),
		BtnDelOk ? TEXT("да") : TEXT("НЕТ"), BtnDelCancel ? TEXT("да") : TEXT("НЕТ"),
		TxtDelName ? TEXT("да") : TEXT("НЕТ"), TxtDelText ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaClanSubsystem::SetupIncreaseControls()
{
	bIncReady = false;
	if (!IncWidget || !IncWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	IncWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == kIdcIncOk)     { BtnIncOk = Btn; continue; }
			if (N == kIdcIncCancel) { BtnIncCancel = Btn; continue; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == kIdcIncName)    { TxtIncName = Tb;    continue; }
			if (N == kIdcIncNowLb)   { TxtIncNowLb = Tb;   continue; }
			if (N == kIdcIncNowVal)  { TxtIncNowVal = Tb;  continue; }
			if (N == kIdcIncNewLb)   { TxtIncNewLb = Tb;   continue; }
			if (N == kIdcIncNewVal)  { TxtIncNewVal = Tb;  continue; }
			if (N == kIdcIncCostLb)  { TxtIncCostLb = Tb;  continue; }
			if (N == kIdcIncCostVal) { TxtIncCostVal = Tb; continue; }
			if (N == kIdcIncMonLb)   { TxtIncMonLb = Tb;   continue; }
			if (N == kIdcIncMonVal)  { TxtIncMonVal = Tb;  continue; }
			if (N == kIdcIncText)    { TxtIncText = Tb;    continue; }
			if (N == FString(kIdcIncOk)     + TEXT("_Text")) { TxtIncOk = Tb;     continue; }
			if (N == FString(kIdcIncCancel) + TEXT("_Text")) { TxtIncCancel = Tb; continue; }
			continue;
		}
	}

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnIncOk,     kIdIncOk,     [this](int32) { OnIncreaseOkClicked(); });      // 0x387D
	Bind(BtnIncCancel, kIdIncCancel, [this](int32) { OnIncreaseCancelClicked(); });  // 0x387E

	// Статичные подписи — ровно те, что ставит `CmtyClan_Delete_BuildUI` (ui_core.c:15064-15168):
	//   значения 14451/14453/14455/14457/14459 билдер ОБНУЛЯЕТ (`&Src` = пустая строка), подписи —
	//   loc 12444…12448, кнопки — 2288/2289. Текст дизайнера из ассета («Текущий лимит…», «15»,
	//   «123», «9,999,999») на экран не выходит.
	auto SetLoc = [this](UTextBlock* Tb, int32 LocId)
	{
		if (!Tb) { return; }
		const FString S = Loc(LocId);
		if (!S.IsEmpty()) { Tb->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtIncNowLb,  ClanLoc::kIncNowLb);
	SetLoc(TxtIncNewLb,  ClanLoc::kIncNewLb);
	SetLoc(TxtIncCostLb, ClanLoc::kIncCostLb);
	SetLoc(TxtIncMonLb,  ClanLoc::kIncMonLb);
	SetLoc(TxtIncText,   ClanLoc::kIncAsk);
	SetLoc(TxtIncOk,     AsdaLoc::kBtnOk);
	SetLoc(TxtIncCancel, AsdaLoc::kBtnCancel);
	UTextBlock* const Values[] = { TxtIncName, TxtIncNowVal, TxtIncNewVal, TxtIncCostVal, TxtIncMonVal };
	for (UTextBlock* Tb : Values)
	{
		if (Tb) { Tb->SetText(FText::GetEmpty()); }
	}

	bIncReady = (BtnIncOk != nullptr && BtnIncCancel != nullptr);
	UE_LOG(LogAsdaClan, Log,
		TEXT("[клан] окно 14450 разобрано: ОК=%s Отмена=%s имя=%s сейчас=%s станет=%s цена=%s золото=%s вопрос=%s"),
		BtnIncOk ? TEXT("да") : TEXT("НЕТ"), BtnIncCancel ? TEXT("да") : TEXT("НЕТ"),
		TxtIncName ? TEXT("да") : TEXT("НЕТ"), TxtIncNowVal ? TEXT("да") : TEXT("НЕТ"),
		TxtIncNewVal ? TEXT("да") : TEXT("НЕТ"), TxtIncCostVal ? TEXT("да") : TEXT("НЕТ"),
		TxtIncMonVal ? TEXT("да") : TEXT("НЕТ"), TxtIncText ? TEXT("да") : TEXT("НЕТ"));
}

// ═════════════════════════════ ГЕЙТЫ ══════════════════════════════════════════════════════════

int32 UAsdaClanSubsystem::SelfClanId() const
{
	// ★ Волна 3: общая точка `AsdaCommunityUi::SelfClanId` — зеркало `CCharacter+0x968` в мире (−1 = клана нет).
	return AsdaCommunityUi::SelfClanId(GetGameInstance());
}

int32 UAsdaClanSubsystem::NoClanLocOr(const UGameInstance* GI, int32 OrigLocId)
{
	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] D8 (шапка заголовка): «клана нет» — 991210; иначе нотис эталона.
	if (CVarNoClanNotice.GetValueOnGameThread() != 0 && AsdaCommunityUi::SelfClanId(GI) == -1)
	{
		return ClanLoc::kNotInClan;
	}
	return OrigLocId;
}

bool UAsdaClanSubsystem::IsSoulmateLocked() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->SelfSoulSilhouette != 0;   // зеркало `CCharacter+0x8C1` (пакет 6152 тело+12)
}

// ★ 27.09.2026: ЕДИНСТВЕННАЯ точка гейта «Друг» 12435 (обоснование и все места эталона — в объявлении).
bool UAsdaClanSubsystem::RefuseIfSoulmateLocked(bool bDropTopMsgBox, const TCHAR* Why) const
{
	if (!IsSoulmateLocked()) { return false; }
	if (bDropTopMsgBox)
	{
		// `itemBuyPanelRefreshOrHide(g_pItemTooltipUI)` — ПЕРЕД нотисом (порт `DropTopMsgBoxBeforeNotice`).
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->DropTopMsgBoxBeforeNotice(Why);
		}
	}
	NoticeMsgBoxLoc(ClanLoc::kSoulmateDeny);   // 12435, `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, …)` — ONEBUT 1500
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] признак «Друг» (SoulState ≠ 0) — нотис 12435%s: %s"),
		bDropTopMsgBox ? TEXT(" после снятия верхнего окна-сообщения") : TEXT(""), Why);
	return true;
}

int32 UAsdaClanSubsystem::SelfClanRank() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net ? (int32)Net->SelfClanRank : 0;    // зеркало `CCharacter+0x97B` (1 = глава)
}

FString UAsdaClanSubsystem::SelfClanName() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net ? Net->SelfClanName : FString();   // зеркало `CCharacter+0x96A`
}

int64 UAsdaClanSubsystem::SelfGold() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return WS ? WS->SelfGold : -1;
}

// ═════════════════════════════ ОКНО СОЗДАНИЯ (30015) ══════════════════════════════════════════

bool UAsdaClanSubsystem::OpenCreateWindow()
{
	// ★ ПОРЯДОК ГЕЙТОВ 1:1 (дизасм 0xBC2CEC…0xBC2D6F): сначала «уже в клане», потом «Друг».
	if (SelfClanId() != -1)
	{
		NoticeMsgBoxLoc(ClanLoc::kAlreadyInClan);   // 12418
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 30015: уже есть клан %d — нотис 12418"), SelfClanId());
		return false;
	}
	// ★ 27.09.2026: общая точка гейта «Друг» (ui_core.c:15737-15740 — БЕЗ снятия верхнего окна-сообщения).
	if (RefuseIfSoulmateLocked(/*bDropTopMsgBox*/false, TEXT("клан 30015: окно создания (ui_core.c:15737)"))) { return false; }
	if (!EnsureCreateWidget()) { return false; }

	// Эталон при a2 != 0: 14413 ← золото игрока, 14410 ← поле окна +0x7B0 (константа 10 000).
	if (TxtMakeMonVal)
	{
		const int64 G = SelfGold();
		TxtMakeMonVal->SetText(FText::FromString(
			AsdaUi::FormatThousands((int32)FMath::Clamp<int64>(G < 0 ? 0 : G, 0, MAX_int32))));
	}
	if (TxtMakeFeeVal)
	{
		TxtMakeFeeVal->SetText(FText::FromString(AsdaUi::FormatThousands(kCreateFeeGold)));
	}
	// Эталон: `CControl_SetFocus(input, 1)` + `uiPanel_SetFocusChildren(g_pChatWnd, 0)` —
	//   ввод уходит в поле имени, чат фокус отдаёт.
	if (NameInput) { NameInput->SetKeyboardFocus(); }

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWinCreate, true);   // `CControl_SetVisible(panel, 1)`
			WM->BringWindowToFront(kWinCreate);       // `CClass_MapInsert(g_pWindowMgr, panel)`
		}
	}
	// Эталон в конце ветки открытия закрывает два соседних бокса (роспуск и расширение).
	CloseDisbandWindow();
	CloseIncreaseWindow();   // ★ 27.09.2026: эталон ui_core.c:15767; закрытие без гейтов — [ПОРТ] исправление дефекта оригинала (см. `CloseIncreaseWindow`)
	bCreateOpen = true;
	bAwaitCreate = false;
	Mouse.Reset();
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] окно создания 14400 открыто (взнос %d, золота %lld)"),
		kCreateFeeGold, SelfGold());
	return true;
}

void UAsdaClanSubsystem::CloseCreateWindow()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWinCreate, false);
		}
	}
	// Эталон при a2 == 0 чистит поле ввода 14402 и подпись золота 14413 (14410 НЕ трогает).
	if (NameInput)     { NameInput->SetText(FText::GetEmpty()); }
	if (TxtMakeMonVal) { TxtMakeMonVal->SetText(FText::GetEmpty()); }
	bCreateOpen = false;
	bAwaitCreate = false;
}

void UAsdaClanSubsystem::SetClanNameInput(const FString& Text)
{
	if (NameInput) { NameInput->SetText(FText::FromString(Text)); }
}

FString UAsdaClanSubsystem::GetClanNameInput() const
{
	return NameInput ? NameInput->GetText().ToString() : FString();
}

void UAsdaClanSubsystem::OnCreateOkClicked()
{
	if (bAwaitCreate)
	{
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14215: ответ 4105 ещё не пришёл — клик пропущен"));
		return;
	}
	UGameInstance* GI = GetGameInstance();
	const FString Raw = GetClanNameInput();

	// 1:1 `case 0x3787` (`ui_core.c:15347…15364`): сперва мат-фильтр, потом длина.
	if (UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
	{
		if (Chat->IsCursed(Raw))
		{
			NoticeMsgBoxLoc(ClanLoc::kBadWord, /*эталон (0, 7009)*/7009);       // 7009
			UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14215: имя не прошло мат-фильтр — нотис 7009"));
			return;
		}
	}
	// Эталон меряет ДЛИНУ В БАЙТАХ ОДНОБАЙТОВОЙ КОДИРОВКИ (`strlen(buf) < 0x11`) — считаем так же.
	uint8 Probe[64];
	const int32 Bytes = FAsdaLoginData::EncodeCp1251(Raw, Probe, sizeof(Probe));
	if (Bytes > kClanNameMaxBytes)
	{
		NoticeMsgBoxLoc(ClanLoc::kNameTooLong);       // 12436
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14215: имя %d б > %d — нотис 12436"), Bytes, kClanNameMaxBytes);
		return;
	}

	// 1:1 `netSendPasswordPacket@0xBC8A10`: вырезать ПРОБЕЛЫ, вернуть очищенный текст в поле,
	//   и только при непустой строке отправить пакет.
	FString Clean = Raw;
	Clean.ReplaceInline(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
	Clean.ReplaceInline(TEXT("\t"), TEXT(""), ESearchCase::CaseSensitive);
	SetClanNameInput(Clean);
	if (Clean.IsEmpty())
	{
		// ★ ДЕФЕКТ ОРИГИНАЛА D3: эталон здесь МОЛЧИТ (нотиса нет, пакет не идёт). 1:1.
		UE_LOG(LogAsdaClan, Warning,
			TEXT("[клан] 14215: имя пустое — эталон молча ничего не делает (дефект D3)"));
		return;
	}
	bAwaitCreate = false;
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		const uint8 Type = (uint8)FMath::Clamp(CVarSendType.GetValueOnGameThread(), 0, 255);
		bAwaitCreate = Net->SendClanCreate4104(Type, Clean);
	}
	// Эталон закрывает окно СРАЗУ после отправки (`uiRepairWindowToggle(v9, 0)`).
	CloseCreateWindow();
	// ★ ЗАЩЁЛКА «ЖДЁМ 4105» ВЗВОДИТСЯ ТОЛЬКО ПРИ РЕАЛЬНО УШЕДШЕМ ПАКЕТЕ (закон
	//   `no-state-latch-before-shown`). БЫЛО: `bAwaitCreate = true;` безусловно — ПОВЕРХ результата
	//   отправки, присвоенного строкой выше. Если пакет не ушёл (нет соединения, а с 2026-09-03 —
	//   ещё и гейт одержимости на 4104, `World/AsdaPossessGate.h`), ответа 4105 не будет НИКОГДА,
	//   и кнопка 14215 остаётся мёртвой до перезахода в игру (`:529` пропускает клик, пока защёлка
	//   взведена). Теперь состояние честное: не ушло — не ждём.
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14215: отправлен 4104, имя «%s»"), *Clean);
}

void UAsdaClanSubsystem::OnCreateCancelClicked()
{
	CloseCreateWindow();   // 1:1 `case 14416` → `uiRepairWindowToggle(this, 0)`
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14416: окно создания закрыто без отправки"));
}

// ═════════════════════════════ ОКНО РОСПУСКА (30016) ══════════════════════════════════════════

bool UAsdaClanSubsystem::OpenDisbandWindow()
{
	// ★ ПОРЯДОК ГЕЙТОВ 1:1 (`uiRepairWindowToggle_54D0`, `ui_core.c:16149…16158`):
	//   сначала «Друг», потом «есть клан И ты глава».
	// ★ 27.09.2026: общая точка гейта «Друг» (ui_core.c:16149-16152 — БЕЗ снятия верхнего окна-сообщения).
	if (RefuseIfSoulmateLocked(/*bDropTopMsgBox*/false, TEXT("клан 30016: окно роспуска (ui_core.c:16149)"))) { return false; }
	if (SelfClanId() == -1 || SelfClanRank() != 1)
	{
		// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] D8: «клана нет» — свой нотис 991210 (эталон: тот же 12431, что «не глава»).
		const int32 LocId = NoClanLocOr(GetGameInstance(), ClanLoc::kCaptainOnly);
		NoticeMsgBoxLoc(LocId);
		UE_LOG(LogAsdaClan, Log,
			TEXT("[клан] 30016: clanId=%d звание=%d — нотис %d"),
			SelfClanId(), SelfClanRank(), LocId);
		return false;
	}
	if (!EnsureDisbandWidget()) { return false; }

	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D2: показываем имя клана, который распускаем.
	if (TxtDelName)
	{
		const bool bShow = CVarShowNameInDisband.GetValueOnGameThread() != 0;
		TxtDelName->SetText(bShow ? FText::FromString(SelfClanName()) : FText::GetEmpty());
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWinDisband, true);
			WM->BringWindowToFront(kWinDisband);
		}
	}
	// Эталон в ветке открытия закрывает два соседних бокса.
	CloseCreateWindow();
	CloseIncreaseWindow();   // ★ 27.09.2026: второй соседний бокс (ui_core.c:16166); без гейтов — [ПОРТ] исправление дефекта оригинала: лишний 12455 у главы с 4 расширениями (см. `CloseIncreaseWindow`)
	bDisbandOpen = true;
	bAwaitDisband = false;
	Mouse.Reset();
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] окно роспуска 14470 открыто (клан %d «%s»)"),
		SelfClanId(), *SelfClanName());
	return true;
}

void UAsdaClanSubsystem::CloseDisbandWindow()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWinDisband, false);
		}
	}
	bDisbandOpen = false;
	bAwaitDisband = false;
}

void UAsdaClanSubsystem::OnDisbandOkClicked()
{
	if (bAwaitDisband)
	{
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14473: ответ 4107 ещё не пришёл — клик пропущен"));
		return;
	}
	// 1:1 `netSendAcceptAuth@0xBC9270`: те же два гейта ПОВТОРЯЮТСЯ перед отправкой.
	// ★ 27.09.2026: как у 14461 — `itemBuyPanelRefreshOrHide(g_pItemTooltipUI)` перед нотисом (network.c:11984);
	//   общая точка гейта «Друг» (`friend_gate_12435.md`).
	if (RefuseIfSoulmateLocked(/*bDropTopMsgBox*/true, TEXT("клан 14473: отказ «Друг» (network.c:11984)"))) { return; }
	if (SelfClanRank() != 1)
	{
		// Эталон здесь МОЛЧА выходит (нотиса нет) — 1:1.
		UE_LOG(LogAsdaClan, Warning, TEXT("[клан] 14473: звание %d != 1 — эталон молча не шлёт"),
			SelfClanRank());
		return;
	}
	const int32 ClanId = SelfClanId();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			bAwaitDisband = Net->SendClanDelete4106(ClanId);
		}
	}
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14473: отправлен 4106, клан %d"), ClanId);
}

void UAsdaClanSubsystem::OnDisbandCancelClicked()
{
	CloseDisbandWindow();   // 1:1 `case 0x37C2 (14274)` → `uiRepairWindowToggle_54D0(this, 0)`
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14274: окно роспуска закрыто без отправки"));
}

// ═════════════════════════════ ОКНО РАСШИРЕНИЯ (30027) ═════════════════════════════════════════
//   ★ 27.09.2026. Эталон — `uiInventoryRepairDialogToggle@0xBC5600` (открытие/закрытие),
//   `netSendInventoryItemNamePacket@0xBC50D0` (кнопка 14461 → 4136), `uiInventoryRepairResultHandler
//   @0xBC5310` (4137). Числа — по правилам СЕРВЕРА (таблица в шапке .h), ключ отката
//   `asda.Clan.IncreaseRule 1`.

int32 UAsdaClanSubsystem::IncSeatsStep() const
{
	return CVarIncreaseRule.GetValueOnGameThread() == 1 ? kIncSeatsClient : kIncSeatsServer;
}

int32 UAsdaClanSubsystem::IncStepsMax() const
{
	return CVarIncreaseRule.GetValueOnGameThread() == 1 ? kIncStepsClient : kIncStepsServer;
}

int32 UAsdaClanSubsystem::IncCurrentSeats() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaCommunityClanSubsystem* CC = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr;
	return (CC && CC->GetBaseInfo().bValid) ? (int32)CC->GetBaseInfo().MaxMembers() : -1;
}

int32 UAsdaClanSubsystem::IncDoneSteps() const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaCommunityClanSubsystem* CC = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr;
	return (CC && CC->GetBaseInfo().bValid) ? (int32)CC->GetBaseInfo().LevelRaw() : -1;
}

void UAsdaClanSubsystem::FillIncreaseNumbers()
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaCommunityClanSubsystem* CC = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr;
	IncShownRevision = CC ? CC->GetBaseInfoRevision() : -1;

	// 14451 ← имя клана персонажа (`CControl_SetText(incName, &char->gap_93A[34])`).
	if (TxtIncName) { TxtIncName->SetText(FText::FromString(SelfClanName())); }

	const int32 Seats = IncCurrentSeats();
	const int32 Done  = IncDoneSteps();
	// 14453 ← текущий лимит, 14455 ← лимит + шаг. ★ [ПОРТ] Пока блока клана нет (4123 ещё не
	//   приходил), числа НЕ выдумываем и нули эталона не рисуем — поля пустые, блок запрошен 4122.
	if (TxtIncNowVal) { TxtIncNowVal->SetText(Seats >= 0 ? FText::FromString(FString::FromInt(Seats)) : FText::GetEmpty()); }
	if (TxtIncNewVal) { TxtIncNewVal->SetText(Seats >= 0 ? FText::FromString(FString::FromInt(Seats + IncSeatsStep())) : FText::GetEmpty()); }
	// 14457 ← цена ступени `cost[число сделанных расширений]` (эталон: `gap_694[4*n + 268]`).
	if (TxtIncCostVal)
	{
		const bool bCostKnown = Done >= 0 && Done < IncStepsMax() && Done < kIncStepsServer;
		TxtIncCostVal->SetText(bCostKnown ? FText::FromString(AsdaUi::FormatThousands(kIncCostTable[Done])) : FText::GetEmpty());
	}
	// 14459 ← золото (`getFieldIfPositive(g_pGameClient)` = поле `uiSetHonorPointsDisplay`, отрицательное → 0).
	if (TxtIncMonVal)
	{
		const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		const int32 Gold = Inv ? Inv->GetGold() : 0;
		TxtIncMonVal->SetText(FText::FromString(AsdaUi::FormatThousands(Gold > 0 ? Gold : 0)));
	}
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] окно 14450: мест сейчас %d → станет %d, расширений %d из %d, цена %d (правила: %s)"),
		Seats, Seats >= 0 ? Seats + IncSeatsStep() : -1, Done, IncStepsMax(),
		(Done >= 0 && Done < kIncStepsServer) ? kIncCostTable[Done] : -1,
		CVarIncreaseRule.GetValueOnGameThread() == 1 ? TEXT("клиент-эталон") : TEXT("сервер"));
}

bool UAsdaClanSubsystem::OpenIncreaseWindow()
{
	// ★ ПОРЯДОК ГЕЙТОВ 1:1 (`uiInventoryRepairDialogToggle@0xBC5600`): клан → «Друг» → число расширений.
	if (SelfClanId() == -1 && !bSelfTestSkipClanGate)
	{
		const int32 LocId = NoClanLocOr(GetGameInstance(), ClanLoc::kCaptainOnly);   // ★ 08.11.2026 [ИСПРАВЛЕНИЕ] D8: 991210 вместо 12431
		NoticeMsgBoxLoc(LocId);
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 30027: клана нет — нотис %d"), LocId);
		return false;
	}
	// ★ 27.09.2026: общая точка гейта «Друг» (ui_core.c:16193-16196 — БЕЗ снятия верхнего окна-сообщения).
	if (RefuseIfSoulmateLocked(/*bDropTopMsgBox*/false, TEXT("клан 30027: окно расширения (ui_core.c:16193)"))) { return false; }
	// Эталон: `if (gap_616[45] < 4)` — порт: порог СЕРВЕРА (5), см. шапку. Пока блока нет (−1),
	//   гейт пропускает — решит сервер (код 5 → тот же нотис 12455).
	const int32 Done = IncDoneSteps();
	if (Done >= IncStepsMax())
	{
		NoticeMsgBoxLoc(ClanLoc::kInc_5);           // 12455
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 30027: расширений %d из %d — нотис 12455"), Done, IncStepsMax());
		return false;
	}
	if (!EnsureIncreaseWidget()) { return false; }

	// ★ [ПОРТ] Блока клана ещё нет — просим его (4122 → 4123), числа дорисует Tick по ревизии блока.
	//   Эталон берёт кэш кланового окна как есть (до первого 4123 там нули).
	if (IncCurrentSeats() < 0)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
			{
				Net->SendClanBaseInfoReq4122(SelfClanId());
			}
		}
	}
	FillIncreaseNumbers();

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWinIncrease, true);   // `CControl_SetVisible(incBox, 1)`
			WM->BringWindowToFront(kWinIncrease);       // `CClass_MapInsert(g_pWindowMgr, incBox)`
		}
	}
	// Эталон в конце ветки открытия закрывает два соседних бокса (создание и роспуск).
	CloseCreateWindow();
	CloseDisbandWindow();
	bIncOpen = true;
	bAwaitIncrease = false;
	Mouse.Reset();
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] окно расширения 14450 открыто (клан %d «%s»)"), SelfClanId(), *SelfClanName());
	return true;
}

// ★★ [ПОРТ] ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (27.09.2026, `who_clan_fix.md`, СРЕДН-1 ревизии `who_clan_REVIEW.md`).
//   Эталон закрывает окно тем же `uiInventoryRepairDialogToggle(this, 0)` (ui_core.c:16173-16231), и при a2 == 0
//   пропускается только гейт «клана нет» (:16187 `if (a2 && …)`). Гейты «Друг» (:16192 → 12435) и «расширений ≥ 4»
//   (:16197 `gap_616[45] < 4`, иначе :16227 → 12455) срабатывают И ПРИ ЗАКРЫТИИ: вместо закрытия эталон показывает
//   нотис. Закрытие зовут: открытие окна создания (:15767), открытие окна роспуска (:16166), кнопка 14462 (:15298),
//   4137 код 1 (:16115). Видимое следствие в оригинале: глава клана с 4 расширениями открывает у NPC «Распустить
//   клан» и получает лишний нотис 12455 «Клану больше некуда расти» (при флаге «Друг» 14462 и вовсе не закрывает
//   окно). Порт закрывает БЕЗ проверок — это и есть исправление; эталонное поведение не воспроизводится.
void UAsdaClanSubsystem::CloseIncreaseWindow()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWinIncrease, false);
		}
	}
	// Эталон при a2 == 0 поля НЕ чистит — только прячет панель.
	bIncOpen = false;
	bAwaitIncrease = false;
}

void UAsdaClanSubsystem::OnIncreaseOkClicked()
{
	if (bAwaitIncrease)
	{
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14461: ответ 4137 ещё не пришёл — клик пропущен"));
		return;
	}
	// 1:1 `netSendInventoryItemNamePacket@0xBC50D0`: гейт «Друг» повторяется перед отправкой.
	// ★ 27.09.2026 (МЕЛК-2 ревизии): перед нотисом эталон зовёт `itemBuyPanelRefreshOrHide(g_pItemTooltipUI)`
	//   (network.c:11775) — снять верхнюю запись окон-сообщений или спрятать их панели; общая точка гейта «Друг».
	if (RefuseIfSoulmateLocked(/*bDropTopMsgBox*/true, TEXT("клан 14461: отказ «Друг» (network.c:11775)"))) { return; }
	const int32 ClanId = SelfClanId();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			// Защёлка — только при реально ушедшем пакете (закон `no-state-latch-before-shown`).
			bAwaitIncrease = Net->SendClanIncrease4136(ClanId);
		}
	}
	// Эталон окно НЕ закрывает: его закрывает только 4137 код 1.
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14461: 4136 расширение клана %d -> %s"), ClanId,
		bAwaitIncrease ? TEXT("отправлен") : TEXT("НЕ отправлен"));
}

void UAsdaClanSubsystem::OnIncreaseCancelClicked()
{
	CloseIncreaseWindow();   // `case 0x387E (14462)` (ui_core.c:15298); без гейтов — [ПОРТ] исправление дефекта оригинала (см. `CloseIncreaseWindow`)
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 14462: окно расширения закрыто без отправки"));
}

// ═════════════════════════════ ПРИЁМ 4137 (расширение) ═══════════════════════════════════════

void UAsdaClanSubsystem::HandleIncreaseResult(const TArray<uint8>& Body)
{
	if (Body.Num() < 1) { return; }
	bAwaitIncrease = false;
	const int32 Code = (int32)Body[0];
	UGameInstance* GI = GetGameInstance();

	if (Code == 1)
	{
		// Эталон (ui_core.c:16114-16118): закрыть окно, `uiSetHonorPointsDisplay(g_pGameClient, @4)`, `gap_616[43] = @3`,
		//   нотис 12451. Закрытие без гейтов — [ПОРТ] исправление дефекта оригинала (см. `CloseIncreaseWindow`).
		CloseIncreaseWindow();
		if (Body.Num() >= 8)
		{
			if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
			{
				Inv->SetGoldDisplay(AsdaBytes::RdI32(Body.GetData() + 4));
			}
		}
		if (Body.Num() >= 4)
		{
			if (UAsdaCommunityClanSubsystem* CC = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr)
			{
				CC->ApplyIncreasedMaxMembers(Body[3]);
			}
		}
		NoticeMsgBoxLoc(ClanLoc::kInc_1);
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 4137 код 1: клан расширен, лимит членов %d"),
			Body.Num() >= 4 ? (int32)Body[3] : -1);
		return;
	}

	int32 LocId = 0;
	switch (Code)
	{
	case 0: LocId = ClanLoc::kInc_0; break;
	case 2: LocId = ClanLoc::kInc_2; break;
	case 3: LocId = ClanLoc::kInc_3; break;
	case 4: LocId = ClanLoc::kInc_4; break;
	case 5: LocId = ClanLoc::kInc_5; break;
	default:
		UE_LOG(LogAsdaClan, Warning, TEXT("[клан] 4137 код %d: эталон его игнорирует"), Code);
		return;
	}
	NoticeMsgBoxLoc(LocId);
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 4137 код %d → нотис %d"), Code, LocId);
}

// ═════════════════════════════ ПРИЁМ 4105 (создание) ══════════════════════════════════════════

void UAsdaClanSubsystem::HandleCreateResult(const TArray<uint8>& Body)
{
	if (Body.Num() < 1) { return; }
	bAwaitCreate = false;
	const int32 Code = (int32)Body[0];
	UGameInstance* GI = GetGameInstance();

	if (Code == 1)
	{
		// Эталон: имя клана в слот 0 нотиса, затем 20050 «Поздравляем. Клан '[ss1]' создан.».
		FString Name;
		if (Body.Num() >= 23) { Name = FAsdaLoginData::DecodeCp1251(Body.GetData() + 3, 20); }
		FString Text;
		if (UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			Text = TS->FormatById(ClanLoc::kMakeOk, { Name }, {});
		}
		if (Text.IsEmpty()) { Text = Loc(ClanLoc::kMakeOk).Replace(TEXT("[ss1]"), *Name); }
		NoticeMsgBox(Text);
		// Поля персонажа (clanId/имя/звание) и золото уже обновил приём в `AsdaNetSubsystem`.
		// Эталон следом шлёт 4138 — «прислать состав клана» (имя серверной функции врёт).
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->SendClanRosterRefresh4138();
		}
		CloseCreateWindow();
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 4105 код 1: клан «%s» создан"), *Name);
		return;
	}

	int32 LocId = 0;
	switch (Code)
	{
	case 0:  LocId = ClanLoc::kMake_0;  break;
	case 2:  LocId = ClanLoc::kMake_2;  break;
	case 3:  LocId = ClanLoc::kMake_3;  break;
	case 4:  LocId = ClanLoc::kMake_4;  break;
	case 6:  LocId = ClanLoc::kMake_6;  break;
	case 7:  LocId = ClanLoc::kMake_7;  break;
	case 8:  LocId = ClanLoc::kMake_8;  break;
	case 9:  LocId = ClanLoc::kMake_9;  break;
	case 10: LocId = ClanLoc::kMake_10; break;
	default:
		// ★ ДЕФЕКТ ОРИГИНАЛА D5: код 5 (и любой другой) эталон молча игнорирует — 1:1.
		UE_LOG(LogAsdaClan, Warning, TEXT("[клан] 4105 код %d: эталон его игнорирует"), Code);
		return;
	}
	NoticeMsgBoxLoc(LocId);
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 4105 код %d → нотис %d"), Code, LocId);
}

// ═════════════════════════════ ПРИЁМ 4107 (роспуск) ═══════════════════════════════════════════

void UAsdaClanSubsystem::HandleDisbandResult(const TArray<uint8>& Body)
{
	if (Body.Num() < 1) { return; }
	bAwaitDisband = false;
	const int32 Code = (int32)Body[0];

	if (Code == 1)
	{
		// Эталон: `sprintf(buf, "%s%s", имяКлана, loc 12410)` — имя берётся из КЭША окна
		//   (`self->gap_616` = window+1558), у нас — из зеркала своего клана ДО сброса.
		// ★ 06.10.2026 (ревизия портрета, вне задачи): БЫЛО `SelfClanName()` = `Net->SelfClanName`, а сеть очищает
		//   его ДО рассылки 4107 — нотис выходил без имени. `gap_616` = блок клана окна сообщества (`selMemberIdxA`
		//   + 2): его заполняют 4123/4131/4132 код 1 и обнуляет 4132 код 2 — в порту это зеркало
		//   `UAsdaCommunityClanSubsystem::GetBaseInfo()` (имя клана @2, 18 б). 4107 его не трогает — 1:1.
		const UAsdaCommunityClanSubsystem* CCn = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr;
		const FString Name = CCn ? CCn->GetBaseInfo().ClanName() : FString();   // кэш окна, как gap_616
		const FString Tail = Loc(ClanLoc::kDel_1);
		NoticeMsgBox(Name + Tail);
		CloseDisbandWindow();
		UE_LOG(LogAsdaClan, Log, TEXT("[клан] 4107 код 1: клан «%s» распущен"), *Name);
		return;
	}

	int32 LocId = 0;
	switch (Code)
	{
	case 0: LocId = ClanLoc::kDel_0; break;
	case 2: LocId = ClanLoc::kDel_2; break;
	case 3: LocId = ClanLoc::kDel_3; break;
	case 4: LocId = ClanLoc::kDel_4; break;
	default:
		UE_LOG(LogAsdaClan, Warning, TEXT("[клан] 4107 код %d: эталон его игнорирует"), Code);
		return;
	}
	NoticeMsgBoxLoc(LocId);
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 4107 код %d → нотис %d"), Code, LocId);
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaClanSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaClanSubsystem::NoticeMsgBox(const FString& Text, int32 EtalonNoticeId) const
{
	// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст)` = `ONEBUT_MSGBOX` 1500.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Social, Text, AsdaLoc::kBtnOk, EtalonNoticeId);
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaClanSubsystem::DumpControls() const
{
	FString S = TEXT("=== клановые боксы: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W)
	{ S += FString::Printf(TEXT("  %-46s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	S += TEXT("-- CLAN_MAKING_WIN 14400 (создание, код NPC 30015)\n");
	Line(TEXT("14400 CLAN_MAKING_WIN (окно)"),        CreateWidget);
	Line(TEXT("14404 CLAN_TITLE (12216, скрыт в JUI)"), TxtMakeTitle);
	Line(TEXT("14403 CLAN_NAME (12217)"),             TxtMakeNameLb);
	Line(TEXT("14402 CLAN_NAME_INPUT (ввод)"),        NameInput);
	Line(TEXT("14405 CLAN_TYPE (12218, скрыт в JUI)"), TxtMakeTypeLb);
	Line(TEXT("14407 CLAN_TYPE1 (12219)"),            TxtMakeType1);
	Line(TEXT("14408 CLAN_TYPE2 (12220)"),            TxtMakeType2);
	Line(TEXT("14409 CLAN_FEE (12221)"),              TxtMakeFeeLb);
	Line(TEXT("14410 CLAN_FEE_VALUE (10 000)"),       TxtMakeFeeVal);
	Line(TEXT("14412 CLAN_MONEY (12222)"),            TxtMakeMonLb);
	Line(TEXT("14413 CLAN_MONEY_VALUE (золото)"),     TxtMakeMonVal);
	Line(TEXT("14215 CLAN_MAKE_BTN (12223)"),         BtnMakeOk);
	Line(TEXT("14416 CLAN_CANCEL_BTN (2289)"),        BtnMakeCancel);
	S += TEXT("  14406 CLAN_TYPE_BTN                            мёртвый контрол (дефект D1)\n");
	S += TEXT("-- CLAN_DELETE_BOX 14470 (роспуск, код NPC 30016)\n");
	Line(TEXT("14470 CLAN_DELETE_BOX (окно)"),        DisbandWidget);
	Line(TEXT("14471 CLAN_DEL_NAME (дефект D2)"),     TxtDelName);
	Line(TEXT("14272 CLAN_DEL_TEXT (12449)"),         TxtDelText);
	Line(TEXT("14473 CLAN_DEL_OKBTN (2288)"),         BtnDelOk);
	Line(TEXT("14274 CLAN_DEL_CABTN (2289)"),         BtnDelCancel);
	S += TEXT("-- CLAN_INCREASE_BOX 14450 (расширение, код NPC 30027; кнопка — у Эллы 107, добавка порта 27.09)\n");
	Line(TEXT("14450 CLAN_INCREASE_BOX (окно)"),       IncWidget);
	Line(TEXT("14451 CLAN_INC_NAME (имя клана)"),      TxtIncName);
	Line(TEXT("14452 CLAN_INC_MEMNOW (12444)"),        TxtIncNowLb);
	Line(TEXT("14453 CLAN_INC_MEMNOW_V (лимит)"),      TxtIncNowVal);
	Line(TEXT("14454 CLAN_INC_MEMINC (12445)"),        TxtIncNewLb);
	Line(TEXT("14455 CLAN_INC_MEMINC_V (станет)"),     TxtIncNewVal);
	Line(TEXT("14456 CLAN_INC_COST (12446)"),          TxtIncCostLb);
	Line(TEXT("14457 CLAN_INC_COST_V (цена)"),         TxtIncCostVal);
	Line(TEXT("14458 CLAN_INC_MONEY (12447)"),         TxtIncMonLb);
	Line(TEXT("14459 CLAN_INC_MONEY_V (золото)"),      TxtIncMonVal);
	Line(TEXT("14460 CLAN_INC_TEXT (12448)"),          TxtIncText);
	Line(TEXT("14461 CLAN_INC_OKBTN (2288)"),          BtnIncOk);
	Line(TEXT("14462 CLAN_INC_CABTN (2289)"),          BtnIncCancel);
	return S;
}

FString UAsdaClanSubsystem::DumpState() const
{
	FString S = TEXT("=== клановые боксы: состояние ===\n");
	S += FString::Printf(TEXT("  создание: открыто=%s готово=%s ждём 4105=%s\n"),
		bCreateOpen ? TEXT("да") : TEXT("нет"), bCreateReady ? TEXT("да") : TEXT("нет"),
		bAwaitCreate ? TEXT("да") : TEXT("нет"));
	S += FString::Printf(TEXT("  роспуск:  открыто=%s готово=%s ждём 4107=%s\n"),
		bDisbandOpen ? TEXT("да") : TEXT("нет"), bDisbandReady ? TEXT("да") : TEXT("нет"),
		bAwaitDisband ? TEXT("да") : TEXT("нет"));
	S += FString::Printf(TEXT("  свой клан: id=%d «%s» звание=%d (1 = глава)\n"),
		SelfClanId(), *SelfClanName(), SelfClanRank());
	S += FString::Printf(TEXT("  признак «Друг» (char+0x8C1) = %s\n"),
		IsSoulmateLocked() ? TEXT("стоит → клановые операции запрещены (12435)") : TEXT("нет"));
	S += FString::Printf(TEXT("  золото=%lld, взнос за создание=%d\n"), SelfGold(), kCreateFeeGold);
	S += FString::Printf(TEXT("  расширение: открыто=%s готово=%s ждём 4137=%s · лимит %d, расширений %d из %d, шаг +%d (правила: %s)\n"),
		bIncOpen ? TEXT("да") : TEXT("нет"), bIncReady ? TEXT("да") : TEXT("нет"),
		bAwaitIncrease ? TEXT("да") : TEXT("нет"), IncCurrentSeats(), IncDoneSteps(), IncStepsMax(), IncSeatsStep(),
		CVarIncreaseRule.GetValueOnGameThread() == 1 ? TEXT("клиент-эталон") : TEXT("сервер"));
	S += FString::Printf(TEXT("  текст поля 14402: «%s»\n"), *GetClanNameInput());
	// ★ 27.09.2026: входящее приглашение в клан (4110/4111).
	S += FString::Printf(TEXT("  приглашение: окно ответа=%s (прошло %.1f с из %.0f) · clanId=%d «%s» пригласил=%d мне=%d\n"),
		bInviteBoxActive ? TEXT("открыто") : TEXT("нет"),
		bInviteBoxActive ? FPlatformTime::Seconds() - InviteBoxStart : 0.0, kInviteTimeoutSec,
		InviteClanId, *FAsdaLoginData::DecodeCp1251(InviteClanName.GetData(), InviteClanName.Num()), InviteReqUser, InviteRespUser);
	return S;
}

// ═════════════════ ★ 27.09.2026: ВХОДЯЩЕЕ ПРИГЛАШЕНИЕ В КЛАН 4110 → 4111 (`friend_gate_12435.md`) ═════════════════
//   Поток эталона целиком (строки — в объявлениях `AsdaClanSubsystem.h`):
//     4110 → `Recv_PartyMember` case 6: окно ответа не открыто И значка 6 нет → значок 6 + звук 215;
//            `itemSetSplitMoveSource`: поля @0/@2/@19/@21 пишутся ВСЕГДА (и при открытом окне);
//     клик по значку → полоса 1522 видна, окно (1, 44) loc 12430, признак «окно открыто»;
//     «Да» → 4111(1), «Нет»/ESC → 4111(0), 30 с → 4111(0) + закрытие; перед 4111 — гейт «Друг» со снятием верхнего окна.

void UAsdaClanSubsystem::HandleInviteRecv(const TArray<uint8>& Body)
{
	if (Body.Num() < kInviteBodyLen)
	{
		UE_LOG(LogAsdaClan, Warning, TEXT("[клан] 4110: тело %d б < %d — отброшено"), Body.Num(), kInviteBodyLen);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	// `Recv_PartyMember(g_pQuickSlotBar, 6, …)` case 6 (network.c:27378-27390).
	if (!bInviteBoxActive)
	{
		UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
		if (Hb && !Hb->HasNoticeIcon(kNoticeTypeClanInvite))   // цикл по 7 слотам `aNoticeType == 6` → return
		{
			Hb->AddNoticeIcon(kNoticeTypeClanInvite);           // `statusBarAddIcon` + `MsgNotify_RefreshIcons`
			if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound2DById(kSoundInvite); }
		}
	}
	// `itemSetSplitMoveSource@0xBC9E40` (item_inventory.c:7334-7341) — БЕЗУСЛОВНО.
	InviteClanId = AsdaBytes::RdI16(Body.GetData() + 0);
	InviteClanName.Reset();
	InviteClanName.Append(Body.GetData() + 2, kInviteNameLen);
	InviteReqUser = AsdaBytes::RdI16(Body.GetData() + 19);
	InviteRespUser = AsdaBytes::RdI16(Body.GetData() + 21);
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] 4110 приглашение: clanId=%d «%s» пригласил=%d мне=%d%s"),
		InviteClanId, *FAsdaLoginData::DecodeCp1251(InviteClanName.GetData(), InviteClanName.Num()), InviteReqUser, InviteRespUser,
		bInviteBoxActive ? TEXT(" — окно ответа уже открыто: значка нет, поля перезаписаны (как эталон)") : TEXT(""));
}

void UAsdaClanSubsystem::OnInviteNoticeIconClicked()
{
	// `uiNoticeButtonDispatch` case 6 (ui_core.c:69418-69424): `sub_D78FB0(g_pItemTooltipUI, 1)` — показать полосу 1522;
	//   `uiWindow_Open`; `CItem_BuildTooltipText(g_pItemTooltipUI, 1, 44, loc 12430)`; `dword_2BE50FC = 1`.
	//   Старт таймера эталон берёт на первом тике после признака (`gap_32D4[16] == 0`) — тот же кадр.
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	InviteBoxStart = FPlatformTime::Seconds();
	bInviteBoxActive = true;   // эталон ставит признак и тогда, когда показ отброшен дедупом пары (1, 44)
	if (!WM) { return; }
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Loc(ClanLoc::kInviteAsk), [this](bool bYes)
	{
		// «Да» — `case 0x2C` (ui_core.c:57432-57434), «Нет»/ESC — ветка 1524 (:57740-57742): 4111 и сброс старта таймера.
		SendInviteAnswer(bYes);
		EndInviteBox(bYes ? TEXT("ответ «Да»") : TEXT("ответ «Нет»"));
	}, 0.f, 2288, 2289, kInviteBoxId, kInviteBoxKind, (float)kInviteTimeoutSec, InviteBoxStart);
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] значок 6: окно (1, 44) «%s», полоса %.0f с"), *Loc(ClanLoc::kInviteAsk), kInviteTimeoutSec);
}

bool UAsdaClanSubsystem::SendInviteAnswer(bool bAccept)
{
	// `itemSendSplitOrMoveRequest@0xBC9ED0` (item_inventory.c:7345-7389): гейт «Друг» — поля НЕ сбрасываются.
	if (RefuseIfSoulmateLocked(/*bDropTopMsgBox*/true, TEXT("4111 ответ на приглашение (item_inventory.c:7360)"))) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendClanInviteAnswer4111(bAccept, InviteClanId, InviteClanName, InviteReqUser, InviteRespUser);
	// После `CNetwork_Send` — сброс БЕЗУСЛОВНО (item_inventory.c:7383-7387): поля → −1 / 0 / −1 / −1.
	//   `*(DWORD*)(wnd + 0x60C) = 0` не переносится: у поля нет ни одного чтения в классе (дизасм — только запись
	//   конструктора 0xBBBBE0 и эта 0xBCA082; «ссылок в срезе нет»).
	InviteClanId = -1;
	InviteClanName.Reset();
	InviteReqUser = -1;
	InviteRespUser = -1;
	return bOk;
}

void UAsdaClanSubsystem::EndInviteBox(const TCHAR* Why)
{
	// [ПОРТ] Дефект оригинала D8 (шапка): эталон признак `dword_2BE50FC` не снимает никогда — порт снимает здесь.
	if (!bInviteBoxActive) { return; }
	bInviteBoxActive = false;
	InviteBoxStart = -1.0;
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] окно ответа на приглашение закрыто (%s) — признак снят [ПОРТ, D8]"), Why);
}

void UAsdaClanSubsystem::TickInviteTimer()
{
	// `uiProgressTickTimedAction@0xD78C20` (ui_core.c:58335-58379), вызов из цикла при `dword_2BE50FC` (game_main.c:2057).
	if (!bInviteBoxActive || InviteBoxStart < 0.0) { return; }
	if (FPlatformTime::Seconds() - InviteBoxStart <= kInviteTimeoutSec) { return; }   // исход — при > 100 %
	UE_LOG(LogAsdaClan, Log, TEXT("[клан] окно ответа на приглашение: %.0f с истекли — 4111 «нет»"), kInviteTimeoutSec);
	// `itemSendSplitOrMoveRequest(g_pCommunityWnd, 0)` — тот же отправитель с гейтом «Друг».
	SendInviteAnswer(false);
	// `uiItemUpgradeWnd_CloseDispatch` (id 44 → `CGameUI_CloseWindow(вид)`): эталон прячет окно, но запись (1, 44) остаётся
	//   в очереди и тут же возвращается. [ПОРТ, D8] снимаем СВОЮ запись (ответ уже отправлен) + звук закрытия 5.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->RemoveMsgBoxEntries(kInviteBoxKind, kInviteBoxId, TEXT("приглашение в клан: таймаут 30 с")) > 0)
		{
			WM->PlayMsgBoxSfx(5, TEXT("закрытие окна приглашения по таймеру (CGameUI_CloseWindow)"));
		}
	}
	EndInviteBox(TEXT("таймаут 30 с"));
}

// ═════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═════════════════════════════════════════════

namespace
{
	UAsdaClanSubsystem* ClanSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaClanSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanCreate(
	TEXT("asda.Clan.OpenCreate"),
	TEXT("asda.Clan.OpenCreate — открыть окно 14400 так же, как код кнопки NPC 30015"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaClanSubsystem* S = ClanSub(W)) { S->OpenCreateWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanDisband(
	TEXT("asda.Clan.OpenDisband"),
	TEXT("asda.Clan.OpenDisband — открыть окно 14470 так же, как код кнопки NPC 30016"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaClanSubsystem* S = ClanSub(W)) { S->OpenDisbandWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

// ═════════════════════════════ САМОТЕСТ ОКНА РАСШИРЕНИЯ (27.09.2026) ═════════════════════════
//
// ★ Редакция 27.09.2026 (`Reports/owner_0927/who_clan_fix.md`, СРЕДН-3 ревизии `who_clan_REVIEW.md`).
//   КАК ПРОВЕРЯЕТ. Пакеты 4131/4132/4137 подаются через ШТАТНЫЙ приём `UAsdaNetSubsystem::DebugFeedPacket` →
//   `HandlePacket` (switch опкодов, проверки длины `NetNeedLen`, делегаты `OnClanBaseInfoPush`/`OnClanIncreaseResult`),
//   ответ 4137 проходит очередь `PendingIncrease` и разбирается только в `Tick` (его и прокачиваем). Отправка 4136 —
//   БОЕВОЙ `SendClanIncrease4136` под узким перехватом `ArmSendCapture(…, 4136)`: в сеть ничего не уходит.
//   Ожидания заданы ЯВНО по эталону (коды 4137 → loc 12450+код, `ui_core.c:16108-16140`; раскладка 4136 —
//   network.c:11781-11793; блок 4132 код 2 — ui_core.c:17193-17215), а не вызовом проверяемой функции.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs):
//     • 4132 длиной 192 применяется ↔ длиной 191 отброшен; 4131 длиной 191 ↔ 190;
//     • 4137 в очереди до `Tick` — нотиса нет ↔ после `Tick` есть; коды 0/2/3/4/5 — нотис, окно ОТКРЫТО ↔ код 1 — закрыто;
//       код 6 — молчание; пустое тело 4137 — отброшено (ожидание не снято) ↔ код 0 снимает ожидание;
//     • перерисовка по ревизии блока: до `Tick` старое число ↔ после — новое;
//     • 4136: первый клик шлёт ↔ второй при ожидании молчит; при флаге «Друг» не шлёт;
//     • «Друг» при непустой очереди: верхняя запись снята и видно 12435 ↔ без снятия 12435 глушился бы дублем (0, −1);
//     • закрытие [ПОРТ] без гейтов: при флаге «Друг» и при 4 расширениях (правило эталона) окно закрыто БЕЗ нотиса.
//   Правило, флаг «Друг», золото восстанавливаются; очередь окон-сообщений снимается между шагами.

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
int32 UAsdaClanSubsystem::RunIncreaseSelfTest()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaCommunityClanSubsystem* CC = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Net || !CC || !WM)
	{
		UE_LOG(LogAsdaClan, Error, TEXT("[клан-самотест] нет сетевой, клановой или оконной подсистемы"));
		return 1;
	}
	int32 Fails = 0;   // число проваленных проверок
	int32 Oks = 0;     // число пройденных проверок
	auto Check = [&Fails, &Oks](bool bOk, const FString& What)
	{
		if (bOk) { ++Oks; } else { ++Fails; }
		UE_LOG(LogAsdaClan, Log, TEXT("[клан-самотест] %s — %s"), bOk ? TEXT("ОК") : TEXT("ПРОВАЛ"), *What);
	};
	auto TextOf = [](const UTextBlock* Tb) { return Tb ? Tb->GetText().ToString() : FString(TEXT("<нет узла>")); };
	// Подать пакет через штатный разбор (без `Tick`).
	auto Feed = [Net](int32 Op, const TArray<uint8>& Body) { Net->DebugFeedPacket(Op, Body); };
	// Один кадр подсистемы: очередь 4137 и перерисовка по ревизии блока.
	auto Pump = [this]() { Tick(0.f); };
	// Тело блока клана 191 б (clanId 7, имя «SelfTest», лимит @45, число расширений @47) со сдвигом Off и длиной Len.
	auto MakeBlock = [](int32 Off, int32 Len, uint8 Cap, uint8 Level)
	{
		TArray<uint8> Body; Body.SetNumZeroed(Len);
		auto Put = [&Body, Off](int32 At, uint8 V) { if (Off + At < Body.Num()) { Body[Off + At] = V; } };   // запись с учётом усечения
		Put(0, 7); Put(1, 0);                         // i16 clanId
		const char* Name = "SelfTest";                // имя клана @2
		for (int32 i = 0; Name[i]; ++i) { Put(2 + i, (uint8)Name[i]); }
		Put(45, Cap);
		Put(47, Level);
		return Body;
	};
	auto Feed4132 = [&](uint8 Cap, uint8 Level, int32 Len = 192)
	{
		TArray<uint8> B = MakeBlock(1, Len, Cap, Level);
		if (B.Num() > 0) { B[0] = 1; }                // код 1 — блок с тела+1
		Feed(4132, B);
	};
	auto Feed4131 = [&](uint8 Cap, uint8 Level, int32 Len = 191) { Feed(4131, MakeBlock(0, Len, Cap, Level)); };
	// Тело 4137 (9 б): @0 код, @1 i16 clanId, @3 лимит, @4 i32 золото, @8 число расширений.
	auto Make4137 = [](uint8 Code, uint8 Cap, int32 Gold, uint8 Steps)
	{
		TArray<uint8> R; R.SetNumZeroed(9);
		R[0] = Code; R[1] = 7; R[3] = Cap;
		FMemory::Memcpy(R.GetData() + 4, &Gold, 4);
		R[8] = Steps;
		return R;
	};
	auto QTop = [WM]() { return WM->GetMsgBoxQueueTopText(); };
	auto QNum = [WM]() { return WM->GetMsgBoxQueueNum(); };
	auto QClear = [WM](const TCHAR* Why) { WM->ClearMsgBoxQueue(Why); };

	IConsoleVariable* Rule = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Clan.IncreaseRule"));
#if UE_BUILD_SHIPPING
	const int32 RuleWas = Rule ? Rule->GetInt() : 0;   // ключ до прогона (вне shipping исходное хранит `AsdaCVarSave`)
#endif
	const uint8 SoulWas = Net->SelfSoulSilhouette;
	const int32 GoldWas = Inv ? Inv->GetGold() : 0;
	if (Rule) { AsdaCVarSave::SetForTest(TEXT("asda.Clan.IncreaseRule"), 0); }
	Net->SelfSoulSilhouette = 0;
	bSelfTestSkipClanGate = true;
	PendingIncrease.Clear();
	QClear(TEXT("клан-самотест: старт"));

	// ── 1. 4132 / 4131 через разбор: длина 192/191 применяется, на байт короче — отброшено ──
	Feed4132(30, 2);
	Check(CC->GetBaseInfo().MaxMembers() == 30 && CC->GetBaseInfo().LevelRaw() == 2, TEXT("4132 код 1 (192 б) → блок: лимит 30, расширений 2"));
	Feed4132(99, 3, 191);
	Check(CC->GetBaseInfo().MaxMembers() == 30 && CC->GetBaseInfo().LevelRaw() == 2,
		FString::Printf(TEXT("контроль: 4132 код 1 длиной 191 отброшен — лимит %d (ждём 30)"), (int32)CC->GetBaseInfo().MaxMembers()));
	Feed4131(35, 2);
	Check(CC->GetBaseInfo().MaxMembers() == 35, FString::Printf(TEXT("4131 (191 б) → лимит %d (ждём 35)"), (int32)CC->GetBaseInfo().MaxMembers()));
	Feed4131(77, 4, 190);
	Check(CC->GetBaseInfo().MaxMembers() == 35 && CC->GetBaseInfo().LevelRaw() == 2,
		FString::Printf(TEXT("контроль: 4131 длиной 190 отброшен — лимит %d (ждём 35)"), (int32)CC->GetBaseInfo().MaxMembers()));

	// ── 2. Окно по серверным правилам: лимит 30, сделано 2 → «30 → 40», цена 3-й ступени 400 000 ──
	Feed4132(30, 2);
	Check(OpenIncreaseWindow(), TEXT("окно 14450 открылось (сделано 2 из 5)"));
	Check(TextOf(TxtIncNowVal) == TEXT("30"), FString::Printf(TEXT("14453 «сейчас» = «%s» (ждём 30)"), *TextOf(TxtIncNowVal)));
	Check(TextOf(TxtIncNewVal) == TEXT("40"), FString::Printf(TEXT("14455 «станет» = «%s» (ждём 40 — шаг сервера +10)"), *TextOf(TxtIncNewVal)));
	Check(TextOf(TxtIncCostVal) == AsdaUi::FormatThousands(400000), FString::Printf(TEXT("14457 цена = «%s» (ждём 400,000)"), *TextOf(TxtIncCostVal)));
	Check(TextOf(TxtIncNowLb) == Loc(12444) && !TextOf(TxtIncNowLb).IsEmpty(), FString::Printf(TEXT("14452 подпись = loc 12444 «%s»"), *TextOf(TxtIncNowLb)));
	Check(TextOf(TxtIncText) == Loc(12448) && !TextOf(TxtIncText).IsEmpty(), FString::Printf(TEXT("14460 вопрос = loc 12448 «%s»"), *TextOf(TxtIncText)));
	Check(TextOf(TxtIncOk) == Loc(2288), FString::Printf(TEXT("14461 подпись = loc 2288 «%s»"), *TextOf(TxtIncOk)));

	// ── 3. Перерисовка по ревизии блока: 4131 при открытом окне — до `Tick` старое, после — новое ──
	Feed4131(45, 2);
	Check(TextOf(TxtIncNowVal) == TEXT("30"), FString::Printf(TEXT("контроль: до Tick 14453 = «%s» (ждём прежнее 30)"), *TextOf(TxtIncNowVal)));
	Pump();
	Check(TextOf(TxtIncNowVal) == TEXT("45") && TextOf(TxtIncNewVal) == TEXT("55"),
		FString::Printf(TEXT("после Tick 14453/14455 = «%s»/«%s» (ждём 45/55)"), *TextOf(TxtIncNowVal), *TextOf(TxtIncNewVal)));

	// ── 4. Отправка 4136: БОЕВОЙ отправитель под узким перехватом ──
	{
		struct FCap { TArray<uint8> Payload; TCHAR Env = 0; };   // один перехваченный 4136
		TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();
		const uint32 Gen = Net->ArmSendCapture([Caps](uint16 Op, const uint8* P, int32 Len, bool /*bPad*/, TCHAR Env)
		{
			if (Op != 4136) { return; }
			FCap C; C.Env = Env;
			if (P && Len > 0) { C.Payload.Append(P, Len); }
			Caps->Add(MoveTemp(C));
		}, (uint16)4136);
		bAwaitIncrease = false;
		OnIncreaseOkClicked();
		Check(Caps->Num() == 1, FString::Printf(TEXT("14461 → ушёл один 4136 (поймано %d)"), Caps->Num()));
		if (Caps->Num() >= 1)
		{
			const TArray<uint8>& P = (*Caps)[0].Payload;
			const bool bLen = P.Num() == 7;
			Check(bLen, FString::Printf(TEXT("4136: тело %d б (ждём 7; конверт 8+16+7 = 31)"), P.Num()));
			if (bLen)
			{
				// ★ 27.09.2026 (аудит charId): @0 — `char+0x41C` = charId, не userNum.
				Check(AsdaBytes::RdI32(P.GetData()) == Net->SelectedCharId,
					FString::Printf(TEXT("4136 @0 i32 userIdx(charId) = %d (ждём %d)"), AsdaBytes::RdI32(P.GetData()), Net->SelectedCharId));
				Check(AsdaBytes::RdI16(P.GetData() + 4) == (int32)(int16)SelfClanId(),
					FString::Printf(TEXT("4136 @4 i16 clanId = %d (ждём %d)"), AsdaBytes::RdI16(P.GetData() + 4), SelfClanId()));
				Check(P[6] == 5, FString::Printf(TEXT("4136 @6 u8 = %d (ждём литерал 5, 0xBC5144)"), (int32)P[6]));
			}
			Check((*Caps)[0].Env == TEXT('S'), FString::Printf(TEXT("4136 конверт «%c» (ждём S — токен + MD5)"), (*Caps)[0].Env ? (*Caps)[0].Env : TEXT('?')));
		}
		Check(bAwaitIncrease && bIncOpen, TEXT("после 4136 ждём 4137, окно не закрыто (закрывает только код 1)"));
		OnIncreaseOkClicked();
		Check(Caps->Num() == 1, FString::Printf(TEXT("контроль: второй клик при ожидании 4137 пакета не шлёт (поймано %d)"), Caps->Num()));

		// Флаг «Друг»: 4136 не уходит, верхняя запись очереди снимается, виден 12435 (network.c:11773-11777).
		bAwaitIncrease = false;
		Net->SelfSoulSilhouette = 1;
		QClear(TEXT("клан-самотест: «Друг» без очереди"));
		OnIncreaseOkClicked();
		Check(Caps->Num() == 1 && QNum() == 1 && QTop() == Loc(12435),
			FString::Printf(TEXT("«Друг», очередь пуста: 4136 не ушёл, нотис «%s» (ждём loc 12435)"), *QTop()));
		QClear(TEXT("клан-самотест: «Друг» с записью"));
		AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, TEXT("клан-самотест: чужая запись"));
		const bool bDummy = QNum() == 1;
		OnIncreaseOkClicked();
		Check(bDummy && Caps->Num() == 1 && QNum() == 1 && QTop() == Loc(12435),
			FString::Printf(TEXT("«Друг», в очереди чужая запись (0, −1): она снята, видно «%s», записей %d (ждём 12435 и 1; без снятия 12435 глушился бы дублем)"),
				*QTop(), QNum()));
		// Закрытие при флаге «Друг»: эталон показал бы 12435 и НЕ закрыл (ui_core.c:16192) — порт закрывает молча [ПОРТ].
		QClear(TEXT("клан-самотест: закрытие при «Друг»"));
		OnIncreaseCancelClicked();
		Check(!bIncOpen && QNum() == 0, FString::Printf(TEXT("[ПОРТ] 14462 при «Друг» закрыл окно без нотиса (записей %d)"), QNum()));
		Net->SelfSoulSilhouette = 0;
		Net->DisarmSendCapture(Gen);
	}

	// ── 5. 4137 коды отказа через разбор и очередь: нотис 12450+код, окно ОТКРЫТО, лимит не тронут ──
	Check(OpenIncreaseWindow(), TEXT("окно снова открыто для кодов отказа"));
	for (const uint8 Code : { (uint8)0, (uint8)2, (uint8)3, (uint8)4, (uint8)5 })
	{
		QClear(TEXT("клан-самотест: код отказа"));
		bAwaitIncrease = true;
		Feed(4137, Make4137(Code, 99, 1, 9));
		const bool bQuietBeforeTick = QNum() == 0 && bAwaitIncrease;
		Pump();
		const int32 LocId = 12450 + Code;
		Check(bQuietBeforeTick && bIncOpen && !bAwaitIncrease && QTop() == Loc(LocId) && CC->GetBaseInfo().MaxMembers() == 45,
			FString::Printf(TEXT("4137 код %d: до Tick тихо=%d; после — нотис «%s» (ждём loc %d), окно открыто=%d, лимит %d (ждём 45)"),
				(int32)Code, bQuietBeforeTick ? 1 : 0, *QTop(), LocId, bIncOpen ? 1 : 0, (int32)CC->GetBaseInfo().MaxMembers()));
	}
	QClear(TEXT("клан-самотест: код 6"));
	Feed(4137, Make4137(6, 99, 1, 9)); Pump();
	Check(QNum() == 0 && bIncOpen, TEXT("контроль: 4137 код 6 — эталон молчит (нотиса нет), окно открыто"));
	bAwaitIncrease = true;
	Feed(4137, TArray<uint8>()); Pump();
	Check(bAwaitIncrease && QNum() == 0, TEXT("контроль: 4137 с пустым телом отброшен разбором — ожидание не снято, нотиса нет"));
	Feed(4137, Make4137(0, 99, 1, 9)); Pump();
	Check(!bAwaitIncrease, TEXT("пара: 4137 код 0 ожидание снял"));

	// ── 6. 4137 код 1: окно закрыто, лимит = @3, золото = @4, нотис 12451 ──
	QClear(TEXT("клан-самотест: код 1"));
	Feed(4137, Make4137(1, 55, 12345, 3));
	Check(bIncOpen, TEXT("контроль: код 1 в очереди до Tick — окно ещё открыто"));
	Pump();
	Check(!bIncOpen, TEXT("4137 код 1 закрыл окно"));
	Check(CC->GetBaseInfo().MaxMembers() == 55, FString::Printf(TEXT("4137 @3 → лимит в блоке = %d (ждём 55)"), (int32)CC->GetBaseInfo().MaxMembers()));
	Check(QTop() == Loc(12451), FString::Printf(TEXT("4137 код 1 → нотис «%s» (ждём loc 12451)"), *QTop()));
	if (Inv) { Check(Inv->GetGold() == 12345, FString::Printf(TEXT("4137 @4 → золото %d (ждём 12345)"), Inv->GetGold())); }
	QClear(TEXT("клан-самотест: после кода 1"));

	// ── 7. Пятая ступень (сделано 4): окно ОТКРЫВАЕТСЯ (клиент-эталон отказал бы), цена 2 000 000 ──
	Feed4132(70, 4);
	Check(OpenIncreaseWindow(), TEXT("сделано 4 из 5 — окно открылось (эталон-клиент отказал бы 12455)"));
	Check(TextOf(TxtIncNewVal) == TEXT("80"), FString::Printf(TEXT("14455 = «%s» (ждём 80)"), *TextOf(TxtIncNewVal)));
	Check(TextOf(TxtIncCostVal) == AsdaUi::FormatThousands(2000000), FString::Printf(TEXT("14457 = «%s» (ждём 2,000,000)"), *TextOf(TxtIncCostVal)));
	// Закрытие при 4 расширениях под правилом эталона: эталон вместо закрытия дал бы 12455 (ui_core.c:16197/:16227) —
	//   это и есть лишний нотис главы при «Распустить клан». Порт закрывает молча [ПОРТ].
	if (Rule)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Clan.IncreaseRule"), 1);
		QClear(TEXT("клан-самотест: закрытие при 4 расширениях"));
		CloseIncreaseWindow();
		Check(!bIncOpen && QNum() == 0, FString::Printf(TEXT("[ПОРТ] закрытие при 4 из 4 (правило эталона) — без нотиса 12455 (записей %d)"), QNum()));
		AsdaCVarSave::SetForTest(TEXT("asda.Clan.IncreaseRule"), 0);
	}
	CloseIncreaseWindow();

	// ── 8. Сделано 5 — отказ 12455, окно не открылось ──
	Feed4132(80, 5);
	QClear(TEXT("клан-самотест: сделано 5"));
	const bool bOpened5 = OpenIncreaseWindow();   // отдельно: порядок вычисления аргументов Check не задан
	Check(!bOpened5 && !bIncOpen && QTop() == Loc(12455),
		FString::Printf(TEXT("сделано 5 из 5 — отказ «%s» (ждём loc 12455), окно не открылось"), *QTop()));
	QClear(TEXT("клан-самотест: после 12455"));

	// ── 9. Ключ отката `asda.Clan.IncreaseRule 1` — числа клиента-эталона: +5, порог 4 ──
	if (Rule)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Clan.IncreaseRule"), 1);
		Feed4132(50, 3);
		Check(OpenIncreaseWindow(), TEXT("правило клиента: сделано 3 из 4 — окно открылось"));
		Check(TextOf(TxtIncNewVal) == TEXT("55"), FString::Printf(TEXT("правило клиента: 14455 = «%s» (ждём 55)"), *TextOf(TxtIncNewVal)));
		CloseIncreaseWindow();
		Feed4132(60, 4);
		Check(!OpenIncreaseWindow(), TEXT("правило клиента: сделано 4 из 4 — отказ 12455"));
		AsdaCVarSave::SetForTest(TEXT("asda.Clan.IncreaseRule"), 0);
		QClear(TEXT("клан-самотест: после правила клиента"));
	}

	// ── 10. 4132 код 2 — клан удалён: эталон пишет только свои поля (ui_core.c:17193-17215) ──
	{
		Feed4132(60, 4);   // [47] = 4 — эталон его НЕ трогает
		TArray<uint8> Body; Body.SetNumZeroed(1);
		Body[0] = 2;
		Feed(4132, Body);
		const FAsdaClanBaseInfo& I = CC->GetBaseInfo();
		Check(I.ClanIndex() == -1 && I.MaxMembers() == 0,
			FString::Printf(TEXT("4132 код 2 → clanId %d, лимит %d (ждём −1 и 0)"), I.ClanIndex(), (int32)I.MaxMembers()));
		Check(I.LevelRaw() == 4, FString::Printf(TEXT("4132 код 2: [47] = %d (ждём прежнее 4 — эталон байт не пишет)"), (int32)I.LevelRaw()));
		Check(I.B[107] == 0xFF && I.B[108] == 0xFF && I.B[189] == 0xFF && I.B[190] == 0xFF && I.B[106] == 0 && I.B[2] == 0,
			TEXT("4132 код 2: word −1 в [107]/[189], нули в [106]/[2] (дизасм 0xBCB5E2…0xBCB694)"));
	}

	bSelfTestSkipClanGate = false;
	CloseIncreaseWindow();
	bAwaitIncrease = false;
	PendingIncrease.Clear();
	QClear(TEXT("клан-самотест: конец"));
	Net->SelfSoulSilhouette = SoulWas;
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ Clan.IncreaseRule — к исходному значению И приоритету SetBy (прежде — RuleWas с SetByCode).
	AsdaCVarSave::RestoreAll(TEXT("asda.Clan.IncreaseSelfTest"));
	if (Inv) { Inv->SetGoldDisplay(GoldWas); }
	AsdaSelfTestKit::Summary(LogAsdaClan, TEXT("[клан-самотест]"), Oks, Fails, TEXT(""), false);   // ★ 11.10.2026 (В3 S1): единый итог (было «проверок N, провалов M»)
	return Fails;
}
#endif // !UE_BUILD_SHIPPING

void UAsdaClanSubsystem::OpenIncreaseWindowForSelfTest(int32 Cap, int32 Level)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// Тестовый блок — тем же штатным разбором 4132, что и в самотесте.
	TArray<uint8> Body; Body.SetNumZeroed(192);
	Body[0] = 1; Body[1] = 7;
	const char* Name = "SelfTest";
	for (int32 i = 0; Name[i]; ++i) { Body[3 + i] = (uint8)Name[i]; }
	Body[1 + 45] = (uint8)Cap;
	Body[1 + 47] = (uint8)Level;
	Net->DebugFeedPacket(4132, Body);
	bSelfTestSkipClanGate = true;
	OpenIncreaseWindow();
	bSelfTestSkipClanGate = false;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanIncreaseSelfTest(
	TEXT("asda.Clan.IncreaseSelfTest"),
	TEXT("asda.Clan.IncreaseSelfTest [keep|show] [delay=N] [quit] — ОФЛАЙН-самотест окна расширения 14450 (4131/4132/4137 через HandlePacket + Tick, 4136 перехватом)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Clan.IncreaseSelfTest"), KitArgs)) { return; }
		if (!W || !ClanSub(W)) { return; }
		// `show` — без проверок (они поднимают нотисы 12455 поверх окна): только открыть окно на
		//   тестовом блоке (лимит 30, сделано 2) для снимка. `keep` — проверки, затем то же окно.
		// ★ 27.09.2026: `delay=N` — запуск через N с (мир и виджеты готовы), `quit` — выйти после прогона (как 5055).
		FString Mode;        // keep / show / пусто
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		// ★ R5-fix3-c (13.11.2026): разбор `delay=` здесь НЕ сведён к `AsdaSelfTestKit::DelayArgOr` намеренно: последняя ветка —
		//   безусловная `else { Mode = Arg; }`, и после выноса `delay=N` попал бы в `Mode` (перевод не дословный).
		for (const FString& Arg : A)
		{
			if (Arg.StartsWith(TEXT("delay="))) { Delay = FCString::Atof(*Arg.Mid(6)); }
			else if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			else { Mode = Arg; }
		}
		TWeakObjectPtr<UWorld> WW = W;
		auto Run = [WW, Mode, bQuit]()   // сам прогон (сразу или по таймеру)
		{
			UWorld* Wd = WW.Get();
			UAsdaClanSubsystem* S = ClanSub(Wd);
			if (!S) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.Clan.IncreaseSelfTest"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			if (!Mode.Equals(TEXT("show"), ESearchCase::IgnoreCase)) { S->RunIncreaseSelfTest(); }
			if (Mode.Equals(TEXT("keep"), ESearchCase::IgnoreCase) || Mode.Equals(TEXT("show"), ESearchCase::IgnoreCase))
			{
				S->OpenIncreaseWindowForSelfTest(30, 2);
			}
			AsdaSelfTestKit::Quit(Wd, bQuit);
		};
		if (Delay <= 0.f) { Run(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Run), Delay, false);
	}));
#endif // ASDA_DEV_CONSOLE

// ═════════════════ ★ 27.09.2026: САМОТЕСТ ГЕЙТА «ДРУГ» 12435 И ПРИГЛАШЕНИЯ 4110/4111 (`friend_gate_12435.md`) ═════════════
//   КАК ПРОВЕРЯЕТ — только ШТАТНЫЕ пути: пункт меню игрока 2003 (`NotifyRightPress/Release` → `ActionClanInvite`),
//   вкладка клана 14032 (`ActionLeaveClan` → ответ «Да» через `AnswerMsgBox` — тот же вызов, что у реле кнопок),
//   4110 через `DebugFeedPacket` → `HandlePacket` → очередь → `Tick`, клик по значку — рассылка `OnNoticeIconClicked`
//   и снятие значка (ровно две строки `UAsdaHotbarSubsystem::TickNoticeClick`), ответы окна — `AnswerMsgBox`,
//   отправки 4108/4111 — боевые отправители под узким перехватом (в сеть ничего не уходит).
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): флаг 0 ↔ 1 на каждом месте; снятие верхнего окна ↔ без
//   него (12435 глушится дублем пары (0, −1)); 4110 длиной 23 ↔ 22; значок до `Tick` ↔ после; окно открыто → значка нет
//   ↔ закрыто → есть; таймер 29 с ↔ 31 с; «Да» ↔ «Нет»; 4116 при «Друг» — гейта «Друг» НЕТ (у эталона его там нет).
//   ★ Дополнено 27.09.2026 (`friend_gate_fix.md`): п. 5 — SoulState 2 на 4108/4111; п. 6 — пять перенесённых мест
//   штатными входами (коды NPC 30015/30016/30027 через `RunCommandForCheck`, кнопки 14473/14461 своим `OnClicked`,
//   4106/4136 перехватом) парами 1 ↔ 0; п. 7 — 4109 код 1 (@3 = я ↔ сосед ↔ вне мира, вкладка клана не видна ↔
//   видна, прочие коды); п. 8 — клан-чат 4120 через путь Enter (SoulState 0 ↔ 1, @0 clanId, лимит поля).
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
int32 UAsdaClanSubsystem::RunFriendGateSelfTest()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	UAsdaPlayerMenuSubsystem* PM = GI ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
	UAsdaCommunityClanSubsystem* CC = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr;
	if (!Net || !WM || !Hb || !PM || !CC)
	{
		UE_LOG(LogAsdaClan, Error, TEXT("[друг-самотест] нет сетевой, оконной, панели, меню игрока или клановой вкладки"));
		return 1;
	}
	int32 Fails = 0;   // число проваленных проверок
	int32 Oks = 0;     // число пройденных проверок
	auto Check = [&Fails, &Oks](bool bOk, const FString& What)
	{
		if (bOk) { ++Oks; } else { ++Fails; }
		UE_LOG(LogAsdaClan, Log, TEXT("[друг-самотест] %s — %s"), bOk ? TEXT("ОК") : TEXT("ПРОВАЛ"), *What);
	};
	auto QTop = [WM]() { return WM->GetMsgBoxQueueTopText(); };
	auto QNum = [WM]() { return WM->GetMsgBoxQueueNum(); };
	auto QClear = [WM](const TCHAR* Why) { WM->ClearMsgBoxQueue(Why); };
	auto Foreign = [GI]() { AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, TEXT("друг-самотест: чужая запись")); };
	const FString L12435 = Loc(ClanLoc::kSoulmateDeny);
	// Один перехваченный пакет: payload и вид конверта.
	struct FCap { TArray<uint8> Payload; TCHAR Env = 0; };
	TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();
	auto Arm = [Net, Caps](uint16 Op)
	{
		Caps->Reset();
		return Net->ArmSendCapture([Caps, Op](uint16 O, const uint8* P, int32 Len, bool /*bPad*/, TCHAR Env)
		{
			if (O != Op) { return; }
			FCap C; C.Env = Env;
			if (P && Len > 0) { C.Payload.Append(P, Len); }
			Caps->Add(MoveTemp(C));
		}, Op);
	};

	const uint8 SoulWas = Net->SelfSoulSilhouette;
	const bool bConnWas = Net->bInGameConnected;
	const uint8 RankWas = Net->SelfClanRank;
	const int32 ClanWas = SelfClanId();
	const FString ClanNameWas = Net->SelfClanName;
	QClear(TEXT("друг-самотест: старт"));

	// ── 1. Общая точка ──
	Net->SelfSoulSilhouette = 0;
	Check(!RefuseIfSoulmateLocked(true, TEXT("самотест")) && QNum() == 0, TEXT("контроль: SoulState 0 → пропуск, нотиса нет"));
	Net->SelfSoulSilhouette = 2;
	const bool bRefused2 = RefuseIfSoulmateLocked(false, TEXT("самотест"));   // отдельно: порядок вычисления аргументов Check не задан
	Check(bRefused2 && QNum() == 1 && QTop() == L12435 && !L12435.IsEmpty() && WM->IsMsgBoxQueued(0, -1),
		FString::Printf(TEXT("SoulState 2 (одержимость) → отказ, ONEBUT (0, −1) «%s» (ждём loc 12435)"), *QTop()));
	QClear(TEXT("друг-самотест: 1б"));
	Net->SelfSoulSilhouette = 1;
	Foreign();
	RefuseIfSoulmateLocked(false, TEXT("самотест: без снятия"));
	Check(QNum() == 1 && QTop() != L12435, TEXT("контроль: без снятия верхнего окна 12435 глушится дублем пары (0, −1)"));
	QClear(TEXT("друг-самотест: 1в"));
	Foreign();
	RefuseIfSoulmateLocked(true, TEXT("самотест: со снятием"));
	Check(QNum() == 1 && QTop() == L12435, FString::Printf(TEXT("SoulState 1 («душа») со снятием: чужая запись снята, видно «%s»"), *QTop()));

	// ── 2. Пункт 2003 «Вступить в клан» → 4108 (social.c:3430-3436) ──
	{
		const int32 kTarget = 77;   // userNum цели
		auto OpenMenu = [PM, kTarget]() { PM->NotifyRightPress(kTarget); return PM->NotifyRightRelease(kTarget, false); };
		Net->bInGameConnected = true;   // гейт обязан держать САМ, а не проверка соединения порта
		const uint32 Gen = Arm(4108);
		QClear(TEXT("друг-самотест: 2"));
		Net->SelfSoulSilhouette = 1;
		const bool bMenu1 = OpenMenu();
		Foreign();
		PM->ActionClanInvite();
		Check(bMenu1 && Caps->Num() == 0 && QNum() == 1 && QTop() == L12435,
			FString::Printf(TEXT("2003 при «Друг»: меню=%d, 4108 не ушёл (поймано %d), чужая запись снята, видно «%s»"),
				bMenu1 ? 1 : 0, Caps->Num(), *QTop()));
		QClear(TEXT("друг-самотест: 2 контроль"));
		Net->SelfSoulSilhouette = 0;
		const bool bMenu2 = OpenMenu();
		PM->ActionClanInvite();
		const bool bOne = Caps->Num() == 1;
		Check(bMenu2 && bOne && QNum() == 0, FString::Printf(TEXT("контроль: 2003 без «Друг» → ушёл один 4108 (поймано %d), нотисов нет"), Caps->Num()));
		if (bOne)
		{
			const TArray<uint8>& P = (*Caps)[0].Payload;
			Check(P.Num() == 2 && AsdaBytes::RdI16(P.GetData()) == kTarget && (*Caps)[0].Env == TEXT('M'),
				FString::Printf(TEXT("4108: тело %d б (ждём 2), userNum %d (ждём %d), конверт «%c» (ждём M)"),
					P.Num(), P.Num() >= 2 ? AsdaBytes::RdI16(P.GetData()) : -1, kTarget, (*Caps)[0].Env ? (*Caps)[0].Env : TEXT('?')));
		}
		Net->DisarmSendCapture(Gen);
		Net->bInGameConnected = bConnWas;
	}

	// ── 3. Выход из клана 14032 → 4116: у эталона гейта «Друг» НЕТ, у главы — снятие верхнего окна + 12433 ──
	{
		Net->OnClanInfo.Broadcast(-1, 7, TEXT("SelfTest"), FString());   // свой клан — штатное зеркало 4105/4135
		Net->SelfClanRank = 1;
		Net->SelfSoulSilhouette = 0;
		QClear(TEXT("друг-самотест: 3"));
		Foreign();
		CC->ActionLeaveClan();
		const bool bAsked = QNum() == 2 && WM->IsMsgBoxQueued(1, 45);
		WM->AnswerMsgBox(/*bTwoBut*/true, /*bYes*/true);
		Check(bAsked && QNum() == 1 && QTop() == Loc(12433),
			FString::Printf(TEXT("глава: вопрос (1, 45)=%d → «Да» → чужая запись снята (network.c:12240), видно «%s» (ждём loc 12433), записей %d"),
				bAsked ? 1 : 0, *QTop(), QNum()));
		QClear(TEXT("друг-самотест: 3 контроль"));
		Net->SelfClanRank = 0;
		Net->SelfSoulSilhouette = 1;
		CC->ActionLeaveClan();
		WM->AnswerMsgBox(true, true);
		Check(QNum() == 0, FString::Printf(TEXT("контроль: не глава при «Друг» → 12435 НЕТ (`netSendNameConfirmPacket` смотрит только звание), записей %d"), QNum()));
		Net->SelfClanRank = RankWas;
		Net->SelfSoulSilhouette = 0;
		Net->OnClanInfo.Broadcast(-1, ClanWas, ClanNameWas, FString());
	}

	// ── 4. Приглашение 4110 → значок 6 → окно (1, 44) → 4111 ──
	{
		auto Make4110 = [](int32 ClanId, const char* Name, int32 Req, int32 Resp, int32 Len)
		{
			TArray<uint8> B; B.SetNumZeroed(23);
			AsdaBytes::WrI16(B.GetData() + 0, ClanId);
			for (int32 i = 0; Name[i] && i < 16; ++i) { B[2 + i] = (uint8)Name[i]; }
			AsdaBytes::WrI16(B.GetData() + 19, Req);
			AsdaBytes::WrI16(B.GetData() + 21, Resp);
			B.SetNum(Len);
			return B;
		};
		auto Feed = [Net](const TArray<uint8>& B) { Net->DebugFeedPacket(4110, B); };
		auto Pump = [this]() { Tick(0.f); };
		auto Click = [Hb]() { Hb->OnNoticeIconClicked.Broadcast(kNoticeTypeClanInvite); Hb->RemoveNoticeIcon(kNoticeTypeClanInvite); };
		auto GaugeShown = [WM]()
		{
			UUserWidget* Box = WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX")));
			if (!Box || !Box->WidgetTree) { return false; }
			TArray<UWidget*> All; Box->WidgetTree->GetAllWidgets(All);
			for (UWidget* Wd : All)
			{
				if (Wd && Wd->GetName().StartsWith(TEXT("TWOBUT_GAUGE")) && Wd->GetVisibility() != ESlateVisibility::Collapsed) { return true; }
			}
			return false;
		};
		QClear(TEXT("друг-самотест: 4"));
		// Панель поднимается лениво (в мире — первым тиком онлайн); офлайн её поднимает штатный дамп `asda.Quick.NoticeDump`.
		Hb->DumpNoticeIcons();
		Hb->RemoveNoticeIcon(kNoticeTypeClanInvite);
		PendingInvite.Clear();
		InviteClanId = -1; InviteClanName.Reset(); InviteReqUser = -1; InviteRespUser = -1;
		bInviteBoxActive = false; InviteBoxStart = -1.0;
		Net->SelfSoulSilhouette = 0;

		Feed(Make4110(5, "Short", 10, 22, 22)); Pump();
		Check(!Hb->HasNoticeIcon(kNoticeTypeClanInvite) && InviteClanId == -1, TEXT("контроль: 4110 длиной 22 отброшен разбором — значка нет, поля пусты"));
		Feed(Make4110(7, "Alpha", 11, 22, 23));
		const bool bQuiet = !Hb->HasNoticeIcon(kNoticeTypeClanInvite);
		Pump();
		Check(bQuiet && Hb->HasNoticeIcon(kNoticeTypeClanInvite) && InviteClanId == 7 && InviteReqUser == 11 && InviteRespUser == 22,
			FString::Printf(TEXT("4110 (23 б): до Tick значка нет=%d; после — значок 6, clanId %d, пригласил %d, мне %d (ждём 7/11/22)"),
				bQuiet ? 1 : 0, InviteClanId, InviteReqUser, InviteRespUser));
		Feed(Make4110(8, "Beta", 12, 22, 23)); Pump();
		Check(Hb->HasNoticeIcon(kNoticeTypeClanInvite) && InviteClanId == 8, TEXT("второе 4110 при висящем значке: поля перезаписаны (clanId 8), значок один"));
		Click();
		Check(bInviteBoxActive && WM->IsMsgBoxQueued(kInviteBoxKind, kInviteBoxId) && QTop() == Loc(ClanLoc::kInviteAsk) && !Hb->HasNoticeIcon(kNoticeTypeClanInvite),
			FString::Printf(TEXT("клик по значку 6: окно (1, 44) «%s» (ждём loc 12430), признак окна, значок снят"), *QTop()));
		Check(GaugeShown(), TEXT("полоса TWOBUT_GAUGE показана (таймер эталона, `sub_D78FB0(…, 1)`)"));
		Feed(Make4110(9, "Gamma", 13, 22, 23)); Pump();
		Check(!Hb->HasNoticeIcon(kNoticeTypeClanInvite) && InviteClanId == 9,
			TEXT("4110 при открытом окне: значка нет (гейт `dword_2BE50FC`), поля перезаписаны — clanId 9 (1:1)"));

		const uint32 Gen = Arm(4111);
		WM->AnswerMsgBox(/*bTwoBut*/true, /*bYes*/true);
		Check(Caps->Num() == 1 && !bInviteBoxActive && InviteClanId == -1 && !WM->IsMsgBoxQueued(kInviteBoxKind, kInviteBoxId),
			FString::Printf(TEXT("«Да»: ушёл один 4111 (поймано %d), признак окна снят [ПОРТ, D8], поля сброшены"), Caps->Num()));
		if (Caps->Num() >= 1)
		{
			const TArray<uint8>& P = (*Caps)[0].Payload;
			const bool bLen = P.Num() == 24;
			Check(bLen && (*Caps)[0].Env == TEXT('S'), FString::Printf(TEXT("4111: тело %d б (ждём 24; 8+16+24 = 48), конверт «%c» (ждём S)"),
				P.Num(), (*Caps)[0].Env ? (*Caps)[0].Env : TEXT('?')));
			if (bLen)
			{
				const FString Nm = FAsdaLoginData::DecodeCp1251(P.GetData() + 3, 17);
				Check(P[0] == 1 && AsdaBytes::RdI16(P.GetData() + 1) == 9 && Nm == TEXT("Gamma") && P[3 + 5] == 0 && P[19] == 0
					&& AsdaBytes::RdI16(P.GetData() + 20) == 13 && AsdaBytes::RdI16(P.GetData() + 22) == 22,
					FString::Printf(TEXT("4111: @0=%d @1=%d @3=«%s» @20=%d @22=%d (ждём 1/9/Gamma/13/22)"), (int32)P[0],
						AsdaBytes::RdI16(P.GetData() + 1), *Nm, AsdaBytes::RdI16(P.GetData() + 20), AsdaBytes::RdI16(P.GetData() + 22)));
			}
		}
		// «Нет» — пара к «Да».
		Feed(Make4110(7, "Alpha", 11, 22, 23)); Pump();
		Check(Hb->HasNoticeIcon(kNoticeTypeClanInvite), TEXT("пара к п. 4110 при открытом окне: окно закрыто → значок снова появляется [ПОРТ, D8]"));
		Click();
		WM->AnswerMsgBox(true, /*bYes*/false);
		Check(Caps->Num() == 2 && (*Caps)[1].Payload.Num() == 24 && (*Caps)[1].Payload[0] == 0 && !bInviteBoxActive,
			FString::Printf(TEXT("«Нет»: второй 4111 с @0 = 0 (поймано %d)"), Caps->Num()));
		// Таймер: 29 с — рано, 31 с — 4111 «нет» и снятие своей записи.
		Feed(Make4110(7, "Alpha", 11, 22, 23)); Pump(); Click();
		InviteBoxStart = FPlatformTime::Seconds() - 29.0; Pump();
		Check(Caps->Num() == 2 && bInviteBoxActive && WM->IsMsgBoxQueued(kInviteBoxKind, kInviteBoxId), TEXT("контроль: таймер 29 с — исхода нет"));
		InviteBoxStart = FPlatformTime::Seconds() - 31.0; Pump();
		Check(Caps->Num() == 3 && (*Caps)[2].Payload.Num() == 24 && (*Caps)[2].Payload[0] == 0 && !bInviteBoxActive
			&& !WM->IsMsgBoxQueued(kInviteBoxKind, kInviteBoxId),
			FString::Printf(TEXT("таймер 31 с: 4111 «нет» (поймано %d), своя запись (1, 44) снята, признак снят [ПОРТ, D8]"), Caps->Num()));
		// «Друг» на ответе: 4111 не уходит, поля НЕ сброшены (эталон), видно 12435.
		QClear(TEXT("друг-самотест: 4 «Друг»"));
		Feed(Make4110(7, "Alpha", 11, 22, 23)); Pump(); Click();
		Net->SelfSoulSilhouette = 1;
		WM->AnswerMsgBox(true, true);
		Check(Caps->Num() == 3 && QTop() == L12435 && QNum() == 1 && InviteClanId == 7 && !bInviteBoxActive,
			FString::Printf(TEXT("«Да» при «Друг»: 4111 не ушёл (поймано %d), видно «%s», поля НЕ сброшены (clanId %d, как эталон)"),
				Caps->Num(), *QTop(), InviteClanId));
		Net->SelfSoulSilhouette = 0;
		Net->DisarmSendCapture(Gen);
		Hb->RemoveNoticeIcon(kNoticeTypeClanInvite);
		InviteClanId = -1; InviteClanName.Reset(); InviteReqUser = -1; InviteRespUser = -1;
		bInviteBoxActive = false; InviteBoxStart = -1.0;
	}

	// ══ ★ 27.09.2026 (`friend_gate_fix.md`, МЕЛК-2 ревизии): SoulState 2 на 4108/4111 и пять перенесённых мест ══
	UAsdaNpcDialogSubsystem* Npc = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();   // штатный вход кодов NPC 30015/30016/30027
	UAsdaWorldSubsystem* WSt = GI->GetSubsystem<UAsdaWorldSubsystem>();            // зеркало `char+0x968`, соседи
	UAsdaCommunitySubsystem* Cm = GI->GetSubsystem<UAsdaCommunitySubsystem>();      // окно 6400 (вкладка «Клан»)
	UAsdaChatWindowSubsystem* Chat = GI->GetSubsystem<UAsdaChatWindowSubsystem>();  // поле ввода чата (режим 6)
	Check(Npc && WSt && Cm && Chat, TEXT("подсистемы NPC-диалога, мира, сообщества и чата есть"));

	// ── 5. SoulState 2 (одержимость) — 4108 и 4111 так же, как при 1 ──
	if (Npc && WSt && Cm && Chat)
	{
		Net->bInGameConnected = true;
		uint32 Gen = Arm(4108);
		QClear(TEXT("друг-самотест: 5"));
		Net->SelfSoulSilhouette = 2;
		PM->NotifyRightPress(77);
		const bool bMenu = PM->NotifyRightRelease(77, false);
		PM->ActionClanInvite();
		Check(bMenu && Caps->Num() == 0 && QTop() == L12435,
			FString::Printf(TEXT("2003 при SoulState 2: 4108 не ушёл (поймано %d), видно «%s»"), Caps->Num(), *QTop()));
		Net->DisarmSendCapture(Gen);
		Net->bInGameConnected = bConnWas;

		Gen = Arm(4111);
		QClear(TEXT("друг-самотест: 5б"));
		Net->SelfSoulSilhouette = 0;
		{
			TArray<uint8> B; B.SetNumZeroed(23);
			AsdaBytes::WrI16(B.GetData() + 0, 7);
			AsdaBytes::WrI16(B.GetData() + 19, 11);
			AsdaBytes::WrI16(B.GetData() + 21, 22);
			Net->DebugFeedPacket(4110, B);
		}
		Tick(0.f);
		Hb->OnNoticeIconClicked.Broadcast(kNoticeTypeClanInvite); Hb->RemoveNoticeIcon(kNoticeTypeClanInvite);
		Net->SelfSoulSilhouette = 2;
		WM->AnswerMsgBox(true, true);
		Check(Caps->Num() == 0 && QTop() == L12435,
			FString::Printf(TEXT("4111 «Да» при SoulState 2: не ушёл (поймано %d), видно «%s»"), Caps->Num(), *QTop()));
		Net->DisarmSendCapture(Gen);
		Net->SelfSoulSilhouette = 0;
		InviteClanId = -1; InviteClanName.Reset(); InviteReqUser = -1; InviteRespUser = -1;
		bInviteBoxActive = false; InviteBoxStart = -1.0;
		QClear(TEXT("друг-самотест: 5 конец"));
	}

	// ── 6. Пять перенесённых мест — ШТАТНЫЕ входы: код NPC (`RunCommandForCheck` = путь нажатия кнопки Эллы),
	//      кнопки окон — собственным `OnClicked` (реле `UAsdaButtonRelay`), отправки 4106/4136 — узкий перехват ──
	if (Npc && WSt && Cm && Chat)
	{
		// 30015 — окно создания (ui_core.c:15737, без снятия верхнего окна). Нужен «клана нет».
		Net->OnClanInfo.Broadcast(-1, -1, FString(), FString());
		QClear(TEXT("друг-самотест: 6/30015"));
		Net->SelfSoulSilhouette = 1;
		Foreign();
		Npc->RunCommandForCheck(30015, 0);
		Check(!IsCreateOpen() && QNum() == 1 && QTop() != L12435,
			FString::Printf(TEXT("30015 при «Друг»: окно не открыто, 12435 без снятия чужой записи глушится дублем (0, −1) — записей %d"), QNum()));
		QClear(TEXT("друг-самотест: 6/30015б"));
		Npc->RunCommandForCheck(30015, 0);
		Check(!IsCreateOpen() && QTop() == L12435, FString::Printf(TEXT("30015 при «Друг»: окно не открыто, видно «%s»"), *QTop()));
		QClear(TEXT("друг-самотест: 6/30015 контроль"));
		Net->SelfSoulSilhouette = 0;
		Npc->RunCommandForCheck(30015, 0);
		Check(IsCreateOpen() && QNum() == 0, TEXT("контроль: 30015 без «Друг» → окно 14400 открыто, нотисов нет"));
		CloseCreateWindow();

		// 30016 — окно роспуска (ui_core.c:16149) и 14473 → 4106 (network.c:11984, со снятием верхнего окна).
		Net->OnClanInfo.Broadcast(-1, 7, TEXT("SelfTest"), FString());
		Net->SelfClanRank = 1;
		QClear(TEXT("друг-самотест: 6/30016"));
		Net->SelfSoulSilhouette = 1;
		Npc->RunCommandForCheck(30016, 0);
		Check(!IsDisbandOpen() && QTop() == L12435, FString::Printf(TEXT("30016 при «Друг»: окно не открыто, видно «%s»"), *QTop()));
		QClear(TEXT("друг-самотест: 6/30016 контроль"));
		Net->SelfSoulSilhouette = 0;
		Npc->RunCommandForCheck(30016, 0);
		Check(IsDisbandOpen() && QNum() == 0 && BtnDelOk != nullptr, TEXT("контроль: 30016 без «Друг» (глава клана 7) → окно 14470 открыто"));
		if (BtnDelOk)
		{
			uint32 Gen = Arm(4106);
			Net->SelfSoulSilhouette = 1;
			Foreign();
			BtnDelOk->OnClicked.Broadcast();
			Check(Caps->Num() == 0 && QNum() == 1 && QTop() == L12435,
				FString::Printf(TEXT("14473 при «Друг»: 4106 не ушёл (поймано %d), чужая запись снята, видно «%s»"), Caps->Num(), *QTop()));
			QClear(TEXT("друг-самотест: 6/14473 контроль"));
			Net->SelfSoulSilhouette = 0;
			BtnDelOk->OnClicked.Broadcast();
			Check(Caps->Num() == 1 && (*Caps)[0].Payload.Num() == 2 && AsdaBytes::RdI16((*Caps)[0].Payload.GetData()) == 7,
				FString::Printf(TEXT("контроль: 14473 без «Друг» → ушёл один 4106 с clanId 7 (поймано %d)"), Caps->Num()));
			Net->DisarmSendCapture(Gen);
		}
		CloseDisbandWindow();

		// 30027 — окно расширения (ui_core.c:16193) и 14461 → 4136 (network.c:11775, со снятием верхнего окна).
		QClear(TEXT("друг-самотест: 6/30027"));
		Net->SelfSoulSilhouette = 1;
		Npc->RunCommandForCheck(30027, 0);
		Check(!IsIncreaseOpen() && QTop() == L12435, FString::Printf(TEXT("30027 при «Друг»: окно не открыто, видно «%s»"), *QTop()));
		QClear(TEXT("друг-самотест: 6/30027 контроль"));
		Net->SelfSoulSilhouette = 0;
		Npc->RunCommandForCheck(30027, 0);
		Check(IsIncreaseOpen() && QNum() == 0 && BtnIncOk != nullptr, TEXT("контроль: 30027 без «Друг» (клан 7) → окно 14450 открыто"));
		if (BtnIncOk)
		{
			uint32 Gen = Arm(4136);
			Net->SelfSoulSilhouette = 1;
			Foreign();
			BtnIncOk->OnClicked.Broadcast();
			Check(Caps->Num() == 0 && QNum() == 1 && QTop() == L12435,
				FString::Printf(TEXT("14461 при «Друг»: 4136 не ушёл (поймано %d), чужая запись снята, видно «%s»"), Caps->Num(), *QTop()));
			QClear(TEXT("друг-самотест: 6/14461 контроль"));
			Net->SelfSoulSilhouette = 0;
			BtnIncOk->OnClicked.Broadcast();
			Check(Caps->Num() == 1, FString::Printf(TEXT("контроль: 14461 без «Друг» → ушёл один 4136 (поймано %d)"), Caps->Num()));
			Net->DisarmSendCapture(Gen);
		}
		CloseIncreaseWindow();
		QClear(TEXT("друг-самотест: 6 конец"));
		Net->SelfClanRank = RankWas;
	}

	// ── 7. 4109 код 1 (СРЕДН-1): имя персонажа @3 в 20051, 4138, при видимой вкладке клана — 4122 + 4124 ──
	if (Npc && WSt && Cm && Chat)
	{
		// ★ R5-fix-c (13.11.2026): свой номер на время блока — общим помощником `FAsdaNetSelfScope` (было ручное сохранение
		//   `SelfUserNum`/`bSelfUserNumKnown` двумя локальными); возврат — явным `Restore()` на прежнем месте.
		FAsdaNetSelfScope SelfScope(Net, TEXT("asda.Clan.FriendGateSelfTest"));
		constexpr int32 kMe = 22, kPeer = 88, kAbsent = 99;   // мой userNum, сосед в мире, userNum вне мира
		Net->SelfUserNum = (uint16)kMe; Net->bSelfUserNumKnown = true;
		WSt->DebugSpawnFakePeer(kPeer, TEXT("Бета"), FVector2D(0.f, 0.f));
		Net->OnClanInfo.Broadcast(-1, -1, FString(), FString());   // приглашённый — пока без клана
		// Перехват всех трёх опкодов хвоста разом (полный перехват, фильтр в лямбде; офлайн).
		struct FOp { uint16 Op; TArray<uint8> P; };
		TSharedRef<TArray<FOp>> Ops = MakeShared<TArray<FOp>>();
		const uint32 Gen = Net->ArmSendCapture([Ops](uint16 O, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (O != 4138 && O != 4122 && O != 4124) { return; }
			FOp X; X.Op = O; if (P && Len > 0) { X.P.Append(P, Len); }
			Ops->Add(MoveTemp(X));
		}, 0);
		auto Count = [Ops](uint16 Op) { int32 N = 0; for (const FOp& X : *Ops) { if (X.Op == Op) { ++N; } } return N; };
		auto Make4109 = [](int32 Inviter, int32 Invitee, int32 ClanId, const char* ClanName)
		{
			TArray<uint8> B; B.SetNumZeroed(27);
			B[0] = 1;
			AsdaBytes::WrI16(B.GetData() + 1, Inviter);
			AsdaBytes::WrI16(B.GetData() + 3, Invitee);
			AsdaBytes::WrI16(B.GetData() + 5, ClanId);
			for (int32 i = 0; ClanName[i] && i < 19; ++i) { B[7 + i] = (uint8)ClanName[i]; }
			return B;
		};
		FString PeerName; int32 Cls = 0, Lvl = 0, PeerClan = -1;
		const bool bPeer = WSt->GetNetPlayerInfo(kPeer, PeerName, Cls, Lvl, PeerClan);
		Check(bPeer && PeerName == TEXT("Бета") && Cm->GetActiveTab() != 2,
			FString::Printf(TEXT("подготовка: сосед %d «%s» в мире=%d, вкладка сообщества %d (≠ 2)"), kPeer, *PeerName, bPeer ? 1 : 0, Cm->GetActiveTab()));

		// а) приглашённый: @3 = я → клан записан ДО нотиса, 20051 со СВОИМ именем, один 4138 с моим userNum, 4122 нет.
		int32 Seq = PM->GetNoticeSeq();
		Net->DebugFeedPacket(4109, Make4109(11, kMe, 7, "Alpha"));
		Check(WSt->GetSelfClanId() == 7 && Net->SelfClanName == TEXT("Alpha"),
			FString::Printf(TEXT("4109 @3 = я: свой клан %d «%s» (ждём 7 «Alpha», network.c:12042-12043)"), WSt->GetSelfClanId(), *Net->SelfClanName));
		Check(PM->GetNoticeSeq() == Seq + 1 && PM->GetLastNoticeLoc() == 20051 && PM->GetLastNoticeSlot() == WSt->GetSelfCharName(),
			FString::Printf(TEXT("4109 @3 = я: нотис %d со слотом «%s» (ждём 20051 со своим именем «%s»)"),
				PM->GetLastNoticeLoc(), *PM->GetLastNoticeSlot(), *WSt->GetSelfCharName()));
		Check(Count(4138) == 1 && Count(4122) == 0 && Count(4124) == 0,
			FString::Printf(TEXT("4109 @3 = я, вкладка не видна: 4138=%d 4122=%d 4124=%d (ждём 1/0/0)"), Count(4138), Count(4122), Count(4124)));
		if (Count(4138) == 1)
		{
			const TArray<uint8>& P = (*Ops)[0].P;
			// ★ 27.09.2026 (аудит charId): @0 — `->userIdx` = charId (`char+1052`), не userNum.
			Check(P.Num() == 4 && AsdaBytes::RdI32(P.GetData()) == Net->SelectedCharId,
				FString::Printf(TEXT("4138: тело %d б (ждём 4; 8+16+4 = 28), userIdx(charId) %d (ждём %d)"), P.Num(), P.Num() >= 4 ? AsdaBytes::RdI32(P.GetData()) : -1, Net->SelectedCharId));
		}
		// б) пара: @3 = сосед → слот = ИМЯ СОСЕДА (не цель последнего меню), ещё один 4138.
		Ops->Reset(); Seq = PM->GetNoticeSeq();
		Net->DebugFeedPacket(4109, Make4109(kMe, kPeer, 7, "Alpha"));
		Check(PM->GetNoticeSeq() == Seq + 1 && PM->GetLastNoticeLoc() == 20051 && PM->GetLastNoticeSlot() == TEXT("Бета") && Count(4138) == 1,
			FString::Printf(TEXT("4109 @3 = сосед: нотис %d «%s» (ждём 20051 «Бета»), 4138=%d (ждём 1)"),
				PM->GetLastNoticeLoc(), *PM->GetLastNoticeSlot(), Count(4138)));
		// в) контроль: @3 вне мира → у эталона блок пропущен (нотис — мусор буфера): ни нотиса, ни 4138.
		Ops->Reset(); Seq = PM->GetNoticeSeq();
		Net->DebugFeedPacket(4109, Make4109(kMe, kAbsent, 7, "Alpha"));
		Check(PM->GetNoticeSeq() == Seq && Count(4138) == 0,
			FString::Printf(TEXT("контроль: 4109 @3 вне мира → нотисов 0 (напечатано %d), 4138=%d (ждём 0) [ПОРТ]"), PM->GetNoticeSeq() - Seq, Count(4138)));
		// г) вкладка «Клан» видима (последней выбрана вкладка 2, окно закрыто — флаг панели живёт) → 4138 + 4122 + 4124.
		Cm->OpenOnTab(2);
		const bool bTabOpened = Cm->GetActiveTab() == 2;
		Cm->CloseWindow();
		Ops->Reset();
		Net->DebugFeedPacket(4109, Make4109(11, kMe, 7, "Alpha"));
		Check(bTabOpened && Count(4138) == 1 && Count(4122) == 1 && Count(4124) == 1,
			FString::Printf(TEXT("4109 при видимой вкладке клана (вкладка 2=%d): 4138=%d 4122=%d 4124=%d (ждём 1/1/1, network.c:12061-12066)"),
				bTabOpened ? 1 : 0, Count(4138), Count(4122), Count(4124)));
		// д) прочие коды 4109 — свой нотис и никакого хвоста (контроль к коду 1).
		static const int32 kCodes[8][2] = { {0, 12413}, {2, 12415}, {3, 12416}, {4, 12417}, {5, 12419}, {6, 12420}, {7, 12421}, {8, 12168} };
		int32 BadCodes = 0;   // кодов с неверным нотисом
		Ops->Reset();
		for (const auto& CL : kCodes)
		{
			TArray<uint8> B; B.SetNumZeroed(27); B[0] = (uint8)CL[0];
			Net->DebugFeedPacket(4109, B);
			// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] D8: код 4 без своего клана → 991210 (ключ asda.Clan.NoClanNotice), иначе 12417.
			const int32 Want = CL[0] == 4 ? UAsdaClanSubsystem::NoClanLocOr(GI, CL[1]) : CL[1];
			if (PM->GetLastNoticeLoc() != Want) { ++BadCodes; }
		}
		QClear(TEXT("самотест: отказы 4109 окнами-сообщениями (asda.Clan.InviteResultMsgBox)"));
		Check(BadCodes == 0 && Ops->Num() == 0, FString::Printf(TEXT("коды 0,2..8 → 12413/12415/12416/12417/12419/12420/12421/12168: неверных %d, хвостовых пакетов %d"), BadCodes, Ops->Num()));
		Net->DisarmSendCapture(Gen);
		SelfScope.Restore();
		Net->SelfClanName = ClanNameWas;   // 4109 @3 = я записал «Alpha»
	}

	// ── 8. Клан-чат 4120 (СРЕДН-2): штатный путь Enter (`asda.ChatWin.Say` → `SubmitInput` → `SendCurrentChannel`) ──
	if (Npc && WSt && Cm && Chat)
	{
		Net->OnClanInfo.Broadcast(-1, 7, TEXT("SelfTest"), FString());
		const int32 StateWas = Chat->GetInputState();
		const int32 LimitWas = UAsdaChatWindowSubsystem::GetInputMaxChars();
		const FString Prefix = Loc(6937);   // «(Друг)» — loc обработчика `chatSendMessage`
		const uint32 Gen = Arm(4120);
		auto Say = [Chat](const FString& T) { Chat->SetInputState(6); Chat->DebugSubmit(T); };
		auto TextAt28 = [Caps](int32 I) { const TArray<uint8>& P = (*Caps)[I].Payload; return P.Num() > 28 ? FAsdaLoginData::DecodeCp1251(P.GetData() + 28, P.Num() - 28) : FString(); };
		Net->SelfSoulSilhouette = 0;
		Say(TEXT("клан раз"));
		Check(Caps->Num() == 1 && (*Caps)[0].Env == TEXT('R') && AsdaBytes::RdI16((*Caps)[0].Payload.GetData()) == 7 && TextAt28(0) == TEXT("клан раз"),
			FString::Printf(TEXT("контроль: SoulState 0 → 4120 @0 clanId %d (ждём 7), текст «%s» без префикса"),
				Caps->Num() ? AsdaBytes::RdI16((*Caps)[0].Payload.GetData()) : -99, Caps->Num() ? *TextAt28(0) : TEXT("—")));
		Net->SelfSoulSilhouette = 1;
		Say(TEXT("клан два"));
		Check(!Prefix.IsEmpty() && Caps->Num() == 2 && TextAt28(1) == Prefix + TEXT("клан два")
			&& (*Caps)[1].Payload.Num() == 28 + Prefix.Len() + 8,
			FString::Printf(TEXT("SoulState 1 → текст «%s» (ждём «%s» + текст, склейка без пробела), тело %d б"),
				Caps->Num() > 1 ? *TextAt28(1) : TEXT("—"), *Prefix, Caps->Num() > 1 ? (*Caps)[1].Payload.Num() : -1));
		// Лимит поля: эталонный 68 и умолчание порта не урезаются (префикс сверху, как в оригинале); у потолка 199 —
		//   `160 − префикс` при «Друг» ↔ 199 без него.
		Chat->SetInputState(6);
		UAsdaChatWindowSubsystem::SetInputMaxChars(68);
		const int32 L68 = Chat->GetActiveInputMaxChars();
		UAsdaChatWindowSubsystem::SetInputMaxChars(199);
		const int32 L199Friend = Chat->GetActiveInputMaxChars();
		Net->SelfSoulSilhouette = 0;
		const int32 L199 = Chat->GetActiveInputMaxChars();
		Chat->SetInputState(4);
		Net->SelfSoulSilhouette = 1;
		const int32 L199Other = Chat->GetActiveInputMaxChars();
		Check(L68 == 68 && L199Friend == 160 - Prefix.Len() && L199 == 199 && L199Other == 199,
			FString::Printf(TEXT("лимит ввода: 68 при «Друг»=%d (ждём 68), 199 при «Друг»=%d (ждём %d), без «Друг»=%d, режим 4 при «Друг»=%d (ждём 199)"),
				L68, L199Friend, 160 - Prefix.Len(), L199, L199Other));
		UAsdaChatWindowSubsystem::SetInputMaxChars(LimitWas);
		Chat->SetInputState(StateWas);
		Net->SelfSoulSilhouette = 0;
		Net->DisarmSendCapture(Gen);
		Net->OnClanInfo.Broadcast(-1, ClanWas, ClanNameWas, FString());
	}

	QClear(TEXT("друг-самотест: конец"));
	Net->SelfSoulSilhouette = SoulWas;
	AsdaSelfTestKit::Summary(LogAsdaClan, TEXT("[друг-самотест]"), Oks, Fails, TEXT(""), false);   // ★ 11.10.2026 (В3 S1): единый итог (было «проверок N, провалов M»)
	return Fails;
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanFriendGateSelfTest(
	TEXT("asda.Clan.FriendGateSelfTest"),
	TEXT("asda.Clan.FriendGateSelfTest [delay=N] [quit] — ОФЛАЙН-самотест гейта «Друг» 12435 (2003/4108, 4116, общая точка, 30015/30016/30027/14473/14461), приглашения в клан 4110/4111, хвоста 4109 код 1 и клан-чата 4120"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Clan.FriendGateSelfTest"), KitArgs)) { return; }
		if (!W || !ClanSub(W)) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Run = [WW, bQuit]()   // сам прогон (сразу или по таймеру)
		{
			UWorld* Wd = WW.Get();
			if (Wd && AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.Clan.FriendGateSelfTest"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			if (UAsdaClanSubsystem* S = ClanSub(Wd)) { S->RunFriendGateSelfTest(); }
			AsdaSelfTestKit::Quit(Wd, bQuit);
		};
		if (Delay <= 0.f) { Run(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Run), Delay, false);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ 27.09.2026: окно расширения — отладочный вход командой. Игровой вход — кнопка «Расширить клан» у Эллы (107), [ПОРТ] добавка данных.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanIncrease(
	TEXT("asda.Clan.OpenIncrease"),
	TEXT("asda.Clan.OpenIncrease — открыть окно 14450 так же, как код кнопки NPC 30027 (расширение клана)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaClanSubsystem* S = ClanSub(W)) { S->OpenIncreaseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanIncreaseOk(
	TEXT("asda.Clan.IncreaseOk"),
	TEXT("asda.Clan.IncreaseOk [online] — нажать 14461 «Подтвердить» в окне расширения (пакет 4136, списывает золото). На живом сервере — только с `online`"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// Списание золота без окна — как у `asda.Clan.DisbandOk`: онлайн только с явным `online`.
		if (FAsdaOutgoingGate::RefuseRiskyWithoutOnlineOptIn(W, TEXT("asda.Clan.IncreaseOk"), A, TEXT("расширение клана 4136 со списанием золота без окна"))) { return; }
		if (UAsdaClanSubsystem* S = ClanSub(W)) { S->OnIncreaseOkClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanName(
	TEXT("asda.Clan.SetName"),
	TEXT("asda.Clan.SetName <имя> — вписать имя в поле 14402 (для проверки без клавиатуры)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaClanSubsystem* S = ClanSub(W)) { S->SetClanNameInput(A.Num() > 0 ? A[0] : FString()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanOk(
	TEXT("asda.Clan.CreateOk"),
	TEXT("asda.Clan.CreateOk — нажать 14215 «Создание клана» (мат-фильтр → длина → пакет 4104)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaClanSubsystem* S = ClanSub(W)) { S->OnCreateOkClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanDelOk(
	TEXT("asda.Clan.DisbandOk"),
	TEXT("asda.Clan.DisbandOk [online] — нажать 14473 «ОК» в окне роспуска (пакет 4106). На живом сервере — только с `online`"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1, редакция 1 (СРЕДН-4): НЕОБРАТИМЫЙ роспуск клана (4106) без окна роспуска и подтверждения — онлайн только
		//   с явным `online`.
		if (FAsdaOutgoingGate::RefuseRiskyWithoutOnlineOptIn(W, TEXT("asda.Clan.DisbandOk"), A, TEXT("необратимый роспуск клана 4106 без окна роспуска"))) { return; }
		if (UAsdaClanSubsystem* S = ClanSub(W)) { S->OnDisbandOkClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GClanDump(
	TEXT("asda.Clan.Dump"),
	TEXT("asda.Clan.Dump — дамп КАЖДОГО контрола обоих боксов и всего состояния"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaClanSubsystem* S = ClanSub(W);
		if (!S) { UE_LOG(LogAsdaClan, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaClan, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE
