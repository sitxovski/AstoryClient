// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ИТОГ ПРИГЛАШЕНИЯ В ПРИЯТЕЛИ 4151» ПУТЁМ ИГРОКА (05.11.2026, баг игроков «друг принял заявку — пригласившему
//   ни слова»). [ОРИГИНАЛ]/[ИСПРАВЛЕНИЕ]
//
// ЭТАЛОН `uiItemNoticeDispatch@0xCE7D00` (game.c:329517-329569): код 1 → плашка 221 (`CControl_SetTooltipText(g_pChatWnd, …,
//   −11514, 10000)`) + строка журнала (`CChat_AddTokenizedLines(…, −11514, 10000, 0)`) текстом loc 20000 со слотом = имя записи
//   (+12 тела); коды 0/2/3/4/5 → окно-сообщение 1500 (`CItem_BuildTooltipText`) с loc 12808/12811/12812/12813/20082; иное — ничего.
//   Сервер шлёт 4151 код 1 ПРИГЛАСИВШЕМУ после согласия друга (`sub_406370@0x406370`, GameServer.c:2542; флаги `UPFriendAdd`
//   1/2/3), коды 4/5/6/7/0 — сырым флагом, 5 — и на отказ (`Recv_UserFriendOK`, GameServer.c:118976).
//
// ЧТО ПРОВЕРЯЕТ (настоящий путь: `DebugFeedPacket(4151)` → приём `UAsdaNetSubsystem` → делегат `OnPlayerMenuResult` →
//   `UAsdaPlayerMenuSubsystem::OnMenuResult` (каналы) и `UAsdaCommunityFriendSubsystem::OnPlayerMenuResult` (модель списка)):
//   A  ключ `asda.Community.Friend.ResultChannels` 1, код 1 с именем «Alice» (ASCII) и «Борис» (cp1251): плашка 221 +1 строка
//      золотом −11514 с текстом loc 20000[имя], журнал +1 строка тем же текстом золотом, окно-сообщение НЕ открыто, приятель в списке.
//   B  коды 0/2/3/4/5 → окно-сообщение с текстом своего loc, плашка не тронута.
//   C  коды 6/7 (флаги `UPFriendAdd` «полны оба» / «уже приятели», эталон молчит) → окно 12813 / 12812 [ИСПРАВЛЕНИЕ].
//   D  код 8 (неизвестный) → ни окна, ни плашки.
//   K  ключ 0 — прежнее: код 1 → строка журнала без плашки; код 2 → строка журнала 12811, окна нет; код 6 → ничего.
// Консоль: `asda.SelfTest.FriendAccept [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ПРИЯТЕЛЬ-4151», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaPlayerMenuSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFriendAcceptTest, Log, All);   // канал строк самотеста

