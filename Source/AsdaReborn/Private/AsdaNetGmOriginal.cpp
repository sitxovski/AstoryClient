// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 19.10.2026: отправители ШТАТНЫХ ОПЕРАТОРСКИХ КОМАНД ОРИГИНАЛА — методы `UAsdaNetSubsystem`, вынесенные в свой файл
//   (общий `AsdaNetSubsystem.cpp` и так огромен; объявления — в `Public/AsdaNetSubsystem.h`, блок «ШТАТНЫЕ ОПЕРАТОРСКИЕ
//   КОМАНДЫ ОРИГИНАЛА»). Раскладки тел сняты с дизасма отправителей оригинального клиента и с серверных обработчиков:
//   `Reports/gm_original_commands/group1_spec.md` (6012/6014/6018/6022/6026/6028/6029),
//   `group2_spec.md` (6032/6033/6035/6036/6045/6046), `group3_spec.md` (6038/6040/6042/6075),
//   `group4_spec.md` (4209/6007/6010/6024/6058/6145/6147; 6146 — давний `SendClientTargetJump6146`).
//   Конверты (у оригинала их три вида):
//     A — «токен(8) + MD5(payload)(16) + payload» = `SendSecured` (6012/6014/6018/6022/6026/6032/6033/6038/6040/6042/6045/6046/6075);
//     B — «только MD5 пустоты (16)», без токена (6028/6029: сервер тело не читает, group1 §1.2);
//     C — «только токен (8)», без MD5 (6035/6036: сервер читает лишь кадр+9, group2 §5.2);
//     пустое тело без конверта — 6007 (group4 §2.1).
//   Гейты исходящих — та же маска, что у `SendSecured` (XyT → одержимость → «душа» → переход). Стенд перехвата
//   (`IsStandCapture`) — тот же путь, что у прочих отправителей: при стоящем перехватчике сокета нет.
#include "AsdaNetSubsystem.h"
#include "AsdaConnection.h"
#include "AsdaOutgoingGate.h"
#include "AsdaPacketWriter.h"
#include "Net/NetOpcodes.h"
#include "Misc/SecureHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGmOrig, Log, All);

namespace AsdaGmOrig
{
	// Опкоды команд (C→S). В `NetOpcodes.h` часть из них заведена под другими (вызывающими) именами — здесь своё имя
	//   по серверному обработчику, чтобы журнал читался без таблицы.
	constexpr uint16 kSummon = 6012, kMeet = 6014, kKick = 6018, kKill = 6022, kMove = 6026;
	constexpr uint16 kLocalChat = 6028, kGlobalChat = 6029, kAdd = 6032, kGetMob = 6033;
	constexpr uint16 kInvincible = 6035, kGetValue = 6036, kEventSet = 6038, kEventOff = 6040;
	constexpr uint16 kItemGive = 6042, kMonsterPop = 6045, kMonsterReturn = 6046, kCharValue = 6075;
	// Группа 4 (`group4_spec.md`).
	constexpr uint16 kCallMap = 4209, kClearItems = 6007, kJump = 6010, kRevive = 6024, kCoolReset = 6058;
	constexpr uint16 kCountDown = 6145, kMapBlock = 6147;
	// Предел имени: 19 байт cp1251 + NUL (group1 §2.1: имя ровно 20 б RelayServer дочитывает в соседние поля).
	constexpr int32 kNameMax = 19;

	// Тело собрано без выхода за границы (иначе — ошибка программиста, пакет не шлём).
	static bool LayoutOk(const FAsdaPacketWriter& W, uint16 Op, const TCHAR* Who)
	{
		if (W.IsOk()) { return true; }
		UE_LOG(LogAsdaGmOrig, Error, TEXT("[GM-ориг] %s: раскладка тела %u вышла за границы — не шлём"), Who, (uint32)Op);
		return false;
	}

