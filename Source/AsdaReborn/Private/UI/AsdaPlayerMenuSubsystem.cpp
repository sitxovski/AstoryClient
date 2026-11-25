// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerMenuSubsystem.cpp — контекстное меню по ЧУЖОМУ игроку (JUI-окно TRANTOOL_WIN 2000).
//   ВЕСЬ разбор эталона (адреса, гейты, побайтовые раскладки, доказательства мёртвых пунктов) —
//   в шапке `Public/UI/AsdaPlayerMenuSubsystem.h`. Здесь только реализация; каждое число ниже
//   сопровождено ссылкой на соответствующий пункт шапки.
#include "UI/AsdaPlayerMenuSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaWarehouseSubsystem.h"   // ★ 30.09.2026 (ревизия Л4/Л5, СРЕДН-1): свиток возврата склада (+0x5734) — гейт 10305
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"   // ★ общая точка cp1251-декода (FAsdaLoginData::DecodeCp1251)
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"   // ★★★ ЕДИНЫЙ маршрутизатор: нотисы в СИСТЕМНЫЙ канал, а не в общий чат
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaJuiSlot.h"
#include "UI/AsdaPartySubsystem.h"   // ★ окно PARTYTAKE_WIN 2250 — пункт 2001
#include "UI/AsdaClanSubsystem.h"    // ★ 27.09.2026: гейт «Друг» 12435 пункта 2003 (общая точка RefuseIfSoulmateLocked)
#include "UI/AsdaSoulmateSubsystem.h"   // ★ волна 1-А (06.10.2026): пункт 2002 → общая точка 5200 `SendProposal5200`
#include "UI/AsdaCommunityClanSubsystem.h"   // ★ 27.09.2026: 4109 код 1 → 4122+4124 при видимой вкладке клана
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: «окно обмена 2008 открыто» (`CExchange+0x2C`) — гейт 5042 меню
#include "UI/AsdaExchangeP2PSubsystem.h"  // ★ 29.09.2026 (волна О1): модель `CExchange` — заявка 5282, таймаут, приём 5283
#include "UI/AsdaOtherCharaSubsystem.h"   // ★ окно OTHERCHARA_DATA 4200 — потребитель ответа 5479+7006
#include "World/AsdaCoord.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"

#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "Camera/PlayerCameraManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPlayerMenu, Log, All);

// ★ 26.10.2026 (navattack): автопуть и атака — единая точка в AsdaPlayerController_Move.cpp (разбор у `AsdaExitCombatForAutoPath`).
bool AsdaAutoPathExitsCombatOn();                                             // ключ `asda.Move.AutoPathExitsCombat`
bool AsdaExitCombatForAutoPath(AAsdaPlayerController* PC, const TCHAR* Why);  // выход из атаки по приказу автопути игрока

namespace
{
	const FName  kWindowId(TEXT("TRANTOOL_WIN"));
	const FName  kClaimId (TEXT("PLAYER_MENU"));
	const FName  kCloserId(TEXT("PLAYER_MENU"));
	const TCHAR* kWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_TRANTOOL_WIN");
	constexpr int32 kMenuZ = 8700;                 // ниже приоритетных 8800, выше обычных окон
	constexpr float kMenuW = 158.f, kMenuH = 276.f;   // Size окна 2000 в .JUI

	// ── loc-идентификаторы подписей (CExchange_BuildUI, shop_trade.c:7258-7540) ──
	constexpr int32 kLocParty    = 6800;    // «Пригласить в отряд»
	constexpr int32 kLocSoulmate = 6801;    // «Предложить Дружбу»
	constexpr int32 kLocClan     = 6802;    // «Вступить в клан»
	constexpr int32 kLocTrade    = 6803;    // «Обмен предметами»
	constexpr int32 kLocTradeCash= 6804;    // «Обмен личными вещами»
	constexpr int32 kLocCharInfo = 5061;    // «Информация о персонаже»
	constexpr int32 kLocClose    = 5035;    // «Закрыть»
	constexpr int32 kLocWhisper  = 6921;    // «Шепот»
	constexpr int32 kLocFriend   = 12819;   // «Добавить приятеля»
	constexpr int32 kLocFollow   = 6958;    // «Следовать»
	constexpr int32 kLocBlog     = 6959;    // «Просмотреть блог»
	constexpr int32 kLocDuel     = 6963;    // «Вызвать на поединок»

	// ── нотисы (см. §3 шапки; тексты — RawAssets/Text/GameText.csv) ──
	constexpr int32 kLocNoticeTradeBusy   = 5042;   // «Открыто окно обмена. Закрой его и повтори попытку.»
	// ★ 29.09.2026 (волна О1): 5054 / 20090 / 5058 / 5059 печатает `UAsdaExchangeP2PSubsystem` (обработчики эталона
	//   `CExchange_SendRequest` и `Recv_PartyMember`), здесь их больше нет.
	constexpr int32 kLocNoticeSelfInvite  = 1093;   // «Приглашать себя нельзя»
	constexpr int32 kLocNoticeInfoInFlight= 9419;   // запрос сведений уже в полёте
	constexpr int32 kLocNoticeFollowStart = 20311;  // следование начато
	constexpr int32 kLocNoticeFollowStop  = 20310;  // следование отменено
	constexpr int32 kLocNoticeNoTarget    = 20308;  // цель не найдена
	// ⚠ 2026-09-17: НАША строка «окно ещё не портировано» (990020) отсюда УБРАНА. Её единственным
	//   потребителем был пункт 2006 «Информация о персонаже», а его окно `OTHERCHARA_DATA` 4200
	//   теперь портировано (`UAsdaOtherCharaSubsystem`). (Соулмейт 2002 портирован волной 1-А, 06.10.2026.)

	// Цвет и канал нотисов эталона: `CChat_AddTokenizedLines(chat, txt, -75279, 10000, 0)`.
	//   −75279 = 0xFFFED9F1 в ARGB (★ 29.09.2026: было 0xFFFEDB71 — ошибка перевода; 2^32 − 75279 = 0xFFFED9F1,
	//   так же `AsdaMessageSubsystem.cpp:167` и `AsdaChatSubsystem.h:81`). Константа справочная: цвет задаёт маршрут категории.
	constexpr uint32 kNoticeArgb = 0xFFFED9F1u;

	// ★ 29.09.2026 (волна О1): таймаут заявки обмена 33 с (`CExchange_SendCancelTimeout`) — в `UAsdaExchangeP2PSubsystem`.
	// У «инфо о персонаже» эталон таймаута НЕ ставит вовсе: защёлку снимает только
	//   ответ сервера, и при молчании пункт мёртв навсегда. Это ДЕФЕКТ ОРИГИНАЛА — чиним (см. CVar).
	//   (У «соулмейта» защёлки у эталона фактически нет — волна 1-А, `AsdaSoulmateSubsystem.h` R21.)
	constexpr double kRequestFixTimeoutSec = 10.0;

	// Следование: пороги эталона в «единицах/100» (`followTargetMoveTick`, math_geom.c:2318-2334).
	constexpr float  kFollowStopDist   = 6.0f;    // < 6 — стоп
	constexpr float  kFollowReorderDist= 10.0f;   // > 10 — новый приказ движения
	constexpr double kFollowOrderPeriod= 3.0;     // 0xBB8 = 3000 мс между приказами
	constexpr float  kFollowStandOff   = 150.f;   // остановка в 150 юнитах от цели
}

// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (закон fix-original-defects). У оригинала ПКМ одновременно вращает
//   камеру (`Camera_BeginRotateAnchor` на WM_RBUTTONDOWN, game_main.c:5832) И открывает меню на
//   WM_RBUTTONUP. Если отпустить кнопку после разворота камеры над игроком — меню выскакивает
//   поверх сцены, чего игрок не просил. Гасим меню, если за зажим ПКМ камера реально повернулась.
//   Вернуть поведение оригинала: asda.PlayerMenu.OpenAfterDrag 1.
static ASDA_DEV_CVAR(int32, CVarMenuOpenAfterDrag,
	TEXT("asda.PlayerMenu.OpenAfterDrag"), 0,
	TEXT("1 — меню открывается и после ПКМ-разворота камеры (как в оригинале); ")
	TEXT("0 — не открывается, если за зажим ПКМ камера повернулась (наша правка дефекта)"),
	ECVF_Default);

// ★★★ КАНАЛЫ ИТОГА «ДОБАВИТЬ ПРИЯТЕЛЯ» 4151 (05.11.2026, баг игроков «друг принял заявку — пригласившему ни слова»). [ОРИГИНАЛ]+[ИСПРАВЛЕНИЕ]
//   Эталон `uiItemNoticeDispatch@0xCE7D00` (game.c:329517-329569):
//     код 1 → `CControl_SetTooltipText(g_pChatWnd, loc 20000[ss1=имя @+12], −11514, 10000)` = ПЛАШКА 221 над чатом
//             + `CChat_AddTokenizedLines(g_pChatWnd, тот же текст, −11514, 10000, 0)` = строка журнала, ЗОЛОТАЯ;
//     коды 0/2/3/4/5 → `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, loc)` = однокнопочное ОКНО-СООБЩЕНИЕ 1500
//             (12808 / 12811 / 12812 / 12813 / 20082); иные коды → `default: return` (ничего).
//   Сервер: при согласии (4153 ответ 1) `Recv_UserFriendOK@0x537BF0` (GameServer.c:118976) уходит в БД 2215 →
//     `UPFriendAdd` (флаг 1 оба внесены · 2/3 достроена половина · 4 полон список пригласившего · 5 полон список принявшего ·
//     6 полны оба · 7 уже приятели · 0 ошибка) → ответ 2216 → `sub_406370@0x406370` (GameServer.c:2542): флаги 1/2/3 → 4151
//     КОД 1 ПРИГЛАСИВШЕМУ (запись принявшего), а при 1/3 — ещё и принявшему (запись пригласившего); прочие флаги — СЫРЫМ кодом
//     4151 только пригласившему. Отказ (4153 ответ 2) → 4151 код 5 пригласившему.
//   Прежде порт печатал ВСЕ коды одной белой строкой журнала категории Social — без плашки 221 и без окна: согласие друга
//     пригласивший пропускал (жалоба игроков), ошибки не всплывали окном.
//   [ИСПРАВЛЕНИЕ] Флаги 6 («полны оба списка») и 7 («уже приятели» — бывает, если вкладку приятелей не открывали и местной
//     проверки 12812 не было) сервер присылает сырым кодом, а эталон роняет их в `default` — молчание. С ключом показываем окно
//     12813 / 12812 тем же каналом, что соседние коды.
//   0 = прежнее поведение порта (одна строка журнала Social на всё, коды 6/7 молча).
static ASDA_DEV_CVAR(int32, CVarFriendResultChannels,
	TEXT("asda.Community.Friend.ResultChannels"), 1,
	TEXT("1 (по умолчанию) = итог 4151 эталонными каналами: код 1 — плашка 221 + золотая строка журнала (loc 20000),\n")
	TEXT("коды 0/2/3/4/5 — окно-сообщение (12808/12811/12812/12813/20082), коды 6/7 — окно 12813/12812 [ИСПРАВЛЕНИЕ].\n")
	TEXT("0 = прежнее: все коды одной строкой журнала Social, коды 6/7 молча."),
	ECVF_Default);

// ★ 08.11.2026 (пачка 16, баг 7). Канал итога 4109 (приглашение в клан). Эталон `Recv_ClanMemberLeave_Notice@0xBC9940`
//   (network.c:12025-12098) печатает ВСЕ ветки — код 1 (20051 с именем) и отказы 0/2..8 — через
//   `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, …)` = окно-сообщение ONEBUT 1500. Порт печатал их строкой журнала.
//   1 = эталонный канал (окно), 0 = прежнее поведение порта (строка журнала).
static ASDA_DEV_CVAR(int32, CVarClanInviteResultMsgBox,
	TEXT("asda.Clan.InviteResultMsgBox"), 1,
	TEXT("1 (по умолчанию) = итог 4109 (приглашение в клан) окном-сообщением, как в оригинале (network.c:12025-12098).\n")
	TEXT("0 = прежнее поведение порта: строка журнала."),
	ECVF_Default);

// Аксессор ключа `CVarFriendResultChannels` для вкладки приятелей (`AsdaCommunityFriendSubsystem.cpp`): код 6 при
//   включённом ключе показывает этот обработчик, вкладка не должна дублировать окно. Чужой ключ читается функцией, а не по имени.
bool AsdaFriendResultChannelsOn()
{
	return CVarFriendResultChannels.GetValueOnGameThread() != 0;
}

