// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_RecvSocial.cpp — часть `AsdaNetSubsystem.cpp`: приёмник пакетов: соулмейт/клан-членство; друзья/пати/клан (4151…); клан/пати/друзья/объявления/соулмейт-поиск (4135…).
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
#include "World/AsdaLoadManagerSubsystem.h"  // 6004 успех → BeginMapLoad (Load Manager)
#include "Text/AsdaTextSubsystem.h"          // loc-строки форматов чата (20030/1187/1109/8200/…)
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

// ★ 01.11.2026 (clan-window) [ОРИГИНАЛ]: своё звание в клане из своей карточки 4135 (разбор — у приёма 4135).
static ASDA_DEV_CVAR(int32, CVarClanSelfGradeFrom4135,
	TEXT("asda.Clan.SelfGradeFrom4135"), 1,
	TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = своё звание в клане берётся из своей карточки 4135 (как эталон при каждом входе); ")
	TEXT("0 = прежнее (звание только из 4105/6523 и списка 4125 — после перезахода глава «без звания»)."),
	ECVF_Default);

// ★ R2 (10.11.2026): приёмник области «Social» — соулмейт/клан-членство; друзья/пати/клан (4151…); клан/пати/друзья/объявления/соулмейт-поиск (4135…).
//   Группы `case` перенесены из `UAsdaNetSubsystem::HandlePacket` ДОСЛОВНО и в исходном порядке; `bHandled` = false только
//   в служебной ветке `default:` в конце — опкод не из этой области, диспетчер идёт дальше.
void UAsdaNetSubsystem::HandlePacket_Social(const Asda::ParsedPacket& Pkt, bool& bHandled)
{
	bHandled = true;
	switch ((NetOpcode)Pkt.Opcode)
	{
	case OP_PARTY_MARK_HELPER2: // 6152 0x1808 — соулмейт-стейт СОСЕДА (AoI-бандл после 4005; §J)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 12, (int32)Pkt.Opcode))
		{
			auto U32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
			const int32 CharId    = U32(0);
			const int32 SoulIdx   = U32(4);
			const int32 PartnerId = U32(8);
			// ★★★ Тело @12 — SoulState (`@State` из `UPLoginGame`: 0 обычный / 1 душа / 2 ОДЕРЖИМ).
			//   Эталон кладёт его в `CCharacter+0x8C1` (`PartyMark_Helper2`: `pChar->gap_8BE[3] =
			//   *(BYTE*)(a2 + 12)`) — и делает это для ЛЮБОГО найденного персонажа, не только своего.
			//   Сервер набивает байт ЧУЖИМ блоком: `sub_422A90` берёт его из 101-байтовой структуры
			//   со смещения +32, а та собрана `memcpy(&v139[6], (a2 + 99001), 0x5F)` из CUser СОСЕДА
			//   (GameServer.c:11407-11408, 15201). Доказательство — possess_broadcast_soulstate.md §1.1.
			//   ★ ДЕФЕКТ ПОРТА (исправляется здесь): байт читался ТОЛЬКО при CharId == SelectedCharId,
			//   для соседей терялся ⇒ метка/цвет одержимости у чужих не зажигались никогда.
			const int32 Soul = (B.size() >= 13) ? (int32)B[12] : 0;
			UE_LOG(LogAsdaNet, Log, TEXT("6152 соулмейт: charId=%d soulIdx=%d партнёр=%d soulState=%d%s"),
				CharId, SoulIdx, PartnerId, Soul, (Soul == 2) ? TEXT(" (ОДЕРЖИМ)") : TEXT(""));
			if (B.size() >= 13)
			{
				// Ненулевой признак запрещает клановые операции нотисом 12435 (дизасм гейта — 0xBC2D3B);
				//   гейт смотрит именно на СВОЙ персонаж, поэтому зеркало остаётся своим.
				if (SelectedCharId != 0 && CharId == SelectedCharId) { SelfSoulSilhouette = B[12]; }
				SoulStateByCharId.Add(CharId, Soul);
				OnPeerSoulStateByCharId.Broadcast(CharId, Soul);
			}
			OnSoulmatePeer.Broadcast(CharId, SoulIdx, PartnerId);
		}
		break;
	}
	case OP_SOULMATE_BOND: // 5203 0x1453 — Recv_SoulmateBond: создание пары (церемония у обоих)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 9, (int32)Pkt.Opcode))
		{
			const int32 Sub  = B[0];
			const int32 Pair = AsdaBytes::RdI32(&B[1]);
			const int32 Init = AsdaBytes::RdI32(&B[5]);
			const FString Name = FAsdaLoginData::DecodeCp1251(B.data() + 9, (int32)B.size() - 9);
			UE_LOG(LogAsdaNet, Log, TEXT("5203 бонд: sub=%d pair=%d init=%d партнёр='%s'"), Sub, Pair, Init, *Name);
			OnSoulmateBond.Broadcast(Sub, Pair, Init, Name);
		}
		break;
	}
	case OP_SOULMATE_WND_HANDLE_RESPONSE_PACKET: // 5207 0x1457 — разрыв пары
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 1, (int32)Pkt.Opcode))
		{
			const int32 Sub = B[0];
			const FString Name = (B.size() > 5) ? FAsdaLoginData::DecodeCp1251(B.data() + 5, (int32)B.size() - 5) : FString();
			UE_LOG(LogAsdaNet, Log, TEXT("5207 разрыв: sub=%d имя='%s'"), Sub, *Name);
			OnSoulmateBreak.Broadcast(Sub, Name);
		}
		break;
	}
	case OP_PARTY_MINIMAP_MARK: // 5220 0x1464 — соулмейт-стейт чаров (мульти-записи 101б)
	{
		const std::vector<uint8>& B = Pkt.Body;
		auto I32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
		auto U16 = [&B](int32 O) { return (int32)AsdaBytes::RdU16(&B[O]); };
		auto I16 = [&B](int32 O) { return AsdaBytes::RdI16(&B[O]); };
		for (int32 Off = 0; Off + 101 <= (int32)B.size(); Off += 101)
		{
			const int32 CharId   = I32(Off + 0);
			const int32 UserIdx  = U16(Off + 4);
			const int32 SoulIdx  = I32(Off + 6);        // блок 0x5F байт 0 (char+2215)
			const int32 PartnerU = I16(Off + 6 + 17);   // байт 17 блока = userIdx партнёра
			// ★★★ Байт +32 записи = байт 26 блока = `CUser+99027` = SoulState (0/1/2). Эталон копирует
			//   ВЕСЬ блок и своему (`memcpy(localChar+2215, Src+6, 0x5F)`), и ЧУЖОМУ персонажу
			//   (`FindByUserIdx(*((__int16*)Src + 2))`), а собственная отладочная строка оригинала
			//   «SOUL OTHER CHECK … connecttype %d» подставляет туда именно `Src[32]`
			//   (possess_broadcast_soulstate.md §1.2/§2.2). Порт этот байт не читал — добавлено.
			const int32 Soul = (int32)B[Off + 32];   // запись гарантированно 101 б (условие цикла)
			UE_LOG(LogAsdaNet, Log, TEXT("5220 soulmate: charId=%d uidx=%d soulIdx=%d partnerUidx=%d soulState=%d%s"),
				CharId, UserIdx, SoulIdx, PartnerU, Soul, (Soul == 2) ? TEXT(" (ОДЕРЖИМ)") : TEXT(""));
			// Кладём по ОБОИМ ключам: 5220 несёт и charId, и userIdx, а плашка соседа ищется по charId
			//   (основной канал 6152), своя — по userIdx.
			SoulStateByUserIdx.Add(UserIdx, Soul);
			if (CharId != 0) { SoulStateByCharId.Add(CharId, Soul); }
			OnPeerSoulStateByUserIdx.Broadcast(UserIdx, Soul);
			if (CharId != 0) { OnPeerSoulStateByCharId.Broadcast(CharId, Soul); }
			OnSoulmateState.Broadcast(CharId, UserIdx, SoulIdx, PartnerU);
			// ★ 23.10.2026 (панель Друга 6500): запись ЦЕЛИКОМ (101 б) — тем же каналом, что 5110/5111, кодом 5220.
			//   Эталон для СВОЕЙ записи (`*(DWORD*)Src == localChar[263]`, charId) кладёт галки 6546/6543..6545 (@33/@36..38),
			//   «Друг в игре» (@25) и показывает панель (`sub_92CBC8`) — это делает `UAsdaSoulmateSubsystem::OnSelfSoulInfo5220`
			//   (сверку «своя ли» — тоже он). Заголовок не трогаем: делегат `OnSoulmateCard` уже несёт (опкод, тело).
			{
				TArray<uint8> Rec101;
				Rec101.Append(&B[Off], 101);
				OnSoulmateCard.Broadcast(5220, Rec101);
			}
		}
		break;
	}
	case OP_BUFF_UI_LOAD_AND_REFRESH_ICONS: // 5112 0x13F8 — ★ 23.10.2026: эффекты Друга (`buffUiLoadAndRefreshIcons@0xE65390`)
	{
		// Тело 168 б = 28 записей × 6 (@0 вид u8, @1 u8, @2 id i32) — сервер `recvSoulMateStateInfo@0x493CB0`
		//   (`sub_4C28F0(…, 5112, 168, …)`). Эталон копирует их в запись соулмейта +83 и рисует значки 33000…33027.
		const std::vector<uint8>& Bv = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(Bv);
		UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←5112 эффекты Друга, тело %d б"), Body.Num());
		OnSoulmateCard.Broadcast(5112, Body);
		break;
	}
	case OP_SOULMATE_UPDATE_INFO:           // 5110 0x13F6 — запись соулмейта ЦЕЛИКОМ (soulmateUpdateInfo@0xE64C30)
	case OP_SOULMATE_WND_APPLY_INFO_PACKET: // 5111 0x13F7 — она же без 13-байтового заголовка (@0xE65060)
	{
		// Тело отдаём как есть — применение к 251-байтовой записи делает UAsdaSoulmateSubsystem
		//   (сдвиг и список копируемых полей описаны у делегата OnSoulmateCard).
		const std::vector<uint8>& B = Pkt.Body;
		if (B.empty()) { break; }
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←%u запись соулмейта, тело %d б"),
			(uint32)Pkt.Opcode, Body.Num());
		OnSoulmateCard.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}
	case OP_ITEM_ENCHANT_ON_RESULT_PACKET: // 5205 0x1455 — ОТКАЗ на запрос разрыва (миснейм; §5.2)
	{
		// ⚠ Диспетчер эталона зовёт этот обработчик с g_pSoulmateWnd — это пакет СОУЛМЕЙТА, не заточки.
		//   [0] = причина: 1 в бою / 2 Smidx не совпал / 3 партнёр не онлайн.
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 1, (int32)Pkt.Opcode))
		{
			const int32 Reason = B[0];
			UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←5205 отказ разрыва: причина=%d, тело %d б"),
				Reason, (int32)B.size());
			OnSoulmateBreakRefused.Broadcast(Reason);
			// ★ АУДИТ 31.08.2026 (T2): одного кода МАЛО. Пакет несёт 61 байт, и ветка
			//   «партнёр просит разрыва» требует его целиком, чтобы ответить 5206 (там идут
			//   charId/userNum партнёра и эхо-байты). Отдаём тело как есть отдельным делегатом.
			OnSoulmateBreakRequest.Broadcast(NetBodyToArray(B));
		}
		break;
	}
	case OP_SOULMATE_PARSE_SEARCH_RESULT: // 5484 0x1564 — список кандидатов поиска (soulmateParseSearchResult@0xE73E00)
	{
		// Записи переменной длины (33 + rec[31]). Разбор с проверкой длины (лечение D7) — в подсистеме.
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←5484 результаты поиска, тело %d б"), Body.Num());
		OnSoulmateSearch.Broadcast(Body);
		break;
	}
	case OP_FIND_SOULMATE_WIN_SHOW: // 5485 0x156D — показ окна поиска Друга 8200 (findSoulmateWin_Show@0xE741B0), ★ волна 1-А
	{
		// [ПРОТОКОЛ] Тело пустое (`sub_4C28F0(…, 5485, 0, …)` в `Send_SoulSearch@0x54DFD0`); эталон его не читает — длину не проверяем.
		UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←5485 показ окна поиска, тело %d б"), (int32)Pkt.Body.size());
		OnSoulmateSearchShow.Broadcast();
		break;
	}
	case OP_CLAN_MY_MEMBERSHIP: // 6713 0x1A39 — Recv_ClanMyMembership@0xCA4AB0: СВОЯ фракция (лагерь)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 7, (int32)Pkt.Opcode))
		{
			const int32 Fid = AsdaBytes::RdI16(&B[0]);
			const int32 Pts = AsdaBytes::RdI32(&B[2]);
			const int32 Gr  = (int32)(int8)B[6];
			UE_LOG(LogAsdaNet, Log, TEXT("6713 фракция self: id=%d очки=%d звание=%d"), Fid, Pts, Gr);
			OnSelfFaction.Broadcast(Fid, Pts, Gr);
		}
		break;
	}
	case OP_UI_ITEM_NOTICE_DISPATCH:   // 4151 — результат «Добавить приятеля» (uiItemNoticeDispatch@0xCE7D00)
	case OP_PARTY_MEMBER_5283:         // 5283 — заявка обмена партнёру (Recv_PartyMember@0xDF3480, a2=7)
	// ★ 29.09.2026 (волна О1 торговли): итог заявки и закрытие обмена — тем же делегатом (потребитель
	//   `UAsdaExchangeP2PSubsystem`, разбор в его `Tick`). Гейт «закрыл я» (+0x08, network.c:1458) — у потребителя.
	case OP_EXCHANGE_REQUEST_RESULT:   // 5285 — `Recv_ExchangeRequestResult@0xC84160`, 50 б обеим сторонам
	case OP_EXCHANGE_CLOSED:           // 5287 — `Recv_ExchangeClosed@0xC86BA0`, 20 б партнёру
	// ★ 29.09.2026 (волна О2): ответ на 5288 — тем же делегатом, разбор в `Tick` `UAsdaExchangeP2PSubsystem`.
	case OP_EXCHANGE_SLOT_UPDATE:      // 5289 — `Recv_Exchange_SlotUpdate@0xC84820`, 8 б: код, invType, invSlot, кол-во
	// ★ 30.09.2026 (волна О3): готовность партнёра / отказ моей фиксации — тем же делегатом, разбор в `Tick`.
	case OP_EXCHANGE_LOCK_STATE:       // 5292 — `Recv_ExchangeLockState@0xC85000`, 261 б: [4] действие, [5] lock, 5×51 записей
	// ★ 30.09.2026 (волна О4): итог обмена — тем же делегатом, разбор (применение записей, золото, вес, закрытие окна) в `Tick`
	//   `UAsdaExchangeP2PSubsystem` (закон no-reentrant-net-callbacks). Диспетчер эталона — network.c:1466-1472, без гейтов.
	case OP_EXCHANGE_COMPLETE_FULL:    // 5294 — `Recv_Exchange_CompleteFull@0xC86150`, 275 б: [0] код, [1..4] вес, 5×54 записей (тип 1)
	case OP_EXCHANGE_COMPLETE:         // 5295 — `Recv_Exchange_Complete@0xC85300`, 70 б: [0] код, [1..4] вес, 5×13 записей (тип 0)
	case OP_PLAYER_INFO_WIN_ON_RECV_CHAR_DATA:  // 5479 — сведения о чужом персонаже (playerInfoWin_onRecvCharData@0xDA8680)
	// ★★★ 7006 — ВТОРАЯ ПОЛОВИНА ТОГО ЖЕ ОТВЕТА (тело 631 б: ключ пары @0 + 11×57 б надетых
	//   предметов). Сервер шлёт её СТРОКОЙ НИЖЕ 5479 в `Send_TargetChar_Information@0x549D10`
	//   (`GameServer.c:126101-126102`), приёмник эталона — `netHandlePlayerInfoPacket@0xDA8BD0`.
	//   Обе половины склеивает окно `OTHERCHARA_DATA` 4200 по 4-байтному ключу (5479@+116 == 7006@+0).
	//   ⚠ В список вкладки «Кто?» 7006 заводить НЕЛЬЗЯ: оригинал делает это по дефекту D-W10
	//   (пропущенный `break` в диспетчере) и дописывает до 18 мусорных строк — доказательство
	//   дизасмом и решение «не заводить вовсе» см. `Private/UI/AsdaCommunityWhoSubsystem.cpp:675`.
	case OP_NET_HANDLE_PLAYER_INFO_PACKET:      // 7006
	case OP_COUPLE_RECV_PROPOSAL_NOTICE:        // 5201 — заявка соулмейта (coupleRecvProposalNotice@0xE65BB0)
	case OP_ITEM_MALL_LOGIN_BEGIN:              // 6083 — ответ на 6082 (itemMallLoginBegin@0xF49A60)
	{
		const std::vector<uint8>& Bv = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(Bv);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		// ★ 4151 с кодом 1 («приятель добавлен») несёт тело 32 б: @0 u8 код + ЗАПИСЬ ПРИЯТЕЛЯ 31 б с +1
		//   (community_tabs_frnd_clan_who.md:281-302). Кладём запись в кэш `FriendEntries` — из него
		//   собирается тело 4154 (код класса @4 и имя @11), которых контракт удаления не передаёт.
		//   Остальные коды 4151 записи не несут (0/2/3/4/5 — только нотисы).
		if (Pkt.Opcode == (uint16)OP_UI_ITEM_NOTICE_DISPATCH && Body.Num() >= 32 && Body[0] == 1)
		{
			UpsertFriendEntry(&Body[1]);
			UE_LOG(LogAsdaNet, Log, TEXT("←4151 приятель добавлен: charId=%d (кэш записей приятелей: %d)"),
				AsdaBytes::RdI32(&Body[1]), FriendEntries.Num());
		}
		UE_LOG(LogAsdaNet, Log, TEXT("[меню игрока] ответ %u принят, тело %d б"),
			(uint32)Pkt.Opcode, Body.Num());
		OnPlayerMenuResult.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}

	// ══════ ★ ОТРЯД: заявки 5091 / 5095 (окно PARTYBOX_WIN 2245) ══════════════════════════════
	//   ⚠ ИМЯ 5091 В ENUM ВРЁТ: `OP_MAP_DECO_RELEASE_ALL_RESOURCES` к декорациям карты отношения
	//   не имеет — диспетчер эталона (`RE/docs/30_recv_dispatch_full_map.tsv`) ведёт 5091 и 5095
	//   в ОДНУ функцию `Recv_PartyMember@0xDF3480` с подтипами 0 и 8 соответственно.
	//   Тело 30 б, раскладка у обоих одинакова (см. объявление `OnPartyRequest`).
	case OP_MAP_DECO_RELEASE_ALL_RESOURCES:   // 5091 — заявка «создать отряд» (подтип 0)
	case OP_PARTY_MEMBER_5095:                // 5095 — заявка «вступить в отряд» (подтип 8)
	{
		const std::vector<uint8>& Bv = Pkt.Body;
		if (Bv.size() < 30)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("[отряд] %u: тело %d б < 30"), (uint32)Pkt.Opcode, (int32)Bv.size());
			break;
		}
		TArray<uint8> Body;
		Body.SetNumUninitialized(30);
		FMemory::Memcpy(Body.GetData(), Bv.data(), 30);
		UE_LOG(LogAsdaNet, Log,
			TEXT("[отряд] ←%u заявка: код=%d userNum=%d charId=%d правило=%d reqId=%d"),
			(uint32)Pkt.Opcode, (int32)Body[0], AsdaBytes::RdI16(&Body[1]), AsdaBytes::RdI32(&Body[3]),
			(int32)Body[27], AsdaBytes::RdI16(&Body[28]));
		OnPartyRequest.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}

	// ══════ ★ ОТРЯД: результаты и события ═════════════════════════════════════════════════════
	//   5093 (15 б) `Recv_GM_NEWPARTY_RESPONSEUSER_RES@0xF38E10` · 5097 (29 б)
	//   `Recv_GM_INVITEPARTY_RESPONSE_RES@0xF39710` · 5099 `Recv_GM_PARTY_LEAVEUSER_RES@0xF39C40` ·
	//   5101 `Recv_GM_PARTY_BANISHUSER_RES@0xF3A0E0` · 5102 `…PARTYMASTER_CHANGE@0xF39DA0` ·
	//   5103 `Recv_GM_PARTY_DESTROY@0xF39ED0` · ★ 5104 `Recv_GM_PARTY_POSITION@0xF3A1F0` (волна 1-Б) ·
	//   5107/5108/5109 — панель 2200. Тела разные — разбирает потребитель (`UAsdaPartySubsystem`).
	case OP_GM_NEWPARTY_RESPONSEUSER_RES:   // 5093
	case OP_GM_INVITEPARTY_RESPONSE_RES:    // 5097
	case OP_GM_PARTY_LEAVEUSER_RES:         // 5099
	case OP_GM_PARTY_BANISHUSER_RES:        // 5101
	case OP_GM_PARTY_PARTYMASTER_CHANGE:    // 5102
	case OP_GM_PARTY_DESTROY:               // 5103
	case OP_GM_PARTY_POSITION:              // 5104 — ★ волна 1-Б: тайл соотрядника для мини-карты (12 б, `Recv_GM_PARTY_POSITION@0xF3A1F0`)
	case OP_RECV_GM_MEMBER_INFO:            // 5107 — состав отряда (144 б)
	case OP_GM_PARTY_UPDATE_STATE:          // 5108 — 28 состояний участника
	case OP_RECV_GM_PARTY_UPDATE_PARAM:     // 5109 — уровень/HP/MP участника
	{
		const std::vector<uint8>& Bv = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(Bv);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("[отряд] ←%u результат, тело %d б"), (uint32)Pkt.Opcode, Body.Num());
		OnPartyResult.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}

	// ══════ ★ ЯБЛОКО ДРУЖБЫ (волна 1-Б, 06.10.2026): 6049 / 6051 → `UAsdaSoulAppleSubsystem` ═══════════════
	//   [ПРОТОКОЛ] ⚠ ИМЕНА В ENUM ВРУТ: `OP_NET_ON_DUEL_STAGE_NOTICE` (6049) к дуэли отношения не имеет —
	//   это рост яблока на панели соулмейта 6500 (`net_OnDuelStageNotice@0xE6CF50`, game.c:475428; сервер
	//   `sub_499220@GameServer.c:61307`: @0 u16 userNum · @2 i32 опыт яблока · @6 u8 процент 0…100, 7 б).
	//   6051 `Net_OnExpGiftNotice@0xE6D940` — «яблоко съедено» (14 б: @0 код · @1 i16 userNum · @3 i32 charId).
	//   Диспетчер эталона (game.c:14235-14239) зовёт их без гейта состояния игры. Разбирает потребитель.
	case OP_UI_REFRESH_SLOT_GRID3X3: // 2243 0x8C3 — ★ 24.10.2026 (sm-offline): прежние Друзья + строки P79 Друга вне игры
	{
		// [ОРИГИНАЛ] Ответ на 2244 (кнопка 6584): `uiRefreshSlotGrid3x3@0xE6C2D0` (объект g_pSoulmateWnd) — 9 строк × 27 б
		//   (@0 Smidx i32, @4 SmLvl, @5 Lvl, @6 Job, @7 Charname char[20]); сервер `sub_499A30@0x499A30` режим 1 шлёт 243 б как есть.
		//   С P79-SQL после стоковых строк идут строки активной пары (SmLvl 254/253). Разбор — в подсистеме соулмейта.
		const std::vector<uint8>& Bv = Pkt.Body;
		if (NetNeedLen(Bv, 243, (int32)Pkt.Opcode))
		{
			TArray<uint8> Body = NetBodyToArray(Bv);
			UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←2243 прежние Друзья / сведения о Друге, тело %d б"), Body.Num());
			OnSoulmateCard.Broadcast((int32)Pkt.Opcode, Body);
		}
		break;
	}
	// ★ 26.10.2026 (агент smmessage) [ОРИГИНАЛ]: записки Другу. Диспетчер эталона (network.c:474-477) зовёт оба обработчика с
	//   `g_pSoulmateWnd` без гейта состояния: 6053 `guildHandleEventNotify@0xE6C840` (МИСНЕЙМ — доставка/итог отправки, 100 б,
	//   сервер `sub_4994D0@0x4994D0`), 6055 `partyMemberSyncIcons@0xE6CB70` (МИСНЕЙМ — до трёх непрочитанных, 279 б = 3 × 93,
	//   сервер `sub_4998F0@0x4998F0` из CharMake/CharPortalMake). Тело может прийти с паддингом — разбирает подсистема соулмейта.
	case OP_GUILD_HANDLE_EVENT_NOTIFY: // 6053 0x17A5
	case OP_PARTY_MEMBER_SYNC_ICONS:   // 6055 0x17A7
	{
		TArray<uint8> Body = NetBodyToArray(Pkt.Body);
		UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←%d записка Другу (%s), тело %d б"), (int32)Pkt.Opcode,
			Pkt.Opcode == OP_GUILD_HANDLE_EVENT_NOTIFY ? TEXT("доставка/итог") : TEXT("непрочитанные"), Body.Num());
		OnSoulmateCard.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}
	case OP_TRADE_WND_UPDATE_NOTICE_AND_TARGET: // 5227 0x146B — ★ 23.10.2026 (sm-info): Друг сменил карту/канал (миснейм)
	{
		// [ПРОТОКОЛ] Сервер `sub_4949C0@0x4949C0` шлёт 2 б: [0] канал Друга, [1] карта Друга (живой лог: тело дополнено до 11 б —
		//   хвост не читаем). Эталон `tradeWnd_updateNoticeAndTarget@0xE67B60` (объект g_pSoulmateWnd). Разбор — в подсистеме
		//   соулмейта (тем же каналом, что 5110/5111/5220: делегат несёт опкод и тело; заголовок не трогаем).
		const std::vector<uint8>& Bv = Pkt.Body;
		if (NetNeedLen(Bv, 2, (int32)Pkt.Opcode))
		{
			TArray<uint8> Body = NetBodyToArray(Bv);
			UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←5227 Друг сменил место: канал=%d карта=%d (тело %d б)"),
				(int32)Bv[0], (int32)Bv[1], Body.Num());
			OnSoulmateCard.Broadcast(5227, Body);
		}
		break;
	}
	case OP_TRADE_WND_RESET_AND_NOTIFY: // 5226 0x146A — ★ ревизия 1-Б: «Друг уходит» (`tradeWnd_resetAndNotify` — миснейм, объект g_pSoulmateWnd)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] ←5226 Друг уходит, тело %d б (эталон его не читает)"), (int32)Pkt.Body.size());
		OnSoulmatePartnerLeft.Broadcast();
		break;
	}
	case OP_NET_ON_DUEL_STAGE_NOTICE:   // 6049 — рост «Яблока Дружбы»
	case OP_NET_ON_EXP_GIFT_NOTICE:     // 6051 — «Яблоко Дружбы» съедено
	{
		const std::vector<uint8>& Bv = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(Bv);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("[яблоко] ←%u тело %d б"), (uint32)Pkt.Opcode, Body.Num());
		OnSoulApplePacket.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}

	case OP_AUTH_VISIBLE:   // 4109 0x100D — КЛАН ЧУЖОГО игрока (Recv_ClanMemberLeave_Notice@0xBC9940, мисномер enum)
	{
		// case 1: [3..4] userIdx i16, [5..6] clanId u16, [7..] clanName sz (20). Единственный путь
		//   узнать клан ЧУЖОГО (в снапшотах 4005/4065 клана нет) — сервер шлёт при инвайте/встрече.
		// ★ 2026-08-29: пакет несёт КОД РЕЗУЛЬТАТА в байте 0 (0,2..8 — отказы клан-инвайта со
		//   своими нотисами; 1 — успех). Раньше все коды кроме 1 молча отбрасывались, и пункт
		//   «Вступить в клан» выглядел «ничего не отвечает». Отдаём тело потребителю ЦЕЛИКОМ.
		// ★ 27.09.2026 (СРЕДН-1 ревизии friend_gate): ПОРЯДОК 1:1 с эталоном (network.c:12039-12060) —
		//   сначала поля персонажа @3 (clanId/имя клана, `OnClanInfo`), ПОТОМ нотис 20051, 4138 и 4122+4124
		//   (`OnPlayerMenuResult`): 4122/4124 берут clanId из `char+0x968`, который у приглашённого
		//   появляется именно здесь. Раньше тело уходило потребителю ДО записи клана.
		auto BroadcastWhole = [this, &Pkt]()   // отдать тело 4109 меню игрока (разбор всех кодов)
		{
			const std::vector<uint8>& Bv0 = Pkt.Body;
			TArray<uint8> Whole = NetBodyToArray(Bv0);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
			OnPlayerMenuResult.Broadcast(4109, Whole);
		};
		const uint8* B = Pkt.Body.data();
		if (Pkt.Body.size() < 8 || B[0] != 1) { BroadcastWhole(); break; }
		const int32   Uidx = AsdaBytes::RdI16(&B[3]);
		const int32   Cid  = (int32)AsdaBytes::RdU16(&B[5]);
		// ★ БЕЗОПАСНОСТЬ (аудит соулмейта 2026-08-30, класс дефекта D7): имя клана — 20 байт с +7,
		//   то есть телу нужно 27 байт, а гейт выше требует лишь 8. Короткий (или подделанный)
		//   пакет заставлял декодер читать до 19 байт ЗА концом принятого тела. Длину режем по
		//   фактически принятому размеру: на полноразмерном пакете поведение не меняется.
		const int32   NmLen = FMath::Clamp((int32)Pkt.Body.size() - 7, 0, 20);
		const FString Nm   = FAsdaLoginData::DecodeCp1251(B + 7, NmLen);
		UE_LOG(LogAsdaNet, Log, TEXT("Клан 4109: userIdx=%d clanId=%d '%s'"), Uidx, Cid, *Nm);
		// ★ 27.09.2026: [ОРИГИНАЛ] @3 = я (приглашённый) → `strcpy(char+0x96A, @7)` пишет и СВОЁ имя клана (network.c:12043).
		if (IsSelfUserIdx(Uidx)) { SelfClanName = Nm; }
		OnClanInfo.Broadcast(Uidx, Cid, Nm, FString());
		BroadcastWhole();
		break;
	}
	case OP_AUTH_PASSWORD:   // 4105 0x1009 — РЕЗУЛЬТАТ СОЗДАНИЯ КЛАНА (Recv_AuthPassword@0xBC8CD0, мисномер enum)
	{
		// ★ Пакет несёт КОД РЕЗУЛЬТАТА в байте 0, а не только «свой клан»: окно 14400 разбирает
		//   ВСЕ коды (1 успех, 0/2/3/4/6/7/8/9/10 — отказы). Сюда его отдаём целиком.
		const std::vector<uint8>& Bv = Pkt.Body;
		if (Bv.empty()) { break; }
		TArray<uint8> Body = NetBodyToArray(Bv);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой

		// case 1: [1..2] clanId u16, [3..] clanName (20), [173..] clanRankName (9), [192..195] золото.
		const uint8* B = Bv.data();
		if (Bv.size() >= 182 && B[0] == 1)
		{
			const int32   Cid  = (int32)AsdaBytes::RdU16(&B[1]);
			const FString Nm   = FAsdaLoginData::DecodeCp1251(B + 3, 20);
			const FString Rk   = FAsdaLoginData::DecodeCp1251(B + 173, 9);
			UE_LOG(LogAsdaNet, Log, TEXT("Свой клан 4105: clanId=%d '%s' звание '%s'"), Cid, *Nm, *Rk);
			// ★ 1:1 `Recv_AuthPassword` case 1: клиент пишет в персонажа clanId, имя клана и
			//   ЗВАНИЕ = 1 (`gap_96A[3] = 1`, char+2427 — «глава»). Держим те же зеркала.
			SelfClanName = Nm;
			SelfClanRank = 1;
			OnClanInfo.Broadcast(-1, Cid, Nm, Rk);   // userIdx −1 = локальный игрок
		}
		OnClanCreateResult.Broadcast(Body);
		break;
	}
	case OP_UI_INVEN_NOTICE_BY_CODE:   // 4107 0x100B — РЕЗУЛЬТАТ РОСПУСКА КЛАНА (uiInvenNoticeByCode@0xBC94A0)
	{
		// ⚠ Имя приёмника в эталоне ВРЁТ: к инвентарю пакет отношения не имеет. Тело — 1 байт кода:
		//   1 успех · 0 (12409) · 2 (12411) · 3 (12412) · 4 (12167).
		const std::vector<uint8>& Bv = Pkt.Body;
		if (Bv.empty()) { break; }
		TArray<uint8> Body; Body.SetNumUninitialized(1);
		Body[0] = Bv[0];
		UE_LOG(LogAsdaNet, Log, TEXT("Роспуск клана 4107: код=%d"), (int32)Body[0]);
		if (Body[0] == 1)
		{
			// 1:1 эталон: обнуление кланового блока персонажа (clanId, имя, звание).
			SelfClanName.Empty();
			SelfClanRank = 0;
			KnownClanIndex = 0;   // ★ 06.10.2026 (ревизия): как у 4117 код 1 — пустой клановый блок, индекса клана нет
			// ★ 06.10.2026 (портрет): [ОРИГИНАЛ] `uiInvenNoticeByCode` case 1 (`ui_core.c:17112-17121`) копирует в
			//   персонажа ПУСТОЙ клановый блок 136 б (`sub_9265D4` → `qmemcpy(char+0x968)`): clanId = −1, флаг клана
			//   и эмблема обнулены, затем `CHUD_SetVisible(0)` гасит эмблему портрета. Зеркало `SelfClanId` в мире
			//   раньше здесь не сбрасывалось — после роспуска эмблема вернулась бы на следующем входе на карту.
			//   Тот же делегат, что у 4117/4133 (мир на него подписан): второй путь к одному сбросу не заводим.
			FString MyName;   // имя своего персонажа — по нему мир узнаёт «это я»
			if (UGameInstance* GI = GetGameInstance())
			{
				if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>()) { MyName = WS->GetSelfCharName(); }
			}
			OnClanReset.Broadcast(SelectedCharId, MyName);
		}
		OnClanDisbandResult.Broadcast(Body);
		break;
	}
	case OP_UI_HANDLE_SLOT_RESULT_PACKET:   // 6523 0x197B — ЗВАНИЕ В КЛАНЕ (uiHandleSlotResultPacket@0xBCD0E0)
	{
		// ⚠ Имя эталона ВРЁТ (к слотам отношения не имеет). Значимое для нас:
		//   `charSetFlagAndRefreshUi(*(i32*)packet, *(u16*)(packet + 7))` — charId @0, ЗВАНИЕ @7
		//   пишется в `CCharacter+0x97B` (char+2427). Гейт роспуска клана требует звание == 1.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 9, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		const int32 CharId = AsdaBytes::RdI32(&B[0]);
		const int32 Rank   = (int32)AsdaBytes::RdU16(&B[7]);
		if (SelectedCharId != 0 && CharId == SelectedCharId)
		{
			SelfClanRank = (uint8)FMath::Clamp(Rank, 0, 255);
			UE_LOG(LogAsdaNet, Log, TEXT("6523: своё звание в клане = %d (1 = глава)"), Rank);
		}
		// ★ 01.11.2026 (clan-window): звание в составе клана, нотисы 17170/17171, перерисовка «Рангов» — вкладка «Клан».
		if (UAsdaCommunityClanSubsystem* Cl = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr)
		{
			Cl->OnAdminPacket(6523, NetBodyToArray(B));
		}
		break;
	}
	// ══════ ★★★ 01.11.2026 (clan-window) [ОРИГИНАЛ]: ОТВЕТЫ ПАНЕЛИ «ПРЕДВОДИТЕЛЬ» И СОСТАВА КЛАНА — до этой правки не принимались ═══
	//   Длины — по серверу (`GameServer.c`, сессия clan-window): 4115 26 б · 6509 3 · 6517 4 · 6520 71 · 6522 1 · 6525 62 · 6527 193 ·
	//   6528 143 · 6529 2. Разбор и действия — `UAsdaCommunityClanSubsystem::OnAdminPacket` (эталонные приёмники — в его объявлении).
	case OP_CHANNEL_LIST_RESP_HANDLER:              // 4115 — итог изгнания
	case OP_NET_HANDLE_ITEM_TOOLTIP_NOTICE:         // 6509 — итог прав званий
	case OP_MEMBER_LIST_SELECT_BY_ID:               // 6517 — объявление удалено (рассылка)
	case OP_PARTY_UPDATE_MEMBER_FROM_ITEM:          // 6520 — заметка члена (рассылка)
	case OP_UI_SLOT_RESULT_SHOW_NOTICE17172:        // 6522 — итог смены звания
	case OP_NET_ON_REFINE_SLOT_UPDATE:              // 6525 — герб: сертификат списан
	case OP_UI_TRADE_WINDOW_SET_DATA:               // 6527 — блок клана 191 б (+ источник)
	case OP_UI_PANEL_SELECT_MODE_NOTIFY:            // 6528 — итог передачи главы
	case OP_QUICK_SLOT_RECV_PARTY_MEMBER_DISPATCH:  // 6529 — мне предлагают стать главой
	{
		const std::vector<uint8>& B = Pkt.Body;
		static const TMap<int32, int32> kNeed = { {4115, 1}, {6509, 3}, {6517, 4}, {6520, 7}, {6522, 1}, {6525, 5}, {6527, 191}, {6528, 1}, {6529, 0} };
		const int32* Need = kNeed.Find((int32)Pkt.Opcode);
		UE_LOG(LogAsdaNet, Log, TEXT("←%u клан (панель «Предводитель»/состав): тело %d б, код=%d"), (uint32)Pkt.Opcode, (int32)B.size(),
			B.empty() ? -1 : (int32)B[0]);
		if (Need && *Need > 0 && !NetNeedLen(B, *Need, (int32)Pkt.Opcode)) { break; }
		if (UAsdaCommunityClanSubsystem* Cl = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr)
		{
			Cl->OnAdminPacket((int32)Pkt.Opcode, NetBodyToArray(B));
		}
		break;
	}
	case OP_AUTH_RELOGIN:   // 4133 0x1025 — ВЫХОД/РОСПУСК клана (Recv_AuthRelogin@0xBCB760 case 2, мисномер)
	{
		// case 2: [3..6] charId dword, [8..] ИМЯ ПЕРСОНАЖА sz — эталон матчит локального по userIdx-полю
		//   и имени, чужого — CharManager_FindByCharId(pkt+3) + strcmp имени; сброс clanId=-1 + memset строк.
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 9, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		// ★ 01.11.2026 (clan-window) [ОРИГИНАЛ]: виды 1/2/3 правят СОСТАВ клана (запись 131 б с +3) — вкладка «Клан»;
		//   вид 2 про меня — ещё нотис 20052 и 4138, про другого — строка чата 20053. Зеркала своего персонажа — ниже, как было.
		if (UAsdaCommunityClanSubsystem* Cl = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCommunityClanSubsystem>() : nullptr)
		{
			Cl->OnAdminPacket(4133, NetBodyToArray(Pkt.Body));
		}
		if (B[0] != 2) { break; }
		const int32   Cid4 = AsdaBytes::RdI32(&B[3]);
		// ★ БЕЗОПАСНОСТЬ (аудит соулмейта 2026-08-30, класс дефекта D7): имя — 20 байт с +8,
		//   телу нужно 28 байт, а гейт выше требует лишь 9 ⇒ чтение до 19 байт за концом тела.
		//   Режем длину по фактически принятому размеру; на полном пакете поведение прежнее.
		const int32   NmLen4 = FMath::Clamp((int32)Pkt.Body.size() - 8, 0, 20);
		const FString Nm   = FAsdaLoginData::DecodeCp1251(B + 8, NmLen4);
		UE_LOG(LogAsdaNet, Log, TEXT("Клан-сброс 4133: charId=%d имя '%s'"), Cid4, *Nm);
		// ★ 1:1 `Recv_AuthRelogin` case 2 для СВОЕГО персонажа: `gap_96A[3] = 0` (звание),
		//   `gap_93A[32] = -1` (clanId), memset имени клана и звания. Держим те же зеркала.
		if (SelectedCharId != 0 && Cid4 == SelectedCharId)
		{
			SelfClanName.Empty();
			SelfClanRank = 0;
			KnownClanIndex = 0;   // ★ 06.10.2026 (ревизия): как у 4117 код 1 — свой клан сброшен
		}
		// Идентификация по charId невозможна без реестра имён здесь — прокидываем charId, матчит мир-подсистема.
		OnClanReset.Broadcast(Cid4, Nm);
		break;
	}
	case OP_PARTY_MEMBER_UPDATE_HP_STATE:   // 6542 0x198E — ФЛАГ (эмблема) клана (partyMemberUpdateHpState, мисномер)
	{
		// [0..1] clanId u16, [2] flagOn, [3..42] 40 байт дизайна флага. Применяется ко ВСЕМ с этим clanId.
		const uint8* B = Pkt.Body.data();
		if (!NetNeedLen(Pkt.Body, 43, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		// ★ Аудит №3 (знаковость): эталон `partyMemberUpdateHpState` — `__int16 *a1`, и сравнение
		//   идёт со ЗНАКОВЫМ полем сущности: `*(__int16 *)(j + 2408) == *a1`.
		const int32 Cid  = AsdaBytes::RdI16(&B[0]);
		const bool  bOn  = (B[2] != 0);
		TArray<uint8> Flag40(B + 3, 40);
		UE_LOG(LogAsdaNet, Log, TEXT("Флаг клана 6542: clanId=%d on=%d"), Cid, bOn ? 1 : 0);
		OnClanFlag.Broadcast(Cid, bOn, Flag40);
		break;
	}
	case OP_NET_PARSE_FIXED_RECORD_LIST: // 4135 — ★ СНАПШОТ КЛАНА чужого игрока (решено 2026-07-30)
	{
		// Эталон: netParseFixedRecordList@0xBC4800 (записи 142б в очередь this+497) → потребитель
		//   charUpdateVisibleCharsFromPacket@0xBC4BB0: FindByCharId(rec[0..3]) и qmemcpy 138 байт в
		//   +2408..+2545 (клан-блок ЦЕЛИКОМ) + пере-бейк эмблемы. Сервер (сборка возле 5417/5418):
		//   memcpy(&rec[4], CUser+99096, 138). Раскладка (client offset − 2408 + 4):
		//   [0..3] charId, [4..5] clanId i16 (−1 нет), [6..] clanName sz, [23] clanGrade (1..5; ненулевой
		//   = «в клане», клиентский bInClan), [28..36] rankName sz(9), [101] flagOn,
		//   [102..141] flag40: BorderIdx@102, BorderColor@110, BgColor@118, SignIdx@126, SignColor@134.
		//   ИМЕННО этим пакетом клиент узнаёт клан СОСЕДА (4109 — только событие вступления).
		const uint8* B = Pkt.Body.data();
		for (int32 Off = 0; Off + 142 <= (int32)Pkt.Body.size(); Off += 142)
		{
			const uint8* R = B + Off;
			auto Dw = [R](int32 O) { return AsdaBytes::RdI32(&R[O]); };
			const int32   CharId = Dw(0);
			const int32   Cid    = AsdaBytes::RdI16(&R[4]);
			const FString Nm     = FAsdaLoginData::DecodeCp1251(R + 6, 17);
			const FString Rk     = FAsdaLoginData::DecodeCp1251(R + 28, 9);
			const bool    bFlag  = (R[101] != 0);
			TArray<uint8> Flag40(R + 102, 40);
			UE_LOG(LogAsdaNet, Log, TEXT("Клан-снапшот 4135: charId=%d clanId=%d '%s' звание '%s' flag=%d"),
				CharId, Cid, *Nm, *Rk, bFlag ? 1 : 0);
			// ★ 01.11.2026 (clan-window) [ОРИГИНАЛ] (`asda.Clan.SelfGradeFrom4135`): эталон `charUpdateVisibleCharsFromPacket@0xBC4BB0`
			//   копирует 138 б записи в клановый блок НАЙДЕННОГО персонажа, и свой не исключение (ветка «это я» там же) — то есть
			//   своё звание `char+0x97B` (= [23]) приходит при каждом входе (CharMake шлёт 4135 своему). Порт звание отсюда не брал:
			//   после перезахода глава был «без звания», «Предводитель» отказывал 17006, роспуск у Эллы — тоже, пока не придёт список.
			if (SelectedCharId != 0 && CharId == SelectedCharId && CVarClanSelfGradeFrom4135.GetValueOnGameThread() != 0)
			{
				SelfClanRank = (Cid >= 0) ? R[23] : 0;
				UE_LOG(LogAsdaNet, Log, TEXT("4135: своё звание в клане = %d (1 = глава)"), (int32)SelfClanRank);
			}
			OnClanSnapshot.Broadcast(CharId, Cid, Nm, Rk, bFlag, Flag40);
		}
		break;
	}
	case OP_NET_PARSE_RECORD_LIST_PACKET:   // 6153 — ★ КЛАН СОСЕДА при enter-world (главный путь клана!)
	{
		// Эталон netParseRecordListPacket@0xBC4930: записи ПЕРЕМЕННОЙ длины 25(+40) в ту же очередь
		//   +497, что и 4135 → charUpdateVisibleCharsFromPacket по charId. Сервер шлёт его каждому
		//   соседу входящего и входящему за каждого соседа (sub_422900: len 25, при flagOn 65).
		//   Раскладка: [0..3] charId, [4..5] clanId i16, [6..22] clanName (17), [23] clanGrade,
		//   [24] flagOn; если flagOn≠0 — далее 40 байт дизайна флага (Border/цвета/Sign).
		const uint8* B = Pkt.Body.data();
		const int32  N = (int32)Pkt.Body.size();
		for (int32 Off = 0; Off + 25 <= N; )
		{
			const uint8* R = B + Off;
			const int32   CharId = AsdaBytes::RdI32(&R[0]);
			const int32   Cid    = AsdaBytes::RdI16(&R[4]);
			const FString Nm     = FAsdaLoginData::DecodeCp1251(R + 6, 17);
			const bool    bFlag  = (R[24] != 0);
			Off += 25;
			TArray<uint8> Flag40;
			if (bFlag && Off + 40 <= N) { Flag40.Append(B + Off, 40); Off += 40; }
			UE_LOG(LogAsdaNet, Log, TEXT("Клан 6153: charId=%d clanId=%d '%s' flag=%d"), CharId, Cid, *Nm, bFlag ? 1 : 0);
			// Звание в клане в 6153 не передаётся (только в 4135) — пустая строка не затирает прежнее.
			OnClanSnapshot.Broadcast(CharId, Cid, Nm, FString(), bFlag, Flag40);
		}
		break;
	}
	// ═══ ★★★ ПОИСК ОТРЯДА (окно 8500): ПРИЁМ ОТВЕТОВ СЕРВЕРА 3411…3425 ════════════════════════
	//   ⚠ У ОРИГИНАЛЬНОГО КЛИЕНТА ЭТИХ ВЕТОК НЕТ ВОВСЕ: среди 415 `case` функции
	//     `Net_RecvPacketDispatch_Loop@0x96AD30` нет ни одного опкода 3411…3425, а тела приёмников
	//     `sub_DBF4A0` / `sub_DBFA00` не имеют ни одного вызывателя (байтовый скан всего образа,
	//     `RE/ghidra_legacy/tools_tmp/pf_callers.py`). Ветка вырезана вместе с привязкой окна — заводим заново.
	//   Сервер жив и все восемь запросов обслуживает (`sub_42A9D0@0x42A9D0` → `CPartyManager`,
	//     подбор — `CPartyMatching` в `ChannelManager.exe`).
	case (NetOpcode)3411:   // ответ на 3410 ADD_MEMBER    (9 б: +6 код, +7 присвоенный индекс)
	case (NetOpcode)3413:   // ответ на 3412 SUB_MEMBER    (7 б)
	case (NetOpcode)3417:   // ответ на 3416 UPDATE_MEMBER (9 б)
	case (NetOpcode)3419:   // ответ на 3418 ADD_LEADER    (9 б: +7 индекс отряда в списке)
	case (NetOpcode)3425:   // ответ на 3424 UPDATE_LEADER (9 б)
	{
		TArray<uint8> B = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("←%u поиск отряда: короткий ответ, тело %d б"),
			Pkt.Opcode, B.Num());
		OnPartyFindAck.Broadcast((int32)Pkt.Opcode, B);
		break;
	}
	case (NetOpcode)3415:   // СТРАНИЦА списка ОДИНОЧЕК, 300 б = 10 хедера + 10 × 29
	case (NetOpcode)3423:   // СТРАНИЦА списка ОТРЯДОВ,  350 б = 10 хедера + 10 × 34
	{
		TArray<uint8> B = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("←%u поиск отряда: страница списка, тело %d б"),
			Pkt.Opcode, B.Num());
		OnPartyFindPage.Broadcast((int32)Pkt.Opcode, B);
		break;
	}
	case (NetOpcode)3421:
	{
		// 3421 — 7-байтный ответ на 3420 SUB_LEADER (`CPartyMatching::SubLeader`, CM `0x40CEF9`).
		// ★★★ ДЕФЕКТ ОРИГИНАЛЬНОГО СЕРВЕРА Д2 (`Reports/partyfind_3421_layout.md` §7.1):
		//   `Recv_Client_GetLeader` в ветке «charId тела ≠ charId сессии» шлёт под этим же номером
		//   350-байтную АВАРИЙНУЮ СТРАНИЦУ (`sub_4C28F0(поток, 3421, 350, …)`, `GameServer.c:33362`),
		//   хотя все нормальные страницы отрядов уходят как 3423/350. В оригинале это ненаблюдаемо:
		//   у клиента ветка приёма 3421 вырезана вовсе. Разводим по длине, чтобы код отказа не уехал
		//   в список, а аварийная страница (в ней charId первой записи = −1) честно очистила список.
		TArray<uint8> B = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		if (B.Num() >= 64)
		{
			UE_LOG(LogAsdaNet, Warning,
				TEXT("←3421 поиск отряда: пришла СТРАНИЦА в %d б — это дефект сервера Д2 ")
				TEXT("(должен быть 3423); разбираем как страницу отрядов"), B.Num());
			OnPartyFindPage.Broadcast(3423, B);
		}
		else
		{
			UE_LOG(LogAsdaNet, Log, TEXT("←3421 поиск отряда: ответ на снятие отряда, тело %d б"), B.Num());
			OnPartyFindAck.Broadcast(3421, B);
		}
		break;
	}
	// ══════ ★★★ ПРИЯТЕЛИ (вкладка COMMUNITY_FRND_WIN 14500), опкоды 4152/4155/4157/4158/4160 ══════
	//   Раскладки — `Reports/community_tabs_frnd_clan_who.md` §1.5 (запись 31 б) и §1.6 (опкоды).
	//   ★ 4151 (результат приглашения) уже принимается ВЫШЕ, в общем блоке «ответы меню игрока» —
	//     второй `case` для него завёл бы «duplicate case». Там же (в том блоке) запись приятеля
	//     из кода 1 кладётся в кэш `FriendEntries`, из которого собирается тело 4154.
	case OP_PARTY_MEMBER:   // 4152 — ★ МИСНЕЙМ (`Recv_PartyMember@0xDF3480`): к отряду отношения нет,
	{                       //   это входящее приглашение в ПРИЯТЕЛИ, 22 б: имя[20] + i16 userIndex
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 22, (int32)Pkt.Opcode, TEXT("4152 приглашение в приятели"))) { break; }
		const int32 InvUserNum = AsdaBytes::RdI16(&B[20]);           // @20 i16 userIndex пригласившего
		// ★ Эталон (`questTrackerAddUniqueEntry@0xDF6BA0`) при повторном приглашении от ТОГО ЖЕ
		//   userIndex молча выходит — очередь хранит по одной записи на пригласившего. Повторяем.
		bool bDup = false;
		for (const TArray<uint8>& Inv : PendingFriendInvites)
		{
			if (Inv.Num() >= 22 && AsdaBytes::RdI16(&Inv[20]) == InvUserNum) { bDup = true; break; }
		}
		TArray<uint8> Body = NetBodyToArray(B);
		if (!bDup) { PendingFriendInvites.Add(Body); }
		UE_LOG(LogAsdaNet, Log, TEXT("←4152 приглашение в приятели от userNum=%d%s (очередь %d)"),
			InvUserNum, bDup ? TEXT(" — ПОВТОР, в очередь не кладём") : TEXT(""),
			PendingFriendInvites.Num());
		OnFriendInvite.Broadcast(Body);
		break;
	}
	case OP_UI_TRADE_LIST_REMOVE_ENTRY_BY_ITEM:   // 4155 — результат удаления приятеля, 6 б
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 6, (int32)Pkt.Opcode, TEXT("4155 результат удаления приятеля"))) { break; }
		const uint8 Code   = B[0];                       // @0 1 = удалено, иначе ошибка (loc 12814)
		const int32 CharId = AsdaBytes::RdI32(&B[1]);    // @1 i32 CharUniqueIndex удалённого
		const uint8 Cls    = B[5];                       // @5 u8 код класса (ключ пары)
		if (Code == 1)
		{
			// Кэш чистим по ТОЙ ЖЕ паре (charId, класс), что и эталон (`listContainsIdTypePair`).
			FriendEntries.RemoveAll([CharId, Cls](const TArray<uint8>& R)
			{
				return R.Num() >= 31 && AsdaBytes::RdI32(&R[0]) == CharId && R[4] == Cls;
			});
		}
		UE_LOG(LogAsdaNet, Log, TEXT("←4155 удаление приятеля: код=%u charId=%d класс=%u (кэш: %d)"),
			(uint32)Code, CharId, (uint32)Cls, FriendEntries.Num());
		OnFriendDeleteResult.Broadcast(NetBodyToArray(B));
		break;
	}
	case OP_PKT_PARSE_LIST_RECORDS31:   // 4157 — СПИСОК приятелей: записи по 31 б подряд
	{
		const std::vector<uint8>& B = Pkt.Body;
		// ★ Защита от переполнения (лечение D7): длину записи зажимаем по ОСТАТКУ тела. Эталон
		//   крутит `for (off = 0; off < size − 1; off += 31)` и на «хвосте» читает за пределами.
		//   Пустой список (тело 0 б) — законный ответ, это не ошибка.
		FriendEntries.Reset();
		const int32 Len = (int32)B.size();
		for (int32 Off = 0; Off + 31 <= Len; Off += 31)
		{
			TArray<uint8> Rec;
			Rec.SetNumUninitialized(31);
			FMemory::Memcpy(Rec.GetData(), &B[Off], 31);
			FriendEntries.Add(MoveTemp(Rec));
		}
		const int32 Tail = Len % 31;
		UE_LOG(LogAsdaNet, Log, TEXT("←4157 список приятелей: %d записей по 31 б (тело %d б%s)"),
			FriendEntries.Num(), Len,
			Tail ? *FString::Printf(TEXT(", хвост %d б ОТБРОШЕН"), Tail) : TEXT(""));
		OnFriendList.Broadcast(NetBodyToArray(B));
		break;
	}
	case OP_RECV_TRADE_LIST_PAGE_REFRESH:   // 4158 — «перерисуй список», тело ПУСТОЕ
	{
		// ★ ДЕФЕКТ ОРИГИНАЛА D-F9 воспроизведён 1:1: приём 4158 ПЕРЕЗАПУСКАЕТ 5-секундный антиспам
		//   4156 (эталон `recvTradeListPageRefresh@0xCE9140` зовёт `timerStart_9B90`), то есть после
		//   каждого серверного «перерисуй» кнопка «Обновить» мертва ещё 5 секунд.
		// ★ Откат дефекта живёт ключом вкладки «Приятель»: сам таймер антиспама живёт
		//   ЗДЕСЬ, в сети, поэтому ключ читаем через общий реестр — иначе он был бы мёртвым
		//   (найдено аудитом 31.08.2026).
		bool bKeepTimer = false;
		if (IConsoleVariable* CVar = ASDA_FIND_DEV_CVAR(TEXT("asda.Community.Friend.KeepTimerOn4158")))
		{
			bKeepTimer = (CVar->GetInt() != 0);
		}
		if (!bKeepTimer) { LastFriendListReqTime = FPlatformTime::Seconds(); }
		UE_LOG(LogAsdaNet, Log, TEXT("←4158 «перерисуй список приятелей» (антиспам 4156 %s)"),
			bKeepTimer ? TEXT("НЕ тронут — ключ KeepTimerOn4158=1") : TEXT("перезапущен, как в оригинале"));
		OnFriendListRefresh.Broadcast();
		break;
	}
	case OP_FRIEND_ENTRY_UPDATE:   // 4160 — обновление ОДНОЙ записи списка, 33 б = i16(0) + запись 31 б
	{
		// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D-F4 (помечено): в эталонном диспетчере `case 0x1040`
		//   отсутствует — сервер шлёт, клиент молча роняет, онлайн-статус приятеля не обновляется.
		//   ДОКАЗАТЕЛЬСТВО отсутствия: в ветке приятелей эталона идут подряд `case 0x1037` (4151),
		//   `0x1038` (4152), `0x103B` (4155), `0x103D` (4157), `0x103E` (4158), `0x103F` (4159)
		//   — game.c:15955-15975 — и сразу `default: continue;`. 0x1040 (4160) там нет.
		//
		// ★★★ ПОЛЕ @0 i16 — ПРОБЕЛ ЗАКРЫТ 31.08.2026. Смысл: это ЗАГОЛОВОЧНОЕ поле серверной
		//   структуры-обёртки `{ __int16 hdr; FriendRecord31 rec; }`, и оно ВСЕГДА НОЛЬ.
		//   (а) Конструктор обёртки `sub_40BDC0@0x40BDC0` (GameServer.c:5174-5179):
		//         `sub_40B8E0(this + 2); *(_WORD *)this = 0;`
		//       где `sub_40B8E0@0x40B8E0` (GameServer.c:4954-4966) — конструктор ЗАПИСИ 31 б
		//       (@0 i32 = −1, @4..@10 = 0, @11 имя[20] = 0). Итого 2 + 31 = 33 ✓.
		//   (б) Единственный во всём GameServer производитель 4160 — обработчик ответа БД со списком
		//       приятелей (GameServer.c:2796-2832). Он явно кладёт `*(_WORD *)v16 = 0;`
		//       (GameServer.c:2805) и больше к этим двум байтам не притрагивается; дальше идут
		//       смежные поля того же 33-байтового блока (ebp-0x28 … ebp-0x07):
		//         @2  i32 charId  = *(v12 + 372)        @6  u8 класс   = *(v12 + 396)
		//         @7  u8 сервер   = qword_5F5EB0[4]     @8  u8 канал    = sub_40C960(userMng)
		//         @9/@10/@11 u8   = *(v12 + 402/400/401)
		//         @12 u8 = 1 (ОНЛАЙН)                   @13 char[20] имя = memcpy(v12 + 376, 20)
		//       Обе рассылки — `sub_4C28F0(..., 4160, 33, v7, v16, 0, 0)` (:2827, тем, кто уже в
		//       игре) и `sub_537FB0(..., &v11[11], 33, 4160, v16)` (:2832, по имени) — шлют этот же
		//       блок. Второго производителя нет ни в GameServer, ни в ItemRelayServer.
		//   (в) Потребителя нет: клиент 4160 не разбирает вообще (см. выше). Парсер 4157
		//       `pktParseListRecords31@0xCE8FE0` идёт записями по 31 б от +9 без всякого заголовка.
		//   ⇒ ДОКАЗАНО: поле @0 не несёт индекса/страницы/кода — обновляемую запись адресует
		//     только charId внутри неё, поэтому наш `UpsertFriendEntry` и ключуется по (charId, класс).
		//     «Всегда 0, никем не читается» — HIGH. Иного смысла в срезе (клиент + GameServer +
		//     ItemRelayServer) не существует: поле мертво с обеих сторон.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 33, (int32)Pkt.Opcode, TEXT("4160 обновление записи приятеля"))) { break; }
		UpsertFriendEntry(&B[2]);                                   // запись 31 б лежит с +2
		UE_LOG(LogAsdaNet, Log, TEXT("←4160 обновление приятеля: charId=%d класс=%u онлайн=%u (кэш %d)"),
			AsdaBytes::RdI32(&B[2]), (uint32)B[6], (uint32)B[12], FriendEntries.Num());
		OnFriendEntryUpdate.Broadcast(NetBodyToArray(B));
		break;
	}

	// ══════ ★★★ ВКЛАДКА КЛАНА (COMMUNITY_CLAN_WIN 14033): 4125 — порция списка членов ═════════
	//   ★ 4133 (обновление ОДНОГО члена) в диспетчере УЖЕ ЗАНЯТ выше (выход/роспуск клана) — не трогаем.
	//   ★ 4126 «конец списка» ЗАВЕДЁН НИЖЕ (реверс закрыт: payload 0, тело клиентом не читается) —
	//     подписчику больше не надо угадывать границу порций по длине пакета.
	case OP_RECV_PARSE_SERVER_CHANNEL_LIST:   // 4125 — ★ МИСНЕЙМ: не список каналов, а СПИСОК ЧЛЕНОВ клана
	{
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Len = (int32)B.size();
		const int32 Cnt = Len / 131;             // записи по 131 б (§2.5); хвост зажимаем по остатку
		UE_LOG(LogAsdaNet, Log, TEXT("←4125 список членов клана: %d записей по 131 б (тело %d б%s)"),
			Cnt, Len, (Len % 131) ? *FString::Printf(TEXT(", хвост %d б"), Len % 131) : TEXT(""));
		OnClanMemberList.Broadcast(NetBodyToArray(B));
		break;
	}

	// ══════ ★★★ ПЯТЬ ОПКОДОВ КЛАНА, ЗАКРЫТЫЕ РЕВЕРСОМ (`clan_soulmate_gaps.md` §2) ═══════════════
	case OP_RECV_TRADE_WINDOW_STATE:   // 4123 0x101B — ИНФО-ПАНЕЛЬ КЛАНА (recvTradeWindowState@0xBCAE60)
	{
		// ⚠ Имя опкода и имя обработчика в эталоне ВРУТ: торговли здесь нет, это клан.
		//   Эталон копирует РОВНО 0xBF = 191 байт в `wnd+0x614` и зовёт перерисовку панели.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 191, (int32)Pkt.Opcode, TEXT("4123 инфо-панель клана"))) { break; }
		// Клан-индекс блока (@0) запоминаем: им пользуются 4122/4124/6511, если явного нет.
		const int32 Cid = AsdaBytes::RdI16(&B[0]);
		if (Cid > 0) { KnownClanIndex = Cid; }
		UE_LOG(LogAsdaNet, Log,
			TEXT("←4123 инфо клана (191 б): clanIndex=%d уровень=%d маски прав 2..5 = %u/%u/%u/%u"),
			Cid, (int32)B[47] + 1,
			(uint32)AsdaBytes::RdU16(&B[57]), (uint32)AsdaBytes::RdU16(&B[59]),
			(uint32)AsdaBytes::RdU16(&B[61]), (uint32)AsdaBytes::RdU16(&B[63]));
		OnClanBaseInfo.Broadcast(NetBodyToArray(B));
		break;
	}
	// ★ 27.09.2026: 4131/4132 — инфо-блок клана «сверху» (раскладка и смысл — в объявлении делегата).
	case OP_RECV_TRADE_WINDOW_STATE_ALT:   // 4131 0x1023 — блок 191 б (recvTradeWindowStateAlt@0xBCB4E0)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 191, (int32)Pkt.Opcode, TEXT("4131 инфо-блок клана"))) { break; }
		UE_LOG(LogAsdaNet, Log, TEXT("←4131 инфо-блок клана (191 б): clanIndex=%d лимит=%d расширений=%d"),
			(int32)AsdaBytes::RdI16(&B[0]), (int32)B[45], (int32)B[47]);
		OnClanBaseInfoPush.Broadcast(4131, NetBodyToArray(B));
		break;
	}
	case OP_UI_TRADE_WINDOW_RESET_STATE:   // 4132 0x1024 — код + блок (uiTradeWindowResetState@0xBCB530)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 1, (int32)Pkt.Opcode, TEXT("4132 обновление клана"))) { break; }
		if (B[0] == 1 && B.size() < 192)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("←4132 код 1: тело %d б < 192 — блок не применён"), (int32)B.size());
			break;
		}
		UE_LOG(LogAsdaNet, Log, TEXT("←4132 обновление клана: код=%d%s"), (int32)B[0],
			B[0] == 1 ? *FString::Printf(TEXT(" clanIndex=%d лимит=%d расширений=%d"),
				(int32)AsdaBytes::RdI16(&B[1]), (int32)B[46], (int32)B[48]) : TEXT(""));
		OnClanBaseInfoPush.Broadcast(4132, NetBodyToArray(B));
		break;
	}
	case OP_UI_INVENTORY_REPAIR_RESULT_HANDLER:   // 4137 0x1029 — РЕЗУЛЬТАТ РАСШИРЕНИЯ КЛАНА (мисномер)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 1, (int32)Pkt.Opcode, TEXT("4137 расширение клана"))) { break; }
		UE_LOG(LogAsdaNet, Log, TEXT("←4137 расширение клана: код=%d%s"), (int32)B[0],
			B.size() >= 9 ? *FString::Printf(TEXT(" лимит=%d золото=%d расширений=%d"),
				(int32)B[3], AsdaBytes::RdI32(&B[4]), (int32)B[8]) : TEXT(""));
		OnClanIncreaseResult.Broadcast(NetBodyToArray(B));
		break;
	}
	case OP_ITEM_SET_SPLIT_MOVE_SOURCE:   // 4110 0x100E — ★ 27.09.2026: ВХОДЯЩЕЕ ПРИГЛАШЕНИЕ В КЛАН (мисномер enum)
	{
		// Эталон `case 0x100E` (network.c:2149-2152): значок 6 + `itemSetSplitMoveSource@0xBC9E40` (поля @0/@2/@19/@21).
		//   Разбор — `UAsdaClanSubsystem` (гейт «открыто окно ответа» и хранение полей); здесь только длина и лог.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 23, (int32)Pkt.Opcode, TEXT("4110 приглашение в клан"))) { break; }
		UE_LOG(LogAsdaNet, Log, TEXT("←4110 приглашение в клан: clanId=%d имя='%s' пригласил=%d мне=%d"),
			(int32)AsdaBytes::RdI16(&B[0]), *FAsdaLoginData::DecodeCp1251(&B[2], 17),
			(int32)AsdaBytes::RdI16(&B[19]), (int32)AsdaBytes::RdI16(&B[21]));
		OnClanInviteRecv.Broadcast(NetBodyToArray(B));
		break;
	}
	case OP_SERVER_CHANNEL_LIST:   // 4126 0x101E — КОНЕЦ СПИСКА ЧЛЕНОВ (Recv_ServerChannelList, мисномер)
	{
		// payload РОВНО 0 — тело не читает ни клиент (`case 0x101E` смотрит только на факт пакета),
		//   ни сервер его не заполняет (`sub_4C28F0(…, 4126, 0, …, nullptr, …)`). Длину НЕ проверяем.
		UE_LOG(LogAsdaNet, Log, TEXT("←4126 конец списка членов клана (тела нет; принято %d б)"),
			(int32)Pkt.Body.size());
		OnClanMemberListEnd.Broadcast();
		break;
	}
	case OP_UI_INVENTORY_ITEM_NAME_RESULT_HANDLER:   // 4117 0x1015 — ОТВЕТ на 4116/4114 (мисномер эталона)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.empty()) { break; }
		// `body[0]`: 0 → нотис 12424 (отказ), 1 → нотис 12425 (успех) + эталон чистит клановый
		//   блок персонажа и закрывает окно сообщества. Зеркала полей держим здесь, показ — у окна.
		UE_LOG(LogAsdaNet, Log, TEXT("←4117 ответ «выход из клана»: код=%u (0 отказ / 1 успех), тело %d б"),
			(uint32)B[0], (int32)B.size());
		if (B[0] == 1)
		{
			SelfClanName.Empty();
			SelfClanRank = 0;      // 1:1 эталон: `memset` кланового блока `char+0x968` (136 б)
			KnownClanIndex = 0;
			// ★ НАЙДЕНО АУДИТОМ 31.08.2026: зеркало `SelfClanId` в мире сбрасывалось ТОЛЬКО
			//   по 4133 (роспуск). После СВОЕГО выхода (4117 код 1) клан «оставался»: плашка
			//   держала название, а гейт вкладки клана (`clanId == -1`) продолжал пускать.
			//   Зовём ТОТ ЖЕ делегат, что и 4133, — мир на него уже подписан
			//   (`AsdaWorldSubsystem.cpp:270`); прямой вызов метода был бы вторым путём к одному и тому же.
			{
				FString MyName;
				if (UGameInstance* GI = GetGameInstance())
				{
					if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
					{
						MyName = WS->GetSelfCharName();
					}
				}
				OnClanReset.Broadcast(SelectedCharId, MyName);
			}
		}
		OnClanLeaveResult.Broadcast(NetBodyToArray(B));
		break;
	}
	case OP_GUILD_MEMBER_LIST_RECV_UPDATE:   // 6512 0x1970 — ОБЪЯВЛЕНИЯ КЛАНА (★ имя ВРЁТ)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 546, (int32)Pkt.Opcode, TEXT("6512 объявления клана"))) { break; }
		// [0] флаг: не 1 — эталон игнорирует пакет ЦЕЛИКОМ (`00BCC3B4 cmp byte [ebp-0x48],1; je`).
		if (B[0] != 1)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("←6512 объявления клана: флаг=%u ⇒ пакет игнорируется (1:1 эталон)"),
				(uint32)B[0]);
			break;
		}
		UE_LOG(LogAsdaNet, Log,
			TEXT("←6512 объявление клана: маркер=%u (2 = последняя порция), ключ записи=%d ")
			TEXT("[раскладка 544-байтовой записи НЕ ДОКАЗАНА]"),
			(uint32)B[1], AsdaBytes::RdI32(&B[2]));
		OnClanNotice.Broadcast(NetBodyToArray(B));
		break;
	}

	// ★★★ 6514 / 6519 — ОТВЕТЫ на запись объявления клана (6513) и заметки о себе (6518).
	//   Добавлены 2026-09-02 вместе с отправителями. ДЕЛАТЬ ЗДЕСЬ НЕЧЕГО, и это НЕ упрощение:
	//   у эталона оба обработчика — пустые заглушки. Диспетчер их принимает (`network.c:844`
	//   `case 0x1972 → sub_92FE27`, `:853` `case 0x1977 → sub_92363B`), но за thunk'ами стоят
	//   `sub_BCC190@0xBCC190` и `sub_BCCAF0@0xBCCAF0`, а их тело дословно `return a1;` (снято
	//   дизассемблером живого exe). То есть оригинал ни подтверждения, ни отказа не показывает.
	//   Ветки заведены ровно затем, чтобы ответ не падал в «неизвестный опкод», и чтобы код
	//   результата был виден в журнале при разборе отказов сервера.
	// ★ 07.11.2026: 6516 — итог удаления объявления (6515). Эталонный приёмник тоже пуст (`case 0x1974`); строка журнала нужна для разбора
	//   отказов: код 0 — неверный клан/нет права, 1 — удалено (следом рассылка 6517), 2 — ошибка БД.
	case (NetOpcode)6516:
		UE_LOG(LogAsdaNet, Log, TEXT("←6516 итог удаления объявления клана: код=%d"), Pkt.Body.empty() ? -1 : (int32)Pkt.Body[0]);
		break;
	case OP_RECV_USER_NOTICE_WRITE_RES:   // 6514 0x1972 — тело 1 б (код результата от БД)
	case OP_RECV_USER_MEMO_WRITE_RES:     // 6519 0x1977 — тело 1 б после БД, 70 б при отказе на гейте
	{
		const std::vector<uint8>& B = Pkt.Body;
		UE_LOG(LogAsdaNet, Log,
			TEXT("←%u ответ на запись %s: тело %d б, код=%d (эталонный обработчик ПУСТ — реакции нет)"),
			(uint32)Pkt.Opcode,
			Pkt.Opcode == (uint16)OP_RECV_USER_NOTICE_WRITE_RES ? TEXT("объявления клана 6513") : TEXT("заметки о себе 6518"),
			(int32)B.size(), B.empty() ? -1 : (int32)B[0]);
		break;
	}

	// ══════ ★★★ ВКЛАДКА «КТО?» (COMMUNITY_Who_WIN 14600): 5481 страница, 5482 конец списка ═════
	case OP_COMMUNITY_WHO_APPEND_LIST:   // 5481 — записи по 35 б, до 18 на пакет
	{
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Len = (int32)B.size();
		// Считаем «живые» записи ровно так же, как эталон `communityWhoAppendListFromPacket@0xEBFC10`:
		//   стоп на записи с userIndex(@28) == −1 ИЛИ CharUniqueIndex(@30) == −1. Длину каждой записи
		//   зажимаем по остатку тела — сервер режет порции по 615 б и хвост может быть неполным.
		// ★ Записи ДОПИСЫВАЮТСЯ в накопитель, как в эталоне (`this->entries[35 * this->entryCount++]`);
		//   очищает накопитель только отправка 5480 (`*(this+73) = 0`, game.c:505590).
		//   Ёмкость окна-эталона 40 записей при серверном лимите выборки 42 — переполнение эталона
		//   (дефект) не воспроизводим: держим весь список, страницы считаются от фактического числа.
		int32 Live = 0;
		for (int32 Off = 0; Off + 35 <= Len; Off += 35)
		{
			if (AsdaBytes::RdI16(&B[Off + 28]) == -1 || AsdaBytes::RdI32(&B[Off + 30]) == -1) { break; }
			TArray<uint8> Rec;
			Rec.SetNumUninitialized(35);
			FMemory::Memcpy(Rec.GetData(), &B[Off], 35);
			WhoEntries.Add(MoveTemp(Rec));
			++Live;
		}
		UE_LOG(LogAsdaNet, Log, TEXT("←5481 «Кто?»: %d записей по 35 б (тело %d б; всего в накопителе %d, ")
			TEXT("страниц по 7 строк: %d)"),
			Live, Len, WhoEntries.Num(), FMath::Max(1, (WhoEntries.Num() + 6) / 7));
		OnWhoResult.Broadcast(NetBodyToArray(B));
		break;
	}
	case OP_UI_COMMUNITY_WHO_UPDATE_PAGING:   // 5482 — «конец списка», тело ПУСТОЕ
	{
		// Эталон здесь считает `totalPages = ceil(entryCount / 7)`, минимум 1
		//   (`uiCommunityWhoUpdatePaging@0xEBFD40`, game.c:505727-505731) — страница КЛИЕНТСКАЯ.
		UE_LOG(LogAsdaNet, Log, TEXT("←5482 «Кто?»: конец списка — %d записей, %d страниц по 7 строк"),
			WhoEntries.Num(), FMath::Max(1, (WhoEntries.Num() + 6) / 7));
		OnWhoNotice.Broadcast(NetBodyToArray(Pkt.Body));
		break;
	}

	// ══════ ★★★ НАВЫКИ ДРУЖБЫ (соулмейт): 5232 состояние, 5233 применение, 2247 кулдауны ═══════
	//   Раскладки — `Reports/soulmate_skills_recall_roulette.md` A.3 / A.4 / A.5.
	case OP_NET_HANDLE_SOULMATE_SKILL_STATE:   // 5232, 197 б
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 197, (int32)Pkt.Opcode, TEXT("5232 состояние навыка Дружбы"))) { break; }
		UE_LOG(LogAsdaNet, Log, TEXT("←5232 навык Дружбы: код=%u userNum кастующего=%d навык=%d"),
			(uint32)B[0], AsdaBytes::RdI16(&B[1]), AsdaBytes::RdI16(&B[3]));
		ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 5232 навык Дружбы (ответ на 5231): код=%d кастер userNum=%d навык=%d"),
			(int32)B[0], AsdaBytes::RdI16(&B[1]), AsdaBytes::RdI16(&B[3]));
		OnSoulSkillState.Broadcast(NetBodyToArray(B));
		break;
	}
	case OP_NET_RECV_SOULMATE_SKILL_APPLY:   // 5233, 196 б
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 196, (int32)Pkt.Opcode, TEXT("5233 применение навыка Дружбы"))) { break; }
		UE_LOG(LogAsdaNet, Log, TEXT("←5233 навык Дружбы применён: userNum получателя=%d навык=%d"),
			AsdaBytes::RdI16(&B[0]), AsdaBytes::RdI16(&B[2]));
		ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 5233 навык Дружбы применён: получатель userNum=%d навык=%d"), AsdaBytes::RdI16(&B[0]), AsdaBytes::RdI16(&B[2]));
		OnSoulSkillApply.Broadcast(NetBodyToArray(B));
		break;
	}
	case OP_NET_RECV_SOULMATE_SKILL_COOL_TIME:   // 2247, 246 б = 6 + 30 пар по 8 б
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 246, (int32)Pkt.Opcode, TEXT("2247 кулдауны навыков Дружбы"))) { break; }
		// ★ charId (@2, i32) — именно charId, а не userNum (@0, i16): сервер сверяет им синхронизацию.
		int32 Live = 0;
		for (int32 i = 0; i < 30; ++i)
		{
			if (AsdaBytes::RdI32(&B[6 + 8 * i]) == -1) { break; }   // терминатор списка
			++Live;
		}
		UE_LOG(LogAsdaNet, Log, TEXT("←2247 кулдауны навыков Дружбы: userNum=%d charId=%d, записей=%d"),
			AsdaBytes::RdI16(&B[0]), AsdaBytes::RdI32(&B[2]), Live);
		OnSoulSkillCooldowns.Broadcast(NetBodyToArray(B));
		break;
	}

	// ══════ ★★★ ПОДСВЕТКА ИКОНОК ЛОТКА ДРУЖБЫ: 5228 весь блок, 5238 один слот ═══════════════════
	//   Оба писателя доказаны в `RE/etalon/network.c` (диспетчер :1782 и :1806) — см. подробный
	//   разбор у объявления `SoulSkillTrayBlob` в заголовке. Без них лоток серый целиком.
	default: bHandled = false; break;
	}
}
