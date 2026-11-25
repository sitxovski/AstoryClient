// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_Combat.cpp — ★ 22.10.2026 часть UAsdaNetSubsystem, вынесенная из AsdaNetSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_net_2026-10-22/plan.json).
//   Движение 4006/4008, подбор 5012, смерть и воскрешение, атака 4026/4028/4201/4203, PvP, цель 6538,
//   отмена баффа, навыки 5256/5231/5253, панель 5067, HP/MP и стат-блоки своего персонажа, консоль asda.Vitals.Dump.
//   #include — те же, что у AsdaNetSubsystem.cpp, в том же порядке; общие хелперы — AsdaNetSubsystemInternal.h.
#include "AsdaNetSubsystem.h"
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire / AsdaWeightUnsignedOn — вес по проводу u16
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (4026/4028/5256)
#include "AsdaTimedSend.h"                     // ★ 15.10.2026: поток точной отправки (стендовая постановка 4028)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"   // ★ 24.10.2026: статус хода 4006 своего персонажа (присед — 4)
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"   // ★ гейт продолжения боя: обездвиживающие состояния
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
#include "AsdaNetSubsystemInternal.h"   // ★ 22.10.2026 разнос: общее для частей AsdaNetSubsystem*.cpp (LogAsdaNet, чистые хелперы)
#include "World/AsdaCrouchStand.h"      // ★ 05.11.2026: присед стоя — отметки 4006/4008 для 4024 132/133


// ★ 09.10.2026 (МЕЛК-1 ревизии autoshovel_fishbooks): [ПОРТ] сторож флага «подбор 5012 в пути» (`bItemPickPending`).
//   Эталон (`SendItemPickPacket@0x9FCFB0`, network.c:5834) выходит сразу, пока ответа 5013/5015 нет, и сторожа не имеет.
//   Но у сервера есть тихие ранние выходы `CharItemPicking` (GameServer.c:84027-84040) — молчание защёлкнуло бы подбор
//   навсегда (закон no-state-latch-before-shown). Через N секунд без ответа флаг снимается. 0 = как эталон (без сторожа).
// ★★★ 05.11.2026 (баг «под оглушением персонаж атакует и наносит урон», сессия stun-attack; отчёт
//   AstoryServer/Docs/GameServer/cant-act-gate-p107.md). [ОРИГИНАЛ] + [ИСПРАВЛЕНИЕ]. Эталон гейтит бой состояниями «не может действовать»
//   `charHasAnyEquippedItemType@0xA38E40` (0 лёд, 1 окаменение, 3 оглушение, 14 мощное оглушение, 8 сон, 4 страх) в тике замаха
//   (`Player_ProcessAttackTick@0x99F2A0` / `CCharacter_AttackAnimTick@0x9A2200`: на границе шага — `CharFx_AutoSabzil_State(…, 0, 0, 20)`,
//   покой и сброс очереди) и перед 4028/4203. Порт гейтил только отправку 4028/4203: замах, начатый до состояния, доигрывал, цифры
//   очереди выходили, у соседей серия не прерывалась вовсе, а 4026 уходил и под станом (сервер его принимает — дефект
//   `sub_484E40`, серверная правка P107; NpcServer кладёт очередь урона, которую применит на следующей заявке уже после снятия).
//   1 (по умолчанию) = своя и соседская серия обрываются в кадре наложения (свой — путь отказа 4028, сосед — конец серии
//   `StopPeerAttackSeries`), 4026 под состоянием не уходит [ИСПРАВЛЕНИЕ: эталон шлёт 4026 и гасит замах на конце шага];
//   0 = прежний порт (гейт только у отправки 4028/4203).
// ★ 08.11.2026 (asda.Death.ZeroVitals): своя смерть → HP/MP = 0; определение ниже, у хранилища HP. Зовут EnterSelfDeadState и 4053.
void AsdaZeroSelfVitalsOnDeath(UAsdaNetSubsystem* Net, const TCHAR* Why);

static ASDA_DEV_CVAR(int32, CVarAsdaControlStopsAttack,
	TEXT("asda.Combat.ControlStopsAttack"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 05.11] Лёд/окаменение/оглушение/мощное оглушение/сон/страх (0/1/3/14/8/4): 1 = своя и соседская серия атаки ")
	TEXT("обрываются сразу (замах гаснет, очередь цифр сброшена), 4026 под состоянием не уходит; 0 = прежний порт (гейт только у 4028/4203)."),
	ECVF_Default);

// Аксессор ключа для других единиц трансляции (без поиска по имени — правило Shipping-проверки).
bool AsdaControlStopsAttackOn() { return CVarAsdaControlStopsAttack.GetValueOnGameThread() != 0; }

static ASDA_DEV_CVAR(float, CVarItemPickPendingGuardSec,
	TEXT("asda.Loot.PickPendingGuardSec"), 5.f,
	TEXT("[ПОРТ] сторож «подбор 5012 в пути»: через N с без ответа 5013/5015 флаг снимается (0 = эталон, без сторожа)."),
	ECVF_Default);

// ── Движение (player_control_move_re.md): тело 27б, payload = MD5(тело)+тело (без секрета) ──
static void FillMoveBody(uint8* Body43, uint8 Type, uint16 TgtUserIdx, uint16 Flag,
	float CurX, float CurZ, float DstX, float DstZ, uint16 TgtRenderId)
{
	uint8* T = Body43 + 16;
	FMemory::Memzero(Body43, 43);
	T[0] = Type;
	FMemory::Memcpy(T + 1, &TgtUserIdx, 2);
	FMemory::Memcpy(T + 3, &Flag, 2);
	FMemory::Memcpy(T + 5, &CurX, 4);
	FMemory::Memcpy(T + 9, &CurZ, 4);
	FMemory::Memcpy(T + 13, &DstX, 4);
	FMemory::Memcpy(T + 17, &DstZ, 4);
	FMemory::Memcpy(T + 25, &TgtRenderId, 2);
	FMD5 Md5; Md5.Update(T, 27); Md5.Final(Body43);   // MD5(тело27) впереди
}

// ★ Учёт неудачных отправок: 3 подряд FAIL (сокет-буфер встал/соединение мертво) → считаем game-коннект
//   потерянным, чтобы клиент не «дёргался» молча замахами в пустоту (живой лог 2026-07-29 19:26).
bool UAsdaNetSubsystem::TrackSend(bool bOk)
{
	if (bOk) { SendFailStreak = 0; return true; }
	if (++SendFailStreak >= 3 && bInGameConnected)
	{
		bInGameConnected = false;
		UE_LOG(LogAsdaNet, Error, TEXT("Game-соединение потеряно (%d подряд неудачных отправок) — стоп боя/движения"), SendFailStreak);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР: %d неудачных отправок подряд — игровое соединение считается потерянным → OnGameDisconnected (стоп боя)"), SendFailStreak);
		OnGameDisconnected.Broadcast();
	}
	return false;
}

// ★ Подбор лута (эталон SendItemPickPacket@0x9FCFB0, 5012, 32б):
//   [0..7] анти-реплей токен + [8..23] MD5(payload,8) + [24..31] payload = 4×i16 тайлов.
bool UAsdaNetSubsystem::SendItemPick5012(const FIntPoint& CharTile, const FIntPoint& ItemTile)
{
	// ★ 09.10.2026 (МЕЛК-1 ревизии autoshovel_fishbooks): гейт эталона первой строкой — `if (sub_92101B(g_pUIManager)) return`
	//   (network.c:5834): пока прошлый 5012 без ответа, новый не уходит ни из цикла автолопаты, ни из окна действий, ни
	//   по клику на лут. Флаг читается со сторожем [ПОРТ] (`IsItemPickPending`).
	if (IsItemPickPending())
	{
		UE_LOG(LogAsdaNet, Log, TEXT("→5012 подбор НЕ отправлен: прошлый 5012 без ответа %.0f мс (эталон sub_92101B) char(%d,%d) item(%d,%d)"),
			MoveClockMs() - ItemPickStampMs, CharTile.X, CharTile.Y, ItemTile.X, ItemTile.Y);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 5012 подбор НЕ отправлен: прошлый 5012 без ответа %.0f мс, перс тайл(%d,%d) предмет тайл(%d,%d)"),
			MoveClockMs() - ItemPickStampMs, CharTile.X, CharTile.Y, ItemTile.X, ItemTile.Y);
		return false;
	}
	// ★ 09.10.2026 (автолопата): перехват самотеста (`asda.Fish.AnimSelfTest`, раздел F) — тот же приём, что у `SendMove4006`:
	//   при перехватчике сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒ порядок прежний.
	const bool bStand = IsStandCapture((uint16)OP_CHAR_ITEM_PICKING);   // перехватчик самотеста стоит
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) return false;
		// ★ Гейт «души» (SoulState==1): даже подбор с земли у сервера под `sub_52EEA0` (GameServer.c:19829).
		if (FAsdaOutgoingGate::Block(this, (uint16)OP_CHAR_ITEM_PICKING, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }
	}
	uint8 Payload[8];
	auto W16 = [&Payload](int32 O, int32 V) { AsdaBytes::WrI16(&Payload[O], V); };
	W16(0, CharTile.X); W16(2, CharTile.Y); W16(4, ItemTile.X); W16(6, ItemTile.Y);
	if (bStand)
	{
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
		Cap((uint16)OP_CHAR_ITEM_PICKING, Payload, 8, false, TEXT('S'));
		bItemPickPending = true;                // как после настоящей отправки
		ItemPickStampMs = MoveClockMs();        // ★ 09.10.2026: метка для сторожа [ПОРТ]
		UE_LOG(LogAsdaNet, Log, TEXT("→5012 подбор (перехват самотеста): char(%d,%d) item(%d,%d)"), CharTile.X, CharTile.Y, ItemTile.X, ItemTile.Y);
		return true;
	}
	uint8 Md5Dig[16]; { FMD5 M; M.Update(Payload, 8); M.Final(Md5Dig); }
	uint8 Body[32];
	FillAntiReplaySecret(Body);                  // [0..7] токен (QPC-маска, как 20000/4026)
	FMemory::Memcpy(Body + 8, Md5Dig, 16);       // [8..23] MD5(payload 8)
	FMemory::Memcpy(Body + 24, Payload, 8);      // [24..31] тайлы
	const bool bOk = ConnSendSecret((uint16)OP_CHAR_ITEM_PICKING, Body, sizeof(Body));
	// ★ 09.10.2026 (автолопата): эталон после отправки ставит «подбор в пути» (`sub_927560(g_pUIManager, 1)`, 0x9FCFB0).
	//   [ПОРТ] при неудачной отправке не ставим: ответа не будет, а флаг держал бы цикл автолопаты (как у бита хода 4006).
	if (bOk) { bItemPickPending = true; ItemPickStampMs = MoveClockMs(); }   // ★ 09.10.2026: метка для сторожа [ПОРТ]
	UE_LOG(LogAsdaNet, Log, TEXT("→5012 подбор: char(%d,%d) item(%d,%d) -> %s"),
		CharTile.X, CharTile.Y, ItemTile.X, ItemTile.Y, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 5012 подбор добычи: перс тайл(%d,%d) предмет тайл(%d,%d) -> %s"),
		CharTile.X, CharTile.Y, ItemTile.X, ItemTile.Y, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 25.10.2026 (присед, AsdaPlayerCharacter_Move.cpp): отметка «сервер получил статус хода N своего персонажа» — по ней смена
//   приседа не шлёт повторный/частый 4006 (ключ asda.Crouch.NetCoalesce).
void AsdaCrouchNote4006Sent(const AAsdaPlayerCharacter* Me, int32 Status);

bool UAsdaNetSubsystem::SendMove4006(float CurX, float CurZ, float DstX, float DstZ, int32 TgtRenderId)
{
	// ★ 27.09.2026: узкий/полный перехват самотеста (`asda.Move.SelfTest4007`) — тот же приём, что у
	//   `SendStop4008`: при перехватчике сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒
	//   порядок проверок прежний: сокет → гейт перехода.
	const bool bStand = IsStandCapture((uint16)4006);
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) return false;
		if (FAsdaOutgoingGate::Block(this, (uint16)4006, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	}
	uint8 Body[43];
	// ★ поле msgBody[1..2] = own in-world userNum (SelfUserNum из 4003), НЕ charId&0xFFFF (перехват оригинала).
	// ★ 24.10.2026 [ПОРТ] статус хода @+3: эталон — всегда 2; в приседе своего персонажа — 4 (ключ asda.Crouch.NetStatus).
	//   Сток сервера принимает 2/4/6/8 (пересчёт скорости, рассылка тела соседям в 4007), патч P80 делит скорость на 2.
	uint16 MoveStatus = 2;
	const AAsdaPlayerCharacter* MoveSelf = nullptr;   // свой персонаж (для отметки отправленного статуса)
	if (const UAsdaWorldSubsystem* Ws = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(Ws->LocalPlayerActor.Get()))
		{
			MoveSelf = Me;
			MoveStatus = (uint16)Me->GetMoveStatusFor4006();
			Me->LastSent4006TargetRid = TgtRenderId;   // ★ ревизия 24.10: досылка статуса приседа сохраняет цель хода
		}
	}
	FillMoveBody(Body, 0, SelfUserNum, MoveStatus, CurX, CurZ, DstX, DstZ, (uint16)TgtRenderId);
	// ★★★ 27.09.2026: эталон `Combat_SendNormalAttack@0x9F8CA0` (0x9F8D2E…0x9F8D64) ДО отправки делает три
	//   записи: `g_bActionPending = 1`, `sub_929D83(&game, 2)` (бит 2 — «запрос хода в пути») и
	//   `g_flNetTickInterval = timeGetTime()` (метка для подгонки скорости по гранту 4007). Две последние
	//   порт раньше не вёл вовсе. Порядок (флаги ДО `CNetwork_Send`) сохранён: результат отправки эталон
	//   не проверяет; у нас флаги ставятся только при успехе — неудачная отправка ответа не получит, а
	//   взведённый бит держал бы ввод 10 с (исправление, помечено).
	bool bOk = false;
	if (bStand)
	{
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
		Cap((uint16)4006, Body, (int32)sizeof(Body), true, TEXT('R'));
		bOk = true;
	}
	else
	{
		// ★ 15.10.2026 (ревизия сети НИЗК-10): поставленный к дедлайну удар ушёл бы ПОСЛЕ хода, а сервер атаку «на бегу» молча
		//   отбрасывает (гейт движения, см. `AAsdaPlayerController::AttackTick`). У оригинала удар уходит в своём кадре, раньше
		//   хода, — у нас такого удара уже не будет: снимаем его, как при заявке навыка.
		CancelPending4028(TEXT("ход 4006"));
		bOk = Conn->SendPacket((uint16)4006, Body, sizeof(Body));
	}
	if (bOk)
	{
		bActionPending = true;
		bMoveRequestInFlight = true;
		MoveRequestStampMs = MoveClockMs();
		if (MoveSelf) { AsdaCrouchNote4006Sent(MoveSelf, (int32)MoveStatus); }   // ★ 25.10.2026: статус у сервера (присед)
		if (MoveSelf) { AsdaCrouchStand::Note4006Sent(MoveSelf); }               // ★ 05.11.2026: ход несёт присед сам (код стоя — заново)
		// ★ 25.10.2026 (фантомный урон, `asda.Combat.OwnHitsServerCommit`): ход НЕ к цели бросает серию обычной атаки — её
		//   неприменённые значения очереди сервер уже не спишет (следующий удар после хода — новый 4026).
		if (TgtRenderId < 0)
		{
			if (UAsdaAttackRhythmSubsystem* RhM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { RhM->NoteFreeMove(); }
		}
	}
	// Журнал боя: ход К ЦЕЛИ (tgt ≥ 0) — событие боя (бег к мобу/игроку); обычный ход по земле — только уровень 2.
	if (TgtRenderId >= 0)
	{
		ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4006 ход к цели rid=%d: cur=(%.0f,%.0f) dst=(%.0f,%.0f) userNum=%d%s -> %s"),
			TgtRenderId, CurX, CurZ, DstX, DstZ, (int32)SelfUserNum, bStand ? TEXT(" [перехват самотеста]") : TEXT(""), bOk ? TEXT("OK") : TEXT("FAIL"));
	}
	else
	{
		ASDA_CTRACE_V(EAsdaCT::Send, TEXT("ОТПР 4006 ход: cur=(%.0f,%.0f) dst=(%.0f,%.0f) userNum=%d%s -> %s"),
			CurX, CurZ, DstX, DstZ, (int32)SelfUserNum, bStand ? TEXT(" [перехват самотеста]") : TEXT(""), bOk ? TEXT("OK") : TEXT("FAIL"));
	}
	if (bStand)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("→4006 move [ПЕРЕХВАТ САМОТЕСТА]: cur=(%.0f,%.0f) dst=(%.0f,%.0f) userNum=%u tgt=%d"),
			CurX, CurZ, DstX, DstZ, SelfUserNum, TgtRenderId);
		return true;
	}
	UE_LOG(LogAsdaNet, Log, TEXT("→4006 move: cur=(%.0f,%.0f) dst=(%.0f,%.0f) userNum=%u tgt=%d -> %s"),
		CurX, CurZ, DstX, DstZ, SelfUserNum, TgtRenderId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★★★ 4008 CharStop — ЕДИНСТВЕННЫЙ пакет, снимающий серверный флаг движения `CUser+51`.
//   `CharStop@0x545C70` (GameServer.c:124545): `if (sub_523ED0(userMng, userNum))
//     sub_523EA0(userMng, userNum, "CharStop()")` → `Return_UserMoveStack@0x542520` → флаг = 0.
//   `MoveSyncProcess@0x5427D0` сам по естественному концу пути флаг НЕ снимает (GameServer.c:123458-123483:
//   при `duration <= elapsed` и пустой очередной точке ветка ПУСТАЯ), поэтому без 4008 флаг висит вечно.
//   ⚠ Сервер отбрасывает 4008 ДО снятия флага, если: тело != 27 б; `body[1..2] != свой userNum`;
//   `body[3..4] != 0` (иначе лог «CharStop() iCharMotionStatus != MOVE_STATUSIDLE»). Поэтому Flag=0
//   и SelfUserNum — обязательны (эталон `Net_SendActionPacket@0x9F9C00`, network.c:5006-5013:
//   msg[0]=0, msg[1..2]=свой userNum, msg[3..4]=0, cur==dst==текущая позиция).
// ★★★ ГЕЙТ `bActionPending` — ЕДИНСТВЕННЫЙ, ОБХОДА У ЭТАЛОНА НЕТ (T2, 2026-08-11).
//   `Net_SendActionPacket@0x9F9C00` (network.c:5016-5028) целиком выглядит так:
//     `if (g_bActionPending) { g_bActionPending = 0; …MD5…; CNetwork_Send(…, 43, OP_CHAR_STOP, 1,1,1,0,0); }`
//   — тела отправки ВНЕ гейта не существует, «принудительного» пути нет ни в одном из вызывающих
//   мест (network.c:5422 и :5575 даже дублируют проверку `if (g_bActionPending)` СНАРУЖИ).
//   Взводит флаг ровно один отправитель — `Combat_SendNormalAttack@0x9F8CA0` (combat_pvp.c:1511),
//   тот самый, что шлёт **4006** (`CNetwork_Send(…, 4006, …)`, combat_pvp.c:1530); ещё одно взведение
//   локальное — `CChar_SetState` case 15 (entity_char.c:16966). Приход 4007 флаг НЕ трогает
//   (все 6 вхождений `g_bActionPending` в эталоне проверены — см. также правку в
//   OP_RECV_PACKET_ENTITY_MOVE ниже).
//   ⇒ Параметр `bForce` порта был отступлением: он слал стоп там, где сервер флаг движения и не
//   ставил (лог пестрел «→4008 stop … [принудительно]»). Убран. Стоп по-прежнему уходит везде, где
//   он нужен, потому что каждый из этих путей ПРЕДВАРЯЕТСЯ отправкой 4006:
//     · конец маршрута / отпускание клавиш — `StopMoveAndNotify` после `SendMove4006`;
//     · вход в бой перед 4026 — после подхода (те же 4006 по путевым точкам);
//     · заявка навыка — `UAsdaSkillUseSubsystem` уже гейтит по `bActionPending` явно.
//   Если 4006 не отправлялся, серверный флаг `CUser+51` не взведён и стоп не нужен — ровно эталон.
//   ★ Паддинг: эталон шлёт 4008 с `a6 = 1`; у `AsdaConnection::SendPacket` это дефолт `bPad = true`.
bool UAsdaNetSubsystem::SendStop4008(float CurX, float CurZ)
{
	// ★ 26.09.2026: узкий перехват самотеста (`ArmSendCapture(…, 4008)`, самотест `asda.Net.SelfTestStop4008`) —
	//   тот же приём, что у `SendSecured`: при перехватчике сокета нет, гейты живой сессии пропускаются, а
	//   эталонный гейт `bActionPending` остаётся. В игре перехватчик ПУСТ ⇒ порядок прежний: сокет → гейты.
	const bool bStand = IsStandCapture((uint16)4008);
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected() || !bActionPending) return false;   // прежний порядок проверок
		if (FAsdaOutgoingGate::Block(this, (uint16)4008, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	}
	else if (!bActionPending) { return false; }   // эталонный гейт — и при перехвате
	uint8 Body[43];
	// ★ 4008 CharStop: Flag=0 (не 2) + own userNum (перехват оригинала: T[1..2]=0, T[3..4]=0).
	FillMoveBody(Body, 0, SelfUserNum, 0, CurX, CurZ, CurX, CurZ, (uint16)-1);
	if (bStand)
	{
		bActionPending = false;   // как у настоящей отправки: флаг гасится внутри гейта
		// Конверт 'R' — «сырой» `Conn->SendPacket` (MD5 уже внутри тела, секрета нет); пад = дефолт `bPad = true`.
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
		Cap((uint16)4008, Body, (int32)sizeof(Body), true, TEXT('R'));
		ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4008 стоп [перехват самотеста]: поз=(%.0f,%.0f) userNum=%d"), CurX, CurZ, (int32)SelfUserNum);
		// ★ 05.11.2026: присед стоя — та же отметка, что у настоящей отправки ниже.
		if (const UAsdaWorldSubsystem* Ws = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(Ws->LocalPlayerActor.Get())) { AsdaCrouchStand::Note4008Sent(Me); }
		}
		return true;
	}
	const bool bOk = Conn->SendPacket((uint16)4008, Body, sizeof(Body));
	bActionPending = false;                      // эталон: `g_bActionPending = 0` ВНУТРИ гейта
	UE_LOG(LogAsdaNet, Log, TEXT("→4008 stop: pos=(%.0f,%.0f) userNum=%u -> %s"),
		CurX, CurZ, SelfUserNum, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4008 стоп: поз=(%.0f,%.0f) userNum=%d (снимает серверный флаг движения) -> %s"),
		CurX, CurZ, (int32)SelfUserNum, bOk ? TEXT("OK") : TEXT("FAIL"));
	// ★ 05.11.2026 [ПОРТ] (присед стоя): сервер на 4008 записал статус 0 — присевший объявляет присед стоя заново (4024 132 ПОСЛЕ 4008:
	//   в ходу сток 4024 отбрасывает). Разбор — `World/AsdaCrouchStand.h`.
	if (bOk)
	{
		if (const UAsdaWorldSubsystem* Ws = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(Ws->LocalPlayerActor.Get())) { AsdaCrouchStand::Note4008Sent(Me); }
		}
	}
	return TrackSend(bOk);
}

