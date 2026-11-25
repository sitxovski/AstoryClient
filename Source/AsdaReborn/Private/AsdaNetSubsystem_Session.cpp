// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_Session.cpp — ★ 22.10.2026 часть UAsdaNetSubsystem, вынесенная из AsdaNetSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_net_2026-10-22/plan.json).
//   Шлюз исходящих (FAsdaOutgoingGate), «это я?» (AsdaSelf), конверт SendSecured, вход в логин-сервер,
//   сброс сессии, список/выбор/создание/удаление персонажа, вход в мир и переход (1028/1031/4003/4024/6084).
//   #include — те же, что у AsdaNetSubsystem.cpp, в том же порядке; общие хелперы — AsdaNetSubsystemInternal.h.
#include "AsdaNetSubsystem.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (4026/4028/5256)
#include "AsdaTimedSend.h"                     // ★ 15.10.2026: поток точной отправки (стендовая постановка 4028)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "World/AsdaWorldSubsystem.h"
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
// 5055 отказ портала: нотисы 1096/1097 в системный чат   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNotice.h"              // ★ 27.09: отказы 5055/6201 — плашка 221 и пара «плашка + журнал» (общие точки)
#include "UI/AsdaChatSubsystem.h"       // ★ 27.09: ColGold() = эталонный −11514 отказов перехода
#include "UI/AsdaHotbarSubsystem.h"     // сохранение раскладки панели перед переходом (hotbar_SendAllLayouts)
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaCampWarSubsystem.h"         // ★ 09.11.2026: слоты заявок CampWar в теле 5058 (эталон net_sendSpawnFieldSelect)
#include "World/AsdaPossessGate.h"      // ★ гейт действий при одержимости (SoulState==2), см. шапку заголовка
#include "AsdaOutgoingGate.h"               // ★ единый шлюз исходящих пакетов (E1, 14.09.2026)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (итог и `quit` диагностики asda.Gm.TestEntryHide)
#include "UI/AsdaEntryWindows.h"               // ★ 22.10.2026: окна при загрузке карты — список эталона
#include "AsdaSelf.h"                       // ★ «это я?» — единая точка своего номера/уровня (E2, 14.09.2026)
#include "World/AsdaCoord.h"                 // ★ единый перевод UE↔игра (Аудит №3 B1) — для SendStop4008AtActor
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"           // ★ 09.11.2026: самотест 5058 (проверка после разбора 6707)
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
#include "World/AsdaKeybindConfig.h"      // ★ 26.10.2026: отметка «самотест допущен» для предохранителя файла раскладки

// ★ 25.10.2026: ключ `asda.Net.GameConnectFailDisconnect` (AsdaNetSubsystem.cpp) — неудача подключения к game-серверу = разрыв с нотисом 1001.
bool AsdaNetGameConnectFailDisconnectOn();
// ★ 26.10.2026: ключ `asda.Net.ConnectFailOnlyPending` (AsdaNetSubsystem.cpp) — ветка «неудача подключения» только для OnClosed несостоявшегося подключения.
bool AsdaNetConnectFailOnlyPendingOn();


// ★ EAC Э1: MD5-hex пароля — ОДНА точка для тела 1003 (cred+81) и для AsdaId (`password_md5`, CONTRACT.md §1):
//   сервис входа EAC проверяет тот же хеш, что LoginManager (UPUserLogin P52), поэтому значения обязаны совпадать.
//   ★ 19.10.2026 (ревизия безопасности журналов, М11): байт в байт как FMD5::HashAnsiString (TCHAR_TO_ANSI + MD5 → 32 строчных
//   hex), но ANSI-копия пароля и дайджест затираются сразу после хеша — пакет падения (minidump) их уже не застанет.
static FString AsdaLoginPasswordMd5Hex(const FString& Password)
{
	auto Ansi = StringCast<ANSICHAR>(*Password);
	uint8 Digest[16];
	FMD5 Md5Gen;
	Md5Gen.Update(reinterpret_cast<const uint8*>(Ansi.Get()), FCString::Strlen(*Password));
	Md5Gen.Final(Digest);
	FString Hex;
	for (int32 i = 0; i < 16; ++i) { Hex += FString::Printf(TEXT("%02x"), Digest[i]); }
	FPlatformMemory::Memzero(const_cast<ANSICHAR*>(Ansi.Get()), Ansi.Length());
	FPlatformMemory::Memzero(Digest, sizeof(Digest));
	FPlatformMemory::Memzero(&Md5Gen, sizeof(Md5Gen));
	return Hex;   // 32-символьный hex MD5 (строчные)
}

// ★ 19.10.2026 (М11): затереть строку с учётными данными (MD5-hex пароля) в памяти и очистить её.
static void AsdaWipeSecretString(FString& S)
{
	TArray<TCHAR, FString::AllocatorType>& A = S.GetCharArray();
	if (A.Num() > 0) { FPlatformMemory::Memzero(A.GetData(), A.Num() * sizeof(TCHAR)); }
	S.Empty();
}

// ═══ ★★★ ЕДИНЫЙ ШЛЮЗ ИСХОДЯЩИХ ПАКЕТОВ (E1, аудит дублей 14.09.2026) ═════════════════════════════
//   Объявление, реверс оригинала и маски ВСЕХ отправителей — `Public/AsdaOutgoingGate.h`.
//   Шлюз зовётся ДО сборки тела и отвечает только «слать / не слать» — байты пакетов он не трогает.
static ASDA_DEV_CVAR(int32, CVarTransferGateAllChannels,
	TEXT("asda.Net.TransferGateAllChannels"),
	1,
	TEXT("Гейт перехода карты/канала. 1 = во ВСЕХ каналах отправки (умолчание: оригинал в окне перехода игровых "
	     "пакетов не порождает — кадр загрузки game_main.c:3782 рисует только полосу и звук, логоны уходят в шаге "
	     "смены слота сокета :3092/:3596); 0 = как до 14.09.2026 — гейт перехода только у SendSecured."),
	ECVF_Default);

bool FAsdaOutgoingGate::IsTransferWideEnabled()
{
	return CVarTransferGateAllChannels.GetValueOnAnyThread() != 0;
}

bool FAsdaOutgoingGate::IsTransferExempt(uint16 Opcode)
{
	return Opcode == (uint16)OP_ACCEPT_EX_USER            // 20000 — хендшейк нового соединения
		|| Opcode == (uint16)OP_CHAR_PORTAL_LOGON_REQ     // 5058  — портальный логон (сам снимает гейт)
		|| Opcode == (uint16)OP_CHAR_LOGON_REQ;           // 4001  — логон входа в мир (сам снимает гейт)
	// 5056 здесь НЕ значится: его отправитель `SendGameAck5056` идёт мимо шлюза (см. шапку `AsdaOutgoingGate.h`).
}

// GameInstance из контекста офлайн-гейта: мир, сам GI, подсистема GI или любой объект с миром. nullptr — не нашли.
static UGameInstance* ResolveDebugGateGameInstance(const UObject* Ctx)
{
	if (!Ctx) { return nullptr; }
	if (const UGameInstance* GI = Cast<UGameInstance>(Ctx)) { return const_cast<UGameInstance*>(GI); }
	if (const UGameInstanceSubsystem* S = Cast<UGameInstanceSubsystem>(Ctx)) { return S->GetGameInstance(); }
	if (const UWorld* W = Cast<UWorld>(Ctx)) { return W->GetGameInstance(); }
	if (const UWorld* W = Ctx->GetWorld()) { return W->GetGameInstance(); }
	return nullptr;
}

bool FAsdaOutgoingGate::RefuseDebugWhenOnline(const UObject* Ctx, const TCHAR* Cmd, float GuardSeconds,
	TArrayView<const uint16> GuardOpcodes)
{
	// ★ С-Р1 — объявление, назначение и граница применения в `Public/AsdaOutgoingGate.h`.
	UGameInstance* GI = ResolveDebugGateGameInstance(Ctx);
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("[офлайн-гейт] %s: нет сетевой подсистемы — состояние соединения неизвестно, отказ"),
			Cmd ? Cmd : TEXT("?"));
		return true;
	}
	if (Net->IsConnected())
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("[офлайн-гейт] %s: команда только для офлайна — есть живое соединение с сервером, отказ (ничего не отправлено)"),
			Cmd ? Cmd : TEXT("?"));
		return true;
	}
	if (GuardSeconds > 0.f && GuardOpcodes.Num() > 0)
	{
		const double Now = FPlatformTime::Seconds();
		// Окно истекло — прежний набор опкодов больше не действует (не копим его между прогонами).
		if (Net->DebugSendGuardUntil <= Now) { Net->DebugSendGuardOpcodes.Reset(); }
		// Окно не короче уже взведённого: проходы одной строки -ExecCmds запускаются с разными задержками.
		Net->DebugSendGuardUntil = FMath::Max(Net->DebugSendGuardUntil, Now + (double)GuardSeconds);
		for (const uint16 Op : GuardOpcodes) { Net->DebugSendGuardOpcodes.Add(Op); }
	}
	// ★ Р-2 (ревизия shopdbl2, 10.10.2026): самотест допущен офлайн — раннее автоподключение к логин-серверу в этом
	//   процессе больше не поднимается (иначе через ~10 с соединение появится посреди `delay=N`). Ручной вход снимает.
	if (Net->OfflineSelfTestLatch.IsEmpty())
	{
		UE_LOG(LogAsdaNet, Log, TEXT("[офлайн-гейт] %s допущен офлайн — раннее автоподключение к серверу в этом процессе отключено"),
			Cmd ? Cmd : TEXT("?"));
	}
	Net->OfflineSelfTestLatch = Cmd ? Cmd : TEXT("?");
	// ★★ МЕЛК-2 (ревизия shop_offline_fix, 10.10.2026): учёт допущенного самотеста — событие соединения его прервёт.
	Net->OfflineSelfTestsAdmitted.FindOrAdd(Net->OfflineSelfTestLatch);
	// ★ 26.10.2026 (ревизия wave_1025d п. 7): процесс — самотест-запуск; предохранитель раскладки (`ResolveShortcutkeyIniPath`) при
	//   `-AsdaNoNet` уводит Shortcutkey.ini в Saved, если песочницы нет.
	UAsdaKeybindConfig::NoteSelfTestAdmitted();
	return false;
}

bool FAsdaOutgoingGate::RefuseDeferredStartWhenOnline(const UObject* Ctx, const TCHAR* Cmd, bool bQuit)
{
	// ★ Р-2 — объявление и назначение в `Public/AsdaOutgoingGate.h`.
	UGameInstance* GI = ResolveDebugGateGameInstance(Ctx);
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOnline = !Net || Net->IsConnected();   // без сетевой подсистемы состояние неизвестно — отказ, как у RefuseDebugWhenOnline
	if (!bOnline)
	{
		// ★★ МЕЛК-2 (10.10.2026): самотест стартует офлайн — запомнить, что при прерывании соединением процесс надо закрыть.
		bool& bQuitOnAbort = Net->OfflineSelfTestsAdmitted.FindOrAdd(Cmd ? Cmd : TEXT("?"));
		bQuitOnAbort = bQuitOnAbort || bQuit;
		return false;
	}
	const FString Msg = FString::Printf(
		TEXT("[офлайн-гейт] %s ИТОГ: ОТКАЗ на отложенном старте — %s; ничего не отправлено. Прогонять с ключом -AsdaNoNet "
		     "(правило `Docs/Port/systems/SELFTESTS_OFFLINE.md`)"),
		Cmd ? Cmd : TEXT("?"), Net ? TEXT("за время задержки появилось живое соединение с сервером") : TEXT("нет сетевой подсистемы"));
	UE_LOG(LogAsdaNet, Warning, TEXT("%s"), *Msg);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Orange, Msg); }
	if (bQuit && GEngine)
	{
		UWorld* W = GI ? GI->GetWorld() : nullptr;   // мир для консольной `quit`
		GEngine->Exec(W, TEXT("quit"));
	}
	return true;
}

void FAsdaOutgoingGate::OnConnectionAppeared(UAsdaNetSubsystem* Net, const TCHAR* Where)
{
	// ★★ МЕЛК-2 — объявление и назначение в `Public/AsdaOutgoingGate.h`.
	if (!Net || Net->OfflineSelfTestsAdmitted.Num() == 0) { return; }   // обычная игра: самотестов в процессе не было
	bool bQuit = false;   // хоть один допущенный самотест запущен с `quit`
	FString Names;        // имена через запятую — для итога
	for (const TPair<FString, bool>& It : Net->OfflineSelfTestsAdmitted)
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + It.Key;
		bQuit = bQuit || It.Value;
	}
	Net->OfflineSelfTestsAdmitted.Reset();
	Net->bOfflineSelfTestAborted = true;
	const FString Msg = FString::Printf(
		TEXT("[офлайн-гейт] ИТОГ: ПРЕРВАН — посреди офлайн-самотеста появилось соединение (%s). Допущенные в процессе самотесты: %s. "
		     "До входа в мир (1021) в сеть уходят только пакеты входа, остальные отправки закрыты%s"),
		Where ? Where : TEXT("?"), *Names, bQuit ? TEXT("; самотест запущен с quit — процесс закрывается") : TEXT(""));
	UE_LOG(LogAsdaNet, Warning, TEXT("%s"), *Msg);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Orange, Msg); }
	if (bQuit && GEngine) { GEngine->DeferredCommands.Add(TEXT("quit")); }   // не изнутри цепочки подключения
}

bool FAsdaOutgoingGate::IsSessionEntryOpcode(uint16 Opcode)
{
	// ★★ МЕЛК-2 — список и назначение в `Public/AsdaOutgoingGate.h` (отправители: SendKeepAlive, SendLogin,
	//   SendCharListRequest, SendResetState3, SendChannelSelect, SendCreateChar, SendSelectChar, SendDeleteChar,
	//   SendReturnToCharSelect, SendCharLogonReq, SendGameAck5056, SendCharPortalLogonReq, SendEnterWorldHandshake).
	switch (Opcode)
	{
	case 1000: case 1003: case 1005: case 1010: case 1012: case 1014: case 1016: case 1018: case 1028:
	case 4001: case 5056: case 5058: case 20000:
		return true;
	default:
		return false;
	}
}

