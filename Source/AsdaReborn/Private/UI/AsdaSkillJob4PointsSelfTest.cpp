// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ 05.11.2026 (сессия job4-points): `asda.SelfTest.SkillJob4Points [delay=N] [quit]` — ДВА ПУЛА ОЧКОВ НАВЫКОВ
//   (ключ `asda.Skill.Job4Pools`, шапка ключа и `ComputePointPools` — `AsdaSkillWindowSubsystem.cpp`). [ИСПРАВЛЕНИЕ]
//   Жалоба игроков: «4-я профессия: можно изучать навыки сверх лимита очков, уже −8; тратятся очки основного дерева».
//   Путь игрока, офлайн (-AsdaNoNet): своё состояние — поля сети (как после 1017/4005), список навыков — НАСТОЯЩИЕ пакеты 5251
//   (записи 34 б `sub_476E10`), ответы — 5254 (`Recv_SkillPoint_SkillActive@0x4774C0`, 111 б), сбросы — 5464 (`sub_480F20`: очки +
//   grade) и 5462 (`sub_4803A0`) с последующим 5251, всё через `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → окно.
//   Клик — штатный `OnSlotClicked` узла (вкладка + прокрутка как у игрока); 5253 — БОЕВОЙ `SendSkillLearn5253` под узким перехватом.
//   Персонаж: боец 4-й профессии (профессия 1, класс 3, подкласс 4), уровень 70 = 132 очка (2·70−8).
//     A. основные 106 + 4-я 20, свободных 6: счётчики 4 / 2; основной узел → 5253, ответ → 3 / 2; узел 4-й профессии → 5253 → 3 / 1.
//     B. основные 110 + 4-я 20, свободных 2: 0 / 2; основной узел — подсветки нет, msgbox 2202, 5253 нет; 4-я → 5253 (×2 до 0 / 0).
//     C. основные 104 + 4-я 22, свободных 6: 6 / 0; узел 4-й профессии — подсветки нет, msgbox 111701, 5253 нет; основной → 5253.
//     D. отказы сервера 5254 код 8 → msgbox 111701, код 3 → msgbox 2202 (ответ P110).
//     E. долг старых данных (живой dev, «Player58»): основные 10 + 4-я 30, свободных 92 → 92 / −8; 4-я — отказ; основной → 5253.
//     F. сброс дерева 4-й профессии (5464 очки 122 grade 10 + 5251 с уровнями 0) → 100 / 22; полный сброс (5462 132 + 5251) → 110 / 22.
//     G. уровень 59 (110 очков): основные 100, свободных 10 → 10 / 0; 4-я — отказ; основной → 5253.
//     H. подтверждение 9415 (ключ NoConfirm 0): основной узел → вопрос TWOBUT, «Да» → 5253; узел 4-й при пустом пуле — вопроса нет.
//     I. ключ `asda.Skill.Job4Pools` 0 (состояние B): счётчик 1092 = 2 (общий счёт), основной узел → 5253 (прежнее поведение).
//   Итог — «ОЧКИ-4-ПРОФ ИТОГ: ok=… fail=…». Тексты отказов — из таблицы языка прогона (RU/EN), сверка с `AsdaNotice::LocRaw`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaJob4PointsTest, Log, All);

namespace AsdaJob4PointsTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SkillJob4Points");   // имя команды (и реестр ключей AsdaCVarSave)

	// Перехватчик msgbox окна навыков (строка `[навыки] msgbox: …` пишет `MsgBox1`).
	class FSink : public FOutputDevice
	{
	public:
		TArray<FString> Msg;   // тексты показанных msgbox
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (!IsInGameThread() || !V) { return; }   // строки пишет проверяемый код игрового потока
			static const FName kSkill(TEXT("LogAsdaSkillWin"));
			if (Category != kSkill) { return; }
			const FString S(V);
			if (S.StartsWith(TEXT("[навыки] msgbox: "))) { Msg.Add(S.Mid(17)); }
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // небуферизованный приём (строка — в момент записи)
	};
}

struct FAsdaSkillJob4PointsSelfTest
{
	// Снимок того, что трогает прогон (возвращается в конце).
	struct FSaved
	{
		uint8 Level = 0; int32 Job = -1, Cls = -1, Sub = -1, Pts = -1;               // свой персонаж (поля сети)
		TMap<int32, FAsdaLearnedSkill> Learned;                                    // модель окна навыков
		int32 WinPoints = -1, Tree = 0, Scroll = 0, Selected = -1, JobOv = -1, SubOv = -1;   // счётчик/вкладка/прокрутка/подмены окна
		bool bWinOpen = false;                                                     // было ли окно открыто
	};