// ★ 09.10.2026 (МЕЛК-1): «подбор 5012 в пути» со сторожем [ПОРТ] `asda.Loot.PickPendingGuardSec` (ленивое снятие при
//   чтении — как у бита 2 ниже: между чтениями флаг никому не нужен). Снятие сторожем — нештатное, пишется в лог.
bool UAsdaNetSubsystem::IsItemPickPending()
{
	if (!bItemPickPending) { return false; }
	const float GuardSec = CVarItemPickPendingGuardSec.GetValueOnGameThread();   // 0 = эталон, без сторожа
	const double WaitMs = MoveClockMs() - ItemPickStampMs;                       // сколько ждём ответа
	if (GuardSec > 0.f && WaitMs > GuardSec * 1000.0)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("[подбор] флаг «5012 в пути» снят сторожем [ПОРТ]: ответа 5013/5015 нет %.0f мс"), WaitMs);
		bItemPickPending = false;
		return false;
	}
	return true;
}

// ★★★ 27.09.2026: чтение бита 2 со сторожем эталона. `effectUpdateAttachedTargetPos@0x9954F0` (0x995962…0x995A33)
//   каждый кадр: `if (flags & 2) { if (|now − g_flNetTickInterval| > 10000.0) flags &= ~2; }` — обе ветки знака
//   сравнивают модуль разности. У нас та же проверка исполняется при чтении: между кадрами флаг никто не читает,
//   поэтому ленивое снятие даёт тот же наблюдаемый результат.
bool UAsdaNetSubsystem::IsMoveRequestInFlight()
{
	if (!bMoveRequestInFlight) { return false; }
	if (FMath::Abs(MoveClockMs() - MoveRequestStampMs) > 10000.0)
	{
		// ★ Ревизия МЕЛК 4: снятие СТОРОЖЕМ — событие нештатное (4007 потерян), в обычный лог; штатное снятие
		//   ответом остаётся на Verbose внутри `ClearMoveRequestInFlight` (иначе строка на каждый шаг).
		UE_LOG(LogAsdaNet, Log, TEXT("[ход] бит 2 снят сторожем: ответа 4007 нет %.0f мс"), FMath::Abs(MoveClockMs() - MoveRequestStampMs));
		ClearMoveRequestInFlight(TEXT("сторож 10 с — ответа 4007 нет"));
		return false;
	}
	return true;
}

// Эталон `gameFlagsClear@0xF3CC50`: `flags &= ~2`. Метку времени НЕ трогает (у эталона её пишут только 4006 и грант).
void UAsdaNetSubsystem::ClearMoveRequestInFlight(const TCHAR* Why)
{
	if (!bMoveRequestInFlight) { return; }
	bMoveRequestInFlight = false;
	UE_LOG(LogAsdaNet, Verbose, TEXT("[ход] бит 2 «запрос хода в пути» снят: %s"), Why);
}

// ★ 4008 от позиции актора: единственное место, где UE-локация превращается в координаты тела 4008
//   (обоснование — в объявлении). Гейт `bActionPending` и всё остальное — внутри `SendStop4008`.
bool UAsdaNetSubsystem::SendStop4008AtActor(const AActor* Self)
{
	// Без актора — (0,0), ровно как прежние вызывающие (их `Loc` по умолчанию был нулевым).
	const FVector2D G = Self ? AsdaCoord::UeToGame2D(Self->GetActorLocation()) : FVector2D::ZeroVector;
	if (!Self) { UE_LOG(LogAsdaNet, Warning, TEXT("→4008 stop: актора нет — позиция (0,0)")); }
	return SendStop4008((float)G.X, (float)G.Y);
}

