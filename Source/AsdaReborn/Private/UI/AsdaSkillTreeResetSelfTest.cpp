// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ СВИТКОВ СБРОСА НАВЫКОВ (25.10.2026): `asda.SelfTest.SkillTreeReset [quit]`. [ПОРТ]
//   Путь игрока, без сети (-AsdaNoNet), карта мира. Свиток кладётся в сумку боевой точкой (`ApplyExternalRecord`), применяется
//   двойным щелчком (`UseByDoubleClick`), кнопки окна 16500 и вопроса TWOBUT нажимаются рассылкой `OnClicked` самих кнопок,
//   исходящие ловятся перехватом отправок (`AsdaSelfTestKit::ArmCapture`). Ожидания — по эталону (шапка подсистемы).
//     A. Новичок (профессия 0): окна нет, 5450 нет (нотис 12848).
//     B. 197: окно 16500, тексты 12861/12863/12864, ветки 9400…9402 (полоса 1), ветка 1; пункт 2 → ветка 2; «ОК» → вопрос
//        (1, 12865) с текстом 12865; «Да» → 5450 кода 197 из той же ячейки, ветка 2 в теле +135 и +255; ответ 5464 с веткой 2 →
//        сообщение 12854 с названием ветки 9401.
//     C. «Отмена» окна — 5450 нет, свиток на месте; D. «Отмена» вопроса 12865 — 5450 нет.
//     E. Класс 4-й профессии (4): флажок 16521 виден; щелчок → ветка 10, пункт снят; повторный щелчок — флажок остаётся;
//        пункт 0 → флажок снят, ветка 1. Контроль: у класса 2 флажка нет.
//     F. 196: окна нет, вопрос (1, 12866); «Да» → 5450 кода 196 без ветки (тело +135 = 0).
//     I. ★ 26.10.2026: 4-я профессия в подклассе (14/15): флажок, ветка 11, имя подкласса; ключ asda.Job4.StageFromSubJob 0.
//     J. ★ 26.10.2026: ремесленный класс 34 — свиток ветки отклонён нотисом 9421; ремесленный подкласс — без флажка; ключ 0.
//     H. ★ 26.10.2026: второй свиток (196) при открытом окне 16500 запрос окна не подменяет (asda.SkillReset.KeepFirstRequest).
//     G. Ключ asda.SkillReset.Dialogs 0 — прежнее поведение: 5450 сразу, окна нет.
//   Итог — «СБРОС-НАВЫКОВ ИТОГ: ok=… fail=…». Язык текстов — язык прогона (RU/EN), ожидания берутся из той же таблицы.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaSkillTreeResetSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkillResetTest, Log, All);

namespace AsdaSkillResetTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SkillTreeReset");

	// Перехватчик сообщений окна навыков («[навыки] msgbox: …»).
	class FSink : public FOutputDevice
	{
	public:
		TArray<FString> Msg;
		TArray<FString> Win;   // ★ 26.10.2026: строки «[сброс навыков] окно 16500: …» (класс, ступень, имя класса)
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			static const FName kReset(TEXT("LogAsdaSkillReset"));
			if (V && Category == kReset && FCString::Strstr(V, TEXT("окно 16500: свиток"))) { Win.Add(FString(V)); }
			static const FName kSkill(TEXT("LogAsdaSkillWin"));
			if (V && Category == kSkill)
			{
				const FString S(V);
				if (S.StartsWith(TEXT("[навыки] msgbox: "))) { Msg.Add(S.Mid(17)); }
			}
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
	};
	// Перехваченная отправка 5450.
	struct FSent { int32 Code = 0; int32 Pos = -1; int32 V135 = -1; int32 V255 = -1; };
}

