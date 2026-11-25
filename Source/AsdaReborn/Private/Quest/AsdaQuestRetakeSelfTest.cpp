// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ДВА ЗАДАНИЯ У ОДНОГО NPC И ПОВТОРНЫЙ ПРИЁМ ПОСЛЕ ОТКАЗА» ПУТЁМ ИГРОКА (08.11.2026, пачка 17 баг 9: у Лучника Курьяна
//   (NPC 19) два задания — взяв одно, второе не взять; удалённое задание у NPC снова не взять).
//
//   ПУТЬ ИГРОКА: журнал — пакетом 5047 (`DebugFeedPacket`, раскладка сервера); разговор — `OpenForNpcId` (тот же вход, что щелчок по NPC:
//   перебор записей сценария → страница); приём — НАСТОЯЩИЙ щелчок по кнопке шага `NPC_QA_BUT00` (5043 под перехватом, в сеть ничего);
//   ответ 5044 — штатным приёмом; отказ — корзина 713 журнала и «Да» настоящими щелчками, ответ 5007 {1, ячейка, 4} штатным приёмом.
//   Оракул — правило данных и эталона, записанное здесь независимо от боевого кода: у Курьяна выдача 56 (класс 12, ур. 24+),
//   292 (класс 13, ур. 40+), 368 (ур. 33+); условие записи выдачи — «задание не взято и не завершено» (`questCheckAllPrereqDone`),
//   взаимоисключения «двух категорийных» (`CQuest_HasActiveQuestOrReq`, категория 1) у пары нет: 56 — категория 1, 292 и 368 — 0.
//   Значит: пустой журнал → облачко выбора (страница 1, выдача 2); взято одно → второе предлагается; отказ → снова оба.
//
// Консоль: `asda.SelfTest.QuestRetake [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`), на карте мира.
//   Строки проверок — «ПОВТОР-ЗАДАНИЯ ok/FAIL», итог — «ПОВТОР-ЗАДАНИЯ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaDataTableCache.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Text/AsdaLang.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuestRetakeTest, Log, All);