// C→S 4054 CharRegen (25б: секрет8+MD5(режим,1)+u8 режим). Режим 0 = «готов, покажи меня» (финал входа).
bool UAsdaNetSubsystem::SendCharRegen(uint8 Mode)
{
	// ★ 31.10.2026: перехват самотеста (`asda.UI.SelfTestCampWarBattle` — окно смерти на карте 19) тем же приёмом, что у `SendSecured`:
	//   при перехватчике сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒ порядок прежний.
	const bool bStand = IsStandCapture((uint16)OP_RECV_CHAR_REGEN);
	if (bStand)
	{
		const uint8 M = Mode;
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
		Cap((uint16)OP_RECV_CHAR_REGEN, &M, 1, true, TEXT('S'));
		return true;
	}
	if (!Conn || !Conn->IsConnected()) return false;
	// ★ Гейт «души» (SoulState==1): воскрешение/реген у сервера под `sub_52EEA0` (GameServer.c:20152).
	if (FAsdaOutgoingGate::Block(this, (uint16)OP_RECV_CHAR_REGEN, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }
	uint8 Body[25];
	FMemory::Memzero(Body, sizeof(Body));
	Body[24] = Mode;
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Body + 24, 1); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_RECV_CHAR_REGEN, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("CharRegen 4054 mode=%u -> %s"), Mode, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4054 реген/воскрешение: режим=%d%s мёртв=%s -> %s"), (int32)Mode,
		Mode == 0 ? TEXT(" (готов, покажи меня)") : TEXT(""), bSelfDead ? TEXT("да") : TEXT("нет"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 4054 (регенерация/воскрешение)
}

// ═══ ★★ ЕДИНАЯ ТОЧКА ВХОДА В СОСТОЯНИЕ СМЕРТИ ═══════════════════════════════════════════════════
//   ЗАЧЕМ. Пакет 4053 приходит ТОЛЬКО при гибели «на глазах». Игрок, который умер и вышел, при
//   следующем входе получает 4053 НИКОГДА: серверный флаг CUser+79 восстанавливается из БД, и
//   единственные признаки — regenState == 200 в 4003 (`CharMake@0x543A10`) и отказы 6001 с причиной 1
//   (`sub_52BFF0` IsCheck_CharFlag) на каждое действие. Раньше порт слушал только 4053 — отсюда
//   «загружаюсь в мир и ничего: ни окна, ни ЧБ, персонаж бегает, а сервер режет каждое движение».
void UAsdaNetSubsystem::EnterSelfDeadState(const TCHAR* Reason, bool bAlreadyDead)
{
	if (bSelfDead) { return; }                 // уже мертвы — повторно не входим
	bSelfDead = true;
	bSelfDeathNoClip = bAlreadyDead;
	AsdaZeroSelfVitalsOnDeath(this, Reason);   // ★ 08.11.2026 asda.Death.ZeroVitals — HP/MP = 0 (состояние 6)
	UE_LOG(LogAsdaNet, Warning, TEXT("[смерть] %s → состояние СМЕРТИ (клип падения: %s)"),
		Reason, bAlreadyDead ? TEXT("НЕ играем — вошли уже мёртвыми") : TEXT("играем"));
	ASDA_CTRACE(EAsdaCT::Death, TEXT("СВОЯ смерть: userNum=%d причина «%s», клип падения %s → OnCharDeath"),
		(int32)SelfUserNum, Reason ? Reason : TEXT("?"), bAlreadyDead ? TEXT("НЕ играем (вошли мёртвыми)") : TEXT("играем"));
	OnCharDeath.Broadcast((int32)SelfUserNum, -1);
}

// ★ C→S 6093 CharResurrectionConfirm (35 б: секрет8 + MD5(payload,11) + payload 11).
//   ЭТАЛОН netSendPkt6093_Target@0xF47090 (network.c:43307) — раскладка payload 1:1:
//     [0..1]  i16 мой userIdx (эталон Target_GetSelectedUserIdx(localPlayer) = свой же id)
//     [2..5]  i32 мой userIdx (дублируется дword-ом)
//     [6..7]  i16 userNum ИНИЦИАТОРА воскрешения (кто применил скилл/предмет)
//     [8]     u8  Type для CharRegenSelfPosition: 0 = полное HP/MP, 2 = процент, 3 = 39 %
//     [9..10] i16 процент HP/MP (для Type 2; иначе −1)
//   Сервер Recv_CharResurrectionConfirm@0x52B0E0 требует, чтобы отправитель был МЁРТВ (sub_4CB030),
//   и зовёт CharRegenSelfPosition(mgr, a2=инициатор, Type, a4=я, a5=процент) → воскрешает МЕНЯ.
bool UAsdaNetSubsystem::SendResurrectConfirm6093(uint8 Type, int32 CasterUserIdx, int32 Percent)
{
	if (!Conn || !Conn->IsConnected()) return false;
	if (FAsdaOutgoingGate::Block(this, (uint16)OP_RECV_CHAR_RESURRECTION_CONFIRM, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	uint8 Body[35];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* P = Body + 24;                                   // payload 11 б
	const int16  Me     = (int16)SelfUserNum;
	const int32  MeDw   = (int32)SelfUserNum;
	const int16  Caster = (int16)CasterUserIdx;
	const int16  Pct    = (int16)Percent;
	FMemory::Memcpy(P + 0, &Me, 2);
	FMemory::Memcpy(P + 2, &MeDw, 4);
	FMemory::Memcpy(P + 6, &Caster, 2);
	P[8] = Type;
	FMemory::Memcpy(P + 9, &Pct, 2);
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(P, 11); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_RECV_CHAR_RESURRECTION_CONFIRM, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("6093 подтверждение воскрешения: тип=%u инициатор=%d процент=%d -> %s"),
		Type, CasterUserIdx, Percent, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 6093 подтверждение воскрешения: я userNum=%d инициатор userIdx=%d тип=%d процент=%d -> %s"),
		(int32)SelfUserNum, CasterUserIdx, (int32)Type, Percent, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 6093 (подтверждение воскрешения)
}

// ★ Атака моба (4026). Формат снят с эталона netSabzil_SendActionRaw@0x9FA180.
bool UAsdaNetSubsystem::SendAttackMob4026(int32 TargetRenderId, float TgtX, float TgtY)
{
	// ★ 15.10.2026 (ритм атаки): перехват стенда/самотеста — как у `SendSecured`: сокет и гейты живой сессии пропускаются,
	//   тело собирается тем же путём. В игре перехватчик пуст — порядок прежний.
	const bool bStand = IsStandCapture((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK);
	if (TargetRenderId < 0) { ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4026 НЕ отправлен: rid цели %d < 0"), TargetRenderId); return false; }
	if (!bStand && (!Conn || !Conn->IsConnected())) { ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4026 НЕ отправлен: нет соединения (rid=%d)"), TargetRenderId); return false; }
	// ★ Гейт «души» (SoulState==1): бой у сервера под `sub_52EEA0` (GameServer.c:20120).
	// ★ Гейт GM-«скрыться»: под `CUser+0x3BC3B == 300` сервер молча выбрасывает 4026
	//   (`Send_CharNormalMonsterAttack`, case 0xFBA). Страховка на проводе — основная точка стоит
	//   в `AAsdaPlayerController::AttackTick`, чтобы не играть и замах.
	if (!bStand && FAsdaOutgoingGate::Block(this, (uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK, EAsdaGate::Soul | EAsdaGate::GmHide | EAsdaGate::TransferWide))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4026 НЕ отправлен: шлюз исходящих (душа/GM-скрытие/переход), rid=%d"), TargetRenderId);
		return false;
	}
	// ★ 05.11.2026 (stun-attack) [ИСПРАВЛЕНИЕ]: под «не может действовать» (0/1/3/4/8/14) вход в бой не уходит — тот же гейт, что у 4028,
	//   и тот же выход (отказ 4028: покой, замах гаснет, автоатака снята). Эталон 4026 шлёт (`netSabzil_SendAction` гейта не имеет) и гасит
	//   замах на конце первого шага; сервер (до P107) заявку принимает и кладёт очередь урона — применит её после снятия. Ключ
	//   `asda.Combat.ControlStopsAttack 0` — прежнее.
	if (CVarAsdaControlStopsAttack.GetValueOnGameThread() != 0)
	{
		if (UGameInstance* GIg = GetGameInstance())
		{
			if (UAsdaEffectsSubsystem* Fx = GIg->GetSubsystem<UAsdaEffectsSubsystem>(); Fx && Fx->HasControlState(Fx->SelfUserIdx()))
			{
				UE_LOG(LogAsdaNet, Log, TEXT("4026 не отправлен: игрок обездвижен управляющим состоянием (состояния 0/1/3/4/8/14) [ИСПРАВЛЕНИЕ 05.11]"));
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4026 НЕ отправлен: игрок обездвижен (состояния 0/1/3/4/8/14), rid=%d → OnCombatRefused(4028)"), TargetRenderId);
				OnCombatRefused.Broadcast(SelfUserNum, /*Sub=*/0, /*Opcode=*/4028);
				return false;
			}
		}
	}
	uint8 Body[42];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* P = Body + 24;                                   // payload 18б
	const uint16 Action = (uint16)TargetRenderId;           // actionType = renderId цели
	FMemory::Memcpy(P + 0, &Action, 2);
	FMemory::Memcpy(P + 2, &TgtX, 4);
	FMemory::Memcpy(P + 6, &TgtY, 4);
	FMemory::Memcpy(P + 10, &TgtX, 4);                      // дубль координат (эталон)
	FMemory::Memcpy(P + 14, &TgtY, 4);
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(P, 18); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	if (bStand)
	{
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
		Cap((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK, Body, (int32)sizeof(Body), true, TEXT('R'));
		if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSent4026(/*bMobReply=*/true, TargetRenderId); }
		ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4026 атака моба [перехват самотеста]: rid=%d action=%d поз=(%.0f,%.0f)"),
			TargetRenderId, (int32)Action, TgtX, TgtY);
		return true;
	}
	const bool bOk = ConnSendSecret((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK, Body, sizeof(Body));
	// ★ 15.10.2026: начало серии у сервера (`sub_45FCD0`: шаг 0, удар принят всегда) — от него считается первый 4028.
	if (bOk) { if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSent4026(/*bMobReply=*/true, TargetRenderId); } }
	// Hex-дамп для байт-сверки с перехватом оригинала (netcap_original.log).
	FString Hex; for (int32 i = 0; i < 42; ++i) { Hex += FString::Printf(TEXT("%02x "), Body[i]); }
	UE_LOG(LogAsdaNet, Log, TEXT("Атака 4026: renderId=%d поз=(%.0f,%.0f) -> %s | %s"),
		TargetRenderId, TgtX, TgtY, bOk ? TEXT("OK") : TEXT("FAIL"), *Hex);
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4026 атака моба (начало серии): rid=%d action(u16)=%d поз цели=(%.0f,%.0f) userNum=%d -> %s"),
		TargetRenderId, (int32)Action, TgtX, TgtY, (int32)SelfUserNum, bOk ? TEXT("OK") : TEXT("FAIL"));
#if ASDA_COMBAT_TRACE
	AsdaCombatTrace::NoteEngaged(TargetRenderId);   // журнал боя: HP этого моба — на уровне 1
#endif
	return TrackSend(bOk);
}

// ★★★ 17.10.2026 (Ф4) PvP 4201 / 4203 / 4206 — разбор у объявлений (`AsdaNetSubsystem.h`).
bool UAsdaNetSubsystem::SendPvpAttackBody(uint16 Op, int32 TargetUserIdx, float SelfX, float SelfZ)
{
	uint8 P[18];
	FMemory::Memzero(P, sizeof(P));
	const int16 Tgt = (int16)TargetUserIdx;   // i16 userIdx цели (не charId)
	FMemory::Memcpy(P + 0, &Tgt, 2);
	FMemory::Memcpy(P + 2, &SelfX, 4);
	FMemory::Memcpy(P + 6, &SelfZ, 4);
	FMemory::Memcpy(P + 10, &SelfX, 4);       // дубль координат (эталон)
	FMemory::Memcpy(P + 14, &SelfZ, 4);
	return SendSecured(Op, P, (int32)sizeof(P));
}

bool UAsdaNetSubsystem::SendAttackPlayer4201(int32 TargetUserIdx, float SelfX, float SelfZ)
{
	const uint16 Op = (uint16)OP_RECV_USER_ATTACK_TO_PLAYER;
	if (TargetUserIdx < 0) { return false; }
	// [ОРИГИНАЛ] `+209060 == 1` — флаг PvP-режима своего персонажа (4206), иначе боевая стойка и пакета нет.
	if (!IsSelfPvpOn())
	{
		UE_LOG(LogAsdaNet, Log, TEXT("4201 не отправлен: PvP-режим своего персонажа выключен (эталонный гейт [160])"));
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4201 НЕ отправлен: PvP-режим своего персонажа выключен (гейт [160]), цель userIdx=%d"), TargetUserIdx);
		return false;
	}
	if (!IsStandCapture(Op) && FAsdaOutgoingGate::Block(this, Op, EAsdaGate::GmHide))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4201 НЕ отправлен: шлюз GM-скрытия, цель userIdx=%d"), TargetUserIdx);
		return false;
	}
	const bool bOk = SendPvpAttackBody(Op, TargetUserIdx, SelfX, SelfZ);
	// [ПОРТ] Серверные часы атаки: NpcServer ведёт PvP-удары той же проверкой длительности, что 4028 (`sub_45FF80`, ТЗ §Ф4) —
	//   4201 открывает серию (как 4026), 4203 — её удары. ★ 25.10.2026: ответ PvP — 4202/4204, в сверку ответов моба не встаёт.
	if (bOk) { if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSent4026(/*bMobReply=*/false); } }
	UE_LOG(LogAsdaNet, Log, TEXT("→4201 атака игрока: цель userIdx=%d поз=(%.0f,%.0f) -> %s"), TargetUserIdx, SelfX, SelfZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4201 PvP-атака (начало серии): цель userIdx=%d своя поз=(%.0f,%.0f) -> %s"), TargetUserIdx, SelfX, SelfZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

bool UAsdaNetSubsystem::SendAttackedPlayer4203(int32 TargetUserIdx, float SelfX, float SelfZ)
{
	const uint16 Op = (uint16)OP_RECV_USER_ATTACKED_TO_PLAYER;
	if (TargetUserIdx < 0) { return false; }
	// [ОРИГИНАЛ] гейт `charHasAnyEquippedItemType(LocalPlayer) || [160] != 1` → `CharFx_AutoSabzil_State(…, 20)` (network.c:5414–5419).
	if (!IsSelfPvpOn())
	{
		UE_LOG(LogAsdaNet, Log, TEXT("4203 не отправлен: PvP-режим своего персонажа выключен (эталонный гейт [160])"));
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4203 НЕ отправлен: PvP-режим своего персонажа выключен (гейт [160]), цель userIdx=%d"), TargetUserIdx);
		return false;
	}
	if (UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
	{
		if (Fx->HasControlState(Fx->SelfUserIdx()))
		{
			UE_LOG(LogAsdaNet, Log, TEXT("4203 не отправлен: игрок обездвижен (эталонный гейт charHasAnyEquippedItemType, состояния 0/1/3/4/8/14)"));
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4203 НЕ отправлен: игрок обездвижен (состояния 0/1/3/4/8/14), цель userIdx=%d"), TargetUserIdx);
			return false;
		}
	}
	if (!IsStandCapture(Op) && FAsdaOutgoingGate::Block(this, Op, EAsdaGate::GmHide))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4203 НЕ отправлен: шлюз GM-скрытия, цель userIdx=%d"), TargetUserIdx);
		return false;
	}
	// [ОРИГИНАЛ] `if (g_bActionPending) Net_SendActionPacket(…)` перед телом 4203 (network.c:5422) — стоп 4008 (гейт внутри).
	SendStop4008(SelfX, SelfZ);
	const bool bOk = SendPvpAttackBody(Op, TargetUserIdx, SelfX, SelfZ);
	if (bOk) { if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSent4028(/*bMobReply=*/false); } }   // ответ PvP — 4204
	UE_LOG(LogAsdaNet, Log, TEXT("→4203 удар по игроку: цель userIdx=%d поз=(%.0f,%.0f) -> %s"), TargetUserIdx, SelfX, SelfZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4203 PvP-удар (продолжение серии): цель userIdx=%d своя поз=(%.0f,%.0f) -> %s"), TargetUserIdx, SelfX, SelfZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

bool UAsdaNetSubsystem::SendBattleState4206(uint8 ArgByte)
{
	if (!bSelfUserNumKnown) { return false; }
	uint8 P[13];
	FMemory::Memzero(P, sizeof(P));
	const int16 Me = (int16)SelfUserNum;      // свой userIdx (`Target_GetSelectedUserIdx(локальный)`)
	const int32 CharId = SelectedCharId;      // свой charId (`sub_93035E(локальный)`; сервер сверяет с CUser+0x16B)
	FMemory::Memcpy(P + 0, &Me, 2);
	FMemory::Memcpy(P + 2, &CharId, 4);
	P[6] = ArgByte; P[7] = ArgByte;           // `LOBYTE(msg[3]) = HIBYTE(msg[3]) = a3`
	const bool bOk = SendSecured((uint16)OP_RECV_USER_BATTLE_STATE, P, (int32)sizeof(P));
	UE_LOG(LogAsdaNet, Log, TEXT("→4206 смена режима боя: userIdx=%d charId=%d байт=%u -> %s"), (int32)SelfUserNum, CharId, (uint32)ArgByte,
		bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4206 режим PvP: userIdx=%d charId=%d байт=%d -> %s"), (int32)SelfUserNum, CharId, (int32)ArgByte,
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

bool UAsdaNetSubsystem::TogglePvpMode(const TCHAR* Why)
{
	if (!bSelfUserNumKnown) { return false; }
	FAsdaPvpState& St = PvpStates.FindOrAdd((int32)SelfUserNum);
	UE_LOG(LogAsdaNet, Log, TEXT("[PvP] переключатель режима (%s): сейчас %s, байт [88]=%u"), Why ? Why : TEXT("?"),
		St.bOn ? TEXT("ВКЛ") : TEXT("выкл"), (uint32)St.ArgByte);
	ASDA_CTRACE(EAsdaCT::Pvp, TEXT("переключатель PvP-режима (%s): сейчас %s → %s, байт [88]=%d"), Why ? Why : TEXT("?"),
		St.bOn ? TEXT("ВКЛ") : TEXT("выкл"), St.bOn ? TEXT("выкл (локально, до ответа)") : TEXT("заявка на ВКЛ"), (int32)St.ArgByte);
	if (St.bOn)
	{
		// [ОРИГИНАЛ] ui_core.c:32299–32311: `[160] = 0`, нотис 108132 (белый), `[88] = 0`, 4206, иконка гаснет — ДО ответа сервера.
		//   Мини-статус и подсистема навыков слушают `OnPvpMode`: локальный отбой проходит тем же делегатом (нотис и иконка — там).
		//   ⚠ [ОРИГИНАЛ] Ответ сервера (4206 команда 1, режим 0) даст 108132 ещё раз — у эталона так же (две строки).
		St.bOn = false;
		St.ArgByte = 0;
		// ★ 18.10.2026 (ревизия Ф4, мелочь 7): признак «локальный отбой» на время рассылки — эталон пишет 108132 только в чат
		//   (`CChat_AddTokenizedLines`, ui_core.c:32302), без пары с подсказкой (у ответа сервера пара есть, network.c:44113–44115).
		bPvpModeLocalBroadcast = true;
		OnPvpMode.Broadcast((int32)SelfUserNum, false, 0);
		bPvpModeLocalBroadcast = false;
		return SendBattleState4206(0);
	}
	return SendBattleState4206(St.ArgByte);   // ui_core.c:32315–32319 — включение: только 4206 с текущим `[88]`
}

// ★ Продолжение автоатаки (4028, эталон netSabzil_SendTargetAction4028@0x9FB6D0; ответ — 4027).
//   Перехват оригинала (netcap 2026-07-29): 48б = секрет8 + MD5-16 + payload 24б:
//   dword targetRid (u16+нули), dword 0, float tgtX, float tgtZ, дубль X/Z.
bool UAsdaNetSubsystem::SendAttackNext4028(int32 TargetRenderId, float TgtX, float TgtY)
{
	return SendAttackNext4028Impl(TargetRenderId, TgtX, TgtY, 0.0);
}

bool UAsdaNetSubsystem::SendAttackNext4028At(int32 TargetRenderId, float TgtX, float TgtY, double DeadlineSec)
{
	return SendAttackNext4028Impl(TargetRenderId, TgtX, TgtY, DeadlineSec);
}

// ★★★ 15.10.2026: итог поставленного 4028. Ушёл — часам фактическое время ухода (интервал до следующего считается от
//   него), перехватчику стенда — тело; не ушёл — часам «постановки нет» (следующая точка замаха попробует снова).
void UAsdaNetSubsystem::PollPending4028()
{
	if (!Pending4028Result.IsValid()) { return; }
	const int32 St = Pending4028Result->State.load();
	if (St == 0) { return; }
	const double SentAt = Pending4028Result->SentAtSec.load();
	const bool bStand = bPending4028Stand;
	TArray<uint8> Body = MoveTemp(Pending4028Body);
	const int32 Rid = Pending4028Rid;
	Pending4028Result.Reset(); Pending4028Ticket = 0; bPending4028Stand = false; Pending4028Rid = -1;
	UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr;
	if (St == 1)
	{
		if (bStand && IsStandCapture((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK_NEXT))
		{
			LastTimedSendAtS = SentAt;
			const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
			Cap((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK_NEXT, Body.GetData(), Body.Num(), true, TEXT('R'));
			LastTimedSendAtS = 0.0;
		}
		if (Rh) { Rh->NoteSent4028At(SentAt, Rid); }
		UE_LOG(LogAsdaNet, Log, TEXT("Атака-продолжение 4028 (точно к дедлайну): renderId=%d -> OK"), Rid);
		ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4028 удар (к дедлайну) УШЁЛ: rid=%d момент ухода %.3f с%s"), Rid, SentAt,
			bStand ? TEXT(" [перехват самотеста]") : TEXT(""));
		if (!bStand) { TrackSend(true); }
	}
	else
	{
		if (Rh) { Rh->ClearPending4028(); }
		UE_LOG(LogAsdaNet, Log, TEXT("Атака-продолжение 4028 (точно к дедлайну): renderId=%d -> %s"), Rid,
			St == 3 ? TEXT("снят (соединения нет)") : TEXT("FAIL"));
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028 удар (к дедлайну) НЕ ушёл: rid=%d итог=%s"), Rid,
			St == 3 ? TEXT("снят до ухода (отмена/соединения нет)") : TEXT("FAIL отправки"));
		if (!bStand && St == 2) { TrackSend(false); }
	}
}

void UAsdaNetSubsystem::CancelPending4028(const TCHAR* Why)
{
	if (!Pending4028Result.IsValid()) { return; }
	if (bPending4028Stand) { FAsdaTimedSend::Get().Cancel(Pending4028Ticket); }
	else if (Conn) { Conn->CancelTimed(Pending4028Ticket); }
	else { FAsdaTimedSend::Get().Cancel(Pending4028Ticket); }
	// Снятие дожидается идущей отправки: State уже окончательный (1 — успел уйти, его учтёт опрос).
	if (Pending4028Result->State.load() == 0) { Pending4028Result->State = 3; }
	UE_LOG(LogAsdaNet, Log, TEXT("4028, поставленный к дедлайну, снят: %s"), Why ? Why : TEXT("?"));
	ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028, поставленный к дедлайну (rid=%d), снимается: %s"), Pending4028Rid, Why ? Why : TEXT("?"));
	PollPending4028();
}

bool UAsdaNetSubsystem::SendAttackNext4028Impl(int32 TargetRenderId, float TgtX, float TgtY, double DeadlineSec)
{
	// ★ 15.10.2026 (ритм атаки): перехват стенда/самотеста — см. `SendAttackMob4026`.
	const bool bStand = IsStandCapture((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK_NEXT);
	if (TargetRenderId < 0) { ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028 НЕ отправлен: rid цели %d < 0"), TargetRenderId); return false; }
	if (!bStand && (!Conn || !Conn->IsConnected())) { ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028 НЕ отправлен: нет соединения (rid=%d)"), TargetRenderId); return false; }
	// ★ Гейт «души» (SoulState==1): бой у сервера под `sub_52EEA0` (GameServer.c:20126).
	// ★ Гейт GM-«скрыться»: 4028 сервер под скрытием тоже глотает (case 0xFBC).
	if (!bStand && FAsdaOutgoingGate::Block(this, (uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK_NEXT, EAsdaGate::Soul | EAsdaGate::GmHide | EAsdaGate::TransferWide))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028 НЕ отправлен: шлюз исходящих (душа/GM-скрытие/переход), rid=%d"), TargetRenderId);
		return false;
	}
	// ★★★ ЭТАЛОННЫЙ ГЕЙТ ПРОДОЛЖЕНИЯ БОЯ. `netSabzil_SendTargetAction4028@0x9FB6D0`
	//   (`RE/etalon/network.c:5416`) ПЕРЕД отправкой проверяет:
	//       if ( charHasAnyEquippedItemType(LocalPlayer) || LocalPlayer[209060] != 1 )
	//           CharFx_AutoSabzil_State(LocalPlayer, …, 20);   // выйти в боевой покой
	//       else { …собрать и отправить пакет… }
	//   ⚠ Имя функции ВРЁТ: она ищет в списке состояний персонажа типы 0,1,3,4,8,14 — лёд,
	//   окаменение, оглушение, страх, сон, абсолютный стан (сверено с таблицей NpcServer).
	//   Смысл: обездвиженный игрок НЕ шлёт заявку на следующий замах. У нас гейта не было — под
	//   станом мы продолжали слать 4028, а серверный античит такие пакеты считает нарушениями.
	//   Тот же гейт в оригинале стоит и перед 4203 (`netSabzil_SendTargetAction4203@0x9FAFC0`);
	//   этого пакета в порту пока нет — появится, поставить сюда же.
	// ★★★ ИСПРАВЛЕНО 2026-09-02 (К-1, дефект ПОРТА). БЫЛО: `Fx->HasControlDebuff(...)` — второй
	//   порт той же эталонной функции, сверявший ТОЛЬКО id состояния и игнорировавший его ТИП.
	//   Эталон ищет через `CList_FindByTypeSub@0xF36D90`, а та сверяет И `node+18` (id), И
	//   `node+20` (type == 0). В одном списке лежат ещё и предметные усилители (type 2): 28 записей
	//   `special_itemlist` с SubType 0/1/3/4 — зелья физ. атаки, зелья защиты, ускорение. Выпил
	//   зелье → id совпал → клиент считал себя обездвиженным и ПЕРЕСТАВАЛ слать 4028
	//   («то атакует, то не атакует»). СТАЛО: `HasControlState` — единственный порт эталона,
	//   сверяющий пару (id, type = 0). Дубль `HasControlDebuff` снят целиком.
	if (UGameInstance* GIg = GetGameInstance())
	{
		if (UAsdaEffectsSubsystem* Fx = GIg->GetSubsystem<UAsdaEffectsSubsystem>())
		{
			if (Fx->HasControlState(Fx->SelfUserIdx()))
			{
				UE_LOG(LogAsdaNet, Log,
					TEXT("4028 не отправлен: игрок обездвижен управляющим состоянием (эталонный гейт ")
					TEXT("charHasAnyEquippedItemType, состояния 0/1/3/4/8/14)"));
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028 НЕ отправлен: игрок обездвижен (состояния 0/1/3/4/8/14), rid=%d → OnCombatRefused(4028)"), TargetRenderId);
				OnCombatRefused.Broadcast(SelfUserNum, /*Sub=*/0, /*Opcode=*/4028);
				return false;
			}
		}
	}
	// ★★★ 15.10.2026 СЕРВЕРНЫЕ ЧАСЫ АТАКИ — последний барьер перед проводом (`UAsdaAttackRhythmSubsystem`).
	//   NpcServer молча выбрасывает удар, пришедший раньше своей длительности (Combo.lst/Skill.lst, баффы), — урона
	//   нет, счётчик нарушений растёт. Ритм обязан держать вызывающий (`AAsdaPlayerController::OnSwingCycleCompleted`
	//   ждёт часов); сюда ранний вызов не должен доходить вовсе — дошёл, значит ошибка порта: не шлём и пишем ПРЕДУПРЕЖДЕНИЕ.
	const double NowS = FPlatformTime::Seconds();
	const double SendAtS = (DeadlineSec > NowS) ? DeadlineSec : NowS;   // момент ухода, который проверяет барьер
	if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr)
	{
		if (HasPending4028())
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("4028 НЕ отправлен: предыдущий ещё поставлен к дедлайну"));
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028 НЕ отправлен: предыдущий (rid=%d) ещё поставлен к дедлайну; новый rid=%d"), Pending4028Rid, TargetRenderId);
			return false;
		}
		const double Earliest = Rh->EarliestNext4028S();
		if (Earliest > 0.0 && SendAtS < Earliest)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("4028 НЕ отправлен: раньше серверной длительности на %.1f мс (нужно %d мс + запас от прошлого удара)"),
				(Earliest - SendAtS) * 1000.0, Rh->GetRequiredMs());
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028 НЕ отправлен: раньше серверных часов на %.1f мс (нужно %d мс), rid=%d"),
				(Earliest - SendAtS) * 1000.0, Rh->GetRequiredMs(), TargetRenderId);
			return false;
		}
	}
	uint8 Body[48];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* P = Body + 24;
	const uint32 Rid = (uint32)(uint16)TargetRenderId;
	FMemory::Memcpy(P + 0,  &Rid, 4);
	// P+4..7 = 0 (перехват: нули)
	FMemory::Memcpy(P + 8,  &TgtX, 4);
	FMemory::Memcpy(P + 12, &TgtY, 4);
	FMemory::Memcpy(P + 16, &TgtX, 4);
	FMemory::Memcpy(P + 20, &TgtY, 4);
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(P, 24); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	// ★★★ 15.10.2026 (СРЕДН-2): дедлайн ещё не наступил — поставить к нему на поток точной отправки.
	if (DeadlineSec > NowS + 0.0002)
	{
		Pending4028Result = MakeShared<FAsdaConnection::FTimedResult, ESPMode::ThreadSafe>();
		Pending4028Rid = TargetRenderId;
		bPending4028Stand = bStand;
		if (bStand)
		{
			// Стенд/самотест: сокета нет — поток точной отправки только отмечает момент ухода, тело перехватчику отдаст опрос.
			Pending4028Body = TArray<uint8>(Body, (int32)sizeof(Body));
			TSharedPtr<FAsdaConnection::FTimedResult, ESPMode::ThreadSafe> R = Pending4028Result;
			Pending4028Ticket = FAsdaTimedSend::Get().Schedule(DeadlineSec, [R](double FiredAt) { R->SentAtSec = FiredAt; R->State = 1; });
		}
		else
		{
			Pending4028Ticket = Conn->SendPacketAt((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK_NEXT, Body, (int32)sizeof(Body), true,
				DeadlineSec, Pending4028Result, /*bStampSecret=*/IsSecretAtSend());   // ★ ревизия сети БЛОКЕР-1: номер — при уходе
		}
		if (Pending4028Ticket == 0)
		{
			Pending4028Result.Reset(); Pending4028Body.Reset(); bPending4028Stand = false; Pending4028Rid = -1;
			UE_LOG(LogAsdaNet, Warning, TEXT("4028 НЕ поставлен к дедлайну (поток точной отправки/вето)"));
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР 4028 НЕ поставлен к дедлайну (поток точной отправки/вето), rid=%d"), TargetRenderId);
			return false;
		}
		if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NotePending4028(DeadlineSec); }
		ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4028 удар ПОСТАВЛЕН к дедлайну: rid=%d поз цели=(%.0f,%.0f) уйдёт через %.1f мс (билет %lld)%s"),
			TargetRenderId, TgtX, TgtY, (DeadlineSec - NowS) * 1000.0, (int64)Pending4028Ticket, bStand ? TEXT(" [перехват самотеста]") : TEXT(""));
		return true;
	}
	if (bStand)
	{
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
		Cap((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK_NEXT, Body, (int32)sizeof(Body), true, TEXT('R'));
		if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSent4028(/*bMobReply=*/true, TargetRenderId); }
		ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4028 удар [перехват самотеста]: rid=%d поз цели=(%.0f,%.0f)"), TargetRenderId, TgtX, TgtY);
		return true;
	}
	const bool bOk = ConnSendSecret((uint16)OP_SEND_CHAR_NORMAL_MONSTER_ATTACK_NEXT, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("Атака-продолжение 4028: renderId=%d -> %s"), TargetRenderId, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4028 удар (продолжение серии, сразу): rid=%d поз цели=(%.0f,%.0f) -> %s"),
		TargetRenderId, TgtX, TgtY, bOk ? TEXT("OK") : TEXT("FAIL"));
#if ASDA_COMBAT_TRACE
	AsdaCombatTrace::NoteEngaged(TargetRenderId);   // журнал боя: HP этого моба — на уровне 1
#endif
	if (bOk) { if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSent4028(/*bMobReply=*/true, TargetRenderId); } }
	return TrackSend(bOk);
}

// ★★★ ЗАПРОС HP/MP ЦЕЛИ-ИГРОКА (6538). ИСПРАВЛЕНО 2026-08-29 — дефект ПОРТА, из-за которого у
//   цели-игрока полоса жизни оставалась пустой.
//   БЫЛО: тело 26 б, payload = 2 байта `u16 userIdx` («как 5416»).
//   ЭТАЛОН (`network.c:30551-30677`, `ui_core.c:75929`): тело 36 б, payload 12 б —
//     charId цели (i32) · userNum цели (i16) · charId прежней цели (i32) · userNum прежней (i16).
//   ПОЧЕМУ ЭТО ЛОМАЛО HP: сервер `Recv_User_TargetHPMP@0x532EC0` берёт 12 байт с +33 и читает из
//     них ДВА ключа — `v12[0]` charId и `v12[1]` userNum. С двухбайтовым телом на месте charId
//     оказывался userNum, а на месте userNum — мусор за концом буфера ⇒ `sub_5324A0` цели не
//     находил и уходил в `LABEL_7` — ответ 6539 с подкодом 2 (наш приёмник применяет только 1 и 3).
//     Ни снимка HP, ни ЗАПИСИ В СПИСОК НАБЛЮДАТЕЛЕЙ цели (20 слотов) не происходило, поэтому
//     дальнейшие изменения HP цели тоже не приходили. Проверку MD5 сервер не роняет
//     (`sub_523F00@0x523F00` считает дайджест, но результат `memcmp` выбрасывает и всегда
//     возвращает 1) — отказ был именно по разбору полей, МОЛЧА, ровно как в памятке charid-vs-usernum.
bool UAsdaNetSubsystem::SendTargetInfoReq6538(int32 TargetCharId, int32 TargetUserNum, int32 PrevCharId, int32 PrevUserNum)
{
	if (TargetUserNum < 0) return false;
	if (FAsdaOutgoingGate::Block(this, (uint16)OP_RECV_USER_TARGET_HPMP, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	uint8 Body[36];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* P = Body + 24;
	const int32  CId  = TargetCharId;
	const uint16 UNum = (uint16)TargetUserNum;
	const int32  PCId = PrevCharId;
	const uint16 PUN  = (uint16)(int16)PrevUserNum;
	FMemory::Memcpy(P + 0,  &CId,  4);    // charId цели   (эталон a2)
	FMemory::Memcpy(P + 4,  &UNum, 2);    // userNum цели  (эталон a3)
	FMemory::Memcpy(P + 6,  &PCId, 4);    // charId прежней цели (эталон a4)
	FMemory::Memcpy(P + 10, &PUN,  2);    // userNum прежней цели (эталон a5)
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(P, 12); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	// ★ Тело собирается ДО проверки соединения, чтобы офлайн-прогон мог доказать раскладку payload
	//   фактическими байтами (без сервера пакет просто не уходит — строка «НЕ отправлен»).
	const bool bConnected = (Conn && Conn->IsConnected());
	const bool bOk = bConnected && ConnSendSecret((uint16)OP_RECV_USER_TARGET_HPMP, Body, sizeof(Body));
	// Доказательная строка: печатаем САМИ отправленные 12 байт payload — не то, что «хотели положить».
	FString Hex; for (int32 i = 0; i < 12; ++i) { Hex += FString::Printf(TEXT("%02X "), P[i]); }
	UE_LOG(LogAsdaNet, Log,
		TEXT("Инфо цели 6538: charId=%d userNum=%d (прежняя: charId=%d userNum=%d), кадр 36 б, payload 12 б=[%s] -> %s"),
		TargetCharId, TargetUserNum, PrevCharId, PrevUserNum, *Hex,
		bConnected ? (bOk ? TEXT("OK") : TEXT("FAIL")) : TEXT("НЕ отправлен (нет соединения)"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 6538 запрос HP/MP цели-игрока: charId=%d userNum=%d (прежняя charId=%d userNum=%d) -> %s"),
		TargetCharId, TargetUserNum, PrevCharId, PrevUserNum,
		bConnected ? (bOk ? TEXT("OK") : TEXT("FAIL")) : TEXT("НЕ отправлен (нет соединения)"));
	if (!bConnected) { return false; }
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 6538 (инфо цели)
}

// ★★★ 6142 (0x17FE) — ОТМЕНА БАФФА КЛИКОМ по иконке мини-статуса.
//   Эталон sendInventoryActionRequest@0xE82ED0 (network.c:30746) 1:1:
//     payload(7) = i32 charId (CClass_GetField142C(g_InventoryBag)->[+0x41C]) | u8 kind | i16 id
//     body(31)   = [0..7] buildAntiCheatToken() | [8..23] Net_Md5Digest16(g_NetMd5Salt, payload, 7)
//                  | [24..30] payload
//   kind: 0 = скилловый эффект (id = skillId), 1 = предметный/table2 (id = itemCode из слота HUD).
//   ⚠ Соулмейт-эффект (typeFlag 1) отменить НЕЛЬЗЯ — эталон оставляет clearType = −1 и не шлёт.
//
// ★★★ ИСПРАВЛЕНО 2026-09-02 (К-5, дефект ПОРТА; правка ВОЗВРАЩАЕТ к эталону — формат, размер и
//   порядок полей пакета не меняются, меняется только СОДЕРЖИМОЕ первых четырёх байт).
//   БЫЛО: `SelfUserNum` (сессионный userNum, i16) в payload[0..3] — отмена баффа не срабатывала.
//   СТАЛО: `SelectedCharId` (charId, i32).
//   ДОКАЗАТЕЛЬСТВО ОБЕИМИ СТОРОНАМИ:
//     • клиент, дизасм живого exe @0xE82EEC-0xE82EF7:
//           call 0x92d640            ; CClass_GetField142C(&g_InventoryBag) → CCharacter*
//           mov  eax, [eax + 0x41C]  ; ← поле 0x41C
//           mov  [ebp-0xC], eax      ; payload[0..3]
//       CCharacter + 0x41C = 1052 — это ПЕРВЫЙ dword блока `g_aCharSelectData`, который эталон
//       кладёт в нового локального игрока (`game_main.c:2801` qmemcpy(pChar + 1052, …, 0x44)),
//       а его источник — тело 1017 `body[4]`, то есть ровно наш `SelectedCharId` (см. разбор
//       блока у `case OP_NET_RECV_CHAR_SELECT`). Тот же `*(DWORD*)(g_LocalPlayer + 1052)` эталон
//       кладёт в +0 у 6513/6518 (`chat.c:5709`, `:5770`).
//     • сервер `RecvClient_AdvantageBuffEnd@0x55E020` (`GameServer.c:134893-134921`):
//           memcpy(v6, (char*)a3 + 33, 7u);              // payload
//           result = sub_5324F0(this, arg0);             // = *(DWORD*)pUser->gap_16B
//           if ( *(CUser **)v6 == result ) { …обработка… }   // ← иначе МОЛЧА ничего
//       `*(DWORD*)CUser->gap_16B` сервер печатает в логах как «UserIndex» (i32) и отличает от
//       «UserNum» (i16, индекс слота сессии) — `GameServer.c:42268` печатает оба рядом.
//       В нашей терминологии это charId ([[charid-vs-usernum]]: charId — i32, userNum — i16).
bool UAsdaNetSubsystem::SendBuffCancel(int32 Kind, int32 Id)
{
	if (!Conn || !Conn->IsConnected()) { return false; }
	if (FAsdaOutgoingGate::Block(this, (uint16)OP_RECV_CLIENT_ADVANTAGE_BUFF_END, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	uint8 Payload[7];
	AsdaBytes::WrI32(&Payload[0], SelectedCharId);
	Payload[4] = (uint8)Kind;
	AsdaBytes::WrI16(&Payload[5], Id);

	uint8 Body[31];
	FMemory::Memzero(Body, sizeof(Body));
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Payload, 7); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FMemory::Memcpy(Body + 24, Payload, 7);
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_RECV_CLIENT_ADVANTAGE_BUFF_END, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("6142 отмена баффа: charId=%d вид=%d (%s) id=%d -> %s"),
		SelectedCharId, Kind, Kind == 0 ? TEXT("скилл") : TEXT("предмет"), Id, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("ОТПР 6142 отмена баффа: charId=%d вид=%d (%s) id=%d -> %s"),
		SelectedCharId, Kind, Kind == 0 ? TEXT("навык") : TEXT("предмет"), Id, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 6142 (отмена баффа)
}

// ★★★ 5256 (0x1488) / 6158 (0x180E) — ПРИМЕНЕНИЕ НАВЫКА. Одиночная запись цели.
//   ⚠ `TargetIdx` — i16: для моба это renderId, для игрока — сессионный userIdx. charId (i32) сюда
//     класть НЕЛЬЗЯ (закон charid-vs-usernum: сервер молча проигнорирует заявку либо откажет).
bool UAsdaNetSubsystem::SendSkillUse5256(int32 SkillCode, int32 TileX, int32 TileZ,
	int32 TargetType, int32 TargetIdx, int32 GroundTileX, int32 GroundTileZ)
{
	TArray<FAsdaSkillUseRec> R;
	FAsdaSkillUseRec One; One.Type = TargetType; One.Idx = TargetIdx;
	R.Add(One);
	return SendSkillUseRequest(SkillCode, TileX, TileZ, R, GroundTileX, GroundTileZ);
}

// ★★★ N записей (0..20) — площадные отправители эталона.
bool UAsdaNetSubsystem::SendSkillUseRequest(int32 SkillCode, int32 TileX, int32 TileZ,
	const TArray<FAsdaSkillUseRec>& Recs, int32 GroundTileX, int32 GroundTileZ)
{
	return SendSkillUseBody(SkillCode, TileX, TileZ, Recs, GroundTileX, GroundTileZ);
}

// Общее тело заявки на применение навыка (31 + 29·N б) — эталон `skillCastSelfSendPacket@0x9FE210`
//   и его семь площадных/адресных близнецов. Опкод выводится из кода навыка:
//   `skillCode < 1201 → 5256, иначе → 6158` (skill_buff.c:2226-2234) — это ЕДИНСТВЕННОЕ отличие
//   заявки Хранителя души от обычной, отдельной функции у эталона нет.
//   Тело:
//     [0..7]   анти-реплей токен (та же QPC-маска, что у 4026/5012/6142)
//     [8..23]  MD5(head, 7)
//     [24..30] head (7 б): i16 skillCode | u8 1 | i16 тайл X | i16 тайл Z
//     [31..]   N записей по 29 б
//   Сервер `RecvChar_SkillUse@0x46F900` / `RecvChar_SoulGuarderSkillUse@0x48E4F0` считает записи
//   по длине кадра: `iRecvCount = (Size − 41) / 29`, `iLeaveSize` обязан быть 0.
bool UAsdaNetSubsystem::SendSkillUseBody(int32 SkillCode, int32 TileX, int32 TileZ,
	const TArray<FAsdaSkillUseRec>& Recs, int32 GroundTileX, int32 GroundTileZ)
{
	// ★ 15.10.2026 (ревизия PORT-RHYTHM НИЗК-7): перехват стенда/самотеста — как у `SendSecured` (сокет и гейты живой сессии
	//   пропускаются, тело собирается тем же путём): самотест ритма атаки проверяет навык НАСТОЯЩИМ отправителем.
	const bool bStandSkill = bStandCaptureSkillUse && IsStandCapture((SkillCode < 1201) ? (uint16)OP_SKILL_USE : (uint16)OP_SOULGUARDER_SKILL_USE);
	if (SkillCode < 0) { ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР навык НЕ отправлен: код навыка %d < 0"), SkillCode); return false; }
	if (!bStandSkill && (!Conn || !Conn->IsConnected())) { ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР навык %d НЕ отправлен: нет соединения"), SkillCode); return false; }
	// Эталон отводит массив ровно на 20 записей (`targetIds[20]`), сервер тем же числом и ограничен
	//   (`iRecvCount > 0x14` ⇒ кадр выброшен МОЛЧА).
	const int32 N = FMath::Min(Recs.Num(), 20);
	// ★ Опкод — по коду навыка, как в эталоне (последняя строка каждого из восьми отправителей).
	const uint16 Opcode = (SkillCode < 1201) ? (uint16)OP_SKILL_USE : (uint16)OP_SOULGUARDER_SKILL_USE;
	const TCHAR* LogTag = (SkillCode < 1201) ? TEXT("5256 применение навыка") : TEXT("6158 скилл хранителя");
	// ★ Гейт «души» (SoulState==1): 5256 у сервера под `sub_52EEA0` (GameServer.c:20051); 6158 в
	//   диспетчере гейта НЕ имеет — таблица его и не содержит, проверка по опкоду это учитывает.
	// ★ Гейт GM-«скрыться»: под скрытием сервер глотает ОБА — 5256 (`RecvChar_SkillUse`, case 0x1488)
	//   и 6158 (`RecvChar_SkillUse_0`, case 0x180E); в отличие от гейта «души», где 6158 не значится.
	if (!bStandSkill && FAsdaOutgoingGate::Block(this, Opcode, EAsdaGate::Soul | EAsdaGate::GmHide | EAsdaGate::TransferWide))
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ОТПР %d навык %d НЕ отправлен: шлюз исходящих (душа/GM-скрытие/переход)"), (int32)Opcode, SkillCode);
		return false;
	}
	uint8 Head[7];
	const uint16 Sc = (uint16)(int16)SkillCode;
	AsdaBytes::WrI16(&Head[0], (int32)Sc);
	Head[2] = 1;                                   // эталон: LOBYTE(skillPacketHead[1]) = 1
	const uint16 Tx = (uint16)(int16)TileX, Tz = (uint16)(int16)TileZ;
	AsdaBytes::WrI16(&Head[3], (int32)Tx);
	AsdaBytes::WrI16(&Head[5], (int32)Tz);

	// Тело = 31 (токен + MD5 + head) + 29·N. При N = 1 это прежние 60 б.
	TArray<uint8> Body;
	Body.SetNumZeroed(31 + 29 * N);
	FMemory::Memcpy(Body.GetData() + 24, Head, 7);
	// ★★★ hit-запись 29 б начинается с Body+31 (эталон: `sendBuffer[31]`, `sendLen = 31`).
	//   Она НЕ нулевая: у неё есть конструктор с СЕНТИНЕЛАМИ. Клиент зовёт `SkillPacketBody_Init`,
	//   сервер — тот же `sub_429ED0@0x429ED0` (GameServer.c:18266):
	//     [0] = 0 · [1..2] = −1 · [3..6] = 0 · [7] = 0 · [8..10] = 0 · [11..14] = 0 ·
	//     [15..16] = 0 · [17..18] = 0 · **[19..20] = −1** · [21..23] = 0 · [24..27] = 0 · [28] = 0
	//   Ноль вместо −1 в [19..20] (stateId) — это валидный id статуса, а не «пусто».
	for (int32 k = 0; k < N; ++k)
	{
		uint8* Rec = Body.GetData() + 31 + 29 * k;
		Rec[19] = 0xFF; Rec[20] = 0xFF;                // stateId = −1 (сентинел, НЕ 0)
		// ★ Байт 0 записи: 1 = цель-ПЕРСОНАЖ (само-каст / игрок), 0 = цель-СУЩНОСТЬ (renderId моба).
		//   `Skill_SendSkillUse@0x9FED00` skill_buff.c:2431 и `Skill_SendAreaSkillTargetsCast@0xA01140`
		//   skill_buff.c:3312-3331 пишут 0, `skillCastSelfSendPacket@0x9FE210` skill_buff.c:2221 и
		//   `skillSendUseSkillPacket@0xA02AB0` skill_buff.c:3857 пишут 1.
		Rec[0] = (uint8)Recs[k].Type;
		const uint16 Tg = (uint16)(int16)Recs[k].Idx;
		AsdaBytes::WrI16(&Rec[1], (int32)Tg);
		// +15/+17 записи = наземные тайлы (навыки «по точке земли»); иначе остаются нулями.
		const uint16 Gx = (uint16)(int16)GroundTileX, Gz = (uint16)(int16)GroundTileZ;
		AsdaBytes::WrI16(&Rec[15], (int32)Gx);
		AsdaBytes::WrI16(&Rec[17], (int32)Gz);
	}
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Head, 7); Md5.Final(Dig); FMemory::Memcpy(Body.GetData() + 8, Dig, 16); }
	FillAntiReplaySecret(Body.GetData());
	// ★★★★★ ГЛАВНАЯ ПРАВКА 2026-08-10 — ПАКЕТ ИДЁТ **БЕЗ ПАДДИНГА** (`bPad = false`).
	//   Эталон `CNetwork_Send@0x12A9330` (`network.c:51521`) добавляет мусорный хвост ТОЛЬКО при
	//   `a6 == 1`: `g_nPacketPadCounter += 11; padLen = counter % 20; Size += padLen`.
	//   ВСЕ ВОСЕМЬ отправителей 5256 и обе ветки 6158 зовут его с **`a6 = 0`**
	//   (`skill_buff.c:2229, 2233, 2574, 2578, 2775, 3053, 3340, 3639, 3866, 4459` —
	//    `CNetwork_Send(net, sess, buf, len, 5256, 0, 1, 1, 0, 0)`), то есть БЕЗ пада.
	//   ★ ПОЧЕМУ ЭТО И ЛОМАЛО ВСЁ. Серверный `RecvChar_SkillUse@0x46F900` считает число записей
	//   ПО ДЛИНЕ КАДРА (`GameServer.c:45902-45906`):
	//       iRecvCount = (Size - 41) / 29;   iLeaveSize = (Size - 41) % 29;
	//       if (iRecvCount > 20 || iLeaveSize) { лог "iRecvCount %d, iLeaveSize %d"; return; }  // ★ МОЛЧА
	//   Кадр = 9 (шапка) + 60 (тело) + padLen + 1 (0xFE). Без пада Size = 70 → (70−41) = 29 → 1 запись,
	//   остаток 0. С падом остаток = padLen ∈ 0..19 и совпадает с нулём лишь 1 раз из 20 ⇒ сервер
	//   ВЫБРАСЫВАЕТ заявку БЕЗ ОТВЕТА. Отсюда «ни полосы, ни кулдауна, ни урона, ни иконки — ничего»:
	//   ответов 5257/5270/5271 в живом логе 2026-08-09 нет НИ ОДНОГО на 6 отправленных заявок.
	//   ★ Тот же расчёт у Хранителя души: `RecvChar_SkillUse_0@0x48E4F0` (`GameServer.c:57012-57015`)
	//   `iRecvCount = (Size − (v12+17)) / 29`, v12 = 24 ⇒ те же 41 — поэтому 6158 ломался так же.
	//   ★ Почему бой (4026/4028) работал: у них в эталоне `a6 = 1` (пад ЕСТЬ), а их обработчики
	//   длину по модулю 29 не проверяют — лишний хвост им безразличен.
	// ★ 15.10.2026 (ревизия): поставленный к дедлайну 4028 не должен уйти ПОСЛЕ заявки навыка — сервер сверил бы его со
	//   свежим Skill.lst и молча отбросил. Эталон после заявки ставит состояние 25 — тик замаха больше не шлёт.
	CancelPending4028(TEXT("заявка навыка"));
	if (bStandSkill)
	{
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
		Cap(Opcode, Body.GetData(), Body.Num(), false, TEXT('R'));
		if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSkillStart(SkillCode); }
		ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР %d навык [перехват самотеста]: код=%d тайл=(%d,%d) целей=%d (первая: тип=%d idx=%d) земля=(%d,%d)"),
			(int32)Opcode, SkillCode, TileX, TileZ, N, N > 0 ? Recs[0].Type : -1, N > 0 ? Recs[0].Idx : -1, GroundTileX, GroundTileZ);
		return true;
	}
	const bool bOk = ConnSendSecret(Opcode, Body.GetData(), Body.Num(), 0, 0, /*bPad=*/false);
	// ★ 15.10.2026 (ритм атаки): `SkillStart` NpcServer ставит прошлый удар := такт и длительность := Skill.lst[код] —
	//   следующий 4028 не раньше; цикл замаха своего персонажа снимается (эталон: состояние 25 после каждой заявки).
	if (bOk) { if (UAsdaAttackRhythmSubsystem* Rh = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr) { Rh->NoteSkillStart(SkillCode); } }
	// ★ Полный дамп тела: без него разрыв «ушло / не ушло / ушло не то» не доказать.
	//   Провод = 9 (шапка) + тело + 1 (0xFE); сервер требует ровно `41 + 29·N`
	//   (`RecvChar_SkillUse`, GameServer.c:45903), при N = 1 это и есть 70.
	FString Hex, Tgt;
	for (int32 i = 0; i < Body.Num(); ++i) { Hex += FString::Printf(TEXT("%02X "), Body[i]); }
	for (int32 k = 0; k < N; ++k)
	{
		Tgt += FString::Printf(TEXT("%s(тип=%d, idx=%d)"), k ? TEXT(" ") : TEXT(""), Recs[k].Type, Recs[k].Idx);
	}
	if (N == 0) { Tgt = TEXT("НЕТ ЗАПИСЕЙ (0 целей — эталонный площадной кадр)"); }
	UE_LOG(LogAsdaNet, Log, TEXT("→%s: код=%d тайл=(%d,%d) целей=%d %s земля=(%d,%d) -> %s\n")
		TEXT("      тело %d б (провод %d): %s"),
		LogTag, SkillCode, TileX, TileZ, N, *Tgt, GroundTileX, GroundTileZ,
		bOk ? TEXT("OK") : TEXT("FAIL"), Body.Num(), Body.Num() + 10, *Hex);
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР %d %s: код=%d тайл=(%d,%d) целей=%d [%s] земля=(%d,%d) тело %d б -> %s"),
		(int32)Opcode, LogTag, SkillCode, TileX, TileZ, N, *Tgt, GroundTileX, GroundTileZ, Body.Num(), bOk ? TEXT("OK") : TEXT("FAIL"));
#if ASDA_COMBAT_TRACE
	// Журнал боя: цели навыка — на уровне 1. Тип записи 0 — моб (Idx = renderId), 1 — персонаж (Idx = userNum).
	for (int32 k = 0; k < N; ++k)
	{
		if (Recs[k].Idx < 0) { continue; }
		if (Recs[k].Type == 0) { AsdaCombatTrace::NoteEngaged(Recs[k].Idx); }
		else                   { AsdaCombatTrace::NoteEngagedUser(Recs[k].Idx); }
	}
#endif
	return TrackSend(bOk);
}

// ★★★ 5231 (0x146F) — ПРИМЕНЕНИЕ НАВЫКА СОУЛМЕЙТА. Эталон `netSkill_SendSoulmateUse@0xA07540`
//   (`network.c:6587`) 1:1. Пакет 218 б:
//     [0..7]   анти-реплей токен | [8..23] MD5(payload, 0xC2) | [24..217] payload (194 б = 97 слов)
//   payload инициализируется конструктором `sub_A07CE0` (сентинелы −1), затем:
//     word[0]  = soulmateSkillId
//     word[57] = word[58] = **СВОЙ** сессионный userNum. ★ Исправлено 2026-08-10: раньше сюда
//       клали индекс ВЫБРАННОЙ ЦЕЛИ из-за мисней-имени `Target_GetSelectedUserIdx@0x9254E5`.
//       Это thunk на `Char_GetHoveredUserIdx@0x966490` = `return *((u16*)this + 95094)`, то есть
//       ГЕТТЕР СОБСТВЕННОГО userNum ПЕРЕДАННОГО персонажа. В `netSkill_SendSoulmateUse@0xA07540`
//       (network.c:6611-6622) ему передают `localPlayer = CharManager_GetLocalPlayer(...)`,
//       значит в оба слова едет СВОЙ userNum. Правильное имя функции — `Char_GetUserNum`.
//   Конструктор (`network.c:6729`, 98 слов):
//     w[0] = −1; w[56] = −1; for i in 0..9 { w[1+i] = −1; dw[(11+2i)/2] = −1 (слова 11..30 = 0xFFFF);
//     байты 62..71 = 0; dword[18+i] = 0 (байты 72..111); w[57+i] = −1; слова 67..86 = 0xFFFF;
//     байты 174..183 = 0 } ; w[92..95] = −1.
//   Сервер: `RecvChar_SoulMateSkillUse@0x49CD10`, case 20007; ответ — 5232 (197 б).
bool UAsdaNetSubsystem::SendSoulmateSkillUse5231(int32 SoulmateSkillId, int32 TargetUserIdx)
{
	if (!Conn || !Conn->IsConnected() || SoulmateSkillId < 0) { return false; }
	// ★ Гейт «души» (SoulState==1): 5231 у сервера под `sub_52EEA0` (GameServer.c:20034).
	if (FAsdaOutgoingGate::Block(this, (uint16)OP_RECV_CHAR_SOUL_MATE_SKILL_USE, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }
	// 98 слов = 196 б, но на провод копируются первые 0xC2 = 194 б (97 слов) — как у эталона.
	uint16 W[98];
	FMemory::Memzero(W, sizeof(W));
	auto SetW = [&W](int32 Idx, uint16 V) { if (Idx >= 0 && Idx < 98) { W[Idx] = V; } };
	SetW(0, 0xFFFF);
	SetW(56, 0xFFFF);
	for (int32 i = 0; i < 10; ++i)
	{
		SetW(1 + i, 0xFFFF);                       // слова 1..10
		SetW(11 + 2 * i, 0xFFFF); SetW(12 + 2 * i, 0xFFFF);   // dword-пары: слова 11..30
		SetW(57 + i, 0xFFFF);                      // слова 57..66
		SetW(67 + 2 * i, 0xFFFF); SetW(68 + 2 * i, 0xFFFF);   // dword-пары: слова 67..86
	}
	SetW(92, 0xFFFF); SetW(93, 0xFFFF); SetW(94, 0xFFFF); SetW(95, 0xFFFF);
	// Байтовые зоны 62..71 и 174..183 конструктор обнуляет — у нас они и так нули (Memzero).
	SetW(0, (uint16)(int16)SoulmateSkillId);
	const uint16 Tg = (uint16)(int16)TargetUserIdx;
	SetW(57, Tg);
	SetW(58, Tg);

	uint8 Payload[194];
	for (int32 i = 0; i < 97; ++i)
	{
		AsdaBytes::WrI16(&Payload[2 * i], (int32)W[i]);
	}

	uint8 Body[218];
	FMemory::Memzero(Body, sizeof(Body));
	FMemory::Memcpy(Body + 24, Payload, sizeof(Payload));
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Payload, sizeof(Payload)); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_RECV_CHAR_SOUL_MATE_SKILL_USE, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("→5231 навык соулмейта: id=%d цель(userIdx)=%d -> %s"),
		SoulmateSkillId, TargetUserIdx, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("ОТПР 5231 навык соулмейта: id=%d слово57/58 userIdx=%d -> %s"),
		SoulmateSkillId, TargetUserIdx, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ Эталонная таблица нотисов отказа каста `dword_15B5D78` (`Recv_SkillCast` network.c:44961,
//   `recvOtherCharSkillUse` :44632 — оба индексируют её байтом result). Считана из самого бинаря
//   сборки IDB (`RE/ida/work/AsdaStory.exe`, VA 0x15B5D78 → файловый оффсет 0xC98D78), 35 значащих
//   элементов; дальше в памяти уже float-константы, значит длина таблицы ровно 35.
//   Коды, которыми отвечает CSoulGuarderManager (GameServer.c:57031-57094):
//     0 → 1017 (общий отказ), 5 → 1021 «нет монет» (`sub_48B850`), 6 → 1022 (skillMotionType 3),
//     13 → 1076 (движение), 16 → 1079, 17 → 1080, 20 → 1083 (нет цели), 21 → 1150 (кулдаун),
//     24 → 1153 (действие занято), 27 → 1156 (10-й буфф), 32 → 2039 (состояние 15), 33 → 1400 (зона).
int32 UAsdaNetSubsystem::SkillFailNoticeLocId(int32 ResultCode)
{
	static const int32 kTable[35] = {
		1017,    0, 1018, 1067, 1020, 1021, 1022, 1023, 1071, 1072,
		1073, 1074, 1075, 1076, 1077, 1078, 1079, 1080,    0, 1082,
		1083, 1150, 1151, 1152, 1153, 1154, 1155, 1156, 1157, 1158,
		9557,13216, 2039, 1400,10184 };
	return (ResultCode >= 0 && ResultCode < 35) ? kTable[ResultCode] : 0;
}

// ★ Разбор 29-байтных записей 5257/5270 (закрытие хвоста §14.6 п.2). Раскладка — §5.3 отчёта
//   skill_use_system.md; смыслы полей — комментарий у FAsdaSkillCastRec (закон дедупа: раскладка
//   расписана один раз, в заголовке).
void UAsdaNetSubsystem::ParseSkillCastRecs29(const uint8* Data, int32 Len, int32 Off, int32 N,
	TArray<FAsdaSkillCastRec>& Out)
{
	Out.Reset();
	if (!Data || Off < 0) { return; }
	for (int32 i = 0; i < N; ++i)
	{
		const int32 P = Off + kSkillCastRecSize * i;
		if (P + kSkillCastRecSize > Len) { break; }   // неполный хвост — сервер такого не шлёт, но границу держим
		const uint8* R = Data + P;
		FAsdaSkillCastRec Rec;
		Rec.TargetKind = (int32)R[0];
		Rec.TargetIdx  = AsdaBytes::RdI16(&R[1]);
		Rec.Damage     = AsdaBytes::RdI32(&R[3]);
		Rec.HitFlags   = (int32)R[7];
		Rec.HpAfter    = AsdaBytes::RdI32(&R[11]);
		Rec.StateId    = AsdaBytes::RdI16(&R[19]);
		Rec.PopupValue = AsdaBytes::RdI32(&R[24]);
		Rec.BuffStacks = (int32)R[8] | ((int32)R[9] << 8) | ((int32)R[10] << 16);   // ★ bowfx: стеки слотов buff0/1/2
		UE_LOG(LogAsdaNet, Verbose,
			TEXT("  29б-запись %d: цель kind=%d idx=%d УРОН=%d flags=0x%02X (бит0 попадание=%d, крит=%d) hp=%d state=%d popup=%d"),
			i, Rec.TargetKind, Rec.TargetIdx, Rec.Damage, Rec.HitFlags, Rec.HitFlags & 1,
			(Rec.HitFlags >> 2) & 1, Rec.HpAfter, Rec.StateId, Rec.PopupValue);
		// Покадрово — уровень 2: те же записи на уровне 1 пишет слой показа навыков (OnNetSkillCastRecs),
		//   итог на пакет («записей=N») — строка ПРИЁМ 5257/5270/6159/6160 у вызывающего.
		ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("  запись навыка %d/%d: цель %s idx=%d урон=%d флаги=0x%02X (попадание=%s крит=%s) HP после=%d состояние=%d всплывашка=%d"),
			i + 1, N, Rec.TargetKind == 1 ? TEXT("игрок(uidx)") : TEXT("моб(rid)"), Rec.TargetIdx, Rec.Damage, Rec.HitFlags,
			(Rec.HitFlags & 1) ? TEXT("да") : TEXT("нет"), ((Rec.HitFlags >> 2) & 1) ? TEXT("да") : TEXT("нет"), Rec.HpAfter, Rec.StateId, Rec.PopupValue);
		Out.Add(Rec);
	}
}

// ★ Аудит №3 (B12): геометрия страницы квикслотов — одной функцией, по образцу `ParseSkillCastRecs29`.
//   Поля 10-байтной записи разбирает ЕДИНСТВЕННОЕ место порта (`UAsdaHotbarSubsystem::ApplyPendingPages`),
//   поэтому здесь фиксируется только размер страницы; поведение 1:1 с прежним открытым кодом —
//   копируем сколько пришло, хвост короткого тела остаётся нулевым (запись НЕ обрезается по границе).
int32 UAsdaNetSubsystem::ParseQuickSlotPage10(const uint8* Data, int32 Len, TArray<uint8>& OutSlots)
{
	OutSlots.Reset();
	OutSlots.SetNumZeroed(kQuickSlotPageBytes);
	const int32 Avail = (Data && Len > 1) ? FMath::Min<int32>(kQuickSlotPageBytes, Len - 1) : 0;
	if (Avail > 0) { FMemory::Memcpy(OutSlots.GetData(), Data + 1, Avail); }
	return Avail;
}

// ★★★ 5067 0x13CB — СОХРАНИТЬ ОДНУ СТРАНИЦУ КВИКСЛОТОВ (эталон `Hotbar_SendLayout@0xDF2070`,
//   ui_core.c:68665). Байт-в-байт:
//       layoutBuf[131]: [0] = page, [1 + 10*i] = 10-байтовая запись слота i (i = 0..12);
//       md5 = Net_Md5Digest16(&g_NetMd5Salt, layoutBuf, 131);
//       packet[155]: [0..7] = buildAntiCheatToken(), [8..23] = md5, [24..154] = layoutBuf;
//       CNetwork_Send(..., 155, OP_RECV_DB_QUICK_SLOT_INFO_SAVE, 1,1,1,0,0).
//   Сервер (`Recv_DBQuickSlotInfo_Save@0x4DAD60`) проверяет ТОЛЬКО анти-реплей-токен и границу
//   слота (`v14 < 78`); MD5 у него де-факто выключен (результат memcmp не используется — дефект
//   ОРИГИНАЛЬНОГО СЕРВЕРА, мы всё равно считаем честно). Ответа сервер не шлёт.
bool UAsdaNetSubsystem::SendHotbarLayout5067(uint8 Page, const TArray<uint8>& Slots130)
{
	// ★ 07.11.2026 перехват самотеста: офлайн раскладка уходит ему (страница + 13×10), как у 4006/4008; без перехвата путь прежний.
	if (IsStandCapture((uint16)OP_RECV_DB_QUICK_SLOT_INFO_SAVE) && Slots130.Num() == kQuickSlotPageBytes)
	{
		uint8 Stand[kQuickSlotLayoutBytes];
		FMemory::Memzero(Stand, sizeof(Stand));
		Stand[0] = Page;
		FMemory::Memcpy(Stand + 1, Slots130.GetData(), kQuickSlotPageBytes);
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри (см. SendSecured)
		Cap((uint16)OP_RECV_DB_QUICK_SLOT_INFO_SAVE, Stand, (int32)sizeof(Stand), false, TEXT('T'));
		return true;
	}
	if (!Conn || !Conn->IsConnected()) { return false; }
	if (FAsdaOutgoingGate::Block(this, (uint16)OP_RECV_DB_QUICK_SLOT_INFO_SAVE, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	if (Slots130.Num() != kQuickSlotPageBytes)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("5067 квикслоты: раскладка страницы %u должна быть %d б, пришло %d"),
			Page, kQuickSlotPageBytes, Slots130.Num());
		return false;
	}
	uint8 Layout[kQuickSlotLayoutBytes];
	FMemory::Memzero(Layout, sizeof(Layout));
	Layout[0] = Page;
	FMemory::Memcpy(Layout + 1, Slots130.GetData(), kQuickSlotPageBytes);

	uint8 Body[24 + kQuickSlotLayoutBytes];
	FMemory::Memzero(Body, sizeof(Body));
	FMemory::Memcpy(Body + 24, Layout, kQuickSlotLayoutBytes);
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Layout, kQuickSlotLayoutBytes); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_RECV_DB_QUICK_SLOT_INFO_SAVE, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("→5067 квикслоты: сохранена страница %u (155 б) -> %s"),
		Page, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 5253 (28 б = 24 + 4) — вложить очко навыка. Эталон `net_sendRequest_5253@0xE54DE0`:
//   msgHeader[0] = skillCode (i16), LOBYTE(msgHeader[1]) = flagByte, HIBYTE(msgHeader[1]) = pointCount.
//   Сервер `Recv_SkillPoint_SkillActive@0x4774C0` (диспетч case 0x1485) читает тело по +33 ровно 4 байта
//   и считает по ним MD5 (`sub_523F00(arg4, 4u, …)`) — длина тела менять НЕЛЬЗЯ.
bool UAsdaNetSubsystem::SendSkillLearn5253(int32 SkillCode, uint8 PointCount, uint8 FlagByte)
{
	FAsdaPacketWriter W(4);
	W.I16(0, SkillCode);
	W.U8(2, FlagByte);
	W.U8(3, PointCount);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_SKILL_POINT_SKILL_ACTIVE, TEXT("SendSkillLearn5253"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_SKILL_POINT_SKILL_ACTIVE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→5253 очко навыка: skill=%d очков=%u флаг=%u (28 б) -> %s"),
		SkillCode, (uint32)PointCount, (uint32)FlagByte, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ══════════════ ★★★ ЕДИНЫЙ ИСТОЧНИК ИСТИНЫ ПО HP/MP (реализация) ═══════════════════════════════
// Все каналы (4058 регенерация/лечение/яд, 4055/4056 воскрешение, 4053 смерть, char-list при
//   спавне, локальный декремент по цифре урона 4015 kind=3 / 4029) пишут ТОЛЬКО сюда. Плашка и
//   HUD подписаны на OnVitalsChanged и ничего не считают сами — двух параллельных путей больше нет.

// Текущее время мира (для отметки последнего серверного снапшота в дампе). 0 вне мира.
double UAsdaNetSubsystem::VitalsNowSeconds() const
{
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return W ? W->GetTimeSeconds() : 0.0;
}

// ★★★ 08.11.2026 (пачка 19, баг 3: «умер, а полоса 1282/15277») [ОРИГИНАЛ] — СВОЯ СМЕРТЬ = HP и MP В НУЛЕ.
//   Эталон: удар по себе пишет HP только при `newHp > 0` (network.c:37680, combat_pvp.c:7780/8282) — убивающий
//   удар, превышающий остаток, HP НЕ меняет. Ноль даёт СОСТОЯНИЕ 6: `CCharacter_SetAction` case 6
//   (entity_char.c:16913: `UI_UpdateHPBar(hud, 0, max)`, `curHp = 0`) и покадрово `HUD_RefreshHPMPGauges_Frame`
//   (ui_core.c:75482-75497: при состоянии 6 `curHp = 0`, второе HP = 0, `curMp = 0`, все полосы 0).
//   Порт при смерти гасил только «второе» HP/MP (`ApplyDeathGaugeReset`), текущее оставалось последним
//   положительным — полоса > 0 у мёртвого. 1 (умолч.) — при своей смерти HP/MP = 0, пока смерть не снята
//   воскрешением (любой снимок в это время пишется нулём, как покадровое обнуление эталона); 0 — прежнее.
static ASDA_DEV_CVAR(int32, CVarAsdaDeathZeroVitals,
	TEXT("asda.Death.ZeroVitals"),
	1,
	TEXT("Своя смерть (состояние 6): 1 = текущее HP и MP = 0 до воскрешения (эталон ui_core.c:75482, entity_char.c:16913); 0 = прежнее (HP оставалось последним положительным)."),
	ECVF_Default);
// ★ [ИСПРАВЛЕНИЕ] 08.11.2026 — смерть ДРУГОГО игрока (4053): HP в хранилище = 0. Эталон `CChar_SetState` case 6
//   (entity_char.c:19622) HP соседа не трогает, и полоса/плашка мёртвого противника держала последний остаток
//   (тот же «убивающий удар не пишется»). 1 (умолч.) — 0 по 4053; 0 — как эталон. Воскрешение соседа — 4056 (HP@6).
static ASDA_DEV_CVAR(int32, CVarAsdaDeathZeroPeerHp,
	TEXT("asda.Death.ZeroPeerHp"),
	1,
	TEXT("[ИСПРАВЛЕНИЕ] Смерть другого игрока (4053): 1 = его HP в хранилище = 0 (полоса цели и плашка); 0 = как эталон (последний остаток)."),
	ECVF_Default);
bool AsdaDeathZeroVitalsOn() { return CVarAsdaDeathZeroVitals.GetValueOnGameThread() != 0; }
bool AsdaDeathZeroPeerHpOn() { return CVarAsdaDeathZeroPeerHp.GetValueOnGameThread() != 0; }

// Своё HP/MP при смерти — в ноль (вход в состояние 6). Зовут 4053 (свой) и EnterSelfDeadState.
void AsdaZeroSelfVitalsOnDeath(UAsdaNetSubsystem* Net, const TCHAR* Why)
{
	if (!Net || !AsdaDeathZeroVitalsOn()) { return; }
	const int32 Self = (int32)Net->SelfUserNum;
	const UAsdaNetSubsystem::FAsdaVitals* V = Net->GetVitals(Self);
	ASDA_CTRACE(EAsdaCT::Death, TEXT("СВОЯ СМЕРТЬ → HP %d→0, MP %d→0 (%s; эталон состояние 6: entity_char.c:16913, ui_core.c:75482)"),
		V ? V->HpCur : -1, V ? V->MpCur : -1, Why ? Why : TEXT("?"));
	UE_LOG(LogAsdaNet, Log, TEXT("[HP] своя смерть (%s): HP %d→0, MP %d→0 — эталон состояние 6"),
		Why ? Why : TEXT("?"), V ? V->HpCur : -1, V ? V->MpCur : -1);
	Net->SetVitalsHp(Self, 0, -1, TEXT("смерть (состояние 6)"), /*bServerSnapshot=*/false);
	Net->SetVitalsMp(Self, 0, -1, TEXT("смерть (состояние 6)"), /*bServerSnapshot=*/false);
}

void UAsdaNetSubsystem::SetVitalsHp(int32 UserIdx, int32 Cur, int32 Max, const TCHAR* Src, bool bServerSnapshot)
{
	if (UserIdx < 0) { return; }
	// ★ 08.11.2026 `asda.Death.ZeroVitals`: пока свой персонаж мёртв (CUser+79, снимается только воскрешением — флаг
	//   снимается ДО записи HP воскрешения), любое значение пишется нулём — покадровое обнуление эталона (ui_core.c:75494).
	if (Cur > 0 && bSelfDead && IsSelfUserIdx(UserIdx) && AsdaDeathZeroVitalsOn())
	{
		ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("HP %d (канал %s) у МЁРТВОГО себя → 0 (эталон: состояние 6 держит HP в нуле)"), Cur, Src ? Src : TEXT("?"));
		Cur = 0;
	}
	FAsdaVitals& V = VitalsByUserIdx.FindOrAdd(UserIdx);
	const int32 OldCur = V.HpCur, OldMax = V.HpMax;
	if (Max > 0) { V.HpMax = Max; }
	if (Cur >= 0)
	{
		// Клампим ТОЛЬКО сверху известным максимумом: снизу 0 — уже мёртв (эталон HP не уходит в минус).
		V.HpCur = (V.HpMax > 0) ? FMath::Clamp(Cur, 0, V.HpMax) : FMath::Max(0, Cur);
	}
	if (bServerSnapshot) { V.LastServerHpS = VitalsNowSeconds(); }
	V.LastHpSrc = Src ? FString(Src) : FString();
	if (V.HpCur == OldCur && V.HpMax == OldMax) { return; }   // ничего не изменилось — сигнал не шлём
	UE_LOG(LogAsdaNet, Log, TEXT("[HP] userIdx=%d: %d→%d / %d (канал %s%s)"),
		UserIdx, OldCur, V.HpCur, V.HpMax, Src ? Src : TEXT("?"),
		bServerSnapshot ? TEXT(", СЕРВЕР") : TEXT(", локально"));
	// Свой — уровень 1; чужие — по UID (наш игрок — 1, прочие — 2): то же изменение HP соседа на уровне 1 пишет
	//   UAsdaWorldSubsystem::OnVitalsChanged (дубль), а реген соседей идёт по всему сектору.
	ASDA_CTRACE_IF(IsSelfUserIdx(UserIdx) || AsdaCombatTrace::IsUserRelevant(UserIdx),
		IsSelfUserIdx(UserIdx) ? EAsdaCT::HpSelf : EAsdaCT::HpPlate,
		TEXT("хранилище HP игрока uidx=%d%s: %d→%d / %d (канал %s, %s) → OnVitalsChanged"),
		UserIdx, IsSelfUserIdx(UserIdx) ? TEXT(" (Я)") : TEXT(""), OldCur, V.HpCur, V.HpMax, Src ? Src : TEXT("?"),
		bServerSnapshot ? TEXT("снимок сервера") : TEXT("локально"));
	OnVitalsChanged.Broadcast(UserIdx, V);
}

void UAsdaNetSubsystem::SetVitalsMp(int32 UserIdx, int32 Cur, int32 Max, const TCHAR* Src, bool bServerSnapshot)
{
	if (UserIdx < 0) { return; }
	// ★ 08.11.2026 `asda.Death.ZeroVitals`: мёртвый — MP в нуле (эталон ui_core.c:75495 `HIWORD(iCurrentHp) = 0`).
	if (Cur > 0 && bSelfDead && IsSelfUserIdx(UserIdx) && AsdaDeathZeroVitalsOn()) { Cur = 0; }
	FAsdaVitals& V = VitalsByUserIdx.FindOrAdd(UserIdx);
	const int32 OldCur = V.MpCur, OldMax = V.MpMax;
	if (Max > 0) { V.MpMax = Max; }
	if (Cur >= 0) { V.MpCur = (V.MpMax > 0) ? FMath::Clamp(Cur, 0, V.MpMax) : FMath::Max(0, Cur); }
	if (V.MpCur == OldCur && V.MpMax == OldMax) { return; }
	UE_LOG(LogAsdaNet, Verbose, TEXT("[MP] userIdx=%d: %d→%d / %d (канал %s%s)"),
		UserIdx, OldCur, V.MpCur, V.MpMax, Src ? Src : TEXT("?"),
		bServerSnapshot ? TEXT(", СЕРВЕР") : TEXT(", локально"));
	// MP своего — событие (расход на навык виден в цикле), чужое MP — только уровень 2.
	if (IsSelfUserIdx(UserIdx))
	{
		ASDA_CTRACE(EAsdaCT::HpSelf, TEXT("хранилище MP (Я) uidx=%d: %d→%d / %d (канал %s, %s)"),
			UserIdx, OldCur, V.MpCur, V.MpMax, Src ? Src : TEXT("?"), bServerSnapshot ? TEXT("снимок сервера") : TEXT("локально"));
	}
	else
	{
		ASDA_CTRACE_UID(UserIdx, EAsdaCT::HpPlate, TEXT("хранилище MP uidx=%d: %d→%d / %d (канал %s)"), UserIdx, OldCur, V.MpCur, V.MpMax, Src ? Src : TEXT("?"));
	}
	OnVitalsChanged.Broadcast(UserIdx, V);
}

// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 (вес ×2, сервер P96 `WeightU16`): ВЕС ПО ПРОВОДУ — u16. Шапка — `AsdaWeightWire.h`.
//   1 (умолч.) — вес в пакетах читается без знака (0…65535: база 30000, потолок 60000, до 64000 у расширенных);
//   0 — прежнее знаковое чтение эталона (вес > 32767 приходил отрицательным, гейты `> 0` выбрасывали пакет).
static ASDA_DEV_CVAR(int32, CVarWeightUnsigned,
	TEXT("asda.Weight.Unsigned"),
	1,
	TEXT("Вес персонажа в пакетах: 1 = беззнаковые 16 бит (вес x2, сервер P96, до 65535); 0 = прежнее знаковое чтение (i16)."),
	ECVF_Default);

bool AsdaWeightUnsignedOn()
{
	return CVarWeightUnsigned.GetValueOnGameThread() != 0;
}

int32 AsdaWeightFromWire(int32 Raw)
{
	// Младшие 16 бит без знака: верно и для `RdI16` (знаковое расширение), и для i32-полей сервера с `movsx` (старый)
	//   либо `movzx` (P96). Значения вне слова протокол не носит (вес — слово во всех 50 каналах).
	return AsdaWeightUnsignedOn() ? (int32)(uint16)(Raw & 0xFFFF) : Raw;
}

// ★★★ ВЕС ЛОКАЛЬНОГО ИГРОКА. Эталон `HUD_UpdateMiniWeightBar@0xE78FA0` (ui_core.c:75139):
//   отрицательный текущий → 0, затем ЗАПИСЬ в char+0x44A, и если максимум <= 0 — выход БЕЗ
//   единой отрисовки. Ровно это здесь: клампим только снизу, максимум не выдумываем.
void UAsdaNetSubsystem::SetSelfWeight(int32 Cur, int32 Max, const TCHAR* Src)
{
	const int32 OldCur = SelfWeightCur, OldMax = SelfWeightMax;
	if (Max >= 0) { SelfWeightMax = Max; }
	if (Cur >= 0) { SelfWeightCur = Cur; }
	else if (Cur < 0 && Cur != -1) { SelfWeightCur = 0; }   // эталон: `if (a2 <= 0) a2 = 0`
	SelfWeightSrc = Src ? FString(Src) : FString(TEXT("?"));
	UE_LOG(LogAsdaNet, Log, TEXT("[вес] %d/%d → %d/%d (канал %s)"),
		OldCur, OldMax, SelfWeightCur, SelfWeightMax, *SelfWeightSrc);
	OnSelfWeight.Broadcast(SelfWeightCur, SelfWeightMax);
}

void UAsdaNetSubsystem::ApplyVitalsDamage(int32 UserIdx, int32 Dmg, const TCHAR* Src)
{
	if (UserIdx < 0 || Dmg <= 0) { return; }
	FAsdaVitals* V = VitalsByUserIdx.Find(UserIdx);
	if (!V || !V->HasHp())
	{
		// Снапшота ещё не было — вычитать не из чего. Эталон в этом случае тоже ничего не рисует
		//   (CCharacter существует только со своими значениями). Логируем: это признак пропуска канала.
		UE_LOG(LogAsdaNet, Verbose, TEXT("[HP] урон %d по userIdx=%d — витальных значений ещё нет (канал %s)"),
			Dmg, UserIdx, Src ? Src : TEXT("?"));
		ASDA_CTRACE_IF(IsSelfUserIdx(UserIdx) || AsdaCombatTrace::IsUserRelevant(UserIdx), EAsdaCT::Damage, TEXT("урон %d по игроку uidx=%d НЕ вычтен: HP ещё неизвестно (нет снимка; канал %s)"), Dmg, UserIdx, Src ? Src : TEXT("?"));
		return;
	}
	// ★★★ ПРАВИЛО ЭТАЛОНА 1:1 (network.c:37680-37682, ветка 4015 kind=3 / kind=0xE):
	//       newHp = curHp − (hpDelta & 0x7FFFFFFF);
	//       if (newHp > 0) curHp = newHp;          // ← при newHp <= 0 значение НЕ ПИШЕТСЯ
	//   То есть локальная предикция урона НИКОГДА не показывает 0: ноль приходит ТОЛЬКО серверным
	//   снапшотом (4058/4056). Порт же клампил в 0 (`FMath::Max(0, …)`) — и полоса намертво вставала
	//   на нуле, пока сервер продолжал бить (жалоба юзера 2026-08-04). Это вторая половина фикса.
	const int32 NewHp = V->HpCur - Dmg;
	if (NewHp <= 0)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("[HP] userIdx=%d: урон %d дал бы %d ≤ 0 — эталон НЕ пишет (ноль только от сервера); остаётся %d/%d"),
			UserIdx, Dmg, NewHp, V->HpCur, V->HpMax);
		ASDA_CTRACE_IF(IsSelfUserIdx(UserIdx) || AsdaCombatTrace::IsUserRelevant(UserIdx), EAsdaCT::Damage, TEXT("урон %d по игроку uidx=%d: %d→%d ≤ 0 — НЕ пишется (ноль только от сервера), остаётся %d/%d (канал %s)"),
			Dmg, UserIdx, V->HpCur, NewHp, V->HpCur, V->HpMax, Src ? Src : TEXT("?"));
		return;
	}
	// Свой — уровень 1; чужие — по UID (урон мобов по всем игрокам сектора идёт сюда же).
	ASDA_CTRACE_IF(IsSelfUserIdx(UserIdx) || AsdaCombatTrace::IsUserRelevant(UserIdx), EAsdaCT::Damage, TEXT("урон %d по игроку uidx=%d%s: локальная предикция HP %d→%d (канал %s)"),
		Dmg, UserIdx, IsSelfUserIdx(UserIdx) ? TEXT(" (Я)") : TEXT(""), V->HpCur, NewHp, Src ? Src : TEXT("?"));
	SetVitalsHp(UserIdx, NewHp, -1, Src, /*bServerSnapshot=*/false);
}

// ══════════ ★★★ БОЕВОЙ БЛОК (физ./маг. атака) — общая точка разбора для 4060 и 4036 ═══════════
// Оба эталонных обработчика делают ОДИН И ТОТ ЖЕ memcpy 0x44 в `CCharacter+1124`, различаясь только
//   тем, КОМУ пишут: 4060 — всегда локальному (`CClass_GetField142C(&g_InventoryBag)`), 4036 — по
//   `CharManager_FindByUserIdx(userIdx)`. Поэтому раскладка здесь одна на оба канала.
//   Смещения внутри блока (от его начала): 0 d maxHp · 4 w maxMp · 6 d curHp · 10 w curMp ·
//   12 w physAtkMin · 14 w physAtkMax · 16 w magAtkMin · 18 w magAtkMax.
void UAsdaNetSubsystem::SetCombatStatsFromBlock(int32 UserIdx, const uint8* Block, int32 Avail, const TCHAR* Src)
{
	// Нужны первые 20 байт блока (до `magAttackMax` включительно). Меньше — канал битый, молча не глотаем.
	if (UserIdx < 0 || !Block || Avail < 20)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("[статы] боевой блок userIdx=%d канал %s: доступно %d б (<20) — не применяем"),
			UserIdx, Src ? Src : TEXT("?"), Avail);
		return;
	}
	FAsdaCombatStats& S = CombatStatsByUserIdx.FindOrAdd(UserIdx);
	// Эталонные поля — `__int16` со знаком (`*((__int16 *)pChar + 569)`), читаем так же.
	S.PhysAtkMin = AsdaBytes::RdI16(Block + 12);
	S.PhysAtkMax = AsdaBytes::RdI16(Block + 14);
	S.MagAtkMin  = AsdaBytes::RdI16(Block + 16);
	S.MagAtkMax  = AsdaBytes::RdI16(Block + 18);
	S.Src        = Src ? FString(Src) : FString();
	// ★ Шесть слов стихийной атаки (+44…+54 = `CCharacter+1168…+1178`) — их и только их читает
	//   панель цели, выбирая метку `PVP_MARK00..05` по МАКСИМУМУ (ui_core.c:77526-77541).
	//   Читаем, лишь если блок пришёл целиком: короткий хвост молча нулями не забиваем.
	if (Avail >= 56)
	{
		for (int32 i = 0; i < 6; ++i) { S.Ele[i] = AsdaBytes::RdI16(Block + 44 + 2 * i); }
		S.bEleValid = true;
	}
	UE_LOG(LogAsdaNet, Log,
		TEXT("[статы] боевой блок userIdx=%d (%s): физ. атака %d~%d, маг. атака %d~%d; ")
		TEXT("стихии %s[%d %d %d %d %d %d] ")
		TEXT("(эталон: CCharacter+1136/+1138/+1140/+1142 и +1168…+1178)"),
		UserIdx, Src ? Src : TEXT("?"), S.PhysAtkMin, S.PhysAtkMax, S.MagAtkMin, S.MagAtkMax,
		S.bEleValid ? TEXT("") : TEXT("НЕ ПРИШЛИ "),
		S.Ele[0], S.Ele[1], S.Ele[2], S.Ele[3], S.Ele[4], S.Ele[5]);
}

