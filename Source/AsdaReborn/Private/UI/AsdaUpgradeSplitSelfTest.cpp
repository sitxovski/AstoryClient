// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОКНА «РАЗДЕЛЕНИЕ УЛУЧШЕНИЯ» 50500 (31.10.2026): `asda.SelfTest.UpgradeSplit [<папка кадров>] [delay=N] [quit]`. [ПОРТ]
//   Путь игрока без сети (-AsdaNoNet), карта мира. Вещи и камни кладутся в сумку боевой точкой (`ApplyExternalRecord`),
//   вход — `UseFromBagWindow` (второй щелчок мыши по ячейке сумки), выбор — двойной щелчок по ячейке сетки окна, кнопки —
//   рассылкой `OnClicked`, исходящие — перехватом отправок, ответы сервера — `DebugFeedPacket(6550)`. Ожидания — по эталону.
//     A. Двойной щелчок по пустому камню 400 (205): окно открыто, в сетке только вещь с +24 > 0 и +27 ≥ 88, счётчики 205/203,
//        подписи 108500/108507/108508/108509.  B. Выбор: имя, «+90%», «90(%)», «есть 3 / нужно 3», камень набран.
//     C. «Разделение»: вопрос (1, 68) с текстом 108509; «Отмена» — 6549 нет; «ОК» — 6549 (72 б тела: камень, −1, вещь).
//     D. Камней мало (уровень 5): 108517 «не хватает 2».  E. Нет места в сумке: 108518.
//     F. 6550 код 1 исход 0: надпись 50584, 109904 с числом камней, готовый камень в панели, камень и вещь обновлены.
//     G. Исход 2 (доспех): 50586 и 109907.  H. Коды 0/2/3/4/5/6/98/99/7 → 108600/108602…108605/108518/108606/108607/молча.
//     I. Крестик 50589 закрывает окно.  J. Ключ asda.UpgradeSplit.Enable 0 — окна нет.
//   Кадры окна (RU/EN — язык прогона): <папка>/upgrade_split_<lang>_select.png и _result.png.
//   Итог — «РАЗДЕЛЕНИЕ-УЛУЧШЕНИЯ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaUpgradeSplitSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaLang.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUpgradeSplitTest, Log, All);

namespace AsdaSplitTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.UpgradeSplit");
	struct FSent { uint16 Op = 0; TArray<uint8> Body; };
}