bool FAsdaOutgoingGate::IsSendVetoedByAbortedSelfTest(const UAsdaNetSubsystem* Net, uint16 Opcode)
{
	// ★★ МЕЛК-2 — объявление и назначение в `Public/AsdaOutgoingGate.h`.
	if (!Net || !Net->bOfflineSelfTestAborted || IsSessionEntryOpcode(Opcode)) { return false; }
	UE_LOG(LogAsdaNet, Warning,
		TEXT("[офлайн-гейт] опкод %u НЕ отправлен: офлайн-самотест прерван появившимся соединением, до входа в мир (1021) "
		     "уходят только пакеты входа"), (uint32)Opcode);
	return true;
}

bool FAsdaSelfTestRunGate::Pass(const UObject* Ctx)
{
	// ★★ СРЕДН-1 — объявление и назначение в `Public/AsdaOutgoingGate.h`.
	if (State == 0) { State = FAsdaOutgoingGate::RefuseDeferredStartWhenOnline(Ctx, *Cmd, bQuit) ? -1 : 1; }
	return State > 0;
}

bool FAsdaOutgoingGate::IsDebugSendWindowOpen(const UAsdaNetSubsystem* Net, uint16 Opcode)
{
	if (!Net || Net->DebugSendGuardUntil <= 0.0) { return false; }                  // окна не было — обычная игра
	if (Net->DebugSendGuardUntil <= FPlatformTime::Seconds()) { return false; }     // окно истекло
	if (Net->DebugSendGuardOpcodes.Contains(Opcode)) { return true; }
	// ★ 11.10.2026 (дедуп №2, В3 S1, N1(б)): метка 0 (`AsdaSelfTestKit::kGuardAllOpcodes`, опкода 0 у протокола нет) — окно
	//   закрывает ВСЕ опкоды, кроме пути входа игрока: второй барьер у каждого самотеста каркаса, а не у 6 из 42.
	return Opcode != 0 && Net->DebugSendGuardOpcodes.Contains((uint16)0) && !IsSessionEntryOpcode(Opcode);
}

bool FAsdaOutgoingGate::IsDebugSendGuarded(const UAsdaNetSubsystem* Net, uint16 Opcode)
{
	// ★ 15.10.2026: провод самотеста (`DebugAttachTestWire`) — не живое соединение: в сеть ничего не уходит.
	if (!IsDebugSendWindowOpen(Net, Opcode) || !Net->IsConnected() || Net->IsTestWireActive()) { return false; }
	// ★ Редакция 1 (МЕЛК-4): отказ заметен — Warning (виден в консоли) и строка на экране, а не только Error в файле лога.
	const FString Msg = FString::Printf(
		TEXT("[офлайн-гейт] пакет %u НЕ отправлен: идёт самотест, запущенный офлайн, а соединение с сервером живое (ещё %.0f с)"),
		(uint32)Opcode, Net->DebugSendGuardUntil - FPlatformTime::Seconds());
	UE_LOG(LogAsdaNet, Warning, TEXT("%s"), *Msg);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Orange, Msg); }
	return true;
}

bool FAsdaOutgoingGate::HasOnlineOptIn(const TArray<FString>& Args)
{
	return Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("online"), ESearchCase::IgnoreCase); });
}

bool FAsdaOutgoingGate::RefuseRiskyWithoutOnlineOptIn(const UObject* Ctx, const TCHAR* Cmd, const TArray<FString>& Args, const TCHAR* Why)
{
	// ★ С-Р1, редакция 1 (СРЕДН-4) — объявление и список команд в `Public/AsdaOutgoingGate.h`.
	if (HasOnlineOptIn(Args)) { return false; }                     // явное согласие оператора
	UGameInstance* GI = ResolveDebugGateGameInstance(Ctx);
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && !Net->IsConnected()) { return false; }              // офлайн — как раньше
	const FString Msg = FString::Printf(
		TEXT("[офлайн-гейт] %s: %s — на живом сервере только с явным аргументом `online` (%s … online). Ничего не отправлено."),
		Cmd ? Cmd : TEXT("?"), Why ? Why : TEXT("необратимое действие"), Cmd ? Cmd : TEXT("?"));
	UE_LOG(LogAsdaNet, Warning, TEXT("%s"), *Msg);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Orange, Msg); }
	return true;
}

void FAsdaOutgoingGate::ClearDebugSendWindow(UAsdaNetSubsystem* Net, const TCHAR* Why)
{
	// ★★ МЕЛК-2 (ревизия shop_offline_fix, 10.10.2026): настоящий вход в мир снимает и вето прерванного самотеста —
	//   уровень логина с его таймерами самотестов уходит, дальше идёт обычная игра.
	if (Net && Net->bOfflineSelfTestAborted)
	{
		Net->bOfflineSelfTestAborted = false;
		UE_LOG(LogAsdaNet, Log, TEXT("[офлайн-гейт] вето прерванного самотеста снято (%s)"), Why ? Why : TEXT("?"));
	}
	if (!Net || (Net->DebugSendGuardUntil <= 0.0 && Net->DebugSendGuardOpcodes.Num() == 0)) { return; }
	UE_LOG(LogAsdaNet, Log, TEXT("[офлайн-гейт] окно защиты самотестов снято (%s): было опкодов %d"),
		Why ? Why : TEXT("?"), Net->DebugSendGuardOpcodes.Num());
	Net->DebugSendGuardUntil = 0.0;
	Net->DebugSendGuardOpcodes.Reset();
}

bool FAsdaOutgoingGate::Block(const UAsdaNetSubsystem* Net, uint16 Opcode, EAsdaGate Mask)
{
	if (!Net) { return false; }

	// ★ С-Р1, второй барьер офлайн-гейта отладочных команд: опкод самотеста в живое соединение не уходит, пока
	//   открыто окно защиты. Окно взводит только самотест офлайн (`RefuseDebugWhenOnline`) — обычную игру не задевает.
	if (IsDebugSendGuarded(Net, Opcode)) { return true; }

	// ★★★ ГЕЙТ ИСХОДЯЩИХ В РЕЖИМЕ XyT. Их сборка сервера знает не все опкоды нашего клиента: пакет с
	//   неизвестным опкодом падает в `default`, счётчик неопознанных переполняется — kick 4101 code=15.
	//   Список правится без пересборки (`asda.XyT.BlockedOpcodes`), разбор — у `IsOpcodeBlockedXyt`.
	if (EnumHasAnyFlags(Mask, EAsdaGate::XyT) && Net->IsOpcodeBlockedXyt(Opcode))
	{
		UE_LOG(LogAsdaNet, Log, TEXT("[XyT] опкод %u не отправлен: их сервер его не знает (гейт asda.XyT.BlockedOpcodes)"),
			(uint32)Opcode);
		return true;
	}
	// ★★★ ГЕЙТ ОДЕРЖИМОСТИ (порт серверного `sub_52EE40 @0x52EE40`). Играя телом соулмейта, игрок не вправе
	//   отчуждать чужое имущество и рвать чужие связи; нотис — строкой оригинала (`World/AsdaPossessGate.h`;
	//   ключи asda.Possess.ActionGate / asda.Possess.SilentBlock).
	if (EnumHasAnyFlags(Mask, EAsdaGate::Possess) && AsdaPossess::BlockOutgoing(Net, Opcode)) { return true; }
	// ★★★ ГЕЙТ «ДУШИ» (порт серверного `sub_52EEA0 @0x52EEA0`, SoulState==1). Сервер такие пакеты МОЛЧА
	//   проглатывает в диспетчере (ключи asda.Soul.ActionGate / asda.Soul.SilentBlock).
	if (EnumHasAnyFlags(Mask, EAsdaGate::Soul) && AsdaSoul::BlockOutgoing(Net, Opcode)) { return true; }
	// ★★★ ГЕЙТ GM-«СКРЫТЬСЯ» (поле `CUser+0x3BC3B == 300`). Сервер молча глотает атаку/навыки (ключ
	//   asda.Gm.HideAttackGate). Движение 4024 в таблицу сознательно не внесено.
	if (EnumHasAnyFlags(Mask, EAsdaGate::GmHide) && AsdaGmHide::BlockOutgoing(Net, Opcode)) { return true; }
	// ★★★ ГЕЙТ ПЕРЕХОДА (найдено живым разбором 24.08.2026). Пока идёт смена карты/канала, соединение уже
	//   новое, но игрок на нём ЕЩЁ НЕ ОПОЗНАН: логон 5058/4001 уходит только после загрузки уровня. Любой
	//   игровой пакет в этот промежуток сервер считает мусором от неизвестного и РВЁТ СВЯЗЬ — в логе это было
	//   «карта не загрузилась, вокруг пустота» (поймано на автопути, слал 4006/4008 после начала перехода).
	//   ⚠ ГЕЙТ РАБОТАЕТ ТОЛЬКО ВО ВРЕМЯ САМОГО ПЕРЕХОДА (`bTransferInProgress`), и ни секундой дольше.
	//   Первая редакция проверяла `!bInGameConnected` — и заблокировала ВЕСЬ ВХОД В ИГРУ: на экране логина
	//   игровой сессии ещё нет, и наружу перестали уходить выбор канала 1012 и запрос списка 1005 (живая
	//   проверка владельца: «зайти в игру не получилось»). Пропускаем то, чем промежуток перехода и живёт —
	//   см. `IsTransferExempt`. Бит `Transfer` действует всегда, `TransferWide` — под ключом
	//   asda.Net.TransferGateAllChannels (доказательство из эталона — в шапке `AsdaOutgoingGate.h`).
	if (Net->bTransferInProgress && !IsTransferExempt(Opcode)
		&& (EnumHasAnyFlags(Mask, EAsdaGate::Transfer)
			|| (EnumHasAnyFlags(Mask, EAsdaGate::TransferWide) && IsTransferWideEnabled())))
	{
		UE_LOG(LogAsdaNet, Verbose,
			TEXT("[переход] пакет %u не отправлен: игрок ещё не опознан на новом соединении"), Opcode);
		return true;
	}
	return false;
}

// ═══ ★★★ «ЭТО Я?» — ЕДИНАЯ ТОЧКА СВОЕГО НОМЕРА / ПЕРСОНАЖА / УРОВНЯ (E2, 14.09.2026) ═════════════════
//   Объявление и доказательство из эталона — `Public/AsdaSelf.h`.
static ASDA_DEV_CVAR(int32, CVarSelfStrictUnknownNum,
	TEXT("asda.Self.StrictUnknownNum"),
	1,
	TEXT("«Это я?» до пакета 4003. 1 = свой номер ещё не известен — «своим» не считается никто (умолчание: оригинал "
	     "узнаёт себя по сущности, CharManager_FindByUserIdx entity_char.c:6272); 0 = как до 14.09.2026 — открытое "
	     "сравнение с SelfUserNum (до 4003 это 0, и чужой с номером 0 принимался за своего). Действует только на места, "
	     "переведённые 14.09 на AsdaSelf::IsSelfUserIdx."),
	ECVF_Default);

bool AsdaSelf::IsSelfUserIdx(const UAsdaNetSubsystem* Net, int32 UserIdx)
{
	if (!Net) { return false; }
	if (CVarSelfStrictUnknownNum.GetValueOnAnyThread() == 0) { return UserIdx == (int32)Net->SelfUserNum; }
	return Net->IsSelfUserIdx(UserIdx);
}

bool AsdaSelf::IsSelfUserIdx(const UGameInstance* GameInstance, int32 UserIdx)
{
	if (!GameInstance) { return false; }
	// `GetSubsystem` неконстантен — снимаем константность только ради чтения (как `AsdaGmHide::IsHidden`).
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return IsSelfUserIdx(GI->GetSubsystem<UAsdaNetSubsystem>(), UserIdx);
}

bool AsdaSelf::IsUserIdxKnown(const UAsdaNetSubsystem* Net)
{
	return Net != nullptr && Net->IsSelfUserNumKnown();
}

int32 AsdaSelf::GetUserIdxOr(const UAsdaNetSubsystem* Net, int32 Fallback)
{
	if (!Net) { return Fallback; }
	// Ключ отката `asda.Self.StrictUnknownNum 0` — как до 14.09.2026: номер без признака «известен» (0 до 4003).
	if (CVarSelfStrictUnknownNum.GetValueOnAnyThread() == 0) { return (int32)Net->SelfUserNum; }
	return Net->IsSelfUserNumKnown() ? (int32)Net->SelfUserNum : Fallback;
}

int32 AsdaSelf::GetUserIdxOr(const UGameInstance* GameInstance, int32 Fallback)
{
	if (!GameInstance) { return Fallback; }
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return GetUserIdxOr(GI->GetSubsystem<UAsdaNetSubsystem>(), Fallback);
}

int32 AsdaSelf::GetCharId(const UAsdaNetSubsystem* Net)
{
	return Net ? Net->SelectedCharId : 0;
}

// ★★★ 30.10.2026 [ИСПРАВЛЕНИЕ] — ЗАПИСЬ ВЫБРАННОГО ПЕРСОНАЖА ПРИ ОДЕРЖИМОСТИ (баг игроков «вход одержимым показывает не ту модель
//   и не то имя»). `LastSelectedRawSlot` — сырой Charidx, ушедший в 1016. При одержимости (режим 2) это Charidx персонажа СОУЛМЕЙТА
//   (10/11/12 его аккаунта), а `CharList` — персонажи СВОЕЙ учётки с теми же номерами 10/11/12: поиск находил своего персонажа с тем
//   же номером, и мир собирал павна с его именем, вещами, HP, свечением оружия, а смена вещей переписывала его запись. Тела в своём
//   списке нет вовсе — оригинал своё тело берёт из 1017/1027 (`game_main.c:2801/2807`), char-list не читает.
//   1 = при `SelfSoulState == 2` записи нет (вызывающие берут тело из 1017/1027/1024, см. `TrySpawnLocalPlayer`); 0 = прежний поиск.
static ASDA_DEV_CVAR(int32, CVarPossessBodyIdentity,
	TEXT("asda.Possess.BodyIdentity"), 1,
	TEXT("1 (по умолчанию) — при одержимости свой персонаж в мире собирается телом соулмейта (имя из 1017, вещи из 1027/1024), ")
	TEXT("запись своего char-list с тем же номером слота не используется; 0 — прежний порт (своё имя и вещи на чужом теле)."),
	ECVF_Default);