// ★★★ ГЕЙТ «НАЖАТИЕ И ОТПУСКАНИЕ ПО ОДНОМУ И ТОМУ ЖЕ ИГРОКУ» — ЭТО ОРИГИНАЛ, И ОН НЕ ТРЕБУЕТ
//   ПРЕДВАРИТЕЛЬНОГО ВЫДЕЛЕНИЯ. Эталонный `sub_C7B550@0xC7B550` сверяет цель ОТПУСКАНИЯ ПКМ с
//   целью НАЖАТИЯ ТОЙ ЖЕ ПКМ (поле CExchange+0x2700 пишется в `case 3` — «правая нажата»).
//   Это обычная проверка целостности клика, как у кнопок Windows, а не «цель должна быть выбрана».
//   Ключ оставлен для отладки: 0 — открывать по отпусканию независимо от того, где было нажатие.
static ASDA_DEV_CVAR(int32, CVarMenuRequireSamePress,
	TEXT("asda.PlayerMenu.RequireSamePress"), 1,
	TEXT("1 — меню 2000 открывается, только если НАЖАТИЕ и ОТПУСКАНИЕ ПКМ пришлись на одного и ")
	TEXT("того же игрока (эталонный sub_C7B550, умолчание); 0 — открывать по отпусканию всегда"),
	ECVF_Default);

// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (закон fix-original-defects + закон «запрос в полёте»). Эталон
//   снимает защёлку «инфо о персонаже» (otherCharaWnd[40]) ТОЛЬКО ответом
//   сервера. Сервер, промолчав, оставляет пункт мёртвым до перезахода. Ставим таймаут 10 с и
//   пишем причину в лог. asda.PlayerMenu.RequestTimeout 0 — как в оригинале (без таймаута).
//   ★ Волна 1-А (06.10.2026): у «соулмейта» (+0xC[132]) защёлка у эталона мертва — снята вовсе, ключ её не касается.
static ASDA_DEV_CVAR(int32, CVarMenuRequestTimeout,
	TEXT("asda.PlayerMenu.RequestTimeout"), 1,
	TEXT("1 — защёлка «запрос в полёте» у пункта «Информация» снимается по таймауту ")
	TEXT("10 с (наша правка дефекта оригинала); 0 — только ответом сервера, как в эталоне"),
	ECVF_Default);

// ★★★ 08.11.2026 [ОРИГИНАЛ] (пачка 16, баг 4): у эталона на пути «Информация о персонаже» нотиса 9419 НЕ БЫВАЕТ —
//   `otherCharaWnd_SetTargetUser@0xDA7640` ставит `this[40] = 0` прямо перед `antiCheat_sendItemFlagDigest@0xDA9000`, а тот
//   проверяет `this[40]` → 9419 только при ненулевом (security.c:1543). Каждый клик шлёт 5478 заново. Порт держал свою защёлку
//   `bCharInfoRequestActive` до ответа / 10 с, и повторный клик (особенно по себе, на что сервер не отвечает) давал 9419.
static ASDA_DEV_CVAR(int32, CVarMenuInfoNoLatch,
	TEXT("asda.PlayerMenu.InfoNoLatch"), 1,
	TEXT("1 (умолчание) = как в оригинале: «Информация о персонаже» каждый раз шлёт 5478, нотиса 9419 «ждём ответ сервера» нет. ")
	TEXT("0 = прежний порт: до ответа 5479 (или таймаута) повторный клик даёт 9419."),
	ECVF_Default);

// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА по решению владельца (2026-08-29): пункт «Просмотреть блог» (2063)
//   ОСТАЁТСЯ ВИДИМЫМ, хотя оригинал его прячет (`BuildUI` → `SetVisible(pListMenu2, 0)`).
//   Обработчик и опкод 6082 в оригинале живы, поэтому пункт РАБОТАЕТ, а не имитируется.
//   asda.PlayerMenu.ShowBlog 0 — спрятать, как в оригинале.
static ASDA_DEV_CVAR(int32, CVarMenuShowBlog,
	TEXT("asda.PlayerMenu.ShowBlog"), 1,
	TEXT("1 — пункт «Просмотреть блог» (2063) виден и шлёт 6082 (решение владельца); ")
	TEXT("0 — спрятан, как в оригинале"),
	ECVF_Default);

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaPlayerMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] подсистема поднята: окно TRANTOOL_WIN 2000, 12 пунктов "
		     "(9 живых, 3 мёртвых), опкоды 4108/4150/5200/5282/5286/5478/4302/6082"));
}

void UAsdaPlayerMenuSubsystem::Deinitialize()
{
	UnregisterZones();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->PopTopmost(kClaimId);
			WM->CloseWindow(kWindowId);
		}
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			if (HMenuResult.IsValid()) { Net->OnPlayerMenuResult.Remove(HMenuResult); }
		}
	}
	HMenuResult.Reset();
	MenuWidget = nullptr;
	IdText = nullptr;
	Relays.Reset();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ. Подсистема живёт на `GameInstance`, поэтому переживает и
//   разрыв связи, и выход в выбор персонажа, и травел — а всё её состояние привязано к СЕССИИ.
//   Оригиналу такой обработчик не нужен: у него разрыв связи завершает процесс
//   (`CommonWin_CommonEventFunc@0xC6EAE0` → `PostQuitMessage(0)`), см. шапку AsdaSessionReset.h.
void UAsdaPlayerMenuSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ── СЛЕДОВАНИЕ: гасим при ЛЮБОЙ причине, включая смену карты ──
	//   `FollowUserNum` — номер сессии; после перезахода или входа на другую карту тот же номер
	//   принадлежит уже ДРУГОМУ персонажу, и следование побежало бы за посторонним.
	//   Нотис 20310 здесь НЕ шлём: игрок не отменял следование, а ведущего уже не существует —
	//   сообщение «<имя>: не следовать» было бы про персонажа из прошлой сессии.
	if (bFollowActive)
	{
		UE_LOG(LogAsdaPlayerMenu, Log,
			TEXT("[меню игрока] следование за «%s» (userNum=%d) снято: %s — номер сессии больше не наш"),
			*FollowName, FollowUserNum, AsdaSessionResetReasonName(Reason));
	}
	bFollowActive = false;
	FollowUserNum = -1;
	FollowCharId  = 0;
	FollowName.Empty();
	FollowLastOrderTime = 0.0;
	bFollowMoving = false;

	// ── ЗАЩЁЛКИ «ЗАПРОС В ПОЛЁТЕ» ──
	//   Снимаем и при смене карты: соединение пересоздаётся (`EnterGameWorld`), ответа на старый запрос не будет никогда.
	//   ★ 29.09.2026 (волна О1): заявка обмена и её таймаут живут в `UAsdaExchangeP2PSubsystem` (свой сброс сессии).
	bCharInfoRequestActive = false;
	CharInfoRequestStartTime = 0.0;

	// ── ЦЕЛЬ И САМО МЕНЮ ──
	LastLeftPickUserNum = -1;
	TargetUserNum = -1;
	TargetCharId  = 0;
	TargetName.Empty();
	if (bMenuOpen) { CloseMenu(); }
}

void UAsdaPlayerMenuSubsystem::Tick(float /*DeltaTime*/)
{
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const double Now = W ? W->GetTimeSeconds() : 0.0;

	// ★ 29.09.2026 (волна О1): таймаут заявки обмена 33 с → 5286(−2) ведёт `UAsdaExchangeP2PSubsystem::Tick`.
	// ★ Наши таймауты на защёлках, у которых в оригинале таймаута нет (см. CVar выше).
	if (CVarMenuRequestTimeout.GetValueOnGameThread() != 0)
	{
		if (bCharInfoRequestActive && Now - CharInfoRequestStartTime > kRequestFixTimeoutSec)
		{
			bCharInfoRequestActive = false;
			UE_LOG(LogAsdaPlayerMenu, Warning,
				TEXT("[меню игрока] ответ 5479 на «Информация о персонаже» не пришёл за %.0f с — "
				     "защёлка снята (в оригинале пункт остался бы мёртвым до перезахода)"), kRequestFixTimeoutSec);
		}
	}

	FollowTick();

	// Подписка на ответы — ЛЕНИВАЯ и ровно одна: сеть поднимается позже подсистем UI.
	if (!bNetBound)
	{
		if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			HMenuResult = Net->OnPlayerMenuResult.AddUObject(this, &UAsdaPlayerMenuSubsystem::OnMenuResult);
			bNetBound = true;
			UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] подписка на ответы 4151/5283/5479/5201/6083 установлена"));
		}
	}

	// Общий реестр «закрыть окно по началу движения» — заводим ОДИН раз, когда оконная подсистема
	//   уже поднялась (эталон: `if (g_bExchangeWndOpen == 1) uiDialog_ClearAndHide(...)`,
	//   input.c:869 и ui_core.c:1260). Копию закрывателя НЕ заводим (закон о едином реестре).
	if (!bCloserBound)
	{
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->RegisterNpcWindowCloser(kCloserId, FAsdaNpcWindowCloser::CreateWeakLambda(this,
				[this]()
				{
					if (bMenuOpen)
					{
						UE_LOG(LogAsdaPlayerMenu, Verbose, TEXT("[меню игрока] закрыто началом движения игрока"));
						CloseMenu();
					}
					// ★★★ ЗДЕСЬ СЛЕДОВАНИЕ НЕ ОТМЕНЯЕТСЯ. Эта точка срабатывает на ЛЮБОЕ начало
					//   движения — а следование само двигает игрока, поэтому прежняя отмена рвала
					//   его на первом же шаге (дефект порта, найден владельцем 2026-08-29:
					//   «следовать работает криво»). Эталон снимает следование ТОЛЬКО из веток
					//   РУЧНОГО ввода (input.c:190/304/389/572) — у нас это клик по миру.
				}));
			bCloserBound = true;
		}
	}
}

// ═══════════════════════════════ ВИДЖЕТ ════════════════════════════════════════════════════════
// ★ СВЕДЕНО (`fix_helpers_dedup.md`): тонкий форвард к общей точке `AsdaUi::FindNodeExact`
//   (была точная копия `GetWidgetFromName`).
static UWidget* FindNode(const UUserWidget* Host, const TCHAR* Exact)
{
	return AsdaUi::FindNodeExact(Host, Exact);
}

bool UAsdaPlayerMenuSubsystem::EnsureMenu()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbp, kMenuZ).Ensure(GetGameInstance(), MenuWidget, bMenuReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.PopTopmost(kClaimId);
			WM.CloseWindow(kWindowId);
			MenuWidget = nullptr; bMenuReady = false; bMenuOpen = false;
		},
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZones();
		},
		[]()
		{
			UE_LOG(LogAsdaPlayerMenu, Error, TEXT("[меню игрока] WBP_TRANTOOL_WIN не загрузился (%s)"), kWbp);
		},
		[this](UAsdaWindowSubsystem&)
		{
			// Скрытие окна (шаг каркаса): .JUI Visible="0" — окно ждёт ПКМ
			bMenuOpen = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R);
}

