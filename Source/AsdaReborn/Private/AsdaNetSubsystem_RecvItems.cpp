// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_RecvItems.cpp — часть `AsdaNetSubsystem.cpp`: приёмник пакетов: контейнеры/склад, предметы, торговец, премиум-баланс/ящик, гача, бустеры, улучшение/разбор, лавки.
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

// ★ 09.11.2026 [ИСПРАВЛЕНИЕ] 6125 — отказ призыва с кодом без строки. Эталон `netHandleItemActionResult@0x973950`
//   (network.c:3577) в ветке default печатает сам номер (`sprintf("%d")`), а сервер `Recv_RaidMonsterCallReq@0x428C50`
//   (GameServer.c:17788) шлёт ещё коды 7 (индекс рецепта CallMob вне таблицы), 8 (число ячеек или код ≠ рецепту),
//   9 (не та карта), 10 (слот босса вне списка мобов карты) — игрок видел окно «7». 1 — такие коды показываются общей
//   строкой отказа 98104 «Не удалось сломать печать монстра.» (той же, что у кодов 0/3/4/5); 0 — как в оригинале, числом.
static TAutoConsoleVariable<int32> CVarRaidCallUnknownCodeAsFail(
	TEXT("asda.Raid.CallUnknownCodeAsFail"), 1,
	TEXT("6125: коды отказа призыва без своей строки (7, 8, 9, 10, прочие) — 1: нотис 98104 [ИСПРАВЛЕНИЕ], 0: номер кода текстом (эталон)."));

