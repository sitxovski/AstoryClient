// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «БИТВЫ АРМИЙ» — РЕГИСТРАЦИЯ И ВХОД (30.10.2026): `asda.UI.SelfTestCampWarReg [delay=N] [shots] [quit]` — окна
//   47000 / 49530 / 49540 / 48099, пакеты 6701, 6704–6715, 6740. Разбор — `Reports/faction_select/REG_SPEC.md`, FLOW.md.
//   Входы ТОЛЬКО штатные:
//     • окно — кнопками NPC 30057/30074/30075 (`RunCommandForCheck` = боевой `ExecuteCommand`);
//     • клики — НАСТОЯЩИЕ события Slate по живой геометрии (`SimulateClickAt`): кнопки окон, строки-панели (опрос мыши подсистемы),
//       «Да» двухкнопочного окна-сообщения `TWOBUT_MSGBOX`; ESC — общий проход окон (`CloseTopmostOnEsc`);
//     • пакеты 6701/6705/6707/6709/6710/6711/6713/6740 — `DebugFeedPacket` → `HandlePacket`;
//     • отправки 6700/6704/6706/6708/6712/6714 — БОЕВЫЕ отправители под перехватом (в сеть не уходит ничего).
//   Контрольные пары: без выбранной строки ↔ со строкой (нотис 10049 / пакет 6706), «Нет» ↔ «Да», отсчёт идёт ↔ истёк (6708(1)
//   без вопроса), 6711 [0]==1 ↔ ≠1 (окно 48099 / нотис 10044), код 6707 с веткой ↔ без ветки (молча).
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaCampWarSubsystem.h"
#include "UI/AsdaCampWarBattleSubsystem.h"   // ★ 30.10.2026: 5055 теперь включает HUD боя — снять его в конце прогона
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapTable.h"
#include "World/AsdaWorldSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"          // FScreenshotRequest — снимки окон (аргумент `shots`)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCampWarRegTest, Log, All);

