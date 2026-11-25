// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВИДОВ СВИТКОВ ОКНА 20000 (04.11.2026, жалоба «редкие/особые свитки не встают в ячейку»):
//   `asda.SelfTest.UpgradeScrollKinds [delay=N] [quit]`. Путь игрока, без сети (-AsdaNoNet), карта мира.
//   Клики — НАСТОЯЩЕЙ мышью: курсор в центр живой ячейки (`SetCursorPos`), нажатие/отпускание ЛКМ через вход Slate
//   (`ProcessMouseButtonDown/UpEvent`) — тот же путь, что от ОС; окно ловит его своим опросом `TickMouse`.
//   Ожидания — по эталону `uiCombineRefreshSlots@0xD28A80` (ui_core.c:48448-48700), независимо от боевого кода:
//   свиток в списке ⇔ расходник (контейнер 2) с `linkCode == 70`, строка UpgradeTable по `itemSubType`,
//   `ReqTargetGrade ∈ {5, pricetable.dw04 цели}` и класс (`ApplyItemTypeGroup` 3 — всем; 0 — оружию `itemType 9`;
//   1 — доспеху `itemType <= 4 || 8`). «Эффект после» = `RateBasePercent + процент цели + resultModel помощника 97`.
//   Для каждой цели (оружие, доспех) и каждого свитка из таблицы предметов (все `linkCode 70` и особые 23525/23526):
//     A. список окна совпадает с ожиданием;  B. клик мышью по ячейке → свиток выбран, карточка 20150 и «эффект после»;
//     C. клик по помощникам 97/98; D. «Улучшить» мышью → старт попытки; E. клик по 20003 → 6504 с парами ячеек.
//     M. ★ Д-19 (04.11.2026): матрица «грейд сервера 0…3 × грейд клиента −1…4» для оружия и доспеха (39 целей + 41798 из отказов
//        01.11): все 38 свитков в расходниках сразу, цель — кликом мыши, список окна = правило СЕРВЕРА (`CharShellItemUpgrade_0`:
//        грейд SowelCombine слово 46, кроме грейда 5 у видов 74/84/90/91; класс по типу Item_Table) при ключе
//        `asda.Upgrade.ServerGradeFilter` 1 и = правило ЭТАЛОНА (pricetable.dw04, клиентский тип) при ключе 0. Серверные грейды
//        целей — константы, выписанные из серверной SowelCombine.txt 04.11.2026 (а не из ini, который читает боевой код).
//   Итог — «ВИДЫ-СВИТКОВ ИТОГ: ok=… fail=…». Предметы кладутся боевой точкой `ApplyExternalRecord` и снимаются в конце.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaItemUpgradeSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Components/Widget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaScrollKindsTest, Log, All);

