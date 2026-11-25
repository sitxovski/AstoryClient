// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_Social.cpp — ★ 22.10.2026 часть UAsdaNetSubsystem, вынесенная из AsdaNetSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_net_2026-10-22/plan.json).
//   Чат (отправка), контекстное меню по игроку, соулмейт, обмен 5282…5293, дуэль, отряд 5090…5100,
//   поиск отряда 3410…3424, клан, друзья, поиск игрока, зов друга 5237/5427.
//   #include — те же, что у AsdaNetSubsystem.cpp, в том же порядке; общие хелперы — AsdaNetSubsystemInternal.h.
#include "AsdaNetSubsystem.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: серверные часы обычной атаки (4026/4028/5256)
#include "AsdaTimedSend.h"                     // ★ 15.10.2026: поток точной отправки (стендовая постановка 4028)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "World/AsdaWorldSubsystem.h"
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
#include "Text/AsdaTextSubsystem.h"          // loc-строки форматов чата (20030/1187/1109/8200/…)
#include "UI/AsdaChatWindowSubsystem.h"      // RankColor / SlotColor — таблицы каналов и рангов
// Дд6: FAsdaItemInstance::kRecordSize (57 б) — длина записи предмета   ★ R3: комментарий снятого включения "UI/AsdaInventorySubsystem.h" (файл им не пользовался) — сохранён
// 5055 отказ портала: нотисы 1096/1097 в системный чат   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNotice.h"              // ★ 27.09: отказы 5055/6201 — плашка 221 и пара «плашка + журнал» (общие точки)
// ★ 27.09: ColGold() = эталонный −11514 отказов перехода   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaHotbarSubsystem.h"     // сохранение раскладки панели перед переходом (hotbar_SendAllLayouts)
#include "World/AsdaPossessGate.h"      // ★ гейт действий при одержимости (SoulState==2), см. шапку заголовка
#include "AsdaOutgoingGate.h"               // ★ единый шлюз исходящих пакетов (E1, 14.09.2026)
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


// ★ ДЕФЕКТ ОРИГИНАЛА Д1 (поиск отряда, 3424 `party_sendBossInfoSecure@0xDBEC10`): байт +6 тела
//   присваивается ДВАЖДЫ подряд — сначала профессия (`g_LocalPlayer+1093`), затем класс
//   (`g_LocalPlayer+1080`). Второе присваивание затирает первое, а байт +7, куда класс и должен
//   был лечь, остаётся нулём. 1 = слать ровно как оригинал; 0 (по умолчанию) = профессия в +6,
//   класс в +7 — как во всех остальных телах этой же фичи (3410/3416/3418).
static ASDA_DEV_CVAR(int32, CVarPartyFindLegacyBossJobByte,
	TEXT("asda.PartyFind.LegacyBossJobByte"), 0,
	TEXT("1 = 3424 шлёт класс в поле профессии и ноль в поле класса (дефект оригинала); 0 = верно."),
	ECVF_Default);

// ★ 6616 — анонс редкой добычи. 1:1 с эталоном netSendSignedPacket6616@0xA0F140 (network.c:7681):
//     msg[0..3]   = itemCode (dword)
//     msg[4..23]  = имя персонажа (strcpy, char[20], cp1251)
//     Body[0..7]  = анти-реплей-секрет (FillAntiReplaySecret — тот же obfTick, что у 20000/5012/6084)
//     Body[8..23] = Net_Md5Digest16(g_NetMd5Salt, msg, 0x18) = обычный MD5 24-байтного msg
//     Body[24..47]= msg
//   Итог 48 байт — ровно то, что живой GameServer проверяет в sub_523F00(pkt, 24, idx, 1)
//   (MD5 тела @pkt+33 против pkt+17) и sub_524390 (монотонный счётчик из секрета).
// ═══════════════════════ ОТПРАВКА ЧАТА (ЭТАП 2) ═══════════════════════════════════════════════
// Строка протокола — cp1251 (как во всём остальном обмене). Возвращает число записанных байт.
// ★ D9 (аудит `code_dedup_audit_2.md`): энкодер жил ТРЕМЯ копиями в этом файле при уже едином
//   декодере. Тело переехало к декодеру — `FAsdaLoginData::EncodeCp1251`; здесь остался форвард
//   (имя `AsdaChatToCp1251` читается в девяти местах сборки чат-пакетов).
static FORCEINLINE int32 AsdaChatToCp1251(const FString& S, uint8* Out, int32 MaxBytes)
{
	return FAsdaLoginData::EncodeCp1251(S, Out, MaxBytes);
}

// ★ [ПОРТ] 08.10.2026 (RU+EN+AR, `Reports/owner_1008/arabic_text.md`): ТЕЛО свободного текста чата — гибрид
//   cp1251 / маркер 0x98 + cp1256 / UTF-8 (`FAsdaLoginData::EncodeWireText`): представимое в cp1251 уходит как раньше
//   байт-в-байт (кроме редких строк, байты которых приём счёл бы UTF-8, — они уходят UTF-8, ревизия С1), арабский — маркером,
//   смешанный — UTF-8 с обрезкой по границе символа. Сервер тело пересылает прозрачно (strlen+memcpy). ИМЕНА идут через
//   `AsdaChatToCp1251` = тот же гибрид (`EncodeCp1251` с 08.10 — гибрид; ревизия М5: прежняя фраза «имена — cp1251» устарела).
//   Откат: `asda.Text.WireCodec 0`.
static FORCEINLINE int32 AsdaChatTextToWire(const FString& S, uint8* Out, int32 MaxBytes)
{
	return FAsdaLoginData::EncodeWireText(S, Out, MaxBytes);
}

// ★★ ПАДДИНГ. Эталонный `CNetwork_Send@0x12A9330` добавляет хвост `(счётчик += 11) % 20` байт только
//   при `a6 == 1`. Полная выборка всех вызовов эталона даёт РОВНО такой список опкодов с `a6 = 0`
//   (пада НЕТ): 4120, 5084, 5086, 5088, 5105, 5256, 6158, 6560, 20000 и группа Indun/9901-9909.
//   Порт слал их с падом по умолчанию. Чату это сходило с рук (его обработчики длину не сверяют),
//   а 5256/6158 сервер из-за лишнего хвоста ВЫБРАСЫВАЛ МОЛЧА — см. `SendSkillUseBody`.
//   Ниже у всех перечисленных отправителей `bPad = false` — 1:1 с эталоном.

// 5084 — «крик»/Общий. Тело = сырой текст (chat.c:2435).
bool UAsdaNetSubsystem::SendChatShout5084(const FString& Text)
{
	// ★ [ПОРТ] 08.10.2026: перехват самотеста `asda.Text.SelfTestArabic` (тот же приём, что у 5086/4120): при перехватчике
	//   сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒ порядок прежний.
	const bool bStand = IsStandCapture((uint16)5084);
	if (!bStand && (!Conn || !Conn->IsConnected())) { return false; }
	// ★ Гейт «души» (SoulState==1): чат у сервера под `sub_52EEA0` (GameServer.c:19939), прямая
	//   отправка идёт мимо общих конвертов — проверка здесь. См. World/AsdaPossessGate.h.
	if (!bStand && FAsdaOutgoingGate::Block(this, (uint16)5084, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }
	uint8 Body[200];
	const int32 N = AsdaChatTextToWire(Text, Body, 199);   // эталон обрезает на 200 (chat.c:2440)
	if (N <= 0) { return false; }
	if (bStand) { const auto Cap = SendCaptureForStand; Cap((uint16)5084, Body, N, false, TEXT('R')); return true; }   // копия: см. SendSecured
	return TrackSend(Conn->SendPacket((uint16)5084, Body, N, 0, 0, /*bPad=*/false));
}

// 5086 — ГЛОБАЛЬНЫЙ чат (режим ввода 3). 27 байт заголовка + текст (chat.c:3053-3084).
//
// ★★★ РАЗБОР 2026-08-05 (chatcd-agent) — БАЙТ +2 ЭТО **НЕ** НОМЕР ЧАТ-КАНАЛА.
//   Прошлый заход трактовал +2 как «номер широкого канала» и слал туда InputState 0/1/2.
//   Живой тест это опроверг: «Торговля» приходила подписанной как **[Сервер]**, «Поиск отряда»
//   как **[кан.]**, «По всем каналам» — вообще без подписи. И это НЕ ошибка подписи: юзер сверил
//   с ОРИГИНАЛЬНЫМ клиентом — тот показывает ровно то же самое.
//
//   ЧТО ЭТО НА САМОМ ДЕЛЕ (эталон, а не догадка):
//     • ОТПРАВКА, chat.c:3053 — `msgHeader[2] = 0;` ⇒ оригинальный клиент кладёт в +2
//       **ВСЕГДА НОЛЬ**. Другого значения он туда не пишет НИКОГДА, ни в одной ветке;
//     • ПРИЁМ, `Recv_ChatMessage @0xBA88E0` (network.c:10983-10995) — это ОБЛАСТЬ ОХВАТА
//       глобального объявления, а не чат-канал:
//           LOBYTE(header[1]) == 0 → sprintf(line, "[%s]: %s", имя, текст)          // обычный глобальный
//           LOBYTE(header[1]) == 1 → GetLocalizedString(1187) = «кан.»   → "[кан.]Имя: текст"
//           LOBYTE(header[1]) == 2 → GetLocalizedString(1109) = «Сервер» → "[Сервер]Имя: текст"
//           > 2                    → строка НЕ формируется вовсе
//       Значения 1 и 2 — это ОБЪЯВЛЕНИЯ ПО КАНАЛУ и ПО СЕРВЕРУ (ср. loc 20034 «[Канал][ss1] >> [ss2]»
//       и 20035 «[Сервер][ss1] >> [ss2]»), их рассылает администрация, а не игрок.
//   ⇒ ★ НИКАКОГО ЧИСЛА, КОТОРОЕ ДАЛО БЫ «Торговля» ИЛИ «Поиск отряда», В 5086 НЕ СУЩЕСТВУЕТ.
//     Приёмная сторона (и наша, и оригинального клиента) всё это время работала ПРАВИЛЬНО.
//   Правильный маршрут широких каналов — своя пара опкодов **6560 → 6561**, см. SendChatChannel6560.
//
//   ★ Ограничение сервера (`recvGlobalChatClient @0x547670`, GameServer.c:124965):
//   `char chatText[80]` и `if (chatLen <= 80)` — текст длиннее 80 байт сервер не рассылает вовсе.
//   Наш лимит ввода 68 (chat.c:829) в него укладывается. Ранг сервер подставляет в +23
//   ТОЛЬКО при нулевом байте +2 — что при эталонной отправке выполняется всегда.
bool UAsdaNetSubsystem::SendChatNormal5086(const FString& Text, const FString& SelfName, uint8 ChannelType)
{
	// ★ 30.09.2026 (волна Л3 торговли): перехват самотеста (`asda.Trade.SelfTestStoreL3` — реклама премиум-лавки из рендера своей
	//   вывески) — тот же приём, что у 4120: при перехватчике сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒
	//   порядок прежний.
	const bool bStand = IsStandCapture((uint16)5086);
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		if (FAsdaOutgoingGate::Block(this, (uint16)5086, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }   // гейт «души», GameServer.c:19945
	}
	uint8 Body[227];
	FMemory::Memzero(Body, sizeof(Body));
	// +0..+1 — WORD из ctor sub_931204: тела ctor'а в срезе НЕТ, поэтому шлём 0 (не выдумываем).
	// ★ +2 — ОБЛАСТЬ ОХВАТА, эталон кладёт 0 (chat.c:3053). Ненулевое значение = объявление
	//   «по каналу»/«по серверу», игроку оно не принадлежит — предупреждаем и в лог.
	Body[2] = ChannelType;
	AsdaChatToCp1251(SelfName, Body + 3, 23);               // char[24] имя (chat.c:3055)
	const int32 N = AsdaChatTextToWire(Text, Body + 27, 199 - 27);
	if (N <= 0) { return false; }
	if (ChannelType != 0)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("5086: байт +2 = %u — это ОБЛАСТЬ ОХВАТА (1 = «кан.», 2 = «Сервер»), а НЕ чат-канал! ")
			TEXT("Эталон (chat.c:3053) всегда кладёт 0. Широкие каналы шлются опкодом 6560"), ChannelType);
	}
	UE_LOG(LogAsdaNet, Log, TEXT("5086 отправка: тело[%d] %s"), 27 + N, *HexDumpBody(Body, 27 + N));
	if (bStand) { const auto Cap = SendCaptureForStand; Cap((uint16)5086, Body, 27 + N, false, TEXT('R')); return true; }   // копия: см. SendSecured
	return TrackSend(Conn->SendPacket((uint16)5086, Body, 27 + N, 0, 0, /*bPad=*/false));
}

