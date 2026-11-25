// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПЕРЕНОСА УЛУЧШЕНИЯ КАМНЕМ (31.10.2026, баг 4): `asda.SelfTest.UpgradeStone [delay=N] [quit]`. [ПОРТ]
//   Путь игрока, без сети (-AsdaNoNet), карта мира. Камень и вещи кладутся в сумку боевой точкой (`ApplyExternalRecord`),
//   двойной щелчок — `UseFromBagWindow` (тот же вход, что второй щелчок мыши по ячейке окна сумки), «ОК»/«Отмена» вопроса —
//   рассылкой `OnClicked` кнопок TWOBUT, исходящие ловятся перехватом отправок, ответы сервера — `DebugFeedPacket(6552)`.
//   Ожидания — по эталону (шапка `UAsdaUpgradeStoneSubsystem.h`), независимо от боевого кода.
//     A. Камень оружия 401 (204): двойной щелчок — взвод, курсор P0, ни одной отправки.
//     B. Двойной щелчок по ДОСПЕХУ (пара 204 + доспех недопустима): взвод снят, вопроса нет, вещь не надета (5000 нет).
//     C. Взвод → щелчок по коробке (200): молча, взвод ОСТАЁТСЯ; затем по ОРУЖИЮ: вопрос (1, 69) с текстом 110322 и именем.
//     D. «Отмена» — 6551 нет; повтор → «ОК»: 6551 (40 б) — камень код/слот/контейнер, затем оружие; второй «ОК» в полёте — тишина.
//     E. 6552 код 1: «в полёте» снят, камень из ячейки ушёл, у оружия +27 = 88, нотисы 110324 «[имя] [+88%]» и 108531.
//     F. 6552 код 5: нотисы 110323 и 108531; коды 0/2/3/4/99 → 108530/108532/108533/108534/108535; код 7 — молча.
//     G. Камень доспеха 363 (206) + доспех — вопрос; + оружие — нет.  H. Открытие сумки снимает взвод.
//     I. Ключ asda.UpgradeStone.Enable 0 — взвода нет (прежний путь).
//   Итог — «КАМЕНЬ-УЛУЧШЕНИЯ ИТОГ: ok=… fail=…». Тексты — язык прогона (RU/EN), ожидания из той же таблицы.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaUpgradeStoneSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "World/AsdaPlayerController.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UI/AsdaItemUpgradeSubsystem.h"
#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUpgradeStoneTest, Log, All);

namespace AsdaUpgradeStoneTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.UpgradeStone");
	// Перехваченная отправка.
	struct FSent { uint16 Op = 0; TArray<uint8> Body; };
}