namespace AsdaScrollKindsLocal
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.UpgradeScrollKinds");
	// Перехваченная отправка.
	struct FSent { uint16 Op = 0; TArray<uint8> Body; };
	// Все свитки таблицы предметов (`linkCode 70`: 20932…20954, 69632…69641, 79778…79781) и особые 23525/23526
	//   (`linkCode 0` — у эталона в список не попадают; проверяем, что и у нас нет).
	static const int32 kScrollCodes[] = {
		20932, 20933, 20934, 20935, 20936, 20937, 20938, 20939, 20940, 20941,
		20942, 20943, 20944, 20945, 20946, 20947, 20948, 20949, 20950, 20951, 20952, 20954,
		69632, 69633, 69634, 69635, 69636, 69637, 69638, 69639, 69640, 69641,
		79778, 79779, 79780, 79781, 23525, 23526 };
	static constexpr int32 kFirstSpendCell = 40;   // ячейки расходников под пачку (40…45)
	static constexpr int32 kBatch = 6;             // видимых ячеек списка свитков
	static constexpr int32 kCellTarget = 50;       // ячейка сумки цели (вторая половина сумки ×2)
	static constexpr int32 kCellH97 = 51, kCellH98 = 52;
	static constexpr int32 kMatrixFirstCell = 2;   // расходники 2…39 — все 38 свитков матрицы разом
	// Матрица Д-19: {код, серверный грейд (SowelCombine слово 46), клиентский грейд pricetable.dw04}; тип у всех совпадает с серверным.
	struct FMatrixTarget { int32 Code; int32 ServerGrade; int32 ClientGrade; };
	static const FMatrixTarget kMatrix[] = {
		{21611, 0, -1}, {21394, 0, 0}, {21598, 0, 1}, {22544, 0, 2}, {25594, 0, 3},
		{21491, 1, -1}, {21447, 1, 0}, {21734, 1, 1}, {21790, 1, 2}, {24955, 1, 3},
		{20715, 2, -1}, {20552, 2, 0}, {21550, 2, 1}, {21949, 2, 2}, {23330, 2, 3}, {27495, 2, 4},
		{20556, 3, -1}, {20555, 3, 0}, {21567, 3, 1}, {22522, 3, 2}, {23627, 3, 3},
		{25409, 0, -1}, {22818, 0, 0}, {23363, 0, 1}, {25362, 0, 2}, {27664, 0, 3},
		{23020, 1, 0}, {24155, 1, 1}, {23417, 1, 2}, {26670, 1, 3}, {41798, 1, 0},
		{23027, 2, 0}, {23038, 2, 1}, {23056, 2, 2}, {24985, 2, 3}, {27518, 2, 4},
		{23106, 3, 0}, {23387, 3, 1}, {23395, 3, 2}, {33896, 3, 3},
		// Класс B правки снаряжения 07.11.2026: грейд сервера = новое слово 46 SowelCombine (target_B.json).
		{73197, 2, 2}, {73236, 0, 0}, {73305, 2, 2}, {73560, 3, 3},
		// Класс C правки снаряжения 07.11.2026: ступени 1/40/60/70, target_C.json.
		{78076, 0, 0}, {77190, 2, 2}, {78105, 3, 3}, {79189, 3, 3}, {78162, 2, 2},
		// Правка слотов 07.11.2026: ступени рядов тыквы и оленей, target_S.json.
		{73515, 0, 0}, {73453, 0, 0}, {31428, 2, 2}, {75888, 3, 3}, {76210, 0, 0},
		// Итоговая волна 07.11.2026: похожие вещи и «только слот», грейд сервера из образца.
		{76035, 3, 3}, {73416, 2, 3}, {73722, 0, 3}, {75798, 0, 3}, {73355, 0, 0}, {75990, 2, 0} };
}

