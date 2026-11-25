// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВЫБОРА АРМИИ (30.10.2026): `asda.UI.SelfTestFactionSelect [delay=N] [quit]` — окно `CAMPWAR_Win` 44000,
//   подтверждение `CAMPWAR_select` 46000, пакеты 6700/6701/6702/6703. Отчёт `Reports/faction_select/FLOW.md` §6.
//   Входы ТОЛЬКО штатные:
//     • разговор с NPC — `UAsdaNpcDialogSubsystem::OpenForNpcId` (боевое открытие: шлёт 6700 у NPC войны);
//     • окно — кнопкой NPC 30056 (`RunCommandForCheck` = боевой `ExecuteCommand`, разговор закрывается как у эталона);
//     • клики — НАСТОЯЩИЕ события Slate по живой геометрии (`UAsdaWindowSubsystem::SimulateClickAt`);
//     • ESC — общий проход окон (`UAsdaWindowSubsystem::CloseTopmostOnEsc`);
//     • пакеты 6701/6703/6713 — `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` (тот же разбор, что у сервера);
//     • отправки 6700/6702 — БОЕВЫЕ отправители под перехватом (`AsdaSelfTestKit::ArmCapture`, в сеть не уходит ничего).
//   Контрольные пары: NPC войны ↔ обычный NPC (6700 есть/нет), «Отмена» ↔ «ОК» (6702 нет/есть), 6701 нули ↔ численность,
//   звание из 6713 > 0 ↔ ≤ 0 (очки в окне персонажа есть/нули), коды 6703 с нотисом ↔ код без ветки (молча).
//   Сервер не нужен: команда отказывает при живом соединении.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaFactionSelectSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"          // FScreenshotRequest — снимки окон (аргумент `shots`)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFactionSelTest, Log, All);

