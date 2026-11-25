// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПОРЯДКА ПРОВЕРОК КАНАЛОВ ЧАТА («гейт канала → антифлуд»), 12.10.2026. [ОРИГИНАЛ]
//   Отчёт: `Reports/owner_1012/party_chat.md`, раздел «Порядок проверок каналов». Эталон — `chatInput_OnCommandKey` case 2609
//   (chat.c:3851-3975): допустимость канала проверяется ДО отправителя, а `chatAntiFloodCheck@0xBB4950` зовут только отправители
//   (`ChatInput_SendChannel/SendNormal/SendShout/SendParty/SendWhisperReply/ParseWhisper`); клан (`chatSendMessage`) и армия
//   (`Clan_Send_SetNotice`) его не зовут вовсе.
//
// ★ ЧТО ПРОВЕРЯЕТ (путь игрока = `DebugSubmit` — тот же `SubmitInput`, что Enter в поле 2609; исходящие — под перехватом, офлайн):
//   для каждого канала «недопустимо → нотис обработчика, пакет не ушёл, антифлуд не тронут (снимок счётчиков тот же)» и
//   «при блоке антифлуда недопустимо → только нотис канала, строки 7032 нет»; «допустимо → пакет ушёл (байты)».
//   G  «Глобальный» (3): нет профессии → loc 7004 красным в журнал 2572; есть → 5086 ×1, антифлуд учёл текст; контроль: блок → 7032.
//   C  «Клан» (6): нет клана → loc 7071 в журнал; есть → 4120 ×1, антифлуд НЕ учитывает текст; при блоке антифлуда 4120 всё равно ×1.
//   W  «Шёпот» (7): адресата нет → loc 6943 строкой ленты кат. 5 (слот 7); есть → 5088 вида 0 на адресата.
//   F  «Друг» (8): слота нет → loc 6929 в журнал; слот есть, пары нет / имени нет → 6929 строкой ленты кат. 101 (слот 8);
//      всё есть → 5088 вида 1 на имя партнёра.
//   K  кулдаун канала 1 «Торговля»: вторая отправка в кулдаун → 6560 ×0 и антифлуд не тронут (эталон: `!v37[112]` до отправителя).
//   P  «Отряд» (5) вне отряда → 6928, антифлуд не тронут (ключ отряда, для полноты таблицы).
//   L  ключ отката `asda.Chat.ChannelGateLegacy 1`: прежний порядок виден — отказ «Глобального» тратит антифлуд, «Друг» всегда
//      6929 в журнал, шёпот без адресата — 6942 в журнал (контрольная пара: проверка различает старое и новое поведение).
//
// Консоль: `asda.Chat.ChannelGateSelfTest [delay=N] [quit]`. Только офлайн (каркас `AsdaSelfTestKit`). Строки — «САМОТЕСТ-КАНАЛЫ»,
//   итог — «САМОТЕСТ-КАНАЛЫ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"           // каркас самотестов: отказ, перехват, свой персонаж, ключи, итог, выход
#include "UI/AsdaChatWindowSubsystem.h"     // SetInputState / DebugSubmit / снимок антифлуда / строки ленты / адресат шёпота
#include "UI/AsdaChatSubsystem.h"           // журнал 2572: DebugCountSystemLines / DebugFindLastSystemLineColor
#include "UI/AsdaSoulmateSubsystem.h"       // слот / пара / имя партнёра (DebugSetChatBond — только этот самотест)
#include "UI/AsdaPartySubsystem.h"          // IsInParty — «Отряд» вне отряда
#include "World/AsdaWorldSubsystem.h"       // GetSelfClanId — вернуть клан
#include "UI/AsdaNotice.h"                  // AsdaNotice::Loc — ожидаемые тексты нотисов
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaChanGateTest, Log, All);   // канал строк самотеста

namespace AsdaChanGateTest
{
	static const TCHAR* kCmd = TEXT("asda.Chat.ChannelGateSelfTest");   // имя команды (каркас, ключи, итог)
	static const TCHAR* kPrefix = TEXT("САМОТЕСТ-КАНАЛЫ");               // префикс строк и итога