void UAsdaPlayerMenuSubsystem::SetupControls()
{
	bMenuReady = false;
	if (!MenuWidget) { return; }
	Relays.Reset();

	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	auto Loc = [Txt](int32 Id) -> FString { return Txt ? Txt->GetTextById(Id) : FString(); };

	IdText = Cast<UTextBlock>(FindNode(MenuWidget, TEXT("TRANTOOL_IDTEXT")));

	// Реле: у UButton::OnClicked нет отправителя, поэтому индекс кладём в мостик.
	auto Bind = [this](const TCHAR* Node, const TCHAR* CapNode, int32 IdNum, const FString& Caption,
		TFunction<void()>&& Fn) -> bool
	{
		UButton* B = Cast<UButton>(FindNode(MenuWidget, Node));
		if (!B)
		{
			UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] узла «%s» (idnum %d) нет в WBP"), Node, IdNum);
			return false;
		}
		// ★★★ РАСПЛЮЩЕННАЯ ИЕРАРХИЯ JUI: подпись `<idc>_Text` — НЕ ребёнок кнопки, а её СОСЕД в
		//   корневом канвасе (доказано дампом ассета: 41 узел, все с POINT-анкором, подписи —
		//   отдельные узлы). Значит ВИДИМОСТЬ надо ставить ОБОИМ. Из-за пропуска этого пункт
		//   «Вызвать на поединок» (2070, в .JUI Visible="0") выходил ПУСТОЙ КНОПКОЙ: кнопку
		//   показывали, а подпись оставалась Collapsed из данных.
		//   Подпись — `SelfHitTestInvisible`, иначе она съедала бы клики по кнопке
		//   (закон selfhittest-vs-hittest-invisible).
		if (UTextBlock* T = Cast<UTextBlock>(FindNode(MenuWidget, CapNode)))
		{
			if (!Caption.IsEmpty()) { T->SetText(FText::FromString(Caption)); }
			T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			UE_LOG(LogAsdaPlayerMenu, Warning,
				TEXT("[меню игрока] узла подписи «%s» в WBP НЕТ — кнопка %d останется без текста"),
				CapNode, IdNum);
		}
		B->SetVisibility(ESlateVisibility::Visible);
		AsdaUi::BindButton(this, B, IdNum, [Fn = MoveTemp(Fn)](int32) { Fn(); }, Relays);   // ★ U20: реле клика — общая точка
		return true;
	};

	// ── ЖИВЫЕ ПУНКТЫ. Порядок и loc — ровно как в `CExchange_BuildUI` (см. §2 шапки). ──
	int32 Live = 0;
	Live += Bind(TEXT("TRANTOOL_BUT01"), TEXT("TRANTOOL_BUT01_Text"), 2001, Loc(kLocParty),     [this]{ ActionPartyInvite(); }) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT03"), TEXT("TRANTOOL_BUT03_Text"), 2003, Loc(kLocClan),      [this]{ ActionClanInvite();  }) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT09"), TEXT("TRANTOOL_BUT09_Text"), 2055, Loc(kLocFriend),    [this]{ ActionFriendAdd();   }) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT02"), TEXT("TRANTOOL_BUT02_Text"), 2002, Loc(kLocSoulmate),  [this]{ ActionSoulmate();    }) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT04"), TEXT("TRANTOOL_BUT04_Text"), 2004, Loc(kLocTrade),     [this]{ ActionExchange(false);}) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT05"), TEXT("TRANTOOL_BUT05_Text"), 2005, Loc(kLocTradeCash), [this]{ ActionExchange(true); }) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT06"), TEXT("TRANTOOL_BUT06_Text"), 2006, Loc(kLocCharInfo),  [this]{ ActionCharInfo();    }) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT08"), TEXT("TRANTOOL_BUT08_Text"), 2054, Loc(kLocWhisper),   [this]{ ActionWhisper();     }) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT11"), TEXT("TRANTOOL_BUT11_Text"), 2062, Loc(kLocFollow),    [this]{ ActionFollow();      }) ? 1 : 0;
	// ★ 2070 в .JUI Visible="0", но BuildUI делает SetVisible(1) (shop_trade.c:7351) ⇒ ЖИВАЯ.
	Live += Bind(TEXT("TRANTOOL_BUT13"), TEXT("TRANTOOL_BUT13_Text"), 2070, Loc(kLocDuel),      [this]{ ActionDuel();        }) ? 1 : 0;
	Live += Bind(TEXT("TRANTOOL_BUT07"), TEXT("TRANTOOL_BUT07_Text"), 2052, Loc(kLocClose),     [this]{ CloseMenu();         }) ? 1 : 0;

	// ── МЁРТВЫЕ ПУНКТЫ (доказательства — §4 шапки). Не оживляем и не удаляем: гасим. ──
	auto MuteDead = [this, &Loc](const TCHAR* Node, const TCHAR* CapNode, int32 IdNum, int32 LocId, const TCHAR* Why)
	{
		// Подпись всё равно ставим там, где её ставит эталон (2053 и 2063 её получают в BuildUI):
		//   если кнопку когда-нибудь включат консолью, она не будет корейской.
		if (LocId > 0)
		{
			if (UTextBlock* T = Cast<UTextBlock>(FindNode(MenuWidget, CapNode)))
			{
				const FString S = Loc(LocId);
				if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
			}
		}
		// ★ Гасим ОБА узла: кнопку и подпись-соседа. Гасилась только кнопка — и текст мёртвого
		//   пункта продолжал висеть «серой строкой» (замечание владельца по пункту «Блог»).
		if (UWidget* B = FindNode(MenuWidget, Node)) { B->SetVisibility(ESlateVisibility::Collapsed); }
		if (UWidget* T2 = FindNode(MenuWidget, CapNode)) { T2->SetVisibility(ESlateVisibility::Collapsed); }
		UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] пункт %d скрыт (кнопка + подпись): %s"), IdNum, Why);
	};
	MuteDead(TEXT("TRANTOOL_BUTDISABLE"), TEXT("TRANTOOL_BUTDISABLE_Text"), 2053, kLocSoulmate,
		TEXT("в .JUI Visible=0, CControl_SetVisible(pBtnDisable,1) в эталоне НЕ вызывается, обработчика msgId 2053 нет"));
	MuteDead(TEXT("TRANTOOL_BUT10"), TEXT("TRANTOOL_BUT10_Text"), 2060, 0,
		TEXT("FindControlByID(2060) в эталоне не вызывается ни разу, обработчика нет"));
	// ★★★ «Просмотреть блог» (2063) — ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА, решение владельца
	//   (2026-08-29): «я не говорил убирать блог, это интересно, нужно будет реализовать потом».
	//   В ОРИГИНАЛЕ пункт НЕДОСТИЖИМ: `CExchange_BuildUI` делает `CControl_SetVisible(pListMenu2, 0)`
	//   (shop_trade.c:7345) и больше её никто не показывает — при этом ОБРАБОТЧИК msgId 2063 жив и
	//   шлёт опкод 6082. Мы оставляем кнопку ВИДИМОЙ и рабочей: 6082 уходит, ответ 6083 логируется.
	//   Разбор «что это за пункт и что нужно для реализации» — §10 шапки и отчёт.
	//   Вернуть поведение оригинала (спрятать пункт): asda.PlayerMenu.ShowBlog 0.
	if (CVarMenuShowBlog.GetValueOnGameThread() != 0)
	{
		Live += Bind(TEXT("TRANTOOL_BUT12"), TEXT("TRANTOOL_BUT12_Text"), 2063, Loc(kLocBlog), [this]{ ActionBlog(); }) ? 1 : 0;
	}
	else
	{
		MuteDead(TEXT("TRANTOOL_BUT12"), TEXT("TRANTOOL_BUT12_Text"), 2063, kLocBlog,
			TEXT("asda.PlayerMenu.ShowBlog 0 — как в оригинале: BuildUI делает SetVisible(pListMenu2,0)"));
	}

	bMenuReady = (Live >= 10);
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] окно 2000 разобрано: живых кнопок %d, заголовок 2007=%s, "
		     "скрыто мёртвых: 2053 и 2060%s"),
		Live, IdText ? TEXT("да") : TEXT("НЕТ"),
		CVarMenuShowBlog.GetValueOnGameThread() != 0 ? TEXT(" (блог 2063 показан по решению владельца)")
		                                             : TEXT(" и 2063"));
}

void UAsdaPlayerMenuSubsystem::RegisterZones()
{
	if (bZonesOn) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	WM->RegisterUiZone(kWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) { return GetMenuRect(Mn, Mx); }));
	bZonesOn = true;
}

void UAsdaPlayerMenuSubsystem::UnregisterZones()
{
	if (!bZonesOn) { return; }
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
	}
	bZonesOn = false;
}

// Прямоугольник по ЖИВОЙ геометрии узлов (абсолютные экранные координаты).
//   ★ Закон jui-flattened-hierarchy-geometry: иерархия JUI у импортёра РАСПЛЮЩЕНА — кнопки,
//   заголовок и планы рамки лежат СОСЕДЯМИ в корневом канвасе, а не внутри окна. Поэтому и зону,
//   и перемещение считаем по НАБОРУ узлов, а не по одному «корню окна».
// ★ Волна 3: накопитель прямоугольника перенесён в `FAsdaPopupMenu::AccumRect` (использовался только корпусом меню).

bool UAsdaPlayerMenuSubsystem::GetMenuRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bMenuOpen || !MenuWidget) { return false; }
	// Корпус меню = 9 планов рамки 3×3 скина Popup_SmWin. Объединять ВСЕ узлы нельзя — зону
	//   раздувают скрытые кнопки (эту граблю уже ловили на чате и на меню 28000).
	static const TCHAR* kBody[] = {
		TEXT("TRANTOOL_WIN_0_0"), TEXT("TRANTOOL_WIN_0_1"), TEXT("TRANTOOL_WIN_0_2"),
		TEXT("TRANTOOL_WIN_0_3"), TEXT("TRANTOOL_WIN_0_4"), TEXT("TRANTOOL_WIN_0_5"),
		TEXT("TRANTOOL_WIN_0_6"), TEXT("TRANTOOL_WIN_0_7"), TEXT("TRANTOOL_WIN_0_8") };
	// ★ Волна 3: корпус меню — общая точка `FAsdaPopupMenu::BodyRect`; фолбэк на прямоугольник всего виджета — как было у меню 2000.
	return FAsdaPopupMenu::BodyRect(MenuWidget, MakeArrayView(kBody), /*bFallbackToWidgetRect*/ true, OutMin, OutMax);
}

// Текущий левый-верхний угол = минимум offsets ПРЯМЫХ детей корневого канваса (у всех узлов
//   окна 2000 в .JUI POINT-анкор «5 0 0», поэтому Left/Top — абсолютные базовые px).
bool UAsdaPlayerMenuSubsystem::GetMenuTopLeft(FVector2D& Out) const
{
	// ★ Волна 3: угол меню — общая точка `FAsdaPopupMenu::GetTopLeft` (меню 2000 ≡ 28000, тело перенесено без изменений).
	return FAsdaPopupMenu::GetTopLeft(MenuWidget, Out);
}

// Эталон `CControl_SetField74_75(this[2500], x, y)` двигает КОРНЕВОЙ контрол — то есть всё окно.
//   У нас «корень» = набор прямых детей канваса ⇒ сдвигаем их ВСЕ на одну дельту. Двигать только
//   плашку рамки нельзя: кнопки и ручка перетаскивания остались бы на месте.
void UAsdaPlayerMenuSubsystem::PlaceMenuAt(const FVector2D& Pos)
{
	// ★ Волна 3: перенос меню — общая точка `FAsdaPopupMenu::PlaceAt` (двигает все дети корневой канвы).
	FAsdaPopupMenu::PlaceAt(MenuWidget, Pos);
}

// ═══════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══════════════════════════════════════════
void UAsdaPlayerMenuSubsystem::NotifyRightPress(int32 UserNum)
{
	// 1:1 `set_o9C0(g_pExchangeWnd, userIdx)` / `set_o9C0(..., -1)` (game_main.c:9386/9391),
	//   ветка `case 3` = ПКМ НАЖАТА (см. §1 шапки: байт [305] == 1).
	LastLeftPickUserNum = UserNum;
	UE_LOG(LogAsdaPlayerMenu, Verbose,
		TEXT("[меню игрока] ПКМ-нажатие: под курсором userNum=%d (эталонный CExchange+0x2700)"), UserNum);
}

void UAsdaPlayerMenuSubsystem::NotePickedUser(int32 UserIdx)
{
	// input.c:1306-1307: `closestDist[2229] = Target_GetSelectedUserIdx(pickBuf)` — только при удачном пике (иначе `return 0`
	//   раньше записи). Поле читает фильтр «личных вещей» окна обмена (коды 335…337 / 418…420).
	PickedUserIdx = UserIdx;
	UE_LOG(LogAsdaPlayerMenu, Verbose, TEXT("[меню игрока] ПКМ-пик: g_nPickedUserIdx = %d"), UserIdx);
}