namespace AsdaFactionSelTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestFactionSelect");   // имя команды (итог, перехват, отказ)

	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaFactionSelTest, Log, TEXT("[армия-самотест] ОК — %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaFactionSelTest, Error, TEXT("[армия-самотест] ПРОВАЛ — %s"), *What); }
		}
	};

	// Перехваченная отправка: опкод и payload (без конверта).
	using FSent = TArray<TPair<int32, TArray<uint8>>>;

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		FRun R;                                   // счётчики
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		float Delay = 0.f;                        // сдвиг старта
		bool bQuit = false;                       // выйти после итога
		TSharedPtr<FSent> Sent;                   // перехваченные исходящие
		uint32 CapGen = 0;                        // поколение перехвата
		TSharedPtr<FAsdaNetSelfScope> SelfScope;  // свой charId на время прогона (RAII возврат)
		int32 SavedCamp = -2;                     // своя армия до прогона (`SelfCampId`)
		int32 SavedPts = 0;                       // очки армии до прогона (последний 6713)
		int32 SavedGrade = 0;                     // звание до прогона (последний 6713)
		FDelegateHandle HSpy;                     // шпион `OnSelfFaction`
		TSharedPtr<TArray<FIntVector>> SpyLog;    // (армия, очки, звание) каждой рассылки `OnSelfFaction`
		FString ShotDir;                          // папка снимков (аргумент `shots`; пусто — без снимков)
	};

	static constexpr int32 kSelfCharId = 920202;   // свой charId на время прогона
	static constexpr int32 kCnt[3] = { 3615, 4382, 2958 };   // численность армий 6701 (как в `Joy1.CampData` dev 30.10)

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Запустить шаг через Sec секунд.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Настоящий клик Slate по центру живой геометрии узла.
	static bool ClickNode(const TSharedRef<FCtx>& C, UWidget* Node, const FString& What)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		FVector2D Mn, Mx;
		const bool bGeo = WM && Node && AsdaUi::WidgetAbsRect(Node, Mn, Mx);
		C->R.Expect(bGeo, FString::Printf(TEXT("клик «%s»: живая геометрия есть"), *What));
		if (bGeo) { WM->SimulateClickAt((Mn + Mx) * 0.5f, /*HoldFrames*/ 3); }
		return bGeo;
	}
	// Сколько перехвачено опкода Op; OutLast — payload последнего.
	using AsdaSelfTestCtx::CountOp;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Тело 6701 (44 б): численность армий i16 по +19/+21/+23, прочие поля — нули.
	static TArray<uint8> Make6701(int32 A, int32 B, int32 Cc)
	{
		TArray<uint8> P; P.SetNumZeroed(44);
		AsdaBytes::WrI16(P.GetData() + 19, A);
		AsdaBytes::WrI16(P.GetData() + 21, B);
		AsdaBytes::WrI16(P.GetData() + 23, Cc);
		return P;
	}
	// Тело 6703 (3 б): код, армия.
	static TArray<uint8> Make6703(int32 Code, int32 Squad)
	{
		TArray<uint8> P; P.SetNumZeroed(3);
		P[0] = (uint8)Code;
		AsdaBytes::WrI16(P.GetData() + 1, Squad);
		return P;
	}
	// Тело 6713 (7 б): i16 армия, i32 очки, i8 звание.
	static TArray<uint8> Make6713(int32 Fid, int32 Pts, int32 Gr)
	{
		TArray<uint8> P; P.SetNumZeroed(7);
		AsdaBytes::WrI16(P.GetData(), Fid);
		AsdaBytes::WrI32(P.GetData() + 2, Pts);
		P[6] = (uint8)(int8)Gr;
		return P;
	}
	// Снимок экрана с интерфейсом (только при аргументе `shots`): <ShotDir>/<Name>.png.
	static void Shot(const TSharedRef<FCtx>& C, const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		const FString File = C->ShotDir / FString::Printf(TEXT("%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaFactionSelTest, Log, TEXT("[армия-самотест] снимок заказан → %s"), *File);
	}
	// Loc-строка языка сессии (оракул текстов окна — та же таблица, что у игры).
	static FString L(const TSharedRef<FCtx>& C, int32 Id) { return AsdaNotice::Loc(GIOf(C), Id); }

	static void Finish(TSharedRef<FCtx> C);

	// Проверки содержимого открытого окна 44000 (Tag — подпись шага).
	static void CheckWindow(const TSharedRef<FCtx>& C, const TCHAR* Tag, const TCHAR* const Pct[3], const float Frac[3])
	{
		UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
		FRun& R = C->R;
		R.Expect(F->IsSelectOpen(), FString::Printf(TEXT("%s: окно 44000 открыто"), Tag));
		R.Expect(F->GetText(44001) == L(C, 10001), FString::Printf(TEXT("%s: заголовок 44001 = loc 10001 «%s»"), Tag, *F->GetText(44001)));
		R.Expect(F->GetText(44099) == L(C, 5035), FString::Printf(TEXT("%s: «Закрыть» 44099 = loc 5035"), Tag));
		for (int32 I = 0; I < 3; ++I)
		{
			R.Expect(F->GetText(44027 + 10 * I) == L(C, 10191 + I),
				FString::Printf(TEXT("%s: имя строки %d = loc %d «%s»"), Tag, I, 10191 + I, *F->GetText(44027 + 10 * I)));
			R.Expect(F->GetText(44025 + 10 * I) == L(C, 10002),
				FString::Printf(TEXT("%s: кнопка строки %d = loc 10002 «%s»"), Tag, I, *F->GetText(44025 + 10 * I)));
			R.Expect(F->GetText(44029 + 10 * I) == Pct[I],
				FString::Printf(TEXT("%s: процент строки %d = «%s» (ждём «%s»)"), Tag, I, *F->GetText(44029 + 10 * I), Pct[I]));
			R.Expect(FMath::IsNearlyEqual(F->GetBarFrac(I), Frac[I], 0.001f),
				FString::Printf(TEXT("%s: полоса строки %d = %.4f (ждём %.4f)"), Tag, I, F->GetBarFrac(I), Frac[I]));
			R.Expect(F->IsMarkShown(44020 + 10 * I), FString::Printf(TEXT("%s: знак армии строки %d нарисован"), Tag, I));
		}
	}

	// ═══════════════════════════ ПРОГОН ═══════════════════════════
	static void Run(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
		UAsdaNpcDialogSubsystem* Npc = Sub<UAsdaNpcDialogSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		FRun& R = C->R;
		if (!Net || !F || !Npc || !WM || !WS)
		{
			R.Expect(false, TEXT("подсистемы сети/армии/NPC/окон/мира есть"));
			Finish(C);
			return;
		}
		// Своё состояние: charId, армия, очки и звание.
		C->SelfScope = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kSelfCharId;
		C->SavedCamp = WS->GetSelfCampId();
		C->SavedPts = F->GetLastSelfPoints();
		C->SavedGrade = F->GetLastSelfGrade();
		// Шпион общей точки «своя армия» (6713 и 6703 код 1 рассылают одно и то же).
		C->SpyLog = MakeShared<TArray<FIntVector>>();
		TSharedPtr<TArray<FIntVector>> Spy = C->SpyLog;
		C->HSpy = Net->OnSelfFaction.AddLambda([Spy](int32 Fid, int32 Pts, int32 Gr) { Spy->Add(FIntVector(Fid, Pts, Gr)); });
		// Перехват ВСЕГО исходящего (в сеть ничего не уходит).
		C->Sent = MakeShared<FSent>();
		TSharedPtr<FSent> S = C->Sent;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* Body, int32 Len, bool, TCHAR)
		{
			TArray<uint8> B; B.Append(Body, Len);
			S->Add(TPair<int32, TArray<uint8>>((int32)Op, MoveTemp(B)));
		}, 0, kCmd);
		if (!C->CapGen) { R.Expect(false, TEXT("перехват отправок взведён")); Finish(C); return; }
		F->ClearTrace();

		// ── A. NPC войны: таблица `itemIsCategory133or179_184` ──
		R.Expect(UAsdaFactionSelectSubsystem::IsWarInfoNpc(133) && UAsdaFactionSelectSubsystem::IsWarInfoNpc(179)
			&& UAsdaFactionSelectSubsystem::IsWarInfoNpc(180) && UAsdaFactionSelectSubsystem::IsWarInfoNpc(184),
			TEXT("A0: NPC 133/179/180/184 — NPC войны"));
		R.Expect(!UAsdaFactionSelectSubsystem::IsWarInfoNpc(181) && !UAsdaFactionSelectSubsystem::IsWarInfoNpc(106),
			TEXT("A0: NPC 181 и 106 — не NPC войны (пара)"));
		// 6701 с нулями — исходное «ничего не известно».
		Net->DebugFeedPacket(6701, Make6701(0, 0, 0));

		At(C, 0.3f, [C]()
		{
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			UAsdaNpcDialogSubsystem* Npc = Sub<UAsdaNpcDialogSubsystem>(C);
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			FRun& R = C->R;
			R.Expect(F->HasMainInfo() && F->GetSquadCount(0) == 0, TEXT("A1: 6701 с нулями принят (HandlePacket → очередь → Tick)"));
			// A2. Разговор с NPC 180 (Альпия, «Select Team») — 6700 (charId, режим 0).
			const int32 Before = CountOp(C, 6700);
			const bool bOpened = Npc->OpenForNpcId(180);
			TArray<uint8> P;
			const int32 After = CountOp(C, 6700, &P);
			R.Expect(bOpened, TEXT("A2: разговор с NPC 180 открыт боевым путём"));
			R.Expect(After == Before + 1, FString::Printf(TEXT("A2: разговор с NPC войны 180 → ушёл 6700 (было %d, стало %d)"), Before, After));
			R.Expect(P.Num() == 5 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && P[4] == 0,
				FString::Printf(TEXT("A2: 6700 payload 5 б = charId %d + режим 0 (длина %d)"), kSelfCharId, P.Num()));
			// B. Кнопка 30056 в открытом разговоре: окно 44000, разговор закрыт (эталон `UI_resetAllWindowsAndLeave_5045`).
			Npc->RunCommandForCheck(30056, -1);
			R.Expect(!Npc->IsDialogOpen(), TEXT("B1: 30056 закрыл разговор (у заглушки разговор оставался — причина повтора строки)"));
			static const TCHAR* const kZeroPct[3] = { TEXT("0.00%"), TEXT("0.00%"), TEXT("0.00%") };
			static const float kZeroFrac[3] = { 0.f, 0.f, 0.f };
			CheckWindow(C, TEXT("B1 (6701 нули)"), kZeroPct, kZeroFrac);
			R.Expect(F->GetNoticeTrace().Num() == 0, TEXT("B1: никаких нотисов при открытии (заглушки нет)"));
			// A3. Контроль: обычный NPC (106 «Статуя души») — 6700 не уходит.
			const int32 B3 = CountOp(C, 6700);
			if (Npc->OpenForNpcId(106)) { Npc->Close(); }
			R.Expect(CountOp(C, 6700) == B3, TEXT("A3: разговор с NPC 106 — 6700 НЕ ушёл (пара)"));
		});

		At(C, 0.8f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			// C1. X 44002 — настоящий клик.
			ClickNode(C, F->GetHitWidget(44002), TEXT("X 44002"));
		});
		At(C, 1.2f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			C->R.Expect(!F->IsSelectOpen(), TEXT("C1: X 44002 закрыл окно 44000"));
			// D. Численность армий из 6701 — проценты при следующем показе.
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6701, Make6701(kCnt[0], kCnt[1], kCnt[2]));
		});
		At(C, 1.5f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			C->R.Expect(F->GetSquadCount(0) == kCnt[0] && F->GetSquadCount(1) == kCnt[1] && F->GetSquadCount(2) == kCnt[2],
				TEXT("D1: 6701 — численность армий 3615/4382/2958 из +19/+21/+23"));
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30056, -1);
			// Оракул: 3615/10955 = 32.9986…% → «33.00%», 4382/10955 = 40.0000% → «40.00%», 2958/10955 = 27.0013…% → «27.00%».
			static const TCHAR* const kPct[3] = { TEXT("33.00%"), TEXT("40.00%"), TEXT("27.00%") };
			const double Tot = (double)(kCnt[0] + kCnt[1] + kCnt[2]);
			const float Frac[3] = { (float)(kCnt[0] / Tot), (float)(kCnt[1] / Tot), (float)(kCnt[2] / Tot) };
			CheckWindow(C, TEXT("D1 (6701 численность)"), kPct, Frac);
		});
		At(C, 1.7f, [C]() { Shot(C, TEXT("faction_44000")); });
		At(C, 1.9f, [C]()
		{
			// E. ESC — общий проход окон: 44000 в эталонной таблице ESC.
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			C->R.Expect(WM->IsEscCloseWindow(FName(TEXT("CAMPWAR_Win"))), TEXT("E1: 44000 в эталонной таблице ESC"));
			WM->CloseTopmostOnEsc();
			C->R.Expect(!F->IsSelectOpen(), TEXT("E1: ESC закрыл окно 44000"));
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30056, -1);
		});
		At(C, 2.3f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			C->R.Expect(F->IsSelectOpen(), TEXT("E2: 30056 снова открыл окно"));
			ClickNode(C, F->GetHitWidget(44099), TEXT("«Закрыть» 44099"));
		});
		At(C, 2.7f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			C->R.Expect(!F->IsSelectOpen(), TEXT("E2: «Закрыть» 44099 закрыл окно 44000"));
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30056, -1);
		});
		At(C, 3.0f, [C]()
		{
			// F. «выбрать» строки 1 → подтверждение 46000.
			ClickNode(C, Sub<UAsdaFactionSelectSubsystem>(C)->GetHitWidget(44035), TEXT("«выбрать» 44035 (Тьма)"));
		});
		At(C, 3.4f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			FRun& R = C->R;
			R.Expect(F->IsConfirmOpen() && F->GetPendingSquad() == 1, FString::Printf(TEXT("F1: подтверждение 46000 открыто, армия %d (ждём 1)"), F->GetPendingSquad()));
			R.Expect(F->GetText(46020) == L(C, 10192), FString::Printf(TEXT("F1: имя 46020 = loc 10192 «%s»"), *F->GetText(46020)));
			R.Expect(F->GetText(46030) == L(C, 10003), TEXT("F1: текст 46030 = loc 10003 (предупреждение «только один раз»)"));
			R.Expect(F->GetText(46040) == L(C, 2288) && F->GetText(46050) == L(C, 2289), TEXT("F1: кнопки 46040/46050 = loc 2288/2289"));
			R.Expect(F->IsMarkShown(46012), TEXT("F1: знак армии 46012 нарисован"));
			R.Expect(CountOp(C, 6702) == 0, TEXT("F1: «выбрать» пакета не шлёт"));
			Shot(C, TEXT("faction_46000"));
			ClickNode(C, F->GetHitWidget(46050), TEXT("Отмена 46050"));
		});
		At(C, 3.8f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			C->R.Expect(!F->IsConfirmOpen() && F->IsSelectOpen(), TEXT("F2: Отмена закрыла только 46000, окно 44000 открыто"));
			C->R.Expect(CountOp(C, 6702) == 0, TEXT("F2: Отмена — 6702 НЕ ушёл (пара)"));
			ClickNode(C, F->GetHitWidget(44045), TEXT("«выбрать» 44045 (Хаос)"));
		});
		At(C, 4.2f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			C->R.Expect(F->IsConfirmOpen() && F->GetPendingSquad() == 2 && F->GetText(46020) == L(C, 10193),
				TEXT("F3: «выбрать» строки 2 → 46000 с армией 2 (loc 10193)"));
			ClickNode(C, F->GetHitWidget(46040), TEXT("ОК 46040"));
		});
		At(C, 4.6f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			FRun& R = C->R;
			TArray<uint8> P;
			const int32 N = CountOp(C, 6702, &P);
			R.Expect(N == 1, FString::Printf(TEXT("G1: ОК → ушёл ровно один 6702 (%d)"), N));
			R.Expect(P.Num() == 6 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && AsdaBytes::RdI16(P.GetData() + 4) == 2,
				FString::Printf(TEXT("G1: 6702 payload 6 б = charId %d + армия 2 (длина %d)"), kSelfCharId, P.Num()));
			R.Expect(!F->IsConfirmOpen() && F->IsSelectOpen(), TEXT("G1: ОК скрыл 46000, окно 44000 осталось (как эталон)"));
			// H. Ответ 6703 код 1 при звании > 0: очки и звание — из 6713.
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->DebugFeedPacket(6713, Make6713(-1, 500, 3));
			C->SpyLog->Reset();
			Net->DebugFeedPacket(6703, Make6703(1, 2));
		});
		At(C, 4.9f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
			FRun& R = C->R;
			const TArray<FIntVector>& Sp = *C->SpyLog;
			R.Expect(Sp.Num() == 1 && Sp[0] == FIntVector(2, 500, 3),
				FString::Printf(TEXT("H1: 6703 код 1 → своя армия (2, очки 500, звание 3) в общую точку 6713 (рассылок %d)"), Sp.Num()));
			R.Expect(WS->GetSelfCampId() == 2, FString::Printf(TEXT("H1: мир: своя армия = 2 (%d)"), WS->GetSelfCampId()));
			R.Expect(F->GetLastSelfPoints() == 500 && F->GetLastSelfGrade() == 3, TEXT("H1: очки/звание 6713 не перетёрты (эталон `this+2946/2950` не трогает)"));
			const int32 NT = F->GetNoticeTrace().Num();
			const FString Want = L(C, 10008).Replace(TEXT("[ss1]"), *L(C, 10193));
			R.Expect(NT == 1 && F->GetNoticeTrace()[0] == 10008, TEXT("H1: нотис 10008"));
			R.Expect(F->GetLastNoticeText() == Want && F->GetLastNoticeText().Contains(L(C, 10193)),
				FString::Printf(TEXT("H1: текст «%s» (ждём «%s»)"), *F->GetLastNoticeText(), *Want));
			Shot(C, TEXT("faction_6703_ok"));
			// H2. Звание ≤ 0 → в окно персонажа уходят нули (`CFactionUI_UpdateInfo(армия, 0, 0)`).
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			Net->DebugFeedPacket(6713, Make6713(-1, 77, 0));
			C->SpyLog->Reset();
			Net->DebugFeedPacket(6703, Make6703(1, 0));
		});
		At(C, 5.2f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			FRun& R = C->R;
			const TArray<FIntVector>& Sp = *C->SpyLog;
			R.Expect(Sp.Num() == 1 && Sp[0] == FIntVector(0, 0, 0),
				FString::Printf(TEXT("H2: звание 0 → (армия 0, очки 0, звание 0) (рассылок %d)"), Sp.Num()));
			R.Expect(Sub<UAsdaWorldSubsystem>(C)->GetSelfCampId() == 0, TEXT("H2: мир: своя армия = 0"));
			// I. Отказы: коды 0/2/3/4/5 — свой нотис каждый; код 9 — молча (`default: return`).
			F->ClearTrace();
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			C->SpyLog->Reset();
			for (int32 Code : { 0, 2, 3, 4, 5, 9 }) { Net->DebugFeedPacket(6703, Make6703(Code, 1)); }
		});
		At(C, 5.5f, [C]()
		{
			UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
			FRun& R = C->R;
			const TArray<int32> Want = { 10009, 10005, 10004, 10006, 10010 };
			R.Expect(F->GetNoticeTrace() == Want, FString::Printf(TEXT("I1: коды 0/2/3/4/5 → нотисы 10009/10005/10004/10006/10010, код 9 молча (нотисов %d)"),
				F->GetNoticeTrace().Num()));
			R.Expect(F->GetLastNoticeText() == L(C, 10010), TEXT("I1: последний текст = loc 10010"));
			R.Expect(C->SpyLog->Num() == 0, TEXT("I1: отказы свою армию не трогают"));
			Finish(C);
		});
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaFactionSelectSubsystem* F = Sub<UAsdaFactionSelectSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		if (F)
		{
			if (F->IsConfirmOpen()) { F->OnConfirmCancel(); }
			if (F->IsSelectOpen()) { F->CloseSelectWindow(); }
			F->ClearTrace();
		}
		if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест выбора армии: конец")); }
		if (Net)
		{
			if (C->HSpy.IsValid()) { Net->OnSelfFaction.Remove(C->HSpy); }
			// Своя армия, очки и звание — как до прогона (общая точка 6713).
			Net->OnSelfFaction.Broadcast(C->SavedCamp, C->SavedPts, C->SavedGrade);
			if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); }
		}
		if (C->SelfScope) { C->SelfScope->Restore(); }
		if (UAsdaNpcDialogSubsystem* D = Sub<UAsdaNpcDialogSubsystem>(C)) { if (D->IsDialogOpen()) { D->Close(); } }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaFactionSelTest, TEXT("[армия-самотест]"), C->R.Ok, C->R.Fail);
	}

	// asda.UI.SelfTestFactionSelect [delay=N] [quit] — офлайн: спавн своего павна на alpen (если его нет), через 4 с — прогон.
	static FAutoConsoleCommandWithWorldAndArgs GAsdaFactionSelSelfTest(
		TEXT("asda.UI.SelfTestFactionSelect"),
		TEXT("asda.UI.SelfTestFactionSelect [delay=N] [shots] [quit] — ОФЛАЙН-самотест выбора армии: 6700 при разговоре с NPC войны, "
		     "кнопка 30056 → окно 44000 (проценты из 6701), «выбрать» → 46000, Отмена/ОК (6702), ответ 6703 (все коды), X/«Закрыть»/ESC — "
		     "клики Slate по живой геометрии, пакеты HandlePacket"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			if (!W) { return; }
			TSharedRef<FCtx> C = MakeShared<FCtx>();
			C->W = W;
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			C->bQuit = KitArgs.bQuit;
			C->Delay = KitArgs.Delay;
			if (KitArgs.Has(TEXT("shots")))
			{
				C->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/faction_select/shots"));
			}
			// ТОЛЬКО ОФЛАЙН: прогон шлёт 6700/6702 боевыми отправителями.
			static const uint16 kGuardOps[] = { 6700, 6702 };
			if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			auto Start = [C]()
			{
				UWorld* Wd = C->W.Get();
				if (!Wd || !GEngine) { return; }
				if (AsdaSelfTestKit::RefuseDeferred(Wd, kCmd, C->bQuit)) { return; }
				APlayerController* P = Wd->GetFirstPlayerController();
				if (!P || !P->GetPawn())
				{
					GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 FactionSel"));
				}
				FTimerHandle H;
				Wd->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Run(C); }), 4.f, false);
			};
			if (C->Delay <= 0.f) { Start(); return; }
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), C->Delay, false);
		}));
}

#endif // !UE_BUILD_SHIPPING
