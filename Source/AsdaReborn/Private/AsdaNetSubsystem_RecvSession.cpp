// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_RecvSession.cpp — часть `AsdaNetSubsystem.cpp`: приёмник пакетов: вход, выбор/создание/удаление персонажа, вход в мир, перенос канала, возрождение-вход, спавн себя, облик.
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
#include "Items/AsdaItemTableSubsystem.h"   // раскладка 0x3EF по itemType
// 6004 успех → BeginMapLoad (Load Manager)   ★ R3: комментарий снятого включения "World/AsdaLoadManagerSubsystem.h" (файл им не пользовался) — сохранён
// loc-строки форматов чата (20030/1187/1109/8200/…)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaChatWindowSubsystem.h"      // RankColor / SlotColor — таблицы каналов и рангов
#include "UI/AsdaInventorySubsystem.h"        // Дд6: FAsdaItemInstance::kRecordSize (57 б) — длина записи предмета
#include "UI/AsdaMessageSubsystem.h"    // 5055 отказ портала: нотисы 1096/1097 в системный чат
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

// ★ [ИСПРАВЛЕНИЕ 05.11.2026] (баг игроков «имена соседей после переподключения», самотест asda.Overhead.SelfTestRelog): 4005, пришедший
//   ДО своего 4003 (номер сессии ещё неизвестен), различается по charId из 1017 (`SelectedCharId`, известен до входа в мир) — а не по
//   «первый 4005 = свой»: тот съедал соседа, чей 4005 опередил наш (сосед без тела до следующего входа в обзор). 0 — прежнее.
static ASDA_DEV_CVAR(int32, CVarSelfDetectByCharId,
	TEXT("asda.Net.SelfDetectByCharId"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 05.11.2026] 1 = 4005 до своего 4003 признаётся своим только по charId из 1017; ")
	TEXT("0 = прежнее (первый 4005 до 4003 — свой)."),
	ECVF_Default);

// ★★★ ДЕФЕКТ ОРИГИНАЛА (КРИТ) в приёмнике 1023 `charInfoParsePacket@0xF02B70`: имя персонажа
//   соулмейта копируется ТРИДЦАТЬЮ ДВУМЯ байтами вместо двадцати (`push 0x20` по ВА 0x00F02E17
//   против `push 0x14` в собственной ветке `charSelect_FillSlotRecords@0xF06310`, ВА 0x00F064C4).
//   Колонка `Charname` — `varchar(20)`, поэтому лишние 12 байт затирают уже разобранные поля
//   `CCharacter+1076..1087` со сдвигом −2: `+1076 Charidx ← rec+21 Clan`, а `Clan` процедура
//   `UPPreSoulMateChar` жёстко обнуляет (`SET @Clan = 0`). Итог: Charidx всегда 0, а
//   `playerFindSubRecordByType@0xF0A210` отсекает всё, что вне 10..12 ⇒ персонаж соулмейта
//   не находился НИКОГДА и фича не работала ни в одной сборке (проверено сигнатурным сканом
//   `66 89 91 6E 04 00 00` в 7 образах клиента; в XYT/Arabic копируется даже 0x3D=61 байт).
//   Мы читаем 20 байт (закон `fix-original-defects`). 1 = воспроизвести дефект эталона
//   ПОБАЙТНО (для сверки с оригиналом) — тогда запись, как и в оригинале, будет отброшена.
static ASDA_DEV_CVAR(int32, CVarSoulmateLegacyNameOverrun,
	TEXT("asda.Soulmate.LegacyNameOverrun"), 0,
	TEXT("1 = разбирать 1023 с дефектом эталона (имя 32 б затирает Charidx нулём — персонаж соулмейта не появится); 0 = 20 б."),
	ECVF_Default);

// ★ 02.11.2026 (волна jl_presm) [ИСПРАВЛЕНИЕ порта] «1025 код 0» после 1023 этой транзакции = сбой статуса соулмейта
//   (`UPPreSMConnect`), а не провал списка персонажей — разбор у ветки 1025. 0 = прежняя трактовка (всегда «список не придёт»).
static ASDA_DEV_CVAR(int32, CVarCode0AfterSoulList,
	TEXT("asda.Login.Code0AfterSoulList"), 1,
	TEXT("1025 код 0 после 1023: 1 = «статус соулмейта не получен» (список уже есть), 0 = как раньше — событие провала списка 1005."),
	ECVF_Default);

// ★★★ 26.10.2026 [ОРИГИНАЛ] (жалоба игроков «канал в игре не меняется», отчёт координатору channel-agent): после 6015
//   `netHandleChannelTransfer@0xF49660` (network.c:43886-43926) эталон ставит ТО ЖЕ состояние 11, что портал 5055
//   (`g_bPortalChannelPending1/2 = 1`, `g_nSpawnSelfFieldA/B` = клетка +18/+20, `g_nCharMgrState = 11`), и после загрузки
//   карты шлёт портальный логон 5058 `net_sendSpawnFieldSelect@0xF489E0` (game_main.c:3607) с этой клеткой. Порт слал 4001 —
//   сервер ставил по позиции из базы (журнал GM 21.10 21:11: Meet 6014 → 6015 клетка (445,157), 4003 — (155,353)).
//   0 — прежнее поведение порта (после 6015 обычный 4001).
static ASDA_DEV_CVAR(int32, CVarAsdaNetChannelTransferPortalLogon,
	TEXT("asda.Net.ChannelTransferPortalLogon"), 1,
	TEXT("1 = после 6015 (смена канала / Meet оператора) вход на новом сервере портальным 5058 с клеткой из пакета, как у оригинала; ")
	TEXT("0 = как прежде: обычный 4001 (позиция из базы)."),
	ECVF_Default);