// ══════ ★★★ ПОЛНЫЙ СТАТ-БЛОК СВОЕГО ПЕРСОНАЖА — питание окна характеристик (900) ══════════════
// Реверс и доказательства раскладки: `Reports/char_info_window.md` §3.2 (68 б) и §3.3 (36 б).
// Оба блока в эталоне живут в `CCharacter`, окно читает их напрямую; в порту храним отдельно, потому
//   что клиентской `CCharacter` у нас нет. HP/MP отсюда НЕ берём — у них свой канал (SetVitalsHp/Mp).
void UAsdaNetSubsystem::SetSelfStatBlockFrom68(const uint8* Block, int32 Avail, const TCHAR* Src)
{
	// Нужен блок целиком: последнее читаемое окном слово — защита по тьме на +66.
	if (!Block || Avail < 68)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[статы] стат-блок 68 б (канал %s): доступно %d б (<68) — окно характеристик не обновляем"),
			Src ? Src : TEXT("?"), Avail);
		return;
	}
	FAsdaSelfStatBlock& S = SelfStatBlock;
	S.PhysAtkMin = AsdaBytes::RdI16(Block + 12);
	S.PhysAtkMax = AsdaBytes::RdI16(Block + 14);
	S.MagAtkMin  = AsdaBytes::RdI16(Block + 16);
	S.MagAtkMax  = AsdaBytes::RdI16(Block + 18);
	S.MagDefense = AsdaBytes::RdI16(Block + 20);
	S.DefMin     = AsdaBytes::RdI16(Block + 22);
	S.DefMax     = AsdaBytes::RdI16(Block + 24);
	S.BlockMin   = AsdaBytes::RdI16(Block + 30);
	S.BlockMax   = AsdaBytes::RdI16(Block + 32);
	for (int32 i = 0; i < 6; ++i)
	{
		S.EleAtk[i] = AsdaBytes::RdI16(Block + 44 + 2 * i);
		S.EleDef[i] = AsdaBytes::RdI16(Block + 56 + 2 * i);
	}
	S.bValid = true;
	UE_LOG(LogAsdaNet, Log,
		TEXT("[статы] стат-блок 68 б (%s): атака %d~%d, маг. атака %d~%d, защита %d~%d, маг. защита %d, ")
		TEXT("блок %d~%d, стихии атк [%d %d %d %d %d %d] защ [%d %d %d %d %d %d]"),
		Src ? Src : TEXT("?"), S.PhysAtkMin, S.PhysAtkMax, S.MagAtkMin, S.MagAtkMax,
		S.DefMin, S.DefMax, S.MagDefense, S.BlockMin, S.BlockMax,
		S.EleAtk[0], S.EleAtk[1], S.EleAtk[2], S.EleAtk[3], S.EleAtk[4], S.EleAtk[5],
		S.EleDef[0], S.EleDef[1], S.EleDef[2], S.EleDef[3], S.EleDef[4], S.EleDef[5]);
	OnSelfStatsChanged.Broadcast();
}