bool UAsdaPlayerMenuSubsystem::NotifyRightRelease(int32 UserNum, bool bIsSelf)
{
	// Промах по игроку — эталон гасит меню (`uiDialog_ClearAndHide`, game_main.c:9434).
	if (UserNum < 0)
	{
		if (bMenuOpen) { CloseMenu(); }
		return false;
	}
	// ★ ГЕЙТ «НЕ Я САМ»: `!pickedChar->effectSlots[112].field_40` (доказательство — §1 шапки).
	if (bIsSelf)
	{
		UE_LOG(LogAsdaPlayerMenu, Verbose,
			TEXT("[меню игрока] ПКМ по СЕБЕ — меню не открывается (эталонный гейт effectSlots[112].field_40)"));
		if (bMenuOpen) { CloseMenu(); }
		return false;
	}
	// ★ ГЕЙТ «ТОТ ЖЕ ИГРОК»: `sub_C7B550` — userNum обязан совпасть с последним ЛКМ-пиком.
	if (CVarMenuRequireSamePress.GetValueOnGameThread() != 0)
	{
		if (LastLeftPickUserNum < 0 || LastLeftPickUserNum != UserNum)
		{
			UE_LOG(LogAsdaPlayerMenu, Verbose,
				TEXT("[меню игрока] ПКМ отпущена над userNum=%d, а НАЖАТА была над %d — эталонный гейт "
				     "sub_C7B550 (нажатие и отпускание должны прийтись на ОДНОГО игрока) не пропускает; "
				     "asda.PlayerMenu.RequireSamePress 0 снимает"),
				UserNum, LastLeftPickUserNum);
			return false;
		}
	}
	// ★ ГЕЙТ «заявка обмена в полёте»: `cPopup_ShowClampedToScreen` при g_bPartyInviteFlag печатает
	//   нотис 5042 И НЕ ПОКАЗЫВАЕТ окно.
	// ★★★ ИСПРАВЛЕНО 2026-08-29 (замечание владельца: «в чат пишется "Открыто окно обмена",
	//   хотя окно не открыто»). Эталонный флаг `CExchange+0x2C` (в прежнем RE назван
	//   `g_bPartyInviteFlag`) означает «ОТКРЫТО ОКНО ОБМЕНА `TRAN_WIN` 2008» — именно об этом и
	//   говорит его нотис 5042 «Открыто окно обмена. Закрой его и повтори попытку.».
	//   Это НЕ то же, что «заявка отправлена и ждёт ответа» — у последней СВОЙ нотис
	//   **5054** «Предложение обмена уже сделано (через 30 секунд оно утратит силу)»
	//   (`CExchange_SendRequest`, shop_trade.c:7906). Окна 2008 в порту нет ⇒ 5042 не печатаем
	//   вовсе, а меню при висящей заявке ОТКРЫВАЕТСЯ — как в оригинале: там заявка в полёте
	//   открытию меню не мешает, мешает только открытое окно обмена.
	//   Сам отказ повторной заявки (нотис 5054) остался в ActionExchange — это тоже оригинал.
	// ★ 29.09.2026 (волна 0 торговли, ПД-2): гейт подключён к общей точке «окно обмена открыто»
	//   (`UAsdaTradeStateSubsystem::IsExchangeWindowOpen` = `CExchange+0x2C`). [ОРИГИНАЛ] `cPopup_ShowClampedToScreen@0xC75690`
	//   (ui_core.c:35112-35115, @0xC756AB): 5042 в чат (−75279, 10 с) и меню НЕ показывается. Пока окна 2008 нет — false.
	// ★ 29.09.2026 (волна О1): `if (!g_bExchangeTimerActive) set_o9C2(g_pExchangeWnd, userNum)` (game_main.c:9423-9424) —
	//   userNum для таймаута заявки 5286(−2) пишется ДО показа меню (и до гейта 5042 внутри показа).
	if (UAsdaExchangeP2PSubsystem* P2P = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr)
	{
		P2P->NoteMenuTargetForTimeout(UserNum);
	}
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()))
	{
		NoticeColored(kLocNoticeTradeBusy, AsdaNoticeColor::Pink);   // 5042, −75279 (ревизия С-1)
		UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] окно обмена 2008 открыто — меню не показано, нотис 5042 (cPopup_ShowClampedToScreen)"));
		// ★ Ревизия М-4: эталон в этой ветке только НЕ показывает меню; лишний `CloseMenu()` убран (меню при открытом
		//   окне обмена открытым быть и не может).
		return false;
	}

	if (!EnsureMenu()) { return false; }

	// Снимок цели: userNum (+0x4C6), имя (+0x4C8) и charId — их пишет `Input_PickEntityAltClick`
	//   (input.c:1304-1306: `g_nSelectedTargetUserIdx = Target_GetSelectedUserIdx(pick);
	//   memcpy(g_szSelectedTargetName, pick + 528, 0x14)` — +528 слов = +1056 байт = имя).
	TargetUserNum = UserNum;
	TargetName.Empty();
	TargetCharId = 0;
	if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		int32 Cls = 0, Lvl = 0, Clan = -1;
		WS->GetNetPlayerInfo(UserNum, TargetName, Cls, Lvl, Clan);
		TargetCharId = WS->GetNetPlayerCharId(UserNum);
	}
	if (TargetName.IsEmpty())
	{
		UE_LOG(LogAsdaPlayerMenu, Warning,
			TEXT("[меню игрока] имя игрока userNum=%d в реестре не найдено — заголовок 2007 останется пустым"),
			UserNum);
	}
	OpenMenuAtCursor();
	return true;
}

void UAsdaPlayerMenuSubsystem::OpenMenuAtCursor()
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;

	// Заголовок 2007 := имя цели (`CControl_SetText(this[2512], this + 1224)`).
	if (IdText) { IdText->SetText(FText::FromString(TargetName)); }

	// Позиция: РОВНО точка курсора, без смещения (эталон не прибавляет ничего, в отличие от
	//   меню 28000 и 80000). Кламп в экран — ТОЖЕ эталонный (`cPopup_ShowClampedToScreen`).
	FVector2D Cursor(0.f, 0.f);
	AsdaUi::CursorInViewportUnits(W, Cursor);
	// ★ Дедуп №2, В1-B (U16, 11.10.2026): кламп — общая точка `AsdaUi::ClampPopupToViewport` (эталонное «сдвиг на перелёт, затем
	//   не меньше 0» математически равно клампу в [0, экран − размер]; доказательство — `asda.Dedup.W1BSelfTest`).
	const FVector2D Pos = AsdaUi::ClampPopupToViewport(Cursor, FVector2D(kMenuW, kMenuH), AsdaUi::ViewportSizeUnits(W));
	PlaceMenuAt(Pos);

	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, true);
		WM->BringWindowToFront(kWindowId);
		FAsdaUiZoneRect D;
		D.BindWeakLambda(this, [this](FVector2D& Mn, FVector2D& Mx) { return GetMenuRect(Mn, Mx); });
		WM->PushTopmost(kClaimId, kWindowId, MoveTemp(D));
		// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: щелчок мимо гасит меню; разбор — `AsdaWindowSubsystem_TopPress.cpp`.
		WM->RegisterClickAwayMenu(kClaimId, FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseMenu(); }));
	}
	bMenuOpen = true;
	++CntOpened;
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] 2000 открыто по «%s» (userNum=%d charId=%d) в (%.0f,%.0f)"),
		*TargetName, TargetUserNum, TargetCharId, Pos.X, Pos.Y);
}

void UAsdaPlayerMenuSubsystem::CloseMenu()
{
	if (!bMenuOpen) { return; }
	bMenuOpen = false;
	FAsdaPopupMenu::HideAndPopTopmost(GetGameInstance(), kWindowId, kClaimId);   // ★ волна 3: скрыть → вернуть верхний слой
	UE_LOG(LogAsdaPlayerMenu, Verbose, TEXT("[меню игрока] 2000 закрыто (uiDialog_ClearAndHide)"));
}

// ═══════════════════════════════ ОБЩЕЕ ═════════════════════════════════════════════════════════
// ★★★ ВСЕ нотисы меню идут через ЕДИНЫЙ маршрутизатор `UAsdaMessageSubsystem`, а не строкой в
//   ленту чата. Раньше здесь стоял `AddChatLineRaw(6, …)` — и служебные сообщения («Открыто окно
//   обмена», «Ты предлагаешь обмен…», «…: следовать») сыпались в ОБЩИЙ чат, что и увидел владелец.
//   Маршрут категории задан ДАННЫМИ (`DT_MessageRoutes`): Social / Trade / Duel / MoveRefused
//   имеют Targets=1 = СИСТЕМНЫЙ ЖУРНАЛ. Менять маршрут — правкой таблицы + `asda.Msg.Reload`.
// ★★★ Слот `[ss1]` заполняет ТОЛЬКО общая точка. Если в строке ЕСТЬ маркер, подстановка идёт
//   ВСЕГДА, даже с пустым именем: иначе на экран уходит сырой «[ss1]» — ровно это владелец увидел
//   в строке 20310 «[ss1]: не следовать».
// ★ Ревизия С-1 волны 0 торговли (29.09.2026): нотис ЦВЕТОМ ОБРАБОТЧИКА эталона — `CChat_AddTokenizedLines(chat, txt, цвет,
//   10000, 0)`. Нужен там, где цвет обработчика расходится с маршрутом категории (5042: −75279 розовый, а категория Trade —
//   янтарный). След самотеста (`NoticeSeq`/`LastNoticeLoc`) — тот же, что у `Notice`.
void UAsdaPlayerMenuSubsystem::NoticeColored(int32 LocId, FColor Color) const
{
	UGameInstance* GI = GetGameInstance();
	++NoticeSeq; LastNoticeLoc = LocId; LastNoticeSlot.Reset();
	const bool bOk = AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, AsdaNotice::LocRaw(GI, LocId), Color, 10000.f);
	UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] нотис %d цветом %s: %s"), LocId, *Color.ToHex(), bOk ? TEXT("выведен") : TEXT("НЕ выведен"));
}

void UAsdaPlayerMenuSubsystem::Notice(int32 LocId, const FString& NameSlot) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (!Txt) { return; }
	++NoticeSeq; LastNoticeLoc = LocId; LastNoticeSlot = NameSlot;   // ★ 27.09.2026: след для самотеста
	const FString Raw = Txt->GetTextById(LocId);
	const bool bHasMarkers = UAsdaTextSubsystem::HasNoticeMarkers(Raw);
	const bool bFormat = bHasMarkers || !NameSlot.IsEmpty();
	const EAsdaMsgCategory Cat = CategoryForNotice(LocId);
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	if (bFormat) { AsdaNotice::ChatFmt(GI, Cat, LocId, { NameSlot }, {}); }
	else         { AsdaNotice::Chat(GI, Cat, LocId); }
	// Лог — всегда: молчаливых выходов быть не должно.
	const FString Line = bFormat ? Txt->FormatById(LocId, { NameSlot }, {}) : Raw;
	UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] нотис %d (категория %d, слот «%s»): %s"),
		LocId, (int32)Cat, *NameSlot, *Line);
}

// Категория нотиса → маршрут из `DT_MessageRoutes`. Таблицей, а не хардкодом в месте вызова.
EAsdaMsgCategory UAsdaPlayerMenuSubsystem::CategoryForNotice(int32 LocId)
{
	switch (LocId)
	{
	// Торговля: обмен игрок-игрок и все его отказы.
	case 5032: case 5033: case 5041: case 5042: case 5043: case 5054:
	case 5058: case 5059: case 5060: case 10307: case 13016: case 13104: case 20090:
		return EAsdaMsgCategory::Trade;
	// Отказ движения/следования.
	case 10305: case 107022: case 20308:
		return EAsdaMsgCategory::MoveRefused;
	// Всё остальное меню — социальное (отряд, клан, друзья, соулмейт, следование, сведения).
	default:
		return EAsdaMsgCategory::Social;
	}
}

AActor* UAsdaPlayerMenuSubsystem::ResolveTargetActor() const
{
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return (WS && TargetUserNum >= 0) ? WS->GetPlayerActorByUserNum(TargetUserNum) : nullptr;
}

// ═══════════════════════════════ ПУНКТЫ МЕНЮ ═══════════════════════════════════════════════════
// 2001 — «Пригласить в отряд». Пакета НЕТ: эталон открывает локальный диалог PARTYTAKE_WIN 2250.
void UAsdaPlayerMenuSubsystem::ActionPartyInvite()
{
	const FString Name = TargetName;
	const int32   User = TargetUserNum;
	CloseMenu();   // `uiDialog_ClearAndHide` стоит на ВСЕХ выходах ветки 0x7D1
	// ★★★ ПОДВЯЗКА К ОКНУ `PARTYTAKE_WIN` 2250 (2026-08-29). Пакета на этом шаге нет и в
	//   эталоне: ветка 0x7D1 гоняет локальные гейты и зовёт `partyUi_ShowInviteRequest@0xB99630`.
	//   ВСЕ гейты (1093/1047/1056/1045/1089) живут внутри `UAsdaPartySubsystem::OpenInviteWindow`
	//   — там же, где состояние отряда, чтобы не разъезжаться.
	//
	//   ★★★ ДЕФЕКТ ОРИГИНАЛА D1, ИСПРАВЛЕН ЗДЕСЬ (перепроверен 2026-08-29).
	//   Первая строка ветки 0x7D1 — `if (g_nPartyUiMode) goto LABEL_31;` (`shop_trade.c:6549`).
	//   Это развилка ИСТОЧНИКА ЦЕЛИ, и у неё два входа с разной судьбой:
	//     • ЭМОЦИИ / ГОРЯЧАЯ КЛАВИША → `UI_DispatchTargetContextMenu@0xC532D0`
	//       (`ui_core.c:32108-32116`): сам пишет `g_nPartyUiMode = 0`, свой гейт
	//       `!g_nInputControlState`, затем `exchangeUiOnCommand(…, 0x7D1, 0)` ⇒ РАБОТАЕТ;
	//     • КНОПКА ЭТОГО МЕНЮ → `exchangeWnd_OnMessage@0xC6F400`, сообщение 0x10401 →
	//       `exchangeUiOnCommand(this, IDNum контрола, …)` — режим НЕ трогает. А открыть
	//       меню можно только правым кликом, и `MainWndProc@0x9841A0` на WM_RBUTTONDOWN
	//       (`case 0x204`, `game_main.c:5830`) безусловно пишет `g_nPartyUiMode = 11`
	//       ⇒ к моменту клика по пункту режим ВСЕГДА 11 и ветка молча гаснет.
	//   ДОКАЗАНО ДВУМЯ МЕТОДАМИ: (1) по потоку — у `UI_DispatchTargetContextMenu` ровно пять
	//   вызывающих во ВСЁМ декомпиле (`EmoteWnd_WndProc`, `hotkeyDispatchAction`,
	//   `input_HotkeyDispatchWithContextMenu`, `uiDispatchTargetMenuByRange`,
	//   `uiOpenWindowDispatch`) — все эмоции/клавиши, ни одного из окна 2000;
	//   (2) сплошной байтовый скан образа по адресу глобала 0x02902F64 — ровно пять ссылок
	//   (0x9851CF mov 0Bh · 0xB980DB cmp 0 · 0xB980FB mov 0Bh · 0xC53DAD mov 0 · 0xC6F8BF cmp 0),
	//   и единственный обнуляющий сайт 0xC53DAD лежит внутри 0xC532D0.
	//   Мы делаем то же, что работающий путь: заходим в ветку с нулевым режимом.
	//   Откат — `asda.Party.FixContextMenuInvite 0` (кнопка снова молчит, как в оригинале).
	UAsdaPartySubsystem* Party = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	if (!Party)
	{
		UE_LOG(LogAsdaPlayerMenu, Error,
			TEXT("[меню игрока] 2001 «Пригласить в отряд» по «%s»: подсистема отряда недоступна"), *Name);
		return;
	}
	const bool bOpened = Party->OpenInviteWindow(User, Name);
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] 2001 «Пригласить в отряд» по «%s» (userNum=%d): окно 2250 %s"),
		*Name, User, bOpened ? TEXT("открыто") : TEXT("НЕ открыто — причина нотисом выше"));
}