// 6560 — «широкие» каналы 0/1/2. BYTE тип + текст (chat.c:2529-2557).
//
// ★★★ ЭТО И ЕСТЬ ЕДИНСТВЕННЫЙ ПРАВИЛЬНЫЙ МАРШРУТ «широких» каналов. Пара опкодов **6560 → 6561**:
//   ОТПРАВКА `ChatInput_SendChannel @0xBA4A20` (chat.c:2466): `channelHeader[0] = channelType;`
//     ⇒ номер канала = РОВНО индекс режима ввода (0 «По всем каналам» / 1 «Поиск отряда» /
//     2 «Торговля»), текст с +1, `sendLen = strlen + 1`. Никакой перестановки номеров нет.
//   ПРИЁМ `Recv_WhisperOrSystemMessage @0xBA8560` (network.c:10823), диспетчер network.c:817
//     `if (opcode == 6561) Recv_WhisperOrSystemMessage(g_pChatWnd, pktBuf);`:
//       заголовок 32 байта, текст с +32;
//       header[0] = вид: 1 = сообщение широкого канала, 5 = нотис предмета (loc 10222);
//       header[1] = НОМЕР КАНАЛА (0/1/2);  header[12…] = имя отправителя;
//       строка = формат loc **20030** = "[%s] > [%s]" ⇒ «[Имя] > [текст]»;
//       `Chat_AddChatLine(self, header[1] + 7, line, -12602, …)` — категория 7/8/9, а в
//       `Chat_AddChatLine` (chat.c:5071) `case 7/8/9: colorType = a2 - 7;` и цвет берётся из
//       таблицы каналов `tabNameColorRecs[24 * colorType]` ⇒ ЦВЕТ СТРОКИ = цвет слота канала
//       (0 = −12602 FFFFCEC6, 1 = −7564801 FF8C91FF, 2 = −6697984 FF99CC00).
//   ★★★ НО: в живом GameServer.exe обработчика 6560 НЕТ, и 6561 он НЕ шлёт —
//   `grep 6560/0x19A0/6561/0x19A1` по `RE/ida/server_work/GameServer.c` даёт **0 совпадений**
//   на все четыре формы; в диспетчере есть ряды `case 0x1964…0x199B` и `case 0x19AF…0x19D0`,
//   а 0x19A0/0x19A1 попадают ровно в дыру между ними. ⇒ круг НЕ замыкается без серверного патча
//   (ТЗ — в отчёте chat_main_window.md, Д13.3 + Д15).
bool UAsdaNetSubsystem::SendChatChannel6560(uint8 ChannelType, const FString& Text)
{
	// ★ [ПОРТ] 08.10.2026: перехват самотеста `asda.Text.SelfTestArabic` (тот же приём, что у 5086/4120): при перехватчике
	//   сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒ порядок прежний.
	const bool bStand = IsStandCapture((uint16)6560);
	if (!bStand && (!Conn || !Conn->IsConnected())) { return false; }
	if (!bStand && FAsdaOutgoingGate::Block(this, (uint16)6560, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	uint8 Body[201];
	FMemory::Memzero(Body, sizeof(Body));
	Body[0] = ChannelType;
	const int32 N = AsdaChatTextToWire(Text, Body + 1, 199);
	if (N <= 0) { return false; }
	UE_LOG(LogAsdaNet, Log,
		TEXT("6560 отправка: канал=%u (эталон chat.c:2529 channelHeader[0] = channelType) тело[%d] %s"),
		ChannelType, 1 + N, *HexDumpBody(Body, 1 + N));
	if (bStand) { const auto Cap = SendCaptureForStand; Cap((uint16)6560, Body, 1 + N, false, TEXT('R')); return true; }   // копия: см. SendSecured
	return TrackSend(Conn->SendPacket((uint16)6560, Body, 1 + N, 0, 0, /*bPad=*/false));
}

// 5088 — шёпот. BYTE тип (0 обычный / 1 соулмейт) + char[20] имя + текст (chat.c:2813-2939).
bool UAsdaNetSubsystem::SendChatWhisper5088(uint8 Kind, const FString& TargetName, const FString& Text)
{
	// ★ [ПОРТ] 08.10.2026: перехват самотеста `asda.Text.SelfTestArabic` (тот же приём, что у 5086/4120): при перехватчике
	//   сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒ порядок прежний.
	const bool bStand = IsStandCapture((uint16)5088);
	if (!bStand && (!Conn || !Conn->IsConnected())) { return false; }
	if (!bStand && FAsdaOutgoingGate::Block(this, (uint16)5088, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }   // гейт «души», GameServer.c:19951
	uint8 NameBuf[24];
	FMemory::Memzero(NameBuf, sizeof(NameBuf));
	const int32 NameLen = AsdaChatToCp1251(TargetName, NameBuf, 21);
	if (NameLen > 20) { return false; }                      // эталон: > 0x14 → отказ, loc 6942
	uint8 Body[221];
	FMemory::Memzero(Body, sizeof(Body));
	Body[0] = Kind;
	FMemory::Memcpy(Body + 1, NameBuf, 20);
	// Эталон заменяет апостроф на обратную кавычку (bufferReplaceByte, chat.c:2815).
	FString T = Text;
	T.ReplaceInline(TEXT("'"), TEXT("`"), ESearchCase::CaseSensitive);
	const int32 N = AsdaChatTextToWire(T, Body + 21, 179);     // чанк эталона — 179 байт (chat.c:2816)
	if (N <= 0) { return false; }
	if (bStand) { const auto Cap = SendCaptureForStand; Cap((uint16)5088, Body, 21 + N, false, TEXT('R')); return true; }   // копия: см. SendSecured
	return TrackSend(Conn->SendPacket((uint16)5088, Body, 21 + N, 0, 0, /*bPad=*/false));
}

// 5105 — отряд. WORD индекс цели + текст (social.c:9591-9598).
bool UAsdaNetSubsystem::SendChatParty5105(uint16 TargetIdx, const FString& Text)
{
	// ★ [ПОРТ] 08.10.2026: перехват самотеста `asda.Text.SelfTestArabic` (тот же приём, что у 5086/4120): при перехватчике
	//   сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒ порядок прежний.
	const bool bStand = IsStandCapture((uint16)5105);
	if (!bStand && (!Conn || !Conn->IsConnected())) { return false; }
	if (!bStand && FAsdaOutgoingGate::Block(this, (uint16)5105, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }   // гейт «души», GameServer.c:19993
	uint8 Body[202];
	FMemory::Memzero(Body, sizeof(Body));
	AsdaBytes::WrI16(&Body[0], (int32)TargetIdx);
	const int32 N = AsdaChatTextToWire(Text, Body + 2, 199);
	if (N <= 0) { return false; }
	if (bStand) { const auto Cap = SendCaptureForStand; Cap((uint16)5105, Body, 2 + N, false, TEXT('R')); return true; }   // копия: см. SendSecured
	return TrackSend(Conn->SendPacket((uint16)5105, Body, 2 + N, 0, 0, /*bPad=*/false));
}

// ★ 27.09.2026 (СРЕДН-2 ревизии friend_gate): префикс «(Друг)» клан-чата — единственная точка правила.
//   [ОРИГИНАЛ] `chatSendMessage@0xBCAA00` (chat.c:5671-5679): `if (char->gap_8BE[3]) sprintf(Str, "%s%s",
//   UI_FormatLocalizedNotice(…, 6937, …), a2)` — loc 6937 «(Друг)» ИМЕННО этого обработчика, склейка без пробела.
FString UAsdaNetSubsystem::GetClanChatSoulPrefix() const
{
	if (SelfSoulSilhouette == 0) { return FString(); }   // `gap_8BE[3] == 0` → текст как есть
	const UGameInstance* GI = GetGameInstance();
	const UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	return Txt ? Txt->GetTextById(6937) : FString();   // пустой loc = нет префикса (не выдумываем текст)
}

// 4120 — клан/сообщество. 28 байт заголовка + текст (chat.c:5666-5684).
//   [ПРОТОКОЛ] @0 i16 clanId (`Src[0] = char+0x968`, chat.c:5666), @2 i32 userIdx = charId (`char+1052`; ★ 27.09.2026 вызывающий
//   передаёт `SelectedCharId`, прежде — userNum), @8 char[20] имя, @28 текст;
//   `CNetwork_Send(…, 4120, 0, 1, 1, 0, 0)` — без конверта и без пада. Сервер (`sub_403F50`, GameServer.c:1509)
//   заголовок клиента НЕ читает (clanId/имя/userIdx берёт из CUser), текст режет по своему пределу:
//   сток 80 байт (`v14 <= 80`, длиннее — пакет молча выброшен), живой сервер с патчем P14 — 160.
bool UAsdaNetSubsystem::SendChatClan4120(int32 SelfCharId, const FString& SelfName, const FString& Text)
{
	// ★ 27.09.2026: узкий перехват самотеста (`asda.Clan.FriendGateSelfTest`) — тот же приём, что у `SendMove4006`:
	//   при перехватчике сокета нет, гейты живой сессии пропускаются. В игре перехватчик ПУСТ ⇒ порядок прежний.
	const bool bStand = IsStandCapture((uint16)4120);
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		if (FAsdaOutgoingGate::Block(this, (uint16)4120, EAsdaGate::TransferWide)) { return false; }   // E1 шаг 2: гейт перехода (asda.Net.TransferGateAllChannels)
	}
	uint8 Body[228];
	FMemory::Memzero(Body, sizeof(Body));
	// ★ 27.09.2026: [ОРИГИНАЛ] @0 — СВОЙ clanId (`gap_93A[32]` = char+0x968; прежде слали 0). Общая точка — зеркало мира.
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AsdaBytes::WrI16(&Body[0], WS ? WS->GetSelfClanId() : -1);
	AsdaBytes::WrI32(&Body[2], SelfCharId);   // @2 charId
	AsdaChatToCp1251(SelfName, Body + 8, 19);                // char[20] имя на +8
	// ★ 27.09.2026: [ОРИГИНАЛ] при «Друг» (SoulState ≠ 0) текст = loc 6937 + текст — склейка ДО перевода в cp1251.
	const FString Full = GetClanChatSoulPrefix() + Text;     // строка `Str` эталона
	const int32 N = AsdaChatTextToWire(Full, Body + 28, 199 - 28);
	if (N <= 0) { return false; }
	UE_LOG(LogAsdaNet, Log, TEXT("4120 отправка: clanId=%d «Друг»=%u тело[%d] %s"),
		(int32)AsdaBytes::RdI16(&Body[0]), (uint32)SelfSoulSilhouette, 28 + N, *HexDumpBody(Body, 28 + N));
	if (bStand) { const auto Cap = SendCaptureForStand; Cap((uint16)4120, Body, 28 + N, false, TEXT('R')); return true; }   // копия: см. SendSecured
	return TrackSend(Conn->SendPacket((uint16)4120, Body, 28 + N, 0, 0, /*bPad=*/false));
}

bool UAsdaNetSubsystem::SendRareItemNotice6616(const FString& CharName, int32 ItemCode)
{
	if (!IsSendLinkUp()) { return false; }

	// ★★★ СТРАХОВКА (наша, в оригинале её нет): не копить серверный счётчик «неизвестных пакетов».
	//   Обоснование адресами — в шапке `kMaxUnacked6616` (AsdaNetSubsystem.h). Коротко: если на
	//   ЭТОМ процессе GameServer наша AsdaPatch.dll не поднялась, обработчика 6616 нет, пакет
	//   уходит в `default` @0x0042F020 → `IncUnknownPacketCount@0x005244B0` инкрементит CUser+0x1E,
	//   и на ШЕСТОМ таком пакете `IsUnknownPacketOverLimit@0x005244F0` (`cmp [ecx+0x1E],5`)
	//   даёт кик `DisconnectUser(idx,3)` @0x0052EDB0 («NotKnow Packet Count»).
	//   Признак «обработчик есть» — приход ЛЮБОГО 6131: P01 рассылает его всем слотам процесса,
	//   включая отправителя (`SendPacket_AllPlayer_InMap@0x0052E520` не исключает себя).
	if (bAnnounce6616Blocked)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("→6616 НЕ ШЛЮ (предмет %d): %d отправки подряд остались без единого 6131 — считаем, что на этом ")
			TEXT("сервере нет обработчика 6616 (AsdaPatch/P01 не поднялась). Дальнейшие отправки копили бы ")
			TEXT("CUser+0x1E (IncUnknownPacketCount@0x005244B0) и на 6-й привели бы к кику ")
			TEXT("«NotKnow Packet Count» (IsUnknownPacketOverLimit@0x005244F0 → DisconnectUser@0x0052EDB0). ")
			TEXT("Блокировка снимется при первом же входящем 6131 или при следующем входе в мир."),
			ItemCode, Unacked6616);
		return false;
	}

	FAsdaPacketWriter W(24);
	W.I32(0, ItemCode);
	// Имя в cp1251, максимум 19 значащих байт + завершающий 0 (эталон strcpy в буфер 20 б).
	// ★ D9: третья копия энкодера жила прямо здесь — теперь общий `FAsdaLoginData::EncodeCp1251`.
	{
		const int32 Written = W.Cp1251(4, CharName, 19);
		W.U8(4 + FMath::Min(Written, 19), 0);
	}

	// Д6: тело собирает SendSecured (секрет + MD5(Msg,24) + Msg 24б).
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_SIGNED_RARE_ITEM_NOTICE, TEXT("SendRareItemNotice6616"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SEND_SIGNED_RARE_ITEM_NOTICE, W.Data(), W.Num());
	if (bOk)
	{
		// Ушедший в сокет 6616 считаем «неподтверждённым», пока не придёт 6131 (см. шапку в .h).
		++Unacked6616;
		if (Unacked6616 >= kMaxUnacked6616)
		{
			bAnnounce6616Blocked = true;
			UE_LOG(LogAsdaNet, Error,
				TEXT("→6616: %d отправки подряд без ответного 6131 — ДАЛЬШЕ НЕ ШЛЮ. Серверный бюджет ")
				TEXT("неизвестных пакетов CUser+0x1E = 5 (кик на 6-м), из них 2 оставляем каналу 6560. ")
				TEXT("Проверьте на этом процессе GameServer: asdapatch_boot.log (нужен шаг «9 patches-done») ")
				TEXT("и Log\\Log_Patch\\PatchLog_<дата>.txt (нужна строка «[OK] P01_RareItemNotice6616»)."),
				Unacked6616);
		}
	}
	UE_LOG(LogAsdaNet, Log, TEXT("→6616 анонс редкой добычи: '%s' предмет %d -> %s (без ответа подряд: %d/%d)"),
		*CharName, ItemCode, bOk ? TEXT("OK") : TEXT("FAIL"), Unacked6616, kMaxUnacked6616);
	return TrackSend(bOk);
}

// ══════════ ★ ОТПРАВИТЕЛИ КОНТЕКСТНОГО МЕНЮ ПО ИГРОКУ (окно TRANTOOL_WIN 2000) ═════════════════
//   Разбор каждого пакета — в объявлениях (`Public/AsdaNetSubsystem.h`) и в
//   `Public/UI/AsdaPlayerMenuSubsystem.h` §3. Здесь только сборка тела 1:1.
//   Общая форма: [0..7] секрет · [8..23] MD5(payload) · [24..] payload; bPad=true (эталонный a6=1).

// Общий помощник: собрать «секрет + MD5 + payload» и отправить. Ровно та схема, что у 6538/6142 —
//   заводить её копию в каждом отправителе было бы дублем (закон port-shared-points).
bool UAsdaNetSubsystem::SendMenuPacket(uint16 Opcode, const uint8* Payload, int32 PayloadLen, const TCHAR* What)
{
	// ★ Стенд `asda.Net.PacketStand` (волна 3 п.10, `AsdaPacketStand.cpp`): при стоящем перехватчике сокет и
	//   гейты (состояние живой сессии, не тело) пропускаются, а проверка ДЛИНЫ — та же, что у игры, и перехват
	//   стоит ПОСЛЕ неё. В игре перехватчик ПУСТ ⇒ порядок прежний: сокет → гейты → длина.
	const bool bStand = IsStandCapture(Opcode);   // ★ волна 14 (МЕЛК-3): узкий перехват берёт только свой опкод
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		// ★★★ ГЕЙТЫ — ЕДИНЫЙ ШЛЮЗ (E1, 14.09.2026): XyT → одержимость (4108 клан-инвайт, 5282 обмен — у сервера
		//   `sub_52EE40`) → «душа» (4108, 5282/5286 — `sub_52EEA0`). Маска = прежняя цепочка этой точки.
		if (FAsdaOutgoingGate::Block(this, Opcode, EAsdaGate::XyT | EAsdaGate::Possess | EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }
	}
	if (PayloadLen <= 0 || PayloadLen > 512) { return false; }
	// Пад кадра меню = эталонный a6 = 1. Прежний вызов `Conn->SendPacket(Op, Body, N)` брал его умолчанием
	//   (`HdrA = 0, HdrB = 0, bPad = true` в `AsdaConnection.h`); теперь одна константа идёт и в провод, и в
	//   стенд — прежний зашитый `true` перехвата с проводом совпадал, но расходиться им больше негде.
	static constexpr bool bMenuPad = true;
	if (bStand) { const auto Cap = SendCaptureForStand; Cap(Opcode, Payload, PayloadLen, bMenuPad, TEXT('M')); return true; }   // копия: см. SendSecured
	TArray<uint8> Body;
	Body.SetNumZeroed(24 + PayloadLen);
	FMemory::Memcpy(Body.GetData() + 24, Payload, PayloadLen);
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Body.GetData() + 24, PayloadLen); Md5.Final(Dig);
	  FMemory::Memcpy(Body.GetData() + 8, Dig, 16); }
	FillAntiReplaySecret(Body.GetData());
	const bool bOk = ConnSendSecret(Opcode, Body.GetData(), Body.Num(), 0, 0, bMenuPad);
	UE_LOG(LogAsdaNet, Log, TEXT("[меню игрока] %s: опкод %u, тело %d б -> %s"),
		What, (uint32)Opcode, Body.Num(), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendClanInvite4108(int32 TargetUserNum)
{
	if (TargetUserNum < 0) { return false; }
	FAsdaPacketWriter W(2); W.I16(0, TargetUserNum);                  // @0 u16 userNum цели
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CLAN_MEMBER_INVITE_REQUEST_USER, TEXT("SendClanInvite4108"))) { return false; }
	return SendMenuPacket((uint16)OP_CLAN_MEMBER_INVITE_REQUEST_USER, W.Data(), W.Num(), TEXT("клан-инвайт 4108"));
}

bool UAsdaNetSubsystem::SendFriendInvite4150(int32 TargetUserNum)
{
	if (TargetUserNum < 0) { return false; }
	FAsdaPacketWriter W(2); W.I16(0, TargetUserNum);                  // @0 u16 userNum цели
	if (AsdaPacketLayoutBroken(W, (uint16)OP_FRIEND_MEMBER_INVITE_REQ, TEXT("SendFriendInvite4150"))) { return false; }
	return SendMenuPacket((uint16)OP_FRIEND_MEMBER_INVITE_REQ, W.Data(), W.Num(), TEXT("друзья 4150"));
}