	// Имя цели в поле 20 б: cp1251, 1…19 байт, хвост нулями. false — пусто или длиннее 19 байт.
	static bool PutName(FAsdaPacketWriter& W, const FString& Name, const TCHAR* Who, uint16 Op)
	{
		const FString N = Name.TrimStartAndEnd();
		// ★ 19.10.2026 (ревизия М-6): RelayServer ищет имя запросом `EXEC UPPrivateChat '%s'` (group1 §2.1) — апостроф ломает SQL.
		//   Имён с апострофом в игре нет, поэтому такое имя не шлём.
		if (N.Contains(TEXT("'")))
		{
			UE_LOG(LogAsdaGmOrig, Warning, TEXT("[GM-ориг] %s (%u): имя «%s» с апострофом — не шлём"), Who, (uint32)Op, *Name);
			return false;
		}
		const int32 Len = N.IsEmpty() ? 0 : W.Cp1251(0, N, kNameMax + 1);
		if (Len <= 0 || Len > kNameMax)
		{
			UE_LOG(LogAsdaGmOrig, Warning, TEXT("[GM-ориг] %s (%u): имя «%s» пустое или длиннее %d байт — не шлём"),
				Who, (uint32)Op, *Name, kNameMax);
			return false;
		}
		// хвост поля уже нулевой (сборщик заполнен нулями) — как memset+strcpy отправителя оригинала
		return true;
	}
}

// ── конверт A (токен + MD5 + payload): все «полные» команды ──────────────────────────────────────────────────

