// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_RecvWorld.cpp — часть `AsdaNetSubsystem.cpp`: приёмник пакетов: сущности мира: мобы/деспавн, квесты, удары 4015/4017, крылья, портал 5055, скорость 6071, усилители, эффекты навыков/баффы, ивенты, GM-состояние, питомцы, касты навыков; движение 4007/4009 и состояния навыков.
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

// Обнулить урон записей «цель-персонаж = сам кастер» (см. шапку ключа выше). Возвращает число исправленных записей.
static int32 AsdaSanitizeSelfHitRecs(int32 CasterUidx, int32 SkillCode, TArray<UAsdaNetSubsystem::FAsdaSkillCastRec>& Recs, int32 Opcode)
{
	if (GAsdaSkillSelfHitNoDamage == 0) { return 0; }
	int32 Fixed = 0;
	for (UAsdaNetSubsystem::FAsdaSkillCastRec& Rec : Recs)
	{
		if (Rec.TargetKind != 1 || Rec.TargetIdx != CasterUidx || Rec.Damage == 0) { continue; }
		UE_LOG(LogAsdaNet, Log, TEXT("%d: навык %d кастера uidx=%d — запись «цель = сам кастер» с уроном %d (флаги 0x%02X) разобрана как само-бафф: урон 0, флаги 0x%02X [ИСПРАВЛЕНИЕ 05.11]"),
			Opcode, SkillCode, CasterUidx, Rec.Damage, Rec.HitFlags, Rec.HitFlags & 0x03);
		ASDA_CTRACE_UID(CasterUidx, EAsdaCT::Recv, TEXT("ПРИЁМ %d: навык %d — урон %d по самому кастеру uidx=%d снят (само-бафф, HP только от сервера)"),
			Opcode, SkillCode, Rec.Damage, CasterUidx);
		Rec.Damage = 0;
		Rec.HitFlags &= 0x03;
		++Fixed;
	}
	return Fixed;
}
#if ASDA_COMBAT_TRACE
// Журнал боя: есть ли в батче 4013 (записи по 173 б, rid — i16 @0 записи) хоть один моб НАШЕГО боя (IsEngaged).
static bool AsdaCtAnyEngaged173(const TArray<uint8>& Raw)
{
	for (int32 P = 0; P + 2 <= Raw.Num(); P += 173)
	{
		if (AsdaCombatTrace::IsEngaged(AsdaBytes::RdI16(Raw.GetData() + P))) { return true; }
	}
	return false;
}
#endif

// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (mount-travel): снимок 6154 без записей type 2 снимает транспорт ТОЛЬКО у соседей, как ветка 5418/6155
//   ниже (`!AsdaSelf::IsSelfUserIdx`). Снятие на пустом снимке — наше расширение: эталон `charBuff_ApplyPMBuffFromPacket` в этих
//   снимках только добавляет. Сервер (`sub_422AF0@0x422AF0`, вызовы из `Send_MoveSectorInClippingUserInfo`) шлёт 6154 про игрока
//   соседям, а не ему самому (живые журналы 28.10–05.11: 119 приёмов 6154, про себя — ни одного), поэтому в игре дыра не
//   срабатывала; но снимок про себя (или с 28 записями навыков, где транспорт срезан лимитом `v159 < 28`) спешил бы свою модель,
//   оставив состояние и скорость транспорта. Свой транспорт снимают только 5454, смерть и позы (как эталон).
static ASDA_DEV_CVAR(int32, CVarMountSnapshotKeepsSelf,
	TEXT("asda.Mount.SnapshotKeepsSelf"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 05.11.2026] 1 = пустой по транспорту снимок 6154 снимает транспорт только у соседей (как 5418); ")
	TEXT("0 = прежнее (снимал и у своего персонажа)."),
	ECVF_Default);

// ★ 26.10.2026 (bowfx) [ОРИГИНАЛ] ключ цепочки состояний навыка на мобе в 4102 (разбор у ветки сущности 4102):
//   1 — как эталон (buff0/1/2 записи навыка; отсев 181/182 обрывает разбор пакета); 0 — прежний порт (номер навыка как состояние).
static ASDA_DEV_CVAR(int32, CVarMobStateChain, TEXT("asda.Skill.MobStateChain"), 1,
	TEXT("4102 по мобу: 1 = как оригинал (состояния buff0/1/2 записи навыка), 0 = прежний порт (код навыка как id состояния)"),
	ECVF_Default);
static bool AsdaMobStateChainOn() { return CVarMobStateChain.GetValueOnGameThread() != 0; }   // аксессор ключа (имя ключа в рабочем коде не ищется)