	UWorld* W = nullptr;                       // мир прогона
	UAsdaNetSubsystem* Net = nullptr;          // сеть
	UAsdaSkillWindowSubsystem* SW = nullptr;   // окно навыков
	UAsdaWindowSubsystem* WM = nullptr;        // оконная подсистема (msgbox, вопрос)
	const UAsdaSkillDatabase* Db = nullptr;    // таблица навыков клиента
	int32 Ok = 0, Fail = 0;                    // счётчики проверок
	uint32 CapGen = 0;                         // поколение перехвата 5253
	TArray<TArray<uint8>> Sent;                // тела перехваченных 5253
	AsdaJob4PointsTestLocal::FSink Sink;       // msgbox окна навыков
	FSaved Saved;                              // снимок

	// Навыки дерева полосы бойца, у которых есть узел сетки и уровень ≥ 1.
	TArray<int32> TreeCodes[4];
	int32 M = -1, M2 = -1, J = -1, J2 = -1;    // цели кликов: два основных (вкладка 0) и два 4-й профессии (вкладка 3)
	static constexpr int32 kJob4Msg = 111701;  // нотис отказа 5254 код 8 «недостаточно очков после 4-й профессии»
	using FPools = UAsdaSkillWindowSubsystem::FAsdaSkillPointPools;   // пулы очков окна

	void Check(bool b, const FString& What)
	{
		if (b) { ++Ok; UE_LOG(LogAsdaJob4PointsTest, Log, TEXT("ОЧКИ-4-ПРОФ ok   %s"), *What); }
		else   { ++Fail; UE_LOG(LogAsdaJob4PointsTest, Error, TEXT("ОЧКИ-4-ПРОФ FAIL %s"), *What); }
	}

