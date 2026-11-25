// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окон отряда `PARTYTAKE_WIN` (2250), `PARTYBOX_WIN` (2245), `PARTY_WIN` (2200),
//   `Follow_Another_win` (8100), ★ `PARTYDISPERSION` (2256, волна 1-Б) и отметок отряда на мини-карте (5104/5109).
//   Весь реверс, адреса, побайтовые раскладки, таблица нотисов и список дефектов оригинала —
//   в шапке `Public/UI/AsdaPartySubsystem.h` и в отчёте `Reports/party_invite_2250.md`.
#include "UI/AsdaPartySubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"          // DecodeCp1251 — имя из тела пакета
#include "Resources/AsdaSoundSubsystem.h" // звук 215 при приходе заявки
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"       // иконка уведомления (statusBarAddIcon)
#include "UI/AsdaMiniStateSubsystem.h"  // ★ 30.10.2026: общая плашка подсказки (ShowSharedTip/HideSharedTip)
#include "UI/AsdaTipPanel.h"            // ★ 30.10.2026: EAsdaTipOwner::PartyHud
#include "UI/AsdaPlayerMenuSubsystem.h"  // ★ ОБЩАЯ точка следования StartFollow
#include "GameFramework/PlayerController.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"      // ★ волна 1-Б: отметки соотрядников (EAsdaMapMark::Party, §10 заголовка)
#include "World/AsdaLoadManagerSubsystem.h" // ★ волна 1-Б: моя карта (g_nCurMapId) — признак +272 «на моей карте»
#include "World/AsdaWorldSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaCloseGear.h"   // ★ 08.11.2026: шестерёнка у крестика — общая точка (asda.UI.CloseGearHover)
#include "AsdaPartySubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPartySubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaParty);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

namespace {
	// Ассеты уже импортированы заходом JUI — своих окон не рисуем (закон `no-invented-ui`).
	const FName  kCreateWinId(TEXT("PARTYTAKE_WIN"));
	const FName  kJoinWinId(TEXT("PARTYBOX_WIN"));
	const TCHAR* kCreateWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYTAKE_WIN.WBP_PARTYTAKE_WIN_C");
	const TCHAR* kJoinWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTYBOX_WIN.WBP_PARTYBOX_WIN_C");

	// idc контролов — ровно те, что эталон ищет `CWindowMgr_FindControlByID` (social.c:1983-2050).
	const TCHAR* kIdcCreateText  = TEXT("PARTYTAKE_TXT");        // 2251
	const TCHAR* kIdcCreateCombo = TEXT("PARTYTAKE_CHOICE");     // 2254
	const TCHAR* kIdcCreateRule  = TEXT("PARTYTAKE_DIS");        // 2255
	const TCHAR* kIdcCreateOk    = TEXT("PARTYTAKE_YES");        // 2252
	const TCHAR* kIdcCreateNo    = TEXT("PARTYTAKE_NO");         // 2253
	const TCHAR* kIdcChoiceGrp   = TEXT("PartyTake_ChoiceBase_grp");

	const TCHAR* kIdcJoinText = TEXT("PARTYBOX_TXT");            // 2246
	const TCHAR* kIdcJoinOk   = TEXT("PARTYBOX_YES");            // 2248
	const TCHAR* kIdcJoinNo   = TEXT("PARTYBOX_NO");             // 2249
	// Шкала 2247 — скин `White_Bar` (Type = "ProgressBar"): ES0 подложка, ES1 ЗАЛИВКА.
	//   Импортёр разложил её на планы `PARTYBOX_TIME_13_0` (подложка) и `_13_1` (заливка).
	const TCHAR* kIdcJoinBarFill = TEXT("PARTYBOX_TIME_13_1");
}

	// ── КЛЮЧИ ОТКАТА ДЕФЕКТОВ ОРИГИНАЛА (закон `fix-original-defects`) ────────────────────────
	// D1: окно 2250 в оригинале открывается ТОЛЬКО с эмоций/горячей клавиши, а из КНОПКИ
	//     контекстного меню — никогда. Поток и оба метода доказательства — §6 заголовка.
	//     ⚠ Перепроверено 2026-08-29 по замечанию координатора: `ui_core.c:32111`
	//     (`g_nPartyUiMode = 0` перед вызовом 0x7D1) — это НЕ обработчик кнопки меню, а
	//     `UI_DispatchTargetContextMenu@0xC532D0`; все ПЯТЬ его вызывающих — эмоции и
	//     горячие клавиши. Кнопка меню идёт другим путём (`exchangeWnd_OnMessage@0xC6F400`,
	//     сообщение 0x10401) и режим не сбрасывает.
	ASDA_DEV_CVAR(int32, CVarFixContextMenuInvite,
		TEXT("asda.Party.FixContextMenuInvite"), 1,
		TEXT("1 (по умолчанию) = КНОПКА «Пригласить в отряд» контекстного меню открывает окно 2250.\n")
		TEXT("0 = поведение оригинала: кнопка не делает ничего (после ПКМ g_nPartyUiMode == 11,\n")
		TEXT("а первая строка ветки 0x7D1 требует ноль). Путь «эмоции/горячая клавиша» в оригинале\n")
		TEXT("работал — он сам обнуляет режим; у нас он ещё не портирован (реестр отложенного, П4)."),
		ECVF_Default);

namespace {
	// ★ 26.10.2026 [ОРИГИНАЛ] (ревизия wave_1025d): пометка общей цели (`g_nPartySelIdx`) и выбор строки под неё
	//   (`g_nPartySelIdxPending`) снимаются при каждой сборке интерфейса мира — `CParty_BuildUI@0xB95FF0` (social.c:1720-1721,
	//   зовёт `LoadAllInterfaces2@0xD5F270` при входе на карту, game_main.c:2827), то есть и при смене карты. 0 — как 25.10:
	//   пометка переживала смену карты и перелогин.
	ASDA_DEV_CVAR(int32, CVarShareMarkResetOnMap,
		TEXT("asda.Party.ShareMarkResetOnMap"), 1,
		TEXT("[ОРИГИНАЛ] 1 = пометка общей цели отряда и выбранная под неё строка снимаются при смене карты и любом сбросе сессии ")
		TEXT("(эталон CParty_BuildUI); 0 = как 25.10.2026: пометка переживала смену карты."),
		ECVF_Default);
}

	// D3: у лидера оригинал заполняет 2255 текстом правила и тут же его гасит.
	ASDA_DEV_CVAR(int32, CVarShowLootRuleForLeader,
		TEXT("asda.Party.ShowLootRuleForLeader"), 1,
		TEXT("1 (по умолчанию) = лидер видит правило дележа своего отряда (контрол 2255).\n")
		TEXT("0 = как в оригинале: текст ставится и сразу скрывается, окно остаётся голым."),
		ECVF_Default);

	// D4 — ПЕРЕКЛАССИФИЦИРОВАН 2026-08-29: это НЕ дефект, а МЁРТВОЕ ПОЛЕ оригинала.
	//     `byte_2BE0540` только читается (4 ссылки, все `movzx`), и все четыре чтения идут
	//     третьим аргументом `partyUi_ShowInviteRequest`, где значение выбирает loc 1167 vs
	//     1168 ТОЛЬКО для контрола 2255 — а он не показывается никогда (D3).
	//     ⇒ в оригинале от этого байта не зависит ничего наблюдаемого; чинить нечего.
	//     Смысл он приобретает лишь после НАШЕГО исправления D3, когда 2255 стал виден,
	//     поэтому ключ ниже включает НАШЕ поведение, а не чинит оригинал.
	ASDA_DEV_CVAR(int32, CVarTrackLootRule,
		TEXT("asda.Party.TrackLootRule"), 1,
		TEXT("1 (по умолчанию) = НАШЕ поведение: порт помнит правило дележа, с которым отряд\n")
		TEXT("создан, и показывает его лидеру в контроле 2255 (имеет смысл только вместе с\n")
		TEXT("asda.Party.ShowLootRuleForLeader 1).\n")
		TEXT("0 = как в оригинале: правило всегда 0 (кто успел). ★ Это НЕ починка дефекта:\n")
		TEXT("в оригинале поле byte_2BE0540 мёртвое — его читают только для контрола 2255,\n")
		TEXT("который не показывается никогда, поэтому наблюдаемой разницы там не было."),
		ECVF_Default);