// 2003 — «Вступить в клан» → 4108.
void UAsdaPlayerMenuSubsystem::ActionClanInvite()
{
	const int32 U = TargetUserNum;
	const FString Name = TargetName;
	CloseMenu();
	UGameInstance* GI = GetGameInstance();
	// ★★★ 27.09.2026 — ГЕЙТ «ДРУГ» ВОССТАНОВЛЕН [ОРИГИНАЛ] (`Reports/owner_0927/friend_gate_12435.md`).
	//   Эталон `partySendInviteAuth@0xBC96F0` (★ МИСНЕЙМ: это клан-инвайт 4108, не отряд; social.c:3430-3436):
	//   `if (a2) { if (char->gap_8BE[3]) { itemBuyPanelRefreshOrHide(g_pItemTooltipUI); нотис 12435 (0, −1); } else … }`.
	//   Снят 29.08 справедливо (тогда он смотрел на ЧУЖОЕ поле — звание), но источник НАСТОЯЩЕГО поля с 03.09 есть:
	//   `char+0x8C1` = SoulState (6152 @12, `UAsdaNetSubsystem::SelfSoulSilhouette`) — общая точка
	//   `UAsdaClanSubsystem::RefuseIfSoulmateLocked`. Гейт стоит ДО проверки соединения: у эталона её нет вовсе.
	//   `a2` эталона — выбранный персонаж (`pExchange->gap_26DC[48]`, shop_trade.c:6611); у порта это цель меню.
	if (U < 0) { return; }
	if (UAsdaClanSubsystem* Clan = GI ? GI->GetSubsystem<UAsdaClanSubsystem>() : nullptr)
	{
		if (Clan->RefuseIfSoulmateLocked(/*bDropTopMsgBox*/true, TEXT("меню игрока 2003 → 4108 (social.c:3434)"))) { return; }
	}
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->bInGameConnected)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] 4108 не отправлен: нет соединения"));
		return;
	}
	// ★★★ ИСПРАВЛЕН ДЕФЕКТ ПОРТА 2026-08-29 (владелец: «Вступить в клан не отвечает»).
	//   Здесь стоял гейт `if (Net->SelfClanRank != 0) → нотис 12435`, и он ГЛУШИЛ пункт у любого,
	//   кто состоит в клане. Поле эталона `localPlayer->gap_8BE[3]` (char+2241) — ЭТО НЕ ЗВАНИЕ
	//   В КЛАНЕ (звание живёт в char+2427, `CCharacter+0x97B`). Разбор поля по всем 12 его
	//   использованиям: `party_UpdateMemberRecord` (social.c:991) кладёт в него байт +12 записи
	//   участника; при значении 1 персонажу включается силуэт `silhoutte_tex_white.TEX`
	//   (social.c:1003); при 2 клан-чат красится другим цветом (network.c:10391/10481/10620);
	//   при НЕНУЛЕВОМ значении отправка клан-чата получает префикс loc **6937 «(Друг)»**
	//   (chat.c:5670). ⇒ это признак «я в связке ПРИЯТЕЛЕЙ (Friend/Buddy)», и нотис 12435
	//   «Друзьям этот навык недоступен» читается ровно так.
	//   Системы приятелей в порту НЕТ ⇒ источника значения нет ⇒ гейт не проверяем и НЕ блокируем
	//   (запись в реестр отложенного). Раньше он блокировал по чужому полю — это и был дефект.
	//   ★ Поправка 27.09.2026: «приятели» — неверное прочтение. Поле = SoulState (1 «душа», 2 одержимость соулмейтом),
	//   источник 6152 @12 появился 03.09 (possess) — гейт восстановлен выше по НАСТОЯЩЕМУ полю.
	const bool bOk = Net->SendClanInvite4108(U);
	if (bOk) { ++CntSent; }
	UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 2003 клан-инвайт «%s» (userNum=%d) -> %s"),
		*Name, U, bOk ? TEXT("отправлен") : TEXT("ОТКАЗ"));
}

// 2055 — «Добавить приятеля» → 4150.
void UAsdaPlayerMenuSubsystem::ActionFriendAdd()
{
	const int32 U = TargetUserNum;
	const FString Name = TargetName;
	CloseMenu();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->bInGameConnected)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] 4150 не отправлен: нет соединения"));
		return;
	}
	// ★ ГЕЙТ ЭТАЛОНА `listContainsIdTypePair(g_pCommunityFriendWnd, charId, char+1076)`:
	//   «уже в списке приятелей» ⇒ нотис 12812, пакет НЕ шлётся.
	//   ★ ВОССТАНОВЛЕН 31.08.2026 (аудит вкладки «Приятель»): раньше гейт был пропущен
	//   с пометкой «списка в порте нет» — теперь вкладка 14500 портирована, список есть
	//   (кэш `FriendEntries`, запись 31 б, charId @0). Без гейта клиент шлёт лишний пакет.
	if (TargetCharId != 0)
	{
		for (const TArray<uint8>& E : Net->GetFriendEntries())
		{
			if (E.Num() >= 4 && AsdaBytes::RdI32(E.GetData()) == TargetCharId)
			{
				Notice(12812);
				UE_LOG(LogAsdaPlayerMenu, Log,
					TEXT("[меню игрока] 4150 НЕ отправлен: «%s» уже в списке (гейт эталона, нотис 12812)"), *Name);
				return;
			}
		}
	}
	const bool bOk = Net->SendFriendInvite4150(U);
	if (bOk) { ++CntSent; }
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] 2055 «Добавить приятеля» «%s» (userNum=%d) -> %s "
		     "(локальный дубль-гейт пропущен: списка друзей в порту нет, дубль отсеет сервер кодом 3 → нотис 12812)"),
		*Name, U, bOk ? TEXT("отправлен") : TEXT("ОТКАЗ"));
}

// 2002 — «Предложить Дружбу» (соулмейт) → 5200.
// ★★★ ВОЛНА 1-А (06.10.2026). [ОРИГИНАЛ] `case 0x7D2` меню (AsdaStory_game.c:292908-292911):
//   `if (pExchange->gap_26DC[48]) coupleSendProposalReq(g_pSoulmateWnd, цель, nullptr)` → `uiDialog_ClearAndHide`.
//   Режим @0 — `CSoulmateWnd+1900` (1; 2 — пока открыто 9100), текст @25 — `CFindSoulmateWin+516` (D15), имя — `цель+1056`.
//   Прежний порт собирал 5200 САМ с режимом 0 и пустым текстом («окно 8200 не портировано») и держал свою защёлку;
//   теперь пункт зовёт ЕДИНУЮ точку `UAsdaSoulmateSubsystem::SendProposal5200` (те же поля, что у 8266 и 9104,
//   и эталонное `editBoxClearAndHide` после отправки). Проверки соединения у эталона нет — отказ без связи даёт
//   сам отправитель `SendSoulmateCreate5200` (молча, с записью в журнал).
void UAsdaPlayerMenuSubsystem::ActionSoulmate()
{
	const FString Name = TargetName;   // `gap_26DC[48]` — цель, по которой открыто меню
	CloseMenu();                       // `uiDialog_ClearAndHide` (у эталона — после отправки; порядок не наблюдаем)
	UGameInstance* GI = GetGameInstance();
	UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
	if (!Soul || Name.IsEmpty())
	{
		UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 2002: нет цели или подсистемы соулмейта — 5200 не отправлен (эталон: `if (gap_26DC[48])`)"));
		return;
	}
	const bool bOk = Soul->SendProposal5200(Name);
	if (bOk) { ++CntSent; }
	UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 2002 соулмейт «%s» -> %s"), *Name, bOk ? TEXT("отправлен") : TEXT("ОТКАЗ"));
}

// ★ 29.09.2026 (волна О1): форварды признака «заявка обмена в полёте» (`CExchange+0x14`) в модель обмена.
bool UAsdaPlayerMenuSubsystem::IsExchangeRequestActive() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaExchangeP2PSubsystem* P2P = GI ? GI->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr;
	return P2P && P2P->IsRequestActive();
}

#if !UE_BUILD_SHIPPING
void UAsdaPlayerMenuSubsystem::DebugSetExchangeRequestActive(bool bOn)
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaExchangeP2PSubsystem* P2P = GI ? GI->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr) { P2P->DebugSetRequestActive(bOn); }
}
#endif

// 2004/2005 — «Обмен предметами» / «Обмен личными вещами» → 5282, различие только в байте режима.
// ★ 29.09.2026 (волна О1 торговли): весь обработчик эталона `exchangeUiOnCommand` case 0x7D4/0x7D5 (shop_trade.c:6616-6717) —
//   7 гейтов (5043 разговор NPC, 5033 элитный магазин, 5032 рюкзак, 13016 парикмахерская, 13104 барабан гачи, 10307 склад,
//   ремесло — молча), 5042 «окно обмена открыто» (меню остаётся), `CExchange_SendRequest`/`SendAcceptRequest` (5054, 20090,
//   5282, таймер 33 с) — живёт в модели `CExchange` (`UAsdaExchangeP2PSubsystem::OnMenuCommand`). Здесь только скрыть меню.
void UAsdaPlayerMenuSubsystem::ActionExchange(bool bPersonalItems)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaExchangeP2PSubsystem* P2P = GI ? GI->GetSubsystem<UAsdaExchangeP2PSubsystem>() : nullptr;
	if (!P2P)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] %s: подсистемы обмена нет"), bPersonalItems ? TEXT("2005") : TEXT("2004"));
		CloseMenu();
		return;
	}
	// +0x2700 (userIdx ПКМ-нажатия) = цель меню: у порта userIdx и userNum — один сессионный номер.
	const bool bHide = P2P->OnMenuCommand(bPersonalItems, TargetUserNum, TargetUserNum, TargetName);
	if (bHide) { CloseMenu(); }   // `uiDialog_ClearAndHide`; при 5042 эталон меню НЕ скрывает
	UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] %s «%s» (userNum=%d) → модель обмена; меню %s"),
		bPersonalItems ? TEXT("2005 обмен личными") : TEXT("2004 обмен предметами"), *TargetName, TargetUserNum,
		bHide ? TEXT("скрыто") : TEXT("остаётся (5042)"));
}

// 2006 — «Информация о персонаже» → 5478.
void UAsdaPlayerMenuSubsystem::ActionCharInfo()
{
	const int32 U = TargetUserNum;
	const int32 Cid = TargetCharId;
	const FString Name = TargetName;
	CloseMenu();
	// ★ 25.10.2026: тело — общая точка `RequestCharInfo` (её же зовёт функция 2/5 «ACTION VIEW OTHER INFO»).
	RequestCharInfo(U, Cid, Name, TEXT("меню 2006"));
}

bool UAsdaPlayerMenuSubsystem::RequestCharInfo(int32 U, int32 Cid, const FString& Name, const TCHAR* Why)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 16, баг 4; `asda.OtherChara.SelfLocal`): о СЕБЕ сервер на 5478 не отвечает
	//   (`Send_TargetChar_Information`: UserIndex запросившего == цели ⇒ молча) — окно 4200 собирается из своих данных, 5478 не шлётся.
	if (Net && UAsdaOtherCharaSubsystem::IsSelfLocalEnabled() && AsdaSelf::IsSelfUserIdx(Net, U))
	{
		UAsdaOtherCharaSubsystem* OC = GI->GetSubsystem<UAsdaOtherCharaSubsystem>();
		const bool bShown = OC && OC->ShowSelf(Why);
		UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] %s: инфо о СЕБЕ «%s» (userNum=%d) → окно 4200 из своих данных: %s"),
			Why, *Name, U, bShown ? TEXT("открыто") : TEXT("НЕ открыто"));
		return bShown;
	}
	if (!Net || !Net->bInGameConnected)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] %s: 5478 не отправлен: нет соединения"), Why);
		return false;
	}
	if (bCharInfoRequestActive && CVarMenuInfoNoLatch.GetValueOnGameThread() == 0)
	{
		Notice(kLocNoticeInfoInFlight);   // 9419
		return false;
	}
	if (Cid == 0)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning,
			TEXT("[меню игрока] %s: 5478 не отправлен: charId цели «%s» неизвестен (снапшот 4005/4065 не приходил). "
			     "Слать userNum вместо charId нельзя — сервер молча отбросит (charid-vs-usernum)"), Why, *Name);
		return false;
	}
	// @6 — байт `char+1076` (раса/пол цели). В реестре соседей он не хранится отдельно; эталон
	//   берёт его из структуры персонажа. Пока шлём 0 и пишем это в лог — поле участвует только в
	//   выборе набора иконок на СЕРВЕРНОЙ стороне ответа 5479.
	const bool bOk = Net->SendTargetCharInfo5478(U, Cid, 0);
	if (bOk)
	{
		++CntSent;
		bCharInfoRequestActive = true;
		const UWorld* W = GI->GetWorld();
		CharInfoRequestStartTime = W ? W->GetTimeSeconds() : 0.0;
	}
	// ★★★ 2026-09-17: окно `OTHERCHARA_DATA` 4200 ПОРТИРОВАНО (`UAsdaOtherCharaSubsystem`).
	//   Раньше здесь печаталась строка «окна ещё нет» (990020) — она снята: эталон в этом месте
	//   никакого сообщения не показывает (`otherCharaWnd_SetTargetUser@0xDA7640` только ставит
	//   цель и шлёт пакет), а окно теперь есть и данные показать есть где.
	//   Отправку 5478 оставляем ЗДЕСЬ (она уже была), окну передаём только цель и признак
	//   ожидания — эталонные `win[238]` и `win[40]`; второй копии отправки не заводим.
	if (bOk)
	{
		if (UAsdaOtherCharaSubsystem* OC = GI->GetSubsystem<UAsdaOtherCharaSubsystem>())
		{
			OC->BeginRequest(U);
		}
	}
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] %s: инфо «%s» (userNum=%d charId=%d, байт расы 0) -> %s; "
		     "ответ (5479 + 7006) разберёт окно OTHERCHARA_DATA 4200"),
		Why, *Name, U, Cid, bOk ? TEXT("отправлен") : TEXT("ОТКАЗ"));
	return bOk;
}

