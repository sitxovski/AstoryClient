// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_Items.cpp — ★ 22.10.2026 часть UAsdaNetSubsystem, вынесенная из AsdaNetSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_net_2026-10-22/plan.json).
//   Перемещение/использование предметов, коробки и гача, премиум-молл, парикмахерская, премиум-почта, рыбалка,
//   книга рыбака, самоцветы, магазин NPC, ремонт/улучшение/переплавка/разборка/ремесло, личная лавка, склад, аукцион, рулетка.
//   #include — те же, что у AsdaNetSubsystem.cpp, в том же порядке; общие хелперы — AsdaNetSubsystemInternal.h.
#include "AsdaNetSubsystem.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (4026/4028/5256)
#include "AsdaTimedSend.h"                     // ★ 15.10.2026: поток точной отправки (стендовая постановка 4028)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"   // ★ гейт продолжения боя: обездвиживающие состояния
#include "AsdaConnection.h"
#include "Net/NetOpcodes.h"
#include "Misc/SecureHash.h"
#include "Login/AsdaLoginData.h"
#include "AsdaBytes.h"   // D4b: единые читалки little-endian вместо лямбд/открытых выражений в каждом хендлере
#include "AsdaPacketWriter.h"   // ★ волна 3 п.10: единый сборщик тел исходящих пакетов (вместо uint8 P[N] + WrI16)
#include "Login/AsdaText.h"                 // имена класса/созвездия из loc-таблиц
#include "Items/AsdaItemTableSubsystem.h"   // раскладка 0x3EF по itemType
// 6004 успех → BeginMapLoad (Load Manager)   ★ R3: комментарий снятого включения "World/AsdaLoadManagerSubsystem.h" (файл им не пользовался) — сохранён
// loc-строки форматов чата (20030/1187/1109/8200/…)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// RankColor / SlotColor — таблицы каналов и рангов   ★ R3: комментарий снятого включения "UI/AsdaChatWindowSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaInventorySubsystem.h"        // Дд6: FAsdaItemInstance::kRecordSize (57 б) — длина записи предмета
// 5055 отказ портала: нотисы 1096/1097 в системный чат   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNotice.h"              // ★ 27.09: отказы 5055/6201 — плашка 221 и пара «плашка + журнал» (общие точки)
// ★ 27.09: ColGold() = эталонный −11514 отказов перехода   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
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


// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D2 (склад): слать ли номер страницы 4050 без ограничения.
//   0 = клампим в реальный диапазон вкладок 1…9 (наше исправление: `pageNo == 0` заставляет сервер
//       читать память ДО начала массива склада и слать её клиенту — см. `warehouse_net_server.md`);
//   1 = эталонное поведение без проверки (нужно только для воспроизведения дефекта в отладке).
static ASDA_DEV_CVAR(int32, CVarWarehouseRawPageNo,
	TEXT("asda.Warehouse.RawPageNo"), 0,
	TEXT("1 = слать номер страницы склада (4050) без ограничения диапазона (дефект D2); 0 = кламп 1..9."),
	ECVF_Default);

// ★ N15 (аудит дублей №2, В1, 11.10.2026): та же проверка для отправителей поверх `Build*Body` (тело собрано отдельно —
//   ради самотестов; при ошибке раскладки сборщик возвращает ПУСТОЕ тело). Шесть копий `if (Body.Num() != N) { UE_LOG(…);
//   return false; }` (6500/6502/6602/6302/6309/6313) сведены сюда; текст строки лога — прежний, слово в слово.
static bool AsdaBodySizeBroken(const TArray<uint8>& Body, int32 Need, int32 Opcode)
{
	if (Body.Num() == Need) { return false; }
	UE_LOG(LogAsdaNet, Error, TEXT("→%d: тело %d б вместо %d — не шлём"), Opcode, Body.Num(), Need);
	return true;
}