bool UAsdaNetSubsystem::SendGmSummon6012(const FString& TargetName)
{
	FAsdaPacketWriter W(20);
	if (!AsdaGmOrig::PutName(W, TargetName, TEXT("SendGmSummon6012"), AsdaGmOrig::kSummon) || !AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kSummon, TEXT("6012"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kSummon, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6012 вызвать к себе «%s» -> %s"), *TargetName, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmMeet6014(const FString& TargetName)
{
	FAsdaPacketWriter W(20);
	if (!AsdaGmOrig::PutName(W, TargetName, TEXT("SendGmMeet6014"), AsdaGmOrig::kMeet) || !AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kMeet, TEXT("6014"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kMeet, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6014 переместиться к «%s» -> %s"), *TargetName, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmKick6018(const FString& TargetName)
{
	FAsdaPacketWriter W(20);
	if (!AsdaGmOrig::PutName(W, TargetName, TEXT("SendGmKick6018"), AsdaGmOrig::kKick) || !AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kKick, TEXT("6018"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kKick, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6018 выкинуть «%s» -> %s"), *TargetName, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmKill6022(int32 TargetUserNum, int32 TargetUserIndex)
{
	if (TargetUserNum < 0) { return false; }
	// [0..1] i16 userNum цели, [2..5] i32 UserIndex цели (`0x9F6E64…0x9F6E6F`, group1 §5.1).
	FAsdaPacketWriter W(6);
	W.I16(0, TargetUserNum).I32(2, TargetUserIndex);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kKill, TEXT("6022"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kKill, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6022 убить userNum=%d UserIndex=%d -> %s"), TargetUserNum, TargetUserIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmMove6026(uint8 Mode, int32 CellX, int32 CellY, int32 TargetUserNum, int32 TargetUserIndex)
{
	if (TargetUserNum < 0) { return false; }
	// [0] u8 режим, [1..2] i16 X, [3..4] i16 Y (клетки), [5..6] i16 userNum, [7..10] i32 UserIndex (group1 §6.1).
	FAsdaPacketWriter W(11);
	W.U8(0, Mode).I16(1, CellX).I16(3, CellY).I16(5, TargetUserNum).I32(7, TargetUserIndex);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kMove, TEXT("6026"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kMove, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6026 переместить режим=%u клетка (%d,%d) userNum=%d UserIndex=%d -> %s"),
		(uint32)Mode, CellX, CellY, TargetUserNum, TargetUserIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmAdd6032(uint8 Type, int32 Value)
{
	// [0] u8 тип (0 опыт, 1 золото, 2 очки навыков), [1..4] i32 значение (group2 §3.1; три отправителя 0xA097F0/0xA09E00/0xA0A3E0).
	FAsdaPacketWriter W(5);
	W.U8(0, Type).I32(1, Value);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kAdd, TEXT("6032"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kAdd, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6032 добавить себе тип=%u значение=%d -> %s"), (uint32)Type, Value, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmMobInfo6033(uint8 Mode, int32 MobIndex)
{
	// [0] u8 режим, [1..4] i32 индекс моба NpcServer, [5..6] u16 userNum (сервер перезаписывает, group2 §4.1).
	FAsdaPacketWriter W(7);
	W.U8(0, Mode).I32(1, MobIndex).I16(5, bSelfUserNumKnown ? (int32)SelfUserNum : -1);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kGetMob, TEXT("6033"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kGetMob, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6033 монстр режим=%u индекс=%d -> %s"), (uint32)Mode, MobIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmEventSet6038(uint8 Type, float Mult, int32 Channel, int32 Map, uint8 SMon, uint8 SDay, uint8 SHour,
	uint8 SMin, uint8 EMon, uint8 EDay, uint8 EHour, uint8 EMin, uint8 MinLevel, uint8 MaxLevel)
{
	// group3 §1.1: [0..5] три i16 −1 (канал GS / система GS / userNum — GS затирает), [6..13] даты, [14] тип, [15..18] f32,
	//   [19..22] i32 канал, [23..26] i32 карта, [27] MinLevel, [28] MaxLevel. Предзаполнение отправителя `0xA0C0E0`.
	FAsdaPacketWriter W(29);
	W.I16(0, -1).I16(2, -1).I16(4, -1)
	 .U8(6, SMon).U8(7, SDay).U8(8, SHour).U8(9, SMin).U8(10, EMon).U8(11, EDay).U8(12, EHour).U8(13, EMin)
	 .U8(14, Type).F32(15, Mult).I32(19, Channel).I32(23, Map).U8(27, MinLevel).U8(28, MaxLevel);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kEventSet, TEXT("6038"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kEventSet, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6038 ивент тип=%u ×%.2f канал=%d карта=%d %u.%u %u:%02u → %u.%u %u:%02u ур. %u…%u -> %s"),
		(uint32)Type, Mult, Channel, Map, (uint32)SDay, (uint32)SMon, (uint32)SHour, (uint32)SMin, (uint32)EDay, (uint32)EMon,
		(uint32)EHour, (uint32)EMin, (uint32)MinLevel, (uint32)MaxLevel, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmEventOff6040(uint8 Type, int32 Channel, int32 Map)
{
	// group3 §2.1: [0..5] три i16 −1, [6] тип, [7..10] i32 канал, [11..14] i32 карта.
	FAsdaPacketWriter W(15);
	W.I16(0, -1).I16(2, -1).I16(4, -1).U8(6, Type).I32(7, Channel).I32(11, Map);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kEventOff, TEXT("6040"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kEventOff, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6040 выключить ивент тип=%u канал=%d карта=%d -> %s"), (uint32)Type, Channel, Map, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmItemGive6042(int32 TargetUserNum, int32 TargetUserIndex, int32 ItemCode, int32 Amount, bool bToWarehouse)
{
	if (TargetUserNum < 0 || ItemCode <= 0) { return false; }
	// group3 §3.1: [0..1] i16 userNum, [2..5] i32 UserIndex, [6..9] i32 код, [10..13] i32 количество, [14] u8 0 сумка / 1 склад.
	FAsdaPacketWriter W(15);
	W.I16(0, TargetUserNum).I32(2, TargetUserIndex).I32(6, ItemCode).I32(10, Amount).U8(14, bToWarehouse ? 1 : 0);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kItemGive, TEXT("6042"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kItemGive, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6042 выдать userNum=%d UserIndex=%d код=%d ×%d %s -> %s"), TargetUserNum, TargetUserIndex,
		ItemCode, Amount, bToWarehouse ? TEXT("на склад") : TEXT("в сумку"), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmMonsterPop6045(int32 Group, uint8 Mode)
{
	// group2 §7.1: [0..1] u16 userNum (сервер перезаписывает), [2..3] u16 группа, [4] u8 режим.
	FAsdaPacketWriter W(5);
	W.I16(0, bSelfUserNumKnown ? (int32)SelfUserNum : -1).U16(2, (uint32)Group).U8(4, Mode);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kMonsterPop, TEXT("6045"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kMonsterPop, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6045 вызвать группу монстров %d режим=%u -> %s"), Group, (uint32)Mode, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmMonsterReturn6046(int32 Group)
{
	// group2 §8.1: [0..1] u16 userNum (сервер перезаписывает), [2..3] u16 группа.
	FAsdaPacketWriter W(4);
	W.I16(0, bSelfUserNumKnown ? (int32)SelfUserNum : -1).U16(2, (uint32)Group);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kMonsterReturn, TEXT("6046"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kMonsterReturn, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6046 убрать группу монстров %d -> %s"), Group, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmCharValue6075(uint8 TargetType, int32 TargetIndex)
{
	// group3 §4.1: [0..1] свой userNum (сервер требует совпадения с отправителем), [2..5] свой UserIndex, [6] тип цели
	//   (1 игрок / 0 монстр), [7..8] индекс цели, [9..23] нули (заполняют NpcServer и GS в ответе).
	if (!bSelfUserNumKnown) { return false; }
	FAsdaPacketWriter W(24);
	W.I16(0, (int32)SelfUserNum).I32(2, GetSelfCharUserIndex()).U8(6, TargetType).I16(7, TargetIndex);   // [2..5] = свой CUser+0x174
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kCharValue, TEXT("6075"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kCharValue, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6075 проценты против цели тип=%u индекс=%d -> %s"), (uint32)TargetType, TargetIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

// ── конверт B (только MD5 пустоты, 16 б): объявления ─────────────────────────────────────────────────────────

bool UAsdaNetSubsystem::SendGmLocalChat6028()
{
	return SendGmEmptyMd5(AsdaGmOrig::kLocalChat, TEXT("объявление сектору (текст — файл сервера GM_Com_kor.txt)"));
}

bool UAsdaNetSubsystem::SendGmGlobalChat6029()
{
	return SendGmEmptyMd5(AsdaGmOrig::kGlobalChat, TEXT("объявление всему серверу (текст — файл сервера GM_Com_kor.txt)"));
}

bool UAsdaNetSubsystem::SendGmEmptyMd5(uint16 Op, const TCHAR* What)
{
	// group1 §1.2: отправители `0x9F60E0`/`0x9F5FF0` кладут в тело ТОЛЬКО MD5 пустой строки (16 б), токена нет.
	const bool bStand = IsStandCapture(Op);
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		if (FAsdaOutgoingGate::Block(this, Op, EAsdaGate::XyT | EAsdaGate::Possess | EAsdaGate::Soul | EAsdaGate::Transfer)) { return false; }
	}
	uint8 Body[16];
	{ FMD5 Md5; Md5.Final(Body); }   // MD5 пустого payload
	bool bOk = false;
	if (bStand) { const auto Cap = SendCaptureForStand; Cap(Op, Body, 16, true, TEXT('E')); bOk = true; }   // копия: см. SendSecured
	else { bOk = Conn->SendPacket(Op, Body, 16, 0, 0, /*bPad*/true); }
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →%u %s (тело 16 б) -> %s"), (uint32)Op, What, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

// ── конверт C (только токен, 8 б): неуязвимость и характеристики ─────────────────────────────────────────────

bool UAsdaNetSubsystem::SendGmInvincible6035()
{
	return SendGmTokenOnly(AsdaGmOrig::kInvincible, TEXT("переключить неуязвимость (ответа у сервера нет)"));
}

bool UAsdaNetSubsystem::SendGmStats6036()
{
	// Тот же пакет, что проба прав `SendGmRightsProbe6036`, но БЕЗ защёлки «неотвеченная проба»: это запрос оператора,
	//   ответ 6037 печатается в чат (`UAsdaGmOriginalSubsystem`). Права к этому моменту уже подтверждены.
	return SendGmTokenOnly(AsdaGmOrig::kGetValue, TEXT("мои характеристики (ответ 6037)"));
}

bool UAsdaNetSubsystem::SendGmTokenOnly(uint16 Op, const TCHAR* What)
{
	// group2 §5.2: тело = 8 б токена, MD5 не считается (сервер читает только кадр+9).
	bool bOk = false;
	if (IsStandCapture(Op))
	{
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри (см. SendSecured)
		uint8 Body[8];
		FillAntiReplaySecret(Body);
		Cap(Op, Body, 8, true, TEXT('T'));
		bOk = true;
	}
	else
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		if (FAsdaOutgoingGate::Block(this, Op, EAsdaGate::XyT | EAsdaGate::Possess | EAsdaGate::Soul | EAsdaGate::Transfer)) { return false; }
		uint8 Body[8];
		FillAntiReplaySecret(Body);   // [0..7] токен — всё тело
		bOk = ConnSendSecret(Op, Body, sizeof(Body), 0, 0, /*bPad*/true);
	}
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →%u %s (тело 8 б) -> %s"), (uint32)Op, What, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

// ── кик оператором (6019) ────────────────────────────────────────────────────────────────────────────────────

void UAsdaNetSubsystem::CloseAsServerDropped(const TCHAR* Why)
{
	// Эталон `netShowDisconnectPopupAndChatNotice@0xF49A00` → `Show_DisconnectPopup(2113)` → `netCloseSocketBySlot` (group1 §4.3):
	//   клиент закрывает связь сам. У порта единая точка «соединение оборвано» — `HandleConnectionClosed` (сброс сессии,
	//   `OnGameDisconnected` → попап 8000; текст 2113 заранее выставил `UAsdaDisconnectPopupSubsystem::SetNextNotice`).
	UE_LOG(LogAsdaGmOrig, Warning, TEXT("[GM-ориг] связь закрывается: %s"), Why ? Why : TEXT("?"));
	HandleConnectionClosed();
}

// ── группа 4 (`group4_spec.md`): сбор карты, предметы с земли, прыжок, воскрешение, откаты, отсчёт, блок карты ──────

bool UAsdaNetSubsystem::SendGmCallMap4209(int32 Map)
{
	// group4 §1.1: [0..1] i16 — сервер не читает (слать 0), [2..3] i16 номер карты M (индекс MapInfo.ini).
	FAsdaPacketWriter W(4);
	W.I16(0, 0).I16(2, Map);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kCallMap, TEXT("4209"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kCallMap, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →4209 собрать игроков карты %d своего канала к себе -> %s"), Map, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmClearGroundItems6007()
{
	// group4 §2.1: тело ПУСТОЕ — ни токена, ни MD5 (отправитель `0x9F7A90`: `CNetwork_Send(…, NULL, 0, 0x1777, 1,…)`;
	//   обработчик сервера кадр не читает). Пад провода как у оригинала (a6 = 1).
	const uint16 Op = AsdaGmOrig::kClearItems;
	bool bOk = false;
	if (IsStandCapture(Op))
	{
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри (см. SendSecured)
		Cap(Op, nullptr, 0, true, TEXT('N'));
		bOk = true;
	}
	else
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		if (FAsdaOutgoingGate::Block(this, Op, EAsdaGate::XyT | EAsdaGate::Possess | EAsdaGate::Soul | EAsdaGate::Transfer)) { return false; }
		bOk = Conn->SendPacket(Op, nullptr, 0, 0, 0, /*bPad*/true);
	}
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6007 убрать предметы с земли в своём секторе (тело 0 б) -> %s"), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmJump6010(int32 CellX, int32 CellZ, int32 MapId)
{
	// group4 §3.1: [0..1] i16 клетка X, [2..3] i16 клетка Z назначения (своя карта).
	// ★ 09.11.2026 [ПОРТ, P123]: MapId ≥ 0 — форма P123 «на другую карту»: [4..5] i16 карта, [6..7] u16 метка 0xA123 (тело 8 б).
	//   Шлётся БЕЗ паддинга (кадр ровно 42 б — в пределе P55 для 6010, 38…57): по длине P123 формы не различает (стоковая 6010 с
	//   паддингом тоже бывает 40–57 б), признак — метка; в паддинге стоковой формы нули. Стоковый сервер (без P123) считает MD5 по
	//   4 б — такой кадр молча отбрасывает.
	const bool bMap = MapId >= 0;
	FAsdaPacketWriter W(bMap ? 8 : 4);
	W.I16(0, CellX).I16(2, CellZ);
	if (bMap) { W.I16(4, MapId).I16(6, 0xA123); }   // I16 пишет младшие 16 бит
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kJump, TEXT("6010"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kJump, W.Data(), W.Num(), /*bPad=*/!bMap);
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6010 прыжок в клетку (%d,%d)%s -> %s"), CellX, CellZ,
		bMap ? *FString::Printf(TEXT(" карты %d (P123)"), MapId) : TEXT(""), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmRevive6024(int32 TargetUserNum, int32 TargetUserIndex)
{
	if (TargetUserNum < 0) { return false; }
	// group4 §4.1: [0..1] i16 userNum цели, [2..5] i32 UserIndex цели (сервер сверяет с CUser+0x174) — раскладка 6022.
	FAsdaPacketWriter W(6);
	W.I16(0, TargetUserNum).I32(2, TargetUserIndex);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kRevive, TEXT("6024"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kRevive, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6024 воскресить/восполнить userNum=%d UserIndex=%d -> %s"), TargetUserNum, TargetUserIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmCoolReset6058(int32 TargetUserNum)
{
	if (TargetUserNum < 0) { return false; }
	// group4 §5.1: [0..1] i16 userNum цели (свой или чужой на этом GS); сверки UserIndex у сервера нет.
	FAsdaPacketWriter W(2);
	W.I16(0, TargetUserNum);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kCoolReset, TEXT("6058"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kCoolReset, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6058 сбросить откаты навыков userNum=%d -> %s"), TargetUserNum, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmCountDown6145(int32 Seconds, uint8 Scope, int32 EndAction)
{
	// group4 §6.2: отправитель `0xBB4D30` при `сек ≤ 0` не шлёт ничего.
	if (Seconds <= 0) { return false; }
	// group4 §6.1: [0..3] i32 секунды, [4] u8 охват (0 карта / 1 канал / 2 все каналы), [5..6] i16 действие по концу (0/1/2).
	FAsdaPacketWriter W(7);
	W.I32(0, Seconds).U8(4, Scope).I16(5, EndAction);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kCountDown, TEXT("6145"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kCountDown, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6145 обратный отсчёт %d с охват=%u действие=%d -> %s"), Seconds, (uint32)Scope, EndAction, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendGmMapBlock6147(uint8 On, int32 A3, int32 A4)
{
	// group4 §8.1: [0] u8 1 — блок включить / иное — снять, [1..4] и [5..8] i32 — не читает никто (отправитель кладёт
	//   свои a3/a4: у конца отсчёта (1,0,0), у пункта меню — значения инициализатора −1/−1).
	FAsdaPacketWriter W(9);
	W.U8(0, On).I32(1, A3).I32(5, A4);
	if (!AsdaGmOrig::LayoutOk(W, AsdaGmOrig::kMapBlock, TEXT("6147"))) { return false; }
	const bool bOk = SendSecured(AsdaGmOrig::kMapBlock, W.Data(), W.Num());
	UE_LOG(LogAsdaGmOrig, Log, TEXT("[GM-ориг] →6147 блок карты %s (%d,%d) -> %s"), On == 1 ? TEXT("ВКЛ") : TEXT("СНЯТ"), A3, A4, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}