namespace {
	// ★ Правка «надпись дёргается» (не дефект оригинала, а грабля переноса в UMG — см. тело
	//   `FixTextWrapJitter`). Ключ на случай, если понадобится сравнить поведение.
	ASDA_DEV_CVAR(int32, CVarFixTextJitter,
		TEXT("asda.Party.FixTextJitter"), 1,
		TEXT("1 (по умолчанию) = у текстов 2251/2246 авто-перенос снят, перенос зафиксирован по\n")
		TEXT("ширине слота из ассета — надпись не дёргается при работе с выпадающим списком.\n")
		TEXT("0 = как импортировалось: bAutoWrapText, перенос по кэшу геометрии прошлого кадра."),
		ECVF_Default);
}

	// Распределение двух подсветок строки между «моя цель» и «общая цель» (см. комментарий
	//   у `ApplyTargetShareHighlight`). 1 = как просит владелец, 0 = как в эталоне.
	ASDA_DEV_CVAR(int32, CVarSwapRowHighlights,
		TEXT("asda.Party.SwapRowHighlights"), 0,
		TEXT("0 (по умолчанию, как в эталоне) = рамка `Party_MemSelect` (183x28) — МОЯ ТЕКУЩАЯ ЦЕЛЬ,\n")
		TEXT("значок `Party_TargetShare` (29x14, кинжал) — пометка «он указывающий».\n")
		TEXT("1 = поменять узлы местами (если в скине окажутся другие картинки)."),
		ECVF_Default);

	// D5: 2245 в оригинале открывается только кликом по иконке уведомления в хотбаре.
	ASDA_DEV_CVAR(int32, CVarAutoOpenJoinBox,
		TEXT("asda.Party.AutoOpenJoinBox"), 0,
		TEXT("0 (по умолчанию, как в оригинале) = заявка кладётся в иконку уведомления хотбара,\n")
		TEXT("окно 2245 открывает КЛИК по этой иконке.\n")
		TEXT("1 = показывать окно 2245 сразу (страховка: при семи занятых слотах уведомлений\n")
		TEXT("оригинал приглашение вообще не покажет, а автоотказ через 30 с всё равно уйдёт)."),
		ECVF_Default);

	// ★ [ОРИГИНАЛ] Окно роспуска 2256: функция показа `0xB99C30` в оригинале не вызывается НИКЕМ
	//   (три метода — шапка .h §9). Оживляем заготовку на её естественном входе: кнопка 2260 у главы.
	ASDA_DEV_CVAR(int32, CVarFixDisbandConfirm,
		TEXT("asda.Party.FixDisbandConfirm"), 1,
		TEXT("1 (по умолчанию) = ГЛАВА, нажав 2260 «Выход», видит окно 2256 «Ты действительно хочешь\n")
		TEXT("разделить отряд?»: «ОК» = 5098 режим 1 (роспуск), «Отмена» = 5098 режим 0 (выход, главенство\n")
		TEXT("перейдёт). Рядовой участник выходит сразу, как в оригинале.\n")
		TEXT("0 = как в оригинале: 2260 у всех шлёт режим 0 без окна; окно 2256 не показывается ничем."),
		ECVF_Default);

	// ★ [ОРИГИНАЛ] Отметка соотрядника до первых координат: оригинал рисует её в тайле (0,0) карты 0
	//   (конструктор записи `sub_9CC8A0` обнуляет +64/+68/+276). Порт ждёт 5104/5109.
	ASDA_DEV_CVAR(int32, CVarMapMarkNeedPos,
		TEXT("asda.Party.MapMarkNeedPos"), 1,
		TEXT("1 (по умолчанию) = отметка соотрядника на мини-карте появляется только после того, как\n")
		TEXT("пришли его координаты (5104/5109) и карта (5109 @23).\n")
		TEXT("0 = как в оригинале: без данных участник считается в тайле (0,0) карты 0."),
		ECVF_Default);

namespace {
	// ── Типы иконок уведомления хотбара (`Recv_PartyMember`, подтипы a2) ──
	constexpr int32 kNoticeTypeNewParty = 0;   // пришло по 5091
	constexpr int32 kNoticeTypeJoin     = 8;   // пришло по 5095
	constexpr int32 kSoundIncoming      = 215; // csoundPlayName2DStore(g_pSoundMgr, 215, 0)
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
void UAsdaPartySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка сети только КЛАДЁМ тело в очередь.
		HReq = Net->OnPartyRequest.AddLambda([this](int32 Op, const TArray<uint8>& B)
			{ PendingIn.Push(TPair<int32, TArray<uint8>>(Op, B)); });
		HRes = Net->OnPartyResult.AddLambda([this](int32 Op, const TArray<uint8>& B)
			{ PendingIn.Push(TPair<int32, TArray<uint8>>(Op, B)); });
	}
	// Клик по иконке уведомления хотбара = `uiNoticeButtonDispatch@0xDF4F10`, ветки 0 и 8.
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->OnNoticeIconClicked.AddWeakLambda(this, [this](int32 Type)
		{
			if (Type != kNoticeTypeNewParty && Type != kNoticeTypeJoin) { return; }
			UE_LOG(LogAsdaParty, Log, TEXT("[отряд] клик по иконке уведомления типа %d → окно 2245"), Type);
			ShowJoinBox();
		});
	}
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] подсистема поднята (окна 2250, 2245, 2200, 2256, 8100; отметки мини-карты 5104/5109)"));
}

void UAsdaPartySubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnPartyRequest.Remove(HReq);
		Net->OnPartyResult.Remove(HRes);
	}
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка AsdaSessionReset.h). Именно с этой подсистемы находка
//   и началась: состав отряда с ПОКАЗАННОЙ панелью 2200 переживал перезаход, и следующий персонаж
//   видел на экране участников предыдущего.
//   ⚠ Отправок здесь нет ни одной: «выйти из отряда» серверу не сообщаем — соединения уже нет,
//     а при смене карты отряд на сервере ЖИВ и разваливать его нельзя.
void UAsdaPartySubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;

	// ── Таймеры заявок: снимаем при ЛЮБОЙ причине ──
	//   Оба таймаута по истечении ОТПРАВЛЯЮТ ответ (автоотказ на входящую, отмена своей). После
	//   разрыва это ушло бы в мёртвый сокет, после смены карты — в чужую сессию.
	bIncomingPending = false; IncomingStart = 0.0;
	IncomingUserNum = -1; IncomingCharId = 0; IncomingName.Empty(); IncomingLootRule = 0;
	bMyRequestActive = false; MyRequestStart = 0.0;
	InviteTargetUserNum = -1; InviteTargetName.Empty();
	PendingIn.Clear();
	if (bJoinOpen) { SetJoinVisible(false); }
	// ★ 26.10.2026 [ОРИГИНАЛ]: `CParty_BuildUI` — `g_nPartySelIdxPending = -1; g_nPartySelIdx = -1;` при каждой сборке интерфейса мира.
	if (CVarShareMarkResetOnMap.GetValueOnGameThread() != 0)
	{
		if (SharedTargetSlot != -1) { UE_LOG(LogAsdaParty, Log, TEXT("[отряд] сброс сессии (%s): пометка общей цели (слот %d) снята — эталон CParty_BuildUI"), AsdaSessionResetReasonName(Reason), SharedTargetSlot); }
		SharedTargetSlot = -1;
		PendingRowSlot = -1;
	}

	if (!AsdaIsFullSessionReset(Reason))
	{
		// СМЕНА КАРТЫ: отряд на сервере цел, состав и панель сохраняем. Гасим только выбор строки
		// и меню, которые указывают на акторов умершего мира и на сессионные userNum.
		if (bRowMenuOpen) { CloseRowMenu(); }
		SelectedSlot = -1;
		MenuRowSlot  = -1;
		TargetedUserNum = -1;
		bRosterDirty = true;   // строки перерисуются, когда соседи приедут на новую карту
		// ★ Волна 1-Б: мини-карта при смене карты сносит ВСЕ свои списки отметок (её `ResetSessionState`),
		//   а признак «на моей карте» (+272) эталон пересчитывает (`objRefreshOwnershipFlags`) — перестроим.
		bMarksDirty = true;
		return;
	}

	// ── ПОЛНЫЙ СБРОС: сессия персонажа кончилась ──
	if (bInParty || Members.Num() > 0)
	{
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] сброс сессии (%s): состав %d, панель 2200 %s"),
			AsdaSessionResetReasonName(Reason), Members.Num(), bRosterOpen ? TEXT("была видна") : TEXT("скрыта"));
	}
	Members.Reset();
	bInParty = false;
	bIsLeader = false;
	PartyIndex = -1;
	LootRule = 0;
	SelectedSlot = -1;
	MenuRowSlot  = -1;
	TargetedUserNum = -1;
	SelectCycleState = 0;   // ★ 25.10.2026: круг Shift+Tab (`g_nDlgStateMachine`) — сессионный

	if (bRowMenuOpen) { CloseRowMenu(); }
	if (bCreateOpen)  { SetCreateVisible(false); }
	if (WM)
	{
		WM->SetWindowVisible(kRosterWinId, false);    // панель 2200 — то самое «пережившее» окно
		WM->SetWindowVisible(kDisbandWinId, false);
	}
	bRosterOpen = false;
	bRosterCollapsed = false;
	bRosterDirty = false;
	bDisbandOpen = false;
	bMarksDirty = true;    // ★ волна 1-Б: отряда нет — список отметок мини-карты снимается на ближайшем тике
	Mouse.Reset();
	RosterMouse.Reset();
}

void UAsdaPartySubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Party");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// Пакеты разбираем ВСЕГДА, даже когда окон нет: заявка может прийти в любой момент.
	if (TArray<TPair<int32, TArray<uint8>>> Batch; PendingIn.Drain(Batch))   // ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива
	{
		for (const TPair<int32, TArray<uint8>>& It : Batch)
		{
			if (It.Key == 5091 || It.Key == 5095) { OnPartyRequest(It.Key, It.Value); }
			else                                  { OnPartyResult(It.Key, It.Value); }
		}
	}
	// ★ Волна 1-Б: отметки соотрядников на мини-карте — по флагу, ПОСЛЕ разбора всей пачки пакетов.
	if (bMarksDirty) { PushMinimapMarks(); }

	const double Now = FPlatformTime::Seconds();

	// ── Таймаут ВХОДЯЩЕЙ заявки: 30 000 мс, автоотказ тем же пакетом с кодом 0 ──
	//   Эталон `partyInviteTimeoutTick`: `if (now - start > 0x7530) { … Accept(…, 0); … }`
	if (bIncomingPending)
	{
		const double Elapsed = Now - IncomingStart;
		if (Elapsed > kIncomingTimeoutSec)
		{
			++CntTimeout;
			SendJoinAnswer(/*bAccept*/ false, /*Code*/ 0, TEXT("таймаут 30 с"));
		}
		else
		{
			// Шкала 2247: `SetRange(0, 30000.0f)` + `SetValue(now - start)` — полоса НАПОЛНЯЕТСЯ
			//   по мере истечения срока. Это поведение оригинала, а не наша инверсия.
			SetJoinTimerFrac((float)(Elapsed / kIncomingTimeoutSec));
		}
	}

	// ── Таймаут МОЕЙ заявки: 32 000 мс от `charManagerMarkAndStampTime` (party+1728) ──
	//   ★ Защёлка «запрос в полёте» ОБЯЗАНА сниматься по таймауту, иначе пункт меню замолкает
	//     навсегда с нотисом 1056 — ровно та грабля, на которой уже обжигались.
	if (bMyRequestActive && (Now - MyRequestStart) > kMyRequestTimeoutSec)
	{
		bMyRequestActive = false;
		UE_LOG(LogAsdaParty, Warning,
			TEXT("[отряд] моя заявка в отряд снята по таймауту %.0f с (эталон 0x7D00 = 32 000 мс): "
			     "сервер не ответил ни 5093, ни 5097"), kMyRequestTimeoutSec);
	}

	// ── Панель 2200: перерисовка по изменению состава и СГЛАЖИВАНИЕ полос ──
	//   ★ Закон «не защёлкивай состояние до того, как строка показана»: перерисовку делаем
	//     ТИКОМ по флагу, а не прямо из колбэка пакета — к этому моменту виджет уже живой.
	// ★★★ 22.10.2026: панель должна быть на экране, а окно снесено (травел / `CloseAll`) — вернуть (`HealRosterIfLost`).
	if (bRosterOpen) { HealRosterIfLost(); }
	if (bRosterDirty && bRosterOpen)
	{
		bRosterDirty = false;
		RefreshRoster();
	}
	TickRosterMouse();
	TickRosterHover();   // ★ 30.10.2026: справка и подсказки иконок состояний (событие 8/0xA эталона)
	if (bRosterOpen && bRosterReady)
	{
		// ★ Обводка «он моя цель» считается КАЖДЫЙ кадр от живого `TargetActor` — иначе она
		//   не погаснет при снятии цели (замечание владельца: «выделение не уходит»).
		ApplyTargetShareHighlight();
		// ★ ОБЩАЯ точка сглаживания (`FAsdaBarSmoother`, скорость `asda.UI.BarSmoothSec`) —
		//   та же, что у полос игрока и цели; своей копии не заводим.
		const float SmoothSec = FAsdaBarSmoother::SmoothSeconds();
		for (FAsdaPartyRowUi& Row : Rows)
		{
			if (Row.MemberIdx == INDEX_NONE) { continue; }
			// ★ Считаем шаг и пишем ширину ВСЕГДА: результат `Advance` больше не решает, рисовать
			//   ли — при неизменной доле он всегда false, и заливка так и оставалась авторской.
			Row.HpBar.Advance(DeltaTime, SmoothSec);
			Row.MpBar.Advance(DeltaTime, SmoothSec);
			ClipBar(Row.HpFill, Row.HpFullW, Row.HpBar.Shown);
			ClipBar(Row.MpFill, Row.MpFullW, Row.MpBar.Shown);
		}
	}

	if (!bCreateOpen && !bJoinOpen) { return; }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал. Здесь кадр мыши читает только комбобокс — своих защёлок нет.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		Mouse.Reset();
		return;
	}
	if (bCreateOpen && Combo) { Combo->Tick(Mouse); }
}

// ═════════════════════════════ ВИДЖЕТЫ ════════════════════════════════════════════════════════
bool UAsdaPartySubsystem::EnsureCreateWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kCreateWinId, kCreateWbp, kCreateZ).Ensure(GetGameInstance(), CreateWidget, bCreateReady,
		[](UAsdaWindowSubsystem&) {},   // как было: протухшее окно отдельно не сносится
		[this](UAsdaWindowSubsystem&)
		{
			SetupCreateControls();
		},
		[]()
		{
			UE_LOG(LogAsdaParty, Error, TEXT("[отряд] WBP_PARTYTAKE_WIN не загрузился (%s)"), kCreateWbp);
		});
	// Скрытие окна (шаг каркаса): .JUI: Visible="0"
	// ⚠ ЗАКРЫВАТЕЛЬ «по началу движения» НЕ РЕГИСТРИРУЕМ. `uiDialog_ClearAndHide` на движение
	//   эталон зовёт только для `g_pExchangeWnd` (input.c:869, ui_core.c:1260) — то есть для
	//   меню 2000 и окна обмена. Окна отряда он на движение не гасит, и это правильно:
	//   2245 живёт по своему таймеру 30 с, а сброс его кликом «идти» потерял бы приглашение.
	return FAsdaJuiWindowHandle::Succeeded(R) && bCreateReady;
}

bool UAsdaPartySubsystem::EnsureJoinWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kJoinWinId, kJoinWbp, kJoinZ).Ensure(GetGameInstance(), JoinWidget, bJoinReady,
		[](UAsdaWindowSubsystem&) {},   // как было: протухшее окно отдельно не сносится
		[this](UAsdaWindowSubsystem&)
		{
			SetupJoinControls();
		},
		[]()
		{
			UE_LOG(LogAsdaParty, Error, TEXT("[отряд] WBP_PARTYBOX_WIN не загрузился (%s)"), kJoinWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bJoinReady;
}

// ★★★ ПРАВКА «НАДПИСЬ ДЁРГАЕТСЯ» (2026-08-29, замечание владельца: текст «Пригласить игрока…»
//   уезжает чуть вправо и возвращается, когда нажимаешь на выпадающий список).
//   ПРИЧИНА — ДОКАЗАНА ДАМПОМ АССЕТА (`Reports/_party_win_dump.txt`):
//       PARTYTAKE_TXT  auto_wrap=True  wrap_at=0.0  just=CENTER
//       PARTYBOX_TXT   auto_wrap=True  wrap_at=0.0  just=CENTER
//   Это ровно грабля `ui-autowrap-canvas-gotcha`. У `UTextBlock` с `bAutoWrapText` ширина
//   переноса берётся из КЭШИРОВАННОЙ геометрии прошлого кадра. Клик по списку 2254 добавляет
//   в тот же канвас узлы раскрывающейся части и поднимает окно в «верхний» слой, из-за чего
//   кэш геометрии сбрасывается: один кадр текст меряется по СВОЕЙ желаемой ширине, а не по
//   ширине слота, и при выравнивании по ЦЕНТРУ строка визуально прыгает вбок; на следующем
//   кадре геометрия восстанавливается и текст возвращается.
//   ЛЕЧЕНИЕ — детерминированный перенос: `AutoWrapText = false` + `WrapTextAt = ширина слота
//   ИЗ АССЕТА` (не константа: берём `Right` слота канваса, у 2251/2246 это 200 из .JUI).
//   Перенос по словам при этом сохраняется — он есть и в оригинале (`cControl_RenderLabel`
//   выбирает флаг 0x2010 «перенос по словам»), просто больше не зависит от кадра.
//   Ключ отката — `asda.Party.FixTextJitter 0` (вернёт авто-перенос и дёрганье).
void UAsdaPartySubsystem::FixTextWrapJitter(UTextBlock* T, const TCHAR* Who)
{
	if (!T) { return; }
	if (CVarFixTextJitter.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: авто-перенос оставлен как в ассете (ключ отката выключил правку)"), Who);
		return;
	}
	float W = 0.f;
	if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(T))
	{
		const FMargin O = S->GetOffsets();
		W = O.Right;                       // ширина слота = Size из .JUI
	}
	if (W <= 1.f)
	{
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] %s: ширина слота не прочиталась (%.1f) — перенос не переставлен"), Who, W);
		return;
	}
	T->SetAutoWrapText(false);
	T->SetWrapTextAt(W);
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] %s: авто-перенос снят, перенос зафиксирован по ширине слота %.0f px"), Who, W);
}

void UAsdaPartySubsystem::SetupCreateControls()
{
	bCreateReady = false;
	if (!CreateWidget || !CreateWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	CreateWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == kIdcCreateOk) { CreateOk = B; }
			else if (N == kIdcCreateNo) { CreateNo = B; }
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == kIdcCreateText) { CreateText = T; }
			else if (N == kIdcCreateRule) { CreateRule = T; }
			else if (N == FString(kIdcCreateOk) + TEXT("_Text")) { CreateOkText = T; }
			else if (N == FString(kIdcCreateNo) + TEXT("_Text")) { CreateNoText = T; }
			continue;
		}
		if (N == kIdcChoiceGrp) { ChoiceGroup = W; }
	}

	// Список 2254 — общая точка `UAsdaJuiComboBox` (скин `Bk_ComboBox`). Раскрываем ВНИЗ:
	//   контрол стоит на y = 82 в окне высотой 145, две строки по 20 px помещаются (82…143).
	Combo = NewObject<UAsdaJuiComboBox>(this);
	if (!Combo->Setup(CreateWidget, kIdcCreateCombo, kCreateWinId, /*bOpenUp*/ false))
	{
		// Эталон в этом месте показывает MessageBoxA(nullptr, "Point NULL", "party create combo", 0).
		UE_LOG(LogAsdaParty, Error, TEXT("[отряд] контрол 2254 `PARTYTAKE_CHOICE` в дереве окна НЕ найден"));
	}
	else
	{
		// Два пункта, порядок эталона (social.c:2014-2020): сперва 12153, потом 12154.
		Combo->SetItems({ Loc(PartyLoc::kComboFree), Loc(PartyLoc::kComboAuto) });
		Combo->OnPicked.BindWeakLambda(this, [this](int32 Idx)
		{
			// `CParty_OnLootCombo@0xB98940`: индекс 0 → поле = 0, индекс 1 → поле = 1.
			UE_LOG(LogAsdaParty, Log, TEXT("[отряд] правило дележа выбрано: %d («%s»)"),
				Idx, Idx == 1 ? *Loc(PartyLoc::kComboAuto) : *Loc(PartyLoc::kComboFree));
		});
	}

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(CreateOk, 2252, [this](int32) { OnCreateOk(); });
	Bind(CreateNo, 2253, [this](int32) { OnCreateCancel(); });

	FixTextWrapJitter(CreateText, TEXT("2251 PARTYTAKE_TXT"));

	// Подписи кнопок — эталон ставит их один раз при привязке (social.c:2008-2013).
	if (CreateOkText) { CreateOkText->SetText(FText::FromString(Loc(AsdaLoc::kBtnOk))); }
	if (CreateNoText) { CreateNoText->SetText(FText::FromString(Loc(AsdaLoc::kBtnCancelAlt))); }

	bCreateReady = (CreateText != nullptr && CreateOk != nullptr && CreateNo != nullptr);
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] окно 2250 разобрано: 2251=%s 2254=%s 2255=%s 2252=%s 2253=%s группа списка=%s"),
		CreateText ? TEXT("да") : TEXT("НЕТ"),
		(Combo && Combo->IsReady()) ? TEXT("да") : TEXT("НЕТ"),
		CreateRule ? TEXT("да") : TEXT("НЕТ"),
		CreateOk ? TEXT("да") : TEXT("НЕТ"), CreateNo ? TEXT("да") : TEXT("НЕТ"),
		ChoiceGroup ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaPartySubsystem::SetupJoinControls()
{
	bJoinReady = false;
	if (!JoinWidget || !JoinWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	JoinWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W))
		{
			if (N == kIdcJoinOk) { JoinOk = B; }
			else if (N == kIdcJoinNo) { JoinNo = B; }
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == kIdcJoinText) { JoinText = T; }
			else if (N == FString(kIdcJoinOk) + TEXT("_Text")) { JoinOkText = T; }
			else if (N == FString(kIdcJoinNo) + TEXT("_Text")) { JoinNoText = T; }
			continue;
		}
		if (N == kIdcJoinBarFill) { JoinBarFill = Cast<UImage>(W); }
	}

	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(JoinOk, 2248, [this](int32) { OnJoinOk(); });
	Bind(JoinNo, 2249, [this](int32) { OnJoinCancel(); });

	FixTextWrapJitter(JoinText, TEXT("2246 PARTYBOX_TXT"));

	if (JoinOkText) { JoinOkText->SetText(FText::FromString(Loc(AsdaLoc::kBtnOk))); }
	if (JoinNoText) { JoinNoText->SetText(FText::FromString(Loc(AsdaLoc::kBtnCancelAlt))); }

	SetJoinTimerFrac(0.f);   // эталон: `SetRange(0, 30000.0f); SetValue(0)` при привязке

	bJoinReady = (JoinText != nullptr && JoinOk != nullptr && JoinNo != nullptr);
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] окно 2245 разобрано: 2246=%s 2247(заливка)=%s 2248=%s 2249=%s"),
		JoinText ? TEXT("да") : TEXT("НЕТ"), JoinBarFill ? TEXT("да") : TEXT("НЕТ"),
		JoinOk ? TEXT("да") : TEXT("НЕТ"), JoinNo ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaPartySubsystem::ResetWidgets()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kCreateWinId);
			WM->CloseWindow(kJoinWinId);
			WM->CloseWindow(kDisbandWinId);   // ★ волна 1-Б: окно роспуска 2256
		}
	}
	DisbandWidget = nullptr; DisbandText = nullptr; DisbandOk = nullptr; DisbandCancel = nullptr;
	DisbandOkText = nullptr; DisbandCancelText = nullptr;
	bDisbandReady = bDisbandOpen = false;
	if (Combo) { Combo->Reset(); }
	Combo = nullptr;
	CreateWidget = nullptr; JoinWidget = nullptr;
	CreateText = nullptr; CreateRule = nullptr; CreateOk = nullptr; CreateNo = nullptr;
	CreateOkText = nullptr; CreateNoText = nullptr; ChoiceGroup = nullptr;
	JoinText = nullptr; JoinOk = nullptr; JoinNo = nullptr;
	JoinOkText = nullptr; JoinNoText = nullptr; JoinBarFill = nullptr;
	Relays.Reset();
	bCreateReady = bJoinReady = false;
	bCreateOpen = bJoinOpen = false;
	Mouse.Reset();
}

