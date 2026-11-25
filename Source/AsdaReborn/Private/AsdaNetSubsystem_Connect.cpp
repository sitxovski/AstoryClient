// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_Connect.cpp — часть `AsdaNetSubsystem.cpp`: подключение к игровому серверу, порталы/перенос канала, вход в мир, анти-повтор (секрет).
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaNetSubsystem.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaNetSubsystemRecvInternal.h`. Шапка системы и ссылки на эталон — в `AsdaNetSubsystem.cpp` и заголовке класса.

#include "AsdaNetSubsystem.h"
#include "UI/AsdaCommunityClanSubsystem.h"   // ★ 01.11.2026 (clan-window): приём клановых пакетов панели «Предводитель»
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес по проводу u16 (1017, 5003, 4999, 6125, …)
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (4026/4028/5256)
#include "AsdaTimedSend.h"                     // ★ 15.10.2026: поток точной отправки (стендовая постановка 4028)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaSoulStoneSubsystem.h"   // ★ 06.11.2026: 5420, 5421, 5424, 5425
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"   // ★ гейт продолжения боя: обездвиживающие состояния
#include "AsdaConnection.h"
#include "Net/NetOpcodes.h"
#include "Misc/SecureHash.h"
#include "Login/AsdaLoginData.h"
#include "AsdaBytes.h"   // D4b: единые читалки little-endian вместо лямбд/открытых выражений в каждом хендлере
#include "AsdaPacketWriter.h"   // ★ волна 3 п.10: единый сборщик тел исходящих пакетов (вместо uint8 P[N] + WrI16)
// Проверка раскладки тела перед отправкой (хвост волны 3 п.10); определение и разбор — у `IsSendLinkUp`.
static bool AsdaPacketLayoutBroken(const FAsdaPacketWriter& W, uint16 Opcode, const TCHAR* Sender);
#include "Login/AsdaText.h"                 // имена класса/созвездия из loc-таблиц
// раскладка 0x3EF по itemType   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
// 6004 успех → BeginMapLoad (Load Manager)   ★ R3: комментарий снятого включения "World/AsdaLoadManagerSubsystem.h" (файл им не пользовался) — сохранён
// loc-строки форматов чата (20030/1187/1109/8200/…)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaChatWindowSubsystem.h"      // RankColor / SlotColor — таблицы каналов и рангов
#include "UI/AsdaInventorySubsystem.h"        // Дд6: FAsdaItemInstance::kRecordSize (57 б) — длина записи предмета
// 5055 отказ портала: нотисы 1096/1097 в системный чат   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNotice.h"              // ★ 27.09: отказы 5055/6201 — плашка 221 и пара «плашка + журнал» (общие точки)
#include "UI/AsdaChatSubsystem.h"       // ★ 27.09: ColGold() = эталонный −11514 отказов перехода
#include "UI/AsdaHotbarSubsystem.h"     // сохранение раскладки панели перед переходом (hotbar_SendAllLayouts)
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaPossessGate.h"      // ★ гейт действий при одержимости (SoulState==2), см. шапку заголовка
#include "AsdaOutgoingGate.h"               // ★ единый шлюз исходящих пакетов (E1, 14.09.2026)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (итог и `quit` диагностики asda.Gm.TestEntryHide)
#include "UI/AsdaEntryWindows.h"               // ★ 22.10.2026: окна при загрузке карты — список эталона
#include "UI/AsdaCampWarSubsystem.h"           // ★ 30.10.2026: 5055 код 1 → «Битва армий» (49530, сброс при уходе с карты кампвара)
#include "UI/AsdaCampWarBattleSubsystem.h"     // ★ 30.10.2026: боевая фаза — 4055/6013 при включённом HUD боя
#include "AsdaSelf.h"                       // ★ «это я?» — единая точка своего номера/уровня (E2, 14.09.2026)
#include "World/AsdaPlayerController.h"
#include "World/AsdaCoord.h"                 // ★ единый перевод UE↔игра (Аудит №3 B1) — для SendStop4008AtActor
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"                 // ★ С-Р1 ред. 1: GEngine->AddOnScreenDebugMessage — отказ второго барьера на экране
#include "Net/ChannelCipher.h"                 // ★ XyT: живой ключ ПРИЁМА (профиль + подстановка)
#include "HAL/PlatformFileManager.h"           // ★ XyT: метка времени файла ключа
#include "Misc/FileHelper.h"                   // ★ XyT: чтение файла ключа
#include "Misc/Parse.h"                        // ★ XyT: FParse::HexDigit
#include "Misc/ConfigCacheIni.h"               // ★ GM: список операторских префиксов имени из DefaultGame.ini
#include "World/AsdaKingCrownSubsystem.h"      // ★ 27.09.2026: приём 6126 «корона владельца добычи рейд-босса»
#include "AsdaLogSeal.h"                     // ★ 19.10.2026: логин учётки → .alog.meta журнала сессии (SetSessionAccount)
#include "AsdaAntiCheatSubsystem.h"           // ★ EAC Э1 (16.10.2026): клиентский Easy Anti-Cheat (модуль AsdaAntiCheat)
#include "UI/AsdaDisconnectPopupSubsystem.h"  // ★ EAC ред. 3: попап 8000 с текстом EAC (991xxx), когда строка утверждена
#include "World/AsdaCombatTrace.h"            // ★ 18.10.2026: журнал боя — этапы ОТПР/ПРИЁМ/ОТКАЗ боевых пакетов
#include "World/AsdaMobSkillSubsystem.h"      // ★ 30.10.2026: навыки мобов 8011/8012/8013
#include "UI/AsdaArrowAmmoSubsystem.h"        // ★ 31.10.2026: 5030 — расход стрел слота 10
#include "AsdaNetSubsystemInternal.h"   // ★ 22.10.2026 разнос: общее для частей AsdaNetSubsystem*.cpp (LogAsdaNet, чистые хелперы)
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaNetSubsystemRecvInternal.h"   // ★ R2: общие помощники и объявления частей AsdaNetSubsystem*.cpp (после включений пролога)

// ★ ФАЗА A2+A3 (world_entry_flow.md, реверс netSendEnterWorldHandshake@0x971E10): дисконнект от login →
//   коннект к GAME-серверу (свежее соединение, ObfCounter=0, крипта та же XOR-32) → enter-хендшейк opcode 20000.
// ★★★ 18.10.2026 (обрыв «due to invalid information» при смене карты): 5056 `CharPortalLogoutReq` — СТАРОЙ карте.
//   Эталон: переход берёт новый слот (`acquireNextSlot`, network.c:3877/3013), прежний остаётся открытым в `Field8`
//   (`nPrevSlot`); после 6004 `netSendAnticheatTsMask` (network.c:1277) шлёт 5056 в `Field8` — то есть ПРЕЖНЕМУ серверу,
//   а закрывает прежний слот только при логоне 4001/5058 (game_main.c:3100/3604). Сервер ставит флаг портального выхода
//   `+0x141` только в `CharPortalLogoutReq` (GameServer.exe.asm 0x52CBBC) — и DisConnectUser старой карты идёт веткой
//   «PortalLogout» (`UPPortalLogOut`, запись входа JoyLog1.Loginfo остаётся открытой). Порт слал 5056 по НОВОМУ сокету:
//   старая карта выходила обычным `UPLogOutGame` → Loginfo закрыта → DBManager P50 отказывал портальному логону 2089
//   («IP не совпал с записью входа») → кик 4101 код 5. [ПОРТ] Отличие от эталона: 5056 уходит прежнему серверу прямо
//   перед закрытием его сокета, а не после 6004 — два защищённых канала AsdaSecure/EAC одновременно порт не держит.
//   Для сервера порядок тот же: раскладка панели → 5056 → закрытие старого → 20000 на новом. Вход с выбора персонажа
//   (прежний сокет — LoginManager) 5056 не шлёт: LoginManager неизвестные коды пропускает (`default: continue`).
//   Откат: `asda.Net.PortalLogoutOnOldConn 0` — прежнее поведение (5056 по новому соединению после 6004).
ASDA_DEV_CVAR(int32, CVarAsdaNetPortalLogoutOnOldConn,
	TEXT("asda.Net.PortalLogoutOnOldConn"), 1,
	TEXT("1 = при переходе между картами/каналами 5056 CharPortalLogoutReq уходит СТАРОМУ game-серверу перед закрытием сокета ")
	TEXT("(старая карта сохраняет портальным выходом, запись входа остаётся открытой); 0 = как прежде, по новому соединению после 6004."),
	ECVF_Default);

// ★★★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА] (жалоба Player52 25.10, тот же сбой у Player17 21.10): признак портального входа
//   `bPortalPending` снимался только отправкой 5058 — переживал разрыв, выход на логин и перелогин; после перелогина
//   Load Manager слал 5058 вместо 4001, и сервер не присылал 5251/6057/6106 (пустые окна навыков и питомцев).
//   Теперь признаки перехода снимает `ClearPortalTransferState` (объявление — AsdaNetSubsystem.h). 0 — прежнее поведение.
static ASDA_DEV_CVAR(int32, CVarAsdaNetPortalPendingReset,
	TEXT("asda.Net.PortalPendingReset"), 1,
	TEXT("1 = признак портального входа 5058, тайл прибытия и отложенный реконнект снимаются при разрыве, полном сбросе сессии, ")
	TEXT("входе из выбора персонажа и по страховке 30 с при мёртвом соединении; 0 = как прежде (снимает только отправка 5058)."),
	ECVF_Default);

// ★★★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: НЕБЛОКИРУЮЩЕЕ подключение к game-серверу со сроком kGameConnectWaitSec (20 с).
//   Разбор журналов игроков 19–25.10 (LogDrop, 220 подключений к 15601): 210 состоялись за < 0.5 с, 2 — за ≈1.19 с (спас
//   повтор SYN через 1 с), 8 оборваны ровно на 3.0 с пределом `asda.Net.ConnectTimeoutSec` («таймаут подключения … сервер
//   не отвечает»), в т.ч. платный телепорт Player52 25.10 16:53:21 и Player17 21.10 21:02:49; на сервере за эти секунды
//   приёма нет (Log_UserMemory/Map*, Login_Check — ни AcceptExUser, ни Connect). Предел 3 с отсекал повторы SYN ОС
//   (Windows 11: 0, 1, 3, 7, 15 с). [ОРИГИНАЛ] `CNetwork_Connect@0x12AA840` ждёт вердикт ОС (WaitForSingleObject INFINITE).
//   Порт ждёт до 20 с, но игровой поток не стоит (`FAsdaConnection::ConnectAsync`, подтверждение — `Tick`, bAwaitGameConnect).
//   0 — прежний блокирующий `Connect` с пределом asda.Net.ConnectTimeoutSec.
static ASDA_DEV_CVAR(int32, CVarAsdaNetGameConnectAsync,
	TEXT("asda.Net.GameConnectAsync"), 1,
	TEXT("1 = подключение к game-серверу (вход в мир, портал, канал, телепорт) неблокирующее, срок 20 с (повторы SYN ОС успевают); ")
	TEXT("0 = как прежде: блокирующий Connect с пределом asda.Net.ConnectTimeoutSec (3 с)."),
	ECVF_Default);

// ★★★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: неудача подключения к game-серверу = разрыв с нотисом 1001 (`HandleGameConnectFailed`).
//   Прежде — только `State = CharSelect`: после неудачного телепорта игрок стоял в мире без связи, чат и NPC молча не работали
//   (журнал Player52 25.10 16:53:24–16:54:01), а гейт перехода держал отправки до страховки 30 с. 0 — прежнее поведение.
static ASDA_DEV_CVAR(int32, CVarAsdaNetGameConnectFailDisconnect,
	TEXT("asda.Net.GameConnectFailDisconnect"), 1,
	TEXT("1 = неудачное подключение к game-серверу оформляется разрывом: сброс сессии, попап обрыва 8000 с нотисом 1001 в мире ")
	TEXT("(ОК → экран входа) или окно кика логина на сцене входа; 0 = как прежде (State = CharSelect без сообщения)."),
	ECVF_Default);

// Аксессор ключа `asda.Net.GameConnectFailDisconnect` для других .cpp (имя ключа вне этого файла не пишем — Shipping).
bool AsdaNetGameConnectFailDisconnectOn()
{
	return CVarAsdaNetGameConnectFailDisconnect.GetValueOnGameThread() != 0;
}

// ★ 26.10.2026 (ревизия wave_1025d 4.2) [ИСПРАВЛЕНИЕ ПОРТА]: ветку «неудача подключения» в `HandleConnectionClosed` открывает только
//   OnClosed НЕСОСТОЯВШЕГОСЯ подключения (`FAsdaConnection::ConsumeClosedByConnectFail`), а `HandleGameConnectFailed` не затирает
//   уже известную причину разрыва (`LastKickCode` ≠ 0xFF) кодом 4. 0 — как 25.10: любое закрытие при ожидании → код 4.
static ASDA_DEV_CVAR(int32, CVarAsdaNetConnectFailOnlyPending,
	TEXT("asda.Net.ConnectFailOnlyPending"), 1,
	TEXT("1 = неудачей подключения считается только закрытие самого несостоявшегося подключения, известная причина разрыва ")
	TEXT("кодом 4 не затирается; 0 = как 25.10 (любое закрытие при ожидании подключения — код 4 и нотис 1001)."),
	ECVF_Default);

// Аксессор ключа `asda.Net.ConnectFailOnlyPending` для других .cpp (имя ключа вне этого файла не пишем — Shipping).
bool AsdaNetConnectFailOnlyPendingOn()
{
	return CVarAsdaNetConnectFailOnlyPending.GetValueOnGameThread() != 0;
}

// ★ 25.10.2026: см. объявление (AsdaNetSubsystem.h).
void UAsdaNetSubsystem::ClearPortalTransferState(const TCHAR* Why)
{
	if (CVarAsdaNetPortalPendingReset.GetValueOnGameThread() == 0) { return; }
	if (!bPortalPending && !bPendingEnterWorld && PortalTileX < 0 && PortalTileZ < 0) { return; }
	UE_LOG(LogAsdaNet, Log,
		TEXT("[переход] признаки перехода сняты (%s): портальный вход 5058=%d, тайл прибытия (%d,%d), отложенный реконнект=%d — следующий вход обычный 4001"),
		Why, bPortalPending ? 1 : 0, PortalTileX, PortalTileZ, bPendingEnterWorld ? 1 : 0);
	bPortalPending     = false;
	PortalTileX        = -1;
	PortalTileZ        = -1;
	bPendingEnterWorld = false;
}

// ★ 25.10.2026: см. объявление (AsdaNetSubsystem.h).
void UAsdaNetSubsystem::HandleGameConnectFailed(const TCHAR* Why)
{
	// Переход из мира (портал 5055, канал 6015, призыв 6013, варп 6201, свиток, 4055) взводит гейт перехода; вход из выбора
	//   персонажа (1021) — нет. По нему и выбирается, где показать нотис (эталон: попап 1001 / ошибка 1001 при входе).
	const bool bFromWorld = bTransferInProgress;
	bAwaitGameConnect   = false;
	AwaitGameConnectSec = 0.f;
	UE_LOG(LogAsdaNet, Warning,
		TEXT("[переход] подключение к game-серверу %s:%d не состоялось (%s) → %s, нотис 1001 [ОРИГИНАЛ: Show_DisconnectPopup(1001), причина 4]"),
		*GameServerIP, GameServerPort, Why, bFromWorld ? TEXT("разрыв в мире (попап 8000)") : TEXT("отказ входа (окно кика логина)"));
	if (Conn) { Conn->Disconnect(); }   // недоделанный сокет — ОС (обратного вызова у Disconnect нет)
	if (CVarAsdaNetGameConnectFailDisconnect.GetValueOnGameThread() == 0)
	{
		State = EAsdaNetState::CharSelect;   // прежнее поведение порта: без сообщения
		return;
	}
	if (bFromWorld)
	{
		// В мире — ветка игрового обрыва: `OnGameDisconnected` (сброс сессии) и попап 8000 с текстом 1001 вместо таблицы причин.
		if (UAsdaDisconnectPopupSubsystem* Pop = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaDisconnectPopupSubsystem>() : nullptr)
		{
			Pop->SetNextNotice(1001);
		}
		bInGameConnected = true;   // признак «был в мире» для развилки HandleConnectionClosed (сам сокет уже закрыт)
	}
	// [ОРИГИНАЛ] `g_nDisconnectReason = 4` — неудача подключения к канал-серверу (на логине порт показывает по ней 1001, см.
	//   AsdaLoginGameMode). ★ 26.10.2026 (4.2): уже известная причина (4101 / EAC пришли раньше) не затирается.
	if (LastKickCode == 0xFF || CVarAsdaNetConnectFailOnlyPending.GetValueOnGameThread() == 0) { LastKickCode = 4; }
	HandleConnectionClosed();
}

void UAsdaNetSubsystem::EnterGameWorld()
{
	if (!Conn) return;
	// ★★★ 18.10.2026: уходим с game-сервера (не с LoginManager) — сперва портальный выход 5056 старой карте (см. выше).
	if (CVarAsdaNetPortalLogoutOnOldConn.GetValueOnGameThread() != 0 && !bLoginConnActive && Conn->IsConnected())
	{
		UE_LOG(LogAsdaNet, Log, TEXT("[переход] 5056 портальный выход — прежнему game-серверу перед закрытием сокета"));
		// ★ Ревизия 18.10 (ВАЖНО): закрытие мягкое — при непрочитанных пакетах старой карты closesocket дал бы RST и мог
		//   потерять 5056; FIN после 5056 гарантирует серверу порядок «5056, затем разрыв». Эталон держит старый сокет до логона.
		if (SendGameAck5056()) { Conn->DisconnectGraceful(0.5f); }
		else                   { Conn->Disconnect(); }
	}
	else
	{
		Conn->Disconnect();             // A2: закрыть прежнее соединение (login или game)
	}
	bEacReturnToCharSelectSent = false; // ★ EAC R3-01: новое игровое соединение — ожидание «закрытия после 1028» снято
	ObfCounter = 0;                     // свежее соединение → анти-реплей-счётчик с нуля
	bLoggedIn = false; ResetNetHeartbeatStamp();   // новый сокет → отсчёт пинга заново; на game-канал 1000 идёт только после 6004 (bInGameConnected)
	bLoginConnActive = false;           // login-сокет закрыт → его пинг-цикл остановлен
	bAwaitGameConnect  = false;         // прежнее ожидание (если было) отменяется этим коннектом
	AwaitGameConnectSec = 0.f;
	// ★ Р-2 (10.10.2026): прогон без сети — game-сервер тоже не трогаем (адрес в журнал не пишем).
	if (IsNoNetRun())
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("EnterGameWorld: ключ -AsdaNoNet — подключение к game-серверу не выполняется"));
		State = EAsdaNetState::CharSelect;
		return;
	}
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: неблокирующее подключение со сроком kGameConnectWaitSec (шапка ключа
	//   asda.Net.GameConnectAsync). true — подключение начато (или уже готово); подтверждение и хендшейк 20000 — в Tick.
	const bool bAsync = CVarAsdaNetGameConnectAsync.GetValueOnGameThread() != 0;
	const bool bOk = bAsync
		? Conn->ConnectAsync(GameServerIP, GameServerPort, /*SecureRole=*/0, kGameConnectWaitSec)
		: Conn->Connect(GameServerIP, GameServerPort, /*SecureRole=*/0);   // ★ В7d: роль — игровой сервер (GameServer/Indun)
	UE_LOG(LogAsdaNet, Log, TEXT("EnterGameWorld: коннект game-сервер %s:%d -> %s%s"), *GameServerIP, GameServerPort,
		bOk ? TEXT("OK") : TEXT("FAIL"), bAsync ? TEXT(" (неблокирующий, срок 20 с)") : TEXT(""));
	// ★★ МЕЛК-2 (10.10.2026): соединение появилось — идущий офлайн-самотест прерывается (итог, `quit`, вето отправок).
	//   У неблокирующего подключения — когда оно подтвердится (Tick, ветка bAwaitGameConnect).
	if (bOk && Conn->IsConnected()) { FAsdaOutgoingGate::OnConnectionAppeared(this, TEXT("game-сервер")); }
	if (!bOk)
	{
		// Эталон: ошибка коннекта → нотис loc 1001 (переход — попап обрыва, вход из выбора персонажа — ошибка входа).
		UE_LOG(LogAsdaNet, Warning, TEXT("EnterGameWorld: game-сервер недоступен → откат в char-select (нотис loc1001)"));
		HandleGameConnectFailed(TEXT("подключение отвергнуто сразу"));   // ★ 25.10.2026: разрыв с нотисом 1001 (при ключе 0 — прежнее)
		return;
	}
	State = EAsdaNetState::EnteringWorld;
	// ★★★ 2026-09-02 (К-4, часть «точка входа в мир»): согласование с НЕБЛОКИРУЮЩИМ коннектом.
	//   Пока `FAsdaConnection::Connect` блокирующий, `true` означает «сокет уже соединён» — тогда
	//   хендшейк 20000 уходит В ЭТОМ ЖЕ КАДРЕ, ровно как раньше (нулевая разница в поведении).
	//   Когда сокет станет неблокирующим, `true` будет означать лишь «SYN отправлен», и слать
	//   хендшейк в несоединённый сокет нельзя — тогда взводим ожидание, а `Tick` отправит A3 в тот
	//   кадр, когда соединение подтвердится. Ни одного изменения в `AsdaConnection` для этого не
	//   нужно: достаточно уже существующего `IsConnected()`.
	//   ⚠ Барьера «пауза перед первым пакетом» здесь НЕТ и не заводится: он относится к паре
	//     connect→1003 у LOGIN-сервера (`AsdaLoginGameMode::DoLogin`, 0,5 с против гонки ProcConnect
	//     и кика 4101 code 5). У game-сервера эталон шлёт 20000 сразу после коннекта.
	if (Conn->IsConnected())
	{
		SendEnterWorldHandshake();      // A3
		return;
	}
	bAwaitGameConnect   = true;
	AwaitGameConnectSec = 0.f;
	UE_LOG(LogAsdaNet, Log,
		TEXT("EnterGameWorld: коннект начат, но сокет ещё не соединён — хендшейк 20000 отложен до подтверждения"));
}

// ★ Остановить автопуть и движение на время перехода между картами и каналами.
//   Зачем: соединение уже новое, а игрок на нём ещё не опознан (логон уходит после загрузки).
//   Любой пакет движения в этот промежуток сервер принимает за мусор и рвёт связь — именно так
//   ловилась «карта не загрузилась, вокруг пустота». Плюс автопуть, не остановившись, повёл бы
//   персонажа к порталу уже на новой карте.
void UAsdaNetSubsystem::StopTravelAndMovementForTransfer(const TCHAR* Reason)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	// ★ 22.10.2026: загрузка карты эталона (портал 5055 / призыв 6013 / канал 6015 / варп 6201 / свиток) — шаги 0 и 11
	//   (game_main.c:3540-3724) закрывают свой список окон, пока соединение прежней карты ещё живо (автоотказ заявки в
	//   отряд уходит ему). Окна, которых в списке нет, эталон держит — их вернёт `ReopenTravelKeptWindows` после входа.
	AsdaEntryWindows::ApplyMapLoadWindowCloses(GI, Reason, /*bLevelReload*/ true);
	// ★ Заявку автопути НЕ снимаем: цель может быть на той карте, куда мы как раз идём, и после
	//   загрузки поход обязан продолжиться сам. Тик автопути умеет ждать («карта ещё грузится»),
	//   а на время перехода он дополнительно замирает по признаку `bTransferInProgress`.
	//   Останавливаем только текущее движение — иначе персонаж бежал бы в пустоту, пока грузится
	//   новая карта, а пакеты движения всё равно не ушли бы (их держит гейт отправки).
	if (UAsdaMinimapSubsystem* Mini = GI->GetSubsystem<UAsdaMinimapSubsystem>())
	{
		if (Mini->IsTravelRequested())
		{
			UE_LOG(LogAsdaNet, Log, TEXT("[переход] автопуть приостановлен (заявка сохранена)"));
		}
	}
	if (UWorld* W = GI->GetWorld())
	{
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
		{
			PC->StopMovementForMapChange();
		}
	}
	UE_LOG(LogAsdaNet, Log, TEXT("[переход] автопуть и движение остановлены: %s"), Reason);
}

// ★ ПЕРЕД ЛЮБЫМ ПЕРЕХОДОМ оригинал сохраняет раскладку панели быстрого доступа: во всех четырёх
//   ветках (портал 5055, призыв 6013, смена канала 6015, короткий варп 6201) он шлёт `hotbar_
//   SendAllLayouts` — шесть страниц квикслотов — ДО того, как рвать соединение. Иначе слоты,
//   переставленные после последнего сохранения, теряются при переходе.
void UAsdaNetSubsystem::FlushHotbarBeforeTransfer(const TCHAR* Reason)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return; }
	Hb->SendAllPageLayouts();
	UE_LOG(LogAsdaNet, Log, TEXT("[переход] раскладка панели сохранена перед: %s"), Reason);
}