bool UAsdaNetSubsystem::SendSoulmateCreate5200(const FString& TargetName, int32 Mode, int32 MyLevel,
	int32 MySex, int32 MyClass, const FString& Message)
{
	if (TargetName.IsEmpty()) { return false; }
	// ★ Волна 1-А (06.10.2026): стенд/перехват исходящих — тем же порядком, что `SendMd5Signed`: при стоящем перехватчике
	//   сокет и гейты сессии пропускаются, тело собирается тем же кодом и уходит в перехватчик (самотест 9100/2002/8266).
	const bool bStand = IsStandCapture((uint16)OP_REQUEST_USER_SOUL_MATE_CREATE);
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		// ★ Гейт «души» (SoulState==1): 5200 у сервера под `sub_52EEA0` (GameServer.c:20004).
		if (FAsdaOutgoingGate::Block(this, (uint16)OP_REQUEST_USER_SOUL_MATE_CREATE, EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }
	}
	// ★ У ЭТОГО пакета 8-байтного анти-реплей секрета НЕТ: `coupleSendProposalReq` кладёт MD5
	//   в [0..15] и payload сразу с [16] (ui_core.c:72796-72800 / AsdaStory_ida.c:684345).
	uint8 Payload[153];
	FMemory::Memzero(Payload, sizeof(Payload));
	Payload[0] = (uint8)FMath::Clamp(Mode, 0, 255);
	FAsdaLoginData::EncodeCp1251(TargetName, &Payload[1], 19);
	const uint16 Lvl = (uint16)FMath::Clamp(MyLevel, 0, 65535);
	FMemory::Memcpy(&Payload[21], &Lvl, 2);
	Payload[23] = (uint8)FMath::Clamp(MySex, 0, 255);
	Payload[24] = (uint8)FMath::Clamp(MyClass, 0, 255);
	FAsdaLoginData::EncodeCp1251(Message, &Payload[25], 127);
	uint8 Body[169];
	FMemory::Memzero(Body, sizeof(Body));
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Payload, sizeof(Payload)); Md5.Final(Dig);
	  FMemory::Memcpy(Body, Dig, 16); }
	FMemory::Memcpy(Body + 16, Payload, sizeof(Payload));
	if (bStand)
	{
		// Копия перехватчика (он вправе снять себя изнутри); в перехватчик — payload 153 б, как у `SendMd5Signed` ('5').
		const auto Cap = SendCaptureForStand;
		Cap((uint16)OP_REQUEST_USER_SOUL_MATE_CREATE, Payload, (int32)sizeof(Payload), false, TEXT('5'));
		UE_LOG(LogAsdaNet, Log, TEXT("[меню игрока] соулмейт 5200 «%s» (режим=%d) -> ПЕРЕХВАТ"), *TargetName, Mode);
		return true;
	}
	const bool bOk = Conn->SendPacket((uint16)OP_REQUEST_USER_SOUL_MATE_CREATE, Body, sizeof(Body));
	UE_LOG(LogAsdaNet, Log, TEXT("[меню игрока] соулмейт 5200 «%s» (режим=%d ур=%d пол=%d класс=%d) -> %s"),
		*TargetName, Mode, MyLevel, MySex, MyClass, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

// ═══════════════════ СОУЛМЕЙТ: ядро протокола (отчёт soulmate_6400.md §5.3) ═══════════════════════

bool UAsdaNetSubsystem::SendSoulmateResponse5202(bool bAccept, int32 InitCharId, int32 InitUserNum,
	uint8 B7, uint8 B8, uint8 B9, uint8 Channel, const FString& InitName, uint8 SkillType)
{
	// Конверт A (MD5 без токена), payload 32, size 48 — `soulmateBond_Init@0xE66970` + ветки 8408/8409.
	// ★ Байт [0]: 1=принять (кнопка 8408 loc20276), 0=отказать (8409 loc20277). Снято с ПОДПИСЕЙ кнопок,
	//   т.к. имена функций Soulmate_SendBondPropose/Accept в декомпиле перепутаны (D2). Сервер
	//   `ResponseUser_SoulMateCreate@0x492AE0` требует InitCharId>=0 && InitUserNum>=0.
	FAsdaPacketWriter W(32);
	W.U8(0, bAccept ? 1 : 0);
	W.I32(1, InitCharId);   // charId инициатора (i32)
	W.I16(5, InitUserNum);  // userNum инициатора (i16) — НЕ путать с charId!
	W.U8(7, B7);
	W.U8(8, B8);
	W.U8(9, B9);
	W.U8(10, bAccept ? Channel : 0);         // канал заполняется ТОЛЬКО в ветке «принять»
	W.Cp1251(11, InitName, 19);   // имя[20], NUL-терминируется в пределах 20
	W.U8(31, SkillType);                     // iSoulMateSkillType (1|2)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RESPONSE_USER_SOUL_MATE_CREATE, TEXT("SendSoulmateResponse5202"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_RESPONSE_USER_SOUL_MATE_CREATE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] →5202 %s: initCharId=%d initUserNum=%d канал=%u '%s' -> %s"),
		bAccept ? TEXT("ПРИНЯТЬ") : TEXT("ОТКАЗАТЬ"), InitCharId, InitUserNum, (uint32)W[10], *InitName,
		bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSoulmateBreakReq5204(int32 SlotIndex)
{
	// Конверт A, payload 5, size 21 — `Soulmate_SendBondRequest@0xE66F70`. [0]=0, [1..4] i32 слот.
	FAsdaPacketWriter W(5);
	W.U8(0, 0);
	W.I32(1, SlotIndex);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_REQUEST_USER_SOUL_MATE_DESTORY, TEXT("SendSoulmateBreakReq5204"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_REQUEST_USER_SOUL_MATE_DESTORY, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] →5204 запрос разрыва: слот=%d -> %s"),
		SlotIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

// ★ Волна 1-А (ревизия §7, 06.10.2026). [ОРИГИНАЛ] `UI_resetAllWindowsAndLeave_5045@0xD995C0` (AsdaStory_game.c:395929-395940):
//   `*(WORD*)v8 = +0x4A0 (npcId); Net_Md5Digest16(v8, 2); memcpy(v4, md5, 16); memcpy(v4+16, v8, 2); CNetwork_Send(…, 18, 5045)`.
//   [ПРОТОКОЛ] Конверт A (MD5 без секрета), payload 2, size 18 — сервер `sub_45C260` читает u16 с +25 (`sub_523F00(a3, 2)`).
bool UAsdaNetSubsystem::SendNpcFirstMeet5045(int32 NpcId)
{
	FAsdaPacketWriter W(2);
	W.I16(0, NpcId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_UI_RESET_ALL_WINDOWS_AND_LEAVE_5045, TEXT("SendNpcFirstMeet5045"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_UI_RESET_ALL_WINDOWS_AND_LEAVE_5045, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[NPC] →5045 первая встреча окончена: npcId=%d -> %s"), NpcId, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSoulmateBreakResp5206(bool bAccept, int32 PartnerCharId, int32 PartnerUserNum,
	uint8 B7, uint8 B8, uint8 Channel, const FString& PartnerName)
{
	// Конверт A, payload 31, size 47 — `soulmateWnd_Init@0xE67510`. Раскладка как у 5202 без [31];
	//   ★ [9] инициализируется в −1 (0xFF), а не 0 (отчёт §5.3).
	FAsdaPacketWriter W(31);
	W.U8(0, bAccept ? 1 : 0);
	W.I32(1, PartnerCharId);
	W.I16(5, PartnerUserNum);
	W.U8(7, B7);
	W.U8(8, B8);
	W.U8(9, 0xFF);                            // init −1 (единственное отличие от 5202)
	W.U8(10, bAccept ? Channel : 0);
	W.Cp1251(11, PartnerName, 19);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RESPONSE_USER_SOUL_MATE_DESTORY, TEXT("SendSoulmateBreakResp5206"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_RESPONSE_USER_SOUL_MATE_DESTORY, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] →5206 %s разрыв: partnerCharId=%d partnerUserNum=%d '%s' -> %s"),
		bAccept ? TEXT("СОГЛАСИТЬСЯ") : TEXT("ОТКАЗ"), PartnerCharId, PartnerUserNum, *PartnerName,
		bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSoulmateModes5221(uint8 Main, uint8 Mode1, uint8 Mode2, uint8 Mode3)
{
	// Конверт A, payload 4, size 20. Шлётся весь блок целиком (галки 6543–6546); enable/disable —
	//   ОДИН опкод и ОДИН формат, сервер различает по содержимому (D3).
	FAsdaPacketWriter W(4);
	W.U8(0, Main).U8(1, Mode1).U8(2, Mode2).U8(3, Mode3);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SOULMATE_SEND_BUFF_DISABLE, TEXT("SendSoulmateModes5221"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_SOULMATE_SEND_BUFF_DISABLE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] →5221 режимы: %u/%u/%u/%u -> %s"),
		(uint32)Main, (uint32)Mode1, (uint32)Mode2, (uint32)Mode3, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSoulmateSearch5483(uint8 Page, uint8 Filter)
{
	// Конверт B (с секретом), payload 2, size 26 — `netSendMaskedPacket_op5483@0xE737D0`.
	// ★★★ ПОРЯДОК ИСПРАВЛЕН 31.08.2026 (аудит): эталон кладёт `[0] = *(self+496)` = ФИЛЬТР,
	//   `[1] = *(self+488)` = СТРАНИЦА. Раньше порт слал их местами ⇒ сервер понимал запрос наоборот.
	//   Имена параметров оставлены по смыслу — меняется только порядок в проводе.
	FAsdaPacketWriter W(2);
	W.U8(0, Filter).U8(1, Page);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_SOUL_SEARCH, TEXT("SendSoulmateSearch5483"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SEND_SOUL_SEARCH, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] →5483 поиск: страница=%u фильтр=%u -> %s"),
		(uint32)Page, (uint32)Filter, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

// 2244 — запрос данных о ПРЕДЫДУЩЕМ соулмейте (кнопка 6584). Тела нет: сервер берёт всё из CUser.
bool UAsdaNetSubsystem::SendSoulmateOldPartnerReq2244()
{
	const bool bOk = SendSecured((uint16)OP_ANTI_CHEAT_SEND_BIT_MASK_CHECK, nullptr, 0);
	UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] →2244 запрос о прошлом Друге (тела нет, 24 б) -> %s"),
		bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

// ★★★ 26.10.2026 (агент smmessage): первый аргумент — charId, который уходит в @0. Имя параметра в объявлении (`PartnerId`) —
//   историческая ОШИБКА порта: эталон `netSendChatGuildMsg@0xE6B070` (network.c:29651) кладёт в @0 `*(g_pLocalSessionCtx + 1052)` =
//   СВОЙ charId, а сервер `Soulmate_SendNoteReq@0x4992E0` (GameServer.c:61333) сверяет его с `CUser+372` (свой charId) и при
//   несовпадении МОЛЧА выбрасывает пакет (ни записи в БД, ни ответа 6053). Вызывающий (`UAsdaSoulmateSubsystem::SendNote`)
//   передаёт свой charId; ключ отката `asda.Soulmate.NoteSelfCharId 0` — прежний порт (charId Друга). Заголовок не трогали
//   (горячий: 167 включений) — расхождение имени параметра там помечено здесь.
bool UAsdaNetSubsystem::SendSoulmateNote6052(int32 PartnerId, const FString& Text)
{
	// Конверт B, payload 69, size 93 — `netSendChatGuildMsg@0xE6B070` (миснейм). @0 i32 charId (СВОЙ — см. выше),
	//   @4 char[65] текст (64 + NUL). Гейт кнопки 6603: длина строго 1..64.
	FString Clean = Text.TrimStartAndEnd();
	// [ПОРТ] С4 08.10.2026: гейт — в байтах провода (арабский: маркер + cp1256, смешанный: UTF-8), а не в символах.
	if (Clean.IsEmpty() || FAsdaLoginData::WireTextByteLen(Clean) > 64) { return false; }
	FAsdaPacketWriter W(69);
	W.I32(0, PartnerId);
	W.Cp1251(4, Clean, 64);   // 64 символа + NUL в пределах 65
	// ★ Защита от SQL-инъекции ОРИГИНАЛА: апостроф → обратная кавычка (bufferReplaceByte). Сохраняем.
	W.ReplaceByte(4, 65, '\'', '`');
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SOULMATE_SEND_NOTE_REQ, TEXT("SendSoulmateNote6052"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SOULMATE_SEND_NOTE_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] →6052 заметка charId=%d длина=%d -> %s"),
		PartnerId, Clean.Len(), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendSoulmateNoteConfirm6054(int32 NoteId)
{
	// Конверт B — подтверждение прочтения записки (`antiCheat_SendHeartbeat6054@0xE6B740`, МИСНЕЙМ; security.c:1994):
	//   payload 4 = @0 i32 номер записки (запись очереди @4). ★ 26.10.2026 разобрано сервером: `Soulmate_ReceiveNoteConfirm@0x4997D0`
	//   (`sub_523F00(a3, 4, …, 1)` — payload 4, с секретом) берёт i32 с +33 и шлёт в DBManager 2240 → `UPSoulMateNoteDel номер, свой charId`.
	FAsdaPacketWriter W(4);
	W.I32(0, NoteId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SOULMATE_RECEIVE_NOTE_CONFIRM, TEXT("SendSoulmateNoteConfirm6054"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_SOULMATE_RECEIVE_NOTE_CONFIRM, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[соулмейт] →6054 подтверждение заметки id=%d -> %s"),
		NoteId, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendExchangeReq5282(int32 TargetUserNum, bool bPersonalItems)
{
	if (TargetUserNum < 0) { return false; }
	FAsdaPacketWriter W(3);
	W.I16(0, TargetUserNum);                         // @0 u16 userNum цели
	W.U8(2, bPersonalItems ? 1 : 0);   // ★ ЕДИНСТВЕННОЕ отличие 2004 от 2005
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_EXCHANGE_REQ, TEXT("SendExchangeReq5282"))) { return false; }
	return SendMenuPacket((uint16)OP_CHAR_EXCHANGE_REQ, W.Data(), W.Num(),
		bPersonalItems ? TEXT("обмен личными 5282(1)") : TEXT("обмен предметами 5282(0)"));
}

bool UAsdaNetSubsystem::SendExchangeCancel5286(int32 TargetUserNum)
{
	FAsdaPacketWriter W(6);
	W.I32(0, -2);                       // эталонный msgBody[0] = −2
	// ★ 29.09.2026 (волна О1): @4 — `LOWORD(+0x2708)` как есть. Прежний кламп `Max(userNum, 0)` при неизвестной цели (−1)
	//   слал userNum 0: сервер (`CharExchanageCancelReq` −2, GameServer.c:88430-88445) снял бы флаг обмена `+0x145` у
	//   ЧУЖОГО игрока №0. Эталон шлёт 0xFFFF — сервер пишет «lpsDestUserArrayInfo == NULL» и ничего не трогает.
	W.I16(4, (int16)TargetUserNum);     // @4 i16 userNum цели
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_EXCHANAGE_CANCEL_REQ, TEXT("SendExchangeCancel5286"))) { return false; }
	return SendMenuPacket((uint16)OP_CHAR_EXCHANAGE_CANCEL_REQ, W.Data(), W.Num(), TEXT("отмена обмена 5286"));
}

bool UAsdaNetSubsystem::SendExchangeAnswer5284(uint8 SubCode, int32 InitiatorUserNum, bool bPersonalItems)
{
	// `CExchange_SendWindowReady/SendWindowCancel/SendRequestAck` (shop_trade.c:8222-8640): отправители сами проверяют
	//   `userNum ≥ 0`; здесь — только тело 1:1.
	if (InitiatorUserNum < 0) { return false; }
	FAsdaPacketWriter W(4);
	W.U8(0, SubCode);                        // @0 подкод: 1 принять / 0 отказ / 3 «занят»
	W.I16(1, (int16)InitiatorUserNum);       // @1 i16 userNum инициатора (`+0x3E` / @1 заявки 5283)
	W.U8(3, bPersonalItems ? 1 : 0);         // @3 `exchangeType != 0`
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_EXCHANGE_ACCEPT_REQ, TEXT("SendExchangeAnswer5284"))) { return false; }
	return SendMenuPacket((uint16)OP_CHAR_EXCHANGE_ACCEPT_REQ, W.Data(), W.Num(),
		SubCode == 1 ? TEXT("обмен: принять 5284(1)") : (SubCode == 3 ? TEXT("обмен: занят 5284(3)") : TEXT("обмен: отказ 5284(0)")));
}

bool UAsdaNetSubsystem::SendExchangeClose5286(int32 WindowIndex)
{
	// `CExchange_SendClose` (shop_trade.c:10240-10366): `memset(body, 0, 6); *(DWORD*)body = windowIndex`.
	FAsdaPacketWriter W(6);
	W.I32(0, WindowIndex);              // @0 i32 windowIndex из 5285
	W.I16(4, 0);                        // @4 — нули (userNum нужен только ветке −2)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_EXCHANAGE_CANCEL_REQ, TEXT("SendExchangeClose5286"))) { return false; }
	return SendMenuPacket((uint16)OP_CHAR_EXCHANAGE_CANCEL_REQ, W.Data(), W.Num(), TEXT("обмен: закрыть окно 5286"));
}

bool UAsdaNetSubsystem::SendExchangeAddItem5288(int32 WindowIndex, int32 TraySlot, int32 InvType, int32 InvSlot, int32 Count)
{
	// `Exchange_SendAddItem` (shop_trade.c:8679-8683): `pkt[0] = windowIndex; LOBYTE(pkt[1]) = activeSlotIndex;
	//   BYTE1(pkt[1]) = лоток[+4]; HIWORD(pkt[1]) = лоток[+5]; pkt[2] = кол-во` — 12 байт, все четыре отправителя одинаковы.
	FAsdaPacketWriter W(12);
	W.I32(0, WindowIndex);              // @0 i32 windowIndex из 5285
	W.U8(4, (uint8)TraySlot);           // @4 u8 ячейка лотка 0..4 (`activeSlotIndex`)
	W.U8(5, (uint8)InvType);            // @5 u8 контейнер записи (+4)
	W.I16(6, (int16)InvSlot);           // @6 i16 ячейка контейнера (+5)
	W.I32(8, Count);                    // @8 i32 кол-во (+9 записи лотка или выбранное нумпадом)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_EXCHANGE_DRAG_DROP_ITEM, TEXT("SendExchangeAddItem5288"))) { return false; }
	return SendMenuPacket((uint16)OP_CHAR_EXCHANGE_DRAG_DROP_ITEM, W.Data(), W.Num(), TEXT("обмен: в лоток 5288"));
}

bool UAsdaNetSubsystem::SendExchangeRemoveItem5290(int32 WindowIndex, int32 InvType, int32 InvSlot, int32 Count)
{
	// `Exchange_SendRemoveItem` (shop_trade.c:9250-9254): `payload[0] = windowIndex; LOBYTE(payload[1]) = лоток[+4];
	//   *(WORD*)(&payload[1] + 1) = лоток[+5]; *(DWORD*)(&payload[1] + 3) = лоток[+9]` — 11 байт (@4 — invType, не ячейка лотка).
	FAsdaPacketWriter W(11);
	W.I32(0, WindowIndex);              // @0 i32 windowIndex
	W.U8(4, (uint8)InvType);            // @4 u8 контейнер записи (+4) — сервер сравнивает его с +665 окна
	W.I16(5, (int16)InvSlot);           // @5 i16 ячейка контейнера (+5)
	W.I32(7, Count);                    // @7 i32 кол-во (+9)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_EXCHANGE_DRAG_DROP_DELETE_ITEM, TEXT("SendExchangeRemoveItem5290"))) { return false; }
	return SendMenuPacket((uint16)OP_CHAR_EXCHANGE_DRAG_DROP_DELETE_ITEM, W.Data(), W.Num(), TEXT("обмен: с лотка 5290"));
}

bool UAsdaNetSubsystem::SendExchangeFix5291(int32 WindowIndex, uint8 Lock, const int32 InvType[5], const int32 InvSlot[5], const int32 Count[5])
{
	// `Exchange_SendLockToggle` (shop_trade.c:9807-9828): `packet[0] = windowIndex; LOBYTE(packet[1]) = +0x4C4;` затем для k = 0…4
	//   `*(DWORD*)((char*)&packet[2] + 7k) = кол-во (+9); *((BYTE*)&packet[1] + 7k + 1) = invType (+4);
	//   *(WORD*)((char*)&packet[1] + 7k + 2) = invSlot (+5)` — 40 байт (`Net_Md5Digest16(…, packet, 0x28)`, CNetwork_Send len 64).
	if (!InvType || !InvSlot || !Count) { return false; }
	FAsdaPacketWriter W(40);
	W.I32(0, WindowIndex);              // @0 i32 windowIndex из 5285
	W.U8(4, Lock);                      // @4 u8 lock (+0x4C4): 1 взвести / 0 снять
	for (int32 k = 0; k < 5; ++k)
	{
		const int32 O = 5 + 7 * k;      // запись k: сервер читает `v26[7k+5]` (контейнер) и `v26[7k+8]` (кол-во), GameServer.c:89160-89164
		W.U8(O, (uint8)InvType[k]);     // +0 u8 контейнер записи лотка (+4)
		W.I16(O + 1, (int16)InvSlot[k]);// +1 i16 ячейка контейнера (+5)
		W.I32(O + 3, Count[k]);         // +3 i32 кол-во (+9)
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_EXCHANGE_FIX_REQ, TEXT("SendExchangeFix5291"))) { return false; }
	return SendMenuPacket((uint16)OP_CHAR_EXCHANGE_FIX_REQ, W.Data(), W.Num(), Lock ? TEXT("обмен: готовность 5291(1)") : TEXT("обмен: снять готовность 5291(0)"));
}

bool UAsdaNetSubsystem::SendExchangeComplete5293(int32 WindowIndex)
{
	// `Exchange_SendComplete` (shop_trade.c:10111-10113): `memset(&tradeId, 0, 4); tradeId = windowIndex` — 4 байта, len 28.
	FAsdaPacketWriter W(4);
	W.I32(0, WindowIndex);              // @0 i32 windowIndex из 5285
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_EXCHANGE_COMPLETE_REQ, TEXT("SendExchangeComplete5293"))) { return false; }
	return SendMenuPacket((uint16)OP_CHAR_EXCHANGE_COMPLETE_REQ, W.Data(), W.Num(), TEXT("обмен: итог 5293"));
}

bool UAsdaNetSubsystem::SendTargetCharInfo5478(int32 TargetUserNum, int32 TargetCharId, int32 TargetRaceByte)
{
	if (TargetUserNum < 0) { return false; }
	FAsdaPacketWriter W(7);
	W.I16(0, TargetUserNum);                           // @0 u16 userNum цели
	W.I32(2, TargetCharId);
	W.U8(6, (uint8)FMath::Clamp(TargetRaceByte, 0, 255));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_TARGET_CHAR_INFORMATION, TEXT("SendTargetCharInfo5478"))) { return false; }
	return SendMenuPacket((uint16)OP_SEND_TARGET_CHAR_INFORMATION, W.Data(), W.Num(), TEXT("инфо о персонаже 5478"));
}

bool UAsdaNetSubsystem::SendDuelReq4302(int32 MyUserNum, int32 MyCharId, const FString& MyName,
	int32 TargetUserNum, int32 TargetCharId)
{
	if (TargetUserNum < 0 || MyUserNum < 0) { return false; }
	FAsdaPacketWriter W(34);
	W.I16(0, MyUserNum);                               // @0 u16 свой userNum
	W.I32(2, MyCharId);
	W.Cp1251(6, MyName, 19);
	W.I16(26, TargetUserNum);                          // @26 u16 userNum цели
	W.I32(28, TargetCharId);
	W.I16(32, -1);                      // эталонный LOWORD(pktBody[8]) = −1
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_PVP_REQ, TEXT("SendDuelReq4302"))) { return false; }
	ASDA_CTRACE(EAsdaCT::Pvp, TEXT("ОТПР 4302 вызов на поединок: я userNum=%d charId=%d «%s» → цель userNum=%d charId=%d"),
		MyUserNum, MyCharId, *MyName, TargetUserNum, TargetCharId);
	return SendMenuPacket((uint16)OP_RECV_USER_PVP_REQ, W.Data(), W.Num(), TEXT("поединок 4302"));
}