void UAsdaPartySubsystem::SetCreateVisible(bool bVisible)
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kCreateWinId, bVisible);
		if (bVisible) { WM->BringWindowToFront(kCreateWinId); }
	}
	bCreateOpen = bVisible;
	if (!bVisible && Combo) { Combo->SetOpen(false); }
}

void UAsdaPartySubsystem::SetJoinVisible(bool bVisible)
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kJoinWinId, bVisible);
		if (bVisible) { WM->BringWindowToFront(kJoinWinId); }
	}
	bJoinOpen = bVisible;
}

void UAsdaPartySubsystem::SetJoinTimerFrac(float Frac)
{
	// Полоса `White_Bar`: ES1 — заливка шириной 20 при подложке 30. Эталон меняет ЗНАЧЕНИЕ
	//   прогресс-бара, у нас эквивалент — ширина плана заливки (та же схема, что у полос
	//   прочности в инфо-панели предмета: Right = полная ширина × доля).
	UCanvasPanelSlot* S = JoinBarFill ? AsdaUi::SlotOf(JoinBarFill) : nullptr;
	if (!S) { return; }
	FMargin O = S->GetOffsets();
	// Полную ширину запоминаем при первом обращении: в .JUI это 196 (Size "196 2").
	static constexpr float kFullW = 196.f;
	O.Right = kFullW * FMath::Clamp(Frac, 0.f, 1.f);
	S->SetOffsets(O);
}

// ═════════════════════════════ ОКНО 2250: ОТКРЫТИЕ ════════════════════════════════════════════
bool UAsdaPartySubsystem::OpenInviteWindow(int32 TargetUserNum, const FString& TargetName, bool bSkipPresenceGate, bool bFromHotkey)
{
	UGameInstance* GI = GetGameInstance();

	// ★★★ ГЕЙТ 1 ОРИГИНАЛА: `if (g_nPartyUiMode) goto LABEL_31;` (`shop_trade.c:6549`).
	//   Это ОДНОВРЕМЕННО развилка источника цели и дефект D1 — зависит от того, КАК пришли:
	//     • путь ЭМОЦИЙ/ГОРЯЧЕЙ КЛАВИШИ (`UI_DispatchTargetContextMenu@0xC532D0`,
	//       `ui_core.c:32108-32116`) перед вызовом сам пишет `g_nPartyUiMode = 0`
	//       (и имеет СВОЙ гейт `!g_nInputControlState` — «ввод не занят») ⇒ окно открывается;
	//     • путь КНОПКИ МЕНЮ (`exchangeWnd_OnMessage@0xC6F400`, сообщение 0x10401 →
	//       `exchangeUiOnCommand` с msgId = IDNum контрола) режим НЕ трогает, а
	//       `MainWndProc@0x9841A0` на WM_RBUTTONDOWN (`case 0x204`, `game_main.c:5830`)
	//       безусловно пишет туда 11 ⇒ ветка всегда гаснет.
	//   Мы приходим кнопкой и делаем ровно то же, что делает работающий путь: заходим с нулём.
	//   При выключенном ключе воспроизводим оригинал ДОСЛОВНО: меню гасится, ничего не делаем.
	if (!bFromHotkey && CVarFixContextMenuInvite.GetValueOnGameThread() == 0)   // ★ 25.10.2026: вход клавиши режим 0 ставит сам
	{
		UE_LOG(LogAsdaParty, Warning,
			TEXT("[отряд] 1:1 оригинал (asda.Party.FixContextMenuInvite 0): КНОПКА «Пригласить в отряд» "
			     "не делает ничего — после ПКМ g_nPartyUiMode == 11, а ветка 0x7D1 требует ноль. "
			     "В оригинале окно открывалось только с эмоций/горячей клавиши."));
		return false;
	}

	// ГЕЙТ 2: цель должна существовать в менеджере персонажей
	//   (`CharManager_FindByUserIdx(g_nPartyInviteTargetUid)`; null ⇒ молча закрыть меню).
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (TargetUserNum < 0 || (!bSkipPresenceGate && WS && !WS->GetPlayerActorByUserNum(TargetUserNum)))
	{
		UE_LOG(LogAsdaParty, Warning,
			TEXT("[отряд] 2250 не открыто: цель userNum=%d в реестре соседей не найдена "
			     "(эталон в этом месте молча гасит меню)"), TargetUserNum);
		return false;
	}

	// ГЕЙТ 3: цель — я сам (`effectSlots[112].field_40`) → нотис 1093.
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && AsdaSelf::IsSelfUserIdx(Net, TargetUserNum))
	{
		Notice(PartyLoc::kSelfInvite);
		return false;
	}

	// ГЕЙТ 4: цель уже в моём отряде (`Party_FindMemberByUserIdx`) → нотис 1047.
	if (IsMemberUserNum(TargetUserNum))
	{
		Notice(PartyLoc::kAlreadyInParty);
		return false;
	}

	// ГЕЙТ 5: моя заявка уже в полёте (`sub_925A3F` = get_o2418E0) → нотис 1056.
	if (bMyRequestActive)
	{
		Notice(PartyLoc::kOneAtATime);
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] 2250 не открыто: моя заявка в полёте уже %.1f с (лимит %.0f с)"),
			FPlatformTime::Seconds() - MyRequestStart, kMyRequestTimeoutSec);
		return false;
	}

	// ГЕЙТ 6: висит ВХОДЯЩАЯ заявка (`sub_924E28` = get_o2418E1) → нотис 1045.
	if (bIncomingPending)
	{
		Notice(PartyLoc::kHasIncoming);
		return false;
	}

	// ГЕЙТ 7: я в отряде, но НЕ глава → нотис 1089 (ветка `Party_IsInParty && !Party_IsMeBoss`).
	if (bInParty && !bIsLeader)
	{
		Notice(PartyLoc::kNotLeader);
		return false;
	}

	return ShowInviteRequestFor(TargetUserNum, TargetName);
}

