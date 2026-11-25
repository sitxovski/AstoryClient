// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ЧАТА ОТРЯДА (5105 / 5106), 12.10.2026. [ОРИГИНАЛ]
//   Отчёт: `Reports/owner_1012/party_chat.md`. Основание — хвост ревизии В2 (`Reports/dedup_audit2/wave3_S10.md`):
//   режим «Отряд» всегда отклонялся заглушкой порта, 5105 не уходил никогда, а проверка S6 (`asda.Net.CharIdSendersSelfTest`)
//   подтверждала заглушку, а не поведение.
//
// ★ ЧТО ПРОВЕРЯЕТ (путь игрока = `DebugSubmit` — тот же `SubmitInput`, что Enter в поле 2609; вход/выход из отряда — НАСТОЯЩИЕ
//   пакеты 5093/5103 через штатный `HandlePacket` → очередь подсистемы отряда → её тик). Ожидания — по эталону, явными байтами:
//   P0  офлайн-старт: я не в отряде.
//   P1  вне отряда, режим 5: 5105 ×0; в системном журнале 2572 +1 строка loc 6928, КРАСНАЯ (−65536)
//       (`chatInput_OnCommandKey` chat.c:3917-3924 → `CChat_AddTokenizedLines(…, −65536, 10000, 0)`).
//   P2  5093 код 1 (отряд создан, partyIndex 42) → в отряде, индекс 42.
//   P3  в отряде, свой номер известен: 5105 ×1, тело = i16 42 + cp1251 текста (`Party_SendChat(g_nPartyInviteTargetIdx, msg)`,
//       chat.c:3230, social.c:9569-9598); строки 6928 нет.
//   P4  в отряде, свой номер НЕ известен: 5105 ×1 с тем же индексом отряда — поле @0 не userNum (сервер его не читает,
//       `sub_450FB0` GameServer.c:32576).
//   P5  ключ отката `asda.Chat.PartyChatLegacy 1`: прежняя заглушка — 5105 ×0, +1 строка 6928.
//   P6  приём 5106 (синтетический пакет): строка ленты «[Имя]: текст» (network.c:39278 `"[%s]: %s"`), слот 5, цвет слота 5
//       (заводской −5505159 = FFABFF79), ник = имя; вкладки, пропускающие слот 5, — 0/1/4 (маски 1023/928/930).
//   P7  5103 (отряд распущен) → не в отряде; режим чата остаётся 5 (у эталона выход из отряда канал не трогает); Enter →
//       5105 ×0, +1 строка 6928.
//
// Консоль: `asda.Party.ChatSelfTest [delay=N] [quit]`. Только офлайн (каркас `AsdaSelfTestKit`). Строки — «САМОТЕСТ-ОТРЯД-ЧАТ»,
//   итог — «САМОТЕСТ-ОТРЯД-ЧАТ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                      // WrI16 / RdI16 — тела пакетов
#include "Test/AsdaSelfTestKit.h"           // каркас самотестов: отказ, перехват, свой персонаж, ключи, итог, выход
#include "Login/AsdaLoginData.h"            // EncodeCp1251 — имя отправителя 5106
#include "UI/AsdaChatWindowSubsystem.h"     // SetInputState / DebugSubmit / DebugLastLogLine / DebugTabsShowingSlot
#include "UI/AsdaChatSubsystem.h"           // журнал 2572: DebugCountSystemLines / DebugFindLastSystemLineColor
#include "UI/AsdaPartySubsystem.h"          // IsInParty / GetPartyIndex / DebugSetPartyState (только возврат состояния)
#include "UI/AsdaNotice.h"                  // AsdaNotice::Loc — ожидаемый текст 6928
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPartyChatTest, Log, All);   // канал строк самотеста

namespace AsdaPartyChatTest
{
	static const TCHAR* kCmd = TEXT("asda.Party.ChatSelfTest");   // имя команды (каркас, ключи, итог)
	static constexpr int32 kPartyIdx = 42;                        // индекс отряда из синтетического 5093
	static constexpr float kTickWait = 0.5f;                      // ожидание разбора очереди подсистемы отряда, с

	// Состояние прогона между шагами (таймеры) — живёт в общем указателе.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		bool bQuit = false;                        // выйти после прогона
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		TUniquePtr<FAsdaNetSelfScope> Self;        // свой номер/charId — вернуть на любом выходе
		TUniquePtr<FAsdaCVarScope> Keys;           // ключ отката — вернуть
		int32 InputStateWas = 0;                   // режим чата до прогона
		bool bInPartyWas = false, bLeaderWas = false;   // отряд до прогона
		int32 PartyIdxWas = -1, LootRuleWas = 0;