struct FAsdaScrollKindsSelfTest
{
	struct FTarget { int32 Code = 0; int32 Grade = -1; int32 Type = -1; const TCHAR* Name = TEXT(""); };
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		uint32 CapGen = 0;
		TArray<AsdaScrollKindsLocal::FSent> Sent;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TArray<FTarget> Targets;
		TUniquePtr<FAsdaCVarScope> Keys;   // ключ Д-19 на время прогона
		int32 MatrixI = 0;                 // позиция матрицы
		int32 MatrixKey = 1;               // проход матрицы: 1 — по серверу, 0 — как оригинал
		int32 TargetI = 0;
		TArray<int32> Expected;   // ожидаемые свитки текущей цели
		int32 BatchStart = 0;
		TArray<int32> Batch;
		int32 CellI = 0;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaScrollKindsTest, Log, TEXT("ВИДЫ-СВИТКОВ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaScrollKindsTest, Error, TEXT("ВИДЫ-СВИТКОВ FAIL %s"), *What); }
		}
	};
	using FRef = TSharedRef<FRun>;

	template<class T> static T* Sub(const FRef& R)
	{
		UWorld* W = R->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<T>() : nullptr;
	}
	static void After(const FRef& R, float Sec, TFunction<void(const FRef&)> Fn)
	{
		UWorld* W = R->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
	}
	// Центр живого виджета в абсолютных координатах Slate (те же, что у `IsCursorOverWidget`).
	static bool CenterOf(UWidget* Wd, FVector2D& Out)
	{
		if (!Wd || !Wd->GetCachedWidget().IsValid()) { return false; }
		const FGeometry& G = Wd->GetCachedGeometry();
		const FVector2D Sz = G.GetAbsoluteSize();
		if (Sz.X < 1.0 || Sz.Y < 1.0) { return false; }
		Out = G.GetAbsolutePosition() + Sz * 0.5;
		return true;
	}
	// Клик настоящей мышью: курсор → (кадр наведения) → нажатие → (кадр опроса) → отпускание → Then.
	static void Click(const FRef& R, UWidget* Wd, TFunction<void(const FRef&, bool)> Then)
	{
		FVector2D P;
		if (!CenterOf(Wd, P) || !FSlateApplication::IsInitialized()) { Then(R, false); return; }
		FSlateApplication::Get().SetCursorPos(P);
		After(R, 0.12f, [P, Then](const FRef& R)
		{
			FPointerEvent Down(0, P, P, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
			FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, Down);
			After(R, 0.12f, [P, Then](const FRef& R)
			{
				FPointerEvent Up(0, P, P, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState());
				FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
				After(R, 0.12f, [Then](const FRef& R) { Then(R, true); });
			});
		});
	}
	static void PutCell(const FRef& R, int32 Code, int32 Cont, int32 Cell, int32 Qty, int32 Max = 0)
	{
		UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
		if (!Inv) { return; }
		FAsdaItemRecordWriter Wr; Wr.Code(Code).Container(Cont).Slot(Cell).Second(-1).Amount(Qty).GemGrade(0).Byte25(0).Byte26(Max).EnchantPercent(0);
		Inv->ApplyExternalRecord(Wr.ToArray(), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	static void ClearCell(const FRef& R, int32 Cont, int32 Cell)
	{
		if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R))
		{
			Inv->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(Cont, Cell), UAsdaInventorySubsystem::EExternalRecordMode::Default);
		}
	}
	// Ожидание по эталону: виден ли свиток Code для цели T (см. шапку).
	static bool EtalonListed(const UAsdaItemTableSubsystem* IT, int32 Code, const FTarget& T, int32& OutRate)
	{
		OutRate = 0;
		const FAsdaItemRecord* SR = IT->GetItem(Code);
		if (!SR || SR->LinkCode != 70) { return false; }
		FAsdaUpgradetableRow Ev;
		if (!IT->GetUpgradeTable(SR->ItemSubType, Ev)) { return false; }
		OutRate = Ev.RateBasePercent;
		if (!(Ev.ReqTargetGrade == 5 || Ev.ReqTargetGrade == T.Grade)) { return false; }
		if (Ev.ApplyItemTypeGroup == 3) { return true; }
		if (T.Type == 9) { return Ev.ApplyItemTypeGroup == 0; }
		if ((uint32)T.Type <= 4u || T.Type == 8) { return Ev.ApplyItemTypeGroup == 1; }
		return false;
	}

	static void Start(UWorld* W, bool bQuit);
	static void NextTarget(const FRef& R);
	static void NextBatch(const FRef& R);
	static void ClickCell(const FRef& R);
	static void UpgradeFlow(const FRef& R);
	static void Finish(const FRef& R);
	static void MatrixNext(const FRef& R);
	// Ожидание матрицы: виден ли свиток Code у цели грейда Grade и типа Type (источник грейда/типа выбирает вызывающий).
	static bool RuleListed(const UAsdaItemTableSubsystem* IT, int32 Code, int32 Grade, int32 Type)
	{
		const FAsdaItemRecord* SR = IT->GetItem(Code);
		if (!SR || SR->LinkCode != 70) { return false; }
		FAsdaUpgradetableRow Ev;
		if (!IT->GetUpgradeTable(SR->ItemSubType, Ev)) { return false; }
		if (!(Ev.ReqTargetGrade == 5 || Ev.ReqTargetGrade == Grade)) { return false; }
		if (Ev.ApplyItemTypeGroup == 3) { return true; }
		if (Type == 9) { return Ev.ApplyItemTypeGroup == 0; }
		if ((uint32)Type <= 4u || Type == 8) { return Ev.ApplyItemTypeGroup == 1; }
		return false;
	}
};