namespace AsdaCampWarRegTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestCampWarReg");   // имя команды

	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCampWarRegTest, Log, TEXT("[битва-самотест] ОК — %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCampWarRegTest, Error, TEXT("[битва-самотест] ПРОВАЛ — %s"), *What); }
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
		FString ShotDir;                          // папка снимков (`shots`)
		TSharedPtr<FSent> Sent;                   // перехваченные исходящие
		uint32 CapGen = 0;                        // поколение перехвата
		TSharedPtr<FAsdaNetSelfScope> SelfScope;  // свой charId/номер (RAII)
		uint8 SavedRawSlot = 0;                   // слот персонажа до прогона
		int32 SavedCamp = -2;                     // своя армия до прогона
		FDelegateHandle HSpy;                     // шпион `OnSelfFaction`
		TSharedPtr<TArray<FIntVector>> SpyLog;    // рассылки «своя армия»
		float T = 0.f;                            // время следующего шага
	};

	static constexpr int32 kSelfCharId = 930303;   // свой charId на время прогона
	static constexpr uint8 kSlot = 11;             // свой слот персонажа (iCharIndex)

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static FString L(const TSharedRef<FCtx>& C, int32 Id) { return AsdaNotice::Loc(GIOf(C), Id); }

	// Следующий шаг через Step секунд после предыдущего.
	using AsdaSelfTestCtx::Then;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
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
	// Узел окна-сообщения по имени.
	static UWidget* MsgNode(const TSharedRef<FCtx>& C, const TCHAR* Win, const TCHAR* Node)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* W = WM ? WM->FindWindow(FName(Win)) : nullptr;
		return W ? W->GetWidgetFromName(FName(Node)) : nullptr;
	}
	// Сколько перехвачено опкода Op; OutLast — payload последнего.
	using AsdaSelfTestCtx::CountOp;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Снять показанные окна-сообщения (ONEBUT модален — перекрыл бы клики по окнам кампвара на следующем шаге).
	static void Dismiss(const TSharedRef<FCtx>& C) { if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->ClearMsgBoxQueue(TEXT("самотест «Битвы армий»: шаг")); } }
	// Снимок экрана с интерфейсом (только с `shots`).
	static void Shot(const TSharedRef<FCtx>& C, const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		FScreenshotRequest::RequestScreenshot(C->ShotDir / FString::Printf(TEXT("%s.png"), Name), /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
	// Тело 6701 (44 б) по раскладке `Recv_ClanInfo`.
	static TArray<uint8> Make6701(int32 Day, int32 SH, int32 SM, int32 EH, int32 EM, int32 Games, const int32 Regist[3], int32 Region,
		const int32 Team[3], const int32 Win[3], const int32 Lose[3], int32 Total, int32 Today, int32 LvMin, int32 LvMax)
	{
		TArray<uint8> P; P.SetNumZeroed(44);
		uint8* D = P.GetData();
		AsdaBytes::WrI16(D + 0, Day); AsdaBytes::WrI16(D + 2, SH); AsdaBytes::WrI16(D + 4, SM); AsdaBytes::WrI16(D + 6, EH); AsdaBytes::WrI16(D + 8, EM);
		AsdaBytes::WrI16(D + 10, Games);
		for (int32 I = 0; I < 3; ++I)
		{
			AsdaBytes::WrI16(D + 12 + 2 * I, Regist[I]); AsdaBytes::WrI16(D + 19 + 2 * I, Team[I]);
			AsdaBytes::WrI16(D + 25 + 2 * I, Win[I]);    AsdaBytes::WrI16(D + 31 + 2 * I, Lose[I]);
		}
		D[18] = (uint8)Region;
		AsdaBytes::WrI16(D + 37, Total); D[39] = (uint8)Today;
		AsdaBytes::WrI16(D + 40, LvMin); AsdaBytes::WrI16(D + 42, LvMax);
		return P;
	}
	// Тело 6707 (7 б): код, k, j, RegistIndex, перед тобой.
	static TArray<uint8> Make6707(int32 Code, int32 K, int32 J, int32 RIdx, int32 Wait)
	{
		TArray<uint8> P; P.SetNumZeroed(7);
		P[0] = (uint8)Code; P[1] = (uint8)K; P[2] = (uint8)J;
		AsdaBytes::WrI16(P.GetData() + 3, RIdx); AsdaBytes::WrI16(P.GetData() + 5, Wait);
		return P;
	}
	// Тело из двух байт (6709, 6740).
	static TArray<uint8> B2(int32 A, int32 B) { TArray<uint8> P; P.Add((uint8)A); P.Add((uint8)B); return P; }
	// Тело 6710 (5 б): k, RegistIndex, перед тобой.
	static TArray<uint8> Make6710(int32 K, int32 Wait) { TArray<uint8> P; P.SetNumZeroed(5); P[0] = (uint8)K; AsdaBytes::WrI16(P.GetData() + 3, Wait); return P; }
	// Тело 6711 (5 б): результат, k, GameIndex, RegistIndex.
	static TArray<uint8> Make6711(int32 Res, int32 K) { TArray<uint8> P; P.SetNumZeroed(5); P[0] = (uint8)Res; P[1] = (uint8)K; return P; }
	// Тело 6713 (7 б): армия, очки, звание.
	static TArray<uint8> Make6713(int32 Fid, int32 Pts, int32 Gr)
	{
		TArray<uint8> P; P.SetNumZeroed(7); AsdaBytes::WrI16(P.GetData(), Fid); AsdaBytes::WrI32(P.GetData() + 2, Pts); P[6] = (uint8)(int8)Gr; return P;
	}
	// Тело 6705 (58 б): код + запись предмета 57 б (код предмета, сумка, ячейка, количество).
	static TArray<uint8> Make6705(int32 Code, int32 Item, int32 Bag, int32 Cell, int32 Amount)
	{
		TArray<uint8> P; P.SetNumZeroed(58); P[0] = (uint8)Code;
		AsdaBytes::WrI32(P.GetData() + 1, Item); P[5] = (uint8)Bag; AsdaBytes::WrI16(P.GetData() + 6, Cell); AsdaBytes::WrI32(P.GetData() + 10, Amount);
		return P;
	}

	static void Finish(TSharedRef<FCtx> C);

	// ═══════════════════════════ ПРОГОН ═══════════════════════════
	static void Run(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
		UAsdaNpcDialogSubsystem* Npc = Sub<UAsdaNpcDialogSubsystem>(C);
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UAsdaWorldSubsystem* WS = Sub<UAsdaWorldSubsystem>(C);
		FRun& R = C->R;
		if (!Net || !CW || !Npc || !WM || !WS) { R.Expect(false, TEXT("подсистемы сети/кампвара/NPC/окон/мира есть")); Finish(C); return; }
		C->SelfScope = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		C->SavedRawSlot = Net->LastSelectedRawSlot;
		C->SavedCamp = WS->GetSelfCampId();
		Net->SelectedCharId = kSelfCharId;
		Net->LastSelectedRawSlot = kSlot;
		C->SpyLog = MakeShared<TArray<FIntVector>>();
		TSharedPtr<TArray<FIntVector>> Spy = C->SpyLog;
		C->HSpy = Net->OnSelfFaction.AddLambda([Spy](int32 F, int32 P, int32 G) { Spy->Add(FIntVector(F, P, G)); });
		C->Sent = MakeShared<FSent>();
		TSharedPtr<FSent> S = C->Sent;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* Body, int32 Len, bool, TCHAR)
		{
			TArray<uint8> B; B.Append(Body, Len); S->Add(TPair<int32, TArray<uint8>>((int32)Op, MoveTemp(B)));
		}, 0, kCmd);
		if (!C->CapGen) { R.Expect(false, TEXT("перехват отправок взведён")); Finish(C); return; }
		CW->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);   // модель — как после входа в мир
		CW->ClearTrace();
		const UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C);
		const FAsdaMapRow* Map19 = LM ? LM->GetMapRow(19) : nullptr;
		const FString Battle = Map19 ? Map19->DirName : FString();
		R.Expect(Battle == TEXT("battlefield"), FString::Printf(TEXT("A0: имя битвы = служебное имя карты 19 «%s»"), *Battle));
		R.Expect(CW->GetBaseRow(0) && CW->GetBaseRow(0)->Field10 == 30 && CW->GetBaseRow(2) && !CW->GetBaseRow(3),
			TEXT("A0: DT_CampWarBase — 3 строки, @16 = 30"));
		// Своя армия 1 (общая точка 6713).
		Net->DebugFeedPacket(6713, Make6713(1, 0, 0));
		// A1. Без выбранной строки «Участвовать» — нотис 10049, пакета нет.
		CW->OnJoinParticipate();
		R.Expect(CountOp(C, 6706) == 0 && CW->GetNoticeTrace().Num() == 1 && CW->GetNoticeTrace()[0] == 10049,
			TEXT("A1: «Участвовать» без строки (+0x231C = −1) → ONEBUT 10049, 6706 НЕ ушёл"));
		CW->ClearTrace();
		// A2. 6701 — сведения войны региона 0.
		static const int32 kRegist[3] = { 12, 8, 4 }, kTeam[3] = { 3615, 4382, 2958 }, kWin[3] = { 5, 3, 2 }, kLose[3] = { 1, 2, 3 };
		Net->DebugFeedPacket(6701, Make6701(2, 19, 0, 19, 20, 1, kRegist, 0, kTeam, kWin, kLose, 10, 4, 24, 39));

		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			FRun& R = C->R;
			R.Expect(CW->HasMainInfo() && CW->GetMapType() == 0 && CW->GetTeamCount(1) == 4382, TEXT("A2: 6701 разобран (регион 0, армия 1 = 4382)"));
			Dismiss(C);
			// B. Кнопка NPC 30057 — окно 47000, разговор закрыт.
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30057, -1);
			R.Expect(CW->IsJoinOpen(), TEXT("B1: 30057 открыл окно 47000 «Битва армий»"));
			R.Expect(!Sub<UAsdaNpcDialogSubsystem>(C)->IsDialogOpen(), TEXT("B1: разговор закрыт (эталон `UI_resetAllWindowsAndLeave_5045`)"));
			struct FCap { int32 Id; int32 Loc; };
			static const FCap kCaps[] = { { 47030, 10022 }, { 47070, 10023 }, { 47050, 10024 }, { 47090, 10025 }, { 47061, 111302 },
				{ 47405, 10048 }, { 47600, 10030 }, { 47601, 10028 }, { 47700, 5035 }, { 47130, 10026 }, { 47110, 10191 }, { 47210, 10192 }, { 47310, 10193 } };
			for (const FCap& X : kCaps)
			{
				R.Expect(CW->GetText(X.Id) == L(C, X.Loc), FString::Printf(TEXT("B1: %d = loc %d «%s»"), X.Id, X.Loc, *CW->GetText(X.Id)));
			}
			R.Expect(CW->GetText(47040) == TEXT("10"), FString::Printf(TEXT("B1: 47040 «[sd1]» всего битв = «%s» (ждём 10)"), *CW->GetText(47040)));
			R.Expect(CW->GetText(47080) == TEXT("5"), FString::Printf(TEXT("B1: 47080 «[sd1]» сегодня + 1 = «%s» (ждём 5)"), *CW->GetText(47080)));
			R.Expect(CW->GetText(47060) == TEXT("19:00") && CW->GetText(47095) == TEXT("19:20"), TEXT("B1: начало/конец «19:00»/«19:20»"));
			R.Expect(CW->GetText(47062) == TEXT("24~39"), FString::Printf(TEXT("B1: уровни «%s» (ждём 24~39)"), *CW->GetText(47062)));
			R.Expect(CW->GetText(47096) == L(C, 10212) && CW->GetText(47097) == L(C, 10212), TEXT("B1: день недели 2 → loc 10212 (оба поля)"));
			static const TCHAR* kPct[3] = { TEXT("50.00%"), TEXT("30.00%"), TEXT("20.00%") };
			static const TCHAR* kCnt[3] = { TEXT("12/30"), TEXT("8/30"), TEXT("4/30") };
			static const float kFrac[3] = { 0.5f, 0.3f, 0.2f };
			for (int32 I = 0; I < 3; ++I)
			{
				R.Expect(CW->GetText(47140 + 100 * I) == kPct[I], FString::Printf(TEXT("B1: «Шанс» армии %d = «%s» (ждём %s)"), I, *CW->GetText(47140 + 100 * I), kPct[I]));
				R.Expect(FMath::IsNearlyEqual(CW->GetBarFrac(I), kFrac[I], 0.001f), FString::Printf(TEXT("B1: полоса армии %d = %.3f"), I, CW->GetBarFrac(I)));
				R.Expect(CW->GetText(47120 + 100 * I) == kCnt[I], FString::Printf(TEXT("B1: заявки армии %d «%s» (ждём %s)"), I, *CW->GetText(47120 + 100 * I), kCnt[I]));
				R.Expect(CW->IsShown(47100 + 100 * I), FString::Printf(TEXT("B1: знак армии %d нарисован"), I));
			}
			R.Expect(CW->GetText(47432) == TEXT("1") && CW->GetText(47401) == TEXT("battlefield"), TEXT("B1: строка 1 списка битв — «1», «battlefield»"));
			R.Expect(CW->GetText(47402) == FString::Printf(TEXT("<FC=ffffffff>%s</F>"), *L(C, 10040)), FString::Printf(TEXT("B1: загруженность «%s»"), *CW->GetText(47402)));
			R.Expect(CW->GetText(47433).IsEmpty(), TEXT("B1: строка 2 пуста (битв в регионе 1)"));
			R.Expect(CW->GetSelRow() == 0 && CW->IsShown(47430), TEXT("B1: рамка 47430 на строке 0 (+0x231C = 0)"));
			R.Expect(!CW->IsShown(47101) && !CW->IsShown(47425), TEXT("B1: строка не затемнена, значка заявки нет"));
			Shot(C, TEXT("campwar_47000"));
		});
		Then(C, 0.4f, [C]()
		{
			// C. Щелчок по строке списка битв (опрос мыши) → 6700 с режимом = строка.
			ClickNode(C, Sub<UAsdaCampWarSubsystem>(C)->GetHitWidget(47400), TEXT("строка 47400"));
		});
		Then(C, 0.4f, [C]()
		{
			TArray<uint8> P;
			C->R.Expect(CountOp(C, 6700, &P) == 1 && P.Num() == 5 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && P[4] == 0,
				TEXT("C1: щелчок строки → 6700 (charId, режим 0)"));
			// D. «Участвовать».
			ClickNode(C, Sub<UAsdaCampWarSubsystem>(C)->GetHitWidget(47600), TEXT("«Участвовать» 47600"));
		});
		Then(C, 0.4f, [C]()
		{
			TArray<uint8> P;
			C->R.Expect(CountOp(C, 6706, &P) == 1 && P.Num() == 8 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && P[4] == kSlot
				&& AsdaBytes::RdI16(P.GetData() + 5) == 1 && P[7] == 0, TEXT("D1: 6706 = charId, слот 11, армия 1, строка 0"));
			// E. 6707 код 2 (очередь).
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6707, Make6707(2, 0, 0, 5, 3));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			FRun& R = C->R;
			R.Expect(CW->IsIngShown() && CW->GetText(49530) == L(C, 10032), TEXT("E1: 6707 код 2 → кнопка 49530 «Ожидание начала столкновения»"));
			R.Expect(CW->GetNoticeTrace().Num() == 1 && CW->GetNoticeTrace()[0] == 10152, TEXT("E1: нотис 10152"));
			R.Expect(CW->GetRegGameIndex(0) == 0 && CW->GetRegIndex(0) == 5 && CW->GetWaitBefore(0) == 3 && CW->GetLastRegIndex() == 5,
				TEXT("E1: заявка региона 0 записана (GameIndex 0, RegistIndex 5, перед тобой 3)"));
			R.Expect(CW->GetText(47403) == L(C, 10029) && CW->IsShown(47425), TEXT("E1: строка 1 «Подать заявку» + значок"));
			CW->ClearTrace();
			Shot(C, TEXT("campwar_47000_applied"));
			Dismiss(C);
			// F. «Вход на поле боя».
			ClickNode(C, CW->GetHitWidget(47601), TEXT("«Вход на поле боя» 47601"));
		});
		Then(C, 0.4f, [C]()
		{
			TArray<uint8> P;
			C->R.Expect(CountOp(C, 6714, &P) == 1 && P.Num() == 9 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && P[4] == kSlot
				&& AsdaBytes::RdI16(P.GetData() + 5) == 1 && P[7] == 0 && P[8] == 0, TEXT("F1: 6714 = charId, слот, армия 1, регион 0, строка 0"));
			// G. Кнопка 49530 → окно 49540.
			ClickNode(C, Sub<UAsdaCampWarSubsystem>(C)->GetHitWidget(49530), TEXT("49530"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			FRun& R = C->R;
			R.Expect(CW->IsInListOpen(), TEXT("G1: 49530 → окно 49540 «Ожидание списка участников битвы»"));
			R.Expect(CW->GetText(49573) == L(C, 10033) && CW->GetText(49570) == L(C, 10034) && CW->GetText(49571) == L(C, 10035), TEXT("G1: подписи 49540"));
			R.Expect(CW->GetText(49542) == TEXT("battlefield") && CW->GetText(49543) == TEXT("3"), TEXT("G1: строка 0 — «battlefield», место «3»"));
			R.Expect(!CW->IsShown(49549) && CW->IsShown(49559) && CW->IsShown(49569), TEXT("G1: строка 0 открыта, строки 1/2 затемнены (эталон)"));
			R.Expect(CW->GetInListSel() == -1, TEXT("G1: выбор снят (+0x2320 = −1)"));
			// H0. «Выйти из очереди» без выбранной строки — ничего.
			ClickNode(C, CW->GetHitWidget(49570), TEXT("«Выйти из очереди» без строки"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			C->R.Expect(CW->GetNoticeTrace().Num() == 0, TEXT("H0: без выбранной строки 49570 ничего не делает"));
			ClickNode(C, CW->GetHitWidget(49541), TEXT("строка 49541"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			C->R.Expect(CW->GetInListSel() == 0 && CW->IsShown(49565), TEXT("H1: щелчок строки → выбрана (+0x2320 = 0), рамка 49565"));
			Shot(C, TEXT("campwar_49540"));
			ClickNode(C, CW->GetHitWidget(49570), TEXT("«Выйти из очереди» 49570"));
		});
		Then(C, 0.5f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			const FString Want = L(C, 10036).Replace(TEXT("[ss1]"), TEXT("battlefield"));
			C->R.Expect(CW->GetNoticeTrace().Num() == 1 && CW->GetNoticeTrace()[0] == 10036 && CW->GetLastNoticeText() == Want,
				FString::Printf(TEXT("H2: TWOBUT 10036 «%s»"), *CW->GetLastNoticeText()));
			Shot(C, TEXT("campwar_10036"));
			ClickNode(C, MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00")), TEXT("«Да» TWOBUT 10036"));
		});
		Then(C, 0.5f, [C]()
		{
			TArray<uint8> P;
			C->R.Expect(CountOp(C, 6708, &P) == 1 && P.Num() == 8 && P[4] == kSlot && P[5] == 0 && AsdaBytes::RdI16(P.GetData() + 6) == 1,
				TEXT("H3: «Да» → 6708(0) = голова, регион карты 0, армия 1"));
			// I. 6709 код 1 — заявка снята.
			Sub<UAsdaCampWarSubsystem>(C)->ClearTrace();
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6709, B2(1, 0));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			FRun& R = C->R;
			R.Expect(!CW->IsIngShown() && !CW->IsInListOpen(), TEXT("I1: 6709 код 1 → 49530 и 49540 скрыты"));
			R.Expect(CW->GetNoticeTrace().Num() == 1 && CW->GetNoticeTrace()[0] == 10162, TEXT("I1: нотис 10162"));
			R.Expect(CW->GetRegIndex(0) == -1 && CW->GetLastRegIndex() == -1 && CW->GetText(47403).IsEmpty(), TEXT("I1: ResetState — заявок нет"));
			// J. 6710 — место в очереди; K. снова заявка (код 1) и вызов 6711.
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6710, Make6710(0, 7));
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6707, Make6707(1, 0, 0, 9, 0));
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6711, Make6711(1, 0));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			FRun& R = C->R;
			R.Expect(CW->GetRegIndex(0) == 9 && CW->GetRegGameIndex(0) == 0, TEXT("J1: 6707 код 1 — заявка 9"));
			// Отсчёт ставится на 30 и уменьшается тиком раз в секунду — к проверке он мог уже шагнуть на 29.
			const int32 Cd0 = CW->GetCountdown();
			R.Expect(CW->IsConfirmOpen() && CW->GetCallRegion() == 0 && (Cd0 == 30 || Cd0 == 29) && CW->GetText(48040) == FString::Printf(TEXT("00:%02d"), Cd0),
				FString::Printf(TEXT("K1: 6711 [0]==1 → окно 48099, отсчёт с 00:30 («%s»)"), *CW->GetText(48040)));
			R.Expect(CW->GetText(48020) == L(C, 10043).Replace(TEXT("[ss1]"), TEXT("battlefield")), FString::Printf(TEXT("K1: 48020 «%s»"), *CW->GetText(48020)));
			R.Expect(CW->GetText(48000) == L(C, 10037) && CW->GetText(48030) == L(C, 10038) && CW->GetText(48050) == L(C, 10039) && CW->GetText(48060) == L(C, 10034),
				TEXT("K1: подписи 48099"));
			Shot(C, TEXT("campwar_48099"));
			Dismiss(C);
		});
		Then(C, 2.2f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			const int32 Cd = CW->GetCountdown();
			C->R.Expect(Cd >= 27 && Cd <= 29 && CW->GetText(48040) == FString::Printf(TEXT("00:%02d"), Cd),
				FString::Printf(TEXT("K2: отсчёт идёт раз в секунду (%d, «%s»)"), Cd, *CW->GetText(48040)));
			// L. «Войти в битву».
			ClickNode(C, CW->GetHitWidget(48050), TEXT("«Войти в битву» 48050"));
		});
		Then(C, 0.4f, [C]()
		{
			TArray<uint8> P;
			C->R.Expect(CountOp(C, 6712, &P) == 1 && P.Num() == 11 && P[4] == kSlot && AsdaBytes::RdI16(P.GetData() + 5) == 1 && P[7] == 0
				&& P[8] == 0 && AsdaBytes::RdI16(P.GetData() + 9) == 9, TEXT("L1: 6712 = голова, армия 1, регион 0, GameIndex 0, RegistIndex 9"));
			C->R.Expect(!Sub<UAsdaCampWarSubsystem>(C)->IsConfirmOpen(), TEXT("L1: 48099 скрыт"));
			// M. снова вызов → «Выйти из очереди» → «Да» → 6708(1).
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6711, Make6711(1, 0));
		});
		Then(C, 0.3f, [C]()
		{
			ClickNode(C, Sub<UAsdaCampWarSubsystem>(C)->GetHitWidget(48060), TEXT("«Выйти из очереди» 48060"));
		});
		Then(C, 0.5f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			C->R.Expect(CW->GetNoticeTrace().Num() > 0 && CW->GetNoticeTrace().Last() == 100360, TEXT("M1: TWOBUT id 100360"));
			ClickNode(C, MsgNode(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00")), TEXT("«Да» TWOBUT 100360"));
		});
		Then(C, 0.5f, [C]()
		{
			TArray<uint8> P;
			C->R.Expect(CountOp(C, 6708, &P) == 2 && P[5] == 0 && !Sub<UAsdaCampWarSubsystem>(C)->IsConfirmOpen(), TEXT("M2: «Да» → 6708(1), регион вызова 0, 48099 скрыт"));
			// N. отсчёт истёк → 6708(1) без вопроса.
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6711, Make6711(1, 0));
		});
		Then(C, 0.3f, [C]()
		{
			Sub<UAsdaCampWarSubsystem>(C)->DebugSetCountdown(0);
		});
		Then(C, 1.4f, [C]()
		{
			C->R.Expect(CountOp(C, 6708) == 3 && !Sub<UAsdaCampWarSubsystem>(C)->IsConfirmOpen(), TEXT("N1: отсчёт 0 → 48099 скрыт и 6708(1) без вопроса"));
			// O. 6711 [0]≠1 → нотис 10044; P. коды 6707 без веток; Q. 6709; R. 6740; S. 6705.
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
			CW->ClearTrace();
			Net->DebugFeedPacket(6711, Make6711(0, 1));
			for (int32 Code : { 0, 3, 4, 6, 7, 8, 9, 10, 11 }) { Net->DebugFeedPacket(6707, Make6707(Code, 0, 0, 0, 0)); }
			for (int32 Code : { 0, 2, 3, 4, 7 }) { Net->DebugFeedPacket(6709, B2(Code, 0)); }
			Net->DebugFeedPacket(6740, B2(0, 5));
			Net->DebugFeedPacket(6740, B2(1, 0));
			Net->DebugFeedPacket(6740, B2(2, 0));
			for (int32 Code : { 0, 2, 3, 4, 5, 9 }) { Net->DebugFeedPacket(6705, Make6705(Code, 0, 0, 0, 0)); }
			C->SpyLog->Reset();
			Net->DebugFeedPacket(6705, Make6705(1, 57001, 1, 29, 0));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			FRun& R = C->R;
			const TArray<int32> Want = { 10044, 10151, 10153, 10154, 10156, 10158, 10159, 111938, 111301, 10161, 10163, 10164, 10165,
				10201, 10202, 10203, 10143, 10143, 10143, 10144, 10145, 10142 };
			R.Expect(CW->GetNoticeTrace() == Want, FString::Printf(TEXT("O–S: нотисы всех кодов 6711/6707/6709/6740/6705 (%d из %d), коды без ветки — молча"),
				CW->GetNoticeTrace().Num(), Want.Num()));
			R.Expect(C->SpyLog->Num() == 1 && (*C->SpyLog)[0].X == -1, TEXT("S1: 6705 код 1 → своя армия −1 (общая точка 6713)"));
			Dismiss(C);
			// T. Закрытие X / «Закрыть» / ESC; U. 5055 скрывает 49530; V. 6704 предметом; W. 30074/30075.
			ClickNode(C, CW->GetHitWidget(47015), TEXT("X 47015"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			C->R.Expect(!CW->IsJoinOpen() && CW->GetSelRow() == 0, TEXT("T1: X закрыл 47000, +0x231C = 0"));
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30074, -1);
			C->R.Expect(CW->IsJoinOpen(), TEXT("W1: 30074 открыл 47000"));
			Sub<UAsdaWindowSubsystem>(C)->CloseTopmostOnEsc();
			C->R.Expect(!CW->IsJoinOpen(), TEXT("T2: ESC закрыл 47000"));
			Sub<UAsdaNpcDialogSubsystem>(C)->RunCommandForCheck(30075, -1);
			C->R.Expect(CW->IsJoinOpen(), TEXT("W2: 30075 открыл 47000"));
		});
		Then(C, 0.4f, [C]()
		{
			ClickNode(C, Sub<UAsdaCampWarSubsystem>(C)->GetHitWidget(47700), TEXT("«Закрыть» 47700"));
		});
		Then(C, 0.4f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			C->R.Expect(!CW->IsJoinOpen(), TEXT("T3: «Закрыть» закрыл 47000"));
			Sub<UAsdaNetSubsystem>(C)->DebugFeedPacket(6707, Make6707(1, 0, 0, 4, 0));
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			C->R.Expect(CW->IsIngShown(), TEXT("U0: 49530 снова виден (код 1)"));
			CW->OnPortalTransfer5055(/*bHudOn*/ true);
		});
		Then(C, 0.3f, [C]()
		{
			UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
			C->R.Expect(!CW->IsIngShown(), TEXT("U1: 5055 (HUD кампвара разрешён) → 49530 скрыт"));
			CW->OnSquadResetItemConfirmed(57001, 1, 5);
			TArray<uint8> P;
			C->R.Expect(CountOp(C, 6704, &P) == 1 && P.Num() == 11 && AsdaBytes::RdI32(P.GetData()) == kSelfCharId && AsdaBytes::RdI32(P.GetData() + 4) == 57001
				&& P[8] == 1 && AsdaBytes::RdI16(P.GetData() + 9) == 5, TEXT("V1: 6704 = charId, предмет 57001, сумка 1, ячейка 5"));
			Finish(C);
		});
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C);
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->ClearMsgBoxQueue(TEXT("самотест «Битвы армий»: конец")); }
		if (CW) { CW->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); CW->ClearTrace(); }
		// ★ 30.10.2026: шаг U1 (5055 с разрешённым HUD) включает боевую фазу — сброс сессии боя, чтобы следующий тест начал с чистого.
		if (UAsdaCampWarBattleSubsystem* Bt = Sub<UAsdaCampWarBattleSubsystem>(C)) { Bt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); }
		if (Net)
		{
			if (C->HSpy.IsValid()) { Net->OnSelfFaction.Remove(C->HSpy); }
			Net->OnSelfFaction.Broadcast(C->SavedCamp, 0, 0);
			if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); }
			Net->LastSelectedRawSlot = C->SavedRawSlot;
		}
		if (C->SelfScope) { C->SelfScope->Restore(); }
		if (UAsdaNpcDialogSubsystem* D = Sub<UAsdaNpcDialogSubsystem>(C)) { if (D->IsDialogOpen()) { D->Close(); } }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaCampWarRegTest, TEXT("[битва-самотест]"), C->R.Ok, C->R.Fail);
	}

	// asda.UI.SelfTestCampWarReg [delay=N] [shots] [quit] — офлайн: спавн своего павна на alpen (если его нет), через 4 с — прогон.
	static FAutoConsoleCommandWithWorldAndArgs GAsdaCampWarRegSelfTest(
		TEXT("asda.UI.SelfTestCampWarReg"),
		TEXT("asda.UI.SelfTestCampWarReg [delay=N] [shots] [quit] — ОФЛАЙН-самотест «Битвы армий» (регистрация и вход): кнопки NPC 30057/30074/30075 → 47000, "
		     "строки и кнопки, 49530/49540, 48099 с отсчётом, TWOBUT 10036/100360, все коды 6707/6709/6711/6740/6705, отправки 6700/6704/6706/6708/6712/6714"),
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
			static const uint16 kGuardOps[] = { 6700, 6704, 6706, 6708, 6712, 6714 };
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
					GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 CampWarReg"));
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