// ★ 06.10.2026 (волна 2-В): ответ на вызов — `duelSendReqPkt4303@0xDE4AB0` (combat_pvp.c:6243-6249, 6352):
//   pktBody[0] = answerFlag; [1] = WORD pThis[71] (userNum вызвавшего); [3] = DWORD pThis+132 (его charId);
//   memset([7], 0, 20); [27] = WORD pThis[68] (pvpIndex). Конверт 57 = тот же «MD5 + счётчик», что у 4302.
bool UAsdaNetSubsystem::SendDuelAnswer4303(uint8 Answer, int32 ChallengerUserNum, int32 ChallengerCharId, int32 PvpIndex)
{
	FAsdaPacketWriter W(33);
	W.U8(0, Answer);                                   // @0 u8 1 = принять, 0 = отказ
	W.I16(1, ChallengerUserNum);                       // @1 u16 userNum вызвавшего (из 4302 @0)
	W.I32(3, ChallengerCharId);                        // @3 i32 charId вызвавшего (из 4302 @2)
	// @7 char[20] — нули: эталон `memset(&pktBody[7], 0, 0x14)`, конструктор писателя тело уже обнулил.
	W.I16(27, PvpIndex);                               // @27 i16 pvpIndex (из 4302 @32)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_PVP_RES, TEXT("SendDuelAnswer4303"))) { return false; }
	ASDA_CTRACE(EAsdaCT::Pvp, TEXT("ОТПР 4303 ответ на поединок: %s, вызвавший userNum=%d charId=%d pvpIndex=%d"),
		Answer == 1 ? TEXT("ПРИНЯТЬ") : TEXT("отказ"), ChallengerUserNum, ChallengerCharId, PvpIndex);
	return SendMenuPacket((uint16)OP_RECV_USER_PVP_RES, W.Data(), W.Num(), TEXT("ответ на поединок 4303"));
}

bool UAsdaNetSubsystem::SendGetUserId6082(const FString& TargetName)
{
	if (TargetName.IsEmpty()) { return false; }
	FAsdaPacketWriter W(20);
	W.Cp1251(0, TargetName, 19);                       // char[20]: 19 байт + ноль заполнения
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_GET_USER_ID, TEXT("SendGetUserId6082"))) { return false; }
	return SendMenuPacket((uint16)OP_RECV_GET_USER_ID, W.Data(), W.Num(), TEXT("блог/витрина 6082"));
}

// ══════════ ★ ОТРЯД: 5090 / 5092 / 5094 / 5096 (окна PARTYTAKE_WIN 2250, PARTYBOX_WIN 2245) ════
//   Разбор — в объявлениях (`Public/AsdaNetSubsystem.h`) и в `Public/UI/AsdaPartySubsystem.h`.
//   ⚠ Конверт ДРУГОЙ, чем у меню игрока: MD5 в [0..15], payload с [16] — секрета НЕТ.

// Второй конверт оригинала: MD5(payload) + payload. См. объявление — там доказательство
//   двумя сторонами (клиентский сборщик и серверная сверка `sub_523F00`, ветка `a4 == 0`).
bool UAsdaNetSubsystem::SendMd5Signed(uint16 Opcode, const uint8* Payload, int32 Len, bool bPad)
{
	// ★ Стенд `asda.Net.PacketStand` (волна 3 п.10, `AsdaPacketStand.cpp`): при стоящем перехватчике сокет и
	//   гейты (состояние живой сессии, не тело) пропускаются, а проверка длины и указателя — та же, что у игры,
	//   и перехват стоит ПОСЛЕ неё. Пад — параметр `bPad` (реальный, как в провод). В игре перехватчик ПУСТ ⇒
	//   порядок прежний: сокет → гейты → длина.
	const bool bStand = IsStandCapture(Opcode);   // ★ волна 14 (МЕЛК-3): узкий перехват берёт только свой опкод
	if (!bStand)
	{
		if (!Conn || !Conn->IsConnected()) { return false; }
		// ★★★ ГЕЙТЫ — ЕДИНЫЙ ШЛЮЗ (E1, 14.09.2026): XyT → одержимость (5204 разрыв бонда — `sub_52EE40`) →
		//   «душа» (отряд 5090…5100, соулмейт 5202/5204/5206/5221 — `sub_52EEA0`). Маска = прежняя цепочка этой точки.
		if (FAsdaOutgoingGate::Block(this, Opcode, EAsdaGate::XyT | EAsdaGate::Possess | EAsdaGate::Soul | EAsdaGate::TransferWide)) { return false; }
	}
	if (Len <= 0 || Len > 2048 || !Payload) { return false; }
	if (bStand) { const auto Cap = SendCaptureForStand; Cap(Opcode, Payload, Len, bPad, TEXT('5')); return true; }   // копия: см. SendSecured
	TArray<uint8> Body;
	Body.SetNumZeroed(16 + Len);
	FMemory::Memcpy(Body.GetData() + 16, Payload, Len);
	{ FMD5 Md5; uint8 Dig[16]; Md5.Update(Body.GetData() + 16, Len); Md5.Final(Dig);
	  FMemory::Memcpy(Body.GetData(), Dig, 16); }
	return Conn->SendPacket(Opcode, Body.GetData(), Body.Num(), 0, 0, bPad);
}

