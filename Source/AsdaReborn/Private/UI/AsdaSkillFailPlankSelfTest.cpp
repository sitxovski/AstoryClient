// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КАНАЛ ОТКАЗА НАВЫКА» (08.11.2026, пачка 16, решение владельца «Да, исправить»): `asda.SelfTest.SkillFailPlank [out=<папка>] [quit]`.
//   Ключ `asda.Skill.FailNoticePlank` (`UAsdaSkillUseSubsystem::PostFailNotice`). Эталон: `Recv_SkillCast@0xF4E370` (5257, network.c:44966)
//   и `recvOtherCharSkillUse@0xF4D7D0` (5271, :44637) — любой код ≠ 1, только свой персонаж → `CControl_SetTooltipText(g_pChatWnd,
//   dword_15B5D78[код], −11514, 10000)`: плашка (золото), строки системного чата нет.
//   Офлайн: входящие — штатный разбор `DebugFeedPacket`.
//   Ветки:
//     P1–P4) 5257 своему коды 0/5/21/28 → плашки 1017/1021/1150/1157 золотом, в системном чате этих строк нет;
//     P5–P6) 5271 своему коды 13/24 → плашки 1076/1153;
//     P7) повтор того же кода → плашка снова (оригинал не подавляет);
//     N1) 5257 соседу (чужой userIdx) → ничего; N2) код 18 (в таблице 0) → ничего;
//     K0) ключ 0 → прежний порт: строка системного чата, плашки нет.
//   Итог: «ПЛАШКА-НАВЫКА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — отложенный старт   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaNoticeColors.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkillFailPlankTest, Log, All);

namespace AsdaSkillFailPlankTest
{
	const TCHAR* const kCmd = TEXT("asda.SelfTest.SkillFailPlank");   // имя команды
	const TCHAR* const kPrefix = TEXT("ПЛАШКА-НАВЫКА");          // префикс строк
	const TCHAR* const kKey = TEXT("asda.Skill.FailNoticePlank");      // ключ правки
	constexpr int32 kSelfFallback = 4324;   // свой номер, если не известен
	constexpr int32 kPeer = 7832;           // чужой userIdx
	constexpr int32 kSkill = 503;           // код навыка в пакетах

	struct FStep { FString Name; float TimeoutSec = 5.f; TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check; };

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ kCmd, false };
		int32 Ok = 0, Fail = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		int32 Self = -1;
		int32 WarMark = 0, ChatMark = 0;   // счётчики строк плашки/чата с текстом шага до пакета
		FString Txt;                         // текст ожидаемого нотиса
		FString OutDir;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaChatSubsystem* Chat() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaChatSubsystem>() : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			const TCHAR* Nm = Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?");
			if (bOk) { UE_LOG(LogAsdaSkillFailPlankTest, Log, TEXT("%s ok   %s: %s"), kPrefix, Nm, *What); }
			else     { UE_LOG(LogAsdaSkillFailPlankTest, Error, TEXT("%s FAIL %s: %s"), kPrefix, Nm, *What); }
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		// 5257 отказ (8 б): код@0, userIdx@1, навык@3, @5 = 0, @6 = −1.
		void Refuse5257(int32 Uidx, int32 Code)
		{
			TArray<uint8> B; B.SetNumZeroed(8); B[0] = (uint8)Code; AsdaBytes::WrI16(&B[1], (int16)Uidx); AsdaBytes::WrI16(&B[3], kSkill);
			AsdaBytes::WrI16(&B[6], -1); Feed(5257, B);
		}
		// 5271 (7 б): код@0, userIdx@1, навык@3, флаг@5.
		void Refuse5271(int32 Uidx, int32 Code)
		{
			TArray<uint8> B; B.SetNumZeroed(7); B[0] = (uint8)Code; AsdaBytes::WrI16(&B[1], (int16)Uidx); AsdaBytes::WrI16(&B[3], kSkill);
			AsdaBytes::WrI16(&B[5], 1); Feed(5271, B);
		}
		// Запомнить счётчики строк с текстом loc LocId до пакета.
		void Arm(int32 LocId)
		{
			Txt = AsdaNotice::Loc(GI(), LocId);
			const UAsdaChatSubsystem* C = Chat();
			WarMark = C ? C->DebugCountWarLines(Txt) : 0;
			ChatMark = C ? C->DebugCountSystemLines(Txt) : 0;
		}
		int32 WarNow(FColor* Col = nullptr) const { const UAsdaChatSubsystem* C = Chat(); return C ? C->DebugCountWarLines(Txt, Col) : 0; }
		int32 ChatNow() const { const UAsdaChatSubsystem* C = Chat(); return C ? C->DebugCountSystemLines(Txt) : 0; }
		void Shot(const TCHAR* Tag)
		{
			if (OutDir.IsEmpty()) { return; }
			FScreenshotRequest::RequestScreenshot(OutDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		}
	};
}

struct FAsdaSkillFailPlankSelfTest { static void Run(const TArray<FString>& Args, UWorld* W); };

void FAsdaSkillFailPlankSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaSkillFailPlankTest;
	const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KA.bQuit; S->Gate.bQuit = KA.bQuit;
	for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { S->OutDir = A.Mid(4); } }
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};

	Add(TEXT("0 подготовка"), 3.f, [S, Net]()
	{
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		S->Self = (int32)Net->SelfUserNum;
		AsdaCVarSave::SetForTest(kKey, 1);
	}, [S]() { return S->El() > 0.3; }, [S]() { S->Expect(S->Chat() != nullptr, TEXT("подсистема чата есть")); });

	// Своему: плашка нужного текста, золотом; строки системного чата нет.
	auto Plank = [S, Add](const TCHAR* Name, int32 Op, int32 Code, int32 LocId, const TCHAR* ShotTag)
	{
		const FString Nm(Name); const FString Tag = ShotTag ? FString(ShotTag) : FString();
		Add(Nm, 3.f, [S, Op, Code, LocId]()
		{
			S->Arm(LocId);
			if (Op == 5257) { S->Refuse5257(S->Self, Code); } else { S->Refuse5271(S->Self, Code); }
		}, [S]() { return S->El() > 0.4; }, [S, Nm, Op, Code, LocId, Tag]()
		{
			FColor Col(0, 0, 0, 0);
			const int32 War = S->WarNow(&Col) - S->WarMark, Ch = S->ChatNow() - S->ChatMark;
			S->Expect(!S->Txt.IsEmpty() && War == 1, FString::Printf(TEXT("%d код %d → плашка loc %d «%s» (+%d)"), Op, Code, LocId, *S->Txt, War));
			S->Expect(Col == AsdaNoticeColor::Gold, FString::Printf(TEXT("цвет плашки #%02X%02X%02X — золото −11514"), Col.R, Col.G, Col.B));
			S->Expect(Ch == 0, FString::Printf(TEXT("строки системного чата нет (+%d)"), Ch));
			if (!Tag.IsEmpty()) { S->Shot(*Tag); }
		});
	};
	Plank(TEXT("P1 5257 код 0"),  5257, 0,  1017, nullptr);
	Plank(TEXT("P2 5257 код 5"),  5257, 5,  1021, nullptr);
	Plank(TEXT("P3 5257 код 21"), 5257, 21, 1150, nullptr);
	Plank(TEXT("P4 5257 код 28"), 5257, 28, 1157, TEXT("P_planks_5257"));
	Plank(TEXT("P5 5271 код 13"), 5271, 13, 1076, nullptr);
	Plank(TEXT("P6 5271 код 24"), 5271, 24, 1153, TEXT("P_planks_5271"));
	Plank(TEXT("P7 повтор 5271 код 24"), 5271, 24, 1153, nullptr);

	// Соседу — ничего.
	Add(TEXT("N1 отказ соседа"), 3.f, [S]() { S->Arm(1018); S->Refuse5257(kPeer, 2); }, [S]() { return S->El() > 0.4; }, [S]()
	{
		S->Expect(S->WarNow() == S->WarMark && S->ChatNow() == S->ChatMark, TEXT("чужой отказ 5257 код 2: ни плашки, ни строки (эталон — только свой)"));
	});
	// Код 18 — в таблице 0: порт молчит (у оригинала строка loc 0 «Ошибок нет» — дефект данных таблицы, оставлен молчаливым).
	Add(TEXT("N2 код 18"), 3.f, [S]()
	{
		const UAsdaChatSubsystem* C = S->Chat(); S->WarMark = C ? C->DebugWarLineCount() : 0;
		S->Refuse5257(S->Self, 18);
	}, [S]() { return S->El() > 0.4; }, [S]()
	{
		const UAsdaChatSubsystem* C = S->Chat(); const int32 Now = C ? C->DebugWarLineCount() : 0;
		S->Expect(Now <= S->WarMark, FString::Printf(TEXT("код 18: новой плашки нет (строк %d → %d)"), S->WarMark, Now));
	});
	// Ключ 0 — прежний порт: строка системного чата.
	Add(TEXT("K0 ключ 0"), 3.f, [S]()
	{
		AsdaCVarSave::SetForTest(kKey, 0);
		S->Arm(1083); S->Refuse5257(S->Self, 20);
	}, [S]() { return S->El() > 0.4; }, [S]()
	{
		S->Expect(S->ChatNow() == S->ChatMark + 1 && S->WarNow() == S->WarMark,
			FString::Printf(TEXT("ключ 0: код 20 → строка чата loc 1083 (+%d), плашки нет (+%d) — так было до правки"), S->ChatNow() - S->ChatMark, S->WarNow() - S->WarMark));
		AsdaCVarSave::SetForTest(kKey, 1);
	});

	Add(TEXT("итог"), 2.f, [S]() {}, [S]() { return S->El() > 0.5; }, [S]()
	{
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), kCmd, false, LogAsdaSkillFailPlankTest, kPrefix, S->Ok, S->Fail);
	});

	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H; TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get(); const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KA.Delay), false);
	}
	TWeakPtr<FState> WSt = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
	{
		TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		if (!St->Steps.IsValidIndex(St->StepIdx)) { Wd->GetTimerManager().ClearTimer(St->Tick); AsdaSelfTestKit::Quit(Wd, St->bQuit); return; }
		const double Now = FPlatformTime::Seconds();
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaSkillFailPlankTest, Log, TEXT("%s шаг %s"), kPrefix, *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = !Stp.Poll || Stp.Poll();
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KA.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillFailPlank(
	TEXT("asda.SelfTest.SkillFailPlank"),
	TEXT("Самотест канала серверного отказа навыка (5257/5271): плашка как оригинал, ключ asda.Skill.FailNoticePlank. ")
	TEXT("Только офлайн. `out=<папка>` `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.SelfTest.SkillFailPlank"));
		FAsdaSkillFailPlankSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
