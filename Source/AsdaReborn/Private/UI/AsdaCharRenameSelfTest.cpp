// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCharRenameSelfTest.cpp — самотест предмета «Смена имени персонажа» (357, подтип 111), пачка 20 баг 8 (09.11.2026).
//   Разбор — Public/UI/AsdaCharRenameSubsystem.h и Reports/batch20/rename/REPORT.md.
//
// СЦЕНАРИЙ `asda.SelfTest.Rename [out=<папка>] [quit]` (офлайн, -AsdaNoNet; RU и EN — прогоном с --lang ru/en).
//   Путь игрока — двойной щелчок по предмету в ячейке сумки (`UseByDoubleClick`). Перехват отправок с отказом; ответы
//   сервера 6139/6141 — через тот же делегат `OnWave2DPacket`, что заполняет диспетчер пакетов.
//   A  применение: вопрос (1, 65) с текстом 109401, 5450 не ушёл; «Отмена» — окна 7900 нет;
//   B  «ОК» 65 → окно 7900 видно, поле пусто;
//   C  своё имя → окно 101118; недопустимые имена (пробел/знак, кириллица, апостроф, пусто, 19 символов) → 9583, 6138 нет;
//   D  годное имя → 6138 (24 б: charId, имя);
//   E  6139 код 2 / 3 (занято / запрещённое слово сервера) → 109404;
//   F  6139 код 1 → вопрос 66 (109403 с именем); «Отмена» — окно 7900 осталось, 6140 нет;
//   G  66 «ОК» → вопрос 67 (109405 со старым и новым именем); «Отмена» — 6140 нет;
//   H  67 «ОК» → окно скрыто, 6140 (27 б: charId, имя, ячейка, контейнер 1);
//   I  6141 коды 0/2/3/4/5/6/7 → 109411/109406/109406/109407/109408/109409/109410;
//   J  6141 код 1 → ячейка предмета пуста, уход к выбору персонажа (подавлен самотестом, счётчик);
//   K  кнопка 7902 закрывает окно; L  ключ asda.Rename.Flow 0 → прежний 5450.
//   Кадры: rename_<язык>_q65 / _window / _badname / _taken / _q66 / _q67 / _clan.png. Итог — `СМЕНА-ИМЕНИ ИТОГ: ok=N fail=M`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда вне Shipping
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Net/NetOpcodes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Login/AsdaLoginData.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaCharRenameSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaWorldSubsystem.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRenameTest, Log, All);