// ★ 30.10.2026 (боевая фаза «Битвы армий», окно «Рейд» 49410): тело `partyUi_ShowInviteRequest@0xB99630` без гейтов меню —
//   эталон зовёт его напрямую из `CClan_OnClanUIInput` (свои гейты 1056/1089/1045 там же, G:308383-308425). Прежде было хвостом
//   `OpenInviteWindow` (одна точка — вторую копию не заводим).
bool UAsdaPartySubsystem::ShowInviteRequestFor(int32 TargetUserNum, const FString& TargetName)
{
	UGameInstance* GI = GetGameInstance();
	if (!EnsureCreateWidget()) { return false; }

	InviteTargetUserNum = TargetUserNum;
	InviteTargetName    = TargetName;

	// Текст 2251 — loc 20020 со слотом-именем (`CObject_SetSlotString(ctx, 0, имя)`).
	if (CreateText)
	{
		FString S;
		if (UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			S = TS->FormatById(PartyLoc::kInviteAsk, { TargetName }, {});
		}
		if (S.IsEmpty()) { S = Loc(PartyLoc::kInviteAsk).Replace(TEXT("[ss1]"), *TargetName); }
		CreateText->SetText(FText::FromString(S));
	}

	// ── Две ветки эталона `partyUi_ShowInviteRequest` ──
	const bool bLeaderBranch = bInParty && bIsLeader;
	const int32 RuleNow = GetPartyLootRule();

	// ★ Расплющенная иерархия: список 2254 — это ComboBoxString + планы + подпись `_Text`.
	//   Гасить/показывать надо ВСЮ группу `PartyTake_ChoiceBase_grp`, иначе от скрытого списка
	//   остаётся рамка (закон `jui-uneven-node-layout`).
	if (ChoiceGroup)
	{
		ChoiceGroup->SetVisibility(bLeaderBranch
			? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (Combo)
	{
		Combo->SetOpen(false);
		// Эталон при привязке ставит выбор аргументом `comboSelArg`; в ветке «создать отряд»
		//   список ПОКАЗАН и выбор сохраняется между открытиями — оставляем как есть, а если
		//   выбора ещё не было, ставим 0 («кто успел»), первый пункт списка.
		if (Combo->GetSelectedIndex() == INDEX_NONE) { Combo->SetSelectedIndex(0); }
	}

	if (CreateRule)
	{
		const int32 RuleLoc = (RuleNow != 0) ? PartyLoc::kRuleAuto : PartyLoc::kRuleFree;
		CreateRule->SetText(FText::FromString(Loc(RuleLoc)));
		// ★ ДЕФЕКТ ОРИГИНАЛА D3: эталон в ОБЕИХ ветках делает `SetVisible(2255, 0)` сразу после
		//   того, как положил туда текст, — работа впустую, лидер правила не видит.
		//   ФИКС: в ветке лидера показываем (только для чтения); в ветке создания отряда
		//   правило выбирается списком 2254, поэтому 2255 скрыт — там оригинал прав.
		const bool bShowRule = bLeaderBranch && CVarShowLootRuleForLeader.GetValueOnGameThread() != 0;
		CreateRule->SetVisibility(bShowRule
			? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	// Эталон в ветке «создать отряд» СБРАСЫВАЕТ флаг заявки (`sub_923EAB(mgr, 0)`), а в ветке
	//   лидера — ВЗВОДИТ (`sub_923EAB(mgr, 1)`) ещё ДО отправки пакета. Воспроизводим 1:1;
	//   штамп времени ставим тоже сейчас, иначе взведённый флаг некому будет снять.
	if (bLeaderBranch) { bMyRequestActive = true; MyRequestStart = FPlatformTime::Seconds(); }
	else               { bMyRequestActive = false; }

	SetCreateVisible(true);
	++CntOpened;
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] окно 2250 открыто по «%s» (userNum=%d), ветка=%s, правило отряда=%d; "
		     "текст 2251 (loc 20020): «%s»; список 2254: [0]=«%s» [1]=«%s», видим=%s; "
		     "2255 (loc %d): «%s», видим=%s"),
		*TargetName, TargetUserNum, bLeaderBranch ? TEXT("я глава отряда") : TEXT("создание отряда"),
		RuleNow,
		CreateText ? *CreateText->GetText().ToString() : TEXT("—"),
		Combo ? *Combo->GetItemLabel(0) : TEXT("—"), Combo ? *Combo->GetItemLabel(1) : TEXT("—"),
		(ChoiceGroup && ChoiceGroup->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("да") : TEXT("нет"),
		(RuleNow != 0) ? PartyLoc::kRuleAuto : PartyLoc::kRuleFree,
		CreateRule ? *CreateRule->GetText().ToString() : TEXT("—"),
		(CreateRule && CreateRule->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("да") : TEXT("нет"));
	return true;
}

void UAsdaPartySubsystem::CloseInviteWindow()
{
	// `sub_923E6A` → `sub_B998D0`: `CControl_SetVisible(this[73], 0)` — просто спрятать.
	SetCreateVisible(false);
}

int32 UAsdaPartySubsystem::GetLootRuleChoice() const
{
	const int32 Idx = Combo ? Combo->GetSelectedIndex() : INDEX_NONE;
	return (Idx == 1) ? 1 : 0;
}

void UAsdaPartySubsystem::SetLootRuleChoice(int32 Idx)
{
	if (Combo) { Combo->SetSelectedIndex(FMath::Clamp(Idx, 0, 1)); }
}

int32 UAsdaPartySubsystem::GetPartyLootRule() const
{
	// ★ D4 — МЁРТВОЕ ПОЛЕ ОРИГИНАЛА (не дефект): `byte_2BE0540` только читается
	//   (4 вхождения, все `movzx eax, byte ptr [0x2BE0540]`), записи нет ⇒ там ВСЕГДА 0.
	//   Наблюдаемой разницы это не давало: единственный потребитель — контрол 2255,
	//   который оригинал не показывает никогда. Наше значение нужно нашему же показу.
	return (CVarTrackLootRule.GetValueOnGameThread() != 0) ? LootRule : 0;
}

// ═════════════════════════════ ОКНО 2250: КНОПКИ ══════════════════════════════════════════════
void UAsdaPartySubsystem::OnCreateOk()
{
	// Порядок эталона (social.c:2295-2343): решение принимается ДО того, как окно прячется,
	//   а прячет его общий `sub_923E6A(this)` в самом конце ветки.
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 Target = InviteTargetUserNum;
	const FString Name = InviteTargetName;

	if (Target < 0 || !Net)
	{
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] 2252: цели нет (userNum=%d) — пакет не отправлен"), Target);
		CloseInviteWindow();
		return;
	}

	bool bSent = false;
	if (bInParty)
	{
		if (bIsLeader)
		{
			// Я глава → «позвать в мой отряд», опкод 5094 (userNum, БЕЗ правила: правило у отряда
			//   уже есть). `Party_Send_JoinReq(targetCha->userNum)`.
			// ★ В ветке лидера защёлка УЖЕ взведена при открытии окна (эталонный
			//   `partyUi_ShowInviteRequest` делает `sub_923EAB(mgr, 1)` до показа), поэтому
			//   здесь её только продлевать не нужно — оставляем как есть, 1:1.
			bSent = Net->SendPartyJoinReq5094(Target);
		}
		else
		{
			// ★ Эталон проверяет это ВТОРОЙ раз (гейт 7 стоял ещё при открытии): состояние могло
			//   смениться, пока окно висело. Повторяем.
			Notice(PartyLoc::kNotLeader);
		}
	}
	else
	{
		// Я не в отряде → «создать отряд», опкод 5090 (userNum + правило из списка 2254).
		const int32 Rule = GetLootRuleChoice();
		// ★ ПОРЯДОК 1:1 (`social.c:2313-2314`): `sub_923EAB(g_CharManager, 1)` стоит ПЕРЕД
		//   отправкой и БЕЗУСЛОВНО, а `charManagerMarkAndStampTime(g_pPartyWnd)` — внутри самого
		//   отправителя. Обе строки про одну защёлку. Ставить её «только если пакет ушёл» было бы
		//   отступлением от эталона; снимет её ответ сервера или таймаут 32 с.
		bMyRequestActive = true;
		MyRequestStart = FPlatformTime::Seconds();
		bSent = Net->SendPartyNewReq5090(Target, (uint8)Rule);
		// ★ D4: оригинал не помнит, каким правилом отряд создан. Мы помним — иначе окно
		//   лидера навсегда показывало бы «кто успел».
		LootRule = Rule;
	}

	if (bSent) { ++CntSentReq; }
	// ★ РАЗВИЛКА ЭТАЛОНА ПО РЕЖИМУ (`social.c:2296-2343`) и как она легла на порт:
	//     `g_nPartyUiMode == 0` (пришли с эмоций/клавиши) → ветка else (`:2319-2341`): цель
	//       берётся из `g_nPartyInviteTargetUid`, после чего режим взводится в 11, а цель — в −1;
	//     `g_nPartyUiMode != 0` → первая ветка: цель уже лежит в окне (`*((_WORD*)this + 862)`),
	//       и после отправки тоже сбрасывается в −1.
	//   У нас цель всегда приходит явным аргументом `OpenInviteWindow` ⇒ воспроизведена ветка
	//   else; общий для обеих веток сброс цели в −1 сохранён строкой ниже.
	//   ★ Взведение режима в 11 порт НАМЕРЕННО не повторяет: второго источника цели у нас нет,
	//     а в оригинале именно эта строка и делала поле окна (party+1724) вечно невалидным (D2).
	InviteTargetUserNum = -1;   // эталон: `*((_WORD*)this + 862) = -1` после отправки
	CloseInviteWindow();
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] 2252 «ОК» по «%s»: пакет %s"),
		*Name, bSent ? TEXT("отправлен") : TEXT("НЕ отправлен (см. причину выше)"));
}

void UAsdaPartySubsystem::OnCreateCancel()
{
	// `case 2253`: `sub_923EAB(g_CharManager, 0); sub_923E6A(this);` — снять флаг и спрятать.
	bMyRequestActive = false;
	InviteTargetUserNum = -1;
	CloseInviteWindow();
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] 2253 «Отмена»: окно 2250 закрыто, флаг заявки снят"));
}

// ═════════════════════════════ ОКНО 2245 ══════════════════════════════════════════════════════
bool UAsdaPartySubsystem::ShowJoinBox()
{
	// `partyShowInviteRequest@0xB99970`: `if (a4 == -1) return 0;` — нет висящей заявки, нет окна.
	if (!bIncomingPending)
	{
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] окно 2245 не показано: висящей заявки нет"));
		return false;
	}
	if (!EnsureJoinWidget()) { return false; }

	if (JoinText)
	{
		// a4 = правило дележа: 0 → loc 20021 «кто успел», иначе → loc 20022 «автораспределение».
		const int32 LocId = (IncomingLootRule != 0) ? PartyLoc::kJoinAskAuto : PartyLoc::kJoinAskFree;
		FString S;
		if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			S = TS->FormatById(LocId, { IncomingName }, {});
		}
		if (S.IsEmpty()) { S = Loc(LocId).Replace(TEXT("[ss1]"), *IncomingName); }
		JoinText->SetText(FText::FromString(S));
	}
	SetJoinVisible(true);
	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] окно 2245 показано: «%s» (userNum=%d), правило=%d ⇒ loc %d, reqId=%d, тип=%s; текст 2246: «%s»"),
		*IncomingName, IncomingUserNum, IncomingLootRule,
		(IncomingLootRule != 0) ? PartyLoc::kJoinAskAuto : PartyLoc::kJoinAskFree, IncomingReqId,
		bIncomingIsNew ? TEXT("новый отряд (5091)") : TEXT("вступление (5095)"),
		JoinText ? *JoinText->GetText().ToString() : TEXT("—"));
	return true;
}