struct FAsdaUpgradeStoneSelfTest
{
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		int32 CStone = -1, CWeapon = -1, CArmor = -1, CBox = -1;   // ячейки сумки
		int32 Weapon = 0, Armor = 0, Box = 0;                       // коды вещей (из таблицы)
		uint32 CapGen = 0;
		TArray<AsdaUpgradeStoneTestLocal::FSent> Sent;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TUniquePtr<FAsdaCVarScope> Keys;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaUpgradeStoneTest, Log, TEXT("КАМЕНЬ-УЛУЧШЕНИЯ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaUpgradeStoneTest, Error, TEXT("КАМЕНЬ-УЛУЧШЕНИЯ FAIL %s"), *What); }
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
	static void Put(const FRef& R, int32 Code, int32 Cell)
	{
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R))
		{
			Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, 1, Cell, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		}
	}
	// Камень с уровнем улучшения (+24) и процентом (+27) — как его кладёт разборка (401/363).
	static void PutStone(const FRef& R, int32 Code)
	{
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R))
		{
			FAsdaItemRecordWriter Wr; Wr.Code(Code).Container(1).Slot(R->CStone).Second(-1).Amount(1).GemGrade(9).EnchantPercent(88);
			Inv->ApplyExternalRecord(Wr.ToArray(), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
		}
	}
	static int32 CellCode(const FRef& R, int32 Cell)
	{
		const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		const FAsdaItemInstance* C = Inv ? Inv->GetCell(1, Cell) : nullptr;
		return (C && !C->IsEmpty()) ? C->ItemCode : 0;
	}
	static bool Dbl(const FRef& R, int32 Cell)
	{
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		return Inv && Inv->UseFromBagWindow(1, Cell);
	}
	static UUserWidget* TwoBut(const FRef& R)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
		return WM ? WM->FindWindow(FName(TEXT("TWOBUT_MSGBOX"))) : nullptr;
	}
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
	static bool Asked(const FRef& R)
	{
		const UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R);
		return WM && WM->IsMsgBoxQueued(1, 69);
	}
	static void ClearBoxes(const FRef& R, const TCHAR* Why)
	{
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(R)) { WM->ClearMsgBoxQueue(Why); }
	}
	static int32 CountOp(const FRef& R, uint16 Op)
	{
		int32 N = 0;
		for (const auto& S : R->Sent) { if (S.Op == Op) { ++N; } }
		return N;
	}
	// Отправки «действия с предметом» (общее использование 5450/5028, надеть 5000, перенос 6551) — посторонний трафик не в счёт.
	static int32 ItemOps(const FRef& R) { return CountOp(R, 5450) + CountOp(R, 5028) + CountOp(R, 5000) + CountOp(R, 6551); }
	static bool CursorIs(const FRef& R, AAsdaPlayerController::EAsdaCursor C)
	{
		UWorld* W = R->W.Get();
		const AAsdaPlayerController* Pc = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		return !Pc || Pc->CurrentCursor == C;   // без контроллера курсор не проверяем (подсистема его тоже не трогает)
	}
	// Тело 6552: код, запись камня (@1), запись вещи (@58), вес (@115).
	static TArray<uint8> Body6552(const FRef& R, int32 Code, int32 StoneCode, int32 GearCode, int32 Pct)
	{
		TArray<uint8> B; B.SetNumZeroed(119);
		B[0] = (uint8)(int8)Code;
		FAsdaItemRecordWriter S; S.Code(StoneCode).Container(1).Slot(R->CStone);
		FAsdaItemRecordWriter G; G.Code(GearCode).Container(1).Slot(R->CWeapon).Second(-1).Amount(1).GemGrade(9).EnchantPercent(Pct);
		const TArray<uint8> SA = S.ToArray(), GA = G.ToArray();
		FMemory::Memcpy(B.GetData() + 1, SA.GetData(), 57);
		FMemory::Memcpy(B.GetData() + 58, GA.GetData(), 57);
		const int32 Wt = 1234;
		FMemory::Memcpy(B.GetData() + 115, &Wt, 4);
		return B;
	}
	static FString Fmt(const FString& F, const FString& S, int32 D)
	{
		FString Out = F;
		Out = Out.Replace(TEXT("%%"), TEXT("\x01"));
		int32 I = Out.Find(TEXT("%s"));
		if (I != INDEX_NONE) { Out = Out.Left(I) + S + Out.Mid(I + 2); }
		I = Out.Find(TEXT("%d"));
		if (I != INDEX_NONE) { Out = Out.Left(I) + FString::FromInt(D) + Out.Mid(I + 2); }
		return Out.Replace(TEXT("\x01"), TEXT("%"));
	}

	static void Start(UWorld* W, bool bQuit);
	static void StepResults(const FRef& R);
	static void Finish(const FRef& R);
};