void UAsdaNetSubsystem::SetSelfBaseStatsFrom36(const uint8* Block, int32 Avail, const TCHAR* Src)
{
	if (!Block || Avail < 36)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[статы] стат-блок 36 б (канал %s): доступно %d б (<36) — базовые статы не обновляем"),
			Src ? Src : TEXT("?"), Avail);
		return;
	}
	FAsdaSelfBaseStats& S = SelfBaseStats;
	for (int32 i = 0; i < 6; ++i)
	{
		S.Base[i]  = AsdaBytes::RdI16(Block + 2 * i);
		S.Bonus[i] = AsdaBytes::RdI16(Block + 12 + 2 * i);
		S.Tail[i]  = AsdaBytes::RdI16(Block + 24 + 2 * i);
	}
	S.bValid = true;
	UE_LOG(LogAsdaNet, Log,
		TEXT("[статы] стат-блок 36 б (%s): база [%d %d %d %d %d %d], прибавка [%d %d %d %d %d %d]"),
		Src ? Src : TEXT("?"),
		S.Base[0], S.Base[1], S.Base[2], S.Base[3], S.Base[4], S.Base[5],
		S.Bonus[0], S.Bonus[1], S.Bonus[2], S.Bonus[3], S.Bonus[4], S.Bonus[5]);
	OnSelfStatsChanged.Broadcast();
}