void FAsdaScrollKindsSelfTest::Start(UWorld* W, bool bQuit)
{
	using namespace AsdaScrollKindsLocal;
	FRef R = MakeShared<FRun>();
	R->W = W; R->bQuit = bQuit;
	UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
	UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
	UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (!S || !Inv || !IT || !Net) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("нет подсистем")); return; }
	UE_LOG(LogAsdaScrollKindsTest, Log, TEXT("ВИДЫ-СВИТКОВ старт, язык %s"), FAsdaLang::IsRu() ? TEXT("RU") : TEXT("EN"));
	// Цели: оружие 22818 (как у основного самотеста) и доспех 70862 «Master Shinobi Pants» (из жалобы).
	for (const TPair<int32, const TCHAR*>& P : { TPair<int32, const TCHAR*>(22818, TEXT("оружие")), TPair<int32, const TCHAR*>(70862, TEXT("доспех")) })
	{
		FTarget T; T.Code = P.Key; T.Name = P.Value;
		const FAsdaItemRecord* TR = IT->GetItem(T.Code);
		FAsdaPriceRow Pr;
		if (TR && IT->GetPrice(T.Code, Pr)) { T.Type = TR->ItemType; T.Grade = Pr.dw04; }
		R->Expect(TR && T.Grade >= 0, FString::Printf(TEXT("0 цель %s %d: тип %d, грейд pricetable.dw04 %d"), T.Name, T.Code, T.Type, T.Grade));
		R->Targets.Add(T);
	}
	for (int32 C = kFirstSpendCell; C < kFirstSpendCell + kBatch; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(2, C);
		if (It && !It->IsEmpty()) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("ячейки расходников 40…45 заняты")); return; }
	}
	for (int32 C : { kCellTarget, kCellH97, kCellH98 })
	{
		const FAsdaItemInstance* It = Inv->GetCell(1, C);
		if (It && !It->IsEmpty()) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("ячейки сумки 50…52 заняты")); return; }
	}
	for (int32 C = kMatrixFirstCell; C < kMatrixFirstCell + (int32)UE_ARRAY_COUNT(kScrollCodes); ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(2, C);
		if (It && !It->IsEmpty()) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("ячейки расходников 2…39 заняты")); return; }
	}
	R->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
	R->Keys->Set(TEXT("asda.Upgrade.ServerGradeFilter"), 1);
	TWeakPtr<FRun> WR = R;
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		TSharedPtr<FRun> Run = WR.Pin();
		if (!Run) { return; }
		FSent Sn; Sn.Op = Op; if (P && Len > 0) { Sn.Body.Append(P, Len); }
		Run->Sent.Add(MoveTemp(Sn));
	}, 0, kCmd);
	if (R->CapGen == 0) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
	PutCell(R, 252, 1, kCellH97, 3);   // помощник 97 (+5 %)
	PutCell(R, 255, 1, kCellH98, 2);   // помощник 98
	if (!S->IsOpen()) { S->OpenWindow(); }
	After(R, 0.5f, [](const FRef& R) { NextTarget(R); });
}

