// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_RecvMisc.cpp — часть `AsdaNetSubsystem.cpp`: приёмник пакетов: наземный лут, PvP/дуэль, война кланов/городов, GM-ответы, торговля-отмена; хвост: горячие клавиши навыков, 5420…, 5236/5237, 6576.
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
// RankColor / SlotColor — таблицы каналов и рангов   ★ R3: комментарий снятого включения "UI/AsdaChatWindowSubsystem.h" (файл им не пользовался) — сохранён
// Дд6: FAsdaItemInstance::kRecordSize (57 б) — длина записи предмета   ★ R3: комментарий снятого включения "UI/AsdaInventorySubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaMessageSubsystem.h"    // 5055 отказ портала: нотисы 1096/1097 в системный чат
#include "UI/AsdaNotice.h"              // ★ 27.09: отказы 5055/6201 — плашка 221 и пара «плашка + журнал» (общие точки)
#include "UI/AsdaChatSubsystem.h"       // ★ 27.09: ColGold() = эталонный −11514 отказов перехода
// сохранение раскладки панели перед переходом (hotbar_SendAllLayouts)   ★ R3: комментарий снятого включения "UI/AsdaHotbarSubsystem.h" (файл им не пользовался) — сохранён
#include "World/AsdaPossessGate.h"      // ★ гейт действий при одержимости (SoulState==2), см. шапку заголовка
// ★ единый шлюз исходящих пакетов (E1, 14.09.2026)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
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

// ★ 08.11.2026 (пачка 19 баг 3): смерть → HP в ноль. Определения — AsdaNetSubsystem_Combat.cpp (ключи asda.Death.ZeroVitals / ZeroPeerHp).
void AsdaZeroSelfVitalsOnDeath(UAsdaNetSubsystem* Net, const TCHAR* Why);
bool AsdaDeathZeroPeerHpOn();