void FAsdaUpgradeStoneSelfTest::Start(UWorld* W, bool bQuit)
{
	using namespace AsdaUpgradeStoneTestLocal;
	FRef R = MakeShared<FRun>();
	R->W = W; R->bQuit = bQuit;
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	UAsdaUpgradeStoneSubsystem* St = Sub<UAsdaUpgradeStoneSubsystem>(R);
	const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
	if (!Net || !Inv || !St || !IT) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("нет подсистемы сети/сумки/камней/таблиц")); return; }
	// Вещи — первые подходящие из таблицы: оружие (тип 9), доспех (тип 1, кираса), коробка (подтип 200).
	for (const FAsdaItemRecord& Rec : IT->GetAllItems())
	{
		if (!R->Weapon && Rec.ItemType == 9 && Rec.ItemCode >= 20551) { R->Weapon = Rec.ItemCode; }
		if (!R->Armor && Rec.ItemType == 1 && Rec.ItemCode >= 20551) { R->Armor = Rec.ItemCode; }
		if (!R->Box && Rec.ItemSubType == 200) { R->Box = Rec.ItemCode; }
	}
	FAsdaSpecialItemlistRow S401, S363;
	const bool bTables = IT->GetSpecialItem(401, S401) && IT->GetSpecialItem(363, S363);
	R->Expect(bTables && S401.SubType == 204 && S363.SubType == 206,
		FString::Printf(TEXT("0 таблица: 401 подтип %d (ждём 204 — оружие), 363 подтип %d (ждём 206 — доспех)"), bTables ? S401.SubType : -1, bTables ? S363.SubType : -1));
	TArray<int32> Free;
	for (int32 i = 29; i >= 0 && Free.Num() < 4; --i)
	{
		const FAsdaItemInstance* C = Inv->GetCell(1, i);
		if (!C || C->IsEmpty()) { Free.Add(i); }
	}
	if (Free.Num() < 4 || !R->Weapon || !R->Armor || !R->Box)
	{
		AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("в сумке нет 4 свободных ячеек или в таблице нет оружия/доспеха/коробки"));
		return;
	}
	R->CStone = Free[0]; R->CWeapon = Free[1]; R->CArmor = Free[2]; R->CBox = Free[3];
	R->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
	R->Keys->Set(TEXT("asda.UpgradeStone.Enable"), 1);
	TWeakPtr<FRun> WR = R;
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		TSharedPtr<FRun> Run = WR.Pin();
		if (!Run) { return; }
		FSent S; S.Op = Op; if (P && Len > 0) { S.Body.Append(P, Len); }
		Run->Sent.Add(MoveTemp(S));
	}, 0, kCmd);
	if (R->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("перехват отправок занят")); return; }

	PutStone(R, 401); Put(R, R->Weapon, R->CWeapon); Put(R, R->Armor, R->CArmor); Put(R, R->Box, R->CBox);
	if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }   // путь игрока: щелчки — по открытой сумке
	St->CancelPick(TEXT("самотест: начало"));

	// A. Взвод.
	Dbl(R, R->CStone);
	R->Expect(St->IsPickActive() && St->GetStoneCell() == R->CStone && ItemOps(R) == 0 && CellCode(R, R->CStone) == 401,
		FString::Printf(TEXT("A1 двойной щелчок по камню 401: взвод, камень на месте, отправок предмета %d (%s)"), ItemOps(R), *St->DumpState()));
	R->Expect(CursorIs(R, AAsdaPlayerController::EAsdaCursor::Pick), TEXT("A2 курсор P0 (индекс 26)"));

	// B. Недопустимая пара: камень оружия + доспех.
	Dbl(R, R->CArmor);
	R->Expect(!St->IsPickActive() && !Asked(R) && ItemOps(R) == 0 && CursorIs(R, AAsdaPlayerController::EAsdaCursor::Normal),
		FString::Printf(TEXT("B1 камень оружия → доспех %d: взвод снят, вопроса нет, вещь не надета (отправок предмета %d)"), R->Armor, ItemOps(R)));

	// C. Коробка при взводе — молча, взвод остаётся; затем оружие — вопрос.
	Dbl(R, R->CStone);
	Dbl(R, R->CBox);
	R->Expect(St->IsPickActive() && !Asked(R), FString::Printf(TEXT("C1 коробка %d при взводе: молча, взвод остался"), R->Box));
	Dbl(R, R->CWeapon);
	const FString Name = IT->GetItemName(R->Weapon);
	R->Expect(!St->IsPickActive() && St->GetGearCell() == R->CWeapon && Asked(R) && ItemOps(R) == 0,
		FString::Printf(TEXT("C2 оружие %d: вопрос (1, 69), взвод снят, отправок предмета %d"), R->Weapon, ItemOps(R)));
	After(R, 0.3f, [Name](const FRef& R)
	{
		const FString Want = Fmt(L(R, 110322), Name, 0);
		R->Expect(!Want.IsEmpty() && QText(R).Contains(Name) && QText(R).Contains(Want.Left(6)),
			FString::Printf(TEXT("C3 текст вопроса 110322 с именем «%s»: «%s»"), *Name, *QText(R)));
		// D. «Отмена».
		R->Expect(Click(TwoBut(R), TEXT("TWOBUT_BUT01")), TEXT("D1 «Отмена» нажата"));
		After(R, 0.3f, [](const FRef& R)
		{
			R->Expect(CountOp(R, 6551) == 0 && CellCode(R, R->CStone) == 401, TEXT("D2 после «Отмены» 6551 нет, камень на месте"));
			Dbl(R, R->CStone); Dbl(R, R->CWeapon);
			After(R, 0.3f, [](const FRef& R)
			{
				R->Expect(Click(TwoBut(R), TEXT("TWOBUT_BUT00")), TEXT("D3 «ОК» нажата"));
				After(R, 0.3f, [](const FRef& R)
				{
					UAsdaUpgradeStoneSubsystem* St = Sub<UAsdaUpgradeStoneSubsystem>(R);
					const AsdaUpgradeStoneTestLocal::FSent* S = R->Sent.FindByPredicate([](const AsdaUpgradeStoneTestLocal::FSent& X) { return X.Op == 6551; });
					bool bBody = false;
					FString Got = TEXT("<нет>");
					if (S && S->Body.Num() >= 16)
					{
						const uint8* P = S->Body.GetData() + (S->Body.Num() - 16);   // тело — последние 16 б (секрет и MD5 впереди)
						bBody = AsdaBytes::RdI32(P) == 401 && AsdaBytes::RdI16(P + 4) == R->CStone && AsdaBytes::RdI16(P + 6) == 1
							&& AsdaBytes::RdI32(P + 8) == R->Weapon && AsdaBytes::RdI16(P + 12) == R->CWeapon && AsdaBytes::RdI16(P + 14) == 1;
						Got = FString::Printf(TEXT("%d/%d/%d %d/%d/%d (длина %d)"), AsdaBytes::RdI32(P), AsdaBytes::RdI16(P + 4), AsdaBytes::RdI16(P + 6),
							AsdaBytes::RdI32(P + 8), AsdaBytes::RdI16(P + 12), AsdaBytes::RdI16(P + 14), S->Body.Num());
					}
					R->Expect(CountOp(R, 6551) == 1 && bBody && St && St->IsPending(),
						FString::Printf(TEXT("D4 6551: камень 401/%d/1, оружие %d/%d/1 — пришло %s, в полёте=%d"), R->CStone, R->Weapon, R->CWeapon, *Got, St && St->IsPending() ? 1 : 0));
					if (St) { St->SendRequest(); }
					R->Expect(CountOp(R, 6551) == 1, TEXT("D5 повторный «ОК» в полёте — молча (6551 один)"));
					StepResults(R);
				});
			});
		});
	});
}

