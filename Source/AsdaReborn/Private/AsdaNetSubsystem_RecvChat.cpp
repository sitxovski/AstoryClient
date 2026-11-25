// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNetSubsystem_RecvChat.cpp — часть `AsdaNetSubsystem.cpp`: приёмник пакетов: чат: обычный/сообщение/шёпот/системный/GM-пати/кланов, серверные уведомления.
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

// ★ 19.10.2026 (ревизия безопасности журналов, М9): в Shipping тексты и тела чата (свои и ЧУЖИЕ: переписка других игроков,
//   шёпот) в журнал не пишутся — только опкод, длина, отправитель. Вне Shipping — как раньше (текст для разбора порта).
static FString AsdaChatTextForLog(const FString& Text)
{
#if UE_BUILD_SHIPPING
	return FString::Printf(TEXT("<текст скрыт: %d симв.>"), Text.Len());
#else
	return Text;
#endif
}

// ★★★ [ОРИГИНАЛ] ЦВЕТ СТРОКИ ЧАТА ОДЕРЖИМОГО. Эталон `Recv_ChatWhisper@0xBA7530`
//   (AsdaStory_game.c:223941): при `LOBYTE(pkt[7]) == 2` (SoulState отправителя, тело +28) строка
//   уходит НЕ через `Chat_AddChatLine(категория 5)`, а через `chat_addLine(вкладка, …, this[615])`,
//   где `this[615]` = −15606785 = 0xFF11DBFF (голубой; задаётся в `CChat_Ctor@0xB9BB60`:223832).
//   Ключ отката, потому что это визуально заметное отличие от «как было в порту».
//   ⚠ В ОБЫЧНОМ чате (5085 `Recv_ChatNormal`:223649/:223739) оригинал этот же цвет ВЫЧИСЛЯЕТ, но
//   `Chat_AddChatLine` для категории 0 безусловно перетирает переданный a4 на `chanColorDefault`
//   (AsdaStory_game.c:227596-227608) ⇒ ветка МЁРТВАЯ, на вид не влияет. Поэтому в 5085/5087
//   НИЧЕГО не меняем (закон never-invent-only-original).
static ASDA_DEV_CVAR(int32, CVarPossessChatColor,
	TEXT("asda.Possess.ChatColor"), 1,
	TEXT("1 = красить строку шёпота от ОДЕРЖИМОГО отправителя в эталонный 0xFF11DBFF (как оригинал); 0 = обычный цвет канала."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 05.11.2026 — ВХОДЯЩИЙ ШЁПОТ, ГДЕ ОТПРАВИТЕЛЬ — САМ ИГРОК (шёпот самому себе). Эталон
//   `Recv_ChatWhisper@0xBA7530` (network.c:10596-10631), msgType 1, flag 0: если `self->userIdx == *(int*)(pkt+2)`
//   (charId тела +2 = свой персонаж), строка собирается из loc 5431 (`sprintf(lineBuf, loc5431, имя)`) + «[loc5348] текст»,
//   и ВЕТКА НЕ ДЕЛАЕТ `memcpy(chat+2552, имя, 0x14)` — «R» (CHAT ANSWER) продолжает отвечать прежнему шептавшему.
//   Порт до этой правки шёл общей веткой: «[Имя]: …» и запоминал себя адресатом для «R».
//   Свой charId — `SelectedCharId` (блок 1017; им же порт пишет свой charId в 4120/6300/6746).
//   1 — как эталон; 0 — прежнее поведение порта (общая ветка входящего шёпота).
static ASDA_DEV_CVAR(int32, CVarChatWhisperSelfBranch,
	TEXT("asda.Chat.WhisperSelfBranch"), 1,
	TEXT("[ОРИГИНАЛ] 5089 от самого себя (charId тела = свой): 1 — строка из loc 5431 и отправитель НЕ запоминается для «R» (как эталон); 0 — прежнее: «[Имя]: …» и запоминание."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 05.11.2026 — 5089 (шёпот, msgType 1): WORD тела +6 = userNum ПОЛУЧАТЕЛЯ; чужой — пакет не показывается.
//   Клиент: `Recv_ChatWhisper@0xBA7530` сравнивает `SHIWORD(pkt[1])` (тело +6) с `LOWORD(self->effectSlots[111].field_5B8)` —
//   это userNum своего персонажа (пишет `Recv_SpawnSelf@0xF3E630`, 4003, network.c:40563; u16 char+190188), в порту `SelfUserNum`.
//   Сервер: `sub_547EB0@0x547EB0` (GameServer.c:125331, пересылка 3105 → 5089) пишет в +6 номер `v10` и шлёт пакет ему же
//   (`sub_4C28F0(…, 5089, …, v18 = v10, …)`) ⇒ на живом сервере проверка проходит всегда; расходится только при устаревшем
//   userNum (пакет, посланный до повторного 4003).
//   Несовпадение у оригинала: flag 0 (входящий) — пакет молча отбрасывается (метка LABEL_33); flag ≠ 0 (эхо своего) — строку
//   не собирает, но `Chat_AddChatLine` и облачко всё равно зовёт с ПУСТЫМ lineBuf (метка LABEL_11) — пустая строка в ленте.
//   [ИСПРАВЛЕНИЕ] для flag ≠ 0: пустую строку и облачко не показываем — отбрасываем, как flag 0.
//   [ПОРТ]: до первого 4003 (`bSelfUserNumKnown` = false) свой номер неизвестен — проверку не делаем.
//   1 — проверять (как эталон); 0 — прежнее поведение порта (WORD +6 не читается).
static ASDA_DEV_CVAR(int32, CVarChatWhisperUserNumCheck,
	TEXT("asda.Chat.WhisperUserNumCheck"), 1,
	TEXT("[ОРИГИНАЛ] 5089 шёпот: 1 — WORD тела +6 (userNum получателя) ≠ свой SelfUserNum ⇒ пакет отбрасывается (как эталон); 0 — прежнее: не проверять."),
	ECVF_Default);

// ★★★ 05.11.2026 — ЧАТ С ДРУГОМ (соулмейт): 5088 вид 1 → 5089 flag 1, категория 3 (слот 8, loc 5349 «Чат с Другом»).
//   Сервер: RelayServer `sub_4014CB@0x4014CB` (CChattingManager::PrivateChatting, 3105 → 3106) шлёт ПОЛУЧАТЕЛЮ charId и имя
//   ОТПРАВИТЕЛЯ, а ОТПРАВИТЕЛЮ — эхо: charId = его собственный, имя = ПАРТНЁР. Клиент `Recv_ChatWhisper@0xBA7530`
//   (network.c:10568-10577): charId = свой ⇒ строка «[своё имя]: текст», иначе «[имя из пакета]: текст».
//   Порт брал имя из пакета всегда ⇒ своё сообщение подписывалось именем партнёра: «не видно, кто отправил».
//   `asda.Chat.SoulmateSelfName` [ОРИГИНАЛ]: 1 — своё эхо подписано своим именем (как эталон); 0 — прежнее (имя партнёра).
//   `asda.Chat.SoulmateToFormat` [ИСПРАВЛЕНИЕ] (требование владельца 05.11 — видно и «кому»): 1 — своё эхо строкой
//   «loc 5431: текст» («Игроку Партнёр: …» / «To [Партнёр]: …», как подтверждение шёпота); имеет приоритет над SelfName.
//   Входящее от партнёра при `asda.Chat.SoulmateToFormat` 1 — «От Партнёр: текст» / «From [Партнёр]: текст» (loc `kLocSoulmateFrom`).
// ★★★ [ИСПРАВЛЕНИЕ] 06.11.2026 (требование владельца) — РАНГ «[#N]» ТОЛЬКО В ОКНЕ ЧАТА, В ОБЛАЧКЕ НАД ПЕРСОНАЖЕМ ЕГО НЕТ. Оригинал ранг в облачко
//   кладёт: 5085 — `&Str` с префиксом (network.c:10459-10475), 5087 — `numberedText` (network.c:10981, 11004-11006); других каналов с рангом нет
//   (`[#%d]` в эталоне — только эти два обработчика). Строка чата, заливка фона 5085 и разметка 5087 не меняются.
//   1 — облачко без ранга; 0 — как оригинал.
static ASDA_DEV_CVAR(int32, CVarChatBalloonRankHide,
	TEXT("asda.Chat.BalloonRankHide"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Ранг [#N] в облачке чата: 1 — только в окне чата, облачко без ранга; 0 — как оригинал, ранг и в облачке."),
	ECVF_Default);

static constexpr int32 kLocSoulmateFrom = 991200;   // [ПОРТ] loc «От %s» / «From [%s]» (ведомости gametext_overrides.json + loc/en/GameText.json)
static ASDA_DEV_CVAR(int32, CVarChatSoulmateSelfName,
	TEXT("asda.Chat.SoulmateSelfName"), 1,
	TEXT("[ОРИГИНАЛ] Чат с Другом: 1 — своё сообщение подписано своим именем «[Я]: …» (эталон); 0 — прежнее: именем партнёра."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarChatSoulmateToFormat,
	TEXT("asda.Chat.SoulmateToFormat"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Чат с Другом: 1 — своё «Игроку Партнёр: …» (loc 5431), входящее «От Партнёр: …» (loc 991200); 0 — «[Имя]: …», своё — по ключу asda.Chat.SoulmateSelfName."),
	ECVF_Default);

// Подстановка двух аргументов в ФОРМАТ-СТРОКУ ИЗ ЛОКАЛИЗАЦИИ (эталон зовёт sprintf с loc-строкой,
//   у нас формат не литерал → FString::Printf неприменим). Заменяем первые два «%s» по порядку;
//   если спецификаторов меньше — лишние аргументы просто не подставляются, как и у sprintf.
static FString AsdaFormatTwo(const FString& Fmt, const FString& A, const FString& B)
{
	if (Fmt.IsEmpty()) { return FString(); }
	FString Out = Fmt;
	int32 Pos = INDEX_NONE;
	if (Out.FindChar(TEXT('%'), Pos))
	{
		const int32 I1 = Out.Find(TEXT("%s"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
		if (I1 != INDEX_NONE)
		{
			Out = Out.Left(I1) + A + Out.Mid(I1 + 2);
			const int32 I2 = Out.Find(TEXT("%s"), ESearchCase::CaseSensitive, ESearchDir::FromStart, I1 + A.Len());
			if (I2 != INDEX_NONE) { Out = Out.Left(I2) + B + Out.Mid(I2 + 2); }
		}
	}
	return Out;
}

// ★ R2 (10.11.2026): приёмник области «Chat» — чат: обычный/сообщение/шёпот/системный/GM-пати/кланов, серверные уведомления.
//   Группы `case` перенесены из `UAsdaNetSubsystem::HandlePacket` ДОСЛОВНО и в исходном порядке; `bHandled` = false только
//   в служебной ветке `default:` в конце — опкод не из этой области, диспетчер идёт дальше.
void UAsdaNetSubsystem::HandlePacket_Chat(const Asda::ParsedPacket& Pkt, bool& bHandled)
{
	bHandled = true;
	switch ((NetOpcode)Pkt.Opcode)
	{
	case OP_CHAT_NORMAL:                 // 5085 0x13DD — Recv_ChatNormal @0xBA6AD0, категория 0
	{
		// payload: WORD userIdx@0, char[20] имя@+2, DWORD rankLevel@+22, текст@+26 (ASCIIZ)
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 27) { UE_LOG(LogAsdaNet, Warning, TEXT("5085 тело мало (%d)"), (int32)B.size()); break; }
		FAsdaChatLineIn L;
		L.SrcOpcode = 5085;
		L.bSenderAuthored = true;   // автор строки — игрок SenderName (игнор-лист, Д27 п.2)
		L.Category = 0;
		L.SenderUserIdx = (int32)AsdaBytes::RdU16(&B[0]);
		L.SenderName = FAsdaLoginData::DecodeCp1251(B.data() + 2, 20);
		int32 Rank = AsdaBytes::RdI32(B.data() + 22);   // ★ N6: `AsdaBytes`
		const FString Body = FAsdaLoginData::DecodeWireText(B.data() + 26, (int32)B.size() - 26);   // [ПОРТ] 08.10: тело — гибрид cp1251/UTF-8
		FString Text = Body;
		// Ранговая ветка (network.c:10442-10475): 1 ≤ rank ≤ 100 → «[#N] » перед текстом,
		//   а ранговый цвет уходит в ЗАЛИВКУ ФОНА строки (a8 + флаг a9=1), НЕ в цвет текста.
		//   Ширина префикса (a7 = textFontMeasureLineWidth("[Имя]: ")) пишется в поле строки
		//   +3104, которое во всём срезе ни разу не читается ⇒ на вид не влияет.
		if (Rank >= 1 && Rank <= 100)
		{
			L.RankLevel = Rank;
			L.bRowBg = true;
			L.RowBg = UAsdaChatWindowSubsystem::RankColor(Rank);
			Text = FString::Printf(TEXT("[#%d] %s"), Rank, *Body);
		}
		L.Text = FString::Printf(TEXT("[%s]: %s"), *L.SenderName, *Text);
		// ★ Облачко: entityName_DrawOverhead(self, ent(userIdx), −16777216, &Str, a6=0, вариант 0)
		//   (network.c:10475). Цвет ТЕКСТА жёстко 0xFF000000, облачко белое TALKBOXW (JUI 9010).
		//   Текст = `&Str` — сообщение С ранговым префиксом «[#N] », но БЕЗ «[Имя]: ».
		L.BalloonVariant = 0;
		L.BalloonText = CVarChatBalloonRankHide.GetValueOnGameThread() != 0 ? Body : Text;   // [ИСПРАВЛЕНИЕ] шапка ключа `asda.Chat.BalloonRankHide`
		L.BalloonColor = FColor(0x00, 0x00, 0x00, 0xFF);
		L.BalloonFontFlag = 0;
		UE_LOG(LogAsdaNet, Log, TEXT("5085 чат: [%s] ранг=%d «%s»"), *L.SenderName, Rank, *AsdaChatTextForLog(Body));   // ★ 19.10 М9
		OnChatLine.Broadcast(L);
		break;
	}
	case OP_CHAT_MESSAGE:                // 5087 0x13DF — Recv_ChatMessage @0xBA88E0, категория 1
	{
		// payload: WORD userIdx@0, BYTE канал@+2 (0/1/2), char[20] имя@+3, DWORD rank@+23, текст@+27
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 28) { UE_LOG(LogAsdaNet, Warning, TEXT("5087 тело мало (%d)"), (int32)B.size()); break; }
		// ★ Сырые байты целиком: байт +2 — ОБЛАСТЬ ОХВАТА (0 обычный / 1 «кан.» / 2 «Сервер»),
		//   а НЕ чат-канал. Разбор — в комментарии к SendChatNormal5086.
		UE_LOG(LogAsdaNet, Log, TEXT("5087 приём: тело[%d] %s | байт+2 (охват) = %u"),
			(int32)B.size(), *HexDumpBody(B.data(), (int32)B.size()), B[2]);
		FAsdaChatLineIn L;
		L.SrcOpcode = 5087;
		L.bSenderAuthored = true;   // глобальный/«кан.»/«Сервер» — пишет игрок SenderName
		L.Category = 1;
		L.SenderUserIdx = (int32)AsdaBytes::RdU16(&B[0]);
		const uint8 Chan = B[2];
		L.SenderName = FAsdaLoginData::DecodeCp1251(B.data() + 3, 20);
		int32 Rank = AsdaBytes::RdI32(B.data() + 23);   // ★ N6: `AsdaBytes`
		FString Body = FAsdaLoginData::DecodeWireText(B.data() + 27, (int32)B.size() - 27);   // [ПОРТ] 08.10: тело — гибрид cp1251/UTF-8
		// ★ Здесь ранг делается ИНЛАЙН-РАЗМЕТКОЙ (в отличие от 5085): эталон
		//   sprintf(msgText, "<FC=%08x>[#%d]</F> %s ", -1, rank, text) — цвет жёстко белый.
		// ★ ОБЛАЧКО берёт ДРУГУЮ строку: эталон готовит ДВЕ (network.c:10979-10983) —
		//   msgText с разметкой «<FC=ffffffff>[#N]</F> …» (в чат-лог) и numberedText «[#N] …»
		//   (в облачко, БЕЗ разметки); при ранге вне 1..100 в облачко идёт msgText без префикса.
		FString BalloonBody = Body;
		if (Rank >= 1 && Rank <= 100)
		{
			L.RankLevel = Rank;
			if (CVarChatBalloonRankHide.GetValueOnGameThread() == 0) { BalloonBody = FString::Printf(TEXT("[#%d] %s "), Rank, *Body); }   // [ИСПРАВЛЕНИЕ] шапка ключа `asda.Chat.BalloonRankHide`
			Body = FString::Printf(TEXT("<FC=ffffffff>[#%d]</F> %s "), Rank, *Body);
		}
		// Канал 0 → «[Имя]: текст»; каналы 1 и 2 → «[loc]Имя: текст» (loc 1187 и 1109).
		//   При канале > 2 эталон НЕ формирует строку вовсе (sprintf не вызывается) — не выводим.
		if (Chan == 0)
		{
			L.Text = FString::Printf(TEXT("[%s]: %s"), *L.SenderName, *Body);
		}
		else if (Chan == 1 || Chan == 2)
		{
			UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
			const FString Tag = Txt ? Txt->GetTextById(Chan == 1 ? 1187 : 1109) : FString();
			L.Text = FString::Printf(TEXT("[%s]%s: %s"), *Tag, *L.SenderName, *Body);
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("5087: канал %u > 2 — эталон строку не формирует, пропускаем"), Chan);
			break;
		}
		// ★ Облачко: entityName_DrawOverhead(self, speaker(userIdx), chatLineColor, msgText|numberedText,
		//   a6=1, вариант 0) — network.c:11004/11006. Цвет = ПОЛЕ CChat ДО подмены цветом слота
		//   (CChat_Ctor chat.c:89-91): +623 = −65536 (0xFFFF0000) при охвате 0,
		//   +624/+625 = −921849 (0xFFF1EF07) при охвате 1 и 2.
		L.BalloonVariant = 0;
		L.BalloonText = BalloonBody;
		L.BalloonColor = (Chan == 0) ? FColor(0xFF, 0x00, 0x00, 0xFF) : FColor(0xF1, 0xEF, 0x07, 0xFF);
		L.BalloonFontFlag = 1;
		UE_LOG(LogAsdaNet, Log, TEXT("5087 чат: канал=%u [%s] ранг=%d"), Chan, *L.SenderName, Rank);
		OnChatLine.Broadcast(L);
		break;
	}
	case OP_CHAT_WHISPER:                // 5089 0x13E1 — Recv_ChatWhisper @0xBA7530
	{
		// payload: BYTE flag@0, BYTE msgType@+1, DWORD charId@+2, WORD@+6,
		//          char[20] имя@+8, BYTE SoulState отправителя@+28 (0/1/2; эталон `LOBYTE(pkt[7])`),
		//          текст@+29
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 30) { UE_LOG(LogAsdaNet, Warning, TEXT("5089 тело мало (%d)"), (int32)B.size()); break; }
		const uint8 Flag = B[0];
		const uint8 MsgType = B[1];
		int32 WhisperCharId = AsdaBytes::RdI32(B.data() + 2);   // charId собеседника (★ N6: `AsdaBytes`)
		const FString Name = FAsdaLoginData::DecodeCp1251(B.data() + 8, 20);
		const FString Body = FAsdaLoginData::DecodeWireText(B.data() + 29, (int32)B.size() - 29);   // [ПОРТ] 08.10: тело — гибрид cp1251/UTF-8
		UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		// ★★★ [ОРИГИНАЛ] 05.11.2026 — ПРОВЕРКА АДРЕСАТА (WORD тела +6). Шапка ключа `asda.Chat.WhisperUserNumCheck`.
		const uint16 WhisperToUserNum = (uint16)AsdaBytes::RdU16(B.data() + 6);   // userNum получателя, который вписал сервер
		if (MsgType == 1 && bSelfUserNumKnown && WhisperToUserNum != SelfUserNum
			&& CVarChatWhisperUserNumCheck.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("5089: тело+6 userNum %u ≠ свой %u — шёпот не мне, отброшен (эталон network.c:10567/10596; flag=%u)"),
				WhisperToUserNum, SelfUserNum, Flag);
			break;
		}
		FAsdaChatLineIn L;
		L.SrcOpcode = 5089;
		L.SenderName = Name;
		// Ветвление 1:1 (network.c:10494-10765).
		if (MsgType == 1 && Flag != 0)
		{
			// Эхо/подтверждение отправленного шёпота: категория 3 (слот 8 «Чат с Другом»).
			// ★ Автора-собеседника у этой строки НЕТ — её написал САМ игрок, поэтому игнор-лист
			//   её не глушит (bSenderAuthored остаётся false).
			L.Category = 3;
			L.Text = FString::Printf(TEXT("[%s]: %s"), *Name, *Body);
			// ★★★ 05.11.2026 — СВОЁ СООБЩЕНИЕ В ЧАТЕ С ДРУГОМ (соулмейт, 5088 вид 1). Шапка ключей `asda.Chat.SoulmateSelfName`
			//   и `asda.Chat.SoulmateToFormat` выше. Эхо отправителю (RelayServer `sub_4014CB`) несёт charId = СВОЙ, имя = ПАРТНЁР.
			const bool bOwnEcho = SelectedCharId != 0 && WhisperCharId == SelectedCharId;   // эхо своего сообщения
			if (CVarChatSoulmateToFormat.GetValueOnGameThread() != 0)
			{
				// [ИСПРАВЛЕНИЕ] своё: «Игроку Партнёр: текст» / «To [Партнёр]: текст» — loc 5431, формат подтверждения шёпота;
				//   входящее: «От Партнёр: текст» / «From [Партнёр]: текст» — loc 991200 (новая строка порта, пара к 5431).
				//   Нет строки в таблице — остаётся «[Партнёр]: текст».
				FString Head = Txt ? Txt->FormatNotice(bOwnEcho ? 5431 : kLocSoulmateFrom, 0, 1) : FString();
				const int32 PctS = Head.Find(TEXT("%s"), ESearchCase::CaseSensitive);
				if (PctS != INDEX_NONE) { Head = Head.Left(PctS) + Name + Head.Mid(PctS + 2); }
				if (!Head.IsEmpty()) { L.Text = FString::Printf(TEXT("%s: %s"), *Head, *Body); }
			}
			else if (bOwnEcho && CVarChatSoulmateSelfName.GetValueOnGameThread() != 0)
			{
				// [ОРИГИНАЛ] network.c:10568-10577: `self->userIdx == charId` ⇒ `sprintf("[%s]: %s", self->name, msg)`.
				const FString Self = GetSelfCharNameForOperatorCheck();   // общая точка «своё имя»
				if (!Self.IsEmpty()) { L.Text = FString::Printf(TEXT("[%s]: %s"), *Self, *Body); L.SenderName = Self; }
			}
			// ★ Облачко (network.c:10591, метка LABEL_11): вариант 1 = TALKBOXB (JUI 9013,
			//   скин TalkBox2, золотая подложка FFD21B), цвет текста −14540254 = 0xFF222222, a6=0.
			//   Цель ищется CharManager_FindByCharId(pkt+2) — сам собеседник, НЕ отправитель.
			L.BalloonVariant = 1;
			L.BalloonText = Body;
			L.BalloonColor = FColor(0x22, 0x22, 0x22, 0xFF);
			L.BalloonFontFlag = 0;
			L.SenderCharId = WhisperCharId;
		}
		else if (MsgType == 1)
		{
			// Входящий шёпот: категория 5 (слот 7 «Шепот»), строка «[loc5348] текст».
			L.bSenderAuthored = true;   // ★ пишет собеседник ⇒ игнор-лист глушит (Д27 п.2)
			L.Category = 5;
			const FString Tag = Txt ? Txt->GetTextById(5348) : FString();
			L.Text = FString::Printf(TEXT("[%s]: [%s] %s"), *Name, *Tag, *Body);
			// ★ Облачко (network.c:10630, метка LABEL_28): тот же вариант 1 и цвет, но a6=1.
			L.BalloonVariant = 1;
			L.BalloonText = Body;
			L.BalloonColor = FColor(0x22, 0x22, 0x22, 0xFF);
			L.BalloonFontFlag = 1;
			L.SenderCharId = WhisperCharId;
			// ★★★ [ОРИГИНАЛ] 05.11.2026 — отправитель = САМ игрок (шапка ключа `asda.Chat.WhisperSelfBranch`).
			//   Эталон: `sprintf(lineBuf, loc5431, имя); strcat(lineBuf, "[loc5348] текст")`, без `memcpy` адресата «R».
			//   `bSenderAuthored = false`: строку написал сам игрок — её не глушит игнор-лист и окно чата НЕ запоминает
			//   её отправителя для «R» (приёмник в AsdaChatWindowSubsystem.cpp берёт только bSenderAuthored).
			//   Облачко и цвет одержимого — те же, что у общей ветки (эталон идёт в ту же метку LABEL_28; SoulState
			//   берёт свой — при отправителе-себе это тот же байт +28).
			const bool bFromSelf = SelectedCharId != 0 && WhisperCharId == SelectedCharId
				&& CVarChatWhisperSelfBranch.GetValueOnGameThread() != 0;   // charId тела = свой персонаж
			if (bFromSelf)
			{
				L.bSenderAuthored = false;
				FString Head = Txt ? Txt->FormatNotice(5431, 0, 1) : FString();   // loc 5431 с подстановкой маркеров
				// Эталон подставляет имя printf-форматом `%s` строки 5431; маркеров имени у неё нет.
				const int32 PctS = Head.Find(TEXT("%s"), ESearchCase::CaseSensitive);
				if (PctS != INDEX_NONE) { Head = Head.Left(PctS) + Name + Head.Mid(PctS + 2); }
				L.Text = FString::Printf(TEXT("%s[%s] %s"), *Head, *Tag, *Body);
				UE_LOG(LogAsdaNet, Log, TEXT("5089: шёпот от самого себя (charId %d) — строка loc 5431, адресат «R» не меняется"), WhisperCharId);
			}
			// ★★★ [ОРИГИНАЛ] БАЙТ +28 = SoulState ОТПРАВИТЕЛЯ. В порту он до сих пор не читался
			//   (тело: flag@0, msgType@1, charId@2, имя@8..27, ТЕКСТ с +29 — байт 28 пропускался).
			//   Эталон: `if (LOBYTE(pkt[7]) == 2) { chat_addLine(вкладка, lineBuf, this[615]); return; }`
			//   (AsdaStory_game.c:223939-223944) — то есть у одержимого отправителя строка идёт
			//   ДРУГИМ путём: без цвета канала, ЦВЕТОМ 0xFF11DBFF, и БЕЗ облачка над головой
			//   (эталон делает `return` до `entityName_DrawOverhead`).
			//   Порт: `chat_addLine` соответствует категории 6 (colorType 40) — единственной, где
			//   `Chat_AddChatLine` НЕ перетирает переданный цвет (`CategoryToSlot` → −1 ⇒ берётся
			//   `Fallback`), и которая, как и прямая запись во вкладку, видна на любой вкладке.
			if (B.size() > 28 && B[28] == 2 && CVarPossessChatColor.GetValueOnGameThread() != 0)
			{
				L.Category = 6;
				L.Fallback = FColor(0x11, 0xDB, 0xFF, 0xFF);   // эталон CChat+615 = −15606785
				L.BalloonVariant = -1;                          // эталон до облачка не доходит
				UE_LOG(LogAsdaNet, Log, TEXT("5089: отправитель '%s' ОДЕРЖИМ (тело+28=2) — строка цветом 0xFF11DBFF, без облачка"), *Name);
			}
		}
		else if (MsgType == 0 || MsgType == 2 || MsgType == 3)
		{
			// ★ Служебные уведомления (bSenderAuthored = false): их пишет СЕРВЕР в ответ на
			//   действие самого игрока, имя в них — лишь упоминание. Игнор-лист их не глушит.
			// Служебные уведомления шёпота: loc 20040 / 20041 / 20042. В эталоне они идут
			//   через chat_addLine с номером вкладки, а не через Chat_AddChatLine.
			//   TODO-ЭТАП 4: маршрутизация по вкладкам. Пока — в общий канал (категория 0).
			const int32 LocId = (MsgType == 0) ? 20040 : (MsgType == 2 ? 20041 : 20042);
			const FString Fmt = Txt ? Txt->GetTextById(LocId) : FString();
			L.Category = 0;
			L.Text = Fmt.IsEmpty() ? FString::Printf(TEXT("%s %s"), *Name, *Body)
			                       : FString::Printf(TEXT("%s: %s"), *Fmt, *Name);
		}
		else if (MsgType == 4)
		{
			// loc 10223 в тултип-нотис, строки в чат НЕТ.
			UE_LOG(LogAsdaNet, Log, TEXT("5089: msgType 4 — нотис loc 10223, строки в чат нет"));
			break;
		}
		else
		{
			UE_LOG(LogAsdaNet, Warning, TEXT("5089: неизвестный msgType %u — пропускаем"), MsgType);
			break;
		}
		UE_LOG(LogAsdaNet, Log, TEXT("5089 шёпот: flag=%u тип=%u [%s]"), Flag, MsgType, *Name);
		OnChatLine.Broadcast(L);
		break;
	}
	case OP_SYSTEM_MESSAGE:              // 4121 0x1019 — Recv_SystemMessage @0xBA82D0, категория 4
	{
		// payload: WORD@0, DWORD charId@+2, WORD@+6, char[20] имя@+8, текст@+28
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 29) { UE_LOG(LogAsdaNet, Warning, TEXT("4121 тело мало (%d)"), (int32)B.size()); break; }
		FAsdaChatLineIn L;
		L.SrcOpcode = 4121;
		L.bSenderAuthored = true;   // системный/клановый канал — пишет игрок SenderName
		L.Category = 4;                                     // → слот 6 «Чат клана»
		L.SenderName = FAsdaLoginData::DecodeCp1251(B.data() + 8, 20);
		int32 SysCharId = AsdaBytes::RdI32(B.data() + 2);   // ★ N6: `AsdaBytes`
		const FString Body = FAsdaLoginData::DecodeWireText(B.data() + 28, (int32)B.size() - 28);   // [ПОРТ] 08.10: тело — гибрид cp1251/UTF-8
		L.Text = FString::Printf(TEXT("%s: %s"), *L.SenderName, *Body);   // эталонный формат «%s: %s»
		// ★ Облачко (network.c:10808): цель = CharManager_FindByCharId(sender+1 → charId@+2),
		//   вариант 0 (TALKBOXW), цвет текста −16748079 = 0xFF0071D1, a6=0. Эталон под гардом `if (pChar)`.
		L.BalloonVariant = 0;
		L.BalloonText = Body;
		L.BalloonColor = FColor(0x00, 0x71, 0xD1, 0xFF);
		L.BalloonFontFlag = 0;
		L.SenderCharId = SysCharId;
		UE_LOG(LogAsdaNet, Log, TEXT("4121 сис-чат: [%s] «%s»"), *L.SenderName, *Body);
		OnChatLine.Broadcast(L);
		break;
	}
	case OP_GM_PARTY_CHAT_RES:           // 5106 0x13F2 — Recv_GM_PARTY_CHAT_RES @0xF3A4B0, категория 2
	{
		// payload (эталон qmemcpy(sender, payload, 0x19) + текст с +25):
		//   WORD@0, char[20] имя@+2 (в эталоне `&sender[1]`), … , текст@+25.
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 26) { UE_LOG(LogAsdaNet, Warning, TEXT("5106 тело мало (%d)"), (int32)B.size()); break; }
		FAsdaChatLineIn L;
		L.SrcOpcode = 5106;
		L.bSenderAuthored = true;   // чат отряда — пишет игрок SenderName
		L.Category = 2;                                     // → слот 5 «Чат отряда»
		L.SenderName = FAsdaLoginData::DecodeCp1251(B.data() + 2, 20);
		const FString Body = FAsdaLoginData::DecodeWireText(B.data() + 25, (int32)B.size() - 25);   // [ПОРТ] 08.10: тело — гибрид cp1251/UTF-8
		L.Text = FString::Printf(TEXT("[%s]: %s"), *L.SenderName, *Body);
		L.Fallback = FColor(0xAB, 0xFF, 0x79, 0xFF);        // −5505159 (у категории 2 есть слот 5 — тот же цвет)
		// ★ Облачко (network.c:39280): цель = CharManager_FindByName(&sender[1]);
		//   вариант 0 (TALKBOXW), цвет текста −15829504 = 0xFF0E7600, a6=0. Гарда `if (pChar)` в эталоне
		//   НЕТ — entityName_DrawOverhead сам отсекает пустой указатель (`if (g_bShowEntityNames && a3)`).
		L.BalloonVariant = 0;
		L.BalloonText = Body;
		L.BalloonColor = FColor(0x0E, 0x76, 0x00, 0xFF);
		L.BalloonFontFlag = 0;
		UE_LOG(LogAsdaNet, Log, TEXT("5106 чат отряда: [%s] «%s»"), *L.SenderName, *AsdaChatTextForLog(Body));   // ★ 19.10 М9
		OnChatLine.Broadcast(L);
		break;
	}
	case OP_MOB_CHAT_BUBBLE:             // 8008 — recvCharChatBubble @0xBA7FE0 (облачко над мобом)
	{
		// Тело (10 б заголовка + строка): renderId = SHIWORD(header[0]) = WORD@+2; строка с +10.
		//   Клиент ищет ПЕРВОЕ двоеточие и берёт хвост начиная с (colon + 2) — то есть режет «Имя: ».
		//   Длина копирования у эталона `textLen - colonPos + 2` (переснимает лишние 2 байта за
		//   концом строки — буфер заранее обнулён, поэтому на видимый результат это не влияет).
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 11) { UE_LOG(LogAsdaNet, Warning, TEXT("8008 тело мало (%d)"), (int32)B.size()); break; }
		// ★ Аудит №3 (знаковость): эталон `recvCharChatBubble@0xBA7FE0` кладёт первые 4 байта тела в
		//   `header[0]` и ищет цель как `Entity_FindByRenderId_thunk(mgr, SHIWORD(header[0]))` —
		//   SHIWORD = ЗНАКОВОЕ старшее слово, т.е. renderId @2 читается со знаком.
		const int32 RenderId = AsdaBytes::RdI16(&B[2]);
		const FString Raw = FAsdaLoginData::DecodeCp1251(B.data() + 10, (int32)B.size() - 10);
		int32 Colon = INDEX_NONE;
		Raw.FindChar(TEXT(':'), Colon);
		if (Colon <= 0)
		{
			// Эталон: `if (colonPos > 0)` — без двоеточия облачко НЕ показывается вовсе.
			UE_LOG(LogAsdaNet, Verbose, TEXT("8008: в строке «%s» нет двоеточия — эталон облачко не рисует"), *Raw);
			break;
		}
		const FString Text = Raw.Mid(Colon + 2);
		UE_LOG(LogAsdaNet, Log, TEXT("8008 облачко моба: rid=%d «%s»"), RenderId, *Text);
		OnMobChatBubble.Broadcast(RenderId, Text);
		break;
	}
	// ★ 30.10.2026 НАВЫКИ МОБОВ [ОРИГИНАЛ] диспетчер network.c:1224 (8011), :1259 (8012), :1263 (8013) — без гейта состояния игры.
	//   Владелец автомата — `UAsdaMobSkillSubsystem` (разбор `Reports/mob_skills/FLOW.md`). До 30.10 опкоды уходили в аудит
	//   «хендлера нет»: сервер шлёт их (журналы игроков 19–25.10: 8012 — 662, 8011 — 7, 8013 — 46), а мобы стояли без навыков.
	case OP_MOB_SKILL_CAST:              // 8011 netApplyCharActionState@0xF33F00
	case OP_MOB_SKILL_RESULT:            // 8012 NetHandler_CharActionUpdate@0xF33FC0
	case OP_MOB_HP_RESTORE:              // 8013 Entity_ApplyHpRestore@0xF34500
	{
		UAsdaMobSkillSubsystem* MS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMobSkillSubsystem>() : nullptr;
		if (!MS) { break; }
		const uint8* B = Pkt.Body.empty() ? nullptr : Pkt.Body.data();
		const int32 N = (int32)Pkt.Body.size();
		if (Pkt.Opcode == OP_MOB_SKILL_CAST) { MS->OnNet8011(B, N); }
		else if (Pkt.Opcode == OP_MOB_SKILL_RESULT) { MS->OnNet8012(B, N); }
		else { MS->OnNet8013(B, N); }
		break;
	}
	case (NetOpcode)6561:                // Recv_WhisperOrSystemMessage @0xBA8560 (в enum отсутствует)
	{
		// payload: BYTE msgKind@0 (1 = шёпот, 5 = item-нотис), BYTE channelIdx@+1,
		//          … , char[20] имя@+12, текст@+32
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 33) { UE_LOG(LogAsdaNet, Warning, TEXT("6561 тело мало (%d)"), (int32)B.size()); break; }
		const uint8 Kind = B[0];
		const uint8 ChanIdx = B[1];
		if (Kind == 5)
		{
			UE_LOG(LogAsdaNet, Log, TEXT("6561: kind 5 — нотис loc 10222, строки в чат нет"));
			break;
		}
		if (Kind != 1)
		{
			UE_LOG(LogAsdaNet, Verbose, TEXT("6561: kind %u — эталон ничего не делает"), Kind);
			break;
		}
		const FString Name = FAsdaLoginData::DecodeCp1251(B.data() + 12, 20);
		const FString Body = FAsdaLoginData::DecodeWireText(B.data() + 32, (int32)B.size() - 32);   // [ПОРТ] 08.10: тело — гибрид cp1251/UTF-8
		// Анти-спам оригинала («.com»/«.c0m» → строка отбрасывается) — ЭТАП 2, вместе с мат-фильтром.
		UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		const FString Fmt = Txt ? Txt->GetTextById(20030) : FString();
		FAsdaChatLineIn L;
		L.SrcOpcode = 6561;
		L.bSenderAuthored = true;   // широкие каналы — пишет игрок SenderName
		// ★ Категория = channelIdx + 7. Проверки диапазона в эталоне НЕТ: байт сырой, 0..255.
		//   При 0/1/2 это слоты 0/1/2 («[Все] …»). При >2 эталон уходит в default: colorType 4
		//   и цвет остаётся равным a4 = −12602 (0xFFFFCEC6) — воспроизводим ровно это.
		L.Category = (int32)ChanIdx + 7;
		L.Fallback = FColor(0xFF, 0xCE, 0xC6, 0xFF);        // a4 = −12602
		L.SenderName = Name;
		// Формат-строка приходит ИЗ ЛОКАЛИЗАЦИИ (loc 20030), поэтому FString::Printf применить
		//   нельзя (в UE формат обязан быть литералом времени компиляции) — подставляем два «%s»
		//   вручную, как это делает эталонный sprintf(formattedLine, loc(20030), имя, текст).
		L.Text = AsdaFormatTwo(Fmt, Name, Body);
		if (Fmt.IsEmpty()) { L.Text = FString::Printf(TEXT("[%s]: %s"), *Name, *Body); }
		if (ChanIdx > 2)
		{
			UE_LOG(LogAsdaNet, Warning,
				TEXT("6561: channelIdx=%u > 2 → категория %d вне 7..9; эталон отправляет в общий канал цветом a4 (так и делаем)"),
				ChanIdx, L.Category);
		}
		UE_LOG(LogAsdaNet, Log,
			TEXT("6561 приём: канал=%u [%s] | тело[%d] %s"),
			ChanIdx, *Name, (int32)B.size(), *HexDumpBody(B.data(), (int32)B.size()));
		OnChatLine.Broadcast(L);
		break;
	}
	case OP_CLAN_CHAT:                   // 6719 0x1A3F — Recv_ClanChat @0xCA4D30, категория 6
	{
		// ★ У этого пакета эталон получает уже `&pktBuf[9]` — то есть ровно наш Body.
		// payload: BYTE onlineFlag@0, char[20] имя@+1, текст@+21
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 22) { UE_LOG(LogAsdaNet, Warning, TEXT("6719 тело мало (%d)"), (int32)B.size()); break; }
		const uint8 Online = B[0];
		const FString Name = FAsdaLoginData::DecodeCp1251(B.data() + 1, 20);
		const FString Body = FAsdaLoginData::DecodeWireText(B.data() + 21, (int32)B.size() - 21);   // [ПОРТ] 08.10: тело — гибрид cp1251/UTF-8
		UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		const FString Head = Txt ? Txt->GetTextById(Online ? 10102 : 10103) : FString();
		const FString Mid  = Txt ? Txt->GetTextById(8200) : FString();
		FAsdaChatLineIn L;
		L.SrcOpcode = 6719;
		L.bSenderAuthored = true;   // клан-чат — пишет игрок SenderName
		// ★ Категория 6 — ЕДИНСТВЕННАЯ, где эталон НЕ перетирает переданный цвет
		//   (colorType 40, textColor остаётся a4). Поэтому Fallback здесь реально работает.
		L.Category = 6;
		L.SenderName = Name;
		L.Text = FString::Printf(TEXT("[%s] [%s]%s : %s"), *Head, *Name, *Mid, *Body);
		if (Online)
		{
			// Онлайн-ветка берёт цвет через `sub_92E4C8(g_pChatWnd, 9)`. ★ 31.10.2026 подтверждено дизасмом (RE_SPEC campwar_bugs §1.1):
			//   `sub_92E4C8` = `jmp 0xBA2410` → `sub_BA2410(this, a2)`: `a2 ≤ 9 ? *(this + 6·a2 + 284) : −1` — цвет СЛОТА 9 таблицы
			//   каналов («Чат армии», 0xFFA03C00). [ОРИГИНАЛ]
			L.Fallback = UAsdaChatWindowSubsystem::SlotColor(9);
		}
		else
		{
			L.Fallback = FColor(0xFF, 0x5A, 0x00, 0xFF);    // −42496 = 0xFFFF5A00 (офлайн, 1:1)
		}
		// ★ Облачко (network.c:15854): цель = CharManager_FindByName(payload+1), вариант 0, a6=0,
		//   текст = ЧИСТОЕ тело (payload+21). Цвет ТЕКСТА = `chatColor`: онлайн — цвет канала 9
		//   (sub_BA2410(chat, 9) = *(chat + 6*9 + 284), «Чат армии» 0xFFA03C00), офлайн — −39906
		//   = 0xFFFF641E (ставится ПОСЛЕ Chat_AddChatLine, поэтому отличается от цвета строки).
		L.BalloonVariant = 0;
		L.BalloonText = Body;
		L.BalloonColor = Online ? FColor(0xA0, 0x3C, 0x00, 0xFF) : FColor(0xFF, 0x64, 0x1E, 0xFF);
		L.BalloonFontFlag = 0;
		UE_LOG(LogAsdaNet, Log, TEXT("6719 клан-чат: онлайн=%u [%s]"), Online, *Name);
		OnChatLine.Broadcast(L);
		break;
	}
	case OP_UI_NOTICE_UPDATE3_LINE_BC90:   // 3113 — серверное уведомление NOTICE_WIN (наш ..notice/..noticeoff)
	{
		// Эталон uiNoticeUpdate3Line_BC90@0xBABC90: payload 107б = pkt+9 → у нас Body[0..106]:
		//   word@+2 = режим (0=строка, 1=убрать всё, 100=loc 10221 «Битва начнется через %d мин.»),
		//   текст ASCIIZ@+4 (≤95), цвет R,G,B @+100..102 (альфа FF достраивается клиентом).
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 103) { UE_LOG(LogAsdaNet, Warning, TEXT("3113 тело мало (%d)"), (int32)B.size()); break; }
		const int32 Mode = (int32)AsdaBytes::RdU16(&B[2]);
		const FString Text = FAsdaLoginData::DecodeCp1251(B.data() + 4, 96);
		const FColor Rgb(B[100], B[101], B[102], 255);
		UE_LOG(LogAsdaNet, Log, TEXT("3113: notice mode=%d «%s» rgb=(%d,%d,%d)"), Mode, *Text, Rgb.R, Rgb.G, Rgb.B);
		OnServerNotice.Broadcast(Mode, Text, Rgb);
		break;
	}
	case OP_PACKET_SHOW_ITEM_ACQUIRED_NOTICE:   // 6131 0x17F3 — «игрок получил редкий предмет» (hero-нотис)
	{
		// Эталон Recv_ItemFound@0x9744B0: memcpy(Src+9, 24) → itemId dword@0, имя игрока cp1251@+4 (char[20]).
		const std::vector<uint8>& B = Pkt.Body;
		if (B.size() < 24) { UE_LOG(LogAsdaNet, Warning, TEXT("6131 тело мало (%d)"), (int32)B.size()); break; }
		const int32 ItemId = AsdaBytes::RdI32(&B[0]);
		const FString Name = FAsdaLoginData::DecodeCp1251(B.data() + 4, 20);
		// Байты 24+ шлёт наш сервер (AsdaPatch.dll, P11). Стоковый сервер оставляет их нулями,
		// стоковый клиент их игнорирует — совместимость в обе стороны.
		const int32 Src   = (B.size() > 24) ? (int32)B[24] : 0;
		const int32 RefLv = (B.size() > 25) ? (int32)B[25] : 0;
		const int32 RefPct= (B.size() > 27) ? (int32)AsdaBytes::RdU16(&B[26]) : 0;
		// ★★★ Сам факт прихода 6131 (чей угодно) доказывает, что на ЭТОМ процессе GameServer
		//   обработчик 6616 есть: 6131 в стоке никем не шлётся, его источник — только наш P01/P11,
		//   а рассылка `SendPacket_AllPlayer_InMap@0x0052E520` идёт всем слотам процесса.
		//   Значит наши 6616 обслуживаются и счётчик CUser+0x1E не растёт — снимаем страховку.
		if (Unacked6616 != 0 || bAnnounce6616Blocked)
		{
			UE_LOG(LogAsdaNet, Log,
				TEXT("6131 получен — обработчик 6616 на сервере ЕСТЬ, страховка снята (было без ответа: %d%s)"),
				Unacked6616, bAnnounce6616Blocked ? TEXT(", отправка была заблокирована") : TEXT(""));
			Unacked6616 = 0;
			bAnnounce6616Blocked = false;
		}
		UE_LOG(LogAsdaNet, Log, TEXT("6131: '%s' предмет %d (src=%d ступень=%d процент=%d)"),
			*Name, ItemId, Src, RefLv, RefPct);
		OnItemAcquired.Broadcast(Name, ItemId, Src, RefLv, RefPct);
		break;
	}
	default: bHandled = false; break;
	}
}