// ★ R2 (10.11.2026): приёмник области «Misc» — наземный лут, PvP/дуэль, война кланов/городов, GM-ответы, торговля-отмена; хвост: горячие клавиши навыков, 5420…, 5236/5237, 6576.
//   Группы `case` перенесены из `UAsdaNetSubsystem::HandlePacket` ДОСЛОВНО и в исходном порядке; `bHandled` = false только
//   в служебной ветке `default:` в конце — опкод не из этой области, диспетчер идёт дальше.
void UAsdaNetSubsystem::HandlePacket_Misc(const Asda::ParsedPacket& Pkt, bool& bHandled)
{
	bHandled = true;
	switch ((NetOpcode)Pkt.Opcode)
	{
	case OP_DROP_ITEM_MGR_SPAWN_GROUND_ITEM: // 5005 0x138D — батч наземного лута (записи 15б; loot_system_re.md §2)
	{
		const std::vector<uint8>& B = Pkt.Body;
		auto I16 = [&B](int32 O) { return AsdaBytes::RdI16(&B[O]); };
		auto I32 = [&B](int32 O) { return AsdaBytes::RdI32(&B[O]); };
		TArray<FAsdaLootDrop> Drops;
		for (int32 Off = 0; Off + 15 <= (int32)B.size(); Off += 15)
		{
			FAsdaLootDrop D;
			D.ItemCode   = I32(Off + 0);
			// ★ Аудит №3 (знаковость): эталон `DropItemMgr_SpawnGroundItem@0xF443D0` читает @4 как ОДИН
			//   `_DWORD` (ключ билборда) и раскладывает его в `DropItem_InitVisual` так:
			//   `CMap_GetTileAt(map, vec, (__int16)a3, SHIWORD(a3))` — ОБА тайла ЗНАКОВЫЕ.
			//   Поле @12 эталон берёт как `*(_WORD *)(dropEntry + 12)` вместе с байтом @14 и кладёт
			//   в 3-байтное упакованное значение (`LOWORD(a5)=@12`, `BYTE2(a5)=@14`) — как ЧИСЛО он его
			//   не использует, поэтому знак там ничего не значит. СЕРВЕР (`sub_421150@0x421150`,
			//   тело 15 б: int@0, __int16@4, __int16@6, int@8, __int16@12, char@14) шлёт это поле
			//   ЗНАКОВЫМ — чтение оставлено знаковым; байт @14 оригинал использует, порт пока нет.
			D.Tile       = FIntPoint(I16(Off + 4), I16(Off + 6));
			D.Count      = I32(Off + 8);
			D.DropperRid = I16(Off + 12);
			Drops.Add(D);
		}
		UE_LOG(LogAsdaNet, Log, TEXT("5005 лут-батч: %d предметов (%d б)"), Drops.Num(), (int32)B.size());
#if ASDA_COMBAT_TRACE
		if (AsdaCombatTrace::IsOn(1) && Drops.Num() > 0)
		{
			// Журнал боя: добыча с убитого — код, количество, тайл и rid «уронившего» каждой записи.
			FString Ls;
			for (const FAsdaLootDrop& D : Drops)
			{
				Ls += FString::Printf(TEXT("%s{предмет=%d ×%d тайл=(%d,%d) с rid=%d}"), Ls.IsEmpty() ? TEXT("") : TEXT(" "), D.ItemCode, D.Count, D.Tile.X, D.Tile.Y, D.DropperRid);
			}
			ASDA_CTRACE(EAsdaCT::Reward, TEXT("ПРИЁМ 5005 добыча на земле: %d шт: %s"), Drops.Num(), *Ls);
		}
#endif
		if (Drops.Num()) { OnLootSpawn.Broadcast(Drops); }
		break;
	}
	case OP_NET_HANDLE_ITEM_PICKUP_RESULT: // 5013 0x1395 — результат подбора (62б; loot_system_re.md §6)
	{
		bItemPickPending = false;   // ★ 09.10.2026: эталон первой строкой `sub_927560(g_pUIManager, 0)` (0xF45420), безусловно
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 3, (int32)Pkt.Opcode))
		{
			const uint8 Result = B[0];
			const int32 Picker = AsdaBytes::RdI16(&B[1]);
			const int32 Weight = (NetNeedLen(B, 62, (int32)Pkt.Opcode)) ? AsdaWeightFromWire(AsdaBytes::RdI16(&B[60])) : -1;   // ★ 29.10.2026: вес u16
			TArray<uint8> Body62 = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
			UE_LOG(LogAsdaNet, Log, TEXT("5013 подбор: result=%d picker=%d вес=%d"), Result, Picker, Weight);
			ASDA_CTRACE(Result == 1 ? EAsdaCT::Reward : EAsdaCT::Refuse, TEXT("ПРИЁМ 5013 подбор: результат=%d%s подобрал uidx=%d%s предмет=%d ×%d вес=%d"),
				(int32)Result, Result == 1 ? TEXT(" (успех)") : TEXT(" (ОТКАЗ)"), Picker,
				(Picker < 0 || AsdaSelf::IsSelfUserIdx(this, Picker)) ? TEXT(" (Я)") : TEXT(""),
				B.size() >= 7 ? AsdaBytes::RdI32(&B[3]) : -1, B.size() >= 16 ? AsdaBytes::RdI32(&B[12]) : -1, Weight);
			OnLootPickupResult.Broadcast(Result, Picker, Body62, Weight);
			// ★★★ ТЕКУЩИЙ вес: эталон `Net_HandleItemPickupResult@0xF45420` (network.c:42846+):
			//     if ( *(__int16*)(a1+1) < 0 || picker == свой userIdx )
			//         { звук charpickup; uiInventoryOnRecvItemAcquired; HUD_UpdateMiniWeightBar(*(__int16*)(a1+60)); }
			//   ⚠ `picker < 0` (сервер шлёт −1) = ЭТО И ЕСТЬ СВОЙ ПОДБОР. Прежнее условие
			//   `Picker == SelfUserNum` резало ровно этот случай, и вес после подбора не обновлялся
			//   (живой лог: «5013 подбор: result=1 picker=-1 вес=10277…11481» — значения приходили,
			//   но до полосы не доходили). Максимум пакет не несёт → −1 = «не менять».
			if (Result == 1 && Weight >= 0 && (Picker < 0 || AsdaSelf::IsSelfUserIdx(this, Picker)))
			{
				SetSelfWeight(Weight, -1, TEXT("5013 подбор"));
			}
		}
		break;
	}
	// ★★★ 5451 (68 б) `netRecvItemBuffApply@0xF23C60` — РЕЗУЛЬТАТ ПРИМЕНЕНИЯ ПРЕМИУМ-ПРЕДМЕТА (5450).
	//   Тело: @0 u8 КОД РЕЗУЛЬТАТА, @1 i16 userNum, @3 57 б запись предмета,
	//         @60 i16 текущий вес, @62 i32, @66 i16 максимальный вес.
	//   ⚠⚠ ИСПРАВЛЕН РАЗБОР (волна 2). Прежний код считал `Body[0]` «подтипом эффекта» и ждал 50 —
	//   но 50 это `case 50` ВЛОЖЕННОГО `switch (Table2Record->subType − 1)` внутри ветки успеха
	//   (`network.c:36541-36555`), то есть `special_itemlist.SubType == 51`, а внешний switch идёт
	//   по КОДУ РЕЗУЛЬТАТА (`network.c:36211`). Условие `Body[0] == 50` не выполнялось никогда,
	//   и максимальный вес по этому каналу не приходил ни разу. Теперь пакет целиком уходит
	//   подписчику (инвентарь: таблица нотисов + применение записи + откат), а вес правится ровно
	//   там же, где его правит эталон — в ветке успеха для подтипа 51.
	case OP_NET_RECV_ITEM_BUFF_APPLY:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 68) { UE_LOG(LogAsdaNet, Warning, TEXT("5451: тело %d б < 68"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(68);
		FMemory::Memcpy(Body.GetData(), B.data(), 68);
		UE_LOG(LogAsdaNet, Log, TEXT("5451 премиум-предмет: result=%d userNum=%d код=%d вес %d/%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[1]), AsdaBytes::RdI32(&Body[3]),
			AsdaBytes::RdI16(&Body[60]), AsdaBytes::RdI16(&Body[66]));
		ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 5451 результат премиум-предмета: код=%d userNum=%d предмет=%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[1]), AsdaBytes::RdI32(&Body[3]));
		OnFunctionItemResult.Broadcast(Body);
		break;
	}
	// ★★★ 4206 OP_RECV_USER_BATTLE_STATE — РЕЖИМ PvP (иконка MiniState_PvPMode 5300).
	//   Эталон netTradeRequestHandler@0xF4A190 (network.c:44087-44175), a1 = i16* тела:
	//     u8 @12 — команда: 1 = смена состояния игрока @0, 2/0 — только чат про цель.
	//     u8 @7  — подтип: 0 = ОТБОЙ (флаг buffFxSlots[160]=0, иконка гаснет, чат loc 108132);
	//              >0 = включить (флаг=1, иконка, чат loc 108114; при подтипе 2 — loc 108131).
	//   Клан-флаг (подтип 1 → uiClanFlagGetOrCreate) и ветки «цель» — система кампвара/цели,
	//   в мини-статусе этапа 1 не участвуют; здесь только сам факт режима.
	case OP_RECV_USER_BATTLE_STATE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 13, (int32)Pkt.Opcode))
		{
			// ★ Аудит №3 (знаковость): эталон `netTradeRequestHandler@0xF4A190` — `__int16 *a1`,
			//   `CharManager_FindByUserIdx(mgr, *a1)`; байты @6/@7 он берёт как `unsigned __int8`.
			const int32 Uidx = AsdaBytes::RdI16(&B[0]);
			const int32 Sub  = (int32)B[7];
			const uint8 Cmd  = B[12];
			if (Cmd == 1)
			{
				// ★★★ 17.10.2026 (Ф4) [ОРИГИНАЛ]: состояние режима по userIdx — построчно network.c:44099–44150 (разбор у `FAsdaPvpState`).
				FAsdaPvpState& St = PvpStates.FindOrAdd(Uidx);
				St.ArgByte = B[6];                                  // `[88]` = @6 — у всех
				if (IsSelfUserIdx(Uidx)) { St.bOn = Sub > 0; }      // `[160]` — только свой (`CClass_GetField47565`)
				St.Mode = (uint8)Sub;                               // `[92]` (у эталона — только свой; чужой — справочно)
				if (Sub == 1) { St.Rival = AsdaBytes::RdI32(&B[8]); }   // `[80]` — соперник дуэли
				if (Sub == 4) { St.Team = AsdaBytes::RdI32(&B[8]); }    // `[116]` — команда
				if (Sub == 0) { St.ArgByte = 0; St.Rival = -1; St.bOn = false; }   // отбой: `[88]`=0, `[80]`=−1, `[160]`=0
				UE_LOG(LogAsdaNet, Log, TEXT("4206 PvP-режим: userIdx=%d подтип=%d байт@6=%u @8=%d → %s"),
					Uidx, Sub, (uint32)B[6], AsdaBytes::RdI32(&B[8]), (Sub > 0) ? TEXT("ВКЛ") : TEXT("выкл"));
				ASDA_CTRACE(EAsdaCT::Pvp, TEXT("ПРИЁМ 4206 PvP-режим: uidx=%d%s подтип=%d%s байт@6=%d @8=%d → %s"),
					Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""), Sub,
					Sub == 0 ? TEXT("(отбой)") : Sub == 1 ? TEXT("(дуэль, соперник @8)") : Sub == 4 ? TEXT("(команда @8)") : TEXT(""),
					(int32)B[6], AsdaBytes::RdI32(&B[8]), (Sub > 0) ? TEXT("ВКЛ") : TEXT("выкл"));
				OnPvpMode.Broadcast(Uidx, Sub > 0, Sub);
			}
			else
			{
				// ★ 06.10.2026: [ОРИГИНАЛ] команды 0/2 — нотисы 108120/108119 своему персонажу; разбирает мини-статус.
				UE_LOG(LogAsdaNet, Log, TEXT("4206: команда=%u userIdx=%d (отказ смены режима боя)"), Cmd, Uidx);
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ПРИЁМ 4206 ОТКАЗ смены режима боя: команда=%d uidx=%d%s"), (int32)Cmd, Uidx,
					(Cmd == 0 || Cmd == 2) ? TEXT(" → OnPvpModeRefused") : TEXT(" (без реакции)"));
				if (Cmd == 0 || Cmd == 2) { OnPvpModeRefused.Broadcast(Uidx, (int32)Cmd); }
			}
		}
		break;
	}
	// ★★★ 17.10.2026 (Ф4): ОТВЕТЫ PvP-АТАКИ 4202 / 4204 — раскладка у `FAsdaPvpAttackPkt`. Здесь только разбор и делегат (закон
	//   no-reentrant-net-callbacks): показ, очередь, нотисы и стоп — мир (`UAsdaWorldSubsystem::OnPvpAttackStart/OnPvpHit`) и контроллер.
	case OP_NET_SKILL_HANDLE_STATE_SYNC:          // 4202
	case OP_NET_SKILL_HANDLE_SABZIL_STATE_SYNC:   // 4204
	{
		const bool b4202 = Pkt.Opcode == OP_NET_SKILL_HANDLE_STATE_SYNC;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, b4202 ? 53 : 49, (int32)Pkt.Opcode, TEXT("PvP-атака"))) { break; }
		FAsdaPvpAttackPkt P;
		P.SrcOpcode = (int32)Pkt.Opcode;
		const int32 OffDmg = b4202 ? 13 : 8, OffSk = b4202 ? 29 : 25, OffSkD = b4202 ? 37 : 33;   // смещения полей
		P.Code = b4202 ? (int32)B[0] : (int32)B[24];
		P.AttackerUserIdx = AsdaBytes::RdI16(&B[b4202 ? 1 : 0]);
		P.TargetUserIdx = AsdaBytes::RdI16(&B[b4202 ? 3 : 2]);
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 V = AsdaBytes::RdI32(&B[OffDmg + 4 * k]);
			if (V == 0) { break; }   // непустой префикс (эталонное «всё или ничего» — дефект, см. 4029)
			P.Dmg.Add(V);
		}
		for (int32 k = 0; k < 4; ++k)
		{
			P.SkillIds[k] = AsdaBytes::RdI16(&B[OffSk + 2 * k]);
			P.SkillDmg[k] = AsdaBytes::RdI32(&B[OffSkD + 4 * k]);
		}
		FString Q; for (const int32 V : P.Dmg) { Q += FString::Printf(TEXT("%d "), V); }
		UE_LOG(LogAsdaNet, Log, TEXT("%d PvP: код=%d атакующий=%d цель=%d удары=%d [%s] навык0=%d/%d"), (int32)Pkt.Opcode, P.Code,
			P.AttackerUserIdx, P.TargetUserIdx, P.Dmg.Num(), *Q, P.SkillIds[0], P.SkillDmg[0]);