// ★ ФАЗА A3 enter-хендшейк game-серверу (opcode 20000). Тело[51] = [8б анти-реплей секрет][16б MD5(payload27)][27б payload].
//   payload[27] = init 0xFF (sub_92604D→sub_9725B0), затем dword[0]=mapId, dword[4]=charId, b24=портал(-1), b25/b26=campwar(0).
bool UAsdaNetSubsystem::SendEnterWorldHandshake()
{
	if (!IsSendLinkUp()) { UE_LOG(LogAsdaNet, Warning, TEXT("Enter-хендшейк: нет game-соединения")); return false; }
	FAsdaPacketWriter W(27, 0xFF);                               // sub_9725B0: все 7 dword = -1
	const uint32 MapId  = (EnterMapId >= 0) ? (uint32)EnterMapId : 0u;
	const uint32 CharId = (uint32)SelectedCharId;
	W.I32(0, (int32)MapId);    // dword[0] = mapId
	W.I32(16, (int32)CharId);   // dword[4] = charId
	// ★ b24 = g_byPortalChannelParam: при входе с char-select = 0xFF (init), при переходе на канал — байт[25]
	//   пакета 6015 (эталон netHandleChannelTransfer). Значение хранится в PortalChannelParam.
	W.U8(24, PortalChannelParam);
	// ★★★ b25/b26 — ЭХО ТОКЕНОВ КАМПВАРА. Оригинал (`sub_92277C`/`sub_93BC36`) возвращает сюда
	//   два байта, которые сервер прислал в портальном пакете 5055 (+51/+52) и которые клиент
	//   у себя не разбирает. Так сервер на НОВОЙ карте узнаёт состояние кампвара игрока.
	//   Пока портала не было, оба оставались 0xFF («не участвует») — это же значение стоит
	//   после init-заполнения, и оно верно при входе с выбора персонажа.
	W.U8(25, CampWarToken1);
	W.U8(26, CampWarToken2);

	// Д6: единый сборщик тела [секрет8][MD5(payload,27)16][payload27]; enter-хендшейк = a6/bPad=false.
	if (AsdaPacketLayoutBroken(W, (uint16)OP_ACCEPT_EX_USER, TEXT("SendEnterWorldHandshake"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_ACCEPT_EX_USER, W.Data(), W.Num(), /*bPad*/false);
	UE_LOG(LogAsdaNet, Log, TEXT("Enter-хендшейк 20000: mapId=%u charId=%u кампвар=(%02X %02X) -> %s"),
		MapId, CharId, CampWarToken1, CampWarToken2, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 20000 (enter-хендшейк)
}

// ★ Анти-реплей-подпись obfTick (obftick_antireplay_spec.md, реверс buildAntiCheatToken@0x955C10 + сервер
//   sub_5240C0/sub_524390). Кодирует монотонный counter в 8-байтный QPC-таймстамп по фиксированным битовым
//   позициям; сервер декодирует, вычитает базу 0x57A7A5 и требует строгого возрастания. Секрет нулями →
//   отрицательный counter → «Secret Error» → кик 4101 code=3 (это был баг char-list/channel-select).
// ★★★ 15.10.2026 (ревизия сети PORT-RHYTHM, БЛОКЕР-1): АНТИ-РЕПЛЕЙ-НОМЕР — В МОМЕНТ УХОДА НА ПРОВОД.
//   Сервер принимает защищённый пакет, только если номер больше прошлого (`sub_524390`, GameServer.c:111065-111085; иначе
//   «Secret Error», пакет выброшен, `++errCount`). Удар 4028 ставится к дедлайну заранее (до 250 мс): получи он номер при
//   сборке, любой защищённый пакет игрового потока в этом окне (зелье 5028, 4026, предметы — 140 точек `SendSecured` и 21
//   прямой вызов) ушёл бы раньше с бо́льшим номером, а удар — с меньшим, и сервер молча выбросил бы удар (модель ревизора:
//   86 из 200). Поэтому при сборке тела номер НЕ выдаётся (байты [0..7] = 0), его вписывает соединение под замком прямо
//   перед сборкой кадра — для ВСЕХ защищённых пакетов одинаково (`ConnSendSecret`, `SendPacketAt(…, true)`). MD5 тела по
//   [0..7] не считается — порядок записи безопасен. Под перехватчиком стенда/самотеста номер выдаётся при сборке, как прежде
//   (сокета нет; так тела стенда байт в байт прежние). Откат: `asda.Net.SecretAtSend 0` — номер при сборке.
static ASDA_DEV_CVAR(int32, CVarAsdaNetSecretAtSend,
	TEXT("asda.Net.SecretAtSend"), 1,
	TEXT("1 = анти-реплей-номер защищённого пакета выдаётся в момент ухода на провод (порядок номеров = порядок на проводе); ")
	TEXT("0 = при сборке тела, как прежде."),
	ECVF_Default);

bool UAsdaNetSubsystem::IsSecretAtSend()
{
	return CVarAsdaNetSecretAtSend.GetValueOnAnyThread() != 0;
}

void UAsdaNetSubsystem::FillAntiReplaySecret(uint8* Out8)
{
	if (IsSecretAtSend() && !SendCaptureForStand)
	{
		FMemory::Memzero(Out8, 8);   // номер впишет соединение в момент ухода (`FAsdaConnection::SendFrameCore`)
		return;
	}
	FillAntiReplaySecretNow(Out8);
}

bool UAsdaNetSubsystem::DebugAttachTestWire(TFunction<void(uint16, const uint8*, int32)> Sink)
{
	if (!Conn || Conn->IsConnected() || !Sink) { return false; }   // только офлайн: живое соединение не подменяем
	Conn->DebugAttachTestWire([Sink](uint16 Op, const uint8* B, int32 N) { Sink(Op, B, N); });
	return true;
}

void UAsdaNetSubsystem::DebugDetachTestWire()
{
	CancelPending4028(TEXT("самотест: провод снят"));
	if (Conn) { Conn->DebugDetachTestWire(); }
}

bool UAsdaNetSubsystem::ConnSendSecret(uint16 Opcode, const uint8* Body, int32 BodyLen, uint16 HdrA, uint16 HdrB, bool bPad)
{
	if (!Conn) { return false; }
	return IsSecretAtSend() ? Conn->SendPacketSecret(Opcode, Body, BodyLen, HdrA, HdrB, bPad)
	                        : Conn->SendPacket(Opcode, Body, BodyLen, HdrA, HdrB, bPad);
}

void UAsdaNetSubsystem::FillAntiReplaySecretNow(uint8* Out8)
{
	// 30 абсолютных битовых позиций (bitPosTable, чётные индексы), подтверждены сервером (дельты sub_5240C0).
	static const int32 kBitPos[30] = {
		3, 4, 8, 11, 12, 14, 15, 16, 18, 20,
		21, 25, 26, 27, 28, 30, 32, 33, 36, 42,
		43, 45, 47, 50, 52, 53, 54, 57, 60, 62
	};
	const int32 Counter = ObfCounter;                 // post-increment (ровно один раз на защищённый пакет)
	ObfCounter = (Counter + 1 > 0x3FFFFFFF) ? 0 : (Counter + 1);

	uint64 MaskValue = (uint64)0x0057A7A5u + (uint64)(uint32)Counter;   // база + счётчик
	// obfTick = QPC-таймстамп (в UE Cycles64 на Windows = QueryPerformanceCounter); шумовые биты вне позиций
	//   значения не имеют — сервер читает только позиции bitPosTable.
	uint64 ObfTick = FPlatformTime::Cycles64();
	for (int32 i = 0; i < 30; ++i)
	{
		const uint64 Shifted = (uint64)1 << kBitPos[i];
		if ((MaskValue & 1) == 1) ObfTick |=  Shifted;
		else                      ObfTick &= ~Shifted;
		MaskValue >>= 1;
		if (MaskValue == 0) break;   // ★ ранний выход: старшие позиции сохраняют шум QPC (1:1 с оригиналом)
	}
	FMemory::Memcpy(Out8, &ObfTick, 8);
}

// ★★ Д6: единый сборщик защищённого пакета (см. заголовок). Байт-в-байт с ручными отправителями.
// ★★★ ГЕЙТ ИСХОДЯЩИХ В РЕЖИМЕ XyT.
//   Их сборка сервера знает НЕ ВСЕ опкоды нашего клиента. Пакет с неизвестным опкодом падает у
//   них в ветку `default`, которая инкрементит счётчик неопознанных; при переполнении сервер
//   выбрасывает игрока (наблюдалось живьём: вход в мир прошёл, через 5 с пришёл kick 4101 code=15
//   и разрыв, а лаунчер показал «Disconnected»).
//   Список составлен СРАВНЕНИЕМ живого трафика: их клиент за долгие сессии (340 и 1211 keepalive)
//   шлёт только 1000/4001/4006/4008/4122/4124/4301/5466/9915/20000, а наш добавляет служебные
//   запросы своих подсистем. Блокируем именно их — на игру они не влияют.
//   Список правится без пересборки: `asda.XyT.BlockedOpcodes`.
static ASDA_DEV_CVAR(FString, CVarXytBlockedOpcodes,
	TEXT("asda.XyT.BlockedOpcodes"),
	TEXT("11004,6080,6538"),
	TEXT("Опкоды, которые НЕ отправляются в режиме XyT (их сервер их не знает и кикает за это)."));

bool UAsdaNetSubsystem::IsOpcodeBlockedXyt(uint16 Opcode) const
{
	if (!bXytMode) { return false; }
	const FString List = CVarXytBlockedOpcodes.GetValueOnGameThread();
	if (List.IsEmpty()) { return false; }
	TArray<FString> Parts;
	List.ParseIntoArray(Parts, TEXT(","), true);
	for (const FString& P : Parts)
	{
		if (FCString::Atoi(*P.TrimStartAndEnd()) == (int32)Opcode) { return true; }
	}
	return false;
}
