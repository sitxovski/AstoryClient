// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_World.cpp — ★ 22.10.2026 часть UAsdaNetSubsystem, вынесенная из AsdaNetSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_net_2026-10-22/plan.json).
//   Окно разговора с NPC (портал 5477, события, обмен NPC, рейд-босс, прыжок к цели), звания, война за лагерь/города,
//   свитки призыва и переход канала, питомцы, задания, знак зодиака, добыча 5428, консоль asda.War.*.
//   #include — те же, что у AsdaNetSubsystem.cpp, в том же порядке; общие хелперы — AsdaNetSubsystemInternal.h.
#include "AsdaNetSubsystem.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (4026/4028/5256)
#include "AsdaTimedSend.h"                     // ★ 15.10.2026: поток точной отправки (стендовая постановка 4028)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaSoulmateSkillSubsystem.h"
// ★ гейт продолжения боя: обездвиживающие состояния   ★ R3: комментарий снятого включения "UI/AsdaEffectsSubsystem.h" (файл им не пользовался) — сохранён
#include "AsdaConnection.h"
#include "Net/NetOpcodes.h"
#include "Misc/SecureHash.h"
#include "Login/AsdaLoginData.h"
#include "AsdaBytes.h"   // D4b: единые читалки little-endian вместо лямбд/открытых выражений в каждом хендлере
#include "AsdaPacketWriter.h"   // ★ волна 3 п.10: единый сборщик тел исходящих пакетов (вместо uint8 P[N] + WrI16)
#include "Login/AsdaText.h"                 // имена класса/созвездия из loc-таблиц
// раскладка 0x3EF по itemType   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
// 6004 успех → BeginMapLoad (Load Manager)   ★ R3: комментарий снятого включения "World/AsdaLoadManagerSubsystem.h" (файл им не пользовался) — сохранён
// loc-строки форматов чата (20030/1187/1109/8200/…)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// RankColor / SlotColor — таблицы каналов и рангов   ★ R3: комментарий снятого включения "UI/AsdaChatWindowSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaInventorySubsystem.h"        // Дд6: FAsdaItemInstance::kRecordSize (57 б) — длина записи предмета
#include "UI/AsdaMessageSubsystem.h"    // 5055 отказ портала: нотисы 1096/1097 в системный чат
#include "UI/AsdaNotice.h"              // ★ 27.09: отказы 5055/6201 — плашка 221 и пара «плашка + журнал» (общие точки)
#include "UI/AsdaChatSubsystem.h"       // ★ 27.09: ColGold() = эталонный −11514 отказов перехода
#include "UI/AsdaUiHelpers.h"           // ★ 08.11.2026: FormatPrintfArgs — строка 111920 при приёме 6743
#include "UI/AsdaNoticeColors.h"        // ★ 08.11.2026: AsdaNoticeColor::Gold (−11514)
#include "UI/AsdaHotbarSubsystem.h"     // сохранение раскладки панели перед переходом (hotbar_SendAllLayouts)
#include "World/AsdaPossessGate.h"      // ★ гейт действий при одержимости (SoulState==2), см. шапку заголовка
// ★ единый шлюз исходящих пакетов (E1, 14.09.2026)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (итог и `quit` диагностики asda.Gm.TestEntryHide)
#include "UI/AsdaEntryWindows.h"               // ★ 22.10.2026: окна при загрузке карты — список эталона
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
#include "AsdaNetSubsystemInternal.h"   // ★ 22.10.2026 разнос: общее для частей AsdaNetSubsystem*.cpp (LogAsdaNet, чистые хелперы)