#if ASDA_COMBAT_TRACE
		// Участник нашего боя (до строки): бьёт МЕНЯ или цель нашего PvP-цикла → атакующий наш.
		if (!IsSelfUserIdx(P.AttackerUserIdx) && P.AttackerUserIdx >= 0
			&& (IsSelfUserIdx(P.TargetUserIdx) || (P.TargetUserIdx >= 0 && P.TargetUserIdx == AsdaCombatTrace::CurrentCyclePlayer())))
		{
			AsdaCombatTrace::NoteEngagedUser(P.AttackerUserIdx);
		}
#endif
		// Канал общий (PvP по всему сектору): уровень 1 — если любой из участников наш (я/цель PvP-цикла/участник).
		ASDA_CTRACE_IF(AsdaCombatTrace::IsUserRelevant(P.AttackerUserIdx) || AsdaCombatTrace::IsUserRelevant(P.TargetUserIdx),
			EAsdaCT::Pvp, TEXT("ПРИЁМ %d PvP-%s: код=%d атакующий uidx=%d%s → цель uidx=%d%s очередь=%s навыки=[%d:%d,%d:%d,%d:%d,%d:%d]"),
			(int32)Pkt.Opcode, b4202 ? TEXT("атака (начало серии)") : TEXT("удар"), P.Code,
			P.AttackerUserIdx, IsSelfUserIdx(P.AttackerUserIdx) ? TEXT(" (Я)") : TEXT(""),
			P.TargetUserIdx, IsSelfUserIdx(P.TargetUserIdx) ? TEXT(" (Я)") : TEXT(""), *AsdaCtQueue(P.Dmg),
			P.SkillIds[0], P.SkillDmg[0], P.SkillIds[1], P.SkillDmg[1], P.SkillIds[2], P.SkillDmg[2], P.SkillIds[3], P.SkillDmg[3]);
		if (b4202) { OnPvpAttackStart.Broadcast(P); } else { OnPvpHit.Broadcast(P); }
		break;
	}
	// ★ 06.10.2026 (волна 2-В): ДУЭЛЬ 4302/4303/4304/4305 — диспетчер эталона network.c:2077-2093
	//   (4302 → `Recv_PartyMember(g_pQuickSlotBar, 14, pkt)`, 4303 → `PvpDuel_OnNoticePacket`,
	//   4304 → `PvpDuel_ApplyMatchResult`, 4305 → `CDuelMgr_CreateDuelArena`, всем — `&pktBuf[9]` = тело).
	//   [ПРОТОКОЛ] длины тел — по серверу (`Recv_UserPvpReq@0x5578F0` 34 б, `sub_4C4340` 33 б, `PvpResult@0x4C3670` 55 б,
	//   `sub_558490` 14 б). Разбор — `UAsdaDuelSubsystem`; здесь только проверка длины, копия и делегат.
	case 4302: case 4303: case 4304: case 4305:
	{
		const int32 Need = (Pkt.Opcode == 4302) ? 34 : (Pkt.Opcode == 4303) ? 33 : (Pkt.Opcode == 4304) ? 55 : 14;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, Need, (int32)Pkt.Opcode, TEXT("дуэль"))) { break; }
		TArray<uint8> Body; Body.SetNumUninitialized(Need);
		FMemory::Memcpy(Body.GetData(), B.data(), Need);
		UE_LOG(LogAsdaNet, Log, TEXT("←%u дуэль: тело %d б"), (uint32)Pkt.Opcode, (int32)B.size());
		ASDA_CTRACE(EAsdaCT::Pvp, TEXT("ПРИЁМ %d дуэль — %s: тело %d б, @0=%d @1(i16)=%d"), (int32)Pkt.Opcode,
			Pkt.Opcode == 4302 ? TEXT("вызов") : Pkt.Opcode == 4303 ? TEXT("ответ/нотис") : Pkt.Opcode == 4304 ? TEXT("итог поединка") : TEXT("арена"),
			(int32)B.size(), (int32)B[0], AsdaBytes::RdI16(&B[1]));
		OnDuelPacket.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}
	// ★ 06.10.2026 (волна 2-Г): ГОРОД И СВИТКИ — диспетчер эталона (network.c:609/612 и :1081; всем — `&pktBuf[9]` = тело).
	//   [ПРОТОКОЛ] длины тел — по серверу: 6745 `sub_4C28F0(…, 6745, 2, …)` (`User_Game_TaxChange`/`sub_46B570`),
	//   6135 `(…, 6135, 62, …)` (`RecvClient_FunctionItem_Summon`/`sub_4F3230`), 6136 `(…, 6136, 62, …)` (`sub_4F2EA0`).
	//   Разбор — `UAsdaTownWarSubsystem` (6745) и `UAsdaScrollCallSubsystem` (6135/6136); здесь только длина, копия, делегат.
	case OP_C_CHAT_SHOW_ITEM_SUBNOTICE:
	case OP_NET_RECV_INVENTORY_ITEM_UPDATE:
	case OP_CAMP_WAR_BUILD_RESULT_TOOLTIP:
	{
		const int32 Need = (Pkt.Opcode == OP_C_CHAT_SHOW_ITEM_SUBNOTICE) ? 2 : 62;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, Need, (int32)Pkt.Opcode, TEXT("город/свитки"))) { break; }
		TArray<uint8> Body; Body.SetNumUninitialized(Need);
		FMemory::Memcpy(Body.GetData(), B.data(), Need);
		UE_LOG(LogAsdaNet, Log, TEXT("←%u город/свитки: тело %d б, код=%u"), (uint32)Pkt.Opcode, (int32)B.size(),
			(uint32)Body[Pkt.Opcode == OP_C_CHAT_SHOW_ITEM_SUBNOTICE ? 0 : (Pkt.Opcode == OP_NET_RECV_INVENTORY_ITEM_UPDATE ? 4 : 0)]);
		OnWave2DPacket.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}
	// ★ 09.11.2026 (пачка 20 баг 8): СМЕНА ИМЕНИ — диспетчер эталона: 6139 `itemTooltipBuildOrNotice(&pktBuf[9])`,
	//   6141 `recvItemActionResult_ShowNotice(&pktBuf[9])` (AsdaStory_game.c:14382-14387). [ПРОТОКОЛ] длины — по серверу:
	//   6139 `sub_4C28F0(…, 6139, 5, …)` (`sub_55DCB0`), 6141 `(…, 6141, 86, …)` (`RecvClient_CharNameChange`/`sub_55DD50`).
	//   Разбор — `UAsdaCharRenameSubsystem`; здесь только длина, копия, делегат.
	case OP_ITEM_TOOLTIP_BUILD_OR_NOTICE:
	case OP_RECV_ITEM_ACTION_RESULT_SHOW_NOTICE:
	{
		const int32 Need = (Pkt.Opcode == OP_ITEM_TOOLTIP_BUILD_OR_NOTICE) ? 5 : 86;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, Need, (int32)Pkt.Opcode, TEXT("смена имени"))) { break; }
		TArray<uint8> Body; Body.SetNumUninitialized(Need);
		FMemory::Memcpy(Body.GetData(), B.data(), Need);
		UE_LOG(LogAsdaNet, Log, TEXT("←%u смена имени: тело %d б, код=%u"), (uint32)Pkt.Opcode, (int32)B.size(),
			(uint32)Body[Pkt.Opcode == OP_ITEM_TOOLTIP_BUILD_OR_NOTICE ? 4 : 24]);
		OnWave2DPacket.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}
	// ★ 30.10.2026 (выбор армии, кнопка NPC 30056): диспетчер эталона — `Recv_ClanInfo(g_pCampWarClan, &pktBuf[9])` (6701) и
	//   `Recv_ClanCreateResult(g_pCampWarClan, &pktBuf[9])` (6703). [ПРОТОКОЛ] длины — по серверу: 6701 `sub_4C28F0(…, 6701, 44, …)`
	//   (`sub_461FA0`), 6703 `(…, 6703, 3, …)` (`User_Game_SelectSquad@0x462480`, `sub_4626C0`). Разбор — `UAsdaFactionSelectSubsystem`.
	//   ★ 30.10.2026 (CampWar целиком) — тем же делегатом регистрация «Битвы армий» (разбор — `UAsdaCampWarSubsystem`,
	//   `Reports/faction_select/REG_SPEC.md` §3): 6705 (58 б, `Recv_ClanStorageItem`, сброс армии), 6707 (7 б,
	//   `Recv_ClanRankResult`), 6709 (2 б, `Recv_ClanKickResult`), 6710 (5 б, `Recv_ClanMemberPoint`), 6711 (5 б,
	//   `Recv_ClanApplyResult`), 6740 (2 б, `uiCampWarRankingResultHandler`) — длины по серверу (`sub_4C28F0(…, op, N, …)`);
	//   6715 (`Recv_ClanMemberListBulk`) — переменной длины, записи по 119 б.
	//   ★ 30.10.2026 (боевая фаза, `Reports/faction_select/BATTLE_SPEC.md` §4) — тем же делегатом бой на карте 19 (разбор —
	//   `UAsdaCampWarBattleSubsystem`, туда же 6715/6737 — одна очередь; 6738 — `UAsdaCampWarSubsystem`). Длины — по серверу:
	//   6716 0, 6720 12, 6721 26, 6724 7, 6725 11, 6726 11, 6728 3, 6729 12, 6730 10, 6732 65, 6733 235, 6735 7, 6736 6, 6737 0,
	//   6738 1 (серверного отправителя нет), 6739 9, 6741 7, 6742 6. 6727 (конец списка точек, 0 б) у эталона в диспетчере
	//   не разбирается (switch 0x1A2D…0x1A4D без case 0x1A47) — не маршрутизируется. 6719 — чат (ниже), 6722 — `OnPeerFaction`.
	case OP_CLAN_DRAW_MEMBER_LIST:
	case OP_CLAN_MEMBER_ONLINE:
	case OP_CLAN_MEMBER_UPDATE:
	case OP_C_CAMP_WAR_ON_OBJECT_EFFECT_PACKET:
	case OP_QUICK_SLOT_ON_UPDATE_PACKET:
	case OP_CAMP_WAR_SET_OBJECTIVE_SLOT:
	case OP_CAMP_WAR_HANDLE_STATE_NOTICE:
	case OP_UI_PANEL_SHOW_WITH3:
	case OP_FLOATING_TEXT_SPAWN_VALUE_OVER_CHAR:
	case OP_RECV_CAMP_WAR_SYNTH_RESULT_REFRESH:
	case OP_ON_RECV_HONOR_AND_INVENTORY_UPDATE:
	case 6735:   // `clanMember_HandleResultCode@0xCA6E60` (в перечне опкодов имени нет)
	case OP_CAMP_WAR_UPDATE_COUNTDOWN:
	case OP_C_CAMP_WAR_RESET_STATE:
	case OP_CAMP_WAR_SET_FACTION_FLAG_TOOLTIP:
	case OP_CAMP_WAR_RANKING_APPLY:
	case OP_UI_TOGGLE_BY_FLAG_BYTE:
	case OP_CAMP_WAR_UPDATE_SCORE_LABELS:
	case OP_CLAN_STORAGE_ITEM:
	case OP_CLAN_RANK_RESULT:
	case OP_CLAN_KICK_RESULT:
	case OP_CLAN_MEMBER_POINT:
	case OP_CLAN_APPLY_RESULT:
	case OP_UI_CAMP_WAR_RANKING_RESULT_HANDLER:
	case OP_CLAN_MEMBER_LIST_BULK:
	case OP_CLAN_INFO:
	case OP_CLAN_CREATE_RESULT:
	{
		int32 Need = 0;
		switch (Pkt.Opcode)
		{
		case OP_CLAN_INFO:                          Need = 44; break;
		case OP_CLAN_CREATE_RESULT:                 Need = 3;  break;
		case OP_CLAN_STORAGE_ITEM:                  Need = 58; break;
		case OP_CLAN_RANK_RESULT:                   Need = 7;  break;
		case OP_CLAN_KICK_RESULT:                   Need = 2;  break;
		case OP_CLAN_MEMBER_POINT:                  Need = 5;  break;
		case OP_CLAN_APPLY_RESULT:                  Need = 5;  break;
		case OP_UI_CAMP_WAR_RANKING_RESULT_HANDLER: Need = 2;  break;
		case OP_CLAN_DRAW_MEMBER_LIST:              Need = 0;   break;
		case OP_CLAN_MEMBER_ONLINE:                 Need = 12;  break;
		case OP_CLAN_MEMBER_UPDATE:                 Need = 26;  break;
		case OP_C_CAMP_WAR_ON_OBJECT_EFFECT_PACKET: Need = 7;   break;
		case OP_QUICK_SLOT_ON_UPDATE_PACKET:        Need = 11;  break;
		case OP_CAMP_WAR_SET_OBJECTIVE_SLOT:        Need = 11;  break;
		case OP_CAMP_WAR_HANDLE_STATE_NOTICE:       Need = 3;   break;
		case OP_UI_PANEL_SHOW_WITH3:                Need = 12;  break;
		case OP_FLOATING_TEXT_SPAWN_VALUE_OVER_CHAR: Need = 10; break;
		case OP_RECV_CAMP_WAR_SYNTH_RESULT_REFRESH: Need = 65;  break;
		case OP_ON_RECV_HONOR_AND_INVENTORY_UPDATE: Need = 235; break;
		case 6735:                                  Need = 7;   break;
		case OP_CAMP_WAR_UPDATE_COUNTDOWN:          Need = 6;   break;
		case OP_C_CAMP_WAR_RESET_STATE:             Need = 0;   break;
		case OP_CAMP_WAR_SET_FACTION_FLAG_TOOLTIP:  Need = 1;   break;
		case OP_CAMP_WAR_RANKING_APPLY:             Need = 9;   break;
		case OP_UI_TOGGLE_BY_FLAG_BYTE:             Need = 7;   break;
		case OP_CAMP_WAR_UPDATE_SCORE_LABELS:       Need = 6;   break;
		default:                                    Need = FMath::Max(1, (int32)Pkt.Body.size()); break;   // 6715 — всё тело
		}
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, Need, (int32)Pkt.Opcode, TEXT("выбор армии"))) { break; }
		TArray<uint8> Body; Body.SetNumUninitialized(Need);
		if (Need > 0) { FMemory::Memcpy(Body.GetData(), B.data(), Need); }
		// ★ 30.10.2026: у 6716/6737 тела нет — `Body[0]` не читаем.
		UE_LOG(LogAsdaNet, Log, TEXT("←%u битва армий: тело %d б, @0=%u"), (uint32)Pkt.Opcode, (int32)B.size(), Need > 0 ? (uint32)Body[0] : 0u);
		OnCampWarSquadPacket.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}
	// ★ 4053 0x0FD5 — СМЕРТЬ персонажа (Recv_CharDeath_Apply@0xF42E50; сервер SendCharDead@0x4CB480
	//   рассылает всем в секторе, включая умершего). Тело 4б: i16 userIdx, i16 killerIdx.
	case OP_RECV_CHAR_DEATH:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 4, (int32)Pkt.Opcode))
		{
			const int32 UserIdx   = AsdaBytes::RdI16(&B[0]);
			const int32 KillerIdx = AsdaBytes::RdI16(&B[2]);
			const bool  bSelf     = AsdaSelf::IsSelfUserIdx(this, UserIdx);   // E2
			UE_LOG(LogAsdaNet, Warning, TEXT("4053 СМЕРТЬ: userIdx=%d (свой=%s) убийца=%d — сервер держит CUser+79=1 и режет ВСЕ действия до 4054"),
				UserIdx, bSelf ? TEXT("ДА") : TEXT("нет"), KillerIdx);
			// Канал общий (смерть любого игрока сектора): уровень 1 — я, наш игрок (UID) или убил я.
			ASDA_CTRACE_IF(bSelf || AsdaCombatTrace::IsUserRelevant(UserIdx) || IsSelfUserIdx(KillerIdx),
				EAsdaCT::Death, TEXT("ПРИЁМ 4053 СМЕРТЬ персонажа: uidx=%d%s убийца=%d%s (клип падения %s)"),
				UserIdx, bSelf ? TEXT(" (Я)") : TEXT(""), KillerIdx, IsSelfUserIdx(KillerIdx) ? TEXT(" (Я)") : TEXT(""),
				bSelf ? TEXT("свой, играем") : TEXT("на акторе соседа"));
			if (bSelf)
			{
				// Гибель «на глазах» → клип падения играем. bSelfDead взводит EnterSelfDeadState.
				bSelfDeathNoClip = false;
				if (!bSelfDead)
				{
					bSelfDead = true;
					UE_LOG(LogAsdaNet, Warning, TEXT("[смерть] 4053 (гибель в игре) → состояние СМЕРТИ, клип падения играем"));
				}
			}
			// ★ Сам Recv_CharDeath_Apply@0xF42E50 (network.c:42584) HP не пишет, но ставит СОСТОЯНИЕ 6, а оно
			//   (`CCharacter_SetAction` case 6, entity_char.c:16913; покадрово ui_core.c:75482) обнуляет своё HP/MP.
			// ★ 08.11.2026 пачка 19 баг 3: до правки HP оставалось последним положительным (убивающий удар,
			//   превышающий остаток, эталон не пишет) — «умер, а полоса 1282/15277». Ключ asda.Death.ZeroVitals.
			if (bSelf) { AsdaZeroSelfVitalsOnDeath(this, TEXT("4053")); }
			// ★ [ИСПРАВЛЕНИЕ] 08.11.2026 asda.Death.ZeroPeerHp: мёртвый ДРУГОЙ игрок — HP 0 (эталон соседу HP не трогает).
			else if (AsdaDeathZeroPeerHpOn())
			{
				const FAsdaVitals* PV = GetVitals(UserIdx);
				if (PV && PV->HpCur > 0)
				{
					ASDA_CTRACE_IF(AsdaCombatTrace::IsUserRelevant(UserIdx) || IsSelfUserIdx(KillerIdx), EAsdaCT::Death,
						TEXT("смерть игрока uidx=%d: HP %d→0 (asda.Death.ZeroPeerHp, [ИСПРАВЛЕНИЕ])"), UserIdx, PV->HpCur);
					SetVitalsHp(UserIdx, 0, -1, TEXT("4053 смерть соседа"), /*bServerSnapshot=*/false);
				}
			}
			OnCharDeath.Broadcast(UserIdx, KillerIdx);   // чужие смерти тоже идут сюда (клип на их акторе)
		}
		break;
	}
	// ★★ 6093 0x17CD — ПРЕДЛОЖЕНИЕ ВОСКРЕШЕНИЯ ОТ ДРУГОГО ИГРОКА (сервер sub_52B070@0x52B070 шлёт
	//   МЁРТВОМУ, когда кто-то применил на него скилл/предмет воскрешения). Тело 11 б:
	//     [0..1] i16 мой userNum, [2..5] i32 мой UserIndex, [6..7] i16 userNum инициатора,
	//     [8] u8 Type (0 = полное HP/MP, 2 = процент, 3 = 39 %), [9..10] i16 процент.
	//   Клиент-эталон: netFriendListAddFromPacket@0xF43C70 (имя авто-генерённое, по факту это
	//   DeathMenu_AddOfferOption) → friendListAddEntry@0xD6D740 (social.c:5237) добавляет в меню смерти
	//   опцию 0/1/2 с текстом loc 2372 / 2380 / 2371 и взводит отсчёт 300 000 мс.
	case OP_RECV_CHAR_RESURRECTION_CONFIRM:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 11, (int32)Pkt.Opcode))
		{
			const int32 Caster = AsdaBytes::RdI16(&B[6]);
			const uint8 Type   = B[8];
			const int32 Pct    = AsdaBytes::RdI16(&B[9]);
			UE_LOG(LogAsdaNet, Warning, TEXT("6093 ПРЕДЛОЖЕНИЕ ВОСКРЕШЕНИЯ: инициатор userIdx=%d тип=%u процент=%d"),
				Caster, Type, Pct);
			ASDA_CTRACE(EAsdaCT::Recv, TEXT("ПРИЁМ 6093 предложение воскрешения: инициатор uidx=%d тип=%d%s процент=%d"),
				Caster, (int32)Type, Type == 0 ? TEXT("(полное)") : Type == 2 ? TEXT("(процент)") : Type == 3 ? TEXT("(39 %)") : TEXT(""), Pct);
			OnResurrectOffer.Broadcast(Caster, Type, Pct);
		}
		break;
	}
	// ★ 6001 0x1771 — эхо серверной проверки состояния (sub_52BFF0 IsCheck_CharFlag). Тело 3б:
	//   i16 опкод, u8 причина (0=ОК, 1=персонаж МЁРТВ, 2..5=прочие блокировки). Диагностический канал:
	//   именно он объясняет «5013 result=0» и «4007 result=0» без единой догадки.
	case OP_ACTION_FLAG_CHECK:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 3, (int32)Pkt.Opcode))
		{
			const int32 CheckedOp = (int32)AsdaBytes::RdU16(&B[0]);
			const uint8 Reason    = B[2];
			if (Reason != 0)
			{
				static const TCHAR* kWhy[6] = { TEXT("ОК"), TEXT("персонаж МЁРТВ"), TEXT("блокировка 2"),
					TEXT("блокировка 3"), TEXT("блокировка 4"), TEXT("блокировка 5") };
				UE_LOG(LogAsdaNet, Warning, TEXT("6001 сервер ОТКЛОНИЛ опкод %d: причина %u (%s)"),
					CheckedOp, Reason, kWhy[FMath::Min<uint8>(Reason, 5)]);
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ПРИЁМ 6001 сервер ОТКЛОНИЛ опкод %d: причина %d (%s)"),
					CheckedOp, (int32)Reason, kWhy[FMath::Min<uint8>(Reason, 5)]);
			}
			// ★★ СТРАХОВОЧНЫЙ ТРИГГЕР: причина 1 = «персонаж МЁРТВ» на СЕРВЕРЕ. Если клиент считает
			//   себя живым — это рассинхрон (пропущен 4053 / вошли мёртвыми до готовности павна).
			//   Переводим в смерть без клипа падения: гибель уже произошла, показывать её поздно.
			if (Reason == 1 && !bSelfDead)
			{
				EnterSelfDeadState(TEXT("рассинхрон по 6001 (сервер считает нас мёртвыми)"), /*bAlreadyDead*/true);
			}
			OnActionBlocked.Broadcast(CheckedOp, Reason);
			// ★ R5-fix8 (14.11.2026): эхо проверки на свой удар по мобу — часам атаки (заявка прошла проверку либо отклонена без
			//   боевого ответа; разбор — `asda.Combat.AwaitReplyByTarget`).
			if (CheckedOp == 4026 || CheckedOp == 4028)
			{
				if (UAsdaAttackRhythmSubsystem* RhC = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { RhC->NoteServerCheck(CheckedOp, Reason); }
			}
		}
		break;
	}
	// ★★★ [ОРИГИНАЛ] 6006 0x1776 — ОТВЕТ сервера на нашу заявку 6005 «скрыться»
	//   (`localPlayerInputToggleByFlags@0xF49BD0`, network.c:44004; диспетчер клиента network.c:422).
	//   Тело 2 б: [0] 1 = команда принята, 0 = ОТКАЗ; [1] 1 = теперь скрыт, 0 = теперь виден.
	//   Эталон при [0]==0 не делает НИЧЕГО — ни нотиса, ни звука (проверено по телу функции:
	//   ни UI_FormatLocalizedNotice, ни CChat_AddTokenizedLines в ней нет). [ПОРТ] отказ пишем в лог
	//   и отдаём подписчикам — иначе GM не понял бы, почему кнопка «не сработала».
	//   ★ Своя невидимость приходит ТОЛЬКО этим пакетом: рассылка 4025 идёт через
	//   `SendPacket_ClippingSectorInOtherUser` и отправителя ПРОПУСКАЕТ (закон self-equip-not-broadcast).
	case OP_LOCAL_PLAYER_INPUT_TOGGLE_BY_FLAGS:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 2, (int32)Pkt.Opcode)) { break; }
		const bool bAccepted = (B[0] != 0);
		const bool bHidden   = (B[1] == 1);   // эталон сравнивает именно с 1, а не «!= 0»
		GmHideLatch.Disarm();                // заявка отработана — «ждём» снимаем в любом исходе
		++GmHideAnswerSeq;                   // ★ античит А2 (мера 7): ответ пришёл — ждущий запрос GM-меню его увидит
		if (bAccepted)
		{
			bOperatorConfirmed6006 = true;   // ★ античит А2 (мера 7): сервер пропустил операторскую команду — признак оператора
			bGmHidden = bHidden;
			bGmHiddenFromEntry = false;   // пришло эхо 6006 — состояние подтверждено сервером
			SaveGmHidden(bHidden);        // ★ [ПОРТ] 18.10.2026: последнее подтверждённое состояние — на следующие входы
			bGmRestoreInFlight = false;
			UE_LOG(LogAsdaNet, Log, TEXT("6006 GM-скрыться: сервер ПРИНЯЛ, состояние = %s"),
				bHidden ? TEXT("СКРЫТ") : TEXT("ВИДЕН"));
		}
		else
		{
			// ★ 18.10.2026 (ревизия, ВАЖНО): отказ на 6005 ОТ ВОССТАНОВЛЕНИЯ — повторить позже (сервер отказывает и пока
			//   персонаж в действии/движении), пока не исчерпан потолок попыток.
			if (bGmRestoreInFlight && GmRestoreAttempts < 10 && bGmHidden)
			{
				bGmRestoreVisiblePending = true;
				GmRestoreRetrySec = 1.0f;
			}
			bGmRestoreInFlight = false;
			// Состояние НЕ меняем: отказ значит «на сервере всё как было».
			UE_LOG(LogAsdaNet, Warning, TEXT("6006 GM-скрыться: сервер ОТКАЗАЛ (нет прав оператора: "
				"UserIndex вне 0…5000, либо персонаж не в игровом состоянии — в т.ч. идёт/занят действием). Остаёмся %s"),
				bGmHidden ? TEXT("СКРЫТЫ") : TEXT("ВИДИМЫ"));
		}
		OnGmHideResult.Broadcast(bAccepted, bGmHidden);
		break;
	}
	// ★ ac0r (12.10.2026, античит): 6037 0x1795 — ответ сервера на пробу прав 6036 GetValue.
	//   Сервер шлёт его ТОЛЬКО отправителю 6036 и только если операторский гейт прошёл (`sub_52ECF0`, под P47 ещё
	//   GM-флаг) — значит, сам факт ответа и есть подтверждение прав, как 6006 «принято». Тело 135 б (характеристики).
	//   [ПОРТ] Эталон печатает тело в чат (`chatDumpCharacterStats@0xF4B260`), но розничный клиент 6036 не шлёт вовсе
	//   (у отправителя `0xA0B550` вызовов нет) — у нас 6036 только проба прав, поэтому в чат ничего не выводим.
	case OP_CHAT_DUMP_CHARACTER_STATS:
	{
		++GmRightsAnswerSeq;
		bGmRightsProbeUnanswered = false;   // явный ответ — новая проба снова допустима
		bOperatorConfirmed6006 = true;      // признак оператора на сессию (тот же, что ставит 6006 «принято»)
		UE_LOG(LogAsdaNet, Log, TEXT("6037 ответ на пробу прав 6036 (%d б): сервер подтвердил права оператора"),
			(int32)Pkt.Body.size());
		// ★ 19.10.2026: если оператор сам запросил «Мои характеристики» (вкладка «Команды оригинала»), тело печатает
		//   в чат `UAsdaGmOriginalSubsystem` — как эталон `chatDumpCharacterStats@0xF4B260`. Проба прав здесь не меняется.
		OnGmOriginalPacket.Broadcast((int32)Pkt.Opcode, NetBodyToArray(Pkt.Body));
		break;
	}
	// ★★★ 19.10.2026: ОТВЕТЫ ШТАТНЫХ ОПЕРАТОРСКИХ КОМАНД ОРИГИНАЛА (до этого порт их не разбирал вовсе):
	//   6019 кик цели (`netShowDisconnectPopupAndChatNotice@0xF49A00`), 6023 «Служба Поддержки убивает тебя»
	//   (`netHandleAutoResurrectReady@0xF49550`), 6027 «…переместила тебя» (`uiShowLocalizedNotice2118@0xF494D0`),
	//   6034 карточка монстра (`ItemMixResult_DumpToChat`), 6043 итог выдачи предмета оператору (`ItemPop_ShowTransferResult`),
	//   6044 предмет получателю (`netHandleItemSlotUpdate@0xF4C100`), 6075 проценты «оператор против цели»
	//   (`Debug_DumpTargetCombatStats@0xA124E0`). Разбор — `Reports/gm_original_commands/group1..3_spec.md`.
	//   Здесь только пересылка подписчику `UAsdaGmOriginalSubsystem` (чат, нотисы, запись предмета, закрытие связи по кику —
	//   всё отложенно, из его Tick: закон no-reentrant-net-callbacks).
	case OP_NET_SHOW_DISCONNECT_POPUP_AND_CHAT_NOTICE:
	case OP_NET_HANDLE_AUTO_RESURRECT_READY:
	case OP_UI_SHOW_LOCALIZED_NOTICE2118:
	case OP_ITEM_MIX_RESULT_DUMP_TO_CHAT:
	case OP_ITEM_POP_SHOW_TRANSFER_RESULT:
	case OP_NET_HANDLE_ITEM_SLOT_UPDATE:
	case OP_DEBUG_DUMP_TARGET_COMBAT_STATS:
	// ★ 19.10.2026 (группа 4, `group4_client_handlers.md`): штатные пакеты операторских команд у ЦЕЛИ/охвата —
	//   6025 «Служба Поддержки воскрешает тебя», 6058 сброс откатов навыков, 6145 обратный отсчёт, 6147 блок карты.
	case OP_CHAR_ON_LOCAL_PLAYER_MAP_ENTRY:
	case OP_SKILL_HOTBAR_REFRESH_FOR_USER:
	case OP_NET_HANDLE_COUNT_DOWN_START:
	case OP_RECV_CLIENT_MAP_BLOCK:
	{
		UE_LOG(LogAsdaNet, Log, TEXT("%d ответ операторской команды (%d б) → подсистема команд оригинала"),
			(int32)Pkt.Opcode, (int32)Pkt.Body.size());
		OnGmOriginalPacket.Broadcast((int32)Pkt.Opcode, NetBodyToArray(Pkt.Body));
		break;
	}
	// ★★★ [ОРИГИНАЛ] 4103 0x1007 — состояние ОПЕРАТОРА, входящего в нашу зону видимости
	//   (`netHandlePersonaInteract@0xF49880`, network.c:43930; сервер GameServer.c:11109 —
	//   отправка обёрнута в `sub_52ECF0`, то есть шлётся ТОЛЬКО про операторов).
	//   Тело 8 б: [0..1] userNum i16 · [2..5] i32 «кто это» · [6..7] состояние i16 (300 = скрыт,
	//   иначе виден). Смещения сняты с раскладки стека отправителя (v13[2] @-0x4D0, v14 @-0x4CE,
	//   v15 @-0x4CA) и совпадают с чтением клиента `a1[3]`.
	//   ★★★ [ПРОТОКОЛ] СРЕДНЕЕ ПОЛЕ У ДВУХ ОТПРАВИТЕЛЕЙ РАЗНОЕ — дефект оригинала, вскрыт сверкой
	//   10.09.2026 (закон `charid-vs-usernum`):
	//     · `Send_AgainUserInfo` (GameServer.c:11107) кладёт `sub_5324F0(userNum)` = **UserIndex учётки**;
	//     · `Send_MoveSectorInClippingUserInfo` (GameServer.c:11991 и 12751) кладёт `*(DWORD*)(CUser+372)`
	//       = **charId персонажа**.
	//   Клиент оригинала эти байты НЕ ЧИТАЕТ ВОВСЕ (`netHandlePersonaInteract` берёт только `*a1` и
	//   `a1[3]`), поэтому расхождение ни на что не влияет и лечить его на сервере не нужно. Мы поле
	//   тоже не используем — только пишем в лог, и подписываем его нейтрально, чтобы лог не врал.
	//   ★ Именно этот пакет объясняет, почему уже скрытый GM не появляется при входе в AoI.
	//   [ПОРТ] Отдаём тем же каналом, что и 4025: там уже есть ветка состояний 300/301.
	case OP_NET_HANDLE_PERSONA_INTERACT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 8, (int32)Pkt.Opcode)) { break; }
		const int32 Uidx          = (int32)AsdaBytes::RdU16(&B[0]);
		const int32 OpWhoId       = (int32)AsdaBytes::RdI32(&B[2]);   // UserIndex ИЛИ charId — см. шапку
		const int32 OpHideState   = (int32)AsdaBytes::RdI16(&B[6]);   // имя с префиксом: `State` — поле класса
		UE_LOG(LogAsdaNet, Log, TEXT("4103 оператор в зоне: userIdx=%d id=%d (UserIndex или charId — зависит от отправителя) состояние=%d (%s)"),
			Uidx, OpWhoId, OpHideState, OpHideState == 300 ? TEXT("СКРЫТ") : TEXT("ВИДЕН"));
		// Эталон: `a1[3] == 300` → скрыть, ЛЮБОЕ иное значение → показать. Повторяем дословно.
		OnCharAction.Broadcast(Uidx, OpHideState == 300 ? 300 : 301, 0.f, 0.f);
		break;
	}
	case OP_BILLBOARD_REMOVE_BY_KEY_CLEAR_TARGET: // 5015 0x1397 — удалить предмет с тайла (мгновенно)
	{
		bItemPickPending = false;   // ★ 09.10.2026: эталон первой строкой `sub_927560(g_pUIManager, 0)` (0xF45350), безусловно
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 4, (int32)Pkt.Opcode))
		{
			const FIntPoint Tile(AsdaBytes::RdI16(&B[0]),
			                     AsdaBytes::RdI16(&B[2]));
			UE_LOG(LogAsdaNet, Log, TEXT("5015 лут снят: тайл(%d,%d)"), Tile.X, Tile.Y);
			ASDA_CTRACE(EAsdaCT::Reward, TEXT("ПРИЁМ 5015 добыча снята с тайла (%d,%d)"), Tile.X, Tile.Y);
			OnLootRemove.Broadcast(Tile);
		}
		break;
	}
	case OP_ITEM_PICKUP_GROUND_OBJECT: // 5017 0x1399 — «подбери предмет на тайле» (58б; клиент идёт/шлёт 5012)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 10, (int32)Pkt.Opcode))
		{
			const uint8 Result = B[0];
			const FIntPoint Tile(AsdaBytes::RdI16(&B[6]),
			                     AsdaBytes::RdI16(&B[8]));
			UE_LOG(LogAsdaNet, Log, TEXT("5017 pick-hint: result=%d тайл(%d,%d)"), Result, Tile.X, Tile.Y);
			ASDA_CTRACE(EAsdaCT::Reward, TEXT("ПРИЁМ 5017 подсказка подбора: результат=%d предмет=%d тайл (%d,%d)"),
				(int32)Result, AsdaBytes::RdI32(&B[1]), Tile.X, Tile.Y);
			OnLootPickHint.Broadcast(Result, Tile);
		}
		break;
	}
	case OP_NET_TRADE_CANCEL_RESET_CHAR_STATE: // 4205 0x106D — netTradeCancelResetCharState@0xF4A0D0:
	{                                     //   сброс боевого состояния (выход из боевой стойки), @0 u16 userIdx
		const std::vector<uint8>& B = Pkt.Body;
		if (NetNeedLen(B, 2, (int32)Pkt.Opcode))
		{
			// ★ Аудит №3 (знаковость): эталон `netTradeCancelResetCharState@0xF4A0D0` принимает тело
			//   как `__int16 *a2` и ищет владельца по `CharManager_FindByUserIdx(mgr, *a2)` — ЗНАКОВО.
			const int32 Uidx = AsdaBytes::RdI16(&B[0]);
			UE_LOG(LogAsdaNet, Log, TEXT("4205: сброс боевого состояния userIdx=%d"), Uidx);
			ASDA_CTRACE_UID(Uidx, EAsdaCT::Recv, TEXT("ПРИЁМ 4205 сброс боевой стойки: uidx=%d%s"), Uidx, IsSelfUserIdx(Uidx) ? TEXT(" (Я)") : TEXT(""));
			OnCombatStanceReset.Broadcast(Uidx);
		}
		break;
	}
	// ★★★ 4207 0x106F — «В БОЮ ПОРТАЛЫ НЕ РАБОТАЮТ» (04.11.2026, баг владельца «под атакой порталы не пускают»). [ОРИГИНАЛ]
	//   Обработчика не было вовсе: пакет молча терялся, и отказ сервера выглядел как «клиент игнорирует портал».
	//   Сервер: `sub_52C4C0@0x52C4C0` (проверка зоны портала из `MoveSyncProcess`) при флаге боя `sub_556D10` шлёт 4207 с
	//   ПУСТЫМ телом и 5055 НЕ шлёт; флаг ставит `sub_556C60(…, 1)` на каждом ударе (моб бьёт игрока `sub_426B00`, обычная
	//   атака 4028 `sub_4C5540`, PvP 4203 `sub_555800`, навыки `sub_470DB0`/`sub_471C70`/`sub_473360`/`sub_48F2B0`/
	//   `sub_48FE40`/`sub_4913E0`), снимается через 10 000 мс после последнего удара. Блок — ЗАДУМКА оригинала.
	//   Эталон приёма `network.c:2112-2117`: нотис 108000 («В бою порталы не работают.») — строка журнала
	//   (`CChat_AddTokenizedLines`) и плашка 221 (`CControl_SetTooltipText`), цвет −11514, 10 000 мс; тела не читает.
	case OP_UI_FORMAT_LOCALIZED_NOTICE:
	{
		AsdaNotice::PlankAndChatLoc(GetGameInstance(), EAsdaMsgCategory::System, 108000, UAsdaChatSubsystem::ColGold(), 10000.f,
			(int32)Pkt.Opcode);
		UE_LOG(LogAsdaNet, Warning, TEXT("[портал] 4207: сервер отказал в переходе — персонаж в бою (флаг 10 с после удара), нотис 108000"));
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ПРИЁМ 4207 отказ портала: в бою (нотис 108000)"));
		break;
	}
	case OP_SKILL_HOTBAR_REFRESH_SLOTS:   // 5228 0x146C — netRecvSkillHotbarFull@0xF50560, payload 101
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, kSoulTrayBlobLen, (int32)Pkt.Opcode, TEXT("5228 блок иконок лотка Дружбы"))) { break; }
		// 1:1 `memcpy(объект + 1256, body, 0x65)`: блок копируется ЦЕЛИКОМ, без разбора полей.
		FMemory::Memcpy(SoulSkillTrayBlob, B.data(), kSoulTrayBlobLen);
		// Диагностика: сколько навыков лотка (k = 30..44) пришло зажжёнными.
		int32 Lit = 0;
		for (int32 k = 30; k <= 44; ++k) { if (IsSoulTraySkillLit(k)) { ++Lit; } }
		UE_LOG(LogAsdaNet, Log,
			TEXT("←5228 блок иконок лотка Дружбы (101 б): зажжено %d из 15 навыков (id 30..44)"), Lit);
		OnSoulSkillTray.Broadcast(-1);   // −1 = «пришёл весь блок»
		break;
	}
	case OP_NET_HANDLE_SKILL_SLOT_ENABLE:   // 5238 0x1476 — netHandleSkillSlotEnable@0xF505A0, payload 2
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 2, (int32)Pkt.Opcode, TEXT("5238 открыт один навык Дружбы"))) { break; }
		// 1:1 `byte_2C3AB91[*a1] = 1`: тело — i16 индекс k, единица кладётся в blob[k + 1].
		const int32 SkillId = AsdaBytes::RdI16(&B[0]);
		const int32 Idx     = SkillId + 1;
		if (Idx < 0 || Idx >= kSoulTrayBlobLen)
		{
			// ⚠ ЭТАЛОН ГРАНИЦУ НЕ ПРОВЕРЯЕТ ВООБЩЕ (`byte_2C3AB91[*a1] = 1` пишет куда угодно) —
			//   это дефект оригинала: подделанный i16 портит соседнюю память. Чиним и помечаем.
			UE_LOG(LogAsdaNet, Warning,
				TEXT("←5238 индекс навыка %d вне блока 0..%d — запись отброшена ")
				TEXT("(дефект оригинала: эталон границу не проверяет)"), SkillId, kSoulTrayBlobLen - 2);
			break;
		}
		SoulSkillTrayBlob[Idx] = 1;
		UE_LOG(LogAsdaNet, Log, TEXT("←5238 навык Дружбы %d открыт (иконка лотка зажигается)"), SkillId);
		OnSoulSkillTray.Broadcast(SkillId);
		break;
	}

	// ★ 06.11.2026 Камень души, банка опыта: 5420, 5421, 5424, 5425 — разбор в `UAsdaSoulStoneSubsystem`.
	case OP_NET_RECV_TRADE_STATUS_RESULT:
	case OP_C_TRADE_ON_TARGET_INFO_PACKET:
	case OP_EXP_GIFT_BUFF:
	case OP_RECV_COPY_POS22_HANDLER:
	{
		UAsdaSoulStoneSubsystem* Stone = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulStoneSubsystem>() : nullptr;
		if (!Stone) { break; }
		const uint8* P = Pkt.Body.data();
		const int32 N = (int32)Pkt.Body.size();
		if (Pkt.Opcode == OP_NET_RECV_TRADE_STATUS_RESULT) { Stone->OnRecvStatus5420(P, N); }
		else if (Pkt.Opcode == OP_C_TRADE_ON_TARGET_INFO_PACKET) { Stone->OnRecvPartnerAsk5421(P, N); }
		else if (Pkt.Opcode == OP_EXP_GIFT_BUFF) { Stone->OnRecvExpGift5424(P, N); }
		else { Stone->OnRecvBar5425(P, N); }
		break;
	}

	// ══════ ★★★ ЗОВ ДРУГА: 5236 входящий зов (S→C), 5237 ответ/отказ (S→C-ветка) ═══════════════
	case OP_SOULMATE_RECALL_REQUEST:   // 5236, 42 б — ★ МИСНЕЙМ эталона `netOnUpgradeResultForLocal`
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 42, (int32)Pkt.Opcode, TEXT("5236 зов друга"))) { break; }
		const int32 InitCharId  = AsdaBytes::RdI32(&B[0]);    // @0  i32 charId инициатора
		const int32 InitUserNum = AsdaBytes::RdI16(&B[4]);    // @4  i16 userNum инициатора
		const int32 TgtUserNum  = AsdaBytes::RdI16(&B[36]);   // @36 i16 userNum ЦЕЛИ (должен быть мой)
		// ★ ГЕЙТ ЭТАЛОНА (network.c:47167): зов принимается, только если @36 == мой userNum.
		//   Если свой userNum ещё не известен — не роняем пакет молча, а пропускаем с предупреждением
		//   (памятка «userNum 0 — законный»: нулём глушить нельзя, проверяем ТОЛЬКО через IsSelfUserIdx).
		if (IsSelfUserNumKnown() && !IsSelfUserIdx(TgtUserNum))
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("←5236 зов друга АДРЕСОВАН НЕ МНЕ (цель userNum=%d, свой=%d) — игнор"),
				TgtUserNum, (int32)SelfUserNum);
			break;
		}
		LastMateRecall = NetBodyToArray(B);
		// «Зов ведёт на другую карту/канал» = (@7 != @39) || (@41 != @9) — ★ МИСНЕЙМ глобала эталона
		//   `g_bUpgradeResultGradeChanged`. При нём согласие идёт сменой канала, а НЕ пакетом 5427.
		const bool bOtherPlace = (B[7] != B[39]) || (B[41] != B[9]);
		UE_LOG(LogAsdaNet, Log,
			TEXT("←5236 зов друга: инициатор charId=%d userNum=%d, карта %u→%u, канал %u→%u, плитка (%d,%d)%s"),
			InitCharId, InitUserNum, (uint32)B[7], (uint32)B[39], (uint32)B[9], (uint32)B[41],
			AsdaBytes::RdI16(&B[28]), AsdaBytes::RdI16(&B[30]),
			bOtherPlace ? TEXT(" — ДРУГАЯ карта/канал: согласие через переход канала, не 5427") : TEXT(""));
		OnMateRecall.Broadcast(LastMateRecall);
		break;
	}
	case OP_SEND_RESPONSE_USER_TO_REQUEST_USER_MATE_RECALL_SKILL:   // 5237 S→C, 12 б — итог зова инициатору
	{
		// Тот же опкод, что и C→S-ответ цели: сервер `sub_49EE70@0x49EE70` маршрутизирует его обратно
		//   инициатору. Отдельного делегата контракт под него не даёт, поэтому здесь — разбор кода и
		//   лог с номером строки локализации (таблица «событие → loc → канал», отчёт B, :684-698).
		//   Канал у всех кодов — подсказка (цвет −11514); у кода 1 (согласие) нотиса нет.
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 12, (int32)Pkt.Opcode, TEXT("5237 итог зова друга"))) { break; }
		const uint8 Code = B[0];
		const TCHAR* What = TEXT("код не описан отчётом");
		int32 Loc = 0;
		switch (Code)
		{
		case 0: What = TEXT("рассинхрон charId цели");            Loc = 9576;  break;
		case 1: What = TEXT("СОГЛАСИЕ (нотиса нет)");             Loc = 0;     break;
		case 2: What = TEXT("цель отказалась");                   Loc = 9577;  break;
		case 3: What = TEXT("другой канал и цель не в группе");   Loc = 9584;  break;
		case 4: What = TEXT("запрещённый тип карты инициатора");  Loc = 13920; break;
		case 5: What = TEXT("цель занята (sub_4CB030)");          Loc = 13928; break;
		default: break;
		}
		UE_LOG(LogAsdaNet, Log, TEXT("←5237 итог зова друга: код=%u (%s, loc %d), инициатор charId=%d userNum=%d"),
			(uint32)Code, What, Loc, AsdaBytes::RdI32(&B[1]), AsdaBytes::RdI16(&B[5]));
		// ★ Нотис показывает подсистема навыков Дружбы (у неё таблица «событие → loc → канал»);
		//   делегата под этот опкод в контракте нет, поэтому зовём её точку напрямую.
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaSoulmateSkillSubsystem* Sk = GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>())
			{
				TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
				Sk->OnMateRecallAnswer5237(Body);
			}
		}
		break;
	}

	// ══════ ★★★ РУЛЕТКА СОУЛМЕЙТА: 6576 результат прокрутки ═══════════════════════════════════
	//   ★ 6580 (выдача приза, 73 б) принимается ВЫШЕ — там уже стоит `case OP_ROULETTE_RESULT`
	//     (снапшот ОЗ/ОМ у приза вида 3); делегат `OnRoulettePrize` вещается оттуда же, второй
	//     `case` на тот же опкод дал бы «duplicate case».
	case OP_ROULETTE_PANEL_HANDLE_NET_EVENT:   // 6576, 3 б
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 3, (int32)Pkt.Opcode, TEXT("6576 результат прокрутки рулетки"))) { break; }
		const uint8 Code = B[0];
		const int32 Slot = (int32)(int8)B[1];    // i8 сектор 0..3 (угол = сектор × 90°, при 0 → 360°)
		const int32 Next = (int32)(int8)B[2];    // i8 следующая строка таблицы (−1 = терминал)
		// ★ ДЕФЕКТ ОРИГИНАЛА D3: код 12 сервер шлёт (GameServer.c:61807), а эталонный клиент молчит.
		//   Здесь он хотя бы виден в логе; текст для него в оригинале не заведён — выдумывать нечего.
		UE_LOG(LogAsdaNet, Log, TEXT("←6576 рулетка: код=%u%s сектор=%d следующая строка=%d"),
			(uint32)Code,
			(Code == 10) ? TEXT(" (отказ, loc 8081)") : (Code == 11) ? TEXT(" (отказ, loc 8167)")
				: (Code == 12) ? TEXT(" (отказ 12 — в оригинале БЕЗ текста, дефект D3)") : TEXT(""),
			Slot, Next);
		OnRouletteResult.Broadcast(NetBodyToArray(B));
		break;
	}

	default: bHandled = false; break;
	}
}