void FAsdaUpgradeStoneSelfTest::StepResults(const FRef& R)
{
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	UAsdaUpgradeStoneSubsystem* St = Sub<UAsdaUpgradeStoneSubsystem>(R);
	const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	if (!Net || !St || !Inv) { Finish(R); return; }
	ClearBoxes(R, TEXT("самотест UpgradeStone E"));
	St->ClearShownNotices();
	// E. Успех: камень снят (код 0 в записи), оружие +88.
	Net->DebugFeedPacket(6552, Body6552(R, 1, 0, R->Weapon, 88));
	After(R, 0.3f, [](const FRef& R)
	{
		UAsdaUpgradeStoneSubsystem* St = Sub<UAsdaUpgradeStoneSubsystem>(R);
		const UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
		const FAsdaItemInstance* G = Inv ? Inv->GetCell(1, R->CWeapon) : nullptr;
		const int32 Pct = (G && G->Raw.Num() >= 29) ? AsdaBytes::RdI16(G->Raw.GetData() + 27) : -1;
		R->Expect(St && !St->IsPending() && CellCode(R, R->CStone) == 0 && Pct == 88,
			FString::Printf(TEXT("E1 6552 код 1: в полёте снят, камень ушёл (ячейка %d), у оружия +27 = %d"), CellCode(R, R->CStone), Pct));
		const TArray<FString> N = St ? St->GetShownNotices() : TArray<FString>();
		const FString WantOk = Fmt(L(R, 110324), IT ? IT->GetItemName(R->Weapon) : FString(), 88);
		R->Expect(N.Num() == 2 && N[0] == WantOk && N[1] == L(R, 108531),
			FString::Printf(TEXT("E2 нотисы: «%s» | «%s» (ждём «%s» | «%s»)"), N.Num() > 0 ? *N[0] : TEXT(""), N.Num() > 1 ? *N[1] : TEXT(""), *WantOk, *L(R, 108531)));
		R->Expect(WantOk.Contains(TEXT("88%")) && !WantOk.Contains(TEXT("%d")) && !WantOk.Contains(TEXT("%s")),
			FString::Printf(TEXT("E3 подстановка 110324 без сырых %%s/%%d: «%s»"), *WantOk));
		// F. Неудача и отказы.
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
		struct FCase { int32 Code; int32 A; int32 B; };
		const FCase Cases[] = { {5, 110323, 108531}, {0, 108530, 0}, {2, 108532, 0}, {3, 108533, 0}, {4, 108534, 0}, {99, 108535, 0}, {7, 0, 0} };
		for (const FCase& C : Cases)
		{
			if (!Net || !St) { break; }
			ClearBoxes(R, TEXT("самотест UpgradeStone F"));
			St->ClearShownNotices();
			PutStone(R, 401);
			Net->DebugFeedPacket(6552, Body6552(R, C.Code, C.Code == 5 ? 0 : 401, R->Weapon, 88));
			St->Tick(0.f);
			const TArray<FString>& M = St->GetShownNotices();
			TArray<FString> Want;
			if (C.A) { Want.Add(L(R, C.A)); }
			if (C.B) { Want.Add(L(R, C.B)); }
			R->Expect(M == Want, FString::Printf(TEXT("F код %d: нотисов %d (ждём %d) «%s»"), C.Code, M.Num(), Want.Num(), M.Num() ? *M[0] : TEXT("")));
		}
		ClearBoxes(R, TEXT("самотест UpgradeStone G"));
		// G. Камень доспеха 363.
		if (St)
		{
			const int32 Before = ItemOps(R);
			PutStone(R, 363);
			Dbl(R, R->CStone); Dbl(R, R->CWeapon);
			R->Expect(!St->IsPickActive() && !Asked(R), TEXT("G1 камень доспеха 363 → оружие: вопроса нет, взвод снят"));
			Dbl(R, R->CStone); Dbl(R, R->CArmor);
			R->Expect(Asked(R) && St->GetGearCell() == R->CArmor && ItemOps(R) == Before,
				FString::Printf(TEXT("G2 камень доспеха 363 → доспех %d: вопрос (1, 69)"), R->Armor));
			ClearBoxes(R, TEXT("самотест UpgradeStone H"));
			// H. Открытие сумки снимает взвод.
			Dbl(R, R->CStone);
			if (UAsdaInventorySubsystem* InvM = Sub<UAsdaInventorySubsystem>(R))
			{
				if (InvM->IsWindowOpen()) { InvM->CloseWindow(); }
				InvM->OpenWindow();
			}
			R->Expect(!St->IsPickActive() && CursorIs(R, AAsdaPlayerController::EAsdaCursor::Normal), TEXT("H открытие сумки снимает взвод, курсор обычный"));
			// I. Ключ 0.
			R->Keys->Set(TEXT("asda.UpgradeStone.Enable"), 0);
			Dbl(R, R->CStone);
			R->Expect(!St->IsPickActive(), TEXT("I ключ asda.UpgradeStone.Enable 0: взвода нет (прежний путь)"));
			R->Keys->Set(TEXT("asda.UpgradeStone.Enable"), 1);
			// K (ревизия Б1). Закрытие сумки снимает взвод (кадром позже — опрос в Tick), курсор отпущен.
			Dbl(R, R->CStone);
			if (UAsdaInventorySubsystem* InvM = Sub<UAsdaInventorySubsystem>(R)) { InvM->CloseWindow(); }
			St->Tick(0.f);
			UWorld* Wk = R->W.Get();
			AAsdaPlayerController* Pc = Wk ? Cast<AAsdaPlayerController>(Wk->GetFirstPlayerController()) : nullptr;
			R->Expect(!St->IsPickActive() && (!Pc || !Pc->bUiCursorOverride), TEXT("K1 закрытие сумки снимает взвод, захват курсора снят"));
			// K2 (Б2). Без взвода снятие НЕ трогает чужой курсор (режим самоцвета «bottle»).
			if (Pc)
			{
				Pc->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Bottle);
				Pc->bUiCursorOverride = true;
				St->CancelPick(TEXT("самотест K2"));
				R->Expect(Pc->CurrentCursor == AAsdaPlayerController::EAsdaCursor::Bottle && Pc->bUiCursorOverride,
					TEXT("K2 снятие без взвода не затирает чужой курсор (bottle) и его захват"));
				Pc->bUiCursorOverride = false;
				Pc->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Normal);
			}
			// K3 (Б1). Взвод при открытой сумке, мышь не над сумкой — курсор отпускается (над миром — курсоры мира).
			if (UAsdaInventorySubsystem* InvM = Sub<UAsdaInventorySubsystem>(R)) { InvM->OpenWindow(); }
			FSlateApplication::Get().SetCursorPos(FVector2D(2.0, 2.0));
			Dbl(R, R->CStone);
			St->Tick(0.f);
			R->Expect(St->IsPickActive() && (!Pc || !Pc->bUiCursorOverride), TEXT("K3 взвод держится, курсор вне сумки не захвачен"));
			St->CancelPick(TEXT("самотест K3"));
			// L. Окно улучшения 20000 открыто — двойной щелчок по камню и по вещи работает (ветки 20000 в эталоне нет).
			if (UAsdaItemUpgradeSubsystem* Up = Sub<UAsdaItemUpgradeSubsystem>(R))
			{
				Up->OpenWindow();
				if (UAsdaInventorySubsystem* InvM = Sub<UAsdaInventorySubsystem>(R)) { if (!InvM->IsWindowOpen()) { InvM->OpenWindow(); } }
				ClearBoxes(R, TEXT("самотест UpgradeStone L"));
				PutStone(R, 401);
				Dbl(R, R->CStone);
				const bool bArmed = St->IsPickActive();
				Dbl(R, R->CWeapon);
				R->Expect(Up->IsOpen() && bArmed && Asked(R), FString::Printf(TEXT("L при открытом окне 20000: взвод=%d, вопрос (1, 69)=%d"), bArmed ? 1 : 0, Asked(R) ? 1 : 0));
				Up->HideWindow();
				ClearBoxes(R, TEXT("самотест UpgradeStone L"));
			}
			// M (Б5). «6551 в полёте» без ответа снимается таймаутом.
			R->Keys->Set(TEXT("asda.UpgradeStone.PendingTimeoutSec"), 0.2f);
			Dbl(R, R->CStone); Dbl(R, R->CWeapon);
			St->SendRequest();
			const bool bPend = St->IsPending();
			After(R, 0.6f, [bPend](const FRef& R)
			{
				UAsdaUpgradeStoneSubsystem* St = Sub<UAsdaUpgradeStoneSubsystem>(R);
				R->Expect(bPend && St && !St->IsPending(), TEXT("M «в полёте» снят таймаутом (asda.UpgradeStone.PendingTimeoutSec)"));
				ClearBoxes(R, TEXT("самотест UpgradeStone M"));
				Finish(R);
			});
			return;
		}
		After(R, 0.3f, [](const FRef& R) { Finish(R); });
	});
}