// 2054 — «Шепот». Пакета НЕТ: `Chat_BeginWhisperTo` только переводит строку ввода в режим 7.
void UAsdaPlayerMenuSubsystem::ActionWhisper()
{
	const FString Name = TargetName;
	CloseMenu();
	if (Name.IsEmpty())
	{
		// 1:1 эталон: `if (strlen(name) <= 0) return 0;` — молча ничего.
		UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 2054 «Шепот»: имя цели пусто — эталон молча выходит"));
		return;
	}
	if (UAsdaChatWindowSubsystem* Chat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr)
	{
		// ★★★ Общая точка порта `CChatList_OnSelectWhisper`: в поле 2609 встаёт «/w имя » —
		//   без этого на экране не менялось ничего (жалоба владельца 2026-08-30).
		Chat->BeginWhisperTo(Name);
	}
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] 2054 «Шепот» по «%s»: режим ввода 7, адресат запомнен (опкод НЕ шлётся — "
		     "5088 уйдёт при Enter)"), *Name);
}

// 2062 — «Следовать». Пакета НЕТ, это клиентский автобег.
void UAsdaPlayerMenuSubsystem::ActionFollow()
{
	const int32 U = TargetUserNum;
	const int32 Cid = TargetCharId;
	const FString Name = TargetName;
	CloseMenu();
	StartFollow(U, Cid, Name, /*Mode*/ 0);
}

// ★ Общее тело для обоих входов (2062 «меню по игроку» и 8101 «меню по строке отряда»).
//   Вынесено из `ActionFollow`, чтобы окно отряда звало ту же логику, а не свою копию:
//   в эталоне оба пункта тоже сходятся в одну функцию, различаясь только режимом.
void UAsdaPlayerMenuSubsystem::StartFollow(int32 U, int32 Cid, const FString& Name, int32 Mode)
{
	// ★ Волна Л5 торговли (30.09.2026): [ОРИГИНАЛ] `uiPreparePartyTradeRequest@0xFBE840` (МИСНЕЙМ = «Следовать», ui_core.c:99429-99433) —
	//   ПЕРВЫЙ гейт: `if (j_get_m_bDlgVisible(g_pSellWindow) || j_get_o168(g_bSomeFeatureEnabled))` → 107022 в чат (−11514, 10 с),
	//   следование не взводится.
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GetGameInstance()))
	{
		++NoticeSeq; LastNoticeLoc = 107022; LastNoticeSlot.Reset();   // след самотеста (как у `Notice`)
		UAsdaTradeStateSubsystem::ChatStoreGate(GetGameInstance(), 107022, TEXT("uiPreparePartyTradeRequest"));
		return;
	}
	// ★ Ревизия Л4/Л5 (СРЕДН-1): [ОРИГИНАЛ] второй гейт (ui_core.c:99434-99438): `else if (j_CObject_GetField5581(g_pWarehouseWnd))`
	//   (свиток возврата склада активен, `UAsdaWarehouseSubsystem::IsRecallActive`) → 10305 в чат (−11514, 10 с), следования нет.
	if (UAsdaWarehouseSubsystem* Ware = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr)
	{
		if (Ware->IsRecallActive())
		{
			++NoticeSeq; LastNoticeLoc = 10305; LastNoticeSlot.Reset();
			Ware->NoticeRecallBlocked(10305);
			return;
		}
	}
	// ГЕЙТ пункта (shop_trade.c:6460-6469): мир тикает, нет активной цели отряда, нет сцены гачи.
	//   У нас эквивалент — «мы в мире и есть павн».
	AActor* Tgt = nullptr;
	if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		Tgt = WS->GetPlayerActorByUserNum(U);
	}
	if (!Tgt)
	{
		Notice(kLocNoticeNoTarget);   // 20308 «цель не найдена»
		return;
	}
	bFollowActive = true;
	FollowUserNum = U;
	FollowCharId  = Cid;
	FollowName    = Name;   // ★ нужен слоту [ss1] нотиса отмены 20310
	FollowLastOrderTime = 0.0;
	bFollowMoving = false;
	Notice(kLocNoticeFollowStart, Name);   // 20311 со слотом-именем
	// ★★★ 26.10.2026 (navattack) [ИСПРАВЛЕНИЕ]: «Следовать» — приказ автопути игрока: выход из атаки сразу (разбор у
	//   `AsdaExitCombatForAutoPath`). У эталона следование, включённое посреди атаки, ждало её конца (заявку `beginMoveToTile`
	//   снимал главный тик в состоянии атаки, LABEL_111).
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		AsdaExitCombatForAutoPath(Cast<AAsdaPlayerController>(W->GetFirstPlayerController()), TEXT("«Следовать»"));
	}
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] «Следовать» за «%s» (userNum=%d charId=%d, вход %s) включено; "
		     "пороги эталона: стоп < %.0f, новый приказ > %.0f, период %.0f с"),
		*Name, U, Cid, (Mode == 1) ? TEXT("8101 строка отряда") : TEXT("2062 меню игрока"),
		kFollowStopDist, kFollowReorderDist, kFollowOrderPeriod);
}

void UAsdaPlayerMenuSubsystem::CancelFollow(const TCHAR* Why)
{
	if (!bFollowActive) { return; }
	bFollowActive = false;
	FollowUserNum = -1;
	FollowCharId  = 0;
	bFollowMoving = false;
	const FString Was = FollowName;
	FollowName.Empty();
	// 1:1 `followTargetCancelReset@0xFBEBD0`: нотис 20310 и поля в −1.
	// ★★★ ИСПРАВЛЕНО 2026-08-29: строка 20310 — «[ss1]: не следовать», у неё ЕСТЬ маркер, и
	//   раньше он уходил на экран НЕЗАПОЛНЕННЫМ (владелец увидел буквально «[ss1]: не следовать»).
	//   Имя ведущего берём из снимка, сделанного при включении следования.
	Notice(kLocNoticeFollowStop, Was);
	UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] следование отменено (%s)"), Why ? Why : TEXT("?"));
}

void UAsdaPlayerMenuSubsystem::FollowTick()
{
	if (!bFollowActive) { return; }
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (!W) { return; }
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
	AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	if (!PC || !Me || !WS) { return; }

	AActor* Tgt = WS->GetPlayerActorByUserNum(FollowUserNum);
	if (!Tgt)
	{
		// Эталон: цель пропала из менеджера ⇒ следование просто перестаёт двигать (поле [64]
		//   не совпало). Ждём возвращения соседа в AoI, как оригинал, но не молчим в логе.
		UE_LOG(LogAsdaPlayerMenu, Verbose, TEXT("[меню игрока] следование: цель userNum=%d вне зоны видимости"), FollowUserNum);
		return;
	}
	// ★★★ 26.10.2026 (navattack): игрок САМ начал атаку, пока идёт следование (горячая клавиша цели) — приказов следования нет до
	//   конца атаки, состояние следования не меняется (после атаки оно продолжится). [ОРИГИНАЛ] ветка «бежим»: заявку
	//   `beginMoveToTile` (followTargetMoveTick, math_geom.c:2336) главный тик в состоянии атаки снимает каждый кадр (game_main.c:1707,
	//   LABEL_111), само следование (+240) живёт. [ИСПРАВЛЕНИЕ] ветка «стоп у цели»: эталон шёл через `CGame_HandleGroundClickAttack`
	//   (действие 16 — атака снималась на точке отправки удара); у нас атаку, выбранную игроком, следование не ломает.
	//   Без этого приказ следования через `MoveToGamePoint` выводил бы из атаки (ключ `asda.Move.AutoPathExitsCombat`).
	if (PC->bAutoAttack && AsdaAutoPathExitsCombatOn())
	{
		UE_LOG(LogAsdaPlayerMenu, Verbose, TEXT("[меню игрока] следование ждёт: игрок атакует (эталон LABEL_111)"));
		return;
	}
	const FVector2D MeG  = Me->GetGamePosXZ();
	const FVector2D TgtG = AsdaCoord::UeToGame2D(Tgt->GetActorLocation());
	// Эталон меряет расстояние в «единицах/100» — обе точки он делит на 100 перед вычитанием.
	const float Dist = (float)FVector2D::Distance(MeG, TgtG) / 100.f;
	const double Now = W->GetTimeSeconds();

	if (Dist >= kFollowStopDist)
	{
		// ★★★ ПРИКАЗ ПРИ ВХОДЕ В СОСТОЯНИЕ «БЕЖИМ». Раньше он выдавался ТОЛЬКО при
		//   `Dist > 10 && прошло 3 с`, а ветка стопа требует `bFollowMoving == true`, который
		//   включение следования ставит в false. Значит при `Dist < 6` и при `6 ≤ Dist ≤ 10`
		//   не выдавалось НИ ОДНОГО приказа — а соотрядец обычно как раз рядом. Отсюда жалоба
		//   владельца «нажимаю и ничего не происходит».
		const bool bJustStarted = !bFollowMoving;
		bFollowMoving = true;
		if (bJustStarted || (Dist > kFollowReorderDist && Now - FollowLastOrderTime > kFollowOrderPeriod))
		{
			FollowLastOrderTime = Now;
			PC->MoveToGamePoint(TgtG);
			UE_LOG(LogAsdaPlayerMenu, Verbose,
				TEXT("[меню игрока] следование: дистанция %.1f > %.0f — приказ движения к (%.0f,%.0f)"),
				Dist, kFollowReorderDist, TgtG.X, TgtG.Y);
		}
	}
	else if (bFollowMoving)
	{
		// Дистанция сомкнулась: эталон останавливается не в самой цели, а в точке
		//   «цель − 150 юнитов по направлению от нас» (`Vector3_Scale(dir, 150.0)`).
		bFollowMoving = false;
		FVector2D Dir = TgtG - MeG;
		if (!Dir.IsNearlyZero()) { Dir.Normalize(); }
		const FVector2D Stop = TgtG - Dir * kFollowStandOff;
		PC->MoveToGamePoint(Stop);
		UE_LOG(LogAsdaPlayerMenu, Verbose,
			TEXT("[меню игрока] следование: дистанция %.1f < %.0f — стоп в (%.0f,%.0f) (цель − %.0f юнитов)"),
			Dist, kFollowStopDist, Stop.X, Stop.Y, kFollowStandOff);
	}
}

// 2063 — «Просмотреть блог» → 6082. Кнопка мертва (BuildUI гасит), ветка кода жива.
void UAsdaPlayerMenuSubsystem::ActionBlog()
{
	const FString Name = TargetName;
	CloseMenu();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Net->bInGameConnected || Name.IsEmpty()) { return; }
	const bool bOk = Net->SendGetUserId6082(Name);
	if (bOk) { ++CntSent; }
	UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 2063 «Просмотреть блог» «%s» -> %s (ответ 6083)"),
		*Name, bOk ? TEXT("отправлен") : TEXT("ОТКАЗ"));
}