// ★ Ревизия six_items СРЕДН-1 (09.10.2026): 5428 «конец добычи» — разбор у объявления.
bool UAsdaNetSubsystem::SendMiningEnd5428(uint8 SubMode)
{
	FAsdaPacketWriter W(3);
	W.U16(0, SelfUserNum);   // свой userNum (из 4003; не charId — закон charid-vs-usernum)
	W.U8(2, SubMode);        // эталон: 1
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_E_MOTION_MINING_END, TEXT("SendMiningEnd5428"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_E_MOTION_MINING_END, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5428 конец добычи: userNum=%u режим=%u (тело 27 б) -> %s"),
		(uint32)SelfUserNum, (uint32)SubMode, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ════════════════ ОКНО РАЗГОВОРА С NPC: отправители диспетчера команд (этап Э3) ═════════════════
bool UAsdaNetSubsystem::SendPortalMove5477(int32 ReqWord)
{
	FAsdaPacketWriter W(2);
	W.I16(0, (int32)(uint16)(int16)ReqWord);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CONTINENT_PORTAL_MOVE, TEXT("SendPortalMove5477"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CONTINENT_PORTAL_MOVE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5477 портал/телепорт: слово=%d (26 б) -> %s"),
		ReqWord, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 27.09.2026 — 1:1 с `netSendPacket6059@0xD8CEA0` (network.c:23260-23401). [ОРИГИНАЛ]
//   • Слово +0 — СВОЙ userNum: вызов `netSendPacket6059(pThis, localPlayer->effectSlots[111].field_5B8, …)` (game_main.c:10119),
//     а `effectSlots[111].field_5B8` в типе IDB = `char+190188` (та же строка `LOWORD(…field_5B8) % 10` в export:54753 =
//     `*(u16*)(char+190188) % 10` в entity_char.c:7051). Прежде здесь уходил charId, урезанный до слова. Сервер
//     `EventNcpProcessReq@GameServer.c:130547` тело копирует, но полей НЕ читает (берёт userNum сессии) — поведение сервера то же.
//   • «Раз в календарный день на цель»: `if (self[24115]==1 && self[24116]==targetId && (int)self[24117]>0 && self[24117]==wDay)` →
//     нотис 7501 окном (0, 24), пакета нет; иначе поля взводятся ДО отправки. Прежде этой проверки в порту не было.
bool UAsdaNetSubsystem::SendEventNpc6059(int32 InSelfUserNum, int32 NpcId, int32 TargetId)
{
	const int32 Today = (EventNpcDayOverrideForTest > 0) ? EventNpcDayOverrideForTest : FDateTime::Now().GetDay();   // `GetLocalTime().wDay`
	if (EventNpcDayArmed == 1 && EventNpcLastTarget == TargetId && EventNpcDay > 0 && EventNpcDay == Today)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("→6059 событийный NPC: цель %d сегодня (день %d) уже была — нотис 7501 окном (0, 24), пакет не отправлен"),
			TargetId, Today);
		AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Trade, 7501, AsdaLoc::kBtnOk, /*EtalonNoticeId*/24);
		return false;
	}
	EventNpcDay = Today;          // `self[24117] = wDay`
	EventNpcDayArmed = 1;         // `self[24115] = 1`
	EventNpcLastTarget = TargetId; // `self[24116] = targetId`
	FAsdaPacketWriter W(6);
	W.I16(0, (int32)(uint16)InSelfUserNum);   // свой userNum (u16)
	W.I16(2, (int32)(uint16)NpcId);
	W.I16(4, (int32)(uint16)TargetId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_EVENT_NCP_PROCESS_REQ, TEXT("SendEventNpc6059"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_EVENT_NCP_PROCESS_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6059 событийный NPC: userNum=%u npcId=%d цель=%d (30 б) -> %s"),
		(uint32)(uint16)InSelfUserNum, NpcId, TargetId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendNpcExchange6098(int32 InSelfUserNum, int32 NpcId, int32 ExchangeType)
{
	// Троттлинг эталона: `timeGetTime() - this[27930] >= 0xBB8` (3000 мс). Раньше срока пакет
	//   просто НЕ уходит — ни сообщения, ни звука (воспроизводим).
	const double Now = FPlatformTime::Seconds();
	if (LastNpcExchangeSendTime > 0.0 && (Now - LastNpcExchangeSendTime) < 3.0)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("→6098 обмен с NPC: троттлинг эталона (%.0f мс из 3000) — пакет не отправлен"),
			(Now - LastNpcExchangeSendTime) * 1000.0);
		return false;
	}
	// ★ 27.09.2026 [ОРИГИНАЛ]: прошедший троттлинг пишет тип в ОБЩЕЕ с 6059 поле `self[24116] = targetId` (network.c:23434) —
	//   так эталон «забывает» цель 6059, и после обмена у NPC суточный отказ 6059 на прежнюю цель снимается.
	EventNpcLastTarget = ExchangeType;
	FAsdaPacketWriter W(6);
	W.I16(0, (int32)(uint16)InSelfUserNum);
	W.I16(2, (int32)(uint16)NpcId);
	W.I16(4, (int32)(uint16)ExchangeType);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_EVENT_ITEM_REQUEST_REQ, TEXT("SendNpcExchange6098"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_EVENT_ITEM_REQUEST_REQ, W.Data(), W.Num());
	if (bOk) { LastNpcExchangeSendTime = Now; }
	// ★ 27.09.2026: первое слово — СВОЙ userNum (эталон `effectSlots[111].field_5B8`); вызывающие передают `SelfUserNum`.
	UE_LOG(LogAsdaNet, Log, TEXT("→6098 обмен с NPC: userNum=%u npcId=%d тип=%d (30 б) -> %s"),
		(uint32)(uint16)InSelfUserNum, NpcId, ExchangeType, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 27.09.2026 — ДВА ДЕФЕКТА ПОРТА исправлены (отчёт `Reports/owner_0927/charid_senders_audit.md`). [ОРИГИНАЛ]
//   1. +0 — charId: вызовы `netSendGuarded_op6124_invMatchList(pThis, pLocalPlayer3->userIdx, …)` (game_main.c:10215, :10258),
//      `CCharacter::userIdx` = `char+1052` = charId (миснейм). Порт клал userNum. Сервер `Recv_RaidMonsterCallReq`
//      (GameServer.c:17788, сверка :17832 `*(DWORD*)v12 == *(DWORD*)(CUser+372)`, лог :17917) на расхождении пишет «UserInfo incorrect…
//      RecvUserIndex %d, ServerUserIndex %d» и отвечает 6125 с кодом 2 — призыв НЕ работал никогда.
//   2. Пустые записи — {i32 −1, u8 0, i16 −1}: тело инициализирует `sub_D8E2B0` (`*this = −1` + 6 × `sub_973DF0`). Сервер
//      идёт по записям `for (i < 6 && itemCode != −1)`, поэтому нулевой хвост порта (код 0) читался как ячейка (0, 0) → отказ.
bool UAsdaNetSubsystem::SendRaidMonsterCall6124(int32 SelfCharId, int32 MatchItemCode, int32 MaxMatches)
{
	// Эталон сканирует 60 ячеек контейнера 2 (шаг 57 б) и кладёт до `MaxMatches` совпадений.
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	FAsdaPacketWriter W(46);                    // payload 0x2E
	W.I32(0, SelfCharId);
	for (int32 e = 0; e < 6; ++e)               // пустая запись эталона `sub_973DF0`: код −1, контейнер 0, слот −1
	{
		W.I32(4 + 7 * e, -1);
		W.U8(4 + 7 * e + 4, 0);
		W.I16(4 + 7 * e + 5, -1);
	}
	int32 Found = 0;
	// Стенд `asda.Net.PacketStand` подменяет ячейки контейнера 2 (у его объекта нет инвентаря); в игре
	//   перехватчик пуст ⇒ StandCells == nullptr и работает прежний путь через `Inv`.
	const TArray<int32>* StandCells = IsStandCaptureAll() ? &StandContainer2CodesForStand : nullptr;   // ★ волна 14: только стенд
	if (Inv || StandCells)
	{
		const int32 Size = StandCells ? StandCells->Num() : Inv->GetContainerSize(2);
		for (int32 i = 0; i < Size && Found < FMath::Min(MaxMatches, 6); ++i)
		{
			if ((StandCells ? (*StandCells)[i] : Inv->GetCellItemCode(2, i)) != MatchItemCode) { continue; }
			const int32 Rec = 4 + 7 * Found;            // запись 7 б; Found ≤ 5 ⇒ последняя кончается ровно на 46
			W.I32(Rec, MatchItemCode);
			W.U8(Rec + 4, 2);                           // контейнер записи (эталон берёт поле ячейки)
			W.I16(Rec + 5, i);                          // номер слота
			++Found;
		}
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_RAID_MONSTER_CALL_REQ, TEXT("SendRaidMonsterCall6124"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_RAID_MONSTER_CALL_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6124 призыв рейд-босса: charId=%d предмет=%d записей=%d/%d (70 б) -> %s"),
		SelfCharId, MatchItemCode, Found, MaxMatches, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 27.09.2026 [ОРИГИНАЛ]: +0 — СВОЙ userNum. Эталон `craftPacket[0] = Target_GetSelectedUserIdx(Field142C)` с Field142C =
//   локальный игрок, а `Target_GetSelectedUserIdx@0x9254E5` — thunk на `Char_GetHoveredUserIdx@0x966490` = `*(u16*)(this+190188)`,
//   то есть номер ТОГО, кого передали (миснейм). Порт клал userNum выбранной цели (−1 без цели). Сервер (case 0x17CB,
//   GameServer.c:19408) пакет в обработчик не отдаёт (`unknown_libname_8`) — поведение сервера то же.
bool UAsdaNetSubsystem::SendItemCraft6091(int32 InSelfUserNum, int32 NpcId, int32 Count)
{
	FAsdaPacketWriter W(6);
	W.I16(0, (int32)(uint16)(int16)InSelfUserNum);
	W.I16(2, (int32)(uint16)NpcId);
	W.I16(4, (int32)(uint16)Count);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_ITEM_SEND_CRAFT_REQUEST_EEE0, TEXT("SendItemCraft6091"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_ITEM_SEND_CRAFT_REQUEST_EEE0, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6091 крафт: userNum=%d npcId=%d кол-во=%d (30 б) -> %s"),
		InSelfUserNum, NpcId, Count, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 27.09.2026 [ОРИГИНАЛ]: у 30071/30072 эталон кладёт `Target_GetSelectedUserIdx(локальный)` = СВОЙ userNum (тот же миснейм, что у
//   6091); режим 1 шлёт −1. Сервер `RecvClient_TargetJump` (GameServer.c) байты +1..+2 не читает — поведение сервера то же.
bool UAsdaNetSubsystem::SendClientTargetJump6146(int32 Mode, int32 UserNum, const int32 (&Six)[6])
{
	FAsdaPacketWriter W(27);
	W.U8(0, (uint8)Mode);
	W.I16(1, (int32)(uint16)(int16)UserNum);
	for (int32 i = 0; i < 6; ++i) { W.I32(3 + 4 * i, Six[i]); }
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_TARGET_JUMP, TEXT("SendClientTargetJump6146"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_TARGET_JUMP, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6146 прыжок к цели: режим=%d userNum=%d [%d %d %d %d %d %d] (51 б) -> %s"),
		Mode, UserNum, Six[0], Six[1], Six[2], Six[3], Six[4], Six[5], bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════ ★★★ ЗВАНИЯ (Fame): ДВЕ ОТПРАВКИ ОКНА 18700 ═══════════════════════════════════════════
//   Эталон `netSendReq6067@0xE9B160` и `netSendReq6069@0xE9B810`: буфер собирается как
//   [0..7] обфусцированный QPC-секрет · [8..23] MD5(payload) · [24..] payload, и уходит
//   `CNetwork_Send(..., 34/32, OP, 1, 1, 1, 0, 0)`. Шестой аргумент = 1 ⇒ ПАДДИНГ включён
//   (`SendSecured(..., bPad = true)`). Это конверт `SendSecured`, НЕ `SendMd5Signed`.
//   ⚠ `payload[0]` эталона — `localPlayer->effectSlots[111].field_5B8`, то есть **userNum i16**
//   (то же поле, что сравнивается с `Target_GetSelectedUserIdx`), а `*(_DWORD*)&payload[1]` —
//   `CCharacter::userIdx`, то есть **charId i32**. Закон `charid-vs-usernum`.
bool UAsdaNetSubsystem::SendTitleApply6067(int32 TitleIdPrefix, int32 TitleIdSuffix)
{
	FAsdaPacketWriter W(10);
	W.I16(0, (int32)SelfUserNum);
	W.I32(2, SelectedCharId);
	// Пустой слот = −1 (закон «пустые слоты массивов = −1»): именно это шлёт кнопка «Убрать звание».
	W.I16(6, (int32)(uint16)(int16)TitleIdPrefix);
	W.I16(8, (int32)(uint16)(int16)TitleIdSuffix);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHANGE_ACTIVE_FAME, TEXT("SendTitleApply6067"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHANGE_ACTIVE_FAME, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6067 надеть звание: userNum=%d charId=%d приставка=%d окончание=%d (34 б) -> %s"),
		(int32)SelfUserNum, SelectedCharId, TitleIdPrefix, TitleIdSuffix, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendTitleDelete6069(int32 TitleId)
{
	FAsdaPacketWriter W(8);
	W.I16(0, (int32)SelfUserNum);
	W.I32(2, SelectedCharId);
	W.I16(6, (int32)(uint16)(int16)TitleId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_DELETE_FAME_INFO, TEXT("SendTitleDelete6069"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_DELETE_FAME_INFO, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6069 удалить звание: userNum=%d charId=%d titleId=%d (32 б) -> %s"),
		(int32)SelfUserNum, SelectedCharId, TitleId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendCampWarReq6746(int32 SelfCharId)
{
	FAsdaPacketWriter W(4);
	W.I32(0, SelfCharId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_ANTI_CHEAT_SEND_HEARTBEAT6746, TEXT("SendCampWarReq6746"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_ANTI_CHEAT_SEND_HEARTBEAT6746, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6746 запрос CampWar: charId=%d (28 б) -> %s"),
		SelfCharId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 06.10.2026 (волна 2-Г): 6744 — ставка налога города. Раскладка — в объявлении (security.c:953-1077).
bool UAsdaNetSubsystem::SendTownTaxChange6744(int32 SelfCharId, int32 Rate)
{
	FAsdaPacketWriter W(5);
	W.I32(0, SelfCharId);
	W.U8(4, Rate);   // `LOBYTE(heartbeatBody[1]) = sub_9331D5(pThis[558])` — младший байт позиции полосы
	if (AsdaPacketLayoutBroken(W, (uint16)OP_USER_GAME_TAX_CHANGE, TEXT("SendTownTaxChange6744"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_USER_GAME_TAX_CHANGE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6744 ставка налога города: charId=%d ставка=%d%% (29 б) -> %s"),
		SelfCharId, Rate, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 06.10.2026 (волна 2-Г): 6134 — свиток призыва. Раскладка — в объявлении (network.c:26296-26430).
bool UAsdaNetSubsystem::SendScrollSummon6134(int32 SelfCharId, int32 BagCell, const FString& TargetName)
{
	FAsdaPacketWriter W(26);   // `sub_D782D0`: +0 −1, +4 −1, +6 нули — затем поля вызова
	W.I32(0, SelfCharId);
	W.I16(4, BagCell);
	W.Cp1251(6, TargetName, 20);   // `strcpy` из поля 1558 (LimitText 20) — без терминатора при 20 байтах, как эталон
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_FUNCTION_ITEM_SUMMON, TEXT("SendScrollSummon6134"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_FUNCTION_ITEM_SUMMON, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6134 свиток призыва: charId=%d ячейка=%d цель «%s» (50 б) -> %s"),
		SelfCharId, BagCell, *TargetName, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 06.10.2026 (волна 2-Г): 5458 — свиток перемещения к игроку. Раскладка — в объявлении (network.c:7281-7410).
bool UAsdaNetSubsystem::SendScrollMeet5458(int32 BagCell, const FString& TargetName)
{
	FAsdaPacketWriter W(22);   // `sub_A0E470`: +0 −1, +2 нули — затем поля вызова
	W.I16(0, BagCell);
	W.Cp1251(2, TargetName, 20);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_FUNCTION_ITEM_MEET, TEXT("SendScrollMeet5458"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_FUNCTION_ITEM_MEET, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5458 свиток перемещения: ячейка=%d цель «%s» (46 б) -> %s"),
		BagCell, *TargetName, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 06.10.2026 (волна 2-Г): 6137 — ответ цели на призыв. Раскладка — в объявлении (network.c:26560-26700).
bool UAsdaNetSubsystem::SendSummonAnswer6137(const TArray<uint8>& Body6136, uint8 Answer)
{
	if (Body6136.Num() < 20)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→6137: тело 6136 %d б < 20 — ответ не собран"), Body6136.Num());
		return false;
	}
	FAsdaPacketWriter W(21);
	W.Bytes(0, Body6136.GetData(), 20);   // поля +0…+19 переносятся из 6136 побайтно (те же смещения)
	W.U8(20, Answer);                      // 1 — «да» (`urgentFlag == 1`), иначе 10
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_PREMIUM_SUMMON_RELAY_RES, TEXT("SendSummonAnswer6137"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_PREMIUM_SUMMON_RELAY_RES, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6137 ответ на призыв свитком: ответ=%u (45 б) -> %s"), (uint32)Answer, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 09.11.2026 (пачка 20 баг 8): 6138 — проверка имени предмета смены имени. Раскладка — в объявлении (`0xD33AB0`).
bool UAsdaNetSubsystem::SendCharNameCheck6138(int32 SelfCharId, const FString& NewName)
{
	FAsdaPacketWriter W(24);   // `sub_9295B8`: +0 −1, +4 нули — затем поля вызова
	W.I32(0, SelfCharId);
	W.Cp1251(4, NewName, 20);  // `strcpy(&payload[1], Source)` из поля 7904 (LimitText 20)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_CHAR_NAME_CHECK, TEXT("SendCharNameCheck6138"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_CHAR_NAME_CHECK, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6138 проверка имени: charId=%d имя «%s» (48 б) -> %s"), SelfCharId, *NewName, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 09.11.2026 (пачка 20 баг 8): 6140 — смена имени. Раскладка — в объявлении (`netSendTsMaskedReq6140@0xD33430`).
bool UAsdaNetSubsystem::SendCharNameChange6140(int32 SelfCharId, const FString& NewName, int32 Cell, int32 Container)
{
	FAsdaPacketWriter W(27);   // `sub_D33A50`: +0 −1, +4 нули, +24 −1, +26 0 — затем поля вызова
	W.I32(0, SelfCharId);
	W.Cp1251(4, NewName, 20);
	W.I16(24, Cell);           // `LOWORD(msg[6]) = a4` — ячейка (`sub_92BA1B`)
	W.U8(26, Container);       // `BYTE2(msg[6]) = a5` — контейнер (`sub_93D5AE`)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_CHAR_NAME_CHANGE, TEXT("SendCharNameChange6140"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_CHAR_NAME_CHANGE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6140 смена имени: charId=%d имя «%s» ячейка %d контейнер %d (51 б) -> %s"),
		SelfCharId, *NewName, Cell, Container, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★★ 16.10.2026: единая точка записи адреса игрового сервера из пакета (объявление и правило — AsdaNetSubsystem.h).
bool UAsdaNetSubsystem::ApplyGameServerFromPacket(const FString& Ip, int32 Port, const TCHAR* Source)
{
	if (Ip.IsEmpty() || Port <= 0 || Port >= 65536) { return false; }
	bool bSub = false;
	const FString Host = AsdaServerAddr::MapPacketHost(Ip, &bSub);
	if (bSub)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("Game-сервер из пакета %s: адрес %s непубличный — подключение к %s:%d (порт из пакета)"),
			Source, *Ip, *Host, Port);
	}
	if (Host != GameServerIP || Port != GameServerPort)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("Game-сервер ИЗ ПАКЕТА %s: %s:%d (было %s:%d)"), Source, *Host, Port, *GameServerIP, GameServerPort);
	}
	GameServerIP   = Host;
	GameServerPort = Port;
	return true;
}

// ★ 06.10.2026 (волна 2-Г): порт `netPortalChannelTransfer@0xF49EE0` — та же дорога, что у призыва 6013 (см. объявление).
void UAsdaNetSubsystem::StartScrollChannelTransfer(const FString& Ip, int32 Port, int32 MapId, int32 TileX, int32 TileZ, const TCHAR* Why)
{
	if (bDebugCaptureTransfer)
	{
		DebugTransferIp = Ip; DebugTransferPort = Port; DebugTransferMap = MapId; DebugTransferX = TileX; DebugTransferZ = TileZ;
		UE_LOG(LogAsdaNet, Warning, TEXT("[свиток] переход ПЕРЕХВАЧЕН самотестом (%s): сервер %s:%d карта=%d прибытие=(%d,%d)"),
			Why, *Ip, Port, MapId, TileX, TileZ);
		return;
	}
	FlushHotbarBeforeTransfer(Why);
	StopTravelAndMovementForTransfer(Why);
	bTransferInProgress = true;
	ApplyGameServerFromPacket(Ip, Port, Why);   // `g_szChannelServerIP`, `g_wPortalServerPort` (★ 16.10.2026: непубличный → домен)
	EnterMapId  = MapId;                 // `g_nPortalTargetMapId` / `g_nPendingSpawnMapId`
	SelfTileX   = TileX; SelfTileZ = TileZ;
	PortalTileX = TileX; PortalTileZ = TileZ;   // `g_wPortalSpawnX/Y`
	bPortalPending     = true;           // после загрузки — портальный логон 5058, как у любого портала
	bInGameConnected   = false;
	bPendingEnterWorld = true;           // `g_nInputControlState = 11` — реконнект на следующем тике (не из колбэка)
	if (Conn) { Conn->RequestStopDrain(); }
	UE_LOG(LogAsdaNet, Warning, TEXT("[свиток] %s: переход на сервер %s:%d, карта=%d, прибытие=(%d,%d) (канал не меняется — как эталон)"),
		Why, *GameServerIP, GameServerPort, MapId, TileX, TileZ);
}

// ★★★ 6723 — ВСТУПИТЬ В ОСАДУ (CampWar), клик по NPC на карте mapId 19.
//   Эталон `netSendCampWarjoin@0xCA5750` (network.c:15916): три memcpy в буфер 32 б —
//   [0..7] `buildAntiCheatToken()`, [8..23] `Net_Md5Digest16(&g_NetMd5Salt, blk, 8)`, [24..31] blk.
//   Ровно это и делает `SendSecured` (конверт 24 + payload). Состав blk — см. объявление в .h.
bool UAsdaNetSubsystem::SendCampWarJoin6723(int32 SelfCharId, int32 FactionId, int32 NpcEntityId)
{
	FAsdaPacketWriter W(8);
	W.I32(0, SelfCharId);
	W.I16(4, (int32)(uint16)FactionId);
	// ★ В пакет уходит МЛАДШЕЕ СЛОВО серверного id сущности (`LOWORD(a2)` эталона) — не весь i32.
	W.I16(6, (int32)(uint16)NpcEntityId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_USER_GAME_REGIST_CANCEL_6723, TEXT("SendCampWarJoin6723"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_USER_GAME_REGIST_CANCEL_6723, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6723 вступление в осаду: charId=%d фракция=%d id NPC=%d (низ. слово %u, 32 б) -> %s"),
		SelfCharId, FactionId, NpcEntityId, (uint32)(uint16)NpcEntityId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★★★ 6749 — код меню диалога 30078 (CampWar). Обоснование состава — в объявлении в .h.
//   Эталон `netSendReqSelfId_op6749@0xFEA180`: `msg[0] = *(i32*)(g_LocalPlayer + 1052);`
//   `LOWORD(msg[1]) = word_2BFF13C;` → payload 6 б, дальше стандартный конверт 24 + payload.
bool UAsdaNetSubsystem::SendCampWarSelfId6749(int32 SelfCharId, int32 FactionId)
{
	FAsdaPacketWriter W(6);
	W.I32(0, SelfCharId);
	W.I16(4, (int32)(uint16)FactionId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_NET_SEND_REQ_SELF_ID_6749, TEXT("SendCampWarSelfId6749"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_NET_SEND_REQ_SELF_ID_6749, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6749 CampWar (код 30078): charId=%d фракция=%d (30 б) -> %s"),
		SelfCharId, FactionId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 30.10.2026 (выбор армии) — 6700 и 6702. Раскладка и обоснование — в объявлениях в .h.
//   Эталон `Clan_Send_Op6700@0xCA21A0`: `payload[0] = this[2228]` (charId), `LOBYTE(payload[1]) = a2` → 5 б, конверт 24 + 5 = 29.
bool UAsdaNetSubsystem::SendCampWarMainInfo6700(int32 SelfCharId, uint8 Mode)
{
	FAsdaPacketWriter W(5);
	W.I32(0, SelfCharId);
	W.U8(4, Mode);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_USER_GAME_GET_MAIN_INFO, TEXT("SendCampWarMainInfo6700"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_USER_GAME_GET_MAIN_INFO, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6700 сведения войны армий: charId=%d режим=%d (29 б) -> %s"),
		SelfCharId, (int32)Mode, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 30.10.2026 (CampWar целиком) — общий отправитель «Битвы армий». Белый список: опкод → длина тела эталонного отправителя
//   (`CNetwork_Send(…, 24 + тело, op, …)`). Тело собирает `UAsdaCampWarSubsystem`; здесь — только сверка длины и конверт.
namespace
{
	struct FCampWarSendOp { uint16 Op; int32 Len; };
	static const FCampWarSendOp kCampWarSendOps[] =
	{
		{ 6704, 11 },   // `Clan_Send_Create@0xCA2530` (имя врёт) — сброс армии предметом категории 78
		{ 6706,  8 },   // `Clan_Send_Op6706@0xCA26A0` — «Участвовать»
		{ 6708,  8 },   // `Clan_Send_Op6708@0xCA28F0` — выйти из очереди
		{ 6712, 11 },   // `Clan_Send_SetRank@0xCA2B20` (имя врёт) — «Войти в битву»
		{ 6714,  9 },   // `Clan_Send_6714@0xCA2D70` — «Вход на поле боя»
		// ★ 30.10.2026 — боевая фаза (`UAsdaCampWarBattleSubsystem`, BATTLE_SPEC §5):
		{ 6717,  6 },   // `Clan_Send_ReqMemberList@0xCA33B0` — 49500 «Рейд» / 6721 при открытом 49399 (конверт 30)
		{ 6731,  8 },   // `Clan_Send_Leave@0xCA37B0` (имя врёт) — «Выйти из боя» 17520 → 10047 / 49880 / 49881 (конверт 32)
		{ 6734, 14 },   // `Clan_Send_KickMember@0xCA39B0` (имя врёт) — 49420 «Назначить капитана» (конверт 38)
		{ 6186, 10 },   // `netSendListSelectReq@0xCA88A0` (имя врёт) — «Да» окна 87 у 49430 «Исключить» (конверт 34, секрет — маска таймштампа)
		// ★ 31.10.2026 — чат армии: `Clan_Send_SetNotice@0xCA3010` шлёт 228, но сервер (S:41990) читает и хеширует 108 — СЕРВЕРНАЯ длина
		//   (P55: кадр 142…161). Разбор — `UAsdaCampWarBattleSubsystem::SendArmyChat6718`.
		{ 6718, 108 },
	};
}

bool UAsdaNetSubsystem::SendCampWar(uint16 Opcode, const uint8* Payload, int32 Len, const TCHAR* What)
{
	const FCampWarSendOp* Row = nullptr;
	for (const FCampWarSendOp& R : kCampWarSendOps) { if (R.Op == Opcode) { Row = &R; break; } }
	if (!Row || !Payload || Len != Row->Len)
	{
		UE_LOG(LogAsdaNet, Error, TEXT("→%u CampWar (%s): НЕ отправлен — опкод не из белого списка или длина тела %d ≠ эталонной %d"),
			(uint32)Opcode, What ? What : TEXT("?"), Len, Row ? Row->Len : -1);
		return false;
	}
	const bool bOk = SendSecured(Opcode, Payload, Len);
	UE_LOG(LogAsdaNet, Log, TEXT("→%u CampWar %s: тело %d б (%d б) -> %s"), (uint32)Opcode, What ? What : TEXT("?"), Len, 24 + Len,
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

//   Эталон `Clan_Send_Op6702@0xCA2360`: `payload[0] = this[2228]` (charId), `LOWORD(payload[1]) = a2` → 6 б, конверт 24 + 6 = 30.
bool UAsdaNetSubsystem::SendCampWarSelectSquad6702(int32 SelfCharId, int32 Squad)
{
	FAsdaPacketWriter W(6);
	W.I32(0, SelfCharId);
	W.I16(4, (int32)(uint16)Squad);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_USER_GAME_SELECT_SQUAD, TEXT("SendCampWarSelectSquad6702"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_USER_GAME_SELECT_SQUAD, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6702 выбор армии: charId=%d армия=%d (30 б) -> %s"),
		SelfCharId, Squad, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═════════════════════════ ПИТОМЦЫ: ВОСЕМЬ ЗАПРОСОВ C→S ═══════════════════════════════════════
// Раскладка и длины каждого — в объявлениях (`AsdaNetSubsystem.h`), доказательства там же.
// Конверт у всех — `SendSecured` (вид «с секретом»), паддинг по умолчанию.

bool UAsdaNetSubsystem::SendPetCall6100(int32 SelfCharId, int32 PetUniqueId)
{
	FAsdaPacketWriter W(8);
	W.I32(0, SelfCharId);      // +0 i32 charId — сверяется с CUser+372 (GameServer.c:34440)
	W.I32(4, PetUniqueId);     // +4 i32 UniqueIndex питомца — поиск среди 30 слотов
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PET_CALL_REQ, TEXT("SendPetCall6100"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_PET_CALL_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6100 питомец призыв/отзыв: charId=%d pet=%d (тело 8 б) -> %s"),
		SelfCharId, PetUniqueId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPetIncubate6102(int32 SelfCharId, int32 Bag1, int32 Slot1,
                                            int32 Bag2, int32 Slot2, int32 Bag3, int32 Slot3)
{
	FAsdaPacketWriter W(13);
	W.I32(0, SelfCharId);
	W.U8(4, Bag1);   W.I16(5, Slot1);    // предмет 1 — КАТАЛИЗАТОР (subType 107)
	W.U8(7, Bag2);   W.I16(8, Slot2);    // предмет 2 — ЯЙЦО (subType 109)
	W.U8(10, Bag3);  W.I16(11, Slot3);   // предмет 3 — бустер (−1 = нет)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PET_INCUBATE_REQ, TEXT("SendPetIncubate6102"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_PET_INCUBATE_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6102 инкубатор: charId=%d катализатор=(%d,%d) яйцо=(%d,%d) бустер=(%d,%d) (тело 13 б) -> %s"),
		SelfCharId, Bag1, Slot1, Bag2, Slot2, Bag3, Slot3, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPetFeed6108(int32 SelfCharId, int32 PetUniqueId, int32 ItemBag, int32 ItemSlot)
{
	FAsdaPacketWriter W(11);
	W.I32(0, SelfCharId);
	W.I32(4, PetUniqueId);
	W.U8(8, ItemBag);
	W.I16(9, ItemSlot);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PET_FEED_REQ, TEXT("SendPetFeed6108"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_PET_FEED_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6108 кормление: charId=%d pet=%d предмет=(%d,%d) (тело 11 б) -> %s"),
		SelfCharId, PetUniqueId, ItemBag, ItemSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPetRelease6110(int32 SelfCharId, int32 PetUniqueId)
{
	FAsdaPacketWriter W(8);
	W.I32(0, SelfCharId);
	W.I32(4, PetUniqueId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PET_RELEASE_REQ, TEXT("SendPetRelease6110"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_PET_RELEASE_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6110 выброс питомца: charId=%d pet=%d (тело 8 б) -> %s"),
		SelfCharId, PetUniqueId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPetUnion6112(int32 SelfCharId, int32 UnionIndex, int32 Mat0, int32 Mat1, int32 Mat2)
{
	FAsdaPacketWriter W(18);
	W.I32(0, SelfCharId);
	W.I16(4, UnionIndex);   // номер рецепта Petdetailevotable; отказ 4, если вне диапазона
	W.I32(6, Mat0);
	W.I32(10, Mat1);
	W.I32(14, Mat2);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PET_UNION_REQ, TEXT("SendPetUnion6112"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_PET_UNION_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6112 эволюция: charId=%d рецепт=%d материалы=%d/%d/%d (тело 18 б) -> %s"),
		SelfCharId, UnionIndex, Mat0, Mat1, Mat2, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPetRename6118(int32 SelfCharId, int32 PetUniqueId, const FString& NewName)
{
	FAsdaPacketWriter W(24);
	W.I32(0, SelfCharId);
	W.I32(4, PetUniqueId);
	// +8 char[16] БЕЗ терминатора: сервер сравнивает ровно strlen(текущего имени) байт с дефолтным
	//   именем породы (GameServer.c:36266-36270). Хвост уже нулевой — тело создано нулями.
	W.Cp1251(8, NewName, 16);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PET_NAME_CHANGE_REQ, TEXT("SendPetRename6118"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_PET_NAME_CHANGE_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6118 переименование: charId=%d pet=%d имя=[%s] (тело 24 б) -> %s"),
		SelfCharId, PetUniqueId, *NewName, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPetSlotUp6121(int32 SelfCharId, int32 ItemBag, int32 ItemSlot)
{
	FAsdaPacketWriter W(7);
	W.I32(0, SelfCharId);
	W.U8(4, ItemBag);
	W.I16(5, ItemSlot);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PET_SLOT_UP_REQ, TEXT("SendPetSlotUp6121"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_PET_SLOT_UP_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6121 расширение ячеек: charId=%d предмет=(%d,%d) (тело 7 б) -> %s"),
		SelfCharId, ItemBag, ItemSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPetLimitBreak6148(int32 PetUniqueId, int32 ItemBag, int32 ItemSlot, int32 PremiumSlot)
{
	// ★ У ЭТОГО ПАКЕТА НЕТ ПОЛЯ charId — тело начинается сразу с номера питомца
	//   (GameServer.c:36632: `memcpy(v25, pkt + 33, 9)`, дальше `v25[0]` = pet, `v25[1]` = слот и сумка).
	FAsdaPacketWriter W(9);
	W.I32(0, PetUniqueId);
	W.I16(4, ItemSlot);      // слот зелья (subType 207)
	W.U8(6, ItemBag);
	W.I16(7, PremiumSlot);   // слот защиты (subType 209), −1 = без защиты
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PET_LIMITBREAK_REQ, TEXT("SendPetLimitBreak6148"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_PET_LIMITBREAK_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6148 лимит-брейк: pet=%d зелье=(%d,%d) защита=%d (тело 9 б) -> %s"),
		PetUniqueId, ItemBag, ItemSlot, PremiumSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendQuestStepAccept5043(int32 NpcId, int32 GiveSlotIndex)
{
	FAsdaPacketWriter W(4);
	W.I16(0, (int32)(uint16)NpcId);
	W.I16(2, (int32)(uint16)GiveSlotIndex);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_START_QUEST_BOOK_PROCESS, TEXT("SendQuestStepAccept5043"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SEND_START_QUEST_BOOK_PROCESS, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5043 принять квест: npcId=%d слот выдачи=%d (28 б) -> %s"),
		NpcId, GiveSlotIndex, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★★★ ИСПРАВЛЕНО 2026-08-21 (`Reports/quest_accept_fix.md` §5): сдача шла опкодом 5043 с
//   семибайтовой нагрузкой. У сервера обработчик 5043 (`Send_StartQuestBookProcess@0x45C960`)
//   считает MD5 РОВНО ПО 4 БАЙТАМ и читает 4 байта тела — наш MD5 по семи байтам не сходился,
//   `sub_523F00` возвращал 0, и сервер МОЛЧА выбрасывал пакет: ответа не приходило вообще.
//   Правильный канал сдачи — 5041 `Send_RewardQuestProcess@0x45CE50`: маска 8 б, MD5 ПО 7 БАЙТАМ,
//   тело 7 б по смещению +33, ретрансляция на NpcServer опкодом 7103. Ответ — 5042 (501 б).
//   Тело 7 б: u16 npcId, u16 номер слота в таблице «ПРИНИМАЕТ» этого NPC (0…9),
//             u8 индекс выбранной награды/цели, u16 номер слота журнала.
bool UAsdaNetSubsystem::SendQuestTurnIn5041(int32 NpcId, int32 TakeSlotIndex, int32 ObjIndex, int32 QuestSlotNo)
{
	FAsdaPacketWriter W(7);
	W.I16(0, (int32)(uint16)NpcId);
	W.I16(2, (int32)(uint16)TakeSlotIndex);
	W.U8(4, (uint8)ObjIndex);
	W.I16(5, (int32)(uint16)(int16)QuestSlotNo);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_QUEST_REWARD_PROCESS_REQ, TEXT("SendQuestTurnIn5041"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_QUEST_REWARD_PROCESS_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5041 сдать шаг квеста: npcId=%d слот приёма=%d награда=%d слот журнала=%d (31 б) -> %s"),
		NpcId, TakeSlotIndex, ObjIndex, QuestSlotNo, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЖУРНАЛ ЗАДАНИЙ: отказ от задания (5006 под-тип 4) и перенос между ячейками (5000, 4→4)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ Обоснование состава — в объявлениях в .h и в `Reports/quest_window_re.md` §4.1/§4.2.
//   Оба пакета используют ОБЩИЙ конверт `SendSecured` (8 б токена + 16 б MD5 + payload) — тот же,
//   что у предметных 5006/5000. Отдельных «квестовых» конвертов у оригинала нет.
bool UAsdaNetSubsystem::SendQuestAbandon5006(int32 QuestSlotIndex)
{
	if (QuestSlotIndex < 0 || QuestSlotIndex > 11)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→5006 отказ от задания: ячейка %d вне 0…11 — не шлём"), QuestSlotIndex);
		return false;
	}
	FAsdaPacketWriter W(11);
	W.I16(0, (int32)(uint16)QuestSlotIndex);
	W.U8(4, 4);                                  // под-тип: ЯЧЕЙКА ЖУРНАЛА КВЕСТОВ
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_DUMP, TEXT("SendQuestAbandon5006"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_DUMP, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5006 отказ от задания: ячейка журнала=%d (35 б) -> %s"),
		QuestSlotIndex, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendQuestSlotMove5000(int32 SrcSlotIndex, int32 DstSlotIndex)
{
	if (SrcSlotIndex < 0 || SrcSlotIndex > 11 || DstSlotIndex < 0 || DstSlotIndex > 11)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("→5000 перенос задания: ячейки %d→%d вне 0…11 (сервер отказал бы 5001) — не шлём"),
			SrcSlotIndex, DstSlotIndex);
		return false;
	}
	FAsdaPacketWriter W(22);
	W.I16(0, (int32)(uint16)SrcSlotIndex);
	W.U8(4, 4);                                 // тип источника  = ячейка журнала
	W.I16(11, (int32)(uint16)DstSlotIndex);
	W.U8(15, 4);                                 // тип приёмника  = ячейка журнала
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_MOVE, TEXT("SendQuestSlotMove5000"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_MOVE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5000 перенос задания: ячейка %d → %d (46 б) -> %s"),
		SrcSlotIndex, DstSlotIndex, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ══════════════ ВОЙНА ФРАКЦИЙ ЗА ГОРОДА: разбор тела 6743 (одна точка на приём и консоль) ══════
// Строка чата 111920 по каждому занятому городу при приёме 6743 (эталон; 0 = прежний порт без строки).
static ASDA_DEV_CVAR(int32, CVarWarTaxChatLine,
	TEXT("asda.War.TaxChatLine"), 1,
	TEXT("1 = при приёме 6743 в чат по каждому занятому городу строка 111920 «Ставка налога города …: N%» (оригинал); "
	     "0 = без строки (прежний порт)."),
	ECVF_Default);

void UAsdaNetSubsystem::ApplyCityOwnerBody(const TArray<uint8>& Body274)
{
	if (Body274.Num() < 274) { return; }
	const uint8* P = Body274.GetData();
	// Имена в протоколе — CP1251; декодер один на порт (`FAsdaLoginData::DecodeCp1251`).
	auto Str = [](const uint8* At, int32 MaxLen) -> FString
	{
		return FAsdaLoginData::DecodeCp1251(At, MaxLen).TrimStartAndEnd();
	};
	for (int32 i = 0; i < 3; ++i)
	{
		const uint8* R = P + 1 + 91 * i;
		FAsdaCityOwner& C = CityOwner[i];
		C.OwnerFaction = AsdaBytes::RdI16(R + 0);
		// Эталон: `if (state == -1) state = 3;` — «−1» и «3» одинаково значат «город не занят».
		if (C.OwnerFaction == -1) { C.OwnerFaction = 3; }
		C.TaxPercent = (int32)R[2];
		C.ClanId     = AsdaBytes::RdI16(R + 6);
		C.ClanName   = Str(R + 8, 17);
		// ★ 07.10.2026 (волна 2-Г, ревизия М-5): эталон подставляет «.» вместо пустых имён клана (+8) и главы (+30)
		//   (`uniconWar_UpdateFactionStateUI`, ui_core.c:3924-3927) — эти строки печатают 111909/111910/111900.
		if (C.ClanName.IsEmpty()) { C.ClanName = TEXT("."); }
		C.MasterCharId  = AsdaBytes::RdI32(R + 25);   // ★ волна 2-Г: глава клана-хозяина (charId)
		C.MasterCharIdx = (int32)R[29];               // ★ волна 2-Г: индекс персонажа главы (10..12)
		C.MasterName = Str(R + 30, 20);
		if (C.MasterName.IsEmpty()) { C.MasterName = TEXT("."); }
		// ★ Эмблема клана-победителя: флаг +50 и 40 байт дизайна +51..+90 (см. объявление в .h).
		C.bFlagOn = (R[50] > 0);
		C.FlagDesign40.Reset();
		if (C.bFlagOn)
		{
			C.FlagDesign40.Append(R + 51, 40);
		}
	}
	bCityOwnerReceived = true;
	// ★ 08.11.2026 (пачка 17, баг 1) [ОРИГИНАЛ]: `uniconWar_UpdateFactionStateUI@0x9C8B90` в том же цикле по трём городам для
	//   КАЖДОГО занятого (состояние ≠ 3) печатает в чат 111920 «Ставка налога города %s: %d%%.» (имя — нотис 603/600/707,
	//   цвет −11514, 10 000 мс, `CChat_AddTokenizedLines`). Порт эту ветку не переносил — игрок не видел в чате, чей город.
	if (CVarWarTaxChatLine.GetValueOnGameThread() != 0)
	{
		static const int32 kCityLoc[3] = { 603, 600, 707 };   // порядок слотов пакета: Альпия, Силарис, Пламения
		const UGameInstance* GI = GetGameInstance();
		for (int32 i = 0; i < 3; ++i)
		{
			if (CityOwner[i].OwnerFaction == 3) { continue; }
			const FString Line = AsdaUi::FormatPrintfArgs(AsdaNotice::LocTagged(GI, 111920),
				{ AsdaNotice::LocTagged(GI, kCityLoc[i]), FString::FromInt(CityOwner[i].TaxPercent) });
			AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Line, AsdaNoticeColor::Gold, 10000.f);
		}
	}
	UE_LOG(LogAsdaNet, Log,
		TEXT("6743 принадлежность городов: Альпия=фракция %d налог %d%% (клан «%s»), ")
		TEXT("Силарис=%d %d%% («%s»), Пламения=%d %d%% («%s»)"),
		CityOwner[0].OwnerFaction, CityOwner[0].TaxPercent, *CityOwner[0].ClanName,
		CityOwner[1].OwnerFaction, CityOwner[1].TaxPercent, *CityOwner[1].ClanName,
		CityOwner[2].OwnerFaction, CityOwner[2].TaxPercent, *CityOwner[2].ClanName);
	OnCityOwnerInfo.Broadcast();
}

void UAsdaNetSubsystem::SimulateCityOwnerInfo(const TArray<int32>& Factions3, const TArray<int32>& TaxPercents3)
{
	TArray<uint8> Body;
	Body.SetNumZeroed(274);
	Body[0] = 1;
	for (int32 i = 0; i < 3; ++i)
	{
		uint8* R = Body.GetData() + 1 + 91 * i;
		AsdaBytes::WrI16(R + 0, Factions3.IsValidIndex(i) ? Factions3[i] : 3);
		R[2] = (uint8)FMath::Clamp(TaxPercents3.IsValidIndex(i) ? TaxPercents3[i] : 0, 0, 255);
		AsdaBytes::WrI16(R + 6, -1);
		// Дизайн флага в офлайн-подаче не задаётся (флаг +50 остаётся нулём) — эмблемы не будет,
		//   как и у города без клана. Для проверки эмблемы есть `asda.War.FakeFlag`.
		R[50] = 0;
	}
	ApplyCityOwnerBody(Body);
}

void UAsdaNetSubsystem::SimulateCityClanFlag(int32 Slot, int32 BorderIdx, int32 BorderColor,
                                             int32 BgColor, int32 SignIdx, int32 SignColor)
{
	if (Slot < 0 || Slot >= 3) { return; }
	FAsdaCityOwner& C = CityOwner[Slot];
	C.bFlagOn = true;
	C.FlagDesign40.SetNumZeroed(40);
	uint8* D = C.FlagDesign40.GetData();
	// Раскладка 1:1 с проводом: BorderIdx@0, BorderColor@8, BgColor@16, SignIdx@24, SignColor@32.
	AsdaBytes::WrI32(D + 0,  BorderIdx);
	AsdaBytes::WrI32(D + 8,  BorderColor);
	AsdaBytes::WrI32(D + 16, BgColor);
	AsdaBytes::WrI32(D + 24, SignIdx);
	AsdaBytes::WrI32(D + 32, SignColor);
	bCityOwnerReceived = true;
	UE_LOG(LogAsdaNet, Log,
		TEXT("[война] дизайн флага клана города %d задан вручную: рамка %d/%08X, фон %08X, знак %d/%08X"),
		Slot, BorderIdx, (uint32)BorderColor, (uint32)BgColor, SignIdx, (uint32)SignColor);
	OnCityOwnerInfo.Broadcast();
}

// ═══════════════════ ЗНАК ЗОДИАКА: 6586 (см. объявление в .h) ═════════════════════════════════
//   Эталон `netSendMaskedPacket_op6586@0xECC0C0` собирает те же 24 байта шапки (8 секрет по
//   таблице из 30 битовых позиций + MD5 по payload), что и общий `SendSecured` — своего
//   сборщика не заводим (закон `port-shared-points`).
bool UAsdaNetSubsystem::SendConstellationSelect6586(uint8 ConstellationId)
{
	FAsdaPacketWriter W(5);
	// @0 — эталон кладёт сюда `->userIdx`; сервер поле не читает (`Recv_UserStarPointUpdate`
	//   берёт charId из своей же `CUser`). Шлём 1:1, тем же значением, что и 5-байтовый 4050.
	//   ★ 27.09.2026 [ОРИГИНАЛ]: `CCharacter::userIdx` = `char+1052` = charId (миснейм IDA), прежде здесь уходил userNum.
	W.I32(0, SelectedCharId);
	W.U8(4, ConstellationId);                       // 1…12; 0 сервер принял бы, но окно его не шлёт
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_STAR_POINT_UPDATE, TEXT("SendConstellationSelect6586"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_STAR_POINT_UPDATE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6586 выбор знака зодиака: знак=%d (29 б) -> %s"),
		(int32)ConstellationId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ КОНСОЛЬ: ВОЙНА ФРАКЦИЙ ЗА ГОРОДА (проверка без сервера) ═══════════════════════════
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarFake(
	TEXT("asda.War.Fake"),
	TEXT("asda.War.Fake <фАльпия> <фСиларис> <фПламения> [%Альпия] [%Силарис] [%Пламения] — ")
	TEXT("сымитировать приход 6743. Фракция: 0 свет(синий), 1 тьма(красный), 2 хаос(зелёный), 3 не занят"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { UE_LOG(LogAsdaNet, Warning, TEXT("[война] NetSubsystem недоступен")); return; }
		TArray<int32> F, P;
		for (int32 i = 0; i < 3; ++i) { F.Add(A.IsValidIndex(i) ? FCString::Atoi(*A[i]) : 3); }
		for (int32 i = 0; i < 3; ++i) { P.Add(A.IsValidIndex(3 + i) ? FCString::Atoi(*A[3 + i]) : 10); }
		Net->SimulateCityOwnerInfo(F, P);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarDump(
	TEXT("asda.War.Dump"),
	TEXT("asda.War.Dump — принадлежность и ставки трёх городов (последний принятый 6743)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { UE_LOG(LogAsdaNet, Warning, TEXT("[война] NetSubsystem недоступен")); return; }
		static const TCHAR* kCity[3] = { TEXT("Альпия (карта 3)"), TEXT("Силарис (карта 0)"), TEXT("Пламения (карта 7)") };
		static const TCHAR* kFac[4]  = { TEXT("Свет"), TEXT("Тьма"), TEXT("Хаос"), TEXT("не занят") };
		FString S = FString::Printf(TEXT("=== Война за города (6743 %s) ===\n"),
			Net->HasCityOwnerInfo() ? TEXT("получен") : TEXT("НЕ приходил"));
		for (int32 i = 0; i < 3; ++i)
		{
			const int32 F = Net->GetCityOwnerFaction(i);
			S += FString::Printf(TEXT("  слот %d %-20s: хозяин %d (%s), ставка %d%%, клан «%s», мастер «%s»\n"),
				i, kCity[i], F, (F >= 0 && F <= 3) ? kFac[F] : TEXT("?"),
				Net->GetCityTaxPercent(i), *Net->CityOwner[i].ClanName, *Net->CityOwner[i].MasterName);
		}
		UE_LOG(LogAsdaNet, Warning, TEXT("%s"), *S);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GWarFakeFlag(
	TEXT("asda.War.FakeFlag"),
	TEXT("asda.War.FakeFlag <слот 0..2> [рамка] [цветРамки] [цветФона] [знак] [цветЗнака] — задать "
	     "дизайн флага клана-хозяина города (те же 40 байт, что приходят в хвосте записи 6743). "
	     "Цвета ARGB в hex/десятично; слоты: 0 Альпия, 1 Силарис, 2 Пламения"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { UE_LOG(LogAsdaNet, Warning, TEXT("[война] NetSubsystem недоступен")); return; }
		auto Num = [&A](int32 i, int32 Def) -> int32
		{
			if (!A.IsValidIndex(i)) { return Def; }
			return A[i].StartsWith(TEXT("0x")) ? (int32)FParse::HexNumber(*A[i].Mid(2)) : FCString::Atoi(*A[i]);
		};
		Net->SimulateCityClanFlag(Num(0, 0), Num(1, 0), Num(2, (int32)0xFFFFFFFF),
		                          Num(3, (int32)0xFF3060C0), Num(4, 0), Num(5, (int32)0xFFFFFFFF));
	}));
#endif // ASDA_DEV_CONSOLE