	// 5251 — запись дерева 34 б (`sub_476E10`): @0 i16 код, @2 u8 уровень, @3 u8 «можно вкачивать».
	static void PutRec34(TArray<uint8>& B, int32 Code, int32 Level, int32 bAvail)
	{
		const int32 Off = B.AddZeroed(34);
		AsdaBytes::WrI16(B.GetData() + Off, Code); B[Off + 2] = (uint8)Level; B[Off + 3] = (uint8)bAvail;
	}
	// 5254 (111 б): @0 код (1 — успех), @1 i16 свободные очки, @3 i16 навык, @5 u8 уровень.
	static TArray<uint8> Make5254(int32 Code, int32 Points, int32 Skill, int32 Level)
	{
		TArray<uint8> B; B.SetNumZeroed(111);
		B[0] = (uint8)Code; AsdaBytes::WrI16(B.GetData() + 1, Points); AsdaBytes::WrI16(B.GetData() + 3, Skill); B[5] = (uint8)Level;
		return B;
	}
	// 5464 (`sub_480F20`, 3 б): @0 i16 очки, @2 u8 grade сброшенного дерева. 5462 (`sub_4803A0`): @0 i16 очки.
	static TArray<uint8> Make5464(int32 Points, int32 Grade)
	{
		TArray<uint8> B; B.SetNumZeroed(3); AsdaBytes::WrI16(B.GetData(), Points); B[2] = (uint8)Grade; return B;
	}
	static TArray<uint8> Make5462(int32 Points)
	{
		TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), Points); return B;
	}

	void Feed(int32 Op, const TArray<uint8>& B) { Net->DebugFeedPacket(Op, B); SW->DrainPending(); }   // штатный HandlePacket + Tick окна

	// Своё состояние (как держит сеть после 1017/4005) и 5251 «полный список»: основные деревья суммой MainSum, 4-я — Job4Sum, цели
	//   M/M2/J/J2 — уровень 0 и «можно вкачивать»; затем 5254 успеха (навык 0 не трогает) — очки окна = Free.
	void Setup(int32 Level, int32 MainSum, int32 Job4Sum, int32 Free)
	{
		Net->SelfLevel = (uint8)Level; Net->SelfJob = 1; Net->SelfClassId = 3; Net->SelfSubJob = 4; Net->SelfSkillPoints = Free;
		TArray<uint8> L;
		auto Fill = [&](int32 T0, int32 T1, int32 Sum)
		{
			for (int32 T = T0; T <= T1; ++T)
			{
				for (int32 C : TreeCodes[T])
				{
					if (C == M || C == M2 || C == J || C == J2) { PutRec34(L, C, 0, 1); continue; }
					const FAsdaSkillDef* D = Db->Get(C);
					const int32 Lv = FMath::Clamp(Sum, 0, D ? D->MaxLevel : 0);
					Sum -= Lv;
					PutRec34(L, C, Lv, 1);
				}
			}
			return Sum;
		};
		const int32 RestMain = Fill(0, 2, MainSum);
		const int32 RestJob4 = Fill(3, 3, Job4Sum);
		WM->ClearMsgBoxQueue(TEXT("самотест очков 4-й профессии: подготовка"));
		Feed(5462, Make5462(Free));   // очки окна — как у сервера при сбросе (`sub_4803A0`: 5462, затем полный список 5251)
		Feed(5251, L);
		Check(RestMain == 0 && RestJob4 == 0, FString::Printf(TEXT("подготовка: ур. %d, основные %d, 4-я %d, свободных %d (недобор %d/%d)"),
			Level, MainSum, Job4Sum, Free, RestMain, RestJob4));
	}

	// Показать узел навыка (вкладка дерева + прокрутка до ряда); индекс узла или −1.
	int32 ShowSlot(int32 Code)
	{
		const FAsdaSkillDef* D = Db->Get(Code);
		if (!D || !SW->IsWindowOpen()) { return -1; }
		SW->SetTree(UAsdaSkillDatabase::TreeFromGrade(D->Grade));
		SW->SetScrollRow(FMath::Max(0, D->AbsSlot() / SW->GridColumns - (SW->GridRows - 1)));
		for (int32 i = 0; i < SW->Slots.Num(); ++i) { if (SW->Slots[i].SkillCode == Code) { return i; } }
		return -1;
	}
	// Подсветка «можно учить» узла.
	bool Selectable(int32 Slot) const
	{
		return SW->Slots.IsValidIndex(Slot) && SW->Slots[Slot].Selectable
			&& SW->Slots[Slot].Selectable->GetVisibility() == ESlateVisibility::SelfHitTestInvisible;
	}
	// Клик игрока по узлу; число ушедших 5253 (тело должно быть ровно «код, 1, 1»).
	int32 Click(int32 Code, int32& OutSlot, bool& bBodyOk)
	{
		Sent.Reset();
		Sink.Msg.Reset();
		OutSlot = ShowSlot(Code);
		if (OutSlot >= 0) { SW->OnSlotClicked(OutSlot); }
		bBodyOk = Sent.Num() == 1 && Sent[0].Num() == 4 && AsdaBytes::RdI16(Sent[0].GetData()) == Code && Sent[0][2] == 1 && Sent[0][3] == 1;
		return Sent.Num();
	}
	// Тексты счётчиков 1092 / 12413.
	FString MainTxt() const { return SW->TxtSkillPoint ? SW->TxtSkillPoint->GetText().ToString() : FString(TEXT("<нет>")); }
	FString Job4Txt() const { return SW->TxtJob4Point ? SW->TxtJob4Point->GetText().ToString() : FString(TEXT("<нет>")); }
	bool Counters(int32 WantMain, int32 WantJob4) const
	{
		return MainTxt() == FString::FromInt(WantMain) && Job4Txt() == FString::FromInt(WantJob4);
	}
	FString Loc(int32 Id) const { return AsdaNotice::LocRaw(SW->GetGameInstance(), Id, /*bDebugFallback*/ true); }
	bool LastMsgIs(int32 Id) const { return Sink.Msg.Num() > 0 && Sink.Msg.Last() == Loc(Id) && !Loc(Id).StartsWith(TEXT("[#")); }
	FString LastMsg() const { return Sink.Msg.Num() ? Sink.Msg.Last() : FString(TEXT("—")); }

	void Run();
	void Restore();
};