bool UAsdaNetSubsystem::SendPartyNewReq5090(int32 TargetUserNum, uint8 LootRule)
{
	if (TargetUserNum < 0) { return false; }
	// payload(3): @0 u16 userNum цели, @2 u8 правило дележа. Сервер `sub_44F4C0` читает ровно это:
	//   `memcpy(v8, pkt+25, 3)`, затем `sub_5324A0(userMng, v8[0])` — выборка CUser по userNum.
	FAsdaPacketWriter W(3);
	W.I16(0, TargetUserNum);
	W.U8(2, (LootRule != 0) ? 1 : 0);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PARTY_SEND_NEW_PARTY_REQ, TEXT("SendPartyNewReq5090"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_PARTY_SEND_NEW_PARTY_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[отряд] →5090 создать отряд: цель userNum=%d правило=%u (19 б) -> %s"),
		TargetUserNum, (uint32)W[2], bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyJoinReq5094(int32 TargetUserNum)
{
	if (TargetUserNum < 0) { return false; }
	FAsdaPacketWriter W(2);
	W.I16(0, TargetUserNum);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PARTY_SEND_JOIN_REQ, TEXT("SendPartyJoinReq5094"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_PARTY_SEND_JOIN_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[отряд] →5094 позвать в мой отряд: цель userNum=%d (18 б) -> %s"),
		TargetUserNum, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyNewAccept5092(uint8 Code, int32 PartyIndex)
{
	// Эталон `Party_Send_NewPartyAccept(a1 = partyIndex, a2 = код)`: msg[0] = код, msg[1..2] = idx.
	FAsdaPacketWriter W(3);
	W.U8(0, Code);
	W.I16(1, PartyIndex);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RESPONSE_USER_NEW_PARTY_CREATE, TEXT("SendPartyNewAccept5092"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_RESPONSE_USER_NEW_PARTY_CREATE, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[отряд] →5092 ответ на создание: код=%u partyIndex=%d (19 б) -> %s"),
		(uint32)Code, PartyIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyInviteAccept5096(uint8 Code, int32 PartyIndex)
{
	// Эталон `Party_Send_InviteAccept(a1 = код, a2 = partyIndex)` — порядок аргументов ОБРАТНЫЙ
	//   по сравнению с 5092, а РАСКЛАДКА ТЕЛА та же: payload[0] = код, payload[1..2] = idx.
	FAsdaPacketWriter W(3);
	W.U8(0, Code);
	W.I16(1, PartyIndex);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PARTY_SEND_INVITE_ACCEPT, TEXT("SendPartyInviteAccept5096"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_PARTY_SEND_INVITE_ACCEPT, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[отряд] →5096 ответ на вступление: код=%u partyIndex=%d (19 б) -> %s"),
		(uint32)Code, PartyIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyLeave5098(int32 PartyIndex, uint8 Mode, const FString& Name)
{
	// payload(23): @0 i16 partyIndex, @2 u8 режим, @3 char[20] имя. Эталон
	//   `Party_Send_InviteByName(a1 = partyIndex, a2 = режим, Source = имя)`, len 39.
	FAsdaPacketWriter W(23);
	W.I16(0, PartyIndex);
	W.U8(2, Mode);
	W.Cp1251(3, Name, 19);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PARTY_SEND_INVITE_BY_NAME, TEXT("SendPartyLeave5098"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_PARTY_SEND_INVITE_BY_NAME, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[отряд] →5098 выход из отряда: partyIndex=%d режим=%u «%s» (39 б) -> %s"),
		PartyIndex, (uint32)Mode, *Name, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyBanish5100(int32 TargetCharId, int32 PartyIndex, const FString& Name)
{
	// payload(26): @0 i32 charId, @4 i16 partyIndex, @6 char[20] имя. Эталон
	//   `Party_Send_BanishByName(a1 = charId, a2 = partyIndex, Source = имя)`, len 42.
	//   ⚠ Здесь идёт именно charId (i32), а не userNum — единственный пакет семьи, где так.
	FAsdaPacketWriter W(26);
	W.I32(0, TargetCharId);
	W.I16(4, PartyIndex);
	W.Cp1251(6, Name, 19);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_PARTY_SEND_BANISH_BY_NAME, TEXT("SendPartyBanish5100"))) { return false; }
	const bool bOk = SendMd5Signed((uint16)OP_PARTY_SEND_BANISH_BY_NAME, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("[отряд] →5100 исключить: charId=%d partyIndex=%d «%s» (42 б) -> %s"),
		TargetCharId, PartyIndex, *Name, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

// ═════════════════════ ПРЕМИУМ-ГАЧА: отправка ═════════════════════
// Разбор: Reports/gacha_net_re.md. Это ДРУГАЯ система, не бустер выше (см. заголовок).

// ★★★ 6080 C→S — СОХРАНИТЬ ТЕКСТ «О СЕБЕ» (`netSecure_sendTextInput@0xE74220`, `network.c:30416`).
//   Кадр 152 б = 8 (обфусцированный таймштамп) + 16 (MD5) + 128 (текст) — ровно то, что делает
//   `SendSecured`. Санитизация ЭТАЛОНА (network.c:30443-30448), повторяем 1:1:
//     • апостроф `'` → обратный апостроф `` ` `` (защита от SQL-инъекции на стороне community-сервера);
//     • обрезка до 128 БАЙТ; поле 128 б, ноль НЕ обязателен (128 байт текста идут без нуля, как у эталона: `Str[textLen] = 0`
//       ставится в исходной строке, а в поле пакета копируется ровно textLen ≤ 128 байт). ★ 08.10.2026 (пакет 2, МЕЛК-4):
//       прежние «127 + ноль» срезали 128-й символ; сервер 128 допускает (разбор у `UAsdaSoulmateSubsystem::SendIntroText`).
//   Сервер (`Recv_SoulmateIntroduction`, GameServer.c:61150) читает текст с +33 кадра (= тело +24),
//   проверяет MD5 по 0x80 байт и пересылает community-серверу опкодом 2262 (136 б); ответ приходит
//   обратно тем же 6080 (128 б) через `charLogonCommunityRes`.
bool UAsdaNetSubsystem::SendSoulmateIntroduction6080(const FString& Text)
{
	FString Sanitized = Text;
	Sanitized.ReplaceInline(TEXT("'"), TEXT("`"));   // эталон: bufferReplaceByte(Str, len, "'", "`")
	FAsdaPacketWriter W(128);
	W.Cp1251(0, Sanitized, 128);  // ★ 08.10.2026 (пакет 2): до 128 б, как эталон; короче — хвост поля нулевой (тело с нуля)
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_SOULMATE_INTRODUCTION, TEXT("SendSoulmateIntroduction6080"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_SOULMATE_INTRODUCTION, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6080 «о себе»: «%s» (152 б) -> %s"),
		*Sanitized, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════ ★★★ ПОИСК ОТРЯДА (окно 8500): ВОСЕМЬ ОТПРАВИТЕЛЕЙ ════════════════════════════════════
//   Раскладки сняты с самих эталонных сборщиков (`AsdaStory_game.c:408469…409752`), привязка к
//   опкодам — по фактическому аргументу `CNetwork_Send`, а НЕ по именам функций: имена в
//   декомпиле врут (`Anticheat_sendTsMaskReport`, `netChar_SendChecksummedStatePacket` — обе про
//   поиск отряда). Подробности и доказательства — в шапке объявлений (`AsdaNetSubsystem.h`).
namespace
{
	// Общая «шапка» тела всех восьми пакетов: userNum i16 @0, charId i32 @2.
	//   ⚠ Именно это сравнивает сервер (`Recv_Client_UpdateMember`: `*(_DWORD*)&v7[1] ==
	//     v6->charId`), и при расхождении МОЛЧА отвечает ошибкой (закон `charid-vs-usernum`).
	FORCEINLINE void PfWriteWho(FAsdaPacketWriter& W, int32 UserNum, int32 CharId)
	{
		W.I16(0, UserNum);   // младшие 16 бит = прежнее (uint16)(int16)
		W.I32(2, CharId);
	}
	// Пустой номер записи в списке = 0xFFFF (эталон `0xDB9D4B: mov word [eax+0x1634], 0xFFFF`).
	FORCEINLINE uint16 PfRegId(int32 V) { return (V < 0) ? 0xFFFFu : (uint16)V; }
}

// Профессия/класс/уровень для полей +6/+7/+8. Держим в одном месте: все четыре «длинных»
//   отправителя (3410/3416/3418/3424) берут ровно эти три байта из одного и того же места.
void UAsdaNetSubsystem::PartyFindSelfTriplet(uint8& OutJob, uint8& OutClass, uint8& OutLevel) const
{
	OutJob   = (uint8)FMath::Clamp(SelfJob,     0, 255);   // `g_LocalPlayer+1093` (0x445)
	OutClass = (uint8)FMath::Clamp(SelfClassId, 0, 255);   // `g_LocalPlayer+1080` (0x438)
	OutLevel = SelfLevel;                                   // `g_LocalPlayer+1082` (0x43A)
}

bool UAsdaNetSubsystem::SendPartyFindAddMember3410(const FString& SelfCharName)
{
	FAsdaPacketWriter W(32);
	PfWriteWho(W, (int32)SelfUserNum, SelectedCharId);
	uint8 Job = 0, Cls = 0, Lv = 0;
	PartyFindSelfTriplet(Job, Cls, Lv);
	W.U8(6, Job); W.U8(7, Cls); W.U8(8, Lv);
	W.U8(9, (uint8)(CurrentChannel + 1));                 // ★ канал ОДНОБАЗОВЫЙ
	// [10..11] — паддинг: эталон его не пишет вовсе (буфер обнулён `sub_933BDA`).
	W.Cp1251(12, SelfCharName, 19);         // char[20] с гарантированным нулём
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_ADD_MEMBER, TEXT("SendPartyFindAddMember3410"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_ADD_MEMBER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→3410 поиск отряда, встать в список одиночек: userNum=%d charId=%d проф=%d класс=%d ")
		TEXT("уровень=%d канал=%d имя='%s' (56 б) -> %s"),
		(int32)SelfUserNum, SelectedCharId, Job, Cls, Lv, CurrentChannel + 1, *SelfCharName,
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyFindSubMember3412(int32 MemberRegId)
{
	FAsdaPacketWriter W(8);
	PfWriteWho(W, (int32)SelfUserNum, SelectedCharId);
	W.I16(6, (int32)PfRegId(MemberRegId));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_SUB_MEMBER, TEXT("SendPartyFindSubMember3412"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_SUB_MEMBER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→3412 поиск отряда, уйти из списка одиночек: мойНомер=%d (32 б) -> %s"),
		MemberRegId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyFindGetMember3414(int32 Page, int32 MemberRegId,
	uint32 MapMaskA, uint32 MapMaskB, int32 JobFilter, int32 LevelMin, int32 LevelMax)
{
	FAsdaPacketWriter W(21);
	PfWriteWho(W, (int32)SelfUserNum, SelectedCharId);
	W.I16(6, (int32)(uint16)FMath::Max(1, Page));
	W.I16(8, (int32)PfRegId(MemberRegId));
	W.I32(10, (int32)MapMaskA);
	W.I32(14, (int32)MapMaskB);
	W.U8(18, (uint8)(int8)FMath::Clamp(JobFilter, -1, 127));   // −1 = «Все»
	W.U8(19, (uint8)FMath::Clamp(LevelMin, 1, 255));
	W.U8(20, (uint8)FMath::Clamp(LevelMax, 1, 255));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_GET_MEMBER, TEXT("SendPartyFindGetMember3414"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_GET_MEMBER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→3414 поиск отряда, страница одиночек %d: мойНомер=%d маска=%08X/%08X проф=%d ")
		TEXT("уровни %d..%d (45 б) -> %s"),
		Page, MemberRegId, MapMaskA, MapMaskB, JobFilter, LevelMin, LevelMax,
		bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyFindUpdateMember3416(int32 MemberRegId, uint32 MapMaskA, uint32 MapMaskB)
{
	FAsdaPacketWriter W(22);
	PfWriteWho(W, (int32)SelfUserNum, SelectedCharId);
	uint8 Job = 0, Cls = 0, Lv = 0;
	PartyFindSelfTriplet(Job, Cls, Lv);
	W.U8(6, Job); W.U8(7, Cls); W.U8(8, Lv);
	W.U8(9, (uint8)(CurrentChannel + 1));
	W.I32(12, (int32)MapMaskA);
	W.I32(16, (int32)MapMaskB);
	W.I16(20, (int32)PfRegId(MemberRegId));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_UPDATE_MEMBER, TEXT("SendPartyFindUpdateMember3416"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_UPDATE_MEMBER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→3416 поиск отряда, обновить свою запись: мойНомер=%d маска=%08X/%08X (46 б) -> %s"),
		MemberRegId, MapMaskA, MapMaskB, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyFindAddLeader3418(const FString& SelfCharName, int32 WantMembers,
	int32 InPartyFlag, int32 PartyIndex)
{
	FAsdaPacketWriter W(36);
	PfWriteWho(W, (int32)SelfUserNum, SelectedCharId);
	uint8 Job = 0, Cls = 0, Lv = 0;
	PartyFindSelfTriplet(Job, Cls, Lv);
	W.U8(6, Job); W.U8(7, Cls); W.U8(8, Lv);
	W.U8(9, (uint8)(CurrentChannel + 1));
	W.Cp1251(12, SelfCharName, 19);         // char[20] имя лидера
	// ★ 1:1: эталон кладёт в [32] КОНСТАНТУ 6 (`LOBYTE(pktBody[16]) = 6`), а не выбор комбобокса —
	//   размер отряда правится потом пакетом 3424. Параметр оставлен, чтобы вызвать иначе было
	//   осознанным действием, а не опечаткой.
	W.U8(32, (uint8)FMath::Clamp(WantMembers, 0, 255));
	W.U8(33, (uint8)FMath::Clamp(InPartyFlag, 0, 255));
	W.I16(34, (int32)(uint16)(int16)PartyIndex);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_ADD_LEADER, TEXT("SendPartyFindAddLeader3418"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_ADD_LEADER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→3418 поиск отряда, опубликовать отряд: лидер='%s' нужноЧленов=%d вОтряде=%d ")
		TEXT("индексОтряда=%d (60 б) -> %s"),
		*SelfCharName, WantMembers, InPartyFlag, PartyIndex, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyFindSubLeader3420(int32 LeaderRegId)
{
	FAsdaPacketWriter W(8);
	PfWriteWho(W, (int32)SelfUserNum, SelectedCharId);
	W.I16(6, (int32)PfRegId(LeaderRegId));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_SUB_LEADER, TEXT("SendPartyFindSubLeader3420"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_SUB_LEADER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→3420 поиск отряда, снять отряд со списка: мойОтряд=%d (32 б) -> %s"),
		LeaderRegId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyFindGetLeader3422(int32 Page, int32 LeaderRegId,
	uint32 MapMaskA, uint32 MapMaskB, int32 ChannelFilter, int32 GoalFilter)
{
	FAsdaPacketWriter W(20);
	PfWriteWho(W, (int32)SelfUserNum, SelectedCharId);
	W.I16(6, (int32)(uint16)FMath::Max(1, Page));
	W.I16(8, (int32)PfRegId(LeaderRegId));
	W.I32(10, (int32)MapMaskA);
	W.I32(14, (int32)MapMaskB);
	W.U8(18, (uint8)(int8)FMath::Clamp(ChannelFilter, -1, 127));
	W.U8(19, (uint8)(int8)FMath::Clamp(GoalFilter, -1, 127));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_GET_LEADER, TEXT("SendPartyFindGetLeader3422"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_GET_LEADER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→3422 поиск отряда, страница отрядов %d: мойОтряд=%d маска=%08X/%08X канал=%d цель=%d ")
		TEXT("(44 б) -> %s"),
		Page, LeaderRegId, MapMaskA, MapMaskB, ChannelFilter, GoalFilter, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendPartyFindUpdateLeader3424(int32 LeaderRegId, uint32 MapMaskA,
	uint32 MapMaskB, int32 Goal, int32 WantMembers, int32 InPartyFlag)
{
	FAsdaPacketWriter W(25);
	PfWriteWho(W, (int32)SelfUserNum, SelectedCharId);
	uint8 Job = 0, Cls = 0, Lv = 0;
	PartyFindSelfTriplet(Job, Cls, Lv);
	// ★★★ ДЕФЕКТ ОРИГИНАЛА Д1: эталон пишет байт +6 ДВАЖДЫ (сначала профессию, затем класс),
	//   так что профессия теряется, а байт +7 уходит нулём. Мы по умолчанию ИСПРАВЛЯЕМ (проф в +6,
	//   класс в +7), но оставляем ключ отката к побайтово-эталонному поведению.
	if (CVarPartyFindLegacyBossJobByte.GetValueOnGameThread() != 0)
	{
		W.U8(6, Cls);    // «что было в оригинале»: класс затирает профессию
		W.U8(7, 0);      // и байт класса остаётся нулём из обнулённого буфера
	}
	else
	{
		W.U8(6, Job);
		W.U8(7, Cls);
	}
	W.U8(8, Lv);
	W.U8(9, (uint8)(CurrentChannel + 1));
	W.I32(12, (int32)MapMaskA);
	W.I32(16, (int32)MapMaskB);
	W.U8(20, (uint8)(int8)FMath::Clamp(Goal, -1, 127));
	W.U8(21, (uint8)FMath::Clamp(WantMembers, 0, 255));
	W.U8(22, (uint8)FMath::Clamp(InPartyFlag, 0, 255));
	W.I16(23, (int32)PfRegId(LeaderRegId));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_CLIENT_UPDATE_LEADER, TEXT("SendPartyFindUpdateLeader3424"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_CLIENT_UPDATE_LEADER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log,
		TEXT("→3424 поиск отряда, правка отряда: мойОтряд=%d маска=%08X/%08X цель=%d нужноЧленов=%d ")
		TEXT("вОтряде=%d (49 б) -> %s"),
		LeaderRegId, MapMaskA, MapMaskB, Goal, WantMembers, InPartyFlag, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═══════════════════ КЛАН: 4104 / 4106 / 4138 (см. объявления в .h) ═══════════════════════════
//   Окна `CLAN_MAKING_WIN` (14400, код NPC 30015) и `CLAN_DELETE_BOX` (14470, код NPC 30016).
//   Полный разбор — `Reports/npc_repair_variants_30015.md`.
bool UAsdaNetSubsystem::SendClanCreate4104(uint8 ClanType, const FString& ClanName)
{
	FAsdaPacketWriter W(72);                        // `authPasswordCtxCtor@0xBC8C00` обнуляет ВСЁ тело
	// @0 — эталон кладёт `g_aCharSelectData`; сервер `ClanCreateReq@0x4017B0` читает тело с байта 4
	//   (`memcpy(clanBody, pPacket + 33, 0x48)` → `clanBody[2]` = байт 4), поэтому поле для него
	//   информационное. Шлём charId — то же значение, что и в остальных запросах порта.
	W.I32(0, SelectedCharId);
	W.U8(4, ClanType);                                // ★ эталон ВСЕГДА 0 (дефект D1)
	// @5 — 17 байт под имя, последний обязан остаться нулём (сервер копирует ровно 0x11 байт).
	W.Cp1251(5, ClanName, 16);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CLAN_CREATE_REQ, TEXT("SendClanCreate4104"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CLAN_CREATE_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→4104 создание клана: имя «%s» тип=%d (96 б) -> %s"),
		*ClanName, (int32)ClanType, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendClanDelete4106(int32 ClanId)
{
	FAsdaPacketWriter W(2);
	W.I16(0, ClanId);                // эталон берёт КЭШ окна (window+1556), это тот же clanId
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CLAN_DELETE_REQ, TEXT("SendClanDelete4106"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CLAN_DELETE_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→4106 роспуск клана: clanId=%d (26 б) -> %s"),
		ClanId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendClanRosterRefresh4138()
{
	FAsdaPacketWriter W(4);
	// ★ 27.09.2026 [ОРИГИНАЛ]: эталон `pwSeqHash = …->userIdx` = charId (`char+1052`, миснейм IDA); прежде уходил userNum.
	//   Сервер `ClanMemberLeaveReq@0x403840` проверяет только `>= 0` — поведение сервера то же.
	W.I32(0, SelectedCharId);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CLAN_MEMBER_LEAVE_REQ_4138, TEXT("SendClanRosterRefresh4138"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CLAN_MEMBER_LEAVE_REQ_4138, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→4138 обновить состав клана: charId=%d (28 б) -> %s"),
		SelectedCharId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 27.09.2026: расширение клана 4136 (раскладка и обоснование — в объявлении в .h).
bool UAsdaNetSubsystem::SendClanIncrease4136(int32 ClanId)
{
	FAsdaPacketWriter W(7);
	W.I32(0, SelectedCharId);       // `char+0x41C` (userIdx) = charId — сервер поле не читает; то же, что у 4138 (★ 27.09.2026: прежде userNum)
	W.I16(4, ClanId);               // `char+0x968` — единственное поле, которое читает сервер
	W.U8(6, 5);                     // литерал клиента `mov byte [ebp-6], 5` (0xBC5144); сервер его НЕ читает
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CLAN_BASE_INFO_MAX_CLAN_USER_COUNT_REQ, TEXT("SendClanIncrease4136"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CLAN_BASE_INFO_MAX_CLAN_USER_COUNT_REQ, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→4136 расширение клана: clanId=%d charId=%d (31 б) -> %s"),
		ClanId, SelectedCharId, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ★ 27.09.2026: 4111 — ответ на приглашение в клан (раскладка — в объявлении). Порядок записи 1:1 с эталоном
//   (item_inventory.c:7367-7373): заготовка −1/0 → @0 ответ → @1 clanId → @20/@22 → имя `sprintf("%s")` с @3.
bool UAsdaNetSubsystem::SendClanInviteAnswer4111(bool bAccept, int32 ClanId, const TArray<uint8>& ClanName17,
	int32 ReqUserNum, int32 RespUserNum)
{
	FAsdaPacketWriter W(24);             // @3..@19 = 0 (`memset((char*)this + 3, 0, 0x11)`)
	W.I16(1, -1); W.I16(20, -1); W.I16(22, -1);   // заготовка `itemInscriptionCtxCtor@0xBCA150`
	W.U8(0, bAccept ? 1 : 0);            // `LOBYTE(v9[0]) = a2`
	W.I16(1, ClanId);                    // `v10[948]` — clanId из 4110 @0
	W.I16(20, ReqUserNum);               // `v10+1915` — 4110 @19
	W.I16(22, RespUserNum);              // `v10+1917` — 4110 @21
	// Имя: `sprintf(&v9[1]+1, "%s", v10+1898)` — байты до первого нуля. Сервер кладёт в 4110 не больше 16 значащих
	//   + NUL; поле тела 17 байт (@3..@19), поэтому длиннее 16 не пишем (у эталона `sprintf` залез бы в @20).
	for (int32 i = 0; i < 16 && i < ClanName17.Num() && ClanName17[i] != 0; ++i) { W.U8(3 + i, ClanName17[i]); }
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CLAN_MEMBER_INVITE_RECV_RESPONSE_USER, TEXT("SendClanInviteAnswer4111"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_CLAN_MEMBER_INVITE_RECV_RESPONSE_USER, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→4111 ответ на приглашение в клан: %s clanId=%d пригласил=%d мне=%d (48 б) -> %s"),
		bAccept ? TEXT("ДА") : TEXT("нет"), ClanId, ReqUserNum, RespUserNum, bOk ? TEXT("OK") : TEXT("FAIL"));
	return TrackSend(bOk);
}

// ═════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ СООБЩЕСТВО (14500 «Приятель», 14033 «Клан», 14600 «Кто?») + СОУЛМЕЙТ (навыки Дружбы,
//     Зов друга, рулетка) — ОТПРАВИТЕЛИ. Все до одного — конверт «B» (`SendSecured`):
//     [0..7] обфусцированный токен + [8..23] MD5(payload, Len) + [24..] payload, паддинг ВКЛЮЧЁН
//     (`CNetwork_Send(..., a6 = 1)` во всех эталонных сборщиках этих семейств).
//     Источники раскладок: `Reports/community_tabs_frnd_clan_who.md` §1.6 / §2.6 / §3.6 и
//     `Reports/soulmate_skills_recall_roulette.md` §0 / B.2 / C.2 / C.4.
// ═════════════════════════════════════════════════════════════════════════════════════════════

// Обновить кэш записей приятелей одной записью в 31 б. Ключ — ПАРА (charId @0, код класса @4),
//   ровно как у эталона (`listContainsIdTypePair`: `*(DWORD*)элемент == a2 && *(u8*)(элемент+4) == a3`).
void UAsdaNetSubsystem::UpsertFriendEntry(const uint8* Rec31)
{
	if (!Rec31) { return; }
	const int32 CharId = AsdaBytes::RdI32(&Rec31[0]);
	const uint8 Cls    = Rec31[4];
	for (TArray<uint8>& R : FriendEntries)
	{
		if (R.Num() >= 31 && AsdaBytes::RdI32(&R[0]) == CharId && R[4] == Cls)
		{
			FMemory::Memcpy(R.GetData(), Rec31, 31);   // запись та же — обновляем на месте
			return;
		}
	}
	TArray<uint8> Rec;
	Rec.SetNumUninitialized(31);
	FMemory::Memcpy(Rec.GetData(), Rec31, 31);
	FriendEntries.Add(MoveTemp(Rec));
}

// C→S 4156 «дай список приятелей» (`netSendReqOp4156@0xCE8A30`). Полный размер 24 = 8 + 16 + 0,
//   payload ПУСТОЙ (`Net_Md5Digest16(salt, nullptr, 0, md5)` — MD5 считается от пустого тела).
//   ★ Антиспам эталона: `isWithinTimeout5000@0xCE9B00` — `timeGetTime() − this[66] <= 0x1388`,
//     то есть чаще раза в 5000 мс запрос НЕ уходит вовсе (функция обёрнута в `if (!...)`).
bool UAsdaNetSubsystem::SendFriendListReq4156()
{
	if (!Conn || !Conn->IsConnected()) { return false; }
	const double Now = FPlatformTime::Seconds();
	if (LastFriendListReqTime >= 0.0 && (Now - LastFriendListReqTime) <= 5.0)
	{
		UE_LOG(LogAsdaNet, Log,
			TEXT("→4156 список приятелей НЕ отправлен: антиспам эталона 5000 мс (прошло %.0f мс)"),
			(Now - LastFriendListReqTime) * 1000.0);
		return false;
	}
	const bool bOk = TrackSend(SendSecured((uint16)OP_FRIEND_MEMBER_LIST_REQ, nullptr, 0));
	// Метку времени эталон ставит ПОСЛЕ отправки (`sub_93A54D(this)`), поэтому неудачная отправка
	//   антиспам не заводит — иначе после обрыва кнопка «Обновить» была бы мертва ещё 5 секунд.
	if (bOk) { LastFriendListReqTime = Now; }
	UE_LOG(LogAsdaNet, Log, TEXT("→4156 запрос списка приятелей (24 б, payload пустой) -> %s"),
		bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 4153 «ответ на приглашение в приятели» (`netSendPacket1039@0xCE7F30`), 47 = 24 + 23.
//   payload(23): @0 char[20] имя пригласившего · @20 i16 userIndex · @22 u8 ответ (1 принять / 2 отклонить).
bool UAsdaNetSubsystem::SendFriendAnswer4153(bool bAccept, int32 CharId, const FString& Name)
{
	if (!IsSendLinkUp()) { return false; }
	FAsdaPacketWriter W(23);
	// @0 char[20] имя пригласившего. Эталон копирует его из записи очереди (`элемент + 2`); сервер
	//   имя НЕ читает вовсе (`Recv_UserFriendOK@0x537BF0` работает по i16), но байты обязаны быть на месте.
	W.Cp1251(0, Name, 19);   // 19 символов + NUL в пределах 20
	// @20 i16 userIndex пригласившего. ★ ЭТО userNum (i16), А НЕ charId (i32): перепутать — сервер
	//   МОЛЧА проигнорирует пакет. Достоверный источник — очередь входящих 4152 (её поле @20);
	//   параметр `CharId` контракта используется только как запасной вариант.
	int32 UserNum = -1;
	int32 FoundIdx = INDEX_NONE;
	{
		uint8 NameBytes[20];
		FMemory::Memzero(NameBytes, sizeof(NameBytes));
		FAsdaLoginData::EncodeCp1251(Name, NameBytes, 19);
		// Сравнение имён — ДО первого NUL, а не «в лоб» 20 байтами: сервер поле именем не
		//   обнуляет полностью, за терминатором в 4152 может лежать мусор стека.
		auto SameName20 = [](const uint8* A, const uint8* C) -> bool
		{
			for (int32 k = 0; k < 20; ++k)
			{
				if (A[k] != C[k]) { return false; }
				if (A[k] == 0)    { return true;  }
			}
			return true;
		};
		for (int32 i = 0; i < PendingFriendInvites.Num(); ++i)
		{
			const TArray<uint8>& Inv = PendingFriendInvites[i];
			if (Inv.Num() < 22) { continue; }
			if (SameName20(Inv.GetData(), NameBytes))
			{
				UserNum = AsdaBytes::RdI16(&Inv[20]);
				FoundIdx = i;
				break;
			}
		}
	}
	if (UserNum < 0)
	{
		UserNum = (int32)(int16)CharId;   // ★ усечение до i16: поле провода — двухбайтовое
		UE_LOG(LogAsdaNet, Warning,
			TEXT("→4153: приглашения от «%s» в очереди нет — userNum берём из параметра CharId (%d → i16 %d)"),
			*Name, CharId, UserNum);
	}
	W.I16(20, UserNum);
	// @22 u8 ответ. Эталон шлёт ровно 1 и 2 (значение 3 сервер предусматривает — ответ 4151 код 6, —
	//   но клиент его не отправляет; не выдумываем и мы).
	W.U8(22, bAccept ? 1 : 2);
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_FRIEND_OK, TEXT("SendFriendAnswer4153"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_RECV_USER_FRIEND_OK, W.Data(), W.Num()));
	// Эталон удаляет запись из очереди сразу после отправки (`sub_93A660(this+68, …)`).
	if (bOk && FoundIdx != INDEX_NONE) { PendingFriendInvites.RemoveAt(FoundIdx); }
	UE_LOG(LogAsdaNet, Log, TEXT("→4153 ответ на приглашение: «%s» userNum=%d ответ=%u -> %s"),
		*Name, UserNum, (uint32)W[22], bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 4154 «удалить приятеля» (`netSendTimeSyncMask49@0xCE8180`, ★ МИСНЕЙМ), 49 = 24 + 25.
//   ★★★ ПРОБЕЛ ЗАКРЫТ 31.08.2026. Раньше поля @4/@5 при пустом кэше уходили НУЛЯМИ «без риска».
//   Риск ЕСТЬ: доказано, что сервер читает ВСЕ ТРИ поля и передаёт их дальше в БД.
//
//   (1) Раскладка — эталон. `netSendTimeSyncMask49@0xCE8180` (game.c:329645-329654) собирает
//       25-байтовый `packet` ТОЛЬКО из выбранной записи списка (`sub_926D09(self+59, idx)`):
//           packet[0]        = *(_DWORD *)(entry + 0)                 → @0 i32 CharUniqueIndex
//           LOBYTE(packet[1])= *(_BYTE  *)(entry + 4)                 → @4 u8  код класса
//           sprintf(&packet[1]+1, "%s", (const char *)(entry + 11))   → @5 char[20] имя (asciiz)
//       и `memcpy(&sendBuf[24], packet, 0x19u)` + `CNetwork_Send(..., 49, …)` (game.c:329752-329754).
//       Индекс записи: `sub_92AA35` — заглушка компоновки, дизасм `0x92aa35: jmp 0xce9bd0`;
//       тело `sub_CE9BD0` = `[this+0x100] < 0 ? -1 : [this+0x100] + 6 * [this+0xFC]`
//       (строка на странице + 6 × номер страницы) ⇒ ДРУГОГО источника полей у клиента НЕТ.
//   (2) Сервер ЧИТАЕТ поля (второй метод). `FriendMemberDeleteReq@0x406630`:
//         GameServer.c:2658  if ( sub_523F00(a3, 0x19u, a2, 1) )        // длина payload = 25, MD5 по ним
//         GameServer.c:2660  memcpy(v6, (char *)a3 + 33, 0x19u);        // a3+33 == наш payload+0
//         GameServer.c:2664  v12 = v6[4];                               // ← @4 код класса
//         GameServer.c:2665  v11 = *(_DWORD *)v6;                       // ← @0 charId
//         GameServer.c:2666  strncpy(Destination, &v6[5], 0x14u);       // ← @5 имя[20]
//         GameServer.c:2668  return sub_5646C0(..., 2217, 32, v4, v8, a2);   // → DBManager, 32 б
//       ★ v8/v9/v10/v11/v12/Destination — не «локали», а ОДИН смежный стековый блок 32 б
//       (ebp-0x2C … ebp-0x0C: 2+4+1+4+1+20 = 32 — ровно размер, переданный в sub_5646C0):
//         @0 u8 · @1 u8 сервер · @2 i32 свой charId · @6 u8 · @7 i32 charId удаляемого ·
//         @11 u8 код класса · @12 char[20] имя.
//   (3) Зачем серверу имя. Ответ из БД разбирает `sub_4067C0@0x4067C0`: при успехе (`v9[0] == 1`)
//       он берёт `v11 = *(_DWORD *)&v9[7]` (charId), `v12 = v9[11]` (класс) и шлёт
//       `sub_537FB0(userMng, &v9[12], 6, 4155, &v5)` (GameServer.c:2617) — то есть находит ВТОРОГО
//       участника ПО ИМЕНИ (`&v9[12]`) и рассылает ему 4155, чтобы запись пропала и у него.
//       Пустое имя ⇒ второй участник уведомления не получает и держит мёртвую запись до перезахода.
//   (4) Откуда берём. `FriendEntries` — не «один из» источников, а СВОДНЫЙ: в него кладут ВСЕ три
//       производителя записей приятелей, какие есть у клиента (4157 список, 4160 обновление одной
//       записи, 4151 код 1 «приглашение принято»). Четвёртого источника 31-байтовой записи в
//       клиенте не существует, а брать класс/имя из чужих семейств (5481 «Кто?», 4152) —
//       выдумывание: кодировки полей там не доказаны.
//   (5) Поэтому при отсутствии записи НЕ ШЛЁМ ВООБЩЕ. Эталон в такое состояние попасть не может
//       (кнопка «Удалить» работает по ВЫДЕЛЕННОЙ строке списка); отправка с пустым именем — это
//       заведомо испорченный пакет, который сервер примет и молча рассинхронизирует список
//       второго игрока. Отказ громкий и диагностируемый. Уверенность HIGH.
bool UAsdaNetSubsystem::SendFriendDelete4154(int32 CharId)
{
	if (!IsSendLinkUp()) { return false; }
	// Ключ поиска — charId (@0 записи). Эталон берёт запись по индексу строки, а не по charId,
	//   но пара (charId, класс) у него же служит ключом дедупликации (`listContainsIdTypePair`),
	//   значит charId однозначно адресует запись.
	const TArray<uint8>* Rec = FriendEntries.FindByPredicate([CharId](const TArray<uint8>& R)
	{
		return R.Num() >= 31 && AsdaBytes::RdI32(&R[0]) == CharId;
	});
	if (!Rec)
	{
		UE_LOG(LogAsdaNet, Error,
			TEXT("→4154 НЕ ОТПРАВЛЕН: записи приятеля charId=%d нет в кэше (%d записей). Код класса и ")
			TEXT("имя есть ТОЛЬКО в записи списка, а сервер (`FriendMemberDeleteReq`, GameServer.c:2664-2666) ")
			TEXT("читает оба и по ИМЕНИ уведомляет второго участника (4155) — пакет с пустым именем ")
			TEXT("рассинхронизировал бы его список. Запросите список 4156 и повторите."),
			CharId, FriendEntries.Num());
		return false;
	}
	FAsdaPacketWriter W(25);
	W.I32(0, CharId);                 // @0 i32 charId — ★ i32, НЕ userNum
	W.U8(4, (*Rec)[4]);                                // @4 u8 код класса (запись @4)
	// @5 char[20] имя (запись @11). ★ Эталон кладёт его не memcpy, а `sprintf("%s")` в буфер,
	//   обнулённый конструктором `sub_CE87F0@0xCE87F0` (`memset(this+5, 0, 0x14u)`), — то есть
	//   копирует ДО первого нуля, хвост остаётся нулевым. Повторяем ровно это: если в записи после
	//   имени лежит мусор, memcpy утащил бы его в провод, а сервер делает `strncpy(dst, &body[5], 20)`.
	for (int32 i = 0; i < 20; ++i)
	{
		const uint8 Ch = (*Rec)[11 + i];
		if (Ch == 0) { break; }
		W.U8(5 + i, Ch);
	}
	// Для лога — безопасная копия имени с гарантированным нулём (в проводе поле может быть заполнено
	//   все 20 байт без терминатора; читать его как C-строку прямо из P нельзя).
	char NameZ[21];
	FMemory::Memcpy(NameZ, W.Data() + 5, 20);
	NameZ[20] = 0;
	if (AsdaPacketLayoutBroken(W, (uint16)OP_FRIEND_MEMBER_DELETE_REQ, TEXT("SendFriendDelete4154"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_FRIEND_MEMBER_DELETE_REQ, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log, TEXT("→4154 удалить приятеля: charId=%d класс=%u имя«%hs» -> %s"),
		CharId, (uint32)W[4], NameZ, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 4122 (`netSendAntiCheatTokenOp4122@0xBCACA0`, ★ МИСНЕЙМ), 26 = 24 + 2. payload(2): @0 i16 clanIndex.
//   ★★★ ВНИМАНИЕ: по отчёту 4122 — это БАЗОВАЯ ИНФА клана (ответ 4123), а СПИСОК ЧЛЕНОВ запрашивает
//   4124 (ответ 4125). Имя метода из контракта («MemberList») противоречит опкоду; ведущим считаем
//   ОПКОД. Параметр `Page` в проводе не существует — трактуем его как клан-индекс.
bool UAsdaNetSubsystem::SendClanBaseInfoReq4122(int32 ClanIndex)
{
	if (!IsSendLinkUp()) { return false; }
	// Постраничного запроса у семейства нет: сервер сам шлёт весь список пакетами 4125 и закрывает
	//   пустым 4126. Поэтому > 0 читаем как клан-индекс, ≤ 0 — как «взять известный».
	const int32 Clan = (ClanIndex > 0) ? ClanIndex : KnownClanIndex;
	if (Clan > 0) { KnownClanIndex = Clan; }
	FAsdaPacketWriter W(2);
	W.I16(0, FMath::Clamp(Clan, 0, 10000));   // серверный гейт `0 ≤ id ≤ 10000`
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_CLAN_BASE_INFO, TEXT("SendClanBaseInfoReq4122"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_SEND_CLAN_BASE_INFO, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log,
		TEXT("→4122 базовая инфа клана (clanIndex=%d; ответ 4123) -> %s ")
		TEXT("[★ список членов запрашивает 4124, а не этот пакет]"),
		Clan, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 4124 (`netSendAntiCheatToken@0xBCAEA0`, ★ МИСНЕЙМ), 26 = 24 + 2. payload(2): @0 i16 clanIndex.
//   Это и есть ЗАПРОС СПИСКА ЧЛЕНОВ (сервер `Send_ClanUserList_DB@0x401450`, DB 2175 → 4125 + 4126).
bool UAsdaNetSubsystem::SendClanMemberListReq4124(int32 ClanIndex)
{
	if (!IsSendLinkUp()) { return false; }
	FAsdaPacketWriter W(2);
	// Клан-индекс: явный аргумент, иначе последний известный (его запоминает 4122).
	const int32 Clan = (ClanIndex > 0) ? ClanIndex : KnownClanIndex;
	if (Clan > 0) { KnownClanIndex = Clan; }
	W.I16(0, FMath::Clamp(Clan, 0, 10000));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_CLAN_USER_LIST_DB, TEXT("SendClanMemberListReq4124"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_SEND_CLAN_USER_LIST_DB, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log,
		TEXT("→4124 СПИСОК ЧЛЕНОВ клана (clanIndex=%d; ответ 4125 записями по 131 б + пустой 4126) -> %s"),
		KnownClanIndex, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 4116 «ВЫЙТИ ИЗ КЛАНА» (`netSendNameConfirmPacket@0xBCA620`, ★ МИСНЕЙМ), 48 = 8 + 16 + 24.
//   payload(24): @0 i32 мой charId (`char+0x41C`) · @4 char[20] моё имя (`char+0x420`, sprintf).
//   Сервер `ClanMemberLeaveReq_0@0x403930`: MD5 по 24 б (`sub_523F00(pPacket, 0x18u, …)`), тело
//   `memcpy(leaveBody, pPacket+33, 24)`; гейты — членство, `CUser+99115 != 1` (глава не выходит),
//   `*(DWORD*)body == CUser+372`, непустое имя. Любой отказ → 4117 с `body[0] = 0`.
//   ★ ГЕЙТ «ГЛАВА НЕ МОЖЕТ ВЫЙТИ» держит ВЫЗЫВАЮЩИЙ (у него окно для нотиса 12433) — здесь мы
//     лишь ПРЕДУПРЕЖДАЕМ в журнал, чтобы молчаливого отказа сервера никто не искал вслепую.
// 6515 — удалить объявление клана (`netSendAuthToken6515@0xBCC470`, имя-миснейм).
//   Конверт B, payload 10: @0 i32 charId · @4 u16 clanIndex · @6 i32 ключ записи.
bool UAsdaNetSubsystem::SendClanNoticeDelete6515(int32 CharId, int32 ClanIndex, int32 NoticeKey)
{
	FAsdaPacketWriter W(10);
	W.I32(0, CharId);                                  // charId (i32, НЕ userNum)
	W.I16(4, FMath::Clamp(ClanIndex, 0, 10000));       // clanIndex (u16), гейт сервера
	W.I32(6, NoticeKey);                               // ключ записи объявления
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_NOTICE_DEL, TEXT("SendClanNoticeDelete6515"))) { return false; }
	const bool bOk = SendSecured((uint16)OP_RECV_USER_NOTICE_DEL, W.Data(), W.Num());
	UE_LOG(LogAsdaNet, Log, TEXT("→6515 удалить объявление клана: charId=%d clan=%d ключ=%d -> %s"),
		CharId, ClanIndex, NoticeKey, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return TrackSend(bOk);
}

bool UAsdaNetSubsystem::SendClanLeaveReq4116(int32 CharId, const FString& CharName)
{
	if (!IsSendLinkUp()) { return false; }
	if (CharName.IsEmpty())
	{
		// Серверный гейт `strlen(имя) != 0` — пустое имя гарантированно вернёт 4117 код 0.
		UE_LOG(LogAsdaNet, Warning, TEXT("→4116 не отправлен: имя персонажа пустое (сервер отобьёт кодом 0)"));
		return false;
	}
	if (SelfClanRank == 1)
	{
		UE_LOG(LogAsdaNet, Warning,
			TEXT("→4116 отправляется при звании 1 (глава): эталон в этом случае пакет НЕ шлёт, ")
			TEXT("а показывает нотис 12433; сервер отобьёт 4117 кодом 0"));
	}
	FAsdaPacketWriter W(24);
	W.I32(0, CharId);                    // ★ charId (i32), НЕ userNum (i16)
	W.Cp1251(4, CharName, 19);              // char[20] с гарантированным нулём
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CLAN_MEMBER_LEAVE_REQ, TEXT("SendClanLeaveReq4116"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_CLAN_MEMBER_LEAVE_REQ, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log,
		TEXT("→4116 ВЫХОД ИЗ КЛАНА: charId=%d имя='%s' (48 б; ответ 4117, 25 б) -> %s"),
		CharId, *CharName, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 6511 «ПРОЧИТАТЬ ОБЪЯВЛЕНИЯ КЛАНА» (`netSendAntiCheatTokenPacket@0xBCC1C0`, ★ МИСНЕЙМ), 32 = 8 + 16 + 8.
//   payload(8): @0 i32 мой charId · @4 u16 индекс клана (блок 4123, смещ. 0) · @6 u16 курсор/страница.
//   Сервер `Recv_User_Notice_Read@0x406EF0`: MD5 по 8 б, тело `a3+33`; гейты `(i16)clanIndex ≤ 10000`
//   и `CUser+372 == charId`. Ответ — 6512 (546 б); при ошибке 6512 с `body[0] = 0`.
bool UAsdaNetSubsystem::SendClanNoticeReadReq6511(int32 CharId, int32 ClanIndex, int32 Cursor)
{
	if (!IsSendLinkUp()) { return false; }
	const int32 Clan = (ClanIndex > 0) ? ClanIndex : KnownClanIndex;
	if (Clan > 0) { KnownClanIndex = Clan; }
	FAsdaPacketWriter W(8);
	W.I32(0, CharId);
	// ★ `WrI16` пишет ДВА байта в том же порядке (LE) — знаковость на проводе роли не играет;
	//   курсор зажат в 0..32767, чтобы запись не поменяла знак у серверного `(i16)`-гейта.
	W.I16(4, FMath::Clamp(Clan,   0, 10000));   // серверный гейт `(i16)clanIndex ≤ 10000`
	W.I16(6, FMath::Clamp(Cursor, 0, 32767));
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_NOTICE_READ, TEXT("SendClanNoticeReadReq6511"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_RECV_USER_NOTICE_READ, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6511 объявления клана: charId=%d clanIndex=%d курсор=%d (32 б; ответ 6512 по 546 б) -> %s"),
		CharId, Clan, Cursor, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// ══════════ ★★★ 6513 / 6518 — ЗАПИСЬ ОБЪЯВЛЕНИЯ КЛАНА И ЗАМЕТКИ О СЕБЕ (добавлено 2026-09-02) ═══
//   В порту отправителей не было вовсе, хотя опкоды объявлены, а СТАРЫЙ сервер их знает:
//   `GameServer.c:19114` `case 0x1971 → Recv_User_Notice_Write@0x4072C0` и
//   `:19124` `case 0x1976 → Recv_User_Memo_Write@0x409320`. Из-за этого кнопки клан-панели
//   «Сохранить» (14023), «ОК» (14031) и «Отмена» (14030) сохранять было нечем.
//   Здесь только СЕТЕВОЙ СЛОЙ; привязка кнопок живёт в `AsdaCommunityClanSubsystem` (чужой файл).
//
//   КОНВЕРТ — вид А, с анти-реплей-секретом (`SendSecured`), паддинг штатный:
//     клиент `chat.c:5741-5743` / `:5789-5791` — [0..7] `buildAntiCheatToken()`, [8..23] MD5(payload),
//     [24..] payload; сервер читает токен с `a3+9` (`sub_524390`) и тело с `a3+33`, а MD5 сверяет
//     через `sub_523F00(a3, <len>, a2, 1)` — последний аргумент 1 и означает «секрет есть».
//   ⚠ НЕ `SendMd5Signed`: у того конверта секрета нет и сверка MD5 на сервере не сойдётся.

// C→S 6513 «НАПИСАТЬ ОБЪЯВЛЕНИЕ КЛАНА» (эталон `chatSendMessage_BE30@0xBCBE30`), 574 = 24 + 550.
//   payload(550) — 1:1 с эталонным буфером `_DWORD chatPacket[138]`:
//     @0   i32  charId          `*(DWORD*)(g_LocalPlayer + 1052)` = `char+0x41C` (chat.c:5709)
//     @4   i16  clanIndex       `*((WORD*)panel + 778)` (chat.c:5708)
//     @6   i32  ключ записи     ─┐ конструктор `sub_BCC150 → 0xBC6760` обнуляет ВЕСЬ этот блок,
//     @10  char[20]             │ отправитель его не заполняет: автора, дату и ключ проставляет
//     @30  char[20]             ─┘ сервер. (Разбор снят по дизасму конструктора.)
//     @50  char[500] текст      `sprintf((char*)&chatPacket[12] + 2, …)` = +50 (chat.c:5730)
//   Серверные гейты (`GameServer.c:2968-3092`): `(u32)clanIndex <= 10000`, клан совпадает с
//     `CUser+99096`, право `Authority & 2` по званию, и `strlen(&body[50])` в диапазоне 1..500.
//     Любой отказ → 6514 телом 1 байт.
//   ⚠ Разметка ограничивает поле ввода 14029 двумя сотнями символов (`LimitText = "200"`), тогда
//     как сервер принимает до 500 — это ограничение ОРИГИНАЛА, здесь ничего не «чиним».
bool UAsdaNetSubsystem::SendClanNoticeWrite6513(int32 CharId, int32 ClanIndex, const FString& Text)
{
	if (!IsSendLinkUp()) { return false; }
	const int32 Clan = (ClanIndex > 0) ? ClanIndex : KnownClanIndex;
	if (Clan > 0) { KnownClanIndex = Clan; }
	FAsdaPacketWriter W(550);
	W.I32(0, CharId);                             // charId (i32), НЕ userNum
	W.I16(4, FMath::Clamp(Clan, 0, 10000));       // серверный гейт `<= 10000`
	// ★ Защита оригинала от SQL-инъекции: апостроф → обратная кавычка (`chat.c:5722-5728`,
	//   тот же `bufferReplaceByte`, что у чата). Клиентская и только клиентская — по-настоящему
	//   лечится патчем сервера P28; воспроизводим 1:1, чтобы не отличаться от эталона.
	FString T = Text;
	T.ReplaceInline(TEXT("'"), TEXT("`"), ESearchCase::CaseSensitive);
	const int32 N = W.Cp1251(50, T, 500);
	// Эталонная обрезка (`chat.c:5731-5732`): при длине ≥ 500 обнуляется ПОСЛЕДНИЙ байт тела,
	//   то есть строка гарантированно заканчивается внутри поля и до сервера доходит ≤ 499 символов.
	if (N >= 500) { W.U8(549, 0); }
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_NOTICE_WRITE, TEXT("SendClanNoticeWrite6513"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_RECV_USER_NOTICE_WRITE, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6513 объявление клана: charId=%d clanIndex=%d длина=%d б (574 б; ответ 6514) -> %s"),
		CharId, Clan, N, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 6518 «НАПИСАТЬ ЗАМЕТКУ О СЕБЕ» (эталон `chatSendMessage_C7D0@0xBCC7D0`), 94 = 24 + 70.
//   payload(70) — 1:1 с эталонным буфером `_DWORD memoPacket[18]`:
//     @0  i32 charId       `*(DWORD*)(g_LocalPlayer + 1052)` (chat.c:5770)
//     @4  i16 clanIndex    `*((WORD*)panel + 778)` (chat.c:5769)
//     @6  char[64] текст   `sprintf((char*)&memoPacket[1] + 2, …)` = +6 (chat.c:5779);
//                          конструктор `sub_BCCAA0` заранее обнуляет ровно эти 64 байта.
//   Серверные гейты (`GameServer.c:3886-3954`): `(u32)clanIndex <= 10000`, совпадение с
//     `CUser+99096`, `strlen(&body[6]) <= 64`. Нижней границы у длины НЕТ — пустая заметка
//     законна и означает «стереть». Отказ → 6519.
bool UAsdaNetSubsystem::SendClanMemoWrite6518(int32 CharId, int32 ClanIndex, const FString& Text)
{
	if (!IsSendLinkUp()) { return false; }
	const int32 Clan = (ClanIndex > 0) ? ClanIndex : KnownClanIndex;
	if (Clan > 0) { KnownClanIndex = Clan; }
	FAsdaPacketWriter W(70);
	W.I32(0, CharId);
	W.I16(4, FMath::Clamp(Clan, 0, 10000));
	// Та же эталонная замена апострофа (`chat.c:5771-5777`).
	FString T = Text;
	T.ReplaceInline(TEXT("'"), TEXT("`"), ESearchCase::CaseSensitive);
	const int32 N = W.Cp1251(6, T, 64);
	// Эталонная обрезка (`chat.c:5780-5781`): при длине ≥ 64 обнуляется последний байт тела.
	if (N >= 64) { W.U8(69, 0); }
	if (AsdaPacketLayoutBroken(W, (uint16)OP_RECV_USER_MEMO_WRITE, TEXT("SendClanMemoWrite6518"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_RECV_USER_MEMO_WRITE, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log,
		TEXT("→6518 заметка о себе: charId=%d clanIndex=%d длина=%d б (94 б; ответ 6519) -> %s"),
		CharId, Clan, N, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 5480 «кто в игре» (`netSendTimestampPacket@0xEBF680`, ★ МИСНЕЙМ), 24 = 8 + 16 + 0 — тела НЕТ.
//   ★★★ ПРОБЕЛ ЗАКРЫТ 31.08.2026: «тела нет» ПОДТВЕРЖДЕНО двумя методами, и найдено, ГДЕ на самом
//   деле живут страница и фильтр. Контракт (Page, Filter) оставлен как есть — внутри правда.
//
//   (1) Тела нет — клиент. `netSendTimestampPacket@0xEBF680` (game.c:505568-505690) собирает
//       buf[0..7] секрет + buf[8..23] MD5 и отдаёт `CNetwork_Send(..., buf, 24, OP_SEND_WHO_COMMAND, …)`
//       (game.c:505688). Ни одного `memcpy(&buf[24], …)` в функции нет — 24 = 8 + 16 + 0.
//   (2) Тела нет — сервер (второй метод). Диспетчер `case 0x1568:` зовёт
//       `Send_WhoCommand(*(CUser **)(*(this+1)+52), a2)` (GameServer.c:19778-19780) — у обработчика
//       `Send_WhoCommand@0x54DB40` (GameServer.c:127871) СИГНАТУРА `(CUser*, __int16 a2)`:
//       указателя на пакет он не получает физически, прочитать тело нечем.
//   (3) ФИЛЬТР — целиком серверный, отдельного опкода нет. `Send_WhoCommand` обходит все слоты
//       (`sub_523E50`, до разрыва по `sub_5328F0`) и берёт запись, если (GameServer.c:127907-127911):
//         · не заблокирован (`!sub_52ECF0`), · charId ≠ свой, · charId ≥ 0,
//         · состояние `+0x2E` == 1 («в игре»), · два поля-флага == 0,
//         · |уровень_свой − уровень_чужой| ≤ 5.
//       Лимит выборки 42 записи (:127914). Порции: 35-байтовые записи копятся до 0x267 б и уходят
//       пакетами 5481 (:127937/:127943), затем пустой 5482 «конец списка» (:127944).
//   (4) СТРАНИЦА — целиком клиентская, в провод не идёт вообще. В окне `CCommunityWhoWnd`:
//         · `this[70]` — номер текущей страницы (0-based), `this[71]` — всего страниц;
//         · `uiCommunityWhoRefreshList@0xEBE630` рисует ровно 7 строк: `v10 = i + 7 * v12[70]`
//           (game.c:505237-505239) — НИКАКОГО поля фильтра в отрисовке нет;
//         · `uiCommunityWhoUpdatePaging@0xEBFD40` на приход 5482 считает
//           `totalPages = ceil(entryCount / 7)`, минимум 1 (game.c:505727-505731);
//         · кнопки 14603..14606 (след./посл./пред./перв.) меняют `this[70]` и перерисовывают —
//           ни одна из них ничего не отправляет.
//   (5) ЕДИНСТВЕННОЕ реальное действие отправителя, кроме самой отправки: `*(this + 73) = 0`
//       (game.c:505590) — СБРОС накопителя записей перед новой выдачей. Воспроизводим его здесь:
//       иначе повторный «Кто?» дописал бы новый список к старому (5481 только ДОПИСЫВАЕТ,
//       `communityWhoAppendListFromPacket@0xEBFC10`: `this->entries[35 * this->entryCount++]`).
//   Уверенность HIGH.
bool UAsdaNetSubsystem::SendWhoSearch5480(uint8 Page, uint8 Filter)
{
	if (!Conn || !Conn->IsConnected()) { return false; }
	// ★ Порядок как в эталоне: сначала сброс накопителя, потом отправка.
	const int32 Dropped = WhoEntries.Num();
	WhoEntries.Reset();
	const bool bOk = TrackSend(SendSecured((uint16)OP_SEND_WHO_COMMAND, nullptr, 0));
	UE_LOG(LogAsdaNet, Log,
		TEXT("→5480 «Кто?» (24 б, тела НЕТ — сервер `Send_WhoCommand` указателя на пакет не получает); ")
		TEXT("накопитель записей сброшен (было %d); страница=%u — клиентская (7 строк на стр., поле ")
		TEXT("окна this[70]), фильтр=%u — серверный (±5 уровней, «в игре», лимит 42) -> %s"),
		Dropped, (uint32)Page, (uint32)Filter, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 5237 «ответ на зов друга» (`netSkill_SendBuffApply5237@0xA09120`, ★ МИСНЕЙМ), 36 = 24 + 12.
//   payload(12): @0 u8 код · @1 i32 charId инициатора · @5 i16 userNum инициатора ·
//                @7..@11 пять эхо-байтов записи 5236 (@6, @7 карта, @8, @9 канал, @41 канал цели).
//
//   ★★★ ПРОБЕЛ ЗАКРЫТ 31.08.2026. Раньше: «@7..@11 — MED, сервер их не читает», при пустом кэше
//   всё уходило НУЛЯМИ. Теперь доказано и то, и другое — и исправлено поведение без кэша.
//
//   (1) Что делает сервер. `Send_ResponseUser_To_RequestUser_MateRecallSkill@0x49ED50`:
//         GameServer.c:64357  result = sub_523F00(a3, 0xCu, a2, 1);   // длина payload = 12, MD5 по ним
//         GameServer.c:64359  memcpy(v7, (char *)a3 + 33, sizeof(v7));// весь блок 12 б
//         GameServer.c:64360  if ( sub_5328F0(*(__int16 *)&v7[5]) )   // ← читает ТОЛЬКО @5 (userNum)
//         GameServer.c:64362  if ( *(int *)&v7[1] >= 0 )              // ← и ТОЛЬКО @1 (charId ≥ 0)
//         GameServer.c:64365  return sub_5646C0(..., 5237, 12, v4, v7, a2);   // ПЕРЕСЫЛАЕТ ВСЕ 12 б
//       Приёмная сторона межсерверного маршрута (`sub_49EE70@0x49EE70`, диспетчер `case 0x1475`
//       GameServer.c:20938) делает то же самое и отдаёт блок инициатору:
//         GameServer.c:64397  sub_4C28F0(*(this + 1), 5237, 12, *(__int16 *)&v4[5], v4, 0, 1);
//       Ошибки сервера именованы его же логом: «if( sRes.iRequestUserIndex < 0 )» (@1) и
//       «if( !IsCheckUserNum(sRes.iRequestUserArrayIndex) )» (@5) — GameServer.c:64368/64372.
//   (2) Что делает КЛИЕНТ ИНИЦИАТОРА (второй метод, до конца потока). Диспетчер `case 0x1475:`
//       → `Recv_CharStateNotice(&pktBuf[9])` (game.c:15571-15573), тело — `@0xF548D0`
//       (game.c:561369-561433): `switch (body[0])` по кодам 0/2/3/4/5, в каждой ветке
//       `CharManager_FindByUserIdx(g_CharManager, *(__int16 *)(body + 5))` и подсказка в чат
//       (loc 9576 / 9577 / 9584 / 13920 / 13928); `default: return;` — код 1 (согласие) молчит.
//       Больше НИ ОДНО поле не читается.
//   ⇒ ДОКАЗАНО: @7..@11 не читает НИКТО — ни сервер, ни клиент-получатель. Но длина обязана быть
//     ровно 12 (`sub_523F00(a3, 0xCu, …)`) и MD5 считается по всем 12 байтам, поэтому байты шлём.
//     Уверенность HIGH (раньше стояло MED).
//   (3) Чем заполнять, если кэша 5236 нет. Эталон в это состояние не попадает: он держит поля в
//       глобалях, которые заполняет обработчик 5236, и собирает пакет покомпонентно
//       (game.c:329931-329939 — копия всех восьми полей из `pSkillSrc`). Но «нули» — НЕВЕРНЫЙ
//       нейтральный вид этой структуры: её собственный конструктор `sub_93DFEF` — заглушка
//       компоновки (дизасм `0x93dfef: jmp 0xa09780`), а тело `sub_A09780@0xA09780`:
//           *this = 0; *(_DWORD *)(this+1) = -1; *(_WORD *)(this+5) = -1;
//           this[7] = this[8] = this[9] = -1; this[10] = 0; this[11] = 0;
//       (побайтово совпадает с серверным ctor `sub_49F000@0x49F000`, GameServer.c:64437-64447).
//       То есть «не задано» здесь = −1, а НЕ 0. Разница принципиальная: @1 = 0 выглядит как
//       законный charId и сервер такой пакет ПРОПУСТИТ (`>= 0`), а @1 = −1 он штатно отобьёт
//       с логом «iRequestUserIndex < 0». Поэтому без кэша заполняем ctor-значениями.
bool UAsdaNetSubsystem::SendMateRecallAnswer5237(int32 Code, int32 PartnerUserNum)
{
	if (!IsSendLinkUp()) { return false; }
	FAsdaPacketWriter W(12);
	// @0 код. Эталон из UI шлёт только 2 (отказ); 1 (согласие) в оригинале уходит НЕ этим пакетом, а
	//   переходом канала либо 5427 — но поле кода сервером читается, поэтому передаём как есть.
	W.U8(0, (uint8)FMath::Clamp(Code, 0, 255));
	const bool bHaveRec = (LastMateRecall.Num() >= 42);
	if (bHaveRec)
	{
		const uint8* R = LastMateRecall.GetData();
		W.I32(1, AsdaBytes::RdI32(&R[0]));    // @1 i32 charId инициатора  (5236@0)
		W.I16(5, AsdaBytes::RdI16(&R[4]));    // @5 i16 userNum инициатора (5236@4)
		W.U8(7, R[6]);    // эхо 5236@6  (мир/сервер-группа)
		W.U8(8, R[7]);    // эхо 5236@7  (карта инициатора)
		W.U8(9, R[8]);    // эхо 5236@8  (зона)
		W.U8(10, R[9]);    // эхо 5236@9  (канал инициатора)
		W.U8(11, R[41]);   // эхо 5236@41 (канал цели)
		const int32 RecUserNum = AsdaBytes::RdI16(&R[4]);
		if (PartnerUserNum >= 0 && PartnerUserNum != RecUserNum)
		{
			UE_LOG(LogAsdaNet, Warning,
				TEXT("→5237: PartnerUserNum=%d расходится с userNum инициатора из 5236 (%d) — ")
				TEXT("шлём значение ИЗ ПАКЕТА, сервер сверяет именно его (IsCheckUserNum)"),
				PartnerUserNum, RecUserNum);
		}
	}
	else
	{
		// Зова в кэше нет (перезаход, потеря 5236). Заполняем ЗНАЧЕНИЯМИ КОНСТРУКТОРА структуры
		//   (`sub_A09780@0xA09780` ≡ серверный `sub_49F000@0x49F000`), а не нулями — см. п.(3) выше:
		//   @1 = −1, @7/@8/@9 = 0xFF, @10 = 0, @11 = 0. userNum (@5) берём из параметра, если он есть:
		//   без него сервер отобьёт пакет по `IsCheckUserNum`.
		W.I32(1, -1);
		W.U8(7, 0xFF); W.U8(8, 0xFF); W.U8(9, 0xFF);
		W.U8(10, 0); W.U8(11, 0);
		W.I16(5, PartnerUserNum >= 0 ? (int32)(int16)PartnerUserNum : -1);
		UE_LOG(LogAsdaNet, Warning,
			TEXT("→5237: тела 5236 в кэше НЕТ — поля заполнены ЗНАЧЕНИЯМИ КОНСТРУКТОРА эталона ")
			TEXT("(@1=−1, @7..@9=0xFF, @10=@11=0), userNum=%d из параметра. Сервер такой ответ штатно ")
			TEXT("отобьёт логом «iRequestUserIndex < 0» — это верно: подтверждать зов, которого клиент ")
			TEXT("не получал, нельзя. Нули дали бы @1=0, который сервер принял бы как настоящий charId."),
			PartnerUserNum);
	}
	if (AsdaPacketLayoutBroken(W, (uint16)OP_SEND_RESPONSE_USER_TO_REQUEST_USER_MATE_RECALL_SKILL, TEXT("SendMateRecallAnswer5237"))) { return false; }
	const bool bOk = TrackSend(SendSecured(
		(uint16)OP_SEND_RESPONSE_USER_TO_REQUEST_USER_MATE_RECALL_SKILL, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log, TEXT("→5237 ответ на зов друга: код=%u charId=%d userNum=%d -> %s"),
		(uint32)W[0], AsdaBytes::RdI32(W.Data() + 1), AsdaBytes::RdI16(W.Data() + 5), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// C→S 5427 «телепорт по зову друга» (`netSendMaskedMd5Packet5427@0xA06C50`), 28 = 24 + 4.
//   payload(4): @0 i16 X плитки · @2 i16 Y/Z плитки — координаты ИНИЦИАТОРА из 5236 (@28 и @30).
//
//   ★★★ ОБРАТНОЙ ВЕТКИ S→C У 5427 НЕТ — разобран последний незакрытый thunk отчёта, `sub_939080`
//   (31.08.2026). В диспетчере клиента стоит `case 0x1533: sub_939080(); continue;`
//   (game.c:15295-15297) — вызов БЕЗ аргументов, тело пакета в него даже не передаётся.
//   Дизасм реального EXE: `0x939080: jmp 0xf548b0` — снова заглушка компоновки, а цель
//   `sub_F548B0@0xF548B0` ПУСТА (только пролог/эпилог, ни одной содержательной инструкции):
//       0xf548b0: push ebp / mov ebp,esp / sub esp,0x40 / push ebx / push esi / push edi
//       0xf548b9: pop edi / pop esi / pop ebx / mov esp,ebp / pop ebp / ret
//   В экспорте это же видно как `void sub_F548B0() { ; }` (game.c:561362-561365).
//   ⇒ Никаких координат этот обработчик никуда не кладёт: он не делает НИЧЕГО.
//   Второй метод (сервер): GameServer знает 5427 ТОЛЬКО как входящий (`case 0x1533` →
//   `CharSoulMateRecallPosition@0x52BAE0`, GameServer.c:19705-19708); отправки 5427 в GameServer.c
//   нет ни одной (единственное вхождение «5427» в файле — адрес не связанной `MoveSyncProcess
//   @0x5427D0`). Телепорт выполняет сам сервер сменой карты, а не ответным 5427.
//   Поэтому приёмного case для 5427 у нас нет и быть не должно. Уверенность HIGH.
bool UAsdaNetSubsystem::SendMateRecallPosition5427(int32 TileX, int32 TileZ)
{
	if (!IsSendLinkUp()) { return false; }
	// Сверка с кэшем 5236 — не «исправление», а диагностика: сервер валидирует плитку сам
	//   (`sub_4221D0`), при провале молча ставит 200/200 и пишет в свой лог.
	if (LastMateRecall.Num() >= 42)
	{
		const int32 RecX = AsdaBytes::RdI16(&LastMateRecall[28]);
		const int32 RecZ = AsdaBytes::RdI16(&LastMateRecall[30]);
		if (TileX != RecX || TileZ != RecZ)
		{
			UE_LOG(LogAsdaNet, Warning,
				TEXT("→5427: координаты (%d,%d) расходятся с зовом 5236 (%d,%d) — шлём переданные"),
				TileX, TileZ, RecX, RecZ);
		}
	}
	FAsdaPacketWriter W(4);
	W.I16(0, TileX);   // @0 i16 X плитки
	W.I16(2, TileZ);   // @2 i16 Y/Z плитки
	if (AsdaPacketLayoutBroken(W, (uint16)OP_CHAR_SOUL_MATE_RECALL_POSITION, TEXT("SendMateRecallPosition5427"))) { return false; }
	const bool bOk = TrackSend(SendSecured((uint16)OP_CHAR_SOUL_MATE_RECALL_POSITION, W.Data(), W.Num()));
	UE_LOG(LogAsdaNet, Log, TEXT("→5427 телепорт по зову друга: плитка (%d,%d) -> %s"),
		TileX, TileZ, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}

// ★ 26.10.2026 (savepoint) [ОРИГИНАЛ] C→S 5280 «сохранить местоположение». Разбор полей — у объявления.
//   Опкода нет в `NetOpcodes.h` (общий заголовок AsdaCore не трогаем ради одной константы) — номер здесь.
//   Эталон шлёт пакет без проверок (`dropPacket[0] > -1` проверяет вызывающий, `itemDropToGroundRequest`), поэтому и здесь
//   отсев только по длине/связи; X/Z ≤ 0 сервер отбивает сам (`sub_52A360`: «if( (sReq.iPosX <= 0) || (sReq.iPosZ <= 0) )»).
static constexpr uint16 kOpSaveVillagePos5280 = 5280;   // C→S `case 0x14A0` GameServer → sub_52A360
bool UAsdaNetSubsystem::SendSaveVillagePos5280(int32 NpcId, int32 TileX, int32 TileZ)
{
	if (!IsSendLinkUp()) { return false; }
	FAsdaPacketWriter W(6);
	W.I16(0, NpcId);   // @0 i16 npcId (`self[48212]` = g_pNpcWnd+0x178A8; сервер не читает)
	W.I16(2, TileX);   // @2 i16 тайл X своего персонажа (`helper_scaleIfNonzero(pos.x)`)
	W.I16(4, TileZ);   // @4 i16 тайл Z своего персонажа (`helper_scaleIfNonzero(pos.z)`)
	if (AsdaPacketLayoutBroken(W, kOpSaveVillagePos5280, TEXT("SendSaveVillagePos5280"))) { return false; }
	// Конверт: MD5(6 б) + тело, без секрета; пад — эталонный a6 = 1.
	const bool bOk = TrackSend(SendMd5Signed(kOpSaveVillagePos5280, W.Data(), W.Num(), /*bPad=*/true));
	UE_LOG(LogAsdaNet, Log, TEXT("→5280 сохранить местоположение: npcId %d, тайл (%d,%d) -> %s"),
		NpcId, TileX, TileZ, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	return bOk;
}