// ★★★ 5000 0x1388 — ПЕРЕМЕСТИТЬ / НАДЕТЬ / СНЯТЬ (эталон `CharItemMove@0x4CE3E0`; клиентские
//   отправители `invBag_sendSlotUpdatePacket@0xD0E4E0`, `inventory_sendItemEquipRequest@0xD0EBE0`,
//   `itemAction_SendUseEquipPacket@0xC69900`, `net_sendInventoryItemUse@0xD11B20` — ОДИН И ТОТ ЖЕ
//   пакет, различаются только dstType/dstSlot).
//   payload 22 б = ДВА item-ref по 11 б; src-половина собирается ИЗ ПОЛЕЙ ЗАПИСИ e[57]:
//       p[0..1]  = e[5..6]    srcSlot            p[11..12] = dstSlot
//       p[2..3]  = e[7..8]    srcSecondIndex     p[13..14] = 0 (клиент не заполняет)
//       p[4]     = e[4]       srcType            p[15]     = dstType
//       p[5..8]  = e[9..12]   amount (сервер НЕ читает — мёртвое поле протокола, отчёт §8.2)
//       p[9..10] = e[14..15]  вес стека (тоже не читается)
//   Пакет: [0..7] токен + [8..23] MD5(payload,22) + [24..45] payload = 46 б → общий `SendSecured`.
bool UAsdaNetSubsystem::SendItemMove5000(const TArray<uint8>& SrcEntry57, int32 DstSlot, int32 DstType)
{
	if (!IsSendLinkUp()) { return false; }
	if (SrcEntry57.Num() < 16)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("5000 перемещение предмета: запись контейнера короче 16 б (%d)"), SrcEntry57.Num());
		return false;
	}
	FAsdaPacketWriter W(22);
	W.U8(0, SrcEntry57[5]);  W.U8(1, SrcEntry57[6]);
	W.U8(2, SrcEntry57[7]);  W.U8(3, SrcEntry57[8]);
	W.U8(4, SrcEntry57[4]);
	W.U8(5, SrcEntry57[9]);  W.U8(6, SrcEntry57[10]); W.U8(7, SrcEntry57[11]); W.U8(8, SrcEntry57[12]);
	W.U8(9, SrcEntry57[14]); W.U8(10, SrcEntry57[15]);
	const uint16 Ds = (uint16)(int16)DstSlot;
	W.I16(11, (int32)Ds);
	W.U8(15, (uint8)DstType);

	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_MOVE, TEXT("SendItemMove5000"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_MOVE, W.Data(), W.Num());
	const int32 ItemCode = AsdaBytes::RdI32(&SrcEntry57[0]);
	UE_LOG(LogAsdaNet, Log, TEXT("→5000 перемещение: code=%d src(тип=%d слот=%d) → dst(тип=%d слот=%d), 46 б -> %s"),
		ItemCode, (int32)SrcEntry57[4], AsdaBytes::RdI16(&SrcEntry57[5]),
		DstType, DstSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ Частный случай 5000 «надеть предмет из сумки» (эталон `net_sendInventoryItemUse@0xD11B20`):
//   dstType = 3 (контейнер надетого), dstSlot = ItemRecord->itemType (номер слота экипировки).
//   ⚠ Раньше здесь была ОТДЕЛЬНАЯ сборка пакета; теперь это тонкая обёртка над общим отправителем —
//   ровно тот же байтовый результат (p[11..12] = itemType, p[15] = 3).
//   ★ Имя `SendInventoryItemUse5000` историческое (его зовёт хотбар); «код действия 3» из старого
//   комментария — на деле dstType = 3, см. отчёт `inventory_re_B.md` §2.2.
bool UAsdaNetSubsystem::SendInventoryItemUse5000(const TArray<uint8>& Entry57, int32 ItemType)
{
	return SendItemMove5000(Entry57, /*DstSlot*/ ItemType, /*DstType*/ 3);
}

// ★ 5028 (29 б = 24 + 5) — использовать расходник (`CharSpendItemUse@0x4D8C40`).
//   payload: @0 u8 containerType, @1 i16 slotIndex, @3 i16 targetUserIdx.
//   Сервер требует `itemTemplate+26 == 11` (расходник), проверяет группу кулдауна (+59/+60) и
//   отвечает 5029; при кулдауне — result = 3.
bool UAsdaNetSubsystem::SendSpendItemUse5028(int32 ContainerType, int32 SlotIndex, int32 TargetUserIdx)
{
	FAsdaPacketWriter W(5);
	W.U8(0, (uint8)ContainerType);
	const uint16 Sl = (uint16)(int16)SlotIndex;
	W.I16(1, (int32)Sl);
	const uint16 Tg = (uint16)(int16)TargetUserIdx;
	W.I16(3, (int32)Tg);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_SPEND_ITEM_USE, TEXT("SendSpendItemUse5028"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_SPEND_ITEM_USE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5028 использовать расходник: контейнер=%d слот=%d цель=%d (29 б) -> %s"),
		ContainerType, SlotIndex, TargetUserIdx, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 5028 расходник (зелье/свиток): контейнер=%d слот=%d цель userIdx=%d -> %s"),
		ContainerType, SlotIndex, TargetUserIdx, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 06.11.2026 Камень души, банка опыта. Раскладки — в объявлениях.
bool UAsdaNetSubsystem::SendSoulJarRequest5419(const uint8 (&Body11)[11])
{
	const bool bOk = SendSecured((uint16)OP_SOUL_JAR_REQUEST_USER_REQ, Body11, 11);
	UE_LOG(LogAsdaNet, Log, TEXT("→5419 банка опыта: просьба Другу (35 б) -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSoulJarResponse5422(uint8 Mode, int32 RequesterUserNum, const uint8 (&Pos11)[11])
{
	FAsdaPacketWriter W(14);
	W.U8(0, Mode);
	W.I16(1, (int32)(uint16)(int16)RequesterUserNum);
	for (int32 i = 0; i < 11; ++i) { W.U8(3 + i, Pos11[i]); }
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SOUL_JAR_RESPONSE_USER_RES, TEXT("SendSoulJarResponse5422"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SOUL_JAR_RESPONSE_USER_RES, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5422 банка опыта: ответ %d просителю %d (38 б) -> %s"), (int32)Mode, RequesterUserNum, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSoulJarForwardExp5423()
{
	const bool bOk = SendSecured((uint16)OP_SOUL_JAR_FORWARD_EXP_REQ, nullptr, 0);
	UE_LOG(LogAsdaNet, Log, TEXT("→5423 банка опыта: передать опыт Другу (24 б) -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═════════ ★★★ ВОЛНА 2: КОРОБКИ · ГАЧА · ПРЕМИУМ — ОТПРАВИТЕЛИ ══════════════════════════════════
//   Раскладки тел и доказательства — в объявлениях (заголовок). Здесь только сборка байтов.

bool UAsdaNetSubsystem::SendOpenPackageItem6096(int32 SenderCharId, int32 SelfNum, uint8 Count,
                                                uint8 SrcContainer, int32 SrcSlot, const TArray<uint8>& Slots15)
{
	if (Slots15.Num() != 15)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→6096: список слотов-приёмников %d б вместо 15 — не шлём"), Slots15.Num());
		return false;
	}
	FAsdaPacketWriter W(25);
	W.I16(0, (int32)(uint16)(int16)SelfNum);   // @0 свой userNum (сервер не читает)
	W.I32(2, SenderCharId);
	W.U8(6, Count);
	W.U8(7, SrcContainer);
	W.I16(8, (int32)(uint16)(int16)SrcSlot);
	W.Bytes(10, Slots15.GetData(), 15);
	// ⚠ Анти-подмена сервера: `*(i32*)&v19[1] != sub_5324F0(user)` ⇒ ответ кодом 2 (нотис 99312).
	if (SenderCharId != SelectedCharId)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("→6096: charId=%d не равен нашему %d — сервер ответит кодом 2"), SenderCharId, SelectedCharId);
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_OPEN_PACKAGE_ITEM, TEXT("SendOpenPackageItem6096"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_OPEN_PACKAGE_ITEM, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6096 вскрыть коробку: charId=%d коробка(контейнер=%d слот=%d) позиций=%d (49 б) -> %s"),
		SenderCharId, (int32)SrcContainer, SrcSlot, (int32)Count, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGachaItemUse5468(int32 ItemCode, uint8 ContainerType, int32 SlotIndex)
{
	FAsdaPacketWriter W(7);
	W.I32(0, ItemCode);
	W.U8(4, ContainerType);
	W.I16(5, (int32)(uint16)(int16)SlotIndex);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_GACHA_ITEM_USE, TEXT("SendGachaItemUse5468"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_GACHA_ITEM_USE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5468 гача: код=%d контейнер=%d слот=%d (31 б) -> %s"),
		ItemCode, (int32)ContainerType, SlotIndex, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGachaItemEffect5474(uint8 bRare)
{
	FAsdaPacketWriter W(1);
	W.U8(0, bRare);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_GACHA_ITEM_EFFECT, TEXT("SendGachaItemEffect5474"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_GACHA_ITEM_EFFECT, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5474 эффект удачи соседям: редкий=%d (25 б) -> %s"),
		(int32)bRare, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ПРЕМИУМ-МОЛЛ: C→S 5070 «купить» (LEN 195 = 24 конверта + 171 тела) ═══════════════
//   Разбор тела и доказательства паддинга — в шапке `Public/AsdaNetSubsystem.h`.
void UAsdaNetSubsystem::BuildPremiumShopBody(const TArray<int32>& ItemCodes, uint8 Mode, bool bTable1,
	bool bToInventory, uint8 OutBody[171])
{
	FMemory::Memzero(OutBody, 171);
	OutBody[0] = Mode;                       // +0  режим/тип витрины (эталон: 1)
	OutBody[1] = bTable1 ? 1 : 0;            // +1  1 = item_list, 0 = special_itemlist
	OutBody[2] = bToInventory ? 1 : 0;       // +2  1 = в инвентарь, 0 = «в хранилище» (сервер v39=5)
	// +3 + 4·i — семь кодов. ★ Граница РОВНО 7 и БЕЗ пропуска индекса 7 (исправление К1/К2).
	for (int32 i = 0; i < 7; ++i)
	{
		const int32 Code = ItemCodes.IsValidIndex(i) ? ItemCodes[i] : -1;
		AsdaBytes::WrI32(&OutBody[3 + 4 * i], Code);
	}
	// +31 + 2·k — 35 пар (u16,u16) гнёзд/опций; эталонный конструктор кладёт всюду −1.
	for (int32 k = 0; k < 70; ++k)
	{
		AsdaBytes::WrI16(&OutBody[31 + 2 * k], -1);
	}
}

bool UAsdaNetSubsystem::SendPremiumShopBuy5070(const uint8 Body171[171])
{
	// ★ bPad = true — шестой аргумент `a6` эталонного `CNetwork_Send` для этой ветки равен 1.
	const bool bOk = SendSecured((uint16)OP_CHAR_CHARGE_SHOP_BUY, Body171, 171, /*bPad=*/true);
	UE_LOG(LogAsdaNet, Log, TEXT("→5070 премиум-покупка: режим=%d таблица=%d доставка=%s коды %d/%d/%d/%d/%d/%d/%d (195 б) -> %s"),
		(int32)Body171[0], (int32)Body171[1], Body171[2] ? TEXT("инвентарь") : TEXT("хранилище"),
		AsdaBytes::RdI32(&Body171[3]),  AsdaBytes::RdI32(&Body171[7]),  AsdaBytes::RdI32(&Body171[11]),
		AsdaBytes::RdI32(&Body171[15]), AsdaBytes::RdI32(&Body171[19]), AsdaBytes::RdI32(&Body171[23]),
		AsdaBytes::RdI32(&Body171[27]), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ★ 01.11.2026 P101_CashGift: C→S 5072 «проверить имя» (44 = 24 + 20) ═══════════════════════════════
//   Тело — 20 байт имени получателя (cp1251, NUL-добивка); MD5 считается по этим 20 байтам (`Net_Md5Digest16(…, 0x14)`).
//   Паддинг a6 = 1 — те же флаги `CNetwork_Send(…, 44, 5072, 1, 1, 1, 0, 0)`, что у 5070 (network.c:13654, shop_trade.c:1041).
bool UAsdaNetSubsystem::SendCashGiftCheck5072(const uint8 Name20[20])
{
	const bool bOk = SendSecured((uint16)OP_BOOSTER_SHOP_SEND_BUY, Name20, 20, /*bPad=*/true);
	UE_LOG(LogAsdaNet, Log, TEXT("→5072 подарок: проверка имени «%s» (44 б) -> %s"),
		*FAsdaLoginData::DecodeCp1251(Name20, 20), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ★ 01.11.2026 P101_CashGift: C→S 5074 «подарить» (211 = 24 + 187) ═══════════════════════════════════
//   Раскладка тела — `re/client.md` §2.2 (сборщик `UAsdaCashGiftSubsystem::BuildPresentBody`); MD5 — по 187 байтам (0xBB).
bool UAsdaNetSubsystem::SendCashGiftPresent5074(const uint8 Body187[187])
{
	const bool bOk = SendSecured((uint16)OP_BOOSTER_SHOP_SEND_BUY_PRESENT, Body187, 187, /*bPad=*/true);
	UE_LOG(LogAsdaNet, Log,
		TEXT("→5074 подарок: режим=%d таблица=%d коды %d/%d/%d/%d/%d/%d/%d получатель ch=%d pt=%d gs=%d th=%d ui=%d ci=%d un=%d (211 б) -> %s"),
		(int32)Body187[0], (int32)Body187[1],
		AsdaBytes::RdI32(&Body187[8]),  AsdaBytes::RdI32(&Body187[12]), AsdaBytes::RdI32(&Body187[16]),
		AsdaBytes::RdI32(&Body187[20]), AsdaBytes::RdI32(&Body187[24]), AsdaBytes::RdI32(&Body187[28]),
		AsdaBytes::RdI32(&Body187[32]),
		(int32)Body187[176], (int32)Body187[177], (int32)(int8)Body187[178], (int32)(int8)Body187[179],
		AsdaBytes::RdI32(&Body187[180]), (int32)Body187[184], (int32)AsdaBytes::RdI16(&Body187[185]),
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPremiumBalanceQuery5466(int32 CharId)
{
	FAsdaPacketWriter W(4);
	// ⚠ charId (i32), НЕ userNum (i16): сервер сверяет его с sub_5324F0 и при расхождении
	//   отвечает 5467 с кодом 2 — то есть ошибка не «молчаливая», но и баланса не будет.
	W.I32(0, CharId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_MAC_VALUE_INQUIRY, TEXT("SendPremiumBalanceQuery5466"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_MAC_VALUE_INQUIRY, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5466 премиум-баланс: charId=%d (28 б) -> %s"),
		CharId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ПАРИКМАХЕРСКАЯ: C→S 5470 «купить причёску» (28 б = 8 + 16 + 4) ═══════════════════
//   Порт `hairShop_SendApplyRequest@0xC4D130` / `boosterShop_SendOpenRequest@0xCF1890`.
//   Разбор тела — в шапке `Public/AsdaNetSubsystem.h`. Ни одно поле не выдумано: все три берутся
//   из записи `hairtable` (+0 Id, +44 HairStyle, +77 ColorIdx), сервер их сверяет с своей таблицей.
bool UAsdaNetSubsystem::SendHairShopBuy5470(int32 HairId, uint8 HairStyle, uint8 ColorIdx)
{
	FAsdaPacketWriter W(4);
	W.I16(0, (int16)HairId);   // ★ i16, «пусто» у эталонного черновика = −1
	W.U8(2, HairStyle);
	W.U8(3, ColorIdx);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_HAIR_SHOP_BUY, TEXT("SendHairShopBuy5470"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_HAIR_SHOP_BUY, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5470 причёска: id=%d стиль=%d цвет=%d (28 б) -> %s"),
		HairId, (int32)HairStyle, (int32)ColorIdx, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPremiumGachaRoll6577(int32 CharId, uint8 SlotType)
{
	FAsdaPacketWriter W(5);
	W.I32(0, CharId);
	W.U8(4, SlotType);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_PRIMIUM_GA_CHA, TEXT("SendPremiumGachaRoll6577"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_PRIMIUM_GA_CHA, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6577 крутить гачу: charId=%d слот=%d (29 б) -> %s"),
		CharId, (int32)SlotType, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPremiumGachaReward6581(int32 ItemCode, int32 SlotIndex,
                                                   uint8 ContainerType, int32 VariantCode)
{
	FAsdaPacketWriter W(11);
	// @0 — КОД ПРЕДМЕТА приза (сервер сверяет с кодом в слоте; при расхождении 6582 код 10).
	W.I32(0, ItemCode);
	W.I16(4, (int32)(uint16)(int16)SlotIndex);
	W.U8(6, ContainerType);
	W.I32(7, VariantCode);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_PRIMIUM_GA_CHA_REWARD, TEXT("SendPremiumGachaReward6581"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_PRIMIUM_GA_CHA_REWARD, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6581 забрать приз: код=%d слот=%d контейнер=%d вариант=%d (35 б) -> %s"),
		ItemCode, SlotIndex, (int32)ContainerType, VariantCode, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ★★★ ЭЛИТНЫЙ ПОЧТОВЫЙ ЯЩИК: отправители 11004 / 11006 ═══════════
//   Оба — тонкие обёртки над общим `SendSecured` (8 токен + 16 MD5 + payload), потому что
//   эталонные сборщики `netSecure_sendGuardedRequest@0xEF0120` и `net_sendSlotSyncOpcode11006
//   @0xEF0CD0` делают ровно то же самое: та же таблица 30 битовых позиций obfTick, тот же
//   MD5 по payload. Своего сборщика заводить нечего (закон `port-shared-points`).

bool UAsdaNetSubsystem::SendPremiumPostListReq11004()
{
	// Эталон шлёт 28 б, из них payload — 4 НУЛЕВЫХ байта (сервер их не читает вовсе).
	FAsdaPacketWriter W(4);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_PREMIUM_ITEM_LIST_REQ, TEXT("SendPremiumPostListReq11004"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_PREMIUM_ITEM_LIST_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→11004 запрос содержимого элитного ящика (28 б) -> %s"),
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPremiumPostToInven11006(const uint8 Rec10[10])
{
	if (!Rec10) { return false; }
	// ⚠ ДЕФЕКТ ОРИГИНАЛА B (клиентская половина): `Count` (@8) в эталоне уходит ИЗ ПОЛЯ UI, и
	//   сервер его ни с чем не сверяет — модифицированный клиент так дюпает предмет, а при
	//   `Count > 7` пишет за границу кадра `Recv_PremiumItemToInven_Req`. Наша сторона обязана
	//   отправлять только то, что пришло в 11005, и не давать выйти за 7 (см. подсистему —
	//   там же и кламп; здесь это ПОСЛЕДНИЙ рубеж перед сокетом).
	FAsdaPacketWriter W(10);
	W.Bytes(0, Rec10, 10);                      // запись строки 11005 как есть: @0 Order, @4 ItemIndex, @8 Count, @9 BuyType
	if (W[8] > 7)
	{
		UE_LOG(LogAsdaNet, Error,
			TEXT("→11006 ОТКАЗ: Count=%d > 7 — такой пакет ломает кадр серверного обработчика ")
			TEXT("`Recv_PremiumItemToInven_Req@0x4F8340` (массив слотов там ровно 7 слов). Не отправляем."),
			(int32)W[8]);
		return false;
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_PREMIUM_ITEM_TO_INVEN_REQ, TEXT("SendPremiumPostToInven11006"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_PREMIUM_ITEM_TO_INVEN_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→11006 забрать заказ: Order=%d ItemIndex=%d Count=%d BuyType=%d (34 б) -> %s"),
		AsdaBytes::RdI32(W.Data() + 0), AsdaBytes::RdI32(W.Data() + 4), (int32)W[8], (int32)W[9],
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 25.10.2026: ветка частичного сброса навыков для СЛЕДУЮЩЕЙ отправки 5450 (−1 — нет). Взводит окно 16500 перед отправкой,
//   снимает сама отправка (одноразово). Поле файла, а не член подсистемы: заголовок сети «горячий» (167 включений).
static int32 GAsda5450TreeGrade = -1;

void AsdaArm5450TreeGrade(int32 Grade)
{
	GAsda5450TreeGrade = Grade;
}

bool UAsdaNetSubsystem::SendFunctionItemUse5450(int32 TargetUserNum, int32 ItemCode, int32 ItemPos,
                                                int32 PosX, int32 PosZ, uint8 ClickByte, int32 Value,
                                                const TArray<int32>& TargetUserNums, const FString& Message)
{
	// ⚠ ПОЛЕ ПО УМОЛЧАНИЮ = −1, А НЕ 0 (`struct_ResetToDefault@0xA0DD60`): сервер отличает
	//   «поля нет» от «поле = 0» (напр. `ItemPos < 0` ⇒ отказ кодом 2, `PosX < 0` ⇒ код 10).
	FAsdaPacketWriter W(257);
	auto W16 = [&W](int32 Off, int32 V) { W.I16(Off, V); };
	W16(0, TargetUserNum);
	W.I32(2, ItemCode);
	W16(6, ItemPos);
	W16(8, PosX);
	W16(10, PosZ);
	W.U8(12, ClickByte);
	W16(13, Value);
	for (int32 i = 0; i < 20; ++i)
	{
		W16(15 + 2 * i, TargetUserNums.IsValidIndex(i) ? TargetUserNums[i] : -1);
	}
	if (!Message.IsEmpty())
	{
		// Строка уходит в cp1251 — тем же кодировщиком, что и весь текстовый обмен клиента
		//   ([[port-shared-points]]: своей копии не заводим). Поле 200 б, последний байт — ноль.
		W.Cp1251(55, Message, 199);
	}
	// ★★★ 25.10.2026: ВЕТКА ЧАСТИЧНОГО СБРОСА НАВЫКОВ (подтип 36, окно 16500 `UAsdaSkillTreeResetSubsystem`).
	//   [ОРИГИНАЛ] `netSendItemUsePacket@0xA0D470`: `if (subType == 36) *(WORD*)(payload + 255) = word_2C0A81B` — ветка,
	//   выбранная в окне (`uiTradeDialogInitFromItemType@0xD7DAF0` / клики `uiItemUpgradeWnd_OnCommand@0xD79260`).
	//   [ПРОТОКОЛ] Наш (старый) сервер читает из тела только 0x89 = 137 байт (`Char_FunctionItem_Use@0x4ED780`,
	//   `memcpy(v88, тело, 0x89)`), поле значения у него — `*(int16*)&v88[135]` («Value» в FuncLog → DB → `sub_480F20` → 5464):
	//   +255 он не видит, и сброс уходил на ветку 0. Пишем ветку в ОБА места: +135 — то, что читает сервер (у подтипа 36
	//   строки нет, поле строки там нулевое), +255 — раскладка эталона. Ветка задаётся только окном 16500 (см. AsdaArm5450TreeGrade).
	if (GAsda5450TreeGrade >= 0)
	{
		W16(135, GAsda5450TreeGrade);
		W16(255, GAsda5450TreeGrade);
		UE_LOG(LogAsdaNet, Log, TEXT("→5450: ветка частичного сброса навыков %d (тело +135 для сервера, +255 как эталон)"), GAsda5450TreeGrade);
		GAsda5450TreeGrade = -1;   // одноразово: следующая отправка — без ветки
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_FUNCTION_ITEM_USE, TEXT("SendFunctionItemUse5450"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_FUNCTION_ITEM_USE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→5450 премиум-предмет: код=%d слот=%d цель=%d клик=%d значение=%d строка='%s' (281 б) -> %s"),
		ItemCode, ItemPos, TargetUserNum, (int32)ClickByte, Value, *Message, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendFunctionItemToggle5455(int32 ItemCode)
{
	FAsdaPacketWriter W(4);
	W.I32(0, ItemCode);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_POP_FUNCTION_ITEM_STATE_TOGGLE, TEXT("SendFunctionItemToggle5455"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_POP_FUNCTION_ITEM_STATE_TOGGLE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5455 выключить состояние предмета: код=%d (28 б) -> %s"),
		ItemCode, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★★★ 6129 «показать предмет» (хвастовство). Полная раскладка и доказательства — в заголовке.
//   Эталон `netSendTsMaskedReq6129@0xD32880`: payload РОВНО 9 б, MD5 по этим 9 б, длина пакета 33.
//   ★★★ @0 — charId (серверный `iUserIndex`, `*(DWORD*)(CUser+372)`), а НЕ сессионный userNum:
//       сервер сверяет `v6[0] == sub_5324F0(this, arg0)` и при несовпадении выходит МОЛЧА.
//       @4 — наоборот, сессионный userNum адресата. Подробности и ссылки — в заголовке.
bool UAsdaNetSubsystem::SendItemInfoDisplay6129(int32 SenderCharId, int32 TargetUserIdx,
                                                uint8 ContainerType, int32 SlotIndex)
{
	FAsdaPacketWriter W(9);
	const uint32 Snd = (uint32)SenderCharId;
	W.I32(0, (int32)Snd);
	const uint16 Tg = (uint16)(int16)TargetUserIdx;      // −1 = «всем вокруг» (сектор AoI)
	W.I16(4, (int32)Tg);
	W.U8(6, ContainerType);                                 // запись+4
	const uint16 Sl = (uint16)(int16)SlotIndex;           // запись+5
	W.I16(7, (int32)Sl);
	// ⚠ Диагностика звена №1 цепочки: сюда попадает ровно то, что уйдёт в сеть.
	if (SenderCharId != SelectedCharId)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("→6129: отправитель=%d НЕ равен нашему charId=%d — сервер молча отбросит пакет ")
			TEXT("(анти-подмена `v6[0] == sub_5324F0(this, arg0)`)"), SenderCharId, SelectedCharId);
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_ITEM_INFO_DISPLAY, TEXT("SendItemInfoDisplay6129"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_ITEM_INFO_DISPLAY, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6129 хвастовство: charId отправителя=%d (сессионный userNum=%u), цель(userNum)=%d, ")
		TEXT("контейнер=%u слот=%d, payload=%02X %02X %02X %02X %02X %02X %02X %02X %02X (33 б) -> %s"),
		SenderCharId, SelfUserNum, TargetUserIdx, (uint32)ContainerType, SlotIndex,
		W[0], W[1], W[2], W[3], W[4], W[5], W[6], W[7], W[8], bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// Сборка 11-байтового item-ref из 57-байтовой записи (общая половина 5000/5002/5006).
static void AsdaBuildItemRef11(const TArray<uint8>& E, uint8* Out11)
{
	FMemory::Memzero(Out11, 11);
	if (E.Num() < 16) { return; }
	Out11[0] = E[5];  Out11[1] = E[6];      // slotIndex
	Out11[2] = E[7];  Out11[3] = E[8];      // вторичный индекс
	Out11[4] = E[4];                        // containerType
	Out11[5] = E[9];  Out11[6] = E[10]; Out11[7] = E[11]; Out11[8] = E[12];   // amount
	Out11[9] = E[14]; Out11[10] = E[15];    // вес стека
}

// ★ 5006 (35 б = 24 + 11) — уничтожить предмет (`CharItemDump@0x4D3410`). Ответ 5007 (+5003).
bool UAsdaNetSubsystem::SendItemDump5006(const TArray<uint8>& Entry57, int32 OverrideAmount)
{
	if (Entry57.Num() < 16) { return false; }
	FAsdaPacketWriter W(11);
	AsdaBuildItemRef11(Entry57, W.Raw(0, 11));
	// ★ ЧАСТИЧНОЕ УНИЧТОЖЕНИЕ СТЕКА. Эталон (`sowelHandleSlotSelect@0xD14A90`, item_inventory.c:41283)
	//   для стека > 1 сначала открывает окно ввода количества (`g_pNumPadDialog`, режим 0) и шлёт 5006
	//   уже С ВВЕДЁННЫМ числом — количество едет в тех же байтах ссылки (amount, +5..+8), что и при
	//   уничтожении всего стека. OverrideAmount <= 0 ⇒ поле остаётся как в записи = весь стек.
	if (OverrideAmount > 0) { W.I32(5, OverrideAmount); }
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_DUMP, TEXT("SendItemDump5006"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_DUMP, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5006 уничтожить предмет: контейнер=%d слот=%d количество=%d (35 б) -> %s"),
		(int32)W[4], AsdaBytes::RdI16(W.Data() + 0), AsdaBytes::RdI32(W.Data() + 5),
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 5002 (35 б = 24 + 11) — выбросить предмет на землю (`CharItemDrop@0x4D2690`).
//   ⚠ ОТПРАВИТЕЛЯ В КЛИЕНТЕ ОРИГИНАЛА НЕТ (дефект §8.3) — серверный обработчик полный и рабочий.
bool UAsdaNetSubsystem::SendItemDrop5002(const TArray<uint8>& Entry57)
{
	if (Entry57.Num() < 16) { return false; }
	FAsdaPacketWriter W(11);
	AsdaBuildItemRef11(Entry57, W.Raw(0, 11));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_DROP, TEXT("SendItemDrop5002"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_DROP, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5002 выбросить предмет: контейнер=%d слот=%d (35 б) -> %s"),
		(int32)W[4], AsdaBytes::RdI16(W.Data() + 0), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★★★ 6165 (51 б = 8 токен + 16 MD5 + 27 payload) — ПОСТАВИТЬ ЛЕСКУ/КРЮЧОК НА УДОЧКУ.
//   Эталон `itemNet_SendItemActionPacket@0xFC9E20`; порядок полей проверен по серверу
//   `Recv_FishingToolUse@0x40E230` (`sub_523F00(a3, 0x1Bu, ...)`, чтение `(char*)a3 + 33`):
//     @0  i32 userIndex   — `*(DWORD*)(g_LocalPlayer + 1052)`, сервер сверяет с `CUser+372`;
//     @4  u8  actionType  — 0 при `itemSubType == 208` (леска), 1 при `209` (крючок);
//     @5  ref11 УДОЧКА    — сервер: `sub_4D61E0(user, num, body[9] /*type*/, body[5] /*slot*/)`;
//     @16 ref11 МАТЕРИАЛ  — сервер: `sub_4D61E0(user, num, body[20], body[16])`.
bool UAsdaNetSubsystem::SendFishingToolUse6165(int32 InUserIndex, uint8 ActionType,
	const TArray<uint8>& RodEntry57, const TArray<uint8>& MaterialEntry57)
{
	if (RodEntry57.Num() < 16 || MaterialEntry57.Num() < 16)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→6165: неполные записи (удочка %d б, материал %d б) — не шлём"),
			RodEntry57.Num(), MaterialEntry57.Num());
		return false;
	}
	FAsdaPacketWriter W(27);
	const uint32 U = (uint32)InUserIndex;
	W.I32(0, (int32)U);
	W.U8(4, ActionType);
	AsdaBuildItemRef11(RodEntry57, W.Raw(5, 11));
	AsdaBuildItemRef11(MaterialEntry57, W.Raw(16, 11));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_FISHING_TOOL_USE, TEXT("SendFishingToolUse6165"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_FISHING_TOOL_USE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6165 снасть на удочку: userIdx=%d действие=%d (%s) удочка(контейнер=%d слот=%d) материал(контейнер=%d слот=%d) (51 б) -> %s"),
		InUserIndex, (int32)ActionType, ActionType == 0 ? TEXT("леска 208") : TEXT("крючок 209"),
		(int32)W[9],  AsdaBytes::RdI16(W.Data() + 5),
		(int32)W[20], AsdaBytes::RdI16(W.Data() + 16),
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════════════════ РЫБАЛКА: 6167 / 6169 / 6171 (волна В1, 08.10.2026) ════════════════════════
//   Раскладки — client_net.md §1.2-1.4 (клиент) и server.md §2.2-2.4 (сервер читает тело с +33 ровно N байт).
//   Конверт вида A: `SendSecured(op, тело, N)` = 8 б секрета + 16 б MD5(тело) + тело, a6 = 1 (как 6165).
//   Байты тела печатаются в журнал целиком (диагностика провода).
static FString AsdaFishHex(const FAsdaPacketWriter& W)
{
	FString Hex;
	for (int32 i = 0; i < W.Num(); ++i) { Hex += FString::Printf(TEXT("%02X "), W[i]); }
	return Hex.TrimEnd();
}

// ═══════════════════════ ДОСКА ОБЪЯВЛЕНИЙ: 6591 / 6593 (28.10.2026) ═══════════════════════════════════════════════
//   `Docs/Port/systems/BOARD_QUEST_FLOW.md` §5.1/§5.5. Конверт вида A (`SendSecured`, a6 = 1): 8 б секрета + 16 б MD5 + тело.
//   Сервер: `RecvUser_LimitationQuestEnter@0x539D20` (MD5 по 4 б) и `RecvUser_LimitationQuestResult@0x53AC40` (MD5 по 5 б).
// `netSendReqDword_op6591@0xEECEB0` (network.c:32990): `CNetwork_Send(…, 28, 6591, 1,1,1,0,0)` — тело 4 б.
bool UAsdaNetSubsystem::SendBoardEnter6591(int32 QuestId)
{
	FAsdaPacketWriter W(4);
	W.I32(0, QuestId);      // `msg[0] = a2` — номер задания 20000+
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_LIMITATION_QUEST_ENTER, TEXT("SendBoardEnter6591"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_LIMITATION_QUEST_ENTER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6591 доска: взять задание %d тело=[%s] (28 б) -> %s"),
		QuestId, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// `netSendTsMaskedReq6593@0xEED4C0` (network.c:33118): `payload5[0] = questId; LOBYTE(payload5[1]) = выбор`,
//   `CNetwork_Send(…, 29, 6593, 1,1,1,0,0)` — тело 5 б. Выбор 0…3 — индекс награды «на выбор», 99 — «без выбора».
bool UAsdaNetSubsystem::SendBoardResult6593(int32 QuestId, int32 Choice)
{
	FAsdaPacketWriter W(5);
	W.I32(0, QuestId);      // номер задания
	W.U8(4, Choice);        // байт выбора
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_LIMITATION_QUEST_RESULT, TEXT("SendBoardResult6593"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_LIMITATION_QUEST_RESULT, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6593 доска: сдать задание %d, выбор %d тело=[%s] (29 б) -> %s"),
		QuestId, Choice, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// `acSendChallenge6167@0xFCB720` (g:606870): N = 6, итог 30.
bool UAsdaNetSubsystem::SendStartFishing6167(int32 CharId, int32 SpotSlot)
{
	FAsdaPacketWriter W(6);
	W.I32(0, CharId);       // `*(DWORD*)(g_LocalPlayer + 1052)` — charId, НЕ userNum
	W.I16(4, SpotSlot);     // `*(i16*)(зона + 148)` — номер слота спота 0..9
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_START_FISHING, TEXT("SendStartFishing6167"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_START_FISHING, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6167 заброс: charId=%d спот=%d тело=[%s] (30 б) -> %s"),
		CharId, SpotSlot, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// `netSendMaskedPacket_op6169@0xFCC150` (g:607096): N = 8, итог 32. Сервер читает только @6/@7.
bool UAsdaNetSubsystem::SendPhaseResult6169(int32 CharId, int32 Gauge, int32 Total, int32 Correct)
{
	FAsdaPacketWriter W(8);
	W.I32(0, CharId);       // charId
	W.I16(4, Gauge);        // `(i16)(int64)g_InputEditNumBuf.value` — шкала
	W.U8(6, Total);         // всего клавиш в раунде (`keyRecords[0]`)
	W.U8(7, Correct);       // нажато верно (`countEntriesByte16Equal1@0xFC7310`)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_PHASE_RESULT, TEXT("SendPhaseResult6169"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_PHASE_RESULT, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6169 итог раунда: charId=%d шкала=%d всего=%d верно=%d тело=[%s] (32 б) -> %s"),
		CharId, Gauge, Total, Correct, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// `netSendTsMaskedReq6171@0xFCC8D0` (g:607274): N = 7, итог 31. Ранний выход «режим 2 без флага» — у вызывающего
//   (`UAsdaFishingSubsystem::Send6171`), здесь только провод.
bool UAsdaNetSubsystem::SendEndFishing6171(int32 CharId, int32 Mode, int32 Gauge)
{
	FAsdaPacketWriter W(7);
	W.I32(0, CharId);       // charId
	W.U8(4, Mode);          // 1 = вытащить (FSM 117), 2 = отмена (диалог 80 / смерть)
	W.I16(5, Gauge);        // `(i16)g_InputEditNumBuf` — шкала (сервер не читает)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_END_FISHING, TEXT("SendEndFishing6171"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_END_FISHING, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6171 конец рыбалки: charId=%d режим=%d шкала=%d тело=[%s] (31 б) -> %s"),
		CharId, Mode, Gauge, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════════════════ КНИГА РЫБАКА: 6176 / 6178 / 6180 / 6182 (волна В3, 08.10.2026) ═══════════════════
//   Раскладки — client_net.md §1.5-1.8 (клиент), server.md §2.5-2.8 (сервер читает тело с +33 ровно N байт).
//   Конверт вида A: `SendSecured(op, тело, N)`, a6 = 1 (как 6165/6167). Тело печатается в журнал целиком.

// `acSendChallenge6176@0xFD7B20` (g:611578): N = 15, итог 39. Сервер ищет книгу по (контейнер @8, слот @4) — GS:7764.
bool UAsdaNetSubsystem::SendFishBookRegist6176(int32 CharId, const TArray<uint8>& BookEntry57)
{
	FAsdaPacketWriter W(15);
	W.I32(0, CharId);                               // `*(DWORD*)(g_LocalPlayer + 1052)` — charId, НЕ userNum
	AsdaBuildItemRef11(BookEntry57, W.Raw(4, 11));  // rec+5 (i16 слот, i16 второй), rec+4 контейнер, rec+9 кол-во, rec+14 вес
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_FISHING_BOOK_REGIST, TEXT("SendFishBookRegist6176"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_FISHING_BOOK_REGIST, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6176 регистрация книги: charId=%d тело=[%s] (39 б) -> %s"),
		CharId, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// `netSendSecuredOp6178@0xFD7360` (g:611399): N = 8, итог 32. Сервер — `Recv_StartFishing_0@0x4120A0` (это УДАЛЕНИЕ книги).
bool UAsdaNetSubsystem::SendFishBookDelete6178(int32 CharId, int32 PageId)
{
	FAsdaPacketWriter W(8);
	W.I32(0, CharId);       // charId
	W.I32(4, PageId);       // `*(DWORD*)(this[460] + 436·[463] + 4)` — id тома
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_START_FISHING_6178, TEXT("SendFishBookDelete6178"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_START_FISHING_6178, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6178 удалить книгу: charId=%d том=%d тело=[%s] (32 б) -> %s"),
		CharId, PageId, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// `netSendPacket6180@0xFD8800` (g:611836): N = 19, итог 43. Сервер: @4 bookIndex, @8 pos, @12 plate, @13 количество (GS:7958).
bool UAsdaNetSubsystem::SendFishBookUpdate6180(int32 CharId, int32 TomeIndex, const TArray<uint8>& FishEntry57, int32 Qty)
{
	FAsdaPacketWriter W(19);
	W.I32(0, CharId);                               // charId
	W.I32(4, TomeIndex);                            // `this[463]` — ИНДЕКС тома (не id)
	AsdaBuildItemRef11(FishEntry57, W.Raw(8, 11));  // ref11 стопки [466]: @8 слот+второй, @12 контейнер, @13 кол-во, @17 вес
	W.I32(13, Qty);                                 // ★ @13 := `this[467]` — введённое в нумпад количество (НЕ rec+9)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_FISHING_BOOK_UPDATE, TEXT("SendFishBookUpdate6180"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_FISHING_BOOK_UPDATE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6180 сдать рыбу: charId=%d том(индекс)=%d кол-во=%d тело=[%s] (43 б) -> %s"),
		CharId, TomeIndex, Qty, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// `item_sendConfirmWithCostCheck@0xFD94A0` (g:612071): N = 12, итог 36. Сервер сверяет код с 4 наградами тома (GS:8256).
bool UAsdaNetSubsystem::SendFishBookReward6182(int32 CharId, int32 PageId, int32 RewardCode)
{
	FAsdaPacketWriter W(12);
	W.I32(0, CharId);       // charId
	W.I32(4, PageId);       // `*(DWORD*)(this[460] + 436·[463] + 4)` — id тома
	W.I32(8, RewardCode);   // `*(DWORD*)(this[459] + 348·[463] + 4·[470] + 316)` — код выбранной награды
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_FISHING_BOOK_REWARD, TEXT("SendFishBookReward6182"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_FISHING_BOOK_REWARD, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6182 награда книги: charId=%d том=%d награда=%d тело=[%s] (36 б) -> %s"),
		CharId, PageId, RewardCode, *AsdaFishHex(W), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════════════════ САМОЦВЕТЫ: 5024 / 5022 / 5031 ═══════════════════════════════════════
//   Раскладки тел доказаны с ОБЕИХ сторон: клиент `itemSowelSocketRequest@0xD090B0`,
//   `invItemDropSendPacket@0xD06BA0`, `net_sendSecuredRequest_5031@0xD167A0`; сервер
//   `CharCellobItemMove@0x4DC390`, `CharCellobItemDetach@0x4DBC30`, `CharSpendHolyItemUse@0x4DE590`.

bool UAsdaNetSubsystem::SendSowelSocket5024(uint8 TargetContainer, int32 TargetSlot, uint8 SocketIndex,
	int32 OldSowelCode, int32 NewSowelCode, uint8 SrcContainer, int32 SrcSlot)
{
	FAsdaPacketWriter W(11);
	W.U8(0, TargetContainer);
	W.I16(1, TargetSlot);
	W.U8(3, SocketIndex);
	W.I16(4, OldSowelCode);
	W.I16(6, NewSowelCode);
	W.U8(8, SrcContainer);
	W.I16(9, SrcSlot);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_CELLOB_ITEM_MOVE, TEXT("SendSowelSocket5024"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_CELLOB_ITEM_MOVE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→5024 самоцвет в гнездо: предмет(контейнер=%d слот=%d) гнездо=%d старый=%d новый=%d камень(контейнер=%d слот=%d) (35 б) -> %s"),
		(int32)TargetContainer, TargetSlot, (int32)SocketIndex, OldSowelCode, NewSowelCode,
		(int32)SrcContainer, SrcSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════════════════ МАГАЗИН NPC: 5020 «купить» и 5018 «продать» ═══════════════════════════
// ★ 5020 — тело РОВНО 42 б = 7 записей по 6 (`i32 код`, `u16 количество`). Эталон
//   `Send_CharItemBuy_1@0xE2A240` заполняет только первые пять (столько ячеек в корзине окна),
//   остальные остаются нулями после `memset(sellPacket, 0, 0x2A)`. Сервер `CharItemBuy@0x50F170`
//   всё равно проходит семь и требует ровно 42 байта в MD5 — длину менять НЕЛЬЗЯ.
bool UAsdaNetSubsystem::SendCharItemBuy5020(const TArray<int32>& ItemCodes, const TArray<int32>& Quantities)
{
	FAsdaPacketWriter W(42);
	int32 Packed = 0;
	for (int32 i = 0; i < ItemCodes.Num() && i < 7; ++i)
	{
		if (ItemCodes[i] <= 0) { continue; }
		W.I32(6 * Packed + 0, ItemCodes[i]);
		W.I16(6 * Packed + 4, Quantities.IsValidIndex(i) ? Quantities[i] : 0);
		++Packed;
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_BUY, TEXT("SendCharItemBuy5020"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_BUY, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5020 покупка у NPC: позиций=%d (66 б) -> %s"),
		Packed, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 5018 — тело РОВНО 55 б = 5 item-ref по 11. Собирает их вызывающий (подсистема магазина):
//   поля берутся из 57-байтовой записи слота (+5, +7, +4, +9, +14), как в эталоне
//   `Send_CharItemSell@0xE32800`. Короткий массив дополняется нулями.
bool UAsdaNetSubsystem::SendCharItemSell5018(const TArray<uint8>& Refs11x5)
{
	FAsdaPacketWriter W(55);
	const int32 N = FMath::Min(Refs11x5.Num(), 55);
	W.Bytes(0, Refs11x5.GetData(), N);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_SELL, TEXT("SendCharItemSell5018"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_SELL, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5018 продажа NPC: байт тела=%d/55 (79 б) -> %s"),
		N, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════════════ РЕМОНТ СНАРЯЖЕНИЯ: 6571 (см. объявление в .h) ═══════════════════════════
//   Эталон `net_SendSlotBatchReq@0xEDA7A0`: тело РОВНО 123 б — 41 байт контейнеров подряд,
//   затем 41 слово слотов. Длина фиксирована, «хвост» уходит нулями (иначе не сойдётся MD5).
bool UAsdaNetSubsystem::SendRepair6571(const TArray<uint8>& Containers, const TArray<int32>& Slots)
{
	FAsdaPacketWriter W(123);
	// ★★★ ХВОСТ ЗАБИВАЕТСЯ СЛОТАМИ −1, А НЕ НУЛЯМИ.
	//   ДЕФЕКТ (2026-08-26, ремонт отвечал кодом 10 «предмет не найден»): мы обнуляли всё тело,
	//   а сервер (`Recv_UserDuraRegen@0x5383D0`) идёт по ВСЕМ 41 записям с условием
	//   `*(__int16 *)&тело[2*i + 41] >= 0` — то есть слот 0 для него ВАЛИДЕН. На первой же пустой
	//   записи он пытался найти предмет в контейнере 0, слоте 0, не находил и слал отказ.
	//   Его собственный лог это и печатал: «Recv_DBDuraRegen(2) 0=0».
	//   Эталон (`sub_EDB0F0@0xEDB0F0`, вызов через `sub_9364C5` в `net_SendSlotBatchReq@0xEDA7A0`)
	//   инициализирует буфер именно так: контейнер 0, слот −1 для всех 41 записи.
	for (int32 i = 0; i < 41; ++i)
	{
		W.U8(i, 0);
		W.I16(41 + 2 * i, -1);
	}
	const int32 N = FMath::Min3(Containers.Num(), Slots.Num(), 41);
	for (int32 i = 0; i < N; ++i)
	{
		W.U8(i, Containers[i]);
		W.I16(41 + 2 * i, Slots[i]);
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_DURA_REGEN, TEXT("SendRepair6571"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_DURA_REGEN, W.Data(), W.Num());
	// ★ Печатаем САМИ ПАРЫ (контейнер, слот): сервер отвечает кодом 10 ровно тогда, когда не нашёл
	//   предмет по такой паре (`Recv_UserDuraRegen@0x5383D0` → `sub_4D61E0` вернул 0), и без этих
	//   чисел причину не отличить от ошибки формата.
	{
		FString Pairs;
		for (int32 i = 0; i < N; ++i)
		{
			Pairs += FString::Printf(TEXT("%s(к=%d сл=%d)"), i ? TEXT(", ") : TEXT(""),
				(int32)Containers[i], Slots[i]);
		}
		UE_LOG(LogAsdaNet, Log, TEXT("→6571 ремонт: пары %s"), *Pairs);
	}
	UE_LOG(LogAsdaNet, Log, TEXT("→6571 ремонт: записей=%d/41 (147 б) -> %s"),
		N, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════ СБРОС/ВОССТАНОВЛЕНИЕ УЛУЧШЕНИЙ: 6506 (см. объявление в .h) ═══════════════════════════
//   Эталон `netSendPacket6506@0xD30D30`; инициализатор тела — `sub_D313B0@0xD313B0`
//   (thunk `sub_929A4A`, разрешён по `RE/ida/thunk_map.tsv`).
bool UAsdaNetSubsystem::SendItemReset6506(uint8 Mode, int32 TargetSlot, const TArray<int32>& MaterialSlots)
{
	FAsdaPacketWriter W(36);
	// ── `sub_D313B0`: нули, кроме слотов — там −1 (и у цели, и у всех десяти материалов) ──
	W.I16(0, 0);
	W.U8(2, 0);                       // режим
	W.U8(3, 0);                       // контейнер цели
	W.I16(4, -1);    // слот цели
	for (int32 i = 0; i < 10; ++i)
	{
		W.U8(6 + i, 0);
		W.I16(16 + 2 * i, -1);
	}
	// ── Что поверх инициализации пишет сам эталон ──
	W.U8(2, Mode);
	W.U8(3, 1);                       // `HIBYTE(payload[1]) = 1` — контейнер цели ВСЕГДА 1
	W.I16(4, TargetSlot);
	// ★ Контейнеры материалов эталон ставит в 1 ВСЕ ДЕСЯТЬ безусловно, даже когда слот = −1
	//   (`*((_BYTE *)&payload[3] + i) = 1` в цикле из десяти без единого условия).
	//   Сервер это переживает: он идёт по паре (контейнер, слот) и по слоту −1 предмет не находит.
	for (int32 i = 0; i < 10; ++i) { W.U8(6 + i, 1); }
	const int32 N = FMath::Min(MaterialSlots.Num(), 10);
	for (int32 i = 0; i < N; ++i) { W.I16(16 + 2 * i, MaterialSlots[i]); }

	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_SHELL_ITEM_UPGRADE_RESET, TEXT("SendItemReset6506"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_SHELL_ITEM_UPGRADE_RESET, W.Data(), W.Num());
	{
		FString Mats;
		for (int32 i = 0; i < N; ++i)
		{
			Mats += FString::Printf(TEXT("%s%d"), i ? TEXT(",") : TEXT(""), MaterialSlots[i]);
		}
		UE_LOG(LogAsdaNet, Log,
			TEXT("→6506 сброс улучшений: режим=%d слот цели=%d материалы=[%s] (60 б) -> %s"),
			(int32)Mode, TargetSlot, *Mats, bOk ? TEXT("OK") : TEXT("FAIL"));
	}
	return TrackSend(bOk);
}

// ═══════ УЛУЧШЕНИЕ СНАРЯЖЕНИЯ (ЗАТОЧКА): 6504 (см. объявление в .h) ═════════════════════════════
//   Эталон `netSendBoneMaskRequest@0xD2AAB0` (миснейм IDA); инициализатор тела — `sub_9392FB` =
//   `sub_D2B170@0xD2B170` (ui_core.c:48947, разрешён по `RE/ida/thunk_map.tsv`).
bool UAsdaNetSubsystem::SendItemUpgrade6504(uint8 ItemContainer, int32 ItemSlot, uint8 ScrollContainer, int32 ScrollSlot,
	uint8 Helper97Container, int32 Helper97Slot, uint8 Helper98Container, int32 Helper98Slot)
{
	FAsdaPacketWriter W(12);
	// ── `sub_D2B170`: во всех четырёх парах контейнер 0, слот −1 (сентинел «пары нет») ──
	W.U8(0, 0);  W.I16(1, -1);
	W.U8(3, 0);  W.I16(4, -1);
	W.U8(6, 0);  W.I16(7, -1);
	W.U8(9, 0);  W.I16(10, -1);
	// ── Что поверх инициализации пишет сам эталон (network.c:19486-19496) ──
	W.U8(0, ItemContainer);          // `LOBYTE(boneMaskReq[0]) = 1` — вызывающий передаёт 1
	W.I16(1, ItemSlot);              // `*(WORD*)(req+1) = boneSlotWord` (слот цели, запись +5)
	W.U8(3, ScrollContainer);        // `HIBYTE(req[1]) = *(свиток+4)`
	W.I16(4, ScrollSlot);            // `req[2] = boneMaskWord` (слот свитка, запись +5)
	W.U8(6, Helper97Container);      // `LOBYTE(req[3]) = *(помощник97+4)` — только если помощник выбран
	W.I16(7, Helper97Slot);
	W.U8(9, Helper98Container);      // `HIBYTE(req[4]) = *(помощник98+4)`
	W.I16(10, Helper98Slot);

	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_SHELL_ITEM_UPGRADE_6504, TEXT("SendItemUpgrade6504"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_SHELL_ITEM_UPGRADE_6504, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6504 заточка: предмет (к=%d сл=%d) свиток (к=%d сл=%d) помощник97 (к=%d сл=%d) помощник98 (к=%d сл=%d) (36 б) -> %s"),
		(int32)ItemContainer, ItemSlot, (int32)ScrollContainer, ScrollSlot,
		(int32)Helper97Container, Helper97Slot, (int32)Helper98Container, Helper98Slot,
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ВЫСОКАЯ ПЕРЕПЛАВКА: 6500 (см. объявление в .h) ═════════════════════════════════════
//   ⚠ Объявление `AsdaWriteItemRef11` — ниже по файлу (у 5433); здесь нужна его сигнатура заранее.
static void AsdaWriteItemRef11(FAsdaPacketWriter& W, int32 At, const uint8* R);

TArray<uint8> UAsdaNetSubsystem::BuildUpperRefine6500Body(int32 RecipeKey, int32 TargetSlot, uint8 TargetContainer,
	const TArray<uint8>& Mat0, const TArray<uint8>& Mat1, const TArray<uint8>& Mat2)
{
	FAsdaPacketWriter W(40);
	// ── `ItemUpdate_InitState@0xCF5B30`: три ссылки — `helper_initInvalidIds` {−1, −1, 0, −1, −1},
	//    затем +4 = −1, +6 = 0, +0 = −1 ──
	for (int32 I = 0; I < 3; ++I)
	{
		const int32 At = 7 + 11 * I;
		W.I16(At + 0, -1); W.I16(At + 2, -1); W.U8(At + 4, 0); W.I32(At + 5, -1); W.I16(At + 9, -1);
	}
	W.I32(0, -1); W.I16(4, -1); W.U8(6, 0);
	// ── Что поверх пишет сам эталон (item_inventory.c:34925-34934) ──
	W.I32(0, RecipeKey);                    // `craftReq[0] = this[94]` — поле +0 записи рецепта
	W.U8(6, TargetContainer);               // `BYTE2(craftReq[1]) = *(запись + 4)`
	W.I16(4, TargetSlot);                   // `LOWORD(craftReq[1]) = *(WORD*)(запись + 5)`
	const TArray<uint8>* Mats[3] = { &Mat0, &Mat1, &Mat2 };
	for (int32 I = 0; I < 3; ++I)
	{
		if (Mats[I]->Num() >= 57) { AsdaWriteItemRef11(W, 7 + 11 * I, Mats[I]->GetData()); }
	}
	TArray<uint8> Out;
	Out.Append(W.Data(), W.Num());
	return Out;
}

bool UAsdaNetSubsystem::SendUpperRefine6500(int32 RecipeKey, int32 TargetSlot, uint8 TargetContainer,
	const TArray<uint8>& Mat0, const TArray<uint8>& Mat1, const TArray<uint8>& Mat2)
{
	const TArray<uint8> Body = BuildUpperRefine6500Body(RecipeKey, TargetSlot, TargetContainer, Mat0, Mat1, Mat2);
	if (AsdaBodySizeBroken(Body, 40, 6500)) { return false; }   // ★ N15: общая проверка «тело N б вместо M»
	const bool bOk = SendSecured((uint16)OP_RECV_USER_UPPER_REFINE, Body.GetData(), Body.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6500 высокая переплавка: рецепт=%d предмет (к=%d сл=%d) материалы (к=%d сл=%d)/(к=%d сл=%d)/(к=%d сл=%d) (64 б) -> %s"),
		RecipeKey, (int32)TargetContainer, TargetSlot,
		(int32)Body[11], AsdaBytes::RdI16(Body.GetData() + 7), (int32)Body[22], AsdaBytes::RdI16(Body.GetData() + 18),
		(int32)Body[33], AsdaBytes::RdI16(Body.GetData() + 29), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ РАЗБОРКА СНАРЯЖЕНИЯ: 6502 (см. объявление в .h), волна 2-А 06.10.2026 ════════════════════
//   Эталон `netSendByteWordCmd_6502@0xE0E460` (network.c:27741-27743): `sub_92CDDF(cmdPayload)` (обнуление),
//   `*(WORD*)&cmdPayload[1] = *(WORD*)(запись + 5)`, `cmdPayload[0] = *(BYTE*)(запись + 4)`; MD5 по 3 байтам.
TArray<uint8> UAsdaNetSubsystem::BuildDisassemble6502Body(uint8 ItemContainer, int32 ItemSlot)
{
	FAsdaPacketWriter W(3);
	W.U8(0, ItemContainer);   // `cmdPayload[0] = *(BYTE*)(pCmdSrc + 4)` — контейнер
	W.I16(1, ItemSlot);       // `*(WORD*)&cmdPayload[1] = *(WORD*)(pCmdSrc + 5)` — слот
	TArray<uint8> Out;
	if (W.IsOk()) { Out.Append(W.Data(), W.Num()); }
	return Out;
}

bool UAsdaNetSubsystem::SendDisassemble6502(uint8 ItemContainer, int32 ItemSlot)
{
	const TArray<uint8> Body = BuildDisassemble6502Body(ItemContainer, ItemSlot);
	if (AsdaBodySizeBroken(Body, 3, 6502)) { return false; }   // ★ N15: общая проверка «тело N б вместо M»
	const bool bOk = SendSecured((uint16)OP_RECV_USER_DISASSEMBLE, Body.GetData(), Body.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6502 разборка: предмет (к=%d сл=%d) (27 б) -> %s"),
		(int32)ItemContainer, ItemSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ СОЗДАНИЕ СНАРЯЖЕНИЯ (РЕМЕСЛО): 6598 / 6600 / 6602 / 6604 (см. объявления в .h) ════════
//   Все четыре — конверт `SendSecured` (эталонные `CNetwork_Send(…, 1,1,1,0,0)`), транспорт общий.
bool UAsdaNetSubsystem::SendCraftRecipeRegist6598(int32 ScrollSlot, uint8 ScrollContainer)
{
	// Эталон network.c:49650-49652: `sub_91F3DD(payload)` (обнуление), `payload[0] = слот`,
	//   `LOBYTE(payload[1]) = контейнер`; MD5 по 3 байтам (`Net_Md5Digest16(…, 3u)`).
	FAsdaPacketWriter W(3);
	W.I16(0, ScrollSlot);
	W.U8(2, ScrollContainer);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_CRAFT_RECIPE_REGIST, TEXT("SendCraftRecipeRegist6598"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_CRAFT_RECIPE_REGIST, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6598 ремесло «изучить рецепт»: свиток (к=%d сл=%d) (27 б) -> %s"),
		(int32)ScrollContainer, ScrollSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendCraftRecipeDelete6600(int32 RecipeId)
{
	// Эталон network.c:49780-49782: `payload[0] = id рецепта`; MD5 по 2 байтам.
	FAsdaPacketWriter W(2);
	W.I16(0, RecipeId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_CRAFT_RECIPE_DELETE, TEXT("SendCraftRecipeDelete6600"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_CRAFT_RECIPE_DELETE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6600 ремесло «удалить инструкцию»: рецепт %d (26 б) -> %s"),
		RecipeId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

TArray<uint8> UAsdaNetSubsystem::BuildCraftEnter6602Body(int32 RecipeId, const TArray<int32>& MatSlots, const TArray<uint8>& MatContainers)
{
	FAsdaPacketWriter W(26);
	// ── `upgradeSlotsInit@0xFE6540` (item_enhance.c:10319-10329): id −1; 6 слотов −1 / контейнеров 0;
	//    2 слота катализаторов −1 / контейнеров 0 ──
	W.I16(0, -1);
	for (int32 I = 0; I < 6; ++I) { W.I16(2 + 2 * I, -1); W.U8(14 + I, 0); }
	for (int32 J = 0; J < 2; ++J) { W.I16(20 + 2 * J, -1); W.U8(24 + J, 0); }
	// ── Что поверх пишет эталон (network.c:50112-50127): `combinePacket[0] = *a3` (id рецепта), для каждого
	//    материала с кодом > 0 — слот (`combinePacket[i+1]`) и контейнер (`combinePacket[7]` + i) найденного стека ──
	W.I16(0, RecipeId);
	for (int32 I = 0; I < 6; ++I)
	{
		if (MatSlots.IsValidIndex(I))      { W.I16(2 + 2 * I, MatSlots[I]); }
		if (MatContainers.IsValidIndex(I)) { W.U8(14 + I, MatContainers[I]); }
	}
	TArray<uint8> Out;
	Out.Append(W.Data(), W.Num());
	return Out;
}

bool UAsdaNetSubsystem::SendCraftEnter6602(int32 RecipeId, const TArray<int32>& MatSlots, const TArray<uint8>& MatContainers)
{
	const TArray<uint8> Body = BuildCraftEnter6602Body(RecipeId, MatSlots, MatContainers);
	if (AsdaBodySizeBroken(Body, 26, 6602)) { return false; }   // ★ N15: общая проверка «тело N б вместо M»
	const bool bOk = SendSecured((uint16)OP_RECV_USER_CRAFT_ENTER, Body.GetData(), Body.Num());
	FString Mats;
	for (int32 I = 0; I < 6; ++I)
	{
		Mats += FString::Printf(TEXT("%s(к=%d сл=%d)"), I ? TEXT(",") : TEXT(""), (int32)Body[14 + I], AsdaBytes::RdI16(Body.GetData() + 2 + 2 * I));
	}
	UE_LOG(LogAsdaNet, Log, TEXT("→6602 ремесло «изготовить»: рецепт %d материалы [%s] катализаторы −1/0 (50 б) -> %s"),
		RecipeId, *Mats, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendCraftScrollLevelUp6604(int32 ScrollSlot, uint8 ScrollContainer)
{
	// Эталон network.c:49909-49911 — раскладка как у 6598.
	FAsdaPacketWriter W(3);
	W.I16(0, ScrollSlot);
	W.U8(2, ScrollContainer);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_CRAFT_SCROLL_LEVEL_UP, TEXT("SendCraftScrollLevelUp6604"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_CRAFT_SCROLL_LEVEL_UP, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6604 ремесло «повысить уровень»: свиток (к=%d сл=%d) (27 б) -> %s"),
		(int32)ScrollContainer, ScrollSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ЛИЧНАЯ ЛАВКА — ПРОДАВЕЦ: 6300 / 6302 / 6305 (волна Л1 торговли; объявления и раскладки — в .h) ═══════════
bool UAsdaNetSubsystem::SendSelfStoreReady6300(int32 SelfCharId, bool bPremium, const FString& SelfName, const TArray<uint8>& Ticket45)
{
	// [ОРИГИНАЛ] `netSendPlayerInfo6300@0xDD45C0` (network.c:26756-26765): `sub_929068` = `sub_DD4D40` (@0 = −1, @4 = 0,
	//   @5..24 = 0, запись @25 — `slotStructReset`), затем @0 charId, @5 `sprintf("%s", имя)`, @4 премиум, @25 копия 45 б билета.
	if (Ticket45.Num() != 45)
	{
		UE_LOG(LogAsdaNet, Error, TEXT("→6300: запись билета %d б вместо 45 — не шлём"), Ticket45.Num());
		return false;
	}
	FAsdaPacketWriter W(70);
	W.I32(0, SelfCharId);
	W.U8(4, bPremium ? 1 : 0);
	W.Cp1251(5, SelfName, 19);             // char[20]: 19 байт + ноль заполнения (`sprintf` пишет строку с терминатором)
	W.Bytes(25, Ticket45.GetData(), 45);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_SELF_STORE_READY, TEXT("SendSelfStoreReady6300"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_SELF_STORE_READY, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6300 лавка «подготовка»: charId=%d премиум=%d имя «%s» билет код=%d к=%d сл=%d (94 б) -> %s"),
		SelfCharId, bPremium ? 1 : 0, *SelfName, AsdaBytes::RdI32(Ticket45.GetData()), (int32)Ticket45[4],
		AsdaBytes::RdI16(Ticket45.GetData() + 5), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

TArray<uint8> UAsdaNetSubsystem::BuildSelfStoreOpen6302Body(int32 SelfCharId, const TArray<int32>& ItemIds, const TArray<uint8>& Containers,
	const TArray<int32>& Slots, const TArray<int32>& Qtys, const TArray<int32>& Prices, const FString& Title)
{
	// [ОРИГИНАЛ] `storeOpenSendRequest@0xDD3AA0` (ui_core.c:65091-65291): `slotArrayReset_4550` — @0 = −1, @4 = 0,
	//   10 лотов через `slotStructReset` (@0 0, @4 0, @5 i16 −1, @7 0, @11 i16 −1, @13 0, @17 4×i16 −1, @25 5 пар {i16 −1, i16 0}),
	//   @455 50 нулей; поверх — @0 charId, @4 число, лоты n = 0..count−1, @455 `sprintf(…, название)`.
	//   ★ Ревизия Л2 (МЕЛК-1): @25 — НЕ нули. `slotStructReset@0xDD39B0` зовёт `sub_926E4E → 0x974480` для 5 элементов по 4 б с +25,
	//   байты `66 c7 00 ff ff … 66 c7 40 02 00 00` дают каждому {i16 −1, i16 0}. Сервер эти поля не читает — это точность байтов.
	FAsdaPacketWriter W(505);
	W.I32(0, -1);
	for (int32 n = 0; n < 10; ++n)
	{
		const int32 L = 5 + 45 * n;
		W.I16(L + 5, -1);
		W.I16(L + 11, -1);
		for (int32 k = 0; k < 4; ++k) { W.I16(L + 17 + 2 * k, -1); }
		for (int32 k = 0; k < 5; ++k) { W.I16(L + 25 + 4 * k, -1); }   // {−1, 0}: вторая половина пары — ноль конструктора
	}
	const int32 Count = FMath::Clamp(ItemIds.Num(), 0, 10);
	W.I32(0, SelfCharId);
	W.U8(4, Count);
	for (int32 n = 0; n < Count; ++n)
	{
		const int32 L = 5 + 45 * n;
		W.I32(L + 0,  ItemIds[n]);
		W.U8 (L + 4,  Containers.IsValidIndex(n) ? Containers[n] : 0);
		W.I16(L + 5,  Slots.IsValidIndex(n) ? Slots[n] : -1);
		W.I32(L + 7,  Qtys.IsValidIndex(n) ? Qtys[n] : 0);
		W.I16(L + 11, n);                                                       // индекс лота = номер записи списка
		const int32 Price = Prices.IsValidIndex(n) ? Prices[n] : 0;
		W.I32(L + 13, Price <= 0 ? 1 : Price);                                  // `if (itemDurability <= 0) = 1` (МИСНЕЙМ: это цена)
	}
	W.Cp1251(455, Title, 50);
	TArray<uint8> Out;
	if (!W.IsOk()) { return Out; }
	Out.Append(W.Data(), W.Num());
	return Out;
}

bool UAsdaNetSubsystem::SendSelfStoreOpen6302(int32 SelfCharId, const TArray<int32>& ItemIds, const TArray<uint8>& Containers,
	const TArray<int32>& Slots, const TArray<int32>& Qtys, const TArray<int32>& Prices, const FString& Title)
{
	const TArray<uint8> Body = BuildSelfStoreOpen6302Body(SelfCharId, ItemIds, Containers, Slots, Qtys, Prices, Title);
	if (AsdaBodySizeBroken(Body, 505, 6302)) { return false; }   // ★ N15: общая проверка «тело N б вместо M»
	const bool bOk = SendSecured((uint16)OP_CHAR_SELF_STORE_OPEN, Body.GetData(), Body.Num());
	FString Lots;
	for (int32 n = 0; n < (int32)Body[4] && n < 10; ++n)
	{
		const uint8* L = Body.GetData() + 5 + 45 * n;
		Lots += FString::Printf(TEXT("%s[%d: код %d к=%d сл=%d кол=%d цена=%d]"), n ? TEXT(" ") : TEXT(""), AsdaBytes::RdI16(L + 11),
			AsdaBytes::RdI32(L), (int32)L[4], AsdaBytes::RdI16(L + 5), AsdaBytes::RdI32(L + 7), AsdaBytes::RdI32(L + 13));
	}
	UE_LOG(LogAsdaNet, Log, TEXT("→6302 лавка «открыть»: charId=%d лотов=%d %s название «%s» (529 б) -> %s"),
		SelfCharId, (int32)Body[4], *Lots, *Title, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSelfStoreClose6305(int32 HostCharId, int32 HostUserNum)
{
	// [ОРИГИНАЛ] `antiCheat_SendPositionReport@0xDD4DA0` (security.c:1958-1983): `sub_934B48` (обнуление 6 б), @0 charId
	//   хозяина, @4 i16 userNum хозяина; MD5 по 6 байтам.
	FAsdaPacketWriter W(6);
	W.I32(0, HostCharId);
	W.I16(4, HostUserNum);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_SELF_STORE_CLOSE, TEXT("SendSelfStoreClose6305"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_SELF_STORE_CLOSE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6305 лавка «закрыть»: хозяин charId=%d userNum=%d (30 б) -> %s"),
		HostCharId, HostUserNum, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ЛИЧНАЯ ЛАВКА — ПОСЕТИТЕЛЬ: 6307 / 6309 (волна Л2 торговли; объявления и раскладки — в .h) ═══════════
bool UAsdaNetSubsystem::SendSelfStoreInfo6307(int32 SelfCharId, int32 HostCharId, int32 HostUserNum)
{
	// [ОРИГИНАЛ] `netSendTradeTargetRequest6307` (network.c:26875-26960): `sub_91EBA9(payload)` — обнуление 10 б; @4 charId
	//   хозяина (`peerChar->userIdx`), @8 i16 userNum хозяина (`Target_GetSelectedUserIdx(peer)` — МИСНЕЙМ), @0 свой charId;
	//   MD5 по 10 байтам. Сервер (`CharSelfStoreInfo` S:100575) сверяет @4 с charId, найденным по userNum из @8.
	FAsdaPacketWriter W(10);
	W.I32(0, SelfCharId);
	W.I32(4, HostCharId);
	W.I16(8, HostUserNum);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_SELF_STORE_INFO, TEXT("SendSelfStoreInfo6307"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_SELF_STORE_INFO, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6307 лавка «войти»: свой charId=%d хозяин charId=%d userNum=%d (34 б) -> %s"),
		SelfCharId, HostCharId, HostUserNum, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

TArray<uint8> UAsdaNetSubsystem::BuildSelfStoreBuy6309Body(int32 SelfCharId, int32 HostCharId, int32 HostUserNum, const TArray<int32>& ItemIds,
	const TArray<int32>& Qtys, const TArray<int32>& LotIdx, const TArray<int32>& Prices)
{
	// [ОРИГИНАЛ] `shopDlg_SendBuyReq@0xDDB5F0` (shop_trade.c:17963-18145): конструктор тела `sub_DDBED0` — 6 позиций
	//   `slotStructReset` (@0 0, @4 0, @5 i16 −1, @7 0, @11 i16 −1, @13 0, @17 4×i16 −1, @25 5 пар {i16 −1, i16 0}), @0 = −1,
	//   @4 = −1, @8 i16 −1, @10 0; поверх — @0 свой charId, @4 charId хозяина (`pNpcChar`), @8 userNum (`selItemType`), @10 число
	//   позиций корзины; позиция n: @0 код, @7 кол-во, @11 индекс, @13 цена. ★ Пустые позиции — код 0 (не −1): сервер (сток и
	//   P44 СЛ-1) пропускает запись с кодом ≤ 0. ★ Ревизия Л2 (МЕЛК-1): @25 — 5 пар {−1, 0} (`sub_926E4E → 0x974480`), как в 6302;
	//   сервер из записи 6309 читает только +0/+7/+11 (S:100843-100858) — это точность байтов [ПРОТОКОЛ].
	FAsdaPacketWriter W(281);
	W.I32(0, -1);
	W.I32(4, -1);
	W.I16(8, -1);
	for (int32 n = 0; n < 6; ++n)
	{
		const int32 L = 11 + 45 * n;
		W.I16(L + 5, -1);
		W.I16(L + 11, -1);
		for (int32 k = 0; k < 4; ++k) { W.I16(L + 17 + 2 * k, -1); }
		for (int32 k = 0; k < 5; ++k) { W.I16(L + 25 + 4 * k, -1); }   // {−1, 0}: вторая половина пары — ноль конструктора
	}
	const int32 Count = FMath::Clamp(ItemIds.Num(), 0, 6);
	W.I32(0, SelfCharId);
	W.I32(4, HostCharId);
	W.I16(8, HostUserNum);
	W.U8(10, Count);
	for (int32 n = 0; n < Count; ++n)
	{
		const int32 L = 11 + 45 * n;
		W.I32(L + 0,  ItemIds[n]);
		W.I32(L + 7,  Qtys.IsValidIndex(n) ? Qtys[n] : 0);
		W.I16(L + 11, LotIdx.IsValidIndex(n) ? LotIdx[n] : -1);
		W.I32(L + 13, Prices.IsValidIndex(n) ? Prices[n] : 0);
	}
	TArray<uint8> Out;
	if (!W.IsOk()) { return Out; }
	Out.Append(W.Data(), W.Num());
	return Out;
}

bool UAsdaNetSubsystem::SendSelfStoreBuy6309(int32 SelfCharId, int32 HostCharId, int32 HostUserNum, const TArray<int32>& ItemIds,
	const TArray<int32>& Qtys, const TArray<int32>& LotIdx, const TArray<int32>& Prices)
{
	const TArray<uint8> Body = BuildSelfStoreBuy6309Body(SelfCharId, HostCharId, HostUserNum, ItemIds, Qtys, LotIdx, Prices);
	if (AsdaBodySizeBroken(Body, 281, 6309)) { return false; }   // ★ N15: общая проверка «тело N б вместо M»
	const bool bOk = SendSecured((uint16)OP_CHAR_SELF_STORE_BUY_ITEM, Body.GetData(), Body.Num());
	FString Lots;
	for (int32 n = 0; n < (int32)Body[10] && n < 6; ++n)
	{
		const uint8* L = Body.GetData() + 11 + 45 * n;
		Lots += FString::Printf(TEXT("%s[%d: код %d кол=%d цена=%d]"), n ? TEXT(" ") : TEXT(""), AsdaBytes::RdI16(L + 11),
			AsdaBytes::RdI32(L), AsdaBytes::RdI32(L + 7), AsdaBytes::RdI32(L + 13));
	}
	UE_LOG(LogAsdaNet, Log, TEXT("→6309 лавка «купить»: свой charId=%d хозяин %d/%d позиций=%d %s (305 б) -> %s"),
		SelfCharId, HostCharId, HostUserNum, (int32)Body[10], *Lots, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ЛИЧНАЯ ЛАВКА — ЧАТ: 6313 (волна Л4 торговли; объявление и раскладка — в .h) ═══════════
TArray<uint8> UAsdaNetSubsystem::BuildSelfStoreChat6313Body(int32 HostCharId, int32 HostUserNum, const FString& SenderName, const FString& Text)
{
	// [ОРИГИНАЛ] `netSendTwoStringRequest6313@0xDDD150` (network.c:27040-27045): `sub_9394B8(pktBody)` (обнуление), @0 = `pThis[157]`
	//   (`CShopDlg+0x274` charId хозяина), @4 i16 = `+0x278` (userNum хозяина), @6 `sprintf("%s", имя)`, @26 `sprintf("%s", текст)`.
	//   [ИСПРАВЛЕНИЕ] К-0 [ПРОТОКОЛ]: тело 106 б (как читает сервер, S:101545), а не 226 — имя ≤ 19 байт, текст ≤ 79 байт + ноль.
	FAsdaPacketWriter W(106);
	W.I32(0, HostCharId);
	W.I16(4, HostUserNum);
	W.Cp1251(6, SenderName, 19);
	W.Cp1251(26, Text, 79);
	return TArray<uint8>(W.Data(), W.Num());
}

bool UAsdaNetSubsystem::SendSelfStoreChat6313(int32 HostCharId, int32 HostUserNum, const FString& SenderName, const FString& Text)
{
	const TArray<uint8> Body = BuildSelfStoreChat6313Body(HostCharId, HostUserNum, SenderName, Text);
	if (AsdaBodySizeBroken(Body, 106, 6313)) { return false; }   // ★ N15: общая проверка «тело N б вместо M»
	const bool bOk = SendSecured((uint16)OP_RECV_SELF_STORE_CHAT_CLIENT, Body.GetData(), Body.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6313 лавка «чат»: хозяин %d/%d имя «%s» текст «%s» (130 б; оригинал 250 — К-0) -> %s"),
		HostCharId, HostUserNum, *SenderName, *Text, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ИЗМЕНЕНИЕ ПАРАМЕТРОВ ПРЕДМЕТА: 6553 (см. объявление в .h) ════════════════════════
//   Эталон `itemSendUpgradeRequest@0xB611E0`: тело 16 б = две 8-байтовые ссылки на предмет,
//   ПЕРВАЯ — свиток (`this+372`, `sumInventoryItemsType67`), ВТОРАЯ — снаряжение (`this+368`).
bool UAsdaNetSubsystem::SendItemOptionTrans6553(int32 ScrollItemCode, int32 ScrollContainer, int32 ScrollSlot,
                                                int32 GearItemCode,   int32 GearContainer,   int32 GearSlot)
{
	FAsdaPacketWriter W(16);            // эталон: `sub_92C772(itemPair)` обнуляет пару целиком
	W.I32(0, ScrollItemCode);
	W.I16(4, ScrollSlot);
	W.I16(6, ScrollContainer & 0xFF);   // HIWORD(pair[1]) = *(u8*)(rec+4)
	W.I32(8, GearItemCode);
	W.I16(12, GearSlot);
	W.I16(14, GearContainer & 0xFF);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_ITEM_OPTION_TRANS, TEXT("SendItemOptionTrans6553"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_ITEM_OPTION_TRANS, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6553 смена параметров: свиток код=%d (к=%d сл=%d), снаряжение код=%d (к=%d сл=%d) (40 б) -> %s"),
		ScrollItemCode, ScrollContainer, ScrollSlot, GearItemCode, GearContainer, GearSlot,
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ПЕРЕНОС УЛУЧШЕНИЯ КАМНЕМ: 6551 (см. объявление в .h) ════════════════════════════════
//   Эталон `netSendAntiCheatChallengeResponse@0xB65D30` (имя врёт, network.c:8815-8955): `equipRecords[0..1]` — камень
//   (`this+432`), `[2..3]` — снаряжение (`this+428`); LOWORD = слот (+5), HIWORD = контейнер (+4). 31.10.2026, баг 4.
bool UAsdaNetSubsystem::SendUpgradeBless6551(int32 StoneItemCode, int32 StoneContainer, int32 StoneSlot,
                                             int32 GearItemCode,  int32 GearContainer,  int32 GearSlot)
{
	FAsdaPacketWriter W(16);            // эталон: `sub_91F5AE(equipRecords)` обнуляет пару целиком
	W.I32(0, StoneItemCode);
	W.I16(4, StoneSlot);
	W.I16(6, StoneContainer & 0xFF);
	W.I32(8, GearItemCode);
	W.I16(12, GearSlot);
	W.I16(14, GearContainer & 0xFF);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_UPGRADE_BLESS, TEXT("SendUpgradeBless6551"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_UPGRADE_BLESS, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6551 перенос улучшения: камень код=%d (к=%d сл=%d), снаряжение код=%d (к=%d сл=%d) (40 б) -> %s"),
		StoneItemCode, StoneContainer, StoneSlot, GearItemCode, GearContainer, GearSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ РАЗДЕЛЕНИЕ УЛУЧШЕНИЯ: 6549 (см. объявление в .h) ═══════════════════════════════════
//   Эталон `antiCheat_sendEquipMaskDigest@0xB664A0` (security.c:651-703): `sub_B66B90` заполняет всё −1, затем камни
//   `this+95…102` (код, `LOWORD` слота +5, `u8` контейнера +4) и предмет `this+94`. 31.10.2026, окно 50500.
bool UAsdaNetSubsystem::SendUpgradeExtraction6549(const TArray<TArray<uint8>>& Stones, int32 ItemCode, int32 ItemContainer, int32 ItemSlot)
{
	FAsdaPacketWriter W(72);
	for (int32 i = 0; i < 8; ++i)
	{
		W.I32(4 * i, -1);
		W.I16(32 + 2 * i, -1);
		W.I16(48 + 2 * i, -1);
	}
	FString Log;
	for (int32 i = 0; i < 8 && i < Stones.Num(); ++i)
	{
		const TArray<uint8>& R = Stones[i];
		if (R.Num() < 7) { continue; }
		W.I32(4 * i, AsdaBytes::RdI32(R.GetData()));
		W.I16(32 + 2 * i, AsdaBytes::RdI16(R.GetData() + 5));
		W.I16(48 + 2 * i, (int32)R[4]);
		Log += FString::Printf(TEXT(" %d(к%d сл%d)"), AsdaBytes::RdI32(R.GetData()), (int32)R[4], AsdaBytes::RdI16(R.GetData() + 5));
	}
	W.I32(64, ItemCode);
	W.I16(68, ItemSlot);
	W.I16(70, ItemContainer & 0xFF);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_UPGRADE_EXTRACTION, TEXT("SendUpgradeExtraction6549"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_UPGRADE_EXTRACTION, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6549 разделение улучшения: предмет код=%d (к=%d сл=%d), камни:%s (96 б) -> %s"),
		ItemCode, ItemContainer, ItemSlot, *Log, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════ ПЕРЕДАЧА ПАРАМЕТРОВ: 5433 (см. объявление в .h) ══════════════════════════════════
//   Эталон `itemExtract_sendCombineRequest@0xC921D0`: тело 25 б = две 11-байтовые ссылки на
//   предмет (источник, приёмник) + слот/контейнер свитка-бустера.
//   ⚠ ХВОСТ ССЫЛОК ЭТАЛОН ИНИЦИАЛИЗИРУЕТ ЕДИНИЦАМИ (`helper_initInvalidIds` = 0xFF), а не нулями —
//   закон «смотри, чем эталон инициализирует хвост»: поля свитка при выключенном бустере обязаны
//   уйти как −1, иначе сервер (`sub_4D61E0(this, arg0, 0, 0, …)`) полезет в слот 0 контейнера 0.
// ★ 11-БАЙТОВАЯ ССЫЛКА НА ПРЕДМЕТ (эталон `itemTableFindRecordById@0x999C80`, item_inventory.c:75-118):
//   +0 i16 слот (запись +5) · +2 i16 запись +7 · +4 u8 контейнер (запись +4) · +5 i32 количество (запись +9) ·
//   +9 i16 запись +14. Одна точка на все отправители со ссылками (5433 «передача параметров», 6500 «высокая
//   переплавка») — вынесена 25.09.2026 из лямбды 5433 без изменения байтов (закон `port-shared-points`).
static void AsdaWriteItemRef11(FAsdaPacketWriter& W, int32 At, const uint8* R)
{
	W.I16(At + 0, AsdaBytes::RdI16(R + 5));
	W.I16(At + 2, AsdaBytes::RdI16(R + 7));
	W.U8 (At + 4, R[4]);
	W.I32(At + 5, AsdaBytes::RdI32(R + 9));
	W.I16(At + 9, AsdaBytes::RdI16(R + 14));
}

bool UAsdaNetSubsystem::SendItemOptionExtraction5433(const TArray<uint8>& SrcRecord57,
                                                     const TArray<uint8>& DstRecord57,
                                                     const TArray<uint8>& ScrollRecord57)
{
	static constexpr int32 kRec = 57;
	if (SrcRecord57.Num() < kRec || DstRecord57.Num() < kRec)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("→5433: записи предметов %d/%d б вместо 57 — не шлём"),
			SrcRecord57.Num(), DstRecord57.Num());
		return false;
	}
	FAsdaPacketWriter W(25, 0xFF);   // эталон: обе ссылки и поля свитка начинаются с −1
	// Одна 11-байтовая ссылка по смещению At — общая точка `AsdaWriteItemRef11` (её же зовёт 6500).
	auto PutRef = [&W](int32 At, const uint8* R) { AsdaWriteItemRef11(W, At, R); };
	PutRef(0,  SrcRecord57.GetData());
	PutRef(11, DstRecord57.GetData());
	if (ScrollRecord57.Num() >= kRec)
	{
		W.I16(22, AsdaBytes::RdI16(ScrollRecord57.GetData() + 5));
		W.U8(24, ScrollRecord57[4]);
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_SHELL_ITEM_OPTION_EXTRACTION, TEXT("SendItemOptionExtraction5433"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_SHELL_ITEM_OPTION_EXTRACTION, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→5433 передача параметров: источник(к=%d сл=%d) → приёмник(к=%d сл=%d), свиток(к=%d сл=%d) (49 б) -> %s"),
		(int32)W[4], AsdaBytes::RdI16(W.Data() + 0), (int32)W[15], AsdaBytes::RdI16(W.Data() + 11),
		(int32)W[24], AsdaBytes::RdI16(W.Data() + 22), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════════════ ВОЕННЫЙ МАГАЗИН: 6162 (см. объявление в .h) ══════════════════════════════
//   Эталон `net_sendStateSyncOpcode6162@0xB74ED0`: тело 12 б, инициализировано −1 (`sub_936F9C`),
//   поэтому поля, которые эталон не заполняет, у нас тоже остаются −1, а не нулями.
bool UAsdaNetSubsystem::SendWarShopBuy6162(int32 StorePos, int32 ItemCode)
{
	FAsdaPacketWriter W(12, 0xFF);                  // `sub_B75530`: −1 во все поля
	W.I16(0, (int32)SelfUserNum);
	W.I32(2, SelectedCharId);
	W.I16(6, StorePos);
	W.I32(8, ItemCode);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ROSE_WAR_STORE_BUY, TEXT("SendWarShopBuy6162"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ROSE_WAR_STORE_BUY, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6162 военный магазин: userNum=%d charId=%d строка=%d предмет=%d (36 б) -> %s"),
		(int32)SelfUserNum, SelectedCharId, StorePos, ItemCode, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════════════ ВОЛНА 2-Б: 6150 доклад об охоте / 6132 пожертвование (см. объявления в .h) ═══════════════════
//   Оба тела эталон заполняет ЦЕЛИКОМ (каждый байт пишется), поэтому заполнитель писателя роли не играет.
bool UAsdaNetSubsystem::SendTitleItemCheck6150(uint8 Container, int32 Slot, int32 Quantity)
{
	FAsdaPacketWriter W(11);
	W.I32(0, SelectedCharId);   // `CharManager_GetLocalPlayer()->userIdx` = поле +0x41C (asm 0xED01A2) — это charId
	W.I16(4, Slot);             // `*(WORD*)(запись + 5)`
	W.U8(6, Container);         // `*(BYTE*)(запись + 4)`
	W.I32(7, Quantity);         // `self[86]` = количество из нумпада режима 9
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_TITLE_ITEM_CHECK, TEXT("SendTitleItemCheck6150"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_TITLE_ITEM_CHECK, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6150 доклад об охоте: charId=%d контейнер=%d слот=%d кол-во=%d (35 б) -> %s"),
		SelectedCharId, (int32)Container, Slot, Quantity, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendUserEventItemStack6132(int32 ItemCode, uint8 Container, int32 Slot, int32 Quantity)
{
	FAsdaPacketWriter W(13);
	W.I32(0, SelectedCharId);   // `*(g_LocalPlayer + 1052)` = charId
	W.I32(4, ItemCode);         // `*(DWORD*)запись`
	W.U8(8, Container);         // `*(BYTE*)(запись + 4)`
	W.I16(9, Slot);             // `*(WORD*)(запись + 5)`
	W.I16(11, Quantity);        // `*(WORD*)(&v12[2] + 3) = a2` — i16 у эталона и у сервера (`*(__int16*)&v7[11]`)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_EVENT_ITEM_STACK_REQ, TEXT("SendUserEventItemStack6132"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_EVENT_ITEM_STACK_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6132 пожертвование: charId=%d предмет=%d контейнер=%d слот=%d кол-во=%d (37 б) -> %s"),
		SelectedCharId, ItemCode, (int32)Container, Slot, Quantity, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSowelDetach5022(uint8 TargetContainer, int32 TargetSlot, uint8 SocketIndex)
{
	FAsdaPacketWriter W(4);
	W.U8(0, TargetContainer);
	W.I16(1, TargetSlot);
	W.U8(3, SocketIndex);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_CELLOB_ITEM_DETACH, TEXT("SendSowelDetach5022"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_CELLOB_ITEM_DETACH, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5022 вынуть самоцвет: предмет(контейнер=%d слот=%d) гнездо=%d (28 б) -> %s"),
		(int32)TargetContainer, TargetSlot, (int32)SocketIndex, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendHolyItemUse5031(uint8 HolyContainer, int32 HolySlot, uint8 SowelContainer, int32 SowelSlot)
{
	FAsdaPacketWriter W(6);
	W.U8(0, HolyContainer);
	W.I16(1, HolySlot);
	W.U8(3, SowelContainer);
	W.I16(4, SowelSlot);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_SPEND_HOLY_ITEM_USE, TEXT("SendHolyItemUse5031"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_SPEND_HOLY_ITEM_USE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5031 святость на камень: святой(контейнер=%d слот=%d) камень(контейнер=%d слот=%d) (30 б) -> %s"),
		(int32)HolyContainer, HolySlot, (int32)SowelContainer, SowelSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 4050 (29 б = 24 + 5) — запрос страницы склада; номер страницы (1-based) в payload @4.
// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D1 (склад): чем индексировать поле `+2` записи 5008/5010.
//   0 = порядковым номером записи, как остальные поля (наше исправление);
//   1 = эталон: номером ячейки корзины (`11 * slot`) — при дырках в корзине значение уезжает
//       в чужую запись (`network.c:31266`). Последствий нет — сервер поле не читает.
static ASDA_DEV_CVAR(int32, CVarWarehouseLegacyField2,
	TEXT("asda.Warehouse.LegacyField2"), 0,
	TEXT("1 = эталонная индексация поля +2 в пакетах склада 5008/5010 (дефект D1); 0 = исправленная."),
	ECVF_Default);

// Общая сборка тела 55 б для 5008/5010: 5 записей по 11 байт.
//   Раскладка записи и правило хвоста — в объявлении (`AsdaNetSubsystem.h`).
//   `SlotOfEntry` нужен ТОЛЬКО для воспроизведения дефекта D1 (эталонная индексация поля `+2`).
void UAsdaNetSubsystem::BuildWarehouseBasketBody55(const TArray<TArray<uint8>>& Basket, uint8* Out55) const
{
	// ★ Хвост: НЕ memzero. Каждая из 5 записей — `helper_initInvalidIds@0x973D30`.
	for (int32 i = 0; i < 5; ++i)
	{
		uint8* R = Out55 + 11 * i;
		AsdaBytes::WrI16(&R[0], -1);
		AsdaBytes::WrI16(&R[2], -1);
		R[4] = 0;                       // ★ именно 0, а не −1
		AsdaBytes::WrI32(&R[5], -1);
		AsdaBytes::WrI16(&R[9], -1);
	}

	const bool bLegacyF2 = CVarWarehouseLegacyField2.GetValueOnGameThread() != 0;
	UAsdaItemTableSubsystem* ITab = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	int32 Cnt = 0;
	for (int32 SlotIdx = 0; SlotIdx < Basket.Num() && Cnt < 5; ++SlotIdx)
	{
		const TArray<uint8>& E = Basket[SlotIdx];
		if (E.Num() < 16) { continue; }
		const int32 Code = AsdaBytes::RdI32(&E[0]);
		if (Code <= 0) { continue; }                 // эталон: `if (*(int*)&pChar[57*slot] > 0)`

		uint8* R = Out55 + 11 * Cnt;
		R[0] = E[5];  R[1] = E[6];                   // слот-источник
		R[4] = E[4];                                 // тип контейнера
		R[5] = E[9];  R[6] = E[10]; R[7] = E[11]; R[8] = E[12];   // количество
		R[9] = E[14]; R[10] = E[15];                 // вес стека (по умолчанию — из записи)

		// ★ Поле `+2`: эталон индексирует его номером ЯЧЕЙКИ, остальные — номером ЗАПИСИ (дефект D1).
		uint8* F2 = bLegacyF2 ? (Out55 + 11 * FMath::Min(SlotIdx, 4)) : R;
		F2[2] = E[7]; F2[3] = E[8];

		// Пересчёт веса стека: только обычные предметы (classify == 0), торгуемые, кроме золота 20551.
		//   `network.c:31272`: `+9 = LOWORD(количество) * LOWORD(field72)`.
		if (ITab && Code != 20551)
		{
			if (const FAsdaItemRecord* Rec = ITab->GetItem(Code))
			{
				if (Rec->TradableFlag)
				{
					const int32 Amount = AsdaBytes::RdI32(&E[9]);
					const int32 StackWeight = (int32)(int16)(uint16)((uint32)Amount * (uint32)Rec->Weight);
					AsdaBytes::WrI16(&R[9], StackWeight);
				}
			}
		}
		++Cnt;
	}
}

bool UAsdaNetSubsystem::SendItemKeeping5008(const TArray<TArray<uint8>>& Basket)
{
	if (!IsSendLinkUp()) { return false; }
	FAsdaPacketWriter W(55);
	BuildWarehouseBasketBody55(Basket, W.Raw(0, 55));   // общий сборщик пишет все 55 б (5 записей по 11), как у 99xx
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_KEEPING, TEXT("SendItemKeeping5008"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_KEEPING, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5008 положить на склад: записей в корзине %d (79 б) -> %s"),
		Basket.Num(), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ══════════════════ АУКЦИОН: отправители ══════════════════════════════════════════════════════
//   Раскладка шапки и записей — `Reports/npc_exchange_win_13000.md` §7.2/§7.6.
//
// ★★★ ПАДДИНГ У ВСЕЙ ГРУППЫ 99xx ВЫКЛЮЧЕН (найдено 2026-08-28, «регистрация не проходит»).
//   Правило `bPad = false` для 9901…9909 было записано в комментарии к паддингу выше ещё
//   2026-08, но НИ ОДИН аукционный отправитель его не применял — все шли с падом по умолчанию.
//   Цена ошибки видна на сервере: `Recv_TransAction_RegistItemReq@0x517AB0`
//   (`GameServer.c:106794`) первым делом зовёт `sub_5218A0(размер_кадра, 98, 1)`, а тот
//   (`GameServer.c:109805-109824`) при `a3 == 1` требует РОВНО `(размер − 49) % 98 == 0`.
//   Наш кадр = 34 + payload = 49 + 98·N, но пад `(счётчик += 11) % 20` добавлял 0…19 лишних байт
//   ⇒ проверка падала ⇒ сервер писал в лог `BrokenPack Size` и отвечал кодом **1**, то есть
//   нотисом **12677 «Не удалось зарегистрировать предмет»**. Ровно та же строгая проверка стоит
//   у 9905 (`:107776`, шаг 45) и 9909 (`:108844`, шаг 45).
//   ★ ПРОВЕРЕНО ПОШТУЧНО, а не «по группе»: шестой аргумент `CNetwork_Send` равен 0 у
//   9901 (`shop_trade.c:14551`), 9905 (`:15199`), 9915 (`:14136`), 9907 (`item_inventory.c:46237`)
//   и 9909 (`:46456`), и равен **1 у 9903** (`shop_trade.c:14948`) — там пад ОСТАВЛЕН.
void UAsdaNetSubsystem::BuildExchangeHeader15(uint8* Out) const
{
	// `sub_D3C720@0xD3C720` (`item_inventory.c:46249`) + ручные записи полей.
	//   ★ Пустые поля — −1, НЕ ноль (закон `helper_initInvalidIds@0x973D30`).
	Out[0] = 0xFF;                                   // −1
	Out[1] = 0xFF;                                   // −1
	Out[2] = 0x00;
	AsdaBytes::WrI16(Out + 3, -1);                   // −1
	// ★ 27.09.2026 [ОРИГИНАЛ]: +5 — charId. Во всех пяти отправителях эталона `*(_DWORD*)((char*)&hdr[2] + 1) = …->userIdx`
	//   (shop_trade.c:14396 / :14649 / :15074, item_inventory.c:46131 / :46333), а `CCharacter::userIdx` = `char+1052` = charId (миснейм).
	//   Прежде здесь стоял `SelfUserNum` при подписи «userIdx (i32), НЕ userNum» — противоречие. Сервер
	//   (`Recv_TransAction_RegistItemReq@GameServer.c:106794`) поле не сверяет — поведение сервера то же.
	AsdaBytes::WrI32(Out + 5, SelectedCharId);
	Out[9] = 0x00;
	// ★ +10 (а не +9): в эталоне шапка объявлена как `_WORD hdr[8]`, запись идёт `*(_DWORD*)&hdr[5]`.
	// ★★★ ЭТО ID (ТИП) NPC-АУКЦИОНЩИКА, а не «сессия» — доказательство в объявлении метода.
	//   Сервер ищет по нему NPC на карте (`sub_4375E0`), и НЕ НАЙДЯ отвечает кодом 2 (у 9903) /
	//   9 (у 9901). Именно это давало владельцу нотис 12672 «Такого персонажа нет на этой карте»
	//   вместо 12725 «Нет данных по разыскиваемому предмету».
	AsdaBytes::WrI32(Out + 10, ExchangeNpcId > 0 ? ExchangeNpcId : 0);
	Out[14] = 0x00;
}

void UAsdaNetSubsystem::SetExchangeNpcId(int32 NpcId)
{
	if (NpcId <= 0 || NpcId == ExchangeNpcId) { return; }
	UE_LOG(LogAsdaNet, Log, TEXT("аукцион: NPC-аукционщик %d → %d (поле +10 шапки 99xx)"),
		ExchangeNpcId, NpcId);
	ExchangeNpcId = NpcId;
}

bool UAsdaNetSubsystem::SendExchangeOpen9915()
{
	if (!Conn || !Conn->IsConnected()) { return false; }
	// `mediationSendOpen@0xD44370` (`shop_trade.c:14138`): payload пустой.
	const bool bOk = SendSecured(9915, nullptr, 0, /*bPad*/ false);
	UE_LOG(LogAsdaNet, Log, TEXT("→9915 открыть аукцион -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendExchangeRegister9901(const TArray<TArray<uint8>>& Rows98)
{
	if (!IsSendLinkUp() || Rows98.Num() <= 0) { return false; }
	// До 5 записей — цикл `i < 5` серверного `Recv_TransAction_RegistItemReq@0x517AB0`.
	const int32 N = FMath::Min(Rows98.Num(), 5);
	FAsdaPacketWriter W(15 + N * 98);
	BuildExchangeHeader15(W.Raw(0, 15));
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Copy = FMath::Min(Rows98[i].Num(), 98);
		W.Bytes(15 + i * 98, Rows98[i].GetData(), Copy);
	}
	if (AsdaPacketLayoutBroken(W, (uint16)9901, TEXT("SendExchangeRegister9901"))) { return false; }
	const bool bOk = SendSecured(9901, W.Data(), W.Num(), /*bPad*/ false);
	UE_LOG(LogAsdaNet, Log, TEXT("→9901 выставить лоты: записей %d (%d б) -> %s"),
		N, W.Num(), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendExchangeSearch9903(const uint8 Criteria8[8])
{
	if (!IsSendLinkUp() || !Criteria8) { return false; }
	// Сервер требует размер РОВНО 23 = 15 шапки + 8 критериев (`sub_523F00(a3, 0x17, …)`).
	FAsdaPacketWriter W(23);
	BuildExchangeHeader15(W.Raw(0, 15));
	W.Bytes(15, Criteria8, 8);
	// ★ У 9903 пад ЕСТЬ — единственный в группе: `CNetwork_Send(…, 9903, 1, 1, 1, 0, 0)`
	//   (`shop_trade.c:14948`), тогда как у 9901/9905/9907/9909/9915 шестой аргумент 0
	//   (`shop_trade.c:14551`, `:15199`, `item_inventory.c:46237`, `:46456`, `shop_trade.c:14136`).
	//   Сервер здесь размер не сверяет (только MD5 первых 23 б), поэтому хвост ему не мешает.
	if (AsdaPacketLayoutBroken(W, (uint16)9903, TEXT("SendExchangeSearch9903"))) { return false; }
	const bool bOk = SendSecured(9903, W.Data(), W.Num(), /*bPad*/ true);
	UE_LOG(LogAsdaNet, Log, TEXT("→9903 поиск: страница %d -> %s"),
		(int32)Criteria8[7], bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendExchangeBuy9905(const TArray<TArray<uint8>>& Rows45)
{
	if (!IsSendLinkUp() || Rows45.Num() <= 0) { return false; }
	const int32 N = FMath::Min(Rows45.Num(), 8);   // серверный буфер рассчитан на 8 (дефект S3)
	FAsdaPacketWriter W(15 + N * 45);
	BuildExchangeHeader15(W.Raw(0, 15));
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Copy = FMath::Min(Rows45[i].Num(), 45);
		W.Bytes(15 + i * 45, Rows45[i].GetData(), Copy);
	}
	if (AsdaPacketLayoutBroken(W, (uint16)9905, TEXT("SendExchangeBuy9905"))) { return false; }
	const bool bOk = SendSecured(9905, W.Data(), W.Num(), /*bPad*/ false);
	UE_LOG(LogAsdaNet, Log, TEXT("→9905 купить: лотов %d (%d б) -> %s"),
		N, W.Num(), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendExchangeMyLots9907(int32 CategoryIndex)
{
	if (!IsSendLinkUp()) { return false; }
	// Сервер требует размер РОВНО 16 = 15 шапки + 1 байт. Байт = индекс категории + 1
	//   (`item_inventory.c:46134-46136`).
	FAsdaPacketWriter W(16);
	BuildExchangeHeader15(W.Raw(0, 15));
	W.U8(15, (uint8)FMath::Clamp(CategoryIndex + 1, 1, 255));
	if (AsdaPacketLayoutBroken(W, (uint16)9907, TEXT("SendExchangeMyLots9907"))) { return false; }
	const bool bOk = SendSecured(9907, W.Data(), W.Num(), /*bPad*/ false);
	UE_LOG(LogAsdaNet, Log, TEXT("→9907 мои лоты: категория %d -> %s"),
		CategoryIndex, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendExchangeDelete9909(const TArray<TArray<uint8>>& Rows45)
{
	if (!IsSendLinkUp() || Rows45.Num() <= 0) { return false; }
	const int32 N = FMath::Min(Rows45.Num(), 10);  // серверный буфер рассчитан на 10 (дефект S4)
	FAsdaPacketWriter W(15 + N * 45);
	BuildExchangeHeader15(W.Raw(0, 15));
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Copy = FMath::Min(Rows45[i].Num(), 45);
		W.Bytes(15 + i * 45, Rows45[i].GetData(), Copy);
	}
	if (AsdaPacketLayoutBroken(W, (uint16)9909, TEXT("SendExchangeDelete9909"))) { return false; }
	const bool bOk = SendSecured(9909, W.Data(), W.Num(), /*bPad*/ false);
	UE_LOG(LogAsdaNet, Log, TEXT("→9909 снять лоты: записей %d (%d б) -> %s"),
		N, W.Num(), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendItemRetake5010(const TArray<TArray<uint8>>& Basket)
{
	if (!IsSendLinkUp()) { return false; }
	FAsdaPacketWriter W(55);
	BuildWarehouseBasketBody55(Basket, W.Raw(0, 55));   // общий сборщик пишет все 55 б (5 записей по 11), как у 99xx
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_ITEM_RETAKE, TEXT("SendItemRetake5010"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_ITEM_RETAKE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5010 забрать со склада: записей в корзине %d (79 б) -> %s"),
		Basket.Num(), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendStoreHousePageReq4050(int32 PageNo)
{
	// Тело 5 б: `+0 u32 userIdx`, `+4 u8 pageNo` (эталон `uiBagSendActionPacket@0xE89B30`,
	//   `ui_core.c:78585`: `v9[0] = …->userIdx; LOBYTE(v9[1]) = a2 + 1`, где `a2` — вкладка 0…8).
	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D2 (разбор `Reports/warehouse_net_server.md`):
	//   серверный `Send_ClientStoreHouseItemInfo@0x529DA0` НЕ проверяет диапазон и при `pageNo == 0`
	//   читает память ДО начала массива склада (`CUser+6342 − 1710`) и отправляет её клиенту —
	//   утечка чужой памяти по запросу клиента. Прежний порт слал ровно `0` (и клампил 0…255),
	//   то есть сам провоцировал утечку. Здесь клампим в РЕАЛЬНЫЙ диапазон вкладок 1…9.
	//   Ключ отката: `asda.Warehouse.RawPageNo 1` — вернуть неограниченную отправку (для отладки).
	const bool bRaw = CVarWarehouseRawPageNo.GetValueOnGameThread() != 0;
	const int32 SafePage = bRaw ? PageNo : FMath::Clamp(PageNo, 1, 9);
	if (!bRaw && SafePage != PageNo)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("→4050 FIX-ORIG D2: страница %d вне диапазона 1..9, отправляем %d (иначе сервер читает чужую память)"),
			PageNo, SafePage);
	}

	FAsdaPacketWriter W(5);
	W.I32(0, SelectedCharId);       // эталон кладёт сюда `->userIdx` = charId (`char+1052`); сервер его игнорирует, но шлём 1:1 (★ 27.09.2026: прежде userNum)
	W.U8(4, (uint8)FMath::Clamp(SafePage, 0, 255));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_CLIENT_STORE_HOUSE_ITEM_INFO, TEXT("SendStoreHousePageReq4050"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SEND_CLIENT_STORE_HOUSE_ITEM_INFO, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→4050 страница склада %d (вкладка %d, 29 б) -> %s"),
		SafePage, SafePage - 1, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// C→S 6575 «крутить рулетку» (`netSendAntiCheatRequest@0xE6E2D0`, ★ МИСНЕЙМ), 28 = 24 + 4.
//   payload(4): @0 i32 charId локального игрока (`g_LocalPlayer+1052`; доказано сверкой с 6580@1).
bool UAsdaNetSubsystem::SendRouletteSpin6575()
{
	if (!IsSendLinkUp()) { return false; }
	FAsdaPacketWriter W(4);
	W.I32(0, SelectedCharId);   // ★ i32 charId, НЕ userNum (i16)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_SOUL_ROULETTET, TEXT("SendRouletteSpin6575"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_RECV_USER_SOUL_ROULETTET, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log, TEXT("→6575 крутить рулетку: charId=%d -> %s"),
		SelectedCharId, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 6579 «забрать приз» (`netSendAntiCheatPacket@0xE6E5F0`, ★ МИСНЕЙМ), 28 = 24 + 4.
//   ★★★ ПРОБЕЛ ЗАКРЫТ 31.08.2026 (было «содержимое 4 б не доказано, уходят нули»).
//
//   (1) Чем пишется payload. Прежний вывод «неразрешённый thunk `sub_934D28`» — ошибка метода:
//       `sub_934D28` не функция, а 5-байтовая заглушка инкрементальной компоновки MSVC.
//       Дизасм РЕАЛЬНОГО EXE (`AsdaStory.exe`, .text VA 0x91E000 → файл 0x1000):
//           0x934d28:  jmp 0xe6e760
//       Тело цели (`sub_E6E760@0xE6E760`) целиком:
//           0xe6e769:  mov  [ebp-4], ecx          ; this
//           0xe6e76c:  mov  eax, [ebp-4]
//           0xe6e76f:  mov  dword ptr [eax], 0xffffffff   ← ЕДИНСТВЕННОЕ действие
//           0xe6e775:  mov  eax, [ebp-4]  /  ret
//       То есть это конструктор 4-байтового поля, кладущий −1.
//
//   (2) Почему Hex-Rays это спрятал. В экспорте тело выглядит как `v8[1] = this; sub_934D28(v8);`
//       и создаёт впечатление «структуры из двух dword». Дизасм 0xE6E5F0 показывает ДВЕ РАЗНЫЕ
//       соседние стековые локали (грабли проекта «локали склеены в структуру»):
//           0xe6e5fc:  mov  [ebp-4], ecx         ; this окна рулетки  — НЕ payload
//           0xe6e5ff:  lea  ecx, [ebp-8]         ; payload-dword
//           0xe6e602:  call 0x934d28             ; [ebp-8] := 0xFFFFFFFF
//       Между этим вызовом и отправкой в [ebp-8] НИКТО не пишет (проверены все store'ы
//       0xE6E607…0xE6E6C7: [ebp-0x18], [ebp-0x20], [ebp-0x820], [ebp-0x824]), а дальше:
//           0xe6e627:  mov ecx, 0x2901123 / call 0x928190   ; MD5(соль, [ebp-8], 4) → [ebp-0x18]
//           0xe6e6c2:  call 0x92747a                        ; memcpy(buf+24, [ebp-8], 4)
//           0xe6e6e3:  push 0x19b3                          ; опкод 6579, размер 28
//       ⇒ payload 6579 = КОНСТАНТА 0xFFFFFFFF. `SlotIndex` в провод не идёт и в эталоне:
//       обработчик кнопок сектора (game.c:473622-473624, id 0x105C7..0x105CA) отдаёт индекс
//       ТОЛЬКО в клиентскую отрисовку `RoulettePanel_UpdateRewardSlots`, затем зовёт отправку.
//
//   (3) Второй метод — сервер. `Recv_UserSoulResult@0x49A8C0` тело ЧИТАЕТ, но значение теряет:
//         GameServer.c:61989  result = sub_523F00(a3, 4u, a2, 1);   // длина payload обязана быть 4,
//                                                                   // MD5 считается по этим 4 б
//         GameServer.c:61991  memcpy(&v8, (char *)a3 + 33, 4u);     // a3+33 == наш payload+0
//         GameServer.c:62008  v8.m_pcThread = *(CThreadRun **)(v11 + 99385);  // ← ЗАТИРАЕТ прочитанное
//       Между :61991 и :62008 `v8` не используется. Приз считается из CUser: строка +99382,
//       сектор +99384, база +99385 (:62005-62008, ветки switch :62016-62100).
//       Сдвиг конверта +9 подтверждён тем же обработчиком: секрет клиента buf+0 ↔ сервер a3+9
//       (:61986), MD5 клиента buf+8 ↔ сервер a3+17 (`sub_523F00`, GameServer.c:110888).
//   ⇒ Шлём ровно то, что шлёт эталон: 0xFFFFFFFF. Уверенность HIGH.
bool UAsdaNetSubsystem::SendRouletteClaim6579(int32 SlotIndex)
{
	if (!IsSendLinkUp()) { return false; }
	FAsdaPacketWriter W(4);
	W.I32(0, -1);   // @0 i32 = 0xFFFFFFFF — константа конструктора sub_E6E760
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_SOUL_RESULT, TEXT("SendRouletteClaim6579"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_RECV_USER_SOUL_RESULT, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6579 забрать приз рулетки: payload = 0xFFFFFFFF (как в эталоне; сектор=%d в провод ")
		TEXT("не идёт — сервер берёт строку/сектор/базу из CUser +99382/+99384/+99385) -> %s"),
		SlotIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}