// Значение `asda.Possess.BodyIdentity` для других частей (спавн своего павна в AsdaWorldSubsystem.cpp).
bool AsdaPossessBodyIdentityOn() { return CVarPossessBodyIdentity.GetValueOnGameThread() != 0; }

const FAsdaCharRecord* AsdaSelf::FindSelectedCharRecord(const UAsdaNetSubsystem* Net)
{
	if (!Net) { return nullptr; }
	if (Net->SelfSoulState == 2 && AsdaPossessBodyIdentityOn()) { return nullptr; }   // ★ 30.10.2026: тела в своём списке нет (разбор выше)
	for (const FAsdaCharRecord& R : Net->CharList)
	{
		if (!R.IsEmpty() && R.RawSlotId == Net->LastSelectedRawSlot) { return &R; }
	}
	return nullptr;
}

int32 AsdaSelf::GetLevel(const UAsdaNetSubsystem* Net)
{
	if (!Net) { return 0; }
	if (Net->SelfLevel > 0) { return (int32)Net->SelfLevel; }
	const FAsdaCharRecord* R = FindSelectedCharRecord(Net);
	return R ? (int32)R->Level : 0;
}

int32 AsdaSelf::GetLevel(const UGameInstance* GameInstance)
{
	if (!GameInstance) { return 0; }
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return GetLevel(GI->GetSubsystem<UAsdaNetSubsystem>());
}

// ★ Дедуп №2, В2-A (U5d): класс своего персонажа — разбор у объявления (`AsdaSelf.h`).
int32 AsdaSelf::GetClassId(const UAsdaNetSubsystem* Net)
{
	if (!Net) { return -1; }
	if (Net->SelfClassId >= 0) { return Net->SelfClassId; }
	const FAsdaCharRecord* R = FindSelectedCharRecord(Net);
	return R ? (int32)R->Job : -1;
}

int32 AsdaSelf::GetClassId(const UGameInstance* GameInstance)
{
	if (!GameInstance) { return -1; }
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return GetClassId(GI->GetSubsystem<UAsdaNetSubsystem>());
}

// ★ 26.10.2026 — подкласс своего персонажа — разбор у объявления (`AsdaSelf.h`).
int32 AsdaSelf::GetSubJob(const UAsdaNetSubsystem* Net)
{
	if (!Net) { return -1; }
	if (Net->SelfSubJob >= 0) { return Net->SelfSubJob; }
	const FAsdaCharRecord* R = FindSelectedCharRecord(Net);
	return R ? (int32)R->SubClass : -1;
}

int32 AsdaSelf::GetSubJob(const UGameInstance* GameInstance)
{
	if (!GameInstance) { return -1; }
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return GetSubJob(GI->GetSubsystem<UAsdaNetSubsystem>());
}

// ★★★ 26.10.2026 [ИСПРАВЛЕНИЕ] — ступень 4-й профессии из подкласса (разбор у объявления, `AsdaSelf.h`).
static ASDA_DEV_CVAR(int32, CVarJob4StageFromSubJob,
	TEXT("asda.Job4.StageFromSubJob"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — 4-я профессия (вкладка навыков 12400, флажок 16521 окна 16500, ветка 10/11/12, класс в тексте 16502) ")
	TEXT("определяется по подклассу, если он ≠ 0 (сервер пишет 4-ю ступень в подкласс, основной класс остаётся 3/14/25); ")
	TEXT("0 — как эталон: только по основному классу."),
	ECVF_Default);

bool AsdaSelf::IsJob4StageFromSubJobOn()
{
	return CVarJob4StageFromSubJob.GetValueOnGameThread() != 0;
}

bool AsdaSelf::IsJob4Class(int32 C)
{
	return (C >= 4 && C <= 7) || (C >= 15 && C <= 18) || (C >= 26 && C <= 29) || (C >= 34 && C <= 39);
}

int32 AsdaSelf::Job4GradeOfClass(int32 C)
{
	if (C >= 4 && C <= 7)   { return 10; }
	if (C >= 15 && C <= 18) { return 11; }
	if (C >= 26 && C <= 29) { return 12; }
	return -1;
}

int32 AsdaSelf::BandOfClass(int32 C)
{
	if (C >= 1 && C <= 11)  { return 1; }
	if (C >= 12 && C <= 22) { return 2; }
	if (C >= 23 && C <= 33) { return 3; }
	return 0;
}

bool AsdaSelf::IsCraftClass(int32 C)
{
	return C >= 34 && C <= 39;
}

int32 AsdaSelf::Job4StageClass(int32 ClassId, int32 SubJob)
{
	if (!IsJob4StageFromSubJobOn() || SubJob <= 0) { return ClassId; }
	// Только боевой класс 4-й ступени ТОЙ ЖЕ полосы: ремесленный 34…39 (полоса 0) и чужая полоса вкладку не меняют.
	const int32 Band = BandOfClass(ClassId);
	if (Band > 0 && Job4GradeOfClass(SubJob) > 0 && BandOfClass(SubJob) == Band) { return SubJob; }
	return ClassId;
}

int32 AsdaSelf::GetJob4StageClass(const UAsdaNetSubsystem* Net)
{
	return Job4StageClass(GetClassId(Net), GetSubJob(Net));
}

int32 AsdaSelf::GetJob4StageClass(const UGameInstance* GameInstance)
{
	if (!GameInstance) { return -1; }
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return GetJob4StageClass(GI->GetSubsystem<UAsdaNetSubsystem>());
}

// ★ Дедуп №2, В2-A (N2): «charId — мой?» с явной политикой «свой ещё неизвестен» — разбор у объявления (`AsdaSelf.h`).
bool AsdaSelf::IsSelfCharId(const UAsdaNetSubsystem* Net, int32 CharId, EAsdaUnknownSelf WhenUnknown)
{
	const int32 Mine = Net ? (int32)Net->SelectedCharId : 0;
	if (Mine == 0)
	{
		if (WhenUnknown == EAsdaUnknownSelf::AcceptAll) { return true; }
		// Ключ отката `asda.Self.StrictUnknownNum 0` — как до 11.10.2026: открытое сравнение (0 == 0 — «мой»).
		if (Net && CVarSelfStrictUnknownNum.GetValueOnAnyThread() == 0) { return CharId == Mine; }
		return false;
	}
	return CharId == Mine;
}

bool AsdaSelf::IsSelfCharId(const UGameInstance* GameInstance, int32 CharId, EAsdaUnknownSelf WhenUnknown)
{
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return IsSelfCharId(GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr, CharId, WhenUnknown);
}

// ★ Связь для отправителей, переведённых на `FAsdaPacketWriter` (волна 3 п.10): сокет жив ЛИБО на объекте стоит
//   перехватчик стенда. В игре перехватчик пуст ⇒ ровно прежнее `Conn && Conn->IsConnected()`.
bool UAsdaNetSubsystem::IsSendLinkUp() const
{
	return (Conn && Conn->IsConnected()) || IsStandCaptureAll();   // ★ волна 14: узкий перехват связь не подменяет
}

bool UAsdaNetSubsystem::SendSecured(uint16 Opcode, const uint8* Payload, int32 Len, bool bPad)
{
	// ★ Стенд `asda.Net.PacketStand` (волна 3 п.10, `AsdaPacketStand.cpp`): при стоящем перехватчике сокета нет,
	//   а гейты — про состояние живой сессии (XyT/одержимость/«душа»/переход), не про тело, поэтому оба шага
	//   пропускаются; всё, что влияет на тело (кламп длины), стенд проходит тем же путём, что игра, и перехват
	//   стоит ПОСЛЕ него. В игре перехватчик ПУСТ ⇒ порядок прежний: сокет → гейты → длина.
	const bool bStand = IsStandCapture(Opcode);   // ★ волна 14 (МЕЛК-3): узкий перехват берёт только свой опкод
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		// ★★★ ГЕЙТЫ — ЕДИНЫЙ ШЛЮЗ (E1, 14.09.2026): XyT → одержимость → «душа» → переход, ровно прежняя цепочка
		//   этой точки. Разбор каждого гейта (и история гейта перехода: первая редакция по `!bInGameConnected`
		//   заблокировала весь вход в игру) — у `FAsdaOutgoingGate::Block` и в `Public/AsdaOutgoingGate.h`.
		if (FAsdaOutgoingGate::Block(this, Opcode, EAsdaGate::XyT | EAsdaGate::Possess | EAsdaGate::Soul | EAsdaGate::Transfer)) { return false; }
	}
	Len = FMath::Max(0, Len);
	// ★ Волна 14: вызываем КОПИЮ — перехватчик вправе снять себя изнутри (самотест 78200 после первого 6500).
	if (bStand) { const auto Cap = SendCaptureForStand; Cap(Opcode, (Len > 0) ? Payload : nullptr, Len, bPad, TEXT('S')); return true; }
	const int32 BodyLen = 24 + Len;
	TArray<uint8> Body;
	Body.SetNumZeroed(BodyLen);
	uint8* B = Body.GetData();
	if (Len > 0 && Payload) { FMemory::Memcpy(B + 24, Payload, Len); }   // [24..] payload
	// [8..23] MD5(payload, Len) — стандартный MD5 (соль g_NetMd5Salt = выходной буфер); Len==0 → MD5 пустого.
	{ FMD5 Md5; uint8 Dig[16]; if (Len > 0) { Md5.Update(B + 24, Len); } Md5.Final(Dig); FMemory::Memcpy(B + 8, Dig, 16); }
	FillAntiReplaySecret(B);                                             // [0..7] анти-реплей секрет
	return ConnSendSecret(Opcode, B, BodyLen, 0, 0, bPad);
}

// ★ Д7: единый захват пола self из снапшота (см. заголовок).
void UAsdaNetSubsystem::CaptureSelfGenderFromSpawn(uint8 Gender, const TCHAR* Src)
{
	if (Gender != 1 && Gender != 2) { return; }   // 0 = сервер не заполнил
	if (SelfGenderFromSpawn != 0 && SelfGenderFromSpawn != Gender)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("[пол] %s: пол сменился %u → %u"),
			Src, (uint32)SelfGenderFromSpawn, (uint32)Gender);
	}
	SelfGenderFromSpawn = Gender;
}

// Строка версии клиента — СВЕРЯЕТСЯ login-сервером (cred+0, 11б; mismatch → ошибка 10).
// Зависит от сборки сервера (реверс — арабский LoginManager = "Arab_Closed"). Вынести в конфиг при смене сборки.
const char* UAsdaNetSubsystem::LoginVersionString = "Arab_Closed";

TArray<uint8> UAsdaNetSubsystem::BuildLoginBody(const FString& Login, const FString& Password) const
{
	// ★ Раскладка ПОДТВЕРЖДЕНА login_server.md §1 (реверс LoginManager sub_406C20 / cred-init sub_407B70).
	// Тело пакета 1003 (начинается с frame[9], т.е. сразу после FB|Size|A|B|Op):
	//   [0..7]    8 б  session-secret (анти-реплей sub_406360; в пропатченном сервере ИГНОРИТСЯ, но клиент формирует)
	//   [8..23]  16 б  MD5 cred-блока (integrity sub_4060E0; ИГНОРИТСЯ патчем)
	//   [24..266] 243 б cred-block (Buf1) — сервер читает ровно отсюда: memcpy(Buf1, frame+33, 0xF3); frame+33 == body+24
	//        cred+0   20 б  VersionString == "Arab_Closed" (сверяется)
	//        cred+20        login (ANSI)
	//        cred+81  32 б  MD5(password) hex
	//   (промежуточные/хвостовые поля cred-блока login_server.md не перечисляет → нули; патч обходит их проверку)
	static constexpr int32 SECRET  = 8;
	static constexpr int32 MD5LEN  = 16;
	static constexpr int32 CRED    = 243;
	static constexpr int32 CRED_AT = SECRET + MD5LEN;   // 24
	static constexpr int32 BODY    = CRED_AT + CRED;    // 267

	TArray<uint8> Body;
	Body.SetNumZeroed(BODY);

	auto WriteAnsi = [&Body](int32 At, const char* S, int32 N, int32 MaxLen)
	{
		FMemory::Memcpy(Body.GetData() + At, S, FMath::Min(N, MaxLen));
	};

	// --- cred-block @ body+24 ---
	WriteAnsi(CRED_AT + 0, LoginVersionString, (int32)FCStringAnsi::Strlen(LoginVersionString), 20); // VersionString
	{
		const FTCHARToUTF8 LoginAnsi(*Login);
		WriteAnsi(CRED_AT + 20, LoginAnsi.Get(), (int32)LoginAnsi.Length(), 32);                      // login
	}
	{
		FString PassMd5 = AsdaLoginPasswordMd5Hex(Password);   // 32-символьный hex MD5 (★ EAC Э1: общая точка с AsdaId)
		const FTCHARToUTF8 PassAnsi(*PassMd5);
		WriteAnsi(CRED_AT + 81, PassAnsi.Get(), (int32)PassAnsi.Length(), 32);                        // password MD5 hex
		// ★ 19.10.2026 (М11): временные копии MD5-hex затираются сразу (в теле пакета он остаётся до отправки — SendLogin).
		FPlatformMemory::Memzero(const_cast<ANSICHAR*>(PassAnsi.Get()), PassAnsi.Length());
		AsdaWipeSecretString(PassMd5);
	}
	// ★★★ ЕЩЁ ДВА ПОЛЯ cred-блока — вскрыты сравнением с ЖИВЫМ логин-пакетом клиента XyT
	//   (`Reports/xyt_traffic_capture.md`). Наш сервер их не проверяет (пропатчен), поэтому
	//   раньше нули проходили; НЕпропатченный сервер (XyT) на пустых полях молча роняет пакет —
	//   логин уходит, ответа 1004 нет вовсе. Формируем честно.
	{
		// cred+113 (тело[137..153]), 17 б: MAC машины строкой «xx-xx-xx-xx-xx-xx», нижний регистр.
		//   Эталон живого захвата: `34-5a-60-55-34-76`.
		// ★ R5-fix2 (13.11.2026, чистка C4996): `GetMacAddressString` помечена в движке устаревшей («ненадёжна не на всех платформах»),
		//   устаревшими помечены обобщённые `GetMacAddress`/`GetMacAddressString` (GenericPlatformMisc.h:656, :664); сама строка — это
		//   формат `%02x` поверх `FPlatformMisc::GetMacAddress()` (GenericPlatformMisc.cpp:548-559), у Windows своя неустаревшая
		//   `GetMacAddress` (WindowsPlatformMisc.h:129). `GetDeviceId`/`GetLoginId` дают другое значение — в пакет ушли бы другие байты.
		//   Вызов оставлен ТЕМ ЖЕ, чтобы 17 байт поля cred+113 не изменились; предупреждение снято только вокруг него.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FString Mac = FPlatformMisc::GetMacAddressString().ToLower();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		FString MacFmt;
		for (int32 i = 0; i + 1 < Mac.Len(); i += 2)
		{
			if (!MacFmt.IsEmpty()) { MacFmt += TEXT("-"); }
			MacFmt += Mac.Mid(i, 2);
		}
		const FTCHARToUTF8 MacAnsi(*MacFmt);
		WriteAnsi(CRED_AT + 113, MacAnsi.Get(), (int32)MacAnsi.Length(), 17);
	}
	{
		// cred+177 (тело[201..210]), 10 б: метка сборки клиента. Значение снято с живого клиента
		//   XyT — `2010110201`; своего не выдумываем, поле сверяется сервером как есть.
		static const char* kClientBuildStamp = "2010110201";
		WriteAnsi(CRED_AT + 177, kClientBuildStamp, 10, 10);
	}

	// --- integrity-префикс (сервер патчем не проверяет; формируем честно на случай непропатченного сервера) ---
	// session-secret [0..7]: у реального клиента = маска QueryPerformanceCounter; нули проходят (SecretProc всегда =1).
	// MD5 [8..23] = MD5(cred-block 243 б)
	FMD5 Md5;
	Md5.Update(Body.GetData() + CRED_AT, CRED);
	uint8 Digest[16];
	Md5.Final(Digest);
	FMemory::Memcpy(Body.GetData() + SECRET, Digest, MD5LEN);

	return Body;
}