struct FAsdaSkillTreeResetSelfTest
{
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		int32 Cell = -1;                                   // ячейка сумки для свитка
		uint32 CapGen = 0;                                 // поколение перехвата отправок
		TArray<AsdaSkillResetTestLocal::FSent> Sent;       // перехваченные 5450
		AsdaSkillResetTestLocal::FSink Sink;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TUniquePtr<FAsdaCVarScope> Keys;
		int32 JobWas = -1, ClassWas = -1, SubWas = -1;   // поля профессии/класса/подкласса до прогона
		~FRun() { if (GLog) { GLog->RemoveOutputDevice(&Sink); } }
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSkillResetTest, Log, TEXT("СБРОС-НАВЫКОВ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSkillResetTest, Error, TEXT("СБРОС-НАВЫКОВ FAIL %s"), *What); }
		}
	};
	using FRef = TSharedRef<FRun>;

	template<class T> static T* Sub(const FRef& R)
	{
		UWorld* W = R->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<T>() : nullptr;
	}
	static UGameInstance* GIOf(const FRef& R) { return R->W.IsValid() ? R->W->GetGameInstance() : nullptr; }
	static FString L(const FRef& R, int32 Id) { return AsdaNotice::Loc(GIOf(R), Id); }

	// Шаг по таймеру мира.
	static void After(const FRef& R, float Sec, TFunction<void(const FRef&)> Fn)
	{
		UWorld* W = R->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
	}
	// Положить свиток в ячейку (боевой точкой приёмников).
	static void Put(const FRef& R, int32 Code)
	{
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R))
		{
			Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, 1, R->Cell, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		}
	}
	static int32 CellCode(const FRef& R)
	{
		const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		const FAsdaItemInstance* C = Inv ? Inv->GetCell(1, R->Cell) : nullptr;
		return (C && !C->IsEmpty()) ? C->ItemCode : 0;
	}
	// Щелчок по кнопке окна 16500 / вопроса TWOBUT (рассылкой `OnClicked` — тот же путь, что щелчок игрока).
	static bool Click(UUserWidget* Host, const TCHAR* Name)
	{
		UButton* B = Host ? Cast<UButton>(Host->GetWidgetFromName(FName(Name))) : nullptr;
		if (!B) { return false; }
		B->OnClicked.Broadcast();
		return true;
	}
	static UUserWidget* TwoBut(const FRef& R)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
		return WM ? WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX"))) : nullptr;
	}
	static FString Text(UUserWidget* Host, const TCHAR* Name)
	{
		const UTextBlock* T = Host ? Cast<UTextBlock>(Host->GetWidgetFromName(FName(Name))) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет>"));
	}
	// Видимый текст вопроса TWOBUT (узел после показа — URichTextBlock; общая точка оконной подсистемы).
	static FString QText(const FRef& R)
	{
		UUserWidget* Q = TwoBut(R);
		return Q ? UAsdaWindowSubsystem::ReadMsgBoxShownText(Q->GetWidgetFromName(FName(TEXT("TWOBUT_TEXT")))) : FString(TEXT("<нет>"));
	}
	// Щелчок игрока по флажку 16521: Slate меняет состояние и рассылает OnCheckStateChanged.
	static bool ClickCheck(UUserWidget* Host)
	{
		UCheckBox* C = Host ? Cast<UCheckBox>(Host->GetWidgetFromName(FName(TEXT("Skill_OneTree_Reset_Win_CHK00")))) : nullptr;
		if (!C) { return false; }
		const bool bNew = !C->IsChecked();
		C->SetIsChecked(bNew);
		C->OnCheckStateChanged.Broadcast(bNew);
		return true;
	}
	static bool Use(const FRef& R)
	{
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		return Inv && Inv->UseByDoubleClick(1, R->Cell);
	}

	static void Start(UWorld* W, bool bQuit);
	static void Finish(const FRef& R);
};