namespace AsdaQuestRetakeTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.QuestRetake");   // имя команды
	static constexpr int32 kNpc = 19;                                // Лучник Курьян, сценарий `kurian`

	// Перехваченная отправка: опкод и тело.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;            // пройдено / провалено
		TWeakObjectPtr<UWorld> W;          // мир прогона
		bool bQuit = false;                // выйти после итога
		uint32 CapGen = 0;                 // поколение перехвата отправок
		TArray<FSent> Sent;                // перехваченные отправки
		int32 Mark = 0;                    // число отправок на начало шага
		float T = 0.5f;                    // время следующего шага

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaQuestRetakeTest, Log,   TEXT("ПОВТОР-ЗАДАНИЯ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaQuestRetakeTest, Error, TEXT("ПОВТОР-ЗАДАНИЯ FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		void Exec(const FString& C) const { if (GEngine && W.IsValid()) { GEngine->Exec(W.Get(), *C); } }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Кадр экрана в `Reports/batch17/quests/<язык>_<имя>.png` (с интерфейсом).
	static void Shot(const TCHAR* Name)
	{
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch17/quests")));
		const FString File = FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
	}
	// Шаг через Dt секунд после предыдущего.
	static void Then(const FCtxRef& C, float Dt, TFunction<void()> Fn)
	{
		C->T += Dt;
		UWorld* W = C->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), C->T, false);
	}
	// Код эпизода задания (`Episode_Table +236 → +200`).
	static int32 EpisodeOf(int32 QuestNo)
	{
		if (const UDataTable* Ep = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/NPC/DT_EpisodeTable.DT_EpisodeTable")))
		{
			for (const TPair<FName, uint8*>& It : Ep->GetRowMap())
			{
				const FAsdaEpisodeRow* R = reinterpret_cast<const FAsdaEpisodeRow*>(It.Value);
				if (R && R->QuestKey == QuestNo) { return R->EpisodeId; }
			}
		}
		return 2001 + QuestNo;
	}
	// Журнал 5047: задания (номер, стадия) по ячейкам 0…; завершённые — в карте с +528.
	static void Journal(const FCtxRef& C, const TArray<TPair<int32, int32>>& Quests, const TArray<int32>& Done = {})
	{
		TArray<uint8> B; B.SetNumZeroed(688);
		for (int32 i = 0; i < Quests.Num() && i < 12; ++i)
		{
			uint8* R = B.GetData() + 44 * i;
			AsdaBytes::WrI32(R + 0, EpisodeOf(Quests[i].Key));
			R[4] = 4;
			AsdaBytes::WrI16(R + 5, i);
			R[7] = 2;
			AsdaBytes::WrI16(R + 8, Quests[i].Key);
			AsdaBytes::WrI16(R + 10, Quests[i].Value);
			for (int32 j = 0; j < 5; ++j) { AsdaBytes::WrI32(R + 14 + 6 * j, -1); }
		}
		for (int32 D : Done) { if (D >= 0 && (D >> 3) < 160) { B[528 + (D >> 3)] |= (uint8)(1u << (D & 7)); } }
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5047, B); }
	}
	// Число строк «выдаёт» в облачке выбора — из живого разбора окна (`DumpMultiQuest`: «выдача=N»).
	static int32 GiveCount(const FCtxRef& C)
	{
		UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
		if (!D) { return -1; }
		const FString S = D->DumpMultiQuest();
		const int32 P = S.Find(TEXT("выдача="));
		return P == INDEX_NONE ? -1 : FCString::Atoi(*S.Mid(P + 7, 4));
	}
	// Открыть разговор так же, как щелчок по NPC: перебор записей сценария выбирает страницу.
	static int32 Open(const FCtxRef& C)
	{
		UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
		if (!D) { return -1; }
		D->Close();
		D->ResetSessionState(EAsdaSessionResetReason::LeftWorld);   // снять «заявку в полёте» прошлого шага
		return D->OpenForNpcId(kNpc) ? D->GetCurrentTalkId() : -1;
	}
	// Метка над NPC после пересчёта (`RebuildMarks` → `GetMarkForNpcId`: 1 — «!», 2 — «?»).
	static int32 MarkOf(const FCtxRef& C)
	{
		UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
		if (!QM) { return -1; }
		C->Exec(TEXT("Asda.Load.CurrentMap 0"));   // метки считаются для NPC текущей карты; Курьян — карта 0
		QM->RebuildMarks();
		return QM->GetMarkForNpcId(kNpc);
	}
	// Центр живой геометрии узла окна.
	static bool CenterOf(const FCtxRef& C, const TCHAR* Win, const TCHAR* Node, FVector2D& Out)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Wd = WM ? WM->FindWindow(FName(Win)) : nullptr;
		UWidget* N = Wd ? AsdaUi::FindNodeByName(Wd, Node, nullptr) : nullptr;
		FVector2D Mn, Mx;
		if (!N || !AsdaUi::IsShown(N) || !AsdaUi::WidgetAbsRect(N, Mn, Mx) || (Mx - Mn).X < 2.0) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	static void Click(const FCtxRef& C, const TCHAR* Win, const TCHAR* Node, const FString& Tag)
	{
		FVector2D P;
		const bool b = CenterOf(C, Win, Node, P);
		C->Expect(b, FString::Printf(TEXT("%s: узел %s окна %s на экране"), *Tag, Node, Win));
		if (b) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 3); }
	}
	// Отправки опкода с начала шага; последняя — в OutLast.
	static int32 SentSince(const FCtxRef& C, int32 Op, TArray<uint8>* OutLast = nullptr)
	{
		int32 N = 0;
		for (int32 i = C->Mark; i < C->Sent.Num(); ++i) { if (C->Sent[i].Op == Op) { ++N; if (OutLast) { *OutLast = C->Sent[i].Body; } } }
		return N;
	}
	// Ответ 5044 «принято» на 5043: код 0, страница, эпизод +3, ячейка +8, номер +11, стадия +13 (`sub_45CA70`).
	static void Reply5044(const FCtxRef& C, int32 Page, int32 QuestNo, int32 Slot)
	{
		TArray<uint8> B; B.SetNumZeroed(47);
		B[0] = 0;
		AsdaBytes::WrI16(B.GetData() + 1, Page);
		AsdaBytes::WrI32(B.GetData() + 3, EpisodeOf(QuestNo));
		B[7] = 4;
		AsdaBytes::WrI16(B.GetData() + 8, Slot);
		B[10] = 2;
		AsdaBytes::WrI16(B.GetData() + 11, QuestNo);
		AsdaBytes::WrI16(B.GetData() + 13, 0);
		for (int32 j = 0; j < 5; ++j) { AsdaBytes::WrI32(B.GetData() + 17 + 6 * j, -1); }
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5044, B); }
	}
	// Ответ 5007 {1, ячейка, 4} — сервер подтвердил отказ (`sendCharQuestDump`).
	static void Reply5007(const FCtxRef& C, int32 Slot)
	{
		TArray<uint8> B; B.SetNumZeroed(18);
		B[0] = 1; AsdaBytes::WrI16(B.GetData() + 1, Slot); B[5] = 4;
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5007, B); }
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->DebugPumpPendingPackets(); }
	}

	// Случай «пустой журнал → оба задания в облачке».
	static void CaseBoth(const FCtxRef& C, const FString& Tag, int32 Lv, int32 Cls, int32 QA, int32 QB, const TArray<int32>& Done = {})
	{
		Then(C, 0.4f, [C, Tag, Lv, Cls, QA, QB, Done]()
		{
			C->Exec(FString::Printf(TEXT("asda.Quest.FakeSelf %d %d 0"), Lv, Cls));
			Journal(C, {}, Done);
			const int32 Page = Open(C);
			const int32 G = GiveCount(C);
			if (Tag.StartsWith(TEXT("A"))) { Shot(TEXT("kurian_1_both_offered")); }
			C->Expect(Page == 1 && G == 2, FString::Printf(TEXT("%s: журнал пуст → облачко выбора (страница %d, выдача %d; ждали 1 и 2: %d и %d)"),
				*Tag, Page, G, QA, QB));
			const int32 M = MarkOf(C);
			C->Expect(M == 1 || M == 2, FString::Printf(TEXT("%s: метка над Курьяном «!» (%d)"), *Tag, M));
		});
	}
	// Случай «взято A → предлагается B» (страница B).
	static void CaseOther(const FCtxRef& C, const FString& Tag, int32 Lv, int32 Cls, int32 Taken, int32 WantPage)
	{
		Then(C, 0.4f, [C, Tag, Lv, Cls, Taken, WantPage]()
		{
			C->Exec(FString::Printf(TEXT("asda.Quest.FakeSelf %d %d 0"), Lv, Cls));
			Journal(C, { { Taken, 0 } });
			const int32 Page = Open(C);
			C->Expect(Page == WantPage, FString::Printf(TEXT("%s: взято %d → страница %d (ждали %d — выдача второго)"), *Tag, Taken, Page, WantPage));
			const int32 M = MarkOf(C);
			C->Expect(M == 1 || M == 2, FString::Printf(TEXT("%s: метка «!» осталась (%d)"), *Tag, M));
		});
	}
	// Полный путь: приём щелчком на странице задания → 5044 → разговор предлагает второе → отказ в журнале → 5007 → снова оба.
	static void CaseTakeAbandon(const FCtxRef& C, const FString& Tag, int32 Lv, int32 Cls, int32 Quest, int32 Page, int32 OtherPage)
	{
		Then(C, 0.4f, [C, Tag, Lv, Cls, Page]()
		{
			C->Exec(FString::Printf(TEXT("asda.Quest.FakeSelf %d %d 0"), Lv, Cls));
			Journal(C, {});
			Open(C);
			if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { D->RenderPage(Page); }
			C->Mark = C->Sent.Num();
		});
		Then(C, 0.6f, [C, Tag]() { Click(C, TEXT("NPC_LINE_NPC"), TEXT("NPC_QA_BUT00"), Tag + TEXT(" «Принять»")); });
		Then(C, 0.8f, [C, Tag, Quest, Page]()
		{
			TArray<uint8> B;
			const int32 N = SentSince(C, 5043, &B);
			C->Expect(N == 1, FString::Printf(TEXT("%s: щелчок «Принять» → одна 5043 (ушло %d)"), *Tag, N));
			Reply5044(C, Page, Quest, 0);
			UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
			C->Expect(QM && QM->GetSlots().IsValidIndex(0) && QM->GetSlots()[0].QuestNo == Quest,
				FString::Printf(TEXT("%s: 5044 → задание %d в журнале"), *Tag, Quest));
		});
		Then(C, 0.4f, [C, Tag, OtherPage]()
		{
			const int32 P = Open(C);
			C->Expect(P == OtherPage, FString::Printf(TEXT("%s: после приёма разговор предлагает второе (страница %d, ждали %d)"), *Tag, P, OtherPage));
			if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { D->Close(); }
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			if (QL && !QL->IsOpen()) { QL->Open(); }
			if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { WM->BringWindowToFront(FName(TEXT("QUEST_WIN"))); }
		});
		Then(C, 0.6f, [C, Tag, Quest]() { if (Quest == 56) { Shot(TEXT("kurian_2_offers_second_after_take")); } });
		Then(C, 0.3f, [C, Tag]() { Click(C, TEXT("QUEST_WIN"), TEXT("QUEST_INVEN0_HIT"), Tag + TEXT(" ячейка 0")); });
		Then(C, 0.4f, [C, Tag]() { C->Mark = C->Sent.Num(); Click(C, TEXT("QUEST_WIN"), TEXT("QUEST_TRASHBUT"), Tag + TEXT(" корзина")); });
		Then(C, 0.8f, [C, Tag]() { Click(C, TEXT("TWOBUT_MSGBOX"), TEXT("TWOBUT_BUT00"), Tag + TEXT(" «Да»")); });
		Then(C, 0.8f, [C, Tag, Quest]()
		{
			C->Expect(SentSince(C, 5006) == 1, FString::Printf(TEXT("%s: отказ → одна 5006"), *Tag));
			Reply5007(C, 0);
			UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
			C->Expect(QM && QM->GetSlots().IsValidIndex(0) && QM->GetSlots()[0].QuestId < 2001, FString::Printf(TEXT("%s: 5007 → журнал пуст"), *Tag));
			C->Expect(QM && !QM->IsQuestCompletedPublic(Quest), FString::Printf(TEXT("%s: отказ не ставит «завершено» задания %d"), *Tag, Quest));
			if (UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>()) { QL->Close(); }
		});
		Then(C, 0.4f, [C, Tag]()
		{
			const int32 P = Open(C);
			const int32 G = GiveCount(C);
			C->Expect(P == 1 && G == 2, FString::Printf(TEXT("%s: удалённое снова доступно — облачко выбора (страница %d, выдача %d)"), *Tag, P, G));
			if (Tag.StartsWith(TEXT("G"))) { Shot(TEXT("kurian_3_after_abandon_both_again")); }
			const int32 M = MarkOf(C);
			C->Expect(M == 1 || M == 2, FString::Printf(TEXT("%s: метка «!» над Курьяном после отказа (%d)"), *Tag, M));
		});
	}

	// Взято A → разговор → страница приёма B → щелчок «Принять» → ровно одна 5043.
	static void CaseTakeSecond(const FCtxRef& C, const FString& Tag, int32 Lv, int32 Cls, int32 Taken, int32 AcceptPage)
	{
		Then(C, 0.4f, [C, Lv, Cls, Taken, AcceptPage]()
		{
			C->Exec(FString::Printf(TEXT("asda.Quest.FakeSelf %d %d 0"), Lv, Cls));
			Journal(C, { { Taken, 0 } });
			Open(C);
			if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { D->RenderPage(AcceptPage); }
			C->Mark = C->Sent.Num();
		});
		Then(C, 0.6f, [C, Tag]() { Click(C, TEXT("NPC_LINE_NPC"), TEXT("NPC_QA_BUT00"), Tag + TEXT(" «Принять»")); });
		Then(C, 0.8f, [C, Tag, AcceptPage]()
		{
			TArray<uint8> B;
			const int32 N = SentSince(C, 5043, &B);
			C->Expect(N == 1, FString::Printf(TEXT("%s: страница %d, «Принять» → одна 5043 (ушло %d)"), *Tag, AcceptPage, N));
		});
	}

	static void Finish(const FCtxRef& C)
	{
		if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { D->Close(); D->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>()) { QL->Close(); }
		Journal(C, {});
		C->Exec(TEXT("asda.Quest.FakeSelf"));
		C->Exec(TEXT("Asda.Load.CurrentMap 3"));
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; } }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaQuestRetakeTest, TEXT("ПОВТОР-ЗАДАНИЯ"), C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Net || !C->Sub<UAsdaNpcDialogSubsystem>() || !C->Sub<UAsdaQuestMarkerSubsystem>()) { C->Expect(false, TEXT("подсистемы есть")); Finish(C); return; }
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { FSent S; S.Op = (int32)Op; if (P && Len > 0) { S.Body.Append(P, Len); } X->Sent.Add(MoveTemp(S)); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }

		// Первая встреча показывает знакомство (стр. 63, `MetNpc`) — снимаем её одним разговором до проверок.
		Then(C, 0.2f, [C]() { C->Exec(TEXT("asda.Quest.FakeSelf 35 12 0")); Journal(C, {}); Open(C); });
		// Класс 12, уровень 35: 56 («Поиск веры», стр. 12) и 368 (стр. 20 → приём на стр. 54).
		CaseBoth(C, TEXT("A класс 12 ур. 35"), 35, 12, 56, 368);
		CaseOther(C, TEXT("B класс 12, взято 368"), 35, 12, 368, 12);
		CaseOther(C, TEXT("C класс 12, взято 56"), 35, 12, 56, 20);
		// Класс 13, уровень 45: 292 (стр. 19) и 368 (стр. 20; потолок уровня 40 снят — `asda.Quest.LevelCapEnforce`).
		CaseBoth(C, TEXT("D класс 13 ур. 45"), 45, 13, 292, 368);
		CaseOther(C, TEXT("E класс 13, взято 292"), 45, 13, 292, 20);
		CaseOther(C, TEXT("F класс 13, взято 368"), 45, 13, 368, 19);
		// Полный путь приёма и отказа.
		CaseTakeAbandon(C, TEXT("G приём 56 → отказ"), 35, 12, 56, 12, 20);
		CaseTakeAbandon(C, TEXT("H приём 368 → отказ"), 35, 12, 368, 54, 12);
		CaseTakeAbandon(C, TEXT("I класс 13: приём 292 → отказ"), 45, 13, 292, 50, 20);
		// Второе задание берётся, пока первое в журнале: щелчок «Принять» на его странице уходит 5043.
		CaseTakeSecond(C, TEXT("J взято 368 → приём 56"), 35, 12, 368, 12);
		CaseTakeSecond(C, TEXT("K взято 56 → приём 368"), 35, 12, 56, 54);
		CaseTakeSecond(C, TEXT("L класс 13, взято 292 → приём 368"), 45, 13, 292, 54);
		CaseTakeSecond(C, TEXT("M класс 13, взято 368 → приём 292"), 45, 13, 368, 50);
		Then(C, 0.6f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestQuestRetake(
	TEXT("asda.SelfTest.QuestRetake"),
	TEXT("Самотест: два задания у Лучника Курьяна (NPC 19) — оба доступны, взятое не прячет второе, после отказа задание снова "
	     "у NPC (облачко, метка). Приём и отказ настоящими щелчками, 5043/5006 под перехватом. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaQuestRetakeTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