struct FAsdaUpgradeSplitSelfTest
{
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		int32 CStoneW = -1, CStoneA = -1, CWeapon = -1, CLow = -1, CArmor = -1, CResult = -1;
		int32 CHigh = -1, C86 = -1;   // ★ 31.10 (баг владельца «сетка пуста»): вторая половина сумки ×2 — +92% (берётся) и +86% (нет)
		int32 Weapon = 0, Armor = 0;
		FString ShotDir;
		uint32 CapGen = 0;
		TArray<AsdaSplitTestLocal::FSent> Sent;
		TArray<int32> Filled;             // ячейки, забитые для шага E
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TUniquePtr<FAsdaCVarScope> Keys;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaUpgradeSplitTest, Log, TEXT("РАЗДЕЛЕНИЕ-УЛУЧШЕНИЯ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaUpgradeSplitTest, Error, TEXT("РАЗДЕЛЕНИЕ-УЛУЧШЕНИЯ FAIL %s"), *What); }
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
	static void After(const FRef& R, float Sec, TFunction<void(const FRef&)> Fn)
	{
		UWorld* W = R->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
	}
	static TArray<uint8> Rec(int32 Code, int32 Cell, int32 Qty, int32 Grade, int32 Pct)
	{
		FAsdaItemRecordWriter Wr; Wr.Code(Code).Container(1).Slot(Cell).Second(-1).Amount(Qty).GemGrade(Grade).EnchantPercent(Pct);
		return Wr.ToArray();
	}
	static void Put(const FRef& R, const TArray<uint8>& Rec57)
	{
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R)) { Inv->ApplyExternalRecord(Rec57, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
	}
	static void Clear(const FRef& R, int32 Cell)
	{
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R)) { Inv->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, Cell), UAsdaInventorySubsystem::EExternalRecordMode::Default); }
	}
	static const FAsdaItemInstance* Cell(const FRef& R, int32 C)
	{
		const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		return Inv ? Inv->GetCell(1, C) : nullptr;
	}
	static int32 Code(const FRef& R, int32 C) { const FAsdaItemInstance* It = Cell(R, C); return (It && !It->IsEmpty()) ? It->ItemCode : 0; }
	static bool Dbl(const FRef& R, int32 C) { UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R); return Inv && Inv->UseFromBagWindow(1, C); }
	static UUserWidget* TwoBut(const FRef& R) { UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R); return WM ? WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX"))) : nullptr; }
	static FString QText(const FRef& R)
	{
		UUserWidget* Q = TwoBut(R);
		return Q ? UAsdaWindowSubsystem::ReadMsgBoxShownText(Q->GetWidgetFromName(FName(TEXT("TWOBUT_TEXT")))) : FString(TEXT("<нет>"));
	}
	static bool Click(UUserWidget* Host, const TCHAR* Name)
	{
		UButton* B = Host ? Cast<UButton>(Host->GetWidgetFromName(FName(Name))) : nullptr;
		if (!B) { return false; }
		B->OnClicked.Broadcast();
		return true;
	}
	static void ClearBoxes(const FRef& R) { if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R)) { WM->ClearMsgBoxQueue(TEXT("самотест UpgradeSplit")); } }
	static int32 CountOp(const FRef& R, uint16 Op) { int32 N = 0; for (const auto& S : R->Sent) { if (S.Op == Op) { ++N; } } return N; }
	static FString Fmt(const FString& F, int32 A, int32 B = 0)
	{
		FString O = F.Replace(TEXT("%%"), TEXT("\x01"));
		int32 I = O.Find(TEXT("%d")); if (I != INDEX_NONE) { O = O.Left(I) + FString::FromInt(A) + O.Mid(I + 2); }
		I = O.Find(TEXT("%d")); if (I != INDEX_NONE) { O = O.Left(I) + FString::FromInt(B) + O.Mid(I + 2); }
		return O.Replace(TEXT("\x01"), TEXT("%"));
	}
	static void Shot(const FRef& R, const TCHAR* Tag)
	{
		if (R->ShotDir.IsEmpty()) { return; }
		const FString File = FPaths::Combine(R->ShotDir, FString::Printf(TEXT("upgrade_split_%s_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), Tag));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogAsdaUpgradeSplitTest, Log, TEXT("РАЗДЕЛЕНИЕ-УЛУЧШЕНИЯ кадр %s"), *File);
	}
	// Тело 6550: код, 10 записей, вес, исход, число камней.
	static TArray<uint8> Body6550(int32 CodeV, const TArray<TArray<uint8>>& Recs, int32 Grade, int32 Used)
	{
		TArray<uint8> B; B.SetNumZeroed(577);
		B[0] = (uint8)(int8)CodeV;
		for (int32 i = 0; i < 10; ++i)
		{
			uint8* P = B.GetData() + 1 + 57 * i;
			if (Recs.IsValidIndex(i)) { FMemory::Memcpy(P, Recs[i].GetData(), 57); }
			else { P[5] = 0xFF; P[6] = 0xFF; }   // слот −1 — запись пропускается
		}
		const int32 Wt = 1500; FMemory::Memcpy(B.GetData() + 571, &Wt, 4);
		B[575] = (uint8)(int8)Grade; B[576] = (uint8)(int8)Used;
		return B;
	}
	static int32 VisibleOf(const FRef& R, int32 BagCell)
	{
		const UAsdaUpgradeSplitSubsystem* S = Sub<UAsdaUpgradeSplitSubsystem>(R);
		for (int32 i = 0; S && i < S->GetListNum(); ++i) { if (S->GetListCell(i) == BagCell) { return i; } }
		return -1;
	}

	static void Start(UWorld* W, bool bQuit, const FString& ShotDir);
	static void StepResults(const FRef& R);
	static void Finish(const FRef& R);
};

void FAsdaUpgradeSplitSelfTest::Start(UWorld* W, bool bQuit, const FString& ShotDir)
{
	using namespace AsdaSplitTestLocal;
	FRef R = MakeShared<FRun>();
	R->W = W; R->bQuit = bQuit; R->ShotDir = ShotDir;
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	UAsdaUpgradeSplitSubsystem* Sp = Sub<UAsdaUpgradeSplitSubsystem>(R);
	const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
	if (!Net || !Inv || !Sp || !IT) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("нет подсистемы сети/сумки/окна/таблиц")); return; }
	for (const FAsdaItemRecord& Rc : IT->GetAllItems())
	{
		if (!R->Weapon && Rc.ItemType == 9 && Rc.ItemCode >= 20551 && Rc.ItemSubType != 66) { R->Weapon = Rc.ItemCode; }
		if (!R->Armor && Rc.ItemType == 1 && Rc.ItemCode >= 20551) { R->Armor = Rc.ItemCode; }
	}
	FAsdaSpecialItemlistRow S400, S387, S401;
	const bool bTab = IT->GetSpecialItem(400, S400) && IT->GetSpecialItem(387, S387) && IT->GetSpecialItem(401, S401);
	R->Expect(bTab && S400.SubType == 205 && S387.SubType == 203 && S401.SubType == 204,
		FString::Printf(TEXT("0 таблица: 400 подтип %d (205), 387 подтип %d (203), 401 подтип %d (204)"), bTab ? S400.SubType : -1, bTab ? S387.SubType : -1, bTab ? S401.SubType : -1));
	TArray<int32> Free;
	for (int32 i = 29; i >= 0 && Free.Num() < 6; --i) { const FAsdaItemInstance* C = Inv->GetCell(1, i); if (!C || C->IsEmpty()) { Free.Add(i); } }
	if (Free.Num() < 6 || !R->Weapon || !R->Armor) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("в сумке нет 6 свободных ячеек или в таблице нет оружия/доспеха")); return; }
	R->CStoneW = Free[0]; R->CStoneA = Free[1]; R->CWeapon = Free[2]; R->CLow = Free[3]; R->CArmor = Free[4]; R->CResult = Free[5];
	for (int32 i = Inv->GetContainerSize(1) - 1; i >= 30 && (R->CHigh < 0 || R->C86 < 0); --i)
	{
		const FAsdaItemInstance* C = Inv->GetCell(1, i);
		if (C && !C->IsEmpty()) { continue; }
		if (R->CHigh < 0) { R->CHigh = i; } else { R->C86 = i; }
	}
	R->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
	R->Keys->Set(TEXT("asda.UpgradeSplit.Enable"), 1);
	R->Keys->Set(TEXT("asda.UpgradeSplit.LiveCounts"), 1);
	R->Keys->Set(TEXT("asda.UpgradeSplit.Code6Notice"), 1);
	R->Keys->Set(TEXT("asda.UpgradeSplit.MinPercent"), 0);   // ★ 01.11: любое улучшение (решение владельца)
	TWeakPtr<FRun> WR = R;
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		TSharedPtr<FRun> Run = WR.Pin();
		if (!Run) { return; }
		FSent S; S.Op = Op; if (P && Len > 0) { S.Body.Append(P, Len); }
		Run->Sent.Add(MoveTemp(S));
	}, 0, kCmd);
	if (R->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("перехват отправок занят")); return; }

	Put(R, Rec(400, R->CStoneW, 3, 0, 0));
	Put(R, Rec(387, R->CStoneA, 5, 0, 0));
	Put(R, Rec(R->Weapon, R->CWeapon, 1, 3, 90));
	Put(R, Rec(R->Weapon, R->CLow, 1, 2, 50));
	if (R->CHigh >= 0) { Put(R, Rec(R->Armor, R->CHigh, 1, 2, 92)); }   // вторая половина сумки ×2, +92% — в сетке
	if (R->C86 >= 0)   { Put(R, Rec(R->Weapon, R->C86, 1, 3, 86)); }    // +86% (как у вещи владельца) — в сетке с 01.11 (эталон: только ≥ 88)
	Put(R, Rec(R->Armor, R->CArmor, 1, 4, 95));
	if (Inv->IsWindowOpen() == false) { Inv->OpenWindow(); }

	// A. Вход.
	Dbl(R, R->CStoneW);
	R->Expect(Sp->IsOpen(), FString::Printf(TEXT("A1 двойной щелчок по пустому камню 400: окно 50500 открыто (%s)"), *Sp->DumpState()));
	R->Expect(VisibleOf(R, R->CWeapon) >= 0 && VisibleOf(R, R->CArmor) >= 0 && VisibleOf(R, R->CLow) >= 0 && VisibleOf(R, R->CStoneW) < 0,
		TEXT("A2 сетка (01.11, любое улучшение): +90%, +95% и +50% есть, камня нет"));
	R->Expect(Sp->GetEmptyCount(true) >= 3 && Sp->GetEmptyCount(false) >= 5 && Sp->GetControlText(50523) == FString::FromInt(Sp->GetEmptyCount(true))
		&& Sp->GetControlText(50525) == FString::FromInt(Sp->GetEmptyCount(false)),
		FString::Printf(TEXT("A3 счётчики пустых камней: оружие «%s», доспех «%s»"), *Sp->GetControlText(50523), *Sp->GetControlText(50525)));
	R->Expect(Sp->GetControlText(50501) == L(R, 108500) && Sp->GetControlText(50582) == L(R, 108507) && Sp->GetControlText(50583) == L(R, 108508)
		&& Sp->GetControlText(50541) == L(R, 108509) && Sp->GetControlText(50540) == L(R, 108506) && Sp->GetControlText(50522) == L(R, 108521),
		FString::Printf(TEXT("A4 подписи: «%s» / «%s» / «%s» / «%s»"), *Sp->GetControlText(50501), *Sp->GetControlText(50582), *Sp->GetControlText(50583), *Sp->GetControlText(50540)));
	R->Expect(Sp->GetControlText(50521).IsEmpty() && Sp->GetControlText(50511).IsEmpty(), TEXT("A5 без выбора: имя и «есть/нужно» пусты (образцы ассета стёрты)"));

	// B. Выбор вещи.
	Sp->OnCellDoubleClick(VisibleOf(R, R->CWeapon));
	const FString Name = IT->GetItemName(R->Weapon);
	R->Expect(Sp->GetSelectedCell() == R->CWeapon && Sp->GetControlText(50511) == Name && Sp->GetControlText(50513) == TEXT("+90%")
		&& Sp->GetControlText(50515) == TEXT("90(%)") && Sp->GetControlText(50516) == TEXT("+90%"),
		FString::Printf(TEXT("B1 выбор оружия: имя «%s», «%s», «%s»"), *Sp->GetControlText(50511), *Sp->GetControlText(50513), *Sp->GetControlText(50515)));
	R->Expect(Sp->GetStoneCells().Num() >= 1 && Sp->GetStoneCells().Contains(R->CStoneW) && Sp->GetControlText(50521).Contains(TEXT("3")),
		FString::Printf(TEXT("B2 камни оружия набраны (%d стопок), «%s»"), Sp->GetStoneCells().Num(), *Sp->GetControlText(50521)));
	R->Expect(Sp->GetControlText(VisibleOf(R, R->CWeapon) + 50551) == TEXT("+90%"), TEXT("B3 «+90%» под ячейкой выбранной вещи"));
	After(R, 0.4f, [](const FRef& R)
	{
		Shot(R, TEXT("select"));
		After(R, 0.6f, [](const FRef& R)
		{
			UAsdaUpgradeSplitSubsystem* Sp = Sub<UAsdaUpgradeSplitSubsystem>(R);
			// C. Вопрос и 6549.
			Sp->OnSplitClicked();
			After(R, 0.3f, [](const FRef& R)
			{
				UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
				R->Expect(WM && WM->IsMsgBoxQueued(1, 68) && QText(R).Contains(L(R, 108509).Left(8)), FString::Printf(TEXT("C1 вопрос (1, 68): «%s»"), *QText(R)));
				R->Expect(Click(TwoBut(R), TEXT("TWOBUT_BUT01")), TEXT("C2 «Отмена» нажата"));
				After(R, 0.3f, [](const FRef& R)
				{
					R->Expect(CountOp(R, 6549) == 0, TEXT("C3 после «Отмены» 6549 нет"));
					Sub<UAsdaUpgradeSplitSubsystem>(R)->OnSplitClicked();
					After(R, 0.3f, [](const FRef& R)
					{
						Click(TwoBut(R), TEXT("TWOBUT_BUT00"));
						After(R, 0.3f, [](const FRef& R)
						{
							const AsdaSplitTestLocal::FSent* S = R->Sent.FindByPredicate([](const AsdaSplitTestLocal::FSent& X) { return X.Op == 6549; });
							bool b = false; FString Got = TEXT("<нет>");
							if (S && S->Body.Num() >= 72)
							{
								const uint8* P = S->Body.GetData() + (S->Body.Num() - 72);
								b = AsdaBytes::RdI32(P) == 400 && AsdaBytes::RdI16(P + 32) == R->CStoneW && AsdaBytes::RdI16(P + 48) == 1
									&& AsdaBytes::RdI32(P + 4) == -1 && AsdaBytes::RdI16(P + 34) == -1 && AsdaBytes::RdI16(P + 50) == -1
									&& AsdaBytes::RdI32(P + 64) == R->Weapon && AsdaBytes::RdI16(P + 68) == R->CWeapon && AsdaBytes::RdI16(P + 70) == 1;
								Got = FString::Printf(TEXT("камень %d сл%d к%d, второй %d, вещь %d сл%d к%d (длина %d)"), AsdaBytes::RdI32(P), AsdaBytes::RdI16(P + 32),
									AsdaBytes::RdI16(P + 48), AsdaBytes::RdI32(P + 4), AsdaBytes::RdI32(P + 64), AsdaBytes::RdI16(P + 68), AsdaBytes::RdI16(P + 70), S->Body.Num());
							}
							R->Expect(CountOp(R, 6549) == 1 && b, FString::Printf(TEXT("C4 «ОК»: 6549 — %s"), *Got));
							ClearBoxes(R);
							// D. Камней мало.
							UAsdaUpgradeSplitSubsystem* Sp = Sub<UAsdaUpgradeSplitSubsystem>(R);
							Put(R, Rec(R->Weapon, R->CWeapon, 1, 5, 90));
							Sp->Tick(0.f);
							Sp->ClearShownNotices();
							Sp->OnSplitClicked();
							const TArray<FString>& N = Sp->GetShownNotices();
							R->Expect(N.Num() == 1 && N[0] == Fmt(L(R, 108517), 2), FString::Printf(TEXT("D уровень 5 при 3 камнях: «%s»"), N.Num() ? *N[0] : TEXT("")));
							ClearBoxes(R);
							Put(R, Rec(R->Weapon, R->CWeapon, 1, 3, 90));
							// E. Нет места в сумке.
							const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
							const int32 Total = Inv ? Inv->GetContainerSize(1) : 0;
							for (int32 C = 0; C < Total; ++C) { const FAsdaItemInstance* It = Cell(R, C); if (!It || It->IsEmpty()) { Put(R, Rec(R->Armor, C, 1, 0, 0)); R->Filled.Add(C); } }
							Sp->ClearShownNotices();
							Sp->OnSplitClicked();
							R->Expect(Sp->GetShownNotices().Num() == 1 && Sp->GetShownNotices()[0] == L(R, 108518),
								FString::Printf(TEXT("E сумка полна (%d забито): «%s»"), R->Filled.Num(), Sp->GetShownNotices().Num() ? *Sp->GetShownNotices()[0] : TEXT("")));
							for (int32 C : R->Filled) { Clear(R, C); }
							R->Filled.Reset();
							ClearBoxes(R);
							StepResults(R);
						});
					});
				});
			});
		});
	});
}