// ★ R2 (10.11.2026): приёмник области «Items» — контейнеры/склад, предметы, торговец, премиум-баланс/ящик, гача, бустеры, улучшение/разбор, лавки.
//   Группы `case` перенесены из `UAsdaNetSubsystem::HandlePacket` ДОСЛОВНО и в исходном порядке; `bHandled` = false только
//   в служебной ветке `default:` в конце — опкод не из этой области, диспетчер идёт дальше.
void UAsdaNetSubsystem::HandlePacket_Items(const Asda::ParsedPacket& Pkt, bool& bHandled)
{
	bHandled = true;
	switch ((NetOpcode)Pkt.Opcode)
	{
	case OP_INVENTORY_BAG_FILL:          // 4045 — батч КОНТЕЙНЕРА 1 (ShellInven, 57б/запись); 4046 — терминатор
	{
		TArray<uint8> Raw = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		// ★ Подписчику (инвентарю) отдаём КОПИЮ — он обязан только положить её в очередь (закон
		//   no-reentrant-net-callbacks) и разобрать в своём Tick.
		OnInventoryBatch.Broadcast(1, Raw);
		PendingBagPackets.Push(MoveTemp(Raw));   // ★ A2 (аудит №4): общая точка TAsdaNetQueue
		break;
	}
	case OP_CHAR_MANAGER_GET_LOCAL_PLAYER: // 4046 — конец батчей 4045 (эталон: LoadProgress +1)
	{
		// ★ A2 (аудит №4): терминатор ЗАБИРАЕТ накопленное (Drain), а не только читает длину. Без этого
		//   очередь росла всю сессию (читателя у неё нет — рабочий канал это делегат OnInventoryBatch),
		//   а число в логе было суммой за сессию, а не размером батча.
		TArray<TArray<uint8>> Done;
		PendingBagPackets.Drain(Done);
		UE_LOG(LogAsdaNet, Log, TEXT("4046: контейнер 1 (снаряжение/квестовые) получен (%d пакетов)"), Done.Num());
		OnInventoryBatchEnd.Broadcast(1);
		break;
	}
	case OP_INVENTORY_FULL_REFRESH:      // 4048 — батч надетого (57б/запись); 4049 — терминатор
	{
		TArray<uint8> Raw = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		// ★ СВОЁ ОРУЖИЕ: запись 57б (inventory_trade.md §1): itemCode i32@0, type u8@4, slotIndex i16@5.
		// ★★★ ФИКС 2026-07-31 (живой лог: «Своё оружие (4048): itemId=20593», а надет itemId=63033):
		//   поле +5 в ЭТОМ батче — НЕ экип-слот, а индекс ячейки инвентаря. По item-БД 20593 = itemType 11
		//   (свиток), 20566 = itemType 10 (стрелы) — т.е. в слоты «9» и «8» попадали расходники, свечение
		//   строилось для свитка и не появлялось на СЕБЕ вообще. Авторитетный источник экипа локального
		//   игрока по эталону — пакет 1027/0x403 (netHandleAppearanceChangeResult@0xF03A50 →
		//   g_aEnterWorldCharData → CCharacter+1588, откуда UpdateHeldItemEffect и читает слот 9/8).
		//   Здесь оставляем ТОЛЬКО подстраховку с проверкой ТИПА предмета по item-таблице.
		UAsdaItemTableSubsystem* ITab = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		bool bGradeChanged = false;
		// * Дд6 (аудит `code_dedup_audit_2.md`): длина записи предмета — константа `kRecordSize` (57),
		//   а не число в коде: этот же шаг встречался ещё в двух местах порта.
		for (int32 Off = 0; Off + FAsdaItemInstance::kRecordSize <= Raw.Num(); Off += FAsdaItemInstance::kRecordSize)
		{
			const int32 Code = AsdaBytes::RdI32(&Raw[Off]);
			int32 Slot = AsdaBytes::RdI16(&Raw[Off+5]);
			// Тип предмета решает, а не номер ячейки: слот принимаем, только если он совпал с itemType.
			if (Code > 0 && ITab)
			{
				const int32 IType = ITab->GetItemType(Code);
				if (IType != Slot) { Slot = -1; }
			}
			// ★ УРОВЕНЬ ЗАТОЧКИ = байт +0x18 инстанса (эталон entity_char.c:28761 LOBYTE(equipSlot[6])).
			const int32 Grade = (int32)Raw[Off + 0x18];
			if (Slot == 9 && Code > 0)
			{
				SelfWeaponItemId = Code;
				SelfWeaponGrade  = Grade;
				bGradeChanged = true;
				UE_LOG(LogAsdaNet, Log, TEXT("Своё оружие (4048): itemId=%d, заточка=%d"), Code, Grade);
			}
			else if (Slot == 8 && Code > 0)
			{
				// Экип-слот 8 = ЩИТ (itemType 8; эталон tagCHARACTER_SetPartsModelByType case 8 →
				//   CCharacter_SetParts_Armor). Его свечение — база "shd" (0xA4DD60).
				SelfShieldItemId = Code;
				SelfShieldGrade  = Grade;
				bGradeChanged = true;
				UE_LOG(LogAsdaNet, Log, TEXT("Свой щит (4048): itemId=%d, заточка=%d"), Code, Grade);
			}
		}
		if (bGradeChanged) { OnSelfEquipGrade.Broadcast(); }
		// ★★★ УТОЧНЕНО РЕВЕРСОМ 2026-08-08 (`inventory_re_B.md` §1.2/§2.1): 4048 — это НЕ «надетое»,
		//   а КОНТЕЙНЕР 2 (`SpendCellobInven`, 60 ячеек: расходники / самоцветы / ЗОЛОТО 20551).
		//   Отсюда и «слот 9 = свиток» из фикса выше: поле +5 — индекс ячейки контейнера 2.
		//   Надетое приходит ПОЛНЫМИ записями только в 1027/0x403 (11 × 57).
		// ★ 19.10.2026 — ОБАЯНИЕ (`SelfCharm`, объявление в .h): хвост `recv_InventoryFullRefresh` (network.c:17309-17311)
		//   `uiSetThousandsFormattedValue(g_pGameClient, CCharacter+0x450 + (u8)CCharacter+0x454)` — это блок 1017 +52 (i32) и +56 (u8).
		//   [ПОРТ] берём блок один раз после 1017 (`bSelfCharmFromBlock`): дальше поле ведут награды 5042.
		if (!bSelfCharmFromBlock && CharSelectData.Num() >= 57)
		{
			SelfCharm = AsdaBytes::RdI32(CharSelectData.GetData() + 52) + (int32)CharSelectData[56];
			bSelfCharmFromBlock = true;
			UE_LOG(LogAsdaNet, Log, TEXT("4048: обаяние из блока 1017 (+52 i32 + +56 u8) = %d"), SelfCharm);
		}
		OnInventoryBatch.Broadcast(2, Raw);
		PendingEquipPackets.Push(MoveTemp(Raw));   // ★ A2 (аудит №4): общая точка TAsdaNetQueue
		break;
	}
	case OP_CHAR_MANAGER_GET_LOCAL_PLAYER_4049: // 4049 (эталон: LoadProgress +2 → 8)
	{
		// ★ A2 (аудит №4): то же, что у 4046 — терминатор забирает накопленное (см. комментарий там).
		TArray<TArray<uint8>> Done;
		PendingEquipPackets.Drain(Done);
		UE_LOG(LogAsdaNet, Log, TEXT("4049: контейнер 2 (расходники/золото) получен (%d пакетов)"), Done.Num());
		OnInventoryBatchEnd.Broadcast(2);
		break;
	}
	case OP_WAREHOUSE_LOAD_SLOTS_FROM_PACKET: // 4051 — батч КОНТЕЙНЕРА 5 (склад), страница по 30
	{
		TArray<uint8> Raw = NetBodyToArray(Pkt.Body);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("4051: склад — %d б (%d записей по %d)"),
			Raw.Num(), Raw.Num() / FAsdaItemInstance::kRecordSize, FAsdaItemInstance::kRecordSize);
		OnInventoryBatch.Broadcast(5, Raw);
		break;
	}
	// ══════════ АУКЦИОН — 7 ответных опкодов, все через один диспетчер эталона ═══════════════
	//   `npcShop_DispatchResult@0xD43270` (`npc_mob_pet.c:511`), приём — `network.c:1116-1177`.
	//   Здесь только проверка минимальной длины и передача тела окну: разбор записей, коды
	//   результата и нотисы живут в `UAsdaExchangeSubsystem`
	//   (таблицы — `Reports/npc_exchange_win_13000.md` §7.4/§7.5).
	//   ★ Пары (Kind, Sub) — ровно аргументы `a3`/`a4` эталонного диспетчера.
	case 9902:  // результат ВЫСТАВЛЕНИЯ лотов — записи по 67 б, до 5
	case 9904:  // страница ПОИСКА          — записи по 80 б, до 8
	case 9906:  // результат ПОКУПКИ        — записи по 85 б, до 8
	case 9908:  // МОИ ЛОТЫ                 — записи по 62 б, до 10
	case 9910:  // результат СНЯТИЯ лота    — записи по 61 б, до 10
	case 9913:  // ★ «твой лот КУПИЛИ» — незапрошенное уведомление, 48 б
	case 9914:  // ★ «аукцион заблокирован» — 6 б, приходит в ответ на 9915
	{
		int32 Kind = 0, Sub = -1;
		switch (Pkt.Opcode)
		{
			case 9902: Kind = 1; Sub = -1; break;
			case 9904: Kind = 4; Sub = 0;  break;
			case 9906: Kind = 4; Sub = 1;  break;
			case 9908: Kind = 3; Sub = 0;  break;
			case 9910: Kind = 3; Sub = 1;  break;
			case 9913: Kind = 6; Sub = -1; break;
			default:   Kind = 7; Sub = -1; break;   // 9914
		}
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		UE_LOG(LogAsdaNet, Log, TEXT("аукцион %d: тело %d б (a3=%d a4=%d)"),
			(int32)Pkt.Opcode, Body.Num(), Kind, Sub);
		OnExchangePacket.Broadcast((int32)Pkt.Opcode, Kind, Sub, Body);
		break;
	}
	case OP_WAREHOUSE_DRAW_CURRENT_TAB:       // 4052 — конец страницы склада
		UE_LOG(LogAsdaNet, Log, TEXT("4052: страница склада получена целиком"));
		OnInventoryBatchEnd.Broadcast(5);
		break;
	// ★ 5009 / 5011 — результаты «положить на склад» и «забрать со склада». Раскладки и коды —
	//   в объявлении `OnWarehouseResult`. Обе ветки только разбирают шапку и отдают тело окну:
	//   применение содержимого и нотисы живут в `UAsdaWarehouseSubsystem` (там же таблица кодов).
	case OP_INVENTORY_SWAP:      // 5009 — ответ на 5008
	case OP_INVENTORY_SWAP2:     // 5011 — ответ на 5010
	{
		const bool bKeeping = (Pkt.Opcode == (uint16)OP_INVENTORY_SWAP);
		const int32 MinLen  = bKeeping ? 113 : 117;
		const std::vector<uint8>& B = Pkt.Body;
		if ((int32)B.size() < MinLen)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("%d: тело %d б < %d"),
				bKeeping ? 5009 : 5011, (int32)B.size(), MinLen);
			break;
		}
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		const uint8 Result = Body[0];
		if (bKeeping)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("5009 положить на склад: код=%d, новый вес=%d"),
				(int32)Result, AsdaBytes::RdI16(&Body[111]));
		}
		else
		{
			// ★ Только у 5011 приходит НОВАЯ СУММА ЗОЛОТА — с неё уже списана комиссия за забор.
			UE_LOG(LogAsdaNet, Log, TEXT("5011 забрать со склада: код=%d, золото=%d, новый вес=%d"),
				(int32)Result, AsdaBytes::RdI32(&Body[111]), AsdaBytes::RdI16(&Body[115]));
		}
		OnWarehouseResult.Broadcast(bKeeping, Result, Body);
		break;
	}
	// ★ 5001 (24 б) — РЕЗУЛЬТАТ 5000. Клиент оригинала НЕ применяет перемещение оптимистично:
	//   до этого пакета висит флаг `g_bInventoryActionPending`, и только result == 1 применяет.
	case OP_CHAR_ITEM_MOVE_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 24) { UE_LOG(LogAsdaNet, Warning, TEXT("5001: тело %d б < 24"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(24);
		FMemory::Memcpy(Body.GetData(), B.data(), 24);
		UE_LOG(LogAsdaNet, Log, TEXT("5001 результат перемещения: result=%d subKind=%d src(тип=%d слот=%d) dst(тип=%d слот=%d)"),
			(int32)Body[0], (int32)Body[1], (int32)Body[6],
			AsdaBytes::RdI16(&Body[2]), (int32)Body[17],
			AsdaBytes::RdI16(&Body[13]));
		OnItemMoveResult.Broadcast(Body);
		break;
	}
	// ★ 5003 (14 б) `Recv_ItemEquipStat@0xD0F350` — изменение количества/очистка слота/ДЕНЬГИ+ВЕС.
	case OP_ITEM_EQUIP_STAT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 14) { UE_LOG(LogAsdaNet, Warning, TEXT("5003: тело %d б < 14"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(14);
		FMemory::Memcpy(Body.GetData(), B.data(), 14);
		const int32 NewCurWeight = AsdaWeightFromWire(AsdaBytes::RdI16(&Body[12]));   // ★ 29.10.2026: вес u16
		UE_LOG(LogAsdaNet, Log, TEXT("5003 слот изменён: result=%d контейнер=%d слот=%d кол-во=%d вес=%d"),
			(int32)Body[0], (int32)Body[5], AsdaBytes::RdI16(&Body[1]),
			AsdaBytes::RdI32(&Body[6]),
			NewCurWeight);
		OnItemSlotUpdate.Broadcast(Body);
		SetSelfWeight(NewCurWeight, -1, TEXT("5003"));   // ★ вес приезжает В КАЖДОМ item-пакете
		break;
	}
	// ★ 4999 (59 б) — слот полностью очищен: запись 57 б + i16 новый текущий вес @57.
	case OP_INVENTORY_REMOVE_SLOT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 59) { UE_LOG(LogAsdaNet, Warning, TEXT("4999: тело %d б < 59"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(59);
		FMemory::Memcpy(Body.GetData(), B.data(), 59);
		const int32 NewCurWeight = AsdaWeightFromWire(AsdaBytes::RdI16(&Body[57]));   // ★ 29.10.2026: вес u16
		UE_LOG(LogAsdaNet, Log, TEXT("4999 слот очищен: контейнер=%d слот=%d вес=%d"),
			(int32)Body[4], AsdaBytes::RdI16(&Body[5]), NewCurWeight);
		OnItemSlotRemoved.Broadcast(Body);
		SetSelfWeight(NewCurWeight, -1, TEXT("4999"));
		break;
	}
	// ★ 5029 (24 б) `OnRecv_UseItemResult@0xD0B1E0` — результат использования расходника.
	case OP_ON_RECV_USE_ITEM_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 24) { UE_LOG(LogAsdaNet, Warning, TEXT("5029: тело %d б < 24"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(24);
		FMemory::Memcpy(Body.GetData(), B.data(), 24);
		UE_LOG(LogAsdaNet, Log, TEXT("5029 использование: result=%d code=%d слот=%d кол-во=%d"),
			(int32)Body[0],
			AsdaBytes::RdI32(&Body[7]),
			AsdaBytes::RdI16(&Body[11]),
			AsdaBytes::RdI32(&Body[16]));
		OnItemUseResult.Broadcast(Body);
		break;
	}
	// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (баг 10): 5030 (18 б) `recvItemResultNotice@0xD61300` — расход стрел слота 10. До правки
	//   пакет не принимался вовсе. Эталон: `memset(0)` + `memcpy(Src+9, 0x12)` — короткое тело дополняется нулями. Вес (@5)
	//   эталон ставит ТОЛЬКО в ветке «код 1, количество ≤ 0» (`itemDropUpdateWeightBar`); остальное — в Tick подсистемы
	//   (`UAsdaArrowAmmoSubsystem`, разбор веток — в её шапке). Ключ отката `asda.Inven.Arrow5030` читает сама подсистема.
	case OP_RECV_ITEM_RESULT_NOTICE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		TArray<uint8> Body; Body.SetNumZeroed(UAsdaArrowAmmoSubsystem::kBodySize);
		if (!B.empty()) { FMemory::Memcpy(Body.GetData(), B.data(), FMath::Min<int32>((int32)B.size(), UAsdaArrowAmmoSubsystem::kBodySize)); }
		UE_LOG(LogAsdaNet, Log, TEXT("5030 стрелы: тело %d б, код=%d кол-во=%d вес=%d ячейка=%d"), (int32)B.size(),
			(int32)Body[0], AsdaBytes::RdI32(&Body[1]), AsdaWeightFromWire(AsdaBytes::RdI16(&Body[5])), AsdaBytes::RdI16(&Body[7]));
		UAsdaArrowAmmoSubsystem* Arrow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaArrowAmmoSubsystem>() : nullptr;
		if (Arrow && Arrow->Push5030(Body) && Body[0] == 1 && AsdaBytes::RdI32(&Body[1]) <= 0)
		{
			SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[5])), -1, TEXT("5030"));
		}
		break;
	}
	// ★★★ 6097 (300 б) `netOnStorageResultPacket@0xDABFD0` — РЕЗУЛЬТАТ ВСКРЫТИЯ КОРОБКИ.
	//   Гейта «свой/чужой» в клиенте нет и не нужно: сервер шлёт пакет только автору
	//   (`sub_4C28F0` → `PackingSendPacket`), а сам предмет ищет в инвентаре запросившего.
	case OP_NET_ON_STORAGE_RESULT_PACKET:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 300) { UE_LOG(LogAsdaNet, Warning, TEXT("6097: тело %d б < 300"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(300);
		FMemory::Memcpy(Body.GetData(), B.data(), 300);
		UE_LOG(LogAsdaNet, Log, TEXT("6097 вскрытие коробки: result=%d выдано=%d коробка(контейнер=%d слот=%d) вес=%d"),
			(int32)Body[7], (int32)Body[6], (int32)Body[8], AsdaBytes::RdI16(&Body[9]), AsdaBytes::RdI32(&Body[296]));
		OnPackageOpenResult.Broadcast(Body);
		break;
	}
	// ★★★ 6099 S→C (3 б) — ОТВЕТ СЕРВЕРА НА ПРАЗДНИЧНЫЙ ОБМЕН 6098 (27.09.2026, ревизия items_consistency_fix СРЕДН-2;
	//   отчёт `Reports/owner_0927/clan_btn_6099.md`). [ОРИГИНАЛ] 1:1 с `netHandleTradeStatusNotice@0x973640`
	//   (network.c:3444-3495; диспетчер `case 0x17D3`, AsdaStory_game.c:14317). До этого порт пакет не разбирал вовсе —
	//   отказ сервера по строке обмена игрок не видел.
	//   [ПРОТОКОЛ] тело: +0 u16 userNum адресата, +2 u8 код (эталон `memcpy(v10, Src+9, 3)`). Сервер
	//   `EventItemRequestReq@0x554330` (GameServer.c:130724-130870) шлёт его ТОЛЬКО на отказ, `v22 = arg0` (свой userNum):
	//   2 — строка вне таблицы, 3 — `idx != позиции`, 4 — предмета нет / строка 57 без квеста 360, 5 — мало (проверка ОДНОЙ
	//   найденной ячейки контейнера 2), 6 — награду не выдать (нет места). Успех идёт пакетом 2270, не сюда.
	//   Таблица «событие → loc → канал» (разбор по ЭТОМУ обработчику, не по семье):
	//     код 0/2/3 → 7502 «Не удалось передать предмет…»;  код 1 → 7506 «Получен праздничный предмет.»;
	//     код 4/5 → 7507 «Недостаточно необходимых предметов…»;  код 6 → 7505 «Рюкзак переполнен…»;
	//     прочие коды → МОЛЧА (`default: return`);  userNum не свой → 133 «Ошибка данных пользователя.»;
	//     своего персонажа нет (эталон `LocalPlayer == NULL`; у порта — userNum ещё не пришёл 4003) → молча.
	//   Канал всех нотисов — `CItem_BuildTooltipText(g_pItemTooltipUI, 0, 24, текст, 1)` = однокнопочное окно
	//   ONEBUT_MSGBOX 1500 с ключом очереди (0, 24) (общая точка `AsdaNotice::MsgBoxLoc`, звуки — у неё).
	//   Цвет — цвет окна (у вызова своего цвета нет). Гейтов эталон здесь НЕ снимает: 6098 не взводит «ответ на действие с
	//   предметом» (`sub_922033(…, 1)` ставят только 5000/5028), а троттлинг 3 с (`this[27930]`) ставит отправка и 6099 его не
	//   сбрасывает — поэтому и порт здесь ничего не гасит (`bActionPending` инвентаря и `LastNpcExchangeSendTime` не трогаем).
	case OP_NET_HANDLE_TRADE_STATUS_NOTICE:   // 6099 0x17D3
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 3, (int32)Pkt.Opcode, TEXT("6099 ответ на праздничный обмен"))) { break; }
		const uint16 Uidx = (uint16)AsdaBytes::RdI16(&B[0]);   // эталон `v10[0]` (_WORD)
		const int32 Code = (int32)B[2];                        // эталон `LOBYTE(v10[1])`
		if (!bSelfUserNumKnown)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("←6099 обмен: userNum=%u код=%d — свой персонаж ещё не в мире, молча (как эталон без LocalPlayer)"),
				(uint32)Uidx, Code);
			break;
		}
		int32 LocId = 0;   // 0 = эталон молчит
		if (Uidx != SelfUserNum) { LocId = 133; }   // «Ошибка данных пользователя.»
		else
		{
			switch (Code)
			{
			case 0: case 2: case 3: LocId = 7502; break;   // «Не удалось передать предмет. Повтори попытку позже.»
			case 1:                 LocId = 7506; break;   // «Получен праздничный предмет.»
			case 4: case 5:         LocId = 7507; break;   // «Недостаточно необходимых предметов…»
			case 6:                 LocId = 7505; break;   // «Рюкзак переполнен…»
			default:                LocId = 0;    break;   // эталон `default: return`
			}
		}
		UE_LOG(LogAsdaNet, Log, TEXT("←6099 обмен: userNum=%u (свой %u) код=%d → %s"),
			(uint32)Uidx, (uint32)SelfUserNum, Code,
			LocId ? *FString::Printf(TEXT("нотис %d, окно 1500 (0, 24)"), LocId) : TEXT("молча (default эталона)"));
		if (LocId)
		{
			AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Trade, LocId, AsdaLoc::kBtnOk, /*EtalonNoticeId*/24);
		}
		break;
	}
	// ★★★ 27.09.2026 — 6060 S→C (3 б) — ОТВЕТ «СОБЫТИЙНОГО NPC» на 6059 (`netHandleEnchantResultNotice@0x9732A0`, network.c:3370-3441,
	//   case 0x17AC; имя эталона ВРЁТ — к заточке отношения не имеет). Прежде в порту приёма не было: ответ на кнопку 30038 терялся.
	//   [ПРОТОКОЛ] тело: +0 u16 userNum адресата, +2 u8 код (эталон `memcpy(enchantResult, Src+9, 3)`). Сервер
	//   `eventNpcProcessRes@0x553DC0` (GameServer.c) шлёт его в ответ DB-серверу: код 1 — подарок выдан, 5 — перевес, 8 — рюкзак полон.
	//   Таблица «событие → loc → канал» (по ЭТОМУ обработчику):
	//     1 → 7500 «Проводится мероприятие…»;  2 → 7501 «Сегодня тебе уже довелось…»;  3/4/7/9 → 7502 «Не удалось передать предмет…»;
	//     5 → 7503 «Превышен максимально допустимый вес…»;  6 → 7504 «Предметы события не относятся к обычным.»;
	//     8 → 7505 «Рюкзак переполнен…»;  прочие коды → МОЛЧА (`default: return`);  userNum не свой → 133;
	//     своего персонажа нет (`LocalPlayer == NULL` → `Net_RecvUiTick()`, у порта — userNum ещё не пришёл 4003) → молча.
	//   Канал всех — `CItem_BuildTooltipText(g_pItemTooltipUI, 0, 24, текст, 1)` = окно ONEBUT_MSGBOX 1500, ключ (0, 24).
	case OP_NET_HANDLE_ENCHANT_RESULT_NOTICE:   // 6060 0x17AC
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 3, (int32)Pkt.Opcode, TEXT("6060 ответ событийного NPC"))) { break; }
		const uint16 Uidx = (uint16)AsdaBytes::RdI16(&B[0]);   // эталон `enchantResult[0]` (_WORD)
		const int32 Code = (int32)B[2];                        // эталон `LOBYTE(enchantResult[1])`
		if (!bSelfUserNumKnown)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("←6060 событийный NPC: userNum=%u код=%d — свой персонаж ещё не в мире, молча (как эталон без LocalPlayer)"),
				(uint32)Uidx, Code);
			break;
		}
		int32 LocId = 0;   // 0 = эталон молчит
		if (Uidx != SelfUserNum) { LocId = 133; }   // «Ошибка данных пользователя.»
		else
		{
			switch (Code)
			{
			case 1:                         LocId = 7500; break;
			case 2:                         LocId = 7501; break;
			case 3: case 4: case 7: case 9: LocId = 7502; break;
			case 5:                         LocId = 7503; break;
			case 6:                         LocId = 7504; break;
			case 8:                         LocId = 7505; break;
			default:                        LocId = 0;    break;   // эталон `default: return`
			}
		}
		UE_LOG(LogAsdaNet, Log, TEXT("←6060 событийный NPC: userNum=%u (свой %u) код=%d → %s"),
			(uint32)Uidx, (uint32)SelfUserNum, Code,
			LocId ? *FString::Printf(TEXT("нотис %d, окно 1500 (0, 24)"), LocId) : TEXT("молча (default эталона)"));
		if (LocId)
		{
			AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Trade, LocId, AsdaLoc::kBtnOk, /*EtalonNoticeId*/24);
		}
		break;
	}
	// ★★★ 27.09.2026 — 6125 S→C (51 б) — ОТВЕТ НА ПРИЗЫВ РЕЙД-БОССА 6124 (`netHandleItemActionResult@0x973950`, network.c:3508-3585).
	//   Прежде в порту приёма не было. [ПРОТОКОЛ] тело (`memcpy(actionData, Src+9, 0x33)`, сервер `Recv_RaidMonsterCallReq`,
	//   GameServer.c:17788, `sub_4C28F0(…, 6125, 51, …)`): +0 i32 charId, +4 шесть записей по 7 б (i32 код, u8 контейнер, i16 слот —
	//   эхо запроса), +46 u8 код, +47 i32 вес.
	//   Адресат ищется по charId (`CharManager_FindByCharId`); не найден → 133. Коды: 0/3/4/5 → 98104 «Не удалось сломать печать монстра.»;
	//   1 → снять из контейнера 2 ячейки, совпавшие с записями (код > 0, тот же контейнер и слот; `OnRecv_RemoveSlot_byPtr` = ClearCell),
	//   вес (`HUD_UpdateMiniWeightBar`), нотис 98105 «Печать монстра сломана.»; 2 → 133; 6 → 98106 «Печать монстра уже сломана.»;
	//   прочие → сам номер кода текстом (`sprintf("%d")`; у нас [ИСПРАВЛЕНИЕ] 09.11 → 98104, ключ asda.Raid.CallUnknownCodeAsFail). Канал всех — окно 1500 (0, 24).
	case OP_NET_HANDLE_ITEM_ACTION_RESULT:   // 6125 0x17ED
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 51, (int32)Pkt.Opcode, TEXT("6125 ответ на призыв рейд-босса"))) { break; }
		const int32 CharId = AsdaBytes::RdI32(&B[0]);   // эталон `actionData[0]`
		const int32 Code = (int32)B[46];                // эталон `BYTE2(actionData[11])`
		UGameInstance* GI = GetGameInstance();
		// `CharManager_FindByCharId`: свой персонаж (если уже в мире) либо любой известный игрок мира.
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const bool bKnown = (bSelfUserNumKnown && SelectedCharId != 0 && CharId == SelectedCharId)
			|| (WS && WS->FindPlayerActorByCharId(CharId) != nullptr);
		if (!bKnown)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("←6125 призыв: charId=%d не найден — нотис 133 окном (0, 24)"), CharId);
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, 133, AsdaLoc::kBtnOk, /*EtalonNoticeId*/24);
			break;
		}
		int32 LocId = 0;   // 0 — текст кода (ветка default эталона)
		switch (Code)
		{
		case 0: case 3: case 4: case 5: LocId = 98104; break;
		case 2:                         LocId = 133;   break;
		case 6:                         LocId = 98106; break;
		case 1:
		{
			int32 Removed = 0;   // снятых ячеек
			if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
			{
				for (int32 e = 0; e < 6; ++e)
				{
					const int32 Rec = 4 + 7 * e;                           // запись 7 б
					const int32 ItemCode = AsdaBytes::RdI32(&B[Rec]);      // код предмета записи
					const int32 Cont = (int32)B[Rec + 4];                  // контейнер записи
					const int32 Slot = (int32)AsdaBytes::RdI16(&B[Rec + 5]);   // слот записи
					if (ItemCode <= 0) { continue; }
					// Эталон перебирает 60 ячеек контейнера 2 и снимает ПЕРВУЮ, где совпали код, контейнер и слот.
					if (Cont != 2 || Slot < 0 || Slot >= Inv->GetContainerSize(2)) { continue; }
					if (Inv->GetCellItemCode(2, Slot) != ItemCode) { continue; }
					Inv->ClearCell(2, Slot);
					++Removed;
				}
			}
			SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI32(&B[47])), -1, TEXT("6125"));   // ★ 29.10.2026: вес u16 (сервер `movsx`→`movzx`)   // эталон `*(int*)((char*)&actionData[11] + 3)`
			UE_LOG(LogAsdaNet, Log, TEXT("←6125 призыв: успех, снято ячеек %d"), Removed);
			LocId = 98105;
			break;
		}
		// Эталон: голый номер кода. [ИСПРАВЛЕНИЕ] 09.11.2026 (asda.Raid.CallUnknownCodeAsFail): общая строка отказа 98104.
		default: LocId = CVarRaidCallUnknownCodeAsFail.GetValueOnGameThread() != 0 ? 98104 : 0; break;
		}
		UE_LOG(LogAsdaNet, Log, TEXT("←6125 призыв: charId=%d код=%d → %s"), CharId, Code,
			LocId ? *FString::Printf(TEXT("нотис %d, окно 1500 (0, 24)"), LocId) : *FString::Printf(TEXT("текст «%d», окно 1500 (0, 24)"), Code));
		if (LocId) { AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, LocId, AsdaLoc::kBtnOk, /*EtalonNoticeId*/24); }
		else       { AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Quest, FString::Printf(TEXT("%d"), Code), AsdaLoc::kBtnOk, /*EtalonNoticeId*/24); }
		break;
	}
	// ★★★ 27.09.2026 — 6126 S→C (5 б) — «КОРОНА» ВЛАДЕЛЬЦА ДОБЫЧИ РЕЙД-БОССА (`netHandleCharIdByteUpdate@0x973E30`, network.c:3609-3640,
	//   case 0x17EE network.c:593). Прежде приёма не было (ревизия charid_senders_REVIEW.md СРЕДН-1).
	//   [ПРОТОКОЛ] `memcpy(v6, Src+9, 5)`: +0 i32 charId, +4 u8 байт (сервер всегда шлёт 1: `Recv_RaidMonsterCallResult`
	//   GameServer.c:18045, повторы при входе `CharMake`/`CharPortalMake` :124185/:124388 и при появлении в обзоре
	//   `Send_MoveSectorInClippingUserInfo` :11795/:11933/:12556/:12693, владелец-по-добыче `Search_AddMonsterMapItem` :14206).
	//   Разбор и отображение — `UAsdaKingCrownSubsystem::OnCharIdByteUpdate` (гейт CallMobTable, поиск по charId, эффект).
	case OP_NET_HANDLE_CHAR_ID_BYTE_UPDATE:   // 6126 0x17EE
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 5, (int32)Pkt.Opcode, TEXT("6126 корона владельца добычи"))) { break; }
		const int32 CharId = AsdaBytes::RdI32(&B[0]);   // эталон `v6[0]`
		const uint8 Byte = B[4];                         // эталон `LOBYTE(v6[1])`
		UE_LOG(LogAsdaNet, Log, TEXT("←6126 корона: charId=%d байт=%u"), CharId, (uint32)Byte);
		if (UAsdaKingCrownSubsystem* Crown = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaKingCrownSubsystem>() : nullptr)
		{
			Crown->OnCharIdByteUpdate(CharId, Byte);
		}
		break;
	}
	// ★★★ 6080 S→C (128 б) — ТЕКСТ «О СЕБЕ» (`uiSetNameText@0xE749B0`, `network.c:525`, case 0x17C0).
	//   Сервер (`charLogonCommunityRes@0x498B40`) шлёт его в ответ на нашу запись и при входе, когда
	//   community/DB-сервер вернул строку (опкод 2262). Тело — РОВНО 128 байт cp1251, без длины.
	case OP_RECV_SOULMATE_INTRODUCTION:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 1) { UE_LOG(LogAsdaNet, Warning, TEXT("6080: пустое тело")); break; }
		const int32 N = FMath::Min<int32>((int32)B.size(), 128);
		SelfIntroduction = FAsdaLoginData::DecodeCp1251(B.data(), N);
		UE_LOG(LogAsdaNet, Log, TEXT("6080 «о себе»: «%s» (%d б)"), *SelfIntroduction, N);
		OnSelfIntroChanged.Broadcast(SelfIntroduction);
		break;
	}
	// ★★★ 5469 (68 б) `Recv_BoosterShopResult@0xF4C220` — РЕЗУЛЬТАТ ГАЧИ (только автору).
	// ═════════════════════ ПРЕМИУМ-ГАЧА: приём ═════════════════════
	// ═══════════ ПРЕМИУМ-МОЛЛ: S→C 5071 — результат покупки (407 б) ═══════════
	//   `shop_OnRecvTransactionResult@0xC30BD0`. ⚠ Поправка к прежней документации: НЕ 5270.
	case OP_SHOP_ON_RECV_TRANSACTION_RESULT:   // 5071
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 407) { UE_LOG(LogAsdaNet, Warning, TEXT("5071: тело %d б < 407"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(407);
		FMemory::Memcpy(Body.GetData(), B.data(), 407);
		UE_LOG(LogAsdaNet, Log,
			TEXT("5071 премиум-покупка: код=%d валюта=%s баланс=%d вес=%d подрежим=%d"),
			(int32)Body[0], Body[1] ? TEXT("GXP") : TEXT("обаяние"),
			AsdaBytes::RdI32(&Body[2]), AsdaBytes::RdI16(&Body[6]), (int32)Body[12]);
		OnPremiumShopResult.Broadcast(Body);
		break;
	}
	// ═══════════ ★ 01.11.2026 P101_CashGift: S→C 5073/5075/5077/5078 — ответы подарка (`re/client.md` §3) ═══════════
	//   Эталонный диспетчер: 0x13D1 → `netHandleCraftResultPacket(g_pShopWnd)`, 0x13D3 → `bankGoldResultPacket_Handle`,
	//   0x13D5 → `tradeOnItemBindResult`, 0x13D6 → `Recv_PartyMember(g_pQuickSlotBar, 3)` (network.c:1613-1623). Каждый копирует
	//   из пакета ровно свой размер (`memcpy(…, Src + 9, N)`) — здесь так же; разбор и окна — `UAsdaCashGiftSubsystem`.
	case OP_NET_HANDLE_CRAFT_RESULT_PACKET:    // 5073 — проверка имени (34 б)
	case OP_BANK_GOLD_RESULT_PACKET_HANDLE:    // 5075 — итог подарка (6 б)
	case OP_TRADE_ON_ITEM_BIND_RESULT:         // 5077 — итог через реле (61 б)
	case OP_PARTY_MEMBER_5078:                 // 5078 — подарок пришёл (421 б)
	{
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Op = (int32)Pkt.Opcode;
		const int32 Need = (Op == OP_NET_HANDLE_CRAFT_RESULT_PACKET) ? 34
			: (Op == OP_BANK_GOLD_RESULT_PACKET_HANDLE) ? 6 : (Op == OP_TRADE_ON_ITEM_BIND_RESULT) ? 61 : 421;
		if ((int32)B.size() < Need)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("%d (подарок): тело %d б < %d — пропуск"), Op, (int32)B.size(), Need);
			break;
		}
		TArray<uint8> Body; Body.SetNumUninitialized(Need);
		FMemory::Memcpy(Body.GetData(), B.data(), Need);
		UE_LOG(LogAsdaNet, Log, TEXT("%d (подарок): код=%d, тело %d б"), Op, (int32)Body[0], Need);
		OnCashGiftPacket.Broadcast(Op, Body);
		break;
	}
	// ★ 10.11.2026 [ОРИГИНАЛ] ОБУЧЕНИЕ: 6185 0x1829 — маска пройденных разделов книги обучения (32 б), сервер шлёт при входе
	//   (`sub_55FE20@0x55FE20`, GameServer.c:135797). Эталон `skillTutorial_RecvBlob32@0xB7ADC0`. Разбор — TUTORIAL.md §3.3.
	case OP_SKILL_TUTORIAL_RECV_BLOB32:
	{
		const std::vector<uint8>& B = Pkt.Body;
		AsdaTutorial::OnMaskPacket(GetGameInstance(), B.data(), (int32)B.size());
		break;
	}
	case OP_NET_HANDLE_AUTH_RESULT:            // 5467 — премиум-баланс (5 б)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 5) { UE_LOG(LogAsdaNet, Warning, TEXT("5467: тело %d б < 5"), (int32)B.size()); break; }
		const uint8 Status = B[0];
		const int32 Balance = AsdaBytes::RdI32(&B[1]);
		UE_LOG(LogAsdaNet, Log, TEXT("5467 премиум-баланс: статус=%d баланс=%d"), (int32)Status, Balance);
		OnPremiumBalance.Broadcast(Status, Balance);
		break;
	}
	// ═══════════ ПАРИКМАХЕРСКАЯ: S→C 5471 результат покупки (9 б) ═══════════
	case OP_HAIR_SHOP_HANDLE_RESULT:           // 5471
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 9) { UE_LOG(LogAsdaNet, Warning, TEXT("5471: тело %d б < 9"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(9);
		FMemory::Memcpy(Body.GetData(), B.data(), 9);
		UE_LOG(LogAsdaNet, Log, TEXT("5471 причёска: код=%d id=%d стиль=%d цвет=%d остаток GXP=%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[1]), (int32)Body[3], (int32)Body[4],
			AsdaBytes::RdI32(&Body[5]));
		OnHairShopResult.Broadcast(Body);
		break;
	}
	// ═══════════ ПАРИКМАХЕРСКАЯ: S→C 5472 «сосед сменил причёску» (10 б) ═══════════
	//   ★★★ Персонажа ищем по **+4 (userNum, i16)**, а НЕ по +0 (charId) — так делает эталон
	//   (`CharManager_FindByUserIdx(v3[2])`, `entity_char.c:45376`); закон `charid-vs-usernum`.
	case OP_CHAR_PACKET_APPLY_HELMET:          // 5472
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 10) { UE_LOG(LogAsdaNet, Warning, TEXT("5472: тело %d б < 10"), (int32)B.size()); break; }
		const int32 CharId  = AsdaBytes::RdI32(&B[0]);
		const int32 UserNum = AsdaBytes::RdI16(&B[4]);
		const int32 HairId  = AsdaBytes::RdI16(&B[6]);
		const uint8 Style   = B[8];
		const uint8 Color   = B[9];
		UE_LOG(LogAsdaNet, Log,
			TEXT("5472 сосед сменил причёску: userNum=%d (charId=%d, НЕ используется) id=%d стиль=%d цвет=%d"),
			UserNum, CharId, HairId, (int32)Style, (int32)Color);
		OnHairChanged.Broadcast(UserNum, HairId, Style, Color);
		break;
	}
	case OP_GACHA_SCENE_ON_SERVER_RESULT:      // 6578 — результат прокрута (63 б)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 63) { UE_LOG(LogAsdaNet, Warning, TEXT("6578: тело %d б < 63"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(63);
		FMemory::Memcpy(Body.GetData(), B.data(), 63);
		UE_LOG(LogAsdaNet, Log, TEXT("6578 гача-прокрут: статус=%d приз=%d новый баланс=%d"),
			(int32)Body[0], AsdaBytes::RdI32(&Body[1]), AsdaBytes::RdI32(&Body[59]));
		OnPremiumGachaResult.Broadcast(Body);
		break;
	}
	case OP_UI_GACHA_RESULT_APPLY:             // 6582 — приз зачислен (61 б)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 61) { UE_LOG(LogAsdaNet, Warning, TEXT("6582: тело %d б < 61"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(61);
		FMemory::Memcpy(Body.GetData(), B.data(), 61);
		UE_LOG(LogAsdaNet, Log, TEXT("6582 приз зачислен: статус=%d контейнер=%d слот=%d предмет=%d"),
			(int32)Body[0], (int32)Body[1], AsdaBytes::RdI16(&Body[2]), AsdaBytes::RdI32(&Body[4]));
		OnPremiumGachaApply.Broadcast(Body);
		break;
	}
	// ═════════════════ ЭЛИТНЫЙ ПОЧТОВЫЙ ЯЩИК: приём 11005 / 11007 ═════════════════
	case OP_RECV_PREMIUM_ITEM_LIST_RES:        // 11005 — содержимое ящика (4 + N×10)
	{
		const std::vector<uint8>& B = Pkt.Body;
		// Минимум — один CharIndex (пустой ящик: N = 0). Меньше 4 байт значит битый кадр.
		if (B.size() < 4) { UE_LOG(LogAsdaNet, Warning, TEXT("11005: тело %d б < 4"), (int32)B.size()); break; }
		TArray<uint8> Body = NetBodyToArray(B);   // ★ N8 (В1 дедупа №2): копия тела — общей точкой
		const int32 N = (Body.Num() - 4) / 10;
		UE_LOG(LogAsdaNet, Log, TEXT("11005 элитный ящик: CharIndex=%d, записей=%d (тело %d б)"),
			AsdaBytes::RdI32(&Body[0]), N, Body.Num());
		if ((Body.Num() - 4) % 10 != 0)
		{
			// Сервер (`RecvDB_PremiumItemList_Res@0x4F8050`) сам отбрасывает такой ответ БД, но
			//   если он всё же пришёл — читаем ТОЛЬКО целые записи (закон «не падать на данных»).
			UE_LOG(LogAsdaNet, Warning, TEXT("11005: длина не кратна 10 (остаток %d б) — хвост отброшен"),
				(Body.Num() - 4) % 10);
		}
		OnPremiumPostList.Broadcast(Body);
		break;
	}
	case OP_RECV_PREMIUM_ITEM_TO_INVEN_RES:    // 11007 — результат забора (409 б)
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 409) { UE_LOG(LogAsdaNet, Warning, TEXT("11007: тело %d б < 409"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(409);
		FMemory::Memcpy(Body.GetData(), B.data(), 409);
		UE_LOG(LogAsdaNet, Log, TEXT("11007 элитный ящик: статус=%d CharIndex=%d вес=%d BuyType=%d"),
			(int32)Body[0], AsdaBytes::RdI32(&Body[1]), AsdaBytes::RdI32(&Body[404]), (int32)Body[408]);
		OnPremiumPostApply.Broadcast(Body);
		break;
	}
	case OP_BOOSTER_SHOP_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 68) { UE_LOG(LogAsdaNet, Warning, TEXT("5469: тело %d б < 68"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(68);
		FMemory::Memcpy(Body.GetData(), B.data(), 68);
		UE_LOG(LogAsdaNet, Log, TEXT("5469 гача: result=%d редкий=%d потрачено=%d приз=%d вес=%d"),
			(int32)Body[0], (int32)Body[1], AsdaBytes::RdI32(&Body[2]),
			AsdaBytes::RdI32(&Body[9]), AsdaBytes::RdI16(&Body[66]));
		OnGachaResult.Broadcast(Body);
		break;
	}
	// ★★★ 5473 (7 б) `OnRecv_OtherPlayerBoost@0xF4C700` — ЧУЖОЙ ЭФФЕКТ УДАЧИ.
	//   @0 i32 charId (клиент оригинала его не читает), @4 i16 userNum, @6 u8 «редкий».
	//   Отправитель исключён сервером (`SendPacket_ClippingSectorInOtherUser@0x41EA80`).
	case OP_ON_RECV_OTHER_PLAYER_BOOST:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 7) { UE_LOG(LogAsdaNet, Warning, TEXT("5473: тело %d б < 7"), (int32)B.size()); break; }
		const int32 UserNum = AsdaBytes::RdI16(&B[4]);
		const bool  bRare   = (B[6] == 1);
		UE_LOG(LogAsdaNet, Log, TEXT("5473 эффект удачи соседа: userNum=%d редкий=%d"), UserNum, (int32)bRare);
		OnOtherPlayerBoost.Broadcast(UserNum, bRare);
		break;
	}
	// ★ 5452 (4 б) — истёк/снят активный функциональный предмет: @0 i32 itemCode.
	case OP_RECV_ITEM_REMOVE_BY_ID:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 4) { UE_LOG(LogAsdaNet, Warning, TEXT("5452: тело %d б < 4"), (int32)B.size()); break; }
		const int32 Code = AsdaBytes::RdI32(&B[0]);
		UE_LOG(LogAsdaNet, Log, TEXT("5452 снятие функционального предмета: код=%d"), Code);
		OnFunctionItemExpired.Broadcast(Code);
		break;
	}
	// ★★★ 5456 (2 б) — РАСШИРЕНИЕ СКЛАДА: новая ёмкость В ЯЧЕЙКАХ (`CCharacter+0x456`).
	//   НЕ ВЕС. Полный вывод и правила приёма — в шапке `OnBagExpand` (AsdaNetSubsystem.h).
	//   Эталон `inventoryChangeBagPage@0xE8E440` (`item_inventory.c:59130`) 1:1.
	case OP_RECV_WAREHOUSE_CHANGE_BAG_PAGE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 2) { UE_LOG(LogAsdaNet, Warning, TEXT("5456: тело %d б < 2"), (int32)B.size()); break; }
		const int32 NewSlots = AsdaBytes::RdI16(&B[0]);
		const int32 PrevSlots = SelfStoreSlotsMax;
		int32 NoticeLoc = 0;
		bool  bAccepted = false;
		// ★ Эталон в трёх ветках из четырёх шлёт текст И в чат, И в модалку, а в ветке отката
		//   (`новое/30 < 2`) — ТОЛЬКО в модалку. Держим этот признак отдельно, иначе подписчик
		//   не отличит его от ветки «новое ≤ текущего» (у обеих loc 12834).
		bool  bAlsoChat = true;
		if (PrevSlots >= NewSlots)
		{
			// Эталон: ветка `else` — ни байта не пишет, только loc 12834 (чат + модалка).
			NoticeLoc = 12834;
		}
		else if (PrevSlots == kStoreSlotsCap)
		{
			// Эталон: `if (curPageMax == 270)` — склад уже максимальный, loc 32010 (чат + модалка).
			NoticeLoc = 32010;
		}
		else if (NewSlots / kStoreSlotsPage < 2)
		{
			// Эталон: записал и тут же ОТКАТИЛ на prevPageMax, loc 12834 — БЕЗ чата.
			NoticeLoc = 12834;
			bAlsoChat = false;
		}
		else
		{
			SelfStoreSlotsMax = NewSlots;
			NoticeLoc = 12833;
			bAccepted = true;
		}
		UE_LOG(LogAsdaNet, Log,
			TEXT("5456 расширение склада: ёмкость %d → %d ячеек (%d вкладок), принято=%d, уведомление loc %d. ")
			TEXT("Вес НЕ ТРОГАЕТСЯ (остаётся %d/%d)"),
			PrevSlots, SelfStoreSlotsMax, SelfStoreSlotsMax / kStoreSlotsPage,
			bAccepted ? 1 : 0, NoticeLoc, SelfWeightCur, SelfWeightMax);
		OnBagExpand.Broadcast(SelfStoreSlotsMax, bAccepted, NoticeLoc, bAlsoChat);
		break;
	}
	// ★ 5457 (57 б) — ВОССТАНОВЛЕНИЕ ПРОЧНОСТИ: полная запись предмета (прочность в байте +13).
	case OP_RECV_INVENTORY_CONFIRM_DYE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 57) { UE_LOG(LogAsdaNet, Warning, TEXT("5457: тело %d б < 57"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(57);
		FMemory::Memcpy(Body.GetData(), B.data(), 57);
		UE_LOG(LogAsdaNet, Log, TEXT("5457 починка: код=%d контейнер=%d слот=%d прочность=%d"),
			AsdaBytes::RdI32(&Body[0]), (int32)Body[4], AsdaBytes::RdI16(&Body[5]), (int32)Body[13]);
		OnItemRepaired.Broadcast(Body);
		break;
	}
	// ★ 5459 (84 б) — статус-нотис функционального предмета: @0 u8 код, @25 57 б запись предмета.
	case OP_RECV_ITEM_STATUS_NOTICE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 84) { UE_LOG(LogAsdaNet, Warning, TEXT("5459: тело %d б < 84"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(84);
		FMemory::Memcpy(Body.GetData(), B.data(), 84);
		UE_LOG(LogAsdaNet, Log, TEXT("5459 статус функционального предмета: result=%d код=%d"),
			(int32)Body[0], AsdaBytes::RdI32(&Body[25]));
		OnItemStatusNotice.Broadcast(Body);
		break;
	}
	// ★★★ 6130 (81 б) `itemListWnd_OnFillPacket@0x974270` — КТО-ТО ПОХВАСТАЛСЯ ПРЕДМЕТОМ.
	//   Тело: @0 i32 (клиентом НЕ читается), @4 char[20] имя cp1251, @24 запись предмета 57 б.
	//   Эталон копирует РОВНО 0x51 б (`memcpy(v2, Src + 9, 0x51u)`) и кладёт запись в список окна
	//   28100. Здесь только копим и раздаём — окно рисует `UAsdaShowOffSubsystem` в своём Tick.
	case OP_ITEM_LIST_WND_ON_FILL_PACKET:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 81) { UE_LOG(LogAsdaNet, Warning, TEXT("6130: тело %d б < 81"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(81);
		FMemory::Memcpy(Body.GetData(), B.data(), 81);
		const FString Who = FAsdaLoginData::DecodeCp1251(Body.GetData() + 4, 20);
		const int32 Code = AsdaBytes::RdI32(&Body[24]);
		const int32 Raw0 = AsdaBytes::RdI32(&Body[0]);
		// Звено №2 цепочки: пакет ДОШЁЛ до диспетчера. @0 клиент-эталон не читает — печатаем для сверки
		//   (при broadcast там charId отправителя, при адресной посылке — charId адресата).
		UE_LOG(LogAsdaNet, Log,
			TEXT("6130 хвастовство: «%s» показывает предмет %d (@0=%d, наш charId=%d, подписчиков=%d)"),
			*Who, Code, Raw0, SelectedCharId, OnItemShowOff.IsBound() ? 1 : 0);
		OnItemShowOff.Broadcast(Body);
		break;
	}
	// ★★★ 6166 (117 б) `invHandleItemSlotSyncPacket@0xFCA590` — РЕЗУЛЬТАТ 6165 «снасть на удочку»
	//   и синхронизация двух слотов. @0 result, @1 i16 текущий вес, @3 и @60 — две записи по 57 б.
	case OP_INV_HANDLE_ITEM_SLOT_SYNC_PACKET:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 117) { UE_LOG(LogAsdaNet, Warning, TEXT("6166: тело %d б < 117"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(117);
		FMemory::Memcpy(Body.GetData(), B.data(), 117);
		const int32 NewCurWeight = AsdaWeightFromWire(AsdaBytes::RdI16(&Body[1]));   // ★ 29.10.2026: вес u16
		UE_LOG(LogAsdaNet, Log, TEXT("6166 снасть на удочку: result=%d вес=%d"), (int32)Body[0], NewCurWeight);
		OnFishingToolResult.Broadcast(Body);
		// ★ Вес приезжает в этом пакете так же, как в 5003/4999 — обновляем полосу мини-статуса,
		//   но ТОЛЬКО при успехе: при отказе сервер шлёт нулевое тело (`v21[0]=код`, остальное нули).
		if (Body[0] == 1) { SetSelfWeight(NewCurWeight, -1, TEXT("6166")); }
		break;
	}
	// ═══ ★★★ РЫБАЛКА (волна В1, 08.10.2026): приём 6164/6168/6170/6172/6173/6183 и износ 6570 ═══
	//   Раскладки — `Reports/owner_1008/fishing_parts/client_net.md` §2. Эталон читает тело с `Src+9`
	//   фиксированной длиной (`memcpy(…, Src + 9, N)`); сервер дописывает случайный хвост 0..19 б — отбрасываем.
	//   Разбор веток — `UAsdaFishingSubsystem` (очередь → Tick, закон no-reentrant-net-callbacks).
	//   ★ Пакеты 6164 идут в потоке загрузки персонажа — здесь ничего не фильтруется по «до 1021».
	case OP_UI_UPDATE_STAT_VALUE_LABEL:          // 6164 (4 б): i32 навык рыбалки
	case OP_NET_HANDLE_TOOLTIP_NOTICE_PACKET:    // 6168 (7 б): u8 код, i32 FishCode/требование, i16 шкала
	case OP_RECV_SET_INPUT_GAUGE_VALUE:          // 6170 (5 б): u8 код, i16 фаза, i16 шкала
	case OP_NET_ON_ITEM_UPGRADE_RESULT:          // 6172 (121 б): результат рыбалки (имя опкода — МИСНЕЙМ)
	case OP_STAT_RECV_STAT_SLOT_UPDATE:          // 6173 (11 б): чужой рыбак (имя опкода — МИСНЕЙМ)
	case OP_NET_HANDLE_INVENTORY_UPDATE:         // 6183 (64 б): награда книги / коды 4,6 из EndFishing
	case OP_NET_APPLY_SLOT_STATE_FLAG:           // 6570 (4 б): износ предмета
	// ★ Волна В3 (08.10.2026): книга рыбака — та же копия фиксированной длины (эталон `memcpy(…, Src + 9, N)`).
	//   6174 идут в потоке загрузки персонажа (по одной на зарегистрированный том) — здесь ничего не фильтруется.
	case OP_LIST_PARSE_ROSTER_ENTRY_PACKET:      // 6174 (305 б): запись тома
	case OP_INV_HANDLE_ITEM_USE_RESULT_PACKET:   // 6177 (365 б): ответ на 6176 (регистрация книги)
	case OP_ITEM_COLLECT_ON_REMOVE_PACKET:       // 6179 (5 б): ответ на 6178 (удаление книги)
	case OP_NET_HANDLE_INVENTORY_UPDATE_9020:    // 6181 (365 б): ответ на 6180 (сдача рыбы)
	{
		const std::vector<uint8>& B = Pkt.Body;
		int32 Need = 0;                          // фиксированная длина тела эталона
		FOnAsdaFishPacket* Dst = nullptr;        // кому отдать копию
		switch ((NetOpcode)Pkt.Opcode)
		{
		case OP_UI_UPDATE_STAT_VALUE_LABEL:       Need = 4;   Dst = &OnFishSkill6164;       break;
		case OP_NET_HANDLE_TOOLTIP_NOTICE_PACKET: Need = 7;   Dst = &OnFishStart6168;       break;
		case OP_RECV_SET_INPUT_GAUGE_VALUE:       Need = 5;   Dst = &OnFishGauge6170;       break;
		case OP_NET_ON_ITEM_UPGRADE_RESULT:       Need = 121; Dst = &OnFishResult6172;      break;
		case OP_STAT_RECV_STAT_SLOT_UPDATE:       Need = 11;  Dst = &OnFishPeer6173;        break;
		case OP_NET_HANDLE_INVENTORY_UPDATE:      Need = 64;  Dst = &OnFishBookReward6183;  break;
		case OP_LIST_PARSE_ROSTER_ENTRY_PACKET:   Need = 305; Dst = &OnFishBookPage6174;    break;
		case OP_INV_HANDLE_ITEM_USE_RESULT_PACKET: Need = 365; Dst = &OnFishBookRegist6177; break;
		case OP_ITEM_COLLECT_ON_REMOVE_PACKET:    Need = 5;   Dst = &OnFishBookDelete6179;  break;
		case OP_NET_HANDLE_INVENTORY_UPDATE_9020: Need = 365; Dst = &OnFishBookUpdate6181;  break;
		default:                                  Need = 4;   Dst = &OnSlotDurability6570;  break;
		}
		// Эталон делает `memset(0)` + `memcpy(Src+9, N)` — короткое тело дополняется нулями, а не выбрасывается.
		TArray<uint8> Body; Body.SetNumZeroed(Need);
		if (!B.empty()) { FMemory::Memcpy(Body.GetData(), B.data(), FMath::Min<int32>((int32)B.size(), Need)); }
		if ((int32)B.size() < Need)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("%d: тело %d б < %d — дополнено нулями (как memset эталона)"),
				(int32)Pkt.Opcode, (int32)B.size(), Need);
		}
		// ★ 6172 код ≤ 2: `HUD_UpdateMiniWeightBar(@3)` — ★ ревизия В1 МЕЛК-8 (08.10.2026, волна В2): вес ставит
		//   `UAsdaFishingSubsystem::Apply6172` в Tick, ПОСЛЕ смены состояния автомата — порядок эталона (g:607453).
		UE_LOG(LogAsdaNet, Log, TEXT("%d рыбалка/износ: тело %d б, байт0=%d, подписчиков=%d"),
			(int32)Pkt.Opcode, Need, (int32)Body[0], Dst->IsBound() ? 1 : 0);
		Dst->Broadcast(Body);
		break;
	}
	// ═══ ★★★ ДОСКА ОБЪЯВЛЕНИЙ (28.10.2026): 6588 / 6589 / 6590 / 6594 / 6595 — `Docs/Port/systems/BOARD_QUEST_FLOW.md` §5 ═══
	//   Эталон: диспетчер network.c:939-951 зовёт `packetParseRecordList8@0xEEE190` (6588), `recvQuestCraftAck@0xEEE2C0` (6589,
	//   `return 1`), `buddyOnStatusUpdatePacket@0xEEDAF0` (6590, `memcpy(…, Src+9, 11)`), `recv_ItemExchangeResult@0xEEE3D0` (6594,
	//   `memcpy(…, Src+9, 0x1E4)`), `itemListClearReadyFlags@0xEEE300` (6595). Фиксированные тела — копия эталонной длины с
	//   дополнением нулями (как `memset` эталона), хвост-паддинг сервера отброшен; 6588 — тело как есть (число узлов считает
	//   подсистема по целым 8-байтовым узлам). Здесь только копия и передача — разбор в Tick подсистемы (no-reentrant-net-callbacks).
	//   Пакеты 6588 идут в потоке входа на карту — здесь ничего не фильтруется по «до 1021».
	case OP_PACKET_PARSE_RECORD_LIST8:          // 6588: N × 8 узлов
	case OP_RECV_QUEST_CRAFT_ACK:               // 6589: пустой
	case OP_BUDDY_ON_STATUS_UPDATE_PACKET:      // 6590: 11 б
	case OP_ITEM_EXCHANGE_RESULT:               // 6594: 484 б
	case OP_ITEM_LIST_CLEAR_READY_FLAGS:        // 6595: 0 б
	{
		const std::vector<uint8>& B = Pkt.Body;
		int32 Need = -1;                        // фиксированная длина тела эталона (−1 — переменная, тело как есть)
		switch ((NetOpcode)Pkt.Opcode)
		{
		case OP_BUDDY_ON_STATUS_UPDATE_PACKET: Need = 11;  break;
		case OP_ITEM_EXCHANGE_RESULT:          Need = 484; break;
		case OP_RECV_QUEST_CRAFT_ACK:          Need = 0;   break;
		case OP_ITEM_LIST_CLEAR_READY_FLAGS:   Need = 0;   break;
		default:                               Need = -1;  break;
		}
		TArray<uint8> Body;
		if (Need < 0) { Body = NetBodyToArray(B); }
		else
		{
			Body.SetNumZeroed(Need);
			if (Need > 0 && !B.empty()) { FMemory::Memcpy(Body.GetData(), B.data(), FMath::Min<int32>((int32)B.size(), Need)); }
			if ((int32)B.size() < Need)
			{
				UE_LOG(LogAsdaNet, Warning, TEXT("%d (доска): тело %d б < %d — дополнено нулями (как memset эталона)"),
					(int32)Pkt.Opcode, (int32)B.size(), Need);
			}
		}
		UE_LOG(LogAsdaNet, Log, TEXT("%d доска объявлений: тело %d б (на проводе %d), подписчиков=%d"),
			(int32)Pkt.Opcode, Body.Num(), (int32)B.size(), OnBoardPacket.IsBound() ? 1 : 0);
		OnBoardPacket.Broadcast((int32)Pkt.Opcode, Body);
		break;
	}
	// ═══ ВОЙНА ФРАКЦИЙ ЗА ГОРОДА: 6743 «кому принадлежат Альпия / Силарис / Пламения» ═══
	// Разбор 1:1 с `uniconWar_UpdateFactionStateUI@0x9C8B90`; раскладка записи — в объявлении в .h.
	case OP_UNICON_WAR_UPDATE_FACTION_STATE_UI:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 274) { UE_LOG(LogAsdaNet, Warning, TEXT("6743: тело %d б < 274"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(274);
		FMemory::Memcpy(Body.GetData(), B.data(), 274);
		// Эталон применяет пакет ТОЛЬКО при коде 1 (`if (LOBYTE(v11[0]) == 1)`).
		if (Body[0] != 1)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("6743: код %d != 1 — состояние не применяем"), (int32)Body[0]);
			break;
		}
		ApplyCityOwnerBody(Body);
		break;
	}

	// ═══ МАГАЗИН NPC ═══
	// ★ 5021 (406 б) `Recv_ShopSell_Result@0xE28FD0` — ответ на 5020 «купить».
	//   ⚠ Имя опкода в перечислении (`OP_SHOP_SELL_RESULT_0`) ВРЁТ: это ответ на ПОКУПКУ.
	//   Доказано сервером: `CharItemBuy@0x50F170` отвечает `sub_4C28F0(…, 5021, 406, …)`.
	case OP_SHOP_SELL_RESULT_0:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 406) { UE_LOG(LogAsdaNet, Warning, TEXT("5021: тело %d б < 406"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(406);
		FMemory::Memcpy(Body.GetData(), B.data(), 406);
		UE_LOG(LogAsdaNet, Log, TEXT("5021 покупка у NPC: result=%d деньги=%d вес=%d"),
			(int32)Body[0], AsdaBytes::RdI32(&Body[400]), AsdaBytes::RdI16(&Body[404]));
		OnShopBuyResult.Broadcast(Body);
		// Вес эталон обновляет только в ветке успеха (`HUD_UpdateMiniWeightBar`, network.c:28166).
		if (Body[0] == 1) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[404])), -1, TEXT("5021")); }   // ★ 29.10.2026: вес u16
		break;
	}
	// ★ 5019 (62 б) `Recv_ShopBuy_Result@0xE31630` — ответ на 5018 «продать».
	//   ⚠ Имя опкода (`OP_SHOP_BUY_RESULT`) тоже ВРЁТ — это ответ на ПРОДАЖУ
	//   (`CharItemSell@0x50D240` → `sub_4C28F0(…, 5019, 62, …)`).
	case OP_SHOP_BUY_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 62) { UE_LOG(LogAsdaNet, Warning, TEXT("5019: тело %d б < 62"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(62);
		FMemory::Memcpy(Body.GetData(), B.data(), 62);
		UE_LOG(LogAsdaNet, Log, TEXT("5019 продажа NPC: result=%d деньги=%d вес=%d"),
			(int32)Body[0], AsdaBytes::RdI32(&Body[56]), AsdaBytes::RdI16(&Body[60]));
		OnShopSellResult.Broadcast(Body);
		if (Body[0] == 1) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[60])), -1, TEXT("5019")); }   // ★ 29.10.2026: вес u16
		break;
	}

	// ═══ РЕМОНТ СНАРЯЖЕНИЯ (окно 57300) ═══
	// ★ 6572 (170 б) `netHandleStateArrayPacket@0xEDB2B0` — ответ на 6571 «починить».
	case OP_NET_HANDLE_STATE_ARRAY_PACKET:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 170) { UE_LOG(LogAsdaNet, Warning, TEXT("6572: тело %d б < 170"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(170);
		FMemory::Memcpy(Body.GetData(), B.data(), 170);
		UE_LOG(LogAsdaNet, Log, TEXT("6572 ремонт: result=%d деньги=%d"),
			AsdaBytes::RdI16(&Body[0]), AsdaBytes::RdI32(&Body[166]));
		OnRepairResult.Broadcast(Body);
		break;
	}

	// ═══ ПЕРЕДАЧА ПАРАМЕТРОВ (окно 16000) ═══
	// ★ 5434 (184 б) `Recv_ExtractShop@0xC92AD0` — ответ на 5433 «передать параметры».
	case OP_EXTRACT_SHOP:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 184) { UE_LOG(LogAsdaNet, Warning, TEXT("5434: тело %d б < 184"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(184);
		FMemory::Memcpy(Body.GetData(), B.data(), 184);
		UE_LOG(LogAsdaNet, Log, TEXT("5434 передача параметров: код=%d вес=%d золото=%d userNum=%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[172]), AsdaBytes::RdI32(&Body[174]),
			AsdaBytes::RdI16(&Body[182]));
		OnExtractShopResult.Broadcast(Body);
		// ★ 1:1 эталон: вес принимается ТОЛЬКО в ветках 9 и 10 (`HUD_UpdateMiniWeightBar(g_pHUD,
		//   body[86])` стоит именно там; ветки отказов 0…7 его не трогают).
		if (Body[0] == 9 || Body[0] == 10)
		{
			SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[172])), -1, TEXT("5434"));   // ★ 29.10.2026: вес u16
		}
		break;
	}

	// ═══ ЗНАК ЗОДИАКА (окно 74500) ═══
	// ★ 6587 (тело 6 б) `itemGradeNotice_HandleResult@0xECC6F0` — ответ на 6586 «выбрать знак».
	//   ⚠ ИМЯ ЭТАЛОНА ВРЁТ: к «грейду предмета» пакет отношения не имеет — это знак зодиака.
	case OP_ITEM_GRADE_NOTICE_HANDLE_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 6) { UE_LOG(LogAsdaNet, Warning, TEXT("6587: тело %d б < 6"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(6);
		FMemory::Memcpy(Body.GetData(), B.data(), 6);
		UE_LOG(LogAsdaNet, Log, TEXT("6587 знак зодиака: код=%d charId=%d знак=%d"),
			(int32)Body[0], AsdaBytes::RdI32(&Body[1]), (int32)Body[5]);
		// ★ 1:1 эталон (`case 1`): поле `CCharacter+0x460` (char+1120) обновляется ДО показа нотиса.
		//   У нас это зеркало — `SelfConstellation`, из него читает окно 900 и гейт открытия 74500.
		if (Body[0] == 1) { SelfConstellation = Body[5]; }
		OnConstellationResult.Broadcast(Body);
		break;
	}

	// ═══ ПЕРЕНОС УЛУЧШЕНИЯ КАМНЕМ (31.10.2026, баг 4) ═══
	// ★ 6552 (119 б) `inventory_parseItemUpdatePacket@0xB65780` — ответ на 6551. Записи и нотисы — UAsdaUpgradeStoneSubsystem.
	case OP_INVENTORY_PARSE_ITEM_UPDATE_PACKET:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 119) { UE_LOG(LogAsdaNet, Warning, TEXT("6552: тело %d б < 119"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(119);
		FMemory::Memcpy(Body.GetData(), B.data(), 119);
		const int32 Code = (int32)(int8)Body[0];
		UE_LOG(LogAsdaNet, Log, TEXT("6552 перенос улучшения: result=%d вес=%d"), Code, AsdaBytes::RdI32(&Body[115]));
		// ★ 1:1 эталон (`case 1: case 5: HUD_UpdateMiniWeightBar(g_pHUD, *(int*)((char*)&pkt[57] + 1))`) — в ветках 1 и 5,
		//   ДО записей и БЕЗ гейта `> 0` (у 6554 гейт есть, здесь нет).
		if (Code == 1 || Code == 5)
		{
			SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI32(&Body[115])), -1, TEXT("6552"));
		}
		OnUpgradeBlessResult.Broadcast(Body);
		break;
	}
	// ★ 6550 (577 б) `netHandleItemOpResultPacket@0xB66C20` — ответ на 6549 «разделить улучшение» (окно 50500).
	case OP_NET_HANDLE_ITEM_OP_RESULT_PACKET:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 577) { UE_LOG(LogAsdaNet, Warning, TEXT("6550: тело %d б < 577"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(577);
		FMemory::Memcpy(Body.GetData(), B.data(), 577);
		const int32 Code = (int32)(int8)Body[0];
		UE_LOG(LogAsdaNet, Log, TEXT("6550 разделение улучшения: result=%d исход=%d камней=%d вес=%d"),
			Code, (int32)(int8)Body[575], (int32)(int8)Body[576], AsdaBytes::RdI32(&Body[571]));
		// ★ 1:1 эталон (`case 1: … HUD_UpdateMiniWeightBar(g_pHUD, *(int*)((char*)&itemOpPayload[285] + 1))`) — только код 1, без гейта.
		if (Code == 1)
		{
			SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI32(&Body[571])), -1, TEXT("6550"));
		}
		OnUpgradeExtractionResult.Broadcast(Body);
		break;
	}

	// ═══ ИЗМЕНЕНИЕ ПАРАМЕТРОВ ПРЕДМЕТА (окно 50300) ═══
	// ★ 6554 (119 б) `Net_OnItemUpgradeResult@0xB61990` — ответ на 6553 «изменить параметры».
	case OP_NET_ON_ITEM_UPGRADE_RESULT_6554:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 119) { UE_LOG(LogAsdaNet, Warning, TEXT("6554: тело %d б < 119"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(119);
		FMemory::Memcpy(Body.GetData(), B.data(), 119);
		UE_LOG(LogAsdaNet, Log, TEXT("6554 смена параметров: result=%d вес=%d"),
			(int32)(int8)Body[0], AsdaBytes::RdI32(&Body[115]));
		OnItemOptionTransResult.Broadcast(Body);
		// ★ 1:1 эталон (`if (*(int *)((char *)&pkt[57] + 1) > 0) HUD_UpdateMiniWeightBar(...)`):
		//   вес принимается ТОЛЬКО в ветке успеха и ТОЛЬКО при положительном значении.
		if ((int8)Body[0] == 1)
		{
			// ★ [ИСПРАВЛЕНИЕ] 29.10.2026 (вес ×2): вес u16 — гейт эталона `> 0` теперь не выбрасывает вес > 32767.
			const int32 NewWeight = AsdaWeightFromWire(AsdaBytes::RdI32(&Body[115]));
			if (NewWeight > 0) { SetSelfWeight(NewWeight, -1, TEXT("6554")); }
		}
		break;
	}

	// ═══ СБРОС/ВОССТАНОВЛЕНИЕ УЛУЧШЕНИЙ (окно 20200) ═══
	// ★ 6507 (154 б) `uiInventoryOnRecvSlotUpdate@0xD31430` — ответ на 6506.
	//   Длина одна и та же на успех и на отказ — сервер шлёт `sub_4C28F0(…, 6507, 154, …)`.
	case OP_UI_INVENTORY_ON_RECV_SLOT_UPDATE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 154) { UE_LOG(LogAsdaNet, Warning, TEXT("6507: тело %d б < 154"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(154);
		FMemory::Memcpy(Body.GetData(), B.data(), 154);
		UE_LOG(LogAsdaNet, Log, TEXT("6507 сброс улучшений: код=%d вес=%d золото=%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[1]), AsdaBytes::RdI32(&Body[3]));
		OnItemResetResult.Broadcast(Body);
		break;
	}

	// ═══ УЛУЧШЕНИЕ СНАРЯЖЕНИЯ / ЗАТОЧКА (окно 20000) ═══
	// ★ 6505 (231 б) `Recv_ItemUpgradeBatch@0xD2B1E0` — ответ на 6504. Длина одна на все исходы:
	//   и успех/провал (`recvDBShellItemUpgrade@0x500800`), и 13 ранних отказов шлют `sub_4C28F0(…, 6505, 231, …)`.
	case OP_ON_RECV_ITEM_UPGRADE_BATCH:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 231) { UE_LOG(LogAsdaNet, Warning, TEXT("6505: тело %d б < 231"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(231);
		FMemory::Memcpy(Body.GetData(), B.data(), 231);
		UE_LOG(LogAsdaNet, Log, TEXT("6505 заточка: код=%d вес=%d предмет код=%d ступень=%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[1]), AsdaBytes::RdI32(&Body[3]), (int32)Body[3 + 24]);
		// ★ 1:1 эталон: `HUD_UpdateMiniWeightBar(g_pHUD, *(WORD*)(msg+1))` стоит ТОЛЬКО в ветке кода 1
		//   (network.c:19646); в ветке провала 8 и в отказах вес не трогается.
		if (Body[0] == 1) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[1])), -1, TEXT("6505")); }   // ★ 29.10.2026: вес u16
		OnItemUpgradeResult.Broadcast(Body);
		break;
	}
	// ═══ ВЫСОКАЯ ПЕРЕПЛАВКА (окно 78200) ═══
	// ★ 6501 (235 б) `Recv_ItemUpdate@0xCF5B90` — ответ на 6500. Длина одна на все исходы (сервер
	//   `sub_4C28F0(…, 6501, 235, …)` и в ранних отказах, и в `recvDBUpperRefine`).
	case OP_ITEM_UPDATE:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 235) { UE_LOG(LogAsdaNet, Warning, TEXT("6501: тело %d б < 235"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(235);
		FMemory::Memcpy(Body.GetData(), B.data(), 235);
		UE_LOG(LogAsdaNet, Log, TEXT("6501 высокая переплавка: код=%d вес=%d золото=%d предмет код=%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[229]), AsdaBytes::RdI32(&Body[231]), AsdaBytes::RdI32(&Body[172]));
		// ★ 1:1 эталон: `HUD_UpdateMiniWeightBar(g_pHUD, *(i16*)(a2 + 229))` — только в ветке кодов 1/8
		//   (network.c:16924); в отказах вес не трогается.
		if (Body[0] == 1 || Body[0] == 8) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[229])), -1, TEXT("6501")); }   // ★ 29.10.2026: вес u16
		OnHighRefineResult.Broadcast(Body);
		break;
	}
	// ═══ РАЗБОРКА СНАРЯЖЕНИЯ (окно 32000), волна 2-А 06.10.2026 ═══
	// ★ 6503 (119 б) `Recv_ItemDisjoint@0xE0EAD0` — ответ на 6502. Длина одна на все исходы (сервер
	//   `sub_4C28F0(…, 6503, 119, …)` и в ранних отказах `Recv_User_Disassemble`, и в ответе БД `sub_4FE900`).
	case OP_ITEM_DISJOINT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 119) { UE_LOG(LogAsdaNet, Warning, TEXT("6503: тело %d б < 119"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(119);
		FMemory::Memcpy(Body.GetData(), B.data(), 119);
		UE_LOG(LogAsdaNet, Log, TEXT("6503 разборка: код=%d вес=%d предмет (код %d к=%d сл=%d) результат (код %d к=%d сл=%d) выдано=%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[1]),
			AsdaBytes::RdI32(&Body[3]), (int32)Body[3 + 4], AsdaBytes::RdI16(&Body[3 + 5]),
			AsdaBytes::RdI32(&Body[60]), (int32)Body[60 + 4], AsdaBytes::RdI16(&Body[60 + 5]), AsdaBytes::RdI16(&Body[117]));
		// ★ 1:1 эталон: `HUD_UpdateMiniWeightBar(g_pHUD, *(i16*)&gap_2D8[1])` — в ветках кодов 1 и 8 (network.c:27887/27948);
		//   в отказах вес не трогается. Окно повторяет то же значение в `applyResult` (конец полосы).
		if (Body[0] == 1 || Body[0] == 8) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[1])), -1, TEXT("6503")); }   // ★ 29.10.2026: вес u16
		OnDisassembleResult.Broadcast(Body);
		break;
	}
	// ═══ СОЗДАНИЕ СНАРЯЖЕНИЯ / РЕМЕСЛО (окна 76000/77000/77500), волна В1 25.09.2026 ═══
	//   Длины — по серверным `sub_4C28F0(…, опкод, N, …)`; раскладки и ветки — в `UAsdaCraftSubsystem`.
	//   Вес ставится ЗДЕСЬ и только в тех ветках, где его ставит эталон (`HUD_UpdateMiniWeightBar`).
	case OP_SHOP_PARSE_STOCK_PACKET:              // 6597 `shopParseStockPacket@0xFDEEB0` (миснейм)
	case OP_NET_HANDLE_INVENTORY_UPDATE_PACKET:   // 6599 `netHandleInventoryUpdatePacket@0xFDF080` (миснейм)
	case OP_NET_ON_SUBCODE_NOTICE_HANDLER:        // 6601 `netOnSubcodeNoticeHandler@0xFDF3E0` (миснейм)
	case OP_UPGRADE_DLG_ON_RESULT_PACKET:         // 6603 `upgradeDlg_onResultPacket@0xFE65E0` (миснейм)
	case OP_NET_ON_ITEM_USE_RESULT:               // 6605 `netOnItemUseResult@0xFE0800` (миснейм)
	{
		const int32 Op = (int32)Pkt.Opcode;
		const int32 Need = (Op == 6597) ? 593 : (Op == 6599) ? 64 : (Op == 6601) ? 3 : (Op == 6603) ? 525 : 61;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, Need, Op, TEXT("ремесло"))) { break; }
		TArray<uint8> Body; Body.SetNumUninitialized(Need);
		FMemory::Memcpy(Body.GetData(), B.data(), Need);
		switch (Op)
		{
		case 6597:
			UE_LOG(LogAsdaNet, Log, TEXT("6597 ремесло: изучено=%d лимит=%d уровень=%d опыт=%d процент=%d"),
				AsdaBytes::RdI16(&Body[0]), AsdaBytes::RdI16(&Body[2]), (int32)Body[584], AsdaBytes::RdI32(&Body[585]), AsdaBytes::RdI32(&Body[589]));
			break;
		case 6599:
			UE_LOG(LogAsdaNet, Log, TEXT("6599 ремесло «изучить»: код=%d вес=%d свиток код=%d кол-во=%d рецепт=%d"),
				(int32)Body[0], AsdaBytes::RdI32(&Body[1]), AsdaBytes::RdI32(&Body[5]), AsdaBytes::RdI32(&Body[5 + 9]), AsdaBytes::RdI16(&Body[62]));
			// ★ 1:1 эталон (network.c:49537-49538): только код 1 и только при весе > 0 (i32 @+1).
			if (Body[0] == 1 && AsdaWeightFromWire(AsdaBytes::RdI32(&Body[1])) > 0) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI32(&Body[1])), -1, TEXT("6599")); }   // ★ 29.10.2026: вес u16, гейт `> 0`
			break;
		case 6601:
			UE_LOG(LogAsdaNet, Log, TEXT("6601 ремесло «удалить»: код=%d рецепт=%d"), (int32)Body[0], AsdaBytes::RdI16(&Body[1]));
			break;
		case 6603:
			UE_LOG(LogAsdaNet, Log, TEXT("6603 ремесло «изготовить»: код=%d вес=%d уровень=%d опыт=%d процент=%d ступень=%d +1шт=%d продукт код=%d кол-во=%d"),
				(int32)Body[0], AsdaBytes::RdI16(&Body[1]), (int32)Body[3], AsdaBytes::RdI32(&Body[4]), AsdaBytes::RdI16(&Body[8]),
				(int32)Body[10], (int32)Body[11], AsdaBytes::RdI32(&Body[12]), AsdaBytes::RdI32(&Body[12 + 9]));
			// ★ 1:1 эталон (item_enhance.c:10381-10382 / :10425-10426): коды 1 и 2, вес i16 @+1 при > 0.
			if ((Body[0] == 1 || Body[0] == 2) && AsdaWeightFromWire(AsdaBytes::RdI16(&Body[1])) > 0) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[1])), -1, TEXT("6603")); }   // ★ 29.10.2026: вес u16, гейт `> 0`
			break;
		default:   // 6605
			UE_LOG(LogAsdaNet, Log, TEXT("6605 ремесло «уровень»: код=%d новый уровень=%d вес=%d свиток код=%d кол-во=%d"),
				(int32)Body[0], (int32)Body[1], AsdaBytes::RdI16(&Body[2]), AsdaBytes::RdI32(&Body[4]), AsdaBytes::RdI32(&Body[4 + 9]));
			// ★ 1:1 эталон (network.c:50039-50040): код 1, вес i16 @+2 при > 0.
			if (Body[0] == 1 && AsdaWeightFromWire(AsdaBytes::RdI16(&Body[2])) > 0) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[2])), -1, TEXT("6605")); }   // ★ 29.10.2026: вес u16, гейт `> 0`
			break;
		}
		OnCraftPacket.Broadcast(Op, Body);
		break;
	}
	// ═══ ЛИЧНАЯ ЛАВКА — ответы продавцу (волна Л1 торговли, 29.09.2026) ═══
	//   Длины — по `memcpy(…, Src + 9, N)` приёмников эталона: 6301 — 1 б, 6303 — 452 б, 6306 — 571 б. Разбор, нотисы и окна —
	//   `UAsdaSelfStoreSubsystem` (из своего Tick; колбэк только копит — закон `no-reentrant-net-callbacks`).
	case OP_ITEM_ENHANCE_ON_RESULT:               // 6301 `itemEnhanceOnResult@0xDD4F80` (МИСНЕЙМ = ответ 6300 «подготовка»)
	case OP_PKT_HANDLE_SHOP_WINDOW_OPEN:          // 6303 `pktHandleShopWindowOpen@0xDD3310` (ответ 6302 «открыть»)
	case OP_SELL_WINDOW_ON_SELL_RESULT:           // 6306 `sellWindowOnSellResult@0xDD5230` (МИСНЕЙМ = ответ 6305 «закрыть»)
	{
		const int32 Op = (int32)Pkt.Opcode;
		const int32 Need = (Op == 6301) ? 1 : (Op == 6303) ? 452 : 571;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, Need, Op, TEXT("лавка"))) { break; }
		TArray<uint8> Body; Body.SetNumUninitialized(Need);
		FMemory::Memcpy(Body.GetData(), B.data(), Need);
		UE_LOG(LogAsdaNet, Log, TEXT("%d лавка: код=%d%s"), Op, (int32)Body[0],
			(Op == 6303) ? *FString::Printf(TEXT(" лотов=%d"), (int32)Body[1]) : TEXT(""));
		OnSelfStorePacket.Broadcast(Op, Body);
		break;
	}
	// ═══ ЛИЧНАЯ ЛАВКА — ответы посетителю и продавцу (волна Л2 торговли, 30.09.2026) ═══
	//   Длины — `memcpy(…, Src + 9, N)` приёмников эталона: 6308 — 628, 6310 — 350, 6311/6312 — 281, 6315 — 5. ★ 6308 сервер шлёт
	//   и длиной 4 («Same User Error», GameServer.c:100595): эталон копирует 628 б из буфера сети не глядя на длину — порт
	//   дополняет нулями (код в [0] тот же). Разбор — `UAsdaSelfStoreSubsystem` из его Tick (колбэк только копит).
	case OP_SHOP_HANDLE_OPEN_SHOP_PACKET:         // 6308 `shopHandleOpenShopPacket@0xDDA580` (ответ 6307 «войти»)
	case OP_SHOP_SELL_DLG_ON_SELL_RESULT:         // 6310 `shopSellDlg_OnSellResult@0xDDBF40` (МИСНЕЙМ = ответ 6309 «купить»)
	case OP_SHOP_DLG_ON_BUY_RESULT_PACKET:        // 6311 `shopDlg_onBuyResultPacket@0xDDC480` (МИСНЕЙМ = продавцу «у тебя купили»)
	case OP_SHOP_DLG_ON_CONFIRM_BUY_PACKET:       // 6312 `shopDlg_onConfirmBuyPacket@0xDDCAA0` (МИСНЕЙМ = гостям «витрина изменилась»)
	case OP_SHOP_DLG_APPEND_TRADE_RESULT_LOG:     // 6315 `shopDlgAppendTradeResultLog@0xDDB3F0` (МИСНЕЙМ = гость вошёл/вышел)
	{
		const int32 Op = (int32)Pkt.Opcode;
		const int32 Need = (Op == 6308) ? 628 : (Op == 6310) ? 350 : (Op == 6315) ? 5 : 281;
		const std::vector<uint8>& B = Pkt.Body;
		const int32 Min = (Op == 6308) ? 1 : Need;   // 6308 — любой длины ≥ 1 (см. выше)
		if (!NetNeedLen(B, Min, Op, TEXT("лавка (посетитель)"))) { break; }
		TArray<uint8> Body; Body.SetNumZeroed(Need);
		FMemory::Memcpy(Body.GetData(), B.data(), FMath::Min<int32>(Need, (int32)B.size()));
		UE_LOG(LogAsdaNet, Log, TEXT("%d лавка: [0]=%d длина %d"), Op, (int32)Body[0], (int32)B.size());
		OnSelfStorePacket.Broadcast(Op, Body);
		break;
	}
	// ═══ ЛИЧНАЯ ЛАВКА — ВЫВЕСКА 6304 (волна Л3 торговли, 30.09.2026) ═══
	//   [ОРИГИНАЛ] диспетчер network.c:790 → `charMgrAddNameStatusEntry@0xDDA2E0` (МИСНЕЙМ = «вывеска лавки»): `memcpy(v3, Src + 9,
	//   56)` без сверки длины → очередь `CShopDlg+0x280`. Тело 56 б: [0] u8 открыта 1 / закрыта 0, [1] u8 премиум (== 1), @2 i32 charId,
	//   @6 char[50] название. Сервер шлёт его сектору при открытии (GameServer.c:100286) и закрытии (:100428) и адресно в снапшоте
	//   входящему в обзор, если лавка открыта (:11942/:12702). Короче 56 — дополняется нулями (как `memcpy` из буфера сети).
	//   Разбор — `UAsdaStallSignSubsystem` из его Tick (колбэк только копит, закон `no-reentrant-net-callbacks`).
	case OP_CHAR_MGR_ADD_NAME_STATUS_ENTRY:       // 6304
	{
		const int32 Op = (int32)Pkt.Opcode;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 1, Op, TEXT("лавка (вывеска)"))) { break; }
		TArray<uint8> Body; Body.SetNumZeroed(56);
		FMemory::Memcpy(Body.GetData(), B.data(), FMath::Min<int32>(56, (int32)B.size()));
		UE_LOG(LogAsdaNet, Log, TEXT("6304 вывеска лавки: открыта=%d премиум=%d charId=%d длина %d"), (int32)Body[0], (int32)Body[1],
			AsdaBytes::RdI32(Body.GetData() + 2), (int32)B.size());
		OnSelfStorePacket.Broadcast(Op, Body);
		break;
	}
	// ═══ ЛИЧНАЯ ЛАВКА — ЧАТ 6314 (волна Л4 торговли, 30.09.2026) ═══
	//   [ОРИГИНАЛ] диспетчер network.c:808 → `shopDlg_RecvLogLineColored@0xDDB1F0` (shop_trade.c:17868): `memcpy(v3, Src + 9, 224)` без
	//   сверки длины; сервер шлёт 104 б (`Recv_SelfStoreChatClient` S:101584/101586): @0 i32 номер (0 хозяин, 1..5 гость), @4 имя[20],
	//   @24 текст[80]. Короче 104 — дополняется нулями. Разбор (журнал 23402, 6 цветов) — `UAsdaSelfStoreSubsystem` из его Tick.
	case OP_SHOP_DLG_RECV_LOG_LINE_COLORED:       // 6314
	{
		const int32 Op = (int32)Pkt.Opcode;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 4, Op, TEXT("лавка (чат)"))) { break; }
		TArray<uint8> Body; Body.SetNumZeroed(104);
		FMemory::Memcpy(Body.GetData(), B.data(), FMath::Min<int32>(104, (int32)B.size()));
		UE_LOG(LogAsdaNet, Log, TEXT("6314 чат лавки: номер=%d длина %d"), AsdaBytes::RdI32(Body.GetData()), (int32)B.size());
		OnSelfStorePacket.Broadcast(Op, Body);
		break;
	}
	// ★ 5034 — ответ ЛЕГАСИ-заточки 5033. Эталон (`network.c:1901`) отдаёт его `g_pRecvUiHandler`
	//   (виртуальный слот +24), но окна-владельца этой ветки в сборке нет, а сами мы 5033 не шлём
	//   (`gear_upgrade_re_report.md` §12). Принимаем и пишем в лог — без действий (совместимость протокола).
	case OP_CHAR_SHELL_ITEM_UPGRADE_RESULT:
	{
		UE_LOG(LogAsdaNet, Log, TEXT("5034 легаси-заточка: %d б — принят без действий (5033 порт не шлёт)"),
			(int32)Pkt.Body.size());
		break;
	}

	// ═══ ВОЕННЫЙ МАГАЗИН (окно 67200) ═══
	// ★ 6163 (121 б) `uiHandleItemActionResult@0xB75580` — ответ на 6162 «купить».
	case OP_UI_HANDLE_ITEM_ACTION_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 121) { UE_LOG(LogAsdaNet, Warning, TEXT("6163: тело %d б < 121"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(121);
		FMemory::Memcpy(Body.GetData(), B.data(), 121);
		UE_LOG(LogAsdaNet, Log, TEXT("6163 военный магазин: result=%d вес=%d деньги=%d"),
			(int32)Body[0], AsdaBytes::RdI16(&Body[1]), AsdaBytes::RdI32(&Body[3]));
		OnWarShopBuyResult.Broadcast(Body);
		// Вес эталон обновляет ТОЛЬКО в ветке успеха (`HUD_UpdateMiniWeightBar`, ui_core.c:8291).
		if (Body[0] == 1) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI16(&Body[1])), -1, TEXT("6163")); }   // ★ 29.10.2026: вес u16
		break;
	}

	// ═══ ВОЛНА 2-Б (06.10.2026): ответы 6151 (доклад об охоте) и 6133 (пожертвование), обе по 66 б ═══
	//   Длина — серверный `sub_4C28F0(…, 6151/6133, 66, …)`. Разбор и инвентарь — в подсистемах (Tick), здесь только вес.
	case OP_INVENTORY_UI_ON_ITEM_MOVE_PACKET:   // 6151 `inventoryUI_onItemMovePacket@0xED0820`
	case OP_ON_RECV_REFINE_SLOT_UPDATE:         // 6133 `onRecvRefineSlotUpdate@0xD9F8B0` (миснейм)
	{
		const int32 Op = (int32)Pkt.Opcode;
		const std::vector<uint8>& B = Pkt.Body;
		if (!NetNeedLen(B, 66, Op, TEXT("волна 2-Б"))) { break; }
		TArray<uint8> Body; Body.SetNumUninitialized(66);
		FMemory::Memcpy(Body.GetData(), B.data(), 66);
		if (Op == 6151)
		{
			const int32 Code = (int32)Body[61];
			UE_LOG(LogAsdaNet, Log, TEXT("6151 доклад об охоте: код=%d предмет=%d контейнер=%d слот=%d кол-во=%d вес=%d"),
				Code, AsdaBytes::RdI32(&Body[4]), (int32)Body[8], AsdaBytes::RdI16(&Body[9]), AsdaBytes::RdI32(&Body[13]), AsdaBytes::RdI32(&Body[62]));
			// `case 1: case 2: HUD_UpdateMiniWeightBar(g_pHUD, *(int*)(тело + 62))` — первым действием ветки сдачи.
			if (Code == 1 || Code == 2) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI32(&Body[62])), -1, TEXT("6151")); }   // ★ 29.10.2026: вес u16
			OnHuntReportResult.Broadcast(Body);
		}
		else
		{
			const int32 Code = (int32)Body[4];
			UE_LOG(LogAsdaNet, Log, TEXT("6133 пожертвование: код=%d предмет=%d контейнер=%d слот=%d кол-во=%d вес=%d"),
				Code, AsdaBytes::RdI32(&Body[5]), (int32)Body[9], AsdaBytes::RdI16(&Body[10]), AsdaBytes::RdI32(&Body[14]), AsdaBytes::RdI32(&Body[62]));
			// `if (a2[4] == 1) { … HUD_UpdateMiniWeightBar(g_pHUD, *(int*)(a2 + 62)); }` — вес только при успехе.
			if (Code == 1) { SetSelfWeight(AsdaWeightFromWire(AsdaBytes::RdI32(&Body[62])), -1, TEXT("6133")); }   // ★ 29.10.2026: вес u16
			OnDonationResult.Broadcast(Body);
		}
		break;
	}

	// ═══ САМОЦВЕТЫ ═══
	// ★ 5025 (117 б) `netRecvItemActionResult@0xD0FB10` — ответ на 5024 «камень в гнездо».
	case OP_NET_RECV_ITEM_ACTION_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 117) { UE_LOG(LogAsdaNet, Warning, TEXT("5025: тело %d б < 117"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(117);
		FMemory::Memcpy(Body.GetData(), B.data(), 117);
		UE_LOG(LogAsdaNet, Log, TEXT("5025 самоцвет в гнездо: result=%d"), (int32)Body[0]);
		OnSowelSocketResult.Broadcast(Body);
		break;
	}
	// ★ 5023 (115 б) `netRecvSowelActionResult@0xD0F7D0` — ответ на 5022 «вынуть камень».
	//   Имени в `NetOpcodes.h` у него нет — адресуем числом (в перечислении опкод отсутствует).
	case 5023:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 115) { UE_LOG(LogAsdaNet, Warning, TEXT("5023: тело %d б < 115"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(115);
		FMemory::Memcpy(Body.GetData(), B.data(), 115);
		UE_LOG(LogAsdaNet, Log, TEXT("5023 самоцвет вынут: result=%d"), (int32)Body[0]);
		OnSowelDetachResult.Broadcast(Body);
		break;
	}
	// ★ 5032 (63 б) `sowelWndOnSocketResultPacket@0xD15EE0` — ответ на 5031 «святость на камень».
	case OP_C_CLASS_GET_FIELD142_C:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 63) { UE_LOG(LogAsdaNet, Warning, TEXT("5032: тело %d б < 63"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(63);
		FMemory::Memcpy(Body.GetData(), B.data(), 63);
		const int32 NewCurWeight = AsdaWeightFromWire(AsdaBytes::RdI16(&Body[1]));   // ★ 29.10.2026: вес u16
		UE_LOG(LogAsdaNet, Log, TEXT("5032 святость на камень: result=%d вес=%d"), (int32)Body[0], NewCurWeight);
		OnHolyItemResult.Broadcast(Body);
		// Вес обновляет сам эталон (`HUD_UpdateMiniWeightBar`) только в ветках 1 и 7.
		if (Body[0] == 1 || Body[0] == 7) { SetSelfWeight(NewCurWeight, -1, TEXT("5032")); }
		break;
	}
	// ★ 5007 (18 б) — результат уничтожения предмета (`CharItemDump`/`sendCharItemDump@0x4D3F70`).
	case OP_NET_ON_INVENTORY_USE_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 18) { UE_LOG(LogAsdaNet, Warning, TEXT("5007: тело %d б < 18"), (int32)B.size()); break; }
		TArray<uint8> Body; Body.SetNumUninitialized(18);
		FMemory::Memcpy(Body.GetData(), B.data(), 18);
		UE_LOG(LogAsdaNet, Log, TEXT("5007 уничтожение предмета: result=%d"), (int32)Body[0]);
		OnItemDumpResult.Broadcast(Body);
		break;
	}
	// ★★★ 1027 / 0x403 — ЭКИП ЛОКАЛЬНОГО ИГРОКА (netHandleAppearanceChangeResult@0xF03A50, network.c:34292).
	//   Эталон: memcpy(v11, Src+9, 0x28B); при v11[0]==1 → qmemcpy(&g_aEnterWorldCharData, v11+5, 0x270),
	//   далее поля +624 (word) и +626 (byte) ⇒ всего 627 б = 11 инстансов × 57, ИНДЕКС = ЭКИП-СЛОТ (=itemType).
	//   game_main.c:2807 копирует массив в CCharacter+1588 локального игрока — ИМЕННО оттуда
	//   CCharacter_UpdateHeldItemEffect читает слот 9 (оружие), а CCharacter_UpdateEquipAuraEffect — слот 8.
	//   Живой пакет порта: body[0]=01, body+5 = инстанс слота 0 (itemId 41782 = шлем, itemType 0) ✔.
	case OP_NET_HANDLE_APPEARANCE_CHANGE_RESULT:
	{
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 5 + 57 * 10u) { UE_LOG(LogAsdaNet, Warning, TEXT("0x403: тело мало (%d)"), (int32)B.size()); break; }
		if (B[0] != 1)
		{
			// ★★★ ЭТИМ ЖЕ ОПКОДОМ логин-сервер отвечает ОТКАЗОМ на выбор персонажа (1016).
			//   `Send_SelectChar@0x40AD30` шлёт `sub_415A40(1027, 651, …)` с кодом в байте [0]:
			//     0 — ошибка процедуры `UPCharSoulState` (эталон просто разблокирует кнопку);
			//     2 — вход ОДЕРЖИМЫМ отклонён: соулмейт СЕЙЧАС В ИГРЕ (`UPPreSMConnect.Ustate == 1`).
			//         Эталон показывает нотис loc 131 и ставит флаг «соулмейт онлайн» (+3075039);
			//     3 — вход одержимым отклонён: у аккаунта нет активной пары (`UPCharSoulState`
			//         вернула Flag 0 или 2).
			//   ★ [ОРИГИНАЛ] ДЕФЕКТ UX: код 3 эталонный клиент НЕ показывает вообще — кнопка молча
			//     разблокируется, и игрок не понимает, почему вход не произошёл. Мы отдаём код
			//     наружу делегатом, чтобы слой сцены выбора персонажа мог сказать об этом словами.
			//   Байты тела при отказе — мусор (сервер шлёт 651 б от неинициализированного буфера),
			//   читать что-либо кроме [0] нельзя.
			if (B[0] == 2) { bSoulmateOnline = true; }   // эталон: +3075039 = 1
			// ★★★ ОТКАЗ ⇒ В МИР МЫ НЕ ВОШЛИ, признак одержимости взводить НЕЛЬЗЯ (закон
			//   `no-state-latch-before-shown`). Раньше он ставился ещё на отправке 1016 и после
			//   такого отказа оставался взведён — гейт действий (`World/AsdaPossessGate.h`) резал
			//   бы отправки обычного персонажа. Снимаем безусловно: любой код 0/2/3 = входа нет.
			//   То же для «души» (режим 1): единое поле `SelfSoulState` — в 0.
			SetSelfSoulState(0);
			UE_LOG(LogAsdaNet, Log, TEXT("1027 ОТКАЗ на выбор персонажа: код=%d%s"), (int32)B[0],
				B[0] == 2 ? TEXT(" (соулмейт в игре — вход одержимым запрещён, loc 131)")
				          : (B[0] == 3 ? TEXT(" (нет активной пары соулмейтов)") : TEXT("")));
			OnCharSelectRefused.Broadcast((int32)B[0]);
			break;
		}
		// ★★★ 25.10.2026 [ОРИГИНАЛ] БЛОК ОПЦИЙ ПЕРСОНАЖА — 19 байт с +632 (разбор у `ServerOptionBlock`). Эталон
		//   `netHandleAppearanceChangeResult` (network.c:34302) отдаёт их `optionsApplyServerFlags`; здесь — делегат окну настроек.
		if (B.size() >= 632 + 19)
		{
			ServerOptionBlock.SetNumUninitialized(19);
			FMemory::Memcpy(ServerOptionBlock.GetData(), B.data() + 632, 19);
			FString Hex;
			for (int32 k = 0; k < 19; ++k) { Hex += FString::Printf(TEXT("%02X"), ServerOptionBlock[k]); }
			UE_LOG(LogAsdaNet, Log, TEXT("1027 опции персонажа с сервера (19 б): %s → показ шлема [15]=%u"), *Hex, (uint32)ServerOptionBlock[15]);
			OnServerOptionFlags.Broadcast(ServerOptionBlock);
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("1027: тело %d б < 651 — блока опций персонажа нет"), (int32)B.size());
		}
		auto Rd32 = [&B](size_t o) { return AsdaBytes::RdI32(&B[o]); };
		FString Dump;
		for (int32 S = 0; S < 11; ++S)
		{
			const size_t Off = 5 + 57 * (size_t)S;
			if (Off + 25 > B.size()) { break; }
			const int32 Code  = Rd32(Off);
			const int32 Grade = (int32)B[Off + 0x18];   // уровень ЗАТОЧКИ (инстанс +0x18)
			if (Code > 0) { Dump += FString::Printf(TEXT("[%d]=%d(+%d) "), S, Code, Grade); }
			if (S == 9) { SelfWeaponItemId = Code; SelfWeaponGrade = Grade; }
			if (S == 8) { SelfShieldItemId = Code; SelfShieldGrade = Grade; }
		}
		UE_LOG(LogAsdaNet, Log, TEXT("0x403 экип СВОЕГО персонажа: %s → оружие=%d(+%d), щит=%d(+%d)"),
			*Dump, SelfWeaponItemId, SelfWeaponGrade, SelfShieldItemId, SelfShieldGrade);
		OnSelfEquipGrade.Broadcast();
		// ★ КОНТЕЙНЕР 3 ЦЕЛИКОМ: 11 полных 57-байтовых записей начиная с body+5 (индекс = экип-слот).
		//   Это единственный источник надетого при входе в мир — окно инвентаря берёт его отсюда.
		if (NetNeedLen(B, 5 + 57 * 11, (int32)Pkt.Opcode))
		{
			TArray<uint8> Equip11; Equip11.SetNumUninitialized(57 * 11);
			FMemory::Memcpy(Equip11.GetData(), B.data() + 5, FAsdaItemInstance::kRecordSize * 11);
			OnEquipFull.Broadcast(Equip11);
		}
		break;
	}
	default: bHandled = false; break;
	}
}