void FAsdaSkillTreeResetSelfTest::Start(UWorld* W, bool bQuit)
{
	using namespace AsdaSkillResetTestLocal;
	FRef R = MakeShared<FRun>();
	R->W = W; R->bQuit = bQuit;
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	UAsdaSkillTreeResetSubsystem* Tr = Sub<UAsdaSkillTreeResetSubsystem>(R);
	if (!Net || !Inv || !Tr) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("нет подсистемы сети/сумки/сброса навыков")); return; }
	for (int32 i = 29; i >= 0 && R->Cell < 0; --i)
	{
		const FAsdaItemInstance* C = Inv->GetCell(1, i);
		if (!C || C->IsEmpty()) { R->Cell = i; }
	}
	if (R->Cell < 0) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("в сумке нет свободной ячейки")); return; }
	GLog->AddOutputDevice(&R->Sink);
	R->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
	R->Keys->Set(TEXT("asda.SkillReset.Dialogs"), 1);
	R->Keys->Set(TEXT("asda.Skill.RealignedTreeSlot"), 1);
	R->Keys->Set(TEXT("asda.Job4.StageFromSubJob"), 1);
	R->Keys->Set(TEXT("asda.SkillReset.CraftClassGuard"), 1);
	R->JobWas = Net->SelfJob; R->ClassWas = Net->SelfClassId; R->SubWas = Net->SelfSubJob;
	Net->SelfUserNum = 1; Net->bSelfUserNumKnown = true;   // свой номер — адресат 5450
	Net->SelfJob = 1; Net->SelfClassId = 2; Net->SelfSubJob = 0;   // профессия есть, класс полосы 1 без 4-й профессии, подкласса нет
	TWeakPtr<FRun> WR = R;
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		TSharedPtr<FRun> Run = WR.Pin();
		if (!Run || Op != 5450 || !P || Len < 257) { return; }
		FSent S;
		S.Code = AsdaBytes::RdI32(P + 2); S.Pos = AsdaBytes::RdI16(P + 6);
		S.V135 = AsdaBytes::RdI16(P + 135); S.V255 = AsdaBytes::RdI16(P + 255);
		Run->Sent.Add(S);
	}, 0, kCmd);
	if (R->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("перехват отправок занят")); return; }

	// A. Новичок: окна нет, 5450 нет.
	Put(R, 197);
	Net->SelfJob = 0;
	Use(R);
	R->Expect(!Tr->IsWindowShown() && R->Sent.Num() == 0, FString::Printf(TEXT("A профессии нет (новичок): окна 16500 нет, 5450 нет (отправок %d)"), R->Sent.Num()));
	Net->SelfJob = 1;

	// B. Окно и выбор ветки.
	Use(R);
	UUserWidget* Win = Tr->GetWidget();
	R->Expect(Tr->IsWindowShown() && Win, TEXT("B1 197 (подтип 36): окно 16500 показано, 5450 не ушёл"));
	R->Expect(R->Sent.Num() == 0, FString::Printf(TEXT("B2 до ответа на вопрос 5450 нет (отправок %d)"), R->Sent.Num()));
	R->Expect(Text(Win, TEXT("Skill_Reset_Title")) == L(R, 12861) && Text(Win, TEXT("Skill_Reset_OkBtn_Text")) == L(R, 12863)
		&& Text(Win, TEXT("Skill_Reset_CanBtn_Text")) == L(R, 12864),
		FString::Printf(TEXT("B3 подписи: заголовок «%s», «%s»/«%s»"), *Text(Win, TEXT("Skill_Reset_Title")),
			*Text(Win, TEXT("Skill_Reset_OkBtn_Text")), *Text(Win, TEXT("Skill_Reset_CanBtn_Text"))));
	R->Expect(Text(Win, TEXT("Skill_Reset_Tree1")) == L(R, 9400) && Text(Win, TEXT("Skill_Reset_Tree2")) == L(R, 9401)
		&& Text(Win, TEXT("Skill_Reset_Tree3")) == L(R, 9402),
		FString::Printf(TEXT("B4 ветки полосы 1: «%s» / «%s» / «%s»"), *Text(Win, TEXT("Skill_Reset_Tree1")),
			*Text(Win, TEXT("Skill_Reset_Tree2")), *Text(Win, TEXT("Skill_Reset_Tree3"))));
	R->Expect(Tr->GetGrade() == 1 && Tr->GetSelectedTree() == 0 && !Tr->IsFourthShown(),
		FString::Printf(TEXT("B5 начальное: пункт 0, ветка 1, флажка 4-й профессии нет (%s)"), *Tr->DumpState()));
	R->Expect(!Text(Win, TEXT("Skill_Reset_Txt00")).IsEmpty() && Text(Win, TEXT("Skill_Reset_Txt00")) != TEXT("<нет>"),
		FString::Printf(TEXT("B6 текст 16502 (12862): «%s»"), *Text(Win, TEXT("Skill_Reset_Txt00"))));
	R->Expect(Click(Win, TEXT("Skill_Reset_SelBtn_OPT1")) && Tr->GetGrade() == 2 && Tr->GetSelectedTree() == 1,
		FString::Printf(TEXT("B7 щелчок по пункту 2 (16510): ветка 2 (%s)"), *Tr->DumpState()));
	R->Expect(Click(Win, TEXT("Skill_Reset_OkBtn")) && !Tr->IsWindowShown(), TEXT("B8 «ОК» 16503: окно скрыто"));
	After(R, 0.3f, [](const FRef& R)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
		UUserWidget* Q = TwoBut(R);
		R->Expect(WM && WM->IsMsgBoxDisplayed(1, 12865) && QText(R).Contains(L(R, 12865).Left(8)),
			FString::Printf(TEXT("B9 вопрос (1, 12865): «%s»"), *QText(R)));
		R->Expect(R->Sent.Num() == 0, TEXT("B10 вопрос показан — 5450 ещё не ушёл"));
		Click(Q, TEXT("TWOBUT_BUT00"));
		After(R, 0.3f, [](const FRef& R)
		{
			const bool b = R->Sent.Num() == 1 && R->Sent[0].Code == 197 && R->Sent[0].Pos == R->Cell && R->Sent[0].V135 == 2 && R->Sent[0].V255 == 2;
			R->Expect(b, FString::Printf(TEXT("B11 «Да»: 5450 код %d ячейка %d, ветка +135=%d +255=%d (ожидание 197/%d/2/2, отправок %d)"),
				R->Sent.Num() ? R->Sent[0].Code : 0, R->Sent.Num() ? R->Sent[0].Pos : -1, R->Sent.Num() ? R->Sent[0].V135 : -1,
				R->Sent.Num() ? R->Sent[0].V255 : -1, R->Cell, R->Sent.Num()));
			// Ответ сервера 5464 {очки 2, ветка 2} → 12854 с названием ветки 9401.
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
			GLog->Flush(); R->Sink.Msg.Reset();
			TArray<uint8> B; B.SetNumZeroed(3); B[0] = 2; B[2] = 2;
			if (Net) { Net->DebugFeedPacket(5464, B); }
			GLog->Flush();
			const FString M = R->Sink.Msg.Num() ? R->Sink.Msg.Last() : FString();
			const FString Tree = L(R, 9401);
			R->Expect(!Tree.IsEmpty() && M.Contains(FString::Printf(TEXT("[%s]"), *Tree)),
				FString::Printf(TEXT("B12 ответ 5464 (ветка 2): «%s»"), *M));
			if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R)) { WM->ClearMsgBoxQueue(TEXT("самотест SkillTreeReset")); }

			// C. «Отмена» окна.
			UAsdaSkillTreeResetSubsystem* Tr = Sub<UAsdaSkillTreeResetSubsystem>(R);
			Use(R);
			UUserWidget* Win = Tr ? Tr->GetWidget() : nullptr;
			R->Expect(Tr && Tr->IsWindowShown() && Click(Win, TEXT("Skill_Reset_CanBtn")) && !Tr->IsWindowShown(),
				TEXT("C1 «Отмена» 16504: окно закрыто"));
			After(R, 0.3f, [](const FRef& R)
			{
				UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
				R->Expect(R->Sent.Num() == 1 && CellCode(R) == 197 && !(WM && WM->IsMsgBoxDisplayed(1, 12865)),
					FString::Printf(TEXT("C2 после отмены: вопроса нет, 5450 нет, свиток на месте (отправок %d, ячейка %d)"), R->Sent.Num(), CellCode(R)));
				// D. «Отмена» вопроса 12865.
				UAsdaSkillTreeResetSubsystem* Tr = Sub<UAsdaSkillTreeResetSubsystem>(R);
				Use(R);
				Click(Tr ? Tr->GetWidget() : nullptr, TEXT("Skill_Reset_OkBtn"));
				After(R, 0.3f, [](const FRef& R)
				{
					R->Expect(Click(TwoBut(R), TEXT("TWOBUT_BUT01")), TEXT("D1 вопрос 12865: «Отмена» нажата"));
					After(R, 0.3f, [](const FRef& R)
					{
						R->Expect(R->Sent.Num() == 1 && CellCode(R) == 197,
							FString::Printf(TEXT("D2 «Отмена» вопроса: 5450 нет, свиток цел (отправок %d)"), R->Sent.Num()));
						// E. Флажок 4-й профессии.
						UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
						UAsdaSkillTreeResetSubsystem* Tr = Sub<UAsdaSkillTreeResetSubsystem>(R);
						if (!Net || !Tr) { Finish(R); return; }
						Net->SelfClassId = 4;   // класс 4-й профессии полосы 1 → ветка флажка 10
						Use(R);
						UUserWidget* Win = Tr->GetWidget();
						R->Expect(Tr->IsWindowShown() && Tr->IsFourthShown() && !Tr->IsFourthChecked(),
							FString::Printf(TEXT("E1 класс 4: флажок 16521 виден и снят (%s)"), *Tr->DumpState()));
						R->Expect(ClickCheck(Win), TEXT("E2а флажок 16521 (UCheckBox) найден, щелчок"));
						R->Expect(Tr->IsFourthChecked() && Tr->GetGrade() == 10 && Tr->GetSelectedTree() == -1,
							FString::Printf(TEXT("E2 щелчок по флажку: ветка 10, пункт снят (%s)"), *Tr->DumpState()));
						ClickCheck(Win);
						const UCheckBox* Chk = Win ? Cast<UCheckBox>(Win->GetWidgetFromName(FName(TEXT("Skill_OneTree_Reset_Win_CHK00")))) : nullptr;
						R->Expect(Tr->IsFourthChecked() && Tr->GetGrade() == 10 && Chk && Chk->IsChecked(),
							FString::Printf(TEXT("E3 повторный щелчок: флажок остаётся (эталон 67074), ветка 10 (%s)"), *Tr->DumpState()));
						Click(Win, TEXT("Skill_Reset_SelBtn_OPT0"));
						R->Expect(!Tr->IsFourthChecked() && Tr->GetGrade() == 1 && Tr->GetSelectedTree() == 0,
							FString::Printf(TEXT("E4 пункт 1: флажок снят, ветка 1 (%s)"), *Tr->DumpState()));
						Click(Win, TEXT("Skill_Reset_CanBtn"));
						Net->SelfClassId = 2;
						Use(R);
						R->Expect(Tr->IsWindowShown() && !Tr->IsFourthShown(), TEXT("E5 контроль: класс 2 — флажка нет"));
						Click(Tr->GetWidget(), TEXT("Skill_Reset_CanBtn"));
						// ★ 26.10.2026 I. 4-я профессия в ПОДКЛАССЕ (сервер `sub_461010`: класс 14 остаётся, подкласс 15) — флажок виден,
						//   ветка 11, в тексте 16502 — имя подкласса (loc 12315); ключ asda.Job4.StageFromSubJob 0 — как эталон: флажка нет.
						Net->SelfClassId = 14; Net->SelfSubJob = 15;
						Use(R);
						Win = Tr->GetWidget();
						R->Expect(Tr->IsWindowShown() && Tr->IsFourthShown(),
							FString::Printf(TEXT("I1 класс 14 + подкласс 15: флажок 16521 виден (%s)"), *Tr->DumpState()));
						GLog->Flush();
						const FString WinLine = R->Sink.Win.Num() ? R->Sink.Win.Last() : FString();
						R->Expect(WinLine.Contains(FString::Printf(TEXT("имя класса «%s»"), *L(R, 12315))) && WinLine.Contains(TEXT("ступень 4-й профессии 15")),
							FString::Printf(TEXT("I2 имя класса для слота [ss2] 16502 — подкласс «%s» (loc 12315), ступень 15: «%s»"), *L(R, 12315), *WinLine));
						ClickCheck(Win);
						R->Expect(Tr->IsFourthChecked() && Tr->GetGrade() == 11,
							FString::Printf(TEXT("I3 щелчок по флажку: ветка 11 (%s)"), *Tr->DumpState()));
						Click(Win, TEXT("Skill_Reset_CanBtn"));
						R->Keys->Set(TEXT("asda.Job4.StageFromSubJob"), 0);
						Use(R);
						R->Expect(Tr->IsWindowShown() && !Tr->IsFourthShown(), TEXT("I4 ключ asda.Job4.StageFromSubJob 0 — как эталон: флажка нет"));
						Click(Tr->GetWidget(), TEXT("Skill_Reset_CanBtn"));
						R->Keys->Set(TEXT("asda.Job4.StageFromSubJob"), 1);
						// ★ 26.10.2026 J. Ремесленный класс 34 (полоса 0): свиток ветки отклоняется нотисом 9421 до отправки, свиток цел;
						//   ремесленный ПОДКЛАСС у боевого класса — окно есть, флажка нет; ключ asda.SkillReset.CraftClassGuard 0 — как эталон.
						Net->SelfClassId = 34; Net->SelfSubJob = 0;
						const int32 SentBefore = R->Sent.Num();
						Use(R);
						UAsdaWindowSubsystem* WMj = Sub<UAsdaWindowSubsystem>(R);
						R->Expect(!Tr->IsWindowShown() && R->Sent.Num() == SentBefore && CellCode(R) == 197 && WMj && WMj->IsMsgBoxDisplayed(0, 9421),
							FString::Printf(TEXT("J1 класс 34: окна нет, 5450 нет, свиток цел, окно-сообщение (0, 9421) (отправок %d, ячейка %d)"),
								R->Sent.Num(), CellCode(R)));
						if (WMj) { WMj->ClearMsgBoxQueue(TEXT("самотест SkillTreeReset J")); }
						Net->SelfClassId = 3; Net->SelfSubJob = 34;
						Use(R);
						R->Expect(Tr->IsWindowShown() && !Tr->IsFourthShown(), FString::Printf(TEXT("J2 класс 3 + ремесленный подкласс 34: окно есть, флажка нет (%s)"), *Tr->DumpState()));
						Click(Tr->GetWidget(), TEXT("Skill_Reset_CanBtn"));
						R->Keys->Set(TEXT("asda.SkillReset.CraftClassGuard"), 0);
						Net->SelfClassId = 34; Net->SelfSubJob = 0;
						Use(R);
						R->Expect(Tr->IsWindowShown() && Tr->IsFourthShown(), TEXT("J3 ключ asda.SkillReset.CraftClassGuard 0 — как эталон: окно 16500 и флажок у класса 34"));
						Click(Tr->GetWidget(), TEXT("Skill_Reset_CanBtn"));
						R->Keys->Set(TEXT("asda.SkillReset.CraftClassGuard"), 1);
						Net->SelfClassId = 2; Net->SelfSubJob = 0;
						Use(R);
						// H. Второй свиток при открытом окне — запрос окна прежний (197), вопроса 12866 нет.
						Put(R, 196);
						Use(R);
						R->Expect(Tr->IsWindowShown() && Tr->DumpState().Contains(TEXT("свиток=197")),
							FString::Printf(TEXT("H второй свиток 196 при открытом окне 16500: запрос не подменён (%s)"), *Tr->DumpState()));
						Put(R, 197);
						Click(Tr->GetWidget(), TEXT("Skill_Reset_CanBtn"));
						// F. 196 — вопрос 12866 без окна.
						Put(R, 196);
						Use(R);
						After(R, 0.3f, [](const FRef& R)
						{
							UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
							UAsdaSkillTreeResetSubsystem* Tr = Sub<UAsdaSkillTreeResetSubsystem>(R);
							R->Expect(WM && WM->IsMsgBoxDisplayed(1, 12866) && Tr && !Tr->IsWindowShown() && R->Sent.Num() == 1,
								FString::Printf(TEXT("F1 196 (подтип 35): вопрос (1, 12866), окна ветки нет, 5450 ещё нет («%s»)"), *QText(R)));
							Click(TwoBut(R), TEXT("TWOBUT_BUT00"));
							After(R, 0.3f, [](const FRef& R)
							{
								const bool b = R->Sent.Num() == 2 && R->Sent[1].Code == 196 && R->Sent[1].V135 == 0 && R->Sent[1].V255 == 0;
								R->Expect(b, FString::Printf(TEXT("F2 «Да»: 5450 код %d, ветки нет (+135=%d +255=%d)"),
									R->Sent.Num() > 1 ? R->Sent[1].Code : 0, R->Sent.Num() > 1 ? R->Sent[1].V135 : -1, R->Sent.Num() > 1 ? R->Sent[1].V255 : -1));
								// G. Ключ 0 — прежнее поведение.
								R->Keys->Set(TEXT("asda.SkillReset.Dialogs"), 0);
								Put(R, 197);
								Use(R);
								UAsdaSkillTreeResetSubsystem* Tr = Sub<UAsdaSkillTreeResetSubsystem>(R);
								R->Expect(R->Sent.Num() == 3 && R->Sent[2].Code == 197 && R->Sent[2].V135 == 0 && Tr && !Tr->IsWindowShown(),
									FString::Printf(TEXT("G ключ asda.SkillReset.Dialogs 0: 5450 сразу, без окна и ветки (отправок %d)"), R->Sent.Num()));
								Finish(R);
							});
						});
					});
				});
			});
		});
	});
}