FString UAsdaNetSubsystem::DumpVitals() const
{
	const double Now = VitalsNowSeconds();
	FString S = FString::Printf(TEXT("[Vitals] записей: %d (свой userIdx=%u)\n"), VitalsByUserIdx.Num(), SelfUserNum);
	for (const TPair<int32, FAsdaVitals>& P : VitalsByUserIdx)
	{
		const FAsdaVitals& V = P.Value;
		const float Pct = (V.HpMax > 0 && V.HpCur >= 0) ? (100.f * (float)V.HpCur / (float)V.HpMax) : -1.f;
		S += FString::Printf(TEXT("  userIdx=%-5d %s HP %d/%d (%.1f%%)  MP %d/%d  последний канал HP='%s'  серверный снапшот %s\n"),
			P.Key, AsdaSelf::IsSelfUserIdx(this, P.Key) ? TEXT("[СВОЙ]") : TEXT("      "),
			V.HpCur, V.HpMax, Pct, V.MpCur, V.MpMax, *V.LastHpSrc,
			(V.LastServerHpS >= 0.0) ? *FString::Printf(TEXT("%.1f с назад"), (float)(Now - V.LastServerHpS)) : TEXT("НЕ БЫЛО"));
	}
	return S;
}

// ════════════════════ ★★★ ПОЛ СВОЕГО ПЕРСОНАЖА: сведение источников ════════════════════
// Приоритет = порядок эталона. Локального игрока создаёт game_main.c:2801 из блока 1017
// (`g_aCharSelectData` → char+1052, пол = блок+27); блок char-select 0x3EE — тот же байт
// (charSelect_FillSlotRecords@0xF06310: v3[1079] = payload[59*i+26]) и служит запасным входом,
// когда 1017 ещё не пришёл (например, повторный спавн после травела при потере блока).
uint8 UAsdaNetSubsystem::ResolveSelfGender(FString* OutDiag) const
{
	// Запись char-list выбранного слота (может отсутствовать — тогда «нет»).
	//   ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Possess.BodyIdentity`): общей точкой — при одержимости своя запись с тем же номером слота
	//   принадлежит ДРУГОМУ персонажу, и запасной пол брался бы с него.
	const FAsdaCharRecord* Rec = AsdaSelf::FindSelectedCharRecord(this);
	const uint8 FromList = Rec ? Rec->Sex : 0;

	uint8 Result = 0;
	const TCHAR* Src = TEXT("нет данных");
	if (SelfGender == 1 || SelfGender == 2)                     { Result = SelfGender;         Src = TEXT("1017 блок+27"); }
	else if (SelfGenderFromSpawn == 1 || SelfGenderFromSpawn == 2) { Result = SelfGenderFromSpawn; Src = TEXT("снапшот 4005/4065"); }
	else if (FromList == 1 || FromList == 2)                    { Result = FromList;           Src = TEXT("char-list 0x3EE rec+22"); }
	else                                                        { Result = 1;                  Src = TEXT("УМОЛЧАНИЕ (муж) — источников нет"); }

	// Расхождение источников — всегда громко в лог: молчаливый выбор «не того» пола именно так и прятался.
	if (SelfGender != 0 && FromList != 0 && SelfGender != FromList)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[пол] РАСХОЖДЕНИЕ: 1017 блок+27 = %u, запись char-list (rawSlot %u) = %u — берём 1017 (эталон game_main.c:2801)"),
			(uint32)SelfGender, (uint32)LastSelectedRawSlot, (uint32)FromList);
	}
	if (SelfGenderFromSpawn != 0 && Result != SelfGenderFromSpawn)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[пол] РАСХОЖДЕНИЕ: снапшот появления даёт %u, выбрано %u (источник «%s»)"),
			(uint32)SelfGenderFromSpawn, (uint32)Result, Src);
	}
	if (Rec == nullptr)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[пол] запись char-list для rawSlot=%u НЕ НАЙДЕНА (слотов %d) — внешность соберётся по 1017"),
			(uint32)LastSelectedRawSlot, CharList.Num());
	}

	if (OutDiag)
	{
		*OutDiag = FString::Printf(
			TEXT("выбран %u (%s) | 1017 блок+27=%s | снапшот 4005/4065=%s | char-list rawSlot %u=%s"),
			(uint32)Result, Src,
			SelfGender ? *FString::Printf(TEXT("%u"), (uint32)SelfGender) : TEXT("нет"),
			SelfGenderFromSpawn ? *FString::Printf(TEXT("%u"), (uint32)SelfGenderFromSpawn) : TEXT("нет"),
			(uint32)LastSelectedRawSlot,
			Rec ? *FString::Printf(TEXT("%u"), (uint32)FromList) : TEXT("записи нет"));
	}
	return Result;
}