// ★ [ИСПРАВЛЕНИЕ] 25.10.2026: ключ «вещи прошлой сессии не переживают перезаход» — определён в `AsdaInventorySubsystem.cpp`.
bool AsdaSessionRelogClearItemsOn();

bool UAsdaNetSubsystem::SendLogin(const FString& Login, const FString& Password)
{
	if (!Conn || !Conn->IsConnected()) return false;
	TArray<uint8> Body = BuildLoginBody(Login, Password);
	// ★ Захват 32-байт логин-блока (cred+20 = body+44, 32б login-поле) для последующих пакетов возврата/сброса
	//   (1028/1010). Точный состав блока sub_922E5C уточняется декомпилом LoginManager — здесь login-поле как база.
	if (Body.Num() >= 44 + 32) { FMemory::Memcpy(LoginCredBlob32, Body.GetData() + 44, 32); }
	// ★ СЕРВЕР-ПОДТВЕРЖДЕНО (LoginManager sub_406C20→sub_406360): логин ТОЖЕ проходит секрет-проверку и
	//   инкрементит серверный счётчик. Значит логин ДОЛЖЕН строить obfTick (counter 0), иначе счётчик клиента и
	//   сервера рассинхронятся → char-list получит рассинхрон → кик 4101 code=3 после накопления. (status 4 =
	//   overlap приходит от DB-состояния аккаунта, НЕ от секрета — это не регрессия секрета.)
	FillAntiReplaySecret(Body.GetData());   // secret[0..7] = obfTick(counter++), логин занимает counter 0
	const bool bOk = ConnSendSecret((uint16)OP_SEND_LOGIN_PACKET_1, Body.GetData(), Body.Num());
	if (bOk) State = EAsdaNetState::LoginSent;
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 — перезаход другой учёткой (`Reports/inventory_relog_leak/REPORT.md`): экип превью
	//   выбора персонажа (`CharList[k].Equip`, 0x3EF) приёмник 0x3EE намеренно сохраняет ПО НОМЕРУ СЛОТА (0x3EF приходит
	//   раньше списка), поэтому слот, для которого новая учётка 0x3EF не прислала, показывал бы экип ПРОШЛОЙ учётки.
	//   Новый логин — новая учётка: все 0x3EF этого входа придут уже ПОСЛЕ этой отправки, старый экип забываем здесь.
	//   Ключ общий с инвентарём (`AsdaSessionRelogClearItemsOn`, 0 = прежнее поведение).
	if (bOk && AsdaSessionRelogClearItemsOn())
	{
		for (FAsdaCharRecord& Rec : CharList) { Rec.Equip.Reset(); }
	}
	// ★ 19.10.2026: логин ждёт 1004 — после успешного входа он станет учёткой журнала сессии (только логин, без пароля).
	PendingSessionAccount = bOk ? Login : FString();
	UE_LOG(LogAsdaNet, Log, TEXT("SendLogin '%s' -> %s"), *Login, bOk ? TEXT("OK") : TEXT("FAIL"));
	// ★ EAC Э1 (16.10.2026): параллельно со старым входом — POST /token AsdaId тем же MD5-hex (асинхронно, вход 1003→1004
	//   его не ждёт). Дальше EOS Connect → BeginSession → канал 15901 (UAsdaAntiCheatSubsystem). EAC выключен — вызов пустой.
	if (bOk)
	{
		FString Md5Hex = AsdaLoginPasswordMd5Hex(Password);
		if (UAsdaAntiCheatSubsystem* Eac = AsdaEacOf(this)) { Eac->NotifyLoginCredentials(Login, Md5Hex); }
		AsdaWipeSecretString(Md5Hex);   // ★ 19.10.2026 (М11): своя копия затирается (EAC хранит свою до /token)
	}
	// ★ 19.10.2026 (М11): тело 1003 с MD5-hex пароля (cred+81) после отправки затирается.
	FPlatformMemory::Memzero(Body.GetData(), Body.Num());
	return bOk;
}

bool UAsdaNetSubsystem::IsConnected() const
{
	return Conn.IsValid() && Conn->IsConnected();
}

void UAsdaNetSubsystem::Disconnect()
{
	if (Conn) Conn->Disconnect();
	State = EAsdaNetState::Disconnected;
	bLoggedIn = false; LastNetTickStampSec = 0.0;   // сессии нет — метка пинга обнулена
	bLoginConnActive = false;
	bInGameConnected = false;   // game-сессия закрыта → стоп game-keepalive
	SendFailStreak = 0;
	bEacReturnToCharSelectSent = false;   // ★ EAC R3-01: соединения больше нет — ожидание «закрытия после 1028» снято
	// ★ EAC Э1: сессия закрыта клиентом (смена адреса сервера / замер) — сессия EAC тоже: EndSession, 15901, Connect.
	if (UAsdaAntiCheatSubsystem* Eac = AsdaEacOf(this)) { Eac->NotifySessionEnded(TEXT("соединение закрыто клиентом")); }
}