// ★ R2 (10.11.2026): приёмник области «Session» — вход, выбор/создание/удаление персонажа, вход в мир, перенос канала, возрождение-вход, спавн себя, облик.
//   Группы `case` перенесены из `UAsdaNetSubsystem::HandlePacket` ДОСЛОВНО и в исходном порядке; `bHandled` = false только
//   в служебной ветке `default:` в конце — опкод не из этой области, диспетчер идёт дальше.
void UAsdaNetSubsystem::HandlePacket_Session(const Asda::ParsedPacket& Pkt, bool& bHandled)
{
	bHandled = true;
	switch ((NetOpcode)Pkt.Opcode)
	{
	case OP_NET_RECV_LOGIN_RESPONSE:        // 1004 0x3EC — УСПЕХ логина (Net_RecvLoginResponse@0xF167D0, тело 8б)
	{
		// body[0]=status (1=OK→server-select), [1]=accountSub, [2..5]=u32 param (UserIndex/сессия).
		if (Pkt.Body.size() < 1) { UE_LOG(LogAsdaNet, Warning, TEXT("Login resp too short")); break; }
		const uint8 Status = Pkt.Body[0];
		if (Status == 1)
		{
			if (NetNeedLen(Pkt.Body, 6, (int32)Pkt.Opcode))
			{
				UserIndex = AsdaBytes::RdU32(&Pkt.Body[2]);
			}
			// ★ char-list userNo = UserIndex low word + session-байт из 1004 body[9]/[10] (сверено с захватом).
			{
				const uint8 B9  = NetNeedLen(Pkt.Body, 10, (int32)Pkt.Opcode) ? Pkt.Body[9]  : 0;
				const uint8 B10 = NetNeedLen(Pkt.Body, 11, (int32)Pkt.Opcode) ? Pkt.Body[10] : 0;
				CharListUserNo = (UserIndex & 0xFFFFu) | ((uint32)B9 << 16) | ((uint32)B10 << 24);
			}
			UE_LOG(LogAsdaNet, Log, TEXT("Login OK (accountSub=%u UserIndex=%u CharListUserNo=0x%08X) -> server-select"),
				NetNeedLen(Pkt.Body, 2, (int32)Pkt.Opcode) ? Pkt.Body[1] : 0, UserIndex, CharListUserNo);
			State = EAsdaNetState::CharSelect;
			// ★ 19.10.2026 (журналы релиза): вход принят — логин этой попытки становится учёткой журнала сессии
			//   (Sessions\<файл>.alog.meta; перезаход другой учёткой перезапишет). Вне ритейл-политики журналов — ничего.
			if (!PendingSessionAccount.IsEmpty()) { AsdaLogSeal::SetSessionAccount(PendingSessionAccount); }
			// char-list НЕ шлём тут — только после выбора канала (GameMode::OnChannelSelected), как в эталоне.
		}
		else if (Status == 4)
		{
			// ★ Overlap (эталон Net_RecvLoginResponse case 4): «выход из игры не осуществлён/не полностью».
			//   Оригинал НЕ дисконнектит и НЕ ретраит — показывает нотис #102 и ждёт РУЧНОГО повторного входа
			//   по ТОМУ ЖЕ соединению (первый overlap-логин заставил сервер кикнуть старую сессию → 2й пустит).
			//   State НЕ трогаем (соединение живо) — иначе повторный логин делал бы дубль-сессию и пост-кик 4101.
			UE_LOG(LogAsdaNet, Warning, TEXT("Login overlap (status=4) — нотис #102, ждём повторного входа (тот же коннект)"));
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("Login FAILED statusCode=%u"), Status);
			State = EAsdaNetState::Disconnected;
		}
		OnLoginResult.Broadcast(Status);
		break;
	}
	case (NetOpcode)4101:                    // 0x1005 — ОТКАЗ/kick с кодом (login_server.md §9.4; нет в auto-enum)
	{
		// body[0]=код (хвост тела = случайный паддинг сервера, не данные!):
		//   1=ping timeout (Check_DisconnectUser — молчали >~50 с), 2=дубль-сессия, 3=MD5/secret fail,
		//   5=IP null (гонка accept→1003), 7=blocked, 10=version mismatch, 22=IP в блок-листе,
		//   28=профилактика (нотис 7017): режим обслуживания L09 (LoginPatch, учётка вне 1…5000) или IP не в AcceptIp.ini.
		const uint8 Code = NetNeedLen(Pkt.Body, 1, (int32)Pkt.Opcode) ? Pkt.Body[0] : 0xFF;
		// ★ 1:1 эталон (network.c, ветка opcode==4101): коды 0x0D..0x20 и 0x65..0x6B — ТОЛЬКО локализованный
		//   нотис + разблокировка полей ввода, соединение НЕ рвётся (вход повторяют по тому же сокету).
		//   ВСЕ прочие коды идут в g_nDisconnectReason, а сокет закрывается (netCloseSocketBySlot внутри
		//   Show_DisconnectPopup). Раньше порт для ЛЮБОГО кода лишь ставил State=Disconnected, НЕ закрывая
		//   сокет: IsConnected() оставался true → DoLogin не переподключался и слал 1003 в мёртвый сокет
		//   («авторизоваться уже невозможно»), а слот на сервере продолжал висеть.
		const bool bNoticeOnly = (Code >= 0x0D && Code <= 0x20) || (Code >= 0x65 && Code <= 0x6B);
		const TCHAR* Reason =
			Code == 1  ? TEXT("нет пинга 1000 (idle-таймаут ~50 с)") :
			Code == 2  ? TEXT("дубль-сессия") :
			Code == 3  ? TEXT("неверный ID/пароль (MD5/secret)") :
			Code == 5  ? TEXT("IP не определён") :
			Code == 7  ? TEXT("заблокирован") :
			Code == 10 ? TEXT("version mismatch (повреждён cred?)") :
			Code == 22 ? TEXT("IP в блок-листе") :
			Code == 28 ? TEXT("профилактика: режим обслуживания L09 или AcceptIp") : TEXT("прочее");   // ★ 21.11 ревизия 43б: 28 — и режим L09
		UE_LOG(LogAsdaNet, Warning, TEXT("Login KICK 4101 code=%u (%s), соединение %s"),
			Code, Reason, bNoticeOnly ? TEXT("сохраняем") : TEXT("закрываем"));
		if (bNoticeOnly)
		{
			// Соединение живо — это только нотис, никакого разрыва не произошло.
			OnLoginKick.Broadcast((int32)Code, /*bClosed*/false);
			break;
		}
		// ★★★ РЕВИЗИЯ 2026-09-17, КРИТ-1: ИГРОВОЙ КИК 4101 ОБЯЗАН ИДТИ В ОБЩУЮ ТОЧКУ РАЗРЫВА.
		//   ЧТО БЫЛО. Здесь стоял СВОЙ, урезанный сброс состояния и `OnLoginKick.Broadcast(...)`.
		//   Единственный подписчик `OnLoginKick` — `AAsdaLoginGameMode` (он живёт на карте ЛОГИНА),
		//   поэтому в ИГРЕ кик 4101 не доходил вообще ни до кого:
		//     • попап обрыва 8000 (`UAsdaDisconnectPopupSubsystem`) подписан на `OnGameDisconnected`
		//       ⇒ 15 причин из 16 (0…15) были недостижимы, в игре попап показывался ТОЛЬКО из
		//       `HandleConnectionClosed`, где причина всегда 255 → нотис 1108;
		//     • сброс сессии (`UAsdaSessionResetSubsystem`) тоже подписан на `OnGameDisconnected`
		//       ⇒ после игрового кика зеркала персонажа/мира оставались жить.
		//   ПОЧЕМУ `OnClosed` не спасал: сокет мы закрываем САМИ через `FAsdaConnection::Disconnect()`,
		//   а колбэк `OnClosed` зовёт только `MarkClosed` (обрыв со стороны сервера) —
		//   `AsdaConnection.cpp:116-121` против `:178-190`.
		//   ЭТАЛОН. 4101 сокет не закрывает вовсе: он лишь пишет `g_nDisconnectReason = body[0]`
		//   (`AsdaStory_game.c:16095`), сервер рвёт соединение сам, приходит FD_CLOSE (WM_USER 0x511,
		//   lParam = 2), и ОДНА общая ветка WndProc (`AsdaStory_game.c:23941-24085`) читает
		//   `g_nDisconnectReason` и показывает попап. То есть у оригинала точка входа ОДНА.
		//   ЛЕЧЕНИЕ. Кладём причину в `LastKickCode` и зовём ту же `HandleConnectionClosed()`, что и
		//   настоящий обрыв: она перенесёт причину в `LastDisconnectReason` (его и читает попап),
		//   закроет сокет, полностью сбросит состояние и разошлёт ПРАВИЛЬНЫЙ делегат —
		//   `OnGameDisconnected` в игре и `OnLoginKick` на логине.
		//   ⚠ Повторного входа в `HandleConnectionClosed` не будет: `Conn->Disconnect()` колбэка
		//   `OnClosed` не дёргает, а разбор входящего потока прекращается по смене `RecvEpoch`
		//   (`AsdaConnection.cpp:188`, 296) — то есть закон `no-reentrant-net-callbacks` соблюдён.
		LastKickCode = Code;
		HandleConnectionClosed();
		break;
	}
	case OP_PARSE_PACKET_NAME_ENTRY_BLOCK:  // 1015 0x3F7 — дефолт-облик для CARAMAKE (parsePacketNameEntryBlock@0xF12D30)
	{
		// payload (Pkt.Body, транспорт-9б уже срезан): +4 дефолт-имя (65б ASCIIZ), +69 триплет причёска/цвет/лицо.
		const std::vector<uint8>& B = Pkt.Body;
		FCharCreateDefaults D;
		if (NetNeedLen(B, 5, (int32)Pkt.Opcode))
		{
			char NameBuf[66]; const int32 NL = FMath::Min((int32)B.size() - 4, 65);
			FMemory::Memcpy(NameBuf, &B[4], NL); NameBuf[FMath::Clamp(NL, 0, 65)] = 0;
			D.Name = FAsdaLoginData::DecodeCp1251((const uint8*)NameBuf, 65);
		}
		if (NetNeedLen(B, 72, (int32)Pkt.Opcode)) { D.Hair = B[69]; D.Color = B[70]; D.Face = B[71]; }
		D.bValid = true;
		CharCreateDefaults = D;
		UE_LOG(LogAsdaNet, Log, TEXT("0x3F7 дефолт-облик: имя='%s' hair=%u color=%u face=%u"),
			*D.Name, D.Hair, D.Color, D.Face);
		break;
	}
	case (NetOpcode)1026:  // 0x0402 — РЕЗУЛЬТАТ создания (эталон netHandleNameEntryDialogPacket@0xF12E50): Body[0]=код
	{                       //   0=ошибка(loc111+9581), 1=УСПЕХ(данные перса Body[1..]), 2/3=ошибка имени(loc112/113+9581).
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Code = B.empty() ? -1 : (int32)B[0];   // 0=отказ(loc111) 1=успех 2=имя(loc112) 3=(loc113)
		UE_LOG(LogAsdaNet, Log, TEXT("[CHAR_CREATE] op1026 результат code=%d bodyLen=%d"), Code, (int32)B.size());
		OnCharCreateResult.Broadcast(Code);
		break;
	}
	// ★★★ 1011 — ОТКАЗ ВХОДА В МИР (enum-имя `OP_NET_RECV_CHAR_CREATE_RESULT` — МИСНЕЙМ: результат
	//   СОЗДАНИЯ приходит опкодом 1026, а 1011 логин-сервер шлёт как отказ на выбор персонажа 1016).
	//   Доказано серверным декомпилом `LoginManager.c` `Send_SelectChar@0x40AD30` (`sub_40AD30`):
	//   тело РОВНО 2 б, [0..1] = код (i16, LE), заполняется `v89`:
	//     201 — Secret Error / MD5 Error / GroupIndex Error / Channel Error / UserIndex,CharIndex Error
	//           (строки 4099-4166: v89=201 → `sub_415A40(1011,2,…)`);
	//     204 — UPCharPos Error / UPChannelCount Error (строки 4230-4256: v89=204).
	//   ★ [ОРИГИНАЛ] ДЕФЕКТ UX: раньше 1011 у нас НЕ обрабатывался вовсе и уходил в `default:` —
	//     при любой из этих ошибок игрок молча висел на экране выбора персонажа (наблюдение
	//     владельца «нажал Играть — ничего не произошло»). Теперь код читается, логируется и
	//     отдаётся наружу тем же делегатом `OnCharSelectRefused`, что и отказ 1027 — слой сцены
	//     логина разблокирует кнопку/сообщит. Коды 1011 (201/204) не пересекаются с кодами 1027
	//     (0/2/3), поэтому канал делегата общий и однозначный.
	case OP_NET_RECV_CHAR_CREATE_RESULT:  // 1011 0x3F3 — отказ входа (НЕ создание перса; см. выше)
	{
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Code = (B.size() >= 2) ? (int32)AsdaBytes::RdU16(B.data())
		                                   : (B.size() == 1 ? (int32)B[0] : -1);
		UE_LOG(LogAsdaNet, Warning, TEXT("1011 ОТКАЗ входа в мир: код=%d%s (тело %d б)"),
			Code,
			Code == 201 ? TEXT(" (Secret/MD5/GroupIndex/Channel/UserIndex — пакет 1016 отвергнут)")
			            : (Code == 204 ? TEXT(" (UPCharPos/UPChannelCount — серверная процедура позиции)") : TEXT("")),
			(int32)B.size());
		// ★ Своё состояние «душа/одержимость» ставится ответом 1017 (выбор принят), а вход в мир
		//   может быть отвергнут ПОЗЖЕ этим 1011 (201/204). Без сброса игрок остался бы на выборе
		//   персонажа с «принятым» режимом прошлой попытки — и гейт `AsdaPossessGate`/`AsdaSoul`
		//   резал бы отправки уже на следующем входе (ревизия `review_soul_mode_gate_and_enter.md`,
		//   п.4). Сброс симметричен отказу 1027.
		SetSelfSoulState(0);
		OnCharSelectRefused.Broadcast(Code);
		break;
	}
	case OP_CHAR_CREATE_ON_CREATE_RESULT_PACKET:  // 1019 0x3FB — результат УДАЛЕНИЯ (эталон charCreate_OnCreateResultPacket@0xF03E90)
	{
		// Live-подтверждено (2026-07-27): сервер шлёт 1019 в ответ на SendDeleteChar (1018): code 1 (bodyLen=1) =
		//   успех удаления; code 0 (bodyLen=12) = отказ. Эталон-хендлер по code 1 чистит слот ЛОКАЛЬНО
		//   (cleanup + глиф создания), 2/3/4 = notice loc 12434/12459/12820. Результат СОЗДАНИЯ приходит 1026.
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Code = B.empty() ? 0 : (int32)B[0];
		UE_LOG(LogAsdaNet, Log, TEXT("[CHAR_DELETE] op1019 результат code=%d bodyLen=%d"), Code, (int32)B.size());
		OnCharDeleteResult.Broadcast(Code);
		break;
	}
	case OP_CHAR_SELECT_INIT_SLOT_ROTATION_FROM_PACKET: // 1006 0x3EE — charSelect_FillSlotRecords (§2B)
	{
		// ★ Сервер кладёт ВСЮ запись перса в 0x3EE (0x3FF/1023 НЕ приходит — проверено логом). Запись 59б @ Body[r],
		//   r=4+59*i. Смещения ОТН. r (charselect_record_layout.md §2B, сверено с живым body «sitxovski» ур.3):
		//   +0 slotId(0=пусто), +1 имя char[20], +21 appA, +22 ПОЛ/gender, +23 classId, +24 subClass, +25 уровень,
		//   +26 exp(u32), +34 HPmax(u32), +38 MPmax(u16), +40 HPcur(u32), +44 MPcur(u16), +46 6×стат(u16).
		//   constellationId = Body[190+i].
		//   ★ ПОЛ = Body[r+22] (= payload[59*i+26]) — это поле CCharacter+0x437 (v3[1079]) в
		//   charSelect_FillSlotRecords@0xF06310, ИМЕННО его читает движок как self->gender==1?ch1_m:ch1_f
		//   (модель-лоадер @0xF... ch1_m/ch1_f). Значения: 1=муж, 2=жен (0=ошибка, сервер валидирует «Sex Type==0»).
		//   appearance-хвост Body[181+3*i] (3б → slot+1121) = внешность (причёска/цвет), НЕ пол.
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 193) { UE_LOG(LogAsdaNet, Warning, TEXT("char-list 0x3EE блок мал (%d)"), (int32)B.size()); break; }
		auto RdU16 = [&](int o){ return (int32)AsdaBytes::RdU16(&B[o]); };
		auto RdU32 = [&](int o){ return AsdaBytes::RdI32(&B[o]); };
		// ★ Кладём запись по slotId%10 (DB-слот 0..2), НЕ по порядку записи k (charlist_slots_fix.md):
		//   движок индексирует персонажа по slotId (slotId∈{10,11,12}, slot=slotId−10), а не по позиции в пакете.
		//   Совпадает лишь когда единственный чар упакован записью 0; иначе рассинхрон слот↔запись (не тот слот).
		CharList.SetNum(3);
		// ★ Экип (0x3EF) на этом сервере приходит РАНЬШЕ char-list (0x3EE) — при сбросе записи НЕ терять уже
		//   разобранный Equip (иначе рендер экипа получит пустой список). Экип индексируется тем же слотом
		//   (bucket-10 == slotId%10), поэтому сохраняем/восстанавливаем по слоту.
		TArray<FAsdaEquipItem> SavedEquip[3];
		for (int32 k = 0; k < 3; ++k) { SavedEquip[k] = CharList[k].Equip; }
		for (int32 k = 0; k < 3; ++k) { CharList[k] = FAsdaCharRecord(); }
		for (int32 k = 0; k < 3; ++k)
		{
			const int32 r = 4 + k * 59;   // начало записи k (slotId-байт)
			const uint8 SlotId = B[r];
			if (SlotId == 0) { continue; }   // пустая запись
			const int32 slot = SlotId % 10;
			if (slot < 0 || slot > 2) { continue; }
			FAsdaCharRecord& Rec = CharList[slot];
			Rec.Slot = (uint8)slot;
			Rec.RawSlotId = SlotId;   // сырой slotId (10/11/12) — эталон rec[1076]; шлётся в select/delete как есть
			{
				uint8 NB[21]; FMemory::Memcpy(NB, &B[r + 1], 20); NB[20] = 0;
				Rec.Name = FAsdaLoginData::DecodeCp1251(NB, 20);   // имя @r+1, 20б cp1251
			}
			Rec.Sex          = B[r + 22];        // ★ ПОЛ = gender-байт записи (CCharacter+0x437 → ch1_m/ch1_f); 1=муж/2=жен
			Rec.Job          = B[r + 23];
			Rec.SubClass     = B[r + 24];
			Rec.Level        = B[r + 25];
			Rec.Exp          = (int64)RdU32(r + 26) | ((int64)RdU32(r + 30) << 32);   // total exp (для exp-бара, статBarUpdatePercent)
			Rec.HpMax        = RdU32(r + 34);
			Rec.MpMax        = RdU16(r + 38);
			Rec.HpCur        = RdU32(r + 40);
			Rec.MpCur        = RdU16(r + 44);
			Rec.Stats.SetNum(6);
			for (int32 s = 0; s < 6; ++s) { Rec.Stats[s] = RdU16(r + 46 + s * 2); }
			Rec.Constellation = B[190 + k];   // созвездие индексируется порядком записи k (эталон v64[187+k])
			// Внешность: блок 3×3б сразу после 3 записей (offset 4+3*59=181). Эталон charSelect_FillSlotRecords:498.
			if (NetNeedLen(B, 184 + 3 * k, (int32)Pkt.Opcode))
			{
				Rec.Hair  = B[181 + 3 * k];
				Rec.Color = B[182 + 3 * k];
				Rec.Face  = B[183 + 3 * k];
			}
		}
		// восстановить экип по слоту (пришёл в 0x3EF ДО этого char-list — сброс выше его обнулил)
		for (int32 k = 0; k < 3; ++k) { CharList[k].Equip = SavedEquip[k]; }
		bLoggedIn = true;   // ★ получили char-list → в char-select, соединение стабильно → включить keepalive
		UE_LOG(LogAsdaNet, Log, TEXT("char-list 0x3EE: слоты %s/%s/%s"),
			CharList[0].IsEmpty()?TEXT("пусто"):*FString::Printf(TEXT("%s ур%u %s"), *CharList[0].Name, CharList[0].Level, *FAsdaCharRecord::ClassName(CharList[0].Job)),
			CharList[1].IsEmpty()?TEXT("пусто"):*CharList[1].Name,
			CharList[2].IsEmpty()?TEXT("пусто"):*CharList[2].Name);
		OnCharListReady.Broadcast();
		break;
	}
	case OP_CHAR_SLOT_FILL_RECORDS_FROM_PACKET:     // 1024 0x400 — charSlotFillRecordsFromPacket@0xF03090
	{
		// ★★★ ЭКИПИРОВКА ПЕРСОНАЖА СОУЛМЕЙТА. ПО ПАКЕТУ НА КАЖДОГО шаренного персонажа (до 3),
		//   и все они приходят РАНЬШЕ списка 1023 (`LoginManager.c:3081` до `:3084`) — та же
		//   инверсия, что у своих (1007 раньше 1006). Поэтому создаёт запись ИМЕННО этот пакет.
		//
		//   Тело 160 б, собирается `LoginManager.c:3062-3079` в буфер `v58[40]` (dword):
		//     [0..3]  i32 Userindex АККАУНТА-ВЛАДЕЛЬЦА (соулмейта)  — `v58[40*i] = v46`
		//     [4]     u8  Charidx персонажа (10/11/12)              — `memcpy(&v58[40*i+1], row, 1)`
		//     [5]     u8  ShareCount                                — `BYTE1(v58[40*i+1]) = v47`
		//     [6+14k] 11 ячеек экипировки по 14 б, k = 0..10        (6 + 11*14 = 160 ✔)
		//   Ячейка (C = 6 + 14k), смещения сверены по ОБЕИМ сторонам:
		//     C+0  i32 Code — код предмета (`Item.Code`, 0 = пусто); сервер `…+14*j+2` ← row+1;
		//                     клиент `*(DWORD*)((char*)&v5[1] + 14*i + 2)` → CCharacter+1588+57k
		//     C+4  i16 Slot — номер слота экипировки (`EqSlot.Slot`, 0..10); сервер ← row+5;
		//                     клиент `*((WORD*)&v5[2] + 7*i + 1)` → CCharacter+1593+57k
		//     C+6  8 б      — по замыслу Cell1..Cell4 (гнёзда), клиент берёт одним memcpy 8 б.
		//   ★ [ОРИГИНАЛ] ДЕФЕКТ СЕРВЕРА, воспроизводить нечего — чинить нечего, но ВЕРИТЬ НЕЛЬЗЯ:
		//     строка процедуры `UPPreSoulEqItem` 17 байт (`Charidx@0, Code@1, Slot@5, Posy@7,
		//     Cell1@9, Cell2@11, Cell3@13, Cell4@15`), а LoginManager читает её поля с шагом 4 —
		//     со смещений 1,5,9,13,17,21 (`LoginManager.c:3069-3078`, ВА 0x408F55..0x40906C).
		//     Верны только Code (row+1) и Slot (row+5). C+6 = Cell1, C+8 = Cell3, а C+10 и C+12 —
		//     байты СЛЕДУЮЩЕЙ строки, то есть мусор. Поэтому гнёзда отсюда НЕ берём вовсе
		//     (на экране выбора персонажа они и не показываются), а `Enchant` оставляем 0:
		//     заточки в этом пакете нет ни одного байта.
		//   ★ Заточка/качество предмета сюда не приезжают — модель собирается по Code, этого хватает.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 6, (int32)Pkt.Opcode, TEXT("экип соулмейта 1024"))) { break; }
		const int32 OwnerIdx = AsdaBytes::RdI32(&B[0]);
		const uint8 RawIdx   = B[4];
		const int32 Share    = (int32)B[5];
		if (RawIdx < 10 || RawIdx > 12)
		{
			// Charidx вне 10..12 — единственные значения, которые пишет БД (`UPSoulMateOptionUp`
			//   жёстко работает с 10/11/12). Чужое значение = битый кадр, слот не трогаем.
			UE_LOG(LogAsdaNet, Warning, TEXT("1024 экип соулмейта: Charidx=%u вне 10..12 — пакет пропущен"), RawIdx);
			break;
		}
		if (SoulmateSharedChars.Num() != 3) { SoulmateSharedChars.SetNum(3); }
		FAsdaSoulmateSharedChar& Rec = SoulmateSharedChars[RawIdx % 10];
		Rec.Charidx        = RawIdx;          // ★ 1023 его затирал в оригинале — здесь он верный
		Rec.OwnerUserIndex = OwnerIdx;
		UGameInstance* GI = GetGameInstance();
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		Rec.Equip.Reset();
		Rec.Equip.SetNum(16);   // индекс = itemType (максимум 10 + запас), как у CharList[].Equip
		int32 Worn = 0;
		for (int32 k = 0; k < 11; ++k)
		{
			const int32 C = 6 + 14 * k;
			if ((int32)B.size() < C + 6) { break; }        // ★ длина ДО чтения полей
			const int32 Code    = AsdaBytes::RdI32(&B[C]);
			const int32 EqSlot  = (int32)AsdaBytes::RdU16(&B[C + 4]);
			if (Code <= 0) { continue; }                    // слот пуст
			// Оригинал кладёт ячейку по её ПОРЯДКОВОМУ номеру (`CCharacter+1588+57k`), тогда как
			//   свою экипировку (1007) раскладывает по itemType. Расхождение эталона; на практике
			//   совпадает, потому что `UPPreSoulEqItem` отдаёт строки `ORDER BY Charidx, Slot`
			//   и слотов ровно 11 ⇒ k == Slot. Мы берём itemType из item-таблицы (иначе сборка
			//   модели получит части не в те гнёзда), а фолбэком — Slot ИЗ ПАКЕТА, а не k:
			//   это то же самое при штатной раскладке и устойчиво при нештатной.
			int32 Dst = (EqSlot >= 0 && EqSlot < 16) ? EqSlot : k;
			if (Items)
			{
				const int32 IT = Items->GetItemType(Code);
				if (IT >= 0 && IT < 16) { Dst = IT; }
			}
			if (Rec.Equip.IsValidIndex(Dst))
			{
				Rec.Equip[Dst].ItemId = Code;
				// ★ 2026-09-02: сохраняем и НОМЕР СЛОТА с провода — чтобы обе ветки (1007 и 1024)
				//   давали одинаково заполненную структуру. Заточки в char-list нет (см. FAsdaEquipItem).
				Rec.Equip[Dst].EqSlot = EqSlot;
				++Worn;
			}
		}
		UE_LOG(LogAsdaNet, Log, TEXT("1024 экип СОУЛМЕЙТА: Charidx=%u аккаунт=%d share=%d надето=%d"),
			RawIdx, OwnerIdx, Share, Worn);
		break;
	}
	case OP_CHAR_INFO_PARSE_PACKET:                 // 1023 0x3FF — charInfoParsePacket@0xF02B70
	{
		// ★★★ СПИСОК ПЕРСОНАЖЕЙ СОУЛМЕЙТА (не наших!). По логин-серверу
		//   (`CUserProc::Send_GetCharInfo@0x407CE0`) 1023 шлётся ТОЛЬКО в ветке соулмейта
		//   (`else if (v47)`, где v47 = ShareCount из `UPCharExist`): `UPPreSoulMateChar` → тело
		//   1023, `UPPreSoulMateCharApp` → его хвост-внешность, `UPPreSoulEqitem` → пакеты 1024.
		//   Собственные 3 персонажа приходят ИСКЛЮЧИТЕЛЬНО в 1006 + 1007 — сюда мы их НЕ пишем.
		//
		//   Тело 184 б (`LoginManager.c:3025-3050`, буфер `v59[184]`, отправка `:3084`):
		//     [0]        u8  ShareCount — сколько записей идёт следом (0..3)
		//     [1+58*i]   запись персонажа №i, 58 байт (раскладка — у FAsdaSoulmateSharedChar)
		//     [175+3*j]  3×u8 внешность записи №j: HairStyle, HairColor, FaceType
		//   1 + 3*58 + 3*3 = 184 ✔
		//
		//   ★ [ОРИГИНАЛ] СЧЁТЧИК БЕРЁТСЯ НЕ ИЗ ТОГО ЗАПРОСА: `v59[0] = v47` — это ShareCount из
		//     `UPCharExist`, а не число строк `UPPreSoulMateChar` (`v43[2]`). Обе величины считает
		//     один предикат, но РАЗНЫМИ запросами в разное время: между ними соулмейт может
		//     переключить галку (5221), и счётчик разойдётся с содержимым. Лишние записи — нули
		//     (буфер обнулён), поэтому мы гейтим не счётчиком, а самим Charidx каждой записи.
		//   ★ [ОРИГИНАЛ] ЗАЩИТА ОТ >3 ЗАПИСЕЙ: при `RowNum > 3` сервер (`LoginManager.c:3139-3143`
		//     и `:3158-3162`) молча выходит, не отправив НИ 1023, НИ 1025 — оригинальный клиент
		//     после этого остаётся ждать вечно. У нас этого не случится по двум причинам:
		//     (1) переход в выбор персонажа у нас взводит 1006, а не 1025 (1025 только уточняет
		//         состояние соулмейта), поэтому молчание сервера ничего не блокирует;
		//     (2) счётчик здесь жёстко зажат тремя записями, а каждая читается с проверкой границ.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 1, (int32)Pkt.Opcode, TEXT("список соулмейта 1023"))) { break; }
		const int32 RawCount = (int32)B[0];
		// Больше трёх персонажей у аккаунта физически не бывает (Charidx только 10/11/12), а буфер
		//   пакета вмещает ровно три записи. Зажимаем — иначе разбор ушёл бы за конец тела.
		const int32 Count = FMath::Clamp(RawCount, 0, 3);
		if (RawCount != Count)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("1023: сервер объявил %d записей — зажато до %d (в теле их только 3)"),
				RawCount, Count);
		}
		if (SoulmateSharedChars.Num() != 3) { SoulmateSharedChars.SetNum(3); }
		const bool bLegacy = CVarSoulmateLegacyNameOverrun.GetValueOnAnyThread() != 0;
		int32 Parsed = 0;
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 r = 1 + 58 * i;                     // начало записи i
			if (!NetNeedLen(B, r + 58, (int32)Pkt.Opcode, TEXT("запись 58 б пакета 1023"))) { break; }
			uint8 Rec58[58];
			FMemory::Memcpy(Rec58, &B[r], 58);
			if (bLegacy)
			{
				// ── ВОСПРОИЗВЕДЕНИЕ ДЕФЕКТА ЭТАЛОНА (ключ отката asda.Soulmate.LegacyNameOverrun=1).
				//   Оригинал последним действием цикла делает `memcpy(slot+1056, &payload[58*i+2], 0x20)`,
				//   то есть 32 байта с rec+1 поверх полей слота +1056..+1087. В терминах записи это
				//   сдвиг на −2: поле слота +1076+n получает байт rec+21+n. Полностью накрыты
				//   Charidx, Clan, Gender, Job, SubJob, Lvl и первые 5 байт опыта; HP/MP/статы
				//   (слот +1124..+1202) 32 байта не достают — там значения остаются верными.
				//   Байты опыта не воспроизводим побайтно намеренно: смысла в них нет, потому что
				//   ПЕРВАЯ же строка обнуляет Charidx (Clan в процедуре жёстко 0), и запись ниже
				//   отбрасывается по гейту 10..12 — ровно как в оригинале, где персонаж соулмейта
				//   не находился никогда.
				Rec58[0]  = Rec58[21];    // Charidx  ← Clan (= 0) ⇒ запись «пропадает»
				Rec58[21] = Rec58[22];    // Clan     ← Gender
				Rec58[22] = Rec58[23];    // Gender   ← Job
				Rec58[23] = Rec58[24];    // Job      ← SubJob
				Rec58[24] = Rec58[25];    // SubJob   ← Lvl
				Rec58[25] = Rec58[26];    // Lvl      ← первый байт опыта
			}
			const uint8 RawIdx = Rec58[0];
			if (RawIdx < 10 || RawIdx > 12)
			{
				// Так выглядит и «лишняя» запись при разошедшемся ShareCount (нули), и результат
				//   дефекта эталона при включённом ключе отката. И то и другое — «персонажа нет».
				UE_LOG(LogAsdaNet, Log, TEXT("1023 запись %d: Charidx=%u вне 10..12 — пропущена%s"),
					i, RawIdx, bLegacy ? TEXT(" (ключ отката: дефект эталона)") : TEXT(""));
				continue;
			}
			FAsdaSoulmateSharedChar& Rec = SoulmateSharedChars[RawIdx % 10];
			Rec.Charidx = RawIdx;
			{
				// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА: имя — РОВНО 20 байт (`Charname varchar(20)`,
				//   без терминатора), как в собственной ветке. Эталон копирует 32 и затирает
				//   Charidx/класс/уровень/опыт — см. CVarSoulmateLegacyNameOverrun.
				uint8 NB[21];
				FMemory::Memcpy(NB, &Rec58[1], 20);
				NB[20] = 0;
				Rec.CharName = FAsdaLoginData::DecodeCp1251(NB, 20);
			}
			Rec.ClanId = Rec58[21];      // всегда 0: `SET @Clan = 0` в UPPreSoulMateChar
			Rec.Gender = Rec58[22];      // 1=муж, 2=жен — выбирает модель и набор клипов
			Rec.Job    = Rec58[23];
			Rec.SubJob = Rec58[24];
			Rec.Level  = Rec58[25];
			Rec.Exp    = (int64)(uint32)AsdaBytes::RdI32(&Rec58[26]) | ((int64)AsdaBytes::RdI32(&Rec58[30]) << 32);
			Rec.MaxHp  = AsdaBytes::RdI32(&Rec58[34]);
			Rec.MaxMp  = (int32)AsdaBytes::RdU16(&Rec58[38]);
			Rec.Hp     = AsdaBytes::RdI32(&Rec58[40]);
			Rec.Mp     = (int32)AsdaBytes::RdU16(&Rec58[44]);
			Rec.Stats.SetNum(6);
			for (int32 s = 0; s < 6; ++s) { Rec.Stats[s] = (int32)AsdaBytes::RdU16(&Rec58[46 + s * 2]); }
			// Внешность — хвост пакета, блок 3 б по ПОРЯДКОВОМУ номеру записи i (не по Charidx):
			//   сервер сопоставляет `UPPreSoulMateChar` и `UPPreSoulMateCharApp` позиционно
			//   (`memcpy(&v59[3*j+175], …, 3)`), ORDER BY ни в одной из процедур нет — [ОРИГИНАЛ].
			if (NetNeedLen(B, 175 + 3 * i + 3, (int32)Pkt.Opcode, TEXT("внешность 1023")))
			{
				Rec.Hair      = B[175 + 3 * i];
				Rec.HairColor = B[176 + 3 * i];
				Rec.Face      = B[177 + 3 * i];
			}
			++Parsed;
			UE_LOG(LogAsdaNet, Log, TEXT("1023 персонаж СОУЛМЕЙТА: Charidx=%u '%s' ур.%u класс=%u пол=%u HP=%d/%d MP=%d/%d"),
				RawIdx, *Rec.CharName, Rec.Level, Rec.Job, Rec.Gender, Rec.Hp, Rec.MaxHp, Rec.Mp, Rec.MaxMp);
		}
		UE_LOG(LogAsdaNet, Log, TEXT("1023 список соулмейта: объявлено %d, разобрано %d"), RawCount, Parsed);
		// ★ 02.11.2026 (волна jl_presm): метка «1023 этой транзакции пришёл, 1025 ещё нет» = -2 (в 1005 сброс в -1, 1025 перепишет
		//   кодом). LoginManager шлёт 1023 только ПОСЛЕ своего списка 1006 (`Send_GetCharInfo`, LoginManager.c:3085) — по ней ветка
		//   «1025 код 0» ниже отличает сбой `UPPreSMConnect` (список уже есть) от сбоя до 1006.
		if (SoulmateBranchCode == -1) { SoulmateBranchCode = -2; }
		break;
	}
	case OP_NET_RECV_ENTER_WORLD_STATUS:            // 1025 0x401 — Net_RecvEnterWorld@0xF03690 (МИСНЕЙМ)
	{
		// ★ Финальный статус транзакции 1005: сервер шлёт его ВСЕГДА, кроме двух ветвей «RowNum > 3»
		//   (см. коммент к 1023). Тело 5 б, значения — из `Send_GetCharInfo`:
		//     [0] 0 = ошибка SQL по своим персонажам (эталон переспрашивает 1028)
		//         1 = соулмейта нет (`MateUserindex <= 0`, подкод 2) либо он ничего не шарит (подкод 3)
		//         2 = `Smstate == 1` (на живой БД недостижимо: процедура держит `SET @SmState = 0`)
		//         3 = ветка соулмейта отработала, соулмейт ОНЛАЙН   (`UPPreSMConnect.Ustate == 1`)
		//         4 = ветка соулмейта отработала, соулмейт офлайн
		//         5 = просто показать панель выбора персонажа
		//     [1] подкод → эталонный флаг +3075045 (1|3 → 1, 0|2 → 0): второй абзац панели соулмейта
		//     [2] соулмейт в игре · [3] его канал · [4] его CPoridx — эталон эти три байта НЕ читает
		//   ★ Переход в сцену выбора персонажа у нас взводит 1006 (см. `bLoggedIn` там), а не этот
		//   пакет: так молчание сервера в дефектной ветке «RowNum > 3» не может нас заморозить.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 2, (int32)Pkt.Opcode, TEXT("статус ветки соулмейта 1025"))) { break; }
		const bool bAfterSoulList = (SoulmateBranchCode == -2);   // 1023 этой транзакции уже был ⇒ 1006 тоже (см. 1023)
		SoulmateBranchCode = (int32)B[0];
		const int32 Sub    = (int32)B[1];
		bSoulmateInfoSecondParagraph = (Sub == 1 || Sub == 3);
		if (SoulmateBranchCode == 3)      { bSoulmateOnline = true;  }
		else if (SoulmateBranchCode == 4) { bSoulmateOnline = false; }
		if (NetNeedLen(B, 5, (int32)Pkt.Opcode, TEXT("хвост 1025")))
		{
			SoulmateInGameByte = (int32)B[2];
			SoulmateChannel    = (int32)B[3];
			SoulmatePortalId   = (int32)B[4];
		}
		// Транзакция 1005 завершена: и экип (1024), и список (1023) уже пришли — данные полные.
		int32 Ready = 0;
		for (const FAsdaSoulmateSharedChar& R : SoulmateSharedChars) { if (!R.IsEmpty()) { ++Ready; } }
		UE_LOG(LogAsdaNet, Log, TEXT("1025 статус соулмейта: код=%d подкод=%d онлайн=%d канал=%d портал=%d; готовых персонажей=%d"),
			SoulmateBranchCode, Sub, bSoulmateOnline ? 1 : 0, SoulmateChannel, SoulmatePortalId, Ready);
		// ★★★ ВЕТКА [0] == 0 — ОШИБКА SQL ПО СВОИМ ПЕРСОНАЖАМ (добавлено 2026-09-02 по ревизии, Р3).
		//   Эталон здесь НЕ молчит: `case 0: g_nCharEnterState = 0; return net_sendLoginCharEnterOpcode1028(self);`
		//   — сбрасывает состояние и ПЕРЕСПРАШИВАЕТ. Это важно, потому что во ВСЕХ путях с кодом 0
		//   сервер ещё НЕ отправил 1006: ошибки `usp_CharConstellationSelect` (LM.c:2936),
		//   `UPPreEqitem` (:3191), `UPPreCharApp` (:3210), `UPPreCharInfo` (:3229), `UPCharExist` (:3253)
		//   — все ДО отправки 1006 (:2994). Значит наша страховка «переход взводит 1006» тут НЕ
		//   работает, и без этой ветки порт оставался бы на экране логина НАВСЕГДА и МОЛЧА.
		//   Сам 1028 не шлём: это отдельный опкод повторного запроса, его раскладка не доказана —
		//   сообщаем слою логина, он повторит штатным 1005 (закон `never-invent-only-original`).
		// ★★★ 02.11.2026 (волна jl_presm) [ИСПРАВЛЕНИЕ порта]: допущение «во ВСЕХ путях с кодом 0 1006 ещё не отправлен» НЕВЕРНО
		//   для одной ветки: сбой `UPPreSMConnect` (статус соулмейта, LoginManager.c:3091-3127) шлёт 1025 код 0 ПОСЛЕ 1006, 1024 и
		//   1023 — список своих персонажей уже показан. Там «список не придёт» — ложь: это только «статус соулмейта не получен»
		//   (на dev до правки БД jl_presm — каждый вход с расшаренным соулмейтом, 171 раз 17.10–01.11). Признак ветки — 1023
		//   этой транзакции (`bAfterSoulList`). Статус «в сети» при этом не знаем — флаг остаётся «не в сети» (как сброшен в 1005).
		//   КЛЮЧ ОТКАТА: `asda.Login.Code0AfterSoulList 0` — прежняя трактовка (событие провала списка всегда).
		if (SoulmateBranchCode == 0 && bAfterSoulList && CVarCode0AfterSoulList.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogAsdaNet, Warning,
				TEXT("1025 код 0 ПОСЛЕ списка соулмейта (1023): сервер не получил статус соулмейта (ошибка UPPreSMConnect). ")
				TEXT("Список персонажей уже пришёл — повтор 1005 не нужен; соулмейт считается «не в сети»."));
		}
		else if (SoulmateBranchCode == 0)
		{
			UE_LOG(LogAsdaNet, Warning,
				TEXT("1025 код 0: сервер не смог собрать список персонажей (ошибка SQL ДО отправки 1006). ")
				TEXT("Список НЕ придёт — запрос 1005 надо повторить, иначе экран логина залипнет."));
			OnCharListFailed.Broadcast();
		}
		else if (SoulmateBranchCode == 5)
		{
			// Этот сервер код 5 не шлёт нигде (проверено ревизией) — только ветка клиентского switch.
			//   Логируем, чтобы поймать, если он всё же появится на другой сборке сервера.
			UE_LOG(LogAsdaNet, Warning, TEXT("1025 код 5 («просто показать панель») — этот сервер его не шлёт; проверить сборку сервера"));
		}
		OnSoulmateSharedCharsUpdated.Broadcast();
		break;
	}
	case OP_NET_RECV_CHAR_LIST_ENTRY:       // 1007 0x3EF — экип персонажа (uiEquipGrid_FillItemRecords@0xF05E90)
	{
		// ★ Формат ТОЧНО из эталона uiEquipGrid_FillItemRecords@0xF05E90 (charselect_record_layout.md §3A):
		//   Body[4] = "bucket": если >=10 → slotIdx = bucket-10, иначе slotIdx=0.
		//   Затем 11 ЯЧЕЕК по 14 байт, cell = Body + 14*i + 6:
		//     cell+0 (u32) itemId ;  cell+4 (u16) enchant/plus ;  cell+6 (8 байт) опции.
		//   Оригинал раскладывает ячейку в грид ПО itemType из клиентской item-таблицы
		//   (CItemManager_GetItemRecord: ItemRecord->itemType). i-я ячейка идёт в грид[itemType], а не грид[i];
		//   при отсутствии записи в таблице — грид[i]. Клиентская item-таблица (item_list.bin/item_img_table.bin
		//   из Data\Character\Bin.bbz) в порт пока НЕ интегрирована → раскладываем по индексу ячейки i
		//   (= itemType при штатной серверной раскладке; для рендера см. charselect_equip.md §B).
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 5) { UE_LOG(LogAsdaNet, Warning, TEXT("0x3EF экип-блок мал (%d)"), (int32)B.size()); break; }
		auto RdU16 = [&](int o){ return (int32)AsdaBytes::RdU16(&B[o]); };
		auto RdU32 = [&](int o){ return AsdaBytes::RdI32(&B[o]); };
		const uint8 Bucket = B[4];
		const int32 Slot   = (Bucket >= 10) ? (Bucket - 10) : 0;
		if (CharList.Num() < 3) { CharList.SetNum(3); }
		if (Slot < 0 || Slot >= 3) { UE_LOG(LogAsdaNet, Warning, TEXT("0x3EF slot вне диапазона (%d)"), Slot); break; }
		FAsdaCharRecord& Rec = CharList[Slot];
		// ★ Раскладка по itemType из item-таблицы (charselect_present_fix.md §A, эталон uiEquipGrid_FillItemRecords):
		//   грид индексируется ItemRecord->itemType (enum A: 0шлем 1тело 2штаны 3обувь 4перчатки 8armor 9оружие),
		//   ячейка i — лишь ФОЛБЭК если itemId не резолвится в таблице. Раньше писали Equip[i]=cell[i] → «сдвиг»
		//   меток слотов. Item-подсистема (GetItemType) теперь интегрирована → раскладываем правильно.
		UGameInstance* GI = GetGameInstance();
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		Rec.Equip.Reset();
		Rec.Equip.SetNum(16);   // индекс = itemType (макс 9 + запас)
		for (int32 i = 0; i < 11; ++i)
		{
			const int32 cell = 14 * i + 6;
			if ((size_t)(cell + 12) > B.size()) { break; }   // тело обрезано
			const int32 ItemId = RdU32(cell);
			if (ItemId <= 0) { continue; }
			int32 Slot2 = i;   // фолбэк — индекс ячейки
			if (Items)
			{
				const int32 IT = Items->GetItemType(ItemId);
				if (IT >= 0 && IT < 16) { Slot2 = IT; }
			}
			if (Rec.Equip.IsValidIndex(Slot2))
			{
				Rec.Equip[Slot2].ItemId  = ItemId;
				// ★ ИСПРАВЛЕНО 2026-09-02: `cell+4` — это НОМЕР СЛОТА (`EqSlot.Slot`), а не заточка.
				//   Заточки на проводе char-list нет вовсе, она приходит пакетом 1027 (запись+0x18).
				Rec.Equip[Slot2].EqSlot = (int32)RdU16(cell + 4);
			}
		}
		// Лог по enum A itemType: 0шлем 1тело 2штаны 3обувь 4перчатки 9оружие.
		UE_LOG(LogAsdaNet, Log, TEXT("экип 0x3EF слот=%u bucket=%u: шлем=%d тело=%d штаны=%d обувь=%d перчатки=%d оружие=%d"),
			Slot, Bucket,
			Rec.Equip.IsValidIndex(0) ? Rec.Equip[0].ItemId : 0,
			Rec.Equip.IsValidIndex(1) ? Rec.Equip[1].ItemId : 0,
			Rec.Equip.IsValidIndex(2) ? Rec.Equip[2].ItemId : 0,
			Rec.Equip.IsValidIndex(3) ? Rec.Equip[3].ItemId : 0,
			Rec.Equip.IsValidIndex(4) ? Rec.Equip[4].ItemId : 0,
			Rec.Equip.IsValidIndex(9) ? Rec.Equip[9].ItemId : 0);
		break;
	}
	case OP_NET_RECV_UNIT_INFO63:           // 1013 0x3F5 — счётчики онлайна каналов (63б: u16+u8+u16[30])
	{
		const std::vector<uint8>& B = Pkt.Body;
		ChannelOnline.Reset();
		// [0..1] u16 max-ёмкость (клиент игнорирует), [2] serverIndex, [3..] u16[30] онлайн-счётчики каналов.
		// ★ Значение 0xFFFF (=-1) — «канал НЕДОСТУПЕН» (не запрошен / UPChannelCount пуст), НЕ «забит»
		//   (channel_load_fix.md, логин-сервер sub_40D760). Читаем ЗНАКОВО (int16), чтобы -1 остался -1,
		//   иначе unsigned 65535 → максимум ромбиков = ложное «полностью забито».
		for (int32 i = 0; i + 1 < 30 * 2 && (size_t)(3 + i + 1) < B.size(); i += 2)
		{
			ChannelOnline.Add(AsdaBytes::RdI16(&B[3 + i]));
		}
		UE_LOG(LogAsdaNet, Log, TEXT("Каналы: получено %d счётчиков онлайна"), ChannelOnline.Num());
		OnChannelListReady.Broadcast();
		break;
	}
	case OP_NET_RECV_CHAR_SELECT: // 1017 0x3F9 — Net_RecvCharSelect@0xF03910: g_aCharSelectData=body[4..71] (68б), charId=dword[4]
	{
		// ★★★ ПРИЗНАК ОДЕРЖИМОСТИ ВЗВОДИТСЯ ЗДЕСЬ, А НЕ НА ОТПРАВКЕ 1016 (закон
		//   `no-state-latch-before-shown`, ревизия 2026-09-03). 1017 — это УСПЕШНЫЙ ответ сервера
		//   на выбор персонажа; отказ приходит другим опкодом (1027) и признак там снимается.
		//   `LastSelectMode` — режим, с которым ушёл последний 1016: 2 = вход телом соулмейта
		//   (сервер ставит `CharPos.SoulState = 2`), 0 = обычный вход, он же снимает признак.
		//   Признак читают индикатор «(Одержим)» над своей плашкой и гейт действий
		//   (`World/AsdaPossessGate.h`); провод он не затрагивает.
		//   С 2026-09-03 режим переносится в ЕДИНОЕ поле `SelfSoulState` (0/1/2): 1 = «душа»
		//   (гейт `AsdaSoul`, белый силуэт), 2 = одержимость (зеркало `bSelfPossessed` ставит сеттер).
		SetSelfSoulState(LastSelectMode);
		UE_LOG(LogAsdaNet, Log, TEXT("1017: вход подтверждён, режим последнего 1016 = %u ⇒ SoulState=%u (%s)"),
			(uint32)LastSelectMode, (uint32)SelfSoulState,
			SelfSoulState == 2 ? TEXT("одержимость") : (SelfSoulState == 1 ? TEXT("душа") : TEXT("обычный вход")));
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() >= 72)
		{
			CharSelectData.SetNum(68);
			FMemory::Memcpy(CharSelectData.GetData(), B.data() + 4, 68);   // body[4..71] = g_aCharSelectData
			bSelfCharmFromBlock = false;   // ★ 19.10.2026: новый блок — обаяние снова возьмёт следующий 4048 (`SelfCharm`, .h)
			SelectedCharId = AsdaBytes::RdI32(&B[4]);
			UE_LOG(LogAsdaNet, Log, TEXT("CharSelect 1017: charId=%d (блок 68б сохранён для enter-хендшейка)"), SelectedCharId);
			// ★★★ РАЗБОР БЛОКА g_aCharSelectData (реверс 2026-08-06, доказан живым дампом).
			//   Эталон кладёт этот блок ЦЕЛИКОМ в только что созданного локального игрока:
			//     game_main.c:2801  qmemcpy((void*)(pNewLocalPlayer + 1052), &g_aCharSelectData, 0x44u);
			//   и сразу зовёт hudRefreshFromCharacterStats (game_main.c:2839) — т.е. ВЕСЬ мини-статус
			//   (вес, уровень, класс, имя) стартует ИМЕННО отсюда, а не из игровых пакетов.
			//   CCharacter+1052 = 0x41C, поэтому смещения внутри блока (= байты тела 1017 минус 4):
			//     +0  i32 charId      (0x41C)   ← уже разобрано выше
			//     +4  char[16] имя    (0x420)
			//     +28 u8  класс       (0x438)  — байт иконки класса мини-статуса (НЕ job!)
			//     +30 u8  уровень     (0x43A)
			//     +31 i64 опыт        (0x43B)
			//     +39 i16 очки умений (0x443)
			//     +41 u8  профессия   (0x445)
			//     +44 i16 МАКС. ВЕС   (0x448)  ★ ЕДИНСТВЕННЫЙ вход базового максимума в клиент
			//     +46 i16 тек. вес    (0x44A)
			//     +48 i32 голда       (0x44C)  — НЕ вес (прежняя дока путала)
			//     +58 i16 ЯЧЕЕК СКЛАДА всего  (0x456) ★ то самое поле, что обновляет 5456 —
			//                                  `gap_43B[27]`, потолок 270, вкладок = /30
			//     +60 i16 ячеек склада занято (0x458) — `gap_43B[29]`, печатается как "%3d/%3d"
			//   ПРОВЕРКА ЖИВЫМ ДАМПОМ live.dmp (локальный чар 0x27D30898, ник sitxovski):
			//     0x41C=75180, 0x420="sitxovski", 0x438=0, 0x43A=3, 0x43B=135,
			//     0x448=15000, 0x44A=3360, 0x44C=0 — совпало байт в байт.
			//   СЕРВЕРНАЯ СТОРОНА (gameserver-crossref): тот же блок лежит в CUser (MaxWeight/CurWeight
			//   читают sub_43FB20/sub_43FA00 по CUser+416/+418), грузится из DBM_CHARINFO_RES
			//   (charLogOnRes@0x5277D0: memcpy(CUser+0x16B, тело+5, 0x45)) — то есть это ПОЛЕ БД.
			{
				const uint8* Blk = CharSelectData.GetData();
				auto I16 = [Blk](int32 O) { return AsdaBytes::RdI16(&Blk[O]); };
				auto I32b = [Blk](int32 O) { return AsdaBytes::RdI32(&Blk[O]); };
				// ★★★ ПОЛ СВОЕГО ПЕРСОНАЖА (блок+27 = CCharacter+0x437 = char+1079).
				//   Эталон: game_main.c:2801 копирует ВЕСЬ блок в char+1052, а весь выбор модели
				//   читает `self->gender` (== char+1079). Живая сверка (лог 15:54:07): тело[31]=0x01
				//   при [32]=0x0C классе 12 и [34]=0x0F уровне 15 — раскладка сходится.
				if (Blk[27] == 1 || Blk[27] == 2) { SelfGender = Blk[27]; }
				else
				{
					UE_LOG(LogAsdaNet, Warning,
						TEXT("[пол] 1017: блок+27 = %u — сервер не заполнил Sex Type (LoginManager «UPCharInfo Sex Type == 0»); берём запасной источник"),
						(uint32)Blk[27]);
				}
				// ★ Внешность своего персонажа = тело 1017 [72..75] (эталон Net_RecvCharSelect@0xF03910:
				//   `dword_29062B0 = recvBuf[18]`, далее game_main.c:2802-2805 → char+1120..1123:
				//   созвездие / причёска / цвет / лицо).
				if (NetNeedLen(B, 76, (int32)Pkt.Opcode))
				{
					SelfConstellation = B[72];
					SelfHair          = B[73];
					SelfHairColor     = B[74];
					SelfFace          = B[75];
				}
				UE_LOG(LogAsdaNet, Log,
					TEXT("1017 внешность: пол=%u (блок+27) созвездие=%d причёска=%d цвет=%d лицо=%d (тело[72..75])"),
					(uint32)SelfGender, (int32)SelfConstellation, (int32)SelfHair, (int32)SelfHairColor, (int32)SelfFace);
				SelfClassId = (int32)Blk[28];
				if (Blk[30] > 0) { SelfLevel = Blk[30]; }   // ★ 19.10.2026 (ревизия квестов): безусловно — перевыбор персонажа не наследует уровень прежнего
				if (SelfExp < 0)
				{
					int64 E = 0; FMemory::Memcpy(&E, Blk + 31, 8);
					if (E >= 0) { SelfExp = E; }
				}
				// ★ [ИСПРАВЛЕНИЕ] 29.10.2026 (вес ×2): вес — u16 по проводу (`AsdaWeightFromWire`, ключ `asda.Weight.Unsigned`).
				const int32 MaxW = AsdaWeightFromWire(I16(44)), CurW = AsdaWeightFromWire(I16(46)), Gold = I32b(48);
				UE_LOG(LogAsdaNet, Log,
					TEXT("1017 блок: класс=%d уровень=%u опыт=%lld вес=%d/%d голда=%d (смещения блока 28/30/31/46/44/48)"),
					SelfClassId, (uint32)SelfLevel, (long long)SelfExp, CurW, MaxW, Gold);
				if (MaxW > 0)
				{
					SetSelfWeight(FMath::Max(CurW, 0), MaxW, TEXT("1017 char-select (g_aCharSelectData+44)"));
				}
				else
				{
					UE_LOG(LogAsdaNet, Warning, TEXT("[вес] 1017: максимум в блоке = %d (<=0) — сервер не заполнил поле БД"), MaxW);
				}
				// ★★★ ЁМКОСТЬ СКЛАДА — из ТОГО ЖЕ блока. Эталон копирует блок целиком в
				//   `CCharacter+0x41C`, а склад читает `gap_43B[27]`/`gap_43B[29]` =
				//   `CCharacter+0x456`/`+0x458` ⇒ смещения блока 0x456−0x41C = 58 и 0x458−0x41C = 60.
				//   Живая сверка (лог 2026-08-18 22:12:34): блок+58 = 180 ячеек (6 вкладок),
				//   блок+60 = 5 занято; затем 5456 принёс 210 (7 вкладок). Раньше эти два поля
				//   не читались вовсе, а 210 из 5456 уезжало в максимум веса — отсюда «10 %».
				{
					const int32 StoreMax = I16(58), StoreUsed = I16(60);
					if (StoreMax > 0) { SelfStoreSlotsMax = StoreMax; }
					if (StoreUsed >= 0) { SelfStoreSlotsUsed = StoreUsed; }
					UE_LOG(LogAsdaNet, Log,
						TEXT("1017 блок: склад %d/%d ячеек (%d вкладок; смещения блока 60/58)"),
						SelfStoreSlotsUsed, SelfStoreSlotsMax,
						SelfStoreSlotsMax > 0 ? SelfStoreSlotsMax / kStoreSlotsPage : 0);
				}
				// ★★★ ОЧКИ УМЕНИЙ, ПОДКЛАСС И ПРОФЕССИЯ — из того же блока (смещения расписаны выше).
				//   Эталон читает их как `CCharacter+0x443` (`gap_43B[8]`, i16 SP), `CCharacter+0x439`
				//   (`gap_438[1]`, подкласс) и `CCharacter+0x445` (`gap_43B[10]`, профессия);
				//   блок 1017 ложится в `CCharacter+0x41C`, поэтому смещения = 0x443−0x41C = 39,
				//   0x439−0x41C = 29 и 0x445−0x41C = 41. Окно навыков (`skillUiUpdatePointText@0xE56720`)
				//   печатает ИМЕННО `gap_43B[8]`, так что это ПЕРВЫЙ и единственный источник счётчика
				//   свободных очков до прихода 5254/5462/5464.
				SelfSubJob      = (int32)Blk[29];
				SelfSkillPoints = I16(39);
				SelfJob         = (int32)Blk[41];
				UE_LOG(LogAsdaNet, Log,
					TEXT("1017 блок: очки умений=%d (блок+39) подкласс=%d (блок+29) профессия=%d (блок+41)"),
					SelfSkillPoints, SelfSubJob, SelfJob);
				OnSelfSkillPoints.Broadcast(SelfSkillPoints);
			}
		}
		else { UE_LOG(LogAsdaNet, Warning, TEXT("1017 тело мало (%d<72)"), (int32)B.size()); }
		break;
	}
	case OP_NET_SEND_ENTER_WORLD_HANDSHAKE: // 1021 0x3FD — ФИНАЛ enter-world (686б снапшот; коннект к game-серверу)
	{
		// netSendEnterWorldHandshake@0x971E10: тело(686)=снапшот; mapId=снапшот[26] (u16, реверс word_29065A9=unk_290658F+0x1A).
		EnterWorldSnapshot.SetNum(0);
		EnterWorldSnapshot.Append(Pkt.Body.data(), (int32)Pkt.Body.size());
		EnterMapId = (NetNeedLen(EnterWorldSnapshot, 28, (int32)Pkt.Opcode))
			? (int32)AsdaBytes::RdU16(&EnterWorldSnapshot[26]) : -1;
		// ★★★ АДРЕС GAME-СЕРВЕРА БЕРЁМ ИЗ ПАКЕТА (login_server.md §3.4/§4; сервер sub_40AD30: тело =
		//   v101[4] + Destination[14] (IP-строка, из таблиц GameServerIP по [group][channel][map]) + ... +
		//   v103 (port, u16) @+24). БЫЛ БАГ: клиент игнорировал это и коннектился на ХАРДКОД 192.168.10.70
		//   (локальный сервер) — вход на удалённый сервер уводил в пустой мир чужого сервера.
		//   ВАЖНО: строка может занимать все 14 байт БЕЗ терминатора (наш случай: "198.51.100.177").
		//   ★★ 24.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: адрес 15 символов ("@@ASTORY_SERVER_WAN_IP@@") сервер кладёт целиком с нулём (+4…+19),
		//   а чтение 14 байт обрезало его до 14 символов → вход в мир срывался. Читаем до порта (+24): 20 байт, до нуля.
		if (NetNeedLen(EnterWorldSnapshot, 26, (int32)Pkt.Opcode))
		{
			const FString Ip = AsdaBytes::AnsiFieldTrim(EnterWorldSnapshot.GetData() + 4, 20);   // N7: общая точка
			const int32 Port = (int32)AsdaBytes::RdU16(&EnterWorldSnapshot[24]);
			// Санити: IP непустой и порт в валидном диапазоне — иначе оставляем прежние значения.
			//   ★ 16.10.2026: запись — через единую точку (непубличный адрес стойки → домен сервера, порт из пакета).
			if (!ApplyGameServerFromPacket(Ip, Port, TEXT("1021")))
			{
				UE_LOG(LogAsdaNet, Warning, TEXT("1021: адрес game-сервера не распознан (ip='%s' port=%d) — остаётся %s:%d"),
					*Ip, Port, *GameServerIP, GameServerPort);
			}
			// ★ Режим XyT: сервер называет СВОЙ внешний адрес, но их клиент туда не ходит —
			//   игровой канал тоже идёт через локальный прокси `Launcher.exe`. Подменяем.
			//   ★ 16.10.2026: режим исследовательский (чужая сборка, скрыт с 31.08) — только в дев-сборках.
#if ASDA_DEV_CONSOLE
			if (bXytMode)
			{
				UE_LOG(LogAsdaNet, Log, TEXT("[XyT] адрес game-сервера из 1021 (%s:%d) заменён на лаунчер %s:%d"),
					*GameServerIP, GameServerPort, XytHost(), XytGamePort);
				GameServerIP   = XytHost();
				GameServerPort = XytGamePort;
			}
#endif
		}
		State = EAsdaNetState::EnteringWorld;
		// ★ С-Р1, редакция 1 (СРЕДН-2): настоящий вход в мир снимает окно второго барьера самотестов. Раньше окно (до 310 с
		//   у `asda.Death.Test`) переживало вход и молча глушило честные 4054/6093. Здесь, а не в рассылке сброса: офлайн-
		//   самотесты сами рассылают `EnteringWorld` посреди прохода, а пакет 1021 приходит только от живого сервера.
		FAsdaOutgoingGate::ClearDebugSendWindow(this, TEXT("вход в мир 1021"));
		UE_LOG(LogAsdaNet, Log, TEXT("Enter-world 1021 (%d б): mapId=%d → game-сервер %s:%d"),
			(int32)Pkt.Body.size(), EnterMapId, *GameServerIP, GameServerPort);
		OnEnterWorld.Broadcast();      // GameMode: скрыть логин-UI + загрузочный экран (A4)
		bPendingEnterWorld = true;     // A2+A3 (реконнект+хендшейк) — на след. тике (не реентрить коннект в колбэке пакета)
		break;
	}
	// ★★★ 6013 0x177D — ПРИЗЫВ ИГРОКА («%s зовёт тебя»), эталон `portalChannelTransferBegin@0xF49C40`
	//   (`world_map.c:8543`). Друг или соклановец переносит игрока к себе — в том числе на другой
	//   канал и другую карту. Тело 82 б, сверено с сервером
	//   (`Send_RequestUser_To_ResponseUser_SummonCommand@0x54F470`):
	//     +7  u8       карта-цель            +9  u8       канал
	//     +10 char[16] IP канал-сервера      +26 u16      порт
	//     +28 u16 X    +30 u16 Y             +62 char[20] имя зовущего
	//   Оригинал показывает нотис 104100 с этим именем, прячет окно кампвара и уходит на новый
	//   канал той же дорогой, что и 6015.
	case OP_PORTAL_CHANNEL_TRANSFER_BEGIN:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 32, (int32)Pkt.Opcode)) { break; }
		// ★ 19.10.2026 [ИСПРАВЛЕНИЕ, ПОРТ]: «собрать игроков карты» 4209 шлёт 6013 КАЖДОМУ на карте, включая самого оператора —
		//   у оригинала он уходит в переход «к себе» (перезаход на свой же сервер, group4_spec §1.4). +0 — UserIndex вызвавшего
		//   (уникален; userNum +4 — номер слота своего GS и может совпасть с чужим на другом сервере). Поле — `CUser+0x174`
		//   вызвавшего = свой `SelectedCharId` (ревизия С-2); для надёжности принимается и UserIndex учётки.
		if (AsdaBytes::RdI32(&B[0]) == GetSelfCharUserIndex() || AsdaBytes::RdI32(&B[0]) == (int32)GetUserIndexForDiagnostics())
		{
			UE_LOG(LogAsdaNet, Log, TEXT("6013: призыв от самого себя (UserIndex %d) — пропущен [ИСПРАВЛЕНИЕ]"), AsdaBytes::RdI32(&B[0]));
			break;
		}

		const FString Ip    = AsdaBytes::AnsiFieldTrim(&B[10], 16);   // N7: общая точка
		const int32   Port  = (int32)AsdaBytes::RdU16(&B[26]);
		const int32   MapId = (int32)B[7];
		const int32   TX    = (int32)AsdaBytes::RdU16(&B[28]);
		const int32   TZ    = (int32)AsdaBytes::RdU16(&B[30]);

		FString Caller;
		if (B.size() >= 82)
		{
			char NameBuf[21] = {};
			FMemory::Memcpy(NameBuf, &B[62], 20);
			Caller = FAsdaLoginData::DecodeCp1251((const uint8*)NameBuf, 20).TrimStartAndEnd();
		}
		// Нотис 104100 «%s зовёт тебя» — имя подставляется в шаблон.
		if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			// Нотис 104100 в таблице содержит подстановку имени — отдаём её как строковый аргумент.
			Msg->PostLocFmt(EAsdaMsgCategory::Social, 104100, { Caller }, {});
		}
		// Окно кампвара оригинал при призыве прячет (`campWarWnd_ToggleVisible(…, 0, 1)`).
		bCampWarHudSuppressed = true;
		// ★ 30.10.2026 (боевая фаза): `portalChannelTransferBegin` — при +0x2388 == 1 Toggle(0,1) + ResetState + ResetPositions (в Tick).
		if (UAsdaCampWarBattleSubsystem* CwB = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr)
		{
			CwB->OnLeaveByServerMove(TEXT("6013 призыв"));
		}

		FlushHotbarBeforeTransfer(TEXT("призыв игрока 6013"));
		StopTravelAndMovementForTransfer(TEXT("призыв 6013"));
		bTransferInProgress = true;
		ApplyGameServerFromPacket(Ip, Port, TEXT("6013"));   // ★ 16.10.2026: непубличный адрес → домен, порт из пакета
		EnterMapId  = MapId;
		SelfTileX   = TX; SelfTileZ = TZ;
		PortalTileX = TX; PortalTileZ = TZ;
		bPortalPending     = true;      // после загрузки — 5058, а не 4001 (как у любого портала)
		CurrentChannel     = (int32)B[9];
		bInGameConnected   = false;
		bPendingEnterWorld = true;
		if (Conn) { Conn->RequestStopDrain(); }
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[призыв] 6013 от «%s»: канал %d, сервер %s:%d, карта=%d, прибытие=(%d,%d)"),
			*Caller, CurrentChannel + 1, *GameServerIP, GameServerPort, MapId, TX, TZ);
		break;
	}
	// ★★★ 6201 0x1839 — КОРОТКИЙ ВАРП, эталон `portalHandleChannelWarp@0xF464C0` (`world_map.c:8460`).
	//   Тело 7 б: код@+0, карта u16@+1, тайл X u16@+3, тайл Y u16@+5. Канал-сервер НЕ меняется —
	//   это усечённая ветка 1 из 5055 без адреса. Коды: 0 → нотис 1096, 1 → переход,
	//   2 → 1097, 8 → 13916, 0xC → 2231, прочие игнорируются.
	//   ★ Отправителя нет в срезе GameServer (ни `6201`, ни `1839h` в 526 216 строках дизассемблера)
	//   — пакет приходит от Indun-серверов.
	case OP_PORTAL_HANDLE_CHANNEL_WARP:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 1, (int32)Pkt.Opcode)) { break; }
		const uint8 Code = B[0];

		if (Code != 1)
		{
			// ★ 27.09.2026: разбор отказа — общая точка с 5055 (`PostPortalRefusalNotice`), каналы по эталону:
			//   0/2 — плашка 221, 8/12 — плашка + журнал. Диапазона 13…20 у 6201 нет (эталон `return 0`).
			if (!PostPortalRefusalNotice(GetGameInstance(), 6201, Code, /*bRoseWarRange=*/false))
			{
				UE_LOG(LogAsdaNet, Log, TEXT("6201 код %u — оригинал такие игнорирует"), Code);
			}
			break;
		}

		if (!NetNeedLen(B, 7, (int32)Pkt.Opcode)) { break; }
		const int32 MapId = (int32)AsdaBytes::RdU16(&B[1]);
		const int32 TX    = (int32)AsdaBytes::RdU16(&B[3]);
		const int32 TZ    = (int32)AsdaBytes::RdU16(&B[5]);
		SelfTileX = TX; SelfTileZ = TZ;

		if (MapId == EnterMapId)
		{
			// Та же карта — просто перенос, соединение не трогаем.
			// ★ 22.10.2026: эталон `portalHandleChannelWarp@0xF464C0` грузит карту и тут (world_map.c:8500-8516) — окна шагов 0/11.
			AsdaEntryWindows::ApplyMapLoadWindowCloses(GetGameInstance(), TEXT("варп 6201 на ту же карту"), /*bLevelReload*/ false);
			OnSelfWarp.Broadcast(-1, TX, TZ);
			UE_LOG(LogAsdaNet, Log, TEXT("[варп] 6201 в пределах карты %d: (%d,%d)"), MapId, TX, TZ);
			break;
		}
		// Другая карта на ТОМ ЖЕ канал-сервере: адрес не меняем, дальше — обычная дорога портала.
		FlushHotbarBeforeTransfer(TEXT("короткий варп 6201"));
		StopTravelAndMovementForTransfer(TEXT("короткий варп 6201"));
		bTransferInProgress = true;
		EnterMapId  = MapId;
		PortalTileX = TX; PortalTileZ = TZ;
		bPortalPending     = true;
		bInGameConnected   = false;
		bPendingEnterWorld = true;
		if (Conn) { Conn->RequestStopDrain(); }
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[варп] 6201 смена карты на %d, прибытие=(%d,%d), канал-сервер прежний %s:%d"),
			MapId, TX, TZ, *GameServerIP, GameServerPort);
		break;
	}
	case OP_NET_HANDLE_CHANNEL_TRANSFER: // 6015 0x177F — ПЕРЕХОД НА КАНАЛ разрешён (netHandleChannelTransfer@0xF49660)
	{
		// Эталон (построчно): strcpy(g_szChannelServerIP, Source); word[8]=порт; word[9]/word[10]=spawn X/Y;
		//   word[11]=mapId; Source[24]=НОВЫЙ номер канала (g_nCurrentChannel); Source[25]=g_byPortalChannelParam.
		//   Далее оригинал гасит HUD, ставит экран загрузки и переводит состояние в 11 → главный цикл делает
		//   реконнект на канал-сервер и повторный enter-хендшейк. В порту тот же путь = EnterGameWorld().
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 26)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("6015 тело мало (%d<26) — переход канала пропущен"), (int32)B.size());
			break;
		}
		const FString Ip   = AsdaBytes::AnsiFieldTrim(B.data(), 16);   // N7: общая точка
		const int32   Port = (int32)AsdaBytes::RdU16(&B[16]);
		const int32   MapId= (int32)AsdaBytes::RdU16(&B[22]);
		ApplyGameServerFromPacket(Ip, Port, TEXT("6015"));   // ★ 16.10.2026: непубличный адрес → домен, порт из пакета
		EnterMapId = MapId;
		CurrentChannel     = (int32)B[24];
		PortalChannelParam = B[25];
		FlushHotbarBeforeTransfer(TEXT("смена канала 6015"));
		StopTravelAndMovementForTransfer(TEXT("смена канала 6015"));
		bTransferInProgress = true;
		bInGameConnected = false;                 // старое game-соединение закрывается в EnterGameWorld()
		if (CVarAsdaNetChannelTransferPortalLogon.GetValueOnGameThread() != 0)
		{
			// [ОРИГИНАЛ] состояние 11, как у портала: клетка прибытия +18/+20 → 5058 после загрузки (шапка ключа выше).
			const int32 SX = (int32)AsdaBytes::RdU16(&B[18]);
			const int32 SZ = (int32)AsdaBytes::RdU16(&B[20]);
			SelfTileX   = SX; SelfTileZ   = SZ;
			PortalTileX = SX; PortalTileZ = SZ;
			bPortalPending = true;                // Load Manager после загрузки → SendCharPortalLogonReq (5058)
			if (Conn) { Conn->RequestStopDrain(); }   // пакеты прежнего канала дальше не разбираем (как 5055/6013/6201)
		}
		UE_LOG(LogAsdaNet, Log, TEXT("6015 переход на канал %d: сервер %s:%d карта=%d спавн=(%u,%u) param=%u"),
			CurrentChannel + 1, *GameServerIP, GameServerPort, MapId,
			(uint32)AsdaBytes::RdU16(&B[18]), (uint32)AsdaBytes::RdU16(&B[20]),
			PortalChannelParam);
		OnChannelChanged.Broadcast(CurrentChannel);
		bPendingEnterWorld = true;                // реконнект + хендшейк 20000 на следующем тике
		break;
	}
	case OP_NET_RECV_RESPAWN_RESULT: // 6004 0x1774 — Recv_RespawnResult@0xF3A840: результат входа в мир (ФАЗА B)
	case OP_RESPAWN_RESULT:          // 6203 0x183B — та же логика (канальный варп, netSendAnticheatTsMask2)
	{
		// ★ Реверс (все ветки): body[0]=результат. 2=УСПЕХ → 5056 прежнему слоту + g_bInGameConnected=1 + загрузка карты;
		//   0/1/3=fail → нотис loc 1001/302 + откат (пока лог). Только byte[0] читается.
		const std::vector<uint8>& B = Pkt.Body;
		const uint8 Res = (NetNeedLen(B, 1, (int32)Pkt.Opcode)) ? B[0] : 0xFF;
		if (Res == 2)
		{
			// ★★★ 18.10.2026: эталон шлёт 5056 ПРЕЖНЕМУ слоту (6004 → `netSendAnticheatTsMask`, `Field8`) — порт отправил
			//   его старой карте в EnterGameWorld(). По новому соединению — только 6203 (`netSendAnticheatTsMask2`, `Field1C`;
			//   сервер 6203 в этой сборке не шлёт) или при откате `asda.Net.PortalLogoutOnOldConn 0`.
			if (Pkt.Opcode == (uint16)OP_RESPAWN_RESULT || CVarAsdaNetPortalLogoutOnOldConn.GetValueOnGameThread() == 0)
			{
				SendGameAck5056();
			}
			bInGameConnected = true;    // g_bInGameConnected=1 — main loop ждёт этот флаг
			ResetNetHeartbeatStamp();   // эталон: ответ 6004 сбрасывает g_LastNetTickStamp (clock_re.md §4.1)
			// ★ Переход завершён — но ТОЛЬКО если это подтверждение пришло уже с НОВОГО соединения.
			//   Живой разбор 24.08.2026: у старого сокета в приёмном буфере остаются пакеты, и
			//   «успех входа» от предыдущей карты прилетал через 20 мс после начала перехода.
			//   Флаг снимался раньше времени, автопуть тут же оживал, слал шаг в неопознанное
			//   соединение — и сервер рвал связь. Признаки незавершённого перехода:
			//   `bPendingEnterWorld` — реконнект ещё не выполнен; `bPortalPending` — портальный
			//   логон 5058 ещё не отправлен (он сам сбрасывает этот флаг при отправке).
			if (bTransferInProgress && !bPendingEnterWorld && !bPortalPending)
			{
				bTransferInProgress = false;
				UE_LOG(LogAsdaNet, Log, TEXT("[переход] завершён — отправка пакетов разблокирована"));
			}
			else if (bTransferInProgress)
			{
				UE_LOG(LogAsdaNet, Log,
					TEXT("[переход] «успех входа» пришёл до конца перехода (реконнект=%d, логон=%d) — гейт держим"),
					bPendingEnterWorld ? 1 : 0, bPortalPending ? 1 : 0);
			}
			// ★ Страховка 6616: вход в мир = новое соединение и новый CUser на том процессе
			//   GameServer, куда мы вошли (мир и подземелья — РАЗНЫЕ процессы одного бинаря,
			//   см. start.bat), а серверное поле CUser+0x1E у нового пользователя снова 0.
			//   Поэтому бюджет отправок пересчитываем с нуля на каждый вход.
			Unacked6616 = 0;
			bAnnounce6616Blocked = false;
			UE_LOG(LogAsdaNet, Log, TEXT("%u: вход в мир ПРИНЯТ (result=2), загрузка карты mapId=%d"), (uint32)Pkt.Opcode, EnterMapId);
			// ★ ТОТ ЖЕ ОТЛОЖЕННЫЙ ПАТТЕРН: загрузка уровня = травел, он уничтожает мир и всё, что в нём;
			//   запускать её из колбэка пакета (мы внутри DrainFrames) нельзя. Ставим флаг — Tick выполнит.
			bPendingMapLoad = true;
			PendingMapLoadId = EnterMapId;
			if (Conn) { Conn->RequestStopDrain(); }
		}
		else
		{
			// Эталон: 0 → нотис loc 1001; 1/3 → loc 302; затем channel==0 → g_GameState=5 (char-select),
			//   channel==1 → в игру. Порт: делегат — GameMode вернёт на char-select и покажет нотис.
			UE_LOG(LogAsdaNet, Warning, TEXT("6004: вход ОТКЛОНЁН (result=%u) → нотис loc %d + возврат char-select"),
				Res, (Res == 0) ? 1001 : 302);
			bInGameConnected = false;
			OnEnterWorldFail.Broadcast((int32)Res);
		}
		break;
	}
	case OP_RECV_PACKET_SPAWN_SELF: // 4003 0xFA3 — спавн self (9б): ПОЗИЦИЯ игрока (финал DB-бурста сервера)
	{
		// Сервер CharMake@0x543A10: [0..1] userNum, [2..3] tileX u16, [4..5] tileZ u16, [6..7] regenState
		//   (==200 → вход МЁРТВЫМ, эталон g_GameState=8), [8] байт. Мир = тайл*100+50, Y=террейн.
		// ★★★ [ОРИГИНАЛ] ДЕФЕКТ, вскрыт дизасмом 10.09.2026 (сверка опкодов входа под GM-скрытие).
		//   4003 шлют ДВА серверных пути, и раскладка тела у них РАЗНАЯ:
		//     · `CharMake`       — буфер `[ebp-0x14]`, 9 б: [6..7] = `*(WORD*)(CUser+0x54E4)`
		//                          (поле состояния действия — то самое, чей миснейм `Net_GetCurrentMapType`);
		//     · `CharPortalMake` — буфер `[ebp-0x10]`, 9 б: байты `[ebp-0x0A]`/`[ebp-0x09]`, то есть
		//                          НАШИ [6..7], НЕ ЗАПИСЫВАЮТСЯ ВООБЩЕ (переменных `var_A`/`var_9` в
		//                          кадре функции нет — проверено по списку локалей). В пакет уходит
		//                          мусор стека.
		//   ⇒ «вход мёртвым» достоверен только на ПЕРВОМ входе; после портала это чтение мусора.
		//   Поведение оставлено как у оригинала СОЗНАТЕЛЬНО (закон `old-server-protocol-compat`:
		//   сервер старый, а «починка» здесь означала бы игнорировать поле и на честном входе тоже).
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() >= 8)
		{
			SelfUserNum = (uint16)AsdaBytes::RdU16(&B[0]);   // ★ in-world userNum сервера → move-поле
			bSelfUserNumKnown = true;   // ★ ноль — ЗАКОННЫЙ номер, признак «известен» держим отдельно
#if ASDA_COMBAT_TRACE
			AsdaCombatTrace::SetSelfUserIdx((int32)SelfUserNum);   // журнал боя: свой userIdx — фильтр «мой ли это бой»
#endif
			// ★ Аудит №3 (знаковость): эталон `Recv_SpawnSelf` (вызов `j_RecvPacket_SpawnSelf@0x9389B4`)
			//   принимает тело как `__int16 *pktBody` и читает ВСЕ слова ЗНАКОВО: `*pktBody` (userNum),
			//   `pktBody[1]`/`pktBody[2]` → `CMap_GetTileAt(map, vec, x, z)`, `pktBody[3] == 200`
			//   (вход мёртвым). Порт читал тайлы и regenState БЕЗ знака — приведено к эталону.
			SelfTileX = AsdaBytes::RdI16(&B[2]);
			SelfTileZ = AsdaBytes::RdI16(&B[4]);
			bSelfSpawnDead = (AsdaBytes::RdI16(&B[6]) == 200);
			// ★★★ 25.10.2026 [ОРИГИНАЛ] ПОКАЗ ШЛЕМА — байт [8] (`Recv_SpawnSelf@0xF3E630`, network.c:40581: `== 1` → 1, иначе 0).
			//   Сервер пишет его в ОБОИХ путях 4003 (`CharMake`/`CharPortalMake`: `mov dl, [CUser+4A6h]`), поэтому флаг из БД
			//   (`Charoption.Hat`) приходит и на входе, и после портала. Короткое тело (8 б) — байта нет: 1, как конструктор.
			if (B.size() >= 9) { SelfHelmetShow = (B[8] == 1) ? 1 : 0; }
			else
			{
				SelfHelmetShow = 1;
				UE_LOG(LogAsdaNet, Warning, TEXT("4003: тело %d б — байта показа шлема [8] нет, показ по умолчанию"), (int32)B.size());
			}
			// ★ Состояние жизни на входе задаёт ИМЕННО этот пакет (серверный regenState из БД) —
			//   сбрасываем локальное зеркало, чтобы EnterSelfDeadState отработал на новом входе.
			bSelfDead = false;
			UE_LOG(LogAsdaNet, Log, TEXT("SpawnSelf 4003: userNum=%u tile=(%d,%d) мир=(%d,%d) мёртв=%d шлем=%u"),
				SelfUserNum, SelfTileX, SelfTileZ, SelfTileX * 100 + 50, SelfTileZ * 100 + 50, bSelfSpawnDead ? 1 : 0,
				(uint32)SelfHelmetShow);

			// ★★★ [ОРИГИНАЛ] ГМ-СКРЫТИЕ ПРИ ВХОДЕ. 4003 шлют ОБА серверных пути, которые пишут в
			//   `CUser+0x3BC3B` значение 300: `CharMake` (вход в мир) и `CharPortalMake` (портал,
			//   смена карты). В обоих 4003 уходит СТРОКОЙ ВЫШЕ этой записи, поэтому сам пакет
			//   несёт ещё старое состояние — флаг сюда не приезжает и приехать не может.
			//   ★ ПЕРЕПРОВЕРЕНО ДИЗАСМОМ 10.09.2026, обе точки, дословный порядок инструкций:
			//       push 9 / push 0FA3h / call sub_4C28F0        ← 4003 УШЁЛ
			//       push 0FFFFFFFFh / push a2 / call sub_52ECF0  ← операторский гейт
			//       test eax,eax / jz  …
			//       mov word ptr [reg+3BC3Bh], 12Ch              ← 300 записано ПОСЛЕ отправки
			//       mov word ptr [reg+54E4h],  12Ch
			//     `CharMake`       — GameServer.exe.asm:447249…447262 (метка loc_543F44);
			//     `CharPortalMake` — GameServer.exe.asm:447882…447895 (метка loc_5447E8).
			//   Всего ссылок на `+0x3BC3B` в бинаре 17 (Hex-Rays видит только 5 — ложное «нет»,
			//   закон `false-negatives-method`); из них ПИШУТ ровно три функции: эти две и
			//   `Send_HideCommand` (:462054/462068, 300/301 по нашему 6005).
			//   Условие сервера воспроизводим дословно (см. `IsSelfOperatorAccount`).
			//   ⚠ Порядок важен: признак взводится ДО `OnSelfSpawn`, иначе мир-подсистема
			//   заспавнит павна видимым и прозрачность включится только со следующего пакета.
			//   ⚠ Ранние данные НЕ теряются: сброс сессии по причине `EnteringWorld` приходит из
			//   обработчика 1021 ЛОГИН-сервера, то есть ДО подключения к игровому серверу и,
			//   значит, до этого 4003 (закон `session-reset-drops-early-data` проверен специально).
			//   ★ [ПОРТ] Критерий вынесен в общую точку `ApplyGmHideOnEntry` и с 10.09.2026
			//   складывается по ИЛИ из двух слагаемых: индекс учётки (как у сервера) и префикс
			//   ИМЕНИ ПЕРСОНАЖА (наша надстройка, сервер имён не читает). Обоснование обоих —
			//   у объявлений в заголовке; имя здесь уже известно, потому что берётся из записи
			//   char-list, а она приходит за минуты до входа (доказательство логом — там же).
			ApplyGmHideOnEntry(TEXT("вход/портал (4003)"));

			OnSelfSpawn.Broadcast();   // B4: мир-система ставит персонажа+камеру
		}
		else { UE_LOG(LogAsdaNet, Warning, TEXT("4003 тело мало (%d)"), (int32)B.size()); }
		break;
	}
	case OP_RECV_PACKET_PLAYER_APPEARANCE: // 5417 0x1529 — современный алиас 4005 (тот же хендлер в эталоне)
	case OP_RECV_PLAYER_APPEARANCE:        // 4005 0xFA5 — внешность (151б; self И СОСЕДИ по AoI 3×3 сектора)
	{
		// Recv_PlayerAppearance@0xF3CD30 (клиент: staging-кэш 0x303). Сервер шлёт 4005 двунаправленно
		//   (Send_MoveSectorInClippingUserInfo@0x41AD20): вход в мир/пересечение сектора/портал/стоп/реген.
		//   Первый 4005 после 4001 — наш (CharMake); остальные с чужим userIdx = СПАВН-триггер соседа.
		const uint8* B = Pkt.Body.data();
		const int32  N = (int32)Pkt.Body.size();
		if (N < 151) { UE_LOG(LogAsdaNet, Warning, TEXT("4005 тело мало (%d)"), N); break; }
		const uint16 Uidx = (uint16)AsdaBytes::RdU16(&B[0]);
		// ★ Self ТОЛЬКО по userNum из 4003 (мир-цепочка гарантирует 4003 ДО 4005-бурста — живой лог).
		//   Прежний критерий «первый 4005 = свой» съедал СОСЕДА, если его 4005 приходил первым
		//   (живой лог: Drama uidx=16 проглочена как self при нашем userNum=5 → игрок-невидимка).
		// ★ 05.11.2026 (asda.Net.SelfDetectByCharId): до 4003 — по charId @11 против своего из 1017; charId неизвестен — прежний признак.
		const bool bSelfByCharId = CVarSelfDetectByCharId.GetValueOnGameThread() != 0 && SelectedCharId != 0;
		const bool bSelf = bSelfUserNumKnown ? (Uidx == (int32)SelfUserNum)
			: (bSelfByCharId ? (AsdaBytes::RdI32(&B[11]) == SelectedCharId) : (SelfAppearance.Num() == 0));
		if (!bSelfUserNumKnown)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("4005 (uidx=%u) ДО 4003 — self-детект по порядку (ненадёжно)"), Uidx);
		}
		if (bSelf)
		{
			SelfAppearance.SetNum(N);
			FMemory::Memcpy(SelfAppearance.GetData(), B, N);
			if (B[40] > 0) { SelfLevel = B[40]; }   // ★ HUD: уровень self (@+40, как у соседей) → EXP-бар
			// ★ КЛАСС self (@+38 — то же поле, что у соседей идёт в FAsdaRemotePlayer::ClassId).
			//   Это байт `CCharacter+0x438`, по нему мини-статус выбирает иконку класса
			//   (hudRefreshClassAndElementIcons@0xE79850). 0 = «Новичок» — ВАЛИДНОЕ значение,
			//   поэтому пишем всегда, а «неизвестно» кодируется −1 до первого 4005/char-записи.
			SelfClassId = (int32)B[38];
			// ★ 19.10.2026 (ревизия квестов): ПОДКЛАСС self @+39 (entry+56 → char+1081 = `CCharacter+0x439`, то же поле,
			//   что у соседей идёт в FAsdaRemotePlayer::SubClass) — условия класса NSR читают его вместе с +0x438.
			SelfSubJob = (int32)B[39];
			// ★ ПОЛ СВОЕГО персонажа из снапшота появления — независимый контрольный канал к блоку 1017
			//   (тот же байт char+1079; см. раскладку у FAsdaRemotePlayer). 0 = сервер не заполнил.
			CaptureSelfGenderFromSpawn(B[37], TEXT("4005-self"));   // Д7: единый захват (пол @37)
			UE_LOG(LogAsdaNet, Log, TEXT("Appearance 4005 (self, userIdx=%u): %d б сохранено, level=%u, пол@37=%u (созвездие@47=%u)"),
				Uidx, N, (uint32)SelfLevel, (uint32)B[37], (uint32)B[47]);
			break;
		}
		// Сосед: wire→FAsdaRemotePlayer (раскладка подтверждена ОБЕИМИ сторонами, remote_player_display_*.md).
		FAsdaRemotePlayer P;
		P.UserIdx  = Uidx;
		P.CharId   = AsdaBytes::RdI32(&B[11]);
		P.Name     = FAsdaLoginData::DecodeCp1251(B + 15, 20);
		P.ClassId  = B[38];                     // entry+55 → char+1080 (класс)
		// ★ ПОДКЛАСС — entry+56 → char+1081. Нужен окну `OTHERCHARA_DATA` 4200: эталон
		//   `CPlayerInfoWin_SetData@0xDA7E30` показывает имя ПОДКЛАССА, когда он не нуль.
		P.SubClass = B[39];
		P.Level    = B[40];                     // entry+57 → char+1082 (уровень)
		// ★★★ ПОЛ = body[37] (entry+54 → char+1079 = CCharacter::gender). ИСПРАВЛЕНО 2026-08-07:
		//   порт читал body[47] — это entry+95 → char+1120 = СОЗВЕЗДИЕ, из-за чего пол соседа
		//   получался «созвездие==2 ? жен : муж», т.е. почти все игроки рисовались мужчинами.
		//   Доказательство раскладки — комментарий к FAsdaRemotePlayer в AsdaNetSubsystem.h.
		P.Gender        = B[37];
		P.Constellation = B[47];                // entry+95 → char+1120 (созвездие; НЕ пол)
		P.Hair     = B[48]; P.HairColor = B[49]; P.Face = B[50];
		// ★ 25.10.2026 [ОРИГИНАЛ] ПОКАЗ ШЛЕМА соседа — 4005 [6] (разбор у `FAsdaRemotePlayer::HelmetShow`): ≠ 0 → показ.
		P.HelmetShow = (B[6] != 0) ? 1 : 0;
		float Px, Pz; FMemory::Memcpy(&Px, B + 56, 4); FMemory::Memcpy(&Pz, B + 60, 4);
		P.PosGame  = FVector2D(Px, Pz);
		for (int32 i = 0; i < 11; ++i)
		{
			const int32 O = 96 + 5 * i;
			P.EquipItemId[i] = AsdaBytes::RdI32(&B[O]);
			// ★ 5-й байт записи = УРОВЕНЬ ЗАТОЧКИ (эталон network.c:40301-40305 кладёт его в
			//   инстанс+0x18; сервер GameServer.c:11368-11372 берёт из CUser+444+57i+24).
			P.EquipGrade[i]  = B[O + 4];
		}
		if (P.EquipGrade[9] > 0 || P.EquipGrade[8] > 0)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("[заточка] появление 4005 userIdx=%u: оружие itemId=%d grade=%d, щит itemId=%d grade=%d"),
				Uidx, P.EquipItemId[9], (int32)P.EquipGrade[9], P.EquipItemId[8], (int32)P.EquipGrade[8]);
		}
		UE_LOG(LogAsdaNet, Log, TEXT("Игрок рядом (%u): userIdx=%u '%s' класс=%u lvl=%u pos=(%.0f,%.0f) шлем=%u"),
			Pkt.Opcode, Uidx, *P.Name, P.ClassId, P.Level, Px, Pz, (uint32)P.HelmetShow);
		// ★ ТРАНСПОРТ ИЗ ПОЯВЛЕНИЯ: экип-слот 10 = RIDING (эталон Recv_PlayerAppearance@0xF3CD30:
		//   11×5б @+96 → слот 10 @+146; staging+696 → GameNet_OnUserSpawn → charMgr_createRemoteEntity →
		//   SetPartsModelByType(тип 10) → RidingSystem — та же логика, что живой 5453).
		//   Кэш MountByUserIdx наполняется ДО спавна актора (OnEnvBatch ниже) — применится при спавне.
		if (P.EquipItemId[10] > 0)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("[транспорт] из появления 4005: userIdx=%u itemCode=%d"), Uidx, P.EquipItemId[10]);
			OnMountState.Broadcast((int32)Uidx, P.EquipItemId[10], true);
		}
		else
		{
			// ★ СНИМОК ПОЯВЛЕНИЯ АВТОРИТЕТЕН (баг 2026-07-31): слот RIDING пуст ⇒ игрок ПЕШИЙ, даже если
			//   в кэше висит транспорт от предыдущего владельца этого userIdx (сервер переиспользует слоты
			//   сессий). Снимаем явно: полагаться на парный 6154 нельзя — при отсутствии премиум-записей
			//   сервер его не шлёт, и стейл-кэш доживал до спавна («вошедший появился верхом на чужом»).
			OnMountState.Broadcast((int32)Uidx, 0, false);
		}
		RequestedUserInfo.Remove((int32)Uidx);   // снапшот пришёл — запрос 5416 исполнен
		// ★ Волна Л4/Л5 торговли (ревизия Л3, К-9 [ИСПРАВЛЕНИЕ]): МАРКЕР «появление charId X» в очередь вывесок лавок — В ПОРЯДКЕ
		//   ПОТОКА (тот же делегат, что у 6304, поэтому порядок 4005 → 6304 снапшота сохраняется, а отставание спавна актора не
		//   мешает). [ПРОТОКОЛ] Сервер шлёт 4005 только из `Send_MoveSectorInClippingUserInfo` и в ветках «данные соседа входящему»
		//   досылает 6304 про этого соседа тогда и только тогда, когда его лавка открыта (GameServer.c:11935-11943, :12695-12703) —
		//   значит всё, что про X пришло раньше, устарело. Тело маркера — 4 б: i32 charId. Разбор — `UAsdaStallSignSubsystem` из
		//   своего Tick (колбэк только копит; закон `no-reentrant-net-callbacks`); прочие подписчики опкод 4005 игнорируют.
		if (P.CharId != 0)
		{
			TArray<uint8> Mark; Mark.SetNumZeroed(4);
			AsdaBytes::WrI32(Mark.GetData(), P.CharId);
			OnSelfStorePacket.Broadcast((int32)OP_RECV_PLAYER_APPEARANCE, Mark);
		}
		PendingRemotePlayers.Push(MoveTemp(P));   // ★ A2 (аудит №4): общая точка TAsdaNetQueue
		OnEnvBatch.Broadcast();   // мир-система задренит (спавн актора с фейд-ин)
		break;
	}
	default: bHandled = false; break;
	}
}