FString UAsdaNetSubsystem::DumpGenderSources() const
{
	auto Word = [](uint8 G) { return (G == 2) ? TEXT("ЖЕН") : (G == 1) ? TEXT("МУЖ") : TEXT("—"); };
	FString Diag;
	const uint8 Resolved = ResolveSelfGender(&Diag);
	FString S = TEXT("\n═══ ПОЛ ПЕРСОНАЖЕЙ: все источники ═══\n");
	S += FString::Printf(TEXT("СВОЙ: итог %s (%u). %s\n"), Word(Resolved), (uint32)Resolved, *Diag);
	S += FString::Printf(TEXT("  1017 блок: charId=%d, пол(+27)=%u %s, класс(+28)=%d, уровень(+30)=%u; внешность тела[72..75]: созвездие=%d причёска=%d цвет=%d лицо=%d\n"),
		SelectedCharId, (uint32)SelfGender, Word(SelfGender), SelfClassId, (uint32)SelfLevel,
		(int32)SelfConstellation, (int32)SelfHair, (int32)SelfHairColor, (int32)SelfFace);
	S += FString::Printf(TEXT("  снапшот появления (4005 body[37] / 4065 rec[54]): %u %s\n"),
		(uint32)SelfGenderFromSpawn, Word(SelfGenderFromSpawn));
	S += FString::Printf(TEXT("  выбранный rawSlot (1016) = %u\n"), (uint32)LastSelectedRawSlot);
	for (int32 i = 0; i < CharList.Num(); ++i)
	{
		const FAsdaCharRecord& R = CharList[i];
		if (R.IsEmpty()) { S += FString::Printf(TEXT("  char-list[%d]: пусто\n"), i); continue; }
		S += FString::Printf(TEXT("  char-list[%d]: rawSlot=%u '%s' пол(rec+22)=%u %s класс=%u ур.=%u причёска=%u цвет=%u лицо=%u%s\n"),
			i, (uint32)R.RawSlotId, *R.Name, (uint32)R.Sex, Word(R.Sex), (uint32)R.Job, (uint32)R.Level,
			(uint32)R.Hair, (uint32)R.Color, (uint32)R.Face,
			(R.RawSlotId == LastSelectedRawSlot) ? TEXT("   ← ВЫБРАН") : TEXT(""));
	}
	return S;
}