// ★ ЕДИНАЯ точка «соединение оборвано» (эталон: WM_USER 0x511 lParam=2 = FD_CLOSE → Show_DisconnectPopup,
//   которое ПЕРВЫМ делом зовёт netCloseSocketBySlot). Здесь: реально закрыть сокет (иначе на сервере
//   висит слот — LoginManager считает CurrentUserCount до 550 и снимает его только в ClearListenArrayIndex
//   по факту закрытия), полностью сбросить состояние сессии и уведомить UI, чтобы вход МОЖНО БЫЛО ПОВТОРИТЬ.
void UAsdaNetSubsystem::HandleConnectionClosed()
{
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: закрылось НЕДОДЕЛАННОЕ неблокирующее подключение к game-серверу (OnClosed из
	//   `FAsdaConnection::FailPendingConnect`: отказ ОС, срок истёк) — это неудача входа, а не обрыв живой сессии. Оформляем
	//   как оригинал (нотис 1001, причина 4) — `HandleGameConnectFailed` снимет ожидание и вернётся сюда уже обычным путём.
	// ★ 26.10.2026 (ревизия wave_1025d 4.2): только OnClosed самого несостоявшегося подключения (`FailPendingConnect` взводит признак).
	//   Иное закрытие при ожидании (GM-путь, EAC-кик, явный разрыв) идёт обычной веткой со своей причиной — код 4 её не затирает.
	//   Ключ `asda.Net.ConnectFailOnlyPending 0` — как 25.10 (любое закрытие при ожидании = неудача подключения).
	const bool bClosedByConnectFail = Conn && Conn->ConsumeClosedByConnectFail();
	if (bAwaitGameConnect && AsdaNetGameConnectFailDisconnectOn() && (bClosedByConnectFail || !AsdaNetConnectFailOnlyPendingOn()))
	{
		HandleGameConnectFailed(TEXT("ОС отвергла подключение или истёк срок"));
		return;
	}
	const bool bWasGame = bInGameConnected;
	const uint8 Reason  = LastKickCode;   // 0xFF, если 4101 перед обрывом не приходил
	// ★ Зеркало эталонного `g_nDisconnectReason` для попапа 8000: WndProc читает его УЖЕ ПОСЛЕ
	//   закрытия сокета, а `LastKickCode` ниже сбрасывается — см. объявление в .h.
	LastDisconnectReason = Reason;
	UE_LOG(LogAsdaNet, Warning, TEXT("Соединение закрыто (game=%d, причина=%u) → сброс сессии, вход можно повторить"),
		bWasGame ? 1 : 0, Reason);

	State = EAsdaNetState::Disconnected;
	bLoggedIn = false; bLoginConnActive = false; bInGameConnected = false;
	bTransferInProgress = false;   // сессия сброшена — гейт перехода снимаем, иначе он завис бы навсегда
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: и признаки перехода — после перелогина вход обычный 4001, а не портальный 5058.
	ClearPortalTransferState(TEXT("соединение закрыто"));
	// ★ 2026-09-02 (К-4): ожидание подтверждения game-коннекта тоже снимаем — сокет закрыт, ждать
	//   нечего, иначе отложенный хендшейк 20000 ушёл бы уже в мёртвое (или в СЛЕДУЮЩЕЕ) соединение.
	bAwaitGameConnect = false; AwaitGameConnectSec = 0.f;
	LastNetTickStampSec = 0.0; SendFailStreak = 0;   // сессия сброшена — метка пинга обнулена
	if (Conn) Conn->Disconnect();         // отдать сокет ОС → сервер снимет слот (иначе утечка слотов)
	// ★ EAC Э1 (16.10.2026): разрыв = полный сброс состояния EAC (правило порта: при разрыве сброс обязателен) —
	//   EndSession, канал 15901 закрыт, Connect_Logout, refresh-токен стёрт. Чисто локально + закрытие ДРУГОГО сокета (не этого).
	// ★ EAC R3-01 (CONTRACT.md §8.6): исключение — сервер закрыл ИГРОВОЕ соединение после нашего 1028 (возврат к выбору
	//   персонажа) без кика 4101: это смена персонажа, канал 15901 и сессия EAC сохраняются (решает NotifyConnectionClosed).
	const bool bEacReturnSent = bEacReturnToCharSelectSent;
	bEacReturnToCharSelectSent = false;
	if (UAsdaAntiCheatSubsystem* Eac = AsdaEacOf(this)) { Eac->NotifyConnectionClosed(bWasGame, bEacReturnSent, Reason); }

	if (bWasGame) OnGameDisconnected.Broadcast();
	else          OnLoginKick.Broadcast((int32)Reason, /*bClosed*/true);
	LastKickCode = 0xFF;
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (см. шапку `AsdaSessionReset.h`).
//   Что здесь важно понимать про ОРИГИНАЛ: у него этой задачи нет по построению — обрыв связи
//   ведёт в `Show_DisconnectPopup@0xEC9A80`, кнопка которого зовёт `PostQuitMessage(0)`
//   (`RE/etalon/network.c:14044`), то есть КЛИЕНТ ЗАВЕРШАЕТСЯ вместе со всеми полями
//   `CCharacter`. Порт намеренно оставляет клиент живым («вход можно повторить»,
//   `HandleConnectionClosed` выше), и цена этого решения — обязанность вычистить зеркала.
//   ★ ЭТО ИСПРАВЛЕНИЕ ДЕФЕКТА ПОРТА, а не отступление от эталона: оригинал ведёт себя так же
//   (после разрыва ни одно поле персонажа не переживает), просто добивается этого выходом.
void UAsdaNetSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ── ОБЩЕЕ ДЛЯ ВСЕХ ПРИЧИН ──
	// ★ EAC R3-01 (CONTRACT.md §8.6): смена персонажа — канал 15901, сессия EAC и Connect СОХРАНЯЮТСЯ (забывается лишь
	//   привязка прежнего игрового соединения); вход в мир (1021) идущую сессию не трогает — сайдкару уйдёт только BIND
	//   нового игрового соединения. Разрыв (Disconnected) EAC сбрасывает раньше — в HandleConnectionClosed (кроме закрытия
	//   игрового соединения после нашего 1028). Смена карты сессию EAC не трогает (учётка та же).
	if (UAsdaAntiCheatSubsystem* Eac = AsdaEacOf(this))
	{
		if (Reason == EAsdaSessionResetReason::LeftWorld)          { Eac->NotifyCharacterLeftWorld(); }
		else if (Reason == EAsdaSessionResetReason::EnteringWorld) { Eac->NotifyEnterWorld(); }
	}
	// ★ 15.10.2026: поставленный к дедлайну 4028 старой сессии не уходит (сервер её уже не знает).
	CancelPending4028(TEXT("сброс сессии"));
	// ★★★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА]: признаки незавершённого перехода (портальный вход 5058, тайл прибытия, отложенный
	//   реконнект) — при ПОЛНОМ сбросе: разрыв, выход в выбор персонажа и вход из него (1021). [ОРИГИНАЛ] вход из выбора
	//   персонажа — ветка загрузки 0xB, её шаг 3.0 шлёт 4001 (game_main.c:3093), портальный 5058 шлёт только загрузка по
	//   переходу (game_main.c:3607). При смене карты (MapChanged) НЕ трогаем: 5058 уходит как раз после загрузки новой карты.
	if (AsdaIsFullSessionReset(Reason)) { ClearPortalTransferState(AsdaSessionResetReasonName(Reason)); }
	// ★★ 27.09.2026 (исправление ревизии хода 4007): бит 2 «запрос хода в пути» и его метка — при ЛЮБОЙ причине,
	//   включая смену карты и вход в мир. Прежде эти строки стояли в хвосте функции, ПОСЛЕ ранних выходов
	//   `MapChanged`/`EnteringWorld`, и при портале бит, взведённый 4006 до перехода, держал клики и навыки до
	//   сторожа 10 с (ответ старого соединения не придёт). [ОРИГИНАЛ] разбор мира обнуляет всё слово флагов:
	//   главный цикл, состояние 0xE (`MainLoop_Tick_SoundUpdate@0x95EAB0`) → `uiResetEffectAndItemSlots@0x966520` →
	//   `uiItemSlotReset@0x992790`, 0x992936 `mov dword [eax+0x4914], 0`.
	bMoveRequestInFlight = false;
	MoveRequestStampMs = 0.0;
	// ★ 09.10.2026 (автолопата): «подбор 5012 в пути» — ответ старого соединения не придёт. [ПОРТ] оригинал при разрыве
	//   закрывает клиент (память original-quits-client-on-disconnect); у порта сброс обязателен при любой причине.
	bItemPickPending = false;
	//   Очереди принятых пакетов держат тела ПРОШЛОЙ карты/сессии: применять их на новой карте
	//   нельзя (мобы прошлой карты «переезжали» — тот же класс дефекта, что чинили реестры
	//   `UAsdaWorldSubsystem` по 4003).
	PendingNpcBatches.Clear();
	PendingMobBatches.Clear();
	PendingMobUpdates.Clear();
	PendingRemotePlayers.Clear();
	// ★ Ревизия волны 1 (14.09.2026): анти-спам 5416 держит userIdx ПРОШЛОЙ карты. Номер слота сервер
	//   переиспользует, а реестр чужих игроков мира на новой карте пуст — без очистки игрок с тем же номером
	//   остался бы без запроса снапшота, т.е. невидимым. Раньше чистился только в `ConnectToLogin`.
	RequestedUserInfo.Reset();
	// ★ 17.10.2026 (Ф4): PvP-режимы персонажей (4206) живут в сессии сервера — новая карта/сессия их переприсылает.
	PvpStates.Reset();

	// ★★★ GM-«скрыться» (6005/6006). Поле живёт в серверном `CUser` и умирает вместе с сессией,
	//   поэтому зеркало держим ровно столько же. При СМЕНЕ КАРТЫ (MapChanged) сессия та же —
	//   состояние сохраняем, сервер его не переприсылает. «Ждём ответа» снимаем в любом случае:
	//   ответ на заявку прошлой сессии не придёт никогда (закон no-state-latch-before-shown).
	GmHideLatch.Disarm();
	// ★ [ПОРТ] Ожидание доопределения по имени тоже сессионное: имя следующей сессии придёт заново.
	bGmHideEntryNamePending = false;
	bGmRestoreVisiblePending = false;   // ★ 18.10.2026: восстановление «виден» взводится заново при следующем 4003
	bGmRestoreInFlight = false;
	// ★ Античит А2 (мера 7): серверный признак оператора (эхо 6006 «принято») выдан на СЕССИЮ — при полном сбросе
	//   снимаем; при смене карты сессия та же, признак остаётся (как и права GM-меню, см. `UAsdaAdminSubsystem`).
	if (AsdaIsFullSessionReset(Reason)) { ClearOperatorConfirmation(); }
	if (AsdaIsFullSessionReset(Reason) && bGmHidden)
	{
		UE_LOG(LogAsdaNet, Log, TEXT("[сессия] GM-невидимость сброшена (%s)"), AsdaSessionResetReasonName(Reason));
		bGmHidden = false;
		bGmHiddenFromEntry = false;
		OnGmHideResult.Broadcast(/*bAccepted*/true, /*bHidden*/false);
	}

	// Витальные значения и боевые блоки СОСЕДЕЙ ключуются сессионным userIdx, который сервер
	//   выдаёт заново. Свою запись при смене карты сохраняем: 5058 её не переприсылает, а полоса
	//   HP не должна обнуляться на ровном месте.
	// ★ СМЯГЧЕНИЕ (решение владельца 2026-09-02): СВОЮ витальную запись сохраняем не только при
	//   смене карты, но и при РАЗРЫВЕ СВЯЗИ. Причина: при разрыве уровень не выгружался, актор
	//   жив (см. `AsdaSessionResetKeepsWorldAlive`), HUD продолжает рисоваться — и без этой записи
	//   он рисовал бы ПУСТЫЕ полосы HP/MP под стоящим персонажем. Чужие записи убираем в любом
	//   случае: они ключуются сессионным `userIdx`, который сервер выдаёт заново.
	//   ⚠ Ноль — ЗАКОННЫЙ `userNum` (закон `self-usernum-zero-is-valid`), поэтому «свой» опознаём
	//   по `bSelfUserNumKnown`, а не по значению.
	if (AsdaSessionResetKeepsWorldAlive(Reason) || !AsdaIsFullSessionReset(Reason))
	{
		const uint16 Self = SelfUserNum;
		for (auto It = VitalsByUserIdx.CreateIterator(); It; ++It)
		{
			if (!bSelfUserNumKnown || It.Key() != (int32)Self) { It.RemoveCurrent(); }
		}
		for (auto It = CombatStatsByUserIdx.CreateIterator(); It; ++It)
		{
			if (!bSelfUserNumKnown || It.Key() != (int32)Self) { It.RemoveCurrent(); }
		}
	}
	else
	{
		VitalsByUserIdx.Reset();
		CombatStatsByUserIdx.Reset();
	}
	if (!AsdaIsFullSessionReset(Reason))
	{
		return;   // при смене карты зеркала `CCharacter` НЕ трогаем — сервер их не переприсылает
	}
	// ★ 22.10.2026 (ревизия локального старта, мелочь): скорость своего хода — от прошлого персонажа (маунт/дебафф); новый персонаж
	//   до первого гранта трогается локально базовой 0.35.
	LastServerSpeed = 0.35f;

	// ★★★ ИСКЛЮЧЕНИЕ ДЛЯ «ВХОДА В МИР». Зеркала блока 1017 (`OP_NET_RECV_CHAR_SELECT`, :4280)
	//   заполняются ОТВЕТОМ НА ВЫБОР ПЕРСОНАЖА, то есть РАНЬШЕ пакета 1021, из обработчика
	//   которого приходит этот сброс. Стереть их здесь значило бы выбросить пол, знак зодиака,
	//   уровень, класс, вес и ёмкость склада ТОГО САМОГО персонажа, который сейчас входит.
	//   Предыдущая сессия к этому моменту уже вычищена своим событием (разрыв связи или выход
	//   в выбор персонажа), поэтому «переживать» здесь нечему.
	if (Reason == EAsdaSessionResetReason::EnteringWorld) { return; }

	// ── ПОЛНЫЙ СБРОС: следующий вход может быть ДРУГИМ персонажем ──
	UE_LOG(LogAsdaNet, Log,
		TEXT("[сессия] сброс зеркал персонажа (%s): уровень %u, клан «%s», знак %u"),
		AsdaSessionResetReasonName(Reason), SelfLevel, *SelfClanName, SelfConstellation);

	// ★ СМЯГЧЕНИЕ (решение владельца 2026-09-02): при РАЗРЫВЕ СВЯЗИ зеркала СВОЕГО персонажа
	//   сохраняем. Актор жив и стоит на карте, HUD рисуется — а без этих полей мини-статус
	//   показал бы уровень 0, пустой вес и неизвестный класс под живым персонажем.
	//   ★ Решающее здесь — `bSelfUserNumKnown`/`SelfUserNum`: по ним мини-статус ищет свою
	//   витальную запись (сохранённую выше). Обнулив их, смягчение не сработало бы вовсе.
	//   Безопасно, потому что следующий персонаж перезаполнит эти поля БЛОКОМ 1017 (ответ на
	//   выбор персонажа) ещё до входа в мир, а `LeftWorld` чистит их как и раньше.
	//   ⚠ Списки, собранные сервером для нашей сессии (ниже), чистятся В ЛЮБОМ случае —
	//   они протухли независимо от того, жив ли мир.
	if (!AsdaSessionResetKeepsWorldAlive(Reason))
	{
		// Внешность и однократный блок 1017.
		SelfGender = 0; SelfConstellation = 0xFF;
		SelfHair = 0xFF; SelfHairColor = 0xFF; SelfFace = 0xFF; SelfGenderFromSpawn = 0;
		SelfStoreSlotsMax = -1; SelfStoreSlotsUsed = -1;
		SelfIntroduction.Empty();

		// Профиль персонажа (4005/4065/4036/…).
		SelfLevel = 0; SelfExp = -1; SelfClassId = -1;
		SelfJob = -1; SelfSubJob = -1; SelfSkillPoints = -1;
		SelfCraftSkill = -1;
		SelfWeightCur = -1; SelfWeightMax = -1;
		SelfWeaponItemId = 0; SelfWeaponGrade = 0;
		SelfShieldItemId = 0; SelfShieldGrade = 0;

		// Клан и соулмейт-признак (гейты клановых окон читают именно их).
		SelfClanRank = 0;
		SelfClanName.Empty();
		SelfSoulSilhouette = 0;
		// ★ Кэши одержимости (6152/5220): userIdx — слот СЕССИИ, сервер его переиспользует, а charId
		//   привязан к персонажу. При новой сессии оба обязаны быть пустыми, иначе метка «залипнет»
		//   на чужом игроке (та же грабля, что была у транспорта/крыльев, см. AsdaWorldSubsystem 4011).
		SoulStateByCharId.Reset();
		SoulStateByUserIdx.Reset();
		// ★ Свой признак одержимости. С 2026-09-03 ставится по УСПЕШНОМУ ответу 1017 (по режиму
		//   последнего 1016, `LastSelectMode`) и снимается отказом 1027 — на отправке 1016 он
		//   больше не взводится, см. `SendSelectChar` и ревизию `review_possess_action_gate.md` п.4.
		//   Обычный вход (Mode 0) его снимает, поэтому «залипнуть» на следующем персонаже он не
		//   может. Гасим здесь ради промежутка МЕЖДУ сессиями: с момента выхода в выбор персонажа
		//   и до ответа на новый выбор гейт `AsdaPossessGate` читал бы признак прошлой сессии и
		//   молча резал бы отправки. Сбрасываем только там же, где чистим остальные свои зеркала:
		//   при РАЗРЫВЕ СВЯЗИ мир жив и признак обязан пережить его вместе с полосами HP и
		//   профилем (смягчение владельца 2026-09-02 выше).
		//   С 2026-09-03 признак — производная единого `SelfSoulState` (0/1/2, «душа» = 1), гасим его.
		SetSelfSoulState(0);
		LastSelectMode = 0;   // и запрошенный режим входа — иначе новый 1017 возьмёт режим прошлой сессии

		// Положение в мире и сессионный номер.
		SelfTileX = -1; SelfTileZ = -1;
		SelfUserNum = 0; bSelfUserNumKnown = false;
#if ASDA_COMBAT_TRACE
		AsdaCombatTrace::SetSelfUserIdx(-1);   // журнал боя: свой userIdx больше неизвестен
#endif
		bSelfSpawnDead = false; bSelfDead = false; bSelfDeathNoClip = false;
		// ★ 25.10.2026: показ шлема — значение нового 4003 (до него — как конструктор персонажа). Блок опций 1027
		//   НЕ трогаем: он приходит ДО этого сброса (закон `session-reset-drops-early-data`) и уже отдан окну.
		SelfHelmetShow = 1;
	}

	// Списки, собранные сервером для НАШЕЙ сессии.
	FriendEntries.Reset();
	PendingFriendInvites.Reset();
	WhoEntries.Reset();
	LastMateRecall.Reset();
	LastFriendListReqTime = -1.0;
	KnownClanIndex = 0;
	// ★ 27.09.2026: «раз в день на цель» у 6059 — к значениям конструктора эталона ТОЛЬКО при разрыве связи.
	//   [ОРИГИНАЛ] поля `self[24115..24117]` живут в `g_pNpcWnd`: `j_gameObj_ctor@0x93DCAC` вызывается ОДИН раз — из
	//   статического инициализатора 0x1388910 (`mov ecx, offset g_pNpcWnd` + `atexit`), других вызовов нет (xref IDB).
	//   Объект живёт весь процесс: выход в выбор персонажа и порталы его не сбрасывают, пишет в поля только 6098.
	//   Процесс оригинала кончается только разрывом (`original-quits-client-on-disconnect`), поэтому сбрасываем здесь
	//   лишь при `Disconnected` (прежде — ещё и при `LeftWorld`; ревизия charid_senders_REVIEW.md МЕЛК-1).
	if (Reason == EAsdaSessionResetReason::Disconnected) { ResetEventNpcDayThrottle(); }

	// Города: владельцы и флаги приходят одним пакетом на входе в мир.
	for (int32 i = 0; i < 3; ++i) { CityOwner[i] = FAsdaCityOwner(); }
	bCityOwnerReceived = false;

	// Сессионные признаки собеседника.
	//   ⚠ `EnterMapId`, `CurrentChannel`, `GameServerIP/Port` НЕ ТРОГАЕМ. Причина: сброс
	//   «вход в мир» рассылается ИЗ ОБРАБОТЧИКА 1021, который эти поля только что заполнил
	//   адресом и картой назначения. Обнулив их здесь, мы отправили бы игрока в никуда.
	//   Ничего не теряем: каждый вход и каждый переход задаёт их заново.
	ExchangeNpcId = -1;
	bCampWarHudSuppressed = false;
	CampWarToken1 = 0xFF; CampWarToken2 = 0xFF;
	bActionPending = false;
	// (бит 2 «запрос хода в пути» чистится в ОБЩЕЙ части выше — при любой причине, см. там.)
	PendingBagPackets.Clear();
	PendingEquipPackets.Clear();
}

bool UAsdaNetSubsystem::SendCharListRequest(uint16 ServerIndex)
{
	if (!IsSendLinkUp()) return false;
	// Тело 30б: [0..7] secret(нули — сервер игнорит, как логин), [8..23] MD5(payload6), [24..29] payload.
	// ★ payload = UserIndex(u32) + seq(u16=0). Сверено ПРАВИЛЬНЫМ ключом захвата: char-list = UserIndex (0x000125AC),
	//   байт[2]=0x01. Прежний «0x14» был артефактом decode_capture.py (неверный keystream[0]=0x57 vs 0x42).
	//   Эталон: get_oBBE4B(char-list userNo) = UserIndex (set через sub_92EA0E в Net_RecvLoginResponse).
	// payload 6б: UserIndex(u32) + ServerIndex(u16). Д6: тело собирает SendSecured (MD5(payload,6)+секрет).
	//   Эталон Send_CharacterPacket@0xF17E60: Net_Md5Digest16(salt, pkt, 6); секрет — анти-реплей
	//   (СЕРВЕР-ПОДТВЕРЖДЕНО: char-list sub_407CE0 проверяет секрет — ОБЯЗАТЕЛЕН).
	FAsdaPacketWriter W(6);
	W.I32(0, (int32)UserIndex);
	W.I16(4, (int32)ServerIndex);
	// ★ 1005 — НАЧАЛО ВСЕЙ транзакции списка персонажей: сервер отвечает на него залпом
	//   1007×N → 1006 (мои) и 1024×N → 1023 → 1025 (соулмейта). Чистим набор соулмейта здесь,
	//   а не в 1023: экип (1024) приходит ПЕРВЫМ и создаёт записи — чистка в 1023 стёрла бы его.
	//   Если у соулмейта сняли галку, повторный запрос вернёт ветку без 1024/1023, и набор
	//   обязан оказаться пустым, а не остаться с прошлого захода.
	SoulmateSharedChars.Reset();
	SoulmateSharedChars.SetNum(3);
	SoulmateBranchCode = -1;
	bSoulmateInfoSecondParagraph = false;
	bSoulmateOnline = false;
	SoulmateInGameByte = 0; SoulmateChannel = 0; SoulmatePortalId = 0;
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_CHARACTER_PACKET, TEXT("SendCharListRequest"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SEND_CHARACTER_PACKET, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("SendCharListRequest UserIndex=%u srv=%u ctr=%d -> %s"), UserIndex, ServerIndex, ObfCounter - 1, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

bool UAsdaNetSubsystem::SendChannelSelect(uint8 ServerGroupIndex)
{
	if (!IsSendLinkUp()) return false;
	// ★ ИСПРАВЛЕНО 2026-09-02: параметр звался `Channel`, а на проводе здесь
	//   `ServerGroupIndex` (`@Seridx` всех процедур сервера). Имя врало — из-за него в 1016
	//   в поле группы уезжал канал. Два из трёх вызовов были верны случайно.
	// payload 5б: targetId=UserIndex(u32) + ServerGroupIndex(u8). Д6: тело собирает SendSecured (MD5(payload,5)+секрет).
	//   Эталон netChannel_SendSelect1012@0xF1FB80: Net_Md5Digest16(salt, selectReq, 5); секрет — анти-реплей
	//   (server-select тоже проходит секрет-проверку: sub_40D540→sub_406360).
	FAsdaPacketWriter W(5);
	W.I32(0, (int32)UserIndex);
	W.U8(4, ServerGroupIndex);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_CHANNEL_PACKET, TEXT("SendChannelSelect"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SEND_CHANNEL_PACKET, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("SendChannelSelect grp=%u ctr=%d -> %s"), ServerGroupIndex, ObfCounter - 1, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

// ★ ПЕРЕХОД НА ДРУГОЙ КАНАЛ из мира (эталон netSec_SendChallenge6084@0xF46A90, опкод 6084, тело 25б):
//   [0..7]  scrambledTs — тот же анти-реплей-токен (QPC + 30 битовых позиций bitPosTable), что у 20000/5012;
//   [8..23] Net_Md5Digest16(g_NetMd5Salt, payload, 1) = обычный MD5 однобайтного payload;
//   [24]    payload = НОМЕР КАНАЛА 0-based (в оригинале = ctrlId − 0x445F, т.е. индекс строки Change_Channel<NN>).
//   Ответ сервера — 6015 (netHandleChannelTransfer): адрес канал-сервера + карта/спавн + новый номер канала.
bool UAsdaNetSubsystem::SendChannelMove6084(uint8 Channel)
{
	// ★ 26.10.2026: проверка связи — общая `IsSendLinkUp` (в игре то же «сокет соединён»; под перехватом самотеста
	//   asda.SelfTest.ChannelMove пакет доходит до перехватчика, а не теряется на проверке сокета).
	if (!IsSendLinkUp()) return false;
	// payload 1б: номер канала 0-based. Д6: тело собирает SendSecured (MD5(payload,1)+секрет).
	const bool bOk = SendSecured((uint16)OP_RECV_USER_CHANNEL_MOVE, &Channel, 1);
	UE_LOG(LogAsdaNet, Log, TEXT("→6084 смена канала: ch=%u (в UI %u) -> %s"),
		Channel, Channel + 1, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ Единственный сеттер своего SoulState (см. объявление): держит зеркало `bSelfPossessed`.
void UAsdaNetSubsystem::SetSelfSoulState(uint8 InState)
{
	if (InState > 2)
	{
		UE_LOG(LogAsdaNet, Warning, TEXT("SetSelfSoulState: значение %u вне 0..2 — считаем обычным входом (0)"), (uint32)InState);
		InState = 0;
	}
	SelfSoulState  = InState;
	bSelfPossessed = (InState == 2);
}

bool UAsdaNetSubsystem::SendSelectChar(uint8 RawSlotId, uint8 Group, uint8 Channel, uint8 Mode)
{
	if (!IsSendLinkUp()) return false;
	// payload 9б (эталон charDealMsg entity_char.c:51795). Д6: тело собирает SendSecured (MD5(payload,9)+секрет).
	//   MD5 — Net_Md5Digest16(salt,charDealMsg,9), чистый MD5; LoginManager РЕАЛЬНО валидирует. Секрет —
	//   анти-реплей (enter-world проходит секрет-проверку sub_40AD30→sub_406360).
	//
	// ★★★ РЕЖИМ 2 — «ОДЕРЖИМОСТЬ» (вход персонажем соулмейта). Эталон `charDealSendReq@0xF088E0`
	//   при режиме слота 2 подменяет ДВА поля и только их:
	//     `*(DWORD*)(charDealMsg+1) = subRecord[1052]`  — Userindex АККАУНТА СОУЛМЕЙТА (из 1024),
	//     `HIBYTE(charDealMsg[2])   = subRecord[1076]`  — Charidx ЧУЖОГО персонажа,
	//   а канал/сервер (b6..b8) берёт из СВОЕЙ записи слота. Мы делаем ровно это.
	//   Сервер `Send_SelectChar@0x40AD30` в режиме 2 намеренно НЕ сверяет аккаунт пакета с аккаунтом
	//   сессии (`if (!LOBYTE(v109[0]))` — проверка только для режима 0), дальше `UPPreSMConnect`:
	//   если соулмейт СЕЙЧАС в игре — отказ 1027 код 2; иначе перепривязка сессии
	//   (`sub_4037B0`) и `UPCharSoulState @State=2` (владельца из игры выкидывает сама процедура).
	//
	// ★★★ РЕЖИМ 1 — «ДУША» (вход СВОИМ персонажем призраком-наблюдателем). ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА
	//   по решению владельца 2026-09-03 (закон `fix-original-defects`, откат — `asda.Soul.AllowEnter 0`).
	//   ЧТО ДЕЛАЕТ ОРИГИНАЛ: `charDealSendReq@0xF088E0` (`entity_char.c:51785-51790`) при
	//   `dealMode == 1` показывает подсказку помощника 33 (`uiSelectListItem(g_pGameApp, 33)` —
	//   это подпись+вкладка, НЕ звук: ревизия §7) и делает `return` ДО заполнения тела и ДО
	//   `CNetwork_Send` (:51909). Пакет не уходит, режим в этой сборке недостижим — ветка авторами
	//   не доделана (доигран только визуал выбора: белый силуэт + подпись 23).
	//   ЧТО ДЕЛАЕМ МЫ: шлём тот же 1016, что и в режиме 0, — тело заполняется СВОИМ слотом
	//   (аккаунт = наш `UserIndex`, b5 = наш сырой Charidx, b6-7 = группа, b8 = канал), только
	//   байт режима = 1. Это ровно то, что сервер ждёт: `LoginManager Send_SelectChar @0x40AD30`
	//   (`LoginManager.c:4151-4167`) сверяет аккаунт пакета с аккаунтом сессии ТОЛЬКО при режиме 0,
	//   ветка `UPPreSMConnect` (:4168-4192) — только при режиме 2, а дальше для ЛЮБОГО режима идёт
	//   `EXEC UPCharSoulState @Seridx=b6-7, @Userindex=b1-4, @Charidx=b5, @State=b0` (:4200) и
	//   `UPCharPos` (:4227). Отказ 1027 код 3 (:4213) — только для режима 2. Для режима 1 процедура
	//   просто пишет `CharPos.SoulState = 1` и возвращает Flag 1 (разбор §2.1, БД §6).
	//   Клиентское следствие — гейт исходящих `AsdaSoul` (`World/AsdaPossessGate.h`): сервер
	//   при SoulState==1 молча проглатывает 71 опкод (`sub_52EEA0` в диспетчере).
	if (Mode == 1 && !AsdaSoul::IsEnterAllowed())
	{
		// Поведение оригинала: пакет не уходит. Подсказку 33 показывает сцена выбора персонажа
		//   (`AAsdaLoginGameMode`, `ApplyHelper(33)`), у сетевого слоя окна нет.
		UE_LOG(LogAsdaNet, Warning, TEXT("SendSelectChar режим 1 («душа») не отправлен: ключ asda.Soul.AllowEnter 0 = поведение оригинала (entity_char.c:51785)"));
		return false;
	}
	uint32 AccountIdx = UserIndex;                    // b1-4 по умолчанию — НАШ аккаунт (режимы 0 и 1)
	if (Mode == 2)
	{
		// Ищем запись персонажа соулмейта по СЫРОМУ Charidx (10/11/12) — так же, как эталонный
		//   `playerFindSubRecordByType@0xF0A210` (он тоже сверяет rec[1076] и отсекает вне 10..12).
		const FAsdaSoulmateSharedChar* Shared = nullptr;
		for (const FAsdaSoulmateSharedChar& R : SoulmateSharedChars)
		{
			if (!R.IsEmpty() && R.Charidx == RawSlotId) { Shared = &R; break; }
		}
		if (!Shared)
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("SendSelectChar режим 2: персонажа соулмейта Charidx=%u нет в наборе — не шлём"), RawSlotId);
			return false;
		}
		if (Shared->OwnerUserIndex <= 0)
		{
			// Аккаунт владельца приходит ТОЛЬКО пакетом 1024. Без него сервер получит наш
			//   Userindex и войдёт нашим же персонажем — молча не тем. Лучше не отправлять.
			UE_LOG(LogAsdaNet, Warning, TEXT("SendSelectChar режим 2: у записи Charidx=%u нет аккаунта владельца (1024 не пришёл) — не шлём"), RawSlotId);
			return false;
		}
		AccountIdx = (uint32)Shared->OwnerUserIndex;
	}
	FAsdaPacketWriter W(9);
	W.U8(0, Mode);                                // b0 режим: 0=свой, 1=душа, 2=одержимость (эталон charDealMsg[0])
	W.I32(1, (int32)AccountIdx); // b1-4 Userindex аккаунта (свой либо соулмейта, rec[1052])
	W.U8(5, RawSlotId);                            // b5 = СЫРОЙ slotId (rec[1076]=10/11/12), НЕ индекс 0..2
	// ★★★ ИСПРАВЛЕНО 2026-09-02 (ревизия, решение владельца «принять эталонное решение»).
	//   БЫЛО: в b6-7 клали КАНАЛ. Это ошибка — там `ServerGroupIndex` (индекс выбранного СЕРВЕРА).
	//   Доказано машинным кодом эталона, а не именами: кнопка сервера (`ServerSelectWnd+8`,
	//   контролы 210..250) → второе поле 1005 → сеттер `sub_F186F0` пишет `GameClient+0x2EF930`
	//   → геттер `get_oBBE4C@0xF065D0` (`mov eax,[eax+2EF930]`) → `slotRecord[1077]` → b6-7.
	//   Канал — ДРУГОЕ поле (`ServerSelectWnd+4`, `sub_B574A0`) и едет ТОЛЬКО в b8.
	//   Сервер `Send_SelectChar@0x40AD30`: `v109[3]` → гейт `> dword_6781920` (число групп) и
	//   `sub_4060A0(v109[3])` — ВЫБОР ПОДКЛЮЧЕНИЯ К БД, плюс первый параметр `@Seridx` во все
	//   процедуры (`UPPreSMConnect`, `UPCharSoulState`, `UPCharPos`). Ошибка → 1011 код 201.
	//   ⚠ ПОЧЕМУ НЕ ПРОЯВЛЯЛОСЬ: у нас и группа, и канал = 0, значения совпадали. Проявилось бы
	//   на канале ≥ 1: «GroupIndex Error», вход не происходит.
	W.I16(6, (int32)Group);      // b6-7 = ServerGroupIndex (rec[1077]=oBBE4C)
	W.U8(8, Channel);                             // b8 = канал (эталон LOBYTE(charDealMsg[4])=sub_92B66A)
	LastSelectedRawSlot = RawSlotId;   // для блока 4001 (iCharIndex на game-сервере)
	LastSelectMode      = Mode;        // ЗАПРОШЕННЫЙ режим входа; в признак уйдёт только по ответу 1017
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_DEAL_SEND_REQ, TEXT("SendSelectChar"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CHAR_DEAL_SEND_REQ, W.Data(), W.Num());
	// ★★★ ИСПРАВЛЕНО 2026-09-03 (ревизия `review_possess_action_gate.md` п.4, закон
	//   `no-state-latch-before-shown`). БЫЛО: `if (bOk) { bSelfPossessed = (Mode == 2); }` — признак
	//   взводился по факту ОТПРАВКИ 1016. Пока он кормил только надпись «(Одержим)» над своей
	//   плашкой, это было безобидно; теперь он же управляет ЖЁСТКИМ запретом действий
	//   (`World/AsdaPossessGate.h`), а сервер вход одержимостью МОЖЕТ ОТКЛОНИТЬ — `Send_SelectChar
	//   @0x40AD30` отвечает 1027 с кодом 0/2/3 вместо 1017. При таком отказе признак оставался
	//   взведён и резал бы действия обычного персонажа.
	//   СТАЛО: здесь запоминаем только ЗАПРОШЕННЫЙ режим (`LastSelectMode`, строка выше), а сам
	//   признак ставится по УСПЕШНОМУ ответу 1017 (`case OP_NET_RECV_CHAR_SELECT`) и снимается
	//   по отказу 1027 (`case OP_NET_HANDLE_APPEARANCE_CHANGE_RESULT`, тело[0] != 1).
	//   Сброс на новой сессии — в `ResetSessionState` (там же, где `SoulStateByCharId.Reset()`).
	UE_LOG(LogAsdaNet, Log, TEXT("SendSelectChar режим=%u rawSlot=%u аккаунт=%u grp=%u ch=%u ctr=%d -> %s"),
		Mode, RawSlotId, AccountIdx, Group, Channel, ObfCounter - 1, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

// ── ★ ФАЗА B: game-протокол (WORLD_ENTRY_CHAIN.md) ──

// C→S 5056 `CharPortalLogoutReq` (★ имя «GameAck» в порту — МИСНЕЙМ, это не подтверждение). 24 б = секрет 8 + MD5(пусто) 16.
//   Клиент оригинала шлёт его после успешного 6004 (`netSendAnticheatTsMask@0xF47710`, network.c:1277) — в ПРЕЖНИЙ слот
//   `Field8` (старый сервер; ★ 18.10.2026: прежняя запись «на НОВОМ соединении» была ошибкой разбора) — и после 6203
//   (`netSendAnticheatTsMask2@0xF47CA0`, network.c:737) — в текущий `Field1C`. Порт: см. `CVarAsdaNetPortalLogoutOnOldConn`.
//   Сервер `CharPortalLogoutReq` (GameServer.c:114312): сверка секрета и MD5 пустого тела, флаг портала
//   `CUser+gap_62[217] = 1`, `PortalDisConnectUser(userNum)`; ответа не шлёт. Вне шлюза исходящих (гейт перехода
//   его пропускать обязан — именно так поступает оригинал).
bool UAsdaNetSubsystem::SendGameAck5056()
{
	if (!Conn || !Conn->IsConnected()) return false;
	uint8 Body[24];
	FMemory::Memzero(Body, sizeof(Body));
	{ FMD5 Md5; Md5.Final(Body + 8); }   // MD5 пустого payload (тела за MD5 нет)
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_CHAR_PORTAL_LOGOUT_REQ, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("Game-ack 5056 -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 5056 (game-ack)
}

// ★ ЧИСТЫЙ выход из мира → возврат в char-select (эталон net_sendLoginCharEnterOpcode1028@0xF0C420, opcode 1028).
//   56б = [секрет8][MD5(логин-блок,32)16][логин-блок32]. Триггерит серверный save+reset (DisConnectUser →
//   сохраняет позицию + снимает блок движения CUser+0x4f; на логин-сервере UPLogoutUser сбрасывает статус
//   аккаунта из «в игре»/overlap). БЕЗ этого статус чара застревает=3 → следующий вход блокируется (нет движения).
bool UAsdaNetSubsystem::SendReturnToCharSelect()
{
	if (!Conn || !Conn->IsConnected()) return false;
	// Д6: тело собирает SendSecured — [секрет8][MD5(логин-блок,32)16][логин-блок32].
	const bool bOk = SendSecured((uint16)OP_LOGIN_WND_SHOW_CHAR_SELECT_PANEL, LoginCredBlob32, 32);
	UE_LOG(LogAsdaNet, Log, TEXT("Чистый выход 1028 (возврат в char-select) -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
	// ★ EAC R3-01: закрытие игрового соединения после этого 1028 — смена персонажа для EAC (канал 15901 не закрывается).
	if (bOk && bInGameConnected) { bEacReturnToCharSelectSent = true; }
	return bOk;
}

// ★ Сброс «состояния 3» при авторизации (эталон gameClientSendStatePacket@0xF0B9C0 case 3 → OP_C_LOGIN_WIN_ON_BUT00,
//   opcode 1010). Тот же формат 56б = [секрет8][MD5(логин-блок,32)16][логин-блок32]. Клиент шлёт его, когда аккаунт
//   в overlap/«уже в игре» (робот «выход не выполнен»), чтобы логин-сервер force-сбросил прошлую сессию
//   (UPLogoutUser/ResetOverlapCount). ТОЧНЫЙ триггер/формат уточняются декомпилом LoginManager.
// ★ ФЛАГИ ОКНА НАСТРОЕК 4070 (эталон `uiOptionsSendSettingsPacket2@0xED69E0`, ui_core.c:89291).
//   Тело 19 байт собирает окно настроек; здесь — конверт «MD5 без секрета» и опкод, как у эталона
//   (`Net_Md5Digest16(salt, body, 0x13, md5)` → `[md5 16][body 19]` → `CNetwork_Send(…, 35, 4070, 1,1,1,0,0)`).
bool UAsdaNetSubsystem::SendOptionFlags4070(const uint8* Body19)
{
	// ★ 25.10.2026: проверка соединения — в `SendMd5Signed` (там же, что у всех отправителей): иначе узкий перехват самотеста
	//   (`ArmSendCapture(…, 4070)`) сюда не доходил. В игре поведение прежнее: без связи — false.
	if (!Body19) { return false; }
	const bool bOk = SendMd5Signed(4070, Body19, 19, /*bPad*/ true);
	UE_LOG(LogAsdaNet, Log, TEXT("Флаги окна настроек 4070 (19 б) -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

bool UAsdaNetSubsystem::SendResetState3()
{
	if (!Conn || !Conn->IsConnected()) return false;
	// Д6: тот же формат 56б, что 1028 — SendSecured собирает [секрет8][MD5(логин-блок,32)16][логин-блок32].
	const bool bOk = SendSecured((uint16)OP_C_LOGIN_WIN_ON_BUT00, LoginCredBlob32, 32);
	UE_LOG(LogAsdaNet, Log, TEXT("Сброс состояния 3 (1010) -> %s"), bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

// ★ ФИНАЛИЗАЦИЯ ВХОДА В МИР (эталон prog11/prog13) — анти-чит-токены «я загрузился/в мире».
//   Эталон шлёт: 4122 (netSendAntiCheatTokenOp4122@0xBCACA0) + 4124 (netSendAntiCheatToken@0xBCAEA0) на
//   LoadProgress==11, затем 4301 (netSendAntiCheatToken_2C40@0xE82C40) при g_GameState=1. Токен[0..7] =
//   buildAntiCheatToken@0x955C10 = ТОТ ЖЕ obfTick (Counter_NextWrapped, инкремент счётчика) → FillAntiReplaySecret.
//   4122/4124 (26б): [0..7]токен [8..23]MD5(clan,2) [24..25]clan(2б клан-индекс; нет клана→0).
//   4301 (40б): [0..7]токен [8..23]MD5(p16,16) [24..39]p16(16б; sub_925BC0 — точный состав «?», нули).
// ★ 07.11.2026 [ОРИГИНАЛ] (`asda.Clan.EntryOwnClanIndex`): `netSendAntiCheatTokenOp4122@0xBCACA0` и `netSendAntiCheatToken@0xBCAEA0` кладут в
//   тело СВОЙ clanId (`char+2408`, −1 — клана нет: сервер такой отбрасывает). Порт слал 0 — то есть запрашивал ЧУЖОЙ клан №0: на каждом
//   входе и переходе приходили его блок 4123 и состав 4125 (журналы игроков 01–03.11: `clanIndex=0` следом за своим). Блок затирал
//   инфо-блок вкладки «Клан», и 6508/6515/6511 уходили с индексом клана 0: сервер отказывал (6509 код 0, без 6527), галочки прав откатывались,
//   объявление не удалялось. 0 — прежнее поведение.
static ASDA_DEV_CVAR(int32, CVarClanEntryOwnIndex,
	TEXT("asda.Clan.EntryOwnClanIndex"), 1,
	TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = 4122/4124 при входе в мир несут свой индекс клана; 0 = прежнее (индекс 0 — приходил чужой клан)."),
	ECVF_Default);

// Индекс клана для токенов входа (самотест зовёт его же).
int32 AsdaEntryClanIndex(const UGameInstance* GI)
{
	if (CVarClanEntryOwnIndex.GetValueOnGameThread() == 0) { return 0; }
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return WS ? WS->GetSelfClanId() : -1;
}

void UAsdaNetSubsystem::SendEntryFinalize()
{
	if (!Conn || !Conn->IsConnected()) return;
	// Д6: клан-токен 26б = [токен8][MD5(clan,2)16][clan2]; SendSecured собирает тело (секрет — анти-чит obfTick).
	// ★ B19 (аудит №4): результат отправки не выбрасываем — три токена идут по ИГРОВОМУ сокету и
	//   обязаны кормить счётчик обрывов `TrackSend` наравне с остальными (иначе мёртвый сокет здесь
	//   молчит, а вход «финализируется» в пустоту).
	uint8 Clan[2] = { 0, 0 };
	AsdaBytes::WrI16(Clan, AsdaEntryClanIndex(GetGameInstance()));
	const bool bOk1 = TrackSend(SendSecured((uint16)OP_SEND_CLAN_BASE_INFO,    Clan, 2));   // 4122
	const bool bOk2 = TrackSend(SendSecured((uint16)OP_SEND_CLAN_USER_LIST_DB, Clan, 2));   // 4124
	// 4301 40б = [токен8][MD5(p16,16)16][p16 16б; sub_925BC0 «?», нули].
	uint8 P16[16]; FMemory::Memzero(P16, sizeof(P16));
	const bool bOk3 = TrackSend(SendSecured((uint16)OP_ALARM_GAME_SERVER_EVENT_INFO, P16, 16));   // 4301
	UE_LOG(LogAsdaNet, Log, TEXT("Финализация входа: 4122=%s 4124=%s 4301=%s"),
		bOk1 ? TEXT("OK") : TEXT("FAIL"), bOk2 ? TEXT("OK") : TEXT("FAIL"), bOk3 ? TEXT("OK") : TEXT("FAIL"));
}

// C→S 4001 CharLogonReq (36б: секрет8 + MD5(блок,12) + блок12) — запускает DB-цепочку сервера.
//   Блок (сервер CharLogonReq@0x527500): [0..3]=charId(iUserIndex), [4..5]=0 (не читается),
//   [6..7]=rawSlot(iCharIndex; 0=DisConnectType(5)), [8..9]/[10..11]=0xFFFF (SaveCurrentTilePos нет).
bool UAsdaNetSubsystem::SendCharLogonReq()
{
	if (!Conn || !Conn->IsConnected()) return false;
	uint8 Body[36];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* Blk = Body + 24;
	AsdaBytes::WrI32(&Blk[0], SelectedCharId);
	Blk[6] = LastSelectedRawSlot;                    // iCharIndex (u16, старший 0)
	Blk[8] = Blk[9] = Blk[10] = Blk[11] = 0xFF;      // нет сохранённой позиции → спавн-точка карты
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Blk, 12); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_CHAR_LOGON_REQ, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("CharLogonReq 4001: charId=%d rawSlot=%u -> %s"),
		SelectedCharId, LastSelectedRawSlot, bOk ? TEXT("OK") : TEXT("FAIL"));
	// ★ Тот же снимок гейта, что и у портального логона: сервер нас опознал, держать нечего.
	if (bOk && bTransferInProgress)
	{
		bTransferInProgress = false;
		UE_LOG(LogAsdaNet, Log, TEXT("[переход] завершён: логон входа в мир отправлен"));
	}
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 4001 (вход в мир)
}

// ★ C→S 5058 CharPortalLogonReq (45 б: секрет8 + MD5(payload,21) + payload 21) — ПОРТАЛЬНЫЙ вход
//   на карту после смены канала (эталон net_sendSpawnFieldSelect@0xF489E0, зовётся на стадии
//   LoadProgress==3.0 сразу после net_MarkSlotAndCloseSocket). Раскладка подтверждена СЕРВЕРОМ
//   (CharPortalLogonReq@0x52CBE0, лог "sPortalLogon.iStartPosX <= 0"):
//     [0..3]  i32 iUserIndex (наш SelectedCharId — он же UserIndex, как в 4001)
//     [4..5]  i16 iCharIndex (rawSlot; 0 = Info Error + дисконнект!)
//     [6..7]  i16 iStartPosX — тайл из 4055 @25
//     [8..9]  i16 iStartPosY — тайл из 4055 @27
//     [10..11] i16 доп. поле (эталон g_nPickIndexB; -1)
//     [12..20] кампвар: 3×u8 + 3×i16 (нули)
//   Сервер пишет позицию в CUser и запускает DB-цепочку 2089 → CharMake → 4003 с ЭТИМ тайлом.
// ★ 09.11.2026: слоты заявок CampWar в теле 5058 (P = начало полезной нагрузки, 21 б). Эталон net_sendSpawnFieldSelect@0xF489E0:
//   payload[12+k] = (байт) GameIndex `sub_F489B0` (+0x22D4+4k), payload[15+2k] = RegistIndex `sub_F48980` (+0x22E0+4k); без заявки −1
//   (инициализатор payload `sub_F488E0`). Общая точка — её же проверяет самотест asda.Net.SelfTest5058Slots.
namespace AsdaPortalLogon
{
	void FillCampWarSlots(const UGameInstance* GI, uint8* P)
	{
		const UAsdaCampWarSubsystem* CW = GI ? GI->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr;
		for (int32 K = 0; K < 3; ++K)
		{
			P[12 + K] = (uint8)(int8)(CW ? CW->GetRawRegGameIndex(K) : -1);
			const int16 Reg = (int16)(CW ? CW->GetRawRegIndex(K) : -1);
			FMemory::Memcpy(P + 15 + 2 * K, &Reg, 2);
		}
	}
}

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
// ═══ ★ 09.11.2026 САМОТЕСТ `asda.Net.SelfTest5058Slots [quit]` (офлайн): слоты CampWar в теле 5058 ═══
//   A1 без заявок — байты 12…14 = FF, слова 15…20 = FFFF (как эталон; раньше были нули).
//   A2 заявка штатным 6707 (код 1, регион 0, строка 2, RegistIndex 7) — слот 0 = {2, 7}, слоты 1/2 = −1.
//   A3 ResetState — снова все −1.
static FAutoConsoleCommandWithWorldAndArgs GAsdaNetSelfTest5058Slots(
	TEXT("asda.Net.SelfTest5058Slots"),
	TEXT("asda.Net.SelfTest5058Slots [quit] — офлайн-самотест слотов заявок CampWar в теле портального входа 5058"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.Net.SelfTest5058Slots");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaCampWarSubsystem* CW = GI ? GI->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr;
		if (!Net || !CW) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети / битвы армий")); return; }
		int32 Ok = 0, Fail = 0;
		auto Check = [&Ok, &Fail](bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaNet, Log, TEXT("[самотест 5058] ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaNet, Error, TEXT("[самотест 5058] FAIL %s"), *What); }
		};
		auto Slots = [GI](uint8 (&P)[21]) { FMemory::Memzero(P, 21); AsdaPortalLogon::FillCampWarSlots(GI, P); };
		auto Hex = [](const uint8 (&P)[21]) { FString H; for (int32 i = 12; i < 21; ++i) { H += FString::Printf(TEXT("%02X "), P[i]); } return H; };
		uint8 P[21];
		CW->ResetState();
		Slots(P);
		bool bAllFF = true; for (int32 i = 12; i < 21; ++i) { bAllFF &= P[i] == 0xFF; }
		Check(bAllFF, FString::Printf(TEXT("A1 без заявок: байты 12..20 = FF (%s)"), *Hex(P)));
		Net->DebugFeedPacket(6707, TArray<uint8>({ 1, 0, 2, 7, 0, 0, 0 }));
		// 6707 разбирает очередь битвы армий на следующих тиках — проверка через 0.5 с.
		TWeakObjectPtr<UWorld> WW(W);
		TWeakObjectPtr<UAsdaCampWarSubsystem> WCW(CW);
		const bool bQuit = KA.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, WCW, GI, bQuit, Ok, Fail, Slots, Hex]() mutable
		{
			auto Check2 = [&Ok, &Fail](bool b, const FString& What)
			{
				if (b) { ++Ok; UE_LOG(LogAsdaNet, Log, TEXT("[самотест 5058] ok   %s"), *What); }
				else   { ++Fail; UE_LOG(LogAsdaNet, Error, TEXT("[самотест 5058] FAIL %s"), *What); }
			};
			uint8 Q[21];
			Slots(Q);
			const int16 R0 = AsdaBytes::RdI16(Q + 15), R1 = AsdaBytes::RdI16(Q + 17), R2 = AsdaBytes::RdI16(Q + 19);
			Check2(Q[12] == 2 && R0 == 7 && Q[13] == 0xFF && Q[14] == 0xFF && R1 == -1 && R2 == -1,
				FString::Printf(TEXT("A2 заявка 6707 (регион 0, строка 2, заявка 7): слот 0 = {%d, %d}, слоты 1/2 = -1 (%s)"), (int8)Q[12], R0, *Hex(Q)));
			if (WCW.IsValid()) { WCW->ResetState(); }
			Slots(Q);
			bool bFF = true; for (int32 i = 12; i < 21; ++i) { bFF &= Q[i] == 0xFF; }
			Check2(WCW.IsValid() && bFF, FString::Printf(TEXT("A3 после ResetState снова все -1 (%s)"), *Hex(Q)));
			AsdaSelfTestKit::Finish(WW.Get(), TEXT("asda.Net.SelfTest5058Slots"), bQuit, LogAsdaNet, TEXT("[самотест 5058] слоты CampWar"), Ok, Fail);
		}), 0.5f, false);
	}));
#endif

bool UAsdaNetSubsystem::SendCharPortalLogonReq()
{
	if (!Conn || !Conn->IsConnected()) return false;
	uint8 Body[45];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* P = Body + 24;
	const int32 Uidx = SelectedCharId;
	FMemory::Memcpy(P + 0, &Uidx, 4);
	const int16 CharIdx = (int16)LastSelectedRawSlot;
	FMemory::Memcpy(P + 4, &CharIdx, 2);
	const int16 SX = (int16)PortalTileX, SY = (int16)PortalTileZ;
	FMemory::Memcpy(P + 6, &SX, 2);
	FMemory::Memcpy(P + 8, &SY, 2);
	P[10] = P[11] = 0xFF;                                  // g_nPickIndexB = -1
	// ★ 09.11.2026: слоты заявок CampWar (эталон net_sendSpawnFieldSelect@0xF489E0: байт +12+k = GameIndex `sub_F489B0`,
	//   слово +15+2k = RegistIndex `sub_F48980`; без заявки −1). Сервер CharPortalLogonReq@0x52CBE0 переписывает ими
	//   CUser+0x3F439/+0x3F43C. Были нули → после любого перехода игрок «записан в слот 0» (отказ 6014/P123, 3615/3609 с индексом 0).
	AsdaPortalLogon::FillCampWarSlots(GetGameInstance(), P);
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(P, 21); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_CHAR_PORTAL_LOGON_REQ, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Warning, TEXT("CharPortalLogonReq 5058: userIdx=%d rawSlot=%u стартовый тайл=(%d,%d) -> %s"),
		Uidx, LastSelectedRawSlot, PortalTileX, PortalTileZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	bPortalPending = false;
	// ★★★ ГЕЙТ ПЕРЕХОДА СНИМАЕТСЯ ЗДЕСЬ, а не по «успеху входа». Живой разбор 24.08.2026:
	//   при ПОРТАЛЬНОМ входе подтверждение 6004 не приходит вовсе (ответ на 5058 — раскладка 5059),
	//   поэтому гейт висел все 30 секунд до страховки. Всё это время движение игрока не уходило
	//   на сервер: игрок бегал локально, сервер считал его в точке прибытия — и на атаку отвечал
	//   «цель вне досягаемости». Логон отправлен → сервер нас опознал → держать нечего.
	if (bOk && bTransferInProgress)
	{
		bTransferInProgress = false;
		UE_LOG(LogAsdaNet, Log, TEXT("[переход] завершён: портальный логон отправлен"));
	}
	return TrackSend(bOk);   // ★ B19 (аудит №4): игровой сокет — отправка идёт в счётчик обрывов, 5058 (портальный вход)
}

// ★★★ C→S 4024 — ЭМОЦИЯ / ДЕЙСТВИЕ ПЕРСОНАЖА (эталон `Send_CharStatePos@0x9F9EF0`, network.c:5063).
//   Кадр 29 б: MD5(тело13) впереди + тело13. Анти-реплей-секрета у этого пакета НЕТ.
//   Раскладка тела и серверные гейты — см. объявление в заголовке.
//   ★ 27.09.2026 (`Reports/owner_0927/pkt4024_facing.md`): +5/+9 — НАПРАВЛЕНИЕ ВЗГЛЯДА (x, z), не позиция.
//   Параметры переименованы (было GameX/GameZ); вызывающие берут `AAsdaPlayerCharacter::GetFacingDirGame`.
//   Узкий перехват самотеста (`ArmSendCapture(…, 4024)`, `asda.Net.SelfTest4024`) — тот же приём, что у
//   `SendStop4008`: при перехватчике сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ.
bool UAsdaNetSubsystem::SendCharStatePos4024(int32 ActionType, float FaceDirX, float FaceDirZ)
{
	const bool bStand = IsStandCapture((uint16)OP_SEND_CHAR_STATE_POS);   // тело уходит в перехватчик, не в сеть
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		// ★ Гейт «души» (SoulState==1): 4024 в диспетчере под `sub_52EEA0` (GameServer.c:20114, `sub_546730`).
		if (FAsdaOutgoingGate::Block(this, (uint16)OP_SEND_CHAR_STATE_POS, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }
	}
	uint8 Body[29];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* T = Body + 16;
	const uint16 Uidx = SelfUserNum;          // ★ именно userNum из 4003, не charId (закон charid-vs-usernum)
	const uint16 Act  = (uint16)ActionType;
	FMemory::Memcpy(T + 0, &Uidx, 2);
	FMemory::Memcpy(T + 2, &Act, 2);
	T[4] = 0;                                  // паддинг: сервер перезапишет его флагом шлема
	FMemory::Memcpy(T + 5, &FaceDirX, 4);
	FMemory::Memcpy(T + 9, &FaceDirZ, 4);
	{ FMD5 Md5; Md5.Update(T, 13); Md5.Final(Body); }
	if (bStand)
	{
		// Конверт 'R' — «сырой» `Conn->SendPacket` (MD5 внутри тела, секрета нет); пад = дефолт `bPad = true`.
		const auto Cap = SendCaptureForStand;   // копия: перехватчик вправе снять себя изнутри
		Cap((uint16)OP_SEND_CHAR_STATE_POS, Body, (int32)sizeof(Body), true, TEXT('R'));
		return true;
	}
	const bool bOk = Conn->SendPacket((uint16)OP_SEND_CHAR_STATE_POS, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("→4024 действие: userNum=%u actionType=%d курс=(%.3f,%.3f) -> %s"),
		Uidx, ActionType, FaceDirX, FaceDirZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("ОТПР 4024 действие/стойка персонажа: userNum=%d действие=%d курс=(%.3f,%.3f) -> %s"),
		(int32)Uidx, ActionType, FaceDirX, FaceDirZ, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// cp1251-энкод ИМЕНИ в буфер фиксированной длины: буфер обнуляется целиком, значащих байт не более
//   `MaxLen − 1` (последний остаётся нулём-терминатором, как у `strcpy` эталона в char[20]).
// ★ D9 (аудит `code_dedup_audit_2.md`): сама таблица перекодировки переехала к декодеру —
//   `FAsdaLoginData::EncodeCp1251`; здесь остаётся ТОЛЬКО правило буфера (memzero + резерв под ноль).
static void EncodeCp1251(const FString& In, uint8* Out, int32 MaxLen)
{
	FMemory::Memzero(Out, MaxLen);
	FAsdaLoginData::EncodeCp1251(In, Out, FMath::Max(0, MaxLen - 1));
}

bool UAsdaNetSubsystem::SendCreateChar(uint8 Slot, const FString& Name, uint8 Gender, uint8 Hair,
	uint8 Color, uint8 Face, uint8 Constellation, const TArray<int32>& PartCodes, uint16 Channel)
{
	if (!Conn || !Conn->IsConnected()) return false;
	uint8 Body[76];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* P = &Body[24];                             // payload после 24б заголовка (secret[8]+digest[16])
	AsdaBytes::WrI32(&P[0], (int32)UserIndex);        // clientId#1 = account id (sub_929928)
	AsdaBytes::WrI16(&P[4], (int32)Channel);          // word#2 @+4 = НОМЕР КАНАЛА (u16, sub_9303EA=oBBE4C)
	P[6] = Slot;                                      // @+6 = СЫРОЙ slotId 10/11/12 (позиция+10), НЕ индекс (эталон pDlg+473524)
	EncodeCp1251(Name, &P[7], 20);                    // имя @+7, ≤20, cp1251
	P[27] = Gender;
	P[28] = Hair;
	P[29] = Color;
	P[30] = Face;
	P[31] = Constellation;
	// 5×i32 part-коды @+32 в порядке slotOrder {0,1,4,2,3} (эталон createPacket[i+8]=parts[slotOrder[i]]).
	//   PartCodes индексируется partType 0..4; переставляем здесь (как эталон), а не в вызывающем.
	static const int32 SlotOrder[5] = { 0, 1, 4, 2, 3 };
	for (int32 i = 0; i < 5; ++i)
	{
		const int32 Pt = SlotOrder[i];
		const int32 Code = PartCodes.IsValidIndex(Pt) ? PartCodes[Pt] : 0;
		FMemory::Memcpy(&P[32 + i * 4], &Code, 4);
	}
	// ★ MD5 целостности [8..23] = MD5 52-байтного payload (эталон ui_core.c:95010 Net_Md5Digest16(salt,createPacket,0x34)).
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Body + 24, 52); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_UI_CHAR_CREATE_SUBMIT_REQUEST, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("SendCreateChar slot=%u '%s' gender=%u hair=%u/%u face=%u const=%u -> %s"),
		Slot, *Name, Gender, Hair, Color, Face, Constellation, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

bool UAsdaNetSubsystem::SendDeleteChar(uint8 RawSlotId, uint16 Channel)
{
	if (!Conn || !Conn->IsConnected()) return false;
	// Эталон netSendReq1018@0xF09B20: payload[0]=rec[1052]=UserIndex, payload[1].b0=rec[1076]=сырой slotId,
	//   payload[1].w1=rec[1077]=канал. MD5[8..23]=MD5(payload,7).
	uint8 Body[31];
	FMemory::Memzero(Body, sizeof(Body));
	uint8* P = &Body[24];                             // payload 7б после 24б заголовка
	AsdaBytes::WrI32(&P[0], (int32)UserIndex);        // charId u32 @+0 = UserIndex/account (rec[1052]=sub_929928)
	P[4] = RawSlotId;                                 // slotNum u8 @+4 = СЫРОЙ slotId (rec[1076]=10/11/12)
	AsdaBytes::WrI16(&P[5], (int32)Channel);          // word u16 @+5 = канал (rec[1077]=sub_9303EA)
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Body + 24, 7); Md5.Final(Dig); FMemory::Memcpy(Body + 8, Dig, 16); }
	FillAntiReplaySecret(Body);
	const bool bOk = ConnSendSecret((uint16)OP_CHAR_DELETE_REQ, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("SendDeleteChar userIdx=%u rawSlot=%u ch=%u -> %s"),
		UserIndex, RawSlotId, Channel, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}