void FAsdaUpgradeSplitSelfTest::StepResults(const FRef& R)
{
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	UAsdaUpgradeSplitSubsystem* Sp = Sub<UAsdaUpgradeSplitSubsystem>(R);
	if (!Net || !Sp) { Finish(R); return; }
	// F. Успех, исход 0: готовый камень 401 в CResult, вещь сброшена, камень оружия израсходован.
	Sp->Tick(0.f);
	Sp->ClearShownNotices();
	TArray<TArray<uint8>> Recs;
	Recs.Add(Rec(401, R->CResult, 1, 3, 90));
	Recs.Add(Rec(R->Weapon, R->CWeapon, 1, 0, 0));
	{ FAsdaItemRecordWriter Wr; Wr.Container(1).Slot(R->CStoneW); Recs.Add(Wr.ToArray()); }   // стопка кончилась: код 0
	Net->DebugFeedPacket(6550, Body6550(1, Recs, 0, 3));
	Sp->Tick(0.f);
	const TArray<FString> N = Sp->GetShownNotices();
	R->Expect(N.Num() == 1 && N[0] == Fmt(L(R, 109904), 3), FString::Printf(TEXT("F1 исход 0 (оружие): «%s»"), N.Num() ? *N[0] : TEXT("")));
	R->Expect(Sp->IsEffectShown(0) && !Sp->IsEffectShown(1) && !Sp->IsEffectShown(2) && !Sp->IsEffectShown(3), TEXT("F2 видна только надпись 50584 «Отлично»"));
	R->Expect(Sp->GetResultCell() == R->CResult && Code(R, R->CResult) == 401 && Sp->GetControlText(50533) == TEXT("+90%") && Sp->GetControlText(50535) == TEXT("90(%)"),
		FString::Printf(TEXT("F3 готовый камень: ячейка %d, «%s» «%s»"), Sp->GetResultCell(), *Sp->GetControlText(50533), *Sp->GetControlText(50535)));
	R->Expect(Code(R, R->CStoneW) == 0 && VisibleOf(R, R->CWeapon) < 0, TEXT("F4 пустой камень израсходован, вещь без улучшения ушла из сетки"));
	After(R, 0.4f, [](const FRef& R)
	{
		Shot(R, TEXT("result"));
		After(R, 0.6f, [](const FRef& R)
		{
			UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
			UAsdaUpgradeSplitSubsystem* Sp = Sub<UAsdaUpgradeSplitSubsystem>(R);
			if (!Net || !Sp) { Finish(R); return; }
			ClearBoxes(R);
			// G. Доспех, исход 2.
			Sp->OnCellDoubleClick(VisibleOf(R, R->CArmor));
			Sp->ClearShownNotices();
			TArray<TArray<uint8>> Recs2;
			Net->DebugFeedPacket(6550, Body6550(1, Recs2, 2, 2));
			Sp->Tick(0.f);
			const TArray<FString> N = Sp->GetShownNotices();
			R->Expect(Sp->IsEffectShown(2) && !Sp->IsEffectShown(0) && N.Num() == 1 && N[0] == Fmt(L(R, 109907), 2),
				FString::Printf(TEXT("G исход 2 (доспех): 50586, «%s»"), N.Num() ? *N[0] : TEXT("")));
			// H. Коды отказа.
			struct FCase { int32 Code; int32 Loc; };
			const FCase Cases[] = { {0, 108600}, {2, 108602}, {3, 108603}, {4, 108604}, {5, 108605}, {6, 108518}, {98, 108606}, {99, 108607}, {7, 0} };
			for (const FCase& C : Cases)
			{
				ClearBoxes(R);
				Sp->ClearShownNotices();
				Net->DebugFeedPacket(6550, Body6550(C.Code, TArray<TArray<uint8>>(), 0, 0));
				Sp->Tick(0.f);
				const TArray<FString>& M = Sp->GetShownNotices();
				const bool b = C.Loc ? (M.Num() == 1 && M[0] == L(R, C.Loc)) : (M.Num() == 0);
				R->Expect(b, FString::Printf(TEXT("H код %d: «%s»"), C.Code, M.Num() ? *M[0] : TEXT("(молча)")));
			}
			ClearBoxes(R);
			// K. ★ Решение владельца 01.11: «переносить любые улучшения, главное — чтобы улучшение было» (asda.UpgradeSplit.MinPercent 0).
			{
				const bool bHigh = R->CHigh >= 0 && VisibleOf(R, R->CHigh) >= 0;
				const bool b86   = R->C86 >= 0 && VisibleOf(R, R->C86) >= 0;
				R->Expect(bHigh && b86, FString::Printf(TEXT("K1 вторая половина сумки ×2: +92%% (ячейка %d) и +86%% (ячейка %d) — оба в сетке"), R->CHigh, R->C86));
				R->Expect(Sp->GetControlText(50541) == L(R, 108509), FString::Printf(TEXT("K2 вещи есть — 50541 предупреждение 108509: «%s»"), *Sp->GetControlText(50541)));
				// Вещь БЕЗ улучшения (+24 = 0, +27 = 0) в сетку не попадает — главное условие.
				Clear(R, R->CArmor); Clear(R, R->CHigh); Clear(R, R->C86); Clear(R, R->CLow);
				Put(R, Rec(R->Armor, R->CArmor, 1, 0, 0));
				Sp->Tick(0.f);
				R->Expect(Sp->GetListNum() == 0 && Sp->GetControlText(50541) == L(R, 108509),
					FString::Printf(TEXT("K3 улучшенных вещей нет (вещь без улучшения в сумке) — сетка пуста, 50541 — 108509: «%s»"), *Sp->GetControlText(50541)));
				// Ключ 88 — порог оригинала: +86% не берётся, пустая сетка объясняет правило 110112.
				R->Keys->Set(TEXT("asda.UpgradeSplit.MinPercent"), 88);
				Put(R, Rec(R->Weapon, R->C86, 1, 3, 86));
				Sp->Tick(0.f);
				R->Expect(Sp->GetListNum() == 0 && Sp->GetControlText(50541) == L(R, 110112),
					FString::Printf(TEXT("K4 ключ MinPercent 88 (оригинал): +86%% не в сетке, 50541 — правило 110112: «%s»"), *Sp->GetControlText(50541)));
				R->Keys->Set(TEXT("asda.UpgradeSplit.MinPercent"), 0);
				Put(R, Rec(R->Weapon, R->C86, 1, 3, 86));   // перерисовка окна идёт по изменению сумки (смена ключа её не вызывает)
				Sp->Tick(0.f);
				R->Expect(Sp->GetListNum() == 1 && VisibleOf(R, R->C86) >= 0, TEXT("K5 ключ 0 — +86% снова в сетке"));
				Shot(R, TEXT("any_upgrade"));
			}
			// Кадр пустой сетки должен успеть отрисоваться — крестик и ключ 0 кадром позже.
			After(R, 0.8f, [](const FRef& R)
			{
				UAsdaUpgradeSplitSubsystem* Sp = Sub<UAsdaUpgradeSplitSubsystem>(R);
				if (!Sp) { Finish(R); return; }
				// I. Крестик.
				R->Expect(Click(Sp->GetWidget(), TEXT("Strength_Part_0_X")) && !Sp->IsOpen(), TEXT("I крестик 50589 закрыл окно"));
				// J. Ключ 0.
				R->Keys->Set(TEXT("asda.UpgradeSplit.Enable"), 0);
				Put(R, Rec(400, R->CStoneW, 1, 0, 0));
				Dbl(R, R->CStoneW);
				R->Expect(!Sp->IsOpen(), TEXT("J ключ asda.UpgradeSplit.Enable 0: окна нет"));
				R->Keys->Set(TEXT("asda.UpgradeSplit.Enable"), 1);
				After(R, 0.3f, [](const FRef& R) { Finish(R); });
			});
		});
	});
}