// Консольный дамп витальных значений: asda.Vitals.Dump
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld CmdAsdaVitalsDump(
	TEXT("asda.Vitals.Dump"),
	TEXT("Дамп HP/MP всех известных игроков (единый источник истины UAsdaNetSubsystem::VitalsByUserIdx)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { UE_LOG(LogAsdaNet, Warning, TEXT("[Vitals] NetSubsystem недоступен")); return; }
		UE_LOG(LogAsdaNet, Warning, TEXT("%s"), *Net->DumpVitals());
	}));
#endif // ASDA_DEV_CONSOLE

// ★ 26.10.2026 (bowfx) — разбор у объявления (AsdaNetSubsystem.h).
bool UAsdaNetSubsystem::SendDotDeadClaim8226(int32 MobRid)
{
	if (!bSelfUserNumKnown || MobRid < 0) { return false; }
	uint8 Pl[4];
	AsdaBytes::WrI16(Pl + 0, (int32)SelfUserNum);   // `LocalPlayer->effectSlots[111].field_5B8` — свой userNum
	AsdaBytes::WrI16(Pl + 2, MobRid);               // renderId моба
	return SendMd5Signed(8226, Pl, 4);
}

// ★ R5 (12.11.2026, дубли): ТЕЛО ПАКЕТА 8215 (17 байт) — одна сборка на оба отправителя. До R5 `SendSkillHit8215` (вид 0) и
//   `SendDash8215` (вид 1) держали дословные копии; различался только байт [0]. Раскладка, порядок записи и байты те же.
//   Комментарии обеих копий сохранены у строк: «попадание» — вид 0, «рывок» — вид 1.
static void AsdaFill8215(uint8 Pl[17], uint8 Kind, int32 CasterUserNum, int32 MobRid, int32 SkillCode, int32 TileX, int32 TileZ)
{
	FMemory::Memzero(Pl, 17);
	// Вид: 0 — `CounterAttack()` NpcServer (списать отложенный урон) — попадание; 1 — ветка «рывок» (0 — CounterAttack у других отправителей).
	Pl[0] = Kind;
	// попадание: userNum кастера (сервер подменяет отправителем — `sub_428020`); рывок: `*((_WORD*)pChar + 526)` — userNum кастера
	AsdaBytes::WrI16(Pl + 1, CasterUserNum);
	// `(u16)Target_GetSelectedUserIdx(pChar)` (МИСНЕЙМ: `*(WORD*)(pChar+190188)`) — userNum САМОГО кастера, i32. У наблюдателя pChar —
	//   сосед, поэтому не наш номер; сервер ([1..2] и [3..6]) всё равно подменяет отправителем (`sub_428020`).
	// попадание: [3..6] — сервер подменяет (`sub_5324F0`)
	const int32 CasterI32 = (int32)(uint16)CasterUserNum;
	FMemory::Memcpy(Pl + 3, &CasterI32, 4);
	AsdaBytes::WrI16(Pl + 7, MobRid);                  // renderId цели; рывок: `CControl_GetField236(target)`
	AsdaBytes::WrI16(Pl + 9, SkillCode);               // код навыка; рывок: 537
	Pl[11] = 1;
	Pl[12] = 0;
	AsdaBytes::WrI16(Pl + 13, TileX);                  // рывок: `helper_scaleIfNonzero(x)` — тайл X
	AsdaBytes::WrI16(Pl + 15, TileZ);                  // рывок: `ratioIfFieldSet(z)` — тайл Z
}

// ★ 05.11.2026 [ОРИГИНАЛ] подтверждение попадания 8215 вид 0 — разбор в объявлении. Раскладка — как у рывка (`netSendSkillCastGroundTarget`
//   пишет: [0] вид, [1..2] userNum, [3..6] charId, [7..8] renderId цели, [9..10] навык, [11] 1, [12] 0, [13..14]/[15..16] клетка).
bool UAsdaNetSubsystem::SendSkillHit8215(int32 CasterUserNum, int32 MobRid, int32 SkillCode, int32 TileX, int32 TileZ)
{
	if (!bSelfUserNumKnown || MobRid < 0) { return false; }
	uint8 Pl[17];
	AsdaFill8215(Pl, 0, CasterUserNum, MobRid, SkillCode, TileX, TileZ);   // ★ R5: общая сборка тела, вид 0 — попадание
	const bool bOk = SendMd5Signed(8215, Pl, 17);
	UE_LOG(LogAsdaNet, Log, TEXT("→8215 попадание: кастер userNum=%d моб rid=%d навык %d клетка=(%d,%d) -> %s"),
		CasterUserNum, MobRid, SkillCode, TileX, TileZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 8215 вид 0 (подтверждение попадания): моб rid=%d навык %d клетка (%d,%d) -> %s"), MobRid, SkillCode, TileX, TileZ,
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

// ★ 29.10.2026 (Charge) — разбор в объявлении. `CNetwork_Send(…, 33, 8215, 1, 1, 1, 0, 0)`: MD5(16) + 17 б, пад по умолчанию.
bool UAsdaNetSubsystem::SendDash8215(int32 CasterUserNum, int32 MobRid, int32 SkillCode, int32 TileX, int32 TileZ)
{
	if (!bSelfUserNumKnown || MobRid < 0) { return false; }
	uint8 Pl[17];
	AsdaFill8215(Pl, 1, CasterUserNum, MobRid, SkillCode, TileX, TileZ);   // ★ R5: общая сборка тела, вид 1 — рывок
	const bool bOk = SendMd5Signed(8215, Pl, 17);
	UE_LOG(LogAsdaNet, Log, TEXT("→8215 рывок: кастер userNum=%d моб rid=%d навык %d тайл=(%d,%d) -> %s"),
		CasterUserNum, MobRid, SkillCode, TileX, TileZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 8215 рывок: моб rid=%d отброшен на тайл (%d,%d), навык %d -> %s"), MobRid, TileX, TileZ, SkillCode, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

// ★ 29.10.2026 (Charge) — разбор в объявлении. Тело как у 4008 стопа, но `[0]=15` и без гейта `bActionPending`.
bool UAsdaNetSubsystem::SendSkillActionStop4008K15(float CurX, float CurZ)
{
	if (!bSelfUserNumKnown) { return false; }
	uint8 Body[43];
	FillMoveBody(Body, 15, SelfUserNum, 0, CurX, CurZ, CurX, CurZ, (uint16)-1);   // MD5(27) уже впереди
	// Конверт — тот же «сырой» MD5 без секрета; `SendMd5Signed` строит его сам из тела 27 б.
	const bool bOk = SendMd5Signed(4008, Body + 16, 27);
	UE_LOG(LogAsdaNet, Log, TEXT("→4008 вид 15 (рывок): pos=(%.0f,%.0f) userNum=%u -> %s"), CurX, CurZ, SelfUserNum, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4008 вид 15 (точка по прибытии рывка): поз=(%.0f,%.0f) -> %s"), CurX, CurZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}
