// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ДОСКИ ОБЪЯВЛЕНИЙ (28.10.2026): `asda.SelfTest.BoardQuest [quit]`.
//   Окно 75000 `Subjugate_WIN`, ограниченные задания 20000…20056 — `Docs/Port/systems/BOARD_QUEST_FLOW.md`.
//   Офлайн (`-AsdaNoNet`), реальными путями (закон diagnostic-must-use-real-path): развилка NPC типа 5 — `OpenForNpcId`; пакеты —
//   штатный `UAsdaNetSubsystem::DebugFeedPacket` (тот же `HandlePacket`, что у сокета); кнопки — `OnClicked` самих кнопок; 567 —
//   `OnDialogButton(567)`; отправки 6591/6593 — БОЕВЫЕ под перехватом (`AsdaSelfTestKit::ArmCapture`). Геометрия окна — живая
//   (`AsdaUi::WidgetAbsRect` после отрисовки). Итог: «ДОСКА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Quest/AsdaBoardQuestSubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Npc/AsdaNpcActor.h"
#include "EngineUtils.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"                 // FScreenshotRequest — кадр окна вместе со слоем Slate (`shot`)
#include "Text/AsdaLang.h"
#include "Misc/Paths.h"                   // путь кадра                // язык сессии — в имени кадра

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoardTest, Log, All);

// Доступ к внутренним полям подсистемы доски (friend).
struct FAsdaBoardQuestSelfTest
{
	static const TCHAR* Cmd() { return TEXT("asda.SelfTest.BoardQuest"); }