// 2070 — «Вызвать на поединок» → 4302.
void UAsdaPlayerMenuSubsystem::ActionDuel()
{
	const int32 U = TargetUserNum;
	const int32 Cid = TargetCharId;
	const FString Name = TargetName;
	CloseMenu();
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net || !Net->bInGameConnected)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] 4302 не отправлен: нет соединения"));
		return;
	}
	// Эталон ищет цель ПО ИМЕНИ (`CharManager_FindByName(name)`) и молча выходит, если не нашёл.
	if (!WS || !WS->FindPlayerActorByName(Name))
	{
		UE_LOG(LogAsdaPlayerMenu, Log,
			TEXT("[меню игрока] 4302 не отправлен: цель «%s» не найдена по имени (эталон молча выходит)"), *Name);
		return;
	}
	if (Cid == 0)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning,
			TEXT("[меню игрока] 4302 не отправлен: charId цели неизвестен — подстановка userNum сломала бы пакет"));
		return;
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5b): свой userNum — `AsdaSelf::GetUserIdxOr`; до 4003 (номер неизвестен) пакет НЕ шлём (было: 0 — номер чужого игрока, сервер выдаёт его штатно); ключ отката `asda.Self.StrictUnknownNum 0` — сырой номер.
	const int32 MyNum = AsdaSelf::GetUserIdxOr(Net, -1);
	if (MyNum < 0)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] 4302 не отправлен: свой userNum ещё неизвестен (4003 не пришёл)"));
		return;
	}
	const FString MyName = WS->GetSelfCharName();
	const bool bOk = Net->SendDuelReq4302(MyNum, Net->SelectedCharId, MyName, U, Cid);
	if (bOk) { ++CntSent; }
	UE_LOG(LogAsdaPlayerMenu, Log,
		TEXT("[меню игрока] 2070 поединок: я (userNum=%u charId=%d «%s») → «%s» (userNum=%d charId=%d) -> %s"),
		Net->SelfUserNum, Net->SelectedCharId, *MyName, *Name, U, Cid, bOk ? TEXT("отправлен") : TEXT("ОТКАЗ"));
}