void FAsdaSkillJob4PointsSelfTest::Run()
{
	using namespace AsdaJob4PointsTestLocal;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	SW = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Db = SW ? SW->GetDatabase() : nullptr;
	if (!Net || !SW || !WM || !Db)
	{
		Check(false, TEXT("нет подсистемы сети / окна навыков с базой / окон"));
		return;
	}
	// ── снимок ──
	Saved.Level = Net->SelfLevel; Saved.Job = Net->SelfJob; Saved.Cls = Net->SelfClassId; Saved.Sub = Net->SelfSubJob; Saved.Pts = Net->SelfSkillPoints;
	Saved.Learned = SW->Learned; Saved.WinPoints = SW->SkillPoints; Saved.Tree = SW->CurTree; Saved.Scroll = SW->ScrollRow;
	Saved.Selected = SW->SelectedSlot; Saved.JobOv = SW->JobOverride; Saved.SubOv = SW->SubJobOverride; Saved.bWinOpen = SW->IsWindowOpen();
	AsdaCVarSave::SetForTest(TEXT("asda.Skill.Job4Pools"), 1);
	AsdaCVarSave::SetForTest(TEXT("asda.Skill.NoConfirm"), 1);
	AsdaCVarSave::SetForTest(TEXT("asda.Job4.StageFromSubJob"), 1);
	WM->ClearMsgBoxQueue(TEXT("самотест очков 4-й профессии: старт"));
	SW->JobOverride = -1; SW->SubJobOverride = -1;   // класс — только из сети
	if (SW->IsWindowOpen()) { SW->CloseWindow(); }
	GLog->AddOutputDevice(&Sink);
	CapGen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		if (Op == 5253) { Sent.Add(TArray<uint8>(P, FMath::Max(0, Len))); }
	}, 5253, kCmd);
	Check(CapGen != 0, TEXT("узкий перехват 5253 взведён (в сеть ничего не уходит)"));
	if (CapGen == 0) { return; }

	// ── навыки полосы бойца: узел сетки, макс. уровень ≥ 1; цели — первые подходящие узлы вкладок 0 и 3 ──
	Net->SelfLevel = 70; Net->SelfJob = 1; Net->SelfClassId = 3; Net->SelfSubJob = 4;
	for (int32 T = 0; T < 4; ++T)
	{
		TArray<int32> Codes; Db->GetTreeSkills(1, T, Codes);
		for (int32 C : Codes) { const FAsdaSkillDef* D = Db->Get(C); if (D && D->MaxLevel >= 1) { TreeCodes[T].Add(C); } }
	}
	if (TreeCodes[0].Num() >= 2) { M = TreeCodes[0][0]; M2 = TreeCodes[0][1]; }
	if (TreeCodes[3].Num() >= 2) { J = TreeCodes[3][0]; J2 = TreeCodes[3][1]; }
	Check(M > 0 && M2 > 0 && J > 0 && J2 > 0 && SW->GetSelfBand() == 1,
		FString::Printf(TEXT("цели: основные %d, %d (grade %d); 4-я профессия %d, %d (grade %d); навыков по вкладкам %d/%d/%d/%d; полоса %d"),
			M, M2, M > 0 ? Db->Get(M)->Grade : -1, J, J2, J > 0 ? Db->Get(J)->Grade : -1,
			TreeCodes[0].Num(), TreeCodes[1].Num(), TreeCodes[2].Num(), TreeCodes[3].Num(), SW->GetSelfBand()));
	if (M <= 0 || J <= 0) { return; }
	SW->OpenWindow();
	Check(SW->IsWindowOpen(), TEXT("окно навыков открыто"));
	if (!SW->IsWindowOpen()) { return; }
	int32 Slot = -1; bool bBody = false;

	// ═══ A. основные 106 + 4-я 20, свободных 6 (ур. 70 = 132) ═══
	{
		Setup(70, 106, 20, 6);
		ShowSlot(M);
		Check(Counters(4, 2), FString::Printf(TEXT("A счётчики: основной «%s» (ждём 4 = min(6, 110−106)), 4-я «%s» (ждём 2 = 106+6−110)"), *MainTxt(), *Job4Txt()));
		const int32 N = Click(M, Slot, bBody);
		Check(N == 1 && bBody && Sink.Msg.Num() == 0, FString::Printf(TEXT("A основной узел %d (навык %d): 5253 ×%d, тело %s, msgbox %d"), Slot, M, N, bBody ? TEXT("верно") : TEXT("НЕ ТО"), Sink.Msg.Num()));
		Feed(5254, Make5254(1, 5, M, 1));
		ShowSlot(M);
		Check(SW->GetLearnedLevel(M) == 1 && Counters(3, 2), FString::Printf(TEXT("A ответ 5254: навык %d ур. %d, счётчики «%s»/«%s» (ждём 3/2)"), M, SW->GetLearnedLevel(M), *MainTxt(), *Job4Txt()));
		const int32 N2 = Click(J, Slot, bBody);
		Check(N2 == 1 && bBody, FString::Printf(TEXT("A узел 4-й профессии %d (навык %d): 5253 ×%d"), Slot, J, N2));
		Feed(5254, Make5254(1, 4, J, 1));
		ShowSlot(J);
		Check(SW->GetLearnedLevel(J) == 1 && Counters(3, 1), FString::Printf(TEXT("A ответ 5254: навык %d ур. %d, счётчики «%s»/«%s» (ждём 3/1)"), J, SW->GetLearnedLevel(J), *MainTxt(), *Job4Txt()));
	}
	// ═══ B. граница основного пула: основные 110 + 4-я 20, свободных 2 ═══
	{
		Setup(70, 110, 20, 2);
		const int32 SlotM = ShowSlot(M);
		Check(Counters(0, 2) && !Selectable(SlotM), FString::Printf(TEXT("B счётчики «%s»/«%s» (ждём 0/2); подсветка основного узла %s"), *MainTxt(), *Job4Txt(), Selectable(SlotM) ? TEXT("ЕСТЬ") : TEXT("нет (верно)")));
		const int32 N = Click(M, Slot, bBody);
		Check(N == 0 && LastMsgIs(2202), FString::Printf(TEXT("B основной узел при пустом основном пуле: 5253 ×%d (ждём 0), msgbox «%s» (ждём 2202 «%s»)"), N, *LastMsg(), *Loc(2202)));
		const int32 SlotJ = ShowSlot(J);
		Check(Selectable(SlotJ), TEXT("B подсветка узла 4-й профессии есть"));
		const int32 N2 = Click(J, Slot, bBody);
		Check(N2 == 1 && bBody, FString::Printf(TEXT("B узел 4-й профессии: 5253 ×%d"), N2));
		Feed(5254, Make5254(1, 1, J, 1));
		const int32 N3 = Click(J2, Slot, bBody);
		Feed(5254, Make5254(1, 0, J2, 1));
		ShowSlot(J2);
		Check(N3 == 1 && Counters(0, 0), FString::Printf(TEXT("B второе очко 4-й профессии: 5253 ×%d, счётчики «%s»/«%s» (ждём 0/0)"), N3, *MainTxt(), *Job4Txt()));
	}
	// ═══ C. граница пула 4-й профессии: основные 104 + 4-я 22, свободных 6 ═══
	{
		Setup(70, 104, 22, 6);
		const int32 SlotJ = ShowSlot(J);
		Check(Counters(6, 0) && !Selectable(SlotJ), FString::Printf(TEXT("C счётчики «%s»/«%s» (ждём 6/0); подсветка узла 4-й %s"), *MainTxt(), *Job4Txt(), Selectable(SlotJ) ? TEXT("ЕСТЬ") : TEXT("нет (верно)")));
		const int32 N = Click(J, Slot, bBody);
		Check(N == 0 && LastMsgIs(kJob4Msg), FString::Printf(TEXT("C узел 4-й профессии при пустом пуле: 5253 ×%d (ждём 0), msgbox «%s» (ждём 111701 «%s»)"), N, *LastMsg(), *Loc(kJob4Msg)));
		const int32 N2 = Click(M, Slot, bBody);
		Check(N2 == 1 && bBody, FString::Printf(TEXT("C основной узел: 5253 ×%d"), N2));
	}
	// ═══ D. отказы сервера (P110): 5254 код 8 и код 3 ═══
	{
		WM->ClearMsgBoxQueue(TEXT("самотест очков 4-й профессии: D"));
		Sink.Msg.Reset();
		Feed(5254, Make5254(8, 0, 0, 0));
		Check(LastMsgIs(kJob4Msg), FString::Printf(TEXT("D 5254 код 8 → msgbox «%s» (ждём 111701)"), *LastMsg()));
		Feed(5254, Make5254(3, 0, 0, 0));
		Check(LastMsgIs(2202), FString::Printf(TEXT("D 5254 код 3 → msgbox «%s» (ждём 2202)"), *LastMsg()));
	}
	// ═══ E. долг старых данных (живой dev 05.11, Player58): основные 10 + 4-я 30, свободных 92 ═══
	{
		Setup(70, 10, 30, 92);
		ShowSlot(J);
		Check(Counters(92, -8), FString::Printf(TEXT("E счётчики «%s»/«%s» (ждём 92/−8 — долг виден)"), *MainTxt(), *Job4Txt()));
		const int32 N = Click(J, Slot, bBody);
		Check(N == 0 && LastMsgIs(kJob4Msg), FString::Printf(TEXT("E узел 4-й профессии при долге: 5253 ×%d (ждём 0), msgbox «%s»"), N, *LastMsg()));
		const int32 N2 = Click(M, Slot, bBody);
		Check(N2 == 1 && bBody, FString::Printf(TEXT("E основной узел: 5253 ×%d"), N2));
	}
	// ═══ F. сбросы: дерево 4-й профессии (5464 grade 10) и полный (5462) ═══
	{
		Setup(70, 100, 30, 2);
		WM->ClearMsgBoxQueue(TEXT("самотест очков 4-й профессии: F"));
		Feed(5464, Make5464(32, 10));     // sub_480F20: очки = прежние + вложенное в дерево (2 + 30)
		TArray<uint8> L;
		for (int32 T = 0; T < 3; ++T) { int32 S = T == 0 ? 100 : 0; for (int32 C : TreeCodes[T]) { const FAsdaSkillDef* D = Db->Get(C); const int32 Lv = FMath::Clamp(S, 0, D ? D->MaxLevel : 0); S -= Lv; PutRec34(L, C, (C == M || C == M2) ? 0 : Lv, 1); } }
		for (int32 C : TreeCodes[3]) { PutRec34(L, C, 0, 1); }
		Feed(5251, L);                    // сервер после 5464 шлёт полный список (sub_480F20 → 5251)
		ShowSlot(J);
		const FPools P = SW->ComputePointPools();
		Check(SW->GetSkillPoints() == 32 && P.Job4Spent == 0 && Counters(FMath::Min(32, 110 - P.MainSpent), FMath::Min(32, P.MainSpent + 32 - 110)),
			FString::Printf(TEXT("F сброс дерева 4-й: очков %d, основные %d, 4-я %d, счётчики «%s»/«%s»"), SW->GetSkillPoints(), P.MainSpent, P.Job4Spent, *MainTxt(), *Job4Txt()));
		Feed(5462, Make5462(132));        // sub_4803A0: очки = 2·ур.−8
		TArray<uint8> L0;
		for (int32 T = 0; T < 4; ++T) { for (int32 C : TreeCodes[T]) { PutRec34(L0, C, 0, 1); } }
		Feed(5251, L0);
		ShowSlot(J);
		Check(SW->GetSkillPoints() == 132 && Counters(110, 22), FString::Printf(TEXT("F полный сброс: очков %d, счётчики «%s»/«%s» (ждём 110/22)"), SW->GetSkillPoints(), *MainTxt(), *Job4Txt()));
	}
	// ═══ G. уровень 59 (110 очков): основные 100, свободных 10 ═══
	{
		Setup(59, 100, 0, 10);
		ShowSlot(J);
		Check(Counters(10, 0), FString::Printf(TEXT("G ур. 59: счётчики «%s»/«%s» (ждём 10/0)"), *MainTxt(), *Job4Txt()));
		const int32 N = Click(J, Slot, bBody);
		Check(N == 0 && LastMsgIs(kJob4Msg), FString::Printf(TEXT("G ур. 59, узел 4-й профессии: 5253 ×%d (ждём 0), msgbox «%s»"), N, *LastMsg()));
		const int32 N2 = Click(M, Slot, bBody);
		Check(N2 == 1 && bBody, FString::Printf(TEXT("G ур. 59, основной узел: 5253 ×%d"), N2));
	}
	// ═══ H. подтверждение 9415 (как у игрока): основной узел → вопрос → «Да» → 5253; 4-я при пустом пуле — вопроса нет ═══
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Skill.NoConfirm"), 0);
		Setup(70, 104, 22, 6);
		WM->ClearMsgBoxQueue(TEXT("самотест очков 4-й профессии: H"));
		const int32 N0 = Click(M, Slot, bBody);
		UUserWidget* Q = WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX")));
		UButton* Yes = Q ? Cast<UButton>(Q->GetWidgetFromName(FName(TEXT("TWOBUT_BUT00")))) : nullptr;
		Check(N0 == 0 && Yes != nullptr, FString::Printf(TEXT("H основной узел: до ответа 5253 ×%d, вопрос 9415 %s"), N0, Yes ? TEXT("показан") : TEXT("НЕТ")));
		Sent.Reset();
		if (Yes) { Yes->OnClicked.Broadcast(); }
		const bool bHBody = Sent.Num() == 1 && Sent[0].Num() == 4 && AsdaBytes::RdI16(Sent[0].GetData()) == M;
		Check(bHBody, FString::Printf(TEXT("H «Да» → 5253 ×%d навыка %d"), Sent.Num(), M));
		WM->ClearMsgBoxQueue(TEXT("самотест очков 4-й профессии: H2"));
		const int32 N1 = Click(J, Slot, bBody);
		UUserWidget* Q2 = WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX")));
		const bool bQ2 = Q2 && Q2->IsVisible();
		Check(N1 == 0 && !bQ2 && LastMsgIs(kJob4Msg), FString::Printf(TEXT("H узел 4-й профессии при пустом пуле: вопроса %s, 5253 ×%d, msgbox «%s»"), bQ2 ? TEXT("ЕСТЬ") : TEXT("нет"), N1, *LastMsg()));
		AsdaCVarSave::SetForTest(TEXT("asda.Skill.NoConfirm"), 1);
	}
	// ═══ I. ключ 0 — прежнее (общий счёт): состояние B ═══
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Skill.Job4Pools"), 0);
		Setup(70, 110, 20, 2);
		WM->ClearMsgBoxQueue(TEXT("самотест очков 4-й профессии: I"));
		ShowSlot(M);
		const FString Main = MainTxt();
		const int32 N = Click(M, Slot, bBody);
		Check(Main == TEXT("2") && N == 1 && bBody, FString::Printf(TEXT("I ключ 0: счётчик 1092 «%s» (ждём 2 — общий счёт), основной узел → 5253 ×%d (прежнее)"), *Main, N));
		AsdaCVarSave::SetForTest(TEXT("asda.Skill.Job4Pools"), 1);
	}
	UE_LOG(LogAsdaJob4PointsTest, Log, TEXT("ОЧКИ-4-ПРОФ тексты: 111701 «%s», 2202 «%s»"), *Loc(kJob4Msg), *Loc(2202));
}