	// Отправка, снятая перехватчиком.
	struct FCap { uint16 Op = 0; TArray<uint8> P; TCHAR Env = 0; bool bPad = false; };

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		int32 Phase = 0;
		double PhaseT = 0.0;
		uint32 Gen = 0;
		TSharedPtr<TArray<FCap>> Caps;
		TUniquePtr<FAsdaCVarScope> Keys;
		bool bShot = false;        // `shot` — кадр окна с самым длинным названием (Reports/board_quest/shot_<язык>.png)
		int32 LongQuest = -1;      // задание с самым длинным названием языка сессии (фаза 2)
	};
	static TSharedPtr<FRun>& Run() { static TSharedPtr<FRun> R; return R; }

	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaBoardTest, Log, TEXT("ДОСКА ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaBoardTest, Error, TEXT("ДОСКА FAIL %s"), *What); }
	}

	static FString TextOf(const UTextBlock* T) { return T ? T->GetText().ToString() : FString(TEXT("<нет узла>")); }

	// Узел 8 б: questId, тип, счёт, состояние.
	static void PutNode(TArray<uint8>& B, int32 Q, int32 Type, int32 Count, int32 State)
	{
		const int32 O = B.Num();
		B.AddZeroed(8);
		FMemory::Memcpy(B.GetData() + O, &Q, 4);
		B[O + 4] = (uint8)Type;
		const int16 C = (int16)Count;
		FMemory::Memcpy(B.GetData() + O + 5, &C, 2);
		B[O + 7] = (uint8)State;
	}
	// Тело 6590: код, подопкод, узел.
	static TArray<uint8> Body6590(int32 Code, int32 Sub, int32 Q, int32 Type, int32 Count, int32 State)
	{
		TArray<uint8> B; B.AddZeroed(3);
		B[0] = (uint8)Code;
		const int16 S = (int16)Sub; FMemory::Memcpy(B.GetData() + 1, &S, 2);
		PutNode(B, Q, Type, Count, State);
		return B;
	}

	static void Feed(UGameInstance* GI, int32 Op, const TArray<uint8>& B)
	{
		if (UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI)) { AsdaTestNet::DebugFeedPacket(Net, Op, B); }
		if (UAsdaBoardQuestSubsystem* Bd = GI->GetSubsystem<UAsdaBoardQuestSubsystem>()) { Bd->DrainPackets(); }
	}

	static int32 RowOf(const UAsdaBoardQuestSubsystem* Bd, int32 QuestId)
	{
		if (!Bd->Tabs.IsValidIndex(Bd->Tab)) { return -1; }
		const TArray<int32>& I = Bd->Tabs[Bd->Tab].Indices;
		for (int32 j = 0; j < I.Num(); ++j) { if (UAsdaBoardQuestSubsystem::kQuestBase + I[j] == QuestId) { return j; } }
		return -1;
	}

	static void Finish(FRun& R)
	{
		UGameInstance* GI = R.GI.Get();
		if (GI)
		{
			if (UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI)) { AsdaSelfTestKit::DisarmCapture(Net, R.Gen); }
			if (UAsdaBoardQuestSubsystem* Bd = GI->GetSubsystem<UAsdaBoardQuestSubsystem>())
			{
				Bd->Close();
				Bd->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			}
			if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { Dlg->DebugHideRewardPopup(); Dlg->HideQuestAcceptWindow(); }
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(TEXT("самотест доски: конец")); }
		}
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (W && GEngine) { GEngine->Exec(W, TEXT("asda.Quest.FakeSelf")); }   // снять подмену уровня
		R.Keys.Reset();
		AsdaSelfTestKit::Finish(W, Cmd(), R.bQuit, LogAsdaBoardTest, TEXT("ДОСКА"), R.Ok, R.Fail);
		Run().Reset();
	}

	// ── Фаза 0: данные, окно, пакеты, кнопки (синхронно) ──
	static void Phase0(FRun& R)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaBoardQuestSubsystem* Bd = GI->GetSubsystem<UAsdaBoardQuestSubsystem>();
		UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
		UAsdaQuestMarkerSubsystem* QM = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
		UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
		UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
		Bd->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		WM->ClearMsgBoxQueue(TEXT("самотест доски: старт"));

		// A. Данные.
		Bd->EnsureData();
		Expect(R, Bd->Quests.Num() == 57, FString::Printf(TEXT("A1 DT_RaidQuestData: 57 заданий (есть %d)"), Bd->Quests.Num()));
		bool bIdx = true;
		for (int32 k = 0; k < Bd->Quests.Num(); ++k) { bIdx &= (Bd->Quests[k].RecIndex == k && Bd->Quests[k].QuestId == 20000 + k); }
		Expect(R, bIdx, TEXT("A2 questTable[k].id == 20000 + k у всех записей (адресация доски по индексу)"));
		const FAsdaRaidQuestRow* Q14 = Bd->FindQuest(20014);
		Expect(R, Q14 && Q14->PeriodType == 1 && Q14->MaxCount == 1 && Q14->Zone == 7 && Q14->Gold == 30750 && Q14->Exp == 152000
			&& Q14->RewardItemIds.Num() == 2 && Q14->RewardItemIds[0] == 73617 && Q14->RewardItemCounts[0] == 2,
			TEXT("A3 запись 20014: тип 1, max 1, зона 7, золото 30750, опыт 152000, предмет 73617 ×2 (Interlude)"));
		const FAsdaBoardNpcRow* N9 = Bd->FindNpcRecord(9);
		const FAsdaBoardNpcRow* N10 = Bd->FindNpcRecord(10);
		int32 S9 = 0, S10 = 0;
		if (N9) { for (int32 v : N9->QuestSlots) { S9 += v >= 0 ? 1 : 0; } }
		if (N10) { for (int32 v : N10->QuestSlots) { S10 += v >= 0 ? 1 : 0; } }
		Expect(R, N9 && N10 && S9 == 17 && S10 == 36 && N10->QuestSlots.IsValidIndex(56) && N10->QuestSlots[56] == 20056,
			FString::Printf(TEXT("A4 DT_BoardNpc: доска 9 — 17 заданий, доска 10 — 36 (с 20056): %d/%d"), S9, S10));
		Expect(R, Bd->FindQuestType(20022) == 2 && Bd->FindQuestType(20000) == 0 && Bd->FindQuestType(20999) == -1,
			TEXT("A5 mapObjFindValueById: 20022 → 2, 20000 → 0, нет записи → −1"));

		// A6. Перевод (решение владельца 28.10): 3573 — пусто, 3577 — «∞», 3595 ≠ 3596.
		Expect(R, AsdaNotice::Loc(GI, 3573).TrimStartAndEnd().IsEmpty() && AsdaNotice::Loc(GI, 3577).TrimStartAndEnd() == TEXT("∞")
			&& AsdaNotice::Loc(GI, 3595).TrimStartAndEnd() != AsdaNotice::Loc(GI, 3596).TrimStartAndEnd(),
			FString::Printf(TEXT("A6 тексты: 3573 «%s», 3577 «%s», 3595 «%s» / 3596 «%s»"), *AsdaNotice::Loc(GI, 3573), *AsdaNotice::Loc(GI, 3577),
				*AsdaNotice::Loc(GI, 3595), *AsdaNotice::Loc(GI, 3596)));

		// B. Развилка NPC типа 5 → доска (до гейтов окна 570).
		const bool bOpened = Dlg->OpenForNpcId(9);
		Expect(R, bOpened && Bd->IsOpen() && !Dlg->IsDialogOpen() && WM->IsWindowVisible(FName(TEXT("Subjugate_WIN"))),
			TEXT("B1 OpenForNpcId(9): окно 75000 открыто, окно разговора 570 НЕ открыто"));
		Expect(R, Bd->bControlsReady, TEXT("B2 контролы окна разобраны полностью (20 строк × 5 колонок, 8 ячеек наград, рамка, вкладки)"));
		Expect(R, Bd->TabCount == 4 && Bd->Tabs[0].Zone == 6 && Bd->Tabs[1].Zone == 2 && Bd->Tabs[2].Zone == 9 && Bd->Tabs[3].Zone == 0,
			FString::Printf(TEXT("B3 вкладки доски 9: 4 зоны 6/2/9/0 (есть %d)"), Bd->TabCount));
		Expect(R, Bd->TabTexts.Num() == 5 && TextOf(Bd->TabTexts[0]) == UAsdaBoardQuestSubsystem::ZoneName(GI, 6)
			&& TextOf(Bd->TabTexts[3]) == AsdaNotice::Loc(GI, 600).TrimStartAndEnd() && !AsdaUi::IsShown(Bd->TabPlanes[4]),
			FString::Printf(TEXT("B4 подписи вкладок — имена зон («%s», «%s»), 5-я вкладка скрыта"), *TextOf(Bd->TabTexts[0]), *TextOf(Bd->TabTexts[3])));
		// Уровень 40 (подмена): вкладка 0 (Солнечный Берег) — 20009, 20011, 20012, 20042, 20043.
		Expect(R, Bd->Tabs[0].Indices.Num() == 5 && Bd->Tabs[0].Indices[0] == 9 && Bd->Tabs[0].Indices[4] == 43,
			FString::Printf(TEXT("B5 фильтр вкладки 0: 5 заданий 20009…20043 по индексам (есть %d)"), Bd->Tabs[0].Indices.Num()));
		const FAsdaBoardRowUi& Row0 = Bd->RowUi[0];
		const FString Kind0 = FString::Printf(TEXT("<%s>"), *AsdaNotice::Loc(GI, 3593).TrimStartAndEnd());
		Expect(R, AsdaUi::IsShown(Row0.Main) && TextOf(Row0.Kind) == Kind0 && TextOf(Row0.Level) == TEXT("37/100")
			&& TextOf(Row0.Name) == UAsdaBoardQuestSubsystem::QuestName(20009) && TextOf(Row0.Count) == AsdaNotice::Loc(GI, 3577).TrimEnd(),
			FString::Printf(TEXT("B6 строка 0: «%s» «%s» «%s» «%s» (Повтор, ∞)"), *TextOf(Row0.Kind), *TextOf(Row0.Level), *TextOf(Row0.Name), *TextOf(Row0.Count)));
		const int32 R42 = RowOf(Bd, 20042);
		const FLinearColor Green = AsdaUi::SrgbU32ToLinear(0xFF00FF00u), White = AsdaUi::SrgbU32ToLinear(0xFFFFFFFFu);
		Expect(R, R42 >= 0 && Bd->RowUi[R42].Name && Bd->RowUi[R42].Name->GetColorAndOpacity().GetSpecifiedColor().Equals(Green, 0.01f)
			&& Row0.Name && Row0.Name->GetColorAndOpacity().GetSpecifiedColor().Equals(White, 0.01f),
			TEXT("B7 цвета: lvMin 30 при ур. 40 — зелёный, lvMin 37 — белый"));
		Expect(R, !AsdaUi::IsShown(Bd->RowUi[5].Main), TEXT("B8 строки за списком скрыты"));
		// B9. Скрытие строкой данных (решение владельца 28.10: 20025, 20048, 20055 — `bHidden` из ведомости board_quest_hidden.json).
		{
			const FAsdaRaidQuestRow* H25 = Bd->FindQuest(20025), *H48 = Bd->FindQuest(20048), *H55 = Bd->FindQuest(20055), *V9 = Bd->FindQuest(20009);
			Expect(R, H25 && H25->bHidden && H48 && H48->bHidden && H55 && H55->bHidden && V9 && !V9->bHidden,
				TEXT("B9 DT: 20025/20048/20055 скрыты (bHidden), 20009 — нет"));
			Bd->Quests[9].bHidden = true;   // проверка механизма на задании, которое у доски есть
			Bd->Close(); Dlg->OpenForNpcId(9);
			const bool bGone = RowOf(Bd, 20009) < 0 && Bd->Tabs[0].Indices.Num() == 4;
			Bd->Quests[9].bHidden = false;
			Bd->Close(); Dlg->OpenForNpcId(9);
			Expect(R, bGone && RowOf(Bd, 20009) == 0, TEXT("B10 bHidden убирает задание из вкладки, снятый флаг — возвращает"));
		}

		// C. 6588: узлы + хвост-паддинг (мусор отброшен).
		{
			TArray<uint8> B;
			PutNode(B, 20009, 0, 0, 1);
			PutNode(B, 20012, 1, 2, 1);
			PutNode(B, 20043, 1, 1, 3);
			B.Append({ 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xFE });   // паддинг сервера + 0xFE
			Feed(GI, 6588, B);
		}
		Expect(R, Bd->Nodes.Num() == 3 && Bd->FindNode(20012) && Bd->FindNode(20012)->Count == 2,
			FString::Printf(TEXT("C1 6588: 3 узла, мусорный узел хвоста отброшен (узлов %d)"), Bd->Nodes.Num()));
		const int32 R12 = RowOf(Bd, 20012), R43 = RowOf(Bd, 20043);
		Expect(R, TextOf(Row0.State) == AsdaNotice::Loc(GI, 3574).TrimEnd() && R12 >= 0 && TextOf(Bd->RowUi[R12].Count) == TEXT("2 / 5")
			&& R43 >= 0 && TextOf(Bd->RowUi[R43].State) == AsdaNotice::Loc(GI, 3576).TrimEnd(),
			FString::Printf(TEXT("C2 состояния: 20009 «%s», 20012 «%s», 20043 «%s»"), *TextOf(Row0.State),
				R12 >= 0 ? *TextOf(Bd->RowUi[R12].Count) : TEXT("-"), R43 >= 0 ? *TextOf(Bd->RowUi[R43].State) : TEXT("-")));
		// Смена карты: узлы живут; 6588 повторно — без дублей.
		Bd->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		{
			TArray<uint8> B; PutNode(B, 20009, 0, 0, 1); Feed(GI, 6588, B);
		}
		Expect(R, Bd->Nodes.Num() == 3 && !Bd->IsOpen(), TEXT("C3 смена карты: окно закрыто, узлы сохранены, повторная 6588 без дублей"));

		// D. Строка и панель подробностей.
		Dlg->OpenForNpcId(9);
		Bd->OnRowClick(0);
		const FString Head = TextOf(Bd->DetailHead);
		Expect(R, Bd->SelRow == 0 && Head.Contains(UAsdaBoardQuestSubsystem::QuestName(20009)) && Head.Contains(TEXT("[37 / 100]")),
			FString::Printf(TEXT("D1 75242 «%s»"), *Head));
		Expect(R, Bd->DetailReq.Num() == 3 && TextOf(Bd->DetailReq[0]).Contains(TEXT("0/20")),
			FString::Printf(TEXT("D2 75244 «%s» (счётчик слота журнала / 20)"), *TextOf(Bd->DetailReq[0])));
		Expect(R, TextOf(Bd->RewardUi[0].Label).StartsWith(TEXT("9000 ")) && TextOf(Bd->RewardUi[1].Label).StartsWith(TEXT("13000 ")),
			FString::Printf(TEXT("D3 награда: «%s», «%s»"), *TextOf(Bd->RewardUi[0].Label), *TextOf(Bd->RewardUi[1].Label)));
		Expect(R, Bd->SelFrame.Num() == 8 && AsdaUi::IsShown(Bd->SelFrame[0]) && TextOf(Bd->BtnOkText) == AsdaNotice::Loc(GI, 3597),
			TEXT("D4 рамка выбора 75041 показана, 75281 «Взять задание»"));
		{
			const int32 R12b = RowOf(Bd, 20012);
			Bd->OnRowClick(R12b);
			const FString Desc = TextOf(Bd->DetailDesc);
			Expect(R, Desc.Contains(TEXT("3")) && Desc.Contains(TEXT("\n")), FString::Printf(TEXT("D5 тип 1: остаток 5 − 2 = 3 в описании («…%s»)"), *Desc.Right(40)));
		}

		// E. «Взять» → 6591 (боевой отправитель под перехватом).
		R.Caps->Reset();
		const int32 R11 = RowOf(Bd, 20011);
		Bd->OnRowClick(R11);
		if (UButton* Ok = Bd->BtnOk) { Ok->OnClicked.Broadcast(); }
		const FCap* C6591 = R.Caps->FindByPredicate([](const FCap& C) { return C.Op == 6591; });
		int32 Q6591 = -1; if (C6591 && C6591->P.Num() >= 4) { FMemory::Memcpy(&Q6591, C6591->P.GetData(), 4); }
		Expect(R, C6591 && C6591->P.Num() == 4 && Q6591 == 20011 && C6591->Env == TEXT('S') && !Bd->IsOpen(),
			FString::Printf(TEXT("E1 75281 → 6591 (тело 4 б, questId %d, конверт %c), окно закрыто"), Q6591, C6591 ? C6591->Env : TEXT('-')));
		// Второй запрос до ответа — не уходит (замок «в полёте»).
		Dlg->OpenForNpcId(9); Bd->OnRowClick(RowOf(Bd, 20011)); R.Caps->Reset();
		if (UButton* Ok = Bd->BtnOk) { Ok->OnClicked.Broadcast(); }
		Expect(R, !R.Caps->ContainsByPredicate([](const FCap& C) { return C.Op == 6591; }) && Bd->IsOpen() && Bd->SelRow >= 0,
			TEXT("E2 второй 6591 до ответа не отправлен, окно и выбор строки остались (ревизия М-1)"));
		Feed(GI, 6590, Body6590(1, 6591, 20011, 0, 0, 1));
		Expect(R, Bd->FindNode(20011) && Bd->FindNode(20011)->State == 1 && Bd->InFlightOp == 0, TEXT("E3 6590{1, 6591}: узел вставлен, замок снят"));
		WM->ClearMsgBoxQueue(TEXT("самотест доски"));
		Feed(GI, 6590, Body6590(7, 6591, 20011, 0, 0, 1));
		Expect(R, Bd->DebugLastMsgBoxLoc == 3608, FString::Printf(TEXT("E4 6590 код 7 → нотис 3608 (есть %d)"), Bd->DebugLastMsgBoxLoc));
		Feed(GI, 6590, Body6590(8, 6591, 20011, 0, 0, 1));
		Expect(R, Bd->DebugLastMsgBoxLoc == 6176, TEXT("E5 6590 код 8 → нотис 6176"));
		WM->ClearMsgBoxQueue(TEXT("самотест доски"));
		// Уровень ниже lvMin — 3604, окно остаётся.
		Dlg->OpenForNpcId(10); Bd->OnTabSelected(0);
		int32 RHigh = -1;
		for (int32 j = 0; j < Bd->Tabs[0].Indices.Num(); ++j) { if (Bd->QuestAt(Bd->Tabs[0].Indices[j])->LevelMin > 40) { RHigh = j; break; } }
		if (RHigh >= 0) { Bd->OnRowClick(RHigh); }
		Bd->DebugLastMsgBoxLoc = -1;
		if (UButton* Ok = Bd->BtnOk) { Ok->OnClicked.Broadcast(); }
		Expect(R, RHigh >= 0 && Bd->DebugLastMsgBoxLoc == 3604 && Bd->IsOpen(), TEXT("E6 уровень ниже lvMin → 3604, окно остаётся"));
		Expect(R, Bd->TabCount == 5 && AsdaUi::IsShown(Bd->TabPlanes[4]), TEXT("E7 доска 10: 5 вкладок"));
		WM->ClearMsgBoxQueue(TEXT("самотест доски"));
		Bd->Close();
		Dlg->OpenForNpcId(9);
		Expect(R, Bd->TabCount == 4 && !AsdaUi::IsShown(Bd->TabPlanes[4]), TEXT("E8 Д-3: после доски 10 у доски 9 видно 4 вкладки (лишняя скрыта)"));

		// F. Сдача: цели выполнены (6590{1, 0}) → «Выполнено» → попап 540 → 567 → 6593(…, 99).
		Feed(GI, 6590, Body6590(1, 0, 20009, 0, 0, 2));
		Expect(R, TextOf(Bd->RowUi[0].State) == AsdaNotice::Loc(GI, 3575).TrimEnd() && Bd->IsQuestReady(20009),
			TEXT("F1 6590{1, 0}: узел 20009 state 2 → «Выполнено», готово к сдаче"));
		Bd->OnRowClick(0);
		Expect(R, TextOf(Bd->BtnOkText) == AsdaNotice::Loc(GI, 3575), TEXT("F2 75281 ← 3575 у готового к сдаче"));
		if (UButton* Ok = Bd->BtnOk) { Ok->OnClicked.Broadcast(); }
		Expect(R, !Bd->IsOpen() && Bd->PendingTurnIn == 20009 && Dlg->DebugRewardPopupVisible() && Dlg->DebugRewardBasicNum() == 4,
			TEXT("F3 75281 → окно закрыто, this[10] = 20009, попап 540 «режим доски» показан (4 места обычной панели)"));
		// М-1: при занятом замке 567 не теряет сдачу — 6593 не уходит, попап и ждущее сдачи остаются.
		R.Caps->Reset();
		Bd->InFlightOp = 6591; Bd->InFlightSince = FPlatformTime::Seconds();
		Dlg->DebugPressRewardOk();
		Expect(R, !R.Caps->ContainsByPredicate([](const FCap& C) { return C.Op == 6593; }) && Dlg->DebugRewardPopupVisible() && Bd->PendingTurnIn == 20009,
			TEXT("F3b замок занят: 567 → 6593 не ушёл, попап 540 открыт, ждущее сдачи 20009 сохранено (ревизия М-1)"));
		Bd->InFlightOp = 0; Bd->InFlightSince = 0.0;
		R.Caps->Reset();
		Dlg->DebugPressRewardOk();
		const FCap* C6593 = R.Caps->FindByPredicate([](const FCap& C) { return C.Op == 6593; });
		int32 Q6593 = -1; if (C6593 && C6593->P.Num() >= 4) { FMemory::Memcpy(&Q6593, C6593->P.GetData(), 4); }
		Expect(R, C6593 && C6593->P.Num() == 5 && Q6593 == 20009 && C6593->P[4] == 99 && C6593->Env == TEXT('S')
			&& Bd->PendingTurnIn == -1 && !Dlg->DebugRewardPopupVisible(),
			FString::Printf(TEXT("F4 567 → 6593 (5 б: %d, выбор %d), попап закрыт, this[10] = −1"), Q6593, C6593 && C6593->P.Num() == 5 ? C6593->P[4] : -1));
		// 6590{1, 6593} у «Повтора» — узел удаляется; 6594 код 1 — золото, слот журнала.
		Feed(GI, 6590, Body6590(1, 6593, 20009, 0, 1, 0));
		Expect(R, Bd->FindNode(20009) == nullptr, TEXT("F5 6590{1, 6593} тип 0: узел удалён"));
		{
			TArray<uint8> S5044; S5044.SetNumZeroed(47);
			const int32 Ep = 20009; FMemory::Memcpy(S5044.GetData() + 3, &Ep, 4);
			const int16 SlotNo = 3, QNo = 20009; FMemory::Memcpy(S5044.GetData() + 8, &SlotNo, 2); FMemory::Memcpy(S5044.GetData() + 11, &QNo, 2);
			S5044[7] = 4; S5044[10] = 2;
			QM->ApplyAcceptedQuestSlot(S5044);
		}
		const int32 Gold0 = Inv ? Inv->GetGold() : 0;
		{
			TArray<uint8> B; B.SetNumZeroed(484);
			B[0] = 1;
			const int32 G = 9000; FMemory::Memcpy(B.GetData() + 5, &G, 4);
			const int32 Wt = 0; FMemory::Memcpy(B.GetData() + 9, &Wt, 4);
			const int16 Sl = 3; FMemory::Memcpy(B.GetData() + 22, &Sl, 2);
			const int32 Q = 20009; FMemory::Memcpy(B.GetData() + 24, &Q, 4);
			const int16 None = -1;
			for (int32 i = 0; i < 3; ++i) { FMemory::Memcpy(B.GetData() + 28 + 57 * i + 5, &None, 2); }
			for (int32 u = 0; u < 5; ++u) { FMemory::Memcpy(B.GetData() + 199 + 57 * u + 5, &None, 2); }
			Feed(GI, 6594, B);
		}
		bool bSlotGone = true;
		for (const FAsdaQuestSlot& S : QM->GetSlots()) { if (S.QuestNo == 20009) { bSlotGone = false; } }
		Expect(R, Inv && Inv->GetGold() == Gold0 + 9000 && bSlotGone,
			FString::Printf(TEXT("F6 6594 код 1: золото %d → %d, слот журнала 20009 снят"), Gold0, Inv ? Inv->GetGold() : -1));
		WM->ClearMsgBoxQueue(TEXT("самотест доски"));
		{
			TArray<uint8> B; B.SetNumZeroed(484); B[0] = 3; Feed(GI, 6594, B);
		}
		Expect(R, Bd->DebugLastMsgBoxLoc == 6176, TEXT("F7 6594 код 3 → 6176"));
		WM->ClearMsgBoxQueue(TEXT("самотест доски"));
		// Д-6: 6593 у задания с лимитом (тип 1/2) узел сохраняет.
		Feed(GI, 6590, Body6590(1, 6593, 20043, 1, 1, 3));
		Expect(R, Bd->FindNode(20043) && Bd->FindNode(20043)->State == 3, TEXT("F8 6590{1, 6593} тип 1: узел заменён (не удалён)"));
		// Попап закрыт без сдачи → this[10] снят.
		Bd->PendingTurnIn = 20012; Dlg->DebugHideRewardPopup();
		Expect(R, Bd->PendingTurnIn == -1, TEXT("F9 попап 540 закрыт без сдачи → ждущее сдачи снято (исправление)"));

		// G. 6595 суточный сброс: дневные — счёт 0 и состояние 3 → 0 (Д-5).
		Feed(GI, 6595, TArray<uint8>());
		Expect(R, Bd->FindNode(20043) && Bd->FindNode(20043)->Count == 0 && Bd->FindNode(20043)->State == 0
			&& Bd->FindNode(20012) && Bd->FindNode(20012)->Count == 0,
			TEXT("G1 6595: дневные узлы — счёт 0, «Заканчивается сегодня» снято"));

		// H. Метка миникарты (Д-1) и «Собрать».
		Expect(R, Bd->MinimapMark(9) == 1, TEXT("H1 метка доски 9 на миникарте при заданиях по уровню (исправление Д-1)"));
		R.Keys->Set(TEXT("asda.Board.FixMinimapLevel"), 0);
		Expect(R, Bd->MinimapMark(9) == 0, TEXT("H2 ключ 0 — как оригинал: без готового к сдаче метки нет"));
		R.Keys->Set(TEXT("asda.Board.FixMinimapLevel"), 1);
		// H3…H8 (ревизия С-1/М-5): уровень 26 — у доски 9 доступно лишь 20040 (25/100, тип 1, max 1).
		if (GEngine) { GEngine->Exec(GI->GetWorld(), TEXT("asda.Quest.FakeSelf 26")); }
		Bd->RemoveNode(20040);
		Expect(R, Bd->MinimapMark(9) == 1, TEXT("H3 ур. 26: 20040 можно взять (узла нет) → метка есть"));
		Feed(GI, 6590, Body6590(1, 6591, 20040, 1, 0, 1));
		Expect(R, Bd->MinimapMark(9) == 0, TEXT("H4 20040 взято (state 1) → метки нет (С-1)"));
		Feed(GI, 6590, Body6590(1, 6593, 20040, 1, 1, 0));
		Expect(R, Bd->MinimapMark(9) == 0, TEXT("H5 20040 исчерпано (тип 1, счёт 1 = max 1) → метки нет (С-1)"));
		Feed(GI, 6595, TArray<uint8>());
		Expect(R, Bd->MinimapMark(9) == 1, TEXT("H6 после 6595 счёт 0 → снова можно взять → метка есть"));
		Feed(GI, 6590, Body6590(1, 0, 20040, 1, 0, 2));
		const int32 K40 = 40;
		Bd->Quests[K40].bHidden = true;
		Expect(R, Bd->IsQuestReady(20040) && Bd->MinimapMark(9) == 0, TEXT("H7 скрытое задание метку не даёт даже готовым к сдаче (М-5)"));
		Bd->Quests[K40].bHidden = false;
		Expect(R, Bd->MinimapMark(9) == 1, TEXT("H8 то же нескрытое готовое к сдаче → метка есть"));
		// H9…H13 значок над доской (запрос владельца 28.10, [ИСПРАВЛЕНИЕ ОРИГИНАЛА] asda.Board.FixOverheadMark).
		Expect(R, Bd->OverheadMark(9) == 2 && Bd->OverheadMark(10) == 0 && Bd->OverheadMark(47) == 0,
			TEXT("H9 готовое к сдаче → красный «!» (2); доска 10 при ур. 26 — нечего взять (0); не доска — 0"));
		{
			// Живой актор доски на карте прогона (если есть): значок ставит штатный пересчёт меток.
			AAsdaNpcActor* Act = nullptr;
			for (TActorIterator<AAsdaNpcActor> It(GI->GetWorld()); It; ++It) { if (It->NpcId == 9) { Act = *It; break; } }
			if (Act)
			{
				QM->RebuildMarks();
				Expect(R, Act->QuestMarkKind == 2, FString::Printf(TEXT("H10 актор доски 9 на карте: значок %d (ждали 2 — красный)"), Act->QuestMarkKind));
			}
			else { UE_LOG(LogAsdaBoardTest, Log, TEXT("ДОСКА H10: актора доски 9 на карте прогона нет — значок проверен по OverheadMark")); }
		}
		Bd->RemoveNode(20040);
		Expect(R, Bd->OverheadMark(9) == 1, TEXT("H11 есть что взять (20040 свободно) → жёлтый «!» (1)"));
		Feed(GI, 6590, Body6590(1, 6591, 20040, 1, 0, 1));
		Expect(R, Bd->OverheadMark(9) == 0, TEXT("H12 взято и не готово → значка нет"));
		R.Keys->Set(TEXT("asda.Board.FixOverheadMark"), 0);
		Feed(GI, 6590, Body6590(1, 0, 20040, 1, 0, 2));
		Expect(R, Bd->OverheadMark(9) == 0, TEXT("H13 ключ 0 — как оригинал: значка над доской нет"));
		R.Keys->Set(TEXT("asda.Board.FixOverheadMark"), 1);
		Bd->RemoveNode(20040);
		if (GEngine) { GEngine->Exec(GI->GetWorld(), TEXT("asda.Quest.FakeSelf 40")); }

		// I. Закрытие кнопкой 75282 и клик по миру.
		Dlg->OpenForNpcId(9);
		if (UButton* No = Bd->BtnNo) { No->OnClicked.Broadcast(); }
		Expect(R, !Bd->IsOpen() && !UAsdaBoardQuestSubsystem::IsBoardOpen(GI), TEXT("I1 75282 «Закрыть» закрывает окно"));
		Dlg->OpenForNpcId(9);
		if (UButton* X = Bd->BtnClose) { X->OnClicked.Broadcast(); }
		Expect(R, !Bd->IsOpen(), TEXT("I2 75289 «×» закрывает окно"));

		// J. Окно 750 при приёме задания доски (5044) и «Quest Error».
		Expect(R, Dlg->ShowQuestAcceptWindow(20011, 20011, 0), TEXT("J1 окно 750 для задания доски 20011 показано (имя записи доски)"));
		Dlg->HideQuestAcceptWindow();
		Expect(R, !Dlg->ShowQuestAcceptWindow(20099, 20099, 0), TEXT("J2 нет записи доски → «Quest Error», окна нет"));
		WM->ClearMsgBoxQueue(TEXT("самотест доски"));

		// M (ревизия М-6): попап 540 «режим доски» закрывает магазин NPC (`npcShop_Close`, ui_core.c:60148).
		if (UAsdaNpcShopSubsystem* Shop = GI->GetSubsystem<UAsdaNpcShopSubsystem>())
		{
			Shop->OpenShop(5, 1);
			const bool bShopWasOpen = Shop->IsOpen();
			Bd->PendingTurnIn = 20012;
			const bool bShown = Dlg->ShowBoardRewardPopup(20012, UAsdaBoardQuestSubsystem::QuestName(20012));
			Expect(R, bShopWasOpen && bShown && !Shop->IsOpen(), FString::Printf(TEXT("M1 магазин NPC был открыт=%d → после попапа 540 закрыт=%d"),
				bShopWasOpen ? 1 : 0, Shop->IsOpen() ? 0 : 1));
			Dlg->DebugHideRewardPopup();
			if (Shop->IsOpen()) { Shop->CloseShop(); }
		}
		// М-4: журнал отправок ограничен.
		for (int32 i = 0; i < 40; ++i) { Bd->NoteSent(6591, TArray<uint8>()); }
		Expect(R, Bd->DebugSent.Num() <= UAsdaBoardQuestSubsystem::kDebugSentCap, FString::Printf(TEXT("M2 журнал отправок ограничен (%d ≤ %d)"),
			Bd->DebugSent.Num(), UAsdaBoardQuestSubsystem::kDebugSentCap));

		// K. Сброс сессии (выход) — узлы стёрты.
		Bd->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Expect(R, Bd->Nodes.Num() == 0, TEXT("K1 выход в выбор персонажа: узлы стёрты"));

		// Открыть ещё раз для проверки геометрии (фаза 1 после отрисовки).
		{
			TArray<uint8> B; PutNode(B, 20012, 1, 2, 1); Feed(GI, 6588, B);
		}
		Dlg->OpenForNpcId(9);
		Bd->OnRowClick(2);
	}

	// ── Фаза 1: живая геометрия (окно отрисовано) ──
	static void Phase1(FRun& R)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaBoardQuestSubsystem* Bd = GI->GetSubsystem<UAsdaBoardQuestSubsystem>();
		FVector2D WinA(0.0), WinB(0.0), A(0.0), B(0.0);
		const bool bWin = Bd->Widget && AsdaUi::WidgetAbsRect(Bd->Widget->GetWidgetFromName(TEXT("Subjugate_WIN_0_0")), WinA, WinB);
		const UTextBlock* N4 = Bd->RowUi[4].Name;
		const bool bRow = N4 && AsdaUi::WidgetAbsRect(N4, A, B) && (B.X - A.X) > 100.f;
		UE_LOG(LogAsdaBoardTest, Log, TEXT("ДОСКА геометрия: угол окна (%.0f,%.0f), имя строки 4 (%.0f,%.0f)-(%.0f,%.0f)"), WinA.X, WinA.Y, A.X, A.Y, B.X, B.Y);
		Expect(R, bWin && bRow && A.X > WinA.X && A.Y > WinA.Y, TEXT("L1 узел имени строки 5 (шаг ассета) отрисован внутри окна, ширина > 100"));
		FVector2D R0a(0.0), R0b(0.0), R1a(0.0), R1b(0.0);
		const bool bStep = AsdaUi::WidgetAbsRect(Bd->RowUi[0].Name, R0a, R0b) && AsdaUi::WidgetAbsRect(Bd->RowUi[1].Name, R1a, R1b);
		Expect(R, bStep && FMath::Abs((R1a.Y - R0a.Y) - (R1b.Y - R0b.Y)) < 1.f && (R1a.Y - R0a.Y) > 10.f,
			FString::Printf(TEXT("L2 шаг строк по живой геометрии %.1f (масштаб 20 px разметки)"), R1a.Y - R0a.Y));
		FVector2D Fa(0.0), Fb(0.0), Ma(0.0), Mb(0.0);
		const bool bFrame = AsdaUi::WidgetAbsRect(Bd->SelFrame[0], Fa, Fb) && AsdaUi::WidgetAbsRect(Bd->RowUi[2].MainText, Ma, Mb);
		Expect(R, bFrame && FMath::Abs(Fa.Y - Ma.Y) < 6.f, FString::Printf(TEXT("L3 рамка 75041 на строке 2 (рамка y %.0f, строка y %.0f)"), Fa.Y, Ma.Y));
		FVector2D Qa(0.0), Qb(0.0), Ia(0.0), Ib(0.0);
		const bool bQty = Bd->RewardUi[2].Qty && Bd->RewardUi[2].Icon && AsdaUi::WidgetAbsRect(Bd->RewardUi[2].Qty, Qa, Qb)
			&& AsdaUi::WidgetAbsRect(Bd->RewardUi[2].Icon, Ia, Ib);
		Expect(R, bQty && FMath::Abs(Qa.X - Ia.X) < 2.f, TEXT("L4 узел количества 75256 (шаг ассета) лежит на иконке"));
	}

	// ── L5…L9: всё помещается в свои поля (самые длинные тексты RU/EN, шрифт самого поля; живая ширина поля) ──
	//   Колонки строк — текст по центру, колонки стоят вплотную: текст шире поля наезжает на соседнюю (жалоба владельца).
	static float FieldW(const UWidget* W)
	{
		FVector2D A(0.0), B(0.0);
		return (W && AsdaUi::WidgetAbsRect(W, A, B)) ? float(B.X - A.X) : 0.f;
	}
	// Pad — запас на каждую сторону (у вкладок — скруглённые углы плашки, текст в них «обрезается» на глаз).
	static void FitCheck(FRun& R, const TCHAR* Tag, const UTextBlock* Field, const TArray<FString>& Texts, float Pad = 0.f)
	{
		const float W = FieldW(Field);
		float Max = 0.f; FString Worst;
		for (const FString& T : Texts)
		{
			const float Tw = Field ? AsdaUi::MeasureTextWidth(T, Field->GetFont(), 0.f) : 0.f;
			if (Tw > Max) { Max = Tw; Worst = T; }
		}
		Expect(R, Field && W > 0.f && Max + 2.f * Pad <= W + 1.f,
			FString::Printf(TEXT("%s: самый длинный «%s» %.0f px, поле %.0f px (запас %.0f с каждой стороны)"), Tag, *Worst, Max, W, Pad));
	}
	static void PhaseFit(FRun& R)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaBoardQuestSubsystem* Bd = GI->GetSubsystem<UAsdaBoardQuestSubsystem>();
		TArray<FString> Names, Lv, Kinds, States, Counts, Heads, Tabs;
		for (const FAsdaRaidQuestRow& Q : Bd->Quests)
		{
			if (Q.bHidden) { continue; }
			const FString Nm = UAsdaBoardQuestSubsystem::QuestName(Q.QuestId);
			const FString K = AsdaNotice::Loc(GI, Q.Mode == 1 ? 3631 : 3593 + Q.PeriodType).TrimStartAndEnd();
			Names.Add(Nm);
			Lv.Add(FString::Printf(TEXT("%d/%d"), Q.LevelMin, Q.LevelMax));
			Kinds.Add(FString::Printf(TEXT("<%s>"), *K));
			Counts.Add(Q.PeriodType != 0 ? FString::Printf(TEXT("%d / %d"), Q.MaxCount, Q.MaxCount) : AsdaNotice::Loc(GI, 3577).TrimEnd());
			Heads.Add(FString::Printf(TEXT("[%s] [%d / %d] %s"), *K, Q.LevelMin, Q.LevelMax, *Nm));
		}
		for (int32 L = 3573; L <= 3576; ++L) { States.Add(AsdaNotice::Loc(GI, L).TrimEnd()); }
		for (const FAsdaBoardNpcRow& N : Bd->Npcs) { for (int32 Z : N.Zones) { if (Z >= 0) { Tabs.Add(UAsdaBoardQuestSubsystem::ZoneName(GI, Z)); } } }
		// Узкая строка (2…20: «Задание» 180 в разметке) и широкая (1: 200) — проверяются обе.
		for (int32 Row : { 0, 1 })
		{
			const FAsdaBoardRowUi& U = Bd->RowUi[Row];
			const FString P = FString::Printf(TEXT("строка %d"), Row);
			FitCheck(R, *(TEXT("L5 «Задание» ") + P), U.Name, Names);
			FitCheck(R, *(TEXT("L6 «Вид» ") + P), U.Kind, Kinds);
			FitCheck(R, *(TEXT("L7 «Состояние» ") + P), U.State, States);
			FitCheck(R, *(TEXT("L8 «Ур.» ") + P), U.Level, Lv);
			FitCheck(R, *(TEXT("L8b «Кол-во» ") + P), U.Count, Counts);
		}
		FitCheck(R, TEXT("L9 строка подробностей 75242"), Bd->DetailHead, Heads);
		FitCheck(R, TEXT("L9b вкладки-зоны 75002"), Bd->TabTexts.Num() ? Bd->TabTexts[0].Get() : nullptr, Tabs, 6.f);
	}

	// ── Фаза 2: путь игрока на САМОМ ДЛИННОМ названии языка сессии — доска, где оно висит, его вкладка и строка ──
	//   (расширение окна 28.10.2026, `Tools/asda_ui_subjugate_widen.py`; проверка — фаза 3 по живой геометрии).
	static void Phase2(FRun& R)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaBoardQuestSubsystem* Bd = GI->GetSubsystem<UAsdaBoardQuestSubsystem>();
		UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
		const UTextBlock* F = Bd->RowUi[1].Name;
		float Max = 0.f;
		for (const FAsdaRaidQuestRow& Q : Bd->Quests)
		{
			if (Q.bHidden || !F) { continue; }
			const float W = AsdaUi::MeasureTextWidth(UAsdaBoardQuestSubsystem::QuestName(Q.QuestId), F->GetFont(), 0.f);
			if (W > Max) { Max = W; R.LongQuest = Q.QuestId; }
		}
		const FAsdaRaidQuestRow* LQ = Bd->FindQuest(R.LongQuest);
		int32 NpcId = -1;
		for (const FAsdaBoardNpcRow& N : Bd->Npcs) { if (LQ && N.QuestSlots.Contains(R.LongQuest)) { NpcId = N.NpcId; break; } }
		Bd->Close();
		if (NpcId < 0 || !Dlg || !Dlg->OpenForNpcId(NpcId)) { return; }
		for (int32 t = 0; t < Bd->Tabs.Num(); ++t) { if (Bd->Tabs[t].Zone == LQ->Zone) { Bd->OnTabSelected(t); break; } }
		const int32 Row = RowOf(Bd, R.LongQuest);
		if (Row >= 0) { Bd->OnRowClick(Row); }
		UE_LOG(LogAsdaBoardTest, Log, TEXT("ДОСКА самое длинное название: задание %d (NPC %d, строка %d, %.0f px)"), R.LongQuest, NpcId, Row, Max);
	}

	// Пересечение по x двух живых полей строки: правый край левого ≤ левый край правого + допуск.
	static bool NoOverlap(const UWidget* L, const UWidget* Rt, float Tol, float& OutGap)
	{
		FVector2D La(0.0), Lb(0.0), Ra(0.0), Rb(0.0);
		if (!AsdaUi::WidgetAbsRect(L, La, Lb) || !AsdaUi::WidgetAbsRect(Rt, Ra, Rb)) { OutGap = -999.f; return false; }
		OutGap = float(Ra.X - Lb.X);
		return OutGap >= -Tol;
	}

	// ── Фаза 3: L10…L13 — окно расширено ЦЕЛИКОМ и симметрично, колонки не наезжают, длинное название видно целиком ──
	static void Phase3(FRun& R)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaBoardQuestSubsystem* Bd = GI->GetSubsystem<UAsdaBoardQuestSubsystem>();
		UUserWidget* Wd = Bd->Widget;
		FVector2D Sa(0.0), Sb(0.0), La(0.0), Lb(0.0), Ra(0.0), Rb(0.0);
		const bool bWin = Wd && Bd->IsOpen() && AsdaUi::WidgetAbsRect(Wd, Sa, Sb)
			&& AsdaUi::WidgetAbsRect(Wd->GetWidgetFromName(TEXT("Subjugate_WIN_0_0")), La, Lb)
			&& AsdaUi::WidgetAbsRect(Wd->GetWidgetFromName(TEXT("Subjugate_WIN_0_2")), Ra, Rb);
		const float Ml = float(La.X - Sa.X), Mr = float(Sb.X - Rb.X);
		Expect(R, bWin && Ml >= 0.f && Mr >= 0.f && FMath::Abs(Ml - Mr) <= 2.f,
			FString::Printf(TEXT("L10 окно %.0f px в экране %.0f px, поля слева %.0f / справа %.0f (симметрично)"), Rb.X - La.X, Sb.X - Sa.X, Ml, Mr));
		const int32 Row = RowOf(Bd, R.LongQuest);
		const FAsdaBoardRowUi* U = (Row >= 0 && Row < UAsdaBoardQuestSubsystem::kRows) ? &Bd->RowUi[Row] : nullptr;
		const float NameW = FieldW(U ? U->Name.Get() : nullptr);
		const float NameD = (U && U->Name) ? float(U->Name->GetDesiredSize().X) : 0.f;
		Expect(R, U && U->Name && NameD > 0.f && NameD <= NameW + 1.f,
			FString::Printf(TEXT("L11 строка %d: самое длинное «%s» отрисовано %.0f px при поле %.0f px"), Row,
				U && U->Name ? *U->Name->GetText().ToString() : TEXT("—"), NameD, NameW));
		// Порядок колонок слева направо: Вид | Кол-во | Ур. | Задание | Состояние. «Кол-во»→«Ур.» — наложение 2 px разметки оригинала.
		float G1 = 0.f, G2 = 0.f, G3 = 0.f, G4 = 0.f;
		const bool bCols = U && NoOverlap(U->Kind, U->Count, 0.5f, G1) && NoOverlap(U->Count, U->Level, 2.5f, G2)
			&& NoOverlap(U->Level, U->Name, 0.5f, G3) && NoOverlap(U->Name, U->State, 0.5f, G4);
		Expect(R, bCols, FString::Printf(TEXT("L12 колонки строки %d не наезжают (зазоры %.0f / %.0f / %.0f / %.0f px)"), Row, G1, G2, G3, G4));
		// Шапка над колонками: центры совпадают (шапка TXT00…04 = Вид / Ур. / Задание / Состояние / Кол-во).
		float Worst = 0.f;
		bool bHead = U && Bd->HeadTexts.Num() == 5;
		if (bHead)
		{
			const UWidget* Cols[5] = { U->Kind, U->Level, U->Name, U->State, U->Count };
			for (int32 c = 0; c < 5; ++c)
			{
				FVector2D Ha(0.0), Hb(0.0), Ca(0.0), Cb(0.0);
				if (!AsdaUi::WidgetAbsRect(Bd->HeadTexts[c], Ha, Hb) || !AsdaUi::WidgetAbsRect(Cols[c], Ca, Cb)) { bHead = false; break; }
				Worst = FMath::Max(Worst, float(FMath::Abs((Ha.X + Hb.X) - (Ca.X + Cb.X)) * 0.5));
			}
		}
		Expect(R, bHead && Worst <= 2.5f, FString::Printf(TEXT("L13 шапка над колонками (расхождение центров до %.1f px)"), Worst));
		FVector2D Da(0.0), Db(0.0), Pa(0.0), Pb(0.0);
		const bool bDet = Bd->DetailHead && AsdaUi::WidgetAbsRect(Bd->DetailHead, Da, Db)
			&& AsdaUi::WidgetAbsRect(Wd ? Wd->GetWidgetFromName(TEXT("Subjugate_RBOX_grp")) : nullptr, Pa, Pb);
		const float DetD = Bd->DetailHead ? float(Bd->DetailHead->GetDesiredSize().X) : 0.f;
		// Правая панель — внутри рамки окна с тем же отступом от внешнего края, что и левая (12 px разметки оригинала).
		FVector2D Ka(0.0), Kb(0.0);
		const bool bL = AsdaUi::WidgetAbsRect(Wd ? Wd->GetWidgetFromName(TEXT("Subjugate_LBOX_grp")) : nullptr, Ka, Kb);
		const float PadL = float(Ka.X - La.X), PadR = float(Rb.X - Pb.X);
		Expect(R, bDet && bL && DetD <= float(Db.X - Da.X) + 1.f && PadR > 0.f && FMath::Abs(PadL - PadR) <= 2.f,
			FString::Printf(TEXT("L14 подробности: «%s» %.0f px при поле %.0f px; отступ панелей от рамки слева %.0f / справа %.0f"),
				Bd->DetailHead ? *Bd->DetailHead->GetText().ToString() : TEXT("—"), DetD, Db.X - Da.X, PadL, PadR));
		if (R.bShot)
		{
			// Песочница Saved_<суффикс> пакетного прогона удаляется — кадр сразу в папку отчёта.
			const FString Name = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(
				TEXT("../Reports/board_quest/shot_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")));
			FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			UE_LOG(LogAsdaBoardTest, Log, TEXT("ДОСКА кадр: %s"), *Name);
		}
	}

	static bool OnTick(float)
	{
		if (!Run().IsValid()) { return false; }
		FRun& R = *Run();
		UGameInstance* GI = R.GI.Get();
		if (!GI) { Run().Reset(); return false; }
		const double Now = FPlatformTime::Seconds();
		if (R.Phase == 0)
		{
			Phase0(R);
			R.Phase = 1; R.PhaseT = Now;
			return true;
		}
		if (Now - R.PhaseT < 1.0) { return true; }   // окну — кадры на отрисовку
		if (R.Phase == 1)
		{
			Phase1(R);
			PhaseFit(R);
			Phase2(R);
			R.Phase = 2; R.PhaseT = Now;
			return true;
		}
		if (R.Phase == 2)
		{
			Phase3(R);
			R.Phase = 3; R.PhaseT = Now;
			return true;
		}
		if (R.bShot && Now - R.PhaseT < 1.0) { return true; }   // кадр пишется в следующем кадре отрисовки
		Finish(R);
		return false;
	}

	static void Start(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, Cmd(), Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI || !GI->GetSubsystem<UAsdaBoardQuestSubsystem>()) { AsdaSelfTestKit::Refused(W, Cmd(), Args.bQuit, TEXT("нет подсистемы доски")); return; }
		if (Run().IsValid()) { AsdaSelfTestKit::Refused(W, Cmd(), Args.bQuit, TEXT("самотест уже идёт")); return; }
		Run() = MakeShared<FRun>();
		FRun& R = *Run();
		R.GI = GI; R.bQuit = Args.bQuit;
		R.bShot = A.Contains(TEXT("shot"));
		R.Caps = MakeShared<TArray<FCap>>();
		R.Keys = MakeUnique<FAsdaCVarScope>(W, Cmd());
		R.Keys->Set(TEXT("asda.Board.Enabled"), 1);
		R.Keys->Set(TEXT("asda.Board.FixStaleTabs"), 1);
		R.Keys->Set(TEXT("asda.Board.FixDailyState"), 1);
		R.Keys->Set(TEXT("asda.Board.FixServerType"), 1);
		R.Keys->Set(TEXT("asda.Board.FixPendingReset"), 1);
		R.Keys->Set(TEXT("asda.Board.FixMinimapLevel"), 1);
		R.Keys->Set(TEXT("asda.Board.InFlightTimeoutSec"), 30.f);
		R.Keys->Set(TEXT("asda.Board.FixOverheadMark"), 1);
		TSharedPtr<TArray<FCap>> C = R.Caps;
		R.Gen = AsdaSelfTestKit::ArmCapture(AsdaTestNet::Get(GI), [C](uint16 Op, const uint8* P, int32 Len, bool bPad, TCHAR Env)
		{
			FCap X; X.Op = Op; X.Env = Env; X.bPad = bPad; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, Cmd());
		if (R.Gen == 0) { Run().Reset(); AsdaSelfTestKit::Refused(W, Cmd(), Args.bQuit, TEXT("перехват отправок занят")); return; }
		if (GEngine) { GEngine->Exec(W, TEXT("asda.Quest.FakeSelf 40")); }   // уровень 40 (офлайн)
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FAsdaBoardQuestSelfTest::OnTick), 0.1f);
	}
};

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBoardQuest(
	TEXT("asda.SelfTest.BoardQuest"),
	TEXT("Самотест доски объявлений 75000: данные, развилка NPC типа 5, вкладки/строки/панель, 6588/6590/6594/6595, 6591/6593 под "
	     "перехватом, попап 540 «режим доски», исправления Д-1…Д-6, живая геометрия, расширенное окно на самом длинном названии (L10…L14). "
	     "Только офлайн. `quit`, `shot` — кадр Reports/board_quest/shot_<язык>.png."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		FAsdaBoardQuestSelfTest::Start(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