void UAsdaPartySubsystem::OnJoinOk()
{
	// ★ 06.10.2026 (волна 2-В): ветки отряда в `CParty_OnControl` стоят под флагами заявки (`sub_929A1D`/`sub_92D0FA`);
	//   без висящей заявки отряда они молчат — и окно может принадлежать дуэли. Поэтому ответ отряда только при заявке.
	if (bIncomingPending) { SendJoinAnswer(/*bAccept*/ true, /*Code*/ 1, TEXT("кнопка 2248 «ОК»")); }
	OnJoinBoxButton.Broadcast(true);   // ветка дуэли `if (dword_2C30494 >= 0)` (ui_core: CParty_OnControl case 2248)
}

void UAsdaPartySubsystem::OnJoinCancel()
{
	if (bIncomingPending) { SendJoinAnswer(/*bAccept*/ false, /*Code*/ 0, TEXT("кнопка 2249 «Отмена»")); }
	OnJoinBoxButton.Broadcast(false);  // ветка дуэли case 2249
}

// ★ 06.10.2026 (волна 2-В): второй владелец окна 2245 — дуэль (`duelUiPanelShow@0xDE4190`).
bool UAsdaPartySubsystem::ShowJoinBoxText(const FString& Text)
{
	if (!EnsureJoinWidget()) { return false; }
	if (JoinText) { JoinText->SetText(FText::FromString(Text)); }   // `CControl_SetText(m_pLabelText, buf)`
	SetJoinVisible(true);                                             // `CControl_SetVisible(m_pDuelWnd, 1)`
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] окно 2245 показано вторым владельцем (дуэль): «%s»"), *Text);
	return true;
}

// ★ 06.10.2026 (волна 2-В): `duelUiPanelReset@0xDE41E0` — окно общее, эталон прячет его целиком.
void UAsdaPartySubsystem::HideJoinBoxShared()
{
	SetJoinVisible(false);
	SetJoinTimerFrac(0.f);
}

// Текст 2246 сейчас (для самотеста дуэли).
FString UAsdaPartySubsystem::GetJoinBoxText() const
{
	return JoinText ? JoinText->GetText().ToString() : FString();
}

void UAsdaPartySubsystem::SendJoinAnswer(bool bAccept, uint8 Code, const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!bIncomingPending)
	{
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] ответ «%s» отброшен: висящей заявки нет"), Why);
		return;
	}
	bool bSent = false;
	if (Net)
	{
		// ★ Ветку выбирают ИМЕННО флаги +2365666 / +2365667 (`social.c:2241-2294`), а не тип окна.
		if (bIncomingIsNew)       { bSent = Net->SendPartyNewAccept5092(Code, IncomingReqId); }
		else if (bIncomingIsJoin) { bSent = Net->SendPartyInviteAccept5096(Code, IncomingReqId); }
		else
		{
			UE_LOG(LogAsdaParty, Error,
				TEXT("[отряд] ответ «%s» не отправлен: ни один из флагов типа заявки не взведён"), Why);
		}
	}
	if (bSent) { ++CntSentAns; }
	UE_LOG(LogAsdaParty, Log, TEXT("[отряд] ответ на заявку: %s (код %u, reqId %d, повод: %s) -> %s"),
		bAccept ? TEXT("ПРИНЯТЬ") : TEXT("ОТКАЗАТЬ"), (uint32)Code, IncomingReqId, Why,
		bSent ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	ClearIncoming(Why);
}

// ★ 22.10.2026 — разбор у объявления. Эталон: `if (sub_924E28) { …значок…; if (sub_929A1D) Party_Send_NewPartyAccept(reqId, 0);
//   else if (sub_92D0FA) Party_Send_InviteAccept(0, reqId); …окно 2245 прячется, таймер снят }` — ровно `SendJoinAnswer(false, 0)`.
void UAsdaPartySubsystem::DeclineIncomingOnMapLoad(const TCHAR* Why)
{
	if (!bIncomingPending) { return; }
	SendJoinAnswer(/*bAccept*/ false, /*Code*/ 0, Why);
}

void UAsdaPartySubsystem::ClearIncoming(const TCHAR* Why)
{
	// `sub_934F53(this)` (спрятать 2245) + сброс четырёх флагов + обнуление шкалы.
	SetJoinVisible(false);
	SetJoinTimerFrac(0.f);
	if (UAsdaHotbarSubsystem* Hb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		// `buffSlotCompact(g_pQuickSlotBar, тип)` + `MsgNotify_RefreshIcons` — снять иконку.
		Hb->RemoveNoticeIcon(bIncomingIsNew ? kNoticeTypeNewParty : kNoticeTypeJoin);
	}
	bIncomingPending = false;
	bIncomingIsNew = false;
	bIncomingIsJoin = false;
	IncomingReqId = -1;
	IncomingUserNum = -1;
	IncomingCharId = 0;
	IncomingName.Reset();
	IncomingLootRule = 0;
	UE_LOG(LogAsdaParty, Verbose, TEXT("[отряд] входящая заявка снята: %s"), Why);
}

bool UAsdaPartySubsystem::IsMemberUserNum(int32 UserNum) const
{
	if (!bInParty || UserNum < 0) { return false; }
	for (const FAsdaPartyMember& M : Members) { if (M.UserNum == UserNum) { return true; } }
	return false;
}

bool UAsdaPartySubsystem::IsMemberCharId(int32 CharId) const
{
	// `Party_FindMemberByUserIdx(g_CharManager, charId)` (имя врёт: ключ — charId записи +4).
	if (!bInParty || CharId == 0) { return false; }
	for (const FAsdaPartyMember& M : Members) { if (M.CharId == CharId) { return true; } }
	return false;
}

// ═════════════════════════════ ПРИЁМ 5091 / 5095 ══════════════════════════════════════════════
void UAsdaPartySubsystem::OnPartyRequest(int32 Opcode, const TArray<uint8>& B)
{
	if (B.Num() < 30) { return; }
	const int32 Code     = (int32)B[0];
	const int32 UserNum  = AsdaBytes::RdI16(&B[1]);
	const int32 CharId   = AsdaBytes::RdI32(&B[3]);
	const FString Name   = FAsdaLoginData::DecodeCp1251(&B[7], 20);
	const int32 Rule     = (int32)B[27];
	const int32 ReqId    = AsdaBytes::RdI16(&B[28]);
	const bool  bIsNew   = (Opcode == 5091);

	// ★ Код ≠ 0 — это НЕ заявка, а событие/отказ. Эталон разводит их разными таблицами:
	//   5091 → `Party_DispatchEventNotice@0xF388E0`, 5095 → `Notice_ShowResultByCode@0xF391F0`.
	if (Code != 0)
	{
		bMyRequestActive = false;   // `sub_923EAB(mgr, 0); sub_92E063(mgr, 0);`
		int32 LocId = 0; bool bPrefixName = false; bool bWarn = false;
		if (bIsNew)
		{
			switch (Code)
			{
			case 1:  LocId = 1040; break;                                    // «Память переполнена»
			case 2:  case 10: LocId = 1041; bPrefixName = true; break;       // «уже состоит в отряде»
			case 3:  LocId = 1042; bPrefixName = true; break;                // «уже вне игры»
			case 4:  LocId = 1043; break;                                    // «нельзя нескольких»
			case 5:  LocId = 1044; break;                                    // «уже приглашаешь»
			case 6:  LocId = 1045; break;                                    // «получено приглашение»
			case 7:  LocId = 1046; break;
			case 8:  LocId = 1047; break;                                    // «уже в отряде»
			case 9:  LocId = 1091; bPrefixName = true; break;                // «приглашение отклонено»
			case 11: LocId = 1112; bWarn = true; break;                      // «другая армия», КРАСНЫЙ
			default: break;
			}
		}
		else
		{
			switch (Code)
			{
			case 1: LocId = 1052; break;
			case 2: LocId = 1053; break;
			case 3: LocId = 1054; break;
			case 4: LocId = 1047; break;
			case 5: LocId = 1055; break;
			case 6: LocId = 1057; break;
			case 7: LocId = 1058; break;
			case 8: LocId = 1059; break;
			case 9: LocId = 1091; bPrefixName = true; break;
			case 10: LocId = 1094; bPrefixName = true; break;
			case 11: LocId = 1112; bWarn = true; break;
			default: break;
			}
		}
		if (LocId != 0) { Notice(LocId, FString(), bPrefixName ? Name : FString(), bWarn); }
		else
		{
			UE_LOG(LogAsdaParty, Warning,
				TEXT("[отряд] ←%d код %d: у эталона строки для него нет (default: без нотиса)"),
				Opcode, Code);
		}
		return;
	}

	// ── Код 0 = настоящая ЗАЯВКА ──
	// Эталон сперва проверяет свои же защёлки и, если занят, отвечает отказом с особым кодом
	//   (`Recv_GM_NEWPARTY_REQUESTUSER_RES`: 5 при `get_o2418E1`, 6 при `get_o2418E0`;
	//    `Recv_GM_INVITEPARTY_REQUEST_RES`: 6 и 7 соответственно).
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (bIncomingPending || bMyRequestActive)
	{
		const uint8 Busy = bIncomingPending ? (bIsNew ? 5 : 6) : (bIsNew ? 6 : 7);
		if (Net)
		{
			if (bIsNew) { Net->SendPartyNewAccept5092(Busy, ReqId); }
			else        { Net->SendPartyInviteAccept5096(Busy, ReqId); }
		}
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] ←%d заявка от «%s» отбита кодом %u: %s"), Opcode, *Name, (uint32)Busy,
			bIncomingPending ? TEXT("уже висит другая входящая") : TEXT("моя заявка в полёте"));
		return;
	}

	++CntIncoming;
	bIncomingPending = true;
	bIncomingIsNew   = bIsNew;
	bIncomingIsJoin  = !bIsNew;
	IncomingStart    = FPlatformTime::Seconds();   // `partyInviteReqTimerStart@0xB99910`
	IncomingReqId    = ReqId;
	IncomingUserNum  = UserNum;
	IncomingCharId   = CharId;
	IncomingName     = Name;
	IncomingLootRule = Rule;

	// 1:1 эталон: иконка уведомления + звук 215 + нотис 5057. Окно 2245 НЕ открывается само.
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->AddNoticeIcon(bIsNew ? kNoticeTypeNewParty : kNoticeTypeJoin);
	}
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(kSoundIncoming);
	}
	Notice(PartyLoc::kIncomingSound);
	SetJoinTimerFrac(0.f);

	UE_LOG(LogAsdaParty, Log,
		TEXT("[отряд] ←%d ЗАЯВКА от «%s» (userNum=%d charId=%d), правило=%d, reqId=%d; "
		     "иконка уведомления типа %d, таймер 30 с пошёл"),
		Opcode, *Name, UserNum, CharId, Rule, ReqId, bIsNew ? kNoticeTypeNewParty : kNoticeTypeJoin);

	// ★ D5: страховка от «семь слотов заняты — приглашения не видно, а автоотказ уйдёт».
	if (CVarAutoOpenJoinBox.GetValueOnGameThread() != 0) { ShowJoinBox(); }
}