void FAsdaSkillJob4PointsSelfTest::Restore()
{
	if (Net)
	{
		AsdaSelfTestKit::DisarmCapture(Net, CapGen);
		Net->SelfLevel = Saved.Level; Net->SelfJob = Saved.Job; Net->SelfClassId = Saved.Cls; Net->SelfSubJob = Saved.Sub; Net->SelfSkillPoints = Saved.Pts;
	}
	CapGen = 0;
	if (SW)
	{
		SW->DrainPending();
		if (SW->IsWindowOpen()) { SW->CloseWindow(); }
		SW->Learned = Saved.Learned; SW->SkillPoints = Saved.WinPoints; SW->CurTree = Saved.Tree; SW->ScrollRow = Saved.Scroll;
		SW->SelectedSlot = Saved.Selected; SW->JobOverride = Saved.JobOv; SW->SubJobOverride = Saved.SubOv;
		if (Saved.bWinOpen) { SW->OpenWindow(); }
	}
	if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест очков 4-й профессии: конец")); }
	if (GLog) { GLog->Flush(); GLog->RemoveOutputDevice(&Sink); }
}

// Запуск прогона целиком (синхронно: проверки сразу после пакета, без тиков между ними).
static void AsdaRunSkillJob4PointsSelfTest(UWorld* W, bool bQuit)
{
	FAsdaSkillJob4PointsSelfTest T;
	T.W = W;
	T.Run();
	T.Restore();
	AsdaSelfTestKit::Finish(W, AsdaJob4PointsTestLocal::kCmd, bQuit, LogAsdaJob4PointsTest, TEXT("ОЧКИ-4-ПРОФ"), T.Ok, T.Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillJob4Points(
	TEXT("asda.SelfTest.SkillJob4Points"),
	TEXT("Самотест двух пулов очков навыков (основные деревья 110 / дерево 4-й профессии): счётчики 1092/12413, подсветка, клик узла → "
	     "5253 или отказ 2202/111701, ответы 5254 кодов 3/8, сбросы 5464/5462, уровень 59, вопрос 9415, ключ asda.Skill.Job4Pools 0. "
	     "Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaJob4PointsTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { AsdaRunSkillJob4PointsSelfTest(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaJob4PointsTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { AsdaRunSkillJob4PointsSelfTest(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