// ★ R2 (10.11.2026): приёмник области «World» — сущности мира: мобы/деспавн, квесты, удары 4015/4017, крылья, портал 5055, скорость 6071, усилители, эффекты навыков/баффы, ивенты, GM-состояние, питомцы, касты навыков; движение 4007/4009 и состояния навыков.
//   Группы `case` перенесены из `UAsdaNetSubsystem::HandlePacket` ДОСЛОВНО и в исходном порядке; `bHandled` = false только
//   в служебной ветке `default:` в конце — опкод не из этой области, диспетчер идёт дальше.
void UAsdaNetSubsystem::HandlePacket_World(const Asda::ParsedPacket& Pkt, bool& bHandled)
{
	bHandled = true;
	switch ((NetOpcode)Pkt.Opcode)
	{
	case OP_DISCONNECT_POPUP: // 4067 0xFE3 — ★ МИСНОМЕР enum: батч NPC (записи 108б; хендлер общий с 8402)
	{
		TArray<uint8> Raw = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("NPC-батч 4067: %d б (~%d записей по 108) — adoption"), Raw.Num(), Raw.Num() / 108);
		PendingNpcBatches.Push(MoveTemp(Raw));   // ★ A2 (аудит №4): общая точка TAsdaNetQueue
		OnEnvBatch.Broadcast();
		break;
	}
	case OP_RECV_PACKET_MOB_SPAWN: // 4013 0xFAD — БАТЧ мобов. Сервер пакует записи по 173 б вплотную
	{                              //   (Send_SectorInNewAddMonsterInfo: `memcpy(&Src[v15],rec,0xADu); v15+=173`),
	                               //   но ЭТАЛОН (наша IDB-сборка 13 344 768 б, юзер играет ИМЕННО ей) шагает
	                               //   по 174 (0x96CC74 `add eax,0AEh`) + гейт *(u32)(rec+27) ⇒ применяется
	                               //   ТОЛЬКО ПЕРВАЯ запись каждой пачки (КРУГ 9) — разбор в AsdaWorldSubsystem.
		TArray<uint8> Raw = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("Моб-батч 4013: %d б (сервер уложил %d записей по 173) — adoption"), Raw.Num(), Raw.Num() / 173);
		// Журнал боя: появление мобов. Эталон применяет ТОЛЬКО первую запись пачки (шаг 174, гейт @27) — печатаем её поля.
		//   Канал общий (весь сектор): уровень 1 — только если в пачке есть моб нашего боя.
		ASDA_CTRACE_IF(AsdaCtAnyEngaged173(Raw), EAsdaCT::Recv, TEXT("ПРИЁМ 4013 появление мобов: %d б (%d записей по 173); запись 0: rid=%d тип=%d действие@8=%d HP=%d тайл=(%d,%d)→(%d,%d) вкл@27=%d"),
			Raw.Num(), Raw.Num() / 173,
			Raw.Num() >= 2  ? AsdaBytes::RdI16(Raw.GetData() + 0) : -1,
			Raw.Num() >= 4  ? (int32)AsdaBytes::RdU16(Raw.GetData() + 2) : -1,
			Raw.Num() >= 9  ? (int32)Raw[8] : -1,
			Raw.Num() >= 13 ? AsdaBytes::RdI32(Raw.GetData() + 9) : -1,
			Raw.Num() >= 21 ? AsdaBytes::RdI16(Raw.GetData() + 19) : 0, Raw.Num() >= 23 ? AsdaBytes::RdI16(Raw.GetData() + 21) : 0,
			Raw.Num() >= 25 ? AsdaBytes::RdI16(Raw.GetData() + 23) : 0, Raw.Num() >= 27 ? AsdaBytes::RdI16(Raw.GetData() + 25) : 0,
			Raw.Num() >= 31 ? AsdaBytes::RdI32(Raw.GetData() + 27) : 0);
		PendingMobBatches.Push(MoveTemp(Raw));   // ★ A2 (аудит №4): общая точка TAsdaNetQueue
		OnEnvBatch.Broadcast();
		break;
	}
	case OP_C_QUEST_RECV_PACKET: // 5044 0x13B4 — ответ на 5043 «принять / сдать шаг квеста»
	{
		// Эталон `CQuest_RecvPacket@0xD9D260`: тело 0x2F, `resultCode = body[0]`,
		//   при 0 — переход диалога на страницу `i16 body+1`, иначе тултип по коду отказа.
		//   Разбор кодов и вся реакция окна — в `UAsdaNpcDialogSubsystem` (закон
		//   `no-reentrant-net-callbacks`: здесь только разбор байт и делегат).
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		const int32 Result = (B.size() >= 1) ? (int32)B[0] : -1;
		const int32 TalkId = (B.size() >= 3) ? AsdaBytes::RdI16(B.data() + 1) : 0;   // ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`
		UE_LOG(LogAsdaNet, Log, TEXT("←5044 результат шага квеста: код=%d talkId=%d (тело %d б)"),
			Result, TalkId, (int32)B.size());
		OnQuestStepResult.Broadcast(Result, TalkId, Body);
		break;
	}
	case OP_QUEST_REWARD_PROCESS_RESULT: // 5042 0x13B2 — ответ на 5041 «сдать шаг квеста»
	{
		// ★ Отправитель — `GameServer!recvRewardQuestProcess@0x45CF90`, тело РОВНО 501 б:
		//   +0  u8  код шага (`HIBYTE(v16[6])`: 0 — обычный шаг; ненулевой — отказ/особый случай,
		//           сервер в этой ветке выходит сразу, ничего не начисляя);
		//   +1  u16 номер квеста;      +3  u16 стадия;
		//   +5  u8  тип завершения (2 = квест ЗАВЕРШЁН целиком, иначе просто сдвиг стадии);
		//   +6  u16 talkId — страница диалога, на которую перейти;
		//   +8  u16 код выбранной награды;
		//   +10 … блок наград/предметов, внутри которого по +15 лежит обновлённая 44-байтовая
		//           запись слота журнала (`Src`, та же раскладка, что в 5047).
		//   Закон `no-reentrant-net-callbacks`: здесь только разбор байт и делегат.
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		auto U16 = [&B](size_t O) { return (B.size() >= O + 2) ? AsdaBytes::RdI16(B.data() + O) : -1; };   // ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`; короткое — −1
		const int32 StepCode  = (B.size() >= 1) ? (int32)B[0] : -1;
		const int32 QuestNo   = U16(1);
		const int32 Stage     = U16(3);
		const int32 FinishTyp = (B.size() >= 6) ? (int32)B[5] : -1;
		const int32 TalkId    = U16(6);
		const int32 RewardId  = U16(8);
		UE_LOG(LogAsdaNet, Log,
			TEXT("←5042 результат сдачи: код=%d квест=%d стадия=%d тип завершения=%d talkId=%d награда=%d (тело %d б)"),
			StepCode, QuestNo, Stage, FinishTyp, TalkId, RewardId, (int32)B.size());
		// ★ 18.10.2026: профессия/класс/очки — РАНЬШЕ подписчиков 5042 (эталон: `case 0` начинается с ветки
		//   `jobChangeFlag`, квестовые записи и `npcUiRefreshActionList` идут после неё).
		if (StepCode == 0 && IsQuestJobChangeApplyOn()) { ApplyQuestTurnInCharBlock(B); }
		OnQuestTurnInResult.Broadcast(StepCode, QuestNo, Stage, FinishTyp, TalkId, Body);
		break;
	}
	case OP_QUEST_ITEM_RESULT_APPLY: // 5052 0x13BC — ПРОГРЕСС ЦЕЛЕЙ КВЕСТА (`questItemResult_apply@0xC63010`)
	{
		// ★★★ Раньше этого разбора не было вовсе, и пакет уходил в `default:` — счётчики целей в
		//   журнале не двигались никогда. Эталон копирует 0x1FC = 508 байт тела; принимаем и
		//   более короткое тело (лог предупредит), чтобы не терять пакет молча.
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		if (B.size() < 508)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("←5052 прогресс квеста: тело %d б (эталон читает 508)"), (int32)B.size());
		}
		else
		{
			UE_LOG(LogAsdaNet, Log, TEXT("←5052 прогресс квеста: тело %d б"), (int32)B.size());
		}
		OnQuestProgress.Broadcast(Body);
		break;
	}
	case OP_C_QUEST_ON_RECV_STATE_SYNC: // 5047 0x13B7 — состояние квестов игрока (688 б; см. шапку делегата)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() >= 688)
		{
			TArray<uint8> Blob; Blob.SetNumUninitialized(688);
			FMemory::Memcpy(Blob.GetData(), B.data(), 688);
			UE_LOG(LogAsdaNet, Log, TEXT("Квест-состояние 5047: %d б тела (используем 688)"), (int32)B.size());
			OnQuestStateSync.Broadcast(Blob);
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("Квест-состояние 5047: тело %d б < 688 — пропуск"), (int32)B.size());
		}
		break;
	}
	case OP_RECV_PACKET_LOG_OUT_DESPAWN: // 4011 0xFAB — деспавн сущности (renderId i16 @0 тела)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 2, (int32)Pkt.Opcode))
		{
			const int32 Rid = AsdaBytes::RdI16(&B[0]);
			UE_LOG(LogAsdaNet, Log, TEXT("Деспавн 4011: renderId=%d"), Rid);
			ASDA_CTRACE_IF(AsdaCombatTrace::IsEngaged(Rid) || AsdaCombatTrace::IsUserRelevant(Rid),
				EAsdaCT::Recv, TEXT("ПРИЁМ 4011 деспавн сущности rid/userIdx=%d%s"), Rid, IsSelfUserIdx(Rid) ? TEXT(" (это мой номер)") : TEXT(""));
			// ★ 18.10.2026 (ревизия Ф4, мелочь 6) [ОРИГИНАЛ]: 4011 удаляет объект персонажа (`CharManager`), а с ним его поля режима
			//   `[80]/[88]/[92]/[116]/[160]` — запись режима ушедшего соседа снимается (номер сессии сервер переиспользует).
			if (!IsSelfUserIdx(Rid) && PvpStates.Remove(Rid) > 0) { UE_LOG(LogAsdaNet, Log, TEXT("4011: PvP-режим userIdx=%d снят"), Rid); }
			OnEntityDespawn.Broadcast(Rid);
		}
		break;
	}
	case OP_COMBAT_HIT: // 4015 0xFAF — экшен сущности (Recv_CombatHit@0xF310E0): kind 1=стоп, 2=движение, 3=удар
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 31, (int32)Pkt.Opcode))
		{
			auto I16 = [&B](int32 O) { return AsdaBytes::RdI16(&B[O]); };
			auto I32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
			FAsdaCombatHit H;
			H.AttackerUserIdx = I16(0);
			H.TargetRid   = I16(4);
			H.Kind        = B[6];
			H.CurTile     = FIntPoint(I16(7),  I16(9));
			H.DstTile     = FIntPoint(I16(11), I16(13));
			H.Speed       = 100.f / (float)FMath::Max(1, I16(15));   // эталон: 100.0/i16@+15
			H.Damage      = I32(21);
			H.NewHp       = I32(25);
			H.HitVariant  = B[29];
			H.HitType     = B[30];
			// ★ Темпы клипов (эталон: i16 × flt_13EC278, flt_13EC278 = 1e-4 по байтам бинаря).
			//   0 в поле = пакет темп не несёт (kind=1 шлёт нули) ⇒ оставляем 1.0, иначе клип замрёт.
			{
				const int32 RawMove = I16(17);
				const int32 RawAtk  = I16(19);
				if (RawMove > 0) { H.MoveAnimRate = (float)RawMove * 1e-4f; }
				if (RawAtk  > 0) { H.AtkAnimRate  = (float)RawAtk  * 1e-4f; }
			}
			// ★★★ ТИП МОБА (@2, i16 ЗНАКОВО). Эталон `Recv_CombatHit@0xF310E0` читает его ТОЛЬКО в
			//   ветке «сущность не найдена» (game.c:548195/:548204 `mobTableFindIndexById(g_pMobMgr,
			//   pPacket[1])`), но читает как `__int16` — той же ширины и знака, что renderId рядом.
			//   Разбираем всегда: ветка спауна на лету в мире-подписчике решает сама.
			H.MobType = I16(2);
			// ★★★ ОТРАЖЕНИЕ УРОНА (@31 i16 знаково + @33 i32 знаково). Тело 4015 = 37 б
			//   (GameServer.c:17264 `Send_SectorInUpdateMonster_NPC_Info(…, 4015, 37, …)`), и эталон
			//   читает оба поля без всякой проверки длины. Гейт всего разбора оставлен на 31 б
			//   СОЗНАТЕЛЬНО: поднять его до 37 значило бы молча ронять весь пакет там, где раньше
			//   работали движение и урон. Короткое тело не молчит — пишем предупреждение (закон
			//   «нет» = ошибка метода: немой отброс неотличим от «пакет не пришёл»).
			if (B.size() >= 37)
			{
				H.ReflectSkillId = I16(31);
				H.ReflectDamage  = I32(33);
			}
			else
			{
				UE_LOG(LogAsdaNet, Warning,
					TEXT("4015: тело %d б < 37 — поля отражения @31/@33 отсутствуют (эталон всегда шлёт 37 б)"),
					(int32)B.size());
			}
			UE_LOG(LogAsdaNet, Verbose, TEXT("4015: atk=%d tgt=%d type=%d kind=%d cur=(%d,%d) dst=(%d,%d) dmg=%d newHp=%d ht=%d refl=0x%04X/%d"),
				H.AttackerUserIdx, H.TargetRid, H.MobType, H.Kind, H.CurTile.X, H.CurTile.Y, H.DstTile.X, H.DstTile.Y,
				H.Damage, H.NewHp, H.HitType, (uint16)H.ReflectSkillId, H.ReflectDamage);
			// Журнал боя: kind 3 (удар моба по игроку) и любые записи с уроном/отражением — событие; ход/стоп — только уровень 2.
			if (H.Kind == 3 || H.Damage != 0 || H.ReflectDamage != 0)
			{
#if ASDA_COMBAT_TRACE
				// Участники нашего боя (до строки): моб бьёт МЕНЯ → моб наш; наш моб бьёт другого игрока → игрок наш.
				if (IsSelfUserIdx(H.AttackerUserIdx)) { AsdaCombatTrace::NoteEngaged(H.TargetRid); }
				else if (H.AttackerUserIdx >= 0 && AsdaCombatTrace::IsEngaged(H.TargetRid)) { AsdaCombatTrace::NoteEngagedUser(H.AttackerUserIdx); }
#endif
				// Канал общий (весь сектор): уровень 1 — если жертва наша (я/участник) или моб нашего боя.
				ASDA_CTRACE_IF(AsdaCombatTrace::IsUserRelevant(H.AttackerUserIdx) || AsdaCombatTrace::IsEngaged(H.TargetRid),
					H.Kind == 3 ? EAsdaCT::MobAttack : EAsdaCT::Recv,
					TEXT("ПРИЁМ 4015 kind=%d%s: моб rid=%d (тип %d) → игрок uidx=%d%s урон=%s HP игрока→%d вариант замаха=%d тип удара=%d темп замаха=%.2f тайл=(%d,%d)→(%d,%d) отражение навык=0x%04X урон=%d"),
					(int32)H.Kind, H.Kind == 3 ? TEXT(" удар") : (H.Kind == 5 || H.Kind == 7) ? TEXT(" каст") : TEXT(""),
					H.TargetRid, H.MobType, H.AttackerUserIdx, IsSelfUserIdx(H.AttackerUserIdx) ? TEXT(" (Я)") : TEXT(""),
					*AsdaCtDmg(H.Damage), H.NewHp, (int32)H.HitVariant, (int32)H.HitType, H.AtkAnimRate,
					H.CurTile.X, H.CurTile.Y, H.DstTile.X, H.DstTile.Y, (int32)(uint16)H.ReflectSkillId, H.ReflectDamage);
			}
			else
			{
				ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("ПРИЁМ 4015 kind=%d%s: rid=%d (тип %d) uidx@0=%d тайл=(%d,%d)→(%d,%d) скорость=%.3f HP@25=%d"),
					(int32)H.Kind, H.Kind == 1 ? TEXT(" стоп") : H.Kind == 2 ? TEXT(" движение") : TEXT(""),
					H.TargetRid, H.MobType, H.AttackerUserIdx, H.CurTile.X, H.CurTile.Y, H.DstTile.X, H.DstTile.Y, H.Speed, H.NewHp);
			}
			OnCombatHit.Broadcast(H);
		}
		break;
	}
	case OP_MOB_SPAWN_LOOP: // 4019 0xFB3 — ОДИНОЧНЫЙ моб-апдейт (Recv_MobSpawnLoop@0xF30330): та же раскладка,
	{                       //   что запись 4013, но для ЗАГРУЖЕННОГО моба (entity[116]==23) позиция из пакета НЕ ставится —
	                        //   только HP@9 + траектория к dst@23/25; not found → полный спавн (RecvPacket_MobSpawn).
	                        //   ★ R5-fix3 (13.11.2026, уточнение по декомпилу): поле [116] (+0x1D0) — `loadSubState`, стадия покадровой
	                        //   загрузки ресурсов моба (23 = загружен), а не «категория моб»: ещё не загруженного (невидимого) моба
	                        //   эталон ставит в cur пакета (network.c:37063). В порту моб готов сразу — ветка «не ставить» единственная.
	                        //   ★ Тело = 173б записи + СЛУЧАЙНЫЙ ПАД 0..19б (сервер sub_5643E0, a8≠0): живые
	                        //   размеры 173..192. У 4013 пада нет (a8=0) → строго 173·N.
		if (NetNeedLen(Pkt.Body, 169, (int32)Pkt.Opcode))
		{
			TArray<uint8> Raw = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
			// Журнал боя: одиночный апдейт моба (частый) — поля записи 4013: @0 rid, @2 тип, @8 действие, @9 HP, @23/@25 dst.
			ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("ПРИЁМ 4019 апдейт моба: rid=%d тип=%d действие@8=%d HP=%d → dst=(%d,%d) (%d б)"),
				AsdaBytes::RdI16(Raw.GetData() + 0), (int32)AsdaBytes::RdU16(Raw.GetData() + 2), (int32)Raw[8],
				AsdaBytes::RdI32(Raw.GetData() + 9), AsdaBytes::RdI16(Raw.GetData() + 23), AsdaBytes::RdI16(Raw.GetData() + 25), Raw.Num());
			PendingMobUpdates.Push(MoveTemp(Raw));   // ★ A2: общая точка TAsdaNetQueue; ОТДЕЛЬНАЯ очередь — семантика Recv_MobSpawnLoop
			OnEnvBatch.Broadcast();
		}
		break;
	}
	case OP_COMBAT_HIT_AUX: // 4017 0xFB1 — Recv_CombatHitAux@0xF33B60: СМЕРТЬ/HP/снап моба.
	{
		// Раскладка: @0 i16 mobType, @2 i16 renderId, @4 u32 FLAGS:
		//   0      → СМЕРТЬ (эталон: state≠10 → SetActionState(10), HP=0) — ОСНОВНОЙ канал смерти моба;
		//   бит2 (4) → спавн/снап: тайлы i16 @96/@98, HP i32 @92, state 1;
		//   бит1 (2) → только бафф-стеки (@8/@64);
		//   прочее → HP i32 @92 (гейт по state 1/2/7) + бафф-стеки.
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 8, (int32)Pkt.Opcode))
		{
			// ★ Аудит №3 (знаковость): эталон `Recv_CombatHitAux@0xF33B60` берёт тип моба ЗНАКОВО —
			//   `mobTableFindIndexById(g_pMobMgr, *(__int16 *)a3)`, ровно как renderId рядом
			//   (`*((__int16 *)a3 + 1)`). Порт читал @0 без знака — приведено к эталону.
			FAsdaCombatHitAux A;
			A.MobType = AsdaBytes::RdI16(&B[0]);   // @0 — тип моба (для ветки flags&4: спавн)
			A.Rid     = AsdaBytes::RdI16(&B[2]);
			A.Flags   = AsdaBytes::RdU32(&B[4]);
			// ★★★ СПИСОК АКТИВНЫХ СОСТОЯНИЙ МОБА: 28 × i16 id с @8 и 28 × u8 глубины стека с @64.
			//   Доказательство раскладки: клиент `Skill_Buff_RefreshStackDepth@0xF84090` (game.c:579325-579351)
			//   берёт id как `*(__int16 *)(a2 + 2*i)`, где `a2 = a3 + 2` (тело + 8 байт), а глубину как
			//   `*(_BYTE *)(i + a3)`, где второй указатель = тело + 64; сервер собирает те же поля
			//   `v7[5..32]` (id) и `v7[33..46]` (уровни) в `sub_425D40@0x425D40`.
			//   Гейт 92 б — ровно столько нужно на оба массива (8 + 56 + 28 = 92).
			if (NetNeedLen(B, 92, (int32)Pkt.Opcode))
			{
				A.BuffIds.SetNumUninitialized(28);
				A.BuffLevels.SetNumUninitialized(28);
				for (int32 i = 0; i < 28; ++i)
				{
					A.BuffIds[i]    = AsdaBytes::RdI16(&B[8 + 2 * i]);   // знаково, как у эталона (__int16)
					A.BuffLevels[i] = (int32)B[64 + i];                  // беззнаковый байт (rec+22)
				}
			}
			if (NetNeedLen(B, 100, (int32)Pkt.Opcode))
			{
				A.Hp   = AsdaBytes::RdI32(&B[92]);
				A.Tile = FIntPoint(AsdaBytes::RdI16(&B[96]),
				                   AsdaBytes::RdI16(&B[98]));
			}
			UE_LOG(LogAsdaNet, Log, TEXT("4017: rid=%d type=%d flags=0x%X hp=%d tile=(%d,%d) состояний=%d"),
				A.Rid, A.MobType, A.Flags, A.Hp, A.Tile.X, A.Tile.Y, A.BuffIds.Num());
#if ASDA_COMBAT_TRACE
			// Канал общий (все мобы сектора): уровень 1 — только моб нашего боя, остальные — уровень 2.
			if ((AsdaCombatTrace::IsOn(1) && (AsdaCombatTrace::IsOn(2) || AsdaCombatTrace::IsEngaged(A.Rid))))
			{
				// Активные состояния моба (id≥0) с глубиной стека — для этапа эффектов/дебаффов.
				FString Bf;
				for (int32 i = 0; i < A.BuffIds.Num(); ++i)
				{
					if (A.BuffIds[i] >= 0) { Bf += FString::Printf(TEXT("%s%d×%d"), Bf.IsEmpty() ? TEXT("") : TEXT(","), A.BuffIds[i], A.BuffLevels.IsValidIndex(i) ? A.BuffLevels[i] : 0); }
				}
				const TCHAR* Br = (A.Flags == 0) ? TEXT("СМЕРТЬ моба") : (A.Flags & 4) ? TEXT("спавн/снап") : (A.Flags & 2) ? TEXT("только стеки состояний") : TEXT("апдейт HP");
				ASDA_CTRACE_MOB(A.Rid, A.Flags == 0 ? EAsdaCT::Death : EAsdaCT::Recv,
					TEXT("ПРИЁМ 4017 %s: rid=%d тип=%d флаги=0x%X HP@92=%d тайл=(%d,%d) состояния=[%s]"),
					Br, A.Rid, A.MobType, A.Flags, A.Hp, A.Tile.X, A.Tile.Y, *Bf);
			}
#endif
			OnCombatHitAux.Broadcast(A);
		}
		break;
	}
	case OP_RECV_WING_DATA_TABLE_INSERT: // 6585 0x19B9 — КРЫЛЬЯ Plus Package (charId→wingId/flag)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 11, (int32)Pkt.Opcode))
		{
			const int32 CharId  = AsdaBytes::RdI32(&B[0]);
			const int32 WingId  = AsdaBytes::RdI16(&B[4]);
			const int32 Remain  = AsdaBytes::RdI32(&B[6]);
			const bool  bOn     = (B[10] != 0);
			UE_LOG(LogAsdaNet, Log, TEXT("6585 крылья: charId=%d wingId=%d остаток=%dс flag=%d"), CharId, WingId, Remain, bOn ? 1 : 0);
			OnWingState.Broadcast(CharId, WingId, Remain, bOn);
		}
		break;
	}
	// ★★★ 5055 0x13BF — ПЕРЕХОД ЧЕРЕЗ ПОРТАЛ (`Recv_PortalChannelChange@0xF45BD0`, `network.c:42956`).
	//
	//   Зоны порталов — ЧИСТО СЕРВЕРНЫЕ: клиент про них не знает вовсе. Сервер держит их в
	//   `GameServer\PortalIP_Info\<Карта>_PortalConfig.Ini` (39 зон на 20 картах; зона — прямоугольник
	//   `POSX..POSX+RANGEX / POSZ..POSZ+RANGEZ` в тайлах, RANGE — ШИРИНА, не радиус) и проверяет их
	//   `sub_52C4C0@0x52C4C0` из `MoveSyncProcess@0x5427D0` на КАЖДОЙ смене тайла. Наступил на зону —
	//   движение обрывается и прилетает этот пакет. Разбор: `Reports/map_triggers_re.md` разделы 2-3.
	//
	//   Тело 53 б: код@+0, IP char[20]@+1, порт u16@+21, карта-цель u16@+23, тайл прибытия u16@+25/+27,
	//   состояние кампвара @+51/+52. Коды: 1 — переход; 0/2/8/12 — отказ с нотисом 1096/1097/13916/2231;
	//   13..20 — отказ битвы армий 7101..7108; 3..7/9..11 — окна инстанс-подземелья (ждут своей системы);
	//   21+ — эталон молча отбрасывает. Полный разбор — `Reports/owner_0927/port_5055.md`.
	//
	//   ★ Сам переход — та же дорога, что у 4055 (смена карты после воскрешения): запоминаем адрес
	//   канала целевой карты и тайл прибытия, поднимаем `bPortalPending`, просим отложенный реконнект.
	//   После загрузки уровня Load Manager пошлёт 5058 `CharPortalLogonReq`, а НЕ обычный 4001 —
	//   иначе сервер останется на старой карте. Реконнект строго отложенный: рвать сеть прямо из
	//   колбэка нельзя (закон `no-reentrant-net-callbacks`).
	case OP_PORTAL_CHANNEL_CHANGE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 1, (int32)Pkt.Opcode)) { break; }
		const uint8 Code = B[0];

		// ★★★ ТОКЕНЫ КАМПВАРА (+51/+52) — забираем ДО разбора кода: эталон зовёт оба сеттера первыми
		//   строками, для ЛЮБОГО кода, включая отказы (`sub_926322(g_pCampWarClan, pkt[52])` → `CCampWar+0x2304`,
		//   `sub_92B066(g_pCampWarClan, pkt[51])` → `+0x2308`; `network.c:42993-42994`). Клиент их не разбирает, а
		//   возвращает эхом в enter-хендшейк 20000 (payload[25]/[26]; единственные читатели — геттеры
		//   `get_o8C1/get_o8C2` в `netSendEnterWorldHandshake`/`netSendEnterGameHandshake`, `network.c:3035/3904`).
		//   ★ 27.09.2026: флаг боевого HUD здесь БОЛЬШЕ НЕ ставится. Бит 0x80 эталон читает только на ветке
		//   перехода (код 1, `network.c:43118`), см. ниже. Сервер кладёт 0xFF в +51/+52 каждого стокового отказа
		//   (буфер `[ebp-0x3C]` из `sub_40DE70`) — прежняя редакция от этого поднимала подавление HUD на отказе.
		if (B.size() >= 53)
		{
			CampWarToken1 = B[51];
			CampWarToken2 = B[52];
			UE_LOG(LogAsdaNet, Verbose, TEXT("[портал] кампвар-токены %02X %02X (эхо в 20000)"),
				CampWarToken1, CampWarToken2);
		}
		// `Net_RecvUiTick()` эталона (`network.c:42995`) — пустая функция (`return 0`, `@0x953C10`), переносить нечего.

		if (Code != 1)
		{
			// Отказы 0/2/8/12 и битва армий 13…20 — общая точка с 6201 (каналы и id — по эталону, см. её шапку).
			if (PostPortalRefusalNotice(GetGameInstance(), 5055, Code, /*bRoseWarRange=*/true)) { break; }
			// 3…7 и 9…11 — окна инстанс-подземелья (реестр отложенного № 94, разбор `Reports/indun_campwar_channel.md` §1):
			//   3 → окно ожидания 15700 (+ g_nPickIndexA/B из +31/+33, этаж +29, карта +23);
			//   4/5/6/9/10 → окно входа 15730, текст 13900/13901/13902/13925/13902 + имя карты +23, этаж +29;
			//   7 → окно условий 15750 (нотис 13915, предметы +35/+39/+43/+47);
			//   11 → окно 15730 в состоянии «возврат в брошенную игру» (13926), ответ — 5113.
			//   Нотисов В ЧАТ у этих кодов эталон не даёт — текст живёт только в окнах. Системы подземелий в порту
			//   нет, на живом сервере ветка мертва (`INDUN = 0` во всех `*_PortalConfig.Ini`). Молча не глотаем — лог.
			//   Коды 21+ эталон молча отбрасывает (`return 0`).
			if ((Code >= 3 && Code <= 7) || (Code >= 9 && Code <= 11))
			{
				UE_LOG(LogAsdaNet, Warning,
					TEXT("[портал] 5055 код=%u — окно инстанс-подземелья (15700/15730/15750), системы подземелий в порту нет (реестр № 94)"),
					(uint32)Code);
			}
			else
			{
				UE_LOG(LogAsdaNet, Log, TEXT("[портал] 5055 код=%u — эталон такие молча отбрасывает"), (uint32)Code);
			}
			break;
		}

		if (!NetNeedLen(B, 29, (int32)Pkt.Opcode)) { break; }
		// IP — char[20], дополнен нулями.
		const FString NewIp   = AsdaBytes::AnsiFieldTrim(&B[1], 20);   // N7: общая точка
		const int32   NewPort = (int32)AsdaBytes::RdU16(&B[21]);
		const int32   NewMap  = (int32)AsdaBytes::RdU16(&B[23]);
		const int32   TX      = (int32)AsdaBytes::RdU16(&B[25]);
		const int32   TZ      = (int32)AsdaBytes::RdU16(&B[27]);

		// ★★★ БОЕВОЙ HUD КАМПВАРА — только на переходе (27.09.2026). Эталон, конец ветки кода 1 (`network.c:43116-43129`):
		//   `if (byte_2C00C80) { if ((pkt[51] & 0x80) || карта-цель == 23) ToggleVisible(0, …); else { ToggleVisible(1, …);
		//   uiCtrlHideAtField374(); } … }`. `byte_2C00C80` = `CCampWar+0x26C0` «контролы созданы» (ставит
		//   `CCampWar_InitControls`), в игре он поднят. Итог для порта — флаг «HUD не показывать» = бит 0x80 у +51
		//   ИЛИ карта 23; иначе HUD разрешён. Окна кампвара в порту ещё нет (реестр № 95) — флаг ждёт его.
		//   Токены без 53 байт тела эталон всё равно читает (буфер у него всегда 53 б) — у нас короткое тело оставляет
		//   прежние токены, флаг считаем от них.
		bCampWarHudSuppressed = ((CampWarToken1 & 0x80) != 0) || (NewMap == 23);
		UE_LOG(LogAsdaNet, Verbose, TEXT("[портал] 5055 переход: боевой HUD кампвара %s (+51=%02X, карта %d)"),
			bCampWarHudSuppressed ? TEXT("подавлен") : TEXT("разрешён"), CampWarToken1, NewMap);
		// ★ 30.10.2026 (CampWar целиком): подсистема «Битвы армий» — HUD разрешён → скрыть 49530 (`uiCtrlHideAtField374`);
		//   уходим С карты кампвара → `CCampWar_ResetState` (оба шага эталона из той же ветки `network.c:43116-43129`).
		if (UAsdaCampWarSubsystem* CampWar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr)
		{
			CampWar->OnPortalTransfer5055(!bCampWarHudSuppressed, NewMap);   // ★ 30.10.2026: + карта-цель (a3 = её +69)
		}

		UE_LOG(LogAsdaNet, Warning,
			TEXT("[портал] 5055: карта=%d канал=%s:%d прибытие=(%d,%d)%s"),
			NewMap, NewIp.IsEmpty() ? TEXT("(тот же)") : *NewIp, NewPort, TX, TZ,
			(NewMap == EnterMapId) ? TEXT(" — та же карта, только позиция") : TEXT(""));

		SelfTileX = TX; SelfTileZ = TZ;
		if (NewMap == EnterMapId)
		{
			// Портал внутри одной карты: перекладываем персонажа, соединение не рвём.
			// ★ 22.10.2026: эталон `Recv_PortalChannelChange@0xF45BD0` при коде 1 ВСЕГДА грузит карту (network.c:43075-43080,
			//   `CMap_LoadLoadingScreen` + `g_nGameFlags |= 4`) — её шаги 0/11 закрывают свой список окон и здесь.
			AsdaEntryWindows::ApplyMapLoadWindowCloses(GetGameInstance(), TEXT("портал 5055 на ту же карту"), /*bLevelReload*/ false);
			OnSelfWarp.Broadcast(-1, TX, TZ);
			break;
		}

		FlushHotbarBeforeTransfer(TEXT("портал 5055"));
		// ★ Остановить автопуть и текущее движение: во время перехода игрок ещё не опознан на новом
		//   соединении, и любой пакет движения обрывает связь (см. гейт в SendSecured). Автопуть
		//   к тому же продолжал бы вести к порталу уже на новой карте.
		StopTravelAndMovementForTransfer(TEXT("портал 5055"));
		bTransferInProgress = true;
		EnterMapId  = NewMap;
		bPortalPending = true;
		PortalTileX = TX; PortalTileZ = TZ;
		ApplyGameServerFromPacket(NewIp, NewPort, TEXT("5055"));   // ★ 16.10.2026: непубличный адрес → домен, порт из пакета
		bInGameConnected   = false;
		bPendingEnterWorld = true;
		if (Conn) { Conn->RequestStopDrain(); }
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[портал] реконнект отложен до конца тика: %s:%d, карта=%d, после загрузки → 5058 тайл=(%d,%d)"),
			*GameServerIP, GameServerPort, NewMap, TX, TZ);
		break;
	}
	// ★★★ 5059 0x13C3 — РАСКЛАДКА КВИКСЛОТОВ, ОДНА СТРАНИЦА (`Hotbar_LoadPage@0xDEA3D0`,
	//   вызов из network.c:1941 с телом от +9). 131 б = u8 page + 13 записей по 10 б.
	//   Ни MD5, ни токена в S→C нет. Сюда же приходят страницы 0..5, где 5 = «T»-ряд (слоты 65..77).
	//   Подписчик (UAsdaHotbarSubsystem) ОБЯЗАН только скопировать данные: закон
	//   `no-reentrant-net-callbacks` — рвать сеть/дёргать UI из колбэка нельзя.
	case OP_HOTBAR_LOAD_PAGE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 1)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("5059 квикслоты: пустое тело"));
			break;
		}
		const uint8 Page = B[0];
		TArray<uint8> Slots;
		const int32 Avail = ParseQuickSlotPage10(B.data(), (int32)B.size(), Slots);
		if (Avail < kQuickSlotPageBytes)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("5059 квикслоты: тело короче эталонных %d б (%d) — хвост занулён"),
				kQuickSlotLayoutBytes, (int32)B.size());
		}
		UE_LOG(LogAsdaNet, Log, TEXT("5059 квикслоты: страница %u, записей %d (тело %d б)"),
			Page, kQuickSlotCols, (int32)B.size());
		OnHotbarPage.Broadcast(Page, Slots);
		break;
	}
	case OP_CHAR_MOVE_TO_CELL: // 6071 0x17B7 — НОВАЯ СКОРОСТЬ движения (в т.ч. посадка/высадка с транспорта)
	{
		// ★★★ 16.09.2026, ПЕРЕПИСАНО. Прежняя версия читала скорость с +2 — там лежит charId, то есть в
		//   скорость попадал мусор; длину проверяла как 6 при фактических 10; не отличала свой пакет от
		//   чужого (чужая посадка на маунт перетирала МОЮ скорость — тот же дефект, что чинили в 4007
		//   и 4062) и не воспроизводила смысл пакета.
		//
		//   РАСКЛАДКА ТЕЛА — РОВНО 10 БАЙТ (сервер: `push 0Ah`), доказано с двух сторон:
		//     +0 i16 userNum | +2 i32 charId (эталон здесь его не читает) | +6 f32 speed (юнит/мс)
		//   `Recv_CharMoveToCell@0xF3CC90` передаёт в `charMoveStepToCell` девятым аргументом
		//   `*(_DWORD *)(a2 + 3)`, где a2 — `__int16 *` ⇒ байт +6; тот же девятый аргумент в клик-муве
		//   (game.c:83907) — поле 52130, то есть скорость.
		//
		//   ЧТО ДЕЛАЕТ ЭТАЛОН: ищет персонажа `CharManager_FindByUserIdx` — БЕЗ различения свой/чужой —
		//   и, если тот идёт, пересчитывает ТЕКУЩИЙ ШАГ с новой скоростью. Поэтому пакет раздаётся
		//   слушателю как есть, а решение принимает мир (см. `OnNetEntitySpeed`).
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 10, (int32)Pkt.Opcode))
		{
			int16 Uidx = (int16)AsdaBytes::RdI16(B.data() + 0);   // ЗНАКОВО: ключ тот же, что у 4007/4062 (★ N6: `AsdaBytes`)
			float Spd = AsdaBytes::RdF32(B.data() + 6);
			// ★ СОЗНАТЕЛЬНОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (не выдавать за 1:1). В оригинале обработчик 6071
			//   в кэш скорости НЕ ПИШЕТ вовсе: единственная игровая запись поля 52130 — в 4062
			//   (`game.c:551913`), а `Recv_CharMoveToCell` правит только скорость текущего шага.
			//   ПРИЧИНА НАШЕГО ОТСТУПЛЕНИЯ — предиктивная модель порта: мы начинаем движение локально,
			//   не дожидаясь 4007, поэтому после посадки на транспорт СТОЯ первый же шаг по WASD ушёл бы
			//   со старой скоростью, и сервер (он считает шаги как расстояние/скорость) копил бы
			//   расхождение. Оригинал так не делает потому, что вообще не двигается до ответа сервера.
			//   ТОЛЬКО ДЛЯ СВОЕГО: `LastServerSpeed` кормит моё движение, и чужой маунт его менять не должен.
			//   Верхняя граница фильтра — 5.0, как у 4007/4062 (серверный кламп скорости 0.66).
			const bool bSelf = AsdaSelf::IsSelfUserIdx(this, (int32)Uidx);
			if (bSelf && Spd > 0.0001f && Spd < 5.f) { LastServerSpeed = Spd; }
			UE_LOG(LogAsdaNet, Log, TEXT("←6071 новая скорость: uidx=%d speed=%.4f юнит/мс%s"),
				(int32)Uidx, Spd, bSelf ? TEXT(" [СВОЙ]") : TEXT(""));
			OnEntitySpeed.Broadcast((int32)Uidx, Spd);
		}
		break;
	}
	case OP_RECV_ITEM_APPLY_PM_BUFF: // 5453 0x154D — ПРЕДМЕТНЫЙ УСИЛИТЕЛЬ (PM Buff) + частный случай «посадка»
	{
		// Эталон charBuff_ApplyPMBuffFromPacket@0xF26110 (entity_char.c:52226), тело 10 б:
		//   i16 userIdx@0, i32 itemCode@2, i32 durationSec@6.
		//   rec = Table2(itemCode);  cCharStatusAddTypeMount(char, rec->subType, duration, rec->code)
		//   ⇒ id записи в списке эффектов = SpecialItemRecord.subType, НЕ itemCode.
		//   subType == 15 → ещё и посадка на транспорт (уже реализована в порту, НЕ трогаем).
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 6, (int32)Pkt.Opcode))
		{
			const int32 Uidx = AsdaBytes::RdI16(&B[0]);
			const int32 ItemCode = AsdaBytes::RdI32(&B[2]);
			const int32 Dur = (NetNeedLen(B, 10, (int32)Pkt.Opcode))
				? AsdaBytes::RdI32(&B[6]) : 0;
			UE_LOG(LogAsdaNet, Log, TEXT("5453 предметный усилитель: userIdx=%d itemCode=%d длительность=%d с"), Uidx, ItemCode, Dur);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 5453 предметный усилитель ВКЛ: uidx=%d%s предмет=%d длительность=%d с"),
				Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), ItemCode, Dur);
			// ★ ВЕТКА ТРАНСПОРТА оставлена как была (мир сам отсеет не-маунт по SubType).
			OnMountState.Broadcast(Uidx, ItemCode, true);
			// ★ НОВОЕ (этап 2): та же запись едет в список активных эффектов (type 2).
			FAsdaEffectMsg M; M.UserIdx = Uidx; M.Kind = EAsdaEffectMsgKind::ItemApply;
			M.ItemCode = ItemCode; M.DurationSec = Dur; M.Byte9 = 99; M.Opcode = 5453;
			OnEffectMsg.Broadcast(M);
		}
		break;
	}
	case OP_RECV_ITEM_REMOVE_BY_TABLE2_RECORD: // 5454 0x154E — СНЯТИЕ предметного усилителя (и высадка)
	{
		// Эталон CItem_OnRemoveByTable2Record@0xF264F0 (item_inventory.c:66750):
		//   i16 userIdx@0, i32 itemCode@2 → charRemoveBuffBySkillId(char, Table2(itemCode)->subType).
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 2, (int32)Pkt.Opcode))
		{
			const int32 Uidx = AsdaBytes::RdI16(&B[0]);
			const int32 ItemCode = (NetNeedLen(B, 6, (int32)Pkt.Opcode))
				? AsdaBytes::RdI32(&B[2]) : 0;
			UE_LOG(LogAsdaNet, Log, TEXT("5454 снятие усилителя: userIdx=%d itemCode=%d"), Uidx, ItemCode);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 5454 предметный усилитель СНЯТ: uidx=%d%s предмет=%d"),
				Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), ItemCode);
			// ★ 04.11.2026 [ОРИГИНАЛ]: код предмета передаётся миру — эталон ссаживает ТОЛЬКО при `Table2Record->subType == 15`
			//   (`case 0xF`, item_inventory.c:66775); снятие бустера скорости и прочих усилителей верхом транспорт не трогает.
			OnMountState.Broadcast(Uidx, ItemCode, false);
			FAsdaEffectMsg M; M.UserIdx = Uidx; M.Kind = EAsdaEffectMsgKind::ItemRemove;
			M.ItemCode = ItemCode; M.Opcode = 5454;
			OnEffectMsg.Broadcast(M);
		}
		break;
	}
	// ═══════════════ ПОТОК АКТИВНЫХ ЭФФЕКТОВ (этап 2 MINISTATE_WIN) ═══════════════
	case OP_PKT_HANDLER_CHAR_SKILL_EFFECT_RESTART: // 5272 0x1498 — наложение/перезапуск СКИЛЛОВОГО баффа
	{
		// Эталон pktHandlerCharSkillEffectRestart@0xF4EE60 (network.c:44981):
		//   i16 userIdx@0, i16 skillId@2 (валидация < 0x15E = 350), далее с +4 «сырая» запись, из
		//   которой buffList_AddOrUpdate читает АБСОЛЮТНО +6/+9/+10/+11/+13 (= raw+2/+5/+6/+7/+9).
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 4, (int32)Pkt.Opcode))
		{
			const int32 Uidx    = AsdaBytes::RdI16(&B[0]);
			const int32 SkillId = (int32)AsdaBytes::RdU16(&B[2]);
			if (SkillId >= 0x15E)
			{
				UE_LOG(LogAsdaNet, Warning, TEXT("5272: skillId=%d >= 350 — пакет отброшен (эталон return 0)"), SkillId);
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ПРИЁМ 5272 отброшен: uidx=%d навык/состояние=%d ≥ 350 (эталон return 0)"), Uidx, SkillId);
				break;
			}
			auto Rd8  = [&B](int32 O) { return (B.size() > (size_t)O) ? (int32)B[O] : 0; };
			auto Rd16 = [&B](int32 O) { return (B.size() > (size_t)(O + 1)) ? AsdaBytes::RdI16(&B[O]) : 0; };
			FAsdaEffectMsg M;
			M.UserIdx = Uidx; M.Kind = EAsdaEffectMsgKind::SkillApply; M.Id = SkillId;
			M.IdFlag      = Rd16(6);    // raw+2
			M.Stack       = Rd8(9);     // raw+5
			M.KindByte    = Rd8(10);    // raw+6  (4 = постоянный)
			M.DurationSec = Rd16(11);   // raw+7
			M.Byte9       = Rd8(13);    // raw+9
			M.Opcode      = 5272;
			UE_LOG(LogAsdaNet, Log, TEXT("5272 бафф: userIdx=%d skillId=%d стек=%d вид=%d остаток=%d с флаг=%d"),
				Uidx, SkillId, M.Stack, M.KindByte, M.DurationSec, M.IdFlag);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 5272 эффект навыка НАЛОЖЕН/перезапущен: uidx=%d%s состояние=%d стек=%d вид=%d%s остаток=%d с флаг=%d байт9=%d"),
				Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), SkillId, M.Stack, M.KindByte, M.KindByte == 4 ? TEXT("(постоянный)") : TEXT(""),
				M.DurationSec, M.IdFlag, M.Byte9);
			OnEffectMsg.Broadcast(M);
		}
		break;
	}
	case OP_NET_HANDLER_REMOVE_CHAR_SKILL_EFFECT: // 5268 0x1494 — снятие эффекта скилла
	case OP_SKILL_EFFECT_REMOVE_FULL:             // 5273 0x1499 — снятие ПОЛНОЕ (+ сброс цвета слотов)
	{
		// Эталон netHandler_RemoveCharSkillEffect@0xF4F320 / skillEffectRemoveFull@0xF4F220:
		//   i16 userIdx@0, i16 skillId@2 (валидация < 350).
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 4, (int32)Pkt.Opcode))
		{
			const int32 Uidx    = AsdaBytes::RdI16(&B[0]);
			const int32 SkillId = (int32)AsdaBytes::RdU16(&B[2]);
			if (SkillId >= 0x15E) { ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ПРИЁМ %d отброшен: uidx=%d состояние=%d ≥ 350"), (int32)Pkt.Opcode, Uidx, SkillId); break; }
			FAsdaEffectMsg M; M.UserIdx = Uidx; M.Kind = EAsdaEffectMsgKind::SkillRemove;
			M.Id = SkillId; M.Opcode = (int32)Pkt.Opcode;
			UE_LOG(LogAsdaNet, Log, TEXT("%d снятие баффа: userIdx=%d skillId=%d"), (int32)Pkt.Opcode, Uidx, SkillId);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ %d эффект навыка СНЯТ%s: uidx=%d%s состояние=%d"), (int32)Pkt.Opcode,
				Pkt.Opcode == OP_SKILL_EFFECT_REMOVE_FULL ? TEXT(" полностью") : TEXT(""), Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), SkillId);
			OnEffectMsg.Broadcast(M);
		}
		break;
	}
	case OP_NET_RECV_BUFF_STATE_APPLY: // 5229 0x146D — СОУЛМЕЙТ-слот включён
	{
		// Эталон netRecvBuffStateApply@0xF50380: i16 userIdx@0, i16 slot@2 (< 30), i16 durationSec@4.
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 6, (int32)Pkt.Opcode))
		{
			const int32 Uidx = AsdaBytes::RdI16(&B[0]);
			const int32 Slot = (int32)AsdaBytes::RdU16(&B[2]);
			const int32 Dur  = AsdaBytes::RdI16(&B[4]);
			if (Slot >= 30) { break; }
			FAsdaEffectMsg M; M.UserIdx = Uidx; M.Kind = EAsdaEffectMsgKind::SoulApply;
			M.Id = Slot; M.DurationSec = Dur; M.Byte9 = 99; M.Opcode = 5229;
			UE_LOG(LogAsdaNet, Log, TEXT("5229 соулмейт-эффект ВКЛ: userIdx=%d слот=%d остаток=%d с"), Uidx, Slot, Dur);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 5229 эффект соулмейта ВКЛ: uidx=%d слот=%d остаток=%d с"), Uidx, Slot, Dur);
			OnEffectMsg.Broadcast(M);
		}
		break;
	}
	case OP_NET_REMOVE_SKILL_BUFF_STATE: // 5230 0x146E — СОУЛМЕЙТ-слот выключен
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 4, (int32)Pkt.Opcode))
		{
			const int32 Uidx = AsdaBytes::RdI16(&B[0]);
			const int32 Slot = (int32)AsdaBytes::RdU16(&B[2]);
			if (Slot >= 30) { break; }
			FAsdaEffectMsg M; M.UserIdx = Uidx; M.Kind = EAsdaEffectMsgKind::SoulRemove;
			M.Id = Slot; M.Opcode = 5230;
			UE_LOG(LogAsdaNet, Log, TEXT("5230 соулмейт-эффект ВЫКЛ: userIdx=%d слот=%d"), Uidx, Slot);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 5230 эффект соулмейта ВЫКЛ: uidx=%d слот=%d"), Uidx, Slot);
			OnEffectMsg.Broadcast(M);
		}
		break;
	}
	case OP_NET_CLEAR_ALL_CHARACTER_BUFFS_PACKET: // 5306 0x14BA — сброс ВСЕХ баффов персонажа
	{
		// ★ Эталон netClearAllCharacterBuffsPacket@0xF4F550 получает ЦЕЛЫЙ кадр (`sub_92B2E6(Str)` в
		//   диспетчере, а не `&Str[9]`) и читает userIdx по СМЕЩЕНИЮ +3 — то есть из ЗАГОЛОВКА кадра
		//   (байты 3..4 = HdrA, см. PacketFrame.h). Поэтому берём Pkt.HdrA, а не тело.
		const int32 Uidx = (int32)(int16)Pkt.HdrA;
		FAsdaEffectMsg M; M.UserIdx = Uidx; M.Kind = EAsdaEffectMsgKind::ClearAll; M.Opcode = 5306;
		UE_LOG(LogAsdaNet, Log, TEXT("5306 сброс всех баффов: userIdx=%d (из заголовка кадра)"), Uidx);
		ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 5306 сброс ВСЕХ эффектов: uidx=%d%s"), Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""));
		OnEffectMsg.Broadcast(M);
		break;
	}
	case OP_NET_CLEAR_CHARACTER_BUFFS_PACKET: // 5307 0x14BB — выборочный сброс
	{
		// Эталон netClearCharacterBuffsPacket@0xF4F5E0: i16 userIdx@0, u8 mode@2.
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 3, (int32)Pkt.Opcode))
		{
			const int32 Uidx = AsdaBytes::RdI16(&B[0]);
			FAsdaEffectMsg M; M.UserIdx = Uidx; M.Kind = EAsdaEffectMsgKind::ClearByMode;
			M.Mode = (int32)B[2]; M.Opcode = 5307;
			UE_LOG(LogAsdaNet, Log, TEXT("5307 выборочный сброс баффов: userIdx=%d режим=%d"), Uidx, M.Mode);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 5307 выборочный сброс эффектов: uidx=%d%s режим=%d"), Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), M.Mode);
			OnEffectMsg.Broadcast(M);
		}
		break;
	}
	case OP_NET_APPLY_SKILL_HIT_RESULT_PACKET: // 4102 0x1006 — пачка результатов применения скилла
	{
		// ★ ПОПРАВКА К РЕВЕРСУ: `netApplySkillHitResultPacket@0xF4F8A0` — это опкод **4102**, а НЕ 4029
		//   (`30_recv_dispatch_full_map.tsv:75`; 4029 = Recv_CastState@0xF41620, уже в порту).
		//   Записи по 17 б, count = (размер кадра − 10) / 17 (эталон: v15 = Src[1] − 10).
		//   Поля: u8 targetKind@0 (0 = сущность по renderId, 1 = игрок по userIdx), i16 targetIdx@1,
		//     i16 skillId@3 (−1 = пусто), i32 damage@5, i32 hpAfter@9, u8 applyFlag@13,
		//     i16 casterUserIdx@14, u8 stack@16.
		//   При targetKind==1 && applyFlag==1 && skillId ∉ {181,182} эталон зовёт
		//   cChar_restartSkillEffectByType — то есть БАФФ приезжает и внутри боевого пакета.
		// ★★★ ИСПРАВЛЕНО 2026-08-10 (дефект «урон и импакт от навыка не виден»). Прежний код брал
		//   ТОЛЬКО ветку баффа и отбрасывал ВСЕ записи с `targetKind == 0` (моб) под предлогом
		//   «урон применяет боевая подсистема опкодами 4015/4029». Это неверно: 4029 — очередь
		//   ОБЫЧНОЙ атаки (4026/4028), 4015 kind=3 — удар МОБА по игроку; урон НАВЫКА не приходит
		//   ни там, ни там. Доказательство — живой лог 2026-08-10 06:00:33: заявка 5256 навыка 707
		//   по мобу rid=164 → ответы 5271/5257 (состояние каста), и НИ ОДНОГО 4029/4015 по этому
		//   мобу; при этом единственные записи с полями «урон/HP после» приходят именно в 4102.
		//   Теперь запись уходит наверх целиком (делегат OnSkillHit) — урон, HP и пометку трупа
		//   применяет мир-подсистема ровно так, как эталон `netApplySkillHitResultPacket@0xF4F8A0`.
		// ★★★★★ ПОПРАВКА 2026-08-10 (dmgtext-agent): вывод «единственные записи с уроном приходят в
		//   4102» был НЕВЕРЕН. В живом бою 2026-08-10 12:17 опкод 4102 не пришёл НИ РАЗУ, а мобы
		//   умирали; в единственном кадре 4102 за всю историю логов (06:00:30) урон = 0 во всех трёх
		//   записях — это канал ПРИМЕНЕНИЯ дебаффа, у которого поле «урон» просто не заполняется.
		//   Настоящий урон навыка едет в 29-байтных записях 5257/5270 (см. `FAsdaSkillCastRec`).
		//   Обработчик 4102 оставлен КАК ЕСТЬ: он валиден для своей ветки (наложение эффекта + HP),
		//   а `HitFlags` у его записей остаётся 1 — «обычное попадание», поля флагов там нет.
		const std::vector<uint8>& B = Pkt.Body;
		const int32 N = (int32)B.size();
		int32 Applied = 0;
		int32 Hits = 0;
		// ★ Аудит №3 (B12): шаг записи назван — `kMobHitRecSize` (ту же длину берёт `AsdaSkillFxSubsystem`).
		for (int32 O = 0; O + kMobHitRecSize <= N; O += kMobHitRecSize)
		{
			FAsdaSkillHitRec R;
			R.TargetKind = (int32)B[O];
			R.TargetIdx  = AsdaBytes::RdI16(&B[O+1]);
			R.SkillId    = AsdaBytes::RdI16(&B[O+3]);
			R.Damage     = AsdaBytes::RdI32(&B[O+5]);
			R.HpAfter    = AsdaBytes::RdI32(&B[O+9]);
			R.ApplyFlag  = (int32)B[O+13];
			R.CasterIdx  = AsdaBytes::RdI16(&B[O+14]);
			R.Stack      = (int32)B[O+16];
			// ★ Пометка источника: у 4102 свой путь показа цифры (`CCharacter_TakeDamage2(a4 = 3)` →
			//   `combatFloatingDamageText(damageType = 0)`), стили 7/15/9 — см. `FAsdaSkillHitRec::bFrom4102`.
			R.bFrom4102  = true;
			// Эталон: `if (rec[+3] != -1)` — пустые записи (заполнитель массива на 20 штук) молча пропускаются.
			if (R.SkillId == -1) { continue; }
#if ASDA_COMBAT_TRACE
			// Участник нашего боя (до строки): чужой заклинатель бьёт моба нашего боя или меня → его события — уровень 1.
			if (!IsSelfUserIdx(R.CasterIdx) && R.CasterIdx >= 0
				&& (R.TargetKind == 1 ? IsSelfUserIdx(R.TargetIdx) : AsdaCombatTrace::IsEngaged(R.TargetIdx)))
			{
				AsdaCombatTrace::NoteEngagedUser(R.CasterIdx);
			}
#endif
			// Канал общий: уровень 1 — заклинатель наш или цель наша (игрок — UID, моб — IsEngaged).
			ASDA_CTRACE_IF(AsdaCombatTrace::IsUserRelevant(R.CasterIdx)
					|| (R.TargetKind == 1 ? AsdaCombatTrace::IsUserRelevant(R.TargetIdx) : AsdaCombatTrace::IsEngaged(R.TargetIdx)),
				EAsdaCT::Recv, TEXT("ПРИЁМ 4102 результат навыка: заклинатель uidx=%d → %s idx=%d навык/состояние=%d урон=%s HP после=%d наложить=%d стек=%d"),
				R.CasterIdx, R.TargetKind == 1 ? TEXT("игрок") : TEXT("моб rid"), R.TargetIdx, R.SkillId, *AsdaCtDmg(R.Damage), R.HpAfter, R.ApplyFlag, R.Stack);
			// ★ УРОН / HP / ПОМЕТКА ТРУПА — мир-подсистема (обе ветки эталона, kind 0 и kind 1).
			OnSkillHit.Broadcast(R);
			++Hits;
			// ── Наложение состояния. `if (rec[+13] == 1)` и отсев 181/182 — обе ветки эталона.
			if (R.ApplyFlag != 1) { continue; }
			// ★ 26.10.2026 (bowfx) [ОРИГИНАЛ]: у эталона отсев 181/182 (ловушки кровотечения/яда — их урон идёт слотом 6079) —
			//   это `return` из ВСЕГО разбора (network.c:45222/45247), а не пропуск записи: следующие записи пакета не применяются.
			//   Урон/HP этой записи слой мира уже отсеял сам (`bSkillIdFiltered`). Ключ `asda.Skill.MobStateChain 0` — прежний пропуск.
			if (R.SkillId == 181 || R.SkillId == 182)
			{
				if (AsdaMobStateChainOn()) { break; }
				continue;
			}
			if (R.TargetKind == 1)
			{
				// Ветка ИГРОКА: `cChar_restartSkillEffectByType(pChar, …, rec[+3])` — id СОСТОЯНИЯ.
				//   Иконку в HUD рисует только она (список эффектов сущности эталон не показывает).
				FAsdaEffectMsg M;
				M.UserIdx = R.TargetIdx;
				M.Kind = EAsdaEffectMsgKind::SkillApply;
				M.Id = R.SkillId;
				M.Stack = R.Stack;
				M.CasterIdx = R.CasterIdx;
				M.Opcode = 4102;
				OnEffectMsg.Broadcast(M);
				++Applied;
				continue;
			}
			// ── ★★★ Ветка СУЩНОСТИ (моба): `buff_ApplySkillBuff(ent, …)` → `skillBuffAdd@0xF836E0`
			//   → ВТОРАЯ таблица обработчиков визуала (`pMob + 1844 + buffId`). Ключ — renderId.
			// ★★★ 26.10.2026 (bowfx, баг «ловушка не видна») [ОРИГИНАЛ]: в ветке сущности поле записи +3 — КОД НАВЫКА, а не id
			//   состояния (в ветке игрока — наоборот, id состояния). Эталон (network.c:45250-45276): кастер (+14) в мире →
			//   `skillApplyChainedSubEffectsAtPos@0xA33900` — `buff_ApplySkillBuff` для buff0/buff1/buff2 записи навыка; кастера нет →
			//   `buff_ApplySkillBuff(ent, SkillRecord[36] = buff0, …)`. Прежний порт клал на моба состояние с номером НАВЫКА:
			//   ловушка 185 «Ледяная» вешала несуществующее у сущности состояние 185 вместо 0 «Оледенение» — ни заморозки, ни остановки.
			//   Ключ `asda.Skill.MobStateChain 0` — прежнее поведение.
			if (AsdaMobStateChainOn())
			{
				UAsdaEffectsSubsystem* FxC = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
				UAsdaWorldSubsystem* WC = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
				if (FxC && WC && WC->FindNetMobByRid(R.TargetIdx))
				{
					const bool bCasterInWorld = WC->ActorByTarget(EAsdaTargetKind::Character, R.CasterIdx) != nullptr;
					const int32 NS = FxC->ApplyMobSkillChain(R.TargetIdx, R.SkillId, R.CasterIdx, /*Stacks3=*/nullptr,
						/*bRequireDuration=*/false, /*bOnlySlot0=*/!bCasterInWorld);
					ASDA_CTRACE_IF(AsdaCombatTrace::IsEngaged(R.TargetIdx) || IsSelfUserIdx(R.CasterIdx), EAsdaCT::Fx,
						TEXT("4102: навык %d на моба rid=%d — цепочка состояний (%s): наложено %d"), R.SkillId, R.TargetIdx,
						bCasterInWorld ? TEXT("кастер в мире: buff0/1/2") : TEXT("кастера нет: только buff0"), NS);
				}
				continue;
			}
			{
				FAsdaEffectMsg M;
				M.UserIdx = R.TargetIdx;               // renderId сущности
				M.Kind = EAsdaEffectMsgKind::MobSkillApply;
				M.Id = R.SkillId;
				M.Stack = R.Stack;
				M.CasterIdx = R.CasterIdx;
				M.Opcode = 4102;
				OnEffectMsg.Broadcast(M);
			}
		}
		if (Hits > 0)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("4102: результатов навыка %d (из них баффов игроку %d), записей в кадре %d"),
				Hits, Applied, N / kMobHitRecSize);
		}
		break;
	}
	// ★★★ 26.10.2026 (bowfx, баг игроков «у яда и кровотечения нет цифр и значка») [ОРИГИНАЛ] 6079 0x17BF — «УРОН ПО ВРЕМЕНИ
	//   НАЛОЖЕН». Эталон `pktHandlerSkillCast@0xF4EFA0` (skill_buff.c:38423; диспетчер network.c:521). Раскладка 21 б и ветки —
	//   `FAsdaDotSlotPkt` / `UAsdaEffectsSubsystem::OnDotSlotPacket`. Прежний порт пакет не разбирал вовсе («хендлера нет»):
	//   ни состояния яда/крови на мобе (значок под полосой HP цели, эффект на мобе), ни тиков урона (цифры).
	case 6079:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 21, (int32)Pkt.Opcode)) { break; }
		FAsdaDotSlotPkt P;
		P.Ok         = (int32)B[0];
		P.CasterIdx  = AsdaBytes::RdI16(&B[1]);
		P.TargetKind = (int32)B[3];
		P.TargetIdx  = AsdaBytes::RdI16(&B[4]);
		P.SkillId    = AsdaBytes::RdI16(&B[6]);
		P.StateId    = AsdaBytes::RdI16(&B[8]);
		P.Level      = (int32)B[10];
		P.DurationMs = AsdaBytes::RdI32(&B[11]);
		P.IntervalMs = AsdaBytes::RdI16(&B[15]);
		P.TickDamage = AsdaBytes::RdI32(&B[17]);
		ASDA_CTRACE_IF(IsSelfUserIdx(P.CasterIdx) || (P.TargetKind == 0 ? AsdaCombatTrace::IsEngaged(P.TargetIdx) : AsdaCombatTrace::IsUserRelevant(P.TargetIdx)),
			EAsdaCT::Recv, TEXT("ПРИЁМ 6079 урон по времени: ok=%d кастер uidx=%d → %s %d, навык %d, состояние %d, ур. %d, %d мс, шаг %d мс, тик %d"),
			P.Ok, P.CasterIdx, P.TargetKind == 0 ? TEXT("моб rid") : TEXT("игрок uidx"), P.TargetIdx, P.SkillId, P.StateId, P.Level,
			P.DurationMs, P.IntervalMs, P.TickDamage);
		if (UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
		{
			Fx->OnDotSlotPacket(P);
		}
		break;
	}
	case OP_EVENT_SET:  // 6039 0x1797 — GM включил ивент (вид 0 = ДРОП, 1 = ОПЫТ) + множитель
	case OP_EVENT_OFF:  // 6041 0x1799 — GM выключил ивент этого вида
	{
		// Эталон Recv_EventSet@0xF4BAD0 / Recv_EventOff@0xF4BCB0 (network.c:44325) — печать GM-строки
		//   в чат; раскладку тел см. в объявлении OnEventSet (AsdaNetSubsystem.h).
		const std::vector<uint8>& B = Pkt.Body;
		const bool bOn = (Pkt.Opcode == (uint16)OP_EVENT_SET);
		if (!NetNeedLen(B, bOn ? 30 : 16, (int32)Pkt.Opcode)) { break; }   // N8 (дедуп №2, В2-A, 11.10.2026): была молчаливая проверка — теперь с Warning-строкой `NetNeedLen`
		const bool bOk = (B[0] == 1);
		int32 Type = -1; float Mult = 0.f; int32 Ch = 0, Srv = 0;
		auto I32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
		if (bOn)
		{
			Type = (int32)(int8)B[15];
			Mult = AsdaBytes::RdF32(B.data() + 16);   // ★ N6: `AsdaBytes`
			Ch = I32(20); Srv = I32(24);
			UE_LOG(LogAsdaNet, Log,
				TEXT("6039 EVENT SET %s: вид=%d (%s) множитель=×%.2f сервер=%d канал=%d, окно %02d.%02d %02d:%02d → %02d.%02d %02d:%02d, уровни %d..%d"),
				bOk ? TEXT("OK") : TEXT("ОТКАЗ"), Type,
				Type == 0 ? TEXT("ДРОП") : Type == 1 ? TEXT("ОПЫТ") : TEXT("?"), Mult, Srv, Ch,
				(int32)(int8)B[8], (int32)(int8)B[7], (int32)(int8)B[9], (int32)(int8)B[10],
				(int32)(int8)B[12], (int32)(int8)B[11], (int32)(int8)B[13], (int32)(int8)B[14],
				(int32)(int8)B[28], (int32)(int8)B[29]);
		}
		else
		{
			Type = (int32)(int8)B[7]; Ch = I32(8); Srv = I32(12);
			UE_LOG(LogAsdaNet, Log, TEXT("6041 EVENT OFF %s: вид=%d (%s) сервер=%d канал=%d"),
				bOk ? TEXT("OK") : TEXT("ОТКАЗ"), Type,
				Type == 0 ? TEXT("ДРОП") : Type == 1 ? TEXT("ОПЫТ") : TEXT("?"), Srv, Ch);
		}
		OnEventSet.Broadcast(bOk, Type, Mult, bOn);
		break;
	}
	case OP_ALARM_GAME_SERVER_EVENT_INFO: // 4301 0x10CD — «час удачи»: множители EXP/Drop
	{
		// Эталон uiSetScheduledClock@0xE82DB0 (ui_core.c:78007), тело 16 б.
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 16, (int32)Pkt.Opcode))
		{
			FAsdaEventBonusPkt E;
			E.EventType      = (int32)(int8)B[0];
			E.Month          = (int32)(int8)B[1];
			E.Day            = (int32)(int8)B[2];
			E.Hour           = (int32)(int8)B[3];
			E.Minute         = (int32)(int8)B[4];
			E.Second         = (int32)(int8)B[5];
			E.SchedYear      = AsdaBytes::RdI16(&B[6]);
			E.SchedMonth     = (int32)(int8)B[8];
			E.SchedDay       = (int32)(int8)B[9];
			E.SchedStartHour = (int32)(int8)B[10];
			E.EndHour        = (int32)(int8)B[11];
			FMemory::Memcpy(&E.BonusRate, B.data() + 12, sizeof(float));
			UE_LOG(LogAsdaNet, Log,
				TEXT("4301 ивент-бонус: вид=%d (%s) часы сервера %02d.%02d %02d:%02d:%02d; "
				     "строка расписания %04d-%02d-%02d %02d..%02d ч; бонус=%.2f (множитель ×%.2f)"),
				E.EventType,
				E.EventType == 1 ? TEXT("опыт") : E.EventType == 2 ? TEXT("золото") : E.EventType == 3 ? TEXT("опыт+золото") : TEXT("нет"),
				E.Day, E.Month, E.Hour, E.Minute, E.Second,
				E.SchedYear, E.SchedMonth, E.SchedDay, E.SchedStartHour, E.EndHour,
				E.BonusRate, E.BonusRate + 1.f);
			OnEventBonus.Broadcast(E);
		}
		break;
	}
	case OP_RECV_PACKET_CHAR_STATE_INFO_6154: // 6154 0x180A — GM_COMPACT_CHARSTATE_INFO: снапшот стейта соседа
	{
		// Эталон Recv_GM_COMPACT_CHARSTATE_INFO@0xF3E1A0: [0..1] userIdx, далее записи по 12 б:
		//   u8 type@+0 (0=скилл-эффект 1=статус 2=ПРЕМИУМ-БАФ); type=2: itemCode i32@+2,
		//   duration i32@+6 (-1=бессрочно), value u16@+10 (у маунта = % скорости, Function_Table+9).
		//   Сервер (GameServer.c:15412) шлёт его В ПАРЕ с 4005 при появлении соседа — это и есть канал
		//   «сосед УЖЕ на транспорте при нашем входе». Живой лог: body=[02 00 | 02 00 15 02 00 00
		//   FF FF FF FF 50 00] → uidx=2, itemCode=533, dur=-1, value=80. Применение = как 5453:
		//   subType в записи НЕ передаётся — мир ищет SubType по itemCode в special_itemlist (Table2).
		const uint8* B = Pkt.Body.data();
		const int32  N = (int32)Pkt.Body.size();
		if (N < 2) { break; }
		const int32 Uidx = (int32)AsdaBytes::RdU16(&B[0]);
		// ★★★ ЭТАП 2: тот же пакет — ЕДИНСТВЕННЫЙ источник ПОЛНОГО состояния эффектов при входе в мир.
		//   Эталон Recv_GM_COMPACT_CHARSTATE_INFO@0xF3E1A0 (network.c:40400) раскладывает 12-байтные
		//   записи в 592-байтный «pending user»-блок, который потом читает
		//   cChar_restoreSkillEffectsFromRecord@0x9CE420 (entity_char.c:6020):
		//     type 0 → блок+2+5i:  u8 kind, i16 duration, i16 skillId      → buffList_AddOrUpdate
		//     type 1 → блок+142+5j: u8 kind, i16 duration, i16 slot        → charStatusAddType1
		//     type 2 → блок+292+10k: i32 itemCode, i32 duration, i16 value → cCharStatusAddTypeMount
		//   ⇒ в САМОЙ записи 12 б: u8 type@0, u8 kind@1, i32/i16 id@2, i32/i16 duration@6, u16 value@10.
		//   Это снимает вопрос №1 и №2 отчёта-реверса (опкод найден, «off-by-one» — его нет:
		//   поля просто лежат в порядке kind/duration/id).
		{
			int32 T0 = 0, T1 = 0, T2 = 0;
			for (int32 Off = 2; Off + 12 <= N; Off += 12)
			{
				const uint8 Type = B[Off];
				FAsdaEffectMsg M; M.UserIdx = Uidx; M.Opcode = 6154;
				M.KindByte = (int32)B[Off + 1];
				if (Type == 0)
				{
					M.Kind = EAsdaEffectMsgKind::SkillApply;
					M.Id          = AsdaBytes::RdI16(&B[Off+2]);
					M.DurationSec = AsdaBytes::RdI16(&B[Off+6]);
					if (M.Id < 0 || M.Id >= 0x15E) { continue; }
					++T0;
				}
				else if (Type == 1)
				{
					M.Kind = EAsdaEffectMsgKind::SoulApply;
					M.Id          = AsdaBytes::RdI16(&B[Off+2]);
					M.DurationSec = AsdaBytes::RdI16(&B[Off+6]);
					M.Byte9 = 99;
					if (M.Id < 0 || M.Id >= 30) { continue; }
					++T1;
				}
				else if (Type == 2)
				{
					M.Kind = EAsdaEffectMsgKind::ItemApply;
					M.ItemCode    = AsdaBytes::RdI32(&B[Off+2]);
					M.DurationSec = AsdaBytes::RdI32(&B[Off+6]);
					M.Byte9 = 99;
					if (M.ItemCode <= 0) { continue; }
					++T2;
				}
				else { continue; }
				OnEffectMsg.Broadcast(M);
			}
			if (T0 + T1 + T2 > 0)
			{
				UE_LOG(LogAsdaNet, Log, TEXT("6154 снапшот эффектов: userIdx=%d скилловых=%d соулмейт=%d предметных=%d"),
					Uidx, T0, T1, T2);
			}
		}
		int32 NumType2 = 0;
		for (int32 Off = 2; Off + 12 <= N; Off += 12)
		{
			if (B[Off] != 2) { continue; }   // 0/1 = скилл-эффекты/статусы соседа (визуала бафов у нас пока нет)
			++NumType2;
			const int32 Code = AsdaBytes::RdI32(&B[Off+2]);
			const int32 Dur  = AsdaBytes::RdI32(&B[Off+6]);
			const int32 Val  = (int32)AsdaBytes::RdU16(&B[Off+10]);
			UE_LOG(LogAsdaNet, Log, TEXT("[транспорт] из появления 6154: userIdx=%d itemCode=%d dur=%d value=%d"), Uidx, Code, Dur, Val);
			if (Code > 0) { OnMountState.Broadcast(Uidx, Code, true); }   // не-маунт коды мир отсеет по SubType
		}
		if (NumType2 == 0)
		{
			// Авторитетный снапшот премиум-бафов пуст → игрок точно НЕ на транспорте: снять стейл-кэш
			//   (высадка вне нашего AoI не приносит 5454).
			// ★ 05.11.2026 (`asda.Mount.SnapshotKeepsSelf`, разбор у ключа): только соседям, как 5418.
			const bool bKeepSelf = CVarMountSnapshotKeepsSelf.GetValueOnGameThread() != 0 && AsdaSelf::IsSelfUserIdx(this, Uidx);
			if (bKeepSelf)
			{
				UE_LOG(LogAsdaNet, Log, TEXT("6154 про себя (userIdx=%d) без записей type 2: свой транспорт не снимаю (снимают 5454/смерть)"), Uidx);
			}
			else
			{
				OnMountState.Broadcast(Uidx, 0, false);
			}
		}
		break;
	}
	// ═════════════════ ПИТОМЦЫ: ВСЕ ТРИНАДЦАТЬ ОТВЕТОВ S→C ═══════════════════════════════════════
	//   Разбор полей — в `UAsdaPetSubsystem::OnPetPacket` (там же, где модель данных, в которую он
	//   пишет). Здесь только проверка МИНИМАЛЬНОЙ длины, копия байт и делегат — закон
	//   `no-reentrant-net-callbacks`.
	// ★ ДЛИНЫ. Сервер шлёт эти ответы через `sub_4C28F0(..., a7 = 1)`, а это в `sub_5643E0`
	//   добавляет 0..19 байт ХВОСТОВОГО МУСОРА (`v24 = a4 + Size + 10`). Поэтому проверка здесь
	//   «не короче», а не «ровно». Исключение — 6106: он шлётся с `a7 = 0` (GameServer.c:34788,
	//   35458, 35470) и эталон проверяет его ТОЧНОЙ формулой `(кадр − 15) % 46 == 0`
	//   (network.c:25630-25633) = `тело == 5 + 46·N` в терминах порта; эта точная проверка
	//   сделана в самом разборе 6106 (закон `packet-size-check-no-padding`).
	case OP_PET_CALL_RESULT:        //   9 б — 6101 призыв/отзыв
	case OP_PET_INCUBATE_RESULT:    //  18 б — 6103 инкубатор
	case OP_PET_CONDITION_UPDATE:   //  29 б — 6104 сытость + три бонуса
	case OP_PET_EXP_UPDATE:         //  15 б — 6105 опыт (+ ветка уровня)
	case OP_PET_LIST:               // 5 + 46·N — 6106 список питомцев
	case OP_PET_HUNGER_RECALL:      //   8 б — 6107 сытость упала в 0, питомец отозван
	case OP_PET_FEED_RESULT:        //  89 б — 6109 кормление
	case OP_PET_RELEASE_RESULT:     //   9 б — 6111 выброс
	case OP_PET_UNION_RESULT:       //  56 б — 6113 эволюция
	case OP_PET_NAME_CHANGE_RESULT: //  25 б — 6119 переименование
	case OP_PET_SLOT_UP_RESULT:     //  13 б — 6122 расширение ячеек
	case OP_PET_SLOT_COUNT:         //   5 б — 6123 число ячеек при входе и портале
	case OP_PET_LIMITBREAK_RESULT:  // 110 б — 6149 лимит-брейк
	{
		const std::vector<uint8>& B = Pkt.Body;
		// Минимальная длина тела по опкоду — та же таблица, что в шапке ветки.
		int32 Need = 0;
		switch (Pkt.Opcode)
		{
			case OP_PET_CALL_RESULT:        Need =   9; break;
			case OP_PET_INCUBATE_RESULT:    Need =  18; break;
			case OP_PET_CONDITION_UPDATE:   Need =  29; break;
			case OP_PET_EXP_UPDATE:         Need =  15; break;
			case OP_PET_LIST:               Need =   5; break;   // точную формулу проверяет разбор
			case OP_PET_HUNGER_RECALL:      Need =   8; break;
			case OP_PET_FEED_RESULT:        Need =  89; break;
			case OP_PET_RELEASE_RESULT:     Need =   9; break;
			case OP_PET_UNION_RESULT:       Need =  56; break;
			case OP_PET_NAME_CHANGE_RESULT: Need =  25; break;
			case OP_PET_SLOT_UP_RESULT:     Need =  13; break;
			case OP_PET_SLOT_COUNT:         Need =   5; break;
			default:                        Need = 110; break;   // OP_PET_LIMITBREAK_RESULT
		}
		if (!NetNeedLen(B, Need, (int32)Pkt.Opcode, TEXT("пакет питомцев"))) { break; }
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Verbose, TEXT("питомцы: пришёл %d, тело %d б (минимум %d)"),
			(int32)Pkt.Opcode, Body.Num(), Need);
		OnPetPacket.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}
	case OP_PEER_ACTIVE_PET: // 6120 0x17E8 — активный ПИТОМЕЦ игрока (pet_system_re.md)
	{
		const std::vector<uint8>& B = Pkt.Body;
		// ★★★ ВОЛНА 8 (пункт 9 ревизии питомцев): МИНИМУМ ПОДНЯТ С 6 Б ДО 22 Б.
		//   Эталон `charNearbyInsertEntry@0xDCA620` копирует ровно 22 байта БЕЗУСЛОВНО:
		//   `memcpy(v3, Src + 9, 0x16u)` (entity_char.c:48541-48543). Сервер всегда шлёт 22:
		//   все восемь вызовов отправки идут с длиной 22 (GameServer.c:11788, 11925,
		//   12549, 12685, 34327, 34397, 35448, 36341) — проверено сплошным поиском по бинарю.
		//   Чем плох был минимум 6: короткое тело проходило и давало ПУСТОЕ имя питомца
		//   вместо отказа — то есть битый пакет молча превращался в «питомца без имени».
		if (NetNeedLen(B, 22, (int32)Pkt.Opcode))
		{
			const int32 CharId = AsdaBytes::RdI32(&B[0]);
			const int32 RawType = (int32)AsdaBytes::RdU16(&B[4]);
			const int32 PetType = (RawType == 0xFFFF) ? -1 : RawType;   // 0xFFFF = питомец убран
			const FString Name = FAsdaLoginData::DecodeCp1251(B.data() + 6, 16);
			UE_LOG(LogAsdaNet, Log, TEXT("6120 питомец: charId=%d тип=%d '%s'"), CharId, PetType, *Name);
			OnPeerPet.Broadcast(CharId, PetType, Name);
		}
		break;
	}
	case OP_RECV_CAMP_WAR_CLAN_MEMBER_UPDATE: // 6722 0x1A42 — фракция/звание ЧУЖОГО (faction_peer_re.md)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 9, (int32)Pkt.Opcode))
		{
			const int32 CharId = AsdaBytes::RdI32(&B[0]);
			const int32 CampId = AsdaBytes::RdI16(&B[6]);
			const int32 Grade  = (int32)(int8)B[8];
			UE_LOG(LogAsdaNet, Log, TEXT("6722 фракция соседа: charId=%d лагерь=%d звание=%d"), CharId, CampId, Grade);
			OnPeerFaction.Broadcast(CharId, CampId, Grade);
		}
		break;
	}
	// ═════════ СИСТЕМА «ХРАНИТЕЛЯ» (ЭТАП 3): 6157 монеты душ, 6156 призванный хранитель ═════════
	case OP_RECV_SKILL_SLOT_STATE: // 6157 0x180D — recvSkillSlotState@0xEF6B10, 11 б от тела
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() >= 11)
		{
			const int32 Uidx      = AsdaBytes::RdI32(&B[0]);
			const int32 AddFlag   = (int32)(int8)B[4];
			const int32 SkillType = (int32)(int8)B[5];
			const int32 SkillIdx  = AsdaBytes::RdI16(&B[6]);
			const int32 C0 = (int32)(int8)B[8], C1 = (int32)(int8)B[9], C2 = (int32)(int8)B[10];
			UE_LOG(LogAsdaNet, Log, TEXT("6157 хранитель: userIdx=%d AddFlag=%d SkillType=%d skill=%d монеты=%d/%d/%d"),
				Uidx, AddFlag, SkillType, SkillIdx, C0, C1, C2);
			ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 6157 слот навыка хранителя: uidx=%d добавить=%d тип=%d навык=%d монеты=%d/%d/%d"),
				Uidx, AddFlag, SkillType, SkillIdx, C0, C1, C2);
			OnSoulSlotState.Broadcast(Uidx, AddFlag, SkillType, SkillIdx, C0, C1, C2);
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("6157 хранитель: тело %d б (ждали 11) — пропуск"), (int32)B.size());
		}
		break;
	}
	case OP_CHAR_HANDLE_SLOT_KEY_PACKET: // 6156 0x180C — charHandleSlotKeyPacket@0xEF6D00, 5 б
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() >= 5)
		{
			// ★★★ КЛЮЧ = charId (CUser+372), НЕ сессионный userIdx — обоснование в шапке делегата
			//   OnSoulGuardIdx (AsdaNetSubsystem.h). Клиент-эталон ищет владельца
			//   `CharManager_FindByCharId`, а не `FindByUserIdx`.
			const int32 CharId = AsdaBytes::RdI32(&B[0]);
			const int32 Idx    = (int32)(int8)B[4];
			UE_LOG(LogAsdaNet, Log, TEXT("6156 хранитель: charId=%d SoulGuardIndex=%d"), CharId, Idx);
			OnSoulGuardIdx.Broadcast(CharId, Idx);
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("6156 хранитель: тело %d б (ждали 5) — пропуск"), (int32)B.size());
		}
		break;
	}
	// ★★★ 6159 / 6160 — КАСТ СКИЛЛА ПО СЕКТОРУ (свой И чужие). Полный разбор — в шапке делегатов
	//   OnSkillCast / OnSkillCastIdle.
	// ★★★ ПОПРАВКА 2026-08-11 (aoedmg-agent): записи по 29 б 6159/6160 — ЭТО КАНАЛ УРОНА СКИЛЛА
	//   ХРАНИТЕЛЯ, ровно как у 5257/5270. Прежний комментарий утверждал, что «боевые числа ведёт
	//   цепочка 4028/4029» — это опровергнуто данными: во ВСЕХ живых логах порта пакетов
	//   4027/4028/4029/4102 РОВНО НОЛЬ, а лог фазы удара печатал «записей урона 0». Урон скилла
	//   хранителя не показывался вовсе, а смерть цели приезжала сырыми HP-пакетами 4015/4017 —
	//   то есть ДО кадра удара. Эталон различия между 5257 и 6159 не делает: обе строки диспетчера
	//   (`network.c:1819 case 0x1489` и `case 0x180F`) ведут в ОДНУ `Recv_SkillCast@0xF4E370`,
	//   которая кладёт `*(u16*)(rec+1)` в массив целей и `*(i32*)(rec+3)` в массив урона
	//   (`skill_buff.c:19019-19054`), а показывает их ТИК на кадре удара.
	case OP_SKILL_CAST: // 6159 0x180F — Recv_SkillCast@0xF4E370
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() >= 8)
		{
			const int32 Res      = (int32)(uint8)B[0];
			// ★ Аудит №3 (знаковость): эталон `Recv_SkillCast@0xF4E370` держит шапку в `_WORD header[4]`
			//   и читает её БЕЗ знака: `*(_WORD *)((char *)header + 1)` → `CharManager_FindByUserIdx`,
			//   `*(_WORD *)((char *)&header[1] + 1)` → `CSkillMgr_GetSkillRecord`, `header[3]` → уровень
			//   в `packPathDispatchEntryHandler`. Порт читал все три поля знаково.
			const int32 Uidx     = (int32)AsdaBytes::RdU16(&B[1]);
			const int32 SkillCode= (int32)AsdaBytes::RdU16(&B[3]);
			const int32 Flag     = (int32)AsdaBytes::RdU16(&B[6]);
			const int32 Hits     = (int32)((B.size() - 8) / 29);
			UE_LOG(LogAsdaNet, Log, TEXT("6159 каст: result=%d кастер(userIdx)=%d скилл=%d флаг=%d записей=%d"),
				Res, Uidx, SkillCode, Flag, Hits);
			ASDA_CTRACE_UID(Uidx, Res == 1 ? EAsdaCT::Recv : EAsdaCT::Refuse,
				TEXT("ПРИЁМ 6159 каст хранителя: результат=%d%s кастер uidx=%d%s навык=%d флаг=%d записей=%d%s"),
				Res, Res == 1 ? TEXT(" (успех)") : TEXT(" (ОТКАЗ)"), Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), SkillCode, Flag, Hits,
				Res == 1 ? TEXT("") : *FString::Printf(TEXT(", нотис loc=%d"), SkillFailNoticeLocId(Res)));
			// ★★★ РАЗБОР 29-БАЙТНЫХ ЗАПИСЕЙ: и цель разворота, И УРОН (поправка 2026-08-11 — см.
			//   шапку `OnSoulSkillCastRecs`). Эталон в `Recv_SkillCast@0xF4E370` кладёт индекс первой
			//   записи в `targetRenderId` (курс персонажа в состоянии 24) И раскладывает ВСЕ записи
				//   по массивам целей/урона (skill_buff.c:19019-19054), которые показывает тик на
				//   кадре удара. Потребителей у делегата теперь двое: мир-подсистема (разворот) и
				//   слой показа навыков (парковка урона до порога фазы).
			if (Res == 1 && Hits > 0)
			{
				TArray<FAsdaSkillCastRec> Recs;
				ParseSkillCastRecs29(B.data(), (int32)B.size(), 8, Hits, Recs);
				OnSoulSkillCastRecs.Broadcast(Uidx, SkillCode, Recs);
			}
			OnSkillCast.Broadcast(Res, Uidx, SkillCode, Flag, Hits);
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("6159 каст: тело %d б (ждали >= 8) — пропуск"), (int32)B.size());
		}
		break;
	}
	case OP_SKILL_CAST_IDLE: // 6160 0x1810 — Recv_SkillCastIdle@0xF4D9C0
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 7, (int32)Pkt.Opcode))
		{
			// ★ Аудит №3 (знаковость): эталон `Recv_SkillCastIdle@0xF4D9C0` копирует 7 байт шапки в
			//   `_WORD hdr[4]` и читает `hdr[0]` (userIdx), `hdr[1]` (skillCode), `hdr[2]` (уровень)
			//   БЕЗ знака; знаковым остаётся только renderId внутри 29-байтных записей.
			const int32 Uidx      = (int32)AsdaBytes::RdU16(&B[0]);
			const int32 SkillCode = (int32)AsdaBytes::RdU16(&B[2]);
			const int32 Flag      = (int32)AsdaBytes::RdU16(&B[4]);
			const int32 Hits      = (int32)((B.size() - 7) / 29);
			UE_LOG(LogAsdaNet, Log, TEXT("6160 начало каста: кастер(userIdx)=%d скилл=%d флаг=%d записей=%d"),
				Uidx, SkillCode, Flag, Hits);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 6160 начало каста хранителя: кастер uidx=%d%s навык=%d флаг=%d записей=%d"),
				Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), SkillCode, Flag, Hits);
			// ★ Те же 29-байтные записи, что и у 6159 (шапка на байт короче — записи с +7 кадра).
			//   Эталон обе шапки разбирает ОДНОЙ `Recv_SkillCastIdle@0xF4D9C0`/`Recv_SkillCast`, и
			//   раскладка массивов целей/урона у них общая — значит и «долгий каст» хранителя несёт
			//   урон. Раньше записи 6160 отбрасывались молча (закон jeb-emitter-routing-hole).
			if (Hits > 0)
			{
				TArray<FAsdaSkillCastRec> Recs;
				ParseSkillCastRecs29(B.data(), (int32)B.size(), 7, Hits, Recs);
				OnSoulSkillCastRecs.Broadcast(Uidx, SkillCode, Recs);
			}
			OnSkillCastIdle.Broadcast(Uidx, SkillCode, Flag, Hits);
		}
		break;
	}
	// ★★★ 5257 / 5270 — ТО ЖЕ САМОЕ для ОБЫЧНЫХ навыков (эталон: те же две функции, диспетчер
	//   network.c:1819 `case 0x1489 → LABEL_723` и :1824 `case 0x1496 → LABEL_724`). Делегаты
	//   отдельные: подсистема Хранителя слушает 6159/6160 и печатает нотис отказа ДО фильтра
	//   «скилл хранителя», поэтому смешивать каналы нельзя (был бы двойной нотис).
	case OP_SKILL_CAST_RESULT: // 5257 0x1489 — Recv_SkillCast@0xF4E370 (обычные навыки)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() >= 8)
		{
			const int32 Res      = (int32)(uint8)B[0];
			// ★ Аудит №3 (знаковость): тот же `Recv_SkillCast@0xF4E370` — шапка `_WORD header[4]`,
			//   все три поля читаются БЕЗ знака (см. пояснение у 6159).
			const int32 Uidx     = (int32)AsdaBytes::RdU16(&B[1]);
			const int32 SkillCode= (int32)AsdaBytes::RdU16(&B[3]);
			const int32 Level    = (int32)AsdaBytes::RdU16(&B[6]);
			const int32 Hits     = (int32)((B.size() - 8) / 29);
			UE_LOG(LogAsdaNet, Log, TEXT("5257 навык: result=%d кастер(userIdx)=%d навык=%d уровень=%d записей=%d"),
				Res, Uidx, SkillCode, Level, Hits);
			ASDA_CTRACE_UID(Uidx, Res == 1 ? EAsdaCT::Recv : EAsdaCT::Refuse,
				TEXT("ПРИЁМ 5257 результат навыка: результат=%d%s кастер uidx=%d%s навык=%d уровень=%d записей=%d%s"),
				Res, Res == 1 ? TEXT(" (успех)") : TEXT(" (ОТКАЗ)"), Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), SkillCode, Level, Hits,
				Res == 1 ? TEXT("") : *FString::Printf(TEXT(", нотис loc=%d"), SkillFailNoticeLocId(Res)));
			// ★★★ Разбор 29-байтных записей — ЭТО КАНАЛ УРОНА НАВЫКА (поправка 2026-08-10, §19
			//   отчёта): `+3` урон, `+7` флаги попадания/крита, `+11` HP после. Наверх едут
			//   делегатом `OnSkillCastRecs`; урон применяет визуальный слой в момент удара
			//   (`UAsdaSkillFxSubsystem::FlushPendingCastHits`), как эталонный тик.
			if (Res == 1 && Hits > 0)
			{
				TArray<FAsdaSkillCastRec> Recs;
				ParseSkillCastRecs29(B.data(), (int32)B.size(), 8, Hits, Recs);
				AsdaSanitizeSelfHitRecs(Uidx, SkillCode, Recs, 5257);   // ★ 05.11.2026 [ИСПРАВЛЕНИЕ] урон по себе у само-каста
				OnSkillCastRecs.Broadcast(Uidx, SkillCode, Recs);
			}
			OnSkillUseResult.Broadcast(Res, Uidx, SkillCode, Level, Hits);
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("5257 навык: тело %d б (ждали >= 8) — пропуск"), (int32)B.size());
		}
		break;
	}
	case OP_SKILL_CAST_IDLE_NORMAL: // 5270 0x1496 — Recv_SkillCastIdle@0xF4D9C0 (обычные навыки)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 7, (int32)Pkt.Opcode))
		{
			// ★ Аудит №3 (знаковость): тот же `Recv_SkillCastIdle@0xF4D9C0` — `_WORD hdr[4]`, все три
			//   поля шапки БЕЗ знака (см. пояснение у 6160).
			const int32 Uidx      = (int32)AsdaBytes::RdU16(&B[0]);
			const int32 SkillCode = (int32)AsdaBytes::RdU16(&B[2]);
			const int32 Level     = (int32)AsdaBytes::RdU16(&B[4]);
			const int32 Hits      = (int32)((B.size() - 7) / 29);
			UE_LOG(LogAsdaNet, Log, TEXT("5270 начало каста: кастер(userIdx)=%d навык=%d уровень=%d записей=%d"),
				Uidx, SkillCode, Level, Hits);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 5270 начало каста навыка: кастер uidx=%d%s навык=%d уровень=%d записей=%d"),
				Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), SkillCode, Level, Hits);
			// ★ Те же 29-байтные записи, что и у 5257 (шапка на байт короче — записи с +7 кадра).
			if (Hits > 0)
			{
				TArray<FAsdaSkillCastRec> Recs;
				ParseSkillCastRecs29(B.data(), (int32)B.size(), 7, Hits, Recs);
				AsdaSanitizeSelfHitRecs(Uidx, SkillCode, Recs, 5270);   // ★ 05.11.2026 [ИСПРАВЛЕНИЕ] урон по себе у само-каста
				OnSkillCastRecs.Broadcast(Uidx, SkillCode, Recs);
			}
			OnSkillUseCastIdle.Broadcast(Uidx, SkillCode, Level, Hits);
		}
		break;
	}
	// ★ 6161 / 5271 `recvOtherCharSkillUse@0xF4D7D0` — «каст начался/кончился» (7 б), по сектору.
	//   Эталон здесь гасит поле каста персонажа (`effectSlots[100].field_4 = -1`) и при result != 1
	//   печатает нотис `dword_15B5D78[result]`. 6161 = скиллы хранителя, 5271 = обычные.
	case OP_RECV_OTHER_CHAR_SKILL_USE: // 6161 0x1811
	case OP_SKILL_USE_STATE_BROADCAST: // 5271 0x1497 — тот же обработчик в диспетчере эталона
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 7, (int32)Pkt.Opcode))
		{
			const int32 Res     = (int32)(uint8)B[0];
			const int32 Uidx    = AsdaBytes::RdI16(&B[1]);
			const int32 SkillId = AsdaBytes::RdI16(&B[3]);
			const int32 Flag    = AsdaBytes::RdI16(&B[5]);
			const bool  bGuard  = (Pkt.Opcode == (uint16)OP_RECV_OTHER_CHAR_SKILL_USE);
			UE_LOG(LogAsdaNet, Log, TEXT("%d каст-состояние: result=%d userIdx=%d skillId=%d flag=%d%s"),
				(int32)Pkt.Opcode, Res, Uidx, SkillId, Flag, bGuard ? TEXT(" [хранитель]") : TEXT(""));
			ASDA_CTRACE_UID(Uidx, Res == 1 ? EAsdaCT::Recv : EAsdaCT::Refuse,
				TEXT("ПРИЁМ %d состояние каста%s: результат=%d кастер uidx=%d%s навык=%d флаг=%d%s"),
				(int32)Pkt.Opcode, bGuard ? TEXT(" хранителя") : TEXT(""), Res, Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), SkillId, Flag,
				Res == 1 ? TEXT("") : *FString::Printf(TEXT(" — ОТКАЗ, нотис loc=%d"), SkillFailNoticeLocId(Res)));
			OnSkillCastState.Broadcast(Res, Uidx, SkillId, Flag, bGuard);
		}
		break;
	}
	case OP_RECV_PACKET_ENTITY_MOVE: // 4007 0xFA7 — движение (зеркало 4006: result@0, userIdx@1, cur/dst float, speed@21)
	case OP_RECV_PACKET_ENTITY_STOP: // 4009 0xFA9 — снап-стоп (та же раскладка; позиция = cur)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 27, (int32)Pkt.Opcode))
		{
			const int32 Res = B[0];
			// ★ 16.09.2026 (ревизия 4062, МЕЛК): ЗНАКОВО, как в эталоне `*(__int16 *)(a2 + 1)` — ключ тот же,
			//   что у 4062 и у реестра NetPlayers; закон проекта «charId (i32) ≠ userNum (i16)».
			int16 Uidx = (int16)AsdaBytes::RdI16(B.data() + 1);   // ★ N6: `AsdaBytes`
			float Cx = AsdaBytes::RdF32(B.data() + 5),  Cz = AsdaBytes::RdF32(B.data() + 9);
			float Dx = AsdaBytes::RdF32(B.data() + 13), Dz = AsdaBytes::RdF32(B.data() + 17);
			float Spd = AsdaBytes::RdF32(B.data() + 21);
			const bool bStop = (Pkt.Opcode == (uint16)OP_RECV_PACKET_ENTITY_STOP);
			// ★★★ 2026-08-10, ПРИЧИНА «Сначала нужно остановиться» (5257 result=24).
			//   Здесь стояло `if (!bStop && Uidx == свой) bActionPending = false;` — ЭТО НЕ ЭТАЛОН.
			//   В оригинале `g_bActionPending` ставится ТОЛЬКО отправкой 4006
			//   (`Combat_SendNormalAttack@0x9F8CA0`, combat_pvp.c:1511; плюс состояние 15
			//   entity_char.c:16966) и сбрасывается ТОЛЬКО внутри `Net_SendActionPacket@0x9F9C00`
			//   (network.c:5016-5018) — то есть в момент РЕАЛЬНОЙ отправки 4008. Приход 4007 его НЕ
			//   трогает (все 6 упоминаний g_bActionPending в эталоне проверены).
			//   Последствие ошибки: после ответа 4007 флаг гас, `SendStop4008` (гейт `!bActionPending`)
			//   молча превращался в no-op, 4008 не уходил НИКОГДА → серверный флаг движения
			//   `CUser+51` (ставится `SetEmpty_UserMoveStack@0x542300` из `CharMove`) не снимался, и
			//   `RecvChar_SkillUse` (GameServer.c:45973) отбивал ЛЮБОЙ навык кодом 24.
			//   Доказательство из живого лога 14:12:53 — ПЕРВЫЙ навык (до всякого движения) получил
			//   5270 «каст пошёл», а сразу после первого 4006 все заявки стали result=24 навсегда.
			// ★★★ 16.09.2026 (ревизия 4062, КРИТ — тот же дефект жил и здесь): кэш скорости ТОЛЬКО ДЛЯ СВОЕГО.
			//   Чужая скорость (маунт, дебаф) перетирала LastServerSpeed, а он кормит моё движение.
			const bool bSelfMove = AsdaSelf::IsSelfUserIdx(this, (int32)Uidx);
			if (bSelfMove && Spd > 0.0001f && Spd < 5.f) { LastServerSpeed = Spd; }
			// ★ 27.09.2026: подтип хода @+3 (эталон `moveSubType = *(__int16*)(pkt+3)`, грант исполняется при 1..2)
			//   и renderId цели хода @+25 (эталон `HIWORD(targetRenderId) = *(_WORD*)(pkt+25)`, 0xF3C2F1). Оба ЗНАКОВЫЕ
			//   i16: сервер эхом возвращает поля нашего 4006 (`CharMove@0x544EB0` копирует тело целиком), а там
			//   «нет цели» = 0xFFFF → −1. Тело 27 байт — последнее поле @+25..26 внутри проверенной длины.
			const int32 SubType = (int32)AsdaBytes::RdI16(&B[3]);
			const int32 MoveTgtRid = (int32)AsdaBytes::RdI16(&B[25]);
			UE_LOG(LogAsdaNet, Log, TEXT("←%u move: result=%d uidx=%d cur=(%.0f,%.0f) dst=(%.0f,%.0f) speed=%.4f sub=%d tgt=%d%s"),
				Pkt.Opcode, Res, (int32)Uidx, Cx, Cz, Dx, Dz, Spd, SubType, MoveTgtRid, bSelfMove ? TEXT(" [СВОЙ]") : TEXT(""));
			// Журнал боя: свой ход к цели / свой стоп-коррекция / любой не-1 код своего хода — событие; прочее (соседи, ход по земле) — уровень 2.
			if (bSelfMove && (MoveTgtRid >= 0 || Res != 1 || bStop))
			{
				ASDA_CTRACE(Res == 1 ? EAsdaCT::Recv : EAsdaCT::Refuse,
					TEXT("ПРИЁМ %d %s (Я): результат=%d%s cur=(%.0f,%.0f) dst=(%.0f,%.0f) скорость=%.4f подтип=%d цель rid=%d"),
					(int32)Pkt.Opcode, bStop ? TEXT("стоп/коррекция позиции") : TEXT("ход"), Res,
					Res == 1 ? TEXT("") : Res == 5 ? TEXT(" (Hack Position — сервер поправил)") : TEXT(" (ОТКАЗ хода)"),
					Cx, Cz, Dx, Dz, Spd, SubType, MoveTgtRid);
			}
			else
			{
				ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("ПРИЁМ %d %s uidx=%d%s: результат=%d cur=(%.0f,%.0f) dst=(%.0f,%.0f) скорость=%.4f цель rid=%d"),
					(int32)Pkt.Opcode, bStop ? TEXT("стоп") : TEXT("ход"), (int32)Uidx, bSelfMove ? TEXT(" (Я)") : TEXT(""), Res, Cx, Cz, Dx, Dz, Spd, MoveTgtRid);
			}
			OnEntityMove.Broadcast((int32)Uidx, Res, FVector2D(Cx, Cz), FVector2D(Dx, Dz), Spd, bStop, SubType, MoveTgtRid);
		}
		break;
	}
	// ★★★ 4062 0xFDE — ПОЗИЦИОННЫЙ СИНК СУЩНОСТИ (`netRecvEntityPosSync@0xF3C150`, срез game.c:551884;
	//   разбор — `client/logic/movement_pathing.md` §3.3). Опкод был объявлен, а обработчика НЕ БЫЛО:
	//   пакет молча терялся (хвост P4 из `Docs/Port/systems/COMBAT_TODO.md` §3).
	//   Раскладка эталона: [0] тип-байт (0 = снап/телепорт, иначе движение), +1 i16 userIdx,
	//   +13 i32 destX, +17 i32 destZ, +21 i32 moveParam.
	//   ★★★ ТИПЫ ПОЛЕЙ (исправлено 16.09.2026 по ревизии — сперва я читал их целыми, и это КРИТ).
	//   Поля +13/+17/+21 — FLOAT. Три доказательства: (а) диспетчер отдаёт 4062 и 4007 ОДИН И ТОТ ЖЕ указатель
	//   `&pktBuf[9]` (game.c:16156 и 16290), а в 4007 порт читает эти смещения как float и это работает на
	//   живом; (б) в теле 4062 сырой DWORD кладётся в float-тройку `Vector3_Set`, и `destWorldX` уже float —
	//   в `Recv_EntityMove` IDA пишет прямо `*(float *)(movePacket + 13)`; (в) поле +21 используется как
	//   `*(float *)&a9` (game.c:80446). Чтение целыми давало координаты порядка 1e9 — персонаж уносился за карту.
	//   ★★★ +21 — это СКОРОСТЬ (единиц/мс), а НЕ длительность. Прежний комментарий опирался на миснейм IDA
	//   `moveDuration` в `charMoveStepToCell@0xA1E860`: в эту переменную кладётся возврат `mathVec3SubLen`,
	//   то есть РАССТОЯНИЕ, и деление `(dest−cur)/moveDuration` — нормализация направления, а не «скорость из
	//   длительности»; настоящая длительность считается ниже как `sub_93DF54(скорость, расстояние)`.
	//   Сходится и по оффсетам: локальная ветка кладёт +21 в `field88` (52130), а тик подаёт `[52130]` девятым
	//   аргументом — тем же, что в `Recv_EntityMove` зовётся `moveSpeed = *(_DWORD*)(movePacket+21)`.
	//   ★ Канал: ОТДЕЛЬНЫЙ делегат OnEntityPosSync, а не OnEntityMove: у 4062 нет ни поля result, ни текущей
	//   точки. Тип-байт передаём КАК ЕСТЬ — решение «двигать/не двигать» принимает слушатель, потому что оно
	//   зависит от того, свой это персонаж или чужой (см. обработчик в мировой подсистеме).
	//   ★★★ 27.09.2026: ВЕТКА СЕРВЕРОМ НЕ ИСПОЛЬЗУЕТСЯ (это не «мёртвый код»: приёмник оставлен как эталон).
	//   Отправителя 4062 нет ни в одном живом серверном образе — доказано тремя методами: литералом в
	//   декомпилах, операндом 0FDEh в листинге IDA и полным проходом дизассемблера по всем исполняемым
	//   секциям GameServer/NpcServer/Relay/патч-DLL живого v2 (контроль 4007/4009 положительный).
	//   Реальную коррекцию позиции своему персонажу сервер шлёт иначе: 4007 result=5 (CharMove «Hack
	//   Position») и 4009 (CharStop «Revision Pos»). Разбор: `Reports/owner_0927/position_correction.md`.
	case OP_NET_RECV_ENTITY_POS_SYNC: // 4062 0xFDE
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 25, (int32)Pkt.Opcode))
		{
			const uint8 Kind = B[0];
			// ★ ЗНАКОВО: эталон `CharManager_FindByUserIdx(g_CharManager, *(__int16 *)(a2 + 1))`; закон проекта
			//   «charId (i32) ≠ userNum (i16)». Беззнаковое чтение дало бы 65535 на серверном −1.
			int16 Uidx = (int16)AsdaBytes::RdI16(B.data() + 1);   // ★ N6: `AsdaBytes`
			float Dx  = AsdaBytes::RdF32(B.data() + 13);
			float Dz  = AsdaBytes::RdF32(B.data() + 17);
			float Spd = AsdaBytes::RdF32(B.data() + 21);
			// ★★★ 16.09.2026 (ревизия, КРИТ): кэш серверной скорости — ТОЛЬКО ДЛЯ СВОЕГО. Эталон кладёт +21
			//   в поле 52130 лишь в self-ветке (game.c:551913); у чужого скорость уходит в charMoveStepToCell
			//   и нигде не запоминается. Без этой проверки пробегающий мимо игрок на маунте (0.49…0.66)
			//   перетирал LastServerSpeed, а он кормит МОЁ движение (Net_LastSpeed → WASD и клик-мув) —
			//   сервер считает шаги как dist/своя_скорость, отсюда рассинхрон и счётчик Hack Position.
			if (AsdaSelf::IsSelfUserIdx(this, (int32)Uidx) && Spd > 0.0001f && Spd < 5.f) { LastServerSpeed = Spd; }
			UE_LOG(LogAsdaNet, Log, TEXT("←4062 поз-синк: uidx=%d вид=%u dst=(%.0f,%.0f) speed=%.4f"),
				(int32)Uidx, Kind, Dx, Dz, Spd);
			ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("ПРИЁМ 4062 поз-синк uidx=%d вид=%d dst=(%.0f,%.0f) скорость=%.4f"), (int32)Uidx, (int32)Kind, Dx, Dz, Spd);
			// cur не передаётся — слушатель возьмёт текущую точку у актора (эталон тоже берёт её из сущности).
			OnEntityPosSync.Broadcast((int32)Uidx, Kind, FVector2D(Dx, Dz), Spd);
		}
		break;
	}
	// (6585 крылья — полноценный хендлер выше, OnWingState)
	// ═══ ДЕРЕВО НАВЫКОВ (окно SKILL_WIN 12000) ═══ реверс: Reports/skill_window_port.md §3
	// ★★★ 5465 `Recv_SkillCoolTime@0xE4B180` — ДВА блока по 30 записей 6 б в ОДНОМ теле:
	//   байты 0…179   — НАВЫКИ  (`a2 + 6*i`,       @0 i32 skillId,  @4 i16 остатокСек);
	//   байты 180…359 — ПРЕДМЕТЫ (`a2 + 6*j + 180`, @0 i32 itemCode, @4 i16 остатокСек), таблица 2.
	//   Оба цикла останавливаются на терминаторе −1 в поле кода. Живое тело = 366 б (лог 14.08,
	//   13.04.34) — то есть второй блок в пакете ЕСТЬ всегда, а не «иногда».
	// ★ 21.10.2026: 5274 «откат навыка истёк» (`pktHandlerCharFxStop@0xF4EDF0`, network.c:44974-44983): тело 2 или 4 б, читается
	//   только [0..1] i16 id. Основной вызов эталона (`0x93B5B5 → 0xE57C90`) пустой; при опции 30479 — значок по центру экрана.
	case OP_PKT_HANDLER_CHAR_FX_STOP:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 2, (int32)Pkt.Opcode)) { break; }
		const int32 SkillId = AsdaBytes::RdI16(&B[0]);
		UE_LOG(LogAsdaNet, Log, TEXT("←5274 откат навыка %d истёк (%d б)"), SkillId, (int32)B.size());
		OnSkillCooldownReady.Broadcast(SkillId);   // подписчик только кладёт запись (закон no-reentrant-net-callbacks)
		break;
	}
	case OP_NET_RECV_SKILL_COOL_TIME:
	{
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Total = (int32)B.size();
		constexpr int32 kBlock = 30 * 6;      // 180 б — размер одного блока (эталонное `i < 30`)
		const int32 SkillLen = FMath::Min(Total, kBlock);
		TArray<uint8> SkillBody; SkillBody.SetNumUninitialized(SkillLen);
		if (SkillLen > 0) { FMemory::Memcpy(SkillBody.GetData(), B.data(), SkillLen); }
		UE_LOG(LogAsdaNet, Log, TEXT("←5465 откаты: тело %d б → навыки %d б (%d записей), предметы %d б (%d записей)"),
			Total, SkillLen, SkillLen / 6,
			FMath::Max(0, FMath::Min(Total - kBlock, kBlock)), FMath::Max(0, FMath::Min(Total - kBlock, kBlock)) / 6);
#if ASDA_COMBAT_TRACE
		if (AsdaCombatTrace::IsOn(1))
		{
			// Журнал боя: активные откаты навыков (код:сек) до терминатора −1.
			FString Cd;
			for (int32 i = 0; i + 6 <= SkillLen; i += 6)
			{
				const int32 Code = AsdaBytes::RdI32(&B[i]);
				if (Code == -1) { break; }
				const int32 Sec = AsdaBytes::RdI16(&B[i + 4]);
				if (Sec > 0) { Cd += FString::Printf(TEXT("%s%d:%dс"), Cd.IsEmpty() ? TEXT("") : TEXT(" "), Code, Sec); }
			}
			ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 5465 откаты навыков/предметов: тело %d б, навыки с откатом: [%s]"), Total, *Cd);
		}
#endif
		OnSkillCooldowns.Broadcast(SkillBody);
		// ★ Блок предметов. Раньше его не читал никто — см. шапку `OnItemCooldowns`.
		if (Total > kBlock)
		{
			const int32 ItemLen = FMath::Min(Total - kBlock, kBlock);
			TArray<uint8> ItemBody; ItemBody.SetNumUninitialized(ItemLen);
			FMemory::Memcpy(ItemBody.GetData(), B.data() + kBlock, ItemLen);
			OnItemCooldowns.Broadcast(ItemBody);
		}
		break;
	}
	// ★ 5251 — ПОЛНЫЙ список записей дерева (N × 34 б), записи ДОБАВЛЯЮТСЯ.
	case OP_SKILL_WND_PARSE_SKILL_UPDATE_PACKET:
	{
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("←5251 список навыков: %d б (%d записей по 34)"),
			(int32)B.size(), (int32)B.size() / 34);
		OnSkillListFull.Broadcast(Body);
		break;
	}
	// ★ 6056 — те же 34-б записи, но с ЗАМЕНОЙ существующей записи того же skillCode.
	case OP_SKILL_WND_ON_SKILL_LIST_PACKET:
	{
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("←6056 обновление навыков: %d б (%d записей по 34)"),
			(int32)B.size(), (int32)B.size() / 34);
		OnSkillListUpdate.Broadcast(Body);
		break;
	}
	// ★ 5252 — компактный список выученного: 35 × 3 б [i16 skillCode][u8 уровень].
	case OP_NET_RECV_LEARNED_SKILL_LIST:
	{
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("←5252 выученные навыки: %d б (%d записей по 3)"),
			(int32)B.size(), (int32)B.size() / 3);
		OnSkillListCompact.Broadcast(Body);
		break;
	}
	// ★ 6057 — полный сброс списков навыков (тело пустое).
	case OP_SKILL_WND_RESET_ELEMENT_SLOTS:
		UE_LOG(LogAsdaNet, Log, TEXT("←6057 сброс дерева навыков (%d б)"), (int32)Pkt.Body.size());
		OnSkillTreeReset.Broadcast();
		break;
	// ★ 5462 — очистка 45 слотов + новое число очков (@0 i16).
	case OP_RECV_SKILL_TREE_RESET_SLOTS:
	{
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Pts = (NetNeedLen(B, 2, (int32)Pkt.Opcode)) ? AsdaBytes::RdI16(&B[0]) : 0;
		UE_LOG(LogAsdaNet, Log, TEXT("←5462 ПОЛНЫЙ СБРОС навыков: очков=%d"), Pts);
		// Эталон `skillTreeUiResetSlots_63D0@0xE563D0` начинается с `if (a2 <= 0) return 0;` —
		//   при неположительном числе очков пакет игнорируется целиком.
		if (Pts > 0) { SelfSkillPoints = Pts; OnSelfSkillPoints.Broadcast(Pts); }
		OnSkillSlotsReset.Broadcast(Pts);
		break;
	}
	// ★ 5464 — обновление очков + подпись дерева (@0 i16 очки, @2 u8 grade).
	case OP_RECV_SKILL_LEARN_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Pts   = (NetNeedLen(B, 2, (int32)Pkt.Opcode)) ? AsdaBytes::RdI16(&B[0]) : 0;
		const int32 Grade = (NetNeedLen(B, 3, (int32)Pkt.Opcode)) ? (int32)B[2] : 0;
		UE_LOG(LogAsdaNet, Log, TEXT("←5464 навыки пересчитаны под характеристику: очков=%d grade=%d"), Pts, Grade);
		// Эталон `skillUiOnLearnSkill_6590@0xE56590` → `skillUiUpdatePointText` (тот же CCharacter+0x443).
		//   ⚠ Гейт эталона `if (очки <= 0) return` — потребитель его повторяет, здесь поле не трогаем.
		if (Pts > 0) { SelfSkillPoints = Pts; OnSelfSkillPoints.Broadcast(Pts); }
		OnSkillsRealigned.Broadcast(Pts, Grade);
		break;
	}
	// ★ 5254 (111 б) — ответ на 5253 «вложить очко».
	case OP_SKILL_HANDLE_UPGRADE_RESPONSE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body; Body.SetNumZeroed(111);
		const int32 N = FMath::Min<int32>(111, (int32)B.size());
		if (N > 0) { FMemory::Memcpy(Body.GetData(), B.data(), N); }
		const int32 Code  = (int32)Body[0];
		const int32 Pts   = AsdaBytes::RdI16(&Body[1]);
		const int32 Code2 = AsdaBytes::RdI16(&Body[3]);
		UE_LOG(LogAsdaNet, Log, TEXT("←5254 очко навыка: код=%d очков=%d skill=%d уровень=%d (тело %d б)"),
			Code, Pts, Code2, (int32)Body[5], (int32)B.size());
		// Эталон `skillHandleUpgradeResponse@0xE55420` при коде 1 зовёт `skillUiUpdatePointText(тело+1)`.
		if (Code == 1) { SelfSkillPoints = Pts; OnSelfSkillPoints.Broadcast(Pts); }
		// ★★★ ВТОРОЙ КАНАЛ СТАТ-БЛОКОВ (реверс `skill_buff.c:33292-33304`, отчёт `char_info_fix2.md` §5.2).
		//   Изучение/усиление навыка МЕНЯЕТ характеристики, и сервер везёт их прямо здесь, без
		//   отдельных 4060/4064: `respBody[6] == 1` — оба блока (36 б с +7 и 68 б с +43),
		//   `== 2` — только 68-байтный. Эталон обоими путями зовёт `UI_FillEquipDetailPanel`.
		//   Без этого окно 900 после вложенного очка показывало СТАРЫЕ числа до следующего 4060.
		if (N >= 111 && Body[6] == 1)
		{
			SetSelfBaseStatsFrom36(Body.GetData() + 7,  36, TEXT("5254"));
			SetSelfStatBlockFrom68(Body.GetData() + 43, 68, TEXT("5254"));
		}
		else if (N >= 111 && Body[6] == 2)
		{
			SetSelfStatBlockFrom68(Body.GetData() + 43, 68, TEXT("5254"));
		}
		OnSkillUpgradeResult.Broadcast(Body);
		break;
	}
	default: bHandled = false; break;
	}
}