// ═════════════════════════════ ПРИЁМ 5093 / 5097 / 5099 / 5101 / 5102 / 5103 ══════════════════
void UAsdaPartySubsystem::OnPartyResult(int32 Opcode, const TArray<uint8>& B)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 SelfCharId = Net ? Net->SelectedCharId : 0;
	// ★ Волна 1-Б: любой пакет этой семьи меняет состав/координаты — отметки мини-карты перестроит `Tick`.
	bMarksDirty = true;

	switch (Opcode)
	{
	case 5093:   // `Recv_GM_NEWPARTY_RESPONSEUSER_RES@0xF38E10`, тело 15 б
	{
		if (B.Num() < 15) { return; }
		bMyRequestActive = false;                 // `sub_923EAB(mgr,0); sub_92E063(mgr,0);`
		const int32 Code = (int32)B[0];
		const int32 ReqUserNum  = AsdaBytes::RdI16(&B[1]);
		const int32 ReqCharId   = AsdaBytes::RdI32(&B[3]);
		const int32 ResUserNum  = AsdaBytes::RdI16(&B[7]);
		const int32 ResCharId   = AsdaBytes::RdI32(&B[9]);
		const int32 NewPartyIdx = AsdaBytes::RdI16(&B[13]);
		static const int32 Map[7] = { 1048, 1049, 1050, 1051, 1047, 1110, 1111 };
		if (Code >= 0 && Code <= 6) { Notice(Map[Code]); }
		if (Code == 1)
		{
			// Отряд СОЗДАН. Эталон здесь запоминает только `g_nPartyInviteTargetIdx = body[13]`
			//   (его читают кнопки 2258/2259 окна роспуска — у порта это `PartyIndex`);
			//   полный состав приезжает пакетом 5107, координаты — 5104/5109.
			//   До 5107 порт держит минимальный состав, чтобы гейты 1047/1089 были не выдуманными.
			bInParty = true;
			PartyIndex = NewPartyIdx;
			bIsLeader = (SelfCharId != 0 && SelfCharId == ReqCharId);
			Members.Reset();
			FAsdaPartyMember A; A.UserNum = ReqUserNum; A.CharId = ReqCharId; A.bLeader = true;
			FAsdaPartyMember C; C.UserNum = ResUserNum; C.CharId = ResCharId; C.bLeader = false;
			if (bIsLeader) { A.Name = TEXT(""); C.Name = InviteTargetName; }
			Members.Add(A); Members.Add(C);
		}
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] ←5093 код=%d partyIndex=%d (запросил userNum=%d/charId=%d, ответил userNum=%d/charId=%d) "
			     "⇒ в отряде=%s, глава=%s"),
			Code, NewPartyIdx, ReqUserNum, ReqCharId, ResUserNum, ResCharId,
			bInParty ? TEXT("да") : TEXT("нет"), bIsLeader ? TEXT("да") : TEXT("нет"));
		break;
	}

	case 5097:   // `Recv_GM_INVITEPARTY_RESPONSE_RES@0xF39710`, тело 29 б
	{
		if (B.Num() < 29) { return; }
		bMyRequestActive = false;
		const int32 Code    = (int32)B[0];
		const int32 UserNum = AsdaBytes::RdI16(&B[1]);
		const int32 CharId  = AsdaBytes::RdI32(&B[3]);
		const FString Name  = FAsdaLoginData::DecodeCp1251(&B[7], 20);
		const int32 PIdx    = AsdaBytes::RdI16(&B[27]);
		switch (Code)
		{
		case 0: Notice(1084, FString(), Name); break;   // отказался
		case 1:
		{
			Notice(1085, FString(), Name);              // принял → `Party_AddMemberFromInvite`
			bInParty = true;
			if (PartyIndex < 0) { PartyIndex = PIdx; }
			// Приглашал я, значит глава — я (5094 шлёт только глава, 5090 создаёт меня главой).
			if (Members.Num() == 0) { bIsLeader = true; }
			FAsdaPartyMember M; M.UserNum = UserNum; M.CharId = CharId; M.Name = Name;
			Members.Add(M);
			break;
		}
		case 2: Notice(1042, FString(), Name); break;
		case 3: Notice(1051); break;
		case 4: Notice(1047); break;
		case 6: Notice(1110); break;
		case 7: Notice(1111); break;
		default:
			UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] ←5097 код %d: у эталона ветки нет"), Code);
			break;
		}
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] ←5097 код=%d «%s» (userNum=%d charId=%d) partyIndex=%d, участников теперь %d"),
			Code, *Name, UserNum, CharId, PIdx, Members.Num());
		break;
	}

	case 5107:   // `Recv_GM_MEMBER_INFO@0x9CD4A0` — ПОЛНЫЙ состав отряда, тело 144 б (0x90)
	{
		// Раскладка: @0…23 = ШЕСТЬ i32 charId, @24…143 = шесть char[20] имён.
		// ★ ЛОВУШКА ПРОЕКТА: пустой слот = **−1** (`if (a2[i] != -1)`, `network.c:4731`).
		//   Слот 0 — ГЛАВА отряда (`if (!i) LOBYTE(memberInfo[18]) = 1`), и там же эталон
		//   решает «я ли босс»: `*(v7 + 2365656) = (a2[0] == myCharId)`.
		if (B.Num() < 144) { return; }
		TArray<FAsdaPartyMember> Fresh;
		int32 LeaderCharId = 0;
		for (int32 i = 0; i < 6; ++i)
		{
			const int32 Cid = AsdaBytes::RdI32(&B[4 * i]);
			if (Cid == -1) { continue; }                       // пустой слот
			FAsdaPartyMember M;
			M.CharId  = Cid;
			M.Name    = FAsdaLoginData::DecodeCp1251(&B[24 + 20 * i], 20);
			M.bLeader = (i == 0);
			if (i == 0) { LeaderCharId = Cid; }
			// Уже известные по прежним пакетам параметры не теряем.
			for (const FAsdaPartyMember& Old : Members)
			{
				if (Old.CharId != Cid) { continue; }
				M.UserNum = Old.UserNum; M.Level = Old.Level; M.ClassCode = Old.ClassCode;
				M.Hp = Old.Hp; M.MaxHp = Old.MaxHp; M.Mp = Old.Mp; M.MaxMp = Old.MaxMp;
				M.StateKind = Old.StateKind; M.StateId = Old.StateId;
				// ★ Волна 1-Б: и координаты на карте (+64/+68/+276/+272/+96). Эталон на 5107 запись
				//   пересоздаёт (`listFreeAllNodes` + конструктор с нулями) — то же отступление порта,
				//   что и для уровня/HP выше: известное не теряем, иначе отметка мигала бы в (0,0).
				M.TileX = Old.TileX; M.TileZ = Old.TileZ; M.bPosKnown = Old.bPosKnown;
				M.MapId = Old.MapId; M.bOnMyMap = Old.bOnMyMap; M.Param96 = Old.Param96;
				break;
			}
			Fresh.Add(M);
		}
		Members = MoveTemp(Fresh);
		bInParty  = (Members.Num() > 0);
		bIsLeader = (SelfCharId != 0 && LeaderCharId == SelfCharId);
		if (!bInParty) { PartyIndex = -1; LootRule = 0; }
		bRosterDirty = true;
		SetRosterVisible(bInParty);
		UE_LOG(LogAsdaParty, Log,
			TEXT("[отряд] ←5107 состав: участников %d, глава charId=%d (%s), я в отряде=%s"),
			Members.Num(), LeaderCharId, bIsLeader ? TEXT("это я") : TEXT("другой"),
			bInParty ? TEXT("да") : TEXT("нет"));
		break;
	}

	case 5108:   // `Recv_GM_PARTY_UPDATE_STATE@0xF3A790` — 28 состояний одного участника
	{
		// Раскладка: @0 i32 userIdx, дальше 28 записей по 6 б начиная с @4.
		// Эталон кладёт их в `member + 104 + 6*i` и читает как «вид @0 (u8), id @2 (i32)»
		//   (`partyUpdateStateIcons`, `social.c:2130/2145`). Виды: 1 — навык, 3 — соулмейт.
		if (B.Num() < 4 + 6 * 28) { return; }
		const int32 UserIdx = AsdaBytes::RdI32(&B[0]);
		FAsdaPartyMember* M = nullptr;
		for (FAsdaPartyMember& It : Members)
		{
			if (It.UserNum == UserIdx || It.CharId == UserIdx) { M = &It; break; }
		}
		if (!M)
		{
			UE_LOG(LogAsdaParty, Verbose, TEXT("[отряд] ←5108: участник userIdx=%d в составе не найден"), UserIdx);
			break;
		}
		M->StateKind.SetNumZeroed(28);
		M->StateId.SetNumZeroed(28);
		int32 Live = 0;
		for (int32 i = 0; i < 28; ++i)
		{
			const int32 Off = 4 + 6 * i;
			M->StateKind[i] = B[Off];
			M->StateId[i]   = AsdaBytes::RdI32(&B[Off + 2]);
			if (M->StateKind[i] == 1 || M->StateKind[i] == 3) { ++Live; }
		}
		bRosterDirty = true;
		UE_LOG(LogAsdaParty, Verbose, TEXT("[отряд] ←5108 состояния «%s»: живых записей %d"), *M->Name, Live);
		break;
	}

	case 5109:   // `Recv_GM_PARTY_UPDATE_PARAM@0xB9ACA0` — уровень и витальные одного участника
	{
		// Раскладка (`network.c:10243-10254`) ЦЕЛИКОМ: @0 i32 charId · @4 u8 уровень (+94) ·
		//   @5 u8 КЛАСС (+95, им выбирается иконка меч/лук/посох) · @6 u8 (+96) ·
		//   @7 i32 maxHP (+80) · @11 i32 curHP (+76) · @15 i16 maxMP (+88) · @17 i16 curMP (+84) ·
		//   @19 i16 тайл X (+64) · @21 i16 тайл Z (+68) · @23 u8 id карты (+276; эталон сравнивает его с
		//   `getField3036(&g_pLocalPlayer)` и кладёт в +272 признак «на моей карте»).
		//   ★ Волна 1-Б (06.10.2026): @19/@21/@23 читает отрисовка отряда на мини-карте
		//     (`minimap_DrawPartyMarkers@0xB55A00`: +64/+68 — точка, +276 — фильтр карты) — подключены.
		//     @6 (+96): читателей в разобранном срезе нет — храним как есть (`Param96`).
		if (B.Num() < 24) { return; }
		const int32 Cid = AsdaBytes::RdI32(&B[0]);
		for (FAsdaPartyMember& M : Members)
		{
			if (M.CharId != Cid) { continue; }
			M.Level     = (int32)B[4];
			M.ClassCode = (int32)B[5];
			M.Param96   = (int32)B[6];
			M.MaxHp = AsdaBytes::RdI32(&B[7]);
			M.Hp    = AsdaBytes::RdI32(&B[11]);
			M.MaxMp = AsdaBytes::RdI16(&B[15]);
			M.Mp    = AsdaBytes::RdI16(&B[17]);
			M.TileX = AsdaBytes::RdI16(&B[19]);   // `*(_DWORD*)(pMember + 64) = *(__int16*)(a2 + 19)`
			M.TileZ = AsdaBytes::RdI16(&B[21]);   // `*(_DWORD*)(pMember + 68) = *(__int16*)(a2 + 21)`
			M.bPosKnown = true;
			M.MapId = (int32)B[23];               // `*(_DWORD*)(pMember + 276) = *(u8*)(a2 + 23)`
			const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
			M.bOnMyMap = LM && LM->CurrentMapId == M.MapId;   // +272
			bRosterDirty = true;
			UE_LOG(LogAsdaParty, Verbose,
				TEXT("[отряд] ←5109 «%s»: ур %d, класс %d, HP %d/%d, MP %d/%d, тайл (%d,%d) карта %d%s"),
				*M.Name, M.Level, M.ClassCode, M.Hp, M.MaxHp, M.Mp, M.MaxMp, M.TileX, M.TileZ, M.MapId,
				M.bOnMyMap ? TEXT(" (моя)") : TEXT(""));
			break;
		}
		break;
	}

	case 5104:   // ★ Волна 1-Б: `Recv_GM_PARTY_POSITION@0xF3A1F0` — тайл одного соотрядника, тело 12 б
	{
		// Раскладка (сервер `sub_451A90@GameServer.c:32884`, `sub_4C28F0(…, 5104, 12, …)`):
		//   @0 i16 partyIndex · @2 i32 charId · @6 i16 userNum (клиент НЕ читает) · @8 i16 тайл X · @10 i16 тайл Z.
		//   Эталон: `if (*a1 == Party_GetMyPartyIdx(g_CharManager)) party_SetMemberFieldsById(mgr,
		//   *(DWORD*)(a1+1), a1[4], a1[5])` → по charId записи +64/+68. Карту 5104 НЕ несёт — она из 5109.
		if (B.Num() < 12) { return; }
		const int32 Idx = AsdaBytes::RdI16(&B[0]);
		const int32 Cid = AsdaBytes::RdI32(&B[2]);
		const int32 Tx  = AsdaBytes::RdI16(&B[8]);
		const int32 Tz  = AsdaBytes::RdI16(&B[10]);
		if (Idx != PartyIndex)
		{
			UE_LOG(LogAsdaParty, Verbose, TEXT("[отряд] ←5104 чужой отряд %d (мой %d) — пропуск, как эталон"), Idx, PartyIndex);
			break;
		}
		bool bFound = false;
		for (FAsdaPartyMember& M : Members)
		{
			if (M.CharId != Cid) { continue; }
			M.TileX = Tx; M.TileZ = Tz; M.bPosKnown = true;
			bFound = true;
			break;
		}
		UE_LOG(LogAsdaParty, Verbose, TEXT("[отряд] ←5104 отряд %d charId=%d → тайл (%d,%d)%s"),
			Idx, Cid, Tx, Tz, bFound ? TEXT("") : TEXT(" — в составе не найден (эталон тоже молча)"));
		break;
	}

	case 5099:   // `Recv_GM_PARTY_LEAVEUSER_RES@0xF39C40`: @0 i32 charId, @6 char[20] имя
	{
		if (B.Num() < 26) { return; }
		const int32 CharId = AsdaBytes::RdI32(&B[0]);
		const FString Name = FAsdaLoginData::DecodeCp1251(&B[6], 20);
		if (SelfCharId != 0 && CharId == SelfCharId)
		{
			Notice(1086);                    // «ты вышел из отряда»
			bInParty = false; bIsLeader = false; PartyIndex = -1; LootRule = 0; Members.Reset();
		}
		else
		{
			Notice(1060, FString(), Name);   // «<имя> вышел»
			Members.RemoveAll([CharId](const FAsdaPartyMember& M) { return M.CharId == CharId; });
		}
		bRosterDirty = true;
		if (!bInParty) { SetRosterVisible(false); }
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] ←5099 вышел charId=%d («%s»); в отряде=%s"),
			CharId, *Name, bInParty ? TEXT("да") : TEXT("нет"));
		break;
	}

	case 5101:   // `Recv_GM_PARTY_BANISHUSER_RES@0xF3A0E0`: @0 i32 charId, @4 char[20] имя
	{
		if (B.Num() < 24) { return; }
		const int32 CharId = AsdaBytes::RdI32(&B[0]);
		const FString Name = FAsdaLoginData::DecodeCp1251(&B[4], 20);
		if (SelfCharId != 0 && CharId == SelfCharId)
		{
			Notice(1064);
			bInParty = false; bIsLeader = false; PartyIndex = -1; LootRule = 0; Members.Reset();
		}
		else
		{
			Notice(1065, FString(), Name);
			Members.RemoveAll([CharId](const FAsdaPartyMember& M) { return M.CharId == CharId; });
		}
		bRosterDirty = true;
		if (!bInParty) { SetRosterVisible(false); }
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] ←5101 исключён charId=%d («%s»)"), CharId, *Name);
		break;
	}

	case 5102:   // `Recv_GM_PARTY_PARTYMASTER_CHANGE@0xF39DA0`: @0 i32 charId нового главы
	{
		if (B.Num() < 4) { return; }
		const int32 CharId = AsdaBytes::RdI32(&B[0]);
		const bool bMe = (SelfCharId != 0 && CharId == SelfCharId);
		bIsLeader = bMe;
		bool bFound = false;   // ★ 06.10.2026: новый глава есть в составе (эталон `member+4 == a2`)
		for (FAsdaPartyMember& M : Members) { M.bLeader = (M.CharId == CharId); bFound |= M.bLeader; }
		OnPartyMasterChanged.Broadcast(CharId, bFound, bMe);   // значок главы на портрете
		Notice(bMe ? 1061 : 1062);
		bRosterDirty = true;
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] ←5102 новый глава charId=%d (%s)"),
			CharId, bMe ? TEXT("это я") : TEXT("другой"));
		break;
	}

	case 5103:   // `Recv_GM_PARTY_DESTROY@0xF39ED0`: @0 i16 partyIndex
	{
		if (B.Num() < 2) { return; }
		const int32 Idx = AsdaBytes::RdI16(&B[0]);
		if (Idx == PartyIndex)
		{
			Notice(1063);
			bInParty = false; bIsLeader = false; PartyIndex = -1; LootRule = 0; Members.Reset();
		}
		bRosterDirty = true;
		if (!bInParty) { SetRosterVisible(false); }
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] ←5103 распущен отряд %d (мой %d)"), Idx, PartyIndex);
		break;
	}

	default:
		UE_LOG(LogAsdaParty, Warning, TEXT("[отряд] ←%d: ветки нет"), Opcode);
		break;
	}

	// ★ [ПОРТ] Ревизия 1-Б М-2: окно роспуска 2256 имеет смысл только у ГЛАВЫ В ОТРЯДЕ. Потеря отряда (5099/5101/5103)
	//   или главенства (5102) гасит его — иначе окно «висело» бы над распавшимся отрядом (у эталона окно недостижимо,
	//   своего правила нет; нажатие и так безопасно — гейт `bInParty`).
	if (bDisbandOpen && (!bInParty || !bIsLeader))
	{
		SetDisbandVisible(false);
		UE_LOG(LogAsdaParty, Log, TEXT("[отряд] ←%d: я больше не глава/не в отряде — окно роспуска 2256 спрятано"), Opcode);
	}
}