void FAsdaSkillTreeResetSelfTest::Finish(const FRef& R)
{
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (Net)
	{
		AsdaSelfTestKit::DisarmCapture(Net, R->CapGen);
		Net->SelfJob = R->JobWas; Net->SelfClassId = R->ClassWas; Net->SelfSubJob = R->SubWas;
	}
	if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R))
	{
		Inv->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, R->Cell), UAsdaInventorySubsystem::EExternalRecordMode::Default);
	}
	if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R)) { WM->ClearMsgBoxQueue(TEXT("самотест SkillTreeReset")); }
	if (UAsdaSkillTreeResetSubsystem* Tr = Sub<UAsdaSkillTreeResetSubsystem>(R)) { if (Tr->IsWindowShown()) { Tr->OnCancelClicked(); } }
	if (R->SelfScope) { R->SelfScope->Restore(); }
	if (R->Keys) { R->Keys->Restore(); }
	GLog->Flush();
	GLog->RemoveOutputDevice(&R->Sink);
	AsdaSelfTestKit::Finish(R->W.Get(), AsdaSkillResetTestLocal::kCmd, R->bQuit, LogAsdaSkillResetTest, TEXT("СБРОС-НАВЫКОВ"), R->Ok, R->Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillTreeReset(
	TEXT("asda.SelfTest.SkillTreeReset"),
	TEXT("Самотест свитков сброса навыков путём игрока: окно выбора ветки 16500 (197), вопросы 12865/12866, 5450 с веткой, отмена "
	     "не тратит свиток, флажок 4-й профессии, гейт новичка, ключ 0. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaSkillResetTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaSkillTreeResetSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaSkillResetTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaSkillTreeResetSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