	// Снимок счётчиков антифлуда `chatAntiFloodCheck` (быстрые, повторы, время последней учтённой отправки, текст, блок).
	struct FFlood
	{
		int32 Fast = 0, Repeat = 0; double LastSend = 0.0; FString LastText; bool bBlocked = false;
		bool operator==(const FFlood& O) const
		{
			return Fast == O.Fast && Repeat == O.Repeat && LastSend == O.LastSend && LastText == O.LastText && bBlocked == O.bBlocked;
		}
		FString Str() const { return FString::Printf(TEXT("{быстрых %d, повторов %d, посл. %.3f, текст «%s», блок %s}"), Fast, Repeat, LastSend, *LastText, bBlocked ? TEXT("да") : TEXT("нет")); }
	};

	// Состояние прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		bool bQuit = false;                        // выйти после прогона
		TWeakObjectPtr<UWorld> W;                  // мир прогона

		// Одна проверка: строка в лог и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaChanGateTest, Log, TEXT("САМОТЕСТ-КАНАЛЫ [OK]   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaChanGateTest, Error, TEXT("САМОТЕСТ-КАНАЛЫ [FAIL] %s"), *What); }
		}
	};

	// Подсистемы прогона.
	struct FSubs
	{
		UAsdaNetSubsystem* Net = nullptr;          // сеть (перехват, профессия, клан)
		UAsdaChatWindowSubsystem* Chat = nullptr;  // окно чата 2500 (ввод, лента, антифлуд)
		UAsdaChatSubsystem* Sys = nullptr;         // системный журнал 2572 (отказы `CChat_AddTokenizedLines`)
		UAsdaSoulmateSubsystem* Soul = nullptr;    // Друг
		UAsdaPartySubsystem* Party = nullptr;      // отряд
		UAsdaWorldSubsystem* WS = nullptr;         // зеркало своего клана
		bool Valid() const { return Net && Chat && Sys && Soul && Party && WS; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { return S; }
		S.Net = GI->GetSubsystem<UAsdaNetSubsystem>();
		S.Chat = GI->GetSubsystem<UAsdaChatWindowSubsystem>();
		S.Sys = GI->GetSubsystem<UAsdaChatSubsystem>();
		S.Soul = GI->GetSubsystem<UAsdaSoulmateSubsystem>();
		S.Party = GI->GetSubsystem<UAsdaPartySubsystem>();
		S.WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		return S;
	}

	static FFlood Flood(const UAsdaChatWindowSubsystem* Chat)
	{
		FFlood F;
		Chat->DebugAntiFloodSnapshot(F.Fast, F.Repeat, F.LastSend, F.LastText, F.bBlocked);
		return F;
	}

	// Enter в режиме `State` под перехватом ВСЕХ исходящих: сколько ушло пакетов `Op` (и всего), тело последнего `Op`.
	static int32 Submit(const FSubs& S, int32 State, const FString& Text, uint16 Op, TArray<uint8>& OutLast, int32* OutAll = nullptr)
	{
		int32 N = 0, All = 0;
		OutLast.Reset();
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(S.Net, [&N, &All, &OutLast, Op](uint16 O, const uint8* P, int32 Len, bool, TCHAR)
		{
			++All;
			if (O != Op) { return; }
			++N; OutLast.Reset();
			if (P && Len > 0) { OutLast.Append(P, Len); }
		}, 0, kCmd);
		if (Gen == 0) { return -1; }   // перехват занят — отказ каркаса уже в логе
		S.Chat->SetInputState(State);
		S.Chat->DebugSubmit(Text);
		AsdaSelfTestKit::DisarmCapture(S.Net, Gen);
		if (OutAll) { *OutAll = All; }
		return N;
	}

	// Байты → «01 4D …» для строк лога.
	static FString Hex(const TArray<uint8>& B)
	{
		FString S;
		for (const uint8 C : B) { S += FString::Printf(TEXT("%02X "), C); }
		return S.TrimEnd();
	}

	// Ожидаемое тело 5088 (эталон chat.c:2310-2320 / 2813-2939): BYTE вид + char[20] имя (ASCII, добито нулями) + текст без нуля.
	static TArray<uint8> Want5088(uint8 Kind, const char* Name, const char* Text)
	{
		TArray<uint8> B; B.SetNumZeroed(21);
		B[0] = Kind;
		for (int32 i = 0; Name[i] && i < 20; ++i) { B[1 + i] = (uint8)Name[i]; }
		for (int32 i = 0; Text[i]; ++i) { B.Add((uint8)Text[i]); }
		return B;
	}

	// ── Проверка «недопустимо»: пакета нет, нотис +1 в нужном месте, антифлуд не тронут; при блоке антифлуда — нет 7032. ──
	//   `FeedSlot` ≥ 0 — нотис строкой ленты этого слота; −1 — красной строкой в журнал 2572.
	static void ExpectDenied(FRun& R, const FSubs& S, const TCHAR* Tag, int32 State, uint16 Op, const FString& Notice, int32 FeedSlot)
	{
		UGameInstance* GI = R.W.IsValid() ? R.W->GetGameInstance() : nullptr;
		const FString L7032 = AsdaNotice::Loc(GI, 7032);
		auto Count = [&]() { return FeedSlot >= 0 ? S.Chat->DebugCountLogLines(Notice, FeedSlot) : S.Sys->DebugCountSystemLines(Notice); };

		// a) антифлуд чистый: снимок до/после одинаков.
		S.Chat->DebugResetAntiFlood(false);
		const FFlood F0 = Flood(S.Chat);
		const int32 Before = Count();
		TArray<uint8> Last; int32 All = 0;
		const int32 N = Submit(S, State, TEXT("gate"), Op, Last, &All);
		const int32 After = Count();
		const FFlood F1 = Flood(S.Chat);
		R.Expect(N == 0 && All == 0, FString::Printf(TEXT("%s-1 режим %d недопустим: %u ×%d, всего исходящих %d (ждём 0 и 0)"), Tag, State, Op, N, All));
		R.Expect(After == Before + 1, FString::Printf(TEXT("%s-2 нотис «%s» %s: %d→%d (ждём +1)"), Tag, *Notice,
			FeedSlot >= 0 ? *FString::Printf(TEXT("в ленте слот %d"), FeedSlot) : TEXT("в журнале 2572"), Before, After));
		if (FeedSlot < 0)
		{
			FColor Col = FColor::Black;
			const bool bCol = S.Sys->DebugFindLastSystemLineColor(Notice, Col);
			R.Expect(bCol && Col == FColor(255, 0, 0, 255), FString::Printf(TEXT("%s-3 цвет нотиса R=%u G=%u B=%u (ждём красный 255/0/0 = −65536)"), Tag, Col.R, Col.G, Col.B));
		}
		else
		{
			FString Raw, Nick; FColor Col; int32 Slot = -9;
			const bool bLine = S.Chat->DebugLastLogLine(Raw, Col, Slot, Nick);
			R.Expect(bLine && Raw == Notice && Slot == FeedSlot && Col == S.Chat->ChanColor(FeedSlot),
				FString::Printf(TEXT("%s-3 последняя строка ленты «%s» слот %d цвет = цвет канала %s (ждём «%s», слот %d)"), Tag, *Raw, Slot,
					*S.Chat->ChanColor(FeedSlot).ToHex(), *Notice, FeedSlot));
		}
		R.Expect(F1 == F0, FString::Printf(TEXT("%s-4 антифлуд не тронут: до %s, после %s"), Tag, *F0.Str(), *F1.Str()));

		// b) антифлуд в блоке: игрок видит причину отказа канала, а не 7032.
		S.Chat->DebugResetAntiFlood(true);
		const int32 B7032 = S.Chat->DebugCountLogLines(L7032);
		const int32 BeforeB = Count();
		const int32 NB = Submit(S, State, TEXT("gate"), Op, Last);
		R.Expect(NB == 0 && Count() == BeforeB + 1 && S.Chat->DebugCountLogLines(L7032) == B7032,
			FString::Printf(TEXT("%s-5 при блоке антифлуда: %u ×%d, нотис канала +%d, строк 7032 +%d (ждём ×0, +1, +0)"), Tag, Op, NB,
				Count() - BeforeB, S.Chat->DebugCountLogLines(L7032) - B7032));
		S.Chat->DebugResetAntiFlood(false);
	}

	// ── Весь прогон (синхронно: состояние каналов выставляется напрямую, приёма пакетов не нужно). ──
	//   Ключи и «свой персонаж» живут в области `RunBody` и возвращаются ДО строки итога (`Finish`).
	static void RunBody(const TSharedRef<FRun>& R, const FSubs& S, UWorld* W)
	{
		UGameInstance* GI = W->GetGameInstance();
		FAsdaNetSelfScope Self(S.Net, kCmd);                 // свой номер/charId — вернуть на выходе
		FAsdaCVarScope Keys(W, kCmd);                         // ключи отката — вернуть на выходе
		Keys.Set(TEXT("asda.Chat.ChannelGateLegacy"), 0);
		Keys.Set(TEXT("asda.Chat.PartyChatLegacy"), 0);
		// Снимок того, что прогон меняет.
		const int32 StateWas = S.Chat->GetInputState();
		const FString WhisperWas = S.Chat->DebugWhisperTarget();
		const int32 JobWas = S.Net->SelfJob;
		const int32 ClanWas = S.WS->GetSelfClanId();
		const int32 SoulSlotWas = S.Soul->GetFriendSlot();
		const bool bSoulPairWas = S.Soul->HasSoulmatePair();
		const FString SoulNameWas = S.Soul->GetPartnerName();
		S.Net->SetSelfUserNumForDebug(7);

		const FString L7004 = AsdaNotice::Loc(GI, 7004), L7071 = AsdaNotice::Loc(GI, 7071), L6943 = AsdaNotice::Loc(GI, 6943);
		const FString L6942 = AsdaNotice::Loc(GI, 6942), L6929 = AsdaNotice::Loc(GI, 6929), L6928 = AsdaNotice::Loc(GI, 6928);
		const FString L7032 = AsdaNotice::Loc(GI, 7032);
		TArray<uint8> Last;

		// ════════ G — «Глобальный» (3): профессия `gap_43B[10]` (у порта SelfJob) ════════
		S.Net->SelfJob = 0;
		ExpectDenied(*R, S, TEXT("G"), 3, 5086, L7004, -1);
		S.Net->SelfJob = 3;
		{
			S.Chat->DebugResetAntiFlood(false);
			const int32 N = Submit(S, 3, TEXT("glob"), 5086, Last);
			const FFlood F = Flood(S.Chat);
			R->Expect(N == 1 && Last.Num() == 27 + 4 && FMemory::Memcmp(Last.GetData() + 27, "glob", 4) == 0,
				FString::Printf(TEXT("G-6 профессия есть: 5086 ×%d, тело %d б, текст с +27 (ждём ×1, 31 б, «glob»)"), N, Last.Num()));
			R->Expect(F.LastText == TEXT("glob") && F.LastSend > 0.0, FString::Printf(TEXT("G-7 антифлуд учёл отправку (эталон: `chatAntiFloodCheck` в `ChatInput_SendNormal`): %s"), *F.Str()));
			S.Chat->DebugResetAntiFlood(true);
			const int32 B = S.Chat->DebugCountLogLines(L7032);
			const int32 NB = Submit(S, 3, TEXT("glob2"), 5086, Last);
			R->Expect(NB == 0 && S.Chat->DebugCountLogLines(L7032) == B + 1,
				FString::Printf(TEXT("G-8 контроль: допустимо, но блок антифлуда → 5086 ×%d, строк 7032 +%d (ждём ×0, +1)"), NB, S.Chat->DebugCountLogLines(L7032) - B));
			S.Chat->DebugResetAntiFlood(false);
		}

		// ════════ C — «Клан» (6): `gap_93A[32]` (clanId) ≥ 0 ════════
		S.Net->OnClanInfo.Broadcast(-1, -1, FString(), FString());
		R->Expect(S.WS->GetSelfClanId() < 0, FString::Printf(TEXT("C-0 клана нет (clanId %d)"), S.WS->GetSelfClanId()));
		ExpectDenied(*R, S, TEXT("C"), 6, 4120, L7071, -1);
		S.Net->OnClanInfo.Broadcast(-1, 7, TEXT("SelfTest"), FString());
		{
			S.Chat->DebugResetAntiFlood(false);
			const FFlood F0 = Flood(S.Chat);
			const int32 N = Submit(S, 6, TEXT("clan"), 4120, Last);
			const FFlood F1 = Flood(S.Chat);
			R->Expect(N == 1 && Last.Num() >= 32 && FMemory::Memcmp(Last.GetData() + Last.Num() - 4, "clan", 4) == 0,
				FString::Printf(TEXT("C-6 клан 7: 4120 ×%d, тело %d б (ждём ×1, текст «clan» в хвосте)"), N, Last.Num()));
			R->Expect(F1 == F0, FString::Printf(TEXT("C-7 клан-чат антифлуд не учитывает (эталон `chatSendMessage` его не зовёт): до %s, после %s"), *F0.Str(), *F1.Str()));
			S.Chat->DebugResetAntiFlood(true);
			const int32 B = S.Chat->DebugCountLogLines(L7032);
			const int32 NB = Submit(S, 6, TEXT("clan2"), 4120, Last);
			R->Expect(NB == 1 && S.Chat->DebugCountLogLines(L7032) == B,
				FString::Printf(TEXT("C-8 блок антифлуда клан-чат не держит: 4120 ×%d, строк 7032 +%d (ждём ×1, +0)"), NB, S.Chat->DebugCountLogLines(L7032) - B));
			S.Chat->DebugResetAntiFlood(false);
		}

		// ════════ W — «Шёпот» (7): адресат (`ChatInput_ParseWhisper(…, 2)`) ════════
		S.Chat->DebugSetWhisperTarget(FString());
		ExpectDenied(*R, S, TEXT("W"), 7, 5088, L6943, 7);
		S.Chat->DebugSetWhisperTarget(TEXT("Bob"));
		{
			S.Chat->DebugResetAntiFlood(false);
			const int32 N = Submit(S, 7, TEXT("psst"), 5088, Last);
			const TArray<uint8> Want = Want5088(0, "Bob", "psst");
			R->Expect(N == 1 && Last == Want, FString::Printf(TEXT("W-6 адресат Bob: 5088 ×%d тело [%s] (ждём ×1 [%s])"), N, *Hex(Last), *Hex(Want)));
			R->Expect(Flood(S.Chat).LastText == TEXT("psst"), TEXT("W-7 антифлуд учёл шёпот (эталон: `chatAntiFloodCheck` в `ChatInput_ParseWhisper`)"));
			S.Chat->DebugResetAntiFlood(false);
		}

		// ════════ F — «Друг» (8): слот → пара → имя партнёра ════════
		S.Soul->DebugSetChatBond(-1, false, FString());
		ExpectDenied(*R, S, TEXT("F1"), 8, 5088, L6929, -1);              // слота нет → журнал
		S.Soul->DebugSetChatBond(3, false, TEXT("Mate"));
		ExpectDenied(*R, S, TEXT("F2"), 8, 5088, L6929, 8);               // пары нет → лента кат. 101 = слот 8
		S.Soul->DebugSetChatBond(3, true, FString());
		ExpectDenied(*R, S, TEXT("F3"), 8, 5088, L6929, 8);               // имени нет → лента кат. 101
		S.Soul->DebugSetChatBond(3, true, TEXT("Mate"));
		S.Chat->DebugSetWhisperTarget(TEXT("Bob"));                       // адресат шёпота НЕ должен попасть в пакет Друга
		{
			S.Chat->DebugResetAntiFlood(false);
			const int32 N = Submit(S, 8, TEXT("hi"), 5088, Last);
			const TArray<uint8> Want = Want5088(1, "Mate", "hi");
			R->Expect(N == 1 && Last == Want, FString::Printf(TEXT("F-6 Друг Mate: 5088 ×%d тело [%s] (ждём ×1 [%s] — вид 1, имя партнёра, не адресат шёпота)"), N, *Hex(Last), *Hex(Want)));
			R->Expect(Flood(S.Chat).LastText == TEXT("hi"), TEXT("F-7 антифлуд учёл сообщение Другу (эталон: `chatAntiFloodCheck` в `ChatInput_SendWhisperReply`)"));
			S.Chat->DebugResetAntiFlood(false);
		}

		// ════════ K — кулдаун канала 1 «Торговля»: проверка до антифлуда ════════
		{
			S.Chat->DebugResetAntiFlood(false);
			const int32 N1 = Submit(S, 1, TEXT("sell"), 6560, Last);
			const FFlood F0 = Flood(S.Chat);
			const int32 N2 = Submit(S, 1, TEXT("sell again"), 6560, Last);
			const FFlood F1 = Flood(S.Chat);
			R->Expect(N1 == 1 && N2 == 0, FString::Printf(TEXT("K-1 «Торговля»: первая 6560 ×%d, вторая в кулдаун ×%d (ждём ×1, ×0)"), N1, N2));
			R->Expect(F1 == F0, FString::Printf(TEXT("K-2 кулдаун канала антифлуд не тратит (эталон `!v37[112]` до `ChatInput_SendChannel`): до %s, после %s"), *F0.Str(), *F1.Str()));
			S.Chat->DebugResetAntiFlood(false);
		}

		// ════════ P — «Отряд» вне отряда (ключ отряда; для полноты таблицы) ════════
		if (!S.Party->IsInParty())
		{
			ExpectDenied(*R, S, TEXT("P"), 5, 5105, L6928, -1);
		}
		else
		{
			R->Expect(false, TEXT("P офлайн-прогон, а персонаж в отряде — проверка «вне отряда» невозможна"));
		}

		// ════════ L — ключ отката: прежний порядок виден (контрольная пара) ════════
		Keys.Set(TEXT("asda.Chat.ChannelGateLegacy"), 1);
		{
			S.Net->SelfJob = 0;
			S.Chat->DebugResetAntiFlood(false);
			const int32 Before = S.Sys->DebugCountSystemLines(L7004);
			const int32 N = Submit(S, 3, TEXT("legacy"), 5086, Last);
			const FFlood F = Flood(S.Chat);
			R->Expect(N == 0 && S.Sys->DebugCountSystemLines(L7004) == Before + 1 && F.LastSend > 0.0,
				FString::Printf(TEXT("L-1 откат: «Глобальный» без профессии → 5086 ×%d, 7004 +%d, антифлуд ПОТРАЧЕН (посл. %.3f) — прежний порядок"), N,
					S.Sys->DebugCountSystemLines(L7004) - Before, F.LastSend));
			S.Chat->DebugResetAntiFlood(false);
			const int32 B6929 = S.Sys->DebugCountSystemLines(L6929);
			const int32 NF = Submit(S, 8, TEXT("legacy"), 5088, Last);
			R->Expect(NF == 0 && S.Sys->DebugCountSystemLines(L6929) == B6929 + 1,
				FString::Printf(TEXT("L-2 откат: «Друг» при полной паре → 5088 ×%d, 6929 в журнал +%d (прежняя заглушка)"), NF, S.Sys->DebugCountSystemLines(L6929) - B6929));
			S.Chat->DebugResetAntiFlood(false);
			S.Chat->DebugSetWhisperTarget(FString());
			const int32 B6942 = S.Sys->DebugCountSystemLines(L6942);
			const int32 NW = Submit(S, 7, TEXT("legacy"), 5088, Last);
			R->Expect(NW == 0 && S.Sys->DebugCountSystemLines(L6942) == B6942 + 1,
				FString::Printf(TEXT("L-3 откат: шёпот без адресата → 5088 ×%d, 6942 в журнал +%d (прежняя строка)"), NW, S.Sys->DebugCountSystemLines(L6942) - B6942));
			S.Chat->DebugResetAntiFlood(false);
		}
		Keys.Set(TEXT("asda.Chat.ChannelGateLegacy"), 0);

		// Возврат состояния.
		S.Chat->SetInputState(StateWas);
		S.Chat->DebugSetWhisperTarget(WhisperWas);
		S.Net->SelfJob = JobWas;
		S.Net->OnClanInfo.Broadcast(-1, ClanWas, FString(), FString());
		S.Soul->DebugSetChatBond(SoulSlotWas, bSoulPairWas, SoulNameWas);
		S.Chat->DebugResetAntiFlood(false);
	}

	// Прогон и итог.
	static void Run(const TSharedRef<FRun>& R)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (!S.Valid()) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет подсистем сети/чата/журнала/Друга/отряда/мира")); return; }
		RunBody(R, S, W);
		AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaChanGateTest, kPrefix, R->Ok, R->Fail);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaChatChannelGateSelfTest(
	TEXT("asda.Chat.ChannelGateSelfTest"),
	TEXT("asda.Chat.ChannelGateSelfTest [delay=N] [quit] — ОФЛАЙН-самотест порядка проверок каналов чата: гейт канала ДО антифлуда ")
	TEXT("(Глобальный 7004, Клан 7071, Шёпот 6943, Друг 6929, кулдаун 0/1/2, Отряд 6928), клан без антифлуда, 5088 Другу на имя партнёра, ")
	TEXT("ключ отката asda.Chat.ChannelGateLegacy"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaChanGateTest;
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		if (!W) { return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->bQuit = Args.bQuit;
		R->W = W;
		auto Go = [R]()
		{
			UWorld* Wd = R->W.Get();
			if (!Wd) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, kCmd, R->bQuit)) { return; }
			Run(R);
		};
		if (Args.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Args.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