void FAsdaUpgradeSplitSelfTest::Finish(const FRef& R)
{
	if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R)) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); }
	if (UAsdaUpgradeSplitSubsystem* Sp = Sub<UAsdaUpgradeSplitSubsystem>(R)) { Sp->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
	for (int32 C : R->Filled) { Clear(R, C); }
	for (int32 C : { R->CStoneW, R->CStoneA, R->CWeapon, R->CLow, R->CArmor, R->CResult, R->CHigh, R->C86 }) { if (C >= 0) { Clear(R, C); } }
	ClearBoxes(R);
	if (R->SelfScope) { R->SelfScope->Restore(); }
	if (R->Keys) { R->Keys->Restore(); }
	AsdaSelfTestKit::Finish(R->W.Get(), AsdaSplitTestLocal::kCmd, R->bQuit, LogAsdaUpgradeSplitTest, TEXT("РАЗДЕЛЕНИЕ-УЛУЧШЕНИЯ"), R->Ok, R->Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestUpgradeSplit(
	TEXT("asda.SelfTest.UpgradeSplit"),
	TEXT("Самотест окна «Разделение улучшения» 50500 путём игрока: вход двойным щелчком по пустому камню, сетка, выбор, вопрос 108509, "
	     "6549, ответы 6550 (исходы и коды), нехватка камней и места, крестик, ключ 0, кадры PNG. `[папка кадров]` `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaSplitTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		const FString Dir = A.Str(0);
		if (A.Delay <= 0.f) { FAsdaUpgradeSplitSelfTest::Start(W, A.bQuit, Dir); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit, Dir]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaSplitTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaUpgradeSplitSelfTest::Start(Wd, bQuit, Dir); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