namespace AsdaFriendAcceptTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.FriendAccept");   // имя команды (каркас, ключи, итог)
	static const TCHAR* kPrefix = TEXT("САМОТЕСТ-ПРИЯТЕЛЬ-4151");     // префикс строк и итога
	static const TCHAR* kKey = TEXT("asda.Community.Friend.ResultChannels");   // проверяемый ключ

	// Один шаг сценария: задержка от начала шага, тело.
	struct FStep { FString Name; double Delay = 0.0; TFunction<void()> Fn; };

	// Снимок приёмников до приёма пакета (счёт «появилось именно сейчас»).
	struct FSnap
	{
		int32 War = 0;       // строк плашки 221 с иглой
		int32 Journal = 0;   // строк журнала 2572 с иглой
		int32 Boxes = 0;     // записей очереди окон-сообщений
		int32 Friends = 0;   // записей списка приятелей
		int32 Seq = 0;       // нотисов меню игрока
	};

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;          // игра прогона
		bool bQuit = false;                        // выйти после прогона
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TArray<FStep> Steps;                       // шаги
		int32 Next = 0;                            // текущий шаг
		double StepT = 0.0;                        // время входа в шаг
		TUniquePtr<FAsdaCVarScope> Keys;           // ключи прогона (возврат на выходе)
		FSnap Before;                              // снимок перед текущим приёмом
		FString Needle;                            // игла текущей проверки (текст строки)

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		UAsdaNetSubsystem* Net() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
		UAsdaChatSubsystem* Chat() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr; }
		UAsdaWindowSubsystem* WM() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr; }
		UAsdaCommunityFriendSubsystem* Fr() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaCommunityFriendSubsystem>() : nullptr; }
		UAsdaPlayerMenuSubsystem* Menu() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr; }
		UAsdaTextSubsystem* Txt() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr; }

		// Одна проверка: строка в лог и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaFriendAcceptTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaFriendAcceptTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		// Шаг сценария.
		void Do(const FString& Name, double Delay, TFunction<void()> Fn)
		{
			FStep St; St.Name = Name; St.Delay = Delay; St.Fn = MoveTemp(Fn);
			Steps.Add(MoveTemp(St));
		}
		// Снимок приёмников по игле `N`.
		FSnap Snap(const FString& N) const
		{
			FSnap R;
			if (const UAsdaChatSubsystem* C = Chat()) { R.War = C->DebugCountWarLines(N); R.Journal = C->DebugCountSystemLines(N); }
			if (const UAsdaWindowSubsystem* M = WM()) { R.Boxes = M->GetMsgBoxQueueNum(); }
			if (const UAsdaCommunityFriendSubsystem* F = Fr()) { R.Friends = F->GetFriendCount(); }
			if (const UAsdaPlayerMenuSubsystem* P = Menu()) { R.Seq = P->GetNoticeSeq(); }
			return R;
		}
	};

	// Имя персонажа в двух видах: cp1251-байты для тела пакета и строка для сверки.
	struct FName2 { TArray<uint8> Cp; FString Str; };
	// «Alice» (ASCII) и «Борис» (cp1251: Б C1, о EE, р F0, и E8, с F1).
	static FName2 NameAlice() { FName2 N; N.Cp = { 'A', 'l', 'i', 'c', 'e' }; N.Str = TEXT("Alice"); return N; }
	static FName2 NameBoris() { FName2 N; N.Cp = { 0xC1, 0xEE, 0xF0, 0xE8, 0xF1 }; N.Str = TEXT("Борис"); return N; }

	// Тело 4151 (32 б, как `sub_4C28F0(…, 4151, 32, …)` сервера): @0 код; @1 запись 31 б — @1 i32 CharUniqueIndex, @5 u8 класс,
	//   @12 имя cp1251 (20 б). Для кодов ≠ 1 сервер шлёт обнулённую запись (`sub_40BD70`).
	static void Feed4151(const FState& S, uint8 Code, const FName2* Name = nullptr, int32 CharId = 0)
	{
		TArray<uint8> B; B.SetNumZeroed(32);
		B[0] = Code;
		if (Name)
		{
			AsdaBytes::WrI32(B.GetData() + 1, CharId);
			B[5] = 1;   // класс-ключ записи (воин)
			for (int32 i = 0; i < Name->Cp.Num() && i < 19; ++i) { B[12 + i] = Name->Cp[i]; }
		}
		if (UAsdaNetSubsystem* N = S.Net()) { N->DebugFeedPacket(4151, B); }
	}

	// Снять очередь окон и запомнить снимок перед приёмом.
	static void Arm(FState& S, const FString& Needle)
	{
		if (UAsdaWindowSubsystem* M = S.WM()) { M->ClearMsgBoxQueue(kCmd); }
		S.Needle = Needle;
		S.Before = S.Snap(Needle);
	}

	// Раздел A: код 1 с именем `Name` при ключе 1.
	static void AddCode1(const TSharedRef<FState>& S, const FName2& Name, int32 CharId)
	{
		const FString Tag = FString::Printf(TEXT("A[%s]"), *Name.Str);   // метка раздела
		S->Do(Tag + TEXT(": приём 4151 код 1"), 0.05, [S, Name, CharId]()
		{
			S->Keys->Set(kKey, 1);
			Arm(*S, Name.Str);
			Feed4151(*S, 1, &Name, CharId);
		});
		S->Do(Tag + TEXT(": проверка"), 0.20, [S, Name, Tag]()
		{
			const FSnap A = S->Snap(Name.Str);
			const FString Want = S->Txt() ? S->Txt()->FormatById(20000, { Name.Str }, {}) : FString();   // loc 20000 со слотом
			FColor WarCol(0, 0, 0, 0), JCol(0, 0, 0, 0);
			UAsdaChatSubsystem* C = S->Chat();
			if (C) { C->DebugCountWarLines(Name.Str, &WarCol); C->DebugFindLastSystemLineColor(Name.Str, JCol); }
			const int32 JIdx = C ? C->DebugLastSystemLineIndex(Name.Str) : -1;
			UE_LOG(LogAsdaFriendAcceptTest, Display, TEXT("%s %s: loc 20000 = «%s»"), kPrefix, *Tag, *Want);
			S->Expect(!Want.IsEmpty() && Want.Contains(Name.Str),
				FString::Printf(TEXT("%s: текст loc 20000 со слотом-именем «%s» непуст"), *Tag, *Want));
			S->Expect(A.War == S->Before.War + 1 && WarCol == AsdaNoticeColor::Gold,
				FString::Printf(TEXT("%s: плашка 221 +%d строк(и) с именем, цвет %s (ждём +1, золотой FFD306 = −11514)"),
					*Tag, A.War - S->Before.War, *WarCol.ToHex()));
			S->Expect(A.Journal == S->Before.Journal + 1 && JCol == AsdaNoticeColor::Gold,
				FString::Printf(TEXT("%s: журнал +%d строк(и) с именем, цвет %s (ждём +1, золотой)"),
					*Tag, A.Journal - S->Before.Journal, *JCol.ToHex()));
			S->Expect(JIdx >= 0, FString::Printf(TEXT("%s: строка журнала найдена (индекс %d)"), *Tag, JIdx));
			S->Expect(A.Boxes == 0, FString::Printf(TEXT("%s: окно-сообщение не открыто (очередь %d, ждём 0)"), *Tag, A.Boxes));
			S->Expect(A.Friends == S->Before.Friends + 1,
				FString::Printf(TEXT("%s: приятель внесён в список вкладки (%d → %d)"), *Tag, S->Before.Friends, A.Friends));
			const UAsdaPlayerMenuSubsystem* P = S->Menu();
			S->Expect(P && P->GetLastNoticeLoc() == 20000 && P->GetLastNoticeSlot() == Name.Str,
				FString::Printf(TEXT("%s: нотис меню игрока loc %d слот «%s» (ждём 20000, «%s»)"), *Tag,
					P ? P->GetLastNoticeLoc() : -1, P ? *P->GetLastNoticeSlot() : TEXT(""), *Name.Str));
		});
	}

	// Разделы B/C/D: код `Code` при ключе `Key` → окно `WantLoc` (−1 — окна нет).
	static void AddRefusal(const TSharedRef<FState>& S, uint8 Code, int32 Key, int32 WantLoc, const TCHAR* Sec)
	{
		const FString Tag = FString::Printf(TEXT("%s код %d ключ %d"), Sec, (int32)Code, Key);   // метка раздела
		S->Do(Tag + TEXT(": приём 4151"), 0.05, [S, Code, Key, WantLoc]()
		{
			S->Keys->Set(kKey, Key);
			const FString Txt = WantLoc > 0 ? AsdaNotice::Loc(S->GI.Get(), WantLoc) : FString(TEXT("\x01\x02"));
			Arm(*S, Txt.IsEmpty() ? FString(TEXT("\x01\x02")) : Txt);
			Feed4151(*S, Code);
		});
		S->Do(Tag + TEXT(": проверка"), 0.20, [S, WantLoc, Tag]()
		{
			const FSnap A = S->Snap(S->Needle);
			const UAsdaWindowSubsystem* M = S->WM();
			const FString Top = M ? M->GetMsgBoxQueueTopText() : FString();
			if (WantLoc > 0)
			{
				S->Expect(A.Boxes == 1 && Top.Contains(S->Needle.Left(12)),
					FString::Printf(TEXT("%s: окно-сообщение (очередь %d, ждём 1) с текстом loc %d «%s»; показано «%s»"),
						*Tag, A.Boxes, WantLoc, *S->Needle, *Top));
				S->Expect(A.War == S->Before.War, FString::Printf(TEXT("%s: плашка 221 не тронута (+%d)"), *Tag, A.War - S->Before.War));
			}
			else
			{
				S->Expect(A.Boxes == 0 && A.Seq == S->Before.Seq,
					FString::Printf(TEXT("%s: ни окна (очередь %d), ни нотиса (+%d) — эталонный `default`"), *Tag, A.Boxes, A.Seq - S->Before.Seq));
			}
		});
	}

	static void Build(const TSharedRef<FState>& S)
	{
		S->Do(TEXT("подготовка"), 0.0, [S]()
		{
			S->Keys = MakeUnique<FAsdaCVarScope>(S->W(), kCmd);
			S->Expect(S->Net() && S->Chat() && S->WM() && S->Fr() && S->Menu() && S->Txt(), TEXT("0: подсистемы сети/чата/окон/приятелей/меню/текста есть"));
		});

		// ── A: согласие друга — код 1 (ASCII и cp1251-имя). ──
		AddCode1(S, NameAlice(), 880001);
		AddCode1(S, NameBoris(), 880002);

		// ── B: эталонные отказы — окно-сообщение. ──
		AddRefusal(S, 0, 1, 12808, TEXT("B"));
		AddRefusal(S, 2, 1, 12811, TEXT("B"));
		AddRefusal(S, 3, 1, 12812, TEXT("B"));
		AddRefusal(S, 4, 1, 12813, TEXT("B"));
		AddRefusal(S, 5, 1, 20082, TEXT("B"));
		// ── C: флаги UPFriendAdd 6/7, эталон молчит → окно [ИСПРАВЛЕНИЕ]. ──
		AddRefusal(S, 6, 1, 12813, TEXT("C"));
		AddRefusal(S, 7, 1, 12812, TEXT("C"));
		// ── D: неизвестный код. ──
		AddRefusal(S, 8, 1, -1, TEXT("D"));

		// ── K: ключ 0 — прежнее поведение порта. ──
		S->Do(TEXT("K: ключ 0, код 1 «Carol»"), 0.05, [S]()
		{
			S->Keys->Set(kKey, 0);
			Arm(*S, TEXT("Carol"));
			FName2 N; N.Cp = { 'C', 'a', 'r', 'o', 'l' }; N.Str = TEXT("Carol");
			Feed4151(*S, 1, &N, 880003);
		});
		S->Do(TEXT("K: проверка кода 1"), 0.20, [S]()
		{
			const FSnap A = S->Snap(TEXT("Carol"));
			S->Expect(A.War == S->Before.War && A.Journal == S->Before.Journal + 1 && A.Boxes == 0,
				FString::Printf(TEXT("K1: ключ 0 → плашка +%d (ждём 0), журнал +%d (ждём 1), окно %d (ждём 0) — прежнее"),
					A.War - S->Before.War, A.Journal - S->Before.Journal, A.Boxes));
		});
		S->Do(TEXT("K: ключ 0, код 2"), 0.05, [S]()
		{
			Arm(*S, AsdaNotice::Loc(S->GI.Get(), 12811).Left(12));
			Feed4151(*S, 2);
		});
		S->Do(TEXT("K: проверка кода 2"), 0.20, [S]()
		{
			const FSnap A = S->Snap(S->Needle);
			S->Expect(A.Boxes == 0 && A.Journal == S->Before.Journal + 1,
				FString::Printf(TEXT("K2: ключ 0, код 2 → окно %d (ждём 0), журнал +%d (ждём 1) — прежнее"), A.Boxes, A.Journal - S->Before.Journal));
		});
		AddRefusal(S, 6, 0, -1, TEXT("K"));
	}

	static void Finish(const TSharedRef<FState>& S)
	{
		if (UAsdaWindowSubsystem* M = S->WM()) { M->ClearMsgBoxQueue(kCmd); }
		S->Keys.Reset();   // ключи вернуть ДО строки итога
		AsdaSelfTestKit::Finish(S->W(), kCmd, S->bQuit, LogAsdaFriendAcceptTest, kPrefix, S->Ok, S->Fail);
	}

	static bool Tick(TSharedRef<FState> S)
	{
		if (!S->GI.IsValid()) { return false; }
		if (S->Next >= S->Steps.Num()) { Finish(S); return false; }
		FStep& St = S->Steps[S->Next];
		if (FPlatformTime::Seconds() - S->StepT < St.Delay) { return true; }
		St.Fn();
		++S->Next;
		S->StepT = FPlatformTime::Seconds();
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestFriendAccept(
	TEXT("asda.SelfTest.FriendAccept"),
	TEXT("[ОРИГИНАЛ] Самотест итога приглашения в приятели 4151 путём игрока: код 1 — плашка 221 + золотая строка журнала loc 20000, ")
	TEXT("коды 0/2/3/4/5 — окно-сообщение, 6/7 — окно [ИСПРАВЛЕНИЕ], ключ asda.Community.Friend.ResultChannels. Офлайн. [delay=N] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaFriendAcceptTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
		}
		TSharedRef<FState> S = MakeShared<FState>();
		S->GI = GI;
		S->bQuit = KA.bQuit;
		Build(S);
		S->StepT = FPlatformTime::Seconds() + KA.Delay + 1.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool { return Tick(S); }), 0.0f);
		UE_LOG(LogAsdaFriendAcceptTest, Display, TEXT("%s запуск: шагов %d"), kPrefix, S->Steps.Num());
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