		// Одна проверка: строка в лог и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaPartyChatTest, Log, TEXT("САМОТЕСТ-ОТРЯД-ЧАТ [OK]   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPartyChatTest, Error, TEXT("САМОТЕСТ-ОТРЯД-ЧАТ [FAIL] %s"), *What); }
		}
	};

	// Подсистемы прогона.
	struct FSubs
	{
		UAsdaNetSubsystem* Net = nullptr;          // сеть (перехват, приём)
		UAsdaChatWindowSubsystem* Chat = nullptr;  // окно чата 2500 (ввод, лента)
		UAsdaChatSubsystem* Sys = nullptr;         // системный журнал 2572 (отказы)
		UAsdaPartySubsystem* Party = nullptr;      // отряд
		bool Valid() const { return Net && Chat && Sys && Party; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { return S; }
		S.Net = GI->GetSubsystem<UAsdaNetSubsystem>();
		S.Chat = GI->GetSubsystem<UAsdaChatWindowSubsystem>();
		S.Sys = GI->GetSubsystem<UAsdaChatSubsystem>();
		S.Party = GI->GetSubsystem<UAsdaPartySubsystem>();
		return S;
	}

	// Ввод строки в режиме чата путём Enter под перехватом исходящих: число 5105 и тело последнего.
	static int32 Submit(UAsdaNetSubsystem* Net, UAsdaChatWindowSubsystem* Chat, int32 State, const FString& Text, TArray<uint8>& OutLast)
	{
		int32 N = 0;
		OutLast.Reset();
		const uint32 Gen = AsdaSelfTestKit::ArmCapture(Net, [&N, &OutLast](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (Op != 5105) { return; }
			++N; OutLast.Reset();
			if (P && Len > 0) { OutLast.Append(P, Len); }
		}, 0, kCmd);
		if (Gen == 0) { return -1; }   // перехват занят — отказ каркаса уже в логе
		Chat->SetInputState(State);
		Chat->DebugSubmit(Text);
		AsdaSelfTestKit::DisarmCapture(Net, Gen);
		return N;
	}

	// Байты → «2A 00 CF …» для строк лога.
	static FString Hex(const TArray<uint8>& B)
	{
		FString S;
		for (const uint8 C : B) { S += FString::Printf(TEXT("%02X "), C); }
		return S.TrimEnd();
	}

	// Конец: вернуть режим чата и отряд, свой номер, ключи; итог и выход.
	static void Finish(const TSharedRef<FRun>& R)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (S.Chat) { S.Chat->SetInputState(R->InputStateWas); }
		if (S.Party && (S.Party->IsInParty() != R->bInPartyWas || S.Party->GetPartyIndex() != R->PartyIdxWas))
		{
			S.Party->DebugSetPartyState(R->bInPartyWas, R->bLeaderWas, R->PartyIdxWas, R->LootRuleWas);
		}
		R->Self.Reset();
		R->Keys.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaPartyChatTest, TEXT("САМОТЕСТ-ОТРЯД-ЧАТ"), R->Ok, R->Fail);
	}

	// Шаг 3 (после 5103): выход из отряда — режим не сброшен, отправки нет, нотис 6928.
	static void Step3(const TSharedRef<FRun>& R)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (!S.Valid()) { R->Expect(false, TEXT("P7 подсистемы пропали")); Finish(R); return; }
		const FString L6928 = AsdaNotice::Loc(W->GetGameInstance(), 6928);
		R->Expect(!S.Party->IsInParty(), FString::Printf(TEXT("P7a 5103 (распущен отряд %d) → не в отряде (индекс %d)"), kPartyIdx, S.Party->GetPartyIndex()));
		R->Expect(S.Chat->GetInputState() == 5, FString::Printf(TEXT("P7b режим чата после выхода из отряда = %d (ждём 5: эталон канал не трогает)"), S.Chat->GetInputState()));
		const int32 Before = S.Sys->DebugCountSystemLines(L6928);
		TArray<uint8> Last;
		const int32 N = Submit(S.Net, S.Chat, 5, TEXT("после выхода"), Last);
		const int32 After = S.Sys->DebugCountSystemLines(L6928);
		R->Expect(N == 0 && After == Before + 1, FString::Printf(TEXT("P7c Enter после выхода: 5105 ×%d (ждём ×0), строк «%s» %d→%d (ждём +1)"), N, *L6928, Before, After));
		Finish(R);
	}

	// Шаг 2 (после 5093): в отряде — отправка, ключ отката, приём 5106; затем 5103.
	static void Step2(const TSharedRef<FRun>& R)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (!S.Valid()) { R->Expect(false, TEXT("P2 подсистемы пропали")); Finish(R); return; }
		const FString L6928 = AsdaNotice::Loc(W->GetGameInstance(), 6928);
		R->Expect(S.Party->IsInParty() && S.Party->GetPartyIndex() == kPartyIdx,
			FString::Printf(TEXT("P2 5093 код 1 → в отряде=%s, индекс %d (ждём %d)"), S.Party->IsInParty() ? TEXT("да") : TEXT("нет"), S.Party->GetPartyIndex(), kPartyIdx));

		// P3: номер известен. Ожидаемое тело — явными байтами: i16 42 + cp1251 «Привет отряд» (CF F0 E8 E2 E5 F2 20 EE F2 F0 FF E4).
		static const uint8 kWant3[] = { 0x2A, 0x00, 0xCF, 0xF0, 0xE8, 0xE2, 0xE5, 0xF2, 0x20, 0xEE, 0xF2, 0xF0, 0xFF, 0xE4 };
		S.Net->SetSelfUserNumForDebug(7);
		{
			const int32 Before = S.Sys->DebugCountSystemLines(L6928);
			TArray<uint8> Last;
			const int32 N = Submit(S.Net, S.Chat, 5, TEXT("Привет отряд"), Last);
			const bool bBody = Last.Num() == UE_ARRAY_COUNT(kWant3) && FMemory::Memcmp(Last.GetData(), kWant3, UE_ARRAY_COUNT(kWant3)) == 0;
			R->Expect(N == 1 && bBody, FString::Printf(TEXT("P3 в отряде, свой номер 7: 5105 ×%d (ждём ×1), тело [%s] (ждём i16 42 + cp1251 текста)"), N, *Hex(Last)));
			R->Expect(S.Sys->DebugCountSystemLines(L6928) == Before, TEXT("P3b в отряде отказа 6928 нет"));
		}
		// P4: номер неизвестен — поле @0 = индекс отряда, свой номер не нужен.
		S.Net->bSelfUserNumKnown = false;
		{
			TArray<uint8> Last;
			const int32 N = Submit(S.Net, S.Chat, 5, TEXT("abc"), Last);
			const int32 Idx = Last.Num() >= 2 ? AsdaBytes::RdI16(Last.GetData()) : -999;
			R->Expect(N == 1 && Idx == kPartyIdx && Last.Num() == 5, FString::Printf(TEXT("P4 в отряде, свой номер НЕ известен: 5105 ×%d, @0 = %d (ждём ×1 и %d), тело [%s]"), N, Idx, kPartyIdx, *Hex(Last)));
		}
		S.Net->SetSelfUserNumForDebug(7);
		// P5: ключ отката — прежняя заглушка.
		R->Keys->Set(TEXT("asda.Chat.PartyChatLegacy"), 1);
		{
			const int32 Before = S.Sys->DebugCountSystemLines(L6928);
			TArray<uint8> Last;
			const int32 N = Submit(S.Net, S.Chat, 5, TEXT("заглушка"), Last);
			const int32 After = S.Sys->DebugCountSystemLines(L6928);
			R->Expect(N == 0 && After == Before + 1, FString::Printf(TEXT("P5 asda.Chat.PartyChatLegacy 1 (откат): 5105 ×%d (ждём ×0), строк 6928 %d→%d (ждём +1)"), N, Before, After));
		}
		R->Keys->Set(TEXT("asda.Chat.PartyChatLegacy"), 0);

		// P6: приём 5106 — тело 25 б заголовка (@0 i16 userNum, @2 char[20] имя, @22 i16, @24 u8) + текст + 0.
		{
			const FString Name = TEXT("Соотрядник");
			TArray<uint8> B; B.SetNumZeroed(25);
			AsdaBytes::WrI16(&B[0], 3);
			FAsdaLoginData::EncodeCp1251(Name, B.GetData() + 2, 19);
			static const uint8 kText[] = { 0xE8, 0xE4, 0xF3, 0x21 };   // cp1251 «иду!»
			B.Append(kText, UE_ARRAY_COUNT(kText)); B.Add(0);
			S.Net->DebugFeedPacket(5106, B);
			FString Raw, Nick; FColor Col; int32 Slot = -9;
			const bool bLine = S.Chat->DebugLastLogLine(Raw, Col, Slot, Nick);
			const FString Want = FString::Printf(TEXT("[%s]: %s"), *Name, TEXT("иду!"));
			R->Expect(bLine && Raw == Want && Slot == 5 && Nick == Name,
				FString::Printf(TEXT("P6a приём 5106 → строка «%s» слот %d ник «%s» (ждём «%s», слот 5)"), *Raw, Slot, *Nick, *Want));
			const FColor Factory = UAsdaChatWindowSubsystem::SlotColor(5);
			R->Expect(Factory == FColor(0xAB, 0xFF, 0x79, 0xFF), FString::Printf(TEXT("P6b заводской цвет слота 5 = %s (ждём FFABFF79 = −5505159)"), *Factory.ToHex()));
			R->Expect(bLine && Col == S.Chat->ChanColor(5), FString::Printf(TEXT("P6c цвет строки %s = действующий цвет канала «отряд» %s"), *Col.ToHex(), *S.Chat->ChanColor(5).ToHex()));
			const TArray<int32> Tabs = S.Chat->DebugTabsShowingSlot(5);
			FString TabsS; for (const int32 T : Tabs) { TabsS += FString::Printf(TEXT("%d "), T); }
			R->Expect(Tabs == TArray<int32>({ 0, 1, 4 }), FString::Printf(TEXT("P6d вкладки, где видна строка отряда: %s(ждём 0 1 4 — маски 1023/928/930)"), *TabsS));
		}

		// Выход из отряда — настоящий 5103 (@0 i16 partyIndex) через штатный приём.
		{
			TArray<uint8> B; B.SetNumZeroed(2);
			AsdaBytes::WrI16(&B[0], kPartyIdx);
			S.Net->DebugFeedPacket(5103, B);
		}
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R]() { Step3(R); }), kTickWait, false);
	}

	// Шаг 1: вне отряда; затем вход в отряд пакетом 5093.
	static void Step1(const TSharedRef<FRun>& R)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (!S.Valid()) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет подсистем сети/чата/журнала/отряда")); return; }
		R->Self = MakeUnique<FAsdaNetSelfScope>(S.Net, kCmd);
		R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
		R->Keys->Set(TEXT("asda.Chat.PartyChatLegacy"), 0);
		R->InputStateWas = S.Chat->GetInputState();
		R->bInPartyWas = S.Party->IsInParty(); R->bLeaderWas = S.Party->IsPartyLeader();
		R->PartyIdxWas = S.Party->GetPartyIndex(); R->LootRuleWas = S.Party->GetPartyLootRule();
		const FString L6928 = AsdaNotice::Loc(W->GetGameInstance(), 6928);

		R->Expect(!S.Party->IsInParty(), FString::Printf(TEXT("P0 офлайн-старт: не в отряде (индекс %d)"), S.Party->GetPartyIndex()));
		S.Net->SetSelfUserNumForDebug(7);
		{
			const int32 Before = S.Sys->DebugCountSystemLines(L6928);
			TArray<uint8> Last;
			const int32 N = Submit(S.Net, S.Chat, 5, TEXT("вне отряда"), Last);
			const int32 After = S.Sys->DebugCountSystemLines(L6928);
			FColor Col = FColor::Black;
			const bool bCol = S.Sys->DebugFindLastSystemLineColor(L6928, Col);
			R->Expect(N == 0, FString::Printf(TEXT("P1a вне отряда, свой номер 7: 5105 ×%d (ждём ×0)"), N));
			R->Expect(After == Before + 1 && bCol && Col == FColor(255, 0, 0, 255),
				FString::Printf(TEXT("P1b нотис «%s» в журнале 2572: %d→%d (ждём +1), цвет %s (ждём FFFF0000 = −65536)"), *L6928, Before, After, *Col.ToHex()));
		}
		// Вход в отряд: 5093 код 1 (тело 15 б: код, userNum/charId запросившего, userNum/charId ответившего, partyIndex @13).
		{
			TArray<uint8> B; B.SetNumZeroed(15);
			B[0] = 1;
			AsdaBytes::WrI16(&B[1], 7);  AsdaBytes::WrI32(&B[3], 1000);
			AsdaBytes::WrI16(&B[7], 3);  AsdaBytes::WrI32(&B[9], 2000);
			AsdaBytes::WrI16(&B[13], kPartyIdx);
			S.Net->DebugFeedPacket(5093, B);
		}
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R]() { Step2(R); }), kTickWait, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaPartyChatSelfTest(
	TEXT("asda.Party.ChatSelfTest"),
	TEXT("asda.Party.ChatSelfTest [delay=N] [quit] — ОФЛАЙН-самотест чата отряда: гейт «в отряде» (loc 6928), 5105 (@0 = индекс отряда), ")
	TEXT("ключ отката asda.Chat.PartyChatLegacy, приём 5106 (строка/цвет/вкладки), выход из отряда 5103"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaPartyChatTest;
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
			Step1(R);
		};
		if (Args.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Args.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