void FAsdaScrollKindsSelfTest::NextTarget(const FRef& R)
{
	using namespace AsdaScrollKindsLocal;
	if (R->TargetI >= R->Targets.Num())
	{
		// M. Матрица Д-19: все свитки в расходники 2…39 разом, затем цели по одной.
		for (int32 C = 0; C < kBatch; ++C) { ClearCell(R, 2, kFirstSpendCell + C); }
		for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(kScrollCodes); ++I) { PutCell(R, kScrollCodes[I], 2, kMatrixFirstCell + I, 2); }
		R->MatrixI = 0; R->MatrixKey = 1;
		R->Keys->Set(TEXT("asda.Upgrade.ServerGradeFilter"), 1);
		After(R, 0.3f, [](const FRef& R) { MatrixNext(R); });
		return;
	}
	const FTarget& T = R->Targets[R->TargetI];
	const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
	R->Expected.Reset();
	for (int32 Code : kScrollCodes)
	{
		int32 Rate = 0;
		if (EtalonListed(IT, Code, T, Rate)) { R->Expected.Add(Code); }
	}
	UE_LOG(LogAsdaScrollKindsTest, Log, TEXT("ВИДЫ-СВИТКОВ цель %s %d: по эталону годных свитков %d"), T.Name, T.Code, R->Expected.Num());
	PutCell(R, T.Code, 1, kCellTarget, 1, /*предел попыток*/8);
	R->BatchStart = 0;
	After(R, 0.4f, [](const FRef& R) { NextBatch(R); });
}

void FAsdaScrollKindsSelfTest::NextBatch(const FRef& R)
{
	using namespace AsdaScrollKindsLocal;
	// Пачка: 6 кодов подряд из ВСЕГО списка (годные и негодные вперемешку — проверяем и отсев).
	if (R->BatchStart >= UE_ARRAY_COUNT(kScrollCodes))
	{
		ClearCell(R, 1, kCellTarget);
		++R->TargetI;
		After(R, 0.3f, [](const FRef& R) { NextTarget(R); });
		return;
	}
	for (int32 C = 0; C < kBatch; ++C) { ClearCell(R, 2, kFirstSpendCell + C); }
	R->Batch.Reset();
	for (int32 I = 0; I < kBatch && R->BatchStart + I < (int32)UE_ARRAY_COUNT(kScrollCodes); ++I)
	{
		const int32 Code = kScrollCodes[R->BatchStart + I];
		PutCell(R, Code, 2, kFirstSpendCell + I, 2);
		if (R->Expected.Contains(Code)) { R->Batch.Add(Code); }
	}
	R->BatchStart += kBatch;
	// Цель выбирается мышью — как игрок: первая видимая ячейка сетки, где лежит цель.
	After(R, 1.2f, [](const FRef& R)   // > kTickValidateMs: список перестроен опросом окна
	{
		UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
		const FTarget& T = R->Targets[R->TargetI];
		int32 VisCell = -1;
		const FString Dump = S->DumpState();
		for (int32 V = 0; V < UAsdaItemUpgradeSubsystem::kGridCells && VisCell < 0; ++V)
		{
			if (Dump.Contains(FString::Printf(TEXT("сетка[%2d] код=%d к=1 яч=%d "), S->GridListIndexOfVisible(V), T.Code, AsdaScrollKindsLocal::kCellTarget))) { VisCell = V; }
		}
		Click(R, VisCell >= 0 ? S->DebugCellWidget(0, VisCell) : nullptr, [VisCell](const FRef& R, bool bClicked)
		{
			UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
			const FTarget& T = R->Targets[R->TargetI];
			int32 Tg, Sc, H7, H8; FString Eff;
			S->DebugSelection(Tg, Sc, H7, H8, Eff);
			R->Expect(bClicked && Tg == T.Code, FString::Printf(TEXT("A0 %s: клик мышью по сетке (видимая %d) → цель %d"), T.Name, VisCell, Tg));
			const TArray<int32> Got = S->DebugScrollCodes();
			TArray<int32> Want;
			for (int32 I = 0; I < AsdaScrollKindsLocal::kBatch; ++I)
			{
				const int32 Pos = R->BatchStart - AsdaScrollKindsLocal::kBatch + I;
				if (Pos < (int32)UE_ARRAY_COUNT(AsdaScrollKindsLocal::kScrollCodes) && R->Expected.Contains(AsdaScrollKindsLocal::kScrollCodes[Pos])) { Want.Add(AsdaScrollKindsLocal::kScrollCodes[Pos]); }
			}
			auto Join = [](const TArray<int32>& A) { FString S; for (int32 V : A) { S += FString::Printf(TEXT("%d "), V); } return S; };
			R->Expect(Got == Want, FString::Printf(TEXT("A1 %s: список свитков окна [%s] = эталон [%s]"), T.Name, *Join(Got), *Join(Want)));
			R->CellI = 0;
			ClickCell(R);
		});
	});
}