namespace AsdaRenameTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.Rename");
	static const TCHAR* kPfx = TEXT("СМЕНА-ИМЕНИ");
	constexpr int32 kItem = 357;
	constexpr int32 kCharId = 4242;

	struct FSent { uint16 Op = 0; TArray<uint8> Body; };

	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;
		TWeakObjectPtr<UWorld> World;
		bool bQuit = false;
		FString OutDir, Lang;
		int32 Ok = 0, Fail = 0;
		int32 Cell = -1;
		uint32 Gen = 0;
		FString OldName;
		const FString NewName = TEXT("Renamed7");
		TSharedRef<TArray<FSent>> Sent = MakeShared<TArray<FSent>>();
		TUniquePtr<FAsdaNetSelfScope> Self;
		TArray<TFunction<void()>> Steps;
		int32 StepI = 0;
		TArray<FString> Shots;

		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		void Check(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaRenameTest, Display, TEXT("%s %s %s"), kPfx, b ? TEXT("ok  ") : TEXT("FAIL"), *What);
		}
		FString L(int32 Id) const { return Norm(AsdaNotice::Loc(GI.Get(), Id)); }
		UAsdaCharRenameSubsystem* Rn() const { return Sub<UAsdaCharRenameSubsystem>(); }
		UAsdaWindowSubsystem* WM() const { return Sub<UAsdaWindowSubsystem>(); }
		int32 CountOp(uint16 Op) const { int32 N = 0; for (const FSent& S : *Sent) { N += (S.Op == Op); } return N; }
		const FSent* LastOp(uint16 Op) const { for (int32 i = Sent->Num() - 1; i >= 0; --i) { if ((*Sent)[i].Op == Op) { return &(*Sent)[i]; } } return nullptr; }
		int32 LastNotice() const { const UAsdaCharRenameSubsystem* R = Rn(); return (R && R->GetNoticeTrace().Num()) ? R->GetNoticeTrace().Last() : -1; }
		FString TopText() const { const UAsdaWindowSubsystem* W = WM(); return Norm(W ? W->GetMsgBoxQueueTopText() : FString()); }
		// Перенос строки: в loc он литералом «\n», в окне — настоящий; сравниваем без разницы.
		static FString Norm(FString S) { S.ReplaceInline(TEXT("\\n"), TEXT("\n")); S.ReplaceInline(TEXT("\r"), TEXT("")); return S; }
		void Shot(const FString& Tag)
		{
			const FString N = FString::Printf(TEXT("rename_%s_%s"), *Lang, *Tag);
			FScreenshotRequest::RequestScreenshot(N, /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
			Shots.Add(N);
		}
		// Ответ верхнему окну-сообщению.
		void Answer(bool bTwoBut, bool bYes) { if (UAsdaWindowSubsystem* W = WM()) { W->AnswerMsgBox(bTwoBut, bYes); } }
		// Снять все окна-сообщения (между шагами).
		void ClearBoxes() { if (UAsdaWindowSubsystem* W = WM()) { W->ClearMsgBoxQueue(TEXT("самотест смены имени")); } }
		// Положить предмет и применить двойным щелчком.
		void UseItem()
		{
			UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>();
			Inv->ClearCell(1, Cell);
			Inv->DebugPutItem(1, Cell, kItem, 1);
			Inv->UseByDoubleClick(1, Cell);
		}
		// Ответ сервера через делегат диспетчера.
		void Inject(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* N = Sub<UAsdaNetSubsystem>()) { N->OnWave2DPacket.Broadcast(Op, B); } }
		void Inject6139(uint8 Code)
		{
			TArray<uint8> B; B.SetNumZeroed(5);
			AsdaBytes::WrI32(B.GetData(), kCharId); B[4] = Code;
			Inject(OP_ITEM_TOOLTIP_BUILD_OR_NOTICE, B);
		}
		void Inject6141(uint8 Code)
		{
			TArray<uint8> B; B.SetNumZeroed(86);
			AsdaBytes::WrI32(B.GetData(), kCharId);
			FAsdaLoginData::EncodeCp1251(NewName, B.GetData() + 4, 20);
			B[24] = Code;
			if (Code == 1)
			{
				uint8* Rec = B.GetData() + 25;
				AsdaBytes::WrI32(Rec, kItem); Rec[4] = 1; AsdaBytes::WrI16(Rec + 5, (int16)Cell); AsdaBytes::WrI32(Rec + 9, 1);
			}
			Inject(OP_RECV_ITEM_ACTION_RESULT_SHOW_NOTICE, B);
		}
		// Ввести имя и нажать «Доступность».
		void TypeAndCheck(const FString& S) { if (UAsdaCharRenameSubsystem* R = Rn()) { R->SetInputText(S); R->ClickCheck(); } }
	};
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestRename(
	TEXT("asda.SelfTest.Rename"),
	TEXT("Самотест предмета смены имени (357, подтип 111): вопрос 65, окно 7900, 6138/6139, вопросы 66/67, 6140/6141, все исходы. [out=<папка>] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaRenameTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 60.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = GI; R->World = W; R->bQuit = KA.bQuit;
		R->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		for (const FString& A : KA.Pos) { if (A.StartsWith(TEXT("out="))) { R->OutDir = A.Mid(4); } }
		UAsdaInventorySubsystem* Inv = R->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = R->Sub<UAsdaNetSubsystem>();
		UAsdaWorldSubsystem* WS = R->Sub<UAsdaWorldSubsystem>();
		const UAsdaItemTableSubsystem* IT = R->Sub<UAsdaItemTableSubsystem>();
		if (!Inv || !Net || !WS || !IT || !R->Rn() || !R->WM()) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.Rename.Flow"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Rename.NameRule"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Text.NameLocale"), 0);
		R->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kCharId;
		if (WS->GetSelfCharName().IsEmpty()) { WS->DebugSpawnSelfAt(256, 256, TEXT("Tester1")); }
		R->OldName = WS->GetSelfCharName();
		R->Rn()->SetSuppressLeave(true);
		R->Rn()->ClearTrace();
		TSharedRef<TArray<FSent>> Sent = R->Sent;
		R->Gen = AsdaSelfTestKit::ArmCapture(Net, [Sent](uint16 Op, const uint8* D, int32 Len, bool, TCHAR)
		{
			FSent S; S.Op = Op; if (D && Len > 0) { S.Body.Append(D, Len); } Sent->Add(MoveTemp(S));
		}, 0, kCmd);
		R->Check(R->Gen != 0, TEXT("перехват отправок взведён"));
		FAsdaSpecialItemlistRow Sp;
		const bool bSp = IT->GetSpecialItem(kItem, Sp);
		R->Check(bSp && Sp.SubType == 111, FString::Printf(TEXT("357 в special_itemlist, подтип %d"), Sp.SubType));
		Inv->OpenWindow();
		R->Cell = Inv->FindFreeSlot(1);
		R->Check(R->Cell >= 0, FString::Printf(TEXT("свободная ячейка сумки %d, своё имя «%s», язык %s"), R->Cell, *R->OldName, *R->Lang));
		R->ClearBoxes();

		TArray<TFunction<void()>>& S = R->Steps;
		FRun* X = &R.Get();
		// A
		S.Add([X]() { X->UseItem(); });
		S.Add([X]()
		{
			X->Check(X->WM()->IsMsgBoxTop(1, 65) && X->TopText() == X->L(AsdaRenameLoc::kAskUse),
				FString::Printf(TEXT("A вопрос (1, 65): «%s»"), *X->TopText().Left(60)));
			X->Check(X->CountOp(5450) == 0, TEXT("A 5450 не ушёл"));
			X->Shot(TEXT("q65"));
		});
		S.Add([X]() { X->Answer(true, false); });
		S.Add([X]()
		{
			X->Check(!X->Rn()->IsWindowShown() && X->WM()->GetMsgBoxQueueNum() == 0, TEXT("A «Отмена» 65 — окна 7900 нет, очередь пуста"));
			X->UseItem();
		});
		S.Add([X]() { X->Answer(true, true); });
		// B
		S.Add([X]()
		{
			X->Check(X->Rn()->IsWindowShown(), TEXT("B «ОК» 65 → окно 7900 видно"));
			X->Check(X->Rn()->GetInputText().IsEmpty(), TEXT("B поле 7904 пусто"));
			X->Check(X->Rn()->GetCheckButton() && X->Rn()->GetCloseButton(), TEXT("B кнопки 7905/7902 найдены"));
			X->Rn()->SetInputText(X->NewName);
			X->Shot(TEXT("window"));
		});
		// C
		S.Add([X]()
		{
			X->TypeAndCheck(X->OldName);
			X->Check(X->LastNotice() == AsdaRenameLoc::kSameName && X->TopText() == X->L(AsdaRenameLoc::kSameName),
				FString::Printf(TEXT("C своё имя → 101118 «%s»"), *X->TopText()));
			X->ClearBoxes();
			const TCHAR* Bad[] = { TEXT("Bad Name!"), TEXT("Иван"), TEXT("O'Neil"), TEXT(""), TEXT("ABCDEFGHIJKLMNOPQRS") };
			int32 N9583 = 0;
			for (const TCHAR* B : Bad)
			{
				X->TypeAndCheck(B);
				if (X->LastNotice() == AsdaRenameLoc::kBadName && X->TopText() == X->L(AsdaRenameLoc::kBadName)) { ++N9583; }
				X->ClearBoxes();
			}
			X->Check(N9583 == UE_ARRAY_COUNT(Bad), FString::Printf(TEXT("C недопустимые имена → 9583: %d из %d («%s»)"), N9583, (int32)UE_ARRAY_COUNT(Bad), *X->L(AsdaRenameLoc::kBadName)));
			X->Check(X->CountOp(OP_RECV_CLIENT_CHAR_NAME_CHECK) == 0, TEXT("C 6138 не ушёл ни разу"));
			X->TypeAndCheck(TEXT("Bad Name!"));
			X->Shot(TEXT("badname"));
		});
		// D
		S.Add([X]()
		{
			X->ClearBoxes();
			X->TypeAndCheck(X->NewName);
			const FSent* P = X->LastOp(OP_RECV_CLIENT_CHAR_NAME_CHECK);
			const bool bLay = P && P->Body.Num() == 24 && AsdaBytes::RdI32(P->Body.GetData()) == kCharId
				&& FAsdaLoginData::DecodeCp1251(P->Body.GetData() + 4, 20).TrimStartAndEnd() == X->NewName;
			X->Check(bLay, FString::Printf(TEXT("D 6138: %d б, charId и имя «%s»"), P ? P->Body.Num() : -1, *X->NewName));
			X->Inject6139(2);
		});
		// E
		S.Add([X]()
		{
			X->Check(X->LastNotice() == AsdaRenameLoc::kTaken && X->TopText() == X->L(AsdaRenameLoc::kTaken),
				FString::Printf(TEXT("E 6139 код 2 → 109404 «%s»"), *X->TopText()));
			X->Shot(TEXT("taken"));
		});
		S.Add([X]() { X->ClearBoxes(); X->Rn()->ClearTrace(); X->Inject6139(3); });
		S.Add([X]()
		{
			X->Check(X->LastNotice() == AsdaRenameLoc::kTaken, TEXT("E 6139 код 3 (запрещённое слово сервера) → 109404"));
			X->ClearBoxes();
			X->Inject6139(1);
		});
		// F
		S.Add([X]()
		{
			const FString T = X->TopText();
			X->Check(X->WM()->IsMsgBoxTop(1, 66) && T.Contains(X->NewName) && !T.Contains(TEXT("%")),
				FString::Printf(TEXT("F 6139 код 1 → вопрос 66 «%s»"), *T));
			X->Shot(TEXT("q66"));
		});
		S.Add([X]() { X->Answer(true, false); });
		S.Add([X]()
		{
			X->Check(X->Rn()->IsWindowShown() && X->CountOp(OP_RECV_CLIENT_CHAR_NAME_CHANGE) == 0, TEXT("F «Отмена» 66 — окно 7900 осталось, 6140 нет"));
			X->Inject6139(1);
		});
		S.Add([X]() { X->Answer(true, true); });
		// G
		S.Add([X]()
		{
			const FString T = X->TopText();
			X->Check(X->WM()->IsMsgBoxTop(1, 67) && T.Contains(TEXT(">") + X->OldName + TEXT("<")) && T.Contains(TEXT(">") + X->NewName + TEXT("<")) && !T.Contains(TEXT("%")),
				FString::Printf(TEXT("G вопрос 67 «%s»"), *T));
			X->Shot(TEXT("q67"));
		});
		S.Add([X]() { X->Answer(true, false); });
		S.Add([X]()
		{
			X->Check(X->CountOp(OP_RECV_CLIENT_CHAR_NAME_CHANGE) == 0 && X->Rn()->IsWindowShown(), TEXT("G «Отмена» 67 — 6140 нет, окно осталось"));
			X->Inject6139(1);
		});
		S.Add([X]() { X->Answer(true, true); });
		S.Add([X]() { X->Answer(true, true); });
		// H
		S.Add([X]()
		{
			const FSent* P = X->LastOp(OP_RECV_CLIENT_CHAR_NAME_CHANGE);
			const bool bLay = P && P->Body.Num() == 27 && AsdaBytes::RdI32(P->Body.GetData()) == kCharId
				&& FAsdaLoginData::DecodeCp1251(P->Body.GetData() + 4, 20).TrimStartAndEnd() == X->NewName
				&& AsdaBytes::RdI16(P->Body.GetData() + 24) == X->Cell && P->Body[26] == 1;
			X->Check(bLay, FString::Printf(TEXT("H 6140: %d б, charId, имя, ячейка %d, контейнер 1"), P ? P->Body.Num() : -1, X->Cell));
			X->Check(!X->Rn()->IsWindowShown() && X->Rn()->GetInputText().IsEmpty(), TEXT("H окно 7900 скрыто, поле очищено"));
		});
		// I
		struct FCase { uint8 Code; int32 Loc; };
		static const FCase kCases[] = { { 0, 109411 }, { 2, 109406 }, { 3, 109406 }, { 4, 109407 }, { 5, 109408 }, { 6, 109409 }, { 7, 109410 } };
		for (const FCase& C : kCases)
		{
			S.Add([X, C]() { X->ClearBoxes(); X->Inject6141(C.Code); });
			S.Add([X, C]()
			{
				X->Check(X->LastNotice() == C.Loc && X->TopText() == X->L(C.Loc),
					FString::Printf(TEXT("I 6141 код %d → %d «%s»"), C.Code, C.Loc, *X->TopText()));
				if (C.Code == 2) { X->Shot(TEXT("clan")); }
			});
		}
		// J
		S.Add([X]() { X->ClearBoxes(); X->Inject6141(1); });
		S.Add([X]()
		{
			const FAsdaItemInstance* C = X->Sub<UAsdaInventorySubsystem>()->GetCell(1, X->Cell);
			X->Check(!C || C->IsEmpty(), TEXT("J 6141 код 1 → ячейка предмета пуста"));
		});
		S.Add([X]()
		{
			X->Check(X->Rn()->GetLeaveCount() == 1, FString::Printf(TEXT("J уход к выбору персонажа сработал (%d)"), X->Rn()->GetLeaveCount()));
			X->Check(X->WM()->GetMsgBoxQueueNum() == 0, TEXT("J окон-отказов нет"));
			X->UseItem();
		});
		// K
		S.Add([X]() { X->Answer(true, true); });
		S.Add([X]()
		{
			const bool bShown = X->Rn()->IsWindowShown();
			X->Rn()->ClickClose();
			X->Check(bShown && !X->Rn()->IsWindowShown(), TEXT("K кнопка 7902 закрывает окно"));
			AsdaCVarSave::SetForTest(TEXT("asda.Rename.Flow"), 0);
			X->ClearBoxes();
			X->UseItem();
		});
		// L
		S.Add([X]()
		{
			X->Check(X->CountOp(5450) == 1 && !X->WM()->IsMsgBoxQueued(1, 65), TEXT("L ключ Flow 0 → прежний 5450, вопроса 65 нет"));
			AsdaCVarSave::SetForTest(TEXT("asda.Rename.Flow"), 1);
		});

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([R](float)
		{
			FRun& Y = R.Get();
			if (Y.StepI < Y.Steps.Num()) { Y.Steps[Y.StepI++](); return true; }
			if (!Y.OutDir.IsEmpty())
			{
				IFileManager::Get().MakeDirectory(*Y.OutDir, true);
				for (const FString& N : Y.Shots)
				{
					const FString Src = FPaths::ScreenShotDir() / (N + TEXT(".png"));
					Y.Check(IFileManager::Get().Copy(*(Y.OutDir / (N + TEXT(".png"))), *Src) == COPY_OK, FString::Printf(TEXT("кадр %s"), *N));
				}
			}
			Y.ClearBoxes();
			if (UAsdaInventorySubsystem* I = Y.Sub<UAsdaInventorySubsystem>()) { I->ClearCell(1, Y.Cell); }
			if (UAsdaCharRenameSubsystem* Rn = Y.Rn()) { Rn->SetSuppressLeave(false); }
			if (UAsdaNetSubsystem* N = Y.Sub<UAsdaNetSubsystem>()) { AsdaSelfTestKit::DisarmCapture(N, Y.Gen); }
			Y.Self.Reset();
			AsdaSelfTestKit::Finish(Y.World.Get(), kCmd, Y.bQuit, LogAsdaRenameTest, kPfx, Y.Ok, Y.Fail);
			return false;
		}), 0.6f);
	}));
#endif