void FAsdaUpgradeStoneSelfTest::Finish(const FRef& R)
{
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (Net) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); }
	if (UAsdaUpgradeStoneSubsystem* St = Sub<UAsdaUpgradeStoneSubsystem>(R)) { St->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
	if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R))
	{
		for (int32 C : { R->CStone, R->CWeapon, R->CArmor, R->CBox })
		{
			if (C >= 0) { Inv->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(1, C), UAsdaInventorySubsystem::EExternalRecordMode::Default); }
		}
	}
	ClearBoxes(R, TEXT("самотест UpgradeStone конец"));
	if (R->SelfScope) { R->SelfScope->Restore(); }
	if (R->Keys) { R->Keys->Restore(); }
	AsdaSelfTestKit::Finish(R->W.Get(), AsdaUpgradeStoneTestLocal::kCmd, R->bQuit, LogAsdaUpgradeStoneTest, TEXT("КАМЕНЬ-УЛУЧШЕНИЯ"), R->Ok, R->Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestUpgradeStone(
	TEXT("asda.SelfTest.UpgradeStone"),
	TEXT("Самотест переноса улучшения камнем (401/363) путём игрока: взвод двойным щелчком, курсор P0, недопустимые пары, вопрос 110322, "
	     "6551, ответы 6552 и нотисы, открытие сумки, ключ 0. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaUpgradeStoneTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaUpgradeStoneSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaUpgradeStoneTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaUpgradeStoneSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