void FAsdaScrollKindsSelfTest::ClickCell(const FRef& R)
{
	UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
	const TArray<int32> Got = S->DebugScrollCodes();
	if (R->CellI >= FMath::Min(Got.Num(), (int32)AsdaScrollKindsLocal::kBatch))
	{
		// По одному полному прогону «Улучшить» на пачку (последний выбранный свиток), если в пачке есть годные.
		if (Got.Num() > 0) { UpgradeFlow(R); }
		else { After(R, 0.1f, [](const FRef& R) { NextBatch(R); }); }
		return;
	}
	const int32 Cell = R->CellI;
	const int32 Code = Got[Cell];
	Click(R, S->DebugCellWidget(1, Cell), [Cell, Code](const FRef& R, bool bClicked)
	{
		UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
		const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
		const FTarget& T = R->Targets[R->TargetI];
		int32 Tg, Sc, H7, H8; FString Eff;
		S->DebugSelection(Tg, Sc, H7, H8, Eff);
		int32 Rate = 0; EtalonListed(IT, Code, T, Rate);
		const FString WantEff = FString::Printf(TEXT("%d%%"), Rate);   // процент цели 0, помощника 97 нет
		R->Expect(bClicked && Sc == Code && Eff == WantEff,
			FString::Printf(TEXT("B %s: клик мышью по свитку [%d] %d → выбран %d, эффект после «%s» (ждём «%s»)"), T.Name, Cell, Code, Sc, *Eff, *WantEff));
		++R->CellI;
		ClickCell(R);
	});
}