// ═══════════════════════════════ ОТВЕТЫ СЕРВЕРА ════════════════════════════════════════════════
//   Разбор тел — §3 шапки. Здесь снимаются защёлки «запрос в полёте» и печатаются нотисы,
//   которые эталон печатает в тех же ветках. Окна-приёмники (2008/4200/8200) не портированы —
//   их подключение записано в реестр отложенного.
void UAsdaPlayerMenuSubsystem::OnMenuResult(int32 Opcode, const TArray<uint8>& Body)
{
	switch (Opcode)
	{
	case 4109:   // «Вступить в клан». Эталон `Recv_ClanMemberLeave_Notice@0xBC9940`: байт 0 — код.
	{
		if (Body.Num() < 1) { break; }
		const int32 Code = (int32)Body[0];
		if (Code == 1)
		{
			// ★ 27.09.2026 (СРЕДН-1 ревизии friend_gate): разбор 1:1 `Recv_ClanMemberLeave_Notice` case 1
			//   (network.c:12039-12068). Сервер (`dBClanMemberInviteRecvResponseUser`, GameServer.c:952-953) шлёт
			//   этот код ОБОИМ — пригласившему и приглашённому; @1 = пригласивший, @3 = ПРИГЛАШЁННЫЙ.
			//   [ОРИГИНАЛ] Порядок эталона:
			//     1) `charPtr = CharManager_FindByUserIdx(@3)` — персонаж @3 (СВОЙ — у приглашённого), clanId/имя
			//        клана ему уже записал приём в `AsdaNetSubsystem` (`OnClanInfo`, до этой рассылки);
			//     2) слот нотиса = `charPtr->name` — ИМЯ ПЕРСОНАЖА @3 → loc 20051;
			//     3) 4138 со своим userIdx — «прислать состав клана» (имя серверной функции врёт);
			//     4) если панель вкладки «Клан» видима — 4122 + 4124.
			//   Прежде имя бралось из `TargetName` (цель ПОСЛЕДНЕГО меню игрока: у приглашённого — чужая/пустая),
			//   а 4138/4122/4124 не слались вовсе.
			// [ПОРТ] Персонажа @3 нет в мире: у эталона тогда весь блок пропущен, а нотис печатает НЕИНИЦИАЛИЗИРОВАННЫЙ
			//   стековый буфер `leaveNoticeText` (мусор, дефект оригинала) — порт нотис не печатает, пишет в лог.
			if (Body.Num() < 5)
			{
				UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] 4109 код 1: тело %d б < 5 — @3 нет"), Body.Num());
				break;
			}
			const int32 U = AsdaBytes::RdI16(Body.GetData() + 3);   // @3 i16 userNum приглашённого
			UGameInstance* GI = GetGameInstance();
			UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			FString CharName;   // `charPtr->name`
			bool bFound = false;   // `charPtr != nullptr`
			if (Net && WS && AsdaSelf::IsSelfUserIdx(Net, U))
			{
				CharName = WS->GetSelfCharName();
				bFound = true;
			}
			else if (WS)
			{
				int32 ClassId = 0, Level = 0, ClanId = -1;   // поля снимка соседа — здесь не нужны
				bFound = WS->GetNetPlayerInfo(U, CharName, ClassId, Level, ClanId);
			}
			if (!bFound)
			{
				UE_LOG(LogAsdaPlayerMenu, Warning,
					TEXT("[меню игрока] 4109 код 1: персонажа userNum=%d в мире нет — эталон пропускает блок и печатает ")
					TEXT("мусор неинициализированного буфера; порт нотис не печатает [ПОРТ]"), U);
				break;
			}
			if (CVarClanInviteResultMsgBox.GetValueOnGameThread() != 0)
			{
				// ★ 08.11.2026 [ОРИГИНАЛ] network.c:12067 `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, leaveNoticeText)` = окно 1500.
				++NoticeSeq; LastNoticeLoc = 20051; LastNoticeSlot = CharName;   // след для самотеста — как у `Notice`
				AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Social, AsdaNotice::LocSlot(GetGameInstance(), 20051, CharName));
			}
			else { Notice(20051, CharName); }
			bool b4138 = false;   // ушёл ли 4138
			if (Net) { b4138 = Net->SendClanRosterRefresh4138(); }
			bool bTab = false;    // ушли ли 4122 + 4124 (панель вкладки клана видима)
			if (UAsdaCommunityClanSubsystem* CC = GI ? GI->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr)
			{
				bTab = CC->RequestClanDataIfTabShown(TEXT("4109 код 1"));
			}
			UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 4109: игрок «%s» (userNum=%d) принят в клан (код 1); 4138=%s, 4122+4124=%s"),
				*CharName, U, b4138 ? TEXT("да") : TEXT("нет"), bTab ? TEXT("да") : TEXT("нет"));
		}
		else
		{
			static const TMap<int32, int32> kLoc = { {0, 12413}, {2, 12415}, {3, 12416}, {4, 12417},
			                                         {5, 12419}, {6, 12420}, {7, 12421}, {8, 12168} };
			const int32* L = kLoc.Find(Code);
			int32 LocId = L ? *L : -1;
			// ★ 08.11.2026 (пачка 16, баг 7) [ИСПРАВЛЕНИЕ] D8: код 4 сервер шлёт и пригласившему БЕЗ клана
			//   (`ClanMemberInvite_RequestUser`, GameServer.c:735: `sub_401070(charIndex) < 0` → 4) — эталон печатает 12417
			//   «…добавляет только его глава». Без клана — 991210 «Ты не состоишь в клане.» (ключ asda.Clan.NoClanNotice).
			if (Code == 4) { LocId = UAsdaClanSubsystem::NoClanLocOr(GetGameInstance(), LocId); }
			if (LocId > 0)
			{
				if (CVarClanInviteResultMsgBox.GetValueOnGameThread() != 0)
				{
					// [ОРИГИНАЛ] network.c:12031-12094 — все отказы `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, …)` = окно 1500.
					++NoticeSeq; LastNoticeLoc = LocId; LastNoticeSlot.Reset();   // след для самотеста — как у `Notice`
					AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Social, LocId);
				}
				else { Notice(LocId); }
			}
			UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 4109: отказ клан-инвайта, код %d → нотис %d"),
				Code, LocId);
		}
		break;
	}
	case 4151:   // «Добавить приятеля». Эталон `uiItemNoticeDispatch@0xCE7D00`: байт 0 — код.
	{
		if (Body.Num() < 1) { break; }
		const int32 Code = (int32)Body[0];
		// 1 = успех: запись друга 31 б с +1, ИМЯ с +12, нотис 20000 со слотом-именем.
		//   Прочие коды — свой нотис, пакет отвергнут.
		if (Code == 1)
		{
			FString Name;
			if (Body.Num() >= 32)
			{
				// Имя лежит в записи по смещению +12 от начала тела, cp1251, ≤ 20 б.
				const int32 Max = FMath::Min(20, Body.Num() - 12);
					// ★ ИСПРАВЛЕНИЕ (`fix_helpers_dedup.md`): прежний инлайн-декодер брал только ASCII и А..я —
					//   байты Ё/ё (0xA8/0xB8) уходили в ветку «А..я» и портились. Сведено к общей точке `DecodeCp1251`.
					Name = FAsdaLoginData::DecodeCp1251(Body.GetData() + 12, Max);
			}
			const FString Slot = Name.IsEmpty() ? TargetName : Name;   // имя из записи (эталон: всегда `a2 + 12`)
			if (AsdaFriendResultChannelsOn())
			{
				// [ОРИГИНАЛ] Плашка 221, затем строка журнала — один текст, цвет −11514, 10 000 мс (порядок эталона).
				UGameInstance* GI = GetGameInstance();
				UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
				const FString Line = Txt ? Txt->FormatById(20000, { Slot }, {}) : FString();   // loc 20000 со слотом [ss1]
				++NoticeSeq; LastNoticeLoc = 20000; LastNoticeSlot = Slot;   // след для самотеста — как у `Notice`
				const bool bPlank = AsdaNotice::Plank(GI, EAsdaMsgCategory::Social, Line, AsdaNoticeColor::Gold, 10000.f, 4151);
				const bool bChat  = AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Social, Line, AsdaNoticeColor::Gold, 10000.f);
				UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 4151: приятель «%s» добавлен (код 1) → плашка 221 %s, журнал %s: %s"),
					*Slot, bPlank ? TEXT("да") : TEXT("НЕТ"), bChat ? TEXT("да") : TEXT("НЕТ"), *Line);
			}
			else
			{
				Notice(20000, Slot);
				UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 4151: приятель «%s» добавлен (код 1)"), *Slot);
			}
		}
		else
		{
			// [ОРИГИНАЛ] 0 → 12808 · 2 → 12811 · 3 → 12812 (уже в списке) · 4 → 12813 · 5 → 20082.
			// [ИСПРАВЛЕНИЕ] (ключ ResultChannels) 6 → 12813 (полны оба списка, флаг UPFriendAdd 6) · 7 → 12812 (уже приятели, флаг 7).
			static const TMap<int32, int32> kLoc = { {0, 12808}, {2, 12811}, {3, 12812}, {4, 12813}, {5, 20082} };
			static const TMap<int32, int32> kLocFix = { {6, 12813}, {7, 12812} };
			const bool bOrig = AsdaFriendResultChannelsOn();
			const int32* L = kLoc.Find(Code);
			if (!L && bOrig) { L = kLocFix.Find(Code); }
			if (L)
			{
				if (bOrig)
				{
					// [ОРИГИНАЛ] `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст)` = окно-сообщение 1500.
					++NoticeSeq; LastNoticeLoc = *L; LastNoticeSlot.Reset();   // след для самотеста — как у `Notice`
					const bool bBox = AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Social, *L);
					UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 4151: отказ, код %d → окно-сообщение %d (%s)"),
						Code, *L, bBox ? TEXT("показано") : TEXT("окна нет — строка журнала"));
				}
				else { Notice(*L); }
			}
			UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] 4151: отказ, код %d → нотис %d"),
				Code, L ? *L : -1);
		}
		break;
	}
	// ★ 29.09.2026 (волна О1 торговли): 5283 (заявка обмена, `Recv_PartyMember` a2=7) разбирает модель `CExchange` —
	//   `UAsdaExchangeP2PSubsystem` (значок 4/5, звук 215, 5048/5049 + 5058/5059, авто-«занят» 5284/3, шкала 30 с).
	//   Прежняя ветка здесь снимала «заявку в полёте» ЛЮБЫМ чужим 5283 (ПД-3) и не отвечала серверу.
	case 5479:   // Сведения о чужом персонаже → окно OTHERCHARA_DATA 4200 (не портировано).
		bCharInfoRequestActive = false;
		UE_LOG(LogAsdaPlayerMenu, Log,
			TEXT("[меню игрока] 5479: сведения о персонаже пришли (%d б); окно OTHERCHARA_DATA 4200 "
			     "не портировано — данные пока не показываются (реестр отложенного)"), Body.Num());
		break;
	case 5201:   // Ответ по Дружбе — разбирает `UAsdaSoulmateSubsystem::OnPlayerMenuResult` (значок 5055 → окно 8400, коды 1..7 → чат).
		UE_LOG(LogAsdaPlayerMenu, Log,
			TEXT("[меню игрока] 5201: ответ по соулмейту (%d б) — обработчик в UAsdaSoulmateSubsystem"), Body.Num());
		break;
	case 6083:   // Логин витрины item-mall (ответ на 6082 «Просмотреть блог»).
		UE_LOG(LogAsdaPlayerMenu, Log,
			TEXT("[меню игрока] 6083: ответ на 6082 (%d б); окно My_BLOG_Win 19000 — встроенный "
			     "Win32-браузер, в порт не переносится"), Body.Num());
		break;
	default:
		break;
	}
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
//   ★ ЗАКОН: диагностика обязана жать САМУ КНОПКУ, а не звать обработчик — иначе она подтверждает
//     несуществующее. Команда `asda.PlayerMenu.Click <idnum>` ищет узел в живом виджете и вызывает
//     его `OnClicked`, то есть проходит ровно тем путём, что и мышь игрока.
namespace
{
	UAsdaPlayerMenuSubsystem* PMSub(const UWorld* W)
	{
		const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPlayerMenuOpen(
	TEXT("asda.PlayerMenu.Open"),
	TEXT("asda.PlayerMenu.Open <userNum> — открыть меню 2000 по соседу с этим сессионным индексом "
	     "(эталонные гейты обходятся: ЛКМ-пик подставляется искусственно)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPlayerMenuSubsystem* S = PMSub(W);
		if (!S) { UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("подсистема недоступна")); return; }
		const int32 U = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		S->NotifyRightPress(U);
		const bool bOk = S->NotifyRightRelease(U, /*bIsSelf=*/false);
		UE_LOG(LogAsdaPlayerMenu, Log, TEXT("asda.PlayerMenu.Open %d -> %s"), U, bOk ? TEXT("открыто") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPlayerMenuClick(
	TEXT("asda.PlayerMenu.Click"),
	TEXT("asda.PlayerMenu.Click <idnum> — нажать пункт меню 2000 ЕГО СОБСТВЕННОЙ кнопкой "
	     "(2001/2002/2003/2004/2005/2006/2052/2054/2055/2062/2070)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPlayerMenuSubsystem* S = PMSub(W);
		if (!S) { UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("подсистема недоступна")); return; }
		if (!S->IsMenuOpen()) { UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("меню закрыто — сначала asda.PlayerMenu.Open")); return; }
		const int32 Id = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		static const TMap<int32, const TCHAR*> kNode = {
			{2001, TEXT("TRANTOOL_BUT01")}, {2002, TEXT("TRANTOOL_BUT02")}, {2003, TEXT("TRANTOOL_BUT03")},
			{2004, TEXT("TRANTOOL_BUT04")}, {2005, TEXT("TRANTOOL_BUT05")}, {2006, TEXT("TRANTOOL_BUT06")},
			{2052, TEXT("TRANTOOL_BUT07")}, {2054, TEXT("TRANTOOL_BUT08")}, {2055, TEXT("TRANTOOL_BUT09")},
			{2062, TEXT("TRANTOOL_BUT11")}, {2070, TEXT("TRANTOOL_BUT13")} };
		const TCHAR* const* Node = kNode.Find(Id);
		if (!Node) { UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("idnum %d — не живой пункт меню 2000"), Id); return; }
		S->ClickByNodeName(*Node);
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ПРОВЕРКА ПУТЁМ ИГРОКА: НАСТОЯЩИЙ ПРАВЫЙ КЛИК, а не вызов обработчика.
//   Ставит фейкового соседа (если попросили), наводит на него курсор через
//   `ProjectWorldLocationToScreen` + `SetMouseLocation`, и ПРОГОНЯЕТ КЛАВИШУ ЧЕРЕЗ ВВОД
//   (`APlayerController::InputKey`) — то есть срабатывают ТЕ ЖЕ привязки
//   `BindKey(RightMouseButton, IE_Pressed/IE_Released)`, что и от мыши. Никакой прямой вызов
//   `OnRightPressed/OnRightReleased` не годится: он обошёл бы биндинг и подтвердил бы
//   несуществующее (закон «диагностика жмёт саму кнопку»).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPlayerMenuRightClick(
	TEXT("asda.PlayerMenu.RightClick"),
	TEXT("asda.PlayerMenu.RightClick <userNum> — навести курсор на актора этого игрока и сделать "
	     "НАСТОЯЩИЙ правый клик (нажатие+отпускание через привязки ввода)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W) { return; }
		APlayerController* PC = W->GetFirstPlayerController();
		UGameInstance* GI = W->GetGameInstance();
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!PC || !WS) { UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("нет контроллера/мира")); return; }
		const int32 U = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		AActor* Tgt = WS->GetPlayerActorByUserNum(U);
		if (!Tgt)
		{
			UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("актора игрока userNum=%d в мире нет"), U);
			return;
		}
		FVector2D Screen(0.f, 0.f);
		if (!PC->ProjectWorldLocationToScreen(Tgt->GetActorLocation(), Screen, false))
		{
			// Вне кадра — переставляем актора прямо перед камерой и пробуем ещё раз. Это ТОЛЬКО
			//   диагностика без сервера: в бою/сети позицию соседа задаёт сеть.
			if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
			{
				const FVector Ahead = Cam->GetCameraLocation() + Cam->GetCameraRotation().Vector() * 700.f;
				Tgt->SetActorLocation(Ahead);
				UE_LOG(LogAsdaPlayerMenu, Log,
					TEXT("[меню игрока] ПРОВЕРКА: актор userNum=%d был вне кадра — переставлен перед камерой"), U);
			}
			if (!PC->ProjectWorldLocationToScreen(Tgt->GetActorLocation(), Screen, false))
			{
				UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("актор userNum=%d вне кадра — курсор не навести"), U);
				return;
			}
		}
		PC->SetMouseLocation((int32)Screen.X, (int32)Screen.Y);
		UE_LOG(LogAsdaPlayerMenu, Log,
			TEXT("[меню игрока] ПРОВЕРКА: курсор наведён на «%s» (userNum=%d) в экранные (%.0f,%.0f); "
			     "шлём НАСТОЯЩИЕ нажатие и отпускание ПКМ через привязки ввода"),
			*Tgt->GetName(), U, Screen.X, Screen.Y);
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::RightMouseButton, IE_Pressed, 1.f));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::RightMouseButton, IE_Released, 0.f));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GPlayerMenuFakePeer(
	TEXT("asda.PlayerMenu.FakePeer"),
	TEXT("asda.PlayerMenu.FakePeer <userNum> [имя] — поставить фейкового соседа-игрока рядом с "
	     "собой (без сервера), чтобы было по кому кликать правой"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: фальшивый сосед с userNum живого игрока — меню отправило бы 4302/5282/4150/… постороннему.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.PlayerMenu.FakePeer"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!WS || !PC) { UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("нет мира/контроллера")); return; }
		const int32 U = A.Num() > 0 ? FCString::Atoi(*A[0]) : 7;
		const FString Nm = A.Num() > 1 ? A[1] : FString(TEXT("Сосед"));
		// ★ Ставим соседа ПЕРЕД КАМЕРОЙ, а не «рядом с павном»: в headless-PIE без сервера павна
		//   может не быть вовсе, и сосед оказывается вне кадра — тогда навести на него курсор
		//   нечем и проверка настоящим кликом невозможна (ложное «не работает»).
		FVector2D Base(0.f, 0.f);
		if (const AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(PC->GetPawn()))
		{
			Base = Me->GetGamePosXZ() + FVector2D(200.f, 0.f);   // 2 тайла в сторону
		}
		else if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
		{
			const FVector Ahead = Cam->GetCameraLocation() + Cam->GetCameraRotation().Vector() * 700.f;
			Base = AsdaCoord::UeToGame2D(Ahead);
		}
		WS->DebugSpawnFakePeer(U, Nm, Base);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPlayerMenuDump(
	TEXT("asda.PlayerMenu.Dump"),
	TEXT("asda.PlayerMenu.Dump — полный дамп ЖИВОГО окна 2000: видимость кнопки и подписи, "
	     "текст подписи, фактический прямоугольник"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		if (const UAsdaPlayerMenuSubsystem* S = PMSub(W))
		{
			UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] ДАМП:\n%s"), *S->DumpMenu());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPlayerMenuBlog(
	TEXT("asda.PlayerMenu.Blog"),
	TEXT("asda.PlayerMenu.Blog — отправить 6082 по текущей цели меню (кнопка 2063 в оригинале скрыта)"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		if (UAsdaPlayerMenuSubsystem* S = PMSub(W)) { S->ActionBlog(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GPlayerMenuFollowStop(
	TEXT("asda.PlayerMenu.FollowStop"),
	TEXT("asda.PlayerMenu.FollowStop — отменить следование (эталонный followTargetCancelReset, нотис 20310)"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		if (UAsdaPlayerMenuSubsystem* S = PMSub(W)) { S->CancelFollow(TEXT("консоль")); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Полный структурный дамп ЖИВОГО окна (закон full-structural-analysis-always): по каждому
//   пункту — видимость кнопки, видимость подписи, её текст; плюс фактический прямоугольник рамки.
//   Именно этим доказывается «пункт виден / подпись пуста», а не рассуждением.
FString UAsdaPlayerMenuSubsystem::DumpMenu() const
{
	FString S;
	S += FString::Printf(TEXT("окно 2000: %s, открыто=%s, цель «%s» (userNum=%d charId=%d)\n"),
		MenuWidget ? TEXT("создано") : TEXT("НЕ СОЗДАНО"), bMenuOpen ? TEXT("да") : TEXT("нет"),
		*TargetName, TargetUserNum, TargetCharId);
	if (!MenuWidget) { return S; }
	struct FRow { int32 Id; const TCHAR* Btn; const TCHAR* Cap; };
	static const FRow kRows[] = {
		{2001, TEXT("TRANTOOL_BUT01"), TEXT("TRANTOOL_BUT01_Text")},
		{2003, TEXT("TRANTOOL_BUT03"), TEXT("TRANTOOL_BUT03_Text")},
		{2055, TEXT("TRANTOOL_BUT09"), TEXT("TRANTOOL_BUT09_Text")},
		{2002, TEXT("TRANTOOL_BUT02"), TEXT("TRANTOOL_BUT02_Text")},
		{2004, TEXT("TRANTOOL_BUT04"), TEXT("TRANTOOL_BUT04_Text")},
		{2005, TEXT("TRANTOOL_BUT05"), TEXT("TRANTOOL_BUT05_Text")},
		{2006, TEXT("TRANTOOL_BUT06"), TEXT("TRANTOOL_BUT06_Text")},
		{2054, TEXT("TRANTOOL_BUT08"), TEXT("TRANTOOL_BUT08_Text")},
		{2062, TEXT("TRANTOOL_BUT11"), TEXT("TRANTOOL_BUT11_Text")},
		{2063, TEXT("TRANTOOL_BUT12"), TEXT("TRANTOOL_BUT12_Text")},
		{2070, TEXT("TRANTOOL_BUT13"), TEXT("TRANTOOL_BUT13_Text")},
		{2052, TEXT("TRANTOOL_BUT07"), TEXT("TRANTOOL_BUT07_Text")},
		{2053, TEXT("TRANTOOL_BUTDISABLE"), TEXT("TRANTOOL_BUTDISABLE_Text")},
		{2060, TEXT("TRANTOOL_BUT10"), TEXT("TRANTOOL_BUT10_Text")},
	};
	auto VisName = [](const UWidget* W) -> const TCHAR*
	{
		if (!W) { return TEXT("НЕТ УЗЛА"); }
		switch (W->GetVisibility())
		{
		case ESlateVisibility::Visible:              return TEXT("Visible");
		case ESlateVisibility::Collapsed:            return TEXT("Collapsed");
		case ESlateVisibility::Hidden:               return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible:     return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInvisible");
		default:                                     return TEXT("?");
		}
	};
	for (const FRow& R : kRows)
	{
		const UWidget* B = FindNode(MenuWidget, R.Btn);
		const UWidget* T = FindNode(MenuWidget, R.Cap);
		FString Txt;
		if (const UTextBlock* Tb = Cast<UTextBlock>(T)) { Txt = Tb->GetText().ToString(); }
		S += FString::Printf(TEXT("  %d %-22s кнопка=%-22s подпись=%-22s текст=«%s»\n"),
			R.Id, R.Btn, VisName(B), VisName(T), *Txt);
	}
	if (const UTextBlock* Id = IdText) { S += FString::Printf(TEXT("  2007 заголовок=«%s»\n"), *Id->GetText().ToString()); }
	FVector2D Mn, Mx;
	if (GetMenuRect(Mn, Mx))
	{
		S += FString::Printf(TEXT("  живой прямоугольник окна: (%.0f,%.0f)-(%.0f,%.0f) = %.0f×%.0f\n"),
			Mn.X, Mn.Y, Mx.X, Mx.Y, Mx.X - Mn.X, Mx.Y - Mn.Y);
	}
	else
	{
		S += TEXT("  живой прямоугольник окна НЕ снят (окно закрыто или геометрия ещё не построена)\n");
	}
	return S;
}

void UAsdaPlayerMenuSubsystem::ClickByNodeName(const TCHAR* Node)
{
	UButton* B = Cast<UButton>(FindNode(MenuWidget, Node));
	if (!B)
	{
		UE_LOG(LogAsdaPlayerMenu, Warning, TEXT("[меню игрока] узла «%s» в живом виджете нет"), Node);
		return;
	}
	UE_LOG(LogAsdaPlayerMenu, Log, TEXT("[меню игрока] диагностика: жмём САМУ кнопку «%s»"), Node);
	B->OnClicked.Broadcast();
}