void FAsdaScrollKindsSelfTest::UpgradeFlow(const FRef& R)
{
	UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
	// C. Помощники 97 и 98 мышью.
	Click(R, S->DebugCellWidget(2, 0), [](const FRef& R, bool b97)
	{
		UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
		Click(R, S->DebugCellWidget(3, 0), [b97](const FRef& R, bool b98)
		{
			UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
			const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
			const FTarget& T = R->Targets[R->TargetI];
			int32 Tg, Sc, H7, H8; FString Eff;
			S->DebugSelection(Tg, Sc, H7, H8, Eff);
			int32 Rate = 0; EtalonListed(IT, Sc, T, Rate);
			const FString WantEff = FString::Printf(TEXT("%d%%"), Rate + 5);   // resultModel помощника 252 = 5
			R->Expect(b97 && b98 && H7 == 252 && H8 == 255 && Eff == WantEff,
				FString::Printf(TEXT("C %s: помощники мышью 97=%d 98=%d, эффект «%s» (ждём «%s»)"), T.Name, H7, H8, *Eff, *WantEff));
			// D. «Улучшить» мышью → старт попытки (окно занято, эффект 20070).
			Click(R, S->DebugCellWidget(5, 0), [Sc](const FRef& R, bool bBtn)
			{
				UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
				const FTarget& T = R->Targets[R->TargetI];
				R->Expect(bBtn && !S->DebugIsIdle(), FString::Printf(TEXT("D %s: «Улучшить» мышью со свитком %d → попытка начата"), T.Name, Sc));
				S->DebugFinishEffect(EAsdaUpgradeFx::Effect1);   // конец анимации 20070 (событие 0x18) — иконка взведена
				R->Sent.Reset();
				Click(R, S->DebugCellWidget(6, 0), [Sc](const FRef& R, bool bIcon)
				{
					UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
					UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(R);
					const FTarget& T = R->Targets[R->TargetI];
					const AsdaScrollKindsLocal::FSent* P = R->Sent.FindByPredicate([](const AsdaScrollKindsLocal::FSent& X) { return X.Op == 6504; });
					// Ячейка выбранного свитка в расходниках.
					int32 ScCell = -1;
					for (int32 C = AsdaScrollKindsLocal::kFirstSpendCell; C < AsdaScrollKindsLocal::kFirstSpendCell + AsdaScrollKindsLocal::kBatch; ++C)
					{
						const FAsdaItemInstance* It = Inv->GetCell(2, C);
						if (It && !It->IsEmpty() && It->ItemCode == Sc) { ScCell = C; break; }
					}
					bool bFields = false;
					FString Got = TEXT("нет 6504");
					if (P && P->Body.Num() >= 12)
					{
						const uint8* B = P->Body.GetData();
						const int32 TSlot = AsdaBytes::RdI16(B + 1), SSlot = AsdaBytes::RdI16(B + 4), S97 = AsdaBytes::RdI16(B + 7), S98 = AsdaBytes::RdI16(B + 10);
						Got = FString::Printf(TEXT("цель %d/%d свиток %d/%d п97 %d/%d п98 %d/%d"), B[0], TSlot, B[3], SSlot, B[6], S97, B[9], S98);
						bFields = B[0] == 1 && TSlot == AsdaScrollKindsLocal::kCellTarget && B[3] == 2 && SSlot == ScCell
							&& B[6] == 1 && S97 == AsdaScrollKindsLocal::kCellH97 && B[9] == 1 && S98 == AsdaScrollKindsLocal::kCellH98;
					}
					R->Expect(bIcon && bFields, FString::Printf(TEXT("E %s: клик по 20003 → 6504 (%s)"), T.Name, *Got));
					// Сброс попытки: окно закрыть/открыть (`uiCombineWindowResetOpen`), замок N-2 снимается переоткрытием.
					S->HideWindow();
					S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
					S->OpenWindow();
					After(R, 0.3f, [](const FRef& R) { NextBatch(R); });
				});
			});
		});
	});
}

void FAsdaScrollKindsSelfTest::MatrixNext(const FRef& R)
{
	using namespace AsdaScrollKindsLocal;
	if (R->MatrixI >= (int32)UE_ARRAY_COUNT(kMatrix))
	{
		if (R->MatrixKey == 1)
		{
			// Второй проход — ключ 0: окно обязано вернуть поведение эталона (клиентский грейд и тип).
			R->MatrixKey = 0; R->MatrixI = 0;
			R->Keys->Set(TEXT("asda.Upgrade.ServerGradeFilter"), 0);
			After(R, 0.2f, [](const FRef& R) { MatrixNext(R); });
			return;
		}
		ClearCell(R, 1, kCellTarget);
		Finish(R);
		return;
	}
	const FMatrixTarget M = kMatrix[R->MatrixI];
	const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
	const FAsdaItemRecord* TR = IT->GetItem(M.Code);
	FAsdaPriceRow Pr;
	const bool bData = TR && IT->GetPrice(M.Code, Pr) && Pr.dw04 == M.ClientGrade;
	if (R->MatrixKey == 1)
	{
		R->Expect(bData, FString::Printf(TEXT("M0 цель %d: в таблицах клиента тип %d, pricetable.dw04 %d (константа %d)"),
			M.Code, TR ? TR->ItemType : -1, bData ? Pr.dw04 : -99, M.ClientGrade));
	}
	if (!bData) { ++R->MatrixI; After(R, 0.05f, [](const FRef& R) { MatrixNext(R); }); return; }
	ClearCell(R, 1, kCellTarget);
	PutCell(R, M.Code, 1, kCellTarget, 1, /*предел попыток*/8);
	After(R, 1.2f, [M](const FRef& R)   // > kTickValidateMs: сетка перестроена опросом окна
	{
		UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
		const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
		int32 VisCell = -1;
		const FString Dump = S->DumpState();
		for (int32 V = 0; V < UAsdaItemUpgradeSubsystem::kGridCells && VisCell < 0; ++V)
		{
			if (Dump.Contains(FString::Printf(TEXT("сетка[%2d] код=%d к=1 яч=%d "), S->GridListIndexOfVisible(V), M.Code, AsdaScrollKindsLocal::kCellTarget))) { VisCell = V; }
		}
		const int32 Type = IT->GetItem(M.Code)->ItemType;
		Click(R, VisCell >= 0 ? S->DebugCellWidget(0, VisCell) : nullptr, [M, Type](const FRef& R, bool bClicked)
		{
			UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R);
			const UAsdaItemTableSubsystem* IT = Sub<UAsdaItemTableSubsystem>(R);
			int32 Tg, Sc, H7, H8; FString Eff;
			S->DebugSelection(Tg, Sc, H7, H8, Eff);
			const int32 Grade = (R->MatrixKey == 1) ? M.ServerGrade : M.ClientGrade;
			TArray<int32> Want;
			for (int32 Code : AsdaScrollKindsLocal::kScrollCodes) { if (RuleListed(IT, Code, Grade, Type)) { Want.Add(Code); } }
			const TArray<int32> Got = S->DebugScrollCodes();
			auto Join = [](const TArray<int32>& A) { FString J; for (int32 V : A) { J += FString::Printf(TEXT("%d "), V); } return J; };
			R->Expect(bClicked && Tg == M.Code && Got == Want,
				FString::Printf(TEXT("M%d %s %d (тип %d, грейд сервера %d, клиента %d): свитки окна [%s] = %s [%s]"),
					R->MatrixKey, Type == 9 ? TEXT("оружие") : TEXT("доспех"), M.Code, Type, M.ServerGrade, M.ClientGrade, *Join(Got),
					R->MatrixKey == 1 ? TEXT("сервер") : TEXT("эталон"), *Join(Want)));
			++R->MatrixI;
			After(R, 0.05f, [](const FRef& R) { MatrixNext(R); });
		});
	});
}

void FAsdaScrollKindsSelfTest::Finish(const FRef& R)
{
	using namespace AsdaScrollKindsLocal;
	UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(R);
	if (Net) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); }
	for (int32 C = 0; C < kBatch; ++C) { ClearCell(R, 2, kFirstSpendCell + C); }
	for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(kScrollCodes); ++I) { ClearCell(R, 2, kMatrixFirstCell + I); }
	for (int32 C : { kCellTarget, kCellH97, kCellH98 }) { ClearCell(R, 1, C); }
	if (UAsdaItemUpgradeSubsystem* S = Sub<UAsdaItemUpgradeSubsystem>(R)) { S->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
	if (R->SelfScope) { R->SelfScope->Restore(); }
	if (R->Keys) { R->Keys->Restore(); }
	AsdaSelfTestKit::Finish(R->W.Get(), kCmd, R->bQuit, LogAsdaScrollKindsTest, TEXT("ВИДЫ-СВИТКОВ"), R->Ok, R->Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestUpgradeScrollKinds(
	TEXT("asda.SelfTest.UpgradeScrollKinds"),
	TEXT("Самотест окна 20000 по ВСЕМ видам свитков (обычные, особые, «сильные», 100 %, оружие/доспех) и помощникам 97/98: "
	     "список против эталона, клики настоящей мышью, «Улучшить», 6504. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaScrollKindsLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaScrollKindsSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaScrollKindsLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaScrollKindsSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
